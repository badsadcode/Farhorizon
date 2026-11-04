# Combat, upgrades and contracts (Stage 6)

## Weapons (`content/combat/weapons.json`, type `weapons`)
`{ "weapons": [ { id, name, damage, fireInterval, heatPerShot, projectileSpeed, range, spreadDeg,
shieldMultiplier, hullMultiplier, homing, boltLength, color, sound } ] }`

| Field | Notes |
|---|---|
| `damage` | per hit, before the multipliers (and the shooter's upgrade damage scale) |
| `fireInterval` | seconds between shots **per mount** |
| `heatPerShot` | added to the ship's heat; at `heatCapacity` the guns lock until heat < 30% |
| `projectileSpeed`, `range` | m/s and metres (lifetime = range / speed); shots inherit the shooter's velocity |
| `spreadDeg` | random cone |
| `shieldMultiplier`, `hullMultiplier` | damage against each layer (lasers strip shields, slugs and rockets punch hull) |
| `homing` | turn rate in rad/s toward the shooter's locked target (0 = dumb-fire) |
| `boltLength`, `color` | visual streak length (m) and linear HDR colour |
| `sound` | audio event name fired on each shot |

## Upgrades (`content/economy/upgrades.json`, type `upgrades`)
`{ "repairPricePerHull": 3, "upgrades": [ { id, name, description, price, effects } ] }`. Each upgrade
can be fitted once. Known `effects` (others are errors): `cargoTons`, `maxHull`, `maxShield`,
`shieldRegen`, `heatCapacity` (added) and `accelerationScale`, `speedScale`, `turnScale`,
`miningRateScale`, `weaponDamageScale` (multiplied).

## Contracts (`content/missions/contracts.json`, type `contracts`)
`{ offersPerLocation, maxActive, daySeconds, templates: [ … ] }`

| Template field | Notes |
|---|---|
| `id`, `kind` | `courier`, `procurement` or `bounty` |
| `title`, `description` | placeholders `{units} {commodity} {dest} {issuer} {kills} {faction}` |
| `issuers[]` | location ids (station prop ids / landing site ids); empty = everywhere |
| `destinations[]` | courier only; empty = any other location |
| `commodities[]` | procurement choices; courier cargo (usually `cargo.sealed`) |
| `unitsMin/Max`, `killsMin/Max` | ranges |
| `faction` | bounty target faction (matches a ship's `ai.faction`) |
| `rewardBase`, `rewardPerUnit`, `priceMultiplier`, `rewardPerKill` | reward = base + units·perUnit + units·basePrice·priceMultiplier + kills·perKill, rounded to 5 |
| `weight` | relative chance per offer slot |

Offers are generated from (location, world day, slot). Courier cargo is loaded on acceptance
and paid on arrival at the destination. Procurement and bounty contracts are paid when you
dock or land at the issuer with the goods or the kills.
