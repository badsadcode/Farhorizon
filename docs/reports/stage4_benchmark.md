# Stage 4 benchmark report

**Environment:** development container, software Vulkan (llvmpipe, 4 cores, Xvfb). This is a
regression baseline only, not target-GPU performance. Look: `usedfuture`. 1920×1080, no vsync.

| Scenario | Frames | Avg frame | p99 | Avg GPU | Notes |
|---|---|---|---|---|---|
| `bench.mining` | 194 | 102.3 ms | 121.4 ms | 99.8 ms | laser on, chunks spawning/scooped, impact point light |
| `bench.asteroid_field_chase` | 90 | 109.4 ms | 141.2 ms | 107.5 ms | |
| `bench.station_hangar` | 86 | 115.1 ms | 148.0 ms | 112.7 ms | 6 point lights + fog |
| `bench.ship_interior` | 74 | 133.9 ms | 161.2 ms | 132.0 ms | 5 point lights + fog + sky through windows |

## Reading
* Still fill-rate bound on the CPU rasteriser. The interiors became ~15 ms slower than in Stage 3
  because every fragment now loops over up to 8 point lights and applies fog. On real GPUs this
  is a small cost; a clustered light list would only be needed for many more lights.
* Mining adds a few hundred glow sprites (the beam) and up to 40 chunk instances; it costs no
  more than plain asteroid flight.
