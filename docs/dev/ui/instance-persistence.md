# GUI ownership and retained instance persistence

This is a foundation checkpoint inside M1 of the
[product completion plan](../plans/ui-product-completion.md). It establishes
manager ownership independent of legacy windows and durable state for the
currently implemented canonical document subset. It does not complete M1,
replace a shipped GUI, or establish game save/demo compatibility.

## Manager ownership

The engine-private `idUserInterfaceManaged` contract supplies source metadata,
reference counts, menu lifetime, memory/transition reporting and background
thinking. `idUserInterfaceLocal` implements that contract using its existing
window tree. Game modules retain the public `idUserInterface` contract; Game API
48 and renderer API 13 are unchanged.

One allocation registry owns all managed objects, including an `Alloc()` result
that has not loaded a resource. Loaded, background-thinking and demo lists are
non-owning subsets. Destruction unregisters from every list, including direct
editor deletion. Shutdown drains the ownership registry before shared retained
resources are destroyed. Level purging respects menu lifetime, references and
material `GlobalGui()` references. Reload copies the resource path before
initialization can replace its storage.

These changes repair stale registrations left by direct editor deletion and
unowned, uninitialized debug/demo allocations. They also remove manager
dereferences of legacy desktops. The existing editor continues to receive its
concrete legacy implementation for `.guied` sources. Pathless allocation and
normal loading still construct the legacy implementation at this checkpoint;
the separate retained adapter and deferred resource selection remain M1 work.

## Snapshot contract

`Runtime::SaveSnapshot` writes version 1 of `openq4-ui-instance` to a bounded
UTF-8 JSON string. `RestoreSnapshot` validates a candidate before changing any
live state. Failure preserves the existing instance; save failure preserves the
caller's previous output. The envelope is limited to 128 MiB, rejects duplicate
keys and unsupported fields/schema, and validates the document identity and
every restored state and presentation value.

Identity includes the canonical format version, document ID, VFS source path
and exact source bytes. The source must already be loaded: a snapshot cannot
load code or substitute a different document. Embedding source bytes avoids an
ambiguous identity in this initial format but increases snapshot size. Shared
compiled resources should supply a compact, versioned resource identity before
the full corpus is qualified. Raw RML previews have no snapshot contract.

The implemented persistent state includes:

- All declared application values, with their declared types and ownership.
- Timeline owners, retarget values, elapsed phase, pause/repeat progress and
  reduced-motion presentation, reanchored to the receiving monotonic clock.
- Focus, nested modal scopes and their return focus, and unbound enabled
  overrides for the implemented button controls.

CVars remain authoritative host data. Restore reads them afresh and evaluates
their values together with saved application state in one transaction. A CVar
cannot be restored as application-owned state. Changed availability can clear
an invalid focus and retarget its feedback while preserving unrelated motion.

Pending actions, hover and armed presses are transient. Saving sanitizes copies
of interaction/motion state, preserving continuous feedback toward the persistent
state without changing the live instance. Restore clears action queues and press
ownership, so a later unmatched release cannot activate a control. Input routing
also quarantines held physical sources when the engine restores a checkpoint.
After quarantine, the adapter clears logical held latches without changing
restored presentation. Releases queued under an earlier ownership generation
can retire only quarantined sources, so the first fresh press is not swallowed.

The reserved widget-state object must currently be empty. Text edits, lists,
sliders, script clocks and other future widget state are not silently accepted
as supported persistence. Game saves still use their existing positional legacy
payload, and render-demo GUI payload writing remains disabled. Both require
explicit backend framing/migration work before retained source routing changes.

## Engine resource lifecycle and diagnostics

The canonical preview uses the complete snapshot across renderer and language
generation changes, replacing its previous application-values-only restore.
Generation checks run before document loading as well as drawing, including
when a view was closed while the renderer changed. Closing a view releases its
runtime without resetting host-owned font/target caches; shutdown and generation
invalidation own those bounded resources.
The shared dictionary explicitly notifies this boundary after loading. A
same-language reload or a switch between languages using the same code page
therefore invalidates retained text/fonts as well.

The current engine host has one preview context. Before manager-owned retained
views are connected, invalidation must capture every affected instance, shut
down all affected contexts, reset the host once, and restore every view. World
GUIs also require an explicit output-surface contract: their geometry collection
cannot use the fullscreen preview submission path.

`ui_retainedCheckpoint save|restore` provides one bounded in-memory diagnostic
slot. It never writes game saves or dispatches actions. Closing the preview
clears the slot. The capture harness accepts a separate
`--retained-resume-script` for checks after `vid_restart`; unlike the default
fixture replay, this can prove that state survives a restart.

## Qualification

All four native suites pass: retained runtime, canonical documents, state and
vectors. Snapshot cases include exact source identity, malformed/duplicate JSON,
wrong ownership/types, failed binding evaluation, current host values,
active/paused/retargeted/repeating/reduced motion, nested modal return focus,
held input and large-clock rejection. Combined `Runtime`/`Input` checks verify
that an old press/release cannot activate after restore and the first fresh
action works once. The production transport test additionally covers stale
keyboard and pointer releases across ownership generations.

The extracted production manager tests cover uninitialized and failed loads,
direct editor deletion/reopen, loaded/demo membership, shared/unique instances,
menu references, material retention, self/peer deletion, address reuse during
thinking/reload, and shutdown. Production cursor, presentation, window-state,
CVar-source, layer-pool, input and startup-language checks pass. The manager
test is included in both script-validation workflows.

The first SP/OpenGL gameplay capture passed but adding full video restart to
the MP/Vulkan capture exposed an existing direct call to OpenGL initialization
from the full restart helper. The failed evidence remains under
`.tmp/ui/persistence-review/mp-vulkan-200/`; it is not accepted. The restart
helper now uses the active renderer's initialization path and reloads images
once, through the same backend-aware initialization used at startup.
`renderer_vid_restart_route.py` compiles the production restart and startup
methods for both GL and Vulkan, checking repeated restarts, window preferences,
resource ordering and exactly one image reload. The existing font-lifecycle
contract and mutation checks also pass. Both validation workflows run the new
route test.

Final captures on 9 September 2026 used the staged Windows debug build, hidden
windowed mode, disabled host mouse/controller input, and the registered engine
`screenshot` command after actual map gameplay. Both passed trace/provenance
verification and visual review. Each run restored one 94,317-byte in-memory
checkpoint, reloaded the same language, restarted the renderer, retained modal
focus/availability and restored the prior focus when the modal was popped.
No pending actions survived. SP stayed paused and resumed on close; MP continued
simulation while the interface owned input.

| Evidence under `.tmp/ui/persistence-review/` | Configuration | Result |
| --- | --- | --- |
| `sp-gl-125-r2` | SP `airdefense1`, OpenGL, 1280x720, 125% density | Exit 0; zero warnings/errors; snapshot/ownership checks pass |
| `mp-vulkan-200-r2` | MP `q4dm1`, Vulkan, 1920x1080, 200% density, explicit auto-join | Exit 0; zero errors/retained diagnostics; snapshot/ownership checks pass |

MP reports 94 warnings with the same 86 unique messages as the preceding
checkpoint. Compared with its 93-warning run, the full renderer restart repeats
the existing `vertex array range in virtual memory (SLOW)` warning once. There
are no new unique warnings. These content/backend diagnostics remain open.

Client SHA-256 is
`6d37792aa468b494c289ee67f18bd6a048ab6918a8e2e31b2cc3cd2853a41d31`;
the renderer module hashes are
`18812bde2a30a8bebee9d0a373821c9cbef69f1fc5aa4f027054b33f931da383`
(GL) and `369598d94f1e4ea226e7e0cdbcceb028202361ff029e52fbdcd0a8aa10296214`
(Vulkan). The unchanged companion source revision
is `1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. Screenshot hashes, respectively:

```text
93306463008e27d1bfa7782b9b6164b3c475d7f73ee614afb3f23b9060171890
f29f785719ffb2e3899f9186b05c4e90a0f2eaf531b5393580ef9a71be46b527
```

The local `summary.json` binds source and binary hashes, capture metadata,
oracle results and warning comparisons. These are existing preview fixtures,
not a finished settings screen, game save round trip, world surface or native
editor. Shared GUI was requested but reported zero owned views: 20 SP and 18 MP
views used its fallback path. No shipping performance claim is made.

Reproduce with `capture_legacy_baseline.py`, the `interaction-smoke.q4ui`
document, `snapshot-smoke.cfg` setup and `snapshot-resume.cfg` resume script,
`--retained-open --language-reload --video-restart --shared-gui`, the table's
mode/backend/dimensions/density, installed assets, and a fresh output folder.
Run `verify_snapshot_capture.py <capture-folder>` and review the engine TGA.
The verifier checks exact source/script and artifact hashes, generated command
order, fresh engine-log traces and ownership timing; setup is never replayed
after restart to manufacture the expected state.

The Windows Meson wrapper also requires numeric arguments as strings when
called from PowerShell (for example `-j '8'`); an unquoted integer reaches a
string-only argument scanner. This existing tooling issue was worked around,
without changing the wrapper.

The full GUI corpus, native visual editor, platform matrix and shipping
performance gates remain required by the product plan. No external
implementation code is incorporated.
