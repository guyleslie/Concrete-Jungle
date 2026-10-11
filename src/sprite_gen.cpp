// =====================================================================================
//  Procedural sprite generator - see sprite_gen.h
// =====================================================================================
#include "sprite_gen.h"
#include "math_utils.h"
#include <vector>
#include <algorithm>

namespace spritegen {

// -------------------------------------------------------------------------------------
//  Signed distance functions (negative inside, in pixels)
// -------------------------------------------------------------------------------------
static inline float sdCircle(Vector2 p, Vector2 c, float r) { return Len(p - c) - r; }

static inline float sdRoundBox(Vector2 p, Vector2 c, Vector2 half, float r) {
    r = std::min(r, std::min(half.x, half.y));
    float qx = fabsf(p.x - c.x) - half.x + r, qy = fabsf(p.y - c.y) - half.y + r;
    return Len({ std::max(qx, 0.0f), std::max(qy, 0.0f) }) + std::min(std::max(qx, qy), 0.0f) - r;
}

static inline float sdCapsule(Vector2 p, Vector2 a, Vector2 b, float r) {
    Vector2 pa = p - a, ba = b - a;
    float h = Clampf(Dot(pa, ba) / std::max(Dot(ba, ba), 1e-6f), 0.0f, 1.0f);
    return Len(pa - ba * h) - r;
}

// Approximate ellipse distance (good enough for shading/AA at these sizes).
static inline float sdEllipse(Vector2 p, Vector2 c, Vector2 r) {
    Vector2 q = { (p.x - c.x) / r.x, (p.y - c.y) / r.y };
    float k = Len(q);
    return (k - 1.0f) * std::min(r.x, r.y);
}

// -------------------------------------------------------------------------------------
//  Painter
// -------------------------------------------------------------------------------------
struct F4 { float r, g, b, a; };
static inline Vector3 C3(Color c) { return { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f }; }
static inline Vector3 Mul3(Vector3 a, float f) { return { a.x * f, a.y * f, a.z * f }; }
static inline Vector3 Mix3(Vector3 a, Vector3 b, float t) { return { Lerpf(a.x, b.x, t), Lerpf(a.y, b.y, t), Lerpf(a.z, b.z, t) }; }
static const Vector3 LIGHT = Vector3Normalize({ -0.45f, -0.62f, 0.66f });
static const Vector3 HALFV = Vector3Normalize(Vector3Add(Vector3Normalize({ -0.45f, -0.62f, 0.66f }), { 0, 0, 1 }));

// Surface material: bevelled, lit from the top-left, optional dark outline.
struct Mat {
    Vector3 base;
    float bevel = 6.0f;      // width of the rounded edge in px
    float spec = 0.35f;      // specular strength
    float shin = 24.0f;      // specular exponent
    float rim = 0.35f;       // edge darkening (when no outline)
    float outline = 0.0f;    // outline width in px (0 = none)
    Vector3 outlineCol = { 0.05f, 0.05f, 0.06f };
    float alpha = 1.0f;
    float bulge = 1.3f;      // how strongly the bevel bends the normal
};
static Mat M(Color c, float bevel = 6, float spec = 0.35f, float shin = 24, float outline = 0) {
    Mat m; m.base = C3(c); m.bevel = bevel; m.spec = spec; m.shin = shin; m.outline = outline; m.alpha = c.a / 255.0f; return m;
}

static F4 Shade(const Mat& m, float d, Vector2 n) {
    float inner = -d - m.outline;                      // distance inside the outline
    float t = 1.0f - SmoothStep(0.0f, m.bevel, inner); // 1 at the edge -> 0 inside
    Vector3 N = Vector3Normalize({ n.x * t * m.bulge, n.y * t * m.bulge, 1.0f });
    float diff = 0.5f + 0.5f * std::max(0.0f, Vector3DotProduct(N, LIGHT));
    float s = powf(std::max(0.0f, Vector3DotProduct(N, HALFV)), m.shin) * m.spec;
    Vector3 c = { m.base.x * diff * 1.1f + s, m.base.y * diff * 1.1f + s, m.base.z * diff * 1.1f + s };
    if (m.outline > 0.0f) {
        float o = SmoothStep(-m.outline - 0.7f, -m.outline + 0.5f, d);
        c = Mix3(c, m.outlineCol, o);
    } else {
        c = Mul3(c, 1.0f - m.rim * SmoothStep(-2.2f, 0.0f, d));
    }
    return { Saturate(c.x), Saturate(c.y), Saturate(c.z), m.alpha };
}

class Painter {
public:
    int w, h;
    std::vector<F4> px;
    Painter(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_, F4{ 0, 0, 0, 0 }) {}

    // Generic fill: 'sdf(p)' distance, 'shade(p, d, n)' -> straight-alpha colour.
    template <class SDF, class SH>
    void Fill(Rectangle bb, SDF sdf, SH shade, float opacity = 1.0f) {
        int x0 = std::max(0, (int)floorf(bb.x) - 2), y0 = std::max(0, (int)floorf(bb.y) - 2);
        int x1 = std::min(w - 1, (int)ceilf(bb.x + bb.width) + 2), y1 = std::min(h - 1, (int)ceilf(bb.y + bb.height) + 2);
        const float e = 0.6f;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                Vector2 p = { x + 0.5f, y + 0.5f };
                float d = sdf(p);
                if (d >= 0.75f) continue;
                float cov = Saturate(0.5f - d);
                Vector2 n = Norm({ sdf({ p.x + e, p.y }) - sdf({ p.x - e, p.y }), sdf({ p.x, p.y + e }) - sdf({ p.x, p.y - e }) });
                F4 c = shade(p, d, n);
                float a = c.a * cov * opacity;
                F4& D = px[(size_t)y * w + x];
                D.r = c.r * a + D.r * (1 - a); D.g = c.g * a + D.g * (1 - a); D.b = c.b * a + D.b * (1 - a);
                D.a = a + D.a * (1 - a);
            }
    }
    template <class SDF>
    void Mat(Rectangle bb, SDF sdf, const spritegen::Mat& m, float opacity = 1.0f) {
        Fill(bb, sdf, [&](Vector2, float d, Vector2 n) { return Shade(m, d, n); }, opacity);
    }
    // Glass: vertical gradient + diagonal sky reflection.
    template <class SDF>
    void Glass(Rectangle bb, SDF sdf, Color top, Color bot, float refl = 0.22f) {
        Vector3 a = C3(top), b = C3(bot);
        Fill(bb, sdf, [&](Vector2 p, float d, Vector2) {
            float t = Saturate((p.y - bb.y) / std::max(bb.height, 1.0f));
            Vector3 c = Mix3(a, b, t);
            float diag = (p.x - bb.x) / std::max(bb.width, 1.0f) + t * 0.8f;
            float streak = SmoothStep(0.30f, 0.36f, diag) * (1.0f - SmoothStep(0.48f, 0.56f, diag)) * refl;
            c = { c.x + streak, c.y + streak, c.z + streak };
            float edge = SmoothStep(-2.0f, 0.0f, d);
            c = Mul3(c, 1.0f - 0.5f * edge);
            return F4{ Saturate(c.x), Saturate(c.y), Saturate(c.z), 1.0f };
        });
    }

    // Soft drop shadow / ambient occlusion blob under a shape.
    template <class SDF>
    void Shadow(Rectangle bb, SDF sdf, float soft, float strength) {
        Fill({ bb.x - soft, bb.y - soft, bb.width + soft * 2, bb.height + soft * 2 },
             [&](Vector2 p) { return sdf(p) - soft; },
             [&](Vector2 p, float, Vector2) {
                 float d = sdf(p);
                 float a = (1.0f - SmoothStep(-soft * 0.3f, soft, d)) * strength;
                 return F4{ 0, 0, 0, a };
             });
    }

    Image ToImage() const {
        Image img = GenImageColor(w, h, BLANK);
        Color* out = (Color*)img.data;
        for (size_t i = 0; i < px.size(); i++) {
            const F4& p = px[i];
            if (p.a <= 0.0001f) { out[i] = { 0, 0, 0, 0 }; continue; }
            // premultiplied -> straight alpha
            out[i] = { (unsigned char)(Saturate(p.r / p.a) * 255), (unsigned char)(Saturate(p.g / p.a) * 255),
                       (unsigned char)(Saturate(p.b / p.a) * 255), (unsigned char)(Saturate(p.a) * 255) };
        }
        return img;
    }
};

// Value noise (for foliage texture and irregular edges)
static float Hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}
static float VNoise(Vector2 p) {
    int ix = (int)floorf(p.x), iy = (int)floorf(p.y);
    float fx = p.x - ix, fy = p.y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = Hash2(ix, iy), b = Hash2(ix + 1, iy), c = Hash2(ix, iy + 1), d = Hash2(ix + 1, iy + 1);
    return Lerpf(Lerpf(a, b, fx), Lerpf(c, d, fx), fy);
}

static Rectangle BB(Vector2 c, float hx, float hy) { return { c.x - hx, c.y - hy, hx * 2, hy * 2 }; }
static Rectangle BBC(Vector2 a, Vector2 b, float r) {
    return { std::min(a.x, b.x) - r, std::min(a.y, b.y) - r, fabsf(a.x - b.x) + 2 * r, fabsf(a.y - b.y) + 2 * r };
}
static Color Shift(Color c, float f) { return ColorMul(c, f); }

// Palette helpers
static const Color METAL_DARK = { 48, 50, 54, 255 };
static const Color GLASS_TOP = { 22, 30, 42, 255 };
static const Color GLASS_BOT = { 70, 92, 112, 255 };

// -------------------------------------------------------------------------------------
//  Elevated metro carriage (canvas 100 x 600 -> 18 m x 3 m)
// -------------------------------------------------------------------------------------
Image TrainCar(Color stripe) {
    const float cx = 50;
    Painter P(100, 600);
    Mat body = M({ 196, 200, 206, 255 }, 10, 0.6f, 30);
    P.Mat(BB({ cx, 300 }, 48, 298), [=](Vector2 p) { return sdRoundBox(p, { cx, 300 }, { 47, 296 }, 14); }, body);
    for (int sx = -1; sx <= 1; sx += 2) {                       // window bands
        Vector2 c = { cx + sx * 42, 300 };
        P.Glass(BB(c, 4, 280), [=](Vector2 p) { return sdRoundBox(p, c, { 3.5f, 278 }, 2); }, GLASS_TOP, GLASS_BOT, 0.0f);
        Vector2 s2 = { cx + sx * 34, 300 };
        P.Mat(BB(s2, 3, 282), [=](Vector2 p) { return sdRoundBox(p, s2, { 2.5f, 280 }, 1); }, M(stripe, 1.5f, 0.3f));
    }
    for (int i = 0; i < 4; i++) {                               // roof equipment
        float y = 90.0f + i * 140;
        P.Shadow(BB({ cx + 3, y + 4 }, 20, 30), [=](Vector2 p) { return sdRoundBox(p, { cx + 3, y + 4 }, { 18, 28 }, 5); }, 4, 0.3f);
        P.Mat(BB({ cx, y }, 20, 30), [=](Vector2 p) { return sdRoundBox(p, { cx, y }, { 18, 28 }, 5); }, M({ 150, 154, 160, 255 }, 4, 0.5f));
        for (int k = -2; k <= 2; k++) {
            float yy = y + k * 9.0f;
            P.Fill(BB({ cx, yy }, 15, 1), [=](Vector2 p) { return sdRoundBox(p, { cx, yy }, { 14, 0.8f }, 0.4f); },
                   [](Vector2, float, Vector2) { return F4{ 0, 0, 0, 0.3f }; });
        }
    }
    P.Glass(BB({ cx, 12 }, 36, 8), [=](Vector2 p) { return sdRoundBox(p, { cx, 12 }, { 34, 7 }, 5); }, GLASS_TOP, GLASS_BOT);
    P.Glass(BB({ cx, 588 }, 36, 8), [=](Vector2 p) { return sdRoundBox(p, { cx, 588 }, { 34, 7 }, 5); }, GLASS_BOT, GLASS_TOP);
    return P.ToImage();
}

// -------------------------------------------------------------------------------------
//  Street furniture
// -------------------------------------------------------------------------------------
Image MakeProp(Prop kind) {
    switch (kind) {
    case Prop::Bench: {             // 1.8 x 0.6 m
        Painter P(96, 34);
        for (float x : { 10.0f, 86.0f })
            P.Mat(BB({ x, 17 }, 4, 16), [=](Vector2 p) { return sdRoundBox(p, { x, 17 }, { 3, 15 }, 1.5f); }, M(METAL_DARK, 2, 0.5f));
        for (int i = 0; i < 3; i++) {
            float y = 8.0f + i * 9;
            P.Mat(BB({ 48, y }, 45, 4), [=](Vector2 p) { return sdRoundBox(p, { 48, y }, { 44, 3.4f }, 1.5f); }, M({ 150, 100, 60, 255 }, 2, 0.25f));
        }
        return P.ToImage();
    }
    case Prop::TrashBin: {          // 0.6 m
        Painter P(40, 40);
        P.Mat(BB({ 20, 20 }, 18, 18), [](Vector2 p) { return sdCircle(p, { 20, 20 }, 17.5f); }, M({ 40, 85, 55, 255 }, 5, 0.6f, 30));
        P.Mat(BB({ 20, 20 }, 12, 12), [](Vector2 p) { return sdCircle(p, { 20, 20 }, 11.5f); }, M({ 30, 60, 40, 255 }, 3, 0.3f));
        P.Mat(BB({ 20, 20 }, 6, 3), [](Vector2 p) { return sdRoundBox(p, { 20, 20 }, { 5, 2 }, 1); }, M(METAL_DARK, 1, 0.6f));
        return P.ToImage();
    }
    case Prop::Hydrant: {           // 0.45 m
        Painter P(28, 28);
        for (int sx = -1; sx <= 1; sx += 2) {
            Vector2 c = { 14.0f + sx * 10, 14 };
            P.Mat(BB(c, 4, 4), [=](Vector2 p) { return sdCircle(p, c, 3.5f); }, M({ 170, 20, 20, 255 }, 2, 0.6f));
        }
        P.Mat(BB({ 14, 14 }, 10, 10), [](Vector2 p) { return sdCircle(p, { 14, 14 }, 9.5f); }, M({ 205, 30, 30, 255 }, 4, 0.9f, 40));
        P.Mat(BB({ 14, 14 }, 5, 5), [](Vector2 p) { return sdCircle(p, { 14, 14 }, 4.5f); }, M({ 230, 200, 60, 255 }, 2, 0.7f));
        return P.ToImage();
    }
    case Prop::Cone: {              // 0.45 m
        Painter P(28, 28);
        P.Mat(BB({ 14, 14 }, 13, 13), [](Vector2 p) { return sdRoundBox(p, { 14, 14 }, { 12.5f, 12.5f }, 3); }, M({ 40, 40, 40, 255 }, 2, 0.2f));
        P.Mat(BB({ 14, 14 }, 10, 10), [](Vector2 p) { return sdCircle(p, { 14, 14 }, 9.5f); }, M({ 245, 110, 20, 255 }, 5, 0.6f));
        P.Mat(BB({ 14, 14 }, 7, 7), [](Vector2 p) { return sdCircle(p, { 14, 14 }, 6.5f); }, M({ 240, 240, 240, 255 }, 3, 0.4f));
        P.Mat(BB({ 14, 14 }, 4, 4), [](Vector2 p) { return sdCircle(p, { 14, 14 }, 3.5f); }, M({ 250, 130, 30, 255 }, 2, 0.6f));
        return P.ToImage();
    }
    case Prop::Barrel: {            // 0.6 m oil drum
        Painter P(40, 40);
        P.Mat(BB({ 20, 20 }, 18, 18), [](Vector2 p) { return sdCircle(p, { 20, 20 }, 17.5f); }, M({ 180, 35, 30, 255 }, 7, 0.7f, 30));
        for (float rr : { 13.0f, 8.5f })
            P.Fill(BB({ 20, 20 }, rr + 1, rr + 1), [=](Vector2 p) { return fabsf(sdCircle(p, { 20, 20 }, rr)) - 0.8f; },
                   [](Vector2, float, Vector2) { return F4{ 0, 0, 0, 0.35f }; });
        P.Mat(BB({ 26, 14 }, 3, 3), [](Vector2 p) { return sdCircle(p, { 26, 14 }, 2.5f); }, M({ 200, 200, 200, 255 }, 1, 0.8f));
        return P.ToImage();
    }
    case Prop::Dumpster: {          // 2.0 x 1.2 m
        Painter P(100, 60);
        P.Mat(BB({ 50, 30 }, 49, 29), [](Vector2 p) { return sdRoundBox(p, { 50, 30 }, { 48, 28 }, 4); }, M({ 40, 90, 60, 255 }, 4, 0.4f));
        for (float x : { 26.0f, 74.0f })
            P.Mat(BB({ x, 30 }, 22, 24), [=](Vector2 p) { return sdRoundBox(p, { x, 30 }, { 21, 23 }, 3); }, M({ 50, 105, 70, 255 }, 3, 0.35f));
        P.Mat(BB({ 50, 6 }, 40, 3), [](Vector2 p) { return sdRoundBox(p, { 50, 6 }, { 38, 1.6f }, 1); }, M(METAL_DARK, 1, 0.5f));
        return P.ToImage();
    }
    case Prop::ACUnit: {            // 1.6 m rooftop unit
        Painter P(64, 64);
        P.Mat(BB({ 32, 32 }, 31, 31), [](Vector2 p) { return sdRoundBox(p, { 32, 32 }, { 30, 30 }, 4); }, M({ 185, 188, 192, 255 }, 4, 0.5f));
        for (int i = 0; i < 6; i++) {
            float y = 8.0f + i * 3.2f;
            P.Fill(BB({ 32, y }, 24, 1), [=](Vector2 p) { return sdRoundBox(p, { 32, y }, { 22, 0.6f }, 0.3f); },
                   [](Vector2, float, Vector2) { return F4{ 0, 0, 0, 0.3f }; });
        }
        P.Mat(BB({ 32, 38 }, 20, 20), [](Vector2 p) { return sdCircle(p, { 32, 38 }, 19); }, M({ 45, 48, 52, 255 }, 3, 0.2f));
        for (int i = 0; i < 3; i++) {
            float a = i * 2.0944f; Vector2 b = { 32 + cosf(a) * 15, 38 + sinf(a) * 15 };
            P.Mat(BBC({ 32, 38 }, b, 4), [=](Vector2 p) { return sdCapsule(p, { 32, 38 }, b, 3.2f); }, M({ 130, 132, 138, 255 }, 2, 0.5f));
        }
        P.Mat(BB({ 32, 38 }, 5, 5), [](Vector2 p) { return sdCircle(p, { 32, 38 }, 4); }, M({ 160, 160, 165, 255 }, 2, 0.7f));
        return P.ToImage();
    }
    case Prop::LampHead: {          // luminaire seen from above, 1.0 x 0.4 m
        Painter P(48, 20);
        P.Mat(BB({ 24, 10 }, 23, 9), [](Vector2 p) { return sdRoundBox(p, { 24, 10 }, { 22, 8 }, 7); }, M({ 58, 62, 66, 255 }, 4, 0.7f, 30));
        P.Mat(BB({ 30, 10 }, 12, 5), [](Vector2 p) { return sdEllipse(p, { 30, 10 }, { 11, 4.5f }); }, M({ 150, 150, 140, 255 }, 3, 0.5f));
        return P.ToImage();
    }
    case Prop::SignalHead: {        // traffic light housing, 0.4 x 1.0 m
        Painter P(22, 50);
        P.Mat(BB({ 11, 25 }, 10, 24), [](Vector2 p) { return sdRoundBox(p, { 11, 25 }, { 9.5f, 23.5f }, 3); }, M({ 34, 36, 38, 255 }, 3, 0.5f));
        for (int i = 0; i < 3; i++) {
            float y = 10.0f + i * 15;
            P.Mat(BB({ 11, y }, 6, 6), [=](Vector2 p) { return sdCircle(p, { 11, y }, 5.2f); }, M({ 18, 18, 20, 255 }, 2, 0.7f, 40));
        }
        return P.ToImage();
    }
    case Prop::Planter: {           // 1.2 m concrete planter with shrub
        Painter P(64, 64);
        P.Mat(BB({ 32, 32 }, 31, 31), [](Vector2 p) { return sdRoundBox(p, { 32, 32 }, { 30, 30 }, 5); }, M({ 170, 165, 155, 255 }, 4, 0.2f));
        P.Mat(BB({ 32, 32 }, 25, 25), [](Vector2 p) { return sdRoundBox(p, { 32, 32 }, { 24, 24 }, 3); }, M({ 70, 50, 35, 255 }, 2, 0.05f));
        Rng r(5);
        for (int i = 0; i < 14; i++) {
            Vector2 c = { 32 + r.Range(-12, 12), 32 + r.Range(-12, 12) };
            float rad = r.Range(7, 11);
            Color col = LerpColor({ 40, 90, 35, 255 }, { 110, 160, 70, 255 }, Saturate(0.5f - (c.x + c.y - 64) / 40.0f));
            Mat m = M(col, rad, 0.1f, 10); m.rim = 0.4f;
            P.Mat(BB(c, rad, rad), [=](Vector2 p) { return sdCircle(p, c, rad); }, m);
        }
        return P.ToImage();
    }
    case Prop::BusShelter: {        // 4.0 x 1.6 m glass roof on a steel frame
        Painter P(192, 78);
        P.Mat(BB({ 96, 39 }, 95, 38), [](Vector2 p) { return sdRoundBox(p, { 96, 39 }, { 94, 37 }, 5); }, M({ 70, 74, 80, 255 }, 3, 0.6f, 30));
        P.Fill(BB({ 96, 39 }, 90, 33), [](Vector2 p) { return sdRoundBox(p, { 96, 39 }, { 89, 32 }, 3); },
               [](Vector2 p, float, Vector2) {
                   float t = (p.x - 6) / 180.0f;
                   float streak = SmoothStep(0.25f, 0.3f, t + p.y / 200.0f) * (1 - SmoothStep(0.36f, 0.44f, t + p.y / 200.0f)) * 0.25f;
                   return F4{ 0.55f + streak, 0.68f + streak, 0.75f + streak, 0.62f };
               });
        for (float x : { 48.0f, 96.0f, 144.0f })
            P.Mat(BB({ x, 39 }, 3, 34), [=](Vector2 p) { return sdRoundBox(p, { x, 39 }, { 2, 33 }, 1); }, M({ 70, 74, 80, 255 }, 1, 0.5f));
        P.Mat(BB({ 186, 39 }, 5, 34), [](Vector2 p) { return sdRoundBox(p, { 185, 39 }, { 4, 32 }, 2); }, M({ 230, 120, 30, 255 }, 2, 0.4f)); // ad panel
        return P.ToImage();
    }
    case Prop::PhoneBooth: {        // 1 x 1 m
        Painter P(48, 48);
        P.Mat(BB({ 24, 24 }, 23, 23), [](Vector2 p) { return sdRoundBox(p, { 24, 24 }, { 22, 22 }, 4); }, M({ 40, 44, 50, 255 }, 4, 0.6f, 30));
        P.Mat(BB({ 24, 24 }, 17, 17), [](Vector2 p) { return sdRoundBox(p, { 24, 24 }, { 16, 16 }, 3); }, M({ 30, 90, 170, 255 }, 3, 0.5f));
        P.Mat(BB({ 24, 24 }, 12, 4), [](Vector2 p) { return sdRoundBox(p, { 24, 24 }, { 11, 3 }, 1.5f); }, M({ 235, 235, 240, 255 }, 1, 0.3f));
        return P.ToImage();
    }
    case Prop::ParasolRed: case Prop::ParasolBlue: case Prop::ParasolGreen: {   // 2.4 m cafe umbrella
        Color a = kind == Prop::ParasolRed ? Color{ 200, 40, 40, 255 } : kind == Prop::ParasolBlue ? Color{ 40, 90, 180, 255 } : Color{ 40, 140, 80, 255 };
        Color b = { 240, 236, 225, 255 };
        Painter P(116, 116);
        const float c = 58, R = 55;
        auto oct = [=](Vector2 p) {                       // octagon
            Vector2 q = { fabsf(p.x - c), fabsf(p.y - c) };
            float d1 = std::max(q.x, q.y), d2 = (q.x + q.y) * 0.7071f;
            return std::max(d1, d2) - R;
        };
        P.Fill(BB({ c, c }, R, R), oct, [=](Vector2 p, float d, Vector2) {
            float ang = atan2f(p.y - c, p.x - c) + PI;
            int seg = (int)(ang / (PI / 4.0f)) % 8;
            float within = fmodf(ang, PI / 4.0f) / (PI / 4.0f);      // 0..1 across a panel
            Vector3 col = C3((seg & 1) ? a : b);
            float r = Len({ p.x - c, p.y - c }) / R;
            float lit = 0.72f + 0.28f * (1.0f - fabsf(within - 0.35f) * 1.6f) - 0.12f * r;   // folded fabric ridges
            float edge = SmoothStep(-2.0f, 0.0f, d);
            lit *= 1.0f - 0.4f * edge;
            return F4{ Saturate(col.x * lit), Saturate(col.y * lit), Saturate(col.z * lit), 1.0f };
        });
        P.Mat(BB({ c, c }, 5, 5), [=](Vector2 p) { return sdCircle(p, { c, c }, 4.5f); }, M({ 200, 190, 170, 255 }, 2, 0.6f));
        return P.ToImage();
    }
    case Prop::Bollard: {           // 0.25 m
        Painter P(16, 16);
        P.Mat(BB({ 8, 8 }, 7, 7), [](Vector2 p) { return sdCircle(p, { 8, 8 }, 6.5f); }, M({ 50, 52, 56, 255 }, 3, 0.8f, 40));
        P.Fill(BB({ 8, 8 }, 5, 5), [](Vector2 p) { return fabsf(sdCircle(p, { 8, 8 }, 4.2f)) - 0.7f; },
               [](Vector2, float, Vector2) { return F4{ 0.95f, 0.85f, 0.2f, 0.9f }; });
        return P.ToImage();
    }
    case Prop::Mailbox: {           // 0.5 x 0.4 m
        Painter P(30, 26);
        Mat m = M({ 30, 70, 160, 255 }, 6, 0.8f, 30); m.bulge = 1.8f;
        P.Mat(BB({ 15, 13 }, 14, 12), [](Vector2 p) { return sdRoundBox(p, { 15, 13 }, { 13, 11 }, 7); }, m);
        P.Mat(BB({ 15, 6 }, 7, 2), [](Vector2 p) { return sdRoundBox(p, { 15, 6 }, { 6, 1.2f }, 0.6f); }, M({ 20, 20, 25, 255 }, 1, 0.1f));
        return P.ToImage();
    }
    case Prop::NewsBox: {           // 0.5 x 0.45 m
        Painter P(30, 28);
        P.Mat(BB({ 15, 14 }, 14, 13), [](Vector2 p) { return sdRoundBox(p, { 15, 14 }, { 13, 12 }, 2); }, M({ 220, 170, 30, 255 }, 3, 0.6f));
        P.Glass(BB({ 15, 9 }, 10, 5), [](Vector2 p) { return sdRoundBox(p, { 15, 9 }, { 9, 4 }, 1); }, GLASS_BOT, GLASS_TOP, 0.2f);
        return P.ToImage();
    }
    case Prop::Manhole: {           // 0.7 m cast iron (ground decal)
        Painter P(36, 36);
        P.Mat(BB({ 18, 18 }, 17, 17), [](Vector2 p) { return sdCircle(p, { 18, 18 }, 16.5f); }, M({ 60, 58, 55, 255 }, 2, 0.3f));
        for (int i = -3; i <= 3; i++) {
            float o = i * 4.2f;
            P.Fill(BB({ 18 + o, 18 }, 1, 15), [=](Vector2 p) { return std::max(fabsf(p.x - 18 - o) - 0.8f, sdCircle(p, { 18, 18 }, 13)); },
                   [](Vector2, float, Vector2) { return F4{ 0, 0, 0, 0.35f }; });
            P.Fill(BB({ 18, 18 + o }, 15, 1), [=](Vector2 p) { return std::max(fabsf(p.y - 18 - o) - 0.8f, sdCircle(p, { 18, 18 }, 13)); },
                   [](Vector2, float, Vector2) { return F4{ 0, 0, 0, 0.35f }; });
        }
        return P.ToImage();
    }
    case Prop::Drain: {             // 0.8 x 0.35 m storm drain grate
        Painter P(40, 18);
        P.Mat(BB({ 20, 9 }, 19, 8), [](Vector2 p) { return sdRoundBox(p, { 20, 9 }, { 18.5f, 8 }, 1.5f); }, M({ 55, 55, 55, 255 }, 1.5f, 0.3f));
        for (int i = 0; i < 7; i++) {
            float x = 6.0f + i * 4.7f;
            P.Fill(BB({ x, 9 }, 1.5f, 6), [=](Vector2 p) { return sdRoundBox(p, { x, 9 }, { 1.0f, 5.5f }, 0.5f); },
                   [](Vector2, float, Vector2) { return F4{ 0.02f, 0.02f, 0.02f, 0.95f }; });
        }
        return P.ToImage();
    }
    case Prop::PicnicTable: {       // 1.8 x 1.6 m
        Painter P(88, 78);
        Color wood = { 160, 110, 65, 255 };
        for (float y : { 10.0f, 68.0f })
            P.Mat(BB({ 44, y }, 42, 7), [=](Vector2 p) { return sdRoundBox(p, { 44, y }, { 41, 6 }, 2); }, M(Shift(wood, 0.9f), 2, 0.2f));
        for (int i = 0; i < 4; i++) {
            float y = 26.0f + i * 9;
            P.Mat(BB({ 44, y }, 43, 4.5f), [=](Vector2 p) { return sdRoundBox(p, { 44, y }, { 42, 4 }, 1.5f); }, M(wood, 2, 0.25f));
        }
        return P.ToImage();
    }
    case Prop::Crate: {             // 1 m wooden crate
        Painter P(48, 48);
        Color wood = { 175, 130, 80, 255 };
        P.Mat(BB({ 24, 24 }, 23, 23), [](Vector2 p) { return sdRoundBox(p, { 24, 24 }, { 22, 22 }, 2); }, M(Shift(wood, 0.8f), 3, 0.2f));
        for (int i = 0; i < 4; i++) {
            float y = 9.0f + i * 10;
            P.Mat(BB({ 24, y }, 19, 4.5f), [=](Vector2 p) { return sdRoundBox(p, { 24, y }, { 18, 4 }, 1); }, M(Shift(wood, 1.0f + 0.05f * (i & 1)), 1.5f, 0.2f));
        }
        P.Mat(BBC({ 6, 6 }, { 42, 42 }, 4), [](Vector2 p) { return sdCapsule(p, { 7, 7 }, { 41, 41 }, 3.2f); }, M(Shift(wood, 0.9f), 2, 0.25f));
        return P.ToImage();
    }
    case Prop::Rock: {              // ~1 m boulder
        Painter P(52, 48);
        P.Fill(BB({ 26, 24 }, 24, 22), [](Vector2 p) {
                   return sdEllipse(p, { 26, 24 }, { 22, 19 }) - (VNoise({ p.x / 7.0f, p.y / 7.0f }) - 0.5f) * 7.0f; },
               [](Vector2 p, float d, Vector2 n) {
                   Mat m = M({ 128, 124, 118, 255 }, 12, 0.15f, 10); m.bulge = 1.6f;
                   F4 f = Shade(m, d, n);
                   float t = 0.85f + 0.3f * VNoise({ p.x / 3.0f + 9, p.y / 3.0f });
                   f.r *= t; f.g *= t; f.b *= t; return f;
               });
        return P.ToImage();
    }
    case Prop::WaterTank: {         // 3 m rooftop water tank (conical lid)
        Painter P(144, 144);
        const float c = 72;
        P.Fill(BB({ c, c }, 70, 70), [=](Vector2 p) { return sdCircle(p, { c, c }, 68); },
               [=](Vector2 p, float d, Vector2) {
                   Vector2 v = { p.x - c, p.y - c };
                   float r = Len(v) / 68.0f;
                   Vector2 dir = Norm(v);
                   // cone lit from the top-left, with radial plank lines
                   float lit = 0.62f + 0.38f * Saturate(-(dir.x * 0.6f + dir.y * 0.8f)) * r + 0.1f * (1 - r);
                   float ang = atan2f(v.y, v.x);
                   float plank = 0.92f + 0.08f * (fmodf(ang * 24.0f / PI + 100.0f, 2.0f) < 1.0f ? 1.0f : 0.0f);
                   float edge = SmoothStep(-3.0f, 0.0f, d);
                   float k = lit * plank * (1.0f - 0.45f * edge);
                   return F4{ 0.55f * k, 0.45f * k, 0.36f * k, 1.0f };
               });
        P.Mat(BB({ c, c }, 7, 7), [=](Vector2 p) { return sdCircle(p, { c, c }, 6); }, M(METAL_DARK, 3, 0.6f));
        return P.ToImage();
    }
    case Prop::Vent: {              // 0.8 m rooftop mushroom vent
        Painter P(40, 40);
        P.Mat(BB({ 20, 20 }, 19, 19), [](Vector2 p) { return sdCircle(p, { 20, 20 }, 18); }, M({ 170, 172, 176, 255 }, 8, 0.7f, 30));
        P.Fill(BB({ 20, 20 }, 12, 12), [](Vector2 p) { return fabsf(sdCircle(p, { 20, 20 }, 10)) - 0.8f; },
               [](Vector2, float, Vector2) { return F4{ 0, 0, 0, 0.3f }; });
        return P.ToImage();
    }
    default: return GenImageColor(4, 4, BLANK);
    }
}

} // namespace spritegen
