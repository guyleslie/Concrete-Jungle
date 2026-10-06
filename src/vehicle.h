// =====================================================================================
//  Vehicles: arcade top-down car physics shared by the player and every AI driver.
//
//  Model: velocity is split into forward / lateral components each sub-step. Throttle,
//  brake and drag act on the forward part; tyre grip exponentially kills the lateral
//  part. Handbrake and high slip angles reduce grip -> drifts. A car sliding sideways
//  (spun or shoved in a crash) gets saturated, constant tyre friction instead, and slow
//  creep is stopped by static friction, so wrecks slide a realistic distance and then
//  stand still. Steering sets a target yaw rate that depends on speed; the tyres can
//  only apply a limited yaw torque to reach it, so a spin from an impact dies out
//  physically instead of being snapped away.
//  Positions are integrated (and collisions solved) by VehiclePhysics - physics.h.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "math_utils.h"
#include "vehicle_types.h"
#include "traffic_recovery.h"
#include <deque>

class CityMap;
class Particles;

enum class DriverType : uint8_t { None, Player, Traffic, Police, Parked };

struct VehicleInput {
    float throttle = 0;      // 0..1
    float brake = 0;         // 0..1 (reverses when stopped)
    float steer = 0;         // -1 (left) .. 1 (right)
    bool  handbrake = false;
};

struct Waypoint {
    Vector2 p;
    float   cum = 0;         // distance along the path from its first point
    bool    turn = false;    // part of a turn (slow down)
    bool    stop = false;    // entry of intersection (si,sj): signal 'axis', planned manoeuvre
    int     si = 0, sj = 0, axis = 0;
    int     turnType = 0;    // at a stop waypoint: 0 straight, 1 right, 2 left, 3 u-turn
    int     d = 0, d2 = 0;   // at a stop waypoint: approach / exit direction
};

struct DriverAI {
    std::deque<Waypoint> path;
    int   ti = 0, tj = 0;    // intersection we are heading to
    int   dir = 0;           // current travel direction 0=N 1=E 2=S 3=W
    float cruise = 220;      // desired speed

    // --- rail (kinematic lane following) ---
    bool  rail = false;      // true: glides along 'path', no physics until something hits it
    float s = 0;             // distance of the vehicle centre along the path
    float speed = 0;
    float laneShift = 0, laneShiftTarget = 0;    // sideways offset for passing obstacles
    int   curTurn = 0;       // manoeuvre inside the current junction (for yielding)
    float blend = 0;         // legacy rail U-turn easing; physical recovery never blends
    Vector2 blendPos{};  float blendAng = 0;
    float dynTimer = 0;      // time spent knocked off the rail
    float shove = 0;         // time spent pushing on something while on the rail
    // --- recovery after a knock: drive back onto the lane ---
    float recover = 0;       // time spent trying to get back on the lane
    float gearTimer = 0;     // recovery diagnostics: > 0 when reverse is selected
    float jammed = 0;        // retained legacy diagnostic field
    float retry = 0;         // next attempt to re-join the lane
    RecoveryState recovery; // bounded physical manoeuvre / persistent hold

    // --- misc ---
    float stuck = 0, reverse = 0, honk = 0, panic = 0;
    float blocked = 0;
    float creep = 0;         // > 0: deadlock breaker
    float distracted = 0;    // > 0: driver not watching for pedestrians (accidents happen)
    float uturnCooldown = 0;
    float temper = 1;        // 0.5 calm .. 1.5 impatient (honking, overtaking)
    int   blocker = -1;      // vehicle we are waiting for (-1 none / person)
    float stopDist = 1e9f;   // rail: how far the front can still go before a planned stop (people read it)
    // --- cooperative yielding: a stable role in a mutual blockage (see traffic.h) ---
    int   waitingOn = -1;    // wait-for edge: the vehicle this driver is stopped behind
    int   yieldTo = -1;      // vehicle given priority; -1 none
    uint32_t yieldSerial = 0;// its serial when the role was taken (slot reuse guard)
    float retreatLeft = 0;   // rail distance still to reverse along the driven path
    float mutualTime = 0;    // how long the mutual wait has persisted
    float yieldClear = 0;    // how long the conflict has looked resolved
    int   yieldDepth = 0;    // 0: direct role, >0: backing up for the driver ahead (chain)
    float cycleTime = 0;     // how long this driver has been part of a wait-for cycle
    float cycleCheck = 0;    // s until a stuck cycle is assessed again
    int   lastYieldTo = -1;  // the driver last given way to (a knocked pair keeps its roles)
    uint32_t lastYieldSerial = 0;
    float lastYieldTime = -1e9f;
    // --- incidents (traffic_incidents.h) ---
    uint8_t mood = 1;        // DriverMood: calm / normal / aggressive
    int   incident = -1;     // incident this driver is stopping for; -1 none
    int   driverPed = -1;    // the driver on foot while out of the car
    uint32_t driverPedSerial = 0;
    Vector2 lastVel{};       // diagnostics (jolt detection)
    int   reason = 0;        // diagnostics: 0 cruise, 1 red light, 2 queue, 3 blocked, 4 yield, 5 static, 6 box
};

struct Vehicle {
    bool    active = false;
    uint32_t serial = 0;     // unique per spawn/recycle: handles check it before use
    int     skin = 0;
    VClass  cls = 0;
    Vector2 pos{}, vel{};
    float   angle = 0, angVel = 0, steer = 0;
    float   width = 30, length = 66, height = 24;

    float   health = 100;
    bool    burning = false;   float burnTimer = 0;
    bool    wrecked = false;   float wreckTimer = 0;
    float   damageFlash = 0;
    bool    hitByPlayer = false;
    bool    recoveryTracked = false; // protect an unresolved recovery / disabled car from population recycling

    Vector2 kinFrom{};  float kinFromAng = 0;     // rail cars: pose at the start of the frame

    DriverType   driver = DriverType::None;
    int          driverSkin = 0;         // look of the (AI) driver, for carjacking
    VehicleInput in;
    DriverAI     ai;

    bool    headlights = false, braking = false, reversing = false, siren = false;
    float   slip = 0, speedFwd = 0, rpm = 0;
    float   frontSlipAngle = NAN, rearSlipAngle = NAN;  // radians; N/A for the baseline arcade model
    Vector2 lastSkid[2]{};
    bool    skidOn[2]{ false, false };
    bool    missionTarget = false;

    const VehicleSpec& S() const { return Spec(cls); }
    OBB  Box(float grow = 0) const { return MakeOBB(pos, angle, width * 0.5f + grow, length * 0.5f + grow); }
    Vector2 Fwd() const { return Forward(angle); }
    float Speed() const { return Len(vel); }
    bool Drivable() const { return active && !wrecked && !burning; }
};

void InitVehicle(Vehicle& v, int skin, Vector2 pos, float angle);
float VehicleYawInertia(const Vehicle& v);   // tonnes * px^2, shared by tyres, contacts and measurements
// Engine, brakes and tyres for one physics sub-step: changes vel / angVel only.
void VehicleForces(Vehicle& v, const CityMap& map, float h);
// Once per frame after the physics step: gauges, skid marks, tyre smoke, dust, damage fx.
void VehicleFrameEffects(Vehicle& v, const CityMap& map, Particles& fx, float dt);
// Smoke / fire / damage effects only (used for kinematic "rail" traffic).
void UpdateVehicleEffects(Vehicle& v, Particles& fx, float dt);

// Rendering (called inside the matching render pass)
void DrawVehicleShadow(const Vehicle& v, Vector2 shadowVec);
void DrawVehicle(const Vehicle& v, float time);
void DrawVehicleLights(const Vehicle& v, float night, float time);
void DrawVehicleEmissive(const Vehicle& v, float night, float time);
