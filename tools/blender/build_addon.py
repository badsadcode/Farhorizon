# Packs the add-on for Blender's "Install from Disk", and remembers where this copy of the game lives.
#   python tools/blender/build_addon.py
# Writes tools/blender/farhorizon_editor.zip.
import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "farhorizon_editor")
GAME = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "farhorizon_editor.zip")

with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
    for name in sorted(os.listdir(SOURCE)):
        path = os.path.join(SOURCE, name)
        if os.path.isfile(path) and not name.endswith(".pyc"):
            z.write(path, name)
    z.writestr("game_root.txt", GAME)
print("Wrote", OUT)
