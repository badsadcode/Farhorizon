# Scene definition

| Field | Type | Notes |
|---|---|---|
| `id`, `name` | string | required |
| `exposure` | number | scene exposure multiplier (default 1) |
| `star.systemPosition` | [x,y,z] metres (double) | system primary, normally `[0,0,0]` |
| `star.color` | [r,g,b] linear | |
| `star.intensity` | number | directional light multiplier |
| `star.angularDiameterDeg` | number | apparent disc size |
| `starfield.seed` | integer | required if `starfield` is present; generation is deterministic on every platform |
| `starfield.count` | integer ≤ 200000 | |
| `starfield.brightness` | number | |
| `starfield.galacticPlaneTiltDeg` | number | orientation of the galactic band |
| `ambient.skyColor` / `ambient.groundColor` | [r,g,b] linear | hemispheric ambient: star-facing / away-facing |
| `floatingOrigin.rebaseDistance` | metres | see ADR 0002 |
| `camera.fovDeg`, `orbitDistance`, `yawDeg`, `pitchDeg` (elevation), `autoRotateDegPerSec` | numbers | initial developer camera |
| `objects[]` | `{ ship: AssetID, systemPosition: [x,y,z], rotationDeg: [x,y,z] }` | the first object anchors the initial floating origin |
