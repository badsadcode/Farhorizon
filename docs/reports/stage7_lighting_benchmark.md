# Stage 7 lighting benchmark

RTX 3080, driver 621.01, Windows 10, 1600×900, Release, VSync off, validation off, all lighting
features on (the default in benchmark runs), 2026-11-04.

| Benchmark | GPU avg (ms) | Frame avg (ms) | Frame p99 (ms) | FPS |
|---|---|---|---|---|
| `bench.asteroid_field_chase` | 0.61 | 0.64 | 1.10 | 1572 |
| `bench.station_approach` | 0.61 | 0.64 | 1.05 | 1562 |
| `bench.combat_skirmish` | 0.83 | 0.85 | 1.51 | 1170 |
| `bench.planet_orbit` | 1.35 | 1.38 | 2.95 | 723 |
| `bench.ship_interior` | 0.97 | 1.00 | 2.31 | 1002 |
| `bench.station_hangar` | 1.02 | 1.06 | 2.23 | 947 |
| `bench.surface_outpost` | 0.82 | 0.85 | 1.48 | 1173 |
| `bench.surface_landed` | 0.95 | 0.98 | 2.18 | 1016 |

These are placeholder scenes with little geometry, so they say the pipeline has plenty of room, not
what a full Blender-built map will cost. Earlier benchmarks in this folder used a software rasteriser
and are not comparable.
