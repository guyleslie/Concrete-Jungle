// =====================================================================================
//  Pedestrian AI & drawing - see pedestrian.h
// =====================================================================================
#include "pedestrian.h"
#include "traffic_incidents.h"
#include "game.h"
#include "traffic.h"
#include "assets.h"
#include "render.h"

using namespace cfg;

// ---- Tuning (world px, s) ------------------------------------------------------------
static const float LOOK_AHEAD   = 1.6f;              // s: how far ahead people predict vehicles
static const float SENSE_PERIOD = 0.1f;              // s between looks at the traffic
static const float BODY_MARGIN  = 0.35f * M;         // space people want between them and a car body
static const float SLOW_VEHICLE = 20.0f / 3.6f * M;  // slower vehicles are stepped away from calmly
static const float RUN_SPEED    = 5.2f * M;          // fleeing, for an average walker
static const float DODGE_SPEED  = 5.5f * M;          // jumping out of a vehicle's path
static const float STEP_SPEED   = 1.8f * M;          // stepping aside for a slow vehicle
// Anticipatory avoidance (Karamouzas, Skinner & Guy 2014): E(tau) = k / tau^2 * e^(-tau / tau0)
static const float TTC_K        = 1.5f * M * M;      // 1.5 m^2
static const float TTC_TAU0     = 3.0f;              // s: interactions fade out beyond this
static const float TTC_MAX      = 4.0f;              // s: collisions later than this are ignored
static const float NEIGHBOUR_R  = 5.0f * M;
static const float GOAL_TIME    = 0.5f;              // s: relaxation time towards the goal velocity

// How quickly a body can change speed and turn, and how hard it may swerve.
struct Gait { float accel, turn, avoid; };           // px/s^2, rad/s, px/s^2
static Gait GaitOf(PedState s) {
    switch (s) {
        case PedState::Flee:  return { 6.0f * M, 8.0f, 8.0f * M };
        case PedState::Dodge: return { 12.0f * M, 20.0f, 3.0f * M };
        case PedState::Fight: return { 6.0f * M, 8.0f, 4.0f * M };
        default:              return { 3.0f * M, 4.5f, 4.0f * M };
    }
}

// -------------------------------------------------------------------------------------
//  Spatial grid
// -------------------------------------------------------------------------------------
void PedGrid::Build(const std::vector<Pedestrian>& peds) {
    head.assign(W * H, -1);
    next.assign(peds.size(), -1);
    for (size_t k = 0; k < peds.size(); k++) {
        const Pedestrian& p = peds[k];
        if (!p.active) continue;
        int x = std::clamp((int)(p.pos.x / TILE), 0, W - 1), y = std::clamp((int)(p.pos.y / TILE), 0, H - 1);
        next[k] = head[y * W + x];
        head[y * W + x] = (int)k;
    }
}

// -------------------------------------------------------------------------------------
//  Sidewalk geometry
// -------------------------------------------------------------------------------------
static void CornerSigns(int c, int& sx, int& sy) {
    sx = (c == 1 || c == 2) ? 1 : -1;
    sy = (c >= 2) ? 1 : -1;
}
static int CornerFromSigns(int sx, int sy) {
    if (sy < 0) return sx < 0 ? 0 : 1;
    return sx > 0 ? 2 : 3;
}

// Nearest point on the sidewalk ring of block (bi,bj).
static Vector2 NearestOnRing(const CityMap& map, int bi, int bj, Vector2 p, int* cornerOut, int dirSign) {
    Rectangle r = map.SidewalkRing(bi, bj);
    Vector2 c[4] = { { r.x, r.y }, { r.x + r.width, r.y }, { r.x + r.width, r.y + r.height }, { r.x, r.y + r.height } };
    float best = 1e9f; Vector2 bp = c[0]; int bestSeg = 0;
    for (int s = 0; s < 4; s++) {
        Vector2 a = c[s], b = c[(s + 1) % 4];
        Vector2 ab = b - a;
        float t = Clampf(Dot(p - a, ab) / Dot(ab, ab), 0, 1);
        Vector2 q = a + ab * t;
        float d = Dist(q, p);
        if (d < best) { best = d; bp = q; bestSeg = s; }
    }
    // walking clockwise along segment s leads to corner s+1, counter-clockwise to corner s
    if (cornerOut) *cornerOut = dirSign > 0 ? (bestSeg + 1) % 4 : bestSeg;
    return bp;
}

// Sidewalk corner shifted towards/away from the block by the ped's personal lane, so
// people don't all walk on one line.
static Vector2 CornerTarget(const CityMap& map, int bi, int bj, int c, float off) {
    int sx, sy; CornerSigns(c, sx, sy);
    return map.SidewalkCorner(bi, bj, c) + V2(-sx * off, -sy * off);
}

static Vector2 Rotate(Vector2 v, float a) {
    float c = cosf(a), s = sinf(a);
    return { v.x * c - v.y * s, v.x * s + v.y * c };
}

// A point 'ahead' px further along the segment a-b than the point nearest to p: steering
// at it brings someone who was pushed aside back onto their own walking line.
static Vector2 AlongLine(Vector2 p, Vector2 a, Vector2 b, float ahead) {
    Vector2 ab = b - a;
    float l = Len(ab);
    if (l < 1) return b;
    float t = Clampf(Dot(p - a, ab) / l + ahead, 0, l);
    return a + ab * (t / l);
}

// -------------------------------------------------------------------------------------
//  Perception: will a vehicle run into me?
// -------------------------------------------------------------------------------------
struct Danger {
    int     vehicle = -1;
    float   ttc = 1e9f;      // s until the body (plus margin) reaches the person
    float   speed = 0;       // vehicle speed then
    Vector2 across{};        // unit vector across the vehicle's motion
    float   lateral = 0;     // person's offset from the vehicle centre along 'across'
    float   halfAcross = 0;  // half width of the danger zone along 'across'
};

// The person keeps their motion. Traffic on its lane follows its planned path at its
// current speed and stops where its driver has planned to (red light, person or obstacle
// ahead); other vehicles keep their velocity and turn rate (at most a quarter turn).
static bool PredictHit(Vector2 pos, Vector2 vel, const Vehicle& v, float horizon, Danger& out) {
    float sp = v.Speed();
    if (sp < 1.5f * M) return false;
    Vector2 rel = pos - v.pos;
    float reach = (sp + Len(vel)) * horizon + v.length * 0.5f + 2 * M;
    if (Len2(rel) > reach * reach) return false;
    bool rail = AIOnRail(v);
    float stopAt = rail ? v.ai.stopDist : 1e9f;
    // a vehicle that is about to stop only counts when its body itself would touch
    float margin = stopAt < 1.0f * M ? 0.1f * M : BODY_MARGIN;
    const float step = 0.1f;
    const float hw = v.width * 0.5f + margin + PED_RADIUS, hl = v.length * 0.5f + margin + PED_RADIUS;
    Vector2 vp = v.pos, dir = Norm(v.vel);
    float va = v.angle, turned = 0;
    for (float t = 0; t <= horizon + 1e-3f; t += step) {
        Vector2 q = pos + vel * t, d = q - vp;
        // inside the body plus margin, and not beside its rear half (it would be moving away)
        if (PointInOBB(MakeOBB(vp, va, hw, hl), q) && Dot(d, dir) > -v.length * 0.25f) {
            out.ttc = t;
            out.speed = sp;
            out.across = Perp(dir);
            out.lateral = Dot(d, out.across);
            out.halfAcross = OBBProjectRadius(MakeOBB(vp, va, v.width * 0.5f, v.length * 0.5f), out.across) + margin + PED_RADIUS;
            return true;
        }
        float next = t + step;
        if (rail) {
            float ahead = std::min(v.ai.speed * next, stopAt);
            Vector2 np = AIPathPose(v, ahead, &va);
            if (Len2(np - vp) > 0.01f) dir = Norm(np - vp);
            else if (ahead >= stopAt) break;                          // stopped: no longer a danger
            vp = np;
        } else {
            float da = Clampf(v.angVel * step, -PI * 0.5f - turned, PI * 0.5f - turned);
            turned += da;
            dir = Rotate(dir, da);
            va += da;
            vp = vp + dir * (sp * step);
        }
    }
    return false;
}

static Danger MostUrgent(Vector2 pos, Vector2 vel, const Game& g, float horizon) {
    Danger best, d;
    for (size_t i = 0; i < g.vehicles.size(); i++) {
        const Vehicle& v = g.vehicles[i];
        if (!v.active) continue;
        if (PredictHit(pos, vel, v, horizon, d) && d.ttc < best.ttc) { best = d; best.vehicle = (int)i; }
    }
    return best;
}

// -------------------------------------------------------------------------------------
//  Reactions
// -------------------------------------------------------------------------------------
static void StartFlee(Pedestrian& p, Vector2 from, float duration, bool secondHand) {
    p.state = PedState::Flee;
    p.threat = from;
    p.timer = duration * GRng().Range(0.8f, 1.2f);
    p.secondHand = secondHand;
    p.rethink = 0;
    p.alarm = -1;
    p.alarmVehicle = -1;
}

void ScarePed(Pedestrian& p, Vector2 from, float duration) {
    if (p.state == PedState::Down || p.state == PedState::Dead) return;
    StartFlee(p, from, duration, false);
}

void AlarmPed(Pedestrian& p, Vector2 from, float duration, bool secondHand) {
    if (p.state == PedState::Down || p.state == PedState::Dead || p.state == PedState::Fight) return;
    if (secondHand && p.incident >= 0) return;                     // a driver in a row knows why the other one runs
    if (p.state == PedState::Flee) {                                // already running: just refresh
        p.threat = from;
        p.timer = std::max(p.timer, duration * 0.8f);
        if (!secondHand) p.secondHand = false;
        return;
    }
    if (p.alarm >= 0) {                                             // already about to react
        if (p.alarmVehicle < 0 && !secondHand) p.alarmSecondHand = false;
        return;
    }
    p.alarm = p.reaction * GRng().Range(0.8f, 1.4f);
    p.alarmFrom = from;
    p.alarmDur = duration;
    p.alarmSecondHand = secondHand;
    p.alarmVehicle = -1;
}

void KnockDownPed(Pedestrian& p, Vector2 impulse) {
    if (p.state == PedState::Dead) return;
    p.state = PedState::Down;
    p.vel = impulse;
    p.timer = GRng().Range(2.5f, 4.0f);
    p.alarm = -1; p.alarmVehicle = -1;
    float il = Len(impulse);
    if (il > 1) {
        p.angle = AngleOf(impulse * -1.0f);
        p.threat = p.pos - impulse / il * 20.0f;                    // where the blow came from
    }
    p.spin = GRng().Range(-1.0f, 1.0f) * std::min(il, 260.0f) * 0.03f;   // tumbling
}

void ProvokePed(Pedestrian& p, const Game& g) {
    if (p.state == PedState::Down || p.state == PedState::Dead || g.player.inVehicle) return;
    if (p.state == PedState::Fight) {                               // already fighting: keep going until badly hurt
        if (p.health < 45) StartFlee(p, g.player.pos, GRng().Range(4.0f, 6.0f), false);
        return;
    }
    if (p.courage < 0.85f || p.health < 45) return;                 // most people run; the tough ones hit back
    p.state = PedState::Fight;
    p.foe = -1; p.foePlayer = true;                                 // a driver on foot turns on the player
    p.timer = GRng().Range(10.0f, 16.0f);                           // then they have had enough
    p.punchCd = GRng().Range(0.1f, 0.25f);
    p.alarm = -1; p.alarmVehicle = -1;
}

// Jump (or, for a slow vehicle, step) sideways out of the vehicle's path, towards the
// nearer edge unless a wall or furniture is in the way there.
static void StartDodge(Pedestrian& p, const Game& g, const Danger& d) {
    float side = d.lateral >= 0 ? 1.0f : -1.0f;
    float needSame = d.halfAcross - fabsf(d.lateral), needOther = d.halfAcross + fabsf(d.lateral);
    auto blocked = [&](float s, float need) {
        Vector2 to = p.pos + d.across * (s * (need + 0.5f * M));
        float t; Vector2 n;
        return !g.map.InCity(to, -8) || g.map.RayCast(p.pos, to, t, n, nullptr);
    };
    float need = needSame;
    if (blocked(side, needSame) && !blocked(-side, needOther)) { side = -side; need = needOther; }
    bool fast = d.speed > SLOW_VEHICLE;
    if (p.state != PedState::Dodge) p.rethink = 0;                  // counts the time spent dodging (negative)
    if (p.state != PedState::Dodge) {
        bool resumable = p.state == PedState::Walk || p.state == PedState::Wait || p.state == PedState::Cross ||
                         p.state == PedState::Wander || p.state == PedState::Flee;
        if (resumable) p.resume = p.state;
        else if (!(p.state == PedState::Idle && p.resume != PedState::Idle)) p.resume = PedState::Rejoin;
    }
    p.state = PedState::Dodge;
    p.moveDir = d.across * side;
    p.moveSpeed = fast ? DODGE_SPEED * Clampf(p.walkSpeed / 24.0f, 0.85f, 1.15f) : STEP_SPEED;
    p.timer = Clampf(need / p.moveSpeed + 0.2f, 0.3f, 1.2f);
    p.dodgeFrom = d.vehicle;
    p.closeCall = fast && d.ttc < 0.9f;
    p.alarm = -1; p.alarmVehicle = -1;
}

static void Resume(Pedestrian& p) {
    PedState s = p.resume;
    p.resume = PedState::Idle;
    if (s == PedState::Walk || s == PedState::Cross || s == PedState::Wander) p.state = s;   // targets are unchanged
    else if (s == PedState::Wait) { p.state = PedState::Wait; p.timer = 0; }
    else { p.state = PedState::Rejoin; p.timer = 0; }
}

static void EndDodge(Pedestrian& p, Game& g) {
    Rng& r = GRng();
    Vector2 from = p.dodgeFrom >= 0 && p.dodgeFrom < (int)g.vehicles.size() ? g.vehicles[p.dodgeFrom].pos : p.pos - p.moveDir * 20.0f;
    if (p.resume == PedState::Flee) { p.resume = PedState::Idle; StartFlee(p, p.threat, r.Range(2.0f, 3.0f), p.secondHand); return; }
    if (p.closeCall && r.Chance(0.5f)) {                            // that was close: panic
        if (r.Chance(0.3f)) g.audio.Play(Sfx::Scream, p.pos, 0.35f, r.Range(0.9f, 1.3f));
        p.resume = PedState::Idle;
        StartFlee(p, from, r.Range(2.0f, 3.5f), false);
        return;
    }
    p.threat = from;                                                // startled: stop and look
    p.state = PedState::Idle;
    p.timer = r.Range(0.5f, 1.4f);
}

// Flee direction: away from the threat, but along the sidewalk rather than into the
// road, not into walls or furniture, and not into the path of a moving vehicle.
// Re-evaluated a few times a second.
static Vector2 PickFleeDir(const Pedestrian& p, const Game& g) {
    const CityMap& map = g.map;
    int movers[8], nm = 0;
    for (size_t i = 0; i < g.vehicles.size() && nm < 8; i++) {
        const Vehicle& v = g.vehicles[i];
        if (v.active && v.Speed() > 3.0f * M && Len2(v.pos - p.pos) < (35 * M) * (35 * M)) movers[nm++] = (int)i;
    }
    float run = RUN_SPEED * p.walkSpeed / 24.0f;
    Vector2 away = Norm(p.pos - p.threat);
    if (Len2(away) < 0.5f) away = Forward(p.angle);
    Vector2 cur = Len2(p.vel) > 100 ? Norm(p.vel) : away;
    bool onRoad = map.TileAt(p.pos) == Tile::Road;
    float best = -1e9f; Vector2 bd = away;
    for (int k = 0; k < 16; k++) {
        Vector2 d = Forward(k * PI / 8);
        float s = 1.4f * Dot(d, away) + 0.4f * Dot(d, cur);
        Vector2 near = p.pos + d * (1.5f * M), far = p.pos + d * (4.0f * M);
        bool roadNear = map.TileAt(near) == Tile::Road, roadFar = map.TileAt(far) == Tile::Road;
        if (onRoad) s += roadFar ? 0.0f : 0.9f;                      // get off the road
        else s -= (roadNear ? 1.6f : 0.0f) + (roadFar ? 0.8f : 0.0f);
        float t; Vector2 n;
        if (!map.InCity(far, -8)) s -= 3.0f;
        else if (map.RayCast(p.pos, far, t, n, nullptr)) s -= 2.2f * (1.0f - t);
        Danger dz;
        for (int m = 0; m < nm; m++) if (PredictHit(p.pos, d * run, g.vehicles[movers[m]], 1.2f, dz)) { s -= 3.0f; break; }
        if (s > best) { best = s; bd = d; }
    }
    return bd;
}

// -------------------------------------------------------------------------------------
//  Walking the city
// -------------------------------------------------------------------------------------
void InitPed(Pedestrian& p, Vector2 pos, int skin, const CityMap& map) {
    Rng& r = GRng();
    static uint32_t nextSerial = 0;
    p = Pedestrian{};
    p.active = true;
    p.serial = ++nextSerial;
    p.skin = skin;
    p.walkSpeed = r.Range(19.0f, 27.0f);
    p.laneOffset = r.Range(-9.0f, 9.0f);
    p.reaction = r.Range(0.18f, 0.45f);
    p.patience = r.Range(18.0f, 45.0f);
    p.courage = r.Float();
    p.sense = r.Range(0.0f, SENSE_PERIOD);
    p.resume = PedState::Idle;
    p.bi = std::clamp((int)(pos.x / (BLOCK_PITCH * TILE)), 0, BLOCKS_X - 1);
    p.bj = std::clamp((int)(pos.y / (BLOCK_PITCH * TILE)), 0, BLOCKS_Y - 1);
    p.dirSign = r.Chance(0.5f) ? 1 : -1;
    p.pos = NearestOnRing(map, p.bi, p.bj, pos, &p.corner, p.dirSign);
    p.target = CornerTarget(map, p.bi, p.bj, p.corner, p.laneOffset);
    p.angle = AngleOf(p.target - p.pos);
    p.state = PedState::Walk;
    p.anim = r.Range(0, 8);
}

static void PickNextLeg(Pedestrian& p, Game& g) {
    const CityMap& map = g.map;
    Rng& r = GRng();
    int sx, sy; CornerSigns(p.corner, sx, sy);
    BlockType bt = map.Block(p.bi, p.bj);
    float u = r.Float();
    if (u < 0.06f) { p.state = PedState::Idle; p.resume = PedState::Idle; p.timer = r.Range(1.0f, 4.0f); return; }
    if ((bt == BlockType::Park || bt == BlockType::Plaza) && u < 0.2f) {
        Rectangle in = map.BlockInterior(p.bi, p.bj);
        p.state = PedState::Wander;
        p.target = { r.Range(in.x + 40, in.x + in.width - 40), r.Range(in.y + 40, in.y + in.height - 40) };
        return;
    }
    if (u < 0.45f) {
        // cross the street to the neighbouring block: first step up to the kerb
        bool horizontal = r.Chance(0.5f);
        int nbi = p.bi + (horizontal ? sx : 0), nbj = p.bj + (horizontal ? 0 : sy);
        if (nbi >= 0 && nbj >= 0 && nbi < BLOCKS_X && nbj < BLOCKS_Y) {
            p.ci = p.bi + (sx > 0 ? 1 : 0);
            p.cj = p.bj + (sy > 0 ? 1 : 0);
            p.caxis = horizontal ? 1 : 0;       // crossing a N-S road needs the E-W phase
            p.nextBi = nbi; p.nextBj = nbj;
            p.nextCorner = horizontal ? CornerFromSigns(-sx, sy) : CornerFromSigns(sx, -sy);
            Vector2 c = map.SidewalkCorner(p.bi, p.bj, p.corner), line = CornerTarget(map, p.bi, p.bj, p.corner, p.laneOffset);
            // spread along the kerb (the crossing is 44 px deep) and a little back from it
            float along = p.laneOffset * 1.9f, back = r.Range(0.0f, 8.0f);
            p.waitPt = horizontal ? V2(c.x + sx * (20.0f - back), line.y - sy * (along - p.laneOffset))
                                  : V2(line.x - sx * (along - p.laneOffset), c.y + sy * (20.0f - back));
            p.queued = 0;
            p.state = PedState::Wait;
            p.timer = 0;
            p.rethink = 0;
            return;
        }
    }
    p.corner = (p.corner + p.dirSign + 4) % 4;
    p.target = CornerTarget(map, p.bi, p.bj, p.corner, p.laneOffset);
    p.state = PedState::Walk;
}

// At the kerb: cross on green if there is time to get off the road before the crossing
// traffic gets green (yellow + all-red = 2 s after ours), or on red after running out of
// patience - either way only if no vehicle will come through while we are on the road.
enum { CROSS_NO = 0, CROSS_GREEN, CROSS_JAYWALK };
static int ReadyToCross(const Pedestrian& p, const Game& g, Vector2 dest) {
    const CityMap& map = g.map;
    float speed = p.walkSpeed * 1.25f;
    float tClear = std::max(0.0f, Dist(p.pos, dest) - 0.5f * TILE) / speed;   // the far half of the sidewalk is safe
    bool green = map.SignalState(p.ci, p.cj, p.caxis) == SIG_GREEN && map.GreenTimeLeft(p.ci, p.cj, p.caxis) + 2.0f >= tClear + 0.5f;
    bool impatient = p.timer > p.patience;
    if (!green && !impatient) return CROSS_NO;
    // on green trust the lights for the far lane and only watch what is close; jaywalkers look properly
    float horizon = green ? std::min(tClear, 2.5f) : tClear + 1.0f;
    Danger d = MostUrgent(p.pos, Norm(dest - p.pos) * speed, g, horizon);
    if (d.vehicle >= 0) return CROSS_NO;
    return green ? CROSS_GREEN : CROSS_JAYWALK;
}

// -------------------------------------------------------------------------------------
//  Locomotion
// -------------------------------------------------------------------------------------
// Anticipatory avoidance force between two discs: x and v are our position and velocity
// relative to the other one, R the sum of the radii (Karamouzas et al. 2014).
static Vector2 TTCForce(Vector2 x, Vector2 v, float R) {
    float a = Dot(v, v);
    if (a < 1e-3f) return { 0, 0 };
    float b = Dot(x, v);
    if (b >= 0) return { 0, 0 };                     // moving apart
    float c = Dot(x, x) - R * R;
    if (c <= 0) return { 0, 0 };                     // touching: the contact pass separates
    float disc = b * b - a * c;
    if (disc <= 0) return { 0, 0 };                  // will pass each other
    float sd = sqrtf(disc);
    float tau = c / (-b + sd);                       // = (-b - sd) / a, numerically stable
    if (tau > TTC_MAX) return { 0, 0 };
    tau = std::max(tau, 0.05f);
    float mag = TTC_K * expf(-tau / TTC_TAU0) / (a * tau * tau) * (2.0f / tau + 1.0f / TTC_TAU0);
    return (v - (x * a - v * b) / sd) * -mag;
}

// People walk where their body faces: the steering velocity turns the body at a limited
// rate, and only its component along the body (plus a small side step) is walked.
static float Locomote(Pedestrian& p, Vector2 goal, Vector2 avoid, float faceTo, float dt) {
    Gait G = GaitOf(p.state);
    float al = Len(avoid);
    if (al > G.avoid) avoid = avoid * (G.avoid / al);
    Vector2 acc = (goal - p.vel) / GOAL_TIME + avoid;
    float accL = Len(acc);
    if (accL > G.accel) acc = acc * (G.accel / accL);
    Vector2 want = p.vel + acc * dt;
    float ws = Len(want);
    // standing still with somewhere to look (the other side of the street, an opponent): face it
    float face = !std::isnan(faceTo) && Len2(goal) < 1.0f ? faceTo : (ws > 4.0f ? AngleOf(want) : faceTo);
    float turn = 0;
    if (!std::isnan(face)) {
        turn = Clampf(WrapAngle(face - p.angle), -G.turn * dt, G.turn * dt);
        p.angle = WrapAngle(p.angle + turn);
    }
    Vector2 f = Forward(p.angle), rt = RightOf(p.angle);
    float fwd = std::max(0.0f, Dot(want, f));
    float side = Clampf(Dot(want, rt), -0.3f * M, 0.3f * M);
    p.vel = f * fwd + rt * side;
    return turn / dt;
}

// Buildings and solid street furniture are hard walls.
static void Collide(Pedestrian& p, const CityMap& map) {
    static std::vector<int> ids;
    Rectangle box = { p.pos.x - 20, p.pos.y - 20, 40, 40 };
    map.QueryBuildings(box, ids);
    for (int k : ids) {
        Vector2 n; float depth;
        if (CircleOBB(p.pos, PED_RADIUS, MakeAABB(map.buildings[k].r), n, depth)) p.pos = p.pos + n * depth;
    }
    map.QueryObjects(box, ids);
    for (int k : ids) {
        const CityObject& o = map.objects[k];
        if (o.walkIn) continue;
        if (o.box) { Vector2 n; float depth; if (CircleOBB(p.pos, PED_RADIUS, o.Box(), n, depth)) p.pos = p.pos + n * depth; continue; }
        Vector2 d = p.pos - o.pos; float l = Len(d), rr = o.radius + PED_RADIUS;
        if (l < rr && l > 0.01f) p.pos = o.pos + d / l * rr;
    }
    p.pos.x = Clampf(p.pos.x, 4, WORLD_W - 4);
    p.pos.y = Clampf(p.pos.y, 4, WORLD_H - 4);
}

// -------------------------------------------------------------------------------------
//  Update
// -------------------------------------------------------------------------------------
void UpdatePed(Pedestrian& p, Game& g, float dt) {
    const CityMap& map = g.map;
    Rng& r = GRng();

    // ---- on the ground ----
    if (p.state == PedState::Down || p.state == PedState::Dead) {
        p.vel = p.vel * expf(-4.0f * dt);
        p.angle = WrapAngle(p.angle + p.spin * dt);
        p.spin *= expf(-3.0f * dt);
        p.runOverT -= dt;
        if (p.state == PedState::Dead) {
            p.deadTime += dt;
            if (!p.pooled && Len2(p.vel) < 15 * 15) {
                p.pooled = true;
                g.fx.AddDecal(p.pos, DECAL_BLOOD, 1.9f * M, r.Range(0.0f, 6.0f), 90);
            }
        } else {
            p.timer -= dt;
            if (p.health <= 0) { p.state = PedState::Dead; p.deadTime = 0; }
            else if (p.timer <= 0) {                                 // back on their feet
                p.spin = 0;
                StartFlee(p, p.threat, 4.0f, false);
                if (p.courage >= 0.85f && Dist(p.pos, g.player.pos) < 6 * M) ProvokePed(p, g);   // the tough ones come back for more
            }
        }
        p.pos = p.pos + p.vel * dt;
        Collide(p, map);
        p.turnRate = 0;
        return;
    }

    // ---- perception: vehicles about to run into us, delayed reactions ----
    p.sense -= dt;
    if (p.sense <= 0) {
        p.sense = SENSE_PERIOD * r.Range(0.8f, 1.2f);
        Danger d = MostUrgent(p.pos, p.vel, g, LOOK_AHEAD);
        if (d.vehicle >= 0) {
            if (p.state == PedState::Dodge) {
                // still in a path: re-plan for another vehicle, or when moving the wrong way
                float side = d.lateral >= 0 ? 1.0f : -1.0f;
                if (d.vehicle != p.dodgeFrom || Dot(p.moveDir, d.across) * side < 0) StartDodge(p, g, d);
            } else if (p.alarm < 0 || p.alarmVehicle < 0) {
                const Vehicle& v = g.vehicles[d.vehicle];
                bool behind = Dot(Forward(p.angle), Norm(v.pos - p.pos)) < -0.3f;   // heard, not seen
                p.alarm = p.reaction * (behind ? 1.5f : 1.0f) * r.Range(0.85f, 1.15f);
                p.alarmVehicle = d.vehicle;
            }
        }
    }
    if (p.alarm >= 0) {
        p.alarm -= dt;
        if (p.alarm < 0) {
            p.alarm = -1;
            int vi = p.alarmVehicle;
            p.alarmVehicle = -1;
            if (vi >= 0) {
                Danger d;
                if (vi < (int)g.vehicles.size() && g.vehicles[vi].active && PredictHit(p.pos, p.vel, g.vehicles[vi], LOOK_AHEAD, d)) {
                    d.vehicle = vi;
                    StartDodge(p, g, d);
                }
            } else StartFlee(p, p.alarmFrom, p.alarmDur, p.alarmSecondHand);
        }
    }

    // ---- decisions ----
    Vector2 dest{};
    switch (p.state) {
    case PedState::Walk:
        if (Dist(p.pos, p.target) < 10) PickNextLeg(p, g);
        break;
    case PedState::Wait:
        p.timer += dt;
        p.rethink -= dt;
        dest = CornerTarget(map, p.nextBi, p.nextBj, p.nextCorner, p.laneOffset);
        if (p.rethink <= 0 && p.queued < 3 && Dist(p.pos, p.waitPt) > 10) {
            // someone already stands on our spot: queue behind them
            bool taken = false;
            g.pedGrid.Query(p.waitPt, 12, [&](int k) {
                const Pedestrian& o = g.peds[k];
                if (&o != &p && o.active && o.state == PedState::Wait && Dist(o.pos, p.waitPt) < PED_RADIUS * 1.9f) taken = true;
            });
            if (taken) { p.waitPt = p.waitPt + Norm(p.waitPt - dest) * (PED_RADIUS * 2.2f); p.queued++; }
        }
        if ((Dist(p.pos, p.waitPt) < 12 || p.timer > 4) && p.rethink <= 0) {
            p.rethink = 0.25f;
            int go = ReadyToCross(p, g, dest);
            if (go != CROSS_NO) {
                p.state = PedState::Cross;
                p.jaywalk = go == CROSS_JAYWALK;
                p.target = dest;
                p.bi = p.nextBi; p.bj = p.nextBj; p.corner = p.nextCorner;
            }
        }
        break;
    case PedState::Cross:
        if (Dist(p.pos, p.target) < 10) { p.state = PedState::Walk; PickNextLeg(p, g); }
        break;
    case PedState::Idle:
        p.timer -= dt;
        if (p.timer <= 0) { if (p.resume != PedState::Idle) Resume(p); else PickNextLeg(p, g); }
        break;
    case PedState::Wander:
        if (Dist(p.pos, p.target) < 10) { p.state = PedState::Rejoin; p.timer = r.Range(1.5f, 5.0f); }
        break;
    case PedState::Flee:
        p.timer -= dt;
        p.rethink -= dt;
        if (p.rethink <= 0) { p.moveDir = PickFleeDir(p, g); p.rethink = r.Range(0.3f, 0.5f); }
        if (p.timer <= 0) { p.state = PedState::Rejoin; p.timer = 0; }
        break;
    case PedState::Rejoin:
        if (p.timer > 0) { p.timer -= dt; break; }
        p.bi = std::clamp((int)(p.pos.x / (BLOCK_PITCH * TILE)), 0, BLOCKS_X - 1);
        p.bj = std::clamp((int)(p.pos.y / (BLOCK_PITCH * TILE)), 0, BLOCKS_Y - 1);
        p.target = NearestOnRing(map, p.bi, p.bj, p.pos, &p.corner, p.dirSign);
        if (Dist(p.pos, p.target) < 8) {
            p.target = CornerTarget(map, p.bi, p.bj, p.corner, p.laneOffset);
            p.state = PedState::Walk;
        }
        break;
    case PedState::Dodge:
        p.timer -= dt;
        p.rethink -= dt;
        if (p.timer <= 0) {
            // only stop once really out of its way (at most 2 s in all)
            Danger d;
            const Vehicle* v = p.dodgeFrom >= 0 && p.dodgeFrom < (int)g.vehicles.size() ? &g.vehicles[p.dodgeFrom] : nullptr;
            if (v && v->active && p.rethink > -2.0f && PredictHit(p.pos, V2(0, 0), *v, 1.0f, d)) p.timer = 0.1f;
            else EndDodge(p, g);
        }
        break;
    case PedState::Fight: {
        p.timer -= dt; p.punchCd -= dt; p.punchT -= dt;
        // A driver in an incident may fight the other driver; everyone else fights the player.
        bool pedFoe = p.foe >= 0;
        Vector2 foePos = g.player.pos;
        if (pedFoe) {
            float reach = 0, x = 0, y = 0;
            if (!IncidentFoePos(g, p, &reach, &x, &y)) { p.state = PedState::Rejoin; p.timer = r.Range(0.5f, 1.5f); break; }
            foePos = V2(x, y);
        }
        Vector2 to = foePos - p.pos;
        float d = Len(to);
        if (!pedFoe && g.player.inVehicle) { StartFlee(p, g.player.pos, r.Range(2.0f, 4.0f), false); break; }
        if (p.timer <= 0 || d > 12 * M || g.state != GameState::Playing) { p.state = PedState::Rejoin; p.timer = r.Range(0.5f, 1.5f); break; }
        if (d < PED_RADIUS * 3.5f && p.punchCd <= 0 && fabsf(WrapAngle(AngleOf(to) - p.angle)) < 0.7f) {
            p.punchCd = r.Range(0.8f, 1.2f);
            p.punchT = 0.25f;
            g.audio.Play(Sfx::Punch, p.pos, 0.7f, r.Range(0.9f, 1.1f));
            if (pedFoe) IncidentPunch(g, (int)(&p - &g.peds[0]));
            else { g.DamagePlayer(r.Range(5.0f, 9.0f), to); g.pedPunches++; }
        }
    } break;
    case PedState::Confront: case PedState::ToCar:
        p.punchT -= dt;                                             // the incident controller decides
        break;
    default: break;
    }

    // ---- goal velocity ----
    Vector2 goal{ 0, 0 };
    float faceTo = NAN;
    auto towards = [&](Vector2 t, float speed) { Vector2 d = t - p.pos; float l = Len(d); return l > 1e-3f ? d / l * speed : V2(0, 0); };
    switch (p.state) {
    case PedState::Walk: {
        Vector2 from = CornerTarget(map, p.bi, p.bj, (p.corner - p.dirSign + 4) % 4, p.laneOffset);
        goal = towards(AlongLine(p.pos, from, p.target, 2.5f * M), p.walkSpeed);
    } break;
    case PedState::Wait: {
        float l = Dist(p.pos, p.waitPt);
        if (l > 3) goal = towards(p.waitPt, std::min(p.walkSpeed, l * 2.0f));
        faceTo = AngleOf(CornerTarget(map, p.nextBi, p.nextBj, p.nextCorner, p.laneOffset) - p.pos);
    } break;
    case PedState::Cross: {
        bool hurry = map.SignalState(p.ci, p.cj, p.caxis) != SIG_GREEN && map.TileAt(p.pos) == Tile::Road;
        goal = towards(p.target, p.walkSpeed * (hurry ? 1.9f : 1.25f));
    } break;
    case PedState::Idle:   if (p.resume != PedState::Idle) faceTo = AngleOf(p.threat - p.pos); break;
    case PedState::Wander: goal = towards(p.target, p.walkSpeed * 0.85f); break;
    case PedState::Rejoin: if (p.timer <= 0) goal = towards(p.target, p.walkSpeed); break;
    case PedState::Flee:   goal = p.moveDir * (RUN_SPEED * p.walkSpeed / 24.0f); break;
    case PedState::Dodge:  goal = p.moveDir * p.moveSpeed; break;
    case PedState::Fight: {
        Vector2 foePos = g.player.pos;
        float reach = 0, x = 0, y = 0;
        if (p.foe >= 0 && IncidentFoePos(g, p, &reach, &x, &y)) foePos = V2(x, y);
        float d = Dist(p.pos, foePos);
        if (d > PED_RADIUS * 2.6f) goal = towards(foePos, p.walkSpeed * 1.6f);
        faceTo = AngleOf(foePos - p.pos);
    } break;
    case PedState::Confront: {
        float reach = 0, x = 0, y = 0;
        if (IncidentFoePos(g, p, &reach, &x, &y)) {
            Vector2 foePos = V2(x, y);
            if (Dist(p.pos, foePos) > reach * 0.8f) goal = towards(foePos, p.walkSpeed * 1.3f);
            faceTo = AngleOf(foePos - p.pos);
        }
    } break;
    case PedState::ToCar: {
        float x = 0, y = 0;
        if (IncidentCarDoor(g, p, &x, &y)) {
            Vector2 door = V2(x, y);
            float l = Dist(p.pos, door);
            goal = towards(door, std::min(p.walkSpeed * 1.1f, l * 3.0f));
            // Turn towards the car first: the gait cannot walk backwards to start.
            faceTo = l < 24 && p.ownVehicle >= 0 ? AngleOf(g.vehicles[p.ownVehicle].pos - p.pos) : AngleOf(door - p.pos);
        }
    } break;
    default: break;
    }

    // ---- avoidance: people (anticipatory), panic spreading, the player, furniture ----
    Vector2 avoid{ 0, 0 };
    bool calm = p.state != PedState::Flee && p.state != PedState::Dodge && p.state != PedState::Fight && p.alarm < 0;
    const float touch = PED_RADIUS * 2.0f;
    g.pedGrid.Query(p.pos, NEIGHBOUR_R, [&](int k) {
        const Pedestrian& o = g.peds[k];
        if (&o == &p || !o.active || o.state == PedState::Dead) return;
        Vector2 x = p.pos - o.pos;
        float d2 = Len2(x);
        if (d2 > NEIGHBOUR_R * NEIGHBOUR_R) return;
        bool lying = o.state == PedState::Down;
        avoid = avoid + TTCForce(x, lying ? p.vel : p.vel - o.vel, lying ? PED_RADIUS * 2.4f : PED_RADIUS * 2 + 2);
        if (!lying && d2 < touch * touch && d2 > 0.01f) {          // bodies touching: share the separation
            float d = sqrtf(d2);
            p.pos = p.pos + x / d * ((touch - d) * 0.5f);
        }
        if (calm && o.state == PedState::Flee && !o.secondHand && d2 < 16 * M * M && Len2(o.vel) > 50 * 50 && r.Chance(1.2f * dt))
            AlarmPed(p, o.threat, r.Range(2.0f, 3.5f), true);
    });
    if (!g.player.inVehicle) {
        Vector2 x = p.pos - g.player.pos;
        avoid = avoid + TTCForce(x, p.vel - g.player.vel, PED_RADIUS * 2.1f + 2);
        float d = Len(x), R = PED_RADIUS * 2.1f;
        if (d < R && d > 0.01f) p.pos = p.pos + x / d * (R - d);   // the player does not give way
    }
    if (Len2(p.vel) > 4) {
        static std::vector<int> near;
        map.QueryObjects({ p.pos.x - 48, p.pos.y - 48, 96, 96 }, near);
        for (int k : near) {
            const CityObject& o = map.objects[k];
            if (o.walkIn) continue;
            Vector2 c = o.box ? OBBClosestPoint(o.Box(), p.pos) : o.pos;   // a box acts as a post at its nearest point
            avoid = avoid + TTCForce(p.pos - c, p.vel, (o.box ? 0.0f : o.radius) + PED_RADIUS + 3);
        }
    }

    // ---- move ----
    p.turnRate = Locomote(p, goal, avoid, faceTo, dt);
    p.pos = p.pos + p.vel * dt;
    Collide(p, map);

    // wanting to move but not getting anywhere? turn around / take another route
    bool mobile = p.state == PedState::Walk || p.state == PedState::Cross || p.state == PedState::Wander ||
                  p.state == PedState::Rejoin || p.state == PedState::Flee || p.state == PedState::Dodge;
    float want = Len(goal);
    if (mobile && want > 5 && Len2(p.pos - p.lastPos) < (4.0f * dt) * (4.0f * dt)) p.stuckT += dt;
    else p.stuckT = std::max(0.0f, p.stuckT - dt);
    p.lastPos = p.pos;
    if (p.stuckT > 1.2f) {
        p.stuckT = 0;
        p.laneOffset = -p.laneOffset;
        if (p.state == PedState::Walk) { p.dirSign = -p.dirSign; p.corner = (p.corner + p.dirSign + 4) % 4; p.target = CornerTarget(map, p.bi, p.bj, p.corner, p.laneOffset); }
        else if (p.state == PedState::Wander || p.state == PedState::Flee) { p.state = PedState::Rejoin; p.timer = 0; }
        else if (p.state == PedState::Dodge) p.timer = 0;
        p.pos = p.pos + V2(r.Range(-3, 3), r.Range(-3, 3));
    }

    // stride-matched walk cycle: 8 frames per ~1.3 m (two steps); turning on the spot
    // steps round as well
    float spd = Len(p.vel);
    p.anim += spd * dt * (8.0f / (1.3f * M)) * (p.state == PedState::Flee || p.state == PedState::Dodge ? 0.7f : 1.0f);
    if (spd < 10) p.anim += fabsf(p.turnRate) * dt * (4.0f / PI);
    p.sway = sinf(p.anim * PI * 0.25f) * Saturate(spd / 20.0f) * 0.05f;
}

// -------------------------------------------------------------------------------------
//  Drawing
// -------------------------------------------------------------------------------------
static const float PED_DRAW = 1.4f * M * CHAR_SCALE;   // atlas frame size in the world (standing)
static const float PED_LYING = 1.0f;                   // lying frame scale: a real-size 1.7 m body (standing people
                                                       // are drawn larger than life, a body on the ground is not)

void DrawPedShadow(const Pedestrian& p, Vector2 sv) {
    const Texture2D& t = gAssets.softCircle;
    bool lying = p.state == PedState::Down || p.state == PedState::Dead;
    float s = lying ? 1.8f * M : 0.8f * M * CHAR_SCALE;
    float fade = p.state == PedState::Dead ? 1.0f - Saturate((p.deadTime - PED_BODY_FADE) / (PED_BODY_GONE - PED_BODY_FADE)) : 1.0f;
    DrawFlatSprite(t, { 0, 0, (float)t.width, (float)t.height }, p.pos + sv * (lying ? 2.0f : 12.0f), 0, s, s * (lying ? 0.6f : 1.0f), p.angle, ColorA(BLACK, 0.8f * fade));
}

void DrawPed(const Pedestrian& p) {
    if (gAssets.peds.empty()) return;
    const Texture2D& t = gAssets.peds[p.skin % gAssets.peds.size()];
    int frame;
    bool lying = p.state == PedState::Down || p.state == PedState::Dead;
    if (lying) frame = spritegen::PED_FRAME_DOWN;
    else if ((p.state == PedState::Fight || p.state == PedState::Confront) && p.punchT > 0) frame = spritegen::PED_FRAME_PUNCH + (p.punchT > 0.12f ? 0 : 1);
    else if (Len(p.vel) < 4 && fabsf(p.turnRate) < 1.0f) frame = spritegen::PED_FRAME_IDLE;
    else frame = (int)p.anim % spritegen::PED_WALK_FRAMES;
    const float F = (float)spritegen::PED_FRAME;
    Rectangle src = { frame * F, 0, F, F };
    float size = PED_DRAW * (lying ? PED_LYING : 1.0f);
    float h = lying ? 1.5f : H_PED;
    Color tint = WHITE;
    if (p.state == PedState::Dead) tint = ColorA({ 200, 190, 190, 255 }, 1.0f - Saturate((p.deadTime - PED_BODY_FADE) / (PED_BODY_GONE - PED_BODY_FADE)));
    DrawFlatSprite(t, src, p.pos, h, size, size, p.angle + (lying ? 0.0f : p.sway), tint);
}
