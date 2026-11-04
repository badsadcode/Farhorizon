# Ship definition (`content/ships/*.json`)

| Field | Type | Req. | Notes |
|---|---|---|---|
| `id`, `name` | string | ✓ | `id` should equal the manifest ID |
| `mesh` | AssetID | ✓ | `mesh` asset (GLB/glTF) |
| `interior` | AssetID | | walkable cabin (`interior` asset) |
| `meshScale` | number | | uniform scale (default 1) |
| `meshRotationDeg` | [x,y,z] | | fixes exporter axes so the nose points −Z; applied X, then Y, then Z |
| `materialOverrides` | object | | by glTF material name: `baseColor` [r,g,b(,a)] linear, `metallic`, `roughness`, `emissive` [r,g,b] |
| `massTons`, `cargoCapacityTons`, `maxHull`, `maxShield` | number | ✓ | cargo is measured in mass tons |
| `collisionRadius` | number | | metres; 0 or missing = derived from mesh bounds |
| `flight.forwardAcceleration` / `reverseAcceleration` / `lateralAcceleration` | m/s² | ✓ / opt / ✓ | reverse defaults to forward × 0.5 |
| `flight.angularAcceleration` | rad/s² | ✓ | |
| `flight.maxAngularRateDeg` | [pitch,yaw,roll] °/s | | default [50,40,80] |
| `flight.maxCruiseSpeed` | m/s | ✓ | |
| `flight.strafeSpeedFraction`, `flight.maxReverseFraction` | number | | fractions of the speed cap |
| `precision.speedScale` / `accelerationScale` / `angularScale` | number | | docking mode scales |
| `boost.speedMultiplier` / `accelerationMultiplier` / `energyCapacity` / `drainPerSecond` / `regenPerSecond` / `minEnergyToStart` | number | | energy-limited burst |
| `cameras.cockpit` | `{ socket, fovDeg }` | | default socket `socket_pilot` |
| `cameras.chase` | `{ distance, height, positionLag, rotationLag, fovDeg }` | | `rotationLag` is the follow stiffness (1/s) |
| `sockets` | `{ name: { position, rotationDeg } }` | | ship-space fallbacks. glTF nodes named `socket_*` / `hardpoint_*` take precedence. |
| `engineGlow` | `{ sockets[], color, boostColor, idleSize, thrustSize, idleIntensity, thrustIntensity }` | | additive exhaust sprites; colours are linear HDR (>1 blooms) |
| `lights[]` | `{ position, color, size, blinkHz, phase, duty }` | | ship-space running lights |
| `mounts` | string[] | | hardpoint names (weapons arrive in Stage 6) |
| `travelDrive` | `{ maxSpeed, acceleration, spoolSeconds, alignToleranceDeg, alignRateDegPerSec, massLockRange, exitSpeed, minDistance }` | | in-system travel drive |
| `mining` | `{ range, unitsPerSecond, coneDeg, scoopRadius, maxChunks, chunkLifetime, chunkSpeed, scanSeconds }` | | mining laser and scoop |
