"""CJ-004: preview civilian atlases at game scale.

    python tools/cj004/atlas_preview.py <out.png> [--scale 2] [<id> ...]

One row per look (all atlases in assets/characters/civilians/ by default): idle, walk 0 and 2,
run 0 and 2, right punch, raised fist and lying, drawn as the game draws them on foot at
1080p (19 m of world on 1080 px) times --scale, on the sidewalk texture. The first row shows
the player and the current procedural civilian for comparison.
"""
import argparse, glob, os
from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHAR_SCALE = 1.35
FRAME_M, FRAME_PX = 2.1, 144
COLUMNS = [("idle", 8), ("walk 0", 0), ("walk 2", 2), ("run 0", 14), ("run 2", 16), ("punch", 10), ("fist", 12), ("lying", 9)]

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("ids", nargs="*")
ap.add_argument("--scale", type=float, default=2.0)
a = ap.parse_args()
px_per_m = 1080 / 19.0 * a.scale
cell = round(FRAME_M * CHAR_SCALE * px_per_m)            # a 2.1 m frame as drawn
paths = [os.path.join(ROOT, "assets/characters/civilians", i + ".png") for i in a.ids] if a.ids else \
    sorted(glob.glob(os.path.join(ROOT, "assets/characters/civilians/*.png")))

label_w = 110
W, H = label_w + cell * len(COLUMNS), cell * (len(paths) + 1)
tex = Image.open(os.path.join(ROOT, "assets/textures/sidewalk.png")).convert("RGBA")
tex = tex.resize((round(4 * px_per_m), round(4 * px_per_m)))
canvas = Image.new("RGBA", (W, H))
for y in range(0, H, tex.height):
    for x in range(0, W, tex.width):
        canvas.paste(tex, (x, y))
d = ImageDraw.Draw(canvas)


def put(img, col, row, size):
    img = img.resize((size, size), Image.BILINEAR)
    cx, cy = label_w + col * cell + cell // 2, row * cell + cell // 2
    canvas.alpha_composite(img, (cx - size // 2, cy - size // 2))


# reference row: the player (Survivor unarmed idle, 0.062 world px per source px, faces +X) and
# the current procedural civilian (96 px = 1.4 m frames, faces up)
player = Image.open(os.path.join(ROOT, "assets/survivor/unarmed/idle/survivor-idle_unarmed_0.png")).convert("RGBA").rotate(90, expand=True)
k = 0.062 / 16 * CHAR_SCALE * px_per_m
canvas.alpha_composite(player.resize((round(player.width * k), round(player.height * k)), Image.BILINEAR),
                       (label_w + cell // 2 - round(player.width * k) // 2, cell // 2 - round(player.height * k) // 2))
old = os.path.join(ROOT, "build/art-sources/reference/ped-atlas-0.png")
if os.path.exists(old):
    o = Image.open(old).convert("RGBA")
    for col, idx in ((1, 8), (2, 0), (3, 2)):
        put(o.crop((idx * 96, 0, idx * 96 + 96, 96)), col, 0, round(1.4 * CHAR_SCALE * px_per_m))
d.text((4, 4), "player | current", fill=(255, 255, 255, 255))
for col, (name, _) in enumerate(COLUMNS):
    d.text((label_w + col * cell + 4, 4 + cell), name, fill=(255, 255, 255, 255))

for row, p in enumerate(paths, start=1):
    atlas = Image.open(p).convert("RGBA")
    d.text((4, row * cell + cell // 2), os.path.splitext(os.path.basename(p))[0], fill=(255, 255, 255, 255))
    for col, (name, idx) in enumerate(COLUMNS):
        frame = atlas.crop((idx * FRAME_PX, 0, (idx + 1) * FRAME_PX, FRAME_PX))
        put(frame, col, row, cell)
canvas.save(a.out)
print("wrote", a.out, canvas.size)
