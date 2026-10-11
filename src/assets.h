// =====================================================================================
//  Asset loading - DATA DRIVEN.
//
//  What gets loaded is described by the text files in assets/data/:
//    vehicles.cfg    CLASS (handling), SPRITE / BIKE (image files), GEN (procedural vehicles)
//    characters.cfg  ANIM / FEET / SCALE lines for animated character sprite sets
//    weapons.cfg     WEAPON lines (damage, fire rate, ammo, which animation set)
//    foliage.cfg     TREE / BUSH image lists
//    civilians.cfg   FRAME / SCALE / SWAY / ANIM / GAIT / WALK / CIVILIAN: pedestrian atlases
//    player.cfg      FRAME / SCALE / COLS / LEGS / GAIT / SET / UPPER / LAMP: the player's layers
//  Adding content = drop the image files in assets/ and add a line. Built-in defaults
//  are used when a file is missing, and every image has a procedural fallback.
//
//  Sources: ambientCG (CC0) textures, Unlucky Studio (CC0) vehicles, FabinhoSC (CC0)
//  foliage, Riley Gombart (CC-BY 3.0) Survivor, Rajdhani (OFL). See CREDITS.md.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "vehicle_types.h"
#include "sprite_gen.h"
#include <vector>
#include <string>

namespace startup { class Reporter; }

// One civilian look. Its frames are cut to their visible part at load and packed side by side,
// so the GPU stores and draws little of the transparent frame margins.
struct CivilianAtlas {
    Texture2D              tex{};
    std::vector<Rectangle> src;          // per atlas frame: its visible part in tex
    std::vector<Vector2>   offset;       // that part's centre minus the frame centre (frame px)
    float                  walkCycleM = 1.3f;   // metres per walk cycle: the look's stride
};
struct PedFrames { int first = 0, count = 0; };
// A gait whose cadence rises with speed: steps per second from 'fromSteps' at 'fromSpeed' to
// 'toSteps' at 'toSpeed' (m/s); it shows from 'fromSpeed' up.
struct PedGait { float fromSpeed = 0, fromSteps = 0, toSpeed = 0, toSteps = 0; };

// The player (player.cfg, CJ-011): a legs layer and an upper-body layer per weapon set, drawn
// on top of each other about the same pivot. Frames are cut to their visible part at load.
struct FrameAtlas {
    Texture2D              tex{};
    std::vector<Rectangle> src;          // per atlas frame: its visible part in tex
    std::vector<Vector2>   offset;       // that part's centre minus the frame centre (frame px)
};
enum class PlayerGait : int { Stand = 0, Walk, Run, StrafeLeft, StrafeRight, Back, JogLeft, JogRight, COUNT };
enum class PlayerAct : int { Aim = 0, Fire, Reload, Melee, COUNT };
struct PlayerGaitDef {
    PedFrames frames;
    bool      byDistance = true;         // false: by cadence
    float     cycleM = 1.4f;             // metres per cycle (two steps) when by distance
    float     steps = 3.0f, atSpeed = 6.5f;   // steps per second at atSpeed (m/s) when by cadence
};
// A flashlight's lens in one upper-body frame (LAMP): metres from the frame centre at life size
// (right, ahead, above the ground) and the beam's direction (degrees right of the facing, below
// the horizontal).
struct PlayerLamp { bool set = false; float right = 0, ahead = 0, height = 0, yaw = 0, pitch = 0; };
struct PlayerSet {
    std::string name;                    // a weapon set of weapons.cfg (unarmed, handgun ...)
    FrameAtlas  atlas;
    std::vector<PlayerLamp> lamp;        // per atlas frame, for a set with a flashlight
    PedFrames   gait[(int)PlayerGait::COUNT];   // the weapon lowered, in step with the legs
    PedFrames   act[(int)PlayerAct::COUNT];
};

struct VehicleSprite {
    Texture2D tex{};
    Rectangle src{};        // tight (alpha-trimmed) source rectangle
    VClass    cls = 0;
    Color     paint = WHITE;
    int       emptySkin = -1;  // motorbikes: the same bike without a rider
    bool      spawnable = true;
};

// One animation packed into an atlas. Frames face +X (east); 'pivot' = body centre.
struct SpriteAnim {
    Texture2D atlas{};
    int     frames = 0, cols = 1;
    Vector2 frameSize{ 0, 0 };
    Vector2 pivot{ 0, 0 };
    Rectangle Frame(int i) const {
        i = frames ? ((i % frames) + frames) % frames : 0;
        return { (i % cols) * frameSize.x, (i / cols) * frameSize.y, frameSize.x, frameSize.y };
    }
};

enum class BodyAnim : int { Idle = 0, Move, Shoot, Reload, Melee, COUNT };
enum class FeetAnim : int { Idle = 0, Walk, Run, StrafeLeft, StrafeRight, COUNT };

struct CharacterSet {             // e.g. "handgun", "rifle" ... from characters.cfg
    std::string name;
    SpriteAnim  anim[(int)BodyAnim::COUNT];
};

enum class WeaponKind : int { Melee, Semi, Auto };
struct WeaponDef {
    std::string name, animSet, sound;
    float damage = 10, fireRate = 2, spreadDeg = 0, rangePx = 30, reloadTime = 1;
    int   pellets = 1, clip = 0, startAmmo = 0;
    WeaponKind kind = WeaponKind::Melee;
    bool  light = false;          // flashlight
    float shellTime = 0, pumpTime = 0;   // > 0: reloads shell by shell, then racks the pump
};

struct Assets {
    // --- ground & building materials (tiling, mipmapped) ---
    Texture2D asphalt{}, sidewalk{}, concrete{}, gravel{}, grass{}, plaza{}, cobble{}, water{};
    Texture2D facade[2]{};     // 0 = brick, 1 = glass office      (day albedo)
    Texture2D facadeLit[2]{};  // matching emissive window masks   (night)

    // --- vehicles ---
    std::vector<VehicleSprite>    vehicles;
    std::vector<std::vector<int>> byClass;       // skins per vehicle class

    // --- characters ---
    std::vector<CivilianAtlas> peds;             // civilian looks (civilians.cfg, else spritegen::PedAtlas)
    int                       pedFramePx = spritegen::PED_FRAME;        // square atlas frame (px)
    float                     pedFrameM = 1.4f;                         // metres a frame covers at life size
    float                     pedDrawScale = 1.0f;                      // standing civilians, on top of CHAR_SCALE
    float                     pedSway = 0.05f;                          // body turn with the steps (rad)
    PedFrames                 pedAnim[(int)spritegen::PedAnim::COUNT];  // atlas layout
    PedGait                   pedJog, pedRun;                           // used when the atlas has those frames
    const PedFrames& PedAnimFrames(spritegen::PedAnim a) const { return pedAnim[(int)a]; }
    Texture2D                 playerUnarmed{};   // Survivor-look atlas without weapon
    std::vector<CharacterSet> charSets;
    SpriteAnim                feet[(int)FeetAnim::COUNT];
    float                     charScale = 0.062f;    // world px per source px
    // the 3D player (player.cfg); when missing, the Survivor sets above are drawn
    bool                      player3D = false;
    int                       playerFramePx = 144;
    float                     playerFrameM = 2.1f, playerDrawScale = 1.0f;
    FrameAtlas                playerLegs;
    PlayerGaitDef             playerGait[(int)PlayerGait::COUNT];
    std::vector<PlayerSet>    playerSets;
    const PlayerSet* FindPlayerSet(const std::string& name) const;
    std::vector<WeaponDef>    weapons;

    // --- world dressing ---
    std::vector<Texture2D> trees, bushes;
    Texture2D props[(int)spritegen::Prop::COUNT]{};
    Texture2D trainCar{};

    // --- generated effect textures ---
    Texture2D lightRadial{}, lightCone{}, softCircle{}, spark{}, ring{}, blood{}, flare{};

    // --- fonts & shaders ---
    Font   font{}, fontSemi{};
    bool   customFont = false;
    Shader composite{};
    int    locLight = -1, locEmissive = -1, locBloom = -1, locBloomStr = -1, locTime = -1;
    Shader blur{};
    int    locBlurDir = -1;

    bool Load(startup::Reporter* reporter = nullptr);
    void Unload();

    int  RandomSkin(VClass c) const;                  // random skin of a class (-1 if none)
    int  RandomTrafficSkin() const;                   // weighted by class traffic weight
    int  RandomSkinWithFlag(uint32_t flag) const;     // e.g. VF_POLICE
    const CharacterSet* FindSet(const std::string& name) const;
    int  FindWeapon(const std::string& name) const;
};

extern Assets gAssets;

// Text helpers (UI font with fallback to raylib's default font)
void  DrawUIText(const char* text, float x, float y, float size, Color c, bool semi = false);
float MeasureUIText(const char* text, float size, bool semi = false);
void  DrawUITextShadow(const char* text, float x, float y, float size, Color c, bool semi = false);
