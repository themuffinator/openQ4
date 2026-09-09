# Retained GUI application integration

This checkpoint connects explicit `.q4ui` documents to normal engine GUI
allocation and session dispatch. It is part of M1 in the
[product completion plan](../plans/ui-product-completion.md). It does not accept
any of the 271 shipped GUI resources or complete the production settings page.
The [full requirement register](product-requirements.md) remains authoritative.

The method coverage and validation below describe commit `e075e4a2`. The
subsequent [presentation alias checkpoint](presentation-aliases.md) implements
writable exports and snapshot version 2, and addresses the Vulkan brightness
limitation identified by this earlier build.

## Loading and ownership

`FindGui` selects the retained implementation only for a complete,
case-insensitive `.q4ui` suffix. Other paths retain the legacy implementation,
including `.gui` and `.guied` editor sources. No implicit source alias or
fallback can mistake a failed retained load for a successful replacement.
Dedicated builds reject explicit retained documents without linking the client
layout/rendering library.

Pathless `Alloc()` returns a deferred public interface. Its first successful
`InitFromFile()` chooses an unregistered child backend. The wrapper is the
manager-owned object; its child never acquires a second allocation or loaded
registration. Caller state, cursor, uniqueness and explicit interactivity survive
selection and replacement. Failed replacement retains the live backend. Reload
copies the source name before modifying the storage it may reference.
Replacement reads activation and explicit interactivity from the live backend,
including after a save restore; pre-load wrapper flags cannot overwrite restored
state. Legacy interactivity remains parse-derived because its save format has no
explicit override provenance.

The retained adapter preserves the public Game API 48 contract. Renderer API 13
is unchanged. All stock paths still select legacy code; the existing source
mapping and old-save migration gates remain open.

## Public method coverage

| Contract | Implemented scope | Remaining product work |
| --- | --- | --- |
| Name, metadata, uniqueness and ownership | Normal manager allocation, explicit source selection, teardown and staged replacement | Qualified production mappings and full level/resource matrix |
| `State`, setters and `StateChanged` | Opaque caller dictionary; atomic typed commits for declared application values; host CVars remain read-only sources | Translated aliases, game objects, lists and complete binding semantics |
| `HandleEvent` and command dispatch | Semantic button input through normal session events; typed queued operations; unavailable-view dismissal | Full widget/IME/device transport and platform qualification |
| Activation, named events and trigger | Authored timelines named `onActivate`, `onDeactivate`, `onTrigger`, or the exact named event | Complete ordered event/script operations and legacy lowering |
| Presentation query | Read a declared node/property through `node::property`; output unchanged on failure | Writable aliases and expression override ownership |
| Text/caret queries and key-binding names | Explicit unsupported results/no-op; no editable widget is claimed | Full text editing, IME, wrapping, binding widgets and prompts |
| Redraw and cursor | Root UI viewport, density-aware vector submission, inverse legacy cursor transport and a generated vector pointer | Explicit HUD/world output contract and complete cursor styling |
| Save/restore | Bounded versioned frame around pending caller state and committed canonical snapshot | Real game save container migration, missing-resource skip/recovery and demo qualification |

The false/default responses above are explicit unfinished contracts. Merely
implementing every virtual method does not satisfy RUN-001.

## Typed application operations

Canonical documents may declare an `actions` object. Each stable action ID maps
to an operation ID and typed expression arguments, for example:

```json
"actions": {
  "applyBrightness": {
    "operation": "settings.brightness.set",
    "arguments": { "value": { "state": "brightnessTarget" } }
  }
}
```

`DocumentModel::ResolveAction` evaluates a complete invocation transactionally;
an error leaves its output untouched. Resolution has no side effects. The
canonical compiler validates expression types and budgets; the engine adapter
then rejects unsupported operations, argument names/types and unresolved control
actions before replacing a live GUI. Preview documents can still expose opaque
action IDs for isolated runtime diagnostics.

The first fixed application catalog is intentionally small:

| Operation | Arguments | Host behavior |
| --- | --- | --- |
| `settings.brightness.set` | One finite numeric `value`, 0.5–2.0 inclusive | Set archived `r_brightness`, then verify live readback |
| `settings.shadows.set` | One Boolean `value` | Set archived `r_shadows`, then verify live readback |
| `ui.dismiss` | Empty object | Ask the owning session to close the current view |

These are actual renderer settings used by the existing system page. They do
not require video restart. They are not a substitute for its 28 choices, three
sliders, six numeric edits, display catalogs, Auto Detect, apply/revert and
failure-recovery flows.

At this checkpoint's `e075e4a2` revision, brightness dispatch/readback worked on
both renderers, but Vulkan lacked the planned
[H3 final brightness/gamma pass](../plans/2026-07-22-vulkan-phase-h.md).
Its visual effect was therefore qualified only on OpenGL. The subsequent
[presentation alias checkpoint](presentation-aliases.md) implements that pass
and records SDR screenshot parity; full display-settings acceptance remains open.

The public command string carries only a fixed queue marker. The manager checks
the public interface's allocation identity, then delegates to its private backend.
Only queued typed invocations from that instance can mutate the host. Operation
IDs and string arguments never become console commands or arbitrary CVar names.
The session consumes these requests before selecting legacy/game command
handlers. Each invocation validates before its own mutation; separate successful
requests retain normal sequential semantics.

## State, update order and input

Public setters preserve the caller's dictionary without implicitly committing
typed state. `StateChanged` converts declared application values according to
their authored types and commits the entire batch, including all derived bindings,
or retains the previous valid presentation. Undeclared caller keys survive.
Canonical state IDs cannot differ only by case or use the reserved `name` key,
because the game dictionary is case-insensitive and the engine owns source names.

Normal session frame events deliver `SE_NONE`; the adapter advances input repeat
on the steady presentation clock, routes semantic actions and drains button
activations. Redraw updates layout/presentation and hit geometry through the
shared runtime. Mouse transport remains the existing 640×480 aspect-corrected
game interface: the adapter converts it back to physical viewport coordinates,
then to window units expected by the retained runtime. Density is applied once.

Deactivation, console/focus suspension and resource reset quarantine held input,
discard transient requests, and clear runtime latches. Closing/replacing a session
GUI explicitly deactivates it. `guiTest` also inhibits gameplay commands and owns
its Escape close path. The SP frame gate uses `IsGUIActive()`, so a normal test
GUI pauses simulation until it closes; MP simulation continues. Back first pops
a retained modal; it dismisses the session
view only when no modal remains. If a resource fails to restore, Escape/controller
Back remains able to dismiss the unavailable view.

Test loading stages a unique instance and retains the current test on load
failure. Replacement/close releases the previous test without deactivating an
installed active menu, even if an older caller left those pointers aliased.

Semantic diagnostics operate on a normal manager-created active/test GUI through
`openq4_retainedGui`. They focus controls, submit menu actions, inspect results,
set caller state and exercise saves without reading or injecting device input.
They do not establish physical device qualification.

## Save framing and pending caller input

The adapter writes a little-endian `Q4UI` tag, version 1 and bounded payload length,
then the caller dictionary, persistent flags/cursor and canonical instance
snapshot. The payload has at most 4,096 dictionary pairs, 64 KiB per string,
16 MiB aggregate dictionary text, and the core's 128 MiB snapshot limit. A complete
payload is validated/staged before writing to the caller's stream. Strings reject
embedded NUL, duplicate case-insensitive keys and invalid framing on read.

The dictionary and snapshot represent two deliberate points in the public
contract. A caller can call `SetState*` and save before `StateChanged`. Restore
therefore preserves pending dictionary input separately from the last committed
typed/presentation state. An invalid pending typed value remains pending; the
next `StateChanged` rejects it exactly as it would before saving. Restore does
not silently commit pending data, restore host CVars or replay queued actions.

Reads validate a complete bounded frame before changing the live instance;
snapshot failure leaves the dictionary, flags and presentation untouched. The
diagnostic checkpoint appends a sentinel and verifies the following record is
still aligned after restoration. This is not a claim about the surrounding game
save container: its GUI name/unique header and missing-resource behavior still
need explicit migration and real SP/MP game-save tests.

## Shared resource lifetime

Each manager adapter and the preview register an independent stable Runtime with
one engine host. Invalidation snapshots all loaded views, quarantines callbacks,
shuts down every context, resets shared resources once, reloads every source and
restores all snapshots against one shared presentation timestamp. One failed
view stays unavailable without invalidating successfully restored peers. Closing
the preview leaves other views alive.

Root drawing selects the engine UI viewport and restores the caller's viewport
selection. This route must not be used while collecting geometry for a world
surface. The adapter rejects that draw request explicitly. Front-end submission
boundaries protect already queued root drawing from a same-frame host reset;
renderer GPU retirement and world composition remain separate qualification.

## Validation

Validated on 9 September 2026 against engine baseline
`b87b703b799cbf120d74b4aed122883a42b0ad68` and unchanged companion
`1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. The Windows x64 debug client,
dedicated server, both renderers and both game modules built and staged through
the standard wrapper. Four native retained suites and fourteen focused
production/capture suites passed. These cover actual manager/deferred methods,
typed adapter dispatch, corrupt/truncated saves and pending state, shared-view
reset lifetime, unavailable-input quarantine, test-session ownership, presentation
compatibility and
the capture oracle's nine positive/negative cases. CI runs the new harnesses.

Both gameplay runs used hidden windowed rendering, disabled mouse/controller
input, semantic engine diagnostics and the engine `screenshot` command. No OS
input or screen capture was used. The authored two-button fixture exercised
normal `testGUI` loading, typed settings mutation/readback, save/restore without
action replay, focus continuity, language reload, full video restart and
independent peer closure. It is not an accepted production screen.

| Gameplay capture | Result |
| --- | --- |
| SP/OpenGL, `airdefense1`, 1280×720, 125% | All integration checks pass; no warnings/errors. Game time stays fixed while the GUI is active and advances after closing. |
| MP/Vulkan, `q4dm1`, 1920×1080, 200%, explicit auto-join | All integration checks pass; simulation advances while open. No errors; 94 warnings, with exactly the same 86 unique messages as the previous MP checkpoint. Brightness visual acceptance remains blocked as described above. |

Engine images were inspected after lossless conversion to PNG: localized
Settings/Brightness/Enable Shadows labels, live `1.25`/YES values, vector framing
and restored shadow-button focus are legible and unclipped at both tested scales.
The shared context count remains two through resets and drops to one when the
preview peer closes. Restoration preserves committed and pending GUI values
without reverting live host CVars.

Exact source, binary, configuration, log and image hashes are recorded locally
in `.tmp/ui/managed-review/summary.json`. The client SHA-256 is
`78317e098fe12342121ead14b2b79a3c62695a7901506827d86392c8e02d7532`.
The SP and MP engine TGA hashes are respectively
`ec29bd86dadfd71d63e1ea9940c16406ec05f366461fe895d596b032cefa3843`
and `50abc4ef450abf6daee6e22e891b66bb27e10513fc12db70df03d625e41d3749`.
These debug fixture results do not establish release performance, physical
device behavior, Linux/macOS operation or GPU resource retirement.

## Remaining work and unrelated findings

M1 remains open for the full public-method matrix, explicit world/output surface
contract, complete events/aliases, source mapping, real game save/demo framing and
broader device/resource failure qualification. M2 must deliver the actual complete
system settings screen and native editor round trip. No production GUI is accepted
by this checkpoint; all artwork, control/editor, game and platform gates remain.

The existing `idStr::CheckExtension` implementation fails to compare the first
character of the requested extension. New retained routing uses an exact checked
suffix helper; unrelated callers have not been changed. The existing build
wrapper still requires numeric arguments such as `-j '8'` to be strings in
PowerShell. Both issues are outside this UI checkpoint's broader repair scope.
The unchanged MP warnings include vertex-array placement and missing AAS files.
Vulkan's missing final brightness/gamma mapping predates this integration:
uniform authored RGB samples are `(80,96,111)` on OpenGL and `(64,77,89)` on
Vulkan at brightness 1.25/gamma 1. OpenGL applies the final mapping in
`RB_ApplyColorMappingsToBackBuffer`; Vulkan's native gamma setter is empty and
its present path has no equivalent pass. This limits settings acceptance, rather than the
typed action/readback result.
