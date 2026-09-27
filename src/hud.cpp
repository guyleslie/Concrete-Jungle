// =====================================================================================
//  HUD, title screen and pause menu (screen-space 2D, drawn after the 3D composite).
// =====================================================================================
#include "game.h"
#include <cstdio>

using namespace cfg;

static void Panel(Rectangle r, float alpha = 0.55f) {
    DrawRectangleRounded(r, 0.18f, 8, ColorA({ 10, 12, 16, 255 }, alpha));
    DrawRectangleRoundedLinesEx(r, 0.18f, 8, 1.5f, ColorA(WHITE, 0.12f));
}

static void Star(Vector2 c, float r, Color fill, Color outline) {
    Vector2 pts[12];
    pts[0] = c;
    for (int i = 0; i <= 10; i++) {
        float a = -PI / 2 + i * PI / 5;
        float rr = (i % 2 == 0) ? r : r * 0.45f;
        pts[i + 1] = { c.x + cosf(a) * rr, c.y + sinf(a) * rr };
    }
    // DrawTriangleFan wants counter-clockwise order on screen
    Vector2 rev[12]; rev[0] = c;
    for (int i = 1; i < 12; i++) rev[i] = pts[12 - i];
    DrawTriangleFan(rev, 12, fill);
    for (int i = 1; i < 11; i++) DrawLineEx(pts[i], pts[i + 1], 1.5f, outline);
}

// The game's own mouse pointer (the Windows cursor is hidden): a ring with four ticks,
// tighter and brighter while aiming.
static void Crosshair(Vector2 m, float ui, bool aiming) {
    float r = (aiming ? 9.0f : 12.0f) * ui, gap = r * 0.45f, len = 7.0f * ui;
    Color c = ColorA(WHITE, aiming ? 0.95f : 0.7f), sh = ColorA(BLACK, 0.55f);
    for (int pass = 0; pass < 2; pass++) {
        Color col = pass == 0 ? sh : c;
        float w = pass == 0 ? 3.5f * ui : 1.6f * ui;
        Vector2 o = pass == 0 ? V2(1, 1) : V2(0, 0);
        DrawRing(m + o, r - w * 0.5f, r + w * 0.5f, 0, 360, 32, col);
        Vector2 d[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (Vector2 v : d) DrawLineEx(m + o + v * (r + gap), m + o + v * (r + gap + len), w, col);
    }
    DrawCircleV(m, 1.6f * ui, c);
}

static void Bar(float x, float y, float w, float h, float v, Color c, const char* label) {
    DrawRectangleRounded({ x, y, w, h }, 0.5f, 6, ColorA(BLACK, 0.5f));
    if (v > 0) DrawRectangleRounded({ x + 2, y + 2, (w - 4) * Saturate(v), h - 4 }, 0.5f, 6, c);
    if (label) DrawUIText(label, x - MeasureUIText(label, h + 6, true) - 8, y - 4, h + 6, ColorA(WHITE, 0.8f), true);
}

void Game::DrawHUD() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float ui = Clampf(H / 900.0f, 0.75f, 1.6f);
    char buf[128];

    // ---- hurt vignette ----
    if (player.hurtFlash > 0) DrawRectangleGradientV(0, 0, (int)W, (int)H, ColorA({ 180, 0, 0, 255 }, 0.25f * player.hurtFlash), BLANK);

    // ---- top-right: money, wanted level, clock ----
    snprintf(buf, sizeof(buf), "$%08d", money);
    float ms = 52 * ui;
    DrawUITextShadow(buf, W - MeasureUIText(buf, ms) - 28 * ui, 18 * ui, ms, { 140, 230, 140, 255 });
    int stars = Stars();
    for (int i = 0; i < 6; i++) {
        Vector2 c = { W - 36 * ui - (5 - i) * 36 * ui, 92 * ui };
        bool on = i < stars;
        bool flash = on && heatCooldown < 1.0f && fmodf(time * 4, 1.0f) < 0.5f;
        Star(c, 14 * ui, on ? (flash ? Color{ 255, 255, 255, 255 } : Color{ 255, 205, 60, 255 }) : ColorA(BLACK, 0.35f), ColorA(BLACK, 0.7f));
    }
    dn.ClockString(buf, sizeof(buf));
    DrawUITextShadow(buf, W - MeasureUIText(buf, 28 * ui, true) - 28 * ui, 112 * ui, 28 * ui, ColorA(WHITE, 0.85f), true);

    // ---- top-left: health / armour, weapon ----
    Panel({ 20 * ui, 18 * ui, 330 * ui, player.inVehicle ? 70 * ui : 112 * ui });
    Bar(96 * ui, 30 * ui, 238 * ui, 16 * ui, player.health / 100.0f, { 220, 60, 60, 255 }, "HP");
    Bar(96 * ui, 56 * ui, 238 * ui, 16 * ui, player.armor / 100.0f, { 70, 140, 255, 255 }, "ARM");
    if (!player.inVehicle) {
        const WeaponDef& Wd = gAssets.weapons[player.weapon];
        if (Wd.kind == WeaponKind::Melee) snprintf(buf, sizeof(buf), "%s", Wd.name.c_str());
        else snprintf(buf, sizeof(buf), "%s   %d / %d%s", Wd.name.c_str(), player.clip[player.weapon], player.ammo[player.weapon],
                      player.reloadTimer > 0 ? "  RELOADING" : "");
        DrawUIText(buf, 36 * ui, 80 * ui, 28 * ui, { 255, 225, 150, 255 });
    }

    // ---- bottom-right: speedometer & vehicle condition ----
    if (player.inVehicle && player.vehicle >= 0) {
        const Vehicle& v = vehicles[player.vehicle];
        float kmh = fabsf(v.speedFwd) / PX_PER_METER * 3.6f;
        Rectangle pr = { W - 300 * ui, H - 150 * ui, 280 * ui, 130 * ui };
        Panel(pr);
        snprintf(buf, sizeof(buf), "%d", (int)kmh);
        DrawUITextShadow(buf, pr.x + 22 * ui, pr.y + 8 * ui, 76 * ui, WHITE);
        DrawUIText("km/h", pr.x + 30 * ui + MeasureUIText(buf, 76 * ui), pr.y + 46 * ui, 26 * ui, ColorA(WHITE, 0.7f), true);
        DrawUIText(v.S().name.c_str(), pr.x + 22 * ui, pr.y + 86 * ui, 24 * ui, { 200, 220, 255, 255 }, true);
        Bar(pr.x + 150 * ui, pr.y + 94 * ui, 110 * ui, 12 * ui, v.health / v.S().health, v.health < v.S().health * 0.3f ? Color{ 255, 90, 60, 255 } : Color{ 120, 220, 120, 255 }, nullptr);
        // rpm arc
        float a1 = -210 + 240 * Saturate(v.rpm);
        DrawRing({ pr.x + pr.width - 58 * ui, pr.y + 50 * ui }, 30 * ui, 36 * ui, -210, 30, 32, ColorA(WHITE, 0.15f));
        DrawRing({ pr.x + pr.width - 58 * ui, pr.y + 50 * ui }, 30 * ui, 36 * ui, -210, a1, 32, v.rpm > 0.85f ? Color{ 255, 90, 60, 255 } : Color{ 255, 205, 60, 255 });
    }

    // ---- bottom-left: radar ----
    float R = 250 * ui;
    Rectangle rr = { 24 * ui, H - R - 24 * ui, R, R };
    Panel({ rr.x - 6, rr.y - 6, rr.width + 12, rr.height + 12 }, 0.7f);
    const float span = 2200.0f;                        // world px across the radar
    Vector2 pp = PlayerPos();
    float s = map.minimapScale;
    Rectangle src = { (pp.x - span * 0.5f) * s, (pp.y - span * 0.5f) * s, span * s, span * s };
    float texH = (float)map.minimap.texture.height;
    BeginScissorMode((int)rr.x, (int)rr.y, (int)rr.width, (int)rr.height);
    DrawRectangleRec(rr, { 20, 45, 60, 255 });
    DrawTexturePro(map.minimap.texture, { src.x, texH - src.y - src.height, src.width, -src.height }, rr, { 0, 0 }, 0, WHITE);
    auto toRadar = [&](Vector2 w) { return Vector2{ rr.x + (w.x - pp.x) / span * R + R * 0.5f, rr.y + (w.y - pp.y) / span * R + R * 0.5f }; };
    for (const Vehicle& v : vehicles) {
        if (!v.active || v.driver != DriverType::Police || Dist(v.pos, pp) > span) continue;
        Color c = fmodf(time * 3, 1.0f) < 0.5f ? Color{ 255, 60, 60, 255 } : Color{ 60, 120, 255, 255 };
        DrawCircleV(toRadar(v.pos), 4 * ui, c);
    }
    for (const Pickup& k : pickups) if (k.active && Dist(k.pos, pp) < span) DrawCircleV(toRadar(k.pos), 2.5f * ui, k.kind == 0 ? GREEN : k.kind == 1 ? SKYBLUE : GOLD);
    EndScissorMode();
    // player arrow
    Vector2 pc = { rr.x + R * 0.5f, rr.y + R * 0.5f };
    float pa = player.inVehicle ? vehicles[player.vehicle].angle : player.aim;
    Vector2 tip = pc + Forward(pa) * (11 * ui), l = pc + Forward(pa + 2.5f) * (8 * ui), r = pc + Forward(pa - 2.5f) * (8 * ui);
    DrawTriangle(tip, r, l, WHITE); DrawTriangle(tip, l, r, WHITE);
    // mission / job blips clamp to the radar edge
    auto edgeBlip = [&](Vector2 w, Color c) {
        Vector2 p = toRadar(w);
        p.x = Clampf(p.x, rr.x + 6, rr.x + R - 6); p.y = Clampf(p.y, rr.y + 6, rr.y + R - 6);
        DrawCircleV(p, 6.5f * ui, BLACK); DrawCircleV(p, 5 * ui, c);
    };
    if (activePhone >= 0) edgeBlip(jobPhones[activePhone], { 255, 210, 60, 255 });
    Vector2 objective{}; bool hasObj = false;
    if (mission.type == MissionType::Courier || (mission.type == MissionType::Steal && mission.stage == 1)) { objective = mission.target; hasObj = true; }
    if ((mission.type == MissionType::Demolition || (mission.type == MissionType::Steal && mission.stage == 0)) && mission.targetVehicle >= 0) {
        objective = vehicles[mission.targetVehicle].pos; hasObj = true;
    }
    if (hasObj) edgeBlip(objective, { 255, 210, 60, 255 });
    edgeBlip(map.hospital, { 240, 240, 240, 255 });
    edgeBlip(map.policeStation, { 70, 110, 220, 255 });

    // ---- mission objective + timer, off-screen arrow ----
    if (mission.type != MissionType::None) {
        float y = 20 * ui;
        float tw = MeasureUIText(mission.objective.c_str(), 30 * ui, true);
        Panel({ W * 0.5f - tw * 0.5f - 20 * ui, y, tw + 40 * ui, 78 * ui });
        DrawUIText(mission.objective.c_str(), W * 0.5f - tw * 0.5f, y + 8 * ui, 30 * ui, { 255, 225, 150, 255 }, true);
        snprintf(buf, sizeof(buf), "%d:%02d", (int)mission.timer / 60, (int)mission.timer % 60);
        DrawUIText(buf, W * 0.5f - MeasureUIText(buf, 32 * ui) * 0.5f, y + 40 * ui, 32 * ui, mission.timer < 15 ? Color{ 255, 90, 90, 255 } : WHITE);
    }
    Vector2 arrowTarget{}; bool arrow = false;
    if (hasObj) { arrowTarget = objective; arrow = true; }
    else if (activePhone >= 0) { arrowTarget = jobPhones[activePhone]; arrow = true; }
    if (arrow && !OnScreen(arrowTarget, -80)) {
        Vector2 sp = cam.WorldToScreen(arrowTarget, 0);
        Vector2 c = { W * 0.5f, H * 0.5f };
        Vector2 d = Norm(sp - c);
        float m = 60 * ui;
        float t = std::min(fabsf((W * 0.5f - m) / (fabsf(d.x) + 1e-4f)), fabsf((H * 0.5f - m) / (fabsf(d.y) + 1e-4f)));
        Vector2 p = c + d * t;
        float a = AngleOf(d);
        Vector2 t0 = p + Forward(a) * (22 * ui), t1 = p + Forward(a + 2.4f) * (16 * ui), t2 = p + Forward(a - 2.4f) * (16 * ui);
        DrawTriangle(t0, t2, t1, { 255, 210, 60, 230 }); DrawTriangle(t0, t1, t2, { 255, 210, 60, 230 });
        snprintf(buf, sizeof(buf), "%dm", (int)(Dist(arrowTarget, PlayerPos()) / PX_PER_METER));
        DrawUITextShadow(buf, p.x - MeasureUIText(buf, 22 * ui, true) * 0.5f, p.y + 18 * ui, 22 * ui, { 255, 220, 120, 255 }, true);
    }

    // ---- nearby pickup / vehicle prompts ----
    if (!player.inVehicle) {
        for (const Pickup& k : pickups) {
            if (!k.active || Dist(k.pos, player.pos) > 7 * M) continue;
            Vector2 sp = cam.WorldToScreen(k.pos, 30);
            const char* name = k.kind == 0 ? "Health" : k.kind == 1 ? "Armor" : gAssets.weapons[k.kind - 2].name.c_str();
            DrawUITextShadow(name, sp.x - MeasureUIText(name, 20 * ui, true) * 0.5f, sp.y - 26 * ui, 20 * ui, { 255, 240, 200, 255 }, true);
        }
        for (const Vehicle& v : vehicles) {
            if (!v.Drivable() || Len(OBBClosestPoint(v.Box(), player.pos) - player.pos) > 3.8f * M) continue;
            Vector2 sp = cam.WorldToScreen(v.pos, v.height + 20);
            snprintf(buf, sizeof(buf), "[E] %s %s", v.driver == DriverType::Traffic || v.driver == DriverType::Police ? "Hijack" : "Enter", v.S().name.c_str());
            DrawUITextShadow(buf, sp.x - MeasureUIText(buf, 22 * ui, true) * 0.5f, sp.y - 30 * ui, 22 * ui, { 200, 230, 255, 255 }, true);
            break;
        }
    }

    // ---- toasts ----
    float ty = H - 190 * ui;
    for (int i = (int)toasts.size() - 1; i >= 0; i--) {
        const Toast& t = toasts[i];
        float a = Saturate(t.time / 0.6f);
        float tw = MeasureUIText(t.text.c_str(), 26 * ui, true);
        DrawUITextShadow(t.text.c_str(), W * 0.5f - tw * 0.5f, ty, 26 * ui, ColorA(t.col, a), true);
        ty -= 34 * ui;
    }
    // ---- big centre message ----
    if (bigTimer > 0) {
        float a = Saturate(bigTimer / 0.8f) * Saturate((5.0f - bigTimer) * 4.0f + 1.0f);
        float size = 96 * ui;
        float tw = MeasureUIText(bigText.c_str(), size);
        DrawUITextShadow(bigText.c_str(), W * 0.5f - tw * 0.5f, H * 0.36f, size, ColorA(bigColor, a));
    }

    // ---- help overlay ----
    if (showHelp) {
        const char* lines[] = {
            "ON FOOT:  WASD move   Shift run   Mouse aim   LMB attack/shoot   R reload",
            "          1-6 / Q / wheel weapons   E / F / Enter  enter or hijack vehicle",
            "DRIVING:  W/S throttle & brake/reverse   A/D steer   Space handbrake",
            "          E exit   H horn   L headlights   G siren (emergency vehicles)",
            "WORLD:    T fast-forward time   F3 debug   Esc/P pause   F1 hide help   Alt+F4 quit",
        };
        float lh = 22 * ui, bw = 0;
        for (auto l : lines) bw = std::max(bw, MeasureUIText(l, 20 * ui, true));
        Rectangle hr = { W * 0.5f - bw * 0.5f - 16, H - (5 * lh) - 40 * ui, bw + 32, 5 * lh + 20 * ui };
        Panel(hr, 0.5f);
        for (int i = 0; i < 5; i++) DrawUIText(lines[i], hr.x + 16, hr.y + 10 * ui + i * lh, 20 * ui, ColorA(WHITE, 0.85f), true);
    }
    if (debug) {
        int nv = 0, np = 0;
        for (auto& v : vehicles) nv += v.active;
        for (auto& p : peds) np += p.active;
        snprintf(buf, sizeof(buf), "FPS %d  vehicles %d  peds %d  particles %d  heat %.2f", GetFPS(), nv, np, fx.Count(), heat);
        DrawUIText(buf, 20 * ui, 140 * ui, 20 * ui, YELLOW, true);
    }
    if (state == GameState::Playing && !player.inVehicle && IsCursorHidden()) Crosshair(GetMousePosition(), ui, player.aiming);
}

void Game::DrawTitle() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float ui = Clampf(H / 900.0f, 0.75f, 1.6f);
    DrawRectangleGradientV(0, 0, (int)W, (int)H, ColorA(BLACK, 0.25f), ColorA(BLACK, 0.8f));
    const char* t = "CONCRETE JUNGLE";
    float size = 130 * ui;
    float tw = MeasureUIText(t, size);
    DrawUITextShadow(t, W * 0.5f - tw * 0.5f, H * 0.28f, size, { 255, 205, 70, 255 });
    const char* sub = "a top-down open-city action game";
    DrawUIText(sub, W * 0.5f - MeasureUIText(sub, 34 * ui, true) * 0.5f, H * 0.28f + size, 34 * ui, ColorA(WHITE, 0.8f), true);
    if (fmodf(time, 1.2f) < 0.85f) {
        const char* p = "Press ENTER to start";
        DrawUITextShadow(p, W * 0.5f - MeasureUIText(p, 40 * ui) * 0.5f, H * 0.62f, 40 * ui, WHITE);
    }
    const char* c = "Assets: Unlucky Studio, ambientCG, FabinhoSC (CC0) - Survivor by Riley Gombart (CC-BY 3.0) - Rajdhani (OFL)";
    DrawUIText(c, W * 0.5f - MeasureUIText(c, 18 * ui, true) * 0.5f, H - 40 * ui, 18 * ui, ColorA(WHITE, 0.5f), true);
}

void Game::DrawPause() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float ui = Clampf(H / 900.0f, 0.75f, 1.6f);
    DrawRectangle(0, 0, (int)W, (int)H, ColorA(BLACK, 0.55f));
    const char* t = "PAUSED";
    DrawUITextShadow(t, W * 0.5f - MeasureUIText(t, 100 * ui) * 0.5f, H * 0.33f, 100 * ui, WHITE);
    const char* o = "Esc / P - resume        Q - quit";
    DrawUIText(o, W * 0.5f - MeasureUIText(o, 30 * ui, true) * 0.5f, H * 0.33f + 120 * ui, 30 * ui, ColorA(WHITE, 0.8f), true);
}
