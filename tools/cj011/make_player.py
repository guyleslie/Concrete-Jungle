"""CJ-011: render every layer frame of the player and write the game's atlases and player.cfg.

    python tools/cj011/make_player.py [--render] [--only walk,run] [--jobs 3] [--samples 32]

With --render, Blender renders (build/cj011/atlas/, from build/art-sources/player/player-anim.blend,
see assets/art/cj011/README.md for how that file is made):
  - every gait of GAITS: the legs layer and, per weapon, the upper body with the weapon
    lowered (render_player.py gait jobs, the weapons in WEAPONS);
  - per weapon, the upper body's actions (ACTIONS: aim, fire, reload, melee), posed standing
    and pivoted on the standing pelvis (render_player.py with CJ_UPPER=1).
Without it, the frames on disk are packed. Packing stylizes every frame to the 144 px atlas
frame as the civilians' (stylize.py); the upper body's dark contour is left out where it lies
over the legs of the same frame, so that no seam shows at the waist. It writes
assets/characters/player/legs.png and one atlas per weapon set (<set>.png, the names of
weapons.cfg), each a grid of COLS frames, and assets/data/player.cfg, which declares them.
"""
import argparse, json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from PIL import Image, ImageEnhance, ImageFilter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "cj004"))
from stylize import CONTRAST, SATURATION, OUTLINE

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
SRC = os.path.join(ROOT, "build", "art-sources", "player")
WORK = os.path.join(ROOT, "build", "cj011", "atlas")
OUT = os.path.join(ROOT, "assets", "characters", "player")
CFG = os.path.join(ROOT, "assets", "data", "player.cfg")
TOOLS = os.path.dirname(os.path.abspath(__file__))
FRAME_PX, FRAME_M, COLS = 144, 2.1, 16
DRAW_SCALE = 1.12               # as civilians.cfg SCALE: standing, 0.58 m wide renders draw 0.65 m


def even(first, cycle, n=16):
    return [round(first + cycle * i / n, 3) for i in range(n)]


def walk_frames():
    return json.load(open(os.path.join(SRC, "walk-frames.json")))["Walk_Formal_Loop"]["locked_frames"]


# gait: (action, frames, how it plays: ("dist", metres per cycle) or ("cadence", steps per second
# at the reference speed, the reference speed m/s))
GAITS = {
    "stand": ("stand", lambda: [0.0], ("none",)),
    "walk": ("Walk_Formal_Loop", walk_frames, ("dist", 1.385)),
    "run": ("Sprint_Loop", lambda: list(range(16)), ("cadence", 3.3, 6.5)),
    "strafel": ("Walk_Strafe_Left", lambda: even(1, 44), ("dist", 1.17)),
    "strafer": ("Walk_Strafe_Right", lambda: even(1, 44), ("dist", 1.05)),
    "back": ("Walking_Backwards", lambda: even(1, 36), ("dist", 1.35)),
    "jogl": ("Jog_Strafe_Left", lambda: even(1, 21), ("cadence", 2.9, 6.5)),
    "jogr": ("Jog_Strafe_Right", lambda: even(1, 22), ("cadence", 2.9, 6.5)),
}
# render key -> the weapon set of weapons.cfg
WEAPONS = {"none": "unarmed", "knife": "knife", "flashlight": "flashlight", "pistol": "handgun",
           "rifle": "rifle", "shotgun": "shotgun"}
POSE_KEY = {"none": "unarmed"}          # the standing poses call the unarmed player "unarmed"
ACTIONS = {
    "none": [("aim", ["unarmed_aim"]), ("melee", ["clip:unarmed:Punch_Jab:5", "clip:unarmed:Punch_Cross:7"])],
    "knife": [("aim", ["knife_aim"]), ("melee", ["knife_stab0", "knife_stab1"])],
    "flashlight": [("aim", ["flashlight_aim"]), ("melee", ["flashlight_strike0", "flashlight_strike1"])],
    "pistol": [("aim", ["pistol_aim"]), ("fire", ["pistol_fire0", "pistol_fire1"]),
               ("reload", ["pistol_reload%d" % i for i in range(4)])],
    "rifle": [("aim", ["rifle_aim"]), ("fire", ["rifle_fire0", "rifle_fire1"]),
              ("reload", ["rifle_reload%d" % i for i in range(4)])],
    "shotgun": [("aim", ["shotgun_aim"]), ("fire", ["shotgun_fire0", "shotgun_fire1"]),
                ("reload", ["shotgun_reload%d" % i for i in range(4)])],
}


def blender(log, env_extra, *args):
    env = dict(os.environ, CJ_TROUSERS_MASK=os.path.join(SRC, "player_suit_diffuse_trousers_mask.png"), **env_extra)
    with open(log, "w", encoding="utf-8") as fh:
        r = subprocess.run([BLENDER, "-b", os.path.join(SRC, "player-anim.blend"), "--python",
                            os.path.join(TOOLS, "render_player.py"), "--", *args],
                           stdout=fh, stderr=subprocess.STDOUT, env=env)
    if r.returncode != 0:
        raise RuntimeError("Blender failed, see " + log)


def render_gait(name, samples):
    action, frames, _ = GAITS[name]
    d = os.path.join(WORK, name)
    os.makedirs(d, exist_ok=True)
    spec = "gait:%s:%s:%s" % (name, action, ",".join("%.3f" % f for f in frames()))
    blender(os.path.join(d, "render.log"), {"CJ_WEAPONS": ",".join(WEAPONS), "CJ_SAMPLES": str(samples)}, d, spec)
    return name


def render_actions(samples):
    d = os.path.join(WORK, "actions")
    os.makedirs(d, exist_ok=True)
    poses = [p for acts in ACTIONS.values() for _, names in acts for p in names]
    blender(os.path.join(d, "render.log"), {"CJ_UPPER": "1", "CJ_SAMPLES": str(samples)}, d, *poses)
    return "actions"


def measure_lamp():
    """The flashlight's lens and beam in every frame of its set, measured without rendering
    (render_player.py, the same poses): build/cj011/atlas/lamp.json, {frame name: [right, ahead,
    height, yaw, pitch]} from the frame centre, metres at life size and degrees."""
    d = os.path.join(WORK, "lamp")
    os.makedirs(d, exist_ok=True)
    out = {}
    for g in GAITS:
        action, frames, _ = GAITS[g]
        spec = "gait:%s:%s:%s" % (g, action, ",".join("%.3f" % f for f in frames()))
        sub = os.path.join(d, g)
        os.makedirs(sub, exist_ok=True)
        blender(os.path.join(sub, "measure.log"), {"CJ_WEAPONS": "flashlight", "CJ_MEASURE_ONLY": "1"}, sub, spec)
        for k, v in json.load(open(os.path.join(sub, "measure.json"))).items():
            if "lamp" in v:
                out[k] = v["lamp"]
    sub = os.path.join(d, "actions")
    os.makedirs(sub, exist_ok=True)
    names = [p for _, ps in ACTIONS["flashlight"] for p in ps]
    blender(os.path.join(sub, "measure.log"), {"CJ_UPPER": "1", "CJ_NO_RENDER": "1"}, sub, *names)
    for k, v in json.load(open(os.path.join(sub, "measure.json"))).items():
        if "lamp" in v:
            out[k] = v["lamp"]
    json.dump(out, open(os.path.join(WORK, "lamp.json"), "w"), indent=1)
    print("LAMP", len(out), "frames measured")


def stylize_layer(render, under=None):
    """stylize.stylize to the atlas frame, the contour left out where 'under' (an alpha image of
    the layer below, the same size) is opaque."""
    im = render.convert("RGBA").resize((FRAME_PX, FRAME_PX), Image.LANCZOS)
    rgb, a = im.convert("RGB"), im.getchannel("A")
    rgb = ImageEnhance.Contrast(rgb).enhance(CONTRAST)
    rgb = ImageEnhance.Color(rgb).enhance(SATURATION)
    rgb = rgb.filter(ImageFilter.UnsharpMask(radius=1.0, percent=60, threshold=2))
    sprite = Image.merge("RGBA", (*rgb.split(), a))
    grown = np.asarray(a.filter(ImageFilter.MaxFilter(3))).astype(np.float32)
    if under is not None:
        u = np.asarray(under.resize((FRAME_PX, FRAME_PX), Image.LANCZOS)).astype(np.float32) / 255.0
        grown *= 1.0 - np.clip(u * 1.5, 0, 1)
    contour = Image.new("RGBA", im.size, OUTLINE + (0,))
    contour.putalpha(Image.fromarray(np.clip(grown * 1.1, 0, 255).astype(np.uint8)))
    contour.alpha_composite(sprite)
    return contour


def pack(frames):
    rows = (len(frames) + COLS - 1) // COLS
    atlas = Image.new("RGBA", (COLS * FRAME_PX, rows * FRAME_PX))
    for i, f in enumerate(frames):
        atlas.paste(f, ((i % COLS) * FRAME_PX, (i // COLS) * FRAME_PX))
    return atlas


def load(path):
    return Image.open(path).convert("RGBA")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--samples", type=int, default=32)
    ap.add_argument("--lamp", action="store_true", help="measure the flashlight's lens in every frame")
    a = ap.parse_args()
    if a.lamp:
        measure_lamp()
    if a.render:
        wanted = [x for x in a.only.split(",") if x] or list(GAITS) + ["actions"]
        with ThreadPoolExecutor(max_workers=a.jobs) as pool:
            futures = [pool.submit(render_actions, a.samples) if w == "actions" else pool.submit(render_gait, w, a.samples)
                       for w in wanted]
            for f in futures:
                print("RENDERED", f.result(), flush=True)

    os.makedirs(OUT, exist_ok=True)
    lines = ["# =====================================================================================",
             "#  Concrete Jungle - the player (CJ-011), written by tools/cj011/make_player.py",
             "#",
             "#  FRAME <px> <metres>            square atlas frames; a frame covers <metres> at life size",
             "#  SCALE <scale>                  drawn this much larger, on top of the characters' scale",
             "#  COLS <n>                       atlases are grids of <n> frames per row",
             "#  LEGS <file>                    the legs layer's atlas (assets/characters/player/)",
             "#  GAIT <gait> <first> <count> dist <metres per cycle>",
             "#  GAIT <gait> <first> <count> cadence <steps/s> <at m/s>",
             "#       stand walk run strafel strafer back jogl jogr; a cycle is two steps",
             "#  SET <set> <file>               a weapon set's upper-body atlas (sets as in weapons.cfg)",
             "#  UPPER <set> <state> <first> <count>",
             "#       state: a gait (the weapon lowered, in step with the legs) or aim fire reload melee",
             "#  LAMP <set> <frame> <right> <ahead> <height> <yaw> <pitch>",
             "#       a flashlight's lens in that upper-body frame: metres from the frame centre at life",
             "#       size (right, ahead, above the ground), the beam degrees right of the facing and below",
             "#       the horizontal",
             "#  Frames face the top of the image; every layer shares the frame centre as its pivot.",
             "# =====================================================================================",
             "FRAME %d %.1f" % (FRAME_PX, FRAME_M), "SCALE %.2f" % DRAW_SCALE, "COLS %d" % COLS, "LEGS legs.png"]
    legs_frames, legs_alpha = [], {}
    for g, (_, _, play) in GAITS.items():
        d = os.path.join(WORK, g)
        n = len(GAITS[g][1]())
        first = len(legs_frames)
        for i in range(n):
            im = load(os.path.join(d, "legs_%s_%02d.png" % (g, i)))
            legs_alpha[(g, i)] = im.getchannel("A")
            legs_frames.append(stylize_layer(im))
        if play[0] == "dist":
            lines.append("GAIT %s %d %d dist %.3f" % (g, first, n, play[1]))
        elif play[0] == "cadence":
            lines.append("GAIT %s %d %d cadence %.2f %.1f" % (g, first, n, play[1], play[2]))
        else:
            lines.append("GAIT %s %d %d dist 0" % (g, first, n))
    pack(legs_frames).save(os.path.join(OUT, "legs.png"), optimize=True)
    lamp_path = os.path.join(WORK, "lamp.json")
    lamp = json.load(open(lamp_path)) if os.path.exists(lamp_path) else {}
    for key, set_name in WEAPONS.items():
        frames, lamp_lines = [], []
        lines.append("SET %s %s.png" % (set_name, set_name))
        for g in GAITS:
            d = os.path.join(WORK, g)
            n = len(GAITS[g][1]())
            first = len(frames)
            for i in range(n):
                if "%s_%s_%02d" % (key, g, i) in lamp:
                    lamp_lines.append("LAMP %s %d %s" % (set_name, len(frames), " ".join("%.3f" % x for x in lamp["%s_%s_%02d" % (key, g, i)])))
                frames.append(stylize_layer(load(os.path.join(d, "%s_%s_%02d.png" % (key, g, i))), legs_alpha[(g, i)]))
            lines.append("UPPER %s %s %d %d" % (set_name, g, first, n))
        for state, names in ACTIONS[key]:
            first = len(frames)
            for nm in names:
                if nm in lamp:
                    lamp_lines.append("LAMP %s %d %s" % (set_name, len(frames), " ".join("%.3f" % x for x in lamp[nm])))
                f = nm.replace(":", "_").replace(".", "_")
                frames.append(stylize_layer(load(os.path.join(WORK, "actions", f + ".png")), legs_alpha[("stand", 0)]))
            lines.append("UPPER %s %s %d %d" % (set_name, state, first, len(names)))
        lines += lamp_lines
        pack(frames).save(os.path.join(OUT, set_name + ".png"), optimize=True)
        print("ATLAS", set_name, len(frames), "frames")
    open(CFG, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print("wrote", CFG)


if __name__ == "__main__":
    main()
