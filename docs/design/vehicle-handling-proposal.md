# CJ-002: Vehicle handling proposal

Approved specification for believable, distinct vehicle handling and carefully measured collision response. Drafted on 2026-09-27 and approved by the user on 2026-09-28. Implementation starts with measurement-only fixtures and a baseline. The current behaviour remains documented in [Vehicles](vehicles.md) and [Physics](physics.md); none of the targets below are measured results.

## Contents

- [Scope and feel](#scope-and-feel)
- [Reference research](#reference-research)
- [Class targets](#class-targets)
- [Collision acceptance](#collision-acceptance)
- [Measurement plan](#measurement-plan)
- [Delivery](#delivery)

## Scope and feel

Driving should communicate speed, tyre grip and vehicle mass. Normal steering should be predictable; excessive speed should widen the turn, rear-wheel drive should permit progressive oversteer, and heavy vehicles should accelerate and change direction slowly. Collisions should transfer momentum at the actual contact point, preserve the resulting slide and spin, and settle without sticking, tunnelling or jitter.

Use a dynamic bicycle model with front and rear axle slip angles, smoothly saturating tyre forces and a shared longitudinal/lateral friction ellipse. Include longitudinal load transfer, a force/power-limited engine, braking distribution, rolling resistance and quadratic aerodynamic drag. Steering controls wheel angle, with a rate limit and a speed-dependent limit for keyboard input. At very low speed, blend to bounded tyre impulses that cannot reverse a resting body or erase collision spin.

The following are implementation constraints:

- Preserve the 2D ground plane, 16 px = 1 m, kinematic rail traffic and the contact-solver ownership of positions ([ADRs 0001–0005](../adr/README.md)). The handling model changes velocities only, once per physics sub-step. Use the same mass, centre of mass and yaw inertia in tyre torque and contact response.
- Put class mass, wheelbase, centre-of-mass height and axle distribution, drivetrain, drive force/power, drag, tyre curve, brake distribution, steering and rear-brake parameters in `assets/data/vehicles.cfg`. Validate values, retain documented defaults and update the content guide with the approved record format.
- Compute road grip at each axle. Braking and cornering share available grip; a handbrake acts on the rear axle instead of reducing the entire body's grip. Motorcycles use a rear brake and a simplified lean-limited lateral response. High-sided vehicles use a conservative lateral-force limit; this remains a planar model without simulated rollovers or suspension travel.
- Keep ordinary traffic on its rails. Adapt its existing acceleration input deliberately: it currently consumes `VehicleSpec::accel`, so replacing arcade values without updating the consumer would silently change traffic. Check police steering and knocked-car recovery with the new response as necessary for compatibility.
- Retain the existing key bindings. Braking to rest must not accidentally become reverse; reverse needs a deliberate continued/repeated input with a documented transition. Reverse limits become walking pace for two-wheelers and modest manoeuvring speeds for cars and trucks.

Damage balance and deformation remain [CJ-003](../backlog.md#cj-003-vehicle-damage-model). Audio, art, crosshair feedback, general traffic recovery and the general-purpose autopilot remain their own backlog items. Existing damage and breakaway rules are exercised in integration tests, not redesigned here.

## Reference research

| Reference | Useful ideas and limits | Reuse status |
|---|---|---|
| [iforce2d: Top-down car physics](https://www.iforce2d.net/b2dtut/top-down-car) | Chris Campbell's community tutorial for Box2D 2.3.0, not an official Box2D vehicle sample. Local tyre velocity, capped lateral impulse, steered front wheels and per-wheel surfaces; its tuning is intentionally simple and is not a real-car dataset. | Conceptual reference only: a redistribution licence for this tutorial's code has not been verified. |
| [Jolt vehicle controller](https://github.com/jrouwe/JoltPhysics/blob/master/Jolt/Physics/Vehicle/WheeledVehicleController.cpp) and [settings](https://github.com/jrouwe/JoltPhysics/blob/master/Jolt/Physics/Vehicle/WheeledVehicleController.h) | Slip-dependent grip, drive distribution, brake torque and rear-wheel handbraking. Useful implementation reference without adding a 3D engine dependency. | [MIT](https://github.com/jrouwe/JoltPhysics/blob/master/LICENSE); preserve notices for any copied code. |
| [Gymnasium top-down car](https://github.com/Farama-Foundation/Gymnasium/blob/main/gymnasium/envs/box2d/car_dynamics.py) | An explicit shared friction budget for longitudinal and lateral tyre forces. Its numerical tuning is for a game environment. | [MIT](https://github.com/Farama-Foundation/Gymnasium/blob/main/LICENSE); preserve notices for any copied code. |
| [Marco Monster: Car Physics for Games](https://rsms.me/etc/car-physics/) | Axle slip angles, load transfer, drag and yaw torque. This mirror preserves the author's article; examples include estimates. | Conceptual reference only; the article reserves copying and redistribution rights. |
| [Box2D Solver2D](https://box2d.org/posts/2024/02/solver2d/) | Accumulated impulses, sub-steps, soft constraints and relaxation underpin the existing solver. | Preserve [ADR-0005](../adr/0005-vehicle-contact-solver.md); [Box2D code is MIT](https://github.com/erincatto/box2d/blob/main/LICENSE). |

No third-party code or assets are imported by this proposal. Any later code reuse must retain its licence text and be recorded in [CREDITS](../../CREDITS.md).

## Class targets

These are proposed **game calibration targets**, not claims that fictional classes reproduce particular production vehicles. Top speeds retain the existing design caps. Mass includes a representative occupant/load; there is no variable cargo simulation. `Semi` remains the current single rigid vehicle, without a trailer joint.

All performance targets assume level, dry asphalt, undamaged vehicles and automatic drivetrain operation. Acceleration has no rollout. Braking distance excludes driver reaction time and ends below 0.1 m/s. Maximum cornering acceleration is a sustained value, not an impact or steering transient. `g` means 9.81 m/s².

| Class | Mass (t) | Drive | 0–100 km/h (s) | 100–0 km/h (m) | Cornering (g) | Top speed (km/h) | Rear-brake profile |
|---|---|---|---|---|---|---|---|
| Stinger | 1.45 | AWD | 6.0 | 36 | 0.95 | 202 | Controlled |
| Viper | 1.55 | RWD | 4.2 | 35 | 1.00 | 214 | Sport |
| Bruiser | 1.75 | RWD | 6.8 | 42 | 0.78 | 198 | Sport |
| Taxi | 1.65 | RWD | 11.0 | 43 | 0.75 | 162 | Controlled |
| Pickup | 2.20 | RWD | 12.0 | 48 | 0.65 | 157 | Utility |
| Van | 2.80 | FWD | 18.0 | 50 | 0.60 | 144 | Utility |
| Limo | 2.60 | RWD | 14.0 | 46 | 0.68 | 160 | Utility |
| Ambulance | 4.00 | RWD | 16.0 | 51 | 0.55 | 166 | Heavy |
| Police | 1.85 | AWD | 6.5 | 39 | 0.90 | 207 | Controlled |
| Bus | 13.00 | RWD | 45.0 | 62 | 0.42 | 108 | Heavy |
| BoxTruck | 7.50 | RWD | 30.0 | 58 | 0.48 | 121 | Heavy |
| Semi | 10.00 | RWD | 32.0 | 60 | 0.45 | 126 | Heavy |
| FireTruck | 15.00 | AWD | 28.0 | 59 | 0.45 | 130 | Heavy |
| Garbage | 16.00 | RWD | 48.0 | 65 | 0.40 | 110 | Heavy |
| Sportbike | 0.28 | RWD | 3.6 | 39 | 0.95 | 212 | Bike |
| Chopper | 0.40 | RWD | 6.2 | 52 | 0.65 | 185 | Bike |
| Scooter | 0.22 | RWD | N/A; 0–50: 6.0 | N/A; 50–0: 13 | 0.60 | 95 | Bike |

Acceptance bands: acceleration ±10 %, braking distance ±10 %, cornering ±0.05 g and steady top speed ±3 %. The scooter must report 0–100 and 100–0 as `N/A`, rather than timing out or pretending to reach 100 km/h. Every shipped class must pass; a pooled average is insufficient. Viper must accelerate faster than Taxi, which must accelerate faster than Van; heavy classes must have lower sustained cornering acceleration than passenger cars.

The cornering target is the vehicle's usable lateral acceleration, not its longitudinal tyre coefficient. Configure longitudinal grip separately from lateral grip and the high-sided stability limit, with a shared axle force constraint `(Fx / (muX * Fz))² + (Fy / (muY * Fz))² <= 1`. For example, Taxi's 43 m braking target needs about 0.91 g average deceleration, while its usable cornering target is 0.75 g. Brake distribution and understeer must be calibrated together; setting one isotropic coefficient to the cornering value cannot meet both targets.

Rear-brake profiles describe a repeatable manoeuvre: start at 50 km/h (30 km/h for Bike), ramp to full steering over 0.25 s, hold the rear brake for 0.6 s with throttle released, then centre steering. Measure peak absolute body sideslip during the next 2 s. Sport: 20–50°; Controlled: 12–35°; Utility: 8–25°; Heavy: at most 15°; Bike: at most 20°. No profile may accumulate more than 180° of yaw in 3 s. Below 1 m/s, use lateral speed instead of an ill-defined sideslip angle. Recover to lateral speed below 0.5 m/s and yaw speed below 0.2 rad/s within 4 s after release. These are gameplay acceptance choices to review in the playtest, not measured manufacturer manoeuvres.

Real-world anchors distinguish published figures from conversions:

- [Dodge's 2016 Viper comparison sheet](https://s3.amazonaws.com/chryslermedia.iconicweb.com/mediasite/attachments/DG_Viper_ACR_Comp-Perform2omi0f8ttjqqdcaucafevll3qfv.pdf), SRT column: 3,378 lb (about 1.532 t), 0–60 mph in the mid-3 s range, 60–0 mph in 106 ft (32.31 m), and 1.03 g on the skidpad. Constant-deceleration scaling gives about 34.65 m at 100 km/h; that is a calculation, not a published 100–0 measurement. This anchors the sporting end of the range, without adopting the real car's much higher top speed.
- [Audi TT 45 TFSI data](https://uploads.audi-mediacenter.com/system/production/car_motorizations/808/file_en/38cfda37fdcdc7930ef9e317e384c05f8d2a16b4/eTD_Audi_TT_Coupe_45_TFSI_S_tronic_180kW_230113.pdf?1698933763=&disposition=attachment): 1,370 kg with driver, 5.8 s to 100 km/h, 2.505 m wheelbase and 250 km/h maximum speed. This anchors Stinger's scale; its proposed AWD configuration is a game choice. [Ford's Mustang GT release](https://media.ford.com/content/fordmedia/feu/de/de/news/2015/04/27/all-new-mustang-sprints-0-100-km-h-in-under-5-seconds--2-200-ord.html) gives 4.8 s to 100 km/h and up to 0.97 g; Bruiser deliberately represents an older, softer muscle car.
- [Michigan State Police MY2026 tests](https://www.michigan.gov/msp/-/media/Project/Websites/msp/training/MY2026_Police_Vehicle_Evaluation_Test_Book.pdf?hash=2E8D99BC1EB31F0B16508C094B2AE79B&rev=be140e0cf04043169fb191bd1ccb24a9): Tahoe RWD, F-150 Police Responder and Harley Road Glide give 60–0 mph distances of 132.09 ft, 148.31 ft and 157.94 ft. Constant-deceleration scaling to 100 km/h gives about 43.2 m, 48.5 m and 51.6 m. These are braking-scale anchors for Police, Pickup and Chopper, not measurements of our classes.
- [Ford Transit data](https://media.ford.com/content/dam/fordmedia/Europe/documents/productReleases/Transit/FordTransitEcoBlue_TechSpecs_EU.pdf): Transit 350 L3H2 has 2,149 kg base kerb mass, 3,500 kg gross vehicle mass and a 3.750 m wheelbase. [Sprinter chassis](https://www.mercedes-benz.co.uk/vans/models/sprinter/cab-chassis/overview.html) include 3,500–5,000 kg gross vehicle mass variants. These anchor Van and equipped Ambulance mass ranges; payload and performance remain our assumptions.
- [Mercedes-Benz Citaro dimensions](https://www.mercedes-benz-bus.com/gb/en/models/citaro/facts-citaro.html): 12.135 m length, 5.900 m wheelbase, 22.970 m turning circle and 19.5 t permissible gross mass. The Bus target of 13 t is an assumed operating load, not the published permissible maximum.
- [Rosenbauer/MAN TGM 18.320 delivery](https://www.rosenbauer.com/en/news/deliveries/at-hlf-3-4000-200-markersdorf-neulengbach-volunteer-fire-department~d-8052): 7.7 m length, 4.2 m wheelbase, 18 t permissible gross mass and 4,000 l water capacity. [Dennis Eagle chassis](https://www.dennis-eagle.co.uk/products/elite-chassis/) include 22 t and 26 t variants. These support a much heavier FireTruck/Garbage than the current arcade masses; our 15 t/16 t operating masses are assumptions.
- [BMW S 1000 RR](https://www.bmw-motorrad.de/de/models/sport/s1000rr/technicaldata.html): 198 kg ready to ride and 303 km/h maximum speed. [Honda SH125i](https://www.honda.co.uk/motorcycles/range/scooter/sh125i/specifications-and-price.html): 138 kg kerb mass and 98 km/h maximum speed. Adding an approximately 80 kg rider supports the proposed bike masses; Sportbike's 212 km/h cap remains a gameplay choice.

Taxi, Limo, BoxTruck and Semi use explicit engineering targets without a matched production-vehicle dataset. For every class, unpublished acceleration, braking and cornering values in the table are our proposed targets. Gross permitted mass must not be presented as kerb or measured operating mass.

## Collision acceptance

Use a new `crash-handling` fixture with prescribed initial velocities and explicit contact geometry. Measure isolated contact response separately from the subsequent motion under tyre forces. Contact-only cases disable tyre/drive forces and damage so momentum checks exclude ground friction, engine input and explosions; then repeat key cases with production forces and consequences enabled.

| Case | Setup | Required result |
|---|---|---|
| Glancing wall | Taxi, 50 km/h, velocity 15° towards a wall relative to its tangent | Continues in the original tangential direction, with at least 70 % of tangential speed immediately after the first contact episode; no adhesion or repeated bouncing. |
| Symmetric head-on | Equal Taxi masses at +50 and −50 km/h, centred | Post-contact centre-of-mass speed below 0.1 m/s; each contact delta-V within ±5 % of `(1 + e) × 50 km/h`; yaw speed below 0.1 rad/s in the symmetric case. |
| Rear-end | Taxi at 50 km/h into an equal, stationary dynamic Taxi, centred | Equal and opposite momentum changes within 2 %; analytical one-dimensional final velocities within ±5 % using the documented restitution. |
| T-bone | Taxi at 50 km/h hits a stationary Taxi 0.75 m ahead of its centre | Rotation has the sign of the contact torque; mirrored offset reverses spin; mirrored delta-V and absolute spin agree within 5 %. |
| Car versus truck | Centred Taxi into dynamic BoxTruck at 50 km/h, then reverse the moving body | In contact-only measurement, `deltaV_car / deltaV_truck` agrees with `mass_truck / mass_car` within 5 %; changing which body moves does not turn the truck into an infinite-mass obstacle. |
| Extreme mass ratio | Centred Scooter–Garbage impact at 50 km/h, both moving-body arrangements | The same momentum, energy and delta-V-ratio checks pass across the proposed 0.22–16 t mass range. |
| Rail transition | Dynamic/rail collision above the existing knock threshold; separate gentle shove | Hard impact converts the rail car before solving; contact delta-V agrees with the equivalent dynamic pair within 5 %. Gentle pushing releases the rail car within 0.35 s plus one simulation frame. |
| Thin obstacle and corner | Rigid thin pole and building corner; each class at its top speed, plus an opposing fast pair | Zero pass-throughs; no missing contacts. Run at both 1/60 s and 1/20 s frame intervals. |
| Breakaway objects | Existing lamp, hydrant and bollard materials with prescribed speed and mass | Break/not-break outcome matches the existing impulse cap; separated bodies do not gain kinetic energy. Record transferred impulse and delta-V. |
| Resting and wedged | Parked bodies against wall/corner/other car, released after a push | Residual penetration at most 0.5 px after 1 s; translation below 0.02 m/s and yaw speed below 0.02 rad/s within 3 s; no persistent oscillation. |

Across fixtures: zero NaNs, zero tunnelling, zero overlap frames deeper than 3 px for static **and vehicle–vehicle** contacts. For isolated finite-mass dynamic pairs with no external forces, total kinetic energy including rotation must not increase by more than 1 % numerical tolerance, and linear momentum residual must be below 2 % of incoming momentum magnitude sums. Record angular momentum including orbital motion as a diagnostic. Static obstacles exchange momentum with the world; kinematic bodies can perform external work; breakaway debris carries away momentum. Account for these separately rather than applying the closed-system conservation check to them. Exclude fixture resets from metrics.

Keep the current restitution law initially; evaluate friction and restitution against these measurements before changing them. The contact model must not use force or velocity clamps that silently delete an impact. If a contact-generation change contradicts ADR-0005, document the exact change in the proposed handling ADR before implementation.

## Measurement plan

After approval, first add measurement-only fixtures while leaving production handling, collision response and class values unchanged. Run the baseline on that build. Freeze input scripts, initial poses, scenario seeds, measurement definitions and tolerances before modifying physics; run exactly those fixtures again afterwards. Record revision, configuration and fixture identifiers in every result. The CLI options and scenarios below are implemented in fixture `cj002-v1`; see the [recorded arcade baseline](vehicle-handling-baseline.md) for results and measurement limitations.

`handling --vehicle <Class>` selects one of the 17 classes and uses an isolated, rendered test ground large enough for the entire manoeuvre, with no traffic, pedestrians, spawning, world-edge collision or live gameplay input contaminating the run. Keep the production tyre model, road material and integrator. Report every phase separately, and fail explicitly on missing measurements. A fixed 240 s (14,400-frame) budget per class allows:

| Phase budget | Measurement |
|---|---|
| 60 s | Standing acceleration to 100 km/h, or 50 km/h for Scooter; report a failure if the target is not reached. |
| 10 s | Full service-brake stop from a prescribed 100 km/h, or 50 km/h for Scooter. |
| 40 s | Top-speed convergence from 95 % and 105 % of the design cap in separate resets; report the speed and acceleration during the last 5 s of each half. Require absolute acceleration below 0.05 m/s² as well as the speed tolerance. |
| 80 s | Left/right skidpad trials on a 40 m radius, five trials per direction. The candidate lateral accelerations are the class target minus 0.10 g, minus 0.05 g, the target, plus 0.05 g and plus 0.10 g; derive requested speed from `sqrt(a * radius)`. Each 8 s trial budgets 2 s reset/setup, 3 s settling and 3 s measurement. Use controls rather than overwriting poses/velocities during settling/measurement. Log radius error, path curvature, actual lateral acceleration and axle slip. |
| 20 s | Mirrored rear-brake manoeuvres with the profile defined above. |
| 30 s | Stop/start, reverse, coast-down and rest checks; grass/asphalt grip comparison. |

The skidpad uses the same frozen controller on both models. The highest sustained 3 s interval with radius error below 5 % estimates usable cornering acceleration; an all-pass/all-fail sweep reports a bound, not a fabricated maximum. Adjust a sweep's range or settling time only in the measurement-only stage, then freeze it before the baseline; if a later change is necessary, rerun both models with the revised fixture. Failure to track the course is reported. Grass must reduce attainable cornering acceleration and increase braking distance compared with asphalt. Straight-line full braking must stay within 0.5 m lateral drift and 2° heading change. Braking must stop without a reverse-speed spike, and steering at rest must not rotate the body.

Example commands for the proposed fixture (repeat for all classes, changing output names):

```bash
sh build.sh
mkdir -p build/shots/cj002/before build/shots/cj002/after
./ConcreteJungle.exe --shot build/shots/cj002/before/handling-Taxi.png --frames 14400 --every 120 --scenario handling --vehicle Taxi > build/shots/cj002/before/handling-Taxi.log 2>&1
./ConcreteJungle.exe --shot build/shots/cj002/before/crash-handling.png --frames 14400 --every 30 --scenario crash-handling > build/shots/cj002/before/crash-handling.log 2>&1
./ConcreteJungle.exe --shot build/shots/cj002/before/crash.png --frames 1500 --every 30 --scenario crash > build/shots/cj002/before/crash.log 2>&1
./ConcreteJungle.exe --shot build/shots/cj002/before/derby.png --frames 1500 --every 30 --scenario derby > build/shots/cj002/before/derby.log 2>&1
./ConcreteJungle.exe --shot build/shots/cj002/before/chase.png --frames 1500 --every 30 --scenario chase > build/shots/cj002/before/chase.log 2>&1
```

Run `foot` and `day` for 1,500 frames and `rampage` for 3,600 frames as pedestrian compatibility checks. All windows remain 1,600 × 900. After implementation, repeat with only `before` changed to `after`. Inspect the screenshot series around brake release, slip onset, handbrake recovery and every collision phase; add phase-labelled captures for events falling between periodic screenshots.

The collision fixture must log its complete scheduled case list, run both physics frame intervals over equal simulated durations and fail on an incomplete run. The 1/20 s stress step belongs to the isolated physics fixture; the `--shot` application loop remains fixed at 1/60 s. Log physics-step count and simulated duration separately from rendered frame count. The 14,400-frame budget is a proposed upper budget, not evidence that any case has run. Exact phase timings must be fixed in the measurement-only harness before the baseline.

For the existing `crash` and `derby`, preserve scripts and the initial RNG seed. The shared gameplay RNG is also consumed by speed/slip-dependent effects, so changing the physics may change later random draws and interactions; record this limitation rather than claiming identical traffic. Compare fresh baseline and after values and retain the [published baseline](../testing.md#baseline) for context. Require zero deep frames, no increase in maximum penetration beyond 0.1 px measurement tolerance, and no increase in position/heading flips per body-second. Report raw counts as well, and add a resting-contact jitter metric that excludes deliberate steering and fixture resets. Slower acceleration may change the number and severity of contacts, so also require the prescribed-speed collision fixtures to pass; a quieter derby alone does not prove better collisions. New traffic failures must be explained and resolved or separately agreed, not silently accepted as a new baseline.

## Delivery

Approval covers CJ-002 only: measurement fixtures, the handling model and class data, collision corrections justified by the fixtures, and the necessary traffic/police input adaptation. The proposed architectural decision is [ADR-0007](../adr/0007-dynamic-vehicle-handling.md).

Build through `sh build.sh` in Git Bash/w64devkit without warnings; run the before/after scenarios, inspect screenshots, update the affected design documents, content guide, test results, changelog and backlog, then run `python tools/check_docs.py`. Work is committed directly on `main`, without a feature branch or remote, as instructed for this session. CJ-002 stays open until the user has playtested and accepted the feel, particularly a sports car, Taxi, a heavy vehicle and a motorcycle.
