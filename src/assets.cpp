// =====================================================================================
//  Asset loading + procedural texture generation.
// =====================================================================================
#include "assets.h"
#include "math_utils.h"
#include <cstdio>
#include <cstring>
#include "datafile.h"
#include "config.h"

Assets gAssets;
using namespace spritegen;

// -------------------------------------------------------------------------------------
//  Helpers
// -------------------------------------------------------------------------------------
static Texture2D MakeTexture(Image img, bool repeat, bool mipmaps) {
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    if (mipmaps) {
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_8X);   // crisp at grazing angles / zoom
    } else {
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    }
    SetTextureWrap(t, repeat ? TEXTURE_WRAP_REPEAT : TEXTURE_WRAP_CLAMP);
    return t;
}

static bool TryLoadImage(const char* path, Image& out) {
    if (!FileExists(path)) { TraceLog(LOG_WARNING, "ASSETS: missing '%s' - using fallback", path); return false; }
    out = LoadImage(path);
    if (!out.data) return false;
    ImageFormat(&out, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    return true;
}

static Image NoiseImage(int size, Color a, Color b, float scale, int seed) {
    Image n = GenImagePerlinNoise(size, size, seed, seed * 3, scale);
    ImageFormat(&n, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Color* px = (Color*)n.data;
    for (int i = 0; i < size * size; i++) px[i] = LerpColor(a, b, px[i].r / 255.0f);
    return n;
}

// Shift hue of the saturated ("paint") pixels of a sprite -> colour variants.
static Image RecolorPaint(const Image& src, float hueShift, float satMul, float valMul) {
    Image img = ImageCopy(src);
    Color* px = (Color*)img.data;
    for (int i = 0; i < img.width * img.height; i++) {
        if (px[i].a < 8) continue;
        Vector3 hsv = ColorToHSV(px[i]);
        if (hsv.y < 0.35f || hsv.z < 0.2f) continue;          // leave glass, chrome, tyres alone
        hsv.x = fmodf(hsv.x + hueShift + 360.0f, 360.0f);
        hsv.y = Saturate(hsv.y * satMul);
        hsv.z = Saturate(hsv.z * valMul);
        Color c = ColorFromHSV(hsv.x, hsv.y, hsv.z); c.a = px[i].a; px[i] = c;
    }
    return img;
}

// Slightly calm down very saturated source art so it sits with photo textures.
static void Desaturate(Image& img, float amount, float value) {
    Color* px = (Color*)img.data;
    for (int i = 0; i < img.width * img.height; i++) {
        if (px[i].a == 0) continue;
        Vector3 hsv = ColorToHSV(px[i]);
        Color c = ColorFromHSV(hsv.x, hsv.y * (1.0f - amount), hsv.z * value); c.a = px[i].a; px[i] = c;
    }
}

static Color AveragePaint(const Image& img) {
    const Color* px = (const Color*)img.data;
    long r = 0, g = 0, b = 0, n = 0;
    for (int i = 0; i < img.width * img.height; i += 3) {
        if (px[i].a < 200) continue;
        r += px[i].r; g += px[i].g; b += px[i].b; n++;
    }
    if (!n) return GRAY;
    return { (unsigned char)(r / n), (unsigned char)(g / n), (unsigned char)(b / n), 255 };
}

static int AddVehicle(Assets& a, Image img, VClass cls, bool spawnable = true) {
    VehicleSprite s;
    s.src = GetImageAlphaBorder(img, 0.1f);
    if (s.src.width <= 0 || s.src.height <= 0) s.src = { 0, 0, (float)img.width, (float)img.height };
    s.paint = AveragePaint(img);
    s.cls = cls;
    s.spawnable = spawnable;
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

static Image MakeFacade(const char* basePath, Color fallback, const FacadeStyle& st) {
    Image img;
    if (TryLoadImage(basePath, img)) ImageResize(&img, 256, 256);
    else img = NoiseImage(256, ColorMul(fallback, 0.85f), fallback, 3.0f, 7);
    float cw = 256.0f / st.cols, ch = 256.0f / st.rows;
    for (int r = 0; r < st.rows; r++)
        for (int c = 0; c < st.cols; c++) {
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
    return img;
}

static Image MakeFacadeLit(const FacadeStyle& st, int repeats, int seed) {
    Image img = GenImageColor(256 * repeats, 256, BLANK);
    Rng rng(seed);
    float cw = 256.0f / st.cols, ch = 256.0f / st.rows;
    for (int rep = 0; rep < repeats; rep++)
        for (int r = 0; r < st.rows; r++)
            for (int c = 0; c < st.cols; c++) {
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
    return img;
}

// -------------------------------------------------------------------------------------
//  Effect textures
// -------------------------------------------------------------------------------------
static Texture2D MakeRadial(int size, float power) {
    Image img = GenImageColor(size, size, BLANK);
    Color* px = (Color*)img.data;
    float h = size * 0.5f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float d = sqrtf((x + 0.5f - h) * (x + 0.5f - h) + (y + 0.5f - h) * (y + 0.5f - h)) / h;
            float a = powf(Saturate(1.0f - d), power);
            px[y * size + x] = { 255, 255, 255, (unsigned char)(a * 255) };
        }
    return MakeTexture(img, false, true);
}

static Texture2D MakeCone() {
    const int S = 256;
    Image img = GenImageColor(S, S, BLANK);
    Color* px = (Color*)img.data;
    const float half = 22.0f * DEG2RAD;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float f = (float)(S - y), l = fabsf(x + 0.5f - S * 0.5f);
            if (f <= 0) continue;
            float ang = atan2f(l, f);
            float angular = SmoothStep(half, half * 0.4f, ang);
            float dist = powf(Saturate(1.0f - f / S), 1.2f) * SmoothStep(0.0f, 14.0f, f);
            px[y * S + x] = { 255, 255, 255, (unsigned char)(Saturate(angular * dist) * 255) };
        }
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
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float d = sqrtf((x - 63.5f) * (x - 63.5f) + (y - 63.5f) * (y - 63.5f)) / 64.0f;
            float a = expf(-((d - 0.86f) / 0.07f) * ((d - 0.86f) / 0.07f));
            px[y * S + x] = { 255, 255, 255, (unsigned char)(Saturate(a) * 255) };
        }
    return MakeTexture(img, false, true);
}

static Texture2D MakeFlare() {
    const int S = 128;
    Image img = GenImageColor(S, S, BLANK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float dx = (x - 63.5f) / 64.0f, dy = (y - 63.5f) / 64.0f;
            float d = sqrtf(dx * dx + dy * dy);
            float core = powf(Saturate(1 - d), 6.0f);
            float glow = powf(Saturate(1 - d), 2.0f) * 0.35f;
            float star = (powf(Saturate(1 - fabsf(dx) * 18), 2) * Saturate(1 - fabsf(dy))
                        + powf(Saturate(1 - fabsf(dy) * 18), 2) * Saturate(1 - fabsf(dx))) * 0.5f;
            px[y * S + x] = { 255, 255, 255, (unsigned char)(Saturate(core + glow + star) * 255) };
        }
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
    Color* px = (Color*)img.data;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float u = x / (float)S * 2 * PI, v = y / (float)S * 2 * PI;   // tileable waves
            float w = sinf(u * 3 + sinf(v * 2) * 1.5f) * 0.5f + sinf(v * 5 + u * 2) * 0.3f + sinf((u + v) * 7) * 0.2f;
            float t = 0.5f + 0.5f * w;
            px[y * S + x] = LerpColor({ 18, 48, 62, 255 }, { 46, 96, 112, 255 }, t * t);
        }
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
//  Built-in defaults for the data files (used only if assets/data/*.cfg is missing)
// -------------------------------------------------------------------------------------
static const char* DEFAULT_VEHICLE_SPRITES = R"(
# SPRITE  <class> <file> <extra paint variants 0-3>
SPRITE Stinger   audi.png       3
SPRITE Viper     viper.png      0
SPRITE Bruiser   muscle.png     3
SPRITE Taxi      taxi.png       0
SPRITE Pickup    pickup.png     3
SPRITE Van       van.png        2
SPRITE Ambulance ambulance.png  0
SPRITE Police    police.png     0
SPRITE Semi      semi.png       2
# DERIVE  <class> stretch <file> <band start> <band end> <factor> <paint colours...>
DERIVE Limo      stretch muscle.png 0.40 0.58 2.3  black white silver
DERIVE Bus       stretch van.png    0.30 0.93 2.5  orange yellow blue white
# COMPOSE <class> <front file> <from> <to> <rear file> <from> <to> <rear stretch> <paint colours...>
COMPOSE BoxTruck  semi.png 0.00 0.52 van.png 0.28 1.00 1.35 white navy cream grey
COMPOSE FireTruck semi.png 0.00 0.52 van.png 0.28 1.00 1.35 red
COMPOSE Garbage   semi.png 0.00 0.52 van.png 0.28 1.00 1.30 darkgreen orange
# BIKE <class> <empty file> <ridden file>
BIKE Sportbike sportbike-empty.png sportbike-ridden.png
BIKE Chopper   chopper-empty.png   chopper-ridden.png
BIKE Scooter   scooter-empty.png   scooter-ridden.png
)";

static const char* DEFAULT_CHARACTERS = R"(
SCALE 0.062
# unarmed set = knife frames with the blade removed (tools/make_unarmed.py)
ANIM unarmed idle   20 117.9 110.3 survivor/unarmed/idle        survivor-idle_unarmed_
ANIM unarmed move   20 117.9 109.3 survivor/unarmed/move        survivor-move_unarmed_
ANIM unarmed melee  15 114.9 110.3 survivor/unarmed/meleeattack survivor-meleeattack_unarmed_
ANIM flashlight idle   20 103.7 102.2 survivor/flashlight/idle        survivor-idle_flashlight_
ANIM flashlight move   20 103.7 110.3 survivor/flashlight/move        survivor-move_flashlight_
ANIM flashlight melee  15 109.7 126.2 survivor/flashlight/meleeattack survivor-meleeattack_flashlight_
ANIM handgun idle   20 107.7 120.4 survivor/handgun/idle        survivor-idle_handgun_
ANIM handgun move   20 112.7 119.4 survivor/handgun/move        survivor-move_handgun_
ANIM handgun shoot   3 109.7 119.4 survivor/handgun/shoot       survivor-shoot_handgun_
ANIM handgun reload 15 111.7 119.4 survivor/handgun/reload      survivor-reload_handgun_
ANIM handgun melee  15 114.7 124.4 survivor/handgun/meleeattack survivor-meleeattack_handgun_
ANIM knife idle     20 117.9 110.3 survivor/knife/idle        survivor-idle_knife_
ANIM knife move     20 117.9 109.3 survivor/knife/move        survivor-move_knife_
ANIM knife melee    15 114.9 110.3 survivor/knife/meleeattack survivor-meleeattack_knife_
ANIM rifle idle     20 104.7 118.3 survivor/rifle/idle        survivor-idle_rifle_
ANIM rifle move     20 104.7 117.3 survivor/rifle/move        survivor-move_rifle_
ANIM rifle shoot     3 103.7 117.3 survivor/rifle/shoot       survivor-shoot_rifle_
ANIM rifle reload   20 110.7 119.3 survivor/rifle/reload      survivor-reload_rifle_
ANIM rifle melee    15 124.7 199.3 survivor/rifle/meleeattack survivor-meleeattack_rifle_
ANIM shotgun idle   20 104.8 118.4 survivor/shotgun/idle        survivor-idle_shotgun_
ANIM shotgun move   20 104.8 117.4 survivor/shotgun/move        survivor-move_shotgun_
ANIM shotgun shoot   3 103.8 117.4 survivor/shotgun/shoot       survivor-shoot_shotgun_
ANIM shotgun reload 20 110.8 119.4 survivor/shotgun/reload      survivor-reload_shotgun_
ANIM shotgun melee  15 124.8 199.4 survivor/shotgun/meleeattack survivor-meleeattack_shotgun_
FEET idle  1 survivor/feet/idle survivor-idle_
FEET walk 20 survivor/feet/walk survivor-walk_
FEET run  20 survivor/feet/run  survivor-run_
FEET strafe_left  20 survivor/feet/strafe_left  survivor-strafe_left_
FEET strafe_right 20 survivor/feet/strafe_right survivor-strafe_right_
)";

static const char* DEFAULT_WEAPONS = R"(
WEAPON Fists      unarmed     12  2.5  0  1  1.2  0   0    0    melee punch
WEAPON Knife      knife       40  2.0  0  1  1.4  0   0    0    melee knife
WEAPON Pistol     handgun     26  4.0  3  1  40   12  96   1.2  semi  pistol
WEAPON Shotgun    shotgun     14  1.1  14 8  22   6   36   2.2  semi  shotgun
WEAPON Rifle      rifle       22  10   4  1  60   30  180  2.0  auto  rifle
WEAPON Flashlight flashlight  10  2.0  0  1  1.4  0   0    0    melee punch light
)";

static const char* DEFAULT_CIVILIANS = R"(
FRAME 144 2.1
SCALE 1.12
SWAY 0
ANIM walk 0 16
ANIM idle 16 1
ANIM lying 17 1
ANIM punch 18 2
ANIM fist 20 2
ANIM jog 22 8
ANIM run 30 8
GAIT jog 2.0 2.5 4.0 2.8
GAIT run 4.0 2.8 6.5 3.3
WALK 1.05
CIVILIAN civilian-01.png
CIVILIAN civilian-02.png
CIVILIAN civilian-03.png
CIVILIAN civilian-04.png
CIVILIAN civilian-05.png
CIVILIAN civilian-06.png
CIVILIAN civilian-07.png
CIVILIAN civilian-08.png
CIVILIAN civilian-09.png
CIVILIAN civilian-10.png
CIVILIAN civilian-11.png
CIVILIAN civilian-12.png
CIVILIAN civilian-13.png
CIVILIAN civilian-14.png
CIVILIAN civilian-15.png
CIVILIAN civilian-16.png
CIVILIAN civilian-17.png
CIVILIAN civilian-18.png
CIVILIAN civilian-19.png
CIVILIAN civilian-20.png
CIVILIAN civilian-21.png
CIVILIAN civilian-22.png
CIVILIAN civilian-23.png
CIVILIAN civilian-24.png
CIVILIAN civilian-25.png
CIVILIAN civilian-26.png
CIVILIAN civilian-27.png
CIVILIAN civilian-28.png
)";

static const char* DEFAULT_FOLIAGE = R"(
TREE oaktoonbranchesgreen.png
TREE oaktoonbranchesolivegreen.png
TREE oaktoonbranchesautunm.png
TREE oaktoonbranchesorange.png
TREE oaktoonbranchescold.png
TREE treetoonbranchtextures.png
TREE treetoonbranchautunm.png
TREE treetoonbranchorange.png
TREE treetoonbranchcold.png
TREE kapoktoonleaves.png
TREE kapoktoonleavesmatured.png
TREE toonsimplescircularbranches.png
TREE toonsimplesbasicbranch.png
TREE toonsimplesbasicbranchjungle.png
TREE toonsimplesbasicbranchautunm.png
TREE toonsimplesbasicbranchexoticcontrastpink.png
TREE toonsimplesbasicbranchexoticpurple.png
BUSH toonsimplesleafs.png
BUSH toonsimplesyoungleafs.png
BUSH kapoktoonleavesemergent.png
BUSH toonsimplesleafsorange.png
BUSH toonsimplesleafreds.png
BUSH toonsimplesleafsredcontrast.png
BUSH toonsimplesleafintenseyellow.png
BUSH toonsimplesleafsyellowdeciduous.png
BUSH toonsimplesleafsyellowdesciduousextra.png
BUSH toonsimplesbasicbranchexoticcontrastyellow.png
BUSH toonsimplesbasicbranchexoticcontrastreds.png
BUSH toonsimplesbasicbranchcold.png
)";

// -------------------------------------------------------------------------------------
//  Colour parsing for GEN lines: named colours or r,g,b ; several joined by '/'
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
    if (sscanf(s.c_str(), "%d,%d,%d", &r, &g, &b) == 3) return { (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
    TraceLog(LOG_WARNING, "ASSETS: unknown colour '%s'", s.c_str());
    return GRAY;
}
static std::vector<Color> ParseColors(const std::string& s) {
    std::vector<Color> out;
    size_t a = 0;
    while (a <= s.size()) {
        size_t b = s.find('/', a);
        if (b == std::string::npos) b = s.size();
        if (b > a) out.push_back(ParseColor(s.substr(a, b - a)));
        a = b + 1;
    }
    return out;
}
static Color Pick(const std::vector<Color>& v, size_t i, Color def) { return i < v.size() ? v[i] : def; }

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
    ImageResize(&band, c.width, bandH);
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
    ImageResize(&rear, rw, std::max(1, (int)(rear.height * ((float)rw / rear.width) * stretch)));
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
    Color* px = (Color*)img.data;
    Vector3 t = ColorToHSV(target);
    for (int i = 0; i < img.width * img.height; i++) {
        if (px[i].a < 8) continue;
        Vector3 hsv = ColorToHSV(px[i]);
        bool paint = hsv.y > 0.35f && hsv.z > 0.2f;
        bool panel = neutralToo && hsv.y < 0.2f && hsv.z > 0.62f;
        if (!paint && !panel) continue;
        float v = Saturate(hsv.z * (t.z + 0.08f) / (panel ? 0.92f : 0.85f));
        Color c = ColorFromHSV(t.x, t.y, v); c.a = px[i].a; px[i] = c;
    }
    return img;
}

// -------------------------------------------------------------------------------------
//  Vehicles (vehicles.cfg: SPRITE / DERIVE / COMPOSE / BIKE / GEN records)
// -------------------------------------------------------------------------------------
static void LoadVehicles(Assets& A) {
    LoadVehicleClasses();
    A.byClass.assign(VehicleClasses().size(), {});
    std::string fallback = std::string(DefaultVehiclesCfg()) + DEFAULT_VEHICLE_SPRITES;
    char path[256];
    const float hues[3] = { 130, 215, 0 };
    for (const DataRecord& r : ReadDataFile("assets/data/vehicles.cfg", fallback.c_str())) {
        if (r.Is("SPRITE") && r.size() >= 3) {
            VClass cls = FindVehicleClass(r[1]);
            if (cls < 0) { TraceLog(LOG_WARNING, "vehicles.cfg:%d unknown class '%s'", r.line, r[1].c_str()); continue; }
            snprintf(path, sizeof(path), "assets/vehicles/%s", r[2].c_str());
            Image img;
            if (!TryLoadImage(path, img)) continue;
            int variants = std::min(3, r.I(3, 0));
            for (int v = 0; v < variants; v++) {
                if (v == 2) AddVehicle(A, RecolorPaint(img, 0, 0.08f, 1.15f), cls);   // silver
                else        AddVehicle(A, RecolorPaint(img, hues[v], 1.0f, 0.95f), cls);
            }
            AddVehicle(A, img, cls);
        } else if (r.Is("BIKE") && r.size() >= 4) {
            VClass cls = FindVehicleClass(r[1]);
            if (cls < 0) { TraceLog(LOG_WARNING, "vehicles.cfg:%d unknown class '%s'", r.line, r[1].c_str()); continue; }
            char pathR[256];
            snprintf(path, sizeof(path), "assets/vehicles/%s", r[2].c_str());
            snprintf(pathR, sizeof(pathR), "assets/vehicles/%s", r[3].c_str());
            Image empty, ridden;
            if (!TryLoadImage(path, empty)) continue;
            if (!TryLoadImage(pathR, ridden)) { UnloadImage(empty); continue; }
            int e = AddVehicle(A, empty, cls, false);
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
            VClass cls = FindVehicleClass(r[1]);
            if (cls < 0) { TraceLog(LOG_WARNING, "vehicles.cfg:%d unknown class '%s'", r.line, r[1].c_str()); continue; }
            Image base{};
            size_t firstColour;
            if (r.Is("DERIVE")) {
                snprintf(path, sizeof(path), "assets/vehicles/%s", r[3].c_str());
                Image a;
                if (!TryLoadImage(path, a)) continue;
                base = StretchBand(a, r.F(4), r.F(5), r.F(6));
                UnloadImage(a);
                firstColour = 7;
            } else {
                char pathB[256];
                snprintf(path, sizeof(path), "assets/vehicles/%s", r[2].c_str());
                snprintf(pathB, sizeof(pathB), "assets/vehicles/%s", r[5].c_str());
                Image a, b;
                if (!TryLoadImage(path, a)) continue;
                if (!TryLoadImage(pathB, b)) { UnloadImage(a); continue; }
                base = ComposeVehicle(a, r.F(3), r.F(4), b, r.F(6), r.F(7), r.F(8, 1.0f));
                UnloadImage(a); UnloadImage(b);
                firstColour = 9;
            }
            if (r.size() <= firstColour) AddVehicle(A, ImageCopy(base), cls);
            for (size_t k = firstColour; k < r.size(); k++) AddVehicle(A, Repaint(base, ParseColor(r[k]), true), cls);
            UnloadImage(base);
        } else if (r.Is("GEN") && r.size() >= 3) {
            VClass cls = FindVehicleClass(r[1]);
            if (cls < 0) { TraceLog(LOG_WARNING, "vehicles.cfg:%d unknown class '%s'", r.line, r[1].c_str()); continue; }
            const std::string& g = r[2];
            std::vector<std::vector<Color>> paints;
            for (size_t k = 3; k < r.size(); k++) paints.push_back(ParseColors(r[k]));
            if (paints.empty()) paints.push_back({ GRAY });
            for (const auto& c : paints) {
                Color c0 = Pick(c, 0, GRAY), c1 = Pick(c, 1, WHITE), c2 = Pick(c, 2, RED);
                if      (g == "car_hatch")  AddVehicle(A, Car(c0, CarStyle::Hatchback), cls);
                else if (g == "car_sedan")  AddVehicle(A, Car(c0, CarStyle::Sedan), cls);
                else if (g == "car_coupe")  AddVehicle(A, Car(c0, CarStyle::Coupe), cls);
                else if (g == "car_suv")    AddVehicle(A, Car(c0, CarStyle::SUV), cls);
                else if (g == "car_limo")   AddVehicle(A, Car(c0, CarStyle::Limo), cls);
                else if (g == "bus")        AddVehicle(A, Bus(c0, c1), cls);
                else if (g == "boxtruck")   AddVehicle(A, BoxTruck(c0, c1, c2), cls);
                else if (g == "firetruck")  AddVehicle(A, FireTruck(), cls);
                else if (g == "garbage")    AddVehicle(A, GarbageTruck(c0), cls);
                else if (g.rfind("bike_", 0) == 0 || g == "bike_scooter") {
                    BikeStyle st = g == "bike_chopper" ? BikeStyle::Chopper : g == "bike_scooter" ? BikeStyle::Scooter : BikeStyle::Sport;
                    int empty = AddVehicle(A, Motorbike(c0, false, c1, c2, st), cls, false);
                    int ridden = AddVehicle(A, Motorbike(c0, true, c1, c2, st), cls);
                    A.vehicles[ridden].emptySkin = empty;
                    A.vehicles[empty].emptySkin = empty;
                } else TraceLog(LOG_WARNING, "vehicles.cfg:%d unknown generator '%s'", r.line, g.c_str());
            }
        }
    }
    if (A.vehicles.empty()) AddVehicle(A, Car({ 200, 30, 35, 255 }, CarStyle::Hatchback), 0);
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
    const int PAD = 4, frames = strip.width / framePx;
    std::vector<Rectangle> part(frames);
    int width = PAD, height = 1;
    for (int i = 0; i < frames; i++) {
        Image f = ImageFromImage(strip, { (float)(i * framePx), 0, (float)framePx, (float)framePx });
        Rectangle b = GetImageAlphaBorder(f, 0.01f);
        UnloadImage(f);
        if (b.width <= 0 || b.height <= 0) b = { framePx * 0.5f, framePx * 0.5f, 1, 1 };
        part[i] = b;
        width += (int)b.width + PAD;
        height = std::max(height, (int)b.height);
    }
    Image packed = GenImageColor(width, height + 2 * PAD, BLANK);
    a.src.resize(frames);
    a.offset.resize(frames);
    float x = PAD;
    for (int i = 0; i < frames; i++) {
        const Rectangle& b = part[i];
        Rectangle from = { i * (float)framePx + b.x, b.y, b.width, b.height };
        a.src[i] = { x, (float)PAD, b.width, b.height };
        a.offset[i] = { b.x + b.width * 0.5f - framePx * 0.5f, b.y + b.height * 0.5f - framePx * 0.5f };
        ImageDraw(&packed, strip, from, a.src[i], WHITE);
        x += b.width + PAD;
    }
    UnloadImage(strip);
    size_t bytes = (size_t)packed.width * packed.height * 4;
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
    for (const DataRecord& r : ReadDataFile("assets/data/civilians.cfg", DEFAULT_CIVILIANS)) {
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
            if (framePx <= 0 || frameM <= 0) { TraceLog(LOG_WARNING, "civilians.cfg:%d CIVILIAN before FRAME", r.line); continue; }
            snprintf(path, sizeof(path), "assets/characters/civilians/%s", r[1].c_str());
            Image img;
            if (!TryLoadImage(path, img)) continue;
            int n = img.width / framePx, needed = 0;
            for (const PedFrames& f : layout) needed = std::max(needed, f.first + f.count);
            bool complete = layout[(int)PedAnim::Walk].count > 0 && layout[(int)PedAnim::Idle].count > 0 &&
                            layout[(int)PedAnim::Lying].count > 0 && layout[(int)PedAnim::Punch].count >= 2 &&
                            layout[(int)PedAnim::Fist].count >= 2;
            if (img.height != framePx || !complete || n < needed || (frames && n != frames)) {
                TraceLog(LOG_WARNING, "civilians.cfg:%d '%s' is %dx%d px: expected %d px frames, at least %d of them, like the others",
                         r.line, r[1].c_str(), img.width, img.height, framePx, needed);
                UnloadImage(img);
                continue;
            }
            frames = n;
            uniform += (size_t)img.width * img.height * 4;
            CivilianAtlas a;
            a.walkCycleM = r.F(2, walkM);
            bytes += PackCivilian(a, img, framePx);
            A.peds.push_back(a);
        }
    }
    if (!A.peds.empty()) {
        A.pedFramePx = framePx;
        A.pedFrameM = frameM;
        A.pedDrawScale = scale;
        A.pedSway = sway;
        for (int i = 0; i < (int)PedAnim::COUNT; i++) A.pedAnim[i] = layout[i];
        A.pedJog = jog;
        A.pedRun = run;
    } else {                    // procedural fallback: 28 looks in the 96 px / 1.4 m format, no jog or run
        using namespace spritegen;
        A.pedAnim[(int)PedAnim::Walk] = { 0, PED_WALK_FRAMES };
        A.pedAnim[(int)PedAnim::Idle] = { PED_FRAME_IDLE, 1 };
        A.pedAnim[(int)PedAnim::Lying] = { PED_FRAME_DOWN, 1 };
        A.pedAnim[(int)PedAnim::Punch] = { PED_FRAME_PUNCH, 2 };
        A.pedAnim[(int)PedAnim::Fist] = { PED_FRAME_FIST, 2 };
        for (int i = 0; i < 28; i++) {
            Image img = PedAtlas(RandomPedLook(1000 + i * 7));
            uniform += (size_t)img.width * img.height * 4;
            CivilianAtlas a;
            bytes += PackCivilian(a, img, PED_FRAME);
            A.peds.push_back(a);
        }
    }
    TraceLog(LOG_INFO, "CIVILIANS: %d looks, %d px frames for %.2f m, drawn x%.2f, %.1f MB packed from %.1f MB (before mipmaps)",
             (int)A.peds.size(), A.pedFramePx, A.pedFrameM, A.pedDrawScale, bytes / 1048576.0, uniform / 1048576.0);
}

// -------------------------------------------------------------------------------------
//  Character animation sets (characters.cfg) and weapons (weapons.cfg)
// -------------------------------------------------------------------------------------
static SpriteAnim LoadAnim(const std::string& dir, const std::string& prefix, int frames, Vector2 pivot, bool centrePivot) {
    SpriteAnim a;
    std::vector<Image> imgs;
    char path[512];
    for (int i = 0; i < frames; i++) {
        snprintf(path, sizeof(path), "assets/%s/%s%d.png", dir.c_str(), prefix.c_str(), i);
        Image img;
        if (!TryLoadImage(path, img)) break;
        imgs.push_back(img);
    }
    if (imgs.empty()) return a;
    // frames within one animation share a size; downscale 50% (still ~3x the on-screen size)
    int fw = imgs[0].width / 2, fh = imgs[0].height / 2;
    a.frames = (int)imgs.size();
    a.cols = std::min(a.frames, 5);
    int rows = (a.frames + a.cols - 1) / a.cols;
    Image atlas = GenImageColor(fw * a.cols, fh * rows, BLANK);
    for (int i = 0; i < a.frames; i++) {
        ImageResize(&imgs[i], fw, fh);
        ImageDraw(&atlas, imgs[i], { 0, 0, (float)fw, (float)fh },
                  { (float)((i % a.cols) * fw), (float)((i / a.cols) * fh), (float)fw, (float)fh }, WHITE);
        UnloadImage(imgs[i]);
    }
    a.frameSize = { (float)fw, (float)fh };
    a.pivot = centrePivot ? Vector2{ fw * 0.5f, fh * 0.5f } : Vector2{ pivot.x * 0.5f, pivot.y * 0.5f };
    a.atlas = MakeTexture(atlas, false, true);
    return a;
}

static int StateIndex(const std::string& s) {
    if (s == "idle") return (int)BodyAnim::Idle;
    if (s == "move") return (int)BodyAnim::Move;
    if (s == "shoot") return (int)BodyAnim::Shoot;
    if (s == "reload") return (int)BodyAnim::Reload;
    if (s == "melee") return (int)BodyAnim::Melee;
    return -1;
}

static void LoadCharacters(Assets& A) {
    for (const DataRecord& r : ReadDataFile("assets/data/characters.cfg", DEFAULT_CHARACTERS)) {
        if (r.Is("SCALE")) A.charScale = r.F(1, A.charScale) * 2.0f;          // atlases are stored at 50%
        else if (r.Is("ANIM") && r.size() >= 8) {
            int st = StateIndex(r[2]);
            if (st < 0) { TraceLog(LOG_WARNING, "characters.cfg:%d unknown state '%s'", r.line, r[2].c_str()); continue; }
            CharacterSet* set = nullptr;
            for (auto& s : A.charSets) if (s.name == r[1]) set = &s;
            if (!set) { A.charSets.push_back({}); set = &A.charSets.back(); set->name = r[1]; }
            set->anim[st] = LoadAnim(r[6], r[7], r.I(3), { r.F(4), r.F(5) }, false);
        } else if (r.Is("FEET") && r.size() >= 5) {
            int k = r[1] == "idle" ? 0 : r[1] == "walk" ? 1 : r[1] == "run" ? 2 : r[1] == "strafe_left" ? 3 : r[1] == "strafe_right" ? 4 : -1;
            if (k >= 0) A.feet[k] = LoadAnim(r[3], r[4], r.I(2), {}, true);
        }
    }
    for (const DataRecord& r : ReadDataFile("assets/data/weapons.cfg", DEFAULT_WEAPONS)) {
        if (!r.Is("WEAPON") || r.size() < 13) continue;
        WeaponDef w;
        w.name = r[1]; w.animSet = r[2];
        w.damage = r.F(3); w.fireRate = std::max(0.1f, r.F(4)); w.spreadDeg = r.F(5); w.pellets = std::max(1, r.I(6));
        w.rangePx = r.F(7) * cfg::M; w.clip = r.I(8); w.startAmmo = r.I(9); w.reloadTime = r.F(10);
        w.kind = r[11] == "auto" ? WeaponKind::Auto : r[11] == "semi" ? WeaponKind::Semi : WeaponKind::Melee;
        w.sound = r[12];
        for (size_t k = 13; k < r.size(); k++) if (r[k] == "light") w.light = true;
        A.weapons.push_back(w);
    }
    if (A.weapons.empty()) { WeaponDef w; w.name = "Fists"; w.animSet = "unarmed"; A.weapons.push_back(w); }
}

const CharacterSet* Assets::FindSet(const std::string& name) const {
    for (auto& s : charSets) if (s.name == name) return &s;
    return nullptr;
}
int Assets::FindWeapon(const std::string& name) const {
    for (size_t i = 0; i < weapons.size(); i++) if (weapons[i].name == name) return (int)i;
    return -1;
}

// -------------------------------------------------------------------------------------
//  Load everything
// -------------------------------------------------------------------------------------
static Texture2D LoadMaterial(const char* path, Color fa, Color fb) {
    Image img;
    if (!TryLoadImage(path, img)) img = NoiseImage(256, fa, fb, 4.0f, 3);
    return MakeTexture(img, true, true);
}

bool Assets::Load() {
    // ---- materials ----
    asphalt  = LoadMaterial("assets/textures/asphalt.png",  { 40, 40, 42, 255 }, { 70, 70, 72, 255 });
    sidewalk = LoadMaterial("assets/textures/sidewalk.png", { 150, 145, 135, 255 }, { 185, 180, 170, 255 });
    concrete = LoadMaterial("assets/textures/concrete.png", { 160, 160, 160, 255 }, { 200, 200, 200, 255 });
    gravel   = LoadMaterial("assets/textures/gravel.png",   { 90, 90, 85, 255 }, { 130, 125, 120, 255 });
    grass    = LoadMaterial("assets/textures/grass.png",    { 40, 90, 30, 255 }, { 80, 130, 50, 255 });
    plaza    = LoadMaterial("assets/textures/plaza.png",    { 150, 110, 100, 255 }, { 190, 150, 130, 255 });
    cobble   = LoadMaterial("assets/textures/cobble.png",   { 110, 110, 100, 255 }, { 150, 150, 140, 255 });
    water    = MakeWater();

    FacadeStyle brick = { 3, 1, 0.42f, 0.55f, { 38, 50, 64, 255 }, { 72, 92, 108, 255 }, { 210, 204, 192, 255 } };
    FacadeStyle glass = { 2, 1, 0.92f, 0.72f, { 58, 88, 112, 255 }, { 112, 148, 168, 255 }, { 58, 62, 68, 255 } };
    facade[0]    = MakeTexture(MakeFacade("assets/textures/bricks.png",   { 150, 80, 60, 255 }, brick), true, true);
    facade[1]    = MakeTexture(MakeFacade("assets/textures/concrete.png", { 190, 190, 190, 255 }, glass), true, true);
    facadeLit[0] = MakeTexture(MakeFacadeLit(brick, 8, 11), true, true);
    facadeLit[1] = MakeTexture(MakeFacadeLit(glass, 8, 23), true, true);

    // ---- vehicles, characters, weapons ----
    LoadVehicles(*this);
    trainCar = MakeTexture(TrainCar({ 30, 110, 200, 255 }), false, true);
    LoadCivilians(*this);
    playerUnarmed = MakeTexture(PedAtlas(PlayerLook()), false, true);
    LoadCharacters(*this);

    // ---- foliage ----
    char path[256];
    for (const DataRecord& r : ReadDataFile("assets/data/foliage.cfg", DEFAULT_FOLIAGE)) {
        bool tree = r.Is("TREE"), bush = r.Is("BUSH");
        if ((!tree && !bush) || r.size() < 2) continue;
        snprintf(path, sizeof(path), "assets/foliage/%s", r[1].c_str());
        Image img;
        if (!TryLoadImage(path, img)) continue;
        Desaturate(img, tree ? 0.18f : 0.15f, tree ? 0.9f : 0.92f);   // calm the saturated toon greens
        (tree ? trees : bushes).push_back(MakeTexture(img, false, true));
    }
    if (trees.empty()) for (int v = 0; v < 5; v++) trees.push_back(MakeTexture(Tree(v, 256, 7), false, true));
    if (bushes.empty()) bushes.push_back(MakeTexture(Tree(1, 96, 3), false, true));

    // ---- props & effects ----
    for (int i = 0; i < (int)Prop::COUNT; i++) props[i] = MakeTexture(MakeProp((Prop)i), false, true);
    lightRadial = MakeRadial(256, 1.8f);
    softCircle  = MakeRadial(64, 1.3f);
    lightCone   = MakeCone();
    spark       = MakeSpark();
    ring        = MakeRing();
    blood       = MakeBlood();
    flare       = MakeFlare();

    // ---- fonts ----
    if (FileExists("assets/fonts/Rajdhani-Bold.ttf")) {
        font = LoadFontEx("assets/fonts/Rajdhani-Bold.ttf", 72, nullptr, 0);
        fontSemi = FileExists("assets/fonts/Rajdhani-SemiBold.ttf")
                       ? LoadFontEx("assets/fonts/Rajdhani-SemiBold.ttf", 48, nullptr, 0) : font;
        GenTextureMipmaps(&font.texture);     SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
        GenTextureMipmaps(&fontSemi.texture); SetTextureFilter(fontSemi.texture, TEXTURE_FILTER_TRILINEAR);
        customFont = font.texture.id != 0;
    }
    if (!customFont) { font = GetFontDefault(); fontSemi = font; }

    // ---- shaders ----
    composite   = LoadShaderFromMemory(nullptr, COMPOSITE_FS);
    locLight    = GetShaderLocation(composite, "lightMap");
    locEmissive = GetShaderLocation(composite, "emissive");
    locBloom    = GetShaderLocation(composite, "bloomTex");
    locBloomStr = GetShaderLocation(composite, "bloomStrength");
    locTime     = GetShaderLocation(composite, "time");
    blur        = LoadShaderFromMemory(nullptr, BLUR_FS);
    locBlurDir  = GetShaderLocation(blur, "dir");

    TraceLog(LOG_INFO, "ASSETS: %d vehicle classes, %d vehicle skins, %d pedestrians, %d trees, %d bushes, %d anim sets, %d weapons",
             (int)VehicleClasses().size(), (int)vehicles.size(), (int)peds.size(), (int)trees.size(), (int)bushes.size(),
             (int)charSets.size(), (int)weapons.size());
    return true;
}

void Assets::Unload() {
    Texture2D all[] = { asphalt, sidewalk, concrete, gravel, grass, plaza, cobble, water, facade[0], facade[1], facadeLit[0], facadeLit[1],
                        trainCar, playerUnarmed, lightRadial, lightCone, softCircle, spark, ring, blood, flare };
    for (auto& t : all) if (t.id) UnloadTexture(t);
    for (auto& v : vehicles) UnloadTexture(v.tex);
    for (auto& p : peds) UnloadTexture(p.tex);
    for (auto& t : trees) UnloadTexture(t);
    for (auto& t : bushes) UnloadTexture(t);
    for (auto& t : props) if (t.id) UnloadTexture(t);
    for (auto& s : charSets) for (auto& a : s.anim) if (a.atlas.id) UnloadTexture(a.atlas);
    for (auto& a : feet) if (a.atlas.id) UnloadTexture(a.atlas);
    if (customFont) { if (fontSemi.texture.id != font.texture.id) UnloadFont(fontSemi); UnloadFont(font); }
    UnloadShader(composite);
    UnloadShader(blur);
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
