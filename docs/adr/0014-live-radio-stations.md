# ADR-0014: Live radio stations, one decoder each

- **Status:** Accepted; amended by [ADR-0015](0015-radio-song-excerpts.md) (songs aired as excerpts, sought on a worker thread)
- **Date:** 2026-10-12

## Context

[CJ-040](../backlog.md#cj-040-car-radio) adds a car radio: stations of songs, DJ talk and commercials that play when the player gets into a car, and the muffled music of passing cars. The user chose a live broadcast: the player joins a station where it is, as in GTA III and later, rather than at the start of a song. A passing car on the same station then plays the same moment as the player's own radio would.

A live broadcast needs a station's sound at an arbitrary point of a clip. Measured with raylib 6.0 on the development machine, a jump into a three-minute song costs 325 ms on average and 660 ms at worst in MP3 (dr_mp3 decodes from the start), 8 ms in OGG Vorbis and 2–3 ms in QOA, which is twice the size of MP3. Decoding all the time costs about 0.5 % of a core per station in any of the three formats. raylib reports neither the device's sample rate nor any stream's sound back to the game, and a stream processor is a plain function without a context pointer.

## Decision

- **Every station plays all the time.** From the first frame of play each station decodes its programme, whether anyone hears it or not, and the player and every car tuned to it only take its sound. Nothing ever jumps into a clip during play, so the clips stay MP3, as delivered, without a second lossy encoding.
- **One decoder and one processor per station.** A station has a single raylib music stream for its current clip. A stream processor, one function per station slot (at most eight stations), makes everything the player hears of it from that one signal: the player's own radio as it is, and up to four traffic radios. A traffic radio is read from a ring buffer of the station's signal, late by the sound's travel time, so the Doppler shift of a passing car follows from the changing distance. It goes through the closed body of its car, and a second body when the player sits in a car, as second-order low-pass filters, with a gain and a constant-power pan.
- **The main thread decides, the audio thread follows.** Once a frame the main thread sets each voice's targets (gain left and right, travel time, the second body) in atomics; the processor moves towards them smoothly (30 ms for gains, 50 ms for the travel time), so nothing clicks.
- **The device's sample rate is measured.** A processor on the final mix counts the frames mixed; after a second the nearest standard rate is taken. Clip ends are counted in device frames, so the next clip starts within one frame of the last one's end instead of after raylib's padding of up to 267 ms.
- **The programme has its own random generator** and a car's radio is drawn from its serial, so the radio never moves the gameplay generator ([CJ-037](../backlog.md#cj-037-audio-randomness-apart-from-gameplay)).

## Alternatives considered

- **A station clock, decoding only the stations heard.** The industry's usual approach, but every tune-in would jump into a clip: a visible hitch with MP3, or the clips converted to QOA (twice the size) or re-encoded as OGG.
- **A separate stream for the player's radio and for each passing car.** The streams would drift apart, every new car would jump into the clip, and a station heard from inside and outside at once would decode twice.
- **raylib's pitch and pan per stream for a passing car.** A pitch shift would make the car's copy run ahead of or behind the broadcast, and raylib's pan has no filter or delay.
- **Recording the outside sound with the effect baked in.** Static: no distance, no direction, no Doppler.

## Consequences

- A station costs about 0.5 % of a core and one decode of about 1.3 ms every 267 ms on the main thread, whether or not anyone listens. Eight stations would cost about 4 % of a core; beyond that, or with clips in a format that seeks quickly, a station clock would pay off.
- At most eight stations, four traffic radios at a time per station.
- The processor runs at the device's rate, so the filters and the travel time are exact only after the first second of measuring (48 kHz is assumed until then).
- The measurement hooks (forced radios, a recording of the mix and of one station) live in the radio module; the [radio scenes](../testing.md#radio-scenes) use them.
