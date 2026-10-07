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
#include "traffic_tests.h"
#include "traffic_clearance_tests.h"
#include "traffic_conflict_tests.h"
#include "traffic_incident_tests.h"
#include "traffic_turn_tests.h"
#include "traffic_turns.h"
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
        // Measurement runs may read an alternative traffic data file (e.g. a baseline).
        else if (TextIsEqual(argv[i], "--traffic-config") && i + 1 < argc) gTrafficConfigPath = argv[++i];
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
    LoadingScreen("Planning traffic turns...");
    RailPlanTurns();
    LoadingScreen("Building the city...");

    static Game game;                     // large object: keep it off the stack
    game.Init();
    VehicleTests tests;
    TrafficTests trafficTests;
    TrafficClearanceTests clearanceTests;
    TrafficConflictTests conflictTests;
    TrafficIncidentTests incidentTests;
    TrafficTurnTests turnTests;
    bool vehicleFixture = shot && (TextIsEqual(scenario, "handling") || TextIsEqual(scenario, "crash-handling"));
    bool trafficFixture = shot && TextIsEqual(scenario, "traffic-recovery");
    bool clearanceFixture = shot && TextIsEqual(scenario, "traffic-clearance");
    bool conflictFixture = shot && TextIsEqual(scenario, "traffic-conflict");
    bool incidentFixture = shot && TextIsEqual(scenario, "traffic-incident");
    bool turnFixture = shot && TextIsEqual(scenario, "traffic-turns");
    bool fixture = vehicleFixture || trafficFixture || clearanceFixture || conflictFixture || incidentFixture || turnFixture;
    if ((vehicleFixture && !tests.Init(game, scenario, testVehicle)) || (trafficFixture && !trafficTests.Init(game, testVehicle)) ||
        (clearanceFixture && !clearanceTests.Init(game)) || (conflictFixture && !conflictTests.Init(game)) ||
        (incidentFixture && !incidentTests.Init(game)) || (turnFixture && !turnTests.Init(game))) {
        game.Unload(); gAssets.Unload(); CloseWindow(); return 2;
    }
    if (shot && !fixture) { game.DebugScenario(scenario); game.debugContacts = true; }
    if (trafficFixture || clearanceFixture || conflictFixture) game.debugContacts = true;
    int frame = 0;

    while (!WindowShouldClose() && !game.quit) {
        float dt = GetFrameTime();
        if (dt > 1.0f / 20.0f) dt = 1.0f / 20.0f;    // avoid huge steps after a stall
        if (shot) dt = 1.0f / 60.0f;
        if (vehicleFixture) tests.Update(game, dt);
        else if (trafficFixture) trafficTests.Update(game, dt);
        else if (clearanceFixture) clearanceTests.Update(game, dt);
        else if (conflictFixture) conflictTests.Update(game, dt);
        else if (incidentFixture) incidentTests.Update(game, dt);
        else if (turnFixture) turnTests.Update(game, dt);
        else game.Update(dt);
        BeginDrawing();
        ClearBackground(BLACK);
        if (vehicleFixture) tests.Draw(game);
        else if (trafficFixture) trafficTests.Draw(game);
        else if (clearanceFixture) clearanceTests.Draw(game);
        else if (conflictFixture) conflictTests.Draw(game);
        else if (incidentFixture) incidentTests.Draw(game);
        else if (turnFixture) turnTests.Draw(game);
        else game.Draw();
        bool lastShot = shot && (++frame >= shotFrames || (conflictFixture && conflictTests.Finished()) ||
                                (incidentFixture && incidentTests.Finished()) || (turnFixture && turnTests.Finished()));
        if (lastShot) {
            rlDrawRenderBatchActive(); TakeScreenshot(shot);
            if (vehicleFixture) tests.Log();
            else if (trafficFixture) trafficTests.Log();
            else if (clearanceFixture) clearanceTests.Log();
            else if (conflictFixture) conflictTests.Log();
            else if (incidentFixture) incidentTests.Log();
            else if (turnFixture) turnTests.Log();
            else { game.LogTrafficStats(); game.LogPhysStats(); game.LogPedStats(); }
        }
        else if (shot && every > 0 && frame % every == 0) {
            rlDrawRenderBatchActive();
            const char* ext = GetFileExtension(shot);
            int stem = ext ? (int)(ext - shot) : (int)TextLength(shot);
            TakeScreenshot(TextFormat("%.*s_%04d.png", stem, shot, frame));
        }
        const char* captureLabel = vehicleFixture ? tests.CaptureLabel() : trafficFixture ? trafficTests.CaptureLabel()
            : clearanceFixture ? clearanceTests.CaptureLabel() : conflictFixture ? conflictTests.CaptureLabel()
            : incidentFixture ? incidentTests.CaptureLabel() : turnFixture ? turnTests.CaptureLabel() : nullptr;
        if (captureLabel) {
            rlDrawRenderBatchActive();
            const char* ext = GetFileExtension(shot);
            int stem = ext ? (int)(ext - shot) : (int)TextLength(shot);
            TakeScreenshot(TextFormat("%.*s_%s.png", stem, shot, captureLabel));
            if (vehicleFixture) tests.ClearCaptureRequest();
            else if (trafficFixture) trafficTests.ClearCaptureRequest();
            else if (clearanceFixture) clearanceTests.ClearCaptureRequest();
            else if (conflictFixture) conflictTests.ClearCaptureRequest();
            else if (incidentFixture) incidentTests.ClearCaptureRequest();
            else turnTests.ClearCaptureRequest();
        }
        EndDrawing();
        if (lastShot) break;
        if (shot && frame > 30) { static double acc = 0; static int n = 0; acc += GetFrameTime(); n++;
            if (frame == shotFrames - 1) TraceLog(LOG_INFO, "SHOT: average %.1f FPS over %d frames", n / acc, n); }
    }
    game.Unload();
    gAssets.Unload();
    CloseWindow();
    if (vehicleFixture) return (!tests.Finished() || tests.Failed()) ? 1 : 0;
    if (trafficFixture) return (!trafficTests.Finished() || trafficTests.Failed()) ? 1 : 0;
    if (clearanceFixture) return (!clearanceTests.Finished() || clearanceTests.Failed()) ? 1 : 0;
    if (conflictFixture) return (!conflictTests.Finished() || conflictTests.Failed()) ? 1 : 0;
    if (incidentFixture) return (!incidentTests.Finished() || incidentTests.Failed()) ? 1 : 0;
    if (turnFixture) return (!turnTests.Finished() || turnTests.Failed()) ? 1 : 0;
    return 0;
}
