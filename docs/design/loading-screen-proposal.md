# CJ-030: Startup loading screen proposal

Design and implementation contract for a scalable startup screen, prepared on 2026-10-08, implemented after background approval and accepted by the user on 2026-10-09. The approved composition uses a fixed four-row status block and a static bar that retains confirmed work. The frozen baseline, automated results and acceptance are recorded in [CJ-030 loading measurements](../testing/cj030-loading.md). Startup ownership is documented in [Architecture](../architecture.md#start-up), rendering in [Rendering](rendering.md), and workflow in [Contributing](../../CONTRIBUTING.md).

## Contents

- [Scope and decisions](#scope-and-decisions)
- [Current startup](#current-startup)
- [Approved visual reference](#approved-visual-reference)
- [Composition and responsive layout](#composition-and-responsive-layout)
- [Content and wording](#content-and-wording)
- [Screen states](#screen-states)
- [Progress model](#progress-model)
- [Extending startup](#extending-startup)
- [Reporting and responsiveness](#reporting-and-responsiveness)
- [Readiness, failure and cleanup](#readiness-failure-and-cleanup)
- [Title handoff and determinism](#title-handoff-and-determinism)
- [Art package and resource budget](#art-package-and-resource-budget)
- [Acceptance criteria](#acceptance-criteria)
- [Measurement and review plan](#measurement-and-review-plan)
- [Delivery sequence](#delivery-sequence)
- [References and open measurements](#references-and-open-measurements)

## Scope and decisions

| Decision | Specification |
|---|---|
| Atmosphere | Detailed, glossy night-time city viewed from above, with warm street lights and restrained neon |
| Identity | Two-line ivory/amber CONCRETE / JUNGLE wordmark at the upper left |
| Progress | One continuous amber bar for the entire startup |
| Active work | A prominent current task and a smaller detail/count line |
| Completed work | Exactly one recent-completion line, replaced as each meaningful group finishes |
| Expansion | New startup work uses the same fixed-size status block |
| Interaction | Automatic handoff to the existing title screen; no extra dismissal key |
| Implementation status | Implemented, measured and accepted by the user on 2026-10-09; CJ-030 closed |

The approved status block has a fixed size as the number of startup groups grows. The complete category list belongs to internal startup reporting; the visible composition shows current work and one recent completion.

The scope is the startup/loading screen. The existing title screen, gameplay HUD, pause menu, control scheme, simulation rules and asset fallback policies remain outside the implementation scope, except for a safe handoff and startup failure handling. No new menu system, loading music, ETA, tips carousel, animated city scene, downloader, save-game loader or background threading framework is specified.

## Current startup

The following describes the source inspected on 2026-10-08 and the behavior preserved by the frozen measurement-only baseline on 2026-10-09. Its executable, source/content manifest, five run logs and initialized-state digest are recorded in [the measurement report](../testing/cj030-loading.md#frozen-baseline).

The baseline draws a near-black screen with a 60 px amber title and a 20 px status. It draws one frame before each of three blocking operations: asset loading, traffic-turn planning and game initialization, without progress callbacks or item counters.

| Existing owner | Actual work | Proposed readable groups and useful local units |
|---|---|---|
| [Assets::Load](../../src/assets.cpp) | Ground materials, generated water, facade textures | Ground materials; texture obligations |
| Asset loaders | Vehicle classes, sprites/paint variants, derived/composed images, motorbikes, train | Vehicles; prepared textures or processed records, explicitly qualified |
| Asset loaders | Civilian atlases and fallbacks, player art, body/feet animation atlases, weapons | Characters and animations; atlases or animation sets |
| Asset loaders | Trees, bushes, procedural props and lighting/effect textures | Foliage; props and effects; items |
| Assets::Load | UI fonts and composite/blur shaders | Interface and graphics; meaningful resources |
| [RailPlanTurns](../../src/traffic_turns.cpp) | Eligible vehicle sprite visits and cached/generated turn paths | Traffic routes; eligible sprite visits or path batches |
| [CityMap::Generate](../../src/city_map.cpp) | Tiles, blocks, rail, bridges, street furniture, spatial indices | City; tile rows, blocks or named generation passes |
| [Game::Init](../../src/game.cpp) | Minimap, renderer targets, synthesized/override audio | Map preview; graphics buffers; audio resources |
| Game::NewGame | Parked cars, traffic, player, pedestrians, starter car, pickups, mission phones, camera | World population; actor groups |
| Startup coordinator in [main.cpp](../../src/main.cpp) | Required-resource and usable-world checks | Final preparation |

The table describes useful presentation groups, not a replacement execution order. Preserve each existing loader's order and dependencies. The baseline caption “Building the city...” also covers minimap, rendering, audio and population work; the new reports distinguish these when they execute.

The implementation retains this dependency chain:

```text
Window / working directory -> cosmetic bootstrap -> side-effect-free discovery
-> assets in existing loader order -> traffic-turn warmup -> city generation
-> minimap -> renderer targets -> audio -> world population
-> final readiness -> existing title update/draw
```

Asset-local child discovery may run immediately before its own group, within the frozen parent budget described below. Required shaders and sprite/class registries must exist before downstream users run. Presentation draws never reorder those users.

Fonts remain late in `Assets::Load`; the presentation loads its own optional bootstrap resources independently. Asset and world initialization now return success/cancellation/failure through the reporter and validate required results. The final gate calls `Game::StartupReady` after all registered work resolves.

| Implementation owner | Source and responsibility |
|---|---|
| Coordinator | [main.cpp](../../src/main.cpp): primitive bootstrap, main-thread presenter, preserved load chain, final gate, diagnostics, title handoff and test CLI |
| Accounting | [startup_loading.h](../../src/startup_loading.h), [startup_loading.cpp](../../src/startup_loading.cpp): frozen task/child budgets, qualified counts, outcomes, snapshots and atomic-call timings; no raylib or gameplay dependency |
| Budget discovery | [startup_plan.cpp](../../src/startup_plan.cpp): current metadata work units and optional successful-startup duration history; no resource loading, RNG or simulation |
| Presentation | [loading_screen.h](../../src/loading_screen.h), [loading_screen.cpp](../../src/loading_screen.cpp): independent cosmetic resources, responsive layout, safe text fitting and snapshot drawing; caller owns drawing boundaries |
| Input | [startup_input.h](../../src/startup_input.h): independent release/repress guards for Enter and Space, called by `Game` |
| Execution owners | [assets.cpp](../../src/assets.cpp), [traffic_turns.cpp](../../src/traffic_turns.cpp), [city_map.cpp](../../src/city_map.cpp), [render.cpp](../../src/render.cpp), [audio.cpp](../../src/audio.cpp), [game.cpp](../../src/game.cpp): authoritative local plans, real completion, safe checkpoints and partial ownership |
| Cosmetics | [loading.cfg](../../assets/data/loading.cfg): paths, colors, wording and per-task text overrides; no scheduling, counts or percentages |

The [loading test interface and results](../testing/cj030-loading.md#loading-test-interface) record runtime verification and its limits.

## Approved visual reference

![Final approved Concrete Jungle loading background](../images/cj030-loading-background.png)

The user approved this text-free image as the final background on 2026-10-09. The [asset record](loading-background.md) preserves its native dimensions, checksum and provenance. The city is atmospheric artwork rather than an exact preview of the generated map. Its runtime copy preserves the approved bytes. The wordmark and live loading interface are separate layers, specified in the [art brief](loading-screen-art-brief.md).

## Composition and responsive layout

Use a full-screen background with aspect-preserving cover cropping and a separately positioned wordmark. Keep the focal intersection toward the centre/right and quiet, dark space behind the upper-left wordmark. Apply a runtime lower gradient and a restrained vignette. There is no bordered status card.

Use a centred safe composition based on 1,920 × 1,080 px. Let s be the smaller of viewport width / 1920 and viewport height / 1080. The status area is 1728s px wide and max(152, 216s) px high, with 64s px bottom clearance. The logo aligns with its left edge, with 64s px top clearance, and fits within 640s × 360s px without distortion. These bounds accommodate the approved two-line wordmark.

| Viewport | Status area x / y | Width / height | Bottom margin | Wordmark left / top |
|---|---|---|---|---|
| 1,920 × 1,080 px | 96 / 800 px | 1,728 / 216 px | 64 px | 96 / 64 px |
| 1,600 × 900 px | 80 / 667 px | 1,440 / 180 px | 53 px | 80 / 53 px |
| 1,280 × 720 px | 64 / 525 px | 1,152 / 152 px | 43 px | 64 / 43 px |
| 1,920 × 1,200 px, 16:10 | 96 / 920 px | 1,728 / 216 px | 64 px | 96 / 64 px |
| 2,560 × 1,080 px, ultrawide | 416 / 800 px | 1,728 / 216 px | 64 px | 416 / 64 px |

Round final coordinates consistently. The centre safe area prevents a very wide monitor from separating the active text and percentage excessively; artwork fills the remaining sides. Treat 1,280 × 720 px as the minimum target for this specification. Check desktop UI scaling and physical viewport dimensions separately to avoid applying DPI scaling twice.

The status area always reserves four rows:

1. Active headline.
2. Friendly detail and an optional qualified current-group count.
3. Overall progress track and a separate right-aligned percentage.
4. The most recently completed group.

At the reference size, use 24 px horizontal padding, 12 px vertical padding, 12 px row gaps and a 12 px track. Headline/detail/percentage/recent font sizes are 32 / 22 / 32 / 20 px. Reserve line boxes of 40 / 30 / 40 / 28 px; the progress row's height includes its percentage. Scale these dimensions with s, with headline/percentage minima of 24 px, supporting-text minima of 18 px, vertical-padding and line-box minima that permit legibility, and an 8 px track minimum.

At 720 px height, the four row boxes are 28 / 22 / 28 / 22 px, the three gaps are 8 px each, and top/bottom padding is 12 px each: a total of 148 px inside the 152 px strip. At 1,080 px, row boxes, gaps and vertical padding use 198 px inside the 216 px strip. Measure the actual selected font against those row boxes; adjust within the remaining space rather than relying on nominal font size alone. All four rows must fit without a visible card.

Reserve the percentage column from the measured width of “100%” plus 24s px separation; it must not move as digits change. The group count has a separate right-aligned column on the detail row. Reserve its measured width, including the unit. The detail text ends before that column. Thus a current-group count and an overall percentage are visibly distinct.

| Element | Proposed colour |
|---|---|
| Base and lower scrim | #10171D, opacity adjusted for reliable contrast |
| Primary text | #F0EEE8 |
| Detail and recent text | #B8C2CC |
| Progress fill and percentage | #FFCD46 |
| Track | #35414C |
| Fatal-state emphasis | #F08E7F |

Treat these as starting tokens for art QA. Validate text contrast against the composed pixels, including the brightest street lights; target at least 4.5:1 for every status text row. The lower scrim may need near-opaque backing locally. Material texture belongs to the wordmark, while changing text remains plain and crisp.

No text may be baked into the city background. Letterboxing, stretching, independent per-row scaling, scrolling labels and a new line for every loading group are excluded.

## Content and wording

Ship this version with English UI, consistent with the existing game. Author concise display names that can later be localized; keep task IDs and file paths independent of display text. Measure text and ellipsize on a Unicode character boundary. A long name must not push a counter or percentage outside the safe area.

Illustrative content:

```text
Loading vehicle textures...
Sedans, vans and motorbikes                       38 / 58 textures
[one continuous overall progress bar]                         68%
Recently completed: Ground materials
```

The headline names the active operation, the detail explains its content, and the count names its own unit. The count is optional. “38 / 58 textures” is permitted only when 58 known texture obligations exist and 38 have usable results. If the loader is counting records, use “38 / 58 records processed” instead. A file attempt that was skipped is not a prepared texture.

| Work | Headline example | Detail example |
|---|---|---|
| Bootstrap | Preparing startup... | Optional cosmetic resources |
| Discovery | Finding game content... | Reading content definitions |
| Materials | Loading ground materials... | Roads, pavements and facades |
| Vehicles | Loading vehicle textures... | Sedans, vans and motorbikes |
| Characters | Loading character animations... | Walking, running and actions |
| Foliage | Preparing foliage... | Trees and bushes |
| Props/effects | Preparing city objects... | Street furniture and effects |
| Graphics | Preparing graphics... | Interface, lighting and render resources |
| Traffic | Planning traffic turns... | Vehicle routes through junctions |
| City | Building the city... | Streets, buildings and metro |
| Minimap | Preparing the city map... | Map preview |
| Audio | Preparing sounds... | Effects, engines and sirens |
| Population | Preparing the city population... | Traffic, pedestrians and player |
| Final gate | Preparing the title screen... | Checking required resources |
| Ready | Ready | Omit unnecessary detail |

The table supplies examples, not a mandatory fourteen-step on-screen sequence. Quick groups can complete between two visible frames. Coalesce detail changes to at most 5 updates/s while showing the newest actual count. Present fatal state, readiness and a visible active-group change promptly. Never delay loading to give a label a minimum screen time.

Before the first group completes, leave the recent row empty but reserve its position. After a group succeeds, replace it with that group's readable name. Do not rotate history. If a fallback was used, qualify the completion when relevant; an unavailable audio device can say “Audio unavailable; continuing silently”, without reporting sounds as loaded. A persistent warning or fatal action may temporarily use the recent row.

Do not show full paths, shader uniform names, debug times, log streams or a future-task queue in the release screen. Full diagnostic detail belongs in startup logs.

## Screen states

| State | Indicator and percentage | Status behavior |
|---|---|---|
| Bootstrap | Empty static track, no percentage | Primitive dark screen can draw before any optional art |
| Discovering | Empty static track, no percentage | Enumerate the startup plan without simulation side effects |
| Loading | Determinate bar and integer percentage | Active task, useful local detail/count, recent completion |
| Unmeasurable work | Static confirmed fill, no numeric percentage | Name the real operation and retain all confirmed progress visibly |
| Loading with fallback | Actual work indicator | Preserve existing fallback policy and report usable outcomes |
| Final preparation | Determinate, below 100% | Validate all mandatory startup prerequisites |
| Ready | 100% and Ready | Hand off automatically at the next normal title update/draw |
| Failed | Frozen track, no activity animation | Concise cause; “Press Esc to exit”; Alt+F4 remains available |
| Cancelled | Stop scheduling work | Unwind owned resources and close safely |

The bar always retains its confirmed fill, including during discovery and brief transitions whose local total is not yet known. Unknown work hides the numeric percentage; the current caption explains preparation. Following the user's simplification on 2026-10-09, there is no moving segment, blinking or pulse inside the bar. Do not infer failure solely from elapsed time, and do not advance percentage on a timer.

No button press is needed at success. Fatal exit is keyboard-accessible because the normal Windows pointer is hidden. Generic retry is excluded from the first implementation: partially initialized resources, caches, RNG and serial counters do not currently have a safe in-place restart contract.

## Progress model

A startup coordinator owns top-level tasks with stable IDs, labels, dependencies and positive planned work weights. Each task supplies a progress fraction f in [0, 1]. Once discovery is complete, freeze the top-level set and total weight W.

```text
W = sum(task weights)
P = sum(task weight × task fraction) / W
displayed percentage = floor(100 × displayed fraction)
```

The percentage represents estimated weighted work completed, not elapsed time or a prediction of seconds remaining. Counts alone are insufficient when a small texture and an expensive atlas have very different costs. `startup_plan` derives current work units from metadata, image sizes, content definitions, world configuration and screen size. First-run weights use current units multiplied by coarse initial unit-cost estimates. Optional `build/cache/startup-work-profile.cfg` records each completed task's units and duration after successful startup; later runs scale the measured cost by their current units. Both paths adapt budgets when content changes, then freeze them for that run. An unavailable/invalid profile uses the initial unit cost and never prevents startup. Timing estimates affect only relative budgets: no timer advances completion or delays a fast load. The [measurement report](../testing/cj030-loading.md#work-accounting-and-duration-adaptation) records the cache format and baseline limits.

Fractions advance only at observed completion checkpoints: a resource is validated, a generation batch finishes or a policy decision resolves an optional obligation. Starting an operation earns no completion credit. A fallback resolves the original obligation once, after its usable result is committed. A failed mandatory task never earns success credit.

Keep one explicit final-validation task with positive weight. Until readiness passes, display at most 99% and keep the endpoint below full completion. Ready alone permits 100%. All weights must be finite and positive; a valid plan always contains the final gate, so its denominator is non-zero.

Tasks have Pending, Running, Ready, ReadyWithFallback, Skipped, Failed and Cancelled outcomes. Skipped means a documented optional decision, not silently ignored failure. A task fraction reaches 1 only after its required result is committed and validated, or an explicit optional policy resolves it. Parents finish only after all required children are resolved according to policy.

For a non-empty child plan, compute its parent fraction as sum(child weight × child fraction) / sum(child weights), with finite positive child weights. Only top-level parent weights enter W; never add their descendants again to the global numerator or denominator. An empty optional group resolves as explicitly Skipped rather than dividing by zero. An empty mandatory group follows its validation policy and fails if required usable content is absent.

Keep confirmed progress monotonic within a frozen plan. Draw the confirmed fraction directly, without interpolation or decorative movement. Compute the displayed number from the same fraction as the fill. Both reach 100% on readiness. If no new work completes, the confirmed value stays still.

## Extending startup

Every future startup feature registers its own ID, dependency, display text, work model and readiness policy during discovery. The UI draws the same four-row snapshot regardless of whether there are 5, 25 or 100 groups. Adding content must not require new icons, new columns, a longer footer or a draw-function branch for each category.

Top-level budgets freeze before determinate display. A dependency-sensitive group may freeze its own child plan when it activates, while its parent fraction is still zero. For example, traffic planning can enumerate actual eligible sprites after vehicle loading, without changing the global denominator. Children consume only that already-registered parent's budget.

The planning pass reads content records and fixed generation recipes without loading the world, generating art, spawning actors or consuming random numbers. Use parsed data consistently so discovery and execution do not count different snapshots. Derive a group's known local totals before advancing that group; reserve named deterministic passes when a precise item total is inappropriate.

Expected fallback branches belong to their original obligations. Future genuinely unknown work must be registered as an unmeasurable/not-started parent before progress begins. Resolve its child plan before switching it to determinate progress.

Late registration after the top-level registry freezes, or child-plan registration after a parent has reported progress, is a plan-contract error. The reporter rejects the call, marks the plan invalid and hides numeric progress while retaining confirmed completion. The coordinator stops the failed initialization and presents Failed state; readiness/title handoff cannot succeed. It never silently increases a completed denominator, moves the bar backwards, hides extra work at 100% or invents an unused percentage reserve. Register a new obligation within its not-started parent before advancing that parent, or extend discovery before the next run.

These rules make the interface extensible while keeping the workload accounting explicit.

## Reporting and responsiveness

`startup::Reporter` is the startup reporting interface; `main` owns the main-thread presentation callback. Subsystems report task start, checkpoint, outcome and error through an optional reporter pointer; they do not draw loading UI themselves. A null reporter retains ordinary calls without presentation. `Pulse` services a safe checkpoint without granting progress; `AtomicSpan` measures an indivisible call without drawing or awarding work.

A snapshot includes active task ID and display text, optional qualified local totals, latest completed group, confirmed overall work, state and concise warning/error. Rendering consumes snapshots without changing game content. Counters and progress events may update more frequently than visible text.

Keep graphics calls on the existing owning main thread. Drawing must occur outside active texture, 3D and shader passes. Complete or suspend an off-screen pass safely before drawing to the window, then restore the required graphics state. Checkpoint reporting must not call BeginDrawing while a minimap texture pass is active.

Proposed targets:

- Aim for a 30 Hz redraw and window-event cadence during cooperative loading.
- Split CPU loops into batches with at most 50 ms between safe checkpoints on the baseline machine.
- Record individual image decode, font, shader compilation, texture upload, audio and framebuffer calls separately; cooperative scheduling cannot preempt an indivisible driver/library call.
- Present state changes immediately at the next safe drawing boundary, and service Alt+F4 there.
- Measure actual draw cost, longest repaint gap and startup-time overhead before claiming these targets are met.

Useful checkpoint locations include animation decode/atlas loops, civilian packing, procedural props/fallbacks, tile rows, city blocks, furniture/index batches, turn-path search batches, each render-target allocation and each audio resource. A callback only at the end of a large function will not solve a long pause inside it.

The minimap and turn planner need particular care: the minimap holds an off-screen pass, while turn planning contains nested candidate/search loops. Audit safe yielding points and verify output equivalence. This is cooperative reporting, not a promise of a new asynchronous engine or faster underlying initialization.

## Readiness, failure and cleanup

Readiness means the initialized title/world can actually run. Validate required resource handles, buffers, intended graphics paths, world collections and player initialization. Existing documented fallbacks are acceptable results. Optional audio and cosmetic loading art must not become mandatory.

| Condition | Required behavior |
|---|---|
| Missing loading background/wordmark | Near-black background and plain amber title; status block remains usable |
| Missing loading font | Built-in font; readable live status |
| Missing data file | Preserve the loader's built-in defaults |
| Missing regular art | Preserve that loader's existing skip/substitute behavior; distinguish valid fallbacks from skipped records |
| No audio device | Continue silently and log the existing warning |
| Mandatory resource cannot be prepared | Failed state, no 100%, no title handoff |
| Alt+F4 during loading | Stop at the next safe boundary, unwind and close |
| Optional resource legitimately omitted | Resolve it as policy-skipped; do not label absent data as loaded |

Do not introduce a universal per-file fallback rule. Today some loaders skip missing records, while others generate art when an entire group is empty. Preserve these policies and validate resulting usable collections.

Shader checks validate the actual required composite/blur resources and locations, not every declared uniform. Composite requires `lightMap`, `emissive`, `bloomTex` and `bloomStrength`; blur requires `dir`. The unused composite `time` uniform may legitimately be optimized to −1. An empty or invalid required render target is not success. Log task ID, source path where relevant, error and disposition; release-screen text remains concise.

Keep a clear ownership record for every acquired loading, game and audio resource, including temporary CPU images, intermediate atlas buffers and PCM allocations in interrupted work. Cleanup runs once in reverse dependency order while the corresponding graphics/audio context is alive. Never unload the built-in font, alias-owned resources twice, or a texture still used by the fade. Resources already transferred to the game are released by its owner.

## Title handoff and determinism

After readiness, present the completed state without an artificial hold, then allow the existing normal title update and draw. A loading-art overlay may fade over that output for 250 ms; it must not gate title input. Remove it immediately if the user starts playing during the fade. This preserves the living-city title behavior and avoids adding a separate menu.

Do not call Game::Update, NewGame or CameraRig::Update just to prepare a loading transition. CameraRig::Update consumes gameplay random numbers even for a zero-dt/no-shake update. Render preparation must not consume extra gameplay RNG or simulate time.

A critical existing dependency: RailPlanTurns visits every eligible vehicle sprite and calls InitVehicle before looking up a turn path. InitVehicle consumes gameplay RNG and advances vehicle serial state. Preserve the exact visits, call count and order, including cache hits. Discovery must inspect metadata without calling InitVehicle. Deduplicating this warmup would change the initial world.

Presentation must also preserve city seeds, generation ordering, spawn attempts, registry order and all asset processing outputs. Drawing never consumes gameplay random numbers.

Loading keypresses must not start gameplay accidentally. Consume startup-only events and record which Enter/Space keys are held at readiness. Suppress each held key only until its release, then accept a fresh press through the normal title input path. A key not held at readiness can accept its first fresh press immediately. Preserve the title's existing Escape exit behavior.

For existing --shot scenarios, loading presentation runs before the scenario frame counter. Bypass wall-clock fades, keep the fixed scenario timestep, and leave the gameplay screenshot sequence unchanged. A title screenshot after initialization is not evidence that the loading screen works.

## Art package and resource budget

Runtime layers:

| Layer | Deliverable | Ownership |
|---|---|---|
| Background | Final approved text-free 1,672 × 941 px PNG | Loading presentation |
| Wordmark | Transparent two-line ivory/amber PNG, bounded near 640 × 360 px at reference scale | Loading presentation |
| Status text | Small loading-only Rajdhani font atlas or the built-in bootstrap font | Loading presentation |
| Contrast | Runtime gradient/vignette | Drawn directly |
| Progress/status | Runtime geometry and live text | Drawn directly |

The runtime background is [assets/ui/loading-city.png](../../assets/ui/loading-city.png), a byte-identical copy of the approved source; the independent transparent wordmark is [assets/ui/loading-logo.png](../../assets/ui/loading-logo.png). [assets/data/loading.cfg](../../assets/data/loading.cfg) holds cosmetic paths, colours and display strings with validated built-in defaults. This file cannot create tasks, set dependency order or encode counts/percentages. Work registration and dependencies belong to their subsystem owners. [CREDITS](../../CREDITS.md#cj-030-loading-screen-background) records generated-art provenance and checksums.

The existing Rajdhani font is already documented in [CREDITS](../../CREDITS.md). The loading-only font owner may use those source files early while leaving the normal game font loader intact; account for temporary duplicate atlases and release the loading atlas after its last transition draw. Future localization needs explicit glyph coverage rather than assuming the default atlas supports every language.

PNG is required; this project's raylib build cannot load JPG. Use bilinear filtering and retain aspect ratios. The background/wordmark are loaded once; changing text and bar geometry do not require regenerating images.

Target at most 16 MiB of additional loading-presentation GPU storage, including background, logo, font and mipmaps. The approved 1,672 × 941 px RGBA background costs about 6.0 MiB before mipmaps. Avoid additional full-screen render targets, live-world rendering, per-frame image processing or a bloom pass solely for the loading screen.

Target presentation draw CPU time of at most 0.5 ms at 1,920 × 1,080 px on the matched baseline machine. Measure separately from waiting on VSync. The [measurement report](../testing/cj030-loading.md) records enqueue CPU, bootstrap time and resident GPU estimates; allocator-level transient CPU image memory remains uninstrumented.

Release loading-only textures/font after the transition; keep them available in an error screen until exit. The fallback presentation must remain available if cosmetic loading itself fails.

## Acceptance criteria

The [measurement record](../testing/cj030-loading.md#verification-checklist) records passing model, layout, real-startup, fallback, cleanup and deterministic-output checks. Draw CPU and GPU targets are met on the measured machine. The user accepted the result on 2026-10-09. Physical no-audio-device behavior and individually unlogged keyboard/close cases remain measurement limits.

| ID | Test | Required result |
|---|---|---|
| L01 | Capture the five layout resolutions above | Correct aspect ratio; all text/geometry inside safe bounds; status text sizes and contrast meet the layout targets |
| L02 | Replay plans with 5, 25 and 100 groups | Identical status area size and four-row layout; no category-specific draw changes |
| L03 | Labels of 100 Unicode characters using supported or deterministic replacement glyphs, long paths, counts through 9999 / 10000 | Safe ellipsis; unit and percentage remain visible; no overlap; complete localization remains outside scope |
| L04 | Known synthetic work with unequal weights | Fill and percentage derive from confirmed weighted work; never run ahead, reverse or restart |
| L05 | Unknown discovery/child work becoming known | Static confirmed fill; no invented percentage, animation, layout jump or changed global denominator |
| L06 | An expensive final task and readiness rejection | 100% never appears before all mandatory checks pass |
| L07 | Missing loading art/font and regular asset fallback cases | Presentation remains readable; original fallback policy preserved; successful/fallback/skipped outcomes distinguished |
| L08 | No audio device | Startup continues silently with a useful warning and valid progress |
| L09 | Injected mandatory failure and cancellation at multiple safe boundaries | Clear stopped/error state, usable exit, no title entry, no double unload |
| L10 | Slow cooperative work and individually timed atomic calls | 30 Hz cadence aim; checkpoint gaps ≤50 ms for subdividable work; atomic exceptions reported separately |
| L11 | Warm/fast startup | No minimum stage duration, forced decorative wait or loading dismissal |
| L12 | Title transition and Enter/Space during loading | Automatic handoff; a fresh title press works; no buffered auto-start; Alt+F4 remains available |
| L13 | Initial world digest and fixed-frame title/day/drive captures | Same seeded world, RNG/serial state and existing scenario setup on matched source/content |
| L14 | Loading resource accounting and draw timing | Additional GPU estimate ≤16 MiB; presentation draw CPU ≤0.5 ms; resources released once |
| L15 | User review of the actual implemented startup | Night-city art, readable status, quiet updates, credible progress and transition accepted |

## Measurement and review plan

1. Retain the [frozen baseline](../testing/cj030-loading.md#frozen-baseline) executable, source/content manifest, phase timings, screenshots and digest. Record the implemented source/content fingerprint and machine/workload for comparisons. Do not compare builds made against changing assets.
2. Record bootstrap, task, matched-chain and full startup times, decode/generation/upload spans, longest event-service gap, outcomes and initial world digest. Preserve the existing execution order. The current baseline records broad asset/turn/world phases; it does not establish atomic-call or presentation targets.
3. Profile cold-start observations separately from repeated warm-cache runs. Do not clear the user's OS caches or stop other processes merely to simulate cold loading. Use at least five alternating matched before/after warm pairs once the environment is stable.
4. Calibrate positive work weights and check long final phases. Record the weighting method, raw timing results and startup overhead; do not infer a remaining-time promise from the weights. A total-startup overhead limit must be agreed from the measured baseline.
5. Run the dedicated loading capture/replay fixture and real startup capture path. Exercise reporter snapshots with unknown work, long labels, extra categories, fallback, failure and cancellation. Use `--loading-test`, `--loading-capture`, `--loading-config`, `--loading-stop` and `--loading-fail` as described in the [test interface](../testing/cj030-loading.md#loading-test-interface). Loading captures stay outside ordinary scenario frame counting and their export cost is excluded from matched duration comparisons.
6. Compare initialized state before the first normal title update: gameplay RNG, generated city/index results, actor classes/skins/serials/driver skins/poses, camera, world clock and mission/population setup. Keep the snapshot digest deterministic and exclude render-only caches.
7. Run fixed-frame title/day/drive smoke captures and relevant existing checks under [Testing](../testing.md). Expand to traffic/recovery suites if state differences or changes to warmup/generation expose risk. Do not rerun unrelated long suites without such evidence.
8. Review resolution/state contact sheets, then let the user watch and playtest actual startup and title handoff. Mark the backlog item complete only after measured verification and visual acceptance.

Five alternating warm before/after pairs preserve the initialized digest/RNG and exact title pixels. Median full startup changes from 9,623.369 ms to 10,352.527 ms (7.58%); the matched initialization chain increases by 2.33%. Loading-owned GPU storage is 10.02 MiB. Atomic audio/decode/resize stalls are reported separately from short cooperative gaps. These machine-specific observations, their limits and the user's acceptance are in the [measurement record](../testing/cj030-loading.md).

## Delivery sequence

1. **Approved design:** retain the accepted background and fixed four-row composition.
2. **Frozen baseline:** measurement-only instrumentation, executable and source/content manifest retained; five initial-state observations recorded.
3. **Production art:** approved background bytes and independent wordmark packaged; eight-resolution crop/readability checks and loading-resource measurements recorded.
4. **Source integration:** discovery, frozen accounting, completion reports, checkpoints, presentation, readiness/cleanup and title-input handoff implemented for verification.
5. **Verification:** build/run accounting and layout fixtures, matched startup comparisons, fallback/cancel/fail injection and deterministic title/day/drive captures. Record results before claiming targets passed.
6. **User review and documentation:** implemented startup accepted by the user on 2026-10-09; runtime documents, credits and changelog updated; CJ-030 closed.

The final background and visual/UX direction are approved, automated verification is recorded, and the user accepted the implemented startup on 2026-10-09. CJ-030 is complete.

## References and open measurements

[Microsoft's progress-control guidance](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/progress-controls) supports using determinate progress when work can be measured and activity indication while totals are unknown. The [Win32 progress-bar guidance](https://learn.microsoft.com/en-us/windows/win32/uxguide/progress-bars) supports one overall bar, genuine progress, monotonic completion and 100% only on completion. These are interaction references; the city's art direction and the fixed four-row composition are project design decisions.

The implementation adds no external library. Shipping-art provenance is recorded in CREDITS. Matched startup timings and shipping GPU/font storage are measured in the report. Weighting remains a distribution estimate; it supplies no remaining-time promise. Transient CPU allocator/driver memory and unavailable-device integration remain outside the recorded measurements.
