# Building

How to set up the toolchain, build the game and fix common build problems.

## Contents

- [Requirements](#requirements)
- [Build options](#build-options)
- [Running](#running)
- [Adding a source file](#adding-a-source-file)
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

Add `debug` for a build with debug symbols and no optimisation. A full rebuild takes about 45 seconds.

### CMake

```bash
cmake -S . -B build_cmake -G Ninja -DCMAKE_CXX_COMPILER=g++ -DRAYLIB_DIR=E:/Apps/raylib/raylib
```

```bash
cmake --build build_cmake
```

Use the Ninja generator: MinGW Make fails on the non-ASCII characters in the project path. The CMake build copies `assets/` next to the executable.

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

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `build.bat` behaves strangely or fails to parse | The file must use CRLF line endings. `.gitattributes` enforces this on checkout; editors must not convert it. |
| CMake with MinGW Make fails with path errors | The project path contains `á`. Use the Ninja generator. |
| An image does not load | This raylib build has no JPG support. Convert the image to PNG. |
| `assets/ folder not found` warning | The game was started from another folder. Start it from the project folder. |
| Link errors about raylib | `RAYLIB_DIR` does not point at the raylib installation. `build.sh` needs the Git Bash form (`/c/raylib`), `build.bat` the Windows form (`C:\raylib`). |
