// =====================================================================================
//  Audio: every sound is synthesised at start-up (no audio files needed).
//  Loops (engine, siren, skid, city ambience) are music streams built from in-memory
//  WAV data so their pitch/volume can change every frame; one-shots are Sounds with a
//  small pool of aliases so the same effect can overlap. Positional: volume falls off
//  with distance from the listener and pans left/right.
//  To replace a sound with a real recording later, drop assets/sounds/<name>.wav in -
//  files found there override the synthesised version.
// =====================================================================================
#pragma once
#include "raylib.h"
#include <vector>
#include <string>
namespace startup { class Reporter; }

enum class Sfx : int { Pistol = 0, Shotgun, Rifle, Punch, Knife, Crash, CrashSmall, Explosion, Horn, Door,
                       Pickup, MissionPass, MissionFail, Wasted, Splash, Glass, Footstep, Reload, Scream,
                       ShoutHey, ShoutOi, ShoutHah, COUNT };
constexpr int SHOUT_VARIANTS = 3;          // ShoutHey, ShoutOi, ShoutHah: an angry driver yelling

class AudioSystem {
public:
    bool ok = false;
    bool Init(startup::Reporter* loading = nullptr);
    void Unload();
    void Update(Vector2 listener);                         // call once per frame

    void Play(Sfx s, Vector2 pos, float volume = 1.0f, float pitch = 1.0f);
    void PlayUI(Sfx s, float volume = 1.0f, float pitch = 1.0f);

    // continuous sounds, set every frame
    void SetEngine(bool on, float rpm, float throttle, bool large);
    void SetSiren(float volume);
    void SetSkid(float volume);

private:
    struct Loop { Music music{}; std::vector<unsigned char> wav; bool playing = false; };
    std::vector<Sound> sounds[(int)Sfx::COUNT];
    int next[(int)Sfx::COUNT]{};
    Loop engine, engineBig, siren, skid, ambience;
    Vector2 listener{ 0, 0 };
    float engineTarget = 0, engineVol = 0, enginePitch = 1, bigMix = 0;
    float sirenVol = 0, skidVol = 0;

    void MakeLoop(Loop& l, const std::vector<short>& pcm, const char* overrideName, startup::Reporter* loading = nullptr);
    void StartLoop(Loop& l);
};
