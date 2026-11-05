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
| **Reflection Probe** | Makes shiny surfaces in a room reflect that room. See [Shiny surfaces](#shiny-surfaces) |
| **Enemy Spawn** | An enemy sleeps here, facing the way the entity faces. Set `fh_kind` to `crawler` (default), `spitter` or `brute` (the types in `content/foot/enemies.json`). Any map with one gives the player their guns |
| **Loot** | A pickup. `fh_kind` names a loot table from `content/foot/loot.json`: `salvage` (default), `medical`, `ammo`, `cache_flechette`, `cache_arc`. The validator rejects unknown names |

**Prompt** is what the player sees, and **Reach** is how close they must be.

### Lights
Use normal Blender lights.

| Light | In the game |
|---|---|
| **Point** | A lamp. 50 to 500 W suits a room |
| **Sun** | Real sunlight with shadows, for example through a hull breach. One per map. Turn on **Space Outside** too |
| **Spot** | Currently drawn as a Point light |
| **Area** | Does not export. The Check warns you |

Up to 16 lights are on at once and all of them cast shadows. Beyond 16, the ones that matter most where the player stands are used, so a big map can have many lights
as long as they are spread out. For a failing lamp, select it and press **Make It Flicker**. A lamp does
not glow by itself: give its fixture an emissive material.

### Emissive surfaces light the room
A material with Emission lights its surroundings in its own colour, so a screen or a glowing strip needs
no extra lamp. Brightness comes from **Emission Strength** and the surface's size, as in Blender: strength
1 to 10 suits screens and panels, and a bigger surface gives more light. Long strips become several lights.
Very small ones only glow and light nothing, so indicator lights do not use up the 16 lights.

A surface within 1.5 m of a real lamp counts as that lamp's fixture and adds no light. For a fixture further
from its lamp, select it and turn off **Emission Lights the Room** in the **Selected** section, or you get
twice the light. A flat panel set against a wall lights the room but not
that wall, which is also what a real one does.

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
* Materials from Blender get none of the game's built-in grime, so what you set is what you see.

Images are packed into the exported file, so you only need to keep them for your own use.

## Shiny surfaces
Set **Roughness** low. Metallic 1 gives chrome and polished metal. Metallic 0 with a dark colour gives
water or wet floors, which reflect most at shallow angles. No texture is needed.

Then add a **Reflection Probe** to the room:
1. Put it in the middle of the room, roughly at head height. It takes its picture from there.
2. Use **Scale** to make its box match the room's walls, floor and ceiling. The default size is a start.
3. Press **Play in Game**.

Without a probe, shiny surfaces still reflect, from a small blurry view around the player. Up to 8
probes per map. Keep the boxes from overlapping much: where they do, the first one wins. Boxes always
line up with the world axes, so a rotated probe is treated as unrotated, and the Check warns you.

**What to expect:** probes are excellent on floors and walls in box-shaped rooms, and less exact in
round rooms. They are not true mirrors:
* The picture is taken once when the map loads, so moving things do not appear, and neither does the
  player. A flickering lamp is captured as steady.
* Curved and small objects look right. A big flat mirror will look slightly off near objects in the
  middle of the room.
* A lamp on a very polished surface (Roughness below about 0.06) shows only through its reflection, so
  give the fixture an emissive material or the lamp will not appear in it.

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

## Putting a map in the game world
For now this takes two small hand edits. Press Export first, so the `.glb` is in `content/maps/`.

1. In `content/asset_manifest.json`, add the map twice, once as the place to walk and once as what you see
   from outside:
   ```json
   "map.my_wreck":  { "type": "map",  "source": "maps/my_wreck.glb" },
   "mesh.my_wreck": { "type": "mesh", "source": "maps/my_wreck.glb" }
   ```
2. In `content/scenes/stage2_flight.scene.json`, add an object to `props`. Copy `station.derelict`, then set
   `id`, `name`, `mesh`, `systemPosition`, and `docking.interior`.

Things to know:
* `colliders` and the docking `bays` position are relative to the **centre of the model's bounding box**,
  with Blender's Z as up and Blender's Y as minus Z. `colliders` are simple boxes that stop ships, and
  the bay is where the ship sits, about 4 m above the surface it lands on.
* With no `market` or `services` in `docking`, the place is treated as a wreck: nobody answers the radio.
* From outside you see the same model as inside. An exterior model of its own also works: point `mesh` at it.
* The Player Start is where you arrive from your airlock, and an **Exit** returns you to the ship.
* `--start-at my_id` starts the game next to it, for testing.

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
