"""CJ-004 / CJ-029: read assets/data/civilians.cfg for the preview tools.

read() returns a dict: frame_px, frame_m, scale (standing civilians drawn larger), anim
{name: (first frame, frames)}, gait {name: (from m/s, steps/s, to m/s, steps/s)}, walk (the
default walk cycle) and looks {id: walk cycle m}.
"""
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CFG = os.path.join(ROOT, "assets", "data", "civilians.cfg")


def read(path=CFG):
    c = {"frame_px": 144, "frame_m": 2.1, "scale": 1.0, "anim": {}, "gait": {}, "walk": 1.3, "looks": {}}
    for line in open(path, encoding="utf-8"):
        f = line.split()
        if not f or f[0].startswith("#"):
            continue
        if f[0] == "FRAME":
            c["frame_px"], c["frame_m"] = int(f[1]), float(f[2])
        elif f[0] == "SCALE":
            c["scale"] = float(f[1])
        elif f[0] == "ANIM":
            c["anim"][f[1]] = (int(f[2]), int(f[3]))
        elif f[0] == "GAIT":
            c["gait"][f[1]] = tuple(float(x) for x in f[2:6])
        elif f[0] == "WALK":
            c["walk"] = float(f[1])
        elif f[0] == "CIVILIAN":
            c["looks"][os.path.splitext(f[1])[0]] = float(f[2]) if len(f) > 2 else None
    for lid in c["looks"]:
        c["looks"][lid] = c["looks"][lid] or c["walk"]
    return c


def cadence(gait, speed):
    """Steps per second of a jog or run at a speed: linear between its two points, clamped."""
    v0, s0, v1, s1 = gait
    t = min(1.0, max(0.0, (speed - v0) / (v1 - v0))) if v1 != v0 else 0.0
    return s0 + (s1 - s0) * t
