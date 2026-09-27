// =====================================================================================
//  Particles, tyre skid marks, ground decals and short-lived flash lights.
//  Particles live in 3D (ground position + height) so smoke rises, debris flies in an
//  arc and bounces, and everything gets the same perspective as the rest of the world.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "math_utils.h"
#include <vector>

enum class PType : uint8_t { Smoke, Fire, Spark, Debris, Dust, Blood, Glass, Ring, Water, Shell };

struct Particle {
    Vector2 pos{}, vel{};
    float   h = 0, vh = 0;            // height and vertical speed
    float   life = 0, maxLife = 1;
    float   size0 = 4, size1 = 4;
    float   rot = 0, rotVel = 0;
    float   drag = 1.0f;
    Color   c0 = WHITE, c1 = WHITE;
    PType   type = PType::Smoke;
    bool    gravity = false;
};

struct Skid   { Vector2 a, b; float width, alpha, life; };
struct Decal  { Vector2 pos; float size, rot, life, maxLife; int kind; Color tint; };
struct Flash  { Vector2 pos; float h, radius, life, maxLife, intensity; Color col; };
struct Tracer { Vector2 a, b; float h, life; };

enum DecalKind { DECAL_BLOOD = 0, DECAL_SCORCH, DECAL_OIL, DECAL_BULLET };

class Particles {
public:
    Particles();
    void Update(float dt);
    void Clear();

    // ---- emitters ----
    void Smoke(Vector2 p, float h, Vector2 vel, float size, Color c, float life);
    void TyreSmoke(Vector2 p, Vector2 carVel, float amount);
    void Dust(Vector2 p, Vector2 vel, Color c);
    void Sparks(Vector2 p, float h, Vector2 dir, int n, float speed = 350);
    void Debris(Vector2 p, float h, Color c, int n, float speed);
    void GlassShards(Vector2 p, int n);
    void Explosion(Vector2 p, float scale = 1.0f);
    void FirePuff(Vector2 p, float h, float size);
    void BloodSpray(Vector2 p, Vector2 dir, int n);
    void MuzzleFlash(Vector2 p, float h, float angle, float size);
    void ShellCasing(Vector2 p, float h, float angle);
    void BulletTracer(Vector2 a, Vector2 b, float h);
    void Splash(Vector2 p, float size);
    void WaterJet(Vector2 p, float h, float power, float dt);   // continuous emitter (fountains, hydrants)
    void AddSkid(Vector2 a, Vector2 b, float width, float alpha);
    void AddDecal(Vector2 p, int kind, float size, float rot, float life = 90.0f, Color tint = WHITE);
    void AddFlash(Vector2 p, float h, float radius, Color c, float intensity, float life);

    // ---- drawing (inside the relevant render pass) ----
    void DrawDecals(Rectangle view) const;     // scene pass, on the ground (skids, blood, scorch)
    void DrawLit(Rectangle view) const;        // scene pass, after sprites (smoke, debris)
    void DrawEmissive(Rectangle view) const;   // emissive pass (fire, sparks, tracers, flashes)
    void DrawLights(Rectangle view) const;     // light pass (flash lights, fire glow)

    int Count() const { return (int)parts.size(); }

private:
    std::vector<Particle> parts;
    std::vector<Skid>     skids;   int skidHead = 0;
    std::vector<Decal>    decals;  int decalHead = 0;
    std::vector<Flash>    flashes;
    std::vector<Tracer>   tracers;
    void Add(const Particle& p);
};
