# Building for FARHORIZON in Blender

The FARHORIZON add-on turns Blender into the game's editor. You model, texture and light the way you
always do. The add-on adds entities with icons in the viewport, collision choices, a check for
anything that would not survive export, and a **Play in Game** button.

Open the sample to see it all in one place: `content_src/maps/derelict_sample.blend`.

## Install
Blender 4.2 or newer. Tested on 5.1.

```bash
python tools/blender/build_addon.py
```

In Blender: **Edit > Preferences > Get Extensions > arrow menu at top right > Install from Disk**, then
choose `tools/blender/farhorizon_editor.zip`. In the 3D viewport, press **N** and open the **FARHORIZON**
tab. The zip remembers where the game is. If you move the game, set **Game Folder** in the add-on's
preferences.

## The basics
* **1 Blender unit = 1 metre.** The player is 1.8 m tall and 0.7 m wide. **Add Scale Reference** gives
  you a player and a doorway to build around. They are never exported.
* **Forward is +Y.** A ship's nose points along +Y, and so does the arrow on a Player Start.
* **Anything with "Disable in Renders" on is left out of the export.** Use that for your own references.
* **Press Play in Game** to export and start the game there. Press it again to replace the running game
  with your changes.

## Making a map
1. Choose **Map**, and give it a name.
2. Model it. **Every mesh is solid as modelled**, including ramps and stairs, so there is no separate
   collision to build. Stairs climb well at 18 cm risers and 30 cm treads.
3. Add a **Player Start** at the 3D cursor (the panel, or **Shift+A > FARHORIZON**).
4. Add lights.
5. Press **Play in Game**.

### What a mesh is
Select meshes and choose in the panel's **Selected** section:

| Choice | Use it for |
|---|---|
| **Solid** (default) | Walls, floors, crates |
| **See Only** | Cables, small detail, lamp fixtures, anything that should not snag the player |
| **Invisible Wall** | Keeping players out of holes, or a simple box around a very detailed object |

### Entities
Each one shows as an icon with its name. They can be moved, rotated, duplicated and renamed freely.

| Entity | What it does |
|---|---|
| **Player Start** | Where the player appears, facing the arrow. Shows a player-sized outline |
| **Exit** | Using it leaves the map |
| **Data Log** | Shows a message when used. Write it in **Text** |
| **Terminal** | A station services terminal |
| **Pilot Seat**, **Airlock** | For ship interiors |
| **Enemy Spawn**, **Loot** | Placed and saved now, but nothing happens yet. They show as red and amber glows until those systems arrive |

**Prompt** is what the player sees, and **Reach** is how close they must be.

### Lights
Use normal Blender lights.

| Light | In the game |
|---|---|
| **Point** | A lamp. 50 to 500 W suits a room |
| **Sun** | Real sunlight with shadows, for example through a hull breach. One per map. Turn on **Space Outside** too |
| **Spot** | Currently drawn as a Point light |
| **Area** | Does not export. The Check warns you |

The 4 nearest lamps cast shadows and up to 8 light a surface at once, so a big map can have many lamps
as long as they are spread out. For a failing lamp, select it and press **Make It Flicker**. A light
does not glow by itself: give the fixture an emissive material.

### Map Settings
Gravity, ambient light, exposure, fog, and **Space Outside** (stars through any openings).

## Materials and textures
Use a **Principled BSDF** with image textures. All of this reaches the game:

| Input | Notes |
|---|---|
| Base Color | Image or colour |
| Metallic, Roughness | Image or value |
| Normal | Through a **Normal Map** node |
| Emission | Image or colour, plus Strength |

Set data images (normal, roughness, metallic) to **Non-Color**. Things to avoid:
* **Procedural nodes** (Noise, Musgrave and so on) cannot export. Bake them to images first. The Check
  names any it finds.
* **Transparency** is not drawn yet. Everything is opaque.
* A material with a base colour image gets none of the game's built-in grime, so what you paint is what
  you see. Materials without one do get it.

Images are packed into the exported file, so you only need to keep them for your own use.

## Making a ship
1. Choose **Ship Exterior**. Model with the nose along **+Y**.
2. Add the points the game looks for. Their names matter, so leave them as they are:

| Point | Meaning |
|---|---|
| **Cockpit Camera** (`socket_pilot`) | The cockpit view |
| **Engine left / right** (`engine_left`, `engine_right`) | Where exhaust and glow come out |
| **Gun left / right** (`hardpoint_left`, `hardpoint_right`) | Where shots start |
| **Airlock Door** (`socket_airlock`) | Reserved for boarding |

3. Press **Play in Game** to fly it straight away. For now this is a preview: it replaces the Sparrow's
   model for that one run, with the Sparrow's flight handling. Making a ship permanent still means
   pointing its ship data file (`content/ships/`) at the exported model.

## Ship interiors
An interior is a **Map** with a **Pilot Seat** and an **Airlock**. Build it now and walk it with Play in
Game. It cannot be attached to a ship yet: that arrives with walkable moving ships in the co-op work
(`docs/ROADMAP.md`). Build it around your exterior so they match: import the exterior, turn on
**Disable in Renders**, and model inside it.

## If something looks wrong
| Symptom | Likely cause |
|---|---|
| Black or plain surface | Procedural nodes, or a texture with no UV map. Press **Check** |
| Stuck at spawn | The Player Start is inside something, or too close to the floor. Raise it a few centimetres |
| Fall through the floor | The floor is set to See Only, or has no thickness and you crossed it at speed |
| Too dark | Lamps need hundreds of watts here, as they do in Blender. Also try Ambient Light |
| Shading looks inverted | Negative scale, or flipped normals. Apply scale and recalculate normals |
