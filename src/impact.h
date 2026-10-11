// =====================================================================================
//  Vehicles hitting people (CJ-035). A contact between a vehicle and a person is one
//  impact, never a push: a person's legs cannot keep up with a vehicle, so in real life
//  the contact lasts a fraction of a second, and then they stagger, fall or are thrown.
//    - Someone who walks into a vehicle that is not moving towards them is stopped by it.
//    - Momentum is conserved: a standing person takes the share mv / (mv + mp) of the
//      closing speed and the vehicle loses what they gain. Below their balance limit they
//      stagger and step out of the way; another contact while they stagger knocks them
//      down. Past the limit they are launched as in Searle's model: a front edge below the
//      person's centre of mass wraps them onto the vehicle, a higher one throws them
//      forward; each has a launch angle and an impact factor, the share of the speed the
//      body keeps.
//    - MoveBody flies a launched body, takes the friction impulse off on landing and slides
//      it to rest. A body in the air touches no vehicle.
//    - A vehicle drives over anyone lying on the road: the living die, the body moves a
//      little, the vehicle jolts.
//  The injury follows the person's speed change on a logistic curve fitted to Tefft (2011).
//  Every value is in assets/data/impacts.cfg; the measurements behind them and the criteria
//  are in docs/backlog.md#cj-035-vehicles-hitting-people.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "datafile.h"
#include <cstdint>
#include <string>
#include <vector>

struct Vehicle;
struct Rng;

// The part of a person that vehicles meet, the same for civilians and the player.
struct PersonBody {
    float    z = 0, vz = 0;          // px above the road and px/s upwards: a body in the air
    float    stagger = 0;            // s left of a stagger: another contact knocks them down
    float    balance = 0;            // px/s of speed change they withstand on their feet
    uint32_t underSerial = 0;        // the vehicle last over them (driven over once per pass),
    float    underT = -1;            // and when it was
    bool Airborne() const { return z > 0 || vz > 0; }
};

enum class HitKind { None, Obstacle, Bump, Launch, RunOver };

// What a vehicle does to a person whose circle overlaps its box.
struct PersonHit {
    HitKind kind = HitKind::None;
    Vector2 push{};                  // px the person moves out of the box (Obstacle, Bump)
    Vector2 vel{};                   // the person's velocity over the ground afterwards (px/s)
    float   vz = 0;                  // and upwards (px/s)
    float   speedChange = 0;         // px/s: their launch speed, for the injury and the time down
    float   damage = 0;              // health points
    Vector2 vehicleDv{};             // added to the vehicle's velocity
};

// 'n' (from the vehicle towards the person) and 'depth' as CircleOBB gives them.
PersonHit VehicleMeetsPerson(const Vehicle& v, Vector2 n, float depth, Vector2 personVel, const PersonBody& b, bool lying);
PersonHit ThrownOff(Vector2 velocity);                 // a rider coming off a motorbike at this velocity
float PersonImpactDamage(const Vehicle& v, float closing);   // the injury of a launch at this closing speed
void  MoveBody(Vector2& pos, Vector2& vel, PersonBody& b, float dt);   // flight, landing, sliding to rest
void  Stagger(PersonBody& b);                           // bumped: staggering for a while
void  Steady(PersonBody& b, float dt);                  // on their feet: the stagger wears off
float SlideSpeed(float px);                             // the speed at which a body slides this far
float TimeDown(float speedChange, Rng& r);              // s on the ground once at rest
float BalanceLimit(Rng& r);                             // a new civilian's balance
float PlayerBalance();
bool  LoadImpacts(const std::vector<DataRecord>& records, std::string& error);   // impacts.cfg
