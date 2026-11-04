# Builds the sample derelict through the FARHORIZON add-on, the same way an artist would, and saves it:
#   blender -b --factory-startup --python tools/blender/build_sample_derelict.py
# Outputs content_src/maps/derelict_sample.blend and content/maps/derelict_sample.glb.
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

def noise_field(size, octaves=5, persistence=0.55, seed_offset=0):
    """Tileable value noise, 0..1."""
    rand = np.random.default_rng(4417 + seed_offset)
    out = np.zeros((size, size), dtype=np.float32)
    amplitude, total = 1.0, 0.0
    cells = 4
    for _ in range(octaves):
        grid = rand.random((cells, cells), dtype=np.float32)
        xs = np.arange(size) / size * cells
        i = xs.astype(int)
        f = xs - i
        f = f * f * (3 - 2 * f)
        i1 = (i + 1) % cells
        a = grid[np.ix_(i, i)]
        b = grid[np.ix_(i, i1)]
        c = grid[np.ix_(i1, i)]
        d = grid[np.ix_(i1, i1)]
        fy, fx = f[:, None], f[None, :]
        out += amplitude * ((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy)
        total += amplitude
        amplitude *= persistence
        cells *= 2
    return out / total


def plate_height(size, plates=4, seam=0.012):
    """Riveted panels: 1 on a plate, falling to 0 in the seams, with rivets near the corners."""
    v, u = np.mgrid[0:size, 0:size] / size
    fu, fv = (u * plates) % 1.0, (v * plates) % 1.0
    edge = np.minimum(np.minimum(fu, 1 - fu), np.minimum(fv, 1 - fv)) / plates
    height = np.clip(edge / seam, 0, 1)
    for cu in (0.09, 0.91):
        for cv in (0.09, 0.91):
            d = np.sqrt((fu - cu) ** 2 + (fv - cv) ** 2)
            height += 0.35 * np.clip(1 - d / 0.022, 0, 1)
    return height


def to_normal(height, strength):
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * strength
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * strength
    n = np.stack((-dx, -dy, np.ones_like(height)), axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


def make_image(name, rgb, data=False):
    size = rgb.shape[0]
    image = bpy.data.images.new(name, size, size, alpha=False)
    rgba = np.ones((size, size, 4), dtype=np.float32)
    rgba[..., :3] = rgb
    image.colorspace_settings.name = "Non-Color" if data else "sRGB"
    image.pixels.foreach_set(rgba.ravel())
    image.update()
    image.pack()
    return image


def textured_material(name, color_a, color_b, plates, base_rough, metal, seed):
    size = 512
    grime = noise_field(size, seed_offset=seed)
    streak = noise_field(size, octaves=4, seed_offset=seed + 1)
    stretched = np.repeat(streak[:, ::8], 8, axis=1)[:size, :size]
    wear = np.clip((grime * 0.65 + stretched * 0.5 - 0.3) * 2.0, 0, 1)
    plate = plate_height(size, plates)
    seams = 1 - np.clip(plate, 0, 1)
    tone = wear[..., None]
    rgb = np.array(color_a, dtype=np.float32) * (1 - tone) + np.array(color_b, dtype=np.float32) * tone
    rgb = rgb * (1 - 0.65 * seams[..., None])
    height = plate * 0.6 + noise_field(size, octaves=6, persistence=0.6, seed_offset=seed + 2) * 0.35
    roughness = np.clip(base_rough + (wear - 0.5) * 0.45, 0.08, 1.0)
    metallic = np.full_like(roughness, metal) * (1 - 0.7 * wear)
    # glTF layout: G roughness, B metallic. The exporter combines separate maps, but one packed
    # image shows the route that needs no node setup beyond Separate Color.
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    base = nodes.new("ShaderNodeTexImage")
    base.image = make_image(name + "_color", rgb)
    links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
    rough_tex = nodes.new("ShaderNodeTexImage")
    rough_tex.image = make_image(name + "_rough", np.stack((roughness, roughness, roughness), axis=-1), data=True)
    links.new(rough_tex.outputs["Color"], bsdf.inputs["Roughness"])
    metal_tex = nodes.new("ShaderNodeTexImage")
    metal_tex.image = make_image(name + "_metal", np.stack((metallic, metallic, metallic), axis=-1), data=True)
    links.new(metal_tex.outputs["Color"], bsdf.inputs["Metallic"])
    normal_tex = nodes.new("ShaderNodeTexImage")
    normal_tex.image = make_image(name + "_normal", to_normal(height, 3.0), data=True)
    normal_map = nodes.new("ShaderNodeNormalMap")
    links.new(normal_tex.outputs["Color"], normal_map.inputs["Color"])
    links.new(normal_map.outputs["Normal"], bsdf.inputs["Normal"])
    return mat


def glow_material(name, color, strength):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0.02, 0.02, 0.02, 1)
    bsdf.inputs["Emission Color"].default_value = (*color, 1)
    bsdf.inputs["Emission Strength"].default_value = strength
    return mat


hull = textured_material("hull_plate", (0.62, 0.66, 0.68), (0.36, 0.31, 0.25), 3, 0.55, 0.3, 10)
deck = textured_material("deck_grate", (0.5, 0.5, 0.48), (0.3, 0.25, 0.19), 6, 0.6, 0.3, 20)
paint = textured_material("hazard_paint", (0.8, 0.55, 0.12), (0.36, 0.26, 0.12), 2, 0.7, 0.1, 30)
lamp_on = glow_material("lamp_warm", (1.0, 0.72, 0.42), 1.3)
screen = glow_material("screen_green", (0.15, 1.0, 0.45), 0.9)


# ---- Geometry helpers ---------------------------------------------------------------------------

def box(name, centre, size, material, solid=True):
    """A box whose UVs repeat every 3 metres, so textures stay the same size everywhere."""
    x, y, z = (size[0] / 2, size[1] / 2, size[2] / 2)
    verts, faces, uvs = [], [], []
    sides = [
        ((-x, -y, -z), (x, -y, -z), (x, -y, z), (-x, -y, z), size[0], size[2]),
        ((x, y, -z), (-x, y, -z), (-x, y, z), (x, y, z), size[0], size[2]),
        ((x, -y, -z), (x, y, -z), (x, y, z), (x, -y, z), size[1], size[2]),
        ((-x, y, -z), (-x, -y, -z), (-x, -y, z), (-x, y, z), size[1], size[2]),
        ((-x, -y, z), (x, -y, z), (x, y, z), (-x, y, z), size[0], size[1]),
        ((-x, y, -z), (x, y, -z), (x, -y, -z), (-x, -y, -z), size[0], size[1]),
    ]
    for side in sides:
        start = len(verts)
        verts.extend(side[:4])
        faces.append((start, start + 1, start + 2, start + 3))
        u, v = side[4] / 3.0, side[5] / 3.0
        uvs.extend(((0, 0), (u, 0), (u, v), (0, v)))
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    layer = mesh.uv_layers.new(name="UVMap")
    for loop in mesh.loops:
        layer.data[loop.index].uv = uvs[loop.vertex_index]
    mesh.materials.append(material)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    obj.location = centre
    bpy.context.scene.collection.objects.link(obj)
    if not solid:
        obj["fh_collide"] = "none"
    return obj


def lamp(name, location, watts, color=(1.0, 0.75, 0.5), flicker=0.0):
    data = bpy.data.lights.new(name, "POINT")
    data.energy = watts
    data.color = color
    obj = bpy.data.objects.new(name, data)
    obj.location = location
    if flicker > 0:
        obj["fh_flicker"] = flicker
    bpy.context.scene.collection.objects.link(obj)
    box(name + "_fixture", (location[0], location[1], location[2] + 0.1), (0.9, 0.22, 0.05), lamp_on, solid=False)
    return obj


def entity(kind, location, rotation_z=0.0, **props):
    bpy.context.scene.cursor.location = location
    bpy.ops.farhorizon.add_entity(kind=kind)
    obj = bpy.context.active_object
    obj.rotation_euler[2] = rotation_z
    for key, value in props.items():
        obj[key] = value
    return obj


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

# A terminal that still has power.
box("console", (-1.6, 12.5, 0.55), (0.6, 1.2, 1.1), hull)
box("console_screen", (-1.28, 12.5, 1.0), (0.03, 1.0, 0.6), screen, solid=False)

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
bpy.context.scene.cursor.location = (0, 0, 0)

errors, warnings = editor.run_checks(bpy.context.scene)
for line in errors:
    print("CHECK ERROR:", line)
for line in warnings:
    print("CHECK WARNING:", line)
if errors:
    sys.exit(1)

blend = os.path.join(ROOT, "content_src", "maps", "derelict_sample.blend")
os.makedirs(os.path.dirname(blend), exist_ok=True)
result = editor.export_scene(bpy.context)
print("EXPORTED:", result)
bpy.ops.wm.save_as_mainfile(filepath=blend, compress=True)
print("SAVED:", blend)
