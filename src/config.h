// =====================================================================================
//  Concrete Jungle - global configuration & tuning constants
//
//  WORLD SCALE: 16 world units ("px") == 1 metre. Everything (lanes, sidewalks,
//  vehicles, people, trees, building storeys) is sized from real-world dimensions.
//  The ground is the X/Y plane of the game logic; the renderer maps it to 3D as
//  (x, height, y) and looks straight down with a perspective camera, exactly like
//  the GTA1/GTA2 engines did.
// =====================================================================================
#pragma once

namespace cfg {

// ---- Window ------------------------------------------------------------------------
constexpr int   SCREEN_W          = 1600;
constexpr int   SCREEN_H          = 900;
constexpr const char* WINDOW_TITLE = "Concrete Jungle";

// ---- Scale -------------------------------------------------------------------------
constexpr float PX_PER_METER      = 16.0f;
constexpr float M                 = PX_PER_METER;   // handy: 3.5f * cfg::M

// ---- City layout -------------------------------------------------------------------
// A block is BLOCK_PITCH tiles: 2 tiles road (8 m, one lane each way) | 1 tile
// sidewalk (4 m) | interior | 1 tile sidewalk. An extra road closes the east/south edge.
constexpr int   TILE              = 64;               // 4 m
constexpr int   BLOCK_PITCH       = 16;               // 64 m
constexpr int   BLOCKS_X          = 9;
constexpr int   BLOCKS_Y          = 9;
constexpr int   MAP_W             = BLOCKS_X * BLOCK_PITCH + 2;   // tiles
constexpr int   MAP_H             = BLOCKS_Y * BLOCK_PITCH + 2;
constexpr float WORLD_W           = (float)(MAP_W * TILE);
constexpr float WORLD_H           = (float)(MAP_H * TILE);
constexpr int   INTER_X           = BLOCKS_X + 1;     // intersections per row
constexpr int   INTER_Y           = BLOCKS_Y + 1;
constexpr float LANE_OFFSET       = TILE * 0.5f;      // lane centre from road centre
constexpr float ROAD_HALF         = (float)TILE;      // half width of a two-lane street
constexpr float STOREY            = 3.3f * M;         // building floor height
constexpr float SEA_LEVEL         = -3.0f * M;        // water around the island

// ---- Population --------------------------------------------------------------------
constexpr int   TRAFFIC_CARS      = 50;
constexpr int   PARKED_CARS       = 40;
constexpr int   PEDESTRIANS       = 220;
constexpr int   MAX_POLICE        = 6;

// ---- Heights of things (for the 3D renderer) ---------------------------------------
constexpr float H_DECAL           = 0.15f;
constexpr float H_CAR             = 1.45f * M;        // car roof
constexpr float H_PED             = 1.75f * M;        // head height
// People are drawn larger than life (like GTA did) so they read well next to cars.
// 1.0 = true scale. Also scales their collision radius.
constexpr float CHAR_SCALE        = 1.35f;
constexpr float H_LAMP            = 8.0f * M;
constexpr float H_SIGNAL          = 5.5f * M;
constexpr float H_RAIL_DECK       = 7.5f * M;         // elevated railway

// ---- Camera (GTA2 style: fixed FOV, height changes with speed) ---------------------
constexpr float CAM_FOVY          = 62.0f;            // widest field of view (degrees)
constexpr float CAM_MIN_HEIGHT    = 62.0f * M;        // camera never goes lower than this
constexpr float CAM_VIEW_FOOT     = 19.0f * M;        // visible world height on foot
constexpr float CAM_VIEW_IDLE     = 60.0f * M;        // in a car, standing still
constexpr float CAM_VIEW_FAST     = 100.0f * M;       // in a car at top speed

// ---- Day / night -------------------------------------------------------------------
constexpr float START_HOUR        = 19.3f;            // start at dusk: lights come on
constexpr float HOURS_PER_SECOND  = 1.0f / 12.0f;     // 24h cycle == 4.8 minutes

} // namespace cfg
