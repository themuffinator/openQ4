# Live retained state and expression bindings

8 September 2026. Canonical documents now bind typed application/CVar state to
layout, text, color, transforms and control availability. Runtime and editor
source edits use the same evaluator. This advances Stage 2; it accepts no GUI
as translated and does not implement the complete game action bridge. The
[full replacement plan](../plans/idtech5-ui.md) remains authoritative.

## State and binding schema

Root `state` declares stable IDs with `type` (`number`, `boolean`, `string`),
required `initial`, optional `cvar` and `extensions`. Numbers are finite within
plus/minus 1e12. Strings are valid UTF-8 without NUL, up to 64 KiB. Authored
string defaults are empty or localization keys; application updates can supply
player names, chat and other dynamic data.

Without `cvar`, the application owns the variable. With `cvar`, the host polls
that registered engine CVar read-only. Application and host batches cannot
overwrite each other's variables. Missing or invalid sources retain the last
valid host snapshot and report a diagnostic. An identical error is logged once
until recovery or a different error. Evaluation never sets CVars or executes commands.

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
skip evaluation and retain revision. Changed batches currently evaluate every
binding; selective dependency invalidation is future work.

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
