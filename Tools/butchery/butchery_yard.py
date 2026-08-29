"""
The rooms nobody models: lairage, plant room, dispatch, hygiene, rendering.

A plant is not only its process line. Half the floor area is animals waiting,
refrigeration running, people changing, and product leaving -- and a level with
those rooms empty reads as a stage set of the interesting part rather than as a
factory. These are the assets that fill them.

Same conventions as the rest of the library: metres, origins on the floor,
tileable pieces starting and stopping on their nominal length.
"""

import math

from butchery_lib import box, cyl, frame, key_spin, tube

LOOP = 60


# ===========================================================================
# Lairage
# ===========================================================================

def yard_pen_rail():
    """
    Three metres of pen railing, tileable.

    Horizontal rails, not vertical bars: a pig pushes with its shoulder and a
    vertical bar lets a snout through. Everything in a lairage is built to be
    hosed and to not injure an animal that leans on it.
    """
    parts = []
    for index, z in enumerate((0.28, 0.58, 0.88)):
        parts.append(tube("Pen_Rail{:d}".format(index), 0.025, 3.00, (0, 0, z),
                          "SteelBrushed", rot=(math.radians(90), 0, 0), verts=10))
    for index, y in enumerate((-1.48, 0.0, 1.48)):
        parts.append(box("Pen_Post{:d}".format(index), (0.07, 0.07, 1.05),
                         (0, y, 0.52), "SteelBrushed"))
        parts.append(box("Pen_Foot{:d}".format(index), (0.16, 0.16, 0.02),
                         (0, y, 0.01), "SteelBrushed"))
    return parts, [], None


def yard_pen_gate():
    """A hung pen gate, with the drop latch that actually holds it."""
    parts = [
        box("Gate_PostHinge", (0.08, 0.08, 1.20), (0, -1.20, 0.60), "SteelBrushed"),
        box("Gate_PostLatch", (0.08, 0.08, 1.20), (0, 1.20, 0.60), "SteelBrushed"),
        box("Gate_Frame", (0.05, 2.30, 0.05), (0, 0, 1.02), "PaintedFrame"),
        box("Gate_FrameLow", (0.05, 2.30, 0.05), (0, 0, 0.22), "PaintedFrame"),
    ]
    for index, z in enumerate((0.42, 0.62, 0.82)):
        parts.append(tube("Gate_Rail{:d}".format(index), 0.022, 2.28, (0, 0, z),
                          "SteelBrushed", rot=(math.radians(90), 0, 0), verts=10))
    parts += [
        box("Gate_Brace", (0.04, 2.20, 0.04), (0, 0, 0.62), "SteelBrushed",
            rot=(math.radians(20), 0, 0)),
        box("Gate_Latch", (0.06, 0.16, 0.10), (0, 1.12, 0.90), "SafetyYellow"),
        cyl("Gate_Hinge", 0.035, 0.14, (0, -1.14, 0.85), "SteelBrushed"),
    ]
    return parts, [], None


def yard_water_trough():
    """Bite drinker over a trough. Animals in lairage are watered, never fed."""
    parts = [
        box("Trough_Body", (0.42, 1.60, 0.24), (0, 0, 0.42), "Stainless"),
        box("Trough_Floor", (0.38, 1.56, 0.03), (0, 0, 0.32), "Stainless"),
        box("Trough_Bracket", (0.10, 0.10, 0.32), (0.16, -0.60, 0.16), "SteelBrushed"),
        box("Trough_Bracket2", (0.10, 0.10, 0.32), (0.16, 0.60, 0.16), "SteelBrushed"),
        tube("Trough_Feed", 0.018, 0.90, (0.20, 0, 0.80), "Stainless"),
        tube("Trough_Header", 0.022, 1.70, (0.20, 0, 1.22), "Stainless",
             rot=(math.radians(90), 0, 0)),
    ]
    for index, y in enumerate((-0.5, 0.0, 0.5)):
        parts.append(tube("Trough_Nipple{:d}".format(index), 0.012, 0.16,
                          (0.14, y, 1.12), "Stainless", rot=(0, math.radians(35), 0)))
    return parts, [], None


def yard_crowd_race():
    """
    Three metres of single-file race, tapering.

    Solid sides, not railed: an animal that cannot see sideways moves forward,
    and this is the one place in the building where that matters more than
    being able to see in.
    """
    parts = [
        box("Race_WallL", (0.06, 3.00, 1.05), (-0.44, 0, 0.52), "FoodPlastic"),
        box("Race_WallR", (0.06, 3.00, 1.05), (0.44, 0, 0.52), "FoodPlastic"),
        box("Race_CapL", (0.10, 3.00, 0.05), (-0.44, 0, 1.07), "SteelBrushed"),
        box("Race_CapR", (0.10, 3.00, 0.05), (0.44, 0, 1.07), "SteelBrushed"),
        box("Race_Floor", (0.90, 3.00, 0.04), (0, 0, 0.02), "Concrete"),
    ]
    # Cleated floor: a wet concrete slope is where animals fall.
    for index in range(10):
        parts.append(box("Race_Cleat{:d}".format(index), (0.86, 0.05, 0.03),
                         (0, -1.35 + index * 0.30, 0.05), "Concrete"))
    for index, y in enumerate((-1.3, 0.0, 1.3)):
        parts.append(box("Race_PostL{:d}".format(index), (0.07, 0.07, 1.10),
                         (-0.50, y, 0.55), "SteelBrushed"))
        parts.append(box("Race_PostR{:d}".format(index), (0.07, 0.07, 1.10),
                         (0.50, y, 0.55), "SteelBrushed"))
    return parts, [], None


def yard_unload_ramp():
    """Lorry unloading ramp with side walls and a levelling lip."""
    parts = [
        box("Ramp_Deck", (3.20, 4.00, 0.14), (0, 0, 0.52), "Concrete",
            rot=(math.radians(-9), 0, 0)),
        box("Ramp_WallL", (0.10, 4.00, 1.10), (-1.60, 0, 1.05), "FoodPlastic",
            rot=(math.radians(-9), 0, 0)),
        box("Ramp_WallR", (0.10, 4.00, 1.10), (1.60, 0, 1.05), "FoodPlastic",
            rot=(math.radians(-9), 0, 0)),
        box("Ramp_Lip", (3.20, 0.50, 0.06), (0, -2.20, 0.20), "SteelBrushed"),
        box("Ramp_Kerb", (3.40, 4.20, 0.30), (0, 0, 0.15), "Concrete"),
        box("Ramp_BumperL", (0.20, 0.24, 0.60), (-1.70, -2.10, 0.60), "Rubber"),
        box("Ramp_BumperR", (0.20, 0.24, 0.60), (1.70, -2.10, 0.60), "Rubber"),
        box("Ramp_Light", (0.16, 0.12, 0.30), (1.80, -1.80, 1.90), "SafetyYellow"),
        box("Ramp_Post", (0.10, 0.10, 1.90), (1.80, -1.80, 0.95), "PaintedFrame"),
    ]
    return parts, [], None


# ===========================================================================
# Plant room
# ===========================================================================

def yard_compressor_skid():
    """Screw compressor on a skid with its oil separator and receiver."""
    parts = [
        box("Comp_Skid", (2.40, 1.20, 0.22), (0, 0, 0.11), "PaintedFrame"),
        cyl("Comp_Body", 0.30, 1.40, (-0.30, 0, 0.70), "SteelBrushed",
            rot=(0, math.radians(90), 0)),
        box("Comp_Motor", (0.80, 0.62, 0.62), (0.72, 0, 0.62), "PaintedFrame"),
        cyl("Comp_MotorFan", 0.24, 0.14, (1.18, 0, 0.62), "SteelBrushed",
            rot=(0, math.radians(90), 0)),
        cyl("Comp_Separator", 0.32, 1.30, (-0.30, 0.62, 0.98), "SteelBrushed"),
        cyl("Comp_Receiver", 0.26, 1.90, (-1.05, -0.40, 0.98), "SteelBrushed",
            rot=(math.radians(90), 0, 0)),
        tube("Comp_Suction", 0.09, 1.40, (-0.30, -0.55, 1.30), "SteelBrushed",
             rot=(math.radians(90), 0, 0)),
        tube("Comp_Discharge", 0.06, 1.20, (0.20, 0.62, 1.70), "Copper"),
        box("Comp_Panel", (0.44, 0.16, 0.60), (0.10, -0.70, 1.20), "PaintedFrame"),
        box("Comp_Display", (0.24, 0.02, 0.16), (0.10, -0.79, 1.32), "Perspex"),
        box("Comp_Guard", (0.34, 0.68, 0.68), (1.14, 0, 0.62), "SafetyYellow"),
    ]
    return parts, [], None


def yard_condenser():
    """Evaporative condenser with two fans, running."""
    parts = [
        box("Cond_Body", (3.00, 1.60, 1.70), (0, 0, 1.05), "SteelBrushed"),
        box("Cond_Base", (3.20, 1.80, 0.20), (0, 0, 0.10), "PaintedFrame"),
        box("Cond_Deck", (3.00, 1.60, 0.08), (0, 0, 1.94), "PaintedFrame"),
        tube("Cond_In", 0.08, 1.00, (-1.20, 0.85, 1.60), "Copper",
             rot=(math.radians(90), 0, 0)),
        tube("Cond_Out", 0.06, 1.00, (1.20, 0.85, 1.20), "Copper",
             rot=(math.radians(90), 0, 0)),
        box("Cond_Louvre", (2.90, 0.04, 0.90), (0, -0.80, 0.70), "SteelBrushed"),
        box("Cond_Panel", (0.34, 0.10, 0.44), (1.30, -0.86, 1.20), "PaintedFrame"),
    ]
    bones = []
    for tag, x in (("fanL", -0.72), ("fanR", 0.72)):
        parts.append(cyl("Cond_Ring" + tag, 0.56, 0.14, (x, 0, 2.02),
                         "PaintedFrame", part=""))
        parts.append(cyl("Cond_Hub" + tag, 0.12, 0.16, (x, 0, 2.08),
                         "SteelBrushed", part=tag))
        for blade in range(4):
            angle = blade * math.pi / 2.0
            parts.append(box("Cond_Blade{:s}{:d}".format(tag, blade),
                             (0.80, 0.16, 0.05),
                             (x + math.cos(angle) * 0.30, math.sin(angle) * 0.30, 2.08),
                             "SteelBrushed", rot=(0, 0, angle), part=tag))
        bones.append({"name": tag, "head": (x, 0.0, 2.00),
                      "axis": (0.0, 0.0, 1.0), "length": 0.40})

    def animate(rig):
        key_spin(rig, "fanL", turns=6, frames=LOOP)
        key_spin(rig, "fanR", turns=6, frames=LOOP)

    return parts, bones, animate


def yard_electrical_panel():
    """A run of motor-control cabinets."""
    parts = [box("MCC_Plinth", (2.70, 0.70, 0.12), (0, 0, 0.06), "PaintedFrame")]
    for index in range(3):
        x = -0.88 + index * 0.88
        tag = "C{:d}".format(index)
        parts += [
            box("MCC_Body" + tag, (0.84, 0.62, 1.90), (x, 0, 1.07), "PaintedFrame"),
            box("MCC_Door" + tag, (0.78, 0.03, 1.76), (x, -0.32, 1.07), "SteelBrushed"),
            box("MCC_Handle" + tag, (0.05, 0.05, 0.24), (x + 0.32, -0.35, 1.10),
                "SteelBrushed"),
            box("MCC_Label" + tag, (0.30, 0.01, 0.10), (x, -0.34, 1.86), "SafetyYellow"),
            box("MCC_Lamp" + tag, (0.06, 0.04, 0.06), (x - 0.28, -0.34, 1.76), "LampPass"),
        ]
    parts += [
        box("MCC_Trunking", (2.70, 0.20, 0.22), (0, 0.24, 2.18), "SteelBrushed"),
        box("MCC_Cap", (2.74, 0.66, 0.06), (0, 0, 2.05), "PaintedFrame"),
    ]
    return parts, [], None


def yard_pipe_rack():
    """Six metres of pipe bridge, tileable, at high level."""
    parts = [
        box("Rack_BeamL", (0.10, 6.00, 0.16), (-0.60, 0, 3.40), "PaintedFrame"),
        box("Rack_BeamR", (0.10, 6.00, 0.16), (0.60, 0, 3.40), "PaintedFrame"),
    ]
    for index, y in enumerate((-2.4, 0.0, 2.4)):
        parts += [
            box("Rack_LegL{:d}".format(index), (0.10, 0.10, 3.40), (-0.60, y, 1.70),
                "PaintedFrame"),
            box("Rack_LegR{:d}".format(index), (0.10, 0.10, 3.40), (0.60, y, 1.70),
                "PaintedFrame"),
            box("Rack_Cross{:d}".format(index), (1.30, 0.08, 0.08), (0, y, 3.48),
                "PaintedFrame"),
        ]
    # Insulated ammonia lines, one lagged and one bare.
    for index, (x, radius, mat) in enumerate((
            (-0.36, 0.11, "FoodPlastic"), (-0.08, 0.07, "FoodPlastic"),
            (0.20, 0.05, "SteelBrushed"), (0.44, 0.04, "Copper"))):
        parts.append(tube("Rack_Pipe{:d}".format(index), radius, 6.00,
                          (x, 0, 3.56), mat, rot=(math.radians(90), 0, 0)))
    return parts, [], None


def yard_render_tank():
    """A vertical rendering tank with its agitator drive."""
    parts = [
        cyl("Tank_Shell", 1.10, 3.20, (0, 0, 2.00), "SteelBrushed", verts=24),
        cyl("Tank_Top", 1.14, 0.10, (0, 0, 3.62), "SteelBrushed", verts=24),
        cyl("Tank_Cone", 0.70, 0.60, (0, 0, 0.20), "SteelBrushed", verts=24),
        box("Tank_Motor", (0.50, 0.50, 0.52), (0, 0, 3.95), "PaintedFrame"),
        box("Tank_Gearbox", (0.36, 0.36, 0.30), (0, 0, 3.72), "SteelBrushed"),
        tube("Tank_Outlet", 0.10, 0.60, (0, 0, 0.05), "Stainless"),
        tube("Tank_Steam", 0.05, 3.00, (1.22, 0, 1.90), "Copper"),
        box("Tank_Valve", (0.16, 0.16, 0.22), (1.22, 0, 0.60), "Copper"),
        box("Tank_Ladder", (0.44, 0.05, 3.40), (0, -1.18, 1.80), "SteelBrushed"),
    ]
    for index, z in enumerate((0.55, 1.55, 2.55)):
        for angle in (0.4, 2.5, 4.6):
            parts.append(box("Tank_Leg{:d}_{:.0f}".format(index, angle * 10),
                             (0.10, 0.10, 0.44),
                             (math.cos(angle) * 1.02, math.sin(angle) * 1.02, 0.22),
                             "PaintedFrame") if index == 0 else
                         box("Tank_Band{:d}_{:.0f}".format(index, angle * 10),
                             (0.06, 0.06, 0.06), (0, 0, z), "SteelBrushed"))
    return parts, [], None


# ===========================================================================
# Dispatch and cold store
# ===========================================================================

def yard_dock_leveller():
    """Dock leveller, bumpers and the traffic light that says whether to move."""
    parts = [
        box("Dock_Plate", (2.60, 2.20, 0.10), (0, 0, 0.95), "SteelBrushed",
            rot=(math.radians(4), 0, 0)),
        box("Dock_Pit", (2.80, 2.40, 0.90), (0, 0, 0.45), "Concrete"),
        box("Dock_Lip", (2.60, 0.44, 0.05), (0, -1.28, 1.02), "SteelBrushed"),
        box("Dock_BumperL", (0.26, 0.30, 0.50), (-1.44, -1.30, 0.80), "Rubber"),
        box("Dock_BumperR", (0.26, 0.30, 0.50), (1.44, -1.30, 0.80), "Rubber"),
        box("Dock_Post", (0.12, 0.12, 2.60), (1.70, -1.30, 1.30), "PaintedFrame"),
        box("Dock_LightRed", (0.16, 0.12, 0.16), (1.70, -1.38, 2.42), "LampFail"),
        box("Dock_LightGreen", (0.16, 0.12, 0.16), (1.70, -1.38, 2.22), "LampPass"),
        box("Dock_Control", (0.24, 0.14, 0.34), (1.70, -1.38, 1.40), "SafetyYellow"),
        box("Dock_Guide", (0.10, 2.20, 0.14), (-1.40, 0, 1.04), "SafetyYellow"),
    ]
    return parts, [], None


def yard_trailer():
    """A refrigerated trailer on the dock, fridge unit running at the nose."""
    parts = [
        box("Trl_Body", (2.55, 13.20, 2.90), (0, 0, 2.55), "FoodPlastic"),
        box("Trl_Roof", (2.60, 13.20, 0.08), (0, 0, 4.02), "FoodPlastic"),
        box("Trl_Skirt", (2.50, 13.20, 0.30), (0, 0, 1.00), "PaintedFrame"),
        box("Trl_DoorL", (0.06, 1.20, 2.60), (-0.64, -6.62, 2.50), "SteelBrushed"),
        box("Trl_DoorR", (0.06, 1.20, 2.60), (0.64, -6.62, 2.50), "SteelBrushed"),
        box("Trl_Fridge", (2.20, 0.60, 1.60), (0, 6.90, 3.60), "SteelBrushed"),
        cyl("Trl_FridgeFan", 0.42, 0.12, (0, 7.22, 3.70), "PaintedFrame",
            rot=(math.radians(90), 0, 0)),
        box("Trl_Legs", (2.00, 0.24, 1.00), (0, 4.20, 0.50), "PaintedFrame"),
        box("Trl_Kingpin", (0.60, 0.60, 0.20), (0, 5.60, 0.95), "SteelBrushed"),
    ]
    for index, y in enumerate((-5.4, -4.0)):
        for side, x in (("L", -1.10), ("R", 1.10)):
            parts.append(cyl("Trl_Wheel{:d}{:s}".format(index, side), 0.52, 0.30,
                             (x, y, 0.52), "Rubber", rot=(0, math.radians(90), 0)))
            parts.append(cyl("Trl_Hub{:d}{:s}".format(index, side), 0.20, 0.32,
                             (x, y, 0.52), "SteelBrushed", rot=(0, math.radians(90), 0)))
    return parts, [], None


def yard_pallet_load():
    """A loaded pallet: cartons stacked four to a layer, five layers, wrapped."""
    parts = []
    for index, x in enumerate((-0.5275, -0.185, 0.185, 0.5275, 0.0)):
        parts.append(box("Load_Deck{:d}".format(index), (0.145, 0.80, 0.022),
                         (x, 0, 0.133), "Carton"))
    for bx in (-0.55, 0.0, 0.55):
        for by in (-0.34, 0.0, 0.34):
            parts.append(box("Load_Block_{:.0f}_{:.0f}".format(bx * 100, by * 100),
                             (0.10, 0.10, 0.078), (bx, by, 0.050), "Carton"))

    for layer in range(5):
        z = 0.144 + 0.125 + layer * 0.25
        for cx, cy in ((-0.3, -0.2), (0.3, -0.2), (-0.3, 0.2), (0.3, 0.2)):
            # Alternate layers turn 90 degrees, which is how a stack is keyed
            # together and why a wrapped pallet does not shear apart in transit.
            turn = (layer % 2) * math.pi / 2.0
            parts.append(box("Load_Carton{:d}_{:.0f}_{:.0f}".format(layer, cx * 10, cy * 10),
                             (0.58, 0.38, 0.24),
                             (cx * (1.0 if layer % 2 == 0 else 0.62),
                              cy * (1.0 if layer % 2 == 0 else 1.55), z),
                             "Carton", rot=(0, 0, turn)))

    parts.append(box("Load_Wrap", (1.24, 0.84, 1.22), (0, 0, 0.77), "Perspex"))
    return parts, [], None


def yard_locker_bank():
    """Lockers and a changing bench, for the hygiene barrier."""
    parts = [box("Lock_Plinth", (2.40, 0.50, 0.12), (0, 0, 0.06), "PaintedFrame")]
    for index in range(6):
        x = -1.00 + index * 0.40
        parts += [
            box("Lock_Body{:d}".format(index), (0.38, 0.46, 1.80), (x, 0, 1.02),
                "BluePlastic"),
            box("Lock_Door{:d}".format(index), (0.34, 0.02, 1.72), (x, -0.24, 1.02),
                "BluePlastic"),
            box("Lock_Vent{:d}".format(index), (0.20, 0.01, 0.12), (x, -0.26, 1.74),
                "SteelBrushed"),
            box("Lock_Handle{:d}".format(index), (0.03, 0.03, 0.14), (x + 0.14, -0.27, 1.10),
                "SteelBrushed"),
        ]
    parts += [
        box("Lock_Top", (2.44, 0.52, 0.06), (0, 0, 1.95), "PaintedFrame"),
        box("Bench_Seat", (2.20, 0.34, 0.05), (0, -0.90, 0.45), "FoodPlastic"),
        box("Bench_LegL", (0.06, 0.30, 0.44), (-0.94, -0.90, 0.22), "SteelBrushed"),
        box("Bench_LegR", (0.06, 0.30, 0.44), (0.94, -0.90, 0.22), "SteelBrushed"),
    ]
    return parts, [], None


YARD = {
    "PEN_RAIL": yard_pen_rail,
    "PEN_GATE": yard_pen_gate,
    "WATER_TROUGH": yard_water_trough,
    "CROWD_RACE": yard_crowd_race,
    "UNLOAD_RAMP": yard_unload_ramp,
    "COMPRESSOR_SKID": yard_compressor_skid,
    "CONDENSER": yard_condenser,
    "ELECTRICAL_PANEL": yard_electrical_panel,
    "PIPE_RACK": yard_pipe_rack,
    "RENDER_TANK": yard_render_tank,
    "DOCK_LEVELLER": yard_dock_leveller,
    "TRAILER": yard_trailer,
    "PALLET_LOAD": yard_pallet_load,
    "LOCKER_BANK": yard_locker_bank,
}
