"""CJ-004: build one civilian from MPFB (MakeHuman) CC0 assets and save it as a .blend.

Run with Blender 5.2 and the MPFB extension, the MakeHuman CC0 asset packs loaded:
    blender -b --factory-startup --python tools/cj004/build_civilian.py -- <look.json> <out.blend>

The look file names the macro details (MakeHuman 0..1 scales) and the assets, for example
    {"macro": {"gender": 1.0, "age": 0.58, ...}, "race": {"caucasian": 1.0},
     "skin": "middleage_caucasian_male", "clothes": ["male_casualsuit05", "shoes03"], "hair": "short02",
     "tint": {"male_casualsuit05": {"hue": 0.0, "sat": 1.0, "val": 1.0, "mul": [0.55, 0.68, 1.0]}}}
"hair" may be null. "height_m" sets the body height: the MakeHuman height slider is searched
until the measured body matches (race and age shift MakeHuman's heights a lot). A tint recolours an asset: a hue shift in turns, saturation and value
multipliers, and an RGB multiplier that also colours grey garments.
The character stands at the origin facing -Y (MakeHuman's front), 1 Blender unit = 1 m.
"""
import bpy, json, os, sys
from mathutils import Color

import addon_utils
addon_utils.enable("bl_ext.user_default.mpfb", default_set=True)
from bl_ext.user_default.mpfb.services import HumanService, TargetService, LocationService
from bl_ext.user_default.mpfb.entities.objectproperties import HumanObjectProperties

args = sys.argv[sys.argv.index("--") + 1:]
look = json.load(open(args[0], encoding="utf-8"))
out = os.path.abspath(args[1])
data = LocationService.get_user_data()

def asset(kind, name, ext):
    path = os.path.join(data, kind, name, name + ext)
    if not os.path.exists(path):
        raise FileNotFoundError(path)
    return path

for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)

macro = TargetService.get_default_macro_info_dict()
macro.update(look.get("macro", {}))
race = {"asian": 0.0, "caucasian": 0.0, "african": 0.0}
race.update(look.get("race", {"caucasian": 1.0}))
macro["race"] = race

base = HumanService.create_human(macro_detail_dict=macro)
base.name = "civilian"


def body_height():
    """Height of the evaluated body (macro targets are shape keys; helpers are masked)."""
    bpy.context.view_layer.update()
    ev = base.evaluated_get(bpy.context.evaluated_depsgraph_get())
    zs = [(base.matrix_world @ v.co).z for v in ev.to_mesh().vertices]
    ev.to_mesh_clear()
    return max(zs) - min(zs)


if look.get("height_m"):
    lo, hi = 0.0, 1.0
    for _ in range(12):
        mid = (lo + hi) / 2
        HumanObjectProperties.set_value("height", mid, entity_reference=base)
        TargetService.reapply_macro_details(base)
        lo, hi = (mid, hi) if body_height() < look["height_m"] else (lo, mid)
    HumanObjectProperties.set_value("height", (lo + hi) / 2, entity_reference=base)
    TargetService.reapply_macro_details(base)
height = body_height()        # before clothes: they hide the body parts they cover
rig = HumanService.add_builtin_rig(base, look.get("rig", "game_engine"))
rig.name = "civilian_rig"
HumanService.set_character_skin(asset("skins", look["skin"], ".mhmat"), base)
for name in look.get("clothes", []):
    HumanService.add_mhclo_asset(asset("clothes", name, ".mhclo"), base, asset_type="Clothes", subdiv_levels=0)
if look.get("hair"):
    HumanService.add_mhclo_asset(asset("hair", look["hair"], ".mhclo"), base, asset_type="Hair", subdiv_levels=0)

def tint(obj, t):
    """Insert hue/saturation/value and multiply nodes in front of the material's base colour."""
    for slot in obj.material_slots:
        mat = slot.material
        if mat is None or not mat.use_nodes:
            continue
        nt = mat.node_tree
        bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if bsdf is None:
            continue
        base = bsdf.inputs["Base Color"]
        if not base.is_linked:
            c = Color(base.default_value[:3])
            c.h = (c.h + t.get("hue", 0.0)) % 1.0
            c.s = min(1.0, c.s * t.get("sat", 1.0))
            c.v = c.v * t.get("val", 1.0)
            mul = t.get("mul", [1, 1, 1])
            base.default_value = (c.r * mul[0], c.g * mul[1], c.b * mul[2], 1.0)
            continue
        link = base.links[0]
        src = link.from_socket
        nt.links.remove(link)
        hsv = nt.nodes.new("ShaderNodeHueSaturation")
        hsv.inputs["Hue"].default_value = (0.5 + t.get("hue", 0.0)) % 1.0      # 0.5: no shift
        hsv.inputs["Saturation"].default_value = t.get("sat", 1.0)
        hsv.inputs["Value"].default_value = t.get("val", 1.0)
        nt.links.new(src, hsv.inputs["Color"])
        out = hsv.outputs["Color"]
        if "mul" in t:
            mix = nt.nodes.new("ShaderNodeMix")
            mix.data_type = "RGBA"
            mix.blend_type = "MULTIPLY"
            mix.inputs["Factor"].default_value = 1.0
            nt.links.new(out, mix.inputs["A"])
            mix.inputs["B"].default_value = (*t["mul"], 1.0)
            out = mix.outputs["Result"]
        nt.links.new(out, base)


for name, t in look.get("tint", {}).items():
    obj = bpy.data.objects.get("civilian." + name)
    if obj is None:
        raise KeyError("tint for an asset the look does not wear: " + name)
    tint(obj, t)

print("CIVILIAN height %.3f m, objects %s" % (height, sorted(o.name for o in bpy.data.objects)))
bpy.ops.wm.save_as_mainfile(filepath=out)
print("SAVED", out)
