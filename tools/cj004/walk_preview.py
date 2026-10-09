"""CJ-004 / CJ-029: animated preview of civilians walking, jogging and running as the game plays them.

    python tools/cj004/walk_preview.py <out.gif> [<id> ...] [--walk 1.3] [--jog 2.8] [--run 5.2] [--seconds 4]

For each look (civilian-01 and civilian-15 by default), one column per gait moves up the
sidewalk at the given speed (m/s), drawn as the game draws it on foot at 1080p (19 m of world
on 1080 px, CHAR_SCALE times the civilians.cfg SCALE). The walk advances with the distance at
the look's walk cycle, the jog and run by the cadence of their GAIT records, all read from
civilians.cfg, like the game.
"""
import argparse, os, sys
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from civilians_cfg import ROOT, cadence, read

CHAR_SCALE, FPS = 1.35, 30
PX_PER_M = 1080 / 19.0

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("ids", nargs="*")
ap.add_argument("--walk", type=float, default=1.3)
ap.add_argument("--jog", type=float, default=2.8)
ap.add_argument("--run", type=float, default=5.2)
ap.add_argument("--seconds", type=float, default=4.0)
a = ap.parse_args()
cfg = read()
ids = a.ids or ["civilian-01", "civilian-15"]
fpx = cfg["frame_px"]
size = round(cfg["frame_m"] * CHAR_SCALE * cfg["scale"] * PX_PER_M)

# columns: (label, frames, speed m/s, walk cycle m or None, gait record or None)
cols = []
for lid in ids:
    atlas = Image.open(os.path.join(ROOT, "assets", "characters", "civilians", lid + ".png")).convert("RGBA")
    for gait, speed in (("walk", a.walk), ("jog", a.jog), ("run", a.run)):
        if gait not in cfg["anim"] or (gait != "walk" and gait not in cfg["gait"]):
            continue
        first, n = cfg["anim"][gait]
        frames = [atlas.crop((i * fpx, 0, (i + 1) * fpx, fpx)).resize((size, size), Image.BILINEAR)
                  for i in range(first, first + n)]
        cols.append(("%s %s %.1f m/s" % (lid, gait, speed), frames, speed,
                     cfg["looks"][lid] if gait == "walk" else None, cfg["gait"].get(gait)))

colw = size + 8
W, H = 10 + colw * len(cols), round(11 * PX_PER_M) + 40
tex = Image.open(os.path.join(ROOT, "assets", "textures", "sidewalk.png")).convert("RGBA")
tex = tex.resize((round(4 * PX_PER_M), round(4 * PX_PER_M)))        # a 256 px tile is about 4 m
ground = Image.new("RGBA", (W, H))
for y in range(0, H, tex.height):
    for x in range(0, W, tex.width):
        ground.paste(tex, (x, y))
d = ImageDraw.Draw(ground)
for c, (label, *_rest) in enumerate(cols):
    x = 10 + c * colw
    d.rectangle((x - 2, 2, x + 6 * len(label) + 2, 16), fill=(0, 0, 0, 190))
    d.text((x, 4), label, fill=(255, 255, 255, 255))

out = []
for k in range(round(a.seconds * FPS)):
    t = k / FPS
    img = ground.copy()
    for c, (_l, frames, speed, cycle_m, gait) in enumerate(cols):
        dist = speed * t
        phase = dist / cycle_m if cycle_m else t * cadence(gait, speed) / 2
        y = H - size - 6 - int((dist * PX_PER_M) % (H - size - 26))
        img.alpha_composite(frames[int(phase * len(frames)) % len(frames)], (10 + c * colw, y))
    out.append(img.convert("P", palette=Image.ADAPTIVE, colors=160))
out[0].save(a.out, save_all=True, append_images=out[1:], duration=round(1000 / FPS), loop=0)
print("wrote", a.out, "%d columns, %d KB" % (len(cols), os.path.getsize(a.out) // 1024))
