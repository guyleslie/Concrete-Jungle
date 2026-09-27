# ADR-0004: Kinematic "rail" traffic

- **Status:** Accepted
- **Date:** 2026-09-26 (recorded 2026-09-27)

## Context

The first traffic implementation drove every AI car through the physics model by steering and throttling it like a player. In practice the cars jittered while following their lanes and deadlocked at junctions.

## Decision

Undisturbed traffic follows its lane **kinematically**, as the GTA games do:

- each car keeps a smooth path along its lane, planned one junction ahead, and a distance along it;
- its pose is sampled from the path at the front and rear axle, so long vehicles sweep through turns;
- speed is controlled by look-ahead along the path (signals, junction rules, vehicles and people ahead).

Only when something disturbs a car — a hard hit, an explosion, or pushing on something — is it *knocked* into full physics. The driver then brakes and either rejoins the lane, drives back to it, or abandons the car.

## Alternatives considered

- **Physics-driven traffic with better controllers** — tried first; jitter and deadlocks.
- **Pure kinematics without knock-off** — stable, but crashes would look unphysical.

## Consequences

- Traffic cannot jitter or deadlock in physics and is cheap to simulate.
- Rail cars interact with physics bodies as infinitely heavy movers, so the solver must knock them off their rail before they push anything unrealistically ([ADR-0005](0005-vehicle-contact-solver.md)).
- Rejoining the lane is kinematic too, so it must check that the way back is free.
- Police cars, which must leave the lanes to chase, stay physics-driven.
- Do not return to physics-driven traffic AI without addressing the original jitter and deadlock problems.
