# idtech5-ui: complete interface replacement

Started 8 September 2026. Status: active; inventory qualification and runtime integration underway. This is a
development branch, not a released UI feature. The [retained runtime checkpoint](../ui/runtime-spike.md)
records the stage 2 integration candidate and its remaining limitations.

The [product completion review and plan](ui-product-completion.md) audits the
implementation at `80bb3939` and defines the next delivery milestones, concrete
runtime/editor/corpus requirements and final quality/performance gates. It refines
delivery order without reducing the complete scope below. A representative screen
is an intermediate milestone; full implementation must deliver the complete,
high-quality UI product at an id Tech 5 standard.

The [product requirement register](../ui/product-requirements.md) tracks the
full normative scope, milestone ownership and acceptance evidence.

## Objective and immutable completion scope

Plan, implement and publish a complete idTech 5-esque replacement for openQ4's
GUI system on branch `idtech5-ui`. Commit and push after each completed stage.
Deliver DPI-aware, aspect-expanding, high-definition Quake 4 interfaces,
quality vector furniture, continuous transitions and an extensive visual
editor. Replace every current GUI. Use bitmaps only for complex imagery.

The normative [visual design specification](../ui-visual-design.md) precedes
screen/vector/editor design. Preserve the complete objective when stages need
more than one work session. Neither conversion counts nor a successful main
menu launch establishes completion. Temporary coexistence is a migration tool,
not the final architecture.

## Branch and source ownership

- Engine branch: `idtech5-ui`, initially based on `23847b22` from the current
  `android-gles` checkout so its Android/GLES integration is retained.
- Companion branch: `idtech5-ui`, initially based on `f9bf8a692de539b9d149cc7b2c47759dbb1ddd62`
  from its current `android-gles` checkout. Record paired revisions in reports.
- New game/UI bridge edits are canonical in `openQ4-game`; no `src/game` mirror.
- Runtime-authored sources live under `content/baseoq4/`; engine defaults that
  need no user editing can be compiled resources. Build outputs belong in
  `builddir/`; runtime installation remains `.install/` and `.install/baseoq4/`.
- Push each completed stage to the corresponding remote branch, verify remote
  heads, and retain evidence. Do not merge the branch or publish a release
  before the final qualification stage.

## Architecture decision

Adopt an idTech 5-style retained document/component and timeline architecture,
with a modern density/layout model and an editable vector source pipeline.
Use RmlUi as the initial layout/text/input integration candidate, with native
engine vector components and a renderer adapter. Its standard SVG bitmap
plugin is not sufficient for the required vector furniture. The dependency
selection is gated on the stage 2 spike; if RmlUi prevents faithful vector
composition or editor round trips, record the evidence and revise the
implementation choice without narrowing the visual/functional requirements.

Do not port an entire Flash VM simply to obtain paths and transitions. The
GPL-released Doom 3 BFG/RBDOOM SWF systems are useful references for retained
display lists, masks, state and timeline editing. They are not proof of
automatic DPI awareness or high-refresh animation. Current reference sources:

- [Doom 3 BFG](https://github.com/id-Software/DOOM-3-BFG): GPLv3 with additional
  notices; retail data excluded from its source licence.
- [RBDOOM](https://github.com/RobertBeckebans/RBDOOM-3-BFG): SWF/JSON tooling and
  maintained id-family rendering reference.
- [RmlUi](https://github.com/mikke89/RmlUi): MIT layout/input library;
  [render integration](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render.html),
  [density units](https://mikke89.github.io/RmlUiDoc/pages/rcss/syntax.html),
  [SVG bitmap behavior](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/svg.html).
- [SDL3 DPI model](https://wiki.libsdl.org/SDL3/README-highdpi): distinct window,
  drawable, density and content-scale values.

No external implementation code is incorporated in the specification stage.
Before integrating a library, pin an exact release/commit, inspect its licence
and transitive dependency licences, preserve notices and add README credits.
Use Meson subprojects; do not add an independent build/package system.

### Document and renderer boundaries

Canonical UI sources need stable document/node/component IDs, typed properties,
constraints, vector paths, tokens, state variants, bindings and timelines. The
editor must losslessly round-trip these sources and explicit extension metadata.
Choose the serialization during stage 2 and version it before translating
assets. If layout markup is generated, it is derived from this model and must
never become a competing source of truth.

The runtime translates documents into ordered draw operations for triangles,
strokes, gradients, text, pictorial materials, clips, transforms and composition
layers. OpenGL, Vulkan and GLES consume equivalent operations. Vector flattening
uses output-space error bounds; cache keys include relevant scale and transforms.
Keep generated geometry out of fixed-size texture atlases. Font atlases may
remain generated coverage textures with density-aware cache invalidation.

The renderer owns graphics resources and thread synchronization. The UI model
owns layout, state, input and animation. The editor owns source mutations and
history. Game libraries communicate through semantic state/events/commands,
not renderer internals or implementation-specific node pointers.

### Behavior compatibility

Translate geometry and behavior. Preserve GUI dictionary values, expressions,
named events, timelines, commands, script-driven visibility, material stages,
text escapes, localization, sound routing and world-GUI instances. Preserve
save/restore semantics and define a migration/version policy before cutover.

The transitional `idUserInterface` now exposes presentation value and focused
text-field operations in place of `GetDesktop()`/`idWindow`. The coordinated
[presentation boundary](../ui/presentation-bridge.md) removes direct session,
SDL and SP/MP player accesses. Retained alias mapping and complete semantic
state/action dispatch remain to be implemented; removing pointers alone does
not establish replacement parity.

Inventory special window types and implement functional replacements, including
render/model previews and any used custom game/instrument widgets. Unsupported
syntax, missing includes or unresolved materials must produce explicit
diagnostics and keep a resource unqualified, never silently disappear.

### Input and application ownership

Use SDL3 platform events, localized text input/composition, navigation actions
and projected world-GUI hit coordinates. Window/drawable/layout transforms must
be composed once and inverted for hit testing. Menus have explicit focus,
modal and activation lifetimes. Press/release pairing prevents click-through.
Touch and gamepad interaction share semantic controls without requiring a
synthetic pointer for every operation.

For automated validation, use engine-internal named events/state fixtures and
engine screenshot commands. Do not inject or capture OS keyboard/mouse input.
Interactive manual manipulation requires specific user permission. No game
test may launch fullscreen without permission for that exact test.

### Editor ownership

Provide a desktop editor integrated with the engine UI renderer and runtime.
It must expose hierarchy, precise layout and vector manipulation, components,
variants, bindings, state preview, timeline/easing editing, localization,
undo/redo, atomic save, autosave/recovery, source diagnostics and output presets.
See the visual specification for detailed acceptance requirements. Preview
and the game share evaluators/renderers; a separately styled web preview is
insufficient. Runtime packages must not acquire editor-only linker artifacts.

## Stages and exit evidence

Stages are ordered dependencies, not estimates. Each needs a scoped review,
appropriate tests, documentation and pushed commits. Split work within a stage
when useful, but mark the stage complete only when its exit evidence exists.

### Stage 0 — specification and delivery plan

- [x] Create `idtech5-ui` branches in engine and companion repositories.
- [x] Write extensive normative appearance, density, layout, state, motion,
  vector, family, editor and qualification requirements before visual authoring.
- [x] Retain old GUI conventions as a clearly historical translation reference.
- [x] Record complete scope, dependency decision, stages and completion audit.
- [x] Review links/status, commit both repositories and verify pushed heads:
  engine `7a0b8153897d37feeaab0cd883697e245f727500`, companion
  `9a2d8715fc40d4f5b1ca1cfd7f90230baf3192f2`.

Exit: published specification/plan and companion integration note. No runtime
or visual-completion claim is associated with this stage.

### Stage 1 — asset and behavior inventory

- [x] Enumerate effective sources from installed PK4s and repo overrides with
  explicit precedence, duplicates, case collisions, hashes and include graph.
- [ ] Parse/tokenize resources without interpreting comments/strings as syntax;
  enumerate windows, properties, expressions, named events, scripts and assets.
- [ ] Classify every root, include, special widget and referenced material;
  distinguish geometry furniture from complex bitmap exceptions.
- [ ] Produce a migration manifest with unresolved cases explicit, dependency
  ordering and per-resource behavioral/visual qualification fields.
- [ ] Record baseline engine captures and behavior fixtures for every family,
  with SP/MP entry paths and map/state requirements.

Exit: reproducible complete inventory and family baseline evidence, including
all diagnostics and still-unclassified resources; no inferred translated status.

### Stage 2 — document, layout, time and engine integration

- [x] Pin/check dependencies, integrate through Meson and credit incorporated code.
- [ ] Version canonical document/token/path/timeline schemas and editor metadata.
- [ ] Load/validate documents with stable IDs and actionable source locations.
- [ ] Implement DPI, viewport, anchoring/expansion, text-scale and clipping math.
- [ ] Implement continuous timelines, easing, interruption, pause and cancellation.
- [ ] Introduce runtime/game bridge and retain explicit temporary migration routing.
- [ ] Render and interact with one representative screen on GL/Vulkan in gameplay;
  validate the chosen layout library against vector/editor requirements.

Exit: an actual integrated runtime, native behavioral/math tests, paired game
builds and windowed engine captures. A standalone math test is partial evidence.

### Stage 3 — production vector renderer and component kit

- [ ] Adaptive paths, fills/holes, strokes/caps/joins, gradients, masks and layers.
- [ ] Antialiasing, premultiplied/color contracts, transforms and density caches.
- [ ] Font rendering, localization, escapes, shaping policy and IME integration.
- [ ] Measured Marine/Strogg/world/vehicle primitives, icons and all widget states.
- [ ] Screenshot comparisons across DPI/backends and measured rendering cost.

Exit: every specified primitive/component represented with editable sources,
state gallery, output-space quality evidence and backend equivalence.

### Stage 4 — editor foundation and real source round trip

- [ ] Shared runtime preview, document hierarchy, selection and property inspector.
- [ ] Editable source model, undo/redo, atomic save and reopen without data loss.
- [ ] Canvas transforms, snapping, anchors/constraints and component instances.
- [ ] Initial path/timeline editing and source-linked diagnostics.
- [ ] Edit/package/run a representative screen through engine and companion.

Exit: demonstrated source round trip, recovery tests and exact-renderer preview.
This is an intermediate editor, not the full editor-delivery gate.

### Stage 5 — full translation and behavior replacement

- [ ] Import every inventoried GUI/include into canonical replacement documents.
- [ ] Implement all used properties, expressions, events, commands and widgets.
- [ ] Replace `idWindow` coupling in engine and canonical SP/MP game sources.
- [ ] Verify per-entity instances, game-state updates, model/material previews,
  save/checkpoint/restore, loading, cinematic and scripted-terminal behavior.
- [ ] Inventory confirms zero missing replacements and zero silently ignored syntax.

Exit: complete functional replacement corpus and per-resource parity evidence.
Retaining bitmap furniture or old GUI rendering does not qualify visually.

### Stage 6 — full visual reconstruction and screen polish

- [ ] All furniture vectorized with source-specific silhouette/detail fidelity.
- [ ] Responsive composition, optical alignment, states and motion for every screen.
- [ ] All bitmap exceptions reviewed as complex imagery, not convenience fallbacks.
- [ ] All languages, text scales and families reviewed against the specification.
- [ ] Every manifest entry has traceable behavioral and visual acceptance evidence.

Exit: high-definition full corpus; generic styled approximations do not pass.

### Stage 7 — extensive editor completion

- [ ] Full vector pen/handles/gradients/fill rules/import/export.
- [ ] Timeline/easing/multi-key editing, state graphs and binding ownership tracing.
- [ ] Dockable/multiple-document workspace, component variants, align/distribute,
  preview fixtures, asset search, diagnostics and performance inspection.
- [ ] Autosave recovery, external changes/conflicts, stable source round trips.
- [ ] Full specified edit/localize/bind/animate/save/package/SP+MP demonstration.

Exit: the complete editor contract, not a subset renamed as finished.

### Stage 8 — cutover, packaging and release qualification

- [ ] Replacement is the default for all current GUIs; obsolete renderer/window
  implementation removed or isolated solely for an explicit external-mod policy.
- [ ] No current resource relies on temporary legacy routing or raster furniture.
- [ ] Windows/Linux/macOS/Android-GLES and dedicated build/packaging qualification.
- [ ] SP/MP gameplay, world terminals, scope/vehicle/cinematic/save/restore coverage.
- [ ] DPI/aspect/refresh/input/localization/extreme-combination evidence matrix.
- [ ] No unexplained renderer/UI warnings, file leaks, unbounded cache growth or
  material performance regressions against the captured representative baselines.
- [ ] README, user/editor documentation, credits, candidate and curated release
  notes describe the delivered behavior and actual upgrade requirements.
- [ ] All stage commits pushed and exact remote revisions verified.

Exit: requirement-by-requirement audit of actual files, builds, packages,
screenshots, behavior results and editor use. Missing or indirect evidence keeps
the goal active. Do not claim cross-platform support from Windows-only tests.

## Evidence discipline and progress log

Store temporary tools/captures under `.tmp/`; keep reproducible commands and
compact reports in documentation. Do not commit proprietary retail extractions.
Use `builddir/` and the provided MSVC-aware Meson wrapper. Stage with
`meson install -C builddir --no-rebuild --skip-subprojects` through that wrapper.
GUI captures come from engine `screenshot`; logs come from the configured
savepath/game directory. Do not substitute OS captures or main-menu-only tests.

Before marking a stage complete, record requirements covered, exact commands,
tested revisions, outcomes, remaining scope and unrelated issues. After a
successful commit/push, verify remote heads before saying the stage is published.

| Date | Stage | Authoritative progress | Remaining |
| --- | --- | --- | --- |
| 2026-09-08 | 0 | Specification/integration contracts committed and pushed in both repositories; remote heads verified | Complete |
| 2026-09-08 | 1 | Effective VFS inventory and native structured import implemented for 271 resources; 15,414 preprocessed windows and 8,654 events, with two explicit expression diagnostics; migration seed remains pending | Full semantic/binding classification, material/bitmap decisions and family gameplay baselines |
| 2026-09-08 | 2 | Retained engine rendering, density and rectangular clipping integrated; canonical documents, value edits, presentation tracks, controls, SDL/session ownership and typed application/CVar expression bindings implemented | Complete component/event/binding semantics, game bridge and legacy lowering, text/IME and broader input qualification, transformed overflow clipping, text backend and representative screen/editor qualification |
| 2026-09-08 | 3 | Editable responsive paths, winding fills, stroke unions, linear paints, analytic coverage, isolated subtree opacity and editable alpha masks integrated | Broader composition effects, complete paints/strokes/fonts, production components and all artwork, performance and broader platform/visual qualification |

Inventory methodology and limitations: [inventory report](../ui/inventory-report.md).
The [native import checkpoint](../ui/legacy-import.md) records source/hash-bound
preprocessing, ordered declarations, expression/script trees and dependency
records. SP/OpenGL and MP/Vulkan produce identical exports; none of the 271
resources is accepted as a replacement.
Canonical source and motion contract: [document checkpoint](../ui/document-format.md).
[Live state bindings](../ui/bindings.md) now update geometry, text and availability
without rebuilding documents, validate state batches atomically and retain
application values through renderer/language changes. Native checks and reviewed
SP/OpenGL and MP/Vulkan captures pass. The game bridge, legacy lowering and full
Stage 2 completion remain open.
The [presentation boundary](../ui/presentation-bridge.md) now separates session,
game and SDL consumers from legacy windows, with read-only value queries and
explicit expression overrides. Game API 48 requires coordinated engine/SP/MP
modules; retained document alias mapping and action dispatch remain open.
The [instance checkpoint](../ui/instances.md) removes the single-document
runtime restriction, isolates state/input/layout/clocks, reuses per-view render
backends and protects composition target identities within a host submission
frame. Normal GUI-manager routing, coordinated engine multi-view resource
reset, world surfaces and save restoration remain open.
The [ownership and persistence checkpoint](../ui/instance-persistence.md)
adds a private manager contract with safe allocation/destruction and versioned,
transactional canonical snapshots. Preview resource reload now preserves focus,
modal scopes, unbound control availability and transition progress as well as
application values. The retained manager adapter, real application operations,
world output and game-save/demo framing remain required M1 work.
The subsequent [application integration checkpoint](../ui/managed-application.md)
adds explicit retained/deferred normal loading, typed brightness/shadow operations,
bounded per-GUI save framing and coordinated engine view invalidation. Complete
events/aliases, text/control contracts, production mappings, world surfaces and
the surrounding game save/demo format remain M1 work. This does not accept a
production GUI or close the complete system-screen/editor milestone.
The [writable presentation alias checkpoint](../ui/presentation-aliases.md)
extends this integration with shared metadata slots, typed property conversion
and durable expression ownership. Full semantic lowering and production screen
acceptance remain open.
Semantic buttons now use authored state timelines, projected hit testing,
source-order/spatial navigation and modal focus ownership. The
[interaction checkpoint](../ui/interaction.md) records native behavior checks,
SP/OpenGL and MP/Vulkan engine traces and reviewed rendered states. Game
dispatch, other widgets and the full Stage 2 gate remain open;
these fixtures accept no stock GUI as a finished replacement. Subsequent
[SDL/session ownership](../ui/input-routing.md) integrates the device adapter,
console handoff and gameplay release gates; game dispatch and complete platform/
widget qualification remain open.
Native path source/compiler: [vector checkpoint](../ui/vector-paths.md). Curves,
holes, strokes, gradients, responsive cuts and
[analytic pixel coverage](../ui/coverage-antialiasing.md) are integrated.
Broader composition effects, quality/performance qualification, full art reconstruction
and the complete Stage 3 gate remain open.
Runtime CPU counters, repeatable native/gameplay profiles, opacity/integer-motion
cache reuse and stable tessellation are recorded in the
[performance checkpoint](../ui/runtime-performance.md). Optimized-build, GPU and
full-corpus costs are still unqualified.
Isolated source-over opacity layers are now integrated with GL/Vulkan targets,
transparent clears, image alpha coverage and stable subtree ordering. The
[composition checkpoint](../ui/composition.md) tracks native overlap checks,
matching-panel pixel comparisons and paired renderer/game API requirements.
Editable mask geometry now uses the same vector compiler for shaped clipping,
holes and soft reveals. The [mask checkpoint](../ui/masks.md) records its alpha,
nesting, resource lifetime and qualification evidence. Other effects and the
complete Stage 3 gate remain open.
GL/Vulkan composition pixels pass in the current fallback draw paths; requesting
the shared GUI owner still reports zero owned views in these captures. Shared
ownership and total GPU image-pool trimming remain explicit follow-up work.
