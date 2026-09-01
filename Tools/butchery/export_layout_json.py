"""
Writes plant_layout.json for the Unreal build commandlet.

The C++ side reads this rather than carrying its own copy of the layout. One
source: change a chamber here and the map and the level both follow. Duplicating
the numbers into C++ would guarantee they drift, and the first symptom would be
a wall through a machine.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import plant_layout as P  # noqa: E402


def main():
    data = {
        "building": P.BUILDING,
        "chambers": [
            {"name": n, "x": x, "y": y, "w": w, "h": h,
             "zone": z, "temperature": t, "label": lb}
            for n, x, y, w, h, z, t, lb in P.CHAMBERS
        ],
        "lines": [
            {"chamber": c, "count": n, "axis": a, "kind": k, "label": lb,
             "segments": [{"ax": p[0][0], "ay": p[0][1], "bx": p[1][0], "by": p[1][1]}
                          for p in P.line_positions(c, n, a)]}
            for c, n, a, k, lb in P.LINES
        ],
        "rail_route": [{"x": x, "y": y} for x, y in P.RAIL_ROUTE],
        "transfers": [
            {"kind": k, "ax": ax, "ay": ay, "bx": bx, "by": by}
            for k, ax, ay, bx, by in P.TRANSFERS
        ],
        "placements": [
            {"chamber": c, "asset": a, "count": n} for c, a, n in P.PLACEMENTS
        ],
    }

    path = os.path.join(HERE, "plant_layout.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=1)
    return path, data


if __name__ == "__main__":
    out, payload = main()
    print("wrote {:s} ({:d} bytes)".format(out, os.path.getsize(out)))
    print("chambers {:d}  lines {:d}  rail points {:d}  placements {:d}".format(
        len(payload["chambers"]), len(payload["lines"]),
        len(payload["rail_route"]), len(payload["placements"])))
