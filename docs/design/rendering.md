# Rendering

How the top-down view is drawn: the camera, the render passes, day/night lighting, particles and the HUD. The choice of a real 3D renderer is recorded in [ADR-0001](../adr/0001-real-3d-top-down-renderer.md).

Source: `src/render.*` (camera, passes, drawing helpers), `src/lighting.*` (day/night), `src/particles.*`, `Game::DrawWorld` (`src/game.cpp`), `src/hud.cpp`, shaders in `src/assets.cpp`.

## Contents

- [World to screen](#world-to-screen)
- [Camera](#camera)
- [Render passes](#render-passes)
- [Day and night](#day-and-night)
- [Particles and decals](#particles-and-decals)
- [HUD](#hud)

## World to screen

The game logic lives on the ground plane *(x, y)*. The renderer maps a ground point at height *h* to the 3D point *(x, h, y)* and looks straight down through a perspective camera.

- **Buildings**, the metro deck, pillars and boxy props are real 3D geometry. Perspective makes tall things lean away from the screen centre, and the depth buffer makes bridges and gate buildings hide what passes beneath them.
- **Vehicles, people, trees and props** are flat textured quads placed at their real height (a car's roof, a person's head, a tree's canopy). Vehicles are drawn from low to high, so trucks overlap cars correctly.
- Tree canopies turn semi-transparent while a person walks underneath.

## Camera

| Situation | Visible ground height |
|---|---|
| On foot | 19 m; the view shifts towards the mouse cursor |
| In a vehicle | 60 m standing still, widening to 100 m at top speed (15 % more for large vehicles); the view leads in the direction of travel |

The camera never drops below 62 m above the ground, well above the tallest roof. To zoom in, it narrows its field of view (up to 62°) instead of descending into the buildings. Impacts, explosions and gunfire add camera shake.

## Render passes

`Game::DrawWorld` renders into off-screen targets that share one depth buffer:

| # | Pass | Contents |
|---|---|---|
| 1 | Shadow mask | Sun shadows of buildings, the metro, trees, props, vehicles and people, drawn flat on the ground |
| 2 | Scene | Ground, road markings, decals, the shadow overlay, 3D structures, sprites and lit particles, with depth |
| 3 | Light | Ambient light plus additive lights: street lamps, headlights, brake lights, sirens, fire, muzzle flashes. Depth-tested, so ground lights are hidden under roofs and bridges |
| 4 | Emissive | Self-lit surfaces: lit windows, neon, bulbs, signal lamps, fire, sparks, tracers |
| 5 | Bloom | The emissive buffer, downsampled and Gaussian-blurred |
| 6 | Composite | *albedo × light + emissive + bloom*, tone-mapped with a vignette, drawn to the screen |

Bloom strength rises at night (0.85 by day, up to 1.45 at night). Light primitives are `Radial`, `Cone` (headlights) and `Flare` (bulbs).

## Day and night

| Parameter | Value |
|---|---|
| Length of a day | 4.8 minutes (hold T to fast-forward 30×) |
| Starting time | 19:18, dusk — the lights come on |
| Ambient light | Interpolated between colour keys: deep blue at night, warm at dawn and dusk, white by day |
| Night factor | Derived from ambient brightness; drives street lamps, headlights, windows and bloom |
| Shadows | The sun moves east to west between 06:00 and 18:00; shadows lengthen at dawn and dusk. A faint moon shadow remains at night. |

## Particles and decals

Particles live in 3D (ground position plus height), so smoke rises, debris flies in an arc and bounces, and everything shares the scene's perspective.

| Kind | Examples |
|---|---|
| Particles | Smoke, fire, sparks, debris, dust, blood, glass, shockwave rings, water, shell casings |
| Ground marks | Tyre skid marks; decals for blood, scorch marks, oil and bullet holes |
| Short-lived lights | Explosion and muzzle flashes |
| Tracers | Bullet trails |

## HUD

Drawn in screen space after the composite (`src/hud.cpp`):

- health and armour bars, the current weapon with clip and reserve ammunition;
- money, wanted stars and the clock;
- a minimap and, in a vehicle, a speedometer with the vehicle's health;
- the mission objective, its timer and an arrow with the distance to the target;
- an "[E] Enter / Hijack" prompt next to vehicles;
- messages (toasts), large banners such as WASTED, the help overlay (F1) and a debug line (F3);
- on foot, a crosshair at the mouse position (tighter while aiming), because the Windows cursor is hidden.

The game runs in a borderless full-screen window at the monitor's resolution (`src/main.cpp`); the render targets follow the screen size. Test runs (`--shot`) keep a fixed 1,600 × 900 window.
