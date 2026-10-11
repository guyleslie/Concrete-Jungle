// =====================================================================================
//  Vehicles hitting people (CJ-035) - see impact.h.
// =====================================================================================
#include "impact.h"
#include "vehicle.h"
#include "config.h"
#include <cmath>
#include <cstdlib>

namespace {
constexpr float M = cfg::M;
constexpr float KMH = cfg::M / 3.6f;             // px/s per km/h
constexpr float GRAVITY = 9.81f * cfg::M;        // px/s^2
constexpr float MOVING = 1.0f;                   // px/s: a vehicle slower than this towards someone stops them as a wall
constexpr float KILL = 1000.0f;                  // health points: a wheel over the body

struct Front { float angle = 0, factor = 0; };   // launch angle (rad), the share of the speed the body keeps
struct Data {
    float personKg = 0, centre = 0;              // kg; px: the centre of mass above the road
    float balanceMin = 0, balanceMax = 0, staggerS = 0;   // px/s; s
    float friction = 0;                          // a body on the road, sliding or landing
    Front wrap, projection;                      // a front edge below / at or above the centre of mass
    float injury50 = 0, injurySlope = 0;         // px/s of speed change at which half die; per px/s
    float throwSpeed = 0, fallMin = 0, fallMax = 0, throwMin = 0, throwMax = 0;   // px/s; s
    float shove = 0, jolt = 0;                   // px a body driven over moves; px/s the vehicle loses
} D;

float Share(const Vehicle& v) { float mv = v.S().mass * 1000.0f; return mv / (mv + D.personKg); }
const Front& FrontOf(const Vehicle& v) { return v.S().front < D.centre ? D.wrap : D.projection; }

// Damage = 100 e^(slope (dv - dv50)) u / (1 - u), u uniform: reaching 100 points, death, has
// exactly the logistic probability 1 / (1 + e^(-slope (dv - dv50))).
float Injury(float speedChange) {
    float u = GRng().Range(1e-4f, 1.0f - 1e-4f);
    return 100.0f * expf(D.injurySlope * (speedChange - D.injury50)) * u / (1.0f - u);
}

bool Number(const std::string& s, float& out) {
    char* end = nullptr;
    out = strtof(s.c_str(), &end);
    return end != s.c_str() && *end == '\0' && std::isfinite(out);
}
} // namespace

// -------------------------------------------------------------------------------------
//  Data
// -------------------------------------------------------------------------------------
bool LoadImpacts(const std::vector<DataRecord>& records, std::string& error) {
    struct Value { float* out; float lo, hi, scale; };
    struct Record { const char* name; std::vector<Value> values; };
    const Record table[] = {
        { "PERSON",     { { &D.personKg, 20, 200, 1 }, { &D.centre, 0.3f, 1.5f, M } } },
        { "BALANCE",    { { &D.balanceMin, 0.1f, 5, M }, { &D.balanceMax, 0.1f, 5, M }, { &D.staggerS, 0, 5, 1 } } },
        { "GROUND",     { { &D.friction, 0.1f, 1.5f, 1 } } },
        { "WRAP",       { { &D.wrap.angle, 0, 60, DEG2RAD }, { &D.wrap.factor, 0.1f, 1, 1 } } },
        { "PROJECTION", { { &D.projection.angle, 0, 60, DEG2RAD }, { &D.projection.factor, 0.1f, 1, 1 } } },
        { "INJURY",     { { &D.injury50, 10, 300, KMH }, { &D.injurySlope, 0.001f, 1, 1 / KMH } } },
        { "DOWN",       { { &D.throwSpeed, 1, 100, KMH }, { &D.fallMin, 0, 30, 1 }, { &D.fallMax, 0, 30, 1 },
                          { &D.throwMin, 0, 30, 1 }, { &D.throwMax, 0, 30, 1 } } },
        { "RUNOVER",    { { &D.shove, 0, 2, M }, { &D.jolt, 0, 20, KMH } } },
    };
    constexpr int N = (int)(sizeof(table) / sizeof(table[0]));
    bool seen[N] = {};
    for (const DataRecord& r : records) {
        int k = 0;
        while (k < N && !r.Is(table[k].name)) k++;
        if (k == N) { error = TextFormat("impacts.cfg line %d: unknown record %s", r.line, r[0].c_str()); return false; }
        const Record& rec = table[k];
        if (seen[k]) { error = TextFormat("impacts.cfg line %d: a second %s record", r.line, rec.name); return false; }
        if (r.size() != rec.values.size() + 1) {
            error = TextFormat("impacts.cfg line %d: %s takes %d values", r.line, rec.name, (int)rec.values.size());
            return false;
        }
        for (size_t i = 0; i < rec.values.size(); i++) {
            const Value& v = rec.values[i];
            float x;
            if (!Number(r[i + 1], x) || x < v.lo || x > v.hi) {
                error = TextFormat("impacts.cfg line %d: %s value %d must be a number from %g to %g", r.line, rec.name, (int)i + 1, v.lo, v.hi);
                return false;
            }
            *v.out = x * v.scale;
        }
        seen[k] = true;
    }
    for (int k = 0; k < N; k++)
        if (!seen[k]) { error = TextFormat("impacts.cfg: no %s record", table[k].name); return false; }
    if (D.balanceMin > D.balanceMax || D.fallMin > D.fallMax || D.throwMin > D.throwMax) {
        error = "impacts.cfg: a range of BALANCE or DOWN ends below where it starts";
        return false;
    }
    return true;
}

// -------------------------------------------------------------------------------------
//  Contact
// -------------------------------------------------------------------------------------
PersonHit VehicleMeetsPerson(const Vehicle& v, Vector2 n, float depth, Vector2 personVel, const PersonBody& b, bool lying) {
    PersonHit h;
    if (b.Airborne()) return h;
    const float mv = v.S().mass * 1000.0f;
    if (lying) {                                          // the wheels go over them
        float speed = v.Speed();
        if (speed < MOVING) return h;
        Vector2 dir = v.vel / speed;
        h.kind = HitKind::RunOver;
        h.vel = personVel - dir * SlideSpeed(D.shove);     // the wheel kicks the body back, under the vehicle
        h.damage = KILL;
        h.vehicleDv = dir * -std::min(D.jolt, speed);
        return h;
    }
    float closing = Dot(v.vel - personVel, n);
    if (Dot(v.vel, n) < MOVING || closing <= 0) {          // they walked into it
        h.kind = HitKind::Obstacle;
        h.push = n * depth;
        return h;
    }
    float dv = closing * Share(v);                         // inelastic: what they would take along n
    if (dv < b.balance && b.stagger <= 0) {                // caught by a step or two
        h.kind = HitKind::Bump;
        h.push = n * depth;
        h.vel = personVel + n * dv;
        h.speedChange = dv;
        h.vehicleDv = n * (-dv * D.personKg / mv);
        return h;
    }
    const Front& f = FrontOf(v);
    float s = f.factor * dv, across = s * cosf(f.angle);   // the launch speed and its part over the road
    h.kind = HitKind::Launch;
    h.vel = personVel + n * across;
    h.vz = s * sinf(f.angle);
    h.speedChange = s;
    h.damage = Injury(s);
    h.vehicleDv = n * (-across * D.personKg / mv);         // the vertical part goes into the road
    return h;
}

PersonHit ThrownOff(Vector2 velocity) {
    PersonHit h;
    float s = Len(velocity);
    h.kind = HitKind::Launch;
    h.vel = velocity * cosf(D.wrap.angle);                 // over the handlebars as over a bonnet
    h.vz = s * sinf(D.wrap.angle);
    h.speedChange = s;
    return h;
}

float PersonImpactDamage(const Vehicle& v, float closing) {
    return Injury(FrontOf(v).factor * closing * Share(v));
}

// -------------------------------------------------------------------------------------
//  A body on the move
// -------------------------------------------------------------------------------------
void MoveBody(Vector2& pos, Vector2& vel, PersonBody& b, float dt) {
    float t = dt;
    if (b.Airborne()) {
        // the flight up to the landing, if it comes within this step
        float land = (b.vz + sqrtf(b.vz * b.vz + 2.0f * GRAVITY * std::max(0.0f, b.z))) / GRAVITY;
        float fly = std::min(dt, land);
        pos = pos + vel * fly;
        b.z += b.vz * fly - 0.5f * GRAVITY * fly * fly;
        b.vz -= GRAVITY * fly;
        if (fly < dt || b.z <= 0) {                         // the friction of the landing takes speed off
            float s = Len(vel), loss = D.friction * -b.vz;
            vel = s > loss ? vel * ((s - loss) / s) : Vector2{ 0, 0 };
            b.z = 0; b.vz = 0;
            t = dt - fly;
        } else return;
    }
    float s = Len(vel);                                     // sliding at the friction's deceleration
    if (s <= 0) return;
    float a = D.friction * GRAVITY, slide = std::min(t, s / a);
    Vector2 dir = vel / s;
    pos = pos + dir * (s * slide - 0.5f * a * slide * slide);
    s -= a * slide;
    vel = s > 1e-3f ? dir * s : Vector2{ 0, 0 };
}

void Stagger(PersonBody& b) { b.stagger = D.staggerS; }
void Steady(PersonBody& b, float dt) { b.stagger = std::max(0.0f, b.stagger - dt); }
float SlideSpeed(float px) { return sqrtf(2.0f * D.friction * GRAVITY * std::max(0.0f, px)); }
float TimeDown(float speedChange, Rng& r) {
    return speedChange < D.throwSpeed ? r.Range(D.fallMin, D.fallMax) : r.Range(D.throwMin, D.throwMax);
}
float BalanceLimit(Rng& r) { return r.Range(D.balanceMin, D.balanceMax); }
float PlayerBalance() { return 0.5f * (D.balanceMin + D.balanceMax); }
