# Retained presentation aliases and mutable values

This M1 checkpoint implements named presentation reads and writes for retained
documents. It extends the [public presentation boundary](presentation-bridge.md)
and [normal application integration](managed-application.md). The complete
[product requirement register](product-requirements.md) still governs acceptance;
this work does not accept a production settings page, translated GUI or editor.

## Canonical contract

The source declares public names explicitly. Root and qualified names can refer
to one presentation variable, or to a rendered node's properties:

```json
"presentationVariables": {
  "currentPage": { "type": "number", "initial": 22 },
  "difficulty": {
    "type": "number", "initial": 1,
    "value": { "state": "selectedDifficulty" }
  }
},
"aliases": {
  "curr": { "variable": "currentPage" },
  "desktop::curr": { "variable": "currentPage" },
  "skill": { "variable": "difficulty" },
  "desktop::skill": { "variable": "difficulty" },
  "set_system_content::rect": { "node": "system-content", "property": "rect" },
  "settings::visible": { "node": "settings", "property": "visible", "shown": "flex" },
  "settings::noevents": { "node": "settings", "property": "noevents" },
  "settings_label::text": { "node": "settings-label", "property": "text" }
}
```

The example requires the named state and nodes to be declared elsewhere in the
same document. Rectangle targets explicitly declare `left`, `top`, `width` and
`height` in dp. Visibility targets declare `display`; their required `shown`
keyword preserves the intended layout mode. `noevents` targets declare
`pointer-events` as `auto` or `none`.

Public names contain one ASCII identifier or two separated by exactly one `::`.
Lookup folds ASCII case, while stable target IDs retain their exact spelling.
Compilation rejects case collisions, missing targets, mixed target kinds and
unsupported types/units. `gui::` remains reserved for application dictionary
access. An alias never implicitly creates a dictionary key or a rendered node.

Presentation variables support number, Boolean, string, and two-, three- or
four-component numeric vectors. They are distinct from the public `State()`
dictionary and can exist without a visual element. This is required by existing
`curr`, `dest`, `skill`, `turboMode`, `blockBrowser` and `dpadGUI` callers. Multiple
names that reference one variable share both its value and expression ownership.
Optional expressions use the existing typed state expression language, including
its lazy branches, validation and budgets. They currently read declared state;
complete legacy variable/event lowering remains separate work.

Metadata strings are bounded UTF-8 data. Authored player-facing text still uses
`#str_*` keys. Rendering translates and escapes text at its existing boundary;
neither metadata nor a public text write is interpreted as markup or a command.

## Value formats and mutation

| Target | Public representation | Write behavior |
| --- | --- | --- |
| Number or fixed dp/px length | Locale-independent number, without a CSS unit | Preserve the declared type/unit and validate the property's range |
| Boolean metadata, `visible`, `noevents` | `0` or `1` | Parse only supported Boolean values; visibility restores its declared display mode |
| Rectangle | Four numbers: x y width height | Accept complete whitespace or comma tuples; validate all four components before mutation |
| Color | Four RGBA numbers in 0–1 | Preserve the actual mapped paint/property; never substitute a different color role |
| Text/font/keyword | Literal data or localization key | Preserve text; validate allowed font/keyword values |
| Metadata vector | Two, three or four numbers | Require the exact component count and finite bounded values |

Numeric formatting round-trips finite values, including subnormals. Parsing
rejects missing/extra components, partial tuples, non-finite values, overflow,
trailing input and invalid encoding. Failed reads preserve the caller's output;
failed writes preserve values, ownership and application state. A rectangle can
mix bound and unbound components: both owners stage their changes before either
commits. These calls do not redraw or dispatch events/actions.

Existing exact `node::property` diagnostic reads continue to work when no public
alias is declared. They retain their canonical CSS/value representation. Public
write names require explicit aliases; production translation must declare each
legacy name and its actual meaning.

## Expressions, transitions and input

An explicit write (`overrideExpression=true`) disables only the target's
expression. A transient write preserves the current ownership flag. Therefore
a transient write after an explicit override does not reactivate the expression.
The next successful state evaluation replaces a transient value even if the
caller supplies an unchanged state batch. Disabled expressions are not evaluated;
their errors cannot reject an otherwise valid update. Unrelated expressions
still validate atomically.

An external write does not cancel active transitions. Existing unbound tracks
can overwrite it on their next sample, and unrelated tracks continue. Mutable
values are separate from immutable authored bases, so later reset/reload and
snapshot behavior remain defined. The compiler's existing prohibition against a
binding and timeline owning the same property is unchanged; smooth retargeting
of authoritative bindings is still a broader motion requirement.

`noevents` suppresses both pointer and semantic input through the affected
subtree, including navigation, direct focus and an already armed activation.
Eligibility refreshes before input, so a write followed by a release cannot
activate a newly blocked control while waiting for the next rendered frame.
Visibility uses the same ancestor eligibility rule. Rendering and geometry still
update at their normal frame boundary. Full transformed clip/mask hit policies
remain a separate requirement.

## Snapshot version 2

New canonical snapshots write version 2. They preserve presentation-variable
values, expression-disable flags, pending transient values, and binding-owned
property overrides separately from current motion values. Unanimated mutable
properties now survive snapshots too. A restored transient value is immediately
readable and remains pending until the next successful evaluation. Ordinary
expression-derived metadata recomputes from the restored application values and
current host sources. Restoration never writes host CVars or replays actions.

Version 1 remains readable for its exact source/path identity with default new
tables. It retains its older restriction on unanimated motion values. Version 2
rejects unknown/missing tables, invalid types, unknown targets, duplicate entries,
invalid pending/ownership combinations and late corruption before committing
live state. The outer adapter's `Q4UI` frame stays at version 1 because its
length-framed embedded snapshot already identifies its own version. Real game
save-container migration and demo acceptance remain open.

## Validation and remaining work

Six native suites cover document compilation, numeric codecs, actual runtime
alias behavior, state/motion, vector rendering and existing retained contracts.
The new runtime suite uses the real retained implementation and a host double;
it tests independent instances, error suppression, mixed-owner atomic writes,
text escaping, both snapshot versions, corrupt-table rollback, transition
continuity and immediate subtree input suppression. The production adapter
harness separately verifies forwarding and failure behavior.

The client, dedicated server, both renderers and coordinated SP/MP modules built
and staged through the Windows Meson wrapper. Six native suites passed; the two
runtime suites were rerun successfully after the final event-time refinement.
Production adapter, manager, input, source binding, resource lifetime, numeric
presentation bridge and list-selection checks passed. Fifteen capture-harness
tests include negative evidence mutations; documentation link checks passed.

### Gameplay and rendered evidence — 9 September 2026

Local evidence is under `.tmp/ui/aliases-review/`; `summary.json` binds changed
source/build-option hashes, staged binary hashes, capture metadata/log/TGA/PNG
hashes, warning comparisons and review results. The baseline engine revision is
`e075e4a2e64e0ff790a1f435ae0854aa51650308`; the companion remains
`1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. Public game/renderer API versions stay
48/13. The tested client SHA-256 is
`3c7bf81479b9a869a4743f559be6dea40a6715b045999b26f8e4da26e00b9793`.

The authored [document](../../../tools/ui/fixtures/presentation-alias-smoke.q4ui),
[setup](../../../tools/ui/fixtures/presentation-alias-smoke.cfg) and
[resume](../../../tools/ui/fixtures/presentation-alias-smoke-resume.cfg) use actual
public presentation calls, framed GUI save/restore and typed shadow operations.
The oracle requires byte-identical fixture inputs and ordered readbacks. It
checks shared/case-insensitive aliases, both ownership modes, unchanged updates,
rect/color/text writes, subtree visibility/input suppression, discarded armed
activation, fresh activation, restored focus and independent peer lifetime.

| Capture directory | Gameplay / image | Brightness / gamma | Result |
| --- | --- | --- | --- |
| `sp-gl-neutral` | SP `airdefense1`, OpenGL, 1280x720 / 125% | 1 / 1 | Alias, save and language/video recovery passed; zero warnings/errors |
| `mp-vulkan-neutral` | MP `q4dm1`, Vulkan, 1280x720 / 125% | 1 / 1 | Same alias/recovery checks passed; 94 existing warnings, zero errors |
| `sp-gl-corrected` | SP, OpenGL, 1280x720 / 125% | 1.25 / 1.3 | Alias/recovery and color oracle passed; zero warnings/errors |
| `mp-vulkan-corrected` | MP, Vulkan, 1280x720 / 125% | 1.25 / 1.3 | Alias/recovery and color oracle passed; 94 existing warnings, zero errors |
| `mp-vulkan-native-corrected` | MP, native GUI path, 1280x720 / 125% | 1.25 / 1.3 | Alias checks passed without resource resets; image identical to corrected shared-request run |
| `mp-vulkan-settings-200` | MP, native GUI path, 1920x1080 / 200% | Typed sequence ending 1.25 / 1 | Existing managed settings/save/recovery oracle passed; 94 existing warnings, zero errors |

All runs were hidden/windowed with mouse/controller input disabled and no host
input injection. They entered gameplay before the semantic probe and used the
engine's registered `screenshot` command. SP paused/resumed correctly; MP kept
simulating. The native run without reloads has 93 warning occurrences; all MP
runs have exactly the same 86 unique warning messages as the prior checkpoint.
No new warning class or error was introduced.

For example, run the corrected Vulkan alias case from the repository root:

```powershell
python tools/ui/capture_legacy_baseline.py --mode mp --renderer vulkan `
  --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' `
  --output .tmp/ui/aliases-review/mp-vulkan-corrected `
  --width 1280 --height 720 --density 1.25 --shared-gui `
  --retained-managed --presentation-alias-probe --retained-peer `
  --language-reload --video-restart --brightness 1.25 --gamma 1.3
```

### Vulkan brightness/gamma correction

The existing planned [H3 pass](../plans/2026-07-22-vulkan-phase-h.md) now applies
`pow(clamp(rgb * brightness, 0, 1), 1 / max(gamma, 0.001))` after the completed
scene/GUI composition and before presentation or screenshot readback. Neutral
settings skip the entire pass. Per-slot source images use existing GPU resource
retirement; frame state prevents repeated correction. GLSL source and its
generated SPIR-V header are checked together. Production-controller tests cover
neutral bypass, reuse, reset, failure diagnostics, copy orientation and math.

On the tested RTX 4060 Laptop GPU (Vulkan 1.4.325), neutral OpenGL/Vulkan captures
differ by one 8-bit value in just three color channels over the entire image.
Corrected captures differ by at most one value over 876 channels. The native
Vulkan GUI run is pixel-identical to the shared-request corrected run. The
shared-request report is `mixed-view-fallback` (three ready views and four
fallback views); this does not claim every view used the shared renderer.

The independent `tools/tests/vk_display_color_mapping_capture.py` oracle compares
two uniform 8x8 regions at top-left coordinates `(52,152)` and `(52,560)`.
Panel RGB changes from `(32,40,35)` to `(61,73,66)`; background changes from
`(64,77,89)` to `(105,120,135)`. Maximum formula error is **0.517/255** on both
renderers, with captured alpha unchanged. Distinct vertically reflected samples
reject a flipped output; the values also reject omitted or repeated correction.
The 200% typed-settings capture produces the expected brightness-only background
`(80,96,111)` and button colors `(190,122,12)` / `(74,84,50)`.

These reviewed render-target images qualify the observed SDR menu subset. They
do not qualify ordinary physical display presentation, HDR/color spaces,
validation-layer cleanliness, all driver/GPU/platform combinations, release
performance, physical input or a complete production settings transaction.

Remaining work includes semantic alias generation for every migrated resource,
direct dictionary-bound legacy variable lowering, component/color-channel and
material/vector paint mappings, complete ordered events, dynamic collections,
editable text/IME, world surfaces and the native editor. Dynamic list feeds
(`*_item_N`, selection IDs, scrolling and icon/tab rows) need their own component
contract; preserving their dictionary strings does not make a list functional.
