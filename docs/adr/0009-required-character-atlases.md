# ADR-0009: Required atlases for the player and the civilians

- **Status:** Accepted; extended to all content by [ADR-0010](0010-no-fallback-content.md)
- **Date:** 2026-10-11
- **Amends:** [ADR-0002](0002-data-driven-content.md), for people only

## Context

[ADR-0002](0002-data-driven-content.md) gives every image a procedural fallback, so that a missing file never stops the game. For people that meant two fallbacks: procedural civilians, drawn when the civilian atlases are missing, and the "Animated Top Down Survivor Player" sprites with a procedural look-alike, drawn when the player's atlases are missing.

Since [CJ-004](../backlog.md#cj-004-replace-placeholder-art) and [CJ-011](../backlog.md#cj-011-relaxed-player-posture) both are rendered from 3D models in layered atlases with their own frame layouts, gaits, weapon holds and actions. The fallbacks cannot show any of that: the player's fallback had no lowered weapon, no strafes and no reloads, and the procedural civilians no jog or run. A game drawn with them would look and behave like a different game, the code kept two drawing paths per character and 15 MB of sprites shipped unused, and the user decided on 2026-10-11 that they will not be used.

## Decision

- The player's atlases (`assets/data/player.cfg`, `assets/characters/player/`) and the civilian atlases (`assets/data/civilians.cfg`, `assets/characters/civilians/`) are required. When they are missing or incomplete, loading fails with a clear message, as for a missing shader.
- The Survivor sprites, `characters.cfg`, the procedural civilian and player generators and their drawing paths are removed.
- Every weapon's set (`anim set` in `weapons.cfg`) must have an upper-body atlas in `player.cfg`.
- Everything else keeps ADR-0002: built-in data defaults, and procedural fallbacks for vehicles, foliage, materials and street furniture.

## Alternatives considered

- **Keep the fallbacks** — they never show in a complete installation, and in a broken one they show a different game; they cost a second drawing path for each character and the Survivor attribution on the title screen.
- **Generate a simple layered fallback** — would have to reproduce every gait, hold and action to be of use, which is the 3D pipeline itself.

## Consequences

- A release must ship the character atlases; [package_release.py](../../tools/package_release.py) packs the whole `assets/` folder.
- The loading screen reports a missing or incomplete atlas as a failure instead of a fallback.
- The "Survivor" character's CC-BY attribution is no longer needed; the title screen credits the sources of the new people instead.
- Changing a person's look or animation means re-rendering with the pipelines of [CJ-004](../../assets/art/cj004/README.md#civilian-3d-pipeline) and [CJ-011](../../assets/art/cj011/README.md).
