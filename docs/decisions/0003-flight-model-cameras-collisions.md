# ADR 0003 — Flight model, cameras and collisions (Stage 2)

**Status:** accepted · **Date:** 2026-11-04

## Flight model (`game/ships/flight_model.*`)
* **Custom kinematic 6DOF integration** (design doc 9.4), not a rigid-body solver. State is
  position, velocity, orientation and body angular velocity. It is stepped at the fixed 60 Hz
  simulation rate with semi-implicit Euler. The code is deterministic, GPU-free and unit-tested
  (`tests/unit/test_flight.cpp`).
* **Flight assist ON (default):** the throttle is a *set-speed*. Strafe and rotation inputs are
  *rate commands*. Every body axis approaches its commanded velocity, limited by that axis's
  acceleration from ship data (forward, reverse, lateral, angular), so released axes damp to zero.
* **Flight assist OFF:** thrusters accelerate directly and linear momentum is kept. Rotation keeps
  rate damping for now, because fully Newtonian rotation is close to unflyable with a mouse. It can
  become an option later.
* **Speed cap:** game-friendly caps from ship data. Precision mode scales speed, acceleration and
  turn rate down for docking. Above the cap (after a boost) speed bleeds off at the reverse
  acceleration rate instead of snapping.
* **Boost:** an energy-limited burst (`boost.*` in ship JSON). After the energy runs out the pilot
  must release and press again, which prevents stutter-boosting while the key is held.
* All tuning lives in `content/ships/*.json`. No flight constants are in gameplay code apart from the
  defaults used for missing optional fields.

## Input
* Mouse steering uses a **virtual stick**: relative mouse motion moves a point inside a unit
  circle, and its deflection (with a deadzone) is the pitch/yaw command. The HUD shows the stick.
  Sensitivity, invert-Y, deadzone and optional auto-recentre live in `content/config/input.json`.
* Keyboard axes add to the stick. All keys are action bindings in the same file and reload with F5.
* Esc releases or captures the mouse. Quit moved to F10 so Esc can't close the game by accident.

## Cameras (`game/camera_rig.*`)
* **Chase:** position is rigidly attached to the ship (no lag at any speed). Rotation follows the
  ship with exponential smoothing, which gives a sense of swing on turns. There is a small
  speed/boost pull-back and the FOV widens during boost. Parameters come from ship JSON.
* **Cockpit:** sits at the `socket_pilot` socket. Sockets come from glTF nodes named `socket_*` or
  `hardpoint_*` when present, otherwise from the ship JSON `sockets` fallback (in ship space).
* **Developer orbit/free:** F2. These cameras also use local-frame floats and the free camera is
  shifted on rebases.

## Floating origin
The rebase focus is now the **player ship** (ADR 0002). Static bodies keep their double-precision
`SystemPosition` and have their local positions recomputed on every rebase (no float drift).
Moving local-space state (ship current/previous state, free camera) gets the float delta.
Render interpolation stays continuous across a rebase, which a unit test checks.

## Collisions
* Stage 2 uses **sphere-vs-sphere** contacts between the ship and asteroids/props. The response is
  push-out plus restitution and friction, with an impact log and HUD warning. Brute force is fine
  for a few hundred bodies. A broadphase grid is a trivial follow-up if counts grow.
* Station proxies use a single bounding sphere. Real docking (Stage 3) needs per-part colliders
  and scripted capture, which are tracked in `docs/ISSUES.md`.
