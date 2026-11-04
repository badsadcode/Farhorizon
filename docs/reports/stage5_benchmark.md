# Stage 5 benchmark report

**Environment:** development container, software Vulkan (llvmpipe, 4 cores, Xvfb), Release,
1920×1080, no vsync, look `usedfuture`. This is a regression baseline only.

| Scenario | Frames | Avg frame | p99 | Avg GPU | Notes |
|---|---|---|---|---|---|
| `bench.planet_orbit` | 55 | 178.8 ms | 214.6 ms | 177.7 ms | Varga fills ~70% of the screen from 400 km |
| `bench.landing` | 141 | 175.8 ms | 239.7 ms | 174.0 ms | full descent → fade → surface approach → touchdown |
| `bench.surface_landed` | 73 | 135.3 ms | 166.1 ms | 133.4 ms | chase view on the pad, terrain + fog + 4 masts |
| `bench.surface_outpost` | 70 | 140.6 ms | 173.3 ms | 138.9 ms | on foot, panning |

## Reading
* The planet impostor costs ~70 ms *on a CPU rasteriser* when it covers most of the screen
  (6-octave fbm plus two 27-cell crater lookups per pixel). It is scissored, so a distant
  planet is cheap. On a GPU this is a typical fullscreen effect cost; baking the surface into
  a cubemap is the fix if profiles ever show it (ISSUES #23).
* The surface scene is in line with the other fog + point-light interiors. The 8,100-triangle
  terrain mesh does not register.
