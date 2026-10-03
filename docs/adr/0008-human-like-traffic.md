# ADR-0008: Human-like physical traffic and persistent drivers

- **Status:** Proposed
- **Date:** 2026-10-03

## Context

The user rejects resolving traffic blockages by making a driver leave after a deadline or relocating the car. The current recovery timer does this after 12 s. Rail traffic also derives poses from a lane path and can resolve mutual blocking by ignoring an obstacle. [CJ-016's proposal](../design/traffic-behaviour-proposal.md) requires actual manoeuvres, cooperative yielding, persistent drivers and event-driven incidents.

[ADR-0004](0004-kinematic-rail-traffic.md) chose rail poses because the original physical controllers jittered and deadlocked. It remains Accepted until a measured replacement addresses both failures. This proposal does not change production physics or supersede that decision yet.

## Proposed decision

Visible/interacting drivers issue controls to the shared physical vehicle integrator. Lane routes guide intent; observation and bounded swept-footprint planning select feasible manoeuvres. Local wait-for groups assign stable yielding roles without removing bodies from perception or contacts. Controllers use measured vehicle capabilities, with the CJ-002 axle model as a dependency.

Keep drivers as persistent actors across in-car and on-foot behaviour. Ownership, incident partners and reservations use generation-checked references. Leaving an obstructed car is a meaningful decision with an explicit cause, never an elapsed-time fallback. Physically impossible blockage remains present and stable or receives visible recovery assistance.

Optimize shared snapshots, spatial queries, reusable buffers and staggered planning before considering threading or distant simulation simplification. Immediate collision checks and physical controls continue between planning jobs.

## Alternatives considered

- Increase the abandonment deadline: postpones the same rejected behaviour without establishing a feasible escape.
- Remove the deadline only: can leave an uncontrolled recovery loop pressing against an obstacle indefinitely.
- Force all cars onto unimpeded rail poses: bypasses physical traffic interactions and persistent incidents.
- Add random collisions and fights: creates activity without causality or resolving the underlying traffic problem.
- Import a complete traffic simulator: adds integration and licensing complexity beyond this game's bounded population and incident needs.

## Consequences

The change requires new driver lifecycle and conflict state, targeted pedestrian combat, controller validation, deterministic fixtures and separate traffic CPU measurements. It must pass the proposal's jitter, collision, feasible-recovery, impossible-blockage and ownership tests before ADR-0004 can be superseded. Personality and planning parameters remain data-driven. Tow content is a subsequent delivery stage, with no invisible clearance fallback in the meantime.
