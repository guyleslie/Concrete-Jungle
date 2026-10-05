// =====================================================================================
//  AI drivers.
//
//  TRAFFIC runs "on rails" (like the GTA games): an undisturbed car is not simulated by
//  the physics engine but glides kinematically along a smooth lane path, so it can
//  never jitter. The path is planned one junction ahead (straight / left / right,
//  curves through the junction). Long vehicles are posed from two points on the path
//  (front & rear axle), so buses and trucks sweep realistically through turns.
//  Junction rules: obey the lights, only enter the junction box when it's free and
//  there is room beyond it, and left turns yield to oncoming traffic. Cooperative
//  resolution of mutually blocked routes is a later CJ-016 increment.
//
//  When something hits a rail car hard enough (collision, explosion) or it keeps pushing
//  on something, it is KNOCKED off the rail and becomes a normal physics body (see
//  physics.h). The driver stabilises, checks bounded physical forward/reverse moves
//  against complete swept footprints, and holds if none is feasible. Rejoining starts
//  at the actual aligned pose. Blockage time never removes or relocates the driver.
//
//  POLICE are physics driven all the time: they use the lane planner to close in on
//  the player, ignore red lights, and switch to direct pursuit / ramming on sight.
// =====================================================================================
#pragma once
#include "vehicle.h"

class Game;
class CityMap;

void AIResetPath(Vehicle& v, const CityMap& map);           // re-plan from the current pose
void AIObserveTraffic(Game& g);                            // common frame snapshot, before driver updates
void AIUpdateTraffic(Game& g, int idx, float dt);
void AIUpdatePolice(Game& g, int idx, float dt);
void AIKnock(Vehicle& v);                                    // rail -> physics
inline bool AIOnRail(const Vehicle& v) { return v.driver == DriverType::Traffic && v.ai.rail && !v.wrecked && !v.burning; }
// Rail car: its pose after driving 'ahead' px further along its planned path (people use
// it to predict where a turning car will go).
Vector2 AIPathPose(const Vehicle& v, float ahead, float* angle);
// Places v on a random lane between minDist..maxDist from 'near' (optionally off-screen).
bool AIPlaceOnRoad(Game& g, Vehicle& v, Vector2 near, float minDist, float maxDist, bool offscreen);
