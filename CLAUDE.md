# Concrete Jungle - notes for Claude

GTA1/2-style top-down open-city game in C++17 + raylib 6.0 (Windows, w64devkit GCC).
Player-facing overview, controls and asset formats: README.md. Open work: BACKLOG.md.

## Working with the user
- The user writes Hungarian: answer in Hungarian. Code, comments and repo docs stay in English
  (BACKLOG.md is Hungarian - it is the user's working list).
- Quality bar: consistent, high-quality art (glossy Unlucky Studio style, no flat/cartoon or pixel art);
  realistic scale 16 px = 1 m; GTA2 fake-3D look.
- Data driven: new vehicles / characters / weapons / foliage come from `assets/data/*.cfg`,
  never from code changes. Keep it that way.
- One topic per session: agree a short spec first (target behaviour, numbers), add a measurable
  `--shot` scenario, implement, let the user playtest, then tune. Update BACKLOG.md when done.
- The user watches the test windows - verify with a run before claiming a fix works.

## Build
- `sh build.sh` - Git Bash, incremental (globs `src/*.cpp`), compiler output in `build/obj/<file>.log`. Preferred.
- `build.bat` - cmd, full rebuild (~45 s). Must stay CRLF (enforced by .gitattributes).
- CMake: Ninja only - MinGW Make breaks on the non-ASCII project path (`Programozás`).
- raylib lives in `E:\Apps\raylib` (`RAYLIB_DIR`). This raylib build has no JPG support: PNG only.
- A new `.cpp` must be added to `build.bat` and `CMakeLists.txt`.

## Test / verify
```
ConcreteJungle.exe --shot build/shots/x.png --frames 1500 [--every 30] --scenario crash
```
- Scenarios: `foot day drive night nightdrive chase overview title crash derby`.
- Logs `TRAFFIC:` (speeds, stop reasons, jolts) and `PHYS:` lines (jitter flips, penetration,
  stuck events, knocked / re-joined traffic); `crash` also logs every `IMPACT`.
- Screenshot paths must be relative (raylib prepends the working directory). `--every N` saves a series.
- `crash` = wall grind, head-on, building corner, lamp post, hydrant, bollard, shoving parked cars;
  `derby` = full throttle through traffic.

## Architecture (where things live)
| Area | Files | Key facts |
|---|---|---|
| Loop / test mode | `main.cpp` | fixed dt 1/60 in `--shot` mode, dt clamped to 1/20 otherwise |
| Rules, player, HUD | `game.*`, `hud.cpp` | `Game::UpdateVehicles` = AI -> `physics.Step` -> `HandleImpacts` (damage, sfx) -> effects |
| Vehicle model | `vehicle.*`, `vehicle_types.*` | `VehicleForces` per physics sub-step (engine, brakes, tyres); classes from `vehicles.cfg` |
| Collisions | `physics.*` | Box2D-v3-style: collide once/frame (speculative), 240 Hz sub-steps, soft push-out, restitution; emits `ImpactEvent`s only |
| Traffic / police AI | `traffic.*` | traffic runs kinematically "on rails"; a hit knocks it into physics, then it re-joins or gives up. Do NOT go back to physics-driven traffic AI (it jittered and deadlocked) |
| City | `city_map.*` | procedural blocks, buildings (AABB), street furniture; `ApplyMaterial` sets breakaway strength / soft / box shape |
| Rendering | `render.*`, `lighting.*`, `particles.*` | real 3D, Camera3D looking straight down; light/emissive passes share the depth buffer; bloom |
| People | `pedestrian.*` | sidewalk AI, obstacle avoidance |
| Assets / audio | `assets.*`, `datafile.*`, `sprite_gen.*`, `audio.*` | cfg loaders with fallbacks; procedural placeholder sprites; synthesised sound |

Conventions: angle 0 = facing up (-Y), clockwise positive; `Forward(a) = (sin a, -cos a)`, `RightOf(a) = (cos a, sin a)`.
Masses in tonnes, speeds in px/s (16 px/s = 1 m/s = 3.6 km/h).

## Harness gotchas
- Bash heredocs containing quotes fail here: write scripts to the scratchpad and run them.
- PowerShell needs `-LiteralPath` for the project path (the `á`).
