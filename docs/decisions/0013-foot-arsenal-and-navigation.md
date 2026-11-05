# ADR 0013 — Weapons and enemies as content, navigation grid, loot tables

**Status:** accepted · **Date:** 2026-11-05

## Content, not constants
* On-foot weapons, ammo types, enemy types and loot tables live in `content/foot/{weapons,enemies,loot}.json`
  (manifest ids `foot.weapons`, `foot.enemies`, `foot.loot`). `loadFootContent` loads all three and checks
  every reference: weapon → ammo and mesh, loot → weapon and ammo, enemy → drop table. The validator also
  checks that every map marker names an enemy type or loot table that exists.
* `builtinFootContent()` reproduces the original rivet driver and crawler exactly. `FootCombat` runs on it when
  nothing is configured, so tools and older tests keep working without content files.

## Weapons
* Three fire modes cover very different guns with one code path. **Auto** fires one hitscan shot per interval.
  **Pellets** fires a cone of hitscan pellets from a seeded generator, so a fight replays exactly. **Beam** is
  continuous: it applies damage per second and drains magazine units per second, with hit events throttled to
  about 8 Hz for sounds and the hit marker.
* Loadouts are vectors indexed like the content: owned weapons, magazines per weapon, ammo per type. Changing
  weapons takes 0.4 s and cancels a reload. Saves store everything by content id, so reordering or removing
  content never breaks a save; unknown ids are dropped.

## Enemies
* Three behaviours as small state machines, sharing sight, hearing, forgetting and the melee lunge:
  * **Melee:** chase, wind up, strike if still in reach.
  * **Ranged:** keep between `retreatRange` and `preferredRange`, aim, then throw a ballistic projectile.
    It aims above the target to allow for the drop but does **not** lead, so stepping aside dodges it, by design.
  * **Charger:** a wind-up, then a straight-line charge with a fixed direction and distance. A hit
    deals damage and knockback (applied by the session to the character's velocity). Afterwards the
    charger is stunned and takes extra damage (`recoverDamageScale`). It only charges when the lane is walkable.
* Running players are heard within `hearRunRange`, through walls.

## Navigation grid (`game/foot/nav_grid.*`)
* Baked per map on first entry, from the map's collision mesh. Every 0.5 m column is raycast top-down, and
  each floor surface with at least 0.85 m headroom becomes a node, so stacked floors (catwalk over a hold)
  are separate nodes. Neighbours link when the height step is ≤ 0.5 m and nothing blocks the way at knee
  height. Diagonals only link where both straight neighbours do (no corner cutting). Nodes near walls cost
  more, which keeps paths off the walls.
* A* respects agent height (a brute cannot path under a low shelf a crawler can use). Paths are then
  straightened by string pulling with `walkableLine`. A straight walk counts only if it stays on floor, hits
  no wall and **ends on the target's floor**, which stops a path to a deck jumping to the floor beneath it.
* Enemies walk straight while `walkableLine` says they can (checked every 0.25 s), otherwise they follow a
  path that is refreshed every 0.6 s or when the goal moves 1.5 m. The old stuck-and-sidestep fallback stays
  for things the grid does not know about (other enemies, small props).
* Baking needed a fast raycast. `TriangleCollider::raycast` became a 3D DDA through its grid cells with a
  per-call stamp array instead of sampling 27 cells per step with a hash set. That cut the test derelict's bake
  from 20.7 s to 0.2 s (debug build), and every gameplay raycast got faster with it.

## Loot
* A loot marker's `fh_kind` names a table (`salvage` means `loot.salvage`). One weighted entry is rolled, seeded
  by FNV-1a of `"<map>#<marker>"`, so a reload cannot reroll it.
* An enemy drop is decided by the same kind of seed (`"<map>#drop<marker>"`). Its position is saved until it
  is taken, so drops survive leaving the map and reloading.

## Alternatives rejected
* **A navigation mesh (polygons) instead of a grid:** smoother paths and less memory, but much more code to
  build robustly from arbitrary artist geometry. A 0.5 m grid with string pulling gives near-straight paths and
  bakes in milliseconds on today's maps. Revisit it for very large maps.
* **Hitscan for enemies too:** globs you can see and dodge make ranged enemies fair in dark rooms.
