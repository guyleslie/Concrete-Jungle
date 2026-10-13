// =====================================================================================
//  Car radio (CJ-040). The stations of assets/data/radio.cfg broadcast live: each one
//  plays its programme of songs, DJ talk and commercials whether anyone listens or not,
//  so a car tuned to a station joins the broadcast where it is, and every car on a station
//  plays the same moment of it. A game joins every station at a random point of its
//  programme; a song is aired as a random excerpt of it (ADR-0015), and the next clip is
//  prepared on a worker thread while the current one plays, so changing clips never stalls.
//
//  One decoder per station. A stream processor on the audio thread makes everything the
//  player hears of that station from its one signal: the player's own radio as it is,
//  and up to four radios in traffic through a closed car body (a low-pass), quieter with
//  distance, from their side, late by the sound's travel time, so that a passing car's
//  music drops in pitch as it goes by (the Doppler shift). The station's programme comes
//  from its own random generator; the radio never touches the gameplay generator.
//  See docs/design/radio.md.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "datafile.h"
#include "math_utils.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Game;
struct Vehicle;
namespace startup { class Reporter; }

enum class RadioClip : int { Song = 0, Dj, Ad };

struct RadioItem {
    RadioClip kind = RadioClip::Song;
    std::string path;            // assets/radio/<folder>/<file>
    float gainDb = 0;            // brings the clip to the LOUDNESS target
    float seconds = 0;           // length, known once the audio device has opened it
};

struct RadioStationDef {
    std::string folder, name;
    float talkShare = 1.0f;      // share of the songs followed by a clip
    int songGap = 5, talkGap = 3;   // other songs / clips before one comes back
    float excerpt = 0, fadeIn = 0, fadeOut = 0;   // s of a song aired (0: whole songs), and its fades
    std::vector<RadioItem> items;
};

// One clip of a programme: which item, from which second of it, for how long.
struct RadioSlot { int item = -1; float start = 0, seconds = 0; };

struct RadioConfig {
    float inCar = 0.5f;                                  // game volume of the player's radio
    float cabinNormal = 80, cabinLoud = 92, loudShare = 0.25f;   // dB inside a traffic car
    float bodyCorner = 300, bodyLoss = 25;               // Hz; dB
    float hearing = 40, reference = 2;                   // dB; m
    float trafficShare = 0.4f;                           // traffic cars with the radio on
    std::vector<int> quiet;                              // vehicle classes that never play one
    float loudness = -16;                                // LUFS, the target of the gains
    std::vector<RadioStationDef> stations;
};

// Reads radio.cfg (vehicle classes must be loaded); every clip it names must exist.
bool LoadRadio(const std::vector<DataRecord>& records, std::string& error);
const RadioConfig& RadioData();

// A WAV file of interleaved frames: 16-bit PCM to listen to, or 32-bit float to measure.
void WriteRadioWav(const std::string& path, const std::vector<float>& data, int channels, int rate, bool pcm16);

// One station's programme: which clip comes next. A clip follows a song with the
// station's talk share and never another clip; a song or clip played recently is not
// chosen again until the station's gaps have passed. A song longer than the station's
// excerpt is aired as a slice of that length from a random point of it.
class RadioProgramme {
public:
    void Reset(const RadioStationDef& station, uint32_t seed);
    RadioSlot Next();
    void Queue(int item) { queued.push_back(item); }   // tests: play these next, in order
private:
    const RadioStationDef* st = nullptr;
    Rng rng;
    std::vector<int> songs, talks;       // recently played, newest last
    std::vector<int> queued;
    bool lastTalk = true;                // the broadcast opens with a song
    void Played(int item);
    int Pick();                          // a song or, after a song, a clip by the talk share
};

class RadioSystem {
public:
    static constexpr int VOICES = 4;     // radios in traffic heard at once, per station
    static constexpr int MAX_STATIONS = 8;

    uint32_t seed = 0;                   // programme seed; 0 takes one from the clock
    bool recordTransitions = false;      // tests: keep the gap of every change of clip

    bool Init(bool audioReady, startup::Reporter* loading = nullptr);   // after the audio device: clip
                                         // lengths, processors, every station joined at a random point
    void Unload();
    void Update(Game& g, float dt);      // once per frame, after the rest of the frame
    void Entered(const Vehicle& v);      // the player got into a vehicle: tune its radio
    void Tune(int step);                 // the player's next (+1) or previous (-1) station, or off
    const char* Banner(float& alpha) const;   // what the HUD shows after tuning; null when nothing

    // The station a car's radio is on (-1: off) and its cabin level in dB: drawn once per
    // car from its serial, so a car keeps its music and the gameplay generator never moves.
    int   CarStation(const Vehicle& v) const;
    float CarCabinDb(const Vehicle& v) const;

    // The station the player hears in their car (-1: none) and the device sample rate.
    int   PlayerStation() const { return heard; }
    float DeviceRate() const;

    // ---- measurement hooks (CJ-040 radio scenes) ----
    struct VoiceProbe { int vehicle = -1; float gain = 0, delay = 0, cabin = 0, targetGain = 0, level = -999; };
    struct Transition { int station = -1, from = -1, to = -1; double gapMs = -1; int64_t startFrame = -1; RadioSlot slot; };
    void Force(uint32_t serial, int station, float cabinDb);   // a car's radio, instead of its draw
    void SetTuned(uint32_t serial, int station) { tuned[serial] = station; }   // as if the player had tuned it
    // Restart a station: the whole clip from a second of it, then the clips given, then the programme.
    void StartAt(int station, int item, float seconds, const std::vector<int>& then = {});
    void SetExcerpt(int station, float seconds, float fadeIn, float fadeOut);
    double TakeWorstUpdateMs();          // the slowest radio update since the last call
    VoiceProbe Probe(int station, int voice) const;
    int  CurrentItem(int station) const;
    void StartCapture(int station);      // record the final mix and the station's own signal
    void StopCapture();
    size_t DrainCapture(std::vector<float>& out);   // interleaved: mix left, mix right, station
    int64_t CaptureStart() const;        // device frame of the first frame recorded
    std::vector<Transition> TakeTransitions();
    void LogSummary() const;             // the RADIO line of a scenario run

private:
    struct Voice { int vehicle = -1; uint32_t serial = 0; double freeAt = 0; float level = -999, targetGain = 0; };
    struct Station {
        bool started = false, paused = false;
        Music music{};
        int item = -1;
        RadioSlot slot;                  // what the current clip airs
        RadioProgramme programme;
        Voice voice[VOICES];
        float dry = 0;                   // the player's radio, faded in and out
    };
    bool ok = false;
    std::vector<Station> stations;
    std::unordered_map<uint32_t, int> tuned;            // the player's choice per car
    std::unordered_map<uint32_t, std::pair<int, float>> forced;
    uint32_t playerCar = 0;              // serial of the car the player sits in (0: none)
    int playerStation = -1, heard = -1;
    std::string banner;
    float bannerTime = 0;
    double rateT0 = -1; int64_t rateF0 = 0;
    std::vector<Transition> transitions;
    // statistics for LogSummary
    double cpuMain = 0; int64_t frames = 0; int items = 0, maxVoices = 0;
    double voiceSeconds = 0, playerHeard = 0, worstUpdate = 0, worstSinceTake = 0;
    std::string mixPath;                 // CJ_RADIO_MIX: record the final mix of the whole run here
    std::vector<float> mix;

    void Start(int s, startup::Reporter* loading);
    void Play(int s, const RadioSlot& slot, float from);   // now, from a second of the clip (a seek)
    // Open the programme's next clip (or the slot given, from the second given); a worker seeks into it.
    void Prepare(int s, const RadioSlot* slot = nullptr, float from = -1);
    void Switch(int s);                  // play the prepared clip
    void Drop(int s);                    // discard the prepared clip
    void Arm(int s, Music& m, const RadioSlot& slot, float from);
    void Voices(Game& g, float dt);
    void EstimateRate();
};
