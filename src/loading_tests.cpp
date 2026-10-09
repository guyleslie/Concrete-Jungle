// =====================================================================================
//  CJ-030 deterministic presentation evidence: real frozen plans, committed mock
//  records, five viewport sizes and exceptional states. Captures never advance work.
// =====================================================================================
#include "loading_tests.h"
#include "loading_screen.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct FixtureCase {
    std::string name;
    startup::Snapshot snapshot;
    int tasks = 0, errors = 0;
};

void Require(bool condition, int& errors, const char* message) {
    if (!condition) {
        errors++;
        TraceLog(LOG_ERROR, "LOADING FIXTURE: %s", message);
    }
}

startup::TaskSpec Work(const std::string& id, const std::string& headline,
                       const std::string& label, double weight = 10) {
    return { id, label, headline, "Preparing deterministic fixture records", weight, true, {} };
}

void Gate(startup::Reporter& reporter, int& errors, const std::vector<std::string>& dependencies) {
    auto task = Work("startup.ready", "Preparing the title screen...", "Startup checks", 0.01);
    task.dependencies = dependencies;
    Require(reporter.Register(task), errors, "register readiness gate");
    Require(reporter.Freeze(), errors, "freeze fixture work plan");
}

// A record is committed before its completion is reported. This private arithmetic
// has no connection to the game's shared RNG, registries, actor serials or content.
void Commit(startup::Reporter& reporter, std::vector<uint32_t>& records, int completed, int& errors) {
    records.push_back(static_cast<uint32_t>(records.size() + 1) * 2654435761u);
    Require(reporter.Progress(completed), errors, "report committed fixture record");
}

FixtureCase Plan(int count) {
    FixtureCase result{ "plan_" + std::to_string(count), {}, count, 0 };
    startup::Reporter reporter;
    std::vector<std::string> ids;
    for (int index = 0; index < count - 1; index++) {
        std::string id = "fixture.group." + std::to_string(index);
        auto task = Work(id, "Loading vehicle textures...", "Fixture group " + std::to_string(index + 1));
        if (!ids.empty()) task.dependencies.push_back(ids.back());
        Require(reporter.Register(task), result.errors, "register expandable fixture group");
        ids.push_back(id);
    }
    Gate(reporter, result.errors, ids);
    std::vector<uint32_t> records;
    int active = (count - 1) * 2 / 3;
    for (int group = 0; group <= active; group++) {
        Require(reporter.Begin(ids[group].c_str(), "Fixture data: vehicle records", 10, "records"),
                result.errors, "begin expandable fixture group");
        int target = group == active ? 7 : 10;
        for (int item = 1; item <= target; item++) Commit(reporter, records, item, result.errors);
        if (group != active) Require(reporter.Finish(), result.errors, "finish committed fixture group");
    }
    result.snapshot = reporter.View();
    Require(reporter.TaskCount() == static_cast<size_t>(count), result.errors, "actual plan task count");
    Require(result.snapshot.countText == "7 / 10 records", result.errors, "latest item count follows committed work");
    Require(result.snapshot.determinate && result.snapshot.Percentage() < 100, result.errors, "partial plan is determinate below 100%");
    return result;
}

FixtureCase LongText() {
    FixtureCase result{ "long_utf8", {}, 2, 0 };
    startup::Reporter reporter;
    std::string longName;
    for (int repeat = 0; repeat < 10; repeat++)
        longName += u8"\u0150r\u00fclt v\u00e1ros \u2014 \u0171j anim\u00e1ci\u00f3k \u4e2d\u6587 \U0001F699 ";
    Require(reporter.Register(Work("fixture.long", longName, longName)), result.errors, "register Unicode fixture");
    Gate(reporter, result.errors, { "fixture.long" });
    std::string path = "assets/characters/" + longName + "/walking_animation_atlas_with_a_long_descriptive_name.png";
    Require(reporter.Begin("fixture.long", path.c_str(), 10000, "records"), result.errors, "begin large real fixture total");
    std::vector<uint32_t> records;
    for (int item = 1; item <= 9999; item++) Commit(reporter, records, item, result.errors);
    result.snapshot = reporter.View();
    Require(records.size() == 9999 && result.snapshot.countText == "9999 / 10000 records", result.errors, "large count has real committed records");
    return result;
}

std::vector<FixtureCase> States() {
    std::vector<FixtureCase> results;
    startup::Reporter initial;
    results.push_back({ "bootstrap", initial.View(), 0, 0 });
    int errors = 0;
    Require(initial.Register(Work("fixture.discovery", "Finding content...", "Content definitions")), errors, "register discovery fixture");
    results.push_back({ "discovery", initial.View(), 1, errors });

    // Unknown work retains earlier confirmed progress but displays no percentage.
    startup::Reporter unknown;
    errors = 0;
    Require(unknown.Register(Work("fixture.materials", "Loading ground materials...", "Ground materials")), errors, "register known prior work");
    Require(unknown.Register(Work("fixture.city", "Building the city...", "City")), errors, "register unknown parent");
    Gate(unknown, errors, { "fixture.materials", "fixture.city" });
    Require(unknown.Begin("fixture.materials", nullptr, 2, "records"), errors, "begin prior work");
    std::vector<uint32_t> records;
    Commit(unknown, records, 1, errors); Commit(unknown, records, 2, errors);
    Require(unknown.Finish(), errors, "finish prior work");
    Require(unknown.Begin("fixture.city", "Discovering city generation batches"), errors, "begin unmeasurable work");
    Require(!unknown.View().determinate && unknown.View().fraction > 0, errors, "unknown work hides percentage and retains prior progress");
    results.push_back({ "unknown", unknown.View(), 3, errors });
    Require(unknown.PlanChildren({ { "streets", 1, true }, { "blocks", 4, true }, { "indices", 1, true } }), errors, "freeze discovered child plan");
    records.push_back(1); Require(unknown.ChildDone("streets"), errors, "commit known street batch");
    records.push_back(2); records.push_back(3);
    Require(unknown.ChildProgress("blocks", 0.5), errors, "commit two of four block batches");
    Require(unknown.Count(1, 3, "generation stages"), errors, "count fully completed known stage");
    Require(unknown.View().determinate && unknown.View().Percentage() < 100, errors, "discovery becomes measured without readiness");
    results.push_back({ "known_after_discovery", unknown.View(), 3, errors });

    startup::Reporter fallback;
    errors = 0;
    Require(fallback.Register(Work("fixture.fallback", "Preparing vehicle textures...", "Vehicle textures")), errors, "register fallback obligation");
    Gate(fallback, errors, { "fixture.fallback" });
    Require(fallback.Begin("fixture.fallback", "Generating a substitute for missing art"), errors, "begin fallback work");
    Require(fallback.PlanChildren({ { "sprite", 1, true } }), errors, "freeze fallback obligation");
    std::vector<unsigned char> substitute{ 240, 238, 232, 255 };
    Require(substitute.size() == 4 && fallback.ChildDone("sprite", startup::Outcome::ReadyWithFallback), errors, "commit usable substitute");
    Require(fallback.Finish(startup::Outcome::ReadyWithFallback, "Using a generated substitute for missing art"), errors, "finish valid fallback group");
    Require(fallback.Result("fixture.fallback") == startup::Outcome::ReadyWithFallback, errors, "fallback outcome is distinct from original art success");
    results.push_back({ "fallback", fallback.View(), 2, errors });

    for (bool cancelled : { false, true }) {
        startup::Reporter stopped;
        errors = 0;
        Require(stopped.Register(Work("fixture.graphics", "Preparing graphics buffers...", "Graphics buffers")), errors, "register interruption fixture");
        Gate(stopped, errors, { "fixture.graphics" });
        Require(stopped.Begin("fixture.graphics", nullptr, 10, "records"), errors, "begin interrupted work");
        records.clear();
        for (int item = 1; item <= 4; item++) Commit(stopped, records, item, errors);
        if (cancelled) stopped.Cancel();
        else Require(!stopped.Fail("A required graphics buffer could not be prepared"), errors, "mandatory failure stops startup");
        Require(stopped.Stopped() && !stopped.View().determinate && stopped.View().Percentage() < 100,
                errors, "interrupted startup has no completion or activity percentage");
        results.push_back({ cancelled ? "cancelled" : "failed", stopped.View(), 2, errors });
    }

    startup::Reporter ready;
    errors = 0;
    Require(ready.Register(Work("fixture.complete", "Preparing game content...", "Game content", 1000)), errors, "register readiness fixture");
    Gate(ready, errors, { "fixture.complete" });
    Require(ready.Begin("fixture.complete", nullptr, 2, "records"), errors, "begin completion fixture");
    records.clear(); Commit(ready, records, 1, errors); Commit(ready, records, 2, errors);
    Require(ready.Finish(), errors, "finish all fixture content");
    Require(ready.Begin("startup.ready", "Checking committed fixture resources", 1, "checks"), errors, "begin final validation last");
    Require(ready.View().Percentage() == 99 && ready.View().state != startup::State::Ready, errors, "unvalidated startup remains at 99%");
    results.push_back({ "ready_99", ready.View(), 2, errors });
    Require(records.size() == 2, errors, "validate all committed fixture resources");
    Commit(ready, records, 1, errors);
    Require(ready.Finish() && ready.Ready(), errors, "validated fixture enters readiness");
    Require(ready.View().state == startup::State::Ready && ready.View().Percentage() == 100, errors, "100% requires real readiness gate");
    results.push_back({ "ready_100", ready.View(), 2, errors });
    return results;
}

std::string Quote(const std::string& input) {
    std::string output = "\"";
    for (unsigned char value : input) {
        if (value == '"' || value == '\\') { output += '\\'; output += static_cast<char>(value); }
        else if (value < 32) {
            char escaped[8]; std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned int>(value));
            output += escaped;
        }
        else output += static_cast<char>(value);
    }
    return output + '"';
}

void Paint(LoadingScreen& screen, const startup::Snapshot& snapshot) {
    BeginDrawing(); ClearBackground({ 16, 23, 29, 255 });
    screen.Draw(snapshot); EndDrawing();
}

int Capture(LoadingScreen& screen, const FixtureCase& fixture, int requestedWidth, int requestedHeight,
            const std::string& directory, FILE* evidence) {
    int errors = fixture.errors;
    SetWindowSize(requestedWidth, requestedHeight);
    // Poll actual resize through normal drawing/event processing, never a wall wait.
    for (int attempt = 0; attempt < 8; attempt++) {
        Paint(screen, fixture.snapshot);
        if (GetScreenWidth() == requestedWidth && GetScreenHeight() == requestedHeight) break;
    }
    int windowWidth = GetScreenWidth(), windowHeight = GetScreenHeight();
    int nativeRenderWidth = GetRenderWidth(), nativeRenderHeight = GetRenderHeight();
    bool offscreen = windowWidth != requestedWidth || windowHeight != requestedHeight;
    RenderTexture2D target{};
    if (offscreen) {
        target = LoadRenderTexture(requestedWidth, requestedHeight);
        Require(target.id && target.texture.id && target.texture.width == requestedWidth
                && target.texture.height == requestedHeight, errors, "clamped window has valid requested-size QA framebuffer");
        TraceLog(LOG_INFO, "LOADING FIXTURE: window %dx%d clamped request %dx%d; using off-screen capture",
                 windowWidth, windowHeight, requestedWidth, requestedHeight);
    }
    int width = offscreen ? requestedWidth : windowWidth;
    int height = offscreen ? requestedHeight : windowHeight;
    LoadingLayout layout = LoadingScreen::Layout(width, height);
    Require(layout.status.x >= 0 && layout.status.y >= 0
            && layout.status.x + layout.status.width <= width + 0.1f
            && layout.status.y + layout.status.height <= height + 0.1f, errors, "status stays inside viewport");
    Require(layout.logo.x >= 0 && layout.logo.y >= 0
            && layout.logo.x + layout.logo.width <= width + 0.1f
            && layout.logo.y + layout.logo.height < layout.status.y, errors, "logo and status safe areas do not collide");
    float sizes[4]{ layout.headlineSize, layout.detailSize, layout.percentageSize, layout.recentSize };
    for (int row = 0; row < 4; row++) {
        Require(layout.rowHeight[row] >= sizes[row] && layout.rowY[row] >= layout.status.y
                && layout.rowY[row] + layout.rowHeight[row] <= layout.status.y + layout.status.height,
                errors, "measured row budget contains its text size");
        if (row) Require(layout.rowY[row] >= layout.rowY[row - 1] + layout.rowHeight[row - 1] + layout.gap - 0.1f,
                         errors, "fixed rows cannot overlap");
    }
    float available = layout.status.width - 2 * layout.paddingX;
    float separation = std::max(16.0f, 24 * layout.scale);
    Require(available * 0.45f > separation && available > 4 * layout.percentageSize + separation,
            errors, "bounded counter and percentage columns retain label/track space");
    Require(screen.GPUBytes() <= 16u * 1024u * 1024u, errors, "loading GPU resources remain within 16 MiB");
    std::string shot = directory + "/" + fixture.name + "_" + std::to_string(requestedWidth) + "x" + std::to_string(requestedHeight) + ".png";
    double drawMilliseconds = 0;
    int captureWidth = offscreen ? requestedWidth : nativeRenderWidth;
    int captureHeight = offscreen ? requestedHeight : nativeRenderHeight;
    size_t captureTargetBytes = target.texture.id
        ? static_cast<size_t>(GetPixelDataSize(target.texture.width, target.texture.height, target.texture.format)) : 0;
    if (offscreen && target.id && target.texture.id) {
        // This temporary QA target is separate from the runtime cosmetic GPU budget.
        // It exercises the same draw implementation at native requested pixels.
        BeginDrawing();
        BeginTextureMode(target); ClearBackground({ 16, 23, 29, 255 });
        double started = GetTime();
        screen.DrawAtSize(fixture.snapshot, requestedWidth, requestedHeight);
        drawMilliseconds = (GetTime() - started) * 1000;
        EndTextureMode(); EndDrawing();
        Image image = LoadImageFromTexture(target.texture);
        Require(image.data && image.width == requestedWidth && image.height == requestedHeight,
                errors, "QA framebuffer readback has exact requested pixels");
        if (image.data) {
            ImageFlipVertical(&image);
            Require(ExportImage(image, shot.c_str()), errors, "off-screen capture exported");
            UnloadImage(image);
        }
    }
    else if (!offscreen) {
        BeginDrawing(); ClearBackground({ 16, 23, 29, 255 });
        double started = GetTime();
        screen.Draw(fixture.snapshot);
        drawMilliseconds = (GetTime() - started) * 1000;
        rlDrawRenderBatchActive(); TakeScreenshot(shot.c_str()); EndDrawing();
    }
    if (target.id) UnloadRenderTexture(target);
    Require(FileExists(shot.c_str()), errors, "capture was written");
    if (evidence) {
        const auto& snapshot = fixture.snapshot;
        bool visiblePercentage = snapshot.determinate || snapshot.state == startup::State::Ready;
        std::string percentage = visiblePercentage ? std::to_string(snapshot.Percentage()) : "null";
        std::fprintf(evidence,
            "{\"case\":%s,\"requested_width\":%d,\"requested_height\":%d,\"width\":%d,\"height\":%d,"
            "\"window_width\":%d,\"window_height\":%d,\"render_width\":%d,\"render_height\":%d,"
            "\"capture_width\":%d,\"capture_height\":%d,\"capture_method\":%s,\"capture_target_color_bytes\":%llu,"
            "\"tasks\":%d,\"state\":%s,\"determinate\":%s,"
            "\"fraction\":%.12f,\"visible_percentage\":%s,\"count\":%s,\"headline\":%s,\"detail\":%s,"
            "\"recent\":%s,\"warning\":%s,\"error\":%s,\"gpu_bytes\":%llu,\"draw_enqueue_ms\":%.6f,"
            "\"status\":[%.3f,%.3f,%.3f,%.3f],\"rows\":[[%.3f,%.3f],[%.3f,%.3f],[%.3f,%.3f],[%.3f,%.3f]],"
            "\"column_geometry_ok\":%s,\"pixel_review\":\"required\",\"errors\":%d,\"screenshot\":%s}\n",
            Quote(fixture.name).c_str(), requestedWidth, requestedHeight, width, height, windowWidth, windowHeight,
            nativeRenderWidth, nativeRenderHeight, captureWidth, captureHeight, Quote(offscreen ? "offscreen" : "window").c_str(),
            static_cast<unsigned long long>(captureTargetBytes),
            fixture.tasks, Quote(startup::StateName(snapshot.state)).c_str(), snapshot.determinate ? "true" : "false",
            snapshot.fraction, percentage.c_str(), Quote(snapshot.countText).c_str(), Quote(snapshot.headline).c_str(),
            Quote(snapshot.detail).c_str(), Quote(snapshot.recent).c_str(), Quote(snapshot.warning).c_str(), Quote(snapshot.error).c_str(),
            static_cast<unsigned long long>(screen.GPUBytes()), drawMilliseconds,
            layout.status.x, layout.status.y, layout.status.width, layout.status.height,
            layout.rowY[0], layout.rowHeight[0], layout.rowY[1], layout.rowHeight[1], layout.rowY[2], layout.rowHeight[2],
            layout.rowY[3], layout.rowHeight[3], errors == fixture.errors ? "true" : "false", errors, Quote(shot).c_str());
    }
    return errors;
}

} // namespace

int RunLoadingPresentationTests(LoadingScreen& screen, const char* outputDirectory) {
    if (!IsWindowReady() || !outputDirectory || !*outputDirectory || outputDirectory[0] == '/'
        || outputDirectory[0] == '\\' || std::string(outputDirectory).find(':') != std::string::npos
        || std::string(outputDirectory).find("..") != std::string::npos) {
        TraceLog(LOG_ERROR, "LOADING FIXTURE: initialized window and relative output directory required");
        return 1;
    }
    std::string directory = outputDirectory;
    if (!DirectoryExists(directory.c_str()) && MakeDirectory(directory.c_str()) != 0) {
        TraceLog(LOG_ERROR, "LOADING FIXTURE: cannot create '%s'", outputDirectory);
        return 1;
    }
    std::string logPath = directory + "/cases.jsonl";
    FILE* evidence = std::fopen(logPath.c_str(), "wb");
    if (!evidence) return 1;
    int oldWidth = GetScreenWidth(), oldHeight = GetScreenHeight();
    Vector2 oldPosition = GetWindowPosition();
    int errors = 0, captures = 0;
    FixtureCase plan = Plan(25);
    const int resolutions[][2]{ { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 1920, 1200 },
                               { 2560, 1080 }, { 2560, 1440 }, { 3840, 2160 }, { 3440, 1440 } };
    for (const auto& resolution : resolutions) {
        if (WindowShouldClose()) { errors++; break; }
        errors += Capture(screen, plan, resolution[0], resolution[1], directory, evidence); captures++;
    }
    std::vector<FixtureCase> cases{ Plan(5), Plan(100), LongText() };
    for (auto& fixture : States()) cases.push_back(std::move(fixture));
    for (const auto& fixture : cases) {
        if (WindowShouldClose()) { errors++; break; }
        errors += Capture(screen, fixture, 1600, 900, directory, evidence); captures++;
    }
    std::fprintf(evidence, "{\"summary\":true,\"captures\":%d,\"errors\":%d}\n", captures, errors);
    std::fclose(evidence);
    SetWindowSize(oldWidth, oldHeight);
    SetWindowPosition(static_cast<int>(oldPosition.x), static_cast<int>(oldPosition.y));
    Paint(screen, startup::Snapshot{});
    TraceLog(LOG_INFO, "LOADING FIXTURE: captures=%d errors=%d evidence='%s'", captures, errors, logPath.c_str());
    return errors ? 1 : 0;
}
