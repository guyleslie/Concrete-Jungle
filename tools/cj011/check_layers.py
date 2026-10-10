"""CJ-011: check the legs and upper-body layers against the whole renders (acceptance criterion 5).

    python tools/cj011/check_layers.py <render dir> <gait> [<gait> ...]

For every frame of a gait (render_player.py: full_, legs_ and pistol_ frames):
  - match: the pistol layer drawn over the legs layer, facing the same way, against the whole
    render: the largest alpha difference and the mean colour difference over the silhouette
    (0..255), and the share of pixels whose alpha differs by more than 0.25;
  - waist gap at +90 and -90 degrees: the upper layer turned about the frame centre over the
    legs layer; a gap is a transparent pixel enclosed by the composite (a hole), counted in cm2.
Prints one line per gait with the worst frame of each measure.
"""
import glob, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

FRAME_M = 2.1


def load(path):
    return Image.open(path).convert("RGBA")


def holes_cm2(img):
    a = np.asarray(img)[..., 3] >= 128
    filled = ndimage.binary_fill_holes(a)
    px = (filled & ~a).sum()
    m_per_px = FRAME_M / a.shape[0]
    return px * (m_per_px * 100) ** 2


src, gaits = sys.argv[1], sys.argv[2:]
for gait in gaits:
    worst = {"alpha_max": 0.0, "colour_mean": 0.0, "alpha_off_share": 0.0, "gap_cm2": 0.0, "gap_self_cm2": 0.0}
    for full_path in sorted(glob.glob(os.path.join(src, "full_%s_[0-9][0-9].png" % gait))):
        i = full_path[-6:-4]
        full = load(full_path)
        legs = load(os.path.join(src, "legs_%s_%s.png" % (gait, i)))
        upper = load(os.path.join(src, "pistol_%s_%s.png" % (gait, i)))
        comp = legs.copy()
        comp.alpha_composite(upper)
        f, c = np.asarray(full).astype(np.float32), np.asarray(comp).astype(np.float32)
        sil = (f[..., 3] > 128) | (c[..., 3] > 128)
        da = np.abs(f[..., 3] - c[..., 3]) / 255
        worst["alpha_max"] = max(worst["alpha_max"], float(da[sil].max()))
        worst["alpha_off_share"] = max(worst["alpha_off_share"], float((da[sil] > 0.25).mean()))
        worst["colour_mean"] = max(worst["colour_mean"], float(np.abs(f[..., :3] - c[..., :3])[sil].mean()))
        self_gap = holes_cm2(comp)
        worst["gap_self_cm2"] = max(worst["gap_self_cm2"], self_gap)
        for turn in (90, -90):
            t = legs.copy()
            t.alpha_composite(upper.rotate(turn, resample=Image.BILINEAR))
            worst["gap_cm2"] = max(worst["gap_cm2"], holes_cm2(t) - self_gap)
    print("%-6s match: alpha max %.2f, off %.2f %%, colour mean %.1f; holes facing alike %.1f cm2, extra at +-90 deg %.1f cm2"
          % (gait, worst["alpha_max"], 100 * worst["alpha_off_share"], worst["colour_mean"], worst["gap_self_cm2"], worst["gap_cm2"]))
