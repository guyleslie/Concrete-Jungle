// =====================================================================================
//  Asset loading - DATA DRIVEN.
//
//  What gets loaded is described by the text files in assets/data/:
//    vehicles.cfg    CLASS (handling), SPRITE (image files), GEN (procedural vehicles)
//    characters.cfg  ANIM / FEET / SCALE lines for animated character sprite sets
//    weapons.cfg     WEAPON lines (damage, fire rate, ammo, which animation set)
//    foliage.cfg     TREE / BUSH image lists
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
    std::vector<Texture2D>    peds;              // civilian atlases (spritegen::PedAtlas)
    Texture2D                 playerUnarmed{};   // Survivor-look atlas without weapon
    std::vector<CharacterSet> charSets;
    SpriteAnim                feet[(int)FeetAnim::COUNT];
    float                     charScale = 0.062f;    // world px per source px
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

    bool Load();
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
