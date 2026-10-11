"""CJ-011: the player's renders next to the shipped player, at game scale, and their widths.

    python tools/cj011/compare_player.py <render dir> <out.png> <pose> [<pose> ...]

Drawn as tools/cj004/compare_scale.py draws people: the on-foot camera at 1080p, doubled,
people CHAR_SCALE times life size, on the sidewalk texture; the renders are stylized to the
144 px atlas frame first. Prints each pose's width across the facing direction (alpha >= 0.1)
in metres against the old Survivor player's 0.65 m (acceptance criterion 6: within 10 %).
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "cj004"))
from stylize import stylize

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHAR_SCALE = 1.35
PX_PER_M = 1080 / 19.0 * 2          # on-foot camera at 1080p, doubled


def alpha_crop(im, thr=26):
    a = np.asarray(im)[..., 3]
    ys, xs = np.where(a >= thr)
    return im.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))


def scaled(im, metres_per_px):
    k = metres_per_px * CHAR_SCALE * PX_PER_M
    return im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)


FRAME_M, ATLAS = 2.1, 144
PLAYER_WIDTH_M = 0.65


def width_m(render):
    a = np.asarray(render.convert("RGBA"))[..., 3]
    xs = np.where((a >= 26).any(axis=0))[0]
    return (xs.max() - xs.min() + 1) * FRAME_M / a.shape[1]


src, out, poses = sys.argv[1], sys.argv[2], sys.argv[3:]
tiles = []
sys.path.insert(0, os.path.dirname(__file__))
from player_frame import player_stand
for label, set_name in (("shipped, pistol", "handgun"), ("shipped, rifle", "rifle")):
    im, mpp = player_stand(set_name)
    tiles.append((label, scaled(alpha_crop(im), mpp)))
for pose in poses:
    render = Image.open(os.path.join(src, pose + ".png"))
    w = width_m(render)
    print("%-12s width %.2f m (%+.0f %% of the player's %.2f m)" % (pose, w, 100 * (w / PLAYER_WIDTH_M - 1), PLAYER_WIDTH_M))
    tiles.append((pose, scaled(alpha_crop(stylize(render, ATLAS)), FRAME_M / ATLAS)))

pad = 30
W = sum(t.width for _, t in tiles) + pad * (len(tiles) + 1) + 30 * len(tiles)
H = max(t.height for _, t in tiles) + 2 * pad + 30
tex = Image.open(os.path.join(ROOT, "assets/textures/sidewalk.png")).convert("RGBA")
tex = tex.resize((round(4 * PX_PER_M / 2), round(4 * PX_PER_M / 2)))
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
    x += t.width + pad + 30
canvas.save(out)
print("wrote", out)
