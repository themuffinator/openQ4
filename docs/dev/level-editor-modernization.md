# Level editor modernization

This is the implementation and evidence reference for the experimental level
editor. The [development roadmap](roadmap.md#engine-loading-safety-and-developer-tools),
TOOL-01 through TOOL-04, is the planning location. Work resumed on
6 November 2026 and closed all four items; milestones 1 and 2 below record the
earlier, initial scope, and milestones 3 to 6 the completion. The user guide is
[level editor](../user/level-editor.md).

Target: a modern, integrated authoring environment at an idStudio-like standard.
This describes the desired product standard, not compatibility with proprietary
idStudio code, project files or tools.

## Audit and first architectural decision

The supported Meson build did not compile `src/tools/radiant/`, define
`ID_ALLOW_TOOLS`, or register the legacy `editor` command. Radiant's windowing,
message loop and widgets depend on Windows/MFC; its views manage WGL contexts
and issue direct OpenGL calls. Simply adding its sources to the build would
not restore a supported editor on SDL3 or across renderer modules.

The source audit also found these defects in the retained, unbuilt Radiant:

- Autosave, Save Copy and region export can clear `mapModified`, suppressing
  the warning for unsaved work in the actual working map.
- `Map_SaveFile` ignores the map writer's result, rotates the original before
  writing, calls `strlen` before checking for null, and borrows a save-dialog
  filename after the dialog's lifetime ends.
- Snapshot allocation probes names such as `level.map.0`, while the writer
  replaces that extension with `.map`; successive snapshots can use the same
  actual destination.
- Map loading frees the active document before parsing the replacement.
- Direct mutable globals couple the document, selection, UI, rendering and undo.

These remain **legacy-source defects**, not fixes shipped by this milestone.
The new implementation is a **separate experiment**: a portable authoring core
and an engine-native graphical workspace share commands and transactions.
`editorExperimental` opens it, while `mapEdit` exposes the same document service.
The legacy Radiant source, startup dispatch and `editor` registration remain
unchanged. Restoring its existing tool build is outside this milestone.
The experiment has separate GUI assets, `editorExperimental_*` settings and
an `editor_experimental/` directory for recovery files, view state and the
build sandbox.

## Milestone 1: document and persistence service

Implemented in `src/tools/leveleditor/`, with engine integration in
`src/framework/LevelEditor.cpp`:

- A portable C++ document with stable entity handles. Opening a new document
  invalidates old handles; deleting and undoing an entity restores its handle.
- Lossless source ownership for map versions 1–3: comments, whitespace, line
  endings, BOMs, primitive text and compiler directives survive unchanged.
  Authoring never runs runtime `Resolve`, `.ent` replacement or `.entx` merging.
- Entity properties and point-entity creation/deletion use undoable text edits.
  Ordinary property history retains changed spans instead of copying world
  geometry. Validated primitive ranges avoid rescanning geometry after ordinary
  property edits. Undo/redo tracks the saved revision, including branches after undo.
- New, Open and Close refuse to replace dirty work without explicit discard.
  A failed parse leaves the current document and history intact.
- Saving stages complete bytes, checks write and sync, then uses the engine's
  atomic file promotion. A changed existing source gets a `.map.bak` first.
  Failed publication keeps the dirty state and previous destination.
- Source-byte conflict detection refuses to overwrite a file changed by another
  editor. Save As and Save Copy refuse unrelated existing destinations.
- Recovery writes are separate from ordinary saves, do not clear dirty state,
  and skip unchanged revisions. Recovery opens as an unnamed dirty document,
  requiring a fresh destination to protect newer external work.
- The command adapter is available in client and dedicated builds and makes no
  graphics or input-capture calls. Periodic recovery is driven by the normal
  engine frame; normal shutdown attempts a final recovery before teardown.

Limits are deliberate: 64 MiB source, 65,536 entities, 16,384 properties per
entity, 256 history entries and 16 MiB history. An edit too large for undo is
rejected before mutation. Structural parsing validates document boundaries and
balanced primitive delimiters; it does **not** validate convex geometry, material
existence, patch dimensions, entity definitions or gameplay references. Those
remain compiler/validator responsibilities. Changing a geometry entity's origin
through a raw property stays blocked; the transform tools move it together with
the geometry (milestone 3).

Milestones 3 and 5 added brush/patch editing, transforms, rename/reference
repair, prefabs and the build manager with its play sandbox. Multi-document
workspaces remain out of scope. Entity handles are session-local and are not
serialized into stock maps. History is memory-only. Current source limits and conservative filename rules are initial
bounds, not claims of unlimited production-map support. File conflict checks
are optimistic; they do not acquire an interprocess lock against external writers.

## Milestone 2: initial graphical source workspace

`editorExperimental` opens a stock-art, localized workspace with a searchable entity/map/class
browser, property inspector, point-entity creation/deletion, save/copy, undo/redo
and an unsaved-work confirmation. Browser/inspector widths are resizable and
archived per user; layout changes preserve uncommitted inspector fields.
Confirmation dialogs block underlying controls and focus Cancel. A confirmed
replacement still reads and validates the next document before discarding the
current source or undo history. Console saves update the graphical dirty/path
state even when the source revision does not change.

The native GUI list now accepts a bounded `maxitems` setting. Existing GUIs keep
their 1,024-item default; the editor can expose all 65,536 supported entities.
Entity-definition browsing reads names without parsing every definition or
loading its models and textures.

The renderer-independent preview constructs brush windings and bounded patch
tessellation from source primitives. It owns a separate render world and its
models, without registering or replacing the game's global `_areaN` models.
It never runs authoring source through runtime entity replacement/Resolve.
Point entities use colored markers. Picking, orbit, pan, zoom and framing use
the document's stable entity handles; selection updates only the old/new render
entities. This preview uses a camera light, not compiled map lighting.

Preview updates compare exact primitive bytes and reuse unchanged models.
Property edits, undo and redo do not retessellate the map; point-origin changes
update the render entity. Flat patch rows/columns are collapsed. Discrete
materials retain separate surfaces so flare and sprite deformation is not
applied to merged geometry. Preview input is bounded (2 million source vertices,
128 sides per brush, odd patch dimensions 3–65 and bounded subdivisions).
Unsupported or incomplete primitives remain losslessly stored, are counted as
skipped in the preview, and do not terminate editing.

This work also fixed a pre-existing `Lexer` memory-constructor defect: its file
pointer was uninitialized, so destruction could attempt to close an invalid
file. The constructor now uses the normal initialization path, and text errors
honor the delegated lexer's nonfatal policy and error state.

Milestone 4 replaced this initial workspace: its fixed panes became docked
panels, its single preview became 3D, top, front and side views, and its
synchronous first build became a time-sliced, cached one.

## Milestone 3: geometry and asset authoring (TOOL-02)

The portable core in `src/tools/leveleditor/` gained typed geometry adapters
and the editing commands built on them. It still has no dependency on the
engine, so the native suites compile it alone.

- **Lossless adapters** (`MapGeometry`). `Primitive::Parse` reads `brushDef3`,
  `patchDef2` and `patchDef3` and records the byte span of every number.
  `Serialize` rewrites only numbers whose value changed, with the shortest
  fixed-point spelling of the same float (the map lexer reads no exponents),
  and keeps comments, spacing, number spelling and material text. A changed
  value whose float is unchanged keeps its original spelling. `brushDef`,
  `brushDef2` and Quake 3 brushes stay opaque: preserved, reported, and refused
  by transforms, so a mixed transform fails before changing anything.
- **Coordinates follow `idMapFile`.** `brushDef3` planes are stored relative to
  the entity's `origin`; patch control points are stored in world space (the
  loader subtracts the origin). Moving an entity with an origin therefore keeps
  its brush planes, moves its patches, and updates `origin`.
- **Transforms and texture lock.** Planes move by the inverse transpose; faces
  keep their outward orientation, and mirroring is refused because it would
  invert faces. Texture lock solves each row of the 2x3 texture matrix from
  three points on the face, in the new face's `ComputeAxisBase` frame (mirrored
  exactly, including its float cleaning), so every corner keeps its texture
  coordinate; the shift stays within half a repeat of the original. Without
  lock the texture stays in world space, as Radiant does. Exact quarter turns
  keep axial planes exact; four locked quarter turns restore planes and
  matrices. Point entities turn `angle`, or `rotation` for other axes,
  `angles` about Z, and a light's projection vectors; scaling scales an
  axis-aligned light's radius.
- **Grouped undo** (`MapDocument`). `BeginTransaction`/`CommitTransaction` make
  any number of edits one undo step; `RevertTransaction` discards the pending
  edits but keeps the transaction open, so a drag re-applies its total offset
  on every pointer move without drift; `CancelTransaction` restores the exact
  revision, so a cancelled gesture leaves a clean document clean. Undo and
  redo refuse to run inside an open transaction. Primitive edits
  (`SetPrimitive`, `SetPrimitives`, `AddPrimitive`, `RemovePrimitive`) and
  `InsertEntity` validate balanced source blocks and keep the cached primitive
  ranges in step with a full re-parse. `SetPrimitives` rewrites many primitives
  of one entity in one linear pass and records only their spans: rotating
  5,000 brushes of a 20,000-brush map is one 146 ms step with a compact undo
  record (one rewrite per brush would copy the 10 MB worldspawn 5,000 times).
- **Editing tools** (`MapEditing`). Mixed selections of point entities,
  geometry entities and individual primitives transform together;
  `Snap`, box brush and flat patch creation in world coordinates (converted to
  entity space), `Duplicate` (copies keep their links to one another, with
  fresh numbered names), `Delete`, `SetMaterial`, `Group`/`Ungroup`
  (`func_group`, which `idMapFile::Resolve` merges into worldspawn for compiles,
  re-expressing origins when needed), `SetLayer` (the `editor_layer` key; world
  primitives join a new layered group), `References` and `Rename` (updates
  `target*`, `bind`, `killtarget`, `cameraTarget`, `enemy`, `focus` and
  entityDef-declared entity keys in one step), and prefabs (`ExportPrefab`
  centres the selection on its base; `InsertPrefab` renames colliding names
  inside the prefab first, so its own references follow). Every command
  validates its whole result before its first change.
- **Geometry cache and picking.** `MapGeometryCache` keeps parsed primitives,
  face polygons and bounds keyed by exact source bytes and entity origin;
  views and picking read it without re-lexing the map. Rays hit brushes as
  exact convex intersections of their outward planes, patches by their control
  net and point entities as 16-unit boxes.
- **Asset index** (`EditorState`). `AssetIndex` catalogues entityDef, material,
  sound, model and map names from pluggable sources a bounded number of items
  per step, de-duplicates without case, stops at 262,144 names or 16 MiB, cuts
  off sources that repeat themselves, and can be cancelled. The engine steps it
  for at most `editorExperimental_indexBudget` (2 ms) per frame and lists each
  file tree once, on its first poll.

## Milestone 4: the docked workspace (TOOL-01)

The workspace GUI is generated by `tools/ui/build_level_editor_gui.py`
(`--check` verifies the committed file). It keeps Quake 4's stock menu art and
every label is a `#str` entry in all thirteen openQ4 language tables.

- **Docking.** `DockLayout` places the Browser, Inspector, Tools and Problems
  panels left, right, under the views (Problems only; the others need a full
  column) or hidden. Panels sharing a dock become tabs; only the active tab
  carries the dock controls, so shared docks keep room for titles. Docks
  resize between 130-230 (sides) and 70-180 (bottom) GUI units and the layout
  persists in `editorExperimental_layout`. Panels are GUI containers whose
  rectangles the engine publishes as `gui::ed_<panel>_*` state, so one set of
  windows works in every dock.
- **Views.** The 3D preview plus top, front and side orthographic views, alone
  or as a quad. The orthographic views are 2D wireframes drawn through the GUI
  device context in batched quads, so both renderer backends draw them the
  same way and no renderer projection change was needed. They draw a grid that
  stays at least 6 px apart with every eighth line brighter, world axes,
  bounds boxes for primitives under 5 px and nothing below 0.75 px, and stop
  outlining at 300,000 lines. Selections are orange; in 3D their primitives
  are drawn as a projected wireframe over the rendered view. Line quads are
  emitted with both windings because GUI surfaces are back-face culled.
- **Gestures.** One controller (`LevelEditor_View*`) serves the GUI widget and
  the scripted `editorExperimentalWorkspace view` commands: click selection
  (Shift toggles), drag-move on the grid as one transaction, Ctrl-drag brush
  creation, pan, orbit, cursor-anchored zoom, arrow-key nudges and Delete.
  Hidden and locked layers are excluded from picking. A pick combines the
  geometry cache's ray with the preview's model traces for entity models.
- **Camera and view persistence.** `ViewState` stores the 3D camera, the
  orthographic centre and scale, view mode, quad flag, grid, texture lock,
  hidden and locked layers and the selection (as document indexes, since
  handles are session-local) in `editor_experimental/views/<map>.map.view`,
  written atomically on save, on leaving the workspace and before a play test.
- **Richer previews.** Point entities show their static entity models, taken
  from their `model` key or their entityDef. The entityDef lookup and the model
  load happen in `UpgradeModels` within `editorExperimental_modelBudget`
  (3 ms) per frame; skinned models keep their marker. 296 of airdefense1's
  point entities show models.
- **Chunked, cached preview.** Each entity's primitives form chunks of 128,
  each its own render entity, and every primitive's tessellation is cached by
  its source bytes, origin and map version with reference counts. Opening a map
  tessellates within a 12 ms budget per frame (the old single-pass build took
  4.7-5.4 s for airdefense1's worldspawn alone, so one-entity slicing could not
  help); a world brush edit re-tessellates that brush and reassembles one chunk.
- **Typed inspector and linked diagnostics.** `MapValidator` reads entityDef
  `editor_var`, `editor_bool`, `editor_int`, `editor_float`, `editor_vector`,
  `editor_color`, `editor_model`, `editor_mat`/`editor_material`, `editor_snd`
  and `editor_gui` declarations plus well-known keys. The inspector shows each
  property's type, the class usage and the key's description, and refuses an
  ill-typed value (console edits stay raw). Validation reports duplicate names,
  missing targets, unknown classes, ill-typed values, malformed origins,
  missing assets once the index is complete (brush entities naming their own
  inline model are not missing), unreadable, degenerate and legacy primitives,
  and a missing SP player start, each linked to its entity or primitive.
  Selecting a problem selects and frames its source.
- **External edits.** `MapWorkspace::ExternalChange` compares the file with the
  bytes last read or saved. The session checks every
  `editorExperimental_externalCheckSeconds` (3 s), reports a change once, and
  `mapEdit reload [discard]` reopens the file.
- **Label fit.** `editorExperimentalWorkspace fit` measures every visible
  single-line label with its font, scale and spacing against its rectangle, and
  every wrapped label's lines against its height, skipping data bound to
  `gui::` state, edit fields and lists.

## Milestone 5: build and play (TOOL-03)

`MapBuild` runs the compile in a child engine process through the SDL3 process
API (Windows, Linux and macOS), preferring the dedicated server beside the
client so no window opens.

- The child gets the build sandbox (`editor_experimental/build`) as its whole
  save path, holding only the saved map, and `com_compilerOutput fs_savepath`
  so `dmap` and AAS write `.proc` and `.aas*` there instead of beside the
  engine (`fs_cdpath`, still the default). `.cm` already follows the save
  path. The editor tails the child's `logFile` for progress (stage markers from
  `LoadDMapFile` to `seconds to create AAS`, monotonic) and diagnostics.
- `CompileIndex` maps the compiler's entity and primitive numbers back to the
  document through `idMapFile::Resolve`'s renumbering: `func_group` primitives
  join worldspawn after its own, and `removeEntities` classes vanish. Warnings
  such as `brush primitive 2 on entity 0 is degenerate` therefore select the
  exact brush, even inside a group.
- Success requires exit code 0, the geometry pass, no leak and a `.proc`. Only
  then are all outputs staged beside the saved map, the previous outputs
  (including ones this build no longer produces) set aside, and the new ones
  published; any failure restores the previous set. A leak, a compiler error,
  a timeout (`editorExperimental_buildTimeout`), a cancel or a child that goes
  silent for 15 s after an error leaves the previous outputs untouched.
- **Play** launches the client with the sandbox as its whole profile: its
  configuration, saves, logs and caches stay there. SP maps use `devmap`; maps
  under `mp/` use `spawnServer`. The editor process keeps its document,
  selection and cameras, saves its view state first, and notes when the test
  ends. `editorExperimental_onBuildDone` and `editorExperimental_onPlayDone`
  run a console command at those transitions, for automation.

The harness exposed two engine defects, both fixed. `dmap` wrote its leak
trail with `fopen` beside the engine and raised a fatal error when the map's
folder did not exist there, so any leak in a new folder aborted the compile; it
now uses the filesystem writer (creating the folder, following
`com_compilerOutput`) and only warns if it cannot write. After that fatal
error the dedicated server also stopped responding in shutdown, which the
build manager now detects and ends.

## Milestone 6: qualification (TOOL-04)

Latency budgets are declared in the session and reported by `mapEdit latency`
for the stock `airdefense1` source (2,195 entities, 11,793 primitives):

| Measure | Budget | Meaning |
| --- | --- | --- |
| startup | 6,000 ms | open to the first rendered workspace frame |
| edit | 100 ms | applying a transform or property edit to the document |
| refresh | 100 ms | from a document change to the workspace showing it |
| select | 16 ms | a pick or browser selection |
| view3d, ortho | 16 ms | drawing one 3D or orthographic view |
| validate | 250 ms | a full validation pass |

## Qualification evidence (6 November 2026)

The workspace phase ran on commit `33bab5d93`; the build, fit and stock phases
ran on `267d39920`, which differs only by the orthographic view cache. Both
were built `debugoptimized` with MSVC and staged into `.install/`, used the stock Steam Quake 4 1.4.2 assets, isolated save paths
under `.tmp/` and no keyboard or mouse input. `level_editor_authoring.py`
checks that the staged runtime is unchanged by each phase.

**Native suites.** `openq4-editor-authoring` passes 458 checks with the 79
retail `.map` sources from the installed PK4s: all 543,372 primitives (345,655
brushes, 197,717 patches) parse and serialize byte for byte, and every valid
one survives a locked translation and its inverse. Five retail primitives are
invalid and are reported rather than moved (`q4cmp2` primitives 445, 448 and
449 have all-zero planes; `q4cmp6` 506 and `q4dm3` 1250 enclose no volume).
Rotating 5,000 brushes and 1,000 entities of a 20,000-brush map takes
146-227 ms as one undo step; 2,000 randomized edits, gestures and undo/redo
return to the original bytes. The suite covers texture lock, transactions,
mixed selections, every tool, validation, compile numbering, build promotion
with injected failures, layouts, view state, the asset index and external
edits. `openq4-editor-document` still passes its 2,938 checks. On Linux (WSL,
Ubuntu 24.04) both pass under GCC 13 and Clang 18 with AddressSanitizer and
UndefinedBehaviorSanitizer, without warnings.

**Workspace (`--phase workspace`, OpenGL and Vulkan).** On `airdefense1` the
phase drives the typed inspector (rejecting `noshadows "yes"`), an ortho
drag-move that undoes exactly as one step, Ctrl-drag brush creation, a
locked turn and snap, duplicate, group, layer hiding, a reference-repairing
rename (all four `target` references follow the new name), prefab save and insert, validation with a problem linked
to its source, docking moves the two-panel limit refuses, engine events into
the workspace GUI (a 3D click selection, wheel zoom and a splitter drag),
view-state persistence across a client restart, detection of an external
edit, and a dirty document that survives a renderer switch and a resize to
1600x900; it ends in SP gameplay of the saved map. Measured on that map:

| Measure | Budget | OpenGL | Vulkan |
| --- | --- | --- | --- |
| startup (worst) | 6,000 ms | 2,053 ms | 883 ms |
| edit (worst) | 100 ms | 1.8 ms | 4.2 ms |
| select (worst) | 16 ms | 1.4 ms | 4.5 ms |
| view3d (95th percentile) | 16 ms | 6.0 ms (worst 277 ms) | 5.5 ms (worst 468 ms) |
| ortho (95th percentile) | 16 ms | 4.4 ms (worst 8.1 ms) | 5.5 ms (worst 8.8 ms) |
| validate (worst) | 250 ms | 56 ms | 127 ms |

The worst 3D frames are the first after the renderer switch and restart, which
load textures. A full refresh after an edit measured 21.8 ms in an earlier run;
the final run's edits fell while the preview was still streaming in and were
not sampled. A concurrent Linux build once pushed the ortho 95th percentile to
47 ms before the view cache, which now redraws only on change.

**Localized fit (`--phase fit`).** Every visible workspace label measured in
all 12 languages at 1280x720, 1920x1080 and 2538x1312 (the largest window the
desktop allows), in the default and an alternate layout, with no overflow
(72 measurements of 57 to 59 labels each checks). The first runs found wrapping labels in nine languages,
which led to two-panel docks, two-line tabs, weighted toolbar widths and
shorter Russian, Ukrainian, Turkish, Hungarian and Polish strings.

**Build and play (`--phase build`).** A map authored only with `mapEdit`
commands (a room of six brushes, a pillar, a player start, a light and two
Strogg marines) builds in the sandboxed dedicated server: seven outputs
(`.proc`, `.cm`, five `.aas*`), with the `aas32` file holding 5
areas and 8 reachabilities. The isolated play client loads it, and
the marines walk about 200 and 150 units around the pillar on the rebuilt
navigation. The editor's workspace state is identical before and after the
play test, and the play client wrote only into the sandbox. Removing the
ceiling makes the next build fail as a leak, and a cancelled build stops
after 1.5 s; both leave the seven published files byte for byte unchanged.

**Stock round trip (`--phase stock`).** `airdefense1` is opened, edited, saved
and rebuilt in 51 s of geometry, 1 s of collision and 183 s of navigation, then played: the rebuilt `aas32` has
1,170 areas and 242 tactical features with no bad
feature spans, eight spawned `monster_tactical_mgun` use it, and one takes
cover, as on retail data in [CORE-06](aas-compiler.md#rebuilt-maps-and-ai-use).

**Restart labels.** The 24 September defect (labels vanishing after a full
`vid_restart`) does not reproduce: with the old editor from the current
`main` package and with the new workspace, on OpenGL and Vulkan, a resize to
1600x900 and restart leaves no label pixel changed between the settled
restart capture and a reopened workspace. Its cause in the old build was not
identified; the fit check now runs after every restart in the workspace phase.

**Linux.** On Ubuntu 24.04 under WSL, a GCC 13 debug build of the editor
sources of `267d39920` passes both editor suites under Meson, and the build
phase passes under Xvfb with Mesa's llvmpipe. The Linux dedicated server compiles
the authored map (seven outputs, 5 areas, 8 reachabilities), the isolated Linux
play client loads it, the marines walk about 220 and 175 units around the
pillar, and the leaking and cancelled builds leave the outputs unchanged. The runtime ran with its
retail packs and save paths in RAM, because the WSL disk image lives on a
nearly full system drive. The first Linux build showed that the dedicated
server, built without SDL3, could not use SDL's process API; it now starts the
child with `posix_spawn`.

**macOS** is a preview platform without project hardware and is qualified
through the [macOS evidence gate](macos-signoff-evidence.md); the editor uses
only SDL3, the GUI and the filesystem layer there.

**Remaining owner check.** Engine events exercise the workspace's own input
handling, not a physical pointer or keyboard. A hands-on pass (click, drag,
wheel, arrow keys and Delete in each view; dragging splitters; typing in the
fields) is the owner's, as for the other tracks' human checks.

## Earlier validation (24 September 2026)

This records the initial milestones' qualification; the old
`level_editor_workspace.py` harness it names was replaced by
`level_editor_authoring.py`, and the restart label defect it records no longer
reproduces (see milestone 6).


`openq4-editor-document` compiles the production portable document and workspace
with deterministic fault-injected storage. It covers byte-exact round trips,
stable handles, history eviction, randomized edits, dirty/savepoint semantics,
malformed replacement loads, invalid paths, partial-write/sync failures, backup
and promotion failures, external-change conflicts, save copies and recovery.
Additional `.map` arguments to its executable verify lossless stock-map load,
world-property edit and undo.

`tools/tests/level_editor_runtime.py` runs the staged client with stock SP assets,
an isolated save/development root and disabled mouse/controller input. It edits
and saves `game/airdefense1`, checks the original/copy/undo/backup/recovery bytes,
enters that saved map and requests an engine screenshot. It neither sends input
events nor captures the desktop. It also rejects a malformed numeric entity
handle and verifies that the failed edit leaves the source unchanged. See the
[user guide](../user/level-editor.md)
for command usage.

Local qualification on 24 September 2026:

- Full Windows x64 build and staging passed. The Meson native target passed
  2,928 assertions for the initial document milestone; a separate Clang build
  and Linux GCC ASan/UBSan build passed.
- All 79 distinct map paths found in the installed PK4s passed byte-exact
  load/serialize, world-property edit and undo checks (3,323 assertions including
  the core suite). These cover source documents, not full-map gameplay parity.
- The optimized standalone Windows build measured a median 1.071 ms and maximum
  6.503 ms for one world-property edit across those maps. This is a core-operation
  measurement on this machine, not a viewport or end-to-end editor latency claim.
- The staged client passed the authoring/save/recovery sequence and subsequent
  `game/airdefense1` SP gameplay on OpenGL and Vulkan. Both produced engine
  captures, returned exit code zero, and logged no `WARNING:` or `ERROR:` lines.
  The tested source has 2,195 entities and 11,793 primitives.
- Initial visual inspection of the Vulkan capture found an **inverted 3D scene with an
  upright HUD** in the currently modified renderer checkout. The OpenGL capture
  is correctly oriented. Those initial results qualified document/persistence
  commands and gameplay entry only. Subsequent workspace captures use the
  concurrent renderer fixes and are upright. No renderer source was changed
  for this editor work.
- Logs, launch arguments, output hashes and captures are retained under
  `.tmp/editor-modernization/final-gl/` and `final-vk/`; native and stock
  round-trip logs are in the parent directory. Generated caches, extracted stock
  sources and test map outputs are also retained: automatic approval review
  rejected the attempted recursive cleanup with the reason "blocked by policy".

An unrelated loose `q4base/maps/game/airdefense1.map` in the retail installation
differs from the packaged source: `func_static_53232` has origin Z `36.125` instead
of `36`. It was left untouched. The final runtime fixture extracts the original
PK4 source into its isolated profile so this override cannot skew the check.
Earlier harness attempts stopped at the loading-continue screen, or used an
unquoted screenshot path; the final fixture uses the established after-map
script/auto-continue options and a quoted engine screenshot command.

### Graphical workspace qualification

`tools/tests/level_editor_workspace.py` drives the same semantic controller
actions as the GUI widgets. It does not inject input events. On 24 September
2026, `.tmp/editor-workspace/closeout-gl/` and `closeout-vk/` passed the
automated controller/document checks below. Visual qualification is partial,
with the restart defect recorded separately:

- Full Windows build/staging and unchanged runtime manifests during each run.
- All 2,195 stock entities are exposed by the native list. The preview contains
  2,097 render entities and 142,982 triangles with no skipped stock primitives.
  Removing redundant patch subdivisions reduced the earlier 363,142 triangles.
- Framed-marker picking, orbit/pan controller calls, saved pane limits and
  preservation of draft inspector fields during resizing.
- Property edit/undo/redo reuse all preview models. The final concurrent runs
  recorded 24–37 ms for these preview updates, versus initial builds of 2.63–4.45
  seconds. Earlier isolated runs recorded 10 ms updates. These measure preview
  synchronization only, not input-to-frame latency or a production performance
  guarantee; initial-load work remains synchronous.
- Invalid geometry and overflowing texture coordinates are skipped without
  ending the session. A confirmed attempt to open an invalid replacement leaves
  the dirty source intact. The native suite also checks retained undo/savepoints.
- Lossless undo, saved edits, copy/backup bytes, cancellation of unsaved-work
  replacement, protected modal actions, renderer restart and resize to 1600x900.
- Saved-map SP gameplay, reopening the workspace while that game remains loaded,
  and return to its menu. Each run writes nine engine screenshots, including
  settled menu, immediate/settled restart and workspace reactivation views.
- Runtime command listings verify `editorExperimental` is separate and has not
  replaced `editor`. The unchanged Radiant launcher/source check is recorded in
  `.tmp/editor-workspace/legacy-preservation.json`. The final document workflow
  in `closeout-document/` also passes save/copy/backup/recovery and SP gameplay
  with no warnings or errors. Recovery uses the portable lowercase directory
  `editor_experimental/recovery/`.
- The standalone Windows and Linux GCC ASan/UBSan suites pass 2,938 assertions.
  Language-table encoding and the existing native menu-control contract pass.

The harness reports the known retail menu warning for the absent
`models/monsters/burn_misc_sm` image separately; all other unexpected warnings,
errors or rejected controller actions fail. The malformed-primitive and invalid
replacement diagnostics are intentionally asserted. The OpenGL/Vulkan workspace
captures are upright. This does not establish all-material renderer parity.

Final visual review found a remaining **full-video-restart label defect** on
both renderers: some workspace labels disappear and stay missing after 30 more
frames. Reactivating the workspace restored them on Vulkan but did not reliably
restore them on OpenGL. This is not qualified as fixed; the initial and resumed
gameplay workflows still pass. The English Save copy label fits, and the new
experimental badge is visible before restart. The broader work is paused;
diagnosis of the restart-specific text failure remains a follow-up item.

Earlier failed runs, the lexer-crash dump/stack, and build logs remain in
`.tmp/editor-workspace/`. Native pointer/keyboard interaction was not automated:
the project requires specific input-control permission, which was not granted.
Only controller calls and engine screenshots were used. No OS capture was taken.
Automatic approval review also rejected cleanup of this area's generated test
caches with "blocked by policy"; no alternative deletion was attempted.
