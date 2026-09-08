# Semantic retained controls and input scopes

8 September 2026. The retained runtime now owns button focus, navigation,
press/release pairing, modal input scopes and authored state feedback. This
advances the [replacement runtime](../plans/idtech5-ui.md) and supplies a shared
interaction model for the future native editor. It does not complete platform
input routing, the game action bridge, the remaining widgets or GUI translation.

## Authored control contract

Any canonical node can declare a `control` object:

```json
{
  "role": "button",
  "action": "menu.controls",
  "label": "#str_200083",
  "enabled": true,
  "states": {
    "default": "controls.default",
    "hover": "controls.hover",
    "focus": "controls.focus",
    "pressed": "controls.pressed",
    "disabled": "controls.disabled"
  },
  "navigation": {"down": "system"}
}
```

`role`, `action`, localized `label` and all five state timelines are required.
`enabled` defaults true; `navigation` and `extensions` are optional. Actions and
navigation targets are validated stable IDs, not executable command text. Labels
use existing `#str_` keys and are stored for semantic consumers; platform
accessibility exposure is not implemented yet. Button controls cannot nest inside
other buttons. Other roles are rejected until their behaviors are implemented.

The existing JSONC source/diagnostic contract applies to every field. Timeline
references must resolve, run once, target only this control's subtree and cover
the same effective properties in all five states. This prevents a pressed offset
or highlight from remaining stuck because its release state omitted a property.
Feedback can change the button's own paint/opacity; layout and transform changes
must target child parts, preserving the button's hit box. A button establishes
`position:relative` by default, so absolute marker/rail parts anchor to its local
box. An explicit authored position takes precedence.

Feedback chooses disabled, pressed, focus, hover, then default in that order.
State changes play the authored timeline through the existing monotonic motion
evaluator. Interruption retargets from the current sample; reduced motion follows
the same evaluator policy as other UI motion. Feedback is initialized at document
load, before subsequent explicit application timelines acquire ownership.
The [fixture](../../../tools/ui/fixtures/interaction-smoke.q4ui) uses separate
editable orange inset rails, label color and a 1 dp pressed label offset. Its
right-hand panel is a test input scope, not an accepted production modal design.

## Interaction ownership

`Interaction` is independent of RmlUi, engine headers and devices. The layout
adapter supplies presented control bounds and hit IDs. It owns source-order
tab/reverse-tab navigation, spatial arrow navigation, explicit navigation links,
enabled state, focus, hover, armed activation, nested modal scopes and action
requests. Explicit links apply when their targets are eligible; otherwise normal
navigation skips disabled/hidden/out-of-scope controls. Directional navigation
selects the nearest forward candidate using squared forward distance plus four
times squared lateral distance; ties preserve source order. Tab wraps within the
active scope. With no focus, forward navigation chooses the first eligible
control and reverse tab chooses the last.

The runtime API accepts window-coordinate pointer movement, primary-pointer
down/up and semantic `MenuInput` requests. Window pixel density and viewport
origin convert once into document pixels. The stored window position is
reprojected after viewport changes without requiring a new device event. RmlUi
resolves transforms during rendering, so hit/navigation bounds are refreshed
after the presented frame. Child artwork cannot enlarge a button's hit box;
the hit is checked against its own projected border box as well as the layout
library's hit result. Decorative alpha masks do not shrink this box.

An activation requires a matching release on the armed, still-eligible control.
Repeated accept-down/pointer-down events cannot duplicate activation. Dragging
off the button, navigating to another control, disabling it, losing valid output,
cancelling input, changing modal scope or replacing the document cancels the arm.
Held-input state survives cancellation/replacement until release, preventing a
repeat or release from activating a replacement target. The pointer and accept
paths share a single arm, so simultaneous input streams cannot both own one
activation. Device adapters must aggregate their physical inputs into these
semantic transitions; this API does not implement a platform repeat scheduler.

`PushModal(root)` constrains input to that existing subtree immediately, chooses
its first eligible control and remembers prior focus. Further scopes must be
descendants. `PopModal()` cancels the arm and restores eligible prior focus, or
the first eligible control in the enclosing scope. Scopes own input independently
of visual transition duration. Opening/closing a visual modal and scroll-to-focus
remain application/widget responsibilities.

`TakeActions()` returns owned document/node/action strings. Activation requests
and Back requests have distinct kinds. No action string is executed by this
runtime. Requests are cleared on document replacement, bounded to 256 between
drains, and overflow is diagnosed. Enabled state is an instance override; it does
not rewrite authored source. Unknown control-state queries return an empty optional.

## Engine qualification without device control

The preview exposes semantic development commands: `ui_retainedFocus`,
`ui_retainedMenu`, `ui_retainedEnabled`, `ui_retainedModal`, `ui_retainedState`
and `ui_retainedEvents`. They exercise the runtime API and print focus/actions;
they never read, inject or move OS input. Quote stable IDs in engine command
text, including IDs containing hyphens. Real SDL input routing and gameplay
input inhibition are still pending, so these commands are not a claim that
live mouse/keyboard/controller menu integration is complete.

The [capture harness](../../../tools/ui/capture_legacy_baseline.py) accepts
`--retained-script` for a bounded sequence of these commands and waits. It rejects
other commands, unquoted IDs and excessive script size/waits. Scripts are hashed
in capture metadata and replayed after an optional video restart. Command-usage
failures are retained diagnostics, not successful interaction. The
[qualification script](../../../tools/ui/fixtures/interaction-smoke.cfg) checks
deduplicated activation, disabled navigation, modal containment/restoration,
release cancellation and live enable/disable. It leaves a pressed control visible.
The [verifier](../../../tools/ui/verify_interaction_capture.py) checks exact
action/state traces, the rendered disabled plate's alpha and the pressed orange
rail in the engine TGA. Screenshots require separate visual inspection.

Native tests use the same canonical fixture and real layout/render adapter. They
also cover pointer hit through a label, density/origin conversion, drag-off,
queued/held input across replacement, cancellation, stationary-pointer viewport
changes, rotated ancestors, invalid output, visible vector focus feedback,
source round trips, localized labels and rejected malformed state/action data.
Document, retained runtime and vector suites pass.

The staged Windows package passes the semantic/pixel verifier on both backends:

| Capture | Output / density | Script repetitions | Activations / state observations | Disabled plate error | Orange rail pixels |
| --- | --- | --- | --- | --- | --- |
| SP `game/airdefense1`, OpenGL, video restart | 1280×720 / 125% | 2 | 2 / 10 | 0.55 of 255 across 186 pixels | 1,835 |
| MP `mp/q4dm1`, Vulkan | 1920×1080 / 200% | 1 | 1 / 5 | 0.55 of 255 across 600 pixels | 3,070 |

Each repetition produces exactly `[0,1,0]` action-batch counts: the initial
release does nothing, the repeated accept pair activates once, and modal teardown
cancels the held release. Both engine TGAs were inspected: the disabled plate and
label fade together, the pressed inset rail and label are visible, and the
editable panel/button chamfers remain intact. The capture is a test layout;
its static orange Controls plate is inherited fixture art, not selection-state
implementation or a completed production screen.

Reproduce using the staged package and installed retail assets:

```powershell
python tools/ui/capture_legacy_baseline.py --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --mode sp --renderer gl --retained-document tools/ui/fixtures/interaction-smoke.q4ui --retained-script tools/ui/fixtures/interaction-smoke.cfg --density 1.25 --video-restart --profile-frames 60 --output .tmp/ui/interaction/sp-gl-125-qualified
python tools/ui/capture_legacy_baseline.py --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --mode mp --renderer vulkan --retained-document tools/ui/fixtures/interaction-smoke.q4ui --retained-script tools/ui/fixtures/interaction-smoke.cfg --density 2 --width 1920 --height 1080 --profile-frames 60 --output .tmp/ui/interaction/mp-vulkan-200-qualified
python tools/ui/verify_interaction_capture.py .tmp/ui/interaction/sp-gl-125-qualified
python tools/ui/verify_interaction_capture.py .tmp/ui/interaction/mp-vulkan-200-qualified
```

Use fresh output directories for reruns. The retained sources and semantic script
are copied into isolated savepaths; no OS device input is supplied. Both runs
enter their gameplay maps and use the registered engine screenshot command.
The OpenGL run repeats the script after a windowed renderer restart.

The final client SHA-256 is
`56bf531e1484a21e938076f80f20dc1677130a02fddf8c5ba875e905940344d3`.
The unchanged companion source revision is
`cc83b0c972c0bef9474462461818096dd690277e`; no shared interface changes are
required by these controls. The fixture SHA-256 is
`77884c8ec65cd2546062130444f5548eebe23026ebc5e970648ee0bb17dd830d`
and the semantic script is
`058330d64abf533f6919dc43fce59dbdf5cae161dc0ef61a647c20a2191025d2`.
Engine screenshot hashes are
`fd953b5b29811af1f1b46578a5faf0ef5c7453980531e6137df38441a6d1f99b`
(SP/OpenGL) and
`15439fc3c3dc21362b6efd0e3d002b2d9665d3954ff795897148ce6970d50a28`
(MP/Vulkan). Full binary/source/log hashes, traces and verifier results are
retained in `.tmp/ui/interaction/summary.json` and each capture's `capture.json`.

The 60-frame MSVC Debug CPU samples measure 2.417/3.337 ms p50/p95 on OpenGL,
2.461/3.126 ms after restart, and 3.774/6.079 ms on Vulkan. Initial compilation
spikes reach 32.343 ms and 39.877 ms respectively. These are development CPU
measurements, not GPU, optimized-build or full-corpus performance acceptance.
Shared GUI ownership was disabled in these captures. There are no retained UI
diagnostics; SP has no warnings/errors. MP has the same 93 stock warnings
(86 unique messages, identical multiplicities) as the preceding mask checkpoint,
with zero errors. These existing MP issues remain open.

## Remaining integration

SDL event routing, gameplay input inhibition, physical-key/source aggregation,
touch/scroll/text editing and IME, game-state bindings/action dispatch, selection,
busy/error semantics, sounds, other widget roles, native editor tools, complete
GUI translation and broad platform/visual qualification remain open. The current
preview remains a developer renderer until that application integration is done.

Transformed overflow clipping still reaches RmlUi's separate clip-mask API,
which the retained renderer has not implemented. The canonical alpha-mask
feature does not cover that interface. Consequently transformed/scrolling
clipping and matching input boundaries remain an explicit qualification gap;
the rotated-pointer native test covers hit projection without overflowing content.
This checkpoint accepts no stock GUI resource as a finished replacement.
