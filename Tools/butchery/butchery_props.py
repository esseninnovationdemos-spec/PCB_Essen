"""
Props for the butchery line.

The things that make a hall look worked-in rather than freshly delivered: what
hangs on the rail, what meat sits in, and what it leaves in. All static, all to
standard sizes where a standard exists -- a Euro pallet is 1200x800x144 mm and
getting that wrong makes every stack built on it wrong too.
"""

import math

from butchery_lib import box, cyl, tube


def prop_gambrel():
    """Gambrel and trolley: the spreader every carcass hangs from."""
    parts = [
        # Trolley: one wheel on the rail, and the shank it hangs by.
        cyl("Gambrel_Wheel", 0.055, 0.03, (0, 0, 0.30), "SteelBrushed",
            rot=(0, math.radians(90), 0)),
        box("Gambrel_Yoke", (0.05, 0.02, 0.16), (0, 0, 0.22), "SteelBrushed"),
        tube("Gambrel_Shank", 0.012, 0.22, (0, 0, 0.05), "SteelBrushed"),

        # Spreader bar and the two hooks.
        box("Gambrel_Bar", (0.46, 0.03, 0.03), (0, 0, -0.07), "SteelBrushed"),
    ]
    for side, x in (("L", -0.21), ("R", 0.21)):
        parts += [
            tube("Gambrel_Hook{:s}A".format(side), 0.010, 0.16, (x, 0, -0.15), "Stainless"),
            tube("Gambrel_Hook{:s}B".format(side), 0.010, 0.10, (x, 0.04, -0.24), "Stainless",
                 rot=(math.radians(70), 0, 0)),
            tube("Gambrel_Hook{:s}C".format(side), 0.010, 0.07, (x, 0.07, -0.30), "Stainless",
                 rot=(math.radians(20), 0, 0)),
        ]
    return parts, [], None


def prop_rail_section():
    """
    Four metres of overhead rail with hangers.

    Tileable end to end: the geometry starts and stops exactly on the 4 m
    boundary so copies butt together with no seam and no overlap.
    """
    parts = [
        box("Rail_Web", (0.02, 4.00, 0.12), (0, 0, 0.06), "SteelBrushed"),
        box("Rail_FlangeTop", (0.09, 4.00, 0.02), (0, 0, 0.13), "SteelBrushed"),
        box("Rail_FlangeBot", (0.09, 4.00, 0.02), (0, 0, -0.01), "SteelBrushed"),
    ]
    for index, y in enumerate((-1.60, 0.0, 1.60)):
        parts += [
            box("Rail_Hanger{:d}".format(index), (0.05, 0.05, 0.50), (0, y, 0.39),
                "PaintedFrame"),
            box("Rail_HangerPlate{:d}".format(index), (0.14, 0.14, 0.02), (0, y, 0.65),
                "PaintedFrame"),
        ]
    return parts, [], None


def prop_carcass_half():
    """
    A split pig half, hanging.

    Deliberately blunt: a slab tapering from ham to shoulder with a fat-side
    face. Anything more detailed than this wants sculpting and a proper texture,
    and a bad detailed carcass looks far worse than a clean simple one.
    """
    parts = [
        # Ham at the top, where it hangs from the gambrel.
        box("Carcass_Ham", (0.30, 0.22, 0.42), (0, 0, 1.32), "Meat"),
        box("Carcass_Loin", (0.34, 0.20, 0.50), (0, 0.01, 0.90), "Meat"),
        box("Carcass_Belly", (0.32, 0.17, 0.42), (0, 0.02, 0.46), "Meat"),
        box("Carcass_Shoulder", (0.28, 0.19, 0.34), (0, 0.01, 0.12), "Meat"),
        # Fat side: the cut face is pale, and it is what faces the aisle.
        box("Carcass_CutFace", (0.02, 0.20, 1.24), (-0.16, 0.01, 0.86), "Fat"),
        box("Carcass_BackFat", (0.30, 0.03, 1.20), (0, -0.10, 0.88), "Fat"),
        # Hock, and the slit it hangs by.
        box("Carcass_Hock", (0.10, 0.10, 0.26), (0.02, 0, 1.62), "Meat"),
    ]
    return parts, [], None


def prop_meat_bin():
    """Standard 200 litre combo bin on castors -- what trim moves in."""
    parts = [
        box("Bin_Base", (1.00, 0.80, 0.06), (0, 0, 0.28), "BluePlastic"),
        box("Bin_WallA", (1.00, 0.05, 0.52), (0, -0.38, 0.56), "BluePlastic"),
        box("Bin_WallB", (1.00, 0.05, 0.52), (0, 0.38, 0.56), "BluePlastic"),
        box("Bin_WallC", (0.05, 0.80, 0.52), (-0.48, 0, 0.56), "BluePlastic"),
        box("Bin_WallD", (0.05, 0.80, 0.52), (0.48, 0, 0.56), "BluePlastic"),
        box("Bin_Rim", (1.06, 0.86, 0.04), (0, 0, 0.84), "BluePlastic"),
    ]
    for index, (dx, dy) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
        parts.append(cyl("Bin_Castor{:d}".format(index), 0.06, 0.04,
                         (dx * 0.40, dy * 0.30, 0.06), "Rubber",
                         rot=(0, math.radians(90), 0)))
        parts.append(box("Bin_CastorFork{:d}".format(index), (0.08, 0.06, 0.12),
                         (dx * 0.40, dy * 0.30, 0.18), "SteelBrushed"))
    return parts, [], None


def prop_crate():
    """E2 meat crate, 600 x 400 x 200 mm. Stackable, and the size everything else assumes."""
    parts = [
        box("Crate_Base", (0.60, 0.40, 0.02), (0, 0, 0.01), "FoodPlastic"),
        box("Crate_WallA", (0.60, 0.02, 0.19), (0, -0.19, 0.105), "FoodPlastic"),
        box("Crate_WallB", (0.60, 0.02, 0.19), (0, 0.19, 0.105), "FoodPlastic"),
        box("Crate_WallC", (0.02, 0.40, 0.19), (-0.29, 0, 0.105), "FoodPlastic"),
        box("Crate_WallD", (0.02, 0.40, 0.19), (0.29, 0, 0.105), "FoodPlastic"),
        # 600 x 400 external, rim included -- that is the whole point of E2.
        box("Crate_Rim", (0.60, 0.40, 0.015), (0, 0, 0.20), "FoodPlastic"),
        box("Crate_GripA", (0.16, 0.03, 0.05), (0, -0.19, 0.17), "FoodPlastic"),
        box("Crate_GripB", (0.16, 0.03, 0.05), (0, 0.19, 0.17), "FoodPlastic"),
    ]
    return parts, [], None


def prop_pallet():
    """Euro pallet, 1200 x 800 x 144 mm: nine blocks, three bearers, five top boards."""
    parts = []
    # Outer boards sit inboard of their own half-width so the deck ends exactly
    # on 1200 mm. At x = +/-0.55 the pallet measured 1245 mm, which is wrong in
    # the one dimension every stack built on it inherits.
    for index, x in enumerate((-0.5275, -0.185, 0.185, 0.5275, 0.0)):
        parts.append(box("Pallet_Top{:d}".format(index), (0.145, 0.80, 0.022),
                         (x, 0, 0.133), "Carton"))
    for index, x in enumerate((-0.55, 0.0, 0.55)):
        parts.append(box("Pallet_Bearer{:d}".format(index), (0.10, 0.80, 0.022),
                         (x, 0, 0.100), "Carton"))
    for bx in (-0.55, 0.0, 0.55):
        for by in (-0.34, 0.0, 0.34):
            parts.append(box("Pallet_Block_{:.0f}_{:.0f}".format(bx * 100, by * 100),
                             (0.10, 0.10, 0.078), (bx, by, 0.050), "Carton"))
    for index, x in enumerate((-0.55, 0.0, 0.55)):
        parts.append(box("Pallet_Bottom{:d}".format(index), (0.10, 0.80, 0.022),
                         (x, 0, 0.011), "Carton"))
    return parts, [], None


def prop_carton():
    """A packed and taped carton, sized to stack four to a pallet layer."""
    parts = [
        box("Carton_Body", (0.60, 0.40, 0.25), (0, 0, 0.125), "Carton"),
        box("Carton_TapeTop", (0.06, 0.40, 0.005), (0, 0, 0.252), "FoodPlastic"),
        box("Carton_Label", (0.16, 0.002, 0.11), (0.12, -0.201, 0.15), "FoodPlastic"),
    ]
    return parts, [], None


def prop_handwash():
    """
    Knee-operated hand basin.

    Knee-operated because hands that have just touched a carcass do not touch a
    tap, and that one detail is what makes a modelled meat plant read as real to
    anyone who has worked in one.
    """
    parts = [
        box("Wash_Basin", (0.46, 0.38, 0.18), (0, 0, 1.00), "Stainless"),
        box("Wash_BasinFloor", (0.42, 0.34, 0.02), (0, 0, 0.92), "Stainless"),
        box("Wash_Splash", (0.46, 0.03, 0.30), (0, 0.18, 1.24), "Stainless"),
        tube("Wash_Spout", 0.014, 0.22, (0, 0.12, 1.22), "Stainless"),
        tube("Wash_SpoutArm", 0.014, 0.14, (0, 0.05, 1.32), "Stainless",
             rot=(math.radians(90), 0, 0)),
        box("Wash_Pedestal", (0.16, 0.16, 0.90), (0, 0.10, 0.45), "Stainless"),
        box("Wash_KneePlate", (0.30, 0.06, 0.14), (0, -0.10, 0.62), "SteelBrushed"),
        box("Wash_KneeArm", (0.05, 0.16, 0.05), (0, 0.02, 0.62), "SteelBrushed"),
        box("Wash_SoapBox", (0.11, 0.09, 0.22), (0.26, 0.16, 1.28), "FoodPlastic"),
        box("Wash_Foot", (0.40, 0.34, 0.03), (0, 0.10, 0.015), "SteelBrushed"),
    ]
    return parts, [], None


PROPS = {
    "PROP_GAMBREL": prop_gambrel,
    "PROP_RAIL_SECTION": prop_rail_section,
    "PROP_CARCASS_HALF": prop_carcass_half,
    "PROP_MEAT_BIN": prop_meat_bin,
    "PROP_CRATE": prop_crate,
    "PROP_PALLET": prop_pallet,
    "PROP_CARTON": prop_carton,
    "PROP_HANDWASH": prop_handwash,
}
