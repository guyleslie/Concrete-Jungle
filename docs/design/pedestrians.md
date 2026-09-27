# Pedestrians

How the people on the sidewalks behave. The player on foot is covered in [Gameplay](gameplay.md).

Source: `src/pedestrian.h`, `src/pedestrian.cpp`; spawning and recycling in `Game::UpdateSpawning` (`src/game.cpp`).

## Contents

- [States](#states)
- [Walking the city](#walking-the-city)
- [Avoidance](#avoidance)
- [Danger](#danger)
- [Population](#population)

## States

| State | Behaviour |
|---|---|
| `Walk` | Walks along its block's sidewalk ring from corner to corner, on a personal line so people don't all walk single file |
| `Wait` | Waits at the kerb for the green light, facing the other side |
| `Cross` | Crosses the street at the crossing, a little faster than walking |
| `Idle` | Stands still for 1–4 s |
| `Wander` | Walks to a random spot inside a park or plaza |
| `Flee` | Runs away from a threat |
| `Rejoin` | Pauses, then returns to the nearest point of its sidewalk ring |
| `Down` | Knocked down; gets up after a few seconds and flees |
| `Dead` | Lies on the ground until recycled |

## Walking the city

At each corner a pedestrian picks the next leg: occasionally a short idle, in parks and plazas sometimes a wander, in almost half of the cases a street crossing to the neighbouring block, otherwise the next corner of its own block.

To cross, it waits until its crossing direction shows green with more than 3 s left (see [City › Traffic signals](city.md#traffic-signals)), or gives up waiting after 25 s and crosses anyway.

## Avoidance

- Pedestrians steer around trees, lamp posts, bins and other solid furniture ahead of them, using each object's real shape (circles, or boxes for benches and similar furniture). Bus shelters can be walked into.
- They keep personal space: they sidestep people ahead and never overlap anyone, including the player.
- They collide with buildings and furniture.
- A pedestrian that keeps trying to move without making progress turns around or takes another route.

## Danger

| Trigger | Reaction |
|---|---|
| A vehicle heading at them faster than 120 px/s within about 9 m | Flee for 2.5 s |
| Gunfire, explosions, people being hurt nearby | Flee for a few seconds; some scream |
| Hit by a vehicle faster than 95 px/s | Knocked down and hurt; may die |
| Hit by weapons | Hurt; heavy hits knock them down |

Drivers of traffic cars usually ignore pedestrians on the sidewalk but stop for people in the road — unless they are distracted (see [Traffic](traffic.md#obstacles-and-impatience)).

## Population

220 pedestrians are spawned at start. Those more than about 175 m from the player, or dead for more than 25 s, are recycled to a random sidewalk point 44–150 m away and off-screen.
