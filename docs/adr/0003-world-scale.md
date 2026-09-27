# ADR-0003: World scale of 16 pixels per metre

- **Status:** Accepted
- **Date:** 2026-09-26 (recorded 2026-09-27)

## Context

Sprites from different sources, the city geometry and the simulation must agree on sizes. Mismatched scales — cars as wide as a street, people as tall as a door — immediately break the look. Physical quantities (speeds, accelerations, distances) should also be easy to relate to real-world values.

## Decision

Use one world unit, the world pixel, with **16 px = 1 m** (`cfg::PX_PER_METER`) everywhere: city layout, vehicle and prop dimensions, speeds and accelerations. Real-world sizes are entered in metres and converted on load. People are the one deliberate exception: they are drawn 1.35× larger than life (`cfg::CHAR_SCALE`) so they stay readable next to vehicles, as in the classic games.

## Alternatives considered

- **Metres as the internal unit** — physically clean, but every drawing call would need a conversion, and pixel-based art sizes would be less intuitive.
- **Per-asset scale factors** — flexible, but the relative scale drifts as assets are added.

## Consequences

- All sizes and speeds in code are in px and px/s; 16 px/s = 1 m/s = 3.6 km/h.
- A 4-metre tile is 64 px, a 64-metre block 1,024 px.
- Sprites need enough resolution for 16 px per metre on screen at the closest camera view.
- The collision radius of people scales with `CHAR_SCALE`.
