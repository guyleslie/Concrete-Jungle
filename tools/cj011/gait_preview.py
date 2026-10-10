"""CJ-011: animated preview of the player's layers walking and running, as the game would play them.

    python tools/cj011/gait_preview.py <render dir> <out.gif> [--stride 1.32] [--walk 1.5] [--run 6.5]
                                       [--cadence 3.3] [--seconds 4]

Reads the gait renders of render_player.py (legs_<gait>_<nn>.png, <weapon>_<gait>_<nn>.png),
stylizes them to the 144 px atlas frame and draws the upper-body layer over the legs layer,
each turned about the frame centre (the shared pivot), on the sidewalk at game scale (the
on-foot camera at 1080p, CHAR_SCALE times life size, times DRAW_SCALE as the civilians). One
column per case moves up the sidewalk at its speed:
  - walk and run, both layers facing the motion, with the pistol and with the rifle;
  - strafe left and right, backpedal: the strafe and backpedal gaits (strafel, strafer, back),
    both layers turned so that the motion runs up the screen while the player faces the side
    or down.
The walking gaits advance with the distance at their measured travel per cycle (--stride and
the travel.json of measure_travel.py for the others), the run by cadence (one cycle per two
steps).
"""
import argparse, glob, json, os, sys
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "cj004"))
from stylize import stylize

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHAR_SCALE, DRAW_SCALE, FPS = 1.35, 1.12, 30
PX_PER_M = 1080 / 19.0
FRAME_M, ATLAS = 2.1, 144

ap = argparse.ArgumentParser()
ap.add_argument("src")
ap.add_argument("out")
ap.add_argument("--stride", type=float, default=1.32)
ap.add_argument("--walk", type=float, default=1.5)
ap.add_argument("--run", type=float, default=6.5)
ap.add_argument("--cadence", type=float, default=3.3)
ap.add_argument("--seconds", type=float, default=4.0)
ap.add_argument("--travel", default=os.path.join(ROOT, "build", "art-sources", "player", "travel.json"))
a = ap.parse_args()
size = round(FRAME_M * CHAR_SCALE * DRAW_SCALE * PX_PER_M)


def frames(stem):
    paths = sorted(glob.glob(os.path.join(a.src, stem + "_[0-9][0-9].png")))
    return [stylize(Image.open(p), ATLAS).resize((size, size), Image.BILINEAR) for p in paths]


GAITS = {"walk": None, "run": None, "strafel": "Walk_Strafe_Left", "strafer": "Walk_Strafe_Right",
         "back": "Walking_Backwards"}
travel = json.load(open(a.travel)) if os.path.exists(a.travel) else {}
cycle_m = {g: travel[act]["travel_m"] for g, act in GAITS.items() if act and act in travel}
cycle_m["walk"] = a.stride
legs = {g: frames("legs_" + g) for g in GAITS}
upper = {(w, g): frames("%s_%s" % (w, g)) for w in ("pistol", "rifle") for g in GAITS}

# (label, gait, weapon, speed, legs turn, upper turn, backwards); turns in degrees, counter-clockwise
cases = [("walk pistol", "walk", "pistol", a.walk, 0, 0, False),
         ("walk rifle", "walk", "rifle", a.walk, 0, 0, False),
         ("run pistol", "run", "pistol", a.run, 0, 0, False),
         ("run rifle", "run", "rifle", a.run, 0, 0, False),
         ("strafe L pistol", "strafel", "pistol", a.walk, -90, -90, False),
         ("strafe R rifle", "strafer", "rifle", a.walk, 90, 90, False),
         ("back pistol", "back", "pistol", a.walk, 180, 180, False),
         ("back rifle", "back", "rifle", a.walk, 180, 180, False)]

colw = size + 6
W, H = 10 + colw * len(cases), round(9 * PX_PER_M) + 40
tex = Image.open(os.path.join(ROOT, "assets", "textures", "sidewalk.png")).convert("RGBA")
tex = tex.resize((round(4 * PX_PER_M), round(4 * PX_PER_M)))
ground = Image.new("RGBA", (W, H))
for y in range(0, H, tex.height):
    for x in range(0, W, tex.width):
        ground.paste(tex, (x, y))
d = ImageDraw.Draw(ground)
for c, case in enumerate(cases):
    x = 8 + c * colw
    d.rectangle((x - 2, 2, x + 6 * len(case[0]) + 2, 16), fill=(0, 0, 0, 190))
    d.text((x, 4), case[0], fill=(255, 255, 255, 255))

out = []
for k in range(round(a.seconds * FPS)):
    t = k / FPS
    img = ground.copy()
    for c, (_l, gait, weapon, speed, lturn, uturn, back) in enumerate(cases):
        dist = speed * t
        phase = t * a.cadence / 2 if gait == "run" else dist / cycle_m[gait]
        if back:
            phase = -phase
        n = len(legs[gait])
        i = int(phase * n) % n
        y = H - size - 6 - int((dist * PX_PER_M) % (H - size - 26))
        x = 10 + c * colw
        img.alpha_composite(legs[gait][i].rotate(lturn, resample=Image.BILINEAR), (x, y))
        img.alpha_composite(upper[(weapon, gait)][i].rotate(uturn, resample=Image.BILINEAR), (x, y))
    out.append(img.convert("P", palette=Image.ADAPTIVE, colors=200))
out[0].save(a.out, save_all=True, append_images=out[1:], duration=round(1000 / FPS), loop=0)
print("wrote", a.out, "%d KB" % (os.path.getsize(a.out) // 1024))
