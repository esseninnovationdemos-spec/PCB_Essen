"""
Assembles the plant map page: template + generated SVG + a live shopping list.

The shopping list is built by reading `fbx/` rather than from a hand-kept list,
so it cannot drift out of date -- an asset is "built" if and only if its file is
on disk.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import plant_layout as P          # noqa: E402
import make_plant_map             # noqa: E402

FBX_DIR = os.path.join(HERE, "fbx")


def built_assets():
    if not os.path.isdir(FBX_DIR):
        return set()
    return {f[3:-4] for f in os.listdir(FBX_DIR)
            if f.startswith("SM_") and f.lower().endswith(".fbx")}


def shopping_table():
    have = built_assets()
    placements = P.shopping_list()
    needed = dict(P.NEEDED)

    # Everything the plan places, plus the infrastructure it needs but does not
    # place by count (walls, doors, conveyor runs).
    rows = []
    for asset, count in placements.items():
        rows.append((asset, count, needed.get(asset, ""), asset in have))
    for asset, note in P.NEEDED:
        if asset not in placements:
            rows.append((asset, None, note, asset in have))

    rows.sort(key=lambda r: (r[3], r[0]))

    out = ['<div class="tablescroll"><table>',
           "<thead><tr><th>Asset</th><th>Status</th><th class=\"num\">Placed</th>"
           "<th>Note</th></tr></thead><tbody>"]
    for asset, count, note, exists in rows:
        tag = ('<span class="tag have">built</span>' if exists
               else '<span class="tag todo">to build</span>')
        out.append(
            "<tr><td><code>{:s}</code></td><td>{:s}</td>"
            "<td class=\"num\">{:s}</td><td>{:s}</td></tr>"
            .format(asset, tag, str(count) if count else "&mdash;", note or "&mdash;"))
    out.append("</tbody></table></div>")

    missing = sum(1 for r in rows if not r[3])
    out.append(
        '<p class="caption">{:d} of {:d} asset types exist. The {:d} outstanding are '
        'the conveyors, the rail modules and the chamber shell &mdash; everything the '
        'plan needs to become a building rather than a set of machines.</p>'
        .format(len(rows) - missing, len(rows), missing))
    return "\n".join(out), missing, len(rows)


def main():
    template_path = os.path.join(HERE, "plant_map_template.html")
    with open(template_path, encoding="utf-8") as handle:
        page = handle.read()

    svg = make_plant_map.build()
    table, missing, total = shopping_table()

    page = page.replace("<!--PLAN-->", svg)
    page = page.replace("<!--SHOPPING-->", table)

    out_path = os.path.join(HERE, "plant_map.html")
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write(page)

    return out_path, missing, total


if __name__ == "__main__":
    path, missing, total = main()
    print("wrote {:s} ({:d} bytes)".format(path, os.path.getsize(path)))
    print("assets: {:d} built, {:d} to build, {:d} total".format(
        total - missing, missing, total))
