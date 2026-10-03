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

Recommended order, based on the playtest feedback of 2026-09-27:

1. **Playtest CJ-010** (pedestrian behaviour) and CJ-013 (full screen) and close them, or note what still feels wrong.
2. **[CJ-002](#cj-002-vehicle-handling-model) Vehicle handling model** — the most frequent complaint: the vehicles need a realistic physics model with clearly different handling per vehicle type.
3. **[CJ-003](#cj-003-vehicle-damage-model) Vehicle damage model**, which builds on the same physics.
4. **[CJ-012](#cj-012-audio-overhaul) Audio overhaul** — positional sound, sirens, horns and effects.
5. **[CJ-004](#cj-004-replace-placeholder-art) and [CJ-011](#cj-011-relaxed-player-posture) Character art** — one art session: civilians, motorbikes, and a relaxed player.

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
| [CJ-016](#cj-016-road-rage-and-traffic-incidents) | Road rage and traffic incidents | Medium | Open |
| [CJ-017](#cj-017-pedestrian-life) | Pedestrian life | Medium | Open |
| [CJ-019](#cj-019-performance-telemetry) | Performance telemetry | Medium | Open |
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

- **Priority:** Medium
- **Status:** Open

**Idea.** Traffic that gets stuck or bumped now and then turns aggressive, as in real cities: a driver gets out, drags the other driver out of their car or beats them up, then gets back in and drives on — or leaves the car where it is. Abandoned cars and wrecks are towed away later by a tow truck (or cleared by the fire brigade).

**Acceptance criteria.** A written rule set, agreed before implementation (what triggers an incident, how often, what each side does, when police react); incidents reuse the pedestrian fight behaviour ([Pedestrians › Fighting back](design/pedestrians.md#fighting-back)); a test scenario that provokes incidents and logs their outcomes; no traffic deadlocks caused by abandoned cars.

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

**Problem.** A 12 m bus knocked off its lane often cannot manoeuvre back and gives up after 12 s.

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
