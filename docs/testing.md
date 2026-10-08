# Testing

The game has an automated test mode that runs a scripted scenario for a fixed number of frames, saves screenshots and logs metrics. Use it to verify every change to the simulation before and after, and compare the numbers.

## Contents

- [Running a scenario](#running-a-scenario)
- [Scenarios](#scenarios)
- [Metrics](#metrics)
- [Baseline](#baseline)
- [CJ-002 measurements](#cj-002-measurements)
- [CJ-016 recovery measurements](#cj-016-recovery-measurements)
- [Cooperative yielding fixture](#cooperative-yielding-fixture)
- [Driver incident fixture](#driver-incident-fixture)
- [Turning fixture](#turning-fixture)
- [Workflow](#workflow)

## Running a scenario

```bash
./ConcreteJungle.exe --shot build/shots/crash.png --frames 1500 --every 30 --scenario crash > build/shots/crash.log 2>&1
```

| Option | Meaning |
|---|---|
| `--shot <file.png>` | Enables test mode and saves a screenshot after the last frame |
| `--frames <n>` | Number of frames to run (default 180); the frame time is fixed at 1/60 s |
| `--every <n>` | Also saves `<file>_<frame>.png` every *n* frames |
| `--scenario <name>` | The scenario to set up (default `foot`) |
| `--vehicle <Class>` | Selects a class for the isolated `handling` or `traffic-recovery` scenario (default `Taxi`; recovery supports `Taxi`, `Bus`, `BoxTruck`) |
| `--uncapped` | Disables vsync in screenshot mode; simulation still advances exactly 1/60 s per rendered frame |

Screenshot paths must be **relative** to the working directory: raylib prefixes the working directory to the file name. The window opens while the test runs. Keep test output in `build/`, which is not version-controlled. On foot, test runs ignore the mouse cursor for the camera look-ahead (it decides what is on screen and so which cars and people are recycled); before 2026-10-06 `foot`, `day` and `brawl` counts depended on where the cursor rested.

## Scenarios

| Scenario | Setup |
|---|---|
| `title` | The title screen with the city simulating behind it |
| `foot` | The player on foot at the central square, at dusk |
| `day`, `night` | On foot at 13:00 or 22:30 |
| `drive`, `nightdrive` | The player in the starter car with a simple autopilot (steady throttle, gentle weaving) |
| `chase` | As `drive`, with three wanted stars |
| `bikes` | At 13:00, on foot beside every two-wheeler class, parked (empty) and ridden, and a parked Stinger for scale. The ridden bikes are traffic placed off the lane, so run it for about 20 frames before they start to move. |
| `civilians` | At 13:00, on foot beside every civilian look standing in rows of 14, facing up; run it for about 20 frames |
| `overview` | A zoomed-out view of the traffic around the player, each car ringed by the reason it is stopped |
| `crash` | Scripted crash course: grinding along a wall at 35°, reversing, a full-speed head-on hit, a building corner at 45°, a lamp post at about 60 km/h, a hydrant, a steel bollard, and shoving three parked cars into a wall. Every impact is logged. |
| `derby` | Full throttle through traffic with random steering; reverses when stuck |
| `rampage` | At 13:00: straight 4 s runs at 50 km/h along the sidewalk walking lines of the blocks around the start, each from a fixed start point, so people have to get out of the way. Run it for 3,600 frames. |
| `brawl` | At 13:00, on foot with fists: runs up to the nearest person standing (preferring anyone fighting back, not chasing runners) and punches them |
| `handling` | Isolated acceleration, braking, top-speed, skidpad, rear-brake, reverse and surface measurements for the selected class; 14,400 frames |
| `crash-handling` | Prescribed-speed contacts with isolated forces and production consequences measured separately, at 1/60 s and 1/20 s physics intervals; 14,400 frames |
| `traffic-recovery` | Isolated enclosed hold, free lane recovery and front-blocked garage escape for Taxi, Bus or BoxTruck, at 1/60 s and 1/20 s physics intervals; 14,400 frames |
| `traffic-clearance` | Taxi rejoin with separated nearby building/parked-car boxes, plus actual-overlap, clearance-only-contact and forward-blockage API guards at 60 Hz and 20 Hz; 420 frames |
| `traffic-turns` | Isolated CJ-020 turning fixture: every traffic class turns right, turns left and goes straight through an empty junction at 60 Hz and 20 Hz; see [Turning fixture](#turning-fixture) |

## Metrics

The run ends by logging:

| Line | Metric | Healthy value |
|---|---|---|
| `SHOT` | Average frames per second | ≈ 75 (vsync-limited on the development machine) |
| `TRAFFIC` | Number of traffic cars, average speed, stopped and blocked cars, AI-to-AI contacts per second | Few blocked cars; AI contacts close to 0 in `drive` |
| `TRAFFIC jolts` | Sudden velocity jumps per second, split into rail, knocked and police cars | Rail jolts close to 0 |
| `TRAFFIC stopped because` | Why stopped cars are stopped: red light, queue, person (or a knocked car holding), yield at a junction, static obstacle, full junction box, giving way to another driver | Mostly red lights and queues |
| `TRAFFIC yielding` | Cooperative yielding roles taken (and how many were chain roles), cars still giving way at the end, and wait-for loops still open at the end: mutual pairs and larger loops | Loops open at the end are only ones that have just formed |
| `TRAFFIC slip` | The angle between a rail car's body and the motion of its rear axle point (0.32 lengths behind the centre), split into lane changes, turning and straight driving: car-seconds, share above 5°, maximum; and car-seconds of sideways shifting while stopped. A slip over 8° is logged as `RAIL-SLIP` with the car and what it was doing | Close to 0° in lane changes, turns and straight driving; at most 8° ([CJ-020](backlog.md#cj-020-turning-kinematics-of-traffic)) |
| `TRAFFIC rail overlaps` | Pair-seconds in which two rail cars overlap by more than 1 px (rail cars pass through each other in the solver, so any overlap shows), how many of them near a junction box, and the deepest overlap | Close to 0 |
| `TRAFFIC turns planned` | Turns traffic planned: clean, wide (out of the lane, taking the junction box alone), not fitting (a class with no other way), and U-turns at a dead end | Not fitting close to 0 |
| `TRAFFIC wait cycles` | Wait-for loops that formed during the run, how many lasted over 10 s, and the longest; then the members of each loop still open at the end (`UNRESOLVED`). A loop lasting 5 s is logged as `WAIT-CYCLE` when it happens | None over 10 s |
| `INCIDENTS` | Collision incidents started, drivers out, confrontations, fights, drivers back in their own car; interruptions by reason (no safe exit, car lost, driver dead, other); impacts that nobody took personally | Every driver out is back in their car or has a logged reason |
| `PHYS` jitter | Frames where a physics body's position or heading reverses direction frame after frame | Close to 0 per body-second |
| `PHYS` penetration | Deepest overlap with buildings or solid furniture, and frames deeper than 3 px | Under 3 px; no deep frames |
| `PHYS` stuck | Times the player pressed on without moving for 2.5 s | Expected in `crash` (pushing into walls on purpose) |
| `PHYS` traffic | Knocked off the lane / re-joined / drivers gave up; a prolonged recovery is logged with its cause | Feasible fixture recoveries rejoin; no safe local candidate holds with its driver; zero timeout abandonments |
| `IMPACT` (`crash`) | Contact kind, object, closing speed, delta-V of both bodies, whether an object broke | Plausible delta-V; breakaway objects break |
| `DEEP` | A body deeper than 3 px in the static world (logged while it happens) | None |
| `PEDS` fleeing, dodging | Average number of people in the `Flee` and `Dodge` states | Below 1 in `foot` and `day` (nothing is happening) |
| `PEDS` on the road off a crossing | Average number of people standing or walking on a road tile outside a zebra crossing (not counting people on the ground) | Below 1 in `foot` and `day` |
| `PEDS` visible | Average number of people on screen, not counting bodies | At least 4 in `foot` and `day` |
| `PEDS` overlaps | Pairs of people closer than 1.6 body radii, per second | Low; a few in crowded scenes |
| `PEDS` crossing | Person-seconds spent on a crossing's road part while the crossing traffic has green, split into people who started on green and jaywalkers | Close to 0 in `foot` and `day` |
| `PEDS` down too long | Person-seconds spent knocked down for more than 1 s beyond their get-up time | 0 |
| `PEDS` sliding | Share of moving time in which a person's body faces more than 35° away from its motion | Close to 0 % |
| `PEDS` hits | People hit by traffic and by the player; in `rampage`, people in the car's straight path within 2 s and how many of them were hit | Traffic hits 0 in `foot` and `day`; at least 70 % escape in `rampage` |
| `PEDS at the end` | People per state and within 30, 60 and 110 m of the player | Most people walking; everyone within 110 m |
| `PEDS` fights | People who fought back and punches they landed (`brawl`) | Some in `brawl` |
| `TIMING` | CPU time per frame of the vehicle update, the pedestrian update and the world drawing (CPU side only) | Pedestrians at most 0.5 ms |
| `RECOVERY CPU` | Recovery snapshot/planning average, 95th percentile, worst time, plans, rejected candidates and holds; job-frames deferred by the [planning budget](design/traffic.md#planning-budget), the longest wait in frames, holds kept because no blocker moved and immediate checks covered by an earlier extended check | Bounded candidate work; assess together with full driver decisions |
| `RECOVERY WORK`, `RECOVERY WORST` | Per frame: recovering cars, neighbourhood gathers, immediate checks, tracking and candidate rollouts, rejoin checks, force steps, awake actor tests, overlap tests and newly computed forecast boxes, with milliseconds per stage; then the six most expensive frames | Identifies which stage causes a CPU peak |
| `DRIVER DECISION CPU` | Vehicle preparation/fire/explosions/wreck cleanup, pedestrian grid build, shared snapshot and traffic/police AI; an upper bound on traffic decisions, excluding physics, pedestrian AI and drawing | CJ-016 target: average at most 0.5 ms/frame, 95th percentile at most 1.0 ms/frame |
| `DRIVER DECISION STAGES` | The same span split per frame: cleanup, pedestrian grid, snapshot, rail traffic, knocked (recovering) traffic, police | Shows which stage to optimise |
| `LONG-REJOIN` | Terminal readiness diagnostics for initialized traffic recovery lasting at least 12 s: tracking mode, lane/heading errors, lateral/forward velocity, angular velocity, upcoming-junction distance/entry limit, last forecast status and `RejoinCause` | Diagnose unresolved recovery; speed alone does not establish a safe rejoin |

## Baseline

Results on 2026-09-27 after the collision rewrite (1,500 frames each):

| Scenario | Position flips | Heading flips | Max penetration | Deep frames | Traffic knocked / re-joined / gave up |
|---|---|---|---|---|---|
| `crash` | 7 | 2 | 0.3 px | 0 | 2 / 1 / 0 |
| `derby` | 1 | 9 | 0.3 px | 0 | 3 / 3 / 0 |
| `chase` | 0 | 5 | 1.7 px | 0 | 4 / 3 / 1 |

For comparison, before the rewrite the `crash` scenario produced 965 position flips, a maximum penetration of 33 px and 1,849 deep frames.

After the pedestrian rework ([CJ-010](backlog.md#cj-010-pedestrian-behaviour), 2026-09-27) the traffic situations in these scenarios differ, because the larger population draws a different random sequence; the physics code itself did not change. New reference values (1,500 frames each):

| Scenario | Position flips | Heading flips | Max penetration | Deep frames | Traffic knocked / re-joined / gave up |
|---|---|---|---|---|---|
| `crash` | 11 | 30 | 0.3 px | 0 | 11 / 9 / 1 |
| `derby` | 3 | 3 | 0.1 px | 0 | 1 / 1 / 0 |
| `chase` | 1 | 10 | 0.1 px | 0 | 5 / 5 / 0 |

In `drive` the simple autopilot now ends up pressed against a building corner (9 stuck events); it cannot reverse ([CJ-008](backlog.md#cj-008-test-autopilot-improvements)).

### Pedestrians

Before and after CJ-010. `foot` ran 1,500 frames and `rampage` 3,600 frames. The old AI ran 220 people spread over the island; the new one runs 300 around the player.

| Metric | Before (`foot`) | After (`foot`) | Before (`rampage`) | After (`rampage`) |
|---|---|---|---|---|
| Fleeing, average | 29.9 | 0.0 | 44.3 | 28.0 |
| On the road off a crossing, average | 11.7 | 0.0 | 12.1 | 2.5 |
| Visible, average | 1.3 | 5.9 | 22.7 | 58.0 |
| Hit by traffic | 7 | 0 | 26 | 23 |
| In the car's path / escaped | — | — | 55 / 67 % | 136 / 74 % |
| Down too long | 71 person-s | 0 | 1,015 person-s | 0 |
| Sliding | 4.0 % | 0.1 % | 4.0 % | 0.1 % |
| Overlaps per second | 0.04 | 1.6 | 0.8 | 13.8 |
| Pedestrian CPU time | 0.28 ms | 0.37 ms | 0.26 ms | 0.41 ms |

The overlap count rose with the local density (about five times as many people near the player) and is highest in `rampage`, where crowds run from the car; people still never stay inside each other. Across runs the `rampage` escape rate varied between 72 % and 81 %. In `brawl`, 1–4 tough people fought back per run and landed up to 10 punches.

## CJ-002 measurements

The [approved handling specification](design/vehicle-handling-proposal.md#measurement-plan) defines class-by-class acceleration, braking, skidpad, rear-brake and collision tests. The measurement-only harness is implemented before changing production handling or collision response. The [recorded arcade baseline](design/vehicle-handling-baseline.md) contains all 17 class results and 122 collision phases. No after measurement or handling playtest has been performed yet.

Use `--run-name review-20261003` to save a new runner evidence set in a subdirectory of the selected phase without replacing previous results. Names allow letters, digits, underscores and hyphens only. Run `python tools/summarize_cj002.py --output docs/design/vehicle-handling-baseline.md` to rebuild the baseline report from complete before manifests. The summarizer requires all classes, matching isolated input hashes, fixture summaries and final screenshots; failed target checks remain visible. An unbracketed skidpad sweep is a bound, not a measured grip maximum; missing baseline tyre telemetry is distinct from a handling defect.

Run `python tools/run_cj002.py --phase before --suite all` after building, then repeat with `--phase after`. The runner opens one visible window at a time and records exact arguments, revision, dirty status, executable/configuration/fixture SHA-256 hashes, exit status and screenshot presence in timestamped manifests under `build/shots/cj002/`. Existing evidence requires explicit `--replace`; `--dry-run` previews the schedule without changing files. `--suite handling --vehicle Taxi` and `--suite city --scenario crash` select individual cases. Isolated fixtures use `--uncapped`; city runs preserve the existing timing options.

Fixture `cj002-v1` reports each scheduled phase and fails missing measurements, incomplete execution, non-finite state or penetration above 3 px, including vehicle pairs. A skidpad trial needs exactly 180 measurement samples, radius error at most 5 %, speed error at most 2 % and measured lateral acceleration within 0.03 g of the requested value. The maximum is bracketed by both successful and unsuccessful trials; slowing down cannot count as meeting a higher-speed target. Axle-slip telemetry is unavailable on the baseline arcade model and is reported explicitly. Rear-brake yaw travel uses the first 3 s, and recovery requires 0.25 s continuously below the specified lateral/yaw limits.

Keep the existing `crash`, `derby` and `chase` scripts; their run-up speeds depend on the handling, so prescribed-speed collision fixtures are also required. Contact-only momentum/energy checks exclude tyre-ground and damage effects. Inspect screenshot series and phase-labelled captures as well as numeric results. See the specification for per-class bands and collision acceptance criteria.

## CJ-016 recovery measurements

The first increment of the [approved traffic specification](design/traffic-behaviour-proposal.md) replaces knocked-vehicle timeout abandonment with physical recovery or a persistent hold. It does not cover mutual yielding or incidents; see the [cooperative yielding](#cooperative-yielding-fixture) and [driver incident](#driver-incident-fixture) fixtures. The frozen `cj016-recovery-v1` scene calls production traffic AI and `VehiclePhysics::Step` on uniform road, with world-edge contacts disabled. Damage consequences, pedestrian AI and population spawning do not run. The original driver, skin, class and active vehicle are checked on every physics step.

```bash
python tools/run_cj016.py --phase before --run-name baseline-20261005
python tools/run_cj016.py --phase after --run-name recovery-20261005
python tools/run_cj016.py --phase after --vehicle Taxi --run-name review-20261005
```

Build separately before running. The runner opens one visible window at a time, uses `--uncapped --every 120`, and retains logs, screenshots and timestamped JSON manifests under `build/shots/cj016/<phase>/<run-name>/`. It records the exact command, revision and dirty state, input SHA-256 hashes, exit status and screenshot presence. Existing evidence requires an explicit `--replace`; `--dry-run` previews the schedule without writing or launching. A complete fixture with failed acceptance exits 1 and remains complete evidence in the manifest. Missing phases, malformed records and absent final screenshots do not count as completed measurements.

Each selected class runs these six phases, in this order:

| Phase | Simulated duration | Physics interval | Fixture |
|---|---|---|---|
| `enclosed-60hz` | 60 s | 1/60 s | Four walls, each 5 px clear of the initial car; no reachable lane |
| `free-60hz` | 30 s | 1/60 s | Initial pose 120 px laterally from the lane on open road |
| `garage-60hz` | 30 s | 1/60 s | Front and side walls 5 px clear; an open rear requires reversing beyond the side walls before turning |
| `enclosed-20hz` | 60 s | 1/20 s | Same enclosed geometry |
| `free-20hz` | 30 s | 1/20 s | Same open-road geometry |
| `garage-20hz` | 30 s | 1/20 s | Same reverse-escape geometry |

All phases use seed `0x000c0016`, an initial north-facing car, and a target lane at `InterCenter(3, 3) + (LANE_OFFSET, 350 px)`. The render clock stays at 60 Hz: each class completes 14,400 rendered frames, 9,600 physics steps and 240 s. At 20 Hz, physics and decisions advance once per three rendered frames. Fixture resets happen between observed cases. Phase-end captures show the completed phase, with enclosed checkpoints at 12 s and 30 s; the scene renders production sprites, obstacles, the target lane and the driven trail on a 10 m grid.

There are 52 acceptance checks per class. Every phase checks unchanged driver/vehicle ownership, finite state, no discontinuous pose jump, static penetration at most 3 px, no deep-overlap steps and no lane-rejoin blend. Enclosed phases additionally require presence after 60 s, at most 5 px displacement, final speed at most 2 px/s, no lane rejoin and at most 0.25 s of pressing after the first 1 s. Feasible phases require a completed rejoin within 30 s; garage phases also require reverse travel of at least 1.5 vehicle lengths. A pose jump is a step above the greater of 8 px and velocity-predicted travel plus 4 px. These tolerances detect relocation; they are not permission for a recovery controller to set a physical body's pose.

`CJ016 metric`, `result`, `diagnostics`, `timing` and `summary` lines retain per-phase acceptance, contact counts, reverse distance, rejoin time, ownership losses, pose corrections and AI/physics CPU costs. Timing includes the shared observation snapshot and driver decision work; rendering and screenshot capture are outside that span. The runner requires all six results and all 52 metric records rather than interpreting process completion as acceptance.

The production recovery forecast uses the same force model and the actual fixture substeps: 240 Hz forces at 60 Hz decisions and 160 Hz forces at 20 Hz decisions. For faster normal game frame rates it caps forecast work at 240 Hz, with controls refreshed at the elapsed actual frame interval rounded to the next forecast sample. This is a bounded forecast, not a promise of identical production discretization at every frame rate. Full planning considers at most 20 candidates; safe lane feedback that improves alignment can skip that search while retaining the complete swept-path and stopping checks.

The CPU optimization caches actor radius/speed/initial oriented box, uses conservative travel/rotation bounds before detailed actor forecasts, and skips a hold rollout when it cannot affect the selected controls. It preserves candidate scores, tie order, the controller and configuration. Safety bounds include legacy rail pose blends, the actual observed body at time zero and `sqrt(2)` corner-radius growth when both box half-extents are inflated. Pilot runs do not replace the required new 156-check recovery series and six city scenarios, with new executable/source hashes and preserved earlier attempts.

Read terminal `LONG-REJOIN` values against the readiness limits: lateral error at most 4 px, heading error at most 0.12 rad, forward velocity at least -2 px/s, lateral speed magnitude at most 6 px/s, yaw magnitude at most 0.2 rad/s and `along_px <= entry_limit_px`. `last_forecast` records the most recent periodic check, not a fresh terminal forecast: `not_tested` means readiness failed, `blocked` means readiness passed but the contact/forward-stop forecast failed, and `clear` means it passed. Current terminal values can differ from that earlier check. `cause` adds the following stage diagnosis without naming the blocking actor; it is not a proof that no global escape exists.

| `cause` | Meaning |
|---|---|
| `not_tested` | The last periodic alignment/velocity/block-position gate was not ready |
| `unavailable` | Invalid index/interval or an inactive, undrivable or non-traffic vehicle |
| `missing_snapshot` | Missing/inconsistent common observations or vehicle forecast storage |
| `capacity` | The bounded nearby-actor storage could not retain the complete neighbourhood |
| `initial_contact` | Actual uninflated initial footprint overlap; preferred over clearance-only contact elsewhere |
| `initial_clearance` | No actual initial overlap, but the configured clearance margin overlaps |
| `unsafe_sweep` | The forward rollout fails a swept-footprint/world-boundary or finite-state check |
| `incomplete_stop` | The checked stopping tail ends above 2 px/s |
| `clear` | The forward and stopping forecast passed |

The initial pre-change run on 2026-10-05 completed all three classes with **14 failed checks per class**. It exposed the legacy abandonment and blend behaviour. Its long result messages truncated timing fields, so the original evidence is retained. The `before/complete-timing` rerun preserved the geometry and checks and reproduced those failures. The pre-fix physical `after/final-recovery` run completed all three classes and **passed all 156 checks**. Its pre-fix first city run completed all six scenarios but exceeded CPU targets in crash, chase and rampage and recorded no completed rejoins. The [result report](design/traffic-recovery-results.md) retains both attempts, all 18 phase comparisons, exact manifest paths and hashes, complete timings and their limits: one seed, three classes, mostly static geometry and no dedicated world-edge fixture. Those runs preceded the false-depth contact fix. The complete corrected `after/final-fixed-recovery` series also passed all 156 checks, with garage rejoins at 10.833/10.600 s for Taxi, 20.967/21.100 s for Bus and 13.767/13.900 s for BoxTruck (60/20 Hz). The matching six-city series is complete; its CPU failures remain explicit in the result report.

The third increment's CPU work and its exactness checks against these fixtures are in the [third increment report](design/traffic-third-increment-results.md). Before CPU costs are artificially low once a driver abandons the car, so they do not represent equal completed work. Evaluate the explicit after cost and measured planner optimization attempts. The user recovery playtest and full 50-car/300-pedestrian CPU acceptance remain pending; isolated fixture acceptance does not close CJ-016. The second increment's re-run of this fixture on the final build, with unchanged rejoin times, is in the [yielding and incident report](design/traffic-yielding-incident-results.md#recovery-regression).

### Nearby-box clearance regression

`cj016-clearance-v1` is a separate regression for the city rejoin diagnosis; the original recovery fixture remains frozen. `OBBOverlap` initializes depth to a large sentinel and can return false at a separating axis without resetting it. `AddNearby` previously ignored that boolean and stored the positive depth, so a separated nearby building or vehicle could veto rejoin. The fix stores zero unless overlap is true; the overlap function's contract is unchanged. The new scene calls production `RecoveryCanRejoin` after a common actor snapshot and checks its contract independently of the ordinary recovery planner.

```bash
python tools/run_cj016_clearance.py --phase before --run-name depth-contract
python tools/run_cj016_clearance.py --phase after --run-name depth-contract
```

Build separately and run one visible test window at a time. The runner preserves logs, eight labelled case-end captures, the final screenshot and timestamped input hashes under `build/shots/cj016-clearance/<phase>/<run-name>/`. Failed acceptance with a complete fixture and exit status 1 remains complete evidence; absent results, checks or captures do not. `--dry-run` previews without launching or writing, and replacing evidence requires explicit `--replace`.

Each rate runs four Taxi cases, in order: `clear-nearby-boxes`, `actual-overlap`, `clearance-margin-only`, `forward-blocker`, suffixed `-60hz` or `-20hz`. Seed `0x000c0016` and uniform road are fixed. The clear case lasts 2 s with a side-separated building and a parked Taxi 200 px beside the ego throughout the interval. Its API must return true, and production traffic AI plus physics must actually rejoin within 2 s. The three 0.5 s guards keep the scene static: 2 px actual building overlap, a 0.5 px gap inside the 1 px clearance margin, and a wall 35 px ahead must each return false. They call the API without AI or physics and do not measure driving or CPU performance.

Every case checks the expected API result, unchanged ego position/angle/linear/angular velocity across explicit API calls and preserved active vehicle/driver/driver skin/vehicle skin/class. Changes to planner diagnostics are allowed. The two clear cases additionally check actual rejoin time, giving **26 checks across eight cases**. The 60 Hz render clock totals 420 frames and 7 s elapsed fixture time, with 280 explicit API calls and 160 actual physics steps over the clear cases' 4 s. At 20 Hz, API/AI/physics advance once per three rendered frames. Actors reset only between cases; frame-start kinematic poses are recorded before clear-case AI/physics to measure a real rail handoff. `CJ016C` records and captures must cover every scheduled case.

The pre-fix `before/depth-contract/manifest-20261005T170722846100Z-35064.json` completed every case, count and capture with **four failed checks**: clear API expectation and actual rejoin at both rates. All six obstacle guard cases and all body/ownership checks passed. Exit status 1 is retained as failed acceptance with complete evidence. The after `depth-contract` manifest completed all 26 checks with zero failures; both clear cases rejoined at 0.700 s and all six obstacle guards still rejected handoff. It matches the corrected full recovery/city executable. See the [result report](design/traffic-recovery-results.md#corrected-build-verification) for the exact manifests, fingerprints and retained city CPU failures.

## Cooperative yielding fixture

`cj016-conflict-v2` freezes five blockages that earlier rules left as permanent stand-offs and runs them with production traffic AI and physics on uniform road (world-edge contacts disabled). The first three are the unchanged cases of `cj016-conflict-v1`; the last two were added in the third CJ-016 increment:

| Situation | Set-up | Resolved when |
|---|---|---|
| `passing-head-on` | A Taxi passing a parked car in the oncoming lane meets an oncoming Taxi (seeds 0–4) or Bus (5–9) | The oncoming car has passed the passing car's start (at most 20 s), and the passing car has then passed the parked car back in its lane |
| `knocked-needs-room` | A knocked Taxi, nose 3–5 px from a kerb wall, can only reverse into the rail Taxi or Bus stopped 6–10 px behind it; a parked car in the oncoming lane prevents passing | The knocked car is back on its lane, and the car behind has driven on |
| `knocked-queue` | The same, with a second rail Taxi queued 10–16 px behind | As above; a role needs a chain role too |
| `knocked-pair` | The same front car, but the Taxi or Bus behind it was knocked too: turned 0.12–0.22 rad towards the kerb, its front corner 4–8 px from the wall and a wall 70–85 px behind it, less than a full reversing manoeuvre needs | Both knocked cars are back on their lanes |
| `junction-gridlock` | Four rail cars in a junction box, each nose 8–14 px short of the side of the car crossing ahead of it (north waits on west, west on south, south on east, east on north); seeds 5–9 make the northbound car a Bus. The junction's lights run as in the city | Two cars have moved off (resolved), then all four have driven two lengths on (completed) |

```bash
python tools/run_cj016_conflict.py --phase before --run-name cycles-20261006
python tools/run_cj016_conflict.py --phase after --run-name cycles-20261006
```

Ten seeds vary the gaps, angles, speeds and classes; every case runs at 60 Hz and 20 Hz physics on the 1/60 s render clock: 100 cases in one process, each ending 2 s after success or after 30 s. Every case checks unchanged ownership, finite state, no pose jump, static penetration and vehicle overlap at most 3 px, no rail car knocked, no role flip, the resolution times and, while a rail car changes lane, a rear-axle slip of at most 8° (`lane_change_slip_deg`; `CJ016Y slip` also reports the largest slip of any rail car, turns included). The first three situations also check exactly one role for the passing car (at most one for a car behind a knocked car, which some seeded poses do not need) and none for the priority driver; the two new ones check one to two (pair) or one to four (gridlock) roles in all. The before phase uses a copy of `traffic.cfg` with `YIELD cycles 0` (the pair rule of the second increment, nothing for knocked pairs or longer loops); the after phase uses the shipped file. The first increment's stand-off baseline (`YIELD enabled 0`) is kept in the `cj016-conflict-v1` evidence. `CJ_TEST_CASE=<substring>` narrows a diagnostic run. Results: [yielding and incident report](design/traffic-yielding-incident-results.md#cooperative-yielding) (v1) and [third increment report](design/traffic-third-increment-results.md#wait-for-cycles) (v2).

## Driver incident fixture

`cj016-incident-v2` stages a rear-end in a queue: a parked Taxi, a rail Taxi (seeds 0–4) or Bus (5–9) stopped behind it, and a third car placed 3–6 px behind that at 170–200 px/s, the instant before a late-braking rear-end. Production physics solves the contact; production traffic, impact, incident and pedestrian AI run in the game's order.

| Situation | Drivers | Expected |
|---|---|---|
| `aggressive-pair` | Both aggressive | One incident; both get out, approach, argue and fight (the fight starts at most 48 px apart, at least 0.5 s after the first exit); each survivor gets back into their own car. At least two shouts and 1 s of raised fist; no punch frame while arguing face to face |
| `calm-pair` | Both calm | No incident, nobody out; the knocked car rejoins its lane within 30 s |
| `aggressive-player` | The player's car hits an aggressive driver; the player stops, gets out 1 s after the contact and stands still | The driver gets out, confronts and fights the player (punches land), then drives the same car on. At least one shout and 1 s of raised fist; no punch frame while arguing face to face |
| `interrupted` | Both aggressive; the first car catches fire once its driver fights | That driver ends with `car_lost`; the other drives on |

```bash
python tools/run_cj016_incident.py --phase before --run-name incidents-20261006
python tools/run_cj016_incident.py --phase after --run-name incidents-20261006
```

80 cases (4 situations, 10 seeds, 60/20 Hz) run in one process, each ending 2 s after every drivable traffic car has its driver back and its lane, or after 60 s. Every case checks that no vehicle is removed or teleported, that every car–driver handle points back (`ownership_violations`), that a car is never both occupied and owned by a person on foot (`duplicate_drivers`), one incident per pair, a contact within 1 s and the settle time. Both phases run a copy of `traffic.cfg` with `INCIDENT confront_chance 1` (scripted aggressive drivers); the before phase also sets `INCIDENT enabled 0`. The per-0.5 s `CJ016I state` and `CJ016I vehicle` lines show every person's and car's state. Version 2 (third increment) adds the shout and gesture checks to the aggressive situations: shouts counted by the incident rules, and the seconds in which an arguing person is drawn with the raised-fist frames or, facing a person rather than a car door, with the punch frames (`PedDrawFrame`). Results: [yielding and incident report](design/traffic-yielding-incident-results.md#driver-incidents) (v1) and [third increment report](design/traffic-third-increment-results.md#incident-presentation) (v2).

## Turning fixture

`cj020-turns-v2` measures how rail traffic turns ([CJ-020](backlog.md#cj-020-turning-kinematics-of-traffic)). Production traffic AI drives one car of every traffic class (traffic weight above 0, police excluded) from 420 px before the box of junction (3, 3) on uniform road, northbound at its cruise speed, through the junction: right, left or straight on, at 60 Hz and 20 Hz on the 1/60 s render clock. The lights stay green. A case ends when the rear axle is 320 px past the box on the exit road and 40 px past the end of the turn path (a wide turn returns to its lane after the box), or after 30 s. 96 cases run in one process.

The junction's geometry is measured, not drawn from the map: square kerbs at the road edges (4 m from the centre line) and the right-hand half of every road. The stop-line zone, the first 60 px of a road past the box, is free when cars wait at a stop line 72 px back, so a corner there does not count as encroachment; the strict value, counting the zone too, is logged as `strict_encroachment_px`.

| Check | Meaning | Limit |
|---|---|---|
| `turning_slip_deg`, `straight_slip_deg` | Largest angle between the body and the motion of the rear axle point, while the heading changes / otherwise | 3° / 1° |
| `rear_radius_over_class_min` | Smallest circumradius of rear-axle points 16 px apart (a shorter span magnifies the path's chords into a smaller radius), over the class's tightest rear-axle radius from its `TURN` turning circle (turns only) | At least 0.98 |
| `lateral_accel_over_limit` | Largest lateral acceleration of the rear axle point, its speed times the yaw rate, over `TURN lateral_accel` (turns only) | At most 1.05 |
| `wheel_kerb_px` | Deepest overlap of the body between the axles (the wheels) with the four blocks round the junction | 1 px |
| `kerb_px` | The same for the whole body; checked for cars on a clean turn, logged otherwise (a large vehicle's overhangs may sweep over a corner) | 4 px |
| `encroachment_px` | How far a body corner reaches, outside the box and the stop-line zone, into the oncoming half of a road or into a road the car does not use | 2 px; 24 px (1.5 m) on a [wide turn](design/traffic.md#turn-paths) |
| `final_lateral_px`, `final_heading_deg` | Offset from the exit lane and heading error at the end | 1 px, 0.5° |

Each case also checks ownership, finite state, no pose jump, no knock and the time to reach the exit. A turn whose path does not fit the planner's limits, which traffic avoids, is still driven: its slip, radius and lateral acceleration are checked, its kerb and encroachment only logged (`fits=0`, `unfit_turns` in the summary). The `CJ020T measure` line adds the share of turning time over 5° of slip and the slowest speed in the turn. Each case ends with a capture of the rear (amber) and front (cyan) axle traces and the body outline every 0.2 s.

```bash
python tools/run_cj020_turns.py --phase before --run-name baseline
python tools/run_cj020_turns.py --phase after --run-name turns
```

The runner runs the fixture and then the six city scenarios (`day`, `chase`, `drive`, `overview`, `crash` for 1,500 frames, `rampage` for 3,600; the others 1,800), one window at a time, and keeps the logs, captures and a manifest with the git state and input hashes under `build/shots/cj020/<phase>/<run-name>/`. `--suite fixture` or `--suite city` runs one part. `CJ_TEST_CASE=<substring>` narrows a diagnostic fixture run. Results: [turning results](design/traffic-turning-results.md).

## Workflow

1. Run the relevant scenarios on the current code and keep the log.
2. Make the change.
3. Run the same scenarios again and compare the metrics.
4. Look at a screenshot series (`--every`) for anything the numbers cannot show.
5. Record new baselines here when a change intentionally moves them.
