# Asphalt Rock FM

The first radio station ([CJ-040](../../../docs/backlog.md#cj-040-car-radio)): 1980s American rock, instrumental, with an FM rock DJ and over-enthusiastic 1980s commercials in American-sounding gibberish. The project's author made every clip with [Suno](https://suno.com/) on a paid plan on 2026-10-11 and 2026-10-12, and owns them; see [CREDITS](../../../CREDITS.md#cj-040-asphalt-rock-fm). The station is defined in [`assets/data/radio.cfg`](../../data/radio.cfg).

## Clips

The files are Suno's MP3 exports (48 kHz stereo, 135–190 kbit/s), copied without re-encoding; the cover art and the tags were removed. Loudness is the integrated EBU R128 value of the file; `radio.cfg` gives each clip the gain to −16 LUFS (`tools/radio_gain.py`).

| File | Kind | Length | Loudness | Suno title | Suno ID |
|---|---|---|---|---|---|
| `song_01.mp3` | Song | 2:41 | −16.2 LUFS | Electric Guitar Riffs (1) | e28eda7f-ae26-4157-8004-5539a06229fc |
| `song_02.mp3` | Song | 2:58 | −15.8 LUFS | Electric Guitar Riffs (2) | dd6a05ae-dc25-435b-99fd-e6130e669bd3 |
| `song_03.mp3` | Song | 2:55 | −15.6 LUFS | Electric Guitar Riffs (3) | 674714d1-3fd9-4987-9112-8001d8374626 |
| `song_04.mp3` | Song | 2:31 | −15.3 LUFS | Electric Guitar Riffs (4) | 318a4d35-2476-4659-a19d-32be4d3e6bc9 |
| `song_05.mp3` | Song | 3:11 | −16.3 LUFS | Electric Guitar Riffs (5) | 1974754c-363d-4f0c-a874-c1c756c15825 |
| `song_06.mp3` | Song | 2:20 | −17.0 LUFS | Electric Guitar Riffs (6) | 927b828a-f8fd-42cc-945f-263889b3106c |
| `song_07.mp3` | Song | 3:18 | −14.9 LUFS | Electric Guitar Riffs (7) | 27ea138e-bd1b-4beb-8149-4dcb1e3bda60 |
| `song_08.mp3` | Song | 3:19 | −15.3 LUFS | Electric Guitar Riffs (8) | 8f868381-80ad-4f0e-8316-e56645abbab0 |
| `dj_01.mp3` | DJ | 5.2 s | −16.5 LUFS | 8 Second Audio Clip Of A Deep-voiced, Energetic 1980s American FM Rock Radio … | 2c580b5d-58ff-47d5-b064-dc611f16207b |
| `dj_02.mp3` | DJ | 9.1 s | −17.1 LUFS | Comin' atcha | ddfd7d0f-1dba-4dae-bb80-3c70bf03dda0 |
| `dj_03.mp3` | DJ | 12.4 s | −16.6 LUFS | Rabba-Dabba Zhoom | 6a0bb74b-9199-4655-a972-bac961a9168d |
| `ad_01.mp3` | Commercial | 4.8 s | −16.5 LUFS | 10 Second Vintage American Radio Commercial From 1987 … | 513ed45d-a0e4-439c-b238-0cfe29972885 |
| `ad_02.mp3` | Commercial | 5.2 s | −16.9 LUFS | 10 Second Vintage American Radio Commercial From 1987 … (second take) | 88df79a5-8f70-4e64-be14-c9ff821deb09 |
| `ad_03.mp3` | Commercial | 9.8 s | −17.5 LUFS | Gloopity Zap-Do! | 458742c1-d4e8-40d3-88f0-a898dddd0b31 |

The delivery also held `Rabba-Dabba Zhoom.mp3`, byte for byte the same file as `dj_03.mp3`; it was not added twice.

A station needs ten songs and ten talk clips ([Radio › Content of a station](../../../docs/design/radio.md#content-of-a-station)); two songs and four talk clips are still to come.
