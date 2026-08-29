"""
Draws the top-view plant map from plant_layout.py.

Emits an SVG fragment with CSS classes rather than baked-in colours, so the page
that embeds it can theme it. Run with any Python 3; needs no Blender.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import plant_layout as P  # noqa: E402

PX = 10.0          # pixels per metre
# Enough margin for the scale bar and north arrow to sit outside the building.
# Inside, they overlapped the cutting hall and read as plant equipment.
PAD_X = 30.0
PAD_TOP = 30.0
PAD_BOTTOM = 76.0


def sx(x):
    return PAD_X + x * PX


def sy(y):
    """Y is flipped: the data has north up, SVG has Y down."""
    return PAD_TOP + (P.BUILDING["depth"] - y) * PX


def esc(text):
    return (text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def build():
    width = P.BUILDING["width"] * PX + PAD_X * 2
    height = P.BUILDING["depth"] * PX + PAD_TOP + PAD_BOTTOM
    out = []

    out.append(
        '<svg class="plan" viewBox="0 0 {:.0f} {:.0f}" '
        'xmlns="http://www.w3.org/2000/svg" role="img" '
        'aria-label="Top view plan of a multi-chamber pork processing plant">'
        .format(width, height))

    # Arrowhead for flow direction.
    out.append(
        '<defs><marker id="flow" viewBox="0 0 10 10" refX="9" refY="5" '
        'markerWidth="5" markerHeight="5" orient="auto-start-reverse">'
        '<path d="M0,0 L10,5 L0,10 z" class="arrow"/></marker>'
        '<marker id="railtick" viewBox="0 0 4 8" refX="2" refY="4" '
        'markerWidth="4" markerHeight="8" orient="auto">'
        '<path d="M2,0 L2,8" class="railtick"/></marker></defs>')

    # --- building shell ---
    out.append(
        '<rect class="shell" x="{:.1f}" y="{:.1f}" width="{:.1f}" height="{:.1f}" '
        'rx="3"/>'.format(sx(0), sy(P.BUILDING["depth"]),
                          P.BUILDING["width"] * PX, P.BUILDING["depth"] * PX))

    # --- chambers ---
    for name, x, y, w, h, zone, temp, label in P.CHAMBERS:
        out.append(
            '<rect class="chamber zone-{:s}" x="{:.1f}" y="{:.1f}" '
            'width="{:.1f}" height="{:.1f}" rx="2"/>'
            .format(zone, sx(x), sy(y + h), w * PX, h * PX))

        cx = sx(x + w / 2.0)
        out.append('<text class="ch-name" x="{:.1f}" y="{:.1f}">{:s}</text>'
                   .format(cx, sy(y + h) + 20, esc(name.replace("_", " "))))
        out.append('<text class="ch-label" x="{:.1f}" y="{:.1f}">{:s}</text>'
                   .format(cx, sy(y + h) + 35, esc(label)))
        if temp is not None:
            out.append('<text class="ch-temp" x="{:.1f}" y="{:.1f}">{:+.0f} &#176;C</text>'
                       .format(cx, sy(y + h) + 50, temp))
        out.append('<text class="ch-dim" x="{:.1f}" y="{:.1f}">{:.0f} &#215; {:.0f} m</text>'
                   .format(sx(x + w) - 6, sy(y) - 7, w, h))

    # --- lines inside chambers ---
    for chamber_name, count, axis, kind, label in P.LINES:
        for (ax, ay), (bx, by) in P.line_positions(chamber_name, count, axis):
            out.append(
                '<line class="line-{:s}" x1="{:.1f}" y1="{:.1f}" x2="{:.1f}" y2="{:.1f}"/>'
                .format(kind, sx(ax), sy(ay), sx(bx), sy(by)))
        x, y, w, h = P.rect(chamber_name)
        out.append('<text class="ch-lines" x="{:.1f}" y="{:.1f}">{:d} &#215; {:s}</text>'
                   .format(sx(x) + 7, sy(y) - 8, count, esc(label)))

    # --- the overhead rail route ---
    points = " ".join("{:.1f},{:.1f}".format(sx(px), sy(py))
                      for px, py in P.RAIL_ROUTE)
    out.append('<polyline class="rail" points="{:s}" marker-end="url(#flow)"/>'.format(points))

    # --- transfers between chambers ---
    for kind, ax, ay, bx, by in P.TRANSFERS:
        out.append(
            '<line class="transfer t-{:s}" x1="{:.1f}" y1="{:.1f}" x2="{:.1f}" y2="{:.1f}" '
            'marker-end="url(#flow)"/>'
            .format(kind, sx(ax), sy(ay), sx(bx), sy(by)))
        mx, my = (sx(ax) + sx(bx)) / 2.0, (sy(ay) + sy(by)) / 2.0
        out.append('<text class="t-label" x="{:.1f}" y="{:.1f}">{:s}</text>'
                   .format(mx, my - 6, esc(kind)))

    # --- scale bar and north ---
    bar_m = 20.0
    bx0 = sx(2.0)
    by0 = sy(0.0) + 34
    out.append('<line class="scalebar" x1="{:.1f}" y1="{:.1f}" x2="{:.1f}" y2="{:.1f}"/>'
               .format(bx0, by0, bx0 + bar_m * PX, by0))
    for tick in (0.0, bar_m):
        out.append('<line class="scalebar" x1="{:.1f}" y1="{:.1f}" x2="{:.1f}" y2="{:.1f}"/>'
                   .format(bx0 + tick * PX, by0 - 5, bx0 + tick * PX, by0 + 5))
    out.append('<text class="scale-label" x="{:.1f}" y="{:.1f}">{:.0f} m</text>'
               .format(bx0 + bar_m * PX / 2.0, by0 + 18, bar_m))

    nx = sx(P.BUILDING["width"] - 6.0)
    ny = sy(0.0) + 52
    out.append('<line class="north" x1="{:.1f}" y1="{:.1f}" x2="{:.1f}" y2="{:.1f}" '
               'marker-end="url(#flow)"/>'.format(nx, ny, nx, ny - 30))
    out.append('<text class="north-label" x="{:.1f}" y="{:.1f}">N</text>'
               .format(nx, ny + 14))

    out.append("</svg>")
    return "\n".join(out)


if __name__ == "__main__":
    svg = build()
    path = os.path.join(HERE, "plant_map.svg")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(svg)
    print("wrote {:s} ({:d} bytes)".format(path, len(svg)))
    print("shopping list:")
    for asset, count in P.shopping_list().items():
        print("  {:>3d}  {:s}".format(count, asset))
