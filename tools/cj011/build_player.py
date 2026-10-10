"""CJ-011: dress a civilian build as the player and add the weapons.

    blender -b <player-base.blend> --python tools/cj011/build_player.py -- <gun OBJ dir> <out.blend>

The base comes from tools/cj004/build_civilian.py with tools/cj011/look-player.json. This
script adds, modelled here because no CC0 MakeHuman asset exists:
  - a dark baseball cap: a crown fitted over the evaluated head and hair, with seams, a button
    and a curved peak, fixed to the head bone;
  - a light camouflage backpack: a rounded fabric body, a flap with a buckle strap and two
    shoulder straps, fixed to spine_03;
and the weapons of GUNS, imported from the Ultimate Gun Pack (Quaternius, CC0) as OBJ. A
weapon object's origin is its grip (where the right palm holds it), its X axis runs along the
bore towards the muzzle, Z is the weapon's top and 1 unit is 1 m; custom properties give the
support hand point ("fore"), the butt ("butt") and the muzzle ("muzzle") in that frame, and the
grip axis ("grip_down", from the top of the grip to its bottom). The weapons are hidden; the
render script places them per pose.
"""
import bpy, bmesh, math, os, sys
from mathutils import Matrix, Vector

args = sys.argv[sys.argv.index("--") + 1:]
gun_dir, out = args[0], os.path.abspath(args[1])

rig = bpy.data.objects["civilian_rig"]
body = bpy.data.objects["civilian"]

# Weapon data, read off side views of the models (tools/cj011/gun_grid.py), in model units.
# length_m: the real overall length (a 1911-style pistol 0.216 m, an M4 carbine with the stock
# extended 0.84 m).
GUNS = {
    "pistol": {"model": "Pistol_1", "length_m": 0.216, "bore_z": 0.58,
               "grip": (-0.03, 0.03), "grip_top": (0.07, 0.35), "grip_bottom": (-0.15, -0.33),
               "fore": None, "butt": None},
    "rifle": {"model": "AssaultRifle2_1", "length_m": 0.84, "bore_z": 0.66,
              "grip": (-0.22, 0.12), "grip_top": (-0.10, 0.45), "grip_bottom": (-0.35, -0.20),
              "fore": (1.75, 0.635), "butt": (-1.56, 0.55)},
}


def mat(name, rgb, rough=0.6, metal=0.0, sheen=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*rgb, 1)
    p.inputs["Roughness"].default_value = rough
    p.inputs["Metallic"].default_value = metal
    if sheen and "Sheen Weight" in p.inputs:
        p.inputs["Sheen Weight"].default_value = sheen
    return m


def parent_to_bone(obj, bone):
    """Keep obj where it is and make it follow a bone of the rig."""
    world = obj.matrix_world.copy()
    obj.parent = rig
    obj.parent_type = "BONE"
    obj.parent_bone = bone
    bpy.context.view_layer.update()
    obj.matrix_world = world


def evaluated_points(objs, test=None):
    deps = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o in objs:
        ev = o.evaluated_get(deps)
        me = ev.to_mesh()
        for v in me.vertices:
            w = o.matrix_world @ v.co
            if test is None or test(w):
                pts.append(w)
        ev.to_mesh_clear()
    return pts


bpy.context.view_layer.update()
head_bone = rig.data.bones["head"]
head_z = head_bone.head_local.z

# ---- cap -------------------------------------------------------------------------------
dressed = [o for o in bpy.data.objects if o.type == "MESH" and o.name.startswith("civilian")]
top = evaluated_points(dressed, lambda w: w.z > head_z - 0.02)
top_z = max(p.z for p in top)
band = [p for p in top if top_z - 0.11 < p.z < top_z - 0.06]   # around the forehead and the back of the head
cx = (min(p.x for p in band) + max(p.x for p in band)) / 2
cy = (min(p.y for p in band) + max(p.y for p in band)) / 2
rx = (max(p.x for p in band) - min(p.x for p in band)) / 2 + 0.008
ry = (max(p.y for p in band) - min(p.y for p in band)) / 2 + 0.008
brim_z = top_z - 0.085                                    # the crown's lower edge, just above the ears
rz = top_z - brim_z + 0.012

bm = bmesh.new()
SEG, RINGS = 48, 12
rings = []
for i in range(RINGS + 1):                                 # i = 0 at the brim, RINGS at the top
    t = i / RINGS * (math.pi / 2)
    ring = []
    for j in range(SEG):
        a = 2 * math.pi * j / SEG
        s = math.cos(t)
        # the crown's front panel is a little taller and flatter, as on a structured cap
        front = max(0.0, -math.sin(a))
        z = brim_z + rz * math.sin(t) * (1.0 + 0.06 * front)
        ring.append(bm.verts.new((cx + rx * s * math.cos(a), cy + ry * s * math.sin(a) * (1.0 - 0.04 * front), z)))
    rings.append(ring)
for i in range(RINGS):
    for j in range(SEG):
        k = (j + 1) % SEG
        if i == RINGS - 1:
            continue
        bm.faces.new((rings[i][j], rings[i][k], rings[i + 1][k], rings[i + 1][j]))
cap_top = bm.verts.new((cx, cy, brim_z + rz * 1.02))
for j in range(SEG):
    bm.faces.new((rings[RINGS - 1][j], rings[RINGS - 1][(j + 1) % SEG], cap_top))
crown_mesh = bpy.data.meshes.new("player_cap")
bm.to_mesh(crown_mesh)
bm.free()
crown = bpy.data.objects.new("player_cap", crown_mesh)
bpy.context.scene.collection.objects.link(crown)
crown.modifiers.new("thick", "SOLIDIFY").thickness = 0.004
crown.modifiers.new("smooth", "SUBSURF").levels = 1
for p in crown_mesh.polygons:
    p.use_smooth = True

# Peak: a curved plate in front of the brim, -Y is the front.
bm = bmesh.new()
PW, PL = 12, 5
grid = []
for i in range(PL + 1):
    row = []
    for j in range(PW + 1):
        u = j / PW * 2 - 1                                  # -1 .. 1 across
        v = i / PL                                          # 0 at the brim, 1 at the tip
        a = math.pi * (1.5 + 0.36 * u)                      # around the front of the crown
        edge = Vector((cx + rx * math.cos(a), cy + ry * math.sin(a), brim_z))
        reach = 0.072 * v * (1 - 0.55 * u * u)              # rounded tip
        row.append(bm.verts.new((edge.x + 0.25 * reach * math.cos(a) * 0.3, edge.y - reach,
                                 edge.z - 0.012 * v - 0.010 * u * u * v)))
    grid.append(row)
for i in range(PL):
    for j in range(PW):
        bm.faces.new((grid[i][j], grid[i][j + 1], grid[i + 1][j + 1], grid[i + 1][j]))
peak_mesh = bpy.data.meshes.new("player_cap_peak")
bm.to_mesh(peak_mesh)
bm.free()
peak = bpy.data.objects.new("player_cap_peak", peak_mesh)
bpy.context.scene.collection.objects.link(peak)
peak.modifiers.new("thick", "SOLIDIFY").thickness = 0.006
peak.modifiers.new("smooth", "SUBSURF").levels = 1

# Seams and the button on top.
seam_objs = []
for j in range(6):
    a = 2 * math.pi * j / 6 + math.pi / 2
    pts = []
    for i in range(1, 11):
        t = i / 10 * (math.pi / 2) * 0.97
        s = math.cos(t)
        pts.append((cx + (rx + 0.002) * s * math.cos(a), cy + (ry + 0.002) * s * math.sin(a), brim_z + (rz + 0.002) * math.sin(t)))
    cu = bpy.data.curves.new("seam", "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = 0.0010
    sp = cu.splines.new("POLY")
    sp.points.add(len(pts) - 1)
    for p, c in zip(sp.points, pts):
        p.co = (*c, 1)
    o = bpy.data.objects.new("player_cap_seam", cu)
    bpy.context.scene.collection.objects.link(o)
    seam_objs.append(o)
bpy.ops.mesh.primitive_uv_sphere_add(radius=0.009, location=(cx, cy, brim_z + rz * 1.02 + 0.003))
button = bpy.context.object
button.scale.z = 0.5
button.name = "player_cap_button"

cap_fabric = mat("cap_fabric", (0.012, 0.013, 0.016), rough=0.8, sheen=0.1)
cap_seam = mat("cap_seam", (0.009, 0.010, 0.012), rough=0.85)
for o in (crown, peak, button):
    o.data.materials.append(cap_fabric)
for o in seam_objs:
    o.data.materials.append(cap_seam)
for o in (crown, peak, button, *seam_objs):
    parent_to_bone(o, "head")

# ---- backpack ----------------------------------------------------------------------------
torso = evaluated_points([o for o in dressed if "casualsuit" in o.name], lambda w: 1.15 < w.z < 1.45 and abs(w.x) < 0.14)
back_y = max(p.y for p in torso)                          # the back of the jacket (+Y is behind)
chest_y = min(p.y for p in torso)
PACK_W, PACK_D, PACK_H = 0.30, 0.10, 0.42
PACK_TOP_R = 0.11                       # the rounded top corners, seen from behind
pack_bottom = 1.03
pack_back = back_y + 0.004              # the back panel lies on the jacket


def sgnpow(v, e):
    return math.copysign(abs(v) ** e, v)


def pack_section(z, inset=0.0):
    """Width, depth and the front's sag of the pack body at height z above its bottom."""
    w = PACK_W + 0.012 * (1 - z / PACK_H)                 # a little wider at the bottom
    if z > PACK_H - PACK_TOP_R:                            # rounded top corners
        t = (z - (PACK_H - PACK_TOP_R)) / PACK_TOP_R
        w -= 2 * PACK_TOP_R * (1 - math.sqrt(max(0.0, 1 - t * t)))
    if z < 0.025:                                          # a soft rounded bottom edge
        t = 1 - z / 0.025
        w -= 2 * 0.025 * (1 - math.sqrt(max(0.0, 1 - t * t)))
    d = PACK_D * (0.55 + 0.45 * math.sin(math.pi * min(1.0, 0.15 + z / PACK_H) ** 0.8))   # fuller low, thinner at the top
    return max(w - 2 * inset, 0.01), max(d - inset, 0.005)


def soft_body(name, height, section, n=4.0, rings=28, seg=40, front_only=False, y0=None, z0=0.0):
    """A soft fabric body lofted from rounded-rectangle sections: the back flat against the
    carrier (y0), the front rounded. front_only: an open shell for a pocket or a pad."""
    bm = bmesh.new()
    rows = []
    for i in range(rings + 1):
        z = height * i / rings
        w, d = section(z)
        row = []
        for j in range(seg):
            t = 2 * math.pi * j / seg
            x = w / 2 * sgnpow(math.cos(t), 2 / n)
            s_ = sgnpow(math.sin(t), 2 / n)               # -1 at the back, 1 at the front
            # the back half is nearly flat against the carrier, the front rounded
            y = 0.12 * d * (1 + s_) if s_ < 0 else 0.12 * d + 0.88 * d * s_
            row.append(bm.verts.new((x, y0 + y, z0 + z)))
        rows.append(row)
    for r0, r1 in zip(rows, rows[1:]):
        for j in range(seg):
            k = (j + 1) % seg
            bm.faces.new((r0[j], r0[k], r1[k], r1[j]))
    for row, rev in ((rows[0], True), (rows[-1], False)):
        c = bm.verts.new(sum((v.co for v in row), Vector()) / len(row))
        for j in range(seg):
            f = (row[j], row[(j + 1) % seg], c)
            bm.faces.new(f[::-1] if rev else f)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    for poly in me.polygons:
        poly.use_smooth = True
    o.modifiers.new("smooth", "SUBSURF").levels = 2
    return o


def fabric(o, strength=0.005, scale=0.035):
    """Wrinkles: a soft cloud displacement along the normals, and a slight sag."""
    tex = bpy.data.textures.get("fabric_wrinkles") or bpy.data.textures.new("fabric_wrinkles", "CLOUDS")
    tex.noise_scale = scale
    tex.noise_depth = 2
    dis = o.modifiers.new("wrinkles", "DISPLACE")
    dis.texture = tex
    dis.texture_coords = "GLOBAL"
    dis.strength = strength
    dis.mid_level = 0.5


pack = soft_body("player_pack", PACK_H, pack_section, y0=pack_back, z0=pack_bottom)
fabric(pack)


def pocket_section(z):
    h = 0.15
    w = 0.21 - 0.03 * (z / h) ** 2
    if z > h - 0.03:
        t = (z - (h - 0.03)) / 0.03
        w -= 2 * 0.03 * (1 - math.sqrt(max(0.0, 1 - t * t)))
    return max(w, 0.01), 0.028 * math.sin(math.pi * min(1.0, 0.1 + z / h)) + 0.004


def front_at(z):
    """The pack's front surface y at height z (its centre line)."""
    w, d = pack_section(z - pack_bottom)
    return pack_back + d


pocket = soft_body("player_pack_pocket", 0.15, pocket_section, y0=front_at(pack_bottom + 0.11) - 0.012, z0=pack_bottom + 0.035)
fabric(pocket, 0.003, 0.02)


def curve_obj(name, pts, depth, cyclic=False):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = depth
    sp = cu.splines.new("NURBS")
    sp.points.add(len(pts) - 1)
    for q, c in zip(sp.points, pts):
        q.co = (*c, 1)
    sp.use_endpoint_u = not cyclic
    sp.use_cyclic_u = cyclic
    sp.order_u = 3
    o = bpy.data.objects.new(name, cu)
    bpy.context.scene.collection.objects.link(o)
    return o


# piping round the front panel and the zip round the top: on the pack's surface, just proud of it
def outline(inset, z_from, z_to, n=40, out=0.002):
    pts = []
    for i in range(n + 1):                                 # up the left side, over the top, down the right
        t = i / n
        if t < 1 / 3:
            z, side = z_from + (z_to - z_from) * (3 * t), -1
        elif t < 2 / 3:
            z, side = z_to, None
        else:
            z, side = z_from + (z_to - z_from) * (3 * (1 - t)), 1
        if side is None:
            u = (t - 1 / 3) * 3 * 2 - 1
            w, d = pack_section(z_to - pack_bottom)
            x = u * (w / 2 - inset)
            zz = z_to - 0.035 * (abs(u) ** 3)
            w2, d2 = pack_section(zz - pack_bottom)
            pts.append((x, pack_back + d2 * (0.6 + 0.4 * (1 - abs(u) ** 4)) + out, zz))
        else:
            w, d = pack_section(z - pack_bottom)
            pts.append((side * (w / 2 - inset), pack_back + d * 0.62 + out, z))
    return pts


zip_pts = outline(0.012, pack_bottom + 0.20, pack_bottom + PACK_H - 0.025)
zip_top = curve_obj("player_pack_zip", zip_pts, 0.0025)
pocket_zip = curve_obj("player_pack_pocket_zip", [(-0.085, front_at(pack_bottom + 0.16) + 0.018, pack_bottom + 0.165),
                                                   (0.0, front_at(pack_bottom + 0.17) + 0.022, pack_bottom + 0.172),
                                                   (0.085, front_at(pack_bottom + 0.16) + 0.018, pack_bottom + 0.165)], 0.002)
pullers = []
top_pull = min(zip_pts, key=lambda q: abs(q[0] - 0.04) - 10 * q[2])     # on the zip, over the top
for x, y, z in (top_pull, (0.06, front_at(pack_bottom + 0.165) + 0.02, pack_bottom + 0.165)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=(x, y + 0.003, z - 0.012))
    o = bpy.context.object
    o.name = "player_pack_puller"
    o.scale = (0.008, 0.004, 0.024)
    pullers.append(o)
# a grab handle on top
handle = curve_obj("player_pack_handle", [(-0.035, pack_back + 0.02, pack_bottom + PACK_H - 0.004),
                                          (0.0, pack_back + 0.03, pack_bottom + PACK_H + 0.02),
                                          (0.035, pack_back + 0.02, pack_bottom + PACK_H - 0.004)], 0.006)

# Shoulder straps: flat padded webbing laid on the jacket, from the top of the pack over the
# shoulder and down the chest to a buckle; from the buckle a narrow lower strap runs under the
# armpit, round the side of the torso, to the pack's bottom corner. At each strap's x, the
# jacket's cross-section (y, z) is swept from the back over the top to the front; the strap
# follows its outer contour, its width across and its thickness along the outward normal.
jacket = evaluated_points([o for o in dressed if "casualsuit" in o.name], lambda w: w.z > 0.95)
STRAP_W, STRAP_T, STRAP_GAP = 0.045, 0.006, 0.004
LOWER_W, LOWER_T = 0.025, 0.003
BUCKLE_Z = 1.21


def contour(x):
    """Outer contour of the jacket's section at x: (y, z) points from the back to the front."""
    sec = [(p.y, p.z) for p in jacket if abs(p.x - x) < 0.012 and p.z > 1.1]
    cy, cz = 0.0, 1.30
    pts = []
    for i in range(41):                                   # from the back (+y), over the top, to the front
        t = math.radians(-35 + i * (250 / 40))            # 0 = straight back, 90 = up, 180 = front
        d = (math.cos(t), math.sin(t))
        best = None
        for y, z in sec:
            vy, vz = y - cy, z - cz
            along = vy * d[0] + vz * d[1]
            off = abs(vy * d[1] - vz * d[0])
            if along > 0 and off < 0.012 and (best is None or along > best):
                best = along
        if best is not None:
            pts.append(Vector((cy + d[0] * best, cz + d[1] * best)))
    return pts


TORSO_CY = (back_y + chest_y) / 2


def around(z, phi):
    """The jacket's surface point at height z in the horizontal direction phi from the torso's
    centre (0 = the character's left, +X; 90 degrees = behind, +Y), and its outward normal."""
    d = Vector((math.cos(phi), math.sin(phi), 0.0))
    for tol in (0.012, 0.02, 0.035, 0.06):
        best = None
        for q in jacket:
            if abs(q.z - z) > tol or abs(q.x) > 0.21:      # the torso, not the sleeves
                continue
            v = Vector((q.x, q.y - TORSO_CY, 0.0))
            along = v.dot(d)
            if along > 0 and abs(v.cross(d).z) < tol and (best is None or along > best):
                best = along
        if best is not None:
            return Vector((d.x * best, TORSO_CY + d.y * best, z)), d
    raise ValueError("no jacket surface at z %.3f, phi %.1f" % (z, math.degrees(phi)))


def ribbon(name, path, normals, width=STRAP_W, thick=STRAP_T):
    """A flat strap along path (3D points) with outward normals; width across, thickness out."""
    bm = bmesh.new()
    rows = []
    for i, (p, n) in enumerate(zip(path, normals)):
        tng = (path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)]).normalized()
        across = tng.cross(n).normalized()
        n = across.cross(tng).normalized()
        base = p + n * STRAP_GAP
        rows.append([bm.verts.new(base + across * s_ * width / 2 + n * t_ * thick)
                     for s_, t_ in ((-1, 0), (1, 0), (1, 1), (-1, 1))])
    for r0, r1 in zip(rows, rows[1:]):
        for k in range(4):
            bm.faces.new((r0[k], r0[(k + 1) % 4], r1[(k + 1) % 4], r1[k]))
    bm.faces.new(rows[0][::-1])
    bm.faces.new(rows[-1])
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    bev = o.modifiers.new("bevel", "BEVEL")
    bev.width = min(0.0015, thick / 3)
    bev.segments = 2
    return o


def smooth(points, passes=3):
    for _ in range(passes):
        points = [points[0]] + [(points[i - 1] + 2 * points[i] + points[i + 1]) / 4 for i in range(1, len(points) - 1)] + [points[-1]]
    return points


straps, buckles = [], []
for sx in (-1.0, 1.0):
    x = sx * 0.12
    # (y, z) points: drop what the pack covers; end at the buckle on the chest
    c = [q for q in contour(x) if not (q.x > 0.08 and q.y < pack_bottom + PACK_H - 0.03) and q.y > BUCKLE_Z]
    sec2d = smooth(c)
    path = [Vector((x * (1.0 + 0.30 * max(0.0, -q.x) / 0.2), q.x, q.y)) for q in sec2d]   # widen a little down the chest
    normals = []
    for i in range(len(sec2d)):
        t = sec2d[min(i + 1, len(sec2d) - 1)] - sec2d[max(i - 1, 0)]
        n2 = Vector((t.y, -t.x)).normalized()               # outward for a sweep from the back over the top
        normals.append(Vector((0.0, n2.x, n2.y)))
    start = Vector((x * 0.9, pack_back + 0.02, pack_bottom + PACK_H - 0.035))
    straps.append(ribbon("player_pack_strap", [start] + path, [Vector((0, 0.3, 1)).normalized()] + normals))
    end, end_n = path[-1], normals[-1]
    # the buckle at the strap's end, and the lower strap from it round the side to the pack
    bpy.ops.mesh.primitive_cube_add(size=1, location=end + end_n * (STRAP_GAP + 0.006) + Vector((0, 0, -0.01)))
    bk = bpy.context.object
    bk.name = "player_pack_strap_buckle"
    bk.scale = (0.032, 0.006, 0.022)
    buckles.append(bk)
    phi0 = math.atan2(end.y - TORSO_CY, end.x)                         # where the chest strap ends, round the torso
    phi1 = math.atan2(pack_back - 0.01 - TORSO_CY, sx * 0.13)            # the pack's bottom corner, behind
    lower, lower_n = [], []
    steps = 16
    for i in range(steps + 1):
        t = i / steps
        # the shorter way round the side
        dphi = (phi1 - phi0 + math.pi) % (2 * math.pi) - math.pi
        z = BUCKLE_Z - 0.02 + (pack_bottom + 0.05 - (BUCKLE_Z - 0.02)) * t
        q, n = around(z, phi0 + dphi * t)
        lower.append(q)
        lower_n.append(n)
    lower[0] = end + Vector((0, 0, -0.02))
    lower = smooth(lower, 2)
    corner = Vector((sx * 0.13, pack_back + 0.012, pack_bottom + 0.04))
    straps.append(ribbon("player_pack_lower_strap", lower + [corner], lower_n + [Vector((sx, 0.3, 0)).normalized()],
                         LOWER_W, LOWER_T))

# camouflage: khaki, tan, olive, sand and brown in irregular patches, on a matt
# woven fabric (a fine bump of crossing threads)
camo = bpy.data.materials.new("pack_camo")
camo.use_nodes = True
nt = camo.node_tree
bsdf = nt.nodes["Principled BSDF"]
bsdf.inputs["Roughness"].default_value = 0.92
if "Sheen Weight" in bsdf.inputs:
    bsdf.inputs["Sheen Weight"].default_value = 0.15
weave_u = nt.nodes.new("ShaderNodeTexWave")
weave_u.inputs["Scale"].default_value = 260.0
weave_v = nt.nodes.new("ShaderNodeTexWave")
weave_v.inputs["Scale"].default_value = 260.0
weave_v.bands_direction = "Y"
weave_mix = nt.nodes.new("ShaderNodeMath")
weave_mix.operation = "MULTIPLY"
bump = nt.nodes.new("ShaderNodeBump")
bump.inputs["Strength"].default_value = 0.25
bump.inputs["Distance"].default_value = 0.0006
nt.links.new(weave_u.outputs["Fac"], weave_mix.inputs[0])
nt.links.new(weave_v.outputs["Fac"], weave_mix.inputs[1])
nt.links.new(weave_mix.outputs["Value"], bump.inputs["Height"])
nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
coord = nt.nodes.new("ShaderNodeTexCoord")
noise = nt.nodes.new("ShaderNodeTexNoise")
noise.inputs["Scale"].default_value = 14.0
noise.inputs["Detail"].default_value = 3.0
noise.inputs["Roughness"].default_value = 0.55
ramp = nt.nodes.new("ShaderNodeValToRGB")
cr = ramp.color_ramp
cr.interpolation = "CONSTANT"
cols = [(0.0, (0.13, 0.11, 0.06)), (0.38, (0.20, 0.16, 0.09)), (0.50, (0.05, 0.06, 0.025)),
        (0.60, (0.28, 0.24, 0.15)), (0.70, (0.07, 0.045, 0.025))]   # linear: khaki, tan, olive, sand, brown
cr.elements[0].position, cr.elements[0].color = cols[0][0], (*cols[0][1], 1)
cr.elements[1].position, cr.elements[1].color = cols[1][0], (*cols[1][1], 1)
for pos, c in cols[2:]:
    e = cr.elements.new(pos)
    e.color = (*c, 1)
nt.links.new(coord.outputs["Object"], noise.inputs["Vector"])
nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
nt.links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
webbing = mat("pack_webbing", (0.10, 0.10, 0.075), rough=0.7)
metal = mat("pack_buckle", (0.04, 0.04, 0.04), rough=0.35)
zip_mat = mat("pack_zip", (0.05, 0.05, 0.04), rough=0.5)
for o in (pack, pocket):
    o.data.materials.append(camo)
for o in (handle, *straps):
    o.data.materials.append(webbing)
for o in (zip_top, pocket_zip):
    o.data.materials.append(zip_mat)
for o in (*buckles, *pullers):
    o.data.materials.append(metal)
for o in (pack, pocket, zip_top, pocket_zip, handle, *pullers, *buckles, *straps):
    parent_to_bone(o, "spine_03")

# ---- weapons -----------------------------------------------------------------------------
for key, g in GUNS.items():
    before = set(bpy.data.objects)
    bpy.ops.wm.obj_import(filepath=os.path.join(gun_dir, g["model"] + ".obj"))
    parts = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    bpy.ops.object.select_all(action="DESELECT")
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    if len(parts) > 1:
        bpy.ops.object.join()
    gun = bpy.context.view_layer.objects.active
    gun.name = "gun_" + key
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    xs = [v.co.x for v in gun.data.vertices]
    k = g["length_m"] / (max(xs) - min(xs))
    gx, gz = g["grip"]
    for v in gun.data.vertices:
        v.co = Vector(((v.co.x - gx) * k, v.co.y * k, (v.co.z - gz) * k))
    gun.data.update()

    def local(p):
        return [(p[0] - gx) * k, 0.0, (p[1] - gz) * k]

    gun["muzzle"] = [(max(xs) - gx) * k, 0.0, (g["bore_z"] - gz) * k]
    gun["fore"] = local(g["fore"]) if g["fore"] else []
    gun["butt"] = local(g["butt"]) if g["butt"] else []
    d = Vector(local(g["grip_bottom"])) - Vector(local(g["grip_top"]))
    gun["grip_down"] = list(d.normalized())
    for slot in gun.material_slots:                       # glossier finish for the close camera
        m = slot.material
        if m and m.use_nodes:
            p = m.node_tree.nodes.get("Principled BSDF")
            if p:
                wood = "Wood" in m.name
                p.inputs["Roughness"].default_value = 0.5 if wood else 0.3
                p.inputs["Metallic"].default_value = 0.0 if wood else 0.6
                if not wood:
                    c = p.inputs["Base Color"].default_value
                    p.inputs["Base Color"].default_value = (c[0] * 2.2, c[1] * 2.2, c[2] * 2.2, 1)
    for p in gun.data.polygons:
        p.use_smooth = False
    gun.hide_render = True
    print("GUN", key, "scale %.4f" % k, "muzzle", [round(v, 3) for v in gun["muzzle"]],
          "fore", [round(v, 3) for v in gun["fore"]], "butt", [round(v, 3) for v in gun["butt"]])

print("CAP top %.3f brim %.3f radii %.3f %.3f; PACK back %.3f chest %.3f" % (top_z, brim_z, rx, ry, back_y, chest_y))
bpy.ops.wm.save_as_mainfile(filepath=out)
print("SAVED", out)
