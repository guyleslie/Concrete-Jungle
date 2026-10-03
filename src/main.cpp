// =====================================================================================
//  Concrete Jungle - a GTA1/2-style top-down open-city game in C++ / raylib 6.
//
//  Module overview:
//    config.h          global constants (scale 16 px = 1 m, city size, population)
//    assets.*          data-driven asset loading (assets/data/*.cfg) + fallbacks
//    sprite_gen.*      procedural sprite painter (props, placeholders)
//    render.*          3D renderer core: camera, render targets, bloom, sprite helpers
//    lighting.*        day/night cycle
//    city_map.*        procedural city: tiles, 3D buildings, rail, furniture, signals
//    vehicle.*         engine / brake / tyre model & drawing;  vehicle_types.* class registry
//    physics.*         vehicle collisions: contact generation + sub-stepped impulse solver
//    traffic.*         traffic & police AI
//    pedestrian.*      pedestrian AI & drawing
//    particles.*       smoke, fire, debris, skid marks, decals, flashes
//    audio.*           procedural sound
//    game.* / hud.cpp  rules, player, weapons, wanted level, missions, HUD
// =====================================================================================
#include "raylib.h"
#include "config.h"
#include "assets.h"
#include "game.h"
#include "vehicle_tests.h"
#include "rlgl.h"

static void LoadingScreen(const char* msg) {
    BeginDrawing();
    ClearBackground({ 12, 14, 18, 255 });
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawText("CONCRETE JUNGLE", w / 2 - MeasureText("CONCRETE JUNGLE", 60) / 2, h / 2 - 60, 60, { 255, 205, 70, 255 });
    DrawText(msg, w / 2 - MeasureText(msg, 20) / 2, h / 2 + 20, 20, LIGHTGRAY);
    EndDrawing();
}

int main(int argc, char** argv) {
    // --shot <file.png> [--frames N] [--every N] [--scenario foot|drive|night|nightdrive|chase|day|crash|derby|rampage]
    // (file names are relative to the working directory; --every also saves <file>_<frame>.png)
    const char* shot = nullptr; const char* scenario = "foot"; int shotFrames = 180, every = 0;
    const char* testVehicle = "Taxi";
    bool uncapped = false;
    for (int i = 1; i < argc; i++) {
        if (TextIsEqual(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (TextIsEqual(argv[i], "--frames") && i + 1 < argc) shotFrames = TextToInteger(argv[++i]);
        else if (TextIsEqual(argv[i], "--every") && i + 1 < argc) every = TextToInteger(argv[++i]);
        else if (TextIsEqual(argv[i], "--scenario") && i + 1 < argc) scenario = argv[++i];
        else if (TextIsEqual(argv[i], "--vehicle") && i + 1 < argc) testVehicle = argv[++i];
        else if (TextIsEqual(argv[i], "--uncapped")) uncapped = true;
    }
    SetConfigFlags(((shot && uncapped) ? 0u : FLAG_VSYNC_HINT) | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(cfg::SCREEN_W, cfg::SCREEN_H, cfg::WINDOW_TITLE);
    SetExitKey(KEY_NULL);                 // Esc opens the pause menu instead of quitting
    if (!shot) {
        // The game only runs full screen: a borderless window at the monitor's resolution,
        // with no frame to resize or minimise. Alt+F4 closes it. The Windows cursor is
        // hidden; the HUD draws a crosshair on foot. Test runs keep the fixed window so
        // their screenshots stay comparable.
        ToggleBorderlessWindowed();
        HideCursor();
        TraceLog(LOG_INFO, "WINDOW: borderless full screen %dx%d on a %dx%d monitor", GetScreenWidth(), GetScreenHeight(),
                 GetMonitorWidth(GetCurrentMonitor()), GetMonitorHeight(GetCurrentMonitor()));
    }

    // Assets are loaded relative to the working directory; fall back to the exe folder.
    if (!DirectoryExists("assets")) ChangeDirectory(GetApplicationDirectory());
    if (!DirectoryExists("assets")) TraceLog(LOG_WARNING, "assets/ folder not found - using procedural fallbacks");

    LoadingScreen("Loading textures, vehicles and characters...");
    gAssets.Load();
    LoadingScreen("Building the city...");

    static Game game;                     // large object: keep it off the stack
    game.Init();
    VehicleTests tests;
    bool fixture = shot && (TextIsEqual(scenario, "handling") || TextIsEqual(scenario, "crash-handling"));
    if (fixture && !tests.Init(game, scenario, testVehicle)) {
        game.Unload(); gAssets.Unload(); CloseWindow(); return 2;
    }
    if (shot && !fixture) { game.DebugScenario(scenario); game.debugContacts = true; }
    int frame = 0;

    while (!WindowShouldClose() && !game.quit) {
        float dt = GetFrameTime();
        if (dt > 1.0f / 20.0f) dt = 1.0f / 20.0f;    // avoid huge steps after a stall
        if (shot) dt = 1.0f / 60.0f;
        if (fixture) tests.Update(game, dt); else game.Update(dt);
        BeginDrawing();
        ClearBackground(BLACK);
        if (fixture) tests.Draw(game); else game.Draw();
        bool lastShot = shot && ++frame >= shotFrames;
        if (lastShot) {
            rlDrawRenderBatchActive(); TakeScreenshot(shot);
            if (fixture) tests.Log();
            else { game.LogTrafficStats(); game.LogPhysStats(); game.LogPedStats(); }
        }
        else if (shot && every > 0 && frame % every == 0) {
            rlDrawRenderBatchActive();
            const char* ext = GetFileExtension(shot);
            int stem = ext ? (int)(ext - shot) : (int)TextLength(shot);
            TakeScreenshot(TextFormat("%.*s_%04d.png", stem, shot, frame));
        }
        if (fixture && tests.CaptureLabel()) {
            rlDrawRenderBatchActive();
            const char* ext = GetFileExtension(shot);
            int stem = ext ? (int)(ext - shot) : (int)TextLength(shot);
            TakeScreenshot(TextFormat("%.*s_%s.png", stem, shot, tests.CaptureLabel()));
            tests.ClearCaptureRequest();
        }
        EndDrawing();
        if (lastShot) break;
        if (shot && frame > 30) { static double acc = 0; static int n = 0; acc += GetFrameTime(); n++;
            if (frame == shotFrames - 1) TraceLog(LOG_INFO, "SHOT: average %.1f FPS over %d frames", n / acc, n); }
    }
    game.Unload();
    gAssets.Unload();
    CloseWindow();
    return fixture && (!tests.Finished() || tests.Failed()) ? 1 : 0;
}
