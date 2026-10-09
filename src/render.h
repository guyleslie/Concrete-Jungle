// =====================================================================================
//  3D renderer core (GTA1/2-style "fake top-down 3D").
//
//  The game logic lives on a 2D ground plane (x, y). The renderer maps a ground point
//  p at height h to 3D as (p.x, h, p.y) and looks straight down through a perspective
//  camera. Buildings, bridges and the elevated railway are real 3D boxes; cars,
//  people, trees and props are flat textured quads placed at their real height. The
//  perspective makes tall things lean away from the screen centre and the depth
//  buffer makes bridges/underpasses hide what is under them - exactly the GTA2 look.
//
//  Frame pipeline (see Renderer):
//    1. shadow mask   : sun shadows of everything, drawn flat on the ground (black)
//    2. scene         : albedo + depth (ground, decals, shadow overlay, boxes, sprites)
//    3. light buffer  : ambient + additive lights; shares the scene depth buffer so
//                       ground lights are hidden under roofs/bridges automatically
//    4. emissive      : self-lit things (windows, bulbs, fire...), also depth-tested
//    5. bloom         : emissive, downsampled + gaussian blurred
//    6. composite     : albedo * light + emissive + bloom, tone map + vignette
// =====================================================================================
#pragma once
#include "raylib.h"
#include "math_utils.h"
namespace startup { class Reporter; }

inline Vector3 W3(Vector2 p, float h) { return { p.x, h, p.y }; }

// ---- Immediate-mode drawing helpers (valid inside BeginMode3D) ---------------------

// Flat sprite whose texture "up" points along Forward(angle).
// origin: (0.5,0.5) = centre; (0.5,1.0) = middle of the rear edge, etc.
void DrawFlatSprite(Texture2D tex, Rectangle src, Vector2 pos, float h, float width, float length,
                    float angle, Color tint, Vector2 origin = { 0.5f, 0.5f });
// Flat sprite whose texture +X points along Forward(angle) (Survivor frames face east).
// 'pivot' in source pixels, 'scale' world px per source px.
void DrawFlatSpriteX(Texture2D tex, Rectangle src, Vector2 pos, float h, float scale, Vector2 pivot, float angle, Color tint);
// Axis aligned textured ground quad; texture repeats every 'worldPerTex' px (world-anchored UVs).
void DrawGroundTex(Texture2D tex, Rectangle area, float h, float worldPerTex, Color tint, Vector2 uvOffset = { 0, 0 });
// Untextured primitives on a horizontal plane.
void DrawFlatRect(Rectangle r, float h, Color c);
void DrawFlatQuad(const Vector2 p[4], float h, Color c);
void DrawFlatPoly(const Vector2* pts, int n, float h, Color c);      // convex, any winding
void DrawFlatLine(Vector2 a, Vector2 b, float width, float h, Color c);
void DrawFlatCircle(Vector2 c, float r, float h, Color col, int segments = 24);
void DrawFlatRing(Vector2 c, float r0, float r1, float h, Color col, int segments = 32);
// Vertical textured wall from ground segment a->b between heights h0..h1.
// uv = {u0, v0(top), u1, v1(bottom)}; colours are vertex tints at the bottom / top.
void DrawWall(Texture2D tex, Vector2 a, Vector2 b, float h0, float h1, Vector4 uv, Color bottom, Color top);
// Solid box (4 walls + lid) with a flat colour per face, lit from the sun direction.
void DrawBox(Rectangle r, float h0, float h1, Color col, Vector2 sunDir, float ambientMul = 1.0f);
// Depth-write toggle that flushes the batch first (rlgl doesn't).
void SetDepthWrite(bool on);
void SetDepthTest(bool on);

// ---- Camera ------------------------------------------------------------------------
struct CameraRig {
    Vector2 pos{ 0, 0 };        // ground point under the camera
    float   viewH = 900.0f;     // visible world height at ground level
    Vector2 shakeOfs{ 0, 0 };
    float   shake = 0.0f;       // trauma 0..1

    void     Snap(Vector2 target, float view) { pos = target; viewH = view; }
    void     Update(Vector2 target, float targetView, float dt);
    void     AddShake(float amount) { shake = Clampf(shake + amount, 0.0f, 1.0f); }
    float    Height() const;                          // camera height above ground
    Camera3D Get() const;
    Vector2  ScreenToGround(Vector2 screen, float h) const;
    Vector2  WorldToScreen(Vector2 p, float h) const;
    Rectangle VisibleGround(float margin) const;      // ground rect covered by the view
};

// ---- Render targets & passes -------------------------------------------------------
class Renderer {
public:
    int w = 0, h = 0;
    RenderTexture2D scene{}, light{}, emissive{}, shadow{}, bloomA{}, bloomB{};

    bool Init(int width, int height, startup::Reporter* loading = nullptr);
    bool Ready() const;
    void Unload();
    void EnsureSize(int width, int height);

    void BeginShadowPass(const Camera3D& cam);
    void EndShadowPass();
    void BeginScenePass(const Camera3D& cam, Color clear);
    void OverlayShadows(const Camera3D& cam, float alpha);   // call inside the scene pass
    void EndScenePass();
    void BeginLightPass(const Camera3D& cam, Color ambient);  // additive, depth-tested
    void EndLightPass();
    void BeginEmissivePass(const Camera3D& cam);             // additive, depth-tested
    void EndEmissivePass();
    void Composite(float time, float bloomStrength);          // to the current target (screen)

    // Light primitives (use inside the light or emissive pass)
    static void Radial(Vector2 pos, float h, float radius, Color col, float intensity);
    static void Cone(Vector2 apex, float h, float angle, float length, float width, Color col, float intensity);
    static void Flare(Vector2 pos, float h, float size, Color col, float intensity);

private:
    unsigned int sharedDepth = 0;
    bool targetsValid = false, sharedTargetsValid = true;
    RenderTexture2D MakeSharedDepthTarget(int width, int height);
    void UnloadSharedTarget(RenderTexture2D& t);
    void ClearColorOnly(Color c);
};
