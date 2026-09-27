# Concrete Jungle

A GTA1/GTA2-style top-down open-city action game in C++17 with raylib 6.0. It keeps the classic overhead view but uses modern rendering.

## What's in it

**Renderer: GTA2-style "fake top-down 3D"**
- The city is real 3D geometry seen through a perspective camera looking straight down.
- Buildings lean away from the screen centre. Bridges, the elevated metro and gate buildings hide what's under them.
- Cars, people, trees and props are flat sprites placed at their real height.
- The camera rises with speed and drops close when you're on foot.

**Lighting and effects**
- Day/night cycle with sun shadows cast by buildings, trees, cars and people.
- Headlights, street lamps, traffic lights, lit windows, neon, sirens, muzzle flashes and explosions.
- Bloom, tone mapping and vignette.
- Particles: tyre smoke, skid marks, fire, debris, sparks, blood and water jets.

**City**
- About 580 m × 580 m, procedurally generated (fixed seed).
- 16 px = 1 m throughout.
- Contents:
  - streets with traffic lights and zebra crossings
  - buildings with rooftop gear
  - parks, plazas with fountains and café terraces, parking lots
  - police station and hospital
  - gate buildings you drive under, skybridges
  - an elevated metro loop with a running train
  - the sea around the island

**Traffic**
- Undisturbed cars run "on rails" along smooth lane paths, so they never jitter.
- Long vehicles swing wide through turns.
- At junctions, cars obey the lights, don't block the box, and yield on left turns.
- They overtake stopped vehicles, mount the kerb, make U-turns and honk.
- Distracted drivers occasionally cause accidents.
- A hit (or pushing on it) knocks a car off its rail into physics. The driver brakes, then re-joins the lane once the spot is free, drives or reverses back towards it, or abandons the car if it is badly damaged.

**Crashes & physics**
- Rigid-body contact solver in the style of Box2D v3: sub-steps (240 Hz), real contact points, Coulomb friction, soft push-out instead of teleporting, speculative contacts so fast cars don't sink into walls or tunnel through poles.
- Restitution depends on impact speed: bumpers spring back in a parking knock, crumple zones absorb a real crash.
- Damage follows the delta-V of the impact, so mass matters: a bus barely notices a hatchback. Very hard crashes hurt the driver; motorbike riders are thrown off.
- Street furniture has a breakaway strength: lamp and signal posts fall over, hydrants burst, bins and cones fly; steel bollards, concrete planters, tree trunks and buildings stop you. Shrubs slow you down and get flattened.
- Spun or shoved cars slide on saturated tyre friction and then stand still; grinding along a wall throws sparks.

**Pedestrians**
- They walk the sidewalks, wait for the green man, use the crossings and wander into parks.
- They avoid obstacles and each other, flee from danger, and can be knocked down.

**Player**
- On foot: Survivor sprite. The legs follow your movement direction and the torso follows your aim (walk, run and strafe animations).
- Weapons: fists, knife, pistol, shotgun, rifle, flashlight.
- You can enter or hijack any vehicle.

**Game systems**
- Money and a wanted level (6 stars).
- Police chases, arrests (Busted) and deaths (Wasted), with respawn at the police station or hospital.
- Pickups.
- Missions from ringing pay phones: courier, demolition, steal-and-deliver.

**Audio**
- Fully synthesised: engine, siren, skids, guns, crashes, explosions, horn and more.

## Building

Requirements: raylib 6.0 (the Windows installer ships prebuilt `libraylib.a` and the w64devkit GCC 15 toolchain).

### Windows, one click (w64devkit GCC)
```bash
build.bat
```
Set `RAYLIB_DIR` if raylib isn't in `E:\Apps\raylib` (the installer default is `C:\raylib`).

### Git-Bash (incremental)
```bash
sh build.sh
```

### CMake (use Ninja; MinGW Make breaks on non-ASCII project paths)
```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DRAYLIB_DIR=E:/Apps/raylib/raylib
```
```bash
cmake --build build
```
Add `-DRAYLIB_FETCH=ON` to download and build raylib 6.0 automatically (Linux and macOS too).

### Manual GCC / Clang
```bash
g++ -std=c++17 -O2 -Isrc -I<raylib>/src src/*.cpp -o ConcreteJungle -L<raylib>/src -lraylib -lopengl32 -lgdi32 -lwinmm
```
On Linux, replace the Windows libraries with `-lGL -lm -lpthread -ldl -lrt -lX11`.

Run the game from the project folder so it finds `assets/`. It also falls back to the exe's folder.

## Controls

| On foot | |
|---|---|
| WASD / arrows | move (the character turns to face where it walks) |
| Shift | run |
| Hold right mouse button | aim at the cursor (strafe / backpedal) |
| Left mouse button | attack / shoot (also turns you towards the cursor) |
| R | reload |
| 1–6, Q, mouse wheel | switch weapon |
| E / F / Enter | enter or hijack a vehicle |

| Driving | |
|---|---|
| W / S | throttle, brake / reverse |
| A / D | steer |
| Space | handbrake (drift) |
| E / F / Enter | get out |
| H | horn |
| L | headlights (auto / on / off) |
| G | siren (emergency vehicles) |

| General | |
|---|---|
| T (hold) | fast-forward time |
| F1 | help |
| F3 | debug info |
| F11 | fullscreen |
| Esc / P | pause (Q quits from the pause menu) |

## Assets: where they go and how loading works

```
assets/
  data/          vehicles.cfg characters.cfg weapons.cfg foliage.cfg   <- what gets loaded
  textures/      asphalt sidewalk plaza cobble concrete gravel grass bricks (.png, ambientCG)
  vehicles/      Unlucky Studio top-down vehicle sprites (.png, front of the car = up)
  survivor/      Survivor animation frames: <set>/<state>/<prefix><n>.png, feet/<anim>/...
  foliage/       top-down tree / shrub canopies (.png)
  fonts/         Rajdhani (.ttf)
  sounds/        optional: <name>.wav overrides a synthesised sound (engine, siren, pistol, ...)
```

Everything is **data driven**. Adding content means dropping in files and adding a line, with no code changes:

- **New vehicle:**
  1. Put `mycar.png` (facing up) in `assets/vehicles/`.
  2. Add `SPRITE <class> mycar.png 2` to `vehicles.cfg`.
  3. For a new *type* of vehicle, also add a `CLASS` line (size in metres, top speed in km/h, grip, mass, hp, traffic weight, flags such as `police` / `emergency` / `large` / `two_wheeler`).

  `DERIVE` and `COMPOSE` build new vehicles from existing sprites (stretching or combining them).
- **New character animation or weapon:** add `ANIM` lines to `characters.cfg` (frames facing right, plus the body pivot), then a `WEAPON` line to `weapons.cfg` that uses that set.
- **New trees or bushes:** a `TREE` or `BUSH` line in `foliage.cfg`.
- **Sounds:** drop `assets/sounds/<name>.wav` in to replace a synthesised effect.

Each loader has a fallback (built-in defaults or procedural art), so a missing file never crashes the game.

Scale is set in `src/config.h`:
- `PX_PER_METER = 16`
- `CHAR_SCALE` draws people slightly larger than life for readability, as GTA did
- camera distances (`CAM_VIEW_*`)
- city size and population

## Source layout

| File | Purpose |
|---|---|
| `main.cpp` | window, main loop, `--shot` test mode |
| `game.*`, `hud.cpp` | game rules, player, weapons, wanted level, police, missions, HUD/menus |
| `render.*` | 3D renderer: camera rig, render targets sharing depth, bloom, sprite/box helpers |
| `lighting.*` | day/night cycle |
| `city_map.*` | city generation, 3D structures, collisions/queries, minimap |
| `vehicle.*`, `vehicle_types.*` | engine / brake / tyre model and drawing; data-driven vehicle classes |
| `physics.*` | vehicle rigid-body contacts: collision manifolds, sub-stepped impulse solver, breakaway objects |
| `traffic.*` | rail-based traffic AI, junction rules, police AI |
| `pedestrian.*` | pedestrian AI & drawing |
| `particles.*` | particles, skid marks, decals, flash lights |
| `assets.*`, `datafile.*` | config-driven asset loading |
| `sprite_gen.*` | anti-aliased SDF "vector painter" for procedural sprites |
| `audio.*` | procedural sound synthesis |
| `tools/make_unarmed.py` | derives the unarmed Survivor frames from the knife set |
| `tools/sprite_dump.cpp` | renders all procedural sprites to PNG for inspection |

## Automated screenshots / tests

```bash
ConcreteJungle.exe --shot out.png --frames 600 --scenario drive
```
Scenarios: `foot day drive night nightdrive chase overview title crash derby`. The run logs the average FPS plus traffic statistics (moving/stopped cars, jams, jolts) and physics statistics (jitter, penetration, stuck events, knocked / re-joined traffic).
- `crash`: scripted course (wall grind, full-speed head-on, building corner, lamp post, hydrant, bollard, shoving parked cars into a wall), logging every impact.
- `derby`: full throttle through traffic with random steering.
- `--every N` also saves a screenshot every N frames. File names are relative to the working directory.

## Credits

See [CREDITS.md](CREDITS.md). The Survivor character by Riley Gombart is CC-BY 3.0; everything else is CC0 or OFL.
