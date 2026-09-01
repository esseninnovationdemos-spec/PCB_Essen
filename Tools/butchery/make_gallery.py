"""
Builds the showcase gallery page from the rendered thumbnails.

Images go in as base64 data URIs because an artifact page has to be
self-contained -- it cannot reach a local file, and a CSP blocks every host but
Google Fonts. Run make_thumbs.ps1 first.
"""

import base64
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHOW = os.path.join(HERE, "showcase")
THUMBS = os.path.join(SHOW, "thumbs")

GROUPS = [
    ("plant", "The plant", "Roof removed, so the whole layout reads at once. "
     "Product runs a flattened S: east along the kill floor, down into chilling, "
     "then west through cutting and packing to the dock."),
    ("shed", "The shed", "The building as it actually stands — cladding, "
     "rooflights and the loading dock. Chambers are rooms inside this, which is "
     "why their partitions stop at 4 m and the shell does not."),
    ("chamber", "Chamber by chamber", "Thirteen rooms, each shot from its own "
     "corner at a distance derived from its footprint."),
    ("line", "Line by line", "Seven production lines, each seen down its own axis "
     "from behind the head of the group."),
]


def data_uri(name):
    path = os.path.join(THUMBS, name + ".jpg")
    if not os.path.exists(path):
        return None
    with open(path, "rb") as handle:
        return "data:image/jpeg;base64," + base64.b64encode(handle.read()).decode("ascii")


def main():
    with open(os.path.join(HERE, "shots.json"), encoding="utf-8") as handle:
        shots = json.load(handle)

    sections = []
    total = 0
    missing = []

    for key, title, blurb in GROUPS:
        rows = [s for s in shots if s["group"] == key]
        cards = []
        for shot in rows:
            uri = data_uri(shot["name"])
            if uri is None:
                missing.append(shot["name"])
                continue
            total += 1
            label = shot["name"]
            if label.startswith(("CH_", "LINE_")):
                label = label.split("_", 1)[1].replace("_", " ").title()
            else:
                label = label.replace("_", " ").title()
            cards.append(
                '<figure class="shot">'
                '<img src="{uri}" alt="{alt}" loading="lazy" width="720">'
                '<figcaption><span class="cap-name">{name}</span>'
                '<span class="cap-note">{note}</span></figcaption>'
                '</figure>'.format(uri=uri, alt=shot["note"], name=label,
                                   note=shot["note"]))

        sections.append(
            '<section><div class="sec-head"><h2>{title}</h2>'
            '<span class="count">{n}</span></div>'
            '<p class="blurb">{blurb}</p>'
            '<div class="grid grid-{key}">{cards}</div></section>'
            .format(title=title, n=len(cards), blurb=blurb, key=key,
                    cards="".join(cards)))

    template = os.path.join(HERE, "gallery_template.html")
    with open(template, encoding="utf-8") as handle:
        page = handle.read()

    page = page.replace("<!--SECTIONS-->", "\n".join(sections))
    page = page.replace("<!--COUNT-->", str(total))

    out = os.path.join(HERE, "gallery.html")
    with open(out, "w", encoding="utf-8") as handle:
        handle.write(page)

    return out, total, missing


if __name__ == "__main__":
    path, count, missing = main()
    print("wrote {:s}  {:d} shots  {:.1f} MB".format(
        path, count, os.path.getsize(path) / 1024.0 / 1024.0))
    if missing:
        print("missing thumbnails: " + ", ".join(missing))
        sys.exit(1)
