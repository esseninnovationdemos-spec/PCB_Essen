"""
Shared modelling helpers for the butchery line.

Everything is built parametrically in metres, origins on the floor at the
footprint centre, so a station drops into Unreal at its world position with no
fiddling. Real dimensions throughout -- a scald tank is 4.2 m long because that
is roughly what a scald tank is, and getting that wrong is the one modelling
error you cannot fix later without re-laying the whole hall.

Moving parts are skinned to a bone rather than left as separate objects.
Unreal's FBX importer ignores object-level animation on a static mesh, so the
only way animation survives the trip is a skeletal mesh: one bone per moving
part, rigid weights, animation baked onto the bones.

The convention that makes that work: **a bone points along its own axis of
motion**. A bone is rotated about its local Y, and translated along its local Y,
so orienting the bone along the drum's spin axis (or the blade's stroke) means
the pose channel is always Y and never needs a per-part special case.
"""

import math

import bmesh  # pylint: disable=import-error
import bpy  # pylint: disable=import-error
from mathutils import Vector  # pylint: disable=import-error

# ---------------------------------------------------------------------------
# Materials
#
# A small palette, because a meat plant is a small palette: stainless
# everywhere, painted steel frames, food-grade plastic, and a few accents.
# ---------------------------------------------------------------------------

# name -> (base colour RGB, metallic, roughness)
PALETTE = {
    "Stainless":     ((0.72, 0.74, 0.76), 1.0, 0.28),
    "SteelBrushed":  ((0.58, 0.60, 0.62), 1.0, 0.45),
    "PaintedFrame":  ((0.24, 0.26, 0.29), 0.6, 0.55),
    "SafetyYellow":  ((0.86, 0.66, 0.05), 0.0, 0.55),
    "FoodPlastic":   ((0.88, 0.89, 0.90), 0.0, 0.42),
    "BluePlastic":   ((0.10, 0.32, 0.58), 0.0, 0.42),
    "Rubber":        ((0.07, 0.07, 0.08), 0.0, 0.78),
    "Perspex":       ((0.72, 0.80, 0.84), 0.0, 0.18),
    "Meat":          ((0.55, 0.16, 0.16), 0.0, 0.62),
    "Fat":           ((0.90, 0.87, 0.78), 0.0, 0.55),
    "Copper":        ((0.72, 0.45, 0.20), 1.0, 0.35),
    "Concrete":      ((0.55, 0.54, 0.52), 0.0, 0.85),
    "Carton":        ((0.62, 0.45, 0.26), 0.0, 0.80),
    "Timber":        ((0.66, 0.52, 0.33), 0.0, 0.72),
    # Indicator lenses. Named for what they mean rather than what colour they
    # are, so a panel lamp and a dock traffic light can share them.
    "LampPass":      ((0.06, 0.85, 0.18), 0.0, 0.30),
    "LampFail":      ((0.88, 0.06, 0.06), 0.0, 0.30),
}


def material(name):
    """Fetch or build one palette material. Idempotent."""
    key = "M_Butchery_" + name
    existing = bpy.data.materials.get(key)
    if existing is not None:
        return existing

    colour, metallic, roughness = PALETTE[name]
    mat = bpy.data.materials.new(key)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (*colour, 1.0)
        bsdf.inputs["Metallic"].default_value = metallic
        bsdf.inputs["Roughness"].default_value = roughness
    mat.diffuse_color = (*colour, 1.0)
    return mat


# ---------------------------------------------------------------------------
# Primitives
#
# Each returns a mesh object already carrying its material and its part name.
# `part` is what the vertex group will be called, and therefore which bone the
# geometry ends up rigid-skinned to -- "" means it belongs to the static body.
# ---------------------------------------------------------------------------

def _register(obj, name, mat_name, part, prim="box"):
    obj.name = name
    obj.data.name = name
    obj.data.materials.append(material(mat_name))
    obj["part"] = part
    # Which primitive this is. Only boxes are used as occluders when culling
    # buried faces: a box's bounding volume is the box, where a cylinder's
    # overestimates it at the corners and would eat faces that are actually
    # visible beside it.
    obj["prim"] = prim
    return obj


def _auto_verts(radius):
    """
    Segment count for a cylinder, from its radius.

    A 12 mm pipe drawn with 24 sides costs the same as a 1 m tank drawn with
    24 sides and is a smooth circle roughly two pixels across. Below is what
    actually reads at the distance each size is seen from.
    """
    if radius <= 0.03:
        return 6
    if radius <= 0.08:
        return 8
    if radius <= 0.20:
        return 12
    if radius <= 0.50:
        return 16
    return 24


def box(name, size, loc, mat_name="Stainless", rot=(0, 0, 0), part=""):
    """An axis-aligned box, sized in metres, positioned by its centre."""
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=loc, rotation=rot)
    obj = bpy.context.active_object
    obj.scale = Vector(size)
    return _register(obj, name, mat_name, part)


def cyl(name, radius, depth, loc, mat_name="Stainless", rot=(0, 0, 0),
        part="", verts=None):
    """A cylinder along local Z unless rotated. Resolution follows the radius."""
    bpy.ops.mesh.primitive_cylinder_add(
        radius=radius, depth=depth, location=loc, rotation=rot,
        vertices=verts if verts else _auto_verts(radius))
    return _register(bpy.context.active_object, name, mat_name, part, prim="cyl")


def ring(name, radius, depth, loc, mat_name="Stainless", rot=(0, 0, 0),
         part="", verts=None):
    """
    An open ring: a cylinder with both end caps removed.

    A fan guard drawn as a solid cylinder is a plate over the fan -- it hides
    the thing it is guarding, and the animation behind it is paid for and never
    seen. Dropping the caps also costs nothing: the caps are the expensive part
    of a low-poly cylinder, being two n-gons that triangulate into 2*(n-2).
    """
    obj = cyl(name, radius, depth, loc, mat_name, rot, part, verts)
    mesh = bmesh.new()
    mesh.from_mesh(obj.data)
    mesh.faces.ensure_lookup_table()
    caps = [f for f in mesh.faces if abs(abs(f.normal.z) - 1.0) < 1e-3]
    if caps:
        bmesh.ops.delete(mesh, geom=caps, context="FACES_ONLY")
        mesh.to_mesh(obj.data)
        obj.data.update()
    mesh.free()
    return obj


def tube(name, radius, depth, loc, mat_name="Stainless", rot=(0, 0, 0),
         part="", verts=None):
    """Alias for a thin cylinder, for pipes and rails. Reads better in a part list."""
    return cyl(name, radius, depth, loc, mat_name, rot, part, verts)


def frame(name_prefix, size, loc, mat_name="PaintedFrame", leg=0.06, part=""):
    """
    Four legs and a top plate -- the bench every meat plant is built from.

    Returns the list of parts rather than one object, so the caller can add to
    it before the station is joined.
    """
    width, depth, height = size
    x, y, z = loc
    parts = [
        box("{:s}_Top".format(name_prefix), (width, depth, 0.05),
            (x, y, z + height - 0.025), "Stainless", part=part),
    ]
    for index, (dx, dy) in enumerate((
            (-1, -1), (1, -1), (-1, 1), (1, 1))):
        parts.append(box(
            "{:s}_Leg{:d}".format(name_prefix, index),
            (leg, leg, height - 0.05),
            (x + dx * (width * 0.5 - leg), y + dy * (depth * 0.5 - leg),
             z + (height - 0.05) * 0.5),
            mat_name, part=part))
    return parts


def rail_section(name, length, loc, height=3.1, part=""):
    """
    A length of overhead rail with its hanger.

    Everything in a slaughterhouse hangs from this. It is modelled as a real
    I-section rather than a bar because it is the most recognisable single
    object in the building.
    """
    x, y, z = loc
    return [
        box(name + "_Web", (0.02, length, 0.12), (x, y, z + height),
            "SteelBrushed", part=part),
        box(name + "_FlangeTop", (0.09, length, 0.02), (x, y, z + height + 0.06),
            "SteelBrushed", part=part),
        box(name + "_FlangeBot", (0.09, length, 0.02), (x, y, z + height - 0.06),
            "SteelBrushed", part=part),
        box(name + "_Hanger", (0.05, 0.05, 0.45),
            (x, y, z + height + 0.28), "PaintedFrame", part=part),
    ]


# ---------------------------------------------------------------------------
# Assembly
# ---------------------------------------------------------------------------

def _select_only(objs):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objs:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]


def unwrap(obj):
    """
    Smart-project UVs.

    Not optional: Unreal builds lightmaps from a UV channel, and a mesh with no
    UVs imports with a warning and lights badly. Cube projection would be
    cheaper but falls apart on the cylinders, which is most of this library.
    """
    _select_only([obj])
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=1.15, island_margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")


def cull_buried_faces(parts, margin=1e-4):
    """
    Deletes faces whose centre lies inside another part.

    These assets are built by overlapping primitives -- a leg pushed into a
    bench top, a lamp sitting on a roof, a shaft running through a housing --
    and every surface inside another solid is geometry nobody can ever see.
    On a library assembled this way that is most of the triangles.

    Only boxes occlude. A box's bounding volume is exactly the box, so the test
    is exact; a cylinder's bounding box is larger than the cylinder at the
    corners and would delete faces sitting legitimately beside it.

    Every corner of the face must be inside, not merely its centre. A face's
    centre is not where the face is: three metres of pen rail threaded through a
    seven-centimetre post has side faces that run the whole length, so every one
    of their centres sits at the middle -- inside the post -- and testing centres
    deletes the entire rail on the strength of the 7 cm it passes through.

    Faces exactly flush with an occluder go too. Two parts meeting face to face
    have two coincident surfaces that z-fight and are invisible either way, so
    dropping both is right -- the hole it leaves is on the inside.

    @return (faces removed, faces kept, names of wholly buried parts)
    """
    # box() sets obj.scale directly and matrix_world is evaluated lazily, so
    # without this every occluder reads back as the 1 m unit cube it was before
    # it was sized. That does not fail loudly: it culls whatever happens to sit
    # near a part's centre and spares whatever sits outside a metre of it, which
    # looks like a plausible saving right up until you notice a crate has been
    # reduced to one handle.
    bpy.context.view_layer.update()

    occluders = []
    for obj in parts:
        if obj.get("prim") == "box":
            # The cube primitive is built at +/-0.5 and scaled by the object, so
            # its local half-extent is always 0.5 whatever size it ended up.
            occluders.append((obj, obj.matrix_world.inverted()))

    removed = 0
    kept = 0
    buried = []
    limit = 0.5 + margin

    for obj in parts:
        matrix = obj.matrix_world
        mesh = bmesh.new()
        mesh.from_mesh(obj.data)
        mesh.faces.ensure_lookup_table()

        doomed = []
        for face in mesh.faces:
            corners = [matrix @ vert.co for vert in face.verts]
            for occluder, inverse in occluders:
                if occluder is obj:
                    continue
                inside = True
                for corner in corners:
                    local = inverse @ corner
                    if (abs(local.x) > limit or abs(local.y) > limit
                            or abs(local.z) > limit):
                        inside = False
                        break
                if inside:
                    doomed.append(face)
                    break

        # Culling thins a part; it must never delete one. A part with every
        # face buried is not a saving, it is a part modelled inside another
        # solid -- and if it is animated, deleting it leaves a bone driving
        # nothing at all. Keep it whole and name it, so the fault gets fixed
        # where it was made instead of being quietly swept up here.
        if doomed and len(doomed) == len(mesh.faces):
            buried.append(obj.name)
            kept += len(mesh.faces)
            mesh.free()
            continue

        removed += len(doomed)
        kept += len(mesh.faces) - len(doomed)
        if doomed:
            bmesh.ops.delete(mesh, geom=doomed, context="FACES")
            # Verts left with no face are dead weight in the export.
            loose = [v for v in mesh.verts if not v.link_faces]
            if loose:
                bmesh.ops.delete(mesh, geom=loose, context="VERTS")
            mesh.to_mesh(obj.data)
            obj.data.update()
        mesh.free()

    return removed, kept, buried


def assemble(parts, name):
    """
    Join a part list into one mesh, preserving which bone each part belongs to.

    Vertex groups are written *before* the join so the group membership rides
    through it; joining first and assigning after would need the original vertex
    ranges, which the join does not report.
    """
    # Before the groups are written, because the group covers every vertex the
    # object still has and culling changes that count.
    _, _, buried = cull_buried_faces(parts)
    if buried:
        print("  buried inside another solid, kept whole: {}".format(
            ", ".join(sorted(buried))))

    # A part swallowed whole by another leaves nothing to join, and an empty
    # mesh in the join list produces a warning and no geometry.
    parts = [obj for obj in parts if len(obj.data.polygons) > 0]

    for obj in parts:
        part = obj.get("part", "")
        group = obj.vertex_groups.new(name=part if part else "root")
        group.add(range(len(obj.data.vertices)), 1.0, "REPLACE")

    _select_only(parts)
    bpy.ops.object.join()
    merged = bpy.context.active_object
    merged.name = name
    merged.data.name = name

    # Triangulate n-gons only, leaving the boxes as quads.
    #
    # Cylinder caps come out of Blender as a single many-sided face, and the FBX
    # exporter refuses to compute tangents for a mesh containing any of them --
    # "cannot compute/export tangent space". Without tangents, normal maps in
    # Unreal light incorrectly. Triangulating everything would fix it too, but
    # quads survive later editing far better, so only the offenders are split.
    _select_only([merged])
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="DESELECT")
    bpy.ops.mesh.select_mode(type="FACE")
    bpy.ops.mesh.select_face_by_sides(number=4, type="GREATER", extend=False)
    bpy.ops.mesh.quads_convert_to_tris(quad_method="BEAUTY", ngon_method="BEAUTY")
    bpy.ops.mesh.select_all(action="DESELECT")
    bpy.ops.object.mode_set(mode="OBJECT")

    # Transforms are applied here rather than at export. Leaving a scale of
    # (2, 0.5, 1) on the object exports geometry that is correct only if the
    # importer honours it, and non-uniform scale on a skinned mesh is a
    # well-known way to get sheared bones in Unreal.
    _select_only([merged])
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

    unwrap(merged)
    try:
        bpy.ops.object.shade_auto_smooth(angle=math.radians(40.0))
    except Exception:  # pylint: disable=broad-exception-caught
        bpy.ops.object.shade_flat()
    return merged


def set_origin_to_floor(obj, floor_z=None):
    """
    Drop the origin to the middle of the footprint at floor level.

    Unreal places actors by their origin, so a station whose origin is at its
    bounding-box centre floats half its own height above the floor. This is the
    single most common reason imported machinery sits wrong.

    `floor_z` overrides where "floor" is. Ceiling-hung assets need it: an
    overhead rail's lowest geometry is 3 m up, so taking the bounding box would
    put its origin at the rail and placing it at world zero would lay the rail
    on the ground. Passing 0.0 keeps the origin at the floor the rail hangs
    above, which is the height the rest of the plant is dimensioned from.
    """
    _select_only([obj])
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    centre_x = sum(c.x for c in corners) / 8.0
    centre_y = sum(c.y for c in corners) / 8.0
    if floor_z is None:
        floor_z = min(c.z for c in corners)

    cursor = bpy.context.scene.cursor.location.copy()
    bpy.context.scene.cursor.location = Vector((centre_x, centre_y, floor_z))
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    bpy.context.scene.cursor.location = cursor

    obj.location = (0.0, 0.0, 0.0)
    return obj


# ---------------------------------------------------------------------------
# Rigging
# ---------------------------------------------------------------------------

def build_armature(name, bones, mesh):
    """
    Build an armature and rigid-skin the mesh to it.

    `bones` is a list of dicts: {name, head, axis, length}. Each bone points
    along its own axis of motion, so every pose channel is the bone's local Y --
    see the module docstring.

    A "root" bone always exists and carries the static body, because a skeletal
    mesh with unweighted vertices collapses them to the origin on import.
    """
    armature_data = bpy.data.armatures.new(name + "_Armature")
    rig = bpy.data.objects.new(name + "_Armature", armature_data)
    bpy.context.collection.objects.link(rig)

    _select_only([rig])
    bpy.ops.object.mode_set(mode="EDIT")

    root = armature_data.edit_bones.new("root")
    root.head = (0.0, 0.0, 0.0)
    root.tail = (0.0, 0.0, 0.25)

    for spec in bones:
        bone = armature_data.edit_bones.new(spec["name"])
        head = Vector(spec["head"])
        axis = Vector(spec["axis"]).normalized()
        bone.head = head
        bone.tail = head + axis * spec.get("length", 0.3)
        bone.parent = root

    bpy.ops.object.mode_set(mode="OBJECT")

    # Parent without automatic weights: these are rigid machine parts, and
    # automatic weights would bleed a drum's influence into the frame holding it.
    mesh.parent = rig
    modifier = mesh.modifiers.new(name="Armature", type="ARMATURE")
    modifier.object = rig
    return rig


def key_spin(rig, bone_name, turns, frames, start=1):
    """Keyframe a continuous rotation about the bone's own axis."""
    pose_bone = rig.pose.bones[bone_name]
    pose_bone.rotation_mode = "XYZ"
    for index in range(turns + 1):
        frame = start + int(round(index * frames / float(turns)))
        pose_bone.rotation_euler = (0.0, index * 2.0 * math.pi / turns, 0.0)
        pose_bone.keyframe_insert("rotation_euler", frame=frame)
    _linear(rig, bone_name)


def key_stroke(rig, bone_name, distance, frames, start=1, dwell=0.0, cycles=1):
    """
    Keyframe a there-and-back translation along the bone's own axis.

    `dwell` is the fraction of the cycle spent held at the far end -- a press
    that slams shut and immediately opens reads as a twitch, not a machine.

    `cycles` repeats the stroke to fill `frames`. It exists because a fast part
    inside a slow loop -- a saw blade against a two-second carcass cycle -- has
    to keep moving for the whole loop. Keying one short stroke and leaving the
    rest of the loop empty produces a part that twitches once and then sits
    still, which looks far more broken than not animating it at all.
    """
    pose_bone = rig.pose.bones[bone_name]
    span = frames / float(cycles)
    hold = int(span * dwell * 0.5)

    for cycle in range(cycles):
        base = start + int(round(cycle * span))
        mid = base + int(span // 2)
        for frame, value in (
                (base, 0.0),
                (mid - hold, distance),
                (mid + hold, distance),
                (base + int(round(span)), 0.0)):
            pose_bone.location = (0.0, value, 0.0)
            pose_bone.keyframe_insert("location", frame=frame)


def iter_fcurves(anim_owner):
    """
    Every F-curve on an object's action, across Blender's two action layouts.

    Blender 5 replaced the flat `action.fcurves` list with slotted actions:
    curves now live at `action.layers[].strips[].channelbag(slot).fcurves`. The
    old attribute is simply gone, so anything walking curves has to handle both
    or break on one version or the other.
    """
    adt = getattr(anim_owner, "animation_data", None)
    action = getattr(adt, "action", None)
    if action is None:
        return

    flat = getattr(action, "fcurves", None)
    if flat is not None:
        yield from flat
        return

    slot = getattr(adt, "action_slot", None)
    for layer in action.layers:
        for strip in layer.strips:
            bag = None
            if slot is not None:
                try:
                    bag = strip.channelbag(slot)
                except Exception:  # pylint: disable=broad-exception-caught
                    bag = None
            if bag is not None:
                yield from bag.fcurves
            else:
                for other in getattr(strip, "channelbags", ()):
                    yield from other.fcurves


def _linear(rig, bone_name):
    """
    Force linear interpolation.

    Bezier easing on a continuous rotation makes a drum visibly slow down and
    speed up once per revolution, which reads as a fault rather than a machine
    running.
    """
    for curve in iter_fcurves(rig):
        if bone_name in curve.data_path:
            for keyframe in curve.keyframe_points:
                keyframe.interpolation = "LINEAR"
