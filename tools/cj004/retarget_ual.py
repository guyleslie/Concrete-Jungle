"""CJ-004 / CJ-029: copy Quaternius Universal Animation Library actions onto an MPFB civilian.

    blender -b <civilian.blend> --python tools/cj004/retarget_ual.py -- <UAL .glb> <out.blend> <request> ...

A request is <action>[=<frames>][~<a>,<c>][^<t>,<s>][&<k>[,<deg>]][@<degrees>][*<k>[,<knee>[,<hips>]]],
its parts in this order:
  =<frames>  comma-separated frames to copy (default: every whole frame of the action);
             =+<frames> copies every whole frame and these too: the library keys its actions
             every 0.8 frames, so a fast leg between two whole frames is off by up to 6 cm;
  ~<a>,<c>   remaps the forward swing of each thigh (degrees from straight down, forward
             positive) to a * angle + c by turning the whole leg about the hip;
  ^<t>,<s>   caps the knee lift: a thigh swinging forward beyond t degrees goes only s of the
             rest of the way (the library walk lifts the knee 50 degrees, a casual walk 25-30;
             a planted leg stays below t);
  &<k>,<deg> calms the upper body: the spine, neck, head and arms move k of their motion about
             their mean over the cycle, and the arms hang deg degrees closer to the body;
  @<degrees> leans the upper body (spine_01 and everything above it, arms included) back by
             that angle about the hips: the library's sprint leans about 45 degrees from the
             pelvis to the head;
  *<k>       scales every leg joint's motion to k of the way from standing straight; with a
             second value the knee, ankle and toes use it instead of k (the hip keeps k). The
             pelvis's sway, bob and turn about their mean over the cycle scale by the third
             value (default k; the upper body turns with it): legs that move less carry the
             hips less far, and a planted foot would otherwise slide sideways. A runner, whose
             feet touch the ground for a frame, keeps the library's hips (1).
Order of application: amplitude and calm, then swing and cap, then the arms in, then the lean.

Both rigs use Unreal-style bone names and face -Y, but the library's rest pose is a T-pose and
MPFB's an A-pose with bent elbows. The civilian's limbs are first posed so that every limb bone
points the way the library's bone points at rest ("matched rest"); the torso, upright in both,
keeps its own rest orientation. The constant rotation between the two bones there is the
offset; every frame, a civilian bone takes the library bone's world rotation times that offset,
so the twist of hips and shoulders carries over. Only the pelvis moves, by the library's pelvis
motion scaled to the civilian's hip height.

Retargeter computes the pose bones' local rotations along the hierarchy itself (no scene update
per bone), and can be imported (tune_gait.py). Run as a script, it keys the requested actions,
removes the imported library and saves the file.
"""
import bpy, math, sys
from mathutils import Matrix, Quaternion, Vector

TORSO = {"pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "head"}


def world(rig, name):
    return rig.matrix_world @ rig.pose.bones[name].matrix


def parse(request):
    """'<action>[=[+]frames][~a,c][^t,s][&k,deg][@deg][*k[,knee[,hips]]]' -> (action, frames or
    None for every whole frame, extra frames, mods dict)."""
    request, _, amplitude = request.partition("*")
    request, _, upright = request.partition("@")
    request, _, calm = request.partition("&")
    request, _, cap = request.partition("^")
    request, _, swing = request.partition("~")
    action, _, frames = request.partition("=")
    mods = {}
    if amplitude:
        k = [float(x) for x in amplitude.split(",")]
        mods["amp"] = (k[0], k[1] if len(k) > 1 else k[0], k[2] if len(k) > 2 else k[0])
    if swing:
        mods["swing"] = tuple(float(x) for x in swing.split(","))
    if cap:
        mods["cap"] = tuple(float(x) for x in cap.split(","))
    if calm:
        k = [float(x) for x in calm.split(",")]
        mods["calm"] = (k[0], k[1] if len(k) > 1 else 0.0)
    if upright:
        mods["upright"] = float(upright)
    extra = frames.startswith("+")
    listed = [float(x) for x in frames.lstrip("+").split(",")] if frames.lstrip("+") else []
    return action, (None if extra or not frames else listed), (listed if extra else []), mods


def toward(mean, q, k):
    """k of the way from the mean rotation to q (the shorter way round)."""
    return mean.slerp(q if q.dot(mean) >= 0 else -q, k)


class Retargeter:
    def __init__(self, target, glb):
        self.target = target
        before, actions_before = set(bpy.data.objects), set(bpy.data.actions)
        bpy.ops.import_scene.gltf(filepath=glb)
        self.imported = [o for o in bpy.data.objects if o not in before]
        self.source = next(o for o in self.imported if o.type == "ARMATURE")
        self.library = {a.name: a for a in bpy.data.actions if a not in actions_before}
        self.action, self.means = None, {}
        for o in self.imported:          # the library's mannequin stands where the civilian does
            o.hide_render = True
        tb, sb = target.data.bones, self.source.data.bones
        # Bones present in both rigs (the library capitalises 'Head' and has extra leaf bones).
        self.names = {}
        for b in sb:
            t = tb.get(b.name) or tb.get(b.name.lower())
            if t is not None and b.name != "root":
                self.names[t.name] = b.name
        # every target bone, parents first; its rest transform relative to its parent
        self.bones = sorted((b.name for b in tb), key=lambda n: len(tb[n].parent_recursive))
        self.rest_rel = {}
        for b in tb:
            self.rest_rel[b.name] = (b.parent.matrix_local.inverted() @ b.matrix_local) if b.parent else b.matrix_local.copy()
        self.legs = {n for n in self.names if n.startswith(("thigh_", "calf_", "foot_", "ball_"))}
        self.upper = {"spine_01"} | {b.name for b in tb["spine_01"].children_recursive}
        self.calm = {n for n in self.names if n in TORSO - {"pelvis"} or
                     n.startswith(("clavicle_", "upperarm_", "lowerarm_", "hand_"))}
        self._matched_rest()

    # ---- helpers ----
    def _clear(self, rig):
        rig.animation_data_create()
        rig.animation_data.action = None
        for pb in rig.pose.bones:
            pb.rotation_mode = "QUATERNION"
            pb.location = (0, 0, 0)
            pb.rotation_quaternion = Quaternion()
            pb.scale = (1, 1, 1)
        bpy.context.view_layer.update()

    def _matched_rest(self):
        """Aim every limb bone along the library bone at rest (one-off, with scene updates).
        The torso keeps its rest: its bones follow different conventions in the two rigs."""
        t, s = self.target, self.source
        self._clear(s)
        self._clear(t)
        for name in self.bones:
            if name not in self.names or name in TORSO:
                continue
            src_dir = (world(s, self.names[name]).to_3x3() @ Vector((0, 1, 0))).normalized()
            m = world(t, name)
            cur = (m.to_3x3() @ Vector((0, 1, 0))).normalized()
            rot = cur.rotation_difference(src_dir) @ m.to_quaternion()
            t.pose.bones[name].matrix = t.matrix_world.inverted() @ Matrix.LocRotScale(m.to_translation(), rot, (1, 1, 1))
            bpy.context.view_layer.update()
        self.offset = {n: world(s, self.names[n]).to_quaternion().inverted() @ world(t, n).to_quaternion() for n in self.names}
        self.straight = {n: t.pose.bones[n].rotation_quaternion.copy() for n in self.legs}
        self.pelvis_rest_src = world(s, self.names["pelvis"]).to_translation()
        self.pelvis_rest_tgt = world(t, "pelvis").to_translation()
        self.hip_ratio = self.pelvis_rest_tgt.z / self.pelvis_rest_src.z

    def set_action(self, name):
        a = self.library[name]
        self.source.animation_data.action = a
        if a.slots:
            self.source.animation_data.action_slot = a.slots[0]
        self.action = name
        return a

    def _means(self):
        """Mean local rotation (and the pelvis's mean location) over the current action's
        cycle, of the pelvis and the bones that *hips and & move about their mean (cached)."""
        if self.action not in self.means:
            first, last = (int(f) for f in self.library[self.action].frame_range)
            frames = range(first, max(first + 1, last))
            sums, locs = {}, Vector()
            for f in frames:
                rot, loc, _ = self.pose(f, {})
                for n in self.calm | {"pelvis"}:
                    q = rot[n]
                    if n in sums and q.dot(sums[n]) < 0:
                        q = -q
                    sums[n] = sums[n] + q if n in sums else q.copy()
                locs += loc["pelvis"]
            self.means[self.action] = ({n: q.normalized() for n, q in sums.items()}, locs / len(frames))
        return self.means[self.action]

    # ---- one frame ----
    def pose(self, frame, mods):
        """Local (basis) rotations, and the pelvis location, of every target bone for a frame of
        the source's current action, with the modifiers applied. Returns (rot, loc, armature
        matrices); nothing is written to the rig."""
        amp, calm, cap = mods.get("amp"), mods.get("calm"), mods.get("cap")
        hips = amp[2] if amp else 1.0
        means, mean_loc = self._means() if hips != 1.0 or calm else (None, None)
        f = math.floor(frame)
        bpy.context.scene.frame_set(int(f), subframe=frame - f)
        tw = self.target.matrix_world
        to_arm = tw.to_quaternion().inverted()
        swing = mods.get("swing")
        back = Quaternion((1, 0, 0), -math.radians(mods["upright"])) if "upright" in mods else None
        pure, final, rot, loc = {}, {}, {}, {}
        for n in self.bones:
            b = self.target.data.bones[n]
            p = b.parent.name if b.parent else None
            rr = self.rest_rel[n]
            base_pure = (pure[p] @ rr) if p else rr
            base_final = (final[p] @ rr) if p else rr
            if n not in self.names:                     # unmapped (the root): stays at rest
                pure[n], final[n], rot[n] = base_pure, base_final, Quaternion()
                continue
            src = world(self.source, self.names[n])
            want = to_arm @ (src.to_quaternion() @ self.offset[n])
            to_local = base_pure.to_quaternion().inverted()
            l = Vector()
            if n == "pelvis":                           # location in the bone's rest frame
                motion = (src.to_translation() - self.pelvis_rest_src) * self.hip_ratio
                l = base_pure.inverted() @ (tw.inverted() @ (self.pelvis_rest_tgt + motion))
            q = to_local @ want
            pure[n] = base_pure @ Matrix.LocRotScale(l, q, (1, 1, 1))
            if amp and n in self.legs:
                q = self.straight[n].slerp(q, amp[0] if n.startswith("thigh_") else amp[1])
            elif n == "pelvis" and hips != 1.0:         # the hips move about their mean as the legs carry them
                q, l = toward(means[n], q, hips), mean_loc + (l - mean_loc) * hips
            elif calm and n in self.calm:
                q = toward(means[n], q, calm[0])
            m = base_final @ Matrix.LocRotScale(l, q, (1, 1, 1))
            if (swing or cap) and n.startswith("thigh_"):
                knee = (m @ Matrix.Translation((0, b.length, 0))).to_translation()
                d = tw.to_3x3() @ (knee - m.to_translation())
                angle = math.degrees(math.atan2(-d.y, -d.z))
                goal = swing[0] * angle + swing[1] if swing else angle
                if cap and goal > cap[0]:
                    goal = cap[0] + (goal - cap[0]) * cap[1]
                turn = to_arm @ Quaternion((1, 0, 0), -math.radians(goal - angle)) @ tw.to_quaternion()
                q = base_final.to_quaternion().inverted() @ (turn @ m.to_quaternion())
                m = base_final @ Matrix.LocRotScale(l, q, (1, 1, 1))
            if calm and calm[1] and n.startswith("upperarm_"):    # the arm hangs closer to the body
                side = 1.0 if n.endswith("_l") else -1.0              # the left arm is on +X
                turn = to_arm @ Quaternion((0, 1, 0), side * math.radians(calm[1])) @ tw.to_quaternion()
                q = base_final.to_quaternion().inverted() @ (turn @ m.to_quaternion())
                m = base_final @ Matrix.LocRotScale(l, q, (1, 1, 1))
            if back is not None and n == "spine_01":     # the rest of the upper body follows
                turn = to_arm @ back @ tw.to_quaternion()
                q = base_final.to_quaternion().inverted() @ (turn @ m.to_quaternion())
                m = base_final @ Matrix.LocRotScale(l, q, (1, 1, 1))
            final[n], rot[n], loc[n] = m, q, l
        return rot, loc, final

    def apply(self, rot, loc):
        for n, q in rot.items():
            pb = self.target.pose.bones[n]
            pb.rotation_mode = "QUATERNION"
            pb.rotation_quaternion = q
            pb.location = loc.get(n, Vector())

    def retarget(self, request):
        """Key one request into a new action on the target; returns it."""
        action_name, frames, extra, mods = parse(request)
        src = self.set_action(action_name)
        first, last = (int(f) for f in src.frame_range)
        act = bpy.data.actions.new(action_name + "_civilian")
        act.use_fake_user = True
        self._clear(self.target)
        self.target.animation_data.action = act
        for f in sorted(set(frames or range(first, last + 1)) | set(extra)):
            rot, loc, _ = self.pose(f, mods)
            self.apply(rot, loc)
            for n in rot:
                pb = self.target.pose.bones[n]
                pb.keyframe_insert("rotation_quaternion", frame=f)
                if n == "pelvis":
                    pb.keyframe_insert("location", frame=f)
        print("RETARGETED", action_name, first, last)
        return act, action_name

    def remove_library(self):
        for o in self.imported:
            bpy.data.objects.remove(o, do_unlink=True)
        for a in self.library.values():
            bpy.data.actions.remove(a)


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    glb, out, wanted = args[0], args[1], args[2:]
    target = bpy.data.objects["civilian_rig"]
    r = Retargeter(target, glb)
    created = [r.retarget(req) for req in wanted]
    r.remove_library()
    for act, name in created:
        act.name = name
    target.animation_data.action = None
    bpy.ops.wm.save_as_mainfile(filepath=out)
    print("SAVED", out, sorted(a.name for a in bpy.data.actions))


if __name__ == "__main__":
    main()
