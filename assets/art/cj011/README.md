# CJ-011 player art

The player becomes a 3D character rendered from above with the [civilian 3D pipeline](../cj004/README.md#civilian-3d-pipeline), replacing the Survivor sprites, so that it walks and runs with the weapon lowered and raises it only to aim and fire ([CJ-011](../../../docs/backlog.md#cj-011-relaxed-player-posture)). This document records the player's look, the weapon holds, the layered gaits, the scripts that make them and their measurements. The renders are not in the game yet.

## Contents

- [Sources](#sources)
- [Look](#look)
- [Weapon holds](#weapon-holds)
- [Layers and gaits](#layers-and-gaits)
- [Scripts](#scripts)
- [Measurements](#measurements)
- [Open items](#open-items)

## Sources

| Part | Source | Licence |
|---|---|---|
| Body, clothes, hair, skin | MakeHuman asset packs, as the civilians ([CREDITS](../../../CREDITS.md)) | CC0 |
| Weapons | [Ultimate Gun Pack](https://opengameart.org/content/low-poly-guns-pack) by Quaternius: `Pistol_1` and `AssaultRifle2_1` (an M4-style carbine), chosen by the user on 2026-10-10 | CC0 |
| Walk and run | [Universal Animation Library](https://quaternius.com/packs/universalanimationlibrary.html) by Quaternius: `Walk_Formal_Loop` and `Sprint_Loop`, as the civilians | CC0 |
| Strafe and backpedal | [Mixamo](https://www.mixamo.com/) by Adobe: `Walk Strafe Left`, `Walk Strafe Right`, `Walking Backwards` (In Place, 30 fps, without skin), downloaded by the user on 2026-10-10 | Mixamo terms: free to use in projects; the animation files are not redistributed |

The Universal Animation Library and its second volume contain no strafe or backpedal. Downloads stay in `build/art-sources/` (ignored by Git): the gun pack in `downloads/`, its OBJ files in `guns/`, the Mixamo files in `mixamo/`.

## Look

The player keeps the Survivor's look, so that it stays recognisable and stands out from the civilians: a 1.80 m man in a dark baseball cap, an olive green jacket over a dark shirt, dark trousers and dark ankle boots, with a camouflage backpack.

![The player's poses from above and from the side](player-poses.png)

| Part | Made by |
|---|---|
| Jacket, shirt and trousers | MakeHuman `male_casualsuit05`, its texture recoloured by [player_texture.py](../../../tools/cj011/player_texture.py): the jacket olive green, every island of the striped shirt a plain dark shirt, the denim dark charcoal |
| Boots | MakeHuman `toigo_ankle_boots_male`, darkened |
| Cap | Modelled by [build_player.py](../../../tools/cj011/build_player.py): a crown fitted over the head and hair, six faint seams, a button and a curved peak, fixed to the head bone |
| Backpack | Modelled by the same script: a soft daypack 0.30 m wide, 0.10 m deep and 0.42 m tall with rounded top corners, fuller low than high, its fabric wrinkled by a displacement and woven by a bump texture; a zip round the top, a front pocket with a zip, a grab handle; flat padded shoulder straps laid on the jacket to a buckle on the chest, and narrow lower straps from the buckle round the side to the pack's bottom corners; khaki, tan, olive, sand and brown camouflage. Fixed to `spine_03` |

The user's corrections on 2026-10-10: no striped shirt; the shoulder straps lie flat on the body instead of standing on edge; a flatter pack; a fabric pack with lower straps, not a rounded box; darker camouflage instead of a whitish one; a darker cap with fainter seams.

![The backpack from behind and from the side](player-pack.png)

## Weapon holds

A weapon is placed in the rig's space and the hands are put on it by a two-bone inverse kinematics solved in [render_player.py](../../../tools/cj011/render_player.py): the palm on the grip (the right hand) or on the fore grip (the left), the hand turned to hold it, the elbow towards a pole. The elbow is a hinge: the upper arm turns so that the arm bends in its rest plane, and the forearm's twist is shared between the elbow and the wrist. The wrist bends at most 30–35° beyond its rest angle; beyond that the hand turns about the palm towards the forearm.

| Hold | Pistol | Rifle |
|---|---|---|
| Lowered (stand, walk) | Beside the right thigh, muzzle 65° down and a little forward, in the swinging right hand | Low ready: the butt at the right shoulder pocket, the barrel 45° down, the torso turned 32° to the right and leaned 8° forward, the left hand under the middle of the handguard |
| Lowered (run, strafe, backpedal) | The hand swings 20–30 % of the gait's swing about where it hangs standing, muzzle 72° down running | As the walk; running 52° down, backpedalling 48°; the chest does not twist with the hips |
| Aim | Isosceles: the arms 90 % extended, elbows down, the torso leaned 7° forward, the shoulders a little forward, the sights 3 cm below the right eye and on its line (3.5 cm right of the centre), the support hand on the grip's left with both thumbs forward | Shouldered, the barrel level |
| Fire | The aim with the muzzle 12° up and the elbows giving (85 %) | The aim with the muzzle 6° up and the butt 2.5 cm back |

The user's corrections on 2026-10-10: the support hand holds the handguard in its middle and supports it from below with an open hand, not a clenched fist; no twisted elbow; the pistol aimed in the isosceles stance, the elbows soft, the gun before the right eye and a little lower.

## Layers and gaits

The legs and the upper body are separate layers, so that the torso faces the aim while the legs walk, strafe or backpedal along the motion. Each layer comes from its own render of the whole player, the other layer's surfaces made transparent: the suit by a texture mask of its trouser islands, the body by its strongest bone, the boots to the legs, everything else (the jacket, head, cap, backpack and weapon) to the upper body. A layer looks through many transparent surfaces, so the renders allow 64 transparent bounces (Cycles' default of 8 left black specks). The frames are 144 px for 2.1 m, as the civilians'; every job is centred on its mean pelvis position, the shared pivot of both layers.

| Gait | Clip | Frames | Plays |
|---|---|---|---|
| Stand | Built by the script | 1 | — |
| Walk | `Walk_Formal_Loop`, the civilians' gait settings, tuned for the player with the backpack (hip swing 0°, upright 6°) | 16, moved to split the 1.385 m stride evenly | By distance, at 1.5 m/s |
| Run | `Sprint_Loop`, tuned with the backpack (hip swing 14°, upright 25°) | 16: every frame of the cycle | By cadence, 3.3 steps/s at 6.5 m/s |
| Strafe left and right | Mixamo `Walk Strafe Left` / `Right` | 16 over the 44-frame cycle | By distance: 1.17 m and 1.05 m per cycle |
| Backpedal | Mixamo `Walking Backwards` | 16 over the 36-frame cycle | By distance: 1.35 m per cycle |

In the strafes the hips turn 20–42° towards the motion and the shoulders 16–39°, as a person strafing does; the upper-body layer turns the chest back to the front every frame, and the waist takes the twist. The tuning showed that the backpack hides the legs behind the body: tuned without it, the legs showed 1.47 times further ahead than behind walking and 1.63 times running.

![Walk, run, strafe and backpedal at game scale](player-gaits.png)

## Scripts

| Script | What it does |
|---|---|
| [gun_sheet.py](../../../tools/cj011/gun_sheet.py), [gun_grid.py](../../../tools/cj011/gun_grid.py) | Side views of the gun models, side by side and on a grid of model coordinates for reading off grip points |
| [player_texture.py](../../../tools/cj011/player_texture.py) | Recolours the suit's texture; writes the trousers mask for the layers |
| [look-player.json](../../../tools/cj011/look-player.json) | The player's look for [build_civilian.py](../../../tools/cj004/build_civilian.py), whose `diffuse` entry swaps an asset's texture |
| [build_player.py](../../../tools/cj011/build_player.py) | Adds the cap, the backpack and the weapons (`GUNS`: model, real length, grip, fore grip, butt and bore read off the side views) |
| [retarget_ual.py](../../../tools/cj004/retarget_ual.py) | Also reads a Mixamo FBX: its one action is named after the file, its bones are mapped by `MIXAMO` |
| [measure_travel.py](../../../tools/cj011/measure_travel.py) | How far an in-place loop travels per cycle and in which direction, from the contact point that bears the weight; unreliable for runs, whose feet leave the ground |
| [render_player.py](../../../tools/cj011/render_player.py) | Poses (`pistol_aim`, `rifle_low` …) and gait jobs (`gait:<name>:<action>:<frames>`): renders, layers, leg masks and the hold measurements in `measure.json`; `CJ_MEASURE_ONLY=1` measures without rendering |
| [check_layers.py](../../../tools/cj011/check_layers.py) | The layers against the whole render, and the holes at ±90° between them |
| [compare_player.py](../../../tools/cj011/compare_player.py), [gait_preview.py](../../../tools/cj011/gait_preview.py) | The poses next to the Survivor at game scale with their widths; an animated preview of the gaits at game scale |

The steps, from the project root, with Blender 5.2 (`BL`) and the paths of the [civilian pipeline](../cj004/README.md#setting-up):

```bash
python tools/cj011/player_texture.py "<MPFB data>/clothes/male_casualsuit05/male_casualsuit05_diffuse.png" build/art-sources/player/player_suit_diffuse.png
"$BL" -b --factory-startup --python tools/cj004/build_civilian.py -- tools/cj011/look-player.json build/art-sources/player/player-base.blend
"$BL" -b build/art-sources/player/player-base.blend --python tools/cj011/build_player.py -- "build/art-sources/guns/Ultimate Gun Pack - July 2019/OBJ" build/art-sources/player/player.blend
"$BL" -b build/art-sources/player/player.blend --python tools/cj004/tune_gait.py -- "<UAL1_Standard.glb>" build/art-sources/player/tune-pack.json walk run
"$BL" -b build/art-sources/player/player.blend --python tools/cj004/retarget_ual.py -- "<UAL1_Standard.glb>" build/art-sources/player/player-anim.blend "Walk_Formal_Loop=+0,2,…,30~1,0^26,0.1&0.5,5@6" "Sprint_Loop=+0,1,…,15~1,14@25"
"$BL" -b build/art-sources/player/player-anim.blend --python tools/cj004/retarget_ual.py -- "build/art-sources/mixamo/Walk Strafe Left.fbx" build/art-sources/player/player-anim.blend Walk_Strafe_Left
CJ_TROUSERS_MASK=build/art-sources/player/player_suit_diffuse_trousers_mask.png "$BL" -b build/art-sources/player/player-anim.blend --python tools/cj011/render_player.py -- build/cj011/gait "gait:walk:Walk_Formal_Loop:<locked frames>" "gait:strafel:Walk_Strafe_Left:<16 frames>"
```

The other Mixamo files are retargeted like the first. The walk's frames are the `locked_frames` that [measure_gait.py](../../../tools/cj004/measure_gait.py) gives for the walk.

## Measurements

Against the [acceptance criteria](../../../docs/backlog.md#cj-011-relaxed-player-posture), on 2026-10-10, every frame of every gait:

| Criterion | Result |
|---|---|
| 1. Lowered: pistol at least 45° down and at most 0.25 m ahead of the body; long gun 30–60° down and at most 0.45 m ahead | Pistol 65–79° and at most 0.14 m; rifle 45–52° and at most 0.43 m (from the posed torso's front) |
| 2. Aiming: barrel level within ±5°; stock within 5 cm of the shoulder | 0°; the butt on the shoulder pocket point (the point is placed by construction; checked in the side views) |
| 3. Grip within 1 cm of the right palm, fore grip within 3 cm of the left | 0 cm; at most 2.3 cm (running and backpedalling with the rifle) |
| 4. Planted foot slip at most 2 cm walking; legs ahead/behind 0.75–1.33 | 0.7 cm; walk 1.03, run 1.18 |
| 5. Layers match a whole render; no waist gap at ±90° | Mean colour difference 0.3–0.6 of 255, no pixel's alpha off by more than 0.25; new holes at ±90° 2.9 cm² walking and 11.4 cm² running (strafes 21–29 cm², where the legs already step sideways) |
| 6. Width within 10 % of today's player (0.65 m) | Standing 0.58 m (−11 %); aiming the pistol 0.50 m, a narrow isosceles triangle. Open: a draw scale such as the civilians' `SCALE 1.12` would bring standing to 0.65 m |

![The poses next to the Survivor player at game scale](player-game-scale.png)

## Open items

- Aim and fire in the layered renders, reload frames, the remaining weapons (fists, knife, flashlight, shotgun) and the fast strafe and backpedal (for now the walking clips played faster).
- The atlases, the game's two-layer player drawing, the walk and run speeds, the raise and lower timing and the `player` scenario (criteria 7 and 8), then the playtest (criterion 9).
