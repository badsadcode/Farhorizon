# Builds the sample derelict through the FARHORIZON add-on, the same way an artist would, and saves it:
#   blender -b --factory-startup --python tools/blender/build_sample_derelict.py
# Writes only the test fixture, tests/data/derelict_fixture.glb. It never touches content/ or content_src/,
# because the maps there are the owner's working files.
# Everything is generated here, so the sample is original and has no outside files.
import math
import os
import random
import sys

import bpy
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import farhorizon_editor as editor  # noqa: E402
import map_kit  # noqa: E402
from map_kit import box, entity, glow_material, lamp, plain_material, textured_material  # noqa: E402

editor.register()
rng = random.Random(4417)
np_rng = np.random.default_rng(4417)

bpy.ops.wm.read_factory_settings(use_empty=True)
editor_scene = bpy.context.scene
s = editor_scene.farhorizon
s.mode = "MAP"
s.name = "Derelict Sample"
s.ambient = (0.018, 0.021, 0.025)
s.exposure = 1.2
s.fog_color = (0.008, 0.011, 0.012)
s.fog_density = 0.02


# ---- Textures, all generated --------------------------------------------------------------------

hull = textured_material("hull_plate", (0.62, 0.66, 0.68), (0.36, 0.31, 0.25), 3, 0.55, 0.3, 10)
deck = textured_material("deck_grate", (0.5, 0.5, 0.48), (0.3, 0.25, 0.19), 6, 0.6, 0.3, 20)
paint = textured_material("hazard_paint", (0.8, 0.55, 0.12), (0.36, 0.26, 0.12), 2, 0.7, 0.1, 30)


chrome = plain_material("polished_steel", (0.82, 0.84, 0.86), 1.0, 0.04)
standing_water = plain_material("standing_water", (0.012, 0.014, 0.016), 0.0, 0.03)
lamp_on = glow_material("lamp_warm", (1.0, 0.72, 0.42), 1.3)
screen = glow_material("screen_green", (0.15, 1.0, 0.45), 0.9)
strip_blue = glow_material("strip_blue", (0.1, 0.25, 1.0), 2.0)


# ---- Geometry helpers ---------------------------------------------------------------------------

map_kit.fixture_material = lamp_on


# ---- The derelict: a corridor, a stairwell up to a catwalk, a breached cargo bay --------------------

WALL = 0.2
H = 3.0

# Long corridor along +Y.
box("corridor_floor", (0, 8, -0.1), (4, 18, 0.2), deck)
box("corridor_ceiling", (0, 8, H + 0.1), (4, 18, 0.2), hull)
box("corridor_wall_w", (-2.1, 8, H / 2), (WALL, 18, H), hull)
box("corridor_wall_e", (2.1, 8, H / 2), (WALL, 18, H), hull)
box("corridor_back", (0, -1.1, H / 2), (4, WALL, H), hull)
box("corridor_stripe_w", (-1.98, 8, 0.55), (0.03, 18, 0.5), paint, solid=False)
box("corridor_stripe_e", (1.98, 8, 0.55), (0.03, 18, 0.5), paint, solid=False)
for i in range(5):  # ribs make the corridor read as a structure, and catch the light
    y = 0.5 + i * 3.4
    box(f"rib_w_{i}", (-1.9, y, H / 2), (0.25, 0.3, H), hull)
    box(f"rib_top_{i}", (0, y, H - 0.14), (4, 0.3, 0.28), hull)
    box(f"rib_e_{i}", (1.9, y, H / 2), (0.25, 0.3, H), hull)

lamp("lamp_a", (0, 2.1, H - 0.3), 110)
lamp("lamp_b", (0, 8.9, H - 0.3), 70, flicker=0.5)

# Cargo bay beyond the corridor's far end, open to space through a breach in the far wall.
BAY = 26
box("bay_floor", (0, BAY, -0.1), (16, 18, 0.2), deck)
box("bay_ceiling", (0, BAY, 8.1), (16, 18, 0.2), hull)
box("bay_wall_w", (-8.1, BAY, 4), (WALL, 18, 8), hull)
box("bay_wall_e", (8.1, BAY, 4), (WALL, 18, 8), hull)
box("bay_front_w", (-5.05, 17, 4), (6.1, WALL, 8), hull)
box("bay_front_e", (5.05, 17, 4), (6.1, WALL, 8), hull)
box("bay_front_top", (0, 17, 5.5), (4, WALL, 5), hull)
box("bay_back_w", (-6, 35, 4), (4, WALL, 8), hull)
box("bay_back_e", (6, 35, 4), (4, WALL, 8), hull)
box("bay_back_low", (0, 35, 1), (8, WALL, 2), hull)
box("bay_back_top", (0, 35, 7), (8, WALL, 2), hull)
# The hole in the hull: bent plates, invisible wall so nobody walks out into space.
box("breach_barrier", (0, 35, 4), (8, 0.4, 4), hull).__setitem__("fh_collide", "only")
for i in range(7):
    angle = rng.uniform(-0.5, 0.5)
    plate = box(f"bent_plate_{i}", (rng.uniform(-3.5, 3.5), 34.7, rng.choice((2.2, 5.8))), (rng.uniform(0.6, 1.6), 0.08, rng.uniform(0.6, 1.3)), hull, solid=False)
    plate.rotation_euler = (angle, 0, rng.uniform(-0.6, 0.6))

# Stairs up to a catwalk along the bay's east wall: 18 cm risers, 30 cm treads.
STEPS = 18
for i in range(STEPS):
    box(f"stair_{i}", (6.5, 20.0 + i * 0.3, (i + 1) * 0.09), (2.0, 0.3, (i + 1) * 0.18), deck)
CATWALK = STEPS * 0.18
box("catwalk", (6.5, 30.2, CATWALK - 0.1), (2.0, 10, 0.2), deck)
box("catwalk_rail", (5.45, 30.2, CATWALK + 0.5), (0.08, 10, 1.1), hull)

# Cargo: containers at human scale, as cover.
for i, (x, y, stack) in enumerate(((-5, 22, 2), (-5, 24.6, 1), (-1.5, 28, 1), (2.5, 24, 2), (-6, 31, 3))):
    for k in range(stack):
        box(f"crate_{i}_{k}", (x + rng.uniform(-0.1, 0.1), y, 0.6 + k * 1.2), (2.2, 2.2, 1.2), paint if (i + k) % 3 == 0 else hull)

lamp("lamp_bay_w", (-5, 21, 7.5), 700, flicker=0.25)
lamp("lamp_bay_e", (6.5, 30, 7.4), 450, color=(0.7, 0.85, 1.0))

# Sunlight through the breach.
sun_data = bpy.data.lights.new("sun_through_breach", "SUN")
sun_data.energy = 5.0
sun_data.color = (1.0, 0.93, 0.82)
sun = bpy.data.objects.new("sun_through_breach", sun_data)
sun.rotation_euler = (math.radians(68), 0, math.radians(180))
bpy.context.scene.collection.objects.link(sun)
s.show_sky = True

# Things that shine: a polished panel in the corridor and standing water on the bay floor.
box("polished_panel", (-1.96, 5.6, 1.55), (0.04, 2.6, 1.7), chrome, solid=False)
box("water_corridor", (0.1, 6.2, 0.006), (2.2, 3.2, 0.012), standing_water, solid=False)
box("water_bay", (-1.0, 25.0, 0.006), (6.0, 7.0, 0.012), standing_water, solid=False)

# A terminal that still has power.
box("console", (-1.6, 12.5, 0.55), (0.6, 1.2, 1.1), hull)
box("console_screen", (-1.28, 12.5, 1.0), (0.03, 1.0, 0.6), screen, solid=False)

# A warning strip over the bay door, across x = 0. It is one surface, so it must be one light.
box("door_strip", (0, 17.25, 3.6), (2.0, 0.03, 0.25), strip_blue, solid=False)

# ---- Entities, placed with the add-on -----------------------------------------------------------
entity("player_start", (0, 0.3, 0))
entity("exit", (0, -0.75, 1.3), fh_prompt="Return to your ship")
entity("data_log", (-1.25, 12.5, 1.2),
       fh_text="Captain's log, day 212. Hull breach in cargo. Crew moved forward. Something is still moving back there.")
entity("data_log", (6.0, 33.5, CATWALK + 1.0), fh_text="Do not open the aft hold. It was not the reactor.")
entity("enemy_spawn", (-3, 30, 0), rotation_z=math.radians(170), fh_kind="crawler")
entity("enemy_spawn", (3, 31, 0), rotation_z=math.radians(190), fh_kind="crawler")
entity("loot", (-6, 31, 3.75), fh_kind="salvage")
entity("loot", (7.1, 34, CATWALK + 0.2), fh_kind="medical")
for location, half_size in (((0, 8, 1.5), (2.0, 9.0, 1.5)), ((0, 26, 4.0), (8.0, 9.0, 4.0))):
    probe = entity("reflection_probe", location)
    probe.scale = half_size
bpy.context.scene.cursor.location = (0, 0, 0)

errors, warnings = editor.run_checks(bpy.context.scene)
for line in errors:
    print("CHECK ERROR:", line)
for line in warnings:
    print("CHECK WARNING:", line)
if errors:
    sys.exit(1)

result = editor.export_scene(bpy.context, os.path.join(ROOT, "tests", "data", "derelict_fixture.glb"))
print("EXPORTED:", result)
