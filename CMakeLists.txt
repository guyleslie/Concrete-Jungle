cmake_minimum_required(VERSION 3.16)
project(ConcreteJungle CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- raylib ------------------------------------------------------------------------
# Option A (default on Windows here): prebuilt raylib from the raylib installer.
#   cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DRAYLIB_DIR=E:/Apps/raylib/raylib
#   cmake --build build
#   (use Ninja: MinGW Make fails when the project path contains non-ASCII characters)
# Option B: -DRAYLIB_FETCH=ON downloads and builds raylib 6.0 with FetchContent.
option(RAYLIB_FETCH "Download and build raylib with FetchContent" OFF)
set(RAYLIB_DIR "E:/Apps/raylib/raylib" CACHE PATH "raylib source folder containing src/raylib.h and src/libraylib.a")

# Sources listed explicitly (a CONFIGURE_DEPENDS glob breaks with MinGW Make when the
# project path contains non-ASCII characters).
set(SOURCES
    src/main.cpp src/game.cpp src/hud.cpp src/assets.cpp src/sprite_gen.cpp src/render.cpp
    src/lighting.cpp src/city_map.cpp src/vehicle.cpp src/vehicle_types.cpp src/traffic.cpp src/traffic_recovery.cpp src/physics.cpp
    src/pedestrian.cpp src/particles.cpp src/audio.cpp src/datafile.cpp src/vehicle_tests.cpp src/traffic_tests.cpp src/traffic_clearance_tests.cpp src/traffic_conflict_tests.cpp src/traffic_incidents.cpp src/traffic_incident_tests.cpp)
add_executable(ConcreteJungle ${SOURCES})
target_include_directories(ConcreteJungle PRIVATE src)

if(RAYLIB_FETCH)
    include(FetchContent)
    FetchContent_Declare(raylib GIT_REPOSITORY https://github.com/raysan5/raylib.git GIT_TAG 6.0)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)
    target_link_libraries(ConcreteJungle PRIVATE raylib)
else()
    target_include_directories(ConcreteJungle PRIVATE ${RAYLIB_DIR}/src)
    target_link_directories(ConcreteJungle PRIVATE ${RAYLIB_DIR}/src)
    target_link_libraries(ConcreteJungle PRIVATE raylib)
    if(WIN32)
        target_link_libraries(ConcreteJungle PRIVATE opengl32 gdi32 winmm)
    elseif(APPLE)
        target_link_libraries(ConcreteJungle PRIVATE "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
    else()
        target_link_libraries(ConcreteJungle PRIVATE GL m pthread dl rt X11)
    endif()
endif()

if(MINGW)
    target_link_options(ConcreteJungle PRIVATE -static)
endif()

# Copy the assets next to the executable so it runs from the build folder too.
add_custom_command(TARGET ConcreteJungle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:ConcreteJungle>/assets)
