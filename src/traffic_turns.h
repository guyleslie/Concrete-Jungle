// =====================================================================================
//  Turn paths of rail traffic through a junction (CJ-020).
//
//  A rail car's rear axle traces its path (traffic.h ShiftedRailPose), so a turn path is
//  the path of the rear axle: an arc with gradual steering in and out (clothoids), its
//  radius no tighter than the class can turn (vehicles.cfg TURN turning circle). Each
//  class gets one right-turn and one left-turn path, checked with the production pose
//  rule against the junction's kerbs and the halves of the roads:
//    - right: the largest radius that keeps the body clear of the inner kerb corner; a
//      class that cannot turn that tightly first moves towards the road centre (a wide
//      turn, which takes the junction box alone) and returns to its lane afterwards;
//    - left: an arc of about 6 m, or the class's tightest radius when that is larger.
//  A class whose turn does not fit within the limits (a 12 m bus turning right between
//  square corners) does not take that turn in traffic unless it has no other way.
//  The speed allowed at each point keeps the lateral acceleration at the rear axle
//  within TURN lateral_accel (traffic.cfg).
//
//  The limits: the wheels (the body between the axles) stay off the kerbs; a clean turn
//  keeps a body corner out of the oncoming half of a road (outside the stop-line zone,
//  see the fixture in testing.md) and a car's body off the kerbs; a large vehicle's
//  overhangs may sweep over a corner. A wide turn reaches at most 1.5 m into the
//  oncoming half and takes the junction box alone.
// =====================================================================================
#pragma once
#include "vehicle.h"
#include <vector>
namespace startup { class Reporter; }

struct TurnPoint {
    Vector2 p{};            // junction frame: centre at the origin, approach northwards in the lane x = +LANE_OFFSET
    float   vmax = 0;       // px/s allowed with the rear axle here (0: no limit)
    bool    stop = false;   // where the path enters the junction box: the stop waypoint
    bool    curve = false;  // part of the turn itself (for the old speed rule and drawing)
};

struct TurnPath {
    std::vector<TurnPoint> points;
    float radius = 0;       // px, the arc's radius
    float swing = 0;        // px the path moves towards the road centre before a wide turn
    float kerb = 0;         // px the body reaches over a kerb (a long vehicle's overhangs may)
    float wheelKerb = 0;    // px the body between the axles reaches over a kerb
    float encroach = 0;     // px a body corner reaches into the wrong half of a road arm
    bool  wide = false;     // leaves its lane: takes the junction box alone
    bool  fits = true;      // within the limits; traffic avoids a turn that does not fit
};

// The turn path of this vehicle's class and size: 1 right, 2 left. Built on first use.
const TurnPath& RailTurnPath(const Vehicle& v, int turnType);
// Builds the turn paths of every traffic class and sprite (about half a second, at load).
bool RailPlanTurns(startup::Reporter* loading = nullptr);
