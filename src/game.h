// =====================================================================================
//  Game: owns the world (map, vehicles, pedestrians, effects) and the game rules
//  (player, weapons, wanted level, police, missions, pickups). Rendering is driven
//  from Game::Draw using the Renderer passes.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "config.h"
#include "assets.h"
#include "city_map.h"
#include "vehicle.h"
#include "pedestrian.h"
#include "particles.h"
#include "lighting.h"
#include "render.h"
#include "audio.h"
#include "physics.h"
#include <vector>
#include <string>

enum class GameState { Title, Playing, Paused, Wasted, Busted };

struct PlayerState {
    bool    inVehicle = false;
    int     vehicle = -1;
    // on foot
    Vector2 pos{}, vel{};
    float   aim = 0;               // body / aim direction
    float   feetAngle = 0;         // legs follow the movement direction
    float   bodyAnim = 0, feetAnim = 0;
    BodyAnim bodyState = BodyAnim::Idle;
    float   actionTimer = 0;       // shoot / melee / reload animation time left
    float   health = 100, armor = 0;
    int     weapon = 0;
    std::vector<int> clip, ammo;   // per weapon (clip = loaded, ammo = reserve)
    std::vector<char> owned;
    float   fireCooldown = 0, reloadTimer = 0;
    bool    triggerHeld = false;
    float   enterTimer = 0;  int enterTarget = -1;
    float   hurtFlash = 0;
    bool    running = false;
    int     feetMode = 0;          // FeetAnim: idle / walk / run / strafe left / strafe right
    bool    aiming = false;
};

struct Pickup {
    Vector2 pos;
    int     kind;          // 0 health, 1 armor, 2.. weapon index + 2
    int     amount;
    bool    active = true;
    float   respawn = 0;
};

enum class MissionType { None, Courier, Demolition, Steal };
struct Mission {
    MissionType type = MissionType::None;
    int     stage = 0;
    Vector2 target{};
    int     targetVehicle = -1;
    VClass  wantedClass = -1;
    float   timer = 0;
    int     reward = 0;
    std::string title, objective;
};

struct Toast { std::string text; float time; Color col; };

class Game {
public:
    // ---- world ----
    CityMap                 map;
    std::vector<Vehicle>    vehicles;
    std::vector<Pedestrian> peds;
    PedGrid                 pedGrid;          // who is where (rebuilt before vehicles and pedestrians update)
    struct DeathSpot { Vector2 pos; float time; };
    std::vector<DeathSpot>  deathSpots;       // recent deaths: nobody new appears there for a while
    std::vector<Pickup>     pickups;
    VehiclePhysics          physics;
    Particles               fx;
    DayNight                dn;
    CameraRig               cam;
    Renderer                renderer;
    AudioSystem             audio;
    PlayerState             player;

    // ---- rules / progress ----
    GameState state = GameState::Title;
    float     stateTimer = 0;
    int       money = 0;
    float     heat = 0;               // wanted level 0..6 (stars = ceil)
    float     heatCooldown = 0;       // time since the police last saw the player
    float     policeTimer = 0;
    Mission   mission;
    std::vector<Vector2> jobPhones;   // mission start points
    int       activePhone = -1;
    float     missionOffer = 3;
    std::vector<Toast> toasts;
    std::string bigText; Color bigColor = WHITE; float bigTimer = 0;
    float     time = 0;
    bool      showHelp = true, debug = false;
    int       kills = 0;
    float     bustTimer = 0;
    bool      quit = false;
    int       headlightMode = 0;      // 0 auto, 1 on, 2 off

    void Init();
    void DebugScenario(const char* name);   // --scenario: foot | drive | night | chase
    bool autoDrive = false;
    int  aiContacts = 0;
    int  jolts = 0, joltRail = 0, joltKnocked = 0, joltPolice = 0;
    bool debugContacts = false;
    bool debugOverview = false;                    // AI-vs-AI collision count (diagnostics)
    void LogTrafficStats() const;
    // --shot autopilots: 0 cruise, 1 scripted crash course, 2 demolition derby, 3 sidewalk rampage
    int   autoMode = 0, autoPhase = -1, testBuilding = -1;
    float autoT = 0, autoTimer = 0, autoSteer = 0, autoReverse = 0, autoSlow = 0;
    void  AutoPilot(Vehicle& v, VehicleInput& in, float dt);
    bool  BrawlPilot(Vector2& move);             // --scenario brawl: walk up to people and punch them
    // physics diagnostics (--shot): jitter = position / heading reversing frame after frame
    struct PhysDiag { Vector2 prevPos{}, prevD{}; float prevA = 0, prevDa = 0; bool init = false, rail = false; };
    std::vector<PhysDiag> diag;
    int   diagPosFlips = 0, diagAngFlips = 0, diagDeepPen = 0, diagStuck = 0;
    int   statKnocks = 0, statRejoins = 0, statAbandons = 0;   // traffic knocked off / back on the lane / given up
    int   statYields = 0, statChainYields = 0;                 // cooperative yielding roles taken (direct + chain)
    float diagMaxPen = 0, diagBodySeconds = 0, diagPlayerSlow = 0;
    void  PhysDiagnostics(float dt);
    // wait-for cycles (--shot): drivers waiting on each other in a closed loop
    std::vector<float> waitCycleTime;               // per vehicle: time spent in a cycle so far
    float diagLongestCycle = 0;                     // s, the longest-lasting cycle
    int   diagCycles = 0, diagLongCycles = 0;       // cycles that formed / lasted over 10 s
    void  WaitDiagnostics(float dt);
    void  LogPhysStats() const;
    void ApplyTestImpacts(float dt) { HandleImpacts(dt); }
    // pedestrian diagnostics (--shot): per-frame sums (divide by 'frames' for averages)
    struct PedDiag {
        double flee = 0, dodge = 0, offCrossing = 0, visible = 0, overlaps = 0;
        float  againstLights = 0, jaywalking = 0, downOverdue = 0, sliding = 0, moving = 0;   // person-seconds
        int    frames = 0, trafficHits = 0, playerHits = 0, threatened = 0, threatHits = 0;
    };
    PedDiag pdiag;
    int     pedFights = 0, pedPunches = 0;       // people who hit back / punches they landed on the player
    std::vector<float> pedThreat;                  // per person: > 0 while in the player's path (rampage)
    double  cpuVehicles = 0, cpuPeds = 0, cpuDraw = 0; int cpuFrames = 0;
    void  PedDiagnostics(float dt);
    void  LogPedStats() const;
    void Update(float dt);
    void Draw();
    void Unload();

    // ---- queries used by AI modules ----
    Vector2 PlayerPos() const;
    Vector2 PlayerVel() const;
    bool    PlayerInCar() const { return player.inVehicle; }
    int     Stars() const { return (int)ceilf(heat - 0.001f); }
    Rectangle ViewRect(float margin = 0) const { return cam.VisibleGround(margin); }
    bool    OnScreen(Vector2 p, float margin = 0) const;

    // ---- events ----
    void DamageVehicle(int idx, float dmg, bool byPlayer, Vector2 at);
    void ExplodeVehicle(int idx);
    void DamagePed(int idx, float dmg, Vector2 dir, bool byPlayer, bool knockDown);
    void DamagePlayer(float dmg, Vector2 dir);
    void Crime(float amount, Vector2 where);
    void Scare(Vector2 where, float radius);
    void Toast_(const char* text, Color c = WHITE);
    void Big(const char* text, Color c, float dur);
    int  SpawnVehicle(int skin, Vector2 pos, float angle, DriverType d);
    int  SpawnPed(Vector2 pos, int skin, bool fleeing = false);
    // Also driven directly by measurement fixtures.
    void HandleImpacts(float dt);                  // physics events -> damage, sparks, sounds, reactions
    void EnterVehicle(int idx);
    void ExitVehicle();

private:
    void NewGame();
    void UpdatePlaying(float dt);
    void UpdatePlayerOnFoot(float dt);
    void UpdatePlayerDriving(float dt);
    void FireWeapon();
    void MeleeHit();
    void UpdateVehicles(float dt);                 // AI, physics step, impacts, effects
    void VehiclePedCollisions();
    void ThrowRider(int idx, float severity);      // motorbike crash: the rider comes off
    void UpdatePeds(float dt);
    void UpdatePolice(float dt);
    void UpdateSpawning(float dt);
    void UpdateMission(float dt);
    void StartMission(int phone);
    void EndMission(bool passed);
    void UpdatePickups(float dt);
    void Respawn(bool busted);
    void DrawWorld();
    void DrawPlayerSprite(bool shadowPass, Vector2 sv) const;
    void DrawHUD();
    void DrawTitle();
    void DrawPause();
};

Rng& GRng();
