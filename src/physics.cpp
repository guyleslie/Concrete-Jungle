// =====================================================================================
//  Vehicle rigid-body physics - see physics.h
// =====================================================================================
#include "physics.h"
#include "game.h"
#include "traffic.h"

using namespace cfg;

// -------------------------------------------------------------------------------------
//  Helpers
// -------------------------------------------------------------------------------------
static Vector2 Rotate(Vector2 v, float a) { float c = cosf(a), s = sinf(a); return { v.x * c - v.y * s, v.x * s + v.y * c }; }
static Vector2 TangentOf(Vector2 n) { return { n.y, -n.x }; }
static float InvInertia(const Vehicle& v) { return 12.0f / (v.S().mass * (v.width * v.width + v.length * v.length)); }
static float HalfDiag(const Vehicle& v) { return 0.5f * sqrtf(v.width * v.width + v.length * v.length); }

// Coefficient of restitution of a car impact: bumpers spring back in a parking knock,
// crumple zones absorb almost everything in a real crash.
static float CarRestitution(float closing) { return 0.05f + 0.30f * expf(-closing / 120.0f); }

// Soft constraint coefficients (Box2D v3 b2MakeSoft): a damped spring that pushes
// overlapping bodies apart smoothly instead of teleporting them out.
struct Softness { float biasRate = 0, massScale = 1, impulseScale = 0; };
static Softness MakeSoft(float hertz, float zeta, float h) {
    if (hertz <= 0) return {};
    float omega = 2 * PI * hertz, a1 = 2 * zeta + h * omega, a2 = h * omega * a1, a3 = 1 / (1 + a2);
    return { omega / a1, a2 * a3, a3 };
}
static Softness gSoftDyn, gSoftStatic;

// -------------------------------------------------------------------------------------
//  Narrow phase
// -------------------------------------------------------------------------------------
struct Manifold { Vector2 n{}; int count = 0; Vector2 p[2]{}; float sep[2]{}; };

// Keeps the part of segment v1-v2 with Dot(normal, p) <= offset.
static bool ClipSegment(Vector2& v1, Vector2& v2, Vector2 normal, float offset) {
    float d1 = Dot(normal, v1) - offset, d2 = Dot(normal, v2) - offset;
    if (d1 > 0 && d2 > 0) return false;
    if (d1 > 0) v1 = v1 + (v2 - v1) * (d1 / (d1 - d2));
    else if (d2 > 0) v2 = v2 + (v1 - v2) * (d2 / (d2 - d1));
    return true;
}

// Box vs box: separating axis test, then the incident face is clipped against the side
// planes of the reference face -> up to two contact points. Normal points from A to B.
// Points up to 'margin' apart are kept (speculative contacts).
static bool CollideBoxes(const OBB& A, const OBB& B, float margin, Manifold& m) {
    Vector2 d = B.c - A.c;
    float bestA = -1e9f, bestB = -1e9f; int axA = 0, axB = 0;
    for (int i = 0; i < 2; i++) {
        float sA = fabsf(Dot(d, A.ax[i])) - A.he[i] - OBBProjectRadius(B, A.ax[i]);
        if (sA > margin) return false;
        if (sA > bestA) { bestA = sA; axA = i; }
        float sB = fabsf(Dot(d, B.ax[i])) - B.he[i] - OBBProjectRadius(A, B.ax[i]);
        if (sB > margin) return false;
        if (sB > bestB) { bestB = sB; axB = i; }
    }
    // prefer A's faces unless B's are clearly better: stops the normal flip-flopping
    bool useA = !(bestB > 0.95f * bestA + 0.5f);
    const OBB& R = useA ? A : B;
    const OBB& I = useA ? B : A;
    int ax = useA ? axA : axB, tax = 1 - ax;
    Vector2 nR = R.ax[ax] * (Dot(I.c - R.c, R.ax[ax]) < 0 ? -1.0f : 1.0f);   // from R towards I
    Vector2 t = R.ax[tax];
    float front = Dot(nR, R.c) + R.he[ax];
    float side = Dot(t, R.c);
    // incident face: the face of I most anti-parallel to the reference normal
    int iax = fabsf(Dot(I.ax[0], nR)) > fabsf(Dot(I.ax[1], nR)) ? 0 : 1;
    float sgn = Dot(I.ax[iax], nR) > 0 ? -1.0f : 1.0f;
    Vector2 ic = I.c + I.ax[iax] * (sgn * I.he[iax]);
    Vector2 it = I.ax[1 - iax] * I.he[1 - iax];
    Vector2 v1 = ic - it, v2 = ic + it;
    if (!ClipSegment(v1, v2, t, side + R.he[tax])) return false;
    if (!ClipSegment(v1, v2, t * -1.0f, -(side - R.he[tax]))) return false;
    m.count = 0;
    for (Vector2 p : { v1, v2 }) {
        float s = Dot(nR, p) - front;
        if (s > margin) continue;
        m.p[m.count] = p - nR * (s * 0.5f);            // midway between the two surfaces
        m.sep[m.count] = s;
        m.count++;
    }
    m.n = useA ? nR : nR * -1.0f;
    return m.count > 0;
}

// Box (A) vs circle. Normal points from the box towards the circle.
static bool CollideBoxCircle(const OBB& A, Vector2 c, float r, float margin, Manifold& m) {
    Vector2 q = OBBClosestPoint(A, c);
    Vector2 d = c - q;
    float l = Len(d);
    if (l > 1e-4f) {
        float s = l - r;
        if (s > margin) return false;
        m.n = d / l;
        m.p[0] = q + m.n * (s * 0.5f);
        m.sep[0] = s;
    } else {                                           // centre inside the box
        Vector2 rel = c - A.c; float best = 1e9f;
        for (int i = 0; i < 2; i++) {
            float pr = Dot(rel, A.ax[i]), pen = A.he[i] - fabsf(pr);
            if (pen < best) { best = pen; m.n = A.ax[i] * (pr < 0 ? -1.0f : 1.0f); }
        }
        m.p[0] = c;
        m.sep[0] = -(best + r);
    }
    m.count = 1;
    return true;
}

// -------------------------------------------------------------------------------------
//  Bodies
// -------------------------------------------------------------------------------------
Vector2 VehiclePhysics::VelAt(int b, Vector2 r) const {
    if (b < 0) return { 0, 0 };
    const Body& B = bodies[b];
    Vector2 v = B.kin ? B.kv : B.v->vel;
    float w = B.kin ? B.kw : B.v->angVel;
    return v + Perp(r) * w;
}

void VehiclePhysics::Impulse(int b, Vector2 r, Vector2 P) {
    if (b < 0) return;
    Body& B = bodies[b];
    if (B.im <= 0) return;
    B.v->vel = B.v->vel + P * B.im;
    B.v->angVel += Cross(r, P) * B.iI;
}

// -------------------------------------------------------------------------------------
//  Contact generation (once per frame)
// -------------------------------------------------------------------------------------
void VehiclePhysics::AddContact(const Contact& c) { contacts.push_back(c); }

void VehiclePhysics::Collide(Game& g, float dt) {
    contacts.clear();
    soft.clear();
    auto& V = g.vehicles;
    const int nv = (int)V.size();
    auto velOf = [&](int i) { return bodies[i].kin ? bodies[i].kv : V[i].vel; };
    auto spinOf = [&](int i) { return bodies[i].kin ? bodies[i].kw : V[i].angVel; };

    // ---- vehicle vs vehicle ----
    for (int i = 0; i < nv; i++) {
        if (!V[i].active) continue;
        float rA = HalfDiag(V[i]);
        for (int j = i + 1; j < nv; j++) {
            if (!V[j].active || (bodies[i].kin && bodies[j].kin)) continue;   // rail traffic spaces itself
            float rB = HalfDiag(V[j]);
            float rel = Len(velOf(j) - velOf(i)) + fabsf(spinOf(i)) * rA + fabsf(spinOf(j)) * rB;
            float margin = Clampf(rel * dt * 1.2f + 2.0f, 2.0f, 48.0f);
            float reach = rA + rB + margin;
            if (Len2(V[j].pos - V[i].pos) > reach * reach) continue;
            Manifold m;
            if (!CollideBoxes(V[i].Box(), V[j].Box(), margin, m)) continue;
            Contact c; c.a = i; c.b = j; c.kind = ContactKind::Vehicle; c.n = m.n; c.count = m.count;
            for (int k = 0; k < m.count; k++) { c.pts[k].ra = m.p[k] - V[i].pos; c.pts[k].rb = m.p[k] - V[j].pos; c.pts[k].sep0 = m.sep[k]; }
            c.friction = 0.35f;                        // sheet metal on sheet metal
            AddContact(c);
        }
    }

    // ---- vehicle vs the static world ----
    static std::vector<int> ids;
    for (int i = 0; i < nv; i++) {
        Vehicle& v = V[i];
        if (!v.active || bodies[i].kin) continue;      // rail cars stay on the road
        float rA = HalfDiag(v);
        float margin = Clampf((Len(v.vel) + fabsf(v.angVel) * rA) * dt * 1.2f + 2.0f, 2.0f, 48.0f);
        float r = rA + margin;
        Rectangle q = { v.pos.x - r, v.pos.y - r, r * 2, r * 2 };
        OBB box = v.Box();
        auto add = [&](const Manifold& m, ContactKind kind, int obj, float friction, float cap) {
            Contact c; c.a = i; c.b = -1; c.kind = kind; c.obj = obj; c.n = m.n; c.count = m.count;
            for (int k = 0; k < m.count; k++) { c.pts[k].ra = m.p[k] - v.pos; c.pts[k].rb = m.p[k]; c.pts[k].sep0 = m.sep[k]; }
            c.friction = friction; c.cap = cap;
            AddContact(c);
        };
        g.map.QueryBuildings(q, ids);
        for (int k : ids) {
            Manifold m;
            if (CollideBoxes(box, MakeAABB(g.map.buildings[k].r), margin, m)) add(m, ContactKind::Building, -1, 0.45f, 0);
        }
        g.map.QueryObjects(q, ids);
        for (int k : ids) {
            const CityObject& o = g.map.objects[k];
            Manifold m;
            if (o.soft) {                              // shrubs: no hard contact, drag while inside
                if (CollideBoxCircle(box, o.pos, o.radius, 0, m) && m.sep[0] < 0) soft.push_back({ i, k });
                continue;
            }
            bool hit = o.box ? CollideBoxes(box, o.Box(), margin, m) : CollideBoxCircle(box, o.pos, o.radius, margin, m);
            if (hit) add(m, ContactKind::Object, k, 0.3f, o.strength);
        }
        if (stepOptions.disableWorldEdges) continue;
        // the seawall around the island
        Vector2 cs[4]; OBBCorners(box, cs);
        static const Vector2 N[4] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };   // from the car into the wall
        for (int e = 0; e < 4; e++) {
            Manifold m; m.n = N[e];
            float best[2] = { 1e9f, 1e9f }; Vector2 bp[2]{};
            for (Vector2 p : cs) {
                float s = e == 0 ? p.x : e == 1 ? p.y : e == 2 ? WORLD_W - p.x : WORLD_H - p.y;
                if (s > margin) continue;
                if (s < best[0]) { best[1] = best[0]; bp[1] = bp[0]; best[0] = s; bp[0] = p; }
                else if (s < best[1]) { best[1] = s; bp[1] = p; }
            }
            for (int k = 0; k < 2; k++) if (best[k] < 1e8f) { m.p[m.count] = bp[k] + N[e] * (best[k] * 0.5f); m.sep[m.count] = best[k]; m.count++; }
            if (m.count) add(m, ContactKind::WorldEdge, -1, 0.45f, 0);
        }
    }
}

// -------------------------------------------------------------------------------------
//  Solver
// -------------------------------------------------------------------------------------
void VehiclePhysics::Prepare(Contact& c) {
    const Body& A = bodies[c.a];
    const Body* B = c.b >= 0 ? &bodies[c.b] : nullptr;
    Vector2 t = TangentOf(c.n);
    float closing = 0;
    for (int k = 0; k < c.count; k++) {
        Point& p = c.pts[k];
        float rnA = Cross(p.ra, c.n), rtA = Cross(p.ra, t);
        float kN = A.im + A.iI * rnA * rnA, kT = A.im + A.iI * rtA * rtA;
        if (B) {
            float rnB = Cross(p.rb, c.n), rtB = Cross(p.rb, t);
            kN += B->im + B->iI * rnB * rnB;
            kT += B->im + B->iI * rtB * rtB;
        }
        p.nMass = kN > 0 ? 1.0f / kN : 0.0f;
        p.tMass = kT > 0 ? 1.0f / kT : 0.0f;
        p.relVel = Dot(VelAt(c.b, p.rb) - VelAt(c.a, p.ra), c.n);
        closing = std::max(closing, -p.relVel);
    }
    c.restitution = c.cap > 0 ? 0.0f : CarRestitution(closing);
}

void VehiclePhysics::WarmStart(Contact& c) {
    if (c.cap > 0) { for (int k = 0; k < c.count; k++) c.pts[k].jn = c.pts[k].jt = 0; return; }
    Vector2 t = TangentOf(c.n);
    for (int k = 0; k < c.count; k++) {
        Point& p = c.pts[k];
        Vector2 P = c.n * p.jn + t * p.jt;
        Impulse(c.a, p.ra, P * -1.0f);
        Impulse(c.b, p.rb, P);
        p.jnTotal += p.jn; p.jtTotal += p.jt;
    }
}

void VehiclePhysics::Solve(Contact& c, bool useBias, float inv_h, float h) {
    (void)h;
    const Body& A = bodies[c.a];
    const Body* B = c.b >= 0 ? &bodies[c.b] : nullptr;
    bool stiff = !B || A.im <= 0 || B->im <= 0;         // against something immovable
    const Softness& soft = stiff ? gSoftStatic : gSoftDyn;
    Vector2 t = TangentOf(c.n);
    // ---- normal: no closing faster than the gap allows, soft push-out of overlap ----
    for (int k = 0; k < c.count; k++) {
        Point& p = c.pts[k];
        Vector2 pA = A.v->pos + Rotate(p.ra, A.v->angle - A.a0);
        Vector2 pB = B ? B->v->pos + Rotate(p.rb, B->v->angle - B->a0) : p.rb;
        float s = Dot(pB - pA, c.n) + p.sep0;
        float bias = 0, massScale = 1, impulseScale = 0;
        if (s > 0) bias = s * inv_h;                     // speculative: may close the gap, no more
        else if (useBias) {
            bias = std::max(soft.biasRate * s, -MAX_PUSH);
            massScale = soft.massScale; impulseScale = soft.impulseScale;
        }
        float vn = Dot(VelAt(c.b, p.rb) - VelAt(c.a, p.ra), c.n);
        float imp = -p.nMass * massScale * (vn + bias) - impulseScale * p.jn;
        float newJ = std::max(p.jn + imp, 0.0f);
        if (c.cap > 0) {                                 // breakaway: can only take so much
            float room = std::max(0.0f, c.cap - (p.jnTotal - p.jn));
            if (newJ > room) { newJ = room; c.broken = true; }
        }
        imp = newJ - p.jn;
        p.jn = newJ; p.jnTotal += imp; p.jnMax = std::max(p.jnMax, newJ);
        Vector2 P = c.n * imp;
        Impulse(c.a, p.ra, P * -1.0f);
        Impulse(c.b, p.rb, P);
    }
    // ---- Coulomb friction: scraping along walls and other cars ----
    for (int k = 0; k < c.count; k++) {
        Point& p = c.pts[k];
        float vt = Dot(VelAt(c.b, p.rb) - VelAt(c.a, p.ra), t);
        float maxF = c.friction * p.jn;
        float newT = Clampf(p.jt - p.tMass * vt, -maxF, maxF);
        float imp = newT - p.jt;
        p.jt = newT; p.jtTotal += imp;
        Vector2 P = t * imp;
        Impulse(c.a, p.ra, P * -1.0f);
        Impulse(c.b, p.rb, P);
    }
}

void VehiclePhysics::ApplyRestitution(Contact& c) {
    if (c.restitution <= 0) return;
    for (int k = 0; k < c.count; k++) {
        Point& p = c.pts[k];
        if (p.relVel > -RESTITUTION_MIN || p.jnMax <= 0) continue;
        float vn = Dot(VelAt(c.b, p.rb) - VelAt(c.a, p.ra), c.n);
        float newJ = std::max(p.jn - p.nMass * (vn + c.restitution * p.relVel), 0.0f);
        float imp = newJ - p.jn;
        p.jn = newJ; p.jnTotal += imp;
        Vector2 P = c.n * imp;
        Impulse(c.a, p.ra, P * -1.0f);
        Impulse(c.b, p.rb, P);
    }
}

// -------------------------------------------------------------------------------------
//  Frame step
// -------------------------------------------------------------------------------------
void VehiclePhysics::Step(Game& g, float dt, const PhysicsStepOptions& options) {
    stepOptions = options;
    events.clear();
    auto& V = g.vehicles;
    const int nv = (int)V.size();
    if (dt <= 0 || nv == 0) return;

    // ---- bodies: rail cars are rewound to where they were; the sub-steps move them ----
    bodies.assign(nv, Body{});
    for (int i = 0; i < nv; i++) {
        Vehicle& v = V[i];
        Body& b = bodies[i];
        b.v = &v;
        if (!v.active) continue;
        if (AIOnRail(v)) {
            b.kin = true;
            b.to = v.pos; b.toAng = v.angle;
            v.pos = v.kinFrom; v.angle = v.kinFromAng;
            b.kv = (b.to - v.pos) / dt;
            b.kw = WrapAngle(b.toAng - v.angle) / dt;
        } else {
            b.im = 1.0f / v.S().mass;
            b.iI = InvInertia(v);
        }
        b.p0 = v.pos; b.a0 = v.angle;
    }

    Collide(g, dt);

    // ---- a real hit knocks a rail car into full physics before solving, so the two
    //      cars exchange momentum instead of one of them acting like a moving wall ----
    for (Contact& c : contacts) {
        if (c.b < 0 || bodies[c.a].kin == bodies[c.b].kin) continue;
        float closing = 0;
        for (int k = 0; k < c.count; k++) {
            float vn = -Dot(VelAt(c.b, c.pts[k].rb) - VelAt(c.a, c.pts[k].ra), c.n);
            if (c.pts[k].sep0 - vn * dt < 0.5f) closing = std::max(closing, vn);   // touches this frame
        }
        if (closing <= KNOCK_SPEED) continue;
        Body& K = bodies[c.a].kin ? bodies[c.a] : bodies[c.b];
        Vehicle& kv = *K.v;
        AIKnock(kv);
        kv.vel = K.kv; kv.angVel = K.kw;
        K.kin = false;
        K.im = 1.0f / kv.S().mass; K.iI = InvInertia(kv);
    }
    for (Contact& c : contacts) Prepare(c);

    // ---- sub-steps ----
    int n = std::clamp((int)ceilf(dt * SUBSTEP_HZ - 0.01f), 1, 8);
    float h = dt / n, inv_h = 1.0f / h;
    float hz = std::min(CONTACT_HZ, 0.25f / h);
    gSoftDyn = MakeSoft(hz, CONTACT_DAMPING, h);
    gSoftStatic = MakeSoft(2.0f * hz, CONTACT_DAMPING, h);
    for (int s = 0; s < n; s++) {
        for (int i = 0; i < nv; i++)
            if (V[i].active && !bodies[i].kin && !stepOptions.disableForces) VehicleForces(V[i], g.map, h);
        for (const SoftHit& sh : soft) {               // pushing through a shrub
            Vehicle& v = V[sh.v];
            float drag = expf(-(1.5f + g.map.objects[sh.obj].radius / 8.0f) * h);
            v.vel = v.vel * drag; v.angVel *= drag;
        }
        for (Contact& c : contacts) WarmStart(c);
        for (int it = 0; it < 2; it++)
            for (Contact& c : contacts) Solve(c, true, inv_h, h);
        float f = (s + 1) / (float)n;
        for (int i = 0; i < nv; i++) {
            Vehicle& v = V[i];
            if (!v.active) continue;
            if (bodies[i].kin) {
                v.pos = LerpV(v.kinFrom, bodies[i].to, f);
                v.angle = WrapAngle(v.kinFromAng + WrapAngle(bodies[i].toAng - v.kinFromAng) * f);
            } else {
                v.pos = v.pos + v.vel * h;
                v.angle = WrapAngle(v.angle + v.angVel * h);
            }
        }
        for (Contact& c : contacts) Solve(c, false, inv_h, h);   // relax: drop the push-out velocity
    }
    for (Contact& c : contacts) ApplyRestitution(c);
    for (int i = 0; i < nv; i++)
        if (V[i].active && bodies[i].kin) { V[i].pos = bodies[i].to; V[i].angle = bodies[i].toAng; }

    // ---- results: events, broken furniture, rail cars being shoved ----
    static std::vector<char> shoved;
    shoved.assign(nv, 0);
    for (Contact& c : contacts) {
        float jn = 0, jt = 0, closing = 0;
        Vector2 pt = { 0, 0 };
        for (int k = 0; k < c.count; k++) {
            jn += c.pts[k].jnTotal; jt += c.pts[k].jtTotal;
            closing = std::max(closing, -c.pts[k].relVel);
            pt = pt + bodies[c.a].p0 + c.pts[k].ra;
        }
        if (jn <= 1e-4f && !c.broken) continue;         // speculative contact that never touched
        ImpactEvent e;
        e.a = c.a; e.b = c.b; e.kind = c.kind; e.obj = c.obj;
        e.point = pt / (float)c.count; e.normal = c.n;
        e.approach = closing;
        float J = sqrtf(jn * jn + jt * jt);
        e.dvA = J * bodies[c.a].im;
        e.dvB = c.b >= 0 ? J * bodies[c.b].im : 0.0f;
        Vector2 dv = VelAt(c.b, c.pts[0].rb) - VelAt(c.a, c.pts[0].ra);
        if (jn * bodies[c.a].im > 2.0f || (c.b >= 0 && jn * bodies[c.b].im > 2.0f)) e.scrape = fabsf(Dot(dv, TangentOf(c.n)));
        if (c.broken && c.obj >= 0) {
            // the object gives way; the loose part takes some momentum with it
            Vehicle& v = V[c.a];
            const CityObject& o = g.map.objects[c.obj];
            float vn = Dot(v.vel, c.n);
            if (vn > 0) v.vel = v.vel - c.n * (vn * o.mass / (v.S().mass + o.mass));
            g.map.BreakObject(c.obj, v.vel, g.fx, true);
            e.broke = true;
        }
        if (c.b >= 0 && bodies[c.a].kin != bodies[c.b].kin && jn > 0) shoved[bodies[c.a].kin ? c.a : c.b] = 1;
        events.push_back(e);
    }
    for (const SoftHit& sh : soft) {
        Vehicle& v = V[sh.v];
        CityObject& o = g.map.objects[sh.obj];
        if (!o.alive) continue;
        ImpactEvent e;
        e.a = sh.v; e.kind = ContactKind::Object; e.obj = sh.obj; e.point = o.pos;
        e.normal = Norm(o.pos - v.pos); e.scrape = v.Speed();
        // a fast or heavy vehicle flattens the shrub
        if (v.Speed() * v.S().mass > o.strength) { g.map.BreakObject(sh.obj, v.vel, g.fx, true); e.broke = true; }
        events.push_back(e);
    }
    // a rail car that keeps pushing on something stops being kinematic
    for (int i = 0; i < nv; i++) {
        Vehicle& v = V[i];
        if (!v.active || !bodies[i].kin) continue;
        if (shoved[i]) { v.ai.shove += dt; if (v.ai.shove > KNOCK_SHOVE_TIME) AIKnock(v); }
        else v.ai.shove = std::max(0.0f, v.ai.shove - dt);
    }
}
