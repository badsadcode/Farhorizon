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
