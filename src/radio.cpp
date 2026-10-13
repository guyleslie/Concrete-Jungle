// =====================================================================================
//  Car radio (CJ-040) - see radio.h.
//
//  Threads: the main thread runs the programme, opens the clips and decides, once a
//  frame, what the player hears of each station (their own radio, and which cars in
//  traffic at what gain, direction, travel time and through how many car bodies). It
//  hands those targets to the audio thread in atomics; the station's stream processor
//  moves towards them smoothly, sample by sample, so nothing clicks. While a clip plays,
//  the next one is opened and a worker thread seeks into it (an MP3 seek decodes from the
//  start: up to 0.7 s), so the main thread only switches streams.
// =====================================================================================
#include "radio.h"
#include "game.h"
#include "startup_loading.h"
#include "vehicle_types.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <thread>

namespace {
constexpr float SOUND_SPEED = 343.0f;      // m/s in air at 20 C
constexpr float FADE_DB = 6.0f;            // a radio fades out over its last 6 dB above the hearing threshold
constexpr float GAIN_TAU = 0.03f;          // s: gains follow their targets this smoothly (no zipper noise)
constexpr float DELAY_TAU = 0.05f;         // s: the travel time follows the distance this smoothly
constexpr double RELEASE_S = 0.25;         // a voice let go is silent after this and may take another car
constexpr float BANNER_S = 3.0f;           // s the tuned station's name stays on screen
constexpr float DRY_IN = 6.0f, DRY_OUT = 25.0f;   // 1/s: the player's radio fades in (~0.4 s) and out
constexpr int RING = 1 << 15;              // frames of a station's signal kept for the travel time (0.68 s at 48 kHz)
constexpr int RING_MASK = RING - 1;
constexpr int VOICES = RadioSystem::VOICES;

RadioConfig C;

bool Number(const std::string& s, float& out) {
    char* end = nullptr;
    out = strtof(s.c_str(), &end);
    return end != s.c_str() && *end == '\0' && std::isfinite(out);
}

// Draws for a car from its serial: the same car always gets the same, and the gameplay
// generator never moves (traffic and every measured scenario stay exactly as they were).
uint32_t Hash(uint32_t serial, uint32_t salt) {
    uint32_t x = serial * 0x9E3779B9u ^ salt * 0x85EBCA6Bu;
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}
float Unit(uint32_t serial, uint32_t salt) { return (Hash(serial, salt) & 0xFFFFFF) / 16777216.0f; }
float DbToGain(float db) { return powf(10.0f, db / 20.0f); }
float Smooth(float x) { x = Saturate(x); return x * x * (3 - 2 * x); }

// ---- audio thread --------------------------------------------------------------------
// Second-order Butterworth low-pass (RBJ cookbook, Q = 1/sqrt 2): a closed car body.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float Run(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void LowPass(float hz, float rate) {
        float w = 2 * PI * std::min(hz, rate * 0.45f) / rate, c = cosf(w), a = sinf(w) / sqrtf(2.0f), n = 1 / (1 + a);
        b0 = (1 - c) * 0.5f * n; b1 = (1 - c) * n; b2 = b0; a1 = -2 * c * n; a2 = (1 - a) * n;
    }
    void Coefficients(const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
    void Clear() { z1 = z2 = 0; }
};

std::atomic<float> gRate{ 48000 };         // device sample rate (measured: raylib does not report it)
std::atomic<float> gCorner{ 300 };         // Hz, the body's corner frequency
std::atomic<int64_t> gMixFrames{ 0 };      // device frames mixed before the current callback
std::atomic<uint64_t> gCallbacks{ 0 };
std::atomic<int64_t> gDspNanos{ 0 };       // time spent in the station processors

// Recording for the measurements: the final mix and one station's own signal, frame by
// frame. The station processors run before the mixed processor in every callback, so the
// station's frames are staged and written together with the mix of the same callback.
struct Capture {
    std::atomic<bool> on{ false };
    std::atomic<int> station{ -1 };
    std::vector<float> ring;                // interleaved: mix left, mix right, station
    std::atomic<size_t> head{ 0 }, tail{ 0 };
    std::atomic<int64_t> dropped{ 0 }, startMix{ -1 };
    float stage[16384];
    unsigned staged = 0;
} gCap;

struct SharedVoice {                       // main thread -> audio thread
    std::atomic<float> gainL{ 0 }, gainR{ 0 }, delay{ 0 }, cabin{ 0 };
    std::atomic<uint32_t> epoch{ 0 };      // bumped when the voice takes another car
    std::atomic<float> nowGain{ 0 }, nowDelay{ 0 }, nowCabin{ 0 };   // audio thread -> probes
};

struct StationDsp {
    // main thread -> audio thread
    std::atomic<float> dry{ 0 }, itemGain{ 1 };
    std::atomic<int> itemSerial{ 0 };      // bumped for every clip
    // Seconds of the clip's file: where playback starts, where the slot aired ends, and the
    // fades: in from fadeFrom (-1: none) over fadeIn, out over the last fadeOut before the end.
    // In seconds, so that they stay right when the device's rate is measured.
    std::atomic<double> offsetS{ 0 }, endS{ 0 }, fadeFromS{ -1 }, fadeInS{ 0 }, fadeOutS{ 0 };
    SharedVoice voice[VOICES];
    // audio thread -> main thread
    std::atomic<int> countedSerial{ 0 };
    std::atomic<double> posS{ 0 };         // second of the clip's file reached
    std::atomic<int64_t> gapFrames{ -1 }, startFrame{ -1 };
    std::atomic<int> gapSerial{ 0 };
    // audio thread only
    float ring[RING]{};
    uint32_t w = 0;
    float dryNow = 0, rate = 0, corner = 0;
    struct Local { float gL = 0, gR = 0, delay = 0, cabin = 0; uint32_t epoch = 0; Biquad body, own; } local[VOICES];
    int counting = 0;
    double pos = 0;
    int64_t endFrame = -1;
    uint64_t lastCallback = ~0ull;
    unsigned inCallback = 0;

    void Run(float* b, unsigned n, int s) {
        auto t0 = std::chrono::steady_clock::now();
        float fs = gRate.load(std::memory_order_relaxed), fc = gCorner.load(std::memory_order_relaxed);
        if (fs != rate || fc != corner) {
            rate = fs; corner = fc;
            Biquad proto; proto.LowPass(fc, fs);
            for (Local& v : local) { v.body.Coefficients(proto); v.own.Coefficients(proto); }
        }
        // Where this block sits in the device's timeline, and in the current clip.
        uint64_t cb = gCallbacks.load(std::memory_order_acquire);
        if (cb != lastCallback) { lastCallback = cb; inCallback = 0; }
        int64_t blockStart = gMixFrames.load(std::memory_order_acquire) + inCallback;
        inCallback += n;
        int serial = itemSerial.load(std::memory_order_acquire);
        if (serial != counting) {          // the first frames of a new clip
            if (endFrame >= 0) { gapFrames.store(blockStart - endFrame); startFrame.store(blockStart); gapSerial.store(serial); }
            counting = serial; pos = offsetS.load(); endFrame = -1;
        }
        double end = endS.load(std::memory_order_relaxed), step = 1.0 / fs, p0 = pos;
        if (endFrame < 0 && end > 0 && pos + n * step >= end) endFrame = blockStart + (int64_t)std::max(0.0, (end - pos) * fs);
        pos += n * step;
        posS.store(pos, std::memory_order_relaxed);
        countedSerial.store(serial, std::memory_order_release);
        double fadeFrom = fadeFromS.load(std::memory_order_relaxed), fadeIn = fadeInS.load(std::memory_order_relaxed);
        double fadeOut = fadeOutS.load(std::memory_order_relaxed);
        bool fadingIn = fadeFrom >= 0 && fadeIn > 0, fadingOut = fadeOut > 0 && end > 0;

        float kG = 1 - expf(-1 / (GAIN_TAU * fs)), kD = 1 - expf(-1 / (DELAY_TAU * fs));
        float dryT = dry.load(std::memory_order_relaxed), g = itemGain.load(std::memory_order_relaxed);
        float tL[VOICES], tR[VOICES], tD[VOICES], tC[VOICES];
        bool live[VOICES];
        for (int v = 0; v < VOICES; v++) {
            SharedVoice& sv = voice[v];
            Local& lv = local[v];
            uint32_t e = sv.epoch.load(std::memory_order_acquire);
            tL[v] = sv.gainL.load(std::memory_order_relaxed); tR[v] = sv.gainR.load(std::memory_order_relaxed);
            tD[v] = sv.delay.load(std::memory_order_relaxed); tC[v] = sv.cabin.load(std::memory_order_relaxed);
            if (e != lv.epoch) {           // another car: its own travel time and a quiet start
                lv.epoch = e; lv.delay = tD[v]; lv.cabin = tC[v]; lv.gL = lv.gR = 0;
                lv.body.Clear(); lv.own.Clear();
            }
            live[v] = tL[v] > 0 || tR[v] > 0 || lv.gL > 1e-7f || lv.gR > 1e-7f;
        }
        float maxDelay = (RING - 4) / fs;
        bool capture = gCap.on.load(std::memory_order_acquire) && gCap.station.load(std::memory_order_relaxed) == s;
        for (unsigned i = 0; i < n; i++) {
            float L = b[2 * i] * g, R = b[2 * i + 1] * g;
            float plain = 0.5f * (L + R);  // recorded before the fades, so a measurement sees them
            if (fadingIn || fadingOut) {
                double p = p0 + i * step;
                float e = 1;
                if (fadingIn) e *= Smooth((float)((p - fadeFrom) / fadeIn));
                if (fadingOut) e *= Smooth((float)((end - p) / fadeOut));
                L *= e; R *= e;
            }
            float mono = 0.5f * (L + R);
            ring[w & RING_MASK] = mono;
            dryNow += (dryT - dryNow) * kG;
            float oL = L * dryNow, oR = R * dryNow;
            for (int v = 0; v < VOICES; v++) {
                if (!live[v]) continue;
                Local& lv = local[v];
                lv.delay += (tD[v] - lv.delay) * kD;
                float frames = std::min(lv.delay, maxDelay) * fs;
                uint32_t whole = (uint32_t)frames;
                float f = frames - (float)whole;
                uint32_t at = w - whole;   // ring[w] is now; one frame further back for the fraction
                float x = ring[at & RING_MASK] * (1 - f) + ring[(at - 1) & RING_MASK] * f;
                float y1 = lv.body.Run(x), y2 = lv.own.Run(y1);
                lv.cabin += (tC[v] - lv.cabin) * kG;
                float y = y1 + (y2 - y1) * lv.cabin;
                lv.gL += (tL[v] - lv.gL) * kG; lv.gR += (tR[v] - lv.gR) * kG;
                oL += y * lv.gL; oR += y * lv.gR;
            }
            if (capture && gCap.staged < sizeof(gCap.stage) / sizeof(float)) gCap.stage[gCap.staged++] = plain;
            b[2 * i] = oL; b[2 * i + 1] = oR;
            w++;
        }
        for (int v = 0; v < VOICES; v++) {
            const Local& lv = local[v];
            voice[v].nowGain.store(sqrtf(0.5f * (lv.gL * lv.gL + lv.gR * lv.gR)), std::memory_order_relaxed);
            voice[v].nowDelay.store(lv.delay, std::memory_order_relaxed);
            voice[v].nowCabin.store(lv.cabin, std::memory_order_relaxed);
        }
        gDspNanos.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count(),
                            std::memory_order_relaxed);
    }
};

StationDsp gDsp[RadioSystem::MAX_STATIONS];

// The next clip of each station, opened while the current one plays; a worker seeks into it.
struct Prepared {
    Music music{};
    RadioSlot slot;
    float from = 0;                        // the second of the file it plays from
    std::thread worker;
    std::atomic<bool> ready{ false };
    bool active = false;
} gPrep[RadioSystem::MAX_STATIONS];

template <int S> void Process(void* buffer, unsigned int frames) { gDsp[S].Run((float*)buffer, frames, S); }
const AudioCallback PROCESSORS[RadioSystem::MAX_STATIONS] = {
    Process<0>, Process<1>, Process<2>, Process<3>, Process<4>, Process<5>, Process<6>, Process<7> };

// After every callback's mix: counts the device's frames (its sample rate is measured from
// them) and records the mix for the measurements.
void MixTap(void* buffer, unsigned int frames) {
    const float* b = (const float*)buffer;
    if (gCap.on.load(std::memory_order_acquire) && !gCap.ring.empty()) {
        size_t size = gCap.ring.size(), head = gCap.head.load(std::memory_order_relaxed);
        size_t tail = gCap.tail.load(std::memory_order_acquire);
        if (size - (head - tail) < (size_t)frames * 3) gCap.dropped.fetch_add(frames);
        else {
            if (head == 0) gCap.startMix.store(gMixFrames.load(std::memory_order_relaxed));
            for (unsigned i = 0; i < frames; i++) {
                gCap.ring[(head + 0) % size] = b[2 * i];
                gCap.ring[(head + 1) % size] = b[2 * i + 1];
                gCap.ring[(head + 2) % size] = i < gCap.staged ? gCap.stage[i] : 0.0f;
                head += 3;
            }
            gCap.head.store(head, std::memory_order_release);
        }
    }
    gCap.staged = 0;
    gMixFrames.fetch_add(frames, std::memory_order_release);
    gCallbacks.fetch_add(1, std::memory_order_release);
}
} // namespace

// -------------------------------------------------------------------------------------
//  Data
// -------------------------------------------------------------------------------------
const RadioConfig& RadioData() { return C; }

void WriteRadioWav(const std::string& path, const std::vector<float>& data, int channels, int rate, bool pcm16) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { TraceLog(LOG_ERROR, "RADIO: cannot write %s", path.c_str()); return; }
    uint32_t bytes = (uint32_t)data.size() * (pcm16 ? 2 : 4);
    uint16_t format = pcm16 ? 1 : 3, ch = (uint16_t)channels, bits = pcm16 ? 16 : 32, align = (uint16_t)(channels * bits / 8);
    uint32_t r = (uint32_t)rate, byteRate = r * align, fmtSize = 16, riff = 36 + bytes;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtSize, 4, 1, f); fwrite(&format, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&r, 4, 1, f);
    fwrite(&byteRate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    if (pcm16) for (float v : data) { int16_t s = (int16_t)lrintf(Clampf(v, -1, 1) * 32767); fwrite(&s, 2, 1, f); }
    else fwrite(data.data(), 4, data.size(), f);
    fclose(f);
}

bool LoadRadio(const std::vector<DataRecord>& records, std::string& error) {
    C = RadioConfig{};
    auto fail = [&](const DataRecord& r, const std::string& what) {
        error = TextFormat("radio.cfg line %d: %s", r.line, what.c_str());
        return false;
    };
    // Reads token i as a number from lo to hi.
    auto value = [&](const DataRecord& r, size_t i, float lo, float hi, float& out) {
        if (i >= r.size() || !Number(r[i], out) || out < lo || out > hi) {
            error = TextFormat("radio.cfg line %d: %s value %d must be a number from %g to %g", r.line, r[0].c_str(), (int)i, lo, hi);
            return false;
        }
        return true;
    };
    const char* globals[] = { "INCAR", "CABIN", "BODY", "HEARING", "TRAFFIC", "LOUDNESS" };
    bool seen[6] = {};
    RadioStationDef* st = nullptr;
    bool programme = false, excerpt = false;
    for (const DataRecord& r : records) {
        int g = 0;
        while (g < 6 && !r.Is(globals[g])) g++;
        if (g < 6) {
            if (seen[g]) return fail(r, std::string("a second ") + globals[g] + " record");
            seen[g] = true;
        }
        float a = 0, b = 0, c = 0;
        if (r.Is("INCAR")) {
            if (r.size() != 2 || !value(r, 1, 0, 1, C.inCar)) return r.size() != 2 ? fail(r, "INCAR takes 1 value") : false;
        } else if (r.Is("CABIN")) {
            if (r.size() != 4) return fail(r, "CABIN takes 3 values");
            if (!value(r, 1, 20, 130, C.cabinNormal) || !value(r, 2, 20, 130, C.cabinLoud) || !value(r, 3, 0, 1, C.loudShare)) return false;
        } else if (r.Is("BODY")) {
            if (r.size() != 3) return fail(r, "BODY takes 2 values");
            if (!value(r, 1, 20, 20000, C.bodyCorner) || !value(r, 2, 0, 80, C.bodyLoss)) return false;
        } else if (r.Is("HEARING")) {
            if (r.size() != 3) return fail(r, "HEARING takes 2 values");
            if (!value(r, 1, 0, 130, C.hearing) || !value(r, 2, 0.1f, 20, C.reference)) return false;
        } else if (r.Is("TRAFFIC")) {
            if (r.size() < 2 || !value(r, 1, 0, 1, C.trafficShare)) return r.size() < 2 ? fail(r, "TRAFFIC needs a share") : false;
            for (size_t i = 2; i < r.size(); i++) {
                VClass cls = FindVehicleClass(r[i]);
                if (cls < 0) return fail(r, "unknown vehicle class '" + r[i] + "'");
                C.quiet.push_back(cls);
            }
        } else if (r.Is("LOUDNESS")) {
            if (r.size() != 2 || !value(r, 1, -70, 0, C.loudness)) return r.size() != 2 ? fail(r, "LOUDNESS takes 1 value") : false;
        } else if (r.Is("STATION")) {
            if (r.size() < 3) return fail(r, "STATION needs a folder and a name");
            if (st && !excerpt) return fail(r, "station " + st->name + " has no EXCERPT record");
            if ((int)C.stations.size() == RadioSystem::MAX_STATIONS)
                return fail(r, TextFormat("at most %d stations", RadioSystem::MAX_STATIONS));
            C.stations.push_back({});
            st = &C.stations.back();
            st->folder = r[1];
            for (size_t i = 2; i < r.size(); i++) st->name += (i > 2 ? " " : "") + r[i];
            programme = excerpt = false;
        } else if (r.Is("PROGRAMME")) {
            if (!st) return fail(r, "PROGRAMME before any STATION");
            if (programme) return fail(r, "a second PROGRAMME for station " + st->name);
            if (r.size() != 4) return fail(r, "PROGRAMME takes 3 values");
            if (!value(r, 1, 0, 1, st->talkShare) || !value(r, 2, 1, 99, b) || !value(r, 3, 0, 99, c)) return false;
            if (b != floorf(b) || c != floorf(c)) return fail(r, "the gaps of PROGRAMME are whole numbers");
            st->songGap = (int)b; st->talkGap = (int)c;
            programme = true;
        } else if (r.Is("EXCERPT")) {
            if (!st) return fail(r, "EXCERPT before any STATION");
            if (excerpt) return fail(r, "a second EXCERPT for station " + st->name);
            if (r.size() != 4) return fail(r, "EXCERPT takes 3 values");
            if (!value(r, 1, 0, 600, st->excerpt) || !value(r, 2, 0, 30, st->fadeIn) || !value(r, 3, 0, 30, st->fadeOut)) return false;
            if (st->excerpt > 0 && st->fadeIn + st->fadeOut >= st->excerpt) return fail(r, "the fades of EXCERPT must be shorter than it");
            excerpt = true;
        } else if (r.Is("SONG") || r.Is("DJ") || r.Is("AD")) {
            if (!st) return fail(r, std::string(r[0]) + " before any STATION");
            if (r.size() != 3) return fail(r, r[0] + " takes a file and a gain");
            if (!value(r, 2, -30, 30, a)) return false;
            RadioItem item;
            item.kind = r.Is("SONG") ? RadioClip::Song : r.Is("DJ") ? RadioClip::Dj : RadioClip::Ad;
            item.path = "assets/radio/" + st->folder + "/" + r[1];
            item.gainDb = a;
            if (!FileExists(item.path.c_str())) return fail(r, "missing clip " + item.path);
            st->items.push_back(item);
        } else return fail(r, "unknown record " + r[0]);
    }
    for (int g = 0; g < 6; g++)
        if (!seen[g]) { error = std::string("radio.cfg: no ") + globals[g] + " record"; return false; }
    if (st && !excerpt) { error = "radio.cfg: station " + st->name + " has no EXCERPT record"; return false; }
    if (C.stations.empty()) { error = "radio.cfg: no STATION"; return false; }
    for (const RadioStationDef& s : C.stations) {
        int songs = 0, talks = 0;
        for (const RadioItem& it : s.items) (it.kind == RadioClip::Song ? songs : talks)++;
        if (songs < 2) { error = "radio.cfg: station " + s.name + " needs at least two songs"; return false; }
        if (s.songGap >= songs || (talks > 0 && s.talkGap >= talks)) {
            error = "radio.cfg: station " + s.name + ": each gap of PROGRAMME must be smaller than its number of songs or clips";
            return false;
        }
    }
    return true;
}

// -------------------------------------------------------------------------------------
//  Programme
// -------------------------------------------------------------------------------------
void RadioProgramme::Reset(const RadioStationDef& station, uint32_t s) {
    st = &station; rng = Rng(s);
    songs.clear(); talks.clear(); queued.clear();
    lastTalk = true;
}

void RadioProgramme::Played(int item) {
    bool talk = st->items[item].kind != RadioClip::Song;
    std::vector<int>& recent = talk ? talks : songs;
    size_t keep = (size_t)(talk ? st->talkGap : st->songGap);
    recent.push_back(item);
    if (recent.size() > keep) recent.erase(recent.begin(), recent.end() - keep);
    lastTalk = talk;
}

RadioSlot RadioProgramme::Next() {
    RadioSlot slot;
    if (!queued.empty()) { slot.item = queued.front(); queued.erase(queued.begin()); }
    else slot.item = Pick();
    Played(slot.item);
    // A song longer than the excerpt is aired as a slice of it from a random point.
    const RadioItem& it = st->items[slot.item];
    slot.seconds = it.seconds;
    if (it.kind == RadioClip::Song && st->excerpt > 0 && it.seconds > st->excerpt) {
        slot.start = rng.Float() * (it.seconds - st->excerpt);
        slot.seconds = st->excerpt;
    }
    return slot;
}

int RadioProgramme::Pick() {
    bool hasTalk = false;
    for (const RadioItem& it : st->items) hasTalk = hasTalk || it.kind != RadioClip::Song;
    bool talk = !lastTalk && hasTalk && rng.Chance(st->talkShare);
    const std::vector<int>& recent = talk ? talks : songs;
    std::vector<int> pick;
    for (int i = 0; i < (int)st->items.size(); i++)
        if ((st->items[i].kind != RadioClip::Song) == talk && std::find(recent.begin(), recent.end(), i) == recent.end()) pick.push_back(i);
    return pick[rng.Int(0, (int)pick.size() - 1)];   // never empty: the gaps are smaller than the counts
}

// -------------------------------------------------------------------------------------
//  Stations
// -------------------------------------------------------------------------------------
bool RadioSystem::Init(bool audioReady, startup::Reporter* loading) {
    stations.assign(C.stations.size(), Station{});
    ok = audioReady && IsAudioDeviceReady();
    if (!ok) return true;              // silent; the clips were checked when the data loaded
    gCorner.store(C.bodyCorner);
    // Every clip must open (ADR-0010); its length is kept for the programme's measurements.
    for (RadioStationDef& s : C.stations)
        for (RadioItem& it : s.items) {
            Music m = LoadMusicStream(it.path.c_str());
            if (!IsMusicValid(m)) { TraceLog(LOG_ERROR, "RADIO: cannot open %s", it.path.c_str()); ok = false; return false; }
            it.seconds = GetMusicTimeLength(m);
            UnloadMusicStream(m);
        }
    AttachAudioMixedProcessor(MixTap);
    // Everything the player hears, recorded for a listening or a measurement of any run.
    if (const char* path = getenv("CJ_RADIO_MIX")) { mixPath = path; StartCapture(0); }
    for (int s = 0; s < (int)stations.size(); s++) Start(s, loading);
    return true;
}

void RadioSystem::Unload() {
    if (!ok) return;
    StopCapture();
    for (int s = 0; s < (int)stations.size(); s++) Drop(s);
    if (!mixPath.empty()) {
        DrainCapture(mix);
        WriteRadioWav(mixPath, mix, 3, (int)gRate.load(), false);
        TraceLog(LOG_INFO, "RADIO: the mix of the run (left, right, the first station) is in %s", mixPath.c_str());
    }
    for (int s = 0; s < (int)stations.size(); s++) {
        Station& st = stations[s];
        if (st.item < 0) continue;
        StopMusicStream(st.music);
        DetachAudioStreamProcessor(st.music.stream, PROCESSORS[s]);
        UnloadMusicStream(st.music);
        st = Station{};
    }
    DetachAudioMixedProcessor(MixTap);
    ok = false;
}

float RadioSystem::DeviceRate() const { return gRate.load(); }

// raylib does not report the device's sample rate, and the processors run at it: count the
// frames mixed over a second and take the nearest standard rate.
void RadioSystem::EstimateRate() {
    if (rateT0 == -2) return;
    int64_t f = gMixFrames.load();
    double t = GetTime();
    if (rateT0 < 0) { rateT0 = t; rateF0 = f; return; }
    if (t - rateT0 < 1.0 || f <= rateF0) return;
    double measured = (f - rateF0) / (t - rateT0), rate = measured;
    for (double r : { 8000.0, 11025.0, 16000.0, 22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        if (fabs(measured / r - 1) < 0.03) rate = r;
    gRate.store((float)rate);
    rateT0 = -2;
    TraceLog(LOG_INFO, "RADIO: device sample rate %.0f Hz (measured %.0f Hz)", rate, measured);
}

// A game joins each station at a random point of its programme: a worker seeks into the
// first song's slice while the title screen shows, and the broadcast holds until play begins.
void RadioSystem::Start(int s, startup::Reporter* loading) {
    Station& st = stations[s];
    uint32_t base = seed ? seed : (uint32_t)time(nullptr);
    st.programme.Reset(C.stations[s], base ^ Hash((uint32_t)s + 1, 77));
    st.started = true;
    st.paused = true;
    RadioSlot slot = st.programme.Next();
    Rng join(base ^ Hash((uint32_t)s + 1, 99));
    startup::AtomicSpan atomic(loading, "audio", "radio station");
    Prepare(s, &slot, slot.start + join.Range(0, 0.8f) * slot.seconds);
}

// Hands a clip's stream to the station's processor: its gain, where it plays from and to,
// and its fades. The old clip is stopped first: a processor must never see two streams.
void RadioSystem::Arm(int s, Music& m, const RadioSlot& slot, float from) {
    Station& st = stations[s];
    StationDsp& d = gDsp[s];
    if (st.item >= 0) {
        StopMusicStream(st.music);
        DetachAudioStreamProcessor(st.music.stream, PROCESSORS[s]);
        UnloadMusicStream(st.music);
        st.music = Music{}; st.item = -1;
    }
    const RadioStationDef& def = C.stations[s];
    const RadioItem& it = def.items[slot.item];
    bool slice = slot.seconds < it.seconds - 0.01f;   // a song's excerpt fades; a whole clip does not
    d.offsetS.store(from);
    d.endS.store(slot.start + slot.seconds);
    d.fadeFromS.store(slice && slot.start > 0 ? slot.start : -1.0);
    d.fadeInS.store(slice ? def.fadeIn : 0.0);
    d.fadeOutS.store(slice ? def.fadeOut : 0.0);
    d.itemGain.store(DbToGain(it.gainDb));
    d.itemSerial.fetch_add(1, std::memory_order_release);
    AttachAudioStreamProcessor(m.stream, PROCESSORS[s]);
    SetMusicVolume(m, 1.0f);
    st.music = m;
    st.item = slot.item;
    st.slot = slot;
    items++;
    TraceLog(LOG_INFO, "RADIO: %s plays %s from %.1f s for %.1f s", def.name.c_str(), GetFileName(it.path.c_str()), slot.start, slot.seconds);
}

// Plays a slot now, from a second of its file: a seek on the main thread, for loading and tests.
void RadioSystem::Play(int s, const RadioSlot& slot, float from) {
    const RadioItem& it = C.stations[s].items[slot.item];
    Music m = LoadMusicStream(it.path.c_str());
    if (!IsMusicValid(m)) { TraceLog(LOG_ERROR, "RADIO: cannot open %s", it.path.c_str()); return; }
    m.looping = false;
    Arm(s, m, slot, from);
    PlayMusicStream(m);
    if (from > 0) SeekMusicStream(m, from);
    UpdateMusicStream(m);
    if (stations[s].paused) PauseMusicStream(m);
}

// Opens the programme's next clip while the current one plays; a worker seeks into it, so
// that changing clips costs the main thread no more than opening a file.
void RadioSystem::Prepare(int s, const RadioSlot* given, float from) {
    Prepared& p = gPrep[s];
    if (p.active) return;
    RadioSlot slot = given ? *given : stations[s].programme.Next();
    if (from < 0) from = slot.start;
    const RadioItem& it = C.stations[s].items[slot.item];
    Music m = LoadMusicStream(it.path.c_str());
    if (!IsMusicValid(m)) { TraceLog(LOG_ERROR, "RADIO: cannot open %s", it.path.c_str()); return; }
    m.looping = false;
    p.music = m; p.slot = slot; p.from = from; p.active = true; p.ready.store(false);
    if (from > 0) p.worker = std::thread([&p] { SeekMusicStream(p.music, p.from); p.ready.store(true, std::memory_order_release); });
    else p.ready.store(true);
}

void RadioSystem::Switch(int s) {
    Prepared& p = gPrep[s];
    if (p.worker.joinable()) p.worker.join();
    Music m = p.music;
    RadioSlot slot = p.slot;
    float from = p.from;
    p.active = false;
    Arm(s, m, slot, from);
    PlayMusicStream(m);              // raylib's own count restarts; the decoder stays where it was sought
    UpdateMusicStream(m);
    if (stations[s].paused) PauseMusicStream(m);
    // The following clip is opened in the next frame: filling this one's buffers and opening a
    // file in the same frame would add up to about 7 ms.
}

void RadioSystem::Drop(int s) {
    Prepared& p = gPrep[s];
    if (p.worker.joinable()) p.worker.join();
    if (p.active) UnloadMusicStream(p.music);
    p.active = false;
    p.ready.store(false);
}

int RadioSystem::CarStation(const Vehicle& v) const {
    auto f = forced.find(v.serial);
    if (f != forced.end()) return f->second.first;
    if (C.stations.empty() || v.S().twoWheeler()) return -1;
    if (std::find(C.quiet.begin(), C.quiet.end(), v.cls) != C.quiet.end()) return -1;
    if (Unit(v.serial, 1) >= C.trafficShare) return -1;
    return (int)(Hash(v.serial, 2) % (uint32_t)C.stations.size());
}

float RadioSystem::CarCabinDb(const Vehicle& v) const {
    auto f = forced.find(v.serial);
    if (f != forced.end()) return f->second.second;
    return Unit(v.serial, 3) < C.loudShare ? C.cabinLoud : C.cabinNormal;
}

// The radio tunes in when the player gets in: to their own last choice in this car, else
// to the station the car's radio was on, else to one drawn for the car. A motorbike has none.
void RadioSystem::Entered(const Vehicle& v) {
    if (C.stations.empty() || v.S().twoWheeler()) { playerCar = 0; playerStation = -1; return; }
    playerCar = v.serial;
    auto t = tuned.find(v.serial);
    int own = CarStation(v);
    playerStation = t != tuned.end() ? t->second : own >= 0 ? own : (int)(Hash(v.serial, 4) % (uint32_t)C.stations.size());
    banner = playerStation >= 0 ? C.stations[playerStation].name : "Radio off";
    bannerTime = BANNER_S;
    TraceLog(LOG_INFO, "RADIO: the player gets into a %s, the radio tunes in to %s", v.S().name.c_str(), banner.c_str());
}

// The stations in order, then off.
void RadioSystem::Tune(int step) {
    if (playerCar == 0 || C.stations.empty()) return;
    int n = (int)C.stations.size(), at = playerStation < 0 ? n : playerStation;
    at = ((at + step) % (n + 1) + n + 1) % (n + 1);
    playerStation = at == n ? -1 : at;
    tuned[playerCar] = playerStation;
    banner = playerStation >= 0 ? C.stations[playerStation].name : "Radio off";
    bannerTime = BANNER_S;
}

const char* RadioSystem::Banner(float& alpha) const {
    if (bannerTime <= 0 || banner.empty()) return nullptr;
    alpha = Saturate(bannerTime / 0.6f) * Saturate((BANNER_S - bannerTime) / 0.15f);
    return banner.c_str();
}

void RadioSystem::Update(Game& g, float dt) {
    double t0 = GetTime();
    bannerTime = std::max(0.0f, bannerTime - dt);
    bool running = g.state == GameState::Playing || g.state == GameState::Wasted || g.state == GameState::Busted;
    bool hold = !running;                // the title and the pause menu hold the broadcast
    const Vehicle* car = g.player.inVehicle && g.player.vehicle >= 0 ? &g.vehicles[g.player.vehicle] : nullptr;
    if (!car || car->S().twoWheeler()) playerCar = 0;
    else if (car->serial != playerCar) Entered(*car);
    heard = running && car && playerCar != 0 && car->Drivable() ? playerStation : -1;
    if (!ok) return;
    EstimateRate();
    for (int s = 0; s < (int)stations.size(); s++) {
        Station& st = stations[s];
        StationDsp& d = gDsp[s];
        if (!st.started) Start(s, nullptr);
        if (hold != st.paused) {
            st.paused = hold;
            if (st.item >= 0) { if (hold) PauseMusicStream(st.music); else ResumeMusicStream(st.music); }
        }
        if (st.paused) continue;
        if (st.item >= 0) UpdateMusicStream(st.music);
        // The slot has been aired (a whole clip's stream only pads its end with silence now,
        // an excerpt has faded out): change to the prepared clip once its seek is done.
        bool counted = d.countedSerial.load(std::memory_order_acquire) == d.itemSerial.load();
        bool aired = st.item < 0 || !IsMusicStreamPlaying(st.music) || (counted && d.posS.load() >= st.slot.start + st.slot.seconds);
        if (!gPrep[s].active) Prepare(s);
        if (aired && gPrep[s].active && gPrep[s].ready.load(std::memory_order_acquire)) {
            int from = st.item;
            Switch(s);
            if (recordTransitions) { Transition tr; tr.station = s; tr.from = from; tr.to = st.item; tr.slot = st.slot; transitions.push_back(tr); }
        }
        int gs = d.gapSerial.load();
        for (Transition& tr : transitions)
            if (tr.station == s && tr.gapMs < 0 && gs == d.itemSerial.load()) {
                tr.gapMs = d.gapFrames.load() * 1000.0 / gRate.load();
                tr.startFrame = d.startFrame.load();
            }
        float target = s == heard ? C.inCar : 0.0f;
        st.dry = Lerpf(st.dry, target, Damp(target > st.dry ? DRY_IN : DRY_OUT, dt));
        if (target == 0 && st.dry < 1e-4f) st.dry = 0;
        if (target > 0 && target - st.dry < 1e-4f) st.dry = target;
        d.dry.store(st.dry);
        if (st.dry > 0) playerHeard += dt;
    }
    if (running) Voices(g, dt);
    if (!mixPath.empty()) DrainCapture(mix);
    double spent = GetTime() - t0;
    cpuMain += spent;
    worstUpdate = std::max(worstUpdate, spent);
    worstSinceTake = std::max(worstSinceTake, spent);
    frames++;
}

// Which radios in traffic the player hears, per station the loudest few, and how: the
// level just outside a car is its cabin level minus the body's loss, 6 dB less per
// doubling of the distance beyond the reference, and a body's loss again if the player
// sits in a car.
void RadioSystem::Voices(Game& g, float dt) {
    struct Cand { int idx; uint32_t serial; float level, d, dx; };
    static std::vector<Cand> cands[MAX_STATIONS];
    for (auto& c : cands) c.clear();
    Vector2 at = g.PlayerPos();
    bool inside = g.player.inVehicle && g.player.vehicle >= 0 && !g.vehicles[g.player.vehicle].S().twoWheeler();
    for (int i = 0; i < (int)g.vehicles.size(); i++) {
        const Vehicle& v = g.vehicles[i];
        if (!v.active || v.driver != DriverType::Traffic || !v.Drivable()) continue;
        int s = CarStation(v);
        if (s < 0 || s >= (int)stations.size()) continue;
        float d = Dist(v.pos, at) / cfg::M;
        float level = CarCabinDb(v) - C.bodyLoss - 20 * log10f(std::max(d, C.reference) / C.reference) - (inside ? C.bodyLoss : 0);
        if (level > C.hearing) cands[s].push_back({ i, v.serial, level, d, (v.pos.x - at.x) / cfg::M });
    }
    double now = GetTime();
    int active = 0;
    for (int s = 0; s < (int)stations.size(); s++) {
        std::vector<Cand>& c = cands[s];
        std::sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.level > b.level; });
        if (c.size() > (size_t)VOICES) c.resize(VOICES);
        std::vector<bool> taken(c.size(), false);
        Station& st = stations[s];
        auto aim = [&](int v, const Cand& k) {
            SharedVoice& sv = gDsp[s].voice[v];
            float gain = C.inCar * DbToGain(k.level - C.cabinNormal) * Saturate((k.level - C.hearing) / FADE_DB);
            float p = Clampf(k.dx / std::max(k.d, C.reference), -1, 1);   // left -1 .. right +1
            float a = (p + 1) * PI * 0.25f;   // constant power, unity per channel in the centre
            sv.gainL.store(gain * sqrtf(2.0f) * cosf(a)); sv.gainR.store(gain * sqrtf(2.0f) * sinf(a));
            sv.delay.store(k.d / SOUND_SPEED);
            sv.cabin.store(inside ? 1.0f : 0.0f);
            st.voice[v].level = k.level; st.voice[v].targetGain = gain;
        };
        for (int v = 0; v < VOICES; v++) {
            Voice& vo = st.voice[v];
            if (vo.vehicle < 0) continue;
            int found = -1;
            for (size_t k = 0; k < c.size(); k++) if (c[k].idx == vo.vehicle && c[k].serial == vo.serial) found = (int)k;
            if (found >= 0) { taken[found] = true; aim(v, c[found]); continue; }
            SharedVoice& sv = gDsp[s].voice[v];
            sv.gainL.store(0); sv.gainR.store(0);
            vo = Voice{}; vo.freeAt = now + RELEASE_S;
        }
        for (size_t k = 0; k < c.size(); k++) {
            if (taken[k]) continue;
            for (int v = 0; v < VOICES; v++) {
                Voice& vo = st.voice[v];
                if (vo.vehicle >= 0 || now < vo.freeAt) continue;
                vo.vehicle = c[k].idx; vo.serial = c[k].serial;
                SharedVoice& sv = gDsp[s].voice[v];
                sv.delay.store(c[k].d / SOUND_SPEED);
                sv.epoch.fetch_add(1, std::memory_order_release);
                aim(v, c[k]);
                break;
            }
        }
        for (const Voice& vo : st.voice) active += vo.vehicle >= 0;
    }
    maxVoices = std::max(maxVoices, active);
    voiceSeconds += active * dt;
}

// -------------------------------------------------------------------------------------
//  Measurement hooks
// -------------------------------------------------------------------------------------
void RadioSystem::Force(uint32_t serial, int station, float cabinDb) { forced[serial] = { station, cabinDb }; }

void RadioSystem::StartAt(int s, int item, float seconds, const std::vector<int>& then) {
    if (!ok || s < 0 || s >= (int)stations.size()) return;
    Station& st = stations[s];
    if (!st.started) { st.programme.Reset(C.stations[s], seed ? seed : 1); st.started = true; }
    Drop(s);
    for (int next : then) st.programme.Queue(next);
    RadioSlot slot;
    slot.item = item; slot.start = 0; slot.seconds = C.stations[s].items[item].seconds;
    Play(s, slot, seconds);
    Prepare(s);
}

void RadioSystem::SetExcerpt(int s, float seconds, float fadeIn, float fadeOut) {
    if (s < 0 || s >= (int)C.stations.size()) return;
    C.stations[s].excerpt = seconds; C.stations[s].fadeIn = fadeIn; C.stations[s].fadeOut = fadeOut;
    if (ok) Drop(s);                   // the prepared clip was cut with the old excerpt
}

double RadioSystem::TakeWorstUpdateMs() { double w = worstSinceTake * 1000.0; worstSinceTake = 0; return w; }

int RadioSystem::CurrentItem(int s) const { return s >= 0 && s < (int)stations.size() ? stations[s].item : -1; }

RadioSystem::VoiceProbe RadioSystem::Probe(int s, int v) const {
    VoiceProbe p;
    if (s < 0 || s >= (int)stations.size() || v < 0 || v >= VOICES) return p;
    const SharedVoice& sv = gDsp[s].voice[v];
    p.vehicle = stations[s].voice[v].vehicle;
    p.gain = sv.nowGain.load(); p.delay = sv.nowDelay.load(); p.cabin = sv.nowCabin.load();
    p.targetGain = stations[s].voice[v].targetGain; p.level = stations[s].voice[v].level;
    if (p.vehicle < 0) { p.targetGain = 0; p.level = -999; }
    return p;
}

void RadioSystem::StartCapture(int s) {
    StopCapture();
    // Let a callback that may still be writing finish before the buffer is reset.
    uint64_t cb = gCallbacks.load();
    for (double until = GetTime() + 0.2; gCallbacks.load() < cb + 2 && GetTime() < until;) {}
    if (gCap.ring.empty()) gCap.ring.assign(1u << 22, 0.0f);
    gCap.head.store(0); gCap.tail.store(0); gCap.dropped.store(0); gCap.startMix.store(-1);
    gCap.station.store(s);
    gCap.on.store(true, std::memory_order_release);
}

void RadioSystem::StopCapture() { gCap.on.store(false, std::memory_order_release); }

int64_t RadioSystem::CaptureStart() const { return gCap.startMix.load(); }

size_t RadioSystem::DrainCapture(std::vector<float>& out) {
    size_t size = gCap.ring.size();
    if (size == 0) return 0;
    size_t head = gCap.head.load(std::memory_order_acquire), tail = gCap.tail.load(std::memory_order_relaxed);
    for (size_t i = tail; i < head; i++) out.push_back(gCap.ring[i % size]);
    gCap.tail.store(head, std::memory_order_release);
    return (head - tail) / 3;
}

std::vector<RadioSystem::Transition> RadioSystem::TakeTransitions() {
    std::vector<Transition> done;
    for (const Transition& t : transitions) if (t.gapMs >= 0) done.push_back(t);
    transitions.erase(std::remove_if(transitions.begin(), transitions.end(), [](const Transition& t) { return t.gapMs >= 0; }),
                      transitions.end());
    return done;
}

void RadioSystem::LogSummary() const {
    double f = (double)std::max<int64_t>(1, frames);
    double mixed = gMixFrames.load() / std::max(1.0f, gRate.load());
    TraceLog(LOG_INFO, "RADIO: stations %d, clips started %d, device %.0f Hz | main thread %.4f ms per frame, worst %.2f ms | audio thread %.3f %% of a core | "
             "voices at once max %d, voice-seconds %.1f | the player's radio %.1f s | dropped capture frames %lld",
             (int)stations.size(), items, gRate.load(), cpuMain * 1000.0 / f, worstUpdate * 1000.0,
             mixed > 0 ? gDspNanos.load() / 1e9 / mixed * 100.0 : 0.0, maxVoices, voiceSeconds, playerHeard, (long long)gCap.dropped.load());
}
