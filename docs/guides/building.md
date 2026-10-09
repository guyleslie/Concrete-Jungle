# Building

How to set up the toolchain, build the game and fix common build problems.

## Contents

- [Requirements](#requirements)
- [Build options](#build-options)
- [Running](#running)
- [Adding a source file](#adding-a-source-file)
- [Packaging a release](#packaging-a-release)
- [Troubleshooting](#troubleshooting)

## Requirements

| Component | Version / notes |
|---|---|
| raylib | 6.0. The Windows installer ships a prebuilt `libraylib.a` and the w64devkit GCC toolchain. |
| Compiler | GCC with C++17 (w64devkit's GCC 15 is used for development). |
| Location | The build scripts expect raylib in `E:\Apps\raylib`; the installer default is `C:\raylib`. Set `RAYLIB_DIR` to the installation folder: `set RAYLIB_DIR=C:\raylib` before `build.bat`, or `RAYLIB_DIR=/c/raylib sh build.sh` in Git Bash. `compile_flags.txt`, the clangd configuration for editors, uses the same default paths. |

Windows is the development platform. The CMake project also supports Linux and macOS by downloading raylib (`-DRAYLIB_FETCH=ON`); those builds are currently untested.

## Build options

### Git Bash — incremental (recommended)

```bash
sh build.sh
```

Recompiles only changed sources (or all of them after a header change) and links `ConcreteJungle.exe`. Compiler output for each file is written to `build/obj/<file>.log`.

### Command Prompt — full rebuild

```bat
build.bat
```

Add `debug` for a build with debug symbols and no optimisation. A full rebuild takes about a minute. In PowerShell, set the folder with `$env:RAYLIB_DIR = "C:\raylib"` and run `.\build.bat`.

Both scripts stop with `raylib not found in <folder>` when `RAYLIB_DIR` does not contain `raylib\src\raylib.h`, and end with `BUILD FAILED` on any error; `build.sh` also names the source file that failed and its log.

### CMake

```bash
cmake -S . -B build_cmake -G Ninja -DCMAKE_CXX_COMPILER=g++ -DRAYLIB_DIR=E:/Apps/raylib/raylib
```

```bash
cmake --build build_cmake
```

Use the Ninja generator: MinGW Make fails on the non-ASCII characters in the project path. The CMake build copies `assets/` next to the executable.

All build entry points include the startup reporter, metadata discovery, loading renderer and snapshot fixture. Package the whole `assets/` directory, including `assets/data/loading.cfg` and `assets/ui/`; the loading images and fonts have a primitive fallback if missing.

### Manual

```bash
g++ -std=c++17 -O2 -Isrc -I<raylib>/src src/*.cpp -o ConcreteJungle -L<raylib>/src -lraylib -lopengl32 -lgdi32 -lwinmm
```

On Linux, replace the Windows libraries with `-lGL -lm -lpthread -ldl -lrt -lX11`.

## Running

Run the executable from the project folder so it finds `assets/`; if it is not there, the game falls back to the executable's own folder. Missing data files or images do not stop the game — built-in defaults and procedural placeholders are used instead, and a warning is logged.

For automated test runs, see [Testing](../testing.md).

## Adding a source file

`build.sh` picks up every `src/*.cpp` automatically. Add new files to `build.bat` and to the `SOURCES` list in `CMakeLists.txt` as well.

## Packaging a release

A release is a zip file with the executable, the `assets/` folder, the licence, credits, readme and changelog, attached to a [GitHub release](https://github.com/guyleslie/Concrete-Jungle/releases).

1. Move the *Unreleased* entries of the [changelog](../../CHANGELOG.md) into a new section, `## [X.Y.Z] - YYYY-MM-DD`, and commit.
2. Tag the commit: `git tag -a vX.Y.Z -m "Concrete Jungle X.Y.Z"`.
3. In a fresh clone of the tag, build with `build.bat` and package:

   ```bash
   python tools/package_release.py --version X.Y.Z
   ```

   The script writes `build/release/ConcreteJungle-X.Y.Z-windows-x64.zip` and prints its SHA-256. It refuses a console executable from `build.sh`, which is meant for test runs, and a version without a changelog section.
4. Extract the zip to a new folder and start the game from there.
5. Push the commit and the tag (`git push origin main vX.Y.Z`). Create a GitHub release from the tag, with the changelog section as its notes, and attach the zip. Mark it as a pre-release while playtests of its changes are open; GitHub's `/releases/latest` address skips pre-releases, so the README links to the releases page instead. With the [GitHub CLI](https://cli.github.com/):

   ```bash
   gh release create vX.Y.Z build/release/ConcreteJungle-X.Y.Z-windows-x64.zip --title "Concrete Jungle X.Y.Z" --notes-file <notes.md> --prerelease --verify-tag
   ```

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `build.bat` behaves strangely or fails to parse | The file must use CRLF line endings. `.gitattributes` enforces this on checkout; editors must not convert it. |
| CMake with MinGW Make fails with path errors | The project path contains `á`. Use the Ninja generator. |
| An image does not load | This raylib build has no JPG support. Convert the image to PNG. |
| `assets/ folder not found` warning | Neither the working folder nor the executable's folder contains `assets/`. Keep the executable next to the `assets/` folder. |
| `raylib not found in <folder>` | `RAYLIB_DIR` is not set, or does not point at the raylib installation (the folder that contains `raylib\src\raylib.h`). `build.sh` needs the Git Bash form (`/c/raylib`), `build.bat` the Windows form (`C:\raylib`). |
| `'build.bat' is not recognized` in PowerShell | PowerShell does not run scripts from the current folder by name. Use `.\build.bat`. |
| Link errors about raylib | `RAYLIB_DIR` points at an incomplete raylib installation: `raylib\src\libraylib.a` is missing. |
