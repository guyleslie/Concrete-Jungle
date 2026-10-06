// =====================================================================================
//  AI drivers - see traffic.h
// =====================================================================================
#include "traffic.h"
#include "game.h"
#include "traffic_incidents.h"

using namespace cfg;

// Every physical recovery reads the same poses before any driver updates.
void AIObserveTraffic(Game& g) { RecoveryBeginFrame(g); }

static Vector2 DirVec(int d) { return d == 0 ? V2(0, -1) : d == 1 ? V2(1, 0) : d == 2 ? V2(0, 1) : V2(-1, 0); }
static Vector2 RightV(int d) { return DirVec((d + 1) & 3); }
static int DX(int d) { return d == 1 ? 1 : d == 3 ? -1 : 0; }
static int DY(int d) { return d == 2 ? 1 : d == 0 ? -1 : 0; }

static const float STOP_BACK = 72.0f;      // stop line distance before the junction entry point
static const float COMFORT_DECEL = 420.0f;

// -------------------------------------------------------------------------------------
//  Path construction
// -------------------------------------------------------------------------------------
static void Push(DriverAI& ai, Waypoint w) {
    w.cum = ai.path.empty() ? 0.0f : ai.path.back().cum + Dist(ai.path.back().p, w.p);
    ai.path.push_back(w);
}

// Plans the route through junction (ti,tj) approached in direction ai.dir.
// 'goal' (optional) makes the turn choice close in on a point (police).
static void PlanNext(Vehicle& v, const CityMap& map, const Vector2* goal) {
    DriverAI& ai = v.ai;
    int d = ai.dir;
    ai.ti = std::clamp(ai.ti, 0, INTER_X - 1); ai.tj = std::clamp(ai.tj, 0, INTER_Y - 1);
    Vector2 c = map.InterCenter(ai.ti, ai.tj);
    int opts[3] = { d, (d + 1) & 3, (d + 3) & 3 };
    float w[3] = { v.S().large() ? 0.7f : 0.55f, 0.25f, v.S().large() ? 0.1f : 0.2f };
    int cand[3]; float cw[3]; int n = 0;
    for (int k = 0; k < 3; k++) {
        int d2 = opts[k];
        if (!map.ValidInter(ai.ti + DX(d2), ai.tj + DY(d2))) continue;
        float weight = w[k];
        if (goal) weight = 0.05f + Saturate(Dot(DirVec(d2), Norm(*goal - c)) + 0.3f) + GRng().Range(0, 0.25f);
        cand[n] = d2; cw[n] = weight; n++;
    }
    int d2;
    if (n == 0) d2 = (d + 2) & 3;
    else if (goal) { int b = 0; for (int k = 1; k < n; k++) if (cw[k] > cw[b]) b = k; d2 = cand[b]; }
    else {
        float tot = 0; for (int k = 0; k < n; k++) tot += cw[k];
        float r = GRng().Float() * tot; d2 = cand[n - 1];
        for (int k = 0; k < n; k++) { r -= cw[k]; if (r <= 0) { d2 = cand[k]; break; } }
    }
    int turnType = d2 == d ? 0 : d2 == ((d + 1) & 3) ? 1 : d2 == ((d + 3) & 3) ? 2 : 3;

    Vector2 E = c - DirVec(d) * ROAD_HALF + RightV(d) * LANE_OFFSET;
    Vector2 X = c + DirVec(d2) * ROAD_HALF + RightV(d2) * LANE_OFFSET;
    Waypoint e; e.p = E; e.stop = true; e.si = ai.ti; e.sj = ai.tj; e.axis = (d == 0 || d == 2) ? 0 : 1;
    e.turnType = turnType; e.d = d; e.d2 = d2;
    Push(ai, e);
    if (turnType == 0) {
        Waypoint x; x.p = X; Push(ai, x);
    } else {
        Vector2 ctrl;
        if (turnType == 3) ctrl = c + DirVec(d) * 40.0f;
        else if (turnType == 1 && v.S().large()) ctrl = c + (RightV(d) + RightV(d2)) * 10.0f;   // wide right turn
        else ctrl = c + RightV(d) * LANE_OFFSET + RightV(d2) * LANE_OFFSET;
        for (int k = 1; k <= 10; k++) { Waypoint t; t.p = QuadBezier(E, ctrl, X, k / 10.0f); t.turn = true; Push(ai, t); }
    }
    // points along the next block so the path stays smooth and long enough
    Vector2 nc = map.InterCenter(ai.ti + DX(d2), ai.tj + DY(d2));
    Vector2 nE = nc - DirVec(d2) * ROAD_HALF + RightV(d2) * LANE_OFFSET;
    for (int k = 1; k <= 3; k++) { Waypoint m; m.p = LerpV(X, nE, k / 4.0f); Push(ai, m); }
    ai.ti += DX(d2); ai.tj += DY(d2); ai.dir = d2;
}

// Starts a path at 'start' heading 'dir', with a tail behind it so the rear-axle sample
// is always on the path (otherwise the pose would lurch when a path begins).
static void SetShiftConstant(DriverAI& ai, float shift) {
    ai.laneShift = ai.shiftFrom = ai.shiftTo = shift;
    ai.shiftS0 = ai.shiftS1 = 0;
}

static void StartPath(Vehicle& v, Vector2 start, Vector2 dir) {
    DriverAI& ai = v.ai;
    ai.path.clear();
    SetShiftConstant(ai, ai.laneShift);       // path distances restart: the shift is held
    Waypoint tail; tail.p = start - dir * (v.length + 24); Push(ai, tail);
    Waypoint s0; s0.p = start; Push(ai, s0);
    ai.s = ai.path.back().cum;
}

// Point (and heading) at distance 's' along the path.
static Vector2 Sample(const DriverAI& ai, float s, float* heading = nullptr) {
    const auto& P = ai.path;
    if (P.empty()) return { 0, 0 };
    if (P.size() == 1 || s <= P.front().cum) {
        if (heading && P.size() > 1) *heading = AngleOf(P[1].p - P[0].p);
        return P.front().p;
    }
    for (size_t i = 0; i + 1 < P.size(); i++) {
        if (s <= P[i + 1].cum) {
            float seg = std::max(1e-3f, P[i + 1].cum - P[i].cum);
            if (heading) *heading = AngleOf(P[i + 1].p - P[i].p);
            return LerpV(P[i].p, P[i + 1].p, (s - P[i].cum) / seg);
        }
    }
    if (heading) *heading = AngleOf(P.back().p - P[P.size() - 2].p);
    return P.back().p;
}

// Sample() for a run of non-decreasing distances: each call continues from the segment
// the previous one ended in instead of scanning the path from its start. Cumulative
// distances never decrease, so the segment found is the same and so is the result.
// The heading of a segment is computed once per segment the cursor visits.
struct PathCursor { size_t seg = 0; size_t headingSeg = (size_t)-1; float heading = 0; };
static float SegmentHeading(const std::deque<Waypoint>& P, size_t i, PathCursor& cursor) {
    if (cursor.headingSeg != i) { cursor.headingSeg = i; cursor.heading = AngleOf(P[i + 1].p - P[i].p); }
    return cursor.heading;
}
static Vector2 SampleAt(const DriverAI& ai, float s, PathCursor& cursor, float* heading = nullptr) {
    const auto& P = ai.path;
    if (P.empty()) return { 0, 0 };
    if (P.size() == 1 || s <= P.front().cum) {
        if (heading && P.size() > 1) *heading = SegmentHeading(P, 0, cursor);
        return P.front().p;
    }
    for (size_t i = cursor.seg; i + 1 < P.size(); i++) {
        if (s <= P[i + 1].cum) {
            cursor.seg = i;
            float seg = std::max(1e-3f, P[i + 1].cum - P[i].cum);
            if (heading) *heading = SegmentHeading(P, i, cursor);
            return LerpV(P[i].p, P[i + 1].p, (s - P[i].cum) / seg);
        }
    }
    cursor.seg = P.size() - 2;
    if (heading) *heading = SegmentHeading(P, P.size() - 2, cursor);
    return P.back().p;
}

Vector2 AIPathPose(const Vehicle& v, float ahead, float* angle) {
    const DriverAI& ai = v.ai;
    Vector2 heading;
    Vector2 pos = ShiftedRailPose([&](float d) { return Sample(ai, d); }, ai.s + ahead, v.length,
                                  ai.shiftFrom, ai.shiftTo, ai.shiftS0, ai.shiftS1, v.angle, &heading);
    if (angle) *angle = AngleOf(heading);
    return pos;
}

void AISetLaneShift(Vehicle& v, float shift) {
    SetShiftConstant(v.ai, shift);
    v.ai.laneShiftTarget = shift;
}

// Length of an S-curve lane change of 'delta' px: the curve's sharpest bend (6*delta/L^2)
// stays within a comfortable turning radius for the vehicle, and at speed its lateral
// acceleration within about 6 m/s^2 (a brisk city lane change); never shorter than one
// and a half car lengths, and no longer than 'room' allows when the bend permits.
static float ShiftLength(const Vehicle& v, float delta, float speed, float room = 1e9f) {
    float radius = std::max(90.0f, v.length * 1.25f);
    float bend = std::max(sqrtf(6 * fabsf(delta) * radius), v.length * 1.5f);
    float lateral = sqrtf(6 * fabsf(delta) * speed * speed / 96.0f);
    return std::max(bend, std::min(lateral, room));
}

// Begin a lane shift to 'target' at the rear axle's current path distance, driven
// forward (+1) or in reverse (-1).
static void StartShift(Vehicle& v, float target, int direction, float speed, float room = 1e9f) {
    DriverAI& ai = v.ai;
    float rear = ai.s - v.length * 0.32f;
    float current = LaneShiftAt(ai.shiftFrom, ai.shiftTo, ai.shiftS0, ai.shiftS1, rear);
    ai.shiftFrom = current; ai.shiftTo = target;
    ai.shiftS0 = rear; ai.shiftS1 = rear + direction * ShiftLength(v, target - current, speed, room);
}

// Does the body, driven along the path from s 'from' to s 'to' with the given shift
// profile, keep clear of other vehicles (and, onto the sidewalk, of buildings and
// solid furniture; with 'people', of people)?
static bool ShiftSweepClear(Game& g, int self, float from, float to, float shiftFrom, float shiftTo,
                            float s0, float s1, bool sidewalk, bool people, float grow = 2) {
    const Vehicle& v = g.vehicles[self];
    float step = from <= to ? 6.0f : -6.0f;
    static std::vector<int> near, ids;
    near.clear();
    float reach = fabsf(to - from) + v.length + 200;
    for (int k = 0; k < (int)g.vehicles.size(); k++)
        if (k != self && g.vehicles[k].active && Len2(g.vehicles[k].pos - v.pos) < reach * reach) near.push_back(k);
    for (float s = from;; s += step) {
        bool last = step > 0 ? s >= to : s <= to;
        if (last) s = to;
        Vector2 heading;
        Vector2 pos = ShiftedRailPose([&](float d) { return Sample(v.ai, d); }, s, v.length, shiftFrom, shiftTo, s0, s1,
                                      v.angle, &heading);
        OBB box = MakeOBB(pos, AngleOf(heading), v.width * 0.5f + grow, v.length * 0.5f + grow);
        Vector2 n; float depth;
        for (int k : near) if (OBBOverlap(box, g.vehicles[k].Box(), n, depth)) return false;
        if (sidewalk) {
            if (g.map.PointInBuilding(pos, v.width * 0.5f)) return false;
            g.map.QueryObjects({ pos.x - 60, pos.y - 60, 120, 120 }, ids);
            for (int k : ids) {
                const CityObject& o = g.map.objects[k];
                if (o.alive && !o.soft && o.radius > 0 && CircleOBB(o.pos, o.radius, box, n, depth)) return false;
            }
        }
        if (people) {
            bool person = false;
            float r = v.length * 0.5f + PED_RADIUS + 2;
            g.pedGrid.Query(pos, r, [&](int k) {
                const Pedestrian& p = g.peds[k];
                if (p.active && p.state != PedState::Dead && PointInOBB(box, p.pos, PED_RADIUS)) person = true;
            });
            if (!g.player.inVehicle && PointInOBB(box, g.player.pos, PED_RADIUS)) person = true;
            if (person) return false;
        }
        if (last) break;
    }
    return true;
}

void AIResetPath(Vehicle& v, const CityMap& map) {
    DriverAI& ai = v.ai;
    ai.path.clear();
    int d = (int)floorf(WrapAngle(v.angle) / (PI * 0.5f) + 0.5f);
    d = ((d % 4) + 4) % 4;
    ai.dir = d;
    const float pitch = (float)(BLOCK_PITCH * TILE);
    float fx = (v.pos.x - TILE) / pitch, fy = (v.pos.y - TILE) / pitch;
    int ti, tj;
    if (d == 0 || d == 2) { ti = (int)roundf(fx); tj = d == 2 ? (int)ceilf(fy + 0.05f) : (int)floorf(fy - 0.05f); }
    else                  { tj = (int)roundf(fy); ti = d == 1 ? (int)ceilf(fx + 0.05f) : (int)floorf(fx - 0.05f); }
    if (!map.ValidInter(ti, tj)) { d = (d + 2) & 3; ai.dir = d; }
    ai.ti = std::clamp(ti, 0, INTER_X - 1); ai.tj = std::clamp(tj, 0, INTER_Y - 1);
}

// U-turn in the middle of a block: swing across into the opposite lane. A failed check
// leaves the current route untouched (it used to clear it, sending the car to the map
// origin), and the whole swept turn must be clear of other vehicles.
static bool PlanUTurn(Game& g, Vehicle& v) {
    const CityMap& map = g.map;
    DriverAI& ai = v.ai;
    DriverAI saved = ai;
    auto fail = [&]() { ai = saved; return false; };
    AIResetPath(v, map);
    int d = ai.dir;
    Vector2 c = map.InterCenter(ai.ti, ai.tj);
    float along = Dot(v.pos - c, DirVec(d));
    if (along > -ROAD_HALF - 90) return fail();                 // too close to the junction
    int od = (d + 2) & 3;
    int bi = ai.ti - DX(d), bj = ai.tj - DY(d);                 // junction behind us
    if (!map.ValidInter(bi, bj)) return fail();
    Vector2 start = c + DirVec(d) * along + RightV(d) * LANE_OFFSET;
    Vector2 end = start - RightV(d) * (LANE_OFFSET * 2);
    // is the opposite lane clear?
    for (const Vehicle& o : g.vehicles) {
        if (!o.active || &o == &v) continue;
        Vector2 rel = o.pos - end;
        if (fabsf(Dot(rel, RightV(d))) < 40 && Dot(rel, DirVec(od)) > -80 && Dot(rel, DirVec(od)) < 260) return fail();
    }
    StartPath(v, start, DirVec(d));
    Vector2 ctrl = start + DirVec(d) * 70.0f - RightV(d) * LANE_OFFSET;
    for (int k = 1; k <= 12; k++) { Waypoint t; t.p = QuadBezier(start, ctrl, end, k / 12.0f); t.turn = true; Push(ai, t); }
    // Every pose along the turn, plus a body length beyond it, must miss other cars.
    float axle = v.length * 0.32f, turnEnd = ai.path.back().cum + v.length;
    for (float s = ai.s; s <= turnEnd; s += 8) {
        Vector2 fp = Sample(ai, s + axle), rp = Sample(ai, s - axle);
        Vector2 dir = Norm(fp - rp);
        if (Len2(dir) < 0.5f) continue;
        OBB pose = MakeOBB((fp + rp) * 0.5f, AngleOf(dir), v.width * 0.5f + 3, v.length * 0.5f + 3);
        for (const Vehicle& o : g.vehicles) {
            if (!o.active || &o == &v || Len2(o.pos - pose.c) > 200 * 200) continue;
            Vector2 n; float depth;
            if (OBBOverlap(pose, o.Box(), n, depth)) return fail();
        }
    }
    ai.dir = od; ai.ti = bi; ai.tj = bj;
    PlanNext(v, map, nullptr);
    ai.blend = 0.6f; ai.blendPos = v.pos; ai.blendAng = v.angle;
    ai.uturnCooldown = 20;
    return true;
}

void AIKnock(Vehicle& v) {
    if (!v.ai.rail) return;
    DriverAI& ai = v.ai;
    ai.rail = false;
    v.recoveryTracked = true;
    RecoveryReset(ai.recovery);
    ai.dynTimer = 0;
    ai.blend = 0;
    ai.shove = ai.recover = ai.gearTimer = ai.jammed = ai.retry = 0;
    // A role in a rail conflict ends with the rail: the physical car plans for itself.
    ai.yieldTo = -1; ai.retreatLeft = 0; ai.mutualTime = ai.yieldClear = 0; ai.yieldDepth = 0; ai.waitingOn = -1;
    v.in = VehicleInput{};
}

// -------------------------------------------------------------------------------------
//  Junction logic
// -------------------------------------------------------------------------------------
static bool InBox(Vector2 p, Vector2 c, float grow = 6) {
    return fabsf(p.x - c.x) < ROAD_HALF + grow && fabsf(p.y - c.y) < ROAD_HALF + grow;
}

// May vehicle 'self' enter the junction of stop waypoint w now? 'blocker' receives the
// vehicle that keeps it out (a wait-for edge for gridlock detection).
static bool JunctionClear(Game& g, int self, const Waypoint& w, int* blocker) {
    *blocker = -1;
    const Vehicle& v = g.vehicles[self];
    Vector2 c = g.map.InterCenter(w.si, w.sj);
    float myDir = AngleOf(DirVec(w.d));
    for (int k = 0; k < (int)g.vehicles.size(); k++) {
        if (k == self) continue;
        const Vehicle& o = g.vehicles[k];
        if (!o.active || o.wrecked) continue;
        bool isPlayer = g.player.inVehicle && g.player.vehicle == k;
        bool mover = AIOnRail(o) || o.driver == DriverType::Police || (isPlayer && o.Speed() > 20);
        if (!mover || Len2(o.pos - c) > 420 * 420) continue;
        float diff = fabsf(WrapAngle(o.angle - myDir));
        if (InBox(o.pos, c)) {
            if (diff < 0.6f) continue;                                         // same way: follow through
            bool opposite = diff > PI - 0.6f;
            int theirTurn = AIOnRail(o) ? o.ai.curTurn : 0;
            if (opposite && w.turnType != 2 && theirTurn != 2) continue;       // straight/right vs straight/right
            *blocker = k;
            return false;
        }
        // left turns yield to oncoming cars that are about to come through
        if (w.turnType == 2 && AIOnRail(o) && o.ai.dir == ((w.d + 2) & 3) && o.ai.ti == w.si && o.ai.tj == w.sj) {
            bool coming = g.map.SignalState(w.si, w.sj, w.axis) != SIG_RED && o.ai.speed > 30;
            if (coming && Dist(o.pos, c) < 260) { *blocker = k; return false; }
        }
    }
    // don't block the box: is there room on the exit lane?
    Vector2 X = c + DirVec(w.d2) * ROAD_HALF + RightV(w.d2) * LANE_OFFSET;
    for (int k = 0; k < (int)g.vehicles.size(); k++) {
        if (k == self) continue;
        const Vehicle& o = g.vehicles[k];
        if (!o.active) continue;
        Vector2 rel = o.pos - X;
        float along = Dot(rel, DirVec(w.d2)), lat = fabsf(Dot(rel, RightV(w.d2)));
        if (along > -10 && along < v.length + 40 && lat < 26 && o.Speed() < 40) { *blocker = k; return false; }
    }
    return true;
}

// -------------------------------------------------------------------------------------
//  Look along our future path for anything in the way.
// -------------------------------------------------------------------------------------
struct Obstacle { float gap = 1e9f; float speed = 0; int vehicle = -1; bool isPlayer = false; bool isStatic = false; bool isPed = false; };

static Obstacle ScanPath(Game& g, int self, float lookAhead, float lateralShift, bool watchPeople) {
    const Vehicle& v = g.vehicles[self];
    const DriverAI& ai = v.ai;
    Obstacle best;
    const float step = 12.0f;
    int n = (int)(lookAhead / step) + 1;
    float front = ai.s + v.length * 0.5f;
    float halfW = v.width * 0.5f + 4.0f;
    float pedReach = halfW + PED_RADIUS;
    // The sample points first, and the box around them: only vehicles and people that
    // can reach that box are tested at every sample. The order of the tests, and so
    // the obstacle found, is the same as testing everyone at every sample.
    struct PathSample { Vector2 p; float hd; OBB body; };
    static std::vector<PathSample> samples;
    samples.clear();
    PathCursor cursor;
    float lastHd = 0; Vector2 lastSide = Perp(Forward(0.0f));
    Rectangle area{};
    // During a lane change each point is looked at where the car will be when it gets there.
    bool profiled = lateralShift == ai.laneShift && ai.shiftFrom != ai.shiftTo;
    float kerbSide = profiled ? std::max(ai.shiftFrom, ai.shiftTo) : lateralShift;   // > 0: onto the sidewalk
    for (int i = 0; i < n; i++) {
        float hd = 0;
        Vector2 p = SampleAt(ai, front + i * step, cursor, &hd);
        if (hd != lastHd) { lastHd = hd; lastSide = Perp(Forward(hd)); }
        if (profiled) {
            // The front of the body when it gets there: the pose d further on, plus half a length.
            Vector2 heading;
            Vector2 centre = ShiftedRailPose([&](float x) { return Sample(ai, x); }, ai.s + i * step, v.length,
                                             ai.shiftFrom, ai.shiftTo, ai.shiftS0, ai.shiftS1, v.angle, &heading);
            p = centre + heading * (v.length * 0.5f);
            hd = AngleOf(heading);
            samples.push_back({ p, hd, MakeOBB(centre, hd, v.width * 0.5f + 4, v.length * 0.5f + 4) });
        } else {
            p = p + lastSide * lateralShift;
            samples.push_back({ p, hd, OBB{} });
        }
        if (i == 0) area = { p.x, p.y, 0, 0 };
        float x0 = std::min(area.x, p.x), y0 = std::min(area.y, p.y);
        float x1 = std::max(area.x + area.width, p.x), y1 = std::max(area.y + area.height, p.y);
        area = { x0, y0, x1 - x0, y1 - y0 };
    }
    // A sample inside a box grown by halfW lies within the grown box's circumcircle.
    static std::vector<int> cands;
    static std::vector<OBB> boxes;
    cands.clear(); boxes.clear();
    for (int k = 0; k < (int)g.vehicles.size(); k++) {
        const Vehicle& o = g.vehicles[k];
        if (k == self || !o.active) continue;
        if (Len2(o.pos - v.pos) >= (lookAhead + 220) * (lookAhead + 220)) continue;
        float hw = o.width * 0.5f + halfW, hl = o.length * 0.5f + halfW;
        float r = sqrtf(hw * hw + hl * hl) + 0.5f;
        if (o.pos.x < area.x - r || o.pos.x > area.x + area.width + r ||
            o.pos.y < area.y - r || o.pos.y > area.y + area.height + r) continue;
        cands.push_back(k); boxes.push_back(o.Box());
    }
    static std::vector<int> people;
    people.clear();
    if (watchPeople)
        g.pedGrid.QueryRect(area.x - pedReach, area.y - pedReach, area.x + area.width + pedReach,
                            area.y + area.height + pedReach, [&](int k) {
            const Pedestrian& pd = g.peds[k];
            if (!pd.active || pd.state == PedState::Dead) return;
            if (g.map.TileAt(pd.pos) != Tile::Road && kerbSide <= 0) return;   // on the sidewalk: ignore
            people.push_back(k);
        });
    for (int i = 0; i < n; i++) {
        float d = i * step;
        Vector2 p = samples[i].p;
        float hd = samples[i].hd;
        for (size_t c = 0; c < cands.size(); c++) {
            int k = cands[c];
            const Vehicle& o = g.vehicles[k];
            // Changing lane, the yawed body itself is tested, as when the change was planned.
            Vector2 n; float depth;
            if (profiled ? !OBBOverlap(samples[i].body, boxes[c], n, depth) : !PointInOBB(boxes[c], p, halfW)) continue;
            if (d < best.gap) {
                bool isPlayer = g.player.inVehicle && g.player.vehicle == k;
                best = Obstacle{};
                best.gap = d; best.vehicle = k; best.isPlayer = isPlayer;
                best.isStatic = !isPlayer && !AIOnRail(o) && o.driver != DriverType::Police && o.Speed() < 8;
                best.speed = Dot(o.vel, Forward(hd));
            }
        }
        if (best.gap <= d) break;
        if (watchPeople) {
            bool seen = false;
            for (int k : people)
                if (Len2(g.peds[k].pos - p) <= pedReach * pedReach) { seen = true; break; }
            if (seen) { best = Obstacle{}; best.gap = d; best.isPed = true; }
            if (!g.player.inVehicle && Len2(g.player.pos - p) < pedReach * pedReach &&
                (g.map.TileAt(g.player.pos) == Tile::Road || kerbSide > 0) && d < best.gap) {
                best = Obstacle{}; best.gap = d; best.isPlayer = true; best.isPed = true;
            }
        }
        if (best.gap <= d) break;
    }
    return best;
}

// Can we drive along our path shifted sideways by 'shift' (checks vehicles and, for
// shifts onto the sidewalk, street furniture) over the next 'len' px?
static bool SideClear(Game& g, int self, float shift, float len, bool sidewalk) {
    const Vehicle& v = g.vehicles[self];
    float front = v.ai.s;
    static std::vector<int> ids;
    for (float d = 0; d < len; d += 16) {
        float hd = 0;
        Vector2 p = Sample(v.ai, front + d, &hd) + Perp(Forward(hd)) * shift;
        for (int k = 0; k < (int)g.vehicles.size(); k++) {
            if (k == self || !g.vehicles[k].active) continue;
            const Vehicle& o = g.vehicles[k];
            if (Len2(o.pos - p) > 200 * 200) continue;
            if (PointInOBB(o.Box(), p, v.width * 0.5f + 6)) return false;
            if (!sidewalk && o.Speed() > 20 && Dot(o.vel, Forward(hd)) < -20 && Dist(o.pos, p) < 220) return false;   // oncoming
        }
        if (sidewalk) {
            g.map.QueryObjects({ p.x - 30, p.y - 30, 60, 60 }, ids);
            for (int k : ids) if (Dist(g.map.objects[k].pos, p) < g.map.objects[k].radius + v.width * 0.5f + 2) return false;
            if (g.map.PointInBuilding(p, v.width * 0.5f)) return false;
        }
    }
    return true;
}

// -------------------------------------------------------------------------------------
//  Cooperative yielding: two drivers stopped behind each other get stable roles. The
//  yielder retraces its own path (still on rails, checked behind) and, if it was
//  passing, tucks back into its lane; the other driver proceeds through the space.
// -------------------------------------------------------------------------------------
namespace {
struct YieldSettings {
    float enabled = 1;            // 0 restores the pre-yielding stand-off (baseline runs)
    float detect = 0.5f;          // s a mutual wait must persist before roles are taken
    float clear = 1.0f;           // s the conflict must look resolved before resuming
    float speed = 60;             // px/s reversing speed
    float accel = 120;            // px/s^2 reversing acceleration
    float extra = 40;             // px beyond a knocked car's length to retreat
    float chain = 3;              // longest chain of drivers backing up together
    float cycles = 1;             // 0: no roles for two knocked cars or longer wait-for cycles
    float minRoom = 6;            // px a knocked car must be able to move to make room
};

const YieldSettings& YieldTuning() {
    static YieldSettings s;
    static bool loaded = false;
    if (loaded) return s;
    loaded = true;
    TrafficField fields[] = {
        { "enabled", &s.enabled, 0, 1 },
        { "detect_time", &s.detect, 0.1f, 3 },
        { "clear_time", &s.clear, 0.2f, 5 },
        { "retreat_speed", &s.speed, 20, 120 },
        { "retreat_accel", &s.accel, 40, 400 },
        { "retreat_extra", &s.extra, 0, 200 },
        { "max_chain", &s.chain, 1, 6 },
        { "cycles", &s.cycles, 0, 1 },
        { "min_room", &s.minRoom, 2, 60 }
    };
    LoadTrafficRecords("YIELD", fields, (int)(sizeof(fields) / sizeof(fields[0])));
    return s;
}

// Path kept behind a rail car, beyond its own body, so it can back up.
float RetreatKeep(const Vehicle& v) { return std::max(240.0f, v.length * 3.0f); }
} // namespace

// Does 'o' wait on vehicle 'idx'? A rail car waits on the car it is stopped behind; a
// holding knocked car on any vehicle that rejected at least two of its rollouts (it may
// be hemmed in by several). A yielder is acting, not waiting.
static bool WaitsOn(const Vehicle& o, int idx) {
    if (!o.active || o.driver != DriverType::Traffic || o.wrecked || o.burning || o.ai.yieldTo >= 0) return false;
    if (o.ai.rail) return o.ai.waitingOn == idx;
    const RecoveryState& r = o.ai.recovery;
    if (r.gear != 0 || r.reason != RecoveryReason::NoFeasibleManoeuvre) return false;
    for (size_t k = 0; k < r.blockIds.size(); k++)
        if (r.blockIds[k] == idx + 1 && r.blockVotes[k] >= 2) return true;
    return false;
}

int AIWaitTarget(const Game& g, int idx) {
    const Vehicle& v = g.vehicles[idx];
    if (!v.active || v.driver != DriverType::Traffic || v.wrecked || v.burning || v.ai.yieldTo >= 0) return -1;
    int target = v.ai.rail ? v.ai.waitingOn : v.ai.recovery.gear == 0 ? v.ai.recovery.blockedBy : -1;
    return target >= 0 && target < (int)g.vehicles.size() && g.vehicles[target].active ? target : -1;
}

// Free distance the rail car can reverse along its path: limited by the retained path
// (the rear axle sample must stay on it) and by people or vehicles behind, 14 px short.
static float RetreatRoom(Game& g, int self, int* blocker = nullptr) {
    const Vehicle& v = g.vehicles[self];
    const DriverAI& ai = v.ai;
    if (blocker) *blocker = -1;
    if (ai.path.size() < 2) return 0;
    float tail = ai.s - v.length * 0.32f - ai.path.front().cum - 2;
    float limit = std::clamp(tail, 0.0f, RetreatKeep(v));
    if (ai.shiftFrom != ai.shiftTo) {
        // Changing lane in reverse the body yaws, so the swept body itself is tested.
        for (float d = 8; d <= limit + 14; d += 8) {
            Vector2 heading;
            Vector2 pos = ShiftedRailPose([&](float x) { return Sample(ai, x); }, ai.s - d, v.length,
                                          ai.shiftFrom, ai.shiftTo, ai.shiftS0, ai.shiftS1, v.angle, &heading);
            OBB box = MakeOBB(pos, AngleOf(heading), v.width * 0.5f + 3, v.length * 0.5f + 3);
            Vector2 n; float depth;
            for (int k = 0; k < (int)g.vehicles.size(); k++) {
                const Vehicle& o = g.vehicles[k];
                if (k == self || !o.active || Len2(o.pos - pos) > 300 * 300 || !OBBOverlap(box, o.Box(), n, depth)) continue;
                if (blocker) *blocker = k;
                return std::clamp(d - 14, 0.0f, limit);
            }
            bool person = false;
            g.pedGrid.Query(pos, v.length * 0.5f + PED_RADIUS + 3, [&](int k) {
                const Pedestrian& pd = g.peds[k];
                if (pd.active && pd.state != PedState::Dead && PointInOBB(box, pd.pos, PED_RADIUS)) person = true;
            });
            if (!g.player.inVehicle && PointInOBB(box, g.player.pos, PED_RADIUS)) person = true;
            if (person) return std::clamp(d - 14, 0.0f, limit);
        }
        return limit;
    }
    float rear = ai.s - v.length * 0.5f, halfW = v.width * 0.5f + 4;
    for (float d = 0; d <= limit + 14; d += 8) {
        float hd = 0;
        Vector2 p = Sample(ai, rear - d, &hd) + Perp(Forward(hd)) * ai.laneShift;
        for (int k = 0; k < (int)g.vehicles.size(); k++) {
            if (k == self || !g.vehicles[k].active) continue;
            const Vehicle& o = g.vehicles[k];
            if (Len2(o.pos - p) > 160 * 160 || !PointInOBB(o.Box(), p, halfW)) continue;
            if (blocker) *blocker = k;
            return std::clamp(d - 14, 0.0f, limit);
        }
        bool person = false;
        g.pedGrid.Query(p, halfW + PED_RADIUS, [&](int k) {
            const Pedestrian& pd = g.peds[k];
            if (pd.active && pd.state != PedState::Dead && Len2(pd.pos - p) < (halfW + PED_RADIUS) * (halfW + PED_RADIUS)) person = true;
        });
        if (!g.player.inVehicle && Len2(g.player.pos - p) < (halfW + PED_RADIUS) * (halfW + PED_RADIUS)) person = true;
        if (person) return std::clamp(d - 14, 0.0f, limit);
    }
    return limit;
}

static void PoseOnPath(Vehicle& v, float dt);

// Pull out round an obstacle into 'target': an S-curve that starts at the rear axle.
// Standing close behind the obstacle, the car backs up first, just far enough for the
// swept body to clear it (up to 1.5 lengths, if the path behind is free); without a
// clear way it stays behind the obstacle.
static bool PlanPullOut(Game& g, int idx, float target, bool sidewalk) {
    Vehicle& v = g.vehicles[idx];
    DriverAI& ai = v.ai;
    float axle = v.length * 0.32f;
    float length = ShiftLength(v, target - ai.laneShift, 0);
    float room = std::min(RetreatRoom(g, idx), v.length * 1.5f);
    for (float back = 0; back <= room; back += 8) {
        float start = ai.s - back, s0 = start - axle, s1 = s0 + length;
        // The look-ahead keeps 4 px round the front: plan with a little more, so that it
        // never stops a car halfway out.
        if (!ShiftSweepClear(g, idx, start, s1 + axle + v.length * 0.5f, ai.laneShift, target, s0, s1, sidewalk, false, 7)) continue;
        if (back == 0) { StartShift(v, target, 1, 0); ai.laneShiftTarget = target; }
        else { ai.pullBack = back; ai.pullShift = target; }
        if (g.debugContacts)
            TraceLog(LOG_INFO, "PULL-OUT #%d %s to shift %.0f over %.0f px, backing up %.0f px first t=%.2f",
                     idx, v.S().name.c_str(), target, length, back, g.time);
        return true;
    }
    return false;
}

// Backing up before a pull-out: reverse along the driven path, checked behind like a
// yielding driver, then start the S-curve from where the car stopped.
static bool UpdatePullBack(Game& g, int idx, float dt) {
    Vehicle& v = g.vehicles[idx];
    DriverAI& ai = v.ai;
    float room = RetreatRoom(g, idx);
    if (room < 2 && ai.speed < 1) { ai.pullBack = 0; return false; }   // someone came behind: stay
    float want = std::min(40.0f, sqrtf(2 * COMFORT_DECEL * std::max(0.0f, std::min(room, ai.pullBack) - 1)));
    float speed = want > ai.speed ? std::min(want, ai.speed + 120 * dt) : std::max(want, ai.speed - COMFORT_DECEL * 1.6f * dt);
    float ds = std::min(speed * dt, ai.pullBack);
    ai.s -= ds; ai.pullBack -= ds; ai.speed = speed;
    PoseOnPath(v, dt);
    v.speedFwd = -speed; v.slip = 0; v.reversing = speed > 1; v.braking = speed < 1;
    v.in = VehicleInput{};
    ai.reason = 7; ai.waitingOn = -1; ai.blocker = -1; ai.stopDist = 0;
    v.rpm = Lerpf(v.rpm, 0.25f + 0.4f * Saturate(speed / 60.0f), Damp(4, dt));
    if (ai.pullBack <= 1.5f && speed < 1) {           // the braking curve stops within a pixel
        ai.pullBack = 0; ai.speed = 0;
        StartShift(v, ai.pullShift, 1, 0);
        ai.laneShiftTarget = ai.pullShift;
    }
    return true;
}

// Where, reversing, the rear axle is back in the lane: the end of a reversing S-curve to
// zero, or the start of a pull-out from zero that is retraced; NaN for neither.
static float TuckEnd(const DriverAI& ai) {
    bool backward = ai.shiftS1 < ai.shiftS0;
    if (ai.shiftTo == 0 && ai.shiftFrom != 0 && backward) return ai.shiftS1;
    if (ai.shiftFrom == 0 && ai.shiftTo != 0 && !backward) return ai.shiftS0;
    return NAN;
}

// A yielding driver out of its lane returns to it in reverse: back along its pull-out
// if it is still on it, otherwise over a new reversing S-curve; only when the swept
// way back is free of vehicles and people.
static void PlanTuckIn(Game& g, int idx) {
    Vehicle& v = g.vehicles[idx];
    DriverAI& ai = v.ai;
    float axle = v.length * 0.32f, rear = ai.s - axle;
    float from = ai.shiftFrom, to = ai.shiftTo, s0 = ai.shiftS0, s1 = ai.shiftS1;
    bool retrace = from == 0 && to != 0 && s1 > s0 && rear > s0 && rear - s0 <= RetreatKeep(v);
    if (!retrace) {
        float current = LaneShiftAt(from, to, s0, s1, rear);
        from = current; to = 0; s0 = rear; s1 = rear - ShiftLength(v, current, YieldTuning().speed);
    }
    float end = (retrace ? s0 : s1) + axle;                           // the rear axle back at the lane
    if (end - axle < ai.path.front().cum + 2) return;                 // not enough path kept behind
    // A wider margin than the room check while reversing (3 px), so a planned tuck-in is
    // never stopped halfway by that check.
    if (!ShiftSweepClear(g, idx, ai.s, end, from, to, s0, s1, false, true, 5)) return;
    ai.shiftFrom = from; ai.shiftTo = to; ai.shiftS0 = s0; ai.shiftS1 = s1;
    ai.laneShiftTarget = 0;
}

// Stable role choice for a mutual pair: a knocked car gets room from the rail car;
// between rail cars the one further out of its lane (passing) backs up; then the one
// with retreat space; then a fixed index order.
static bool ShouldYield(Game& g, int a, int b) {
    const Vehicle& A = g.vehicles[a];
    const Vehicle& B = g.vehicles[b];
    if (!AIOnRail(A)) return false;
    if (!AIOnRail(B)) return true;
    float sa = fabsf(A.ai.laneShift), sb = fabsf(B.ai.laneShift);
    if (sa > sb + 4) return true;
    if (sb > sa + 4) return false;
    float ra = RetreatRoom(g, a), rb = RetreatRoom(g, b);
    if (ra < 20 && rb >= 60) return false;
    if (rb < 20 && ra >= 60) return true;
    return a > b;
}

static void StartYield(Game& g, int self, int priority, float need, int depth, const char* why) {
    Vehicle& v = g.vehicles[self];
    DriverAI& ai = v.ai;
    ai.yieldTo = priority; ai.yieldSerial = g.vehicles[priority].serial;
    ai.retreatLeft = need; ai.yieldClear = 0; ai.mutualTime = 0;
    ai.waitingOn = -1;
    ai.yieldDepth = depth;
    ai.lastYieldTo = priority; ai.lastYieldSerial = ai.yieldSerial; ai.lastYieldTime = g.time;
    g.statYields++;
    if (depth > 0) g.statChainYields++;
    if (g.debugContacts)
        TraceLog(LOG_INFO, "YIELD #%d %s gives way to #%d %s: %s, retreat %.0f px, shift %.1f, depth %d t=%.2f",
                 self, v.S().name.c_str(), priority, g.vehicles[priority].S().name.c_str(), why, need, ai.laneShift, depth, g.time);
}

static void EndYield(Game& g, int self, const char* why) {
    DriverAI& ai = g.vehicles[self].ai;
    if (g.debugContacts)
        TraceLog(LOG_INFO, "YIELD #%d ends (%s) after giving way to #%d t=%.2f", self, why, ai.yieldTo, g.time);
    ai.yieldTo = -1; ai.retreatLeft = 0; ai.yieldClear = 0; ai.mutualTime = 0; ai.yieldDepth = 0;
    ai.speed = 0;
    ai.blocked = 0;
}

// Pose from the path at ai.s (front/rear axle samples), velocity from the pose change.
static void PoseOnPath(Vehicle& v, float dt) {
    DriverAI& ai = v.ai;
    Vector2 dir;
    Vector2 pos = ShiftedRailPose([&](float d) { return Sample(ai, d); }, ai.s, v.length,
                                  ai.shiftFrom, ai.shiftTo, ai.shiftS0, ai.shiftS1, v.angle, &dir);
    float ang = AngleOf(dir);
    ai.laneShift = LaneShiftAt(ai.shiftFrom, ai.shiftTo, ai.shiftS0, ai.shiftS1, ai.s - v.length * 0.32f);
    if (ai.blend > 0) {
        ai.blend = std::max(0.0f, ai.blend - dt * 1.4f);
        float t = SmoothStep(0, 1, ai.blend);
        pos = LerpV(pos, ai.blendPos, t);
        ang = ang + WrapAngle(ai.blendAng - ang) * t;
    }
    float idt = dt > 1e-5f ? 1.0f / dt : 0.0f;
    v.vel = (pos - v.pos) * idt;
    if (Len(v.vel) > ai.speed * 1.5f + 60) v.vel = dir * (ai.yieldTo >= 0 ? -ai.speed : ai.speed);   // no velocity spikes when re-attaching
    v.angVel = WrapAngle(ang - v.angle) * idt;
    v.pos = pos;
    v.angle = ang;
}

// Runs the yielding role; false when the role ended and normal driving resumes.
static bool UpdateYield(Game& g, int idx, float dt) {
    Vehicle& v = g.vehicles[idx];
    DriverAI& ai = v.ai;
    const YieldSettings& y = YieldTuning();
    int b = ai.yieldTo;
    if (b < 0 || b >= (int)g.vehicles.size() || !g.vehicles[b].active || g.vehicles[b].serial != ai.yieldSerial) {
        EndYield(g, idx, "priority_gone"); return false;
    }
    const Vehicle& B = g.vehicles[b];
    // Resolved once the priority driver no longer waits on us and is no longer in our
    // way: passed, tucked away from, back on its lane and gone, or out of the picture.
    bool clear;
    if (B.driver != DriverType::Traffic || B.wrecked || B.burning) clear = true;
    else {
        Obstacle ahead = ScanPath(g, idx, B.length * 2 + 80, ai.laneShift, false);
        clear = !WaitsOn(B, idx) && ahead.vehicle != b;
    }
    ai.yieldClear = clear ? ai.yieldClear + dt : 0;
    if (ai.yieldClear >= y.clear) { EndYield(g, idx, "resolved"); return false; }

    float speed = ai.speed;
    float want = 0;
    if (ai.retreatLeft > 0) {
        int behind = -1;
        float room = RetreatRoom(g, idx, &behind);
        // A driver queued close behind backs up too (a short wait-for chain).
        if (room < ai.retreatLeft && behind >= 0 && ai.yieldDepth + 1 < (int)y.chain) {
            Vehicle& q = g.vehicles[behind];
            if (AIOnRail(q) && q.ai.yieldTo < 0 && q.ai.waitingOn == idx)
                StartYield(g, behind, idx, ai.retreatLeft - room + 14, ai.yieldDepth + 1, "chain");
        }
        float stopIn = std::min(room, ai.retreatLeft);
        want = std::min(y.speed, sqrtf(2 * COMFORT_DECEL * std::max(0.0f, stopIn - 1)));
        if (room <= 1 && behind < 0 && speed < 1) ai.retreatLeft = 0;      // retained path exhausted
    }
    if (want > speed) speed = std::min(want, speed + y.accel * dt);
    else speed = std::max(want, speed - COMFORT_DECEL * 1.6f * dt);
    float ds = std::min(speed * dt, std::max(0.0f, ai.retreatLeft));
    ai.s -= ds; ai.retreatLeft -= ds;
    ai.speed = speed;
    // Passing drivers tuck back into their own lane as soon as the way back is free:
    // reversing, they retrace their pull-out if they are still on it, or steer back in
    // over a reversing S-curve. The rear axle leads; the car turns in, it does not slide.
    float rearS = ai.s - v.length * 0.32f;
    float zeroEnd = TuckEnd(ai);
    bool tucking = ai.laneShiftTarget == 0 && std::isfinite(zeroEnd) && rearS > zeroEnd + 0.5f;
    if (!tucking && fabsf(ai.laneShift) > 1) {
        PlanTuckIn(g, idx);
        zeroEnd = TuckEnd(ai);
        tucking = ai.laneShiftTarget == 0 && std::isfinite(zeroEnd) && rearS > zeroEnd + 0.5f;
    }
    if (tucking) ai.retreatLeft = std::max(ai.retreatLeft, rearS - zeroEnd);
    else if (std::isfinite(zeroEnd) && ai.laneShiftTarget == 0 && fabsf(ai.laneShift) < 1) {
        SetShiftConstant(ai, 0);             // tucked in
        ai.retreatLeft = 0;
    }
    PoseOnPath(v, dt);
    v.speedFwd = -speed;
    v.slip = 0;
    v.reversing = speed > 1;
    v.braking = speed < 1;
    v.in = VehicleInput{};
    ai.reason = 7;
    ai.waitingOn = -1;
    ai.blocker = -1;
    ai.stopDist = 0;
    v.rpm = Lerpf(v.rpm, 0.25f + 0.4f * Saturate(speed / 60.0f), Damp(4, dt));
    return true;
}

// A knocked car making room for the driver waiting on it (a role from a wait-for cycle):
// the role ends once that driver has not waited on this car for clear_time, or after
// three creeps that did not free it.
static void UpdateRoom(Game& g, int idx, float dt) {
    Vehicle& v = g.vehicles[idx];
    DriverAI& ai = v.ai;
    int b = ai.yieldTo;
    const char* why = nullptr;
    if (b < 0 || b >= (int)g.vehicles.size() || !g.vehicles[b].active || g.vehicles[b].serial != ai.yieldSerial ||
        g.vehicles[b].driver != DriverType::Traffic || !g.vehicles[b].Drivable()) why = "priority_gone";
    else {
        bool waiting = AIWaitTarget(g, b) == idx;
        ai.yieldClear = waiting ? 0 : ai.yieldClear + dt;
        if (ai.yieldClear >= YieldTuning().clear) why = "resolved";
        else if (waiting && ai.recovery.roomCreeps >= 3 && ai.recovery.gear == 0 && ai.recovery.roomRetry <= 0) why = "no_more_room";
    }
    if (!why) return;
    if (g.debugContacts)
        TraceLog(LOG_INFO, "YIELD #%d ends (%s) after making room for #%d t=%.2f", idx, why, b, g.time);
    ai.yieldTo = -1; ai.yieldClear = 0; ai.mutualTime = 0;
    RecoveryEndRoom(v);
}

// -------------------------------------------------------------------------------------
//  Wait-for cycles. The pair rule above covers two drivers stopped behind each other
//  when at least one is on rails. Two knocked cars, and loops of three or more drivers
//  (junction gridlock), are found here once per frame from the frame-start wait-for
//  edges. A loop that persists for detect_time gets one driver who gives way to the
//  driver waiting on it: a rail car backs up along its own path, preferably one whose
//  waiting driver is a knocked car that needs room; otherwise a knocked car that can
//  move furthest away creeps clear. A pair kept by the pair rule for longer than three
//  detection times is taken over too. A loop nobody can open is assessed again every
//  second and stays observable.
// -------------------------------------------------------------------------------------
void AIResolveWaitCycles(Game& g, float dt) {
    const YieldSettings& y = YieldTuning();
    int n = (int)g.vehicles.size();
    static std::vector<int> cycleOf, walk, target;
    cycleOf.assign(n, -1); walk.assign(n, -1); target.resize(n);
    for (int k = 0; k < n; k++) target[k] = AIWaitTarget(g, k);
    int cycles = 0;
    for (int start = 0; start < n; start++) {
        int k = start;
        while (k >= 0 && walk[k] < 0 && cycleOf[k] < 0) { walk[k] = start; k = target[k]; }
        if (k >= 0 && walk[k] == start && cycleOf[k] < 0) {
            for (int m = k; cycleOf[m] < 0; m = target[m]) cycleOf[m] = cycles;
            cycles++;
        }
    }
    for (int k = 0; k < n; k++) {
        DriverAI& ai = g.vehicles[k].ai;
        ai.cycleTime = cycleOf[k] >= 0 ? ai.cycleTime + dt : 0;
        ai.cycleCheck = std::max(0.0f, ai.cycleCheck - dt);
    }
    if (y.enabled < 0.5f || y.cycles < 0.5f) return;
    for (int c = 0; c < cycles; c++) {
        int length = 0, rail = 0, first = -1;
        float age = 1e9f;
        for (int k = 0; k < n; k++) {
            if (cycleOf[k] != c) continue;
            if (first < 0) first = k;
            length++;
            if (AIOnRail(g.vehicles[k])) rail++;
            age = std::min(age, g.vehicles[k].ai.cycleTime);
        }
        bool pairRule = length == 2 && rail > 0;
        if (age < (pairRule ? y.detect * 3 : y.detect) || g.vehicles[first].ai.cycleCheck > 0) continue;
        // The driver giving way, chosen for the one waiting on it (its predecessor).
        int best = -1, bestPred = -1;
        float bestScore = -1;
        for (int k = 0; k < n; k++) {
            if (cycleOf[k] != c) continue;
            int pred = -1;
            for (int m = 0; m < n; m++) if (cycleOf[m] == c && target[m] == k) pred = m;
            if (pred < 0) continue;
            const Vehicle& v = g.vehicles[k];
            float score;
            if (AIOnRail(v)) {
                float room = RetreatRoom(g, k);
                if (room < 30) continue;
                score = 2000 + room + (AIOnRail(g.vehicles[pred]) ? 0.0f : 1000.0f);
            } else {
                // A knocked pair keeps its roles: the car that made room before does so again.
                bool before = v.ai.lastYieldTo == pred && v.ai.lastYieldSerial == g.vehicles[pred].serial &&
                              g.time - v.ai.lastYieldTime < 30;
                float room = RecoveryFreeRoom(g, k, pred);
                if (room < y.minRoom) continue;
                score = room + (before ? 1000.0f : 0.0f);
            }
            if (score > bestScore || (score == bestScore && k > best)) { bestScore = score; best = k; bestPred = pred; }
        }
        if (best < 0) {
            for (int k = 0; k < n; k++) if (cycleOf[k] == c) g.vehicles[k].ai.cycleCheck = 1.0f;
            if (g.debugContacts)
                TraceLog(LOG_INFO, "CYCLE from #%d (%d drivers) cannot be opened now t=%.2f", first, length, g.time);
            continue;
        }
        Vehicle& v = g.vehicles[best];
        const Vehicle& p = g.vehicles[bestPred];
        if (AIOnRail(v)) {
            float need = AIOnRail(p) ? std::max(v.length * 0.5f, 40.0f) + y.extra : p.length + y.extra;
            StartYield(g, best, bestPred, need, 0, length == 2 ? "stuck_pair" : "wait_cycle");
        } else {
            v.ai.yieldTo = bestPred; v.ai.yieldSerial = p.serial; v.ai.yieldClear = 0; v.ai.mutualTime = 0;
            v.ai.lastYieldTo = bestPred; v.ai.lastYieldSerial = p.serial; v.ai.lastYieldTime = g.time;
            RecoveryStartRoom(v, bestPred);
            g.statYields++;
            if (g.debugContacts)
                TraceLog(LOG_INFO, "YIELD #%d %s makes room for #%d %s: %s of %d drivers t=%.2f", best, v.S().name.c_str(),
                         bestPred, p.S().name.c_str(), length == 2 ? "knocked pair" : "wait cycle", length, g.time);
        }
        for (int k = 0; k < n; k++) if (cycleOf[k] == c) g.vehicles[k].ai.cycleTime = 0;
    }
}

// -------------------------------------------------------------------------------------
//  Traffic update
// -------------------------------------------------------------------------------------
//  Physical recovery: stabilise, plan a checked manoeuvre, or hold indefinitely.
//  A recovery deadline is never a reason to remove the driver or relocate the car.
// -------------------------------------------------------------------------------------
static void UpdateKnocked(Game& g, int idx, float dt) {
    Vehicle& v = g.vehicles[idx];
    DriverAI& ai = v.ai;
    v.recoveryTracked = true;
    ai.dynTimer += dt;
    v.in = VehicleInput{};
    float speed = Dot(v.vel, v.Fwd());
    // Stopping to get out after an incident: brake and hold; no manoeuvre meanwhile.
    bool incidentStop = IncidentHoldsVehicle(v);
    if (incidentStop || (ai.recover <= 0 && (ai.dynTimer < 0.7f || (v.Speed() > 18 && ai.dynTimer < 3.0f)))) {
        // At low speed the arcade pedals engage the opposite gear; hold instead.
        v.in.brake = speed > 40 ? 1.0f : 0.0f;
        v.in.throttle = speed < -40 ? 1.0f : 0.0f;
        v.in.handbrake = fabsf(speed) < 60;
        return;
    }
    ai.recover += dt;
    if (!ai.recovery.initialized) AIResetPath(v, g.map);
    Vector2 forward = ai.recovery.initialized ? ai.recovery.forward : DirVec(ai.dir);
    Vector2 origin = ai.recovery.initialized ? ai.recovery.origin
        : g.map.InterCenter(ai.ti, ai.tj) + RightV(ai.dir) * LANE_OFFSET;

    // Reattachment is permitted only after actual controls align the body. Start the
    // rail at the actual pose with a straight tail/front segment, so both axle samples
    // reproduce that pose exactly. There is no blend or correction to a lane pose.
    ai.retry -= dt;
    if (ai.retry <= 0) {
        ai.retry = 0.25f;
        float lateral = fabsf(Dot(v.pos - origin, Perp(forward)));
        float heading = fabsf(WrapAngle(v.angle - AngleOf(forward)));
        bool aligned = lateral <= 4 && heading <= 0.12f;
        // Recovering past a junction must use the next junction on the current block.
        // The frozen corridor still guides recovery; only route construction changes.
        if (aligned) AIResetPath(v, g.map);
        Vector2 centre = g.map.InterCenter(ai.ti, ai.tj);
        float along = Dot(v.pos - centre, forward);
        float lead = std::max(v.length, 60.0f);
        bool ready = aligned && along <= -ROAD_HALF - lead - 10 &&
            speed >= -2 && fabsf(Dot(v.vel, Perp(forward))) <= 6 && fabsf(v.angVel) <= 0.2f;
        ai.recovery.rejoinForecastTested = ready;
        ai.recovery.rejoinForecastClear = ready && RecoveryCanRejoin(g, idx, dt);
        if (ai.recovery.rejoinForecastClear) {
            StartPath(v, v.pos, v.Fwd());
            Waypoint ahead; ahead.p = v.pos + v.Fwd() * lead; Push(ai, ahead);
            PlanNext(v, g.map, nullptr);
            ai.rail = true;
            ai.speed = std::max(0.0f, speed);
            ai.blend = 0;
            SetShiftConstant(ai, 0); ai.laneShiftTarget = 0; ai.pullBack = 0;
            ai.recover = ai.gearTimer = ai.jammed = 0;
            if (ai.yieldTo >= 0 && g.debugContacts)
                TraceLog(LOG_INFO, "YIELD #%d ends (rejoined) after making room for #%d t=%.2f", idx, ai.yieldTo, g.time);
            ai.yieldTo = -1; ai.yieldClear = 0;
            RecoveryReset(ai.recovery);
            v.recoveryTracked = false;
            v.in = VehicleInput{};
            g.statRejoins++;
            if (g.debugContacts) TraceLog(LOG_INFO, "RECOVERY #%d rejoined at actual pose t=%.2f", idx, g.time);
            return;
        }
    }
    if (ai.yieldTo >= 0) UpdateRoom(g, idx, dt);
    else if (ai.recovery.roomFor >= 0) RecoveryEndRoom(v);   // the role was cleared elsewhere (an incident)
    RecoveryReason previous = ai.recovery.reason;
    int previousGear = ai.recovery.gear;
    RecoveryDrive(g, idx, origin, forward, dt);
    ai.gearTimer = ai.recovery.gear < 0 ? 1.0f : 0.0f;
    ai.reason = ai.recovery.gear == 0 ? 3 : 0;
    if (g.debugContacts && (previous != ai.recovery.reason || previousGear != ai.recovery.gear))
        TraceLog(LOG_INFO, "RECOVERY #%d %s reason=%s gear=%d steer=%.2f plans=%d rejected=%d t=%.2f",
                 idx, v.S().name.c_str(), RecoveryReasonText(ai.recovery.reason), ai.recovery.gear,
                 v.in.steer, ai.recovery.plans, ai.recovery.rejected, g.time);
}

void AIUpdateTraffic(Game& g, int idx, float dt) {
    Vehicle& v = g.vehicles[idx];
    const CityMap& map = g.map;
    DriverAI& ai = v.ai;
    Rng& r = GRng();
    v.headlights = g.dn.night > 0.35f;
    if (!ai.rail) { UpdateKnocked(g, idx, dt); return; }

    // ---- keep enough path ahead, drop what is far behind ----
    while (ai.path.empty() || ai.path.back().cum - ai.s < 520) {
        if (ai.path.empty()) StartPath(v, v.pos, Forward(v.angle));
        PlanNext(v, map, nullptr);
    }
    // Keep the driven path behind the car as well: a yielding driver retraces it.
    while (ai.path.size() > 3 && ai.path[1].cum < ai.s - v.length - 60 - RetreatKeep(v)) {
        float base = ai.path[1].cum;
        ai.path.pop_front();
        for (Waypoint& w : ai.path) w.cum -= base;
        ai.s -= base;
        ai.shiftS0 -= base; ai.shiftS1 -= base;
    }
    ai.uturnCooldown = std::max(0.0f, ai.uturnCooldown - dt);
    if (ai.yieldTo >= 0 && UpdateYield(g, idx, dt)) return;
    if (ai.pullBack > 0 && UpdatePullBack(g, idx, dt)) return;
    // now and then a driver looks at their phone... (accidents happen)
    if (ai.distracted > 0) ai.distracted -= dt;
    else if (r.Chance(dt * 0.004f * ai.temper)) ai.distracted = r.Range(1.0f, 2.5f);

    // ---- desired speed ----
    const VehicleSpec& sp = v.S();
    float desired = ai.cruise * (ai.panic > 0 ? 1.5f : 1.0f);
    ai.panic = std::max(0.0f, ai.panic - dt);
    ai.reason = 0;
    ai.stopDist = 1e9f;
    int junctionBlocker = -1;               // the vehicle keeping us out of the junction box
    float front = ai.s + v.length * 0.5f;
    for (size_t k = 0; k < ai.path.size(); k++) {
        const Waypoint& w = ai.path[k];
        float gap = w.cum - front;
        if (gap > 320) break;
        if (w.turn && gap > -v.length) desired = std::min(desired, (sp.large() ? 95.0f : 130.0f) + std::max(0.0f, gap) * 1.1f);
        if (w.stop) {
            float stopGap = w.cum - STOP_BACK - front;
            if (stopGap < -8) { ai.curTurn = w.turnType; continue; }       // committed: past the stop line
            int sig = map.SignalState(w.si, w.sj, w.axis);
            bool canStop = stopGap > (ai.speed * ai.speed) / (2 * COMFORT_DECEL * 1.6f) - 6;
            bool mustStop = false;
            if (ai.panic <= 0 && (sig == SIG_RED || (sig == SIG_YELLOW && canStop))) { mustStop = true; ai.reason = 1; }
            else if (stopGap < 90 && !JunctionClear(g, idx, w, &junctionBlocker)) { mustStop = true; ai.reason = w.turnType == 2 ? 4 : 6; }
            if (mustStop) {
                desired = std::min(desired, sqrtf(2 * COMFORT_DECEL * std::max(0.0f, stopGap - 2)));
                ai.stopDist = std::max(0.0f, stopGap - 2);
            }
            break;
        }
    }
    // ---- vehicles / people ahead on our path ----
    float look = 40 + ai.speed * 1.1f + v.length * 0.5f;
    Obstacle ob = ScanPath(g, idx, look, ai.laneShift, ai.distracted <= 0);
    ai.blocker = ob.gap < 30 ? ob.vehicle : -1;
    // Wait-for edge, and a mutual blockage: the two drivers stopped behind each other.
    // Held at the stop line by a car that keeps the box occupied, we wait on that car.
    ai.waitingOn = ob.vehicle >= 0 && ob.gap < 30 && ai.speed < 5 ? ob.vehicle : -1;
    if (ai.waitingOn < 0 && junctionBlocker >= 0 && ai.speed < 5 && ai.stopDist < 30) ai.waitingOn = junctionBlocker;
    if (ai.waitingOn >= 0) {
        int b = ai.waitingOn;
        const Vehicle& B = g.vehicles[b];
        bool mutual = WaitsOn(B, idx) && B.ai.yieldTo != idx;
        ai.mutualTime = mutual ? ai.mutualTime + dt : 0;
        if (YieldTuning().enabled >= 0.5f && ai.mutualTime >= YieldTuning().detect && ShouldYield(g, idx, b)) {
            bool knocked = !AIOnRail(B);
            float need = knocked ? B.length + YieldTuning().extra
                : fabsf(ai.laneShift) > 4 ? RetreatKeep(v) : B.length * 0.5f + YieldTuning().extra;
            StartYield(g, idx, b, need, 0, knocked ? "knocked_car_needs_room" : fabsf(ai.laneShift) > 4 ? "passing_head_on" : "head_on");
            UpdateYield(g, idx, dt);
            return;
        }
    } else ai.mutualTime = 0;
    if (ob.gap < 1e8f) {
        float lim = std::max(0.0f, ob.gap - 10) * 2.0f + std::max(0.0f, ob.speed) * 0.8f;
        if (ob.gap < 14) lim = 0;
        if (lim < desired) { desired = lim; if (lim < 20) ai.reason = ob.isStatic ? 5 : ob.vehicle >= 0 ? 2 : 3; }
        if (ob.isPed || ob.isStatic || ob.speed < 10) ai.stopDist = std::min(ai.stopDist, std::max(0.0f, ob.gap - 14));
    }

    // ---- getting around trouble: overtake, mount the kerb, U-turn, honk ----
    bool stoppedByBlocker = ob.gap < 70 && (ob.isStatic || (ob.vehicle >= 0 && g.vehicles[ob.vehicle].Speed() < 5 && !AIOnRail(g.vehicles[ob.vehicle])));
    if (stoppedByBlocker || (ob.vehicle >= 0 && ob.gap < 40 && ai.speed < 5 && ai.reason == 2)) ai.blocked += dt;
    else ai.blocked = std::max(0.0f, ai.blocked - dt);
    if (ai.laneShiftTarget == 0 && ai.pullBack <= 0 && stoppedByBlocker && ai.blocked > 1.0f / ai.temper) {
        const float overtake = -LANE_OFFSET * 1.9f, kerb = LANE_OFFSET * 1.35f;
        if (!(SideClear(g, idx, overtake, look + 160, false) && PlanPullOut(g, idx, overtake, false)) &&   // oncoming lane
            !sp.large() && SideClear(g, idx, kerb, look + 120, true)) PlanPullOut(g, idx, kerb, true);  // onto the sidewalk
    }
    if (ai.blocked > 6.0f && ai.uturnCooldown <= 0 && !sp.large()) {                               // give up: turn round
        if (PlanUTurn(g, v)) { ai.blocked = 0; return; }
        ai.uturnCooldown = 4;
    }
    // A lane change in progress keeps a moderate speed (its S-curve was sized for it).
    if (ai.shiftFrom != ai.shiftTo && ai.laneShiftTarget == 0) desired = std::min(desired, 130.0f);
    if (ai.laneShiftTarget != 0) {
        desired = std::min(desired, ai.laneShiftTarget > 0 ? 70.0f : 150.0f);
        bool beside = false;
        for (const Vehicle& o : g.vehicles) {
            if (!o.active || &o == &v || o.Speed() > 8 || AIOnRail(o)) continue;
            Vector2 rel = o.pos - v.pos;
            float al = Dot(rel, Forward(v.angle));
            if (al > -v.length && al < v.length + 60 && fabsf(Dot(rel, Perp(Forward(v.angle)))) < 80) beside = true;
        }
        // Passed: steer back once the whole body is out in the new lane.
        float rear = ai.s - v.length * 0.32f;
        bool settled = ai.shiftTo == ai.laneShiftTarget && (ai.shiftS1 >= ai.shiftS0 ? rear >= ai.shiftS1 : rear <= ai.shiftS1);
        if (!beside && settled) {
            // Back in the lane before the next stop line, if the bend allows.
            float room = 1e9f;
            for (const Waypoint& w : ai.path)
                if (w.stop && w.cum - STOP_BACK - (ai.s + v.length * 0.5f) > 0) { room = w.cum - STOP_BACK - ai.s - v.length; break; }
            StartShift(v, 0, 1, ai.speed, room);
            ai.laneShiftTarget = 0;
        }
    }
    // An incident: pull up where we are, then the driver gets out (traffic_incidents).
    if (IncidentHoldsVehicle(v)) { desired = 0; ai.reason = 3; ai.stopDist = 0; }
    // horn: at the player, at people in the road, and at anyone blocking us for too long
    ai.honk -= dt;
    bool annoyed = (ob.isPlayer && ob.gap < 40) || (ob.isPed && ob.gap < 30 && ai.speed < 20) || (ai.blocked > 2.5f / ai.temper);
    if (annoyed && ai.honk <= 0) {
        g.audio.Play(Sfx::Horn, v.pos, 0.55f, r.Range(0.85f, 1.2f));
        ai.honk = r.Range(2.5f, 5.0f) / ai.temper;
    }

    // ---- speed integration (smooth, jerk-free) ----
    float accel = sp.accel * 0.45f;
    float decel = desired < 1 && ob.gap < 30 ? 900.0f : COMFORT_DECEL * 1.6f;
    float prevSpeed = ai.speed;
    if (desired > ai.speed) ai.speed = std::min(desired, ai.speed + accel * dt);
    else ai.speed = std::max(desired, ai.speed - decel * dt);
    ai.speed = std::max(0.0f, ai.speed);
    ai.s += ai.speed * dt;

    // ---- pose from two points on the path (front / rear axle) ----
    PoseOnPath(v, dt);
    v.speedFwd = ai.speed;
    v.slip = 0;
    v.braking = ai.speed < prevSpeed - 0.5f || (ai.speed < 1 && desired < 1);
    v.reversing = false;
    v.in = VehicleInput{};
    v.in.throttle = desired > ai.speed ? 0.6f : 0.0f;
    v.rpm = Lerpf(v.rpm, 0.25f + 0.6f * Saturate(ai.speed / sp.maxSpeed), Damp(4, dt));
}

// -------------------------------------------------------------------------------------
//  Police (physics driven)
// -------------------------------------------------------------------------------------
static Vector2 LookAheadPts(const Vehicle& v, float L) {
    Vector2 prev = v.pos, target = v.ai.path.empty() ? v.pos + v.Fwd() * L : v.ai.path.front().p;
    float remain = L;
    for (const Waypoint& w : v.ai.path) {
        float seg = Dist(prev, w.p);
        if (seg >= remain) return prev + Norm(w.p - prev) * remain;
        remain -= seg; prev = w.p; target = w.p;
    }
    return target;
}

static void PopReached(Vehicle& v) {
    Vector2 f = v.Fwd();
    while (!v.ai.path.empty()) {
        Vector2 to = v.ai.path.front().p - v.pos;
        float d = Len(to);
        if (d < 30 || (d < 130 && Dot(to, f) < 0)) v.ai.path.pop_front(); else break;
    }
}

void AIUpdatePolice(Game& g, int idx, float dt) {
    Vehicle& v = g.vehicles[idx];
    const CityMap& map = g.map;
    DriverAI& ai = v.ai;
    v.headlights = g.dn.night > 0.35f;
    bool chasing = g.heat > 0.01f && g.state == GameState::Playing;
    v.siren = chasing;
    Vector2 tp = g.PlayerPos(), tv = g.PlayerVel();
    float dist = Dist(v.pos, tp);
    bool see = chasing && dist < 650 && map.LineOfSight(v.pos, tp);
    if (see) g.heatCooldown = 0;

    if (ai.reverse > 0) {                                        // backing out after getting wedged
        ai.reverse -= dt;
        v.in.throttle = 0; v.in.brake = 1; v.in.handbrake = false;
        return;
    }
    if (see || (chasing && dist < 160)) {
        // direct pursuit: lead the target, ram when close
        Vector2 aim = tp + tv * Clampf(dist / 700.0f, 0, 0.7f);
        float err = WrapAngle(AngleOf(aim - v.pos) - v.angle);
        v.in.steer = Clampf(err * 2.2f, -1, 1);
        float desired = dist < 120 && !g.PlayerInCar() ? 60.0f : v.S().maxSpeed;
        float e = desired - v.speedFwd;
        v.in.throttle = e > 0 ? 1.0f : 0.0f;
        v.in.brake = e < 0 ? Clampf(-e / 60, 0, 1) : 0.0f;
        v.in.handbrake = fabsf(err) > 1.1f && v.speedFwd > 280;
        ai.path.clear();
        if (chasing && dist < 400) {
            ai.honk -= dt;
            if (ai.honk <= 0 && GRng().Chance(0.3f)) { g.audio.Play(Sfx::Horn, v.pos, 0.5f, 0.8f); ai.honk = 3; }
        }
    } else {
        // road driving: pursuit along the grid choosing turns that close in, or patrol
        if (ai.path.empty()) AIResetPath(v, map);
        PopReached(v);
        while (ai.path.size() < 12) PlanNext(v, map, chasing ? &tp : nullptr);
        if (Dist(v.pos, ai.path.front().p) > 380) { AIResetPath(v, map); while (ai.path.size() < 12) PlanNext(v, map, chasing ? &tp : nullptr); }
        Vector2 target = LookAheadPts(v, 50 + fabsf(v.speedFwd) * 0.35f);
        float desired = chasing ? v.S().maxSpeed * 0.8f : 210.0f;
        float along = 0; Vector2 prev = v.pos;
        for (size_t k = 0; k < ai.path.size() && along < 260; k++) {
            along += Dist(prev, ai.path[k].p); prev = ai.path[k].p;
            if (ai.path[k].turn) { desired = std::min(desired, 200.0f + std::max(0.0f, along - 60) * 1.5f); break; }
        }
        // don't plough into traffic when not chasing
        if (!chasing) {
            for (const Vehicle& o : g.vehicles) {
                if (!o.active || &o == &v) continue;
                Vector2 rel = o.pos - v.pos;
                float al = Dot(rel, v.Fwd());
                if (al > 0 && al < 160 && fabsf(Dot(rel, Perp(v.Fwd()))) < 36) desired = std::min(desired, std::max(0.0f, al - 60) * 1.5f);
            }
        }
        float err = WrapAngle(AngleOf(target - v.pos) - v.angle);
        v.in.steer = Clampf(err * 2.0f, -1, 1);
        float e = desired - v.speedFwd;
        v.in.throttle = e > 0 ? Clampf(e / 80, 0.2f, 1.0f) : 0.0f;
        v.in.brake = e < 0 ? Clampf(-e / 60, 0, 1) : 0.0f;
        v.in.handbrake = false;
    }
    // wedged against something while trying to drive? back out once
    if (v.in.throttle > 0.5f && fabsf(v.speedFwd) < 15) ai.stuck += dt; else ai.stuck = std::max(0.0f, ai.stuck - dt * 2);
    if (ai.stuck > 1.8f) { ai.stuck = 0; ai.reverse = 1.0f; ai.path.clear(); }
}

// -------------------------------------------------------------------------------------
//  Spawning
// -------------------------------------------------------------------------------------
void AIStartRail(Game& g, Vehicle& v, float tail, bool insideJunction) {
    AIResetPath(v, g.map);
    if (insideJunction && InBox(v.pos, g.map.InterCenter(v.ai.ti, v.ai.tj), 40)) {
        v.ai.ti = std::clamp(v.ai.ti + DX(v.ai.dir), 0, INTER_X - 1);
        v.ai.tj = std::clamp(v.ai.tj + DY(v.ai.dir), 0, INTER_Y - 1);
    }
    Vector2 dir = DirVec(v.ai.dir);
    StartPath(v, v.pos - dir * tail, dir);
    PlanNext(v, g.map, nullptr);
    v.ai.s += tail;
    v.ai.rail = true;
    v.ai.blend = 0;
}

bool AIPlaceOnRoad(Game& g, Vehicle& v, Vector2 near, float minDist, float maxDist, bool offscreen) {
    const CityMap& map = g.map;
    Rng& r = GRng();
    for (int tries = 0; tries < 40; tries++) {
        bool horizontal = r.Chance(0.5f);
        int i = horizontal ? r.Int(0, INTER_X - 2) : r.Int(0, INTER_X - 1);
        int j = horizontal ? r.Int(0, INTER_Y - 1) : r.Int(0, INTER_Y - 2);
        Vector2 a = map.InterCenter(i, j), b = horizontal ? map.InterCenter(i + 1, j) : map.InterCenter(i, j + 1);
        bool fwd = r.Chance(0.5f);
        int d = horizontal ? (fwd ? 1 : 3) : (fwd ? 2 : 0);
        Vector2 p = LerpV(a, b, r.Range(0.3f, 0.6f)) + RightV(d) * LANE_OFFSET;
        float dist = Dist(p, near);
        if (dist < minDist || dist > maxDist) continue;
        if (offscreen && g.OnScreen(p, 150)) continue;
        bool clear = true;
        for (const Vehicle& o : g.vehicles) if (o.active && &o != &v && Len2(o.pos - p) < 150 * 150) { clear = false; break; }
        if (!clear) continue;
        v.pos = p; v.vel = { 0, 0 }; v.angle = AngleOf(DirVec(d)); v.angVel = 0;
        v.ai = DriverAI{};
        v.ai.dir = d;
        v.ai.ti = fwd ? (horizontal ? i + 1 : i) : i;
        v.ai.tj = fwd ? (horizontal ? j : j + 1) : j;
        v.ai.cruise = (v.S().large() ? r.Range(170, 205) : r.Range(215, 265));   // ~48-60 km/h
        v.ai.temper = r.Range(0.6f, 1.5f);
        IncidentAssignMood(v);
        if (v.driver == DriverType::Traffic) {
            StartPath(v, p, DirVec(d));
            PlanNext(v, map, nullptr);
            v.ai.rail = true;
            v.ai.speed = v.ai.cruise * 0.6f;
        }
        return true;
    }
    return false;
}
