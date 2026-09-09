# Retained event programs and application delivery

This M1 increment adds ordered behavior to canonical retained documents and
connects it to normal GUI lifecycle and session delivery. It extends
[presentation aliases](presentation-aliases.md) and
[application integration](managed-application.md). The
[product completion plan](../plans/ui-product-completion.md) and
[requirement register](product-requirements.md) still require the complete GUI
corpus, controls, production screen, editor and platform qualification.

## Canonical programs

`events` maps a public name to an ordered array of instructions. Names use the
same ASCII case-folding and qualified-name grammar as presentation aliases;
`gui::` is reserved. Event targets can refer forward to another program.

```json
"events": {
  "ApplyBrightness": [
    {"op": "setState", "values": {"brightnessDraft": {"state": "brightnessSelected"}}},
    {"op": "if", "condition": {"op": ">=", "args": [{"state": "brightnessDraft"}, 0.5]},
     "then": [{"op": "action", "action": "applyBrightness"}]},
    {"op": "playTimeline", "timeline": "appliedFeedback"}
  ]
}
```

The referenced state, action and timeline must be declared in the same document.
This example illustrates structure; complete settings validation, cancel/default
and display-failure recovery remain production component work.

| Instruction | Contract |
| --- | --- |
| `setState` | `values` contains typed expressions for declared application keys; all right-hand sides read the same pre-instruction snapshot and commit as one batch |
| `setPresentation` | `alias`, scalar/tuple `value`, and explicit Boolean `overrideExpression`; applies the existing typed alias and ownership contract |
| `if` | Boolean `condition`, ordered `then`, optional ordered `else`; only the selected branch evaluates |
| `call` | Invoke `event` synchronously within the current transaction |
| `action` | Resolve a declared `action` descriptor now; preserve its arguments even if a later instruction changes their sources |
| `playTimeline` | Start/retarget the declared `timeline` at the event's presentation time |
| `pauseTimeline` / `resumeTimeline` | Change the named timeline's playback without changing the event clock |
| `cancelTimeline` | Cancel the named timeline using explicit `policy`: `hold` or `base` |

Button controls declare exactly one `action` or `event`. Existing direct-action
controls remain valid; event controls route the named program through the same
manager-owned application boundary. Canonical source retains comments and
extension fields for later editor use.

Action arguments and event expressions can read presentation aliases using
`{"presentation":"curr"}`. Number, Boolean and string aliases keep their types.
A vector read requires a valid zero-based `component`, for example
`{"presentation":"panel::rect","component":2}`. Scalar aliases reject a
component. These reads never create dictionary entries, disable expressions or
parse CSS. Ordinary bindings and presentation-variable expressions still accept
only their existing state sources; they cannot introduce presentation dependency
cycles through this syntax.

## Ordering, validation and rollback

The engine-independent behavior evaluator stages State and Motion. Later
instructions see earlier committed instruction values within that candidate;
nested calls share the same candidate and execution budget. Each action captures
its typed arguments at its position in the program. The optional application
validator is pure and runs before the whole result is published. No application
operation, drawing, input polling or resource callback occurs in the evaluator.

Invalid expressions, wrong types/ranges, unresolved targets, rejected operations
and exhausted budgets leave the original state, motion and previous result
untouched. A failed nested call rejects the entire outer event. Conditional
recursion is supported within a shared limit of 32 call/branch levels and 16,384
executed instructions. Source compilation limits programs to 1,024 names, 8,192
total authored instructions and 32 structural levels. Each transaction emits at
most 256 actions, further limited by remaining adapter queue capacity.

The runtime reads fresh host CVars and validates pending application input
together before the first instruction. This matters because game callers often
perform `SetState*` followed by `HandleNamedEvent` without `StateChanged`.
`State::SetCombined` validates both owners and evaluates derived bindings once;
an invalid intermediate update cannot reject an otherwise coherent pair. If the
program later fails, neither input batch commits.

After success, the adapter publishes only the application's keys explicitly
written by the program. Undeclared keys and unrelated pending dictionary input
remain intact. Numeric output uses the locale-independent round-trip codec.
Local writes, alias ownership and motion state participate in existing snapshots;
queued host invocations do not. Restore never repeats a program or host action.

## Lifecycle and safe delivery

`HandleNamedEvent` selects an explicit canonical program. An unhandled name
retains the earlier exact-name timeline fallback; unknown names remain silent.
`Activate` runs `onActivate` or `onDeactivate` on each explicit call. `Trigger`
runs `onTrigger`. Automatic `onInit` runs on the first successful redraw, and
save restoration suppresses it to avoid replaying initialization side effects.
Resource recovery retains the adapter's initialization state.

Pending application invocations retain FIFO order. Completed programs and
explicit semantic diagnostic activations survive focus, console and interactivity
suspension. Undispatched physical-input requests remain cancellable. Source
replacement, resource invalidation and save restoration discard pending actions
explicitly. Before dispatch, the adapter observes current input suspension so
the session pump cannot send a stale physical click first.

The private manager pump snapshots owner pointers, allocation serials and pending
command markers. It checks that each allocation still exists with the same
identity before invoking the session callback, and does not dereference the owner
afterward. New owners wait for a later pump. Global pumping is non-reentrant;
targeted lifecycle drains share a bounded nesting/dispatch budget.

Session drains pending programs before input-related early returns. It also
drains activation/deactivation at explicit lifecycle boundaries. Outgoing active
and test GUIs are detached before their deactivation requests run, and test
instances drain before deletion. Therefore an outgoing `ui.dismiss` cannot close
its replacement or recursively deactivate itself. An action queue is moved out
before host operations run. Dismissal is applied after that queue's ordered
operations finish.

The current application catalog remains brightness, shadows and dismissal.
Programs validate supported operation types and ranges before local commit.
Engine dispatch confirms setting readback. This does not promise rollback of
arbitrary future host operations; additional catalog operations need explicit
transaction, asynchronous result and failure contracts.

`ui_retainedTrace 1` enables event-commit counts and post-dispatch setting
readbacks for semantic validation. It defaults off and is not archived. Diagnostic
`openq4_retainedGui pending`, `event` and `trigger` exercise public dictionary and
event methods without host device input.

## Validation and remaining scope

The coordinated Windows x64 debug/MSVC build and release-style staging passed.
The five native suites for behavior, presentation runtime, document compilation,
retained runtime and state passed. They exercise the real compiler/evaluator and
runtime, including ordered presentation reads, conditional/nested programs,
combined pending/host input, late-failure rollback, execution budgets and
unavailable controls. Production adapter and manager/Session extraction suites
passed separately, covering queue cancellation, callback deletion/address reuse,
bounded lifecycle drains and non-replaying restoration. These extraction tests
use host/backend doubles; the following captures exercise the built engine.

| Gameplay capture | Display | Result |
| --- | --- | --- |
| SP `game/airdefense1`, OpenGL | 1280x720, 125% density | Event/ownership oracle and visual review passed; zero warnings or errors |
| MP `mp/q4dm1`, Vulkan | 1920x1080, 200% density | Event/ownership oracle and visual review passed; zero errors; 94 warnings in 86 pre-existing unique warning lines, with no new warning class |

Both runs use the mode-specific launch profile, installed stock assets, a hidden
window, disabled host mouse/controller input and the engine `screenshot` command.
MP explicitly sets `ui_autoJoin 1`. The
[event fixture](../../../tools/ui/fixtures/event-program-smoke.q4ui),
[semantic script](../../../tools/ui/fixtures/event-program-smoke.cfg) and
[read-only recovery script](../../../tools/ui/fixtures/event-program-smoke-resume.cfg)
are authored test assets. They are not production GUI replacements.

The strict `--event-program-probe` oracle binds exact fixture/script bytes,
observes ten committed outer programs and eleven ordered application operations,
and checks every submitted readback in order. It proves that pending dictionary
values are visible at event entry, nested and conditional execution use current
values, action arguments survive later source changes, save restore retains live
host settings without replay, and language/video recovery preserves local state
and an independent peer. Outgoing deactivation applies brightness and dismissal
after the render capture; the ownership checks also verify SP pause/resume and
continued MP simulation. Twenty capture-harness tests pass, including corrupted
trace/readback/order evidence and exact standalone completion-marker checks.

To repeat the SP capture from the repository root after building and staging:

```powershell
python tools/ui/capture_legacy_baseline.py --mode sp --renderer gl `
  --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' `
  --output .tmp/ui/event-program-sp-new --width 1280 --height 720 --density 1.25 `
  --shared-gui --retained-managed --event-program-probe --retained-peer `
  --language-reload --video-restart
```

For MP use `--mode mp --renderer vulkan`, a new output directory,
`--width 1920 --height 1080 --density 2`; the harness applies the MP profile and
explicit auto-join. Each output directory must be new to preserve earlier proof.
The local evidence record is `.tmp/ui/events-review/capture-evidence.json`;
it binds the source, build/staging logs, build options, binaries, capture commands,
semantic results, engine logs and visually reviewed render-target images.
`.tmp/ui/events-review/summary.json` binds final documentation, requirement
validation and publication separately. The original checker failure caused by
engine echo's trailing space is preserved with a read-only reassessment; the two
final runs use the corrected standalone-marker parser.

Canonical programs are not a claim of complete legacy script emulation. The
translator still needs root-first/subtree broadcasts, first-local duplicate-name
rules, cached legacy condition registers, native CVar-control group events,
timeline due/declaration/rearm behavior, sound/material/model operations, complete
game actions, event markers and production component behavior. Legacy
`resetTime` is not equivalent to the canonical timeline play operation.

Current source review also found an existing duplicate `NoIntro` root handler:
an earlier empty handler shadows a later one in the effective `mainmenu.gui`.
This is recorded for source-specific lowering; the retail/reference behavior
was not silently rewritten by this increment.
