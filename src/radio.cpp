// =====================================================================================
//  Car radio (CJ-040) - see radio.h.
//
//  Threads: the main thread runs the programme, opens the clips and decides, once a
//  frame, what the player hears of each station (their own radio, and which cars in
//  traffic at what gain, direction, travel time and through how many car bodies). It
//  hands those targets to the audio thread in atomics; the station's stream processor
//  moves towards them smoothly, sample by sample, so nothing clicks.
// =====================================================================================
#include "radio.h"
#include "game.h"
#include "vehicle_types.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>

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
    std::atomic<int64_t> itemLength{ 0 }, itemOffset{ 0 };   // device frames
    SharedVoice voice[VOICES];
    // audio thread -> main thread
    std::atomic<int> countedSerial{ 0 };
    std::atomic<int64_t> itemFrames{ 0 }, gapFrames{ -1 }, startFrame{ -1 };
    std::atomic<int> gapSerial{ 0 };
    // audio thread only
    float ring[RING]{};
    uint32_t w = 0;
    float dryNow = 0, rate = 0, corner = 0;
    struct Local { float gL = 0, gR = 0, delay = 0, cabin = 0; uint32_t epoch = 0; Biquad body, own; } local[VOICES];
    int counting = 0;
    int64_t counted = 0, endFrame = -1;
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
            counting = serial; counted = itemOffset.load(); endFrame = -1;
        }
        int64_t length = itemLength.load(std::memory_order_relaxed);
        if (endFrame < 0 && length > 0 && counted + (int64_t)n >= length) endFrame = blockStart + (length - counted);
        counted += n;
        itemFrames.store(counted, std::memory_order_relaxed);
        countedSerial.store(serial, std::memory_order_release);

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
            if (capture && gCap.staged < sizeof(gCap.stage) / sizeof(float)) gCap.stage[gCap.staged++] = mono;
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
    bool programme = false;
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
            if ((int)C.stations.size() == RadioSystem::MAX_STATIONS)
                return fail(r, TextFormat("at most %d stations", RadioSystem::MAX_STATIONS));
            C.stations.push_back({});
            st = &C.stations.back();
            st->folder = r[1];
            for (size_t i = 2; i < r.size(); i++) st->name += (i > 2 ? " " : "") + r[i];
            programme = false;
        } else if (r.Is("PROGRAMME")) {
            if (!st) return fail(r, "PROGRAMME before any STATION");
            if (programme) return fail(r, "a second PROGRAMME for station " + st->name);
            if (r.size() != 4) return fail(r, "PROGRAMME takes 3 values");
            if (!value(r, 1, 0, 1, st->talkShare) || !value(r, 2, 1, 99, b) || !value(r, 3, 0, 99, c)) return false;
            if (b != floorf(b) || c != floorf(c)) return fail(r, "the gaps of PROGRAMME are whole numbers");
            st->songGap = (int)b; st->talkGap = (int)c;
            programme = true;
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

int RadioProgramme::Next() {
    if (!queued.empty()) { int item = queued.front(); queued.erase(queued.begin()); Played(item); return item; }
    bool hasTalk = false;
    for (const RadioItem& it : st->items) hasTalk = hasTalk || it.kind != RadioClip::Song;
    bool talk = !lastTalk && hasTalk && rng.Chance(st->talkShare);
    const std::vector<int>& recent = talk ? talks : songs;
    std::vector<int> pick;
    for (int i = 0; i < (int)st->items.size(); i++)
        if ((st->items[i].kind != RadioClip::Song) == talk && std::find(recent.begin(), recent.end(), i) == recent.end()) pick.push_back(i);
    int item = pick[rng.Int(0, (int)pick.size() - 1)];   // never empty: the gaps are smaller than the counts
    Played(item);
    return item;
}

// -------------------------------------------------------------------------------------
//  Stations
// -------------------------------------------------------------------------------------
bool RadioSystem::Init(bool audioReady) {
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
    return true;
}

void RadioSystem::Unload() {
    if (!ok) return;
    StopCapture();
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
    for (int s = 0; s < (int)stations.size(); s++) {
        Station& st = stations[s];
        if (st.item < 0) continue;
        st.lengthFrames = (double)st.music.frameCount * rate / st.music.stream.sampleRate;
        gDsp[s].itemLength.store((int64_t)st.lengthFrames);
    }
    TraceLog(LOG_INFO, "RADIO: device sample rate %.0f Hz (measured %.0f Hz)", rate, measured);
}

void RadioSystem::Start(int s) {
    Station& st = stations[s];
    uint32_t base = seed ? seed : (uint32_t)time(nullptr);
    st.programme.Reset(C.stations[s], base ^ Hash((uint32_t)s + 1, 77));
    st.started = true;
    Play(s, st.programme.Next(), 0);
}

// Ends the station's clip (if any) and plays one from the given second. The old clip is
// stopped before the new one starts: a station's processor must never see two streams.
void RadioSystem::Play(int s, int item, float offset) {
    Station& st = stations[s];
    StationDsp& d = gDsp[s];
    if (st.item >= 0) {
        StopMusicStream(st.music);
        DetachAudioStreamProcessor(st.music.stream, PROCESSORS[s]);
        UnloadMusicStream(st.music);
        st.music = Music{}; st.item = -1;
    }
    const RadioItem& it = C.stations[s].items[item];
    Music m = LoadMusicStream(it.path.c_str());
    if (!IsMusicValid(m)) { TraceLog(LOG_ERROR, "RADIO: cannot open %s", it.path.c_str()); return; }
    m.looping = false;
    float rate = gRate.load();
    st.lengthFrames = (double)m.frameCount * rate / m.stream.sampleRate;
    d.itemLength.store((int64_t)st.lengthFrames);
    d.itemOffset.store((int64_t)(offset * rate));
    d.itemGain.store(DbToGain(it.gainDb));
    d.itemSerial.fetch_add(1, std::memory_order_release);
    AttachAudioStreamProcessor(m.stream, PROCESSORS[s]);
    SetMusicVolume(m, 1.0f);
    PlayMusicStream(m);
    if (offset > 0) SeekMusicStream(m, offset);
    UpdateMusicStream(m);
    if (st.paused) PauseMusicStream(m);
    st.music = m;
    st.item = item;
    items++;
    TraceLog(LOG_INFO, "RADIO: %s plays %s", C.stations[s].name.c_str(), GetFileName(it.path.c_str()));
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
    bool paused = g.state == GameState::Paused;
    const Vehicle* car = g.player.inVehicle && g.player.vehicle >= 0 ? &g.vehicles[g.player.vehicle] : nullptr;
    if (!car || car->S().twoWheeler()) playerCar = 0;
    else if (car->serial != playerCar) Entered(*car);
    heard = running && car && playerCar != 0 && car->Drivable() ? playerStation : -1;
    if (!ok) return;
    EstimateRate();
    for (int s = 0; s < (int)stations.size(); s++) {
        Station& st = stations[s];
        StationDsp& d = gDsp[s];
        if (!st.started) {
            if (!running) continue;
            Start(s);
        }
        if (paused != st.paused) {
            st.paused = paused;
            if (st.item >= 0) { if (paused) PauseMusicStream(st.music); else ResumeMusicStream(st.music); }
        }
        if (st.paused) continue;
        if (st.item >= 0) UpdateMusicStream(st.music);
        // The clip's content has been played: its stream only pads the end with silence now.
        bool counted = d.countedSerial.load(std::memory_order_acquire) == d.itemSerial.load();
        if (st.item < 0 || !IsMusicStreamPlaying(st.music) || (counted && d.itemFrames.load() >= (int64_t)st.lengthFrames)) {
            int from = st.item;
            Play(s, st.programme.Next(), 0);
            if (recordTransitions) transitions.push_back({ s, from, st.item, -1 });
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
    }
    if (running) Voices(g, dt);
    cpuMain += GetTime() - t0;
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

void RadioSystem::StartAt(int s, int item, float seconds) {
    if (!ok || s < 0 || s >= (int)stations.size()) return;
    Station& st = stations[s];
    if (!st.started) { st.programme.Reset(C.stations[s], seed ? seed : 1); st.started = true; }
    Play(s, item, seconds);
}

void RadioSystem::QueueNext(int s, int item) { if (s >= 0 && s < (int)stations.size()) stations[s].programme.Queue(item); }

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
    TraceLog(LOG_INFO, "RADIO: stations %d, clips started %d, device %.0f Hz | main thread %.4f ms per frame | audio thread %.3f %% of a core | "
             "voices at once max %d, voice-seconds %.1f | dropped capture frames %lld",
             (int)stations.size(), items, gRate.load(), cpuMain * 1000.0 / f, mixed > 0 ? gDspNanos.load() / 1e9 / mixed * 100.0 : 0.0,
             maxVoices, voiceSeconds, (long long)gCap.dropped.load());
}
