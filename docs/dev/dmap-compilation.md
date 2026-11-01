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
