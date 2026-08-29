"""
The plant: chambers, the lines inside them, and the routes between.

One source of truth. The top-view map is drawn from this, and the Unreal build
will place from the same numbers, so a chamber cannot be one size on the plan
and another in the level.

Layout follows the rule that governs every real slaughterhouse: **product moves
one way and never doubles back, and the dirty side never touches the clean
side.** Everything here falls out of that. Lairage, kill floor and the scalding
run are dirty; the line crosses to clean at evisceration, which is why
inspection sits there and why the byproduct room hangs off it rather than off
the cutting hall. Cold store and dispatch are the far end because product that
has been chilled must not pass back through anything warm.

Coordinates are metres, origin at the building's south-west corner, X east and
Y north. That matches FactoryGrid in the Unreal side, so a chamber at (66, 32)
is at (6600, 3200) in world centimetres.
"""

BUILDING = {"width": 130.0, "depth": 88.0, "eaves": 8.0, "rail_height": 3.1}

# Hygiene zone drives wall spec, footwear, and who may walk where.
DIRTY, CLEAN, CHILLED, SUPPORT = "dirty", "clean", "chilled", "support"

# name: (x, y, w, h, zone, temperature C or None, label)
CHAMBERS = [
    # --- band 1: the kill floor, west to east ---------------------------
    ("LAIRAGE",        2.0,  2.0, 26.0, 26.0, DIRTY,   None, "Lairage & race"),
    ("KILL_FLOOR",    30.0,  2.0, 34.0, 26.0, DIRTY,   None, "Stun · bleed · scald · dehair · singe"),
    ("EVISCERATION",  66.0,  2.0, 30.0, 26.0, CLEAN,   12.0, "Eviscerate · split · inspect"),
    ("BLAST_CHILL",   98.0,  2.0, 30.0, 26.0, CHILLED, -8.0, "Blast chill tunnels"),

    # --- band 2: support, and the chill that feeds the cut hall ---------
    ("HYGIENE",        2.0, 32.0, 20.0, 26.0, SUPPORT, None, "Welfare · boot wash · laundry"),
    ("PLANT_ROOM",    24.0, 32.0, 18.0, 26.0, SUPPORT, None, "Refrigeration · compressors"),
    ("RENDERING",     44.0, 32.0, 20.0, 26.0, DIRTY,   None, "Inedible rendering"),
    ("BYPRODUCT",     66.0, 32.0, 26.0, 26.0, CLEAN,   10.0, "Red & green offal"),
    ("CARCASS_CHILL", 96.0, 32.0, 32.0, 26.0, CHILLED,  2.0, "Equalisation chill"),

    # --- band 3: cutting through to dispatch, east to west --------------
    ("DISPATCH",       2.0, 62.0, 24.0, 24.0, CHILLED,  4.0, "Dock & load-out"),
    ("COLD_STORE",    28.0, 62.0, 30.0, 24.0, CHILLED, -2.0, "Palletised cold store"),
    ("PACKING",       60.0, 62.0, 30.0, 24.0, CHILLED,  6.0, "Vacuum · detect · carton"),
    ("CUTTING_HALL",  92.0, 62.0, 36.0, 24.0, CHILLED, 10.0, "Deboning & trimming"),
]

# Lines inside chambers. `axis` is the direction product travels.
# (chamber, count, axis, kind, label)
LINES = [
    ("KILL_FLOOR",    2, "x", "rail",  "Kill line"),
    ("EVISCERATION",  2, "x", "rail",  "Dressing line"),
    ("BLAST_CHILL",   4, "x", "rail",  "Chill tunnel"),
    ("CARCASS_CHILL", 6, "x", "rail",  "Chill rail"),
    ("BYPRODUCT",     2, "x", "belt",  "Offal line"),
    ("CUTTING_HALL",  4, "y", "belt",  "Deboning line"),
    ("PACKING",       3, "y", "belt",  "Packing line"),
]

# The overhead rail, in absolute metres. Carcasses ride this from the bleed rail
# to the cutting-hall drop-off, and it is the single longest object in the
# building.
#
# Orthogonal, not point-to-point. Rail is steel section hung from the structure:
# it runs along a chamber and turns at a bend, and it crosses between chambers
# through an opening in the wall. A diagonal between two chamber centres is a
# line through a wall, which is neither buildable nor how any plant is laid out.
RAIL_ROUTE = [
    (34.0, 20.0),    # bleed rail, kill floor
    (94.0, 20.0),    # east the length of kill floor and evisceration
    (94.0, 12.0),    # drop to the dressing line
    (124.0, 12.0),   # east into blast chill
    (124.0, 44.0),   # north through the chill wall into carcass chill
    (100.0, 44.0),   # west along the equalisation rail
    (100.0, 74.0),   # north into the cutting hall
    (96.0, 74.0),    # drop-off at the head of the deboning lines
]

# Where product leaves the rail. Each is a short run across a shared wall, at the
# opening -- not a line between chamber centres.
# (label, x1, y1, x2, y2)
TRANSFERS = [
    ("race",   28.0, 15.0,  30.0, 15.0),   # lairage into the kill floor
    ("chute",  80.0, 26.0,  80.0, 34.0),   # dressing floor down to byproduct
    ("screw",  66.0, 45.0,  64.0, 45.0),   # byproduct to rendering, enclosed
    ("belt",   92.0, 74.0,  90.0, 74.0),   # cutting hall to packing
    ("roller", 60.0, 74.0,  58.0, 74.0),   # packing to cold store
    ("roller", 28.0, 74.0,  26.0, 74.0),   # cold store to dispatch
]

# Which station model goes where, and how many. Drives the shopping list below.
# (chamber, asset, count)
PLACEMENTS = [
    ("KILL_FLOOR",    "CO2_STUNNER",         1),
    ("KILL_FLOOR",    "BLEED_RAIL",          2),
    ("KILL_FLOOR",    "SCALD_TANK",          2),
    ("KILL_FLOOR",    "DEHAIRER",            2),
    ("KILL_FLOOR",    "SINGE_CABINET",       2),
    ("KILL_FLOOR",    "POLISHER",            2),
    ("EVISCERATION",  "EVISCERATION_TABLE",  2),
    ("EVISCERATION",  "SPLITTING_SAW",       2),
    ("EVISCERATION",  "CARCASS_INSPECTION",  2),
    ("EVISCERATION",  "OFFAL_CHUTE",         2),
    ("BLAST_CHILL",   "BLAST_CHILLER",       4),
    ("BYPRODUCT",     "BYPRODUCT_TABLE",     4),
    ("BYPRODUCT",     "GRINDER",             2),
    ("RENDERING",     "RENDERING_HOPPER",    2),
    ("CUTTING_HALL",  "DEBONING_LINE",       4),
    ("CUTTING_HALL",  "DEBONE_STATION",     16),
    ("CUTTING_HALL",  "TRIM_BENCH",          8),
    ("PACKING",       "VACUUM_PACKER",       3),
    ("PACKING",       "METAL_DETECTOR",      3),
    ("PACKING",       "PALLETISER",          2),
    ("COLD_STORE",    "PALLET_RACK",        12),
    ("HYGIENE",       "BOOT_WASH",           4),
]

# Assets the plan needs that do not exist yet.
NEEDED = [
    ("RAIL_RUN",         "6 m of powered overhead rail with trolleys, tileable"),
    ("RAIL_CARCASS_RUN", "the same run carrying four hanging carcasses"),
    ("RAIL_CURVE",       "90 degree rail bend, for the corners of the route"),
    ("RAIL_SWITCH",      "rail points, where the line splits between chambers"),
    ("DEBONE_STATION",   "one deboning workstation: bench, chute, bin, sterilizer"),
    ("BYPRODUCT_TABLE",  "offal separation table with red and green sides"),
    ("OFFAL_CHUTE",      "drop chute from the dressing floor to byproduct"),
    ("RENDERING_HOPPER", "inedible collection hopper with a screw take-off"),
    ("BELT_CONVEYOR",    "6 m straight belt, tileable"),
    ("ROLLER_CONVEYOR",  "3 m gravity roller for cartons and crates"),
    ("SCREW_CONVEYOR",   "trim and byproduct auger"),
    ("ACCUMULATION_TABLE", "rotary accumulation table between lines"),
    ("WALL_PANEL",       "3 m hygienic sandwich panel, tileable"),
    ("CHAMBER_DOOR",     "sliding cold-room door"),
    ("STRIP_CURTAIN",    "PVC strip curtain for a chamber opening"),
    ("BOOT_WASH",        "hygiene entry: boot wash and hand basin"),
    ("PALLET_RACK",      "3-bay pallet racking for the cold store"),
]


def chamber(name):
    for row in CHAMBERS:
        if row[0] == name:
            return row
    raise KeyError(name)


def rect(name):
    _n, x, y, w, h, _z, _t, _l = chamber(name)
    return x, y, w, h


def point(name, fx, fy):
    """A point inside a chamber, given as a fraction of its extent."""
    x, y, w, h = rect(name)
    return x + w * fx, y + h * fy


def line_positions(chamber_name, count, axis):
    """Evenly spaced line centres inside a chamber, inset from the walls."""
    x, y, w, h = rect(chamber_name)
    inset = 0.14
    out = []
    for index in range(count):
        t = (index + 0.5) / count
        t = inset + t * (1.0 - 2.0 * inset)
        if axis == "x":
            out.append(((x, y + h * t), (x + w, y + h * t)))
        else:
            out.append(((x + w * t, y), (x + w * t, y + h)))
    return out


def shopping_list():
    """Every asset the plan references, with how many instances it needs."""
    totals = {}
    for _chamber, asset, count in PLACEMENTS:
        totals[asset] = totals.get(asset, 0) + count
    return dict(sorted(totals.items()))
