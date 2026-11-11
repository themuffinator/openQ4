# Stage-one baseline evidence

> **Development status:** This document describes the current source tree and its evidence, not the contents of the latest available 0.14.1 package. The 0.14.0 release was withdrawn. See [release status](../user/release-status.md).

18 October 2026; starting source `8c1a49ca7a033e163e271ef3787a4a9ba73f7479`.
Completed implementation and final tested source:
`7674acef91c2653e8c6153ae11e7b158c05e52ff`, rebased onto `80060ccd8`.
This is the evidence and ownership record for stage 1 of the
[roadmap](roadmap.md), not another development plan. Stage 1 establishes an
honest baseline, including reproduced failures; it does not require the later
product, renderer, device or release gates to pass. The runtime report index
below identifies the tested binaries and limits each observation.

Stage 1 is complete: GOV-01/GOV-02/GOV-03, HF-01/HF-02, Q4-01 and UI-10,
plus the scope/recovery review portions of VR-01/VR-06 and AND-01. The latter
three full implementation/qualification items remain open. Current failures,
unaccepted UI migrations and hardware gates are not waived by baseline closure.

## Current defaults and accountable workstreams

The source audit covers the current in-tree engine, SP/MP modules, renderer
selection, package metadata and UI inventory. Existing domain evidence retains
its recorded package/platform scope. A first-class platform target does not
mean every renderer, optional feature or current package is qualified.

| Configuration or default | Current source and disposition | Evidence owner / remaining gate |
| --- | --- | --- |
| Windows x64, automatic desktop renderer | Fresh `best` prefers compatible high-performance Vulkan hardware with OpenGL fallback; existing GL choices persist. See [selection evidence](ui/performance-presets.md#rendering-backend-and-device-selection-28-october-2026). | Desktop/platform workstream, [platform matrix](platform-support.md), REL-01 |
| Windows x64 Vulkan | Supported; the 28 October selection policy prefers it for fresh desktop configurations with OpenGL fallback. October NVIDIA/Intel evidence retains its original scope. | Vulkan workstream, [capability matrix](engine-capability-matrix.md), VK-01/VK-02 |
| Linux x64 / Steam Deck, GL | First-class target; existing native Wayland/X11 evidence is historical, not rerun by this Windows audit | Linux/platform workstream, platform matrix and PLAT-01 |
| Linux Vulkan and macOS Vulkan | Experimental; software/CI evidence does not qualify physical GPUs | Vulkan/platform workstream, VK-01 and PLAT-01/PLAT-04 |
| macOS arm64 | Preview; unsigned packages and OS-floor signoff remain explicit | Apple/platform workstream, [macOS signoff](macos-signoff-evidence.md), PLAT-04 |
| Linux arm64 | Preview; current physical release signoff pending | ARM/platform workstream, [ARM64 evidence](linux-arm64-signoff-evidence.md), PLAT-02 |
| Windows arm64 | Experimental; no current client hardware qualification | ARM/platform workstream, platform matrix, PLAT-03 |
| Android arm64/API 24+/GLES3 | Experimental; source/package support exists, device qualification pending | Android workstream, [build contract](android-build.md), AND-01/AND-02/AND-03 |
| PC OpenXR GL, VR off by default | Implemented opt-in; Vulkan/EGL/Quest recovery is not active implementation | XR workstream, [recovery review](platform-recovery-review.md), VR-01/VR-02/VR-06 |
| MP prediction / lag compensation | `src/mpgame/gamesys/SysCvar.cpp`: both `0`; enhanced modes remain optional | Multiplayer workstream, MP-01/MP-07; corrected [player guide](../user/multiplayer-networking.md) |
| Pure assets, dual-stack network, rcon2 | Existing bounded implementations; proprietary game DLL and retail protocol 2.85 interoperability intentionally unsupported | Network/security workstream, capability matrix, MP-08 / REL-01 |
| Retained interface | `ui_retained 1` in `Session_menu.cpp`; stock fallback remains `0`. Withdrawn 0.14.000 notes describe that historical build's earlier default | UI workstream, [register](ui/product-requirements.md), UI-01 through UI-11 |
| TAA / GPU skinning / dynamic resolution | `r_temporalAA 2`; GPU skinning and dynamic resolution remain opt-in. VR/crop/nearest exclusions remain | Rendering workstream, [temporal contract](temporal-presentation.md), REN-04/REN-05 |
| Authored PBR and shared classic domains | Native authored PBR default-on does not promote shared classic domains; `r_rendererShared*` domains remain `0`; aggregate modern-lighting proven domains remains zero | Rendering workstream, capability matrix, REN-01/REN-02 |
| Room reverb / audio distance | `s_useEAXReverb 1`; `DOOM_TO_METERS` and `METERS_TO_DOOM` both `1.0f`. The old metre-conversion diagnosis is withdrawn | Audio workstream, [audio contract](audio-modernization.md), AUD-01 through AUD-03 |
| Exact scheduling / game time | 60 Hz scheduler and 16 ms ordinary game/snapshot time remain separate | Game/presentation workstream, [clock contract](presentation-clock.md), HF-01/HF-02 |
| Retail SP, MP and additive Awakening SP | In-tree canonical modules; no expansion multiplayer module | Compatibility/game workstream, [retail ledger](retail-baseline-ledger.md), Q4-02 through Q4-07 |
| Save readers, BSE, jobs, caches, native tools | Existing implementation is preserved. Full progression, effect, memory and tool qualification remain separate | Game/core/tools workstreams; capability matrix's linked evidence, Q4-03/Q4-05/Q4-07/CORE-01 through CORE-06; paused TOOL scope |

No default or support tier was changed to match stale prose. Historical resolved
decl/BSE fixes remain closed. Missing physical/device evidence is classified as
qualification pending; optional rendering/VR/Android work is not an implicit
prerequisite for unrelated supported configurations. Each capability row in the
capability matrix retains its more specific source/evidence owner.

## Executable coverage and clock ownership

The [coverage map](game-test-coverage.json) accounts for all 83 tests/probes from
the imported game history. The unified suite runs 74 relevant Python contracts
and compiled fixtures against this repository; obsolete companion-layout checks
map to current checks. Meson registers the native probes and production clock
test, and the push-validation profile runs Meson tests. See
[coverage and provenance](game-test-coverage.md) for the SDK-EULA boundary.

The clock test executes the shared production policy, including submillisecond
reads, 16/17 ms scheduling, one million ticks, non-divisor caps, jitter, catch-up,
stalls, pause, slow steps, resets, signed timestamp wrap and XR host deltas.
The pause fixture executes extracted production SP/MP methods and proves
scene/material reads stay frozen during a draw. Clock inputs are values, not
authoritative state references; deterministic event/state fixtures remain equal
across presentation caps. This proves clock-policy invariants, not a complete
game command replay or physical 240/360 Hz pacing qualification (HF-03/HF-06/HF-07).

## Workload and timing policy

Use existing `stock_asset_baseline.py`, renderer map harnesses and their evidence
schemas. Keep the full launch/config, `logFileName` log, engine screenshot,
source revision/dirty state, staged executable/module hashes, retail PK4 manifest,
overlay collision list, backend/GPU/driver and camera together. Raw reports and
failures stay in their original directories. Do not turn a failed report into a
pass by omitting its failed role or broadening thresholds.

The retail baseline uses its existing `stock-baseline-targets-v1` contract and
256 CPU/GPU samples per profiled map. Those results are absolute checks of that
contract, not before/after performance claims. The lighting comparisons use
ordered mapped/stencil/shadows-off engine captures; animation is not synchronized
between separate processes, so full-image differences do not isolate shading.

For any subsequent timing comparison, declare this protocol before collection:
30 seconds of loaded gameplay warmup, 2,048 measured frames per run, three
interleaved A/B then B/A pairs at identical camera/cvars/package/driver/power
state, engine CPU plus asynchronous GPU timing, no screenshot/log flush in the
sample window, and no simultaneous build/test workload. Retain all per-frame
samples and p50/p95/p99 for each run. A result is inconclusive if between-repeat
p95 spread exceeds 5%; otherwise require a change larger than both 5% and the
observed repeat spread before claiming an improvement. Frame-budget gates remain
their declared absolute budgets. Record timer/logging overhead with a paired
instrumentation-off run; absent that measurement, label overhead unmeasured.
This protocol is policy for comparisons, not a claim that this stage performed
such a benchmark or set final HF-07 hardware budgets.

Visible bordered-window GPU captures, hidden/offscreen capture, software rendering
and physical display/input testing are separate evidence classes. This stage
uses engine render-target screenshots only and sends no device input. None of
its image or synthetic-clock evidence certifies physical motion, input latency,
headset comfort, Android devices, macOS or a final release package.

## Completed validation and tested package

The final MSVC `debugoptimized` build has native tests enabled and was staged
with the project wrapper's `install -C builddir --no-rebuild --skip-subprojects`.
Meson passed **184/184**, including the recovered 74-check game suite and the
production clock/ownership tests. Linux g++ under WSL passed seven native
portability/checksum/string/unsigned-char probes plus the production clock test;
this is Linux compiler/runtime evidence, not Linux graphics qualification.
The governed UI inventory revision passed; seven inventory test cases and nine
negative CLI controls rejected stale sources, false acceptance, changed counts,
rewritten history and weakened policy, including misuse of all three old modes.

The full `push --skip-build` profile ran 348 Python check invocations: 345 passed
and three exposed missing documentation links, a governance wording contract and
incomplete validation discovery. Its failed log is retained. After correcting
those issues, all three checks passed individually, along with release-tooling
safety, production clock ownership, seven UI inventory cases and fourteen UI
archive cases. The archive test is now registered in both CI workflows and local
validation. Generated offline documentation includes its linked inventory and
coverage records. The final recovered game suite was rerun through Meson and
passed 74/74 after replacing its obsolete aggregate runner. These follow-up
changes affect documentation, tests and CI only; the engine, game sources,
runtime content and Meson definitions are unchanged from tested `7674acef9`.
This is a full-profile run followed by targeted correction checks, not a claim
that a second full-profile invocation or remote CI has completed.

Final runtime matrices recorded clean source `7674acef9` and the same staged
binary set throughout. The four-map GL/Vulkan matrix passed 8/8, lit Recomp
passed 2/2, and live SP/MP clock controls passed 2/2. All 96 launch/config/log/
capture artifact hashes in those reports were checked. Original-content SP/GL
and MP/Vulkan controls also passed, with matching executable/module hashes,
configuration/log/image bindings and visually reviewed engine captures. The
40-PK4 identity and 165 retail overlay collisions are recorded in each ordinary
matrix identity; the original-content controls mount none of those overlays.

The live clock runs requested a 144 FPS cap without vsync and recorded 256 samples
each. SP phases ranged 0.039384..0.924252; MP 0.009270..0.964356. Host samples were
monotonic and fractional, with game labels advancing only in 16 ms multiples and
the scheduler remaining 60 Hz. This confirms production sampling and bounds; it
does not measure physical pacing or input latency. Builds and other checks were
allowed alongside visual/clock capture, so these runs are excluded from timing
comparison claims under the protocol above.

| Final runtime file | SHA-256 |
| --- | --- |
| `openQ4-client_x64.exe` | `2461657aa0cf5b1b39a09e56efc882b7f5d7ce80735dfeca3eb45939254351b1` |
| `openQ4-ded_x64.exe` | `0ada4e1f608e4801edd525e70197060fa63f8a9367bbc854d2fcf39f6bcf0870` |
| `baseoq4/game-sp_x64.dll` | `6454c6b5ef63c42803ade3f8d73cbc0a859be1e31b792183acb72c9587ba0b64` |
| `baseoq4/game-mp_x64.dll` | `80d4508151dfbf18bf41b5c12d9a889a7a9dee2449c0e26b2dff9a7f1577e7c7` |
| `renderer-gl_x64.dll` | `f90f30234e31d52c25d510c4d7603b523cfe9ef3c4329233f753c8cd0fd1c692` |
| `renderer-vk_x64.dll` | `a37e458821b8e135600722af45333113ebcc355d7f68f49c613af74c6a179590` |
| `OpenAL32.dll` | `c5b7aa0c5237c4d202f798fe7825f59af5bb4b9a1ac1e654e7011908843fbb83` |

The earlier complete stock-baseline failures retain their original pre-commit
`8c1a49ca7`/dirty source and binary identities. They are reproduced workload
failures, not tests of the final binary set above. Their SP save/restore, demo
and listen-server passes do not erase the connecting-client failure. The later
controls qualify their own explicitly narrower workload only.

## Retained local evidence

All paths below are repository-relative and deliberately outside the release
package. The [retail ledger](retail-baseline-ledger.md) records observed results;
the UI [inventory report](ui/inventory-report.md) records source reconciliation.

| Evidence | Location |
| --- | --- |
| Configuration, build, stage and native-suite logs | `.tmp/stage-one/` |
| First complete recovered game suite | `.tmp/stage-one/game-suite/results.json` |
| Meson-discovered recovered suite | `builddir/game-regression-logs/results.json` |
| UI raw inventories and governed revision audit | `.tmp/stage-one/ui-*-inventory*.json`, `.tmp/stage-one/ui-register-audit.json` |
| First retail baseline and shorter-path retry, including failures | `.tmp/stage-one/stock-baseline-run/`, `.tmp/s1/` |
| Initial and final four-map GL/Vulkan controls | `.tmp/s1m/`, `.tmp/s1mf/` |
| Initial and final Recomp lit decal controls | `.tmp/s1r/`, `.tmp/s1rf/` |
| Final live SP/MP clock samples | `.tmp/s1cf/` |
| Original-content SP/GL and MP/Vulkan controls | `.tmp/s1oa/`, `.tmp/s1om/`; final `.tmp/s1oaf/`, `.tmp/s1omf/` |
| Final native logs and artifact verification | `.tmp/stage-one/meson-testlog-committed.txt`, `.tmp/stage-one/evidence-verification.json` |
| Full static profile, final correction checks and recovered suite | `.tmp/stage-one/push-static.log`, `.tmp/stage-one/*-final.log`, `.tmp/stage-one/game-suite-final-results.json` |
| UI inventory transition and negative controls | `.tmp/stage-one/ui-register-audit-verified.json`, `.tmp/stage-one/ui-negative/results.json` |
| Independent original-PK4 asset view and bound expected manifest | `.tmp/stage-one/paks/`, `.tmp/stage-one/stock-baseline-paks/stock_pk4_manifest.json` |

The asset view contains independent copies of the installed 40 PK4s, with no
loose retail files. It avoids importing unrelated user mods/generated caches
from the installed game. No proprietary source or screenshots are added to Git.
The fixed comparison runtime remains under `.tmp/runtime/baseline/`. Disposable
editing helpers, probe binaries and failed-setup asset junctions were removed
with non-recursive file/link operations; the installed game was not modified.
Logs, engine captures, replay assets and the two evidence/reproduction helpers
are intentionally retained.

## Report bindings

These hashes bind the retained reports above. Their existing schemas own the
detailed settings, results and artifact identities; this table is only an index.

| Repository-relative report | SHA-256 |
| --- | --- |
| `.tmp/s1mf/identity.json` | `95d5134282121bef6d0a1e3289d3b7fee15e027560a4332adb590935920def6c` |
| `.tmp/s1mf/results.json` | `c1e0cb6c7621f0d134f859a94ccf2bffe8600157a3b23341080fdc873286f40a` |
| `.tmp/s1rf/identity.json` | `95d5134282121bef6d0a1e3289d3b7fee15e027560a4332adb590935920def6c` |
| `.tmp/s1rf/results.json` | `14942731de1b5822c9ed622479865b9951f7aca17e62dc56151a48b83c747167` |
| `.tmp/s1cf/identity.json` | `95d5134282121bef6d0a1e3289d3b7fee15e027560a4332adb590935920def6c` |
| `.tmp/s1cf/results.json` | `fb614e09fac4cd55fcff04b8ade3c8f8ade25d0e44b0f187f12f7bbb23988b39` |
| `.tmp/s1oaf/capture.json` | `75dc3383adfb82b611da87808425bd2147c36ad0cba9142089500019b3d1dacc` |
| `.tmp/s1omf/capture.json` | `de062823b4fb347a5f8361a320d1d969fe335fc241aa3e1a7ffa90c3faafddcb` |
| `.tmp/stage-one/meson-testlog-committed.json` | `8c228aaf17c1c57dc85d8f03a4202a57860ec80e6341a8970b80e8891db30b13` |
| `.tmp/stage-one/evidence-verification.json` | `41585aa468702bab5b9c76eed6e3a5e24fd8e0b90f1c9ce93cc075bc09ab0b0e` |
| `.tmp/stage-one/linux-probes.log` | `d436755e5437e30a1a509161d66a409320249c2730776d13fc60eeeacd14cc0c` |
| `.tmp/stage-one/ui-register-audit-closure.json` | `c6bc6ffcaf763c8cb3ee46cc7a81ba292889dec0b78769d0ddafa2164327fe6c` |
| `.tmp/stage-one/ui-negative/results.json` | `5db26f5aaa614d06e17ca5b994afe8cc7a7280f9ea9ec6e015e75f58168f3378` |
| `.tmp/stage-one/stock-baseline-run/stock_asset_baseline_report.json` | `b6134078b64a9e12c9ed78ab9dbf5513fe841c4036bc5b034d6089afa2aeb6ef` |
| `.tmp/s1/stock_asset_baseline_report.json` | `9af6a4bde5123b3cc4340881f4689bb4fcd48ecef347f8c2a7890205a4b5ac5e` |
| `.tmp/stage-one/push-static.log` | `3e78c9b91781437716ddf733f3faa5aa8a29a389746643dd1865f3f3f0b5125b` |
| `.tmp/stage-one/docs_link_integrity-final.log` | `42767402e2834cfef812fa5a36c7a1d9a8745796ed59bffadd70e586841c8129` |
| `.tmp/stage-one/p0_governance_evidence-final.log` | `37fe53f7ee0ec0d4c60aa845f6bdcb5e21fd279b13435ce177411d4766a96365` |
| `.tmp/stage-one/validation_hardening-final.log` | `1611a145c59875351dec78c0463a3bf8f10714b8b33d707df404867e7c51a794` |
| `.tmp/stage-one/release_tooling_safety-final.log` | `82a900ab7b3390c2f2b5ea5ef87b80d49ec45702af2e10b829a6ac7585184a5b` |
| `.tmp/stage-one/ui_requirement_archive-final.log` | `b6315130c684cedee5f15c29885f1dd7e649513ecb532bbfbef609549e2d2ffa` |
| `.tmp/stage-one/presentation_clock_ownership-final.log` | `756c1cdb142a4a8f4acbaa993f147c6a4e0c4a0042bc0c99562e6c40c316eac8` |
| `.tmp/stage-one/ui_inventory_revision-final.log` | `e5c3e3d5ce2dc32441a76d433a40cc1349b2f23ef2099b89177b61a27bf33dce` |
| `.tmp/stage-one/game-suite-final-results.json` | `10aad9ec887a773fcfa7b0f316b91d2a614887232375ca516d3a1cf4783ec83e` |
