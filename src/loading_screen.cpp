// =====================================================================================
//  Cosmetic startup drawing. The approved city PNG is untouched; its decoded texture
//  uses cover cropping. Wordmark and live text remain independent screen-space layers.
// =====================================================================================
#include "loading_screen.h"
#include "datafile.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace {

Color Opacity(Color color, float opacity) {
    color.a = static_cast<unsigned char>(std::lround(color.a * std::clamp(opacity, 0.0f, 1.0f)));
    return color;
}

std::string Join(const DataRecord& record, size_t first) {
    std::string value;
    for (size_t i = first; i < record.size(); i++) {
        if (!value.empty()) value += ' ';
        value += record[i];
    }
    return value;
}

bool Channel(const std::string& token, unsigned char& value) {
    char* end = nullptr;
    long parsed = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str() || *end || parsed < 0 || parsed > 255) return false;
    value = static_cast<unsigned char>(parsed);
    return true;
}

bool HasGlyph(Font font, int codepoint) {
    if (!font.glyphs) return false;
    for (int i = 0; i < font.glyphCount; i++) if (font.glyphs[i].value == codepoint) return true;
    return false;
}

// Validate bytes before touching a continuation byte, including truncated input.
int Decode(const std::string& input, size_t offset, size_t& bytes) {
    unsigned char lead = static_cast<unsigned char>(input[offset]);
    bytes = 1;
    if (lead < 0x80) return lead;
    size_t length = lead >= 0xc2 && lead <= 0xdf ? 2 : lead >= 0xe0 && lead <= 0xef ? 3
                  : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
    if (!length || offset + length > input.size()) return '?';
    int codepoint = lead & (length == 2 ? 0x1f : length == 3 ? 0x0f : 0x07);
    for (size_t i = 1; i < length; i++) {
        unsigned char next = static_cast<unsigned char>(input[offset + i]);
        if ((next & 0xc0) != 0x80) return '?';
        codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800)
        || (length == 4 && codepoint < 0x10000) || codepoint > 0x10ffff
        || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return '?';
    bytes = length;
    return codepoint;
}

std::string Normalize(const std::string& input, Font font) {
    std::string value;
    value.reserve(input.size());
    for (size_t offset = 0; offset < input.size();) {
        size_t bytes = 1;
        int codepoint = Decode(input, offset, bytes);
        offset += bytes;
        if (codepoint < 32 || codepoint == 127) codepoint = ' ';
        if (!HasGlyph(font, codepoint)) codepoint = '?';
        int encodedBytes = 0;
        const char* encoded = CodepointToUTF8(codepoint, &encodedBytes);
        value.append(encoded, static_cast<size_t>(encodedBytes));
    }
    return value;
}

float Spacing(float size) { return 0.6f * size / 22.0f; }

float Width(Font font, const std::string& value, float size) {
    return value.empty() ? 0 : MeasureTextEx(font, value.c_str(), size, Spacing(size)).x;
}

std::string Fit(Font font, const std::string& input, float size, float width) {
    if (width <= 0 || input.empty()) return {};
    std::string value = Normalize(input, font);
    if (Width(font, value, size) <= width) return value;
    std::string suffix = HasGlyph(font, 0x2026) ? "\xe2\x80\xa6" : "...";
    if (Width(font, suffix, size) > width) return {};
    std::vector<size_t> ends;
    for (size_t offset = 0; offset < value.size();) {
        size_t bytes = 1;
        Decode(value, offset, bytes);
        offset += bytes;
        ends.push_back(offset);
    }
    size_t low = 0, high = ends.size();
    while (low < high) {
        size_t middle = (low + high + 1) / 2;
        std::string candidate = value.substr(0, ends[middle - 1]) + suffix;
        if (Width(font, candidate, size) <= width) low = middle;
        else high = middle - 1;
    }
    return value.substr(0, low ? ends[low - 1] : 0) + suffix;
}

void Line(Font font, const std::string& value, float x, float rowY, float rowHeight,
          float size, Color color) {
    if (value.empty()) return;
    DrawTextEx(font, value.c_str(), { x, rowY + (rowHeight - size) * 0.5f }, size, Spacing(size), color);
}

std::string Override(const std::unordered_map<std::string, std::string>& values,
                     const std::string& id, const std::string& fallback) {
    auto found = values.find(id);
    return found == values.end() ? fallback : found->second;
}

Texture2D CosmeticTexture(const std::string& path, bool wordmark) {
    if (!FileExists(path.c_str())) {
        TraceLog(LOG_WARNING, "LOADING: optional image '%s' missing", path.c_str());
        return {};
    }
    Image image = LoadImage(path.c_str());
    if (!image.data) return {};
    // The packaged source stays unchanged. Two reference-display pixels per logo
    // pixel keep its upload small while retaining clean filtering at large monitors.
    if (wordmark && (image.width > 1280 || image.height > 720)) {
        float factor = std::min(1280.0f / image.width, 720.0f / image.height);
        ImageResize(&image, std::max(1, static_cast<int>(std::lround(image.width * factor))),
                    std::max(1, static_cast<int>(std::lround(image.height * factor))));
    }
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id) SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}

Font CosmeticFont(const std::string& path, int baseSize, bool& owned) {
    Font fallback = GetFontDefault();
    owned = false;
    if (!FileExists(path.c_str())) {
        TraceLog(LOG_WARNING, "LOADING: optional font '%s' missing", path.c_str());
        return fallback;
    }
    std::vector<int> codepoints;
    for (int codepoint = 32; codepoint <= 126; codepoint++) codepoints.push_back(codepoint);
    for (int codepoint = 160; codepoint <= 255; codepoint++) codepoints.push_back(codepoint);
    for (int codepoint : { 0x150, 0x151, 0x170, 0x171, 0x2026 }) codepoints.push_back(codepoint);
    Font font = LoadFontEx(path.c_str(), baseSize, codepoints.data(), static_cast<int>(codepoints.size()));
    owned = font.glyphs && font.glyphs != fallback.glyphs;
    if (!font.texture.id || !font.glyphs || font.glyphCount == 0) {
        if (owned) UnloadFont(font);
        owned = false;
        return fallback;
    }
    if (owned) SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    return font;
}

size_t TextureBytes(Texture2D texture) {
    if (!texture.id) return 0;
    size_t total = 0;
    int width = texture.width, height = texture.height;
    for (int level = 0; level < std::max(1, texture.mipmaps); level++) {
        total += static_cast<size_t>(std::max(0, GetPixelDataSize(width, height, texture.format)));
        width = std::max(1, width / 2);
        height = std::max(1, height / 2);
    }
    return total;
}

} // namespace

// ---- Bootstrap configuration and resource lifetime ----

void LoadingScreen::ReadConfig(const char* path) {
    for (const DataRecord& record : ReadDataFile(path)) {
        if (record.Is("BACKGROUND") && record.size() == 2) theme.background = record[1];
        else if (record.Is("WORDMARK") && record.size() == 2) theme.logo = record[1];
        else if (record.Is("FONT_BOLD") && record.size() == 2) theme.boldFont = record[1];
        else if (record.Is("FONT_SEMI") && record.size() == 2) theme.semiFont = record[1];
        else if (record.Is("HEADLINE") && record.size() >= 3) headlines[record[1]] = Join(record, 2);
        else if (record.Is("LABEL") && record.size() >= 3) labels[record[1]] = Join(record, 2);
        else if (record.Is("DETAIL") && record.size() >= 3) details[record[1]] = Join(record, 2);
        else if (record.Is("TEXT") && record.size() >= 3) text[record[1]] = Join(record, 2);
        else if (record.Is("COLOR") && record.size() == 6) {
            Color value{};
            if (!Channel(record[2], value.r) || !Channel(record[3], value.g)
                || !Channel(record[4], value.b) || !Channel(record[5], value.a)) {
                TraceLog(LOG_WARNING, "LOADING: invalid color at %s:%d; retaining default", path, record.line);
                continue;
            }
            if (record[1] == "base") theme.base = value;
            else if (record[1] == "primary") theme.primary = value;
            else if (record[1] == "secondary") theme.secondary = value;
            else if (record[1] == "accent") theme.accent = value;
            else if (record[1] == "track") theme.track = value;
            else if (record[1] == "fatal") theme.fatal = value;
            else TraceLog(LOG_WARNING, "LOADING: unknown color '%s' at %s:%d", record[1].c_str(), path, record.line);
        }
        else TraceLog(LOG_WARNING, "LOADING: ignored malformed/unknown record at %s:%d", path, record.line);
    }
}

void LoadingScreen::Load(const char* configPath) {
    Unload();
    theme = Theme{};
    headlines.clear(); labels.clear(); details.clear(); text.clear();
    ReadConfig(configPath);
    background = CosmeticTexture(theme.background, false);
    logo = CosmeticTexture(theme.logo, true);
    boldFont = CosmeticFont(theme.boldFont, 64, ownsBold);
    semiFont = CosmeticFont(theme.semiFont, 48, ownsSemi);
    TraceLog(LOG_INFO, "LOADING RESOURCES: gpu_bytes=%llu background=%dx%d logo=%dx%d",
             static_cast<unsigned long long>(GPUBytes()), background.width, background.height, logo.width, logo.height);
}

void LoadingScreen::Unload() {
    if (background.id) UnloadTexture(background);
    if (logo.id) UnloadTexture(logo);
    if (ownsBold) UnloadFont(boldFont);
    if (ownsSemi) UnloadFont(semiFont);
    background = {}; logo = {}; boldFont = {}; semiFont = {};
    ownsBold = false; ownsSemi = false;
}

size_t LoadingScreen::GPUBytes() const {
    return TextureBytes(background) + TextureBytes(logo)
         + (ownsBold ? TextureBytes(boldFont.texture) : 0)
         + (ownsSemi ? TextureBytes(semiFont.texture) : 0);
}

Font LoadingScreen::Face(bool bold) const {
    Font face = bold ? boldFont : semiFont;
    return face.texture.id && face.glyphs ? face : GetFontDefault();
}

std::string LoadingScreen::Text(const char* key, const char* fallback) const {
    auto found = text.find(key);
    return found == text.end() ? fallback : found->second;
}

startup::TaskSpec LoadingScreen::Describe(startup::TaskSpec task) const {
    task.headline = Override(headlines, task.id, task.headline);
    task.label = Override(labels, task.id, task.label);
    task.detail = Override(details, task.id, task.detail);
    return task;
}

// ---- Responsive composition ----

LoadingLayout LoadingScreen::Layout(int width, int height) {
    LoadingLayout layout;
    layout.scale = std::min(width / 1920.0f, height / 1080.0f);
    float scale = layout.scale;
    layout.status.width = 1728 * scale;
    layout.status.height = std::max(152.0f, 216 * scale);
    layout.status.x = (width - layout.status.width) * 0.5f;
    layout.status.y = height - 64 * scale - layout.status.height;
    layout.logo = { layout.status.x, 64 * scale, 640 * scale, 360 * scale };
    layout.paddingX = 24 * scale;
    layout.gap = std::max(8.0f, 12 * scale);
    layout.trackHeight = std::max(8.0f, 12 * scale);
    layout.headlineSize = layout.percentageSize = std::max(24.0f, 32 * scale);
    layout.detailSize = std::max(18.0f, 22 * scale);
    layout.recentSize = std::max(18.0f, 20 * scale);
    layout.rowHeight[0] = layout.rowHeight[2] = std::max(28.0f, 40 * scale);
    layout.rowHeight[1] = std::max(22.0f, 30 * scale);
    layout.rowHeight[3] = std::max(22.0f, 28 * scale);
    layout.rowY[0] = layout.status.y + std::max(12.0f, 12 * scale);
    for (int row = 1; row < 4; row++)
        layout.rowY[row] = layout.rowY[row - 1] + layout.rowHeight[row - 1] + layout.gap;
    return layout;
}

void LoadingScreen::Draw(const startup::Snapshot& snapshot, float opacity) const {
    DrawAtSize(snapshot, GetScreenWidth(), GetScreenHeight(), opacity);
}

void LoadingScreen::DrawAtSize(const startup::Snapshot& snapshot, int width, int height, float opacity) const {
    opacity = std::isfinite(opacity) ? std::clamp(opacity, 0.0f, 1.0f) : 0;
    if (opacity <= 0) return;
    if (width <= 0 || height <= 0) return;
    LoadingLayout layout = Layout(width, height);
    float scale = layout.scale;
    Font strong = Face(true), regular = Face(false);

    if (background.id && background.width > 0 && background.height > 0) {
        float factor = std::max(width / static_cast<float>(background.width), height / static_cast<float>(background.height));
        Rectangle destination{ (width - background.width * factor) * 0.5f,
                               (height - background.height * factor) * 0.5f,
                               background.width * factor, background.height * factor };
        DrawTexturePro(background, { 0, 0, static_cast<float>(background.width), static_cast<float>(background.height) },
                       destination, { 0, 0 }, 0, Opacity(WHITE, opacity));
    }
    else DrawRectangle(0, 0, width, height, Opacity(theme.base, opacity));

    // Fade into a near-opaque backing before the first status row, so even bright
    // headlights cannot change the text contrast. This is an integrated scrim.
    Color clear = theme.base; clear.a = 0;
    Color backing = theme.base; backing.a = 238;
    float gradientHeight = 240 * scale;
    DrawRectangleGradientV(0, static_cast<int>(std::floor(layout.status.y - gradientHeight)), width,
                           static_cast<int>(std::ceil(gradientHeight)), clear, Opacity(backing, opacity));
    DrawRectangle(0, static_cast<int>(std::floor(layout.status.y)), width,
                  height - static_cast<int>(std::floor(layout.status.y)), Opacity(backing, opacity));
    Color vignette = theme.base; vignette.a = 64;
    DrawRectangleGradientH(0, 0, static_cast<int>(width * 0.18f), height, Opacity(vignette, opacity), clear);
    DrawRectangleGradientH(static_cast<int>(width * 0.82f), 0, width - static_cast<int>(width * 0.82f),
                           height, clear, Opacity(vignette, opacity));

    if (logo.id && logo.width > 0 && logo.height > 0) {
        float factor = std::min(layout.logo.width / logo.width, layout.logo.height / logo.height);
        DrawTexturePro(logo, { 0, 0, static_cast<float>(logo.width), static_cast<float>(logo.height) },
                       { layout.logo.x, layout.logo.y, logo.width * factor, logo.height * factor },
                       { 0, 0 }, 0, Opacity(WHITE, opacity));
    }
    else {
        float size = 104 * scale;
        float measured = Width(strong, "CONCRETE", size);
        if (measured > layout.logo.width) size *= layout.logo.width / measured;
        Line(strong, "CONCRETE", layout.logo.x, layout.logo.y, size, size, Opacity(theme.primary, opacity));
        Line(strong, "JUNGLE", layout.logo.x, layout.logo.y + size + 12 * scale,
             size, size, Opacity(theme.accent, opacity));
    }

    bool failed = snapshot.state == startup::State::Failed;
    bool cancelled = snapshot.state == startup::State::Cancelled;
    bool ready = snapshot.state == startup::State::Ready;
    std::string headline = Override(headlines, snapshot.activeId, snapshot.headline);
    std::string detail = snapshot.detail.empty() ? Override(details, snapshot.activeId, "") : snapshot.detail;
    std::string recent;
    if (!snapshot.recent.empty()) recent = Text("recent", "Recently completed:") + " "
                                       + Override(labels, snapshot.recentId, snapshot.recent);
    if (!snapshot.warning.empty()) recent = snapshot.warning;
    if (failed) {
        headline = Text("failed", "Unable to start the game");
        detail = snapshot.error;
        recent = Text("exit", "Press Esc to exit");
    }
    else if (cancelled) {
        headline = Text("cancelled", "Closing game...");
        recent.clear();
    }
    else if (ready) {
        headline = Text("ready", "Ready");
        detail.clear();
    }
    else if (snapshot.state == startup::State::Bootstrapping && snapshot.activeId.empty()) {
        headline = Text("bootstrap", "Preparing startup...");
        detail = Text("bootstrap_detail", "Optional cosmetic resources");
    }
    else if (snapshot.state == startup::State::Discovering && snapshot.activeId.empty()) {
        headline = Text("discovery", "Finding game content...");
    }

    float left = layout.status.x + layout.paddingX;
    float right = layout.status.x + layout.status.width - layout.paddingX;
    float available = right - left;
    Color mainColor = Opacity(failed ? theme.fatal : theme.primary, opacity);
    Line(strong, Fit(strong, headline, layout.headlineSize, available), left,
         layout.rowY[0], layout.rowHeight[0], layout.headlineSize, mainColor);

    std::string count = failed || cancelled || ready ? ""
                      : Fit(regular, snapshot.countText, layout.detailSize, available * 0.55f);
    float countWidth = Width(regular, count, layout.detailSize);
    float separation = std::max(16.0f, 24 * scale);
    Line(regular, count, right - countWidth, layout.rowY[1], layout.rowHeight[1],
         layout.detailSize, Opacity(theme.primary, opacity));
    Line(regular, Fit(regular, detail, layout.detailSize, available - countWidth - (count.empty() ? 0 : separation)),
         left, layout.rowY[1], layout.rowHeight[1], layout.detailSize, Opacity(theme.secondary, opacity));

    float percentageWidth = Width(strong, "100%", layout.percentageSize);
    Rectangle track{ left, layout.rowY[2] + (layout.rowHeight[2] - layout.trackHeight) * 0.5f,
                     std::max(0.0f, available - percentageWidth - separation), layout.trackHeight };
    DrawRectangleRec(track, Opacity(theme.track, opacity));
    double fraction = std::isfinite(snapshot.fraction) ? snapshot.fraction : 0;
    fraction = ready ? 1 : std::clamp(fraction, 0.0, 0.99);
    // Unknown work keeps every already-confirmed pixel. Only real reporter
    // checkpoints can advance the static fill; the caption explains the next work.
    if (fraction > 0)
        DrawRectangleRec({ track.x, track.y, track.width * static_cast<float>(fraction), track.height },
                         Opacity(failed ? theme.fatal : theme.accent, opacity));
    if (snapshot.determinate || ready) {
        char percentage[16];
        // Match the reporter's tiny floating-point tolerance at integer checkpoints.
        std::snprintf(percentage, sizeof(percentage), "%d%%", static_cast<int>(std::floor(fraction * 100 + 1e-9)));
        std::string value = percentage;
        Line(strong, value, right - Width(strong, value, layout.percentageSize), layout.rowY[2], layout.rowHeight[2],
             layout.percentageSize, Opacity(failed ? theme.fatal : theme.accent, opacity));
    }
    Line(regular, Fit(regular, recent, layout.recentSize, available), left,
         layout.rowY[3], layout.rowHeight[3], layout.recentSize, Opacity(theme.secondary, opacity));
}
