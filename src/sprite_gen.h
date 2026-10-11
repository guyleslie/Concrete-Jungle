// =====================================================================================
//  Procedural sprite generator.
//
//  A tiny anti-aliased "vector painter": every shape is a signed distance function that
//  is rasterised with analytic coverage and shaded with a bevel/specular model. This
//  yields smooth, glossy, high-resolution sprites in the same style as the Unlucky
//  Studio vehicles, for everything the asset packs don't cover (street furniture, the
//  metro train), and fallbacks (cars, buses, trucks, motorbikes, trees) used when an
//  image file is missing or a GEN line asks for them. People have no procedural
//  fallback: their rendered atlases are required (ADR-0009).
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

// The animations of a pedestrian atlas; image atlases declare where each one is (civilians.cfg ANIM).
enum class PedAnim : uint8_t { Walk, Idle, Lying, Punch, Fist, Jog, Run, COUNT };

Image Tree(int variant, int size, uint32_t seed);   // variant 0..4
enum class Prop {
    Bench, TrashBin, Hydrant, Cone, Barrel, Dumpster, ACUnit, LampHead, SignalHead, Planter,
    BusShelter, PhoneBooth, ParasolRed, ParasolBlue, ParasolGreen, Bollard, Mailbox, NewsBox,
    Manhole, Drain, PicnicTable, Crate, Rock, WaterTank, Vent, COUNT
};
Image MakeProp(Prop p);

} // namespace spritegen
