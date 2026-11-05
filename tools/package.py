#!/usr/bin/env python3
"""Builds the one zip anyone needs: the game, the server program, their data and a short guide.

    python tools/package.py

Run it after a Release build. It writes dist/Farhorizon-<version>-windows.zip and changes nothing else.
"""
import pathlib
import re
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
RELEASE = ROOT / "build" / "Release"
EXES = ["farhorizon.exe", "farhorizon_server.exe"]
CONTENT = ROOT / "content"
SHADERS = ROOT / "build" / "shaders"
SKIP = re.compile(r"^maps/untitled.*\.glb$")  # the owner's throwaway test maps

README = """FARHORIZON {version}
=================

Nothing is installed: to remove it, delete this folder. Unzip the whole folder first, and don't run it from inside the
zip. If Windows SmartScreen warns about a program here, choose "More info", then "Run anyway". The programs aren't
signed, so Windows doesn't recognise them.

TO PLAY
Double-click farhorizon.exe. It needs a graphics card with Vulkan, which any recent driver includes. If it says
vulkan-1.dll is missing, update your graphics driver.

TO JOIN A FRIEND'S GAME
In the main menu choose JOIN GAME, then type the address, your name and the password your friend gave you.

TO HOST A GAME FOR FRIENDS
1. Double-click farhorizon_server.exe. It needs no special graphics card, and you don't have to play on this PC.
2. Type a password and press START SERVER. If Windows Firewall asks, allow it.
3. Open the HELP tab and follow the steps to set up your router.

Keep the server window open while people play. Closing it saves the world. Choose a password you don't use for
anything else: it is kept as plain text in server_config.json in this folder.

Everyone must use the same version of the game: this is version {version}.
"""


def main():
    for name in EXES:
        if not (RELEASE / name).exists():
            sys.exit(f"{RELEASE / name} is missing: do a Release build first")
    shaders = sorted(SHADERS.glob("*.spv"))
    if not shaders:
        sys.exit(f"no compiled shaders in {SHADERS}: do a Release build first")
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    version = re.search(r"project\(FARHORIZON VERSION ([0-9.]+)", cmake).group(1)
    out = ROOT / "dist" / f"Farhorizon-{version}-windows.zip"
    out.parent.mkdir(exist_ok=True)
    folder = f"Farhorizon-{version}"
    files = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in EXES:
            z.write(RELEASE / name, f"{folder}/{name}")
        z.writestr(f"{folder}/README.txt", README.format(version=version).replace("\n", "\r\n"))
        for path in shaders:
            z.write(path, f"{folder}/shaders/{path.name}")
        for path in sorted(CONTENT.rglob("*")):
            relative = path.relative_to(CONTENT).as_posix()
            if path.is_file() and not SKIP.match(relative):
                z.write(path, f"{folder}/content/{relative}")
                files += 1
    print(f"{out}  {out.stat().st_size / 1e6:.0f} MB, {files} content files, {len(shaders)} shaders")


if __name__ == "__main__":
    main()
