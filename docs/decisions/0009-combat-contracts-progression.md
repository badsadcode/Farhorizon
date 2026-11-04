# ADR 0009 — Combat, contracts and progression (Stage 6)

**Status:** accepted · **Date:** 2026-11-04

## Combat core (`game/combat/`, GPU-free, unit-tested)
* **Durability:** shields absorb damage first (scaled by the weapon's `shieldMultiplier`), the
  overflow goes to the hull (`hullMultiplier`). Shields recharge at `shieldRegen` once
  `shieldDelay` seconds pass without a hit. Collisions above 10 m/s deal
  `(speed − 10) × collisionDamagePerMs`.
* **Weapons** are data (`content/combat/weapons.json`): projectile speed, range, fire interval,
  spread, heat, damage multipliers, optional homing turn rate. Every shot adds heat. The ship
  dissipates heat continuously, and reaching capacity **locks the guns until heat drops below
  30%**, so you fire in bursts.
* **Projectiles** are simulated (not hitscan), so leading a target matters and the HUD shows a
  lead pip. Hits are segment-vs-sphere tests per fixed step against ships and asteroids, and
  rocks absorb fire. A hard cap of 400 projectiles keeps the cost bounded.
* **NPC ships fly the player's flight model** (`stepFlight`) with assist on. AI only produces
  stick and throttle input, so NPCs obey the same speed caps, acceleration and boost rules.
  Steering converts a desired world direction into pitch and yaw commands. NPCs push out of
  asteroids, like the player.
* **AI = small readable state machines** (design doc 8.2):
  * Pirate: Patrol (random points in its zone) → Intercept (detection range, lead pursuit,
    boost when far) → Attack (fire inside the aim tolerance, rockets at mid range,
    preferred distance) → Evade (2.5 s boost jink after 7 s or when too close) → Intercept…
    Below the flee threshold → Retreat (escapes once 5 km away and despawns quietly).
  * Trader: Route (waypoint loop) → Flee when the player shoots it → Route again after 12 s.
* **Only ships near the player exist** (design doc 7.3). An encounter zone fills itself (`count`
  ships at once) when the player comes inside `triggerRange`, never spawning closer than
  `spawnMinDistance`. Losses start a `respawnSeconds` timer. Traffic routes spawn civilians near
  their first waypoint. Ships far from the player despawn. There is no offscreen simulation.
* **Determinism:** a seeded splitmix RNG per combat instance and fixed-step updates. Tests
  replay exact fights.

## Session integration
* `Combat` lives in `GameSession`. It updates every fixed step, but the player is only "active"
  (targetable, able to fire, able to trigger spawns) in free flight from the cockpit. While
  docked, landed or in transit, pirates lose interest and go back to patrol.
* Ships are a new `TargetRef::Kind::Ship` (index = NPC id). `SpaceWorld` stays combat-agnostic
  and describes ships through a **describer callback** that the session installs.
* **Salvage** reuses the mining scoop: wrecks drop canisters through `Mining::addChunk`, so loot
  needs no new pickup code. Canisters blink cyan to tell them apart from ore.
* **Defeat** is not a game over. The insurer tows the ship to the nearest station with a repair
  service (`dockAt`), restores hull and shields, and charges `insuranceDeductible` (capped at
  the balance). Cargo is lost and courier contracts fail. Bounty and procurement contracts
  survive.

## Upgrades (`game/progression/`)
* Each upgrade is a one-off purchase with **additive / multiplicative effects** on a base
  `ShipDef`. `applyUpgrades(base, owned)` derives the ship you fly, and the session re-applies it
  after every change (hold capacity, mining rate, durability limits, armament heat and damage
  scale). Saves store only the owned ids. Unknown ids in a save are ignored with a warning, so
  removing an upgrade from content cannot break old saves.
* Station services (`repair`, `upgrades`, `contracts`) are declared per docking def or landing
  site. Kestrel has all three. Gale Ridge has repair and contracts.

## Contracts (`game/missions/`)
* Three kinds: **courier** (sealed cargo loaded on acceptance, paid on arrival), **procurement**
  (bring N units back to the issuer), **bounty** (confirm N kills of a faction, then report).
* **Offers are a pure function** of (location, world day, slot). Each slot is seeded by FNV-1a
  of `"location/day/slot"`, then templates are picked by weight with random units/kills and a
  rounded reward. Nothing about offers is stored. A save keeps the accepted contracts in full,
  the keys of taken offers, and the completion count. The board refreshes every `daySeconds`.
* Progress comes from events: `ShipDestroyed(byPlayer)` advances one matching bounty, and
  becoming **parked** (dock or land) at a location triggers turn-in. Contracts are never
  completed by a menu click, only by being there with the goods.

## Terminal and HUD
* The market panel became a tabbed terminal: MARKET | SERVICES | CONTRACTS. Tabs appear only for
  the services the location offers. Keys: Tab (next), Enter (take/fit/repair), Delete (abandon),
  Esc. `ui.close` no longer includes Tab.
* The combat HUD has shield/hull/heat meters with text labels, a weapon selector (1 guns /
  2 mining laser, LMB fires the selected system, RMB fires rockets), ship markers (**triangle =
  hostile, square = civilian**), target shield/hull bars, a lead pip, edge flashes (cyan shield,
  red hull) and blinking text warnings (MISSILE INBOUND, HULL CRITICAL, WEAPONS OVERHEATED).
  Colour is never the only cue. The contract tracker sits top-left.

## Saves v3
* v3 adds hull, shield, owned upgrades, active contracts, taken offer keys, completed count and
  defeat count. v2 files migrate by adding full durability and empty lists. Upgrades are
  restored before cargo, because they change hold capacity.

## Alternatives rejected
* **Hitscan lasers:** cheaper, but they remove the leading skill and make NPC accuracy binary.
* **Persistent offscreen NPC simulation:** out of MVP scope (design doc 7.3). Encounters respawn.
* **Separate pickup system for loot:** the scoop already handles drifting objects.
* **Stored contract boards:** generating from (location, day) is cheaper and cannot desync.
