// =====================================================================================
//  Game rules & world simulation - see game.h. HUD / menus live in hud.cpp.
// =====================================================================================
#include "game.h"
#include "traffic_incidents.h"
#include "traffic.h"
#include "rlgl.h"
#include <algorithm>
#include <cstdio>

using namespace cfg;

Rng& GRng() { static Rng r(0xC0FFEEu); return r; }

// -------------------------------------------------------------------------------------
//  Setup
// -------------------------------------------------------------------------------------
void Game::Init() {
    map.Generate(20260926u);
    map.BuildMinimap();
    renderer.Init(GetScreenWidth(), GetScreenHeight());
    audio.Init();
    NewGame();
    state = GameState::Title;
}

// Test scenarios used by the --shot command line mode (automated screenshots).
void Game::DebugScenario(const char* name) {
    std::string n = name ? name : "foot";
    if (n == "title") return;
    state = GameState::Playing;
    showHelp = false;
    if (n == "drive" || n == "chase" || n == "nightdrive" || n == "crash" || n == "derby" || n == "rampage") {
        int best = -1; float bd = 1e9f;
        for (size_t k = 0; k < vehicles.size(); k++)
            if (vehicles[k].active && vehicles[k].driver == DriverType::Parked && Dist(vehicles[k].pos, player.pos) < bd) { bd = Dist(vehicles[k].pos, player.pos); best = (int)k; }
        if (best >= 0) { EnterVehicle(best); autoDrive = true; }
        if (n == "chase") heat = 3.0f;
        if (n == "crash") autoMode = 1;
        if (n == "derby") autoMode = 2;
        if (n == "rampage") { autoMode = 3; dn.hour = 13.0f; }
    }
    if (n == "overview") { debugOverview = true; dn.hour = 13.0f; }
    if (n == "brawl") { autoMode = 4; dn.hour = 13.0f; }
    if (n == "night" || n == "nightdrive") dn.hour = 22.5f;
    if (n == "day") dn.hour = 13.0f;
    cam.Snap(PlayerPos(), player.inVehicle ? CAM_VIEW_IDLE : CAM_VIEW_FOOT);
}

void Game::LogTrafficStats() const {
    RecoveryLogStats();
    IncidentLogStats();
    int n = 0, stopped = 0, blocked = 0; float speed = 0; int reasons[8] = {};
    for (const Vehicle& v : vehicles) {
        if (!v.active || v.driver != DriverType::Traffic || v.wrecked) continue;
        n++; speed += fabsf(v.speedFwd);
        if (fabsf(v.speedFwd) < 5) { stopped++; reasons[std::clamp(v.ai.reason, 0, 7)]++; }
        if (v.ai.blocked > 3.0f) blocked++;
    }
    TraceLog(LOG_INFO, "TRAFFIC: %d cars, avg %.0f km/h, %d stopped, %d blocked>3s, %.1f AI contacts/s",
             n, n ? speed / n / PX_PER_METER * 3.6f : 0.0f, stopped, blocked, aiContacts / std::max(1.0f, time));
    TraceLog(LOG_INFO, "TRAFFIC jolts (sudden velocity jumps): %.2f per second  [rail %d, knocked %d, police %d]", jolts / std::max(1.0f, time), joltRail, joltKnocked, joltPolice);
    for (size_t i = 0; i < vehicles.size(); i++) {
        const Vehicle& v = vehicles[i];
        if (!v.active || v.driver != DriverType::Traffic || v.ai.reason != 3) continue;
        const Vehicle* b = v.ai.blocker >= 0 ? &vehicles[v.ai.blocker] : nullptr;
        Vector2 ic = map.InterCenter(std::clamp((int)roundf((v.pos.x - TILE) / (BLOCK_PITCH * TILE)), 0, INTER_X - 1),
                                     std::clamp((int)roundf((v.pos.y - TILE) / (BLOCK_PITCH * TILE)), 0, INTER_Y - 1));
        TraceLog(LOG_INFO, "  BLOCKED #%d %s dir=%d ang=%.0f rel-inter=(%.0f,%.0f) blocked=%.1f | by #%d %s drv=%d reason=%d ang=%.0f spd=%.0f dist=%.0f",
                 (int)i, v.S().name.c_str(), v.ai.dir, v.angle * RAD2DEG, v.pos.x - ic.x, v.pos.y - ic.y, v.ai.blocked,
                 v.ai.blocker, b ? b->S().name.c_str() : "ped/player", b ? (int)b->driver : -1, b ? b->ai.reason : -1,
                 b ? b->angle * RAD2DEG : 0.0f, b ? b->speedFwd : 0.0f, b ? Dist(b->pos, v.pos) : 0.0f);
    }
    TraceLog(LOG_INFO, "TRAFFIC stopped because: moving %d, red light %d, queue %d, person/held %d, yield %d, static %d, junction box %d, giving way %d",
             reasons[0], reasons[1], reasons[2], reasons[3], reasons[4], reasons[5], reasons[6], reasons[7]);
    // Mutual pairs still stopped behind each other at the end are unresolved conflicts.
    int mutualPairs = 0, yielding = 0;
    for (size_t i = 0; i < vehicles.size(); i++) {
        const Vehicle& v = vehicles[i];
        if (!v.active || v.driver != DriverType::Traffic || v.wrecked) continue;
        if (v.ai.yieldTo >= 0) yielding++;
        int b = v.ai.rail ? v.ai.waitingOn : v.ai.recovery.gear == 0 ? v.ai.recovery.blockedBy : -1;
        if (b > (int)i && b < (int)vehicles.size()) {
            const Vehicle& o = vehicles[b];
            int back = o.ai.rail ? o.ai.waitingOn : o.ai.recovery.gear == 0 ? o.ai.recovery.blockedBy : -1;
            if (back == (int)i && v.ai.yieldTo < 0 && o.ai.yieldTo < 0) mutualPairs++;
        }
    }
    TraceLog(LOG_INFO, "TRAFFIC yielding: roles taken %d (chain %d), yielding at end %d, unresolved mutual pairs at end %d",
             statYields, statChainYields, yielding, mutualPairs);
}

// Scripted driving for the --shot physics tests.
//   crash: grind along a wall, back off, full-speed head-on hit, a building corner at
//          45 degrees, a lamp post, a hydrant, a steel bollard, then shove a row of
//          abandoned cars against the wall.
//   derby: full throttle through traffic with random steering, reversing when stuck.
//   rampage: straight 4 s runs at 50 km/h along the sidewalk walking lines of the blocks
//            around the start, each run from a fixed start (people have to dodge).
void Game::AutoPilot(Vehicle& v, VehicleInput& in, float dt) {
    autoT += dt;
    in = VehicleInput{};
    auto place = [&](Vector2 p, float ang) {
        v.pos = p; v.angle = ang; v.vel = { 0, 0 }; v.angVel = 0; v.steer = 0;
        v.health = v.S().health; v.burning = false; v.wrecked = false;
    };
    // nearest street object of a kind with a clear straight run-up from the road
    auto runUp = [&](CityObject::Kind kind, int sprite, float dist) {
        float bd = 1e9f; Vector2 bp{}; float ba = 0;
        for (const CityObject& o : map.objects) {
            if (!o.alive || o.kind != kind || (sprite >= 0 && o.sprite != sprite)) continue;
            float d = Dist(o.pos, v.pos);
            if (d > bd || d > 3000) continue;
            for (int k = 0; k < 16; k++) {
                float a = k * PI / 8;
                Vector2 p = o.pos + Forward(a) * dist;
                float t; Vector2 n;
                if (map.TileAt(p) != Tile::Road || map.RayCast(o.pos + Forward(a) * (o.radius + 3), p + Forward(a) * 40, t, n, nullptr)) continue;
                bd = d; bp = p; ba = a + PI;
                break;
            }
        }
        if (bd < 1e9f) place(bp, ba);
    };
    if (autoMode == 1) {
        if (testBuilding < 0) {                        // nearest big building with room in front
            float bd = 1e9f;
            for (size_t k = 0; k < map.buildings.size(); k++) {
                const Building& b = map.buildings[k];
                if (!b.Solid() || b.r.width < 14 * M || b.r.height < 14 * M) continue;
                Vector2 f = { b.r.x + b.r.width * 0.5f, b.r.y + b.r.height + 2 };
                float t; Vector2 n;
                if (map.RayCast(f, f + V2(0, 520), t, n, nullptr)) continue;
                float d = Dist(f, player.pos);
                if (d < bd) { bd = d; testBuilding = (int)k; }
            }
            if (testBuilding < 0) return;
        }
        const Rectangle& r = map.buildings[testBuilding].r;
        float y1 = r.y + r.height;
        static const float ends[] = { 3, 4.5f, 8, 11, 13.5f, 16, 18.5f, 1e9f };
        int phase = 0;
        while (autoT >= ends[phase]) phase++;
        bool enter = phase != autoPhase;
        autoPhase = phase;
        if (enter) TraceLog(LOG_INFO, "CRASH phase %d at t=%.1f", phase, autoT);
        switch (phase) {
        case 0:                                        // hit the wall at 35 deg, keep grinding along it
            if (enter) place({ r.x + r.width * 0.2f, y1 + 110 }, 35 * DEG2RAD);
            in.throttle = 1; in.steer = autoT < 1.2f ? 0.0f : 0.25f;
            break;
        case 1: in.brake = 1; in.steer = 0.8f; break;  // reverse away
        case 2:                                        // full-speed head-on hit, then keep pushing
            if (enter) place({ r.x + r.width * 0.6f, y1 + 500 }, 0);
            in.throttle = 1;
            break;
        case 3:                                        // building corner at 45 degrees
            if (enter) { Vector2 c = { r.x + r.width, y1 }; place(c + V2(200, 200), AngleOf(V2(-1, -1))); }
            in.throttle = 1; in.steer = autoT > 9.5f ? -1.0f : 0.0f;
            break;
        case 4:                                        // lamp post at ~60 km/h: breakaway, falls over
            if (enter) runUp(CityObject::Lamp, -1, 150);
            in.throttle = 0.7f;
            break;
        case 5:                                        // hydrant at ~45 km/h: shears off
            if (enter) runUp(CityObject::Prop, (int)spritegen::Prop::Hydrant, 110);
            in.throttle = 0.6f;
            break;
        case 6:                                        // steel bollard at ~40 km/h: stops the car
            if (enter) runUp(CityObject::Prop, (int)spritegen::Prop::Bollard, 100);
            in.throttle = 0.6f;
            break;
        case 7:                                        // push abandoned cars into the wall
            if (enter) {
                float x0 = r.x + r.width * 0.3f;
                for (int k = 0; k < 3; k++) {
                    int idx = SpawnVehicle(gAssets.RandomSkin(FindVehicleClass("Taxi")), { x0 + k * 84.0f, y1 }, PI * 0.5f, DriverType::None);
                    vehicles[idx].pos.y = y1 + vehicles[idx].width * 0.5f + 1;
                }
                place({ x0 + 84, y1 + 150 }, 0.12f);
            }
            in.throttle = 1; in.steer = 0.25f * sinf(autoT * 1.3f);
            break;
        }
        return;
    }
    if (autoMode == 3) {                               // sidewalk rampage
        const float RUN = 4.0f, SPEED = 50.0f / 3.6f * M;
        int run = (int)(autoT / RUN);
        static Vector2 lineP{}; static float lineA = 0;
        if (run != autoPhase) {
            autoPhase = run;
            // blocks around the centre in a fixed order; each run follows one side clockwise
            static const int order[9][2] = { { 4, 4 }, { 3, 4 }, { 5, 4 }, { 4, 3 }, { 4, 5 }, { 3, 3 }, { 5, 5 }, { 5, 3 }, { 3, 5 } };
            int bi = order[run % 9][0], bj = order[run % 9][1], side = (run / 9 + run) % 4;
            lineP = map.SidewalkCorner(bi, bj, side);
            lineA = side * PI * 0.5f + PI * 0.5f;      // corner 0 -> 1 heads east, 1 -> 2 south, ...
            place(lineP, lineA);
            v.vel = Forward(lineA) * SPEED;
            TraceLog(LOG_INFO, "RAMPAGE run %d block (%d,%d) side %d at t=%.1f", run, bi, bj, side, autoT);
        }
        Vector2 n = RightOf(lineA);
        float e = Dot(v.pos - lineP, n), he = WrapAngle(lineA - v.angle);
        in.steer = Clampf(-e * 0.03f + he * 2.5f, -1, 1);
        if (v.speedFwd < SPEED) in.throttle = 1; else in.brake = v.speedFwd > SPEED + 20 ? 0.3f : 0.0f;
        return;
    }
    if (autoMode == 2) {                               // demolition derby
        autoTimer -= dt;
        if (autoTimer <= 0) { autoTimer = GRng().Range(0.8f, 2.2f); autoSteer = GRng().Range(-1, 1); }
        if (autoReverse > 0) {
            autoReverse -= dt;
            in.brake = 1; in.steer = -autoSteer;
            return;
        }
        in.throttle = 1; in.steer = autoSteer; in.handbrake = GRng().Chance(0.01f);
        autoSlow = fabsf(v.speedFwd) < 25 ? autoSlow + dt : 0;
        if (autoSlow > 0.8f) { autoReverse = 1.0f; autoSlow = 0; }
    }
}

// --scenario brawl: walk up to the nearest person standing and punch them (fists).
bool Game::BrawlPilot(Vector2& move) {
    int best = -1; float bd = 30 * M;
    for (size_t k = 0; k < peds.size(); k++) {
        const Pedestrian& p = peds[k];
        if (!p.active || p.state == PedState::Down || p.state == PedState::Dead) continue;
        float d = Dist(p.pos, player.pos);
        if (p.state == PedState::Fight) d *= 0.3f;       // deal with whoever fights back first
        else if (p.state == PedState::Flee) d *= 3.0f;   // and do not chase runners
        if (d < bd) { bd = d; best = (int)k; }
    }
    move = { 0, 0 };
    if (best < 0) return false;
    Vector2 to = peds[best].pos - player.pos;
    player.aim = AngleOf(to);
    if (Len(to) > 1.0f * M) move = Norm(to);
    return Len(to) < 1.3f * M;
}

void Game::PhysDiagnostics(float dt) {
    if (diag.size() < vehicles.size()) diag.resize(vehicles.size());
    static std::vector<int> ids;
    for (size_t i = 0; i < vehicles.size(); i++) {
        Vehicle& v = vehicles[i];
        PhysDiag& d = diag[i];
        bool rail = v.active && AIOnRail(v);
        if (d.rail && !rail && v.active && v.driver == DriverType::Traffic) statKnocks++;
        d.rail = rail;
        if (!v.active || rail) { d.init = false; continue; }
        diagBodySeconds += dt;
        if (d.init) {
            Vector2 dp = v.pos - d.prevPos;
            float da = WrapAngle(v.angle - d.prevA);
            float l1 = Len(dp), l0 = Len(d.prevD);
            if (l1 > 0.15f && l0 > 0.15f && Dot(dp, d.prevD) < -0.5f * l1 * l0) diagPosFlips++;
            if (fabsf(da) > 0.003f && fabsf(d.prevDa) > 0.003f && da * d.prevDa < 0) diagAngFlips++;
            d.prevD = dp; d.prevDa = da;
        }
        d.prevPos = v.pos; d.prevA = v.angle; d.init = true;
        // penetration into the static world after the collision pass
        float r = v.length * 0.5f + 4, pen = 0;
        int what = -1;
        map.QueryBuildings({ v.pos.x - r, v.pos.y - r, r * 2, r * 2 }, ids);
        for (int k : ids) { Vector2 n; float depth; if (OBBOverlap(v.Box(), MakeAABB(map.buildings[k].r), n, depth) && depth > pen) { pen = depth; what = -2; } }
        map.QueryObjects({ v.pos.x - r, v.pos.y - r, r * 2, r * 2 }, ids);
        for (int k : ids) {
            Vector2 n; float depth;
            const CityObject& o = map.objects[k];
            bool hit = o.box ? OBBOverlap(v.Box(), o.Box(), n, depth) : CircleOBB(o.pos, o.radius, v.Box(), n, depth);
            if (!o.soft && hit && depth > pen) { pen = depth; what = k; }
        }
        diagMaxPen = std::max(diagMaxPen, pen);
        if (pen > 3.0f) {
            diagDeepPen++;
            static int logged = 0;
            if (logged < 25 && diagDeepPen % 20 == 1) {
                logged++;
                const CityObject* o = what >= 0 ? &map.objects[what] : nullptr;
                TraceLog(LOG_INFO, "DEEP #%d %s drv=%d pen=%.1f spd=%.0f vs %s kind=%d sprite=%d radius=%.1f t=%.2f",
                         (int)i, v.S().name.c_str(), (int)v.driver, pen, v.Speed(), what == -2 ? "building" : "object",
                         o ? (int)o->kind : -1, o ? o->sprite : -1, o ? o->radius : 0.0f, time);
            }
        }
    }
    // the player pressing on but not moving
    if (player.inVehicle && player.vehicle >= 0) {
        const Vehicle& v = vehicles[player.vehicle];
        bool pushing = v.in.throttle > 0.5f || v.in.brake > 0.5f;
        diagPlayerSlow = pushing && v.Speed() < 10 ? diagPlayerSlow + dt : 0;
        if (diagPlayerSlow > 2.5f) { diagStuck++; diagPlayerSlow = 0; }
    }
}

void Game::LogPhysStats() const {
    int knocked = 0, knockedLong = 0, abandoned = 0;
    for (const Vehicle& v : vehicles) {
        if (!v.active) continue;
        if (v.driver == DriverType::Traffic && !v.ai.rail) { knocked++; if (v.ai.dynTimer > 12) knockedLong++; }
        if (v.driver == DriverType::None) abandoned++;
    }
    float bs = std::max(1.0f, diagBodySeconds);
    TraceLog(LOG_INFO, "PHYS: %.0f body-seconds | jitter: pos flips %d (%.2f per body-s), heading flips %d (%.2f per body-s)",
             diagBodySeconds, diagPosFlips, diagPosFlips / bs, diagAngFlips, diagAngFlips / bs);
    TraceLog(LOG_INFO, "PHYS: penetration max %.1f px, frames > 3px: %d | player stuck events %d | knocked AI %d (>12s: %d), abandoned %d",
             diagMaxPen, diagDeepPen, diagStuck, knocked, knockedLong, abandoned);
    TraceLog(LOG_INFO, "PHYS: traffic knocked off the lane %d times, re-joined %d, drivers gave up %d", statKnocks, statRejoins, statAbandons);
    for (size_t i = 0; i < vehicles.size(); i++) {
        const Vehicle& v = vehicles[i];
        if (!v.active || v.driver != DriverType::Traffic || v.ai.rail || v.ai.dynTimer < 12) continue;
        TraceLog(LOG_INFO, "  LONG-KNOCKED #%d %s t=%.1f recover=%.1f spd=%.0f vF=%.0f hp=%.0f gear=%.1f jam=%.1f in(t%.1f b%.1f s%.1f h%d) onscreen=%d wrecked=%d burning=%d",
                 (int)i, v.S().name.c_str(), v.ai.dynTimer, v.ai.recover, v.Speed(), v.speedFwd, v.health, v.ai.gearTimer, v.ai.jammed,
                 v.in.throttle, v.in.brake, v.in.steer, (int)v.in.handbrake, (int)OnScreen(v.pos), (int)v.wrecked, (int)v.burning);
        const RecoveryState& recovery = v.ai.recovery;
        if (recovery.initialized) {
            float lateral = fabsf(Dot(v.pos - recovery.origin, Perp(recovery.forward)));
            float heading = fabsf(WrapAngle(v.angle - AngleOf(recovery.forward)));
            int ti = v.ai.ti, tj = v.ai.tj;
            if (lateral <= 4 && heading <= 0.12f) {
                // Mirror route assessment on a separate body; diagnostics must never
                // clear the actual driver's path or change its next junction.
                Vehicle route; route.pos = v.pos; route.angle = v.angle;
                AIResetPath(route, map); ti = route.ai.ti; tj = route.ai.tj;
            }
            float along = Dot(v.pos - map.InterCenter(ti, tj), recovery.forward);
            float entryLimit = -ROAD_HALF - std::max(v.length, 60.0f) - 10;
            const char* forecast = !recovery.rejoinForecastTested ? "not_tested"
                : recovery.rejoinForecastClear ? "clear" : "blocked";
            TraceLog(LOG_INFO, "  LONG-REJOIN #%d reason=%s tracking=%d lateral_px=%.3f heading_rad=%.4f lateral_speed_px_s=%.3f yaw_rad_s=%.4f forward_px_s=%.3f along_px=%.3f entry_limit_px=%.3f last_forecast=%s cause=%s",
                     (int)i, RecoveryReasonText(recovery.reason), (int)recovery.tracking, lateral, heading,
                     Dot(v.vel, Perp(recovery.forward)), v.angVel, Dot(v.vel, v.Fwd()), along, entryLimit, forecast,
                     recovery.rejoinForecastTested ? RejoinCauseText(recovery.rejoinCause) : "not_tested");
        }
    }
    if (player.inVehicle && player.vehicle >= 0) {
        const Vehicle& v = vehicles[player.vehicle];
        TraceLog(LOG_INFO, "PHYS: player car %s health %.0f/%.0f speed %.0f wrecked %d", v.S().name.c_str(), v.health, v.S().health, v.Speed(), (int)v.wrecked);
    }
}

// Pedestrian behaviour metrics for the --shot scenarios (see docs/testing.md).
void Game::PedDiagnostics(float dt) {
    PedDiag& d = pdiag;
    d.frames++;
    if (pedThreat.size() < peds.size()) pedThreat.resize(peds.size(), 0.0f);
    const Vehicle* pv = player.inVehicle && player.vehicle >= 0 ? &vehicles[player.vehicle] : nullptr;
    for (size_t k = 0; k < peds.size(); k++) {
        const Pedestrian& p = peds[k];
        if (!p.active) { pedThreat[k] = 0; continue; }
        bool lying = p.state == PedState::Down || p.state == PedState::Dead;
        if (p.state == PedState::Flee) d.flee += 1;
        if (IsDodging(p)) d.dodge += 1;
        if (p.state == PedState::Down && p.timer < -1.0f) d.downOverdue += dt;
        if (lying) { pedThreat[k] = 0; continue; }
        if (OnScreen(p.pos)) d.visible += 1;
        // sliding: moving (> 0.4 m/s) while the body faces more than 35 degrees away from the motion
        if (Len2(p.vel) > (0.4f * M) * (0.4f * M)) {
            d.moving += dt;
            if (fabsf(WrapAngle(AngleOf(p.vel) - p.angle)) > 35 * DEG2RAD) d.sliding += dt;
        }
        if (map.TileAt(p.pos) == Tile::Road) {
            int axis, ci, cj;
            if (!map.OnCrossing(p.pos, 16, &axis, &ci, &cj)) d.offCrossing += 1;
            else if (map.SignalState(ci, cj, 1 - axis) == SIG_GREEN) {                        // crossing traffic has green
                if (p.state == PedState::Cross && p.jaywalk) d.jaywalking += dt; else d.againstLights += dt;
            }
        }
        for (size_t o = k + 1; o < peds.size(); o++) {
            const Pedestrian& q = peds[o];
            if (q.active && q.state != PedState::Dead && q.state != PedState::Down && Len2(q.pos - p.pos) < (PED_RADIUS * 1.6f) * (PED_RADIUS * 1.6f)) d.overlaps += 1;
        }
        // rampage: standing in the straight path of the player's car, reached within 2 s
        pedThreat[k] = std::max(0.0f, pedThreat[k] - dt);
        if (pv && pedThreat[k] <= 0 && pv->Speed() > 60) {
            Vector2 f = Norm(pv->vel), rel = p.pos - pv->pos;
            float front = OBBProjectRadius(pv->Box(), f), halfW = OBBProjectRadius(pv->Box(), Perp(f));
            float along = Dot(rel, f), lat = fabsf(Cross(f, rel));
            if (along > 0 && along - front < pv->Speed() * 2.0f && lat < halfW + PED_RADIUS) { pedThreat[k] = 3.0f; d.threatened++; }
        }
    }
}

void Game::LogPedStats() const {
    const PedDiag& d = pdiag;
    float n = (float)std::max(1, d.frames), secs = n / 60.0f;
    int active = 0;
    for (const Pedestrian& p : peds) active += p.active;
    TraceLog(LOG_INFO, "PEDS: %d people | avg fleeing %.2f, dodging %.2f, on the road off a crossing %.2f, visible %.2f",
             active, d.flee / n, d.dodge / n, d.offCrossing / n, d.visible / n);
    TraceLog(LOG_INFO, "PEDS: overlaps %.2f per s | still on a crossing at the crossing traffic's green %.1f person-s, jaywalking %.1f person-s | down too long %.1f person-s | sliding %.1f %% of the time moving",
             d.overlaps / secs, d.againstLights, d.jaywalking, d.downOverdue, d.moving > 0 ? 100.0f * d.sliding / d.moving : 0.0f);
    TraceLog(LOG_INFO, "PEDS: hit by traffic %d, by the player %d | in the player's path %d, of them hit %d (escaped %.0f %%)",
             d.trafficHits, d.playerHits, d.threatened, d.threatHits,
             d.threatened ? 100.0f * (d.threatened - d.threatHits) / d.threatened : 0.0f);
    int byState[(int)PedState::Dead + 1] = {}, near30 = 0, near60 = 0, near110 = 0;
    static_assert((int)PedState::Dead == 12, "update the PEDS state log below");
    for (const Pedestrian& p : peds) {
        if (!p.active) continue;
        byState[(int)p.state]++;
        float dd = Dist(p.pos, PlayerPos());
        near30 += dd < 30 * M; near60 += dd < 60 * M; near110 += dd < 110 * M;
    }
    TraceLog(LOG_INFO, "PEDS at the end: walk %d wait %d cross %d idle %d wander %d flee %d rejoin %d dodge %d fight %d confront %d to-car %d down %d dead %d | within 30 m %d, 60 m %d, 110 m %d",
             byState[0], byState[1], byState[2], byState[3], byState[4], byState[5], byState[6], byState[7], byState[8], byState[9], byState[10],
             byState[11], byState[12], near30, near60, near110);
    TraceLog(LOG_INFO, "PEDS: fought back %d times, landed %d punches on the player (player health %.0f)", pedFights, pedPunches, player.health);
    float cf = (float)std::max(1, cpuFrames);
    TraceLog(LOG_INFO, "TIMING: CPU per frame - vehicles %.3f ms, pedestrians %.3f ms, world drawing %.3f ms",
             cpuVehicles * 1000.0 / cf, cpuPeds * 1000.0 / cf, cpuDraw * 1000.0 / cf);
}

void Game::Unload() {
    renderer.Unload();
    map.UnloadMinimap();
    audio.Unload();
}

int Game::SpawnVehicle(int skin, Vector2 pos, float angle, DriverType d) {
    int slot = -1;
    for (size_t i = 0; i < vehicles.size(); i++) if (!vehicles[i].active) { slot = (int)i; break; }
    if (slot < 0) { vehicles.push_back({}); slot = (int)vehicles.size() - 1; }
    Vehicle& v = vehicles[slot];
    InitVehicle(v, skin, pos, angle);
    v.driver = d;
    return slot;
}

int Game::SpawnPed(Vector2 pos, int skin, bool fleeing) {
    int slot = -1;
    for (size_t i = 0; i < peds.size(); i++) if (!peds[i].active) { slot = (int)i; break; }
    if (slot < 0) { peds.push_back({}); slot = (int)peds.size() - 1; }
    InitPed(peds[slot], pos, skin, map);
    peds[slot].pos = pos;
    if (fleeing) ScarePed(peds[slot], pos, 6.0f);
    return slot;
}

void Game::NewGame() {
    RecoveryResetStats();
    IncidentsReset();
    Rng& r = GRng();
    vehicles.clear(); vehicles.reserve(256);
    deathSpots.clear();
    peds.clear(); peds.reserve(400);
    pickups.clear(); toasts.clear();
    fx.Clear();
    money = 0; heat = 0; heatCooldown = 0; kills = 0; bustTimer = 0;
    mission = Mission{}; activePhone = -1; missionOffer = 4;
    dn.hour = START_HOUR;

    // ---- parked cars (police at the station, ambulances at the hospital) ----
    std::vector<ParkingSpot> spots = map.parking;
    for (size_t i = spots.size(); i > 1; i--) std::swap(spots[i - 1], spots[r.Int(0, (int)i - 1)]);
    int parked = 0;
    for (const ParkingSpot& s : spots) {
        if (parked >= PARKED_CARS) break;
        int bi = (int)(s.pos.x / (BLOCK_PITCH * TILE)), bj = (int)(s.pos.y / (BLOCK_PITCH * TILE));
        BlockType bt = map.Block(std::clamp(bi, 0, BLOCKS_X - 1), std::clamp(bj, 0, BLOCKS_Y - 1));
        int skin;
        if (bt == BlockType::Police) skin = gAssets.RandomSkinWithFlag(VF_POLICE);
        else if (bt == BlockType::Hospital) skin = gAssets.RandomSkin(FindVehicleClass("Ambulance"));
        else {
            if (!r.Chance(0.6f)) continue;
            skin = gAssets.RandomTrafficSkin();
            for (int k = 0; k < 8 && (Spec(gAssets.vehicles[skin].cls).large() || Spec(gAssets.vehicles[skin].cls).length > 5.6f * M); k++)
                skin = gAssets.RandomTrafficSkin();
            if (Spec(gAssets.vehicles[skin].cls).large()) continue;
        }
        if (skin < 0) continue;
        if (!map.AreaFree({ s.pos.x - 16, s.pos.y - 32, 32, 64 })) continue;
        SpawnVehicle(skin, s.pos, s.angle + r.Range(-0.04f, 0.04f), DriverType::Parked);
        parked++;
    }
    // ---- moving traffic ----
    for (int i = 0; i < TRAFFIC_CARS; i++) {
        int skin = gAssets.RandomTrafficSkin();
        int idx = SpawnVehicle(skin, { 0, 0 }, 0, DriverType::Traffic);
        if (!AIPlaceOnRoad(*this, vehicles[idx], { WORLD_W * 0.5f, WORLD_H * 0.5f }, 0, 1e9f, false)) vehicles[idx].active = false;
    }
    // ---- player: on foot at the central square, a sports car waiting at the curb ----
    player = PlayerState{};
    int cb = BLOCKS_X / 2, cbj = BLOCKS_Y / 2;
    Rectangle ring = map.SidewalkRing(cb, cbj);
    player.pos = { ring.x + ring.width * 0.45f, ring.y + ring.height };

    // ---- pedestrians: around the player ----
    for (int i = 0; i < PEDESTRIANS; i++) {
        Vector2 at = map.RandomSidewalkPointNear(r, player.pos, 0, PED_SPAWN_MAX);
        SpawnPed(Dist(at, player.pos) > 1.5f * M ? at : map.RandomSidewalkPointNear(r, player.pos, 6 * M, PED_SPAWN_MAX), r.Int(0, (int)gAssets.peds.size() - 1));
    }
    player.aim = player.feetAngle = 0;
    size_t nw = gAssets.weapons.size();
    player.clip.assign(nw, 0); player.ammo.assign(nw, 0); player.owned.assign(nw, 0);
    int fists = std::max(0, gAssets.FindWeapon("Fists"));
    player.owned[fists] = 1;
    int pistol = gAssets.FindWeapon("Pistol");
    if (pistol >= 0) { player.owned[pistol] = 1; player.clip[pistol] = gAssets.weapons[pistol].clip; player.ammo[pistol] = 36; }
    player.weapon = fists;
    int starter = gAssets.RandomSkin(FindVehicleClass("Stinger"));
    if (starter < 0) starter = gAssets.RandomTrafficSkin();
    {
        // park it on the central square, on a spot free of shrubs / furniture
        Vector2 spot = player.pos + V2(110, -80);
        for (int tries = 0; tries < 60; tries++) {
            Vector2 c = player.pos + V2(r.Range(-260, 260), r.Range(-200, -70));
            if (map.AreaFree({ c.x - 45, c.y - 22, 90, 44 })) { spot = c; break; }
        }
        SpawnVehicle(starter, spot, PI * 0.5f, DriverType::Parked);
    }

    // ---- pickups ----
    auto addPickup = [&](Vector2 p, int kind, int amount) { Pickup k; k.pos = p; k.kind = kind; k.amount = amount; pickups.push_back(k); };
    addPickup(map.hospital + V2(40, 0), 0, 60);
    addPickup(map.policeStation + V2(-40, 0), 1, 60);
    for (int i = 0; i < 24; i++) {
        Vector2 p = map.RandomSidewalkPoint(r);
        int kind = r.Int(0, 1 + (int)nw);
        if (kind >= 2 && gAssets.weapons[kind - 2].kind != WeaponKind::Melee && gAssets.weapons[kind - 2].startAmmo <= 0) kind = 0;
        int amount = kind < 2 ? 50 : std::max(1, gAssets.weapons[kind - 2].startAmmo / 2);
        addPickup(p, kind, amount);
    }
    // ---- mission phones: use real phone booths where possible ----
    jobPhones.clear();
    for (const CityObject& o : map.objects)
        if (o.kind == CityObject::Prop && o.sprite == (int)spritegen::Prop::PhoneBooth && r.Chance(0.3f) && jobPhones.size() < 6)
            jobPhones.push_back(o.pos + V2(0, 14));
    while (jobPhones.size() < 4) jobPhones.push_back(map.RandomSidewalkPoint(r));

    cam.Snap(player.pos, CAM_VIEW_FOOT);
}

// -------------------------------------------------------------------------------------
//  Queries & events
// -------------------------------------------------------------------------------------
Vector2 Game::PlayerPos() const { return player.inVehicle && player.vehicle >= 0 ? vehicles[player.vehicle].pos : player.pos; }
Vector2 Game::PlayerVel() const { return player.inVehicle && player.vehicle >= 0 ? vehicles[player.vehicle].vel : player.vel; }
bool Game::OnScreen(Vector2 p, float margin) const { return CheckCollisionPointRec(p, cam.VisibleGround(margin)); }

void Game::Toast_(const char* text, Color c) { toasts.push_back({ text, 4.0f, c }); if (toasts.size() > 5) toasts.erase(toasts.begin()); }
void Game::Big(const char* text, Color c, float dur) { bigText = text; bigColor = c; bigTimer = dur; }

void Game::Crime(float amount, Vector2 where) {
    int before = Stars();
    heat = std::min(6.0f, heat + amount);
    heatCooldown = 0;
    if (Stars() > before) Toast_(Stars() == 1 ? "The cops are after you!" : "Wanted level increased!", { 255, 90, 90, 255 });
    (void)where;
}

void Game::Scare(Vector2 where, float radius) {
    for (Pedestrian& p : peds)
        if (p.active && Len2(p.pos - where) < radius * radius) {
            if (p.state != PedState::Flee && GRng().Chance(0.05f)) audio.Play(Sfx::Scream, p.pos, 0.35f, GRng().Range(0.85f, 1.3f));
            AlarmPed(p, where, GRng().Range(4, 7), false);          // runs after their reaction time
        }
    for (Vehicle& v : vehicles)
        if (v.active && v.driver == DriverType::Traffic && Len2(v.pos - where) < radius * radius) v.ai.panic = 8;
}

void Game::DamagePlayer(float dmg, Vector2 dir) {
    if (state != GameState::Playing) return;
    float absorbed = std::min(player.armor, dmg * 0.7f);
    player.armor -= absorbed;
    player.health -= dmg - absorbed;
    player.hurtFlash = 1;
    if (!player.inVehicle && Len(dir) > 1) player.vel = player.vel + Norm(dir) * 120;
    if (player.health <= 0) {
        player.health = 0;
        state = GameState::Wasted; stateTimer = 0;
        Big("WASTED", { 220, 40, 40, 255 }, 5);
        audio.PlayUI(Sfx::Wasted, 0.9f);
        if (mission.type != MissionType::None) EndMission(false);
    }
}

void Game::DamageVehicle(int idx, float dmg, bool byPlayer, Vector2 at) {
    Vehicle& v = vehicles[idx];
    if (!v.active || v.wrecked) return;
    v.health -= dmg;
    v.damageFlash = 1;
    if (v.driver == DriverType::Traffic) v.ai.panic = 10;
    if (byPlayer) {
        v.hitByPlayer = true;
        if (v.S().police() && v.driver == DriverType::Police) Crime(0.25f, v.pos);
    }
    if (v.health <= 0 && !v.burning) {
        if (v.driver == DriverType::Traffic) v.recoveryTracked = true;
        v.burning = true;
        v.burnTimer = GRng().Range(3.0f, 5.0f);
        v.health = 0;
        if (v.driver == DriverType::Traffic || v.driver == DriverType::Police) {       // driver bails out
            SpawnPed(v.pos - RightOf(v.angle) * (v.width * 0.5f + 10), v.driverSkin, true);
            v.driver = DriverType::None;
        }
        if (player.inVehicle && player.vehicle == idx) Toast_("Your vehicle is on fire - get out! [E]", { 255, 160, 60, 255 });
    }
    (void)at;
}

void Game::ExplodeVehicle(int idx) {
    Vehicle& v = vehicles[idx];
    v.burning = false; v.wrecked = true; v.wreckTimer = 0; v.health = 0; v.siren = false;
    v.vel = v.vel * 0.3f + V2(GRng().Range(-40, 40), GRng().Range(-40, 40));
    v.angVel += GRng().Range(-2, 2);
    fx.Explosion(v.pos, v.S().large() ? 1.3f : 1.0f);
    audio.Play(Sfx::Explosion, v.pos, 1.0f, GRng().Range(0.85f, 1.05f));
    float d = Dist(v.pos, PlayerPos());
    cam.AddShake(Saturate(1.0f - d / 1400.0f) * 0.9f);
    bool playerInside = player.inVehicle && player.vehicle == idx;
    if (v.hitByPlayer || playerInside) { Crime(0.8f, v.pos); money += 150; }
    // blast damage & impulses
    const float R = 15 * M;
    for (size_t k = 0; k < vehicles.size(); k++) {
        Vehicle& o = vehicles[k];
        if (!o.active || (int)k == idx) continue;
        float dd = Dist(o.pos, v.pos);
        if (dd > R) continue;
        float f = 1.0f - dd / R;
        AIKnock(o);
        o.vel = o.vel + Norm(o.pos - v.pos) * (520 * f / o.S().mass);
        o.angVel += GRng().Range(-3, 3) * f;
        DamageVehicle((int)k, 140 * f, v.hitByPlayer || playerInside, o.pos);
    }
    for (size_t k = 0; k < peds.size(); k++) {
        Pedestrian& p = peds[k];
        if (!p.active) continue;
        float dd = Dist(p.pos, v.pos);
        if (dd < R) DamagePed((int)k, 120 * (1.0f - dd / R), p.pos - v.pos, v.hitByPlayer || playerInside, true);
    }
    if (playerInside) DamagePlayer(1000, { 0, 0 });
    else if (!player.inVehicle && d < R) DamagePlayer(90 * (1.0f - d / R), player.pos - v.pos);
    Scare(v.pos, 900);
}

void Game::DamagePed(int idx, float dmg, Vector2 dir, bool byPlayer, bool knockDown) {
    Pedestrian& p = peds[idx];
    if (!p.active || p.state == PedState::Dead) return;
    p.health -= dmg;
    Vector2 d = Norm(dir);
    fx.BloodSpray(p.pos, d, 6 + (int)(dmg * 0.2f));
    if (byPlayer) p.hitByPlayer = true;
    if (p.health <= 0 || knockDown || dmg > 40) {
        KnockDownPed(p, d * std::min(260.0f, 60 + dmg * 3));
        if (p.health <= 0) {
            p.state = PedState::Dead; p.deadTime = 0;
            fx.AddDecal(p.pos + d * 8, DECAL_BLOOD, 0.9f * M, GRng().Range(0, 6), 60);   // splash; the pool forms where the body stops
            deathSpots.push_back({ p.pos, time });
            if (deathSpots.size() > 32) deathSpots.erase(deathSpots.begin());
            if (byPlayer) { kills++; Crime(1.0f, p.pos); }
        } else if (byPlayer) Crime(0.35f, p.pos);
    } else {
        if (p.state != PedState::Fight) ScarePed(p, p.pos - d * 20, 6);   // a fighter keeps fighting (see ProvokePed)
        if (byPlayer) Crime(0.3f, p.pos);
    }
    if (GRng().Chance(0.5f)) audio.Play(Sfx::Scream, p.pos, 0.5f, GRng().Range(0.85f, 1.3f));
    Scare(p.pos, 260);
}

// -------------------------------------------------------------------------------------
//  Main update
// -------------------------------------------------------------------------------------
void Game::Update(float dt) {
    time += dt;
    if (IsKeyPressed(KEY_F1)) showHelp = !showHelp;
    if (IsKeyPressed(KEY_F3)) debug = !debug;

    switch (state) {
    case GameState::Title:
        dn.Update(dt);
        map.Update(dt, fx);
        UpdateVehicles(dt);
        UpdatePeds(dt);
        fx.Update(dt);
        cam.Update(player.pos + V2(sinf(time * 0.05f) * 600, cosf(time * 0.04f) * 400), CAM_VIEW_FAST * 0.9f, dt);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
            state = GameState::Playing;
            cam.Snap(player.pos, CAM_VIEW_FOOT);
            Toast_("Explore the city. Yellow marker = job phone.", { 255, 220, 120, 255 });
        }
        if (IsKeyPressed(KEY_ESCAPE)) quit = true;
        break;
    case GameState::Playing:
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) { state = GameState::Paused; break; }
        UpdatePlaying(dt);
        break;
    case GameState::Paused:
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ENTER)) state = GameState::Playing;
        if (IsKeyPressed(KEY_Q)) quit = true;
        audio.SetEngine(false, 0, 0, false); audio.SetSiren(0); audio.SetSkid(0);
        break;
    case GameState::Wasted:
    case GameState::Busted:
        stateTimer += dt;
        UpdatePlaying(dt * 0.35f);    // slow motion
        if (stateTimer > 4.0f) Respawn(state == GameState::Busted);
        break;
    }
    audio.Update(cam.pos);
    for (auto& t : toasts) t.time -= dt;
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [](const Toast& t) { return t.time <= 0; }), toasts.end());
    bigTimer = std::max(0.0f, bigTimer - dt);
}

void Game::UpdatePlaying(float dt) {
    dn.timeScale = IsKeyDown(KEY_T) ? 30.0f : 1.0f;
    dn.Update(dt);
    map.Update(dt, fx);
    if (state == GameState::Playing) {
        if (player.inVehicle) UpdatePlayerDriving(dt); else UpdatePlayerOnFoot(dt);
    }
    double t0 = GetTime();
    UpdateVehicles(dt);
    double t1 = GetTime();
    if (debugContacts) PhysDiagnostics(dt);
    IncidentsUpdate(*this, dt);                    // drivers stopping, getting out and back in
    double t2 = GetTime();
    UpdatePeds(dt);
    if (debugContacts) {
        cpuVehicles += t1 - t0; cpuPeds += GetTime() - t2; cpuFrames++;
        PedDiagnostics(dt);
    }
    UpdatePolice(dt);
    UpdateSpawning(dt);
    UpdateMission(dt);
    UpdatePickups(dt);
    fx.Update(dt);

    // wanted level cools down while the police can't see you
    heatCooldown += dt;
    if (heat > 0 && heatCooldown > 10.0f) {
        int before = Stars();
        heat = std::max(0.0f, heat - dt * 0.12f);
        if (Stars() < before) Toast_(heat <= 0 ? "You lost the cops." : "Wanted level decreased.", { 150, 200, 255, 255 });
    }

    // camera: look-ahead in the direction of travel, zoom out with speed (GTA-style)
    Vector2 target = PlayerPos(), vel = PlayerVel();
    float view = CAM_VIEW_FOOT;
    if (player.inVehicle && player.vehicle >= 0) {
        const Vehicle& v = vehicles[player.vehicle];
        view = Lerpf(CAM_VIEW_IDLE, CAM_VIEW_FAST, Saturate(v.Speed() / v.S().maxSpeed));
        if (v.S().large()) view *= 1.15f;
        target = target + vel * 0.45f;
    } else {
        Vector2 m = GetMousePosition();
        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        Vector2 off = { (m.x - sw * 0.5f) / sh * cam.viewH, (m.y - sh * 0.5f) / sh * cam.viewH };
        target = target + Vector2Clamp(off * 0.22f, V2(-90, -90), V2(90, 90));
    }
    if (debugOverview) { target = PlayerPos(); view = 260 * M; }
    cam.Update(target, view, dt);

    // audio for the player's vehicle, sirens nearby, tyre squeal
    if (player.inVehicle && player.vehicle >= 0) {
        const Vehicle& v = vehicles[player.vehicle];
        audio.SetEngine(v.Drivable(), v.rpm, v.in.throttle, v.S().large());
        audio.SetSkid(Saturate((v.slip - 120) / 350.0f) * (v.S().twoWheeler() ? 0.5f : 0.8f));
    } else { audio.SetEngine(false, 0, 0, false); audio.SetSkid(0); }
    float siren = 0;
    for (const Vehicle& v : vehicles)
        if (v.active && v.siren) siren = std::max(siren, Saturate(1.0f - Dist(v.pos, PlayerPos()) / 1500.0f));
    audio.SetSiren(siren * 0.55f);
    player.hurtFlash = std::max(0.0f, player.hurtFlash - dt * 2);
}

// -------------------------------------------------------------------------------------
//  Player on foot
// -------------------------------------------------------------------------------------
static const float PLAYER_RADIUS = 0.35f * M * CHAR_SCALE;

void Game::UpdatePlayerOnFoot(float dt) {
    PlayerState& P = player;
    Vector2 mv = { 0, 0 };
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) mv.y -= 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) mv.y += 1;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) mv.x -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) mv.x += 1;
    P.running = IsKeyDown(KEY_LEFT_SHIFT) || autoMode == 4;
    float speed = P.running ? 6.5f * M : 4.0f * M;
    if (P.reloadTimer > 0) speed *= 0.6f;
    bool autoAttack = autoMode == 4 && BrawlPilot(mv);
    Vector2 want = Len2(mv) > 0 ? Norm(mv) * speed : V2(0, 0);
    P.vel = LerpV(P.vel, want, Damp(12, dt));

    // GTA-style: the character turns towards where it walks; holding the right mouse
    // button (aim) or attacking turns it towards the cursor instead.
    static float aimHold = 0;
    bool aiming = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_LEFT) || aimHold > 0;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) aimHold = 0.8f; else aimHold = std::max(0.0f, aimHold - dt);
    float spd = Len(P.vel);
    if (autoMode == 4) {
        aiming = true;                                   // BrawlPilot has set the aim
    } else if (aiming) {
        Vector2 aimPt = cam.ScreenToGround(GetMousePosition(), H_PED * 0.8f);
        if (Dist(aimPt, P.pos) > 4) P.aim += WrapAngle(AngleOf(aimPt - P.pos) - P.aim) * Damp(25, dt);
    } else if (spd > 8) {
        P.aim += WrapAngle(AngleOf(P.vel) - P.aim) * Damp(12, dt);
    }
    P.aiming = aiming;
    // Legs and torso are separate layers: the torso always faces the aim, the legs
    // walk/run towards the movement direction, strafe sideways or backpedal.
    bool haveStrafe = gAssets.feet[(int)FeetAnim::StrafeLeft].frames > 0 && gAssets.feet[(int)FeetAnim::StrafeRight].frames > 0;
    if (spd > 8) {
        float mv = AngleOf(P.vel);
        float diff = WrapAngle(mv - P.aim), ad = fabsf(diff);
        float feetTarget = mv, dir = 1.0f, cycleM = P.running ? 2.6f : 1.5f;   // metres per 20-frame cycle
        if (ad < 0.75f) P.feetMode = P.running ? (int)FeetAnim::Run : (int)FeetAnim::Walk;
        else if (ad < 2.25f && haveStrafe && P.aiming) {
            P.feetMode = diff > 0 ? (int)FeetAnim::StrafeRight : (int)FeetAnim::StrafeLeft;
            feetTarget = P.aim; cycleM = 1.4f;
        } else if (P.aiming) { P.feetMode = (int)FeetAnim::Walk; feetTarget = mv + PI; dir = -1.0f; cycleM = 1.3f; }   // backpedal
        else P.feetMode = P.running ? (int)FeetAnim::Run : (int)FeetAnim::Walk;
        P.feetAngle += WrapAngle(feetTarget - P.feetAngle) * Damp(14, dt);
        P.feetAnim += dir * spd * dt * (20.0f / (cycleM * M));
    } else {
        P.feetMode = (int)FeetAnim::Idle;
        P.feetAngle += WrapAngle(P.aim - P.feetAngle) * Damp(8, dt);
    }

    P.pos = P.pos + P.vel * dt;
    // collisions: buildings, furniture, vehicles
    static std::vector<int> ids;
    Rectangle box = { P.pos.x - 30, P.pos.y - 30, 60, 60 };
    map.QueryBuildings(box, ids);
    for (int k : ids) { Vector2 n; float d; if (CircleOBB(P.pos, PLAYER_RADIUS, MakeAABB(map.buildings[k].r), n, d)) P.pos = P.pos + n * d; }
    map.QueryObjects(box, ids);
    for (int k : ids) {
        const CityObject& o = map.objects[k];
        if (o.walkIn) continue;
        if (o.box) { Vector2 n; float d; if (CircleOBB(P.pos, PLAYER_RADIUS, o.Box(), n, d)) P.pos = P.pos + n * d; continue; }
        Vector2 d = P.pos - o.pos; float l = Len(d), rr = o.radius + PLAYER_RADIUS;
        if (l < rr && l > 0.01f) P.pos = o.pos + d / l * rr;
    }
    for (Vehicle& v : vehicles) {
        if (!v.active || Len2(v.pos - P.pos) > 200 * 200) continue;
        Vector2 n; float d;
        if (CircleOBB(P.pos, PLAYER_RADIUS, v.Box(), n, d)) {
            float impact = Dot(v.vel, n * -1.0f);
            if (impact > 110 && v.driver != DriverType::Player) {
                DamagePlayer((impact - 80) * 0.12f, v.vel);
                audio.Play(Sfx::Punch, P.pos, 0.8f, 0.8f);
                fx.BloodSpray(P.pos, v.vel, 8);
            }
            P.pos = P.pos + n * d;
        }
    }
    P.pos.x = Clampf(P.pos.x, 8, WORLD_W - 8);
    P.pos.y = Clampf(P.pos.y, 8, WORLD_H - 8);

    // torso animation: synced with the legs while moving, slow breathing idle otherwise
    if (spd > 8) P.bodyAnim = fabsf(P.feetAnim); else P.bodyAnim += dt * 12.0f;
    static float stepT = 0;
    stepT += dt * spd / (P.running ? 60.0f : 45.0f);
    if (stepT > 1) { stepT = 0; audio.Play(Sfx::Footstep, P.pos, 0.25f, GRng().Range(0.8f, 1.2f)); }

    // ---- weapons ----
    int nw = (int)gAssets.weapons.size();
    for (int k = 0; k < std::min(9, nw); k++)
        if (IsKeyPressed(KEY_ONE + k) && P.owned[k]) { P.weapon = k; P.reloadTimer = 0; }
    float wheel = GetMouseWheelMove();
    if (IsKeyPressed(KEY_Q) || wheel != 0) {
        int dir = wheel < 0 ? -1 : 1;
        for (int k = 1; k <= nw; k++) {
            int c = ((P.weapon + dir * k) % nw + nw) % nw;
            if (P.owned[c]) { P.weapon = c; P.reloadTimer = 0; break; }
        }
    }
    const WeaponDef& W = gAssets.weapons[P.weapon];
    P.fireCooldown -= dt;
    P.actionTimer = std::max(0.0f, P.actionTimer - dt);
    if (P.reloadTimer > 0) {
        P.reloadTimer -= dt;
        if (P.reloadTimer <= 0) {
            int take = std::min(W.clip - P.clip[P.weapon], P.ammo[P.weapon]);
            P.clip[P.weapon] += take; P.ammo[P.weapon] -= take;
        }
    }
    bool wantFire = W.kind == WeaponKind::Auto ? IsMouseButtonDown(MOUSE_BUTTON_LEFT) : IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (W.kind == WeaponKind::Melee) wantFire = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (autoAttack) wantFire = true;
    if (wantFire && P.fireCooldown <= 0 && P.reloadTimer <= 0) {
        if (W.kind == WeaponKind::Melee) {
            MeleeHit();
            P.fireCooldown = 1.0f / W.fireRate;
            P.bodyState = BodyAnim::Melee; P.actionTimer = 0.45f;
        } else if (P.clip[P.weapon] > 0) {
            FireWeapon();
            P.clip[P.weapon]--;
            P.fireCooldown = 1.0f / W.fireRate;
            P.bodyState = BodyAnim::Shoot; P.actionTimer = 0.12f;
        } else if (P.ammo[P.weapon] > 0) {
            P.reloadTimer = W.reloadTime; P.bodyState = BodyAnim::Reload; P.actionTimer = W.reloadTime;
            audio.Play(Sfx::Reload, P.pos, 0.6f);
        } else {
            P.fireCooldown = 0.3f;
            audio.Play(Sfx::Reload, P.pos, 0.3f, 2.0f);         // empty click
        }
    }
    if (IsKeyPressed(KEY_R) && W.kind != WeaponKind::Melee && P.reloadTimer <= 0 && P.clip[P.weapon] < W.clip && P.ammo[P.weapon] > 0) {
        P.reloadTimer = W.reloadTime; P.bodyState = BodyAnim::Reload; P.actionTimer = W.reloadTime;
        audio.Play(Sfx::Reload, P.pos, 0.6f);
    }
    if (P.actionTimer <= 0) P.bodyState = spd > 8 ? BodyAnim::Move : BodyAnim::Idle;

    // ---- get into a vehicle ----
    if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_F)) {
        int best = -1; float bd = 3.8f * M;
        for (size_t k = 0; k < vehicles.size(); k++) {
            const Vehicle& v = vehicles[k];
            if (!v.Drivable()) continue;
            float d = Len(OBBClosestPoint(v.Box(), P.pos) - P.pos);
            if (d < bd) { bd = d; best = (int)k; }
        }
        if (best >= 0) EnterVehicle(best);
    }
}

void Game::FireWeapon() {
    const WeaponDef& W = gAssets.weapons[player.weapon];
    Vector2 f = Forward(player.aim), rt = RightOf(player.aim);
    Vector2 muzzle = player.pos + f * (0.85f * M) + rt * (0.12f * M);
    float h = H_PED * 0.8f;
    Rng& r = GRng();
    for (int k = 0; k < W.pellets; k++) {
        float a = player.aim + r.Range(-W.spreadDeg, W.spreadDeg) * DEG2RAD;
        Vector2 end = muzzle + Forward(a) * W.rangePx;
        float tBest = 1.0f; int hitType = 0, hitIdx = -1; Vector2 n{};
        float t; int obj;
        if (map.RayCast(muzzle, end, t, n, &obj) && t < tBest) { tBest = t; hitType = obj >= 0 ? 3 : 1; hitIdx = obj; }
        for (size_t i = 0; i < vehicles.size(); i++) {
            const Vehicle& v = vehicles[i];
            if (!v.active) continue;
            if (SegmentOBB(muzzle, end, v.Box(), &t) && t < tBest) { tBest = t; hitType = 2; hitIdx = (int)i; }
        }
        for (size_t i = 0; i < peds.size(); i++) {
            const Pedestrian& p = peds[i];
            if (!p.active || p.state == PedState::Dead) continue;
            if (SegmentCircle(muzzle, end, p.pos, PED_RADIUS + 1.5f, &t) && t < tBest) { tBest = t; hitType = 4; hitIdx = (int)i; }
        }
        Vector2 hp = LerpV(muzzle, end, tBest);
        fx.BulletTracer(muzzle, hp, h);
        switch (hitType) {
            case 1: fx.Sparks(hp, h * 0.6f, n, 4, 180); fx.Dust(hp, n * 40, { 170, 160, 150, 255 }); break;
            case 2: fx.Sparks(hp, 18, Forward(a) * -1.0f, 5, 220); DamageVehicle(hitIdx, W.damage * 0.55f, true, hp);
                    if (r.Chance(0.2f)) { fx.GlassShards(hp, 4); audio.Play(Sfx::Glass, hp, 0.4f); } break;
            case 3: map.BreakObject(hitIdx, Forward(a) * 300, fx); fx.Sparks(hp, 10, n, 3, 150); break;
            case 4: DamagePed(hitIdx, W.damage, Forward(a), true, false); break;
        }
    }
    fx.MuzzleFlash(muzzle, h, player.aim, W.pellets > 1 ? 30.0f : 20.0f);
    fx.ShellCasing(player.pos + rt * 6, h, player.aim);
    Sfx s = W.sound == "shotgun" ? Sfx::Shotgun : W.sound == "rifle" ? Sfx::Rifle : Sfx::Pistol;
    audio.Play(s, player.pos, 0.9f);
    cam.AddShake(W.pellets > 1 ? 0.28f : 0.1f);
    Scare(player.pos, 34 * M);
    if (heat < 1.0f) Crime(0.12f, player.pos);
}

void Game::MeleeHit() {
    const WeaponDef& W = gAssets.weapons[player.weapon];
    Vector2 f = Forward(player.aim);
    bool hit = false;
    for (size_t i = 0; i < peds.size(); i++) {
        Pedestrian& p = peds[i];
        if (!p.active || p.state == PedState::Dead) continue;
        Vector2 rel = p.pos - player.pos;
        float d = Len(rel);
        if (d > W.rangePx + PED_RADIUS + PLAYER_RADIUS || Dot(rel / std::max(d, 0.01f), f) < 0.45f) continue;
        bool wasFighting = p.state == PedState::Fight;
        DamagePed((int)i, W.damage, f, true, GRng().Chance(W.sound == "knife" ? 0.2f : 0.45f));
        ProvokePed(p, *this);
        if (!wasFighting && p.state == PedState::Fight) pedFights++;
        hit = true;
    }
    for (size_t i = 0; i < vehicles.size(); i++) {
        Vehicle& v = vehicles[i];
        if (!v.active) continue;
        if (Len(OBBClosestPoint(v.Box(), player.pos) - player.pos) < W.rangePx + PLAYER_RADIUS) {
            DamageVehicle((int)i, W.damage * 0.15f, true, player.pos);
            hit = true;
        }
    }
    audio.Play(W.sound == "knife" ? Sfx::Knife : Sfx::Punch, player.pos, hit ? 0.9f : 0.4f, hit ? 1.0f : 1.4f);
}

void Game::EnterVehicle(int idx) {
    Vehicle& v = vehicles[idx];
    if (v.driver == DriverType::Traffic || v.driver == DriverType::Police) {
        // carjacking: the driver is thrown out and runs away
        SpawnPed(v.pos - RightOf(v.angle) * (v.width * 0.5f + 12), v.driverSkin, true);
        Crime(v.driver == DriverType::Police ? 2.0f : 0.5f, v.pos);
        Toast_(v.driver == DriverType::Police ? "You stole a police car!" : "Carjacked!", { 255, 200, 120, 255 });
    }
    v.driver = DriverType::Player;
    v.ai = DriverAI{};
    v.siren = false;
    v.in = VehicleInput{};
    player.inVehicle = true;
    player.vehicle = idx;
    player.reloadTimer = 0;
    audio.Play(Sfx::Door, v.pos, 0.8f);
    char buf[64]; snprintf(buf, sizeof(buf), "%s", v.S().name.c_str());
    Toast_(buf, { 200, 220, 255, 255 });
}

void Game::ExitVehicle() {
    Vehicle& v = vehicles[player.vehicle];
    Vector2 side = RightOf(v.angle);
    Vector2 p = v.pos - side * (v.width * 0.5f + 12);
    if (map.PointInBuilding(p, 6)) p = v.pos + side * (v.width * 0.5f + 12);
    float speed = v.Speed();
    v.driver = DriverType::None;
    v.in = VehicleInput{};
    player.inVehicle = false;
    player.vehicle = -1;
    player.pos = p;
    player.vel = v.vel * 0.4f;
    player.aim = player.feetAngle = v.angle;
    audio.Play(Sfx::Door, p, 0.8f);
    if (speed > 250) { DamagePlayer((speed - 250) * 0.06f, v.vel); Toast_("Ouch! Jumped out at speed.", { 255, 180, 120, 255 }); }
}

// -------------------------------------------------------------------------------------
//  Player driving
// -------------------------------------------------------------------------------------
void Game::UpdatePlayerDriving(float dt) {
    Vehicle& v = vehicles[player.vehicle];
    if (!v.active) { player.inVehicle = false; player.vehicle = -1; return; }
    VehicleInput in;
    in.throttle = (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) ? 1.0f : 0.0f;
    in.brake = (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) ? 1.0f : 0.0f;
    in.steer = ((IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) ? 1.0f : 0.0f) - ((IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) ? 1.0f : 0.0f);
    in.handbrake = IsKeyDown(KEY_SPACE);
    if (autoDrive) { in.throttle = 0.8f; in.steer = sinf(time * 0.7f) * 0.25f; }
    if (autoMode) AutoPilot(v, in, dt);
    v.in = in;
    if (IsKeyPressed(KEY_L)) headlightMode = (headlightMode + 1) % 3;
    v.headlights = headlightMode == 1 || (headlightMode == 0 && dn.night > 0.3f);
    if (IsKeyPressed(KEY_H)) audio.Play(Sfx::Horn, v.pos, 0.9f);
    if (v.S().emergency() && IsKeyPressed(KEY_G)) v.siren = !v.siren;
    if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_F)) { ExitVehicle(); return; }
    player.pos = v.pos;
    (void)dt;
}

// -------------------------------------------------------------------------------------
//  Vehicles: AI, physics, fire, wreck clean-up
// -------------------------------------------------------------------------------------
void Game::UpdateVehicles(float dt) {
    double decisionStart = GetTime();
    // Resolve fire first: blasts and exiting occupants must be visible in the common
    // snapshot, rather than appearing halfway through another driver's decisions.
    for (size_t i = 0; i < vehicles.size(); i++) {
        Vehicle& v = vehicles[i];
        if (!v.active) continue;
        if (v.burning) {
            v.burnTimer -= dt;
            if (v.burnTimer <= 0) ExplodeVehicle((int)i);
        }
        if (v.wrecked) {
            v.wreckTimer += dt;
            if (!v.recoveryTracked && v.wreckTimer > 40 && !OnScreen(v.pos, 200) && !(player.inVehicle && player.vehicle == (int)i)) v.active = false;
        }
    }
    pedGrid.Build(peds);
    AIObserveTraffic(*this);
    // ---- drivers decide (rail traffic computes where it will be at the end of the frame) ----
    for (size_t i = 0; i < vehicles.size(); i++) {
        Vehicle& v = vehicles[i];
        if (!v.active) continue;
        v.kinFrom = v.pos; v.kinFromAng = v.angle;
        if (!v.wrecked && !v.burning) {
            if (v.driver == DriverType::Traffic) AIUpdateTraffic(*this, (int)i, dt);
            else if (v.driver == DriverType::Police) AIUpdatePolice(*this, (int)i, dt);
        }
        // placed somewhere else (spawn / recycle): no sweep through the city
        if (Len2(v.pos - v.kinFrom) > 40 * 40) { v.kinFrom = v.pos; v.kinFromAng = v.angle; }
    }
    // Includes vehicle preparation, the pedestrian grid and police decisions: an
    // upper bound on traffic cost, separate from physics, pedestrian AI and rendering.
    RecoveryRecordDecisionTime((GetTime() - decisionStart) * 1000.0);

    // ---- rigid-body step: forces, contacts, integration ----
    physics.Step(*this, dt);
    HandleImpacts(dt);
    VehiclePedCollisions();

    // ---- per-frame effects & diagnostics ----
    for (size_t i = 0; i < vehicles.size(); i++) {
        Vehicle& v = vehicles[i];
        if (!v.active) continue;
        if (AIOnRail(v)) UpdateVehicleEffects(v, fx, dt);
        else VehicleFrameEffects(v, map, fx, dt);
        if ((v.driver == DriverType::Traffic || v.driver == DriverType::Police) && dt > 0) {
            if (Len(v.vel - v.ai.lastVel) / dt > 2500.0f && Len(v.ai.lastVel) > 0) {
                jolts++;
                if (v.driver == DriverType::Police) joltPolice++; else if (AIOnRail(v)) joltRail++; else joltKnocked++;
                if (debugContacts && joltRail < 12 && AIOnRail(v))
                    TraceLog(LOG_INFO, "JOLT #%d %s v=(%.0f,%.0f)->(%.0f,%.0f) spd=%.0f s=%.1f path=%d shift=%.1f blend=%.2f reason=%d t=%.2f",
                             (int)i, v.S().name.c_str(), v.ai.lastVel.x, v.ai.lastVel.y, v.vel.x, v.vel.y, v.ai.speed, v.ai.s,
                             (int)v.ai.path.size(), v.ai.laneShift, v.ai.blend, v.ai.reason, time);
            }
            v.ai.lastVel = v.vel;
        }
    }
}

// Crash consequences. Damage follows the delta-V of the impact (the velocity change the
// car went through - the standard measure of crash severity), so hitting a parked car
// of the same weight at 60 km/h hurts like hitting a wall at 30, a truck barely
// notices a hatchback, and a hatchback is wrecked by a truck.
static const float DV_DAMAGE_FREE = 90.0f;     // px/s of delta-V a car shrugs off (~20 km/h)
static const float DV_DAMAGE_K    = 0.13f;     // health per px/s above that
static const float DV_INJURY      = 450.0f;    // ~100 km/h delta-V: the driver gets hurt too
static const float DV_BIKE_THROW  = 170.0f;    // motorbike riders come off above ~38 km/h

void Game::HandleImpacts(float dt) {
    int pv = player.inVehicle ? player.vehicle : -1;
    for (const ImpactEvent& e : physics.events) {
        Vehicle& a = vehicles[e.a];
        Vehicle* b = e.b >= 0 ? &vehicles[e.b] : nullptr;
        bool aPlayer = e.a == pv, bPlayer = e.b >= 0 && e.b == pv;
        bool involvesPlayer = aPlayer || bPlayer;

        // ---- street furniture ----
        if (e.kind == ContactKind::Object && e.obj >= 0) {
            const CityObject& o = map.objects[e.obj];
            if (o.soft) {                                      // pushing through a shrub
                if (e.scrape > 40 && GRng().Chance(dt * 25)) fx.Dust(e.point, a.vel * 0.3f, { 70, 110, 55, 255 });
                if (e.broke) audio.Play(Sfx::CrashSmall, e.point, 0.35f, 1.6f);
                continue;
            }
            if (e.broke) {
                bool pole = o.kind == CityObject::Lamp || o.kind == CityObject::Signal;
                audio.Play(pole ? Sfx::Crash : Sfx::CrashSmall, e.point, pole ? 0.7f : 0.5f, pole ? 1.25f : 1.3f);
                if (o.sprite == (int)spritegen::Prop::PhoneBooth || pole) audio.Play(Sfx::Glass, e.point, 0.5f);
                if (aPlayer) cam.AddShake(pole ? 0.25f : 0.08f);
            }
        }

        // ---- damage by delta-V ----
        if (e.dvA > DV_DAMAGE_FREE) DamageVehicle(e.a, (e.dvA - DV_DAMAGE_FREE) * DV_DAMAGE_K, bPlayer, e.point);
        if (b && e.dvB > DV_DAMAGE_FREE) DamageVehicle(e.b, (e.dvB - DV_DAMAGE_FREE) * DV_DAMAGE_K, aPlayer, e.point);

        // ---- occupants ----
        if (aPlayer && e.dvA > DV_INJURY) DamagePlayer((e.dvA - DV_INJURY) * 0.05f, { 0, 0 });
        if (bPlayer && e.dvB > DV_INJURY) DamagePlayer((e.dvB - DV_INJURY) * 0.05f, { 0, 0 });
        if (a.S().twoWheeler() && a.driver != DriverType::None && e.dvA > DV_BIKE_THROW) ThrowRider(e.a, e.dvA);
        if (b && b->S().twoWheeler() && b->driver != DriverType::None && e.dvB > DV_BIKE_THROW) ThrowRider(e.b, e.dvB);

        // ---- impact effects scale with the closing speed ----
        float impact = e.approach;
        if (impact > 90 && (e.dvA > 25 || e.dvB > 25)) {
            if (impact > 160) fx.Sparks(e.point, 14, e.normal * -1.0f, 6 + (int)(impact / 80), 200 + impact * 0.4f);
            if (impact > 330) {
                fx.GlassShards(e.point, 8);
                fx.Debris(e.point, 12, gAssets.vehicles[a.skin].paint, 4, impact * 0.5f);
            }
            audio.Play(impact > 260 ? Sfx::Crash : Sfx::CrashSmall, e.point, Saturate(impact / 500.0f), GRng().Range(0.85f, 1.1f));
            if (involvesPlayer) cam.AddShake(Saturate(impact / 900.0f) * 0.6f);
            if (b && b->driver == DriverType::Traffic && aPlayer) b->ai.panic = 6;
            if (a.driver == DriverType::Traffic && bPlayer) a.ai.panic = 6;
        }
        // ---- grinding along a wall or another car: sparks ----
        if (e.scrape > 90 && e.kind != ContactKind::Object) {
            Vector2 t = Perp(e.normal);
            if (GRng().Chance(dt * 28)) fx.Sparks(e.point, 10, (t * (GRng().Chance(0.5f) ? 1.0f : -1.0f) - e.normal) * 0.7f, 2, 140 + e.scrape * 0.3f);
            if (involvesPlayer && GRng().Chance(dt * 5)) audio.Play(Sfx::CrashSmall, e.point, 0.18f, GRng().Range(1.5f, 1.9f));
        }

        // ---- the drivers' reaction: an aggressive one may get out (traffic_incidents) ----
        if (b) IncidentOnImpact(*this, e);

        // ---- diagnostics ----
        if (b && (a.driver == DriverType::Traffic || a.driver == DriverType::Police) &&
            (b->driver == DriverType::Traffic || b->driver == DriverType::Police) && impact > 5) aiContacts++;
        if (debugContacts && (impact > 120 || e.broke) && (involvesPlayer || e.broke))
            TraceLog(LOG_INFO, "IMPACT t=%.2f kind=%d obj=%d %s approach=%.0f dvA=%.0f dvB=%.0f broke=%d | A %s hp %.0f vel %.0f",
                     time, (int)e.kind, e.obj, b ? b->S().name.c_str() : "-", impact, e.dvA, e.dvB, (int)e.broke,
                     a.S().name.c_str(), a.health, a.Speed());
    }
}

// Motorbike crash: the rider is thrown off over the bars.
void Game::ThrowRider(int idx, float severity) {
    Vehicle& v = vehicles[idx];
    Vector2 dir = Len(v.vel) > 20 ? Norm(v.vel) : v.Fwd();
    Vector2 p = v.pos + dir * (v.length * 0.5f + 14);
    if (map.PointInBuilding(p, 6)) p = v.pos - RightOf(v.angle) * (v.width * 0.5f + 14);
    if (player.inVehicle && player.vehicle == idx) {
        v.driver = DriverType::None;
        v.in = VehicleInput{};
        player.inVehicle = false;
        player.vehicle = -1;
        player.pos = p;
        player.aim = player.feetAngle = AngleOf(dir);
        player.vel = dir * std::min(260.0f, severity * 0.5f);
        DamagePlayer(std::min(60.0f, (severity - DV_BIKE_THROW) * 0.08f + 5), dir);
        Toast_("Thrown off the bike!", { 255, 180, 120, 255 });
        cam.AddShake(0.3f);
        return;
    }
    if (v.driver == DriverType::Traffic || v.driver == DriverType::Police) {
        int k = SpawnPed(p, v.driverSkin, false);
        DamagePed(k, std::min(90.0f, (severity - DV_BIKE_THROW) * 0.15f + 10), dir, v.hitByPlayer, true);
        v.driver = DriverType::None;
        v.in = VehicleInput{};
    }
}

void Game::VehiclePedCollisions() {
    int pv = player.inVehicle ? player.vehicle : -1;
    for (size_t i = 0; i < vehicles.size(); i++) {
        Vehicle& v = vehicles[i];
        if (!v.active) continue;
        float r = v.length * 0.5f + 4;
        float sp = v.Speed();
        static std::vector<int> near;
        near.clear();
        pedGrid.Query(v.pos, r + 20, [&](int k) { near.push_back(k); });
        for (int k : near) {
            Pedestrian& p = peds[k];
            if (!p.active || Len2(p.pos - v.pos) > r * r + 400) continue;
            Vector2 n; float depth;
            if (!CircleOBB(p.pos, PED_RADIUS, v.Box(), n, depth)) continue;
            if (p.state == PedState::Dead || p.state == PedState::Down) {
                // the wheels go over someone on the ground: they are not shoved along
                if (sp > 40 && p.runOverT <= 0) {
                    p.runOverT = 0.8f;
                    fx.BloodSpray(p.pos, Norm(v.vel), 8);
                    fx.AddDecal(p.pos + Norm(v.vel) * 10, DECAL_BLOOD, 1.1f * M, GRng().Range(0, 6), 75);
                    audio.Play(Sfx::Punch, p.pos, 0.6f, 0.55f);
                    if (p.state == PedState::Down) DamagePed((int)k, 40 + sp * 0.2f, v.vel, (int)i == pv, true);
                    if ((int)i == pv) cam.AddShake(0.08f);
                }
                continue;
            }
            if (sp > 95) {
                if (debugContacts) {
                    if ((int)i == pv) pdiag.playerHits++; else pdiag.trafficHits++;
                    if (k < (int)pedThreat.size() && pedThreat[k] > 0) { pdiag.threatHits++; pedThreat[k] = 0; }
                }
                // injury grows with the impact energy: about half die at 36 km/h, nearly all above 45 km/h
                float dmg = 100.0f * (sp / 160.0f) * (sp / 160.0f) * GRng().Range(0.8f, 1.25f);
                DamagePed((int)k, dmg, v.vel, (int)i == pv, true);
                p.vel = v.vel * 0.45f + n * 50;                 // thrown a couple of metres, then lies there
                p.runOverT = 0.5f;
                if (v.driver == DriverType::Traffic && GRng().Chance(0.6f)) v.ai.panic = 10;   // hit and run
                audio.Play(Sfx::Punch, p.pos, 0.8f, 0.7f);
                v.vel = v.vel * 0.95f;
                if ((int)i == pv) cam.AddShake(0.12f);
                continue;
            }
            p.pos = p.pos + n * depth;
        }
    }
}

// -------------------------------------------------------------------------------------
//  Pedestrians, police, population management
// -------------------------------------------------------------------------------------
void Game::UpdatePeds(float dt) {
    pedGrid.Build(peds);
    for (Pedestrian& p : peds) if (p.active) UpdatePed(p, *this, dt);
}

void Game::UpdatePolice(float dt) {
    int stars = Stars();
    int want = stars == 0 ? 0 : std::min(MAX_POLICE, stars + 1);
    int chasing = 0;
    for (Vehicle& v : vehicles) if (v.active && v.driver == DriverType::Police && !v.wrecked) chasing++;
    policeTimer -= dt;
    if (chasing < want && policeTimer <= 0 && state == GameState::Playing) {
        int skin = gAssets.RandomSkinWithFlag(VF_POLICE);
        int idx = SpawnVehicle(skin, { 0, 0 }, 0, DriverType::Police);
        if (AIPlaceOnRoad(*this, vehicles[idx], PlayerPos(), 900, 1600, true)) {
            vehicles[idx].ai.cruise = vehicles[idx].S().maxSpeed * 0.7f;
            vehicles[idx].siren = true;
        } else vehicles[idx].active = false;
        policeTimer = 2.5f;
    }
    // arrests: a slow police car right next to a slow player
    bool closeCop = false;
    for (const Vehicle& v : vehicles) {
        if (!v.active || v.driver != DriverType::Police || v.wrecked) continue;
        float d = Dist(v.pos, PlayerPos());
        float reach = player.inVehicle ? (v.length + vehicles[player.vehicle].length) * 0.5f + 12 : v.length * 0.5f + 40;
        if (d < reach && v.Speed() < 90) closeCop = true;
    }
    float playerSpeed = Len(PlayerVel());
    if (stars > 0 && closeCop && playerSpeed < (player.inVehicle ? 45.0f : 60.0f) && state == GameState::Playing) {
        bustTimer += dt;
        if (bustTimer > (player.inVehicle ? 2.5f : 1.2f)) {
            state = GameState::Busted; stateTimer = 0;
            Big("BUSTED", { 90, 150, 255, 255 }, 5);
            audio.PlayUI(Sfx::MissionFail, 0.9f);
            if (mission.type != MissionType::None) EndMission(false);
        }
    } else bustTimer = std::max(0.0f, bustTimer - dt);
}

void Game::UpdateSpawning(float dt) {
    (void)dt;
    Vector2 pp = PlayerPos();
    Rng& r = GRng();
    int traffic = 0;
    for (size_t i = 0; i < vehicles.size(); i++) {
        Vehicle& v = vehicles[i];
        if (!v.active) continue;
        if (v.driver == DriverType::Traffic) {
            traffic++;
            // recycle cars far from the player so the city around you stays busy
            if (AIOnRail(v) && !v.recoveryTracked && v.ai.blocked <= 0 && v.ai.yieldTo < 0 && !v.missionTarget && Dist(v.pos, pp) > 3400 && !OnScreen(v.pos, 200)) {
                int skin = gAssets.RandomTrafficSkin();
                InitVehicle(v, skin, v.pos, v.angle);
                v.driver = DriverType::Traffic;
                if (!AIPlaceOnRoad(*this, v, pp, 900, 2600, true)) v.active = false;
            }
        }
        if (v.driver == DriverType::Police && heat <= 0 && Dist(v.pos, pp) > 2200 && !OnScreen(v.pos, 200)) v.active = false;
        if (v.driver == DriverType::None && !v.recoveryTracked && !v.missionTarget && Dist(v.pos, pp) > 2600 && !OnScreen(v.pos, 200)) v.active = false;
    }
    if (traffic < TRAFFIC_CARS) {
        int idx = SpawnVehicle(gAssets.RandomTrafficSkin(), { 0, 0 }, 0, DriverType::Traffic);
        if (!AIPlaceOnRoad(*this, vehicles[idx], pp, 900, 2600, true)) vehicles[idx].active = false;
    }
    for (Pedestrian& p : peds) {
        if (!p.active) continue;
        if (p.ownVehicle >= 0 && p.state != PedState::Dead) continue;      // a driver on foot keeps their identity
        bool dead = p.state == PedState::Dead;
        bool far = Dist(p.pos, pp) > PED_KEEP_RADIUS;
        bool gone = dead && p.deadTime > PED_BODY_GONE;                  // the body has faded out
        if (gone || (far && !OnScreen(p.pos, 150))) {
            for (int tries = 0; tries < 10; tries++) {
                Vector2 np = map.RandomSidewalkPointNear(r, pp, PED_SPAWN_MIN, PED_SPAWN_MAX);
                float d = Dist(np, pp);
                if (d <= PED_SPAWN_MIN || d >= PED_SPAWN_MAX || OnScreen(np, 100)) continue;
                bool nearDeath = false;
                for (const DeathSpot& ds : deathSpots) if (time - ds.time < 90 && Dist(ds.pos, np) < 20 * M) nearDeath = true;
                if (nearDeath) continue;
                InitPed(p, np, r.Int(0, (int)gAssets.peds.size() - 1), map);
                break;
            }
            if (gone && p.state == PedState::Dead) p.active = false;      // no place found: just remove the body
        }
    }
    // keep the population up (removed bodies): one newcomer per frame at most
    int alive = 0;
    for (const Pedestrian& p : peds) alive += p.active;
    if (alive < PEDESTRIANS) {
        Vector2 np = map.RandomSidewalkPointNear(r, pp, PED_SPAWN_MIN, PED_SPAWN_MAX);
        float d = Dist(np, pp);
        if (d > PED_SPAWN_MIN && d < PED_SPAWN_MAX && !OnScreen(np, 100)) SpawnPed(np, r.Int(0, (int)gAssets.peds.size() - 1));
    }
}

// -------------------------------------------------------------------------------------
//  Pickups
// -------------------------------------------------------------------------------------
void Game::UpdatePickups(float dt) {
    for (Pickup& k : pickups) {
        if (!k.active) { k.respawn -= dt; if (k.respawn <= 0) k.active = true; continue; }
        if (player.inVehicle || Dist(k.pos, player.pos) > 1.2f * M) continue;
        char buf[96];
        if (k.kind == 0) { if (player.health >= 100) continue; player.health = std::min(100.0f, player.health + k.amount); snprintf(buf, sizeof(buf), "+%d health", k.amount); }
        else if (k.kind == 1) { if (player.armor >= 100) continue; player.armor = std::min(100.0f, player.armor + k.amount); snprintf(buf, sizeof(buf), "+%d armor", k.amount); }
        else {
            int w = k.kind - 2;
            const WeaponDef& W = gAssets.weapons[w];
            bool had = player.owned[w];
            player.owned[w] = 1;
            if (W.kind != WeaponKind::Melee) {
                if (!had) { player.clip[w] = std::min(W.clip, k.amount); player.ammo[w] += std::max(0, k.amount - W.clip); }
                else player.ammo[w] += k.amount;
            }
            if (!had) player.weapon = w;
            snprintf(buf, sizeof(buf), had ? "%s ammo" : "Picked up: %s", W.name.c_str());
        }
        Toast_(buf, { 160, 255, 160, 255 });
        audio.PlayUI(Sfx::Pickup, 0.7f);
        k.active = false; k.respawn = 60;
    }
}

// -------------------------------------------------------------------------------------
//  Missions
// -------------------------------------------------------------------------------------
void Game::StartMission(int phone) {
    Rng& r = GRng();
    mission = Mission{};
    Vector2 pp = PlayerPos();
    int kind = r.Int(0, 2);
    if (kind == 1) {                                   // demolition: pick a traffic car
        int best = -1;
        for (int tries = 0; tries < 60 && best < 0; tries++) {
            int k = r.Int(0, (int)vehicles.size() - 1);
            const Vehicle& v = vehicles[k];
            float d = Dist(v.pos, pp);
            if (v.active && v.driver == DriverType::Traffic && d > 1200 && d < 4000) best = k;
        }
        if (best < 0) kind = 0;
        else {
            mission.type = MissionType::Demolition;
            mission.targetVehicle = best;
            vehicles[best].missionTarget = true;
            mission.timer = 150; mission.reward = 1200;
            mission.title = "Scrap Metal";
            mission.objective = "Destroy the marked " + vehicles[best].S().name + ".";
        }
    }
    if (kind == 2) {                                   // steal a parked car and deliver it
        int best = -1;
        for (int tries = 0; tries < 80 && best < 0; tries++) {
            int k = r.Int(0, (int)vehicles.size() - 1);
            const Vehicle& v = vehicles[k];
            float d = Dist(v.pos, pp);
            if (v.active && v.driver == DriverType::Parked && !v.S().police() && d > 900 && d < 4500) best = k;
        }
        if (best < 0) kind = 0;
        else {
            mission.type = MissionType::Steal;
            mission.targetVehicle = best;
            vehicles[best].missionTarget = true;
            mission.timer = 200; mission.reward = 1600;
            mission.title = "Special Delivery";
            mission.objective = "Steal the marked " + vehicles[best].S().name + ".";
            for (int tries = 0; tries < 50; tries++) {
                Vector2 g = map.RandomSidewalkPoint(r);
                if (Dist(g, vehicles[best].pos) > 2500) { mission.target = g; break; }
            }
        }
    }
    if (kind == 0) {
        mission.type = MissionType::Courier;
        for (int tries = 0; tries < 50; tries++) {
            Vector2 g = map.RandomSidewalkPoint(r);
            float d = Dist(g, pp);
            if (d > 2200 && d < 6000) { mission.target = g; break; }
        }
        float d = Dist(mission.target, pp);
        mission.timer = d / (14.0f * M) + 25.0f;
        mission.reward = 300 + (int)(d * 0.12f);
        mission.title = "Hot Package";
        mission.objective = "Deliver the package to the drop-off.";
    }
    activePhone = -1;
    (void)phone;
    Big(mission.title.c_str(), { 255, 210, 90, 255 }, 3);
    audio.PlayUI(Sfx::Pickup, 0.8f, 0.8f);
}

void Game::EndMission(bool passed) {
    if (passed) {
        money += mission.reward;
        char buf[64]; snprintf(buf, sizeof(buf), "MISSION PASSED  $%d", mission.reward);
        Big(buf, { 255, 210, 90, 255 }, 4);
        audio.PlayUI(Sfx::MissionPass, 0.9f);
    } else if (state == GameState::Playing) {
        Big("MISSION FAILED", { 230, 80, 80, 255 }, 3);
        audio.PlayUI(Sfx::MissionFail, 0.9f);
    }
    if (mission.targetVehicle >= 0 && mission.targetVehicle < (int)vehicles.size()) vehicles[mission.targetVehicle].missionTarget = false;
    mission = Mission{};
    missionOffer = 8;
}

void Game::UpdateMission(float dt) {
    Vector2 pp = PlayerPos();
    if (mission.type == MissionType::None) {
        missionOffer -= dt;
        if (activePhone < 0 && missionOffer <= 0 && !jobPhones.empty()) {
            // offer the job at the phone nearest to the player (but not on top of them)
            float best = 1e9f;
            for (size_t k = 0; k < jobPhones.size(); k++) {
                float d = Dist(jobPhones[k], pp);
                if (d > 400 && d < best) { best = d; activePhone = (int)k; }
            }
            if (activePhone >= 0) { Toast_("A pay phone is ringing! (yellow marker)", { 255, 220, 90, 255 }); audio.PlayUI(Sfx::Pickup, 0.6f, 1.5f); }
        }
        if (activePhone >= 0 && Dist(jobPhones[activePhone], pp) < 2.8f * M && state == GameState::Playing) StartMission(activePhone);
        return;
    }
    mission.timer -= dt;
    bool inCar = player.inVehicle;
    switch (mission.type) {
    case MissionType::Courier:
        if (Dist(pp, mission.target) < 3.5f * M) { EndMission(true); return; }
        break;
    case MissionType::Demolition: {
        Vehicle& v = vehicles[mission.targetVehicle];
        if (!v.active) { EndMission(false); return; }
        if (v.wrecked) { EndMission(true); return; }
    } break;
    case MissionType::Steal: {
        Vehicle& v = vehicles[mission.targetVehicle];
        if (!v.active || v.wrecked) { EndMission(false); return; }
        if (mission.stage == 0 && inCar && player.vehicle == mission.targetVehicle) {
            mission.stage = 1;
            mission.objective = "Deliver it to the garage - keep it in one piece!";
            Toast_("Now drive it to the garage.", { 255, 220, 90, 255 });
        }
        if (mission.stage == 1 && Dist(v.pos, mission.target) < 4 * M && v.Speed() < 80) {
            float cond = Saturate(v.health / v.S().health);
            mission.reward = (int)(mission.reward * (0.4f + 0.6f * cond));
            EndMission(true);
            return;
        }
    } break;
    default: break;
    }
    if (mission.timer <= 0) EndMission(false);
}

void Game::Respawn(bool busted) {
    if (player.inVehicle && player.vehicle >= 0) {
        vehicles[player.vehicle].driver = DriverType::None;
        player.inVehicle = false; player.vehicle = -1;
    }
    player.pos = busted ? map.policeStation : map.hospital;
    player.vel = { 0, 0 };
    player.health = 100; player.armor = 0;
    if (busted) {
        int fine = std::max(0, money / 4);
        money -= fine;
        for (size_t w = 0; w < player.owned.size(); w++)
            if (gAssets.weapons[w].kind != WeaponKind::Melee) { player.owned[w] = 0; player.clip[w] = player.ammo[w] = 0; }
        player.weapon = std::max(0, gAssets.FindWeapon("Fists"));
        char buf[64]; snprintf(buf, sizeof(buf), "Fined $%d. Weapons confiscated.", fine);
        Toast_(buf, { 150, 200, 255, 255 });
    } else {
        int bill = std::min(money, 500);
        money -= bill;
        char buf[64]; snprintf(buf, sizeof(buf), "Hospital bill: $%d", bill);
        Toast_(buf, { 255, 150, 150, 255 });
    }
    heat = 0; bustTimer = 0;
    state = GameState::Playing;
    cam.Snap(player.pos, CAM_VIEW_FOOT);
}

// -------------------------------------------------------------------------------------
//  Rendering
// -------------------------------------------------------------------------------------
void Game::DrawPlayerSprite(bool shadowPass, Vector2 sv) const {
    if (player.inVehicle) return;
    const WeaponDef& W = gAssets.weapons[player.weapon];
    const CharacterSet* set = gAssets.FindSet(W.animSet);
    float s = gAssets.charScale * CHAR_SCALE;
    if (shadowPass) {
        const Texture2D& t = gAssets.softCircle;
        DrawFlatSprite(t, { 0, 0, (float)t.width, (float)t.height }, player.pos + sv * 14.0f, 0, 0.9f * M * CHAR_SCALE, 0.9f * M * CHAR_SCALE, 0, ColorA(BLACK, 0.85f));
        return;
    }
    // legs first (they follow the movement direction), then the upper body (aim)
    float spd = Len(player.vel);
    const SpriteAnim& feet = gAssets.feet[spd < 8 ? (int)FeetAnim::Idle : player.feetMode];
    if (feet.frames) DrawFlatSpriteX(feet.atlas, feet.Frame((int)player.feetAnim), player.pos, H_PED - 6, s * 0.95f, feet.pivot, player.feetAngle, WHITE);
    Color tint = player.hurtFlash > 0 ? LerpColor(WHITE, { 255, 120, 120, 255 }, player.hurtFlash) : WHITE;
    if (set) {
        const SpriteAnim* a = &set->anim[(int)player.bodyState];
        if (a->frames == 0) a = &set->anim[(int)BodyAnim::Idle];
        if (a->frames) {
            int frame;
            if (player.bodyState == BodyAnim::Shoot || player.bodyState == BodyAnim::Melee || player.bodyState == BodyAnim::Reload) {
                float dur = player.bodyState == BodyAnim::Reload ? W.reloadTime : player.bodyState == BodyAnim::Melee ? 0.45f : 0.12f;
                frame = (int)((1.0f - player.actionTimer / dur) * a->frames);
                frame = std::clamp(frame, 0, a->frames - 1);
            } else frame = (int)player.bodyAnim;
            DrawFlatSpriteX(a->atlas, a->Frame(frame), player.pos, H_PED, s, a->pivot, player.aim, tint);
            return;
        }
    }
    // fallback: procedural look-alike
    const Texture2D& t = gAssets.playerUnarmed;
    const float F = (float)spritegen::PED_FRAME;
    int fr = spd < 4 ? spritegen::PED_FRAME_IDLE : (int)player.feetAnim % spritegen::PED_WALK_FRAMES;
    DrawFlatSprite(t, { fr * F, 0, F, F }, player.pos, H_PED, 1.0f * M * CHAR_SCALE, 1.0f * M * CHAR_SCALE, player.aim, tint);
}

void Game::DrawWorld() {
    Rectangle view = cam.VisibleGround(300);
    Camera3D c3 = cam.Get();
    Vector2 sv = dn.shadowVec, sunDir = Norm(sv);
    renderer.EnsureSize(GetScreenWidth(), GetScreenHeight());

    // ---- 1. shadows ----
    renderer.BeginShadowPass(c3);
    map.DrawShadowCasters(view, sv);
    for (const Vehicle& v : vehicles) if (v.active && CheckCollisionPointRec(v.pos, view)) DrawVehicleShadow(v, sv);
    for (const Pedestrian& p : peds) if (p.active && CheckCollisionPointRec(p.pos, view)) DrawPedShadow(p, sv);
    DrawPlayerSprite(true, sv);
    renderer.EndShadowPass();

    // ---- 2. scene albedo ----
    renderer.BeginScenePass(c3, { 10, 12, 16, 255 });
    map.DrawGround(view, time);
    map.DrawMarkings(view);
    SetDepthTest(false);
    fx.DrawDecals(view);
    SetDepthTest(true);
    renderer.OverlayShadows(c3, dn.shadowAlpha);
    map.DrawStructures(view, cam.pos, dn.night, sunDir);
    SetDepthWrite(false);
    // pickups & mission ground markers
    for (const Pickup& k : pickups) {
        if (!k.active || !CheckCollisionPointRec(k.pos, view)) continue;
        const Texture2D& t = gAssets.props[(int)spritegen::Prop::Crate];
        float bob = sinf(time * 3 + k.pos.x) * 3;
        DrawFlatSprite(t, { 0, 0, (float)t.width, (float)t.height }, k.pos, 14 + bob, 0.8f * M, 0.8f * M, time * 1.2f, WHITE);
    }
    for (const Pedestrian& p : peds) if (p.active && (p.state == PedState::Dead || p.state == PedState::Down) && CheckCollisionPointRec(p.pos, view)) DrawPed(p);
    // vehicles: low first so tall trucks overlap small cars correctly
    static std::vector<int> order;
    order.clear();
    for (size_t i = 0; i < vehicles.size(); i++) if (vehicles[i].active && CheckCollisionPointRec(vehicles[i].pos, view)) order.push_back((int)i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return vehicles[a].height < vehicles[b].height; });
    for (int i : order) DrawVehicle(vehicles[i], time);
    for (const Pedestrian& p : peds) if (p.active && p.state != PedState::Dead && p.state != PedState::Down && CheckCollisionPointRec(p.pos, view)) DrawPed(p);
    DrawPlayerSprite(false, sv);
    SetDepthWrite(true);
    map.characters.clear();
    if (!player.inVehicle) map.characters.push_back(player.pos);
    for (const Pedestrian& p : peds) if (p.active && CheckCollisionPointRec(p.pos, view)) map.characters.push_back(p.pos);
    map.DrawSprites(view, time);
    SetDepthWrite(false);
    fx.DrawLit(view);
    SetDepthWrite(true);
    renderer.EndScenePass();

    // ---- 3. lights ----
    renderer.BeginLightPass(c3, dn.AmbientColor());
    map.DrawLights(view, dn.night, time);
    for (int i : order) DrawVehicleLights(vehicles[i], dn.night, time);
    fx.DrawLights(view);
    if (!player.inVehicle) {
        const WeaponDef& W = gAssets.weapons[player.weapon];
        if (W.light) Renderer::Cone(player.pos + Forward(player.aim) * 10.0f, 3, player.aim, 18 * M, 11 * M, { 230, 240, 255, 255 }, 1.3f);
        if (dn.night > 0.2f) Renderer::Radial(player.pos, 3, 2.5f * M, { 255, 230, 200, 255 }, 0.15f * dn.night);   // readability
    }
    for (const Pickup& k : pickups)
        if (k.active && CheckCollisionPointRec(k.pos, view)) Renderer::Radial(k.pos, 3, 2.2f * M, k.kind == 0 ? Color{ 80, 255, 120, 255 } : k.kind == 1 ? Color{ 80, 160, 255, 255 } : Color{ 255, 200, 60, 255 }, 0.5f);
    renderer.EndLightPass();

    // ---- 4. emissive (bloom sources) ----
    renderer.BeginEmissivePass(c3);
    map.DrawEmissive(view, dn.night, time);
    for (int i : order) DrawVehicleEmissive(vehicles[i], dn.night, time);
    fx.DrawEmissive(view);
    auto beacon = [&](Vector2 p, Color c) {
        float pulse = 0.6f + 0.4f * sinf(time * 4);
        DrawFlatRing(p, 2.0f * M, 2.4f * M, 1.5f, ColorA(c, pulse), 40);
        for (int k = 0; k < 6; k++) DrawFlatRing(p, 1.8f * M, 2.0f * M, 1.5f + k * 9.0f, ColorA(c, pulse * (1.0f - k / 6.0f) * 0.6f), 40);
    };
    if (debugOverview) {
        static const Color rc[7] = { GREEN, YELLOW, ORANGE, RED, MAGENTA, SKYBLUE, WHITE };
        for (int i : order) {
            const Vehicle& v = vehicles[i];
            if (v.driver != DriverType::Traffic) continue;
            DrawFlatRing(v.pos, 40, 52, 30, rc[std::clamp(v.ai.reason, 0, 6)], 16);
        }
    }
    if (activePhone >= 0) beacon(jobPhones[activePhone], { 255, 210, 60, 255 });
    if (mission.type == MissionType::Courier || (mission.type == MissionType::Steal && mission.stage == 1)) beacon(mission.target, { 255, 210, 60, 255 });
    for (const Pickup& k : pickups)
        if (k.active && CheckCollisionPointRec(k.pos, view))
            DrawFlatRing(k.pos, 0.7f * M, 0.85f * M, 2, ColorA(k.kind == 0 ? Color{ 80, 255, 120, 255 } : k.kind == 1 ? Color{ 80, 160, 255, 255 } : Color{ 255, 200, 60, 255 }, 0.8f), 28);
    renderer.EndEmissivePass();

    // ---- 5. final composite ----
    renderer.Composite(time, 0.85f + 0.6f * dn.night);
}

void Game::Draw() {
    double t0 = GetTime();
    DrawWorld();
    if (debugContacts) cpuDraw += GetTime() - t0;
    switch (state) {
        case GameState::Title: DrawTitle(); break;
        case GameState::Paused: DrawHUD(); DrawPause(); break;
        default: DrawHUD(); break;
    }
}
