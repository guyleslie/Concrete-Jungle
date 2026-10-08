"""CJ-004: measure rendered civilian frames (see render_civilian.py).

    python tools/cj004/measure_frames.py <render dir> <pose>

Per frame: opaque pixels, visible leg/shoe pixels (alpha >= 0.5 and white in the _legs mask),
the silhouette's alpha box across and along the facing (alpha >= 0.1, metres) and the
centre of the opaque torso band, which must not drift during a cycle.
"""
import glob, os, sys
import numpy as np
from PIL import Image

FRAME_M, FRAME_PX = 1.4, 384
ATLAS = 96
MPP = FRAME_M / FRAME_PX

d, pose = sys.argv[1], sys.argv[2]
alphas, centres = [], []
for path in sorted(glob.glob(os.path.join(d, pose + "_[0-9][0-9].png"))):
    a = np.asarray(Image.open(path).convert("RGBA"))[..., 3]
    m = np.asarray(Image.open(path[:-4] + "_legs.png").convert("RGBA"))
    vis = a >= 128
    legs = vis & (m[..., 0] > 128) & (m[..., 3] >= 128)
    ys, xs = np.where(a >= 26)
    across = (xs.max() - xs.min() + 1) * MPP
    along = (ys.max() - ys.min() + 1) * MPP
    body = vis & ~legs                       # head, torso and arms: the part that must not drift
    by, bx = np.where(body)
    centre = (bx.mean() * ATLAS / FRAME_PX, by.mean() * ATLAS / FRAME_PX)
    alphas.append(a.astype(np.float32) / 255)
    centres.append(centre)
    print("%s opaque %d px, legs visible %d px (%.1f %%), across %.3f m, along %.3f m, body centre (%.2f, %.2f) atlas px" %
          (os.path.basename(path), vis.sum(), legs.sum(), 100.0 * legs.sum() / vis.sum(), across, along, *centre))

if len(alphas) > 1:
    cx = [c[0] for c in centres]; cy = [c[1] for c in centres]
    print("body centre drift: x %.2f, y %.2f atlas px (max - min)" % (max(cx) - min(cx), max(cy) - min(cy)))
    steps = [float(np.abs(alphas[i] - alphas[(i + 1) % len(alphas)]).mean()) for i in range(len(alphas))]
    inner = steps[:-1]
    print("silhouette change between frames: " + ", ".join("%.4f" % v for v in steps) +
          "; wrap-around %.4f vs others %.4f-%.4f" % (steps[-1], min(inner), max(inner)))
