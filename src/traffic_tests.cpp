// =====================================================================================
//  CJ-016 recovery measurements, frozen before the production recovery rewrite.
//  A trapped driver must hold indefinitely; a free or reverse escape must reach a lane
//  through actual controls. Fixture resets happen between cases, outside observations.
// =====================================================================================
#include "traffic_tests.h"
#include "game.h"
#include "traffic.h"
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {
constexpr const char* FIXTURE_ID = "cj016-recovery-v1";
constexpr float RENDER_DT = 1.0f / 60.0f;
constexpr uint32_t SEED = 0x000c0016u;
constexpr int CASE_COUNT = 6;
constexpr float LATERAL_OFFSET = 120.0f;
constexpr float WALL_CLEARANCE = 5.0f;
constexpr float WALL_THICKNESS = 24.0f;

enum class Kind { Enclosed, Free, Garage };
struct Case {
    Kind kind;
    const char* name;
    int frames;
    float step;
};
const Case CASES[CASE_COUNT] = {
    { Kind::Enclosed, "enclosed-60hz", 3600, RENDER_DT },
    { Kind::Free, "free-60hz", 1800, RENDER_DT },
    { Kind::Garage, "garage-60hz", 1800, RENDER_DT },
    { Kind::Enclosed, "enclosed-20hz", 3600, 1.0f / 20.0f },
    { Kind::Free, "free-20hz", 1800, 1.0f / 20.0f },
    { Kind::Garage, "garage-20hz", 1800, 1.0f / 20.0f }
};

bool Finite(const Vehicle& v) {
    return std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && std::isfinite(v.vel.x) &&
           std::isfinite(v.vel.y) && std::isfinite(v.angle) && std::isfinite(v.angVel);
}
uint32_t ConfigHash() {
    std::ifstream file("assets/data/vehicles.cfg", std::ios::binary);
    uint32_t hash = 2166136261u;
    char c;
    while (file.get(c)) { hash ^= (unsigned char)c; hash *= 16777619u; }
    return hash;
}
float Penetration(const Game& g, const Vehicle& v) {
    Vector2 normal; float depth, maximum = 0;
    for (const Building& wall : g.map.buildings)
        if (wall.Solid() && OBBOverlap(v.Box(), MakeAABB(wall.r), normal, depth)) maximum = std::max(maximum, depth);
    return maximum;
}
} // namespace

struct TrafficTests::State {
    std::string className, capture, lastResult;
    int skin = -1, driverSkin = 0, index = 0, frame = 0, phaseFrame = 0;
    int checks = 0, failures = 0, completed = 0, physicsSteps = 0;
    int ownershipLosses = 0, teleports = 0, nonfinite = 0, deepSteps = 0, contacts = 0;
    int lastDriver = -1, lastReason = -1, lastGear = -1, railTransitions = 0;
    bool invalid = false, finished = false, nextCase = false, lastRail = false;
    VClass cls = -1;
    Vector2 base{}, initialPos{};
    float phaseTime = 0, maxPen = 0, reverse = 0, maxDisplacement = 0;
    float rejoined = -1, maxPoseStep = 0, maxBlend = 0, pressingTime = 0, peakSpeed = 0;
    double aiTotal = 0, physicsTotal = 0;
    std::vector<Vector2> trail;
    std::vector<double> aiTimes;

    const Case& Current() const { return CASES[index]; }
    void Check(const char* metric, double value, double low, double high) {
        bool pass = std::isfinite(value) && value >= low && value <= high;
        checks++; if (!pass) { failures++; lastResult = std::string(Current().name) + ": " + metric + " FAILED"; }
        TraceLog(LOG_INFO, "CJ016 metric fixture=%s class=%s phase=%s name=%s value=%.6f min=%.6f max=%.6f result=%s",
                 FIXTURE_ID, className.c_str(), Current().name, metric, value, low, high, pass ? "PASS" : "FAIL");
    }
    void Wall(Game& g, Rectangle r) {
        Building wall; wall.r = r; wall.height = 3.0f * cfg::M;
        g.map.buildings.push_back(wall);
    }
    void Start(Game& g) {
        const Case& c = Current();
        phaseFrame = 0; phaseTime = 0; rejoined = -1; maxPen = 0; reverse = 0;
        ownershipLosses = teleports = nonfinite = deepSteps = contacts = 0;
        maxDisplacement = maxPoseStep = maxBlend = pressingTime = peakSpeed = 0;
        lastDriver = lastReason = lastGear = -1; lastRail = false; railTransitions = 0;
        trail.clear(); aiTimes.clear(); aiTotal = physicsTotal = 0;
        g.vehicles.clear(); g.peds.clear(); g.pickups.clear(); g.pedGrid.Build(g.peds);
        g.map.ResetTestGround(Tile::Road);
        g.physics = VehiclePhysics{};
        GRng().s = SEED;
        base = g.map.InterCenter(3, 3) + V2(cfg::LANE_OFFSET, 350.0f);
        initialPos = base + V2(LATERAL_OFFSET, 0);
        Vehicle car; InitVehicle(car, skin, initialPos, 0);
        car.driver = DriverType::Traffic; car.driverSkin = driverSkin;
        car.missionTarget = true; // A tracked fixture cannot take the off-screen recycling path.
        car.ai.rail = false; car.ai.cruise = 180;
        AIResetPath(car, g.map);
        g.vehicles.push_back(car);
        g.cam.Snap(initialPos, 900); g.player.inVehicle = false; g.player.vehicle = -1;
        g.player.pos = initialPos + V2(-400, 0); g.heat = 0; g.time = 0;
        float left = initialPos.x - car.width * 0.5f - WALL_CLEARANCE;
        float right = initialPos.x + car.width * 0.5f + WALL_CLEARANCE;
        float top = initialPos.y - car.length * 0.5f - WALL_CLEARANCE;
        float bottom = initialPos.y + car.length * 0.5f + WALL_CLEARANCE;
        if (c.kind == Kind::Enclosed) {
            Wall(g, { left - WALL_THICKNESS, top - WALL_THICKNESS, right - left + WALL_THICKNESS * 2, WALL_THICKNESS });
            Wall(g, { left - WALL_THICKNESS, bottom, right - left + WALL_THICKNESS * 2, WALL_THICKNESS });
            Wall(g, { left - WALL_THICKNESS, top, WALL_THICKNESS, bottom - top });
            Wall(g, { right, top, WALL_THICKNESS, bottom - top });
        } else if (c.kind == Kind::Garage) {
            // A physical garage has an open rear. The car must reverse beyond the side
            // walls before steering towards its lane, rather than turning into them.
            bottom = initialPos.y + car.length * 1.5f;
            Wall(g, { left - WALL_THICKNESS, top - WALL_THICKNESS, right - left + WALL_THICKNESS * 2, WALL_THICKNESS });
            Wall(g, { left - WALL_THICKNESS, top, WALL_THICKNESS, bottom - top });
            Wall(g, { right, top, WALL_THICKNESS, bottom - top });
        }
        g.map.RebuildTestIndex();
        trail.push_back(initialPos);
        TraceLog(LOG_INFO, "CJ016 begin fixture=%s class=%s phase=%s seed=%08x physics_dt=%.8f duration_s=%.3f x=%.3f y=%.3f angle=0 width_px=%.3f length_px=%.3f offset_px=%.3f wall_clearance_px=%.3f",
                 FIXTURE_ID, className.c_str(), c.name, SEED, c.step, c.frames * RENDER_DT,
                 initialPos.x, initialPos.y, car.width, car.length, LATERAL_OFFSET, WALL_CLEARANCE);
    }
    void Observe(Game& g, const Vehicle& before, float step) {
        phaseTime += step;
        Vehicle& car = g.vehicles[0];
        if (!car.active || car.driver != DriverType::Traffic || car.driverSkin != driverSkin || car.cls != cls || car.skin != skin) ownershipLosses++;
        if (!Finite(car)) { nonfinite++; invalid = true; return; }
        float distance = Dist(before.pos, car.pos);
        float physicalTravel = std::max(before.Speed(), car.Speed()) * step;
        float poseError = std::max(0.0f, distance - physicalTravel);
        maxPoseStep = std::max(maxPoseStep, poseError);
        if (distance > std::max(8.0f, physicalTravel + 4.0f)) {
            teleports++;
            if (teleports < 5) TraceLog(LOG_WARNING, "CJ016 pose_jump phase=%s time_s=%.3f distance_px=%.3f velocity_travel_px=%.3f", Current().name, phaseTime, distance, physicalTravel);
        }
        maxDisplacement = std::max(maxDisplacement, Dist(initialPos, car.pos));
        peakSpeed = std::max(peakSpeed, car.Speed());
        maxBlend = std::max(maxBlend, car.ai.blend);
        reverse += std::max(0.0f, -Dot(car.pos - before.pos, before.Fwd()));
        maxPen = std::max(maxPen, Penetration(g, car));
        if (Penetration(g, car) > 3.0f) deepSteps++;
        contacts += (int)g.physics.events.size();
        if (Current().kind == Kind::Enclosed && phaseTime > 1.0f && !car.in.handbrake &&
            (car.in.throttle > 0.3f || car.in.brake > 0.3f)) pressingTime += step;
        if (car.ai.rail && car.ai.blend <= 0.001f && rejoined < 0) rejoined = phaseTime;
        int gear = car.ai.gearTimer > 0 ? -1 : car.in.throttle > 0 ? 1 : 0;
        if (lastDriver != (int)car.driver || lastRail != car.ai.rail || lastReason != car.ai.reason || lastGear != gear) {
            if (lastRail != car.ai.rail) railTransitions++;
            TraceLog(LOG_INFO, "CJ016 state phase=%s time_s=%.3f vehicle=0 driver=%d rail=%d reason=%d gear=%d recover_s=%.3f speed_px_s=%.3f throttle=%.3f brake=%.3f steer=%.3f handbrake=%d",
                     Current().name, phaseTime, (int)car.driver, (int)car.ai.rail, car.ai.reason, gear,
                     car.ai.recover, car.Speed(), car.in.throttle, car.in.brake, car.in.steer, (int)car.in.handbrake);
            lastDriver = (int)car.driver; lastRail = car.ai.rail; lastReason = car.ai.reason; lastGear = gear;
        }
        if (phaseFrame % 6 == 0 && trail.size() < 2400) trail.push_back(car.pos);
    }
    void Finish(const Game& g) {
        int failuresBefore = failures;
        const Vehicle& car = g.vehicles[0];
        Check("ownership_losses", ownershipLosses, 0, 0);
        Check("nonfinite", nonfinite, 0, 0);
        Check("teleports", teleports, 0, 0);
        Check("penetration_px", maxPen, 0, 3);
        Check("deep_steps", deepSteps, 0, 0);
        Check("rejoin_blend_s", maxBlend, 0, 0);
        if (Current().kind == Kind::Enclosed) {
            Check("present_at_60s", car.active && car.driver == DriverType::Traffic ? 1 : 0, 1, 1);
            Check("enclosed_displacement_px", maxDisplacement, 0, 5);
            Check("enclosed_final_speed_px_s", car.Speed(), 0, 2);
            Check("enclosed_pressing_s", pressingTime, 0, 0.25);
            Check("enclosed_no_rejoin", rejoined < 0 ? 1 : 0, 1, 1);
        } else {
            Check("rejoined_s", rejoined, 0, 30);
            if (Current().kind == Kind::Garage) Check("reverse_escape_px", reverse, car.length * 1.5f, 2000);
        }
        std::sort(aiTimes.begin(), aiTimes.end());
        double avg = aiTimes.empty() ? 0 : aiTotal / aiTimes.size();
        double p95 = aiTimes.empty() ? 0 : aiTimes[(aiTimes.size() - 1) * 95 / 100];
        // raylib bounds each trace message. Keep outcomes, diagnostics and timing on
        // separate lines so heavy-vehicle numbers cannot truncate the result field.
        TraceLog(LOG_INFO, "CJ016 result fixture=%s class=%s phase=%s duration_s=%.3f physics_dt=%.8f rejoined_s=%.3f reverse_px=%.3f max_pen_px=%.3f ownership_losses=%d teleports=%d nonfinite=%d result=%s",
                 FIXTURE_ID, className.c_str(), Current().name, phaseTime, Current().step, rejoined, reverse, maxPen,
                 ownershipLosses, teleports, nonfinite, failures == failuresBefore ? "PASS" : "FAIL");
        TraceLog(LOG_INFO, "CJ016 diagnostics fixture=%s class=%s phase=%s contacts=%d rail_transitions=%d pose_error_px=%.3f blend_s=%.3f pressing_s=%.3f",
                 FIXTURE_ID, className.c_str(), Current().name, contacts, railTransitions, maxPoseStep, maxBlend, pressingTime);
        TraceLog(LOG_INFO, "CJ016 timing fixture=%s class=%s phase=%s samples=%d ai_avg_ms=%.6f ai_p95_ms=%.6f physics_avg_ms=%.6f",
                 FIXTURE_ID, className.c_str(), Current().name, (int)aiTimes.size(), avg, p95,
                 aiTimes.empty() ? 0 : physicsTotal / aiTimes.size());
        completed++; capture = Current().name;
    }
};

TrafficTests::TrafficTests() = default;
TrafficTests::~TrafficTests() = default;

bool TrafficTests::Init(Game& g, const char* className) {
    state = std::make_unique<State>();
    State& s = *state;
    s.className = className && *className ? className : "Taxi";
    if (s.className != "Taxi" && s.className != "Bus" && s.className != "BoxTruck") {
        s.invalid = true; TraceLog(LOG_ERROR, "CJ016 supported classes: Taxi, Bus, BoxTruck (got '%s')", s.className.c_str()); return true;
    }
    s.cls = FindVehicleClass(s.className);
    for (size_t i = 0; i < gAssets.vehicles.size(); i++)
        if (gAssets.vehicles[i].cls == s.cls && gAssets.vehicles[i].spawnable) { s.skin = (int)i; break; }
    if (s.skin < 0) { s.invalid = true; TraceLog(LOG_ERROR, "CJ016 missing sprite for %s", s.className.c_str()); return true; }
    const char* revision = std::getenv("CJ_TEST_REVISION");
    TraceLog(LOG_INFO, "CJ016 run fixture=%s scenario=traffic-recovery revision=%s config_fnv1a=%08x seed=%08x class=%s planned_cases=%d render_dt=%.8f build=%s_%s",
             FIXTURE_ID, revision ? revision : "UNRECORDED", ConfigHash(), SEED, s.className.c_str(), CASE_COUNT, RENDER_DT, __DATE__, __TIME__);
    for (int i = 0; i < CASE_COUNT; i++) TraceLog(LOG_INFO, "CJ016 scheduled index=%d phase=%s frames=%d simulation_s=%.3f physics_dt=%.8f",
                                               i + 1, CASES[i].name, CASES[i].frames, CASES[i].frames * RENDER_DT, CASES[i].step);
    TraceLog(LOG_INFO, "CJ016 schedule render_frames=14400 simulation_s=240.000 budget_frames=14400");
    s.Start(g); return true;
}

void TrafficTests::Update(Game& g, float dt) {
    if (!state || state->finished || state->invalid) return;
    State& s = *state;
    if (fabsf(dt - RENDER_DT) > 1e-6f) { s.invalid = true; TraceLog(LOG_ERROR, "CJ016 requires fixed 1/60 s render clock"); return; }
    if (s.nextCase) { s.index++; s.Start(g); s.nextCase = false; }
    s.frame++;
    const Case& c = s.Current();
    int divisor = c.step > 0.03f ? 3 : 1;
    if ((s.phaseFrame + 1) % divisor == 0) {
        Vehicle before = g.vehicles[0];
        g.vehicles[0].kinFrom = before.pos; g.vehicles[0].kinFromAng = before.angle;
        g.time += c.step;
        double start = GetTime();
        AIObserveTraffic(g);
        if (g.vehicles[0].active && g.vehicles[0].driver == DriverType::Traffic) AIUpdateTraffic(g, 0, c.step);
        double aiMs = (GetTime() - start) * 1000.0;
        s.aiTotal += aiMs; s.aiTimes.push_back(aiMs);
        PhysicsStepOptions options; options.disableWorldEdges = true;
        start = GetTime(); g.physics.Step(g, c.step, options); s.physicsTotal += (GetTime() - start) * 1000.0;
        s.physicsSteps++; s.Observe(g, before, c.step);
    }
    s.phaseFrame++;
    if (c.kind == Kind::Enclosed && (s.phaseFrame == 720 || s.phaseFrame == 1800))
        s.capture = std::string(c.name) + (s.phaseFrame == 720 ? "-t12" : "-t30");
    if (s.phaseFrame >= c.frames) {
        s.Finish(g);
        if (s.index == CASE_COUNT - 1) s.finished = true;
        else s.nextCase = true;
    }
}

void TrafficTests::Draw(const Game& g) const {
    if (!state) return;
    const State& s = *state;
    ClearBackground({ 20, 25, 30, 255 });
    if (s.invalid || g.vehicles.empty()) { DrawUIText("CJ-016 fixture is invalid. See the log.", 40, 70, 28, RED); return; }
    const Vehicle& car = g.vehicles[0];
    Vector2 focus = s.initialPos * 0.45f + car.pos * 0.55f;
    float zoom = 1.0f;
    Camera2D camera{}; camera.target = focus; camera.offset = V2(GetScreenWidth() * 0.5f, GetScreenHeight() * 0.57f); camera.zoom = zoom;
    BeginMode2D(camera);
    float halfW = GetScreenWidth(), halfH = GetScreenHeight();
    float tile = 16 * cfg::M;
    Texture2D ground = gAssets.asphalt;
    for (int y = (int)floorf((focus.y - halfH) / tile); y <= (int)ceilf((focus.y + halfH) / tile); y++)
        for (int x = (int)floorf((focus.x - halfW) / tile); x <= (int)ceilf((focus.x + halfW) / tile); x++)
            DrawTexturePro(ground, { 0, 0, (float)ground.width, (float)ground.height }, { x * tile, y * tile, tile, tile }, {}, 0, { 165, 175, 185, 255 });
    float grid = 10 * cfg::M;
    for (float x = floorf((focus.x - halfW) / grid) * grid; x < focus.x + halfW; x += grid)
        DrawLineV(V2(x, focus.y - halfH), V2(x, focus.y + halfH), { 140, 160, 170, 45 });
    for (float y = floorf((focus.y - halfH) / grid) * grid; y < focus.y + halfH; y += grid)
        DrawLineV(V2(focus.x - halfW, y), V2(focus.x + halfW, y), { 140, 160, 170, 45 });
    DrawLineEx(V2(s.base.x, s.base.y - 500), V2(s.base.x, s.base.y + 600), 3, { 215, 230, 140, 180 });
    DrawCircleLinesV(s.initialPos, 5, { 235, 185, 85, 255 });
    for (const Building& wall : g.map.buildings) {
        DrawRectangleRec(wall.r, { 80, 91, 104, 255 });
        DrawRectangleLinesEx(wall.r, 2, { 180, 195, 205, 255 });
    }
    for (size_t i = 1; i < s.trail.size(); i++) DrawLineEx(s.trail[i - 1], s.trail[i], 1.5f, { 76, 210, 194, 150 });
    const VehicleSprite& sprite = gAssets.vehicles[car.skin];
    DrawTexturePro(sprite.tex, sprite.src, { car.pos.x + 4, car.pos.y + 5, car.width, car.length }, V2(car.width * 0.5f, car.length * 0.5f), car.angle * RAD2DEG, { 0, 0, 0, 130 });
    DrawTexturePro(sprite.tex, sprite.src, { car.pos.x, car.pos.y, car.width, car.length }, V2(car.width * 0.5f, car.length * 0.5f), car.angle * RAD2DEG, WHITE);
    DrawLineEx(car.pos, car.pos + car.vel * 0.2f, 1.5f, { 85, 190, 255, 200 });
    EndMode2D();
    DrawRectangle(0, 0, GetScreenWidth(), 175, { 12, 17, 23, 238 });
    DrawUIText("CONCRETE JUNGLE / CJ-016 TRAFFIC RECOVERY", 28, 15, 26, { 215, 231, 241, 255 }, true);
    DrawUIText(TextFormat("%s   |   %s   |   case %d / %d   |   %.2f / %.0f s", s.className.c_str(), s.Current().name,
                         s.index + 1, CASE_COUNT, s.phaseTime, s.Current().frames * RENDER_DT), 28, 51, 24, WHITE);
    DrawUIText(TextFormat("driver %s    %s    speed %.2f px/s    physics %.0f Hz    penetration %.3f px    reverse %.1f px",
                         car.driver == DriverType::Traffic ? "PRESENT" : "LOST", car.ai.rail ? "LANE" : "RECOVERY",
                         car.Speed(), 1.0f / s.Current().step, s.maxPen, s.reverse), 28, 89, 21, { 151, 198, 211, 255 });
    DrawUIText(TextFormat("%s    checks %d / failures %d    seed %08x    10 m grid / production sprites / 16 px = 1 m",
                         s.finished ? "COMPLETE" : "MEASURING", s.checks, s.failures, SEED), 28, 126, 19,
               s.failures ? Color{ 255, 151, 115, 255 } : Color{ 116, 217, 165, 255 });
    DrawRectangle(0, GetScreenHeight() - 38, GetScreenWidth(), 38, { 12, 17, 23, 230 });
    DrawUIText(s.lastResult.empty() ? "Enclosed: hold with the same driver. Free / garage: physically recover within 30 s. Yellow line: target lane."
                                  : s.lastResult.c_str(), 25, GetScreenHeight() - 29, 18, { 220, 225, 230, 255 });
}

void TrafficTests::Log() const {
    if (!state) return;
    const State& s = *state;
    TraceLog(LOG_INFO, "CJ016 summary fixture=%s class=%s scheduled=%d completed=%d render_frames=%d physics_steps=%d simulated_s=%.3f checks=%d failures=%d incomplete=%d invalid=%d result=%s",
             FIXTURE_ID, s.className.c_str(), CASE_COUNT, s.completed, s.frame, s.physicsSteps, s.frame * RENDER_DT,
             s.checks, s.failures, (int)!s.finished, (int)s.invalid, s.finished && !s.invalid && s.failures == 0 ? "PASS" : "FAIL");
}
bool TrafficTests::Failed() const { return state && (state->invalid || state->failures > 0 || !state->finished); }
bool TrafficTests::Finished() const { return state && state->finished; }
const char* TrafficTests::CaptureLabel() const { return state && !state->capture.empty() ? state->capture.c_str() : nullptr; }
void TrafficTests::ClearCaptureRequest() { if (state) state->capture.clear(); }
