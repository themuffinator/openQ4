# Live retained state and expression bindings

8 September 2026. Canonical documents now bind typed application/CVar state to
layout, text, color, transforms and control availability. Runtime and editor
source edits use the same evaluator. This advances Stage 2; it accepts no GUI
as translated and does not implement the complete game action bridge. The
[archived replacement plan](../archive/2026-10-18/plans/idtech5-ui.md) preserves
the full technical scope. The [development roadmap](../roadmap.md#new-user-interface)
coordinates current work; the [product register](product-requirements.md) retains
the detailed acceptance requirements.

## State and binding schema

Root `state` declares stable IDs with `type` (`number`, `boolean`, `string`),
required `initial`, optional `cvar`, `view`, `keyBinding` and `extensions`. Numbers are finite within
plus/minus 1e12. Strings are valid UTF-8 without NUL, up to 64 KiB. Authored
string defaults are empty or localization keys; application updates can supply
player names, chat and other dynamic data.

Without `cvar`, the application owns the variable. With `cvar`, the host polls
that registered engine CVar read-only. Application and host batches cannot
overwrite each other's variables. Missing or invalid sources retain the last
valid host snapshot and report a diagnostic. An identical error is logged once
until recovery or a different error. Evaluation never sets CVars or executes commands.

A number state may instead declare a view source: `"view": "aspect"` (the
window safe area's width over its height), `"text"` (the text size, 1 to 2) or
`"safeLeft"`, `"safeTop"`, `"safeRight"` and `"safeBottom"` (the safe area's
insets in dp; see [the window's safe area](document-format.md#the-windows-safe-area)).
The runtime sets it each time it lays the document out, before bindings
apply, so the first frame of a new view already uses it. Like a CVar source it
is host-owned and read-only; it cannot also name a CVar, and a view that has
not been laid out keeps the last value (at first the authored `initial`). The
settings sub-pages read the aspect to arrange their rows in one column in
views of 3:4 and taller.

Numeric sources require engine numeric flags and use `GetInteger` or `GetFloat`;
boolean sources require `CVAR_BOOL`. String sources read the current CVar string.
This preserves engine values without depending on floating-point `from_chars`,
which [libc++ added in version 20](https://github.com/llvm/llvm-project/issues/99940).

This fragment drives an existing gauge and text node:

```json
{
  "state": {
    "progress": {"type":"number", "initial":25},
    "limit": {"type":"number", "initial":100},
    "scale": {"type":"number", "initial":1, "cvar":"ui_retainedScale"}
  },
  "bindings": [
    {
      "id":"gauge-width", "node":"fill", "property":"width",
      "value":{"op":"clamp", "args":[
        {"op":"*", "args":[100, {"op":"/", "args":[{"state":"progress"}, {"state":"limit"}]}]},
        0, 100
      ]}
    },
    {
      "id":"gauge-number", "node":"reading", "property":"text",
      "value":{"op":"numberText", "args":[{"state":"progress"}], "decimals":0}
    }
  ]
}
```

`fill.width` needs an explicit base length in `%`; `reading` needs an explicit
localized base text property. Bindings inherit the target's authored type and
unit. There is no raw CSS or implicit unit conversion in state values.

A binding has unique `id`, existing `node`, `property`, `value` and optional
`extensions`. Numbers/lengths take one numeric expression; colors take four;
transforms take five. Keywords/fonts/text take one string expression. Shorthands
expand into effective side properties. Special property `enabled` takes one
boolean expression and requires a semantic control, without a CSS base value.

Expressions are JSON number/boolean/string literals, `{"state":"id"}` references,
or `{"op":"...", "args":[...]}` operations. Objects accept `extensions`; only
`numberText` accepts `decimals` (0–6).

| Operations | Contract |
| --- | --- |
| `+`, `-`, `*`, `/`, `%`, `min`, `max` | Two numbers; `%` is floating-point remainder |
| `abs`, `floor`, `ceil`, `round` | One number |
| `clamp` | Value, lower bound, upper bound; reversed bounds are an error |
| `<`, `<=`, `>`, `>=` | Two numbers, boolean result |
| `==`, `!=` | Two values of the same type, boolean result |
| `&&`, `||`, `!` | Booleans; binary operations short-circuit |
| `select` | Boolean condition, matching branch types; only the selected branch evaluates |
| `numberText` | One number; fixed decimals, independent of host locale |

Trees preserve explicit grouping. Legacy translation must retain the
[imported tree](legacy-import.md), insert required conversions and preserve
legacy arithmetic. Canonical `%` does not implement legacy integer-truncating
remainder. Full expression lowering, tables and window-variable resolution
remain open. There is no implicit C-like string-expression parser.

Literal display-text results must be localization keys or empty, including
both possible `select` branches. Numeric text and application string references
are permitted. Runtime translates keys and escapes the result before inserting
text, preventing player text from creating markup/elements. Numeric formatting
does not yet provide locale-specific grouping, pluralization or message templates.

## Updates, ownership and lifetime

`Runtime::SetState(changes, error, monotonicSeconds)` validates the entire batch
and evaluates its bindings against a candidate snapshot. Type errors, division
by zero, nonfinite results, reversed clamps or invalid properties reject the
batch without changing any variable, derived property, availability or revision.
The error identifies the variable or binding. Successful updates change the
existing document without rebuilding its layout tree or editing source.

Colors remain in 0–1, geometry obeys existing signed/nonnegative rules, keywords
use the property registry, and font identifiers stay bounded. Unchanged batches
skip evaluation and retain revision. A changed batch evaluates only the bindings
whose expressions read a changed variable (an index built when the document
loads), against the committed values overlaid with the batch, and commits once
every one of them has succeeded; presentation variables and control readbacks
are evaluated in full when the document declares any. A pending transient
property or presentation write takes the full evaluation instead, so that
successful expressions still replace it. The state logs the bound properties
whose values changed, and the next update applies only those; after a full
evaluation, a restore or an overflowing log it applies every bound property.
Without authored modals, `SetState` updates the state in place rather than
validating a copy.

One binding owns each effective property. A timeline cannot also own that same
property. Conflicting ownership is a source error: put data-driven geometry or
visibility on a containing node and animate presentation on its child. Retargeting
transitions to changing bound values is not implemented. Other properties keep
their existing authored feedback timelines.

Availability updates reach interaction immediately and cancel an ineligible
pending activation. Direct `SetControlEnabled` cannot bypass a binding.
Display/layout changes update visibility/navigation bounds with the rendered frame.
`GetState(false)` snapshots application values without read-only host sources;
the engine validates/restores this snapshot after renderer or language changes
and rereads current CVars. Close/replacement releases state. This is not game
save serialization or per-entity world-GUI instancing. `PresentedValue` and
`StateRevision` expose current values and revision for developer/editor inspection.

Limits: 4,096 state declarations, 8,192 binding declarations, 65,536 compiled
expression nodes and depth 32, within the 16 MiB document limit. JSON-pointer
edits preserve surrounding source and revalidate defaults, types, ownership
and references before committing.

## Engine qualification

`ui_retainedData <VFS state.json>` applies a flat JSON object as one typed batch.
`ui_retainedValue <node> <property>` reports the bound/animated value and layout
bounds. These internal developer operations do not synthesize platform input.
The capture harness accepts up to 16 `--retained-data` files and records their
hashes with the document, script, executable, log and engine screenshot.

The [fixture](../../../tools/ui/fixtures/binding-smoke.q4ui) reuses existing Marine
vector qualification art. Its [script](../../../tools/ui/fixtures/binding-smoke.cfg)
changes progress 25→40→75, disables a pressed control and releases it without
an action, then reenables it and obtains exactly one activation. A read-only
CVar supplies the displayed UI scale. Existing feedback timelines operate
throughout. This fixture is not a completed settings screen.

After building/staging through the standard Meson wrapper:

```powershell
tools/build/meson_setup.ps1 test -C builddir --no-rebuild --print-errorlogs openq4-ui-state openq4-ui-document openq4-ui-vector openq4-retained-ui
python tools/ui/capture_legacy_baseline.py --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --mode sp --renderer gl --retained-document tools/ui/fixtures/binding-smoke.q4ui --retained-script tools/ui/fixtures/binding-smoke.cfg --retained-data tools/ui/fixtures/binding-first.json --retained-data tools/ui/fixtures/binding-final.json --density 1.25 --ui-scale 1.25 --video-restart --output .tmp/ui/state/sp-gl
python tools/ui/verify_binding_capture.py .tmp/ui/state/sp-gl --output .tmp/ui/state/sp-gl/verification.json
```

For MP, use `--mode mp --renderer vulkan --density 2`, omit `--ui-scale` and
`--video-restart`, and choose a fresh output directory. Both modes enter their
gameplay map before loading the fixture. Runs are hidden/windowed with host
mouse/controller input disabled. Images come from engine `screenshot`.

Final captures: `.tmp/ui/state/sp-gl-125-r2` and `mp-vulkan-200-r2`. The
[verifier](../../../tools/ui/verify_binding_capture.py) checks source/log/image
hashes, exact data/action traces, restart restoration, actual gauge bounds and
filled/unfilled interior pixels. Every sampled pixel passes: GL 3,136 filled/
1,022 unfilled; Vulkan 5,184/1,692. Both screenshots were reviewed for text,
framing, feedback and numeric values. SP retains 75 across the video restart.

All four native UI suites pass, including atomic failures, component counts,
property ranges, host errors/recovery, markup escaping, lazy evaluation, source
edits, actual layout and cancellation. The state suite is registered in the
existing Meson validation profile. Production input and both legacy import/
inventory suites pass. The production CVar adapter also passes typed-source,
integer-precision and invalid-source tests in the script smoke jobs.

| Evidence | SHA-256 |
| --- | --- |
| Staged client | `d08fe1e2f70f0b3b4378d9a1edc6baa801132a73640d7357a7f4595309e8fdbe` |
| SP/OpenGL screenshot | `fbc67fc548e314d028b63ee2b6363a74f80778fc95ab52c788430810bfb2bf93` |
| MP/Vulkan screenshot | `acb51f8a629093948702365e3c9108a4d91cc30c8cbec6ac82014601457665a4` |

Based on engine `d2082557`; unchanged companion:
`300aedd9c56e20666ca7eacc0d71db70502660b9`. No external implementation code is
incorporated. SP has zero warnings/errors. MP has zero errors and the unchanged
93 warnings (86 unique) from the previous import checkpoint.

The game action/state bridge, legacy script lowering, remaining widgets,
all-GUI vector reconstruction, localization/text backend, world instances/save
restoration, complete editor and broader platform/performance qualification
remain open. No migration manifest entry is accepted by this work.

### Public key-binding refresh, 4 November 2026

An application string can declare a `keyBinding` lookup:

```json
"forwardKeys": {"type":"string", "initial":"", "keyBinding":"_forward"}
```

The public GUI `SetKeyBindingNames()` call refreshes these declarations from
the current key map. It lists every matching key with localized names and the
language's list separator, capitalizes single-character key caps, and uses the
localized empty-binding label when no key matches. The lookup can contain
arguments, such as `savegame quick`; it never executes that string. Declarations
must be nonempty, single-line UTF-8 without NUL, at most 1,024 bytes, and cannot
also declare a CVar or view source.

The values remain application-owned. Call `StateChanged()` to publish the batch,
and refresh after rebinding or reopening. CONTROLS now uses this public path
for all 49 rows. The session then replaces the row waiting for a key with its
localized capture instruction before publishing. This preserves the existing
capture, cancel, clear and two-key binding behavior without a second name-list
implementation in the session. No physical-device qualification is implied.

### Public text-fitting query, 4 November 2026

`GetMaxTextIndex(nodeId, text, wrapInfo)` now measures plain UTF-8 against a
visible canonical text node's last laid-out **content** width, resolved font,
letter spacing and case transform. It shares the immutable scalar runs used by
font submission, including the same final width rounding. The query does not
advance presentation time, mutate the document or draw another frame.

On overflow it returns true, with `maxIndex` as the number of original UTF-8
bytes before the overflowing scalar and `lastWhitespace` as the byte index of
the last fitting ASCII space. A first scalar wider than the whole field is
returned intact so wrapping callers can progress. A fitting string returns
false and clears `maxIndex` to -1; an empty string fits. Missing, hidden,
non-text, legacy-text, invalid UTF-8, oversized or unavailable-font queries
return false and preserve the caller's output. The limit is the shared 64 KiB
text transport bound. These are scalar boundaries, not grapheme or shaping
clusters; legacy escape/icon parsing, bidi and full text layout remain UI-02/
UI-09 work.

The read-only diagnostic `openq4_retainedGui textfit "node-id" "plain text"`
reports availability, overflow and byte indices through this public API. Quote
node IDs containing punctuation. `tools/ui/capture_text_queries.py` exercises
the ordinary CONTROLS page in SP/MP gameplay, records binding/capture/clear
readbacks and text queries, and captures its 100% and 200% arrangements through
the engine. Text size can widen the column as well as its glyphs; its check uses
the measured width ratio. Native tests separately hold width fixed to verify
font scaling and cover UTF-8 boundaries, invalid requests and independent views.

Windows x64 verification on 4 November passes the native retained runtime,
CONTROLS, SETTINGS and MP settings/welcome suites; the compiled public adapter
passes with all 20 deliberate regressions rejected. The generated-menu gate and
player-identity/target regressions also pass. Windowed SP/OpenGL at 1280x720 in
English and MP/Vulkan at 1920x1080 in French both enter gameplay, open CONTROLS,
capture/rebind/clear a key, change text size and return to gameplay. All 14 final
engine captures were visually reviewed, including readable accented labels and
the 200% single-column arrangement. The exact binaries, source hashes, reports
and capture review are retained locally in
`.tmp/ui-roadmap-next/validation-evidence.json`; failed harness attempts remain
separate from the final evidence.

These runs have no retained-UI errors. Existing late-precache warnings remain
(one in SP and 178 in MP, around player-preview assets). Physical input, live
IME, shaping, other platforms and full toolkit/product acceptance are unqualified.
