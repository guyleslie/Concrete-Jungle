"""CJ-004 / CJ-029: build, animate, render and measure the civilian looks; write their atlases.

    python tools/cj004/make_civilians.py [--only civilian-01,civilian-15] [--jobs 3] [--poses idle,fist] [--summary]

--poses re-renders only those jobs from the existing civilian-anim.blend, then rebuilds the
atlas and the report from the frames on disk. --gait re-measures only the gaits (and the walk
strides in civilians.cfg). --summary only re-checks and prints the report.

For every look in tools/cj004/looks.json (Blender 5.2 with MPFB and the MakeHuman packs, the
Universal Animation Library unpacked under build/art-sources/ual/):
  1. build_civilian.py    -> build/art-sources/civilians/<id>/civilian.blend
  2. retarget_ual.py      -> civilian-anim.blend (gaits on every frame, poses on the frames used)
  3. render_civilian.py   -> frames and leg masks, 2.1 m per frame
  4. measure_gait.py      -> stride, foot slip, toe reach and leg spread per gait
  5. stylize.py           -> assets/characters/civilians/<id>.png, 38 frames of 144 px
  6. measure_frames.py    -> build/art-sources/civilians/report.json and a summary table
and the CIVILIAN lines of assets/data/civilians.cfg get each look's measured walk stride.
The atlas layout is ATLAS_LAYOUT below; civilians.cfg declares the same in its ANIM records.
"""
import argparse, json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from stylize import stylize
from measure_frames import measure, ATLAS

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
UAL = os.path.join(ROOT, "build", "art-sources", "ual", "Universal Animation Library[Standard]", "Unreal-Godot", "UAL1_Standard.glb")
WORK = os.path.join(ROOT, "build", "art-sources", "civilians")
OUT = os.path.join(ROOT, "assets", "characters", "civilians")
CFG = os.path.join(ROOT, "assets", "data", "civilians.cfg")
TOOLS = os.path.dirname(os.path.abspath(__file__))

# Gaits: library action, retarget modifiers (see retarget_ual.py), the frames that become atlas
# frames, and the cycle length used to check foot slip (the walk uses its measured stride; jog
# and run play by cadence in the game, so their check uses the cycle at a typical speed).
# The legs move 0.65 (walk), 0.55 (jog) and 0.5 (run) of the library's range: the user chose
# the compact stride on 2026-10-08, so the feet do not reach out like the splits from above.
GAITS = {
    "walk": ("Walk_Formal_Loop", "~1,-6*0.65", [2 * i for i in range(16)], "auto"),
    "jog": ("Jog_Fwd_Loop", "@12*0.55", [round(2.75 * i, 2) for i in range(8)], "2.26"),   # 3.0 m/s at 2.65 steps/s
    "run": ("Sprint_Loop", "@25*0.5", [2 * i for i in range(8)], "3.42"),                  # 5.2 m/s at 3.04 steps/s
}
POSES_FRAMES = {"punch": ("Punch_Cross", [7]), "jab": ("Punch_Jab", [5]), "lying": ("Death01", [57])}
# Atlas layout: (name, render stem, frame count) in order; the game reads it from civilians.cfg.
ATLAS_LAYOUT = [("walk", "Walk_Formal_Loop", 16), ("idle", "idle", 1), ("lying", "Death01", 1),
                ("punch", None, 2), ("fist", "fist", 2), ("jog", "Jog_Fwd_Loop", 8), ("run", "Sprint_Loop", 8)]
PLAYER_WIDTH_M = 0.65
LEGS_LIMIT = 0.04
DRAW_SCALE = 1.12       # civilians.cfg SCALE: the average man within 10 % of the player's width
SLIP_LIMIT = 0.02
WALK_TOE_LIMIT = 0.30
WALK_SPREAD_LIMIT = 0.70


def blender(*args, log):
    with open(log, "a", encoding="utf-8") as fh:
        r = subprocess.run([BLENDER, "-b", *args], stdout=fh, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        raise RuntimeError("Blender failed, see " + log)


def flist(frames):
    return ",".join(str(f) for f in frames)


POSES = []        # set from --poses: re-render only these jobs


def make(look):
    lid = look["id"]
    d = os.path.join(WORK, lid)
    os.makedirs(d, exist_ok=True)
    log = os.path.join(d, "blender.log")
    blend, anim = os.path.join(d, "civilian.blend"), os.path.join(d, "civilian-anim.blend")
    renders = os.path.join(d, "frames")
    if not POSES:
        open(log, "w").close()
        look_file = os.path.join(d, "look.json")
        json.dump(look, open(look_file, "w"), indent=2)
        blender("--factory-startup", "--python", os.path.join(TOOLS, "build_civilian.py"), "--", look_file, blend, log=log)
        blender(blend, "--python", os.path.join(TOOLS, "retarget_ual.py"), "--", UAL, anim,
                *[action + mods for action, mods, _, _ in GAITS.values()],
                *["%s=%s" % (action, flist(frames)) for action, frames in POSES_FRAMES.values()], log=log)
    jobs = {"idle": "idle", "fist": "fist:0,1", "lying": "%s:%s:lying" % (POSES_FRAMES["lying"][0], flist(POSES_FRAMES["lying"][1])),
            "punch": "%s:%s" % (POSES_FRAMES["punch"][0], flist(POSES_FRAMES["punch"][1])),
            "jab": "%s:%s" % (POSES_FRAMES["jab"][0], flist(POSES_FRAMES["jab"][1]))}
    for name, (action, _, frames, _) in GAITS.items():
        jobs[name] = "%s:%s" % (action, flist(frames))
    # idle first: it chooses the stance that the raised-fist pose then shares
    order_jobs = ["idle", "fist", "walk", "lying", "punch", "jab", "jog", "run"]
    blender(anim, "--python", os.path.join(TOOLS, "render_civilian.py"), "--", renders,
            *[jobs[k] for k in order_jobs if not POSES or k in POSES], log=log)
    gait_json = os.path.join(d, "gait.json")
    if not POSES:
        blender(anim, "--python", os.path.join(TOOLS, "measure_gait.py"), "--", gait_json,
                *["%s:%s:%s" % (action, flist(frames), cycle) for action, _, frames, cycle in GAITS.values()], log=log)

    names = []
    for name, stem, count in ATLAS_LAYOUT:
        names += [POSES_FRAMES["punch"][0] + "_00", POSES_FRAMES["jab"][0] + "_00"] if name == "punch" else \
                 ["%s_%02d" % (stem, i) for i in range(count)]
    atlas = Image.new("RGBA", (ATLAS * len(names), ATLAS))
    for i, stem in enumerate(names):
        atlas.paste(stylize(Image.open(os.path.join(renders, stem + ".png")), ATLAS), (i * ATLAS, 0))
    os.makedirs(OUT, exist_ok=True)
    atlas.save(os.path.join(OUT, lid + ".png"))

    report = {"id": lid, "gender": look["macro"].get("gender", 1.0), "gait": json.load(open(gait_json))}
    for key, pose in (("walk", GAITS["walk"][0]), ("idle", "idle"), ("jog", GAITS["jog"][0]), ("run", GAITS["run"][0])):
        report[key] = measure(renders, pose)
        pel = json.load(open(os.path.join(renders, pose + "_pelvis.json")))
        report[key]["pelvis_mean_atlas_px"] = [sum(p[0] for p in pel) / len(pel), sum(p[1] for p in pel) / len(pel)]
    for line in open(log, encoding="utf-8", errors="ignore"):
        if line.startswith("CIVILIAN height"):
            report["height_m"] = float(line.split()[2])
    return report


def verdict(rep):
    idle = rep["idle"]["frames"][0]
    walk = rep["gait"][GAITS["walk"][0]]
    checks = {"idle legs": idle["legs_share"] <= LEGS_LIMIT,
              "walk toe": walk["toe_ahead_max_m"] <= WALK_TOE_LIMIT,
              "walk spread": walk["spread_max_m"] <= WALK_SPREAD_LIMIT}
    for k in ("walk", "jog", "run"):
        m = rep[k]
        checks[k + " loop"] = m["inner_steps"][0] * 0.8 <= m["wrap_step"] <= m["inner_steps"][1] * 1.2
        checks[k + " pelvis"] = max(abs(v) for v in m["pelvis_mean_atlas_px"]) <= 1.0
        checks[k + " slip"] = rep["gait"][GAITS[k][0]]["slip_m"] <= SLIP_LIMIT
    return checks


def write_cfg_strides(reports):
    """Give every CIVILIAN line of civilians.cfg its look's measured walk stride."""
    text = open(CFG, encoding="utf-8").read()

    def line(m):
        lid = m.group(1)
        r = reports.get(lid)
        return "CIVILIAN %s.png %.2f" % (lid, r["gait"][GAITS["walk"][0]]["stride_m"]) if r else m.group(0)
    open(CFG, "w", encoding="utf-8", newline="\n").write(re.sub(r"^CIVILIAN (\S+)\.png.*$", line, text, flags=re.M))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--poses", default="")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--gait", action="store_true")
    a = ap.parse_args()
    POSES.extend(p for p in a.poses.split(",") if p)
    looks = json.load(open(os.path.join(TOOLS, "looks.json"), encoding="utf-8"))["looks"]
    if a.only:
        wanted = set(a.only.split(","))
        looks = [l for l in looks if l["id"] in wanted]
    report_path = os.path.join(WORK, "report.json")
    reports = json.load(open(report_path)) if os.path.exists(report_path) else {}
    if a.gait:
        def regait(look):
            d = os.path.join(WORK, look["id"])
            blender(os.path.join(d, "civilian-anim.blend"), "--python", os.path.join(TOOLS, "measure_gait.py"), "--",
                    os.path.join(d, "gait.json"), *["%s:%s:%s" % (act, flist(fr), cyc) for act, _, fr, cyc in GAITS.values()],
                    log=os.path.join(d, "blender.log"))
            return look["id"], json.load(open(os.path.join(d, "gait.json")))
        with ThreadPoolExecutor(max_workers=a.jobs) as pool:
            for lid, g in pool.map(regait, looks):
                reports[lid]["gait"] = g
        json.dump(reports, open(report_path, "w"), indent=1)
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for rep in pool.map(make, [] if a.summary or a.gait else looks):
            reports[rep["id"]] = rep
            json.dump(reports, open(report_path, "w"), indent=1)
            print("DONE", rep["id"], flush=True)
    reports = {k: r for k, r in reports.items() if "gait" in r}        # reports from before CJ-029 are stale
    for r in reports.values():
        r["checks"] = verdict(r)
    if reports and (not a.summary or a.gait):
        write_cfg_strides(reports)
    # Width is a property of the set: the average man, as drawn (DRAW_SCALE), within 10 % of the player.
    men = [r for r in reports.values() if r.get("gender", 1.0) >= 0.5]
    if men:
        idle_w = DRAW_SCALE * sum(r["idle"]["frames"][0]["across_m"] for r in men) / len(men)
        walk_w = DRAW_SCALE * sum(max(f["across_m"] for f in r["walk"]["frames"]) for r in men) / len(men)
        print("average man as drawn: idle %.3f m (%+.0f %%), walk %.3f m (%+.0f %%) against the player's %.2f m: %s" % (
            idle_w, 100 * (idle_w / PLAYER_WIDTH_M - 1), walk_w, 100 * (walk_w / PLAYER_WIDTH_M - 1), PLAYER_WIDTH_M,
            "pass" if max(abs(idle_w / PLAYER_WIDTH_M - 1), abs(walk_w / PLAYER_WIDTH_M - 1)) <= 0.10 else "FAIL"))
    print("%-12s %5s %5s %6s %6s %6s %6s %6s %6s %6s  %s" % ("look", "h m", "legs%", "stride", "toe", "spread", "w.slip", "j.slip", "r.slip", "steps", "failed"))
    for lid in sorted(reports):
        r = reports[lid]
        w = r["gait"][GAITS["walk"][0]]
        print("%-12s %5.2f %5.1f %6.2f %6.2f %6.2f %6.3f %6.3f %6.3f %6.2f  %s" % (
            lid, r.get("height_m", 0), 100 * r["idle"]["frames"][0]["legs_share"], w["stride_m"], w["toe_ahead_max_m"],
            w["spread_max_m"], w["slip_m"], r["gait"][GAITS["jog"][0]]["slip_m"], r["gait"][GAITS["run"][0]]["slip_m"],
            2 * 1.5 / w["stride_m"] if w["stride_m"] else 0,
            ", ".join(k for k, v in r["checks"].items() if not v) or "-"))


if __name__ == "__main__":
    main()
