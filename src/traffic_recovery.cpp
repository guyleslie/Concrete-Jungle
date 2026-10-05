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

namespace {

constexpr float PREDICT_STEP = 1.0f / 240.0f;
constexpr float OBB_GROW_RADIUS = 1.414214f;  // rounded above sqrt(2): both half-extents grow
constexpr int MAX_PATH_POINTS = 96;
constexpr int MAX_NEARBY = 1024;
constexpr int MAX_TIMINGS = 16384;
constexpr int MAX_FORECAST_SAMPLES = 962;     // 4 s at 240 Hz, including the initial pose

struct Settings {
    float forwardSpeed = 75, reverseSpeed = 60;
    float horizon = 2.4f, stopTail = 0.5f, clearance = 1;
    float planningInterval = 0.25f, commitment = 0.45f, hysteresis = 6;
    float stallTime = 1, nearbyRadius = 900;
};

const Settings& Tuning() {
    static Settings settings;
    static bool loaded = false;
    if (loaded) return settings;
    loaded = true;
    struct Field { const char* name; float* value; float low, high; };
    Field fields[] = {
        { "forward_speed", &settings.forwardSpeed, 20, 100 },
        { "reverse_speed", &settings.reverseSpeed, 15, 80 },
        { "horizon", &settings.horizon, 1, 4 },
        { "stop_tail", &settings.stopTail, 0.25f, 1 },
        { "clearance", &settings.clearance, 0.25f, 1.6f },
        { "planning_interval", &settings.planningInterval, 0.1f, 1 },
        { "commitment", &settings.commitment, 0.15f, 1 },
        { "hysteresis", &settings.hysteresis, 0, 30 },
        { "stall_time", &settings.stallTime, 0.5f, 3 },
        { "nearby_radius", &settings.nearbyRadius, 300, 1500 }
    };
    for (const DataRecord& r : ReadDataFile("assets/data/traffic.cfg")) {
        bool found = false;
        if (r.Is("RECOVERY") && r.size() == 3) {
            for (Field& f : fields) {
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
        if (!found) TraceLog(LOG_WARNING, "TRAFFIC DATA line %d: unknown or malformed recovery record", r.line);
    }
    settings.stopTail = std::min(settings.stopTail, settings.horizon * 0.5f);
    return settings;
}

struct PathPoint { Vector2 pos{}; float distance = 0; };
struct ObservedVehicle {
    bool active = false, rail = false;
    Vector2 pos{}, vel{};
    float angle = 0, angVel = 0, width = 0, length = 0;
    float pathDistance = 0, speed = 0, shift = 0, radius = 0, routeGap = 0;
    float blend = 0, blendAngle = 0;
    Vector2 blendPos{};
    int pathCount = 0;
    std::array<PathPoint, MAX_PATH_POINTS> path{};
};
struct ObservedPerson { Vector2 pos{}, vel{}; bool active = false; };
std::vector<ObservedVehicle> observedVehicles;
std::vector<ObservedPerson> observedPeople;
Game* observedGame = nullptr;

struct ForecastSample { OBB box{}; float sweptPad = 0; };
struct ForecastRow {
    int count = 0;
    std::array<ForecastSample, MAX_FORECAST_SAMPLES> samples{};
};
std::vector<ForecastRow> vehicleForecasts;
float predictionStep = PREDICT_STEP;
float predictionControlInterval = 1.0f / 60.0f;

void SetControlInterval(float dt) {
    int substeps = std::clamp((int)ceilf(dt * 240 - 0.01f), 1, 8);
    // Match the 60/20 Hz fixtures. Higher frame rates use the same force model at
    // a bounded 240 Hz forecast cadence, never unbounded work as frame dt shrinks.
    float h = std::max(PREDICT_STEP, dt / substeps);
    if (fabsf(h - predictionStep) > 1e-7f)
        for (ForecastRow& row : vehicleForecasts) row.count = 0;
    predictionStep = h;
    predictionControlInterval = std::max(PREDICT_STEP, dt);
}

using Clock = std::chrono::steady_clock;
struct Timing {
    bool pending = false;
    double frameMs = 0, totalMs = 0, worstMs = 0;
    int frames = 0, samples = 0, cursor = 0;
    int plans = 0, rejected = 0, holds = 0;
    std::array<double, MAX_TIMINGS> values{};
} timing;
Timing decisionTiming;

double Milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void FinishTimingFrame(Timing& record = timing) {
    if (!record.pending) return;
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
    for (int k = 0; k + 1 < o.pathCount; k++) {
        const PathPoint& a = o.path[k];
        const PathPoint& b = o.path[k + 1];
        if (distance <= b.distance)
            return LerpV(a.pos, b.pos, Saturate((distance - a.distance) / std::max(0.001f, b.distance - a.distance)));
    }
    // A snapshot cannot plan another junction. Extend only its final known tangent.
    const PathPoint& last = o.path[o.pathCount - 1];
    return last.pos + Norm(last.pos - o.path[o.pathCount - 2].pos) * (distance - last.distance);
}

Vector2 ObservedRailCentre(const ObservedVehicle& o, float time, Vector2* heading = nullptr) {
    float axle = o.length * 0.32f;
    float distance = o.pathDistance + o.speed * time;
    Vector2 front = SampleObservedPath(o, distance + axle);
    Vector2 rear = SampleObservedPath(o, distance - axle);
    Vector2 direction = Norm(front - rear);
    if (Len2(direction) < 0.5f) direction = Forward(o.angle);
    if (heading) *heading = direction;
    return (front + rear) * 0.5f + Perp(direction) * o.shift;
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
    while (row.count <= step) {
        ForecastSample& sample = row.samples[row.count];
        sample.box = PredictObserved(observedVehicles[actor], row.count * predictionStep, 0);
        sample.sweptPad = row.count == 0 ? 0
            : PoseMotionBound(row.samples[row.count - 1].box, sample.box, observedVehicles[actor].radius);
        row.count++;
    }
    return row.samples[step];
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
};
std::array<Nearby, MAX_NEARBY> nearby;
int nearbyCount = 0;
std::vector<int> queryBuildings, queryObjects;
bool nearbyComplete = true;

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
    const OBB* observed = nullptr;
    if (value.kind == GeometryKind::Vehicle) {
        // Every driver reads this same initial footprint; do not repeat its
        // rail path/trigonometry for every recovery neighbourhood.
        observed = &ForecastAt(value.observed, 0).box;
        value.forecastOrigin = observed->c;
    }
    bool hit = Overlap(value, geometry.initial, 0, Tuning().clearance, depth, observed);
    // OBBOverlap may leave depth at its sentinel or a previous axis depth
    // when a separating axis rejects the pair. Only a true hit has depth.
    value.initialDepth = hit ? std::max(0.0f, depth) : 0.0f;
    nearby[nearbyCount++] = value;
}

void GatherNearby(Game& g, const Vehicle& v, int self, float horizon = -1) {
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
            incomingReach += o.routeGap + 2 * fabsf(o.shift);
            if (o.blend > 0) incomingReach = std::max(incomingReach, Dist(o.blendPos, o.pos));
        }
        float reach = std::max(cfg.nearbyRadius, ownReach + radius + incomingReach);
        if (Len2(o.pos - v.pos) > reach * reach) continue;
        Nearby n; n.kind = GeometryKind::Vehicle; n.observed = idx;
        n.centre = o.pos; n.velocity = o.vel; n.radius = radius;
        AddNearby(n);
    }
    for (int idx = 0; idx < (int)observedPeople.size(); idx++) {
        const ObservedPerson& p = observedPeople[idx];
        if (!p.active) continue;
        float reach = ownReach + PED_RADIUS + Len(p.vel) * horizon;
        if (Len2(p.pos - v.pos) > reach * reach) continue;
        Nearby n; n.kind = GeometryKind::Person; n.centre = p.pos;
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

struct Candidate {
    int gear = 0;
    float steer = 0, secondSteer = 0, switchTime = 0;
    bool safe = false;
    bool tracking = false;
    bool terminalAligned = false;
    float score = -1e9f, distance = 0, headingGain = 0, lateralGain = 0, laneImprovement = 0;
};

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

bool FootprintClear(const Vehicle& scratch, int step, float time, float h, float sweptPad) {
    const Settings& cfg = Tuning();
    OBB box = scratch.Box();
    if (!InsideWorld(box)) return false;
    for (int idx = 0; idx < nearbyCount; idx++) {
        Nearby& n = nearby[idx];
        const OBB* observed = nullptr;
        OBB partialPose;
        Vector2 position = n.centre + n.velocity * time;
        float otherPad = n.centreSpeed * h;
        float growScale = n.kind == GeometryKind::Person || n.kind == GeometryKind::ObjectCircle
            ? 1.0f : OBB_GROW_RADIUS;
        if (n.kind == GeometryKind::Vehicle) {
            const ObservedVehicle& actor = observedVehicles[n.observed];
            float baseReach = geometry.diagonal + n.radius + growScale * (cfg.clearance + sweptPad);
            bool distant = false;
            if (!actor.rail || actor.pathCount <= 1) {
                // The physical forecast centre is linear. Its endpoint pad is
                // bounded by translation + radius * |angular velocity| * h;
                // wrapped endpoint rotation can never exceed that angle.
                float bound = baseReach + growScale * (otherPad + n.radius * fabsf(actor.angVel) * h);
                distant = Len2(position - scratch.pos) > bound * bound;
            } else if (actor.blend <= 0) {
                // Each axle point moves at most |path speed| * time. Their
                // midpoint inherits that bound; a fixed lane shift contributes
                // at most 2*|shift| as its direction changes. For the existing
                // inflated-endpoint test, also enclose the preceding sample's
                // translation/shift and its worst wrapped rotation (pi). The
                // actual initial pose may differ from route centre zero;
                // include that gap both in centre travel and the first sweep.
                // A skipped actor passes the original distance cull too.
                float shiftSpan = 2 * fabsf(actor.shift);
                float travel = fabsf(actor.speed) * time + shiftSpan + actor.routeGap;
                float maxSweep = fabsf(actor.speed) * h + shiftSpan + n.radius * PI + actor.routeGap;
                float bound = baseReach + travel + growScale * maxSweep;
                distant = Len2(n.forecastOrigin - scratch.pos) > bound * bound;
            }
            // Legacy blend mixes a separate pose, so it deliberately bypasses
            // the path-only envelope and receives the full exact forecast.
            if (distant) { n.released = true; continue; }
            if (step < MAX_FORECAST_SAMPLES && fabsf(time - step * predictionStep) < 1e-6f) {
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
        if (Len2(position - scratch.pos) > reach * reach) { n.released = true; continue; }
        float depth = 0;
        bool hit = Overlap(n, box, time, cfg.clearance, depth, observed);
        if (!n.released && n.initialDepth > 0) {
            if (!hit) n.released = true;
            else {
                if (depth > n.previousDepth + 0.025f) return false;
                n.previousDepth = std::min(n.previousDepth, depth);
                continue;
            }
        }
        // Inflating the endpoint by its translation + corner rotation encloses the
        // intervening footprint; moving actors get the same conservative treatment.
        if (Overlap(n, box, time, cfg.clearance + sweptPad + otherPad, depth, observed)) return false;
    }
    return true;
}

Candidate Predict(Game& g, const Vehicle& v, const RecoveryState& state, Candidate candidate, float horizon, bool stoppingTail,
                  RejoinCause* cause = nullptr) {
    if (cause) *cause = RejoinCause::UnsafeSweep;
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
            !FootprintClear(scratch, k + 1, time + h, h, sweptPad)) return candidate;
        travelled += moved;
    }
    // An unfinished stop must not certify room to stop beyond the checked corridor.
    if (stoppingTail && scratch.Speed() > 2) {
        if (cause) *cause = RejoinCause::IncompleteStop;
        return candidate;
    }
    candidate.safe = true;
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

bool MakesLaneProgress(const Candidate& candidate) {
    // A long vehicle must temporarily turn across the lane; its initial heading cost
    // must not veto genuine lateral progress. Driving straight back into a dead end
    // provides neither lateral nor heading improvement and does not cancel retreat.
    // Once aligned, forward travel can be necessary to reach the next clear block
    // segment. Zero remaining lane error must not manufacture another reverse escape.
    return (candidate.gear > 0 && candidate.terminalAligned) || candidate.laneImprovement > 0.25f ||
           candidate.lateralGain > 2 || candidate.headingGain > 0.03f;
}

void SelectPlan(Game& g, Vehicle& v, RecoveryState& state) {
    const Settings& cfg = Tuning();
    state.plans++; timing.plans++;
    Candidate tracking; tracking.gear = 1; tracking.tracking = true;
    tracking = Predict(g, v, state, tracking, cfg.horizon, true);
    // Normal lane convergence does not need an escape search. It receives the same
    // complete swept check; unsuccessful or stalled tracking still evaluates all arcs.
    if (tracking.safe && tracking.distance > 2 && MakesLaneProgress(tracking) && state.stalled <= cfg.stallTime) {
        CommitCandidate(state, tracking);
        return;
    }
    std::array<Candidate, 20> candidates{};
    int count = 0;
    // Preserve index zero for the original hold-first score tie breaker.
    // Its full rollout is unnecessary when the winning moving candidate
    // already implies hold (none safe, filtered out, or distance <= 2).
    candidates[count++] = Candidate{};
    candidates[count++] = tracking;
    for (int gear : { 1, -1 }) {
        for (float steer : { 0.0f, -0.5f, 0.5f, -1.0f, 1.0f }) {
            Candidate c; c.gear = gear; c.steer = steer;
            candidates[count++] = Predict(g, v, state, c, cfg.horizon, true);
        }
        // Unwinding after an initial arc can clear a corner that a constant-steer
        // rollout would hit. Execution only commits the first, validated segment.
        for (float steer : { -0.5f, 0.5f, -1.0f, 1.0f }) {
            Candidate c; c.gear = gear; c.steer = steer;
            c.switchTime = std::max(cfg.commitment, (cfg.horizon - cfg.stopTail) * 0.5f);
            c.secondSteer = 0;
            candidates[count++] = Predict(g, v, state, c, cfg.horizon, true);
        }
    }
    bool forwardAvailable = false;
    for (int k = 1; k < count; k++) {
        if (!candidates[k].safe) { state.rejected++; timing.rejected++; }
        if (candidates[k].safe && candidates[k].gear == 1 && candidates[k].distance > 5 && MakesLaneProgress(candidates[k]))
            forwardAvailable = true;
    }
    int best = -1;
    for (int k = 1; k < count; k++) {
        Candidate& c = candidates[k];
        if (!c.safe) continue;
        if (state.gear < 0 && c.gear > 0 && !MakesLaneProgress(c)) continue;
        if (c.gear == -1 && !forwardAvailable) c.score += std::min(c.distance, v.length * 2) * 0.3f;
        if (c.gear == 0) c.score -= 3;
        if (state.stalled > cfg.stallTime && c.gear == state.gear && fabsf(c.steer - state.steer) < 0.2f) c.score -= 20;
        if (best < 0 || c.score > candidates[best].score) best = k;
    }
    state.nextPlan = cfg.planningInterval;
    state.commit = cfg.commitment;
    if (best >= 0 && candidates[best].distance > 2) {
        Candidate& hold = candidates[0];
        hold = Predict(g, v, state, hold, cfg.horizon, true);
        if (!hold.safe) { state.rejected++; timing.rejected++; }
        else {
            hold.score -= 3;
            if (state.stalled > cfg.stallTime && hold.gear == state.gear &&
                fabsf(hold.steer - state.steer) < 0.2f) hold.score -= 20;
            // Hold came first in the original candidate array: equality must
            // still favour hold, while all moving ties retain their order.
            if (hold.score >= candidates[best].score) best = 0;
        }
    }
    if (best < 0 || candidates[best].gear == 0 || candidates[best].distance <= 2) {
        state.gear = 0; state.steer = 0;
        state.tracking = false;
        state.reason = RecoveryReason::NoFeasibleManoeuvre;
        state.holds++; timing.holds++;
        return;
    }
    CommitCandidate(state, candidates[best]);
}

} // namespace

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
    }
    return "unknown";
}

void RecoveryBeginFrame(Game& g) {
    FinishTimingFrame();
    auto start = Clock::now();
    Tuning();
    bool recovering = false;
    for (const Vehicle& v : g.vehicles)
        if (v.active && v.driver == DriverType::Traffic && !v.ai.rail && !v.wrecked && !v.burning) recovering = true;
    observedVehicles.resize(g.vehicles.size());
    if (recovering && vehicleForecasts.size() < g.vehicles.size()) vehicleForecasts.resize(g.vehicles.size());
    for (ForecastRow& forecast : vehicleForecasts) forecast.count = 0;
    for (int idx = 0; idx < (int)g.vehicles.size(); idx++) {
        const Vehicle& v = g.vehicles[idx];
        ObservedVehicle& o = observedVehicles[idx];
        o.active = v.active; o.pos = v.pos; o.vel = v.vel;
        o.angle = v.angle; o.angVel = v.angVel; o.width = v.width; o.length = v.length;
        float halfWidth = v.width * 0.5f, halfLength = v.length * 0.5f;
        o.radius = sqrtf(halfWidth * halfWidth + halfLength * halfLength);
        o.rail = AIOnRail(v); o.speed = v.ai.speed; o.pathDistance = v.ai.s; o.shift = v.ai.laneShift;
        o.blend = v.ai.blend; o.blendPos = v.ai.blendPos; o.blendAngle = v.ai.blendAng;
        o.pathCount = 0;
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
    state.nextPlan -= dt; state.commit -= dt;
    if (v.wrecked || v.burning || v.driver != DriverType::Traffic) {
        state.gear = 0; state.reason = RecoveryReason::Disabled;
        v.in = Controls(v, 0, 0);
        timing.frameMs += Milliseconds(start);
        return;
    }
    GatherNearby(g, v, idx);
    if (!nearbyComplete) {
        state.gear = 0; state.reason = RecoveryReason::NoFeasibleManoeuvre;
        v.in = Controls(v, 0, 0);
        timing.frameMs += Milliseconds(start);
        return;
    }
    // A committed move is checked through stopping, not just until the next update.
    // An actor entering its sweep invalidates it immediately, even between plans.
    bool urgent = false;
    if (state.gear != 0) {
        Candidate c; c.gear = state.gear; c.steer = state.steer;
        c.tracking = state.tracking;
        float immediate = std::max(0.3f, fabsf(Dot(v.vel, v.Fwd())) / 160 + dt + 0.12f);
        c = Predict(g, v, state, c, std::min(Tuning().horizon, immediate + Tuning().stopTail), true);
        if (!c.safe) {
            state.gear = 0; state.steer = 0; state.commit = 0; state.nextPlan = 0;
            state.tracking = false;
            state.reason = RecoveryReason::Hazard;
            urgent = true;
        }
    }
    if (state.nextPlan <= 0 && (state.commit <= 0 || state.gear == 0 || urgent)) SelectPlan(g, v, state);
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
        return cause == RejoinCause::Clear;
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
    Predict(g, v, state, forward, horizon, true, &cause);
    return finish(cause);
}

void RecoveryResetStats() { timing = Timing{}; decisionTiming = Timing{}; }

static RecoveryStats ReadTimingStats(Timing& record) {
    FinishTimingFrame(record);
    RecoveryStats stats;
    stats.frames = record.frames; stats.plans = record.plans;
    stats.rejected = record.rejected; stats.holds = record.holds;
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
    TraceLog(LOG_INFO, "RECOVERY CPU: avg %.4f ms p95 %.4f ms worst %.4f ms | frames %d percentile samples %d | plans %d rejected %d holds %d",
             s.averageMs, s.p95Ms, s.worstMs, s.frames, s.percentileSamples, s.plans, s.rejected, s.holds);
    RecoveryStats d = RecoveryGetDecisionStats();
    TraceLog(LOG_INFO, "DRIVER DECISION CPU (includes police and cleanup): avg %.4f ms p95 %.4f ms worst %.4f ms | frames %d percentile samples %d",
             d.averageMs, d.p95Ms, d.worstMs, d.frames, d.percentileSamples);
}
