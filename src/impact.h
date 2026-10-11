// =====================================================================================
//  Vehicles hitting people (CJ-035): what a vehicle does to a person it meets. For now
//  the injury of a person a vehicle runs into, and the height of a body in the air, so
//  that the person-hit scenes (person_hit_tests) can measure the game before the model
//  of docs/backlog.md#cj-035-vehicles-hitting-people replaces them.
// =====================================================================================
#pragma once

struct Vehicle;

// The part of a person that vehicles meet, the same for civilians and the player.
struct PersonBody {
    float z = 0, vz = 0;             // px above the road and px/s upwards: a body in the air
    bool  Airborne() const { return z > 0 || vz > 0; }
};

// Health points a person loses when the vehicle runs into them at 'closing' px/s.
float PersonImpactDamage(const Vehicle& v, float closing);
