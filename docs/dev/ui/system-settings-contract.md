# Production SYSTEM settings contract

This is the implementation contract for the complete production SYSTEM page in
M2 of the [product completion plan](../plans/ui-product-completion.md). It follows
the [visual design requirements](../ui-visual-design.md) and the
[full requirement register](product-requirements.md). It is not a new diagnostic
screen or an acceptance record for the shipped GUI corpus.

The transaction core, fixed 53-field engine catalog and typed application service
are implemented. Immediate changes can be drafted, validated, applied, canceled
and restored through the service. The [display confirmation integration](display-confirmation.md)
adds frame-owned Apply/Keep/Revert, durable recovery and strict initial-device
recovery for eligible confirmation documents. Audio, image/resource reload,
next-map and preset-expansion effects still block the complete batch before live
writes. Production controls, editor support and full product qualification remain
required. Earlier immediate-only evidence below describes its recorded checkpoint;
the newer display contract defines the current integration and its limits.

## Source identity and precedence

The audit baseline is engine commit
`15e35beaf11a1f1942ba25e9431211f64c1ef7f1`. Repository content, the matching members
of `.install/baseoq4/pak0.pk4`, and the existing native import sources agree on
the following SHA-256 values. Staged packages are evidence, not editing targets.

| Effective VFS resource | SHA-256 |
| --- | --- |
| `guis/mainmenu.gui` | `adf6141b459b582e5d7ed7ba6edc15d23469a5c70be43e1e3724324558c359af` |
| `guis/menu/settings/system.gui` | `768e2c015e24b0d7e648a08e5df39164008ebfade6b01eda9308beacb577b361` |
| `guis/menu/settings/popups.gui` | `aa76411d20362ca122b9707dc43e2cd872201bbfe65bbd290a6a619143bb8988` |
| `guis/menu/settings/audio.gui` | `6b75ecb3849cbbb9d751452b5c03659bc8f435ceaa3729cc8478533b729849ae` |
| `default.cfg` | `23187ea6f205903241b30909f2eeadd07a7c62d39c7015b1a540b14ce6fd21c0` |

The installed retail main menu is successively overridden by
`pak001`, `pak014`, `pak016`, `pak018`, `pak019`, `pak021`, `pak023`, `pak024`, and
`pak025`. The last retail version has SHA-256
`c4bf96513314718eea55df2ace0a7a44562d1e95fd5657194f472019a1c617c6`.
openQ4's `content/baseoq4/pak0/guis/mainmenu.gui` takes precedence over it.
The separate SYSTEM, popup and AUDIO includes originate in openQ4's content
override. The effective main menu includes them alongside Controls and Game.
See the [baseline main-menu source](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/content/baseoq4/pak0/guis/mainmenu.gui#L5786).

Local native-preprocessor evidence is retained under
`.tmp/ui/import/sp-gl-corpus2/save/baseoq4/ui-import/`: `00037.json` is the main
menu, `00102.json` the popup include, and `00103.json` SYSTEM. The latter consumes
all 4,381 native tokens. Main-menu token provenance identifies the included
resources in staged `pak0.pk4`. `.tmp/ui/legacy-inventory-import2.json` records
the effective inventory and source precedence. These local files are not
packaged documentation or migration acceptance.

Retail SYSTEM also contained sound controls. The effective openQ4 menu exposes
those in AUDIO. Preserve the effective product coverage rather than recreating
the older arrangement or counting duplicate popup controls as new features.

## Complete current page inventory

SYSTEM has 31 distinct setting options and a separate Auto-Detect action. Its
37 native choice/slider/edit widgets include two hidden resolution duplicates,
paired brightness and ambient numeric editors, a section choice and a scrollbar.
The 31 options map to 30 real CVars because ambient enable and ambient brightness
share one CVar. GUI dictionary keys and scrollbar state are not host settings.

The table gives declared CVar defaults, not the player's archived values,
autodetected values, or the result of the global Defaults button. Boolean
choices use `#str_200059` (No;Yes) unless noted. Existing translated labels remain
authoritative; numeric values and device names are data.

| Option / native control | Label | Host representation and declared default |
| --- | --- | --- |
| Performance / `set_sys_perf_preset_val` | `#str_229976` | String `com_performancePreset=balanced`; minimum, lowpower, performance, balanced, quality, ultra; choice labels `#str_229977` |
| Resolution / `set_sys_screensize_val_0` | `#str_229975` | Coupled integer `r_mode=-2`, dimensions below; host-provided mode list, not a fixed integer range |
| Fullscreen / `set_sys_fullscreen_val` | `#str_200147` | Boolean `r_fullscreen=1` |
| Brightness / `set_sys_gamma_slider`, `set_sys_gamma_value` | `#str_200148` | Float `r_brightness=1`, 0.5..2; slider step 0.1; the widget name does not make this `r_gamma` |
| Shadows / `set_sys_shadows_val` | `#str_200160` | Boolean `r_shadows=1` |
| Specular / `set_sys_specular_val` | `#str_200161` | Enabled is the inverse of Boolean `r_skipSpecular=0` |
| Bump maps / `set_sys_bump_val` | `#str_200162` | Enabled is the inverse of Boolean `r_skipBump=0` |
| Detailed sky / `set_sys_sky_val` | `#str_223009` | Enabled is the inverse of Boolean `r_skipSky=0` |
| Force ambient / `set_sys_ambient_val` | `#str_223002` | Derived Boolean from `r_forceAmbient > 0`; existing enable writes 0.7 and disable writes 0 |
| Ambient brightness / `set_sys_ambientbr_val`, `set_sys_ambientbr_valnum` | `#str_223003` | Float `r_forceAmbient=0`, 0..1; slider step 0.025 |
| V-Sync / `set_sys_vsync_val` | `#str_41092` | Integer `r_swapInterval=0`; page choices 0/1 |
| MSAA / `set_sys_msaa_val` | `#str_41093` | Integer `r_multiSamples=0`; page choices 0/2/4/8/16, labels `#str_200165`; CVar spelling is case-insensitive |
| Post AA / `set_sys_postaa_val` | `#str_41094` | Integer `r_postAA=0`, 0..4; `#str_41095`: Off, SMAA Medium/High/Ultra, Color Edge |
| Resolution scale / `set_sys_supersample_val` | `#str_41091` | Integer `r_screenFraction=100`, 10..200; page choices 10/25/50/75/85/100/125/150/200 percent |
| Borderless / `set_sys_borderless_val` | `#str_229909` | Boolean `r_borderless=0` |
| Fullscreen policy / `set_sys_fullscreen_policy_val` | `#str_229910` | Boolean `r_fullscreenDesktop=1`; `#str_229911` Desktop/Exclusive maps to 1/0 |
| Display / `set_sys_display_device_val` | `#str_229912` | Integer `r_screen=-1`; Auto(-1) or a current zero-based display index |
| Span displays / `set_sys_multiscreen_val` | `#str_229915` | Integer `r_multiScreen=0`, 0..1; `#str_229916` Primary Display Only/Span All Displays |
| Renderer fallback / `set_sys_vidqual_val` | `#str_41103` | String `r_renderer=best`; page values best/arb2, labels `#str_41104` Auto/ARB2; this is not `r_renderApi` |
| Bloom / `set_sys_bloom_val` | `#str_41082` | Boolean `r_bloom=0` |
| SSAO / `set_sys_ssao_val` | `#str_41088` | Boolean `r_ssao=0` |
| HDR tonemap / `set_sys_tonemap_val` | `#str_41084` | Boolean `r_hdrToneMap=0` |
| CRT / `set_sys_crt_val` | `#str_41090` | Boolean `r_crt=0` |
| Irradiance volumes / `set_sys_irradiance_val` | `#str_41107` | Boolean `r_useLightGrid=1` |
| Light-grid preload / `set_sys_preload_val` | `#str_42820` | Boolean `r_lightGridPreload=0`; `#str_42821` explains next-map application and memory/load tradeoff |
| UI aspect / `set_sys_ui_aspect_val` | `#str_229944` | Boolean `ui_aspectCorrection=1` |
| Refresh / `set_sys_refresh_val` | `#str_229945` | Integer `r_displayRefresh=0`, declared range 0..1000; Auto(0) plus supported rates for the selected display/resolution |
| Window width / `set_sys_window_width_val` | `#str_229946` | Integer `r_windowWidth=1280`; renderer validates 320..16384 |
| Window height / `set_sys_window_height_val` | `#str_229947` | Integer `r_windowHeight=720`; renderer validates 240..16384 |
| Custom fullscreen width / `set_sys_custom_width_val` | `#str_229948` | Integer `r_customWidth=1920`; renderer validates 320..16384 |
| Custom fullscreen height / `set_sys_custom_height_val` | `#str_229949` | Integer `r_customHeight=1080`; renderer validates 240..16384 |

Source contracts:
[SYSTEM declarations](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/content/baseoq4/pak0/guis/menu/settings/system.gui),
[renderer CVar types and defaults](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/src/renderer/RenderSystem_init.cpp#L242),
[window defaults](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/src/renderer/RendererModule.cpp#L38),
[SDL display fields](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/src/sys/sdl3/sdl3_backend.cpp#L198),
[dimension validation](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/src/renderer/RenderSystem_init.cpp#L1737).

## Display choices and native event semantics

The host enumerates current SDL displays, sanitizes semicolons in device names,
and publishes `display_names`, `display_values` and `display_count`. Resolution
choices contain Desktop Native, sorted unique supported resolutions, and Custom.
Selecting Desktop Native writes `r_mode=-2`. A supported resolution writes both
dimensions and its legacy mode ID if one exists, otherwise `-1`. Custom writes
`r_mode=-1`. Refresh choices contain Auto and distinct rounded rates matching the
selected display and requested resolution. A new service must validate the
coupled selection against a current capability snapshot and detect topology
changes; a previously displayed ordinal is not a durable display identifier.
See [host enumeration and mode application](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/src/framework/Session_menu.cpp#L260).

`fromMp_toSystem` targets desktop page 22. On entry, the menu issues
`refreshSystemSettings`, which refreshes display data, inverse quality booleans
and renderer capabilities, then calls `StateChanged`. `forceAspect0` selects the
effective resolution widget; the other two widgets are compatibility duplicates.
`showSetSystem`, `hideSetSystem`, `layoutSetSystemFull` and
`layoutSetSystemCompact` control presentation and eligibility. Multiple-display
rows are hidden on a single display. Section labels `#str_229961` are
Video/Window/Rendering/Quality/Post FX/Sizing; section destinations are scrollbar
values 0/5/9/14/21/28. The host blocks scroll input beneath a visible settings
popup. These legacy coordinates and dictionary names describe translation
inputs, not a requirement to reproduce an inaccessible fixed-size layout.
See [entry and eligibility](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/content/baseoq4/pak0/guis/mainmenu.gui#L16574)
and [scroll routing](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/src/framework/Session_menu.cpp#L3409).

Native choice, slider and edit controls default to `liveUpdate=true`. A choice
writes its selected CVar before `onActionRelease`. Sliders clamp and snap to
their declared range/step; numeric editors rely on CVar and renderer validation.
The generic native events `cvar read <group>` and `cvar write <group>` match
case-sensitively and force read/write even when live updates are disabled.
Choices name the group `updateGroup`; sliders and editors use `cvarGroup`.
None of the effective settings includes declares either group or disables live
updates. Accordingly, Session's `systemCvars` read-render/read-sound events and
`video restart` write-render event have no matching groups in these controls.
See [choice behavior](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/src/ui/ChoiceWindow.cpp#L115),
[slider behavior](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/src/ui/SliderWindow.cpp#L494),
[editor behavior](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/src/ui/EditWindow.cpp#L693).

## Existing actions and required transaction flow

| Existing behavior | Production transaction requirement |
| --- | --- |
| Most choices and numeric edits write live CVars immediately | Begin one owned edit session from a full host snapshot; validate draft edits without publishing unrelated changes |
| Preset choice and Auto-Detect apply a multi-domain profile immediately | Expand a selected/detected preset into a complete typed draft; preview its changes before Apply; preserve non-preset custom values |
| Back/Escape keeps edits; `vidwarn` opens an acknowledgement popup | Dirty Back/close must offer explicit Apply/Discard/continue-editing behavior; Cancel restores permitted live previews and discards deferred edits |
| Warning dismissal clears `vidwarn`; its `vid_restart` is commented out, and the restart button is block-commented | Apply must classify live, renderer, audio and next-map effects, obtain actual completion/failure results, and keep status accurate across recreation |
| No display Keep/Revert timer or durable failure recovery | Unsafe display changes require confirmation, timeout/focus-loss handling and verified rollback, backed by a recovery journal that survives process failure |
| Defaults Yes resets CVars globally, executes default bindings, detects machine settings and restarts audio | SYSTEM Defaults must have an explicit scope and confirmation; stage the documented page/profile defaults without silently resetting unrelated bindings, language or game options |
| Hidden old Advanced/Auto/Ultra launch controls and duplicate popup content remain | Preserve reference semantics where reachable; consolidate into complete production controls, rather than exposing obsolete duplicate pages |

The old Defaults Yes handler queues `exec cvar_restart`, `resetdefaults` and
`exec s_restart`; the exit animation queues `resetdefaults` again. `set cmd`
appends commands, so these are not replacement assignments. Session's reset path
preserves language and runs `default.cfg`; that file begins with `unbindall`.
No/Escape exits without resetting. Back from SYSTEM does not restore host
settings. These are compatibility observations, not the desired new defaults
and cancellation design.
See [Defaults handler](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/content/baseoq4/pak0/guis/mainmenu.gui#L21984),
[delayed reset](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/content/baseoq4/pak0/guis/mainmenu.gui#L14922),
[Session reset](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/src/framework/Session_menu.cpp#L3182),
[warning acknowledgement](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/content/baseoq4/pak0/guis/mainmenu.gui#L23586).

The old Advanced popup adds `r_skipNewAmbient` and `r_useSmp` to duplicated
shadow/specular/bump/sky/ambient/V-Sync/MSAA controls. Its launch button and the
old Auto launch button are explicitly hidden by current SYSTEM activation.
Reachability of the retained old Advanced/Auto/Ultra paths has not been accepted
as production coverage. The new visible Auto-Detect is a separate action.

## Full preset transaction footprint

The baseline preset implementation declares exactly 32 touched CVars. The
transaction catalog now covers their union with the 30 SYSTEM backing CVars:
**53 real fields**. It does not replace `r_mode` and its dimensions with the
temporary `display_mode_choice` index, or store GUI aliases as host settings.

| Domain | Exact preset-touched keys |
| --- | --- |
| Selection and timing | `com_performancePreset`, `com_machineSpec`, `r_rendererBenchmarkPreset`, `com_maxfps` |
| Image quality | `r_screenFraction`, `r_multiSamples`, `r_postAA`, `image_anisotropy` |
| Texture policy | `image_usePrecompressedTextures`, `image_downSize`, `image_downSizeLimit`, `image_downSizeSpecular`, `image_downSizeBump`, `image_downSizeSpecularLimit`, `image_downSizeBumpLimit`, `image_ignoreHighQuality`, `image_writeGeneratedImages` |
| Lighting and effects | `r_useShadowMap`, `r_shadowMapSize`, `r_shadowMapMaxUpdatesPerView`, `r_bloom`, `r_ssao`, `r_hdrToneMap`, `r_motionBlur`, `r_crt`, `r_useLightGrid` |
| Upload resources | `r_rendererUploadMegs`, `r_rendererUploadFrameBuffers` |
| Audio | `s_maxSoundsPerShader`, `s_numberOfSpeakers`, `s_useEAXReverb`, `s_maxEmitterChannels` |

Existing preset application validates that every target exists, backs up values
and flags, verifies accepted readbacks, and rolls the whole operation back if
normalization fails. It does not perform the renderer/audio restart it reports
as necessary. Reuse that complete ownership boundary, with a pure profile
expansion path for draft editing and explicit result delivery for actual effects.
See [touched set and backup/apply implementation](https://github.com/themuffinator/openQ4/blob/15e35beaf11a1f1942ba25e9431211f64c1ef7f1/src/framework/Common.cpp#L2694).

Baseline preservation is broader than values shown in a choice list. For
example, `image_usePrecompressedTextures` is an integer 0..2; value 2 means
BC7-only replacements, although presets select 1. `r_renderer` accepts additional
compatibility paths beyond the page's best/arb2 choices. V-Sync and resolution
scale likewise have a wider underlying configuration space. Preserve captured
custom values and CVar flags during Cancel/rollback; validate requested writes
against the catalog and capabilities instead of normalizing every untouched
baseline field to a current menu choice.

Useful hidden-field bounds include anisotropy 1..16; texture limits 0..32768;
frame cap 0..1000 with 0 uncapped; shadow-map size 128..4096 and update budget
0..1024 with 0 unlimited; upload size 1..128 MiB and rotation count 3..8.
These constraints do not grant permission to expose every profile implementation
field as an editable product control. The catalog must retain actual CVar types:
the texture downsize toggles are Boolean, while precompressed-texture policy,
speaker count and budget fields are integers.

## Implemented transaction and service boundary

The implementation increment follows engine baseline
`15e35beaf11a1f1942ba25e9431211f64c1ef7f1`; the companion remains
`1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. It adds the engine-independent
[SettingsTransaction](https://github.com/themuffinator/openQ4/blob/idtech5-ui/src/ui/application/SettingsTransaction.cpp),
fixed [SystemSettingsHost](https://github.com/themuffinator/openQ4/blob/idtech5-ui/src/ui/application/SystemSettingsHost.cpp)
and private [SettingsService](https://github.com/themuffinator/openQ4/blob/idtech5-ui/src/ui/SettingsService.cpp).
These links identify the branch implementation; the local checkpoint evidence
binds exact source hashes. The historical retail/native audit above is unchanged.

`SettingsTransaction` holds a complete typed baseline and independent draft for
one owner. It validates a merged edit before replacing the draft and sends only
changed keys to the host on Apply. It rejects an externally changed baseline,
checks exact readback, and handles refused, partial or throwing writes. Rollback
restores owned keys only while their live values still match the transaction's
write or already equal the original value. Divergent external changes remain
untouched and require explicit recovery. A failed Apply retains attempted edits
while rebasing untouched keys to fresh live values. Snapshot size, value types,
time monotonicity and reentrant calls are bounded and checked.

The fixed host reads the complete 53-field catalog, exposes registered CVar
defaults without resetting the engine, and validates actual types, editor
choices, writable flags and coupled display requests. It preserves unchanged
custom configuration values; rollback can restore captured values outside a
short menu choice list when registered limits and current device constraints
still permit them. Writes never create unknown CVars, change CVar protection
flags or evaluate command text. They use exact decimal serialization/readback.
The catalog's display checks query current SDL topology/modes; a complete
capability model and stable production display-choice service remain open.

The normal retained adapter validates and dispatches these operations:

| Operation | Arguments and current result |
| --- | --- |
| `settings.system.begin` | No arguments; capture live baseline and draft, or keep an existing draft for the same owner |
| `settings.system.edit` | Nonempty map of exact catalog CVar names to typed values; validate the complete merged candidate without live writes |
| `settings.system.defaults` | No arguments; stage all 53 registered defaults after complete validation; no global reset, binding reset or immediate write |
| `settings.system.apply` | No arguments; apply an eligible immediate batch and verify readback; reject the whole batch if any changed field requires a deferred effect |
| `settings.system.cancel` | No arguments; close an editing draft; during confirmation/recovery, first restore to Editing on success |
| `settings.system.confirm` | No arguments; commit a matching pending readback when the transaction is Confirming |
| `settings.system.revert` | No arguments; discard an editing draft, or attempt safe rollback of pending/recovery writes |

Confirming, timeout and rollback are implemented transaction states. The real
service cannot yet enter a qualified display-confirmation flow: display restart,
image reload, audio restart, renderer resources, next-map changes and preset
expansion are all classified as non-immediate and conservatively blocked before
Apply writes. That includes changing `com_performancePreset`; a pure complete
profile expansion and Auto-Detect draft operation are still required. A batch
mixing brightness with one blocked field applies neither change. Registered
Defaults can likewise remain unappliable when the resulting draft changes a
protected or non-immediate field. This is a disclosed implementation boundary,
not the completed production Defaults/Apply flow.

The service owns the reserved `settings.*` state namespace. The adapter accepts
only exact schema names/types without CVar sources, rejects authored event
writes to this namespace, ignores caller dictionary attempts to supply these
values, and republishes current service data. Documents may declare a subset of
the 112 available fields:

- Boolean `settings.open`, `settings.dirty`, `settings.busy`, `settings.canApply`.
- Number `settings.phase`: Closed 0, Editing 1, Confirming 2, RecoveryRequired 3.
- String `settings.message`: one of the localized keys below, not raw diagnostics.
- Typed `settings.draft.<CVar>` and `settings.baseline.<CVar>` for each catalog
  field, available only to the transaction owner. Other owners receive status
  only; absent values in an adapter document use its declared initial values.

Owners are unique process tokens rather than GUI pointers. Closing an editing
view discards its draft without host I/O. Closing or releasing a pending view
queues abandonment for `UI_SettingsFrame`; a GUI destructor does not write CVars
or restart devices. A failed recovery blocks persistence and is not retried every
frame. An explicit Begin may queue one further attempt, returning Busy first.
Only successful recovery opens the waiting owner with a fresh baseline; closing
or releasing that waiter cancels its pending open. Same-source recreation keeps
the live service owner. A GUI snapshot cannot rewind the engine-owned draft;
restoring an inactive GUI closes its edit session instead of replaying actions.

`Common::Frame` runs settings recovery/timeout before configuration persistence.
Both automatic `WriteConfiguration` and explicit `WriteConfigToFile` return
before dirty-flag clearing or file opening while Confirming or RecoveryRequired.
Editing may persist the committed host values, since draft edits have not been
written. This protects in-process persistence; it is not a durable recovery
journal or proof of successful renderer/audio reconstruction.

### Required device-result boundary

Before removing the non-immediate Apply gate, add a renderer-private typed
request/result carrying a request token, renderer generation and actual
display/window dimensions, refresh rate, MSAA and V-Sync. Schedule work from the
Common frame. Introduce nonfatal full-restart attempts while preserving the
legacy startup wrapper's behavior; an initialization fatal path or silent GL
fallback cannot stand in for a successful requested mode. Query the resulting
SDL and renderer state, verify resource reconstruction, and wait for the first
successful present before starting user confirmation.

The transaction also needs explicit Applying and Restoring stages for those
asynchronous results; its current host interface is synchronous CVar mutation
and readback. A request token must prevent a stale result from committing a new
owner's draft. A durable journal and verified rollback of the actual device are
required alongside timeout/Keep/Revert. Renderer initialization false-success
checks are necessary preparation, but do not by themselves satisfy this route.

### Validation and current limits

The native settings transaction suite passes ownership, atomic edits, budget,
time, partial-write, conflict/rollback, reentrancy and merged-recovery cases. The
six selected native suites pass with zero failures. The production host
extraction test checks all 53 catalog keys against the source page/preset union,
registered default privacy, exact values, protected writes, custom rollback and
coupled SDL display classification, using counted CVar/SDL stand-ins.

The production service extraction test passes 15 fresh-process scenarios,
including typed operation validation, owner privacy, defaults and immediate
Apply/Cancel, device-batch rejection before host I/O, partial failure, pending
confirmation, lifecycle abandonment, divergent/refused recovery, waiting-owner
Close/Release, config blocking and frame timeout. It compiles the actual service,
transaction and Common persistence bodies. Its four-field host and forced
confirmation state do not qualify the complete catalog or any real device work.
The separate host test supplies catalog coverage. Dedicated service stubs and
config/frame source-order checks also pass.

Reproduction commands are `python tools/tests/ui_settings_service.py` and
`python tools/tests/ui_system_settings_host.py`; the native target is
`openq4-ui-settings-transaction`. The six language tables pass
`python tools/tests/lang_table_encoding.py`, and the added block preserves all
prior table bytes. Local validation is under `.tmp/ui/settings-review/`, with
the language audit under `.tmp/ui/system-settings-document-validation.json`.
The source/binary/log/render-target gameplay record is
`.tmp/ui/settings-review/capture-evidence.json`.

| Hidden windowed Windows gameplay probe | Exact ordered readbacks | Service results | Language/video recoveries | Errors / warnings |
| --- | ---: | ---: | ---: | --- |
| SP/OpenGL, 1280x720, density 125% | 168 passed | 19 matched | 2 passed | 0 / 3 expected negative cases |
| MP/Vulkan, 1280x720, density 200% | 168 passed | 19 matched | 2 passed | 0 / 94 recorded baseline warnings plus the same 3 expected negative cases |

Both probes use the engine screenshot command, with no host input injection.
The explicit density overrides are 1.25/2.0; user UI scale remains 1.0. These
captures do not qualify an independent UI-scale or text-scale control.
They exercise pending caller input, owned drafts, exact immediate Apply and
Defaults staging, service-state protection, Cancel/reopen, rejected brightness,
blocked window-size work and external-conflict recovery. The three expected
diagnostics are the out-of-range brightness edit, blocked device batch and stale
baseline Apply. The resource resumes read state without replaying setup/actions;
the service owner and draft survive both recoveries. A GUI save/restore cannot
rewind the live service draft. Final readback shows draft brightness 1.50 against
the live/committed 1.25, with shadows disabled. Render-target images were reviewed
as legible and unclipped, with the authored 45-degree vector framing intact.

These are bounded semantic probes using authored diagnostic buttons and explicit
service events. They do not exercise every catalog field or production control,
actual display/audio reconfiguration, physical-device input, the complete six-
language rendering matrix or the native editor. No complete SYSTEM page, stock
resource, widget library, editor workflow or supported-platform gate is accepted.

## Localized service status

The following IDs are reserved in all six current `*_openq4.lang` tables:
English, French, Italian, Spanish, Polish and Russian. They are ordinary
player-facing status messages. Detailed failures and internal identifiers remain
in diagnostics, not in the menu's status text.

| ID | English text |
| --- | --- |
| `#str_229982` | Ready |
| `#str_229983` | Another settings window is editing these options. |
| `#str_229984` | Open settings before editing. |
| `#str_229985` | These changes could not be applied. |
| `#str_229986` | Settings changed elsewhere. Review them and try again. |
| `#str_229987` | The previous settings could not be restored. |
| `#str_229988` | Changes are waiting for confirmation. |
| `#str_229989` | Unsaved changes. |

## Open dependencies and acceptance evidence

The [private display-device service](display-device-contract.md) now supplies
strict window requests/readback, recoverable restart/restore and actual backend
presentation results. It is a dependency for the following work, not a reason
to lift the production service's device gate. Its video reference must span a
failed attempt and restoration so captured SDL display identities remain valid.
Display preflight now uses the read-only window query when resolving Auto; it
does not refresh native handles or persist visible geometry. Exclusive mode
validation matches the strict device service's rounded pixel dimensions, rather
than also accepting logical-point dimensions on high-density displays. Counted
host regressions cover both rules. The transaction-wide persistence guard and
canonical candidate-to-device request builder remain integration work.

- Complete production mappings from every real control to the typed service,
  including inverse/facade controls, full profile/Auto-Detect draft expansion,
  dependencies and meaningful localized conflict/recovery presentation. The
  fixed catalog and ownership service above are a foundation for these flows.
- Complete coupled display enumeration/validation, topology changes, backend
  capability gating and actual renderer/audio/reload completion routing. A
  queued restart or accepted CVar write is not a successful applied mode.
- Implement and qualify the recovery journal, Keep/Revert, timeout, failure,
  owner close and process-restart recovery. Confirmed application must update
  the saved baseline; resource recreation must not replay Apply or Defaults.
- Implement all production controls, numeric validation, dropdown/list behavior,
  keyboard/controller/touch navigation, focus, scrolling, modal isolation, IME
  where applicable, and accessibility. Use editable vector framing and localized
  labels from the canonical document, with fidelity to the visual specification.
- Establish explicit effect and persistence policy. `r_displayRefresh` and
  `r_skipSky` lack `CVAR_ARCHIVE` in the baseline. Presets cross into AUDIO, and
  light-grid preload is documented as applying on next map load.
- Resolve existing inconsistencies: SYSTEM refresh does not initialize the
  `r_forceAmbientOn` facade; the disabled ambient slider leaves its numeric editor
  active; unsupported Post AA is greyed but remains interactive; capability data
  currently comes from GL configuration rather than a complete backend contract.
- Add the product-required renderer API and UI scaling controls deliberately;
  neither is represented by the current renderer-fallback or UI-aspect choice.
  Keep their scope and dependencies distinct from this source inventory.
- Complete canonical editor support for the actual page, reusable control
  components and behavior lowering. A generated diagnostic layout is not the
  complete production screen or an editable product workflow.
- Qualify real SP and MP gameplay entry, every page section/control, all six
  languages, DPI/density/text scaling, GL/Vulkan and supported platforms, resource
  recreation, custom configurations, Apply/Cancel/Defaults, capability failures
  and recovery. Use engine render-target screenshots for visual evidence and
  actual host readbacks/result traces for behavior. Preserve full corpus and
  final-gate acceptance as pending until the required evidence exists.
