// =====================================================================================
//  Car radio (CJ-040). The stations of assets/data/radio.cfg broadcast live: from the
//  first moment of play each one plays its programme of songs, DJ talk and commercials,
//  whether anyone listens or not, so a car tuned to a station joins the broadcast where
//  it is, and every car on a station plays the same moment of it.
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

enum class RadioClip : int { Song = 0, Dj, Ad };

struct RadioItem {
    RadioClip kind = RadioClip::Song;
    std::string path;            // assets/radio/<folder>/<file>
    float gainDb = 0;            // brings the clip to the LOUDNESS target
    float seconds = 0;           // length, known once the audio device has opened it
};

struct RadioStationDef {
    std::string folder, name;
    float talkShare = 0.65f;     // share of the songs followed by a clip
    int songGap = 5, talkGap = 3;   // other songs / clips before one comes back
    std::vector<RadioItem> items;
};

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

// One station's programme: which clip comes next. A clip follows a song with the
// station's talk share and never another clip; a song or clip played recently is not
// chosen again until the station's gaps have passed.
class RadioProgramme {
public:
    void Reset(const RadioStationDef& station, uint32_t seed);
    int Next();                          // index into the station's items
    void Queue(int item) { queued.push_back(item); }   // tests: play these next, in order
private:
    const RadioStationDef* st = nullptr;
    Rng rng;
    std::vector<int> songs, talks;       // recently played, newest last
    std::vector<int> queued;
    bool lastTalk = true;                // the broadcast opens with a song
    void Played(int item);
};

class RadioSystem {
public:
    static constexpr int VOICES = 4;     // radios in traffic heard at once, per station
    static constexpr int MAX_STATIONS = 8;

    uint32_t seed = 0;                   // programme seed; 0 takes one from the clock
    bool recordTransitions = false;      // tests: keep the gap of every change of clip

    bool Init(bool audioReady);          // after the audio device: clip lengths, processors
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
    struct Transition { int station = -1, from = -1, to = -1; double gapMs = -1; int64_t startFrame = -1; };
    void Force(uint32_t serial, int station, float cabinDb);   // a car's radio, instead of its draw
    void SetTuned(uint32_t serial, int station) { tuned[serial] = station; }   // as if the player had tuned it
    void StartAt(int station, int item, float seconds);        // restart a station mid-clip
    void QueueNext(int station, int item);
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
        double lengthFrames = 0;         // the current clip's length in device frames
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
    double voiceSeconds = 0;

    void Start(int s);
    void Play(int s, int item, float offset);
    void Voices(Game& g, float dt);
    void EstimateRate();
};
