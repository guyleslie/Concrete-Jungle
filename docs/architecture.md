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
| Entry point | `main.cpp` | Window, main loop, `--shot` test mode |
| Game | `game.h`, `game.cpp` | World ownership, rules, player, weapons, wanted level, police spawning, missions, pickups, crash consequences, test autopilots |
| HUD | `hud.cpp` | Screen-space HUD, title screen, pause menu |
| Configuration | `config.h` | Global constants: scale, city size, population, camera, time of day |
| Assets | `assets.*`, `datafile.*` | Loading everything described by `assets/data/*.cfg`, with built-in defaults and procedural fallbacks |
| Procedural art | `sprite_gen.*` | Signed-distance-field painter for sprites no asset pack covers |
| City | `city_map.*` | City generation, 3D structures, street furniture, spatial queries, traffic signals, minimap |
| Vehicles | `vehicle.*`, `vehicle_types.*` | Vehicle classes, engine / brake / tyre model, vehicle drawing |
| Physics | `physics.*` | Vehicle collision detection and the contact solver |
| Traffic | `traffic.*` | Traffic and police driving AI |
| Pedestrians | `pedestrian.*` | Pedestrian AI and drawing |
| Rendering | `render.*`, `lighting.*`, `particles.*` | Camera, render passes, day/night cycle, particles, decals |
| Audio | `audio.*` | Procedural sound synthesis, positional playback |
| Utilities | `math_utils.h` | Vectors, angles, oriented boxes, intersection tests, random numbers |

Dependencies point downwards: `game` uses every other module; `traffic`, `physics` and `pedestrian` read the `Game`; `vehicle`, `city_map` and `render` do not know about `Game`.

## Start-up

1. `main` opens the window and loads all assets through `gAssets.Load()`. Each loader reads its `.cfg` file, falls back to a built-in default when the file is missing, and substitutes a procedural image for every missing picture.
2. `Game::Init` generates the city from a fixed seed, builds the minimap, initialises the renderer and audio, and calls `NewGame`.
3. `NewGame` places parked cars on parking spots, spawns moving traffic and pedestrians, places the player and a starter car on the central square, and sets up pickups and mission phones.
4. The game starts on the title screen, where the world already simulates in the background.

## Frame lifecycle

`main` measures the frame time, clamps it to 1/20 s (1/60 s fixed in test mode) and calls `Game::Update`, then `Game::Draw`.

`Game::Update` dispatches on the game state (`Title`, `Playing`, `Paused`, `Wasted`, `Busted`). During play, `Game::UpdatePlaying` runs the systems in this order:

| # | Step | Function |
|---|---|---|
| 1 | Advance the time of day (hold T to fast-forward) | `DayNight::Update` |
| 2 | Signals, metro train, fountain and hydrant water | `CityMap::Update` |
| 3 | Player input (on foot or driving) | `UpdatePlayerOnFoot` / `UpdatePlayerDriving` |
| 4 | Vehicles: AI, physics, crash consequences, effects | `UpdateVehicles` (see below) |
| 5 | Pedestrians | `UpdatePeds` |
| 6 | Police spawning and arrests | `UpdatePolice` |
| 7 | Recycling far-away traffic and pedestrians | `UpdateSpawning` |
| 8 | Missions and pickups | `UpdateMission`, `UpdatePickups` |
| 9 | Particles | `Particles::Update` |
| 10 | Wanted-level cool-down, camera, audio | inline in `UpdatePlaying` |

After `Wasted` or `Busted`, the world keeps running in slow motion (35 % speed) for four seconds before the player respawns.

## Vehicle update pipeline

`Game::UpdateVehicles` is the heart of the simulation. It keeps decision making, physics and consequences strictly separate:

1. **Decide.** Every vehicle records its pose at the start of the frame. Traffic and police AI then run. Traffic that is on its lane (a *rail car*) computes where it will be at the end of the frame; police cars and the player only set their controls. Burning vehicles count down to their explosion.
2. **Simulate.** `VehiclePhysics::Step` applies engine, brake and tyre forces, detects and solves all contacts in sub-steps, and moves every vehicle. Rail cars move kinematically along their path. The step produces a list of `ImpactEvent`s but applies no game rules.
3. **Consequences.** `Game::HandleImpacts` turns impact events into damage, driver injury, motorbike rider ejection, sparks, sounds, camera shake and driver reactions. `VehiclePedCollisions` handles vehicles hitting people.
4. **Effects.** Skid marks, tyre smoke, dust, engine sound state and damage smoke are updated from the final velocities.

See [Physics](design/physics.md), [Vehicles](design/vehicles.md) and [Traffic](design/traffic.md) for the details.

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
