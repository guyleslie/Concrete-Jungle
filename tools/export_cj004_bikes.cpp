// =====================================================================================
// CJ-004: deterministic packing of accepted bike artwork and runtime alpha-bound checks.
// Source-grid measurements are export inputs, never runtime pivot or scale overrides.
// =====================================================================================
#include "raylib.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static constexpr int CELL = 512;
static constexpr int ACTIVE_HEIGHT = 448;
static constexpr float ALPHA_THRESHOLD = 0.1f;

struct Bike {
    const char* cls;
    const char* file;
    float lengthM = 0;
    Rectangle source[2]{};
    Image image[2]{};
    Texture2D texture[2]{};
    int activeWidth = 0;
};

static float ClassLength(const char* name) {
    std::ifstream file("assets/data/vehicles.cfg");
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream record(line);
        std::string type, cls;
        float length;
        if (record >> type >> cls >> length && type == "CLASS" && cls == name) return length;
    }
    return 0;
}

static bool SameRect(Rectangle a, Rectangle b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static void WriteRect(std::ostream& out, Rectangle r) {
    out << "[" << r.x << ',' << r.y << ',' << r.width << ',' << r.height << ']';
}

int main(int argc, char** argv) {
    SetTraceLogLevel(LOG_WARNING);
    const bool exportFiles = argc == 2 && std::string(argv[1]) == "--export";
    const char* dir = "assets/vehicles";
    Bike bikes[3] = {{"Sportbike", "sportbike"}, {"Chopper", "chopper"}, {"Scooter", "scooter"}};
    Image sheet = LoadImage("assets/art/cj004/motorbike-sheet.png");
    if (!sheet.data || sheet.width != 3 * CELL || sheet.height != 2 * CELL) return 1;
    if (exportFiles && MakeDirectory(dir) < 0 && !DirectoryExists(dir)) return 2;
    std::ofstream report("assets/art/cj004/motorbike-export-check.json");
    report << "{\"runtime_integrated\":true,\"alpha_threshold\":" << ALPHA_THRESHOLD
           << ",\"canvas_px\":[512,512],\"alpha_centre_px\":[256,256],\"active_height_px\":448,\"pairs\":[";
    bool pass = true;
    for (int k = 0; k < 3; ++k) {
        Bike& b = bikes[k];
        b.lengthM = ClassLength(b.cls);
        if (!(b.lengthM > 0)) return 3;
        for (int row = 0; row < 2; ++row) {
            Image cell = ImageFromImage(sheet, {float(k * CELL), float(row * CELL), float(CELL), float(CELL)});
            b.source[row] = GetImageAlphaBorder(cell, ALPHA_THRESHOLD);
            if (b.source[row].width <= 0 || b.source[row].height <= 0) return 4;
            // An even width places every tight alpha-box centre at the same integer point.
            if (!row) b.activeWidth = 2 * int(std::round(ACTIVE_HEIGHT * b.source[0].width / b.source[0].height / 2));
            std::string path = std::string(dir) + '/' + b.file + (row ? "-ridden.png" : "-empty.png");
            if (exportFiles) {
                Image art = ImageFromImage(cell, b.source[row]);
                // Crop/registration resampling only; keep the accepted source unchanged.
                ImageResize(&art, b.activeWidth, ACTIVE_HEIGHT);
                Image packed = GenImageColor(CELL, CELL, BLANK);
                ImageDraw(&packed, art, {0, 0, float(art.width), float(art.height)},
                          {float((CELL - b.activeWidth) / 2), float((CELL - ACTIVE_HEIGHT) / 2), float(b.activeWidth), float(ACTIVE_HEIGHT)}, WHITE);
                if (!ExportImage(packed, path.c_str())) return 5;
                UnloadImage(art);
                UnloadImage(packed);
            }
            UnloadImage(cell);
            b.image[row] = LoadImage(path.c_str());
            if (!b.image[row].data) return 6;
            const Rectangle expected = {float((CELL - b.activeWidth) / 2), 32, float(b.activeWidth), 448};
            const Rectangle actual = GetImageAlphaBorder(b.image[row], ALPHA_THRESHOLD);
            const bool valid = b.image[row].width == CELL && b.image[row].height == CELL && SameRect(actual, expected);
            pass = pass && valid;
            printf("%s %s: alpha [%g,%g,%g,%g], centre [%g,%g], %s\n", b.cls, row ? "ridden" : "empty",
                   actual.x, actual.y, actual.width, actual.height, actual.x + actual.width / 2, actual.y + actual.height / 2, valid ? "PASS" : "FAIL");
        }
        if (k) report << ',';
        report << "{\"class\":\"" << b.cls << "\",\"length_m\":" << b.lengthM << ",\"source_empty_rect_px\":";
        WriteRect(report, b.source[0]);
        report << ",\"source_ridden_rect_px\":"; WriteRect(report, b.source[1]);
        report << ",\"export_empty_rect_px\":"; WriteRect(report, GetImageAlphaBorder(b.image[0], ALPHA_THRESHOLD));
        report << ",\"export_ridden_rect_px\":"; WriteRect(report, GetImageAlphaBorder(b.image[1], ALPHA_THRESHOLD));
        report << ",\"full_silhouette_width_m\":" << b.lengthM * b.activeWidth / ACTIVE_HEIGHT << '}';
    }
    UnloadImage(sheet);
    report << "],\"passed\":" << (pass ? "true" : "false") << "}\n";
    report.close();
    if (!pass) return 7;

    InitWindow(1200, 780, "CJ-004 - common vehicle export convention");
    SetTargetFPS(60);
    for (Bike& b : bikes) for (int row = 0; row < 2; ++row) {
        b.texture[row] = LoadTextureFromImage(b.image[row]);
        UnloadImage(b.image[row]);
    }
    const float pxPerM = 100;
    for (int frame = 0; frame < 180 && !WindowShouldClose(); ++frame) {
        BeginDrawing();
        ClearBackground({76, 79, 80, 255});
        DrawText("CJ-004 - exported PNGs, shared alpha centre and pair dimensions", 25, 15, 24, RAYWHITE);
        DrawText("Same drawing rule for all six files. Grid: 1 m. Bottom row alternates empty/ridden.", 25, 49, 18, RAYWHITE);
        for (int x = 0; x < 1200; x += 100) DrawLine(x, 85, x, 750, {95, 98, 99, 255});
        for (int y = 85; y < 750; y += 100) DrawLine(0, y, 1200, y, {95, 98, 99, 255});
        for (int k = 0; k < 3; ++k) {
            const Bike& b = bikes[k];
            const float cx = 250 + k * 360;
            const Rectangle src = {float((CELL - b.activeWidth) / 2), 32, float(b.activeWidth), 448};
            const float height = b.lengthM * pxPerM;
            const float width = height * src.width / src.height;
            DrawText(TextFormat("%s: %.1f m", b.cls, b.lengthM), int(cx - 120), 100, 22, RAYWHITE);
            for (int row = 0; row < 2; ++row) {
                const float cy = row ? 585 : 300;
                const int skin = row ? (frame / 30) % 2 : 0;
                DrawTexturePro(b.texture[skin], src, {cx, cy, width, height}, {width / 2, height / 2}, 0, WHITE);
                DrawLine(int(cx - 90), int(cy - height / 2), int(cx + 90), int(cy - height / 2), {210, 180, 90, 255});
                DrawLine(int(cx - 90), int(cy + height / 2), int(cx + 90), int(cy + height / 2), {210, 180, 90, 255});
                DrawCircleLines(int(cx), int(cy), 4, {255, 170, 40, 255});
            }
        }
        EndDrawing();
        if (frame == 100) TakeScreenshot("assets/art/cj004/motorbike-export-preview.png");
    }
    for (Bike& b : bikes) for (Texture2D tex : b.texture) UnloadTexture(tex);
    CloseWindow();
    return 0;
}
