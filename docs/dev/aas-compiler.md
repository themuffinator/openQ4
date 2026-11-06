# AAS Navigation Compiler

openQ4 compiles Quake 4 AAS (Area Awareness System) navigation in the engine.
Recompiling a map's geometry no longer leaves its navigation stale, and map
authors no longer need the Quake 4 SDK tools to rebuild AI pathing.

This document owns the acceptance evidence for that capability.

## What runs, and when

`dmap` writes `.proc` and `.cm`, then rebuilds navigation for every AAS type the
shipped `aas_types` entityDef declares (`aas32`, `aas48`, `aas96`, `aas250`,
`aas128`). `dmap -noAAS <map>` skips the navigation pass; a `.reg` region compile
always skips it, because a partial map cannot produce valid navigation.

Four console commands drive the same code directly:

| Command | Purpose |
|---|---|
| `runAAS [options] <map>` | Compile every AAS type for one map. |
| `runAASDir <folder>` | Compile every AAS type for every `.map` under `maps/<folder>`. |
| `runReach [options] <map>` | Recompute reachability and clusters for existing AAS files. |
| `runAASTactical <map>` | Regenerate the tactical features of the map's existing AAS files. |

`runAAS`/`runReach` accept `-usePatches`, `-writeBrushMap`, `-playerFlood` and
`-noOptimize`. These override the AAS type's shipped settings for that run; they
are debugging switches, not a supported way to ship navigation.

These commands register next to `dmap` rather than inside the optional
`ID_ALLOW_TOOLS` editor block, so every build that can compile a map can also
compile its navigation. The AAS type list is resolved through `declManager`, not
through `gameEdit`, so a dedicated-server build behaves the same as the client
and dmap does not depend on a loaded game module.

## Where the output goes

Compiled output is written beside the engine, not beside the `.map` that was
read, and the `.cm` cache uses a different root again:

| File | Write root | Default on Windows |
|---|---|---|
| `.proc` | `fs_cdpath` | the directory the engine runs from |
| `.aas*` | `fs_cdpath` | the directory the engine runs from |
| `.cm` | `fs_devpath`, falling back to `fs_savepath` when unset | the user save path |

`fs_devpath` is not registered by default, so a stock run puts the `.cm` under
the save path while `.proc`/`.aas*` land next to the executable. Every one of
these writers now prints the resolved absolute path of the file it produced, so
a compile reports where its output actually went instead of leaving authors to
search for it.

## Placeholder files for unused AAS types

Quake 4 writes an AAS file for every declared type even when a map contains
nothing that uses it: the file carries the map's geometry CRC and the type's
settings, with every data section empty. `idAASFile::IsDummyFile()` recognises
that shape, and `idAASLocal::Init()` then treats the type as "this map has no
such navigation by design" and loads silently.

openQ4 reproduces this. Without it, a freshly compiled map would warn about
missing navigation for every unused AAS type on every load.

A type whose build fails outright — a leak, or no valid entity position inside
the world — writes no file at all and reports the failure. dmap warns that
navigation was not fully rebuilt, and any stale file left behind is reported as
out of date the next time the map loads.

## Relationship to the retail compiler

The compiler is the Doom 3 GPL AAS builder (`idAASBuild`, `idAASReach`,
`idAASCluster`, `idBrush`, `idBrushBSP`) adapted to Quake 4's AAS 1.08 file
format and map conventions. The adaptations are:

- `func_group` entities are resolved into world geometry before brushes are
  collected, matching dmap and the runtime map loader.
- The `.proc` reader consumes Quake 4's quoted version and geometry CRC between
  the file id and the first section, and uses the shared binary-capable lexer.
- Areas are cleared before they are filled in, because Quake 4's `aasArea_t`
  carries fields this pass does not produce: `bounds`, `center` and `ceiling`
  are derived by `idAASFileLocal::FinishAreas()` on load, and the runtime
  obstacle marker is not compiled.
- Tactical features are generated after optimization, as described below.
- Placeholder files are written for unused AAS types, as described above.

## Verified against retail output

`game/core2` compiled from the retail `.map` against the retail `.aas` files:

| | Retail `aas32` | openQ4 `aas32` |
|---|---:|---:|
| Map geometry CRC | 3455855974 | 3455855974 |
| Navigation bounds | (-7280 2784 -1216) to (-5392 8064 2144) | identical |
| Areas | 1091 | 1168 |
| Vertices / edges / faces | 1705 / 2335 / 6724 | 1680 / 2302 / 7274 |
| Cluster portals | 3 | 3 |
| Clusters | 5 | 5 |
| Reachabilities | 7566 | 8435 |
| Travel types present | walk, walk-off-ledge, fly | walk, walk-off-ledge, fly |

The CRC and the navigation bounds match exactly; counts land within roughly 8%,
which is the expected spread between the Doom 3 builder and Raven's fork of it.
Every edge, face, area, node, portal, cluster and reachability reference in the
produced files stays within its section, matching the retail files.

The same map's `aas48` is a useful example of correct behaviour that looks wrong
at first glance. With core2's shipped entities, only `monster_makron` and
`monster_makron_legs` declare `use_aas "aas48"`, and the compile yields 15
areas. Retail's shipped `core2.aas48` has 868. Forcing `-playerFlood` — which
seeds the flood from player starts, as the shipped `aas32` settings do —
produces identical bounds to the retail file, (-7272 2792 696) to
(-5400 8056 2144), with 939 areas. The map's geometry CRC covers brushes only,
not entities, so retail's file can predate later entity edits and still validate.
The compiler reproduces the retail result from the retail seeding; it is the
seed set that differs, not the algorithm.

`game/hangar2`, the map reported on #158, compiles all five AAS types in 16
seconds. With the map's matching `.proc` present for the brush-side clip, its
`aas32` lands within about one percent of the retail file on every count:

| | Retail `aas32` | openQ4 `aas32` |
|---|---:|---:|
| Map geometry CRC | 4138459670 | 4138459670 |
| Areas | 1627 | 1616 |
| Vertices / edges / faces | 2875 / 3727 / 9894 | 2883 / 3712 / 9820 |
| Cluster portals | 17 | 17 |
| Clusters | 33 | 14 |

Clustering is where the two builders visibly differ. Retail's file splits the
same 17 cluster portals into 33 clusters, 14 of which hold four areas or fewer;
openQ4 produces 14 larger clusters covering the same 1631 reachable areas.
Clusters are a routing partition, not a reachability limit, so this changes how
the router caches routes rather than where AI can go.

Runtime acceptance: `game/core2` loads all five compiled files with no AAS
warnings. `aas32` and `aas48` load as real navigation, and `aas96`, `aas250` and
`aas128` are recognised as placeholders and dropped silently. A full
`dmap mp/liquid_lab` produces `.proc`, `.cm` and all five AAS files, with the
`aas32` mesh built from the map's player spawns.

## Tactical features

Quake 4's `aas32` settings set `generateTacticalFeatures`, and retail `aas32`
files carry `featureIndex`/`features` sections: cover positions, lean-and-fire
directions, corners and look-over positions that `rvAASTacticalSensor` uses to
choose where AI takes cover and attacks from. openQ4 generates them for every
AAS type whose settings ask for them, after reachability, clustering and
optimization and before the file is written. That is where Raven's compiler ran
them. `runAASTactical <map>` regenerates them on a map's existing AAS files
without rebuilding navigation.

`idAASSettings::FromDict()` now reads `generateTacticalFeatures`, `debugColor`
and `debugDraw` from the AAS type's entityDef, as retail does; previously the
shipped `aas32` request was ignored.

### Placement rules

The rules reproduce the retail generator in Quake 4 1.4.2's `Toolsx86.dll`. Its
behavior was recovered from that binary for compatibility; no Raven source is
included.

1. `info_tactical_feature` entities become features directly. Their `cover`,
   `lean_left`, `lean_right`, `corner_left`, `corner_right`, `pinch` and
   `vantage` keys set the flags, `angle` the facing and `height` (default 72) the
   height; a height inside the 20-60 unit low-wall band adds cover with look-over.
2. Areas are flooded through every reachability, starting from each entity the
   AAS type serves (`idAASSettings::ValidEntity`, player starts when the type
   floods from players).
3. Every edge of a flooded area's floor face that no other floor face of the area
   shares, and that no walk reachability crosses, is a wall edge. Areas flagged
   `AREA_LEDGE` contribute none. Walk-off-ledge drops of 20-60 units into the
   area, whose source lies on the wall side and whose landing point is within 3
   units of the edge, mark that part of the wall as low.
4. Wall edges are chained end to start, and across into walk-reachable
   neighbors by matching end points within 2 units in 2D. Chained edges that stay
   within 3 units of the first edge's line merge into one wall.
5. Each wall is split into segments: low runs measured from its ledge drops, and
   72-unit full-height runs between them. Segment ends move one bounding-box
   radius (R, 16 units for `aas32`) away from taller neighbors, and toward a
   joint with another wall by the sharpness of the turn.
6. A segment at least 1.6R long gets features when it is a corner end (the
   neighboring walls fold back around this wall within short distances and turn
   by more than about 78 degrees), steps down more than 10 units to a neighbor, or
   is under 60 units high. Up to 3R long, or up to 4.5R without a lean, it gets
   one central feature. Longer segments get lean positions R from their ends and
   one to three evenly spaced look-over positions between them.
7. Each feature sits one unit off the wall, inside a reachable area. It needs
   headroom from step height to 48 units without solid or
   `notacticalfeatures` contents. Cover needs the wall to block a 3R trace at a
   third of its height. Look-over needs a clear 7R trace 10 units above it, and a
   lean needs the same clear trace R+5 units to that side. Cover with no
   remaining attack option is dropped.

Two retail behaviors are deliberately not reproduced. Retail indexed features
through a 1024-bucket hash keyed on area number without checking the stored
area, so every area also listed the features of all areas congruent to it modulo
1024. Every shipped `aas32` with more than 1024 areas shows this: `core1` lists
1,573 references to 472 features. openQ4 lists each area's own features, in the
most-recent-first order retail used. Retail also fixed its visited sets at 32,000
areas and edges; openQ4's are sized to the file. Area spans are 16-bit, so a file
needing more than 65,535 index entries stops indexing with a warning.

### Verified against retail output

`runAASTactical` regenerated the features of all 28 shipped `aas32` files that
have navigation (`tram1b` ships a placeholder), on the retail geometry and
reachability, against the retail `.map` sources and shipped `.cm` collision. Each
retail feature was matched to the nearest unused generated feature within 8
units.

| | Count |
|---|---:|
| Retail features | 7,422 |
| Generated features | 7,418 |
| Retail features reproduced | 7,333 (98.8%) |
| ...within 1.5 units | 7,329 |
| ...with identical flags and encoded normal | 7,333 |
| Retail features not reproduced | 89 |
| Generated features retail lacks | 85 |

`core2`, `convoy1`, `convoy2`, `network2` and `storage2` match feature for
feature. Every reproduced feature has retail's exact flags and normal; the
residue is whole features, mostly corners around small obstacles (`mcc_1` has
19 extra), which is where a different entity set in Raven's working map would
change the flood. The geometry CRC covers brushes, not entities, so a shipped
file can predate later entity edits.

Reaching this parity required the collision contents fix recorded in
[dmap compilation](dmap-compilation.md#collision-contents-names-6-november-2026):
before it, the 700 vehicle-clip brushes in the shipped convoy and walker
collision decoded as `notacticalfeatures`, and `convoy2b` lost 29 features.

### Rebuilt maps and AI use

A full `dmap game/airdefense1` from the retail source produced 242 features in
its rebuilt `aas32` (retail ships 228 on its own geometry); the tactical pass
took 7 ms of the 692-second AAS compile. In SP gameplay the rebuilt map loaded
with 242 features, 242 area references and no bad spans in `aiDumpState`, which
also confirms each area lists only its own features (the retail file has 247
references to 228 features).

`rvAITactical` monsters choose cover only from features: `idAI::MoveToCover`
returns when the tactical sensor finds none. Eight `monster_tactical_mgun` were
spawned in front of a god-mode player at the map start and their tactic sampled
four times over 12 seconds:

| AAS data | AI in a cover tactic | Others |
|---|---|---|
| Retail `airdefense1.aas32` | 1 of 8, at 718.7 units from the player | ranged |
| Fully rebuilt by openQ4, generated features | 1 of 8, at 718.7 units | ranged |

The AI took cover at the same distance on both, so the generated features feed
the same decision. The run also exposed that `runAASTactical` wrote its file
with a zero geometry CRC, which the game rejected as out of date; it now writes
the map's CRC, as `runReach` does.

### Loading and malformed data

`idAASFileLocal::Write()` now writes each area's feature span and the two
feature sections, omitted when a file has no features, as retail does. The loader
parses them strictly. A truncated section or a negative count fails the load like
any other malformed section. A feature index entry outside the features, or an
area span past the index, would otherwise be read unchecked by the tactical
sensor, so the file loads with a warning and without tactical features;
navigation is unaffected. Each case was exercised with a modified `core2.aas32`.

Without features `rvAASTacticalSensor` checks `GetNumFeatures()` before every use
and takes no action, so AI still navigates, chases and attacks but picks no
cover or lean positions from the mesh.
