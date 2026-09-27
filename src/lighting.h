// =====================================================================================
//  Day / night cycle: ambient light colour, sun shadow direction, night factor.
// =====================================================================================
#pragma once
#include "raylib.h"

struct DayNight {
    float   hour      = 20.0f;   // 0..24
    float   timeScale = 1.0f;    // multiplier (fast-forward with T)
    Vector3 ambient{ 1, 1, 1 };  // 0..1, 1 == full daylight
    float   night     = 0.0f;    // 0 = day, 1 = full night (drives lamps / headlights / windows)
    Vector2 shadowVec{ 0.5f, 0.5f };  // ground offset of a shadow per unit of height
    float   shadowAlpha = 0.4f;

    void  Update(float dt);
    void  ClockString(char* buf, int size) const;
    Color AmbientColor() const;   // for clearing the light buffer (stored at half intensity)
};
