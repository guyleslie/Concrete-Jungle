# CJ-030: Loading screen art brief

Use the [final approved background](loading-background.md) with the presentation defined in the [loading-screen specification](loading-screen-proposal.md). The city image, wordmark and live loading information are independent layers.

## Background

The fixed city layer is [cj030-loading-background.png](../images/cj030-loading-background.png), approved on 2026-10-09 and created with the built-in ImageGen tool. Its art direction is a detailed high bird's-eye night city, brick and concrete architecture, damp glossy asphalt, warm amber streetlights and deep navy/graphite shadows. The upper-left dark roof provides space for the title; the lower frame supports the status overlay.

Retain the accepted image's composition and native 1,672 × 941 px content. Use aspect-preserving scaling, bilinear filtering and the responsive crop checks in the specification when preparing the runtime package.

## Wordmark

Prepare CONCRETE above JUNGLE as a separate transparent PNG. Use tall condensed uppercase lettering with warm ivory and amber accents, restrained concrete/metal texture and clean edges. Keep the approved upper-left placement and two-line proportions. Export with enough transparent padding for filtering, within the specified 640 × 360 px reference bounds.

## Live interface

Draw one amber overall progress bar and four fixed rows: current task, useful detail and qualified group count, overall progress and percentage, and one recent-completion line. Use the font sizes, spacing and colour tokens in the specification. Counts and percentages come from real startup reports. Draw the contrast gradient and vignette at runtime.

## Packaging

Runtime assets are `assets/ui/loading-city.png` and `assets/ui/loading-logo.png`; the city copy preserves the approved bytes. `assets/data/loading.cfg` selects the cosmetic resources and labels. The wordmark source has alpha transparency and is reduced only for GPU upload. Monitor crop/readability checks and resource measurements are recorded in the [loading checks](../testing/cj030-loading.md).
