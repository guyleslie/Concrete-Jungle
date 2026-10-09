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
#include "loading_screen.h"
#include "loading_tests.h"
#include "startup_plan.h"
#include "rlgl.h"
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

// Startup measurements inspect state without consuming randomness or advancing the world.
static void LogStartupDigest(const Game& game) {
    uint64_t hash = 14695981039346656037ull;
    auto add = [&](const auto& value) {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&value);
        for (size_t i = 0; i < sizeof(value); i++) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    };
    add(GRng().s);
    for (int y = 0; y < cfg::MAP_H; y++) for (int x = 0; x < cfg::MAP_W; x++) add(game.map.TileAtIdx(x, y));
    for (int y = 0; y < cfg::BLOCKS_Y; y++) for (int x = 0; x < cfg::BLOCKS_X; x++) add(game.map.Block(x, y));
    add(game.map.buildings.size()); add(game.map.objects.size()); add(game.map.parking.size());
    for (const Building& b : game.map.buildings) {
        add(b.r); add(b.base); add(b.height); add(b.facade); add(b.wallTint); add(b.roofTint);
        add(b.roofTex); add(b.litOffset); add(b.litAmount); add(b.special); add(b.beacon); add(b.neon);
        for (const RoofProp& p : b.props) { add(p.pos); add(p.rot); add(p.prop); add(p.size); }
    }
    for (const CityObject& o : game.map.objects) {
        add(o.kind); add(o.pos); add(o.rot); add(o.sprite); add(o.w); add(o.l); add(o.h); add(o.radius);
        add(o.breakable); add(o.alive); add(o.axis); add(o.head); add(o.strength); add(o.mass); add(o.soft); add(o.box);
    }
    for (const ParkingSpot& p : game.map.parking) { add(p.pos); add(p.angle); }
    for (const Vector2& p : game.map.rail.pts) add(p);
    for (float d : game.map.rail.dist) add(d);
    for (float d : game.map.trainPos) add(d);
    add(game.map.trainSpeed); add(game.map.policeStation); add(game.map.hospital); add(game.map.time);
    std::vector<int> indexed;
    for (int y = 0; y < cfg::MAP_H; y += 7) for (int x = 0; x < cfg::MAP_W; x += 7) {
        Rectangle area = { (float)(x * cfg::TILE), (float)(y * cfg::TILE), (float)cfg::TILE, (float)cfg::TILE };
        game.map.QueryBuildings(area, indexed); add(indexed.size()); for (int i : indexed) add(i);
        game.map.QueryObjects(area, indexed); add(indexed.size()); for (int i : indexed) add(i);
    }
    add(game.vehicles.size()); add(game.peds.size());
    for (const Vehicle& v : game.vehicles) {
        add(v.active); add(v.serial); add(v.skin); add(v.pos); add(v.vel); add(v.angle); add(v.health); add(v.driver);
        add(v.ai.rail); add(v.ai.s); add(v.ai.speed); add(v.ai.cruise); add(v.ai.path.size());
        for (const Waypoint& p : v.ai.path) { add(p.p); add(p.cum); add(p.turn); add(p.vmax); add(p.stop); add(p.si); add(p.sj); }
    }
    for (const Pedestrian& p : game.peds) {
        add(p.active); add(p.serial); add(p.pos); add(p.vel); add(p.angle); add(p.skin); add(p.state);
        add(p.target); add(p.walkSpeed); add(p.health); add(p.timer); add(p.anim); add(p.reaction); add(p.courage);
    }
    add(game.player.pos); add(game.player.vel); add(game.player.aim); add(game.player.weapon); add(game.player.health);
    for (int a : game.player.ammo) add(a);
    for (int c : game.player.clip) add(c);
    for (char c : game.player.owned) add(c);
    for (const Pickup& p : game.pickups) { add(p.pos); add(p.kind); add(p.amount); add(p.active); add(p.respawn); }
    for (const Vector2& p : game.jobPhones) add(p);
    add(game.cam.pos); add(game.cam.viewH); add(game.cam.shakeOfs); add(game.cam.shake);
    add(game.dn.hour); add(game.time); add(game.state); add(game.money); add(game.heat);
    TraceLog(LOG_INFO, "STARTUP digest %016llx rng=%08x vehicles=%d pedestrians=%d buildings=%d objects=%d",
             (unsigned long long)hash, GRng().s, (int)game.vehicles.size(), (int)game.peds.size(),
             (int)game.map.buildings.size(), (int)game.map.objects.size());
}

static void DrainStartupInput() {
    while (GetKeyPressed() != 0) {}
    while (GetCharPressed() != 0) {}
}

int main(int argc, char** argv) {
    // --shot <file.png> [--frames N] [--every N] [--scenario foot|drive|night|nightdrive|chase|day|crash|derby|rampage|bikes|...]
    // (file names are relative to the working directory; --every also saves <file>_<frame>.png)
    const char* shot = nullptr; const char* scenario = "foot"; int shotFrames = 180, every = 0;
    const char* testVehicle = "Taxi";
    bool uncapped = false;
    const char* loadingTest = nullptr;
    const char* loadingCapture = nullptr;
    const char* loadingConfig = "assets/data/loading.cfg";
    const char* loadingStop = nullptr;
    const char* loadingFail = nullptr;
    for (int i = 1; i < argc; i++) {
        if (TextIsEqual(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (TextIsEqual(argv[i], "--frames") && i + 1 < argc) shotFrames = TextToInteger(argv[++i]);
        else if (TextIsEqual(argv[i], "--every") && i + 1 < argc) every = TextToInteger(argv[++i]);
        else if (TextIsEqual(argv[i], "--scenario") && i + 1 < argc) scenario = argv[++i];
        else if (TextIsEqual(argv[i], "--vehicle") && i + 1 < argc) testVehicle = argv[++i];
        else if (TextIsEqual(argv[i], "--uncapped")) uncapped = true;
        // Measurement runs may read an alternative traffic data file (e.g. a baseline).
        else if (TextIsEqual(argv[i], "--traffic-config") && i + 1 < argc) gTrafficConfigPath = argv[++i];
        else if (TextIsEqual(argv[i], "--loading-test") && i + 1 < argc) loadingTest = argv[++i];
        else if (TextIsEqual(argv[i], "--loading-capture") && i + 1 < argc) loadingCapture = argv[++i];
        else if (TextIsEqual(argv[i], "--loading-config") && i + 1 < argc) loadingConfig = argv[++i];
        else if (TextIsEqual(argv[i], "--loading-stop") && i + 1 < argc) loadingStop = argv[++i];
        else if (TextIsEqual(argv[i], "--loading-fail") && i + 1 < argc) loadingFail = argv[++i];
    }
    bool loadingFixture = loadingTest || loadingStop || loadingFail;
    SetConfigFlags((((shot && uncapped) || loadingFixture) ? 0u : FLAG_VSYNC_HINT) | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(cfg::SCREEN_W, cfg::SCREEN_H, cfg::WINDOW_TITLE);
    if (!IsWindowReady()) return 2;
    SetExitKey(KEY_NULL);                 // Esc opens the pause menu instead of quitting
    if (!shot && !loadingFixture) {
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

    const double startupStart = GetTime();
    // Startup repaints are work checkpoints, so a swap must not add a vsync wait.
    // Restore the normal game setting once preparation has finished.
    const bool restoreVsync = !(shot && uncapped) && !loadingFixture;
    if (restoreVsync) ClearWindowState(FLAG_VSYNC_HINT);
    LoadingScreen loadingScreen;
    startup::Snapshot bootstrap;
    BeginDrawing(); loadingScreen.Draw(bootstrap); EndDrawing();
    loadingScreen.Load(loadingConfig);
    TraceLog(LOG_INFO, "STARTUP bootstrap %.3f ms loading-gpu-bytes=%llu", (GetTime() - startupStart) * 1000,
             (unsigned long long)loadingScreen.GPUBytes());
    if (loadingTest) {
        int result = RunLoadingPresentationTests(loadingScreen, loadingTest);
        loadingScreen.Unload(); CloseWindow(); return result;
    }
    std::ofstream snapshotLog;
    if (loadingCapture) {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(loadingCapture).parent_path(), error);
        snapshotLog.open(std::string(loadingCapture) + ".tsv");
        snapshotLog << "state\tactive\tpercent\tdeterminate\tcount\tdetail\trecent\n";
    }
    double lastPresentation = -1, lastBoundary = GetTime(), maxBoundaryGap = 0;
    double lastAtomicMs = 0, maxCooperativeMs = 0;
    double drawTotal = 0, drawMaximum = 0;
    int drawCount = 0, lastPercent = -1;
    std::string previousTask;
    std::string shownDetail;
    startup::State previousState = startup::State::Bootstrapping;
    double detailTime = -1;
    bool injected = false;
    startup::Reporter* activeReporter = nullptr;
    std::set<std::string> captures;
    auto present = [&](const startup::Snapshot& snapshot, bool force) {
        double now = GetTime();
        double boundaryMs = (now - lastBoundary) * 1000;
        double atomicMs = 0;
        if (activeReporter) for (const auto& timing : activeReporter->Atomics()) atomicMs += timing.totalMs;
        maxCooperativeMs = std::max(maxCooperativeMs, boundaryMs - std::max(0.0, atomicMs - lastAtomicMs));
        lastAtomicMs = atomicMs;
        maxBoundaryGap = std::max(maxBoundaryGap, now - lastBoundary); lastBoundary = now;
        if (WindowShouldClose()) return false;
        bool taskChanged = previousTask != snapshot.activeId || previousState != snapshot.state;
        if (taskChanged) {
            previousTask = snapshot.activeId;
            previousState = snapshot.state;
        }
        // Test-only stop injection occurs after confirmed work, exercising partial owners.
        if (!injected && snapshot.state == startup::State::Loading && activeReporter->CurrentTaskFraction() > 0 &&
            ((loadingStop && snapshot.activeId == loadingStop) || (loadingFail && snapshot.activeId == loadingFail))) {
            injected = true;
            if (loadingStop) return false;
            activeReporter->Fail("Injected required startup failure");
            return true;
        }
        if (!force && now - lastPresentation < 1.0 / 30.0) return true;
        lastPresentation = now;
        startup::Snapshot displayed = snapshot;
        if (taskChanged || now - detailTime >= 0.2) {
            shownDetail = snapshot.detail; detailTime = now;
        }
        displayed.detail = shownDetail;      // labels stay readable while counts and progress stay current
        BeginDrawing();
        double drawStart = GetTime(); loadingScreen.Draw(displayed);
        double drawMs = (GetTime() - drawStart) * 1000;
        drawTotal += drawMs; drawMaximum = std::max(drawMaximum, drawMs); drawCount++;
        std::string captureKey;
        if (loadingCapture) {
            if (snapshot.state == startup::State::Ready || snapshot.state == startup::State::Failed)
                captureKey = startup::StateName(snapshot.state);
            else if (snapshot.determinate && activeReporter->CurrentTaskFraction() > 0 && !snapshot.activeId.empty())
                captureKey = snapshot.activeId;
            if (!captureKey.empty() && captures.insert(captureKey).second) {
                rlDrawRenderBatchActive();
                startup::AtomicSpan capture(activeReporter, "capture", "Startup PNG export");
                TakeScreenshot((std::string(loadingCapture) + "-" + captureKey + ".png").c_str());
            }
        }
        EndDrawing();                     // services events at a safe graphics boundary
        DrainStartupInput();
        if (snapshotLog && (force || lastPercent != snapshot.Percentage())) {
            snapshotLog << startup::StateName(snapshot.state) << '\t' << snapshot.activeId << '\t' << snapshot.Percentage()
                        << '\t' << snapshot.determinate << '\t' << snapshot.countText << '\t' << snapshot.detail
                        << '\t' << snapshot.recent << '\n';
        }
        lastPercent = snapshot.Percentage();
        return !WindowShouldClose();
    };
    startup::Reporter reporter(present); activeReporter = &reporter;
    bool loaded = true;
    double discoveryStart = GetTime();
    loaded = reporter.Discover();
    for (startup::TaskSpec task : DiscoverStartupPlan(&reporter))
        if (!reporter.Register(loadingScreen.Describe(std::move(task)))) { loaded = false; break; }
    TraceLog(LOG_INFO, "STARTUP discovery %.3f ms", (GetTime() - discoveryStart) * 1000);
    if (loaded) loaded = reporter.Freeze();
    double startupPhase = GetTime(), matchedStart = startupPhase;
    if (loaded) loaded = gAssets.Load(&reporter);
    TraceLog(LOG_INFO, "STARTUP phase assets %.3f ms", (GetTime() - startupPhase) * 1000.0);
    startupPhase = GetTime();
    if (loaded) loaded = RailPlanTurns(&reporter);
    TraceLog(LOG_INFO, "STARTUP phase turns %.3f ms", (GetTime() - startupPhase) * 1000.0);
    startupPhase = GetTime();
    static Game game;                     // large object: keep it off the stack
    if (loaded) loaded = game.Init(&reporter);
    TraceLog(LOG_INFO, "STARTUP phase world %.3f ms", (GetTime() - startupPhase) * 1000.0);
    if (loaded) {
        std::string reason;
        loaded = reporter.Begin("startup.ready", nullptr, 1) && game.StartupReady(&reason);
        if (!loaded && !reporter.Stopped()) reporter.Fail(reason.empty() ? "Startup preparation failed" : reason.c_str());
        if (loaded) loaded = reporter.Progress(1) && reporter.Finish() && reporter.Ready();
    }
    if (!loaded && !reporter.Stopped()) reporter.Fail("Startup preparation did not complete its work plan");
    TraceLog(LOG_INFO, "STARTUP matched %.3f ms", (GetTime() - matchedStart) * 1000.0);
    TraceLog(LOG_INFO, "STARTUP total %.3f ms", (GetTime() - startupStart) * 1000.0);
    TraceLog(LOG_INFO, "STARTUP presentation draws=%d mean=%.3f ms max=%.3f ms boundary-gap=%.3f ms cooperative-gap=%.3f ms state=%s", drawCount,
             drawCount ? drawTotal / drawCount : 0, drawMaximum, maxBoundaryGap * 1000, maxCooperativeMs,
             startup::StateName(reporter.View().state));
    for (const auto& timing : reporter.Atomics())
        TraceLog(LOG_INFO, "STARTUP atomic %s calls=%llu total=%.3f ms max=%.3f ms operation=%s", timing.category.c_str(),
                 (unsigned long long)timing.calls, timing.totalMs, timing.maxMs, timing.slowest.c_str());
    if (!loaded) {
        if (!reporter.Cancelled() && !loadingFixture && !shot) {
            while (!WindowShouldClose() && !IsKeyPressed(KEY_ESCAPE)) {
                BeginDrawing(); loadingScreen.Draw(reporter.View()); EndDrawing();
            }
        }
        game.Unload(); gAssets.Unload(); loadingScreen.Unload();
        if (loadingFixture) {
            game.Unload(); gAssets.Unload(); loadingScreen.Unload();
            TraceLog(LOG_INFO, "STARTUP cleanup completed twice with graphics context alive");
        }
        CloseWindow();
        return reporter.Cancelled() ? 0 : 2;
    }
    if (!loadingCapture) SaveStartupProfile(reporter);
    if (restoreVsync) SetWindowState(FLAG_VSYNC_HINT);
    PollInputEvents();                    // clear pressed states as well as the queued keys
    DrainStartupInput();
    game.SuppressStartupKeys(IsKeyDown(KEY_ENTER), IsKeyDown(KEY_SPACE));
    LogStartupDigest(game);
    const double readyTime = GetTime();
    bool loadingOverlay = !shot;
    if (!loadingOverlay) loadingScreen.Unload();
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
        game.Unload(); gAssets.Unload(); loadingScreen.Unload(); CloseWindow(); return 2;
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
        if (loadingOverlay) {
            double fade = (GetTime() - readyTime) / 0.25;
            if (game.state != GameState::Title || fade >= 1) {
                loadingScreen.Unload(); loadingOverlay = false;
            } else loadingScreen.Draw(reporter.View(), (float)(1 - fade));
        }
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
    loadingScreen.Unload();
    CloseWindow();
    if (vehicleFixture) return (!tests.Finished() || tests.Failed()) ? 1 : 0;
    if (trafficFixture) return (!trafficTests.Finished() || trafficTests.Failed()) ? 1 : 0;
    if (clearanceFixture) return (!clearanceTests.Finished() || clearanceTests.Failed()) ? 1 : 0;
    if (conflictFixture) return (!conflictTests.Finished() || conflictTests.Failed()) ? 1 : 0;
    if (incidentFixture) return (!incidentTests.Finished() || incidentTests.Failed()) ? 1 : 0;
    if (turnFixture) return (!turnTests.Finished() || turnTests.Failed()) ? 1 : 0;
    return 0;
}
