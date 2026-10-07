// =====================================================================================
//  CJ-016 separated-box regression: non-overlapping nearby boxes must not veto rejoin.
//  Real overlap, clearance-only contact and a forward obstruction must still veto it.
//  Guard cases call the production API without physics; clear cases also drive and step.
// =====================================================================================
#include "traffic_clearance_tests.h"
#include "game.h"
#include "traffic.h"
#include "traffic_recovery.h"
#include <cstdlib>
#include <string>

namespace {
constexpr const char* FIXTURE_ID = "cj016-clearance-v1";
constexpr uint32_t SEED = 0x000c0016u;
constexpr float RENDER_DT = 1.0f / 60.0f;
constexpr int CASE_COUNT = 8;
constexpr int TOTAL_FRAMES = 420;

enum class Kind { Clear, Overlap, Margin, ForwardBlocker };
struct Case { Kind kind; const char* name; int frames; float step; };
const Case CASES[CASE_COUNT] = {
    { Kind::Clear, "clear-nearby-boxes-60hz", 120, RENDER_DT },
    { Kind::Overlap, "actual-overlap-60hz", 30, RENDER_DT },
    { Kind::Margin, "clearance-margin-only-60hz", 30, RENDER_DT },
    { Kind::ForwardBlocker, "forward-blocker-60hz", 30, RENDER_DT },
    { Kind::Clear, "clear-nearby-boxes-20hz", 120, 1.0f / 20.0f },
    { Kind::Overlap, "actual-overlap-20hz", 30, 1.0f / 20.0f },
    { Kind::Margin, "clearance-margin-only-20hz", 30, 1.0f / 20.0f },
    { Kind::ForwardBlocker, "forward-blocker-20hz", 30, 1.0f / 20.0f }
};

bool SamePhysicalState(const Vehicle& a, const Vehicle& b) {
    return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.angle == b.angle &&
           a.vel.x == b.vel.x && a.vel.y == b.vel.y && a.angVel == b.angVel;
}
} // namespace

struct TrafficClearanceTests::State {
    int skin = -1, driverSkin = 0, index = 0, frame = 0, phaseFrame = 0;
    int checks = 0, failures = 0, completed = 0, physicsSteps = 0, apiCalls = 0;
    int phaseApiCalls = 0, phasePhysicsSteps = 0, mismatches = 0, mutations = 0, ownershipLosses = 0;
    bool invalid = false, finished = false, nextCase = false, lastClear = false;
    float rejoined = -1;
    Vector2 initialPos{};
    std::string capture, lastResult;

    const Case& Current() const { return CASES[index]; }
    bool Ownership(const Game& g) const {
        if (g.vehicles.empty()) return false;
        const Vehicle& v = g.vehicles[0];
        return v.active && v.driver == DriverType::Traffic && v.driverSkin == driverSkin &&
               v.skin == skin && v.cls == gAssets.vehicles[skin].cls;
    }
    void Check(const char* name, float value, float low, float high) {
        bool pass = std::isfinite(value) && value >= low && value <= high;
        checks++;
        if (!pass) { failures++; lastResult = std::string(Current().name) + ": " + name + " FAILED"; }
        TraceLog(LOG_INFO, "CJ016C metric fixture=%s class=Taxi phase=%s name=%s value=%.6f min=%.6f max=%.6f result=%s",
                 FIXTURE_ID, Current().name, name, value, low, high, pass ? "PASS" : "FAIL");
    }
    void Wall(Game& g, Rectangle r) {
        Building b; b.r = r; b.height = 3.0f * cfg::M;
        g.map.buildings.push_back(b);
    }
    void Start(Game& g) {
        phaseFrame = phaseApiCalls = phasePhysicsSteps = mismatches = mutations = ownershipLosses = 0;
        rejoined = -1; lastClear = false;
        g.vehicles.clear(); g.peds.clear(); g.pickups.clear();
        g.map.ResetTestGround(Tile::Road); g.pedGrid.Build(g.peds);
        g.physics = VehiclePhysics{}; GRng().s = SEED;
        initialPos = g.map.InterCenter(3, 3) + V2(cfg::LANE_OFFSET, 350);
        Vehicle ego; InitVehicle(ego, skin, initialPos, 0);
        ego.driver = DriverType::Traffic; ego.driverSkin = driverSkin;
        ego.missionTarget = true; ego.ai.rail = false; ego.ai.cruise = 180;
        AIResetPath(ego, g.map); g.vehicles.push_back(ego);
        g.player.inVehicle = false; g.player.vehicle = -1;
        g.player.pos = initialPos + V2(-600, 0); g.player.vel = {};
        g.cam.Snap(initialPos, 900); g.heat = 0; g.time = 0;
        if (Current().kind == Kind::Clear) {
            // Both boxes are inside the local neighbourhood but laterally separated.
            // The parked vehicle remains within 900 px throughout the full 2 s case.
            Wall(g, { initialPos.x - 220, initialPos.y - 50, 40, 100 });
            Vehicle parked; InitVehicle(parked, skin, initialPos + V2(200, 0), 0);
            parked.driver = DriverType::None; parked.in.handbrake = true;
            parked.missionTarget = true; g.vehicles.push_back(parked);
        } else if (Current().kind == Kind::Overlap || Current().kind == Kind::Margin) {
            float gap = Current().kind == Kind::Overlap ? -2.0f : 0.5f;
            Wall(g, { initialPos.x + ego.width * 0.5f + gap, initialPos.y - 100, 24, 200 });
        } else {
            float bottom = initialPos.y - ego.length * 0.5f - 35;
            Wall(g, { initialPos.x - 60, bottom - 24, 120, 24 });
        }
        g.map.RebuildTestIndex();
        TraceLog(LOG_INFO, "CJ016C begin fixture=%s class=Taxi phase=%s seed=%08x expected_clear=%d physics_dt=%.8f duration_s=%.3f x=%.3f y=%.3f width_px=%.3f length_px=%.3f buildings=%d vehicles=%d",
                 FIXTURE_ID, Current().name, SEED, (int)(Current().kind == Kind::Clear), Current().step,
                 Current().frames * RENDER_DT, initialPos.x, initialPos.y, ego.width, ego.length,
                 (int)g.map.buildings.size(), (int)g.vehicles.size());
    }
    void Probe(Game& g) {
        // The API may update planner diagnostics/timing, but must leave the real body
        // and its driver intact. The snapshot is always captured before the call.
        AIObserveTraffic(g);
        Vehicle before = g.vehicles[0];
        bool ownedBefore = Ownership(g);
        lastClear = RecoveryCanRejoin(g, 0, Current().step);
        phaseApiCalls++; apiCalls++;
        if (lastClear != (Current().kind == Kind::Clear)) mismatches++;
        if (!SamePhysicalState(before, g.vehicles[0])) mutations++;
        if (!ownedBefore || !Ownership(g)) ownershipLosses++;
    }
    void Finish() {
        int failuresBefore = failures;
        Check("api_expected", mismatches == 0 ? 1.0f : 0.0f, 1, 1);
        Check("api_pose_unchanged", mutations == 0 ? 1.0f : 0.0f, 1, 1);
        Check("ownership_preserved", ownershipLosses == 0 ? 1.0f : 0.0f, 1, 1);
        if (Current().kind == Kind::Clear) Check("rejoined_s", rejoined, 0, 2);
        completed++;
        TraceLog(LOG_INFO, "CJ016C result fixture=%s class=Taxi phase=%s expected_clear=%d api_calls=%d physics_steps=%d rejoined_s=%.6f checks=%d failures=%d result=%s",
                 FIXTURE_ID, Current().name, (int)(Current().kind == Kind::Clear), phaseApiCalls, phasePhysicsSteps,
                 rejoined, Current().kind == Kind::Clear ? 4 : 3, failures - failuresBefore,
                 failures == failuresBefore ? "PASS" : "FAIL");
        capture = Current().name;
    }
};

TrafficClearanceTests::TrafficClearanceTests() = default;
TrafficClearanceTests::~TrafficClearanceTests() = default;

bool TrafficClearanceTests::Init(Game& g) {
    state = std::make_unique<State>();
    State& s = *state;
    VClass cls = FindVehicleClass("Taxi");
    for (size_t i = 0; i < gAssets.vehicles.size(); i++)
        if (gAssets.vehicles[i].cls == cls && gAssets.vehicles[i].spawnable) { s.skin = (int)i; break; }
    if (s.skin < 0) { s.invalid = true; TraceLog(LOG_ERROR, "CJ016C missing Taxi sprite"); return false; }
    const char* revision = std::getenv("CJ_TEST_REVISION");
    TraceLog(LOG_INFO, "CJ016C run fixture=%s scenario=traffic-clearance class=Taxi revision=%s seed=%08x planned_cases=%d render_dt=%.8f build=%s_%s",
             FIXTURE_ID, revision ? revision : "UNRECORDED", SEED, CASE_COUNT, RENDER_DT, __DATE__, __TIME__);
    for (int i = 0; i < CASE_COUNT; i++)
        TraceLog(LOG_INFO, "CJ016C scheduled index=%d phase=%s frames=%d simulation_s=%.3f physics_dt=%.8f expected_clear=%d",
                 i + 1, CASES[i].name, CASES[i].frames, CASES[i].frames * RENDER_DT, CASES[i].step,
                 (int)(CASES[i].kind == Kind::Clear));
    TraceLog(LOG_INFO, "CJ016C schedule render_frames=%d simulated_s=7.000 physics_steps=160 api_calls=280 checks=26", TOTAL_FRAMES);
    s.Start(g); return true;
}

void TrafficClearanceTests::Update(Game& g, float dt) {
    if (!state || state->invalid || state->finished) return;
    State& s = *state;
    if (!std::isfinite(dt) || fabsf(dt - RENDER_DT) > 1e-6f) {
        s.invalid = true; TraceLog(LOG_ERROR, "CJ016C requires a fixed 1/60 s render clock"); return;
    }
    if (s.nextCase) { s.index++; s.Start(g); s.nextCase = false; }
    const Case& c = s.Current();
    s.frame++;
    int divisor = c.step > 0.03f ? 3 : 1;
    if ((s.phaseFrame + 1) % divisor == 0) {
        g.time += c.step;
        s.Probe(g);
        if (c.kind == Kind::Clear) {
            Vehicle& ego = g.vehicles[0];
            ego.kinFrom = ego.pos; ego.kinFromAng = ego.angle;
            AIUpdateTraffic(g, 0, c.step);
            PhysicsStepOptions options; options.disableWorldEdges = true;
            g.physics.Step(g, c.step, options);
            s.physicsSteps++; s.phasePhysicsSteps++;
            if (!s.Ownership(g)) s.ownershipLosses++;
            if (g.vehicles[0].ai.rail && s.rejoined < 0) s.rejoined = (s.phaseFrame + 1) * RENDER_DT;
        }
    }
    s.phaseFrame++;
    if (s.phaseFrame == c.frames) {
        s.Finish();
        if (s.index == CASE_COUNT - 1) s.finished = true;
        else s.nextCase = true;
    }
}

void TrafficClearanceTests::Draw(const Game& g) const {
    if (!state) return;
    const State& s = *state;
    ClearBackground({ 20, 25, 30, 255 });
    if (s.invalid || g.vehicles.empty()) { DrawUIText("CJ-016 clearance fixture invalid. See the log.", 30, 60, 26, RED); return; }
    Camera2D camera{}; camera.target = s.initialPos;
    camera.offset = V2(GetScreenWidth() * 0.5f, GetScreenHeight() * 0.58f); camera.zoom = 1.0f;
    BeginMode2D(camera);
    float tile = 16 * cfg::M;
    Texture2D ground = gAssets.asphalt;
    for (int y = -4; y <= 4; y++) for (int x = -5; x <= 5; x++)
        DrawTexturePro(ground, { 0, 0, (float)ground.width, (float)ground.height },
                       { s.initialPos.x + x * tile, s.initialPos.y + y * tile, tile, tile }, {}, 0, { 165, 175, 185, 255 });
    for (int i = -5; i <= 5; i++) {
        DrawLineV(s.initialPos + V2(i * 10 * cfg::M, -700), s.initialPos + V2(i * 10 * cfg::M, 700), { 140, 160, 170, 60 });
        DrawLineV(s.initialPos + V2(-900, i * 10 * cfg::M), s.initialPos + V2(900, i * 10 * cfg::M), { 140, 160, 170, 60 });
    }
    DrawLineEx(s.initialPos + V2(0, -500), s.initialPos + V2(0, 500), 2, { 215, 230, 140, 180 });
    for (const Building& b : g.map.buildings) {
        DrawRectangleRec(b.r, { 80, 91, 104, 255 });
        DrawRectangleLinesEx(b.r, 2, { 180, 195, 205, 255 });
        DrawText("BUILDING", (int)b.r.x, (int)b.r.y - 20, 16, LIGHTGRAY);
    }
    for (size_t i = 0; i < g.vehicles.size(); i++) {
        const Vehicle& v = g.vehicles[i];
        const VehicleSprite& sprite = gAssets.vehicles[v.skin];
        const float sw = SpriteWidth(v);
        DrawTexturePro(sprite.tex, sprite.src, { v.pos.x, v.pos.y, sw, v.length },
                       V2(sw * 0.5f, v.length * 0.5f), v.angle * RAD2DEG, WHITE);
        Vector2 corners[4]; OBBCorners(v.Box(), corners);
        for (int k = 0; k < 4; k++) DrawLineEx(corners[k], corners[(k + 1) % 4], 1, i == 0 ? GREEN : ORANGE);
        DrawText(i == 0 ? "RECOVERY TAXI" : "PARKED TAXI", (int)v.pos.x - 55, (int)(v.pos.y + v.length * 0.5f + 8), 16, WHITE);
    }
    EndMode2D();
    DrawRectangle(0, 0, GetScreenWidth(), 173, { 12, 17, 23, 240 });
    DrawUIText("CONCRETE JUNGLE / CJ-016 NEARBY-BOX CLEARANCE", 25, 15, 25, { 215, 231, 241, 255 }, true);
    DrawUIText(TextFormat("Taxi | %s | case %d / 8 | %.2f / %.2f s", s.Current().name, s.index + 1,
                         s.phaseFrame * RENDER_DT, s.Current().frames * RENDER_DT), 25, 52, 23, WHITE);
    DrawUIText(TextFormat("API expected %s / observed %s | %s | %.0f Hz | calls %d | unchanged-body violations %d",
                         s.Current().kind == Kind::Clear ? "CLEAR" : "BLOCKED", s.lastClear ? "CLEAR" : "BLOCKED",
                         s.Current().kind == Kind::Clear ? "REAL AI + PHYSICS" : "STATIC API GUARD", 1.0f / s.Current().step,
                         s.phaseApiCalls, s.mutations), 25, 89, 20, { 151, 198, 211, 255 });
    DrawUIText(TextFormat("%s | checks %d / failures %d | rejoin %.3f s | seed %08x | 10 m grid / real sprites",
                         s.finished ? "COMPLETE" : "MEASURING", s.checks, s.failures, s.rejoined, SEED), 25, 127, 19,
               s.failures ? Color{ 255, 151, 115, 255 } : Color{ 116, 217, 165, 255 });
    DrawRectangle(0, GetScreenHeight() - 38, GetScreenWidth(), 38, { 12, 17, 23, 235 });
    DrawUIText(s.lastResult.empty() ? "Separated boxes permit rejoin; actual contact, clearance contact and forward blockage still reject it."
                                  : s.lastResult.c_str(), 25, GetScreenHeight() - 29, 18, LIGHTGRAY);
}

void TrafficClearanceTests::Log() const {
    if (!state) return;
    const State& s = *state;
    TraceLog(LOG_INFO, "CJ016C summary fixture=%s class=Taxi seed=%08x scheduled=%d completed=%d render_frames=%d physics_steps=%d api_calls=%d simulated_s=%.3f checks=%d failures=%d incomplete=%d invalid=%d result=%s",
             FIXTURE_ID, SEED, CASE_COUNT, s.completed, s.frame, s.physicsSteps, s.apiCalls, s.frame * RENDER_DT,
             s.checks, s.failures, (int)!s.finished, (int)s.invalid, s.finished && !s.invalid && s.failures == 0 ? "PASS" : "FAIL");
}
bool TrafficClearanceTests::Failed() const { return state && (state->invalid || state->failures > 0 || !state->finished); }
bool TrafficClearanceTests::Finished() const { return state && state->finished; }
const char* TrafficClearanceTests::CaptureLabel() const { return state && !state->capture.empty() ? state->capture.c_str() : nullptr; }
void TrafficClearanceTests::ClearCaptureRequest() { if (state) state->capture.clear(); }
