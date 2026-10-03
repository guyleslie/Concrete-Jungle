@echo off
rem ==================================================================================
rem  Concrete Jungle - one-click build with the w64devkit GCC that ships with raylib.
rem  Usage:  build.bat          (optimised release build -> ConcreteJungle.exe)
rem          build.bat debug    (debug symbols, no optimisation)
rem  Adjust RAYLIB_DIR if raylib is installed somewhere else (default installer: C:\raylib).
rem ==================================================================================
setlocal
if "%RAYLIB_DIR%"=="" set RAYLIB_DIR=E:\Apps\raylib
set PATH=%RAYLIB_DIR%\w64devkit\bin;%PATH%
set CXXFLAGS=-std=c++17 -O2 -DNDEBUG
if /I "%1"=="debug" set CXXFLAGS=-std=c++17 -O0 -g
if not exist build mkdir build

g++ %CXXFLAGS% -Wall -Wno-missing-braces -Isrc -I"%RAYLIB_DIR%\raylib\src" ^
    src\main.cpp src\game.cpp src\hud.cpp src\assets.cpp src\sprite_gen.cpp src\render.cpp ^
    src\lighting.cpp src\city_map.cpp src\vehicle.cpp src\vehicle_types.cpp src\traffic.cpp src\physics.cpp ^
    src\pedestrian.cpp src\particles.cpp src\audio.cpp src\datafile.cpp src\vehicle_tests.cpp ^
    -o ConcreteJungle.exe -L"%RAYLIB_DIR%\raylib\src" -lraylib -lopengl32 -lgdi32 -lwinmm -static -mwindows
if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)
echo.
echo Built ConcreteJungle.exe - run it from this folder so it finds assets\
