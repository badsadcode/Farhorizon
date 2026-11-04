# ADR 0012 — Blender as the map and ship editor

**Status:** accepted · **Date:** 2026-11-04

## Context
The owner works in Blender and wants to build maps, ships and points of interest there, with lights,
textures and entities, rather than learn a separate tool.

## Decisions
* **An add-on inside Blender, not a separate editor.** Modelling, texturing and lighting already work
  there. The add-on only adds what Blender does not know: entities, collision choices, checks and Play.
* **One standard `.glb` is the map.** Geometry, PBR materials, lights (`KHR_lights_punctual`) and
  entities (custom properties, exported as glTF `extras`) travel in one file written by Blender's own
  exporter. Rejected: a custom exporter (maintenance for no gain), and a sidecar JSON file (two files
  that can disagree). Blender 5 no longer gives the exporter a light's colour reliably, so the add-on
  stores it in `fh_color` for the moment of export.
* **Entities are Empties drawn with icons.** The add-on draws small original line icons, labels and a
  player-sized guide over each one. Entities stay ordinary Blender objects, so move, duplicate, undo and
  Outliner all work. Rejected: image Empties (they do not face the camera, and need image files).
* **Meshes are solid by default, with triangle collision.** Rejected: naming conventions like `COL_`,
  and box colliders. Both make the artist build everything twice, and a whole room as one object would
  seal the player inside its bounding box. `TriangleCollider` keeps a uniform grid and uses the same
  push-out as the box colliders. It is two-sided, and stairs work because the bottom capsule sphere rides
  over small steps.
* **Blender watts stay meaningful.** Light intensity is exported raw. The game multiplies by 0.05, so
  the 50-500 W a Blender user would pick for a room looks right. This is tuned by eye, not derived: the
  physical conversion would be about 0.025.
* **Normal maps use no stored tangents.** The frame comes from screen derivatives, so the vertex format
  and the glTF export stay simple. Quality suits hard-surface work; skinned characters may want real
  tangents later.
* **Material textures now have full mip chains and 8x anisotropic filtering** where supported.
* **Real textures replace procedural grime.** A material with a base colour image gets none of it.
* **The game's reader checks every export.** `content_validator --map` uses the same code as the game,
  and the add-on runs it after exporting. The sample map is built by a script through the add-on
  (`tools/blender/build_sample_derelict.py`), and unit tests load the result, so the whole path is
  tested. The script needs Blender, so the committed `.glb` is what CI tests.
* **Enemies and loot are placed before they exist.** They are saved with the map and shown as glows, so
  maps built now will not need rework.

* **Placed reflection probes are box-corrected cube maps** (0.8.1), one 512-pixel cube each, in an array
  of up to 8 sharing one descriptor. Each is captured once, one per frame, with the lamps nearest to it
  at steady strength and their own shadow pass. The lookup follows the reflected ray to the box wall.
  Rejected: re-rendering every frame (cost) and a global probe only (reflections slide). The box must
  fit the room, and floors and walls lie exactly on it, so the fade happens outside the box. The live
  camera probe remains for surfaces outside every box.
* **Materials from Blender skip procedural grime,** with or without textures, because authored values
  must survive: the grime clamped roughness to at least 0.3.

## Limits
* Everything visible merges into one mesh, so there is no per-object culling. Fine at this scale; big
  maps will want splitting.
* Maps run through `--map` and the add-on's Play. Nothing in the game world leads to one yet.
* No transparency. Spot lights act as point lights. Area lights do not export.
* Ship preview uses the Sparrow's flight data, and a ship made permanent needs a JSON file by hand.
* Triangle collision is not continuous: the character is sub-stepped, so very thin walls hold at normal
  speeds, and capped by the sub-step count at extreme ones.
