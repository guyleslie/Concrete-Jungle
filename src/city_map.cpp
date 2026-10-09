// =====================================================================================
//  City map - generation, queries and 3D drawing. See city_map.h.
// =====================================================================================
#include "city_map.h"
#include "assets.h"
#include "render.h"
#include "particles.h"
#include "lighting.h"
#include "rlgl.h"
#include "startup_loading.h"
#include <algorithm>

using namespace cfg;
using spritegen::Prop;

static const float CURB_H = 1.2f;         // sidewalks / block interiors are raised 7.5 cm
static const float LAMP_REACH = 28.0f;    // lamp arm length (px)

// Real-world prop sizes (m) -> px, and the height of their top surface.
struct PropDim { float w, l, h, radius; bool breakable; };
static PropDim Dim(Prop p) {
    switch (p) {
        case Prop::Bench:       return { 1.8f * M, 0.6f * M, 0.5f * M, 0, false };
        case Prop::TrashBin:    return { 0.6f * M, 0.6f * M, 1.0f * M, 0.3f * M, true };
        case Prop::Hydrant:     return { 0.45f * M, 0.45f * M, 0.8f * M, 0.25f * M, true };
        case Prop::Cone:        return { 0.45f * M, 0.45f * M, 0.7f * M, 0.2f * M, true };
        case Prop::Barrel:      return { 0.6f * M, 0.6f * M, 0.9f * M, 0.3f * M, true };
        case Prop::Dumpster:    return { 2.0f * M, 1.2f * M, 1.3f * M, 0.8f * M, false };
        case Prop::ACUnit:      return { 1.6f * M, 1.6f * M, 1.2f * M, 0, false };
        case Prop::LampHead:    return { 1.4f * M, 0.55f * M, 0, 0, false };
        case Prop::SignalHead:  return { 0.45f * M, 1.0f * M, 0, 0, false };
        case Prop::Planter:     return { 1.2f * M, 1.2f * M, 0.6f * M, 0.6f * M, false };
        case Prop::BusShelter:  return { 4.0f * M, 1.6f * M, 2.6f * M, 0, false };
        case Prop::PhoneBooth:  return { 1.0f * M, 1.0f * M, 2.3f * M, 0.55f * M, false };
        case Prop::ParasolRed: case Prop::ParasolBlue: case Prop::ParasolGreen:
                                return { 2.4f * M, 2.4f * M, 2.3f * M, 0, false };
        case Prop::Bollard:     return { 0.25f * M, 0.25f * M, 0.9f * M, 0.15f * M, true };
        case Prop::Mailbox:     return { 0.5f * M, 0.4f * M, 1.3f * M, 0.3f * M, true };
        case Prop::NewsBox:     return { 0.5f * M, 0.45f * M, 1.1f * M, 0.3f * M, true };
        case Prop::Manhole:     return { 0.7f * M, 0.7f * M, 0.02f * M, 0, false };
        case Prop::Drain:       return { 0.8f * M, 0.35f * M, 0.02f * M, 0, false };
        case Prop::PicnicTable: return { 1.8f * M, 1.6f * M, 0.8f * M, 0, false };
        case Prop::Crate:       return { 1.0f * M, 1.0f * M, 1.0f * M, 0.55f * M, true };
        case Prop::Rock:        return { 1.0f * M, 0.9f * M, 0.6f * M, 0.45f * M, false };
        case Prop::WaterTank:   return { 3.0f * M, 3.0f * M, 3.5f * M, 0, false };
        case Prop::Vent:        return { 0.8f * M, 0.8f * M, 0.8f * M, 0, false };
        default:                return { 16, 16, 8, 0, false };
    }
}

// -------------------------------------------------------------------------------------
//  Rail loop
// -------------------------------------------------------------------------------------
Vector2 RailLoop::PointAt(float s, float* angle) const {
    if (pts.size() < 2 || length <= 0) return { 0, 0 };
    s = fmodf(s, length); if (s < 0) s += length;
    size_t lo = 0, hi = dist.size() - 1;
    while (hi - lo > 1) { size_t mid = (lo + hi) / 2; if (dist[mid] <= s) lo = mid; else hi = mid; }
    Vector2 a = pts[lo], b = pts[(lo + 1) % pts.size()];
    float seg = std::max(1e-3f, dist[lo + 1] - dist[lo]);
    float t = (s - dist[lo]) / seg;
    if (angle) *angle = AngleOf(b - a);
    return LerpV(a, b, t);
}

// -------------------------------------------------------------------------------------
//  Generation
// -------------------------------------------------------------------------------------
bool CityMap::Generate(uint32_t seed, startup::Reporter* loading) {
    generated = false;
    if (loading && (!loading->Begin("world.city", "Streets, buildings and metro", 1) ||
        !loading->PlanChildren({ { "layout" }, { "tiles" }, { "signals" }, { "blocks" }, { "rail" },
                                 { "bridges" }, { "furniture" }, { "index" }, { "train" } }))) return false;
    auto phase = [&](const char* detail) {
        return !loading || (loading->Count(0, 0, "") && loading->Pulse(detail, true));
    };
    Rng rng(seed);
    buildings.clear(); objects.clear(); parking.clear();
    for (auto& v : tileBuildings) v.clear();
    for (auto& v : tileObjects) v.clear();

    // ---- block types: downtown stays dense, special blocks sprinkled around ----
    for (int j = 0; j < BLOCKS_Y; j++)
        for (int i = 0; i < BLOCKS_X; i++) blocks[j][i] = BlockType::Buildings;
    auto pickFree = [&](int margin) {
        for (int tries = 0; tries < 200; tries++) {
            int i = rng.Int(margin, BLOCKS_X - 1 - margin), j = rng.Int(margin, BLOCKS_Y - 1 - margin);
            bool downtown = abs(i - BLOCKS_X / 2) <= 1 && abs(j - BLOCKS_Y / 2) <= 1;
            if (blocks[j][i] == BlockType::Buildings && !downtown) return std::pair<int, int>(i, j);
        }
        return std::pair<int, int>(0, 0);
    };
    auto place = [&](BlockType t, int n, int margin) { for (int k = 0; k < n; k++) { auto p = pickFree(margin); blocks[p.second][p.first] = t; } };
    place(BlockType::Police, 1, 1);
    place(BlockType::Hospital, 1, 1);
    place(BlockType::Park, 7, 0);
    place(BlockType::Plaza, 4, 1);
    place(BlockType::Parking, 6, 0);
    blocks[BLOCKS_Y / 2][BLOCKS_X / 2] = BlockType::Plaza;       // central square
    if (loading && (!loading->ChildDone("layout") || !phase("Laying roads and pavements"))) return false;

    // ---- tiles ----
    for (int ty = 0; ty < MAP_H; ty++) {
        for (int tx = 0; tx < MAP_W; tx++) {
            int lx = tx % BLOCK_PITCH, ly = ty % BLOCK_PITCH;
            Tile t;
            if (lx < 2 || ly < 2) t = Tile::Road;
            else if (lx == 2 || lx == BLOCK_PITCH - 1 || ly == 2 || ly == BLOCK_PITCH - 1) t = Tile::Sidewalk;
            else {
                switch (blocks[ty / BLOCK_PITCH][tx / BLOCK_PITCH]) {
                    case BlockType::Park:    t = Tile::Grass; break;
                    case BlockType::Parking: t = Tile::Parking; break;
                    case BlockType::Plaza:   t = Tile::Plaza; break;
                    default:                 t = Tile::Lot; break;
                }
            }
            tiles[ty][tx] = t;
        }
        if (loading && (!loading->Count(ty + 1, MAP_H, "tile rows") ||
            !loading->ChildProgress("tiles", (double)(ty + 1) / MAP_H))) return false;
    }
    if (loading && (!loading->ChildDone("tiles") || !phase("Preparing traffic signals"))) return false;
    for (int j = 0; j < INTER_Y; j++) {
        for (int i = 0; i < INTER_X; i++) signalOffset[j][i] = rng.Range(0, 16);
        if (loading && (!loading->Count(j + 1, INTER_Y, "junction rows") ||
            !loading->ChildProgress("signals", (double)(j + 1) / INTER_Y))) return false;
    }
    if (loading && (!loading->ChildDone("signals") || !phase("Constructing city blocks"))) return false;

    for (int j = 0; j < BLOCKS_Y; j++)
        for (int i = 0; i < BLOCKS_X; i++) {
            GenBlock(i, j, rng);
            int completed = j * BLOCKS_X + i + 1;
            if (loading && (!loading->Count(completed, BLOCKS_X * BLOCKS_Y, "blocks") ||
                !loading->ChildProgress("blocks", (double)completed / (BLOCKS_X * BLOCKS_Y)))) return false;
        }
    if (loading && (!loading->ChildDone("blocks") || !phase("Constructing metro routes"))) return false;
    GenRail();
    if (loading && (!loading->Count(1, 1, "metro loops") || !loading->ChildDone("rail") ||
        !phase("Constructing bridges and gates"))) return false;
    GenGatesAndBridges(rng);
    if (loading && (!loading->Count(1, 1, "bridge passes") || !loading->ChildDone("bridges") ||
        !phase("Placing street furniture"))) return false;
    if (!GenStreetFurniture(rng, loading)) return false;
    if (loading && (!loading->ChildDone("furniture") || !phase("Preparing city spatial queries"))) return false;
    if (!IndexBuildings(loading)) return false;
    if (loading && !loading->ChildDone("index")) return false;

    // train: 3 carriages
    trainPos = { 0.0f, -300.0f, -600.0f };
    trainSpeed = 0;
    generated = true;
    if (!Ready()) { if (loading) loading->Fail("City generation is incomplete"); return false; }
    if (loading && (!loading->ChildDone("train") || !loading->Finish())) return false;
    return true;
}

bool CityMap::Ready() const {
    return generated && !buildings.empty() && !objects.empty() && !parking.empty() &&
           rail.pts.size() > 1 && rail.dist.size() == rail.pts.size() + 1 && std::isfinite(rail.length) && rail.length > 0 &&
           trainPos.size() == 3 && stamp.size() > std::max(buildings.size(), objects.size()) &&
           InCity(policeStation) && InCity(hospital);
}

// How street furniture reacts to a vehicle hitting it (see CityObject::strength).
// Real-world reference: city lamp and sign posts are breakaway designs, hydrants shear
// off at the flange, steel bollards, concrete planters and tree trunks stop a car.
// strength = impulse in tonnes * px/s (1 t car at 170 px/s ~ 38 km/h knocks a lamp over).
static void ApplyMaterial(CityObject& o) {
    switch (o.kind) {
        case CityObject::Bush:   o.soft = true; o.strength = 170; break;   // flattened, else drag
        case CityObject::Lamp:   o.strength = 170; o.mass = 0.15f; break;
        case CityObject::Signal: o.strength = 260; o.mass = 0.25f; break;
        case CityObject::Prop:
            switch ((Prop)o.sprite) {
                case Prop::Cone:       o.strength = 1;   o.mass = 0.004f; break;
                case Prop::TrashBin:   o.strength = 10;  o.mass = 0.02f;  break;
                case Prop::Crate:      o.strength = 12;  o.mass = 0.03f;  break;
                case Prop::Barrel:     o.strength = 14;  o.mass = 0.06f;  break;
                case Prop::NewsBox:    o.strength = 25;  o.mass = 0.04f;  break;
                case Prop::Mailbox:    o.strength = 45;  o.mass = 0.06f;  break;
                case Prop::Hydrant:    o.strength = 90;  o.mass = 0.12f;  break;
                case Prop::PhoneBooth: o.strength = 260; o.mass = 0.30f;  break;
                case Prop::Bollard:    o.strength = 600; o.mass = 0.10f;  break;   // steel: only a truck
                // box-shaped furniture (was drive-through / a circle before)
                case Prop::Bench:       o.box = true; o.strength = 70;  o.mass = 0.06f; break;
                case Prop::PicnicTable: o.box = true; o.strength = 60;  o.mass = 0.08f; break;
                case Prop::BusShelter:  o.box = true; o.walkIn = true; o.strength = 300; o.mass = 0.40f; break;
                case Prop::Dumpster:    o.box = true; break;                       // steel skip, loaded: rigid
                case Prop::Planter:     o.box = true; break;                       // concrete: rigid
                default: break;                                                    // rock
            }
            if (o.box) o.radius = 0.5f * sqrtf(o.w * o.w + o.l * o.l);           // bounding circle for queries
            break;
        default: break;                                                            // tree, pillar, fountain
    }
}

void CityMap::AddObject(const CityObject& o) {
    objects.push_back(o);
    ApplyMaterial(objects.back());
}

void CityMap::AddBuilding(Rectangle r, float floors, Rng& rng, int special) {
    Building b;
    b.r = r;
    floors = Clampf(roundf(floors), 2, 8);
    b.height = floors * STOREY + 0.9f * M;
    b.facade = floors >= 6 ? (rng.Chance(0.75f) ? 1 : 0) : (rng.Chance(0.78f) ? 0 : 1);
    static const Color brickTints[] = { { 255, 255, 255, 255 }, { 240, 225, 210, 255 }, { 225, 225, 230, 255 },
                                        { 255, 236, 205, 255 }, { 210, 200, 195, 255 }, { 250, 215, 200, 255 } };
    static const Color glassTints[] = { { 235, 245, 255, 255 }, { 215, 235, 235, 255 }, { 245, 240, 230, 255 },
                                        { 200, 215, 235, 255 }, { 230, 230, 230, 255 } };
    b.wallTint = b.facade == 0 ? brickTints[rng.Int(0, 5)] : glassTints[rng.Int(0, 4)];
    unsigned char g = (unsigned char)rng.Int(150, 215);
    b.roofTint = { g, (unsigned char)(g - rng.Int(0, 12)), (unsigned char)(g - rng.Int(0, 20)), 255 };
    b.roofTex = rng.Chance(0.6f) ? 0 : 1;
    b.litOffset = (float)rng.Int(0, 7);
    b.litAmount = rng.Range(0.45f, 1.0f);
    b.special = special;
    b.beacon = floors >= 8;
    if (b.facade == 0 && rng.Chance(0.18f)) {
        static const Color neons[] = { { 255, 60, 170, 255 }, { 60, 220, 255, 255 }, { 255, 210, 60, 255 }, { 120, 255, 120, 255 } };
        b.neon = neons[rng.Int(0, 3)];
    }
    if (special == 1) { b.roofTint = { 120, 135, 160, 255 }; b.wallTint = { 225, 230, 240, 255 }; b.facade = 1; }
    if (special == 2) { b.roofTint = { 225, 225, 222, 255 }; b.wallTint = { 250, 250, 250, 255 }; b.facade = 1; }

    // rooftop gear (skip a margin for the parapet); helipads keep their centre clear
    float m = 1.2f * M;
    Rectangle in = { r.x + m, r.y + m, r.width - 2 * m, r.height - 2 * m };
    int nAc = special ? 2 : rng.Int(1, 4), nVent = rng.Int(1, 4);
    auto place = [&](int prop, float size) {
        for (int tries = 0; tries < 12; tries++) {
            Vector2 p = { rng.Range(in.x + size * 0.6f, in.x + in.width - size * 0.6f), rng.Range(in.y + size * 0.6f, in.y + in.height - size * 0.6f) };
            if (special && Dist(p, { r.x + r.width * 0.5f, r.y + r.height * 0.5f }) < 7.5f * M) continue;
            bool clash = false;
            for (auto& q : b.props) if (Dist(q.pos, p) < (q.size + size) * 0.6f) { clash = true; break; }
            if (!clash) { b.props.push_back({ p, rng.Chance(0.5f) ? 0.0f : PI * 0.5f, prop, size }); return; }
        }
    };
    if (in.width > 40 && in.height > 40) {
        if (!special && b.facade == 0 && rng.Chance(0.35f)) place((int)Prop::WaterTank, 3.0f * M);
        for (int k = 0; k < nAc; k++) place((int)Prop::ACUnit, rng.Range(1.4f, 2.2f) * M);
        for (int k = 0; k < nVent; k++) place((int)Prop::Vent, 0.8f * M);
    }
    buildings.push_back(b);
}

void CityMap::GenBuildingsIn(Rectangle lot, int depth, float heightMul, Rng& rng) {
    const float MIN = 11.0f * M;          // don't split below ~22 m lots
    bool canX = lot.width > MIN * 2, canY = lot.height > MIN * 2;
    if (depth < 3 && (canX || canY) && !(depth > 0 && rng.Chance(0.18f))) {
        bool splitX = canX && (!canY || lot.width > lot.height || (lot.width == lot.height && rng.Chance(0.5f)));
        float f = rng.Range(0.38f, 0.62f);
        float gap = rng.Chance(0.35f) ? 2.5f * M : 0.0f;         // service alley
        if (splitX) {
            float w1 = roundf(lot.width * f / 8) * 8;
            GenBuildingsIn({ lot.x, lot.y, w1 - gap * 0.5f, lot.height }, depth + 1, heightMul, rng);
            GenBuildingsIn({ lot.x + w1 + gap * 0.5f, lot.y, lot.width - w1 - gap * 0.5f, lot.height }, depth + 1, heightMul, rng);
        } else {
            float h1 = roundf(lot.height * f / 8) * 8;
            GenBuildingsIn({ lot.x, lot.y, lot.width, h1 - gap * 0.5f }, depth + 1, heightMul, rng);
            GenBuildingsIn({ lot.x, lot.y + h1 + gap * 0.5f, lot.width, lot.height - h1 - gap * 0.5f }, depth + 1, heightMul, rng);
        }
        return;
    }
    // leaf lot: small courtyard with greenery now and then
    if (depth > 0 && rng.Chance(0.1f)) {
        Vector2 c = { lot.x + lot.width * 0.5f, lot.y + lot.height * 0.5f };
        CityObject t; t.kind = CityObject::Tree; t.pos = c; t.sprite = rng.Int(0, (int)gAssets.trees.size() - 1);
        t.w = t.l = rng.Range(7, 10) * M; t.h = rng.Range(6, 8) * M; t.rot = rng.Range(0, 2 * PI); t.radius = 0.35f * M;
        AddObject(t);
        CityObject d; d.kind = CityObject::Prop; d.sprite = (int)Prop::Dumpster; d.pos = { lot.x + 1.5f * M, lot.y + 1.2f * M };
        PropDim pd = Dim(Prop::Dumpster); d.w = pd.w; d.l = pd.l; d.h = pd.h + CURB_H; d.radius = pd.radius;
        AddObject(d);
        return;
    }
    float inset = 0.4f * M;
    Rectangle r = { lot.x + inset, lot.y + inset, lot.width - 2 * inset, lot.height - 2 * inset };
    float floors = rng.Range(2.0f, 5.5f) * heightMul;
    AddBuilding(r, floors, rng);
}

void CityMap::GenBlock(int bi, int bj, Rng& rng) {
    Rectangle in = BlockInterior(bi, bj);
    Vector2 c = { in.x + in.width * 0.5f, in.y + in.height * 0.5f };
    float dx = (c.x - WORLD_W * 0.5f) / (WORLD_W * 0.5f), dy = (c.y - WORLD_H * 0.5f) / (WORLD_H * 0.5f);
    float d = Saturate(sqrtf(dx * dx + dy * dy));
    float heightMul = 1.0f + 0.9f * (1 - d) * (1 - d);

    auto tree = [&](Vector2 p, float sizeM, float hM, bool large = true) {
        CityObject t; t.kind = CityObject::Tree; t.pos = p;
        t.sprite = rng.Int(0, std::max(0, (int)gAssets.trees.size() - 1));
        t.w = t.l = sizeM * M; t.h = hM * M; t.rot = rng.Range(0, 2 * PI); t.radius = large ? 0.35f * M : 0;
        AddObject(t);
    };
    auto bush = [&](Vector2 p, float sizeM) {
        CityObject t; t.kind = CityObject::Bush; t.pos = p;
        t.sprite = rng.Int(0, std::max(0, (int)gAssets.bushes.size() - 1));
        t.w = t.l = sizeM * M; t.h = CURB_H + sizeM * 0.45f * M; t.rot = rng.Range(0, 2 * PI);
        t.radius = sizeM * M * 0.36f;                   // dense shrub: solid
        AddObject(t);
    };
    auto prop = [&](Prop p, Vector2 pos, float rot, float baseH = CURB_H) {
        PropDim pd = Dim(p);
        CityObject o; o.kind = CityObject::Prop; o.sprite = (int)p; o.pos = pos; o.rot = rot;
        o.w = pd.w; o.l = pd.l; o.h = baseH + pd.h; o.radius = pd.radius; o.breakable = pd.breakable;
        AddObject(o);
    };
    auto lamp = [&](Vector2 base, Vector2 dir, float height) {
        CityObject o; o.kind = CityObject::Lamp; o.pos = base; o.head = base + dir * LAMP_REACH * 0.6f;
        o.h = height; o.radius = 0.12f * M; o.rot = AngleOf(dir);
        AddObject(o);
    };

    switch (blocks[bj][bi]) {
    case BlockType::Buildings:
        GenBuildingsIn(in, 0, heightMul, rng);
        break;

    case BlockType::Police:
    case BlockType::Hospital: {
        bool police = blocks[bj][bi] == BlockType::Police;
        Rectangle br = { in.x + 3 * M, in.y + 3 * M, in.width - 6 * M, in.height * 0.62f };
        AddBuilding(br, police ? 4 : 6, rng, police ? 1 : 2);
        // forecourt parking for emergency vehicles
        float y = br.y + br.height + 5.0f * M;
        for (float x = in.x + 5 * M; x < in.x + in.width - 4 * M; x += 3.4f * M)
            parking.push_back({ { x, y }, 0.0f });
        Vector2 entrance = { c.x, (bj * BLOCK_PITCH + BLOCK_PITCH - 1) * (float)TILE + TILE * 0.5f };
        if (police) policeStation = entrance; else hospital = entrance;
        for (int k = 0; k < 4; k++) prop(Prop::Planter, { in.x + (2.5f + k * 13.5f) * M, in.y + in.height - 2 * M }, 0);
    } break;

    case BlockType::Park: {
        // footpaths (a cross) - drawn as plaza patches in DrawGround via tile checks
        float pw = 3.0f * M;
        Rectangle ph = { in.x, c.y - pw * 0.5f, in.width, pw }, pv = { c.x - pw * 0.5f, in.y, pw, in.height };
        CityObject f; f.kind = CityObject::Fountain; f.pos = c; f.radius = 4.2f * M; f.w = f.l = 8.4f * M; f.h = 0.8f * M;
        AddObject(f);
        for (int k = 0; k < 26; k++) {
            Vector2 p = { rng.Range(in.x + 3 * M, in.x + in.width - 3 * M), rng.Range(in.y + 3 * M, in.y + in.height - 3 * M) };
            Rectangle ep = { p.x - 4 * M, p.y - 4 * M, 8 * M, 8 * M };
            if (CheckCollisionRecs(ep, ph) || CheckCollisionRecs(ep, pv) || Dist(p, c) < 9 * M) continue;
            bool clash = false;
            for (auto& o : objects) if (o.kind == CityObject::Tree && Dist(o.pos, p) < 6.5f * M) { clash = true; break; }
            if (clash) continue;
            tree(p, rng.Range(7.0f, 12.0f), rng.Range(6.0f, 10.0f));
        }
        for (int k = 0; k < 30; k++) {                          // shrubs along the paths
            bool alongH = rng.Chance(0.5f);
            float t = rng.Range(0.08f, 0.92f), side = rng.Chance(0.5f) ? 1.0f : -1.0f;
            Vector2 p = alongH ? Vector2{ in.x + in.width * t, c.y + side * (pw * 0.5f + 1.2f * M) }
                               : Vector2{ c.x + side * (pw * 0.5f + 1.2f * M), in.y + in.height * t };
            if (Dist(p, c) < 7 * M) continue;
            bush(p, rng.Range(1.4f, 2.6f));
        }
        for (int k = 0; k < 4; k++) {                           // benches & lamps along paths
            float t = k < 2 ? 0.25f : 0.75f;
            bool alongH = (k % 2) == 0;
            Vector2 p = alongH ? Vector2{ in.x + in.width * t, c.y - pw * 0.5f - 0.6f * M } : Vector2{ c.x - pw * 0.5f - 0.6f * M, in.y + in.height * t };
            prop(Prop::Bench, p, alongH ? 0.0f : PI * 0.5f);
            Vector2 lp = alongH ? Vector2{ in.x + in.width * t + 3 * M, c.y + pw * 0.5f + 0.4f * M } : Vector2{ c.x + pw * 0.5f + 0.4f * M, in.y + in.height * t + 3 * M };
            lamp(lp, alongH ? Vector2{ 0, -1 } : Vector2{ -1, 0 }, 4.5f * M);
        }
        prop(Prop::PicnicTable, { in.x + 6 * M, in.y + 6 * M }, rng.Range(0, 1));
        prop(Prop::PicnicTable, { in.x + in.width - 7 * M, in.y + in.height - 6 * M }, rng.Range(0, 1));
        for (int k = 0; k < 3; k++) prop(Prop::Rock, { rng.Range(in.x + 3 * M, in.x + in.width - 3 * M), rng.Range(in.y + 3 * M, in.y + in.height - 3 * M) }, rng.Range(0, 6));
        prop(Prop::TrashBin, { c.x + pw, c.y + pw }, 0);
    } break;

    case BlockType::Plaza: {
        CityObject f; f.kind = CityObject::Fountain; f.pos = c; f.radius = 6.0f * M; f.w = f.l = 12 * M; f.h = 0.9f * M;
        AddObject(f);
        for (int k = 0; k < 8; k++) {                           // ring of planters with trees
            float a = k * PI / 4 + PI / 8;
            Vector2 p = c + V2(cosf(a), sinf(a)) * 16.0f * M;
            prop(Prop::Planter, p, 0);
            tree(p, rng.Range(5.0f, 7.0f), rng.Range(5.0f, 6.5f), false);
        }
        for (int k = 0; k < 6; k++) {                           // benches facing the fountain
            float a = k * PI / 3;
            Vector2 p = c + V2(cosf(a), sinf(a)) * 9.0f * M;
            prop(Prop::Bench, p, a + PI * 0.5f);
        }
        // cafe terrace in one corner
        Prop par[3] = { Prop::ParasolRed, Prop::ParasolBlue, Prop::ParasolGreen };
        Prop pick = par[rng.Int(0, 2)];
        for (int k = 0; k < 6; k++) {
            Vector2 p = { in.x + (4.0f + (k % 3) * 4.2f) * M, in.y + (4.0f + (k / 3) * 4.2f) * M };
            prop(pick, p, rng.Range(0, 1));
        }
        for (int k = 0; k < 4; k++) {
            Vector2 corner = { k % 2 ? in.x + in.width - 2.5f * M : in.x + 2.5f * M, k / 2 ? in.y + in.height - 2.5f * M : in.y + 2.5f * M };
            lamp(corner, Norm(c - corner), 5.5f * M);
        }
        for (int k = 0; k < 10; k++) bush({ rng.Range(in.x + 2 * M, in.x + in.width - 2 * M), in.y + in.height - 1.5f * M }, rng.Range(1.2f, 2.0f));
    } break;

    case BlockType::Parking: {
        // three double rows of 2.5 x 5 m bays, aisles in between
        float bayW = 2.5f * M, bayL = 5.0f * M;
        for (int row = 0; row < 3; row++) {
            float yMid = in.y + 5.2f * M + row * 17.0f * M;
            for (float x = in.x + 2.0f * M + bayW * 0.5f; x < in.x + in.width - 2.0f * M; x += bayW) {
                parking.push_back({ { x, yMid - bayL * 0.5f }, PI });
                parking.push_back({ { x, yMid + bayL * 0.5f }, 0.0f });
            }
        }
        for (int k = 0; k < 4; k++) lamp({ in.x + (8.0f + k * 12.0f) * M, in.y + 13.6f * M }, { 0, 1 }, 7.0f * M);
        prop(Prop::PhoneBooth, { in.x + in.width - 1.5f * M, in.y + 1.5f * M }, 0);
        for (int k = 0; k < 6; k++) bush({ in.x + (2.0f + k * 8.5f) * M, in.y + in.height - 1.2f * M }, rng.Range(1.2f, 1.8f));
    } break;
    }
}

bool CityMap::RailOverRoad(int tx, int ty) const {
    int i = tx / BLOCK_PITCH, j = ty / BLOCK_PITCH;
    bool col = (i == 2 || i == INTER_X - 3) && tx % BLOCK_PITCH < 2;
    bool row = (j == 2 || j == INTER_Y - 3) && ty % BLOCK_PITCH < 2;
    return col || row;
}

void CityMap::GenRail() {
    rail.pts.clear(); rail.dist.clear();
    Vector2 C0 = InterCenter(2, 2), C1 = InterCenter(INTER_X - 3, 2), C2 = InterCenter(INTER_X - 3, INTER_Y - 3), C3 = InterCenter(2, INTER_Y - 3);
    const float R = 6.0f * M, step = 1.5f * M;
    auto line = [&](Vector2 a, Vector2 b) {
        int n = std::max(1, (int)(Dist(a, b) / step));
        for (int k = 0; k < n; k++) rail.pts.push_back(LerpV(a, b, k / (float)n));
    };
    auto arc = [&](Vector2 centre, float a0, float a1) {
        int n = 10;
        for (int k = 0; k < n; k++) { float a = Lerpf(a0, a1, k / (float)n); rail.pts.push_back(centre + V2(cosf(a), sinf(a)) * R); }
    };
    line(C0 + V2(R, 0), C1 - V2(R, 0));   arc(C1 + V2(-R, R), -PI * 0.5f, 0.0f);
    line(C1 + V2(0, R), C2 - V2(0, R));   arc(C2 + V2(-R, -R), 0.0f, PI * 0.5f);
    line(C2 - V2(R, 0), C3 + V2(R, 0));   arc(C3 + V2(R, -R), PI * 0.5f, PI);
    line(C3 - V2(0, R), C0 + V2(0, R));   arc(C0 + V2(R, R), PI, PI * 1.5f);
    float acc = 0;
    for (size_t k = 0; k < rail.pts.size(); k++) {
        rail.dist.push_back(acc);
        acc += Dist(rail.pts[k], rail.pts[(k + 1) % rail.pts.size()]);
    }
    rail.dist.push_back(acc);
    rail.length = acc;
    // pillars on the road centre line, never inside intersections
    for (float s = 0; s < rail.length; s += 11.0f * M) {
        Vector2 p = rail.PointAt(s);
        bool nearInter = false;
        for (int j = 0; j < INTER_Y && !nearInter; j++)
            for (int i = 0; i < INTER_X; i++) {
                Vector2 ic = InterCenter(i, j);
                if (fabsf(p.x - ic.x) < 2.6f * TILE && fabsf(p.y - ic.y) < 2.6f * TILE) { nearInter = true; break; }
            }
        if (nearInter) continue;
        // portal frame: a column on each sidewalk and a cross beam under the deck,
        // so the traffic lanes underneath stay completely free
        float ang; rail.PointAt(s, &ang);
        Vector2 n = RightOf(ang) * (ROAD_HALF + TILE * 0.5f);
        for (int side = -1; side <= 1; side += 2) {
            CityObject o; o.kind = CityObject::Pillar; o.pos = p + n * (float)side; o.radius = 0.6f * M; o.w = o.l = 1.0f * M;
            o.h = H_RAIL_DECK - 0.8f * M;
            o.axis = side < 0 ? 1 : 0;             // the first column of the pair carries the beam
            o.head = p - n * (float)side;
            AddObject(o);
        }
    }
}

void CityMap::GenGatesAndBridges(Rng& rng) {
    // Gate buildings span a north-south street: drive underneath (GTA-style underpass).
    int made = 0;
    for (int tries = 0; tries < 200 && made < 4; tries++) {
        int i = rng.Int(1, INTER_X - 2), j = rng.Int(0, INTER_Y - 2);
        if (i == 2 || i == INTER_X - 3) continue;                         // keep clear of the rail
        if (blocks[j][i - 1] != BlockType::Buildings || blocks[j][i] != BlockType::Buildings) continue;
        float x0 = (float)((i * BLOCK_PITCH - 2) * TILE), x1 = (float)((i * BLOCK_PITCH + 4) * TILE);
        float y0 = (float)((j * BLOCK_PITCH + 7) * TILE);
        Rectangle r = { x0, y0, x1 - x0, 4.0f * TILE };
        bool clash = false;
        for (auto& b : buildings) if (!b.Solid() && CheckCollisionRecs(b.r, { r.x, r.y - 400, r.width, r.height + 800 })) clash = true;
        if (clash) continue;
        size_t idx = buildings.size();
        AddBuilding(r, (float)rng.Int(3, 6), rng);
        buildings[idx].base = 5.8f * M;
        buildings[idx].height += buildings[idx].base;
        buildings[idx].props.clear();
        // arcade columns on both sidewalks
        for (int sx = 0; sx < 2; sx++)
            for (int sy = 0; sy < 2; sy++) {
                CityObject o; o.kind = CityObject::Pillar;
                o.pos = { (float)((i * BLOCK_PITCH + (sx ? 2 : -1)) * TILE + TILE * 0.5f), y0 + (sy ? 4 * TILE - 20.0f : 20.0f) };
                o.radius = 0.7f * M; o.w = o.l = 1.2f * M; o.h = buildings[idx].base;
                AddObject(o);
            }
        made++;
    }
    // Glass skybridges across east-west streets.
    made = 0;
    for (int tries = 0; tries < 200 && made < 4; tries++) {
        int i = rng.Int(0, INTER_X - 2), j = rng.Int(1, INTER_Y - 2);
        if (j == 2 || j == INTER_Y - 3) continue;
        if (blocks[j - 1][i] != BlockType::Buildings || blocks[j][i] != BlockType::Buildings) continue;
        float x0 = (float)((i * BLOCK_PITCH + 7) * TILE) + rng.Range(0, 2) * TILE;
        Rectangle r = { x0, (float)((j * BLOCK_PITCH - 2) * TILE), 3.0f * M, 6.0f * TILE };
        Building b; b.r = r; b.base = 12.0f * M; b.height = 15.0f * M; b.special = 3; b.facade = 1;
        b.wallTint = { 170, 210, 235, 255 }; b.roofTint = { 150, 160, 170, 255 }; b.roofTex = 1;
        buildings.push_back(b);
        made++;
    }
}

bool CityMap::GenStreetFurniture(Rng& rng, startup::Reporter* loading) {
    const int batches = BLOCKS_X * BLOCKS_Y + 2 * INTER_Y;
    int completed = 0;
    auto checkpoint = [&]() {
        completed++;
        return !loading || (loading->Count(completed, batches, "generation batches") &&
                           loading->ChildProgress("furniture", (double)completed / batches));
    };
    auto prop = [&](Prop p, Vector2 pos, float rot) {
        PropDim pd = Dim(p);
        CityObject o; o.kind = CityObject::Prop; o.sprite = (int)p; o.pos = pos; o.rot = rot;
        o.w = pd.w; o.l = pd.l; o.h = CURB_H + pd.h; o.radius = pd.radius; o.breakable = pd.breakable;
        AddObject(o);
    };
    const float T = (float)TILE;
    for (int bj = 0; bj < BLOCKS_Y; bj++)
        for (int bi = 0; bi < BLOCKS_X; bi++) {
            float bx = (float)(bi * BLOCK_PITCH * TILE), by = (float)(bj * BLOCK_PITCH * TILE);
            // four sides: curb line, direction along the side, inward normal (towards buildings)
            struct Side { Vector2 start; Vector2 along; Vector2 inward; };
            Side sides[4] = {
                { { bx + 2 * T, by + 2 * T }, { 1, 0 }, { 0, 1 } },                                    // north
                { { bx + 2 * T, by + (BLOCK_PITCH) * T }, { 1, 0 }, { 0, -1 } },                       // south
                { { bx + 2 * T, by + 2 * T }, { 0, 1 }, { 1, 0 } },                                    // west
                { { bx + BLOCK_PITCH * T, by + 2 * T }, { 0, 1 }, { -1, 0 } },                         // east
            };
            bool busStopPlaced = false;
            for (int s = 0; s < 4; s++) {
                const Side& sd = sides[s];
                float sideRot = sd.along.x != 0 ? 0.0f : PI * 0.5f;
                bool treeStreet = rng.Chance(0.55f);
                for (int k = 2; k <= BLOCK_PITCH - 5; k++) {
                    Vector2 curb = sd.start + sd.along * ((k + 0.5f) * T);
                    if (k % 4 == 1) {                                               // street lamp
                        CityObject o; o.kind = CityObject::Lamp; o.pos = curb + sd.inward * 10.0f;
                        o.head = o.pos - sd.inward * LAMP_REACH; o.h = H_LAMP; o.radius = 0.15f * M; o.rot = AngleOf(sd.inward * -1.0f);
                        AddObject(o);
                    } else if (k % 4 == 3 && treeStreet) {                         // street tree
                        CityObject t; t.kind = CityObject::Tree; t.pos = curb + sd.inward * 11.0f;
                        t.sprite = rng.Int(0, std::max(0, (int)gAssets.trees.size() - 1));
                        t.w = t.l = rng.Range(5.5f, 7.5f) * M; t.h = rng.Range(5.0f, 6.5f) * M; t.rot = rng.Range(0, 2 * PI); t.radius = 0.3f * M;
                        AddObject(t);
                    } else {
                        Vector2 inner = curb + sd.inward * (T - 9.0f);
                        float r = rng.Float();
                        if (r < 0.10f) prop(Prop::TrashBin, inner, 0);
                        else if (r < 0.16f) prop(Prop::Bench, inner + sd.inward * -2.0f, sideRot);
                        else if (r < 0.20f) prop(Prop::NewsBox, inner, sideRot);
                        else if (r < 0.23f) prop(Prop::Mailbox, inner, sideRot);
                        else if (r < 0.26f) prop(Prop::PhoneBooth, inner - sd.inward * 4.0f, sideRot);
                        else if (r < 0.32f) prop(Prop::Hydrant, curb + sd.inward * 8.0f, 0);
                        else if (r < 0.40f && blocks[bj][bi] == BlockType::Buildings) {
                            CityObject b; b.kind = CityObject::Bush; b.pos = inner;
                            b.sprite = rng.Int(0, std::max(0, (int)gAssets.bushes.size() - 1));
                            b.w = b.l = rng.Range(1.2f, 1.8f) * M; b.h = CURB_H + 0.7f * M; b.rot = rng.Range(0, 6);
                            b.radius = b.w * 0.36f;
                            AddObject(b);
                        }
                    }
                    if (k == BLOCK_PITCH / 2 && !busStopPlaced && rng.Chance(0.3f)) {
                        prop(Prop::BusShelter, curb + sd.inward * (T * 0.62f) + sd.along * 20.0f, sideRot);
                        busStopPlaced = true;
                    }
                }
                // storm drains in the gutter, manholes on the road
                for (int k = 3; k < BLOCK_PITCH - 3; k += 5) {
                    CityObject d; d.kind = CityObject::Prop; d.sprite = (int)Prop::Drain;
                    d.pos = sd.start + sd.along * (k * T) - sd.inward * 6.0f; d.rot = sideRot;
                    PropDim pd = Dim(Prop::Drain); d.w = pd.w; d.l = pd.l; d.h = 0.1f;
                    AddObject(d);
                }
            }
            // bollards at the four corners
            Rectangle ring = SidewalkRing(bi, bj);
            Vector2 cs[4] = { { ring.x - 22, ring.y - 22 }, { ring.x + ring.width + 22, ring.y - 22 },
                              { ring.x + ring.width + 22, ring.y + ring.height + 22 }, { ring.x - 22, ring.y + ring.height + 22 } };
            for (auto& p : cs) prop(Prop::Bollard, p, 0);
            if (!checkpoint()) return false;
        }
    // manholes along the roads
    for (int j = 0; j < INTER_Y; j++) {
        for (int i = 0; i < INTER_X - 1; i++) {
            Vector2 a = InterCenter(i, j);
            CityObject m; m.kind = CityObject::Prop; m.sprite = (int)Prop::Manhole;
            m.pos = { a.x + rng.Range(4, 12) * T, a.y + rng.Range(-20, 20) }; m.rot = rng.Range(0, 6);
            PropDim pd = Dim(Prop::Manhole); m.w = pd.w; m.l = pd.l; m.h = 0.1f;
            AddObject(m);
        }
        if (!checkpoint()) return false;
    }
    // traffic signals at every corner of every intersection
    for (int j = 0; j < INTER_Y; j++) {
        for (int i = 0; i < INTER_X; i++) {
            Vector2 c = InterCenter(i, j);
            const float o = TILE + 10.0f;
            struct Corner { int sx, sy, axis; Vector2 arm; } corners[4] = {
                { -1, -1, 0, { 1, 0 } }, { 1, 1, 0, { -1, 0 } }, { 1, -1, 1, { 0, 1 } }, { -1, 1, 1, { 0, -1 } } };
            for (auto& k : corners) {
                Vector2 base = c + V2(k.sx * o, k.sy * o);
                if (!InCity(base, -8)) continue;
                CityObject s; s.kind = CityObject::Signal; s.pos = base; s.axis = k.axis;
                s.head = base + k.arm * 34.0f; s.h = H_SIGNAL; s.radius = 0.15f * M;
                s.rot = k.axis == 0 ? PI * 0.5f : 0.0f;
                AddObject(s);
            }
        }
        if (!checkpoint()) return false;
    }
    return true;
}

bool CityMap::IndexBuildings(startup::Reporter* loading) {
    const size_t records = buildings.size() + objects.size();
    auto checkpoint = [&](size_t completed) {
        return !loading || (loading->Count((int64_t)completed, (int64_t)records, "records examined") &&
                           loading->ChildProgress("index", records == 0 ? 0 : (double)completed / records));
    };
    for (auto& v : tileBuildings) v.clear();
    for (auto& v : tileObjects) v.clear();
    for (size_t k = 0; k < buildings.size(); k++) {
        if (k % 64 == 0 && !checkpoint(k)) return false;
        const Building& b = buildings[k];
        if (!b.Solid()) continue;
        int x0 = std::max(0, (int)(b.r.x / TILE)), x1 = std::min(MAP_W - 1, (int)((b.r.x + b.r.width) / TILE));
        int y0 = std::max(0, (int)(b.r.y / TILE)), y1 = std::min(MAP_H - 1, (int)((b.r.y + b.r.height) / TILE));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) tileBuildings[y * MAP_W + x].push_back((int)k);
    }
    for (size_t k = 0; k < objects.size(); k++) {
        if (k % 64 == 0 && !checkpoint(buildings.size() + k)) return false;
        const CityObject& o = objects[k];
        if (o.radius <= 0) continue;
        int x0 = std::max(0, (int)((o.pos.x - o.radius) / TILE)), x1 = std::min(MAP_W - 1, (int)((o.pos.x + o.radius) / TILE));
        int y0 = std::max(0, (int)((o.pos.y - o.radius) / TILE)), y1 = std::min(MAP_H - 1, (int)((o.pos.y + o.radius) / TILE));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) tileObjects[y * MAP_W + x].push_back((int)k);
    }
    stamp.assign(std::max(buildings.size(), objects.size()) + 1, 0);
    return checkpoint(records);
}

void CityMap::ResetTestGround(Tile surface) {
    generated = false;
    testGround = true;
    testSurface = surface;
    buildings.clear(); objects.clear(); parking.clear(); characters.clear();
    rail = RailLoop{}; trainPos.clear(); trainSpeed = 0;
    for (auto& row : tiles) for (Tile& tile : row) tile = surface;
    IndexBuildings();
}

void CityMap::RebuildTestIndex() { IndexBuildings(); }

// -------------------------------------------------------------------------------------
//  Update: signals, train, broken hydrants
// -------------------------------------------------------------------------------------
void CityMap::Update(float dt, Particles& fx) {
    time += dt;
    trainSpeed = Lerpf(trainSpeed, 24.0f * M, Damp(0.3f, dt));
    for (float& s : trainPos) s += trainSpeed * dt;
    // fountain jets and broken hydrants gushing water
    for (const CityObject& o : objects) {
        if (o.kind == CityObject::Fountain) fx.WaterJet(o.pos, o.h + 2.2f * M, 0.6f, dt);
        else if (o.kind == CityObject::Prop && !o.alive && o.sprite == (int)Prop::Hydrant) fx.WaterJet(o.pos, 0.5f * M, 1.0f, dt);
    }
}

// -------------------------------------------------------------------------------------
//  Queries
// -------------------------------------------------------------------------------------
Tile CityMap::TileAtIdx(int tx, int ty) const {
    if (testGround) return testSurface;
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return Tile::Road;
    return tiles[ty][tx];
}
Tile CityMap::TileAt(Vector2 p) const { return TileAtIdx((int)floorf(p.x / TILE), (int)floorf(p.y / TILE)); }
bool CityMap::InCity(Vector2 p, float m) const { return p.x >= -m && p.y >= -m && p.x <= WORLD_W + m && p.y <= WORLD_H + m; }

float CityMap::Grip(Vector2 p) const {
    switch (TileAt(p)) {
        case Tile::Grass: return 0.55f;
        case Tile::Plaza: return 0.9f;
        default: return 1.0f;
    }
}
float CityMap::GroundHeight(Vector2 p) const { return TileAt(p) == Tile::Road ? 0.0f : CURB_H; }

Rectangle CityMap::BlockInterior(int bi, int bj) const {
    return { (float)((bi * BLOCK_PITCH + 3) * TILE), (float)((bj * BLOCK_PITCH + 3) * TILE),
             (float)((BLOCK_PITCH - 4) * TILE), (float)((BLOCK_PITCH - 4) * TILE) };
}
Rectangle CityMap::SidewalkRing(int bi, int bj) const {
    float x0 = (bi * BLOCK_PITCH + 2.5f) * TILE, y0 = (bj * BLOCK_PITCH + 2.5f) * TILE;
    float s = (BLOCK_PITCH - 3) * (float)TILE;
    return { x0, y0, s, s };
}
Vector2 CityMap::SidewalkCorner(int bi, int bj, int c) const {
    Rectangle r = SidewalkRing(bi, bj);
    switch (c & 3) {
        case 0: return { r.x, r.y };
        case 1: return { r.x + r.width, r.y };
        case 2: return { r.x + r.width, r.y + r.height };
        default: return { r.x, r.y + r.height };
    }
}
Vector2 CityMap::InterCenter(int i, int j) const {
    return { (float)((i * BLOCK_PITCH + 1) * TILE), (float)((j * BLOCK_PITCH + 1) * TILE) };
}

int CityMap::SignalState(int i, int j, int axis) const {
    float ph = fmodf(time + signalOffset[j][i], 16.0f);
    if (axis == 0) return ph < 6.0f ? SIG_GREEN : (ph < 7.5f ? SIG_YELLOW : SIG_RED);
    return (ph >= 8.0f && ph < 14.0f) ? SIG_GREEN : ((ph >= 14.0f && ph < 15.5f) ? SIG_YELLOW : SIG_RED);
}
float CityMap::GreenTimeLeft(int i, int j, int axis) const {
    float ph = fmodf(time + signalOffset[j][i], 16.0f);
    if (axis == 0) return ph < 6.0f ? 6.0f - ph : 0.0f;
    return (ph >= 8.0f && ph < 14.0f) ? 14.0f - ph : 0.0f;
}

Vector2 CityMap::RandomSidewalkPoint(Rng& r) const {
    int bi = r.Int(0, BLOCKS_X - 1), bj = r.Int(0, BLOCKS_Y - 1);
    Rectangle ring = SidewalkRing(bi, bj);
    float t = r.Float() * 4.0f;
    int side = (int)t; float f = t - side;
    switch (side) {
        case 0: return { ring.x + ring.width * f, ring.y };
        case 1: return { ring.x + ring.width, ring.y + ring.height * f };
        case 2: return { ring.x + ring.width * (1 - f), ring.y + ring.height };
        default: return { ring.x, ring.y + ring.height * (1 - f) };
    }
}

Vector2 CityMap::RandomSidewalkPointNear(Rng& r, Vector2 centre, float minDist, float maxDist) const {
    Vector2 best = RandomSidewalkPoint(r);
    for (int tries = 0; tries < 8; tries++) {
        // uniform over the ring-shaped area, then snapped to the nearest sidewalk line
        float a = r.Range(0, 2 * PI), d = sqrtf(r.Range(minDist * minDist, maxDist * maxDist));
        Vector2 q = centre + Forward(a) * d;
        int bi = std::clamp((int)(q.x / (BLOCK_PITCH * TILE)), 0, BLOCKS_X - 1);
        int bj = std::clamp((int)(q.y / (BLOCK_PITCH * TILE)), 0, BLOCKS_Y - 1);
        Rectangle ring = SidewalkRing(bi, bj);
        Vector2 p = { Clampf(q.x, ring.x, ring.x + ring.width), Clampf(q.y, ring.y, ring.y + ring.height) };
        float dl = p.x - ring.x, dr = ring.x + ring.width - p.x, dt = p.y - ring.y, db = ring.y + ring.height - p.y;
        float m = std::min(std::min(dl, dr), std::min(dt, db));
        if (m == dl) p.x = ring.x; else if (m == dr) p.x = ring.x + ring.width; else if (m == dt) p.y = ring.y; else p.y = ring.y + ring.height;
        best = p;
        float dd = Dist(p, centre);
        if (dd >= minDist && dd <= maxDist) break;
    }
    return best;
}

bool CityMap::OnCrossing(Vector2 p, float margin, int* walkAxis, int* interI, int* interJ) const {
    int i = std::clamp((int)roundf((p.x - TILE) / (BLOCK_PITCH * TILE)), 0, INTER_X - 1);
    int j = std::clamp((int)roundf((p.y - TILE) / (BLOCK_PITCH * TILE)), 0, INTER_Y - 1);
    Vector2 rel = p - InterCenter(i, j);
    const float T = (float)TILE, half = 22.0f + margin;         // zebra stripes: 1.5 tiles from the centre, 44 px deep
    int axis = -1;
    if (fabsf(rel.x) <= T + margin && fabsf(fabsf(rel.y) - 1.5f * T) <= half) axis = 1;        // across a north-south street
    else if (fabsf(rel.y) <= T + margin && fabsf(fabsf(rel.x) - 1.5f * T) <= half) axis = 0;   // across an east-west street
    if (axis < 0) return false;
    if (walkAxis) *walkAxis = axis;
    if (interI) *interI = i;
    if (interJ) *interJ = j;
    return true;
}

Vector2 CityMap::RandomRoadPoint(Rng& r, float* angle) const {
    bool horizontal = r.Chance(0.5f);
    int i = r.Int(0, INTER_X - 2), j = r.Int(0, INTER_Y - 1);
    if (!horizontal) { i = r.Int(0, INTER_X - 1); j = r.Int(0, INTER_Y - 2); }
    Vector2 a = InterCenter(i, j), b = horizontal ? InterCenter(i + 1, j) : InterCenter(i, j + 1);
    float t = r.Range(0.25f, 0.75f);
    Vector2 p = LerpV(a, b, t);
    Vector2 dir = Norm(b - a) * (r.Chance(0.5f) ? 1.0f : -1.0f);
    float ang = AngleOf(dir);
    if (angle) *angle = ang;
    return p + RightOf(ang) * LANE_OFFSET;       // right-hand traffic
}

void CityMap::QueryBuildings(Rectangle a, std::vector<int>& out) const {
    out.clear();
    if (++stampId == 0x7FFFFFFF) { std::fill(stamp.begin(), stamp.end(), 0); stampId = 1; }
    int x0 = std::max(0, (int)(a.x / TILE)), x1 = std::min(MAP_W - 1, (int)((a.x + a.width) / TILE));
    int y0 = std::max(0, (int)(a.y / TILE)), y1 = std::min(MAP_H - 1, (int)((a.y + a.height) / TILE));
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int k : tileBuildings[y * MAP_W + x])
                if (stamp[k] != stampId) { stamp[k] = stampId; out.push_back(k); }
}

void CityMap::QueryObjects(Rectangle a, std::vector<int>& out) const {
    out.clear();
    if (++stampId == 0x7FFFFFFF) { std::fill(stamp.begin(), stamp.end(), 0); stampId = 1; }
    int x0 = std::max(0, (int)(a.x / TILE)), x1 = std::min(MAP_W - 1, (int)((a.x + a.width) / TILE));
    int y0 = std::max(0, (int)(a.y / TILE)), y1 = std::min(MAP_H - 1, (int)((a.y + a.height) / TILE));
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int k : tileObjects[y * MAP_W + x])
                if (stamp[k] != stampId && objects[k].alive) { stamp[k] = stampId; out.push_back(k); }
}

bool CityMap::PointInBuilding(Vector2 p, float margin) const {
    int tx = (int)(p.x / TILE), ty = (int)(p.y / TILE);
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return false;
    for (int k : tileBuildings[ty * MAP_W + tx]) {
        const Rectangle& r = buildings[k].r;
        if (p.x > r.x - margin && p.x < r.x + r.width + margin && p.y > r.y - margin && p.y < r.y + r.height + margin) return true;
    }
    return false;
}

bool CityMap::AreaFree(Rectangle r) const {
    static std::vector<int> ids;
    QueryBuildings(r, ids);
    for (int k : ids) if (CheckCollisionRecs(r, buildings[k].r)) return false;
    QueryObjects({ r.x - 40, r.y - 40, r.width + 80, r.height + 80 }, ids);
    for (int k : ids) if (CheckCollisionCircleRec(objects[k].pos, objects[k].radius, r)) return false;
    return true;
}

bool CityMap::LineOfSight(Vector2 a, Vector2 b) const {
    static std::vector<int> ids;
    Rectangle box = { std::min(a.x, b.x), std::min(a.y, b.y), fabsf(a.x - b.x) + 1, fabsf(a.y - b.y) + 1 };
    QueryBuildings(box, ids);
    for (int k : ids) if (SegmentRect(a, b, buildings[k].r)) return false;
    return true;
}

bool CityMap::RayCast(Vector2 a, Vector2 b, float& tBest, Vector2& normal, int* objectHit) const {
    static std::vector<int> ids;
    tBest = 1.0f; bool hit = false;
    if (objectHit) *objectHit = -1;
    Rectangle box = { std::min(a.x, b.x) - 2, std::min(a.y, b.y) - 2, fabsf(a.x - b.x) + 4, fabsf(a.y - b.y) + 4 };
    QueryBuildings(box, ids);
    for (int k : ids) {
        float t;
        if (SegmentRect(a, b, buildings[k].r, &t) && t < tBest) {
            tBest = t; hit = true;
            Vector2 p = LerpV(a, b, t); const Rectangle& r = buildings[k].r;
            float dl = fabsf(p.x - r.x), dr = fabsf(p.x - r.x - r.width), dt = fabsf(p.y - r.y), db = fabsf(p.y - r.y - r.height);
            float m = std::min(std::min(dl, dr), std::min(dt, db));
            normal = m == dl ? V2(-1, 0) : m == dr ? V2(1, 0) : m == dt ? V2(0, -1) : V2(0, 1);
            if (objectHit) *objectHit = -1;
        }
    }
    QueryObjects(box, ids);
    for (int k : ids) {
        const CityObject& o = objects[k];
        float t;
        bool hitIt = o.box ? SegmentOBB(a, b, o.Box(), &t) : SegmentCircle(a, b, o.pos, o.radius, &t);
        if (hitIt && t < tBest) {
            tBest = t; hit = true; normal = Norm(LerpV(a, b, t) - o.pos);
            if (objectHit) *objectHit = k;
        }
    }
    return hit;
}

void CityMap::BreakObject(int idx, Vector2 impactVel, Particles& fx, bool byVehicle) {
    if (idx < 0 || idx >= (int)objects.size()) return;
    CityObject& o = objects[idx];
    if (!o.alive || !(o.breakable || (byVehicle && o.strength > 0))) return;
    o.alive = false;
    float speed = Len(impactVel);
    if (o.kind == CityObject::Lamp || o.kind == CityObject::Signal) {
        // breakaway base: the pole topples in the direction it was pushed
        o.fallen = true;
        o.fallAngle = speed > 1 ? AngleOf(impactVel) : o.rot;
        Vector2 tip = o.pos + Forward(o.fallAngle) * (o.h * 0.92f);
        fx.Sparks(o.pos, 12, Norm(impactVel), 12, 260);
        fx.Debris(o.pos, 20, { 90, 94, 100, 255 }, 6, 120 + speed * 0.3f);
        fx.GlassShards(tip, 8);
        return;
    }
    if (o.kind == CityObject::Bush) {                              // flattened shrub
        fx.Debris(o.pos, o.h * 0.6f, { 62, 105, 48, 255 }, 14, 80 + speed * 0.3f);
        fx.Dust(o.pos, impactVel * 0.3f, { 80, 120, 60, 255 });
        return;
    }
    Color c = GRAY;
    switch ((Prop)o.sprite) {
        case Prop::TrashBin: c = { 40, 85, 55, 255 }; break;
        case Prop::Hydrant:  c = { 200, 30, 30, 255 }; fx.Splash(o.pos, 0.6f); break;
        case Prop::Cone:     c = { 245, 110, 20, 255 }; break;
        case Prop::NewsBox:  c = { 220, 170, 30, 255 }; break;
        case Prop::Mailbox:  c = { 30, 70, 160, 255 }; break;
        case Prop::Crate:    c = { 170, 125, 75, 255 }; break;
        case Prop::Barrel:   c = { 150, 30, 26, 255 }; break;
        case Prop::Bollard:  c = { 50, 52, 56, 255 }; fx.Sparks(o.pos, 10, Norm(impactVel), 8, 240); break;
        case Prop::PhoneBooth: c = { 40, 60, 110, 255 }; fx.GlassShards(o.pos, 14); break;
        case Prop::BusShelter: c = { 70, 74, 80, 255 }; fx.GlassShards(o.pos, 20); fx.Sparks(o.pos, 20, Norm(impactVel), 6, 200); break;
        case Prop::Bench: case Prop::PicnicTable: c = { 120, 85, 50, 255 }; break;
        default: break;
    }
    fx.Debris(o.pos, o.h, c, 8, 120 + speed * 0.4f);
}

// -------------------------------------------------------------------------------------
//  Drawing - ground
// -------------------------------------------------------------------------------------
struct TileLook { const Texture2D* tex; float h; float scale; Color tint; };
static TileLook Look(Tile t) {
    const Assets& A = gAssets;
    switch (t) {
        case Tile::Road:     return { &A.asphalt,  0.0f,   20 * M, { 200, 200, 204, 255 } };
        case Tile::Sidewalk: return { &A.sidewalk, CURB_H, 4.5f * M, { 214, 210, 202, 255 } };
        case Tile::Lot:      return { &A.concrete, CURB_H, 16 * M, { 175, 173, 170, 255 } };
        case Tile::Grass:    return { &A.grass,    CURB_H, 12 * M, { 170, 190, 150, 255 } };
        case Tile::Parking:  return { &A.asphalt,  CURB_H, 20 * M, { 175, 175, 180, 255 } };
        case Tile::Plaza:    return { &A.plaza,    CURB_H, 2.4f * M, { 225, 220, 214, 255 } };
    }
    return { &A.asphalt, 0, 20 * M, WHITE };
}

void CityMap::DrawGround(Rectangle view, float t) const {
    // ---- the sea and the seawall (only when the view reaches the island edge) ----
    if (view.x < 0 || view.y < 0 || view.x + view.width > WORLD_W || view.y + view.height > WORLD_H) {
        Rectangle sea = { view.x - 200, view.y - 200, view.width + 400, view.height + 400 };
        DrawGroundTex(gAssets.water, sea, SEA_LEVEL, 18 * M, { 170, 200, 210, 255 }, { t * 0.012f, t * 0.007f });
        DrawGroundTex(gAssets.water, sea, SEA_LEVEL + 0.4f, 11 * M, { 120, 150, 160, 90 }, { -t * 0.009f, t * 0.011f });
        Vector2 c[4] = { { 0, 0 }, { WORLD_W, 0 }, { WORLD_W, WORLD_H }, { 0, WORLD_H } };
        for (int k = 0; k < 4; k++) {
            Vector2 a = c[k], b = c[(k + 1) % 4];
            float len = Dist(a, b) / (8 * M);
            DrawWall(gAssets.concrete, a, b, SEA_LEVEL, CURB_H, { 0, 0, len, 0.5f }, { 90, 95, 100, 255 }, { 185, 182, 176, 255 });
        }
    }
    // ---- tiles, merged into horizontal runs per material ----
    int x0 = std::max(0, (int)(view.x / TILE)), x1 = std::min(MAP_W - 1, (int)((view.x + view.width) / TILE));
    int y0 = std::max(0, (int)(view.y / TILE)), y1 = std::min(MAP_H - 1, (int)((view.y + view.height) / TILE));
    for (int pass = 0; pass < 6; pass++) {
        Tile want = (Tile)pass;
        TileLook L = Look(want);
        for (int y = y0; y <= y1; y++) {
            int x = x0;
            while (x <= x1) {
                if (tiles[y][x] != want) { x++; continue; }
                int s = x;
                while (x <= x1 && tiles[y][x] == want) x++;
                Rectangle r = { (float)(s * TILE), (float)(y * TILE), (float)((x - s) * TILE), (float)TILE };
                DrawGroundTex(*L.tex, r, L.h, L.scale, L.tint);
            }
        }
    }
    // ---- curb faces (vertical 7.5 cm step between sidewalk and road) ----
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (tiles[y][x] == Tile::Road) continue;
            float fx = (float)(x * TILE), fy = (float)(y * TILE);
            Color cc = { 150, 148, 142, 255 };
            if (TileAtIdx(x, y - 1) == Tile::Road && y > 0) DrawWall(gAssets.concrete, { fx, fy }, { fx + TILE, fy }, 0, CURB_H, { 0, 0, 1, 0.05f }, cc, cc);
            if (TileAtIdx(x, y + 1) == Tile::Road && y < MAP_H - 1) DrawWall(gAssets.concrete, { fx, fy + TILE }, { fx + TILE, fy + TILE }, 0, CURB_H, { 0, 0, 1, 0.05f }, cc, cc);
            if (TileAtIdx(x - 1, y) == Tile::Road && x > 0) DrawWall(gAssets.concrete, { fx, fy }, { fx, fy + TILE }, 0, CURB_H, { 0, 0, 1, 0.05f }, cc, cc);
            if (TileAtIdx(x + 1, y) == Tile::Road && x < MAP_W - 1) DrawWall(gAssets.concrete, { fx + TILE, fy }, { fx + TILE, fy + TILE }, 0, CURB_H, { 0, 0, 1, 0.05f }, cc, cc);
        }
    // ---- park footpaths ----
    for (int bj = 0; bj < BLOCKS_Y; bj++)
        for (int bi = 0; bi < BLOCKS_X; bi++) {
            if (blocks[bj][bi] != BlockType::Park) continue;
            Rectangle in = BlockInterior(bi, bj);
            if (!CheckCollisionRecs(in, view)) continue;
            float pw = 3.0f * M;
            Vector2 c = { in.x + in.width * 0.5f, in.y + in.height * 0.5f };
            DrawGroundTex(gAssets.cobble, { in.x, c.y - pw * 0.5f, in.width, pw }, CURB_H + 0.1f, 2.2f * M, { 215, 210, 200, 255 });
            DrawGroundTex(gAssets.cobble, { c.x - pw * 0.5f, in.y, pw, in.height }, CURB_H + 0.12f, 2.2f * M, { 215, 210, 200, 255 });
        }
}

void CityMap::DrawMarkings(Rectangle view) const {
    SetDepthTest(false);
    const float T = (float)TILE, H = 0.2f;
    const Color white = { 235, 235, 228, 215 }, yellow = { 235, 190, 60, 220 };
    for (int j = 0; j < INTER_Y; j++)
        for (int i = 0; i < INTER_X; i++) {
            Vector2 c = InterCenter(i, j);
            if (c.x < view.x - 16 * T || c.x > view.x + view.width + 16 * T || c.y < view.y - 16 * T || c.y > view.y + view.height + 16 * T) continue;
            // centre lines of the road segments going east and south of this intersection
            if (i < INTER_X - 1) {
                float xa = c.x + 2 * T + 8, xb = InterCenter(i + 1, j).x - 2 * T - 8;
                for (float x = xa; x < xb; x += 6 * M) DrawFlatRect({ x, c.y - 1.5f, std::min(3 * M, xb - x), 3 }, H, (j == 0 || j == INTER_Y - 1) ? yellow : white);
            }
            if (j < INTER_Y - 1) {
                float ya = c.y + 2 * T + 8, yb = InterCenter(i, j + 1).y - 2 * T - 8;
                for (float y = ya; y < yb; y += 6 * M) DrawFlatRect({ c.x - 1.5f, y, 3, std::min(3 * M, yb - y) }, H, (i == 0 || i == INTER_X - 1) ? yellow : white);
            }
            // zebra crossings + stop lines on every approach
            for (int d = 0; d < 4; d++) {
                Vector2 dir = d == 0 ? V2(0, -1) : d == 1 ? V2(1, 0) : d == 2 ? V2(0, 1) : V2(-1, 0);   // travel direction
                Vector2 side = dir * -1.0f;                                                              // approach side
                Vector2 zc = c + side * (1.5f * T);
                if (!InCity(zc, -4)) continue;
                Vector2 across = { -dir.y, dir.x };
                for (float o = -T + 10; o <= T - 10; o += 16) {
                    Vector2 p = zc + across * o;
                    Vector2 q[4] = { p + dir * 22 + across * 4, p + dir * 22 - across * 4, p - dir * 22 - across * 4, p - dir * 22 + across * 4 };
                    DrawFlatQuad(q, H, white);
                }
                Vector2 laneRight = RightOf(AngleOf(dir));
                Vector2 sc = c + side * (2 * T + 5) + laneRight * (T * 0.5f);
                Vector2 q[4] = { sc + dir * 3 + laneRight * 30, sc + dir * 3 - laneRight * 30, sc - dir * 3 - laneRight * 30, sc - dir * 3 + laneRight * 30 };
                DrawFlatQuad(q, H, white);
            }
        }
    // parking bays
    for (int bj = 0; bj < BLOCKS_Y; bj++)
        for (int bi = 0; bi < BLOCKS_X; bi++) {
            if (blocks[bj][bi] != BlockType::Parking) continue;
            Rectangle in = BlockInterior(bi, bj);
            if (!CheckCollisionRecs(in, view)) continue;
            float bayW = 2.5f * M, bayL = 5.0f * M;
            for (int row = 0; row < 3; row++) {
                float yMid = in.y + 5.2f * M + row * 17.0f * M;
                for (float x = in.x + 2.0f * M; x <= in.x + in.width - 2.0f * M + 1; x += bayW)
                    DrawFlatRect({ x - 1, yMid - bayL, 2, bayL * 2 }, CURB_H + 0.2f, white);
                DrawFlatRect({ in.x + 2.0f * M, yMid - 1, in.width - 4.0f * M, 2 }, CURB_H + 0.2f, white);
            }
        }
    // curb stones: light strip along every sidewalk edge that meets the road
    int x0 = std::max(0, (int)(view.x / TILE)), x1 = std::min(MAP_W - 1, (int)((view.x + view.width) / TILE));
    int y0 = std::max(0, (int)(view.y / TILE)), y1 = std::min(MAP_H - 1, (int)((view.y + view.height) / TILE));
    const Color curb = { 190, 188, 182, 255 };
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (tiles[y][x] != Tile::Sidewalk) continue;
            float fx = (float)(x * TILE), fy = (float)(y * TILE);
            if (y > 0 && TileAtIdx(x, y - 1) == Tile::Road) DrawFlatRect({ fx, fy, T, 4 }, CURB_H + 0.1f, curb);
            if (y < MAP_H - 1 && TileAtIdx(x, y + 1) == Tile::Road) DrawFlatRect({ fx, fy + T - 4, T, 4 }, CURB_H + 0.1f, curb);
            if (x > 0 && TileAtIdx(x - 1, y) == Tile::Road) DrawFlatRect({ fx, fy, 4, T }, CURB_H + 0.1f, curb);
            if (x < MAP_W - 1 && TileAtIdx(x + 1, y) == Tile::Road) DrawFlatRect({ fx + T - 4, fy, 4, T }, CURB_H + 0.1f, curb);
        }
    // flat props: manholes & drains
    for (const CityObject& o : objects) {
        if (o.kind != CityObject::Prop || (o.sprite != (int)Prop::Manhole && o.sprite != (int)Prop::Drain)) continue;
        if (!CheckCollisionPointRec(o.pos, view)) continue;
        const Texture2D& t = gAssets.props[o.sprite];
        DrawFlatSprite(t, { 0, 0, (float)t.width, (float)t.height }, o.pos, 0.3f, o.w, o.l, o.rot, WHITE);
    }
    SetDepthTest(true);
}

// -------------------------------------------------------------------------------------
//  Drawing - shadows
// -------------------------------------------------------------------------------------
static void ConvexHull(Vector2* p, int n, std::vector<Vector2>& out) {
    std::sort(p, p + n, [](Vector2 a, Vector2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    out.assign(2 * n, {});
    int k = 0;
    for (int i = 0; i < n; i++) { while (k >= 2 && Cross(out[k - 1] - out[k - 2], p[i] - out[k - 2]) <= 0) k--; out[k++] = p[i]; }
    for (int i = n - 2, t = k + 1; i >= 0; i--) { while (k >= t && Cross(out[k - 1] - out[k - 2], p[i] - out[k - 2]) <= 0) k--; out[k++] = p[i]; }
    out.resize(k - 1);
}

void CityMap::DrawShadowCasters(Rectangle view, Vector2 sv) const {
    std::vector<Vector2> hull;
    Rectangle wide = { view.x - 900, view.y - 900, view.width + 1800, view.height + 1800 };
    for (const Building& b : buildings) {
        if (!CheckCollisionRecs(b.r, wide)) continue;
        Vector2 c[4] = { { b.r.x, b.r.y }, { b.r.x + b.r.width, b.r.y }, { b.r.x + b.r.width, b.r.y + b.r.height }, { b.r.x, b.r.y + b.r.height } };
        Vector2 pts[8];
        for (int k = 0; k < 4; k++) { pts[k] = c[k] + sv * b.base; pts[k + 4] = c[k] + sv * b.height; }
        ConvexHull(pts, 8, hull);
        DrawFlatPoly(hull.data(), (int)hull.size(), 0, BLACK);
    }
    // elevated railway deck
    for (size_t k = 0; k < rail.pts.size(); k++) {
        Vector2 a = rail.pts[k], b = rail.pts[(k + 1) % rail.pts.size()];
        if (!CheckCollisionPointRec(a, wide)) continue;
        Vector2 n = Perp(Norm(b - a)) * (1.8f * M);
        Vector2 q[4] = { a + n + sv * H_RAIL_DECK, a - n + sv * H_RAIL_DECK, b - n + sv * H_RAIL_DECK, b + n + sv * H_RAIL_DECK };
        DrawFlatQuad(q, 0, BLACK);
    }
    const Texture2D& soft = gAssets.softCircle;
    Rectangle ss = { 0, 0, (float)soft.width, (float)soft.height };
    for (const CityObject& o : objects) {
        if (!o.alive || !CheckCollisionPointRec(o.pos, wide)) continue;
        switch (o.kind) {
            case CityObject::Tree:
            case CityObject::Bush:
                DrawFlatSprite(soft, ss, o.pos + sv * o.h, 0, o.w * 0.95f, o.l * 0.95f, 0, ColorA(BLACK, 0.85f));
                break;
            case CityObject::Lamp:
            case CityObject::Signal:
                DrawFlatLine(o.pos, o.pos + sv * o.h, 3, 0, BLACK);
                DrawFlatLine(o.pos + sv * o.h, o.head + sv * o.h, 3, 0, BLACK);
                break;
            case CityObject::Pillar:
                DrawFlatLine(o.pos, o.pos + sv * o.h, o.w, 0, BLACK);
                break;
            case CityObject::Prop:
                if (o.h > 3) DrawFlatSprite(soft, ss, o.pos + sv * (o.h * 0.5f), 0, o.w * 1.1f, o.l * 1.1f, o.rot, ColorA(BLACK, 0.7f));
                break;
            case CityObject::Fountain:
                DrawFlatCircle(o.pos + sv * o.h, o.radius, 0, ColorA(BLACK, 0.5f));
                break;
        }
    }
    // train
    for (float s : trainPos) {
        float a; Vector2 p = rail.PointAt(s, &a);
        if (!CheckCollisionPointRec(p, wide)) continue;
        Vector2 f = Forward(a) * (8.8f * M), r = RightOf(a) * (1.45f * M);
        Vector2 o = sv * (H_RAIL_DECK + 3.4f * M);
        Vector2 q[4] = { p + f + r + o, p + f - r + o, p - f - r + o, p - f + r + o };
        DrawFlatQuad(q, 0, BLACK);
    }
}

// -------------------------------------------------------------------------------------
//  Drawing - opaque 3D structures
// -------------------------------------------------------------------------------------
static void DrawPrism(Vector2 c, float r, int sides, float h0, float h1, Color col, bool lid = true) {
    rlBegin(RL_QUADS);
    for (int i = 0; i < sides; i++) {
        float a0 = i * 2 * PI / sides, a1 = (i + 1) * 2 * PI / sides;
        Vector2 p0 = c + V2(cosf(a0), sinf(a0)) * r, p1 = c + V2(cosf(a1), sinf(a1)) * r;
        float shade = 0.7f + 0.3f * Saturate(-(cosf((a0 + a1) * 0.5f) * 0.6f + sinf((a0 + a1) * 0.5f) * 0.8f));
        Color cc = ColorMul(col, shade);
        rlColor4ub(cc.r, cc.g, cc.b, 255);
        rlVertex3f(p0.x, h1, p0.y); rlVertex3f(p0.x, h0, p0.y); rlVertex3f(p1.x, h0, p1.y); rlVertex3f(p1.x, h1, p1.y);
    }
    rlEnd();
    if (lid) {
        rlBegin(RL_TRIANGLES);
        rlColor4ub(col.r, col.g, col.b, 255);
        for (int i = 0; i < sides; i++) {
            float a0 = i * 2 * PI / sides, a1 = (i + 1) * 2 * PI / sides;
            rlVertex3f(c.x, h1, c.y);
            rlVertex3f(c.x + cosf(a0) * r, h1, c.y + sinf(a0) * r);
            rlVertex3f(c.x + cosf(a1) * r, h1, c.y + sinf(a1) * r);
        }
        rlEnd();
    }
}

static void BoxAt(Vector2 c, float hw, float hl, float h0, float h1, Color col, Vector2 sun) {
    DrawBox({ c.x - hw, c.y - hl, hw * 2, hl * 2 }, h0, h1, col, sun);
}

void CityMap::DrawBuilding(const Building& b, Vector2 cam, float night, Vector2 sunDir) const {
    const Rectangle& r = b.r;
    Vector2 p[4] = { { r.x, r.y }, { r.x + r.width, r.y }, { r.x + r.width, r.y + r.height }, { r.x, r.y + r.height } };
    Vector2 n[4] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    float h0 = b.base, h1 = b.height;

    if (b.special == 3) {                                          // glass skybridge
        for (int i = 0; i < 4; i++) {
            float shade = Lerpf(0.7f + 0.3f * Saturate(-Dot(n[i], sunDir)), 0.85f, night);
            Color c = ColorMul(b.wallTint, shade);
            DrawWall(gAssets.facade[1], p[i], p[(i + 1) % 4], h0, h1, { 0, 0, Dist(p[i], p[(i + 1) % 4]) / (6 * M), 1 }, ColorMul(c, 0.8f), c);
        }
        DrawGroundTex(gAssets.concrete, r, h1, 8 * M, b.roofTint);
        return;
    }

    const Texture2D& fac = gAssets.facade[b.facade];
    float tileW = b.facade == 0 ? 7.2f * M : 6.0f * M;
    float storeys = (h1 - h0) / STOREY;
    for (int i = 0; i < 4; i++) {
        Vector2 a = p[i], e = p[(i + 1) % 4];
        Vector2 mid = (a + e) * 0.5f;
        if (Dot(cam - mid, n[i]) <= 0) continue;                  // faces away: hidden by the building
        float len = Dist(a, e);
        float shade = Lerpf(0.66f + 0.34f * Saturate(-Dot(n[i], sunDir) * 1.2f + 0.3f), 0.9f, night);
        Color top = ColorMul(b.wallTint, shade), bottom = ColorMul(b.wallTint, shade * 0.72f);
        float u0 = (i * 0.37f + b.litOffset * 0.13f);
        DrawWall(fac, a, e, h0, h1, { u0, 0, u0 + len / tileW, storeys }, bottom, top);
    }
    // roof: recessed surface, parapet inner walls, parapet top
    float par = 0.9f * M, inset = 0.35f * M;
    float roofH = h1 - par;
    Rectangle in = { r.x + inset, r.y + inset, r.width - 2 * inset, r.height - 2 * inset };
    const Texture2D& roofTex = b.roofTex == 0 ? gAssets.gravel : gAssets.concrete;
    DrawGroundTex(roofTex, in, roofH, b.roofTex == 0 ? 7 * M : 14 * M, b.roofTint);
    Vector2 q[4] = { { in.x, in.y }, { in.x + in.width, in.y }, { in.x + in.width, in.y + in.height }, { in.x, in.y + in.height } };
    Color pin = ColorMul(b.roofTint, 0.62f), ptop = ColorMul(b.roofTint, 1.12f);
    for (int i = 0; i < 4; i++) DrawWall(gAssets.concrete, q[i], q[(i + 1) % 4], roofH, h1, { 0, 0, 1, 0.1f }, ColorMul(pin, 0.8f), pin);
    for (int i = 0; i < 4; i++) {
        Vector2 quad[4] = { p[i], p[(i + 1) % 4], q[(i + 1) % 4], q[i] };
        DrawFlatQuad(quad, h1, ptop);
    }
    // helipad / hospital cross
    if (b.special == 1 || b.special == 2) {
        Vector2 c = { r.x + r.width * 0.5f, r.y + r.height * 0.5f };
        DrawFlatCircle(c, 6.5f * M, roofH + 0.3f, { 70, 72, 76, 255 }, 40);
        DrawFlatRing(c, 5.6f * M, 6.0f * M, roofH + 0.4f, b.special == 1 ? Color{ 240, 240, 240, 255 } : Color{ 220, 40, 40, 255 }, 40);
        Color mk = b.special == 1 ? Color{ 245, 245, 245, 255 } : Color{ 220, 40, 40, 255 };
        if (b.special == 1) {   // "H"
            DrawFlatRect({ c.x - 2.2f * M, c.y - 3 * M, 0.9f * M, 6 * M }, roofH + 0.5f, mk);
            DrawFlatRect({ c.x + 1.3f * M, c.y - 3 * M, 0.9f * M, 6 * M }, roofH + 0.5f, mk);
            DrawFlatRect({ c.x - 1.4f * M, c.y - 0.45f * M, 2.8f * M, 0.9f * M }, roofH + 0.5f, mk);
        } else {                // red cross
            DrawFlatRect({ c.x - 0.9f * M, c.y - 3 * M, 1.8f * M, 6 * M }, roofH + 0.5f, mk);
            DrawFlatRect({ c.x - 3 * M, c.y - 0.9f * M, 6 * M, 1.8f * M }, roofH + 0.5f, mk);
        }
    }
    // boxy rooftop gear (sides); lids are sprites drawn in DrawSprites
    for (const RoofProp& rp : b.props) {
        if (rp.prop == (int)Prop::ACUnit) BoxAt(rp.pos, rp.size * 0.5f, rp.size * 0.5f, roofH, roofH + 1.2f * M, { 165, 168, 172, 255 }, sunDir);
        else if (rp.prop == (int)Prop::WaterTank) {
            DrawPrism(rp.pos + V2(0.9f * M, 0.9f * M), 0.18f * M, 6, roofH, roofH + 1.5f * M, { 60, 55, 50, 255 });
            DrawPrism(rp.pos, rp.size * 0.5f, 16, roofH + 1.5f * M, roofH + 3.2f * M, { 120, 92, 70, 255 }, false);
        } else if (rp.prop == (int)Prop::Vent) DrawPrism(rp.pos, rp.size * 0.3f, 8, roofH, roofH + 0.6f * M, { 150, 152, 156, 255 });
    }
}

void CityMap::DrawStructures(Rectangle view, Vector2 cam, float night, Vector2 sunDir) const {
    // buildings: cull by footprint (perspective only pushes tall things outwards)
    for (const Building& b : buildings)
        if (CheckCollisionRecs(b.r, { view.x - 64, view.y - 64, view.width + 128, view.height + 128 })) DrawBuilding(b, cam, night, sunDir);

    // elevated railway: deck strip with side walls, rails and sleepers
    size_t n = rail.pts.size();
    const float hw = 1.8f * M, top = H_RAIL_DECK, bot = H_RAIL_DECK - 0.9f * M;
    auto normalAt = [&](size_t k) {
        Vector2 a = rail.pts[(k + n - 1) % n], b = rail.pts[(k + 1) % n];
        return Perp(Norm(b - a));
    };
    for (size_t k = 0; k < n; k++) {
        Vector2 a = rail.pts[k], b = rail.pts[(k + 1) % n];
        if (!CheckCollisionPointRec(a, { view.x - 200, view.y - 200, view.width + 400, view.height + 400 })) continue;
        Vector2 na = normalAt(k) * hw, nb = normalAt((k + 1) % n) * hw;
        float u0 = rail.dist[k] / (6 * M), u1 = rail.dist[k + 1] / (6 * M);
        rlSetTexture(gAssets.concrete.id);
        rlBegin(RL_QUADS);
        rlColor4ub(170, 168, 165, 255);
        rlTexCoord2f(u0, 0); rlVertex3f(a.x + na.x, top, a.y + na.y);
        rlTexCoord2f(u0, 0.5f); rlVertex3f(a.x - na.x, top, a.y - na.y);
        rlTexCoord2f(u1, 0.5f); rlVertex3f(b.x - nb.x, top, b.y - nb.y);
        rlTexCoord2f(u1, 0); rlVertex3f(b.x + nb.x, top, b.y + nb.y);
        rlEnd();
        rlSetTexture(0);
        DrawWall(gAssets.concrete, a + na, b + nb, bot, top, { u0, 0, u1, 0.15f }, { 110, 108, 104, 255 }, { 150, 148, 144, 255 });
        DrawWall(gAssets.concrete, a - na, b - nb, bot, top, { u0, 0, u1, 0.15f }, { 110, 108, 104, 255 }, { 150, 148, 144, 255 });
    }
    // lamp posts, signal poles, pillars, boxy props, fountains
    for (const CityObject& o : objects) {
        if (o.fallen && CheckCollisionPointRec(o.pos, { view.x - 200, view.y - 200, view.width + 400, view.height + 400 })) {
            // knocked-over pole lying across the pavement / road
            Vector2 dir = Forward(o.fallAngle), tip = o.pos + dir * (o.h * 0.92f), side = Perp(dir);
            Color pole = o.kind == CityObject::Lamp ? Color{ 96, 100, 106, 255 } : Color{ 60, 62, 66, 255 };
            float w = o.kind == CityObject::Lamp ? 3.4f : 4.2f;
            DrawFlatLine(o.pos, tip, w, CURB_H + 1.2f, ColorMul(pole, 0.75f));
            DrawFlatLine(o.pos - side * (w * 0.15f), tip - side * (w * 0.15f), w * 0.45f, CURB_H + 1.8f, ColorMul(pole, 1.15f));
            continue;
        }
        if (!o.alive || !CheckCollisionPointRec(o.pos, { view.x - 100, view.y - 100, view.width + 200, view.height + 200 })) continue;
        switch (o.kind) {
            case CityObject::Lamp: {
                Color pole = { 96, 100, 106, 255 };
                BoxAt(o.pos, 1.6f, 1.6f, 0, o.h, pole, sunDir);
                Vector2 d = o.head - o.pos;
                Vector2 m = (o.pos + o.head) * 0.5f;
                if (fabsf(d.x) > fabsf(d.y)) BoxAt(m, fabsf(d.x) * 0.5f, 1.1f, o.h - 2.0f, o.h, pole, sunDir);
                else BoxAt(m, 1.1f, fabsf(d.y) * 0.5f, o.h - 2.0f, o.h, pole, sunDir);
            } break;
            case CityObject::Signal: {
                Color pole = { 60, 62, 66, 255 };
                BoxAt(o.pos, 2.0f, 2.0f, 0, o.h, pole, sunDir);
                Vector2 m = (o.pos + o.head) * 0.5f, d = o.head - o.pos;
                if (fabsf(d.x) > fabsf(d.y)) BoxAt(m, fabsf(d.x) * 0.5f, 1.3f, o.h - 2, o.h, pole, sunDir);
                else BoxAt(m, 1.3f, fabsf(d.y) * 0.5f, o.h - 2, o.h, pole, sunDir);
                bool alongX = o.axis == 0;
                BoxAt(o.head, alongX ? 3.6f : 8.0f, alongX ? 8.0f : 3.6f, o.h - 16, o.h, { 34, 36, 38, 255 }, sunDir);
            } break;
            case CityObject::Pillar:
                BoxAt(o.pos, o.w * 0.5f, o.l * 0.5f, 0, o.h, { 150, 148, 145, 255 }, sunDir);
                if (o.axis == 1) {                                     // cross beam of a rail portal
                    Vector2 m = (o.pos + o.head) * 0.5f, d = o.head - o.pos;
                    if (fabsf(d.x) > fabsf(d.y)) BoxAt(m, fabsf(d.x) * 0.5f + 8, 0.6f * M, o.h - 1.2f * M, o.h, { 140, 138, 134, 255 }, sunDir);
                    else BoxAt(m, 0.6f * M, fabsf(d.y) * 0.5f + 8, o.h - 1.2f * M, o.h, { 140, 138, 134, 255 }, sunDir);
                }
                break;
            case CityObject::Fountain: {
                DrawPrism(o.pos, o.radius, 28, 0, o.h + CURB_H, { 175, 170, 160, 255 });
                DrawGroundTex(gAssets.water, { o.pos.x - o.radius * 0.85f, o.pos.y - o.radius * 0.85f, o.radius * 1.7f, o.radius * 1.7f },
                              o.h + CURB_H + 0.2f, 5 * M, { 160, 200, 215, 255 }, { time * 0.05f, time * 0.03f });
                DrawPrism(o.pos, o.radius * 0.18f, 12, o.h, o.h + 2.2f * M, { 185, 180, 170, 255 });
            } break;
            case CityObject::Prop: {
                Prop p = (Prop)o.sprite;
                Color side = BLANK;
                if (p == Prop::PhoneBooth) side = { 40, 60, 110, 255 };
                else if (p == Prop::Dumpster) side = { 35, 80, 52, 255 };
                else if (p == Prop::NewsBox) side = { 190, 145, 25, 255 };
                else if (p == Prop::Mailbox) side = { 28, 60, 140, 255 };
                else if (p == Prop::Crate) side = { 140, 100, 60, 255 };
                if (side.a) {
                    bool rot90 = fabsf(sinf(o.rot)) > 0.7f;
                    float hw = (rot90 ? o.l : o.w) * 0.46f, hl = (rot90 ? o.w : o.l) * 0.46f;
                    BoxAt(o.pos, hw, hl, CURB_H, o.h - 0.5f, side, sunDir);
                }
                if (p == Prop::TrashBin || p == Prop::Hydrant || p == Prop::Barrel || p == Prop::Bollard)
                    DrawPrism(o.pos, o.w * 0.45f, 10, CURB_H, o.h - 0.3f, p == Prop::Hydrant ? Color{ 170, 25, 25, 255 } :
                              p == Prop::TrashBin ? Color{ 34, 72, 46, 255 } : p == Prop::Barrel ? Color{ 150, 30, 26, 255 } : Color{ 50, 52, 56, 255 }, false);
                if (p == Prop::BusShelter) {   // posts
                    Vector2 a = Forward(o.rot + PI * 0.5f) * (o.w * 0.45f);
                    BoxAt(o.pos + a, 1.2f, 1.2f, CURB_H, o.h, { 70, 74, 80, 255 }, sunDir);
                    BoxAt(o.pos - a, 1.2f, 1.2f, CURB_H, o.h, { 70, 74, 80, 255 }, sunDir);
                }
                if (p >= Prop::ParasolRed && p <= Prop::ParasolGreen) BoxAt(o.pos, 0.8f, 0.8f, CURB_H, o.h, { 200, 200, 200, 255 }, sunDir);
            } break;
            default: break;
        }
    }
}

// -------------------------------------------------------------------------------------
//  Drawing - flat sprites (no depth write): props, rooftop lids, foliage, heads, train
// -------------------------------------------------------------------------------------
void CityMap::DrawSprites(Rectangle view, float t) const {
    SetDepthWrite(false);
    Rectangle v = { view.x - 150, view.y - 150, view.width + 300, view.height + 300 };
    // rooftop gear lids
    for (const Building& b : buildings) {
        if (b.props.empty() || !CheckCollisionRecs(b.r, v)) continue;
        float roofH = b.height - 0.9f * M;
        for (const RoofProp& rp : b.props) {
            const Texture2D& tx = gAssets.props[rp.prop];
            float lid = rp.prop == (int)Prop::ACUnit ? 1.2f * M : rp.prop == (int)Prop::WaterTank ? 3.4f * M : 0.65f * M;
            DrawFlatSprite(tx, { 0, 0, (float)tx.width, (float)tx.height }, rp.pos, roofH + lid, rp.size, rp.size, rp.rot, WHITE);
        }
    }
    // street props, then bushes, trees, lamp & signal heads (low -> high)
    for (int pass = 0; pass < 3; pass++)
        for (const CityObject& o : objects) {
            if (pass == 2 && o.fallen && CheckCollisionPointRec(o.pos, v)) {       // head of a fallen pole
                Vector2 tip = o.pos + Forward(o.fallAngle) * (o.h * 0.92f);
                bool lamp = o.kind == CityObject::Lamp;
                const Texture2D& tx = gAssets.props[(int)(lamp ? Prop::LampHead : Prop::SignalHead)];
                DrawFlatSprite(tx, { 0, 0, (float)tx.width, (float)tx.height }, tip, CURB_H + 3.0f,
                               lamp ? 1.4f * M : 0.45f * M, lamp ? 0.55f * M : 1.0f * M, o.fallAngle + (lamp ? 0.0f : PI * 0.5f), ColorMul(WHITE, 0.8f));
                continue;
            }
            if (!o.alive || !CheckCollisionPointRec(o.pos, v)) continue;
            if (pass == 0 && o.kind == CityObject::Prop && o.sprite != (int)Prop::Manhole && o.sprite != (int)Prop::Drain) {
                const Texture2D& tx = gAssets.props[o.sprite];
                DrawFlatSprite(tx, { 0, 0, (float)tx.width, (float)tx.height }, o.pos, o.h, o.w, o.l, o.rot, WHITE);
                if (o.sprite == (int)Prop::Planter) {
                    // planters carry a small shrub unless a tree grows in them
                }
            } else if (pass == 1 && o.kind == CityObject::Bush && !gAssets.bushes.empty()) {
                const Texture2D& tx = gAssets.bushes[o.sprite % gAssets.bushes.size()];
                DrawFlatSprite(tx, { 0, 0, (float)tx.width, (float)tx.height }, o.pos, o.h, o.w, o.l, o.rot, WHITE);
            } else if (pass == 2) {
                if (o.kind == CityObject::Tree && !gAssets.trees.empty()) {
                    const Texture2D& tx = gAssets.trees[o.sprite % gAssets.trees.size()];
                    // gentle sway in the wind; see-through while someone walks underneath
                    float sway = sinf(t * 0.9f + o.pos.x * 0.01f) * 0.03f;
                    float alpha = 1.0f;
                    float r2 = (o.w * 0.42f) * (o.w * 0.42f);
                    for (const Vector2& c : characters) if (Len2(c - o.pos) < r2) { alpha = 0.45f; break; }
                    DrawFlatSprite(tx, { 0, 0, (float)tx.width, (float)tx.height }, o.pos, o.h, o.w, o.l, o.rot + sway, ColorA(WHITE, alpha));
                } else if (o.kind == CityObject::Lamp) {
                    const Texture2D& tx = gAssets.props[(int)Prop::LampHead];
                    DrawFlatSprite(tx, { 0, 0, (float)tx.width, (float)tx.height }, o.head, o.h + 0.5f, 1.4f * M, 0.55f * M, o.rot + PI * 0.5f, WHITE);
                } else if (o.kind == CityObject::Signal) {
                    const Texture2D& tx = gAssets.props[(int)Prop::SignalHead];
                    DrawFlatSprite(tx, { 0, 0, (float)tx.width, (float)tx.height }, o.head, o.h + 0.3f, 0.45f * M, 1.0f * M, o.rot, WHITE);
                }
            }
        }
    // rails & sleepers on the deck, then the train
    size_t n = rail.pts.size();
    for (size_t k = 0; k < n; k += 1) {
        Vector2 a = rail.pts[k], b = rail.pts[(k + 1) % n];
        if (!CheckCollisionPointRec(a, v)) continue;
        Vector2 nn = Perp(Norm(b - a));
        DrawFlatLine(a, b + (b - a) * 0.05f, 2.2f * M, H_RAIL_DECK + 0.2f, { 95, 88, 80, 255 });          // ballast
        for (float o : { -0.72f * M, 0.72f * M }) DrawFlatLine(a + nn * o, b + nn * o, 2.0f, H_RAIL_DECK + 0.6f, { 60, 60, 64, 255 });
        DrawFlatLine(a - nn * (1.2f * M), a + nn * (1.2f * M), 3.0f, H_RAIL_DECK + 0.4f, { 70, 62, 55, 255 });
    }
    const Texture2D& tc = gAssets.trainCar;
    for (float s : trainPos) {
        float ang; Vector2 p = rail.PointAt(s, &ang);
        if (!CheckCollisionPointRec(p, { v.x - 300, v.y - 300, v.width + 600, v.height + 600 })) continue;
        DrawFlatSprite(tc, { 0, 0, (float)tc.width, (float)tc.height }, p, H_RAIL_DECK + 3.4f * M, 3.0f * M, 18.0f * M, ang, WHITE);
    }
    SetDepthWrite(true);
}

// -------------------------------------------------------------------------------------
//  Drawing - lights & emissive
// -------------------------------------------------------------------------------------
void CityMap::DrawLights(Rectangle view, float night, float t) const {
    Rectangle v = { view.x - 300, view.y - 300, view.width + 600, view.height + 600 };
    if (night > 0.02f) {
        for (const CityObject& o : objects) {
            if (!o.alive || !CheckCollisionPointRec(o.pos, v)) continue;
            if (o.kind == CityObject::Lamp) {
                float r = o.h > 6 * M ? 15 * M : 8 * M;
                Renderer::Radial(o.head, 3.0f, r, { 255, 196, 130, 255 }, 1.05f * night);
            } else if (o.kind == CityObject::Signal) {
                int s = SignalState(0, 0, 0);   // colour pool is subtle; use the pole's own axis below
                (void)s;
            } else if (o.kind == CityObject::Fountain) {
                Renderer::Radial(o.pos, 3.0f, o.radius * 2.0f, { 120, 190, 255, 255 }, 0.6f * night);
            }
        }
        // train headlights / window spill on the deck
        for (size_t k = 0; k < trainPos.size(); k++) {
            float ang; Vector2 p = rail.PointAt(trainPos[k], &ang);
            if (!CheckCollisionPointRec(p, v)) continue;
            Renderer::Radial(p, H_RAIL_DECK + 1, 12 * M, { 200, 220, 255, 255 }, 0.5f * night);
            if (k == 0) Renderer::Cone(p + Forward(ang) * (9 * M), H_RAIL_DECK + 1, ang, 30 * M, 18 * M, { 255, 240, 210, 255 }, 1.2f * night);
        }
    }
}

void CityMap::DrawEmissive(Rectangle view, float night, float t) const {
    Rectangle v = { view.x - 64, view.y - 64, view.width + 128, view.height + 128 };
    Vector2 camC = { view.x + view.width * 0.5f, view.y + view.height * 0.5f };
    if (night > 0.03f) {
        // lit windows: re-draw camera-facing walls with the emissive window mask
        for (const Building& b : buildings) {
            if (!CheckCollisionRecs(b.r, v)) continue;
            const Rectangle& r = b.r;
            Vector2 p[4] = { { r.x, r.y }, { r.x + r.width, r.y }, { r.x + r.width, r.y + r.height }, { r.x, r.y + r.height } };
            Vector2 n[4] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
            if (b.special == 3) {
                Color gl = ColorA({ 255, 220, 170, 255 }, 0.35f * night);
                for (int i = 0; i < 4; i++) DrawWall(gAssets.facadeLit[1], p[i], p[(i + 1) % 4], b.base, b.height, { 0, 0, 1, 1 }, gl, gl);
                continue;
            }
            float tileW = b.facade == 0 ? 7.2f * M : 6.0f * M;
            float storeys = (b.height - b.base) / STOREY;
            Color lit = ColorA({ 255, 235, 200, 255 }, night * b.litAmount);
            for (int i = 0; i < 4; i++) {
                Vector2 a = p[i], e = p[(i + 1) % 4];
                if (Dot(camC - (a + e) * 0.5f, n[i]) <= 0) continue;
                float u0 = (i * 0.37f + b.litOffset * 0.13f);
                float len = Dist(a, e) / tileW;
                float lo = b.litOffset + i * 1.7f;
                DrawWall(gAssets.facadeLit[b.facade], a, e, b.base, b.height, { (lo + u0) / 8.0f, 0, (lo + u0 + len) / 8.0f, storeys }, lit, lit);
            }
            if (b.neon.a) {                                          // neon strip on the parapet
                float k = 0.75f + 0.25f * sinf(t * 3.0f + b.litOffset);
                Color nc = ColorA(b.neon, night * k);
                for (int i = 0; i < 4; i++) DrawFlatLine(p[i], p[(i + 1) % 4], 3.0f, b.height + 0.4f, nc);
            }
        }
        // lamp bulbs
        for (const CityObject& o : objects) {
            if (!o.alive || !CheckCollisionPointRec(o.pos, v)) continue;
            if (o.kind == CityObject::Lamp) Renderer::Flare(o.head, o.h + 0.8f, 2.6f * M, { 255, 210, 150, 255 }, night);
        }
        // train windows
        for (float s : trainPos) {
            float ang; Vector2 p = rail.PointAt(s, &ang);
            if (!CheckCollisionPointRec(p, { v.x - 300, v.y - 300, v.width + 600, v.height + 600 })) continue;
            Vector2 f = Forward(ang) * (8.6f * M), r = RightOf(ang) * (1.3f * M);
            Color wc = ColorA({ 255, 240, 210, 255 }, 0.8f * night);
            DrawFlatLine(p - f + r, p + f + r, 3.0f, H_RAIL_DECK + 3.45f * M, wc);
            DrawFlatLine(p - f - r, p + f - r, 3.0f, H_RAIL_DECK + 3.45f * M, wc);
        }
    }
    // traffic signal bulbs (always on), aviation beacons, helipad lights
    for (const CityObject& o : objects) {
        if (o.kind != CityObject::Signal || !o.alive || !CheckCollisionPointRec(o.pos, v)) continue;
        int i = (int)floorf(o.pos.x / (BLOCK_PITCH * TILE) + 0.5f), j = (int)floorf(o.pos.y / (BLOCK_PITCH * TILE) + 0.5f);
        i = std::clamp(i, 0, INTER_X - 1); j = std::clamp(j, 0, INTER_Y - 1);
        int s = SignalState(i, j, o.axis);
        Color c = s == SIG_GREEN ? Color{ 60, 255, 120, 255 } : s == SIG_YELLOW ? Color{ 255, 200, 40, 255 } : Color{ 255, 40, 30, 255 };
        Vector2 along = Forward(o.rot);
        float slot = s == SIG_RED ? -0.3f : s == SIG_YELLOW ? 0.0f : 0.3f;
        Vector2 bp = o.head + along * (slot * M);
        Renderer::Flare(bp, o.h + 0.6f, 1.6f * M, c, 0.9f);
        DrawFlatCircle(bp, 2.2f, o.h + 0.5f, c, 10);
    }
    for (const Building& b : buildings) {
        if (!CheckCollisionRecs(b.r, v)) continue;
        Vector2 c = { b.r.x + b.r.width * 0.5f, b.r.y + b.r.height * 0.5f };
        if (b.beacon && fmodf(t + b.litOffset * 0.3f, 1.6f) < 0.35f)
            Renderer::Flare(c, b.height + 1.0f, 3.0f * M, { 255, 40, 30, 255 }, 1.0f);
        if ((b.special == 1 || b.special == 2) && night > 0.1f) {
            for (int k = 0; k < 8; k++) {
                float a = k * PI / 4 + t * 0.0f;
                Vector2 lp = c + V2(cosf(a), sinf(a)) * (6.8f * M);
                Renderer::Flare(lp, b.height - 0.5f, 1.2f * M, { 120, 255, 140, 255 }, night * (0.6f + 0.4f * sinf(t * 4 + k)));
            }
        }
    }
}

// -------------------------------------------------------------------------------------
//  Minimap (pre-rendered, 2 px per tile)
// -------------------------------------------------------------------------------------
bool CityMap::BuildMinimap(startup::Reporter* loading) {
    if (loading && (!loading->Begin("world.minimap", "Map preview", 1) ||
        !loading->PlanChildren({ { "target" }, { "tiles" }, { "buildings" }, { "rail" } }))) return false;
    const int S = 2;
    {
        startup::AtomicSpan atomic(loading, "framebuffer", "minimap");
        minimap = LoadRenderTexture(MAP_W * S, MAP_H * S);
    }
    bool bufferReady = IsRenderTextureValid(minimap);
    if (bufferReady) {
        rlEnableFramebuffer(minimap.id);
        bufferReady = rlFramebufferComplete(minimap.id);
        rlDisableFramebuffer();
    }
    if (!bufferReady) {
        if (loading) loading->Fail("City map buffer could not be prepared");
        return false;
    }
    minimapScale = (float)S / TILE;
    if (loading && (!loading->ChildDone("target") || !loading->Pulse("Painting map terrain", true))) return false;

    // Close the off-screen pass before any presenter can draw to the window. Resuming
    // the same target without clearing keeps all previous minimap pixels intact.
    auto checkpoint = [&](const char* child, size_t completed, size_t total, const char* unit) {
        EndTextureMode();
        if (!loading->Count((int64_t)completed, (int64_t)total, unit) ||
            !loading->ChildProgress(child, total == 0 ? 0 : (double)completed / total)) return false;
        BeginTextureMode(minimap);
        return true;
    };
    auto nextPhase = [&](const char* child, const char* detail) {
        EndTextureMode();
        if (!loading->ChildDone(child) || !loading->Count(0, 0, "") || !loading->Pulse(detail, true)) return false;
        BeginTextureMode(minimap);
        return true;
    };
    BeginTextureMode(minimap);
    ClearBackground({ 20, 45, 60, 255 });
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            Color c;
            switch (tiles[y][x]) {
                case Tile::Road:     c = { 58, 60, 66, 255 }; break;
                case Tile::Sidewalk: c = { 120, 118, 112, 255 }; break;
                case Tile::Lot:      c = { 100, 98, 95, 255 }; break;
                case Tile::Grass:    c = { 60, 105, 55, 255 }; break;
                case Tile::Parking:  c = { 75, 76, 82, 255 }; break;
                case Tile::Plaza:    c = { 140, 115, 100, 255 }; break;
            }
            DrawRectangle(x * S, y * S, S, S, c);
        }
        if (loading && ((y + 1) % 8 == 0 || y + 1 == MAP_H) &&
            !checkpoint("tiles", (size_t)y + 1, MAP_H, "tile rows")) return false;
    }
    if (loading && !nextPhase("tiles", "Painting buildings on the map")) return false;
    size_t buildingsPainted = 0;
    for (const Building& b : buildings) {
        Color c = b.special == 1 ? Color{ 70, 110, 200, 255 } : b.special == 2 ? Color{ 230, 230, 230, 255 } :
                  b.special == 3 ? Color{ 150, 190, 210, 255 } : Color{ 165, 160, 150, 255 };
        if (!b.Solid() && b.special != 3) c = { 140, 135, 128, 255 };
        DrawRectangleRec({ b.r.x * minimapScale, b.r.y * minimapScale, b.r.width * minimapScale, b.r.height * minimapScale }, c);
        buildingsPainted++;
        if (loading && (buildingsPainted % 32 == 0 || buildingsPainted == buildings.size()) &&
            !checkpoint("buildings", buildingsPainted, buildings.size(), "buildings")) return false;
    }
    if (loading && !nextPhase("buildings", "Painting metro routes on the map")) return false;
    for (size_t k = 0; k < rail.pts.size(); k++) {
        Vector2 a = rail.pts[k] * minimapScale, b = rail.pts[(k + 1) % rail.pts.size()] * minimapScale;
        DrawLineEx(a, b, 1.5f, { 70, 150, 230, 255 });
        if (loading && ((k + 1) % 32 == 0 || k + 1 == rail.pts.size()) &&
            !checkpoint("rail", k + 1, rail.pts.size(), "rail segments")) return false;
    }
    EndTextureMode();
    SetTextureFilter(minimap.texture, TEXTURE_FILTER_BILINEAR);
    if (loading && (!loading->ChildDone("rail") || !loading->Finish())) return false;
    return true;
}

void CityMap::UnloadMinimap() { if (minimap.id) UnloadRenderTexture(minimap); minimap = {}; }
