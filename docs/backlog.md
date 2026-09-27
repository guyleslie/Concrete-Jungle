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

## Summary

| ID | Title | Priority | Status |
|---|---|---|---|
| [CJ-002](#cj-002-vehicle-handling-model) | Vehicle handling model | High | Open |
| [CJ-003](#cj-003-vehicle-damage-model) | Vehicle damage model | High | Open |
| [CJ-004](#cj-004-replace-placeholder-art) | Replace placeholder art | Medium | Open |
| [CJ-005](#cj-005-data-driven-street-furniture) | Data-driven street furniture | Medium | Open |
| [CJ-006](#cj-006-collision-polish) | Collision polish | Low | Open |
| [CJ-007](#cj-007-large-vehicle-recovery) | Large-vehicle recovery after a crash | Low | Open |
| [CJ-008](#cj-008-test-autopilot-improvements) | Test autopilot improvements | Low | Open |
| [CJ-009](#cj-009-licence-for-the-code) | Licence for the code | Low | Open |

## Open items

### CJ-002 Vehicle handling model

- **Priority:** High
- **Status:** Open

**Problem.** Playtesting shows the vehicles are not controllable enough; the handling maths is not right.

**Current state.** An arcade model (`VehicleForces` in `src/vehicle.cpp`, described in [Vehicles › Handling model](design/vehicles.md#handling-model)): steering sets a target yaw rate, and sideways velocity decays exponentially. The class values in `vehicles.cfg` are 4–9 times real-world figures (for example 90 m/s² braking).

**Proposed approach.** A bicycle model: slip angles at the front and rear axle, a tyre force curve that saturates, load transfer under braking and acceleration. All parameters in `vehicles.cfg`. Keep the physics integration contract: the model only changes velocities.

**Acceptance criteria.**

- A written target for the feel, agreed before implementation, with reference numbers per vehicle type (braking distance from 100 km/h, 0–100 km/h time, maximum cornering acceleration, handbrake-turn behaviour).
- A handling test scenario that logs those numbers; every shipped class meets its targets.
- The `crash` and `derby` metrics are no worse than the [baseline](testing.md#baseline).
- Playtested and accepted.
- [Vehicles](design/vehicles.md) updated; a new ADR records the model change.

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

### CJ-004 Replace placeholder art

- **Priority:** Medium
- **Status:** Open

**Problem.** Motorbikes and civilian pedestrians use procedural placeholder sprites that do not match the quality of the rest of the art.

**Notes.** No free, high-resolution top-down motorbike or civilian art has been found yet. itch.io listings show a bot check to automated browsers, so they must be searched manually. Any new art must meet the [art requirements](guides/adding-content.md#art-and-licence-requirements).

**Acceptance criteria.** Motorbikes (sport, chopper, scooter, with and without rider) and civilians replaced by art of consistent quality, added through the data files, credited in [CREDITS.md](../CREDITS.md).

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
- dumpsters are rigid; they could be pushable bodies.

### CJ-007 Large-vehicle recovery

- **Priority:** Low
- **Status:** Open

**Problem.** A 12 m bus knocked off its lane often cannot manoeuvre back and gives up after 12 s.

**Acceptance criteria.** Buses and trucks recover to their lane in most knock-offs in the `chase` and `derby` scenarios, without teleporting on-screen.

### CJ-008 Test autopilot improvements

- **Priority:** Low
- **Status:** Open

The `drive` autopilot never reverses, so it stays stuck once it drives into something. It should back out like the `derby` autopilot. Handling scenarios for [CJ-002](#cj-002-vehicle-handling-model) will need scripted manoeuvres as well.

### CJ-009 Licence for the code

- **Priority:** Low
- **Status:** Open

The project's own code has no licence yet. Choose one (and add a `LICENSE` file) before the code is shared. Third-party asset licences are already listed in [CREDITS.md](../CREDITS.md).

## Done

| ID | Title | Completed |
|---|---|---|
| CJ-001 | Documentation overhaul: documentation structure, design documents, guides, testing guide, ADRs, changelog, contributing guide | 2026-09-27 |
| — | Collision physics rewrite: vehicles no longer stick to obstacles or jitter; breakaway street furniture; traffic recovery after crashes ([ADR-0005](adr/0005-vehicle-contact-solver.md)) | 2026-09-27 |
