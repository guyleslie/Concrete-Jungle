"""CJ-004 / CJ-029: build, animate, render and measure the civilian looks; write their atlases.

    python tools/cj004/make_civilians.py [--only civilian-01,civilian-15] [--jobs 3] [--retarget] [--poses idle,fist] [--summary]

--retarget animates again from the existing build and tuning (steps 3-6), for a change to
gaits.py that leaves the tuning alone, such as how close the walking arms hang. --poses
re-renders only those jobs, from the existing civilian-anim.blend unless --retarget is given,
then rebuilds the atlas and the report from the frames on disk. --gait re-measures only the
gaits (and the walk strides in civilians.cfg). --summary re-measures the frames on disk,
re-checks and prints the report.

For every look in tools/cj004/looks.json (Blender 5.2 with MPFB and the MakeHuman packs, the
Universal Animation Library unpacked under build/art-sources/ual/):
  1. build_civilian.py    -> build/art-sources/civilians/<id>/civilian.blend
  2. tune_gait.py         -> tune.json: per gait, the hip swing and upright that show as much
                             leg ahead of the body as behind it (gaits.py lists the candidates)
  3. retarget_ual.py      -> civilian-anim.blend (gaits on every frame, poses on the frames used)
  4. measure_gait.py      -> frames.json: the walk frames that split its stride evenly
  5. render_civilian.py   -> frames and leg masks, 2.1 m per frame
  6. measure_gait.py      -> stride, foot slip, toe reach and leg spread per gait
  7. stylize.py           -> assets/characters/civilians/<id>.png, 38 frames of 144 px
  8. measure_frames.py    -> build/art-sources/civilians/report.json and a summary table
and the CIVILIAN lines of assets/data/civilians.cfg get each look's measured walk stride.
The atlas layout is ATLAS_LAYOUT below; civilians.cfg declares the same in its ANIM records.
"""
import argparse, json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from stylize import stylize
from measure_frames import measure, ATLAS
from gaits import GAITS, request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
UAL = os.path.join(ROOT, "build", "art-sources", "ual", "Universal Animation Library[Standard]", "Unreal-Godot", "UAL1_Standard.glb")
WORK = os.path.join(ROOT, "build", "art-sources", "civilians")
OUT = os.path.join(ROOT, "assets", "characters", "civilians")
CFG = os.path.join(ROOT, "assets", "data", "civilians.cfg")
TOOLS = os.path.dirname(os.path.abspath(__file__))

POSES_FRAMES = {"punch": ("Punch_Cross", [7]), "jab": ("Punch_Jab", [5]), "lying": ("Death01", [57])}
# Atlas layout: (name, render stem, frame count) in order; the game reads it from civilians.cfg.
ATLAS_LAYOUT = [("walk", GAITS["walk"]["action"], 16), ("idle", "idle", 1), ("lying", "Death01", 1),
                ("punch", None, 2), ("fist", "fist", 2), ("jog", GAITS["jog"]["action"], 8), ("run", GAITS["run"]["action"], 8)]
PLAYER_WIDTH_M = 0.65
LEGS_LIMIT = 0.04
DRAW_SCALE = 1.12       # civilians.cfg SCALE: the average man within 10 % of the player's width
SLIP_LIMIT = 0.02
WALK_SPEED = 1.3                # m/s, a casual walk
WALK_CADENCE = (1.8, 2.2)       # steps/s at WALK_SPEED for a SIDE_HEIGHT person (the user's choice of
                                # 2026-10-09); a shorter person steps shorter and so more often
WALK_KNEE_LIMIT = 30.0          # thigh forward swing, degrees
WALK_WIDER = 0.02               # a walker at most this much wider than standing (arms hang alike)
WALK_SIDE_LIMIT = 0.35          # legs beyond the body from above, ahead and behind, for a 1.78 m
SIDE_HEIGHT = 1.78              # person; in proportion to height, since taller people step further
SYMMETRY = (0.75, 1.33)         # ahead / behind
RUN_SWING = 2.0                 # the runner's hands travel at least this many times the walker's


def blender(*args, log):
    with open(log, "a", encoding="utf-8") as fh:
        r = subprocess.run([BLENDER, "-b", *args], stdout=fh, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        raise RuntimeError("Blender failed, see " + log)


def flist(frames):
    return ",".join(str(f) for f in frames)


def gait_jobs(frames):
    """measure_gait.py jobs for the gaits at the given frames."""
    return ["%s:%s:%s" % (g["action"], flist(frames[k]), g["cycle"]) for k, g in GAITS.items()]


POSES = []        # set from --poses: re-render only these jobs
RETARGET = []     # [True] with --retarget: animate again from the existing build and tuning


def make(look):
    lid = look["id"]
    d = os.path.join(WORK, lid)
    os.makedirs(d, exist_ok=True)
    log = os.path.join(d, "blender.log")
    blend, anim = os.path.join(d, "civilian.blend"), os.path.join(d, "civilian-anim.blend")
    renders = os.path.join(d, "frames")
    tune_json, frames_json = os.path.join(d, "tune.json"), os.path.join(d, "frames.json")
    animate = not POSES or RETARGET
    if not POSES and not RETARGET:
        open(log, "w").close()
        look_file = os.path.join(d, "look.json")
        json.dump(look, open(look_file, "w"), indent=2)
        blender("--factory-startup", "--python", os.path.join(TOOLS, "build_civilian.py"), "--", look_file, blend, log=log)
        blender(blend, "--python", os.path.join(TOOLS, "tune_gait.py"), "--", UAL, tune_json, log=log)
    if animate:
        tune = json.load(open(tune_json))
        # the tuned hip swing and upright with gaits.py's other settings; every whole frame of
        # each gait, and its atlas frames exactly (=+)
        blender(blend, "--python", os.path.join(TOOLS, "retarget_ual.py"), "--", UAL, anim,
                *[request(k, tune[k]["best"]["swing_c"], tune[k]["best"]["upright"]).replace(
                    g["action"], "%s=+%s" % (g["action"], flist(g["frames"])), 1) for k, g in GAITS.items()],
                *["%s=%s" % (action, flist(frames)) for action, frames in POSES_FRAMES.values()], log=log)
        # the walk's atlas frames split its stride evenly, so the planted foot stays put
        walk = GAITS["walk"]
        locked_json = os.path.join(d, "walk-frames.json")
        blender(anim, "--python", os.path.join(TOOLS, "measure_gait.py"), "--", locked_json,
                "%s:%s:auto" % (walk["action"], flist(walk["frames"])), log=log)
        frames = {k: g["frames"] for k, g in GAITS.items()}
        frames["walk"] = json.load(open(locked_json))[walk["action"]]["locked_frames"]
        os.remove(locked_json)
        json.dump(frames, open(frames_json, "w"), indent=1)
    frames = json.load(open(frames_json))
    jobs = {"idle": "idle", "fist": "fist:0,1", "lying": "%s:%s:lying" % (POSES_FRAMES["lying"][0], flist(POSES_FRAMES["lying"][1])),
            "punch": "%s:%s" % (POSES_FRAMES["punch"][0], flist(POSES_FRAMES["punch"][1])),
            "jab": "%s:%s" % (POSES_FRAMES["jab"][0], flist(POSES_FRAMES["jab"][1]))}
    for name, g in GAITS.items():
        jobs[name] = "%s:%s" % (g["action"], flist(frames[name]))
    # idle first: it chooses the stance that the raised-fist pose then shares
    order_jobs = ["idle", "fist", "walk", "lying", "punch", "jab", "jog", "run"]
    blender(anim, "--python", os.path.join(TOOLS, "render_civilian.py"), "--", renders,
            *[jobs[k] for k in order_jobs if not POSES or k in POSES], log=log)
    gait_json = os.path.join(d, "gait.json")
    if animate:
        blender(anim, "--python", os.path.join(TOOLS, "measure_gait.py"), "--", gait_json, *gait_jobs(frames), log=log)

    names = []
    for name, stem, count in ATLAS_LAYOUT:
        names += [POSES_FRAMES["punch"][0] + "_00", POSES_FRAMES["jab"][0] + "_00"] if name == "punch" else \
                 ["%s_%02d" % (stem, i) for i in range(count)]
    atlas = Image.new("RGBA", (ATLAS * len(names), ATLAS))
    for i, stem in enumerate(names):
        atlas.paste(stylize(Image.open(os.path.join(renders, stem + ".png")), ATLAS), (i * ATLAS, 0))
    os.makedirs(OUT, exist_ok=True)
    atlas.save(os.path.join(OUT, lid + ".png"))

    report = {"id": lid, "gender": look["macro"].get("gender", 1.0), "gait": json.load(open(gait_json)),
              "tune": {k: v["best"] for k, v in json.load(open(tune_json)).items()}, "frames": frames}
    report.update(measure_renders(renders))
    for line in open(log, encoding="utf-8", errors="ignore"):
        if line.startswith("CIVILIAN height"):
            report["height_m"] = float(line.split()[2])
    return report


def measure_renders(renders):
    """measure_frames.py of the rendered idle and gaits, with their mean pelvis offset."""
    out = {}
    for key, pose in (("walk", GAITS["walk"]["action"]), ("idle", "idle"), ("jog", GAITS["jog"]["action"]),
                      ("run", GAITS["run"]["action"])):
        out[key] = measure(renders, pose)
        pel = json.load(open(os.path.join(renders, pose + "_pelvis.json")))
        out[key]["pelvis_mean_atlas_px"] = [sum(p[0] for p in pel) / len(pel), sum(p[1] for p in pel) / len(pel)]
    return out


def cadence(stride):
    """Walking steps per second at WALK_SPEED for a stride (2 steps) in metres."""
    return 2 * WALK_SPEED / stride if stride else 0.0


def verdict(rep):
    idle = rep["idle"]["frames"][0]
    walk = rep["gait"][GAITS["walk"]["action"]]
    run = rep["gait"][GAITS["run"]["action"]]
    tall = rep.get("height_m", SIDE_HEIGHT) / SIDE_HEIGHT
    checks = {"idle legs": idle["legs_share"] <= LEGS_LIMIT,
              "walk cadence": WALK_CADENCE[0] <= cadence(walk["stride_m"]) * tall <= WALK_CADENCE[1],
              "walk knee": walk["thigh_forward_max_deg"] <= WALK_KNEE_LIMIT,
              "walk arms": max(f["across_m"] for f in rep["walk"]["frames"]) <= idle["across_m"] + WALK_WIDER,
              "walk legs out": max(rep["walk"]["front_max_m"], rep["walk"]["back_max_m"]) <= WALK_SIDE_LIMIT * tall,
              "run swing": run["hand_swing_m"] >= RUN_SWING * walk["hand_swing_m"]}
    for k in ("walk", "jog", "run"):
        m = rep[k]
        checks[k + " loop"] = m["inner_steps"][0] * 0.8 <= m["wrap_step"] <= m["inner_steps"][1] * 1.2
        checks[k + " pelvis"] = max(abs(v) for v in m["pelvis_mean_atlas_px"]) <= 1.0
        checks[k + " slip"] = rep["gait"][GAITS[k]["action"]]["slip_m"] <= SLIP_LIMIT
        checks[k + " symmetry"] = SYMMETRY[0] * m["back_max_m"] <= m["front_max_m"] <= SYMMETRY[1] * m["back_max_m"]
    return checks


def write_cfg_strides(reports):
    """Give every CIVILIAN line of civilians.cfg its look's measured walk stride."""
    text = open(CFG, encoding="utf-8").read()

    def line(m):
        lid = m.group(1)
        r = reports.get(lid)
        return "CIVILIAN %s.png %.2f" % (lid, r["gait"][GAITS["walk"]["action"]]["stride_m"]) if r else m.group(0)
    open(CFG, "w", encoding="utf-8", newline="\n").write(re.sub(r"^CIVILIAN (\S+)\.png.*$", line, text, flags=re.M))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--poses", default="")
    ap.add_argument("--retarget", action="store_true")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--gait", action="store_true")
    a = ap.parse_args()
    POSES.extend(p for p in a.poses.split(",") if p)
    if a.retarget:
        RETARGET.append(True)
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
                    os.path.join(d, "gait.json"), *gait_jobs(json.load(open(os.path.join(d, "frames.json")))),
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
    reports = {k: r for k, r in reports.items() if "tune" in r}        # reports from before the gait tuning are stale
    if a.summary:                                                     # the frames on disk, measured afresh
        for lid, r in reports.items():
            r.update(measure_renders(os.path.join(WORK, lid, "frames")))
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
    # out: legs beyond the body ahead/behind (m); steps: per second at WALK_SPEED; knee: thigh
    # forward (deg); hand: beyond the shoulder (m); swing: hands fore-aft, walk/run (m)
    print("%-12s %5s %5s %6s %5s %5s %6s %9s %9s %9s %11s %5s %6s %6s %6s  %s" % (
        "look", "h m", "legs%", "stride", "steps", "knee", "hand", "walk out", "jog out", "run out", "swing w/r",
        "lean", "w.slip", "j.slip", "r.slip", "failed"))
    for lid in sorted(reports):
        r = reports[lid]
        g = {k: r["gait"][GAITS[k]["action"]] for k in GAITS}
        out = ["%.2f/%.2f" % (r[k]["front_max_m"], r[k]["back_max_m"]) for k in ("walk", "jog", "run")]
        print("%-12s %5.2f %5.1f %6.2f %5.2f %5.1f %+6.2f %9s %9s %9s %5.2f/%5.2f %5.1f %6.3f %6.3f %6.3f  %s" % (
            lid, r.get("height_m", 0), 100 * r["idle"]["frames"][0]["legs_share"], g["walk"]["stride_m"],
            cadence(g["walk"]["stride_m"]), g["walk"]["thigh_forward_max_deg"], g["walk"]["hand_out_m"], *out,
            g["walk"]["hand_swing_m"], g["run"]["hand_swing_m"], g["run"]["lean_deg"],
            g["walk"]["slip_m"], g["jog"]["slip_m"], g["run"]["slip_m"],
            ", ".join(k for k, v in r["checks"].items() if not v) or "-"))


if __name__ == "__main__":
    main()
