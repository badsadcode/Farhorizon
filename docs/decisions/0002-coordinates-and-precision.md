# ADR 0002 — Coordinate frames and precision

**Status:** accepted · **Date:** 2026-11-04

## Decision
* **SystemPosition** (`engine/math/frames.h`) is the authoritative, persistent position. It stores
  `double` metres in the star-system frame, with the primary star normally at the origin.
* **LocalFrame** is a floating origin (double) near the active play area. Rendering and close-range
  simulation use `float` positions relative to it.
* **Rebasing:** when the focus (currently the camera; the player ship from Stage 2) is farther than
  `rebaseDistance` (scene data, default 4096 m) from the origin, the origin jumps to the focus. The
  returned float delta is added to *every* active local transform in the same frame.
  `LocalFrame::generation()` counts rebases. Future TAA/motion-vector history must reset when it
  changes.
* **Camera-relative rendering:** the renderer only ever sees `objectLocal − cameraLocal` and a
  rotation-only view matrix. Precision near the camera is therefore independent of both the
  absolute system position and the local-frame offset.
* Distant bodies (the star today, planets later) are drawn as directions and analytic sky
  elements, never as gigantic meshes in the near scene.

## Evidence (unit tests, `tests/unit/test_frames.cpp`)
* A float alone cannot represent 1 m steps at 1 AU (spacing ≈ 16 km). The test pins this fact.
* Local↔system round trip at 1 AU is accurate to under 1 mm.
* Rebasing preserves the system position of all active objects to under 1 mm.
* A 2,000 km flight with more than 100 rebases ends within 1 m of the exact analytic position.
* In-app check: a free-fly at about 35 km/s triggered 44 rebases with no visible jump and the camera
  stayed within 4 km of the origin.
