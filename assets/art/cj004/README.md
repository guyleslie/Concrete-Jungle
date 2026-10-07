# CJ-004 art preparation

The user accepted the motorbike designs on 2026-10-07, subject to correct size, and rejected the earlier civilian sheets and idle masters. The accepted bike source is preserved, and its six exports, which follow the shared vehicle convention below, replaced the procedural motorbikes in the game on 2026-10-07; they await the user's playtest. A new v4 civilian idle candidate awaits review. CJ-004 remains open for the civilians and the motorbike playtest.

## Motorbike exports

| Class | Empty PNG | Ridden PNG | Active alpha bounds (x, y, width, height) |
|---|---|---|---|
| Sportbike | [Empty](../../vehicles/sportbike-empty.png) | [Ridden](../../vehicles/sportbike-ridden.png) | 154, 32, 204, 448 px |
| Chopper | [Empty](../../vehicles/chopper-empty.png) | [Ridden](../../vehicles/chopper-ridden.png) | 140, 32, 232, 448 px |
| Scooter | [Empty](../../vehicles/scooter-empty.png) | [Ridden](../../vehicles/scooter-ridden.png) | 140, 32, 232, 448 px |

The game loads them from `assets/vehicles/` through the `BIKE` records in [vehicles.cfg](../../data/vehicles.cfg).

The [original sheet](motorbike-sheet.png) is 1,536 × 1,024 px: three columns (sport, chopper, scooter), empty bikes above ridden bikes. Its checksum and visual acceptance are recorded in [motorbike-lock.json](motorbike-lock.json). Do not repaint or regenerate the accepted designs to correct packing.

## Shared vehicle convention

- New vehicle exports are 512 × 512 px RGBA PNGs, viewed vertically from above, front facing up (−Y).
- Visible alpha bounds at the runtime threshold of 0.1 have a height of 448 px, from y = 32 px to y = 480 px, and centre (256, 256) px. Use an even width derived from the vehicle's aspect ratio.
- Empty and ridden versions use the same active width, height and centre. The artwork is registered during export. Canvas padding alone is insufficient because the current loader trims alpha independently.
- Position and size corrections belong in the exported pixels. Do not add per-image pivot, offset or scale overrides to the runtime data. Class dimensions in [vehicles.cfg](../../data/vehicles.cfg) remain ordinary physical data.
- Keep genuine transparent padding, soft alpha edges and complete silhouettes. Do not add nearly invisible pixels to manipulate the measured bounds.

The renderer applies the same alpha-trimmed centre/aspect rule to every vehicle and scales its active height to the class length; these exports need no exceptions. The full widths in the [export measurement](motorbike-export-check.json) include the handlebars and mirrors, which the art draws about 30 % wider than on real bikes, so they are not the collision widths: `WIDTH` records in [vehicles.cfg](../../data/vehicles.cfg) give the handlebar width of real bikes, and the drawn mirrors overhang the collision box. [CJ-023](../../../docs/design/street-geometry-proposal.md#vehicle-widths) adds the cars' widths.

## Export and verification

[tools/export_cj004_bikes.cpp](../../../tools/export_cj004_bikes.cpp) crops the source cells, resamples their active bounds into the common layout and writes the PNGs to `assets/vehicles/`. The empty member determines each pair's aspect; small source differences are corrected in the exported pixels. The largest relative aspect correction is about 2.4 % for the ridden sportbike. The accepted source is unchanged. The tool is an offline exporter and check gallery; the game does not read its measurements or the lock file.

Build with the project's compiler and raylib paths:

```powershell
& 'E:\Apps\raylib\w64devkit\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Wno-missing-braces -I'E:/Apps/raylib/raylib/src' tools/export_cj004_bikes.cpp -o build/scratchpad/export_cj004_bikes.exe -L'E:/Apps/raylib/raylib/src' -lraylib -lopengl32 -lgdi32 -lwinmm -static
& '.\build\scratchpad\export_cj004_bikes.exe' --export
```

Run without `--export` to check the saved PNGs and view the gallery without rewriting them. The check reads the actual exported files and uses the same `GetImageAlphaBorder(..., 0.1f)` as the vehicle loader. Baseline source bounds differ between pair members; all six exports pass identical centre/height checks and equal pair bounds. The alternating gallery supports inspection of exposed wheels and handlebars; matching bounds do not imply identical internal source pixels.

![Normalized motorbike gallery, 1 m grid](motorbike-export-preview.png)

The exporter compiles without warnings, and its check passes on the files in `assets/vehicles/`. In the game the empty and ridden images switch without a visible jump: the uncovered front tyre and tail differ by at most 1 px (0.5 cm) between the pair members. There is one look per class. More colours come as more exported pairs and `BIKE` lines, because the game's automatic paint variants also recolour the lights and cannot recolour the black chopper. The user playtest is pending.

## Rejected civilian experiments

[civilian-relaxed-sheet.png](civilian-relaxed-sheet.png) is a rejected 22-pose experiment, not an integration candidate or master reference. Defects include head/body proportions, anatomy, repeated or unclear gait phases and limbs crossing cell boundaries. Do not extract it into a runtime atlas.

Earlier idle attempts projected arms and legs forward. [V3](civilian-idle-master-v3.png) hid the legs but was rejected for enormous rounded shoulder/upper-arm blobs. Preserve failed experiments; do not animate them.

## Civilian idle candidate

[V4 idle master](civilian-idle-master-v4.png), based on the [revised upright pose guide](civilian-upright-pose-guide-v2.png), awaits user review. The target completely occludes idle legs and shoes, with arms hanging vertically, small integrated upper arms, ordinary clothing and the player's moderate detail. No walk, run or action groups have been generated from this candidate. After acceptance, follow the [revised prompts and quality gates](civilian-prompts-v2.md) to generate those groups separately.

## Provenance

[prompts.json](prompts.json) preserves the exact built-in image generation/edit prompts and the user's subsequent corrections. The motorbikes were generated without an external image input. The Survivor player by Riley Gombart (CC-BY 3.0) was a civilian camera/style reference, and this project's procedural civilian preview was an earlier pose reference. Keep the existing [Survivor attribution](../../../CREDITS.md#required-attribution). No third-party CC0 pack licence is claimed for the generated artwork.

Some fully transparent source pixels retain coloured RGB. They can look like glows in previews that ignore alpha; inspect a proper composite rather than removing valid transparency.
