# SYSTEM performance presets

SYSTEM uses the same nine performance profiles and hardware-detection policy as
the existing engine commands (seven until Maximum and Low were added; the dated
sections below describe the ladder as it stood on their date). `framework/PerformancePreset` owns the profile
records, their ordered 55 assignments and the read-only detection decision.
Common retains its original setters, readback checks, modified flags and rollback;
it publishes the selected profile name only after all other writes succeed.

The retained settings service exposes two typed draft operations:

| Operation | Arguments | Result |
| --- | --- | --- |
| `settings.system.preset` | One string, `name` | Expand Minimum, Low Power, Low, Performance, Balanced, Enhanced, Quality, Ultra or Maximum into the current draft. |
| `settings.system.autodetect` | None | Observe the existing platform/memory signals and expand the selected profile into the current draft. |

These operations validate the entire merged draft. They replace exactly the
55 profile fields and preserve the other 19 catalog fields, including custom
Brightness and Ambient Brightness edits. They do not set CVars, run commands,
restart devices or persist configuration. The complete local Number draft
barrier also applies to these bulk changes.

`SettingsTransaction::EditGenerated` holds the owner and reentrancy guard while
the producer observes capabilities and builds the candidate. Invalid profiles,
unavailable observations, owner closure, attempted nested mutation, failed
validation or a caught allocation failure leave the previous draft intact.
Publication follows successful construction of the caller's result object. This
also covers the allocating result-copy boundary in MSVC's debug standard library.

Detection observes the live renderer snapshot. Hardware/capability constraints
precede platform power hints; CPU ISA is not a GPU performance score. It does
not invent a benchmark or treat memory size as measured frame-rate performance.

The emitter limit now declares its local archive policy at registration, so
lower preset drafts can edit it without relying on a previous legacy preset
command. Refresh rate, sky rendering and generated-image cache preferences
also declare their archive policy. A production-method regression checks the
actual declarations, effective protection and exact configuration selection;
unrelated renderer diagnostics and explicitly protected values stay protected.

The isolated and root standalone suites compile the production model and
transaction and exercise the actual Common, SYSTEM host, service and adapter
method bodies. Golden profiles, detection boundaries, all 55 assignments,
untouched values, reentry, failed allocation, legacy flag/value restoration and
the local draft barrier are covered. Windows and Linux sanitizer runs also
reject compiled behavioral mutations. The separate MSVC debug-library test
targets result storage; it does not claim recovery from termination inside
third-party `noexcept` operations or persistent process-wide memory exhaustion.

The opt-in production page places the selector and Auto-Detect action in a
wrapping band above the existing image/render columns. Their editable vector
parts come from the existing Post AA selector and Back button. All twelve language
tables supply the existing profile labels and Auto-Detect text. The compact
selector places its title and value on one row when they fit and wraps them
when translations need more room. Both controls
require an open, idle editing session without a confirmation, discard dialog
or unfinished local Number edit. Named Auto-Detect events use the same guard.

Popup highlight is transient. Selecting an option sends a typed proposal;
the service's authoritative draft readback precedes acknowledgement. Rejected
proposals preserve the accepted selection. Draft and baseline aliases survive
source-bound resource recreation without replaying an action.

The actual Runtime/RmlUi suite covers all seven profiles and combinations of
locale, wider deterministic glyph metrics, density and resized viewport.
It inspects every popup label, the longest collapsed translation, popup bounds
and focused Auto-Detect reveal. Opening-layout checks also preserve complete
footer targets, flowing status text and room for numeric controls at 200%.
The status and actions share a wrapping footer, while explicit button widths
prevent accidental full-row flex sizing. Buttons retain 44 dp minimum targets
and the visual contract's 18/24 dp action typography. Its counted host does not qualify the actual
engine font, device or settings-service route; gameplay captures provide
separate evidence for those integration boundaries.

Apply was gated at this increment. Since 10 November 2026 a preset or
Auto-Detect drafted on the page applies through the
[combined executor](settings-effect-execution.md#combined-executor): one
journaled attempt that restarts the renderer device and, for a speaker layout
or EFX change, the sound device, as the stock page's `applyPerformancePreset`
and APPLY did. Two settings finish at the next start and the page says so
(`image_ignoreHighQuality`, `s_maxSoundsPerShader`). The checked image and
audio results described in [effect execution](settings-effect-execution.md)
are not part of that executor. The complete page, editor, supported platforms,
all 271 migrations and final product gates remain unaccepted.

## Maximum-quality revision, 27 October 2026

Ultra now requests the maximum stable AA, shadow and post-processing quality;
Quality supplies intermediate costs. The [player guide](../../user/display-settings.md#performance-presets)
records the values and the distinction between quality and artistic controls.
The V3 SYSTEM catalog introduced 74 fields including shadow point resolution, filtering, atlas,
four-cascade policy, object vectors, post sample counts, HDR target policy and
diffuse mip reduction. The authored
page declares matching draft/baseline state. Effect-plan catalog V3 preserves
V1/V2 definitions; old display journals acquire newly added fields from the
current read without claiming ownership of them. Historical effect journals
retain their original catalog requirement.

On 28 October 2026 the user changed the fullscreen airdefense performance goal
to **Quality**, the preset immediately below Ultra, at 2560 x 1440 with the
Remastered HUD. Its 144 FPS cap is disabled for measurement; its graphics
settings are unchanged. The threshold remains **every measured frame <=6.67 ms**.
Earlier Ultra results retain their original workload and do not qualify Quality.
Ultra still selects maximum supported quality. Qualification remains open.

## Preset and detection audit, 28 October 2026

The ladder now separates constrained hardware (Minimum), power saving (Low
Power), modest/handheld graphics (Performance), native classic rendering
(Balanced), affordable modern effects (Enhanced), higher filtering and sampling
(Quality), and explicit maximum settings (Ultra). Enhanced fills the former
Balanced-to-Quality jump without moving Quality away from immediately below
Ultra. Existing profile names and the 55-field transaction footprint stay stable.
The added choice requires no effect-journal schema change: field types and
effect ownership are unchanged. All twelve translations and both menus share
the new ordering.

Enhanced keeps native scale, 2x MSAA and SMAA High, 1024/512 shadow maps, 5-tap
fixed PCF, no cascades, 12 SSAO samples, bloom and HDR tone mapping. Shadow
updates remain unlimited to avoid stale moving shadows. The five-level bloom
pyramid is retained for its spatial extent. Lower profiles reset hidden
filter/sample costs and do not request an unused HDR scene target. Material
normals/specular/skies, soft particles, light grids, authored room reverb and automatic speaker
layout remain available throughout. High-resolution DDS replacements stay
enabled, with low-tier texture ceilings where mip chains permit downsizing.

Renderer feature coverage deliberately excludes experimental PBR/visible-pass
promotion, temporal AA, optional translucent-moment shadows and debug modes.
Exposure, effect strengths, enhanced-material relighting, CRT styling, texture pixel-art filtering, UI size,
display mode and VSync remain independent preferences. Upload pool capacities
are resource budgets, not sliders to maximize. Ultra's supported quality
maxima are still subject to actual format/device resource limits.

The previous detector used host-wide VRAM, grouped ARM64 systems together, and
called `GetCardCaps`, whose implementation returned false unconditionally.
The replacement reads the initialized active renderer. Vulkan supplies its
physical-device class and largest dedicated local heap; integrated heaps never
become dedicated VRAM. OpenGL classifies the active renderer string, reads
dedicated memory only through the active context's NVX extension when present,
and otherwise retains an unknown memory observation. No OS-wide adapter total
or fabricated fallback can promote a hybrid, UMA, virtual or unknown device.
The renderer-module ABI is 28; rebuild/stage matching modules with the engine.

The pure policy covers software/legacy renderers, Pi, Deck, shared/discrete/
virtual/unknown GPUs, invalid memory, CPU-count and drawable-size boundaries.
It considers the actual drawable before render scale, limits automatic selection
to Enhanced, and refuses unavailable observations without mutating settings.
It intentionally needs no GPU model performance database. A safe recommendation
is not a measured optimum: AMD GL drivers without a dedicated-memory query and
unrecognized drivers start at Performance; users can raise it after playing.
Real hardware qualification is separate from simulated platform-policy cases.

### First-launch and constrained-device follow-up

The holistic follow-up keeps the seven-level ladder: Enhanced supplies the
needed intermediate modern-effects tier, while Quality and Ultra remain
explicit, progressively expensive choices. No additional level is needed.
Ultra retains its documented maximum-quality role rather than becoming an
automatic recommendation or a performance promise.

Fresh installations now bootstrap at Minimum until a real renderer exists,
then use the same canonical detection/expansion as Auto-Detect and recreate
video/audio resources before startup map commands. The old frequency-based
four-tier classifier no longer chooses initial settings. `setMachineSpec`
also derives its legacy classification from the shared policy, without
guessing VRAM from system memory. An existing saved config is preserved when
only its detection marker is missing; settings recovery still takes precedence.
An explicitly named first-launch profile is respected. A scoped, allocation-free
assignment capture preserves launch/platform/autoexec overrides even when they
assign the bootstrap's current value. It observes accepted CVar setters before
the unchanged-value early return and ends before detection writes its profile.
The public preset marker remains a valid enum value throughout startup.

Platform hints now restrict a hardware recommendation instead of bypassing
smaller memory budgets. Verified discrete memory of at most 512 MiB selects
Minimum even with a Deck, Pi or low-power hint. Android observations impose a
Performance ceiling, preserving stricter memory/capability limits. ARM64 by
itself still has no effect. GL names now distinguish integrated Radeon R5/R7
Graphics and newer Radeon/Intel integrated families from discrete cards, and
recognize Apple GPU and Tegra UMA names. Missing dedicated-memory reporting
continues to remain unknown.

The Enhanced detection band admits 15360 MiB system RAM and 7680 MiB dedicated
VRAM so small firmware/driver reservations do not demote nominal 16/8 GiB
systems. Raw reported memory is preserved; shared RAM is never substituted.

GLES capability observation also accepts the standard `OpenGL ES GLSL ES`
version prefix: ES 3.x can run translated post shaders; ES 1.00 and desktop
GLSL 1.20 remain ineligible. The production parser is compiled in the policy
regression suite alongside the active-device observer.

### Validation and limits

Evidence is retained under `.tmp/preset-audit/`, summarized in `audit.json`:

- Full Windows build/staging passed. The production profile, Common observer,
  transaction, host and adapter tests passed with allocation-failure and mutation
  controls: `.tmp/performance-presets-dft0kzwh/result.json`.
- The same portable suites passed under Linux ASan/UBSan, including mutation
  controls: `.tmp/performance-presets-87qdhewl/result.json`. Named scenarios cover
  Apple Silicon/UMA, x86 integrated graphics, discrete GPUs, GLES, Pi, Deck,
  software/virtual renderers, unavailable data and memory/CPU/resolution bounds.
- The three Meson preset suites passed, including actual Runtime/RmlUi layouts
  for all seven choices in twelve locales, density/resizing and wider glyphs.
  Menu coverage, frame-rate policy, renderer ABI/settings/timing contracts and
  the unchanged strict airdefense workload contract passed.
- `gl-final/report.json` and `vk-final/report.json` pass all seven presets in
  live `game/airdefense1` gameplay, starting with Ultra and switching down,
  recreating the device for each profile. Engine-render-target captures were
  visually inspected. The window stayed at 1280x720; no native input was used.
  Vulkan validation reported no errors. Runtime identities stayed unchanged.
- NVIDIA RTX 4060 Laptop auto-detection selects Enhanced on both APIs, using
  8188 MiB dedicated GL memory and 7956 MiB Vulkan local memory respectively.
  `vk-integrated-final/report.json` verifies native Intel Iris Xe selects Balanced
  at 1280x720 with dedicated memory reported as zero, not another GPU's VRAM.
- The initial GL sequence and first Vulkan attempt exited with code -1 without
  an engine error/crash report. They remain under `gl/` and `vk/`; isolated serial
  repeats passed. The shared UI audit observed the same unexplained exits.
  `vk-integrated/` was an invalid integrated test: forced index 1 selected a
  Vulkan 1.2 Dozen adapter and correctly fell back to GL. Native Intel is index 2
  in this executable's enumeration. No fallback run is counted as Vulkan proof.

Physical Linux/macOS/Android/ARM64, AMD GPU and handheld qualification was not
performed in this audit. Their policy coverage is simulated, and conservative
defaults remain recommendations rather than measured optima. The separate
2560x1440 fullscreen 150 FPS goal and checkpoint-stall work remain paused/open.
Earlier cleanup was deferred after automatic approval review. The follow-up
uses the user's standing cleanup permission to remove verified disposable
test binaries, caches and the obsolete one-shot wiring helper, while retaining
useful source fixtures, logs, configuration records and accepted engine captures.

### Classic-renderer and saved-config correction

A further review found two gaps in the earlier simulated platform coverage.
Apple's supported GL 2.1 compatibility context has GLSL 1.20: it can draw the
classic material path, but cannot compile the GLSL 1.30 post shaders. The
observer incorrectly classified that optional-effect limitation as weak
hardware, selecting Minimum. It now checks GLSL material support or the complete
ARB vertex/fragment program pair and a 2048-pixel texture limit separately from
modern-effect eligibility. Balanced's classic lighting does not require modern
effects. Capable shared-memory hardware can therefore use Balanced on this
context, subject to the existing RAM, CPU and drawable limits. Missing optional
FBO/post support does not imply poor classic throughput; unsupported effects
still use the renderer's existing fallback and diagnostics.

SDL3 builds now observe system RAM through SDL on Windows, Linux, macOS and
Android. Failed observations remain unknown instead of inheriting platform
fallbacks of 512, 1024 or 2048 MiB. Dedicated GPU memory continues to come only
from the active renderer; shared memory is never relabeled as VRAM.

A live saved-config reproduction also found that an unset `com_machineSpec`
invoked the old `execMachineSpec` recipes during startup, replacing saved AA,
render scale, texture settings and VSync. Startup now repairs the classification
alone. The seven canonical presets, their values, localization and 55-field
transaction footprint remain sufficient and unchanged by this correction.
Quality and Ultra remain explicit choices; no unmeasured platform is promoted.

New evidence belongs under `.tmp/preset-platform-followup/`; the earlier
`.tmp/preset-review/` results below retain their original scope. In particular,
the earlier saved-config test set a valid legacy classification, so it did not
exercise this second missing-marker case. Physical Apple/ARM/mobile validation
is still separate from compiled production-observer tests.

The correction passes the Windows build/stage, all three Meson preset suites,
and Windows plus Linux ASan/UBSan production-model/observer/transaction tests
with 18 rejected mutations each. `saved-before/` reproduces saved MSAA 0 and
scale 67 becoming 2 and 100; `gl-final/` preserves 0 and 67 and passes all seven
profiles. `vk-final/` passes all seven plus fresh first-launch detection, and
`vk-integrated/` selects Balanced on native Intel Iris Xe with zero dedicated
VRAM. All runs are windowed 1280x720 SP gameplay with unchanged runtime hashes;
all fourteen profile captures were visually inspected. These are functional
checks, not timing qualification. `audit.json` records exact reports and limits.
Automatic approval review blocked both bulk cleanup and a single verified
temporary-directory removal without a specific reason beyond "blocked by
policy"; generated caches and test binaries remain beside the retained evidence.

### Drawable workload and legacy-command follow-up

Reviewing 4K/5K UMA cases exposed a gap in tier-only resolution handling:
Performance still rendered at 85%, over six million scene pixels at 4K.
Detection now resolves a copy of the selected immutable profile with an
automatic scene scale and supported post-AA mode. Console Auto-Detect, first
launch and the typed SYSTEM host consume that same resolved record. Named
manual profiles and the 55-field effect/journal footprint are unchanged.

The detector selects the largest existing menu scale at or below the template
(100, 85, 75, 50, 25 or 10 percent) within these scene-pixel ceilings:

| Automatic hardware/profile case | Scene-pixel ceiling |
|---|---:|
| Minimum, including software and limited classic renderers | 960 x 540 |
| Low Power, including Pi/explicit power limits; Android | 1280 x 720 |
| Shared, virtual, unknown or modest discrete memory; Deck | 1920 x 1080 |
| Verified discrete memory above 2 GiB, at least 8 GiB RAM | 2560 x 1440 |
| Verified discrete memory at least 7680 MiB and RAM at least 15360 MiB | 3840 x 2160 |

The first applicable constrained case takes precedence. Missing/invalid
drawable dimensions do not invent a render scale; the existing Performance
tier ceiling still applies. Arithmetic uses 64-bit products through the accepted
32768-per-axis limit. Extreme drawables can exhaust the supported 10% scale
floor; this is a conservative viewport-work recommendation, not a guarantee
about total render-target memory, sustained FPS or hardware power consumption.
The UI/window retain their own resolution. VR eye scale and headset pacing stay
separate. The seven-level ladder needs no extra level for display size.

Post AA is observed separately from classic rendering and full modern effects:
native Vulkan supplies it, while GL requires GLSL programs, GLSL 1.30 and FBOs.
Auto-Detect leaves it off when unavailable without demoting capable classic
rendering. Manual choices retain the renderer's existing capability fallback.

The deprecated four-way `execMachineSpec` recipes have been removed. Its 0..3
choices map to Low Power, Performance, Balanced and Ultra through the same
checked, atomic 55-field application. An unset/out-of-range spec or extra
argument is refused without writes. Stock `video recommended` directly invokes
Auto-Detect, and all stock quality commands refresh the full quality GUI even
when their coarse spec marker is unchanged. No old resolution/VSync rewrites,
host-wide VRAM queries or obsolete image CVars remain in that route.
Pre-device `setMachineSpec` classification also uses SDL's real RAM result,
including unknown on failure, instead of the legacy getter's invented fallback.

Validation for this follow-up is recorded under `.tmp/preset-resolution-audit/`.
The final Windows and Linux ASan/UBSan production-policy suites pass all 22
mutation controls with unchanged inputs. All three Meson profile/SYSTEM suites
and the menu source contract pass. A full Windows build/stage passes, followed
by windowed SP first-launch/Auto-Detect and four legacy-command checks on
NVIDIA OpenGL and Vulkan. Both select Enhanced at native 1280x720. Native Intel
Iris Xe Vulkan selects Performance at 75% for a 2400x1280 drawable: post-AA
targets are 1800x960 while the engine capture remains 2400x1280. All three
accepted captures were inspected; runtime identities remain unchanged during
each probe and Vulkan reports no validation errors.

The requested 4K window was constrained by Windows to 2538x1312. Detection
correctly used that actual size and selected 75%, but the harness rejected the
run as 4K evidence. That report is retained separately; 4K/5K and other physical
platforms remain policy-tested, not hardware-qualified. These are functional
checks, not FPS qualification.

The shared stage also exposes unrelated in-progress UI errors: the Multiplayer
page supplies a non-string Button context, and the new air-gauge binding has an
incorrect component count, triggering stock-screen/HUD fallbacks. The latter
was reported to the multiplayer owner; both owners' separate goals are now
paused and these asset errors remain unresolved. A vector wordmark coverage overshoot appears at the Windows-constrained
size. Those warnings are retained in the logs and are not claimed as clean UI
qualification. Earlier full-seven-profile gameplay evidence remains above;
this follow-up does not change the manual profile values.
Automatic approval review again rejected verified temporary cleanup with only
"blocked by policy" as its reason. The exact link-checked targets are recorded
in `cleanup-verified.json`; disposable binaries/caches remain alongside the
useful reports, logs, configurations and engine captures.

### Earlier follow-up validation

The follow-up evidence is under `.tmp/preset-review/`, summarized by `audit.json`.
Windows build/staging, all six selected Meson profile/UI/journal suites and menu,
startup-language and Steam Deck source contracts pass. The Windows production
model/Common/host suite passes 15 mutation controls with intact input hashes
(`.tmp/performance-presets-g1g2t9a1/result.json`); Linux ASan/UBSan passes the same
controls (`.tmp/performance-presets-jspkvmkd/result.json`). Each run includes over
65,000 model/transaction checks, the Android observer branch and bounded nested
CVar assignment capture.

`gl-final/` and `vk-final/` pass all seven profiles in live windowed 1280x720
`game/airdefense1`, with a device restart and engine capture for each profile.
All fourteen profile captures were visually inspected. First launch and explicit
Auto-Detect both select Enhanced on the NVIDIA RTX 4060 Laptop; native Intel Iris
Xe Vulkan selects Balanced with zero dedicated memory (`vk-integrated/`).
Vulkan validation is clean. No mouse/keyboard input was injected or captured.

`startup-overrides/` verifies a first-launch Quality selection from autoexec,
including explicit MSAA 0 and scale 50 assignments equal to the temporary
Minimum values. `saved-config/` verifies a missing spec marker does not trigger
detection over existing Quality/MSAA 0/scale 67 settings. `gl/` retains the
initial failed first-launch report/log: an invalid empty enum marker normalized
to Balanced. The corrected path keeps a valid marker and observes assignments
separately. A transient unrelated UI model-scene link failure and concurrent
source/build changes required reruns; only passing, hash-bound results are
counted. The stock shotgun rigid-body inertia-clamp diagnostic remains unrelated
to presets. These results do not extend physical platform or frame-time coverage
beyond the limits above.

## Rendering backend and device selection, 28 October 2026

The retained SYSTEM performance band now contains backend, device, preset and
Auto-Detect controls together. All twelve language tables cover the new labels,
availability and restart guidance. Long GPU names wrap; incompatible entries
are disabled; a missing saved UUID has an explicit unavailable value. The
active backend/device readout remains separate from the draft. Disabled
controls use the visual contract's 0.40 opacity.

Fresh desktop `r_renderApi best` configurations try Vulkan with OpenGL fallback;
Android keeps GLES. Existing explicit backend choices are preserved. This is
the user-requested high-performance device policy, not a new Vulkan platform
qualification: Linux/macOS Vulkan remains experimental. Native GL/GLES GPU
routing belongs to the OS/driver. Choosing a named device selects Vulkan too;
choosing Auto restores `best` and `r_renderDevice auto` together.
Automatic preserves OpenGL when PC OpenXR is enabled, retaining the current
VR graphics binding. Its saved/CLI enable state is read before renderer boot.
Explicit Vulkan selection remains explicit and cannot provide VR rendering.

Both Vulkan's activation probe and actual surface admission enumerate the full
inventory and rank compatible devices: discrete, integrated, virtual, other,
then CPU. Only discrete GPUs use dedicated local capacity as a tie-breaker;
shared/software heaps cannot outrank discrete hardware. UUID resolves ties
independently of driver enumeration order. Capacity is not a speed benchmark.
After automatic startup creation failure, retries advance through this ranked
list, including candidates at lower original indices. Explicit UUID/index
selection never silently substitutes another Vulkan GPU.

`r_renderDevice` persists the lowercase 32-character Vulkan device UUID.
`r_vkDevice` remains a temporary diagnostic index override and disables menu
GPU selection. Saved/CLI preferences load before the startup probe; the live
device consumes a separate boot snapshot, so `vid_restart` cannot activate an
unapplied GPU preference. `rendererDevices` reports bounded owned inventory
(32 entries), the actual device identity and the automatic preference. The
menu caches inventory per owner and rechecks IDs, names and suitability before
accepting an explicit pick. Hotplug/list drift rejects a stale pick. A live
Vulkan query uses the existing instance; a GL-side query loads only the Vulkan
inventory probe and restores module completion callbacks before unloading it.
The inventory checks core Vulkan requirements; real surface/presentation and
resource admission still run during activation.

Backend/device Apply writes preferences through the existing settings
transaction. Restart is a separate, one-shot action: only a clean owner at the
main menu can use it, after checked config persistence. It consumes the current
process's original API/GPU launch overrides, retires session/UI/game/decl and
renderer owners, then unloads and reactivates the module. The old module cannot
be unloaded from its own renderer stack or an active GUI callback. Window/input
launch options remain intact. Auto-Detect is unavailable while hardware choices
are pending, so it cannot recommend settings for a GPU that has not started.

The SYSTEM catalog is now 76 fields. Effect-plan V4 adds the two engine-restart
preferences without changing V1/V2/V3 definitions. Old display journals migrate
their added fields from the current snapshot without owning them. Renderer ABI
29 appends the owned inventory export; engine and renderer modules must be
replaced together. This work does not bypass the existing complete-preset live
Apply gate described above or accept the whole SYSTEM product/performance gate.

### Selection validation

Evidence is under `.tmp/rendering-selection/`. The Windows build/stage passes.
Native selectors pass 67 renderer, 36 probe and 25 instance/presentation cases,
including hybrid enumeration order, software/shared heaps, capability refusal,
UUID stability/missing IDs, forced overrides, overflow and ranked retries.
The same selector passes Linux ASan/UBSan. Production rendering-settings bodies
pass 162 checks each for desktop and Android branches on Windows and Linux
(sanitized on Linux). Startup lifecycle tests cover config/CLI precedence,
one-shot override consumption, owner teardown and module replacement.
The startup suite also covers OpenXR-enabled/disabled desktop builds, Android
defaults, and the saved/CLI VR-state precedence without starting a VR runtime.

The real settings host, 117 service scenarios, 1,047 display-service checks,
historical journals and all 22 preset fault mutations pass. The actual
SYSTEM/RmlUi layout suite passes 115,299 checks across twelve locales, 40%
glyph-width stress, small windows and increased UI/text size, including long
GPU labels and unavailable-device state. These tests use no native input.

Windowed, hidden SP `game/airdefense1` runs use installed stock assets and the
stable staged runtime. Engine-command captures and hash-bound reports verify:

- `default-final/`: fresh Automatic selects the NVIDIA RTX 4060 Laptop and
  detects Enhanced at 1280 x 720.
- `integrated-final/`: a quoted saved Intel Iris Xe UUID is consumed before
  startup and detects Balanced at 1280 x 720.
- `missing-final/`: an unavailable saved UUID falls back to OpenGL with the
  correct active renderer and a localized unavailable-device notice.
- `menu-reload/`: GL plus conflicting launch API/UUID preferences changes to a
  named NVIDIA GPU through the real menu, Apply and full engine reload.
- `backend-gl/`, `menu-integrated/`, `menu-auto/`: real menu switches from
  Vulkan to GL, GL to named Intel Vulkan, and GL/missing UUID to automatic NVIDIA
  respectively; each reaches gameplay, detects the actual GPU and captures the
  final menu and complete detected-device list. Unsupported translated/software
  Vulkan entries remain visible and disabled. Vulkan validation is clean.
- `accepted/`: the final staged build repeats Automatic selection from GL with
  a conflicting launch UUID after the OpenXR compatibility correction. It
  reaches NVIDIA Vulkan gameplay and retains the final menu/device-list
  captures. `audit.json` indexes all eight passing runtime cases and their
  individual binary hashes; failed earlier cases are not counted as passes.

Initial failed reports are retained as diagnostic history: `default/` exposed
unquoted diagnostic aliases in the harness; `saved-integrated/` exposed an
unquoted manually authored UUID (the engine's saved configs already quote it);
`menu-switch/` exposed the old module surviving reload; `menu-final/` captured
the final UI but its unquoted hyphenated screenshot path was rejected. Passing
runs above supersede those failures. No actual macOS, Android, Linux GPU, AMD,
eGPU hotplug, multi-discrete performance comparison or frame-rate qualification
is claimed; those platform/driver gates remain open.

Unrelated existing findings remain: malformed Remastered marine HUD binding
117 falls back to the stock HUD; the multiplayer page has an invalid
`contextButton` value; stock non-precache and rigid-body diagnostics occur in
the development runtime. Earlier wordmark fractional coverage and Meson/RmlUi
configuration warnings remain outside this change. Detached clean worktrees
`.tmp/wt-hotfix-0133` (67366e2d8, last 23 October) and `.tmp/wt-pr202`
(4ab985d9d, last 26 October) look inactive but retain unmerged commits; review
those commits before authorizing removal. `.tmp/wt-hf` and `.tmp/wt-ui-round`
have local changes and were preserved.

## Hardware-range audit, 10 November 2026

A review of the ladder against the hardware openQ4 supports, from the
[documented minimum](../../user/getting-started.md#system-requirements) to
current high-end PCs and phones, found four gaps. The ladder is now nine
profiles; the 55-field footprint, the SYSTEM catalog and the effect journal are
unchanged, because every value below was already a preset target.

### The bottom of the ladder shed no rendering cost

Minimum and Low Power differed from Performance only in scene scale, texture
size, sound samples and the frame cap. Every rendering feature stayed,
stencil shadows included, so the two profiles for hardware below the minimum
requirements removed no work except pixels. The
documented minimum PC (4 GiB RAM, a 1 GiB GPU) was also sent to Low Power,
whose 30 FPS cap is a battery choice, not a desktop one.

| Profile | Scene | Shadows | Cap | For |
|---|---|---|---:|---|
| Minimum | 50% | none | 30 | Software and limited renderers, 2 GiB systems, 512 MiB GPUs |
| Low Power | 75% | none | 30 | Raspberry Pi, phones with at most 4 GiB, explicit low-power requests |
| **Low** (new) | 75% | stencil | 60 | The documented minimum PC: at most 4 GiB RAM or a 1 GiB GPU |
| Performance | 85% | stencil | 60 | Modest desktops, handhelds, phones |

`r_shadows` was already written by every profile as the constant 1; it is now a
per-profile value (`PerformancePreset::shadows`), and the self-test requires
0 on exactly Minimum and Low Power and a non-decreasing progression. Low takes
Low Power's texture and sound budgets with 2x anisotropy. The stock four-way
quality control maps Low to the new profile, so no stock choice turns shadows
off. A configuration already on Minimum or Low Power keeps its shadows until
the profile is selected again.

### Auto-Detect stopped at Enhanced

A 24 GiB card received the same recommendation as the 8 GiB reference laptop,
and Balanced at 4K. FPS-02 measured that laptop holding Quality at 100 FPS and
Ultra at 75 FPS at 2560x1440, so a card in a larger memory class, which is at
least as fast, now takes the profile above. Each resolution class above 1440p
costs one profile:

| Verified dedicated memory | Up to 1440p | Up to 4K | Above 4K |
|---|---|---|---|
| 8 GiB class (7680 MiB) | Enhanced | Balanced | Performance |
| 12 GiB class (11520 MiB) | Quality | Enhanced | Balanced |
| 16 GiB class (15360 MiB) | Ultra | Quality | Enhanced |
| 24 GiB class (23040 MiB) | Ultra | Ultra | Quality |

Each floor is 15/16 of the nominal size, as the 8 GiB floor already was. The
8 GiB row is the previous behaviour. Quality and Ultra shadow-map every light,
so they also require eight logical CPUs, 15360 MiB of system RAM and the
modern effects. Maximum has no frame-rate target and is still never detected.
Dedicated memory remains a capacity class and not a benchmark: an old
workstation card with a large frame buffer is slower than its class suggests,
and the owner lowers the profile.

### Resolution classes assumed 16:9

Class limits were the exact 16:9 pixel counts, so a 1920x1200 laptop with
shared-memory graphics fell out of the 1080p class into Performance at 85%, and
a 1080p display on a 720p budget rendered 960x540 because 75% missed the
budget by one percent. `PerformancePresetClassPixels(h)` is now 2.25 h^2
(shapes up to 20.25:9): 1920x1200, 2256x1504 at 75%, 2560x1600 and 20:9 phones
sit in the class of their 16:9 sibling, and the next 16:9 class up fits at 75%.
The same classes bound the tier ceilings and the scene budgets. A discrete card
between 2 and 8 GiB at 4K therefore renders a 2880x1620 scene instead of
1920x1080.

### Phones and other power-limited devices

Android kept the Performance ceiling but every phone rendered a 720p-class
scene, which gave the Galaxy S25 (Adreno 830, 2340x1080) a 50% scale although
the AND-02 runs hold 60 FPS there at native resolution with 4x MSAA in five of
six campaign scenes. A phone reporting at least 10240 MiB (the 12 GB class)
now keeps the 1080p class and renders 85%; smaller phones keep the 720p class;
one with at most 4 GiB selects Low Power instead of Low. A detected frame cap
followed the display on every device, which raised a 120 Hz phone and a 90 Hz
handheld from Performance's 60 to the panel rate. A power-limited device
(phone, Steam Deck, Raspberry Pi, explicit request) now keeps the lower of its
profile's cap and the display rate.

### Active-device names

`RendererClassifyGLDevice` compared raw driver strings, so the same GPU
classified differently by driver spelling. It now matches lower-case text with
`(R)`/`(TM)` removed and spaces collapsed, which fixes `AMD Radeon(TM) 780M`
and `Radeon (TM) RX 480 Graphics`, and it recognizes Mesa's
`Intel(R) Xe Graphics`, Radeon 8040S/8050S/8060S, Intel Arc B390/B370
(integrated, unlike the discrete B5xx/B7xx), NVIDIA TITAN, Immortalis and
Xclipse names and the Steam Deck's `AMD Custom GPU`. Vulkan still reports its
own device class and is unaffected.

### Not changed

Shared-memory GPUs have no capacity signal, so Apple Silicon and x86
integrated graphics still share Balanced at the 1080p class; a fast Apple or
AMD integrated GPU starts lower than it could. OpenGL on AMD and Intel
discrete cards still has no dedicated-memory query and starts at Performance;
fresh desktop configurations use Vulkan, which reports it. iOS is not an
integrated target (IOS-03).

### Validation

Evidence is under `.tmp/preset-scrutiny/`.

- `ui_performance_presets.py --mutations` passes: 143,760 model and transaction
  checks, the real SYSTEM host and Common bodies, the Android observer, and 29
  rejected mutations. Seven are new: the shadowless floor, a phone keeping
  shadows, the top tiers without the CPU requirement, a capacity class, a
  resolution step, the power-limited cap and the phone scene budget.
- The three Meson preset suites pass, including the Runtime/RmlUi SYSTEM page
  with nine options in twelve locales. `settings_menu_coverage.py`,
  `presentation_hf_contract.py` and `ui_system_presets.py` pass.
- After a Windows build and stage, `performance_presets_smoke.py` passes on
  Vulkan (`vk/`) and OpenGL (`gl/`): all nine profiles in live windowed
  `game/airdefense1`, Maximum first and then down the ladder with a device
  restart for each, the self-test (`presets=9`), the four stock quality
  commands (Low now applies `low` at 75%), and first-launch plus explicit
  detection, which select Enhanced on the RTX 4060 Laptop on both backends.
  `vk-intel/` selects Balanced on the Intel Iris Xe. The Low Power and Low
  captures were inspected: the trench shadow is absent in the first and present
  in the second.

Limits:

- **What dropping shadows saves is measured on one integrated GPU only.** See
  [the shadow saving](#the-shadow-saving-on-integrated-graphics) below. A
  weaker or tile-based GPU (Raspberry Pi, a phone) may differ and has not run
  it.
- The 12, 16 and 24 GiB recommendations, the phone rules, Raspberry Pi and
  macOS are covered by the model suite only. No such device ran this build.
- Today's shadow-map commits changed what Quality, Ultra and Maximum cost; the
  FPS-02 budgets the capacity table leans on have not been re-measured since.

### The shadow saving on integrated graphics

Measured later on 10 November with `performance_suite.py`, fullscreen 1920x1080
on the Intel Iris Xe through Vulkan (`low+r_vkDevice=2`, a 1440x810 scene, frame
cap and VSync off). Each launch is a ladder of six-second windows: Low, Low
with `r_shadows 0`, Low, Low at 50% scene scale, Low. A toggle is compared with
the mean of the Low windows on either side of it, using the frame time from
`profileFrameWork`.

| Launch (`shadow-ladder-1/`) | Low | No shadows | 50% scene |
|---|---:|---:|---:|
| `combat_airdefense1`, round 1 | 16.4 ms | 13.8 ms (-15.5%) | 13.1 ms (-21.6%) |
| `indoor_hub2`, round 2 | 11.4 ms | 9.8 ms (-14.1%) | 9.1 ms (-19.5%) |
| `combat_airdefense1`, round 2 | 16.4 ms | 14.5 ms (-12.4%) | 14.9 ms (-8.3%) |

Dropping stencil shadows removes 12 to 16 percent of the frame, about what the
step from a 75% to a 50% scene removes (8 to 22 percent). The Low windows of a
launch agree within 4 to 6 percent. That supports the shadowless floor: the two
profiles below the minimum requirements now shed about as much again as their
resolution step, and Low keeps the shadows for a PC that can afford them.

Only these three launches count. The other three of that run (both
`indoor_storage2` launches and one `indoor_hub2`) lost the foreground and were
held at the 60 Hz background cap, which shows as `bound=presentation-cap` in
the pacing snapshot; their GPU was never saturated, so they price CPU work
only (under 1 ms either way). A second run (`shadow-ladder-2/`) overlapped
another session's build and is not counted. The earlier windowed attempts
(`low-ladder/`, `shadow-cost/`) ran during builds, ranged from 20 to 95 Hz at
identical settings and were discarded. The suite's idle-GPU check reads the
NVIDIA adapter, so on an Intel run read `bound` instead.
