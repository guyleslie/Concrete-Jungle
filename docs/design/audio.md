# Audio

Every sound in the game is synthesised at start-up, so the game needs no audio files. Any sound can be replaced by a recording without code changes.

Source: `src/audio.h`, `src/audio.cpp`.

## Contents

- [Sound types](#sound-types)
- [Shouts](#shouts)
- [Positional playback](#positional-playback)
- [Replacing sounds](#replacing-sounds)

## Sound types

| Type | Sounds | How they are made |
|---|---|---|
| One-shots | pistol, shotgun, rifle, punch, knife, crash, crash_small, explosion, horn, door, pickup, mission_pass, mission_fail, wasted, splash, glass, footstep, reload, scream | Filtered noise bursts and tones with envelopes; five overlapping voices per effect |
| Voices | shout1 ("hey!"), shout2 ("oi!"), shout3 ("hah!") | A formant voice: see [Shouts](#shouts) |
| Loops | engine, engine_diesel, siren, skid, ambience | Seamless buffers (a whole number of cycles), streamed so their pitch and volume can change every frame |

The engine loop follows the player's vehicle: its pitch tracks a simulated rev counter and throttle, and large vehicles blend in the diesel loop. The siren loop follows the nearest active siren; the skid loop follows the player's tyre slip.

## Shouts

An angry driver yells (see [Traffic › Driver incidents](traffic.md#driver-incidents)). Each shout is a glottal pulse train at a raised pitch (190–225 Hz) that rises and falls over the shout, with a little jitter and shimmer, filtered by three formant resonators in cascade that glide from one vowel to the next ("e" to "i", "o" to "i", "a" to the neutral vowel), after an aspirated onset; a soft saturation gives the strain of a raised voice. Every person has a voice of their own: one of the three shouts, chosen from their identity, at a personal pitch of 0.86–1.12. The synthesis is a stand-in until recorded voices replace it ([CJ-012](../backlog.md#cj-012-audio-overhaul)); `shout1.wav`–`shout3.wav` in `assets/sounds/` override it.

## Positional playback

One-shots are played at a world position. Their volume falls off with the distance from the listener (the camera) and they are panned left or right.

## Replacing sounds

Drop a WAV file named after the sound into `assets/sounds/`, for example `assets/sounds/explosion.wav` or `assets/sounds/engine.wav`. A file found there overrides the synthesised version at start-up. See [Adding content › Sounds](../guides/adding-content.md#sounds).
