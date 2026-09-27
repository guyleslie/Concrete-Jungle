// =====================================================================================
//  Small math / geometry helpers shared by every module.
//  raymath.h provides the Vector2 C++ operators (+, -, *, /) used throughout.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "raymath.h"
#include <cmath>
#include <cstdint>
#include <algorithm>

// ---- Random numbers (fast xorshift, deterministic per seed) -------------------------
struct Rng {
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 1234567u) : s(seed ? seed : 1u) {}
    uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float    Float() { return (Next() & 0xFFFFFF) / (float)0x1000000; }          // [0,1)
    float    Range(float a, float b) { return a + (b - a) * Float(); }
    int      Int(int a, int b) { return a + (int)(Next() % (uint32_t)(b - a + 1)); } // [a,b]
    bool     Chance(float p) { return Float() < p; }
};
Rng& GRng();   // global gameplay RNG (defined in game.cpp)

// ---- Scalars ------------------------------------------------------------------------
inline float Clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float Saturate(float v) { return Clampf(v, 0.0f, 1.0f); }
inline float Lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float SmoothStep(float e0, float e1, float x) { float t = Saturate((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }
// Frame-rate independent exponential smoothing factor.
inline float Damp(float rate, float dt) { return 1.0f - expf(-rate * dt); }
inline float WrapAngle(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
inline float Sign(float v) { return v < 0 ? -1.0f : 1.0f; }

// ---- Vectors ------------------------------------------------------------------------
// Angle convention: 0 = facing up (-Y, screen north), increasing clockwise.
// This matches raylib's DrawTexturePro rotation, so sprite rotation = angle * RAD2DEG.
inline Vector2 Forward(float a) { return { sinf(a), -cosf(a) }; }
inline Vector2 RightOf(float a) { return { cosf(a), sinf(a) }; }
inline float   AngleOf(Vector2 dir) { return atan2f(dir.x, -dir.y); }
inline float   Dot(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
inline float   Cross(Vector2 a, Vector2 b) { return a.x * b.y - a.y * b.x; }
inline float   Len(Vector2 v) { return sqrtf(v.x * v.x + v.y * v.y); }
inline float   Len2(Vector2 v) { return v.x * v.x + v.y * v.y; }
inline Vector2 Norm(Vector2 v) { float l = Len(v); return l > 1e-6f ? Vector2{ v.x / l, v.y / l } : Vector2{ 0, 0 }; }
inline Vector2 Perp(Vector2 v) { return { -v.y, v.x }; }
inline Vector2 V2(float x, float y) { return { x, y }; }
inline Vector2 LerpV(Vector2 a, Vector2 b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }
inline float   Dist(Vector2 a, Vector2 b) { return Len(b - a); }
inline Vector2 QuadBezier(Vector2 a, Vector2 c, Vector2 b, float t) {
    float u = 1 - t; return a * (u * u) + c * (2 * u * t) + b * (t * t);
}

// ---- Colours ------------------------------------------------------------------------
inline Color ColorMul(Color c, float f) {
    return { (unsigned char)Clampf(c.r * f, 0, 255), (unsigned char)Clampf(c.g * f, 0, 255),
             (unsigned char)Clampf(c.b * f, 0, 255), c.a };
}
inline Color ColorMulRGB(Color c, Vector3 f) {
    return { (unsigned char)Clampf(c.r * f.x, 0, 255), (unsigned char)Clampf(c.g * f.y, 0, 255),
             (unsigned char)Clampf(c.b * f.z, 0, 255), c.a };
}
inline Color ColorA(Color c, float a) { c.a = (unsigned char)Clampf(a * 255.0f, 0, 255); return c; }
inline Color LerpColor(Color a, Color b, float t) {
    return { (unsigned char)Lerpf(a.r, b.r, t), (unsigned char)Lerpf(a.g, b.g, t),
             (unsigned char)Lerpf(a.b, b.b, t), (unsigned char)Lerpf(a.a, b.a, t) };
}

// ---- Oriented boxes (Separating Axis Test) -----------------------------------------
struct OBB {
    Vector2 c;          // centre
    Vector2 ax[2];      // unit axes (x = right, y = forward)
    float   he[2];      // half extents along the axes
};

inline OBB MakeOBB(Vector2 c, float angle, float halfW, float halfL) {
    OBB b; b.c = c; b.ax[0] = RightOf(angle); b.ax[1] = Forward(angle); b.he[0] = halfW; b.he[1] = halfL; return b;
}
inline OBB MakeAABB(Rectangle r) {
    OBB b; b.c = { r.x + r.width * 0.5f, r.y + r.height * 0.5f };
    b.ax[0] = { 1, 0 }; b.ax[1] = { 0, 1 }; b.he[0] = r.width * 0.5f; b.he[1] = r.height * 0.5f; return b;
}
inline void OBBCorners(const OBB& b, Vector2 out[4]) {
    Vector2 x = b.ax[0] * b.he[0], y = b.ax[1] * b.he[1];
    out[0] = b.c - x - y; out[1] = b.c + x - y; out[2] = b.c + x + y; out[3] = b.c - x + y;
}
inline float OBBProjectRadius(const OBB& b, Vector2 axis) {
    return b.he[0] * fabsf(Dot(b.ax[0], axis)) + b.he[1] * fabsf(Dot(b.ax[1], axis));
}
inline bool PointInOBB(const OBB& b, Vector2 p, float margin = 0.0f) {
    Vector2 d = p - b.c;
    return fabsf(Dot(d, b.ax[0])) <= b.he[0] + margin && fabsf(Dot(d, b.ax[1])) <= b.he[1] + margin;
}
// Returns true on overlap. 'normal' points from a towards b, 'depth' is penetration.
inline bool OBBOverlap(const OBB& a, const OBB& b, Vector2& normal, float& depth) {
    Vector2 axes[4] = { a.ax[0], a.ax[1], b.ax[0], b.ax[1] };
    Vector2 d = b.c - a.c;
    depth = 1e9f;
    normal = { 1, 0 };
    for (int i = 0; i < 4; i++) {
        float ra = OBBProjectRadius(a, axes[i]);
        float rb = OBBProjectRadius(b, axes[i]);
        float dist = Dot(d, axes[i]);
        float overlap = ra + rb - fabsf(dist);
        if (overlap <= 0) return false;
        if (overlap < depth) { depth = overlap; normal = dist < 0 ? axes[i] * -1.0f : axes[i]; }
    }
    return true;
}
// Approximate contact point of two overlapping boxes (average of penetrating corners).
inline Vector2 OBBContactPoint(const OBB& a, const OBB& b) {
    Vector2 ca[4], cb[4]; OBBCorners(a, ca); OBBCorners(b, cb);
    Vector2 sum = { 0, 0 }; int n = 0;
    for (int i = 0; i < 4; i++) { if (PointInOBB(a, cb[i], 1.0f)) { sum = sum + cb[i]; n++; } }
    for (int i = 0; i < 4; i++) { if (PointInOBB(b, ca[i], 1.0f)) { sum = sum + ca[i]; n++; } }
    if (n == 0) return (a.c + b.c) * 0.5f;
    return sum / (float)n;
}
// Closest point on box to p (used for box-vs-circle).
inline Vector2 OBBClosestPoint(const OBB& b, Vector2 p) {
    Vector2 d = p - b.c;
    float x = Clampf(Dot(d, b.ax[0]), -b.he[0], b.he[0]);
    float y = Clampf(Dot(d, b.ax[1]), -b.he[1], b.he[1]);
    return b.c + b.ax[0] * x + b.ax[1] * y;
}
// Circle vs OBB. normal points from box towards circle.
inline bool CircleOBB(Vector2 p, float r, const OBB& b, Vector2& normal, float& depth) {
    normal = { 0, 0 }; depth = 0;
    Vector2 q = OBBClosestPoint(b, p);
    Vector2 d = p - q;
    float l2 = Len2(d);
    if (l2 > r * r) return false;
    if (l2 < 1e-6f) {
        // centre inside box: push out along the shallowest axis
        Vector2 rel = p - b.c; float best = 1e9f;
        for (int i = 0; i < 2; i++) {
            float pr = Dot(rel, b.ax[i]); float pen = b.he[i] - fabsf(pr);
            if (pen < best) { best = pen; normal = b.ax[i] * (pr < 0 ? -1.0f : 1.0f); }
        }
        depth = best + r; return true;
    }
    float l = sqrtf(l2); normal = d / l; depth = r - l; return true;
}
// Segment vs axis aligned rectangle (slab test) - used for line of sight & bullets.
inline bool SegmentRect(Vector2 a, Vector2 b, Rectangle r, float* tHit = nullptr) {
    Vector2 d = b - a; float tmin = 0, tmax = 1;
    float lo[2] = { r.x, r.y }, hi[2] = { r.x + r.width, r.y + r.height };
    float o[2] = { a.x, a.y }, dd[2] = { d.x, d.y };
    for (int i = 0; i < 2; i++) {
        if (fabsf(dd[i]) < 1e-6f) { if (o[i] < lo[i] || o[i] > hi[i]) return false; }
        else {
            float t1 = (lo[i] - o[i]) / dd[i], t2 = (hi[i] - o[i]) / dd[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    if (tHit) *tHit = tmin;
    return true;
}
// Segment vs circle; returns param t of first hit.
inline bool SegmentCircle(Vector2 a, Vector2 b, Vector2 c, float r, float* tHit = nullptr) {
    Vector2 d = b - a, f = a - c;
    float A = Dot(d, d), B = 2 * Dot(f, d), C = Dot(f, f) - r * r;
    float disc = B * B - 4 * A * C;
    if (disc < 0 || A < 1e-6f) return false;
    disc = sqrtf(disc);
    float t = (-B - disc) / (2 * A);
    if (t < 0 || t > 1) { t = (-B + disc) / (2 * A); if (t < 0 || t > 1) return false; }
    if (tHit) *tHit = t;
    return true;
}

// Segment vs oriented box: returns the entry parameter t in [0,1].
inline bool SegmentOBB(Vector2 a, Vector2 b, const OBB& box, float* tHit = nullptr) {
    Vector2 da = a - box.c, db = b - box.c;
    Vector2 la = { Dot(da, box.ax[0]), Dot(da, box.ax[1]) }, lb = { Dot(db, box.ax[0]), Dot(db, box.ax[1]) };
    return SegmentRect(la, lb, { -box.he[0], -box.he[1], box.he[0] * 2, box.he[1] * 2 }, tHit);
}
