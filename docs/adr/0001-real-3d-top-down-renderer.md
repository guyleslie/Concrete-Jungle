# ADR-0001: Real 3D renderer with a top-down perspective camera

- **Status:** Accepted
- **Date:** 2026-09-26 (recorded 2026-09-27)

## Context

The game should look like GTA 2: an overhead view in which tall buildings lean away from the centre of the screen, and bridges, overpasses and gate buildings hide what passes underneath. A flat 2D sprite renderer cannot produce either effect convincingly; faking parallax per object is fragile and does not give correct occlusion.

## Decision

Render the city as real 3D geometry seen through a perspective camera looking straight down:

- the game logic stays on a 2D ground plane; a ground point *(x, y)* at height *h* becomes the 3D point *(x, h, y)*;
- buildings, the metro deck, pillars and boxy props are 3D boxes;
- vehicles, people, trees and props are flat textured quads placed at their real height;
- all render passes (scene, light, emissive) share one depth buffer, so lights and glows are hidden under roofs and bridges automatically.

## Alternatives considered

- **Pure 2D sprites with painted building sides** — no real perspective or occlusion; buildings look flat.
- **2D with per-object parallax offsets** — approximates leaning, but occlusion under bridges needs special cases everywhere.

## Consequences

- Perspective, occlusion and correct light hiding come for free from the GPU.
- The camera must stay above the tallest roof; zooming in narrows the field of view instead of lowering the camera.
- Every sprite needs a sensible height, and flat sprites must be drawn without depth writes where they overlap.
- Shadows are projected flat on the ground in a separate pass rather than computed in 3D.
