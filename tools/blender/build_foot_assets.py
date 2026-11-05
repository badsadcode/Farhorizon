# Builds the first-person gun, the crawler and the loot from scratch. All original, nothing imported.
#   blender -b --factory-startup --python tools/blender/build_foot_assets.py
# Writes content/models/foot/*.glb and touches nothing else.
# Facing: +Y in Blender is forward in the game. The crawler and loot stand on z = 0. The gun's origin is its
# grip. A leg's origin is its hip, and it reaches out along +X.
import math
import os

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "content", "models", "foot"))
os.makedirs(OUT, exist_ok=True)


def material(name, color, metallic=0.6, roughness=0.5, glow=None, strength=0.0):
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


def finish(obj, mat, bevel=0.0, smooth=False):
    obj.data.materials.append(mat)
    if bevel > 0:
        mod = obj.modifiers.new("bevel", "BEVEL")
        mod.width = bevel
        mod.segments = 2
        mod.limit_method = "ANGLE"
    if smooth:
        bpy.ops.object.shade_smooth()
    return obj


def box(at, size, mat, rot=(0, 0, 0), bevel=0.006):
    bpy.ops.mesh.primitive_cube_add(size=1, location=at, rotation=rot)
    o = bpy.context.active_object
    o.scale = size
    bpy.ops.object.transform_apply(scale=True)
    return finish(o, mat, bevel)


def tube(at, radius, length, mat, axis="Y", verts=14):
    rot = {"X": (0, math.pi / 2, 0), "Y": (math.pi / 2, 0, 0), "Z": (0, 0, 0)}[axis]
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=length, location=at, rotation=rot)
    return finish(bpy.context.active_object, mat, smooth=True)


def blob(at, size, mat, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=18, ring_count=12, radius=1, location=at, rotation=rot)
    o = bpy.context.active_object
    o.scale = size
    bpy.ops.object.transform_apply(scale=True)
    return finish(o, mat, smooth=True)


def export(name):
    for o in bpy.data.objects:
        o.select_set(True)
    path = os.path.join(OUT, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_apply=True, export_yup=True,
                              export_lights=False, export_cameras=False)
    print("WROTE", path)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


# ---- The rivet driver: a fitter's tool, blocky, worn, with a magazine of rivets on its side ----------
reset()
steel = material("driver_steel", (0.17, 0.18, 0.19), 0.9, 0.42)
casing = material("driver_casing", (0.26, 0.17, 0.065), 0.45, 0.58)
grip = material("driver_grip", (0.035, 0.035, 0.04), 0.0, 0.85)
brass = material("driver_rivets", (0.62, 0.45, 0.18), 1.0, 0.35)
gauge = material("driver_gauge", (0.02, 0.05, 0.03), 0.0, 0.3, (0.25, 1.0, 0.45), 0.12)
box((0, -0.01, 0.075), (0.052, 0.20, 0.075), casing)                    # body
box((0, 0.13, 0.085), (0.04, 0.09, 0.05), steel)                        # breech
tube((0, 0.235, 0.085), 0.017, 0.14, steel, "Y")                        # barrel
tube((0, 0.31, 0.085), 0.024, 0.024, steel, "Y")                        # muzzle collar
box((0, 0.20, 0.052), (0.022, 0.10, 0.012), steel)                      # guide rail
box((0, -0.015, -0.03), (0.04, 0.056, 0.14), grip, rot=(math.radians(-14), 0, 0))
box((0, 0.045, 0.005), (0.012, 0.065, 0.009), steel)                    # trigger guard
box((0.040, 0.03, 0.078), (0.028, 0.125, 0.038), steel)                 # magazine
for i in range(5):
    tube((0.056, -0.018 + i * 0.024, 0.078), 0.0065, 0.006, brass, "X", verts=8)
box((0.012, -0.09, 0.117), (0.014, 0.022, 0.006), gauge, bevel=0.0)         # charge window at the back
box((-0.028, 0.02, 0.09), (0.004, 0.09, 0.012), brass, bevel=0.0)       # a strip of worn tape
export("rivet_driver")

# ---- The crawler: a hull-grazing thing, low and plated, with a cluster of red sensors -----------------
for name, strength in (("crawler_body", 1.6), ("crawler_dead", 0.0)):
    reset()
    shell = material("crawler_shell", (0.17, 0.155, 0.13), 0.45, 0.42)
    under = material("crawler_under", (0.05, 0.028, 0.024), 0.05, 0.75)
    sensor = material("crawler_sensor", (0.1, 0.0, 0.0), 0.0, 0.25, (1.0, 0.06, 0.02), strength)
    blob((0, 0, 0.42), (0.34, 0.50, 0.19), under)
    blob((0, -0.34, 0.40), (0.24, 0.28, 0.14), under)
    for i, y in enumerate((-0.42, -0.2, 0.02, 0.24)):   # overlapping back plates
        w = 0.37 - abs(y + 0.1) * 0.25
        blob((0, y, 0.52 - abs(y + 0.1) * 0.07), (w, 0.2, 0.085), shell, rot=(math.radians(-10), 0, 0))
    blob((0, 0.42, 0.42), (0.17, 0.17, 0.12), shell)    # head
    for x, z, r in ((-0.06, 0.46, 0.022), (0.06, 0.46, 0.022), (-0.03, 0.42, 0.013), (0.03, 0.42, 0.013), (0, 0.48, 0.012)):
        blob((x, 0.555, z), (r, r, r), sensor)
    for side in (-1, 1):                                # mandibles
        box((side * 0.09, 0.60, 0.33), (0.024, 0.15, 0.026), shell, rot=(0, 0, side * math.radians(-18)))
    export(name)

reset()
shell = material("crawler_shell", (0.17, 0.155, 0.13), 0.45, 0.42)
under = material("crawler_under", (0.05, 0.028, 0.024), 0.05, 0.75)
# Hip at the origin: up and out to the knee, then down to the deck.
box((0.13, 0, 0.08), (0.28, 0.038, 0.04), under, rot=(0, math.radians(-30), 0), bevel=0.004)
box((0.36, 0, -0.14), (0.05, 0.03, 0.46), shell, rot=(0, math.radians(-16), 0), bevel=0.004)
export("crawler_leg")

# ---- Loot ---------------------------------------------------------------------------------------------
reset()
olive = material("salvage_case", (0.19, 0.2, 0.11), 0.2, 0.68)
steel = material("salvage_steel", (0.25, 0.25, 0.26), 0.9, 0.45)
lamp = material("salvage_lamp", (0.2, 0.1, 0.0), 0.0, 0.4, (1.0, 0.55, 0.1), 1.5)
box((0, 0, 0.14), (0.62, 0.38, 0.28), olive, bevel=0.012)
box((0, 0, 0.29), (0.65, 0.41, 0.035), steel)
for x in (-0.2, 0.2):
    box((x, -0.195, 0.2), (0.05, 0.02, 0.09), steel)
box((0, -0.195, 0.09), (0.09, 0.012, 0.025), lamp, bevel=0.001)
export("loot_salvage")

reset()
white = material("medical_case", (0.62, 0.62, 0.58), 0.05, 0.5)
mark = material("medical_cross", (0.5, 0.02, 0.02), 0.0, 0.5)
lamp = material("medical_lamp", (0.0, 0.1, 0.03), 0.0, 0.4, (0.15, 1.0, 0.4), 1.5)
box((0, 0, 0.11), (0.42, 0.28, 0.22), white, bevel=0.018)
box((0, 0, 0.222), (0.16, 0.05, 0.006), mark, bevel=0.0)
box((0, 0, 0.222), (0.05, 0.16, 0.006), mark, bevel=0.0)
box((0.15, -0.142, 0.17), (0.04, 0.008, 0.02), lamp, bevel=0.001)
export("loot_medical")

# ==== 0.10: more weapons, enemies and loot (same conventions as above) =================================

# ---- The flechette caster: a breaching tool that throws a fan of steel darts ----------------------------
reset()
steel = material("caster_steel", (0.15, 0.155, 0.16), 0.9, 0.38)
paint = material("caster_paint", (0.45, 0.09, 0.04), 0.3, 0.6)
grip = material("caster_grip", (0.035, 0.035, 0.04), 0.0, 0.85)
hazard = material("caster_hazard", (0.6, 0.45, 0.05), 0.2, 0.55)
lamp = material("caster_lamp", (0.1, 0.05, 0.0), 0.0, 0.3, (1.0, 0.55, 0.15), 0.4)
box((0, 0.0, 0.08), (0.075, 0.24, 0.085), paint)                        # receiver
for x in (-0.022, 0.022):
    tube((x, 0.26, 0.085), 0.019, 0.24, steel, "Y")                     # paired barrels
box((0, 0.37, 0.085), (0.075, 0.03, 0.05), steel)                       # muzzle brace
tube((0, 0.05, 0.015), 0.045, 0.07, steel, "X", verts=16)              # dart drum
for i in range(6):
    a = i * math.pi / 3
    box((0.04, 0.05 + math.cos(a) * 0.03, 0.015 + math.sin(a) * 0.03), (0.006, 0.012, 0.012), hazard, bevel=0.0)
box((0, -0.06, -0.035), (0.042, 0.06, 0.14), grip, rot=(math.radians(-16), 0, 0))
box((0, 0.17, 0.0), (0.04, 0.10, 0.03), grip)                           # fore grip
box((0, -0.13, 0.11), (0.05, 0.05, 0.03), steel)                        # brace plate
box((-0.039, 0.0, 0.08), (0.003, 0.2, 0.02), hazard, bevel=0.0)         # warning stripe
box((0.0, -0.08, 0.126), (0.02, 0.02, 0.006), lamp, bevel=0.0)          # ready lamp
export("flechette_caster")

# ---- The arc cutter: a plasma-arc hull cutter with a bare coil and a cell on top -------------------------
reset()
steel = material("cutter_steel", (0.2, 0.21, 0.22), 0.9, 0.35)
body = material("cutter_body", (0.12, 0.16, 0.2), 0.5, 0.5)
grip = material("cutter_grip", (0.035, 0.035, 0.04), 0.0, 0.85)
copper = material("cutter_coil", (0.55, 0.28, 0.12), 1.0, 0.3)
glow = material("cutter_glow", (0.02, 0.05, 0.1), 0.0, 0.2, (0.35, 0.65, 1.0), 3.0)
cell = material("cutter_cell", (0.3, 0.32, 0.12), 0.3, 0.5)
box((0, 0.0, 0.075), (0.06, 0.2, 0.08), body)                           # housing
tube((0, 0.20, 0.072), 0.022, 0.22, steel, "Y")                         # nozzle
for i in range(5):
    tube((0, 0.13 + i * 0.03, 0.072), 0.03, 0.01, copper, "Y")          # coil rings
tube((0, 0.315, 0.072), 0.012, 0.02, glow, "Y")                         # arc tip
box((0, -0.01, 0.15), (0.05, 0.12, 0.05), cell)                         # cell
box((0, -0.01, 0.178), (0.03, 0.08, 0.006), glow, bevel=0.0)            # charge strip
box((0, -0.02, -0.035), (0.04, 0.056, 0.14), grip, rot=(math.radians(-14), 0, 0))
box((0, 0.05, 0.0), (0.012, 0.06, 0.009), steel)
export("arc_cutter")

# ---- The spitter: tall and thin, on four long legs, with a swollen glowing throat ------------------------
for name, strength in (("spitter_body", 1.8), ("spitter_dead", 0.0)):
    reset()
    hide = material("spitter_hide", (0.11, 0.12, 0.09), 0.1, 0.55)
    plate = material("spitter_plate", (0.2, 0.19, 0.15), 0.45, 0.4)
    sac = material("spitter_sac", (0.18, 0.2, 0.02), 0.0, 0.3, (0.6, 0.9, 0.08), strength)
    eye = material("spitter_eye", (0.1, 0.0, 0.0), 0.0, 0.25, (1.0, 0.1, 0.02), strength)
    blob((0, -0.05, 0.62), (0.2, 0.32, 0.17), hide)                     # body, raised high
    blob((0, -0.12, 0.72), (0.17, 0.24, 0.07), plate, rot=(math.radians(-8), 0, 0))
    blob((0, 0.22, 0.66), (0.13, 0.16, 0.14), sac)                      # throat sac
    blob((0, 0.36, 0.76), (0.09, 0.12, 0.08), plate)                    # head
    for x in (-0.035, 0.035):
        blob((x, 0.46, 0.79), (0.016, 0.016, 0.016), eye)
    tube((0, 0.47, 0.72), 0.03, 0.06, hide, "Y", verts=10)              # spout
    export(name)

reset()
hide = material("spitter_hide", (0.11, 0.12, 0.09), 0.1, 0.55)
plate = material("spitter_plate", (0.2, 0.19, 0.15), 0.45, 0.4)
box((0.16, 0, 0.12), (0.34, 0.03, 0.03), hide, rot=(0, math.radians(-38), 0), bevel=0.004)
box((0.38, 0, -0.26), (0.035, 0.025, 0.66), plate, rot=(0, math.radians(-10), 0), bevel=0.004)
export("spitter_leg")

# ---- The brute: a hunched armoured mass with a ram of fused plates and a single burning slit -------------
for name, strength in (("brute_body", 2.2), ("brute_dead", 0.0)):
    reset()
    armour = material("brute_armour", (0.16, 0.14, 0.12), 0.6, 0.45)
    flesh = material("brute_flesh", (0.07, 0.035, 0.03), 0.05, 0.7)
    slit = material("brute_slit", (0.1, 0.0, 0.0), 0.0, 0.25, (1.0, 0.18, 0.03), strength)
    blob((0, -0.1, 0.9), (0.5, 0.62, 0.42), flesh)                      # bulk
    for i, y in enumerate((-0.5, -0.2, 0.1)):                           # back plates
        blob((0, y, 1.22 - i * 0.04), (0.46 - i * 0.03, 0.24, 0.13), armour, rot=(math.radians(-14), 0, 0))
    box((0, 0.5, 0.85), (0.7, 0.18, 0.52), armour, rot=(math.radians(10), 0, 0), bevel=0.03)  # ram
    box((0, 0.6, 0.98), (0.36, 0.02, 0.035), slit, bevel=0.0)          # slit
    for side in (-1, 1):
        box((side * 0.42, 0.42, 0.6), (0.12, 0.3, 0.12), armour, rot=(math.radians(25), 0, 0), bevel=0.02)  # tusks
    export(name)

reset()
armour = material("brute_armour", (0.16, 0.14, 0.12), 0.6, 0.45)
flesh = material("brute_flesh", (0.07, 0.035, 0.03), 0.05, 0.7)
box((0.16, 0, 0.0), (0.34, 0.12, 0.14), flesh, rot=(0, math.radians(-25), 0), bevel=0.02)
box((0.34, 0, -0.38), (0.16, 0.14, 0.62), armour, rot=(0, math.radians(-6), 0), bevel=0.02)
export("brute_leg")

# ---- Loot: a long tool cache (weapons) and a small ammunition box (drops) --------------------------------
reset()
olive = material("cache_case", (0.12, 0.14, 0.16), 0.35, 0.6)
steel = material("cache_steel", (0.25, 0.25, 0.26), 0.9, 0.45)
lamp = material("cache_lamp", (0.0, 0.05, 0.1), 0.0, 0.4, (0.3, 0.6, 1.0), 1.8)
box((0, 0, 0.12), (1.1, 0.34, 0.24), olive, bevel=0.012)
box((0, 0, 0.25), (1.13, 0.37, 0.03), steel)
for x in (-0.4, 0.0, 0.4):
    box((x, -0.175, 0.17), (0.05, 0.02, 0.08), steel)
box((0.48, -0.175, 0.07), (0.07, 0.012, 0.022), lamp, bevel=0.001)
export("loot_cache")

reset()
case = material("ammo_case", (0.22, 0.18, 0.1), 0.3, 0.6)
stripe = material("ammo_stripe", (0.6, 0.42, 0.05), 0.2, 0.55)
lamp = material("ammo_lamp", (0.1, 0.05, 0.0), 0.0, 0.4, (1.0, 0.6, 0.15), 1.4)
box((0, 0, 0.09), (0.32, 0.2, 0.18), case, bevel=0.01)
box((0, -0.101, 0.1), (0.3, 0.004, 0.035), stripe, bevel=0.0)
box((0, 0, 0.183), (0.12, 0.05, 0.012), stripe, bevel=0.002)
box((0.11, -0.102, 0.15), (0.03, 0.004, 0.015), lamp, bevel=0.0)
export("loot_ammo")
