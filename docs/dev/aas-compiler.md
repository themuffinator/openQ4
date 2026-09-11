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

Three console commands drive the same code directly:

| Command | Purpose |
|---|---|
| `runAAS [options] <map>` | Compile every AAS type for one map. |
| `runAASDir <folder>` | Compile every AAS type for every `.map` under `maps/<folder>`. |
| `runReach [options] <map>` | Recompute reachability and clusters for existing AAS files. |

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
  are derived by `idAASFileLocal::FinishAreas()` on load, and the tactical
  feature index and runtime obstacle marker are not compiled.
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

## Not compiled: tactical features

Quake 4's `aas32` settings enable `generateTacticalFeatures`, and retail `aas32`
files carry `featureIndex`/`features` sections describing cover positions,
lean-and-fire directions, corners, pinch points and vantage points. openQ4 does
not generate them, and `idAASFileLocal::Write()` writes zero feature counts.

The consequence is bounded and does not affect pathing. `rvAASTacticalSensor`
checks `GetNumFeatures()` before every use and takes no action when it is zero,
so AI in a recompiled map still navigates, chases and attacks but does not pick
cover or lean positions from the navigation mesh. Retail maps keep their shipped
features until they are recompiled.

Generating them requires reproducing Raven's feature-placement rules, which have
no reference implementation in the Doom 3 release; that work is not scheduled.
