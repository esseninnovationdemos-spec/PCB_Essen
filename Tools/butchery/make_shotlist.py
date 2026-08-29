"""
Generates the showcase shot list from plant_layout.py.

Camera positions are computed from the chambers and lines rather than typed by
hand, so a shot cannot end up inside a wall when the layout moves -- which is
how the first three interior attempts were lost.

Writes shots.json for the batch renderer.
"""

import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import plant_layout as P  # noqa: E402

CUTAWAY = "/Game/level5_cutaway"
ROOFED = "/Game/level5"


def world(x, y, z=0.0):
    """Layout metres to the level's origin-centred metres."""
    return [round(x - P.BUILDING["width"] * 0.5, 2),
            round(y - P.BUILDING["depth"] * 0.5, 2),
            round(z, 2)]


import math as _math


def orbit(frm, look, degrees, rise=0.0):
    """
    The same viewpoint swung a few degrees around what it is looking at.

    A still only needs one pose; a shot needs two. Orbiting rather than dollying
    keeps the subject the same size for the whole shot and lets parallax do the
    work of showing depth -- a straight push-in on a static machine reads as a
    zoom, which looks like stock footage.
    """
    dx, dy = frm[0] - look[0], frm[1] - look[1]
    a = _math.radians(degrees)
    return [round(look[0] + dx * _math.cos(a) - dy * _math.sin(a), 2),
            round(look[1] + dx * _math.sin(a) + dy * _math.cos(a), 2),
            round(frm[2] + rise, 2)]


def shot(name, level, frm, look, group, note, seconds=2.6, swing=11.0, rise=0.0):
    return {"name": name, "level": level, "from": frm, "look": look,
            "to": orbit(frm, look, swing, rise),
            "seconds": seconds, "group": group, "note": note}


def build():
    shots = []
    W, D = P.BUILDING["width"], P.BUILDING["depth"]

    # --- the whole plant, cut away, from four corners ---------------------
    for name, fx, fy, note in (
            ("PLANT_NW", -0.24, -0.30, "whole plant from the south-west"),
            ("PLANT_NE",  1.24, -0.30, "whole plant from the south-east"),
            ("PLANT_SE",  1.24,  1.30, "whole plant from the north-east"),
            ("PLANT_SW", -0.24,  1.30, "whole plant from the north-west")):
        shots.append(shot(name, CUTAWAY,
                          world(W * fx, D * fy, 72.0),
                          world(W * 0.5, D * 0.52, 3.0),
                          "plant", note, seconds=4.5, swing=9.0, rise=-4.0))

    # Straight down the length, low enough to read the rail.
    shots.append(shot("PLANT_AXIS", CUTAWAY,
                      world(-24.0, D * 0.42, 34.0),
                      world(W * 0.7, D * 0.5, 3.0),
                      "plant", "along the building, cut away",
                      seconds=4.5, swing=7.0, rise=-2.0))

    # --- the shed itself, roofed ------------------------------------------
    shots.append(shot("SHED_DOCK", ROOFED,
                      world(-42.0, -20.0, 26.0), world(55.0, 62.0, 4.0),
                      "shed", "dock elevation from the yard", seconds=4.0, swing=8.0))
    shots.append(shot("SHED_SOUTH", ROOFED,
                      world(W * 0.35, -76.0, 30.0), world(W * 0.55, 24.0, 4.0),
                      "shed", "south elevation", seconds=4.0, swing=8.0))
    shots.append(shot("SHED_AERIAL", ROOFED,
                      world(-30.0, -34.0, 96.0), world(W * 0.5, D * 0.5, 8.0),
                      "shed", "roof and rooflights from above", seconds=4.0, swing=8.0))

    # --- one per chamber ---------------------------------------------------
    for name, x, y, w, h, zone, temp, label in P.CHAMBERS:
        span = max(w, h)
        # Stand off the chamber's south-west corner, high enough to see over
        # its own walls but low enough that the machines still have depth.
        shots.append(shot(
            "CH_" + name, CUTAWAY,
            world(x - span * 0.34, y - span * 0.40, span * 0.62),
            world(x + w * 0.52, y + h * 0.52, 1.6),
            "chamber", label))

    # --- one per line group -------------------------------------------------
    for chamber, count, axis, kind, label in P.LINES:
        segments = P.line_positions(chamber, count, axis)
        first = segments[0]
        last = segments[-1]
        (ax, ay), (bx, by) = first
        dx, dy = bx - ax, by - ay
        length = math.hypot(dx, dy) or 1.0
        ux, uy = dx / length, dy / length
        # Perpendicular, to stand beside the group rather than on it.
        px, py = -uy, ux

        # Behind the head of the first line, offset across the whole group.
        (cx, cy), _ = last
        offx = (cx - ax) * 0.5
        offy = (cy - ay) * 0.5

        shots.append(shot(
            "LINE_" + chamber, CUTAWAY,
            world(ax - ux * 9.0 + offx + px * 2.0,
                  ay - uy * 9.0 + offy + py * 2.0, 7.5),
            world(ax + ux * length * 0.62 + offx,
                  ay + uy * length * 0.62 + offy, 2.0),
            "line", "{:d} x {:s}".format(count, label)))

    return shots


if __name__ == "__main__":
    shots = build()
    path = os.path.join(HERE, "shots.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(shots, handle, indent=1)
    print("wrote {:s}: {:d} shots".format(path, len(shots)))
    for group in ("plant", "shed", "chamber", "line"):
        names = [s["name"] for s in shots if s["group"] == group]
        print("  {:<8s} {:2d}  {:s}".format(group, len(names), ", ".join(names)))
