"""
The butchery line, station by station, in process order.

Dimensions are real. A scald tank is 4.2 m long and a chill tunnel is 6 m
because that is roughly what they are, and a line laid out from wrong-sized
machines cannot be fixed later without moving everything.

Each builder returns (parts, bones, animate):
  parts    -- primitives; each carries a `part` naming the bone it skins to
  bones    -- [] for a static mesh, so no skeleton is paid for on a bench
  animate  -- called with the rig in pose mode, or None

Bones point along their own axis of motion; see butchery_lib.
"""

import math

from butchery_lib import box, cyl, frame, key_spin, key_stroke, rail_section, tube

LOOP = 60          # frames per loop, 2 s at 30 fps
RAIL_Z = 3.10      # overhead rail height, the datum the whole kill floor hangs from


# ===========================================================================
# 1. Stunning
# ===========================================================================

def station_co2_stunner():
    """
    CO2 stunning chamber: pigs ride a gondola down into a CO2 well.

    Modelled as the well head rather than the pit, because the pit is below
    floor and the only part anyone sees is the enclosure, the race and the
    gondola rising back into view.
    """
    parts = [
        box("Stunner_WellRim", (3.60, 3.60, 0.25), (0, 0, 0.12), "PaintedFrame"),
        box("Stunner_WallN", (3.60, 0.12, 2.60), (0, 1.74, 1.55), "Stainless"),
        box("Stunner_WallS", (3.60, 0.12, 2.60), (0, -1.74, 1.55), "Stainless"),
        box("Stunner_WallW", (0.12, 3.60, 2.60), (-1.74, 0, 1.55), "Stainless"),
        box("Stunner_Roof", (3.60, 3.60, 0.10), (0, 0, 2.90), "SteelBrushed"),

        # Entry race: crowding pen into the gondola.
        box("Stunner_RaceL", (0.10, 2.40, 1.10), (0.60, -2.90, 0.80), "Stainless"),
        box("Stunner_RaceR", (0.10, 2.40, 1.10), (1.50, -2.90, 0.80), "Stainless"),
        box("Stunner_RaceFloor", (1.00, 2.40, 0.06), (1.05, -2.90, 0.28), "SteelBrushed"),

        # Gas plant and the CO2 monitor that has to be there by law.
        cyl("Stunner_GasTank", 0.36, 2.20, (-2.35, 1.20, 1.10), "SteelBrushed"),
        cyl("Stunner_GasTank2", 0.36, 2.20, (-2.35, 0.35, 1.10), "SteelBrushed"),
        tube("Stunner_GasLine", 0.05, 2.00, (-2.35, 0.78, 2.30), "Stainless",
             rot=(math.radians(90), 0, 0)),
        box("Stunner_Monitor", (0.26, 0.10, 0.34), (-1.80, -1.60, 1.90), "SafetyYellow"),
        box("Stunner_Panel", (0.50, 0.12, 0.62), (-0.60, -1.82, 1.50), "PaintedFrame"),
        box("Stunner_PanelFace", (0.42, 0.02, 0.52), (-0.60, -1.89, 1.50), "Perspex"),

        # Discharge rail: stunned pigs leave hanging.
    ] + rail_section("Stunner_Rail", 3.00, (0, 2.60, 0), RAIL_Z)

    # --- gondola ---------------------------------------------------------
    lift = "gondola"
    parts += [
        box("Stunner_GondolaFloor", (1.20, 1.60, 0.08), (0, 0, 2.10), "SteelBrushed", part=lift),
        box("Stunner_GondolaBackW", (1.20, 0.06, 1.20), (0, 0.77, 2.70), "Stainless", part=lift),
        box("Stunner_GondolaSideL", (0.06, 1.60, 1.20), (-0.57, 0, 2.70), "Stainless", part=lift),
        box("Stunner_GondolaSideR", (0.06, 1.60, 1.20), (0.57, 0, 2.70), "Stainless", part=lift),
        tube("Stunner_GondolaRod", 0.05, 1.40, (0, 0, 3.60), "SteelBrushed", part=lift),
    ]
    bones = [{"name": lift, "head": (0.0, 0.0, 2.10), "axis": (0.0, 0.0, -1.0), "length": 0.8}]

    def animate(rig):
        # Down into the well, hold while the gas does its work, back up.
        key_stroke(rig, lift, distance=1.95, frames=LOOP, dwell=0.45)

    return parts, bones, animate


# ===========================================================================
# 2. Bleeding
# ===========================================================================

def station_bleed_rail():
    """
    Bleed rail: a run of overhead rail over a collection trough.

    Nothing here moves under power -- the carcasses travel and the blood drains.
    The trough is the whole design constraint: it has to catch everything and be
    hosable, which is why it is a wide shallow channel with a splash wall.
    """
    parts = rail_section("Bleed_Rail", 8.00, (0, 0, 0), RAIL_Z)
    parts += [
        # Trough: 300 mm deep, falling to one end.
        box("Bleed_TroughFloor", (1.40, 8.00, 0.06), (0, 0, 0.06), "Stainless"),
        box("Bleed_TroughWallL", (0.06, 8.00, 0.34), (-0.70, 0, 0.20), "Stainless"),
        box("Bleed_TroughWallR", (0.06, 8.00, 0.34), (0.70, 0, 0.20), "Stainless"),
        box("Bleed_TroughEnd", (1.40, 0.06, 0.34), (0, 4.00, 0.20), "Stainless"),

        # Splash walls either side, floor to 2 m, hosed down every shift.
        box("Bleed_SplashL", (0.08, 8.00, 2.00), (-1.60, 0, 1.00), "FoodPlastic"),
        box("Bleed_SplashR", (0.08, 8.00, 2.00), (1.60, 0, 1.00), "FoodPlastic"),

        # Blood pump and line to the collection tank.
        cyl("Bleed_Pump", 0.24, 0.50, (0.95, -4.20, 0.30), "SteelBrushed"),
        tube("Bleed_Line", 0.07, 3.20, (1.25, -4.20, 1.20), "Stainless"),
        box("Bleed_PumpBase", (0.70, 0.60, 0.12), (0.95, -4.20, 0.06), "PaintedFrame"),

        # Drip pans under the rail between the trough and the walls.
        box("Bleed_DripL", (0.40, 8.00, 0.04), (-1.10, 0, 0.34), "Stainless"),
        box("Bleed_DripR", (0.40, 8.00, 0.04), (1.10, 0, 0.34), "Stainless"),
    ]
    return parts, [], None


# ===========================================================================
# 3. Scalding
# ===========================================================================

def station_scald_tank():
    """
    Scald tank: 60 C water, with a paddle wheel moving carcasses along it.

    The paddle is the moving part and the reason the tank reads as a machine
    rather than a bath.
    """
    parts = [
        box("Scald_TankFloor", (1.80, 4.20, 0.10), (0, 0, 0.35), "Stainless"),
        box("Scald_WallL", (0.10, 4.20, 1.20), (-0.95, 0, 0.95), "Stainless"),
        box("Scald_WallR", (0.10, 4.20, 1.20), (0.95, 0, 0.95), "Stainless"),
        box("Scald_WallIn", (1.80, 0.10, 1.20), (0, -2.15, 0.95), "Stainless"),
        box("Scald_WallOut", (1.80, 0.10, 1.20), (0, 2.15, 0.95), "Stainless"),

        # Water surface, sitting just below the rim.
        box("Scald_Water", (1.70, 4.10, 0.02), (0, 0, 1.38), "Perspex"),

        # Frame, steam manifold and the thermostat pocket.
        box("Scald_LegFL", (0.10, 0.10, 0.35), (-0.80, -1.95, 0.17), "PaintedFrame"),
        box("Scald_LegFR", (0.10, 0.10, 0.35), (0.80, -1.95, 0.17), "PaintedFrame"),
        box("Scald_LegBL", (0.10, 0.10, 0.35), (-0.80, 1.95, 0.17), "PaintedFrame"),
        box("Scald_LegBR", (0.10, 0.10, 0.35), (0.80, 1.95, 0.17), "PaintedFrame"),
        tube("Scald_SteamPipe", 0.06, 4.00, (-1.10, 0, 0.60), "Copper",
             rot=(math.radians(90), 0, 0)),
        box("Scald_SteamValve", (0.18, 0.18, 0.26), (-1.10, -2.10, 0.85), "Copper"),
        box("Scald_Thermo", (0.16, 0.10, 0.22), (0.95, -1.60, 1.70), "PaintedFrame"),
        box("Scald_Hood", (2.00, 4.20, 0.08), (0, 0, 2.55), "SteelBrushed"),
        box("Scald_HoodDuctL", (0.30, 0.30, 1.00), (-0.60, 1.60, 2.05), "SteelBrushed"),
    ]

    # --- paddle wheel ----------------------------------------------------
    spin = "paddle"
    parts.append(cyl("Scald_PaddleShaft", 0.08, 1.70, (0, 0, 1.55), "SteelBrushed",
                     rot=(0, math.radians(90), 0), part=spin))
    for blade in range(6):
        angle = blade * math.pi / 3.0
        radius = 0.42
        parts.append(box(
            "Scald_Paddle{:d}".format(blade), (1.60, 0.06, 0.34),
            (0, math.sin(angle) * radius, 1.55 + math.cos(angle) * radius),
            "Stainless", rot=(angle, 0, 0), part=spin))

    bones = [{"name": spin, "head": (-0.85, 0.0, 1.55), "axis": (1.0, 0.0, 0.0), "length": 1.70}]

    def animate(rig):
        key_spin(rig, spin, turns=2, frames=LOOP)

    return parts, bones, animate


# ===========================================================================
# 4. Dehairing
# ===========================================================================

def station_dehairer():
    """Dehairing machine: a beater shaft with rubber paddles inside a drum."""
    parts = [
        box("Dehairer_Roof", (2.20, 3.00, 0.08), (0, 0, 2.36), "Stainless"),
        box("Dehairer_SideL", (0.08, 3.00, 1.90), (-1.06, 0, 1.37), "Stainless"),
        box("Dehairer_SideR", (0.08, 3.00, 1.90), (1.06, 0, 1.37), "Stainless"),
        box("Dehairer_Floor", (2.20, 3.00, 0.10), (0, 0, 0.47), "SteelBrushed"),
        box("Dehairer_WindowFrame", (0.06, 1.20, 0.80), (-1.06, 0, 1.60), "PaintedFrame"),
        box("Dehairer_WindowGlass", (0.02, 1.10, 0.70), (-1.08, 0, 1.60), "Perspex"),
        box("Dehairer_LegFL", (0.10, 0.10, 0.42), (-0.95, -1.35, 0.21), "PaintedFrame"),
        box("Dehairer_LegFR", (0.10, 0.10, 0.42), (0.95, -1.35, 0.21), "PaintedFrame"),
        box("Dehairer_LegBL", (0.10, 0.10, 0.42), (-0.95, 1.35, 0.21), "PaintedFrame"),
        box("Dehairer_LegBR", (0.10, 0.10, 0.42), (0.95, 1.35, 0.21), "PaintedFrame"),
        cyl("Dehairer_Drain", 0.09, 0.40, (0, -1.20, 0.25), "Stainless",
            rot=(math.radians(90), 0, 0)),
        box("Dehairer_Motor", (0.34, 0.46, 0.34), (0, 1.75, 1.20), "PaintedFrame"),
        cyl("Dehairer_MotorFan", 0.16, 0.10, (0, 2.03, 1.20), "SteelBrushed",
            rot=(math.radians(90), 0, 0)),
        box("Dehairer_Guard", (0.30, 0.16, 0.60), (0, 1.48, 1.20), "SafetyYellow"),
        box("Dehairer_Panel", (0.40, 0.10, 0.50), (0.80, -1.55, 1.45), "PaintedFrame"),
        box("Dehairer_PanelFace", (0.34, 0.02, 0.42), (0.80, -1.61, 1.45), "Perspex"),
    ]

    spin = "beater"
    parts.append(cyl("Dehairer_Shaft", 0.09, 2.70, (0, 0, 1.25), "SteelBrushed",
                     rot=(math.radians(90), 0, 0), part=spin))
    for row in range(6):
        y = -1.10 + row * 0.44
        for blade in range(3):
            angle = math.radians(blade * 120 + row * 20)
            radius = 0.38
            parts.append(box(
                "Dehairer_Paddle{:d}_{:d}".format(row, blade), (0.05, 0.30, 0.62),
                (math.sin(angle) * radius, y, 1.25 + math.cos(angle) * radius),
                "Rubber", rot=(0, -angle, 0), part=spin))

    bones = [{"name": spin, "head": (0.0, -1.35, 1.25), "axis": (0.0, 1.0, 0.0), "length": 2.70}]

    def animate(rig):
        key_spin(rig, spin, turns=4, frames=LOOP)

    return parts, bones, animate


# ===========================================================================
# 5. Singeing
# ===========================================================================

def station_singe_cabinet():
    """
    Singeing furnace: a gas flame cabinet the carcass passes through.

    Static. The flames belong in a particle or material effect in engine, not
    in geometry -- modelled fire is the fastest way to make a scene look cheap.
    """
    parts = [
        box("Singe_ShellL", (0.14, 1.80, 3.00), (-0.90, 0, 1.50), "SteelBrushed"),
        box("Singe_ShellR", (0.14, 1.80, 3.00), (0.90, 0, 1.50), "SteelBrushed"),
        box("Singe_ShellBack", (1.80, 0.14, 3.00), (0, 0.86, 1.50), "SteelBrushed"),
        box("Singe_Roof", (1.90, 1.90, 0.12), (0, 0, 3.06), "SteelBrushed"),
        box("Singe_Base", (1.90, 1.90, 0.20), (0, 0, 0.10), "PaintedFrame"),

        # Burner ports down both inner walls.
    ]
    for index in range(5):
        z = 0.70 + index * 0.45
        parts += [
            box("Singe_BurnerL{:d}".format(index), (0.10, 1.40, 0.10),
                (-0.76, 0, z), "Copper"),
            box("Singe_BurnerR{:d}".format(index), (0.10, 1.40, 0.10),
                (0.76, 0, z), "Copper"),
        ]

    parts += [
        # Gas train and extraction.
        tube("Singe_GasMain", 0.06, 2.60, (-1.05, 0, 1.60), "Copper"),
        box("Singe_GasValve", (0.20, 0.20, 0.26), (-1.05, 0, 0.55), "Copper"),
        box("Singe_Hood", (2.10, 2.10, 0.10), (0, 0, 3.40), "SteelBrushed"),
        cyl("Singe_Duct", 0.34, 1.20, (0, 0.55, 4.00), "SteelBrushed"),
        box("Singe_Panel", (0.44, 0.12, 0.56), (1.05, -0.70, 1.60), "PaintedFrame"),
        box("Singe_PanelFace", (0.36, 0.02, 0.46), (1.05, -0.77, 1.60), "Perspex"),
        box("Singe_Warn", (0.30, 0.02, 0.22), (1.05, -0.77, 2.10), "SafetyYellow"),
    ]
    return parts, [], None


# ===========================================================================
# 6. Polishing
# ===========================================================================

def station_polisher():
    """Polisher: counter-rotating brush columns that finish the skin."""
    parts = [
        box("Polish_Roof", (2.00, 2.20, 0.08), (0, 0, 2.40), "Stainless"),
        box("Polish_SideL", (0.08, 2.20, 2.00), (-0.96, 0, 1.40), "Stainless"),
        box("Polish_SideR", (0.08, 2.20, 2.00), (0.96, 0, 1.40), "Stainless"),
        box("Polish_Floor", (2.00, 2.20, 0.12), (0, 0, 0.40), "SteelBrushed"),
        box("Polish_LegFL", (0.09, 0.09, 0.34), (-0.85, -0.95, 0.17), "PaintedFrame"),
        box("Polish_LegFR", (0.09, 0.09, 0.34), (0.85, -0.95, 0.17), "PaintedFrame"),
        box("Polish_LegBL", (0.09, 0.09, 0.34), (-0.85, 0.95, 0.17), "PaintedFrame"),
        box("Polish_LegBR", (0.09, 0.09, 0.34), (0.85, 0.95, 0.17), "PaintedFrame"),
        box("Polish_Motor", (0.30, 0.30, 0.40), (0, 1.25, 2.60), "PaintedFrame"),
        tube("Polish_Water", 0.04, 2.00, (0, 0, 2.55), "Stainless",
             rot=(math.radians(90), 0, 0)),
        box("Polish_Panel", (0.36, 0.10, 0.46), (0.96, -0.80, 1.50), "PaintedFrame"),
    ]

    bones = []
    for side, x in (("L", -0.42), ("R", 0.42)):
        part = "brush{:s}".format(side)
        parts.append(cyl("Polish_Shaft{:s}".format(side), 0.07, 1.70,
                         (x, 0, 1.35), "SteelBrushed", part=part))
        for row in range(7):
            z = 0.62 + row * 0.22
            for blade in range(6):
                angle = blade * math.pi / 3.0 + row * 0.25
                radius = 0.30
                parts.append(box(
                    "Polish_Bristle{:s}{:d}_{:d}".format(side, row, blade),
                    (0.26, 0.04, 0.18),
                    (x + math.cos(angle) * radius, math.sin(angle) * radius, z),
                    "FoodPlastic", rot=(0, 0, angle), part=part))
        bones.append({"name": part, "head": (x, 0.0, 0.50),
                      "axis": (0.0, 0.0, 1.0), "length": 1.70})

    def animate(rig):
        # Counter-rotating: same speed, opposite sign, which is what a polisher
        # does and what stops the carcass being pushed out sideways.
        key_spin(rig, "brushL", turns=3, frames=LOOP)
        key_spin(rig, "brushR", turns=-3, frames=LOOP)

    return parts, bones, animate


# ===========================================================================
# 7. Evisceration
# ===========================================================================

def station_evisceration_table():
    """
    Viscera table: a rotating carousel of pans, synchronised with the rail.

    Each pan carries one carcass's viscera past the inspector, so pluck and
    carcass stay together -- lose that pairing and the whole inspection is void.
    """
    parts = [
        cyl("Viscera_Column", 0.28, 1.00, (0, 0, 0.50), "SteelBrushed"),
        cyl("Viscera_Base", 0.70, 0.14, (0, 0, 0.07), "PaintedFrame"),
        box("Viscera_Motor", (0.36, 0.36, 0.36), (0.62, 0, 0.30), "PaintedFrame"),

        # Operator platform alongside.
        box("Viscera_Platform", (1.60, 3.00, 0.10), (2.30, 0, 0.45), "SteelBrushed"),
        box("Viscera_PlatRailA", (0.05, 3.00, 0.05), (3.05, 0, 1.55), "Stainless"),
        box("Viscera_PlatPostA", (0.06, 0.06, 1.05), (3.05, -1.40, 1.03), "Stainless"),
        box("Viscera_PlatPostB", (0.06, 0.06, 1.05), (3.05, 1.40, 1.03), "Stainless"),
        box("Viscera_Step", (0.60, 1.00, 0.06), (3.35, 0, 0.24), "SteelBrushed"),

        # Wash trough and the inspector's light.
        box("Viscera_Wash", (0.40, 2.20, 0.24), (1.55, 0, 1.10), "Stainless"),
        box("Viscera_LightBar", (0.14, 2.40, 0.10), (1.20, 0, 2.35), "SteelBrushed"),
        box("Viscera_LightLens", (0.10, 2.30, 0.03), (1.20, 0, 2.29), "Perspex"),
    ] + rail_section("Viscera_Rail", 4.00, (-0.30, 0, 0), RAIL_Z)

    # --- carousel --------------------------------------------------------
    spin = "carousel"
    parts.append(cyl("Viscera_Deck", 1.55, 0.08, (0, 0, 1.05), "Stainless", part=spin, verts=32))
    parts.append(cyl("Viscera_DeckLip", 1.60, 0.10, (0, 0, 1.14), "Stainless", part=spin, verts=32))
    for index in range(8):
        angle = index * math.pi / 4.0
        radius = 1.05
        parts.append(box(
            "Viscera_Pan{:d}".format(index), (0.62, 0.52, 0.16),
            (math.cos(angle) * radius, math.sin(angle) * radius, 1.17),
            "FoodPlastic", rot=(0, 0, angle), part=spin))

    bones = [{"name": spin, "head": (0.0, 0.0, 1.00), "axis": (0.0, 0.0, 1.0), "length": 0.60}]

    def animate(rig):
        # One slow turn: the carousel indexes at line speed, not machine speed.
        key_spin(rig, spin, turns=1, frames=LOOP)

    return parts, bones, animate


# ===========================================================================
# 8. Splitting
# ===========================================================================

def station_splitting_saw():
    """
    Carcass splitting saw on a balancer.

    Two moving parts, and they mean different things: the blade runs constantly,
    the arm strokes once per carcass. Animating only one of them looks wrong in
    a way people notice without being able to say why.
    """
    parts = [
        # Gantry.
        box("Split_PostL", (0.16, 0.16, 3.40), (-1.30, 0, 1.70), "PaintedFrame"),
        box("Split_PostR", (0.16, 0.16, 3.40), (1.30, 0, 1.70), "PaintedFrame"),
        box("Split_Beam", (2.90, 0.20, 0.22), (0, 0, 3.50), "PaintedFrame"),
        box("Split_BaseL", (0.50, 0.50, 0.10), (-1.30, 0, 0.05), "PaintedFrame"),
        box("Split_BaseR", (0.50, 0.50, 0.10), (1.30, 0, 0.05), "PaintedFrame"),

        # Balancer and hose drop.
        cyl("Split_Balancer", 0.18, 0.34, (0, 0, 3.20), "SteelBrushed"),
        tube("Split_Hose", 0.03, 1.20, (0.22, 0, 2.70), "Rubber"),

        # Operator platform, adjustable in reality, fixed here.
        box("Split_Platform", (1.40, 1.20, 0.10), (0, -1.10, 0.55), "SteelBrushed"),
        box("Split_PlatPostA", (0.06, 0.06, 0.55), (-0.65, -1.60, 0.27), "PaintedFrame"),
        box("Split_PlatPostB", (0.06, 0.06, 0.55), (0.65, -1.60, 0.27), "PaintedFrame"),
        box("Split_Panel", (0.36, 0.12, 0.46), (1.30, -0.70, 1.40), "PaintedFrame"),
        box("Split_Sterilizer", (0.22, 0.20, 0.32), (-1.30, -0.80, 1.25), "Stainless"),
    ] + rail_section("Split_Rail", 3.00, (0, 0, 0), RAIL_Z + 0.30)

    # --- saw body (strokes) and blade (runs) -----------------------------
    stroke = "sawarm"
    blade = "sawblade"
    parts += [
        box("Split_SawBody", (0.36, 0.30, 0.52), (0, 0.10, 2.45), "PaintedFrame", part=stroke),
        box("Split_SawHandle", (0.10, 0.46, 0.10), (0, -0.18, 2.55), "Rubber", part=stroke),
        box("Split_SawGuard", (0.10, 0.22, 0.70), (0, 0.10, 2.02), "SafetyYellow", part=stroke),
    ]
    parts += [
        box("Split_Blade", (0.02, 0.14, 0.90), (0, 0.10, 1.92), "Stainless", part=blade),
    ]

    bones = [
        {"name": stroke, "head": (0.0, 0.10, 2.45), "axis": (0.0, 0.0, -1.0), "length": 0.70},
        {"name": blade, "head": (0.0, 0.10, 2.30), "axis": (0.0, 0.0, -1.0), "length": 0.90},
    ]

    def animate(rig):
        key_stroke(rig, stroke, distance=1.10, frames=LOOP, dwell=0.15)
        # Short, fast reciprocation across the whole loop: a band blade running,
        # not a part moving. Ten strokes per carcass cycle.
        key_stroke(rig, blade, distance=0.06, frames=LOOP, cycles=10)

    return parts, bones, animate


# ===========================================================================
# 9. Carcass inspection
# ===========================================================================

def station_carcass_inspection():
    """
    Veterinary inspection stand: rail, hard light, platform, terminal.

    Everything about it exists to let one person see a carcass properly and
    record a verdict, so the light bar and the terminal are the station.
    """
    parts = rail_section("Insp_Rail", 4.00, (0, 0, 0), RAIL_Z)
    parts += [
        box("Insp_Platform", (1.50, 2.60, 0.10), (1.40, 0, 0.40), "SteelBrushed"),
        box("Insp_Step", (0.55, 1.00, 0.06), (2.35, 0, 0.20), "SteelBrushed"),
        box("Insp_HandrailTop", (0.05, 2.60, 0.05), (2.10, 0, 1.50), "Stainless"),
        box("Insp_HandrailPostA", (0.06, 0.06, 1.00), (2.10, -1.20, 0.95), "Stainless"),
        box("Insp_HandrailPostB", (0.06, 0.06, 1.00), (2.10, 1.20, 0.95), "Stainless"),

        # Inspection light: high CRI, close, and from two sides.
        box("Insp_LightPost", (0.09, 0.09, 2.20), (0.75, -1.35, 1.55), "PaintedFrame"),
        box("Insp_LightArm", (0.09, 2.60, 0.09), (0.75, 0, 2.60), "PaintedFrame"),
        box("Insp_LightBox", (0.22, 2.20, 0.14), (0.62, 0, 2.45), "SteelBrushed"),
        box("Insp_LightLens", (0.03, 2.10, 0.10), (0.50, 0, 2.45), "Perspex"),

        # Terminal for the verdict, and the knife station.
        box("Insp_Terminal", (0.34, 0.10, 0.40), (1.95, -0.90, 1.35), "PaintedFrame"),
        box("Insp_Screen", (0.28, 0.02, 0.32), (1.95, -0.96, 1.35), "Perspex"),
        box("Insp_Sterilizer", (0.22, 0.20, 0.32), (1.95, 0.60, 1.20), "Stainless"),
        box("Insp_Basin", (0.44, 0.36, 0.22), (1.95, 1.10, 1.05), "Stainless"),
        box("Insp_Retain", (0.60, 0.60, 1.10), (-1.40, 1.60, 0.55), "SafetyYellow"),
    ]
    return parts, [], None


# ===========================================================================
# 10. Chilling
# ===========================================================================

def station_blast_chiller():
    """
    Blast chill tunnel: carcasses enter warm and leave at 7 C.

    The fans are the only thing to animate and they are behind a grille, so they
    are modelled as simple bladed discs -- detail there is invisible in engine.
    """
    parts = [
        box("Chill_WallL", (0.20, 6.00, 3.20), (-1.60, 0, 1.60), "FoodPlastic"),
        box("Chill_WallR", (0.20, 6.00, 3.20), (1.60, 0, 1.60), "FoodPlastic"),
        box("Chill_Roof", (3.40, 6.00, 0.20), (0, 0, 3.30), "FoodPlastic"),
        box("Chill_DoorFrame", (3.40, 0.14, 3.20), (0, -3.00, 1.60), "Stainless"),
        box("Chill_DoorLeaf", (1.40, 0.10, 2.60), (-0.80, -3.05, 1.35), "Stainless"),
        box("Chill_DoorHandle", (0.06, 0.10, 0.34), (-0.15, -3.14, 1.35), "SteelBrushed"),
        box("Chill_Kerb", (3.40, 6.00, 0.10), (0, 0, 0.05), "Concrete"),

        # Evaporator over the middle of the tunnel.
        box("Chill_EvapBody", (2.40, 1.90, 0.80), (0, 0.80, 2.70), "SteelBrushed"),
        box("Chill_EvapFins", (2.20, 0.24, 0.62), (0, -0.20, 2.70), "SteelBrushed"),
        tube("Chill_Suction", 0.09, 1.60, (1.05, 1.70, 3.40), "Copper"),
        tube("Chill_Liquid", 0.05, 1.60, (0.80, 1.70, 3.40), "Copper"),
        box("Chill_Panel", (0.40, 0.12, 0.50), (1.60, -2.20, 1.60), "PaintedFrame"),
        box("Chill_Display", (0.22, 0.02, 0.14), (1.60, -2.28, 1.75), "Perspex"),
    ] + rail_section("Chill_Rail", 5.80, (0, 0, 0), RAIL_Z)

    bones = []
    for side, y in (("A", 0.30), ("B", 1.30)):
        part = "fan{:s}".format(side)
        parts.append(cyl("Chill_FanHub{:s}".format(side), 0.10, 0.16, (0, y, 2.70),
                         "SteelBrushed", rot=(math.radians(90), 0, 0), part=part))
        for blade in range(5):
            angle = blade * 2.0 * math.pi / 5.0
            radius = 0.30
            parts.append(box(
                "Chill_Blade{:s}{:d}".format(side, blade), (0.44, 0.06, 0.18),
                (math.cos(angle) * radius, y, 2.70 + math.sin(angle) * radius),
                "SteelBrushed", rot=(0, 0, 0), part=part))
        parts.append(cyl("Chill_Grille{:s}".format(side), 0.42, 0.03, (0, y - 0.18, 2.70),
                         "PaintedFrame", rot=(math.radians(90), 0, 0), part=""))
        bones.append({"name": part, "head": (0.0, y, 2.70),
                      "axis": (0.0, 1.0, 0.0), "length": 0.40})

    def animate(rig):
        key_spin(rig, "fanA", turns=8, frames=LOOP)
        key_spin(rig, "fanB", turns=8, frames=LOOP)

    return parts, bones, animate


# ===========================================================================
# 11. Deboning
# ===========================================================================

def station_deboning_line():
    """
    Deboning line: a moving belt with work positions and chutes down both sides.

    The belt itself does not need a bone -- in engine it is a scrolling UV,
    which is cheaper and reads better than any geometry.
    """
    parts = [
        box("Debone_BeltTop", (0.90, 6.00, 0.04), (0, 0, 0.92), "Rubber"),
        box("Debone_BeltFrameL", (0.06, 6.00, 0.20), (-0.48, 0, 0.85), "Stainless"),
        box("Debone_BeltFrameR", (0.06, 6.00, 0.20), (0.48, 0, 0.85), "Stainless"),
        cyl("Debone_DrumIn", 0.12, 0.90, (0, -3.00, 0.86), "SteelBrushed",
            rot=(0, math.radians(90), 0)),
        cyl("Debone_DrumOut", 0.12, 0.90, (0, 3.00, 0.86), "SteelBrushed",
            rot=(0, math.radians(90), 0)),
        box("Debone_Motor", (0.30, 0.34, 0.30), (0.70, 3.00, 0.60), "PaintedFrame"),
    ]

    # Legs down the run.
    for index in range(5):
        y = -2.60 + index * 1.30
        parts += [
            box("Debone_LegL{:d}".format(index), (0.08, 0.08, 0.82), (-0.44, y, 0.41),
                "PaintedFrame"),
            box("Debone_LegR{:d}".format(index), (0.08, 0.08, 0.82), (0.44, y, 0.41),
                "PaintedFrame"),
        ]

    # Work positions: board, chute and bin, alternating sides.
    for index in range(4):
        y = -2.25 + index * 1.50
        x = 1.05 if index % 2 == 0 else -1.05
        tag = "P{:d}".format(index)
        parts += [
            box("Debone_Bench" + tag, (0.80, 1.20, 0.05), (x, y, 0.95), "Stainless"),
            box("Debone_Board" + tag, (0.66, 1.00, 0.03), (x, y, 0.985), "FoodPlastic"),
            box("Debone_LegA" + tag, (0.06, 0.06, 0.92), (x - 0.34, y - 0.52, 0.46),
                "PaintedFrame"),
            box("Debone_LegB" + tag, (0.06, 0.06, 0.92), (x + 0.34, y - 0.52, 0.46),
                "PaintedFrame"),
            box("Debone_LegC" + tag, (0.06, 0.06, 0.92), (x - 0.34, y + 0.52, 0.46),
                "PaintedFrame"),
            box("Debone_LegD" + tag, (0.06, 0.06, 0.92), (x + 0.34, y + 0.52, 0.46),
                "PaintedFrame"),
            box("Debone_Bin" + tag, (0.52, 0.42, 0.34), (x * 1.55, y, 0.17), "BluePlastic"),
            box("Debone_Steri" + tag, (0.20, 0.18, 0.30), (x * 1.28, y + 0.50, 1.10),
                "Stainless"),
        ]

    parts += [
        box("Debone_LightBar", (0.30, 5.60, 0.10), (0, 0, 2.45), "SteelBrushed"),
        box("Debone_LightLens", (0.24, 5.50, 0.03), (0, 0, 2.39), "Perspex"),
        box("Debone_LightPostA", (0.07, 0.07, 1.50), (0, -2.80, 1.75), "PaintedFrame"),
        box("Debone_LightPostB", (0.07, 0.07, 1.50), (0, 2.80, 1.75), "PaintedFrame"),
    ]
    return parts, [], None


# ===========================================================================
# 12. Grinding
# ===========================================================================

def station_grinder():
    """Industrial mincer: hopper, horizontal auger, plate and knife head."""
    parts = [
        box("Grind_Body", (0.80, 1.30, 0.70), (0, 0.10, 1.05), "Stainless"),
        box("Grind_HopperFrontL", (0.06, 0.90, 0.55), (-0.52, -0.30, 1.65), "Stainless",
            rot=(0, math.radians(-18), 0)),
        box("Grind_HopperFrontR", (0.06, 0.90, 0.55), (0.52, -0.30, 1.65), "Stainless",
            rot=(0, math.radians(18), 0)),
        box("Grind_HopperBack", (1.00, 0.06, 0.55), (0, 0.16, 1.65), "Stainless"),
        box("Grind_HopperFront", (1.00, 0.06, 0.55), (0, -0.76, 1.65), "Stainless"),
        box("Grind_HopperLip", (1.10, 1.00, 0.04), (0, -0.30, 1.93), "Stainless"),

        cyl("Grind_Head", 0.24, 0.44, (0, -1.05, 1.05), "Stainless",
            rot=(math.radians(90), 0, 0)),
        cyl("Grind_Plate", 0.25, 0.05, (0, -1.29, 1.05), "SteelBrushed",
            rot=(math.radians(90), 0, 0)),
        box("Grind_Outlet", (0.44, 0.30, 0.34), (0, -1.48, 0.90), "Stainless"),

        box("Grind_Motor", (0.52, 0.56, 0.50), (0, 1.05, 0.95), "PaintedFrame"),
        cyl("Grind_MotorFan", 0.18, 0.10, (0, 1.38, 0.95), "SteelBrushed",
            rot=(math.radians(90), 0, 0)),
        box("Grind_LegFL", (0.10, 0.10, 0.70), (-0.32, -0.45, 0.35), "PaintedFrame"),
        box("Grind_LegFR", (0.10, 0.10, 0.70), (0.32, -0.45, 0.35), "PaintedFrame"),
        box("Grind_LegBL", (0.10, 0.10, 0.70), (-0.32, 0.65, 0.35), "PaintedFrame"),
        box("Grind_LegBR", (0.10, 0.10, 0.70), (0.32, 0.65, 0.35), "PaintedFrame"),
        box("Grind_Panel", (0.30, 0.10, 0.40), (0.52, 0.10, 1.35), "PaintedFrame"),
        box("Grind_Estop", (0.10, 0.06, 0.10), (0.52, 0.04, 1.58), "SafetyYellow"),
        box("Grind_Bin", (0.62, 0.52, 0.42), (0, -1.85, 0.21), "BluePlastic"),
    ]

    spin = "auger"
    parts.append(cyl("Grind_AugerShaft", 0.07, 1.90, (0, -0.10, 1.05), "SteelBrushed",
                     rot=(math.radians(90), 0, 0), part=spin))
    for index in range(14):
        y = -1.00 + index * 0.15
        angle = index * math.pi / 3.5
        parts.append(box(
            "Grind_Flight{:d}".format(index), (0.36, 0.03, 0.36),
            (0, y, 1.05), "SteelBrushed", rot=(0, angle, 0), part=spin))

    bones = [{"name": spin, "head": (0.0, -1.05, 1.05), "axis": (0.0, 1.0, 0.0), "length": 1.90}]

    def animate(rig):
        key_spin(rig, spin, turns=5, frames=LOOP)

    return parts, bones, animate


# ===========================================================================
# 13. Packing
# ===========================================================================

def station_vacuum_packer():
    """Chamber vacuum packer: the lid is the cycle, so the lid is the animation."""
    parts = [
        box("Pack_Body", (1.30, 0.90, 0.55), (0, 0, 0.75), "Stainless"),
        box("Pack_ChamberFloor", (1.10, 0.70, 0.04), (0, 0, 1.00), "Stainless"),
        box("Pack_SealBarA", (1.00, 0.06, 0.05), (0, -0.22, 1.04), "PaintedFrame"),
        box("Pack_SealBarB", (1.00, 0.06, 0.05), (0, 0.22, 1.04), "PaintedFrame"),
        box("Pack_LegFL", (0.10, 0.10, 0.48), (-0.55, -0.35, 0.24), "PaintedFrame"),
        box("Pack_LegFR", (0.10, 0.10, 0.48), (0.55, -0.35, 0.24), "PaintedFrame"),
        box("Pack_LegBL", (0.10, 0.10, 0.48), (-0.55, 0.35, 0.24), "PaintedFrame"),
        box("Pack_LegBR", (0.10, 0.10, 0.48), (0.55, 0.35, 0.24), "PaintedFrame"),
        box("Pack_Panel", (0.30, 0.08, 0.24), (0.48, -0.47, 0.95), "PaintedFrame"),
        box("Pack_Screen", (0.22, 0.02, 0.16), (0.48, -0.52, 0.95), "Perspex"),
        cyl("Pack_Pump", 0.18, 0.50, (0, 0.30, 0.40), "SteelBrushed",
            rot=(0, math.radians(90), 0)),

        # Infeed and outfeed rollers.
        box("Pack_InTable", (0.70, 0.60, 0.04), (-1.20, 0, 1.00), "Stainless"),
        box("Pack_OutTable", (0.70, 0.60, 0.04), (1.20, 0, 1.00), "Stainless"),
        box("Pack_InLegA", (0.06, 0.06, 0.98), (-1.45, -0.25, 0.49), "PaintedFrame"),
        box("Pack_InLegB", (0.06, 0.06, 0.98), (-1.45, 0.25, 0.49), "PaintedFrame"),
        box("Pack_OutLegA", (0.06, 0.06, 0.98), (1.45, -0.25, 0.49), "PaintedFrame"),
        box("Pack_OutLegB", (0.06, 0.06, 0.98), (1.45, 0.25, 0.49), "PaintedFrame"),
    ]

    lid = "lid"
    parts += [
        box("Pack_Lid", (1.24, 0.84, 0.06), (0, 0, 1.36), "Perspex", part=lid),
        box("Pack_LidRim", (1.30, 0.90, 0.06), (0, 0, 1.31), "Stainless", part=lid),
        box("Pack_LidHandle", (0.30, 0.06, 0.06), (0, -0.47, 1.38), "SteelBrushed", part=lid),
    ]
    bones = [{"name": lid, "head": (0.0, 0.0, 1.36), "axis": (0.0, 0.0, -1.0), "length": 0.35}]

    def animate(rig):
        # Long dwell: the vacuum draw is most of the cycle, and a lid that
        # bounces straight back up reads as a lid, not a vacuum packer.
        key_stroke(rig, lid, distance=0.30, frames=LOOP, dwell=0.6)

    return parts, bones, animate


# ===========================================================================
# 14. Metal detection
# ===========================================================================

def station_metal_detector():
    """Inline metal detector with a reject arm and a lockable reject bin."""
    parts = [
        box("Metal_HeadTop", (1.10, 0.44, 0.22), (0, 0, 1.52), "Stainless"),
        box("Metal_HeadL", (0.22, 0.44, 0.60), (-0.44, 0, 1.10), "Stainless"),
        box("Metal_HeadR", (0.22, 0.44, 0.60), (0.44, 0, 1.10), "Stainless"),
        box("Metal_Display", (0.30, 0.04, 0.22), (0, -0.24, 1.52), "Perspex"),

        box("Metal_BeltTop", (0.70, 2.60, 0.04), (0, 0, 0.86), "Rubber"),
        box("Metal_BeltFrameL", (0.05, 2.60, 0.16), (-0.38, 0, 0.80), "Stainless"),
        box("Metal_BeltFrameR", (0.05, 2.60, 0.16), (0.38, 0, 0.80), "Stainless"),
        cyl("Metal_DrumIn", 0.10, 0.70, (0, -1.30, 0.80), "SteelBrushed",
            rot=(0, math.radians(90), 0)),
        cyl("Metal_DrumOut", 0.10, 0.70, (0, 1.30, 0.80), "SteelBrushed",
            rot=(0, math.radians(90), 0)),
        box("Metal_LegFL", (0.07, 0.07, 0.78), (-0.34, -1.15, 0.39), "PaintedFrame"),
        box("Metal_LegFR", (0.07, 0.07, 0.78), (0.34, -1.15, 0.39), "PaintedFrame"),
        box("Metal_LegBL", (0.07, 0.07, 0.78), (-0.34, 1.15, 0.39), "PaintedFrame"),
        box("Metal_LegBR", (0.07, 0.07, 0.78), (0.34, 1.15, 0.39), "PaintedFrame"),

        # Reject: a pusher and a locked bin, because a reject nobody can
        # retrieve without a key is the entire point of the device.
        box("Metal_Pusher", (0.18, 0.30, 0.20), (-0.52, 0.60, 0.98), "PaintedFrame"),
        box("Metal_RejectChute", (0.44, 0.50, 0.40), (0.72, 0.60, 0.75), "Stainless"),
        box("Metal_RejectBin", (0.50, 0.50, 0.55), (0.95, 0.60, 0.27), "SafetyYellow"),
        box("Metal_RejectLock", (0.08, 0.06, 0.10), (0.95, 0.34, 0.50), "SteelBrushed"),
        box("Metal_Panel", (0.30, 0.10, 0.38), (0.62, -1.05, 1.30), "PaintedFrame"),
    ]
    return parts, [], None


# ===========================================================================
# 15. Palletising
# ===========================================================================

def station_palletiser():
    """Gantry palletiser: a travelling carriage with a vacuum head."""
    parts = [
        box("Pall_PostA", (0.18, 0.18, 3.00), (-1.70, -1.60, 1.50), "PaintedFrame"),
        box("Pall_PostB", (0.18, 0.18, 3.00), (1.70, -1.60, 1.50), "PaintedFrame"),
        box("Pall_PostC", (0.18, 0.18, 3.00), (-1.70, 1.60, 1.50), "PaintedFrame"),
        box("Pall_PostD", (0.18, 0.18, 3.00), (1.70, 1.60, 1.50), "PaintedFrame"),
        box("Pall_BeamL", (0.16, 3.40, 0.24), (-1.70, 0, 3.10), "PaintedFrame"),
        box("Pall_BeamR", (0.16, 3.40, 0.24), (1.70, 0, 3.10), "PaintedFrame"),
        box("Pall_BaseA", (0.50, 0.50, 0.08), (-1.70, -1.60, 0.04), "PaintedFrame"),
        box("Pall_BaseB", (0.50, 0.50, 0.08), (1.70, -1.60, 0.04), "PaintedFrame"),
        box("Pall_BaseC", (0.50, 0.50, 0.08), (-1.70, 1.60, 0.04), "PaintedFrame"),
        box("Pall_BaseD", (0.50, 0.50, 0.08), (1.70, 1.60, 0.04), "PaintedFrame"),

        # Guarding, because a palletiser cell is fenced or it is not legal.
        box("Pall_FenceA", (3.60, 0.05, 2.00), (0, -1.85, 1.00), "SafetyYellow"),
        box("Pall_FenceB", (0.05, 3.60, 2.00), (-1.95, 0, 1.00), "SafetyYellow"),
        box("Pall_Panel", (0.40, 0.12, 0.52), (1.95, -1.20, 1.50), "PaintedFrame"),

        # Infeed conveyor and the pallet position.
        box("Pall_InfeedTop", (0.60, 1.60, 0.04), (0, -2.60, 0.86), "Rubber"),
        box("Pall_InfeedLegA", (0.06, 0.06, 0.82), (-0.26, -3.20, 0.41), "PaintedFrame"),
        box("Pall_InfeedLegB", (0.06, 0.06, 0.82), (0.26, -3.20, 0.41), "PaintedFrame"),
        box("Pall_PalletPad", (1.30, 0.90, 0.03), (0, 0.60, 0.02), "Concrete"),
    ]

    travel = "carriage"
    lift = "head"
    parts += [
        box("Pall_Carriage", (3.20, 0.34, 0.22), (0, -1.20, 3.10), "SteelBrushed", part=travel),
        box("Pall_CarriageRail", (0.24, 0.24, 0.60), (0, -1.20, 2.78), "SteelBrushed", part=travel),
    ]
    parts += [
        box("Pall_HeadPlate", (0.70, 0.56, 0.10), (0, -1.20, 2.45), "SteelBrushed", part=lift),
        box("Pall_HeadCupA", (0.14, 0.14, 0.12), (-0.22, -1.34, 2.34), "Rubber", part=lift),
        box("Pall_HeadCupB", (0.14, 0.14, 0.12), (0.22, -1.34, 2.34), "Rubber", part=lift),
        box("Pall_HeadCupC", (0.14, 0.14, 0.12), (-0.22, -1.06, 2.34), "Rubber", part=lift),
        box("Pall_HeadCupD", (0.14, 0.14, 0.12), (0.22, -1.06, 2.34), "Rubber", part=lift),
    ]

    bones = [
        {"name": travel, "head": (0.0, -1.20, 3.10), "axis": (0.0, 1.0, 0.0), "length": 0.60},
        {"name": lift, "head": (0.0, -1.20, 2.45), "axis": (0.0, 0.0, -1.0), "length": 0.60},
    ]

    def animate(rig):
        # Pick at the infeed, carry to the pallet, place, return. The lift runs
        # at twice the carriage rate so it is down at both ends of the travel.
        key_stroke(rig, travel, distance=1.90, frames=LOOP, dwell=0.25)
        key_stroke(rig, lift, distance=1.35, frames=LOOP, cycles=2)

    return parts, bones, animate


# ===========================================================================
# 16. Trimming
# ===========================================================================

def station_trim_bench():
    """Trimming bench: work height, backsplash, chute, sterilizer, hard light."""
    parts = frame("TrimBench", (2.40, 0.90, 0.95), (0, 0, 0), "Stainless")
    parts += [
        box("TrimBench_Splash", (2.40, 0.04, 0.45), (0, 0.43, 1.17), "Stainless"),
        box("TrimBench_LipFront", (2.40, 0.04, 0.06), (0, -0.43, 0.98), "Stainless"),
        box("TrimBench_Board", (2.10, 0.70, 0.03), (0, -0.05, 0.965), "FoodPlastic"),
        box("TrimBench_ChuteRim", (0.34, 0.30, 0.04), (0.95, -0.10, 0.955), "Stainless"),
        box("TrimBench_ChuteBody", (0.30, 0.26, 0.42), (0.95, -0.10, 0.72), "SteelBrushed"),
        box("TrimBench_Shelf", (2.20, 0.60, 0.03), (0, 0, 0.30), "Stainless"),
        box("TrimBench_Bin", (0.52, 0.42, 0.34), (0.95, -0.10, 0.20), "BluePlastic"),
        box("Sterilizer_Body", (0.22, 0.20, 0.34), (-1.06, 0.30, 1.12), "Stainless"),
        box("Sterilizer_Lid", (0.24, 0.22, 0.03), (-1.06, 0.30, 1.30), "Stainless"),
        tube("Sterilizer_Feed", 0.012, 0.30, (-1.06, 0.40, 1.45), "Stainless"),
        box("TrimBench_LightBar", (2.20, 0.12, 0.08), (0, 0.10, 2.05), "SteelBrushed"),
        box("TrimBench_LightLens", (2.10, 0.09, 0.02), (0, 0.10, 2.00), "Perspex"),
        box("TrimBench_LightPostL", (0.05, 0.05, 0.95), (-1.05, 0.40, 1.60), "PaintedFrame"),
        box("TrimBench_LightPostR", (0.05, 0.05, 0.95), (1.05, 0.40, 1.60), "PaintedFrame"),
    ]
    return parts, [], None


STATIONS = {
    "CO2_STUNNER": station_co2_stunner,
    "BLEED_RAIL": station_bleed_rail,
    "SCALD_TANK": station_scald_tank,
    "DEHAIRER": station_dehairer,
    "SINGE_CABINET": station_singe_cabinet,
    "POLISHER": station_polisher,
    "EVISCERATION_TABLE": station_evisceration_table,
    "SPLITTING_SAW": station_splitting_saw,
    "CARCASS_INSPECTION": station_carcass_inspection,
    "BLAST_CHILLER": station_blast_chiller,
    "DEBONING_LINE": station_deboning_line,
    "GRINDER": station_grinder,
    "VACUUM_PACKER": station_vacuum_packer,
    "METAL_DETECTOR": station_metal_detector,
    "PALLETISER": station_palletiser,
    "TRIM_BENCH": station_trim_bench,
}
