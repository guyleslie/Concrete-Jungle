"""CJ-004: measure rendered civilian frames (see render_civilian.py).

    python tools/cj004/measure_frames.py <render dir> <pose>

Per frame: opaque pixels, visible leg/shoe pixels (alpha >= 0.5 and white in the _legs mask)
and their share of the silhouette, the silhouette's alpha box across and along the facing
(alpha >= 0.1, metres) and the centre of the head, torso and arms in atlas pixels. For a
sequence: the drift of that centre, and the silhouette change between neighbouring frames
including the wrap-around from the last frame to the first, which closes a cycle.
CJ_FRAME_M is the frame size in metres (2.1, as rendered); the atlas frame is 96 px per 1.4 m.
"""
import glob, os, sys
import numpy as np
from PIL import Image

FRAME_M = float(os.environ.get("CJ_FRAME_M", "2.1"))
ATLAS = round(96 * FRAME_M / 1.4)


def measure(d, pose):
    frames, alphas = [], []
    for path in sorted(glob.glob(os.path.join(d, pose + "_[0-9][0-9].png"))):
        a = np.asarray(Image.open(path).convert("RGBA"))[..., 3]
        m = np.asarray(Image.open(path[:-4] + "_legs.png").convert("RGBA"))
        px = a.shape[0]
        mpp = FRAME_M / px
        vis = a >= 128
        legs = vis & (m[..., 0] > 128) & (m[..., 3] >= 128)
        ys, xs = np.where(a >= 26)
        body = vis & ~legs                       # head, torso and arms: the part that must not drift
        by, bx = np.where(body)
        frames.append({
            "file": os.path.basename(path),
            "opaque_px": int(vis.sum()),
            "legs_px": int(legs.sum()),
            "legs_share": float(legs.sum() / max(1, vis.sum())),
            "across_m": float((xs.max() - xs.min() + 1) * mpp),
            "along_m": float((ys.max() - ys.min() + 1) * mpp),
            "body_centre_atlas_px": [float(bx.mean() * ATLAS / px), float(by.mean() * ATLAS / px)],
        })
        alphas.append(a.astype(np.float32) / 255)
    result = {"pose": pose, "frames": frames}
    if len(alphas) > 1:
        cx = [f["body_centre_atlas_px"][0] for f in frames]
        cy = [f["body_centre_atlas_px"][1] for f in frames]
        steps = [float(np.abs(alphas[i] - alphas[(i + 1) % len(alphas)]).mean()) for i in range(len(alphas))]
        result.update(drift_atlas_px=[max(cx) - min(cx), max(cy) - min(cy)],
                      mean_centre_offset_atlas_px=[float(np.mean(cx)) - ATLAS / 2, float(np.mean(cy)) - ATLAS / 2],
                      steps=steps, wrap_step=steps[-1], inner_steps=[min(steps[:-1]), max(steps[:-1])])
    return result


if __name__ == "__main__":
    res = measure(sys.argv[1], sys.argv[2])
    for f in res["frames"]:
        print("%s opaque %d px, legs %d px (%.1f %%), across %.3f m, along %.3f m, body centre (%.2f, %.2f) atlas px" %
              (f["file"], f["opaque_px"], f["legs_px"], 100 * f["legs_share"], f["across_m"], f["along_m"], *f["body_centre_atlas_px"]))
    if "steps" in res:
        print("body centre drift: x %.2f, y %.2f atlas px; cycle mean offset from the frame centre (%.2f, %.2f)" %
              (*res["drift_atlas_px"], *res["mean_centre_offset_atlas_px"]))
        print("silhouette change: wrap-around %.4f vs others %.4f-%.4f" % (res["wrap_step"], *res["inner_steps"]))
