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

// Trapezoid centred at c: half width 'topW' at the top edge (smaller y), 'botW' at the
// bottom edge, half height 'he', rounded by 'r'. (after Inigo Quilez)
static inline float sdTrapezoid(Vector2 p, Vector2 c, float topW, float botW, float he, float r) {
    Vector2 q = p - c;
    float r1 = topW - r, r2 = botW - r; he -= r;
    Vector2 k1 = { r2, he }, k2 = { r2 - r1, 2.0f * he };
    q.x = fabsf(q.x);
    Vector2 ca = { q.x - std::min(q.x, (q.y < 0.0f) ? r1 : r2), fabsf(q.y) - he };
    Vector2 cb = q - k1 + k2 * Clampf(Dot(k1 - q, k2) / Dot(k2, k2), 0.0f, 1.0f);
    float s = (cb.x < 0.0f && ca.y < 0.0f) ? -1.0f : 1.0f;
    return s * sqrtf(std::min(Dot(ca, ca), Dot(cb, cb))) - r;
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
static const Color RUBBER = { 26, 26, 28, 255 };
static const Color METAL_DARK = { 48, 50, 54, 255 };
static const Color GLASS_TOP = { 22, 30, 42, 255 };
static const Color GLASS_BOT = { 70, 92, 112, 255 };

// -------------------------------------------------------------------------------------
//  Cars (canvas 120 x 240, front at the top)
// -------------------------------------------------------------------------------------
Image Car(Color paint, CarStyle style) {
    const float W = 120, H = style == CarStyle::Limo ? 310.0f : 240.0f, cx = 60;
    Painter P((int)W, (int)H);
    // Layout (y grows towards the rear). Greenhouse = the whole glass area; the roof
    // panel is painted on top so the glass shows as windscreen, side and rear windows.
    float y0 = 16, y1 = 224, halfW = 44;
    float ghFront = 70, ghRear = 184, roofFront = 96, roofRear = 160;
    if (style == CarStyle::Sedan) { y0 = 10; y1 = 230; ghFront = 72; ghRear = 176; roofFront = 98; roofRear = 150; }
    if (style == CarStyle::Coupe) { halfW = 45; ghFront = 80; ghRear = 176; roofFront = 110; roofRear = 150; }
    if (style == CarStyle::SUV)   { halfW = 46; y0 = 10; y1 = 230; ghFront = 64; ghRear = 204; roofFront = 82; roofRear = 194; }
    if (style == CarStyle::Limo)  { y0 = 10; y1 = 300; ghFront = 72; ghRear = 258; roofFront = 98; roofRear = 240; }
    float bodyC = (y0 + y1) * 0.5f, bodyHe = (y1 - y0) * 0.5f;
    float ghC = (ghFront + ghRear) * 0.5f, ghHe = (ghRear - ghFront) * 0.5f;
    float roofC = (roofFront + roofRear) * 0.5f, roofHe = (roofRear - roofFront) * 0.5f;

    // soft contact shadow + tyres peeking out under the body
    P.Shadow(BB({ cx, bodyC }, halfW, bodyHe), [=](Vector2 p) { return sdRoundBox(p, { cx, bodyC }, { halfW - 2, bodyHe - 2 }, 24); }, 4, 0.35f);
    for (int sx = -1; sx <= 1; sx += 2)
        for (float ty : { y0 + 44.0f, y1 - 42.0f }) {
            Vector2 c = { cx + sx * (halfW - 3), ty };
            P.Mat(BB(c, 9, 18), [=](Vector2 p) { return sdRoundBox(p, c, { 7.5f, 16 }, 5); }, M(RUBBER, 3, 0.15f, 10));
        }
    for (int sx = -1; sx <= 1; sx += 2) {                                   // mirrors
        Vector2 c = { cx + sx * (halfW + 3), ghFront + 6 };
        P.Mat(BB(c, 7, 5), [=](Vector2 p) { return sdEllipse(p, c, { 6.0f, 4.0f }); }, M(Shift(paint, 0.9f), 3, 0.6f));
    }
    // body: rounded box intersected with a slight taper towards the nose
    auto bodySdf = [=](Vector2 p) {
        float a = sdRoundBox(p, { cx, bodyC }, { halfW, bodyHe }, 28);
        float b = sdTrapezoid(p, { cx, bodyC }, halfW - 5, halfW + 1, bodyHe + 1, 0);
        return std::max(a, b);
    };
    Mat body = M(paint, 20, 0.8f, 34); body.bulge = 1.9f; body.rim = 0.45f;
    P.Mat(BB({ cx, bodyC }, halfW + 1, bodyHe + 1), bodySdf, body);
    // hood creases + clear-coat reflections
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 a = { cx + sx * 14, y0 + 16 }, b = { cx + sx * 19, ghFront - 6 };
        P.Fill(BBC(a, b, 2), [=](Vector2 p) { return sdCapsule(p, a, b, 1.0f); },
               [](Vector2, float, Vector2) { return F4{ 0, 0, 0, 0.14f }; });
    }
    auto reflect = [&](Vector2 a, Vector2 b, float r, float alpha) {
        P.Fill(BBC(a, b, r + 2), [=](Vector2 p) { return sdCapsule(p, a, b, r); },
               [=](Vector2, float d, Vector2) { return F4{ 1, 1, 1, alpha * (1.0f - SmoothStep(-r, 0.0f, d)) }; });
    };
    reflect({ cx - 18, y0 + 18 }, { cx - 10, ghFront - 10 }, 6, 0.22f);
    reflect({ cx - 16, ghRear + 8 }, { cx - 12, y1 - 16 }, 4, 0.14f);
    // greenhouse glass, then the roof panel
    P.Glass(BB({ cx, ghC }, 38, ghHe), [=](Vector2 p) { return sdTrapezoid(p, { cx, ghC }, 33, 37, ghHe, 13); }, { 95, 125, 150, 255 }, GLASS_TOP, 0.25f);
    Mat roof = M(Shift(paint, 1.04f), 12, 0.7f, 30); roof.bulge = 1.5f;
    P.Mat(BB({ cx, roofC }, 32, roofHe), [=](Vector2 p) { return sdRoundBox(p, { cx, roofC }, { 31, roofHe }, 11); }, roof);
    reflect({ cx - 18, roofFront + 8 }, { cx - 16, roofRear - 8 }, 3.5f, 0.18f);
    if (style == CarStyle::SUV) {                                   // roof rails + cross bars
        for (int sx = -1; sx <= 1; sx += 2) {
            Vector2 a = { cx + sx * 26, roofFront + 6 }, b = { cx + sx * 26, roofRear - 6 };
            P.Mat(BBC(a, b, 3), [=](Vector2 p) { return sdCapsule(p, a, b, 2.2f); }, M({ 40, 42, 46, 255 }, 1.5f, 0.6f));
        }
        for (float y : { roofFront + 30, roofRear - 30 }) {
            Vector2 a = { cx - 26, y }, b = { cx + 26, y };
            P.Mat(BBC(a, b, 2), [=](Vector2 p) { return sdCapsule(p, a, b, 1.6f); }, M({ 40, 42, 46, 255 }, 1.2f, 0.6f));
        }
    }
    if (style == CarStyle::Limo)                                    // centre divider chrome strip
        P.Mat(BB({ cx, (roofFront + roofRear) * 0.5f }, 3, (roofRear - roofFront) * 0.45f),
              [=](Vector2 p) { return sdRoundBox(p, { cx, (roofFront + roofRear) * 0.5f }, { 1.5f, (roofRear - roofFront) * 0.42f }, 1); },
              M({ 200, 200, 205, 255 }, 1, 1.0f, 50));
    // lights & grille
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 hl = { cx + sx * 29, y0 + 8 };
        P.Mat(BB(hl, 11, 6), [=](Vector2 p) { return sdEllipse(p, hl, { 10, 4.2f }); }, M({ 240, 242, 230, 255 }, 3, 1.0f, 50));
        Vector2 tl = { cx + sx * 30, y1 - 4 };
        P.Mat(BB(tl, 12, 5), [=](Vector2 p) { return sdRoundBox(p, tl, { 10, 3.0f }, 2); }, M({ 200, 22, 28, 255 }, 2, 0.9f, 30));
    }
    P.Mat(BB({ cx, y0 + 4 }, 20, 3), [=](Vector2 p) { return sdRoundBox(p, { cx, y0 + 4 }, { 17, 2.0f }, 1.5f); }, M({ 30, 30, 32, 255 }, 1, 0.2f));
    return P.ToImage();
}

// -------------------------------------------------------------------------------------
//  City bus (canvas 104 x 424 -> 12 m long, 2.55 m wide)
// -------------------------------------------------------------------------------------
Image Bus(Color bodyCol, Color stripe) {
    const float cx = 52;
    Painter P(104, 424);
    for (int sx = -1; sx <= 1; sx += 2) {                       // mirrors
        Vector2 c = { cx + sx * 48, 22 };
        P.Mat(BB(c, 4, 7), [=](Vector2 p) { return sdEllipse(p, c, { 3.5f, 6 }); }, M(RUBBER, 2, 0.3f));
    }
    Mat body = M(bodyCol, 12, 0.6f, 28);
    P.Mat(BB({ cx, 212 }, 46, 206), [=](Vector2 p) { return sdRoundBox(p, { cx, 212 }, { 45, 205 }, 14); }, body);
    P.Glass(BB({ cx, 15 }, 42, 10), [=](Vector2 p) { return sdRoundBox(p, { cx, 15 }, { 40, 9 }, 6); }, GLASS_TOP, GLASS_BOT);
    for (int sx = -1; sx <= 1; sx += 2) {                       // side glazing
        Vector2 c = { cx + sx * 41.5f, 216 };
        P.Glass(BB(c, 3.5f, 180), [=](Vector2 p) { return sdRoundBox(p, c, { 3.2f, 178 }, 2); }, GLASS_TOP, GLASS_BOT, 0.0f);
    }
    P.Mat(BB({ cx, 216 }, 37, 188), [=](Vector2 p) { return sdRoundBox(p, { cx, 216 }, { 36, 186 }, 8); }, M(Shift(bodyCol, 1.07f), 6, 0.25f, 18));
    for (int sx = -1; sx <= 1; sx += 2) {                       // livery stripes
        Vector2 c = { cx + sx * 31, 216 };
        P.Mat(BB(c, 3, 181), [=](Vector2 p) { return sdRoundBox(p, c, { 2.5f, 180 }, 2); }, M(stripe, 1.5f, 0.2f));
    }
    // destination sign
    P.Mat(BB({ cx, 33 }, 25, 5), [=](Vector2 p) { return sdRoundBox(p, { cx, 33 }, { 24, 4 }, 2); }, M({ 20, 20, 22, 255 }, 1, 0.1f));
    for (int i = 0; i < 7; i++) {
        Vector2 c = { cx - 18 + i * 6.0f, 33 };
        P.Fill(BB(c, 2.5f, 2), [=](Vector2 p) { return sdRoundBox(p, c, { 2.0f, 1.4f }, 0.6f); },
               [](Vector2, float, Vector2) { return F4{ 1.0f, 0.62f, 0.1f, 1.0f }; });
    }
    // roof A/C unit with fans
    P.Shadow(BB({ cx + 3, 154 }, 22, 46), [=](Vector2 p) { return sdRoundBox(p, { cx + 3, 154 }, { 21, 45 }, 6); }, 5, 0.35f);
    P.Mat(BB({ cx, 150 }, 22, 46), [=](Vector2 p) { return sdRoundBox(p, { cx, 150 }, { 21, 45 }, 6); }, M({ 200, 202, 206, 255 }, 5, 0.4f));
    for (float fy : { 126.0f, 174.0f }) {
        Vector2 c = { cx, fy };
        P.Mat(BB(c, 11, 11), [=](Vector2 p) { return sdCircle(p, c, 10); }, M({ 55, 58, 62, 255 }, 3, 0.2f));
        P.Mat(BB(c, 4, 4), [=](Vector2 p) { return sdCircle(p, c, 3.5f); }, M({ 150, 150, 155, 255 }, 2, 0.4f));
    }
    for (float hy : { 70.0f, 300.0f }) {                         // roof hatches
        Vector2 c = { cx, hy };
        P.Mat(BB(c, 13, 13), [=](Vector2 p) { return sdRoundBox(p, c, { 12, 12 }, 3); }, M(Shift(bodyCol, 0.85f), 3, 0.3f));
    }
    for (int i = 0; i < 4; i++) {                               // engine vents
        Vector2 c = { cx, 382.0f + i * 7 };
        P.Mat(BB(c, 23, 2), [=](Vector2 p) { return sdRoundBox(p, c, { 22, 1.5f }, 1); }, M({ 40, 40, 44, 255 }, 1, 0.1f));
    }
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 hl = { cx + sx * 32, 8 };
        P.Mat(BB(hl, 9, 4), [=](Vector2 p) { return sdEllipse(p, hl, { 8, 3 }); }, M({ 235, 238, 225, 255 }, 2, 0.9f));
        Vector2 tl = { cx + sx * 34, 414 };
        P.Mat(BB(tl, 8, 4), [=](Vector2 p) { return sdRoundBox(p, tl, { 7, 2.6f }, 1.5f); }, M({ 190, 20, 25, 255 }, 2, 0.8f));
    }
    return P.ToImage();
}

// -------------------------------------------------------------------------------------
//  Box truck (canvas 104 x 300 -> 7.5 m long)
// -------------------------------------------------------------------------------------
Image BoxTruck(Color cab, Color box, Color stripe) {
    const float cx = 52;
    Painter P(104, 300);
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 c = { cx + sx * 48, 30 };
        P.Mat(BB(c, 4, 7), [=](Vector2 p) { return sdEllipse(p, c, { 3.5f, 6.5f }); }, M(RUBBER, 2, 0.3f));
    }
    P.Mat(BB({ cx, 96 }, 27, 9), [=](Vector2 p) { return sdRoundBox(p, { cx, 96 }, { 26, 8 }, 2); }, M({ 35, 35, 38, 255 }, 2, 0.1f));
    Mat cabM = M(cab, 14, 0.7f, 28); cabM.bulge = 1.5f;
    P.Mat(BB({ cx, 48 }, 45, 43), [=](Vector2 p) { return sdRoundBox(p, { cx, 48 }, { 44, 42 }, 18); }, cabM);
    P.Glass(BB({ cx, 28 }, 41, 11), [=](Vector2 p) { return sdTrapezoid(p, { cx, 28 }, 34, 40, 11, 4); }, GLASS_TOP, GLASS_BOT);
    P.Mat(BB({ cx, 64 }, 37, 21), [=](Vector2 p) { return sdRoundBox(p, { cx, 64 }, { 36, 20 }, 8); }, M(Shift(cab, 1.08f), 8, 0.5f));
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 hl = { cx + sx * 30, 9 };
        P.Mat(BB(hl, 10, 4), [=](Vector2 p) { return sdEllipse(p, hl, { 9, 3.2f }); }, M({ 235, 238, 225, 255 }, 2, 0.9f));
    }
    // cargo box
    P.Mat(BB({ cx, 198 }, 48, 100), [=](Vector2 p) { return sdRoundBox(p, { cx, 198 }, { 47, 99 }, 5); }, M(box, 5, 0.2f, 14));
    for (float y = 112; y < 294; y += 15) {
        Vector2 c = { cx, y };
        P.Fill(BB(c, 46, 2), [=](Vector2 p) { return sdRoundBox(p, c, { 44, 1.0f }, 0.5f); },
               [](Vector2, float, Vector2) { return F4{ 0, 0, 0, 0.14f }; });
    }
    P.Mat(BB({ cx, 103 }, 47, 5), [=](Vector2 p) { return sdRoundBox(p, { cx, 103 }, { 46, 4 }, 2); }, M(stripe, 2, 0.3f));
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 c = { cx + sx * 40, 200 };
        P.Mat(BB(c, 4, 88), [=](Vector2 p) { return sdRoundBox(p, c, { 3, 86 }, 2); }, M(stripe, 1.5f, 0.2f));
        Vector2 tl = { cx + sx * 36, 294 };
        P.Mat(BB(tl, 8, 4), [=](Vector2 p) { return sdRoundBox(p, tl, { 7, 2.4f }, 1.2f); }, M({ 190, 20, 25, 255 }, 2, 0.8f));
    }
    return P.ToImage();
}

// -------------------------------------------------------------------------------------
//  Motorbike (canvas 48 x 110 -> 2.1 m long), optional rider
// -------------------------------------------------------------------------------------
static Image Chopper(Color paint, bool rider, Color jacket, Color helmet);
static Image Scooter(Color paint, bool rider, Color jacket, Color helmet);

Image Motorbike(Color paint, bool rider, Color jacket, Color helmet, BikeStyle style) {
    if (style == BikeStyle::Chopper) return Chopper(paint, rider, jacket, helmet);
    if (style == BikeStyle::Scooter) return Scooter(paint, rider, jacket, helmet);
    const float cx = 24;
    Painter P(48, 110);
    // tyres & fork
    P.Mat(BBC({ cx, 78 }, { cx, 104 }, 7), [=](Vector2 p) { return sdCapsule(p, { cx, 78 }, { cx, 104 }, 6.2f); }, M(RUBBER, 3, 0.25f, 12));
    P.Mat(BBC({ cx, 5 }, { cx, 28 }, 6), [=](Vector2 p) { return sdCapsule(p, { cx, 6 }, { cx, 27 }, 5.0f); }, M(RUBBER, 3, 0.25f, 12));
    P.Mat(BBC({ cx, 18 }, { cx, 38 }, 3), [=](Vector2 p) { return sdCapsule(p, { cx, 18 }, { cx, 38 }, 2.4f); }, M({ 160, 162, 168, 255 }, 2, 0.8f, 40));
    // fairing, tank, seat, tail
    Mat pm = M(paint, 5, 0.8f, 32); pm.bulge = 1.6f;
    P.Mat(BB({ cx, 40 }, 11, 13), [=](Vector2 p) { return sdTrapezoid(p, { cx, 40 }, 7, 10.5f, 12, 3); }, pm);
    P.Mat(BB({ cx, 53 }, 10, 12), [=](Vector2 p) { return sdEllipse(p, { cx, 53 }, { 9.5f, 11 }); }, pm);
    P.Mat(BB({ cx, 72 }, 9, 13), [=](Vector2 p) { return sdRoundBox(p, { cx, 72 }, { 8, 12 }, 6); }, M({ 28, 26, 26, 255 }, 4, 0.25f, 14));
    P.Mat(BB({ cx, 89 }, 8, 9), [=](Vector2 p) { return sdTrapezoid(p, { cx, 89 }, 7.5f, 3.5f, 8, 2); }, pm);
    P.Mat(BB({ cx, 27 }, 5, 3), [=](Vector2 p) { return sdEllipse(p, { cx, 27 }, { 4.2f, 2.4f }); }, M({ 240, 240, 225, 255 }, 1.5f, 0.9f));
    P.Mat(BB({ cx, 97 }, 4, 2), [=](Vector2 p) { return sdEllipse(p, { cx, 97 }, { 3, 1.5f }); }, M({ 200, 20, 25, 255 }, 1, 0.8f));
    // handlebars
    P.Mat(BBC({ 6, 38 }, { 42, 38 }, 2), [=](Vector2 p) { return sdCapsule(p, { 7, 38 }, { 41, 38 }, 1.6f); }, M(METAL_DARK, 1.5f, 0.6f));
    if (rider) {
        Color pants = { 40, 42, 50, 255 };
        Mat o = M(jacket, 5, 0.25f, 16);
        for (int sx = -1; sx <= 1; sx += 2) {
            Vector2 k = { cx + sx * 8.5f, 62 };
            P.Mat(BB(k, 5, 9), [=](Vector2 p) { return sdEllipse(p, k, { 4.2f, 8.5f }); }, M(pants, 3, 0.2f));
        }
        P.Mat(BB({ cx, 67 }, 12, 13), [=](Vector2 p) { return sdEllipse(p, { cx, 67 }, { 11.5f, 12 }); }, o);
        for (int sx = -1; sx <= 1; sx += 2) {
            Vector2 a = { cx + sx * 9.5f, 60 }, b = { cx + sx * 16.5f, 40 };
            P.Mat(BBC(a, b, 4), [=](Vector2 p) { return sdCapsule(p, a, b, 3.3f); }, o);
            Vector2 g = { cx + sx * 17, 38.5f };
            P.Mat(BB(g, 4, 4), [=](Vector2 p) { return sdCircle(p, g, 3.1f); }, M({ 25, 25, 25, 255 }, 2, 0.3f));
        }
        Mat hm = M(helmet, 5, 1.0f, 50); hm.bulge = 1.8f;
        P.Mat(BB({ cx, 56 }, 8, 8), [=](Vector2 p) { return sdCircle(p, { cx, 56 }, 7.3f); }, hm);
        P.Glass(BB({ cx, 51 }, 6, 3), [=](Vector2 p) { return sdEllipse(p, { cx, 51.5f }, { 5.2f, 2.4f }); }, GLASS_TOP, GLASS_BOT, 0.3f);
    } else {
        for (int sx = -1; sx <= 1; sx += 2) {
            Vector2 g = { cx + sx * 17, 38 };
            P.Mat(BBC({ g.x - 3, g.y }, { g.x + 3, g.y }, 3), [=](Vector2 p) { return sdCapsule(p, { g.x - 2.5f, g.y }, { g.x + 2.5f, g.y }, 2.4f); },
                  M({ 25, 25, 25, 255 }, 1, 0.3f));
        }
    }
    return P.ToImage();
}

// Rider seen from above: knees, torso, arms to the grips, helmet with visor.
static void Rider(Painter& P, float cx, float seatY, float gripY, float gripX, Color jacket, Color helmet, float lean) {
    Color pants = { 40, 42, 50, 255 };
    Mat o = M(jacket, 5, 0.25f, 16);
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 k = { cx + sx * 8.5f, seatY - 6 };
        P.Mat(BB(k, 5, 9), [=](Vector2 p) { return sdEllipse(p, k, { 4.2f, 8.5f }); }, M(pants, 3, 0.2f));
    }
    Vector2 tc = { cx, seatY - lean };
    P.Mat(BB(tc, 12, 13), [=](Vector2 p) { return sdEllipse(p, tc, { 11.5f, 12 }); }, o);
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 a = { cx + sx * 9.5f, tc.y - 7 }, b = { cx + sx * (gripX - 1.5f), gripY + 1.5f };
        P.Mat(BBC(a, b, 4), [=](Vector2 p) { return sdCapsule(p, a, b, 3.3f); }, o);
        Vector2 g = { cx + sx * gripX, gripY };
        P.Mat(BB(g, 4, 4), [=](Vector2 p) { return sdCircle(p, g, 3.1f); }, M({ 25, 25, 25, 255 }, 2, 0.3f));
    }
    Vector2 hc = { cx, tc.y - 11 };
    Mat hm = M(helmet, 5, 1.0f, 50); hm.bulge = 1.8f;
    P.Mat(BB(hc, 8, 8), [=](Vector2 p) { return sdCircle(p, hc, 7.3f); }, hm);
    P.Glass(BB({ cx, hc.y - 5 }, 6, 3), [=](Vector2 p) { return sdEllipse(p, { cx, hc.y - 4.5f }, { 5.2f, 2.4f }); }, GLASS_TOP, GLASS_BOT, 0.3f);
}

// Chopper: long raked fork, fat rear tyre, teardrop tank, ape-hanger bars (2.4 m)
static Image Chopper(Color paint, bool rider, Color jacket, Color helmet) {
    const float cx = 26;
    Painter P(52, 126);
    Mat chrome = M({ 190, 192, 198, 255 }, 2, 1.0f, 60);
    P.Mat(BBC({ cx, 92 }, { cx, 120 }, 9), [=](Vector2 p) { return sdCapsule(p, { cx, 94 }, { cx, 118 }, 8.0f); }, M(RUBBER, 3, 0.25f, 12));
    P.Mat(BBC({ cx, 3 }, { cx, 22 }, 5), [=](Vector2 p) { return sdCapsule(p, { cx, 4 }, { cx, 21 }, 4.2f); }, M(RUBBER, 3, 0.25f, 12));
    for (int sx = -1; sx <= 1; sx += 2) {                       // twin fork tubes
        Vector2 a = { cx + sx * 4.5f, 12 }, b = { cx + sx * 5.5f, 46 };
        P.Mat(BBC(a, b, 2), [=](Vector2 p) { return sdCapsule(p, a, b, 1.6f); }, chrome);
    }
    Mat pm = M(paint, 6, 0.9f, 40); pm.bulge = 1.8f;
    P.Mat(BB({ cx, 60 }, 10, 15), [=](Vector2 p) { return sdEllipse(p, { cx, 60 }, { 9.0f, 14.5f }); }, pm);   // teardrop tank
    P.Mat(BB({ cx, 86 }, 11, 12), [=](Vector2 p) { return sdRoundBox(p, { cx, 86 }, { 10, 11 }, 7); }, M({ 40, 28, 22, 255 }, 4, 0.3f, 14));
    P.Mat(BB({ cx, 108 }, 10, 12), [=](Vector2 p) { return sdRoundBox(p, { cx, 108 }, { 9.5f, 11 }, 6); }, pm);    // rear fender
    for (int sx = -1; sx <= 1; sx += 2) {                       // exhaust pipes
        Vector2 a = { cx + sx * 12, 70 }, b = { cx + sx * 13, 116 };
        P.Mat(BBC(a, b, 3), [=](Vector2 p) { return sdCapsule(p, a, b, 2.2f); }, chrome);
    }
    P.Mat(BB({ cx, 24 }, 5, 5), [=](Vector2 p) { return sdCircle(p, { cx, 24 }, 4.5f); }, M({ 240, 240, 225, 255 }, 2, 1.0f));
    // ape-hanger handlebars
    P.Mat(BBC({ 2, 44 }, { 50, 44 }, 3), [=](Vector2 p) {
              return std::min(sdCapsule(p, { 4, 40 }, { cx, 46 }, 1.6f), sdCapsule(p, { cx, 46 }, { 48, 40 }, 1.6f)); }, chrome);
    if (rider) Rider(P, cx, 86, 40, 22, jacket, helmet, 0);
    return P.ToImage();
}

// Scooter: step-through body, small wheels, front shield (1.8 m)
static Image Scooter(Color paint, bool rider, Color jacket, Color helmet) {
    const float cx = 24;
    Painter P(48, 94);
    P.Mat(BBC({ cx, 74 }, { cx, 90 }, 6), [=](Vector2 p) { return sdCapsule(p, { cx, 75 }, { cx, 89 }, 5.0f); }, M(RUBBER, 3, 0.25f, 12));
    P.Mat(BBC({ cx, 3 }, { cx, 17 }, 5), [=](Vector2 p) { return sdCapsule(p, { cx, 4 }, { cx, 16 }, 4.2f); }, M(RUBBER, 3, 0.25f, 12));
    Mat pm = M(paint, 7, 0.9f, 40); pm.bulge = 1.9f;
    P.Mat(BB({ cx, 20 }, 13, 9), [=](Vector2 p) { return sdEllipse(p, { cx, 20 }, { 12.5f, 8.0f }); }, pm);         // leg shield
    P.Mat(BB({ cx, 44 }, 9, 14), [=](Vector2 p) { return sdRoundBox(p, { cx, 44 }, { 8, 13 }, 4); }, M({ 50, 50, 54, 255 }, 3, 0.2f));  // floorboard
    P.Mat(BB({ cx, 68 }, 13, 16), [=](Vector2 p) { return sdEllipse(p, { cx, 68 }, { 12.5f, 15.5f }); }, pm);      // rear body
    P.Mat(BB({ cx, 64 }, 8, 11), [=](Vector2 p) { return sdRoundBox(p, { cx, 64 }, { 7, 10 }, 5); }, M({ 35, 30, 28, 255 }, 3, 0.3f));
    P.Mat(BB({ cx, 14 }, 4, 3), [=](Vector2 p) { return sdEllipse(p, { cx, 14 }, { 3.5f, 2.5f }); }, M({ 240, 240, 225, 255 }, 1.5f, 1.0f));
    P.Mat(BBC({ 8, 25 }, { 40, 25 }, 2), [=](Vector2 p) { return sdCapsule(p, { 9, 25 }, { 39, 25 }, 1.7f); }, M(METAL_DARK, 1.5f, 0.6f));
    if (rider) Rider(P, cx, 64, 25, 15, jacket, helmet, 2);
    return P.ToImage();
}

// Fire engine: red body, ladder, turntable, silver lockers (8.5 m, canvas 104 x 340)
Image FireTruck() {
    const float cx = 52;
    Painter P(104, 340);
    Color red = { 200, 28, 28, 255 };
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 c = { cx + sx * 48, 28 };
        P.Mat(BB(c, 4, 7), [=](Vector2 p) { return sdEllipse(p, c, { 3.5f, 6.5f }); }, M(RUBBER, 2, 0.3f));
    }
    Mat body = M(red, 12, 0.8f, 30); body.bulge = 1.6f;
    P.Mat(BB({ cx, 170 }, 47, 168), [=](Vector2 p) { return sdRoundBox(p, { cx, 170 }, { 46, 167 }, 12); }, body);
    P.Glass(BB({ cx, 20 }, 41, 11), [=](Vector2 p) { return sdTrapezoid(p, { cx, 20 }, 36, 40, 10, 4); }, GLASS_TOP, GLASS_BOT);
    P.Mat(BB({ cx, 52 }, 40, 18), [=](Vector2 p) { return sdRoundBox(p, { cx, 52 }, { 38, 17 }, 6); }, M(ColorMul(red, 1.1f), 6, 0.5f));
    P.Mat(BB({ cx, 38 }, 30, 4), [=](Vector2 p) { return sdRoundBox(p, { cx, 38 }, { 28, 3 }, 2); }, M({ 230, 230, 235, 255 }, 1.5f, 0.6f)); // light bar
    for (int sx = -1; sx <= 1; sx += 2) {                       // lockers
        Vector2 c = { cx + sx * 40, 200 };
        P.Mat(BB(c, 6, 120), [=](Vector2 p) { return sdRoundBox(p, c, { 5, 118 }, 2); }, M({ 190, 192, 196, 255 }, 2, 0.9f, 50));
    }
    // ladder: two rails with rungs, turntable at the rear
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 a = { cx + sx * 16, 70 }, b = { cx + sx * 16, 300 };
        P.Mat(BBC(a, b, 4), [=](Vector2 p) { return sdCapsule(p, a, b, 3.0f); }, M({ 200, 200, 205, 255 }, 2, 0.9f, 50));
    }
    for (float y = 78; y < 296; y += 12) {
        Vector2 a = { cx - 16, y }, b = { cx + 16, y };
        P.Mat(BBC(a, b, 2), [=](Vector2 p) { return sdCapsule(p, a, b, 1.3f); }, M({ 170, 170, 176, 255 }, 1, 0.7f));
    }
    P.Mat(BB({ cx, 300 }, 26, 26), [=](Vector2 p) { return sdCircle(p, { cx, 300 }, 24); }, M({ 70, 72, 76, 255 }, 5, 0.5f));
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 hl = { cx + sx * 32, 7 };
        P.Mat(BB(hl, 9, 4), [=](Vector2 p) { return sdEllipse(p, hl, { 8, 3 }); }, M({ 240, 240, 225, 255 }, 2, 0.9f));
    }
    return P.ToImage();
}

// Refuse truck: cab + ribbed compactor body + rear loader (8 m, canvas 104 x 320)
Image GarbageTruck(Color bodyCol) {
    const float cx = 52;
    Painter P(104, 320);
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 c = { cx + sx * 48, 28 };
        P.Mat(BB(c, 4, 7), [=](Vector2 p) { return sdEllipse(p, c, { 3.5f, 6.5f }); }, M(RUBBER, 2, 0.3f));
    }
    Color cab = { 235, 235, 232, 255 };
    Mat cabM = M(cab, 14, 0.7f, 28); cabM.bulge = 1.5f;
    P.Mat(BB({ cx, 44 }, 45, 40), [=](Vector2 p) { return sdRoundBox(p, { cx, 44 }, { 44, 39 }, 16); }, cabM);
    P.Glass(BB({ cx, 24 }, 41, 11), [=](Vector2 p) { return sdTrapezoid(p, { cx, 24 }, 34, 40, 10, 4); }, GLASS_TOP, GLASS_BOT);
    P.Mat(BB({ cx, 58 }, 36, 18), [=](Vector2 p) { return sdRoundBox(p, { cx, 58 }, { 35, 17 }, 8); }, M(ColorMul(cab, 1.05f), 8, 0.4f));
    P.Mat(BB({ cx, 90 }, 27, 8), [=](Vector2 p) { return sdRoundBox(p, { cx, 90 }, { 26, 7 }, 2); }, M({ 35, 35, 38, 255 }, 2, 0.1f));
    Mat bm = M(bodyCol, 16, 0.35f, 16); bm.bulge = 1.4f;
    P.Mat(BB({ cx, 188 }, 48, 96), [=](Vector2 p) { return sdRoundBox(p, { cx, 188 }, { 47, 95 }, 18); }, bm);
    for (float y = 104; y < 280; y += 18) {
        Vector2 c = { cx, y };
        P.Fill(BB(c, 46, 2), [=](Vector2 p) { return sdRoundBox(p, c, { 44, 1.2f }, 0.6f); },
               [](Vector2, float, Vector2) { return F4{ 0, 0, 0, 0.18f }; });
    }
    P.Mat(BB({ cx, 296 }, 46, 22), [=](Vector2 p) { return sdRoundBox(p, { cx, 296 }, { 45, 21 }, 6); }, M({ 70, 72, 76, 255 }, 5, 0.4f));
    for (int k = 0; k < 6; k++) {                              // hazard chevrons on the loader
        float x = cx - 38 + k * 15.0f;
        Vector2 a = { x, 314 }, b = { x + 8, 304 };
        P.Mat(BBC(a, b, 4), [=](Vector2 p) { return sdCapsule(p, a, b, 3.0f); }, M({ 245, 200, 30, 255 }, 1.5f, 0.3f));
    }
    return P.ToImage();
}

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
//  Pedestrians (atlas of PED_ATLAS_FRAMES frames, each 96x96, facing up)
//  Proportions at the world scale 1 frame == 1 m: shoulders ~0.46 m, head ~0.2 m.
//  Drawn with dark outlines to sit next to the Survivor player sprite.
// -------------------------------------------------------------------------------------
PedLook RandomPedLook(uint32_t seed) {
    Rng r(seed * 2654435761u + 17);
    static const Color skins[] = { { 255, 219, 180, 255 }, { 234, 190, 150, 255 }, { 198, 146, 105, 255 },
                                   { 150, 100, 70, 255 },  { 105, 70, 48, 255 } };
    static const Color hairs[] = { { 26, 20, 18, 255 }, { 62, 38, 24, 255 }, { 105, 66, 36, 255 }, { 196, 150, 80, 255 },
                                   { 40, 30, 26, 255 }, { 140, 48, 26, 255 } };
    static const Color shirts[] = { { 200, 40, 45, 255 },  { 40, 90, 170, 255 }, { 240, 240, 235, 255 }, { 50, 50, 55, 255 },
                                    { 60, 140, 80, 255 },  { 230, 180, 40, 255 }, { 140, 60, 150, 255 }, { 90, 110, 130, 255 },
                                    { 220, 110, 40, 255 }, { 170, 150, 120, 255 }, { 30, 150, 170, 255 }, { 240, 150, 170, 255 } };
    static const Color pants[] = { { 40, 50, 80, 255 }, { 30, 30, 34, 255 }, { 110, 95, 70, 255 }, { 70, 70, 75, 255 }, { 60, 80, 120, 255 } };
    PedLook l;
    l.skin = skins[r.Int(0, 4)];
    l.hair = hairs[r.Int(0, 5)];
    l.shirt = shirts[r.Int(0, 11)];
    l.pants = pants[r.Int(0, 4)];
    l.shoes = r.Chance(0.5f) ? Color{ 30, 30, 30, 255 } : Color{ 220, 220, 220, 255 };
    l.cap = shirts[r.Int(0, 11)];
    l.bag = { (unsigned char)r.Int(30, 120), (unsigned char)r.Int(30, 90), (unsigned char)r.Int(20, 60), 255 };
    l.hairStyle = r.Int(0, 9) < 4 ? 0 : (r.Chance(0.5f) ? 1 : (r.Chance(0.5f) ? 3 : 2));
    l.backpack = r.Chance(0.25f);
    l.build = r.Range(0.86f, 1.08f);
    return l;
}

static void PedFrame(Painter& P, float ox, const PedLook& L, int frame) {
    const float OL = 2.2f;                        // outline width
    const float cx = ox + 48, cy = 48;
    auto m = [&](Color c, float bevel = 5, float spec = 0.15f) { return M(c, bevel, spec, 14, OL); };
    float bw = 21.0f * L.build;

    if (frame == 9) {                              // knocked down: lying on the back
        for (int sx = -1; sx <= 1; sx += 2) {
            Vector2 a = { cx + sx * 6, 58 }, b = { cx + sx * 10, 84 };
            P.Mat(BBC(a, b, 6), [=](Vector2 p) { return sdCapsule(p, a, b, 5.2f); }, m(L.pants));
            Vector2 s = { cx + sx * 10.5f, 88 };
            P.Mat(BB(s, 5, 5), [=](Vector2 p) { return sdEllipse(p, s, { 4.4f, 3.8f }); }, m(L.shoes));
            Vector2 ha = { cx + sx * 13, 32 }, hb = { cx + sx * 28, 50 };
            P.Mat(BBC(ha, hb, 5), [=](Vector2 p) { return sdCapsule(p, ha, hb, 4.6f); }, m(L.shirt));
            P.Mat(BB(hb, 5, 5), [=](Vector2 p) { return sdCircle(p, hb, 4.2f); }, m(L.skin));
        }
        P.Mat(BBC({ cx, 32 }, { cx, 58 }, 14), [=](Vector2 p) { return sdCapsule(p, { cx, 34 }, { cx, 56 }, bw * 0.62f); }, m(L.shirt));
        P.Mat(BB({ cx, 14 }, 10, 10), [=](Vector2 p) { return sdCircle(p, { cx, 15 }, 9.2f); }, m(L.hairStyle == 2 ? L.skin : L.hair));
        P.Mat(BB({ cx, 19 }, 9, 8), [=](Vector2 p) { return sdEllipse(p, { cx, 19.5f }, { 7.5f, 6.5f }); }, m(L.skin, 4, 0.2f));
        return;
    }

    float ph = frame < 8 ? frame / 8.0f * 2.0f * PI : 0.0f;
    float s = frame < 8 ? sinf(ph) : 0.0f;
    int punchArm = frame == PED_FRAME_PUNCH ? 1 : frame == PED_FRAME_PUNCH + 1 ? -1 : 0;   // +1 right, -1 left
    float stride = 13.0f, swing = 8.0f;
    // feet (under the body)
    for (int sx = -1; sx <= 1; sx += 2) {
        float fy = cy + 2 - sx * s * stride;
        Vector2 c = { cx + sx * 7.5f, fy };
        P.Mat(BB(c, 6, 10), [=](Vector2 p) { return sdEllipse(p, c, { 5.0f, 8.5f }); }, m(L.shoes, 4, 0.3f));
        Vector2 leg = { cx + sx * 7.5f, (fy + cy) * 0.5f + 3 };
        if (fabsf(s) > 0.2f)
            P.Mat(BB(leg, 6, 8), [=](Vector2 p) { return sdEllipse(p, leg, { 5.5f, 6.5f }); }, m(L.pants, 4, 0.1f));
    }
    // arms swing opposite to the legs
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 a = { cx + sx * (bw - 3), cy - 1 };
        Vector2 b = { cx + sx * (bw + 1), cy + 4 + sx * s * swing };
        if (punchArm == sx) b = { cx + sx * 7.0f, cy - 30 };            // jab: fist straight ahead
        else if (punchArm != 0) b = { cx + sx * (bw - 4), cy - 9 };      // guard hand up
        P.Mat(BBC(a, b, 6), [=](Vector2 p) { return sdCapsule(p, a, b, 5.0f); }, m(L.shirt));
        P.Mat(BB(b, 5, 5), [=](Vector2 p) { return sdCircle(p, b, 4.3f); }, m(L.skin, 3, 0.2f));
    }
    // torso / shoulders
    P.Mat(BB({ cx, cy }, bw + 1, 13), [=](Vector2 p) { return sdEllipse(p, { cx, cy }, { bw, 11.5f }); }, m(L.shirt, 7, 0.18f));
    if (L.backpack)
        P.Mat(BB({ cx, cy + 8 }, 12, 8), [=](Vector2 p) { return sdRoundBox(p, { cx, cy + 8 }, { 11, 7 }, 4); }, m(L.bag, 4, 0.2f));
    if (L.hairStyle == 1)
        P.Mat(BB({ cx, cy + 5 }, 10, 9), [=](Vector2 p) { return sdEllipse(p, { cx, cy + 5 }, { 9.5f, 8.5f }); }, m(L.hair, 4, 0.3f));
    // head: face peeks out in front of the hair
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector2 e = { cx + sx * 9.3f, cy - 3 };
        P.Mat(BB(e, 3, 3), [=](Vector2 p) { return sdCircle(p, e, 2.3f); }, m(L.skin, 2, 0.1f));
    }
    P.Mat(BB({ cx, cy - 5 }, 10, 10), [=](Vector2 p) { return sdCircle(p, { cx, cy - 5 }, 9.3f); }, m(L.skin, 4, 0.25f));
    if (L.hairStyle == 3) {
        Color brim = Shift(L.cap, 0.8f);
        P.Mat(BB({ cx, cy - 14 }, 7, 5), [=](Vector2 p) { return sdEllipse(p, { cx, cy - 13.5f }, { 6.5f, 4.5f }); }, m(brim, 3, 0.2f));
        P.Mat(BB({ cx, cy - 2 }, 10, 10), [=](Vector2 p) { return sdCircle(p, { cx, cy - 2.5f }, 9.0f); }, m(L.cap, 5, 0.3f));
    } else if (L.hairStyle != 2) {
        Mat hm = M(L.hair, 5, 0.12f, 10, OL); hm.bulge = 1.2f;
        P.Mat(BB({ cx, cy - 2 }, 10, 10), [=](Vector2 p) { return sdCircle(p, { cx, cy - 2.2f }, 9.1f); }, hm);
    }
}

PedLook PlayerLook() {
    PedLook l = RandomPedLook(1);
    l.skin = { 205, 150, 115, 255 };
    l.hair = { 56, 40, 36, 255 };
    l.shirt = { 62, 78, 78, 255 };      // Survivor's teal-grey jacket
    l.pants = { 46, 48, 52, 255 };
    l.shoes = { 28, 28, 28, 255 };
    l.bag = { 78, 74, 50, 255 };        // khaki tactical vest / pack
    l.hairStyle = 0; l.backpack = true; l.build = 1.06f;
    return l;
}

Image PedAtlas(const PedLook& look) {
    Painter P(PED_FRAME * PED_ATLAS_FRAMES, PED_FRAME);
    for (int f = 0; f < PED_ATLAS_FRAMES; f++) PedFrame(P, (float)f * PED_FRAME, look, f);
    return P.ToImage();
}

// -------------------------------------------------------------------------------------
//  Trees: clustered foliage blobs lit from the top-left
// -------------------------------------------------------------------------------------
Image Tree(int variant, int size, uint32_t seed) {
    Rng r(seed + 101 * variant);
    Painter P(size, size);
    float c = size * 0.5f, R = size * 0.43f;
    Color dark, mid, light;
    switch (variant) {
        default:
        case 0: dark = { 24, 52, 24, 255 };  mid = { 52, 100, 40, 255 };  light = { 110, 160, 70, 255 }; break;  // oak
        case 1: dark = { 40, 70, 22, 255 };  mid = { 90, 135, 45, 255 };  light = { 165, 200, 90, 255 }; break;  // lime
        case 2: dark = { 14, 40, 32, 255 };  mid = { 30, 75, 55, 255 };   light = { 70, 120, 90, 255 };  break;  // pine
        case 3: dark = { 90, 40, 18, 255 };  mid = { 185, 95, 30, 255 };  light = { 240, 170, 70, 255 }; break;  // autumn
        case 4: dark = { 120, 60, 90, 255 }; mid = { 220, 135, 165, 255 };light = { 255, 215, 228, 255 }; break; // blossom
    }
    float k = size / 256.0f;
    float nOff = (float)(seed % 97) * 13.7f;
    // Leaf texture: noise both perturbs the blob edge (ragged leafy outline) and
    // modulates the colour. Shading is a dome per cluster, lit from the top-left.
    auto leafy = [&](Vector2 bp, float br, Color col, float edgeNoise) {
        auto sdf = [=](Vector2 p) {
            float n = VNoise({ (p.x + nOff) / (9.0f * k), (p.y + nOff) / (9.0f * k) });
            return sdCircle(p, bp, br) - (n - 0.5f) * edgeNoise * k;
        };
        Mat m = M(col, br * 0.95f, 0.08f, 8); m.rim = 0.28f; m.bulge = 1.5f;
        P.Fill(BB(bp, br + 6 * k, br + 6 * k), sdf, [=](Vector2 p, float d, Vector2 n) {
            F4 f = Shade(m, d, n);
            float t = 0.86f + 0.24f * VNoise({ (p.x + 31) / (4.5f * k), (p.y + 17) / (4.5f * k) });
            f.r *= t; f.g *= t; f.b *= t;
            return f;
        });
    };
    // dark core that fills gaps between clusters
    leafy({ c + 3 * k, c + 3 * k }, R * 0.9f, dark, 18);
    struct Blob { Vector2 p; float r; float tone; };
    std::vector<Blob> blobs;
    int n = variant == 2 ? 90 : 60;
    for (int i = 0; i < n; i++) {
        float a = r.Range(0, 2 * PI), d = sqrtf(r.Float()) * R * 0.8f;
        float br = (variant == 2 ? r.Range(0.08f, 0.13f) : r.Range(0.10f, 0.17f)) * size;
        blobs.push_back({ { c + cosf(a) * d, c + sinf(a) * d }, br, r.Float() });
    }
    // clusters furthest from the light are drawn first (lit clusters sit on top)
    std::sort(blobs.begin(), blobs.end(), [](const Blob& a, const Blob& b) { return a.p.x + a.p.y > b.p.x + b.p.y; });
    for (auto& b : blobs) {
        float lightSide = Saturate(0.5f - ((b.p.x - c) + (b.p.y - c)) / (R * 2.4f));
        float centre = 1.0f - Saturate(Dist(b.p, { c, c }) / R);
        Color col = LerpColor(LerpColor(dark, mid, 0.45f + 0.4f * b.tone + 0.2f * centre), light, lightSide * 0.6f);
        leafy(b.p, b.r, col, 14);
    }
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
