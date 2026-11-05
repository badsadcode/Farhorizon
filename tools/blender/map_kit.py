# Helpers shared by the scripts that build maps: generated textures, boxes, lamps and entities.
# Everything is made here, so nothing comes from outside files.
import bpy
import numpy as np

np_rng = np.random.default_rng(4417)
fixture_material = None  # set this to the glowing material lamp fixtures use


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


def plain_material(name, color, metallic, roughness):
    """No textures at all: just the sliders, as anyone would set them in Blender."""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, 1)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    return mat


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
    fixture = box(name + "_fixture", (location[0], location[1], location[2] + 0.1), (0.9, 0.22, 0.05), fixture_material, solid=False)
    fixture["fh_glow_only"] = 1  # the lamp above does the lighting
    return obj


def entity(kind, location, rotation_z=0.0, **props):
    bpy.context.scene.cursor.location = location
    bpy.ops.farhorizon.add_entity(kind=kind)
    obj = bpy.context.active_object
    obj.rotation_euler[2] = rotation_z
    for key, value in props.items():
        obj[key] = value
    return obj
