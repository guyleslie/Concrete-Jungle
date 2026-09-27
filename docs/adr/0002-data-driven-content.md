# ADR-0002: Data-driven content in plain-text files

- **Status:** Accepted
- **Date:** 2026-09-26 (recorded 2026-09-27)

## Context

The game needs many vehicles, character animations, weapons and plants, and more will be added over time as better art is found. Adding content should not require changing or recompiling code, and a missing or broken file must never stop the game.

## Decision

Describe content in line-based text files in `assets/data/` (`vehicles.cfg`, `characters.cfg`, `weapons.cfg`, `foliage.cfg`):

- one record per line, whitespace-separated tokens, `#` comments;
- a tiny shared parser (`datafile.*`); each loader interprets its own record types, so new record types need no parser changes;
- every loader has built-in defaults, and every image a procedural fallback;
- sounds are synthesised, and a WAV file with the sound's name overrides the synthesised version.

## Alternatives considered

- **Hard-coded tables** — every new vehicle needs a code change and a rebuild.
- **JSON or another structured format** — more expressive, but needs a parser dependency and is noisier to edit by hand for flat, tabular data.

## Consequences

- New vehicles, characters, weapons and foliage need no code changes.
- Loaders must validate input and log warnings instead of failing.
- Values in the files are the single source of truth; documentation refers to them instead of copying them.
- Street furniture is still defined in code; moving it to a data file is tracked as [CJ-005](../backlog.md#cj-005-data-driven-street-furniture).
