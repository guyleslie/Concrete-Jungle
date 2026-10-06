# Backlog

Planned work for Concrete Jungle, in priority order. Each item has a stable ID (`CJ-NNN`) that commits and documents can refer to.

## How to use this backlog

- Work on one item per session. Before implementing, agree a short specification: the target behaviour and measurable acceptance criteria.
- Add a [test scenario](testing.md) that measures the criteria whenever the item changes the simulation.
- When an item is finished, move it to [Done](#done) with the date, and add an entry to the [changelog](../CHANGELOG.md).
- New findings from playtesting become new items (or notes on an existing one) with the next free ID.

| Priority | Meaning |
|---|---|
| High | Directly hurts the game today; next in line |
| Medium | Clear improvement; schedule after the high items |
| Low | Polish or housekeeping |

## Next session

Recommended order, updated after the third CJ-016 increment on 2026-10-06:

1. **[CJ-016](#cj-016-road-rage-and-traffic-incidents) Human-like traffic and incidents** — the third increment is done: decision CPU under the targets, wait-for loops resolved, shouting, a raised fist and steered lane changes. Next: the user playtest (queues, two-car conflicts, heavy-vehicle reversing, a full confrontation, lane changes; temporarily `INCIDENT aggressive_share 1` makes every driver aggressive), under the [approved specification](design/traffic-behaviour-proposal.md). The user explicitly rejects removing drivers or relocating vehicles to resolve a blockage.
2. **[CJ-020](#cj-020-turning-kinematics-of-traffic) Turning kinematics of traffic** — rail cars still slide through tight right turns.
3. **[CJ-002](#cj-002-vehicle-handling-model) Vehicle handling model** — retain its approved specification and recorded baseline; implement the controller/physical capabilities needed by CJ-016 deliberately rather than changing traffic behaviour incidentally.
4. **[CJ-003](#cj-003-vehicle-damage-model) Vehicle damage model**, which builds on the same physics.
5. **[CJ-012](#cj-012-audio-overhaul) Audio overhaul** — positional sound, sirens, horns and effects.
6. **[CJ-004](#cj-004-replace-placeholder-art) and [CJ-011](#cj-011-relaxed-player-posture) Character art** — one art session: civilians, motorbikes, and a relaxed player.

CJ-010 (pedestrians) and CJ-013 (full screen) still await user playtest acceptance. The new traffic priority does not close them.

Before each item, search for open-source code, assets and references that would help (the licence must allow redistribution; see [Adding content](guides/adding-content.md#art-and-licence-requirements)). Examples worth evaluating: Box2D or Jolt Physics as a reference for CJ-002, recorded CC0 sound libraries for CJ-012, OpenGameArt and Kenney-style texture packs of the right quality for CJ-014.

## Summary

| ID | Title | Priority | Status |
|---|---|---|---|
| [CJ-010](#cj-010-pedestrian-behaviour) | Pedestrian behaviour | High | Implemented, awaiting playtest |
| [CJ-013](#cj-013-full-screen-only) | Full screen only | High | Implemented, awaiting playtest |
| [CJ-002](#cj-002-vehicle-handling-model) | Vehicle handling model | High | In progress |
| [CJ-003](#cj-003-vehicle-damage-model) | Vehicle damage model | High | Open |
| [CJ-012](#cj-012-audio-overhaul) | Audio overhaul | High | Open |
| [CJ-004](#cj-004-replace-placeholder-art) | Replace placeholder art | High | Open |
| [CJ-011](#cj-011-relaxed-player-posture) | Relaxed player posture | High | Open |
| [CJ-018](#cj-018-police-drivers-avoid-pedestrians) | Police drivers avoid pedestrians | Medium | Open |
| [CJ-014](#cj-014-city-art-and-layout) | City art and layout | Medium | Open |
| [CJ-015](#cj-015-street-lighting) | Street lighting | Medium | Open |
| [CJ-016](#cj-016-road-rage-and-traffic-incidents) | Human-like traffic and incidents | High | In progress; awaiting playtest of the third increment |
| [CJ-017](#cj-017-pedestrian-life) | Pedestrian life | Medium | Open |
| [CJ-019](#cj-019-performance-telemetry) | Performance telemetry | Medium | Open |
| [CJ-020](#cj-020-turning-kinematics-of-traffic) | Turning kinematics of traffic | Medium | Open |
| [CJ-005](#cj-005-data-driven-street-furniture) | Data-driven street furniture | Medium | Open |
| [CJ-006](#cj-006-collision-polish) | Collision polish | Low | Open |
| [CJ-007](#cj-007-large-vehicle-recovery) | Large-vehicle recovery after a crash | Low | Open |
| [CJ-008](#cj-008-test-autopilot-improvements) | Test autopilot improvements | Low | Open |
| [CJ-009](#cj-009-licence-for-the-code) | Licence for the code | Low | Open |

## Open items

### CJ-010 Pedestrian behaviour

- **Priority:** High
- **Status:** Implemented on 2026-09-27, awaiting playtest

**Problem.** Pedestrians panicked at normal passing traffic and ran into the road, where traffic hit them; knocked-down people never got up; people slid sideways; the city looked empty on foot.

**Done.** Predictive perception of vehicles, dodging, sidewalk-aware fleeing with one-step panic spreading, safe crossing at the lights with queueing at the kerb, heading-constrained locomotion with time-to-collision avoidance, tough people who fight back, energy-based injuries, bodies that stay where they fall and fade out, blood pools, a population of 300 around the player, a pedestrian grid. See [Pedestrians](design/pedestrians.md) and [ADR-0006](adr/0006-pedestrian-steering.md).

**Acceptance criteria.**

| Criterion (`foot`/`day`, 1,500 frames) | Before | Target | Result |
|---|---|---|---|
| Fleeing without cause, average | 29.9 | below 1 | 0.0 |
| On the road off a crossing, average | 11.7 | below 1 | 0.0 |
| Hit by traffic | 7 | 0 | 0 |
| Knocked-down people who never get up | about 3 | 0 | 0 |
| Visible on foot, average | 1.3 | at least 4 | 5.9 |
| `rampage`: people in the car's path who escape | 67 % | at least 70 % | 72–81 % |
| Pedestrian CPU time | 0.28 ms | at most 0.5 ms | 0.37–0.41 ms |
| Sliding (added after playtest feedback) | 4.0 % | close to 0 | 0.1 % |
| `crash`, `derby`, `chase` physics | see [Testing](testing.md#baseline) | no worse | no deep frames, penetration under 3 px |

Remaining: the playtest.

**Feedback request (2026-09-27, repeated 2026-09-28).** Follow-up feedback requested on walking, crossings, dodging and getting up. No new acceptance or defect report received yet; keep this item awaiting playtest.

### CJ-013 Full screen only

- **Priority:** High
- **Status:** Implemented on 2026-09-27, awaiting playtest

**Problem.** The game opened in a resizable window that could be minimised, resized or switched with F11, and the Windows mouse cursor was visible.

**Done.** The game starts in borderless full screen at the monitor's resolution and cannot be resized; the Windows cursor is hidden and the game draws its own crosshair on foot; Alt+F4 closes the game. The `--shot` test mode keeps the 1,600 × 900 window so screenshots stay comparable.

**Acceptance criteria.** Playtested: full screen after start, no window controls, no Windows cursor, crosshair visible on foot, Alt+F4 quits. Verified on 2026-09-27: the start-up log reports a 1,920 × 1,080 borderless window on a 1,920 × 1,080 monitor, and Alt+F4 ends the process.

**Playtest feedback (2026-09-27).** The user questioned whether a crosshair is needed and requested an approach consistent with GTA 1 and GTA 2. Their original PC manuals describe character rotation and attack controls rather than mouse aiming with a persistent reticle ([GTA 1 manual](https://www.bestoldgames.net/download/games/grand-theft-auto/grand-theft-auto-manual.pdf), [GTA 2 manual](https://mocagh.org/miscgame/gtaset-gta2-manual.pdf)). Follow up on removing the persistent crosshair and verifying that shot direction remains readable with Concrete Jungle's mouse aiming; coordinate with [CJ-011](#cj-011-relaxed-player-posture). This revisits the crosshair acceptance criterion above; no HUD or control change is implemented in the CJ-002 session. Full-screen operation, cursor hiding and Alt+F4 have not yet received new user acceptance.

### CJ-002 Vehicle handling model

- **Priority:** High
- **Status:** In progress; specification approved, isolated baseline recorded

**Specification approved (2026-09-28).** [Vehicle handling proposal](design/vehicle-handling-proposal.md), with source/licence research, numeric targets for all 17 shipped classes, rear-brake profiles, prescribed-speed collision cases and a baseline-first measurement plan. [ADR-0007](adr/0007-dynamic-vehicle-handling.md) stays Proposed until implemented. Work stays directly on `main` for this session, as requested.

**Baseline review (2026-10-03).** All 17 classes completed their isolated handling measurements; the collision fixture completed 122 phases with 18 failed acceptance checks. The recorded executable, configuration and fixture hashes match the current build. See the [baseline report](design/vehicle-handling-baseline.md) for class results, collision failures and limitations of the old model's slip/skidpad telemetry. A fresh Taxi run reproduced all 97 metric records, including its missing stop and 23 failed checks. All six city reference runs completed on the matching build; `crash`, `derby` and `chase` reproduced the documented physics reference, with no deep frames. Production handling remains unchanged. Next: implement the approved axle model and compare against this evidence; keep CJ-002 open for the driving playtest.

**Problem.** Playtesting shows the vehicles are not controllable enough; the handling maths is not right. The physics is far from perfect: collisions need much closer attention, the vehicle types must handle clearly differently, and the game needs a reasonably realistic physics engine so that driving and every other vehicle action feel real.

**City collision finding (2026-10-03).** The matching `rampage` baseline recorded 4 deep-overlap frames and a maximum penetration of 5.4 px. Keep this visible alongside the isolated collision failures; a zero process exit status is not proof of collision acceptance.

**Current state.** An arcade model (`VehicleForces` in `src/vehicle.cpp`, described in [Vehicles › Handling model](design/vehicles.md#handling-model)): steering sets a target yaw rate, and sideways velocity decays exponentially. The class values in `vehicles.cfg` are 4–9 times real-world figures (for example 90 m/s² braking). The contact solver ([ADR-0005](adr/0005-vehicle-contact-solver.md)) is sound but was tuned for the arcade model.

**Proposed approach.** A bicycle model: slip angles at the front and rear axle, a tyre force curve that saturates, load transfer under braking and acceleration, drivetrain (front, rear, four-wheel drive), mass and moment of inertia from the class. All parameters in `vehicles.cfg`. Keep the physics integration contract: the model only changes velocities. Revisit the impact response (restitution, friction, spin) with the new model. Study open-source references first (for example Box2D's top-down car sample, Jolt Physics vehicle constraints, Marco Monster's car physics notes).

**Acceptance criteria.**

- A written target for the feel, agreed before implementation, with reference numbers per vehicle type (braking distance from 100 km/h, 0–100 km/h time, maximum cornering acceleration, handbrake-turn behaviour, top speed).
- A handling test scenario that logs those numbers; every shipped class meets its targets, and the classes differ measurably.
- Impacts: a documented table of expected outcomes (glancing blow, T-bone, head-on, car versus truck) that a `crash` variant checks.
- The `crash` and `derby` metrics are no worse than the [baseline](testing.md#baseline).
- Playtested and accepted.
- [Vehicles](design/vehicles.md) and [Physics](design/physics.md) updated; a new ADR records the model change.

### CJ-003 Vehicle damage model

- **Priority:** High
- **Status:** Open

**Problem.** When and how a vehicle breaks down is not convincing yet.

**Current state.** A single health value per vehicle. Crash damage follows the impact's delta-V (`DV_*` constants in `src/game.cpp`); the engine smokes below 50 % and 25 % health, catches fire at zero and explodes 3–5 s later. There is no visible deformation, and the delta-V balance has not been playtested. See [Vehicles › Damage, fire and explosions](design/vehicles.md#damage-fire-and-explosions).

**Ideas.** Separate subsystems (engine, body, tyres, lights) with their own effects — power loss, pulling to one side, flat tyres, broken headlights; visible damage on the sprite; clear rules for fire and explosion; driver injury tuning.

**Acceptance criteria.**

- A rules table, agreed before implementation, stating what each kind of hit does and when a vehicle is disabled, catches fire or explodes.
- Visible damage states for cars.
- Test scenario output that shows the damage progression for a fixed series of impacts.
- Playtested and accepted; [Vehicles](design/vehicles.md) and [Physics](design/physics.md#crash-consequences) updated.

### CJ-012 Audio overhaul

- **Priority:** High
- **Status:** Open

**Problem.** Sounds do not tell where things are, and they are monotonous. Every horn is the same sound at a different pitch; the siren is one loop; deaths, collisions and brakes sound thin. Nearly every important effect needs a better, more varied and realistic sound.

**Current state.** All sounds are synthesised at start-up (`src/audio.cpp`, [Audio](design/audio.md)). One-shots fade with the square of *1 − distance / 100 m* from the camera and are panned by the horizontal offset only. The siren is a single loop whose volume follows the nearest active siren (linear to 94 m), with no direction. The horn is one synthesised tone; traffic varies only its pitch.

**Scope.**

- **Positional sound:** a realistic distance law (inverse distance with a reference distance and a roll-off), panning for every loop including sirens and engines of other vehicles, optionally a slight Doppler shift for passing sirens.
- **Horns:** several realistic horn types chosen per vehicle class in `vehicles.cfg` (single-tone and dual-tone car horns, truck air horn, bus, scooter), and horn patterns chosen by the driver's mood: a short tap, a double tap, a long lean on the horn.
- **Sirens:** police, ambulance and fire sirens with wail, yelp and phaser modes, switching between slow and fast, and stopping and starting like real ones.
- **Effects:** varied recorded sounds for deaths and screams, punches, impacts by severity and material, tyre screech and braking, glass, explosions, footsteps on different surfaces.
- Recorded sounds from sources that allow redistribution (for example CC0 libraries) instead of synthesis wherever synthesis sounds artificial; several variants per sound, picked at random.

**Acceptance criteria.** A sound list with sources and licences agreed before implementation; at least three variants for each frequent sound; horn and siren types configurable in the data files; a test scenario that logs which sounds played at which volume and pan; playtested and accepted; [Audio](design/audio.md) and [CREDITS](../CREDITS.md) updated.

### CJ-004 Replace placeholder art

- **Priority:** High
- **Status:** Open

**Problem.** Motorbikes and civilian pedestrians use procedural placeholder sprites that do not match the quality of the rest of the art. Playtest feedback (2026-09-27): the civilians look much cheaper than the player, their walk animation does not look real, and standing civilians look smaller than the player, because the player sprite holds its arms forward while the civilians are drawn thin from the top.

**Notes.** No free, high-resolution top-down motorbike or civilian art has been found yet. itch.io listings show a bot check to automated browsers, so they must be searched manually. The player's Survivor sprites are a soldier in a combat stance, so recolouring them into civilians (GTA 2 style remaps) was considered and rejected. Any new art must meet the [art requirements](guides/adding-content.md#art-and-licence-requirements); consider rendering sprites from open-source 3D character models (for example CC0 models rendered top-down) if no 2D set exists.

**Acceptance criteria.** Motorbikes (sport, chopper, scooter, with and without rider) and civilians replaced by art of consistent quality, with walk, run, idle, punch and lying frames, added through the data files, credited in [CREDITS.md](../CREDITS.md). Civilians and the player read as the same size.

### CJ-011 Relaxed player posture

- **Priority:** High
- **Status:** Open

**Problem.** The player always looks as if in action. Unarmed, the character holds its fists up as if about to fight; armed, it constantly aims. In GTA the character walked normally and only raised the weapon to aim or fire.

**Current state.** The player uses the "Animated Top Down Survivor Player" sprites ([CREDITS](../CREDITS.md)), which only contain combat poses; the `unarmed` set was derived from the knife frames.

**Scope.** A character sheet with the arms down (idle, walk, run), a weapon carried lowered or holstered while not aiming, and a short raise transition when the player aims or fires. Needs new art (see CJ-004 for sources).

**Acceptance criteria.** Walking without aiming shows a relaxed pose for every weapon; aiming or firing raises the weapon within 0.2 s; playtested.

### CJ-018 Police drivers avoid pedestrians

- **Priority:** Medium
- **Status:** Open

**Problem.** Police cars in pursuit drive at up to 150 km/h through crowds and on sidewalks and do not react to people at all; in the `brawl` scenario they caused most of the pedestrian deaths.

**Acceptance criteria.** Police cars brake for and steer around people when it does not cost them the pursuit, mount the sidewalk only when necessary and slowly; in `chase` and `brawl`, pedestrians hit by police drop by at least 80 %.

### CJ-014 City art and layout

- **Priority:** Medium
- **Status:** Open

**Problem.** The city relies on one downloaded texture set and looks repetitive. Parks need to be redesigned, and the city lacks fences and many small details.

**Scope.**

- More ground, wall and roof textures of the same quality, with variation per district.
- Parks rethought: paths, lawns, fences and hedges, gates, playgrounds, ponds.
- Fences, walls and gates around lots, courtyards and parking lots.
- The gaps between buildings (service alleys) furnished: bins, back doors, fire escapes, puddles.
- Parking lots: markings, barriers, ticket machines, lamps.
- The elevated metro where it runs above the streets: pillars, the underside of the deck, shadows and lights.
- Small details everywhere: road wear, manholes, signs, awnings, shop fronts.
- Evaluate open-source city generators and asset packs before building everything by hand.

**Acceptance criteria.** A before/after screenshot set of the same views agreed with the user; new content defined in data files where possible; playtested.

### CJ-015 Street lighting

- **Priority:** Medium
- **Status:** Open

**Problem.** The light from the street lamps does not fall where it should, the lamp posts themselves could look better, and the lighting of the whole game at night could be much better, with a cooler colour and without the current bugs.

**Acceptance criteria.** A list of the lighting bugs with screenshots; lamp light pools aligned with the lamp heads; a night palette agreed with the user; before/after screenshots at dusk and at night; see [Rendering](design/rendering.md).

### CJ-016 Road rage and traffic incidents

- **Priority:** High
- **Status:** In progress; specification approved on 2026-10-05; recovery (2026-10-05), CPU peaks, cooperative yielding and driver incidents (2026-10-06) implemented and measured; city CPU targets met, wait-for loops resolved, shouting, raised fist and steered lane changes (2026-10-06); the user playtest pending

**User direction (2026-10-03).** Drivers should behave like people: follow, yield, avoid, reverse, honk, sometimes misjudge and collide, and occasionally get out to confront or fight another driver. A blocked situation must not be solved by deleting a driver, forcing them to walk away after a timer, or relocating an involved vehicle. This is the current priority; CJ-002 supplies the necessary vehicle capabilities.

**Pre-change problem.** `UpdateKnocked` gave up after 12 s or below 35 % vehicle health; it spawned an unrelated pedestrian on-screen or relocated the vehicle off-screen. Recovery alternated controls towards one target without validating a full escape path. Pedestrian fighting still targets the player, so a driver-versus-driver incident also needs persistent ownership and explicit actor targeting.

**Approved specification.** [Human-like traffic and incident proposal](design/traffic-behaviour-proposal.md): predicted occupancy, feasible swept-footprint manoeuvres, stable yielding roles, impossible-blockage handling, persistent drivers, cause-based incidents, reusable spatial queries and measured CPU budgets. [ADR-0008](adr/0008-human-like-traffic.md) is Proposed; the accepted rail decision remains in force until a tested replacement addresses its original jitter/deadlock concerns.

**First increment (2026-10-05).** Frozen `traffic-recovery` fixtures measure Taxi, Bus and BoxTruck in enclosed, free and reverse-escape geometry at 60 Hz and 20 Hz: 52 checks, 14,400 rendered frames and 240 s per class. The complete baseline had 14 failed checks per class; the corrected build passed all 156 recovery checks. A separate 26-check clearance regression reproduced four pre-fix failures caused by separated nearby boxes being falsely classified as initial contact, then passed every check after the fix. The [result report](design/traffic-recovery-results.md) preserves exact manifests/hashes, timings and failed intermediate attempts. Corrected garage recovery took 10.833/10.600 s for Taxi, 20.967/21.100 s for Bus and 13.767/13.900 s for BoxTruck at 60/20 Hz; enclosed cars stayed with their drivers for 60 s with no movement, overlap, blend or ownership loss. All six matching city runs completed and recorded zero recovery give-ups; crash/chase each recorded one rejoin and rampage recorded eleven. The crash/rampage CPU failures remain open in the report. The controller uses a shared snapshot and at most 20 physical forward/reverse/hold candidates, with safe progressing lane feedback skipping escape search. Path scanning no longer ignores a mutually blocking vehicle; ordinary rail passing and junction right-of-way rules remain. Full-population CPU acceptance, city-edge/moving/multi-seed fixtures and user playtest remain pending. Cooperative conflict roles/reservations, persistent on-foot ownership and fights remain to be implemented; CJ-002 remains an explicit handling dependency.

**Second increment (2026-10-06).** Recovery planning is sliced across frames under a shared deterministic step budget; holds replan only when a blocker moves; followers behind a car keep their own distance (the rear-end rule) and rail cars are forecast no further than their planned stop; immediate checks validate a few extra frames and are reused while everything moves as forecast. The worst city decision frame fell from 11.6 ms to 2.5 ms; `crash` meets both decision targets (0.434 ms average, 0.933 ms p95), while `chase` (0.739 / 1.455 ms) and `rampage` (0.799 / 1.617 ms) still exceed them. Cooperative yielding gives two drivers stopped behind each other stable roles; the yielder retraces its own path on rails and tucks back into its lane, with a chain behind it. The new `traffic-conflict` fixture (60 cases) went from 6 to 60 resolved cases. Driver moods (10 % aggressive) and collision incidents let an aggressive driver stop, get out on a safe side, confront the other driver or the player, fight, and drive the same car on; interruptions end with a logged reason. The new `traffic-incident` fixture (80 cases) went from 20 to 80 accepted cases with no ownership violation or duplicate driver. Queue-aware rejoining, escape from an existing contact and a fixed U-turn route loss came out of the new fixtures. The frozen recovery and clearance fixtures still pass all 182 checks with unchanged rejoin times. Evidence: [yielding and incident report](design/traffic-yielding-incident-results.md).

**Third increment (2026-10-06), CPU.** Every city scenario now meets the decision CPU targets: `chase` 0.689 / 1.415 → 0.321 / 0.819 ms, `rampage` 0.727 / 1.547 → 0.378 / 0.861 ms, `crash` 0.390 / 0.865 → 0.234 / 0.484 ms (average / 95th percentile). The rail look-ahead tests only what can reach its sampled path, recovery rollouts let distant actors sleep and skip rail forecast boxes out of reach, and force steps share their trigonometry; these leave every decision unchanged, and all four fixtures reproduce the second increment line by line. Covered immediate checks now record up to 32 actors instead of 12, which changes city decisions. On-foot test runs no longer depend on the mouse cursor. Evidence: [third increment report](design/traffic-third-increment-results.md).

**Third increment (2026-10-06), wait-for cycles.** Two knocked cars blocking each other and loops of three or more drivers (junction gridlock) are found once per frame; one driver gives way to the driver waiting on it, a rail car by backing up, a knocked car by short checked creeps that make room. A car held at a stop line waits on the car in the box. The conflict fixture (v2, 100 cases) adds `knocked-pair` and `junction-gridlock`: 0 → 20 of 20 each, with the 60 earlier cases unchanged. The city runs had no loop lasting over 10 s; the "unresolved pair" at the end of `crash` is one that formed 0.3 s before the end.

**Third increment (2026-10-06), presentation.** Arguing drivers shout (three synthesised voices at a personal pitch, every second or so) and shake a raised fist (two new civilian atlas frames); the punch frame is kept for blows. Rail cars change lane by steering: the lateral offset follows an S-curve along the path, the rear axle traces it and the body points along it; a car standing too close behind an obstacle backs up before pulling out, and a yielding car reverses back into its lane the same way. Incident fixture v2: 40 → 80 of 80 (shouts 0 → 4–10 per case, raised fist 0 → 1.5–4.4 s, punch frame while arguing face to face 0.5–1.5 → 0 s); conflict fixture: lane-change slip 90° → 2.1°, 80 → 100 of 100. The slip measurement also found turning traffic sliding (CJ-020).

**Remaining.** Police reaction to a fight; the user playtest of queues, two-car conflicts, heavy-vehicle reversing and a full confrontation. Visible towing follows the accepted core.

**Acceptance criteria.** Follow the approved proposal's scenario table. Feasible blockages resolve physically; impossible ones remain stable and observable; timeout-based disappearance/relocation is zero; incident participants retain identity and car ownership; exiting, approaching, fighting and returning are visible actions with interruption rules. Validate with fixed seeds, 60 Hz/20 Hz scenarios, class-specific recovery tests, regression runs, separate traffic CPU timings and a user playtest. First deliver reliable manoeuvring, then incidents and combat; visible towing follows the accepted core.

### CJ-017 Pedestrian life

- **Priority:** Medium
- **Status:** Open

**Idea.** Follow-ups to CJ-010 that make people look smarter and the city more alive:

- witnesses report crimes, and the wanted level depends on being seen;
- couples and groups walking together, people stopping to chat;
- using phone booths, waiting at bus stops and boarding buses, sitting on benches;
- reacting to a drawn gun (hands up, running), to the player's car honking, to sirens;
- drivers who get out of cars and people who get into parked cars;
- bloody tyre tracks after driving through blood.

**Acceptance criteria.** One behaviour at a time, each with its own measurable criteria and scenario; the `foot` metrics of CJ-010 stay met.

### CJ-019 Performance telemetry

- **Priority:** Medium
- **Status:** Open

**Idea.** Measure how fast the game runs, when and why it slows down. The `TIMING` log line (CPU time of the vehicle update, the pedestrian update and the world drawing) is a start.

**Acceptance criteria.** A frame-time log over the run (average, 95th and 99th percentile, worst frame, and the frames above 20 ms with what was happening), frames that save screenshots excluded; per-system CPU times including physics, traffic, particles and each render pass; an optional on-screen graph (F3 debug view); documented in [Testing](testing.md).

### CJ-020 Turning kinematics of traffic

- **Priority:** Medium
- **Status:** Open (found by the rear-axle slip measurement on 2026-10-06)

**Problem.** Rail cars turn on a quadratic curve whose control point sits at the two lane offsets, so a right turn has a radius of about 2 m; real cars need about 5.5 m. Both axle samples stay on that curve, so the body follows the chord and the rear axle point slides sideways through the bend. `TRAFFIC slip` measured in `chase` and `day` on 2026-10-06: over 5° of rear-axle slip in 72–74 % of the turning time, up to 70–75°. Lane changes no longer slide (CJ-016); turns still do.

**Acceptance criteria.** Rear-axle slip while turning at most 8° (`TRAFFIC slip`, turning) in every city scenario; turning paths sized for each class's turning radius, large vehicles included; the junction rules keep working (no new box conflicts, `TRAFFIC stopped because` comparable); before/after screenshots of turning traffic; playtested.

### CJ-005 Data-driven street furniture

- **Priority:** Medium
- **Status:** Open

**Problem.** Street furniture sizes, collision shapes and breakaway materials are defined in code (`Dim` and `ApplyMaterial` in `src/city_map.cpp`), unlike every other kind of content ([ADR-0002](adr/0002-data-driven-content.md)).

**Acceptance criteria.** A `props.cfg` (or similar) defines each prop's size, collision shape, bullet breakability, strength and loose mass; the city generator reads it; [Adding content](guides/adding-content.md#street-furniture) documents it.

### CJ-006 Collision polish

- **Priority:** Low
- **Status:** Open

Smaller improvements left after the collision rewrite:

- a continuous grinding sound while scraping along walls (now sparks and a short crash sound);
- wheel-spin smoke when a vehicle presses against an obstacle under full throttle;
- knocked-over poles are decoration only; they could become low obstacles;
- dumpsters are rigid; they could be pushable bodies;
- a shrub driven over must stay as a flattened, broken shrub, with leaves and twigs flying, instead of disappearing; destructible scenery in general should leave realistic remains.

### CJ-007 Large-vehicle recovery

- **Priority:** Low
- **Status:** Open

**Problem.** Before CJ-016, a 12 m bus knocked off its lane often failed to manoeuvre back and gave up after 12 s. CJ-016's first increment covers Bus/BoxTruck recovery fixtures; broader heavy-vehicle city recovery and playtest acceptance remain open.

**Acceptance criteria.** Buses and trucks recover to their lane in most knock-offs in the `chase` and `derby` scenarios, without teleporting on-screen.

### CJ-008 Test autopilot improvements

- **Priority:** Low
- **Status:** Open

The `drive` autopilot never reverses, so it stays stuck once it drives into something (since 2026-09-27 it ends up against a building corner). It should back out like the `derby` autopilot. Handling scenarios for [CJ-002](#cj-002-vehicle-handling-model) will need scripted manoeuvres as well.

### CJ-009 Licence for the code

- **Priority:** Low
- **Status:** Open

The project's own code has no licence yet. Choose one (and add a `LICENSE` file) before the code is shared. Third-party asset licences are already listed in [CREDITS.md](../CREDITS.md).

## Done

| ID | Title | Completed |
|---|---|---|
| CJ-001 | Documentation overhaul: documentation structure, design documents, guides, testing guide, ADRs, changelog, contributing guide | 2026-09-27 |
| — | Collision physics rewrite: vehicles no longer stick to obstacles or jitter; breakaway street furniture; traffic recovery after crashes ([ADR-0005](adr/0005-vehicle-contact-solver.md)) | 2026-09-27 |
