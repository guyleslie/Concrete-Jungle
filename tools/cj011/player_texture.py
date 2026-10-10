"""CJ-011: recolour male_casualsuit05 (jacket over a striped shirt, jeans) into the player's
clothes: an olive green jacket, a dark shirt and dark trousers, like the Survivor player.

    python tools/cj011/player_texture.py <male_casualsuit05_diffuse.png> <out.png>

Also writes <out>_trousers_mask.png: white where the texture is the trousers (the denim grown
islands, grown a few pixels into the empty background only), for the render's legs and upper-body layers.

The texture's seams, folds and shading carry over:
  - denim (blue pixels): dark charcoal trousers, the denim's light and dark kept at reduced
    contrast;
  - the shirt: every UV island that contains the shirt's red stripes becomes a plain dark
    olive shirt, shaded by the island's blurred brightness, so no stripe shows;
  - everything else (the grey-green jacket): pushed towards olive green.
"""
import sys
import numpy as np
from scipy import ndimage
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGBA")
a = np.asarray(im).astype(np.float32) / 255.0
rgb, alpha = a[..., :3], a[..., 3:]
r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
lum = 0.3 * r + 0.59 * g + 0.11 * b

denim = (b > r + 0.06) & (b > g)
red = (r > g + 0.12) & (r > b + 0.08)
islands, n = ndimage.label(ndimage.binary_dilation(lum > 0.03, iterations=3))
red_count = np.bincount(islands[red], minlength=n + 1)
size = np.bincount(islands.ravel(), minlength=n + 1)
striped = np.flatnonzero((red_count > 0.02 * size) & (np.arange(n + 1) > 0))
stripe = np.isin(islands, striped) & (lum > 0.01)

olive = np.array([0.86, 0.90, 0.66], np.float32)           # multiplier on the jacket's grey-green
jacket = np.clip(rgb * olive * 1.05, 0, 1)

trousers_base = np.array([0.105, 0.105, 0.115], np.float32)
t = (lum - lum[denim].mean() if denim.any() else lum) * 0.45
trousers = np.clip(trousers_base + t[..., None], 0, 1)

shade = ndimage.gaussian_filter(lum, 6)
shirt = np.clip(np.array([0.085, 0.09, 0.06], np.float32) * (0.75 + 0.8 * shade[..., None]), 0, 1)

res = np.where(denim[..., None], trousers, np.where(stripe[..., None], shirt, jacket))
Image.fromarray((np.concatenate([res, alpha], -1) * 255).round().astype(np.uint8), "RGBA").save(out)
# whole denim islands, grown only into the empty background (not into the jacket's islands)
denim_count = np.bincount(islands[denim], minlength=n + 1)
denim_islands = np.flatnonzero((denim_count > 0.5 * size) & (np.arange(n + 1) > 0))
whole = np.isin(islands, denim_islands) & (lum > 0.01)
grown = whole | (ndimage.binary_dilation(whole, iterations=4) & (lum <= 0.03))
Image.fromarray((grown * 255).astype(np.uint8), "L").save(out[:-4] + "_trousers_mask.png")
print("denim %.1f %%, shirt %.1f %% in %d islands" % (100 * denim.mean(), 100 * stripe.mean(), len(striped)))
