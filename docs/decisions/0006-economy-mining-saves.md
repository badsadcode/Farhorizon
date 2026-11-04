# ADR 0006 — Economy, mining and save games (Stage 4)

**Status:** accepted · **Date:** 2026-11-04

## Economy (`game/economy/economy.*`)
* **Commodities** are data (`content/economy/commodities.json`): id, name, category, base
  price, mass per unit, legality, description. Cargo is measured in **mass tons** (doc 6).
* **Markets** (`content/economy/markets/*.json`) hold per-commodity stock, target stock, max
  stock, production/consumption per hour, elasticity, price clamps, and whether the station buys
  and/or sells. The price follows the design formula, clamped:
  `mid = base × clamp(1 + elasticity·(target − stock)/target, min, max)`, buy = mid·(1+spread/2),
  sell = mid·(1−spread/2).
* **Anti-exploit by construction:** every unit of a trade is priced after the previous unit moved
  the stock (slippage), the spread has a data-validated floor (≥ 2 %), stocks are bounded, and
  markets refuse to buy past max stock. A buy→sell round trip always loses money (unit-tested).
* **Deterministic background updates:** markets advance in whole ticks of world time only when
  touched or saved. Nothing runs per frame, and no NPC traders are simulated. Long absences are
  capped and converge.

## Mining (`game/space/mining.*`)
* Asteroids get a primary resource, a yield in units and a hardness, derived deterministically
  from the field seed in their **own RNG stream**, so tuning composition never moves rocks.
* The laser needs a targeted asteroid in range and inside an aim cone that widens with the rock's
  angular size. Progress accumulates at `unitsPerSecond / hardness`; each unit detaches an
  **ore chunk** from the facing surface. A scoop field draws chunks in and the hold takes them if
  it has room. Chunks are hard-capped (count and lifetime). We chose chunks over direct cargo
  conversion because they read better and give the scoop a job.
* Scanning (R) reveals resource, remaining yield and hardness after a short delay.

## Save games (`game/persistence/save_game.*`)
* A versioned JSON snapshot (`schemaVersion` 1): scene, world time, location (cockpit, ship or
  station), docked station and bay, ship pose and velocity, boost energy, credits, cargo, market
  states, and **only the asteroids whose yield changed** (deviations from the procedural baseline,
  doc 10).
* Written atomically (`.tmp` then rename) with the previous file kept as `.bak`. Corrupted JSON,
  missing fields, unknown locations, negative cargo and unsupported versions are refused with a
  message. Saves are never half-applied silently.
* A docked ship's position is saved from the station's double-precision position plus the bay
  offset, which makes snapshots origin-independent. **Save → load → save is byte-identical**
  (unit-tested), so reloading cannot duplicate anything.
* Saving on foot restores the interior at the location's standard spawn (doc 13).
* **Hot reload and loading share one path:** capture/read a snapshot → rebuild the world from
  content → apply. F7 quicksave, F8 load, autosave on every docking, `--load`/`--continue`.

## Verification
`stage4_profitable_mining_trip_save_reload` (CTest): undock, mine nickel until the hold is full,
autopilot-dock, sell at a profit, buy rations, save twice (backup created), load into a fresh
session, and compare credits, cargo, docking, location, asteroid yield and market stock. Then
check that the snapshot is identical and that a second load does not duplicate cargo.
