# CJ-030: Loading screen art brief

Use the [supplied background](loading-background.md) and wordmark with the presentation defined in the [loading-screen specification](loading-screen-proposal.md). The 2026-10-14 refresh replaces the artwork while retaining the layout and loading behavior. The city image, wordmark and live loading information are independent layers; visual review of the refreshed composition remains pending.

## Background

The fixed city layer is the user-supplied [cj030-loading-background.png](../images/cj030-loading-background.png). Its original creation tool is not recorded. The image shows a detailed elevated view of a wet night-time intersection, brick architecture, warm amber streetlights, traffic, pedestrians and shop signs. The title and live status remain separate overlays.

Retain the supplied image's composition and native 1,670 × 942 px content without repainting or resampling the file. Use aspect-preserving scaling, bilinear filtering and the responsive crop checks in the specification when preparing the runtime package.

## Wordmark

Prepare the user-supplied [wordmark source](../images/cj030-loading-wordmark-source.png) as a separate transparent PNG. Preserve the white cracked-concrete CONCRETE lettering, the red graffiti JUNGLE lettering, black edges and the red crown. The original 1,774 × 887 px source has a magenta background; built-in ImageGen was used only to extract transparency on 2026-10-13, not to create a new design. The [exact edit prompt](loading-wordmark-prompt.txt) records the requested invariants. Keep the existing upper-left placement and the supplied proportions. Export with transparent padding for filtering, within the specified 640 × 360 px reference bounds.

| File | Dimensions and format | File size | SHA-256 |
|---|---|---:|---|
| `docs/images/cj030-loading-wordmark-source.png` | 1,774 × 887 px, magenta-background PNG | 2,372,533 bytes | `260dc6f25ee5e9d089310f942bcd887c08b5876d8915b7f8ca5dc6ced73c0703` |
| `assets/ui/loading-logo.png` | 1,774 × 887 px, RGBA PNG | 1,532,088 bytes | `02e0bcf4abb66faf39586b184ed67d869d606d6342066f15ad5201acf8753002` |

The runtime file is the first transparent cutout, integrated on 2026-10-14. The lettering, crown and composition were visually checked against the source; this is not a pixel-identical foreground extraction. A second extraction attempt was rejected and is not used.

## Live interface

Draw one amber overall progress bar and four fixed rows: current task, useful detail and qualified group count, overall progress and percentage, and one recent-completion line. Use the font sizes, spacing and colour tokens in the specification. Counts and percentages come from real startup reports. Draw the contrast gradient and vignette at runtime.

## Packaging

Runtime assets are `assets/ui/loading-city.png` and `assets/ui/loading-logo.png`; the city copy preserves the supplied bytes. `assets/data/loading.cfg` selects the cosmetic resources and labels. Retain the original magenta wordmark source in `docs/images/`; the runtime cutout has alpha transparency and is reduced only for GPU upload. [Credits](../../CREDITS.md#cj-030-loading-screen-wordmark) record its source and preparation. Monitor crop/readability checks and resource measurements are recorded in the [loading checks](../testing/cj030-loading.md).
