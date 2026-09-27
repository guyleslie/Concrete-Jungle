// =====================================================================================
//  Pedestrian AI & drawing - see pedestrian.h
// =====================================================================================
#include "pedestrian.h"
#include "game.h"
#include "assets.h"
#include "render.h"

using namespace cfg;

static void CornerSigns(int c, int& sx, int& sy) {
    sx = (c == 1 || c == 2) ? 1 : -1;
    sy = (c >= 2) ? 1 : -1;
}
static int CornerFromSigns(int sx, int sy) {
    if (sy < 0) return sx < 0 ? 0 : 1;
    return sx > 0 ? 2 : 3;
}

// Nearest point on the sidewalk ring of block (bi,bj).
static Vector2 NearestOnRing(const CityMap& map, int bi, int bj, Vector2 p, int* cornerOut, int dirSign) {
    Rectangle r = map.SidewalkRing(bi, bj);
    Vector2 c[4] = { { r.x, r.y }, { r.x + r.width, r.y }, { r.x + r.width, r.y + r.height }, { r.x, r.y + r.height } };
    float best = 1e9f; Vector2 bp = c[0]; int bestSeg = 0;
    for (int s = 0; s < 4; s++) {
        Vector2 a = c[s], b = c[(s + 1) % 4];
        Vector2 ab = b - a;
        float t = Clampf(Dot(p - a, ab) / Dot(ab, ab), 0, 1);
        Vector2 q = a + ab * t;
        float d = Dist(q, p);
        if (d < best) { best = d; bp = q; bestSeg = s; }
    }
    // walking clockwise along segment s leads to corner s+1, counter-clockwise to corner s
    if (cornerOut) *cornerOut = dirSign > 0 ? (bestSeg + 1) % 4 : bestSeg;
    return bp;
}

// Sidewalk corner shifted towards/away from the block by the ped's personal lane, so
// people don't all walk on one line.
static Vector2 CornerTarget(const CityMap& map, int bi, int bj, int c, float off) {
    Vector2 corner = map.SidewalkCorner(bi, bj, c);
    Rectangle r = map.SidewalkRing(bi, bj);
    Vector2 centre = { r.x + r.width * 0.5f, r.y + r.height * 0.5f };
    int sx = (c == 1 || c == 2) ? 1 : -1, sy = (c >= 2) ? 1 : -1;
    (void)centre;
    return corner + V2(-sx * off, -sy * off);
}

void InitPed(Pedestrian& p, Vector2 pos, int skin, const CityMap& map) {
    Rng& r = GRng();
    p = Pedestrian{};
    p.active = true;
    p.skin = skin;
    p.walkSpeed = r.Range(19.0f, 27.0f);
    p.laneOffset = r.Range(-9.0f, 9.0f);
    p.bi = std::clamp((int)(pos.x / (BLOCK_PITCH * TILE)), 0, BLOCKS_X - 1);
    p.bj = std::clamp((int)(pos.y / (BLOCK_PITCH * TILE)), 0, BLOCKS_Y - 1);
    p.dirSign = r.Chance(0.5f) ? 1 : -1;
    p.pos = NearestOnRing(map, p.bi, p.bj, pos, &p.corner, p.dirSign);
    p.target = CornerTarget(map, p.bi, p.bj, p.corner, p.laneOffset);
    p.angle = AngleOf(p.target - p.pos);
    p.state = PedState::Walk;
    p.anim = r.Range(0, 8);
}

void ScarePed(Pedestrian& p, Vector2 from, float duration) {
    if (p.state == PedState::Down || p.state == PedState::Dead) return;
    p.state = PedState::Flee;
    p.threat = from;
    p.timer = duration * GRng().Range(0.8f, 1.2f);
}

void KnockDownPed(Pedestrian& p, Vector2 impulse) {
    if (p.state == PedState::Dead) return;
    p.state = PedState::Down;
    p.vel = impulse;
    p.timer = GRng().Range(2.5f, 4.0f);
    if (Len(impulse) > 1) p.angle = AngleOf(impulse * -1.0f);
}

// -------------------------------------------------------------------------------------
//  Update
// -------------------------------------------------------------------------------------
static bool MoveTowards(Pedestrian& p, Vector2 target, float speed, float dt) {
    Vector2 d = target - p.pos;
    float len = Len(d);
    if (len < 3.0f) { p.vel = { 0, 0 }; return true; }
    Vector2 dir = d / len;
    p.vel = LerpV(p.vel, dir * speed, Damp(10, dt));
    float desired = AngleOf(dir);
    p.angle += WrapAngle(desired - p.angle) * Damp(10, dt);
    return false;
}

static void PickNextLeg(Pedestrian& p, Game& g) {
    const CityMap& map = g.map;
    Rng& r = GRng();
    int sx, sy; CornerSigns(p.corner, sx, sy);
    BlockType bt = map.Block(p.bi, p.bj);
    float u = r.Float();
    if (u < 0.06f) { p.state = PedState::Idle; p.timer = r.Range(1.0f, 4.0f); return; }
    if ((bt == BlockType::Park || bt == BlockType::Plaza) && u < 0.2f) {
        Rectangle in = map.BlockInterior(p.bi, p.bj);
        p.state = PedState::Wander;
        p.target = { r.Range(in.x + 40, in.x + in.width - 40), r.Range(in.y + 40, in.y + in.height - 40) };
        return;
    }
    if (u < 0.45f) {
        // cross the street to the neighbouring block, waiting for the green man
        bool horizontal = r.Chance(0.5f);
        int nbi = p.bi + (horizontal ? sx : 0), nbj = p.bj + (horizontal ? 0 : sy);
        if (nbi >= 0 && nbj >= 0 && nbi < BLOCKS_X && nbj < BLOCKS_Y) {
            p.ci = p.bi + (sx > 0 ? 1 : 0);
            p.cj = p.bj + (sy > 0 ? 1 : 0);
            p.caxis = horizontal ? 1 : 0;       // crossing a N-S road needs the E-W phase
            p.nextBi = nbi; p.nextBj = nbj;
            p.nextCorner = horizontal ? CornerFromSigns(-sx, sy) : CornerFromSigns(sx, -sy);
            p.state = PedState::Wait;
            p.timer = 0;
            return;
        }
    }
    p.corner = (p.corner + p.dirSign + 4) % 4;
    p.target = CornerTarget(map, p.bi, p.bj, p.corner, p.laneOffset);
    p.state = PedState::Walk;
}

void UpdatePed(Pedestrian& p, Game& g, float dt) {
    const CityMap& map = g.map;
    Rng& r = GRng();
    float speed = 0;
    switch (p.state) {
    case PedState::Walk:
        speed = p.walkSpeed;
        if (MoveTowards(p, p.target, speed, dt)) PickNextLeg(p, g);
        break;
    case PedState::Wait: {
        p.vel = p.vel * expf(-8 * dt);
        Vector2 dest = CornerTarget(map, p.nextBi, p.nextBj, p.nextCorner, p.laneOffset);
        p.angle += WrapAngle(AngleOf(dest - p.pos) - p.angle) * Damp(6, dt);
        p.timer += dt;
        bool go = map.SignalState(p.ci, p.cj, p.caxis) == SIG_GREEN && map.GreenTimeLeft(p.ci, p.cj, p.caxis) > 3.0f;
        if (go || p.timer > 25.0f) {
            p.state = PedState::Cross;
            p.target = dest;
            p.bi = p.nextBi; p.bj = p.nextBj; p.corner = p.nextCorner;
        }
    } break;
    case PedState::Cross:
        speed = p.walkSpeed * 1.25f;
        if (MoveTowards(p, p.target, speed, dt)) { p.state = PedState::Walk; PickNextLeg(p, g); }
        break;
    case PedState::Idle:
        p.vel = p.vel * expf(-8 * dt);
        p.timer -= dt;
        if (p.timer <= 0) PickNextLeg(p, g);
        break;
    case PedState::Wander:
        speed = p.walkSpeed * 0.85f;
        if (MoveTowards(p, p.target, speed, dt)) { p.state = PedState::Rejoin; p.timer = r.Range(1.5f, 5.0f); }
        break;
    case PedState::Flee: {
        speed = 5.2f * M * (p.walkSpeed / 24.0f);
        Vector2 away = Norm(p.pos - p.threat);
        if (Len(away) < 0.1f) away = Forward(p.angle);
        // bias fleeing along the sidewalk instead of into walls
        p.vel = LerpV(p.vel, away * speed, Damp(6, dt));
        p.angle += WrapAngle(AngleOf(p.vel) - p.angle) * Damp(12, dt);
        p.timer -= dt;
        if (p.timer <= 0) { p.state = PedState::Rejoin; p.timer = 0; }
    } break;
    case PedState::Rejoin: {
        if (p.timer > 0) { p.timer -= dt; p.vel = p.vel * expf(-8 * dt); break; }
        p.bi = std::clamp((int)(p.pos.x / (BLOCK_PITCH * TILE)), 0, BLOCKS_X - 1);
        p.bj = std::clamp((int)(p.pos.y / (BLOCK_PITCH * TILE)), 0, BLOCKS_Y - 1);
        Vector2 q = NearestOnRing(map, p.bi, p.bj, p.pos, &p.corner, p.dirSign);
        speed = p.walkSpeed;
        if (MoveTowards(p, q, speed, dt) || Dist(p.pos, q) < 6) {
            p.target = CornerTarget(map, p.bi, p.bj, p.corner, p.laneOffset);
            p.state = PedState::Walk;
        }
    } break;
    case PedState::Down:
        p.vel = p.vel * expf(-4.0f * dt);
        p.timer -= dt;
        if (p.health <= 0) { p.state = PedState::Dead; p.deadTime = 0; }
        else if (p.timer <= 0) ScarePed(p, p.pos - Forward(p.angle) * 10.0f, 4.0f);
        break;
    case PedState::Dead:
        p.vel = p.vel * expf(-5.0f * dt);
        p.deadTime += dt;
        break;
    }

    // avoid fast cars heading at us
    if (p.state != PedState::Down && p.state != PedState::Dead && p.state != PedState::Flee) {
        for (const Vehicle& v : g.vehicles) {
            if (!v.active || v.Speed() < 120) continue;
            Vector2 rel = p.pos - v.pos;
            float d2 = Len2(rel);
            if (d2 > 140 * 140) continue;
            if (Dot(Norm(v.vel), Norm(rel)) > 0.7f) { ScarePed(p, v.pos, 2.5f); break; }
        }
    }

    // walk around trunks, lamp posts, bins... instead of pushing into them
    if (p.state != PedState::Dead && p.state != PedState::Down && Len2(p.vel) > 4) {
        static std::vector<int> near;
        Vector2 fwd = Norm(p.vel);
        map.QueryObjects({ p.pos.x - 40, p.pos.y - 40, 80, 80 }, near);
        for (int k : near) {
            const CityObject& o = map.objects[k];
            if (o.walkIn) continue;
            Vector2 rel = o.pos - p.pos;
            float ahead = Dot(rel, fwd);
            float along = o.box ? OBBProjectRadius(o.Box(), fwd) : 0.0f;       // boxes: their real extent
            if (ahead <= 0 || ahead > 34 + along) continue;
            float lat = Cross(fwd, rel);                     // > 0: obstacle on our right
            float clear = (o.box ? OBBProjectRadius(o.Box(), Perp(fwd)) : o.radius) + PED_RADIUS + 3;
            if (fabsf(lat) > clear) continue;
            float push = (clear - fabsf(lat)) / clear;
            Vector2 side = Perp(fwd) * (lat > 0 ? -1.0f : 1.0f);
            p.vel = p.vel + side * (Len(p.vel) * 2.2f * push);
        }
    }
    // personal space: sidestep people ahead, never overlap anyone
    if (p.state != PedState::Dead && p.state != PedState::Down) {
        Vector2 fwd = Len2(p.vel) > 1 ? Norm(p.vel) : Forward(p.angle);
        auto avoid = [&](Vector2 o) {
            Vector2 d = p.pos - o;
            float l2 = Len2(d);
            if (l2 > 30 * 30 || l2 < 0.01f) return;
            float l = sqrtf(l2);
            if (l < PED_RADIUS * 2.1f) p.pos = p.pos + d / l * ((PED_RADIUS * 2.1f - l) * 0.5f);
            if (Dot(o - p.pos, fwd) > 0 && fabsf(Cross(fwd, o - p.pos)) < 10) p.vel = p.vel + RightOf(AngleOf(fwd)) * (18.0f * dt * 8);
        };
        for (const Pedestrian& o : g.peds) if (&o != &p && o.active && o.state != PedState::Dead) avoid(o.pos);
        if (!g.player.inVehicle) avoid(g.player.pos);
    }
    p.pos = p.pos + p.vel * dt;
    // collide with buildings and street furniture
    static std::vector<int> ids;
    Rectangle box = { p.pos.x - 20, p.pos.y - 20, 40, 40 };
    map.QueryBuildings(box, ids);
    for (int k : ids) {
        Vector2 n; float depth;
        if (CircleOBB(p.pos, PED_RADIUS, MakeAABB(map.buildings[k].r), n, depth)) p.pos = p.pos + n * depth;
    }
    map.QueryObjects(box, ids);
    for (int k : ids) {
        const CityObject& o = map.objects[k];
        if (o.walkIn) continue;
        if (o.box) { Vector2 n; float depth; if (CircleOBB(p.pos, PED_RADIUS, o.Box(), n, depth)) p.pos = p.pos + n * depth; continue; }
        Vector2 d = p.pos - o.pos; float l = Len(d), rr = o.radius + PED_RADIUS;
        if (l < rr && l > 0.01f) p.pos = o.pos + d / l * rr;
    }
    p.pos.x = Clampf(p.pos.x, 4, WORLD_W - 4);
    p.pos.y = Clampf(p.pos.y, 4, WORLD_H - 4);
    // wanting to move but not getting anywhere? turn around / take another route
    bool mobile = p.state == PedState::Walk || p.state == PedState::Cross || p.state == PedState::Wander || p.state == PedState::Rejoin || p.state == PedState::Flee;
    if (mobile && Len2(p.vel) > 25 && Len2(p.pos - p.lastPos) < 0.04f * dt * dt * 400) p.stuckT += dt; else p.stuckT = std::max(0.0f, p.stuckT - dt);
    p.lastPos = p.pos;
    if (p.stuckT > 1.2f) {
        p.stuckT = 0;
        p.laneOffset = -p.laneOffset;
        if (p.state == PedState::Walk) { p.dirSign = -p.dirSign; p.corner = (p.corner + p.dirSign + 4) % 4; p.target = CornerTarget(map, p.bi, p.bj, p.corner, p.laneOffset); }
        else if (p.state == PedState::Wander || p.state == PedState::Flee) { p.state = PedState::Rejoin; p.timer = 0; }
        p.pos = p.pos + V2(r.Range(-3, 3), r.Range(-3, 3));
    }
    // stride-matched walk cycle: 8 frames per ~1.3 m (two steps)
    float spd = Len(p.vel);
    p.anim += spd * dt * (8.0f / (1.3f * M)) * (p.state == PedState::Flee ? 0.7f : 1.0f);
    p.sway = sinf(p.anim * PI * 0.25f) * Saturate(spd / 20.0f) * 0.07f;
}

// -------------------------------------------------------------------------------------
//  Drawing
// -------------------------------------------------------------------------------------
void DrawPedShadow(const Pedestrian& p, Vector2 sv) {
    const Texture2D& t = gAssets.softCircle;
    bool lying = p.state == PedState::Down || p.state == PedState::Dead;
    float s = (lying ? 1.8f * M : 0.75f * M) * CHAR_SCALE;
    DrawFlatSprite(t, { 0, 0, (float)t.width, (float)t.height }, p.pos + sv * (lying ? 2.0f : 12.0f), 0, s, s * (lying ? 0.6f : 1.0f), p.angle, ColorA(BLACK, 0.8f));
}

void DrawPed(const Pedestrian& p) {
    if (gAssets.peds.empty()) return;
    const Texture2D& t = gAssets.peds[p.skin % gAssets.peds.size()];
    int frame;
    bool lying = p.state == PedState::Down || p.state == PedState::Dead;
    if (lying) frame = spritegen::PED_FRAME_DOWN;
    else if (Len(p.vel) < 4) frame = spritegen::PED_FRAME_IDLE;
    else frame = (int)p.anim % spritegen::PED_WALK_FRAMES;
    const float F = (float)spritegen::PED_FRAME;
    Rectangle src = { frame * F, 0, F, F };
    float size = 1.3f * M * CHAR_SCALE;   // civilians are drawn at the same visual size as the player
    float h = lying ? 1.5f : H_PED;
    Color tint = p.state == PedState::Dead ? Color{ 200, 190, 190, 255 } : WHITE;
    DrawFlatSprite(t, src, p.pos, h, size * (lying ? 1.9f : 1.0f), size * (lying ? 1.9f : 1.0f), p.angle + (lying ? 0.0f : p.sway), tint);
}
