// =====================================================================================
//  Procedural sprite generator.
//
//  A tiny anti-aliased "vector painter": every shape is a signed distance function that
//  is rasterised with analytic coverage and shaded with a bevel/specular model. This
//  yields smooth, glossy, high-resolution sprites in the same style as the Unlucky
//  Studio vehicles, for everything the asset packs don't cover (motorbikes + riders,
//  civilians, street furniture, the metro train), and fallbacks (cars, buses, trucks,
//  trees) used when an image file is missing or a GEN line asks for them.
//
//  All generators take real-world proportions (16 world px == 1 m) into account; the
//  canvas resolution is ~3x the on-screen size so mipmapping keeps them crisp.
// =====================================================================================
#pragma once
#include "raylib.h"
#include <cstdint>

namespace spritegen {

enum class CarStyle { Hatchback, Sedan, Coupe, SUV, Limo };
Image Car(Color paint, CarStyle style);
Image Bus(Color body, Color stripe);
Image BoxTruck(Color cab, Color box, Color stripe);
enum class BikeStyle { Sport, Chopper, Scooter };
Image Motorbike(Color paint, bool rider, Color jacket, Color helmet, BikeStyle style = BikeStyle::Sport);
Image FireTruck();
Image GarbageTruck(Color body);
Image TrainCar(Color stripe);        // 18 m metro carriage, canvas 100 x 600

struct PedLook {
    Color skin, hair, shirt, pants, shoes, cap, bag;
    int   hairStyle;   // 0 short, 1 long, 2 bald, 3 cap
    bool  backpack;
    float build;       // shoulder width multiplier (0.85 .. 1.1)
};
PedLook RandomPedLook(uint32_t seed);
constexpr int PED_FRAME = 96;          // square frame size in the atlas
constexpr int PED_WALK_FRAMES = 8;     // frames 0..7 walk, 8 idle, 9 knocked down, 10/11 punch, 12/13 raised fist
constexpr int PED_FRAME_IDLE = 8, PED_FRAME_DOWN = 9, PED_FRAME_PUNCH = 10, PED_FRAME_FIST = 12;
constexpr int PED_ATLAS_FRAMES = 14;
PedLook PlayerLook();                  // matches the Survivor sprite (unarmed player)
Image PedAtlas(const PedLook& look);

Image Tree(int variant, int size, uint32_t seed);   // variant 0..4
enum class Prop {
    Bench, TrashBin, Hydrant, Cone, Barrel, Dumpster, ACUnit, LampHead, SignalHead, Planter,
    BusShelter, PhoneBooth, ParasolRed, ParasolBlue, ParasolGreen, Bollard, Mailbox, NewsBox,
    Manhole, Drain, PicnicTable, Crate, Rock, WaterTank, Vent, COUNT
};
Image MakeProp(Prop p);

} // namespace spritegen
