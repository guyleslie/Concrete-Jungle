"""CJ-004: build, animate, render and measure the civilian looks, and write their atlases.

    python tools/cj004/make_civilians.py [--only civilian-01,civilian-15] [--jobs 3] [--poses idle,fist] [--summary]

--poses re-renders only those jobs from the existing civilian-anim.blend, then rebuilds the
atlas and the report from the frames on disk. --summary only re-checks and prints the report.

For every look in tools/cj004/looks.json (Blender 5.2 with MPFB and the MakeHuman packs, the
Universal Animation Library unpacked under build/art-sources/ual/):
  1. build_civilian.py    -> build/art-sources/civilians/<id>/civilian.blend
  2. retarget_ual.py      -> civilian-anim.blend with only the frames that are rendered
  3. render_civilian.py   -> frames and leg masks, 2.1 m per frame
  4. stylize.py           -> assets/characters/civilians/<id>.png, 22 frames of 144 px
  5. measure_frames.py    -> build/art-sources/civilians/report.json and a summary table
Atlas frame order (src/sprite_gen.h): walk 0-7, idle, lying, right punch, left punch,
raised fist 0-1, run 0-7.
"""
import argparse, json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from stylize import stylize
from measure_frames import measure, FRAME_M, ATLAS

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
UAL = os.path.join(ROOT, "build", "art-sources", "ual", "Universal Animation Library[Standard]", "Unreal-Godot", "UAL1_Standard.glb")
WORK = os.path.join(ROOT, "build", "art-sources", "civilians")
OUT = os.path.join(ROOT, "assets", "characters", "civilians")
TOOLS = os.path.dirname(os.path.abspath(__file__))

WALK = ("Walk_Formal_Loop", [0, 4, 8, 12, 16, 20, 24, 28])
RUN = ("Sprint_Loop", [0, 2, 4, 6, 8, 10, 12, 14])
RUN_UPRIGHT = 25        # degrees: the library sprint leans about 40, a runner at 5 m/s about 15-20
PUNCH_RIGHT = ("Punch_Cross", [7])
PUNCH_LEFT = ("Punch_Jab", [5])
LYING = ("Death01", [57])
PLAYER_WIDTH_M = 0.65
LEGS_LIMIT = 0.04
DRAW_SCALE = 1.12       # civilians.cfg SCALE: the average man within 10 % of the player's width


def blender(*args, log):
    with open(log, "a", encoding="utf-8") as fh:
        r = subprocess.run([BLENDER, "-b", *args], stdout=fh, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        raise RuntimeError("Blender failed, see " + log)


def frames_arg(action):
    return ",".join(str(f) for f in action[1])


POSES = []        # set from --poses: re-render only these jobs


def make(look):
    lid = look["id"]
    d = os.path.join(WORK, lid)
    os.makedirs(d, exist_ok=True)
    log = os.path.join(d, "blender.log")
    blend, anim = os.path.join(d, "civilian.blend"), os.path.join(d, "civilian-anim.blend")
    jobs = {"walk": "%s:%s" % (WALK[0], frames_arg(WALK)), "idle": "idle",
            "lying": "%s:%s:lying" % (LYING[0], frames_arg(LYING)),
            "punch": "%s:%s" % (PUNCH_RIGHT[0], frames_arg(PUNCH_RIGHT)), "jab": "%s:%s" % (PUNCH_LEFT[0], frames_arg(PUNCH_LEFT)),
            "fist": "fist:0,1", "run": "%s:%s" % (RUN[0], frames_arg(RUN))}
    if not POSES:
        open(log, "w").close()
        look_file = os.path.join(d, "look.json")
        json.dump(look, open(look_file, "w"), indent=2)
        blender("--factory-startup", "--python", os.path.join(TOOLS, "build_civilian.py"), "--", look_file, blend, log=log)
        blender(blend, "--python", os.path.join(TOOLS, "retarget_ual.py"), "--", UAL, anim,
                *["%s=%s" % (a[0], frames_arg(a)) for a in (WALK, PUNCH_RIGHT, PUNCH_LEFT, LYING)],
                "%s=%s@%d" % (RUN[0], frames_arg(RUN), RUN_UPRIGHT), log=log)
    renders = os.path.join(d, "frames")
    # idle first: it chooses the stance that the raised-fist pose then shares
    order_jobs = ["idle", "fist", "walk", "lying", "punch", "jab", "run"]
    blender(anim, "--python", os.path.join(TOOLS, "render_civilian.py"), "--", renders,
            *[jobs[k] for k in order_jobs if not POSES or k in POSES], log=log)

    order = ["%s_%02d" % (WALK[0], i) for i in range(8)] + ["idle_00", LYING[0] + "_00", PUNCH_RIGHT[0] + "_00",
             PUNCH_LEFT[0] + "_00", "fist_00", "fist_01"] + ["%s_%02d" % (RUN[0], i) for i in range(8)]
    atlas = Image.new("RGBA", (ATLAS * len(order), ATLAS))
    for i, name in enumerate(order):
        atlas.paste(stylize(Image.open(os.path.join(renders, name + ".png")), ATLAS), (i * ATLAS, 0))
    os.makedirs(OUT, exist_ok=True)
    atlas.save(os.path.join(OUT, lid + ".png"))

    report = {"id": lid, "gender": look["macro"].get("gender", 1.0)}
    for key, pose in (("walk", WALK[0]), ("idle", "idle"), ("run", RUN[0])):
        report[key] = measure(renders, pose)
        pel = json.load(open(os.path.join(renders, pose + "_pelvis.json")))
        report[key]["pelvis_mean_atlas_px"] = [sum(p[0] for p in pel) / len(pel), sum(p[1] for p in pel) / len(pel)]
    for line in open(log, encoding="utf-8", errors="ignore"):
        if line.startswith("CIVILIAN height"):
            report["height_m"] = float(line.split()[2])
    return report


def verdict(rep):
    idle = rep["idle"]["frames"][0]
    checks = {"idle legs": idle["legs_share"] <= LEGS_LIMIT}
    for k in ("walk", "run"):
        m = rep[k]
        checks[k + " loop"] = m["inner_steps"][0] * 0.8 <= m["wrap_step"] <= m["inner_steps"][1] * 1.2
        checks[k + " pelvis"] = max(abs(v) for v in m["pelvis_mean_atlas_px"]) <= 1.0
    return checks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--poses", default="")
    ap.add_argument("--summary", action="store_true")
    a = ap.parse_args()
    POSES.extend(p for p in a.poses.split(",") if p)
    looks = json.load(open(os.path.join(TOOLS, "looks.json"), encoding="utf-8"))["looks"]
    if a.only:
        wanted = set(a.only.split(","))
        looks = [l for l in looks if l["id"] in wanted]
    report_path = os.path.join(WORK, "report.json")
    reports = json.load(open(report_path)) if os.path.exists(report_path) else {}
    for look in looks:                      # older reports lack the gender
        if look["id"] in reports:
            reports[look["id"]]["gender"] = look["macro"].get("gender", 1.0)
            reports[look["id"]]["checks"] = verdict(reports[look["id"]])
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for rep in pool.map(make, [] if a.summary else looks):
            rep["checks"] = verdict(rep)
            reports[rep["id"]] = rep
            json.dump(reports, open(report_path, "w"), indent=1)
            print("DONE", rep["id"], "pass" if all(rep["checks"].values()) else "FAIL",
                  {k: v for k, v in rep["checks"].items() if not v}, flush=True)
    # Width is a property of the set: the average man, as drawn (DRAW_SCALE), within 10 % of the player.
    men = [r for r in reports.values() if r.get("gender", 1.0) >= 0.5]
    if men:
        idle_w = DRAW_SCALE * sum(r["idle"]["frames"][0]["across_m"] for r in men) / len(men)
        walk_w = DRAW_SCALE * sum(max(f["across_m"] for f in r["walk"]["frames"]) for r in men) / len(men)
        print("average man as drawn: idle %.3f m (%+.0f %%), walk %.3f m (%+.0f %%) against the player's %.2f m: %s" % (
            idle_w, 100 * (idle_w / PLAYER_WIDTH_M - 1), walk_w, 100 * (walk_w / PLAYER_WIDTH_M - 1), PLAYER_WIDTH_M,
            "pass" if max(abs(idle_w / PLAYER_WIDTH_M - 1), abs(walk_w / PLAYER_WIDTH_M - 1)) <= 0.10 else "FAIL"))
    print("%-12s %6s %6s %7s %7s %7s %8s %8s  %s" % ("look", "h m", "legs%", "idle m", "walk m", "run m", "walk px", "run px", "failed"))
    for lid in sorted(reports):
        r = reports[lid]
        print("%-12s %6.2f %6.1f %7.3f %7.3f %7.3f %8.2f %8.2f  %s" % (
            lid, r.get("height_m", 0), 100 * r["idle"]["frames"][0]["legs_share"], r["idle"]["frames"][0]["across_m"],
            max(f["across_m"] for f in r["walk"]["frames"]), max(f["across_m"] for f in r["run"]["frames"]),
            max(abs(v) for v in r["walk"]["pelvis_mean_atlas_px"]), max(abs(v) for v in r["run"]["pelvis_mean_atlas_px"]),
            ", ".join(k for k, v in r["checks"].items() if not v) or "-"))


if __name__ == "__main__":
    main()
