# ADR-0007: Dynamic axle-based vehicle handling

- **Status:** Proposed
- **Date:** 2026-09-27

## Context

The current arcade model sets a target yaw rate and removes lateral velocity directly. Acceleration and braking are several times real-world magnitudes, class differences are weak, and tyre forces can obscure collision response. [CJ-002](../backlog.md#cj-002-vehicle-handling-model) calls for believable handling, distinct classes and more carefully verified impacts. The user must approve the [specification and class targets](../design/vehicle-handling-proposal.md) before implementation.

## Decision

Propose a dynamic bicycle model with front/rear slip angles, load transfer, a shared longitudinal/lateral tyre friction budget, force/power-limited acceleration, wheel-angle steering and axle-specific braking. Class parameters remain data-driven. A stable low-speed treatment must resist creep without overriding impact spin.

Preserve the accepted 2D simulation, world scale, kinematic traffic and sub-stepped contact solver from ADRs 0001–0005. Tyre forces change velocities; the contact solver integrates positions. Both use the same mass properties. Two-wheelers use a simplified planar, lean-limited model; high-sided vehicles receive conservative cornering limits. Full suspension, rollovers and articulated trailers are outside this decision.

Add isolated handling and prescribed-speed collision fixtures before changing production physics, and freeze those fixtures for the baseline and verification. Keep the existing city scenarios for integration regression. Revisit collision tuning only against recorded outcomes, preserving the solver's accumulated impulses, bounded push-out and relaxation. Any departure from ADR-0005's contact-generation contract requires an explicit amendment to this proposal before implementation.

## Alternatives considered

- **Retune the arcade yaw controller:** cheap, but still makes steering prescribe body rotation and does not give braking and cornering a physical shared grip limit.
- **Integrate Box2D:** a proven 2D contact engine, but replacing the existing solver is unnecessary for tyre dynamics and reopens rail-traffic and breakaway integration already covered by ADR-0005.
- **Integrate Jolt vehicles:** useful reference implementation, but a full 3D suspension and collision dependency exceeds the planar simulation's needs.
- **Copy the iforce2d tutorial or Marco Monster article:** useful conceptual material; redistribution permission has not been established for the former and the latter reserves rights. The proposal links them without importing content.

## Consequences

- Mass, axle geometry and tyre force saturation make vehicle differences and impact response measurable.
- Realistic acceleration and braking require deliberate adaptation of consumers such as the rail-traffic speed controller and police controls.
- Heavy-vehicle masses will change collision delta-V under existing damage rules; damage-system redesign remains CJ-003.
- The proposal has no implementation or baseline results yet. Acceptance requires class metrics, collision fixtures, city regressions, screenshot inspection and a user playtest. Any copied permissively licensed code must retain licence notices and be recorded in CREDITS.md.
