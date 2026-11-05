# ADR 0015 — Walking around a ship while it flies

_Status: accepted (0.12.0, roadmap step 4.2). Replaces "no walking inside a moving ship" in ADR 0005._

## Decision
* **Characters stay in the place's own coordinates.** The ship's interior is an ordinary place (ADR 0014). Nothing
  in the character's simulation knows the ship moves, so a ship at any speed is still a level floor. Inertia
  isn't simulated, except that a crash adds a kick towards the bow.
* **Drawing is what changes.** While someone is aboard (and the ship isn't landed), the app keeps the flight scene,
  puts the eye at `ship pose * character eye`, skips the exterior hull, and draws the interior with the ship's pose.
  The cabin keeps its lamps and ambient light. The sun stays the real one, with shadows, so it enters by the windows.
* **Interiors of ships are Blender maps.** `interior.sparrow` now points at `interiors/sparrow_decks.glb`. A Pilot
  Seat and an Airlock define the `seat` and `airlock` spawns, one metre below the entity. Windows are gaps with an
  invisible collider, because materials can't be transparent yet (issue 48).
* **An empty helm is safe but not magic.** With no pilot the ship's input is zero, so flight assist brakes it, and
  the travel drive continues. Combat keeps running in open space with or without a pilot; before this, leaving the
  seat would have switched off pirates.
* **Who may leave:** anyone, except during docking or landing, which need a pilot.

## Consequences
* The interior doesn't match the exterior model's size or shape (issue 69).
* Placed reflection probes are skipped aboard, since their boxes assume a scene that doesn't move (issue 70).
* A network client only needs its ship-relative position; the ship's pose comes from the server. That suits step 4.3.
