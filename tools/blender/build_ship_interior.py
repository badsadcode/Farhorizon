# Builds the inside of the Sparrow: two decks, joined by a stair.
#   blender -b --factory-startup --python tools/blender/build_ship_interior.py
# Writes content/interiors/sparrow_decks.glb and touches nothing else. All generated and original.
#
# +Y is the bow. The windows are real gaps in the walls: what you see through them is the game's own space.
#   Lower deck (floor 0):     engine room at the stern, then the hold, with the airlock on the starboard side.
#   Upper deck (floor 2.8):   bunks at the stern, then the galley, then the cockpit in the bow.
import os
import random
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import farhorizon_editor as editor  # noqa: E402
import map_kit  # noqa: E402
from map_kit import box, entity, glow_material, lamp, plain_material, textured_material  # noqa: E402

editor.register()
rng = random.Random(9021)

bpy.ops.wm.read_factory_settings(use_empty=True)
s = bpy.context.scene.farhorizon
s.mode = "MAP"
s.name = "Sparrow"
s.ambient = (0.022, 0.024, 0.027)
s.exposure = 1.1
s.fog_density = 0.0

hull = textured_material("sparrow_plate", (0.56, 0.58, 0.58), (0.3, 0.27, 0.23), 3, 0.5, 0.35, 110)
deck = textured_material("sparrow_deck", (0.44, 0.44, 0.42), (0.25, 0.22, 0.18), 6, 0.62, 0.25, 120)
paint = textured_material("sparrow_paint", (0.2, 0.34, 0.36), (0.11, 0.17, 0.18), 2, 0.6, 0.1, 130)
cloth = plain_material("sparrow_cloth", (0.2, 0.15, 0.11), 0.0, 0.92)
dark = plain_material("sparrow_dark", (0.03, 0.03, 0.035), 0.5, 0.5)
steel = plain_material("sparrow_steel", (0.4, 0.41, 0.42), 0.9, 0.38)
crate = plain_material("sparrow_crate", (0.3, 0.2, 0.08), 0.1, 0.72)
lamp_on = glow_material("sparrow_lamp", (1.0, 0.78, 0.55), 1.4)
screen = glow_material("sparrow_screen", (0.2, 0.85, 0.75), 0.3)
amber = glow_material("sparrow_amber", (1.0, 0.5, 0.1), 1.5)
core = glow_material("sparrow_core", (0.35, 0.75, 1.0), 1.1)
map_kit.fixture_material = lamp_on

LOWER, SLAB, UPPER, TOP = 0.0, 2.6, 2.8, 5.2
T = 0.2  # wall thickness


def slab(name, x0, x1, y0, y1, z, thickness, material):
    box(name, ((x0 + x1) / 2, (y0 + y1) / 2, z + thickness / 2), (x1 - x0, y1 - y0, thickness), material)


def wall_x(name, x, y0, y1, z0, z1, material=hull):
    """A wall along Y, at a fixed X."""
    box(name, (x, (y0 + y1) / 2, (z0 + z1) / 2), (T, y1 - y0, z1 - z0), material)


def wall_y(name, y, x0, x1, z0, z1, material=hull):
    """A wall along X, at a fixed Y."""
    box(name, ((x0 + x1) / 2, y, (z0 + z1) / 2), (x1 - x0, T, z1 - z0), material)


def windowed_x(name, x, y0, y1, z0, z1, gap_y0, gap_y1, gap_z0, gap_z1):
    wall_x(name + "_a", x, y0, gap_y0, z0, z1)
    wall_x(name + "_b", x, gap_y1, y1, z0, z1)
    wall_x(name + "_low", x, gap_y0, gap_y1, z0, gap_z0)
    wall_x(name + "_high", x, gap_y0, gap_y1, gap_z1, z1)
    block = box(name + "_pane", (x, (gap_y0 + gap_y1) / 2, (gap_z0 + gap_z1) / 2), (T, gap_y1 - gap_y0, gap_z1 - gap_z0), dark)
    block["fh_collide"] = "only"  # nobody walks out into space
    box(name + "_bar", (x, (gap_y0 + gap_y1) / 2, (gap_z0 + gap_z1) / 2), (0.05, 0.05, gap_z1 - gap_z0), steel, solid=False)


# ---- The shell -----------------------------------------------------------------------------------
slab("lower_floor", -3, 3, -10, 6, LOWER - T, T, deck)
slab("lower_roof_stern", -3, 3, -10, -6, SLAB, UPPER - SLAB, hull)
wall_x("lower_w_stern", -3.1, -10, 1, LOWER, SLAB)
windowed_x("lower_w_port", -3.1, 1, 6, LOWER, SLAB, 2.4, 4.2, 1.0, 1.9)
wall_x("lower_e", 3.1, -10, 6, LOWER, SLAB)
wall_y("lower_stern", -10.1, -3.2, 3.2, LOWER, SLAB)
wall_y("lower_bow", 6.1, -3.2, 3.2, LOWER, SLAB)

# The upper floor, with a hole above the stair.
slab("upper_floor_port", -3, 1, -6, 6, SLAB, UPPER - SLAB, deck)
slab("upper_floor_stern", 1, 3, -6, -3.6, SLAB, UPPER - SLAB, deck)
slab("upper_floor_bow", 1, 3, 1.4, 6, SLAB, UPPER - SLAB, deck)
slab("upper_roof", -3, 3, -6, 6, TOP, T, hull)
windowed_x("upper_w_bunks", -3.1, -6, 1.8, UPPER, TOP, -3.2, -0.8, 3.7, 4.6)
windowed_x("upper_w_galley", -3.1, 1.8, 6, UPPER, TOP, 3, 5, 3.7, 4.6)
windowed_x("upper_e_galley", 3.1, -6, 6, UPPER, TOP, 2.4, 4.8, 3.7, 4.6)
wall_y("upper_stern", -6.1, -3.2, 3.2, UPPER, TOP)

# The cockpit: narrower, with the big window.
slab("cockpit_floor", -2.5, 2.5, 6, 10, SLAB, UPPER - SLAB, deck)
slab("cockpit_roof", -2.5, 2.5, 6, 10, TOP, T, hull)
windowed_x("cockpit_w", -2.6, 6, 10, UPPER, TOP, 7, 9.4, 3.6, 4.8)
windowed_x("cockpit_e", 2.6, 6, 10, UPPER, TOP, 7, 9.4, 3.6, 4.8)
wall_y("bow_low", 10.1, -2.7, 2.7, UPPER, 3.5)
wall_y("bow_high", 10.1, -2.7, 2.7, 4.95, TOP)
wall_y("bow_side_w", 10.1, -2.7, -2.0, 3.5, 4.95)
wall_y("bow_side_e", 10.1, 2.0, 2.7, 3.5, 4.95)
pane = box("bow_pane", (0, 10.1, 4.22), (4.0, T, 1.45), dark)
pane["fh_collide"] = "only"
for x in (-0.7, 0.7):
    box(f"bow_strut_{x}", (x, 10.1, 4.22), (0.06, 0.06, 1.45), steel, solid=False)

# Bulkhead between galley and cockpit, with a door.
wall_y("bulkhead_w", 6, -3.1, -0.9, UPPER, TOP)
wall_y("bulkhead_e", 6, 0.9, 3.1, UPPER, TOP)
wall_y("bulkhead_lintel", 6, -0.9, 0.9, 4.95, TOP)

# ---- The stair, along the starboard side of the hold ----------------------------------------------
STEPS = 16
RISE = (UPPER) / STEPS
for i in range(STEPS):
    height = (i + 1) * RISE
    box(f"stair_{i}", (1.95, 1.0 - i * 0.28, height / 2), (1.9, 0.28, height), deck)
box("stair_top", (1.95, -3.47, UPPER / 2), (1.9, 0.26, UPPER), deck)  # meets the landing
box("stair_side", (0.95, -1.17, SLAB / 2), (0.1, 4.62, SLAB), hull)  # you go up from the foot, not over the side
box("hole_rail_w", (1.0, -1.1, UPPER + 0.5), (0.08, 5.0, 1.0), steel)
box("hole_rail_bow", (2.0, 1.4, UPPER + 0.5), (2.1, 0.08, 1.0), steel)

# ---- Lower deck: the hold --------------------------------------------------------------------------
for i, (x, y, stack) in enumerate(((-2.1, 5.0, 2), (-2.1, -3.4, 2), (-2.1, -1.8, 1), (0.1, 5.1, 1))):
    for k in range(stack):
        box(f"crate_{i}_{k}", (x + rng.uniform(-0.05, 0.05), y, 0.5 + k), (1.3, 1.3, 1.0), crate if (i + k) % 2 else paint)
box("hold_stripe_w", (-2.98, -2, 0.45), (0.03, 8.0, 0.5), paint, solid=False)
box("hold_stripe_e", (2.98, 4, 0.45), (0.03, 3.8, 0.5), paint, solid=False)

# The airlock, on the starboard wall.
box("airlock_door", (2.97, 4.2, 1.1), (0.05, 1.4, 2.2), steel, solid=False)
box("airlock_frame_top", (2.95, 4.2, 2.26), (0.08, 1.7, 0.1), dark, solid=False)
box("airlock_lamp", (2.94, 3.3, 1.85), (0.03, 0.16, 0.1), amber, solid=False).__setitem__("fh_glow_only", 1)

# Bulkhead to the engine room, with a door.
wall_y("engine_bulk_w", -5, -3.1, -1.5, LOWER, SLAB)
wall_y("engine_bulk_e", -5, 0.5, 3.1, LOWER, SLAB)
wall_y("engine_lintel", -5, -1.5, 0.5, 2.1, SLAB)

# ---- Lower deck: the engine room -------------------------------------------------------------------
box("core_base", (0, -8, 0.3), (1.6, 1.6, 0.6), dark)
box("core_cap", (0, -8, 2.3), (1.6, 1.6, 0.6), dark)
box("core_glow", (0, -8, 1.3), (0.7, 0.7, 1.4), core)
for x, y in ((-0.62, -8.62), (0.62, -8.62), (-0.62, -7.38), (0.62, -7.38)):
    box(f"core_rod_{x}_{y}", (x, y, 1.3), (0.1, 0.1, 1.4), steel)
for z, x in ((2.25, 2.4), (1.7, 2.65), (1.1, 2.4)):
    box(f"pipe_{z}", (x, -7.5, z), (0.14, 4.6, 0.14), steel, solid=False)
box("engine_panel", (-2.96, -7.5, 1.4), (0.05, 2.0, 0.9), dark, solid=False)
box("engine_panel_screen", (-2.92, -7.5, 1.45), (0.02, 1.5, 0.45), screen, solid=False).__setitem__("fh_glow_only", 1)

# ---- Upper deck: bunks ------------------------------------------------------------------------------
wall_y("bunks_bulk", 1.8, -3.1, -1.2, UPPER, TOP)
for i, y in enumerate((-5.1, -2.4 + 2.4, -5.1 + 2.3)):
    box(f"bunk_{i}", (-2.3, y, UPPER + 0.25), (1.2, 2.0, 0.5), dark)
    box(f"bunk_mattress_{i}", (-2.3, y, UPPER + 0.55), (1.15, 1.95, 0.14), cloth, solid=False)
box("locker", (-0.2, -5.6, UPPER + 1.0), (1.8, 0.6, 2.0), paint)

# ---- Upper deck: galley -----------------------------------------------------------------------------
box("galley_table", (-1.6, 4.0, UPPER + 0.72), (1.2, 2.0, 0.08), steel)
box("galley_leg", (-1.6, 4.0, UPPER + 0.36), (0.14, 0.14, 0.72), dark)
box("galley_counter", (2.55, 4.0, UPPER + 0.45), (0.8, 3.4, 0.9), paint)
box("galley_top", (2.55, 4.0, UPPER + 0.92), (0.85, 3.45, 0.04), steel, solid=False)

# ---- Cockpit ----------------------------------------------------------------------------------------
box("seat_base", (0, 7.5, UPPER + 0.25), (0.6, 0.6, 0.5), dark)
box("seat_pad", (0, 7.5, UPPER + 0.55), (0.56, 0.56, 0.12), cloth, solid=False)
box("seat_back", (0, 7.2, UPPER + 0.95), (0.56, 0.1, 0.8), cloth, solid=False)
box("console", (0, 9.3, UPPER + 0.4), (3.2, 0.9, 0.8), dark)
box("console_screen_l", (-0.8, 8.84, UPPER + 0.72), (1.0, 0.02, 0.32), screen, solid=False).__setitem__("fh_glow_only", 1)
box("console_screen_r", (0.8, 8.84, UPPER + 0.72), (1.0, 0.02, 0.32), screen, solid=False).__setitem__("fh_glow_only", 1)

# ---- Lamps. Each brings its own glowing fixture -----------------------------------------------------
WARM = (1.0, 0.78, 0.55)
lamp("lamp_cockpit", (0, 8.0, TOP - 0.3), 75, color=WARM)
lamp("lamp_galley", (0.3, 4.0, TOP - 0.3), 200, color=WARM)
lamp("lamp_bunks", (-1.2, -2.0, TOP - 0.3), 140, color=WARM)
lamp("lamp_landing", (1.7, -4.8, TOP - 0.3), 120, color=WARM)
lamp("lamp_hold_bow", (-0.8, 3.6, SLAB - 0.25), 220, color=WARM)
lamp("lamp_hold_stern", (-0.8, -2.6, SLAB - 0.25), 180, color=WARM)
lamp("lamp_engine", (-1.8, -7.6, SLAB - 0.25), 120, color=(0.7, 0.85, 1.0))

# ---- Entities: placed at chest height, and the player stands one metre below them ------------------
entity("player_start", (0, 3.0, UPPER))
entity("seat", (0, 8.0, UPPER + 1.0))
entity("airlock", (2.0, 4.2, 1.0), rotation_z=1.5708)
bpy.context.scene.cursor.location = (0, 0, 0)

errors, warnings = editor.run_checks(bpy.context.scene)
for line in errors:
    print("CHECK ERROR:", line)
for line in warnings:
    print("CHECK WARNING:", line)
if errors:
    sys.exit(1)
out = os.path.join(ROOT, "content", "interiors", "sparrow_decks.glb")
print("EXPORTED:", editor.export_scene(bpy.context, out))
