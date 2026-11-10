# SYSTEM Display Resolution and Refresh Rate

The retained SYSTEM page now offers **Display Resolution** and **Refresh
Rate**, the last settings of the stock SYSTEM page it had no control for. Both
rows read the [SYSTEM display lists](system-display-catalog.md), as
[Display Device](system-display-rows.md) does. At this increment the page
stayed opt-in (`ui_retainedSystem 1`): a control for every stock setting was
not yet the same as every setting working there. Since 10 November 2026 a
Performance Preset applies there too, and `ui_retained` opens the page
([the gate](#the-gate)). The increment also
makes Auto refresh mean the same rate on every display path, lets MSAA apply on
Vulkan, and fixes three ways a display Apply or Revert failed.

## Display Resolution and Refresh Rate

- **Options.** One option per list slot, labelled from the list:
  - Display Resolution reads the mode list's 40 slots
    (`settings.display.mode.N.label`): Desktop Native, the sizes the display
    offers for exclusive fullscreen, and Custom.
  - Refresh Rate reads the refresh list's 16 slots
    (`settings.display.refresh.N.label`): Auto and the rates the requested
    size offers.

  The lists' counts (`.optionCount`) bound the popup. A size or rate the
  display does not list sits in the reserved last slot (39, 15). The closed
  control names it, but the popup never shows it.
- **Value.** Each row's value is the list's selection
  (`settings.display.mode.selected`, `.refresh.selected`), not the setting.
- **Picks.** A pick sends `settings.system.displayMode` or `.displayRefresh`
  with its slot and the list token, `{index, catalog}` (`display.mode`,
  `display.refresh`), as Display Device does.
  - The service writes `r_mode` (with the custom size for Custom or a listed
    size) or `r_displayRefresh` as an ordinary draft edit.
  - A size or display pick also returns what the new choice lacks to its
    automatic value: a rate the new size lacks to Auto, and a listed or legacy
    size a newly picked display lacks to Desktop Native. A Custom size the
    list does not offer stays. The pick alone therefore never leaves Apply
    refusing the draft. While a custom size field holds unconfirmed text, a
    display pick waits as a size pick does.
  - The page's unused direct `edit.r_mode` and `edit.r_displayRefresh`
    actions are gone.
  - A size pick waits while a custom size field holds an unconfirmed number,
    as a preset does.
- **Availability.** The lists describe exclusive fullscreen, so both rows are
  enabled under the Exclusive policy, including while preparing it in a
  window, on one display as on several. Under desktop fullscreen, or without
  lists, they read at 45%, as the custom size fields do. They follow the
  page's shared editing rule.
- **Placement.** Display Sizing reads `[title, window width, window height,
  hint, Display Resolution, custom width, custom height, Refresh Rate]`. Eight
  rows show before a popup scrolls where the scroll body has room.
- **Size and cost.** The page grows from about 4.56 MB to 6.73 MB, almost all
  of it the 56 option rows. In the frame probe at 1280×720 its CPU time per
  frame rises from about 7.3 to 10 ms (8.1 ms with a few slots per row): the
  runtime visits every option element each frame, shown or not. Skipping a
  hidden option's paint work measured no gain. Building a popup's rows only
  while it is open is the follow-up.

## Auto refresh, one rule

Auto (`r_displayRefresh` 0) used to mean two things. The page's strict Apply
took the highest rate at the size, while startup, `vid_restart` and Alt+Enter
took SDL's closest mode, which aims at the desktop rate. A player who applied
Auto at 144 Hz could restart at 60 Hz.

`src/sys/DisplayModeRule.h` now holds one rule for exclusive modes, which the
SYSTEM planner, its restores and recovery replays, the strict window service
and the legacy path all use:

- the exact pixel size only;
- a whole-hertz rate takes the highest rate in its bucket (59.94 is 60), as
  before;
- Auto takes the rate nearest the desktop's, the lower of two equally near,
  as SDL does for refresh 0;
- equal rates keep SDL's first mode, its deepest format.

Legacy falls back to SDL's closest mode only for a size or rate the display
does not offer. A restore whose captured rate its whole hertz cannot reproduce
(an Auto start at 59.94 Hz beside 60.00) replays Auto. On Windows and Linux the
legacy result is unchanged; on macOS, where the legacy search took the highest
rate, Auto now takes the desktop rate.

## Display fixes

- **A saved size or rate the display no longer offers.** Every display restart
  (V-Sync, MSAA, Borderless, window size) requests the exclusive mode again.
  `DisplayTuple` now checks the exclusive size and rate whenever an Apply
  restarts the display, so such a draft is refused up front with
  `#str_230023` ("Choose a supported display, size and refresh rate before
  applying.") instead of failing into the recovery dialog. The Refresh Rate
  row names the stale rate in its reserved slot. A rollback keeps to the
  settings that choose the mode: it can run with no window, when Auto would
  resolve to the primary display rather than the one it restores.
- **Revert from a non-desktop exclusive size.** Windows, X11 and macOS report
  the active exclusive mode as the display's bounds, and Wayland does the same
  for a focused exclusive window. Monitor descriptors compared those bounds,
  so a Revert or the confirmation timeout could not find its monitor. A
  descriptor's size is now its desktop mode's, which equals the bounds
  otherwise. Its position still comes from the bounds, so a display the
  operating system moves during an exclusive mode is not covered.
- **Native Wayland.** SDL emulates exclusive modes per window there and never
  switches the output, so the strict query's current display mode stayed the
  desktop and every smaller exclusive size failed its post-check. On native
  Wayland the query now reads an exclusive window's own fullscreen mode. Each
  size offers only the output's current rate.
- **Vulkan MSAA.** Vulkan presents a single-sample swapchain and multisamples
  its scene targets, so the display request now asks Vulkan for no window
  samples ([display controls](display-controls.md)). Display Applies no longer
  fail while MSAA is on, and the MSAA row works on Vulkan. A recovery record
  written under OpenGL recovers under Vulkan with a single-sample window.

## The gate

The stock page was the default until the retained page did what it did. Since
10 November 2026 it does, so with default settings SETTINGS > DISPLAY opens
the retained page:

- `RETAINED_SYSTEM_MISSING_SETTINGS`, the stock settings the page has no
  control for, is empty.
- `RETAINED_SYSTEM_INCOMPLETE_SETTINGS`, the settings it has a control for
  that cannot do what the stock page did, is empty too. Its entries left as
  their reasons were fixed:
  - `com_performancePreset` left on 10 November 2026. A preset or Auto-Detect
    used to fill the draft only: the settings a preset writes include image,
    audio and renderer resource effects, which had no apply path, so
    `ApplyClassOf` reported Unsupported and Apply stayed disabled. The
    [combined executor](settings-effect-execution.md#combined-executor) now
    applies such a batch as the stock page's preset and APPLY did.
  - `r_windowWidth`, `r_windowHeight`, `r_customWidth`, `r_customHeight`
    left when committed characters began to reach retained fields
    ([typed characters](numeric-fields.md#typed-characters)): only number
    fields offer them, and they now take typed digits as well as pasted ones.
  - `r_multiSamples` left when MSAA began to apply on Vulkan.
- `Session_RetainedSystemEnabled()` includes the page in `ui_retained` while
  both lists are empty. `ui_retainedSystem 1` opens it regardless, also with
  `ui_retained 0`. The stock page still stands in where the retained document
  is missing or cannot load.
- `tools/tests/ui_retained_gate.py` derives both lists from the code:
  - A setting is offered when a control value reads its draft, or a list
    selection standing for it (`RETAINED_SYSTEM_DERIVED_STATE`), and the
    control's action writes it (`OPERATION_WRITES`, checked against each
    branch of `SystemDisplaySelectionPatch`).
  - A setting is incomplete when its catalog effect has no apply path, when
    only number fields
    offer it while typed characters reach no retained Number field, or
    when its control needs `settings.msaaAvailable` and
    `SupportsMultisampling` no longer holds the statement that publishes it for
    Vulkan (the display service test proves that statement's behaviour). Typed
    characters count as delivered once the adapter routes them into Number
    fields or engine code drives the native text owner.
  - An effect has an apply path when `ApplyClassOf` gives it an executor. The
    test mirrors that function statement by statement
    (`APPLY_CLASS_STATEMENTS`). An image, audio, renderer-resource or
    load-policy effect applies through the combined executor only while the
    service admits and dispatches a Combined batch, the host prepares,
    restarts and recovers it (`COMBINED_ROUTE`), and `CombinedPolicy` gives
    that effect a domain in the record (`COMBINED_DOMAINS`).
  - A preset counts as incomplete when any setting it writes has no apply
    path, or when its settings span more than one apply class without the
    combined executor.
  - Fixing a reason makes the test demand the entry's removal; a new reason
    makes it demand an entry. A control whose enabled binding is the literal
    false fails, a control that neither acts nor names an event counts for no
    setting, and the page must keep its own Auto-Detect.

## Known limitations

- Display Resolution shows at most 37 sizes. A display that reports more
  (scaled-resolution factors, long TV lists) loses its smallest sizes, which
  Custom still reaches by size. The stock page lists every size.
- Like every retained dropdown, these rows step through their lists with left
  and right and wrap (`Interaction::ChoiceKey`); Accept opens the list.
- No visible exclusive fullscreen Apply has been qualified on any platform:
  the automated runs keep the window hidden, and a mode switch would change
  the tester's display. The native Wayland observation follows SDL 3.4.16's
  Wayland driver (pinned by `tools/tests/sdl3_wayland_mode_contract.py`); no
  Wayland session has run it.
- SDL's colour-depth tie-break is not reproduced: the published list carries
  no format. It matters only where a rate exists solely at a shallower depth,
  which no supported platform is known to list.
- A recovery journal written by an earlier build for an exclusive Auto Apply
  can be refused by this build when the old pick was a rate the new rule does
  not choose: a journal left during its Keep countdown when the restore record
  holds an exclusive mode at the same size, and a confirmed journal whose
  commit did not finish. Startup then stops with "Settings startup recovery
  failed" on every launch until `ui-settings-recovery.dat` is deleted from the
  `baseoq4` save folder, which keeps the saved settings.
- Picking another display and then the original one again does not undo the
  size or rate a display pick reset; pick them again. A Custom size equal to a
  listed size counts as that listed size.
- Desktop Native in legacy startup uses the desktop's points and the page uses
  pixels, so on Retina and scaled Wayland desktops the two can choose
  different sizes (unchanged by this increment).

## Qualification

- `tools/tests/native/UiSystemDisplayCatalogTest.cpp`, in seven languages at
  two glyph widths:
  - the closed controls name the list selections;
  - the size list ends at Custom, and slots past it stay hidden;
  - a size pick sends its slot and token through `displayMode`, and a
    declined one keeps the accepted choice;
  - a rate pick, and the readback that acknowledges it;
  - both reserved slots, named when closed but never shown in their popups;
  - one display keeps both rows;
  - the desktop policy and missing lists disable and dim both rows to 45%,
    and the exclusive policy enables them at full strength even in a window;
  - the shared editing guards;
  - full lists (39 sizes, 15 rates) at every fit size, with the popup in the
    body, the last entry reachable and readable, and eight rows at
    1920×1080.
- The text-scale, choice-scroll and popup-placement tests include both rows.
  The text-scale test now fits each viewport to at least 640×480 dp, as the
  engine does, so 1280×720 at 200% runs at 150% as in play.
- The six SYSTEM page tests that check every locale (text scale, interface
  settings, render options, display controls, display lists, dimensions) now
  run as one Meson test per locale, 42 in all. The Windows CI runner, about
  three times slower than the development machine, had timed out the text
  scale, dimensions and popup placement tests on the previous page; split,
  none comes near its limit, and the runner spreads them over its cores.
  Popup placement, which takes no locale, now has 900 s.
- `tools/tests/native/DisplayModeRuleTest.cpp` checks the rule and compares
  its Auto pick with a transcription of SDL 3.4.16's closest-mode loop over
  20,000 random lists (60,134 comparisons), with Windows-style 16-bit copies.
- `ui_system_display.py`, `ui_settings_service.py`, `sdl3_strict_window.py`
  and `ui_settings_display_service.py` cover the rule in the planner, its
  restores and recovery and the strict window service, the restore replay,
  the pick resets (on an explicit and on the Auto display), descriptors under
  an exclusive mode on four drivers, the Wayland observation, the
  single-sample Vulkan request and its recovery records, including an OpenGL
  record recovering under Vulkan. No test runs the legacy path; its order
  (the shared rule first, the macOS exact-pixel search only for an explicit
  rate, then SDL's closest mode) is pinned by `sdl3_multidisplay_windowing.py`.
  `ui_system_settings_host.py` covers the up-front refusal of a stale rate and
  the rollback that skips it.
- `ui_retained_gate.py` checks both lists against the derivation above and
  that the gate follows them. `ui_system_session_route.py` opens the page
  under `ui_retained` alone exactly when both lists are empty.
- A Vulkan run on Windows/NVIDIA (`game/airdefense2`, hidden window, console
  menu verbs only) applied and kept a V-Sync change under 4x, MSAA 4x to off
  and off to 8x; the scene target followed each (`Forward render target MSAA:
  requested 0, effective 0`, then 8 and 8), and 16x rendered at 8x.

Evidence: `.tmp/ui/retained-system-resolution/validation-evidence.json`, SHA-256
`d65f5e8eaac90ec10de47c348fb076ec23ef83e8c7316da1c07e19f9bd7adf32`.

The SYSTEM page's own acceptance continues under `FLOW-002`.
