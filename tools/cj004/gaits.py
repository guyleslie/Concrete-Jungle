"""CJ-029: the civilian gaits, shared by make_civilians.py and tune_gait.py (no dependencies,
so Blender's Python can import it too).

Per gait (retarget_ual.py explains the modifiers):
  - action: the Universal Animation Library action;
  - amp, knee, hips: the leg motion kept, from standing straight, and the hips' motion about
    their mean (*amp,knee,hips; None keeps the library's). The walk keeps the library's legs:
    their stride, 1.3-1.4 m per cycle, is a real walker's at about 2 steps/s. The faster the
    gait, the further the limbs reach: the jog keeps 0.85 of the library's leg motion (the
    knee 0.7), the run all of it (the user's choices of 2026-10-09);
  - cap: the knee lift cap (^t,s): the library walk lifts the knee 50 degrees, a casual walk
    25-30; the planted leg stays below 26 degrees, and the swinging one goes 0.1 of the way
    beyond (29 degrees at most, with the tuned hip swing);
  - calm: the upper body's motion about its mean kept, and how many degrees closer to the body
    the arms hang (&k,deg): a walker's arms swing little and hang about as close as a
    standing person's (the library walk holds them 5-6 degrees further out);
  - frames: the scene frames that become atlas frames (the walk's are then moved so that they
    split the stride evenly, see measure_gait.py locked_frames);
  - cycle: the cycle length for the slip check: the walk uses its measured stride, the jog and
    run play by cadence in the game, so their check uses the cycle at a typical speed;
  - swing, upright: the hip-swing shifts (~1,c) and upper-body corrections (@deg) that
    tune_gait.py tries for each look; it keeps the one that shows as much leg ahead of the
    body as behind it from above (the user's playtest of 2026-10-09). The runner leans about
    23 degrees from the pelvis to the head (the library 45), so its pumping arms show;
  - spread_limit, side_limit: the largest distance from the front toe to the back foot, and how
    far the legs may show beyond the body ahead or behind (None: no limit), that tune_gait.py
    accepts; the walk's is a centimetre under make_civilians.py's check, because the tuning
    masks are smaller than the renders.
"""
GAITS = {
    "walk": {"action": "Walk_Formal_Loop", "amp": None, "knee": None, "hips": None, "cap": (26, 0.1),
             "calm": (0.5, 5), "frames": [2 * i for i in range(16)], "cycle": "auto",
             "swing": (-4, -2, 0, 2, 4), "upright": (4, 6, 8), "spread_limit": 0.95, "side_limit": 0.34},
    "jog": {"action": "Jog_Fwd_Loop", "amp": 0.85, "knee": 0.7, "hips": None, "cap": None, "calm": None,
            "frames": [round(2.75 * i, 2) for i in range(8)], "cycle": "2.26",                     # 3.0 m/s at 2.65 steps/s
            "swing": (10, 12, 14, 16, 18, 20, 22), "upright": (18,), "spread_limit": 1.40, "side_limit": None},
    "run": {"action": "Sprint_Loop", "amp": None, "knee": None, "hips": None, "cap": None, "calm": None,
            "frames": [2 * i for i in range(8)], "cycle": "3.42",                                  # 5.2 m/s at 3.04 steps/s
            "swing": (14, 16, 18, 20, 22, 24, 26, 28), "upright": (25,), "spread_limit": 1.50, "side_limit": None},
}


def request(gait, swing, upright):
    """The retarget request (retarget_ual.py) of a gait with a hip-swing shift and upright."""
    g = GAITS[gait]
    r = "%s~1,%g" % (g["action"], swing)
    if g["cap"]:
        r += "^%g,%g" % g["cap"]
    if g["calm"]:
        r += "&%g,%g" % g["calm"]
    r += "@%g" % upright
    if g["amp"] is not None:
        r += "*%g,%g" % (g["amp"], g["knee"] if g["knee"] is not None else g["amp"])
        if g["hips"] is not None:
            r += ",%g" % g["hips"]
    return r
