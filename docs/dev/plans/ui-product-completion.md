# UI product completion: implementation review and delivery plan

9 September 2026. Status: reviewed plan; product implementation incomplete.

Reviewed engine revision: `80bb39392299b1efc1e33a836818896b70e1d2c8` on
`idtech5-ui`. Paired game revision:
`1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`.

## 1. Required outcome

Full implementation of this plan must deliver a complete, high-quality Quake 4
UI product at an id Tech 5 standard of presentation, interaction, authoring and
integration, with modern DPI and display support. This is an acceptance
requirement, not an aspiration that can be dropped when individual subsystems
become difficult. Completion includes **all current GUIs, the complete runtime,
professional vector artwork, the extensive visual editor, gameplay parity,
packaging and platform qualification**.

The [original delivery plan](idtech5-ui.md) retains the full scope and historical
stage record. This document adds a source-based implementation audit and the
next delivery order. The [visual design specification](../ui-visual-design.md)
remains normative for framing, color, type, family identity, controls, states,
motion and editor behavior. Implement its requirements before accepting artwork;
do not create a competing set of design values here.

A runtime demonstration, a polished main menu, a conversion counter, a property
inspector or a partial editor cannot satisfy the final gate. A representative
screen is an intermediate integration milestone. Every outstanding requirement
must retain an owner, evidence and an explicit completion state.

## 2. Assessment of the current implementation

The architecture is a useful foundation, but the current branch is an integration
prototype rather than a complete user interface. It has substantial work in
rendering and isolated semantics, while actual application integration, the
control library, production artwork and editor remain unfinished. Prioritize
connecting these capabilities into complete user tasks, while retaining focused
tests for geometry and lifetime correctness.

### What should be preserved

- The engine-independent retained core, pinned RmlUi/JsonCpp/libtess2 dependencies
  and native Meson integration in [meson.build](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/meson.build).
- The canonical JSONC document, stable node IDs, explicit typed properties,
  source diagnostics and transactional value replacement in
  [Document](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/src/ui/retained/Document.h).
- Continuous presentation time, easing, retargeting, pause/cancel and reduced
  motion in [Motion](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/src/ui/retained/Motion.cpp).
- Atomic typed state updates, explicit property ownership and read-only CVar
  sources in [State](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/src/ui/retained/State.cpp).
- Native editable paths, output-space curve tolerances, coverage, isolated
  opacity and masks; semantic focus/modal/input separation; the public
  [presentation boundary](../ui/presentation-bridge.md).
- The new [independent contexts](../ui/instances.md), shared-service lifetime,
  reusable render backends and frame-aware composition target identities.
- Native preprocessed import with source/hash provenance and explicit unresolved
  cases, rather than treating a text substitution as successful translation.

Continue the retained document/component architecture. Use BFG/SWF as a design
and selective implementation reference when it solves a measured problem. A
wholesale Flash runtime port would still leave our layout, authoring, text,
behavior migration and high-refresh requirements to implement. RmlUi remains
the layout integration candidate; complete the screen/editor gate below before
declaring that choice proven for the whole product.

### Current implementation progress

The historical [typed value-control increment](../ui/value-controls.md), based on engine
`b3bf279b4347329cd8a45a4533536adc0510e860`, adds canonical toggle, stepped slider
and constrained choice controls with authoritative readback, immutable typed
proposals and editable vector parts. A packaged `openq4.system` document and
default-off normal Session route connect eight initial immediate image controls
to the settings transaction. The source includes all 53 draft/baseline fields,
guarded dirty Back/discard behavior and the display-confirmation structure.

The final Windows build/package stage, 13 native UI suites, 173 production
Session checks and 15 capture tests with 444 rejected log mutations passed.
Hidden windowed SP/OpenGL at 125% and MP/Vulkan at 200% each passed 18 normal
Session stages after active gameplay; all 14 engine render-target images were
reviewed. The [qualification record](../ui/value-controls.md#qualification-of-this-increment)
binds the final sources/binaries and states the physical-input, motion and
platform limits. This progress addresses parts of UI-01/UI-02/UI-03/UI-05/UI-12; it
does not close those findings, M2 or any migration. Full controls, precise
numeric editing/IME, the entire page and dependent flows, artwork, editor and
platform acceptance remain required. The findings below retain the evidence
and required resolution from the original reviewed revision.

The subsequent [SYSTEM controls and transactional exit increment](../ui/system-exit.md)
starts from engine `6c0defc8158c674c0e117a0a3754882a51bcef0d`. It expands the
normal page to 16 value controls with seven additional immediate settings and
VSync, retaining the full 53-field schema. Dirty Back offers Apply changes,
Discard changes and continue editing. A typed Apply-and-exit operation binds
exit intent to the live owner/request and returns through a private, one-use
receipt only after successful immediate application or Keep and completed
persistence. Revert, timeout, failure and recovery cancel exit intent; the
ordinary Apply button remains on the page. The adapter also preserves repeated
wheel navigation without replaying an unchanged pointer move. Authored modal
scopes establish safe focus before the first draw, restore prior focus and reject
stale input across scope replacement and snapshot reconstruction. The final
Windows build, 15 native UI suites, four staged normal Session runs (92 stages)
and review of all 34 engine images passed; the
[bounded qualification](../ui/system-exit.md#qualification-and-remaining-work)
records source/binary/log/image bindings and the remaining rendering findings.
This increment changes no
requirement status: all 227 requirements, 271 pending GUI migrations and seven
final gates remain required. Complete controls, numeric editing/IME, all settings
and dependent flows/effects, source-derived artwork and transitions, the native
editor round trip and full platform/input/display/language qualification remain
open; this subset cannot accept M2 or the production SYSTEM screen.

The subsequent [native-output OpenGL repair](../ui/native-output.md), starting
from `a8bad0f5bf69b08493714ad4faaeae7585d5abf2`, removes the swap-tail filter
that could resample completed UI-only frames. Existing world scene sizing and
spatial/temporal resolves, supersampling limits and the explicit legacy mode-0
crop remain in place. The final integrated production-method test passes 1,220 checks and
rejects seven compiled mutations. Separately bound Windows SP/OpenGL engine
captures show zero RGB differences from the 100% UI reference across eight
ordinary-scale cases; the preserved baseline demonstrates the former filtering.
All 24 engine images were reviewed. SUR-006 is
partial and M6 remains unaccepted. Dynamic resolution,
all UI surfaces and input/cinematic/subtitle geometry, CRT parity, the existing
GL mode-3 filter mismatch and the full renderer/platform matrix remain required.
This bounded repair removes no requirement, migration or final gate.

The subsequent [text-entry foundation](../ui/text-entry-foundation.md), starting
from `e3e65887480368191df154a9b52cce69d8e72140`, integrates validated Unicode
commits/preedit, an atomic bounded edit buffer, exact decimal parsing and checked
Windows clipboard primitives. Pending event-payload cleanup is repaired on both
queue implementations. Native and compiled-method checks cover these services;
live fields, ordered owner-aware delivery, native composition provenance,
shaping/caret geometry and unconfirmed-edit Apply/exit guards remain required.
The full 227-requirement scope, 271 migration records and seven final gates stay
unchanged and unaccepted by this increment.

The native managed-bridge increment, based on engine
`59dbfa9429056a85ead40fff5e8f4f5b38be4225`, extends the [numeric field contract](../ui/numeric-fields.md)
and [native text boundary](../ui/text-input-routing.md). Exact typed settings comparisons and FTZ/DAZ-independent journal number serialization extend through transaction, display validation and startup recovery. Managed native-owner endpoints re-resolve the registered allocation/backend/document after resource preparation; copied owner barriers and exact retirement preserve stable drafts. The Windows hook-to-store bridge binds one checked provider generation and native/editor lease, preserves the immutable closed receipt through FIFO acknowledgements and synchronization, and closes its own callback scope before fence publication. Explicit lifecycle reconciliation and a checked idle barrier query supplement the ordinary Pump path. Shared focus reveal uses exact projected border corners, a density-aware 4dp inset where authored scroll ranges permit it, nested transformed scroll planes and the owning view clock without repeated pointer-scroll takeover.

The Windows engine build and all 40 UI suites pass. Windowed SP/OpenGL at 125% and MP/Vulkan at 200% each reach gameplay, pass 38 semantic operations and produce 12 reviewed engine screenshots. The focused field retains the checked 4 dp body inset; at 200% its bottom is pixel 464 inside a body ending at 472. Raw screenshots and PNG previews have identical RGB pixels. SP has no warnings; MP retains the previous 93 warnings. Final captures use the verified E: installation, whose engine package checksums match the earlier capture.

The coordinator passes MSVC debug checks with 18 rejected mutations, Clang with 19 (including allocation failure), and Linux sanitizer checks. SDK bridge, managed-owner and exact recovery tests retain their own frozen source bindings and explicit limits. The immutable combined record is `.tmp/ui/native-managed-bridge-integration/validation-evidence.json`. Earlier timeout, harness-path and asset-discovery runs remain preserved; they are not substituted for the final qualification.

Native activation, the production Session route/probe, ordinary native character association, candidate geometry, complete composition/shaping, full page/editor/platform qualification, all 271 migrations and seven final gates remain open. Counted SDK callbacks, semantic controls and bounded focus geometry do not establish installed IME behavior, physical-input qualification or real interrupted-device/power-loss recovery. No requirement status or product acceptance changes.

The current [scrollbar integration](../ui/scrollbars.md), beginning at engine `5e0d3ebf20234415815a72c914fe788953e4367b`, adds actual authored SYSTEM scroll geometry and dp persistence. The same scoped increment checks original native-store transfer, exact image reductions and actual GL upload allocation. The full build and 61 UI suites pass, with reviewed windowed GL/SP and Vulkan/MP gameplay/restart/SYSTEM captures. `WID-010` is partial; the register retains 227 requirements, 271 unaccepted migrations and seven open final gates. Native activation, mixed settings recovery, complete controls/screens, editor and full platform/artwork acceptance remain required. The revision-bound findings below remain historical audit evidence.

The subsequent [native ownership publication increment](../ui/native-input-publication.md), based on engine `d16c5b9e7935a273e274ea6c5956729d0da62c85`, connects production Session/GUI/window facts and typed issued-emission inventory while retaining default-off activation. Conditional legacy-alpha metadata preserves original unresolved source rather than accepting incomplete migration. The full build,64 UI suites and reviewed windowed GL/SP and Vulkan/MP gameplay/restart/menu views pass. Terminal event disposal/release, live native routing and exact native expression observations remain underway. Requirement/migration counts and final gates do not change.

The following [native legacy-alpha observation](../ui/legacy-import.md), based on engine `fae10f2d5bd610369b42f042780d9cd8f24aa19d`, establishes initialized zero alpha for the exact core4/hub4 stock expressions in both SP/OpenGL and MP/Vulkan. It retains source bytes, actual lookup/fixup/register/property evidence and separate material-preload logs. The complete build,65 UI suites and reviewed gameplay/restart/SYSTEM continuity checks pass. Include provenance, later scripts/timelines and source-bound retained lowering remain required; no final product or migration gate is accepted.

The following [exact image-content](../ui/image-content-recovery.md) and [native terminal-disposal](../ui/native-input-publication.md) checkpoint, based on engine `2e95a66bab30b1acd3f315f86f3570f142965b1d`, adds checked per-image CPU reconstruction and actual store ownership/disposal. The full build,67 UI suites and reviewed windowed SP/OpenGL and MP/Vulkan gameplay/restart/SYSTEM checks pass. Actual loaded-image census supports the next journal sizing decision. Complete source/material cohorts,durable recovery,normal native sink scheduling,held-source inhibition,all production controls,editor and every migration/final gate remain required. No acceptance status changes.

The [authored choice scrollbar](../ui/choice-scrollbars.md) checkpoint, based on engine `fc4443cb071022e29b429c9b74731dfa9b0c3b86`, integrates all four SYSTEM dropdowns. The full build,69 UI suites,compiled behavioral mutations,Linux sanitizers and20 reviewed SP/OpenGL125% and MP/Vulkan200% gameplay/SYSTEM/popup images pass. Fitting lists hide inactive bars and scrolling leaves settings unchanged. Popup safe-area placement and source-derived framing still need refinement; physical/native input, complete settings recovery, editor and full-corpus acceptance remain required. No requirement, migration or gate acceptance changes.

The [canonical document-editing foundation](../ui/document-editing.md), based on engine `1bcb4aec68fbaa3187ca5ffea30720f003f57f1f`, adds source-preserving structural/value batches and bounded undo/redo. The full build and70 UI suites pass; independent source review, integrated compiled mutations and frozen MSVC/GCC sanitizer evidence qualify this portable core. The existing strict parser is shared through narrow internal forwarding calls. The next editor increment must prepare and publish history, actual Runtime context and cached source/path metadata together, after exact native-owner retirement. Visible authoring, file-save recovery and full editor/product acceptance remain required. No requirement status or gate changes.

The [owned image recovery](../ui/image-recovery-ownership.md) and [native sink driver](../ui/native-input-driver.md) increment, based on engine `b880d593e358d64276bccbb0ec58c3b83f5e5011`, retains prepared texture directions and connects actual engine input disposal to checked retirement. The full engine build and76 UI suites pass; the older metadata fixture needed declaration/link updates and then passed533 checks and14 compiled mutations. Windowed SP/OpenGL at125% and MP/Vulkan at200% passed gameplay, ordinary renderer restart and SYSTEM activation; all four engine images were reviewed. SP is warning-free; MP retains94 baseline warnings. These passive captures do not qualify prepared Apply or native entry. Built-in quiescent image disposal, complete retail reconstruction, native translator/bootstrap and lifecycle activation remain required. No requirement status, migration acceptance or final gate changes.

### Findings that block product completion

The [native text owner/store checkpoint](../ui/text-input-routing.md), based on
`f2afdd1ce62201137f9513ed4dea6bf4ad481733`, adds allocation/backend/document/editor
ownership checks, engine queue-continuity signals, a portable native shadow
document and an actual Windows SDK text store. Application-origin notifications
allow reads and defer writes until the complete batch has returned. A first
disabled SDL native dispatch fence bounds collection; its publication marker
alone does not prove that earlier SDL events were consumed without loss. The
checked queue consumer, native activation and editor range reconciliation remain
required. Native field use, complete composition/shaping, production screens,
the editor, every migration and all final product gates remain open.

These findings are based on source inspection at the revisions above, except
where a test or capture is explicitly named. They are not all new regressions.
Priority P0 blocks replacement correctness; P1 blocks the required quality or
authoring standard. Both must be resolved before final acceptance.

| ID | Priority | Evidence and consequence | Required resolution |
| --- | --- | --- | --- |
| UI-01 | P0 | [UserInterface.cpp](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/src/ui/UserInterface.cpp), `Alloc`/`FindGui`, still construct the legacy implementation; manager lists, level-load handling and diagnostics assume a desktop window. [RetainedUI.cpp](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/src/ui/RetainedUI.cpp) owns one preview runtime. Normal menu/HUD/world loading cannot use the replacement. | A retained `idUserInterface` adapter, backend-neutral manager ownership and explicit per-resource routing. Exercise reload, references, unique instances and teardown through normal callers. |
| UI-02 | P0 | `RetainedUI_ProcessEvent` queues activation requests; `Events_f` prints and drains them. There is no production dispatcher for those requests. State bindings read CVars, but do not implement settings transactions or the legacy event language. | Typed action/state bridge to session and canonical SP/MP game code; named events, presentation aliases and command results with exactly-once dispatch. |
| UI-03 | P0 | [Document.cpp](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/src/ui/retained/Document.cpp), `ReadNode`, accepts group/text/vector and only the button control role. [RetainedUI.h](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/src/ui/RetainedUI.h) carries key/pointer/focus/cancel events, with no text/composition or wheel payload. | Functional compound widgets, scrolling, editable text, IME, binding capture and complete device behavior. An enabled-looking control must perform its advertised task. |
| UI-04 | P0 | Core contexts are independent, but the engine host still has mutable global viewport fields and `Close`/restart call `host.Reset()`. Layer zero means the default output, and [VectorElement.cpp](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/src/ui/retained/VectorElement.cpp) explicitly limits projection to affine transforms. | A per-view render-surface contract, coordinated invalidation, world projection/density/input and durable instance snapshots. Native multi-context tests alone do not establish this. |
| UI-05 | P0 | The [migration manifest](../ui/migration-manifest.json) has 271 effective resources, all pending; there are no production `.q4ui` files under `content/`. [Legacy import](../ui/legacy-import.md) records 269 syntax-clean resources and two unresolved alpha expressions. | Complete semantic lowering and reviewed behavioral/visual evidence for every root/include/special widget. Preserve unusual expression grouping and integer remainder semantics. |
| UI-06 | P1 | The retained `Fonts` adapter in [Runtime.cpp](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/src/ui/retained/Runtime.cpp) measures and emits individual code points, ignores requested style/weight and has no shaped-run/caret contract. EngineHost reuses the large font slot. The engine already has [TrueType rasterization](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/src/renderer/tr_fontTTF.cpp), so this is not an absence of scalable font support. | A density-aware retained font/run service, kerning/shaping/fallback and text editing using the same measured glyph runs. Qualify existing languages and define missing-glyph behavior. |
| UI-07 | P1 | `VectorGeometry::Render` keys coverage on fractional transform phase; `Renderer::RenderGeometry` transforms/clips CPU meshes; `EngineHost::Draw` expands indexed triangles into temporary engine vertices on each draw. Current CPU profiles are unoptimized and GPU cost is unqualified. | Release-build CPU/GPU measurement, persistent indexed submissions, reduced allocation/copying and a fractional-motion strategy that meets the quality/performance budget. |
| UI-08 | P1 | All composition targets are viewport-sized. The pool tracks 48 slots and 256 MiB of RGBA8 attachments. `GenerateTexture` fails explicitly; the separate RmlUi clip-mask interface is not implemented. | Cropped/cached composition, actual renderer resource accounting/reclamation, generated-image support where required, and exact transformed clipping. Keep allocation failure diagnosable and the next frame recoverable. |
| UI-09 | P1 | `HitControl` uses RmlUi element hit testing and a rectangular control box. Canonical alpha masks are decorators and are not explicitly evaluated for interaction. | Authored hit-region policy, ancestor clip/mask participation and matching focus/navigation eligibility. Hidden or clipped controls must not intercept input. Preserve ergonomic button areas deliberately. |
| UI-10 | P1 | `.q4ui` has value replacement, but no component definitions/instances, structural edit transactions or full behavior graph. [GEWorkspaceFile.cpp](https://github.com/themuffinator/openQ4/blob/80bb39392299b1efc1e33a836818896b70e1d2c8/src/tools/guied/GEWorkspaceFile.cpp) still serializes legacy `idWindow` GUIs. | The full shared document/editor model, history, component overrides, path/timeline editing, save/recovery and exact-runtime preview. The old editor is not a Q4UI editor. |
| UI-11 | P1 | Current fixtures demonstrate rails, buttons and masks; none is accepted production artwork or a translated screen. `EngineHost::LoadMaterial` handles direct images/font pages, not complete legacy material/movie/model behavior. | Measured family-specific component artwork, all interaction states, responsive screen compositions and functional complex-imagery/material/model operations. |
| UI-12 | P1 | `Viewport` has display and user scale, but no independent text scale or per-domain HUD/surface policy. Restart restores application variables, not focus, scroll, modal and timeline snapshots. | Independent ergonomic scale controls, live monitor/viewport changes and explicit state continuity through resize, language change, restart and save restoration. |

### Verification performed for this review

The current unstaged implementation was reviewed, built, validated and published
as `80bb3939`. Client, dedicated and both renderer targets built/staged through
the standard Windows wrapper. Four native suites and production layer-pool,
input, CVar-source and presentation-boundary checks passed. Documentation links
passed. The companion remained unchanged.

The engine-render-target images were inspected, and an independent mask oracle
passed in actual windowed SP/MP gameplay. Exact commands, hashes and limits are
in the [instance checkpoint](../ui/instances.md); local evidence is under
`.tmp/ui/contexts-review/`.

| Capture | Pixel result | CPU submission p95 |
| --- | --- | --- |
| SP/OpenGL, 1280x720, 125%, before/after video restart | 157,080 checked pixels; no hidden-pixel leaks; max channel error 3.413/255 | 10.071 / 10.724 ms |
| MP/Vulkan, 1920x1080, 200% | 401,472 checked pixels; no hidden-pixel leaks; max channel error 2.602/255 | 16.938 ms |

These are 60-frame mask-fixture samples in a debug, optimization-0 build. They
include cold work and GUI flushes. They do not establish release performance,
GPU completion, real 144/240 Hz pacing or multiple engine-facing views. The
visible panels and hole demonstrate composition, not a usable settings screen.
Requested shared-GUI execution still reports fallback and zero ready views.

## 3. Product architecture to complete

### Runtime, document and engine boundaries

Keep one canonical source model. Add reusable component definitions, stable
instance IDs, explicit overrides, state variants, semantic events/actions,
presentation aliases, resource references and editor metadata. Generated RML
is layout output. It must not become an independently edited document.

Implement a retained `idUserInterface` adapter and make the manager own common
interface/lifecycle records. Isolate legacy desktop inspection in its backend
and old editor. Define behavior for every public method, including `State`,
`StateChanged`, activation, named events, cursor/text queries and save/restore.
Preserve existing game/session GUI identifiers with explicit source-to-document
mapping. During migration, route only qualified entries; report the selected
backend and unresolved resource. Final stock-product routing must have complete
replacement coverage. Decide and document the external-mod compatibility policy
before removing legacy loading; it must not conceal an unconverted stock GUI.

Separate immutable compiled documents from mutable instances. Own application
state, focus, input pairing, scroll, modal stack, timers and command results per
instance. Share immutable fonts/resources/components and measured cache entries.
An application frame evaluates authoritative state/events, layout/presentation,
hit geometry and render submission in a documented order. Gameplay timers and
presentation time remain distinct; pausing SP must not freeze menu motion.

Give each render operation an explicit surface: output target, viewport/origin,
logical dimensions, density, projection and clip. Avoid global viewport fields
that can leak between editor panes, HUDs and world terminals. The renderer owns
GPU resources and in-flight lifetime; the runtime owns logical leases. Coordinate
all live views before video/font invalidation, then rebuild from preserved
instance state. Respect deferred submission across frames in flight as well as
multiple views in one frame; retain the new same-frame target guard.

### Behavior translation and application actions

Lower the native import records into the canonical model with a source map and
a coverage report for every used declaration, property, expression, event and
command. The 271-resource count includes shared includes; an include may become
a component rather than a standalone screen, but still needs complete mapping.
Never assume one source file equals one independent runtime view.

Build a semantic compatibility suite around real examples: right-associated
subtraction/division, legacy remainder conversion, tables/indexing, duplicate
window resolution/merges, `gui::` values, CVar/string conversion, named events,
relative `onTime`/reset behavior, conditional scripts and animation overrides.
Resolve the two bare-backslash alpha expressions from native behavior, preserving
the source and diagnostic until the expected result is established.

Route UI intent to typed application operations. Settings use validated draft,
apply, cancel, default and rollback state; changing a control is not equivalent
to blindly writing a CVar. Multiplayer commands retain server authority and
error/results handling. Keep translated command parameters as data until the
appropriate dispatcher handles them. Trigger actions once per completed semantic
activation and play stock UI sounds once per event. Trace action provenance for
debugging without exposing implementation detail in player-facing screens.

Version canonical documents and instance snapshots before bulk migration.
Snapshots must identify the source/schema and preserve the required application
state, selection/scroll, script timers and overrides. Specify which transient
press/hover states cancel and which presentation states resume. Preserve or
explicitly migrate existing save payloads in coordinated engine/SP/MP changes;
test old saves, new saves and invalid/missing resource failures without positional
stream corruption. Do not infer save compatibility from application-variable
restoration during a video restart.

### Text, controls and accessibility

Extend the existing TrueType/glyph services into an explicit retained text
backend with per-view density and text-scale cache keys, consistent metrics and
layout/draw runs. Add shaping, kerning and fallback support through a bounded
integration decision; inspect and credit any added dependency before use. Resolve
the current BMP glyph limit and define behavior for valid unsupported characters
in player/server/chat text. Qualify all shipped languages, accented/Cyrillic
text, combining characters, long strings, line wrapping and legacy inline colors
and icons. Supply new UI copy through language tables.

Text fields need selection, caret movement, deletion by grapheme, horizontal
scrolling, clipboard operations, composition/IME and correct candidate placement.
Scope text input to the active field; language/monitor changes must update its
geometry without losing the edit. Connect wheel, scroll, gamepad repeat, touch,
focus loss, device removal and reconnection to semantic controls. Automated
tests use internal fixtures; direct host input work needs the specific permission
required by AGENTS.md.

Implement the full widget set from the design specification. Beyond buttons,
this includes checkbox/toggle, slider, choice/dropdown, tabs, list/table/tree,
scrollbar, text/key-binding field, gauge/progress, tooltip, modal and status
feedback. Specify value, selected, focus, pressed, disabled, loading, empty and
error states where meaningful. Keep focus visibly distinct from selection,
restore focus predictably, and prevent clipped/inactive content from intercepting
input. Server lists preserve selection across updates; chat preserves inspection
position; tables need virtualization once realistic data establishes its need.

Expose independent UI/text/HUD scale, reduced motion, readable panel backing and
non-color state cues. Audit essential text against the specified 4.5:1 normal
and 3:1 large-text contrast targets on actual composited backgrounds. Define
semantic role/name/value/state and focus information in the control model and
implement the platform accessibility exposure required by the product. A label
field alone is not accessibility support.

### Visual quality and motion

Build family-specific vector component libraries from measured source GUIs,
materials and engine captures. Marine menus retain military plates, thin olive
rails, 45-degree corners and orange activation. Strogg, HUD, scope, vehicle and
diegetic families retain their own identity. Every recognizable rail, inset,
marker, separator, icon and control needs editable geometry and all required
states. Reusing a generic panel across every family fails visual acceptance.

Store design tokens and reusable path/component assets once, with documented
variants. Preserve fixed corner cuts, stroke weights and glyph proportions while
layout expands. Reflow forms/tables at narrow widths or large text scale; extend
imagery/framing appropriately on ultrawide displays. HUD anchors and aim geometry
follow the gameplay projection, with independent safe areas. Show hierarchy,
spacing and optical alignment at normal viewing size as well as zoomed edges.

Keep complex levelshots, scene imagery, cinematics and model views as approved
resource operations. Classify each bitmap exception with its source and purpose.
Rasterized buttons, text, framing or whole-screen screenshots do not qualify as
vector reconstruction. Keep extracted retail art out of Git; resolve installed
assets through the VFS and keep generated resources reproducible.

Complete masks, transformed clipping, gradients and stroke features needed by
the full corpus and editor, including the specified SVG import/export contract.
Unsupported input gets a source-linked diagnostic. Define visual clip and input
clip policies together; use the existing path hit-testing capability where the
authored shape is the interaction boundary. Test partially transparent masks,
holes, fractional positions and nested fades over bright and dark backgrounds.

Build page/modal/state transition orchestration above `Motion`: enter/exit,
completion, cancellation, interruption, sound and lifetime semantics. A new bound
value must be able to retarget its displayed value without losing the binding's
authority; the present binding-versus-timeline exclusion is insufficient for
animated live controls. Support timeline event markers and editor scrubbing
without replaying gameplay side effects. Use the normative timing/easing tokens,
including asymmetric hover and reduced-motion behavior. Rapid reversal, resize,
language changes and repeated back/forward must not jump, flicker or double-fire.

### The complete visual editor

Build a desktop editor using the same canonical evaluator, fonts and renderer as
the game. Reuse the engine's cross-platform window/event/resource abstractions.
Select the editor shell at the first authoring milestone and record the choice;
do not embed an unrelated preview renderer. The existing legacy GUI editor may
remain a translation reference, but its window-pointer model is not the new
document model.

Required editor capabilities are concrete deliverables:

| Area | Complete capability |
| --- | --- |
| Workspace | Dockable/resizable panels, multiple documents/views, hierarchy, search, dirty state, recent files and configurable output presets |
| Canvas/layout | Pan/zoom/fit, rulers/guides/grid, snapping, multiselection, lock/hide/isolate, move/resize/rotate, anchors/constraints, align/distribute, grouping/reparenting and z order |
| Component model | Create/update reusable components, instantiate, switch variants, inspect/reset overrides and propagate changes without losing instance data |
| Vector authoring | Pen/node/handle editing, line/quadratic/cubic paths, holes/fill rules, stroke caps/joins/alignment, gradients, transforms, masks and supported SVG import/export |
| Behavior/motion | State graphs, bindings and current-owner inspection, action/event editing, multi-key timeline editing, easing handles, scrub/loop/rate and deterministic fixture playback |
| Text/assets | Localization key search/edit, typography, material/model/movie references, family libraries and missing-resource diagnostics |
| Persistence | Transactional structural edits, complete undo/redo, comment/extension preservation, atomic save, autosave/recovery and external-change conflict handling |
| Diagnostics | Source/node/property navigation; overflow, focus/hit/clip regions, missing strings, bitmap exceptions, contrast, tessellation/cache, draw and GPU-memory inspection |
| Delivery | Save/reopen, generate/package, run in actual SP and MP, and capture the exact engine render target |

Editor data and dependencies must not leak into the player runtime package.
The final demonstration must create and structurally edit a real translated
screen, alter a path and component variant, change a transition, add a localized
control and binding, undo/redo, recover an interrupted save, reopen, package and
run the result through normal game UI routing. Include multiple documents and
independent viewports. An inspector with no durable round trip fails this gate.

## 4. Delivery sequence and exit gates

Each milestone ends with coherent code, targeted tests, updated evidence and
documentation, a committed checkpoint and verified remote heads. Checkpoints
inside a milestone are allowed but do not mark that milestone complete. The
sequence is a dependency order, not a calendar estimate. Prioritize work needed
by the next complete user flow over additional disconnected demonstrations.

### M0 — Published implementation checkpoint and audited baseline

Status: implementation checkpoint published; this review records its limits.
Keep the paired revisions and evidence above. Refresh the effective inventory
before translation and retain source hashes. Derive a requirement register from
the normative specification and the findings here, with milestone ownership and
evidence fields. The final requirement register is a required implementation
deliverable; the [product requirement register](../ui/product-requirements.md)
now records this scope, refreshed inventory evidence and milestone ownership.

### M1 — Normal application integration and durable instances

Progress: the [ownership and persistence checkpoint](../ui/instance-persistence.md)
adds private manager-neutral lifecycle operations and canonical instance
snapshots. [Normal application integration](../ui/managed-application.md) adds
explicit retained loading and typed session operations;
[presentation aliases](../ui/presentation-aliases.md) add writable metadata and
visual properties with persistent expression ownership. Complete event/action
lowering, the world surface contract and game-save/demo framing remain incomplete;
M1 is still open.
The [event-program increment](../ui/event-programs.md) adds transactional ordered
canonical behavior, lifecycle entry and independent session delivery. It supplies
the application mechanism; full legacy broadcast/timer/native-control semantics,
production component behavior and the remaining M1 boundaries still require work.

Resolve UI-01/UI-02 and the ownership/snapshot portion of UI-04/UI-12. Add the
retained adapter, manager-neutral lifecycle, source routing, typed dispatcher,
state/alias/event bridge, render-surface contract and versioned snapshot design.
Use the companion repository for canonical game changes and align API versions,
build pins and staged modules when the shared interface changes.

Exit evidence: open/close/reload a retained document through a real session/menu
caller; a control changes and reads back an actual setting through the intended
application operation; invalid actions/data fail without partial changes; SP
pause and MP ownership work; independent views survive teardown and renderer
restart. Demonstrate a saved instance round trip. An explicit diagnostic fixture
may establish the adapter, but is not accepted as a finished screen.

### M2 — One production settings screen and the first editor round trip

Progress: the [source-backed SYSTEM contract](../ui/system-settings-contract.md)
inventories all 31 current options, dependent popups and the 32-field preset
footprint. The resulting 53-field catalog, engine-independent transaction core
and typed application service implement owned drafts, validated immediate
Apply/Cancel/Defaults, conflict-safe rollback, service-owned read-only state and
frame-managed owner cleanup. Pending/recovery transactions block automatic and
explicit config writes. The native core, 53-field host extraction and 15-scenario
production service tests pass. Hidden windowed SP/OpenGL density 125% and MP/Vulkan density 200%
gameplay probes each pass 168 ordered readbacks, 19 service results and two
language/video recoveries, with no errors and only the documented baseline and
three deliberate negative-case warnings. Source-bound evidence and reviewed
render-target images are under `.tmp/ui/settings-review/capture-evidence.json`.

The subsequent [display confirmation integration](../ui/display-confirmation.md)
implements asynchronous Apply/Keep/Revert/Retry, owner-present countdown, durable
Pending/Confirmed recovery, exact native file leases, checked config persistence
and strict first-device startup recovery. Eligible confirmation documents can
apply Immediate + DisplayRestart batches. Audio/image/resource, next-map and
preset effects still reject the entire batch before live writes. Pure preset/
Auto-Detect draft expansion, all production controls/artwork, the native editor
round trip and complete platform qualification remain required. These increments
do not complete M2 or accept the SYSTEM page, any migration entry or a product gate.

The historical [value-control implementation](../ui/value-controls.md) connected
canonical toggles, constrained choices and stepped sliders to eight initial
SYSTEM image controls. Typed proposals remain separate from authoritative
draft readback; authored part identity and versioned widget state survive in
the canonical model. The packaged page enters through a non-archived,
default-off normal Session child route and retains its parent on clean return.
At that checkpoint, dirty Back offered continue-editing or discard-to-Editing;
discard-and-exit remained unfinished. The subsequent
[SYSTEM exit increment](../ui/system-exit.md) implements that flow and records
its qualification separately. The source/binary-bound Windows SP/OpenGL 125% and MP/Vulkan
200% normal-route captures qualify the initial draft/apply/discard/choice,
resource-reset, clean-return and reopen subset at 1280x720. Eighteen ordered
stages per run and 14 reviewed render-target images establish this bounded
integration and static presentation only. SP had no warnings/errors; MP retained
94 baseline warnings with no new messages or errors. The immutable record is
`.tmp/ui/value-controls-review/capture-evidence.json`; complete input, motion,
display/platform/language and editor qualification remains open.

Next, complete the actual setting inventory and dependent controls, precise
numeric entry/IME, display capability and resolution catalogs, preset expansion,
remaining effect executors, complete draft/exit workflows and source-derived
artwork. Preserve stable component/source identity and versioned widget state
for editor round trips and renderer recreation. The same M2 exit conditions
apply; an initial usable subset cannot replace them.

Resolve the representative parts of UI-03/UI-06/UI-09/UI-10/UI-11/UI-12. Choose
the shipped system settings page and its dependent popup/confirmation behavior
as the first complete screen; inventory its actual controls and references before
authoring. It must support validated apply/cancel/default behavior and recovery
after display changes. Build genuine controls, typography, stock-derived vector
components and transitions, using the component/source model that the editor
will persist. Keep production documents under `content/baseoq4/`.

Exit evidence: enter from normal SP and MP menus, use the real settings flow,
return to gameplay, and reopen with correct values. Validate narrow/ultrawide,
fractional DPI, large text, long localized strings, focus/modal lifecycle and
reduced motion. Edit hierarchy/layout, a path and a timeline in the native editor,
undo/redo, save/reopen and run the edited screen in game. Source and engine
captures must agree. Give this screen behavioral and visual review independently.

At this gate, decide whether the RmlUi adapter meets exact composition, input,
text and editor requirements. If it fails, record a bounded remedy or replacement
at that interface. Retain the canonical sources and acceptance requirements.
Do not replace a failing requirement with a narrower demonstration.

### M3 — Complete shared controls, rendering features and authoring tool

Finish the full control, text, composition, component, motion and editor
contracts above. Build state galleries for every family and widget. Implement
structural document editing before scaling up source authoring. Integrate
generated textures/resource reclamation as needed for fonts and supported
operations; enforce clip/input agreement. Measure fractional-motion and nested
composition cost in an optimized build before committing to bulk artwork.

Exit evidence: all specified controls work with keyboard, pointer, gamepad and
supported touch/text entry; editor capability table passes its real round-trip
and recovery scenarios; all required primitives/paints/strokes/masks are editable
and render correctly across the backend/density matrix. There are no placeholder
buttons or decorative substitutes for complex controls. Remaining full-corpus
work stays open.

### M4 — Complete menu and multiplayer application flows

Translate and polish main/pause menus, settings, save/load, loading/game-over,
server browser, join/team/spectator flows, scoreboard/chat, buy/arena/Match
Control and demo flows according to the inventory. Bring complex imagery,
material/movie/model previews and asynchronous data/error states through real
operations. Preserve selection, authority and navigation during refresh or loss
of connection. Package and run each family after translation and review.

Exit evidence: every application flow has source/behavior coverage, authored
states, responsive layouts and reviewed engine captures. Complete both SP and
MP task sequences, including cancellation and failures. Save/load and destructive
in-game choices retain appropriate user-facing confirmations. No route silently
falls back to an unqualified stock menu.

### M5 — Complete HUD, world, scope, vehicle and scripted GUI corpus

Finish UI-04/UI-05 and all remaining families. Establish projected density and
render-target selection for world surfaces, per-entity state/events, input-ray
mapping and save/checkpoint continuity. Translate and reconstruct HUDs, wrist
communications, scopes, vehicles, cinematics, loading interfaces and all
diegetic displays. Use explicit gameplay states and representative maps; test
multiple differently sized surfaces simultaneously, including after restore.

Exit evidence: fresh inventory reports complete replacement mappings and no
unclassified declarations/material exceptions; every resource is behaviorally
and visually accepted, with includes traced to components or documents. All
scripted terminal interactions, aim/crosshair geometry, HUD updates and authored
timing work. Save/restore preserves required state and produces no duplicate
actions. Current count is 271 resources; changes to the effective corpus update
the gate instead of freezing an obsolete denominator.

### M6 — Performance, platform qualification and final cutover

Complete the performance/quality matrix below on Windows, Linux, macOS and
Android/GLES through the repository's supported workflows. Qualify dedicated
builds/staging separately. Make all stock-product UI routes use the retained
system; remove obsolete dependencies and isolate any explicitly supported
external-mod legacy policy. Validate a fresh release-style installation using
retail assets and the staged binaries/approved minimal content.

Exit evidence: every requirement and migration entry has current traceable
evidence; no product flow depends on the preview commands, unfinished controls,
temporary legacy routing or raster furniture. Complete player/editor docs,
credits, compatibility/upgrade guidance and curated end-user release notes.
Run the final product audit below before declaring completion or release-ready.

### Relationship to the original stage plan

| Original scope | Delivery milestones |
| --- | --- |
| Stage 0 specification | Remains normative; M0 creates implementation traceability |
| Stage 1 inventory | M0 refresh; M1/M4/M5 semantic and family qualification |
| Stage 2 runtime/integration | M1/M2, completed for all domains in M5/M6 |
| Stage 3 vectors/components/text | M2/M3, family completion M4/M5 |
| Stage 4 editor foundation | M2 |
| Stage 5 full behavior translation | M1 foundations; M4/M5 full coverage |
| Stage 6 all artwork/polish | M2 reference quality; M3/M4/M5 full coverage |
| Stage 7 extensive editor | M3, exercised on actual corpus in M4/M5 |
| Stage 8 cutover/qualification | M6 |

## 5. Measurable quality and performance gates

### Visual and interaction qualification

Use the specification's covering matrix: 720p/1080p/4K; 4:3, 16:10, 16:9,
21:9 and 32:9; 100/125/150/200% density; independent 100/150/200% text scale.
Include targeted small-viewport/large-text combinations, monitor changes,
nonzero viewport origins and world projection extremes. Record physical pixels,
logical viewport, display/user/text scale and input transforms separately.

For each screen, review source-specific silhouette, typography, spacing, contrast,
bitmap exceptions and every interaction/error state at normal size. Require
crisp stable strokes, clean holes and corners, no composition seams, no blurred
UI after world upscaling, no clipped essential text/actions and no distorted
circles/cuts. Use independent geometry/pixel checks for mathematical invariants
and human visual review for composition/fidelity. Similarity scores alone do
not prove artwork or usability.

Sample motion at 30/60/144/240 Hz, including irregular frame times, dropped
frames, pause, reversal and reduced motion. Verify continuity and exactly-once
commands separately. Navigation/press feedback must reach the next presented
UI frame without waiting for a transition to finish. Qualify visible response
on real supported hosts before claiming display/input latency.

### Performance and resource acceptance

First establish named baseline hardware, release build flags, backend, resolution
and workload. Record paired UI-on/UI-off runs in the same gameplay scene and
measure layout/state, vector preparation, engine submission, GPU UI execution,
frame pacing, draw/vertex counts, allocations and resident resources separately.
Use at least 1,000 measured warm frames per representative workload plus distinct
cold-open/restart/resize runs; preserve p50/p95/p99/max and repetitions.

Initial engineering budgets to ratify with those baselines are:

- Steady UI CPU and GPU work should each fit within 10% of the target frame
  interval at p95 and 20% at p99 on the selected support tier. At 144 Hz these
  are approximately 0.69 ms and 1.39 ms respectively. These are proposed targets,
  not claims about current performance or sums of CPU/GPU measurements.
- Warm stationary screens must not retessellate unchanged paths or rebuild
  unchanged text. Simple hover/page/opacity transitions must not create visible
  hitches, repeatedly allocate full documents or grow caches without bounds.
- Avoid synchronous cold preparation on an active navigation frame. Precompile
  and preload predictable screens/components; report cold work independently.
  A budget miss requires an explicit implementation/architecture decision and
  fresh evidence, not a claim that a debug benchmark is fast enough.
- Account for CPU meshes, compiled documents, font/image caches, composition
  targets and renderer-owned images separately. Enforce a configurable resource
  budget appropriate to the support tier, including transient peaks. Repeated
  open/close, language changes, resizing and 1,000 navigation cycles must settle
  to a bounded plateau and release resources on shutdown.

Optimize measured bottlenecks in this order: persistent indexed engine draws and
buffer reuse; dirty property/node lookup and state evaluation; visibility-aware
layout/text updates; cropped composition targets and cached masks; fractional
transform coverage. Evaluate a GPU coverage path or another vector strategy if
the current CPU coverage cannot meet the budget while preserving edge quality.
Keep editable vectors authoritative and retain independent coverage tests during
any renderer change. Raising the target cap alone is not an optimization.

### Verification layers and reproducibility

Native tests cover typed/transactional model operations, semantic compatibility,
layout/input transforms, ownership, interruption, bounds and recovery. Production
adapter tests cover actual application operations and backend contracts. Engine
tests enter relevant gameplay, drive internal state/events, read the configured
log and use registered `screenshot` output. Tests that only match implementation
text cannot establish behavior. Editor verification includes actual saved files,
reopening, recovered changes and exact-runtime previews.

Record engine/companion revisions, source/asset hashes, binaries, build options,
commands, renderer selection, log/image hashes and review outcomes. Keep raw
retail references/captures under `.tmp/`; publish reproducible procedures and
compact findings. Engine screenshots are mandatory; no OS capture substitution.
Tests remain windowed with input control scoped according to AGENTS.md. Use
canonical companion sources, standard Meson wrappers and `.install/` staging.

## 6. Final product audit

The complete implementation is accepted only when all of these are true:

1. Every effective GUI root/include and special widget has a qualified mapping;
   no unclassified syntax, dead button, inert field or required legacy route
   remains. Gameplay events, commands, saves and multiplayer behavior pass.
2. Every family has faithful professional artwork, full states and transitions,
   responsive layouts, editable vector furniture and reviewed complex-image
   exceptions. No diagnostic fixture is substituted for a production screen.
3. All specified editor capabilities work on real production documents, including
   complete source round trips, undo/recovery, components, vectors and timelines.
4. DPI, text, localization, accessibility, input and resource-lifetime behavior
   pass the recorded matrix in both game and editor.
5. Named release-build performance, memory and actual backend/platform results
   meet the agreed budgets. Windows-only captures do not qualify other platforms.
6. A clean staged installation works with retail assets, correct SP/MP modules
   and the intended minimal runtime dependencies/content. No development-only
   linker/editor artifacts or extracted proprietary artwork are shipped.
7. Player/editor documentation, credits, compatibility policy and release notes
   match the delivered product. All milestones are committed/pushed and the
   remote revisions are verified. Missing evidence keeps the product incomplete.

## 7. Unrelated issues and decision tracking

The MP qualification run retained **93 pre-existing content warnings, 86 unique**,
with an exact match to the preceding presentation-boundary capture and zero new
warnings. Keep these in the existing compatibility backlog; they do not become
UI regressions by being observed here, and they are not silently marked fixed.
SP reported no warnings/errors. No other unrelated defect was established in
this review.

Implementation decisions still requiring measured resolution are the editor
shell, shaping integration, optimized fractional-coverage strategy, final GPU
resource budget and external-mod/save migration policy. Resolve them at their
dependent milestones with alternatives, evidence and compatibility consequences.
None authorizes reducing the complete product, GUI corpus or editor scope.
