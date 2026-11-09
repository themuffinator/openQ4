# dmap surface storage

The `hub2.map` attachment in [issue #181](https://github.com/themuffinator/openQ4/issues/181)
exposed a crash in `ShareMapTriVerts` while writing the compiled `.proc` file.
The supplied dump records a request for 36,635,340 vertex slots, or
2,344,661,760 bytes with the 64-byte vertex layout. This exceeds the legacy
heap's signed 32-bit allocation limit. The index allocation succeeds, but the
vertex allocation returns null and the old writer immediately writes through it.

Surface sharing now keeps references to the first occurrence of each unique
vertex and allocates the final vertex buffer only after sharing is complete.
Index storage still includes every input triangle. The implementation checks
index-count multiplication, rejected geometry allocations and non-finite
positions, freeing the surface before reporting a compiler error.

A spatial hash replaces the linear scan through all preceding vertices.
Neighboring cells are searched across epsilon boundaries, and the earliest
matching vertex wins. This preserves the original position/UV tolerances,
normal matching, vertex order and index order, including texture seams and
hard edges. It does not discard overlapping triangles or change triangulation.

T-junction repair and output surface assembly also retain the tail of each
accumulated triangle list.
Appending each new fragment no longer traverses every previously appended
fragment. This preserves list order and avoids quadratic list assembly on
heavily split surfaces. A sample of the original `hub2` compile was inside
`MergeTriLists`, called by `FixGlobalTjunctions`; the same rescan was measured
later in `WriteOutputSurfaces`.

## Regression coverage

`python tools/tests/dmap_vertex_sharing.py` compiles the production sharing
implementation against a counted, fallible geometry allocator. It compares
results with the original linear matching rule, including positive and negative
cell boundaries, seams, smoothed/hard normals, ambiguous matches and varied
geometry. A 14,000-triangle surface with three unique vertices reproduces the
allocation-limit condition at a small scale: the old reservation exceeds the
256 KiB test limit, while the shared vertices and all 42,000 indexes fit.
Rejected index/vertex allocations, integer overflow and invalid positions also
exercise the error and cleanup paths.

Both GitHub validation workflows and the local validation runner include the
test. Use `--compiler cl` in the Windows developer environment, or
`--sanitize` with Clang/GCC on Linux. The existing
`dmap_render_geometry_lifecycle.py` and `aas_compiler_contract.py` checks cover
the surrounding allocator lifetime and compiler wiring.

The reporter's map and crash dump are investigation inputs only and are not
redistributed in the repository. Local qualification evidence belongs under
`.tmp/issue-181/`.

## Supplied-map qualification (14 October 2026)

The Windows x64 optimized build completed `dmap game/hub2` against installed
stock assets and exited normally. Geometry took 503 seconds, collision 3 seconds,
and navigation 9 seconds. The run produced the `.proc`, `.cm` and all five AAS
files. The largest surface contains 36,635,340 indexes but only 21,559 shared
vertices: its final vertex allocation is 1,379,776 bytes rather than the old
2,344,661,760-byte reservation. Windows Clang/MSVC native checks and Linux
Clang/GCC address/undefined-behavior sanitizer checks pass. Running the original
sharing implementation in the same regression fixture reproduces the null write.

Compilation success does **not** qualify this map for gameplay. Loading the
result reaches `R_ParseModel: bad surface counts`: its largest surface exceeds
the renderer's existing 16,777,216-index limit. The load also reports
`MAX_SIL_EDGES`. Those runtime limits and the source map's duplicate-plane,
node-without-volume and backwards-triangle diagnostics remain follow-up work;
the compiler fix does not relax renderer validation or rewrite map geometry.
Keep issue #181 open to track this distinction.

A separate staged OpenGL single-player smoke loads stock `game/airdefense1`,
enters gameplay, writes an engine screenshot and exits normally after three
seconds. This confirms stock-map startup, not playability of the rebuilt `hub2`.

The reporter's log additionally rejects the `hidef2kbc72` mod manifest's required
openQ4 version `1`; it expects a `major.minor.patch` value. This is separate from
the compiler allocation failure and was excluded from the stock-asset compile.

## Sliver multiplication in T-junction repair (31 October 2026)

The oversized surface was not real detail. Of the 12,601,692 triangles the
supplied map compiled to, 12,302,653 were collinear slivers with no area. The
largest surface's 12.2 million triangles held only 23,671 with any area, and
11.55 million slivers lay on a single line along one pipe.

T-junction repair splits a triangle wherever another surface's vertex lies
within `COLINEAR_EPSILON` of one of its edges. A sliver's three edges overlap
along one line, so both halves of a split still span it, and every further
vertex on that line splits both again. The supplied map raises 345 pipe patches
to explicit 10×10 subdivision, which puts many vertices on those lines. With
the stock subdivision and every other edit kept, it compiles in about a minute
to 118,497 triangles. The weakness is inherited: retail `hub2.proc` contains 43
slivers, and the stock map built with the previous dmap 58.

`FixTriangleAgainstHashVert` now leaves a sliver unsplit: a triangle whose
corners all lie within `COLINEAR_EPSILON` of the line through its longest edge.
Such a triangle is nowhere wider than the tolerance that places a vertex on an
edge, so it can hide no visible crack. Every other split is unchanged.

| Windows x64 debugoptimized | Previous dmap | Fixed dmap |
|---|---|---|
| Supplied map triangles | 12,601,692 | 298,438 |
| Supplied map zero-area triangles | 12,302,653 | 69 |
| Supplied map largest surface | 36,635,340 indexes | 153,000 indexes |
| Supplied map `.proc` | 271.6 MB | 49.6 MB |
| Stock `hub2` triangles | 118,312 | 118,255 |

Stock `hub2` changes in 14 surfaces. Of the 57 triangles it loses, 55 are
slivers, and the area of the non-sliver triangles is unchanged apart from one
118-unit needle in `_area63` that is no longer split. A full compile of the
supplied map, with collision and all five navigation files, takes 127 seconds.
The result loads into single-player gameplay without
`R_ParseModel: bad surface counts` and saves normally.

One limit remained: the `_area0` pipe surface was above the renderer's
65,536-edge working limit. See
[Silhouette edges on large surfaces](#silhouette-edges-on-large-surfaces-1-november-2026).

`python tools/tests/dmap_tjunction_slivers.py` compiles the production splitter
against a minimal geometry shim. A real triangle still splits at its
T-junction, a thin but valid triangle splits at all 31 vertexes along its edge,
and three slivers pass through unsplit; the previous splitter turns one of them
into 36 triangles. Both GitHub validation workflows and the local validation
runner include it. `--production <file>` runs it against another revision of
`tritjunction.cpp`.

The slivers come from the optimizer. `RegenerateTriangles` discards a
regenerated triangle that faces away from its group's plane, but a zero-area
triangle has no plane: `idPlane::FromPoints` returns false and
`FixDegenerateNormal` has already turned the zero normal into (0, 0, -1). On a
downward-facing group that passes the facing test, so the optimizer kept the
sliver and T-junction repair then met it. The optimizer now discards a
regenerated triangle with no area before the facing test. Measured with
`dmap noAAS noCM` on Linux x64 (GCC 16, debugoptimized):

| | Before | After |
|---|---|---|
| Supplied map triangles | 298,498 | 298,446 |
| Supplied map zero-area triangles | 58 | 6 |
| Stock `hub2` triangles | 118,336 | 118,318 |
| Stock `hub2` zero-area triangles | 24 | 6 |

The largest surface is unchanged. The same six zero-area triangles remain in
both maps, four in `func_static` pipe models and two in world surfaces. Their
corners lie on the 1/32-unit T-junction grid, so they arise after regeneration.
A Windows x64 build of the same change left none.

`python tools/tests/dmap_optimizer_zero_area.py` compiles the production
`RegenerateTriangles` with idlib's plane, normal and area code. Facing
triangles are kept and backwards ones dropped with their warning on upward,
downward and tilted planes, and zero-area triangles, including the logged
`hub2` seam, are dropped on all three. A mutant without the area check must
fail. `--production <file>` runs it against another revision of
`optimize.cpp`; the revision before this change fails it.

## Silhouette edges on large surfaces (1 November 2026)

Every triangle the renderer loads contributes its edges to a silhouette edge
list, which stencil shadow volumes extrude. The scratch that builds the list
held 65,536 edges. A larger surface lost every edge past that, reported only
by `developer 1` warnings. The supplied map's `_area0` pipe surface
(153,000 indexes) has 78,794 edges by the renderer's own matching rule, and
loading it printed 25,951 `MAX_SIL_EDGES` warnings.

`R_IdentifySilEdges` now sizes the scratch to the surface: a triangle defines at
most three edges, so the index count bounds them. The edge hash grows by the
same factor, so its chains stay as short as the fixed table's. After a large
surface both return to their usual size. Surfaces of up to 65,536 indexes use
exactly the old scratch, so their edges are unchanged. The two turbo shadow
builders kept their extruded side walls in a stack buffer of six indexes per
edge, which the old limit had kept near 1.5 MiB. Larger buffers now go on the
heap through `OPENQ4_ALLOC16_SCRATCH`. Silhouette edges are stored in generated
render-world and static-model caches, so their parser versions were raised to 4
and `0x00010004`. Any cache written with a cut-off list is rebuilt.

No stock content reached the limit. The largest stock map surface has 33,696
indexes, the largest static model 15,104 triangles, and the largest animated
mesh at most 39,804 edges.

The supplied map, rebuilt with the fixed dmap, now loads into single-player
gameplay and saves with no `MAX_SIL_EDGES` warnings. The same load with the
previous renderer still prints 25,951. Views of the pipe room with
`r_useOptimizedShadows 0` and `r_useShadowMap 0` render its runtime stencil
volumes normally. Animated lights and screens changed more between two runs of
the same build than between the builds, so the screenshots show no visible
difference either way.

`python tools/tests/renderer_silhouette_edges.py` compiles the production edge
builder with the real `idHashIndex` and compares it with a direct
implementation of id's matching rule. It covers a small closed mesh, an open
grid of 120,400 edges, a closed torus of 120,000 edges and a tangled surface
with duplicated and tripled edges. It also checks that the scratch and hash
return to their usual size. On the previous code the open grid stops at the
limit. Windows Clang, MSVC, Clang sanitizers and Linux GCC with sanitizers pass.
Both GitHub validation workflows and the local validation runner include it.
`--production <file>` runs it against another revision of
`RenderGeometryTriSurf.cpp`.

## airdefense1 and core1 slow compile report (5 November 2026)

The follow-up in [issue #209](https://github.com/themuffinator/openQ4/issues/209)
changes the reported symptom from an unexplained stop to excessive compile time:
the reporter says `airdefense1` completed after two days, the initial attempt did
not crash, and `core1` also eventually built. These are reporter observations;
neither run has been independently timed or qualified for gameplay.

The [supplied ZIP](https://github.com/user-attachments/files/34399834/airdefense1.zip)
contains exactly `airdefense1.map` (8,152,153 bytes) and `core1.map` (12,966,369 bytes).
Its SHA-256 is
`64b17d9f0c59c742c67d3ed3d814e05428ea3ea499ffb3b022e8eae15a8beae6`.
Static inspection against `maps/game/` in the installed Quake 4 1.4.2
`q4base/pak001.pk4` gives:

| Map | Total patches, supplied / stock | Explicit 10×10 patches, supplied / stock |
| --- | ---: | ---: |
| `airdefense1` | 5,500 / 5,500 | 1,206 / 0 |
| `core1` | 8,366 / 8,366 | 1,880 / 0 |

Both maps retain the stock entity count and set of `.lwo`/`.ase`/`.ma` model
references. This does not establish that the referenced assets are unmodified.
The higher subdivision is a concrete profiling lead, not proof of the cause or
evidence that reducing subdivision is required to compile correctly.

The original log records `0.13.2-dev+g3ea033f7` and
`dmap game/airdefense1.map`, with `hidef2kbc72/gfx.pk4` and
`hidef2kbc72/zzzzzpak_sikk.pk4` in the search path. Shadow building takes 254.7
seconds; the log then ends during `FixGlobalTjunctions`, after per-area
optimization, without a `.proc` write or normal completion. It cannot locate the
whole two-day delay, identify a hang, or qualify the later successful output.

`3ea033f7` contains the allocation/list-assembly fix, the T-junction sliver guard
and the optimizer zero-area rejection described above. Its
`src/tools/compilers/dmap/` tree is unchanged in the reviewed checkout
`2fdb9fe98`. It is a useful development baseline for these reports, but there is
no measured comparison here establishing a faster or better compiler for these
two maps. Compile completion alone does not establish rendering, collision or
AI correctness of the rebuilt files.

CORE-05 owns the remaining work: obtain the completed-run log and confirmation
of the successful command/build and asset modifications, measure stock-asset
stage timings and memory on both supplied maps, then profile the slow pass and
qualify any repair. This update made no compiler change and ran no compilation
or gameplay test. The original log, attachment and per-map hashes/comparison are
retained locally under `.tmp/issue209/`; the source maps are investigation inputs
and are not redistributed in Git.

## Compile time on dense patches (6 November 2026)

The supplied #209 maps were compiled on the Windows x64 debugoptimized build
against the installed retail packages only (no `hidef2kbc72` overlay), with the
main thread's stack sampled every 10-20 seconds. On the first attempt,
`airdefense1` spent over 10 minutes in `PutPrimitivesInAreas` alone, and every
sample was in `AddTriListToArea`. Each fix below exposed the next stage:

| Stage | Cost | Change |
|---|---|---|
| `AddTriListToArea` | Scanned every optimize group in the area for each new triangle, then walked the matching group's list to append. A curve puts each triangle on its own plane, and each plane gets its own group. | Groups are found by key while an entity's primitives are placed, and appended at a remembered tail. |
| `GetHashVert` | The 16x16x16 vertex bins span the whole map; dense patches put most vertexes in a few bins, and each merge scanned its bin. | An index on the snapped coordinate checks the 27 neighbors directly and takes the newest match, which is what the bin scan returned. |
| `FixTriangleAgainstHash` | Each triangle visited every vertex in its bins, one pass over its fragment list per vertex. | Only vertexes that can reach the fragments are visited. A skipped pass only reversed the list, so a parity flag keeps the fragment order exact. |
| `BuildLightShadows`, `CarveGroupsByLight`, `ClipTriList`, `CreateLightShadow` | Searched shadower groups by plane linearly, and appended each fragment by walking the list built so far. | Shadower groups are found by plane; appends use tails (`AppendTriList`). |
| `WriteOutputSurfaces`, `CountUniqueShaders` | Compared every group with every later group to combine surfaces. | Groups are chained to the next compatible group in one pass. The per-light interaction lists this writer built, never wrote and never freed are gone. |
| AAS `idBrushBSP::TryMergeLeafNodes` | Tested every portal plane of one node against every portal point of the other, and dominated AAS builds even on stock maps. | Each plane first tests the bounding box of those points, with a margin, and visits the points only when the box crosses the limit. |

None of these change what is produced. The same build with each change switched
off and on gave byte-identical `.proc` files for `airdefense1`, `core1`,
`medlabs`, `waste`, `process2` and `hub2`, and identical AAS files for
`airdefense1` (all five types), `hangar2`, `core2` and `storage2`; the switches
were for this comparison only. `tools/tests/dmap_tjunction_slivers.py` now also
checks the indexed vertex merge and fix against copies of the original bin scans
on dense random geometry, under Clang ASan+UBSan and MSVC.

| Map | Before | After |
|---|---|---|
| Stock `airdefense1` geometry | 25-28 s | 12-14 s |
| Stock `airdefense1` AAS | 692 s (689 s in `aas32`) | 58 s, identical files |
| Stock `core1`, `medlabs`, `waste`, `process2`, `hub2` geometry | 13-23 s | 10-14 s |
| Supplied `airdefense1` (reported: about two days) | Still in `PutPrimitivesInAreas` after 10 minutes | 138-198 s geometry, 2 s collision, 64 s AAS; 1.18 GB peak working set |
| Supplied `core1` | Not timed | 168 s geometry, 2 s collision, 10 s AAS; 1.75 GB peak |

The ranges come from runs with and without an SP client sharing the machine.
After the last change no stage dominated `core1`'s stack samples.

The rebuilt supplied `airdefense1`, with its own `.map` beside the outputs, loads
in SP gameplay with valid navigation (242 tactical features, no bad spans) and
renders its subdivided terrain correctly in an engine capture; spawned tactical
AI take cover and fire. Whether the reporter's two-day run used the same build,
command and `hidef2kbc72` packages remains unconfirmed, but these maps no longer
take long to compile with the retail packages.

## Collision contents names (6 November 2026)

A `.cm` file names each brush's contents in text, such as `"solid,opaque"`.
Quake 4 1.4.2 converts between names and flags through two tables paired by
index, and its name table has an extra `solid` entry after `largeShotClip`. Every
later name therefore sits one slot from the flag it was meant for. Retail
compiled and read its collision files through that pairing, so in shipped
`.cm` files:

| Name written | Contents it carries |
|---|---|
| `notacticalfeatures` | vehicle clip |
| `vehicleclip` | fly clip |
| `flyclip` | shot clip (`CONTENTS_PROJECTILE`) |
| `shotClip` | item clip |
| `opaque` | opaque and sight clip |

Retail could not write `CONTENTS_NOTACTICALFEATURES` at all. openQ4 decoded these
names by their spelling instead. Across the 960 shipped collision files, 700
vehicle-clip brushes (`convoy1`, `convoy2`, `convoy2b`, `walker`) loaded as
`notacticalfeatures` and stopped clipping vehicles. 117 shot-clip brushes,
including in `q4dm4`, `q4dm6`, `q4ctf5` and `q4ctf7`, loaded as fly clip and let
shots through. 22 item-clip brushes loaded as shot clip, and the `putra`
fly-clip brush as vehicle clip.

openQ4 now reads versions `"2"` and `"3"`, the ones Quake 4 and The Awakening
wrote, through retail's pairing, falling back to openQ4's names only for names
retail never wrote, such as the liquids in openQ4's own `liquid_lab.cm`. Its own
compiler writes version `"4"`, which names each flag directly and so also keeps
`notacticalfeatures`. The generated collision cache payload moved to version 2,
so caches decoded the old way are rebuilt. A custom map compiled by an earlier
openQ4 build whose collision uses vehicle, fly, shot or item clip should be
recompiled; its version `"3"` file now decodes the retail way.

The pairing was read from the name and flag tables in the retail `Quake4.exe`;
the brush counts come from every `.cm` in the installed Quake 4 1.4.2 packages.
Matching it is what lets the AAS tactical pass reproduce retail features on the
convoy maps (see [AAS compiler](aas-compiler.md#verified-against-retail-output)).

## Texture seams between consecutive patches (9 November 2026)

The #209 reporter's rebuilt `walker` showed `textures/tram/road1_1` smeared into
streaks on a ramp in `_area13`. The [supplied `walker.map`](https://github.com/user-attachments/files/34512935/walker.zip)
differs from the stock source only in patch subdivision, and its `road1_1`
brushes and patches are identical to stock. Compiling the stock map reproduces
it: eight triangles of `_area13`'s `road1_1` surface (109,907 square units) have
their texture collapsed along one direction, where the retail `walker.proc` has
none.

The ramp is three flat 3×3 patches laid end to end, each running t from 0 to 1,
so where two meet the same position carries t = 1 on one and t = 0 on the next.
dmap puts coplanar triangles with the same material and texture mapping in one
optimize group, and the optimizer welds a group's coincident vertices to the
first coordinates it meets. id's `TexVecForTri` fitted a curve triangle's mapping
as two normalized directions and an offset that took t along the s direction, so
it could not tell where a repeat starts. Triangles from both patches matched,
and the seam vertices were welded to one value, stretching the texture across
the triangles between them. `noOpt` removes the streaks.

Curve and model triangles are now fitted with their in-plane gradient and
intercept, the form brush sides use, and such a triangle joins a group only when
the group's mapping reproduces each of its texture coordinates to within 0.005.
Brush sides keep id's comparison, and curves still never share a group with
brush sides.

One build compared both rules, the old one behind a temporary switch whose
output was byte-identical to the previous build. The runs in each row ran side
by side on a desktop shared with game clients, so times compare only within a
row. A collapsed triangle has its texture squeezed below 1/50 along one
direction over more than 64 square units, the signature of a welded seam:

| Map | Old grouping | New grouping |
|---|---|---|
| Stock `walker` | 97,769 triangles; 55 collapsed (159,328 sq units), including the 8 streaked `road1_1` triangles; `tr_rail`, `console5b` and `pipecap1_1` texel density 17%, 7% and 1% below retail; 67 s geometry | 96,286 triangles; 12 collapsed (3,987 sq units); every material whose world area matches retail matches its texel density; 40 s |
| Supplied `airdefense1` | 983,934 triangles; 197 collapsed (192,160 sq units); 490 s geometry, 193 s of it shadows | 628,994 triangles; none collapsed; 398 s, 144 s shadows |
| Supplied `core1` | 1,632,526 triangles; 301 collapsed (42,348 sq units); 558 s, 246 s shadows | 1,024,404 triangles; 276 collapsed (30,200 sq units), exactly as in retail's `core1.proc`; 450 s, 176 s shadows |

Fewer triangles come out because a flat patch's triangles now reliably share
one group. Near-zero-area triangles (below 0.0001 square units) rise from 36 to
287 in `airdefense1` and from 121 to 281 in `core1`, almost all on thin railing
strips that now merge; retail's `core1.proc` has 210. Stock `airdefense1`, `putra`, `hub2`, `process2` and `medlabs` compiled
with the new rule show no material whose texel density differs from retail
where the world area matches, apart from one `airdefense1` brush face whose
coordinates in the stock map are near ±9.5 million. Recompiling is what
repairs a map; existing `.proc` files keep their streaks.

`tools/tests/dmap_patch_texture_groups.py` runs the production fit and
comparison on the road seam, brush sides, zero-area and constant mappings, and
4,000 random planes and mappings, including small fragments far from the origin
like a 10×10 subdivided patch. A control built with id's fit must accept the
road seam. Clang, Clang ASan+UBSan and MSVC pass; both GitHub validation
workflows and the local validation runner include it.
