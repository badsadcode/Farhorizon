# FARHORIZON Editor: turns Blender into the game's map and ship editor.
# Press N in the 3D viewport and open the FARHORIZON tab. Guide: docs/BLENDER_GUIDE.md
#
# You model, texture and light as usual. This add-on adds what Blender does not know about: game
# entities with icons in the viewport, what the player collides with, a check for things that would
# not survive export, and one button that plays the map in the game.

bl_info = {
    "name": "FARHORIZON Editor",
    "author": "FARHORIZON",
    "version": (0, 8, 1),
    "blender": (4, 2, 0),
    "location": "3D Viewport > Sidebar (N) > FARHORIZON",
    "description": "Build maps and ships for FARHORIZON and play them with one button",
    "category": "Import-Export",
}

import math
import os
import subprocess

import blf
import bpy
import gpu
from bpy.props import BoolProperty, EnumProperty, FloatProperty, FloatVectorProperty, StringProperty
from bpy_extras import view3d_utils
from gpu_extras.batch import batch_for_shader
from mathutils import Matrix, Vector

COLLECTION = "FH Entities"
REFERENCE = "FH Reference (not exported)"
PLAYER_RADIUS = 0.35
PLAYER_HEIGHT = 1.8

# id, label, icon glyph, colour, extra properties, tooltip
ENTITIES = {
    "player_start": ("Player Start", "person", (0.45, 0.95, 0.5), {}, "Where the player appears. The arrow is the way they face"),
    "exit": ("Exit", "door", (0.5, 0.85, 1.0), {"fh_prompt": "Leave", "fh_radius": 1.6}, "Using this leaves the map"),
    "data_log": ("Data Log", "note", (1.0, 0.85, 0.45), {"fh_prompt": "Read log", "fh_text": "", "fh_radius": 1.4},
                 "A message the player can read"),
    "terminal": ("Terminal", "screen", (0.75, 0.75, 1.0), {"fh_prompt": "Terminal", "fh_radius": 1.6},
                 "A station services terminal"),
    "reflection_probe": ("Reflection Probe", "probe", (0.7, 0.9, 1.0), {},
                         "Scale the box to fit a room. Shiny surfaces inside it reflect that room"),
    "enemy_spawn": ("Enemy Spawn", "skull", (1.0, 0.3, 0.25), {"fh_kind": ""},
                    "Placed now, used when enemies arrive. Shows as a red glow in the game"),
    "loot": ("Loot", "crate", (1.0, 0.7, 0.2), {"fh_kind": ""},
             "Placed now, used when loot arrives. Shows as an amber glow in the game"),
    "seat": ("Pilot Seat", "seat", (0.6, 1.0, 0.8), {"fh_prompt": "Take the pilot seat", "fh_radius": 1.6},
             "For ship interiors: where the pilot sits"),
    "airlock": ("Airlock", "door", (0.9, 0.6, 1.0), {"fh_prompt": "Cycle the airlock", "fh_radius": 1.6},
                "For ship interiors: the way out when docked"),
}
# Points on a ship. The object name is what the game looks for.
SOCKETS = {
    "socket_pilot": ("Cockpit Camera", (0.45, 0.95, 0.5)),
    "engine_left": ("Engine (left)", (0.4, 0.75, 1.0)),
    "engine_right": ("Engine (right)", (0.4, 0.75, 1.0)),
    "hardpoint_left": ("Gun (left)", (1.0, 0.4, 0.3)),
    "hardpoint_right": ("Gun (right)", (1.0, 0.4, 0.3)),
    "socket_airlock": ("Airlock Door", (0.9, 0.6, 1.0)),
}
# Small line drawings on a -1..1 square. Original, drawn by the add-on, no image files needed.
GLYPHS = {
    "person": [(0, 1), (0.28, 0.72), (0, 0.45), (-0.28, 0.72), (0, 1), None, (0, 0.45), (0, -0.25), None, (-0.55, 0.2), (0.55, 0.2),
               None, (0, -0.25), (-0.4, -0.95), None, (0, -0.25), (0.4, -0.95)],
    "door": [(-0.55, -0.95), (-0.55, 0.95), (0.55, 0.95), (0.55, -0.95), (-0.55, -0.95), None, (0.2, 0.1), (0.2, -0.1)],
    "note": [(-0.6, 0.95), (0.6, 0.95), (0.6, -0.95), (-0.6, -0.95), (-0.6, 0.95), None, (-0.35, 0.5), (0.35, 0.5), None, (-0.35, 0.1),
             (0.35, 0.1), None, (-0.35, -0.3), (0.1, -0.3)],
    "screen": [(-0.85, 0.75), (0.85, 0.75), (0.85, -0.3), (-0.85, -0.3), (-0.85, 0.75), None, (0, -0.3), (0, -0.8), None, (-0.45, -0.8),
               (0.45, -0.8)],
    "skull": [(-0.7, 0.2), (-0.45, 0.8), (0.45, 0.8), (0.7, 0.2), (0.4, -0.3), (0.4, -0.85), (-0.4, -0.85), (-0.4, -0.3), (-0.7, 0.2),
              None, (-0.4, 0.2), (-0.15, 0.2), None, (0.15, 0.2), (0.4, 0.2), None, (0, -0.85), (0, -0.45)],
    "crate": [(-0.8, -0.75), (0.8, -0.75), (0.8, 0.75), (-0.8, 0.75), (-0.8, -0.75), None, (-0.8, 0.25), (0.8, 0.25), None, (-0.15, 0.25),
              (-0.15, -0.1), (0.15, -0.1), (0.15, 0.25)],
    "seat": [(-0.45, 0.95), (-0.45, -0.1), (0.6, -0.1), (0.6, -0.9), None, (-0.45, -0.1), (-0.45, -0.9)],
    "probe": [(0, 0.9), (0.64, 0.64), (0.9, 0), (0.64, -0.64), (0, -0.9), (-0.64, -0.64), (-0.9, 0), (-0.64, 0.64), (0, 0.9), None,
              (-0.3, 0.5), (0.1, 0.5), None, (-0.5, 0.2), (-0.15, 0.2)],
    "socket": [(-0.8, 0), (-0.25, 0), None, (0.25, 0), (0.8, 0), None, (0, 0.8), (0, 0.25), None, (0, -0.25), (0, -0.8), None, (-0.25, 0),
               (0, 0.25), (0.25, 0), (0, -0.25), (-0.25, 0)],
}


# ---- Finding the game ---------------------------------------------------------------------------

def addon_key():
    return __package__ or __name__


def prefs():
    addon = bpy.context.preferences.addons.get(addon_key())
    return addon.preferences if addon else None


def game_root():
    p = prefs()
    if p and p.game_folder:
        return bpy.path.abspath(p.game_folder)
    here = os.path.dirname(os.path.abspath(__file__))
    # Installed from build_addon.py's zip, which remembers where the game is.
    remembered = os.path.join(here, "game_root.txt")
    if os.path.isfile(remembered):
        with open(remembered, encoding="utf-8") as f:
            folder = f.read().strip()
        if os.path.isdir(os.path.join(folder, "content")):
            return folder
    # Running from the source tree: tools/blender/farhorizon_editor/__init__.py
    candidate = os.path.normpath(os.path.join(here, "..", "..", ".."))
    return candidate if os.path.isdir(os.path.join(candidate, "content")) else ""


def game_paths():
    root = game_root()
    if not root:
        return "", ""
    for folder in (os.path.join("build", "Release"), "build", ""):
        exe = os.path.join(root, folder, "farhorizon.exe")
        if os.path.isfile(exe):
            validator = os.path.join(root, folder, "content_validator.exe")
            return exe, validator if os.path.isfile(validator) else ""
    return "", ""


def export_path(scene):
    settings = scene.farhorizon
    name = bpy.path.clean_name(settings.name).lower().strip("_") or "untitled"
    folder = "maps" if settings.mode == "MAP" else "ships"
    return os.path.join(game_root(), "content", folder, name + ".glb")


# ---- Checks -------------------------------------------------------------------------------------

def exported_objects(scene):
    return [o for o in scene.objects if not o.hide_render]


def is_entity(obj):
    return obj.type == "EMPTY" and ("fh_type" in obj or obj.name.split(".")[0] in SOCKETS)


def material_problems(mat):
    """Things Blender can render that a glTF file cannot carry."""
    problems = []
    if not mat.node_tree:
        return problems
    output = next((n for n in mat.node_tree.nodes if n.type == "OUTPUT_MATERIAL" and n.is_active_output), None)
    if not output or not output.inputs["Surface"].is_linked:
        return [f"Material '{mat.name}': nothing is connected to its output"]
    shader = output.inputs["Surface"].links[0].from_node
    if shader.type != "BSDF_PRINCIPLED":
        problems.append(f"Material '{mat.name}': use a Principled BSDF, '{shader.name}' will not export")
        return problems
    allowed = {"TEX_IMAGE", "NORMAL_MAP", "SEPARATE_COLOR", "SEPRGB", "MAPPING", "TEX_COORD", "MIX", "MIX_RGB", "UVMAP", "BUMP", "GROUP_OUTPUT",
               "REROUTE", "MATH", "SEPARATE_XYZ", "COMBINE_COLOR", "COMBINE_XYZ"}

    def walk(node, seen):
        if node in seen:
            return
        seen.add(node)
        if node.type not in allowed:
            problems.append(f"Material '{mat.name}': '{node.name}' is procedural and will not export. Bake it to an image")
        if node.type == "TEX_IMAGE" and not node.image:
            problems.append(f"Material '{mat.name}': image node '{node.name}' has no image")
        for socket in node.inputs:
            for link in socket.links:
                walk(link.from_node, seen)

    seen = set()
    for socket in shader.inputs:
        for link in socket.links:
            walk(link.from_node, seen)
    return problems


def run_checks(scene):
    """Returns (errors, warnings). Errors stop the export, because the result could not work."""
    errors, warnings = [], []
    settings = scene.farhorizon
    objects = exported_objects(scene)
    meshes = [o for o in objects if o.type == "MESH"]
    if not game_root():
        errors.append("Game folder not found. Set it in this add-on's preferences")
    if not bpy.path.clean_name(settings.name).strip("_"):
        errors.append("Give it a name")
    if not meshes:
        errors.append("There is nothing to export: no visible meshes")
    if settings.mode == "MAP":
        starts = [o for o in objects if o.get("fh_type") == "player_start"]
        if not starts:
            errors.append("The map needs a Player Start")
        elif len(starts) > 1:
            warnings.append(f"{len(starts)} Player Starts: the game uses one of them")
        for o in starts:
            flat = (o.matrix_world.to_quaternion() @ Vector((0, 1, 0)))
            if abs(flat.z) > 0.3:
                warnings.append(f"'{o.name}' is tilted. Players always stand upright")
        probes = [o for o in objects if o.get("fh_type") == "reflection_probe"]
        if len(probes) > 8:
            warnings.append(f"{len(probes)} Reflection Probes: the game uses the first 8")
        for o in probes:
            rot = o.matrix_world.to_euler()
            if any(abs(a) > 0.01 for a in rot):
                warnings.append(f"'{o.name}' is rotated. Probe boxes always line up with the world axes")
            if o.empty_display_size != 1.0:
                warnings.append(f"'{o.name}' has a changed display size. Use Scale to size the box")
        if not any(o.get("fh_type") == "exit" for o in objects):
            warnings.append("No Exit: players can only leave through the menu")
        solid = [o for o in meshes if o.get("fh_collide", "both") != "none"]
        if meshes and not solid:
            errors.append("Nothing can be walked on: every mesh is set to See Only")
        for o in objects:
            if o.get("fh_type") == "data_log" and not o.get("fh_text"):
                warnings.append(f"Data log '{o.name}' has no text")
    else:
        names = {o.name for o in objects}
        for needed in ("engine_left", "engine_right"):
            if needed not in names:
                warnings.append(f"No '{needed}': the ship's own engine positions are used")
        if "socket_pilot" not in names:
            warnings.append("No Cockpit Camera: the ship's own position is used")
    lights = [o for o in objects if o.type == "LIGHT"]
    for o in lights:
        if o.data.type == "AREA":
            warnings.append(f"Light '{o.name}' is an Area light, which cannot export. Use Point, Spot or Sun")
        if o.data.type == "POINT" and o.data.energy > 3000:
            warnings.append(f"Light '{o.name}' is {o.data.energy:.0f} W. Room lamps look right around 50 to 500 W")
    if settings.mode == "MAP" and not lights:
        warnings.append("No lights: the map will be almost black")
    seen = set()
    for o in meshes:
        scale = o.matrix_world.to_scale()
        for slot in o.material_slots:
            if slot.material and slot.material not in seen:
                seen.add(slot.material)
                warnings.extend(material_problems(slot.material))
        textured = any(slot.material and slot.material.node_tree and
                       any(n.type == "TEX_IMAGE" for n in slot.material.node_tree.nodes) for slot in o.material_slots)
        if textured and not o.data.uv_layers:
            errors.append(f"'{o.name}' has textures but no UV map")
        if min(scale) < 0:
            warnings.append(f"'{o.name}' is mirrored with a negative scale. Apply it if it shades oddly")
    triangles = sum(len(o.data.polygons) * 2 for o in meshes if len(o.data.polygons) < 2000000)
    if triangles > 1500000:
        warnings.append(f"About {triangles:,} triangles: very heavy for a map")
    return errors, warnings


# ---- Export -------------------------------------------------------------------------------------

def export_scene(context):
    """Writes the .glb. Returns its path."""
    scene = context.scene
    settings = scene.farhorizon
    path = export_path(scene)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    marker = None
    if settings.mode == "MAP":
        # The exporter carries custom properties only on objects, so settings ride on a temporary one.
        marker = bpy.data.objects.new("fh_map_settings", None)
        scene.collection.objects.link(marker)
        marker["fh_type"] = "map_settings"
        marker["fh_name"] = settings.name
        marker["fh_gravity"] = settings.gravity
        marker["fh_ambient"] = [settings.ambient[0], settings.ambient[1], settings.ambient[2]]
        marker["fh_exposure"] = settings.exposure
        marker["fh_show_sky"] = 1 if settings.show_sky else 0
        marker["fh_fog_color"] = [settings.fog_color[0], settings.fog_color[1], settings.fog_color[2]]
        marker["fh_fog_density"] = settings.fog_density
    # Lights carry their own colour, because the exporter's reading of it varies between Blender versions.
    lights = [o for o in exported_objects(scene) if o.type == "LIGHT"]
    for light in lights:
        light["fh_color"] = [light.data.color[0], light.data.color[1], light.data.color[2]]
    try:
        bpy.ops.export_scene.gltf(
            filepath=path,
            export_format="GLB",
            use_renderable=True,          # hidden from render = reference only, never exported
            export_extras=True,           # entities and collision settings
            export_lights=True,
            export_yup=True,
            export_apply=True,            # modifiers
            export_materials="EXPORT",
            export_import_convert_lighting_mode="RAW",  # lamp strength stays in Blender's watts
            export_cameras=False,
            export_animations=False,
        )
    finally:
        if marker:
            bpy.data.objects.remove(marker)
        for light in lights:
            del light["fh_color"]
    return path


def check_with_game(path):
    """Asks the game's own reader whether it accepts the file."""
    _exe, validator = game_paths()
    if not validator:
        return True, "Exported. The game's checker is not built, so it was not double-checked"
    run = subprocess.run([validator, "--map", path], capture_output=True, text=True, timeout=120)
    return run.returncode == 0, run.stdout.strip() or run.stderr.strip()


class State:
    game = None        # the running game, so Play replaces it
    report = ([], [])  # last check, shown in the panel
    checked = False
    last = ""


class FH_OT_check(bpy.types.Operator):
    bl_idname = "farhorizon.check"
    bl_label = "Check"
    bl_description = "Look for anything that would not work in the game"

    def execute(self, context):
        State.report = run_checks(context.scene)
        State.checked = True
        errors, warnings = State.report
        if errors:
            self.report({"ERROR"}, f"{len(errors)} problem(s) to fix: see the FARHORIZON panel")
        elif warnings:
            self.report({"WARNING"}, f"{len(warnings)} warning(s): see the FARHORIZON panel")
        else:
            self.report({"INFO"}, "All good")
        return {"FINISHED"}


class FH_OT_export(bpy.types.Operator):
    bl_idname = "farhorizon.export"
    bl_label = "Export"
    bl_description = "Write this into the game's content folder"

    def execute(self, context):
        State.report = run_checks(context.scene)
        State.checked = True
        if State.report[0]:
            self.report({"ERROR"}, State.report[0][0])
            return {"CANCELLED"}
        path = export_scene(context)
        if context.scene.farhorizon.mode == "MAP":
            ok, message = check_with_game(path)
            State.last = message
            if not ok:
                State.report[0].append(message)
                self.report({"ERROR"}, message)
                return {"CANCELLED"}
            for line in message.splitlines():
                if line.startswith("WARNING: "):
                    State.report[1].append(line[9:])
        self.report({"INFO"}, f"Exported {path}")
        return {"FINISHED"}


class FH_OT_play(bpy.types.Operator):
    bl_idname = "farhorizon.play"
    bl_label = "Play in Game"
    bl_description = "Export, then start the game right here. Press Play again to replace it with your changes"

    def execute(self, context):
        exe, _validator = game_paths()
        if not exe:
            self.report({"ERROR"}, "farhorizon.exe not found. Set the game folder in this add-on's preferences")
            return {"CANCELLED"}
        if bpy.ops.farhorizon.export() != {"FINISHED"}:
            return {"CANCELLED"}
        if State.game and State.game.poll() is None:
            State.game.terminate()
        settings = context.scene.farhorizon
        path = export_path(context.scene)
        flag = "--map" if settings.mode == "MAP" else "--ship-mesh"
        State.game = subprocess.Popen([exe, flag, path], cwd=os.path.dirname(exe))
        return {"FINISHED"}


# ---- Adding things ------------------------------------------------------------------------------

def entity_collection(context):
    coll = bpy.data.collections.get(COLLECTION)
    if not coll:
        coll = bpy.data.collections.new(COLLECTION)
    if coll.name not in context.scene.collection.children:
        context.scene.collection.children.link(coll)
    return coll


def place(context, name, size):
    obj = bpy.data.objects.new(name, None)
    obj.empty_display_type = "SPHERE"
    obj.empty_display_size = size
    obj.location = context.scene.cursor.location
    entity_collection(context).objects.link(obj)
    for other in context.selected_objects:
        other.select_set(False)
    obj.select_set(True)
    context.view_layer.objects.active = obj
    return obj


class FH_OT_add_entity(bpy.types.Operator):
    bl_idname = "farhorizon.add_entity"
    bl_label = "Add Entity"
    bl_description = "Add a game entity at the 3D cursor"
    bl_options = {"REGISTER", "UNDO"}
    kind: StringProperty()

    @classmethod
    def description(cls, context, properties):
        return ENTITIES[properties.kind][4] if properties.kind in ENTITIES else cls.bl_description

    def execute(self, context):
        label, _glyph, _color, extra, _tip = ENTITIES[self.kind]
        obj = place(context, label, 0.25)
        obj["fh_type"] = self.kind
        for key, value in extra.items():
            obj[key] = value
        if self.kind == "reflection_probe":
            obj.empty_display_type = "CUBE"
            obj.empty_display_size = 1.0
            obj.scale = (4.0, 4.0, 1.5)
        return {"FINISHED"}


class FH_OT_add_socket(bpy.types.Operator):
    bl_idname = "farhorizon.add_socket"
    bl_label = "Add Ship Point"
    bl_description = "Add an attachment point at the 3D cursor. Its name tells the game what it is"
    bl_options = {"REGISTER", "UNDO"}
    name_: StringProperty()

    def execute(self, context):
        place(context, self.name_, 0.2)
        return {"FINISHED"}


class FH_OT_add_reference(bpy.types.Operator):
    bl_idname = "farhorizon.add_reference"
    bl_label = "Add Scale Reference"
    bl_description = "Add a 1.8 m player and a doorway to build around. They are never exported"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        coll = bpy.data.collections.get(REFERENCE)
        if not coll:
            coll = bpy.data.collections.new(REFERENCE)
            context.scene.collection.children.link(coll)
        at = context.scene.cursor.location

        def add(name, verts, edges):
            mesh = bpy.data.meshes.new(name)
            mesh.from_pydata([v + at for v in verts], edges, [])
            obj = bpy.data.objects.new(name, mesh)
            obj.hide_render = True
            obj.display_type = "WIRE"
            obj.hide_select = True
            coll.objects.link(obj)

        verts, edges = [], []
        for height in (0.0, PLAYER_HEIGHT / 2, PLAYER_HEIGHT):
            start = len(verts)
            for k in range(24):
                a = k / 24 * math.tau
                verts.append(Vector((math.cos(a) * PLAYER_RADIUS, math.sin(a) * PLAYER_RADIUS, height)))
                edges.append((start + k, start + (k + 1) % 24))
        add("Player 1.8 m", verts, edges)
        door = [Vector((1.0, 0, 0)), Vector((1.0, 0, 2.2)), Vector((2.2, 0, 2.2)), Vector((2.2, 0, 0))]
        add("Doorway 1.2 x 2.2 m", door, [(0, 1), (1, 2), (2, 3)])
        return {"FINISHED"}


class FH_OT_set_collision(bpy.types.Operator):
    bl_idname = "farhorizon.set_collision"
    bl_label = "Collision"
    bl_description = "Choose whether the selected objects are seen and collided with"
    bl_options = {"REGISTER", "UNDO"}
    value: StringProperty()

    def execute(self, context):
        for obj in context.selected_objects:
            if obj.type != "MESH":
                continue
            if self.value == "both":
                if "fh_collide" in obj:
                    del obj["fh_collide"]
            else:
                obj["fh_collide"] = self.value
        return {"FINISHED"}


# ---- Viewport icons -----------------------------------------------------------------------------

def draw_lines(shader, points, color, width=1.5):
    shader.uniform_float("color", color)
    batch_for_shader(shader, "LINES", {"pos": points}).draw(shader)


def icon_segments(glyph, cx, cy, size):
    segments, previous = [], None
    for point in GLYPHS[glyph]:
        if point is None:
            previous = None
            continue
        p = (cx + point[0] * size, cy + point[1] * size)
        if previous:
            segments.extend((previous, p))
        previous = p
    return segments


def draw_icons_2d():
    context = bpy.context
    region, rv3d = context.region, context.region_data
    if not rv3d or not context.scene or not hasattr(context.scene, "farhorizon") or not context.scene.farhorizon.show_icons:
        return
    shader = gpu.shader.from_builtin("UNIFORM_COLOR")
    shader.bind()
    gpu.state.blend_set("ALPHA")
    gpu.state.line_width_set(2.0)
    font = 0
    blf.size(font, 11)
    for obj in context.visible_objects:
        if not is_entity(obj):
            continue
        kind = obj.get("fh_type")
        if kind in ENTITIES:
            label, glyph, color = ENTITIES[kind][0], ENTITIES[kind][1], ENTITIES[kind][2]
        else:
            base = obj.name.split(".")[0]
            label, color = SOCKETS[base]
            glyph = "socket"
        at = view3d_utils.location_3d_to_region_2d(region, rv3d, obj.matrix_world.translation)
        if at is None:
            continue
        selected = obj.select_get()
        size = 15.0 if selected else 12.0
        fill = (0.03, 0.03, 0.04, 0.8)
        # A dark disc, so an icon reads against any scene.
        disc = [(at.x + math.cos(k / 16 * math.tau) * size * 1.5, at.y + math.sin(k / 16 * math.tau) * size * 1.5) for k in range(16)]
        tris = []
        for k in range(16):
            tris.extend(((at.x, at.y), disc[k], disc[(k + 1) % 16]))
        shader.uniform_float("color", fill)
        batch_for_shader(shader, "TRIS", {"pos": tris}).draw(shader)
        rim = []
        for k in range(16):
            rim.extend((disc[k], disc[(k + 1) % 16]))
        bright = (*color, 1.0)
        draw_lines(shader, rim, (1, 1, 1, 1) if selected else (*color, 0.7))
        draw_lines(shader, icon_segments(glyph, at.x, at.y, size * 0.95), bright)
        blf.color(font, *color, 1.0)
        text = obj.name if obj.name != label and not obj.name.startswith(label) else label
        blf.position(font, at.x + size * 1.7, at.y - 4, 0)
        blf.draw(font, text)
    gpu.state.line_width_set(1.0)
    gpu.state.blend_set("NONE")


def draw_guides_3d():
    """World-space helpers: how big the player is, and which way things face."""
    context = bpy.context
    if not context.scene or not hasattr(context.scene, "farhorizon") or not context.scene.farhorizon.show_icons:
        return
    shader = gpu.shader.from_builtin("UNIFORM_COLOR")
    shader.bind()
    gpu.state.blend_set("ALPHA")
    gpu.state.depth_test_set("LESS_EQUAL")
    gpu.state.line_width_set(2.0)
    for obj in context.visible_objects:
        kind = obj.get("fh_type") if obj.type == "EMPTY" else None
        if kind not in ("player_start", "enemy_spawn"):
            continue
        color = (*ENTITIES[kind][2], 0.85)
        at = obj.matrix_world.translation
        forward = (obj.matrix_world.to_quaternion() @ Vector((0, 1, 0)))
        forward.z = 0
        if forward.length < 1e-3:
            forward = Vector((0, 1, 0))
        forward.normalize()
        side = Vector((forward.y, -forward.x, 0))
        lines = [at, at + forward * 1.0, at + forward * 1.0, at + forward * 0.75 + side * 0.15, at + forward * 1.0,
                 at + forward * 0.75 - side * 0.15]
        for height in (0.0, PLAYER_HEIGHT):
            for k in range(20):
                a0, a1 = k / 20 * math.tau, (k + 1) / 20 * math.tau
                lines.append(at + Vector((math.cos(a0) * PLAYER_RADIUS, math.sin(a0) * PLAYER_RADIUS, height)))
                lines.append(at + Vector((math.cos(a1) * PLAYER_RADIUS, math.sin(a1) * PLAYER_RADIUS, height)))
        for a in (0, math.pi / 2, math.pi, math.pi * 1.5):
            offset = Vector((math.cos(a) * PLAYER_RADIUS, math.sin(a) * PLAYER_RADIUS, 0))
            lines.extend((at + offset, at + offset + Vector((0, 0, PLAYER_HEIGHT))))
        draw_lines(shader, lines, color)
    gpu.state.depth_test_set("NONE")
    gpu.state.line_width_set(1.0)
    gpu.state.blend_set("NONE")


# ---- Settings and panels ------------------------------------------------------------------------

class FH_Settings(bpy.types.PropertyGroup):
    mode: EnumProperty(name="Making", items=[("MAP", "Map", "A place to walk around: a derelict, or a ship's interior"),
                                             ("SHIP", "Ship Exterior", "A ship to fly: nose along +Y, 1 unit = 1 metre")])
    name: StringProperty(name="Name", default="Untitled")
    show_icons: BoolProperty(name="Show Entity Icons", default=True)
    gravity: FloatProperty(name="Gravity", default=9.81, min=0.0, max=30.0, unit="ACCELERATION")
    ambient: FloatVectorProperty(name="Ambient Light", subtype="COLOR", size=3, default=(0.012, 0.013, 0.016), min=0.0, max=1.0)
    exposure: FloatProperty(name="Exposure", default=1.0, min=0.1, max=8.0)
    show_sky: BoolProperty(name="Space Outside", description="Draw stars and sky through any openings", default=False)
    fog_color: FloatVectorProperty(name="Fog Colour", subtype="COLOR", size=3, default=(0.01, 0.012, 0.014), min=0.0, max=1.0)
    fog_density: FloatProperty(name="Fog Density", default=0.0, min=0.0, max=1.0, precision=3, step=1)


class FH_Preferences(bpy.types.AddonPreferences):
    bl_idname = addon_key()
    game_folder: StringProperty(name="Game Folder", subtype="DIR_PATH",
                                description="The FARHORIZON folder, which holds content and build")

    def draw(self, context):
        self.layout.prop(self, "game_folder")
        exe, _validator = game_paths()
        self.layout.label(text=exe or "farhorizon.exe not found yet", icon="CHECKMARK" if exe else "ERROR")


class FH_PT_main(bpy.types.Panel):
    bl_label = "FARHORIZON"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "FARHORIZON"

    def draw(self, context):
        layout = self.layout
        settings = context.scene.farhorizon
        layout.row().prop(settings, "mode", expand=True)
        layout.prop(settings, "name")
        row = layout.row(align=True)
        row.scale_y = 1.6
        row.operator("farhorizon.play", icon="PLAY")
        row = layout.row(align=True)
        row.operator("farhorizon.check", icon="CHECKMARK")
        row.operator("farhorizon.export", icon="EXPORT")
        if State.checked:
            errors, warnings = State.report
            box = layout.box()
            if not errors and not warnings:
                box.label(text="All good", icon="CHECKMARK")
            for message in errors:
                box.label(text=message, icon="ERROR")
            for message in warnings:
                box.label(text=message, icon="INFO")


class FH_PT_entities(bpy.types.Panel):
    bl_label = "Add"
    bl_parent_id = "FH_PT_main"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "FARHORIZON"

    def draw(self, context):
        layout = self.layout
        settings = context.scene.farhorizon
        grid = layout.grid_flow(columns=2, align=True)
        if settings.mode == "MAP":
            for kind, data in ENTITIES.items():
                grid.operator("farhorizon.add_entity", text=data[0]).kind = kind
        else:
            for name, data in SOCKETS.items():
                grid.operator("farhorizon.add_socket", text=data[0]).name_ = name
        layout.operator("farhorizon.add_reference", icon="ARMATURE_DATA")
        layout.prop(settings, "show_icons")


class FH_PT_selected(bpy.types.Panel):
    bl_label = "Selected"
    bl_parent_id = "FH_PT_main"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "FARHORIZON"

    def draw(self, context):
        layout = self.layout
        obj = context.active_object
        if not obj:
            layout.label(text="Nothing selected")
            return
        if obj.type == "MESH":
            current = obj.get("fh_collide", "both")
            layout.label(text="What this object is:")
            col = layout.column(align=True)
            for value, label in (("both", "Solid"), ("none", "See Only (walk through)"), ("only", "Invisible Wall")):
                op = col.operator("farhorizon.set_collision", text=label, depress=current == value)
                op.value = value
        elif obj.type == "LIGHT":
            layout.prop(obj.data, "energy", text="Power (W)")
            layout.prop(obj.data, "color")
            if "fh_flicker" in obj:
                layout.prop(obj, '["fh_flicker"]', text="Flicker")
            else:
                layout.operator("farhorizon.add_flicker", text="Make It Flicker")
        elif "fh_type" in obj:
            layout.label(text=ENTITIES.get(obj["fh_type"], ("Entity",))[0])
            names = {"fh_prompt": "Prompt", "fh_text": "Text", "fh_radius": "Reach (m)", "fh_kind": "Kind"}
            for key, label in names.items():
                if key in obj:
                    layout.prop(obj, f'["{key}"]', text=label)
        else:
            layout.label(text="No game settings on this object")


class FH_OT_add_flicker(bpy.types.Operator):
    bl_idname = "farhorizon.add_flicker"
    bl_label = "Make It Flicker"
    bl_description = "A failing lamp: 0 is steady and 1 stutters badly"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        obj = context.active_object
        if obj and obj.type == "LIGHT":
            obj["fh_flicker"] = 0.5
        return {"FINISHED"}


class FH_PT_map(bpy.types.Panel):
    bl_label = "Map Settings"
    bl_parent_id = "FH_PT_main"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "FARHORIZON"
    bl_options = {"DEFAULT_CLOSED"}

    @classmethod
    def poll(cls, context):
        return context.scene.farhorizon.mode == "MAP"

    def draw(self, context):
        layout = self.layout
        settings = context.scene.farhorizon
        for name in ("gravity", "ambient", "exposure", "show_sky", "fog_color", "fog_density"):
            layout.prop(settings, name)


def add_menu(self, context):
    self.layout.separator()
    self.layout.menu("FH_MT_add", icon="WORLD")


class FH_MT_add(bpy.types.Menu):
    bl_label = "FARHORIZON"
    bl_idname = "FH_MT_add"

    def draw(self, context):
        for kind, data in ENTITIES.items():
            self.layout.operator("farhorizon.add_entity", text=data[0]).kind = kind


CLASSES = (FH_Settings, FH_Preferences, FH_OT_check, FH_OT_export, FH_OT_play, FH_OT_add_entity, FH_OT_add_socket, FH_OT_add_reference,
           FH_OT_set_collision, FH_OT_add_flicker, FH_PT_main, FH_PT_entities, FH_PT_selected, FH_PT_map, FH_MT_add)
_handlers = []


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Scene.farhorizon = bpy.props.PointerProperty(type=FH_Settings)
    bpy.types.VIEW3D_MT_add.append(add_menu)
    if not bpy.app.background:
        _handlers.append((bpy.types.SpaceView3D.draw_handler_add(draw_icons_2d, (), "WINDOW", "POST_PIXEL")))
        _handlers.append((bpy.types.SpaceView3D.draw_handler_add(draw_guides_3d, (), "WINDOW", "POST_VIEW")))


def unregister():
    for handler in _handlers:
        bpy.types.SpaceView3D.draw_handler_remove(handler, "WINDOW")
    _handlers.clear()
    bpy.types.VIEW3D_MT_add.remove(add_menu)
    del bpy.types.Scene.farhorizon
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)
