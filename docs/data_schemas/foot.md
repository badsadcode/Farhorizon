# On-foot weapons, enemies and loot (`content/foot/`)

Validated by `content_validator` (cross-references included). See ADR 0013 for how they behave.

## weapons.json (type `foot_weapons`)
`{ ammo: [ { id, name, maxReserve } ], weapons: [ … ], start: { weapons: [ids], ammo: { ammoId: n } } }`

| Weapon field | Notes |
|---|---|
| `id`, `name` | `name` is shown on the HUD |
| `mode` | `auto` (one hitscan shot per `fireInterval` while held), `pellets` (a cone of `pellets` shots, `spreadDeg`), `beam` (continuous) |
| `ammo` | an ammo id; reloading takes from that reserve |
| `damage` | per shot (auto), per pellet (pellets), per second (beam) |
| `fireInterval`, `ammoPerSecond` | seconds between shots / magazine units drained per second (beam) |
| `magazine`, `reloadSeconds`, `range`, `noiseRadius` | noise wakes sleeping enemies within the radius |
| `knockback` | m/s pushed into an enemy per hit (reduced by its `knockbackResistance`) |
| `mesh`, `muzzle` | first-person model (mesh id) and its muzzle point on the model |
| `tracerColor` | linear HDR colour of tracers, flash and beam |
| `sounds` | `{ fire, reload, dry, hit }` audio event names (beam: `fire` plays when it strikes) |

`start` is the loadout of a new game: each starting weapon comes with a full magazine, on top of the listed reserves.

## enemies.json (type `foot_enemies`)
`{ enemies: [ … ] }`. A map's Enemy Spawn names one with `fh_kind` (`crawler` = `enemy.crawler`).

| Field | Notes |
|---|---|
| `behaviour` | `melee`, `ranged` or `charger` |
| `health`, `speed`, `bodyRadius`, `bodyHeight` | body for collision and pathfinding (height decides which low spaces it fits under) |
| `hitRadius`, `hitCentreHeight` | the sphere shots hit |
| `sightRange`, `closeSenseRange`, `hearRunRange`, `forgetSeconds` | senses |
| `attackRange`, `attackReach`, `windupSeconds`, `attackCooldown`, `attackDamage` | melee lunge (every behaviour uses it up close) |
| `preferredRange`, `retreatRange`, `projectileSpeed`, `projectileDamage`, `projectileRadius`, `projectileGravity`, `aimSeconds`, `fireCooldown` | ranged |
| `chargeMinRange`, `chargeMaxRange`, `chargeWindup`, `chargeSpeed`, `chargeDistance`, `chargeDamage`, `chargeKnockback`, `recoverSeconds`, `recoverDamageScale` | charger |
| `knockbackResistance` | 0..1 |
| `dropTable`, `dropChance` | loot table rolled on death (0..1) |
| `visual`, `visualScale` | model set: `crawler`, `spitter`, `brute` |
| `sounds` | `{ wake, hit, die, attack }` |

## loot.json (type `foot_loot`)
`{ tables: [ { id, mesh, prompt, entries: [ { weight, credits: [min,max], ammo: { ammoId: [min,max] }, heal, weapon } ] } ] }`

A Loot entity's `fh_kind` names a table (`salvage` = `loot.salvage`). `mesh` is what lies on the floor:
`salvage`, `medical`, `cache` or `ammo`. One weighted entry is rolled per pickup, the same every time for that
map and marker. An entry with nothing in it is a valid "nothing". `weapon` unlocks a weapon and hands it to
you loaded.
