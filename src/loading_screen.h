// =====================================================================================
//  Loading presentation: independent cosmetic resources and a fixed four-row status.
//  Draw consumes startup snapshots inside the caller's normal drawing pass. It never
//  advances work, services input, simulates the world or owns Begin/EndDrawing.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "startup_loading.h"
#include <cstddef>
#include <string>
#include <unordered_map>

struct LoadingLayout {
    Rectangle status{}, logo{};
    float rowY[4]{}, rowHeight[4]{};
    float scale = 1, paddingX = 24, gap = 12, trackHeight = 12;
    float headlineSize = 32, detailSize = 22, percentageSize = 32, recentSize = 20;
};

class LoadingScreen {
public:
    LoadingScreen() = default;
    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;

    // Cosmetic failures retain the primitive presentation; they do not fail startup.
    void Load(const char* configPath = "assets/data/loading.cfg");
    void Draw(const startup::Snapshot& snapshot, float opacity = 1.0f) const;
    // Same production drawing with explicit viewport dimensions for off-screen QA.
    void DrawAtSize(const startup::Snapshot& snapshot, int width, int height,
                    float opacity = 1.0f) const;
    void Unload();                       // call before closing the graphics context
    size_t GPUBytes() const;
    startup::TaskSpec Describe(startup::TaskSpec task) const;
    static LoadingLayout Layout(int width, int height);

private:
    struct Theme {
        std::string background = "assets/ui/loading-city.png";
        std::string logo = "assets/ui/loading-logo.png";
        std::string boldFont = "assets/fonts/Rajdhani-Bold.ttf";
        std::string semiFont = "assets/fonts/Rajdhani-SemiBold.ttf";
        Color base{ 16, 23, 29, 255 }, primary{ 240, 238, 232, 255 };
        Color secondary{ 184, 194, 204, 255 }, accent{ 255, 205, 70, 255 };
        Color track{ 53, 65, 76, 255 }, fatal{ 240, 142, 127, 255 };
    } theme;
    Texture2D background{}, logo{};
    Font boldFont{}, semiFont{};
    bool ownsBold = false, ownsSemi = false;
    std::unordered_map<std::string, std::string> headlines, labels, details, text;

    Font Face(bool bold) const;
    std::string Text(const char* key, const char* fallback) const;
    void ReadConfig(const char* path);
};
