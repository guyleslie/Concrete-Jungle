# Traffic

How traffic and police vehicles drive. Traffic follows its lane kinematically ("on rails") until something knocks it into physics; police cars are always physics-driven. The rationale is recorded in [ADR-0004](../adr/0004-kinematic-rail-traffic.md).

Source: `src/traffic.h`, `src/traffic.cpp`, `src/traffic_recovery.*`, `src/traffic_incidents.*`; recovery, yielding and incident tuning in `assets/data/traffic.cfg`; population management in `Game::UpdateSpawning` and `Game::UpdatePolice` (`src/game.cpp`).

The user approved the [CJ-016 specification](traffic-behaviour-proposal.md) on 2026-10-05. Its first increment covers knocked-vehicle recovery and persistent holding. The second increment (2026-10-06) adds a shared CPU budget for recovery planning, [cooperative yielding](#cooperative-yielding) for two drivers stopped behind each other, queue-aware rejoining and [driver incidents](#driver-incidents): stopping, getting out, confronting, fighting and returning to the same car. The third increment (2026-10-06) brings the decision CPU under its targets and resolves [wait-for cycles](#wait-for-cycles): two knocked cars and junction gridlock. The user playtested and accepted the result on 2026-10-07. Ordinary traffic stays on rails: a yielding driver retraces its own path kinematically. [ADR-0004](../adr/0004-kinematic-rail-traffic.md) remains Accepted and [ADR-0008](../adr/0008-human-like-traffic.md) remains Proposed. Measured results are in the [yielding and incident report](traffic-yielding-incident-results.md) and the [third increment report](traffic-third-increment-results.md).

## Contents

- [Rail driving](#rail-driving)
- [Route planning](#route-planning)
- [Speed control](#speed-control)
- [Junction rules](#junction-rules)
- [Obstacles and impatience](#obstacles-and-impatience)
- [Cooperative yielding](#cooperative-yielding)
- [Knocked off the lane](#knocked-off-the-lane)
- [Driver incidents](#driver-incidents)
- [Police](#police)
- [Population](#population)

## Rail driving

A traffic car on its lane is not simulated by the physics solver. It keeps a path — a list of waypoints along the right-hand lane — and a distance *s* along it. Every frame the AI advances *s* by its speed and places the car on the path: the rear axle (32 % of the length behind the centre) traces the path, and the body points along the path's tangent there (taken over 2 px either side). As a car's rear wheels follow and its front swings out, the rear axle never slides sideways, in turns as in lane changes, and long vehicles sweep wide with their front. The path through a junction is therefore the path of the rear axle; see [Turn paths](#turn-paths). A lateral offset moves the car out of its lane when it overtakes or mounts the kerb; see [Lane changes](#lane-changes).

### Lane changes

The offset is a function of the rear axle's path distance, not of time: it holds one value, follows a smooth S-curve over a stretch of path and holds the next. While it changes, the rear axle traces the shifted path and the body points along that path's tangent, the way a steered car's rear wheels follow and its front swings out; a car that is not rolling does not move sideways at all. The S-curve is long enough for its sharpest bend to stay within a comfortable turning radius (`max(90 px, 1.25 lengths)`) and, at speed, for a lateral acceleration of about 6 m/s²; it is at least one and a half car lengths. Returning to the lane after passing, the curve is fitted before the next stop line when the bend allows, and the car keeps to 130 px/s (about 30 km/h) while a lane change is in progress.

- **Pulling out** (overtaking or onto the kerb): before committing, the driver checks the body's swept poses along the whole curve, with 7 px to spare, against other vehicles (and, onto the sidewalk, buildings and solid furniture). Standing too close behind the obstacle, it first backs up along its driven path, 8 px at a time up to one and a half lengths, as far as the curve needs.
- **Looking ahead** during a lane change, the look-ahead tests the yawed body at each point ahead instead of a point at the front bumper, as the plan did.
- **Tucking back in while giving way** (see [Cooperative yielding](#cooperative-yielding)): reversing, the driver retraces its pull-out if it is still on it, or steers in over a reversing S-curve; the room behind is checked with the swept body.

Other drivers' forecasts use the same pose rule. `TRAFFIC slip` in the test log measures the angle between a rail car's body and the motion of its rear axle point, separately for lane changes, turning and straight driving.

### Turn paths

Each traffic class gets one right-turn and one left-turn path (`traffic_turns.*`, CJ-020), built for every traffic sprite while the game loads (about half a second) and reused at every junction, rotated to the approach. A path is an arc between the two lane lines with gradual steering in and out: clothoids, each half a radius long (`TURN easement`). Its radius is never tighter than the class can turn: the rear axle's tightest radius follows from the class's kerb-to-kerb turning circle (`TURN` records in `vehicles.cfg`), traced by the outer front wheel a wheelbase (0.64 lengths) ahead.

The planner drives the body along each candidate with the production pose rule and checks it against the junction's square kerbs and the halves of the roads:

| Limit | Clean turn | Wide turn |
|---|---|---|
| Wheels (the body between the axles) over a kerb | 0.5 px | 0.5 px |
| A car's body over a kerb | 2 px | no limit; large vehicles' overhangs may sweep over a corner, and the planner keeps it small |
| A body corner in the oncoming half of a road (not counting the 60 px stop-line zone past the box, which cars waiting at a stop line 72 px back leave free) | 1 px | 22 px |

A right turn takes the largest radius that fits, a left turn the radius closest to `TURN left_radius` (6 m). A class that cannot turn cleanly takes a wide turn: it moves up to `TURN max_swing` (2.5 m) towards the centre lines before the turn and back after it, and takes the junction box alone. A class whose turn fits neither way (a 12 m bus or the Semi between square corners) does not take that turn in traffic unless it has no other way; such forced turns are counted in the test log (`TRAFFIC turns planned`). Measured in the [turning fixture](../testing.md#turning-fixture): the cars with real-world widths turn cleanly; the vehicles whose sprites make them 30–40 % wider than their real counterparts need wide turns, and Bus, Semi and the right turns of BoxTruck, FireTruck, Garbage and Limo do not fit.

The paths of police cars are unchanged: they steer physically along the lane planner's curves.

Rail cars cannot jitter or deadlock in physics. They keep their distance by looking ahead along their own path, and in the solver they are infinitely heavy moving bodies that ignore buildings and street furniture (see [Physics › Traffic on rails](physics.md#traffic-on-rails)).

## Route planning

The path is planned one junction ahead and always extends at least 520 px beyond the car.

- At each junction the driver picks straight on (55 %, 70 % for large vehicles), right (25 %) or left (20 %, 10 % for large vehicles), among the exits that exist. A dead end at the city edge forces a U-turn.
- Traffic turns along its class's [turn path](#turn-paths); a turn the class cannot make within the limits is avoided unless it is the only way. If the turn would begin behind the car (a car rejoining close to a junction), the driver goes straight on. Police cars use smooth curves through the junction.
- Each turn is preceded by a stop waypoint that carries the junction, the signal axis and the planned manoeuvre.

## Speed control

| Influence | Behaviour |
|---|---|
| Cruise speed | 215–265 px/s (≈ 48–60 km/h); large vehicles 170–205 px/s. Panicking drivers go 50 % faster. |
| Curves | On a turn path, the speed keeps the lateral acceleration at the rear axle within `TURN lateral_accel` (3.5 m/s²) from how fast the body's heading turns at each point: about 13–15 km/h through a right turn and 16–20 km/h through a left turn. The driver brakes towards these limits at the comfortable rate. U-turns keep the old limit of 130 px/s (95 px/s for large vehicles). |
| Signals | Stops for red. Stops for yellow if it can do so comfortably. |
| Obstacles | A look-ahead of 40 px + 1.1 × speed + half its length along the path finds vehicles and, unless the driver is distracted, pedestrians on the road (found through the pedestrian grid). The allowed speed follows the gap; the car stops 14 px short of the obstacle. |
| Acceleration | 45 % of the class acceleration; comfortable braking at 670 px/s², hard braking at 900 px/s² for an obstacle right ahead. |

### Planned stop

Every frame a rail car records in `DriverAI::stopDist` how far its front can still travel before a stop it has planned: the stop line at a red light or a blocked junction, or 14 px short of a person, a stopped vehicle or a static obstacle ahead. Pedestrians read it, together with the planned path (`AIPathPose`), to predict whether the car will reach them (see [Pedestrians › Perception and dodging](pedestrians.md#perception-and-dodging)).

### Scan cost

The look-ahead runs for every rail car every frame, so its cost matters: it first samples its path points (continuing along the path from the previous sample and computing each segment's heading once), then tests only the vehicles whose enlarged box could reach the area around those points and only the people found in the grid cells that area covers. The obstacle found is the same as when every car and person is tested at every point.

## Junction rules

A car only enters a junction box when:

- the box holds no crossing traffic (vehicles going the same way are followed through; straight-on and right turns may meet oncoming straight-on and right turns, unless either is a [wide turn](#turn-paths), which takes the box alone);
- a left turn has no oncoming traffic about to come through on green;
- its exit lane has room for it, so it never blocks the box.

Directional right-of-way rules allow compatible movements through a junction and make left turns yield to oncoming traffic. They do not authorize ignoring an occupied vehicle footprint. A knocked car standing in the box is not a "mover" for these rules, so traffic can still enter and get stuck around it; drivers who end up waiting on each other in a loop are resolved by the [wait-for cycle](#wait-for-cycles) rule.

## Obstacles and impatience

| Situation | Reaction |
|---|---|
| Stopped behind a static vehicle for 1 s (scaled by the driver's temper) | Overtakes through the oncoming lane if it is clear; otherwise, except for large vehicles, mounts the kerb if the sidewalk is free of furniture and buildings. It steers out along an S-curve, backing up first if it stands too close ([Lane changes](#lane-changes)). |
| Two drivers stopped behind each other | One of them gives way and backs up; see [Cooperative yielding](#cooperative-yielding). |
| Two knocked cars, or three or more drivers, waiting on each other in a loop | One of them backs up or makes room; see [Wait-for cycles](#wait-for-cycles). |
| Blocked for 6 s | Makes a U-turn in the middle of the block, if it is far enough from the junction, the opposite lane is clear and every pose of the swept turn misses other vehicles (not for large vehicles). A refused U-turn keeps the current route. The rear axle follows a semicircle between the lanes, 2 m in radius: the widest one-move turn on an 8 m street, tighter than any car can steer; a realistic three-point turn is [CJ-022](../backlog.md#cj-022-three-point-turns). A dead end at the city edge uses the same semicircle inside the junction box. |
| Blocked, or someone standing in the road | Honks; impatient drivers honk sooner. |
| Distracted (random, about once every few minutes per driver) | Ignores pedestrians for 1–2.5 s — accidents happen. |
| Scared (gunfire, explosions, hit by the player) | Panics: drives faster and runs red lights. |

## Cooperative yielding

Each traffic driver keeps a *wait-for* edge: a rail car waits on the vehicle it is stopped behind (within 30 px, below 5 px/s); a knocked car holding with `no_feasible_manoeuvre` waits on every vehicle that rejected at least two of its rollouts. When two drivers wait on each other for `detect_time`, one takes the yielding role:

| Pair | Who gives way |
|---|---|
| A rail car and a knocked (physical) car | The rail car: the knocked car needs room to manoeuvre |
| Two rail cars, one further out of its lane (passing) | The one further out of its lane |
| Two rail cars otherwise | The one with retreat space when the other has none; then a fixed index order |

The role is stable: the other driver never takes the opposite role for the same pair, and the role ends only after the conflict has looked resolved for `clear_time` (the priority driver no longer waits on the yielder and is no longer in its way within two of its lengths). The yielder stays on rails and retraces its own driven path at up to `retreat_speed`, with comfortable braking, stopping 14 px short of any vehicle, person or the player behind it. Traffic keeps the driven path behind each car (at least 240 px or three lengths) for this. A passing driver tucks back into its own lane in reverse as soon as the swept way back is free of vehicles and people, retracing its pull-out or steering in over a reversing S-curve ([Lane changes](#lane-changes)), and reverses on until the rear axle is back in the lane; a car giving room to a knocked car backs up that car's length plus `retreat_extra`. If a car queued close behind blocks the retreat, it backs up too (a chain of at most `max_chain` drivers).

Other drivers' recovery forecasts see a yielding car's negative path speed and its retreat end. Yielding cars are not recycled.

A rail car held at its stop line because a car occupies the junction box, or has stopped on its exit lane, waits on that car too: the wait-for graph includes junction waits.

### Wait-for cycles

The pair rule above needs a rail car. Two knocked cars blocking each other, and loops of three or more drivers (junction gridlock: four cars in a box, each nose against the next car's side), are found once per frame (`AIResolveWaitCycles`), before any driver decides: every traffic driver has at most one wait-for edge, so following the edges from each car finds every closed loop. A loop that persists for `detect_time` gets one driver who gives way to the driver waiting on it (its predecessor in the loop):

| Candidate | Condition | Preferred |
|---|---|---|
| A rail car | At least 30 px of driven path free behind it | One whose predecessor is a knocked car that needs room; then the one with the most room |
| A knocked car | Can move at least `min_room` straight along its axis away from its predecessor | The one that made room for the same driver within the last 30 s, then the one with the most room |

Rail cars rank before knocked cars; ties go to the higher index. A rail car backs up along its driven path as in the pair rule. A knocked car *makes room*: its planner is replaced by short checked creeps away from its predecessor, 20 candidates of 0.3–1.2 s of driving at five steering angles, each followed by the checked stopping tail and checked against every actor like any rollout. The creep that opens the largest gap to the predecessor's box (at least 2 px) wins; it runs under the per-frame immediate check, then the car holds while the other driver plans its way out (its hold replans as soon as a blocker moves). After at most three creeps, or once the predecessor has not waited on the car for `clear_time`, the role ends and the car plans normally again. A pair that the pair rule has not resolved after three detection times is taken over the same way. A loop that nobody can open is assessed again every second and remains observable; nobody is moved or removed. The pass costs only the edge walk unless a loop persists.

`TRAFFIC yielding` in the test log reports roles taken, chains, unresolved mutual pairs and larger loops at the end of a run; `TRAFFIC wait cycles` reports how many loops formed, how many lasted over 10 s and the longest, and lists the members of loops still open at the end. A loop that has just formed at the end of a run (before `detect_time`) counts as open there, so the duration figures are the meaningful ones. See the [conflict fixture](../testing.md#cooperative-yielding-fixture).

## Knocked off the lane

A hard hit, an explosion, or pushing on something for a moment turns a rail car into a normal physics car (see [Physics](physics.md#traffic-on-rails)). The driver then goes through these stages:

1. **Stop.** Brakes, then holds the car with the handbrake. This stage lasts at least 0.7 s and at most 3 s.
2. **Assess and plan.** Read the common snapshot captured after vehicle fire, explosions and cleanup, before any vehicle decisions. Rebuild the pedestrian grid before capture so explosion-created actors and velocity changes are visible to every driver in the same decision stage. First predict lane-following feedback with the full safety checks; if it is safe, improves lane alignment and is not stalled, commit it without an escape search. Otherwise evaluate at most 20 candidates: hold, forward and reverse, including arcs that unwind their steering. Prediction uses the production vehicle forces, class dimensions and complete swept oriented footprints against buildings, solid furniture, vehicles and people, including the on-foot player. A checked stopping tail leaves room to halt before an obstruction.
3. **Drive or hold.** Issue throttle, brake, steering and handbrake controls; the physics solver owns the actual position and angle. Keep a committed move with hysteresis, replan after poor progress, and check for an immediate hazard between planning jobs. Stop before changing gear. If no safe candidate is found within the bounded local planner, hold with `no_feasible_manoeuvre` and retain the same vehicle and driver. This diagnosis is not proof that every possible global escape is impossible; the enclosed fixture has known impassable geometry. There is no recovery-time or low-health exit, replacement pedestrian or relocation.
4. **Rejoin without correction.** Physically align with the lane before resuming the existing rail route. Rejoining must not blend, snap or move the car into a lane pose. Damage, fire and driver injury remain separate game consequences; a low health percentage alone does not abandon a blocked car.

### Queues and contact

- **Queued behind a vehicle.** An aligned, stopped car whose every forward candidate is rejected by a stopped vehicle ahead in its lane corridor (below 8 px/s, not touching it) holds with `queued_behind_vehicle` instead of reversing away. Its rejoin check returns `queued_behind`: the car resumes its rail route at rest and the ordinary rail rules make it wait, pass or yield. Static geometry ahead still vetoes the rejoin, and any actual or clearance-margin contact still does.
- **Touched from behind.** A queued car in contact with a vehicle behind it (a rear-end) first creeps forward with a short checked rollout (0.15 s of drive plus the stopping tail) to release the contact, then waits.
- **Leaving an existing contact.** A rollout that starts in contact may escape it but never deepen it. Once the pose is outside the clearance margin, the no-deepening rule stays in force until the swept test of that step also clears; previously the conservative sweep pad rejected every slow escape on its first clear step, so a car pressed against a wall or another car could never move off.

### Planning budget

Rollouts are the cost of recovery: each force step costs about 0.25 µs for the vehicle forces and 0.3–0.6 µs for the swept checks and other drivers' forecasts on the reference machine. Recovery therefore schedules its work instead of letting simultaneous plans pile into one frame:

- **Sliced jobs.** A planning job runs its rollouts (lane tracking first, then the 18 arcs, then the conditional hold) one unit at a time across frames. All jobs share `planning_steps` force steps per 1/60 s of frame time; only the longest-waiting jobs may use it, so none starves. Candidate order, scores and the hold-first tie are unchanged; a hazard restarts the job. While a job is pending the car continues its committed move only while the per-frame immediate check passes, otherwise it holds.
- **Event-driven holds.** Each job records the vehicles and people that rejected its rollouts. A holding car (no feasible manoeuvre, or queued) replans only when its quantized pose, the number of static obstacles around it or the state of one of those blockers changes, and at least every 2 s. An actor that did not block cannot open a way by moving.
- **Covered immediate checks.** A full immediate check validates four extra frames (1/15 s) of the committed move and records the predicted poses at those frame boundaries and every vehicle or person that could reach the car within the horizon. The next frames reuse it only while the car stays within 1 px and 0.02 rad of its prediction and every such actor within 2 px of its linear forecast, with no newcomer and no more than 32 of them; otherwise the full check runs at once. With room for only 12 actors, crowds near sidewalks overflowed nine checks out of ten in `chase`.
- **Stationary forecasts.** A stopped rail car or a motionless body has one forecast pose after its first sample; residual motion is added to its sweep pad, which keeps contact depths exact.
- **Sleeping actors.** An actor found beyond its cull reach sleeps until the first force step at which it could be within reach again: each step the gap can shrink by at most the car's own travel, the actor's speed and a rounding allowance, and the reach can grow by at most the inflated sweep pads. A rail car's centre sits an axle length ahead of its rear axle along the path's tangent, so its reach also includes the axle length times the heading change along the part of its path a 4 s forecast can reach (at most twice the axle length); only that part of the path is copied into the frame's observation. The car's per-step travel and pad are bounded from its speed and yaw rate; a step that exceeds those bounds wakes every sleeper. Only awake actors are tested, in neighbourhood order, so the first actor to reject a rollout, and with it the recorded blocker, is the one the exhaustive loop would find.
- **Route-centre cull.** A moving rail car's forecast box sits at its route centre. Before the box (and its trigonometry) is built, the centre is compared with the reach the largest possible sweep pad could give: the centre travel plus the car's radius times the turn between two route directions, at most pi/2 times their cross product below a right angle. Nine out of ten rail forecasts in `rampage` ended there.
- **Cheap bookkeeping.** A hold between planning jobs neither checks nor plans, so it gathers no neighbourhood; the initial-contact test of a gathered actor is skipped beyond both enclosing circles; a new snapshot empties the forecast cache by advancing an epoch instead of touching every row; a rollout step reuses the sine and cosine of the heading its box was just posed at.

Two forecast rules removed spurious hazards that made recovering cars start, stop and replan every few frames (one car did so 453 times in `rampage`):

- **The rear-end rule.** A vehicle behind the car in its lane corridor, moving its way, keeps its own distance; forward rollouts and immediate checks do not treat it as an obstacle. Reverse rollouts still do. Forecast at constant speed, such a follower (a queued rail car or a patrolling police car) always ran into the stopping tail of a car moving off.
- **Planned stops.** A rail car is forecast no further than its planned stop (`DriverAI::stopDist`: the stop line, a person, a stopped or knocked car ahead), as pedestrians already predict it; a rail car stopped there is a stationary forecast.

Apart from the larger covered-check capacity, which lets more frames reuse a check and so changes city decisions, these changes leave the decisions bit for bit unchanged. All four fixtures, which never had more than 12 actors to record, reproduce their earlier results exactly. `RECOVERY CPU` reports deferred job-frames, the longest wait, holds kept unchanged and covered immediate checks; `RECOVERY WORK` and `RECOVERY WORST` report per-frame rollouts, force steps, awake actor tests and newly computed forecast boxes, and the six most expensive frames with their stage times.

Tuning is data-driven in `assets/data/traffic.cfg` ([format](../guides/adding-content.md#recovery)). The controller explicitly uses the current arcade model: its low-speed brake pedal engages reverse, so stationary holding uses the handbrake. CJ-002's axle model and class calibration remain pending; this increment does not claim that the arcade forces are calibrated human driving.

The forecast matches production force substeps in the measured 60 Hz and 20 Hz cases: respectively 240 Hz and 160 Hz. At faster frame rates it caps the forecast at 240 Hz to bound work, so it does not claim identical discretization at every frame rate. Predicted controls refresh at the elapsed actual frame interval, rounded to the next forecast sample, also bounded to 240 Hz. A full 4 s candidate uses at most 960 force steps. Moving-actor forecasts are cached and reused across candidates; complete swept and stopping checks also apply to the lane-tracking fast path.

The CPU optimization keeps the controller, candidate scores and tuning unchanged. Actor radii, centre speeds and initial oriented boxes are cached from the common observation. Conservative travel and rotation bounds reject actors that cannot reach a candidate's swept footprint before constructing their detailed forecast. Physical cars use their observed linear and angular velocity; rail cars include path travel, lane shift and any gap between the route and actual pose. A legacy rail pose blend bypasses the path-only bound and receives the complete forecast, with the observed actual body authoritative at time zero. Inflating both oriented-box half-extents can increase its corner radius by up to `sqrt(2)` times the margin; distance culls include this bound. The hold rollout is evaluated only if its score can affect a moving choice; original candidate order and hold-first ties are preserved. These changes require new isolated and city evidence before a measured cost improvement is claimed.

Terminal city diagnostics add `LONG-REJOIN` beside vehicles still knocked after 12 s. It records lane-tracking mode, lateral and heading error, lateral/forward velocity, angular velocity, distance to the upcoming junction and its required entry limit. `last_forecast` describes the most recent periodic rejoin check: `not_tested` means the alignment/velocity/block-position conditions were not ready; `blocked` means they were ready but the contact/forward-stop forecast failed; `clear` means that forecast passed. `cause` refines the tested result through `RejoinCause`: unavailable vehicle/call, missing snapshot, neighbourhood capacity, actual initial contact, clearance-only initial contact, unsafe sweep, incomplete stop or clear. Actual contact takes precedence over clearance-only contact elsewhere in the neighbourhood. Terminal pose values can differ from that last check. The line identifies which readiness conditions or forecast stage need investigation but does not identify a particular blocking actor or prove that a global escape is impossible.

The separate [nearby-box regression](../testing.md#nearby-box-clearance-regression) covers a contract the empty recovery scenes missed: a separated building or parked-vehicle box inside the local neighbourhood must permit a clear handoff. `OBBOverlap` initializes penetration to a large sentinel and can return false after a separating axis without resetting that output. Previously `AddNearby` stored the output without checking the boolean, falsely classifying separated boxes as initial contact and vetoing rejoin. It now records zero depth unless overlap is true. Actual overlap, clearance-margin contact and an obstructed forward stopping forecast must still reject rejoin. The regression checks both the API's unchanged-body contract and a real rail handoff. The pre-fix run completed all 26 checks with four failures, exactly the clear API result and handoff at both rates; the six obstacle guard cases passed. The corrected build passed all 26 clearance checks and all 156 frozen recovery checks, then completed six city regressions. Both clear-box cases rejoined at 0.700 s; obstacle guards still rejected handoff. See the result report for exact hashes, city outcomes and remaining CPU failures.

City profiling separates recovery planning from the full driver decision span. `DRIVER DECISION CPU` conservatively includes vehicle preparation, fire, explosions and wreck cleanup, pedestrian grid construction, the shared snapshot and traffic/police AI. It is an upper bound on traffic decision cost; physics, pedestrian AI, rendering and screenshot capture are outside that measured span. Isolated fixture timings cover their single driver and physics separately; they do not substitute for the 50-car/300-pedestrian CPU acceptance test.

The [recovery fixture](../testing.md#cj-016-recovery-measurements) covers Taxi, Bus and BoxTruck in open, enclosed and reverse-escape geometry at 60 Hz and 20 Hz. The [result report](traffic-recovery-results.md) preserves a pre-fix physical recovery build with all 156 isolated checks passing, compared with 14 failures per class before implementation. Its pre-fix first city run completed but exceeded CPU targets in crash, chase and rampage and recorded no completed rejoins. The complete corrected clearance/recovery/city series is recorded in that report: all 182 isolated checks pass, city rejoins occur and recovery give-ups remain zero. The third increment met the city CPU targets ([report](traffic-third-increment-results.md)), and the user accepted the behaviour in a playtest on 2026-10-07. These one-seed isolated cases do not establish moving-gap negotiations, simultaneous recovery intent reservations, city-edge acceptance or incident interruptions.

## Driver incidents

Every traffic driver has a mood drawn when the car is placed: `aggressive_share` aggressive, `calm_share` calm, the rest normal. A vehicle–vehicle contact with a closing speed of at least `min_impact` and a delta-V below `serious_dv` may start one incident per pair of cars (`pair_memory` prevents repeats); the player's car counts as a party.

| Driver | Reaction |
|---|---|
| Calm | Carries on (a knocked car recovers as usual) |
| Normal | Honks |
| Aggressive | With probability `confront_chance`: stops, waits a personal 0.8–1.6 s, gets out and confronts the other party |

1. **Stop.** A rail car pulls up where it is; a knocked car holds with the handbrake. Nothing else plans meanwhile.
2. **Get out on a safe side.** The driver's (left) door first, otherwise the right one: not into a building, a car or the path of a vehicle moving towards that point. With no safe side for `exit_wait`, the driver stays in and drives on (`no_safe_exit`). The person who gets out is the same driver: the car keeps a handle (index and serial) to them and they keep one to the car, which waits with the handbrake on and is protected from recycling.
3. **Confront.** The driver walks to the other driver if they are out, to the player on foot, or otherwise to the other car's door, shouting on the way, and argues face to face for `argue_time`, shouting and shaking a raised fist ([Pedestrians › Drivers on foot](pedestrians.md#drivers-on-foot), [Audio › Shouts](audio.md#shouts)); at a car door the fist comes down on the door.
4. **Escalate or end.** Two drivers who both came to argue fight; so does a driver facing the player on foot. Otherwise the argument ends. A fight lasts at most about `fight_time`; a person below 45 health backs off for 1.5–2.5 s. If the other party leaves by more than `give_up_distance`, the driver gives up.
5. **Return.** Fight over, flight over or interrupted, the driver walks back to the door of the same car, gets in and recovers to the lane physically. The car is never moved and nobody is replaced.

Interruptions end a driver's part with a logged reason instead of a duplicate or stale reference: `car_lost` when the car is destroyed, burning or taken (for example by the player), `driver_dead`, `driver_gone`, `no_safe_exit`. The empty car is then an ordinary abandoned car. Drivers on foot keep their identity: population recycling and second-hand panic leave them alone, so the winner does not run because the loser does. `INCIDENTS` in the test log counts starts, exits, confrontations, fights, returns and each interruption reason. See the [incident fixture](../testing.md#driver-incident-fixture) and, for the on-foot behaviour, [Pedestrians](pedestrians.md#drivers-on-foot).

## Police

Police cars are physics-driven at all times, like the player's car.

| Mode | Behaviour |
|---|---|
| Patrol / pursuit on the grid | Plans routes along the lanes, choosing turns that close in on the player when chasing. Ignores red lights; when not chasing it keeps its distance from traffic. |
| Direct pursuit | With line of sight within about 40 m, or when very close: steers straight at a point ahead of the player, rams, and handbrakes into sharp turns. |
| Stuck | Pressing on without moving for 1.8 s: reverses for 1 s. |

Police do not react to a fight between drivers. Playtesting found pursuit driving too aggressive: police run down pedestrians, ram uninvolved cars and cause crashes. Both are tracked in [CJ-018](../backlog.md#cj-018-police-driving-and-reactions).

Arrests, the number of police cars and when they give up are game rules — see [Gameplay › Police response](gameplay.md#police-response).

## Population

- 50 traffic cars drive at all times. Ordinary rail cars more than about 210 m from the player and off-screen are recycled to a random lane 55–160 m away, off-screen, so the city around the player stays busy. Knocked/recovering, persistently held, yielding and incident cars keep their vehicle and driver when the camera turns away.
- Up to 40 parked cars stand on parking spots, including police cars at the police station and ambulances at the hospital.
- Abandoned vehicles are removed once they are far away and off-screen.
