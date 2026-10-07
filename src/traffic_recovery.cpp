// =====================================================================================
//  Physical traffic recovery: immutable frame observation, bounded control rollouts,
//  complete vehicle footprints, committed manoeuvres and persistent safe holding.
// =====================================================================================
#include "traffic_recovery.h"
#include "game.h"
#include "datafile.h"
#include "traffic.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>

const char* gTrafficConfigPath = "assets/data/traffic.cfg";

void LoadTrafficRecords(const char* type, TrafficField* fields, int count) {
    static const char* const known[] = { "RECOVERY", "YIELD", "INCIDENT", "TURN" };
    for (const DataRecord& r : ReadDataFile(gTrafficConfigPath)) {
        bool knownType = false;
        for (const char* k : known) knownType = knownType || r.Is(k);
        // Each record type is validated by its own loader; the RECOVERY loader also
        // reports record types that no loader understands.
        if (!knownType) {
            if (TextIsEqual(type, "RECOVERY")) TraceLog(LOG_WARNING, "TRAFFIC DATA line %d: unknown record type", r.line);
            continue;
        }
        if (!r.Is(type)) continue;
        bool found = false;
        if (r.size() == 3) {
            for (int k = 0; k < count; k++) {
                TrafficField& f = fields[k];
                if (r[1] != f.name) continue;
                found = true;
                char* end = nullptr;
                const char* begin = r[2].c_str();
                float value = strtof(begin, &end);
                if (!end || end == begin || *end != '\0' || !std::isfinite(value) || value < f.low || value > f.high)
                    TraceLog(LOG_WARNING, "TRAFFIC DATA line %d: invalid %s, keeping %.3f", r.line, f.name, *f.value);
                else *f.value = value;
                break;
            }
        }
        if (!found) TraceLog(LOG_WARNING, "TRAFFIC DATA line %d: unknown or malformed %s record", r.line, type);
    }
}

namespace {
std::array<double, (size_t)DecisionStage::COUNT> stageMs{};

constexpr float PREDICT_STEP = 1.0f / 240.0f;
constexpr float OBB_GROW_RADIUS = 1.414214f;  // rounded above sqrt(2): both half-extents grow
constexpr int MAX_PATH_POINTS = 96;
constexpr int MAX_NEARBY = 1024;
constexpr int MAX_TIMINGS = 16384;
constexpr int MAX_FORECAST_SAMPLES = 962;     // 4 s at 240 Hz, including the initial pose
constexpr float HOLD_RECHECK = 2.0f;          // s: a hold is searched again at least this often
constexpr float COVER_TIME = 4.0f / 60.0f;     // s of extra moving time a full immediate check validates

struct Settings {
    float forwardSpeed = 75, reverseSpeed = 60;
    float horizon = 2.4f, stopTail = 0.5f, clearance = 1;
    float planningInterval = 0.25f, commitment = 0.45f, hysteresis = 6;
    float stallTime = 1, nearbyRadius = 900;
    float planningSteps = 1200;              // shared rollout force steps per 1/60 s frame
};

const Settings& Tuning() {
    static Settings settings;
    static bool loaded = false;
    if (loaded) return settings;
    loaded = true;
    TrafficField fields[] = {
        { "forward_speed", &settings.forwardSpeed, 20, 100 },
        { "reverse_speed", &settings.reverseSpeed, 15, 80 },
        { "horizon", &settings.horizon, 1, 4 },
        { "stop_tail", &settings.stopTail, 0.25f, 1 },
        { "clearance", &settings.clearance, 0.25f, 1.6f },
        { "planning_interval", &settings.planningInterval, 0.1f, 1 },
        { "commitment", &settings.commitment, 0.15f, 1 },
        { "hysteresis", &settings.hysteresis, 0, 30 },
        { "stall_time", &settings.stallTime, 0.5f, 3 },
        { "nearby_radius", &settings.nearbyRadius, 300, 1500 },
        { "planning_steps", &settings.planningSteps, 300, 20000 }
    };
    LoadTrafficRecords("RECOVERY", fields, (int)(sizeof(fields) / sizeof(fields[0])));
    settings.stopTail = std::min(settings.stopTail, settings.horizon * 0.5f);
    return settings;
}

struct PathPoint { Vector2 pos{}; float distance = 0; };
struct ObservedVehicle {
    bool active = false, rail = false;
    Vector2 pos{}, vel{};
    float angle = 0, angVel = 0, width = 0, length = 0;
    float pathDistance = 0, speed = 0, shift = 0, radius = 0, routeGap = 0;
    float shiftFrom = 0, shiftTo = 0, shiftS0 = 0, shiftS1 = 0;   // lane-change profile
    float shiftSpan = 0;                     // how far the lane offset can move the centre
    float minDistance = -1e9f;               // a yielding retreat stops here
    float maxDistance = 1e9f;                // the planned stop: red light, queue, person
    float blend = 0, blendAngle = 0;
    Vector2 blendPos{};
    // A stopped rail car or a (nearly) motionless body keeps one pose after the
    // first forecast sample; 'drift' bounds any residual motion over 4 s.
    bool stationary = false;
    float drift = 0;
    int pathCount = 0;
    std::array<PathPoint, MAX_PATH_POINTS> path{};
};
struct ObservedPerson { Vector2 pos{}, vel{}; bool active = false; };
std::vector<ObservedVehicle> observedVehicles;
std::vector<ObservedPerson> observedPeople;
Game* observedGame = nullptr;

struct ForecastSample { OBB box{}; float sweptPad = 0; };
struct ForecastRow {
    uint32_t epoch = 0;                      // rows of an older epoch are empty
    int count = 0;
    bool constantReady = false;
    ForecastSample constant{};               // stationary actors: every sample after the first
    std::array<ForecastSample, MAX_FORECAST_SAMPLES> samples{};
    // Rail actors: the route centre and direction of each sample, without the box.
    int centreCount = 0;
    std::array<Vector2, MAX_FORECAST_SAMPLES> centre{}, direction{};
};
std::vector<ForecastRow> vehicleForecasts;
// A new snapshot or force step empties every row by starting a new epoch, without
// touching the rows (each is ~46 KB; resetting them all cost a cache miss apiece).
uint32_t forecastEpoch = 1;
float predictionStep = PREDICT_STEP;
float predictionControlInterval = 1.0f / 60.0f;

void SetControlInterval(float dt) {
    int substeps = std::clamp((int)ceilf(dt * 240 - 0.01f), 1, 8);
    // Match the 60/20 Hz fixtures. Higher frame rates use the same force model at
    // a bounded 240 Hz forecast cadence, never unbounded work as frame dt shrinks.
    float h = std::max(PREDICT_STEP, dt / substeps);
    if (fabsf(h - predictionStep) > 1e-7f) forecastEpoch++;
    predictionStep = h;
    predictionControlInterval = std::max(PREDICT_STEP, dt);
}

using Clock = std::chrono::steady_clock;
struct Timing {
    bool pending = false;
    double frameMs = 0, totalMs = 0, worstMs = 0;
    int frames = 0, samples = 0, cursor = 0;
    int plans = 0, rejected = 0, holds = 0;
    int deferredFrames = 0, unchangedHolds = 0, maxWaitFrames = 0, coveredChecks = 0;
    std::array<double, MAX_TIMINGS> values{};
} timing;
Timing decisionTiming;

// Deterministic work counters beside wall-clock timing: they identify which stage
// produces a CPU spike without depending on the machine's timer resolution.
struct WorkFrame {
    double ms = 0, gatherMs = 0, immediateMs = 0, planMs = 0, rejoinMs = 0;
    int recovering = 0, gathers = 0, immediate = 0, tracking = 0, candidates = 0, rejoins = 0;
    long long steps = 0, actorTests = 0, overlaps = 0, forecasts = 0;
    int nearbyMax = 0;
};
constexpr int WORST_WORK = 6;
struct WorkProfile {
    WorkFrame current, total;
    std::array<WorkFrame, WORST_WORK> worst{};
    int frames = 0;
} work;
// Shared planning budget of the current frame.
struct PlanBudget {
    long long steps = 0;                     // planning rollout force steps used
    int oldestWait = 0;                      // longest wait among pending jobs at frame start
} budget;

void AddWork(WorkFrame& to, const WorkFrame& w) {
    to.ms += w.ms; to.gatherMs += w.gatherMs; to.immediateMs += w.immediateMs;
    to.planMs += w.planMs; to.rejoinMs += w.rejoinMs;
    to.recovering += w.recovering; to.gathers += w.gathers; to.immediate += w.immediate;
    to.tracking += w.tracking; to.candidates += w.candidates; to.rejoins += w.rejoins;
    to.steps += w.steps; to.actorTests += w.actorTests; to.overlaps += w.overlaps; to.forecasts += w.forecasts;
    to.nearbyMax = std::max(to.nearbyMax, w.nearbyMax);
}

double Milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void FinishTimingFrame(Timing& record = timing) {
    if (!record.pending) return;
    if (&record == &timing) {
        work.current.ms = record.frameMs;
        AddWork(work.total, work.current);
        work.frames++;
        int slot = 0;
        for (int k = 1; k < WORST_WORK; k++) if (work.worst[k].ms < work.worst[slot].ms) slot = k;
        if (work.current.ms > work.worst[slot].ms) work.worst[slot] = work.current;
        work.current = WorkFrame{};
    }
    record.totalMs += record.frameMs;
    record.worstMs = std::max(record.worstMs, record.frameMs);
    record.values[record.cursor] = record.frameMs;
    record.cursor = (record.cursor + 1) % MAX_TIMINGS;
    record.samples = std::min(record.samples + 1, MAX_TIMINGS);
    record.frames++;
    record.pending = false;
}

Vector2 SampleObservedPath(const ObservedVehicle& o, float distance) {
    if (o.pathCount == 0) return o.pos;
    if (o.pathCount == 1) return o.path[0].pos;
    if (distance <= o.path[0].distance) return o.path[0].pos;
    // Cumulative distances are non-decreasing: find the first point at or beyond
    // the distance, which is the segment end the linear scan would have chosen.
    const PathPoint* end = std::lower_bound(o.path.begin() + 1, o.path.begin() + o.pathCount, distance,
        [](const PathPoint& p, float d) { return p.distance < d; });
    if (end != o.path.begin() + o.pathCount) {
        const PathPoint& a = *(end - 1);
        const PathPoint& b = *end;
        return LerpV(a.pos, b.pos, Saturate((distance - a.distance) / std::max(0.001f, b.distance - a.distance)));
    }
    // A snapshot cannot plan another junction. Extend only its final known tangent.
    const PathPoint& last = o.path[o.pathCount - 1];
    return last.pos + Norm(last.pos - o.path[o.pathCount - 2].pos) * (distance - last.distance);
}

Vector2 ObservedRailCentre(const ObservedVehicle& o, float time, Vector2* heading = nullptr) {
    float distance = Clampf(o.pathDistance + o.speed * time, o.minDistance, o.maxDistance);
    Vector2 direction;
    Vector2 pos = ShiftedRailPose([&](float d) { return SampleObservedPath(o, d); }, distance, o.length,
                                  o.shiftFrom, o.shiftTo, o.shiftS0, o.shiftS1, o.angle, &direction);
    if (heading) *heading = direction;
    return pos;
}

OBB PredictObserved(const ObservedVehicle& o, float time, float grow) {
    // A legacy U-turn can replace its rail before posing the body. The
    // actual common observation is authoritative at the initial instant.
    if (time <= 0) return MakeOBB(o.pos, o.angle, o.width * 0.5f + grow, o.length * 0.5f + grow);
    Vector2 pos = o.pos + o.vel * time;
    float angle = o.angle + o.angVel * time;
    if (o.rail && o.pathCount > 1) {
        Vector2 direction;
        pos = ObservedRailCentre(o, time, &direction);
        angle = AngleOf(direction);
        if (o.blend > 0) {
            float blend = std::max(0.0f, o.blend - time * 1.4f);
            float weight = SmoothStep(0, 1, blend);
            pos = LerpV(pos, o.blendPos, weight);
            angle += WrapAngle(o.blendAngle - angle) * weight;
        }
    }
    return MakeOBB(pos, angle, o.width * 0.5f + grow, o.length * 0.5f + grow);
}

float PoseMotionBound(const OBB& previous, const OBB& current, float radius) {
    float angle = fabsf(atan2f(Cross(previous.ax[1], current.ax[1]), Dot(previous.ax[1], current.ax[1])));
    return Dist(previous.c, current.c) + radius * angle;
}

const ForecastSample& ForecastAt(int actor, int step) {
    ForecastRow& row = vehicleForecasts[actor];
    if (row.epoch != forecastEpoch) { row.epoch = forecastEpoch; row.count = 0; row.constantReady = false; row.centreCount = 0; }
    const ObservedVehicle& o = observedVehicles[actor];
    if (o.stationary && step >= 2) {
        if (row.count < 2) ForecastAt(actor, 1);
        // Residual motion goes into the sweep pad, which bounds every later pose for the
        // inflated clearance test; an existing contact keeps its exact depth reference.
        if (!row.constantReady) {
            row.constant.box = row.samples[1].box;
            row.constant.sweptPad = o.drift;
            row.constantReady = true;
        }
        return row.constant;
    }
    while (row.count <= step) {
        ForecastSample& sample = row.samples[row.count];
        work.current.forecasts++;
        sample.box = PredictObserved(observedVehicles[actor], row.count * predictionStep, 0);
        sample.sweptPad = row.count == 0 ? 0
            : PoseMotionBound(row.samples[row.count - 1].box, sample.box, observedVehicles[actor].radius);
        row.count++;
    }
    return row.samples[step];
}

// A rail actor's forecast centre at 'step' (the centre of its forecast box, computed the
// same way) and a bound on that sample's sweep pad, from the route alone: the box
// orientation and its trigonometry are needed only when the actor comes within reach.
// The pad is the centre travel plus radius times the turn between the samples; for
// unit directions less than a right angle apart the turn is at most pi/2 * |cross|.
Vector2 RailCentreAt(int actor, int step, float* padBound) {
    ForecastRow& row = vehicleForecasts[actor];
    if (row.epoch != forecastEpoch) { row.epoch = forecastEpoch; row.count = 0; row.constantReady = false; row.centreCount = 0; }
    const ObservedVehicle& o = observedVehicles[actor];
    while (row.centreCount <= step) {
        int k = row.centreCount;
        row.centre[k] = ObservedRailCentre(o, k * predictionStep, &row.direction[k]);
        row.centreCount++;
    }
    Vector2 a = row.direction[step - 1], b = row.direction[step];
    float turn = Dot(a, b) > 0 ? PI * 0.5f * fabsf(Cross(a, b)) * 1.001f + 1e-5f : PI;
    *padBound = Dist(row.centre[step - 1], row.centre[step]) * 1.001f + 1e-3f + o.radius * turn;
    return row.centre[step];
}

enum class GeometryKind { Building, ObjectBox, ObjectCircle, Vehicle, Person };
struct Nearby {
    GeometryKind kind = GeometryKind::Building;
    OBB box{};
    Vector2 centre{}, velocity{};
    float radius = 0;
    int observed = -1;
    float initialDepth = 0, previousDepth = 0;
    float centreSpeed = 0;                 // fixed observation, reused at every force sample
    Vector2 forecastOrigin{};
    bool released = true;
    bool follower = false;                 // behind, moving our way: responsible for the gap
};
std::array<Nearby, MAX_NEARBY> nearby;
int nearbyCount = 0;
std::vector<int> queryBuildings, queryObjects;
bool nearbyComplete = true;

// Rollout broadphase. An actor found beyond its cull reach sleeps until the first force
// step at which it could possibly be within reach again, given a per-step bound on the
// ego's travel and sweep pad and the actor's own speed bound. Only awake actors are
// tested, in nearby order, so the exact checks and the reported blocker are unchanged.
// A step that exceeds the assumed ego bounds wakes every sleeper at once.
std::array<int, MAX_NEARBY> awake;
int awakeCount = 0;
std::array<int, MAX_NEARBY> sleepNext;
std::array<int, MAX_FORECAST_SAMPLES + 2> wakeHead;
int wakeLimit = 0;                           // last force step of the rollout
float egoStepBound = 0, padStepBound = 0;    // assumed ego travel / sweep pad per force step

struct PredictionGeometry {
    OBB initial{};
    float diagonal = 0;
    float minX = 0, minY = 0, maxX = cfg::WORLD_W, maxY = cfg::WORLD_H;
} geometry;

bool Overlap(const Nearby& n, const OBB& box, float time, float grow, float& depth, const OBB* observed = nullptr) {
    Vector2 normal;
    depth = 0;
    if (n.kind == GeometryKind::Vehicle) {
        OBB other = observed ? *observed : PredictObserved(observedVehicles[n.observed], time, 0);
        other.he[0] += grow; other.he[1] += grow;
        return OBBOverlap(box, other, normal, depth);
    }
    if (n.kind == GeometryKind::Person || n.kind == GeometryKind::ObjectCircle)
        return CircleOBB(n.centre + n.velocity * time, n.radius + grow, box, normal, depth);
    OBB other = n.box;
    other.he[0] += grow; other.he[1] += grow;
    return OBBOverlap(box, other, normal, depth);
}

void AddNearby(Nearby value) {
    if (nearbyCount >= MAX_NEARBY) { nearbyComplete = false; return; }
    // Existing contacts may be escaped, but never deepened by a prediction.
    float depth = 0;
    value.centreSpeed = Len(value.velocity);
    // The initial footprint is the observed pose; beyond both enclosing circles (the
    // other one grown by the clearance) there is no initial contact to test for.
    if (value.kind == GeometryKind::Vehicle) value.forecastOrigin = observedVehicles[value.observed].pos;
    Vector2 centre = value.kind == GeometryKind::Vehicle ? value.forecastOrigin : value.centre;
    float growScale = value.kind == GeometryKind::Person || value.kind == GeometryKind::ObjectCircle ? 1.0f : OBB_GROW_RADIUS;
    float reach = geometry.diagonal + value.radius + growScale * Tuning().clearance + 0.01f;
    if (Len2(centre - geometry.initial.c) > reach * reach) {
        value.initialDepth = 0;
        nearby[nearbyCount++] = value;
        return;
    }
    const OBB* observed = nullptr;
    if (value.kind == GeometryKind::Vehicle) {
        // Every driver reads this same initial footprint; do not repeat its
        // rail path/trigonometry for every recovery neighbourhood.
        observed = &ForecastAt(value.observed, 0).box;
    }
    bool hit = Overlap(value, geometry.initial, 0, Tuning().clearance, depth, observed);
    // OBBOverlap may leave depth at its sentinel or a previous axis depth
    // when a separating axis rejects the pair. Only a true hit has depth.
    value.initialDepth = hit ? std::max(0.0f, depth) : 0.0f;
    nearby[nearbyCount++] = value;
}

void GatherNearbyImpl(Game& g, const Vehicle& v, int self, float horizon);
void GatherNearby(Game& g, const Vehicle& v, int self, float horizon = -1) {
    auto start = Clock::now();
    GatherNearbyImpl(g, v, self, horizon);
    work.current.gatherMs += Milliseconds(start);
    work.current.gathers++;
    work.current.nearbyMax = std::max(work.current.nearbyMax, nearbyCount);
}
void GatherNearbyImpl(Game& g, const Vehicle& v, int self, float horizon) {
    nearbyCount = 0; nearbyComplete = true;
    const Settings& cfg = Tuning();
    if (horizon < 0) horizon = cfg.horizon;
    geometry.initial = v.Box();
    geometry.diagonal = sqrtf(v.width * v.width + v.length * v.length) * 0.5f;
    Vector2 initialPoints[4]; OBBCorners(geometry.initial, initialPoints);
    geometry.minX = 0; geometry.minY = 0; geometry.maxX = cfg::WORLD_W; geometry.maxY = cfg::WORLD_H;
    for (Vector2 p : initialPoints) {
        geometry.minX = std::min(geometry.minX, p.x); geometry.minY = std::min(geometry.minY, p.y);
        geometry.maxX = std::max(geometry.maxX, p.x); geometry.maxY = std::max(geometry.maxY, p.y);
    }
    // Recovery starts with the observed crash velocity, which can exceed its target.
    float ownSpeed = std::max(v.Speed(), std::max(cfg.forwardSpeed, cfg.reverseSpeed));
    float ownReach = ownSpeed * horizon + geometry.diagonal + 20;
    Rectangle area = { v.pos.x - ownReach, v.pos.y - ownReach, ownReach * 2, ownReach * 2 };
    g.map.QueryBuildings(area, queryBuildings);
    for (int idx : queryBuildings) {
        const Building& b = g.map.buildings[idx];
        if (!b.Solid()) continue;
        Nearby n; n.kind = GeometryKind::Building; n.box = MakeAABB(b.r);
        n.centre = n.box.c;
        n.radius = sqrtf(n.box.he[0] * n.box.he[0] + n.box.he[1] * n.box.he[1]);
        AddNearby(n);
    }
    g.map.QueryObjects(area, queryObjects);
    for (int idx : queryObjects) {
        const CityObject& o = g.map.objects[idx];
        if (!o.alive || o.soft || o.radius <= 0) continue;
        Nearby n; n.centre = o.pos; n.radius = o.radius;
        n.kind = o.box ? GeometryKind::ObjectBox : GeometryKind::ObjectCircle;
        if (o.box) n.box = o.Box();
        AddNearby(n);
    }
    for (int idx = 0; idx < (int)observedVehicles.size(); idx++) {
        const ObservedVehicle& o = observedVehicles[idx];
        if (idx == self || !o.active) continue;
        float radius = o.radius;
        // All possible incoming actors are retained, including those outside the usual
        // neighbourhood. The configured radius is a minimum, never an unsafe cutoff.
        // A rail's centre can slow on a curve while its path distance advances faster.
        float incomingSpeed = std::max(Len(o.vel), o.rail ? fabsf(o.speed) : 0.0f);
        float incomingReach = incomingSpeed * horizon;
        if (o.rail && o.pathCount > 1) {
            incomingReach += o.routeGap + o.shiftSpan;
            if (o.blend > 0) incomingReach = std::max(incomingReach, Dist(o.blendPos, o.pos));
        }
        float reach = std::max(cfg.nearbyRadius, ownReach + radius + incomingReach);
        float distance2 = Len2(o.pos - v.pos);
        if (distance2 > reach * reach) continue;
        Nearby n; n.kind = GeometryKind::Vehicle; n.observed = idx;
        n.centre = o.pos; n.velocity = o.vel; n.radius = radius;
        // A vehicle behind us in our lane, moving our way, keeps its own distance (the
        // rear-end rule): forward moves do not treat it as an obstacle. Forecasting it
        // at constant speed into our stopping tail vetoed every move off a queue.
        Vector2 rel = o.pos - v.pos, fwd = v.Fwd();
        n.follower = Dot(rel, fwd) < -(v.length * 0.5f) && Dot(o.vel, fwd) > 5 &&
                     fabsf(Dot(rel, Perp(fwd))) < (v.width + o.width) * 0.5f + 10;
        AddNearby(n);
    }
    for (int idx = 0; idx < (int)observedPeople.size(); idx++) {
        const ObservedPerson& p = observedPeople[idx];
        if (!p.active) continue;
        float reach = ownReach + PED_RADIUS + Len(p.vel) * horizon;
        if (Len2(p.pos - v.pos) > reach * reach) continue;
        Nearby n; n.kind = GeometryKind::Person; n.centre = p.pos; n.observed = idx;
        n.velocity = p.vel; n.radius = PED_RADIUS;
        AddNearby(n);
    }
}

// No Vehicle assignment here: it would copy DriverAI's deque and allocate per rollout.
void CopyPhysics(const Vehicle& source, Vehicle& target) {
    target.active = source.active; target.cls = source.cls;
    target.pos = source.pos; target.vel = source.vel;
    target.angle = source.angle; target.angVel = source.angVel; target.steer = source.steer;
    target.width = source.width; target.length = source.length; target.height = source.height;
    target.driver = source.driver; target.wrecked = source.wrecked; target.burning = source.burning;
    target.slip = source.slip; target.speedFwd = Dot(source.vel, source.Fwd());
    target.in = VehicleInput{};
}

bool LaneAligned(const Vehicle& v, const RecoveryState& state);

VehicleInput Controls(const Vehicle& v, int gear, float steer, const RecoveryState* lane = nullptr) {
    VehicleInput in;
    float speed = Dot(v.vel, v.Fwd());
    in.steer = steer;
    bool changing = gear > 0 ? speed < -6 : gear < 0 ? speed > 6 : false;
    if (gear == 0 || changing) {
        in.handbrake = true;
        in.steer = 0;
        if (speed > 30) in.brake = 1;
        else if (speed < -30) in.throttle = 1;
        return in;
    }
    const Settings& cfg = Tuning();
    float want = gear > 0 ? cfg.forwardSpeed : cfg.reverseSpeed;
    if (lane && gear > 0) {
        float lateral = Dot(v.pos - lane->origin, Perp(lane->forward));
        float heading = WrapAngle(v.angle - AngleOf(lane->forward));
        float look = std::max(80.0f, v.length * 0.75f);
        float desiredHeading = -atan2f(lateral, look);
        float desiredYaw = WrapAngle(desiredHeading - heading) * 1.4f;
        // Reduce speed as the corridor is approached. The feedback remains continuous
        // down to zero error; coarse fixed steering cannot finish a precise rejoin.
        want = LaneAligned(v, *lane) ? cfg.forwardSpeed
            : Lerpf(28.0f, cfg.forwardSpeed, SmoothStep(8, 48, fabsf(lateral)));
        float effectiveSpeed = std::max(25.0f, fabsf(speed));
        float factor = Saturate(effectiveSpeed / 150) * (1 - 0.42f * Saturate(effectiveSpeed / v.S().maxSpeed));
        float yawAuthority = v.S().steerRate * factor * Saturate(effectiveSpeed / 30);
        in.steer = Clampf(desiredYaw / std::max(0.05f, yawAuthority), -1, 1);
    }
    if (gear > 0) {
        if (speed > want + 5) in.brake = speed > 30 ? Saturate((speed - want) / 40) : 0;
        else if (speed < want) in.throttle = Clampf(0.13f + (want - speed) / 80, 0, 0.65f);
    } else {
        if (speed < -want - 5) in.throttle = speed < -30 ? Saturate((-speed - want) / 40) : 0;
        else if (speed > -want) in.brake = Clampf(0.25f + (want + speed) / 70, 0, 0.7f);
    }
    return in;
}

using Candidate = RecoveryCandidate;

float LaneError(const Vehicle& v, const RecoveryState& state) {
    float lateral = fabsf(Dot(v.pos - state.origin, Perp(state.forward)));
    float heading = fabsf(WrapAngle(v.angle - AngleOf(state.forward)));
    return lateral * 1.8f + heading * v.length * 0.7f;
}

bool LaneAligned(const Vehicle& v, const RecoveryState& state) {
    return fabsf(Dot(v.pos - state.origin, Perp(state.forward))) <= 4 &&
           fabsf(WrapAngle(v.angle - AngleOf(state.forward))) <= 0.12f &&
           Dot(v.vel, v.Fwd()) >= -2 && fabsf(Dot(v.vel, Perp(state.forward))) <= 6 && fabsf(v.angVel) <= 0.2f;
}

bool InsideWorld(const OBB& box) {
    float radiusX = fabsf(box.ax[0].x) * box.he[0] + fabsf(box.ax[1].x) * box.he[1];
    float radiusY = fabsf(box.ax[0].y) * box.he[0] + fabsf(box.ax[1].y) * box.he[1];
    return box.c.x - radiusX >= geometry.minX - 0.025f && box.c.y - radiusY >= geometry.minY - 0.025f &&
           box.c.x + radiusX <= geometry.maxX + 0.025f && box.c.y + radiusY <= geometry.maxY + 0.025f;
}

Vector2 terminalPos{};                       // final pose of the last safe rollout
float terminalAngle = 0;
int failedVehicle = -1;                      // observed vehicle that rejected the last rollout
int failedPerson = -1;                       // observed person that rejected it
int rolloutGear = 0;                         // gear of the rollout being checked
// Optional capture of the ego pose at the next frame boundaries of a rollout.
float captureDt = 0;
int captureCount = 0;
std::array<Vector2, 4> capturePos{};
std::array<float, 4> captureAngle{};

void StartBroadphase(const Vehicle& v, int steps) {
    awakeCount = 0;
    for (int k = 0; k < nearbyCount; k++) {
        // A follower never vetoes a forward rollout; it is not tested at all.
        if (nearby[k].follower && rolloutGear > 0) continue;
        awake[awakeCount++] = k;
    }
    wakeLimit = std::min(steps, MAX_FORECAST_SAMPLES);
    for (int k = 0; k <= wakeLimit + 1; k++) wakeHead[k] = -1;
    // Generous starting bounds: the controls aim below these speeds and yaw rates.
    float speed = std::max(Len(v.vel), std::max(Tuning().forwardSpeed, Tuning().reverseSpeed)) * 1.25f + 20;
    float yaw = std::max(fabsf(v.angVel), 2.0f) * 1.25f;
    egoStepBound = speed * predictionStep;
    padStepBound = egoStepBound + geometry.diagonal * yaw * predictionStep;
}

void WakeAll() {
    for (int k = 0; k <= wakeLimit + 1; k++)
        for (int n = wakeHead[k]; n >= 0; n = sleepNext[n]) awake[awakeCount++] = n;
    for (int k = 0; k <= wakeLimit + 1; k++) wakeHead[k] = -1;
    std::sort(awake.begin(), awake.begin() + awakeCount);
}

// Put actor 'idx' to sleep after it was culled at 'step' with 'margin' (centre distance
// minus cull reach). Until it wakes, its distance stays above its reach: every step the
// gap shrinks by at most the ego travel bound, the actor speed bound and a rounding
// allowance, and the reach grows by at most the inflated sweep pads. Returns false
// when the actor must stay awake.
bool Sleep(int idx, int step, float margin, float actorSpeed, float actorPad, float growScale) {
    float rate = egoStepBound + actorSpeed * predictionStep + 0.01f;
    float room = margin - growScale * (padStepBound + actorPad);
    if (room <= rate) return false;
    int skip = (int)ceilf(room / rate) - 1;     // skip * rate < room
    int wake = step + 1 + skip;
    if (wake <= step + 1) return false;
    // Beyond the rollout: parked in the last bucket, which only a bound violation wakes.
    wake = std::min(wake, wakeLimit + 1);
    sleepNext[idx] = wakeHead[wake];
    wakeHead[wake] = idx;
    return true;
}

bool FootprintClear(const Vehicle& scratch, int step, float time, float h, float sweptPad, float moved) {
    const Settings& cfg = Tuning();
    // scratch.Box(), with the sine and cosine the next force step reuses.
    float sinA, cosA;
    CachedSinCos(scratch.angle, sinA, cosA);
    OBB box; box.c = scratch.pos; box.ax[0] = { cosA, sinA }; box.ax[1] = { sinA, -cosA };
    box.he[0] = scratch.width * 0.5f; box.he[1] = scratch.length * 0.5f;
    if (!InsideWorld(box)) return false;
    // The sleepers assumed smaller ego steps: all of them are tested again now.
    if (moved > egoStepBound || sweptPad > padStepBound) {
        egoStepBound = std::max(egoStepBound * 2, moved * 1.5f);
        padStepBound = std::max(std::max(padStepBound * 2, sweptPad * 1.5f), egoStepBound);
        WakeAll();
    }
    if (step > wakeLimit) WakeAll();
    else if (wakeHead[step] >= 0) {
        for (int n = wakeHead[step]; n >= 0; n = sleepNext[n]) awake[awakeCount++] = n;
        wakeHead[step] = -1;
        std::sort(awake.begin(), awake.begin() + awakeCount);
    }
    work.current.steps++; work.current.actorTests += awakeCount;
    int kept = 0;
    for (int a = 0; a < awakeCount; a++) {
        int idx = awake[a];
        awake[kept++] = idx;
        Nearby& n = nearby[idx];
        const OBB* observed = nullptr;
        OBB partialPose;
        Vector2 position = n.centre + n.velocity * time;
        float otherPad = n.centreSpeed * h;
        float growScale = n.kind == GeometryKind::Person || n.kind == GeometryKind::ObjectCircle
            ? 1.0f : OBB_GROW_RADIUS;
        // Speed and sweep-pad bounds of the actor over later steps, for sleeping.
        float actorSpeed = n.centreSpeed, actorPad = n.centreSpeed * predictionStep;
        bool canSleep = true;
        if (n.kind == GeometryKind::Vehicle) {
            const ObservedVehicle& actor = observedVehicles[n.observed];
            float baseReach = geometry.diagonal + n.radius + growScale * (cfg.clearance + sweptPad);
            bool distant = false;
            if (!actor.rail || actor.pathCount <= 1) {
                // The physical forecast centre is linear. Its endpoint pad is
                // bounded by translation + radius * |angular velocity| * h;
                // wrapped endpoint rotation can never exceed that angle.
                float bound = baseReach + growScale * (otherPad + n.radius * fabsf(actor.angVel) * h);
                float distance2 = Len2(position - scratch.pos);
                distant = distance2 > bound * bound;
                // The detailed forecast has the same linear centre; its pad is the
                // centre travel plus corner rotation, or the stationary drift bound.
                actorPad = std::max((n.centreSpeed + n.radius * fabsf(actor.angVel)) * predictionStep * 1.01f + 0.01f,
                                    actor.stationary ? actor.drift + 0.01f : 0.0f);
                if (distant) {
                    n.released = true;
                    if (Sleep(idx, step, sqrtf(distance2) - bound, actorSpeed, actorPad, growScale)) kept--;
                    continue;
                }
            } else if (actor.blend <= 0) {
                // Each axle point moves at most |path speed| * time. Their
                // midpoint inherits that bound; a fixed lane shift contributes
                // at most 2*|shift| as its direction changes. For the existing
                // inflated-endpoint test, also enclose the preceding sample's
                // translation/shift and its worst wrapped rotation (pi). The
                // actual initial pose may differ from route centre zero;
                // include that gap both in centre travel and the first sweep.
                // A skipped actor passes the original distance cull too.
                float shiftSpan = actor.shiftSpan;
                float travel = fabsf(actor.speed) * time + shiftSpan + actor.routeGap;
                float maxSweep = fabsf(actor.speed) * h + shiftSpan + n.radius * PI + actor.routeGap;
                float bound = baseReach + travel + growScale * maxSweep;
                float distance2 = Len2(n.forecastOrigin - scratch.pos);
                distant = distance2 > bound * bound;
                // Later steps: the envelope grows by the path speed per step; its sweep
                // term is fixed. The exact rail forecast below is not linear, so only
                // this envelope may put the actor to sleep.
                canSleep = false;
                if (distant) {
                    n.released = true;
                    if (Sleep(idx, step, sqrtf(distance2) - bound, fabsf(actor.speed), 0.0f, growScale)) kept--;
                    continue;
                }
            } else canSleep = false;   // a legacy blend mixes a separate pose: full forecast
            bool sampled = step < MAX_FORECAST_SAMPLES && fabsf(time - step * predictionStep) < 1e-6f;
            if (sampled && actor.rail && actor.pathCount > 1 && actor.blend <= 0 && !actor.stationary && step >= 2) {
                // The forecast box sits at the route centre; out of reach even with the
                // largest possible pad, its orientation is not needed.
                float padBound = 0;
                Vector2 centre = RailCentreAt(n.observed, step, &padBound);
                float reachBound = geometry.diagonal + n.radius + growScale * (cfg.clearance + sweptPad + padBound) + 1e-3f;
                if (Len2(centre - scratch.pos) > reachBound * reachBound) { n.released = true; continue; }
            }
            if (sampled) {
                const ForecastSample& forecast = ForecastAt(n.observed, step);
                observed = &forecast.box; otherPad = forecast.sweptPad;
            } else {
                // Only a shortened final substep needs an uncached exact-time pose.
                partialPose = PredictObserved(observedVehicles[n.observed], time, 0);
                OBB previous = PredictObserved(observedVehicles[n.observed], std::max(0.0f, time - h), 0);
                observed = &partialPose; otherPad = PoseMotionBound(previous, partialPose, n.radius);
            }
            position = observed->c;
        }
        // OBB inflation increases both half-extents, so its enclosing circle
        // grows by up to sqrt(2)*grow. Circle inflation grows its radius once.
        float reach = geometry.diagonal + n.radius + growScale * (cfg.clearance + sweptPad + otherPad);
        float distance2 = Len2(position - scratch.pos);
        if (distance2 > reach * reach) {
            n.released = true;
            if (canSleep && Sleep(idx, step, sqrtf(distance2) - reach, actorSpeed, actorPad, growScale)) kept--;
            continue;
        }
        work.current.overlaps++;
        float depth = 0;
        if (!n.released && n.initialDepth > 0) {
            // An existing contact: escaping is allowed, deepening it is not.
            if (Overlap(n, box, time, cfg.clearance, depth, observed)) {
                if (depth > n.previousDepth + 0.025f) {
                    failedVehicle = n.kind == GeometryKind::Vehicle ? n.observed : -1;
                    failedPerson = n.kind == GeometryKind::Person ? n.observed : -1;
                    return false;
                }
                n.previousDepth = std::min(n.previousDepth, depth);
                continue;
            }
            // Leaving an existing contact: the pose is already outside the clearance
            // margin, but the sweep pad of this step can still reach back to it. Keep
            // the no-deepening rule (no renewed contact at all) until the swept test
            // clears too; otherwise every slow escape failed on its first clear step.
            n.previousDepth = 0;
            float sweptDepth = 0;
            if (Overlap(n, box, time, cfg.clearance + sweptPad + otherPad, sweptDepth, observed)) continue;
            n.released = true;
            continue;
        }
        // Inflating the endpoint by its translation + corner rotation encloses the
        // intervening footprint; moving actors get the same conservative treatment.
        if (Overlap(n, box, time, cfg.clearance + sweptPad + otherPad, depth, observed)) {
            failedVehicle = n.kind == GeometryKind::Vehicle ? n.observed : -1;
            failedPerson = n.kind == GeometryKind::Person ? n.observed : -1;
            return false;
        }
    }
    awakeCount = kept;
    return true;
}

Candidate Predict(Game& g, const Vehicle& v, const RecoveryState& state, Candidate candidate, float horizon, bool stoppingTail,
                  RejoinCause* cause = nullptr) {
    if (cause) *cause = RejoinCause::UnsafeSweep;
    failedVehicle = -1; failedPerson = -1;
    rolloutGear = candidate.gear;
    static Vehicle scratch;
    CopyPhysics(v, scratch);
    for (int k = 0; k < nearbyCount; k++) {
        nearby[k].previousDepth = nearby[k].initialDepth;
        nearby[k].released = nearby[k].initialDepth <= 0;
    }
    float startError = LaneError(v, state), startHeading = fabsf(WrapAngle(v.angle - AngleOf(state.forward)));
    float movingTime = stoppingTail ? std::max(0.1f, horizon - Tuning().stopTail) : horizon;
    float travelled = 0;
    int steps = (int)ceilf(horizon / predictionStep - 0.0001f);
    StartBroadphase(v, steps);
    VehicleInput input;
    float nextControl = 0;
    for (int k = 0; k < steps; k++) {
        float h = std::min(predictionStep, horizon - k * predictionStep);
        if (h <= 0) break;
        float time = k * predictionStep;
        int gear = time < movingTime ? candidate.gear : 0;
        float steer = candidate.switchTime > 0 && time >= candidate.switchTime ? candidate.secondSteer : candidate.steer;
        // Hold controls between frame boundaries; refresh at the next bounded sample
        // when an actual frame boundary falls between prediction force steps.
        if (time + 1e-6f >= nextControl) {
            input = Controls(scratch, gear, steer, candidate.tracking ? &state : nullptr);
            do { nextControl += predictionControlInterval; } while (nextControl <= time + 1e-6f);
        }
        scratch.in = input;
        Vector2 previousPos = scratch.pos;
        float previousAngle = scratch.angle;
        VehicleForces(scratch, g.map, h);
        scratch.pos = scratch.pos + scratch.vel * h;
        scratch.angle = WrapAngle(scratch.angle + scratch.angVel * h);
        float moved = Dist(scratch.pos, previousPos);
        float sweptPad = moved + geometry.diagonal * fabsf(WrapAngle(scratch.angle - previousAngle));
        if (!std::isfinite(scratch.pos.x) || !std::isfinite(scratch.pos.y) || !std::isfinite(scratch.angle) ||
            !FootprintClear(scratch, k + 1, time + h, h, sweptPad, moved)) {
            candidate.blocker = failedVehicle;
            return candidate;
        }
        travelled += moved;
        while (captureDt > 0 && captureCount < (int)capturePos.size() &&
               time + h >= (captureCount + 1) * captureDt - 1e-5f) {
            capturePos[captureCount] = scratch.pos; captureAngle[captureCount] = scratch.angle;
            captureCount++;
        }
    }
    // An unfinished stop must not certify room to stop beyond the checked corridor.
    if (stoppingTail && scratch.Speed() > 2) {
        if (cause) *cause = RejoinCause::IncompleteStop;
        return candidate;
    }
    candidate.safe = true;
    terminalPos = scratch.pos; terminalAngle = scratch.angle;
    if (cause) *cause = RejoinCause::Clear;
    candidate.terminalAligned = LaneAligned(scratch, state);
    candidate.distance = travelled;
    candidate.headingGain = startHeading - fabsf(WrapAngle(scratch.angle - AngleOf(state.forward)));
    candidate.lateralGain = fabsf(Dot(v.pos - state.origin, Perp(state.forward)))
        - fabsf(Dot(scratch.pos - state.origin, Perp(state.forward)));
    float along = Dot(scratch.pos - v.pos, state.forward);
    candidate.laneImprovement = startError - LaneError(scratch, state);
    candidate.score = candidate.laneImprovement + along * 0.08f;
    candidate.score -= fabsf(candidate.steer) * 1.5f;
    if (state.gear != 0 && candidate.gear != state.gear) candidate.score -= Tuning().hysteresis;
    candidate.score -= fabsf(candidate.steer - state.steer) * 1.5f;
    if (candidate.switchTime > 0) candidate.score -= 1;
    if (candidate.tracking) candidate.score += 4;
    if (state.tracking && !candidate.tracking && candidate.gear > 0) candidate.score -= Tuning().hysteresis;
    // A complete stop remains a useful candidate. Moving in a physically clear
    // direction is preferred to hold when another move must first create turn space.
    if (candidate.gear != 0 && travelled > 2) candidate.score += 2;
    return candidate;
}

void CommitCandidate(RecoveryState& state, const Candidate& candidate) {
    state.nextPlan = Tuning().planningInterval;
    state.commit = Tuning().commitment;
    state.gear = candidate.gear;
    state.steer = candidate.steer;
    state.tracking = candidate.tracking;
    state.reason = state.gear > 0 ? RecoveryReason::Forward : RecoveryReason::Reverse;
}

// A stopped vehicle ahead in our lane corridor (a queue we may join, not an obstacle).
bool QueuedAhead(const Vehicle& v, Vector2 forward, int observed) {
    if (observed < 0 || observed >= (int)observedVehicles.size()) return false;
    const ObservedVehicle& o = observedVehicles[observed];
    float speed = std::max(Len(o.vel), o.rail ? fabsf(o.speed) : 0.0f);
    Vector2 rel = o.pos - v.pos;
    return o.active && speed < 8 && Dot(rel, forward) > 0 &&
           fabsf(Dot(rel, Perp(forward))) < (v.width + o.width) * 0.5f + 6;
}

bool MakesLaneProgress(const Candidate& candidate) {
    // A long vehicle must temporarily turn across the lane; its initial heading cost
    // must not veto genuine lateral progress. Driving straight back into a dead end
    // provides neither lateral nor heading improvement and does not cancel retreat.
    // Once aligned, forward travel can be necessary to reach the next clear block
    // segment. Zero remaining lane error must not manufacture another reverse escape.
    return (candidate.gear > 0 && candidate.terminalAligned) || candidate.laneImprovement > 0.25f ||
           candidate.lateralGain > 2 || candidate.headingGain > 0.03f;
}

// Fixed arc order of a planning job; indices match the original candidate array.
Candidate ArcCandidate(int arc) {
    const Settings& cfg = Tuning();
    static const float constant[5] = { 0.0f, -0.5f, 0.5f, -1.0f, 1.0f };
    static const float unwinding[4] = { -0.5f, 0.5f, -1.0f, 1.0f };
    Candidate c; c.gear = arc < 9 ? 1 : -1;
    int k = arc % 9;
    if (k < 5) c.steer = constant[k];
    else {
        // Unwinding after an initial arc can clear a corner that a constant-steer
        // rollout would hit. Execution only commits the first, validated segment.
        c.steer = unwinding[k - 5];
        c.switchTime = std::max(cfg.commitment, (cfg.horizon - cfg.stopTail) * 0.5f);
        c.secondSteer = 0;
    }
    return c;
}

void VoteBlocker(RecoveryState& state, const Candidate& c) {
    if (c.safe || (failedVehicle < 0 && failedPerson < 0)) return;
    int id = failedVehicle >= 0 ? failedVehicle + 1 : RECOVERY_PERSON_ID + failedPerson + 1, slot = -1;
    for (int k = 0; k < (int)state.blockIds.size(); k++) {
        if (state.blockIds[k] == id) { slot = k; break; }
        if (state.blockIds[k] == 0 && slot < 0) slot = k;
    }
    if (slot < 0) return;
    state.blockIds[slot] = id; state.blockVotes[slot]++;
}

uint64_t HoldSignature(const Vehicle& v, const RecoveryState& state);

void FinishHold(const Vehicle& v, RecoveryState& state) {
    int best = -1;
    for (int k = 0; k < (int)state.blockIds.size(); k++)
        if (state.blockIds[k] && state.blockIds[k] <= RECOVERY_PERSON_ID && state.blockVotes[k] >= 3 &&
            (best < 0 || state.blockVotes[k] > state.blockVotes[best])) best = k;
    state.blockedBy = best >= 0 ? state.blockIds[best] - 1 : -1;
    state.gear = 0; state.steer = 0;
    state.tracking = false;
    state.reason = RecoveryReason::NoFeasibleManoeuvre;
    state.holds++; timing.holds++;
    state.holdSignature = HoldSignature(v, state);
    state.holdAge = 0;
    state.planning = false;
}

void FinishMove(RecoveryState& state, const Candidate& candidate) {
    CommitCandidate(state, candidate);
    state.blockedBy = -1;
    state.holdSignature = 0;
    state.planning = false;
}

// One unit of a planning job: a single rollout, or the cheap selection step.
// Candidate order, scores and the hold-first tie are those of the one-frame planner.
void PlanUnit(Game& g, Vehicle& v, RecoveryState& state) {
    const Settings& cfg = Tuning();
    auto& candidates = state.candidates;
    const int arcs = RECOVERY_CANDIDATES - 2;
    if (state.planNext == 0) {
        Candidate tracking; tracking.gear = 1; tracking.tracking = true;
        work.current.tracking++;
        tracking = Predict(g, v, state, tracking, cfg.horizon, true);
        VoteBlocker(state, tracking);
        state.planNext = 1;
        // Normal lane convergence does not need an escape search. It receives the same
        // complete swept check; unsuccessful or stalled tracking still evaluates all arcs.
        if (tracking.safe && tracking.distance > 2 && MakesLaneProgress(tracking) && state.stalled <= cfg.stallTime) {
            FinishMove(state, tracking);
            return;
        }
        candidates[0] = Candidate{};
        candidates[1] = tracking;
        return;
    }
    if (state.planNext <= arcs) {
        work.current.candidates++;
        candidates[state.planNext + 1] = Predict(g, v, state, ArcCandidate(state.planNext - 1), cfg.horizon, true);
        VoteBlocker(state, candidates[state.planNext + 1]);
        state.planNext++;
        return;
    }
    if (state.planNext == arcs + 1) {
        // Aligned in the lane and every forward move blocked by a stopped vehicle ahead
        // that we do not touch: wait in the queue instead of reversing away from it.
        bool aligned = fabsf(Dot(v.pos - state.origin, Perp(state.forward))) <= 4 &&
                       fabsf(WrapAngle(v.angle - AngleOf(state.forward))) <= 0.12f && v.Speed() < 5;
        bool queued = aligned;
        for (int k = 1; k < RECOVERY_CANDIDATES && queued; k++) {
            const Candidate& c = candidates[k];
            if (c.gear != 1) continue;
            if (c.safe || !QueuedAhead(v, state.forward, c.blocker)) queued = false;
            for (int n = 0; queued && n < nearbyCount; n++)
                if (nearby[n].kind == GeometryKind::Vehicle && nearby[n].observed == c.blocker && nearby[n].initialDepth > 0) queued = false;
        }
        if (queued) {
            state.nextPlan = cfg.planningInterval; state.commit = cfg.commitment;
            for (int k = 1; k < RECOVERY_CANDIDATES; k++) if (!candidates[k].safe) { state.rejected++; timing.rejected++; }
            // Touched from behind (a rear-end): creep forward into the gap until the
            // contact is released, checked like any rollout, then wait in the queue.
            bool touchedBehind = false;
            for (int n = 0; n < nearbyCount; n++)
                if (nearby[n].kind == GeometryKind::Vehicle && nearby[n].initialDepth > 0 &&
                    Dot(nearby[n].centre - v.pos, state.forward) < 0) touchedBehind = true;
            if (touchedBehind) {
                Candidate creep; creep.gear = 1;
                work.current.candidates++;
                creep = Predict(g, v, state, creep, cfg.stopTail + 0.15f, true);
                if (creep.safe && creep.distance > 0.5f) {
                    FinishMove(state, creep);
                    state.commit = 0.2f; state.nextPlan = 0.2f;
                    return;
                }
            }
            FinishHold(v, state);
            state.reason = RecoveryReason::Queued;
            return;
        }
        bool forwardAvailable = false;
        for (int k = 1; k < RECOVERY_CANDIDATES; k++) {
            if (!candidates[k].safe) { state.rejected++; timing.rejected++; }
            if (candidates[k].safe && candidates[k].gear == 1 && candidates[k].distance > 5 && MakesLaneProgress(candidates[k]))
                forwardAvailable = true;
        }
        int best = -1;
        for (int k = 1; k < RECOVERY_CANDIDATES; k++) {
            Candidate& c = candidates[k];
            if (!c.safe) continue;
            if (state.gear < 0 && c.gear > 0 && !MakesLaneProgress(c)) continue;
            if (c.gear == -1 && !forwardAvailable) c.score += std::min(c.distance, v.length * 2) * 0.3f;
            if (c.gear == 0) c.score -= 3;
            if (state.stalled > cfg.stallTime && c.gear == state.gear && fabsf(c.steer - state.steer) < 0.2f) c.score -= 20;
            if (best < 0 || c.score > candidates[best].score) best = k;
        }
        state.planBest = best;
        state.nextPlan = cfg.planningInterval;
        state.commit = cfg.commitment;
        // Hold kept index zero for the original tie breaker. Its rollout is needed
        // only when the winning moving candidate does not already imply hold.
        if (best >= 0 && candidates[best].distance > 2) { state.planNext++; return; }
        FinishHold(v, state);
        return;
    }
    int best = state.planBest;
    Candidate& hold = candidates[0];
    work.current.candidates++;
    hold = Predict(g, v, state, hold, cfg.horizon, true);
    if (!hold.safe) { state.rejected++; timing.rejected++; }
    else {
        hold.score -= 3;
        if (state.stalled > cfg.stallTime && hold.gear == state.gear &&
            fabsf(hold.steer - state.steer) < 0.2f) hold.score -= 20;
        // Equality still favours hold, while all moving ties retain their order.
        if (hold.score >= candidates[best].score) best = 0;
    }
    state.nextPlan = cfg.planningInterval;
    state.commit = cfg.commitment;
    if (candidates[best].gear == 0 || candidates[best].distance <= 2) FinishHold(v, state);
    else FinishMove(state, candidates[best]);
}

uint64_t Mix(uint64_t hash, int64_t value) {
    hash ^= (uint64_t)value;
    return hash * 1099511628211ull;
}

// What a hold depends on: the quantized own pose, the static obstacles around and the
// actors that rejected the rollouts. Actors that did not block cannot open a way by
// moving; static geometry only changes when furniture breaks. A changed signature, or
// the periodic re-check, replans the hold.
uint64_t HoldSignature(const Vehicle& v, const RecoveryState& state) {
    uint64_t hash = 1469598103934665603ull;
    hash = Mix(hash, (int64_t)floorf(v.pos.x)); hash = Mix(hash, (int64_t)floorf(v.pos.y));
    hash = Mix(hash, (int64_t)floorf(v.angle * 100)); hash = Mix(hash, (int64_t)floorf(v.Speed() / 2));
    int statics = 0;
    for (int k = 0; k < nearbyCount; k++)
        if (nearby[k].kind != GeometryKind::Vehicle && nearby[k].kind != GeometryKind::Person) statics++;
    hash = Mix(hash, statics);
    for (int id : state.blockIds) {
        if (id <= 0) continue;
        hash = Mix(hash, id);
        Vector2 c{}, vel{};
        float angle = 0;
        bool active = false;
        if (id <= RECOVERY_PERSON_ID) {
            int idx = id - 1;
            if (idx < (int)observedVehicles.size()) {
                const ObservedVehicle& o = observedVehicles[idx];
                active = o.active; c = o.pos; vel = o.vel; angle = o.angle;
                hash = Mix(hash, (int64_t)floorf(o.speed / 4) * 2 + (o.rail ? 1 : 0));
            }
        } else {
            int idx = id - RECOVERY_PERSON_ID - 1;
            if (idx < (int)observedPeople.size()) {
                const ObservedPerson& p = observedPeople[idx];
                active = p.active; c = p.pos; vel = p.vel;
            }
        }
        hash = Mix(hash, active);
        hash = Mix(hash, (int64_t)floorf(c.x / 2)); hash = Mix(hash, (int64_t)floorf(c.y / 2));
        hash = Mix(hash, (int64_t)floorf(vel.x / 4)); hash = Mix(hash, (int64_t)floorf(vel.y / 4));
        hash = Mix(hash, (int64_t)floorf(angle * 50));
    }
    return hash | 1;                         // zero means "no recorded hold"
}

// Dynamic actors that could reach the car within the immediate horizon: they decide
// whether a later frame may reuse the extended check.
template <class F> void ForEachRelevantActor(const Vehicle& v, F&& f) {
    float own = std::max(v.Speed(), Tuning().forwardSpeed) * Tuning().horizon + geometry.diagonal + 30;
    for (int k = 0; k < nearbyCount; k++) {
        const Nearby& n = nearby[k];
        if (n.kind != GeometryKind::Vehicle && n.kind != GeometryKind::Person) continue;
        Vector2 pos = n.centre, vel = n.velocity;
        if (n.kind == GeometryKind::Vehicle) {
            const ObservedVehicle& o = observedVehicles[n.observed];
            pos = o.pos; vel = o.vel;
        }
        float reach = own + n.radius + Len(vel) * Tuning().horizon;
        if (Len2(pos - v.pos) > reach * reach) continue;
        f(n.kind == GeometryKind::Vehicle ? n.observed + 1 : RECOVERY_PERSON_ID + n.observed + 1, pos, vel);
    }
}

void RecordCheck(const Vehicle& v, RecoveryState& state, int frames) {
    int count = 0;
    bool overflow = false;
    ForEachRelevantActor(v, [&](int id, Vector2 pos, Vector2 vel) {
        if (count >= (int)state.checkId.size()) { overflow = true; return; }
        state.checkId[count] = id; state.checkActorPos[count] = pos; state.checkActorVel[count] = vel;
        count++;
    });
    if (overflow) return;                    // a crowd: check every frame
    state.checkActors = count;
    state.checkFrames = frames; state.checkStep = 0;
    state.checkGear = state.gear; state.checkSteer = state.steer; state.checkTracking = state.tracking;
    state.checkPos = capturePos; state.checkAngle = captureAngle;
}


bool CheckCovered(const Vehicle& v, const RecoveryState& state) {
    if (state.checkFrames <= 0 || state.checkGear != state.gear || state.checkTracking != state.tracking ||
        fabsf(state.checkSteer - state.steer) > 1e-4f) return false;
    int j = state.checkStep;                 // the pose predicted for this frame boundary
    if (j >= (int)state.checkPos.size() || Dist(v.pos, state.checkPos[j]) > 1.0f ||
        fabsf(WrapAngle(v.angle - state.checkAngle[j])) > 0.02f) return false;
    float elapsed = (j + 1) * predictionControlInterval;
    int count = 0;
    bool same = true;
    ForEachRelevantActor(v, [&](int id, Vector2 pos, Vector2) {
        if (!same) return;
        // The relevant actors come in nearby order, as recorded: try the same slot first.
        int k = count < state.checkActors && state.checkId[count] == id ? count : 0;
        while (k < state.checkActors && state.checkId[k] != id) k++;
        if (k == state.checkActors) { same = false; return; }       // a newcomer
        Vector2 expected = state.checkActorPos[k] + state.checkActorVel[k] * elapsed;
        if (Dist(pos, expected) > 2.0f) same = false;               // not moving as forecast
        count++;
    });
    return same && count == state.checkActors;
}

// Separation of two boxes along the four box axes: positive when apart. It is the gap
// a separating-axis test finds, a lower bound of the true distance, and grows as the
// boxes move apart.
float OBBGap(const OBB& a, const OBB& b) {
    Vector2 axes[4] = { a.ax[0], a.ax[1], b.ax[0], b.ax[1] };
    Vector2 d = b.c - a.c;
    float gap = -1e9f;
    for (Vector2 n : axes) gap = std::max(gap, fabsf(Dot(d, n)) - OBBProjectRadius(a, n) - OBBProjectRadius(b, n));
    return gap;
}

OBB ObservedBox(int actor) {
    const ObservedVehicle& o = observedVehicles[actor];
    return MakeOBB(o.pos, o.angle, o.width * 0.5f, o.length * 0.5f);
}

// Moving along its own axis, which way takes the car away from 'other'.
int AwayGear(const Vehicle& v, int other) {
    return Dot(observedVehicles[other].pos - v.pos, v.Fwd()) > 0 ? -1 : 1;
}

// A making-room job: 20 short creeps away from the other car (five steering angles,
// 0.3-1.2 s of driving, each followed by the checked stopping tail). The creep that
// opens the largest gap to the other car wins; on a tie the shorter one.
constexpr int ROOM_CANDIDATES = 20;
void RoomUnit(Game& g, Vehicle& v, RecoveryState& state) {
    static const float steers[5] = { 0.0f, -0.5f, 0.5f, -1.0f, 1.0f };
    static const float times[4] = { 0.3f, 0.6f, 0.9f, 1.2f };
    int k = state.roomNext++;
    Candidate c;
    c.gear = AwayGear(v, state.roomFor);
    c.steer = steers[k % 5];
    float drive = times[k / 5];
    work.current.candidates++;
    c = Predict(g, v, state, c, drive + Tuning().stopTail, true);
    if (c.safe && c.distance > 2) {
        OBB other = ObservedBox(state.roomFor);
        float gain = OBBGap(MakeOBB(terminalPos, terminalAngle, v.width * 0.5f, v.length * 0.5f), other)
                   - OBBGap(v.Box(), other);
        if (gain > state.roomBestGain + 0.5f) {
            state.roomBestGain = gain; state.roomBest = k;
            state.candidates[0] = c;          // the slot is free while a role replaces planning
            state.roomTime = drive;
        }
    }
    if (state.roomNext >= ROOM_CANDIDATES) state.roomPlanning = false;
}

} // namespace

float RecoveryFreeRoom(Game& g, int idx, int other) {
    if (observedGame != &g || idx < 0 || other < 0 || idx >= (int)observedVehicles.size() || other >= (int)observedVehicles.size())
        return 0;
    const Vehicle& v = g.vehicles[idx];
    GatherNearby(g, v, idx);
    Vector2 dir = v.Fwd() * (float)AwayGear(v, other);
    float limit = v.length * 1.2f, step = 4;
    for (float d = step; d <= limit; d += step) {
        OBB box = v.Box(); box.c = box.c + dir * d;
        for (int k = 0; k < nearbyCount; k++) {
            float depth = 0;
            // An existing contact may be left behind, not deepened.
            if (Overlap(nearby[k], box, 0, Tuning().clearance, depth) && depth > nearby[k].initialDepth + 0.025f)
                return d - step;
        }
    }
    return limit;
}

void RecoveryStartRoom(Vehicle& v, int other) {
    RecoveryState& state = v.ai.recovery;
    state.roomFor = other;
    state.roomPlanning = false; state.roomCreeps = 0; state.roomRetry = 0; state.moveLeft = -1;
    state.planning = false; state.planWait = 0;
    state.gear = 0; state.steer = 0; state.tracking = false;
}

void RecoveryEndRoom(Vehicle& v) {
    RecoveryState& state = v.ai.recovery;
    state.roomFor = -1; state.roomPlanning = false; state.moveLeft = -1;
    state.gear = 0; state.steer = 0; state.tracking = false;
    state.nextPlan = 0; state.commit = 0; state.holdSignature = 0;   // plan afresh
}

void RecoveryReset(RecoveryState& state) { state = RecoveryState{}; }

const char* RecoveryReasonText(RecoveryReason reason) {
    switch (reason) {
    case RecoveryReason::Assessing: return "assessing";
    case RecoveryReason::Forward: return "forward";
    case RecoveryReason::Reverse: return "reverse";
    case RecoveryReason::GearChange: return "gear_change";
    case RecoveryReason::NoFeasibleManoeuvre: return "no_feasible_manoeuvre";
    case RecoveryReason::Hazard: return "immediate_hazard";
    case RecoveryReason::Disabled: return "disabled";
    case RecoveryReason::Queued: return "queued_behind_vehicle";
    }
    return "unknown";
}

const char* RejoinCauseText(RejoinCause cause) {
    switch (cause) {
    case RejoinCause::Unavailable: return "unavailable";
    case RejoinCause::MissingSnapshot: return "missing_snapshot";
    case RejoinCause::Capacity: return "capacity";
    case RejoinCause::InitialContact: return "initial_contact";
    case RejoinCause::InitialClearance: return "initial_clearance";
    case RejoinCause::UnsafeSweep: return "unsafe_sweep";
    case RejoinCause::IncompleteStop: return "incomplete_stop";
    case RejoinCause::Clear: return "clear";
    case RejoinCause::QueuedBehind: return "queued_behind";
    }
    return "unknown";
}

void RecoveryBeginFrame(Game& g) {
    FinishTimingFrame();
    auto start = Clock::now();
    Tuning();
    bool recovering = false;
    budget = PlanBudget{};
    for (const Vehicle& v : g.vehicles)
        if (v.active && v.driver == DriverType::Traffic && !v.ai.rail && !v.wrecked && !v.burning) {
            recovering = true;
            if (v.ai.recovery.planning || v.ai.recovery.planWait > 0)
                budget.oldestWait = std::max(budget.oldestWait, v.ai.recovery.planWait);
        }
    observedVehicles.resize(g.vehicles.size());
    if (recovering && vehicleForecasts.size() < g.vehicles.size()) vehicleForecasts.resize(g.vehicles.size());
    forecastEpoch++;
    for (int idx = 0; idx < (int)g.vehicles.size(); idx++) {
        const Vehicle& v = g.vehicles[idx];
        ObservedVehicle& o = observedVehicles[idx];
        o.active = v.active; o.pos = v.pos; o.vel = v.vel;
        o.angle = v.angle; o.angVel = v.angVel; o.width = v.width; o.length = v.length;
        float halfWidth = v.width * 0.5f, halfLength = v.length * 0.5f;
        o.radius = sqrtf(halfWidth * halfWidth + halfLength * halfLength);
        // A yielding rail car retraces its path: its path speed is negative.
        o.rail = AIOnRail(v); o.speed = v.ai.yieldTo >= 0 ? -v.ai.speed : v.ai.speed;
        o.minDistance = v.ai.yieldTo >= 0 ? v.ai.s - std::max(0.0f, v.ai.retreatLeft) : -1e9f;
        // A rail driver does not pass its planned stop (the line, a person, a stopped or
        // knocked car ahead): forecasting it beyond made a car queued behind a recovering
        // one look like an incoming collision each time the recovering car moved off.
        o.maxDistance = o.rail && v.ai.yieldTo < 0 && v.ai.stopDist < 1e8f ? v.ai.s + std::max(0.0f, v.ai.stopDist) : 1e9f; o.pathDistance = v.ai.s; o.shift = v.ai.laneShift;
        o.shiftFrom = v.ai.shiftFrom; o.shiftTo = v.ai.shiftTo; o.shiftS0 = v.ai.shiftS0; o.shiftS1 = v.ai.shiftS1;
        // A constant offset swings by at most twice itself as the path turns; during a lane
        // change the rear-axle pose adds up to a third of the change (slope * axle).
        o.shiftSpan = 2 * std::max(fabsf(o.shiftFrom), fabsf(o.shiftTo)) + 0.35f * fabsf(o.shiftTo - o.shiftFrom);
        o.blend = v.ai.blend; o.blendPos = v.ai.blendPos; o.blendAngle = v.ai.blendAng;
        o.pathCount = 0;
        // 4 s is the longest forecast; the drift bound encloses the residual motion.
        o.drift = o.rail ? 0.0f : (Len(o.vel) + o.radius * fabsf(o.angVel)) * 4.0f;
        o.stationary = o.rail ? ((o.speed == 0 || o.maxDistance <= o.pathDistance) && o.blend <= 0) : o.drift <= 0.05f;
        if (recovering && o.rail) {
            for (const Waypoint& p : v.ai.path) {
                if (o.pathCount == MAX_PATH_POINTS) break;
                o.path[o.pathCount++] = { p.p, p.cum };
            }
        }
        // Cache the raw route centre separately from the authoritative actual
        // body. A newly changed rail can otherwise invalidate travel bounds.
        o.routeGap = o.rail && o.pathCount > 1 ? Dist(ObservedRailCentre(o, 0), o.pos) : 0;
    }
    observedPeople.resize(g.peds.size() + 1);
    for (int idx = 0; idx < (int)g.peds.size(); idx++) {
        const Pedestrian& p = g.peds[idx];
        observedPeople[idx] = { p.pos, p.vel, p.active };
    }
    observedPeople.back() = { g.player.pos, g.player.vel, !g.player.inVehicle };
    observedGame = &g;
    timing.frameMs = Milliseconds(start);
    timing.pending = true;
}

void RecoveryDrive(Game& g, int idx, Vector2 laneOrigin, Vector2 laneForward, float dt) {
    auto start = Clock::now();
    Vehicle& v = g.vehicles[idx];
    RecoveryState& state = v.ai.recovery;
    if (!std::isfinite(dt) || dt <= 0 || observedGame != &g || observedVehicles.size() != g.vehicles.size()) {
        // The caller must supply a common snapshot; a missing one cannot authorize
        // a manoeuvre based on a mixture of old/new poses.
        v.in = Controls(v, 0, 0);
        state.reason = RecoveryReason::Hazard;
        timing.frameMs += Milliseconds(start);
        return;
    }
    SetControlInterval(dt);
    if (!state.initialized) {
        state.initialized = true;
        state.origin = laneOrigin; state.forward = Norm(laneForward);
        if (Len2(state.forward) < 0.5f) state.forward = v.Fwd();
        state.lastPos = v.pos; state.lastLaneError = LaneError(v, state);
    }
    float moved = Dist(v.pos, state.lastPos);
    state.travelled += moved;
    if (state.gear < 0) state.reverseDistance += moved;
    state.lastProgress = state.lastLaneError - LaneError(v, state);
    state.lastLaneError = LaneError(v, state);
    state.lastPos = v.pos;
    state.stalled = state.gear != 0 && moved < 2 * dt ? state.stalled + dt : 0;
    state.nextPlan -= dt; state.commit -= dt; state.holdAge += dt;
    if (v.wrecked || v.burning || v.driver != DriverType::Traffic) {
        state.gear = 0; state.reason = RecoveryReason::Disabled;
        state.planning = false; state.planWait = 0;
        v.in = Controls(v, 0, 0);
        timing.frameMs += Milliseconds(start);
        return;
    }
    // A hold between planning jobs neither checks nor plans this frame: it needs no
    // neighbourhood. Moving, planning or due to (re)plan, the car gathers it first.
    bool room = state.roomFor >= 0;
    if (room) {
        state.roomRetry -= dt;
        if (state.moveLeft >= 0) {
            state.moveLeft -= dt;
            if (state.moveLeft <= 0) {               // the creep is done: hold and let them out
                state.moveLeft = -1; state.gear = 0; state.steer = 0;
                state.reason = RecoveryReason::NoFeasibleManoeuvre;
                state.roomRetry = 1.0f;
            }
        }
    }
    bool idleHold = room ? state.gear == 0 && !state.roomPlanning && state.roomRetry > 0
                         : state.gear == 0 && !state.planning && state.nextPlan > 0;
    if (!idleHold) {
        GatherNearby(g, v, idx);
        if (!nearbyComplete) {
            state.gear = 0; state.reason = RecoveryReason::NoFeasibleManoeuvre;
            state.planning = false; state.planWait = 0;
            v.in = Controls(v, 0, 0);
            timing.frameMs += Milliseconds(start);
            return;
        }
    }
    // A committed move is checked through stopping, not just until the next update.
    // An actor entering its sweep invalidates it immediately, even between plans.
    bool urgent = false;
    work.current.recovering++;
    bool covered = state.gear != 0 && CheckCovered(v, state);
    if (covered) { state.checkFrames--; state.checkStep++; timing.coveredChecks++; }
    else if (state.gear != 0) {
        auto immediateStart = Clock::now();
        work.current.immediate++;
        Candidate c; c.gear = state.gear; c.steer = state.steer;
        c.tracking = state.tracking;
        float immediate = std::max(0.3f, fabsf(Dot(v.vel, v.Fwd())) / 160 + dt + 0.12f);
        // Validate a few frames more of moving time, so the next frames can reuse it.
        int extra = std::clamp((int)lroundf(COVER_TIME / dt), 0, (int)state.checkPos.size());
        captureDt = dt; captureCount = 0;
        float moving = immediate + extra * dt;
        if (state.moveLeft >= 0) moving = std::min(moving, state.moveLeft);   // a creep ends earlier
        c = Predict(g, v, state, c, std::min(Tuning().horizon, moving + Tuning().stopTail), true);
        captureDt = 0;
        state.checkFrames = 0;
        if (c.safe && captureCount >= extra) RecordCheck(v, state, extra);
        if (!c.safe) {
            state.gear = 0; state.steer = 0; state.commit = 0; state.nextPlan = 0;
            state.tracking = false;
            state.reason = RecoveryReason::Hazard;
            urgent = true;
            if (room) { state.moveLeft = -1; state.roomRetry = 0.5f; }
        }
        work.current.immediateMs += Milliseconds(immediateStart);
    }
    // A hazard restarts any job: earlier rollouts did not foresee the new conflict.
    if (urgent) state.planning = false;
    if (room) {
        // Making room replaces planning: at most three creeps, each assessed afresh.
        if (!state.roomPlanning && state.gear == 0 && state.roomRetry <= 0 && state.roomCreeps < 3) {
            state.roomPlanning = true; state.roomNext = 0; state.roomBest = -1; state.roomBestGain = 2.0f;
            state.plans++; timing.plans++;
        }
        if (state.roomPlanning) {
            auto planStart = Clock::now();
            long long frameBudget = (long long)(Tuning().planningSteps * std::clamp(dt * 60.0f, 1.0f, 3.0f));
            while (state.roomPlanning && budget.steps < frameBudget) {
                long long before = work.current.steps;
                RoomUnit(g, v, state);
                budget.steps += work.current.steps - before;
            }
            if (!state.roomPlanning) {
                if (state.roomBest >= 0) {
                    const Candidate& c = state.candidates[0];
                    state.gear = c.gear; state.steer = c.steer; state.tracking = false;
                    state.moveLeft = state.roomTime;
                    state.reason = state.gear > 0 ? RecoveryReason::Forward : RecoveryReason::Reverse;
                    state.roomCreeps++;
                } else state.roomRetry = 1.0f;     // nothing opens a gap now; look again later
            }
            work.current.planMs += Milliseconds(planStart);
        }
    } else if (!state.planning && state.nextPlan <= 0 && (state.commit <= 0 || state.gear == 0 || urgent)) {
        bool holding = state.reason == RecoveryReason::NoFeasibleManoeuvre || state.reason == RecoveryReason::Queued;
        if (state.gear == 0 && holding && state.holdAge < HOLD_RECHECK && HoldSignature(v, state) == state.holdSignature) {
            // Same pose, statics and blockers: the previous search still applies.
            state.nextPlan = Tuning().planningInterval;
            timing.unchangedHolds++;
        } else {
            state.planning = true; state.planNext = 0; state.planBest = -1;
            state.blockIds.fill(0); state.blockVotes.fill(0);
            state.plans++; timing.plans++;
        }
    }
    if (state.planning && !room) {
        auto planStart = Clock::now();
        // Rollouts share a deterministic per-frame step budget, scaled to the frame
        // interval. Only the longest-waiting jobs may use it, so none starves.
        long long frameBudget = (long long)(Tuning().planningSteps * std::clamp(dt * 60.0f, 1.0f, 3.0f));
        if (state.planWait >= budget.oldestWait) {
            while (state.planning && budget.steps < frameBudget) {
                long long before = work.current.steps;
                PlanUnit(g, v, state);
                budget.steps += work.current.steps - before;
            }
        }
        if (state.planning) {
            state.planWait++;
            timing.deferredFrames++;
            timing.maxWaitFrames = std::max(timing.maxWaitFrames, state.planWait);
        } else state.planWait = 0;
        work.current.planMs += Milliseconds(planStart);
    }
    v.in = Controls(v, state.gear, state.steer, state.tracking ? &state : nullptr);
    float speed = Dot(v.vel, v.Fwd());
    if ((state.gear > 0 && speed < -6) || (state.gear < 0 && speed > 6)) state.reason = RecoveryReason::GearChange;
    timing.frameMs += Milliseconds(start);
}

bool RecoveryCanRejoin(Game& g, int idx, float dt) {
    auto start = Clock::now();
    auto finish = [&](RejoinCause cause) {
        if (idx >= 0 && idx < (int)g.vehicles.size()) g.vehicles[idx].ai.recovery.rejoinCause = cause;
        timing.frameMs += Milliseconds(start);
        return cause == RejoinCause::Clear || cause == RejoinCause::QueuedBehind;
    };
    if (idx < 0 || idx >= (int)g.vehicles.size() || !std::isfinite(dt) || dt <= 0)
        return finish(RejoinCause::Unavailable);
    if (observedGame != &g || observedVehicles.size() != g.vehicles.size() ||
        vehicleForecasts.size() < g.vehicles.size()) return finish(RejoinCause::MissingSnapshot);
    const Vehicle& v = g.vehicles[idx];
    SetControlInterval(dt);
    if (!v.active || !v.Drivable() || v.driver != DriverType::Traffic)
        return finish(RejoinCause::Unavailable);
    // Check at least one vehicle length of forward intent plus a real stopping tail.
    // The long-vehicle horizon remains bounded at 4 s, with matching broadphase reach.
    float horizon = std::min(4.0f, std::max(Tuning().horizon,
        v.length / Tuning().forwardSpeed + Tuning().stopTail + dt));
    GatherNearby(g, v, idx, horizon);
    if (!nearbyComplete) return finish(RejoinCause::Capacity);
    bool initialClearance = false;
    for (int k = 0; k < nearbyCount; k++) {
        // Recovery may escape existing contact; rejoining an infinite-mass rail while
        // still touching a physical actor is never allowed. Report actual contact in
        // preference to a clearance-only overlap anywhere in the neighbourhood.
        if (nearby[k].initialDepth <= 0) continue;
        float depth = 0;
        if (Overlap(nearby[k], geometry.initial, 0, 0, depth)) return finish(RejoinCause::InitialContact);
        initialClearance = true;
    }
    if (initialClearance) return finish(RejoinCause::InitialClearance);
    RecoveryState state; state.origin = v.pos; state.forward = v.Fwd();
    Candidate forward; forward.gear = 1;
    RejoinCause cause;
    auto rejoinStart = Clock::now();
    work.current.rejoins++;
    Candidate checked = Predict(g, v, state, forward, horizon, true, &cause);
    work.current.rejoinMs += Milliseconds(rejoinStart);
    // A stopped car ahead in the lane is a queue, not an obstacle course: the rail
    // driver waits behind it (or passes it) under the ordinary rail rules. Static
    // geometry ahead still vetoes the handoff.
    if (cause == RejoinCause::UnsafeSweep && v.Speed() < 5 && QueuedAhead(v, v.Fwd(), checked.blocker))
        cause = RejoinCause::QueuedBehind;
    return finish(cause);
}

void DecisionStageAdd(DecisionStage stage, double ms) { stageMs[(size_t)stage] += ms; }

void RecoveryResetStats() { timing = Timing{}; decisionTiming = Timing{}; work = WorkProfile{}; stageMs.fill(0); }

static RecoveryStats ReadTimingStats(Timing& record) {
    FinishTimingFrame(record);
    RecoveryStats stats;
    stats.frames = record.frames; stats.plans = record.plans;
    stats.rejected = record.rejected; stats.holds = record.holds;
    stats.deferredFrames = record.deferredFrames; stats.unchangedHolds = record.unchangedHolds;
    stats.maxWaitFrames = record.maxWaitFrames; stats.coveredChecks = record.coveredChecks;
    stats.averageMs = record.totalMs / std::max(1, record.frames);
    stats.worstMs = record.worstMs; stats.percentileSamples = record.samples;
    std::array<double, MAX_TIMINGS> sorted{};
    std::copy(record.values.begin(), record.values.begin() + record.samples, sorted.begin());
    if (record.samples > 0) {
        int index = std::max(0, (int)ceil(record.samples * 0.95) - 1);
        std::nth_element(sorted.begin(), sorted.begin() + index, sorted.begin() + record.samples);
        stats.p95Ms = sorted[index];
    }
    return stats;
}

RecoveryStats RecoveryGetStats() { return ReadTimingStats(timing); }
RecoveryStats RecoveryGetDecisionStats() { return ReadTimingStats(decisionTiming); }

void RecoveryRecordDecisionTime(double ms) {
    if (!std::isfinite(ms) || ms < 0) return;
    decisionTiming.frameMs = ms; decisionTiming.pending = true;
    FinishTimingFrame(decisionTiming);
}

void RecoveryLogStats() {
    RecoveryStats s = RecoveryGetStats();
    TraceLog(LOG_INFO, "RECOVERY CPU: avg %.4f ms p95 %.4f ms worst %.4f ms | frames %d percentile samples %d | plans %d rejected %d holds %d | deferred job-frames %d longest wait %d frames | unchanged holds kept %d | covered immediate checks %d",
             s.averageMs, s.p95Ms, s.worstMs, s.frames, s.percentileSamples, s.plans, s.rejected, s.holds,
             s.deferredFrames, s.maxWaitFrames, s.unchangedHolds, s.coveredChecks);
    const WorkFrame& t = work.total;
    double f = std::max(1, work.frames);
    TraceLog(LOG_INFO, "RECOVERY WORK per frame: recovering %.2f gathers %.2f immediate %.2f tracking %.2f candidates %.2f rejoins %.2f steps %.0f actor_tests %.0f overlaps %.0f forecasts %.0f nearby_max %d | ms gather %.4f immediate %.4f plan %.4f rejoin %.4f",
             t.recovering / f, t.gathers / f, t.immediate / f, t.tracking / f, t.candidates / f, t.rejoins / f,
             t.steps / f, t.actorTests / f, t.overlaps / f, t.forecasts / f, t.nearbyMax,
             t.gatherMs / f, t.immediateMs / f, t.planMs / f, t.rejoinMs / f);
    std::array<WorkFrame, WORST_WORK> worst = work.worst;
    std::sort(worst.begin(), worst.end(), [](const WorkFrame& a, const WorkFrame& b) { return a.ms > b.ms; });
    for (const WorkFrame& w : worst) {
        if (w.ms <= 0) continue;
        TraceLog(LOG_INFO, "  RECOVERY WORST %.4f ms: recovering %d gathers %d immediate %d tracking %d candidates %d rejoins %d steps %lld actor_tests %lld overlaps %lld forecasts %lld nearby_max %d | ms gather %.4f immediate %.4f plan %.4f rejoin %.4f",
                 w.ms, w.recovering, w.gathers, w.immediate, w.tracking, w.candidates, w.rejoins, w.steps, w.actorTests,
                 w.overlaps, w.forecasts, w.nearbyMax, w.gatherMs, w.immediateMs, w.planMs, w.rejoinMs);
    }
    RecoveryStats d = RecoveryGetDecisionStats();
    TraceLog(LOG_INFO, "DRIVER DECISION CPU (includes police and cleanup): avg %.4f ms p95 %.4f ms worst %.4f ms | frames %d percentile samples %d",
             d.averageMs, d.p95Ms, d.worstMs, d.frames, d.percentileSamples);
    double df = std::max(1, d.frames);
    TraceLog(LOG_INFO, "DRIVER DECISION STAGES per frame: cleanup %.4f grid %.4f snapshot %.4f rail %.4f knocked %.4f police %.4f ms",
             stageMs[0] / df, stageMs[1] / df, stageMs[2] / df, stageMs[3] / df, stageMs[4] / df, stageMs[5] / df);
}
