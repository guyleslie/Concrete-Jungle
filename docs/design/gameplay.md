# Gameplay

The player, weapons, the wanted level and police response, missions, pickups and money.

Source: `src/game.h`, `src/game.cpp`; weapon definitions in `assets/data/weapons.cfg`.

## Contents

- [Player on foot](#player-on-foot)
- [Weapons](#weapons)
- [Vehicles](#vehicles)
- [Health and armour](#health-and-armour)
- [Wanted level](#wanted-level)
- [Police response](#police-response)
- [Busted and wasted](#busted-and-wasted)
- [Missions](#missions)
- [Pickups and money](#pickups-and-money)

## Player on foot

- Walks at 4 m/s and runs at 6.5 m/s (Shift); reloading slows the player to 60 %.
- The character turns to face where it walks. Holding the right mouse button, or attacking, turns the torso towards the cursor while the legs keep walking — strafing sideways or backpedalling.
- The player collides with buildings, solid furniture and vehicles; a vehicle hitting the player faster than about 25 km/h hurts.

## Weapons

Weapons are defined in `assets/data/weapons.cfg` (format: [Adding content › Weapons](../guides/adding-content.md#weapons)); the number keys select them in file order. The shipped set is fists, knife, pistol, shotgun, rifle and flashlight. The player starts with fists and a loaded pistol.

| Mode | Behaviour |
|---|---|
| Melee | Hits people in front of the player; can knock them down (knife 20 %, others 45 % per hit). Vehicles take 15 % of the damage. |
| Semi-automatic / automatic | Each shot (or pellet) is a ray against buildings, street furniture, vehicles and people. Vehicles take 55 % of the damage and sometimes lose glass; breakable props such as hydrants and bins break. |

Firing scares pedestrians within 34 m and, below one star, raises the wanted level slightly. An empty clip reloads automatically when the player fires again; R reloads manually.

## Vehicles

- **Enter** any drivable vehicle within 3.8 m (E, F or Enter). An occupied traffic car or police car is **hijacked**: the driver is thrown out and runs away.
- **Exit** the same way. Jumping out faster than about 56 km/h hurts.
- The vehicle model, damage and crash consequences are described in [Vehicles](vehicles.md) and [Physics](physics.md#crash-consequences). Very hard crashes injure the driver; motorbike riders can be thrown off.

## Health and armour

The player has 100 health. Armour absorbs up to 70 % of each hit until it is used up. Health reaching zero means **Wasted**.

## Wanted level

The wanted level (`heat`) runs from 0 to 6; the number of stars is its value rounded up.

| Crime | Heat |
|---|---|
| Firing a gun (only below one star) | +0.12 |
| Hurting a person | +0.3 (+0.35 if knocked down) |
| Killing a person | +1.0 |
| Hijacking a car | +0.5 |
| Stealing a police car | +2.0 |
| Damaging a police car | +0.25 |
| Blowing up a vehicle you damaged | +0.8 |

The police notice the player when they have line of sight within about 40 m. After 10 s without being seen, the heat drops by 0.12 per second — roughly one star every 8 s.

## Police response

| Aspect | Rule |
|---|---|
| Units | Stars + 1 police cars, at most 6. A new car is dispatched every 2.5 s until there are enough, 56–100 m away and off-screen. |
| Driving | See [Traffic › Police](traffic.md#police). |
| Arrest | A slow police car (below 90 px/s) right next to a slow player — below 45 px/s in a vehicle, 60 px/s on foot — for 2.5 s in a vehicle or 1.2 s on foot. |
| Stand-down | Police cars leave once the heat is zero and they are far away and off-screen. |

## Busted and wasted

The world keeps running in slow motion for four seconds, then the player respawns with full health, no armour and no wanted level. A running mission fails.

| Outcome | Respawn | Penalty |
|---|---|---|
| Busted | Police station | 25 % of the money; all guns confiscated |
| Wasted | Hospital | A hospital bill of up to $500 |

## Missions

A pay phone starts ringing a few seconds after the previous mission ended: the one nearest to the player (but at least 25 m away), marked in yellow. Walking or driving up to it starts a mission.

| Mission | Objective | Time | Reward |
|---|---|---|---|
| Hot Package (courier) | Reach a drop-off 140–375 m away | Travel time at 14 m/s + 25 s | $300 plus a distance bonus |
| Scrap Metal (demolition) | Destroy a marked traffic car | 150 s | $1,200 |
| Special Delivery (steal) | Steal a marked parked car and deliver it to the garage, arriving slowly | 200 s | $1,600, scaled by the car's condition (40–100 %) |

A mission fails when its timer runs out, when its target vehicle disappears (or, for a delivery, is wrecked), or when the player is busted or wasted.

## Pickups and money

| Pickup | Where | Amount |
|---|---|---|
| Health | Outside the hospital; random sidewalks | +60 / +50 |
| Armour | Outside the police station; random sidewalks | +60 / +50 |
| Weapons | Random sidewalks | The weapon, or ammunition if already owned |

There are 24 random pickups; a collected pickup returns after 60 s. Money comes from missions and from blowing up vehicles ($150 each).
