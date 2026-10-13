# Concrete Jungle

A top-down open-city action game in the spirit of GTA 1 and GTA 2, written in C++17 with [raylib](https://www.raylib.com/) 6.0. The classic overhead view is rendered as real 3D geometry, so buildings lean with perspective and bridges hide what passes beneath them.

> **Status:** early development. The [changelog](CHANGELOG.md) lists what each release brings, the [backlog](docs/backlog.md) what comes next.

![A junction at dusk: a school bus crossing, traffic at the lights, people on the sidewalks, street lamps and lit windows](docs/images/drive-dusk.png)

## The game

Explore a procedurally generated city on foot or in any vehicle you can take: traffic that follows the rules of the road, people going about their day, police who pursue you, missions from ringing pay phones and a car radio. The city lives through a day-and-night cycle, and vehicles, people and street furniture react to each other physically.

Vehicles, people, weapons, sounds and radio stations are defined in plain-text data files, so content can be added without changing the code. How each part works is described in the [design documents](docs/README.md#design-documents).

## Quick start

### Play

You need 64-bit Windows and a graphics card that supports OpenGL 3.3.

1. Download `ConcreteJungle-<version>-windows-x64.zip` from the newest release on the [releases page](https://github.com/guyleslie/Concrete-Jungle/releases) and extract it.
2. Double-click `ConcreteJungle.exe` in the extracted folder. The executable is not signed, so Windows SmartScreen may warn about it: choose *More info › Run anyway*.

The game runs full screen at your monitor's resolution. Press Enter on the title screen, F1 for help, Esc to pause and Q in the pause menu to quit. Keep the executable next to the `assets/` folder: the game loads its data, textures and fonts from there.

### Build from source

You need the same, and about 2 GB of disk space for raylib and its tools.

1. **Install raylib 6.0.** Download the *raylib 6.0 Windows Installer (64bit)* from [itch.io](https://raysan5.itch.io/raylib) and run it. It installs raylib together with the w64devkit GCC compiler the game is built with, by default into `C:\raylib`.
2. **Get the source.** Clone the repository, or download it from GitHub as a ZIP (*Code › Download ZIP*) and extract it:

   ```bash
   git clone https://github.com/guyleslie/Concrete-Jungle.git
   ```

3. **Build.** Open a Command Prompt in the project folder (in File Explorer, type `cmd` into the address bar and press Enter), tell the build script where raylib is installed, and build:

   ```bat
   set RAYLIB_DIR=C:\raylib
   ```

   ```bat
   build.bat
   ```

   The build takes a minute or two and ends with `Built ConcreteJungle.exe`; the game is then in the project folder. If the build cannot find raylib, it says so: check `RAYLIB_DIR`.

   In PowerShell (the default of Windows Terminal), the same two commands are `$env:RAYLIB_DIR = "C:\raylib"` and `.\build.bat`. In Git Bash, run `RAYLIB_DIR=/c/raylib sh build.sh`: it recompiles only the changed files, and the game it builds opens a console window with its log.
4. **Play.** Double-click `ConcreteJungle.exe` in the project folder, as in [Play](#play).

Other build options (debug builds, CMake, Linux and macOS), packaging a release and troubleshooting are in the [build guide](docs/guides/building.md).

## Controls

### On foot

| Key | Action |
|---|---|
| W A S D / arrow keys | Move (the character faces where it walks) |
| Shift | Run (walking is a real walking pace) |
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
| Q / mouse wheel | Radio: next or previous station, then off |

### General

| Key | Action |
|---|---|
| T (hold) | Fast-forward time |
| F1 | Show or hide help |
| F3 | Debug information |
| Esc / P | Pause (Q quits from the pause menu) |
| Alt+F4 | Quit |

## Documentation

| Document | Contents |
|---|---|
| [Documentation index](docs/README.md) | Every document, including the design document of each subsystem |
| [Architecture](docs/architecture.md) | Modules, frame lifecycle, units and conventions |
| [Building](docs/guides/building.md) | Toolchain, build options, packaging a release, troubleshooting |
| [Adding content](docs/guides/adding-content.md) | New vehicles, people, weapons, foliage, sounds and radio stations without code changes |
| [Testing](docs/testing.md) | Automated scenarios, isolated test scenes and their measurements |
| [Decision records](docs/adr/README.md) | Why the fundamental approaches are the way they are |
| [Backlog](docs/backlog.md) | Planned work with priorities and acceptance criteria |
| [Changelog](CHANGELOG.md) | Notable changes per release |
| [Contributing](CONTRIBUTING.md) | Workflow, coding style, commits and documentation conventions |

## Project layout

```text
assets/   game content: data files (assets/data/*.cfg), art, fonts and sounds
docs/     project documentation
src/      C++ sources, one module per subsystem
tests/    scene files of the test scenarios (not part of a release)
tools/    helper scripts: art pipelines, test runners, documentation check, release packaging
```

## Credits and licensing

The project's own code is licensed under the [MIT licence](LICENSE). Third-party assets keep their own licences, listed in [CREDITS.md](CREDITS.md).
