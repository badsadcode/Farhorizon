# Benchmark definition (`content/benchmarks/*.json`, type `benchmark`)

A fixed, repeatable benchmark path (design doc 14): a scene, a start pose, a camera and scripted
input.

```json
{
  "id": "bench.asteroid_field_chase",
  "scene": "scene.stage2_flight",
  "durationSeconds": 10,
  "camera": "chase",
  "player": { "systemPosition": [x, y, z], "rotationDeg": [x, y, z] },
  "autofly": { "throttle": 0.35, "rotation": [pitch, yaw, roll] }
}
```
Run it with `farhorizon --benchmark bench.asteroid_field_chase --no-vsync`. Output goes to
`benchmarks/<id>.csv` (per frame) and `benchmarks/<id>.json` (summary).

Optional fields:
* `"startIn": "ship" | "station"` starts docked and on foot (interior benchmarks), and
  `"lookTurnDegPerSec"` pans the view.
* `"autoDock": true` requests docking and engages the approach autopilot. The run ends 2 s after
  the ship is clamped.

`"startLanded": true` starts landed at the first site; `"autoLand": true` requests a landing immediately; `"startIn": "surface"` starts on foot at the site.
`"mine": true` targets the nearest asteroid, faces it and holds the laser.

Shipped benchmarks: `bench.planet_orbit`, `bench.landing`, `bench.surface_landed`, `bench.surface_outpost`, `bench.mining`, `bench.empty_space_cockpit`, `bench.asteroid_field_chase`,
`bench.station_approach`, `bench.docking_autopilot`, `bench.ship_interior`, `bench.station_hangar`.
