#!/bin/sh
# Incremental build for the Git-Bash / w64devkit shell (build.bat is the cmd equivalent).
# Set RAYLIB_DIR if raylib is installed somewhere else, e.g. RAYLIB_DIR=/c/raylib sh build.sh
RAYLIB_DIR=${RAYLIB_DIR:-/e/Apps/raylib}
export PATH="$RAYLIB_DIR/w64devkit/bin:$PATH"
RL=$RAYLIB_DIR/raylib/src
mkdir -p build/obj
fail=0
for f in src/*.cpp; do
  n=$(basename "$f" .cpp); o=build/obj/$n.o
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find src -name '*.h' -newer "$o" 2>/dev/null | head -1)" ]; then
    g++ -std=c++17 -O2 -Wall -Wno-missing-braces -Isrc -I"$RL" -c "$f" -o "$o" 2> "build/obj/$n.log" || { fail=1; grep -E "error" "build/obj/$n.log" | head -20; }
  fi
done
[ $fail = 0 ] && g++ build/obj/*.o -o ConcreteJungle.exe -L"$RL" -lraylib -lopengl32 -lgdi32 -lwinmm -static && echo BUILD OK
