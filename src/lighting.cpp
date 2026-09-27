#include "lighting.h"
#include "config.h"
#include "math_utils.h"
#include <cstdio>

struct AmbientKey { float hour; Vector3 c; };
static const AmbientKey KEYS[] = {
    { 0.0f,  { 0.12f, 0.14f, 0.26f } },
    { 4.5f,  { 0.13f, 0.15f, 0.27f } },
    { 6.0f,  { 0.62f, 0.48f, 0.46f } },
    { 7.5f,  { 0.94f, 0.86f, 0.78f } },
    { 10.0f, { 1.00f, 0.99f, 0.96f } },
    { 16.0f, { 1.00f, 0.97f, 0.91f } },
    { 18.0f, { 0.97f, 0.74f, 0.55f } },
    { 19.5f, { 0.50f, 0.38f, 0.50f } },
    { 21.0f, { 0.16f, 0.17f, 0.31f } },
    { 24.0f, { 0.12f, 0.14f, 0.26f } },
};

void DayNight::Update(float dt) {
    hour += dt * cfg::HOURS_PER_SECOND * timeScale;
    while (hour >= 24.0f) hour -= 24.0f;

    int n = sizeof(KEYS) / sizeof(KEYS[0]);
    for (int i = 0; i < n - 1; i++) {
        if (hour >= KEYS[i].hour && hour <= KEYS[i + 1].hour) {
            float t = SmoothStep(KEYS[i].hour, KEYS[i + 1].hour, hour);
            ambient = { Lerpf(KEYS[i].c.x, KEYS[i + 1].c.x, t), Lerpf(KEYS[i].c.y, KEYS[i + 1].c.y, t),
                        Lerpf(KEYS[i].c.z, KEYS[i + 1].c.z, t) };
            break;
        }
    }
    float lum = ambient.x * 0.3f + ambient.y * 0.59f + ambient.z * 0.11f;
    night = Saturate((0.72f - lum) / 0.45f);

    // Sun travels east -> west between 6h and 18h; shadows point away from it and get
    // long at dawn / dusk. At night a faint moon shadow remains.
    float t = Saturate((hour - 6.0f) / 12.0f);
    float len = 0.35f + 1.2f * (1.0f - sinf(PI * t));
    Vector2 sun = Norm(V2(-1.0f + 2.0f * t, 0.55f)) * len;
    Vector2 moon = V2(0.25f, 0.4f);
    shadowVec = LerpV(sun, moon, night);
    shadowAlpha = Lerpf(0.45f, 0.18f, night);
}

void DayNight::ClockString(char* buf, int size) const {
    int h = (int)hour, m = (int)((hour - h) * 60.0f);
    snprintf(buf, size, "%02d:%02d", h, m);
}

Color DayNight::AmbientColor() const {
    return { (unsigned char)(Saturate(ambient.x) * 127.5f), (unsigned char)(Saturate(ambient.y) * 127.5f),
             (unsigned char)(Saturate(ambient.z) * 127.5f), 255 };
}
