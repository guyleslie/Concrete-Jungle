"""CJ-004: contact sheet of rendered frames, with visible legs/shoes tinted magenta on the right.

    python tools/cj004/preview_frames.py <render dir> <pose> <out.png>
"""
import glob, os, sys
import numpy as np
from PIL import Image

d, pose, out = sys.argv[1], sys.argv[2], sys.argv[3]
tiles = []
for path in sorted(glob.glob(os.path.join(d, pose + "_[0-9][0-9].png"))):
    im = Image.open(path).convert("RGBA")
    bg = Image.new("RGBA", im.size, (88, 90, 94, 255))
    bg.alpha_composite(im)
    m = np.asarray(Image.open(path[:-4] + "_legs.png").convert("RGBA"))
    a = np.asarray(im)[..., 3]
    hl = np.asarray(bg).copy()
    legs = (a >= 128) & (m[..., 0] > 128) & (m[..., 3] >= 128)
    hl[legs] = (255, 0, 255, 255)
    tiles.append((bg, Image.fromarray(hl)))
w, h = tiles[0][0].size
sheet = Image.new("RGBA", (len(tiles) * w, 2 * h), (30, 30, 30, 255))
for i, (c, l) in enumerate(tiles):
    sheet.paste(c, (i * w, 0)); sheet.paste(l, (i * w, h))
sheet.save(out)
