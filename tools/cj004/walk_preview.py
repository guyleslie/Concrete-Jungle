"""CJ-004: atlas strip and animated walk preview of a rendered civilian.

    python tools/cj004/walk_preview.py <idle render> <walk render dir> <walk pose> <out prefix>

Writes <prefix>-strip.png, the nine 96 px frames the game would store (idle, then walk 0-7,
stylized), and <prefix>-walk.gif: the current procedural civilian (left) and the new one
(right) walking up a sidewalk at 1.5 m/s, drawn as the game draws them on foot at 1080p
(doubled): 1.4 m frames times CHAR_SCALE, the walk cycle advancing 8 frames per 1.3 m.
"""
import glob, os, sys
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from stylize import stylize

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHAR_SCALE, PX_PER_M, FRAME_M = 1.35, 1080 / 19.0 * 2, 1.4
SPEED, CYCLE_M, GIF_FPS = 1.5, 1.3, 12

idle_path, walk_dir, walk_pose, prefix = sys.argv[1:5]
new = [stylize(Image.open(idle_path))]
new += [stylize(Image.open(p)) for p in sorted(glob.glob(os.path.join(walk_dir, walk_pose + "_[0-9][0-9].png")))]
assert len(new) == 9, "expected an idle and eight walk frames"
strip = Image.new("RGBA", (96 * 9, 96))
for i, f in enumerate(new):
    strip.paste(f, (i * 96, 0))
strip.save(prefix + "-strip.png")

atlas = Image.open(os.path.join(ROOT, "build/art-sources/reference/ped-atlas-0.png")).convert("RGBA")
old = [atlas.crop((i * 96, 0, (i + 1) * 96, 96)) for i in (8, 0, 1, 2, 3, 4, 5, 6, 7)]

size = round(FRAME_M * CHAR_SCALE * PX_PER_M)            # drawn frame size in the preview
W, H = 2 * size + 120, 760
tex = Image.open(os.path.join(ROOT, "assets/textures/sidewalk.png")).convert("RGBA")
tex = tex.resize((round(2 * PX_PER_M), round(2 * PX_PER_M)))
ground = Image.new("RGBA", (W, H))
for y in range(0, H, tex.height):
    for x in range(0, W, tex.width):
        ground.paste(tex, (x, y))

frames = []
step = SPEED * PX_PER_M / GIF_FPS                        # px per GIF frame
n = int((H + size) / step)
for k in range(n):
    canvas = ground.copy()
    walked = k * SPEED / GIF_FPS                          # metres
    cycle = int(walked / CYCLE_M * 8) % 8
    y = round(H - k * step - size / 2)
    for col, set_ in enumerate((old, new)):
        sprite = set_[1 + cycle].resize((size, size), Image.BILINEAR)
        canvas.alpha_composite(sprite, (40 + col * (size + 40), y - size // 2))
    frames.append(canvas.convert("RGB"))
frames[0].save(prefix + "-walk.gif", save_all=True, append_images=frames[1:], duration=round(1000 / GIF_FPS), loop=0)
print("wrote", prefix + "-strip.png", prefix + "-walk.gif", len(frames), "frames")
