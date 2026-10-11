# ADR-0010: No fallback content

- **Status:** Accepted
- **Date:** 2026-10-11
- **Amends:** [ADR-0002](0002-data-driven-content.md); extends [ADR-0009](0009-required-character-atlases.md) from the people to all content

## Context

[ADR-0002](0002-data-driven-content.md) made sure that a missing file never stops the game: every loader had a built-in copy of its data file, and every image a procedural fallback. In practice this kept old art alive: placeholder cars, buses, trucks and motorbikes painted in code, painted trees and bushes, noise textures for the ground and the walls, a bare fists-only weapon set, raylib's pixel font for the interface, and a loading screen drawn in that font when its own art was missing. [ADR-0009](0009-required-character-atlases.md) already removed the fallbacks for people, because a game drawn with them looked and behaved like a different game.

On 2026-10-11 the user saw such a fallback: the first 0.4 s of every start showed the loading screen's text in raylib's pixel font, before its art was loaded. The user decided that the code keeps no old fallbacks.

## Decision

- Every data file in `assets/data/` (`vehicles.cfg`, `weapons.cfg`, `civilians.cfg`, `player.cfg`, `foliage.cfg`, `traffic.cfg`, `loading.cfg`) and every file a record names is required. There are no built-in copies of the data files.
- A missing or unreadable file, or a record that names an unknown vehicle class or paint colour, stops loading. The loading screen shows the cause, the log names the file, and the game exits with code 2. Other malformed records are still reported as warnings and skipped, as before.
- The loading screen's own configuration, background, wordmark and fonts are required. Until they are loaded the window shows a plain dark frame; without them the game logs the missing file and exits with code 2, since nothing could be shown.
- Removed: the built-in data copies, the procedural placeholder vehicles and trees with the `GEN` record, the noise textures, the fallback vehicle, trees, bushes and weapon, raylib's default font as the interface font, and the loading screen's text-only presentation.
- Kept, because they are the content itself and not substitutes: the procedural street furniture, metro car, water and effect textures, and the synthesised sounds, which a WAV file of the same name still overrides. A missing audio device still lets the game run silently, since it is the machine, not the content, that lacks it.

## Alternatives considered

- **Keep the fallbacks** — they never show in a complete installation, and in a broken one they show a different game; they kept a second, lower-quality version of much of the art in the code.
- **Only remove the loading screen's fallback** — fixes what the user saw, but leaves the same problem everywhere else.

## Consequences

- A release must ship the whole `assets/` folder; [package_release.py](../../tools/package_release.py) packs it.
- A broken installation does not start, and says exactly which file it lacks.
- Adding a vehicle needs an image file: there are no procedural vehicles any more.
- The CJ-030 checks that ran with missing cosmetics or content now expect exit code 2 instead of a fallback (see [CJ-030 loading checks](../testing/cj030-loading.md)).
