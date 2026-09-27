// =====================================================================================
//  City map: procedural block-grid city with real 3D buildings.
//
//  Layout (see config.h): a grid of BLOCKS_X x BLOCKS_Y blocks separated by two-lane
//  streets. Each block is ringed by a raised sidewalk and filled with one of: office /
//  apartment buildings, a park, a plaza, a parking lot, or a special building (police
//  station, hospital). The whole city is an island surrounded by a seawall and water.
//
//  3D structures: buildings (with parapets and rooftop gear), "gate" buildings that
//  span a street (drive under them), glass skybridges, and an elevated metro loop on
//  pillars with a running train.
//
//  The map also answers gameplay queries: surface type/grip, solid obstacles, line of
//  sight, traffic intersections & signals, sidewalk rings for pedestrians, spawn points.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "config.h"
#include "math_utils.h"
#include "sprite_gen.h"
#include <vector>

class Particles;
struct DayNight;

enum class Tile : uint8_t { Road, Sidewalk, Lot, Grass, Parking, Plaza };
enum class BlockType : uint8_t { Buildings, Park, Parking, Plaza, Police, Hospital };

struct RoofProp { Vector2 pos; float rot; int prop; float size; };

struct Building {
    Rectangle r{};
    float base = 0;           // > 0 for gate buildings / skybridges (you can drive under)
    float height = 100;       // top of the parapet
    int   facade = 0;         // 0 brick, 1 glass
    Color wallTint = WHITE;
    Color roofTint = WHITE;
    int   roofTex = 0;        // 0 gravel, 1 concrete
    float litOffset = 0;      // offset into the emissive window texture
    float litAmount = 1;      // how many windows are lit
    int   special = 0;        // 0 none, 1 police, 2 hospital, 3 skybridge
    bool  beacon = false;     // red aviation light on the roof
    Color neon = BLANK;       // neon strip along the roof edge (alpha 0 = none)
    std::vector<RoofProp> props;
    bool Solid() const { return base <= 0.0f; }
};

// Street furniture & vegetation. Anything with radius > 0 is a solid obstacle.
// Vehicle impacts (see physics.h): 'strength' is the impulse (tonnes * px/s) the object
// can take before it gives way - a 1 t car loses at most that much speed breaking it.
// 0 = rigid (trees, concrete planters, pillars). Soft objects (shrubs) are pushed
// through with drag and flattened by anything faster/heavier than 'strength'.
struct CityObject {
    enum Kind : uint8_t { Tree, Bush, Prop, Lamp, Signal, Pillar, Fountain };
    Kind    kind = Prop;
    Vector2 pos{};
    float   rot = 0;
    int     sprite = 0;       // tree/bush index or spritegen::Prop
    float   w = 10, l = 10;   // sprite size (px)
    float   h = 1;            // draw height
    float   radius = 0;       // collision radius (0 = walk/drive through)
    bool    breakable = false;  // bullets break it
    bool    alive = true;
    int     axis = 0;         // signals: 0 = controls N/S traffic, 1 = E/W
    Vector2 head{};           // lamps / signals: position of the head
    float   strength = 0;     // breakaway impulse for vehicles (0 = rigid)
    float   mass = 0;         // tonnes of the part that flies off when it breaks
    bool    soft = false;     // vehicles push through it (shrubs)
    bool    box = false;      // solid as a w x l box turned by 'rot' ('radius' then only bounds it)
    bool    walkIn = false;   // solid for vehicles only - people walk in (bus shelter)
    bool    fallen = false;   // knocked-over pole (drawn lying on the ground)
    float   fallAngle = 0;
    OBB     Box() const { return MakeOBB(pos, rot, w * 0.5f, l * 0.5f); }
};

struct ParkingSpot { Vector2 pos; float angle; };

enum Signal { SIG_GREEN = 0, SIG_YELLOW, SIG_RED };

// Elevated metro: closed loop polyline + a train running on it.
struct RailLoop {
    std::vector<Vector2> pts;     // dense centre line (closed)
    std::vector<float>   dist;    // cumulative length at each point
    float length = 0;
    Vector2 PointAt(float s, float* angle = nullptr) const;
};

class CityMap {
public:
    void Generate(uint32_t seed);
    void Update(float dt, Particles& fx);

    // ---- tiles & surfaces ----
    Tile      TileAtIdx(int tx, int ty) const;
    Tile      TileAt(Vector2 p) const;
    bool      InCity(Vector2 p, float margin = 0) const;
    float     Grip(Vector2 p) const;            // 1 = asphalt, lower on grass
    float     GroundHeight(Vector2 p) const;    // 0 road, curb height elsewhere
    BlockType Block(int bi, int bj) const { return blocks[bj][bi]; }

    // ---- collision / visibility ----
    void QueryBuildings(Rectangle area, std::vector<int>& out) const;   // solid buildings only
    void QueryObjects(Rectangle area, std::vector<int>& out) const;     // solid objects only
    bool PointInBuilding(Vector2 p, float margin = 0) const;
    bool AreaFree(Rectangle r) const;                                    // no building / solid object inside
    bool LineOfSight(Vector2 a, Vector2 b) const;
    // Bullet ray: nearest hit against buildings & solid objects. Returns t in [0,1].
    bool RayCast(Vector2 a, Vector2 b, float& t, Vector2& normal, int* objectHit) const;
    // Bullets break 'breakable' props; vehicles (byVehicle) also knock over anything with
    // a breakaway strength: poles fall, shrubs get flattened.
    void BreakObject(int idx, Vector2 impactVel, Particles& fx, bool byVehicle = false);

    // ---- traffic ----
    Vector2 InterCenter(int i, int j) const;
    bool    ValidInter(int i, int j) const { return i >= 0 && j >= 0 && i < cfg::INTER_X && j < cfg::INTER_Y; }
    int     SignalState(int i, int j, int axis) const;      // axis 0 = N/S travel, 1 = E/W
    float   GreenTimeLeft(int i, int j, int axis) const;

    // ---- pedestrians ----
    Vector2   SidewalkCorner(int bi, int bj, int corner) const;   // 0 NW,1 NE,2 SE,3 SW
    Rectangle SidewalkRing(int bi, int bj) const;                 // centre line of the ring
    Rectangle BlockInterior(int bi, int bj) const;
    Vector2   RandomSidewalkPoint(Rng& r) const;
    Vector2   RandomRoadPoint(Rng& r, float* angle) const;        // on a lane, facing traffic

    // ---- drawing (called by the game inside the right render pass) ----
    void DrawGround(Rectangle view, float time) const;            // scene: opaque ground
    void DrawMarkings(Rectangle view) const;                      // scene: paint & manholes
    void DrawShadowCasters(Rectangle view, Vector2 sv) const;     // shadow pass
    void DrawStructures(Rectangle view, Vector2 camPos, float night, Vector2 sunDir) const; // scene: opaque 3D
    void DrawSprites(Rectangle view, float time) const;           // scene: props/trees/train (no depth write)
    void DrawLights(Rectangle view, float night, float time) const;      // light pass
    void DrawEmissive(Rectangle view, float night, float time) const;    // emissive pass

    // ---- minimap ----
    RenderTexture2D minimap{};
    float           minimapScale = 1;   // minimap px per world px
    void BuildMinimap();
    void UnloadMinimap();

    // ---- data ----
    std::vector<Building>    buildings;
    std::vector<CityObject>  objects;
    std::vector<ParkingSpot> parking;
    std::vector<Vector2>     characters;     // set each frame by the game: people that trees fade over
    RailLoop                 rail;
    std::vector<float>       trainPos;       // distance along the loop of each carriage
    float                    trainSpeed = 0;
    Vector2 policeStation{}, hospital{};     // respawn points (in front of the entrance)
    float   time = 0;

private:
    Tile      tiles[cfg::MAP_H][cfg::MAP_W]{};
    BlockType blocks[cfg::BLOCKS_Y][cfg::BLOCKS_X]{};
    float     signalOffset[cfg::INTER_Y][cfg::INTER_X]{};
    std::vector<int> tileBuildings[cfg::MAP_H * cfg::MAP_W];
    std::vector<int> tileObjects[cfg::MAP_H * cfg::MAP_W];
    mutable std::vector<int> stamp;
    mutable int stampId = 1;

    void GenBlock(int bi, int bj, Rng& rng);
    void GenBuildingsIn(Rectangle lot, int depth, float heightMul, Rng& rng);
    void AddBuilding(Rectangle r, float floors, Rng& rng, int special = 0);
    void GenStreetFurniture(Rng& rng);
    void GenRail();
    void GenGatesAndBridges(Rng& rng);
    void AddObject(const CityObject& o);
    void IndexBuildings();
    bool RailOverRoad(int tx, int ty) const;
    void DrawBuilding(const Building& b, Vector2 camPos, float night, Vector2 sunDir) const;
};
