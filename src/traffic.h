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
// Fixtures: put v on rails at its current pose and lane direction, with 'tail' px of
// already-driven path behind it (a yielding driver can retrace that much). With
// 'insideJunction' a car standing in a junction box goes straight through it. 'forcedTurn'
// fixes the manoeuvre at the first junction (0 straight, 1 right, 2 left; -1 random).
void AIStartRail(Game& g, Vehicle& v, float tail, bool insideJunction = false, int forcedTurn = -1);
// Fixtures: a constant lane shift (a car already passing, for example).
void AISetLaneShift(Vehicle& v, float shift);
// Turning (CJ-020): the tightest radius the rear axle can follow (px, from the class's
// turning circle) and the lateral acceleration allowed in a turn (px/s^2, traffic.cfg).
float RailTurnMinRadius(const Vehicle& v);
float RailTurnLateralAccel();
// Turns planned by traffic so far: clean, wide, not fitting (no other way), other (U-turn).
void AITurnCounts(int counts[4]);

// Lane shift of a rail car at path distance s of its rear axle: 'from' before s0, 'to'
// beyond s1 (s1 < s0 for a shift driven in reverse), a smooth S-curve between. 'slope'
// receives its derivative along the path.
inline float LaneShiftAt(float from, float to, float s0, float s1, float s, float* slope = nullptr) {
    if (slope) *slope = 0;
    if (from == to || s0 == s1) return to;
    float t = (s - s0) / (s1 - s0);
    if (t <= 0) return from;
    if (t >= 1) return to;
    if (slope) *slope = (to - from) * 6 * t * (1 - t) / (s1 - s0);
    return from + (to - from) * t * t * (3 - 2 * t);
}

// Half the span over which a path's tangent is taken: turn paths have points about
// 4 px apart, so the tangent turns smoothly from one segment to the next.
constexpr float RAIL_TANGENT_SPAN = 2.0f;

// Pose of a rail car at path distance s (its centre). The rear axle (0.32 lengths behind
// the centre) traces the path, shifted sideways by the lane shift, and the body points
// along that shifted path's tangent, as a car's rear wheels follow and its front swings
// out: in turns as in lane changes, the rear axle does not slide sideways. The shifted
// path's tangent is t (1 - k w) + n w', for the path's tangent t, normal n and signed
// curvature k (positive turning right), the shift w and its slope w'.
template <class Sampler>
inline Vector2 ShiftedRailPose(Sampler sample, float s, float length, float from, float to, float s0, float s1,
                               float fallbackAngle, Vector2* heading) {
    float axle = length * 0.32f, rearS = s - axle;
    Vector2 rp = sample(rearS);
    Vector2 dir = Norm(sample(rearS + RAIL_TANGENT_SPAN) - sample(rearS - RAIL_TANGENT_SPAN));
    if (Len2(dir) < 0.5f) dir = Forward(fallbackAngle);
    float slope = 0;
    float rear = LaneShiftAt(from, to, s0, s1, rearS, &slope);
    Vector2 body = dir;
    if (rear != 0 || slope != 0) {
        float curvature = 0;
        if (rear != 0) {
            Vector2 before = sample(rearS) - sample(rearS - RAIL_TANGENT_SPAN * 2);
            Vector2 after = sample(rearS + RAIL_TANGENT_SPAN * 2) - sample(rearS);
            if (Len2(before) > 1e-4f && Len2(after) > 1e-4f)
                curvature = Cross(Norm(before), Norm(after)) / (RAIL_TANGENT_SPAN * 2);
        }
        body = Norm(dir * std::max(0.05f, 1 - curvature * rear) + Perp(dir) * slope);
    }
    *heading = body;
    return rp + Perp(dir) * rear + body * axle;
}
// The one vehicle this traffic driver waits for, or -1 (a wait-for edge): a rail car the
// car it is stopped behind; a holding knocked car the vehicle that blocked most of its
// moves. A driver acting on a yielding role waits on nobody.
int AIWaitTarget(const Game& g, int idx);
// Once per frame after AIObserveTraffic: finds wait-for cycles the pair rule does not
// cover (two knocked cars, three or more drivers) and gives one driver a yielding role.
void AIResolveWaitCycles(Game& g, float dt);
// Places v on a random lane between minDist..maxDist from 'near' (optionally off-screen).
bool AIPlaceOnRoad(Game& g, Vehicle& v, Vector2 near, float minDist, float maxDist, bool offscreen);
