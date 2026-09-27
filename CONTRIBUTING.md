# Contributing

How work is done on Concrete Jungle: the workflow, coding style, commits, documentation and assets.

## Contents

- [Workflow](#workflow)
- [Branches and commits](#branches-and-commits)
- [Coding style](#coding-style)
- [Documentation](#documentation)
- [Assets](#assets)

## Workflow

1. **Pick one item** from the [backlog](docs/backlog.md). Keep changes focused on it.
2. **Specify** the target behaviour and how it will be measured before writing code. For simulation changes, add or extend a [test scenario](docs/testing.md).
3. **Measure the baseline** with the relevant scenarios.
4. **Implement** the change.
5. **Verify**: build without warnings, run the scenarios again, compare the metrics, and look at a screenshot series.
6. **Document**: update the affected documents in the same change (see [Documentation](#documentation)).
7. **Commit** and move the backlog item to *Done*.

Changes that affect the feel of the game (handling, damage, AI behaviour) are finished only after a playtest.

## Branches and commits

- `main` must always build and run.
- Use a short-lived branch for multi-step work, named after the backlog item: `feature/cj-002-handling-model`.
- One logical change per commit.

Commit messages follow the usual Git conventions:

```
Replace arcade yaw-rate steering with a bicycle model

Explain what changed and why, wrapped at 72 characters. Mention
measured results when they matter.

Refs: CJ-002
```

- Subject line in the imperative mood, at most 72 characters, no trailing full stop.
- A blank line, then a body that explains *why*.
- Reference the backlog item with `Refs: CJ-NNN`.

## Coding style

The code base is C++17 on raylib. Follow the style of the surrounding code:

| Aspect | Convention |
|---|---|
| Indentation | 4 spaces; opening braces on the same line |
| Types | `PascalCase` (`Vehicle`, `CityMap`, `ImpactEvent`) |
| Functions | `PascalCase` (`UpdateVehicles`, `AIKnock`) |
| Variables and members | `camelCase` (`maxSpeed`, `kinFrom`) |
| Constants | `UPPER_SNAKE_CASE` (`TILE`, `KNOCK_SPEED`); global tuning constants in `config.h` under `cfg::` |
| Enumerations | `enum class` with `PascalCase` values |
| File header | Every module starts with a banner comment describing what it does and how it fits in |
| Sections | Separated by `// ----` banner comments |
| Comments | Explain *why*, units and non-obvious maths; do not restate the code |
| Units | World pixels (16 px = 1 m), px/s, radians, tonnes — see [Architecture › Units and conventions](docs/architecture.md#units-and-conventions) |

Principles:

- **Data-driven content.** New vehicles, characters, weapons and foliage come from `assets/data/*.cfg`, never from code ([ADR-0002](docs/adr/0002-data-driven-content.md)).
- **Separation of concerns.** The physics step reports impacts; game rules decide what they mean. AI sets controls or kinematic poses; it does not move physics bodies directly.
- **Compile cleanly** with `-Wall`.

## Documentation

Documentation is part of the change, not an afterthought.

### What to update

| When you change… | Update |
|---|---|
| A subsystem's behaviour or tuning constants | Its [design document](docs/README.md#design-documents) |
| Data file formats | [Adding content](docs/guides/adding-content.md) and the comment header of the data file |
| Build scripts or requirements | [Building](docs/guides/building.md) and the README quick start |
| Test scenarios or metrics | [Testing](docs/testing.md), including the baseline if it moves intentionally |
| A fundamental approach | Write a new [ADR](docs/adr/README.md) |
| Anything user-visible | [CHANGELOG.md](CHANGELOG.md) under *Unreleased* |
| Controls or features | [README.md](README.md) |
| Third-party assets | [CREDITS.md](CREDITS.md) |

### Writing style

- English, clear and concise; address the reader directly in guides.
- One `#` title per file, followed by a one-paragraph summary. Documents longer than a few screens get a table of contents.
- Sentence-case headings.
- Tables for reference data; numbered lists for procedures; bullet lists for unordered facts.
- Code, file names, identifiers and commands in backticks; fenced code blocks with a language.
- Relative links between documents. Link to the source of truth (a data file, a design document) instead of copying values that can change.
- Do not hard-wrap paragraphs.
- Units with every number (px, m, km/h, s).

### Checking

Run the link checker before committing documentation changes:

```bash
python tools/check_docs.py
```

It reports broken relative links and anchors in every Markdown file of the project.

## Assets

- Match the existing art quality and style; see [Adding content › Art and licence requirements](docs/guides/adding-content.md#art-and-licence-requirements).
- Only use assets whose licence allows redistribution, and record each one in [CREDITS.md](CREDITS.md).
- PNG for images, WAV for sound overrides.
