"""
Writes asset_manifest.json from the FBX files themselves.

Run inside Blender, after build_butchery:

    exec(open(r"...\\Tools\\butchery\\export_manifest.py").read())

The manifest is what the Unreal import and build commandlets read. It carries
the one thing that cannot be inferred from a filename -- whether an FBX holds a
skeleton -- plus the footprint the build needs to tile and space assets.

Measured from the exported files, not from the Blender scene that made them.
Everything measured before export describes the scene, not the file, and an
export setting that halves the scale or drops the rig leaves the scene looking
perfect and the manifest lying about what is on disk.

It had no generator at all until now: the file was written by hand in a session
and then went stale the moment an asset was added, which is a silent failure --
the build simply skips an asset it cannot find in the manifest and reports it
as unplaced.
"""

import json
import os

import bpy  # pylint: disable=import-error

LIB_DIR = r"C:\Users\Metaverse\Documents\PCB_Essen\Tools\butchery"
FBX_DIR = os.path.join(LIB_DIR, "fbx")
OUT = os.path.join(LIB_DIR, "asset_manifest.json")


def clear():
    active = bpy.context.view_layer.objects.active
    if active is not None and active.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    if bpy.context.view_layer.objects:
        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.object.delete(use_global=False)


def measure(path):
    clear()
    bpy.ops.import_scene.fbx(filepath=path)
    objects = list(bpy.context.scene.objects)

    rig = next((o for o in objects if o.type == "ARMATURE"), None)
    bones = [b.name for b in rig.data.bones] if rig is not None else []

    # The footprint is the mesh's, not the armature's: an armature's bounding
    # box includes bones that stick out past the geometry, and the build spaces
    # assets by this figure.
    lo = [1e18] * 3
    hi = [-1e18] * 3
    for obj in objects:
        if obj.type != "MESH":
            continue
        for corner in obj.bound_box:
            world = obj.matrix_world @ __import__("mathutils").Vector(corner)
            for axis in range(3):
                lo[axis] = min(lo[axis], world[axis])
                hi[axis] = max(hi[axis], world[axis])

    size = [round(hi[a] - lo[a], 3) if hi[a] > lo[a] else 0.0 for a in range(3)]
    return {"skeletal": rig is not None, "bones": bones, "size": size}


def main():
    rows = []
    for filename in sorted(f for f in os.listdir(FBX_DIR) if f.lower().endswith(".fbx")):
        name = filename[3:-4] if filename.startswith("SM_") else filename[:-4]
        info = measure(os.path.join(FBX_DIR, filename))
        rows.append({"asset": name, "file": filename,
                     "skeletal": info["skeletal"], "bones": info["bones"],
                     "size": info["size"]})

    with open(OUT, "w") as handle:
        json.dump({"assets": rows}, handle, indent=1)

    return {"assets": len(rows), "skeletal": sum(1 for r in rows if r["skeletal"]),
            "output": OUT}


result = main()
print(json.dumps(result, indent=1))
