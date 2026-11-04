# Interior definition (`content/interiors/*.interior.json`, type `interior`)

| Field | Notes |
|---|---|
| `id`, `name` | required |
| `kind` | `ship` (needs spawns `seat` and `airlock`) or `station` (needs spawn `fromShip`) |
| `gravity` | m/s² for the on-foot controller |
| `showSky` | draw the starfield/sky behind the geometry (windows, open bay) |
| `lighting` | `keyDirection`, `keyColor`, `ambientSky`, `ambientGround`, `fillDirection`, `fillColor`, `rimColor`, `rimPower`, `exposure`, `fogColor`, `fogDensity` (per metre) |
| `materialOverrides` | applied to every piece mesh (unmatched names are ignored here) |
| `templates` | `{ name: { mesh: AssetID, colliders: [ {min,max} ] } }`; collider units are the mesh's own (Kenney tiles = 1) |
| `pieces[]` | `{ template, position (m), yawDeg (multiple of 90), scale }` |
| `colliders[]` | extra `{min,max}` boxes in interior space (floors, walls, force fields) |
| `lights[]` | `{ position, color, size, blinkHz, phase, duty, range, intensity, flicker }`. `range > 0` also makes it a point light (8 nearest are used); `flicker` 0..1 makes it a failing lamp |
| `spawns` | `{ name: { position (feet), yawDeg } }`; yaw 0 looks down −Z |
| `interactables[]` | `{ id, action, prompt, position, radius }`; actions: `takeSeat`, `exitShip`, `boardShip`, `terminal` |
| `parkedShip` | station only: `{ position, yawDeg, colliderHalfExtents }`; draws the player's ship |
