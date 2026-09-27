// =====================================================================================
//  Particles - see particles.h
// =====================================================================================
#include "particles.h"
#include "assets.h"
#include "render.h"
#include "rlgl.h"

static const int MAX_PARTICLES = 6000;
static const int MAX_SKIDS = 6000;
static const int MAX_DECALS = 600;

Particles::Particles() {
    parts.reserve(MAX_PARTICLES);
    skids.reserve(MAX_SKIDS);
    decals.reserve(MAX_DECALS);
}

void Particles::Clear() { parts.clear(); skids.clear(); decals.clear(); flashes.clear(); tracers.clear(); skidHead = decalHead = 0; }

void Particles::Add(const Particle& p) {
    if ((int)parts.size() >= MAX_PARTICLES) return;
    parts.push_back(p);
}

// -------------------------------------------------------------------------------------
//  Update
// -------------------------------------------------------------------------------------
void Particles::Update(float dt) {
    for (size_t i = 0; i < parts.size();) {
        Particle& p = parts[i];
        p.life -= dt;
        if (p.life <= 0) { parts[i] = parts.back(); parts.pop_back(); continue; }
        p.pos = p.pos + p.vel * dt;
        float k = expf(-p.drag * dt);
        p.vel = p.vel * k;
        p.h += p.vh * dt;
        if (p.gravity) {
            p.vh -= 900.0f * dt;
            if (p.h < 0.5f) {                       // bounce & settle on the ground
                p.h = 0.5f; p.vh = -p.vh * 0.3f;
                p.vel = p.vel * 0.55f; p.rotVel *= 0.5f;
                if (fabsf(p.vh) < 30) p.vh = 0;
            }
        } else {
            p.vh *= k;
        }
        p.rot += p.rotVel * dt;
        i++;
    }
    for (auto& s : skids) s.life -= dt;
    for (auto& d : decals) d.life -= dt;
    for (size_t i = 0; i < flashes.size();) {
        flashes[i].life -= dt;
        if (flashes[i].life <= 0) { flashes[i] = flashes.back(); flashes.pop_back(); } else i++;
    }
    for (size_t i = 0; i < tracers.size();) {
        tracers[i].life -= dt;
        if (tracers[i].life <= 0) { tracers[i] = tracers.back(); tracers.pop_back(); } else i++;
    }
}

// -------------------------------------------------------------------------------------
//  Emitters
// -------------------------------------------------------------------------------------
void Particles::Smoke(Vector2 pos, float h, Vector2 vel, float size, Color c, float life) {
    Particle p;
    p.pos = pos; p.vel = vel; p.h = h; p.vh = GRng().Range(15, 40);
    p.life = p.maxLife = life;
    p.size0 = size * 0.5f; p.size1 = size * 1.8f;
    p.rot = GRng().Range(0, 2 * PI); p.rotVel = GRng().Range(-1, 1);
    p.drag = 1.4f; p.c0 = c; p.c1 = ColorA(c, 0); p.type = PType::Smoke;
    Add(p);
}

void Particles::TyreSmoke(Vector2 pos, Vector2 carVel, float amount) {
    Rng& r = GRng();
    Particle p;
    p.pos = pos + V2(r.Range(-3, 3), r.Range(-3, 3));
    p.vel = carVel * 0.12f + V2(r.Range(-25, 25), r.Range(-25, 25));
    p.h = 2; p.vh = r.Range(8, 22);
    p.life = p.maxLife = r.Range(0.9f, 1.8f);
    p.size0 = 10 + 6 * amount; p.size1 = 40 + 30 * amount;
    p.rot = r.Range(0, 2 * PI); p.rotVel = r.Range(-1.5f, 1.5f);
    p.drag = 2.2f;
    unsigned char g = (unsigned char)r.Int(200, 235);
    p.c0 = { g, g, g, (unsigned char)(80 + 70 * Saturate(amount)) }; p.c1 = { g, g, g, 0 };
    p.type = PType::Smoke;
    Add(p);
}

void Particles::Dust(Vector2 pos, Vector2 vel, Color c) {
    Rng& r = GRng();
    Particle p;
    p.pos = pos; p.vel = vel * 0.2f + V2(r.Range(-20, 20), r.Range(-20, 20));
    p.h = 1; p.vh = r.Range(5, 15);
    p.life = p.maxLife = r.Range(0.6f, 1.2f);
    p.size0 = 10; p.size1 = 34; p.drag = 2.5f;
    p.c0 = ColorA(c, 0.5f); p.c1 = ColorA(c, 0); p.type = PType::Dust;
    Add(p);
}

void Particles::Sparks(Vector2 pos, float h, Vector2 dir, int n, float speed) {
    Rng& r = GRng();
    for (int i = 0; i < n; i++) {
        Particle p;
        float a = AngleOf(dir) + r.Range(-0.9f, 0.9f);
        p.pos = pos; p.vel = Forward(a) * r.Range(speed * 0.3f, speed);
        p.h = h; p.vh = r.Range(40, 220); p.gravity = true;
        p.life = p.maxLife = r.Range(0.15f, 0.45f);
        p.size0 = r.Range(5, 9); p.size1 = 2; p.drag = 3.0f;
        p.c0 = { 255, 230, 150, 255 }; p.c1 = { 255, 120, 30, 0 };
        p.type = PType::Spark;
        Add(p);
    }
}

void Particles::Debris(Vector2 pos, float h, Color c, int n, float speed) {
    Rng& r = GRng();
    for (int i = 0; i < n; i++) {
        Particle p;
        p.pos = pos + V2(r.Range(-8, 8), r.Range(-8, 8));
        p.vel = Forward(r.Range(0, 2 * PI)) * r.Range(speed * 0.2f, speed);
        p.h = h; p.vh = r.Range(80, 380); p.gravity = true;
        p.life = p.maxLife = r.Range(2.0f, 5.0f);
        p.size0 = p.size1 = r.Range(3, 8);
        p.rot = r.Range(0, 2 * PI); p.rotVel = r.Range(-12, 12);
        p.drag = 0.6f;
        p.c0 = ColorMul(c, r.Range(0.5f, 1.0f)); p.c1 = p.c0;
        p.type = PType::Debris;
        Add(p);
    }
}

void Particles::GlassShards(Vector2 pos, int n) {
    Rng& r = GRng();
    for (int i = 0; i < n; i++) {
        Particle p;
        p.pos = pos; p.vel = Forward(r.Range(0, 2 * PI)) * r.Range(40, 220);
        p.h = 18; p.vh = r.Range(40, 200); p.gravity = true;
        p.life = p.maxLife = r.Range(3.0f, 6.0f);
        p.size0 = p.size1 = r.Range(1.5f, 3.0f);
        p.rot = r.Range(0, 6); p.rotVel = r.Range(-10, 10); p.drag = 1.2f;
        p.c0 = p.c1 = { 190, 220, 235, 220 };
        p.type = PType::Glass;
        Add(p);
    }
}

void Particles::FirePuff(Vector2 pos, float h, float size) {
    Rng& r = GRng();
    Particle p;
    p.pos = pos + V2(r.Range(-size * 0.3f, size * 0.3f), r.Range(-size * 0.3f, size * 0.3f));
    p.vel = V2(r.Range(-15, 15), r.Range(-15, 15));
    p.h = h; p.vh = r.Range(30, 70);
    p.life = p.maxLife = r.Range(0.35f, 0.7f);
    p.size0 = size * 0.7f; p.size1 = size * 0.2f;
    p.rot = r.Range(0, 6); p.rotVel = r.Range(-3, 3); p.drag = 1.0f;
    p.c0 = { 255, 220, 140, 255 }; p.c1 = { 220, 60, 10, 0 };
    p.type = PType::Fire;
    Add(p);
}

void Particles::Explosion(Vector2 pos, float s) {
    Rng& r = GRng();
    AddFlash(pos, 40, 560 * s, { 255, 170, 80, 255 }, 2.6f, 0.7f);
    for (int i = 0; i < (int)(26 * s); i++) {                     // fireball
        Particle p;
        p.pos = pos; p.vel = Forward(r.Range(0, 2 * PI)) * r.Range(40, 260) * s;
        p.h = r.Range(10, 40); p.vh = r.Range(40, 160);
        p.life = p.maxLife = r.Range(0.45f, 1.0f);
        p.size0 = r.Range(50, 110) * s; p.size1 = r.Range(20, 50) * s;
        p.rot = r.Range(0, 6); p.rotVel = r.Range(-2, 2); p.drag = 3.0f;
        p.c0 = { 255, 235, 170, 255 }; p.c1 = { 200, 50, 10, 0 };
        p.type = PType::Fire;
        Add(p);
    }
    for (int i = 0; i < (int)(24 * s); i++) {                     // black smoke column
        Particle p;
        p.pos = pos + V2(r.Range(-30, 30), r.Range(-30, 30));
        p.vel = Forward(r.Range(0, 2 * PI)) * r.Range(20, 120) * s;
        p.h = r.Range(10, 50); p.vh = r.Range(30, 90);
        p.life = p.maxLife = r.Range(2.5f, 5.0f);
        p.size0 = r.Range(60, 90) * s; p.size1 = r.Range(160, 240) * s;
        p.rot = r.Range(0, 6); p.rotVel = r.Range(-0.6f, 0.6f); p.drag = 1.2f;
        unsigned char g = (unsigned char)r.Int(25, 55);
        p.c0 = { g, g, g, 220 }; p.c1 = { g, g, g, 0 };
        p.type = PType::Smoke;
        Add(p);
    }
    Particle ring;                                                // shockwave
    ring.pos = pos; ring.h = 3; ring.life = ring.maxLife = 0.4f;
    ring.size0 = 30; ring.size1 = 420 * s; ring.drag = 0;
    ring.c0 = { 255, 200, 140, 200 }; ring.c1 = { 255, 120, 60, 0 };
    ring.type = PType::Ring;
    Add(ring);
    Sparks(pos, 20, { 1, 0 }, 10, 500);
    Sparks(pos, 20, { -1, 0 }, 10, 500);
    Sparks(pos, 20, { 0, 1 }, 10, 500);
    Debris(pos, 20, { 70, 70, 75, 255 }, (int)(18 * s), 420);
    AddDecal(pos, DECAL_SCORCH, 200 * s, r.Range(0, 6), 120.0f);
}

void Particles::BloodSpray(Vector2 pos, Vector2 dir, int n) {
    Rng& r = GRng();
    for (int i = 0; i < n; i++) {
        Particle p;
        float a = AngleOf(dir) + r.Range(-0.7f, 0.7f);
        p.pos = pos; p.vel = Forward(a) * r.Range(40, 180);
        p.h = 20; p.vh = r.Range(20, 120); p.gravity = true;
        p.life = p.maxLife = r.Range(0.4f, 0.9f);
        p.size0 = p.size1 = r.Range(1.5f, 3.5f); p.drag = 2.0f;
        p.c0 = p.c1 = { 120, 10, 14, 230 };
        p.type = PType::Blood;
        Add(p);
    }
}

void Particles::MuzzleFlash(Vector2 pos, float h, float angle, float size) {
    Particle p;
    p.pos = pos; p.h = h; p.life = p.maxLife = 0.06f;
    p.size0 = size; p.size1 = size * 0.6f; p.rot = angle; p.drag = 0;
    p.c0 = { 255, 230, 160, 255 }; p.c1 = { 255, 150, 50, 0 };
    p.type = PType::Fire;
    Add(p);
    AddFlash(pos, h, 170, { 255, 200, 120, 255 }, 1.4f, 0.07f);
}

void Particles::ShellCasing(Vector2 pos, float h, float angle) {
    Rng& r = GRng();
    Particle p;
    p.pos = pos; p.vel = RightOf(angle) * r.Range(60, 120) + Forward(angle) * r.Range(-20, 20);
    p.h = h; p.vh = r.Range(60, 120); p.gravity = true;
    p.life = p.maxLife = r.Range(1.5f, 3.0f);
    p.size0 = p.size1 = 1.6f; p.rot = r.Range(0, 6); p.rotVel = r.Range(-20, 20); p.drag = 2.0f;
    p.c0 = p.c1 = { 210, 170, 70, 255 };
    p.type = PType::Shell;
    Add(p);
}

void Particles::BulletTracer(Vector2 a, Vector2 b, float h) { tracers.push_back({ a, b, h, 0.05f }); }

void Particles::Splash(Vector2 pos, float size) {
    Rng& r = GRng();
    for (int i = 0; i < 26; i++) {
        Particle p;
        p.pos = pos; p.vel = Forward(r.Range(0, 2 * PI)) * r.Range(30, 160) * size;
        p.h = -40; p.vh = r.Range(100, 300); p.gravity = true;
        p.life = p.maxLife = r.Range(0.4f, 0.9f);
        p.size0 = r.Range(4, 9); p.size1 = 2; p.drag = 1.0f;
        p.c0 = { 200, 225, 235, 220 }; p.c1 = { 200, 225, 235, 0 };
        p.type = PType::Water;
        Add(p);
    }
}

void Particles::WaterJet(Vector2 pos, float h, float power, float dt) {
    Rng& r = GRng();
    int n = (int)(dt * 60.0f * power + r.Float());
    for (int i = 0; i < n; i++) {
        Particle p;
        p.pos = pos; p.vel = Forward(r.Range(0, 2 * PI)) * r.Range(10, 45) * power;
        p.h = h; p.vh = r.Range(160, 260) * power; p.gravity = true;
        p.life = p.maxLife = r.Range(0.7f, 1.1f);
        p.size0 = r.Range(3, 6); p.size1 = r.Range(7, 12); p.drag = 0.3f;
        p.c0 = { 215, 235, 245, 170 }; p.c1 = { 215, 235, 245, 0 };
        p.type = PType::Water;
        Add(p);
    }
}

void Particles::AddSkid(Vector2 a, Vector2 b, float width, float alpha) {
    Skid s{ a, b, width, alpha, 45.0f };
    if ((int)skids.size() < MAX_SKIDS) skids.push_back(s);
    else { skids[skidHead] = s; skidHead = (skidHead + 1) % MAX_SKIDS; }
}

void Particles::AddDecal(Vector2 p, int kind, float size, float rot, float life, Color tint) {
    Decal d{ p, size, rot, life, life, kind, tint };
    if ((int)decals.size() < MAX_DECALS) decals.push_back(d);
    else { decals[decalHead] = d; decalHead = (decalHead + 1) % MAX_DECALS; }
}

void Particles::AddFlash(Vector2 p, float h, float radius, Color c, float intensity, float life) {
    flashes.push_back({ p, h, radius, life, life, intensity, c });
}

// -------------------------------------------------------------------------------------
//  Drawing
// -------------------------------------------------------------------------------------
static bool InView(Rectangle v, Vector2 p, float m) {
    return p.x > v.x - m && p.x < v.x + v.width + m && p.y > v.y - m && p.y < v.y + v.height + m;
}

void Particles::DrawDecals(Rectangle view) const {
    // skid marks: thin dark rubber strips
    for (const Skid& s : skids) {
        if (s.life <= 0 || !InView(view, s.a, 40)) continue;
        float a = s.alpha * Saturate(s.life / 12.0f);
        DrawFlatLine(s.a, s.b, s.width, 0.12f, { 18, 18, 18, (unsigned char)(a * 255) });
    }
    for (const Decal& d : decals) {
        if (d.life <= 0 || !InView(view, d.pos, d.size)) continue;
        float a = Saturate(d.life / 10.0f);
        switch (d.kind) {
            case DECAL_BLOOD: {
                const Texture2D& t = gAssets.blood;
                DrawFlatSprite(t, { 0, 0, (float)t.width, (float)t.height }, d.pos, 0.14f, d.size, d.size, d.rot, ColorA(WHITE, a));
            } break;
            case DECAL_SCORCH: {
                const Texture2D& t = gAssets.softCircle;
                DrawFlatSprite(t, { 0, 0, (float)t.width, (float)t.height }, d.pos, 0.13f, d.size, d.size, d.rot, ColorA({ 10, 8, 6, 255 }, 0.85f * a));
            } break;
            case DECAL_OIL: {
                const Texture2D& t = gAssets.softCircle;
                DrawFlatSprite(t, { 0, 0, (float)t.width, (float)t.height }, d.pos, 0.13f, d.size, d.size * 0.7f, d.rot, ColorA({ 12, 10, 14, 255 }, 0.7f * a));
            } break;
            case DECAL_BULLET:
                DrawFlatCircle(d.pos, d.size, 0.16f, ColorA({ 20, 20, 20, 255 }, 0.8f * a), 8);
                break;
        }
    }
}

void Particles::DrawLit(Rectangle view) const {
    const Texture2D& soft = gAssets.softCircle;
    Rectangle ss = { 0, 0, (float)soft.width, (float)soft.height };
    for (const Particle& p : parts) {
        if (!InView(view, p.pos, 200)) continue;
        float t = 1.0f - p.life / p.maxLife;
        float size = Lerpf(p.size0, p.size1, t);
        Color c = LerpColor(p.c0, p.c1, t);
        switch (p.type) {
            case PType::Smoke: case PType::Dust:
                DrawFlatSprite(soft, ss, p.pos, p.h, size, size, p.rot, c);
                break;
            case PType::Debris: case PType::Glass: case PType::Shell: case PType::Blood: {
                Vector2 r = RightOf(p.rot) * (size * 0.5f), f = Forward(p.rot) * (size * 0.35f);
                Vector2 q[4] = { p.pos - r - f, p.pos + r - f, p.pos + r + f, p.pos - r + f };
                DrawFlatQuad(q, p.h, c);
            } break;
            case PType::Water:
                DrawFlatSprite(soft, ss, p.pos, p.h, size, size, 0, c);
                break;
            default: break;
        }
    }
}

void Particles::DrawEmissive(Rectangle view) const {
    const Texture2D& soft = gAssets.lightRadial;
    Rectangle ss = { 0, 0, (float)soft.width, (float)soft.height };
    for (const Particle& p : parts) {
        if (!InView(view, p.pos, 300)) continue;
        float t = 1.0f - p.life / p.maxLife;
        float size = Lerpf(p.size0, p.size1, t);
        Color c = LerpColor(p.c0, p.c1, t);
        switch (p.type) {
            case PType::Fire:
                DrawFlatSprite(soft, ss, p.pos, p.h, size, size, p.rot, c);
                break;
            case PType::Spark: {
                const Texture2D& sp = gAssets.spark;
                float len = size + Len(p.vel) * 0.03f;
                DrawFlatSprite(sp, { 0, 0, (float)sp.width, (float)sp.height }, p.pos, p.h, len * 0.3f, len,
                               AngleOf(p.vel) , c);
            } break;
            case PType::Ring: {
                const Texture2D& rg = gAssets.ring;
                DrawFlatSprite(rg, { 0, 0, (float)rg.width, (float)rg.height }, p.pos, p.h, size, size, 0, c);
            } break;
            default: break;
        }
    }
    for (const Tracer& tr : tracers) DrawFlatLine(tr.a, tr.b, 1.6f, tr.h, { 255, 230, 170, 200 });
}

void Particles::DrawLights(Rectangle view) const {
    for (const Flash& f : flashes) {
        if (!InView(view, f.pos, f.radius)) continue;
        float k = f.life / f.maxLife;
        Renderer::Radial(f.pos, 0.5f, f.radius * (0.6f + 0.4f * k), f.col, f.intensity * k * k);
    }
}
