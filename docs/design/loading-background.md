# CJ-030: Loading background

The user supplied this replacement startup-screen background for the 2026-10-14 artwork refresh. The [loading screen](loading-screen-proposal.md) uses its unchanged runtime copy; the wordmark and loading interface are separate layers. CJ-030's original implementation was accepted on 2026-10-09; the refreshed in-game composition awaits the user's visual review.

![Supplied Concrete Jungle loading background](../images/cj030-loading-background.png)

## Asset record

| Property | Value |
|---|---|
| File | `docs/images/cj030-loading-background.png` |
| Dimensions | 1,670 × 942 px |
| Format | Opaque PNG |
| File size | 3,481,552 bytes |
| SHA-256 | `c7a1dd759d18e7fc367c28bd9b6a253ccdce0dab2b6ba1cb655dffbf5bb6b8ef` |
| Origin | User-supplied artwork; original creation tool not recorded |
| Integration status | Replacement requested by the user; in-game visual review pending |
| Runtime installation | Byte-identical copy at `assets/ui/loading-city.png`, selected by `assets/data/loading.cfg` |

The canonical file and runtime copy preserve the supplied image's bytes. Keep its city composition fixed when packaging. It depicts a wet night-time intersection with warm street lights, traffic, pedestrians and shop signs. It is atmospheric artwork rather than an exact preview of the generated game map.

## Integration

Use aspect-preserving cover scaling and the safe areas defined in the [responsive layout](loading-screen-proposal.md#composition-and-responsive-layout). Place the separate white CONCRETE / red graffiti JUNGLE wordmark and crown at the upper left and the live four-row status block at the bottom, with the specified contrast gradient.

Use the image at its native size with bilinear filtering. Verify readability and cropping at the target monitor resolutions during implementation. The native image is the supplied source; no larger source master is required by this design. Account for approximately 6.0 MiB of RGBA texture storage before mipmaps, within the shared loading-presentation budget.

The [art brief](loading-screen-art-brief.md) specifies the separate wordmark and interface layers. [Credits](../../CREDITS.md#cj-030-loading-screen-background) record the artwork's origin.
