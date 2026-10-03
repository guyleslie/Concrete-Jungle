# Architecture decision records

An architecture decision record (ADR) captures one significant technical decision: the situation that required it, what was decided, and the consequences. ADRs explain *why* the code is the way it is, so a later change does not unknowingly undo a hard-won decision.

## Index

| ADR | Title | Status | Date |
|---|---|---|---|
| [0001](0001-real-3d-top-down-renderer.md) | Real 3D renderer with a top-down perspective camera | Accepted | 2026-09-26 |
| [0002](0002-data-driven-content.md) | Data-driven content in plain-text files | Accepted | 2026-09-26 |
| [0003](0003-world-scale.md) | World scale of 16 pixels per metre | Accepted | 2026-09-26 |
| [0004](0004-kinematic-rail-traffic.md) | Kinematic "rail" traffic | Accepted | 2026-09-26 |
| [0005](0005-vehicle-contact-solver.md) | Sub-stepped impulse solver for vehicle contacts | Accepted | 2026-09-27 |
| [0006](0006-pedestrian-steering.md) | Predictive perception and time-to-collision steering for pedestrians | Accepted | 2026-09-27 |
| [0007](0007-dynamic-vehicle-handling.md) | Dynamic axle-based vehicle handling | Proposed | 2026-09-27 |
| [0008](0008-human-like-traffic.md) | Human-like physical traffic and persistent drivers | Proposed | 2026-10-03 |

ADRs 0001–0004 were recorded retroactively on 2026-09-27 for decisions made during the first development session.

## When to write one

Write an ADR when a decision:

- changes how a subsystem fundamentally works (for example replacing the handling model);
- introduces or removes a dependency, file format or convention other code relies on;
- rejects an obvious alternative for reasons that are not visible in the code.

Small, local choices belong in code comments, not ADRs.

## Process

1. Copy the template below into `NNNN-short-title.md`, using the next free number.
2. Set the status to `Proposed` while it is under discussion and `Accepted` once implemented.
3. Add it to the index.
4. Never rewrite an accepted ADR's decision. To change course, write a new ADR and set the old one's status to `Superseded by ADR-NNNN`.

## Template

```markdown
# ADR-NNNN: Title

- **Status:** Proposed | Accepted | Superseded by ADR-NNNN
- **Date:** YYYY-MM-DD

## Context

The situation and the forces at play: requirements, constraints, problems observed.

## Decision

What we decided to do, stated plainly.

## Alternatives considered

The options that were rejected and why.

## Consequences

What becomes easier or harder, new obligations, known risks.
```
