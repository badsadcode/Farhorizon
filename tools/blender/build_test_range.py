# Builds a small "test range" map through the FARHORIZON add-on, to try every weapon and enemy type:
#   blender -b --factory-startup --python tools/blender/build_test_range.py
# Writes content/maps/test_range.glb only. Play it with:  farhorizon --map content/maps/test_range.glb
# Everything here is generated (plain materials, no textures), so it is original and needs no files.
#
# Layout, walking forward (+Y) from the start:
#   1. Armoury: two tool caches (flechette caster, arc cutter) and an ammunition box.
#   2. Crates: two crawlers asleep among low cover, facing away from you.
#   3. A wall with one doorway off to the side (enemies have to path round it), then a raised platform
#      reached by a ramp, with two spitters on it.
#   4. A pillared hall with a brute, and a medical kit at the far end.
import math
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import farhorizon_editor as editor  # noqa: E402

editor.register()
bpy.ops.wm.read_factory_settings(use_empty=True)
s = bpy.context.scene.farhorizon
s.mode = "MAP"
s.name = "Test Range"
s.ambient = (0.03, 0.032, 0.036)
s.exposure = 1.1
s.fog_color = (0.01, 0.012, 0.014)
s.fog_density = 0.012


def material(name, color, metallic=0.2, roughness=0.6, glow=None, strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1)
    b.inputs["Metallic"].default_value = metallic
    b.inputs["Roughness"].default_value = roughness
    if glow:
        b.inputs["Emission Color"].default_value = (*glow, 1)
        b.inputs["Emission Strength"].default_value = strength
    return m


deck = material("range_deck", (0.16, 0.16, 0.15), 0.5, 0.55)
hull = material("range_hull", (0.23, 0.22, 0.2), 0.6, 0.5)
paint = material("range_paint", (0.35, 0.24, 0.07), 0.3, 0.6)
stripe = material("range_stripe", (0.55, 0.42, 0.06), 0.2, 0.55)
lamp_on = material("range_lamp", (0.9, 0.8, 0.6), 0.0, 0.4, (1.0, 0.82, 0.55), 6.0)


def box(name, centre, size, mat, solid=True):
    bpy.ops.mesh.primitive_cube_add(size=1, location=centre)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = size
    bpy.ops.object.transform_apply(scale=True)
    obj.data.materials.append(mat)
    if not solid:
        obj["fh_collide"] = "none"
    return obj


def lamp(name, location, watts, color=(1.0, 0.8, 0.6)):
    data = bpy.data.lights.new(name, "POINT")
    data.energy = watts
    data.color = color
    obj = bpy.data.objects.new(name, data)
    obj.location = location
    bpy.context.scene.collection.objects.link(obj)
    fixture = box(name + "_fixture", (location[0], location[1], location[2] + 0.12), (0.8, 0.2, 0.05), lamp_on, solid=False)
    fixture["fh_glow_only"] = 1


def entity(kind, location, rotation_z=0.0, **props):
    bpy.context.scene.cursor.location = location
    bpy.ops.farhorizon.add_entity(kind=kind)
    obj = bpy.context.active_object
    obj.rotation_euler[2] = rotation_z
    for key, value in props.items():
        obj[key] = value
    return obj


# ---- Shell: one long hall, 24 x 52 m, 6 m high --------------------------------------------------------
W, L, H = 24.0, 52.0, 6.0
box("floor", (0, L / 2 - 2, -0.1), (W, L, 0.2), deck)
box("ceiling", (0, L / 2 - 2, H + 0.1), (W, L, 0.2), hull)
box("wall_w", (-W / 2 - 0.1, L / 2 - 2, H / 2), (0.2, L, H), hull)
box("wall_e", (W / 2 + 0.1, L / 2 - 2, H / 2), (0.2, L, H), hull)
box("wall_s", (0, -2.1, H / 2), (W, 0.2, H), hull)
box("wall_n", (0, L - 1.9, H / 2), (W, 0.2, H), hull)

# 1. Armoury.
box("rack", (0, 2.9, 0.5), (4.2, 0.6, 1.0), paint)  # within reach of the start: no walking needed
box("rack_stripe", (0, 2.59, 0.85), (4.2, 0.02, 0.08), stripe, solid=False)

# 2. Crates as low cover.
for i, (x, y) in enumerate(((-6, 11), (-1.5, 13), (4, 10.5), (7, 14.5), (-4, 16))):
    box(f"crate_{i}", (x, y, 0.6), (1.8, 1.8, 1.2), paint if i % 2 else hull)

# 3. A dividing wall at y = 20 with a single doorway at the west end (x -10.5 .. -8.5).
box("divider_e", (1.75, 20, H / 2), (20.5, 0.4, H), hull)
box("divider_w", (-11.25, 20, H / 2), (1.5, 0.4, H), hull)
box("door_frame", (-9.5, 20, H - 0.9), (2.0, 0.42, 1.8), hull)
# Raised platform (1.5 m) across the back of this section, with a ramp up on the east side.
box("platform", (0, 30.5, 0.75), (W, 5.0, 1.5), deck)
box("platform_lip", (0, 27.9, 1.6), (W - 6, 0.15, 0.2), stripe, solid=False)
ramp = box("ramp", (9.5, 25.5, 0.75), (3.0, 5.4, 0.15), deck)
ramp.rotation_euler = (math.atan2(1.5, 5.0), 0, 0)
box("platform_cover_a", (-5, 31, 2.1), (2.4, 0.5, 1.2), hull)
box("platform_cover_b", (4, 31, 2.1), (2.4, 0.5, 1.2), hull)

# 4. Pillared hall beyond the platform (the platform's north face drops back to the floor via steps).
for i in range(6):
    box(f"step_{i}", (0, 33.3 + i * 0.3, 1.5 - (i + 1) * 0.25 + 0.125), (6, 0.3, 0.25), deck)
for i, (x, y) in enumerate(((-6, 38), (6, 38), (-6, 45), (6, 45))):
    box(f"pillar_{i}", (x, y, H / 2), (1.2, 1.2, H), hull)

for i, (x, y) in enumerate(((0, 3), (-6, 12), (6, 13), (-8, 24), (6, 30), (0, 41), (0, 47))):
    lamp(f"lamp_{i}", (x, y, H - 0.4), 520 if i != 4 else 380)

# ---- Entities -------------------------------------------------------------------------------------------
entity("player_start", (0, 0.5, 0))
entity("exit", (0, -1.6, 1.3), fh_prompt="Leave the range")
entity("data_log", (-1.9, 2.9, 1.1), fh_text="Test range. Caches on the rack: take what you want. Doors north.")
entity("loot", (-1.3, 2.9, 1.0), fh_kind="cache_flechette")
entity("loot", (1.3, 2.9, 1.0), fh_kind="cache_arc")
entity("loot", (0, 2.9, 1.0), fh_kind="ammo")
entity("enemy_spawn", (-3, 16.5, 0), rotation_z=math.radians(10), fh_kind="crawler")  # facing away: sneak up
entity("enemy_spawn", (5.5, 17, 0), rotation_z=math.radians(-15), fh_kind="crawler")
entity("enemy_spawn", (-6, 31.5, 1.5), rotation_z=math.radians(180), fh_kind="spitter")
entity("enemy_spawn", (5.5, 30, 1.5), rotation_z=math.radians(170), fh_kind="spitter")
entity("enemy_spawn", (0, 44, 0), rotation_z=math.radians(180), fh_kind="brute")
entity("loot", (0, 48.5, 0), fh_kind="medical")
entity("loot", (-9, 47, 0), fh_kind="ammo")
bpy.context.scene.cursor.location = (0, 0, 0)

errors, warnings = editor.run_checks(bpy.context.scene)
for line in errors:
    print("CHECK ERROR:", line)
for line in warnings:
    print("CHECK WARNING:", line)
if errors:
    sys.exit(1)
result = editor.export_scene(bpy.context, os.path.join(ROOT, "content", "maps", "test_range.glb"))
print("EXPORTED:", result)
