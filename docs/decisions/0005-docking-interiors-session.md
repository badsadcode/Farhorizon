# ADR 0005 — Docking, walkable interiors and the GameSession

**Status:** accepted · **Date:** 2026-11-04

## Context
Stage 3 needs docking authorisation and capture, a scene swap into compact interiors, an on-foot
controller, seat and airlock interactions, a walkable ship cabin and a station hangar. The exit
test: fly → dock → leave seat → exit ship → return → launch, 20 times, with no crash or invalid
state (design doc 15).

## Decisions
* **Docking is a deterministic state machine** (`game/space/docking.*`):
  Undocked → RequestingClearance → ApproachAuthorized → Capture → Docked → Undocking → Undocked.
  * Capture fires only inside data-defined tolerances (distance to the bay, speed, nose and up
    alignment). The ship is then *settled by script* (smoothstep over 2.5 s) and clamped. Contacts
    never complete a dock (doc 5.2).
  * Invalid approaches raise HUD warnings (red readouts), never a game over. Clearance expires if
    the pilot leaves the control zone.
  * An optional **approach autopilot** (K) uses kinematic guidance: climb to the approach
    height, traverse above the station, descend the column at a speed under the capture limit.
    It assumes an approach from above or the side. Approaching from directly below is a known
    limitation.
  * The landing bay is a **roof pad** on the station proxy. The station collides as an oriented box
    (`colliders` on props), so the ship can land on the roof without a mesh collider.
  * While Capture/Docked/Undocking, the player ship is kinematic and its collisions are off (the
    pad pose is authoritative).
* **Interiors are separate local scenes** (`content/interiors/*.interior.json`). There was no
  walking inside a moving ship at first (doc 5.1); ADR 0015 changed that. An interior is built from **piece templates** (a mesh plus
  box colliders in tile units) placed on a 90° grid, plus authored colliders, spawn points,
  interactables (`takeSeat`, `exitShip`, `boardShip`, `terminal`) and glow lights. A station
  interior can show the player's **parked ship** with a walk-blocking box.
* **On-foot controller** (`game/player/character_controller.*`): a capsule approximated by three
  stacked spheres doing collide-and-slide against AABBs, with gravity, a jump, and sub-stepping
  so it never tunnels through thin walls. Deterministic and unit-tested.
* **GameSession** (`game/session/game_session.*`) is the authoritative gameplay state above the
  space world: docking, the player's **Location** (Cockpit / ShipInterior / StationInterior),
  the active interior and character, and the interaction rules. Leaving the seat and using the
  airlock are refused unless docked. Hot reload is only allowed from the seat.
  `world_builder` builds the world from scene data with visual callbacks, so the **headless
  integration test and the game build the same world**.
* **Rendering interiors** reuses the forward renderer: a key light (the "sun" slot), fill, rim,
  hemispheric ambient, emissive panels and glow sprites. There are no point lights or shadows yet.
  The sky is optional (hangar bay opening, ship windows) with the star glare disabled.

## Verification
* `stage3_dock_walk_launch_loop_20_times` (CTest) runs the real content: autopilot docking, the
  character physically walking the cabin corridor and the hangar floor, every interaction, then
  undocking and flying away. 20 cycles, 0 collisions, with invariants checked every step.
* An interactive run in the real app with keyboard/mouse (xdotool) did seat → cabin → airlock →
  hangar → board → seat → undock with 0 validation messages.

## Consequences / follow-ups
* Interior lighting is flat without point lights or shadows (design 9.3 step 5).
* Station-to-interior mapping is per prop. Several bays or hangars per station can come later.
* There are no animated doors or seat animations; transitions are instant cuts (allowed by doc 5.1).
