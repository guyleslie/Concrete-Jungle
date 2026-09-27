"""
make_unarmed.py - derive an UNARMED animation set from the Survivor "knife" frames.

The Top Down Survivor pack (Riley Gombart, CC-BY 3.0) has no empty-handed animations,
but its knife set shows the character in a boxer-like stance with a small knife in the
right hand. This tool erases the blade (and its black outline) from every knife frame,
producing idle / move / melee(punch) frames in exactly the original art style.

Usage (from the project root):
    python tools/make_unarmed.py assets/survivor
Writes: assets/survivor/unarmed/{idle,move,meleeattack}/survivor-<state>_unarmed_<n>.png
Requires: Pillow, numpy
"""
import os, sys, glob
import numpy as np
from PIL import Image

def dilate(mask, r):
    out = mask.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy > r * r:
                continue
            out |= np.roll(np.roll(mask, dy, 0), dx, 1)
    return out

def strip_blade(img):
    a = np.array(img.convert("RGBA")).astype(np.int32)
    rgb, al = a[..., :3], a[..., 3]
    mx, mn = rgb.max(-1), rgb.min(-1)
    mean = rgb.mean(-1)
    sat = mx - mn
    # The knife is drawn in perfectly neutral greys (r == g == b); the jacket, vest and
    # hair all carry a slight colour cast, so "neutral mid-grey" isolates the blade.
    blade = (al > 40) & (sat <= 6) & (mean >= 48) & (mean <= 130)
    if blade.sum() == 0:
        return img, 0
    # skin of the hands (reddish brown) must be preserved together with its outline
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    skin = (al > 200) & (r > g + 12) & (r > b + 12) & (mean > 45)
    near_skin = dilate(skin, 3)
    zone = dilate(blade, 6)
    dark = (sat <= 10) & (mean < 60)                 # black outline / dark knife handle
    kill = (blade | (zone & dark)) & ~near_skin
    a[..., 3] = np.where(kill, 0, al)
    remove_specks(a, 60)
    return Image.fromarray(a.astype(np.uint8), "RGBA"), int(kill.sum())

def remove_specks(a, min_size):
    """Delete small detached islands (anti-aliased leftovers of the blade tip)."""
    h, w = a.shape[:2]
    solid = a[..., 3] > 8
    seen = np.zeros_like(solid)
    for y0 in range(h):
        for x0 in range(w):
            if not solid[y0, x0] or seen[y0, x0]:
                continue
            stack, comp = [(y0, x0)], []
            seen[y0, x0] = True
            while stack:
                y, x = stack.pop()
                comp.append((y, x))
                for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                    if 0 <= ny < h and 0 <= nx < w and solid[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
            if len(comp) < min_size:
                for y, x in comp:
                    a[y, x, 3] = 0

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "assets/survivor"
    for state in ("idle", "move", "meleeattack"):
        src_dir = os.path.join(root, "knife", state)
        dst_dir = os.path.join(root, "unarmed", state)
        os.makedirs(dst_dir, exist_ok=True)
        files = glob.glob(os.path.join(src_dir, "*.png"))
        for f in files:
            n = f.rsplit("_", 1)[-1]
            out, removed = strip_blade(Image.open(f))
            out.save(os.path.join(dst_dir, "survivor-%s_unarmed_%s" % (state, n)))
        print(state, len(files), "frames")

if __name__ == "__main__":
    main()
