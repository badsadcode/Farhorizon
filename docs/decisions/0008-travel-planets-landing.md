# ADR 0008 — Travel drive, planets and landing sites (Stage 5)

**Status:** accepted · **Date:** 2026-11-04

## Travel drive (`game/space/travel_drive.*`)
* A state machine as in design doc 4.2: **Idle → Aligning → Spooling → Transit → Decelerating →
  Idle**. N cycles nav destinations (stations, asteroid fields, landing sites) and J engages or
  aborts.
* **Kinematic and exact:** the remaining distance is computed in double precision from the
  arrival point, and the speed profile is `min(max, sqrt(v_exit² + 2·a·remaining))`, so the ship
  arrives on the point at `exitSpeed`. A 7,000 km jump does well over a thousand floating-origin
  rebases; the test checks arrival within 50 m.
* **Refusals:** mass lock (within `massLockRange` of stations or field edges, or deep in a
  planet's gravity well), destinations closer than `minDistance`, and anything other than free
  flight. Spooling aborts if a mass lock appears.
* Collisions are ignored during transit (no physics at 250 km/s). The corridor is assumed clear.

## Planets (`shaders/planet.frag`, `PlanetDraw`)
* Planets and moons are **far-field impostors**: a fullscreen triangle scissored to the body's
  projected bounds; each pixel ray-traces a sphere **in units of the planet radius** (so float
  precision holds from 10,000 km to orbit) and shades procedural terrain, craters, a sun-lit
  terminator and an atmospheric limb or halo. They are never physics bodies (doc 4.3/5.3). They
  draw after the stars and before local geometry, with no depth.
* Planets don't rotate in the MVP. Each landing site is a lat/lon direction, with a surface point
  and an **orbit marker** (`orbitAltitude` above it) computed in double precision.

## Landing (`game/space/landing.*`)
* **Descent** (space, kinematic): from the current position down a corridor to 25 km above the
  site. Re-entry heat ramps up (post overlay, camera shake, engine roar), and a fade to "cloud deck"
  colour **hides the scene swap**. Doc 1.5 says not to promise seamless descent, and we don't.
* **SurfaceApproach → Landed** happen in the site's surface scene. The ship flies a scripted curve
  onto the pad. Meanwhile the space ship is parked (kinematic, hidden) at the orbit marker.
* **Launch:** SurfaceAscent → fade → **Ascent** in space from the orbit marker, climbing radially →
  normal flight. The orbit marker is the guaranteed return destination.
* **"Parked" = docked or landed:** leaving the seat, the airlock and the market work identically
  at stations and landing sites.

## Surface scenes
* Interior kind `surface` with a **procedural heightfield** (`game/world/terrain.*`):
  deterministic noise, flattened zones for the pad and buildings, and edges that rise into
  **cliffs that are the natural walkable boundary** (doc 5.3). The character controller stands on
  slopes with normal.y ≥ 0.57 and is blocked by steeper ones.
* An atmospheric **sky mode** (zenith/horizon/ground gradient, hazy sun, stars dimmed), dust fog,
  local gravity, sodium masts, and the outpost built from corridor pieces with its own market.

## Saves
Schema **v2** adds `landedPlanet`/`landedSite`. **v1 files are migrated** in place on load (no
landings existed). A landed ship is saved at its site's orbit marker (double), keeping snapshots
origin-independent. Liftoff and descent phases save as orbit/flight, never mid-corridor.

## Verification
`stage5_landing_launch_cycles_with_reload` (CTest), run 3 times:
1. Buy rations at Kestrel, undock, travel drive to Gale Ridge, land.
2. Walk across the terrain to the supply shed, sell the rations, buy cheap nickel.
3. Walk back, board, take the seat, launch.
4. Check the return is within 2 km of the orbit marker and above orbit altitude.
5. Travel drive home, autopilot docking, sell the nickel.

Cycle 2 saves while landed and continues from a freshly loaded session, checked byte-identical.
The run ends with credits above the start and 0 collisions. Separate tests cover travel-drive
rules and exact arrival, and terrain walkability.
