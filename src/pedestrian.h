// =====================================================================================
//  Pedestrians: walk around their block's sidewalk ring, wait at the kerb for the
//  lights and a gap in the traffic, use the zebra crossings, wander into parks, idle.
//
//  Perception: every ~0.1 s a person predicts the motion of nearby vehicles (keeping
//  their speed and turn rate; a driver braking for people is assumed to stop). Only a
//  vehicle whose predicted path actually reaches them within 1.6 s is a danger: after
//  a personal reaction time they jump sideways out of its path (Dodge), then either
//  carry on, stand startled, or run (Flee). Gunfire and explosions make people flee
//  after their reaction time; panic spreads to bystanders, but only one step.
//
//  Locomotion: a person has a body heading, a turn rate and an acceleration limit,
//  and walks where the body faces (no sliding sideways). Steering combines the goal
//  velocity with anticipatory avoidance of other people and street furniture based
//  on the time to collision (Karamouzas, Skinner & Guy 2014) - see docs/adr/0006.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "config.h"
#include "math_utils.h"
#include <vector>

class Game;
class CityMap;

// Confront: a driver walks to the other party and argues; ToCar: back to their own car.
enum class PedState : uint8_t { Walk, Wait, Cross, Idle, Wander, Flee, Rejoin, Dodge, Fight, Confront, ToCar, Down, Dead };

struct Pedestrian {
    bool     active = false;
    uint32_t serial = 0;             // unique per spawn: handles check it before use
    Vector2  pos{}, vel{};
    float    angle = 0;              // body heading
    float    walkSpeed = 24;         // px/s (1.2 - 1.7 m/s)
    int      skin = 0;
    PedState state = PedState::Walk;
    PedState resume = PedState::Walk;   // after a dodge or a startled pause
    float    timer = 0, anim = 0;
    float    health = 100;
    int      bi = 0, bj = 0, corner = 0, dirSign = 1;
    Vector2  target{};
    int      ci = 0, cj = 0, caxis = 0;   // intersection & signal axis while crossing
    int      nextBi = 0, nextBj = 0, nextCorner = 0;
    Vector2  waitPt{};               // where to stand at the kerb
    int      queued = 0;             // times the kerb spot was moved back behind others
    Vector2  threat{};               // what a fleeing person runs from
    Vector2  moveDir{};              // Dodge / Flee: chosen direction
    float    moveSpeed = 0;          // Dodge: speed of the jump / step aside
    float    rethink = 0;            // Flee / Wait: time until the next decision
    int      dodgeFrom = -1;         // vehicle being dodged
    bool     closeCall = false;      // the vehicle came very close (may panic afterwards)
    float    alarm = -1;             // >= 0: will react to a danger after this delay
    Vector2  alarmFrom{};  float alarmDur = 0;
    bool     alarmSecondHand = false;
    bool     secondHand = false;     // fleeing because others flee (does not spread further)
    int      alarmVehicle = -1;      // the reaction is a dodge from this vehicle
    float    sense = 0;              // time until the next look at the traffic
    float    reaction = 0.3f;        // personal reaction time (s)
    float    patience = 30;          // waiting at a red light before crossing on a gap
    float    courage = 0;            // > 0.85: hits back when punched instead of running
    float    punchCd = 0, punchT = 0;   // Fight: time to the next punch / punch animation left
    bool     jaywalk = false;        // crossing against the lights
    bool     hitByPlayer = false;
    float    deadTime = 0;
    float    runOverT = 0;           // lying: time until a wheel can hurt them again
    bool     pooled = false;         // dead: blood pool left where the body came to rest
    float    laneOffset = 0;         // personal walking line on the sidewalk (px)
    float    sway = 0, spin = 0;
    float    turnRate = 0;           // rad/s, for stepping round on the spot
    float    stuckT = 0;
    Vector2  lastPos{};
    // --- a traffic driver on foot during an incident (traffic_incidents.h) ---
    int      incident = -1;
    int      ownVehicle = -1;  uint32_t ownSerial = 0;          // the car they return to
    int      foe = -1;         uint32_t foeSerial = 0;          // opponent on foot
    bool     foePlayer = false;                                  // the opponent is the player
    int      foeVehicle = -1;  uint32_t foeVehicleSerial = 0;   // opponent still in this car
    float    argue = 0;                                          // s spent arguing face to face
};

constexpr float PED_RADIUS = 0.32f * 16.0f * 1.35f;   // keep in sync with cfg::CHAR_SCALE
constexpr float PED_BODY_FADE = 24.0f;                // s after death: the body starts to fade...
constexpr float PED_BODY_GONE = 27.0f;                // ...and is gone (the blood pool stays longer)
inline bool IsDodging(const Pedestrian& p) { return p.state == PedState::Dodge; }

// Uniform grid (one cell per tile) for "who is near this point" queries. Rebuilt from
// scratch whenever the positions have changed enough to matter (twice a frame).
struct PedGrid {
    static constexpr int W = cfg::MAP_W, H = cfg::MAP_H;
    std::vector<int> head, next;
    void Build(const std::vector<Pedestrian>& peds);
    template <class F> void Query(Vector2 p, float r, F&& f) const {
        if (head.empty()) return;
        int x0 = std::max(0, (int)((p.x - r) / cfg::TILE)), x1 = std::min(W - 1, (int)((p.x + r) / cfg::TILE));
        int y0 = std::max(0, (int)((p.y - r) / cfg::TILE)), y1 = std::min(H - 1, (int)((p.y + r) / cfg::TILE));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                for (int k = head[y * W + x]; k >= 0; k = next[k]) f(k);
    }
};

void InitPed(Pedestrian& p, Vector2 pos, int skin, const CityMap& map);
void UpdatePed(Pedestrian& p, Game& g, float dt);
void ScarePed(Pedestrian& p, Vector2 from, float duration);                 // flee right away (victims)
void AlarmPed(Pedestrian& p, Vector2 from, float duration, bool secondHand); // flee after the reaction time
void KnockDownPed(Pedestrian& p, Vector2 impulse);
void ProvokePed(Pedestrian& p, const Game& g);                                // punched by the player
void DrawPed(const Pedestrian& p);
void DrawPedShadow(const Pedestrian& p, Vector2 sv);
