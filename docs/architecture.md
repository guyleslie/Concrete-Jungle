# Architecture

This document describes how Concrete Jungle is put together: the modules, what happens each frame, and the conventions every module shares. Subsystem details live in the [design documents](README.md#design-documents).

## Contents

- [Overview](#overview)
- [Module map](#module-map)
- [Start-up](#start-up)
- [Frame lifecycle](#frame-lifecycle)
- [Vehicle update pipeline](#vehicle-update-pipeline)
- [Rendering pipeline](#rendering-pipeline)
- [Units and conventions](#units-and-conventions)
- [Global state and determinism](#global-state-and-determinism)

## Overview

The game logic runs on a 2D ground plane. Everything that exists in the world — vehicles, pedestrians, buildings, street furniture — has a position on that plane, and optionally a height used only for drawing. The renderer lifts this plane into real 3D and looks straight down with a perspective camera, which produces the GTA 2 look (see [ADR-0001](adr/0001-real-3d-top-down-renderer.md)).

The `Game` class owns the world and the rules. Subsystems are plain modules with free functions or small classes; they receive the `Game` or the specific objects they need as parameters.

## Module map

| Module | Files | Responsibility |
|---|---|---|
| Entry point | `main.cpp` | Window, main loop, main-thread startup coordination, `--shot` test mode |
| Startup work | `startup_loading.*`, `startup_plan.*`, `startup_input.h` | Frozen weighted task/child accounting, metadata and optional duration-history estimates, title-input handoff |
| Loading presentation | `loading_screen.*`, `assets/data/loading.cfg` | Independent cosmetic resources and responsive four-row startup status; drawing consumes work snapshots |
| Game | `game.h`, `game.cpp` | World ownership, rules, player, weapons, wanted level, police spawning, missions, pickups, crash consequences, test autopilots |
| HUD | `hud.cpp` | Screen-space HUD, title screen, pause menu |
| Configuration | `config.h` | Global constants: scale, city size, population, camera, time of day |
| Assets | `assets.*`, `datafile.*` | Loading everything described by `assets/data/*.cfg`, with built-in defaults and procedural fallbacks |
| Procedural art | `sprite_gen.*` | Signed-distance-field painter for sprites no asset pack covers |
| City | `city_map.*` | City generation, 3D structures, street furniture, spatial queries, traffic signals, minimap |
| Vehicles | `vehicle.*`, `vehicle_types.*` | Vehicle classes, engine / brake / tyre model, vehicle drawing |
| Measurements | `vehicle_tests.*`, `traffic_tests.*`, `traffic_clearance_tests.*`, `traffic_conflict_tests.*`, `traffic_incident_tests.*`, `traffic_turn_tests.*` | Isolated CJ-002 handling/collision, frozen CJ-016 recovery scenes, the nearby-box rejoin API regression, cooperative yielding and driver incident scenes, the CJ-020 turning scenes, enabled only by screenshot scenarios |
| Physics | `physics.*` | Vehicle collision detection and the contact solver |
| Traffic | `traffic.*`, `traffic_turns.*`, `traffic_recovery.*`, `traffic_incidents.*` | Rail traffic, cooperative yielding and police AI; each class's turn paths through a junction; shared observations and budgeted physical recovery for knocked traffic; driver moods and collision incidents |
| Pedestrians | `pedestrian.*` | Pedestrian AI, steering, the pedestrian grid and drawing |
| Rendering | `render.*`, `lighting.*`, `particles.*` | Camera, render passes, day/night cycle, particles, decals |
| Audio | `audio.*` | Procedural sound synthesis, positional playback |
| Utilities | `math_utils.h` | Vectors, angles, oriented boxes, intersection tests, random numbers |

Dependencies point downwards: `game` uses every other module; `traffic`, `physics` and `pedestrian` read the `Game`; `vehicle`, `city_map` and `render` do not know about `Game`.

## Start-up

1. `main` opens the window and presents primitive startup status, then loads the loading screen's independent optional art/fonts. `startup_plan` discovers estimates from current metadata and optional local duration history before `startup_loading::Reporter` freezes the registry. Cosmetics come from `assets/data/loading.cfg`; counts, dependencies and completion come from the execution owners.
2. `gAssets.Load(&reporter)` prepares materials, vehicles, civilian atlases, character animations, foliage, props/effects, interface fonts and shaders in the existing order. Missing data files use built-in defaults. Missing image records follow each loader's established skip/substitute policy; an empty group may generate its existing procedural fallback. Required committed resources are validated.
3. `RailPlanTurns(&reporter)` warms the traffic-turn cache before `Game::Init(&reporter)` generates the fixed-seed city, builds the minimap, initializes renderer targets and audio, and calls `NewGame`. Each group reports safe completion checkpoints using its current content/configuration totals. Audio may legitimately resolve as skipped when the device is unavailable.
4. `NewGame` places parked cars on parking spots, spawns moving traffic and pedestrians, places the player and starter car on the central square, and sets up pickups and mission phones. A final readiness gate validates the usable world and required resources. Only success enables 100% and automatic title handoff; failure/cancellation unwind acquired resources while graphics/audio contexts remain alive.
5. The normal title update/draw begins after readiness, with the city simulating in the background. The loading overlay may fade for 250 ms without delaying fresh title input. Keys held during loading rearm individually on release. Screenshot scenarios bypass the wall-clock fade and start their frame counter after initialization.

Overall progress is confirmed weighted completion, never elapsed time. Top-level budgets stay frozen for the run; local child plans freeze at zero parent progress and share its budget. `Pulse` grants no progress. Optional `build/cache/startup-work-profile.cfg` history scales later budget estimates from measured unit costs; it never skips loading or proves readiness. The presenter services events and draws on the main thread outside unfinished texture/3D/shader passes. Indivisible driver/library calls are timed separately from cooperative checkpoints.

The [CJ-030 specification](design/loading-screen-proposal.md) defines the accounting, visual and handoff contract; the [loading measurement record](testing/cj030-loading.md) records the frozen baseline, passing implementation checks, measured costs, user acceptance and hardware measurement limits.

## Frame lifecycle

`main` measures the frame time, clamps it to 1/20 s (1/60 s fixed in test mode) and calls `Game::Update`, then `Game::Draw`.

`Game::Update` dispatches on the game state (`Title`, `Playing`, `Paused`, `Wasted`, `Busted`). During play, `Game::UpdatePlaying` runs the systems in this order:

| # | Step | Function |
|---|---|---|
| 1 | Advance the time of day (hold T to fast-forward) | `DayNight::Update` |
| 2 | Signals, metro train, fountain and hydrant water | `CityMap::Update` |
| 3 | Player input (on foot or driving) | `UpdatePlayerOnFoot` / `UpdatePlayerDriving` |
| 4 | Vehicles: AI, physics, crash consequences, effects | `UpdateVehicles` (see below) |
| 5 | Driver incidents: stopping, getting out and back in | `IncidentsUpdate` |
| 6 | Pedestrians (the pedestrian grid is rebuilt first) | `UpdatePeds` |
| 7 | Police spawning and arrests | `UpdatePolice` |
| 8 | Recycling far-away traffic and pedestrians | `UpdateSpawning` |
| 9 | Missions and pickups | `UpdateMission`, `UpdatePickups` |
| 10 | Particles | `Particles::Update` |
| 11 | Wanted-level cool-down, camera, audio | inline in `UpdatePlaying` |

After `Wasted` or `Busted`, the world keeps running in slow motion (35 % speed) for four seconds before the player respawns.

## Vehicle update pipeline

`Game::UpdateVehicles` is the heart of the simulation. It keeps decision making, physics and consequences strictly separate:

1. **Prepare and decide.** Advance vehicle fire, explosions and wreck cleanup before any driver decisions. Then rebuild the pedestrian grid, record frame-start poses and capture one common traffic observation snapshot. This includes any actors or velocity changes produced by an explosion, so later drivers cannot use observations captured before that event. Traffic and police AI then run. Traffic that is on its lane (a *rail car*) computes where it will be at the end of the frame; recovering traffic, police cars and the player only set controls. Recovery first checks safe, progressing lane feedback; otherwise it evaluates at most 20 candidate trajectories using the production forces, swept footprints and a stopping tail, sliced across frames under a shared step budget. It retains safe committed controls or holds. Rail cars detect mutual waits and take a stable yielding role; before any driver decides, `AIResolveWaitCycles` gives a role in loops the pair rule does not cover (two knocked cars, three or more drivers).
2. **Simulate.** `VehiclePhysics::Step` applies engine, brake and tyre forces, detects and solves all contacts in sub-steps, and moves every vehicle. Rail cars move kinematically along their path. The step produces a list of `ImpactEvent`s but applies no game rules.
3. **Consequences.** `Game::HandleImpacts` turns impact events into damage, driver injury, motorbike rider ejection, sparks, sounds, camera shake and driver reactions, including a possible incident between the two drivers (`IncidentOnImpact`). `VehiclePedCollisions` handles vehicles hitting people and driving over people on the ground.
4. **Effects.** Skid marks, tyre smoke, dust, engine sound state and damage smoke are updated from the final velocities.

See [Physics](design/physics.md), [Vehicles](design/vehicles.md) and [Traffic](design/traffic.md) for the details.

`DRIVER DECISION CPU` measures vehicle preparation, pedestrian grid construction, the shared snapshot and all traffic/police decisions, including fire, explosions and wreck cleanup; `DRIVER DECISION STAGES` splits it into those stages. This conservative span is an upper bound for traffic decisions rather than the whole vehicle update; physics, pedestrian AI and rendering have separate spans. Recovery forecasts match the actual force substeps in the 60 Hz/20 Hz fixtures (240 Hz/160 Hz), with a 240 Hz forecast cap at faster frame rates to bound work.

The observation and forecast cache stores each actor's radius, centre speed and initial oriented box. Before constructing a detailed moving-actor forecast, conservative travel/rotation bounds can rule out contact with the candidate sweep. Rail observations include any legacy pose blend; the actual observed body is used at time zero, and blends bypass the path-only bound. Oriented-box radius bounds account for both inflated half-extents (`sqrt(2)` times the margin). Planning can omit a hold rollout only when it cannot change the control outcome; moving-candidate order and hold-first score ties remain unchanged. The controller and configuration are unchanged by these implementation optimizations. The complete corrected recovery and city measurement series is retained in the [result report](design/traffic-recovery-results.md); city CPU acceptance remains open.

Initial contact depth is stored only when the geometry query returns true: `OBBOverlap` can leave a positive output after returning false on a separating axis. A separate clearance fixture reproduces the pre-fix false rejoin veto from separated nearby boxes without changing the frozen recovery scene. The rejoin API may write `RejoinCause` diagnostics but must not move the actual body or change its driver; terminal city output distinguishes actual initial overlap, clearance-only contact, unsafe sweep and an incomplete stopping tail.

## Rendering pipeline

`Game::DrawWorld` renders the world in five passes into off-screen targets that share one depth buffer — shadows, scene albedo, additive light, emissive, bloom — and composites them to the screen. `Game::Draw` then draws the HUD or the menus on top. See [Rendering](design/rendering.md).

## Units and conventions

| Quantity | Unit / convention |
|---|---|
| Length | World pixels: **16 px = 1 m** (`cfg::PX_PER_METER`) — see [ADR-0003](adr/0003-world-scale.md) |
| Speed | px/s; 16 px/s = 1 m/s = 3.6 km/h |
| Acceleration | px/s² |
| Mass | tonnes (vehicle classes), 1.0 = a typical car |
| Impulse | tonnes × px/s |
| Angle | radians; 0 = facing up (−Y, screen north), increasing clockwise |
| Direction helpers | `Forward(a) = (sin a, −cos a)`, `RightOf(a) = (cos a, sin a)`, `AngleOf(v)` is the inverse |
| Height | world pixels above the ground, used only for drawing (the renderer maps ground point *(x, y)* at height *h* to 3D *(x, h, y)*) |
| City grid | a tile is 64 px (4 m); a block is 16 tiles (64 m) including its streets |
| Time of day | hours 0–24; a full day lasts 4.8 minutes |

Oriented boxes (`OBB`) have axis 0 = right and axis 1 = forward, matching how sprites are drawn, so a vehicle's collision box always lines up with its sprite.

## Global state and determinism

- `gAssets` holds every loaded asset and is read-only after start-up.
- `GRng()` is the single gameplay random number generator (xorshift, fixed seed).
- The city is generated from a fixed seed, so every run has the same map.
- In test mode (`--shot`) the frame time is fixed at 1/60 s, which makes runs comparable between code versions.
- Isolated `traffic-recovery` cases retain the 1/60 s render clock while advancing AI and physics at either 1/60 s or 1/20 s. Their fixed seed, geometry and ownership checks are described in [Testing](testing.md#cj-016-recovery-measurements).
- Recovery decisions read frame-start actor observations rather than another driver's already-updated pose. Recovery stays on one thread; its work is bounded per frame by a deterministic step budget, not by wall-clock time, so runs remain reproducible.
- `VehicleForces` and the recovery forecast reuse a sine/cosine pair and the per-sub-step exponential factors through small caches keyed on the exact input value, so the cached results are bit-identical to recomputing them.
- Vehicles and pedestrians carry a `serial` that changes whenever a slot is reused. Cross-references between a car and its driver on foot, a yielder and its priority car, and a fighter and their opponent store the index together with the serial and are checked before use.
- Startup reporting never consumes gameplay RNG or advances simulation. Traffic-turn warmup preserves every eligible sprite visit and `InitVehicle` call, including cache hits, because those calls consume RNG and advance vehicle serials. No extra `Game::Update` or `CameraRig::Update` is used to prepare a loading transition.
