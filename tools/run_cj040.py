"""CJ-040 radio measurements: runs the radio scenes and measures their recordings.

    python tools/run_cj040.py              # run --scenario radio, then measure
    python tools/run_cj040.py --analyse    # measure the last run (build/shots/radio.log, build/radio/)

For every passing car (PASS scene) the recording of the final mix is compared with the
station's own signal, delayed by the sound's travel time:
  level      the mix's 40-150 Hz band, where the body passes everything, against the gain
             the radio.cfg model gives for the distance (inverse distance, the body's loss,
             the fade over the last 6 dB), followed as smoothly as the processor follows it
             (30 ms): 95 % of the windows within 1.5 dB
  tone       (1-4 kHz) / (40-150 Hz) of the mix against the same ratio of the station: at
             least 20 dB duller on foot, and at least 8 dB duller again from inside a car
  doppler    the playback rate of the travel-time delay line against 1 - (dd/dt) / c from
             the car's motion 50 ms earlier (the delay's smoothing), on the audio clock,
             where the car is well away from its closest point: within 0.5 %
  range      the distance at which the mix falls below -60 dB of the player's own radio:
             within 1 m of the model's hearing range for a normal radio, 3 m for a loud one;
             silence where the model says the radio is never heard
  direction  a car to the left (more than 3 m) is louder on the left channel, and to the right
  clicks     no 5 ms window whose second difference jumps above the scene's smooth signal
For the player's own radio (INCAR scenes) the loudness of every complete talk clip in the
mix, with the in-car volume and the pan law taken out: within 1 LU of the LOUDNESS target.
For songs cut to slices (EXCERPT scenes) the mix against the station's own signal before
the fades: flat within 0.5 dB between the fades (95 % of the 20 ms windows), and the fades
within 1 dB of their smoothstep curves wherever they are above -20 dB. The fixture itself checks the programme and the gaps between clips.
"""
import math
import re
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
LOG = ROOT / "build" / "shots" / "radio.log"
OUT = ROOT / "build" / "radio"
EXE = ROOT / "ConcreteJungle.exe"
PAN_CENTRE = 0.5 * 0.5 * (3 - 0.25)   # raylib 6.0 MixAudioFrames: a stream panned to the centre
SOUND_SPEED = 343.0
GAIN_TAU = 0.03                        # s, radio.cpp: gains follow their targets this smoothly
DELAY_TAU = 0.05                       # s, radio.cpp: the travel time follows the distance this smoothly
# The recording is taken in raylib's mixed processor, before miniaudio applies the master
# volume, so only the pan law stands between a stream and the recording.


def config():
    """The radio.cfg values the model needs."""
    c = {}
    for line in (ROOT / "assets" / "data" / "radio.cfg").read_text(encoding="utf-8").splitlines():
        tok = line.split("#", 1)[0].split()
        if not tok:
            continue
        key = tok[0].upper()
        if key == "INCAR":
            c["incar"] = float(tok[1])
        elif key == "CABIN":
            c["normal"], c["loud"] = float(tok[1]), float(tok[2])
        elif key == "BODY":
            c["corner"], c["loss"] = float(tok[1]), float(tok[2])
        elif key == "HEARING":
            c["hearing"], c["reference"] = float(tok[1]), float(tok[2])
        elif key == "LOUDNESS":
            c["loudness"] = float(tok[1])
    return c


def read_wav(path):
    """(rate, frames x channels) of a WAV the fixture wrote (PCM 16 or float 32)."""
    raw = path.read_bytes()
    fmt, channels, rate = struct.unpack_from("<HHI", raw, 20)
    bits = struct.unpack_from("<H", raw, 34)[0]
    data = np.frombuffer(raw, dtype=np.float32 if fmt == 3 else np.int16, offset=44)
    data = data.astype(np.float64) / (1.0 if fmt == 3 else 32767.0)
    return rate, data.reshape(-1, channels), bits


def write_wav(path, data, rate):
    data = np.asarray(data, dtype=np.float32)
    channels = data.shape[1]
    header = b"RIFF" + struct.pack("<I", 36 + data.nbytes) + b"WAVEfmt " + struct.pack(
        "<IHHIIHH", 16, 3, channels, rate, rate * channels * 4, channels * 4, 32) + b"data" + struct.pack("<I", data.nbytes)
    path.write_bytes(header + data.tobytes())


def lufs(path):
    err = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", str(path), "-af", "ebur128", "-f", "null", "-"],
                         capture_output=True, text=True).stderr
    for line in err[err.rfind("Summary:"):].splitlines():
        if line.strip().startswith("I:"):
            return float(line.split()[1])
    return float("nan")


def band(spectrum, freqs, lo, hi):
    return spectrum[..., (freqs >= lo) & (freqs < hi)].sum(axis=-1)


def scenes(log):
    found = []
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        if "RADIOTEST scene=" not in line:
            continue
        fields = dict(re.findall(r"(\w+)=(\S+)", line.split("RADIOTEST", 1)[1]))
        found.append(fields)
    return found


def model_gain(c, cabin, d, in_car):
    """The radio.cfg model: the player's gain for a radio d metres away."""
    level = cabin - c["loss"] - 20 * np.log10(np.maximum(d, c["reference"]) / c["reference"]) - (c["loss"] if in_car else 0)
    fade = np.clip((level - c["hearing"]) / 6.0, 0, 1)
    return c["incar"] * 10 ** ((level - c["normal"]) / 20) * fade, level


def analyse_pass(c, s, results):
    name = s["scene"]
    rate, data, _ = read_wav(OUT / f"{name}_data.wav")
    rows = np.genfromtxt(OUT / f"{name}.csv", delimiter=",", names=True)
    in_car = s["listener"] == "car"
    cabin, kmh, side = float(s["cabin_db"]), float(s["kmh"]), float(s["side_m"])
    out, src = data[:, :2], data[:, 2]
    keep = np.r_[True, np.diff(rows["frame"]) > 0]
    f_at, t_at, d_at, dx_at = rows["frame"][keep], rows["t"][keep], rows["d_m"][keep], rows["dx_m"][keep]
    delay_at, gain_at, target_at = rows["delay_s"][keep], rows["gain"][keep], rows["target_gain"][keep]
    audio = f_at / rate                     # the probes on the audio clock
    rec = {"scene": name}

    # The model's gain at every probe, and as smoothly as the processor follows it.
    g_model, _ = model_gain(c, cabin, d_at, in_car)
    g_smooth = np.empty_like(g_model)
    for i in range(len(g_model)):
        k = 1 - math.exp(-(t_at[i] - t_at[i - 1]) / GAIN_TAU) if i else 1.0
        g_smooth[i] = g_model[i] if i == 0 else g_smooth[i - 1] + (g_model[i] - g_smooth[i - 1]) * k
    heard = g_model > 1e-6
    if heard.any():
        rec["law_db"] = float(np.max(np.abs(20 * np.log10(np.maximum(target_at[heard], 1e-12) / g_model[heard]))))

    # The playback rate of the delay line (the Doppler factor) on the audio clock, and the
    # one the car's motion gives on the main thread's clock.
    def playback(at, span=0.05):
        return 1 - (np.interp(at + span, audio, delay_at) - np.interp(at - span, audio, delay_at)) / (2 * span)

    def motion(at, span=0.05):
        return 1 - (np.interp(at + span, t_at, d_at) - np.interp(at - span, t_at, d_at)) / (2 * span) / SOUND_SPEED

    # Windows of the mix against the station's own signal, delayed by the travel time; the
    # station's bands are scaled by the Doppler factor, so the same sound is compared.
    n, hop, pad = 2048, 1024, 16384         # zero-padded: a band edge moved by the Doppler factor moves smoothly
    win = np.hanning(n)
    freqs = np.fft.rfftfreq(pad, 1 / rate)
    mids, lo_l, lo_r, lo_o, hi_o, lo_s, hi_s, wide_l, wide_r = [], [], [], [], [], [], [], [], []
    for a in range(int(rate * 0.5), len(src) - n, hop):
        mid = a + n // 2
        b = a - int(round(np.interp(mid, f_at, delay_at) * rate))
        if b < 0:
            continue
        r = motion(np.interp(mid, f_at, t_at) - DELAY_TAU)
        fo = np.abs(np.fft.rfft(out[a:a + n] * win[:, None], n=pad, axis=0)) ** 2
        fs = np.abs(np.fft.rfft(src[b:b + n] * win, n=pad)) ** 2
        mids.append(mid)
        lo_l.append(band(fo[:, 0], freqs, 40, 150)); lo_r.append(band(fo[:, 1], freqs, 40, 150))
        lo_o.append(band(fo.mean(axis=1), freqs, 40, 150)); hi_o.append(band(fo.mean(axis=1), freqs, 1000, 4000))
        lo_s.append(band(fs, freqs, 40 / r, 150 / r)); hi_s.append(band(fs, freqs, 1000 / r, 4000 / r))
        wide_l.append(band(fo[:, 0], freqs, 40, 4000)); wide_r.append(band(fo[:, 1], freqs, 40, 4000))
    mids = np.array(mids)
    lo_o, hi_o, lo_s, hi_s = map(np.array, (lo_o, hi_o, lo_s, hi_s))
    wide_l, wide_r = np.array(wide_l), np.array(wide_r)
    d = np.interp(mids, f_at, d_at)
    dx = np.interp(mids, f_at, dx_at)
    _, level = model_gain(c, cabin, d, in_car)
    expected = np.interp(mids, f_at, g_smooth)
    measured = np.sqrt(lo_o / np.maximum(lo_s, 1e-30)) / PAN_CENTRE

    # Level and tone where the model hears the car, its level changes by less than 1 dB in
    # 100 ms (the processor follows 30-40 ms late) and the station plays something.
    t_mid = np.interp(mids, f_at, t_at)
    g_late, _ = model_gain(c, cabin, np.interp(t_mid + 0.05, t_at, d_at), in_car)
    g_early, _ = model_gain(c, cabin, np.interp(t_mid - 0.05, t_at, d_at), in_car)
    steady = np.abs(20 * np.log10(np.maximum(g_late, 1e-12) / np.maximum(g_early, 1e-12))) < 1
    playing = lo_s > lo_s.max() * 1e-4
    ok = (level > c["hearing"] + 1) & steady & playing
    heardw = (level > c["hearing"] + 1) & playing
    if heardw.sum() >= 3:
        rec["tone_db"] = float(10 * np.log10(hi_o[heardw].sum() / lo_o[heardw].sum()) - 10 * np.log10(hi_s[heardw].sum() / lo_s[heardw].sum()))
    if ok.sum() >= 3:
        err = 20 * np.log10(measured[ok] / expected[ok])
        rec["level_p95_db"] = float(np.percentile(np.abs(err), 95))
        rec["level_median_db"] = float(np.median(err))
        rec["level_windows"] = int(ok.sum())
        rec["level_pass"] = rec["level_p95_db"] <= 1.5

    # Hearing range: the model's (where the level reaches the threshold), and where the mix
    # falls below -60 dB of the player's own radio, against the distance at which the model
    # does (the fade puts that a little short of the threshold).
    floor = c["incar"] * 1e-3
    audible = measured >= floor
    top = cabin - c["loss"] - (c["loss"] if in_car else 0)
    model_range = c["reference"] * 10 ** ((top - c["hearing"]) / 20) if top > c["hearing"] else 0.0
    rec["range_model_m"] = model_range
    lo_d, hi_d = 0.0, model_range
    for _ in range(60):
        mid_d = 0.5 * (lo_d + hi_d)
        if model_gain(c, cabin, np.array([mid_d]), in_car)[0][0] >= floor:
            lo_d = mid_d
        else:
            hi_d = mid_d
    rec["range_at_floor_m"] = lo_d
    if lo_d <= abs(side):
        rec["range_m"] = 0.0
        rec["range_pass"] = not audible.any()
    else:
        rec["range_m"] = float(d[audible].max()) if audible.any() else 0.0
        tol = 1.0 if cabin < c["loud"] else 3.0
        rec["range_pass"] = bool(audible.any()) and abs(rec["range_m"] - lo_d) <= tol

    # Direction: louder on the side the car is on.
    sided = audible & (np.abs(dx) > 3)
    if sided.any():
        good = np.where(dx[sided] > 0, wide_r[sided] > wide_l[sided], wide_l[sided] > wide_r[sided])
        rec["direction_share"] = float(good.mean())
        rec["direction_pass"] = rec["direction_share"] >= 0.99

    # Doppler: the delay line's playback rate against 1 - (dd/dt) / c of the car 50 ms
    # earlier (the delay's smoothing), where the motion along the line of sight hardly
    # changes (within 0.1 % over 0.3 s).
    active = target_at > 0                  # the voice follows the car (afterwards its delay stays put)
    if active.sum() > 20:
        span = 0.1
        tt = audio[(audio > audio[active].min() + 0.3 + span) & (audio < audio[active].max() - 0.1 - span)]
        if len(tt):
            when = np.interp(tt, audio, t_at) - DELAY_TAU

            def expect(at):
                return 1 - (np.interp(at + span, t_at, d_at) - np.interp(at - span, t_at, d_at)) / (2 * span) / SOUND_SPEED
            steady = np.abs(expect(when + 0.15) - expect(when - 0.15)) < 1e-3
            if steady.any():
                ratio = playback(tt[steady], span)
                want = expect(when[steady])
                rec["doppler_max_err_pct"] = float(np.max(np.abs(ratio / want - 1)) * 100)
                rec["doppler_pass"] = rec["doppler_max_err_pct"] <= 0.5
                rec["doppler_samples"] = int(steady.sum())
                coming = np.interp(tt[steady], audio, dx_at) * np.sign(kmh) < 0
                if coming.any() and (~coming).any():
                    rec["pitch_drop_semitones"] = float(12 * math.log2(ratio[coming].max() / ratio[~coming].min()))

    # Clicks: the mix is low-passed sound, so its second difference stays a small share of
    # its level; a jump would show as a spike of it in a 5 ms block.
    mono = out.mean(axis=1)
    block = int(rate * 0.005)
    blocks = (len(mono) - 2) // block
    if blocks:
        d2 = np.abs(np.diff(mono, 2))[:blocks * block].reshape(blocks, block).max(axis=1)
        rms = np.sqrt((mono[:blocks * block].reshape(blocks, block) ** 2).mean(axis=1))
        live = rms > rms.max() * 1e-3
        if live.any():
            share = d2[live] / rms[live]
            rec["click_ratio"] = float(share.max() / np.median(share))
            rec["click_pass"] = rec["click_ratio"] <= 20
        else:
            rec["click_ratio"] = 0.0
            rec["click_pass"] = True
    results.append(rec)


def smooth(x):
    x = np.clip(x, 0, 1)
    return x * x * (3 - 2 * x)


def slots(log, scene):
    """The fixture's slot lines of an EXCERPT scene: every clip aired, with its boundaries."""
    out = []
    for line in log.splitlines():
        if "RADIOTEST slot scene=" + scene + " " in line:
            out.append(dict(re.findall(r"(\w+)=(\S+)", line.split("RADIOTEST slot", 1)[1])))
    return out


def analyse_excerpt(c, s, results, log):
    name = s["scene"]
    rate, data, _ = read_wav(OUT / f"{name}_data.wav")
    mix, src = data[:, :2].mean(axis=1), data[:, 2]
    fade_in, fade_out = float(s["fade_in_s"]), float(s["fade_out_s"])
    win = int(rate * 0.02)
    for slot in slots(log, name):
        if slot["kind"] != "song":
            continue
        a, aired, start = int(slot["start_frame"]), float(slot["aired_s"]), float(slot["start_s"])
        n = int(aired * rate) // win
        seg_m = mix[a:a + n * win].reshape(n, win)
        seg_s = src[a:a + n * win].reshape(n, win)
        t = (np.arange(n) + 0.5) * win / rate
        expect = (smooth(t / fade_in) if start > 0 else np.ones(n)) * smooth((aired - t) / fade_out)
        level = np.sqrt((seg_m ** 2).mean(axis=1) / np.maximum((seg_s ** 2).mean(axis=1), 1e-20))
        live = (seg_s ** 2).mean(axis=1) > 1e-8
        flat = live & (t > fade_in) & (t < aired - fade_out)
        ref = np.median(level[flat])
        db = 20 * np.log10(np.maximum(level, 1e-9) / ref)
        flat_p95 = float(np.percentile(np.abs(db[flat]), 95))
        fading = live & ~flat & (expect >= 0.1)
        fade_p95 = float(np.percentile(np.abs(db[fading] - 20 * np.log10(expect[fading])), 95)) if fading.any() else 0.0
        results.append({"scene": f"{name}/{slot['clip']}", "slice_start_s": start, "aired_s": aired, "flat_p95_db": flat_p95,
                        "fade_p95_db": fade_p95, "fades_pass": flat_p95 <= 0.5 and fade_p95 <= 1.0})


def analyse_incar(c, s, results):
    name = s["scene"]
    rate, data, _ = read_wav(OUT / f"{name}_data.wav")
    starts = [int(v) for v in s["starts"].split(",")]
    scale = c["incar"] * PAN_CENTRE
    seg = data[starts[0]:starts[1], :2] / scale
    tmp = OUT / f"{name}_talk.wav"
    write_wav(tmp, seg, rate)
    level = lufs(tmp)
    tmp.unlink()
    clip = s["clips"].split(",")[1]
    rec = {"scene": name, "clip": clip, "lufs": level, "lufs_pass": abs(level - c["loudness"]) <= 1.0}
    results.append(rec)


def main():
    if "--analyse" not in sys.argv:
        OUT.mkdir(parents=True, exist_ok=True)
        with open(LOG, "w", encoding="utf-8") as log:
            code = subprocess.run([str(EXE), "--shot", "build/shots/radio.png", "--frames", "100000", "--scenario", "radio"],
                                  cwd=ROOT, stdout=log, stderr=subprocess.STDOUT).returncode
        print(f"radio scenes finished with exit code {code}")
    c = config()
    found = scenes(LOG)
    results = []
    text = LOG.read_text(encoding="utf-8", errors="replace")
    for s in found:
        if s.get("kind") == "pass":
            analyse_pass(c, s, results)
        elif s.get("kind") == "excerpt":
            analyse_excerpt(c, s, results, text)
        else:
            analyse_incar(c, s, results)
    by = {r["scene"]: r for r in results}
    # From inside a car: at least 8 dB duller than the same car heard on foot.
    for r in results:
        if r["scene"].startswith("car-") and "tone_db" in r:
            foot = by.get("foot-" + r["scene"][4:])
            if foot and "tone_db" in foot:
                r["tone_vs_foot_db"] = r["tone_db"] - foot["tone_db"]
                r["tone_pass"] = r["tone_vs_foot_db"] <= -8
        elif "tone_db" in r:
            r["tone_pass"] = r["tone_db"] <= -20
    failed = 0
    for r in results:
        checks = {k: v for k, v in r.items() if k.endswith("_pass")}
        bad = [k[:-5] for k, v in checks.items() if not v]
        failed += len(bad)
        values = "  ".join(f"{k}={v:.3f}" if isinstance(v, float) else f"{k}={v}" for k, v in r.items()
                           if k != "scene" and not k.endswith("_pass"))
        print(f"{r['scene']:24} {'PASS' if not bad else 'FAIL ' + ','.join(bad):18} {values}")
    checks = sum(1 for r in results for k in r if k.endswith("_pass"))
    log = LOG.read_text(encoding="utf-8", errors="replace")
    fixture = re.search(r"RADIOTEST summary .*", log)
    print(f"\nmeasured checks: {checks}, failed: {failed}")
    if fixture:
        print(fixture.group(0))
    for line in re.findall(r"RADIOTEST (?:schedule|schedule-slices|slot) .*", log):
        print(line)
    summary = re.search(r"RADIO: stations .*", log)
    if summary:
        print(summary.group(0))
    fixture_failed = not fixture or "failed=0" not in fixture.group(0)
    sys.exit(1 if failed or fixture_failed else 0)


if __name__ == "__main__":
    main()
