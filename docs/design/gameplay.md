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

- Walks at 1.5 m/s, a real walking pace, and runs at 6.5 m/s (Shift); reloading slows the player to 60 %.
- The character turns to face where it walks. Holding the right mouse button, or attacking, turns it towards the cursor while the legs keep moving: within 45° of the aim they walk or run, within 135° they strafe (with Shift, a jogging strafe), beyond that they backpedal.
- The weapon is carried lowered and raised only to aim: while either mouse button is held and for 0.8 s after a shot. It is raised in 0.2 s and lowered in 0.25 s; the drawing switches half way.
- Drawn as two layers from 3D renders ([CJ-011 player art](../../assets/art/cj011/README.md), format: [Adding content › Player](../guides/adding-content.md#player)): the legs, played by the distance walked (walks, strafes and the backpedal) or by cadence (the run and the jogging strafes), and the upper body of the weapon's set, which shows the weapon lowered in step with the legs, the aim, the fire, reload and melee frames. Both layers face the aim and share the pivot.
- The player collides with buildings, solid furniture and vehicles; a vehicle hitting the player faster than about 25 km/h hurts.

## Weapons

Weapons are defined in `assets/data/weapons.cfg` (format: [Adding content › Weapons](../guides/adding-content.md#weapons)); the number keys select them in file order. The shipped set is fists, knife, pistol, shotgun, rifle and flashlight. The player starts with fists and a loaded pistol.

| Mode | Behaviour |
|---|---|
| Melee | Hits people in front of the player; can knock them down (knife 20 %, others 45 % per hit). About 15 % of people hit back (see [Pedestrians › Fighting back](pedestrians.md#fighting-back)). Vehicles take 15 % of the damage. |
| Semi-automatic / automatic | Each shot (or pellet) is a ray against buildings, street furniture, vehicles and people. Vehicles take 55 % of the damage and sometimes lose glass; breakable props such as hydrants and bins break. |

Firing scares pedestrians within 34 m (they run after their reaction time) and, below one star, raises the wanted level slightly. An empty clip reloads automatically when the player fires again; R reloads manually. Changing weapons or getting into a vehicle stops a reload, and its animation, without loading the clip; the number key of the weapon in hand does nothing. The shotgun reloads shell by shell: only the missing shells, 0.45 s each, then 0.4 s to rack the pump; each shell counts as soon as it is in, and stays in when the reload is stopped. Every shotgun shot is followed by racking the pump in the drawing. The flashlight's beam starts at the lens of the drawn lamp and points where it points, frame by frame (`LAMP` records): lowered it lights the ground a few metres ahead and swings with the steps, aimed it reaches 18 m, raised to strike it points at the sky and goes out, and the blow sweeps it down in front of the player.

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

The world keeps running in slow motion for four seconds, then the player respawns with full health, no armour and no wanted level. A running mission fails. Vehicles the player damaged before no longer count as the player's: a car the player set on fire that explodes after the respawn raises no heat and pays no reward, so the police do not come straight back.

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
