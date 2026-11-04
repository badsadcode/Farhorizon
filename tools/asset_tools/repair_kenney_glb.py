#!/usr/bin/env python3
"""Repairs Kenney Space Kit GLB exports (UniGLTF) so they are valid glTF 2.0.

Problem: the exported scene lists a *child* node as a scene root, which the glTF spec forbids
(cgltf rejects it). This script points the scene at the true root nodes, zeroes the
grid-placement offset of the mesh node so the model is centred, and records the repair in
asset.extras.  Usage:  repair_kenney_glb.py <in.glb> <out.glb>
"""
import json
import struct
import sys


def main(src, dst):
    data = open(src, "rb").read()
    magic, version, _ = struct.unpack("<III", data[:12])
    if magic != 0x46546C67 or version != 2:
        sys.exit(f"{src}: not a glTF 2.0 binary")
    json_len, json_type = struct.unpack("<II", data[12:20])
    assert json_type == 0x4E4F534A
    doc = json.loads(data[20:20 + json_len])
    rest = data[20 + json_len:]

    nodes = doc.get("nodes", [])
    children = {c for n in nodes for c in n.get("children", [])}
    roots = [i for i in range(len(nodes)) if i not in children]
    doc["scenes"] = [{"nodes": roots}]
    doc["scene"] = 0
    for n in nodes:
        if "mesh" in n and "translation" in n:
            n["translation"] = [0, 0, 0]
    doc.setdefault("asset", {})["extras"] = {
        "farhorizonRepair": "scene roots fixed to true root nodes; mesh grid offset removed"}

    out_json = json.dumps(doc, separators=(",", ":")).encode()
    out_json += b" " * ((4 - len(out_json) % 4) % 4)
    total = 12 + 8 + len(out_json) + len(rest)
    with open(dst, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(out_json), 0x4E4F534A))
        f.write(out_json)
        f.write(rest)
    print(f"{dst}: roots={roots}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
