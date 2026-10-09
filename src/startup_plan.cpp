// =====================================================================================
//  Metadata-only startup budgets and optional observed unit-cost history (CJ-030).
//  Discovery reads file sizes/content definitions; it never loads resources or uses RNG.
// =====================================================================================
#include "startup_plan.h"
#include "config.h"
#include "datafile.h"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace {
constexpr const char* PROFILE = "build/cache/startup-work-profile.cfg";
struct Estimate { double units = 1, milliseconds = 1; };
std::map<std::string, double> discoveredUnits;
struct DiscoveryCancelled {};

void DiscoveryPulse(startup::Reporter* loading, const char* detail = nullptr) {
    if (loading && !loading->Pulse(detail)) throw DiscoveryCancelled{};
}

void DiscoveryBatch(startup::Reporter* loading, size_t completed,
                    std::chrono::steady_clock::time_point& lastPulse) {
    auto now = std::chrono::steady_clock::now();
    if (completed % 16 == 0 || now - lastPulse >= std::chrono::milliseconds(25)) {
        DiscoveryPulse(loading);
        lastPulse = std::chrono::steady_clock::now();
    }
}

double ImageWork(const char* directory, const char* detail, startup::Reporter* loading) {
    DiscoveryPulse(loading, detail);
    std::error_code error;
    std::filesystem::recursive_directory_iterator it, end;
    {
        startup::AtomicSpan atomic(loading, "discovery", "Open image directory");
        it = std::filesystem::recursive_directory_iterator(directory, error);
    }
    double work = 0;
    size_t visited = 0;
    auto lastPulse = std::chrono::steady_clock::now();
    while (!error && it != end) {
        bool regular = false;
        {
            startup::AtomicSpan atomic(loading, "discovery", "Read image entry metadata");
            regular = it->is_regular_file(error);
        }
        if (regular && !error && it->path().extension() == ".png") {
            uintmax_t bytes = 0;
            {
                startup::AtomicSpan atomic(loading, "discovery", "Read PNG file size");
                bytes = it->file_size(error);
            }
            if (!error) work += 1.0 + (double)bytes / (1024.0 * 1024.0);
        }
        {
            startup::AtomicSpan atomic(loading, "discovery", "Advance image directory");
            it.increment(error);
        }
        DiscoveryBatch(loading, ++visited, lastPulse);
    }
    DiscoveryPulse(loading);
    return std::max(1.0, work);
}
std::vector<DataRecord> Metadata(const char* path, const char* detail, startup::Reporter* loading) {
    // The unit estimate is separate from each loader's authoritative parsed execution plan.
    DiscoveryPulse(loading, detail);
    std::vector<DataRecord> records;
    {
        startup::AtomicSpan atomic(loading, "discovery", path);
        if (FileExists(path)) records = ReadDataFile(path);
    }
    DiscoveryPulse(loading);
    return records;
}
std::map<std::string, double> WorkUnits(startup::Reporter* loading) {
    std::map<std::string, double> units;
    auto vehicles = Metadata("assets/data/vehicles.cfg", "Reading vehicle definitions", loading);
    auto civilians = Metadata("assets/data/civilians.cfg", "Reading civilian definitions", loading);
    auto characters = Metadata("assets/data/characters.cfg", "Reading character definitions", loading);
    auto foliage = Metadata("assets/data/foliage.cfg", "Reading foliage definitions", loading);
    double sprites = 0, animationFrames = 0;
    size_t visited = 0;
    auto lastPulse = std::chrono::steady_clock::now();
    for (const DataRecord& r : vehicles) {
        if (r.Is("SPRITE")) sprites += 1 + std::clamp(r.I(3), 0, 3);
        else if (r.Is("DERIVE")) sprites += std::max<size_t>(1, r.size() > 7 ? r.size() - 7 : 0);
        else if (r.Is("COMPOSE")) sprites += std::max<size_t>(1, r.size() > 9 ? r.size() - 9 : 0);
        else if (r.Is("GEN")) sprites += std::max<size_t>(1, r.size() > 3 ? r.size() - 3 : 0);
        else if (r.Is("BIKE")) sprites += 1;
        DiscoveryBatch(loading, ++visited, lastPulse);
    }
    for (const DataRecord& r : characters) {
        if (r.Is("ANIM")) animationFrames += std::max(0, r.I(3));
        else if (r.Is("FEET")) animationFrames += std::max(0, r.I(2));
        DiscoveryBatch(loading, ++visited, lastPulse);
    }
    units["assets.materials"] = ImageWork("assets/textures", "Inspecting ground textures", loading);
    units["assets.vehicles"] = ImageWork("assets/vehicles", "Inspecting vehicle textures", loading) + std::max(1.0, sprites);
    units["assets.civilians"] = ImageWork("assets/characters/civilians", "Inspecting civilian animations", loading) + civilians.size();
    // Declared frame counts cover the animation work without revisiting hundreds
    // of PNG entries, including pictures that the content definitions never use.
    units["assets.animations"] = std::max(1.0, animationFrames);
    units["assets.foliage"] = ImageWork("assets/foliage", "Inspecting foliage textures", loading) + foliage.size();
    units["assets.effects"] = 1;
    units["assets.fonts"] = 2;
    units["assets.shaders"] = 2;
    units["traffic.turns"] = std::max(1.0, sprites);
    units["world.city"] = (double)cfg::MAP_W * cfg::MAP_H + cfg::BLOCKS_X * cfg::BLOCKS_Y;
    units["world.minimap"] = (double)cfg::MAP_W * cfg::MAP_H;
    units["world.renderer"] = (double)std::max(1, GetScreenWidth()) * std::max(1, GetScreenHeight());
    units["world.audio"] = 1;
    units["world.population"] = cfg::PARKED_CARS + cfg::TRAFFIC_CARS + cfg::PEDESTRIANS;
    units["startup.ready"] = 1;
    return units;
}

std::map<std::string, Estimate> ReadHistory(startup::Reporter* loading) {
    DiscoveryPulse(loading, "Reading previous startup costs");
    std::map<std::string, Estimate> history;
    constexpr uintmax_t MAX_PROFILE_BYTES = 64 * 1024;
    std::error_code error;
    uintmax_t bytes = 0;
    {
        startup::AtomicSpan atomic(loading, "discovery", "Read startup profile size");
        bytes = std::filesystem::file_size(PROFILE, error);
    }
    DiscoveryPulse(loading);
    // Timing history is optional. Bounded input prevents an accidentally huge cache
    // from becoming mandatory work; the read remains bounded if the file grows.
    if (error || bytes > MAX_PROFILE_BYTES) return history;
    std::ifstream input;
    {
        startup::AtomicSpan atomic(loading, "discovery", "Open startup profile");
        input.open(PROFILE);
    }
    DiscoveryPulse(loading);
    std::string text((size_t)MAX_PROFILE_BYTES + 1, '\0');
    {
        startup::AtomicSpan atomic(loading, "discovery", "Read startup profile");
        input.read(&text[0], (std::streamsize)text.size());
    }
    size_t read = (size_t)input.gcount();
    DiscoveryPulse(loading);
    if (read > MAX_PROFILE_BYTES) return history;
    text.resize(read);
    std::istringstream records(text);
    std::string id;
    double units = 0, milliseconds = 0;
    size_t visited = 0;
    auto lastPulse = std::chrono::steady_clock::now();
    while (records >> id >> units >> milliseconds) {
        if (std::isfinite(units) && units > 0 && std::isfinite(milliseconds) && milliseconds > 0)
            history[id] = { units, milliseconds };
        DiscoveryBatch(loading, ++visited, lastPulse);
    }
    DiscoveryPulse(loading);
    return history;
}
}

std::vector<startup::TaskSpec> DiscoverStartupPlan(startup::Reporter* loading) {
    discoveredUnits.clear();
    try {
        DiscoveryPulse(loading, "Inspecting game content");
        auto units = WorkUnits(loading);
        auto history = ReadHistory(loading);
        using startup::TaskSpec;
        std::vector<TaskSpec> plan = {
            { "assets.materials", "Ground materials", "Loading ground materials...", "Roads, pavements and facades", 150 },
            { "assets.vehicles", "Vehicles", "Loading vehicle textures...", "Cars, vans and motorbikes", 300, true, { "assets.materials" } },
            { "assets.civilians", "Civilians", "Loading civilian animations...", "Walking, running and actions", 1500, true, { "assets.vehicles" } },
            { "assets.animations", "Character animations", "Loading character animations...", "Player, feet and equipment", 5500, true, { "assets.civilians" } },
            { "assets.foliage", "Foliage", "Preparing foliage...", "Trees and bushes", 250, true, { "assets.animations" } },
            { "assets.effects", "City objects and effects", "Preparing city objects...", "Street furniture and effects", 200, true, { "assets.foliage" } },
            { "assets.fonts", "Interface fonts", "Preparing the interface...", "Game fonts", 30, false, { "assets.effects" } },
            { "assets.shaders", "Graphics shaders", "Preparing graphics...", "Lighting and post-processing", 20, true, { "assets.fonts" } },
            { "traffic.turns", "Traffic routes", "Planning traffic routes...", "Vehicle turn paths", 900, true, { "assets.shaders" } },
            { "world.city", "City", "Building the city...", "Blocks, streets and spatial indices", 50, true, { "traffic.turns" } },
            { "world.minimap", "Map preview", "Preparing the map preview...", "Drawing the city map", 40, true, { "world.city" } },
            { "world.renderer", "Graphics buffers", "Preparing graphics buffers...", "Scene, lights and bloom", 20, true, { "world.minimap" } },
            { "world.audio", "Audio", "Preparing audio...", "Sound effects and ambience", 300, false, { "world.renderer" } },
            { "world.population", "World population", "Populating the city...", "Traffic, people and mission points", 50, true, { "world.audio" } },
            { "startup.ready", "Final preparation", "Final preparation...", "Checking the initialized world", 2, true, { "world.population" } }
        };
        // First-run estimates are unit costs too, so changed content adapts immediately.
        // Successful measurements replace these coarse costs on subsequent starts.
        const std::map<std::string, double> initialUnitMs = {
            { "assets.materials", 12 }, { "assets.vehicles", 4 }, { "assets.civilians", 20 },
            { "assets.animations", 9 }, { "assets.foliage", 15 }, { "assets.effects", 200 },
            { "assets.fonts", 15 }, { "assets.shaders", 10 }, { "traffic.turns", 12 },
            { "world.city", 0.002 }, { "world.minimap", 0.002 }, { "world.renderer", 0.000014 },
            { "world.audio", 300 }, { "world.population", 0.12 }, { "startup.ready", 2 }
        };
        for (TaskSpec& task : plan) {
            task.weight = std::max(0.25, initialUnitMs.at(task.id) * units.at(task.id));
            auto previous = history.find(task.id);
            if (previous != history.end()) {
                double estimate = previous->second.milliseconds * units[task.id] / previous->second.units;
                // Extreme or unusable cache values must not swallow the whole visible budget.
                if (std::isfinite(estimate)) task.weight = std::clamp(estimate, 0.25, 120000.0);
            }
        }
        DiscoveryPulse(loading, "Preparing the startup work plan");
        discoveredUnits = std::move(units);
        TraceLog(LOG_INFO, "STARTUP plan tasks=%d timing-history=%d metadata-only", (int)plan.size(), (int)history.size());
        return plan;
    } catch (const DiscoveryCancelled&) {
        return {};
    }
}

void SaveStartupProfile(const startup::Reporter& reporter) {
    if (reporter.View().state != startup::State::Ready) return;
    std::error_code error;
    std::filesystem::create_directories("build/cache", error);
    if (error) return;                   // optional history must not prevent startup in read-only installs
    std::ofstream output("build/cache/startup-work-profile.tmp", std::ios::trunc);
    if (!output) return;
    output.precision(12);
    for (const auto& t : reporter.Timings()) {
        auto units = discoveredUnits.find(t.id);
        if (units != discoveredUnits.end() && t.milliseconds > 0)
            output << t.id << ' ' << units->second << ' ' << t.milliseconds << '\n';
    }
    output.close();
    if (!output) return;
    // The profile is cosmetic timing history, never a readiness cache or a scheduler.
    std::filesystem::rename("build/cache/startup-work-profile.tmp", PROFILE, error);
    if (error) {
        std::ofstream direct(PROFILE, std::ios::trunc);
        direct.precision(12);
        for (const auto& t : reporter.Timings()) {
            auto units = discoveredUnits.find(t.id);
            if (units != discoveredUnits.end() && t.milliseconds > 0)
                direct << t.id << ' ' << units->second << ' ' << t.milliseconds << '\n';
        }
    }
}
