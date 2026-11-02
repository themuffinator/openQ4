# Current retail compatibility ledger

Stage-one Q4-01 investigation, 18 October 2026. Starting source
`8c1a49ca7a033e163e271ef3787a4a9ba73f7479`; current evidence is indexed in
[baseline evidence](baseline-evidence.md). This ledger classifies observed gaps;
it does not reopen resolved decl/BSE reconstruction or certify full campaigns.
Final controls use clean implementation `7674acef91c2653e8c6153ae11e7b158c05e52ff`;
earlier failed reports retain their own package/source identities.

## Current reported symptoms

The [roadmap report register](roadmap.md#report-and-contribution-traceability)
retains the complete issue/PR ownership map. The following retail gameplay reports
were checked against their current issue text and discussion on 18 October:

| Report | Baseline disposition and remaining evidence |
| --- | --- |
| [Weapon effects, sound and reload, #170](https://github.com/themuffinator/openQ4/issues/170) | Reported on 0.13.2. The merged BSE sound-diversity fix stays closed; individual particle/reload symptoms remain qualification pending under Q4-03/AUD-01/HF-04. Current weapon, effect, build/backend and timestamped reproduction are not yet supplied; the four lighting maps do not qualify these symptoms. |
| [Machinegun transparency, #183](https://github.com/themuffinator/openQ4/issues/183) | Report names Windows 10/RTX 3060 but no exact build, backend, map/save or asset set. Qualification pending under VK-04/REN-08; generic current weapon captures cannot reproduce or clear the reporter's firing/view conditions. The existing request for those details remains open. |
| [Tech Li at the Hangar Perimeter lift, #184](https://github.com/themuffinator/openQ4/issues/184) | Report identifies hangar1/Windows 10 and a progression stall. The #189 medic watchdog addresses an unreachable-patient case, but the exact reporter save and normal heal/progression regression remain Q4-02 qualification; map startup does not close it. |

These entries classify available evidence and preserve outstanding reports. They
do not treat absent reproduction data as a resolution or invent a root cause.

## Lighting findings rechecked

The existing map runner entered Network2, Recomp, Storage1 and Q4DM9 on both
GL and Vulkan, then wrote mapped, stencil and shadows-disabled engine captures.
All eight processes completed with the requested API, 960x540 bordered window
and capture sequence. A further two runs exercised Recomp's steady diagnostic
point light and the material/shadow planner self-tests. These are visual
investigations, not performance comparisons. Images were decoded and inspected;
successful capture is not a claim of pixel parity.

Host: Windows x64, NVIDIA GeForce RTX 4060 Laptop GPU; GL reports NVIDIA 591.74,
OpenGL 4.6.0; Vulkan reports API 1.4.325. Each launch/config and `gfxInfo` log owns
its precise settings/device record. SP freezes after settling, but separate
processes do not share identical particle/light/actor time. MP remains live.

| Report | Current observation and classification | Owner / remaining evidence |
| --- | --- | --- |
| Network2 GL/Vulkan lighting difference | **Reproduced differences** around the opening's upper/left illumination across control modes. Initial shadows-off captures also differ; the final mapped/stencil contrast remains conspicuous. Effects occur at different phases, so this is not an isolated shadow-map root cause | Q4-03 / VK-04: isolate light/material/effect and synchronize time before assigning root cause |
| Recomp dark opening and decal wall silhouette | Opening remains dark on both APIs. The lit fixture shows receiving walls and floor on both and the historical solid decal-wall failure is **not reproduced**. Residual lit/shadows-off differences remain | Keep the explicit `noShadows`/`DECAL_MACRO` repair closed; Q4-03/VK-04 own remaining non-shadow parity |
| Storage1 corpse/fire difference | Both APIs draw corridor, corpse, fire and smoke; actor/effect phases differ. No missing-corpse verdict is supported by these controls: **qualification pending** for synchronized effect comparison | Q4-03. This corridor fixture uses the explicitly recorded pod-intro bypass; it does not qualify the original opening or campaign progression |
| Q4DM9 darker Vulkan ambient/wall | Fixed camera `(1528,-384,452.25)`, yaw 180; matched structure and visible wall/pipe lighting, with no obvious former broad dark-wall failure. **Not reproduced in this view**; moving pickups/effects remain uncontrolled | Q4-02/VK-04; retain the old failing package, qualify additional views separately |

Historical [September shadow evidence](shadowmapping-final-audit-2026-09-05.md)
is unchanged. Authored `noShadows`, ambient lights and unsupported caster types
are reported separately from API/resource failures. Stock warnings remain in
the logs; no asset edits were made to silence them.

## Runtime baseline and failures

The bound original-media view contains 40 unmodified installed PK4s and zero
loose files. Its expected-manifest SHA-256 is
`5250429197dd4083cf464f4cb83cd02fdbbd28be52d35c0c6688e6ca898bd67e`.
The ordinary staged package adds pak0/pak1 and supersedes **165 retail virtual
paths**. It is therefore a retail-PK4 compatibility baseline with declared
overlays, not an original-asset-only baseline.

Two complete harness runs (`.tmp/stage-one/stock-baseline-run/`, `.tmp/s1/`)
passed SP Storage1 save/restore, render-demo playback and the Q4DM1 listen-server
role. Each profiled successful role supplied 256 CPU/GPU samples and passed its
unchanged existing per-map budget. Both **failed the connecting MP client**:
startup timed out before active gameplay/display/capture completion. The shorter
save path did not resolve it. This is a reproduced failure of this workload,
not a qualified pure-client result or a proven path-length diagnosis. The
stage-two follow-up below resolves this startup failure; the full failed reports
and logs remain alongside reruns. Broader MP-08/Q4-02 qualification remains open.

An initial overlay-free probe incorrectly selected `fs_game baseoq4` without
its required package metadata/paks. Its pre-log timeout was traced with a
non-invasive debugger stack to `idFileSystemLocal::Startup` â†’ `FatalError` â†’
`Sys_Error`'s Windows message loop. This is an invalid package setup, not evidence
of broken retail rendering. The supported original-content control must select
`fs_game q4base`, stock UI and the same openQ4 binaries, with an isolated writable
q4base. `capture_legacy_baseline.py --stock-assets-only` now makes that distinction
explicit. The failed setup reports remain under `.tmp/s1os*` and `.tmp/s1ov`.

The corrected controls reached Air Defense 1 SP/GL and Q4DM1 MP/Vulkan gameplay
using original content only (initial `.tmp/s1oa/`, `.tmp/s1om/`; final clean-build
`.tmp/s1oaf/`, `.tmp/s1omf/`). Both final engine images were
visually inspected: world geometry, weapon and stock HUD are present; MP is an
active joined player. Logs confirm loading the matching in-tree game modules
from the fixed comparison runtime's baseoq4 binary directory, with no content
packs mounted from it. These hidden-window captures are compatibility controls,
not pacing or full progression evidence. The final controls report 21 SP and
622 MP warnings and zero ERROR records; the initial MP run reported 616 warnings.
Repeated runtime warnings vary with the sampled gameplay interval. Missing openQ4 Arena/demo
menus, SMAA/postprocess/soft-particle resources and custom declarations are
expected omissions in this control. Undefined stock `r_aspectRatio` binding,
missing MP AAS/sound and non-precached stock decl warnings remain explicit
UI-03/Q4-02/MP-02/MP-03 investigation inputs; capture success does not clear them.

The installed Steam directory also contained unrelated loose/generated content
and an extra baseoq4 overlay. The baseline preflight rejected that directory;
the audit uses independent PK4 copies instead. No installed assets or user
configuration were changed. Missing-media, authored-content warnings, package
configuration errors and renderer defects must not be merged into one verdict.

## Cross-platform pure peers, 18 October 2026

The production PK4 writer previously inherited Python's host-dependent ZIP
`create_system` field. Windows and Linux builds contained identical member bytes
but different archive bytes, so the engine's embedded package identity rejected
a package copied from the other host. `openq4_pak.py` now writes the existing
Windows/DOS value explicitly. No authored content, pure-check policy or protocol
version changes. Actual Windows and Linux builds now produce identical pak0/pak1
SHA-256 values, respectively
`329725e33f909bf351231784c36773b0c9bbbffc8922223482112e8dca30501a` and
`1a7c763fbba69207725774d45139ce5a8583300e4f89a55ecf12760bfb6570a9`.
The packaging regression executes the real writer with both host metadata
defaults, checks payloads and byte equality, and rejects the old behavior.
Windows and Linux runs pass.

`tools/tests/mp_wsl_peer_smoke.py` ran a staged Linux x64 dedicated server and two
successive native Windows x64 clients against the same 40 retail PK4s. IPv4 and
scoped link-local IPv6 both passed pure negotiation, active player/view checks,
disconnect/reconnect and clean shutdown of all three processes. Engine captures
were decoded and inspected: Q4DM1 world, weapon and HUD are visible. Each client
played alone in warmup; these runs do not qualify combat, concurrent player load,
WAN impairment, MTU edge cases, another architecture or physical Linux display.

| Evidence | SHA-256 / identity |
| --- | --- |
| IPv4 `.tmp/s2peer-ipv4-r4/report.json`, 100.91 s | `ac41ca476e1cecddda1348f7ffd3c9ff0120ba642fbf7f8a3ceebd20fd83d80c` |
| IPv6 `.tmp/s2peer-ipv6-r2/report.json`, 98.17 s | `b04b64de1117bc8855f447d2ff8dfde2c7f215262012bd435e37ee28a715433e` |
| Windows engine / MP module | `c30c191835a11e82ac12d7c34dc5fb393af6c131e95913b0e20066e9f297bf81` / `20ce933689ee4b7fa2fe08a1327a1013a34a23241596cb04775a59dcc47cd065` |
| Linux engine / MP module | `51ac07e50a87b6dd1885fb988f51447ead90d985e232575cd95d0c640bd2a3f9` / `10955ae59b803281f555ed8a66f28994800a0f0a4c64b5110f838fdcbdb541da` |
| Linux stage identity `.tmp/stage-two/linux-final-runtime-identity.json` | `8e07962db5adbe590174a1a5e56305e26912f4703b1873f6308cf35b40769d32` |

Runtime source is the `6de4e622e` base plus the Arena/session/sound corrections
bound in [Arena evidence](arena-campaign-ceremony.md#current-pause-and-result-evidence-18-october-2026)
and the deterministic ZIP metadata change. OpenXR lifecycle work edited during
these runs was not built into these binaries. Both client runs used hidden,
windowed GL at 640x480, disabled physical input, and engine screenshots only.
Reports retain exact commands, addresses/scopes, runtime and asset hashes.
No firewall, installed game content or shared player profile was changed.

The initial `.tmp/s2peer-ipv4/` package-identity rejection remains recorded.
Earlier `.tmp/s2peer-ipv4-r2/`, `.tmp/s2peer-ipv4-r3/` and `.tmp/s2peer-ipv6/`
attempts timed out during WSL startup before any peer connection; reports and
verified process-cleanup records preserve those failures. The successful runs
first verified that runtime, assets and writable profiles on the Windows mount
resolve mixed-case names to the same files, then used `fs_caseSensitiveOS 0`
for that filesystem. This avoids expensive case-recovery directory searches;
it is not evidence for case-sensitive Linux filesystem behavior. These are
functional checks, not controlled performance measurements. MP-08 remains open.

## Stage-two connection regression

The failure reproduced again in `.tmp/s2connect/` on the staged stage-one
binaries. Non-invasive debugger snapshots showed an idle, disconnected client
with an empty command queue. Temporary command-dispatch diagnostics then showed
empty command names at queue indices 16, 32 and 48; `connect` was index 48.
`idList::Append` correctly copies its value before growing storage, but
`idCmdArgs` had only a deep assignment operator and an implicit shallow copy
constructor. Copying through the list's temporary therefore produced token
pointers outside the receiving object. This also affected insertion and reload
replay; changing network timeouts or bypassing the SP-to-MP handoff would not
repair it.

`idCmdArgs` now uses the same owned-token copy for construction and assignment,
and assignment explicitly handles self-assignment. The native fixture compiles
the production methods and actual list container. It checks 100 queued commands,
every growth boundary, aliased growth/insertion, source destruction, independent
copies and reload replay. Removing the copy constructor in a disposable header
copy makes that fixture fail its pointer-ownership check. All 185 Meson tests
pass in MSVC `debugoptimized`; the new fixture also passes with Linux g++ in WSL.
The game-module selection, pure-pack and network-security contracts pass.

After rebuilding and staging the matching engine/modules/renderers, the full
stock baseline in `.tmp/s2fixed/` passes all four roles: SP save/restore,
render-demo playback, listen server and connecting pure client. The client
switches to `game_mp`, completes pure restart/admission, joins as a player and
writes the engine screenshot bracketed by active-player/gameplay-view checks.
That capture was visually checked. The report verifier passes all artifact and
retail/runtime package bindings. This is Windows x64 loopback/OpenGL evidence;
it does not close cross-platform peers, physical input or full campaign play.
The timing samples meet the existing absolute baseline gates; no comparative
performance improvement is claimed. Temporary dispatch instrumentation was
removed before the final build.

The tested worktree is based on `cbe967f2b`, with source and reports bound below.
The existing report schema records the complete dirty-source and package identity.

| Evidence | SHA-256 |
| --- | --- |
| `src/idlib/CmdArgs.h` | `5b2d49b661f60945ccfbcc5e71284a94189592edfc14380859268ff5582bb3e1` |
| `src/idlib/CmdArgs.cpp` | `7be0cd90967b33a096ac3e79d56781733bb3383b2fddfe10dbb8f3c2c378fdbf` |
| `.tmp/s2connect/stock_asset_baseline_report.json` (failed) | `165259e3843f13b7305948bbcfc4b2682c8223295e95cda7b03cab6ed5df5e86` |
| `.tmp/s2fixed/stock_asset_baseline_report.json` (passed) | `4f8d444ab033ee1a1580a9b950624655827c0183c5e5592ba6b57076a4cc7444` |
| `.tmp/stage-two/cmdargs-native-testlog.json` | `19c16f8c49dcc5dceef66b19fb41ec33e4a93f3afb758ef97892545f330f6b94` |
| `.tmp/stage-two/cmdargs-negative.log` | `3704ea1626d27dd4d69ee27873c15498e0908b92287f08b2db9c716782ad6dd4` |
| `.tmp/stage-two/cmdargs-linux.log` | `56f3ae643db14849a59652dacdfb8acb46f378b678d39e4cede3234372ca3486` |

## Campaign transition chain, 2 November 2026

Q4-02 transition, save and restore evidence for the whole stock campaign, not
playable-progression evidence. One hidden client (Windows x64, NVIDIA RTX 4060
Laptop GPU, build `0.13.2-nightly+gf5a64d5b.dirty` with the fixes below, retail
Steam assets, no input) walks the retail chain from `game/airdefense1` to
`game/core2`. On each map it saves, loads that save, then fires the map's own
`target_endlevel` (names taken from the retail `.map` files) so the session
performs the real level change with persistent player state and entity filter.

- All 31 stages pass on OpenGL (622 seconds, radio-chatter fix) and on Vulkan
  (565 seconds, also with the client-entity owner fix), including the first and
  second visits to `storage1` and `process1`. Each run makes 62 map loads in the
  expected order (each map by level change, then by restoring its save) and
  ends with `core2`'s `endGame`, with no errors.
- The first run stopped at `game/hangar1`: its save, made after a radio message
  on `game/airdefense2`, was refused with `idEntityPtr::Restore: entity 1090 has
  type 'idSound', expected 'idFuncRadioChatter'`. The static last-radio-chatter
  reference carried the previous map's spawn id; see
  [savegame reliability](savegame-reliability.md#typed-spawn-references-during-restore).
  It is fixed, and that save now loads into gameplay.
- 393 warnings of 96 kinds remain on OpenGL (395 of 97 on Vulkan), classified
  by kind. Expected retail or
  authored-content conditions: `mcc_1.aas48` and `tram1b.aas32` are refused as
  invalid data, as retail's loader also refuses them; `process2.reverb` lists
  areas beyond the map's 96; entities and rigid bodies start in solid,
  articulated-figure joints are outside their bodies, unhidden AI is placed in
  maps, and `idTrigger_Timer` has `random >= wait`. Engine diagnostics: decls
  loaded without precache (crosshairs, `_tracemodel`, sequence sounds).

The end triggers skip everything between them: scripted sequences, AI, bosses,
vehicles and terminals were not played, and saves made mid-sequence and other
platforms were not covered.

## File-by-file overlay disposition

[retail-overlay-audit.csv](retail-overlay-audit.csv) lists every one of the 165
colliding files, its repository source SHA-256 (LF-normalized text, raw binary), original archive locations,
classification and workstream owner. It is the source counterpart of the runtime
harness's `retailPathCollisions`, not a new acceptance schema.

| Collisions | Disposition |
| --- | --- |
| 128 `gfx/` files | Intentional loading/reticle imagery; UI-04/UI-05 own replacement/exception evidence. They are not fixes for the four maps' world lighting |
| 24 `guis/` files | Intentional menus/HUD behavior and layout; UI-03/UI-04 own migration, with save readers under Q4-05/Q4-07 |
| 8 `strings/` files | Intentional localization and openQ4 text; no missing retail media is inferred from these overrides |
| 2 `glprogs/glsl/SniperStretch2` files | Intentional scope presentation, owned by UI-04/REN-08 |
| `default.cfg` | Intentional bindings/defaults, including wheel/quick-chat controls |
| `scripts/maps/storage1.script` | Compared against pak010: adds only the opt-in `g_openQ4Storage1NoDropPodState` branch/helper; default still calls retail `inFlightSetup`. Retain the named diagnostic fixture until Q4-02 replaces it with a validated engine/game facility |
| `scripts/maps/convoy2b.script` | Compared against pak010: adds only opt-in no-actor/unlocked-hovertank guards. Default retains retail driver/vehicle actions; Q4-02 owns eventual fixture retirement |

Virtual-path collisions alone miss declaration-level overrides. The additional
source review records these separately:

| Source under content/baseoq4/pak0 | Purpose and disposition |
| --- | --- |
| `def/weapons/00_napalmgun_openq4.def` | Deliberate residue balance change (smaller hazard, longer lifetime), including stock-named impact declaration; retain as an intentional gameplay difference, never describe a run using it as stock gameplay |
| `def/vehicles/awakening_turret_openq4.def`, `materials/00_awakening_turret_openq4.mtr`, three `effects/openq4_awakening_turret_*` files | Additive Awakening turret implementation; Q4-04 owns campaign-scoped behavior |
| `def/openq4_m01_cinematic.def`, `scripts/openq4/awakening/m01_dropship.script` | Original Awakening scene helpers referencing installed media; Q4-04 |
| `def/liquids_openq4.def`, `materials/liquids_openq4.mtr`, `materials/types/liquids_openq4.mtt`, two `effects/openq4_liquids_surface_bubbles_*` files | Intentional liquid feature resources; retain separately from retail compatibility claims |
| `def/maps_openq4.def`, `def/bot_support.def` | New laboratory map and bot declarations; intentional new content |
| `materials/hardwareshaders_openq4.mtr`, `materials/hud_openq4.mtr`, `materials/weaponwheel_openq4.mtr` | Scope/HUD/wheel presentation resources; UI-04/REN-08, not world-lighting workarounds |
| `materials/postprocess_openq4.mtr` and remaining `glprogs/` | Existing renderer feature resources. Original-content controls omit them; shared/generated engine resources remain the preferred direction for future changes |

Retained Q4UI documents, bot profiles, the new map, light grids and new language
tables are intentional features, not candidates for automatic deletion merely
because retail lacks them. No blanket source/bitmap acceptance is granted.
This audit found no safe compatibility-only override to remove without changing
an intentional feature or a named diagnostic fixture. Their retirement or
replacement requires the owning roadmap item's gameplay evidence.
