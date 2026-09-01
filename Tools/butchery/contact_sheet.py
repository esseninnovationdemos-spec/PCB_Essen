"""
Imports every exported FBX into a grid and renders one contact sheet.

The point is to look at the assets the way a person will, all at once and at the
same scale. Numbers in a verification report tell you a mesh has UVs and the
right bounding box; they cannot tell you a machine reads as the machine it is
meant to be, or that one asset is quietly twice the size of its neighbours.
"""

import math
import os

import bpy  # pylint: disable=import-error
from mathutils import Matrix, Vector  # pylint: disable=import-error

FBX_DIR = r"C:\Users\Metaverse\Documents\PCB_Essen\Tools\butchery\fbx"
OUT = r"C:\Users\Metaverse\Documents\PCB_Essen\Tools\butchery\contact_sheet.png"

COLUMNS = 6
# Wider than the longest asset. The bleed rail is 8.5 m and the stunner 8.2 m,
# so at 7 m cells they overlapped their neighbours, and an oblique camera then
# threw the tall ones up into the row behind -- which reads as assets missing
# rather than as a layout that is too tight.
SPACING = 12.0
LABEL_DROP = 5.2


def clear():
    active = bpy.context.view_layer.objects.active
    if active is not None and active.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    if bpy.context.view_layer.objects:
        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.object.delete(use_global=False)


def label(text, location):
    bpy.ops.object.text_add(location=location, rotation=(math.radians(64), 0, 0))
    obj = bpy.context.active_object
    obj.data.body = text
    obj.data.size = 0.55
    obj.data.align_x = "CENTER"
    mat = bpy.data.materials.new("M_Label")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (0.05, 0.05, 0.06, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.9
    obj.data.materials.append(mat)
    return obj


def fit_camera(camera, aspect, margin=1.04):
    """
    Fits the camera to what is actually in the scene.

    The scale was guessed from the grid size divided by a constant, and the
    guess was wide by a factor of three: the assets came out as specks in the
    middle of an empty floor with the bottom row over the edge. Guessing is
    also the wrong shape of solution -- the projected height of the grid
    depends on the camera tilt, the labels hang below the last row, and a tall
    asset pokes above the first one, so the only figure that fits is the one
    measured off the scene itself.

    Everything is projected into camera space and the span taken there, which
    is where the framing actually happens.
    """
    bpy.context.view_layer.update()

    inverse = camera.matrix_world.inverted()
    lo_x = lo_y = 1e18
    hi_x = hi_y = -1e18
    for obj in bpy.context.scene.objects:
        if obj.type not in {"MESH", "FONT"} or obj is camera:
            continue
        # Text has no useful bound_box until it is evaluated to a mesh, and the
        # floor plane is 200 m across and would swamp the fit.
        if obj.type == "MESH" and max(obj.dimensions) > 100.0:
            continue
        for corner in obj.bound_box:
            local = inverse @ (obj.matrix_world @ Vector(corner))
            lo_x = min(lo_x, local.x); hi_x = max(hi_x, local.x)
            lo_y = min(lo_y, local.y); hi_y = max(hi_y, local.y)

    if hi_x < lo_x:
        return

    # Recentre on the content, then scale to whichever axis binds.
    camera.matrix_world = camera.matrix_world @ Matrix.Translation(
        Vector(((lo_x + hi_x) * 0.5, (lo_y + hi_y) * 0.5, 0.0)))
    camera.data.ortho_scale = max(hi_x - lo_x, (hi_y - lo_y) * aspect) * margin


def main():
    clear()

    files = sorted(f for f in os.listdir(FBX_DIR) if f.lower().endswith(".fbx"))
    placed = []

    for index, filename in enumerate(files):
        before = set(bpy.context.scene.objects)
        bpy.ops.import_scene.fbx(filepath=os.path.join(FBX_DIR, filename))
        imported = [o for o in bpy.context.scene.objects if o not in before]

        column = index % COLUMNS
        row = index // COLUMNS
        offset = (column * SPACING, -row * SPACING, 0.0)

        # delta_location, not location, and only on the roots.
        #
        # bake_anim writes object-level location/rotation/scale channels onto
        # the armature, all keyed at the origin. Setting `location` looks right
        # in the viewport and is then overwritten the moment the frame is
        # evaluated -- which is what a render does first -- so every rigged
        # asset silently snapped back to 0,0,0 and stacked up in the first cell.
        # A delta transform is added on top of the animated value instead.
        for obj in imported:
            if obj.parent is None:
                obj.delta_location = offset

        name = filename[3:-4] if filename.startswith("SM_") else filename[:-4]
        label(name, (offset[0], offset[1] - LABEL_DROP, 0.02))
        meshes = [o for o in imported if o.type == "MESH"]
        placed.append({"name": name, "objects": len(imported),
                       "meshes": len(meshes),
                       "tris": sum(len(m.data.polygons) for m in meshes)})

    # Floor, so the assets sit on something and cast onto it. Centred on the
    # grid, not on the origin: the grid runs away in -Y and a floor centred at
    # zero ran out from under the last row, which reads as the bottom of the
    # sheet being cut off rather than as a floor that is too small.
    rows_so_far = (len(files) + COLUMNS - 1) // COLUMNS
    bpy.ops.mesh.primitive_plane_add(
        size=max(COLUMNS, rows_so_far) * SPACING * 2.5,
        location=((COLUMNS - 1) * SPACING * 0.5,
                  -(rows_so_far - 1) * SPACING * 0.5, -0.01))
    floor = bpy.context.active_object
    mat = bpy.data.materials.new("M_Floor")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (0.62, 0.62, 0.63, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.85
    floor.data.materials.append(mat)

    rows = (len(files) + COLUMNS - 1) // COLUMNS
    centre_x = (COLUMNS - 1) * SPACING * 0.5
    centre_y = -(rows - 1) * SPACING * 0.5

    bpy.ops.object.light_add(type="SUN", location=(centre_x - 12, centre_y - 16, 30))
    sun = bpy.context.active_object
    sun.data.energy = 4.0
    sun.rotation_euler = (math.radians(48), 0, math.radians(38))

    bpy.ops.object.light_add(type="AREA", location=(centre_x + 14, centre_y + 12, 22))
    fill = bpy.context.active_object
    fill.data.energy = 12000.0
    fill.data.size = 30.0
    fill.rotation_euler = (math.radians(-35), 0, math.radians(-40))

    # Orthographic, so every asset is at the same scale and can be compared.
    # A perspective camera would make the far row read as smaller machines.
    # 38 degrees off vertical rather than 56: shallow enough to read the shapes
    # in three dimensions, steep enough that a 4 m machine does not project into
    # the cell behind it.
    bpy.ops.object.camera_add(location=(centre_x, centre_y - 46, 58),
                              rotation=(math.radians(38), 0, 0))
    camera = bpy.context.active_object
    camera.data.type = "ORTHO"
    bpy.context.scene.camera = camera
    fit_camera(camera, 2200 / 1500.0)

    scene = bpy.context.scene
    for engine in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE", "CYCLES"):
        try:
            scene.render.engine = engine
            break
        except TypeError:
            continue

    scene.render.resolution_x = 2200
    scene.render.resolution_y = 1500
    scene.render.film_transparent = False
    scene.render.filepath = OUT
    scene.render.image_settings.file_format = "PNG"
    bpy.ops.render.render(write_still=True)

    # Counted, not eyeballed: the render can hide an asset behind another, so
    # the object tally is what actually proves every file imported.
    return {"assets": len(files), "rows": rows, "engine": scene.render.engine,
            "output": OUT, "placed": placed}
