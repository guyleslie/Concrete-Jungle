# CJ-030 loading measurements

This record separates the frozen startup baseline from verification of the [loading-screen implementation](../design/loading-screen-proposal.md). Baseline and implemented-build measurements were collected on 2026-10-09. Automated checks pass; actual startup and title handoff await final user review. The limitations below distinguish measured behavior from untested hardware and input cases.

## Contents

- [Frozen baseline](#frozen-baseline)
- [Work accounting and duration adaptation](#work-accounting-and-duration-adaptation)
- [Loading test interface](#loading-test-interface)
- [Verification checklist](#verification-checklist)
- [Implemented-build evidence](#implemented-build-evidence)
- [Matched startup times](#matched-startup-times)
- [Responsiveness and resource cost](#responsiveness-and-resource-cost)

## Frozen baseline

The local evidence is retained under `build/cj030/before/`: `ConcreteJungle.exe`, `manifest.json`, `run-1.log` through `run-5.log`, five title screenshots and three loading screenshots. Build output is ignored by Git; these paths describe local evidence rather than distributed assets.

| Property | Recorded value |
|---|---|
| Source commit | `7a1cc99c51f4ad035b6ba49c60989190d91231eb` |
| Source/content fingerprint | `manifest.json`: SHA-256 entries for 50 source files and 580 asset files |
| Frozen executable SHA-256 | `861d3cb70ab153a66cf6475e2b5f1f0bd32d29c799aa64dc0fa90d2aee37c637` |
| Instrumentation | Measurement-only phase timings and initialized-state digest in `main.cpp`; startup order unchanged |
| Baseline command | `--shot build/cj030/before/title-N.png --frames 1 --scenario title --uncapped` |
| Window / display | 1,600 × 900 px / 1,920 × 1,080 px |
| Graphics reported by raylib | AMD Radeon(TM) Graphics; OpenGL 3.3 core; raylib 6.0, GLFW on Win32 |

Phase durations below come directly from `STARTUP phase` and `STARTUP total` log lines. They measure elapsed startup work, not loading-screen draw CPU cost.

| Run | Assets (ms) | Traffic turns (ms) | World (ms) | Total (ms) |
|---|---:|---:|---:|---:|
| 1, first observed with loading captures | 9,568.675 | 1,673.872 | 1,376.005 | 12,618.658 |
| 2, repeated | 8,509.913 | 887.205 | 538.397 | 9,935.592 |
| 3, repeated | 8,125.190 | 871.372 | 509.354 | 9,505.961 |
| 4, repeated | 8,030.544 | 844.830 | 510.493 | 9,385.925 |
| 5, repeated | 8,357.198 | 902.573 | 524.491 | 9,784.336 |

Runs 2–5 have a total range of 9,385.925–9,935.592 ms and median of 9,645.149 ms. Run 1 includes three loading PNG exports and is kept separate. It is not a controlled cold-start measurement: OS caches were not cleared, and CPU model and other running workloads were not recorded in this manifest. Use the frozen executable again for matched before/after pairs rather than interpreting these observations as a portable performance guarantee.

All five runs reported initialized-state digest `26cd87aeff4216b3`, gameplay RNG state `54469b23`, 91 vehicles, 300 pedestrians, 338 buildings and 4,266 objects before the normal title update. Every asset summary reported 17 vehicle classes, 42 vehicle skins, 28 civilian atlases, 17 trees, 12 bushes, 6 character animation sets and 6 weapons. These are measurements of this content snapshot, not hardcoded progress totals or permanent expectations for future content.

## Work accounting and duration adaptation

[startup_loading.h](../../src/startup_loading.h) and [startup_loading.cpp](../../src/startup_loading.cpp) own the frozen task registry. Overall confirmed completion is `sum(weight × fraction) / sum(weight)`; the bar and integer percentage use that same fraction. Only top-level weights enter the denominator. Child obligations freeze when their parent activates at zero progress, and share that parent's existing weight. Registration or child-plan mutation after progress is a contract failure, with no title handoff.

Each subsystem derives its local count from the records, frames or generation obligations it actually executes. A “records processed” count can include a policy-skipped record; it does not claim that many textures were loaded. Frame decoding, atlas packing, target preparation, city construction and population attempts use their own qualified units. `Pulse` permits a redraw and cancellation check without awarding completion. `ReadyWithFallback` and legitimate optional `Skipped` outcomes resolve work according to the existing resource policy; failure and cancellation do not.

[startup_plan.cpp](../../src/startup_plan.cpp) estimates current work from content metadata, image sizes, configuration and screen size. Without history, each task's current units multiply a coarse built-in unit-cost estimate, so added content changes its initial budget immediately. It optionally reads `build/cache/startup-work-profile.cfg`, whose lines contain `task-id work-units elapsed-milliseconds` from the last successful startup. A known task's next weight is its previous duration multiplied by `current units / previous units`, with finite positive bounds. Missing or invalid history uses the current units and built-in unit cost. The cache is optional and may fail to write in a read-only installation; it never establishes readiness, schedules work or introduces a wait. Freeze the resulting weights for each run. The weighting estimates the distribution of work; actual completion always controls progress, regardless of startup duration.

[assets/data/loading.cfg](../../assets/data/loading.cfg) controls cosmetic paths, colors and presentation strings. It cannot create work, change dependency order, supply item totals or set stage percentages. Optional loading art/fonts have a primitive fallback. The accepted city image is copied unchanged to `assets/ui/loading-city.png`; the separate wordmark is `assets/ui/loading-logo.png`.

The final gate checks the usable world and required graphics/resources after all declared tasks resolve. Until it succeeds, progress stays below 100%. Required composite locations are `lightMap`, `emissive`, `bloomTex` and `bloomStrength`; blur requires `dir`. The unused composite `time` uniform may legitimately return −1 after shader optimization. Preserve group-specific missing-image skips and whole-group procedural fallbacks instead of treating every missing file as fatal.

## Loading test interface

The following interface is implemented and exercised by the recorded checks. Keep paths relative to the game's working directory, as with ordinary [screenshot tests](../testing.md#running-a-scenario).

| Option | Intended verification |
|---|---|
| `--loading-test <relative-dir>` | Dedicated presentation/reporter replay fixture with layout resolutions, more categories, long labels, unknown work, fallback, failure and readiness snapshots; headless model tests separately cover input/accounting contracts |
| `--loading-capture <relative-prefix>` | Real startup snapshots in a TSV log plus representative PNGs from actual task progress and terminal state |
| `--loading-config <cfg>` | Cosmetic configuration override, including missing-image/font fallback cases |
| `--loading-stop <taskid>` | Inject cancellation after confirmed work in the selected task, exercising partially initialized owners |
| `--loading-fail <taskid>` | Inject fatal startup failure after confirmed work in the selected task; verify no Ready or title handoff |

Examples for the implemented build:

```powershell
./ConcreteJungle.exe --loading-test build/cj030/layout
./ConcreteJungle.exe --loading-capture build/cj030/after/loading --shot build/cj030/after/title.png --frames 1 --scenario title --uncapped
./ConcreteJungle.exe --loading-config build/cj030/fallback.cfg --loading-capture build/cj030/fallback/loading --shot build/cj030/fallback/title.png --frames 1 --scenario title --uncapped
./ConcreteJungle.exe --loading-stop assets.animations --shot build/cj030/cancel/title.png --frames 1 --scenario title --uncapped
./ConcreteJungle.exe --loading-fail world.renderer --loading-capture build/cj030/failure/loading --shot build/cj030/failure/title.png --frames 1 --scenario title --uncapped
```

Loading fixtures and startup captures run before ordinary scenario frame counting. Normal `--shot` runs bypass the decorative title fade and retain the fixed simulation timestep. PNG exports have their own cost: use captures to review behavior and non-capture matched runs to compare startup duration.

## Verification checklist

The evidence below comes from executed model tests, visible game runs and exported images. Source inspection alone is not a passing result.

| Check | Evidence required | Status |
|---|---|---|
| Registry and counts | Unequal weights, nested child plan, unknown work, rejected late mutation, optional skip and rejected unresolved mandatory work | Passed: 49 model cases, 1,672 checks |
| Layout and scalability | Eight resolutions; fixed four rows; 5/25/100 groups; fitted labels/counts | Passed: 20 captures, zero fixture errors; visual review |
| Actual startup | State/count snapshot log, progress captures and required resource readiness | Passed: real task captures and Ready handoff |
| Fallbacks | Missing cosmetic files, ordinary content fallback, absent audio device when reproducible | Cosmetic and missing-content-config fallback passed; physical no-device case not reproduced |
| Partial cleanup | Cancel and fail at several assets/world boundaries; clean exit; no title screenshot, crash or duplicate resource unload | Passed: six injected runs, cleanup called twice |
| Input handoff | Independently held Enter/Space release/repress cases and fresh first title press | Seven production-used guard cases passed; physical keyboard/close review pending |
| Determinism | Exact initialized digest/RNG and fixed-frame title/day/drive outputs on matched content | Passed: five title pairs and six day/drive capture pairs |
| Responsiveness and cost | Bootstrap/full/matched time, draw CPU, boundary gaps, atomic maxima and GPU estimate | Measured below; indivisible-call exceptions reported |
| User review | Actual startup watched and title handoff playtested | Pending final acceptance |

Aim for a 30 Hz cooperative redraw/event cadence and safe CPU checkpoints within 50 ms on the measured machine. Record indivisible decode, font, shader, upload, audio and framebuffer calls separately: a cooperative presenter cannot preempt them. The draw CPU target is 0.5 ms at 1,920 × 1,080 px and the extra loading GPU budget is 16 MiB. The measurements below meet these presentation targets; they do not promise uninterrupted redraws inside blocking library calls.

## Implemented-build evidence

The measured executable is `build/cj030/after/ConcreteJungle.exe`, SHA-256 `ae5bb010c4e425be5e366aff94f2e2418f9d912910ce5481a7ec961cee71d0ea`. Its source/content hashes are in `build/cj030/after/manifest.json`. All source files compiled with GCC 15.2, C++17, `-O2 -Wall -Wno-missing-braces`; compilation produced no warnings. All 580 pre-existing asset hashes still match the frozen baseline. The additional loading configuration and two UI images are recorded separately in the implemented manifest. The approved background source and runtime copy have identical SHA-256, recorded in [the asset record](../design/loading-background.md).

| Run | Local evidence | Result |
|---|---|---|
| `build/cj030/model-tests.exe` | `build/cj030/model-tests.log`; source: [model_tests.cpp](../../tools/cj030/model_tests.cpp) | Exit 0; 49 cases, 1,672 checks, zero failures |
| `--loading-test build/cj030/layout-final` | `build/cj030/layout-final.log`, `cases.jsonl` and 20 PNGs | Exit 0; zero accounting/layout errors |
| `--loading-capture build/cj030/final/loading --shot build/cj030/final/title.png --frames 1 --scenario title --uncapped` | `build/cj030/final/startup.log`, task PNGs and snapshot TSV | Ready, title handoff; actual completion and qualified counts |
| `--loading-config build/cj030/inputs/missing-cosmetics.cfg --shot … --frames 1 --scenario title --uncapped` | `build/cj030/checks/fallback-cosmetics.log` and title PNG | Exit 0; all loading images/fonts missing; primitive fallback; exact healthy digest/RNG |
| Run from the isolated `build/cj030/fallback-content/` copy with `civilians.cfg` omitted | `fallback.log`, real task captures and `fallback-title.png` | Exit 0; built-in content definitions used; Ready; exact healthy digest/RNG |
| `--loading-stop assets.animations`, `world.minimap`, `world.audio` | `build/cj030/checks/cancel-*.log` | Exit 0; Cancelled; no title handoff |
| `--loading-fail assets.vehicles`, `world.renderer`, `world.population` | `build/cj030/checks/fail-*.log` | Exit 2; Failed; no title handoff |
| Before/after `--scenario day` and `drive`, `--frames 180 --every 60 --uncapped` | `build/cj030/smoke/` logs and PNGs | Exit 0; exact matching pixels at frames 60, 120 and 180 |

Negative runs call partial cleanup twice while the graphics context is alive, then close it normally. This exercises cancellation during atlas work, minimap generation and audio work, and failure during textures, render-target preparation and late population work. The readiness gate prevents entering the title after any injected stop.

The final real-startup TSV has 147 snapshots, zero backwards percentage changes and zero failed/cancelled states. Its maximum before Ready is 99%; the single Ready snapshot is 100%. The captured startup exits 0 and retains the baseline digest/RNG.

Build and run the headless contracts separately from the visible fixture (adjust the compiler path to your installation):

```powershell
& 'E:/Apps/raylib/w64devkit/bin/g++.exe' -std=c++17 -O2 -Wall -Isrc tools/cj030/model_tests.cpp src/startup_loading.cpp -o build/cj030/model-tests.exe
& './build/cj030/model-tests.exe'
```

The presentation fixture covers 1,280 × 720, 1,600 × 900, 1,920 × 1,080, 1,920 × 1,200, 2,560 × 1,080, 2,560 × 1,440, 3,840 × 2,160 and 3,440 × 1,440 px. Windows clamps the last two physical windows on this desktop, so those captures use an exact-size temporary render target and the same `DrawAtSize` implementation. `cases.jsonl` records requested, window, viewport, render and capture dimensions separately. Its `capture_target_color_bytes` field reports temporary capture color storage, not an estimate of total target/driver memory or part of the shipping loading resource budget.

The 5/25/100-group plans retain identical four-row geometry. Long UTF-8 text uses supported glyphs or deterministic replacement and measured ellipsis; `9999 / 10000` remains visible. Unknown local work after completed work retains the confirmed static fill and hides only the percentage. No clock, moving marker, pulse or blinking changes the bar. Only accepted work reports can extend it. Separate 99% and Ready/100% captures exercise the final gate. The source-used `TitleInputGuard` tests independently held Enter/Space, release/repress, immediate use of an unheld key and replacement of stale suppression at readiness.

No audio device could not be physically reproduced on this machine. The reporter model covers optional skip accounting, but that is not a device-unavailable integration result. Physical Enter/Space during loading, Esc and Alt+F4, and final visual acceptance remain user-review checks.

## Matched startup times

Five alternating, sequential warm-cache pairs reran the frozen executable and implemented executable against unchanged original content, using `--shot … --frames 1 --scenario title --uncapped`. Loading capture was disabled. PNG title export occurs after these measured startup times. The implemented matched chain includes the original assets, traffic turns and world initialization; full startup additionally includes loading-art bootstrap and metadata discovery.

| Pair | Before total (ms) | After matched chain (ms) | After full startup (ms) |
|---|---:|---:|---:|
| 1 | 9,679.469 | 9,836.318 | 10,349.133 |
| 2 | 9,949.060 | 9,865.950 | 10,375.089 |
| 3 | 9,623.369 | 9,885.336 | 10,394.596 |
| 4 | 9,589.444 | 9,847.927 | 10,352.527 |
| 5 | 9,326.103 | 9,782.614 | 10,269.452 |
| Median | 9,623.369 | 9,847.927 | 10,352.527 |

The matched-chain median increases by 224.558 ms (2.33%). Full startup increases by 729.158 ms (7.58%); about 0.5 s of the implemented total is cosmetic bootstrap and metadata discovery. Pair 1 records 426.952 ms bootstrap and 85.105 ms discovery. Local raw evidence is `build/cj030/pairs/before-N.log`, `after-N.log`, paired title PNGs and `summary.json`. The last successful 15-task timing history is used for subsequent budgets; it never advances progress or delays readiness.

Every pair reports digest `26cd87aeff4216b3` and RNG `54469b23`, with 91 vehicles, 300 pedestrians, 338 buildings and 4,266 objects. All five paired title PNGs are byte-identical. Both day/drive smoke series have identical PNGs at all three sampled frames. This verifies the measured initialization order/state and sampled simulation output, rather than a claim about every possible future seed or scenario.

These are observations on the baseline machine with warm caches. CPU model and other running workloads were not controlled. Capture runs are excluded: for example, the isolated fallback run exports 16 loading PNGs at 17,476.399 ms total export cost. Their longer startup must not be interpreted as normal loading overhead. No artificial stage hold was added.

## Responsiveness and resource cost

The five uncaptured implemented runs draw 302–304 startup frames. Presentation enqueue CPU means are 0.117–0.121 ms and maxima are 0.266–0.413 ms. The resolution fixture separately records `draw_enqueue_ms` at each viewport. These values measure CPU draw submission, excluding `EndDrawing`/VSync and GPU completion. Loading-owned texture/font storage is 10,503,608 bytes (10.02 MiB), below 16 MiB; it is released after the title transition. Allocator-level transient CPU image memory and driver overhead were not instrumented.

Uncaptured longest presenter boundary gaps are 375.654–400.488 ms, dominated by an indivisible audio-device call. Subtracting recorded atomic durations between callbacks leaves cooperative gaps of 4.509–6.419 ms. This establishes frequent safe service points outside blocking calls; it does not mean the whole startup always repaints every 33 ms. Pair 1's separately recorded maxima are:

| Atomic category | Longest call (ms) |
|---|---:|
| Metadata discovery I/O | 4.031 |
| Image decode | 71.552 |
| Image processing | 21.409 |
| Texture upload | 1.360 |
| Mipmap generation | 0.095 |
| Resize | 58.198 |
| Content definitions | 1.295 |
| Procedural image generation | 7.476 |
| Font | 7.941 |
| Shader | 23.989 |
| Framebuffer | 0.413 |
| Audio device/resource | 394.664 |

The bar remains still during these calls and then reflects newly confirmed work. Added content changes discovered budgets and local counts; longer or shorter actual work changes how long the screen stays visible. No elapsed-time percentage or minimum loading duration can finish it early or hold it after readiness.
