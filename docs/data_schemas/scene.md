# Scene definition (`content/scenes/*.scene.json`)

| Field | Notes |
|---|---|
| `id`, `name` | required |
| `exposure` | exposure multiplier applied before tonemapping |
| `star` | `systemPosition` (double metres), `color` (linear), `intensity`, `angularDiameterDeg` |
| `starfield` | `seed` (required), `count` ≤ 200000, `brightness`, `galacticPlaneTiltDeg` |
| `sky` | `baseColor`, `bandColor`, `bandIntensity`, `nebulaColor`, `nebulaStrength` (procedural sky colours) |
| `ambient` | `skyColor` / `groundColor` (hemispheric, star-facing / away), `fillDirection` + `fillColor` (soft coloured fill light), `rimColor` + `rimPower` (edge light) |
| `floatingOrigin.rebaseDistance` | metres, within [100, 100000] (ADR 0002) |
| `camera` | `initialMode` (chase, cockpit, orbit, free), plus developer camera `fovDeg`, `orbitDistance`, `yawDeg`, `pitchDeg` (elevation), `autoRotateDegPerSec` |
| `player` | `{ ship: AssetID, systemPosition, rotationDeg }`; the start pose also anchors the initial floating origin |
| `props[]` | `{ id, name, mesh, scale, systemPosition, rotationDeg, spinAxis, spinDegPerSec, collisionRadius, targetable, marker, materialOverrides, lights[] }`. `marker` is the HUD category: `station`, or `none` to hide. Light positions are in scaled prop space. |
| `asteroidFields[]` | `{ id, name, systemPosition, seed, count ≤ 20000, radius, thickness, clusters, minScale, maxScale, variants 1–16, detail 0–4, roughness, color }`. Rock meshes and placement are generated deterministically from the seed. |
