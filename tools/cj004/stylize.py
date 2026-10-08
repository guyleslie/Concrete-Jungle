"""CJ-004: reduce a 1.4 m render to a 96 px atlas frame in the game's sprite style.

The player and the procedural civilians have a dark contour and firm colours; a raw render
reads grey on the sidewalk. Steps: Lanczos reduction, contrast and saturation, light
sharpening, then a dark contour one atlas pixel wide behind the sprite.
"""
import numpy as np
from PIL import Image, ImageEnhance, ImageFilter

ATLAS_FRAME = 96
CONTRAST, SATURATION = 1.18, 1.25
OUTLINE = (24, 22, 24)


def stylize(render, size=ATLAS_FRAME):
    im = render.convert("RGBA").resize((size, size), Image.LANCZOS)
    rgb, a = im.convert("RGB"), im.getchannel("A")
    rgb = ImageEnhance.Contrast(rgb).enhance(CONTRAST)
    rgb = ImageEnhance.Color(rgb).enhance(SATURATION)
    rgb = rgb.filter(ImageFilter.UnsharpMask(radius=1.0, percent=60, threshold=2))
    sprite = Image.merge("RGBA", (*rgb.split(), a))
    # contour: the alpha grown by one pixel, filled dark, under the sprite
    grown = a.filter(ImageFilter.MaxFilter(3))
    contour = Image.new("RGBA", im.size, OUTLINE + (0,))
    contour.putalpha(grown.point(lambda v: min(255, int(v * 1.1))))
    contour.alpha_composite(sprite)
    return contour
