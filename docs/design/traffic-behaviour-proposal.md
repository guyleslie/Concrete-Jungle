# CJ-016: Human-like traffic and incident proposal

Specification approved by the user on 2026-10-05 following their feedback on 2026-10-03: drivers resolve traffic situations through believable decisions and physical manoeuvres, retain their identity during incidents, and never disappear merely because a recovery timer expires. Implementation proceeds through the delivery increments below; production behaviour remains described in [Traffic](traffic.md).

## Contents

- [Pre-implementation findings](#pre-implementation-findings)
- [Behaviour contract](#behaviour-contract)
- [Architecture](#architecture)
- [Efficient execution](#efficient-execution)
- [Proposed acceptance tests](#proposed-acceptance-tests)
- [Delivery order](#delivery-order)
- [Research and licence boundaries](#research-and-licence-boundaries)

## Pre-implementation findings

These findings describe the implementation reviewed on 2026-10-03, before the first CJ-016 increment. The implementation and verification status is tracked in [Traffic](traffic.md#knocked-off-the-lane), [Testing](../testing.md#cj-016-recovery-measurements) and the [backlog](../backlog.md#cj-016-road-rage-and-traffic-incidents).

`UpdateKnocked` in `src/traffic.cpp` abandoned recovery after 12 s or below 35 % vehicle health. On-screen it spawned a pedestrian and cleared the vehicle's driver; off-screen it called `AIPlaceOnRoad`. Recovery steered towards one lane target and alternated forward/reverse without checking a complete escape manoeuvre. `SideClear` sampled positions along a shifted path rather than reserving the vehicle's future swept footprint against other moving traffic. The mutual-blocker tie-breaker could ignore an obstacle instead of negotiating an actual manoeuvre. The first increment addresses knocked recovery; ordinary rail passing and shared conflicts remain later work.

The existing pedestrian `Fight` state targets the player. Driver-versus-driver incidents need explicit actor targets, persistent ownership and safe vehicle exits, rather than simply invoking that state twice. The [CJ-002 baseline](vehicle-handling-baseline.md) remains useful: its matching city runs record 11/9/1 knocks/rejoins/give-ups in `crash`, 1/1/0 in `derby`, and 5/5/0 in `chase`. End-of-run abandoned-car counts include other causes and must not be mistaken for timeout abandonments.

## Behaviour contract

| Situation | Driver decision | Observable outcome |
|---|---|---|
| Ordinary queue or red light | Follow at a personal time gap; brake progressively and wait | No artificial frustration from an expected stop; no avoidance through an occupied sidewalk |
| A stationary car obstructs the route | Observe, leave room, signal/honk according to patience, evaluate passing and another route | Execute a checked manoeuvre or wait while explaining the blocked reason in diagnostics |
| Two cars obstruct each other | Identify the shared conflict and agree which yields or reverses to a reachable passing space | One proceeds while the other holds; priority persists until clearance instead of flipping each frame |
| A car is knocked sideways | Stabilise, assess damage and threats, inspect reachable forward/reverse paths, manoeuvre back | Repeated planning considers progress and previous failed actions; no timer-triggered exit or relocation |
| A genuinely impassable obstruction | Hold safely, reroute when possible, or request assistance | The vehicle and driver remain present; diagnostics distinguish physical blockage from planner failure |
| Minor impact | Stop safely; a calm driver may inspect and continue, a fearful driver may flee, an aggressive driver may confront the other driver | Reactions depend on the actual event and personalities; all collisions retain the physical response |
| Confrontation | Stop, secure the car, exit on a clear side, approach the other driver, escalate or de-escalate | Both participants have identities; a disagreement may end peacefully or become a fight |
| End of incident | Return to the same car, drive away, flee a threat, or remain with a disabled vehicle | No duplicate driver, instant re-entry, abandoned running car, or replacement pedestrian |
| Disabled/burning car | Exit safely if possible; flee immediate danger; request recovery if appropriate | Leaving has an explicit physical or behavioural cause rather than a generic recovery deadline |

Normal traffic should mostly succeed at driving. Accidents arise from reaction delay, distraction, misjudged gaps or aggressive manoeuvres evaluated against actual nearby traffic. Never manufacture collisions by randomly injecting impulses or suppressing the contact solver. An impatient driver can choose a smaller margin; the vehicle's dimensions and dynamics remain real constraints. Heavy vehicles may need more space and multiple reversals.

No vehicle or driver may be deleted, teleported, replaced or detached solely to break a local blockage, even when the camera temporarily turns away. Routine population recycling is allowed only outside the active simulation area and outside an ongoing incident, recovery, pursuit or player-observed interaction. Visible removal of disabled vehicles requires an actual recovery interaction. Tow-truck content is a later delivery stage; until available, a disabled car persists and traffic reacts to it.

## Architecture

Keep five separate responsibilities: observation, route intent, manoeuvre planning, control, and incident rules. The planning stages read one common world snapshot before any driver applies a new decision. This avoids frame-order advantages and makes a seeded test reproducible.

1. **Observation.** A vehicle spatial grid provides nearby cars, people, solid geometry and lane occupancy. Predict motion over a short horizon using velocity, yaw and known manoeuvre intent; refresh sooner when a conflict changes. Reuse the existing pedestrian grid and static-world indices.
2. **Route intent.** A lane graph provides a destination corridor and legal alternatives. A route is guidance, not permission to ignore an obstacle. Signal waiting, moving queues, temporary obstructions and broken vehicles have distinct reasons.
3. **Manoeuvres.** Generate a bounded set of forward, reverse, pass-left, pass-right, pull-aside, rejoin and hold candidates. Check swept oriented vehicle footprints, curvature/steering limits, braking room and predicted occupancy over the whole candidate. Score progress, collision risk, clearance, rule compliance, discomfort and personality. Retain a committed manoeuvre with hysteresis; replan when invalidated or demonstrably making no progress. Reverse must check behind the vehicle, including pedestrians.
4. **Shared conflicts.** Build local wait-for groups only among mutually blocked drivers. A stable priority and short-lived manoeuvre reservation assign a feasible yielding role. Reservations communicate intent but never hide a physical body from collision detection. Reject a reservation if another actor enters it. An impossible group waits or requests recovery instead of deadlocking an unlimited planner search.
5. **Control.** Drivers issue throttle, brake and steering; the contact solver owns positions. Use the CJ-002 axle model and measured class capabilities for speed, curvature and stopping limits. Until that model is available, expose the controller's dependency explicitly rather than assuming the arcade brakes are realistic.

The visible/interacting traffic ultimately needs the same physical integration contract as the player. Replacing rail poses requires a new decision record because [ADR-0004](../adr/0004-kinematic-rail-traffic.md) is Accepted. Proposed [ADR-0008](../adr/0008-human-like-traffic.md) addresses the original jitter/deadlock problems through measured tracking, prediction and conflict resolution. Do not change the accepted decision until the replacement is implemented and verified. Any distant traffic simplification must preserve actor identity, avoid external kinematic work during contact, and transition outside an interaction; start with the existing population of 50 cars before introducing simulation levels of detail.

Drivers have stable IDs and personality parameters: reaction time, desired following gap, patience, caution, aggression and courage. Vehicle ownership and pedestrian embodiment refer to generation-checked handles so recycled vector slots cannot become another person's car or opponent. One driver owns at most one active embodiment and one vehicle. An incident record retains participants, cause, phase and reservations. On-foot fighting uses an explicit target actor, reusing locomotion and punch mechanics while preserving their existing player interaction.

Personality profiles, thresholds and action budgets belong in `assets/data/traffic.cfg`, with validated defaults and a documented format. Emergency vehicles get role-specific policies, not an exemption from perceiving physical obstructions. Police incident response can initially reuse existing danger/pursuit rules; broader witness and crime attribution remains CJ-017.

## Efficient execution

Start on one thread with deterministic update order and contiguous cached data. Do not add worker-thread synchronization until measured CPU costs justify it. Rebuild the vehicle grid once per simulation frame; inspect neighbouring cells instead of rescanning every vehicle for every path sample.

Proposed scheduling: ordinary observation at 10 Hz, manoeuvre planning at 5 Hz, and conflict reconsideration up to 20 Hz for involved drivers. Stagger scheduled work by stable ID. Controls and a cheap immediate-hazard check run every frame. An incident or newly invalid manoeuvre triggers a priority replan. Allocate a fixed number of candidates and prediction samples per planning job; log exhausted budgets and defer nonurgent jobs fairly. Never reuse an unsafe command merely to meet a CPU budget: hold/brake while an urgent plan is pending.

Keep occupancy buffers and candidate storage reusable, with no per-driver heap allocation in the steady-state update. Keep random draws local to a driver/test seed so unrelated pedestrian spawning cannot change incident decisions. Profile traffic decision time separately from physics, pedestrian AI and rendering; screenshot capture frames are excluded.

## Proposed acceptance tests

Freeze fixtures and initial poses before implementation; record before/after logs, snapshots, seeds, hashes and action reasons. Run each feasible fixture with 10 fixed seeds at both 1/60 s and 1/20 s frame intervals. These numeric targets are proposed gameplay acceptance bands, not published human-driving statistics.

| Fixture | Acceptance target |
|---|---|
| Lane tracking and ordinary queue | No fixture collisions or sidewalk intrusion; no NaNs; no overlap above 3 px; steady lane-centre error at most 0.25 m outside deliberate avoidance; no incidents caused solely by a red light |
| Parked obstruction with a clear passing route | All 10 seeded cases pass the obstacle and rejoin within 20 s; zero disappearances, unsafe swept-path intersections or pedestrian hits |
| Passing route initially occupied by oncoming traffic | Yield while occupied, then clear within 20 s after the gap becomes available; no priority oscillation or forced collision |
| Mutual blockage with a reachable retreat space | Resolve every seeded case within 30 s using physical yielding/reversing; zero ignored bodies, ownership loss or timeout exits |
| Knocked Taxi, Bus and BoxTruck with feasible escape space | Each class recovers in every fixture within 30 s; record reverse distance, failed candidates and class-specific tracking error; no instant rail blend or pose correction |
| No feasible escape | Remain present and stable for 60 s; report `no_feasible_manoeuvre` or assistance state; no teleport, deletion or continuing throttle into a wall |
| Scripted aggressive-driver impact | At least one genuine physical contact from the prescribed manoeuvre; log cause, impact and both reactions; no synthetic collision impulse |
| Calm-versus-calm minor impact | No fight in the 10 fixed seeded fixtures; participants either safely resume or inspect as defined by their profiles |
| Scripted aggressive-versus-aggressive confrontation | Both actors stop, exit safely and approach before a fight; at most one incident per pair; an uninterrupted survivor returns to the same car or has a logged reason for leaving |
| Interrupt an incident with danger, injury or vehicle loss | No duplicate actor, invalid target or stale ownership; interruption leads to a documented state |
| Performance with 50 traffic cars and 300 pedestrians | Traffic decisions average at most 0.5 ms/frame and 95th percentile at most 1.0 ms/frame on the same reference machine; record worst frame and planning deferrals; total vehicle CPU average at most 1.0 ms/frame; retain CJ-010 pedestrian targets |

Blocked time alone must never select exit, despawn or relocation. Log every driver state transition with its actor ID, vehicle ID, reason, counterpart and chosen manoeuvre. Separate voluntary exits, injury/fire exits, successful recovery, impossible geometry and population recycling. Retain `crash`, `derby`, `chase`, `foot`, `day` and `rampage` regressions; the known rampage deep-overlap defect remains visible until fixed. Playtest queues, two-car conflicts, heavy-vehicle reversing and a full confrontation before accepting the result.

## Delivery order

Treat CJ-016 as the active item, with these reviewable increments inside it:

1. Instrument causes, add obstruction/conflict fixtures and record the baseline. Remove timer-based disappearance only together with a working persistent recovery/hold behaviour; deleting the timeout branch alone leaves cars pressing forever.
2. Implement observation, feasible manoeuvres and shared conflict resolution. Establish car and heavy-vehicle recovery before adding fights. Integrate the necessary CJ-002 controller capability as an explicit dependency, preserving its class measurements.
3. Add persistent driver ownership, safe exit/re-entry, personalities and event-based reactions; then driver-versus-driver confrontation and combat.
4. Profile and tune budgets, run regressions and playtest. Visible towing and additional incident content follow once the core is accepted.

## Research and licence boundaries

[CARLA's Traffic Manager](https://carla.readthedocs.io/en/latest/adv_traffic_manager/) provides a useful primary reference for a shared state cache, staged localization/hazard/control decisions, path-overlap prediction and driver parameters. Its implementation is [MIT-licensed](https://github.com/carla-simulator/carla/blob/master/LICENSE). Its documented blocked-vehicle destruction is explicitly outside this proposal's behaviour contract. Use architectural ideas; do not add CARLA as a runtime dependency.

[SUMO's sublane model](https://sumo.dlr.de/docs/Simulation/SublaneModel.html) is a reference for vehicle width, lateral clearance, lane-changing motivation and cooperation. Its parameters are simulation references, not automatic calibration values for this game. No SUMO code is imported. All sources remain conceptual references for this proposal; copied code would require its licence to be verified and recorded in CREDITS.md before use.
