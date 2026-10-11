// =====================================================================================
//  Asset loading + procedural texture generation.
// =====================================================================================
#include "assets.h"
#include "impact.h"
#include "math_utils.h"
#include <cstdio>
#include <cstring>
#include "datafile.h"
#include "config.h"
#include "startup_loading.h"
#include <exception>
#include <utility>

Assets gAssets;
using namespace spritegen;

// Startup stays on the main thread. This scope lets CPU image helpers report safe
// checkpoints without coupling procedural art or the asset format to presentation.
static startup::Reporter* assetReporter = nullptr;
struct AssetInterrupted {};

struct AssetReportingScope {
    startup::Reporter* previous;
    explicit AssetReportingScope(startup::Reporter* reporter) : previous(assetReporter) { assetReporter = reporter; }
    ~AssetReportingScope() { assetReporter = previous; }
};

static void AssetRequire(bool continued) { if (!continued) throw AssetInterrupted{}; }
static void AssetPulse(const char* detail = nullptr) {
    if (assetReporter) AssetRequire(assetReporter->Pulse(detail));
}
static void FreeAssetImage(Image& image) { if (image.data) UnloadImage(image); image = {}; }
struct AssetImageGuard {
    Image* image;
    explicit AssetImageGuard(Image& value) : image(&value) {}
    ~AssetImageGuard() { if (image) FreeAssetImage(*image); }
    void Release() { image = nullptr; }
};

static void AssetBegin(const char* id, const char* detail) {
    if (assetReporter) AssetRequire(assetReporter->Begin(id, detail));
}
static void AssetPlan(const std::vector<startup::ChildSpec>& children) {
    if (assetReporter) AssetRequire(assetReporter->PlanChildren(children));
}
static void AssetDone(const char* id, startup::Outcome outcome = startup::Outcome::Ready) {
    if (assetReporter) AssetRequire(assetReporter->ChildDone(id, outcome));
}
static void AssetFraction(const char* id, double fraction) {
    if (assetReporter) AssetRequire(assetReporter->ChildProgress(id, fraction));
}
static void AssetCount(int64_t done, int64_t total, const char* unit) {
    if (assetReporter) AssetRequire(assetReporter->Count(done, total, total ? unit : ""));
}
static void AssetFinish() {
    if (assetReporter) AssetRequire(assetReporter->Finish());
}
// Every content file is required (ADR-0010): a missing or broken one stops loading with
// a message that names it, shown on the loading screen and in the log.
static void AssetValidate(bool valid, const std::string& cause) {
    if (valid) return;
    TraceLog(LOG_ERROR, "ASSETS: %s", cause.c_str());
    if (assetReporter) assetReporter->Fail(cause.c_str());
    throw AssetInterrupted{};
}
static std::vector<DataRecord> AssetData(const char* path) {
    AssetValidate(FileExists(path), std::string("Missing data file ") + path);
    startup::AtomicSpan span(assetReporter, "data", path);
    return ReadDataFile(path);
}
template<class Make>
static Image AssetProcedural(const char* name, Make make) {
    startup::AtomicSpan span(assetReporter, "procedural", name);
    return make();
}

// A skipped optional record still resolves its configured obligation. Destruction
// runs after its temporary images are released, and never reports while unwinding.
struct AssetRecordStep {
    const char* child;
    int64_t& completed;
    int64_t total;
    AssetRecordStep(const char* id, int64_t& done, int64_t count) : child(id), completed(done), total(count) {}
    ~AssetRecordStep() noexcept(false) {
        if (std::uncaught_exceptions()) return;
        ++completed;
        AssetCount(completed, total, "records processed");
        AssetFraction(child, (double)completed / std::max<int64_t>(1, total));
    }
};
struct AssetChildStep {
    std::string child;
    int64_t& completed;
    int64_t total;
    startup::Outcome outcome = startup::Outcome::Ready;
    AssetChildStep(std::string id, int64_t& done, int64_t count) : child(std::move(id)), completed(done), total(count) {}
    ~AssetChildStep() noexcept(false) {
        if (std::uncaught_exceptions()) return;
        ++completed;
        AssetCount(completed, total, "records processed");
        AssetDone(child.c_str(), outcome);
    }
};

// -------------------------------------------------------------------------------------
//  Helpers
// -------------------------------------------------------------------------------------
static Texture2D MakeTexture(Image img, bool repeat, bool mipmaps) {
    Texture2D t;
    { startup::AtomicSpan span(assetReporter, "upload", "Texture upload"); t = LoadTextureFromImage(img); }
    UnloadImage(img);
    if (mipmaps) {
        { startup::AtomicSpan span(assetReporter, "mipmap", "Texture mipmaps"); GenTextureMipmaps(&t); }
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_8X);   // crisp at grazing angles / zoom
    } else {
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    }
    SetTextureWrap(t, repeat ? TEXTURE_WRAP_REPEAT : TEXTURE_WRAP_CLAMP);
    if (!IsTextureValid(t)) {
        if (t.id) UnloadTexture(t);
        AssetValidate(false, "A required texture could not be prepared");
    }
    return t;
}

// The image a data file names (RGBA): a missing or unreadable file stops loading.
static Image LoadRequiredImage(const char* path) {
    AssetValidate(FileExists(path), std::string("Missing image ") + path);
    Image img;
    { startup::AtomicSpan span(assetReporter, "decode", path); img = LoadImage(path); }
    AssetValidate(img.data != nullptr, std::string("Unreadable image ") + path);
    { startup::AtomicSpan span(assetReporter, "image", "Image format conversion"); ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8); }
    return img;
}

// Shift hue of the saturated ("paint") pixels of a sprite -> colour variants.
static Image RecolorPaint(const Image& src, float hueShift, float satMul, float valMul) {
    Image img = ImageCopy(src);
    AssetImageGuard guard(img);
    Color* px = (Color*)img.data;
    for (int i = 0; i < img.width * img.height; i++) {
        if (i % 4096 == 0) AssetPulse();
        if (px[i].a < 8) continue;
        Vector3 hsv = ColorToHSV(px[i]);
        if (hsv.y < 0.35f || hsv.z < 0.2f) continue;          // leave glass, chrome, tyres alone
        hsv.x = fmodf(hsv.x + hueShift + 360.0f, 360.0f);
        hsv.y = Saturate(hsv.y * satMul);
        hsv.z = Saturate(hsv.z * valMul);
        Color c = ColorFromHSV(hsv.x, hsv.y, hsv.z); c.a = px[i].a; px[i] = c;
    }
    guard.Release();
    return img;
}

// Slightly calm down very saturated source art so it sits with photo textures.
static void Desaturate(Image& img, float amount, float value) {
    Color* px = (Color*)img.data;
    for (int i = 0; i < img.width * img.height; i++) {
        if (i % 4096 == 0) AssetPulse();
        if (px[i].a == 0) continue;
        Vector3 hsv = ColorToHSV(px[i]);
        Color c = ColorFromHSV(hsv.x, hsv.y * (1.0f - amount), hsv.z * value); c.a = px[i].a; px[i] = c;
    }
}

static Color AveragePaint(const Image& img) {
    const Color* px = (const Color*)img.data;
    long r = 0, g = 0, b = 0, n = 0;
    for (int i = 0; i < img.width * img.height; i += 3) {
        if (i % 12288 == 0) AssetPulse();
        if (px[i].a < 200) continue;
        r += px[i].r; g += px[i].g; b += px[i].b; n++;
    }
    if (!n) return GRAY;
    return { (unsigned char)(r / n), (unsigned char)(g / n), (unsigned char)(b / n), 255 };
}

static int AddVehicle(Assets& a, Image img, VClass cls, bool spawnable = true) {
    AssetImageGuard guard(img);
    VehicleSprite s;
    { startup::AtomicSpan span(assetReporter, "image", "Vehicle alpha trim"); s.src = GetImageAlphaBorder(img, 0.1f); }
    if (s.src.width <= 0 || s.src.height <= 0) s.src = { 0, 0, (float)img.width, (float)img.height };
    s.paint = AveragePaint(img);
    s.cls = cls;
    s.spawnable = spawnable;
    guard.Release();
    s.tex = MakeTexture(img, false, true);   // takes ownership of img
    int idx = (int)a.vehicles.size();
    if ((int)a.byClass.size() <= cls) a.byClass.resize(cls + 1);
    if (spawnable) a.byClass[cls].push_back(idx);
    a.vehicles.push_back(s);
    return idx;
}

// -------------------------------------------------------------------------------------
//  Building facades: albedo (bricks / concrete + windows) and matching emissive mask.
//  One 256x256 tile == 1 storey tall x 'cols' windows wide.
// -------------------------------------------------------------------------------------
struct FacadeStyle { int cols, rows; float winW, winH; Color glassTop, glassBot, frame; };

static Image MakeFacade(const char* basePath, const FacadeStyle& st) {
    Image img = LoadRequiredImage(basePath);
    AssetImageGuard guard(img);
    { startup::AtomicSpan span(assetReporter, "resize", "Facade resize"); ImageResize(&img, 256, 256); }
    float cw = 256.0f / st.cols, ch = 256.0f / st.rows;
    for (int r = 0; r < st.rows; r++)
        for (int c = 0; c < st.cols; c++) {
            AssetPulse();
            int w = (int)(cw * st.winW), h = (int)(ch * st.winH);
            int x = (int)(c * cw + (cw - w) * 0.5f), y = (int)(r * ch + (ch - h) * 0.45f);
            ImageDrawRectangle(&img, x - 4, y - 4, w + 8, h + 8, st.frame);
            ImageDrawRectangle(&img, x - 6, y + h + 2, w + 12, 6, ColorMul(st.frame, 0.8f));     // sill
            for (int yy = 0; yy < h; yy++) {
                float t = yy / (float)h;
                ImageDrawRectangle(&img, x, y + yy, w, 1, LerpColor(st.glassTop, st.glassBot, t));
            }
            ImageDrawLineEx(&img, { (float)x + 3, (float)y + h * 0.75f }, { (float)x + w * 0.55f, (float)y + 3 }, 3, ColorA(WHITE, 0.16f));
            ImageDrawRectangle(&img, x + w / 2 - 1, y, 3, h, ColorMul(st.frame, 0.9f));       // mullion
        }
    for (int r = 0; r < st.rows; r++) ImageDrawRectangle(&img, 0, (int)((r + 1) * ch) - 5, 256, 5, ColorA(BLACK, 0.2f));
    guard.Release();
    return img;
}

static Image MakeFacadeLit(const FacadeStyle& st, int repeats, int seed) {
    Image img = GenImageColor(256 * repeats, 256, BLANK);
    AssetImageGuard guard(img);
    Rng rng(seed);
    float cw = 256.0f / st.cols, ch = 256.0f / st.rows;
    for (int rep = 0; rep < repeats; rep++)
        for (int r = 0; r < st.rows; r++)
            for (int c = 0; c < st.cols; c++) {
                AssetPulse();
                if (!rng.Chance(0.4f)) continue;
                int w = (int)(cw * st.winW), h = (int)(ch * st.winH);
                int x = rep * 256 + (int)(c * cw + (cw - w) * 0.5f), y = (int)(r * ch + (ch - h) * 0.45f);
                float k = rng.Range(0.6f, 1.0f);
                Color warm = rng.Chance(0.72f) ? Color{ 255, 196, 120, 255 } : Color{ 160, 205, 255, 255 };
                warm = ColorMul(warm, k);
                for (int yy = 0; yy < h; yy++) {
                    float t = yy / (float)h;
                    ImageDrawRectangle(&img, x, y + yy, w, 1, ColorA(warm, 0.55f + 0.45f * t));
                }
            }
    guard.Release();
    return img;
}

// -------------------------------------------------------------------------------------
//  Effect textures
// -------------------------------------------------------------------------------------
static Texture2D MakeRadial(int size, float power) {
    Image img = GenImageColor(size, size, BLANK);
    AssetImageGuard guard(img);
    Color* px = (Color*)img.data;
    float h = size * 0.5f;
    for (int y = 0; y < size; y++) {
        if (y % 16 == 0) AssetPulse();
        for (int x = 0; x < size; x++) {
            float d = sqrtf((x + 0.5f - h) * (x + 0.5f - h) + (y + 0.5f - h) * (y + 0.5f - h)) / h;
            float a = powf(Saturate(1.0f - d), power);
            px[y * size + x] = { 255, 255, 255, (unsigned char)(a * 255) };
        }
    }
    guard.Release();
    return MakeTexture(img, false, true);
}

static Texture2D MakeCone() {
    const int S = 256;
    Image img = GenImageColor(S, S, BLANK);
    AssetImageGuard guard(img);
    Color* px = (Color*)img.data;
    const float half = 22.0f * DEG2RAD;
    for (int y = 0; y < S; y++) {
        if (y % 16 == 0) AssetPulse();
        for (int x = 0; x < S; x++) {
            float f = (float)(S - y), l = fabsf(x + 0.5f - S * 0.5f);
            if (f <= 0) continue;
            float ang = atan2f(l, f);
            float angular = SmoothStep(half, half * 0.4f, ang);
            float dist = powf(Saturate(1.0f - f / S), 1.2f) * SmoothStep(0.0f, 14.0f, f);
            px[y * S + x] = { 255, 255, 255, (unsigned char)(Saturate(angular * dist) * 255) };
        }
    }
    guard.Release();
    return MakeTexture(img, false, true);
}

static Texture2D MakeSpark() {
    Image img = GenImageColor(32, 8, BLANK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 32; x++) {
            float dx = fabsf(x - 15.5f) / 16.0f, dy = fabsf(y - 3.5f) / 4.0f;
            px[y * 32 + x] = { 255, 255, 255, (unsigned char)(Saturate(1 - dx) * Saturate(1 - dy * dy) * 255) };
        }
    return MakeTexture(img, false, false);
}

static Texture2D MakeRing() {
    const int S = 128;
    Image img = GenImageColor(S, S, BLANK);
    AssetImageGuard guard(img);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; y++) {
        if (y % 16 == 0) AssetPulse();
        for (int x = 0; x < S; x++) {
            float d = sqrtf((x - 63.5f) * (x - 63.5f) + (y - 63.5f) * (y - 63.5f)) / 64.0f;
            float a = expf(-((d - 0.86f) / 0.07f) * ((d - 0.86f) / 0.07f));
            px[y * S + x] = { 255, 255, 255, (unsigned char)(Saturate(a) * 255) };
        }
    }
    guard.Release();
    return MakeTexture(img, false, true);
}

static Texture2D MakeFlare() {
    const int S = 128;
    Image img = GenImageColor(S, S, BLANK);
    AssetImageGuard guard(img);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; y++) {
        if (y % 16 == 0) AssetPulse();
        for (int x = 0; x < S; x++) {
            float dx = (x - 63.5f) / 64.0f, dy = (y - 63.5f) / 64.0f;
            float d = sqrtf(dx * dx + dy * dy);
            float core = powf(Saturate(1 - d), 6.0f);
            float glow = powf(Saturate(1 - d), 2.0f) * 0.35f;
            float star = (powf(Saturate(1 - fabsf(dx) * 18), 2) * Saturate(1 - fabsf(dy))
                        + powf(Saturate(1 - fabsf(dy) * 18), 2) * Saturate(1 - fabsf(dx))) * 0.5f;
            px[y * S + x] = { 255, 255, 255, (unsigned char)(Saturate(core + glow + star) * 255) };
        }
    }
    guard.Release();
    return MakeTexture(img, false, true);
}

static Texture2D MakeBlood() {
    Image img = GenImageColor(64, 64, BLANK);
    Rng r(99);
    for (int i = 0; i < 26; i++) {
        float a = r.Range(0, 2 * PI), d = r.Range(0, 22) * (i < 6 ? 0.2f : 1.0f);
        int rad = i < 6 ? r.Int(8, 12) : r.Int(1, 5);
        ImageDrawCircle(&img, 32 + (int)(cosf(a) * d), 32 + (int)(sinf(a) * d), rad, { 110, 8, 12, 230 });
    }
    return MakeTexture(img, false, true);
}

static Texture2D MakeWater() {
    const int S = 256;
    Image img = GenImageColor(S, S, BLANK);
    AssetImageGuard guard(img);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; y++) {
        if (y % 16 == 0) AssetPulse();
        for (int x = 0; x < S; x++) {
            float u = x / (float)S * 2 * PI, v = y / (float)S * 2 * PI;   // tileable waves
            float w = sinf(u * 3 + sinf(v * 2) * 1.5f) * 0.5f + sinf(v * 5 + u * 2) * 0.3f + sinf((u + v) * 7) * 0.2f;
            float t = 0.5f + 0.5f * w;
            px[y * S + x] = LerpColor({ 18, 48, 62, 255 }, { 46, 96, 112, 255 }, t * t);
        }
    }
    guard.Release();
    return MakeTexture(img, true, true);
}

// -------------------------------------------------------------------------------------
//  Shaders (GLSL 330, desktop OpenGL 3.3)
// -------------------------------------------------------------------------------------
static const char* COMPOSITE_FS = R"(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;    // scene albedo
uniform sampler2D lightMap;    // accumulated light, stored at x0.5
uniform sampler2D emissive;    // self-lit surfaces (windows, lamps, fire ...)
uniform sampler2D bloomTex;    // blurred emissive
uniform float bloomStrength;
uniform float time;
out vec4 finalColor;
void main() {
    vec2 uv = fragTexCoord;
    vec3 albedo = texture(texture0, uv).rgb;
    vec3 light  = texture(lightMap, uv).rgb * 2.0;
    vec3 emis   = texture(emissive, uv).rgb;
    vec3 bloom  = texture(bloomTex, uv).rgb;
    vec3 c = albedo * light + emis + bloom * bloomStrength;
    // filmic-ish tone curve: keeps mid-tones, rolls off highlights
    c = c / (1.0 + max(c - 0.9, 0.0) * 0.8);
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.22);
    // subtle saturation boost and vignette
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, 1.08);
    vec2 d = uv - 0.5;
    float vig = smoothstep(0.85, 0.25, length(d * vec2(1.25, 1.0)));
    c *= mix(0.55, 1.0, vig);
    finalColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

static const char* BLUR_FS = R"(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 dir;              // texel step * direction
out vec4 finalColor;
void main() {
    float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 c = texture(texture0, fragTexCoord).rgb * w[0];
    for (int i = 1; i < 5; i++) {
        c += texture(texture0, fragTexCoord + dir * float(i) * 1.5).rgb * w[i];
        c += texture(texture0, fragTexCoord - dir * float(i) * 1.5).rgb * w[i];
    }
    finalColor = vec4(c, 1.0);
}
)";

// -------------------------------------------------------------------------------------
//  Paint colours of derived vehicles: a named colour or r,g,b
// -------------------------------------------------------------------------------------
static Color ParseColor(const std::string& s) {
    struct Named { const char* n; Color c; };
    static const Named table[] = {
        { "red", { 200, 30, 35, 255 } },    { "blue", { 30, 80, 180, 255 } },    { "white", { 236, 236, 232, 255 } },
        { "black", { 32, 33, 36, 255 } },   { "silver", { 172, 174, 178, 255 } },{ "green", { 40, 120, 80, 255 } },
        { "darkgreen", { 30, 70, 45, 255 } },{ "beige", { 200, 182, 145, 255 } },{ "teal", { 30, 130, 150, 255 } },
        { "maroon", { 115, 25, 35, 255 } }, { "yellow", { 235, 190, 40, 255 } }, { "orange", { 235, 125, 30, 255 } },
        { "cream", { 245, 238, 222, 255 } },{ "grey", { 120, 122, 126, 255 } },  { "darkgrey", { 62, 62, 66, 255 } },
        { "navy", { 28, 42, 90, 255 } },    { "brown", { 95, 62, 40, 255 } },    { "purple", { 110, 50, 140, 255 } },
        { "pink", { 225, 110, 150, 255 } },
    };
    for (const Named& n : table) if (s == n.n) return n.c;
    int r = 128, g = 128, b = 128;
    bool parsed = sscanf(s.c_str(), "%d,%d,%d", &r, &g, &b) == 3;
    AssetValidate(parsed, "vehicles.cfg: unknown colour '" + s + "'");
    return { (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
}

// -------------------------------------------------------------------------------------
//  Deriving new vehicles from existing sprites (keeps the original art style)
// -------------------------------------------------------------------------------------
static Image CropToContent(const Image& img) {
    Rectangle bb = GetImageAlphaBorder(img, 0.05f);
    if (bb.width <= 0 || bb.height <= 0) return ImageCopy(img);
    return ImageFromImage(img, bb);
}
// Stretch the horizontal band [f0, f1] (fractions of the height) by 'factor'.
static Image StretchBand(const Image& src, float f0, float f1, float factor) {
    Image c = CropToContent(src);
    int y0 = (int)(c.height * f0), y1 = std::max(y0 + 1, (int)(c.height * f1));
    int bandH = std::max(1, (int)((y1 - y0) * factor));
    Image top = ImageFromImage(c, { 0, 0, (float)c.width, (float)std::max(1, y0) });
    Image band = ImageFromImage(c, { 0, (float)y0, (float)c.width, (float)(y1 - y0) });
    Image bot = ImageFromImage(c, { 0, (float)y1, (float)c.width, (float)std::max(1, c.height - y1) });
    { startup::AtomicSpan span(assetReporter, "resize", "Derived vehicle band resize"); ImageResize(&band, c.width, bandH); }
    Image out = GenImageColor(c.width, top.height + bandH + bot.height, BLANK);
    ImageDraw(&out, top, { 0, 0, (float)top.width, (float)top.height }, { 0, 0, (float)top.width, (float)top.height }, WHITE);
    ImageDraw(&out, band, { 0, 0, (float)band.width, (float)band.height }, { 0, (float)top.height, (float)band.width, (float)band.height }, WHITE);
    ImageDraw(&out, bot, { 0, 0, (float)bot.width, (float)bot.height }, { 0, (float)(top.height + bandH), (float)bot.width, (float)bot.height }, WHITE);
    UnloadImage(c); UnloadImage(top); UnloadImage(band); UnloadImage(bot);
    return out;
}
// Front section of A + rear section of B (scaled to A's width, stretched) stacked.
static Image ComposeVehicle(const Image& a, float a0, float a1, const Image& b, float b0, float b1, float stretch) {
    Image ca = CropToContent(a), cb = CropToContent(b);
    Image front = ImageFromImage(ca, { 0, ca.height * a0, (float)ca.width, ca.height * (a1 - a0) });
    Image rear = ImageFromImage(cb, { 0, cb.height * b0, (float)cb.width, cb.height * (b1 - b0) });
    int rw = (int)(ca.width * 0.98f);
    { startup::AtomicSpan span(assetReporter, "resize", "Composed vehicle resize");
      ImageResize(&rear, rw, std::max(1, (int)(rear.height * ((float)rw / rear.width) * stretch))); }
    int overlap = 4;
    Image out = GenImageColor(ca.width, front.height + rear.height - overlap, BLANK);
    ImageDraw(&out, rear, { 0, 0, (float)rear.width, (float)rear.height },
              { (ca.width - rw) * 0.5f, (float)(front.height - overlap), (float)rear.width, (float)rear.height }, WHITE);
    ImageDraw(&out, front, { 0, 0, (float)front.width, (float)front.height }, { 0, 0, (float)front.width, (float)front.height }, WHITE);
    UnloadImage(ca); UnloadImage(cb); UnloadImage(front); UnloadImage(rear);
    return out;
}
// Repaint: saturated "paint" pixels (and optionally bright neutral body panels) take the
// target colour while keeping the original shading.
static Image Repaint(const Image& src, Color target, bool neutralToo) {
    Image img = ImageCopy(src);
    AssetImageGuard guard(img);
    Color* px = (Color*)img.data;
    Vector3 t = ColorToHSV(target);
    for (int i = 0; i < img.width * img.height; i++) {
        if (i % 4096 == 0) AssetPulse();
        if (px[i].a < 8) continue;
        Vector3 hsv = ColorToHSV(px[i]);
        bool paint = hsv.y > 0.35f && hsv.z > 0.2f;
        bool panel = neutralToo && hsv.y < 0.2f && hsv.z > 0.62f;
        if (!paint && !panel) continue;
        float v = Saturate(hsv.z * (t.z + 0.08f) / (panel ? 0.92f : 0.85f));
        Color c = ColorFromHSV(t.x, t.y, v); c.a = px[i].a; px[i] = c;
    }
    guard.Release();
    return img;
}

// -------------------------------------------------------------------------------------
//  Vehicles (vehicles.cfg: SPRITE / DERIVE / COMPOSE / BIKE records)
// -------------------------------------------------------------------------------------
static VClass RequiredClass(const DataRecord& r) {
    VClass cls = FindVehicleClass(r[1]);
    AssetValidate(cls >= 0, TextFormat("vehicles.cfg line %d: unknown class '%s'", r.line, r[1].c_str()));
    return cls;
}

static void LoadVehicles(Assets& A) {
    const auto records = AssetData("assets/data/vehicles.cfg");
    { startup::AtomicSpan span(assetReporter, "data", "Vehicle classes"); LoadVehicleClasses(records); }
    std::string impactError;                       // how vehicles meet people (CJ-035)
    AssetValidate(LoadImpacts(AssetData("assets/data/impacts.cfg"), impactError), impactError);
    A.byClass.assign(VehicleClasses().size(), {});
    char path[256];
    const float hues[3] = { 130, 215, 0 };
    AssetPlan({ { "classes", 1 }, { "records", (double)std::max<size_t>(1, records.size()), false },
                { "train", 1 }, { "validation", 1 } });
    AssetValidate(!A.byClass.empty(), "vehicles.cfg defines no vehicle class");
    for (const VehicleSpec& s : VehicleClasses())
        AssetValidate(s.front > 0, "vehicles.cfg: class '" + s.name + "' has no FRONT record");
    AssetDone("classes");
    int64_t completed = 0;
    AssetCount(0, (int64_t)records.size(), "records processed");
    for (const DataRecord& r : records) {
        AssetRecordStep step("records", completed, (int64_t)records.size());
        AssetPulse(r[1].c_str());
        if (r.Is("SPRITE") && r.size() >= 3) {
            VClass cls = RequiredClass(r);
            snprintf(path, sizeof(path), "assets/vehicles/%s", r[2].c_str());
            Image img = LoadRequiredImage(path);
            AssetImageGuard guard(img);
            int variants = std::min(3, r.I(3, 0));
            for (int v = 0; v < variants; v++) {
                if (v == 2) AddVehicle(A, RecolorPaint(img, 0, 0.08f, 1.15f), cls);   // silver
                else        AddVehicle(A, RecolorPaint(img, hues[v], 1.0f, 0.95f), cls);
            }
            guard.Release();
            AddVehicle(A, img, cls);
        } else if (r.Is("BIKE") && r.size() >= 4) {
            VClass cls = RequiredClass(r);
            char pathR[256];
            snprintf(path, sizeof(path), "assets/vehicles/%s", r[2].c_str());
            snprintf(pathR, sizeof(pathR), "assets/vehicles/%s", r[3].c_str());
            Image empty = LoadRequiredImage(path);
            AssetImageGuard emptyGuard(empty);
            Image ridden = LoadRequiredImage(pathR);
            AssetImageGuard riddenGuard(ridden);
            emptyGuard.Release();
            int e = AddVehicle(A, empty, cls, false);
            riddenGuard.Release();
            int rd = AddVehicle(A, ridden, cls);
            A.vehicles[rd].emptySkin = e;
            A.vehicles[e].emptySkin = e;
            // Both are drawn at the class length: a different aspect changes the width
            // when the rider gets on or off.
            Rectangle a = A.vehicles[e].src, b = A.vehicles[rd].src;
            if (fabsf(a.width / a.height - b.width / b.height) > 0.005f * a.width / a.height)
                TraceLog(LOG_WARNING, "vehicles.cfg:%d '%s' and '%s' differ in aspect (%.0fx%.0f, %.0fx%.0f px)",
                         r.line, r[2].c_str(), r[3].c_str(), a.width, a.height, b.width, b.height);
        } else if ((r.Is("DERIVE") && r.size() >= 7) || (r.Is("COMPOSE") && r.size() >= 9)) {
            VClass cls = RequiredClass(r);
            Image base{};
            AssetImageGuard baseGuard(base);
            size_t firstColour;
            if (r.Is("DERIVE")) {
                snprintf(path, sizeof(path), "assets/vehicles/%s", r[3].c_str());
                Image a = LoadRequiredImage(path);
                AssetImageGuard aGuard(a);
                base = StretchBand(a, r.F(4), r.F(5), r.F(6));
                firstColour = 7;
            } else {
                char pathB[256];
                snprintf(path, sizeof(path), "assets/vehicles/%s", r[2].c_str());
                snprintf(pathB, sizeof(pathB), "assets/vehicles/%s", r[5].c_str());
                Image a = LoadRequiredImage(path);
                AssetImageGuard aGuard(a);
                Image b = LoadRequiredImage(pathB);
                AssetImageGuard bGuard(b);
                base = ComposeVehicle(a, r.F(3), r.F(4), b, r.F(6), r.F(7), r.F(8, 1.0f));
                firstColour = 9;
            }
            if (r.size() <= firstColour) AddVehicle(A, ImageCopy(base), cls);
            for (size_t k = firstColour; k < r.size(); k++) AddVehicle(A, Repaint(base, ParseColor(r[k]), true), cls);
        }
    }
    AssetDone("records", records.empty() ? startup::Outcome::Skipped : startup::Outcome::Ready);
    for (VClass c = 0; c < (VClass)A.byClass.size(); c++) {
        if (A.byClass[c].empty()) continue;
        const VehicleSprite& s = A.vehicles[A.byClass[c][0]];
        const VehicleSpec& sp = Spec(c);
        float drawn = sp.length * (s.src.width / s.src.height);
        TraceLog(LOG_INFO, "VEHICLE %-9s length %.2f m, collision width %.3f m, drawn width %.3f m, sprite %dx%d px",
                 sp.name.c_str(), sp.length / cfg::M, (sp.width > 0 ? sp.width : drawn) / cfg::M, drawn / cfg::M,
                 (int)s.src.width, (int)s.src.height);
    }
}

// -------------------------------------------------------------------------------------
//  Civilians (civilians.cfg): one atlas strip per look, all in the same frame format
// -------------------------------------------------------------------------------------
// Cut every frame of a uniform atlas strip to its visible part and pack the parts side by side
// (4 px apart, so that mipmaps do not bleed); returns the texture's size in bytes.
static size_t PackCivilian(CivilianAtlas& a, Image strip, int framePx) {
    AssetImageGuard stripGuard(strip);
    const int PAD = 4, frames = strip.width / framePx;
    std::vector<Rectangle> part(frames);
    int width = PAD, height = 1;
    for (int i = 0; i < frames; i++) {
        Image f;
        Rectangle b;
        { startup::AtomicSpan span(assetReporter, "image", "Civilian frame trim");
          f = ImageFromImage(strip, { (float)(i * framePx), 0, (float)framePx, (float)framePx });
          b = GetImageAlphaBorder(f, 0.01f); }
        UnloadImage(f);
        if (b.width <= 0 || b.height <= 0) b = { framePx * 0.5f, framePx * 0.5f, 1, 1 };
        part[i] = b;
        width += (int)b.width + PAD;
        height = std::max(height, (int)b.height);
        AssetPulse();
    }
    Image packed = GenImageColor(width, height + 2 * PAD, BLANK);
    AssetImageGuard packedGuard(packed);
    a.src.resize(frames);
    a.offset.resize(frames);
    float x = PAD;
    for (int i = 0; i < frames; i++) {
        const Rectangle& b = part[i];
        Rectangle from = { i * (float)framePx + b.x, b.y, b.width, b.height };
        a.src[i] = { x, (float)PAD, b.width, b.height };
        a.offset[i] = { b.x + b.width * 0.5f - framePx * 0.5f, b.y + b.height * 0.5f - framePx * 0.5f };
        { startup::AtomicSpan span(assetReporter, "image", "Civilian frame packing"); ImageDraw(&packed, strip, from, a.src[i], WHITE); }
        x += b.width + PAD;
        AssetPulse();
    }
    FreeAssetImage(strip);
    size_t bytes = (size_t)packed.width * packed.height * 4;
    packedGuard.Release();
    a.tex = MakeTexture(packed, false, true);
    return bytes;
}

static spritegen::PedAnim PedAnimByName(const std::string& s) {
    using spritegen::PedAnim;
    static const char* names[] = { "walk", "idle", "lying", "punch", "fist", "jog", "run" };
    for (int i = 0; i < (int)PedAnim::COUNT; i++) if (s == names[i]) return (PedAnim)i;
    return PedAnim::COUNT;
}

static void LoadCivilians(Assets& A) {
    using spritegen::PedAnim;
    int framePx = 0, frames = 0;
    float frameM = 0, walkM = 1.3f, scale = 1.0f, sway = 0.05f;
    PedFrames layout[(int)PedAnim::COUNT];
    PedGait jog, run;
    size_t bytes = 0, uniform = 0;
    char path[256];
    const auto records = AssetData("assets/data/civilians.cfg");
    AssetPlan({ { "records", (double)std::max<size_t>(1, records.size()), false }, { "validation", 1 } });
    int64_t completed = 0;
    AssetCount(0, (int64_t)records.size(), "records processed");
    for (const DataRecord& r : records) {
        AssetRecordStep step("records", completed, (int64_t)records.size());
        if (r.Is("CIVILIAN")) AssetPulse(TextFormat("Civilian look %lld", (long long)(A.peds.size() + 1)));
        if (r.Is("FRAME") && r.size() >= 3) { framePx = r.I(1); frameM = r.F(2); }
        else if (r.Is("SCALE") && r.size() >= 2) scale = r.F(1);
        else if (r.Is("SWAY") && r.size() >= 2) sway = r.F(1);
        else if (r.Is("WALK") && r.size() >= 2) walkM = r.F(1);
        else if (r.Is("ANIM") && r.size() >= 4) {
            PedAnim a = PedAnimByName(r[1]);
            if (a == PedAnim::COUNT) { TraceLog(LOG_WARNING, "civilians.cfg:%d unknown animation '%s'", r.line, r[1].c_str()); continue; }
            layout[(int)a] = { r.I(2), r.I(3) };
        } else if (r.Is("GAIT") && r.size() >= 6) {
            PedGait g = { r.F(2), r.F(3), r.F(4), r.F(5) };
            if (r[1] == "jog") jog = g; else if (r[1] == "run") run = g;
            else TraceLog(LOG_WARNING, "civilians.cfg:%d unknown gait '%s'", r.line, r[1].c_str());
        } else if (r.Is("CIVILIAN") && r.size() >= 2) {
            AssetValidate(framePx > 0 && frameM > 0, TextFormat("civilians.cfg line %d: CIVILIAN before FRAME", r.line));
            snprintf(path, sizeof(path), "assets/characters/civilians/%s", r[1].c_str());
            Image img = LoadRequiredImage(path);
            AssetImageGuard guard(img);
            int n = img.width / framePx, needed = 0;
            for (const PedFrames& f : layout) needed = std::max(needed, f.first + f.count);
            bool complete = layout[(int)PedAnim::Walk].count > 0 && layout[(int)PedAnim::Idle].count > 0 &&
                            layout[(int)PedAnim::Lying].count > 0 && layout[(int)PedAnim::Punch].count >= 2 &&
                            layout[(int)PedAnim::Fist].count >= 2;
            AssetValidate(img.height == framePx && complete && n >= needed && (!frames || n == frames),
                          TextFormat("civilians.cfg line %d: '%s' is %dx%d px: expected %d px frames, at least %d of them, like the others",
                                     r.line, r[1].c_str(), img.width, img.height, framePx, needed));
            frames = n;
            uniform += (size_t)img.width * img.height * 4;
            CivilianAtlas a;
            a.walkCycleM = r.F(2, walkM);
            guard.Release();
            bytes += PackCivilian(a, img, framePx);
            A.peds.push_back(a);
        }
    }
    AssetDone("records", records.empty() ? startup::Outcome::Skipped : startup::Outcome::Ready);
    if (!A.peds.empty()) {
        A.pedFramePx = framePx;
        A.pedFrameM = frameM;
        A.pedDrawScale = scale;
        A.pedSway = sway;
        for (int i = 0; i < (int)PedAnim::COUNT; i++) A.pedAnim[i] = layout[i];
        A.pedJog = jog;
        A.pedRun = run;
    }
    AssetValidate(!A.peds.empty(), "No usable civilian atlases are available");
    for (const CivilianAtlas& atlas : A.peds) AssetValidate(IsTextureValid(atlas.tex), "A civilian atlas is unavailable");
    AssetDone("validation");
    TraceLog(LOG_INFO, "CIVILIANS: %d looks, %d px frames for %.2f m, drawn x%.2f, %.1f MB packed from %.1f MB (before mipmaps)",
             (int)A.peds.size(), A.pedFramePx, A.pedFrameM, A.pedDrawScale, bytes / 1048576.0, uniform / 1048576.0);
}

// -------------------------------------------------------------------------------------
//  The 3D player (player.cfg, CJ-011): a legs atlas and an upper-body atlas per weapon set
// -------------------------------------------------------------------------------------
// Cut every frame of a grid atlas ('cols' frames per row) to its visible part and pack the parts
// in rows of at most 4096 px (4 px apart, so that mipmaps do not bleed); returns the bytes.
static size_t PackFrames(FrameAtlas& a, Image grid, int framePx, int cols) {
    AssetImageGuard gridGuard(grid);
    const int PAD = 4, MAXW = 4096;
    int frames = cols * (grid.height / framePx);
    std::vector<Rectangle> part(frames), from(frames);
    int x = PAD, y = PAD, rowH = 0, width = 1;
    a.src.resize(frames);
    a.offset.resize(frames);
    for (int i = 0; i < frames; i++) {
        Rectangle cell = { (float)((i % cols) * framePx), (float)((i / cols) * framePx), (float)framePx, (float)framePx };
        Image f;
        Rectangle b;
        { startup::AtomicSpan span(assetReporter, "image", "Player frame trim");
          f = ImageFromImage(grid, cell);
          b = GetImageAlphaBorder(f, 0.01f); }
        UnloadImage(f);
        if (b.width <= 0 || b.height <= 0) b = { framePx * 0.5f, framePx * 0.5f, 1, 1 };
        if (x + (int)b.width + PAD > MAXW) { x = PAD; y += rowH + PAD; rowH = 0; }
        from[i] = { cell.x + b.x, cell.y + b.y, b.width, b.height };
        a.src[i] = { (float)x, (float)y, b.width, b.height };
        a.offset[i] = { b.x + b.width * 0.5f - framePx * 0.5f, b.y + b.height * 0.5f - framePx * 0.5f };
        x += (int)b.width + PAD;
        rowH = std::max(rowH, (int)b.height);
        width = std::max(width, x);
        AssetPulse();
    }
    Image packed = GenImageColor(width, y + rowH + PAD, BLANK);
    AssetImageGuard packedGuard(packed);
    for (int i = 0; i < frames; i++) {
        startup::AtomicSpan span(assetReporter, "image", "Player frame packing");
        ImageDraw(&packed, grid, from[i], a.src[i], WHITE);
    }
    FreeAssetImage(grid);
    size_t bytes = (size_t)packed.width * packed.height * 4;
    packedGuard.Release();
    a.tex = MakeTexture(packed, false, true);
    return bytes;
}

static int PlayerGaitByName(const std::string& s) {
    static const char* names[] = { "stand", "walk", "run", "strafel", "strafer", "back", "jogl", "jogr" };
    for (int i = 0; i < (int)PlayerGait::COUNT; i++) if (s == names[i]) return i;
    return -1;
}
static int PlayerActByName(const std::string& s) {
    static const char* names[] = { "aim", "fire", "reload", "melee" };
    for (int i = 0; i < (int)PlayerAct::COUNT; i++) if (s == names[i]) return i;
    return -1;
}

// Loads player.cfg and its atlases; the player cannot be drawn without them (ADR-0009).
static void LoadPlayer(Assets& A) {
    const auto records = AssetData("assets/data/player.cfg");
    int framePx = 0, cols = 16;
    float frameM = 0, scale = 1.0f;
    size_t bytes = 0;
    char path[256];
    auto loadAtlas = [&](FrameAtlas& atlas, const std::string& file) {
        snprintf(path, sizeof(path), "assets/characters/player/%s", file.c_str());
        if (framePx <= 0) return false;
        bytes += PackFrames(atlas, LoadRequiredImage(path), framePx, cols);
        return true;
    };
    bool ok = !records.empty();
    for (const DataRecord& r : records) {
        AssetPulse("Player layers");
        if (r.Is("FRAME") && r.size() >= 3) { framePx = r.I(1); frameM = r.F(2); }
        else if (r.Is("SCALE") && r.size() >= 2) scale = r.F(1);
        else if (r.Is("COLS") && r.size() >= 2) cols = std::max(1, r.I(1));
        else if (r.Is("LEGS") && r.size() >= 2) ok = loadAtlas(A.playerLegs, r[1]) && ok;
        else if (r.Is("GAIT") && r.size() >= 6) {
            int g = PlayerGaitByName(r[1]);
            if (g < 0) { TraceLog(LOG_WARNING, "player.cfg:%d unknown gait '%s'", r.line, r[1].c_str()); continue; }
            PlayerGaitDef& d = A.playerGait[g];
            d.frames = { r.I(2), r.I(3) };
            d.byDistance = r[4] == "dist";
            if (d.byDistance) d.cycleM = r.F(5);
            else { d.steps = r.F(5); d.atSpeed = r.F(6, 6.5f); }
        } else if (r.Is("SET") && r.size() >= 3) {
            A.playerSets.push_back({});
            A.playerSets.back().name = r[1];
            if (!loadAtlas(A.playerSets.back().atlas, r[2])) { A.playerSets.pop_back(); ok = false; }
        } else if (r.Is("UPPER") && r.size() >= 5) {
            PlayerSet* set = nullptr;
            for (auto& s : A.playerSets) if (s.name == r[1]) set = &s;
            if (!set) { TraceLog(LOG_WARNING, "player.cfg:%d UPPER for an unknown set '%s'", r.line, r[1].c_str()); continue; }
            int g = PlayerGaitByName(r[2]), act = PlayerActByName(r[2]);
            if (g >= 0) set->gait[g] = { r.I(3), r.I(4) };
            else if (act >= 0) set->act[act] = { r.I(3), r.I(4) };
            else TraceLog(LOG_WARNING, "player.cfg:%d unknown state '%s'", r.line, r[2].c_str());
        } else if (r.Is("DOWN") && r.size() >= 4) {
            PlayerDown& D = A.playerDown;
            D.framePx = r.I(2); D.frameM = r.F(3);
            snprintf(path, sizeof(path), "assets/characters/player/%s", r[1].c_str());
            if (D.framePx > 0 && D.frameM > 0) bytes += PackFrames(D.atlas, LoadRequiredImage(path), D.framePx, cols);
            else ok = false;
        }
        else if (r.Is("FALL") && r.size() >= 4) { A.playerDown.fall = { r.I(1), r.I(2) }; A.playerDown.fallS = r.F(3); }
        else if (r.Is("LIE") && r.size() >= 2) A.playerDown.lie = r.I(1);
        else if (r.Is("GETUP") && r.size() >= 4) { A.playerDown.getUp = { r.I(1), r.I(2) }; A.playerDown.getUpS = r.F(3); }
        else if (r.Is("LAMP") && r.size() >= 8) {
            PlayerSet* set = nullptr;
            for (auto& s : A.playerSets) if (s.name == r[1]) set = &s;
            int f = r.I(2);
            if (!set || f < 0) { TraceLog(LOG_WARNING, "player.cfg:%d LAMP for an unknown set or frame", r.line); continue; }
            if ((int)set->lamp.size() <= f) set->lamp.resize(f + 1);
            set->lamp[f] = { true, r.F(3), r.F(4), r.F(5), r.F(6), r.F(7) };
        }
    }
    for (int g = 0; g < (int)PlayerGait::COUNT; g++)
        if (A.playerGait[g].frames.count <= 0 || A.playerGait[g].frames.first + A.playerGait[g].frames.count > (int)A.playerLegs.src.size()) ok = false;
    for (const PlayerSet& s : A.playerSets)
        for (int g = 0; g < (int)PlayerGait::COUNT; g++)
            if (s.gait[g].count <= 0 || s.gait[g].first + s.gait[g].count > (int)s.atlas.src.size()) ok = false;
    const PlayerDown& D = A.playerDown;
    int downFrames = (int)D.atlas.src.size();
    for (const PedFrames& f : { D.fall, D.getUp })
        if (f.first < 0 || f.count <= 0 || f.first + f.count > downFrames) ok = false;
    if (D.lie < 0 || D.lie >= downFrames || D.fallS <= 0 || D.getUpS <= 0) ok = false;
    AssetValidate(ok && !A.playerSets.empty(), "The player's atlases (player.cfg) are missing or incomplete");
    A.playerFramePx = framePx;
    A.playerFrameM = frameM;
    A.playerDrawScale = scale;
    TraceLog(LOG_INFO, "PLAYER: legs and %d weapon sets, %d px frames for %.2f m, drawn x%.2f, %.1f MB packed (before mipmaps)",
             (int)A.playerSets.size(), framePx, frameM, scale, bytes / 1048576.0);
}

const PlayerSet* Assets::FindPlayerSet(const std::string& name) const {
    for (auto& s : playerSets) if (s.name == name) return &s;
    return nullptr;
}

// -------------------------------------------------------------------------------------
//  The player (player.cfg) and weapons (weapons.cfg)
// -------------------------------------------------------------------------------------
static void LoadCharacters(Assets& A) {
    AssetPlan({ { "player", 7 }, { "weapons", 1, false }, { "validation", 1 } });
    LoadPlayer(A);
    AssetDone("player");
    int64_t completed = 0;
    const auto weapons = AssetData("assets/data/weapons.cfg");
    AssetPulse("Weapon definitions");
    AssetCount(0, (int64_t)weapons.size(), "records processed");
    for (const DataRecord& r : weapons) {
        AssetRecordStep step("weapons", completed, (int64_t)weapons.size());
        if (!r.Is("WEAPON") || r.size() < 13) continue;
        WeaponDef w;
        w.name = r[1]; w.animSet = r[2];
        w.damage = r.F(3); w.fireRate = std::max(0.1f, r.F(4)); w.spreadDeg = r.F(5); w.pellets = std::max(1, r.I(6));
        w.rangePx = r.F(7) * cfg::M; w.clip = r.I(8); w.startAmmo = r.I(9); w.reloadTime = r.F(10);
        w.kind = r[11] == "auto" ? WeaponKind::Auto : r[11] == "semi" ? WeaponKind::Semi : WeaponKind::Melee;
        w.sound = r[12];
        for (size_t k = 13; k < r.size(); k++) {
            if (r[k] == "light") w.light = true;
            else if (r[k] == "shells" && k + 2 < r.size()) { w.shellTime = r.F(k + 1); w.pumpTime = r.F(k + 2); k += 2; }
        }
        A.weapons.push_back(w);
    }
    AssetDone("weapons", weapons.empty() ? startup::Outcome::Skipped : startup::Outcome::Ready);
    AssetValidate(!A.weapons.empty(), "weapons.cfg defines no weapon");
    for (const WeaponDef& w : A.weapons)
        AssetValidate(A.FindPlayerSet(w.animSet) != nullptr,
                      "weapons.cfg: the set '" + w.animSet + "' of " + w.name + " has no player atlas in player.cfg");
    AssetDone("validation");
}

int Assets::FindWeapon(const std::string& name) const {
    for (size_t i = 0; i < weapons.size(); i++) if (weapons[i].name == name) return (int)i;
    return -1;
}

// -------------------------------------------------------------------------------------
//  Load everything
// -------------------------------------------------------------------------------------
static Texture2D LoadMaterial(const char* path) { return MakeTexture(LoadRequiredImage(path), true, true); }

bool Assets::Load(startup::Reporter* reporter) {
    AssetReportingScope reporting(reporter);
    try {
        // ---- materials ----
        AssetBegin("assets.materials", "Roads, pavements and facades");
        std::vector<startup::ChildSpec> materials;
        for (int i = 0; i < 12; i++) materials.push_back({ "texture" + std::to_string(i), 1 });
        AssetPlan(materials);
        int64_t prepared = 0;
        auto textureDone = [&](int i, int total) {
            AssetDone(("texture" + std::to_string(i)).c_str());
            AssetCount(++prepared, total, "textures prepared");
        };
        AssetCount(0, 12, "textures prepared");
        AssetPulse("Asphalt");
        asphalt  = LoadMaterial("assets/textures/asphalt.png");
        textureDone(0, 12); AssetPulse("Pavements");
        sidewalk = LoadMaterial("assets/textures/sidewalk.png");
        textureDone(1, 12); AssetPulse("Concrete");
        concrete = LoadMaterial("assets/textures/concrete.png");
        textureDone(2, 12); AssetPulse("Gravel");
        gravel   = LoadMaterial("assets/textures/gravel.png");
        textureDone(3, 12); AssetPulse("Grass");
        grass    = LoadMaterial("assets/textures/grass.png");
        textureDone(4, 12); AssetPulse("Plazas");
        plaza    = LoadMaterial("assets/textures/plaza.png");
        textureDone(5, 12); AssetPulse("Cobblestones");
        cobble   = LoadMaterial("assets/textures/cobble.png");
        textureDone(6, 12); AssetPulse("Water");
        water    = MakeWater();
        textureDone(7, 12);

        FacadeStyle brick = { 3, 1, 0.42f, 0.55f, { 38, 50, 64, 255 }, { 72, 92, 108, 255 }, { 210, 204, 192, 255 } };
        FacadeStyle glass = { 2, 1, 0.92f, 0.72f, { 58, 88, 112, 255 }, { 112, 148, 168, 255 }, { 58, 62, 68, 255 } };
        AssetPulse("Brick facades");
        facade[0]    = MakeTexture(MakeFacade("assets/textures/bricks.png", brick), true, true);
        textureDone(8, 12); AssetPulse("Glass facades");
        facade[1]    = MakeTexture(MakeFacade("assets/textures/concrete.png", glass), true, true);
        textureDone(9, 12); AssetPulse("Brick windows");
        facadeLit[0] = MakeTexture(MakeFacadeLit(brick, 8, 11), true, true);
        textureDone(10, 12); AssetPulse("Office windows");
        facadeLit[1] = MakeTexture(MakeFacadeLit(glass, 8, 23), true, true);
        textureDone(11, 12);
        AssetFinish();

        // ---- vehicles, characters, weapons ----
        AssetBegin("assets.vehicles", "Vehicle classes, paint variants and motorbikes");
        LoadVehicles(*this);
        AssetPulse("Metro car");
        trainCar = MakeTexture(AssetProcedural("Metro car", [] { return TrainCar({ 30, 110, 200, 255 }); }), false, true);
        AssetDone("train");
        AssetValidate(!vehicles.empty(), "No usable vehicle textures are available");
        for (const VehicleSprite& sprite : vehicles) AssetValidate(IsTextureValid(sprite.tex), "A vehicle texture is unavailable");
        AssetDone("validation");
        AssetFinish();
        AssetBegin("assets.civilians", "Civilian looks and animation atlases");
        LoadCivilians(*this);
        AssetFinish();
        AssetBegin("assets.animations", "Player layers and weapon definitions");
        LoadCharacters(*this);
        AssetFinish();

        // ---- foliage ----
        AssetBegin("assets.foliage", "Trees and bushes");
        char path[256];
        const auto foliage = AssetData("assets/data/foliage.cfg");
        AssetPlan({ { "records", (double)std::max<size_t>(1, foliage.size()), false }, { "validation", 1 } });
        int64_t completed = 0;
        AssetCount(0, (int64_t)foliage.size(), "records processed");
        for (const DataRecord& r : foliage) {
            AssetRecordStep step("records", completed, (int64_t)foliage.size());
            bool tree = r.Is("TREE"), bush = r.Is("BUSH");
            if ((!tree && !bush) || r.size() < 2) continue;
            AssetPulse(tree ? "Tree textures" : "Bush textures");
            snprintf(path, sizeof(path), "assets/foliage/%s", r[1].c_str());
            Image img = LoadRequiredImage(path);
            AssetImageGuard guard(img);
            Desaturate(img, tree ? 0.18f : 0.15f, tree ? 0.9f : 0.92f);   // calm the saturated toon greens
            guard.Release();
            (tree ? trees : bushes).push_back(MakeTexture(img, false, true));
        }
        AssetDone("records", foliage.empty() ? startup::Outcome::Skipped : startup::Outcome::Ready);
        AssetValidate(!trees.empty() && !bushes.empty(), "foliage.cfg needs at least one TREE and one BUSH");
        AssetDone("validation");
        AssetFinish();

        // ---- props & effects ----
        AssetBegin("assets.effects", "Street furniture and lighting effects");
        const int effectTotal = (int)Prop::COUNT + 7;
        std::vector<startup::ChildSpec> effects;
        for (int i = 0; i < effectTotal; i++) effects.push_back({ "texture" + std::to_string(i), 1 });
        AssetPlan(effects); prepared = 0; AssetCount(0, effectTotal, "textures prepared");
        for (int i = 0; i < (int)Prop::COUNT; i++) {
            AssetPulse("Street furniture");
            props[i] = MakeTexture(AssetProcedural("City object", [i] { return MakeProp((Prop)i); }), false, true);
            textureDone(i, effectTotal);
        }
        AssetPulse("Lighting and particle textures");
        lightRadial = MakeRadial(256, 1.8f);
        textureDone((int)Prop::COUNT, effectTotal);
        softCircle  = MakeRadial(64, 1.3f);
        textureDone((int)Prop::COUNT + 1, effectTotal);
        lightCone   = MakeCone();
        textureDone((int)Prop::COUNT + 2, effectTotal);
        spark       = MakeSpark();
        textureDone((int)Prop::COUNT + 3, effectTotal);
        ring        = MakeRing();
        textureDone((int)Prop::COUNT + 4, effectTotal);
        blood       = MakeBlood();
        textureDone((int)Prop::COUNT + 5, effectTotal);
        flare       = MakeFlare();
        textureDone((int)Prop::COUNT + 6, effectTotal);
        AssetFinish();

        // ---- fonts ----
        AssetBegin("assets.fonts", "Interface typefaces");
        AssetPlan({ { "bold", 1 }, { "semibold", 1 }, { "validation", 1 } });
        auto loadFont = [&](const char* path, int size, const char* role) {
            AssetValidate(FileExists(path), std::string("Missing font ") + path);
            Font f;
            { startup::AtomicSpan span(assetReporter, "font", role); f = LoadFontEx(path, size, nullptr, 0); }
            AssetValidate(IsFontValid(f), std::string("Unreadable font ") + path);
            { startup::AtomicSpan span(assetReporter, "mipmap", role); GenTextureMipmaps(&f.texture); }
            SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
            return f;
        };
        font = loadFont("assets/fonts/Rajdhani-Bold.ttf", 72, "Bold font");
        AssetDone("bold");
        AssetCount(1, 2, "font roles prepared");
        AssetPulse("Bold typeface");
        fontSemi = loadFont("assets/fonts/Rajdhani-SemiBold.ttf", 48, "Semibold font");
        AssetDone("semibold");
        AssetCount(2, 2, "font roles prepared");
        AssetDone("validation"); AssetFinish();

        // ---- shaders ----
        AssetBegin("assets.shaders", "Lighting composite and bloom shaders");
        AssetPlan({ { "composite", 1 }, { "blur", 1 } });
        { startup::AtomicSpan span(assetReporter, "shader", "Lighting composite shader"); composite = LoadShaderFromMemory(nullptr, COMPOSITE_FS); }
        locLight    = GetShaderLocation(composite, "lightMap");
        locEmissive = GetShaderLocation(composite, "emissive");
        locBloom    = GetShaderLocation(composite, "bloomTex");
        locBloomStr = GetShaderLocation(composite, "bloomStrength");
        locTime     = GetShaderLocation(composite, "time");
        AssetValidate(IsShaderValid(composite) && locLight >= 0 && locEmissive >= 0 && locBloom >= 0 && locBloomStr >= 0,
                      "The lighting composite shader is unavailable");
        AssetDone("composite"); AssetCount(1, 2, "shaders prepared");
        { startup::AtomicSpan span(assetReporter, "shader", "Bloom blur shader"); blur = LoadShaderFromMemory(nullptr, BLUR_FS); }
        locBlurDir  = GetShaderLocation(blur, "dir");
        AssetValidate(IsShaderValid(blur) && locBlurDir >= 0, "The bloom shader is unavailable");
        AssetDone("blur"); AssetCount(2, 2, "shaders prepared"); AssetFinish();

        TraceLog(LOG_INFO, "ASSETS: %d vehicle classes, %d vehicle skins, %d pedestrians, %d trees, %d bushes, %d player sets, %d weapons",
                 (int)VehicleClasses().size(), (int)vehicles.size(), (int)peds.size(), (int)trees.size(), (int)bushes.size(),
                 (int)playerSets.size(), (int)weapons.size());
        return true;
    } catch (const AssetInterrupted&) { return false; }
}

void Assets::Unload() {
    // A cancellation may leave only a prefix of the resources committed. Reset each
    // owner as it is released so cleanup remains safe if the caller invokes it twice.
    auto releaseTexture = [](Texture2D& texture) { if (texture.id) UnloadTexture(texture); texture = {}; };
    Texture2D* all[] = { &asphalt, &sidewalk, &concrete, &gravel, &grass, &plaza, &cobble, &water,
                        &facade[0], &facade[1], &facadeLit[0], &facadeLit[1], &trainCar,
                        &lightRadial, &lightCone, &softCircle, &spark, &ring, &blood, &flare };
    for (Texture2D* texture : all) releaseTexture(*texture);
    for (auto& v : vehicles) releaseTexture(v.tex);
    for (auto& p : peds) releaseTexture(p.tex);
    for (auto& texture : trees) releaseTexture(texture);
    for (auto& texture : bushes) releaseTexture(texture);
    for (auto& texture : props) releaseTexture(texture);
    releaseTexture(playerLegs.tex);
    for (auto& set : playerSets) releaseTexture(set.atlas.tex);
    vehicles.clear(); byClass.clear(); peds.clear(); trees.clear(); bushes.clear(); playerSets.clear(); weapons.clear();
    if (fontSemi.texture.id) UnloadFont(fontSemi);
    if (font.texture.id) UnloadFont(font);
    font = {}; fontSemi = {};
    if (composite.id) UnloadShader(composite);
    if (blur.id) UnloadShader(blur);
    composite = {}; blur = {};
    locLight = locEmissive = locBloom = locBloomStr = locTime = locBlurDir = -1;
}

int Assets::RandomSkin(VClass c) const {
    if (c < 0 || c >= (int)byClass.size() || byClass[c].empty()) return -1;
    return byClass[c][GRng().Int(0, (int)byClass[c].size() - 1)];
}

int Assets::RandomTrafficSkin() const {
    float total = 0;
    for (size_t c = 0; c < byClass.size(); c++) if (!byClass[c].empty()) total += Spec((int)c).trafficWeight;
    float r = GRng().Float() * total;
    for (size_t c = 0; c < byClass.size(); c++) {
        if (byClass[c].empty()) continue;
        r -= Spec((int)c).trafficWeight;
        if (r <= 0) return RandomSkin((int)c);
    }
    return 0;
}

int Assets::RandomSkinWithFlag(uint32_t flag) const {
    std::vector<int> cands;
    for (size_t c = 0; c < byClass.size(); c++)
        if ((Spec((int)c).flags & flag) && !byClass[c].empty()) cands.push_back((int)c);
    if (cands.empty()) return RandomTrafficSkin();
    return RandomSkin(cands[GRng().Int(0, (int)cands.size() - 1)]);
}

// -------------------------------------------------------------------------------------
//  Text helpers
// -------------------------------------------------------------------------------------
void DrawUIText(const char* text, float x, float y, float size, Color c, bool semi) {
    const Font& f = semi ? gAssets.fontSemi : gAssets.font;
    DrawTextEx(f, text, { x, y }, size, size * 0.03f, c);
}
float MeasureUIText(const char* text, float size, bool semi) {
    const Font& f = semi ? gAssets.fontSemi : gAssets.font;
    return MeasureTextEx(f, text, size, size * 0.03f).x;
}
void DrawUITextShadow(const char* text, float x, float y, float size, Color c, bool semi) {
    DrawUIText(text, x + 2, y + 2, size, ColorA(BLACK, 0.6f * c.a / 255.0f), semi);
    DrawUIText(text, x, y, size, c, semi);
}
