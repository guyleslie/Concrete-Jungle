// =====================================================================================
//  Radio scenes (CJ-040) - see radio_tests.h.
//
//  Test file records (tests/radio.cfg):
//    SCHEDULE <hours> <seeds>
//    PASS  <name> <km/h> <side m> <cabin dB> <foot|car>
//    INCAR <name> <clip> <clip after it> <song after that>
//  The fixture checks the programme and the gaps between clips itself; the recordings of
//  the passing cars are measured by tools/run_cj040.py.
// =====================================================================================
#include "radio_tests.h"
#include "game.h"
#include "radio.h"
#include "datafile.h"
#include "vehicle_types.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr float M = cfg::M;
constexpr float KMH = cfg::M / 3.6f;          // px/s per km/h
constexpr double WARMUP_S = 1.5;              // the device's sample rate is measured in the first second
constexpr double SETTLE_S = 0.6;              // after joining a clip, before a scene records
constexpr float MARGIN_M = 8;                 // a passing car starts and ends this far beyond hearing
constexpr float LEAD_S = 6;                   // an in-car scene joins its first clip this long before the end
constexpr double TAIL_S = 4;                  // and records this much of its last song
constexpr double GAP_MS = 50;                 // the most silence allowed between two clips
const char* OUT_DIR = "build/radio";

// The screen's left part is the fixture's panel; the scene is drawn centred beside it.
float UiScale() { return Clampf(GetScreenHeight() / 900.0f, 0.75f, 1.6f); }
float PanelWidth() { return std::min(600.0f * UiScale(), GetScreenWidth() * 0.42f); }

enum class Kind { Pass, InCar };
struct Scene {
    Kind kind = Kind::Pass;
    std::string name;
    float kmh = 0, side = 0, cabin = 80;      // a passing car: speed (negative: westwards), path, cabin level
    bool inCar = false;                       // the player listens from a car with its radio off
    int clips[3] = { -1, -1, -1 };            // in the car: the first clip, the clip after it, the song after that
};

bool Number(const std::string& s, float& out) {
    char* end = nullptr;
    out = strtof(s.c_str(), &end);
    return end != s.c_str() && *end == '\0' && std::isfinite(out);
}

int FindClip(const RadioStationDef& st, const std::string& file) {
    for (int i = 0; i < (int)st.items.size(); i++)
        if (file == GetFileName(st.items[i].path.c_str())) return i;
    return -1;
}

// A WAV file: 16-bit PCM to listen to, or 32-bit float to measure.
void WriteWav(const std::string& path, const std::vector<float>& data, int channels, int rate, bool pcm16) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { TraceLog(LOG_ERROR, "RADIOTEST cannot write %s", path.c_str()); return; }
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
} // namespace

struct RadioTests::State {
    std::string file, error;
    bool invalid = false, finished = false;
    int checks = 0, failures = 0;
    float hours = 24; int seeds = 10;
    std::vector<Scene> scenes;
    int index = -1;
    enum class Phase { Warmup, Settle, Record } phase = Phase::Warmup;
    double phaseStart = 0;
    // the running scene
    Vector2 centre{}, from{}, dir{};
    int car = -1;
    float travel = 0, range = 0;              // px the car drives; m out to which it is heard
    std::vector<float> capture;
    std::string csv;
    std::vector<RadioSystem::Transition> transitions;
    double lastChange = -1;
    float view = 40;                          // m of the world shown, top to bottom
    // What the screen shows while a scene runs: the model's numbers next to the radio's.
    struct Live {
        float d = 0, dx = 0, level = -999, model = 0, gain = 0, doppler = 1, meterL = -120, meterR = -120;
        bool heard = false;
    } live;
    std::vector<std::pair<double, float>> delays;   // (audio s, travel time s) for the Doppler factor
    std::string programme;                    // the programme check, for the screen

    void Check(bool pass) { checks++; failures += !pass; }

    bool Parse(const char* path) {
        if (!FileExists(path)) { error = std::string(path) + ": no such file"; return false; }
        const RadioConfig& C = RadioData();
        for (const DataRecord& r : ReadDataFile(path)) {
            auto bad = [&](const char* what) { error = TextFormat("%s line %d: %s", path, r.line, what); return false; };
            if (r.Is("SCHEDULE")) {
                float h, s;
                if (r.size() != 3 || !Number(r[1], h) || !Number(r[2], s) || h <= 0 || s < 1) return bad("SCHEDULE <hours> <seeds>");
                hours = h; seeds = (int)s;
            } else if (r.Is("PASS")) {
                Scene sc; sc.kind = Kind::Pass;
                if (r.size() != 6 || !Number(r[2], sc.kmh) || !Number(r[3], sc.side) || !Number(r[4], sc.cabin) || sc.kmh == 0 ||
                    (r[5] != "foot" && r[5] != "car")) return bad("PASS <name> <km/h> <side m> <cabin dB> <foot|car>");
                sc.name = r[1]; sc.inCar = r[5] == "car";
                scenes.push_back(sc);
            } else if (r.Is("INCAR")) {
                Scene sc; sc.kind = Kind::InCar;
                if (r.size() != 5) return bad("INCAR <name> <clip> <clip after it> <song after that>");
                sc.name = r[1];
                for (int k = 0; k < 3; k++) {
                    sc.clips[k] = FindClip(C.stations[0], r[2 + k]);
                    if (sc.clips[k] < 0) return bad(TextFormat("no clip %s on the first station", r[2 + k].c_str()));
                }
                scenes.push_back(sc);
            } else return bad("unknown record");
        }
        return true;
    }

    // The programme over many hours, from the clip lengths: never two clips in a row, no
    // song or clip again within its gap, the share of songs followed by a clip, everything
    // played.
    void Schedule() {
        const RadioConfig& C = RadioData();
        for (const RadioStationDef& st : C.stations) {
            int n = (int)st.items.size(), consecutive = 0, songGap = INT_MAX, talkGap = INT_MAX, unplayed = 0;
            double shareMin = 1, shareMax = 0, backMin = 1e9, backSum = 0; int backs = 0, clips = 0;
            for (int seed = 1; seed <= seeds; seed++) {
                RadioProgramme p;
                p.Reset(st, 0xCA400000u + (uint32_t)seed * 7919u);
                std::vector<int> played(n, 0), lastAt(n, -1);
                std::vector<double> lastT(n, -1);
                int songs = 0, talks = 0, gaps = 0, talkGaps = 0, prev = -1;
                for (double t = 0; t < hours * 3600.0;) {
                    int i = p.Next();
                    bool talk = st.items[i].kind != RadioClip::Song;
                    bool prevTalk = prev >= 0 && st.items[prev].kind != RadioClip::Song;
                    if (talk && prevTalk) consecutive++;
                    if (prev >= 0 && !prevTalk) { gaps++; talkGaps += talk; }
                    int at = talk ? talks++ : songs++;
                    if (lastAt[i] >= 0) {
                        int others = at - lastAt[i] - 1;
                        (talk ? talkGap : songGap) = std::min(talk ? talkGap : songGap, others);
                        if (!talk) { backMin = std::min(backMin, (t - lastT[i]) / 60); backSum += (t - lastT[i]) / 60; backs++; }
                    }
                    lastAt[i] = at; lastT[i] = t; played[i]++; clips++;
                    t += st.items[i].seconds;
                    prev = i;
                }
                double share = gaps ? (double)talkGaps / gaps : 0;
                shareMin = std::min(shareMin, share); shareMax = std::max(shareMax, share);
                for (int k = 0; k < n; k++) unplayed += played[k] == 0;
            }
            bool pass[5] = { consecutive == 0, songGap >= st.songGap, talkGap >= st.talkGap,
                             shareMin >= 0.55 && shareMax <= 0.75, unplayed == 0 };
            for (bool b : pass) Check(b);
            bool all = pass[0] && pass[1] && pass[2] && pass[3] && pass[4];
            programme += TextFormat("%s, %d runs of %.0f h\n", st.name.c_str(), seeds, hours);
            programme += TextFormat("two clips in a row: %d\n", consecutive);
            programme += TextFormat("a song back after %d others at least (rule %d)\n", songGap, st.songGap);
            programme += TextFormat("a clip back after %d others at least (rule %d)\n", talkGap, st.talkGap);
            programme += TextFormat("a clip after %.0f-%.0f %% of the songs (rule 55-75 %%)\n", shareMin * 100, shareMax * 100);
            programme += TextFormat("clips never played: %d\n", unplayed);
            programme += all ? "PASS\n" : "FAIL\n";
            TraceLog(LOG_INFO, "RADIOTEST schedule station=\"%s\" hours=%.0f seeds=%d clips=%d consecutive_clips=%d song_gap_min=%d rule=%d "
                     "talk_gap_min=%d rule=%d talk_share=%.3f-%.3f target=%.2f song_back_min=%.1f avg=%.1f minutes unplayed=%d %s",
                     st.name.c_str(), hours, seeds, clips, consecutive, songGap, st.songGap, talkGap, st.talkGap, shareMin, shareMax,
                     st.talkShare, backMin, backs ? backSum / backs : 0.0, unplayed, all ? "PASS" : "FAIL");
        }
    }

    const Scene& S() const { return scenes[index]; }

    // Hearing range of a radio at a cabin level (m; 0: never heard).
    float Range(float cabin, bool inCar) const {
        const RadioConfig& C = RadioData();
        float atReference = cabin - C.bodyLoss - (inCar ? C.bodyLoss : 0);
        return atReference <= C.hearing ? 0.0f : C.reference * powf(10.0f, (atReference - C.hearing) / 20.0f);
    }

    void Begin(Game& g) {
        const Scene& s = S();
        const RadioConfig& C = RadioData();
        g.vehicles.clear(); g.peds.clear(); g.pickups.clear(); g.deathSpots.clear();
        g.fx.Clear(); g.toasts.clear(); g.bigTimer = 0;
        g.pedGrid.Build(g.peds);
        g.state = GameState::Playing; g.heat = 0; g.autoMode = 0;
        g.player.inVehicle = false; g.player.vehicle = -1;
        g.player.pos = centre; g.player.vel = V2(0, 0); g.player.health = 100;
        int taxi = gAssets.byClass[FindVehicleClass("Taxi")][0];
        car = -1;
        g.radio.recordTransitions = s.kind == Kind::InCar;
        g.radio.TakeTransitions();
        transitions.clear(); lastChange = -1;
        if (s.kind == Kind::InCar || s.inCar) {          // the player sits in a car
            int own = g.SpawnVehicle(taxi, centre, 0, DriverType::Player);
            g.player.inVehicle = true; g.player.vehicle = own;
            g.radio.SetTuned(g.vehicles[own].serial, s.kind == Kind::InCar ? 0 : -1);
        }
        if (s.kind == Kind::Pass) {
            range = Range(s.cabin, s.inCar);
            float reach = std::max(range, fabsf(s.side) + 1) + MARGIN_M;
            float half = sqrtf(std::max(0.0f, reach * reach - s.side * s.side)) * M;
            dir = V2(s.kmh > 0 ? 1.0f : -1.0f, 0);
            from = centre + V2(-dir.x * half, s.side * M);
            travel = 2 * half;
            car = g.SpawnVehicle(taxi, from, s.kmh > 0 ? PI / 2 : -PI / 2, DriverType::Traffic);
            g.radio.Force(g.vehicles[car].serial, 0, s.cabin);
            int song = 0;
            for (int i = 0, k = 0; i < (int)C.stations[0].items.size(); i++)
                if (C.stations[0].items[i].kind == RadioClip::Song && k++ == index % 8) song = i;
            g.radio.StartAt(0, song, 30);
        } else {
            const RadioItem& first = C.stations[0].items[s.clips[0]];
            g.radio.StartAt(0, s.clips[0], std::max(0.0f, first.seconds - LEAD_S));
            g.radio.QueueNext(0, s.clips[1]);
            g.radio.QueueNext(0, s.clips[2]);
        }
        view = s.kind == Kind::InCar ? 24.0f : Clampf(2.3f * (std::max(range, 6.0f) + 3), 24, 125);
        live = Live{}; delays.clear();
        phase = Phase::Settle; phaseStart = GetTime();
    }

    void Record(Game& g, double t) {
        const Scene& s = S();
        if (s.kind == Kind::Pass && car >= 0) {
            Vehicle& v = g.vehicles[car];
            v.pos = from + dir * std::min(travel, (float)(t * fabsf(s.kmh) * KMH));
            v.vel = dir * (fabsf(s.kmh) * KMH);
        }
    }

    void Probe(Game& g, double t) {
        const Scene& s = S();
        g.radio.DrainCapture(capture);
        size_t captured = capture.size() / 3;
        if (s.kind == Kind::Pass && car >= 0) {
            RadioSystem::VoiceProbe p;
            for (int k = 0; k < RadioSystem::VOICES; k++) {
                RadioSystem::VoiceProbe q = g.radio.Probe(0, k);
                if (q.vehicle == car || (p.vehicle < 0 && q.gain > p.gain)) p = q;
            }
            const Vehicle& v = g.vehicles[car];
            const RadioConfig& C = RadioData();
            live.d = Dist(v.pos, g.PlayerPos()) / M;
            live.dx = (v.pos.x - g.PlayerPos().x) / M;
            live.level = s.cabin - C.bodyLoss - 20 * log10f(std::max(live.d, C.reference) / C.reference) - (s.inCar ? C.bodyLoss : 0);
            live.model = live.level > C.hearing
                ? C.inCar * powf(10.0f, (live.level - C.cabinNormal) / 20) * Saturate((live.level - C.hearing) / 6) : 0.0f;
            live.gain = p.gain;
            live.heard = p.vehicle == car;
            double at = captured / (double)g.radio.DeviceRate();
            delays.push_back({ at, p.delay });
            while (delays.size() > 2 && at - delays.front().first > 0.15) delays.erase(delays.begin());
            double span = delays.back().first - delays.front().first;
            live.doppler = live.heard && span > 0.08 ? (float)(1 - (delays.back().second - delays.front().second) / span) : 1.0f;
            csv += TextFormat("%zu,%.4f,%.4f,%.4f,%.3f,%.9g,%.9g,%.9g,%.4f\n", captured, t, Dist(v.pos, g.PlayerPos()) / M,
                              (v.pos.x - g.PlayerPos().x) / M, p.level, p.targetGain, p.gain, p.delay, p.cabin);
        }
        for (const RadioSystem::Transition& tr : g.radio.TakeTransitions()) { transitions.push_back(tr); lastChange = t; }
        // The output level of the last 2,048 frames, left and right.
        size_t frames = std::min<size_t>(captured, 2048);
        double l = 0, r = 0;
        for (size_t i = captured - frames; i < captured; i++) { l += capture[i * 3] * capture[i * 3]; r += capture[i * 3 + 1] * capture[i * 3 + 1]; }
        live.meterL = frames ? 10 * log10f((float)(l / frames) + 1e-12f) : -120;
        live.meterR = frames ? 10 * log10f((float)(r / frames) + 1e-12f) : -120;
    }

    bool Done(Game& g, double t) const {
        (void)g;
        const Scene& s = S();
        if (s.kind == Kind::Pass) return t * fabsf(s.kmh) * KMH >= travel;
        return transitions.size() >= 2 && t - lastChange >= TAIL_S;
    }

    void End(Game& g) {
        const Scene& s = S();
        g.radio.StopCapture();
        g.radio.DrainCapture(capture);
        float rate = g.radio.DeviceRate();
        std::string base = std::string(OUT_DIR) + "/" + s.name;
        std::vector<float> mix;
        mix.reserve(capture.size() / 3 * 2);
        for (size_t i = 0; i + 2 < capture.size(); i += 3) { mix.push_back(capture[i]); mix.push_back(capture[i + 1]); }
        WriteWav(base + ".wav", mix, 2, (int)rate, true);
        WriteWav(base + "_data.wav", capture, 3, (int)rate, false);
        const RadioConfig& C = RadioData();
        if (s.kind == Kind::Pass) {
            SaveFileText((base + ".csv").c_str(), (char*)("frame,t,d_m,dx_m,level_db,target_gain,gain,delay_s,cabin\n" + csv).c_str());
            TraceLog(LOG_INFO, "RADIOTEST scene=%s kind=pass kmh=%.0f side_m=%.1f cabin_db=%.0f listener=%s range_m=%.2f frames=%zu rate=%.0f "
                     "master=%.3f incar=%.3f files=%s",
                     s.name.c_str(), s.kmh, s.side, s.cabin, s.inCar ? "car" : "foot", range, capture.size() / 3, rate,
                     GetMasterVolume(), C.inCar, base.c_str());
        } else {
            int64_t start = g.radio.CaptureStart();
            std::string bounds;
            bool pass = transitions.size() >= 2;
            for (const RadioSystem::Transition& tr : transitions) {
                bool ok = tr.gapMs >= 0 && tr.gapMs <= GAP_MS;
                pass = pass && ok;
                Check(ok);
                TraceLog(LOG_INFO, "RADIOTEST transition scene=%s from=%s to=%s gap_ms=%.2f start_frame=%lld %s", s.name.c_str(),
                         GetFileName(C.stations[0].items[tr.from].path.c_str()), GetFileName(C.stations[0].items[tr.to].path.c_str()),
                         tr.gapMs, (long long)(tr.startFrame - start), ok ? "PASS" : "FAIL");
                bounds += TextFormat("%s%lld", bounds.empty() ? "" : ",", (long long)(tr.startFrame - start));
            }
            if (transitions.size() < 2) Check(false);
            TraceLog(LOG_INFO, "RADIOTEST scene=%s kind=incar clips=%s,%s,%s starts=%s frames=%zu rate=%.0f master=%.3f incar=%.3f files=%s %s",
                     s.name.c_str(), GetFileName(C.stations[0].items[s.clips[0]].path.c_str()),
                     GetFileName(C.stations[0].items[s.clips[1]].path.c_str()), GetFileName(C.stations[0].items[s.clips[2]].path.c_str()),
                     bounds.c_str(), capture.size() / 3, rate, GetMasterVolume(), C.inCar, base.c_str(), pass ? "PASS" : "FAIL");
        }
        capture.clear(); csv.clear();
    }
};

RadioTests::RadioTests() = default;
RadioTests::~RadioTests() = default;

bool RadioTests::Init(Game& g, const char* testFile) {
    state = std::make_unique<State>();
    State& s = *state;
    s.file = testFile ? testFile : "tests/radio.cfg";
    bool lengths = true;
    for (const RadioStationDef& st : RadioData().stations)
        for (const RadioItem& it : st.items) lengths = lengths && it.seconds > 0;
    if (!lengths) s.error = "the radio scenes need an audio device";
    else if (!s.Parse(s.file.c_str())) {}
    if (!s.error.empty()) {
        s.invalid = true;
        TraceLog(LOG_ERROR, "RADIOTEST invalid: %s", s.error.c_str());
        return false;
    }
    MakeDirectory(OUT_DIR);
    g.map.ResetTestGround(Tile::Road);
    g.dn.hour = 13.0f; g.dn.timeScale = 0; g.dn.Update(0);
    g.showHelp = false;
    g.testView = true;                                 // the world and this fixture's panel only
    g.state = GameState::Playing;
    s.centre = V2(cfg::WORLD_W * 0.5f, cfg::WORLD_H * 0.5f);
    g.vehicles.clear(); g.peds.clear();
    g.player.inVehicle = false; g.player.vehicle = -1; g.player.pos = s.centre;
    TraceLog(LOG_INFO, "RADIOTEST run file=%s scenes=%d schedule_hours=%.0f seeds=%d build=%s_%s", s.file.c_str(), (int)s.scenes.size(),
             s.hours, s.seeds, __DATE__, __TIME__);
    s.Schedule();
    s.phase = State::Phase::Warmup;
    s.phaseStart = GetTime();
    if (s.scenes.empty()) s.finished = true;
    return true;
}

void RadioTests::Update(Game& g, float dt) {
    if (!state || state->invalid || state->finished) return;
    State& s = *state;
    double now = GetTime();
    if (s.phase == State::Phase::Warmup && now - s.phaseStart >= WARMUP_S) { s.index = 0; s.Begin(g); }
    else if (s.phase == State::Phase::Settle && now - s.phaseStart >= SETTLE_S) {
        g.radio.StartCapture(0);
        s.phase = State::Phase::Record; s.phaseStart = GetTime();
    }
    double t = now - s.phaseStart;
    if (s.phase == State::Phase::Record) s.Record(g, t);
    g.radio.Update(g, dt);
    g.cam.Snap(s.centre - V2(PanelWidth() * 0.5f * s.view * M / GetScreenHeight(), 0), s.view * M);
    if (s.phase != State::Phase::Record) return;
    s.Probe(g, t);
    if (s.Done(g, t)) {
        s.End(g);
        if (++s.index >= (int)s.scenes.size()) { s.finished = true; g.radio.StopCapture(); return; }
        s.Begin(g);
    }
}

// The world without the HUD; on it the hearing range and the passing car's path, and in a
// sidebar what the scene tests, with the model's numbers next to the radio's.
void RadioTests::Draw(Game& g) const {
    if (!state) return;
    const State& s = *state;
    if (s.invalid) {
        ClearBackground({ 20, 25, 30, 255 });
        DrawUITextShadow("CJ-040 radio scenes: invalid", 40, 60, 30, RED);
        DrawUITextShadow(s.error.c_str(), 40, 110, 22, WHITE);
        return;
    }
    g.Draw();
    float H = (float)GetScreenHeight(), ui = UiScale(), pw = PanelWidth();
    const RadioConfig& C = RadioData();
    bool running = s.index >= 0 && s.index < (int)s.scenes.size();
    const Scene* sc = running ? &s.scenes[s.index] : nullptr;
    const Color green = { 120, 220, 140, 255 }, red = { 255, 110, 90, 255 }, amber = { 255, 190, 90, 255 }, dim = ColorA(WHITE, 0.6f);

    // ---- on the ground ----
    if (sc && sc->kind == Kind::Pass) {
        Vector2 c = g.cam.WorldToScreen(s.centre, 0);
        auto ring = [&](float m, Color col, const char* label) {
            if (m <= 0) return;
            float r = Len(g.cam.WorldToScreen(s.centre + V2(m * M, 0), 0) - c);
            DrawRing(c, r - 1.5f * ui, r + 1.5f * ui, 0, 360, 128, col);
            DrawUITextShadow(label, c.x + r * 0.71f + 8 * ui, c.y - r * 0.71f - 22 * ui, 18 * ui, col, true);
        };
        float full = s.range > 0 ? s.range / powf(10.0f, 6 / 20.0f) : 0;   // the fade covers the last 6 dB
        ring(s.range, green, TextFormat("heard to %.1f m", s.range));
        if (full > fabsf(sc->side)) ring(full, ColorA(green, 0.5f), TextFormat("full level within %.1f m", full));
        Vector2 a = g.cam.WorldToScreen(s.from, 0), b = g.cam.WorldToScreen(s.from + s.dir * s.travel, 0);
        DrawLineEx(a, b, 2 * ui, ColorA(WHITE, 0.3f));
        if (s.car >= 0 && s.car < (int)g.vehicles.size()) {
            const Vehicle& v = g.vehicles[s.car];
            Vector2 p = g.cam.WorldToScreen(v.pos + V2(0, -v.width * 0.5f - 1.2f * M), v.height);
            const char* tag = TextFormat("radio %.0f dB", sc->cabin);
            DrawUITextShadow(tag, p.x - MeasureUIText(tag, 18 * ui, true) * 0.5f, p.y - 22 * ui, 18 * ui, amber, true);
        }
    }

    // ---- the sidebar ----
    DrawRectangleRec({ 0, 0, pw, H }, ColorA({ 10, 12, 16, 255 }, 0.86f));
    float x = 28 * ui, y = 26 * ui, line = 27 * ui, size = 21 * ui;
    DrawUIText("CJ-040 RADIO SCENES", x, y, 18 * ui, amber, true);
    const char* counter = TextFormat("scene %d / %d", std::min(s.index + 1, (int)s.scenes.size()), (int)s.scenes.size());
    DrawUIText(counter, pw - 28 * ui - MeasureUIText(counter, 18 * ui, true), y, 18 * ui, dim, true);
    y += line * 1.4f;
    auto text = [&](const char* t, Color col = WHITE, float sz = 0) { DrawUIText(t, x, y, sz > 0 ? sz : size, col, true); y += line; };
    auto gap = [&](float f = 0.5f) { y += line * f; };
    auto row = [&](const char* label, const char* value, Color col = WHITE) {
        DrawUIText(label, x, y, size, dim, true);
        DrawUIText(value, x + 130 * ui, y, size, col, true);
        y += line;
    };
    auto meters = [&](float l, float r) {
        float mw = pw - 230 * ui;
        for (int k = 0; k < 2; k++) {
            float db = k == 0 ? l : r, by = y + 5 * ui, bx = x + 130 * ui;
            DrawUIText(k == 0 ? "output L" : "output R", x, y, size, dim, true);
            DrawRectangleRec({ bx, by, mw, 12 * ui }, ColorA(BLACK, 0.6f));
            DrawRectangleRec({ bx, by, mw * Saturate((db + 72) / 72), 12 * ui }, green);   // -72 .. 0 dBFS
            DrawUIText(db > -119 ? TextFormat("%.0f dB", db) : "silent", bx + mw + 8 * ui, y, 18 * ui, dim, true);
            y += line;
        }
    };
    if (!sc) {
        text(s.finished ? "All scenes recorded." : "Measuring the sound device ...", WHITE, 26 * ui);
        gap();
        text("The programme, simulated from the clip lengths:", dim);
        const char* p = s.programme.c_str();
        while (*p) {
            const char* e = strchr(p, '\n');
            std::string one(p, e ? (size_t)(e - p) : strlen(p));
            bool verdict = one == "PASS" || one == "FAIL";
            text(one.c_str(), verdict ? (one == "PASS" ? green : red) : WHITE);
            if (!e) break;
            p = e + 1;
        }
        if (s.finished) { gap(); text("Measure the recordings:", dim); text("python tools/run_cj040.py --analyse", WHITE, 18 * ui); }
        return;
    }
    text(sc->name.c_str(), WHITE, 30 * ui);
    gap(0.3f);
    if (sc->kind == Kind::Pass) {
        text(TextFormat("A taxi plays %s,", C.stations[0].name.c_str()), ColorA(WHITE, 0.85f));
        text(TextFormat("%s: %.0f dB in its cabin.", sc->cabin >= C.cabinLoud ? "loud" : "normal", sc->cabin), ColorA(WHITE, 0.85f));
        text(TextFormat("It passes the player %s", sc->inCar ? "in a car (radio off)" : "on foot"), ColorA(WHITE, 0.85f));
        text(TextFormat("at %.0f km/h, %.0f m away.", fabsf(sc->kmh), fabsf(sc->side)), ColorA(WHITE, 0.85f));
        gap();
        text("Expected", dim);
        row("heard", s.range > fabsf(sc->side) ? TextFormat("out to %.1f m", s.range) : "never");
        row("sound", sc->inCar ? "through two car bodies" : "through the car's body");
        row("pitch", TextFormat("%+.1f %% coming, %+.1f %% going", fabsf(sc->kmh) / 3.6f / 343 * 100, -fabsf(sc->kmh) / 3.6f / 343 * 100));
        gap();
        text("Now", dim);
        const State::Live& L = s.live;
        row("distance", TextFormat("%.1f m, %s", L.d, L.dx < 0 ? "left" : "right"));
        row("level", L.level > -900 ? TextFormat("%.1f dB (heard above %.0f)", L.level, C.hearing) : "-",
            L.level > C.hearing ? green : dim);
        auto db = [](float v) { return v > 1e-7f ? TextFormat("%.1f dB", 20 * log10f(v)) : "silent"; };
        std::string model = db(L.model);
        row("gain", TextFormat("model %s, radio %s", model.c_str(), db(L.gain)));
        row("pitch", L.heard ? TextFormat("%+.2f %% (%s)", (L.doppler - 1) * 100, L.doppler > 1 ? "coming" : "going") : "-");
        meters(L.meterL, L.meterR);
    } else {
        auto name = [&](int item) { return GetFileName(C.stations[0].items[item].path.c_str()); };
        text("The player sits in a car tuned to", ColorA(WHITE, 0.85f));
        text(TextFormat("%s. It joins %s", C.stations[0].name.c_str(), name(sc->clips[0])), ColorA(WHITE, 0.85f));
        text(TextFormat("%.0f s before its end, then plays", (double)LEAD_S), ColorA(WHITE, 0.85f));
        text(TextFormat("%s and %s.", name(sc->clips[1]), name(sc->clips[2])), ColorA(WHITE, 0.85f));
        gap();
        text("Expected", dim);
        row("changes", TextFormat("at most %.0f ms of silence", GAP_MS));
        row("loudness", TextFormat("every clip at %.0f LUFS", C.loudness));
        gap();
        text("Now", dim);
        int now = g.radio.CurrentItem(0);
        row("playing", now >= 0 ? name(now) : "-", amber);
        for (const RadioSystem::Transition& tr : s.transitions) {
            bool ok = tr.gapMs >= 0 && tr.gapMs <= GAP_MS;
            row("change", TextFormat("%.1f ms  %s", tr.gapMs, ok ? "PASS" : "FAIL"), ok ? green : red);
        }
        meters(s.live.meterL, s.live.meterR);
    }
    gap();
    text("Recorded to build/radio/; measured after the run", ColorA(WHITE, 0.45f), 17 * ui);
    text("by tools/run_cj040.py", ColorA(WHITE, 0.45f), 17 * ui);
}

void RadioTests::Log() const {
    if (!state) return;
    const State& s = *state;
    TraceLog(LOG_INFO, "RADIOTEST summary scenes=%d done=%d checks=%d failed=%d invalid=%d finished=%d", (int)s.scenes.size(),
             std::min(s.index, (int)s.scenes.size()), s.checks, s.failures, (int)s.invalid, (int)s.finished);
}

bool RadioTests::Failed() const { return !state || state->invalid || state->failures > 0; }
bool RadioTests::Finished() const { return state && state->finished; }
