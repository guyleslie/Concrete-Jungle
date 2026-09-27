# City

The procedurally generated island city: its street grid, blocks, buildings, special structures, street furniture and traffic signals.

Source: `src/city_map.h`, `src/city_map.cpp`; layout constants in `src/config.h`.

## Contents

- [Layout](#layout)
- [Block types](#block-types)
- [Buildings](#buildings)
- [Special structures](#special-structures)
- [Street furniture](#street-furniture)
- [Traffic signals](#traffic-signals)
- [Surfaces](#surfaces)
- [Queries](#queries)

## Layout

The city is generated from a fixed seed (`20260926`), so every run produces the same map.

| Element | Size |
|---|---|
| Tile | 64 px (4 m) |
| Block pitch | 16 tiles (64 m): a 2-tile street (one lane each way), a 1-tile sidewalk, a 12-tile interior, a 1-tile sidewalk |
| Grid | 9 × 9 blocks, closed by an extra street on the east and south edges |
| Island | 146 × 146 tiles ≈ 584 × 584 m, surrounded by a seawall and water |

Every tile is one of `Road`, `Sidewalk`, `Lot`, `Grass`, `Parking` or `Plaza`. Sidewalks and block interiors are raised by a 7.5 cm kerb.

## Block types

| Type | Count | Contents |
|---|---|---|
| Buildings | the rest | Office and apartment buildings (see below). The 3 × 3 blocks around the centre stay built-up and are the tallest. |
| Plaza | 4 + the central square | Fountain, a ring of planters with trees, benches facing the fountain, a café terrace, lamps, shrubs |
| Park | 7 | Fountain, cross-shaped paths lined with shrubs, trees, benches, lamps, picnic tables, rocks |
| Parking | 6 | Three double rows of 2.5 × 5 m bays, lamps, a phone booth, shrubs |
| Police station | 1 | Station building with a helipad, forecourt parking for police cars |
| Hospital | 1 | Hospital building with a helipad, forecourt parking for ambulances |

The police station and hospital entrances are the respawn points after an arrest or death.

## Buildings

Building blocks are subdivided recursively into lots (up to three levels, lots no smaller than about 22 m); 35 % of splits leave a 2.5 m service alley. Each leaf lot gets a building inset by 0.4 m, or occasionally a small courtyard with a tree and a dumpster.

| Property | Rule |
|---|---|
| Height | 2–5.5 storeys × a height multiplier that reaches 1.9 downtown; clamped to 2–8 storeys of 3.3 m, plus a parapet |
| Facade | Brick or glass; tall buildings are mostly glass. Night-time window lighting comes from a matching emissive mask |
| Details | Rooftop AC units, vents and water tanks; neon strips on some brick buildings; aviation beacons on the tallest |

Buildings are axis-aligned boxes and solid for vehicles, people and bullets.

## Special structures

| Structure | Description |
|---|---|
| Gate buildings | Four buildings span north–south streets 5.8 m above the road; traffic drives underneath. Arcade columns stand on the sidewalks. |
| Skybridges | Four glass bridges cross east–west streets 12 m up. |
| Elevated metro | A closed loop 7.5 m above the streets, carried by portal frames whose columns stand on the sidewalks, so the lanes underneath stay free. A three-carriage train runs on it at up to 24 m/s. |

Gate buildings, skybridges and the metro deck are not collision obstacles; the arcade and metro columns are.

## Street furniture

Every sidewalk side of every block is furnished along the kerb:

| Item | Placement |
|---|---|
| Street lamps | Every fourth tile |
| Street trees | On 55 % of streets, between the lamps |
| Props | Trash bins, benches, newspaper boxes, mailboxes, phone booths, hydrants and shrubs at random |
| Bus shelter | On about 30 % of blocks |
| Bollards | At the four corners of each block |
| Drains and manholes | In the gutter and on the road (decoration) |
| Traffic signals | At every corner of every junction |

How each object reacts when a vehicle hits it — rigid, breakaway, or soft — is defined in `ApplyMaterial` and documented in [Physics › Street furniture](physics.md#street-furniture). Most objects collide as circles; benches, picnic tables, bus shelters, dumpsters and planters collide as rotated boxes.

Knocked-over lamp and signal posts stay on the ground as decoration; broken hydrants keep gushing water.

## Traffic signals

Each junction runs a 16-second cycle with a random offset:

| Phase | North–south traffic | East–west traffic |
|---|---|---|
| 0–6 s | Green | Red |
| 6–7.5 s | Yellow | Red |
| 7.5–8 s | Red | Red |
| 8–14 s | Red | Green |
| 14–15.5 s | Red | Yellow |
| 15.5–16 s | Red | Red |

Pedestrians cross only when the green phase has more than 3 s left (see [Pedestrians](pedestrians.md)).

## Surfaces

`CityMap::Grip` returns the tyre grip of the surface under a point: grass 0.55, plaza paving 0.9, everything else 1.0. Anything below 0.8 counts as off-road for the handling model (see [Vehicles](vehicles.md#longitudinal)).

## Queries

Buildings and solid objects are indexed per tile, so spatial queries only touch nearby items.

| Query | Use |
|---|---|
| `QueryBuildings`, `QueryObjects` | Candidates near an area (collision, AI) |
| `PointInBuilding`, `AreaFree` | Spawn and exit placement |
| `LineOfSight` | Police spotting the player |
| `RayCast` | Bullets against buildings and objects |
| `SignalState`, `GreenTimeLeft` | Traffic and pedestrians at junctions |

A pre-rendered minimap (2 px per tile) is built once after generation.
