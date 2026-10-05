# Traffic

How traffic and police vehicles drive. Traffic follows its lane kinematically ("on rails") until something knocks it into physics; police cars are always physics-driven. The rationale is recorded in [ADR-0004](../adr/0004-kinematic-rail-traffic.md).

Source: `src/traffic.h`, `src/traffic.cpp`, `src/traffic_recovery.*`; recovery tuning in `assets/data/traffic.cfg`; population management in `Game::UpdateSpawning` and `Game::UpdatePolice` (`src/game.cpp`).

The user approved the [CJ-016 specification](traffic-behaviour-proposal.md) on 2026-10-05. Its first increment covers knocked-vehicle recovery and persistent holding. Ordinary rail following, passing and junction right-of-way rules remain; path scanning no longer ignores a vehicle solely because both drivers block each other. Shared yielding roles, physical ordinary traffic, persistent on-foot drivers and confrontations are later increments. [ADR-0004](../adr/0004-kinematic-rail-traffic.md) remains Accepted and [ADR-0008](../adr/0008-human-like-traffic.md) remains Proposed.

## Contents

- [Rail driving](#rail-driving)
- [Route planning](#route-planning)
- [Speed control](#speed-control)
- [Junction rules](#junction-rules)
- [Obstacles and impatience](#obstacles-and-impatience)
- [Knocked off the lane](#knocked-off-the-lane)
- [Police](#police)
- [Population](#population)

## Rail driving

A traffic car on its lane is not simulated by the physics solver. It keeps a path — a list of waypoints along the right-hand lane — and a distance *s* along it. Every frame the AI advances *s* by its speed and places the car on the path: the pose comes from two sample points at the front and rear axle (±32 % of the length), so long vehicles sweep realistically through turns. A lateral offset shifts the car sideways when it overtakes or mounts the kerb.

Rail cars cannot jitter or deadlock in physics. They keep their distance by looking ahead along their own path, and in the solver they are infinitely heavy moving bodies that ignore buildings and street furniture (see [Physics › Traffic on rails](physics.md#traffic-on-rails)).

## Route planning

The path is planned one junction ahead and always extends at least 520 px beyond the car.

- At each junction the driver picks straight on (55 %, 70 % for large vehicles), right (25 %) or left (20 %, 10 % for large vehicles), among the exits that exist. A dead end at the city edge forces a U-turn.
- Turns are smooth curves through the junction. Large vehicles swing wide on right turns.
- Each turn is preceded by a stop waypoint that carries the junction, the signal axis and the planned manoeuvre.

## Speed control

| Influence | Behaviour |
|---|---|
| Cruise speed | 215–265 px/s (≈ 48–60 km/h); large vehicles 170–205 px/s. Panicking drivers go 50 % faster. |
| Curves | Slows to 130 px/s (95 px/s for large vehicles) before and through turns. |
| Signals | Stops for red. Stops for yellow if it can do so comfortably. |
| Obstacles | A look-ahead of 40 px + 1.1 × speed + half its length along the path finds vehicles and, unless the driver is distracted, pedestrians on the road (found through the pedestrian grid). The allowed speed follows the gap; the car stops 14 px short of the obstacle. |
| Acceleration | 45 % of the class acceleration; comfortable braking at 670 px/s², hard braking at 900 px/s² for an obstacle right ahead. |

### Planned stop

Every frame a rail car records in `DriverAI::stopDist` how far its front can still travel before a stop it has planned: the stop line at a red light or a blocked junction, or 14 px short of a person, a stopped vehicle or a static obstacle ahead. Pedestrians read it, together with the planned path (`AIPathPose`), to predict whether the car will reach them (see [Pedestrians › Perception and dodging](pedestrians.md#perception-and-dodging)).

## Junction rules

A car only enters a junction box when:

- the box holds no crossing traffic (vehicles going the same way are followed through; straight-on and right turns may meet oncoming straight-on and right turns);
- a left turn has no oncoming traffic about to come through on green;
- its exit lane has room for it, so it never blocks the box.

Directional right-of-way rules allow compatible movements through a junction and make left turns yield to oncoming traffic. They do not authorize ignoring an occupied vehicle footprint. Cooperative resolution of mutually blocked vehicles remains a later CJ-016 increment.

## Obstacles and impatience

| Situation | Reaction |
|---|---|
| Stopped behind a static vehicle for 1 s (scaled by the driver's temper) | Overtakes through the oncoming lane if it is clear; otherwise, except for large vehicles, mounts the kerb if the sidewalk is free of furniture and buildings. |
| Blocked for 6 s | Makes a U-turn in the middle of the block, if it is far enough from the junction and the opposite lane is clear (not for large vehicles). |
| Blocked, or someone standing in the road | Honks; impatient drivers honk sooner. |
| Distracted (random, about once every few minutes per driver) | Ignores pedestrians for 1–2.5 s — accidents happen. |
| Scared (gunfire, explosions, hit by the player) | Panics: drives faster and runs red lights. |

## Knocked off the lane

A hard hit, an explosion, or pushing on something for a moment turns a rail car into a normal physics car (see [Physics](physics.md#traffic-on-rails)). The driver then goes through these stages:

1. **Stop.** Brakes, then holds the car with the handbrake. This stage lasts at least 0.7 s and at most 3 s.
2. **Assess and plan.** Read the common snapshot captured after vehicle fire, explosions and cleanup, before any vehicle decisions. Rebuild the pedestrian grid before capture so explosion-created actors and velocity changes are visible to every driver in the same decision stage. First predict lane-following feedback with the full safety checks; if it is safe, improves lane alignment and is not stalled, commit it without an escape search. Otherwise evaluate at most 20 candidates: hold, forward and reverse, including arcs that unwind their steering. Prediction uses the production vehicle forces, class dimensions and complete swept oriented footprints against buildings, solid furniture, vehicles and people, including the on-foot player. A checked stopping tail leaves room to halt before an obstruction.
3. **Drive or hold.** Issue throttle, brake, steering and handbrake controls; the physics solver owns the actual position and angle. Keep a committed move with hysteresis, replan after poor progress, and check for an immediate hazard between planning jobs. Stop before changing gear. If no safe candidate is found within the bounded local planner, hold with `no_feasible_manoeuvre` and retain the same vehicle and driver. This diagnosis is not proof that every possible global escape is impossible; the enclosed fixture has known impassable geometry. There is no recovery-time or low-health exit, replacement pedestrian or relocation.
4. **Rejoin without correction.** Physically align with the lane before resuming the existing rail route. Rejoining must not blend, snap or move the car into a lane pose. Damage, fire and driver injury remain separate game consequences; a low health percentage alone does not abandon a blocked car.

Tuning is data-driven in `assets/data/traffic.cfg` ([format](../guides/adding-content.md#traffic-recovery)). The controller explicitly uses the current arcade model: its low-speed brake pedal engages reverse, so stationary holding uses the handbrake. CJ-002's axle model and class calibration remain pending; this increment does not claim that the arcade forces are calibrated human driving.

The forecast matches production force substeps in the measured 60 Hz and 20 Hz cases: respectively 240 Hz and 160 Hz. At faster frame rates it caps the forecast at 240 Hz to bound work, so it does not claim identical discretization at every frame rate. Predicted controls refresh at the elapsed actual frame interval, rounded to the next forecast sample, also bounded to 240 Hz. A full 4 s candidate uses at most 960 force steps. Moving-actor forecasts are cached and reused across candidates; complete swept and stopping checks also apply to the lane-tracking fast path.

The CPU optimization keeps the controller, candidate scores and tuning unchanged. Actor radii, centre speeds and initial oriented boxes are cached from the common observation. Conservative travel and rotation bounds reject actors that cannot reach a candidate's swept footprint before constructing their detailed forecast. Physical cars use their observed linear and angular velocity; rail cars include path travel, lane shift and any gap between the route and actual pose. A legacy rail pose blend bypasses the path-only bound and receives the complete forecast, with the observed actual body authoritative at time zero. Inflating both oriented-box half-extents can increase its corner radius by up to `sqrt(2)` times the margin; distance culls include this bound. The hold rollout is evaluated only if its score can affect a moving choice; original candidate order and hold-first ties are preserved. These changes require new isolated and city evidence before a measured cost improvement is claimed.

Terminal city diagnostics add `LONG-REJOIN` beside vehicles still knocked after 12 s. It records lane-tracking mode, lateral and heading error, lateral/forward velocity, angular velocity, distance to the upcoming junction and its required entry limit. `last_forecast` describes the most recent periodic rejoin check: `not_tested` means the alignment/velocity/block-position conditions were not ready; `blocked` means they were ready but the contact/forward-stop forecast failed; `clear` means that forecast passed. `cause` refines the tested result through `RejoinCause`: unavailable vehicle/call, missing snapshot, neighbourhood capacity, actual initial contact, clearance-only initial contact, unsafe sweep, incomplete stop or clear. Actual contact takes precedence over clearance-only contact elsewhere in the neighbourhood. Terminal pose values can differ from that last check. The line identifies which readiness conditions or forecast stage need investigation but does not identify a particular blocking actor or prove that a global escape is impossible.

The separate [nearby-box regression](../testing.md#nearby-box-clearance-regression) covers a contract the empty recovery scenes missed: a separated building or parked-vehicle box inside the local neighbourhood must permit a clear handoff. `OBBOverlap` initializes penetration to a large sentinel and can return false after a separating axis without resetting that output. Previously `AddNearby` stored the output without checking the boolean, falsely classifying separated boxes as initial contact and vetoing rejoin. It now records zero depth unless overlap is true. Actual overlap, clearance-margin contact and an obstructed forward stopping forecast must still reject rejoin. The regression checks both the API's unchanged-body contract and a real rail handoff. The pre-fix run completed all 26 checks with four failures, exactly the clear API result and handoff at both rates; the six obstacle guard cases passed. The corrected build passed all 26 clearance checks and all 156 frozen recovery checks, then completed six city regressions. Both clear-box cases rejoined at 0.700 s; obstacle guards still rejected handoff. See the result report for exact hashes, city outcomes and remaining CPU failures.

City profiling separates recovery planning from the full driver decision span. `DRIVER DECISION CPU` conservatively includes vehicle preparation, fire, explosions and wreck cleanup, pedestrian grid construction, the shared snapshot and traffic/police AI. It is an upper bound on traffic decision cost; physics, pedestrian AI, rendering and screenshot capture are outside that measured span. Isolated fixture timings cover their single driver and physics separately; they do not substitute for the 50-car/300-pedestrian CPU acceptance test.

The [recovery fixture](../testing.md#cj-016-recovery-measurements) covers Taxi, Bus and BoxTruck in open, enclosed and reverse-escape geometry at 60 Hz and 20 Hz. The [result report](traffic-recovery-results.md) preserves a pre-fix physical recovery build with all 156 isolated checks passing, compared with 14 failures per class before implementation. Its pre-fix first city run completed but exceeded CPU targets in crash, chase and rampage and recorded no completed rejoins. The complete corrected clearance/recovery/city series is recorded in that report: all 182 isolated checks pass, city rejoins occur and recovery give-ups remain zero. City CPU acceptance and the user playtest remain open. These one-seed isolated cases do not establish moving-gap negotiations, simultaneous recovery intent reservations, city-edge acceptance or incident interruptions.

## Police

Police cars are physics-driven at all times, like the player's car.

| Mode | Behaviour |
|---|---|
| Patrol / pursuit on the grid | Plans routes along the lanes, choosing turns that close in on the player when chasing. Ignores red lights; when not chasing it keeps its distance from traffic. |
| Direct pursuit | With line of sight within about 40 m, or when very close: steers straight at a point ahead of the player, rams, and handbrakes into sharp turns. |
| Stuck | Pressing on without moving for 1.8 s: reverses for 1 s. |

Arrests, the number of police cars and when they give up are game rules — see [Gameplay › Police response](gameplay.md#police-response).

## Population

- 50 traffic cars drive at all times. Ordinary rail cars more than about 210 m from the player and off-screen are recycled to a random lane 55–160 m away, off-screen, so the city around the player stays busy. Knocked/recovering or persistently held cars must keep their vehicle and driver when the camera turns away.
- Up to 40 parked cars stand on parking spots, including police cars at the police station and ambulances at the hospital.
- Abandoned vehicles are removed once they are far away and off-screen.
