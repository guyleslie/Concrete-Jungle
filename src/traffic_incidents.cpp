// =====================================================================================
//  Driver incidents - see traffic_incidents.h
// =====================================================================================
#include "traffic_incidents.h"
#include "game.h"
#include "traffic.h"
#include <cstring>
#include <vector>

namespace {

struct Settings {
    float enabled = 1;
    float aggressiveShare = 0.10f, calmShare = 0.45f;
    float minImpact = 60;          // px/s closing speed that a driver takes personally
    float seriousDv = 320;         // px/s delta-V: a serious crash, nobody starts an argument
    float confrontChance = 0.75f;  // an aggressive driver gets out after such an impact
    float argueTime = 3;           // s of shouting before it escalates or ends
    float giveUp = 480;            // px: the other party has gone
    float fightTime = 10;          // s at most of trading punches
    float exitWait = 3;            // s to wait for a safe side to get out
    float pairMemory = 60;         // s: one incident per pair of cars
};

const Settings& Tuning() {
    static Settings s;
    static bool loaded = false;
    if (loaded) return s;
    loaded = true;
    TrafficField fields[] = {
        { "enabled", &s.enabled, 0, 1 },
        { "aggressive_share", &s.aggressiveShare, 0, 1 },
        { "calm_share", &s.calmShare, 0, 1 },
        { "min_impact", &s.minImpact, 10, 400 },
        { "serious_dv", &s.seriousDv, 50, 1000 },
        { "confront_chance", &s.confrontChance, 0, 1 },
        { "argue_time", &s.argueTime, 0.5f, 15 },
        { "give_up_distance", &s.giveUp, 100, 2000 },
        { "fight_time", &s.fightTime, 2, 60 },
        { "exit_wait", &s.exitWait, 0.5f, 15 },
        { "pair_memory", &s.pairMemory, 0, 600 }
    };
    LoadTrafficRecords("INCIDENT", fields, (int)(sizeof(fields) / sizeof(fields[0])));
    if (s.aggressiveShare + s.calmShare > 1) s.calmShare = 1 - s.aggressiveShare;
    return s;
}

enum class Phase : uint8_t { Seated, Stopping, OnFoot, Back, Gone };
struct Side {
    int vehicle = -1; uint32_t serial = 0;  // the car; the player's side keeps its car if any
    bool player = false;                    // this side is the player
    bool confronts = false;                 // this driver gets out
    int ped = -1; uint32_t pedSerial = 0;   // the driver on foot
    Phase phase = Phase::Seated;
    float wait = 0, exitDelay = 1;          // stopped time / personal pause before getting out
    const char* reason = "";
};

struct Incident {
    int id = 0;
    bool active = false;
    Side side[2];
    float time = 0;
    bool fought = false;
};

struct PairMemory { uint32_t a = 0, b = 0; float time = 0; };

std::vector<Incident> incidents;
std::vector<PairMemory> pairs;
IncidentStats stats;
int nextId = 1;

bool ValidVehicle(const Game& g, int idx, uint32_t serial) {
    return idx >= 0 && idx < (int)g.vehicles.size() && g.vehicles[idx].active && g.vehicles[idx].serial == serial;
}
bool ValidPed(const Game& g, int idx, uint32_t serial) {
    return idx >= 0 && idx < (int)g.peds.size() && g.peds[idx].active && g.peds[idx].serial == serial;
}

// The driver's door: left of the car, beside the front seats.
Vector2 DoorPoint(const Vehicle& v, float side) {
    return v.pos + RightOf(v.angle) * (side * (v.width * 0.5f + 12)) - v.Fwd() * (v.length * 0.05f);
}

// A side to get out on: not into a wall, a car or the path of moving traffic.
bool SafeExit(const Game& g, int self, Vector2 p) {
    if (g.map.PointInBuilding(p, PED_RADIUS)) return false;
    for (int k = 0; k < (int)g.vehicles.size(); k++) {
        const Vehicle& o = g.vehicles[k];
        if (!o.active || k == self || Len2(o.pos - p) > 220 * 220) continue;
        if (PointInOBB(o.Box(), p, PED_RADIUS + 3)) return false;
        Vector2 to = p - o.pos;
        if (o.Speed() > 30 && Dot(o.vel, to) > 0 && Len(to) < 4 * cfg::M + o.length * 0.5f) return false;
    }
    return true;
}

void Log(const Game& g, const Incident& in, const char* text) {
    if (g.debugContacts) TraceLog(LOG_INFO, "INCIDENT #%d t=%.2f %s", in.id, g.time, text);
}

// The opponent of side s, for the person who got out.
void SetFoe(const Game& g, Incident& in, int s, Pedestrian& p) {
    const Side& o = in.side[1 - s];
    p.foe = -1; p.foeSerial = 0; p.foePlayer = false; p.foeVehicle = -1; p.foeVehicleSerial = 0;
    if (o.player) { p.foePlayer = true; return; }
    if (o.phase == Phase::OnFoot && ValidPed(g, o.ped, o.pedSerial)) { p.foe = o.ped; p.foeSerial = o.pedSerial; return; }
    if (ValidVehicle(g, o.vehicle, o.serial)) { p.foeVehicle = o.vehicle; p.foeVehicleSerial = o.serial; }
}

void EndSide(Game& g, Incident& in, int s, const char* reason) {
    Side& side = in.side[s];
    if (side.phase == Phase::Gone || side.phase == Phase::Back) return;
    if (ValidVehicle(g, side.vehicle, side.serial)) {
        Vehicle& v = g.vehicles[side.vehicle];
        if (v.ai.incident == in.id) v.ai.incident = -1;
        v.ai.driverPed = -1; v.ai.driverPedSerial = 0;
        // A car this driver will not return to (empty, taken or wrecked) is no longer
        // protected: it is recycled or cleaned up like any other car in that state.
        if (v.driver != DriverType::Traffic) v.recoveryTracked = false;
    }
    if (ValidPed(g, side.ped, side.pedSerial)) {
        Pedestrian& p = g.peds[side.ped];
        p.incident = -1; p.ownVehicle = -1; p.ownSerial = 0; p.foe = -1; p.foePlayer = false; p.foeVehicle = -1;
        if (p.state == PedState::Confront || p.state == PedState::ToCar || p.state == PedState::Fight) {
            p.state = PedState::Rejoin; p.timer = 0;
        }
    }
    side.phase = Phase::Gone;
    side.reason = reason;
    if (!strcmp(reason, "car_lost")) stats.carLost++;
    else if (!strcmp(reason, "driver_dead")) stats.driverDead++;
    else if (!strcmp(reason, "no_safe_exit")) stats.noSafeExit++;
    else stats.interrupted++;
    Log(g, in, TextFormat("side %d ends: %s", s, reason));
}

void GetOut(Game& g, Incident& in, int s) {
    Side& side = in.side[s];
    Vehicle& v = g.vehicles[side.vehicle];
    Vector2 door = DoorPoint(v, -1);
    float doorSide = -1;
    if (!SafeExit(g, side.vehicle, door)) { door = DoorPoint(v, 1); doorSide = 1; }
    if (!SafeExit(g, side.vehicle, door)) return;
    int idx = g.SpawnPed(door, v.driverSkin, false);
    Pedestrian& p = g.peds[idx];
    p.angle = v.angle + doorSide * PI * 0.5f;
    p.courage = v.ai.mood == (uint8_t)DriverMood::Aggressive ? 0.95f : 0.5f;
    p.state = PedState::Confront;
    p.incident = in.id; p.ownVehicle = side.vehicle; p.ownSerial = side.serial;
    p.argue = 0; p.timer = 0; p.punchCd = 0.6f;
    side.ped = idx; side.pedSerial = p.serial;
    side.phase = Phase::OnFoot;
    // The car waits with its handbrake on; it is a parked physical body until the
    // same driver returns, and population recycling leaves it alone.
    v.driver = DriverType::None;
    v.in = VehicleInput{}; v.in.handbrake = true;
    v.ai.rail = false; v.ai.yieldTo = -1;
    v.ai.driverPed = idx; v.ai.driverPedSerial = p.serial;
    v.recoveryTracked = true;
    g.audio.Play(Sfx::Door, door, 0.8f);
    stats.exits++;
    Log(g, in, TextFormat("driver of #%d (%s) gets out on the %s, person #%d", side.vehicle,
                          DriverMoodText((DriverMood)v.ai.mood), doorSide < 0 ? "left" : "right", idx));
    // An opponent already waiting on foot now faces this person instead of the car.
    Side& other = in.side[1 - s];
    if (other.phase == Phase::OnFoot && ValidPed(g, other.ped, other.pedSerial)) SetFoe(g, in, 1 - s, g.peds[other.ped]);
    SetFoe(g, in, s, p);
}

// Back behind the wheel: the same person, the same car, then physical recovery to the lane.
void GetIn(Game& g, Incident& in, int s) {
    Side& side = in.side[s];
    Vehicle& v = g.vehicles[side.vehicle];
    Pedestrian& p = g.peds[side.ped];
    v.driver = DriverType::Traffic;
    v.driverSkin = p.skin;
    v.ai.incident = -1; v.ai.driverPed = -1; v.ai.driverPedSerial = 0;
    v.ai.rail = false;
    RecoveryReset(v.ai.recovery);
    v.ai.dynTimer = 5; v.ai.recover = 0; v.ai.retry = 0;
    v.recoveryTracked = true;
    v.in = VehicleInput{};
    AIResetPath(v, g.map);
    p.active = false; p.incident = -1; p.ownVehicle = -1;
    g.audio.Play(Sfx::Door, v.pos, 0.8f);
    side.phase = Phase::Back;
    side.reason = "returned";
    stats.returns++;
    Log(g, in, TextFormat("driver is back in #%d", side.vehicle));
}

void StartFight(Game& g, Incident& in, Pedestrian& p) {
    const Settings& cfg = Tuning();
    p.state = PedState::Fight;
    p.timer = cfg.fightTime * GRng().Range(0.8f, 1.2f);
    p.punchCd = GRng().Range(0.2f, 0.5f);
    if (!in.fought) { in.fought = true; stats.fights++; Log(g, in, "the argument becomes a fight"); }
}

void UpdateOnFoot(Game& g, Incident& in, int s, float dt) {
    const Settings& cfg = Tuning();
    Side& side = in.side[s];
    if (!ValidPed(g, side.ped, side.pedSerial)) { EndSide(g, in, s, "driver_gone"); return; }
    Pedestrian& p = g.peds[side.ped];
    if (p.state == PedState::Dead) { EndSide(g, in, s, "driver_dead"); return; }
    bool carOk = ValidVehicle(g, side.vehicle, side.serial);
    const Vehicle* car = carOk ? &g.vehicles[side.vehicle] : nullptr;
    if (!car || car->driver != DriverType::None || car->wrecked || car->burning) {
        EndSide(g, in, s, car && (car->wrecked || car->burning) ? "car_lost" : "car_lost");
        return;
    }
    switch (p.state) {
    case PedState::Down: case PedState::Flee: case PedState::Dodge:
        break;                                        // injured or scared: their own reflexes first
    case PedState::Confront: {
        SetFoe(g, in, s, p);
        float reach = 0, x = 0, y = 0;
        if (!IncidentFoePos(g, p, &reach, &x, &y)) { p.state = PedState::ToCar; break; }
        float d = Dist(p.pos, V2(x, y));
        if (d > cfg.giveUp) { p.state = PedState::ToCar; Log(g, in, "the other party has gone"); break; }
        if (d > reach * 1.5f) break;
        if (p.argue == 0) { stats.confrontations++; Log(g, in, TextFormat("person #%d confronts the other driver", side.ped)); }
        p.argue += dt;
        p.punchCd -= dt;
        if (p.punchCd <= 0) {                         // fist shaking; on a car it lands on the door
            p.punchCd = GRng().Range(0.9f, 1.4f);
            p.punchT = 0.25f;
            if (p.foeVehicle >= 0) {
                g.audio.Play(Sfx::Punch, p.pos, 0.6f, GRng().Range(0.8f, 0.95f));
                g.DamageVehicle(p.foeVehicle, 1.5f, false, p.pos);
            }
        }
        if (p.argue >= cfg.argueTime) {
            const Side& o = in.side[1 - s];
            bool foeOut = p.foe >= 0 && ValidPed(g, p.foe, p.foeSerial);
            bool foeWilling = foeOut && (g.peds[p.foe].state == PedState::Confront || g.peds[p.foe].state == PedState::Fight);
            if (o.player && !g.player.inVehicle) StartFight(g, in, p);
            else if (foeWilling) { StartFight(g, in, p); StartFight(g, in, g.peds[p.foe]); }
            else { p.state = PedState::ToCar; Log(g, in, "the argument ends; walking back"); }
        }
    } break;
    case PedState::Fight: {
        // Lost the fight, or had enough: get away from the opponent first.
        if (p.health < 45) {
            float rx = 0, ry = 0, reach = 0;
            Vector2 from = IncidentFoePos(g, p, &reach, &rx, &ry) ? V2(rx, ry) : p.pos;
            ScarePed(p, from, GRng().Range(1.5f, 2.5f));
            Log(g, in, TextFormat("person #%d backs off, hurt", side.ped));
        }
        break;                                        // UpdatePed moves and punches; it ends the fight
    }
    case PedState::ToCar: {
        float x = 0, y = 0;
        if (IncidentCarDoor(g, p, &x, &y) && Dist(p.pos, V2(x, y)) < 16) GetIn(g, in, s);
    } break;
    default:
        // Fight over, flight over or interrupted: walk back to the same car.
        p.state = PedState::ToCar;
        Log(g, in, TextFormat("person #%d walks back to #%d", side.ped, side.vehicle));
        break;
    }
}

} // namespace

const char* DriverMoodText(DriverMood mood) {
    switch (mood) {
    case DriverMood::Calm: return "calm";
    case DriverMood::Normal: return "normal";
    case DriverMood::Aggressive: return "aggressive";
    }
    return "unknown";
}

void IncidentsReset() {
    incidents.clear(); pairs.clear(); stats = IncidentStats{}; nextId = 1;
}

void IncidentAssignMood(Vehicle& v) {
    const Settings& cfg = Tuning();
    float r = GRng().Float();
    v.ai.mood = (uint8_t)(r < cfg.aggressiveShare ? DriverMood::Aggressive
        : r < cfg.aggressiveShare + cfg.calmShare ? DriverMood::Calm : DriverMood::Normal);
}

bool IncidentHoldsVehicle(const Vehicle& v) { return v.ai.incident >= 0 && v.driver == DriverType::Traffic; }

void IncidentOnImpact(Game& g, const ImpactEvent& e) {
    const Settings& cfg = Tuning();
    if (cfg.enabled < 0.5f || e.kind != ContactKind::Vehicle || e.b < 0) return;
    if (e.approach < cfg.minImpact || std::max(e.dvA, e.dvB) > cfg.seriousDv) return;
    int ids[2] = { e.a, e.b };
    Side sides[2];
    int traffic = 0;
    for (int s = 0; s < 2; s++) {
        const Vehicle& v = g.vehicles[ids[s]];
        sides[s].vehicle = ids[s]; sides[s].serial = v.serial;
        if (g.player.inVehicle && g.player.vehicle == ids[s]) sides[s].player = true;
        else if (v.driver == DriverType::Traffic && v.Drivable()) {
            if (v.ai.incident >= 0) return;               // already busy with another incident
            traffic++;
        } else return;                                    // parked, police, empty or wrecked
    }
    if (traffic == 0) return;
    uint32_t a = std::min(sides[0].serial, sides[1].serial), b = std::max(sides[0].serial, sides[1].serial);
    for (const PairMemory& m : pairs) if (m.a == a && m.b == b && g.time - m.time < cfg.pairMemory) return;
    pairs.push_back({ a, b, g.time });
    if (pairs.size() > 64) pairs.erase(pairs.begin());
    bool anyone = false;
    for (int s = 0; s < 2; s++) {
        if (sides[s].player) continue;
        Vehicle& v = g.vehicles[ids[s]];
        DriverMood mood = (DriverMood)v.ai.mood;
        if (mood == DriverMood::Aggressive && GRng().Chance(cfg.confrontChance)) {
            sides[s].confronts = true; sides[s].phase = Phase::Stopping; anyone = true;
            sides[s].exitDelay = GRng().Range(0.8f, 1.6f);
        } else if (mood == DriverMood::Normal) {
            g.audio.Play(Sfx::Horn, v.pos, 0.55f, GRng().Range(0.85f, 1.1f));
            v.ai.honk = 3;
        }
    }
    if (!anyone) {
        stats.ignoredCalm++;
        if (g.debugContacts)
            TraceLog(LOG_INFO, "INCIDENT none t=%.2f impact %.0f px/s between #%d (%s) and #%d (%s): nobody gets out",
                     g.time, e.approach, ids[0], sides[0].player ? "player" : DriverMoodText((DriverMood)g.vehicles[ids[0]].ai.mood),
                     ids[1], sides[1].player ? "player" : DriverMoodText((DriverMood)g.vehicles[ids[1]].ai.mood));
        return;
    }
    Incident in;
    in.id = nextId++; in.active = true; in.time = 0;
    in.side[0] = sides[0]; in.side[1] = sides[1];
    for (int s = 0; s < 2; s++) if (in.side[s].confronts) {
        Vehicle& v = g.vehicles[ids[s]];
        v.ai.incident = in.id;
        v.recoveryTracked = true;
        v.ai.yieldTo = -1;
    }
    stats.started++;
    incidents.push_back(in);
    Log(g, incidents.back(), TextFormat("starts: impact %.0f px/s, #%d %s%s vs #%d %s%s", e.approach,
        ids[0], sides[0].player ? "player" : DriverMoodText((DriverMood)g.vehicles[ids[0]].ai.mood), sides[0].confronts ? " (gets out)" : "",
        ids[1], sides[1].player ? "player" : DriverMoodText((DriverMood)g.vehicles[ids[1]].ai.mood), sides[1].confronts ? " (gets out)" : ""));
}

void IncidentsUpdate(Game& g, float dt) {
    const Settings& cfg = Tuning();
    for (Incident& in : incidents) {
        if (!in.active) continue;
        in.time += dt;
        for (int s = 0; s < 2; s++) {
            Side& side = in.side[s];
            if (side.player || !side.confronts) continue;
            if (side.phase == Phase::Stopping) {
                if (!ValidVehicle(g, side.vehicle, side.serial)) { EndSide(g, in, s, "car_lost"); continue; }
                Vehicle& v = g.vehicles[side.vehicle];
                if (v.driver != DriverType::Traffic || !v.Drivable()) { EndSide(g, in, s, "car_lost"); continue; }
                if (v.Speed() < 3) {
                    side.wait += dt;
                    if (side.wait >= side.exitDelay) GetOut(g, in, s);
                    if (side.phase == Phase::Stopping && side.wait > side.exitDelay + cfg.exitWait) {
                        Log(g, in, "no safe side to get out; driving on");
                        EndSide(g, in, s, "no_safe_exit");
                    }
                }
            } else if (side.phase == Phase::OnFoot) UpdateOnFoot(g, in, s, dt);
        }
        bool open = false;
        for (const Side& side : in.side) open = open || side.phase == Phase::Stopping || side.phase == Phase::OnFoot;
        if (!open) {
            in.active = false;
            Log(g, in, TextFormat("ends after %.1f s: %s / %s", in.time,
                in.side[0].confronts ? in.side[0].reason : "stayed", in.side[1].confronts ? in.side[1].reason : "stayed"));
        }
    }
    incidents.erase(std::remove_if(incidents.begin(), incidents.end(), [](const Incident& in) { return !in.active; }),
                    incidents.end());
}

bool IncidentFoePos(const Game& g, const Pedestrian& p, float* reach, float* x, float* y) {
    Vector2 pos;
    if (p.foe >= 0) {
        if (!ValidPed(g, p.foe, p.foeSerial) || g.peds[p.foe].state == PedState::Dead) return false;
        pos = g.peds[p.foe].pos; *reach = PED_RADIUS * 2.6f;
    } else if (p.foePlayer) {
        if (g.player.inVehicle) {
            if (g.player.vehicle < 0) return false;
            pos = DoorPoint(g.vehicles[g.player.vehicle], -1); *reach = 14;
        } else { pos = g.player.pos; *reach = PED_RADIUS * 2.6f; }
    } else if (p.foeVehicle >= 0) {
        if (!ValidVehicle(g, p.foeVehicle, p.foeVehicleSerial)) return false;
        pos = DoorPoint(g.vehicles[p.foeVehicle], -1); *reach = 14;
    } else return false;
    *x = pos.x; *y = pos.y;
    return true;
}

void IncidentPunch(Game& g, int attacker) {
    Pedestrian& p = g.peds[attacker];
    if (!ValidPed(g, p.foe, p.foeSerial)) return;
    Pedestrian& foe = g.peds[p.foe];
    // The other driver trades punches; a bystander would run, but only drivers fight here.
    if (foe.state == PedState::Confront && foe.foe == attacker) {
        foe.state = PedState::Fight;
        foe.timer = std::max(foe.timer, Tuning().fightTime * 0.8f);
    }
    g.DamagePed(p.foe, GRng().Range(6.0f, 10.0f), foe.pos - p.pos, false, GRng().Chance(0.12f));
}

bool IncidentCarDoor(const Game& g, const Pedestrian& p, float* x, float* y) {
    if (!ValidVehicle(g, p.ownVehicle, p.ownSerial)) return false;
    Vector2 door = DoorPoint(g.vehicles[p.ownVehicle], -1);
    if (!SafeExit(g, p.ownVehicle, door)) door = DoorPoint(g.vehicles[p.ownVehicle], 1);
    *x = door.x; *y = door.y;
    return true;
}

IncidentStats IncidentGetStats() { return stats; }
int IncidentActiveCount() { return (int)incidents.size(); }

void IncidentLogStats() {
    TraceLog(LOG_INFO, "INCIDENTS: started %d, drivers out %d, confrontations %d, fights %d, back in their car %d | no safe exit %d, car lost %d, driver dead %d, other interruptions %d | impacts without a confrontation %d",
             stats.started, stats.exits, stats.confrontations, stats.fights, stats.returns,
             stats.noSafeExit, stats.carLost, stats.driverDead, stats.interrupted, stats.ignoredCalm);
}
