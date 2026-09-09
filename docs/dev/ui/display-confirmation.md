# Display confirmation and settings recovery

This implements the display transaction portion of the [SYSTEM contract](system-settings-contract.md)
and M2 of the [complete UI product plan](../plans/ui-product-completion.md).
It does not accept the production SYSTEM screen, the 271 GUI migrations, editor
completion, or the final platform and visual-quality gates. The requirement
register remains authoritative for those deliverables.

## Application flow

`SettingsService` admits an Immediate + DisplayRestart batch only when the
owning canonical document provides the confirmation contract below. Image
reload, audio restart, renderer-resource, next-map and preset-expansion effects
still reject the entire batch before any live write. The 53-field catalog and
existing immediate-only path remain in use.

`SettingsTransaction` separates preparation, CVar execution and completion.
Preparing freezes the original catalog, candidate and owned patch without
changing live state. Each Apply, Restore and Keep preparation receives a new
process-wide request ID. Owners and requests are never recycled. A string
transports request identity through UI state without double-precision loss.
Old request-bearing actions are rejected even after the owner opens a new draft.

`SettingsDisplayController` queues work for the main engine frame. It never
restarts a renderer from an action, a view destructor, or a nested loading frame.
The engine adapter obtains an exclusive settings-file lease and a window
placement lease, captures actual display policy separately from archived intent,
and durably records recovery before executing the CVar patch. A failed callback
can have partially succeeded; it retains recovery ownership instead of retrying
or rolling back blindly.

After strict device creation and resource reconstruction, the controller checks
the actual requested parameters, module epoch, device generation and presentation
failure counters. The owning view must redraw successfully with an eligible
Revert control inside a tracked engine frame. Its receipt is accepted only after
that exact synchronous `EndFrame` returns with one successful submission and one
successful API presentation, unchanged device identity and no new failure.
Aborted, nested, skipped and capture-only frames cannot acknowledge the dialog.
Historical Vulkan readback submissions may make the two totals unequal; the
receipt checks this frame's increments instead of requiring equal totals.

The first receipt remains fixed for its owner/request; one subsequent successful
presentation then qualifies confirmation. Repeated redraws cannot move the
threshold indefinitely. This depends on the current synchronous GL/Vulkan
backend scheduler. A future asynchronous backend must carry explicit frame tags;
independent counters would not establish which frame was presented. API success
does not prove physical scanout. Device/presentation qualification has a 20-second
deadline; the separate 15-second Keep countdown begins after qualification.
Initialization and CVar echoes alone cannot arm Keep.

Keep rechecks the frozen live state and actual device before durable persistence.
Revert, expiration, visible-window focus loss, minimization or owner closure
restore the original actual display and only the CVar keys still owned by the
attempt. Unrelated external changes survive. Divergent owned values become an
explicit conflict. Restore completion also needs a fresh successful presentation.
Nested loading frames observe deadlines but perform no device or file work.

Recovery never retries each frame. Retry is a distinct action. In particular,
Revert cannot become a retry of an approved Keep when saving fails; the UI shows
that the accepted settings still need to finish saving. A failed device leaves
drawing suspended until a usable device is available.

## Confirmation document contract

In addition to any draft/baseline fields, a qualifying document declares these
reserved read-only service values with their exact types:

| State | Type and meaning |
| --- | --- |
| `settings.request` | String; opaque current request, empty when none is pending. |
| `settings.confirmationVisible` | Boolean; confirmation or explicit recovery must be shown. |
| `settings.canConfirm` | Boolean; Keep is currently eligible. |
| `settings.canRevert` | Boolean; the pending choice can be restored. |
| `settings.canRetry` | Boolean; a failed recovery/finalization can be retried explicitly. |
| `settings.remaining` | Number; remaining qualified confirmation seconds. |

Three labeled controls have the IDs `settings_keep`, `settings_revert` and
`settings_retry`. They directly reference `settings.system.confirm`,
`settings.system.revert` and `settings.system.retry` actions respectively, each
with its sole `request` argument bound to `settings.request`. They use the
corresponding eligibility states. Their controls and prompt must be visible,
readable and navigable in the actual confirmation layout. Capability validation
is necessary, but does not replace visual and accessibility review of that layout.

The canonical compiler represents a state expression by an empty operation and
its `state` field; tests compile the actual document before checking capability.
Arbitrary literals, presentation aliases and malformed expressions cannot claim
this capability. On resource recreation, the same owner survives and the
application does not replay its initialization/actions.

Progress, prompt and Keep/Revert/Retry labels are localized in all six current
language tables (`#str_229990`–`#str_229997`). Numeric dimensions, request IDs and
device names are data. The [diagnostic fixture](https://github.com/themuffinator/openQ4/blob/e3e65887480368191df154a9b52cce69d8e72140/tools/ui/fixtures/display-settings-smoke.q4ui)
uses editable first-party vectors and the established 45-degree framing; it is
not a substitute for the complete production settings component set.

## Durable storage and crash boundaries

The sole journal is `fs_savepath/baseoq4/ui-settings-recovery.dat`; its permanent
inert lock is `.settings-recovery.lock` in the same directory. Reads use these
exact native paths, never VFS/PK4 search or console execution. All writers follow
the same nonblocking cross-process lease. The save root remains pinned through
commit. Normal configuration writes refuse any outstanding journal, including
corrupt records, and retain archive-dirty state on failed serialization or I/O.

The bounded journal uses a canonical typed schema, complete catalog snapshots,
an exact changed-key patch, a random persistent attempt ID, actual restoration
and intended display records, placement metadata, and Pending/Confirmed state.
A CRC detects accidental corruption; it is not authentication. Unknown fields,
schema changes, inconsistent snapshots, invalid metadata and conflicting current
values fail before replay. Outputs remain unchanged on decode failure.

| Durable boundary reached | Recovery behavior |
| --- | --- |
| No Pending journal acknowledged | No display/CVar execution is permitted. An uncertain journal publication must be resolved explicitly. |
| Pending journal | Restore the owned original settings and recorded actual display. Unconfirmed choices must not reach configuration. |
| Confirmed journal | Finish saving the approved choice. A failed config write or journal removal retains recovery. |
| Config committed, journal cleared | Release the transaction and native leases. |

`DurableFile` uses checked exact-file reads, adjacent exclusive temporary files,
checked writes, filesystem/device flush acknowledgments and durable namespace
operations. Windows uses native sharing locks and write-through same-directory
replacement; POSIX uses `flock`, file sync and parent-directory sync; macOS also
requests `F_FULLFSYNC`. Failure after publication can mean bytes are visible but
durability was not acknowledged. No code equates a buffered flush with durable
publication or claims protection against lying hardware.

Configuration is serialized to a bounded checked memory sink before replacement.
Formatting truncation, short writes and optional version compression failures
prevent publication. The public `idCommon` ABI stays unchanged. Window x/y,
window dimensions and the normal-placement cache remain leased throughout
confirmation and restoration; external changes are compared before setters,
including recovery from an owned partial setter failure.

These comparisons detect divergent current values. The CVar interface has no
mutation serial, so an external write that returns a value to its original state
cannot be distinguished. The engine's normal-placement cache also cannot prove
fidelity of a compositor's hidden restore rectangle, particularly across process
restart; that visible-window qualification remains open.

## Startup and teardown

Startup recovery runs after config, initial command-line settings, migrations and
platform preferences, before initial device creation. Deferred startup commands
can subsequently reassert explicit user settings. Recovery tests therefore omit
window-dimension overrides on the second launch and obtain those values from the
archive and journal; replaying baseline `+set` arguments would change the saved
result after recovery. A Pending record resolves
only its restoration display against current hardware; a Confirmed record
resolves only its intended display. Both historical records are still validated
against each other and the catalog. A disconnected unused monitor cannot block
an otherwise valid recovery.

Persistent display descriptors contain names, bounds and desktop pixel modes,
not process-local SDL IDs. Resolution requires a unique matching configuration;
this is not a hardware serial/EDID identity. Explicit numeric display selections
are remapped to the current resolved index. A second startup accepts that same
owned remapping after a preceding config commit and failed journal cleanup.
Missing or ambiguous required hardware retains the journal instead of selecting
an arbitrary monitor.

Renderer-private ABI **15** adds strict first-device initialization. It uses the
same exact backend request policy as recovery, without startup mode fallback or
the world/font/session restart tail. Renderer initialization, actual state and a
fresh presentation must be observed before startup configuration/journal cleanup.
Game API remains **48**. Shutdown does not pretend to have verified restoration;
unfinished durable recovery survives the process and is handled at next startup.

## Qualification limits

Native suites exercise the real transaction/controller, catalog, display-record
compiler, journal codec, application service and checked config methods. Counted
platform/failure callbacks cover partial writes, stale identities, deadlines,
exceptions, conflicts, cross-process exclusion and each persistence boundary.
Separate native filesystem tests perform real Windows and Linux/WSL I/O and
process-lock/crash-release operations. These are not power-cut experiments.

The current Windows checks include nine native UI suites, 592 controller checks,
313 production display-host checks, 44 application-service/frame scenarios,
216 client and 220 dedicated/version configuration checks, and 280 native
filesystem checks. The earlier 273-check WSL filesystem run predates the final
Windows `min` macro compatibility adjustment; it is retained as historical
evidence, not a current Linux engine qualification. Windows symlink-creation
coverage was unavailable without the host privilege; the earlier WSL run skipped
FIFO behavior on DrvFS. No macOS runtime or power-cut acceptance is claimed.

`openq4_retainedGui inspect <node-id>` reports current node bounds and canonical
opacity/display plus explicitly view-wide vector statistics. It performs no
input or frame advancement; aggregate counters do not prove an individual path
was visible. The display probe records five such observations alongside its
screenshots. Its controlled initial sample count is zero: an inherited MSAA
preference can otherwise request an unsupported Vulkan framebuffer, which the
strict path correctly refuses and restores. Production requests are not coerced.

The configuration durability work does not change the pre-existing escaping of
quotes/newlines in archived CVar and binding text. Serialization completeness
checks should not be read as an acceptance of every legacy text round trip.

Gameplay qualification uses isolated saves, explicit SP/MP profiles, hidden
windowed GL/Vulkan devices, semantic application actions, actual presentation
traces and engine-written screenshots. Hidden diagnostics do not qualify visible
focus behavior, interactive fullscreen, multi-monitor movement, other platforms,
professional production artwork or the final UI product. Those remain explicit
acceptance work in the complete plan.

## Recorded Windows checkpoint

The final build and staging completed successfully. All 8,773 recorded compile
inputs and seven staged binaries remained unchanged through qualification; the
companion game repository remained clean. The inspected display probes exercise
128 ordered state reads, two actual display changes, Keep and Revert, and five
resource restorations in each SP/OpenGL at 200% density and MP/Vulkan at 125%
density run. Exact owner-frame and subsequent presentation traces precede Keep.
Engine screenshots show readable prompts and complete vector framing.

Four separate two-process recovery cases then passed with the same final runner:

| Profile | Journal supplied to cold startup | Recovered window and archive |
| --- | --- | --- |
| SP / OpenGL | Real Pending journal from unconfirmed quit | 1280 × 720 |
| SP / OpenGL | Injected Confirmed marker and checksum | 960 × 540 |
| MP / Vulkan | Real Pending journal from unconfirmed quit | 1280 × 720 |
| MP / Vulkan | Injected Confirmed marker and checksum | 960 × 540 |

All eight processes exited successfully. Each cold startup recorded four fresh
submissions/presentations with no presentation failures before recovery finished;
two later gameplay observations showed another six successful presentations.
Each case preserved the correct archive and retired the journal. SP emitted no
diagnostics. Each MP case emitted 94 then 93 previously reviewed stock warnings,
with no additional warning multiplicities against its explicit baseline.
The approved variants test replay of a marked journal, not an interrupted Keep
operation or power loss. Unique lossless previews were checked against all eight
authoritative engine screenshots before visual review.

Local immutable evidence is indexed by
`.tmp/ui/confirmation-review/capture-evidence.json`; individual captures are under
`.tmp/ui/confirmation-review/*-inspected/` and
`.tmp/ui/recovery-review/*-qualified/`. Logs, commands, source/binary hashes,
configuration/journal bytes, screenshots and failed attempts are retained there.
These machine-local artifacts are not packaged runtime content.

Prepare a new recovery run with the following command; add `--execute` to launch
both hidden windowed processes. Use `--state confirmed` for the explicitly
injected approved variant. For MP use `--mode mp --renderer vulkan` and supply
`--warning-baseline` with an independently reviewed existing log; unexpected
diagnostics continue to fail the check.

```powershell
python tools/ui/capture_settings_recovery.py --mode sp --renderer gl `
  --assets 'C:/Program Files (x86)/Steam/steamapps/common/Quake 4' `
  --output .tmp/ui/recovery-review/new-sp-gl-pending --state pending
```

The requirements register retains 60 partial requirements, 166 pending and one
inventory-only verification. No migration or final product gate is accepted by
this checkpoint. The next implementation dependency is canonical value controls
connected to actual SYSTEM rows; complete screen behavior, artwork and editor
round trips remain required.
