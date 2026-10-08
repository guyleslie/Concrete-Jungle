"""CJ-029: measure a civilian's gaits as the game plays them.

    blender -b <civilian-anim.blend> --python tools/cj004/measure_gait.py -- <out.json> <action>:<frames>:<cycle> ...

<frames> are the scene frames that become atlas frames (comma-separated, fractions allowed);
<cycle> is the distance in metres over which the game plays those frames once, or "auto" to
use the stride measured here. For each action, the JSON holds:
  - stride_m: how far the planted foot travels per cycle (the in-place loop's ground speed
    times its duration), measured on every keyed frame in steps of a quarter frame; only
    meaningful when the whole action is keyed;
  - thigh_forward_max_deg: the furthest the thighs swing forward from straight down;
  - slip_m: the largest movement of a planted foot over the atlas frames, with the sprite
    advanced by cycle / frames per frame, as the game does (0 = the foot stays put);
  - cadence_steps_per_m: steps per metre walked (2 per cycle), to turn into steps per second;
  - toe_ahead_max_m, foot_behind_max_m: the furthest a toe reaches ahead of the pelvis and a
    foot (toe or heel) behind it in the atlas frames; spread_max_m: the largest distance from
    the front toe to the back of the other foot in one frame (the "splits").
A foot counts as planted while its ball is within 1.5 cm of its lowest point. The rig faces -Y.
"""
import bpy, json, math, sys

args = sys.argv[sys.argv.index("--") + 1:]
out, jobs = args[0], args[1:]
scene = bpy.context.scene
rig = bpy.data.objects["civilian_rig"]
rig.rotation_euler = (0.0, 0.0, 0.0)
CONTACT = 0.015      # a foot is planted within 1.5 cm of its lowest point


def set_action(name):
    a = bpy.data.actions[name]
    rig.animation_data_create()
    rig.animation_data.action = a
    if a.slots:
        rig.animation_data.action_slot = a.slots[0]
    return a


def at(f):
    scene.frame_set(int(math.floor(f)), subframe=f - math.floor(f))
    w = rig.matrix_world
    pb = rig.pose.bones
    feet = {s: w @ pb["ball_" + s].head for s in ("l", "r")}
    thighs = {}
    for s in ("l", "r"):
        d = (w @ pb["calf_" + s].head) - (w @ pb["thigh_" + s].head)
        thighs[s] = math.degrees(math.atan2(-d.y, -d.z))
    return feet, thighs


result = {}
for job in jobs:
    name, frame_list, cycle = job.split(":")
    a = set_action(name)
    f0, f1 = (int(x) for x in a.frame_range)
    try:
        keyed = sorted({int(round(k.co.x)) for fc in a.fcurves for k in fc.keyframe_points})
    except (AttributeError, TypeError):
        keyed = []
    # ---- stride and thigh swing over the whole action ----
    samples = []
    t = float(f0)
    while t < f1:
        samples.append((t,) + at(t))
        t += 0.25
    zmin = {s: min(sm[1][s].z for sm in samples) for s in ("l", "r")}
    # Per foot, the longest stretch on the ground (the stance); shorter stretches near the
    # lowest point are a foot still landing or lifting, moving the other way.
    slopes = []
    for s in ("l", "r"):
        runs, run = [], []
        for t, feet, _ in samples + [(None, None, None)]:
            if feet is not None and feet[s].z < zmin[s] + CONTACT:
                run.append((t, feet[s].y))
            else:
                if run:
                    runs.append(run)
                run = []
        if runs and len(runs) > 1 and runs[0][0][0] == samples[0][0] and runs[-1][-1][0] == samples[-1][0]:
            last = runs.pop()                    # a stance across the loop's end: join its two parts
            span = f1 - f0
            runs[0] = [(t - span, y) for t, y in last] + runs[0]
        run = max(runs, key=len) if runs else []
        if len(run) >= 3:
            n = len(run); mt = sum(p[0] for p in run) / n; my = sum(p[1] for p in run) / n
            den = sum((p[0] - mt) ** 2 for p in run)
            if den > 0:
                slopes.append(sum((p[0] - mt) * (p[1] - my) for p in run) / den)
    speed = sum(slopes) / len(slopes) if slopes else 0.0        # metres per frame, backwards (+Y)
    stride = abs(speed) * (f1 - f0)
    thigh_max = max(max(sm[2].values()) for sm in samples)
    # ---- slip over the atlas frames, advanced like the game ----
    frames = [float(x) for x in frame_list.split(",")]
    cycle_m = stride if cycle == "auto" else float(cycle)
    n = len(frames)
    slip = 0.0
    toe_ahead = behind = spread = 0.0
    for f in frames:
        at(f)
        w = rig.matrix_world
        pel = w @ rig.pose.bones["pelvis"].head
        ys = []
        for s in ("l", "r"):
            toe = w @ rig.pose.bones["ball_" + s].tail
            heel = w @ rig.pose.bones["foot_" + s].head
            toe_ahead = max(toe_ahead, pel.y - toe.y)
            behind = max(behind, toe.y - pel.y, heel.y - pel.y)
            ys.append((toe.y, max(toe.y, heel.y)))
        spread = max(spread, max(ys[0][1], ys[1][1]) - min(ys[0][0], ys[1][0]))
    for s in ("l", "r"):
        world = []
        for k, f in enumerate(frames):
            feet, _ = at(f)
            planted = feet[s].z < zmin[s] + CONTACT
            # the sprite moves forward (-Y) by cycle / n per frame
            world.append((planted, feet[s].y - k * cycle_m / n, feet[s].x))
        # the last frame is followed by frame 0 one cycle on
        world.append((world[0][0], world[0][1] - cycle_m, world[0][2]))
        run = []
        for planted, y, x in world + [(False, 0, 0)]:
            if planted:
                run.append((y, x))
            else:
                if len(run) >= 2:
                    slip = max(slip, max(p[0] for p in run) - min(p[0] for p in run),
                               max(p[1] for p in run) - min(p[1] for p in run))
                run = []
    result[name] = {"stride_m": stride, "thigh_forward_max_deg": thigh_max, "slip_m": slip,
                    "cycle_m": cycle_m, "frames": n, "cadence_steps_per_m": 2.0 / cycle_m if cycle_m else 0,
                    "keyed_frames": len(keyed), "toe_ahead_max_m": toe_ahead, "foot_behind_max_m": behind,
                    "spread_max_m": spread}
    print("GAIT %-17s stride %.3f m, cycle %.3f m, thigh forward %.1f deg, slip %.3f m, toe ahead %.2f m, behind %.2f m, spread %.2f m" %
          (name, stride, cycle_m, thigh_max, slip, toe_ahead, behind, spread))

json.dump(result, open(out, "w"), indent=1)
