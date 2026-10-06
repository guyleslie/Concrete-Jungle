// =====================================================================================
//  CJ-016 driver incident fixture. A queue on uniform road: a parked car, a rail car
//  stopped behind it and a third car that runs into the back of the rail car (placed a
//  few pixels behind it at speed: the instant before a late-braking rear-end; the
//  contact itself is solved by production physics). Production traffic AI, impacts,
//  incident rules and pedestrian AI then run unchanged. Four situations:
//    aggressive-pair    both drivers aggressive: stop, exit, approach, argue, fight,
//                       and each survivor drives the same car on;
//    calm-pair          both calm: nobody gets out, the knocked car recovers;
//    aggressive-player  the player's car hits an aggressive driver; the player gets
//                       out and stands still; the driver fights, then drives on;
//    interrupted        an aggressive pair whose first car catches fire during the
//                       incident: that driver has no car to return to (logged reason).
//  Ten seeds vary gaps, speeds and the hit car's class; 60 Hz and 20 Hz physics.
// =====================================================================================
#include "traffic_incident_tests.h"
#include "game.h"
#include "traffic.h"
#include "traffic_incidents.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace {
constexpr const char* FIXTURE_ID = "cj016-incident-v1";
constexpr uint32_t SEED = 0x000c0016u;
constexpr float RENDER_DT = 1.0f / 60.0f;
constexpr int SEEDS = 10;
constexpr float MAX_CASE_S = 60.0f;
constexpr float SETTLE_S = 2.0f;

enum class Kind { AggressivePair, CalmPair, AggressivePlayer, Interrupted };
const char* KindName(Kind k) {
    switch (k) {
    case Kind::AggressivePair: return "aggressive-pair";
    case Kind::CalmPair: return "calm-pair";
    case Kind::AggressivePlayer: return "aggressive-player";
    case Kind::Interrupted: return "interrupted";
    }
    return "unknown";
}
struct Case { Kind kind; int seed; float step; std::string name; };

std::vector<Case> Schedule() {
    std::vector<Case> cases;
    const char* only = std::getenv("CJ_TEST_CASE");   // diagnostic narrowing only
    for (float step : { RENDER_DT, 1.0f / 20.0f })
        for (Kind kind : { Kind::AggressivePair, Kind::CalmPair, Kind::AggressivePlayer, Kind::Interrupted })
            for (int seed = 0; seed < SEEDS; seed++) {
                Case c{ kind, seed, step, std::string(KindName(kind)) + (step > 0.03f ? "-20hz" : "-60hz") + "-s" + std::to_string(seed) };
                if (!only || !*only || c.name.find(only) != std::string::npos) cases.push_back(c);
            }
    return cases;
}

int SkinOf(const char* cls) {
    VClass c = FindVehicleClass(cls);
    for (size_t i = 0; i < gAssets.vehicles.size(); i++)
        if (gAssets.vehicles[i].cls == c && gAssets.vehicles[i].spawnable) return (int)i;
    return -1;
}
} // namespace

struct TrafficIncidentTests::State {
    std::vector<Case> cases;
    std::vector<Vector2> lastPos;
    std::vector<uint32_t> serials;
    std::vector<float> rejoined;
    int index = 0, frame = 0, phaseFrame = 0, checks = 0, failures = 0, completed = 0, passedCases = 0, physicsSteps = 0;
    int rear = -1, hit = -1, parked = -1;
    int violations = 0, duplicates = 0, removed = 0, teleports = 0, nonfinite = 0;
    bool invalid = false, finished = false, nextCase = false, fired = false, braking = false;
    float phaseTime = 0, contactAt = -1, firstExit = -1, fightAt = -1, fightDistance = -1, successAt = -1;
    int lastFights = 0, lastExits = 0, punchesAtStart = 0;
    IncidentStats startStats;
    Vector2 centre{};
    std::string capture, lastResult;

    const Case& Current() const { return cases[index]; }
    IncidentStats Delta() const {
        IncidentStats s = IncidentGetStats(), d;
        d.started = s.started - startStats.started; d.exits = s.exits - startStats.exits;
        d.confrontations = s.confrontations - startStats.confrontations; d.fights = s.fights - startStats.fights;
        d.returns = s.returns - startStats.returns; d.noSafeExit = s.noSafeExit - startStats.noSafeExit;
        d.carLost = s.carLost - startStats.carLost; d.driverDead = s.driverDead - startStats.driverDead;
        d.interrupted = s.interrupted - startStats.interrupted; d.ignoredCalm = s.ignoredCalm - startStats.ignoredCalm;
        return d;
    }
    void Check(const char* name, float value, float low, float high) {
        bool pass = std::isfinite(value) && value >= low && value <= high;
        checks++;
        if (!pass) { failures++; lastResult = Current().name + ": " + name + " FAILED"; }
        TraceLog(LOG_INFO, "CJ016I metric fixture=%s case=%s name=%s value=%.6f min=%.6f max=%.6f result=%s",
                 FIXTURE_ID, Current().name.c_str(), name, value, low, high, pass ? "PASS" : "FAIL");
    }
    int Add(Game& g, const char* cls, Vector2 pos, DriverType driver) {
        Vehicle v; InitVehicle(v, SkinOf(cls), pos, 0);
        v.driver = driver; v.missionTarget = true;
        if (driver == DriverType::None) v.in.handbrake = true;
        g.vehicles.push_back(v);
        return (int)g.vehicles.size() - 1;
    }
    void Start(Game& g) {
        const Case& c = Current();
        g.vehicles.clear(); g.peds.clear(); g.pickups.clear();
        g.map.ResetTestGround(Tile::Road); g.map.RebuildTestIndex(); g.pedGrid.Build(g.peds);
        IncidentsReset();                          // cases are independent; nothing carries over
        g.physics = VehiclePhysics{};
        GRng().s = SEED + (uint32_t)c.seed * 7919u + (uint32_t)c.kind * 104729u;
        Rng rng(SEED ^ ((uint32_t)c.seed * 2654435761u + (uint32_t)c.kind * 40503u));
        phaseFrame = 0; phaseTime = 0; contactAt = firstExit = fightAt = fightDistance = successAt = -1;
        violations = duplicates = removed = teleports = nonfinite = 0;
        fired = braking = false;
        startStats = IncidentGetStats(); lastFights = startStats.fights; lastExits = startStats.exits;
        punchesAtStart = g.pedPunches;
        g.state = GameState::Playing; g.heat = 0; g.time = 0;
        g.player = PlayerState{}; g.player.health = 5000; g.player.inVehicle = false; g.player.vehicle = -1;
        Vector2 ic = g.map.InterCenter(3, 3);
        float lane = ic.x + cfg::LANE_OFFSET;
        float y0 = ic.y + 420;
        parked = Add(g, "Taxi", V2(lane, y0), DriverType::None);
        const char* hitClass = c.seed >= SEEDS / 2 ? "Bus" : "Taxi";
        float hitLength = Spec(FindVehicleClass(hitClass)).length, taxi = Spec(FindVehicleClass("Taxi")).length;
        float hy = y0 + taxi * 0.5f + 14 + hitLength * 0.5f;
        hit = Add(g, hitClass, V2(lane, hy), DriverType::Traffic);
        Vehicle& h = g.vehicles[hit];
        AIStartRail(g, h, 400); h.ai.speed = 0; h.ai.cruise = 200; h.ai.temper = 1;
        float gap = rng.Range(3.0f, 6.0f), speed = rng.Range(170.0f, 200.0f);
        float ry = hy + hitLength * 0.5f + gap + taxi * 0.5f;
        bool player = c.kind == Kind::AggressivePlayer;
        rear = Add(g, "Taxi", V2(lane + rng.Range(-3.0f, 3.0f), ry), player ? DriverType::Player : DriverType::Traffic);
        Vehicle& r = g.vehicles[rear];
        r.vel = V2(0, -speed); r.speedFwd = speed;
        if (player) {
            g.player.inVehicle = true; g.player.vehicle = rear;
        } else {
            r.ai.rail = false; r.ai.dynTimer = 0; r.recoveryTracked = true; r.ai.cruise = 200;
            AIResetPath(r, g.map);
        }
        DriverMood mood = c.kind == Kind::CalmPair ? DriverMood::Calm : DriverMood::Aggressive;
        h.ai.mood = (uint8_t)mood;
        if (!player) r.ai.mood = (uint8_t)mood;
        lastPos.clear(); serials.clear(); rejoined.assign(g.vehicles.size(), -1);
        for (const Vehicle& v : g.vehicles) { lastPos.push_back(v.pos); serials.push_back(v.serial); }
        centre = V2(ic.x, hy);
        g.player.pos = player ? r.pos : centre + V2(-3000, 0);
        g.cam.Snap(centre, 900);
        TraceLog(LOG_INFO, "CJ016I begin fixture=%s case=%s kind=%s seed_index=%d physics_dt=%.8f hit_class=%s gap_px=%.3f speed_px_s=%.3f",
                 FIXTURE_ID, c.name.c_str(), KindName(c.kind), c.seed, c.step, hitClass, gap, speed);
    }
    void Script(Game& g) {
        const Case& c = Current();
        if (c.kind == Kind::AggressivePlayer && g.player.inVehicle) {
            Vehicle& r = g.vehicles[rear];
            r.in = VehicleInput{};
            // After the contact the player stops: the arcade pedals brake in either gear.
            float forward = Dot(r.vel, r.Fwd());
            if (contactAt >= 0) {
                r.in.brake = forward > 5 ? 1.0f : 0.0f;
                r.in.throttle = forward < -5 ? 1.0f : 0.0f;
                r.in.handbrake = fabsf(forward) <= 5;
            }
            if (contactAt >= 0 && phaseTime >= contactAt + 1.0f && r.Speed() < 5) {
                g.ExitVehicle();
                g.player.vel = {};
                TraceLog(LOG_INFO, "CJ016I event case=%s t=%.3f player_exits", c.name.c_str(), phaseTime);
            }
        }
        if (c.kind == Kind::Interrupted && !fired) {
            bool fighting = false;
            for (const Pedestrian& p : g.peds) if (p.active && p.ownVehicle == rear && p.state == PedState::Fight) fighting = true;
            if (fighting || (firstExit >= 0 && phaseTime > firstExit + 12)) {
                fired = true;
                g.DamageVehicle(rear, 100000, false, g.vehicles[rear].pos);
                TraceLog(LOG_INFO, "CJ016I event case=%s t=%.3f first_car_catches_fire fighting=%d", c.name.c_str(), phaseTime, (int)fighting);
            }
        }
    }
    void Observe(Game& g, float step) {
        phaseTime += step;
        const Case& c = Current();
        for (const ImpactEvent& e : g.physics.events)
            if (e.kind == ContactKind::Vehicle && contactAt < 0 &&
                ((e.a == rear && e.b == hit) || (e.a == hit && e.b == rear))) {
                contactAt = phaseTime;
                TraceLog(LOG_INFO, "CJ016I event case=%s t=%.3f contact approach_px_s=%.3f dvA=%.3f dvB=%.3f", c.name.c_str(), phaseTime, e.approach, e.dvA, e.dvB);
            }
        for (size_t i = 0; i < g.vehicles.size(); i++) {
            const Vehicle& v = g.vehicles[i];
            if (!v.active || v.serial != serials[i]) { removed++; continue; }
            if (!std::isfinite(v.pos.x) || !std::isfinite(v.pos.y) || !std::isfinite(v.angle)) { nonfinite++; invalid = true; return; }
            if (Dist(lastPos[i], v.pos) > std::max(8.0f, v.Speed() * step + 4.0f)) teleports++;
            lastPos[i] = v.pos;
            if (v.ai.rail && v.driver == DriverType::Traffic && rejoined[i] < 0 && phaseTime > 0.5f &&
                ((int)i == rear || IncidentGetStats().returns > startStats.returns)) rejoined[i] = phaseTime;
            if (!v.ai.rail) rejoined[i] = -1;
            // Ownership: a car's driver on foot points back to the car; never both seated and out.
            int owners = 0;
            for (size_t k = 0; k < g.peds.size(); k++)
                if (g.peds[k].active && g.peds[k].ownVehicle == (int)i) owners++;
            if (owners > 1 || (owners == 1 && v.driver != DriverType::None)) duplicates++;
            if (v.ai.driverPed >= 0) {
                const Pedestrian* p = v.ai.driverPed < (int)g.peds.size() ? &g.peds[v.ai.driverPed] : nullptr;
                if (!p || !p->active || p->serial != v.ai.driverPedSerial || p->ownVehicle != (int)i ||
                    p->ownSerial != v.serial || v.driver != DriverType::None) violations++;
            }
        }
        for (size_t k = 0; k < g.peds.size(); k++) {
            const Pedestrian& p = g.peds[k];
            if (!p.active || p.ownVehicle < 0) continue;
            const Vehicle* v = p.ownVehicle < (int)g.vehicles.size() ? &g.vehicles[p.ownVehicle] : nullptr;
            if (!v || !v->active || v->serial != p.ownSerial || v->ai.driverPed != (int)k) violations++;
        }
        if ((int)(phaseTime / 0.5f) != (int)((phaseTime - step) / 0.5f))
            for (size_t k = 0; k < g.peds.size(); k++) {
                const Pedestrian& p = g.peds[k];
                if (!p.active) continue;
                TraceLog(LOG_INFO, "CJ016I state case=%s t=%.2f person=%d state=%d x=%.1f y=%.1f speed=%.1f health=%.0f own=%d foe=%d foe_player=%d foe_vehicle=%d",
                         c.name.c_str(), phaseTime, (int)k, (int)p.state, p.pos.x, p.pos.y, Len(p.vel), p.health, p.ownVehicle,
                         p.foe, (int)p.foePlayer, p.foeVehicle);
            }
        if ((int)(phaseTime / 0.5f) != (int)((phaseTime - step) / 0.5f))
            for (size_t i = 0; i < g.vehicles.size(); i++) {
                const Vehicle& v = g.vehicles[i];
                TraceLog(LOG_INFO, "CJ016I vehicle case=%s t=%.2f #%d driver=%d rail=%d x=%.1f y=%.1f ang=%.3f speed=%.1f reason=%s gear=%d cause=%s incident=%d driver_out=%d",
                         c.name.c_str(), phaseTime, (int)i, (int)v.driver, (int)v.ai.rail, v.pos.x, v.pos.y, v.angle, v.Speed(),
                         RecoveryReasonText(v.ai.recovery.reason), v.ai.recovery.gear, RejoinCauseText(v.ai.recovery.rejoinCause),
                         v.ai.incident, v.ai.driverPed);
            }
        IncidentStats d = Delta();
        if (d.exits > 0 && firstExit < 0) firstExit = phaseTime;
        if (d.fights > 0 && fightAt < 0) {
            fightAt = phaseTime;
            // Distance between the two people when the fight starts: they walked up first.
            for (const Pedestrian& p : g.peds) {
                if (!p.active || p.state != PedState::Fight || p.ownVehicle < 0) continue;
                Vector2 foe = g.player.pos;
                if (p.foe >= 0 && p.foe < (int)g.peds.size()) foe = g.peds[p.foe].pos;
                fightDistance = std::max(fightDistance, Dist(p.pos, foe));
            }
            TraceLog(LOG_INFO, "CJ016I event case=%s t=%.3f fight_starts distance_px=%.3f", c.name.c_str(), phaseTime, fightDistance);
        }
        // Done: no open incident, and every drivable traffic car has its driver and lane.
        bool settled = phaseTime > 3 && IncidentActiveCount() == 0;
        for (size_t i = 0; i < g.vehicles.size() && settled; i++) {
            const Vehicle& v = g.vehicles[i];
            if ((int)i == parked || !v.Drivable() || (c.kind == Kind::AggressivePlayer && (int)i == rear)) continue;
            if (v.driver != DriverType::Traffic || !v.ai.rail) settled = false;
        }
        if (settled && successAt < 0) successAt = phaseTime;
        if (!settled) successAt = -1;
    }
    void Finish(const Game& g) {
        int before = failures;
        const Case& c = Current();
        IncidentStats d = Delta();
        Check("vehicles_removed", (float)removed, 0, 0);
        Check("teleports", (float)teleports, 0, 0);
        Check("ownership_violations", (float)violations, 0, 0);
        Check("duplicate_drivers", (float)duplicates, 0, 0);
        Check("incidents_per_pair", (float)d.started, c.kind == Kind::CalmPair ? 0.0f : 1.0f, c.kind == Kind::CalmPair ? 0.0f : 1.0f);
        Check("contact_s", contactAt, 0, 1);
        Check("settled_s", successAt, 0, MAX_CASE_S);
        switch (c.kind) {
        case Kind::AggressivePair:
            Check("drivers_out", (float)d.exits, 2, 2);
            Check("fights", (float)d.fights, 1, 1);
            Check("exit_before_fight_s", fightAt - firstExit, 0.5f, MAX_CASE_S);
            Check("fight_start_distance_px", fightDistance, 0, 48);
            Check("back_in_own_car", (float)(d.returns + d.driverDead), 2, 2);
            break;
        case Kind::CalmPair:
            Check("drivers_out", (float)d.exits, 0, 0);
            Check("fights", (float)d.fights, 0, 0);
            Check("knocked_car_rejoined_s", rejoined[rear], 0, 30);
            break;
        case Kind::AggressivePlayer:
            Check("drivers_out", (float)d.exits, 1, 1);
            Check("confrontations", (float)d.confrontations, 1, 1);
            Check("fights", (float)d.fights, 1, 1);
            Check("punches_on_player", (float)(g.pedPunches - punchesAtStart), 1, 1000);
            Check("back_in_own_car", (float)d.returns, 1, 1);
            break;
        case Kind::Interrupted:
            Check("drivers_out", (float)d.exits, 2, 2);
            Check("car_lost", (float)d.carLost, 1, 1);
            Check("back_in_own_car", (float)(d.returns + d.driverDead), 1, 1);
            break;
        }
        bool pass = failures == before;
        if (pass) passedCases++;
        // raylib bounds each trace line: outcome and counts are logged separately.
        TraceLog(LOG_INFO, "CJ016I result fixture=%s case=%s duration_s=%.3f settled_s=%.3f checks_failed=%d result=%s",
                 FIXTURE_ID, c.name.c_str(), phaseTime, successAt, failures - before, pass ? "PASS" : "FAIL");
        TraceLog(LOG_INFO, "CJ016I diagnostics case=%s contact_s=%.3f first_exit_s=%.3f fight_s=%.3f fight_distance_px=%.3f started=%d exits=%d confrontations=%d fights=%d returns=%d car_lost=%d driver_dead=%d no_safe_exit=%d violations=%d duplicates=%d",
                 c.name.c_str(), contactAt, firstExit, fightAt, fightDistance, d.started, d.exits, d.confrontations, d.fights,
                 d.returns, d.carLost, d.driverDead, d.noSafeExit, violations, duplicates);
        completed++;
        capture = c.name;
    }
};

TrafficIncidentTests::TrafficIncidentTests() = default;
TrafficIncidentTests::~TrafficIncidentTests() = default;

bool TrafficIncidentTests::Init(Game& g) {
    state = std::make_unique<State>();
    State& s = *state;
    s.cases = Schedule();
    if (SkinOf("Taxi") < 0 || SkinOf("Bus") < 0 || s.cases.empty()) { s.invalid = true; TraceLog(LOG_ERROR, "CJ016I missing sprites or cases"); return false; }
    IncidentsReset();
    g.debugContacts = true;
    const char* revision = std::getenv("CJ_TEST_REVISION");
    TraceLog(LOG_INFO, "CJ016I run fixture=%s scenario=traffic-incident revision=%s seed=%08x planned_cases=%d render_dt=%.8f max_case_s=%.1f build=%s_%s",
             FIXTURE_ID, revision ? revision : "UNRECORDED", SEED, (int)s.cases.size(), RENDER_DT, MAX_CASE_S, __DATE__, __TIME__);
    for (size_t i = 0; i < s.cases.size(); i++)
        TraceLog(LOG_INFO, "CJ016I scheduled index=%d case=%s physics_dt=%.8f", (int)i + 1, s.cases[i].name.c_str(), s.cases[i].step);
    s.Start(g);
    return true;
}

void TrafficIncidentTests::Update(Game& g, float dt) {
    if (!state || state->invalid || state->finished) return;
    State& s = *state;
    if (!std::isfinite(dt) || fabsf(dt - RENDER_DT) > 1e-6f) {
        s.invalid = true; TraceLog(LOG_ERROR, "CJ016I requires a fixed 1/60 s render clock"); return;
    }
    if (s.nextCase) { s.index++; s.Start(g); s.nextCase = false; }
    const Case& c = s.Current();
    s.frame++;
    int divisor = c.step > 0.03f ? 3 : 1;
    if ((s.phaseFrame + 1) % divisor == 0) {
        // The game's own order: drivers decide on one snapshot, physics, impacts and
        // their consequences, incident rules, then the people on foot.
        g.time += c.step;
        s.Script(g);
        g.pedGrid.Build(g.peds);
        AIObserveTraffic(g);
        for (size_t i = 0; i < g.vehicles.size(); i++) {
            Vehicle& v = g.vehicles[i];
            v.kinFrom = v.pos; v.kinFromAng = v.angle;
            if (v.active && v.driver == DriverType::Traffic && v.Drivable()) AIUpdateTraffic(g, (int)i, c.step);
        }
        PhysicsStepOptions options; options.disableWorldEdges = true;
        g.physics.Step(g, c.step, options);
        g.HandleImpacts(c.step);
        IncidentsUpdate(g, c.step);
        g.pedGrid.Build(g.peds);
        for (Pedestrian& p : g.peds) if (p.active) UpdatePed(p, g, c.step);
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

void TrafficIncidentTests::Draw(const Game& g) const {
    if (!state) return;
    const State& s = *state;
    ClearBackground({ 20, 25, 30, 255 });
    if (s.invalid || g.vehicles.empty()) { DrawUIText("CJ-016 incident fixture invalid. See the log.", 30, 60, 26, RED); return; }
    Camera2D camera{}; camera.target = s.centre;
    camera.offset = V2(GetScreenWidth() * 0.5f, GetScreenHeight() * 0.56f); camera.zoom = 1.6f;
    BeginMode2D(camera);
    float tile = 16 * cfg::M;
    Texture2D ground = gAssets.asphalt;
    for (int y = -3; y <= 3; y++) for (int x = -4; x <= 4; x++)
        DrawTexturePro(ground, { 0, 0, (float)ground.width, (float)ground.height },
                       { s.centre.x + x * tile, s.centre.y + y * tile, tile, tile }, {}, 0, { 165, 175, 185, 255 });
    for (float y = -500; y < 500; y += 40) DrawLineEx(V2(s.centre.x, s.centre.y + y), V2(s.centre.x, s.centre.y + y + 22), 3, { 235, 235, 225, 200 });
    for (size_t i = 0; i < g.vehicles.size(); i++) {
        const Vehicle& v = g.vehicles[i];
        const VehicleSprite& sprite = gAssets.vehicles[v.skin];
        DrawTexturePro(sprite.tex, sprite.src, { v.pos.x + 4, v.pos.y + 5, v.width, v.length },
                       V2(v.width * 0.5f, v.length * 0.5f), v.angle * RAD2DEG, { 0, 0, 0, 120 });
        DrawTexturePro(sprite.tex, sprite.src, { v.pos.x, v.pos.y, v.width, v.length },
                       V2(v.width * 0.5f, v.length * 0.5f), v.angle * RAD2DEG, v.burning ? Color{ 255, 140, 90, 255 } : WHITE);
        const char* who = (int)i == s.parked ? "PARKED" : v.driver == DriverType::Player ? "PLAYER" :
            v.driver == DriverType::Traffic ? DriverMoodText((DriverMood)v.ai.mood) : v.burning ? "ON FIRE - EMPTY" : "EMPTY: DRIVER OUT";
        DrawText(who, (int)(v.pos.x + v.width * 0.5f + 6), (int)v.pos.y - 6, 10, LIGHTGRAY);
    }
    for (const Pedestrian& p : g.peds) {
        if (!p.active) continue;
        Color c = p.state == PedState::Fight ? Color{ 255, 90, 80, 255 } : p.state == PedState::Confront ? Color{ 255, 190, 70, 255 }
            : p.state == PedState::ToCar ? Color{ 110, 220, 160, 255 } : Color{ 200, 200, 210, 255 };
        if (p.state == PedState::Down || p.state == PedState::Dead) c = { 140, 60, 60, 255 };
        DrawCircleV(p.pos, PED_RADIUS, c);
        DrawLineEx(p.pos, p.pos + Forward(p.angle) * (PED_RADIUS + 6), 2, BLACK);
        const char* label = p.state == PedState::Fight ? "FIGHT" : p.state == PedState::Confront ? (p.argue > 0 ? "ARGUING" : "APPROACHING")
            : p.state == PedState::ToCar ? "BACK TO CAR" : p.state == PedState::Flee ? "BACKING OFF" : p.state == PedState::Down ? "DOWN" : "";
        DrawText(label, (int)p.pos.x - 20, (int)(p.pos.y + PED_RADIUS + 3), 10, c);
    }
    if (!g.player.inVehicle) {
        DrawCircleV(g.player.pos, PED_RADIUS, { 90, 170, 255, 255 });
        DrawText("PLAYER", (int)g.player.pos.x - 18, (int)(g.player.pos.y + PED_RADIUS + 3), 10, { 90, 170, 255, 255 });
    }
    EndMode2D();
    IncidentStats d = s.Delta();
    DrawRectangle(0, 0, GetScreenWidth(), 140, { 12, 17, 23, 240 });
    DrawUIText("CONCRETE JUNGLE / CJ-016 DRIVER INCIDENTS", 25, 14, 25, { 215, 231, 241, 255 }, true);
    DrawUIText(TextFormat("%s | case %d / %d | %.2f s | %.0f Hz physics", s.Current().name.c_str(), s.index + 1,
                         (int)s.cases.size(), s.phaseTime, 1.0f / s.Current().step), 25, 50, 22, WHITE);
    DrawUIText(TextFormat("out %d | confront %d | fights %d | back in car %d | car lost %d | checks %d / failures %d | passed %d / %d",
                         d.exits, d.confrontations, d.fights, d.returns, d.carLost, s.checks, s.failures, s.passedCases, s.completed),
               25, 86, 19, s.failures ? Color{ 255, 151, 115, 255 } : Color{ 116, 217, 165, 255 });
    DrawRectangle(0, GetScreenHeight() - 38, GetScreenWidth(), 38, { 12, 17, 23, 235 });
    DrawUIText(s.lastResult.empty() ? "Real rear-end contact; production traffic, incident and pedestrian AI. Drivers return to the same car."
                                  : s.lastResult.c_str(), 25, GetScreenHeight() - 29, 18, LIGHTGRAY);
}

void TrafficIncidentTests::Log() const {
    if (!state) return;
    const State& s = *state;
    TraceLog(LOG_INFO, "CJ016I summary fixture=%s seed=%08x scheduled=%d completed=%d passed_cases=%d render_frames=%d physics_steps=%d checks=%d failures=%d incomplete=%d invalid=%d result=%s",
             FIXTURE_ID, SEED, (int)s.cases.size(), s.completed, s.passedCases, s.frame, s.physicsSteps, s.checks, s.failures,
             (int)!s.finished, (int)s.invalid, s.finished && !s.invalid && s.failures == 0 ? "PASS" : "FAIL");
}
bool TrafficIncidentTests::Failed() const { return state && (state->invalid || state->failures > 0 || !state->finished); }
bool TrafficIncidentTests::Finished() const { return state && state->finished; }
const char* TrafficIncidentTests::CaptureLabel() const { return state && !state->capture.empty() ? state->capture.c_str() : nullptr; }
void TrafficIncidentTests::ClearCaptureRequest() { if (state) state->capture.clear(); }
