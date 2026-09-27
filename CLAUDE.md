# Concrete Jungle — working notes for Claude

A GTA 1/2-style top-down open-city game in C++17 with raylib 6.0 (Windows, w64devkit GCC). The project documentation is the source of truth; this file only lists how to work here.

## Read first

| Need | Document |
|---|---|
| How the code fits together | [docs/architecture.md](docs/architecture.md) |
| A subsystem in depth | [docs/design/](docs/README.md#design-documents) |
| What to work on | [docs/backlog.md](docs/backlog.md) |
| How to verify a change | [docs/testing.md](docs/testing.md) |
| Why something is the way it is | [docs/adr/](docs/adr/README.md) — do not undo an accepted decision without a new ADR |
| Workflow, style, commits, documentation rules | [CONTRIBUTING.md](CONTRIBUTING.md) |

## Working with the user

- The user writes Hungarian: reply in Hungarian. Code, comments and all repository documents are in English.
- Quality bar: consistent, high-quality art (glossy, detailed; no flat cartoon or pixel art), realistic scale, the GTA 2 look.
- Content stays data-driven (`assets/data/*.cfg`).
- One backlog item per session. Agree a short specification with measurable criteria before coding, measure before and after with a test scenario, and let the user playtest anything that changes the feel.
- Documentation is part of every change: update the affected documents, the changelog and the backlog in the same commit, following the writing style in CONTRIBUTING.md, and run `python tools/check_docs.py`.
- The user watches the test windows. Verify with a run before claiming a fix works.

## Commands

```bash
sh build.sh
```

```bash
./ConcreteJungle.exe --shot build/shots/crash.png --frames 1500 --scenario crash > build/shots/crash.log 2>&1
```

```bash
python tools/check_docs.py
```

## Environment gotchas

- The project path contains `á`: PowerShell needs `-LiteralPath`; CMake needs the Ninja generator.
- `build.bat` must stay CRLF (enforced by `.gitattributes`).
- raylib prefixes the working directory to screenshot paths: pass relative paths to `--shot`.
- This raylib build cannot load JPG.
- For longer scripts (patches, file splicing) write the script to the scratchpad and run it instead of inlining it in a shell command.
