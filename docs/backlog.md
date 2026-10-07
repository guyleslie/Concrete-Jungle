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

Recommended order, updated after CJ-020 on 2026-10-07:

1. **[CJ-018](#cj-018-police-driving-and-reactions) Police driving and reactions** — police in pursuit run down pedestrians, ram other cars and cause crashes; they do not react to a fight between drivers. Needs its own session and specification.
2. **[CJ-023](#cj-023-vehicle-widths-and-street-geometry) Vehicle widths and street geometry** — the streets feel cramped; vehicles are 30–40 % too wide and junction corners are square, so buses and trucks cannot turn.
3. **[CJ-002](#cj-002-vehicle-handling-model) Vehicle handling model** — retain its approved specification and recorded baseline; implement the controller/physical capabilities that [ADR-0008](adr/0008-human-like-traffic.md) still needs deliberately rather than changing traffic behaviour incidentally.
4. **[CJ-003](#cj-003-vehicle-damage-model) Vehicle damage model**, which builds on the same physics.
5. **[CJ-012](#cj-012-audio-overhaul) Audio overhaul** — positional sound, sirens, horns and effects.
6. **[CJ-004](#cj-004-replace-placeholder-art) and [CJ-011](#cj-011-relaxed-player-posture) Character art** — one art session: civilians, motorbikes, and a relaxed player.

CJ-020 (turning) awaits the user's playtest: the turning speed and the look of wide turns. CJ-010 (pedestrians) and CJ-013 (full screen) still await user playtest acceptance.

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
| [CJ-018](#cj-018-police-driving-and-reactions) | Police driving and reactions | High | Open |
| [CJ-023](#cj-023-vehicle-widths-and-street-geometry) | Vehicle widths and street geometry | High | Open |
| [CJ-014](#cj-014-city-art-and-layout) | City art and layout | Medium | Open |
| [CJ-015](#cj-015-street-lighting) | Street lighting | Medium | Open |
| [CJ-017](#cj-017-pedestrian-life) | Pedestrian life | Medium | Open |
| [CJ-019](#cj-019-performance-telemetry) | Performance telemetry | Medium | Open |
| [CJ-020](#cj-020-turning-kinematics-of-traffic) | Turning kinematics of traffic | Medium | Implemented, awaiting playtest |
| [CJ-024](#cj-024-recovery-cost-in-dense-traffic) | Recovery cost in dense traffic | Medium | Open |
| [CJ-005](#cj-005-data-driven-street-furniture) | Data-driven street furniture | Medium | Open |
| [CJ-006](#cj-006-collision-polish) | Collision polish | Low | Open |
| [CJ-007](#cj-007-large-vehicle-recovery) | Large-vehicle recovery after a crash | Low | Open |
| [CJ-008](#cj-008-test-autopilot-improvements) | Test autopilot improvements | Low | Open |
| [CJ-021](#cj-021-visible-towing) | Visible towing | Low | Open |
| [CJ-022](#cj-022-three-point-turns) | Three-point turns | Low | Open |

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

### CJ-018 Police driving and reactions

- **Priority:** High
- **Status:** Open

**Problem.** Police cars in pursuit drive at up to 150 km/h through crowds and on sidewalks and do not react to people at all; in the `brawl` scenario they caused most of the pedestrian deaths.

**Playtest feedback (2026-10-07).** During a pursuit the police are far too aggressive: they run down many pedestrians, drive their cars badly, ram other cars and cause crashes. They also do not react to a fight between drivers (left open by [CJ-016](#cj-016-road-rage-and-traffic-incidents)). The user wants police behaviour discussed again in a separate session before any change.

**Acceptance criteria.** To be agreed in that session. Starting points: police cars brake for and steer around people when it does not cost them the pursuit, mount the sidewalk only when necessary and slowly; in `chase` and `brawl`, pedestrians hit by police drop by at least 80 %; collisions between police and uninvolved traffic are measured and reduced; police respond to a fight between drivers.

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
- **Status:** Implemented on 2026-10-07, awaiting playtest; found by the rear-axle slip measurement on 2026-10-06; specification approved on 2026-10-07

**Problem.** Rail cars turn on a quadratic curve whose control point sits at the two lane offsets, so a right turn has a radius of about 2 m; real cars need about 5.5 m. Both axle samples stay on that curve, so the body follows the chord and the rear axle point slides sideways through the bend. `TRAFFIC slip` measured in `chase` and `day` on 2026-10-06: over 5° of rear-axle slip in 72–74 % of the turning time, up to 70–75°. Lane changes no longer slide (CJ-016); turns still do.

**Approved specification (2026-10-07).**

- **Pose rule.** In turns too, the rear axle follows the path and the body points along the path's tangent, so the front swings out, as in lane changes. Straight driving is unchanged.
- **Turn geometry per class.** Each class gets a kerb-to-kerb turning circle in `vehicles.cfg` (`TURN` records: cars about 11 m, the Bus 23 m); the tightest rear-axle radius follows from it. Turns are arcs with gradual steering in and out (clothoid transitions). A right turn uses the largest radius that keeps the inner kerb corner clear; a class that cannot turn that tightly (large vehicles) first moves towards the road centre and then takes the junction box alone. A left turn uses an arc of about 6 m; large vehicles start turning before the box.
- **Turning speed.** Limited so that the lateral acceleration at the rear axle stays within about 3.5 m/s² (`TURN lateral_accel` in `traffic.cfg`): about 14 km/h through a right turn and 17 km/h through a left turn, instead of about 29 km/h on a 2 m arc. A change to the feel for the playtest.
- **Out of scope.** U-turns in the middle of a block: a car cannot turn round in one move on an 8 m street. They take the new pose rule; a realistic three-point turn becomes a new backlog item.
- **Measurement.** An isolated `traffic-turns` fixture drives every traffic class right, left and straight through an empty junction at 60 Hz and 20 Hz and records slip, rear-axle radius, kerb intrusion, encroachment and lateral acceleration, with trace captures. A new city line counts rail cars overlapping each other.

**Acceptance criteria.**

- Every city scenario: rear-axle slip while turning at most 8°, at most 1 % of the turning time over 5°.
- Fixture, every class: slip at most 3°; rear-axle radius at least the class minimum; lateral acceleration within the limit; cars (all but `large`) at most 4 px (0.25 m) over the kerb and no encroachment into the oncoming half of a road; large vehicles at most 1.5 m of encroachment, only in right turns.
- No regression: straight and lane-change slip, the CJ-016 fixtures (recovery and clearance checks, 100/100 conflict cases, 80/80 incident cases), AI contacts, cars blocked over 3 s, wait cycles, rail overlaps, and the decision CPU targets.
- Before/after captures of turning traffic; playtested.

**Result (2026-10-07).** The rear axle traces the path in turns too, and every traffic class turns on its own [turn path](design/traffic.md#turn-paths). Turning fixture: 32 → 96 of 96 cases; slip 13–70° → at most 1.9°; a Taxi's right turn 1.8 → 3.6 m (class minimum 3.3 m) at 2.7 m/s² instead of 37 m/s². City: turning slip 70–75 % of the time over 5°, up to 76° → 0.0 %, at most 1.9° in all six scenarios; rail overlaps 1.65 → 0.33 pair-s. The CJ-016 fixtures pass. The user agreed these refinements during the work: the stop-line zone does not count as encroachment; wheels stay off the kerbs while large vehicles' overhangs may sweep a corner; over-wide cars take wide turns; turns that do not fit at all (Bus, Semi, the large trucks' right turns) are avoided; the `rampage` CPU stays above target as [CJ-024](#cj-024-recovery-cost-in-dense-traffic). Out of the work came [CJ-022](#cj-022-three-point-turns) and [CJ-023](#cj-023-vehicle-widths-and-street-geometry). Evidence: [turning results](design/traffic-turning-results.md).

Remaining: the playtest.

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

**Observed on 2026-10-06** (`drive`, 1,800 frames, build `a3da34f`): the run now stops in the first junction. At t = 1.65 s a traffic taxi (#44, normal driver) hits the Stinger at an approach speed of 221 px/s; 0.05 s later the Stinger hits two pieces of street furniture. From then on the car stays at 0 km/h until the end of the run (11 player stuck events, health 61/100). The taxi stays knocked for more than 12 s (`LONG-REJOIN reason=no_feasible_manoeuvre`), so neither car frees the other, and every screenshot after the first two seconds shows the same junction.

### CJ-021 Visible towing

- **Priority:** Low
- **Status:** Open

**Idea.** A car that cannot get free, such as one boxed in by buildings and other cars, stays in place and stays observable under [CJ-016](#cj-016-road-rage-and-traffic-incidents). A tow truck should come and remove it in view, as the approved [traffic proposal](design/traffic-behaviour-proposal.md) plans after the core. Nothing may vanish or be relocated invisibly in the meantime.

**Acceptance criteria.** To be agreed: the tow truck drives there on the road network, hooks up the car and tows it away; a test scenario logs the call, the arrival time and the removal; playtested.

### CJ-024 Recovery cost in dense traffic

- **Priority:** Medium
- **Status:** Open (found by CJ-020 on 2026-10-07)

**Problem.** Since CJ-020 traffic takes turns at a realistic 11–19 km/h, so traffic is denser round the junctions. In `rampage`, where the player knocks many cars off their lanes, about 7 cars recover at a time instead of 4.5, with more moving drivers to forecast round each, and the driver decision CPU (0.60 ms average, 1.17 ms 95th percentile on 2026-10-07) exceeds the CJ-016 targets of 0.5 and 1.0 ms. The recovery decisions themselves are unchanged; the other city scenarios stay within the targets. The user accepted this for CJ-020 on 2026-10-07.

**Acceptance criteria.** `rampage` within 0.5 ms average and 1.0 ms 95th percentile decision CPU, with the CJ-016 fixtures passing unchanged.

### CJ-022 Three-point turns

- **Priority:** Low
- **Status:** Open (found by CJ-020 on 2026-10-07)

**Problem.** A traffic car blocked for 6 s turns round in the middle of a block, and a dead end at the city edge forces a U-turn in the junction. On an 8 m street the widest one-move turn between the lanes has a 2 m radius, while a car's rear axle needs at least 3.3 m (a bus 7 m): since CJ-020 the rear axle follows that semicircle without sliding, but no real car could make the turn.

**Acceptance criteria.** To be agreed: a three-point turn (forward into the far lane, reverse, forward) within each class's turning radius, checked against vehicles and people like a pull-out; large vehicles keep avoiding U-turns; playtested.

### CJ-023 Vehicle widths and street geometry

- **Priority:** High
- **Status:** Open (found by CJ-020 on 2026-10-07)

**Problem.** Playtest feedback (2026-10-07): the streets feel cramped; cars and pedestrians seem too close together. The measurements of CJ-020 point to the vehicles and the junctions rather than the road width: lanes are 4 m (real ones 3–3.5 m) and the 4 m sidewalks keep pedestrians 1.4–2.6 m from the kerb, but a vehicle's width follows its sprite's aspect ratio, which makes most of them 30–40 % wider than the real vehicle (Taxi 2.46 m against about 1.8 m, Pickup 2.88 m, Ambulance 2.96 m, Bus 2.92 m against 2.55 m, Semi 3.63 m against 2.55 m), and junction corners are square, without the 3–6 m kerb radius of a real street. As a result the Pickup, Van, Limo and Ambulance need wide turns, and the Bus, the Semi and the right turns of the BoxTruck, FireTruck and Garbage truck do not fit at all, so traffic avoids them ([turning results](design/traffic-turning-results.md)).

**Acceptance criteria.** To be agreed: real-world widths for every class (sprite art drawn or cropped to them), rounded kerb corners at junctions (geometry, art and pedestrian waiting spots), possibly wider streets; the turning fixture shows clean turns for cars and fitting turns for large vehicles; collisions and the CJ-002 baseline re-measured; before/after screenshots of street scenes; playtested.

## Done

| ID | Title | Completed |
|---|---|---|
| CJ-001 | Documentation overhaul: documentation structure, design documents, guides, testing guide, ADRs, changelog, contributing guide | 2026-09-27 |
| — | Collision physics rewrite: vehicles no longer stick to obstacles or jitter; breakaway street furniture; traffic recovery after crashes ([ADR-0005](adr/0005-vehicle-contact-solver.md)) | 2026-09-27 |
| CJ-009 | Licence for the code: MIT licence for the project's own code (`LICENSE`); third-party assets keep the licences in [CREDITS](../CREDITS.md) | 2026-10-06 |
| CJ-016 | Human-like traffic and incidents: physical recovery without timeouts, cooperative yielding, wait-for cycles, persistent drivers and road-rage incidents ([details](#cj-016-road-rage-and-traffic-incidents)) | 2026-10-07 |

### CJ-016 Road rage and traffic incidents

- **Priority:** High
- **Status:** Done on 2026-10-07; specification approved on 2026-10-05; three increments (2026-10-05, 2026-10-06); playtested and accepted on 2026-10-07

**User direction (2026-10-03).** Drivers should behave like people: follow, yield, avoid, reverse, honk, sometimes misjudge and collide, and occasionally get out to confront or fight another driver. A blocked situation must not be solved by deleting a driver, forcing them to walk away after a timer, or relocating an involved vehicle. This is the current priority; CJ-002 supplies the necessary vehicle capabilities.

**Pre-change problem.** `UpdateKnocked` gave up after 12 s or below 35 % vehicle health; it spawned an unrelated pedestrian on-screen or relocated the vehicle off-screen. Recovery alternated controls towards one target without validating a full escape path. Pedestrian fighting still targets the player, so a driver-versus-driver incident also needs persistent ownership and explicit actor targeting.

**Approved specification.** [Human-like traffic and incident proposal](design/traffic-behaviour-proposal.md): predicted occupancy, feasible swept-footprint manoeuvres, stable yielding roles, impossible-blockage handling, persistent drivers, cause-based incidents, reusable spatial queries and measured CPU budgets. [ADR-0008](adr/0008-human-like-traffic.md) is Proposed; the accepted rail decision remains in force until a tested replacement addresses its original jitter/deadlock concerns.

**First increment (2026-10-05).** Frozen `traffic-recovery` fixtures measure Taxi, Bus and BoxTruck in enclosed, free and reverse-escape geometry at 60 Hz and 20 Hz: 52 checks, 14,400 rendered frames and 240 s per class. The complete baseline had 14 failed checks per class; the corrected build passed all 156 recovery checks. A separate 26-check clearance regression reproduced four pre-fix failures caused by separated nearby boxes being falsely classified as initial contact, then passed every check after the fix. The [result report](design/traffic-recovery-results.md) preserves exact manifests/hashes, timings and failed intermediate attempts. Corrected garage recovery took 10.833/10.600 s for Taxi, 20.967/21.100 s for Bus and 13.767/13.900 s for BoxTruck at 60/20 Hz; enclosed cars stayed with their drivers for 60 s with no movement, overlap, blend or ownership loss. All six matching city runs completed and recorded zero recovery give-ups; crash/chase each recorded one rejoin and rampage recorded eleven. The crash/rampage CPU failures remain open in the report. The controller uses a shared snapshot and at most 20 physical forward/reverse/hold candidates, with safe progressing lane feedback skipping escape search. Path scanning no longer ignores a mutually blocking vehicle; ordinary rail passing and junction right-of-way rules remain. Full-population CPU acceptance, city-edge/moving/multi-seed fixtures and user playtest remain pending. Cooperative conflict roles/reservations, persistent on-foot ownership and fights remain to be implemented; CJ-002 remains an explicit handling dependency.

**Second increment (2026-10-06).** Recovery planning is sliced across frames under a shared deterministic step budget; holds replan only when a blocker moves; followers behind a car keep their own distance (the rear-end rule) and rail cars are forecast no further than their planned stop; immediate checks validate a few extra frames and are reused while everything moves as forecast. The worst city decision frame fell from 11.6 ms to 2.5 ms; `crash` meets both decision targets (0.434 ms average, 0.933 ms p95), while `chase` (0.739 / 1.455 ms) and `rampage` (0.799 / 1.617 ms) still exceed them. Cooperative yielding gives two drivers stopped behind each other stable roles; the yielder retraces its own path on rails and tucks back into its lane, with a chain behind it. The new `traffic-conflict` fixture (60 cases) went from 6 to 60 resolved cases. Driver moods (10 % aggressive) and collision incidents let an aggressive driver stop, get out on a safe side, confront the other driver or the player, fight, and drive the same car on; interruptions end with a logged reason. The new `traffic-incident` fixture (80 cases) went from 20 to 80 accepted cases with no ownership violation or duplicate driver. Queue-aware rejoining, escape from an existing contact and a fixed U-turn route loss came out of the new fixtures. The frozen recovery and clearance fixtures still pass all 182 checks with unchanged rejoin times. Evidence: [yielding and incident report](design/traffic-yielding-incident-results.md).

**Third increment (2026-10-06), CPU.** Every city scenario now meets the decision CPU targets: `chase` 0.689 / 1.415 → 0.321 / 0.819 ms, `rampage` 0.727 / 1.547 → 0.378 / 0.861 ms, `crash` 0.390 / 0.865 → 0.234 / 0.484 ms (average / 95th percentile). The rail look-ahead tests only what can reach its sampled path, recovery rollouts let distant actors sleep and skip rail forecast boxes out of reach, and force steps share their trigonometry; these leave every decision unchanged, and all four fixtures reproduce the second increment line by line. Covered immediate checks now record up to 32 actors instead of 12, which changes city decisions. On-foot test runs no longer depend on the mouse cursor. Evidence: [third increment report](design/traffic-third-increment-results.md).

**Third increment (2026-10-06), wait-for cycles.** Two knocked cars blocking each other and loops of three or more drivers (junction gridlock) are found once per frame; one driver gives way to the driver waiting on it, a rail car by backing up, a knocked car by short checked creeps that make room. A car held at a stop line waits on the car in the box. The conflict fixture (v2, 100 cases) adds `knocked-pair` and `junction-gridlock`: 0 → 20 of 20 each, with the 60 earlier cases unchanged. The city runs had no loop lasting over 10 s; the "unresolved pair" at the end of `crash` is one that formed 0.3 s before the end.

**Third increment (2026-10-06), presentation.** Arguing drivers shout (three synthesised voices at a personal pitch, every second or so) and shake a raised fist (two new civilian atlas frames); the punch frame is kept for blows. Rail cars change lane by steering: the lateral offset follows an S-curve along the path, the rear axle traces it and the body points along it; a car standing too close behind an obstacle backs up before pulling out, and a yielding car reverses back into its lane the same way. Incident fixture v2: 40 → 80 of 80 (shouts 0 → 4–10 per case, raised fist 0 → 1.5–4.4 s, punch frame while arguing face to face 0.5–1.5 → 0 s); conflict fixture: lane-change slip 90° → 2.1°, 80 → 100 of 100. The slip measurement also found turning traffic sliding (CJ-020).

**Playtest (2026-10-07).** The user tested queues, two-car conflicts, heavy-vehicle reversing, a full confrontation and lane changes, and accepted them. Moved on: the police reaction to a fight to [CJ-018](#cj-018-police-driving-and-reactions), visible towing to [CJ-021](#cj-021-visible-towing). [ADR-0008](adr/0008-human-like-traffic.md) stays Proposed until visible drivers control the physical integrator.

**Acceptance criteria.** Follow the approved proposal's scenario table. Feasible blockages resolve physically; impossible ones remain stable and observable; timeout-based disappearance/relocation is zero; incident participants retain identity and car ownership; exiting, approaching, fighting and returning are visible actions with interruption rules. Validate with fixed seeds, 60 Hz/20 Hz scenarios, class-specific recovery tests, regression runs, separate traffic CPU timings and a user playtest. First deliver reliable manoeuvring, then incidents and combat; visible towing follows the accepted core.
