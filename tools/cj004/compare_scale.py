"""CJ-004: compare rendered civilians with the player and the current civilians at game scale.

    python tools/cj004/compare_scale.py <out.png> <label=render.png> [<label=render.png> ...]

Everything is drawn as the game draws it on foot at 1080p: 19 m of world on 1080 px, people
CHAR_SCALE (1.35) times life size. Renders are CJ_FRAME_M metres square (render_civilian.py,
2.1 by default); they are reduced to the atlas frame (96 px per 1.4 m) first, then drawn. The
background is the game's sidewalk texture. The display is doubled for inspection.
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(__file__))
from stylize import stylize

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHAR_SCALE = 1.35
PX_PER_M = 1080 / 19.0 * 2          # on-foot camera at 1080p, doubled
FRAME_M = float(os.environ.get("CJ_FRAME_M", "2.1"))
ATLAS_FRAME = round(96 * FRAME_M / 1.4)


def alpha_crop(im, thr=26):
    a = np.asarray(im)[..., 3]
    ys, xs = np.where(a >= thr)
    return im.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))


def scaled(im, metres_per_px):
    k = metres_per_px * CHAR_SCALE * PX_PER_M
    return im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)


tiles = []
# The player: Survivor unarmed idle, frames face +X; 0.062 world px per source px at 16 px/m.
player = Image.open(os.path.join(ROOT, "assets/survivor/unarmed/idle/survivor-idle_unarmed_0.png")).convert("RGBA")
tiles.append(("player", scaled(alpha_crop(player).rotate(90, expand=True), 0.062 / 16)))
# Current procedural civilian: atlas frame 8 (idle), 96 px = 1.4 m, faces up.
atlas = Image.open(os.path.join(ROOT, "build/art-sources/reference/ped-atlas-0.png")).convert("RGBA")
tiles.append(("current civilian", scaled(alpha_crop(atlas.crop((8 * 96, 0, 9 * 96, 96))), 1.4 / 96)))
# The v4 generated idle has no scale of its own: shown at the current civilian's width.
v4 = alpha_crop(Image.open(os.path.join(ROOT, "assets/art/cj004/civilian-idle-master-v4.png")).convert("RGBA")).rotate(90, expand=True)
cur_w = tiles[-1][1].width
tiles.append(("v4 (width matched)", v4.resize((cur_w, round(v4.height * cur_w / v4.width)), Image.LANCZOS)))
for arg in sys.argv[2:]:
    label, path = arg.split("=", 1)
    raw = label.endswith("(raw)")
    frame = Image.open(path).convert("RGBA")
    frame = frame.resize((ATLAS_FRAME, ATLAS_FRAME), Image.LANCZOS) if raw else stylize(frame, ATLAS_FRAME)
    tiles.append((label, scaled(alpha_crop(frame), FRAME_M / ATLAS_FRAME)))

pad = 40
W = sum(t.width for _, t in tiles) + pad * (len(tiles) + 1) + 60 * len(tiles)
H = max(t.height for _, t in tiles) + 2 * pad + 30
tex = Image.open(os.path.join(ROOT, "assets/textures/sidewalk.png")).convert("RGBA")
tex = tex.resize((round(4 * PX_PER_M / 2), round(4 * PX_PER_M / 2)))    # a 256 px tile is about 4 m
canvas = Image.new("RGBA", (W, H))
for y in range(0, H, tex.height):
    for x in range(0, W, tex.width):
        canvas.paste(tex, (x, y))
d = ImageDraw.Draw(canvas)
x = pad
for label, t in tiles:
    canvas.alpha_composite(t, (x, pad + 30 + (H - 2 * pad - 30 - t.height) // 2))
    d.rectangle((x - 4, 6, x + max(t.width, 7 * len(label)) + 4, 24), fill=(0, 0, 0, 170))
    d.text((x, 9), label, fill=(255, 255, 255, 255))
    x += t.width + pad + 60
canvas.save(sys.argv[1])
print("wrote", sys.argv[1])
