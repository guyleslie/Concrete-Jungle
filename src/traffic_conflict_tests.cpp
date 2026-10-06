// =====================================================================================
//  CJ-016 cooperative yielding fixture. Production traffic AI and physics run on
//  uniform road; each case freezes a mutual blockage that the old rules left as a
//  permanent stand-off:
//    passing-head-on   a passing car meets an oncoming car beside a parked car;
//    knocked-needs-room a knocked car can only escape backwards into the rail car
//                      stopped behind it;
//    knocked-queue     the same, with a second rail car queued behind (a chain);
//    knocked-pair      the car behind is knocked too and has too little room behind
//                      for a full manoeuvre: only a short creep back frees the other;
//    junction-gridlock four cars in a junction box, each nose against the next car's
//                      side: a wait-for cycle of four that no pair rule resolves.
//  Ten seeds vary the poses, gaps, speeds and classes; every case runs at 60 Hz and
//  20 Hz physics. Drivers must keep their cars; nothing may be moved by the fixture.
// =====================================================================================
#include "traffic_conflict_tests.h"
#include "game.h"
#include "traffic.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace {
constexpr const char* FIXTURE_ID = "cj016-conflict-v2";
constexpr uint32_t SEED = 0x000c0016u;
constexpr float RENDER_DT = 1.0f / 60.0f;
constexpr int SEEDS = 10;
constexpr float MAX_CASE_S = 30.0f;
constexpr float SETTLE_S = 2.0f;           // keep observing after success
constexpr float RAIL_TAIL = 400.0f;        // already-driven path behind each rail car

enum class Kind { HeadOn, KnockedRoom, KnockedQueue, KnockedPair, Gridlock };
const char* KindName(Kind k) {
    return k == Kind::HeadOn ? "passing-head-on" : k == Kind::KnockedRoom ? "knocked-needs-room"
         : k == Kind::KnockedQueue ? "knocked-queue" : k == Kind::KnockedPair ? "knocked-pair" : "junction-gridlock";
}
struct Case { Kind kind; int seed; float step; std::string name; };

std::vector<Case> Schedule() {
    std::vector<Case> cases;
    // CJ_TEST_CASE=<substring> narrows a diagnostic run; evidence runs use all cases.
    const char* only = std::getenv("CJ_TEST_CASE");
    for (float step : { RENDER_DT, 1.0f / 20.0f })
        for (Kind kind : { Kind::HeadOn, Kind::KnockedRoom, Kind::KnockedQueue, Kind::KnockedPair, Kind::Gridlock })
            for (int seed = 0; seed < SEEDS; seed++)
                cases.push_back({ kind, seed, step, std::string(KindName(kind)) + (step > 0.03f ? "-20hz" : "-60hz") +
                                  "-s" + std::to_string(seed) });
    if (only && *only) {
        std::vector<Case> chosen;
        for (const Case& c : cases) if (c.name.find(only) != std::string::npos) chosen.push_back(c);
        if (!chosen.empty()) cases = chosen;
    }
    return cases;
}

int SkinOf(const char* cls) {
    VClass c = FindVehicleClass(cls);
    for (size_t i = 0; i < gAssets.vehicles.size(); i++)
        if (gAssets.vehicles[i].cls == c && gAssets.vehicles[i].spawnable) return (int)i;
    return -1;
}

bool Finite(const Vehicle& v) {
    return std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && std::isfinite(v.angle) &&
           std::isfinite(v.vel.x) && std::isfinite(v.vel.y) && std::isfinite(v.angVel);
}

struct Actor {
    const char* role = "";
    DriverType driver = DriverType::None;
    int driverSkin = 0, skin = 0;
    bool startedRail = false;
};
} // namespace

struct TrafficConflictTests::State {
    std::vector<Case> cases;
    std::vector<Actor> actors;
    std::vector<std::pair<int, int>> pairs;   // (yielder, priority) roles taken
    std::vector<int> roles, lastYield;
    std::vector<Vector2> lastPos;
    int index = 0, frame = 0, phaseFrame = 0, checks = 0, failures = 0, completed = 0, physicsSteps = 0;
    int passedCases = 0, teleports = 0, nonfinite = 0, knocks = 0, ownershipLosses = 0, flips = 0;
    int chainRoles = 0, vehicleContacts = 0;
    bool invalid = false, finished = false, nextCase = false;
    float phaseTime = 0, maxOverlap = 0, maxPen = 0, resolved = -1, secondary = -1, successAt = -1;
    float priorityStart = 0, yielderStart = 0, parkedY = 0;
    std::vector<Vector2> startPos;           // gridlock: each car must drive clear of the box
    std::vector<int> gridlock;
    int yielder = -1, priority = -1, parked = -1, queued = -1, overlapA = -1, overlapB = -1;
    Vector2 centre{};
    std::string capture, lastResult;

    const Case& Current() const { return cases[index]; }
    void Check(const char* name, float value, float low, float high) {
        bool pass = std::isfinite(value) && value >= low && value <= high;
        checks++;
        if (!pass) { failures++; lastResult = Current().name + ": " + name + " FAILED"; }
        TraceLog(LOG_INFO, "CJ016Y metric fixture=%s case=%s name=%s value=%.6f min=%.6f max=%.6f result=%s",
                 FIXTURE_ID, Current().name.c_str(), name, value, low, high, pass ? "PASS" : "FAIL");
    }
    int Add(Game& g, const char* cls, Vector2 pos, float angle, DriverType driver, const char* role) {
        Vehicle v; InitVehicle(v, SkinOf(cls), pos, angle);
        v.driver = driver; v.missionTarget = true;
        if (driver == DriverType::None) v.in.handbrake = true;
        g.vehicles.push_back(v);
        Actor a; a.role = role; a.driver = driver; a.driverSkin = v.driverSkin; a.skin = v.skin;
        actors.push_back(a);
        return (int)g.vehicles.size() - 1;
    }
    void Rail(Game& g, int idx, float speed, float temper, bool insideJunction = false) {
        Vehicle& v = g.vehicles[idx];
        AIStartRail(g, v, RAIL_TAIL, insideJunction);
        v.ai.speed = speed; v.ai.cruise = std::max(speed, 200.0f); v.ai.temper = temper;
        actors[idx].startedRail = true;
    }
    void Wall(Game& g, Rectangle r) {
        Building b; b.r = r; b.height = 3.0f * cfg::M;
        g.map.buildings.push_back(b);
    }
    void Start(Game& g) {
        const Case& c = Current();
        g.vehicles.clear(); g.peds.clear(); g.pickups.clear(); gridlock.clear();
        g.map.ResetTestGround(Tile::Road); g.pedGrid.Build(g.peds);
        g.physics = VehiclePhysics{};
        GRng().s = SEED + (uint32_t)c.seed * 7919u + (uint32_t)c.kind * 104729u;
        Rng rng(SEED ^ ((uint32_t)c.seed * 2654435761u + (uint32_t)c.kind * 40503u));
        actors.clear(); pairs.clear();
        phaseFrame = 0; phaseTime = 0; maxOverlap = maxPen = 0; resolved = secondary = successAt = -1;
        teleports = nonfinite = knocks = ownershipLosses = flips = chainRoles = vehicleContacts = 0;
        yielder = priority = parked = queued = overlapA = overlapB = -1;
        Vector2 ic = g.map.InterCenter(3, 3);
        float north = ic.x + cfg::LANE_OFFSET, south = ic.x - cfg::LANE_OFFSET;
        parkedY = ic.y + 520;
        bool bus = c.seed >= SEEDS / 2;
        if (c.kind == Kind::Gridlock) {
            // Each car's nose stops 'gap' short of the side of the car crossing ahead of it:
            // north waits on west, west on south, south on east, east on north.
            float gap = rng.Range(8.0f, 14.0f);
            const char* roles[4] = { "NORTH", "WEST", "SOUTH", "EAST" };
            const float angles[4] = { 0, -PI * 0.5f, PI, PI * 0.5f };
            for (int k = 0; k < 4; k++) {
                const char* cls = bus && k == 0 ? "Bus" : "Taxi";
                const char* crossed = bus && k == 3 ? "Bus" : "Taxi";     // the car ahead, crossing
                float half = Spec(FindVehicleClass(cls)).length * 0.5f;
                Vehicle probe; InitVehicle(probe, SkinOf(crossed), V2(0, 0), 0);
                // Along the travel direction, the crossing car's near side lies 'edge' past
                // the junction centre; the nose stops 'gap' short of it.
                float edge = cfg::LANE_OFFSET - probe.width * 0.5f;
                Vector2 dir = Forward(angles[k]);
                Vector2 lane = V2(ic.x, ic.y) + RightOf(angles[k]) * cfg::LANE_OFFSET;
                Vector2 pos = lane + dir * (edge - gap - half);
                int idx = Add(g, cls, pos, angles[k], DriverType::Traffic, roles[k]);
                Rail(g, idx, 0, 1.0f, true);
                gridlock.push_back(idx);
            }
            parkedY = ic.y;
            yielder = gridlock[0]; priority = gridlock[1];
        } else if (c.kind == Kind::HeadOn) {
            // The passing Taxi is already in the oncoming lane beside a parked car.
            float shift = -cfg::LANE_OFFSET * 1.9f;
            parked = Add(g, "Taxi", V2(north, parkedY), 0, DriverType::None, "PARKED");
            float y = parkedY + rng.Range(-20.0f, 40.0f);
            yielder = Add(g, "Taxi", V2(north, y), 0, DriverType::Traffic, "PASSING");
            Rail(g, yielder, rng.Range(70.0f, 110.0f), 1.2f);
            Vehicle& x = g.vehicles[yielder];
            x.ai.laneShift = x.ai.laneShiftTarget = shift;
            x.pos = V2(north + shift, y);
            float gap = rng.Range(230.0f, 330.0f);
            priority = Add(g, bus ? "Bus" : "Taxi", V2(south, y - gap), PI, DriverType::Traffic, "ONCOMING");
            Rail(g, priority, rng.Range(150.0f, 210.0f), 1.0f);
            yielderStart = y; priorityStart = y - gap;
        } else {
            // A kerb wall and a parked car in the oncoming lane leave no way round:
            // the knocked Taxi can only reverse, into the space of the car behind.
            float wallX = ic.x + cfg::ROAD_HALF + 6;
            Wall(g, { wallX, parkedY - 400, 24, 900 });
            float angle = rng.Range(0.55f, 0.70f);
            Vehicle probe; InitVehicle(probe, SkinOf("Taxi"), V2(0, 0), angle);
            Vector2 corners[4]; OBBCorners(probe.Box(), corners);
            float maxX = -1e9f, maxY = -1e9f;
            for (Vector2 p : corners) { maxX = std::max(maxX, p.x); maxY = std::max(maxY, p.y); }
            Vector2 kp = V2(wallX - rng.Range(3.0f, 5.0f) - maxX, parkedY);
            priority = Add(g, "Taxi", kp, angle, DriverType::Traffic, "KNOCKED");
            Vehicle& k = g.vehicles[priority];
            k.ai.rail = false; k.ai.dynTimer = 5; k.recoveryTracked = true;
            AIResetPath(k, g.map);
            // Ahead in the oncoming lane: it blocks passing, not the knocked car's reversing.
            parked = Add(g, "Taxi", V2(south, parkedY - 130), PI, DriverType::None, "PARKED");
            const char* cls = bus ? "Bus" : "Taxi";
            float length = Spec(FindVehicleClass(cls)).length;
            float ry = kp.y + maxY + rng.Range(6.0f, 10.0f) + length * 0.5f;
            if (c.kind == Kind::KnockedPair) {
                // The car behind was knocked too: turned towards the kerb, it cannot simply
                // queue, and a wall close behind leaves less room than a full reversing
                // manoeuvre needs; only a short creep back gives the front car room.
                float tilt = rng.Range(0.12f, 0.22f);
                Vehicle shape; InitVehicle(shape, SkinOf(cls), V2(0, 0), tilt);
                // The front corner nearest the kerb stays 4-8 px clear of the wall.
                float reach = cosf(tilt) * shape.width * 0.5f + sinf(tilt) * length * 0.5f;
                float behindX = std::min(north - rng.Range(2.0f, 6.0f), wallX - rng.Range(4.0f, 8.0f) - reach);
                yielder = Add(g, cls, V2(behindX, ry + 6), tilt, DriverType::Traffic, "BEHIND");
                Vehicle& b = g.vehicles[yielder];
                b.ai.rail = false; b.ai.dynTimer = 5; b.recoveryTracked = true;
                AIResetPath(b, g.map);
                float rear = ry + 6 + length * 0.5f + rng.Range(70.0f, 85.0f);
                Wall(g, { north - 60, rear, 120, 24 });
            } else {
                yielder = Add(g, cls, V2(north, ry), 0, DriverType::Traffic, "BEHIND");
                Rail(g, yielder, 0, 1.0f);
            }
            yielderStart = ry;
            if (c.kind == Kind::KnockedQueue) {
                float qy = ry + length * 0.5f + rng.Range(10.0f, 16.0f) + Spec(FindVehicleClass("Taxi")).length * 0.5f;
                queued = Add(g, "Taxi", V2(north, qy), 0, DriverType::Traffic, "QUEUED");
                Rail(g, queued, 0, 1.0f);
            }
        }
        g.map.RebuildTestIndex();
        roles.assign(g.vehicles.size(), 0); lastYield.assign(g.vehicles.size(), -1);
        lastPos.clear(); startPos.clear();
        for (const Vehicle& v : g.vehicles) { lastPos.push_back(v.pos); startPos.push_back(v.pos); }
        centre = V2(ic.x, parkedY);
        g.player.inVehicle = false; g.player.vehicle = -1;
        g.player.pos = centre + V2(-3000, 0); g.player.vel = {};
        g.cam.Snap(centre, 900); g.heat = 0; g.time = 0;
        TraceLog(LOG_INFO, "CJ016Y begin fixture=%s case=%s kind=%s seed_index=%d physics_dt=%.8f vehicles=%d yielder=%d %s priority=%d %s",
                 FIXTURE_ID, c.name.c_str(), KindName(c.kind), c.seed, c.step, (int)g.vehicles.size(),
                 yielder, g.vehicles[yielder].S().name.c_str(), priority, g.vehicles[priority].S().name.c_str());
    }
    void Observe(Game& g, float step) {
        phaseTime += step;
        for (size_t i = 0; i < g.vehicles.size(); i++) {
            const Vehicle& v = g.vehicles[i];
            const Actor& a = actors[i];
            if (!v.active || v.driver != a.driver || v.driverSkin != a.driverSkin || v.skin != a.skin) ownershipLosses++;
            if (!Finite(v)) { nonfinite++; invalid = true; return; }
            float travel = v.Speed() * step;
            if (Dist(lastPos[i], v.pos) > std::max(8.0f, travel + 4.0f)) teleports++;
            lastPos[i] = v.pos;
            if (a.startedRail && !v.ai.rail) knocks++;
            int to = v.ai.yieldTo;
            if (to >= 0 && lastYield[i] != to) {
                roles[i]++;
                if (v.ai.yieldDepth > 0) chainRoles++;
                for (auto& p : pairs) if (p.first == to && p.second == (int)i) flips++;
                pairs.push_back({ (int)i, to });
                TraceLog(LOG_INFO, "CJ016Y role case=%s time_s=%.3f yielder=%d %s priority=%d depth=%d retreat_px=%.1f",
                         Current().name.c_str(), phaseTime, (int)i, a.role, to, v.ai.yieldDepth, v.ai.retreatLeft);
            }
            lastYield[i] = to;
            Vector2 n; float depth;
            for (const Building& b : g.map.buildings)
                if (OBBOverlap(v.Box(), MakeAABB(b.r), n, depth)) maxPen = std::max(maxPen, depth);
            for (size_t j = i + 1; j < g.vehicles.size(); j++)
                if (OBBOverlap(v.Box(), g.vehicles[j].Box(), n, depth) && depth > maxOverlap) {
                    maxOverlap = depth; overlapA = (int)i; overlapB = (int)j;
                }
        }
        for (const ImpactEvent& e : g.physics.events) if (e.kind == ContactKind::Vehicle) vehicleContacts++;
        if ((int)(phaseTime / 0.5f) != (int)((phaseTime - step) / 0.5f))
            for (size_t i = 0; i < g.vehicles.size(); i++) {
                const Vehicle& v = g.vehicles[i];
                TraceLog(LOG_INFO, "CJ016Y state case=%s t=%.2f #%d %s x=%.1f y=%.1f ang=%.3f spd=%.1f rail=%d reason=%d s=%.1f shift=%.1f wait=%d yield=%d retreat=%.1f rec_gear=%d rec_reason=%s blocked_by=%d",
                         Current().name.c_str(), phaseTime, (int)i, actors[i].role, v.pos.x, v.pos.y, v.angle, v.Speed(), (int)v.ai.rail, v.ai.reason,
                         v.ai.s, v.ai.laneShift, v.ai.waitingOn, v.ai.yieldTo, v.ai.retreatLeft, v.ai.recovery.gear,
                         RecoveryReasonText(v.ai.recovery.reason), v.ai.recovery.blockedBy);
            }
        const Vehicle& y = g.vehicles[yielder];
        const Vehicle& p = g.vehicles[priority];
        if (c_kind() == Kind::Gridlock) {
            // Resolved: every car has driven clear of the box (two car lengths on).
            bool all = true;
            for (int k : gridlock) {
                const Vehicle& v = g.vehicles[k];
                if (Dot(v.pos - startPos[k], v.Fwd()) < v.length * 2) all = false;
            }
            int moved = 0;
            for (int k : gridlock) if (Dot(g.vehicles[k].pos - startPos[k], g.vehicles[k].Fwd()) > 40) moved++;
            if (resolved < 0 && moved >= 1 && !g.vehicles[yielder].ai.rail) resolved = -1;
            if (resolved < 0 && moved >= 2) resolved = phaseTime;
            if (secondary < 0 && all) secondary = phaseTime;
            if (successAt < 0 && resolved >= 0 && secondary >= 0) successAt = phaseTime;
        } else if (c_kind() == Kind::KnockedPair) {
            // Resolved: the knocked car in front is back on its lane; then the car behind too.
            if (resolved < 0 && p.ai.rail) resolved = phaseTime;
            if (secondary < 0 && resolved >= 0 && y.ai.rail) secondary = phaseTime;
            if (successAt < 0 && resolved >= 0 && secondary >= 0) successAt = phaseTime;
        } else if (c_kind() == Kind::HeadOn) {
            // The oncoming car has passed the passing car's starting point...
            if (resolved < 0 && p.pos.y > yielderStart + p.length * 0.5f) resolved = phaseTime;
            // ...and the passing car has then passed the parked car, back in its lane.
            if (secondary < 0 && resolved >= 0 && y.pos.y < parkedY - g.vehicles[parked].length - 10 && fabsf(y.ai.laneShift) < 6)
                secondary = phaseTime;
            if (successAt < 0 && resolved >= 0 && secondary >= 0) successAt = phaseTime;
        } else {
            if (resolved < 0 && p.ai.rail) resolved = phaseTime;
            // After the knocked car rejoined, the car behind drives on again.
            if (secondary < 0 && resolved >= 0 && y.ai.yieldTo < 0 && y.ai.rail && y.pos.y < yielderStart - 40) secondary = phaseTime;
            if (successAt < 0 && resolved >= 0 && secondary >= 0) successAt = phaseTime;
        }
    }
    Kind c_kind() const { return Current().kind; }
    void Finish(const Game& g) {
        int before = failures;
        const Case& c = Current();
        Check("ownership_losses", (float)ownershipLosses, 0, 0);
        Check("nonfinite", (float)nonfinite, 0, 0);
        Check("teleports", (float)teleports, 0, 0);
        Check("static_penetration_px", maxPen, 0, 3);
        Check("vehicle_overlap_px", maxOverlap, 0, 3);
        Check("rail_cars_knocked", (float)knocks, 0, 0);
        Check("role_flips", (float)flips, 0, 0);
        int totalRoles = 0;
        for (int r : roles) totalRoles += r;
        if (c.kind == Kind::Gridlock || c.kind == Kind::KnockedPair) {
            // Somebody has to give way; at most one driver per car in the loop.
            Check("roles", (float)totalRoles, 1, c.kind == Kind::Gridlock ? 4.0f : 2.0f);
        } else {
            // A knocked car may still find its own way out in some seeded poses; it
            // must then not provoke a role. Head-on passing always needs one.
            Check("yielder_roles", (float)roles[yielder], c.kind == Kind::HeadOn ? 1.0f : 0.0f, 1);
            Check("priority_roles", (float)roles[priority], 0, 0);
        }
        Check("resolved_s", resolved, 0, c.kind == Kind::HeadOn ? 20.0f : MAX_CASE_S);
        Check("completed_s", secondary, 0, MAX_CASE_S);
        // The queued car stands too close behind: a role always needs the chain.
        if (c.kind == Kind::KnockedQueue && roles[yielder] > 0) Check("chain_roles", (float)chainRoles, 1, 3);
        bool pass = failures == before;
        if (pass) passedCases++;
        TraceLog(LOG_INFO, "CJ016Y result fixture=%s case=%s duration_s=%.3f resolved_s=%.3f completed_s=%.3f yielder_roles=%d all_roles=%d chain_roles=%d flips=%d vehicle_overlap_px=%.3f overlap_pair=%d,%d static_pen_px=%.3f contacts=%d knocks=%d teleports=%d result=%s",
                 FIXTURE_ID, c.name.c_str(), phaseTime, resolved, secondary, roles[yielder], totalRoles, chainRoles, flips,
                 maxOverlap, overlapA, overlapB, maxPen, vehicleContacts, knocks, teleports, pass ? "PASS" : "FAIL");
        completed++;
        capture = c.name;
        (void)g;
    }
};

TrafficConflictTests::TrafficConflictTests() = default;
TrafficConflictTests::~TrafficConflictTests() = default;

bool TrafficConflictTests::Init(Game& g) {
    state = std::make_unique<State>();
    State& s = *state;
    s.cases = Schedule();
    if (SkinOf("Taxi") < 0 || SkinOf("Bus") < 0) { s.invalid = true; TraceLog(LOG_ERROR, "CJ016Y missing Taxi/Bus sprite"); return false; }
    const char* revision = std::getenv("CJ_TEST_REVISION");
    TraceLog(LOG_INFO, "CJ016Y run fixture=%s scenario=traffic-conflict revision=%s seed=%08x planned_cases=%d render_dt=%.8f max_case_s=%.1f build=%s_%s",
             FIXTURE_ID, revision ? revision : "UNRECORDED", SEED, (int)s.cases.size(), RENDER_DT, MAX_CASE_S, __DATE__, __TIME__);
    for (size_t i = 0; i < s.cases.size(); i++)
        TraceLog(LOG_INFO, "CJ016Y scheduled index=%d case=%s physics_dt=%.8f", (int)i + 1, s.cases[i].name.c_str(), s.cases[i].step);
    s.Start(g);
    return true;
}

void TrafficConflictTests::Update(Game& g, float dt) {
    if (!state || state->invalid || state->finished) return;
    State& s = *state;
    if (!std::isfinite(dt) || fabsf(dt - RENDER_DT) > 1e-6f) {
        s.invalid = true; TraceLog(LOG_ERROR, "CJ016Y requires a fixed 1/60 s render clock"); return;
    }
    if (s.nextCase) { s.index++; s.Start(g); s.nextCase = false; }
    const Case& c = s.Current();
    s.frame++;
    int divisor = c.step > 0.03f ? 3 : 1;
    if ((s.phaseFrame + 1) % divisor == 0) {
        g.time += c.step;
        // In the junction the lights run as in the city: a car that backed out behind
        // the stop line waits for green. Elsewhere the fixed phase keeps v1 unchanged.
        if (c.kind == Kind::Gridlock) g.map.time += c.step;
        AIObserveTraffic(g);
        AIResolveWaitCycles(g, c.step);
        for (size_t i = 0; i < g.vehicles.size(); i++) {
            Vehicle& v = g.vehicles[i];
            v.kinFrom = v.pos; v.kinFromAng = v.angle;
            if (v.active && v.driver == DriverType::Traffic && v.Drivable()) AIUpdateTraffic(g, (int)i, c.step);
        }
        PhysicsStepOptions options; options.disableWorldEdges = true;
        g.physics.Step(g, c.step, options);
        s.physicsSteps++;
        s.Observe(g, c.step);
    }
    s.phaseFrame++;
    bool done = s.phaseTime >= MAX_CASE_S - 1e-4f || (s.successAt >= 0 && s.phaseTime >= s.successAt + SETTLE_S);
    if (done && !s.invalid) {
        s.Finish(g);
        if (s.index == (int)s.cases.size() - 1) s.finished = true;
        else s.nextCase = true;
    }
}

void TrafficConflictTests::Draw(const Game& g) const {
    if (!state) return;
    const State& s = *state;
    ClearBackground({ 20, 25, 30, 255 });
    if (s.invalid || g.vehicles.empty()) { DrawUIText("CJ-016 conflict fixture invalid. See the log.", 30, 60, 26, RED); return; }
    Camera2D camera{}; camera.target = s.centre;
    camera.offset = V2(GetScreenWidth() * 0.5f, GetScreenHeight() * 0.56f); camera.zoom = 1.0f;
    BeginMode2D(camera);
    float tile = 16 * cfg::M;
    Texture2D ground = gAssets.asphalt;
    for (int y = -4; y <= 4; y++) for (int x = -5; x <= 5; x++)
        DrawTexturePro(ground, { 0, 0, (float)ground.width, (float)ground.height },
                       { s.centre.x + x * tile, s.centre.y + y * tile, tile, tile }, {}, 0, { 165, 175, 185, 255 });
    for (int i = -6; i <= 6; i++) {
        DrawLineV(s.centre + V2(i * 10 * cfg::M, -700), s.centre + V2(i * 10 * cfg::M, 700), { 140, 160, 170, 45 });
        DrawLineV(s.centre + V2(-900, i * 10 * cfg::M), s.centre + V2(900, i * 10 * cfg::M), { 140, 160, 170, 45 });
    }
    // Lane centres: northbound (right) and southbound (left), and the road centre line.
    DrawLineEx(V2(s.centre.x + cfg::LANE_OFFSET, s.centre.y - 700), V2(s.centre.x + cfg::LANE_OFFSET, s.centre.y + 700), 2, { 215, 230, 140, 120 });
    DrawLineEx(V2(s.centre.x - cfg::LANE_OFFSET, s.centre.y - 700), V2(s.centre.x - cfg::LANE_OFFSET, s.centre.y + 700), 2, { 215, 230, 140, 120 });
    for (float y = -700; y < 700; y += 40) DrawLineEx(V2(s.centre.x, s.centre.y + y), V2(s.centre.x, s.centre.y + y + 22), 3, { 235, 235, 225, 200 });
    for (const Building& b : g.map.buildings) {
        DrawRectangleRec(b.r, { 80, 91, 104, 255 });
        DrawRectangleLinesEx(b.r, 2, { 180, 195, 205, 255 });
    }
    for (size_t i = 0; i < g.vehicles.size(); i++) {
        const Vehicle& v = g.vehicles[i];
        const VehicleSprite& sprite = gAssets.vehicles[v.skin];
        DrawTexturePro(sprite.tex, sprite.src, { v.pos.x + 4, v.pos.y + 5, v.width, v.length },
                       V2(v.width * 0.5f, v.length * 0.5f), v.angle * RAD2DEG, { 0, 0, 0, 120 });
        DrawTexturePro(sprite.tex, sprite.src, { v.pos.x, v.pos.y, v.width, v.length },
                       V2(v.width * 0.5f, v.length * 0.5f), v.angle * RAD2DEG, WHITE);
        Color c = v.ai.yieldTo >= 0 ? Color{ 255, 200, 70, 255 } : (int)i == s.priority ? Color{ 110, 220, 160, 255 } : Color{ 170, 180, 190, 255 };
        Vector2 corners[4]; OBBCorners(v.Box(), corners);
        for (int k = 0; k < 4; k++) DrawLineEx(corners[k], corners[(k + 1) % 4], 1.5f, c);
        const char* label = v.ai.yieldTo >= 0 && !v.ai.rail ? "GIVING WAY: MAKING ROOM"
            : v.ai.yieldTo >= 0 ? (v.ai.retreatLeft > 0 ? "GIVING WAY: REVERSING" : "GIVING WAY: HOLDING")
            : v.driver == DriverType::Traffic && !v.ai.rail ? "KNOCKED: RECOVERING" : s.actors[i].role;
        DrawText(label, (int)(v.pos.x + v.width * 0.5f + 10), (int)v.pos.y - 8, 16, c);
    }
    EndMode2D();
    DrawRectangle(0, 0, GetScreenWidth(), 140, { 12, 17, 23, 240 });
    DrawUIText("CONCRETE JUNGLE / CJ-016 COOPERATIVE YIELDING", 25, 14, 25, { 215, 231, 241, 255 }, true);
    DrawUIText(TextFormat("%s | case %d / %d | %.2f s | %.0f Hz physics", s.Current().name.c_str(), s.index + 1,
                         (int)s.cases.size(), s.phaseTime, 1.0f / s.Current().step), 25, 50, 22, WHITE);
    DrawUIText(TextFormat("%s | checks %d / failures %d | passed cases %d / %d | resolved %.2f s | completed %.2f s",
                         s.finished ? "COMPLETE" : "MEASURING", s.checks, s.failures, s.passedCases, s.completed,
                         s.resolved, s.secondary), 25, 86, 19,
               s.failures ? Color{ 255, 151, 115, 255 } : Color{ 116, 217, 165, 255 });
    DrawRectangle(0, GetScreenHeight() - 38, GetScreenWidth(), 38, { 12, 17, 23, 235 });
    DrawUIText(s.lastResult.empty() ? "Yellow: the driver giving way retraces its own path. Green: the driver with priority. 10 m grid, real sprites."
                                  : s.lastResult.c_str(), 25, GetScreenHeight() - 29, 18, LIGHTGRAY);
}

void TrafficConflictTests::Log() const {
    if (!state) return;
    const State& s = *state;
    TraceLog(LOG_INFO, "CJ016Y summary fixture=%s seed=%08x scheduled=%d completed=%d passed_cases=%d render_frames=%d physics_steps=%d checks=%d failures=%d incomplete=%d invalid=%d result=%s",
             FIXTURE_ID, SEED, (int)s.cases.size(), s.completed, s.passedCases, s.frame, s.physicsSteps, s.checks, s.failures,
             (int)!s.finished, (int)s.invalid, s.finished && !s.invalid && s.failures == 0 ? "PASS" : "FAIL");
}
bool TrafficConflictTests::Failed() const { return state && (state->invalid || state->failures > 0 || !state->finished); }
bool TrafficConflictTests::Finished() const { return state && state->finished; }
const char* TrafficConflictTests::CaptureLabel() const { return state && !state->capture.empty() ? state->capture.c_str() : nullptr; }
void TrafficConflictTests::ClearCaptureRequest() { if (state) state->capture.clear(); }
