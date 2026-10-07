// =====================================================================================
//  Turn paths of rail traffic - see traffic_turns.h
// =====================================================================================
#include "traffic_turns.h"
#include "traffic.h"
#include "traffic_recovery.h"
#include "config.h"
#include "assets.h"
#include <algorithm>
#include <array>

using namespace cfg;

namespace {
struct TurnSettings {
    float lateralAccel = 3.5f;    // m/s^2 at the rear axle
    float easement = 0.5f;        // clothoid length at each end of the arc, in radii
    float leftRadius = 6.0f;      // m, preferred left-turn arc
    float maxSwing = 2.5f;        // m a wide turn may move towards the centre lines
};

const TurnSettings& Tuning() {
    static TurnSettings s;
    static bool loaded = false;
    if (loaded) return s;
    loaded = true;
    TrafficField fields[] = {
        { "lateral_accel", &s.lateralAccel, 1, 8 },
        { "easement", &s.easement, 0, 1.5f },
        { "left_radius", &s.leftRadius, 3, 12 },
        { "max_swing", &s.maxSwing, 0, 4 }
    };
    LoadTrafficRecords("TURN", fields, (int)(sizeof(fields) / sizeof(fields[0])));
    return s;
}

constexpr float SPACING = 4.0f;           // px between points on curved parts
constexpr float STEP = 0.25f;             // px integration step along the arc
constexpr float GAP = 8.0f;               // px of straight between a swing and the arc

float Smooth(float t) { t = Saturate(t); return t * t * (3 - 2 * t); }

// Heading change along a symmetric 90-degree turn of arc radius R with clothoids of
// length lc at both ends, at arc length u of a total 'total'.
float TurnAngle(float u, float R, float lc, float total) {
    float la = total - 2 * lc;
    if (lc <= 0) return u / R;
    if (u <= lc) return u * u / (2 * lc * R);
    if (u <= lc + la) return lc / (2 * R) + (u - lc) / R;
    float w = total - u;
    return PI * 0.5f - w * w / (2 * lc * R);
}

// The turn from heading north to heading east (side +1) or west (side -1), starting at
// the origin: points about SPACING apart, the last one exactly at the end.
std::vector<Vector2> Curve(float R, float lc, int side, float* tangent) {
    // Heading change 2 * lc / (2R) + la / R = pi/2, so the arc between is R*pi/2 - lc.
    lc = std::min(lc, R * PI * 0.5f);
    float total = lc + R * PI * 0.5f;
    int n = std::max(2, (int)ceilf(total / SPACING));
    std::vector<Vector2> out;
    out.push_back({ 0, 0 });
    Vector2 p{ 0, 0 };
    float u = 0;
    for (int k = 1; k <= n; k++) {
        float target = total * k / n;
        while (u < target - 1e-6f) {
            float h = std::min(STEP, target - u);
            float a = TurnAngle(u + h * 0.5f, R, lc, total);
            p = p + V2(side * sinf(a), -cosf(a)) * h;
            u += h;
        }
        out.push_back(p);
    }
    *tangent = (fabsf(p.x) + fabsf(p.y)) * 0.5f;   // symmetric: equal along both roads
    return out;
}

void Add(TurnPath& t, Vector2 p, bool curve) {
    if (!t.points.empty() && Dist(t.points.back().p, p) < 0.05f) return;
    TurnPoint q; q.p = p; q.curve = curve;
    t.points.push_back(q);
}

// An S-shaped move of 'delta' sideways (along 'side') over 'length' along 'along'.
void Swing(TurnPath& t, Vector2 from, Vector2 along, Vector2 side, float delta, float length) {
    int n = std::max(2, (int)ceilf(length / SPACING));
    for (int k = 0; k <= n; k++) {
        float u = length * k / n;
        Add(t, from + along * u + side * (delta * Smooth(u / length)), true);
    }
}

// The rear-axle path of a turn in the junction frame. 'swing' moves the path towards the
// centre line of both roads (a wide turn); 'swingLength' is the length of each S-shaped
// move out of the lane and back.
TurnPath Build(int turnType, float R, float swing, float swingLength) {
    const float H = ROAD_HALF, L = LANE_OFFSET;
    TurnPath t;
    t.radius = R; t.swing = swing; t.wide = swing > 0;
    int side = turnType == 1 ? 1 : -1;
    float T = 0;
    std::vector<Vector2> curve = Curve(R, R * Tuning().easement, side, &T);
    // The two lane lines (moved by the swing) meet at P; the arc starts T before P.
    Vector2 P = turnType == 1 ? V2(L - swing, L - swing) : V2(L - swing, -L + swing);
    Vector2 S = P + V2(0, T);
    float entry = swing > 0 ? S.y + GAP + swingLength : S.y;
    Add(t, V2(L, std::max(H, entry)), false);
    if (swing > 0) {
        Swing(t, V2(L, entry), V2(0, -1), V2(-1, 0), swing, swingLength);
        Add(t, S, false);
    } else if (S.y < H) Add(t, S, false);
    for (Vector2 c : curve) Add(t, S + c, true);
    Vector2 E = S + curve.back();
    if (turnType == 1) {
        if (swing > 0) {
            Add(t, E + V2(GAP, 0), false);
            Swing(t, E + V2(GAP, 0), V2(1, 0), V2(0, 1), swing, swingLength);
        } else if (E.x < H) Add(t, V2(H, L), false);
    } else if (swing > 0) {
        Add(t, E + V2(-GAP, 0), false);
        Swing(t, E + V2(-GAP, 0), V2(-1, 0), V2(0, -1), swing, swingLength);
    } else if (E.x > -H) Add(t, V2(-H, -L), false);
    // The stop waypoint: where the path enters the junction box.
    for (size_t i = 0; i < t.points.size(); i++) {
        if (t.points[i].p.y > H + 0.01f) continue;
        if (i > 0 && t.points[i].p.y < H - 0.01f) {
            Vector2 a = t.points[i - 1].p, b = t.points[i].p;
            TurnPoint q; q.p = LerpV(a, b, (a.y - H) / (a.y - b.y)); q.curve = t.points[i].curve;
            t.points.insert(t.points.begin() + i, q);
        }
        t.points[i].stop = true;
        break;
    }
    return t;
}

// How far a body corner reaches where it can meet other traffic (junction frame): the
// oncoming half of the approach road, where cars leave the junction; and, beyond the
// stop-line zone (the first STOP_ZONE px past the box, which cars waiting at a stop
// line leave free), the oncoming half of the exit road or a road the path does not use.
constexpr float STOP_ZONE = 60.0f;
float Encroachment(Vector2 c, int turnType) {
    const float H = ROAD_HALF, Z = H + STOP_ZONE;
    if (fabsf(c.x) <= H && fabsf(c.y) <= H) return 0;
    if (fabsf(c.x) > H && fabsf(c.y) > H) return 0;     // over a kerb: checked separately
    if (c.y > H) return std::max(0.0f, -c.x);
    if (c.y < -H) return std::max(0.0f, -c.y - Z);
    if (c.x > H) return turnType == 1 ? (c.x > Z ? std::max(0.0f, -c.y) : 0.0f) : std::max(0.0f, c.x - Z);
    return turnType == 2 ? (c.x < -Z ? std::max(0.0f, c.y) : 0.0f) : std::max(0.0f, -c.x - Z);
}

// The path with 400 px of straight road before and after, sampled like a rail car's.
struct Sampled {
    std::vector<Vector2> pts;
    std::vector<float> cum;
    Sampled(const TurnPath& t, int turnType) {
        Vector2 exitDir = turnType == 1 ? V2(1, 0) : V2(-1, 0);
        pts.push_back(t.points.front().p + V2(0, 400));
        for (const TurnPoint& q : t.points) pts.push_back(q.p);
        pts.push_back(t.points.back().p + exitDir * 400);
        cum.push_back(0);
        for (size_t i = 1; i < pts.size(); i++) cum.push_back(cum.back() + Dist(pts[i - 1], pts[i]));
    }
    Vector2 operator()(float s) const {
        if (s <= 0) return pts.front();
        size_t i = std::lower_bound(cum.begin() + 1, cum.end(), s) - cum.begin();
        if (i >= pts.size()) return pts.back();
        return LerpV(pts[i - 1], pts[i], (s - cum[i - 1]) / std::max(1e-3f, cum[i] - cum[i - 1]));
    }
};

// How far a box reaches into the block round the junction in quadrant (sx, sy): its
// deepest corner, or the block's corner poking into its side.
float BlockDepth(const OBB& box, const Vector2 corners[4], float sx, float sy) {
    const float H = ROAD_HALF;
    float depth = 0;
    for (int k = 0; k < 4; k++) depth = std::max(depth, std::min(sx * corners[k].x - H, sy * corners[k].y - H));
    Vector2 q = V2(sx * H, sy * H) - box.c;
    float inside = 1e9f;
    for (int a = 0; a < 2; a++) inside = std::min(inside, box.he[a] - fabsf(Dot(q, box.ax[a])));
    return std::max(depth, inside);
}

// Drives the body along the path with the production pose rule, the rear axle 'step' px
// at a time, and measures how far it reaches over a kerb: the wheels (the body between
// the axles) and the whole body, whose overhangs may sweep over a corner as a long
// vehicle's do; and how far a corner reaches where it can meet other traffic.
void Check(TurnPath& t, int turnType, float length, float width, float step) {
    Sampled path(t, turnType);
    t.kerb = t.wheelKerb = t.encroach = 0;
    float axle = length * 0.32f;
    const float quadrants[4][2] = { { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
    // Rear axle from the path's first point until the whole body has left the turn.
    for (float r = path.cum[1] - length; r <= path.cum[path.cum.size() - 2] + length; r += step) {
        Vector2 heading;
        Vector2 pos = ShiftedRailPose(path, r + axle, length, 0, 0, 0, 0, 0, &heading);
        float angle = AngleOf(heading);
        OBB body = MakeOBB(pos, angle, width * 0.5f, length * 0.5f);
        OBB wheels = MakeOBB(pos, angle, width * 0.5f, axle);
        Vector2 bc[4], wc[4];
        OBBCorners(body, bc); OBBCorners(wheels, wc);
        for (const auto& q : quadrants) {
            t.kerb = std::max(t.kerb, BlockDepth(body, bc, q[0], q[1]));
            t.wheelKerb = std::max(t.wheelKerb, BlockDepth(wheels, wc, q[0], q[1]));
        }
        for (Vector2 c : bc) t.encroach = std::max(t.encroach, Encroachment(c, turnType));
    }
}

// Speed allowed at each point: from how fast the body's heading turns (the production
// tangent rule) while the rear axle is within RAIL_TANGENT_SPAN of the point, where the
// driver applies its limit, and a little either side.
void SetSpeeds(TurnPath& t, int turnType) {
    float accel = Tuning().lateralAccel * M;
    Sampled path(t, turnType);
    const float step = 0.25f, reach = RAIL_TANGENT_SPAN * 2;
    auto heading = [&](float s) {
        return AngleOf(path(s + RAIL_TANGENT_SPAN) - path(s - RAIL_TANGENT_SPAN));
    };
    for (size_t i = 0; i < t.points.size(); i++) {
        float at = path.cum[i + 1], curvature = 0;
        float previous = heading(at - reach);
        for (float s = at - reach + step; s <= at + reach + 1e-4f; s += step) {
            float h = heading(s);
            curvature = std::max(curvature, fabsf(WrapAngle(h - previous)) / step);
            previous = h;
        }
        t.points[i].vmax = curvature > 1e-4f ? sqrtf(accel / curvature) : 0.0f;
    }
}

// Limits for a planned turn. The wheels (the body between the axles) stay off the kerbs.
// A clean turn keeps a corner within 2 px of where other traffic can be, and a car's body
// within 2 px of a kerb; a large vehicle's overhangs may sweep over a corner, as little
// as possible. A class that cannot turn cleanly (a large vehicle, or a car whose body is
// too wide for the lane) takes a wide turn: it may move towards the centre lines and
// reach up to 24 px (1.5 m) into the oncoming half, and takes the junction box alone.
// The planner keeps a margin below the fixture's limits (2 px, 24 px), which measure the
// body continuously.
constexpr float WHEEL_KERB = 0.5f, CAR_KERB = 2.0f, ENCROACH = 1.0f, WIDE_ENCROACH = 22.0f;
// A turn that stays out of the lane (no swing) and within the fixture's clean limit
// does not cross another driver's way: it shares the junction box as before.
constexpr float CLEAN_ENCROACH = 2.0f;

TurnPath Plan(int turnType, const Vehicle& v) {
    const TurnSettings& s = Tuning();
    // Measured paths follow their polygon slightly inside the arc: keep a little above.
    float rmin = ceilf(RailTurnMinRadius(v)) + 1;
    bool large = v.S().large();
    const float top = 10.0f * M;
    // Right: the largest radius that fits; left: the radius closest to left_radius.
    float pref = turnType == 1 ? top : std::max(rmin, s.leftRadius * M);
    // A move out of the lane and back like a lane change (traffic.cpp ShiftLength): its
    // sharpest bend within a comfortable radius, and at least one and a half lengths, so
    // the front of a long vehicle does not wag.
    auto swingLength = [&](float swing) {
        float radius = std::max({ 90.0f, v.length * 1.25f, rmin });
        return swing > 0 ? std::max(sqrtf(6 * swing * radius), v.length * 1.5f) : 0.0f;
    };
    auto excess = [&](const TurnPath& t, bool wide) {
        float kerbLimit = large || wide ? 1e9f : CAR_KERB;
        return std::max(0.0f, t.wheelKerb - WHEEL_KERB) * 4 + std::max(0.0f, t.kerb - kerbLimit) +
               std::max(0.0f, t.encroach - (wide ? WIDE_ENCROACH : ENCROACH));
    };
    auto cost = [&](const TurnPath& t) { return t.kerb + 0.05f * fabsf(t.radius - pref) + 0.2f * t.swing; };
    // Every candidate once, the rear axle 3 px at a time; the chosen one again at 1 px.
    // Once a swing allows a clean turn, larger swings are not needed.
    std::vector<TurnPath> candidates;
    for (float swing = 0; swing <= s.maxSwing * M + 1e-3f; swing += 4) {
        bool clean = false;
        for (float R = rmin; R <= top + 1e-3f; R += 3) {
            TurnPath t = Build(turnType, R, swing, swingLength(swing));
            Check(t, turnType, v.length, v.width, 3);
            t.points.clear();
            clean = clean || excess(t, false) <= 0;
            candidates.push_back(t);
        }
        if (clean) break;
    }
    auto exact = [&](const TurnPath& c) {
        TurnPath t = Build(turnType, c.radius, c.swing, swingLength(c.swing));
        Check(t, turnType, v.length, v.width, 1);
        return t;
    };
    for (int stage = 0; stage < 2; stage++) {
        bool wide = stage == 1;
        std::vector<const TurnPath*> fitting;
        float swingLimit = 1e9f;                // a clean turn swings no further than it must
        for (const TurnPath& c : candidates)
            if (excess(c, wide) <= 0 && c.swing <= swingLimit) {
                if (!wide) swingLimit = std::min(swingLimit, c.swing);
                fitting.push_back(&c);
            }
        std::sort(fitting.begin(), fitting.end(), [&](const TurnPath* a, const TurnPath* b) { return cost(*a) < cost(*b); });
        for (const TurnPath* c : fitting) {
            if (c->swing > swingLimit) continue;
            TurnPath t = exact(*c);
            if (excess(t, wide) > 0) continue;
            t.wide = t.swing > 0 || t.encroach > CLEAN_ENCROACH;
            SetSpeeds(t, turnType);
            return t;
        }
    }
    // Nothing fits: the candidate closest to the limits, which traffic avoids.
    const TurnPath* least = &candidates.front();
    for (const TurnPath& c : candidates) if (excess(c, true) < excess(*least, true)) least = &c;
    TurnPath t = exact(*least);
    t.fits = false; t.wide = true;
    SetSpeeds(t, turnType);
    return t;
}

struct Cached { VClass cls = -1; float length = 0, width = 0; std::array<TurnPath, 2> paths; };
} // namespace

float RailTurnLateralAccel() { return Tuning().lateralAccel * M; }

// The tightest path the rear axle can follow at full lock: the class's kerb-to-kerb
// turning circle is traced by the outer front wheel, a wheelbase (the pose's axle
// spacing, 0.64 lengths) ahead of the rear axle and half a width outside it.
float RailTurnMinRadius(const Vehicle& v) {
    float outer = v.S().turnCircle * 0.5f, wheelbase = v.length * 0.64f;
    if (outer <= wheelbase * 1.05f) return wheelbase * 0.3f;
    return std::max(wheelbase * 0.3f, sqrtf(outer * outer - wheelbase * wheelbase) - v.width * 0.5f);
}

const TurnPath& RailTurnPath(const Vehicle& v, int turnType) {
    static std::vector<Cached> cache;     // per class and body size
    Cached* c = nullptr;
    for (Cached& k : cache)
        if (k.cls == v.cls && fabsf(k.length - v.length) <= 0.5f && fabsf(k.width - v.width) <= 0.5f) { c = &k; break; }
    if (!c) {
        cache.push_back(Cached{ v.cls, v.length, v.width, {} });
        c = &cache.back();
        for (int k = 0; k < 2; k++) {
            double started = GetTime();
            c->paths[k] = Plan(k + 1, v);
            const TurnPath& t = c->paths[k];
            TraceLog(LOG_INFO, "TURN-PATH %s %s: %s, radius %.2f m (class minimum %.2f m), swing %.0f px, over a kerb: wheels %.1f px, body %.1f px; encroachment %.1f px; %d points; %.0f ms",
                     v.S().name.c_str(), k == 0 ? "right" : "left", !t.fits ? "does not fit" : t.wide ? "wide" : "clean", t.radius / M,
                     RailTurnMinRadius(v) / M, t.swing, t.wheelKerb, t.kerb, t.encroach, (int)t.points.size(),
                     (GetTime() - started) * 1000.0);
        }
    }
    return c->paths[turnType == 1 ? 0 : 1];
}

void RailPlanTurns() {
    for (size_t i = 0; i < gAssets.vehicles.size(); i++) {
        const VehicleSprite& sprite = gAssets.vehicles[i];
        const VehicleSpec& spec = Spec(sprite.cls);
        if (!sprite.spawnable || spec.trafficWeight <= 0 || spec.police()) continue;
        Vehicle v; InitVehicle(v, (int)i, V2(0, 0), 0);
        RailTurnPath(v, 1);
    }
}
