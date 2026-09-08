# GUI presentation boundary

Development checkpoint on `idtech5-ui`. This removes legacy window pointers
from the engine/game interface so document implementations can provide the
same value and focus operations. It does not route production GUIs through the
retained runtime or accept any GUI as a completed replacement.

## Interface

`idUserInterface` no longer exposes `GetDesktop()` or an `idWindow` type.
Three operations replace the external dependencies:

| Operation | Contract |
| --- | --- |
| `GetPresentationValue(name, value)` | Copy an existing named presentation value; do not change its expression. Failure leaves the output unchanged. |
| `SetPresentationValue(name, value, overrideExpression)` | Write an existing value. The default explicit override stops its expression; a transient write retains current expression ownership. |
| `GetTextInputState(area, cursorOffset)` | Query the focused editable field and caret in the GUI's cursor coordinate space. Failure leaves both outputs unchanged. |

Presentation values are separate from the application `State()` dictionary.
The legacy implementation accepts a root variable or `element::variable`.
Qualified lookup uses the existing first-match window traversal, including
optimized simple windows. A replacement document will need corresponding
stable aliases for callers that retain these names. This alias mapping and its
typed conversions are still to be implemented in the retained adapter.

Missing names and malformed qualifications fail without adding variables or
elements. `gui::name` dictionary allocation is not part of presentation lookup;
use the existing state API for application dictionary values. Writes do not
run commands, named events or redraws. Callers retain their existing
`StateChanged` and redraw timing. A transient write does not re-enable an
expression previously stopped by an override.

## Coordinated consumers

The session uses these operations for new-game options, settings popup/page
visibility, settings scroll geometry and browser/GUI diagnostics. New-game
options retain the existing presentation-first, dictionary-second fallback
order. An absent value remains distinguishable from a present zero.

Both SP and MP player sources use presentation operations for the inherited
console weapon selector and terminal D-pad metadata. These sites are under
`_XENON`; the desktop gameplay checks do not exercise those console branches.
Their pointer dependencies are removed from the canonical companion source;
this is not a claim of console platform qualification.

SDL asks the active GUI for its text-input field instead of casting its focused
window. The legacy implementation preserves the existing field/caret geometry
and coordinate conversion. Candidate outputs ensure a rejected empty edit
rectangle cannot partially update the caller. This does not add retained text
widgets, text shaping or IME composition support.

Legacy window code and the old GUI editor still have a concrete, non-virtual
`idUserInterfaceLocal::GetDesktop()`. This is implementation-private access,
outside the public game interface. It is not a placeholder returning null.

## Expression ownership correction

The old external code called `GetWinVarByName(..., true)`. That parser fixup
mode can disable root registers and create GUI dictionary variables. A
diagnostic read therefore could stop an expression from following its input.
Presentation queries resolve the element separately and use non-fixup lookup.
The engine scripts themselves retain their existing fixup behavior.

`openq4_guiSet` now makes an explicit presentation override. Unlike its previous
inconsistent root/qualified lookup behavior, both forms retain the written
value across later expression evaluation. Settings overrides retain their
existing `SetEval(false)` behavior. No arbitrary expression or command is
evaluated by the new boundary.

## ABI and qualification

Game API **48** accompanies this vtable change; renderer API remains **13**.
The public GUI header is synchronized in engine and companion. Rebuild and
ship engine and both game modules together. Earlier game API modules are
rejected by the existing loader version check. No save format is changed.

`tools/tests/ui_presentation_bridge.py` compiles and runs the production resolver,
accessors and session helper functions with bounded window/register/edit-field
doubles. It checks continuing expressions after reads, root/child/simple-window
resolution, missing-name failure without allocation, explicit versus transient
writes, menu fallback order and unchanged failed text-input outputs. Existing
settings, browser, clipping and retained input/source tests also cover the
affected neighboring contracts.

`tools/ui/capture_legacy_baseline.py --presentation-probe` runs the authored
`presentation-smoke.gui`/`.cfg` fixture after three seconds of SP/MP map gameplay.
Its production `testGUI`, named events and `openq4_guiGet/Set` commands check:

1. A bound number changes from 1 to 2 after a query.
2. Root and qualified reads agree; an explicit override remains 7 after its
   source changes again.
3. A child can be hidden, resized and shown through the boundary.
4. Missing reads and writes report failure.

The fixture then closes, returning to gameplay before any retained fixture or
engine screenshot. Captures are hidden/windowed, with mouse and controller
input disabled. No OS input is injected or captured. Fixture, configuration,
binary, log and engine screenshot hashes are recorded in each capture report.

The Windows client, dedicated executable, OpenGL/Vulkan renderers and both
game modules build successfully. All four retained native suites pass, as do
the production presentation, settings, browser, clipping, input and CVar
source checks. The live SP/OpenGL and MP/Vulkan probe reports both pass, with
identical value sequences and all missing-name failures accounted for.

The same runs qualify the unchanged retained binding fixture at 125% density
with 125% user scale in SP and at 200% density in MP. SP includes a renderer
restart and retains its application values. All sampled gauge pixels and
layout checks pass, and both engine screenshots have been visually reviewed.
These captures test the public boundary on the legacy fixture and the retained
runtime separately; they do not claim the retained adapter is integrated.

| Evidence | SHA-256 |
| --- | --- |
| Captured client | `fcf7c2c21ea9f13d6a58248411a0ac97ed1925fff7ff65b7ca6b082f29458d6e` |
| SP/OpenGL screenshot | `fbc67fc548e314d028b63ee2b6363a74f80778fc95ab52c788430810bfb2bf93` |
| MP/Vulkan screenshot | `acb51f8a629093948702365e3c9108a4d91cc30c8cbec6ac82014601457665a4` |

Local evidence: `.tmp/ui/bridge/`, including source/capture hashes and paired
publication revisions in `summary.json`. The checkpoint starts from engine
`bf333b28` and companion `300aedd9`; the coordinated companion revision is
`1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. No external implementation code is added.
SP has zero warnings/errors. MP has zero errors and the exact unchanged
93-warning multiset (86 unique) from the preceding binding checkpoint.

Full game state/action dispatch, retained aliases, world instances, save/restore,
GUI migration and the visual editor remain open. No manifest entry is accepted.
