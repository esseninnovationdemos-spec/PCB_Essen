"""
Infrastructure: what carries product between the machines, and what the
chambers are made of.

Everything here is tileable or repeated. A plant is mostly not machines -- it is
rail, belt, wall panel and door, laid end to end -- so these are the assets that
actually get placed in the hundreds. Each one starts and stops exactly on its
nominal length so copies butt together with no seam and no overlap.

Rail height is 3.1 m to the underside of the flange throughout, matching
RAIL_Z in butchery_stations. It is the datum the whole kill floor hangs from and
nothing may quietly disagree with it.
"""

import math

from butchery_lib import box, cyl, frame, key_spin, key_stroke, tube

LOOP = 60
RAIL_Z = 3.10
RAIL_MODULE = 6.00      # tile length for rail and belt
CARCASS_PITCH = 1.50    # spacing on the rail, and the travel distance per loop


# ===========================================================================
# Overhead rail
# ===========================================================================

def _rail_beam(prefix, length, part=""):
    """The I-section itself, centred on the origin and running along Y."""
    return [
        box(prefix + "_Web", (0.02, length, 0.12), (0, 0, RAIL_Z), "SteelBrushed", part=part),
        box(prefix + "_FlangeTop", (0.09, length, 0.02), (0, 0, RAIL_Z + 0.07),
            "SteelBrushed", part=part),
        box(prefix + "_FlangeBot", (0.09, length, 0.02), (0, 0, RAIL_Z - 0.07),
            "SteelBrushed", part=part),
    ]


def _hangers(prefix, length, spacing=2.0):
    parts = []
    count = max(2, int(round(length / spacing)))
    for index in range(count):
        y = -length / 2.0 + (index + 0.5) * (length / count)
        parts += [
            box("{:s}_Hanger{:d}".format(prefix, index), (0.05, 0.05, 0.70),
                (0, y, RAIL_Z + 0.44), "PaintedFrame"),
            box("{:s}_HangPlate{:d}".format(prefix, index), (0.16, 0.16, 0.02),
                (0, y, RAIL_Z + 0.80), "PaintedFrame"),
        ]
    return parts


def infra_rail_run():
    """
    Six metres of powered rail: beam, hangers, drive chain and a return guide.

    The chain above the beam is what makes it read as powered rather than as a
    gravity rail -- on gravity rail the floor falls and the carcasses slide, and
    a level gravity rail is a rail nothing moves along.
    """
    parts = _rail_beam("Rail", RAIL_MODULE) + _hangers("Rail", RAIL_MODULE)
    parts += [
        box("Rail_ChainRail", (0.06, RAIL_MODULE, 0.06), (0, 0, RAIL_Z + 0.30), "SteelBrushed"),
        box("Rail_ChainGuide", (0.10, RAIL_MODULE, 0.02), (0, 0, RAIL_Z + 0.26), "PaintedFrame"),
    ]
    for index in range(12):
        y = -RAIL_MODULE / 2.0 + (index + 0.5) * (RAIL_MODULE / 12.0)
        parts.append(box("Rail_Dog{:d}".format(index), (0.03, 0.10, 0.08),
                         (0, y, RAIL_Z + 0.30), "SteelBrushed"))
    return parts, [], None


def infra_rail_carcass_run():
    """
    The same run carrying four hanging carcasses, travelling.

    This is the asset that makes the building read as a slaughterhouse rather
    than a warehouse full of stainless boxes. The travel loops over exactly one
    carcass pitch, so the last frame lands where the first began and a row of
    these tiles into a continuously moving line with no visible restart.
    """
    parts = _rail_beam("Rail", RAIL_MODULE) + _hangers("Rail", RAIL_MODULE)
    parts.append(box("Rail_ChainRail", (0.06, RAIL_MODULE, 0.06),
                     (0, 0, RAIL_Z + 0.30), "SteelBrushed"))

    travel = "carcasses"
    count = int(RAIL_MODULE / CARCASS_PITCH)
    for index in range(count):
        y = -RAIL_MODULE / 2.0 + (index + 0.5) * CARCASS_PITCH
        tag = "C{:d}".format(index)

        # Trolley and gambrel.
        parts += [
            cyl("Rail_Wheel" + tag, 0.055, 0.03, (0, y, RAIL_Z - 0.02), "SteelBrushed",
                rot=(0, math.radians(90), 0), part=travel),
            tube("Rail_Shank" + tag, 0.012, 0.26, (0, y, RAIL_Z - 0.20),
                 "SteelBrushed", part=travel),
            box("Rail_Bar" + tag, (0.44, 0.03, 0.03), (0, y, RAIL_Z - 0.34),
                "SteelBrushed", part=travel),
        ]

        # Two halves, one either side of the gambrel. A pig is split before it
        # reaches the chill, so a single whole carcass on the rail would be
        # wrong everywhere downstream of the splitting saw.
        for side, dx in (("L", -0.20), ("R", 0.20)):
            base = RAIL_Z - 0.42
            parts += [
                box("Carcass{:s}{:s}_Ham".format(tag, side), (0.28, 0.21, 0.40),
                    (dx, y, base - 0.20), "Meat", part=travel),
                box("Carcass{:s}{:s}_Loin".format(tag, side), (0.32, 0.19, 0.48),
                    (dx, y + 0.01, base - 0.62), "Meat", part=travel),
                box("Carcass{:s}{:s}_Belly".format(tag, side), (0.30, 0.16, 0.40),
                    (dx, y + 0.02, base - 1.04), "Meat", part=travel),
                box("Carcass{:s}{:s}_Shldr".format(tag, side), (0.26, 0.18, 0.32),
                    (dx, y + 0.01, base - 1.38), "Meat", part=travel),
                box("Carcass{:s}{:s}_Cut".format(tag, side), (0.02, 0.19, 1.16),
                    (dx + (0.14 if side == "L" else -0.14), y + 0.01, base - 0.72),
                    "Fat", part=travel),
            ]

    bones = [{"name": travel, "head": (0.0, -RAIL_MODULE / 2.0, RAIL_Z),
              "axis": (0.0, 1.0, 0.0), "length": 1.0}]

    def animate(rig):
        # One pitch per loop, linear, no dwell: the line does not pause.
        key_stroke(rig, travel, distance=CARCASS_PITCH, frames=LOOP, cycles=1)

    return parts, bones, animate


def infra_rail_curve():
    """A 90 degree bend at 1.5 m radius, approximated in eight segments."""
    radius = 1.50
    parts = []
    steps = 8
    for index in range(steps):
        angle = (index + 0.5) * (math.pi / 2.0) / steps
        seg = radius * (math.pi / 2.0) / steps
        x = radius * math.cos(angle) - radius
        y = radius * math.sin(angle)
        parts += [
            box("Curve_Web{:d}".format(index), (0.02, seg * 1.08, 0.12),
                (x, y, RAIL_Z), "SteelBrushed", rot=(0, 0, -angle)),
            box("Curve_Flange{:d}".format(index), (0.09, seg * 1.08, 0.02),
                (x, y, RAIL_Z - 0.07), "SteelBrushed", rot=(0, 0, -angle)),
        ]
    parts += [
        box("Curve_HangerA", (0.05, 0.05, 0.70), (0, 0.10, RAIL_Z + 0.44), "PaintedFrame"),
        box("Curve_HangerB", (0.05, 0.05, 0.70), (-1.40, 1.45, RAIL_Z + 0.44), "PaintedFrame"),
        box("Curve_Brace", (1.60, 0.05, 0.05), (-0.75, 0.78, RAIL_Z + 0.60), "PaintedFrame"),
    ]
    return parts, [], None


def infra_rail_switch():
    """
    Rail points: a through route and a diverging stub, with the throw lever.

    The lever is the reason this is a separate asset rather than a curve laid
    over a run -- it is where a person decides which chamber a carcass goes to,
    and every routing decision in the building is made at one of these.
    """
    parts = _rail_beam("Switch", 4.00) + _hangers("Switch", 4.00, spacing=2.0)
    for index in range(5):
        t = index / 4.0
        parts.append(box("Switch_Diverge{:d}".format(index), (0.02, 0.90, 0.12),
                         (-0.28 * t * t - 0.06 * t, -1.0 + t * 2.0, RAIL_Z),
                         "SteelBrushed", rot=(0, 0, math.radians(-9 * t))))
    parts += [
        box("Switch_Tongue", (0.03, 0.70, 0.10), (0, -0.90, RAIL_Z), "Stainless"),
        box("Switch_Frog", (0.16, 0.20, 0.13), (-0.10, 0.30, RAIL_Z), "SteelBrushed"),
        box("Switch_LeverPost", (0.07, 0.07, 1.10), (0.34, -0.90, RAIL_Z - 0.60),
            "PaintedFrame"),
        box("Switch_Lever", (0.05, 0.34, 0.05), (0.34, -0.74, RAIL_Z - 0.06), "SafetyYellow"),
        box("Switch_Rod", (0.03, 0.03, 0.30), (0.16, -0.90, RAIL_Z - 0.12), "SteelBrushed"),
    ]
    return parts, [], None


# ===========================================================================
# Conveyors
# ===========================================================================

def infra_belt_conveyor():
    """Six metres of belt at working height, drums turning."""
    parts = [
        box("Belt_Top", (0.80, RAIL_MODULE, 0.04), (0, 0, 0.90), "Rubber"),
        box("Belt_Return", (0.80, RAIL_MODULE, 0.03), (0, 0, 0.66), "Rubber"),
        box("Belt_FrameL", (0.06, RAIL_MODULE, 0.22), (-0.43, 0, 0.83), "Stainless"),
        box("Belt_FrameR", (0.06, RAIL_MODULE, 0.22), (0.43, 0, 0.83), "Stainless"),
        box("Belt_GuideL", (0.03, RAIL_MODULE, 0.10), (-0.40, 0, 0.97), "FoodPlastic"),
        box("Belt_GuideR", (0.03, RAIL_MODULE, 0.10), (0.40, 0, 0.97), "FoodPlastic"),
        box("Belt_Motor", (0.28, 0.32, 0.28), (0.62, RAIL_MODULE / 2 - 0.4, 0.62),
            "PaintedFrame"),
    ]
    for index in range(4):
        y = -RAIL_MODULE / 2.0 + (index + 0.5) * (RAIL_MODULE / 4.0)
        parts += [
            box("Belt_LegL{:d}".format(index), (0.07, 0.07, 0.80), (-0.38, y, 0.40),
                "PaintedFrame"),
            box("Belt_LegR{:d}".format(index), (0.07, 0.07, 0.80), (0.38, y, 0.40),
                "PaintedFrame"),
            box("Belt_Cross{:d}".format(index), (0.80, 0.05, 0.05), (0, y, 0.22),
                "PaintedFrame"),
        ]

    bones = []
    for tag, y in (("drumIn", -RAIL_MODULE / 2.0 + 0.10), ("drumOut", RAIL_MODULE / 2.0 - 0.10)):
        parts.append(cyl("Belt_" + tag, 0.11, 0.80, (0, y, 0.79), "SteelBrushed",
                         rot=(0, math.radians(90), 0), part=tag))
        parts.append(box("Belt_" + tag + "Key", (0.84, 0.05, 0.05), (0, y, 0.79),
                         "PaintedFrame", part=tag))
        bones.append({"name": tag, "head": (-0.40, y, 0.79),
                      "axis": (1.0, 0.0, 0.0), "length": 0.80})

    def animate(rig):
        key_spin(rig, "drumIn", turns=4, frames=LOOP)
        key_spin(rig, "drumOut", turns=4, frames=LOOP)

    return parts, bones, animate


def infra_roller_conveyor():
    """Three metres of gravity roller for cartons and crates."""
    parts = [
        box("Roller_FrameL", (0.05, 3.00, 0.16), (-0.33, 0, 0.82), "Stainless"),
        box("Roller_FrameR", (0.05, 3.00, 0.16), (0.33, 0, 0.82), "Stainless"),
    ]
    for index in range(15):
        y = -1.40 + index * 0.20
        parts.append(cyl("Roller_R{:d}".format(index), 0.032, 0.62, (0, y, 0.88),
                         "SteelBrushed", rot=(0, math.radians(90), 0), verts=10))
    for index in range(3):
        y = -1.20 + index * 1.20
        parts += [
            box("Roller_LegL{:d}".format(index), (0.06, 0.06, 0.74), (-0.31, y, 0.37),
                "PaintedFrame"),
            box("Roller_LegR{:d}".format(index), (0.06, 0.06, 0.74), (0.31, y, 0.37),
                "PaintedFrame"),
            box("Roller_Cross{:d}".format(index), (0.62, 0.04, 0.04), (0, y, 0.18),
                "PaintedFrame"),
        ]
    return parts, [], None


def infra_screw_conveyor():
    """Enclosed trough auger: how trim and inedible material move without spilling."""
    parts = [
        box("Screw_TroughL", (0.04, 4.00, 0.40), (-0.24, 0, 1.10), "Stainless"),
        box("Screw_TroughR", (0.04, 4.00, 0.40), (0.24, 0, 1.10), "Stainless"),
        cyl("Screw_TroughBase", 0.26, 4.00, (0, 0, 0.90), "Stainless",
            rot=(math.radians(90), 0, 0)),
        box("Screw_Lid", (0.56, 4.00, 0.03), (0, 0, 1.31), "Stainless"),
        box("Screw_Inlet", (0.50, 0.60, 0.34), (0, -1.50, 1.48), "Stainless"),
        box("Screw_Outlet", (0.40, 0.34, 0.40), (0, 2.05, 0.72), "Stainless"),
        box("Screw_Motor", (0.34, 0.40, 0.34), (0, -2.30, 0.90), "PaintedFrame"),
        box("Screw_LegA", (0.09, 0.09, 0.72), (-0.20, -1.60, 0.36), "PaintedFrame"),
        box("Screw_LegB", (0.09, 0.09, 0.72), (0.20, -1.60, 0.36), "PaintedFrame"),
        box("Screw_LegC", (0.09, 0.09, 0.72), (-0.20, 1.60, 0.36), "PaintedFrame"),
        box("Screw_LegD", (0.09, 0.09, 0.72), (0.20, 1.60, 0.36), "PaintedFrame"),
    ]
    spin = "auger"
    parts.append(cyl("Screw_Shaft", 0.05, 3.80, (0, 0, 0.90), "SteelBrushed",
                     rot=(math.radians(90), 0, 0), part=spin))
    for index in range(24):
        y = -1.80 + index * 0.155
        parts.append(box("Screw_Flight{:d}".format(index), (0.42, 0.02, 0.42),
                         (0, y, 0.90), "SteelBrushed",
                         rot=(0, index * math.pi / 4.0, 0), part=spin))
    bones = [{"name": spin, "head": (0.0, -1.90, 0.90), "axis": (0.0, 1.0, 0.0), "length": 3.80}]

    def animate(rig):
        key_spin(rig, spin, turns=4, frames=LOOP)

    return parts, bones, animate


def infra_accumulation_table():
    """Rotary accumulation table: the buffer between two lines running at different rates."""
    parts = [
        cyl("Accum_Base", 0.34, 0.80, (0, 0, 0.40), "SteelBrushed"),
        cyl("Accum_Foot", 0.60, 0.08, (0, 0, 0.04), "PaintedFrame"),
        box("Accum_Motor", (0.30, 0.30, 0.30), (0.52, 0, 0.30), "PaintedFrame"),
        box("Accum_Panel", (0.26, 0.10, 0.32), (0.70, 0, 1.00), "PaintedFrame"),
    ]
    # Guard rail around three quarters, open where the operator stands.
    for index in range(14):
        angle = math.radians(40 + index * 22)
        parts.append(box("Accum_Post{:d}".format(index), (0.04, 0.04, 0.26),
                         (math.cos(angle) * 1.02, math.sin(angle) * 1.02, 1.02),
                         "Stainless"))
    spin = "table"
    parts.append(cyl("Accum_Deck", 1.00, 0.05, (0, 0, 0.86), "Stainless",
                     part=spin, verts=32))
    parts.append(cyl("Accum_Lip", 1.03, 0.06, (0, 0, 0.92), "Stainless",
                     part=spin, verts=32))
    bones = [{"name": spin, "head": (0.0, 0.0, 0.82), "axis": (0.0, 0.0, 1.0), "length": 0.50}]

    def animate(rig):
        key_spin(rig, spin, turns=1, frames=LOOP)

    return parts, bones, animate


# ===========================================================================
# Cutting and byproduct
# ===========================================================================

def infra_debone_station():
    """
    One deboning position: the unit the cutting hall is built from, sixteen over.

    Narrower than a trim bench because a boner works one carcass primal at a
    time and stands closer in; the chute is on the right because most of them
    are right-handed and the offcut hand is the left.
    """
    parts = frame("Debone", (1.60, 0.90, 0.95), (0, 0, 0), "Stainless")
    parts += [
        box("Debone_Board", (1.36, 0.72, 0.03), (0, -0.04, 0.965), "FoodPlastic"),
        box("Debone_Splash", (1.60, 0.04, 0.36), (0, 0.43, 1.13), "Stainless"),
        box("Debone_ChuteRim", (0.30, 0.28, 0.04), (0.60, -0.08, 0.955), "Stainless"),
        box("Debone_ChuteBody", (0.26, 0.24, 0.44), (0.60, -0.08, 0.72), "SteelBrushed"),
        box("Debone_Bin", (0.50, 0.40, 0.34), (0.60, -0.08, 0.19), "BluePlastic"),
        box("Debone_Steri", (0.20, 0.18, 0.32), (-0.68, 0.30, 1.11), "Stainless"),
        box("Debone_SteriLid", (0.22, 0.20, 0.03), (-0.68, 0.30, 1.28), "Stainless"),
        box("Debone_HookRail", (1.40, 0.03, 0.03), (0, 0.36, 1.62), "SteelBrushed"),
        box("Debone_LightBar", (1.44, 0.10, 0.07), (0, 0.10, 2.00), "SteelBrushed"),
        box("Debone_LightLens", (1.36, 0.08, 0.02), (0, 0.10, 1.955), "Perspex"),
        box("Debone_PostL", (0.05, 0.05, 0.95), (-0.72, 0.40, 1.55), "PaintedFrame"),
        box("Debone_PostR", (0.05, 0.05, 0.95), (0.72, 0.40, 1.55), "PaintedFrame"),
        box("Debone_Mat", (1.50, 0.70, 0.02), (0, -0.85, 0.01), "Rubber"),
    ]
    for index in range(3):
        parts.append(box("Debone_Hook{:d}".format(index), (0.02, 0.02, 0.16),
                         (-0.40 + index * 0.40, 0.36, 1.52), "Stainless"))
    return parts, [], None


def infra_byproduct_table():
    """
    Offal separation table, red one side and green the other.

    The divider is the whole point: red offal is edible (heart, liver, kidney)
    and green offal is the gut set. They are separated here and must not touch,
    so the table is two tables sharing a frame, with its own wash trough each.
    """
    parts = frame("Bypro", (3.00, 1.20, 0.92), (0, 0, 0), "Stainless")
    parts += [
        box("Bypro_Divider", (0.06, 1.20, 0.34), (0, 0, 1.09), "FoodPlastic"),
        box("Bypro_SplashBack", (3.00, 0.05, 0.40), (0, 0.58, 1.12), "Stainless"),
        box("Bypro_RedTop", (1.40, 1.06, 0.03), (-0.76, -0.03, 0.935), "FoodPlastic"),
        box("Bypro_GreenTop", (1.40, 1.06, 0.03), (0.76, -0.03, 0.935), "BluePlastic"),
        box("Bypro_RedWash", (0.36, 1.00, 0.16), (-1.36, 0, 1.00), "Stainless"),
        box("Bypro_GreenWash", (0.36, 1.00, 0.16), (1.36, 0, 1.00), "Stainless"),
        tube("Bypro_RedFeed", 0.014, 0.36, (-1.36, 0.40, 1.24), "Stainless"),
        tube("Bypro_GreenFeed", 0.014, 0.36, (1.36, 0.40, 1.24), "Stainless"),
        box("Bypro_RedBin", (0.52, 0.42, 0.34), (-0.76, -1.00, 0.17), "FoodPlastic"),
        box("Bypro_GreenBin", (0.52, 0.42, 0.34), (0.76, -1.00, 0.17), "BluePlastic"),
        box("Bypro_Steri", (0.20, 0.18, 0.30), (0, 0.50, 1.10), "Stainless"),
        box("Bypro_LightBar", (2.80, 0.10, 0.07), (0, 0.10, 2.00), "SteelBrushed"),
        box("Bypro_LightLens", (2.70, 0.08, 0.02), (0, 0.10, 1.955), "Perspex"),
        box("Bypro_PostL", (0.05, 0.05, 1.05), (-1.42, 0.50, 1.50), "PaintedFrame"),
        box("Bypro_PostR", (0.05, 0.05, 1.05), (1.42, 0.50, 1.50), "PaintedFrame"),
    ]
    for index in range(4):
        x = -1.05 + index * 0.70
        parts.append(box("Bypro_Pan{:d}".format(index), (0.46, 0.36, 0.10),
                         (x, 0.20, 1.00), "FoodPlastic"))
    return parts, [], None


def infra_offal_chute():
    """Drop chute from the dressing floor into byproduct, with its hygiene skirt."""
    parts = [
        box("Chute_MouthN", (1.10, 0.05, 0.34), (0, 0.55, 1.25), "Stainless"),
        box("Chute_MouthS", (1.10, 0.05, 0.34), (0, -0.55, 1.25), "Stainless"),
        box("Chute_MouthW", (0.05, 1.10, 0.34), (-0.55, 0, 1.25), "Stainless"),
        box("Chute_MouthE", (0.05, 1.10, 0.34), (0.55, 0, 1.25), "Stainless"),
        box("Chute_Rim", (1.24, 1.24, 0.05), (0, 0, 1.44), "Stainless"),
    ]
    # Tapering barrel down to the outlet.
    for index in range(5):
        t = index / 4.0
        size = 1.05 - 0.52 * t
        parts.append(box("Chute_Taper{:d}".format(index), (size, size, 0.24),
                         (0, 0, 1.02 - index * 0.22), "Stainless"))
    parts += [
        cyl("Chute_Outlet", 0.26, 0.34, (0, 0, 0.06), "Stainless"),
        box("Chute_Skirt", (0.80, 0.80, 0.03), (0, 0, 0.24), "FoodPlastic"),
        box("Chute_LegA", (0.07, 0.07, 1.20), (-0.56, -0.56, 0.60), "PaintedFrame"),
        box("Chute_LegB", (0.07, 0.07, 1.20), (0.56, -0.56, 0.60), "PaintedFrame"),
        box("Chute_LegC", (0.07, 0.07, 1.20), (-0.56, 0.56, 0.60), "PaintedFrame"),
        box("Chute_LegD", (0.07, 0.07, 1.20), (0.56, 0.56, 0.60), "PaintedFrame"),
        box("Chute_Label", (0.22, 0.02, 0.14), (0, -0.58, 1.25), "SafetyYellow"),
    ]
    return parts, [], None


def infra_rendering_hopper():
    """Inedible collection hopper with a screw take-off, on load cells."""
    parts = [
        box("Rend_WallN", (2.20, 0.06, 1.00), (0, 1.07, 1.90), "SteelBrushed"),
        box("Rend_WallS", (2.20, 0.06, 1.00), (0, -1.07, 1.90), "SteelBrushed"),
        box("Rend_WallW", (0.06, 2.20, 1.00), (-1.07, 0, 1.90), "SteelBrushed"),
        box("Rend_WallE", (0.06, 2.20, 1.00), (1.07, 0, 1.90), "SteelBrushed"),
        box("Rend_Rim", (2.34, 2.34, 0.06), (0, 0, 2.43), "SteelBrushed"),
    ]
    for index in range(5):
        t = index / 4.0
        size = 2.10 - 1.50 * t
        parts.append(box("Rend_Cone{:d}".format(index), (size, size, 0.20),
                         (0, 0, 1.32 - index * 0.19), "SteelBrushed"))
    parts += [
        box("Rend_Outlet", (0.52, 0.52, 0.26), (0, 0, 0.48), "Stainless"),
        box("Rend_LegA", (0.11, 0.11, 1.50), (-1.00, -1.00, 0.75), "PaintedFrame"),
        box("Rend_LegB", (0.11, 0.11, 1.50), (1.00, -1.00, 0.75), "PaintedFrame"),
        box("Rend_LegC", (0.11, 0.11, 1.50), (-1.00, 1.00, 0.75), "PaintedFrame"),
        box("Rend_LegD", (0.11, 0.11, 1.50), (1.00, 1.00, 0.75), "PaintedFrame"),
        # Load cells: an inedible hopper is weighed, because what leaves has to
        # be reconciled against what was condemned.
        cyl("Rend_CellA", 0.09, 0.10, (-1.00, -1.00, 0.05), "Stainless"),
        cyl("Rend_CellB", 0.09, 0.10, (1.00, -1.00, 0.05), "Stainless"),
        cyl("Rend_CellC", 0.09, 0.10, (-1.00, 1.00, 0.05), "Stainless"),
        cyl("Rend_CellD", 0.09, 0.10, (1.00, 1.00, 0.05), "Stainless"),
        box("Rend_Panel", (0.34, 0.12, 0.44), (1.20, -0.60, 1.40), "PaintedFrame"),
        box("Rend_Display", (0.24, 0.02, 0.14), (1.20, -0.67, 1.52), "Perspex"),
        box("Rend_Warn", (0.26, 0.02, 0.18), (0, -1.11, 2.20), "SafetyYellow"),
    ]
    spin = "takeoff"
    parts.append(cyl("Rend_ScrewTube", 0.18, 1.80, (0, -1.30, 0.40), "Stainless",
                     rot=(math.radians(90), 0, 0)))
    parts.append(cyl("Rend_ScrewShaft", 0.04, 1.70, (0, -1.30, 0.40), "SteelBrushed",
                     rot=(math.radians(90), 0, 0), part=spin))
    for index in range(10):
        y = -2.05 + index * 0.17
        parts.append(box("Rend_Flight{:d}".format(index), (0.30, 0.02, 0.30),
                         (0, y, 0.40), "SteelBrushed",
                         rot=(0, index * math.pi / 3.0, 0), part=spin))
    bones = [{"name": spin, "head": (0.0, -2.15, 0.40), "axis": (0.0, 1.0, 0.0), "length": 1.70}]

    def animate(rig):
        key_spin(rig, spin, turns=3, frames=LOOP)

    return parts, bones, animate


# ===========================================================================
# Chamber shell
# ===========================================================================

def infra_wall_panel():
    """
    Three metres of hygienic sandwich panel, floor to 4 m.

    The coved skirting is not decoration: a square wall-to-floor joint cannot be
    cleaned, so food-industry walls are coved at the base by regulation. It is
    the single detail that separates a food building from a warehouse.
    """
    parts = [
        box("Wall_Panel", (3.00, 0.10, 3.80), (0, 0, 2.00), "FoodPlastic"),
        box("Wall_Cove", (3.00, 0.16, 0.14), (0, -0.03, 0.07), "FoodPlastic"),
        box("Wall_CapRail", (3.00, 0.13, 0.06), (0, 0, 3.93), "Stainless"),
        box("Wall_JointL", (0.03, 0.11, 3.80), (-1.49, 0, 2.00), "Stainless"),
        box("Wall_JointR", (0.03, 0.11, 3.80), (1.49, 0, 2.00), "Stainless"),
        # Kick rail: trolleys hit walls, and every plant has one at bumper height.
        box("Wall_Kick", (3.00, 0.06, 0.16), (0, -0.07, 0.42), "SteelBrushed"),
    ]
    return parts, [], None


def infra_chamber_door():
    """Sliding cold-room door on its track, opening and closing."""
    parts = [
        box("Door_Track", (3.60, 0.14, 0.16), (0, 0.16, 2.86), "SteelBrushed"),
        box("Door_TrackEndL", (0.10, 0.16, 0.24), (-1.75, 0.16, 2.78), "PaintedFrame"),
        box("Door_TrackEndR", (0.10, 0.16, 0.24), (1.75, 0.16, 2.78), "PaintedFrame"),
        box("Door_JambL", (0.14, 0.24, 2.70), (-1.22, 0, 1.35), "Stainless"),
        box("Door_JambR", (0.14, 0.24, 2.70), (1.22, 0, 1.35), "Stainless"),
        box("Door_Head", (2.60, 0.24, 0.14), (0, 0, 2.63), "Stainless"),
        box("Door_Threshold", (2.60, 0.26, 0.03), (0, 0, 0.015), "SteelBrushed"),
        box("Door_Bumper", (0.16, 0.16, 1.00), (-1.45, -0.30, 0.50), "SafetyYellow"),
    ]
    slide = "leaf"
    parts += [
        box("Door_Leaf", (2.30, 0.12, 2.56), (0, 0.10, 1.30), "FoodPlastic", part=slide),
        box("Door_LeafEdge", (2.34, 0.14, 0.08), (0, 0.10, 2.58), "Stainless", part=slide),
        box("Door_Window", (0.46, 0.04, 0.46), (0.55, 0.04, 1.70), "Perspex", part=slide),
        box("Door_Handle", (0.06, 0.10, 0.40), (-0.95, 0.02, 1.10), "SteelBrushed", part=slide),
        box("Door_HangerA", (0.08, 0.10, 0.24), (-0.70, 0.13, 2.70), "SteelBrushed", part=slide),
        box("Door_HangerB", (0.08, 0.10, 0.24), (0.70, 0.13, 2.70), "SteelBrushed", part=slide),
    ]
    bones = [{"name": slide, "head": (0.0, 0.10, 1.30), "axis": (1.0, 0.0, 0.0), "length": 1.20}]

    def animate(rig):
        # Long dwell open: a chamber door is open while a trolley goes through
        # and shut the rest of the time, not oscillating.
        key_stroke(rig, slide, distance=2.20, frames=LOOP, dwell=0.5)

    return parts, bones, animate


def infra_strip_curtain():
    """PVC strip curtain: what actually holds the cold in between chambers."""
    parts = [
        box("Curtain_Rail", (2.20, 0.10, 0.10), (0, 0, 2.55), "Stainless"),
        box("Curtain_BracketL", (0.10, 0.16, 0.14), (-1.02, 0.06, 2.62), "SteelBrushed"),
        box("Curtain_BracketR", (0.10, 0.16, 0.14), (1.02, 0.06, 2.62), "SteelBrushed"),
    ]
    # Overlapping strips, alternating forward and back so they read as a curtain
    # rather than a slab.
    for index in range(16):
        x = -1.02 + index * 0.136
        parts.append(box("Curtain_Strip{:d}".format(index), (0.19, 0.012, 2.40),
                         (x, 0.012 if index % 2 else -0.012, 1.30),
                         "Perspex"))
    return parts, [], None


def infra_boot_wash():
    """
    Hygiene entry: boot brushes, a hand basin and a turnstile you cannot pass dry.

    Laid out so the only path through is over the brushes -- the handrail is on
    both sides for the same reason, not for support.
    """
    parts = [
        box("Boot_TroughFloor", (1.40, 0.90, 0.04), (0, 0, 0.06), "Stainless"),
        box("Boot_TroughL", (0.05, 0.90, 0.20), (-0.70, 0, 0.14), "Stainless"),
        box("Boot_TroughR", (0.05, 0.90, 0.20), (0.70, 0, 0.14), "Stainless"),
        box("Boot_Grate", (1.30, 0.86, 0.03), (0, 0, 0.20), "SteelBrushed"),
    ]
    for index in range(6):
        parts.append(cyl("Boot_Brush{:d}".format(index), 0.07, 0.80,
                         (-0.55 + index * 0.22, 0, 0.13), "FoodPlastic",
                         rot=(math.radians(90), 0, 0), verts=10))
    parts += [
        box("Boot_RailL", (0.05, 0.90, 0.05), (-0.80, 0, 1.00), "Stainless"),
        box("Boot_RailR", (0.05, 0.90, 0.05), (0.80, 0, 1.00), "Stainless"),
        box("Boot_PostLA", (0.06, 0.06, 1.00), (-0.80, -0.42, 0.50), "Stainless"),
        box("Boot_PostLB", (0.06, 0.06, 1.00), (-0.80, 0.42, 0.50), "Stainless"),
        box("Boot_PostRA", (0.06, 0.06, 1.00), (0.80, -0.42, 0.50), "Stainless"),
        box("Boot_PostRB", (0.06, 0.06, 1.00), (0.80, 0.42, 0.50), "Stainless"),
        box("Boot_Basin", (0.44, 0.36, 0.16), (0, 0.86, 0.98), "Stainless"),
        box("Boot_BasinSplash", (0.44, 0.03, 0.28), (0, 1.03, 1.20), "Stainless"),
        tube("Boot_Spout", 0.013, 0.20, (0, 0.98, 1.18), "Stainless"),
        box("Boot_Pedestal", (0.14, 0.14, 0.90), (0, 0.94, 0.45), "Stainless"),
        box("Boot_Soap", (0.10, 0.08, 0.20), (0.26, 1.02, 1.24), "FoodPlastic"),
        box("Boot_Sign", (0.34, 0.02, 0.24), (0, 1.05, 1.62), "SafetyYellow"),
    ]
    return parts, [], None


def infra_pallet_rack():
    """Three bays of pallet racking, three levels, for the cold store."""
    parts = []
    bays, levels = 3, 3
    bay_w, depth, level_h = 2.70, 1.10, 1.80

    for upright in range(bays + 1):
        x = -bays * bay_w / 2.0 + upright * bay_w
        for side, y in (("F", -depth / 2.0), ("B", depth / 2.0)):
            parts.append(box("Rack_Post{:d}{:s}".format(upright, side),
                             (0.09, 0.09, levels * level_h + 0.20),
                             (x, y, (levels * level_h + 0.20) / 2.0), "PaintedFrame"))
        for brace in range(4):
            z = 0.5 + brace * 1.35
            parts.append(box("Rack_Brace{:d}_{:d}".format(upright, brace),
                             (0.05, depth, 0.05), (x, 0, z), "PaintedFrame"))
        parts.append(box("Rack_Foot{:d}".format(upright), (0.16, depth + 0.14, 0.05),
                         (x, 0, 0.025), "SteelBrushed"))

    for bay in range(bays):
        x = -bays * bay_w / 2.0 + (bay + 0.5) * bay_w
        for level in range(levels):
            z = 0.45 + level * level_h
            for side, y in (("F", -depth / 2.0 + 0.12), ("B", depth / 2.0 - 0.12)):
                parts.append(box("Rack_Beam{:d}_{:d}{:s}".format(bay, level, side),
                                 (bay_w - 0.10, 0.08, 0.12), (x, y, z), "SafetyYellow"))
    return parts, [], None


INFRA = {
    "RAIL_RUN": infra_rail_run,
    "RAIL_CARCASS_RUN": infra_rail_carcass_run,
    "RAIL_CURVE": infra_rail_curve,
    "RAIL_SWITCH": infra_rail_switch,
    "BELT_CONVEYOR": infra_belt_conveyor,
    "ROLLER_CONVEYOR": infra_roller_conveyor,
    "SCREW_CONVEYOR": infra_screw_conveyor,
    "ACCUMULATION_TABLE": infra_accumulation_table,
    "DEBONE_STATION": infra_debone_station,
    "BYPRODUCT_TABLE": infra_byproduct_table,
    "OFFAL_CHUTE": infra_offal_chute,
    "RENDERING_HOPPER": infra_rendering_hopper,
    "WALL_PANEL": infra_wall_panel,
    "CHAMBER_DOOR": infra_chamber_door,
    "STRIP_CURTAIN": infra_strip_curtain,
    "BOOT_WASH": infra_boot_wash,
    "PALLET_RACK": infra_pallet_rack,
}
