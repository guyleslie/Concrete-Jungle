"""CJ-011: the shipped player's standing frame, for the comparison tools.

    from player_frame import player_stand
    img, metres_per_px = player_stand("unarmed")

Reads assets/data/player.cfg: the legs' stand frame and a weapon set's stand frame, drawn one
over the other as the game draws them. The frame faces the top of the image; metres_per_px is
at life size (the game draws it CHAR_SCALE times SCALE larger, see player.cfg).
"""
import os
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def read_cfg():
    cfg = {"cols": 16, "sets": {}, "gaits": {}, "upper": {}}
    for line in open(os.path.join(ROOT, "assets", "data", "player.cfg"), encoding="utf-8"):
        t = line.split("#")[0].split()
        if not t:
            continue
        if t[0] == "FRAME":
            cfg["px"], cfg["m"] = int(t[1]), float(t[2])
        elif t[0] == "SCALE":
            cfg["scale"] = float(t[1])
        elif t[0] == "COLS":
            cfg["cols"] = int(t[1])
        elif t[0] == "LEGS":
            cfg["legs"] = t[1]
        elif t[0] == "GAIT":
            cfg["gaits"][t[1]] = int(t[2])
        elif t[0] == "SET":
            cfg["sets"][t[1]] = t[2]
        elif t[0] == "UPPER":
            cfg["upper"][(t[1], t[2])] = int(t[3])
    return cfg


def frame(cfg, file, index):
    atlas = Image.open(os.path.join(ROOT, "assets", "characters", "player", file)).convert("RGBA")
    px, cols = cfg["px"], cfg["cols"]
    x, y = (index % cols) * px, (index // cols) * px
    return atlas.crop((x, y, x + px, y + px))


def player_stand(weapon_set="unarmed"):
    cfg = read_cfg()
    img = frame(cfg, cfg["legs"], cfg["gaits"]["stand"])
    img.alpha_composite(frame(cfg, cfg["sets"][weapon_set], cfg["upper"][(weapon_set, "stand")]))
    return img, cfg["m"] / cfg["px"]
