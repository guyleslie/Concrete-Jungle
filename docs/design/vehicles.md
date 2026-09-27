# Vehicles

Vehicle classes, the handling model that moves a vehicle between collisions, and what happens as a vehicle takes damage. Collisions themselves are covered in [Physics](physics.md).

Source: `src/vehicle_types.*` (classes), `src/vehicle.*` (handling, effects, drawing), `Game::DamageVehicle` / `Game::ExplodeVehicle` in `src/game.cpp`.

> **Status:** the handling model and the damage model are due for a rework — see [CJ-002](../backlog.md#cj-002-vehicle-handling-model) and [CJ-003](../backlog.md#cj-003-vehicle-damage-model).

## Contents

- [Vehicle classes](#vehicle-classes)
- [Driver states](#driver-states)
- [Handling model](#handling-model)
- [Damage, fire and explosions](#damage-fire-and-explosions)
- [Visual and audio feedback](#visual-and-audio-feedback)
- [Tuning reference](#tuning-reference)

## Vehicle classes

Classes are defined by `CLASS` records in `assets/data/vehicles.cfg`, which is the single source of truth for their values; the file format is described in [Adding content](../guides/adding-content.md#vehicles). The shipped classes are:

| Category | Classes |
|---|---|
| Cars | Stinger, Viper, Bruiser, Taxi, Limo |
| Utility | Pickup, Van |
| Emergency | Ambulance, Police, FireTruck |
| Large (`large` flag) | Bus, BoxTruck, Semi, FireTruck, Garbage |
| Two-wheelers (`two_wheeler` flag) | Sportbike, Chopper, Scooter |

Each class sets length, height, top speed, acceleration, braking, reverse speed, steering rate, tyre grip, mass, health and how often it appears in traffic. A vehicle's width comes from its sprite's aspect ratio, so the collision box always matches the picture.

The acceleration and braking values are arcade-tuned: roughly 4–9 times real-world figures (for example 90 m/s² braking). This is one of the reasons the handling is being reworked.

## Driver states

| `DriverType` | Controlled by | Tyres |
|---|---|---|
| `Player` | Keyboard | Normal |
| `Traffic` | Traffic AI; kinematic while on its lane | Normal once knocked off the lane |
| `Police` | Police AI, always physics-driven | Normal |
| `Parked` | Nobody | In gear with the handbrake on: all wheels resist rolling and sliding |
| `None` | Nobody (the driver left) | Rolls freely until friction stops it |

Wrecks behave like parked vehicles on burnt-out, locked wheels. Burning vehicles have no driver input.

## Handling model

`VehicleForces` runs once per physics sub-step and changes only the velocity and angular velocity; positions are integrated by the solver. The model splits the velocity into a forward component *vF* and a sideways component *vR*.

### Longitudinal

| Input / effect | Behaviour |
|---|---|
| Throttle | Accelerates with the class acceleration, fading with *(vF / top speed)²*; 70 % off-road. Pressed while rolling backwards, it brakes. |
| Brake | Brakes with the class braking value while moving forward faster than 25 px/s; below that it engages reverse (60 % of the acceleration, up to the class reverse speed). |
| Handbrake | Velocity-proportional braking plus, at low speed, a constant locked-wheel deceleration so a slow car really stops. The constant part fades out between 60 and 200 px/s, so fast handbrake turns keep their feel. |
| Drag | Proportional to speed; much stronger off-road. |
| Rolling resistance | 45 px/s² on paved ground, 120 px/s² off-road, when not accelerating. |

A surface counts as off-road when its grip is below 0.8 (grass: 0.55, plaza paving: 0.9, everything else: 1.0).

### Lateral

- Tyre grip removes sideways velocity exponentially at the class grip rate × surface grip. Grip falls as the sideways speed grows (divided by *1 + |vR| / 380*).
- The handbrake cuts grip to 16 % (35 % for two-wheelers) — the drift mechanic. It blends in between 40 and 160 px/s, so at a standstill the handbrake holds the car instead of making it slide.
- When the car slides sideways at a large angle (the slide angle *atan(|vR| / |vF|)* between about 26° and 46°), the tyres saturate: sideways deceleration is capped at `SLIDE_DECEL`. A car spun or shoved in a crash slides a few metres instead of stopping dead.
- Static friction (`LAT_STATIC`) stops slow sideways creep completely.

### Steering

- Steering input is smoothed at a rate of 9/s.
- The target yaw rate is *steer × class steering rate × speed factor × direction*. The speed factor rises from zero at a standstill to full at 150 px/s and falls by up to 42 % towards top speed. The direction factor *clamp(vF / 30, −1, 1)* reverses steering smoothly when reversing, without flipping at zero speed.
- The yaw rate approaches the target at a rate of 9/s (3.5/s while sliding), but the tyres can only change it by `YAW_AUTHORITY` × surface grip per second. A spin from an impact therefore dies out physically instead of being snapped away.

## Damage, fire and explosions

| Stage | Rule |
|---|---|
| Damage | Crash damage follows the impact's delta-V (see [Physics › Crash consequences](physics.md#crash-consequences)); weapons and melee also damage vehicles. Each hit flashes the sprite and makes a traffic driver panic. |
| Smoking | Below 50 % health the engine smokes grey, below 25 % black. |
| Burning | At 0 health the vehicle catches fire and an AI driver bails out. The player is told to get out. |
| Explosion | 3–5 s after catching fire. Within 15 m, other vehicles are knocked off their lane, pushed away and spun, and damaged by up to 140; people take up to 120 damage (the player on foot up to 90); anyone inside the exploding vehicle dies. |
| Wreck | The wreck burns for 8 s, stays in the world as an obstacle on locked wheels, and is removed after 40 s once off-screen. |

Blowing up a vehicle the player damaged raises the wanted level and pays $150.

## Visual and audio feedback

- The sprite darkens with damage; wrecks are drawn charred.
- Skid marks and tyre smoke appear when sliding sideways faster than 130 px/s, when handbraking above 140 px/s, and on hard launches of powerful cars. Off-road driving throws up dust.
- Headlights switch on automatically at night (for the player: L cycles auto / on / off); brake and reversing lights follow the controls; emergency vehicles have a siren and light bar (G toggles it for the player).
- The engine sound follows a simulated four-gear rev counter; the tyre squeal follows the sideways slip.

## Tuning reference

| Constant | File | Value | Effect |
|---|---|---|---|
| `SLIDE_DECEL` | `vehicle.cpp` | 520 px/s² | Sideways deceleration of saturated, sliding tyres |
| `LAT_STATIC` | `vehicle.cpp` | 90 px/s² | Static friction against sideways creep |
| `HANDBRAKE_DECEL` | `vehicle.cpp` | 160 px/s² | Locked-wheel deceleration at low speed |
| `LOCKED_DECEL` | `vehicle.cpp` | 420 px/s² | Parked and wrecked vehicles |
| `YAW_AUTHORITY` | `vehicle.cpp` | 45 rad/s² | Maximum yaw acceleration the tyres produce |
| Class values | `assets/data/vehicles.cfg` | per class | Speed, acceleration, braking, steering, grip, mass, health |
