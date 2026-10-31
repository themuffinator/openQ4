# SYSTEM interface size preferences

17 September 2026. Increment on `idtech5-ui`, based on engine
`85c011754b800409e5e471b454ac4d658b6f3d00` and companion
`a4946f8b28c53bb32ec323bddcf0e2276bc8492c`.

The experimental SYSTEM page now offers UI scale (75, 100, 125, 150 and 200%)
and independent text size (100, 125, 150 and 200%). Both default to 100%.
Choices edit the existing settings draft; Apply commits them without restarting
the renderer. Reset UI sizes sets only these two draft preferences to 100%,
preserves other unfinished settings, and still requires Apply. Discard preserves
the previously applied sizes. Unfinished numeric text blocks these operations
through the existing draft guard.

All six repository languages have labels, the reset action and an explanation
that large sizes may be reduced to fit the window. The controls reuse the
existing editable vector choice/button components, on the SYSTEM page's
GRAPHICS tab. `tools/ui/retained_system.py` generates the page, and
`build_retained_screens.py --check` compares it without writing.

## Window fitting and persistence

Root-menu viewports fit their requested density to a minimum logical area of
640×480 dp. `Viewport::fitScale` is separate from display density, user scale
and text scale; the effective dp ratio includes this factor. Fitting uniformly
reduces the whole root composition and retains independent text enlargement.
Growing the window restores the requested size automatically. The archived
percentages stay unchanged, and the controls display those requested values.
Incoming window coordinates still map directly to physical document pixels;
layout, vectors, clipping and hit testing share the effective density.

This is an explicit safety limit for the default root-menu viewport. Generic
editor/surface viewports retain their existing density unless their owner opts
into fitting. Full editor and independent HUD/world-surface scale policy remain
open. A 640×480 physical window is the smallest qualified size; this increment
does not claim usability below it.

The SYSTEM catalog grows from 53 to 55 fields. Both preferences permit finite
continuous values within their supported ranges and are immediate effects.
The original 53-field effect catalog remains immutable as version 1; new effect
plans use version 2. The schema-1 display recovery reader accepts either the exact
current catalog or the exact old catalog. When reading an old record it extends
both snapshots with freshly read current size preferences and leaves the owned
patch unchanged. Original journal bytes remain authoritative through retirement.
Malformed or partial catalogs, conflicts and changed evidence still fail closed.
The generic effect-journal codec retains explicit version-1 decoding with its
old catalog; this does not add effect-journal startup execution to the engine.

## Qualification and remaining scope

The Windows x64 MSVC debug build and release-style staging pass. All 84 UI
regression suites pass, together with two separately executed production-page
matrices: 34,032 interface preference checks and 205,453 text-layout checks.
That completes all 86 registered UI suites. The interface matrix uses the actual
production document, Runtime, RmlUi and transaction source with counted font/CVar
boundaries. Its six locales and 40% wider glyph fixtures exercise Apply, reset,
unrelated drafts, guarded actions and live resize at combined 200% display/UI/text
settings: 48 extreme-size cases including 640×480 physical windows. It also
selects and applies 75% UI/100% text from the largest small-window layout.
The existing text matrix covers 84 viewport/locale/width combinations and all
eight service status messages. These counted metrics do not establish real-font
localization acceptance. Their Runtime/test sources were unchanged by the later
journal-only correction and final relink; their pre-relink executable hashes were
not recorded. The final 84-suite regression run uses the rebuilt executables.

The actual host passes range/type/default and immediate-effect classification
checks in SDL3 and non-SDL3 configurations. The display recovery service passes
430 checks, including old 53-field Pending/Confirmed records preserving current
size preferences through qualified startup and original-byte retirement.
The Clang effect-journal suite passes 8,122 checks and rejects 21 mutations.
MSVC debug passes 5,073 checks. Owned empty string/vector construction now permits
allocation refusal to propagate to the wrapper rather than terminate inside
`noexcept`. JsonCpp's existing debug-STL default-string proxy allocation still
prevents an exhaustive decoder-denial sweep on that configuration: it tests the
first wrapper allocation, while Clang/release STL covers all decoder sites.
Ordinary decoding, corruption, ownership and encoder denial run in both.
All six authoring generators pass `--check`; 39 source/capture tests pass,
including 1,224 rejected capture mutations.

Six fresh staged engine processes qualify the normal SYSTEM Session route after
active SP `game/airdefense1` and MP `mp/q4dm1` gameplay: French/OpenGL and
English/Vulkan each run Apply, a small-window reset/Discard/Apply sequence, and
a final restart/readback. No process supplies a UI/text-scale launch override.
The 16 semantic stages compare draft, baseline and actual live CVars, exact
transaction operations, archived configuration and effective fitted density.
Drafts preserve the live values; Apply archives 200%; restart reads those values
at 640×480; Discard preserves them; reset plus Apply archives 100%; the final
restart reads 100%. The 200% requested display/UI/text combination fits to 1 dp
per physical pixel at 640×480, and requested percentages remain intact.

All 16 engine-render-target screenshots were visually reviewed, with full-size
small-window checks. The reset and footer decisions remain complete and
localized labels wrap. PNG previews preserve raw TGA RGB pixels exactly. Runs
were hidden and windowed with host input disabled. Each French run retains the
same six font-material precache warnings. Each MP run retains 93 existing
asset/gameplay warnings, with no additions against the earlier 94-warning record;
these runs do not reproduce its restart-time virtual-memory warning. The build
still repacks unchanged `pak1.pk4`; that iteration-cost issue is outside this UI
increment. The companion repository remains clean.

Source, test, command, binary, archive, warning and image evidence is recorded in
`.tmp/ui/scale-preferences/validation-evidence.json`; visual review is in
`.tmp/ui/scale-preferences/visual-review.json`. These are bounded SYSTEM states,
not full screen or product acceptance.

The initial extreme-size case exposed a reset taller than its scroll viewport;
the widened French fixture then demonstrated why a 360 dp height floor was
insufficient. Final fitting uses the established 640×480 minimum instead.
The first engine run's 640×480 images were valid, but a reused capture validator
assumed 1280×720. That run remains unaccepted; the corrected harness verifies
decoded image dimensions and the final six processes were recaptured.

`LAY-003` and `LAY-004` remain partial. Complete font services, native editor
operation, all GUI families, every real-font state and supported-platform
qualification remain required. All 227 requirements, 271 unaccepted migrations
and seven open final gates remain in scope. The earlier
[text-scale evidence](text-scale.md) records its own historical source/binary
state and is not rebound to this increment.
