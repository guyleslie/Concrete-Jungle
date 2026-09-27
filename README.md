# Concrete Jungle

A top-down open-city action game in the spirit of GTA 1 and GTA 2, written in C++17 with [raylib](https://www.raylib.com/) 6.0. The classic overhead view is rendered as real 3D geometry, so buildings lean with perspective and bridges hide what passes beneath them.

> **Status:** early development. The game is playable end to end; the vehicle handling and damage models are being reworked. See the [backlog](docs/backlog.md).

## Features

| Area | Highlights |
|---|---|
| City | A procedurally generated 584 × 584 m island: streets with traffic lights, buildings with rooftop detail, parks, plazas, parking lots, a police station and a hospital, gate buildings, skybridges and an elevated metro with a running train |
| Rendering | Perspective top-down camera, day/night cycle with sun shadows, headlights and street lamps, lit windows and neon, bloom |
| Traffic | Lane-following traffic that obeys signals and junction rules, overtakes, honks and makes U-turns; police pursuits |
| Physics | Sub-stepped rigid-body contact solver, crash damage based on delta-V, breakaway lamp posts, hydrants and other street furniture |
| Pedestrians | Sidewalk walkers who wait for the green light, wander into parks, flee danger and can be knocked down |
| Gameplay | On-foot combat with six weapons, carjacking, a six-star wanted level, arrests, missions from ringing pay phones, pickups |
| Audio | Every sound synthesised at start-up; any of them can be replaced with a WAV file |
| Content | Vehicles, characters, weapons and foliage defined in plain-text data files |

## Quick start

**Requirements:** Windows with raylib 6.0 and its bundled w64devkit GCC toolchain, installed in `E:\Apps\raylib` (otherwise see the [build guide](docs/guides/building.md)).

Build from Git Bash:

```bash
sh build.sh
```

or from the Command Prompt:

```bat
build.bat
```

Run the game from the project folder so it finds `assets/`:

```bash
./ConcreteJungle.exe
```

## Controls

### On foot

| Key | Action |
|---|---|
| W A S D / arrow keys | Move (the character faces where it walks) |
| Shift | Run |
| Right mouse button (hold) | Aim at the cursor, strafe or backpedal |
| Left mouse button | Attack or shoot (also turns towards the cursor) |
| R | Reload |
| 1–6, Q, mouse wheel | Switch weapon |
| E / F / Enter | Enter or hijack a vehicle |

### Driving

| Key | Action |
|---|---|
| W / S | Throttle; brake, then reverse |
| A / D | Steer |
| Space | Handbrake |
| E / F / Enter | Get out |
| H | Horn |
| L | Headlights: auto / on / off |
| G | Siren (emergency vehicles) |

### General

| Key | Action |
|---|---|
| T (hold) | Fast-forward time |
| F1 | Show or hide help |
| F3 | Debug information |
| F11 | Fullscreen |
| Esc / P | Pause (Q quits from the pause menu) |

## Documentation

| Document | Contents |
|---|---|
| [Documentation index](docs/README.md) | Everything below, plus the design document for each subsystem |
| [Architecture](docs/architecture.md) | Modules, frame lifecycle, units and conventions |
| [Building](docs/guides/building.md) | Toolchain, build options, troubleshooting |
| [Adding content](docs/guides/adding-content.md) | New vehicles, characters, weapons, foliage and sounds without code changes |
| [Testing](docs/testing.md) | Automated scenario runs and their metrics |
| [Backlog](docs/backlog.md) | Planned work with priorities and acceptance criteria |
| [Changelog](CHANGELOG.md) | Notable changes per milestone |
| [Contributing](CONTRIBUTING.md) | Workflow, coding style, commits and documentation conventions |

## Project layout

```text
assets/   data files (assets/data/*.cfg), textures, sprites, fonts
docs/     project documentation
src/      C++ sources, one module per subsystem
tools/    helper scripts: derived art, sprite export, documentation check
```

## Credits and licensing

Third-party assets and their licences are listed in [CREDITS.md](CREDITS.md). The "Survivor" character by Riley Gombart is licensed under CC-BY 3.0 and is credited on the title screen. A licence for the project's own code has not been chosen yet ([CJ-009](docs/backlog.md#cj-009-licence-for-the-code)).
