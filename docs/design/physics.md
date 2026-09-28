# Physics

Vehicle collision detection and response: how vehicles touch each other and the world, and what a crash does to them. The engine, brake and tyre forces that move a vehicle between collisions are described in [Vehicles](vehicles.md).

Source: `src/physics.h`, `src/physics.cpp`; crash consequences in `Game::HandleImpacts` (`src/game.cpp`); object materials in `ApplyMaterial` (`src/city_map.cpp`). The reasoning behind the design is recorded in [ADR-0005](../adr/0005-vehicle-contact-solver.md).

The [CJ-002 proposal](vehicle-handling-proposal.md#collision-acceptance) defines additional collision measurements for review. It does not change the implemented solver or supersede ADR-0005.

## Contents

- [Goals](#goals)
- [Frame step](#frame-step)
- [Shapes and contacts](#shapes-and-contacts)
- [Solver](#solver)
- [Traffic on rails](#traffic-on-rails)
- [Street furniture](#street-furniture)
- [Crash consequences](#crash-consequences)
- [Tuning reference](#tuning-reference)
- [Diagnostics](#diagnostics)
- [References](#references)

## Goals

- Vehicles never sink into walls, tunnel through thin poles or jitter while resting against something.
- A vehicle wedged between several obstacles settles instead of being pushed back and forth.
- Crashes transfer momentum plausibly: mass matters, crumple zones absorb energy, cars slide and spin.
- Street furniture reacts like its real counterpart: breakaway posts fall over, bollards stop you.
- Physics reports what happened; game rules decide what it means.

## Frame step

`VehiclePhysics::Step(game, dt)` runs once per frame:

1. **Bodies.** Every active vehicle becomes a body. Rail cars (see [below](#traffic-on-rails)) are rewound to their frame-start pose and marked kinematic; everything else is dynamic with the class mass and the moment of inertia of a solid rectangle.
2. **Collide.** Contacts are generated once, at the frame-start poses, for vehicle–vehicle, vehicle–building, vehicle–object and vehicle–seawall pairs.
3. **Knock pre-pass.** A contact that makes a rail car hit a dynamic body with a closing speed above `KNOCK_SPEED` converts the rail car into a dynamic body before solving, so both exchange momentum.
4. **Prepare.** Effective masses and the closing velocity of every contact point are computed; restitution is chosen from the closing speed.
5. **Sub-steps.** The frame is split into sub-steps of about 1/240 s (at most 8). Each sub-step:
   1. applies engine, brake and tyre forces to dynamic bodies (`VehicleForces`),
   2. applies shrub drag,
   3. warm-starts the contacts with the previous sub-step's impulses,
   4. solves all contacts twice with push-out enabled,
   5. integrates positions (rail cars are interpolated along their path),
   6. solves all contacts once more without push-out (*relax* pass).
6. **Restitution.** A single pass adds bounce to contacts that closed faster than `RESTITUTION_MIN`.
7. **Results.** Broken objects are removed, rail cars that kept pushing are knocked off their rail, and one `ImpactEvent` per touching contact is emitted.

## Shapes and contacts

| Body | Shape |
|---|---|
| Vehicle | Oriented box, sprite width × class length |
| Building | Axis-aligned box (only solid buildings; gate buildings and skybridges can be driven under) |
| Street furniture | Circle, or oriented box for box-shaped furniture (`CityObject::box`) |
| Seawall | Four half-planes around the island |

**Box vs box** uses the separating axis test to find the axis of least penetration, then clips the incident face against the side planes of the reference face. This yields up to two contact points with individual separations, so a car resting flat against a wall is supported at both corners instead of rocking around one averaged point. The reference face prefers the first box unless the second is clearly better, which stops the contact normal from flipping between frames.

**Box vs circle** uses the closest point on the box; when the circle's centre is inside the box, the normal follows the shallowest face.

**Speculative contacts.** A contact is created while the shapes are still up to a margin apart. The margin is the distance the pair can close within the frame (relative speed plus rotation, × 1.2, clamped to 2–48 px). The solver lets such a contact close exactly its gap and no more, which prevents tunnelling without continuous collision detection.

Contact anchors are stored relative to each body, and the separation is re-evaluated from the bodies' current poses in every sub-step, so contacts stay accurate while bodies move within the frame.

## Solver

The solver follows the sequential-impulse method with the soft-step refinements of Box2D v3.

| Element | Behaviour |
|---|---|
| Normal impulse | Accumulated per contact point and clamped to be non-negative (contacts push, never pull). |
| Push-out | Overlap is removed by a soft constraint (30 Hz, damping ratio 10; twice as stiff against immovable bodies) and never faster than `MAX_PUSH` = 3 m/s. Nothing is teleported. |
| Relax pass | After integrating positions, one more solve without push-out removes the push-out velocity, so resolving overlap does not launch bodies. |
| Friction | Coulomb friction, bounded by the friction coefficient times the normal impulse. |
| Restitution | Applied once after all sub-steps, only for contacts that closed faster than 1 m/s: *e = 0.05 + 0.30 · exp(−v / 120 px/s)*. A parking knock bounces (*e* ≈ 0.3), a real crash hardly does (*e* ≈ 0.1 at 50 km/h). |
| Iterations | Two solve iterations and one relax iteration per sub-step, warm-started from the previous sub-step. |

Friction coefficients: 0.35 between vehicles, 0.45 against walls and the seawall, 0.3 against street furniture.

## Traffic on rails

Undisturbed traffic is not simulated by the solver: it follows its lane kinematically (see [Traffic](traffic.md) and [ADR-0004](../adr/0004-kinematic-rail-traffic.md)). In the solver a rail car is an infinitely heavy moving body whose motion over the frame is known in advance; it is interpolated along that motion in every sub-step. Rail cars ignore buildings and street furniture and never collide with each other.

A rail car must never act as a bulldozer, so it is converted into a normal dynamic body when:

- a contact makes it close on a dynamic body faster than `KNOCK_SPEED` (60 px/s ≈ 13 km/h) — handled before solving, so the crash itself is physical; or
- it keeps pushing on a dynamic body for more than `KNOCK_SHOVE_TIME` (0.35 s).

The driver then brakes and later rejoins the lane — see [Traffic › Knocked off the lane](traffic.md#knocked-off-the-lane).

## Street furniture

Every object has a breakaway **strength**: the impulse (tonnes × px/s) its contact can take before it gives way. The solver caps the contact's total impulse at that strength; if the cap is reached, the object breaks, disappears from collision, and the vehicle additionally loses the momentum carried away by the object's loose **mass**. A 1-tonne car therefore loses at most *strength* px/s of speed when it breaks something.

| Object | Strength | Loose mass (t) | Result |
|---|---|---|---|
| Traffic cone | 1 | 0.004 | Knocked away |
| Trash bin | 10 | 0.02 | Knocked away |
| Crate | 12 | 0.03 | Smashed |
| Barrel | 14 | 0.06 | Knocked away |
| Newspaper box | 25 | 0.04 | Knocked away |
| Mailbox | 45 | 0.06 | Knocked away |
| Picnic table | 60 | 0.08 | Smashed |
| Bench | 70 | 0.06 | Smashed |
| Hydrant | 90 | 0.12 | Shears off; water gushes |
| Lamp post | 170 | 0.15 | Falls over in the push direction; the light goes out |
| Signal post | 260 | 0.25 | Falls over; the signal goes dark |
| Phone booth | 260 | 0.30 | Glass shatters |
| Bus shelter | 300 | 0.40 | Glass shatters |
| Steel bollard | 600 | 0.10 | Stops a car; only a heavy or very fast vehicle breaks it |
| Tree trunk, concrete planter, dumpster, rock, pillar, fountain, building | rigid | — | Stops the vehicle |

**Shrubs** are soft: they create no contact. A vehicle inside a shrub is slowed by drag (stronger for larger shrubs), and a vehicle whose momentum (mass × speed) exceeds 170 flattens it.

Bus shelters are solid for vehicles but people can walk into them (`CityObject::walkIn`). Fallen poles are decoration only.

## Crash consequences

Physics emits an `ImpactEvent` for every contact that transferred impulse: the two bodies, the contact point and normal, the closing speed at first touch, each body's **delta-V** (velocity change caused by the contact), the sliding speed while pressed together, and whether an object broke. `Game::HandleImpacts` applies the game rules:

| Rule | Condition | Effect |
|---|---|---|
| Vehicle damage | delta-V above `DV_DAMAGE_FREE` (90 px/s ≈ 20 km/h) | `(delta-V − 90) × DV_DAMAGE_K` health lost |
| Driver injury | player's vehicle, delta-V above `DV_INJURY` (450 px/s ≈ 100 km/h) | `(delta-V − 450) × 0.05` player health lost |
| Rider ejection | motorbike, delta-V above `DV_BIKE_THROW` (170 px/s ≈ 38 km/h) | The rider is thrown over the bars and hurt; the bike is left without a rider |
| Impact effects | closing speed above 90 px/s | Crash sound scaled by speed; sparks above 160; glass and body debris above 330; camera shake for the player |
| Scraping | sliding above 90 px/s while pressed together | Sparks along the contact; quiet grinding sound for the player |
| Driver reaction | traffic hit by the player | The driver panics |

Damage by delta-V is the standard crash-severity measure. It makes mass matter automatically: hitting a parked car of the same mass at 60 km/h costs each car about as much as hitting a wall at 30 km/h, and a bus barely notices a hatchback. What happens once a vehicle's health runs out is described in [Vehicles › Damage, fire and explosions](vehicles.md#damage-fire-and-explosions).

## Tuning reference

| Constant | File | Value | Effect |
|---|---|---|---|
| `SUBSTEP_HZ` | `physics.h` | 240 | Sub-step rate (max 8 sub-steps per frame) |
| `CONTACT_HZ`, `CONTACT_DAMPING` | `physics.h` | 30, 10 | Stiffness and damping of the soft push-out |
| `MAX_PUSH` | `physics.h` | 48 px/s | Fastest overlap resolution |
| `RESTITUTION_MIN` | `physics.h` | 16 px/s | No bounce below this closing speed |
| `KNOCK_SPEED`, `KNOCK_SHOVE_TIME` | `physics.h` | 60 px/s, 0.35 s | When a rail car becomes a physics body |
| `CarRestitution` | `physics.cpp` | 0.05 + 0.30 · exp(−v/120) | Bounciness by closing speed |
| Friction coefficients | `physics.cpp` | 0.35 / 0.45 / 0.3 | Vehicle / wall / furniture |
| `DV_DAMAGE_FREE`, `DV_DAMAGE_K` | `game.cpp` | 90 px/s, 0.13 | Damage from delta-V |
| `DV_INJURY`, `DV_BIKE_THROW` | `game.cpp` | 450, 170 px/s | Driver injury and rider ejection thresholds |
| Object strength and mass | `city_map.cpp` (`ApplyMaterial`) | see [table](#street-furniture) | Breakaway behaviour |

## Diagnostics

The `crash` and `derby` test scenarios measure jitter (position or heading reversing frame after frame), penetration into the static world, stuck events and traffic knock/recovery counts, and `crash` logs every impact with its delta-V. See [Testing](../testing.md).

## References

- Erin Catto, [Fast and Simple Physics using Sequential Impulses](https://box2d.org/files/ErinCatto_SequentialImpulses_GDC2006.pdf), GDC 2006.
- Erin Catto, [Solver2D](https://box2d.org/posts/2024/02/solver2d/) — soft step, relax iterations, sub-stepping.
- [Box2D documentation: Simulation](https://box2d.org/documentation/md_simulation.html) — defaults for restitution threshold, contact stiffness and sub-steps.
- Allen Chou, [Game Physics: Contact Constraints](https://allenchou.net/2013/12/game-physics-resolution-contact-constraints/).
