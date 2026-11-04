# Ship definition

| Field | Type | Req. | Notes |
|---|---|---|---|
| `id` | string | ✓ | Should equal the manifest ID |
| `name` | string | ✓ | Display name |
| `mesh` | AssetID | ✓ | Must be a `mesh` asset (GLB/glTF) |
| `meshScale` | number | | Uniform scale applied to the model (default 1) |
| `meshRotationDeg` | [x,y,z] | | Corrects exporter axis conventions; applied X, then Y, then Z |
| `materialOverrides` | object | | Keyed by glTF material name: `baseColor` [r,g,b(,a)] linear, `metallic`, `roughness`, `emissive` [r,g,b]. An unknown name produces a warning. |
| `massTons` | number | ✓ | > 0 |
| `cargoCapacityTons` | number | ✓ | Cargo is measured in mass tons (design doc 6) |
| `maxHull`, `maxShield` | number | ✓ | |
| `flight` | object | ✓ | `forwardAcceleration`, `lateralAcceleration`, `angularAcceleration`, `maxCruiseSpeed`. Parsed now, used from Stage 2. |
| `mounts` | string[] | | Hardpoint names. Socket support arrives with Stage 2+. |

Not yet parsed (planned): `interiorScene`, `airlockSocket`, `pilotSeatSocket`.
