#!/usr/bin/env python3
"""Builds the zip a friend needs to host a game: the server program, the game's data and a short guide.

    python tools/package_server.py

Run it after a Release build. It writes dist/FarhorizonServer-<version>-windows.zip and changes nothing else.
"""
import pathlib
import re
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "Release" / "farhorizon_server.exe"
CONTENT = ROOT / "content"
SKIP = re.compile(r"^maps/untitled.*\.glb$")  # the owner's throwaway test maps

README = """FARHORIZON server {version}
=========================

This lets your friends play FARHORIZON together on this PC. Nothing is installed: to remove it, delete this folder.
You don't need to own or run the game yourself, and you don't need a good graphics card.

GETTING STARTED
1. Unzip this whole folder somewhere, such as your Desktop. Don't run it from inside the zip.
2. Double-click farhorizon_server.exe. If Windows SmartScreen warns about it, choose "More info", then "Run anyway".
   The program isn't signed, so Windows doesn't recognise it.
3. Type a password and press START SERVER.
4. If Windows Firewall asks, allow it.
5. Open the HELP tab in the program and follow the steps to set up your router.

Keep the window open while people play. Closing it saves the world.

Choose a password you don't use for anything else. It is kept as plain text in server_config.json in this folder.

Everyone must use the same version of the game: this is version {version}.
"""


def main():
    if not EXE.exists():
        sys.exit(f"{EXE} is missing: do a Release build first")
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    version = re.search(r"project\(FARHORIZON VERSION ([0-9.]+)", cmake).group(1)
    out = ROOT / "dist" / f"FarhorizonServer-{version}-windows.zip"
    out.parent.mkdir(exist_ok=True)
    folder = f"FarhorizonServer-{version}"
    files = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.write(EXE, f"{folder}/farhorizon_server.exe")
        z.writestr(f"{folder}/README.txt", README.format(version=version).replace("\n", "\r\n"))
        for path in sorted(CONTENT.rglob("*")):
            relative = path.relative_to(CONTENT).as_posix()
            if path.is_file() and not SKIP.match(relative):
                z.write(path, f"{folder}/content/{relative}")
                files += 1
    print(f"{out}  {out.stat().st_size / 1e6:.0f} MB, {files} content files")


if __name__ == "__main__":
    main()
