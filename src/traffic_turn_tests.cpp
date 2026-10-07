// =====================================================================================
//  CJ-020 turning fixture. Production traffic AI drives one rail car of every traffic
//  class through an empty junction on uniform road: right, left and straight on, at
//  60 Hz and 20 Hz. The lights stay green. Measured against the junction's geometry
//  (kerbs at the road edges, the right-hand half of each road):
//    - rear-axle slip: the angle between the body and the motion of its rear axle point;
//    - rear-axle path radius (circumradius of rear points 8 px apart) and the lateral
//      acceleration there;
//    - body over the kerb: the deepest overlap with the four blocks round the junction;
//    - encroachment: how far a body corner reaches outside the junction box into the
//      oncoming half of the road, or into an arm the car does not use.
// =====================================================================================
#include "traffic_turn_tests.h"
#include "game.h"
#include "traffic.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace {
constexpr const char* FIXTURE_ID = "cj020-turns-v1";
constexpr uint32_t SEED = 0x000c0020u;
constexpr float RENDER_DT = 1.0f / 60.0f;
constexpr float MAX_CASE_S = 30.0f;
constexpr float START_GAP = 420.0f;        // px from the box edge to the car's front at the start
constexpr float EXIT_GAP = 320.0f;         // px past the box edge the rear axle must reach
constexpr float RAIL_TAIL = 300.0f;
constexpr float SAMPLE_SPACING = 8.0f;     // px between rear-axle samples for the radius

enum class Turn { Right, Left, Straight };
const char* TurnName(Turn t) { return t == Turn::Right ? "right" : t == Turn::Left ? "left" : "straight"; }
int TurnCode(Turn t) { return t == Turn::Right ? 1 : t == Turn::Left ? 2 : 0; }

struct Case { std::string cls; Turn turn; float step; std::string name; };

int SkinOf(const std::string& cls) {
    VClass c = FindVehicleClass(cls);
    for (size_t i = 0; i < gAssets.vehicles.size(); i++)
        if (gAssets.vehicles[i].cls == c && gAssets.vehicles[i].spawnable) return (int)i;
    return -1;
}

std::vector<Case> Schedule() {
    std::vector<Case> cases;
    std::vector<std::string> classes;
    for (const VehicleSpec& s : VehicleClasses())
        if (s.trafficWeight > 0 && !s.police() && SkinOf(s.name) >= 0) classes.push_back(s.name);
    const char* only = std::getenv("CJ_TEST_CASE");
    for (float step : { RENDER_DT, 1.0f / 20.0f })
        for (const std::string& cls : classes)
            for (Turn t : { Turn::Right, Turn::Left, Turn::Straight })
                cases.push_back({ cls, t, step, cls + "-" + TurnName(t) + (step > 0.03f ? "-20hz" : "-60hz") });
    if (only && *only) {
        std::vector<Case> chosen;
        for (const Case& c : cases) if (c.name.find(only) != std::string::npos) chosen.push_back(c);
        if (!chosen.empty()) cases = chosen;
    }
    return cases;
}

bool Finite(const Vehicle& v) {
    return std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && std::isfinite(v.angle) &&
           std::isfinite(v.vel.x) && std::isfinite(v.vel.y);
}
} // namespace

struct TrafficTurnTests::State {
    std::vector<Case> cases;
    int index = 0, frame = 0, phaseFrame = 0, checks = 0, failures = 0, completed = 0, passedCases = 0, physicsSteps = 0;
    bool invalid = false, finished = false, nextCase = false, reached = false;
    float phaseTime = 0;
    Vector2 ic{};
    int car = -1, driverSkin = 0, skin = 0;
    // measurements
    Vector2 lastRear{ NAN, NAN };
    float lastAngle = 0;
    float turnSlip = 0, straightSlip = 0, turnSeconds = 0, slipSeconds = 0;
    std::vector<Vector2> samples;            // rear-axle points SAMPLE_SPACING apart
    std::vector<float> sampleTimes;
    float minRadius = 1e9f, maxLateral = 0, minTurnSpeed = 1e9f;
    float kerb = 0, encroach = 0, boxTime = 0, stoppedTime = 0;
    int teleports = 0, nonfinite = 0, knocks = 0, ownership = 0;
    Vector2 lastPos{};
    float finalLateral = NAN, finalHeading = NAN, reachedAt = -1;
    std::vector<Vector2> rearTrace, frontTrace;
    std::vector<OBB> ghosts;
    float ghostClock = 0;
    std::string capture, lastResult;
    float classRadius = 0;

    const Case& Current() const { return cases[index]; }
    void Check(const char* name, float value, float low, float high) {
        bool pass = std::isfinite(value) && value >= low && value <= high;
        checks++;
        if (!pass) { failures++; lastResult = Current().name + ": " + name + " FAILED"; }
        TraceLog(LOG_INFO, "CJ020T metric fixture=%s case=%s name=%s value=%.6f min=%.6f max=%.6f result=%s",
                 FIXTURE_ID, Current().name.c_str(), name, value, low, high, pass ? "PASS" : "FAIL");
    }
    // Local frame: junction centre at the origin, the car approaches northwards (-y) in
    // the lane at x = +LANE_OFFSET.
    Vector2 Local(Vector2 p) const { return p - ic; }

    void Start(Game& g) {
        const Case& c = Current();
        g.vehicles.clear(); g.peds.clear(); g.pickups.clear();
        g.map.ResetTestGround(Tile::Road); g.pedGrid.Build(g.peds);
        g.physics = VehiclePhysics{};
        GRng().s = SEED + (uint32_t)index * 7919u;
        ic = g.map.InterCenter(3, 3);
        // The approach stays green: the map clock does not run in the fixture.
        for (float t = 0; t < 16; t += 0.25f) {
            g.map.time = t;
            if (g.map.SignalState(3, 3, 0) == SIG_GREEN && g.map.GreenTimeLeft(3, 3, 0) >= 3) break;
        }
        Vehicle v; InitVehicle(v, SkinOf(c.cls), V2(0, 0), 0);
        float y = ic.y + cfg::ROAD_HALF + START_GAP + v.length * 0.5f;
        v.pos = V2(ic.x + cfg::LANE_OFFSET, y);
        v.driver = DriverType::Traffic; v.missionTarget = true;
        g.vehicles.push_back(v);
        car = 0;
        Vehicle& r = g.vehicles[car];
        AIStartRail(g, r, RAIL_TAIL, false, TurnCode(c.turn));
        r.ai.cruise = r.S().large() ? 185.0f : 235.0f;
        r.ai.speed = r.ai.cruise; r.ai.temper = 1.0f;
        driverSkin = r.driverSkin; skin = r.skin;
        classRadius = RailTurnMinRadius(r);
        phaseFrame = 0; phaseTime = 0; reached = false;
        lastRear = V2(NAN, NAN); lastAngle = r.angle;
        turnSlip = straightSlip = turnSeconds = slipSeconds = 0;
        samples.clear(); sampleTimes.clear();
        minRadius = 1e9f; maxLateral = 0; minTurnSpeed = 1e9f;
        kerb = encroach = boxTime = stoppedTime = 0;
        teleports = nonfinite = knocks = ownership = 0;
        lastPos = r.pos; finalLateral = finalHeading = NAN; reachedAt = -1;
        rearTrace.clear(); frontTrace.clear(); ghosts.clear(); ghostClock = 0;
        g.player.inVehicle = false; g.player.vehicle = -1;
        g.player.pos = ic + V2(-3000, 0); g.player.vel = {};
        g.cam.Snap(ic, 900); g.heat = 0; g.time = 0;
        TraceLog(LOG_INFO, "CJ020T begin fixture=%s case=%s class=%s turn=%s physics_dt=%.8f length_px=%.1f width_px=%.1f turn_circle_m=%.2f min_rear_radius_px=%.2f",
                 FIXTURE_ID, c.name.c_str(), c.cls.c_str(), TurnName(c.turn), c.step, r.length, r.width,
                 r.S().turnCircle / cfg::M, classRadius);
    }
    // How far a body corner reaches outside the box into the wrong half of a road arm.
    float Encroachment(Vector2 c, Turn turn) const {
        const float H = cfg::ROAD_HALF;
        if (fabsf(c.x) <= H && fabsf(c.y) <= H) return 0;     // in the junction box
        if (fabsf(c.x) > H && fabsf(c.y) > H) return 0;       // over the kerb: measured separately
        if (c.y > H) return std::max(0.0f, -c.x);                                   // approach arm
        if (c.y < -H) return turn == Turn::Straight ? std::max(0.0f, -c.x) : -c.y - H;
        if (c.x > H) return turn == Turn::Right ? std::max(0.0f, -c.y) : c.x - H;
        return turn == Turn::Left ? std::max(0.0f, c.y) : -c.x - H;
    }

    void Observe(Game& g, float step) {
        phaseTime += step;
        const Vehicle& v = g.vehicles[car];
        const Case& c = Current();
        if (!v.active || v.driver != DriverType::Traffic || v.driverSkin != driverSkin || v.skin != skin) ownership++;
        if (!Finite(v)) { nonfinite++; invalid = true; return; }
        if (Dist(lastPos, v.pos) > std::max(8.0f, v.Speed() * step + 4.0f)) teleports++;
        lastPos = v.pos;
        if (!v.ai.rail) knocks++;
        if (v.ai.speed < 1) stoppedTime += step;
        // Slip of the rear axle point, split into turning and straight driving.
        Vector2 rear = v.pos - v.Fwd() * (v.length * 0.32f);
        bool turning = fabsf(WrapAngle(v.angle - lastAngle)) > 0.0005f;
        if (std::isfinite(lastRear.x)) {
            Vector2 r2;
            float slip = RailRearSlip(v, lastRear, step, &r2);
            if (slip >= 0) {
                if (turning) { turnSlip = std::max(turnSlip, slip); turnSeconds += step; if (slip > 5) slipSeconds += step; }
                else straightSlip = std::max(straightSlip, slip);
            }
        }
        lastRear = rear; lastAngle = v.angle;
        // Rear-axle path radius and lateral acceleration from points SAMPLE_SPACING apart.
        if (samples.empty() || Dist(samples.back(), rear) >= SAMPLE_SPACING) {
            samples.push_back(rear); sampleTimes.push_back(phaseTime);
            size_t n = samples.size();
            if (n >= 3) {
                Vector2 a = samples[n - 3], b = samples[n - 2], d = samples[n - 1];
                float ab = Dist(a, b), bd = Dist(b, d), ad = Dist(a, d);
                float area2 = fabsf(Cross(b - a, d - a));
                if (area2 > 1e-3f) {
                    float radius = ab * bd * ad / (2 * area2);
                    if (radius < 5000) {
                        minRadius = std::min(minRadius, radius);
                        float speed = (ab + bd) / std::max(1e-4f, sampleTimes[n - 1] - sampleTimes[n - 3]);
                        maxLateral = std::max(maxLateral, speed * speed / radius);
                        minTurnSpeed = std::min(minTurnSpeed, speed);
                    }
                }
            }
        }
        // Body against the junction's geometry.
        const float H = cfg::ROAD_HALF, far = 4000;
        OBB box = v.Box();
        Rectangle blocks[4] = { { ic.x + H, ic.y + H, far, far }, { ic.x - H - far, ic.y + H, far, far },
                                { ic.x + H, ic.y - H - far, far, far }, { ic.x - H - far, ic.y - H - far, far, far } };
        Vector2 n; float depth;
        for (const Rectangle& b : blocks) if (OBBOverlap(box, MakeAABB(b), n, depth)) kerb = std::max(kerb, depth);
        Vector2 corners[4]; OBBCorners(box, corners);
        for (Vector2 k : corners) encroach = std::max(encroach, Encroachment(Local(k), c.turn));
        Vector2 lp = Local(v.pos);
        if (fabsf(lp.x) < H && fabsf(lp.y) < H) boxTime += step;
        // Traces for the capture.
        Vector2 front = v.pos + v.Fwd() * (v.length * 0.32f);
        if (rearTrace.empty() || Dist(rearTrace.back(), rear) > 2) { rearTrace.push_back(rear); frontTrace.push_back(front); }
        ghostClock -= step;
        if (ghostClock <= 0 && Local(v.pos).y < H + 140 && !reached) { ghosts.push_back(box); ghostClock = 0.2f; }
        // Done: the rear axle is EXIT_GAP past the box edge on the exit arm.
        Vector2 lr = Local(rear);
        bool past = c.turn == Turn::Right ? lr.x > H + EXIT_GAP : c.turn == Turn::Left ? lr.x < -H - EXIT_GAP : lr.y < -H - EXIT_GAP;
        if (past && !reached) {
            reached = true; reachedAt = phaseTime;
            float lane = cfg::LANE_OFFSET;
            float exitAngle = c.turn == Turn::Right ? PI * 0.5f : c.turn == Turn::Left ? -PI * 0.5f : 0.0f;
            finalLateral = c.turn == Turn::Right ? fabsf(lp.y - lane) : c.turn == Turn::Left ? fabsf(lp.y + lane) : fabsf(lp.x - lane);
            finalHeading = fabsf(WrapAngle(v.angle - exitAngle)) * RAD2DEG;
        }
    }

    void Finish(const Game& g) {
        int before = failures;
        const Case& c = Current();
        const Vehicle& v = g.vehicles[car];
        bool large = v.S().large();
        bool turn = c.turn != Turn::Straight;
        float lateralLimit = RailTurnLateralAccel();
        Check("ownership_losses", (float)ownership, 0, 0);
        Check("nonfinite", (float)nonfinite, 0, 0);
        Check("teleports", (float)teleports, 0, 0);
        Check("knocked", (float)knocks, 0, 0);
        Check("reached_exit_s", reachedAt, 0, MAX_CASE_S);
        Check("turning_slip_deg", turnSlip, 0, 3);
        Check("straight_slip_deg", straightSlip, 0, 1);
        if (turn) {
            Check("rear_radius_over_class_min", minRadius / classRadius, 0.98f, 1e6f);
            Check("lateral_accel_over_limit", maxLateral / lateralLimit, 0, 1.05f);
        }
        Check("kerb_px", kerb, 0, 4);
        Check("encroachment_px", encroach, 0, large && c.turn == Turn::Right ? 24.0f : 0.5f);
        Check("final_lateral_px", finalLateral, 0, 1);
        Check("final_heading_deg", finalHeading, 0, 0.5f);
        bool pass = failures == before;
        if (pass) passedCases++;
        // Two lines: raylib truncates a log message at 256 characters.
        TraceLog(LOG_INFO, "CJ020T measure case=%s slip_deg=%.3f slip_over_5=%.4f straight_slip_deg=%.3f rear_radius_m=%.3f class_min_m=%.3f lateral_ms2=%.3f min_speed_kmh=%.2f",
                 c.name.c_str(), turnSlip, turnSeconds > 0 ? slipSeconds / turnSeconds : 0.0f, straightSlip,
                 minRadius < 1e8f ? minRadius / cfg::M : -1.0f, classRadius / cfg::M, maxLateral / cfg::M,
                 minTurnSpeed < 1e8f ? minTurnSpeed / cfg::M * 3.6f : -1.0f);
        TraceLog(LOG_INFO, "CJ020T result fixture=%s case=%s duration_s=%.3f reached_s=%.3f kerb_px=%.2f encroachment_px=%.2f box_s=%.3f stopped_s=%.3f final_lateral_px=%.3f final_heading_deg=%.3f result=%s",
                 FIXTURE_ID, c.name.c_str(), phaseTime, reachedAt, kerb, encroach, boxTime, stoppedTime,
                 finalLateral, finalHeading, pass ? "PASS" : "FAIL");
        completed++;
        capture = c.name;
    }
};

TrafficTurnTests::TrafficTurnTests() = default;
TrafficTurnTests::~TrafficTurnTests() = default;

bool TrafficTurnTests::Init(Game& g) {
    state = std::make_unique<State>();
    State& s = *state;
    s.cases = Schedule();
    if (s.cases.empty()) { s.invalid = true; TraceLog(LOG_ERROR, "CJ020T no traffic classes with sprites"); return false; }
    const char* revision = std::getenv("CJ_TEST_REVISION");
    TraceLog(LOG_INFO, "CJ020T run fixture=%s scenario=traffic-turns revision=%s seed=%08x planned_cases=%d render_dt=%.8f max_case_s=%.1f lateral_accel_ms2=%.3f build=%s_%s",
             FIXTURE_ID, revision ? revision : "UNRECORDED", SEED, (int)s.cases.size(), RENDER_DT, MAX_CASE_S,
             RailTurnLateralAccel() / cfg::M, __DATE__, __TIME__);
    for (size_t i = 0; i < s.cases.size(); i++)
        TraceLog(LOG_INFO, "CJ020T scheduled index=%d case=%s physics_dt=%.8f", (int)i + 1, s.cases[i].name.c_str(), s.cases[i].step);
    s.Start(g);
    return true;
}

void TrafficTurnTests::Update(Game& g, float dt) {
    if (!state || state->invalid || state->finished) return;
    State& s = *state;
    if (!std::isfinite(dt) || fabsf(dt - RENDER_DT) > 1e-6f) {
        s.invalid = true; TraceLog(LOG_ERROR, "CJ020T requires a fixed 1/60 s render clock"); return;
    }
    if (s.nextCase) { s.index++; s.Start(g); s.nextCase = false; }
    const Case& c = s.Current();
    s.frame++;
    int divisor = c.step > 0.03f ? 3 : 1;
    if ((s.phaseFrame + 1) % divisor == 0) {
        g.time += c.step;
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
    bool done = s.phaseTime >= MAX_CASE_S - 1e-4f || s.reached;
    if (done && !s.invalid) {
        s.Finish(g);
        if (s.index == (int)s.cases.size() - 1) s.finished = true;
        else s.nextCase = true;
    }
}

void TrafficTurnTests::Draw(const Game& g) const {
    if (!state) return;
    const State& s = *state;
    ClearBackground({ 20, 25, 30, 255 });
    if (s.invalid || g.vehicles.empty()) { DrawUIText("CJ-020 turning fixture invalid. See the log.", 30, 60, 26, RED); return; }
    const float H = cfg::ROAD_HALF, L = cfg::LANE_OFFSET;
    Vector2 c = s.ic;
    Camera2D camera{}; camera.target = c + V2(40, 20);
    camera.offset = V2(GetScreenWidth() * 0.5f, GetScreenHeight() * 0.55f);
    camera.zoom = GetScreenHeight() / 560.0f;
    BeginMode2D(camera);
    // Road, blocks (sidewalk) and markings.
    DrawRectangleRec({ c.x - 900, c.y - 900, 1800, 1800 }, { 52, 56, 62, 255 });
    Color walk{ 118, 122, 128, 255 }, kerbLine{ 205, 208, 210, 255 };
    Rectangle blocks[4] = { { c.x + H, c.y + H, 900, 900 }, { c.x - H - 900, c.y + H, 900, 900 },
                            { c.x + H, c.y - H - 900, 900, 900 }, { c.x - H - 900, c.y - H - 900, 900, 900 } };
    for (const Rectangle& b : blocks) { DrawRectangleRec(b, walk); DrawRectangleLinesEx(b, 2, kerbLine); }
    for (float t = H + 8; t < 900; t += 40) {
        DrawLineEx(V2(c.x, c.y + t), V2(c.x, c.y + t + 22), 2, { 235, 235, 225, 170 });
        DrawLineEx(V2(c.x, c.y - t), V2(c.x, c.y - t - 22), 2, { 235, 235, 225, 170 });
        DrawLineEx(V2(c.x + t, c.y), V2(c.x + t + 22, c.y), 2, { 235, 235, 225, 170 });
        DrawLineEx(V2(c.x - t, c.y), V2(c.x - t - 22, c.y), 2, { 235, 235, 225, 170 });
    }
    DrawRectangleLinesEx({ c.x - H, c.y - H, 2 * H, 2 * H }, 1, { 200, 200, 120, 90 });
    DrawLineEx(V2(c.x, c.y + H + 72), V2(c.x + H, c.y + H + 72), 3, { 240, 240, 240, 200 });   // stop line
    // 1 m ticks along the lanes, lane centres faint.
    Color laneC{ 215, 230, 140, 60 };
    DrawLineEx(V2(c.x + L, c.y + 900), V2(c.x + L, c.y - 900), 1, laneC);
    DrawLineEx(V2(c.x - 900, c.y + L), V2(c.x + 900, c.y + L), 1, laneC);
    DrawLineEx(V2(c.x - 900, c.y - L), V2(c.x + 900, c.y - L), 1, laneC);
    // Body outlines every 0.2 s, then the front (cyan) and rear (amber) axle traces.
    for (const OBB& b : s.ghosts) {
        Vector2 k[4]; OBBCorners(b, k);
        for (int i = 0; i < 4; i++) DrawLineEx(k[i], k[(i + 1) % 4], 1, { 190, 200, 215, 110 });
    }
    for (size_t i = 1; i < s.frontTrace.size(); i++) DrawLineEx(s.frontTrace[i - 1], s.frontTrace[i], 2, { 90, 210, 240, 230 });
    for (size_t i = 1; i < s.rearTrace.size(); i++) DrawLineEx(s.rearTrace[i - 1], s.rearTrace[i], 2.5f, { 255, 190, 70, 255 });
    const Vehicle& v = g.vehicles[s.car];
    const VehicleSprite& sprite = gAssets.vehicles[v.skin];
    DrawTexturePro(sprite.tex, sprite.src, { v.pos.x + 3, v.pos.y + 4, v.width, v.length },
                   V2(v.width * 0.5f, v.length * 0.5f), v.angle * RAD2DEG, { 0, 0, 0, 120 });
    DrawTexturePro(sprite.tex, sprite.src, { v.pos.x, v.pos.y, v.width, v.length },
                   V2(v.width * 0.5f, v.length * 0.5f), v.angle * RAD2DEG, WHITE);
    EndMode2D();
    DrawRectangle(0, 0, GetScreenWidth(), 140, { 12, 17, 23, 240 });
    DrawUIText("CONCRETE JUNGLE / CJ-020 TURNING KINEMATICS", 25, 14, 25, { 215, 231, 241, 255 }, true);
    DrawUIText(TextFormat("%s | case %d / %d | %.2f s | %.0f Hz | turning circle %.1f m, min rear radius %.2f m",
                          s.Current().name.c_str(), s.index + 1, (int)s.cases.size(), s.phaseTime, 1.0f / s.Current().step,
                          v.S().turnCircle / cfg::M, s.classRadius / cfg::M), 25, 50, 21, WHITE);
    DrawUIText(TextFormat("slip turning %.1f deg | rear radius %.2f m | lateral %.2f m/s2 | kerb %.1f px | encroach %.1f px | failures %d",
                          s.turnSlip, s.minRadius < 1e8f ? s.minRadius / cfg::M : 0.0f, s.maxLateral / cfg::M, s.kerb, s.encroach,
                          s.failures), 25, 86, 19, s.failures ? Color{ 255, 151, 115, 255 } : Color{ 116, 217, 165, 255 });
    DrawRectangle(0, GetScreenHeight() - 38, GetScreenWidth(), 38, { 12, 17, 23, 235 });
    DrawUIText(s.lastResult.empty() ? "Amber: rear axle path. Cyan: front axle path. Outlines every 0.2 s. Kerbs at the road edges."
                                    : s.lastResult.c_str(), 25, GetScreenHeight() - 29, 18, LIGHTGRAY);
}

void TrafficTurnTests::Log() const {
    if (!state) return;
    const State& s = *state;
    TraceLog(LOG_INFO, "CJ020T summary fixture=%s seed=%08x scheduled=%d completed=%d passed_cases=%d render_frames=%d physics_steps=%d checks=%d failures=%d incomplete=%d invalid=%d result=%s",
             FIXTURE_ID, SEED, (int)s.cases.size(), s.completed, s.passedCases, s.frame, s.physicsSteps, s.checks, s.failures,
             (int)!s.finished, (int)s.invalid, s.finished && !s.invalid && s.failures == 0 ? "PASS" : "FAIL");
}
bool TrafficTurnTests::Failed() const { return state && (state->invalid || state->failures > 0 || !state->finished); }
bool TrafficTurnTests::Finished() const { return state && state->finished; }
const char* TrafficTurnTests::CaptureLabel() const { return state && !state->capture.empty() ? state->capture.c_str() : nullptr; }
void TrafficTurnTests::ClearCaptureRequest() { if (state) state->capture.clear(); }
