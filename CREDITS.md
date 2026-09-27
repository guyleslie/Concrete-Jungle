# Credits & asset sources

All third-party assets are free for commercial use. Only the Survivor sprites require attribution (CC-BY 3.0).

| What | Author | License | Source |
|---|---|---|---|
| Top-down vehicles (Audi, Viper, muscle car, taxi, pickup, van, ambulance, police, truck) | Unlucky Studio | CC0 | https://opengameart.org/content/free-top-down-car-sprites-by-unlucky-studio |
| Player character, "Animated Top Down Survivor Player" (body + feet animations) | Riley Gombart | **CC-BY 3.0** | https://opengameart.org/content/animated-top-down-survivor-player |
| Tree & shrub canopies, "Stylized Textures of Shrubs and Tree Tops" | FabinhoSC | CC0 | https://opengameart.org/content/stylized-textures-of-shrubs-and-tree-tops |
| Asphalt026C (roads) | ambientCG | CC0 | https://ambientcg.com/view?id=Asphalt026C |
| PavingStones130 (sidewalks) | ambientCG | CC0 | https://ambientcg.com/view?id=PavingStones130 |
| PavingStones107 (plazas) | ambientCG | CC0 | https://ambientcg.com/view?id=PavingStones107 |
| PavingStones131 (park paths) | ambientCG | CC0 | https://ambientcg.com/view?id=PavingStones131 |
| Concrete034 (lots, roofs, glass facades) | ambientCG | CC0 | https://ambientcg.com/view?id=Concrete034 |
| Gravel022 (gravel roofs) | ambientCG | CC0 | https://ambientcg.com/view?id=Gravel022 |
| Grass004 (parks) | ambientCG | CC0 | https://ambientcg.com/view?id=Grass004 |
| Bricks090 (brick facades) | ambientCG | CC0 | https://ambientcg.com/view?id=Bricks090 |
| Rajdhani font | Indian Type Foundry | SIL OFL 1.1 | https://fonts.google.com/specimen/Rajdhani |

## Derived and generated content

- **Unarmed player animations** (`assets/survivor/unarmed/`) are derived from the Survivor knife frames with the blade removed (`tools/make_unarmed.py`). They are under the same CC-BY 3.0 license as the source.
- **Limo, bus, box truck, fire truck and garbage truck** are built at start-up by stretching, combining and repainting the Unlucky Studio sprites (`DERIVE` / `COMPOSE` in `assets/data/vehicles.cfg`).
- **Generated in code by the game:**
  - motorbikes, scooters and choppers (placeholders until real top-down art is found)
  - civilian pedestrians
  - street furniture, the metro train and building window masks
  - light, flare and smoke textures
  - all sound effects

  The generators are in `src/sprite_gen.cpp`, `src/assets.cpp` and `src/audio.cpp`.

Required in-game attribution: *"Survivor" character by Riley Gombart (CC-BY 3.0)*. It is shown on the title screen.
