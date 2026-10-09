// =====================================================================================
//  Procedural audio - see audio.h
// =====================================================================================
#include "audio.h"
#include "math_utils.h"
#include "startup_loading.h"
#include <cstring>
#include <utility>

static const int SR = 44100;

// Synthesis scratch buffers are vectors, so cancellation between sample batches
// unwinds them without changing the committed audio resources or local noise seeds.
struct AudioCancelled {};
static void AudioPulse(startup::Reporter* loading, size_t completed) {
    if (loading && completed % 2048 == 0 && !loading->Pulse()) throw AudioCancelled{};
}

// ---- tiny synthesis helpers ----------------------------------------------------------
static float Noise(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFF) / 32767.5f - 1.0f; }

struct Synth {
    std::vector<float> buf;
    explicit Synth(float seconds) : buf((size_t)(seconds * SR), 0.0f) {}
    size_t N() const { return buf.size(); }
    std::vector<short> PCM(float gain = 1.0f, startup::Reporter* loading = nullptr) const {
        float peak = 1e-6f;
        size_t completed = 0;
        for (float v : buf) { peak = std::max(peak, fabsf(v)); AudioPulse(loading, ++completed); }
        std::vector<short> out(buf.size());
        for (size_t i = 0; i < buf.size(); i++) {
            out[i] = (short)(Clampf(buf[i] / peak * gain, -1, 1) * 32000);
            AudioPulse(loading, i + 1);
        }
        return out;
    }
};

// one-pole low pass in place
static void LowPass(std::vector<float>& b, float cutoff, startup::Reporter* loading = nullptr) {
    float a = 1.0f - expf(-2.0f * PI * cutoff / SR), y = 0;
    size_t completed = 0;
    for (float& v : b) { y += a * (v - y); v = y; AudioPulse(loading, ++completed); }
}
static void HighPass(std::vector<float>& b, float cutoff, startup::Reporter* loading = nullptr) {
    float a = 1.0f - expf(-2.0f * PI * cutoff / SR), y = 0;
    size_t completed = 0;
    for (float& v : b) { y += a * (v - y); v = v - y; AudioPulse(loading, ++completed); }
}

// Noise burst with exponential decay, filtered: gunshots, crashes, explosions.
static std::vector<short> Burst(float seconds, float decay, float lp, float hp, float bodyHz, float bodyAmt, uint32_t seed, startup::Reporter* loading = nullptr) {
    Synth s(seconds);
    for (size_t i = 0; i < s.N(); i++) {
        AudioPulse(loading, i);
        float t = i / (float)SR;
        float env = expf(-t * decay);
        float body = sinf(2 * PI * bodyHz * t * (1.0f - 0.4f * t / seconds)) * bodyAmt;
        s.buf[i] = (Noise(seed) + body) * env;
    }
    LowPass(s.buf, lp, loading);
    if (hp > 0) HighPass(s.buf, hp, loading);
    // fade the tail
    for (size_t i = 0; i < 200 && i < s.N(); i++) s.buf[s.N() - 1 - i] *= i / 200.0f;
    return s.PCM(0.95f, loading);
}

static Sound SoundFromPCM(const std::vector<short>& pcm, startup::Reporter* loading = nullptr) {
    Wave w{};
    w.frameCount = (unsigned int)pcm.size();
    w.sampleRate = SR; w.sampleSize = 16; w.channels = 1;
    w.data = (void*)pcm.data();
    startup::AtomicSpan atomic(loading, "audio", "synthesized sound upload");
    return LoadSoundFromWave(w);   // copies the data
}

static std::vector<unsigned char> MakeWav(const std::vector<short>& pcm) {
    std::vector<unsigned char> out(44 + pcm.size() * 2);
    auto w32 = [&](size_t o, uint32_t v) { memcpy(&out[o], &v, 4); };
    auto w16 = [&](size_t o, uint16_t v) { memcpy(&out[o], &v, 2); };
    memcpy(&out[0], "RIFF", 4); w32(4, (uint32_t)(36 + pcm.size() * 2)); memcpy(&out[8], "WAVE", 4);
    memcpy(&out[12], "fmt ", 4); w32(16, 16); w16(20, 1); w16(22, 1); w32(24, SR); w32(28, SR * 2); w16(32, 2); w16(34, 16);
    memcpy(&out[36], "data", 4); w32(40, (uint32_t)(pcm.size() * 2));
    memcpy(&out[44], pcm.data(), pcm.size() * 2);
    return out;
}

// Seamless loops: whole number of cycles of the base frequency in the buffer.
static std::vector<short> EngineLoop(float baseHz, float grit, uint32_t seed, startup::Reporter* loading = nullptr) {
    const float secs = 1.0f;
    Synth s(secs);
    float f = roundf(baseHz * secs) / secs;
    for (size_t i = 0; i < s.N(); i++) {
        AudioPulse(loading, i);
        float t = i / (float)SR;
        float ph = fmodf(t * f, 1.0f);
        float saw = 2 * ph - 1;
        float v = saw * 0.5f + sinf(2 * PI * f * t) * 0.6f + sinf(4 * PI * f * t) * 0.25f + sinf(2 * PI * f * 0.5f * t) * 0.35f;
        // firing pulses
        v += (ph < 0.12f ? 1.0f : 0.0f) * 0.4f;
        v += Noise(seed) * grit;
        s.buf[i] = v;
    }
    LowPass(s.buf, 1400, loading);
    return s.PCM(0.8f, loading);
}

static std::vector<short> SirenLoop(startup::Reporter* loading = nullptr) {
    const float secs = 2.0f;           // one full wail cycle
    Synth s(secs);
    float phase = 0;
    for (size_t i = 0; i < s.N(); i++) {
        AudioPulse(loading, i);
        float t = i / (float)SR;
        float f = 750 + 450 * (0.5f - 0.5f * cosf(2 * PI * t / secs));
        phase += f / SR;
        float sq = fmodf(phase, 1.0f) < 0.5f ? 1.0f : -1.0f;
        s.buf[i] = sq * 0.4f + sinf(2 * PI * phase) * 0.6f;
    }
    LowPass(s.buf, 3500, loading);
    return s.PCM(0.7f, loading);
}

static std::vector<short> NoiseLoop(float lp, float hp, float amMod, uint32_t seed, startup::Reporter* loading = nullptr) {
    Synth s(2.0f);
    for (size_t i = 0; i < s.N(); i++) {
        AudioPulse(loading, i);
        float t = i / (float)SR;
        s.buf[i] = Noise(seed) * (1.0f - amMod * 0.5f * (1 + sinf(2 * PI * 3.0f * t)));
    }
    LowPass(s.buf, lp, loading);
    if (hp > 0) HighPass(s.buf, hp, loading);
    // crossfade the ends for a seamless loop
    size_t n = s.N(), fade = SR / 10;
    for (size_t i = 0; i < fade; i++) {
        AudioPulse(loading, i);
        float a = i / (float)fade;
        s.buf[i] = s.buf[i] * a + s.buf[n - fade + i] * (1 - a);
    }
    s.buf.resize(n - fade);
    return s.PCM(0.6f, loading);
}

static std::vector<short> Tone(std::initializer_list<float> notes, float noteLen, float square, startup::Reporter* loading = nullptr) {
    Synth s(noteLen * notes.size() + 0.2f);
    size_t k = 0;
    for (float f : notes) {
        for (size_t i = 0; i < (size_t)(noteLen * SR); i++) {
            AudioPulse(loading, i);
            float t = i / (float)SR;
            float env = Saturate(t * 60) * expf(-t * 4);
            float ph = fmodf(t * f, 1.0f);
            float v = (1 - square) * sinf(2 * PI * ph) + square * (ph < 0.5f ? 1.0f : -1.0f);
            s.buf[k * (size_t)(noteLen * SR) + i] += v * env;
        }
        k++;
    }
    LowPass(s.buf, 5000, loading);
    return s.PCM(0.6f, loading);
}

static std::vector<short> Horn(startup::Reporter* loading = nullptr) {
    Synth s(0.5f);
    for (size_t i = 0; i < s.N(); i++) {
        AudioPulse(loading, i);
        float t = i / (float)SR;
        float env = Saturate(t * 40) * Saturate((0.5f - t) * 20);
        float v = (fmodf(t * 415, 1.0f) < 0.5f ? 1.0f : -1.0f) + (fmodf(t * 523, 1.0f) < 0.5f ? 1.0f : -1.0f);
        s.buf[i] = v * env;
    }
    LowPass(s.buf, 2200, loading);
    return s.PCM(0.6f, loading);
}

static std::vector<short> Swish(float seconds, float lpFrom, uint32_t seed, startup::Reporter* loading = nullptr) {
    Synth s(seconds);
    for (size_t i = 0; i < s.N(); i++) {
        AudioPulse(loading, i);
        float t = i / (float)SR;
        float env = sinf(PI * t / seconds);
        s.buf[i] = Noise(seed) * env * env;
    }
    LowPass(s.buf, lpFrom, loading);
    HighPass(s.buf, 300, loading);
    return s.PCM(0.7f, loading);
}

static std::vector<short> Scream(startup::Reporter* loading = nullptr) {
    Synth s(0.7f);
    float ph = 0;
    uint32_t seed = 77;
    for (size_t i = 0; i < s.N(); i++) {
        AudioPulse(loading, i);
        float t = i / (float)SR;
        float f = 620 + 180 * sinf(t * 9) - 200 * t;
        ph += f / SR;
        float env = Saturate(t * 20) * Saturate((0.7f - t) * 5);
        float v = sinf(2 * PI * ph) + 0.5f * sinf(4 * PI * ph) + 0.25f * sinf(6 * PI * ph) + Noise(seed) * 0.1f;
        s.buf[i] = v * env;
    }
    LowPass(s.buf, 3000, loading);
    return s.PCM(0.5f, loading);
}

// An angry shout: a glottal pulse train (raised pitch that rises and falls, with jitter
// and shimmer) through three formant resonators in cascade that glide from one vowel to
// the next, an aspirated onset and a little saturation for the strain of a raised voice.
struct Vowel { float f1, f2, f3; };
static std::vector<short> Shout(float seconds, float f0, Vowel from, Vowel to, float aspiration, uint32_t seed, startup::Reporter* loading = nullptr) {
    Synth s(seconds);
    const float bw[3] = { 90, 120, 170 };
    float y1[3] = {}, y2[3] = {};
    float phase = 0, jitter = 0, shimmer = 1, prevFlow = 0;
    for (size_t i = 0; i < s.N(); i++) {
        AudioPulse(loading, i);
        float t = i / (float)SR, u = t / seconds;
        // Breath first, then the voice; it swells, holds and drops away.
        float voicing = Saturate((t - 0.04f) * 25) * Saturate((1 - u) * 3.5f);
        float pitch = f0 * (1 + 0.18f * sinf(PI * Saturate(u * 1.8f)) - 0.25f * u * u);
        jitter += (Noise(seed) - jitter) * 0.003f;
        phase += pitch * (1 + jitter * 0.04f) / SR;
        if (phase >= 1) { phase -= 1; shimmer = 1 + Noise(seed) * 0.08f; }
        // Glottal flow (open 0..0.6, closing to 0.72) and its derivative: the radiated pulse.
        float flow = phase < 0.6f ? 0.5f - 0.5f * cosf(PI * phase / 0.6f)
                   : phase < 0.72f ? cosf(PI * 0.5f * (phase - 0.6f) / 0.12f) : 0.0f;
        float pulse = (flow - prevFlow) * SR / (pitch * 6) * shimmer;
        prevFlow = flow;
        float breath = Noise(seed) * (aspiration * Saturate(1 - t * 12) * 0.8f + 0.04f * voicing);
        float x = pulse * voicing + breath;
        float glide = SmoothStep(0.2f, 0.8f, u);
        // A raised voice opens the mouth wider: the first formant rises.
        float f[3] = { Lerpf(from.f1, to.f1, glide) * 1.1f, Lerpf(from.f2, to.f2, glide), Lerpf(from.f3, to.f3, glide) };
        for (int k = 0; k < 3; k++) {           // unity gain at DC, peaks at the formants
            float r = expf(-PI * bw[k] / SR), c = 2 * r * cosf(2 * PI * f[k] / SR);
            float y = (1 - c + r * r) * x + c * y1[k] - r * r * y2[k];
            y2[k] = y1[k]; y1[k] = y;
            x = y;
        }
        s.buf[i] = x * Saturate(t * 80) * Saturate((1 - u) * 4.0f);
    }
    HighPass(s.buf, 120, loading);
    float peak = 1e-6f;
    size_t completed = 0;
    for (float v : s.buf) { peak = std::max(peak, fabsf(v)); AudioPulse(loading, ++completed); }
    completed = 0;
    for (float& v : s.buf) { v = tanhf(1.4f * v / peak); AudioPulse(loading, ++completed); }
    LowPass(s.buf, 6000, loading);
    return s.PCM(0.9f, loading);
}

// -------------------------------------------------------------------------------------
void AudioSystem::MakeLoop(Loop& l, const std::vector<short>& pcm, const char* overrideName, startup::Reporter* loading) {
    char path[256];
    snprintf(path, sizeof(path), "assets/sounds/%s.wav", overrideName);
    if (FileExists(path)) {
        startup::AtomicSpan atomic(loading, "audio", overrideName);
        l.music = LoadMusicStream(path);
    }
    else {
        l.wav = MakeWav(pcm);
        startup::AtomicSpan atomic(loading, "audio", overrideName);
        l.music = LoadMusicStreamFromMemory(".wav", l.wav.data(), (int)l.wav.size());
    }
    l.music.looping = true;
}

void AudioSystem::StartLoop(Loop& l) {
    if (!l.playing && IsMusicValid(l.music)) { SetMusicVolume(l.music, 0); PlayMusicStream(l.music); l.playing = true; }
}

bool AudioSystem::Init(startup::Reporter* loading) {
    if (loading && !loading->Begin("world.audio", "Effects, engines and sirens", 1)) return false;
    {
        startup::AtomicSpan atomic(loading, "audio", "audio device");
        InitAudioDevice();
    }
    ok = IsAudioDeviceReady();
    if (!ok) {
        TraceLog(LOG_WARNING, "AUDIO: no audio device - running silent");
        return !loading || loading->Finish(startup::Outcome::Skipped, "Audio unavailable; continuing silently");
    }

    std::vector<startup::ChildSpec> children;
    for (int i = 0; i < (int)Sfx::COUNT; i++) children.push_back({ "pcm." + std::to_string(i) });
    for (int i = 0; i < (int)Sfx::COUNT; i++) children.push_back({ "sound." + std::to_string(i), 1, false });
    for (const char* id : { "engine", "engineBig", "siren", "skid", "ambience" }) children.push_back({ id, 1, false });
    children.push_back({ "commit" });
    if (loading && (!loading->PlanChildren(children) || !loading->Pulse("Synthesizing sound effects", true))) return false;

    try {
        std::vector<short> pcm[(int)Sfx::COUNT];
        int synthesized = 0;
        auto prepare = [&](Sfx kind, std::vector<short> samples) {
            pcm[(int)kind] = std::move(samples);
            synthesized++;
            std::string id = "pcm." + std::to_string((int)kind);
            return !loading || (loading->Count(synthesized, (int)Sfx::COUNT, "effects synthesized") && loading->ChildDone(id.c_str()));
        };
        if (!prepare(Sfx::Pistol, Burst(0.35f, 22, 5000, 120, 90, 1.2f, 1, loading))) return false;
        if (!prepare(Sfx::Shotgun, Burst(0.7f, 9, 3500, 60, 60, 1.8f, 2, loading))) return false;
        if (!prepare(Sfx::Rifle, Burst(0.22f, 30, 6000, 200, 110, 0.9f, 3, loading))) return false;
        if (!prepare(Sfx::Punch, Burst(0.15f, 40, 900, 0, 70, 2.0f, 4, loading))) return false;
        if (!prepare(Sfx::Knife, Swish(0.22f, 6000, 5, loading))) return false;
        if (!prepare(Sfx::Crash, Burst(0.9f, 6, 2500, 40, 45, 1.6f, 6, loading))) return false;
        if (!prepare(Sfx::CrashSmall, Burst(0.35f, 14, 1800, 60, 80, 1.0f, 7, loading))) return false;
        if (!prepare(Sfx::Explosion, Burst(2.6f, 2.2f, 900, 20, 35, 2.5f, 8, loading))) return false;
        if (!prepare(Sfx::Horn, Horn(loading))) return false;
        if (!prepare(Sfx::Door, Burst(0.18f, 30, 1500, 100, 140, 1.5f, 9, loading))) return false;
        if (!prepare(Sfx::Pickup, Tone({ 880, 1318 }, 0.09f, 0.3f, loading))) return false;
        if (!prepare(Sfx::MissionPass, Tone({ 523, 659, 784, 1046 }, 0.16f, 0.4f, loading))) return false;
        if (!prepare(Sfx::MissionFail, Tone({ 392, 330, 262 }, 0.22f, 0.5f, loading))) return false;
        if (!prepare(Sfx::Wasted, Tone({ 196, 185, 175, 131 }, 0.3f, 0.2f, loading))) return false;
        if (!prepare(Sfx::Splash, Swish(0.6f, 2500, 10, loading))) return false;
        if (!prepare(Sfx::Glass, Burst(0.4f, 12, 9000, 2500, 0, 0, 11, loading))) return false;
        if (!prepare(Sfx::Footstep, Burst(0.08f, 60, 700, 0, 60, 0.6f, 12, loading))) return false;
        if (!prepare(Sfx::Reload, Tone({ 1800, 1200 }, 0.05f, 0.9f, loading))) return false;
        if (!prepare(Sfx::Scream, Scream(loading))) return false;
        if (!prepare(Sfx::ShoutHey, Shout(0.42f, 205, { 520, 1850, 2500 }, { 300, 2250, 2950 }, 0.9f, 31, loading))) return false;
        if (!prepare(Sfx::ShoutOi, Shout(0.46f, 190, { 560, 860, 2400 }, { 320, 2150, 2850 }, 0.25f, 32, loading))) return false;
        if (!prepare(Sfx::ShoutHah, Shout(0.36f, 225, { 820, 1250, 2650 }, { 650, 1200, 2450 }, 0.8f, 33, loading))) return false;

        const char* names[(int)Sfx::COUNT] = { "pistol", "shotgun", "rifle", "punch", "knife", "crash", "crash_small", "explosion",
                                               "horn", "door", "pickup", "mission_pass", "mission_fail", "wasted", "splash", "glass",
                                               "footstep", "reload", "scream", "shout1", "shout2", "shout3" };
        bool incomplete = false;
        if (loading && (!loading->Count(0, 0, "") || !loading->Pulse("Preparing sound playback", true))) return false;
        for (int i = 0; i < (int)Sfx::COUNT; i++) {
            char path[256];
            snprintf(path, sizeof(path), "assets/sounds/%s.wav", names[i]);
            Sound base{};
            if (FileExists(path)) {
                startup::AtomicSpan atomic(loading, "audio", names[i]);
                base = LoadSound(path);
            } else base = SoundFromPCM(pcm[i], loading);
            bool valid = IsSoundValid(base);
            if (valid) {
                sounds[i].push_back(base);
                for (int k = 0; k < 5; k++) {
                    startup::AtomicSpan atomic(loading, "audio", "sound alias");
                    Sound alias = LoadSoundAlias(base);
                    if (IsSoundValid(alias)) sounds[i].push_back(alias);
                else {
                    if (alias.stream.buffer) UnloadSoundAlias(alias);
                    incomplete = true;
                }
                }
            } else {
                incomplete = true;
                if (base.stream.buffer) UnloadSound(base);
                TraceLog(LOG_WARNING, "AUDIO: sound %s unavailable - skipping playback", names[i]);
            }
            std::string id = "sound." + std::to_string(i);
            if (loading && (!loading->Count(i + 1, (int)Sfx::COUNT, "sound records processed") ||
                !loading->ChildDone(id.c_str(), valid ? startup::Outcome::Ready : startup::Outcome::Skipped))) return false;
        }
        int loopsProcessed = 0;
        auto loopDone = [&](Loop& loop, const char* id) {
            bool valid = IsMusicValid(loop.music);
            incomplete = incomplete || !valid;
            loopsProcessed++;
            if (!valid) TraceLog(LOG_WARNING, "AUDIO: loop %s unavailable - continuing silently", id);
            return !loading || (loading->Count(loopsProcessed, 5, "loop records processed") &&
                loading->ChildDone(id, valid ? startup::Outcome::Ready : startup::Outcome::Skipped));
        };
        if (loading && (!loading->Count(0, 0, "") || !loading->Pulse("Preparing engines and ambience", true))) return false;
        MakeLoop(engine, EngineLoop(55, 0.08f, 21, loading), "engine", loading);
        if (!loopDone(engine, "engine")) return false;
        MakeLoop(engineBig, EngineLoop(32, 0.12f, 22, loading), "engine_diesel", loading);
        if (!loopDone(engineBig, "engineBig")) return false;
        MakeLoop(siren, SirenLoop(loading), "siren", loading);
        if (!loopDone(siren, "siren")) return false;
        MakeLoop(skid, NoiseLoop(2500, 700, 0.3f, 23, loading), "skid", loading);
        if (!loopDone(skid, "skid")) return false;
        MakeLoop(ambience, NoiseLoop(350, 0, 0.1f, 24, loading), "ambience", loading);
        if (!loopDone(ambience, "ambience")) return false;
        StartLoop(ambience);
        SetMasterVolume(0.85f);
        if (loading && (!loading->ChildDone("commit") ||
            !loading->Finish(incomplete ? startup::Outcome::ReadyWithFallback : startup::Outcome::Ready,
                             incomplete ? "Some sounds unavailable; continuing" : nullptr))) return false;
        return true;
    } catch (const AudioCancelled&) {
        return false;
    }
}

void AudioSystem::Unload() {
    if (!ok) return;
    for (auto& v : sounds) {
        for (size_t k = 1; k < v.size(); k++) if (v[k].stream.buffer) UnloadSoundAlias(v[k]);
        if (!v.empty() && v[0].stream.buffer) UnloadSound(v[0]);
        v.clear();
    }
    for (Loop* l : { &engine, &engineBig, &siren, &skid, &ambience }) {
        if (l->music.ctxData || l->music.stream.buffer) UnloadMusicStream(l->music);
        l->music = {}; l->wav.clear(); l->playing = false;
    }
    CloseAudioDevice();
    ok = false;
}

void AudioSystem::Play(Sfx s, Vector2 pos, float volume, float pitch) {
    if (!ok) return;
    float d = Dist(pos, listener);
    float att = Saturate(1.0f - d / 1600.0f);
    att *= att;
    if (att < 0.01f) return;
    auto& pool = sounds[(int)s];
    if (pool.empty()) return;
    Sound& snd = pool[next[(int)s]];
    next[(int)s] = (next[(int)s] + 1) % (int)pool.size();
    SetSoundVolume(snd, Saturate(volume * att));
    SetSoundPitch(snd, pitch * GRng().Range(0.95f, 1.05f));
    SetSoundPan(snd, Clampf(0.5f - (pos.x - listener.x) / 1400.0f, 0.1f, 0.9f));   // raylib: 1 = left
    PlaySound(snd);
}

void AudioSystem::PlayUI(Sfx s, float volume, float pitch) {
    if (!ok) return;
    auto& pool = sounds[(int)s];
    if (pool.empty()) return;
    Sound& snd = pool[next[(int)s]];
    next[(int)s] = (next[(int)s] + 1) % (int)pool.size();
    SetSoundVolume(snd, volume); SetSoundPitch(snd, pitch); SetSoundPan(snd, 0.5f);
    PlaySound(snd);
}

void AudioSystem::SetEngine(bool on, float rpm, float throttle, bool large) {
    engineTarget = on ? 0.25f + 0.35f * throttle : 0.0f;
    enginePitch = 0.55f + rpm * 1.25f;
    bigMix = large ? 1.0f : 0.0f;
}
void AudioSystem::SetSiren(float v) { sirenVol = v; }
void AudioSystem::SetSkid(float v) { skidVol = v; }

void AudioSystem::Update(Vector2 l) {
    if (!ok) return;
    listener = l;
    float dt = GetFrameTime();
    engineVol = Lerpf(engineVol, engineTarget, Damp(8, dt));
    for (Loop* lp : { &engine, &engineBig, &siren, &skid }) {
        float v = lp == &engine ? engineVol * (1 - bigMix) : lp == &engineBig ? engineVol * bigMix : lp == &siren ? sirenVol : skidVol;
        if (v > 0.01f) StartLoop(*lp);
        if (lp->playing) {
            UpdateMusicStream(lp->music);
            SetMusicVolume(lp->music, Saturate(v));
            if (lp == &engine || lp == &engineBig) SetMusicPitch(lp->music, Clampf(enginePitch, 0.4f, 2.4f));
        }
    }
    if (IsMusicValid(ambience.music)) {
        UpdateMusicStream(ambience.music);
        SetMusicVolume(ambience.music, 0.18f);
    }
}
