# Civilian art workflow, second attempt

> **Superseded (2026-10-08).** Civilians are now rendered from 3D models; see the [civilian 3D pipeline](README.md#civilian-3d-pipeline). This workflow is kept as a record.

The user rejected the civilian sheets and earlier idle masters on 2026-10-07. The [v4 idle candidate](civilian-idle-master-v4.png), based on the [revised upright pose guide](civilian-upright-pose-guide-v2.png), awaits user review; no animation groups have been generated from it. Idle must completely occlude legs and shoes, with arms hanging vertically and compact, integrated shoulders rather than separate rounded blobs or bulky clothing. Match the existing player's moderate painted detail.

## What failed

The whole-sheet prompts mixed camera, anatomy, style, 22 poses, temporal order, pivots and layout. Results included oversized heads, heavy outlines, unclear gait phases, disconnected limbs and cell overlap. Earlier idle masters also projected arms and legs forward; v3 hid the legs but was rejected for enormous shoulder/upper-arm blobs. Do not use rejected anatomy as a master reference.

## Delivery

1. Review the v4 relaxed idle candidate beside the player: camera, head/shoulder proportions, natural pose and scale. Resolve defects and obtain user acceptance before multiplying it.
2. Generate walk only: eight ordered phases in a 4 × 2 grid, using the accepted master as the identity reference.
3. Generate run only: eight ordered phases in a 4 × 2 grid, keeping the same identity, proportions and camera.
4. Generate actions separately: five poses in a 3 × 2 grid (two forward punches, lying and two raised-fist gestures), with one empty cell. Split the lying pose from the action group if its different projection disrupts the other poses.
5. Repair only the failed group; preserve accepted groups. Register complete poses and assemble the runtime atlas after visual and loop checks.

This starts with four generation calls, rather than 22 individual frame calls. Smaller groups are a quality strategy, not a guarantee of coherent animation. If eight-frame walk/run still drift, request four key poses first and validate them before adding intermediate phases. Keep the four-pose and eight-pose sheets as distinct versions; do not pass a four-frame sequence off as eight unique phases.

## Shared prompt

```text
Use case: stylized-concept.
Asset type: overhead civilian game sprite.
For idle, use the revised upright pose guide as the geometry reference. If the Survivor player is supplied, use it only for moderate painted detail, not combat posture or military equipment.
For animation groups, use the accepted civilian master to preserve identity, clothing, proportions and direction. Never use a rejected pose as the anatomy reference.

An ordinary adult civilian in a simple muted blue jacket, dark trousers and dark shoes, short brown hair. Exactly vertical orthographic overhead view, facing RIGHT. Forehead/nose edge points RIGHT; nape points LEFT. The head and body must agree on facing; clothing below the torso is fully occluded in idle.
Realistic adult head/shoulder proportions comparable to the player reference, with compact upper arms integrated into the shoulder silhouette. No separate rounded shoulder blobs, padded shoulders or bulky jacket. Broad simple shading, restrained hair highlights and a few cloth folds. No oversized round head, heavy cartoon contour, individual hairs or fabric microtexture.
Keep the shoulder/torso root at the same local position and preserve body scale.
Actual transparent alpha; no floor, ground shadow, scenery, text or visible grid. Complete silhouettes with clear padding.
```

Do not include every state's instructions in the same prompt. Append only the current group below. For edits, supply the accepted master and only the group being repaired; do not supply the rejected full sheet as an identity reference.

### Idle master

```text
One relaxed upright standing pose only, centred with generous transparent padding. Arms hang vertically toward the ground beside the torso; hands and forearms are occluded, with only small integrated upper-arm caps visible. Legs AND shoes are completely hidden beneath the torso: no foot tips, trouser shapes or forward-projecting limbs. Keep a compact natural shoulder silhouette without separate rounded blobs or bulky clothing. Crown and shoulder tops are visible; no front-view chest or portrait face. Match the player's moderate painted detail.
```

### Walk

```text
Eight ordered frames in a 4-column by 2-row sheet, read left to right, 256 px square cells. An in-place natural walk with opposite arm and leg movement:
0 left foot forward contact;
1 left support/down;
2 right foot passing;
3 right foot travelling forward/up;
4 right foot forward contact;
5 right support/down;
6 left foot passing;
7 left foot travelling forward/up.
Frame 7 connects naturally to frame 0. Hands swing gently around the relaxed arms-down pose; no forward guard. Preserve the master's head, torso, clothing, camera and scale. Exactly two connected arms, two connected legs and two feet with believable occlusion. Keep each complete pose inside its cell with padding; the torso root remains at the same local coordinate.
```

### Run

```text
Eight ordered run frames in a 4-column by 2-row sheet, read left to right, 256 px square cells. An in-place run using the same accepted civilian identity and camera:
0 left foot forward contact;
1 left support/compression;
2 left push-off while right leg passes;
3 flight with right leg reaching forward;
4 right foot forward contact;
5 right support/compression;
6 right push-off while left leg passes;
7 flight with left leg reaching forward.
Frame 7 connects naturally to frame 0. Elbows bend for running and swing opposite the legs. Keep head and body facing RIGHT, with a stable torso root. No extra limbs, detached shoes or unrelated poses. Preserve the head/shoulder proportions and body scale in every phase.
```

### Actions

```text
Five distinct action poses in a 3-column by 2-row sheet, read left to right, 256 px square cells:
0 right-hand forward punch toward RIGHT, connected shoulder/elbow/wrist; inactive hand near body;
1 left-hand forward punch toward RIGHT, connected shoulder/elbow/wrist; inactive hand near body;
2 lying face down, head toward RIGHT, complete body with connected arms and legs;
3 raised fist held above the shoulder toward the overhead camera;
4 the same raised-fist gesture with a small movement;
5 empty transparent cell.
Both punches travel forward along the heading, not sideways. A lifted fist is distinct from a forward punch: foreshorten the arm toward the camera. Preserve identity, camera and natural body proportions. Leave enough padding for the full lying body and every extended limb.
```

## Quality gates

- Reject reversed heads, extra or detached limbs, oversized heads, implausible shoulder/elbow/wrist connections and a combat guard in relaxed poses.
- Idle must show no legs, shoes, hands or forward forearms; reject enormous separate shoulder/upper-arm blobs and bulky clothing. Natural overhead occlusion is required, not a crop through limbs.
- Compare the idle at intended game size beside the player before generating appearance variants. Head and shoulders should read as belonging to a person of the same scale.
- Every pose needs clear cell margins and genuine transparent gaps. Do not repair cropping by hiding a limb or shrinking just that frame.
- Register the torso root explicitly; independent alpha-box centres move as limbs swing.
- View walk/run loops slowly and at intended playback speed, including the last-to-first transition. Check alternate support feet, distinct ordered phases, body scale and stable pivots.
- Inspect punches, raised fists and lying at game size. Each action must be readable and anatomically distinct.

The current renderer has only an eight-frame walk loop shared with running. Integration needs a data-driven state/sequence contract and explicit run selection; the generated run group is not used by the existing game.
