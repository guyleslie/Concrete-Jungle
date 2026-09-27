// =====================================================================================
//  Pedestrians: walk around their block's sidewalk ring, wait for the lights and use
//  the zebra crossings, wander into parks, idle, flee from danger, get knocked down.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "math_utils.h"

class Game;
class CityMap;

enum class PedState : uint8_t { Walk, Wait, Cross, Idle, Wander, Flee, Rejoin, Down, Dead };

struct Pedestrian {
    bool     active = false;
    Vector2  pos{}, vel{};
    float    angle = 0;
    float    walkSpeed = 24;
    int      skin = 0;
    PedState state = PedState::Walk;
    float    timer = 0, anim = 0;
    float    health = 100;
    int      bi = 0, bj = 0, corner = 0, dirSign = 1;
    Vector2  target{};
    int      ci = 0, cj = 0, caxis = 0;   // intersection & signal axis while crossing
    int      nextBi = 0, nextBj = 0, nextCorner = 0;
    Vector2  threat{};
    bool     hitByPlayer = false;
    float    deadTime = 0;
    float    laneOffset = 0;         // personal walking line on the sidewalk (px)
    float    sway = 0;
    float    stuckT = 0;             // time spent pushing without progress
    Vector2  lastPos{};
};

constexpr float PED_RADIUS = 0.32f * 16.0f * 1.35f;   // keep in sync with cfg::CHAR_SCALE

void InitPed(Pedestrian& p, Vector2 pos, int skin, const CityMap& map);
void UpdatePed(Pedestrian& p, Game& g, float dt);
void ScarePed(Pedestrian& p, Vector2 from, float duration);
void KnockDownPed(Pedestrian& p, Vector2 impulse);
void DrawPed(const Pedestrian& p);
void DrawPedShadow(const Pedestrian& p, Vector2 sv);
