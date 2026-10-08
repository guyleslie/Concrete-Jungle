"""CJ-004: build one civilian from MPFB (MakeHuman) CC0 assets and save it as a .blend.

Run with Blender 5.2 and the MPFB extension, the MakeHuman CC0 asset packs loaded:
    blender -b --factory-startup --python tools/cj004/build_civilian.py -- <look.json> <out.blend>

The look file names the macro details (MakeHuman 0..1 scales) and the assets, for example
    {"macro": {"gender": 1.0, "age": 0.58, ...}, "race": {"caucasian": 1.0},
     "skin": "middleage_caucasian_male", "clothes": ["male_casualsuit05", "shoes03"], "hair": "short02"}
The character stands at the origin facing -Y (MakeHuman's front), 1 Blender unit = 1 m.
"""
import bpy, json, os, sys

import addon_utils
addon_utils.enable("bl_ext.user_default.mpfb", default_set=True)
from bl_ext.user_default.mpfb.services import HumanService, TargetService, LocationService

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
rig = HumanService.add_builtin_rig(base, look.get("rig", "game_engine"))
rig.name = "civilian_rig"
HumanService.set_character_skin(asset("skins", look["skin"], ".mhmat"), base)
for name in look.get("clothes", []):
    HumanService.add_mhclo_asset(asset("clothes", name, ".mhclo"), base, asset_type="Clothes", subdiv_levels=0)
if look.get("hair"):
    HumanService.add_mhclo_asset(asset("hair", look["hair"], ".mhclo"), base, asset_type="Hair", subdiv_levels=0)

# Log the height of the evaluated body (macro targets are shape keys; helpers are masked).
evaluated = base.evaluated_get(bpy.context.evaluated_depsgraph_get())
zs = [(base.matrix_world @ v.co).z for v in evaluated.to_mesh().vertices]
evaluated.to_mesh_clear()
print("CIVILIAN height %.3f m, objects %s" % (max(zs) - min(zs), sorted(o.name for o in bpy.data.objects)))
bpy.ops.wm.save_as_mainfile(filepath=out)
print("SAVED", out)
