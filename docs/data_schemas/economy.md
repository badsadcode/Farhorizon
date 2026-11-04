# Economy data

## Commodities (`content/economy/commodities.json`, type `commodities`)
```json
{ "schemaVersion": 1, "commodities": [
  { "id": "ore.iron", "name": "Iron Ore", "category": "ore", "basePrice": 28, "massTons": 1,
    "legal": true, "description": "..." } ] }
```

## Markets (`content/economy/markets/*.json`, type `market`)
| Field | Notes |
|---|---|
| `id`, `name` | |
| `spread` | buy/sell gap, 0.02–0.8 (the floor prevents round-trip exploits) |
| `tickSeconds` | world seconds per market update |
| `restockFraction` | per tick, fraction of (target − stock) recovered |
| `entries[]` | `{ commodity, stock, targetStock, maxStock, productionPerHour (± units/hour), elasticity, minMultiplier, maxMultiplier, buys, sells }` |

A station links its market through `docking.market` in the scene.

## Mining data
* Asteroid fields: `resources: [ { commodity, weight } ]`, `yieldPerMetre`, `hardnessMin`, `hardnessMax`.
* Ships: `mining: { range, unitsPerSecond, coneDeg, scoopRadius, maxChunks, chunkLifetime, chunkSpeed, scanSeconds }`.
* Scenes: `startingCredits`.

## Save files (`saves/*.json`)
These are not content but are documented for support: `schemaVersion` 3 (v1 and v2 migrated on load), `sceneId`, `worldTime`,
`player` { location, docked, dockedStation, dockedBay, landedPlanet, landedSite, shipPosition (double, system frame),
shipOrientation, shipVelocity, boostEnergy, credits, cargo, hull, shield (−1 = full), upgrades [ids], defeats },
`contracts` { active: [ { key, template, kind, title, description, issuer, destination, commodity, faction, units,
killsRequired, kills, reward } ], taken: [offer keys], completed }, `markets` { id: { lastTick,
stocks, carry } }, and `asteroids` [ { field, index, yield } ] (changed rocks only). The previous
save is kept as `<file>.bak`.
