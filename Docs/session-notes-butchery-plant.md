# Session notes — butchery plant, optimisation and layout

What was done, what was found, and what is still open. Written so the next
person does not re-derive it.

The PLC work is not covered here; it is in
[`plc-digital-twin-plan.md`](plc-digital-twin-plan.md), and it is the thing most
worth picking up next.

---

## What was built

A 56-asset butchery library, modelled procedurally in Blender and exported as
FBX, plus a 13-chamber plant assembled from it in Unreal.

| | |
|---|---|
| Assets | 56 (`Tools/butchery/`), 18 skeletal with animation |
| Library cost | 17,476 triangles total — ~310 per asset |
| Levels | `/Game/level5` (roofed), `/Game/level5_cutaway` (no roof, for filming) |
| Plant | 13 chambers, 9 line groups, 172 stations, 420 line modules, 56 line ends, 66 transfer modules |
| Video | `Saved/Renders/Butchery_Interior_60fps.mp4` — 27 shots, 79.7 s at 1080p60 |

Single source of truth for the layout is `Tools/butchery/plant_layout.py`. The
top-view map and the Unreal build both read from it, so a chamber cannot be one
size on the plan and another in the level.

---

## Triangle optimisation

Asked to minimise unnecessary triangles. Final result: **22,584 → 17,026, 24.6%**
across the 55 assets that existed at the time.

Two mechanisms:

- **Cylinder resolution follows radius.** A 12 mm pipe drawn with 24 sides costs
  what a 1 m tank costs and is a smooth circle two pixels across.
- **Buried-face culling.** These assets are built from overlapping primitives —
  a leg pushed into a bench top, a shaft through a housing — so a real share of
  every surface is inside another solid.

### The correction that matters

**An earlier figure of 40% was wrong, and it was destructive.** `box()` assigns
`obj.scale` directly, and nothing in the library ever updated the depsgraph — so
`matrix_world` was still the pre-scale value and every occluder was tested as
the 1 m unit cube it was before it was sized.

That does not fail loudly. It culls whatever lies within a metre of a part's
centre and spares whatever lies outside, which looks exactly like a plausible
saving: 55 assets built, no failures, no warnings, a contact sheet full of
assets. What it was actually doing was reducing `PROP_CRATE` to a single grip
handle, `PROP_CARTON` to a floating label, and `PROP_CARCASS_HALF` — which hangs
on rails throughout the plant — to two faces on the loin.

A second flaw was independent of it: **testing a face by its centre**. A face's
centre is not where the face is. Three metres of pen rail threaded through a
7 cm post has side faces running the whole length, so every centre sits at the
middle, inside the post, and the whole rail was deleted on the strength of the
7 cm it passes through. A face is hidden only when *every corner* is inside.

### Invariants that now prevent a repeat

Both failures were silent, so both are now checked at build time:

- **Culling may thin a part, never delete one.** A wholly-buried part is not a
  saving; it is a part modelled inside another solid. It is kept whole and named
  in the build output. Nine such parts remain and all nine are legitimate — an
  auger in its barrel, basin floors, the interior cartons of a pallet stack, a
  kingpin under a chassis.
- **Every bone must still weigh geometry.** An earlier orphan check passed on
  geometry it had erased, because it walked the vertex groups that *survived*
  rather than the bones that should have had them.

The second invariant earned its keep immediately: the first `CHILL_EVAPORATOR`
had its coil fins modelled inside the body, and the build named them on the
first attempt instead of shipping a cooler with a blank front.

### Modelling faults the culling exposed

- The render tank's leg loop also emitted a "band" box at the tank's own axis,
  twice over, three times each — six identical solids buried in the shell.
- The blast chiller's two fans sat wholly inside the evaporator body. Deleting
  them left two animated bones driving nothing: the rig exported, the animation
  played, and nothing on screen moved. Their blades were also rotated about Z
  while the fan turns about Y, so each read as a tab on the rim. The guard was a
  solid disc over the thing it guards.
- The dehairer's inspection window sat inside the side panel's own 80 mm
  thickness — a window buried in the wall it was meant to be a window in.

---

## Layout: dead ends and empty rooms

Audited from the layout data rather than by eye, which is the only reason it was
found.

**39 of 60 line ends terminated at nothing.** `line_positions` spanned every
line from one chamber wall to the other, so each belt, rail and pen run was
tiled into masonry and stopped. Lines now stop 4 m short, and each kind names
the unit that puts product on and takes it off — accumulation off a belt, points
where a rail joins the main route, a gate at the end of a pen run. 56 ends are
furnished; the remaining four are the plant room's pipe bridge, exempt on
purpose, because services running into a wall is what services do.

**`CARCASS_CHILL` was empty** — six rails and no equipment, in the largest
chamber in the building at 832 m². A chill hall has no process machinery,
because the process is time and cold air, so the library had no asset for what
belongs there. `CHILL_EVAPORATOR` was added: ceiling-hung, fins on the back and
fans on the front, which is both how air moves through one and the face by which
a cooler is recognised. Five, not eight — eight 3.1 m units in a 32 m hall meet
end to end and read as one continuous duct.

Occupancy per 100 m², before → after:

| chamber | before | after |
|---|---|---|
| CARCASS_CHILL | **0.00** | 2.64 |
| BLAST_CHILL | 0.51 | 2.71 |
| BYPRODUCT | 0.89 | 1.78 |
| EVISCERATION | 1.03 | 1.54 |
| PACKING | 1.11 | 1.94 |
| RENDERING | 1.15 | 1.54 |

Nothing is empty; nothing is below 1.5.

---

## Tooling faults fixed along the way

- **`asset_manifest.json` had no generator.** It was written by hand in an
  earlier session and went stale the moment an asset was added — a silent
  failure, since the build skips what it cannot find in the manifest and only
  reports it as unplaced. `export_manifest.py` now derives it from the exported
  FBX files, which is the only place the answer is true: everything measured
  before export describes the Blender scene, not the file on disk.
- **The contact sheet was useless and looked fine.** Its ortho scale came from
  the grid size over a guessed constant and was wide by a factor of three — the
  assets were specks on an empty floor with the bottom row over the edge of it.
  It now fits to the scene's own bounds in camera space, and the floor is
  centred on the grid rather than on an origin the grid runs 108 m away from.

---

## Still open

- **The 28 showcase stills and the gallery page are stale.** They predate the
  room-filling work, so they show empty chambers. Roughly 50 minutes to refresh.
- **`level5` has never been GPU-profiled.** At ~310 triangles per asset the
  geometry is not the cost; the level places on the order of 1,500 actors, so
  draw calls dominate. If it needs to run fast, that is where to look, not in
  the meshes.
- **Trailers stand inside the dispatch room** rather than at the dock face.
- `Config/DefaultEngine.ini` has an uncommitted `EditorStartupMap` change
  pointing at `level4`; left alone deliberately, as it is a local preference.

---

## Working notes for anyone continuing

- Blender's `matrix_world` is lazily evaluated. After setting `obj.scale`, call
  `bpy.context.view_layer.update()` before reading it — see above for what
  happens otherwise.
- `bake_anim` writes object-level location keyed at the origin, which overwrites
  `location`. Use `delta_location` to reposition imported rigs.
- Unreal's FBX import ignores object-level animation on static meshes, so
  anything that moves must be a skeletal mesh with one bone per moving part.
- Convention throughout the library: **a bone points along its own axis of
  motion**, so every pose channel is the bone's local Y.
- Verify by measuring, not by looking. Every significant fault in this session
  was invisible in a render and obvious in the data.
