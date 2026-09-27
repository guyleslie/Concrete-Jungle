// =====================================================================================
//  3D renderer core - see render.h
// =====================================================================================
#include "render.h"
#include "assets.h"
#include "config.h"
#include "rlgl.h"

// -------------------------------------------------------------------------------------
//  Immediate-mode helpers
// -------------------------------------------------------------------------------------
static inline void V(Vector2 p, float h) { rlVertex3f(p.x, h, p.y); }
static inline void C(Color c) { rlColor4ub(c.r, c.g, c.b, c.a); }

void DrawFlatSprite(Texture2D tex, Rectangle src, Vector2 pos, float h, float width, float length,
                    float angle, Color tint, Vector2 origin) {
    if (!tex.id) return;
    Vector2 r = RightOf(angle), f = Forward(angle);
    float u0 = src.x / tex.width, v0 = src.y / tex.height;
    float u1 = (src.x + src.width) / tex.width, v1 = (src.y + src.height) / tex.height;
    auto corner = [&](float u, float v) { return pos + r * ((u - origin.x) * width) + f * ((origin.y - v) * length); };
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    C(tint); rlNormal3f(0, 1, 0);
    rlTexCoord2f(u0, v0); V(corner(0, 0), h);
    rlTexCoord2f(u0, v1); V(corner(0, 1), h);
    rlTexCoord2f(u1, v1); V(corner(1, 1), h);
    rlTexCoord2f(u1, v0); V(corner(1, 0), h);
    rlEnd();
    rlSetTexture(0);
}

void DrawFlatSpriteX(Texture2D tex, Rectangle src, Vector2 pos, float h, float scale, Vector2 pivot, float angle, Color tint) {
    if (!tex.id) return;
    Vector2 r = RightOf(angle), f = Forward(angle);
    float u0 = src.x / tex.width, v0 = src.y / tex.height;
    float u1 = (src.x + src.width) / tex.width, v1 = (src.y + src.height) / tex.height;
    auto corner = [&](float x, float y) { return pos + f * ((x - pivot.x) * scale) + r * ((y - pivot.y) * scale); };
    float W = fabsf(src.width), H = fabsf(src.height);
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    C(tint); rlNormal3f(0, 1, 0);
    rlTexCoord2f(u0, v0); V(corner(0, 0), h);
    rlTexCoord2f(u0, v1); V(corner(0, H), h);
    rlTexCoord2f(u1, v1); V(corner(W, H), h);
    rlTexCoord2f(u1, v0); V(corner(W, 0), h);
    rlEnd();
    rlSetTexture(0);
}

void DrawGroundTex(Texture2D tex, Rectangle a, float h, float worldPerTex, Color tint, Vector2 o) {
    float u0 = a.x / worldPerTex + o.x, v0 = a.y / worldPerTex + o.y;
    float u1 = (a.x + a.width) / worldPerTex + o.x, v1 = (a.y + a.height) / worldPerTex + o.y;
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    C(tint); rlNormal3f(0, 1, 0);
    rlTexCoord2f(u0, v0); rlVertex3f(a.x, h, a.y);
    rlTexCoord2f(u0, v1); rlVertex3f(a.x, h, a.y + a.height);
    rlTexCoord2f(u1, v1); rlVertex3f(a.x + a.width, h, a.y + a.height);
    rlTexCoord2f(u1, v0); rlVertex3f(a.x + a.width, h, a.y);
    rlEnd();
    rlSetTexture(0);
}

void DrawFlatRect(Rectangle a, float h, Color c) {
    rlBegin(RL_QUADS);
    C(c); rlNormal3f(0, 1, 0);
    rlVertex3f(a.x, h, a.y);
    rlVertex3f(a.x, h, a.y + a.height);
    rlVertex3f(a.x + a.width, h, a.y + a.height);
    rlVertex3f(a.x + a.width, h, a.y);
    rlEnd();
}

void DrawFlatQuad(const Vector2 p[4], float h, Color c) {
    rlBegin(RL_QUADS);
    C(c); rlNormal3f(0, 1, 0);
    for (int i = 0; i < 4; i++) V(p[i], h);
    rlEnd();
}

void DrawFlatPoly(const Vector2* pts, int n, float h, Color c) {
    if (n < 3) return;
    rlBegin(RL_TRIANGLES);
    C(c);
    for (int i = 1; i < n - 1; i++) { V(pts[0], h); V(pts[i], h); V(pts[i + 1], h); }
    rlEnd();
}

void DrawFlatLine(Vector2 a, Vector2 b, float width, float h, Color c) {
    Vector2 d = Norm(b - a), n = Perp(d) * (width * 0.5f);
    Vector2 q[4] = { a + n, a - n, b - n, b + n };
    DrawFlatQuad(q, h, c);
}

void DrawFlatCircle(Vector2 c, float r, float h, Color col, int seg) {
    rlBegin(RL_TRIANGLES);
    C(col);
    for (int i = 0; i < seg; i++) {
        float a0 = i * 2 * PI / seg, a1 = (i + 1) * 2 * PI / seg;
        V(c, h); V({ c.x + cosf(a0) * r, c.y + sinf(a0) * r }, h); V({ c.x + cosf(a1) * r, c.y + sinf(a1) * r }, h);
    }
    rlEnd();
}

void DrawFlatRing(Vector2 c, float r0, float r1, float h, Color col, int seg) {
    rlBegin(RL_QUADS);
    C(col);
    for (int i = 0; i < seg; i++) {
        float a0 = i * 2 * PI / seg, a1 = (i + 1) * 2 * PI / seg;
        V({ c.x + cosf(a0) * r0, c.y + sinf(a0) * r0 }, h); V({ c.x + cosf(a0) * r1, c.y + sinf(a0) * r1 }, h);
        V({ c.x + cosf(a1) * r1, c.y + sinf(a1) * r1 }, h); V({ c.x + cosf(a1) * r0, c.y + sinf(a1) * r0 }, h);
    }
    rlEnd();
}

void DrawWall(Texture2D tex, Vector2 a, Vector2 b, float h0, float h1, Vector4 uv, Color bottom, Color top) {
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    C(top);    rlTexCoord2f(uv.x, uv.y); V(a, h1);
    C(bottom); rlTexCoord2f(uv.x, uv.w); V(a, h0);
    C(bottom); rlTexCoord2f(uv.z, uv.w); V(b, h0);
    C(top);    rlTexCoord2f(uv.z, uv.y); V(b, h1);
    rlEnd();
    rlSetTexture(0);
}

void DrawBox(Rectangle r, float h0, float h1, Color col, Vector2 sunDir, float ambientMul) {
    Vector2 p[4] = { { r.x, r.y }, { r.x + r.width, r.y }, { r.x + r.width, r.y + r.height }, { r.x, r.y + r.height } };
    Vector2 n[4] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    rlBegin(RL_QUADS);
    for (int i = 0; i < 4; i++) {
        float shade = (0.62f + 0.38f * Saturate(-Dot(n[i], sunDir))) * ambientMul;
        Color c = ColorMul(col, shade);
        Vector2 a = p[i], b = p[(i + 1) % 4];
        C(c); V(a, h1); C(ColorMul(c, 0.8f)); V(a, h0); V(b, h0); C(c); V(b, h1);
    }
    C(ColorMul(col, ambientMul));
    for (int i = 0; i < 4; i++) V(p[i], h1);
    rlEnd();
}

void SetDepthWrite(bool on) { rlDrawRenderBatchActive(); if (on) rlEnableDepthMask(); else rlDisableDepthMask(); }
void SetDepthTest(bool on)  { rlDrawRenderBatchActive(); if (on) rlEnableDepthTest(); else rlDisableDepthTest(); }

// -------------------------------------------------------------------------------------
//  Camera
// -------------------------------------------------------------------------------------
// GTA2-style camera: stays at least CAM_MIN_HEIGHT above the ground (well above the
// tallest roof) and widens its field of view (up to CAM_FOVY) when zoomed out. Close up
// the lens gets longer instead of the camera dropping into the buildings.
float CameraRig::Height() const {
    return std::max(cfg::CAM_MIN_HEIGHT, (viewH * 0.5f) / tanf(cfg::CAM_FOVY * 0.5f * DEG2RAD));
}

void CameraRig::Update(Vector2 target, float targetView, float dt) {
    pos = LerpV(pos, target, Damp(5.0f, dt));
    viewH = Lerpf(viewH, targetView, Damp(1.6f, dt));
    shake = std::max(0.0f, shake - dt * 1.5f);
    float s = shake * shake * 22.0f;
    shakeOfs = { GRng().Range(-s, s), GRng().Range(-s, s) };
}

Camera3D CameraRig::Get() const {
    Camera3D c{};
    Vector2 p = pos + shakeOfs;
    c.position = { p.x, Height(), p.y };
    c.target = { p.x, 0.0f, p.y };
    c.up = { 0.0f, 0.0f, -1.0f };          // screen up == world north (-y)
    c.fovy = 2.0f * atanf(viewH * 0.5f / Height()) * RAD2DEG;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

Vector2 CameraRig::ScreenToGround(Vector2 screen, float h) const {
    Ray ray = GetScreenToWorldRay(screen, Get());
    if (fabsf(ray.direction.y) < 1e-5f) return pos;
    float t = (h - ray.position.y) / ray.direction.y;
    return { ray.position.x + ray.direction.x * t, ray.position.z + ray.direction.z * t };
}

Vector2 CameraRig::WorldToScreen(Vector2 p, float h) const { return GetWorldToScreen(W3(p, h), Get()); }

Rectangle CameraRig::VisibleGround(float margin) const {
    float halfH = viewH * 0.5f;
    float halfW = halfH * (float)GetScreenWidth() / std::max(1, GetScreenHeight());
    return { pos.x - halfW - margin, pos.y - halfH - margin, (halfW + margin) * 2, (halfH + margin) * 2 };
}

// -------------------------------------------------------------------------------------
//  Render targets
// -------------------------------------------------------------------------------------
RenderTexture2D Renderer::MakeSharedDepthTarget(int width, int height) {
    RenderTexture2D t{};
    t.id = rlLoadFramebuffer();
    rlEnableFramebuffer(t.id);
    t.texture.id = rlLoadTexture(nullptr, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    t.texture.width = width; t.texture.height = height;
    t.texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8; t.texture.mipmaps = 1;
    rlFramebufferAttach(t.id, t.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(t.id, sharedDepth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
    if (!rlFramebufferComplete(t.id)) TraceLog(LOG_WARNING, "RENDER: shared-depth framebuffer incomplete");
    rlDisableFramebuffer();
    t.depth.id = sharedDepth; t.depth.width = width; t.depth.height = height;
    SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
    return t;
}

void Renderer::UnloadSharedTarget(RenderTexture2D& t) {
    if (!t.id) return;
    // detach the shared depth buffer first: rlUnloadFramebuffer deletes attachments
    rlEnableFramebuffer(t.id);
    rlFramebufferAttach(t.id, 0, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
    rlDisableFramebuffer();
    rlUnloadTexture(t.texture.id);
    rlUnloadFramebuffer(t.id);
    t = {};
}

void Renderer::Init(int width, int height) {
    w = width; h = height;
    scene = LoadRenderTexture(w, h);
    SetTextureFilter(scene.texture, TEXTURE_FILTER_BILINEAR);
    sharedDepth = scene.depth.id;
    light = MakeSharedDepthTarget(w, h);
    emissive = MakeSharedDepthTarget(w, h);
    shadow = LoadRenderTexture(w, h);
    bloomA = LoadRenderTexture(std::max(1, w / 4), std::max(1, h / 4));
    bloomB = LoadRenderTexture(std::max(1, w / 4), std::max(1, h / 4));
    SetTextureFilter(bloomA.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(bloomB.texture, TEXTURE_FILTER_BILINEAR);
    rlSetClipPlanes(8.0, 12000.0);   // good depth precision for a camera 800..3000 px up
}

void Renderer::Unload() {
    UnloadSharedTarget(light);
    UnloadSharedTarget(emissive);
    if (scene.id) UnloadRenderTexture(scene);
    if (shadow.id) UnloadRenderTexture(shadow);
    if (bloomA.id) UnloadRenderTexture(bloomA);
    if (bloomB.id) UnloadRenderTexture(bloomB);
    scene = shadow = bloomA = bloomB = {};
}

void Renderer::EnsureSize(int width, int height) {
    if (width == w && height == h) return;
    if (width <= 0 || height <= 0) return;
    Unload();
    Init(width, height);
}

void Renderer::ClearColorOnly(Color c) {
    // ClearBackground() would also wipe the shared depth buffer -> draw a full-screen quad
    rlDisableDepthTest();
    DrawRectangle(0, 0, w, h, c);
    rlDrawRenderBatchActive();
}

void Renderer::BeginShadowPass(const Camera3D& cam) {
    BeginTextureMode(shadow);
    ClearBackground(BLANK);
    BeginMode3D(cam);
    SetDepthTest(false);
    rlDisableBackfaceCulling();
}
void Renderer::EndShadowPass() { EndMode3D(); EndTextureMode(); }

void Renderer::BeginScenePass(const Camera3D& cam, Color clear) {
    BeginTextureMode(scene);
    ClearBackground(clear);            // clears colour + the shared depth buffer
    BeginMode3D(cam);
    rlDisableBackfaceCulling();
}

void Renderer::OverlayShadows(const Camera3D& cam, float alpha) {
    EndMode3D();
    DrawTextureRec(shadow.texture, { 0, 0, (float)w, -(float)h }, { 0, 0 }, ColorA(WHITE, alpha));
    BeginMode3D(cam);
    rlDisableBackfaceCulling();
}

void Renderer::EndScenePass() { EndMode3D(); EndTextureMode(); }

void Renderer::BeginLightPass(const Camera3D& cam, Color ambient) {
    BeginTextureMode(light);
    ClearColorOnly(ambient);
    BeginMode3D(cam);
    rlDisableBackfaceCulling();
    SetDepthWrite(false);              // test against the scene depth, never write it
    BeginBlendMode(BLEND_ADDITIVE);
}
void Renderer::EndLightPass() { EndBlendMode(); SetDepthWrite(true); EndMode3D(); EndTextureMode(); }

void Renderer::BeginEmissivePass(const Camera3D& cam) {
    BeginTextureMode(emissive);
    ClearColorOnly(BLACK);
    BeginMode3D(cam);
    rlDisableBackfaceCulling();
    SetDepthWrite(false);
    BeginBlendMode(BLEND_ADDITIVE);
}
void Renderer::EndEmissivePass() { EndBlendMode(); SetDepthWrite(true); EndMode3D(); EndTextureMode(); }

void Renderer::Composite(float time, float bloomStrength) {
    // ---- bloom: downsample emissive, blur H/V twice ----
    float bw = (float)bloomA.texture.width, bh = (float)bloomA.texture.height;
    BeginTextureMode(bloomA);
    ClearBackground(BLACK);
    DrawTexturePro(emissive.texture, { 0, 0, (float)w, -(float)h }, { 0, 0, bw, bh }, { 0, 0 }, 0, WHITE);
    EndTextureMode();
    for (int pass = 0; pass < 2; pass++) {
        Vector2 dh = { 1.0f / bw, 0.0f }, dv = { 0.0f, 1.0f / bh };
        BeginTextureMode(bloomB);
        BeginShaderMode(gAssets.blur);
        SetShaderValue(gAssets.blur, gAssets.locBlurDir, &dh, SHADER_UNIFORM_VEC2);
        DrawTextureRec(bloomA.texture, { 0, 0, bw, -bh }, { 0, 0 }, WHITE);
        EndShaderMode();
        EndTextureMode();
        BeginTextureMode(bloomA);
        BeginShaderMode(gAssets.blur);
        SetShaderValue(gAssets.blur, gAssets.locBlurDir, &dv, SHADER_UNIFORM_VEC2);
        DrawTextureRec(bloomB.texture, { 0, 0, bw, -bh }, { 0, 0 }, WHITE);
        EndShaderMode();
        EndTextureMode();
    }
    // ---- final composite ----
    BeginShaderMode(gAssets.composite);
    SetShaderValueTexture(gAssets.composite, gAssets.locLight, light.texture);
    SetShaderValueTexture(gAssets.composite, gAssets.locEmissive, emissive.texture);
    SetShaderValueTexture(gAssets.composite, gAssets.locBloom, bloomA.texture);
    SetShaderValue(gAssets.composite, gAssets.locBloomStr, &bloomStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gAssets.composite, gAssets.locTime, &time, SHADER_UNIFORM_FLOAT);
    DrawTextureRec(scene.texture, { 0, 0, (float)w, -(float)h }, { 0, 0 }, WHITE);
    EndShaderMode();
}

// -------------------------------------------------------------------------------------
//  Light primitives
// -------------------------------------------------------------------------------------
void Renderer::Radial(Vector2 pos, float h, float radius, Color col, float intensity) {
    const Texture2D& t = gAssets.lightRadial;
    Rectangle src = { 0, 0, (float)t.width, (float)t.height };
    while (intensity > 0.001f) {
        float a = std::min(intensity, 1.0f);
        DrawFlatSprite(t, src, pos, h, radius * 2, radius * 2, 0, ColorA(col, a * col.a / 255.0f));
        intensity -= 1.0f;
    }
}

void Renderer::Cone(Vector2 apex, float h, float angle, float length, float width, Color col, float intensity) {
    const Texture2D& t = gAssets.lightCone;
    Rectangle src = { 0, 0, (float)t.width, (float)t.height };
    while (intensity > 0.001f) {
        float a = std::min(intensity, 1.0f);
        DrawFlatSprite(t, src, apex, h, width, length, angle, ColorA(col, a), { 0.5f, 1.0f });
        intensity -= 1.0f;
    }
}

void Renderer::Flare(Vector2 pos, float h, float size, Color col, float intensity) {
    const Texture2D& t = gAssets.flare;
    DrawFlatSprite(t, { 0, 0, (float)t.width, (float)t.height }, pos, h, size, size, 0, ColorA(col, Saturate(intensity)));
}
