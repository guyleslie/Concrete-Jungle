// Dev tool: renders every procedural sprite to PNG for visual inspection.
// Build: see README ("Sprite preview tool").
#include "../src/sprite_gen.h"
#include "raylib.h"
#include <cstdio>
using namespace spritegen;

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : ".";
    char p[512], nm[64];
    auto save = [&](Image img, const char* name) { snprintf(p, sizeof p, "%s/%s.png", out, name); ExportImage(img, p); UnloadImage(img); };
    save(Car({ 200, 30, 35, 255 }, CarStyle::Hatchback), "car_hatch");
    save(Car({ 30, 70, 160, 255 }, CarStyle::Sedan), "car_sedan");
    save(Car({ 235, 235, 235, 255 }, CarStyle::Coupe), "car_coupe");
    save(Car({ 60, 90, 60, 255 }, CarStyle::SUV), "car_suv");
    save(Car({ 25, 25, 28, 255 }, CarStyle::Limo), "car_limo");
    save(Bus({ 235, 125, 30, 255 }, { 250, 240, 230, 255 }), "bus");
    save(BoxTruck({ 220, 220, 225, 255 }, { 235, 235, 238, 255 }, { 200, 40, 40, 255 }), "boxtruck");
    save(FireTruck(), "firetruck");
    save(GarbageTruck({ 40, 120, 60, 255 }), "garbage");
    save(Motorbike({ 200, 30, 30, 255 }, true, { 40, 40, 45, 255 }, { 230, 230, 230, 255 }), "bike_sport");
    save(Motorbike({ 20, 20, 22, 255 }, true, { 90, 60, 40, 255 }, { 30, 30, 30, 255 }, BikeStyle::Chopper), "bike_chopper");
    save(Motorbike({ 120, 200, 220, 255 }, true, { 200, 60, 80, 255 }, { 240, 240, 240, 255 }, BikeStyle::Scooter), "bike_scooter");
    save(Motorbike({ 30, 90, 200, 255 }, false, {}, {}), "bike_empty");
    save(TrainCar({ 30, 110, 200, 255 }), "train");
    save(PedAtlas(PlayerLook()), "ped_player");
    for (int i = 0; i < 4; i++) { snprintf(nm, sizeof nm, "ped_%d", i); save(PedAtlas(RandomPedLook(i + 1)), nm); }
    for (int i = 0; i < 5; i++) { snprintf(nm, sizeof nm, "tree_%d", i); save(Tree(i, 256, 7), nm); }
    for (int i = 0; i < (int)Prop::COUNT; i++) { snprintf(nm, sizeof nm, "prop_%02d", i); save(MakeProp((Prop)i), nm); }
    return 0;
}
