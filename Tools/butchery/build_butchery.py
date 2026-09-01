"""
Builds the butchery line's station meshes and props, and exports them as FBX.

Run inside Blender:

    exec(open(r"...\\Tools\\butchery\\build_butchery.py").read())

`BUILD` selects which assets to make; empty means all of them. Each is built in
a cleared scene, exported, then re-imported from its own FBX and measured --
everything measured before export describes the Blender scene, not the file, and
an export setting that quietly drops UVs or halves the scale leaves the scene
looking perfect.
"""

import importlib
import json
import os
import sys

import bpy  # pylint: disable=import-error

LIB_DIR = r"C:\Users\Metaverse\Documents\PCB_Essen\Tools\butchery"
OUT_DIR = os.path.join(LIB_DIR, "fbx")

if LIB_DIR not in sys.path:
    sys.path.insert(0, LIB_DIR)

import butchery_infra     # noqa: E402  pylint: disable=wrong-import-position
import butchery_lib       # noqa: E402  pylint: disable=wrong-import-position
import butchery_props     # noqa: E402  pylint: disable=wrong-import-position
import butchery_stations  # noqa: E402  pylint: disable=wrong-import-position
import butchery_yard      # noqa: E402  pylint: disable=wrong-import-position

# Reloaded every run so editing a station does not need Blender restarted.
importlib.reload(butchery_lib)
importlib.reload(butchery_stations)
importlib.reload(butchery_props)
importlib.reload(butchery_infra)
importlib.reload(butchery_yard)

from butchery_lib import (  # noqa: E402  pylint: disable=wrong-import-position
    assemble, build_armature, iter_fcurves, set_origin_to_floor)

BUILD = []
FPS = 30
LOOP_FRAMES = 60

ASSETS = dict(butchery_stations.STATIONS)
ASSETS.update(butchery_props.PROPS)
ASSETS.update(butchery_infra.INFRA)
ASSETS.update(butchery_yard.YARD)

# Assets that hang from the structure rather than stand on the floor. Their
# origin stays at floor level -- world zero in the source -- so placing one at
# a floor position puts the rail at 3.1 m instead of laying it on the ground.
CEILING_MOUNTED = {"RAIL_RUN", "RAIL_CARCASS_RUN", "RAIL_CURVE", "RAIL_SWITCH",
                   "CHILL_EVAPORATOR"}


# ---------------------------------------------------------------------------

def reset_scene():
    """
    Empty the file without disturbing the user's preferences.

    The mode reset is not belt-and-braces: an asset that fails midway leaves
    Blender in Edit or Pose mode, and every object operator then refuses to
    poll -- so one broken asset would cascade into every asset after it failing
    for an unrelated reason.
    """
    active = bpy.context.view_layer.objects.active
    if active is not None and active.mode != "OBJECT":
        try:
            bpy.ops.object.mode_set(mode="OBJECT")
        except RuntimeError:
            bpy.context.view_layer.objects.active = None

    if bpy.context.view_layer.objects:
        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.object.delete(use_global=False)

    for block in (bpy.data.meshes, bpy.data.armatures, bpy.data.actions):
        for item in list(block):
            if item.users == 0:
                block.remove(item)

    bpy.context.scene.render.fps = FPS
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = LOOP_FRAMES


def export(name, mesh, rig):
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, "SM_{:s}.fbx".format(name))

    bpy.ops.object.select_all(action="DESELECT")
    mesh.select_set(True)
    if rig is not None:
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
    else:
        bpy.context.view_layer.objects.active = mesh

    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        # Unreal's expected axes. Both apps are Z-up, but the FBX convention
        # between them is Y-up; these two are what stop a station arriving on
        # its side.
        axis_forward="-Z",
        axis_up="Y",
        global_scale=1.0,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_NONE",
        use_space_transform=True,
        bake_space_transform=False,
        object_types={"MESH", "ARMATURE"} if rig else {"MESH"},
        # False when rigged: applying modifiers would bake the armature into the
        # mesh and freeze it in its rest pose.
        use_mesh_modifiers=rig is None,
        mesh_smooth_type="FACE",
        use_tspace=True,
        add_leaf_bones=False,
        primary_bone_axis="Y",
        secondary_bone_axis="X",
        bake_anim=rig is not None,
        bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False,
        bake_anim_force_startend_keying=True,
        bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0,
        path_mode="COPY",
        use_custom_props=False,
    )
    return path


def build(name):
    reset_scene()
    parts, bones, animate = ASSETS[name]()

    mesh = assemble(parts, "SM_{:s}".format(name))
    set_origin_to_floor(mesh, floor_z=0.0 if name in CEILING_MOUNTED else None)

    rig = None
    if bones:
        rig = build_armature("SM_{:s}".format(name), bones, mesh)
        bpy.ops.object.select_all(action="DESELECT")
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
        bpy.ops.object.mode_set(mode="POSE")
        if animate is not None:
            animate(rig)
        bpy.ops.object.mode_set(mode="OBJECT")

    mesh.data.calc_loop_triangles()

    # Every bone must still weigh some geometry. Culling buried faces can erase
    # a part outright -- correctly, if it was invisible -- and that leaves a
    # bone animating nothing. The failure is silent: the rig exports, the
    # animation plays, and there is simply nothing on screen moving.
    if bones:
        weighted = {group.name: 0 for group in mesh.vertex_groups}
        for vertex in mesh.data.vertices:
            for entry in vertex.groups:
                if entry.weight > 0.5:
                    weighted[mesh.vertex_groups[entry.group].name] += 1
        starved = [spec["name"] for spec in bones
                   if weighted.get(spec["name"], 0) == 0]
        if starved:
            raise RuntimeError(
                "bones with no geometry after culling: {} -- the parts skinned "
                "to them are buried inside another solid and cannot be seen"
                .format(sorted(starved)))

    path = export(name, mesh, rig)

    return {
        "name": name,
        "path": path,
        "tris": len(mesh.data.loop_triangles),
        "verts": len(mesh.data.vertices),
        "materials": sorted(m.name for m in mesh.data.materials if m),
        "uvs": len(mesh.data.uv_layers),
        "dimensions": [round(d, 3) for d in mesh.dimensions],
        "bones": [b.name for b in rig.data.bones] if rig else [],
        "kb": round(os.path.getsize(path) / 1024.0, 1),
    }


def verify(path, expected):
    """Re-import an exported FBX and measure what actually came back."""
    reset_scene()
    bpy.ops.import_scene.fbx(filepath=path)

    scene = bpy.context.scene
    meshes = [o for o in scene.objects if o.type == "MESH"]
    rigs = [o for o in scene.objects if o.type == "ARMATURE"]

    problems = []
    if not meshes:
        problems.append("no mesh came back")

    tris = 0
    dims = [0.0, 0.0, 0.0]
    for mesh in meshes:
        mesh.data.calc_loop_triangles()
        tris += len(mesh.data.loop_triangles)
        if not mesh.data.uv_layers:
            problems.append("{:s} has no UV layer".format(mesh.name))
        if not any(m for m in mesh.data.materials):
            problems.append("{:s} has no material".format(mesh.name))
        dims = [max(a, b) for a, b in zip(dims, mesh.dimensions)]
        if any(abs(s - 1.0) > 1e-4 for s in mesh.scale):
            problems.append("{:s} imported with scale {}".format(
                mesh.name, tuple(round(s, 3) for s in mesh.scale)))

    if tris != expected["tris"]:
        problems.append("tris {:d} != {:d} exported".format(tris, expected["tris"]))

    # Size is the failure that costs most downstream: an asset at 100x or 0.01x
    # only becomes obvious once it is in the level beside a person.
    for axis, (got, want) in enumerate(zip(dims, expected["dimensions"])):
        if want > 0 and abs(got - want) > max(0.02, want * 0.02):
            problems.append("axis {:d} {:.3f} m, expected {:.3f} m".format(axis, got, want))

    anim = {}
    if expected["bones"]:
        if not rigs:
            problems.append("expected an armature, none came back")
        else:
            rig = rigs[0]
            bones = [b.name for b in rig.data.bones]
            missing = sorted(set(expected["bones"]) - set(bones))
            if missing:
                problems.append("bones lost in the round trip: {}".format(missing))

            moving = 0
            for curve in iter_fcurves(rig):
                values = [k.co[1] for k in curve.keyframe_points]
                if values and (max(values) - min(values)) > 1e-4:
                    moving += 1
            anim = {"bones": len(bones), "moving_channels": moving}
            if moving == 0:
                problems.append("animation came back flat -- nothing moves")

    return {"tris": tris, "dimensions": [round(d, 3) for d in dims],
            "anim": anim, "problems": problems}


def run(names=None):
    names = names or list(ASSETS)
    report = []
    for name in names:
        try:
            built = build(name)
            built["verify"] = verify(built["path"], built)
            report.append(built)
        except Exception:  # pylint: disable=broad-exception-caught
            import traceback
            report.append({"name": name, "error": traceback.format_exc()[-900:]})
    return report


result = {"report": run(BUILD or None)}
print(json.dumps(result, indent=2))
