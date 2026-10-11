// =====================================================================================
//  Procedural sprite generator.
//
//  A tiny anti-aliased "vector painter": every shape is a signed distance function that
//  is rasterised with analytic coverage and shaded with a bevel/specular model. This
//  yields smooth, glossy, high-resolution sprites in the same style as the Unlucky
//  Studio vehicles, for what the asset packs don't cover: the street furniture and the
//  metro train. Nothing is painted in place of a missing file (ADR-0010).
//
//  All generators take real-world proportions (16 world px == 1 m) into account; the
//  canvas resolution is ~3x the on-screen size so mipmapping keeps them crisp.
// =====================================================================================
#pragma once
#include "raylib.h"
#include <cstdint>

namespace spritegen {

Image TrainCar(Color stripe);        // 18 m metro carriage, canvas 100 x 600

// The animations of a pedestrian atlas; image atlases declare where each one is (civilians.cfg ANIM).
enum class PedAnim : uint8_t { Walk, Idle, Lying, Punch, Fist, Jog, Run, COUNT };

enum class Prop {
    Bench, TrashBin, Hydrant, Cone, Barrel, Dumpster, ACUnit, LampHead, SignalHead, Planter,
    BusShelter, PhoneBooth, ParasolRed, ParasolBlue, ParasolGreen, Bollard, Mailbox, NewsBox,
    Manhole, Drain, PicnicTable, Crate, Rock, WaterTank, Vent, COUNT
};
Image MakeProp(Prop p);

} // namespace spritegen
