# SYSTEM performance presets

SYSTEM uses the same six performance profiles and hardware-detection policy as
the existing engine commands. `framework/PerformancePreset` owns the profile
records, their ordered 32 assignments and the read-only detection decision.
Common retains its original setters, readback checks, modified flags and rollback;
it publishes the selected profile name only after all other writes succeed.

The retained settings service exposes two typed draft operations:

| Operation | Arguments | Result |
| --- | --- | --- |
| `settings.system.preset` | One string, `name` | Expand Minimum, Low Power, Performance, Balanced, Quality or Ultra into the current draft. |
| `settings.system.autodetect` | None | Observe the existing platform/memory signals and expand the selected profile into the current draft. |

These operations validate the entire merged draft. They replace exactly the
32 profile fields and preserve the other 21 catalog fields, including custom
Brightness and Ambient Brightness edits. They do not set CVars, run commands,
restart devices or persist configuration. The complete local Number draft
barrier also applies to these bulk changes.

`SettingsTransaction::EditGenerated` holds the owner and reentrancy guard while
the producer observes capabilities and builds the candidate. Invalid profiles,
unavailable observations, owner closure, attempted nested mutation, failed
validation or a caught allocation failure leave the previous draft intact.
Publication follows successful construction of the caller's result object. This
also covers the allocating result-copy boundary in MSVC's debug standard library.

The implementation preserves Common's explicit low-power, Raspberry Pi, Steam
Deck, legacy-renderer, ARM and memory-policy precedence. It does not invent a
benchmark or treat memory size as measured frame-rate performance.

The emitter limit now declares its local archive policy at registration, so
lower preset drafts can edit it without relying on a previous legacy preset
command. Refresh rate, sky rendering and generated-image cache preferences
also declare their archive policy. A production-method regression checks the
actual declarations, effective protection and exact configuration selection;
unrelated renderer diagnostics and explicitly protected values stay protected.

The isolated and root standalone suites compile the production model and
transaction and exercise the actual Common, SYSTEM host, service and adapter
method bodies. Golden profiles, detection boundaries, all 32 assignments,
untouched values, reentry, failed allocation, legacy flag/value restoration and
the local draft barrier are covered. Windows and Linux sanitizer runs also
reject compiled behavioral mutations. The separate MSVC debug-library test
targets result storage; it does not claim recovery from termination inside
third-party `noexcept` operations or persistent process-wide memory exhaustion.

The opt-in production page places the selector and Auto-Detect action in a
wrapping band above the existing image/render columns. Their editable vector
parts come from the existing Post AA selector and Back button. All six language
tables supply the existing profile labels and Auto-Detect text. The compact
selector places its title and value on one row when they fit and wraps them
when translations need more room. Both controls
require an open, idle editing session without a confirmation, discard dialog
or unfinished local Number edit. Named Auto-Detect events use the same guard.

Popup highlight is transient. Selecting an option sends a typed proposal;
the service's authoritative draft readback precedes acknowledgement. Rejected
proposals preserve the accepted selection. Draft and baseline aliases survive
source-bound resource recreation without replaying an action.

The actual Runtime/RmlUi suite covers all six profiles and 48 combinations of
locale, wider deterministic glyph metrics, density and resized viewport.
It inspects every popup label, the longest collapsed translation, popup bounds
and focused Auto-Detect reveal. Opening-layout checks also preserve complete
footer targets, flowing status text and room for numeric controls at 200%.
The status and actions share a wrapping footer, while explicit button widths
prevent accidental full-row flex sizing. Buttons retain 44 dp minimum targets
and the visual contract's 18/24 dp action typography. Its counted host does not qualify the actual
engine font, device or settings-service route; gameplay captures provide
separate evidence for those integration boundaries.

Complete Apply remains gated until every changed effect has a checked execution and
recovery path. Image/material policy, renderer resources, audio and deferred
load policy need actual observed results beyond CVar readback, as described in
[effect execution](settings-effect-execution.md). The complete
page, editor, supported platforms, all 271 migrations and final product gates
remain unaccepted.
