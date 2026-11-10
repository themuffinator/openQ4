# XR and Android recovery review

18 October 2026, current source baseline `8c1a49ca7a033e163e271ef3787a4a9ba73f7479`.
This records the stage-one scope/recovery portion of roadmap VR-01/VR-06 and
AND-01, followed by current stage-two evidence. Archived commits below
exist in this repository; none is replayed wholesale or used as current hardware
evidence. The current in-tree engine/game/renderer contracts take precedence.

## XR selection and dependency review

| Archived change | Reviewed implementation | Selected boundary for later integration |
| --- | --- | --- |
| `0d83c14c4` Vulkan | `XR_KHR_vulkan_enable2`, runtime-created instance/device services, queue identity, eye target format/layout and renderer image release | Recover device ownership and stereo output together against the current module ABI. Historical render API 24 is not a version to restore blindly. Require GL/Vulkan SP, MP, menus, restart and VR-off parity |
| `f98a1d247` desktop-independent submission | `vk_GuiExecutor.cpp` adds a windowless frame, bypasses desktop acquire/present and preserves XR output targets | Headset submission must continue during minimized/out-of-date/absent desktop swapchain; no accidental desktop image/semaphore use |
| `4b5e0c4f9` EGL/Wayland | `gl_VRPresentation.cpp` queries SDL EGL display/context/config and proc lookup; extends binding kinds | Qualify native Wayland EGL independently from GLX fallback; do not introduce a shared-code platform dependency where SDL owns the binding |
| `65aa9bd88` Quest | Android activity/manifest, loader initialization, GLES stereo and optional test runtime; generated headset manifest and payload flags | Optional standalone flavor only after lifecycle safety and variant-specific APK verification. Debug XR runtime must be impossible in published APKs |
| `9dcd121da` hand GUI pointing | `idPlayer::UpdateFocus` starts the focus trace at `GetVRAimOrigin`; debug coordinates aid alignment | Reconcile SP and MP aim/focus with current retained/world GUI ownership; never double-apply physical motion or silently alter authoritative weapon traces |

On 18 October the active Vulkan VR presentation was still a stub and active PC OpenXR was GL; the 6 November integration below supersedes that.
Archived happy-path captures do not close current VR-02 or VR-06.

The stage-one `OpenXRSystem.cpp` had the source-derived cycle `Check(SESSION_LOST)` →
`DestroySession()` → `ReleaseImages()` → `Check(xrReleaseSwapchainImage)` before
`frameOpen` and `chain.acquired` are cleared. This confirms the re-entry risk in
source; the reproduction and repair are recorded below. Instance/runtime loss also entered
destruction. VR-06 required taking ownership out of circulation before fallible cleanup,
make destruction idempotent, and stop the caller after session invalidation.
Its fault matrix must cover session/instance loss, each partial acquisition,
wait/release failure, invalid image count/index, create/resize failure and lost
tracking. Require bounded calls, balanced ownership and no stale handles, plus
desktop recovery. Existing call-order tests do not satisfy those conditions.

HF-02 separately replaces millisecond smooth-turn deltas with monotonic host
ticks. XR predicted display time remains the runtime's pose/submission domain;
the [clock contract](presentation-clock.md) does not authorize converting one
clock to the other or claiming lifecycle repair.

## OpenGL XR lifecycle closure, 18 October 2026

VR-06 is complete for the active desktop OpenGL implementation in
`284af3f9`. `Check` records
session/instance retirement without destroying the caller's temporary resources;
frame boundaries consume that request once. A failed frame call stops subsequent
pose, action, image and submission work. Teardown invalidates published frame and
input state, releases owned held keys and pointer state once, and abandons pending
frames/images through session destruction. It never retries a failed release or
releases an image whose wait timed out. This follows the
[OpenXR image ownership contract](https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html)
and [session destruction rules](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrDestroySession.html).

Image counts are bounded before allocation, filled counts must match capacity,
and unsigned image indices are checked before access. Failed screen replacement
preserves its old swapchain unless the session itself was lost. Session failures
return to desktop rendering and retry after three seconds; instance/runtime loss
stays on desktop until `vr_restart`. Screenshot draws whose renderer declines
stereo now draw the world in both SP and MP instead of capturing only the HUD.

`tools/tests/openxr_lifecycle.py` compiles the actual production ownership methods
against a hostile handle/image implementation. All 19 cases pass on Windows and
Linux, including 1,000 acquisition cycles, each of three partial acquisition
positions under four failure results, wait/release failures, malformed counts and
indices, failed create/resize, idempotent teardown and held input retirement.
Original recursive teardown, timeout-as-success and retained pointer/movement
failures remain in `.tmp/stage-two/xr-{teardown,timeout,input-teardown}-repro.log`.
The old engine also fails the actual-runtime timeout control in
`.tmp/s2xr-old-timeout/report.json` (SHA-256
`18a6fa188d3cae8a6138a9da74cefa7d4ee21363ba0c1ed50f49640dd6497a30`).
That negative control intentionally used the pre-XR-repair engine, identified by
its report's executable hash; its source hashes describe the newer fixture tree.

The final staged package passes 28 hidden-window gameplay runs using the
process-local test runtime: SP Storage 1 with its documented pod diagnostic and
MP Q4DM1 listen-server warmup. The matrix covers acquire/wait/release, frame
wait/begin/end, view/space/action calls, partial session creation, malformed image
enumeration, loss-pending and invalid tracking. It checks bounded recovery,
balanced session/instance ownership, no stale handles or calls after loss, desktop
fallback and manual restart. All controller actions are inactive; physical input
is disabled. Engine `screenshot` captures include the desktop scene, both eye
render targets and continued stereo after a screenshot. Final SP timeout and MP
acquisition-loss captures were visually inspected. This is simulated-runtime
evidence, without physical input, headset or comfort qualification.

The exact 28 reports and source/binary hashes are bound by
`.tmp/stage-two/xr-final-evidence.json` (SHA-256
`da1e581e88550a8ed0c517cafd750af65fd5facf8ba873f83f2f1bd6c11cfc83`);
the 27-case batch is `.tmp/stage-two/xr-final-matrix.json` and the separate wait
timeout is `.tmp/s2xr-final-wait/`. Tested engine SHA-256:
`f66cfcae8c48801d37c78eb30526a8e87a9a9100befa36e37c0894b3d5d58afb`;
SP module `a70276783ab66977b1c398a0b5c92a3a4869914a1b4142478331f606e5415225`;
MP module `999a8dedb3b17bcd449c17f78ea984a4c6c377826e25d639543bc6d4741e7895`;
test runtime `1aff033fd54019a246b21a143a4c5f8f311c038b3af42c0e9b7183ba4e24c1b3`.
The full Windows native suite passes 207/207
(`.tmp/stage-two/xr-final-native-suite.log`, SHA-256
`f5fed293932114ec59d2d057e84c12a88cc7aedb6782f44d8af7e028cc44449e`), the XR
source contract passes, and the complete XR source compiles with the Linux client
flags in a GCC 13 syntax check. Earlier fixture build errors and an input-driver
source-drift rejection are retained; the stable-source full suite passes.

VR-01 must carry these guarantees into any later Vulkan/EGL/Quest integration;
none is promoted by this GL result. VR-02 still owns real headset/controller,
focus, comfort and pacing evidence. The Linux syntax/native checks do not establish
Linux GL runtime or headset support. No frame-time or audible-device claim is made.

## Vulkan, EGL and world-GUI XR integration, 6 November 2026

VR-01 integrates the selected archived changes against the current renderer
module ABI (render API 32) rather than replaying them; VR-03 and VR-05 land on
top. Commits: `f6034967f` (Vulkan, desktop-independent submission, EGL and
world-GUI pointing), `244cb2457` (settings tabs take the pointer), `b8995be28`
(play spaces) and the VR-05/VR-04 commit that follows them.

- **Vulkan device ownership.** With `XR_KHR_vulkan_enable2` the runtime creates
  the `VkInstance` and `VkDevice` through three render-module services, on the
  physical device the runtime names; the renderer restarts once on the first
  Vulkan VR start to adopt them. Eye swapchain images are the executor's
  default output for each VR target, with display colour mapping per target.
- **Submission without the desktop.** A VR frame opens on a stand-in when the
  desktop swapchain is minimized, out of date or absent; the headset keeps
  receiving frames and only the mirror waits.
- **EGL.** On Wayland, OpenGL binds through `XR_MNDX_egl_enable` with SDL's EGL
  display, config and context; X11 keeps GLX.
- **World GUIs.** Single-player focus traces start at the VR aim origin, so the
  weapon hand points at door panels and lift screens.
- **VR-06 carried forward.** Both graphics APIs bound image counts before
  allocation, check filled counts and indices, release renderer images before a
  swapchain is destroyed and defer retirement through `Check`.
- **Settings tabs.** Each settings page's full-size group sat above its tab
  strip, so RmlUi's topmost-box hit test gave every tab click to the page; the
  generator now orders the strip after the pages and `settings_menu_coverage.py`
  guards it.
- **Play spaces (VR-03).** `vr_playSpace` picks STAGE, then LOCAL_FLOOR, then
  LOCAL with a warning; standing height is measured from the floor and kept
  across recentering; `vr_calibrateHeight` measures again.
- **Per-eye TAA (VR-05).** See the roadmap entry. Two faults found on the way:
  the backend read one temporal frame state per frame, which the front end last
  wrote for the virtual screen, so every eye's backend check failed; each
  `RC_VR_TARGET` now carries its target's state. And Vulkan found a portal
  sky's root through the frame's one primary view (the right eye), so with the
  backend owning eye scenes the left eye lost its sky; the portal-sky pairing
  from the command list now names each eye's main view.

Evidence, all against the OpenXR test runtime with physical input disabled and
no input injection; Vulkan smokes with validation enabled. Build
`0.13.2-dev+gb8995be2` plus the uncommitted VR-05/VR-04 changes, identified by
these SHA-256s: client `aa189f6e314256550af77caa70e8f43358b35d282b95e3507692b14babdcfc9b`,
`renderer-gl` `0f6b4e83dbacddcb3bba6f23640eeabf6b093c2e7f80bf98d367641c7d2448b8`,
`renderer-vk` `170af8194ca3f3cc4ad409205115fe431d6f79de2a3be7513d00b0450c991932`,
SP module `8233d793322e9bd38a3b12561b1c38bf4546ddeee35483962b233788680a9c60`,
MP module `fed11fd5c2c646409815411c0faf6b32a1c0e80ebbcec68d5639490b90f90983`,
test runtime `361d9a40313abe515ab57436a9358668aede2a5734249d722f36d8c719aab630`.

The qualification batch passes 51 of 51 (`.tmp/stage-vr/2026-11-06/final/summary.json`,
SHA-256 `33ffc4d30e3614e092bd1dc1e4bb7416a883ebaea86f49c15a5f22d81bd9605e`):
GL and Vulkan SP, menu and MP smokes; GL and Vulkan play-space smokes; the
per-eye TAA smoke in SP and MP on both APIs (240 of 240 resolves from history,
no resets or fallbacks); both stereo-cost runs; seven GL lifecycle cases; and 30
Vulkan lifecycle cases with validation, covering partial acquisition, wait,
release, frame, view, space and action failures, creation faults, malformed
image counts, windowless frames and MP. One Vulkan case (`acquire-2-end`)
first failed with `FATAL: Out of memory` while a WSL build held the machine's
commit charge (4.8 GB free); rerun alone with 14.6 GB free it passes. Both runs
are kept (`acquire-2-end.oom*`).

Linux, WSL snapshot `af19bc5` of the same tree, release build, software
drivers (lavapipe, llvmpipe): the lifecycle runtime passes on a headless
native-Wayland compositor with Vulkan and with OpenGL bound through EGL
(`opengl_egl`), and on X11 (Xvfb) with Vulkan and with GLX (`opengl_xlib`).
Evidence: `.tmp/stage-vr/2026-11-06/linux-probe.tar` (SHA-256
`d5142ed670f824b7e0e927d6675afe0409321fcf3c085da6e27d133a45c78f62`); client
`e4606c207d06af3fe76846f18da26b2eaa09bb1970c61fad16a331d3f6d85272`.

Static checks: `openxr_vr_contract.py` (now including VR-05),
`openxr_lifecycle.py` (20 production-method cases, Vulkan images included),
`vk_display_color_mapping.py`, `renderer_display_presentation.py`,
`renderer_temporal_presentation.py`, `renderer_render_thread_runtime.py` and
the other suites that extract the touched code pass.

Not established here: any headset, runtime other than the test runtime,
physical tracking, readability, comfort or frame pacing (the
[VR-02 checklist](vr-headset-qualification.md)), and Quest standalone, which
waits on AND-05.

### Stereo frame cost (VR-04)

`openxr_vr_stereo_cost.py`, airdefense1 with game time stopped, 200 frames per
phase, unpaced test runtime, two 1440x1600 eyes against a 1920x1080 window
(milliseconds; "stereo" is the first VR session, "warm" a second one after
`vr_restart`):

| API | Phase | CPU p50 / p95 / p99 | GPU p50 / p95 | Draws | Lights |
| --- | --- | --- | --- | ---: | ---: |
| OpenGL | flat | 6.51 / 8.56 / 10.73 | 4.56 / 6.41 | 1,046 | 30 |
| OpenGL | stereo | 9.48 / 12.04 / 13.37 | 7.76 / 11.93 | 1,442 | 62 |
| OpenGL | warm | 17.66 / 31.60 / 35.68 | 11.93 / 21.82 | 1,442 | 62 |
| Vulkan | flat | 5.65 / 9.53 / 17.06 | 3.05 / 4.90 | 1,040 | 30 |
| Vulkan | stereo | 12.49 / 15.14 / 16.56 | 12.93 / 15.00 | 1,442 | 62 |
| Vulkan | warm | 10.32 / 13.25 / 15.05 | 10.54 / 13.30 | 1,442 | 62 |

Uploads stay flat (588 to 611 KB on OpenGL), so dynamic geometry is already
built once per frame; entities, lights and surfaces double as each eye culls
for itself, which VR-04 requires. Two eyes carry 2.2 times the flat pixels.
The desktop was shared with other openQ4 clients during the run: OpenGL's warm
phase doubled with no change in work, so these tails are a simulated baseline,
not a budget result. Against the headset intervals (11.1 ms at 90 Hz, 13.9 ms
at 72 Hz) the cleaner phases sit above a 90 Hz p95 and inside 72 Hz on this
GPU. Headset tails, from the same script with `--system-runtime`, decide
whether a sharing change is needed (checklist 2.2). Reports:
`.tmp/stage-vr/2026-11-06/final/cost-{gl,vk}/*/report.json`.

### Stereo work sharing (8 November 2026)

Timing on this desktop cannot attribute a sharing change, so this round counts
work instead. `r_showDynamic` now also prints `guisShared`, `eyeSubviews`
(mirror, reflection, refraction and x-ray maps rendered) and `remoteSubviews`
(remote-camera views rendered). `openxr_vr_stereo_share_smoke.py` spawns three
test screens in front of the player on airdefense1 (a world GUI, a remote
camera and a mirror, as fixtures in its own profile), stops game time and
takes the medians per frame:

| Phase | GUI surfaces | Shared | Eye subviews | Remote subviews |
| --- | ---: | ---: | ---: | ---: |
| flat | 2 | 0 | 1 | 1 |
| stereo, `r_vrStereoGuiShare 0` | 4 | 0 | 2 | 1 |
| stereo, sharing on | 4 | 2 | 2 | 1 |

OpenGL and Vulkan count the same. With sharing on, the second eye emits the
quads the first eye's GUI drew. They are in the GUI's own 640x480 space and
each eye places them through its own matrices, so only the redraw is saved.
Both eyes captured with sharing on match those with it off exactly. A GUI
that renders a nested view while drawing (a renderDef window) is not shared,
nor are GUIs inside subviews or nested GUIs. Eye-dependent subviews key their
once-per-frame reuse by frame and eye, because their camera follows the
viewer: before, the second eye drew the first eye's mirror image. Remote
cameras keep one render per frame.

Shadow maps at the Quality preset (`openxr_vr_stereo_cost.py --preset quality
--extra r_shadowMapReport=1`): in steady state every view, flat or eye,
re-renders no static map (OpenGL `updates=0`; Vulkan two exact cache hits and
`fresh=0`) and composes one dynamic map (`composed=1`). Static maps are
therefore already shared between the eyes, and stereo adds one composition per
frame here. That does not yet justify same-frame composition sharing in both
backends.

| API | Phase | CPU p50 / p95 | GPU p50 / p95 | Draws | Lights |
| --- | --- | --- | --- | ---: | ---: |
| OpenGL | flat | 19.35 / 29.56 | 8.65 / 12.28 | 1,053 | 30 |
| OpenGL | stereo | 30.58 / 47.88 | 11.57 / 15.91 | 1,445 | 62 |
| OpenGL | warm | 28.52 / 45.14 | 10.61 / 14.48 | 1,445 | 62 |
| Vulkan | flat | 5.99 / 8.39 | 6.48 / 6.52 | 1,052 | 30 |
| Vulkan | stereo | 9.51 / 13.62 | 7.48 / 7.50 | 1,426 | 62 |
| Vulkan | warm | 7.32 / 11.33 | 7.51 / 7.53 | 1,426 | 62 |

Vulkan's stereo GPU cost at Quality is 1.16 times flat and inside a 90 Hz
budget on this GPU; its CPU p95 is not. OpenGL's CPU tails are high in every
phase of this run, flat included, so they say nothing about stereo.

Found during this round, OpenGL only and present in the build before it: in
the eye views a world GUI loses to the geometry behind it, while the remote
screen beside it at the same depth draws (flat views draw the GUI whole), and
a mirror render map shows horizontal streaks in flat views too. Vulkan draws
both correctly in each eye. Evidence: `.tmp/stage-vr/2026-11-08/`
(share-smoke reports and captures per API, Quality cost reports). Both are
fixed; see the next section.

### OpenGL portal sky and mirror fixes (9 November 2026)

**World GUIs in front of a portal sky.** A portal sky draws straight into the
window, and the root view then renders into the scene target whenever a
post-process effect needs one (SSAO, on by default, does). The present step
discarded every pixel whose scene depth was still at the far plane so that the
sky underneath showed through. Anything in front of the sky that writes no
depth was discarded with it: world GUIs, and also particles and glass. The eye
views see more sky behind a screen, which is why the GUI vanished there and
only partly in flat views. `r_skipPostProcess 1` restored it, because the
root then draws straight into the window over the sky. A diagnostic run that
switched passes off one at a time isolated that toggle. The authored
post-process surfaces in the view (one heat-haze sprite) were not the cause.

The root view now copies the backdrop into its scene target before drawing,
and the present copies the scene whole. The portal-sky view takes that copy
itself as it finishes. Otherwise the root's render-map subviews, which draw
into the window's lower-left corner after the sky, landed in it: a 256x256
leftover of the mirror's render showed in the eyes. A root without such a copy
takes one from the window as it starts. A linear (PBR) scene keeps the old
discard, since it would read the display-encoded window as linear colour.

**Mirror streaks.** A `mirrorRenderMap` stage samples its capture through
screen texgen, whose coordinates run from -1 to 1 and rely on the image's
declared `TR_REPEAT`. The OpenGL framebuffer copy sets clamp-to-edge whenever
it resizes its texture, and the mirror shared `_scratch` with OpenGL's
post-process passes, which resize it every frame. Clamped, the out-of-range
coordinates stretched the edge texels into horizontal streaks. Captures now
restore the declared wrap after the copy (wrap only: the copy has a single
level). The mirror captures into its own `_mirrorRender`, so it is no longer
reallocated against the post passes every frame. Vulkan already sampled with
the declared wrap.

Verification on the simulated runtime, airdefense1 with the share-smoke
fixtures: each OpenGL eye shows the GUI's blue backdrop over about 25,000
pixels, up from about 1,300 before. That matches the eye with post-processing
off, and Vulkan's capture. The mirror shows the reflected scene in flat views
and in both eyes. The share smoke now requires the GUI in each eye on both
backends.

### VR eye culling restored (9 November 2026)

The share smoke's eye captures showed flat, smeared desert to the left of the
dropship where the flat view at the same pose shows dark, rocky near ground and
the wrecked dropship's side. OpenGL and Vulkan agreed, so the renderers were not
the cause. A same-run A/B at the share pose (`10310.89 -6950.34 6.96 150.7`,
game time stopped, base captures bracketing each change and reading exactly 0
between them) found culling:

| Change | Left eye | Right eye | Flat view |
|---|---|---|---|
| `r_useCulling 0` + `r_useEntityCulling 0`, OpenGL | 36.0% of pixels | 47.8% | 0.00% |
| the same, Vulkan | 33.7% | 46.5% | 0.00% |
| `r_useCulling 0` alone, OpenGL | 35.4% | 46.7% | |
| `r_useEntityCulling 0` alone, OpenGL | 9.0% | 27.2% | |

The cause was fixed once, on 4 October (`ea85bdd7b`): the off-axis branch of
`R_SetupViewFrustum`, which VR eyes and levelshot tiles take, builds its side
planes from near-plane extents, so their normals are `zNear * sqrt( 1 + tan^2 )`
long instead of unit length, and `R_RadiusCullLocalBox` compares those inflated
distances with world radii. That commit stayed on
`archive/worktree-wtq-quest-vr`, which the VR-01 recovery reviewed for its
Vulkan, EGL, Quest and GUI-pointing commits; the culling fix was not among them,
so main and the 0.13.2 and 0.14.0 releases never had it. `3a40f705c` restores the
normalization, its source pin and the VR smoke's culling check (turning
`r_useCulling` off over the open ground at `10094 -6825 130 150.7` must change
under 0.5% of either eye).

With the fix, every row above reads 0.00% on both renderers, and the eyes show
the near ground and the wreck the flat view shows.

With the eyes culling correctly, the dropship hull, the wreck and a dune hid
the stereo-share smoke's remote-camera screen and half its GUI (about 13,500
backdrop pixels per eye instead of 25,000). The smoke now looks 40 degrees
further left (yaw 190.7) with its screens raised clear of the ground: all
three are in view and the GUI covers about 23,900 pixels per eye.

### VR seat turn restored (9 November 2026)

The same archived branch held a second fix main never got (`383eebfc5`,
4 October). `idPlayer::UpdateVRVehicleFrame` carries a vehicle's turns into
the seat's facing, but it began counting at the first frame the vehicle was
driven. The Awakening's m07 race seats the player on its speeder bike and
turns it 165 degrees in the same script frame, so that turn was lost and the
head-steered turret ran to its stop chasing the old facing, leaving the
player looking back over the bike. `EnterVehicle` now records the vehicle's
heading at boarding (`00c2addb7`).

The VR smoke seats the player in a walker and turns the walker 45 degrees in
the same script line. Against a build without the fix the seat looked along
the boarding heading, 150.7; with it, along the new heading, 105.7, on both
renderers. The m07 race itself was not re-run: its October harness and
Awakening mirror no longer exist, and the smoke exercises the same path.

## Android selection and package matrix

| Variant | Selected ABI/API/render scope | Distribution and prerequisite |
| --- | --- | --- |
| Phone/tablet production | arm64-v8a, minimum API 24, target/compile SDK 35, GLES3 | Existing signed experimental package; preserve certificate identity, matching SDL/native version, 16 KiB ELF/package alignment, notices and exact verifier allowlist |
| Emulator diagnostic | x86_64, API 24 floor plus current emulator image, GLES3 | Debug-only recovery candidate; separate module suffix/ELF metadata/allowlist, no release publication until separately selected and qualified |
| Quest | arm64-v8a, GLES3/OpenXR, explicit headset manifest | Optional experimental flavor; API floor/device claims need Quest-specific verification. Depends on VR-01/VR-06 and flavor-aware stage/APK checks; no debug runtime in distributable artifacts |

At the stage-one review, Meson/Gradle and `verify_android_apk.py` admitted the
ARM64 package only. The subsequent diagnostic emulator recovery below adds
x86_64 with a separate application identity and disables release packaging for
that ABI. Quest remains a candidate. No NDK, SDL or API bump is bundled into
recovery, and build support alone does not establish runtime qualification.

| Archived change | Recovery decision and current conflict |
| --- | --- |
| `486ecf735` touch gameplay | Recover touch-owner state/controls and executable input contracts; merge with current retained UI ownership and editable layout policy, never enable duplicate SDL/usercmd consumers |
| `3ce244cda` diagnostics/lifecycle | Recover accessible logs and lifecycle reset boundaries with localized strings; qualify pause/resume, rotation, process death and input cancellation against current SDLActivity |
| `5c8693456`, `91fc0bad4` background audio/input and SP resume | Integrated with paired OpenAL pause/resume functions, persistent fallback mute and session-owned resume before audio/gameplay. Native recovery checks pass; physical Android qualification remains open |
| `819846f5d` background tic sleep | Integrated with an atomic lifecycle query, 100 ms background waits and a ten-current-tic Android catch-up budget at every timescale. Production-method clock/wake/shutdown tests pass; physical suspension and resumed network sessions remain unqualified |
| `20766e8d1` cutouts | Surface padding and child-inset coordinates recovered in the current Activity, with density/configuration ownership and Windows/Linux JVM checks. Real orientation/density/input qualification and portrait presentation remain open |
| `ba6d3af2e`, `c5845ca2d`, `2f1d2d681` launcher/preflight | Recover localized missing-data flow; archived `RetailData.isPresent` tests only for a regular `pak001.pk4`, not complete integrity. Preserve usable installed data on interrupted extraction; AND-03 owns full validation and recovery |
| `023f0eeee`, `1484195c4` emulator | Recover ABI metadata, module names and ELF checks together; archived artifact-verifier changes alone miss the current exact APK allowlist. Keep diagnostic payloads out of production |

The stage-one `OpenQ4Activity` trusted `.ready` after extraction. The repair below
addresses that part of AND-03. Signed release verification and dependency
licensing added after the archived branch remain required. Each recovered feature
needs an independently reviewable change and its stated failure/recovery tests
under the existing roadmap IDs.

## Android extracted-package repair, 18 October 2026

`OpenQ4Activity` delegates APK-owned extraction to `PackageStorage`. Gradle now
produces a canonical ASCII manifest of normalized paths, byte lengths and SHA-256
digests; the package revision is the SHA-256 of that manifest. The signed-APK
verifier requires both exact metadata files and still compares every asset and
native library with the validated stage. ABI/API, certificate, alignment, license
and native-library allowlists are unchanged.

Before launching the engine, the host reads the bounded manifest and verifies
every extracted file, even when `.ready` exists. Missing, truncated and mismatched
files are copied into a separate temporary outside the mounted `baseoq4` tree,
checked against the declared size/hash, synced and renamed over the destination.
Healthy files are read for verification but never recopied. Failure invalidates
readiness and leaves the previous package and saves/configuration untouched.
Only after complete verification and readiness publication are old generated
revisions removed. Unexpected content and links below the normalized app-private
root fail closed without deleting that content. Android's
[file-space estimate](https://developer.android.com/reference/java/io/File#getUsableSpace())
is an early preflight; write/sync/rename failures still stop startup because the
estimate does not reserve space. Atomic replacement uses
[Android's existing rename API](https://developer.android.com/reference/android/system/Os).
An already valid readiness record is preserved, so verified healthy content does
not depend on a new metadata write when storage is exhausted.

The original real-Activity corruption control fails with an existing `.ready`
marker (`.tmp/stage-two/android-ready-repro.log`). The expanded
`tools/tests/android_host_storage.py` executes the production Java host/storage
classes with small Android/SDL stand-ins. Windows JDK 21 and Linux JDK 17 runs pass
healthy reuse, missing/truncated/same-size corruption, interrupted partial reads,
failed replacement, poisoned APK source length/hash, malformed/oversized metadata,
low-space preflight, upgrade cleanup and save preservation. Linux also rejects
active-file, nested-directory, temporary-file and package-parent links and
preserves an outside sentinel. Windows explicitly skips Android-style link
canonicalization instead of counting it as a pass.

The optional `--package-source .install/baseoq4` run uses independent copies of
the actual 810 MB staged package, performs two verified reuses without recopying,
corrupts a PK4 byte without changing its length, repairs only that pack and verifies
the result again. The log records exact package sizes and hashes. This is host
storage validation with warm filesystem caches, not Android startup timing.
The actual Gradle manifest closure matches Python's canonical bytes, including a
path ordering boundary and an empty file, and rejects an invalid path. All seven
APK-verifier/build test groups pass. The complete Java host plus the matching
SDL 3.4.16 sources compile against configured Android SDK 35.

Local evidence is `.tmp/stage-two/android-repair-evidence.json` (SHA-256
`b787b2bbb980d07132b34c2a1a08a2a9d28a406faf50b3b20c6b8521a3a1bc9c`), with
per-source and per-log identities. Logs include `android-repair-full-package.log`,
`android-repair-linux-final.log`, `android-package-tests-r1.log`,
`android-manifest-probe.log` and `android-sdk-java-compile.log`. No Android device,
power-loss test, new signed APK installation or certificate migration is claimed.
The existing signed release pipeline checks the new exact manifest after packaging.
Localized onboarding/retry UI, retail-data preflight, accessible release logs and
save export remain AND-01/AND-03 work; the broader items stay open.

## Android window ownership, 19 October 2026

The startup-window portion of archived `81d29509b` is adapted to the current
SDL/native-window lifetime contract. SDL 3.4.16's Android driver permits one
window. The engine splash previously occupied it until after graphics startup,
preventing creation of the game window. Android now keeps the Activity's launch
screen and skips the engine splash. Desktop splash behavior is unchanged.

On the fatal-error path, hiding the game window did not free that slot either.
Android now stops its text input, unbinds the GL context, retires the published
native-window identity and destroys the game window before the error console
creates its replacement. Repeated release is harmless. This changes window
ownership only; fatal-screen touch/back handling and broader lifecycle recovery
remain separate AND-01 work.

`tools/tests/android_window_lifecycle.py` compiles the actual production methods
against an in-memory SDL API enforcing the one-window rule. Before the change,
Android fails because its splash consumes the slot; the desktop control passes.
Afterward, Windows and Linux runs pass the Android startup/game/fatal sequence,
retirement ordering, repeated release and unchanged desktop behavior. These
fixtures neither inject device input nor replace a real Android runtime check.

The NDK r27d ARM64/API 24 build and stage pass with matching SDL 3.4.16 and
OpenAL Soft 1.24.3 dependencies. The Windows build/stage and all 254 native tests
pass. The first suite's existing durable-file test failed a post-rename assertion;
its unchanged isolated rerun passed. Its assertion now requires the intended
injected fault before a read clears the original error, so a native rename
failure cannot masquerade as the post-rename case. No production file-I/O change
was made. Linux passes its 343 checks and explicitly skips DrvFS FIFO support.
The original transient failure's OS error was not retained by the old assertion;
its exact external cause is unproven.

Source, dependency, build, test and staged-library/package identities are bound
by `.tmp/stage-two/android-window-evidence.json` (SHA-256
`794f400eeb26fd98f81e284cf902a771a2ab447eed23cf45121acb0d7c7ee0ff`).
The interrupted builds were resumed after the requested pause; their original
logs are retained. The debug APK also passes exact staged payload/manifest,
seven-library allowlist, API/ABI, native extraction, 16 KiB ELF/ZIP alignment and
v2 signature checks. Its SHA-256 is
`1a2c65f4e50f39d8f68ad8ad6f557e7071a3335aa80d34ef03461c080f050231`;
`.tmp/stage-two/android-apk-verification.json` records the checks (SHA-256
`b2815612ffe96fcdbad60a9d3f9bfb1b5a31d4c2883885bd031fb9de5c3de3ce`).
This local debug signature does not establish release certificate continuity.
The subsequent S25 check below qualifies startup for that device/package only.

## S25 startup, data path and upgrade check, 19 October 2026

The test Galaxy S25 (SM-S931B, Android 16/API 36, ARM64, 4 KiB pages) installs
the debug APK above. All 40 copied retail files match the independent source
fixture's SHA-256 manifest. Storage 1 reaches its first active draw, the engine
`screenshot` command captures the opening scene at 2340 by 1080, and
`Stage2AndroidSmoke` saves successfully. Final engine/app logs show renderer
shutdown and process exit without a reported crash. This is opening-scene
startup evidence, not campaign progression; audio is disabled and no device
input or OS screenshot is used.

The initial missing-data launch exposed a path bug: the engine discarded the
Android host's external data directory and fell through desktop discovery to
`/`. Android now preserves the host's explicit directory even when retail files
are absent. Required-PK4 integrity checks remain unchanged. The production
selection-block fixture fails before the change and passes afterward on Windows
and Linux, including unchanged desktop discovery and unset-path controls.
Android/Windows builds and staging pass; all 256 Windows native tests pass.
The corrected missing-data path still needs a physical-device retry.

The updated debug APK, SHA-256
`de949e3f506d75c12bae162129ea442bb8961b7982e2d300d07486f30caea64e`,
passes exact staged payload, seven-library, manifest, API/ABI, 16 KiB ELF/ZIP
alignment and v2-signature checks. An in-place update with the same debug
certificate preserves the original save, metadata and preview byte-for-byte.
Its restore attempt fails safely because `guis/menu/pause.q4ui` cannot be
created; the game displays its load-failed dialog and is then force-stopped.
The original save and failure logs are retained. Inspection identifies a UI
thread binding captured during library loading instead of engine initialization;
the repair below passes native checks, while successful device restore remains
required. This is not a release
certificate-continuity qualification.

Evidence is `.tmp/stage-two/android-basepath-evidence.json` (SHA-256
`7c8f08eb0179c70cb7ad44841d679a3731457a12d0acafd728961d88af12f0d8`),
including source/build/test identities and the `android-s25-baseline/` and
`android-s25-upgrade/` logs, engine capture, asset manifest and save hashes.
Remaining observed warnings include unsupported mobile window placement/MSAA,
unavailable SMAA resources and existing Storage 1 script/content diagnostics.
Onboarding, retained UI recovery, touch, audio and lifecycle checks remain open
under AND-01/AND-03; this check does not close broad device qualification.

## Android UI engine-thread binding, 19 October 2026

SDLActivity loads native libraries on its Java thread and invokes the engine
later on SDLThread. The retained-view coordinator and global GUI manager captured
their owning thread during library construction. Consequently, actual engine
calls could neither create retained views nor resolve native GUI allocations.
Both owners now bind during UI initialization, preserving rejection of calls
from other threads and allowing serial engine reinitialization.

The S25 restore failure above occurs in optional pause-menu preparation before
the game reads saved state. The original save contains no `pause.q4ui` reference;
`FindGui`'s broad loading-session guard produced the serialized-GUI diagnostic.
The subsequent [serialized-GUI boundary repair](savegame-reliability.md#serialized-gui-admission-and-optional-menu-fallback)
preserves optional fallback while rejecting missing definitions required by the
save. Native and Windows GL/Vulkan checks pass; physical retry remains pending.

`tools/tests/ui_engine_thread.py` runs the production manager/retained
initialization, view creation and allocation query methods with counted command
and Runtime stand-ins. Construction happens on one thread and engine startup on
another. Before the fix, both engine operations fail and the library-loading
thread incorrectly remains authorized. Afterward, Windows and Linux pass that
sequence, foreign-thread rejection and serial reinitialization on the desktop
thread. The retained resource-lifecycle fixture and all 257 Windows native tests
also pass. These checks do not activate a real SDL window or inject input.

Android ARM64/API 24 and Windows builds/stages pass. The updated debug APK,
SHA-256 `905cad2bccf62a55aa1aaff176f4d704b99cd237773fd5582b0f0b7393add668`,
passes exact payload, API/ABI, alignment, native extraction and v2-signature
verification. Its in-place installation preserves all three original save files.
The Android debugging interface disconnected before the physical retry; no
successful device restore or UI visual/input acceptance is claimed yet.

Evidence is `.tmp/stage-two/android-ui-thread-evidence.json` (SHA-256
`4f8d698f484efbb6ebd7e79bf0522a6f3b3fd90412ac717dc22fba74133e5ebe`),
with source identities, original failure analysis, native logs and package/update
records. AND-01/AND-03 and Q4-05 remain open.

## Android background audio and single-player resume, 19 October 2026

SDL 3.4.16's Android pump sends background lifecycle events on the engine thread
and then blocks before returning them to the game. The old deferred handler
therefore released held input and changed the sound world only after the app
returned. OpenAL's mixer could continue running while the game loop was parked.

The Android main-thread watch now performs the existing background transition
before that block. Foreign-thread callbacks still publish pending work for the
engine thread. The sound hardware immediately flushes a silent listener and uses
`ALC_SOFT_pause_device` only when both pause and resume functions exist. It retains
the matching resume pointer for that device. Missing extensions fall back to a
mute that normal updates preserve, and a device retry retains the requested
suspension. A player's explicit mute remains independent.

Foreground events leave audio suspended until the session handles the return.
A live SP map opens its pause menu before sound rendering or game tics; an
existing menu, map load, demo, editor or network session keeps its current flow.
A suspension during loading is consumed at the map handoff. Early/late input
cleanup also tolerates an uninitialized key table while a fatal console pumps
events. Desktop lifecycle dispatch remains deferred.

`android_lifecycle.py` compiles the production watch, transitions, input cleanup
and session resume method with counted API stand-ins. Windows and Linux pass
Android/desktop branches, foreign-thread deferral, repeated cycles, one-time
consumption and the SP/menu/load/network controls. Substituting the old watch
fails before queued events are processed. `openal_device_pause.py` runs the
production pause, deferred-flush and update methods against counted OpenAL,
covering absent/partial extensions, a changed later function lookup, duplicate
events, player mute, device retry and a foreign current context. Removing the
paired-function guard, persistent mute or deferred flush independently fails.
Device creation/retry API calls are stand-ins; this is not a real-device test.

Windows and Android ARM64/API 24 builds/stages and all **262 Windows native
tests** pass. The new debug APK passes the existing exact-payload, ABI/API,
16 KiB alignment, native extraction and v2-signature verification. Physical
installation, save restoration and Android suspend/listening checks remain
pending the disconnected phone. Background tic scheduling, process-death
recovery and the other AND-01/AND-03 items remain open.

The matched desktop package passes an input-disabled, hidden-window Arena run
with OpenAL's null output: two pauses freeze the gameplay sound world's clock,
both resumes retain its 14 active channels, and the results menu/ledger and
normal shutdown complete. Engine captures show the pause and result screens.
This checks desktop regression without a physical listening claim.

Evidence is `.tmp/stage-two/android-pause-evidence.json`, SHA-256
`cf789c484de5729b9512c58b39ba15f1a1fa021be3e23011085f6f237c0155b0`.
It binds sources, native/negative-control logs, the two builds/stages, SDL/OpenAL
implementation identities, the desktop runtime and debug APK SHA-256
`4e9b8c1ad27e802e9ef1b78eb3f00f87f35203b253dcc7e997ee26175ab90e98`.

## Android background timer recovery, 19 October 2026

The asynchronous worker previously woke every millisecond even while SDL parked
the game loop. The recovered background sleep uses an atomic lifecycle snapshot
and waits 100 ms in the background, retaining 1 ms foreground waits. It checks
shutdown again after sleeping before advancing time or emitting wake events.

The archived sleep alone did not bound accelerated-time catch-up. Android now
limits each scheduler call to ten current, scaled tics, including the first call
after an OS freeze when foreground has already been announced. The clock still
advances and wake events correspond to emitted tics; a long suspension does not
leave a backlog. The 60 Hz base command clock and desktop policy are unchanged.
This does not establish that a suspended multiplayer connection survives.

`android_async.py` compiles the production scheduler, Android worker and atomic
query with controlled clocks, sleep and event APIs. Windows and Linux pass
foreground cadence at five scales, combined game-time scales, one-hour freezes,
smooth slow time, the ten-tic limit, wake counts, shutdown during a background
wait, initialization and imprecise-timer controls. Substituting the old scheduler
fails on unbounded catch-up; the old worker independently fails its sleep check.
The production lifecycle fixtures and million-tic clock-ownership check pass.
All **264 Windows native tests** pass, alongside Windows and Android ARM64/API 24
builds and staging.

The debug APK passes exact staged payload, seven-library, manifest, API/ABI,
16 KiB ELF/ZIP alignment, native extraction and v2-signature checks. SHA-256:
`ba6b01cf8c96d791a353c9ba2f435d832d053c6d3fc51f999127a6ed7e94d242`.
At package verification, Windows enumerated the S25's file-transfer and modem
interfaces, but not its debugging interface. After reconnection, the in-place
debug update installs and preserves the original save, metadata and preview
hashes. That update record is
`.tmp/stage-two/android-s25-async-install-r1/upgrade.json` (SHA-256
`1abe439459c81ba78f43e269a2e7af9d6795b4ecb1a96ffd979eb5701c2fc64d`).
The restore retry awaits an unlocked phone. No measured power, thermal, device
suspension, network-resume or release-certificate claim is made. AND-01/AND-03
remain open.

Evidence is `.tmp/stage-two/android-async-evidence.json`, SHA-256
`fb0c1779500bb4df0a732db00a3850be12a7889e65a29b45f4bb790e63b7438a`.
It binds the source and staged identities, native/negative-control logs, full
suite, builds and verified package. Initial fixture compilation failures from
a misspelled stand-in helper remain beside the corrected passing logs.

## Android early diagnostic path, 19 October 2026

The standalone host supplied `fs_savepath` as private `files/saves`, but the
native default returned `files`. Early fatal reports therefore landed outside
the normal log directory. The host now publishes `OPENQ4_SAVE_ROOT` before
native startup and the platform's default path honors that absolute directory.
Unset, empty, relative or content-URI values retain the legacy host/private
storage fallback. Trailing slashes are removed without erasing `/` itself.
Native fatal reports and the normal log now share `files/saves/baseoq4/logs`.

`android_savepath.py` runs the production default-path and POSIX fatal-path
initializers with environment, string, directory and SDL stand-ins. Windows and
Linux pass precedence, invalid/absent overrides, legacy/private/cwd fallback,
cached paths and breadcrumb placement; the original method fails the explicit
host-path control. The production Java Activity/storage tests also pass on both
hosts, including matching native-environment and engine-argument roots.
Windows skips its unsupported Android-style symlink canonicalization; Linux
exercises that branch. An initial Linux invocation without a JDK fails, and
the rerun with the existing explicit JDK passes.

Windows and Android ARM64/API 24 build and stage successfully. Five focused
Meson path/lifecycle/timer tests pass. The debug APK passes exact staged payload,
manifest, seven-library, API/ABI, native extraction, 16 KiB ELF/ZIP alignment and
v2-signature checks; SHA-256
`c17fe831dfbb3e0f069da4f8721cae52dabcd57a56bd0ece533b7a6ea94cb034`.
This package is not installed, and real-device fatal diagnostics and release
log export remain open. The S25 still has the preceding timer/GUI repair package.

Evidence is `.tmp/stage-two/android-savepath-evidence.json`, SHA-256
`fb522e1cfbde1eacde1ab71843c5fa8bde42d5b4dd5de87aede9d676c308712c`,
binding sources, native/JVM/build/stage logs and the verified package.

## Android Back and fatal-console controls, 19 October 2026

The Android error console previously received mouse events only, while normal
backend startup disables SDL's conversion of touch into mouse input. Its fixed
pixel-sized controls were also too small on dense phone displays. It now owns
one finger/device pair, activates a button on release and cancels on an outside
release, movement over 8 dp, malformed coordinates, focus loss, hiding, resize,
density change, backgrounding or teardown. Synthetic touch/mouse duplicates
cannot override that cancellation. Physical mouse input retains its own path.

Android console layout now uses the display's density with 48 dp button and
input heights and at least 8 dp between adjacent buttons. Fatal presentation
does not start the text-input keyboard, which would cover the report and its
controls. Desktop console layout and text entry keep their previous behavior.

Back had two separate gaps: the engine did not map Android's Back key, and
SDLActivity's gesture callback used Android's default activity-close behavior.
The host now forwards a gesture as one SDL Back press/release pair. Both native
key mappings use the normal menu/pause action on Android. The fatal console
handles Back as dismissal; an ordinary console hides without queuing game exit.
A finishing Activity ignores the callback, and an unavailable SDL load closes
without entering JNI. No gameplay quit command is added.

`android_console_input.py` compiles the production layout, console handlers and
key mappings with counted SDL/command stand-ins. Windows/Linux pass Android and
desktop variants, density/layout checks, ownership/cancellation, duplicate mouse
events, Back up/repeat/window isolation, fatal keyboard suppression and physical
mouse controls. Seven old-code/removed-guard controls fail as expected. The
production Java Activity/storage tests pass on both hosts, including Back's
gesture and unavailable-library branches; substituting the default activity
Back action fails. Windows retains its documented symlink canonicalization
skip; Linux exercises that storage branch.

Both platform builds and stages pass, and all 267 Windows native tests pass
after adding the Android Back mapping. These checks do not establish real
device touch/gesture behavior, console readability, cutout handling or gameplay
controls. The S25 retains the preceding timer/GUI package while its restore
test waits for the user's readiness. AND-01 and AND-03 remain open.

The debug APK passes exact staged payload/manifest, seven-library, API/ABI,
native extraction, 16 KiB ELF/ZIP alignment and v2-signature checks; SHA-256
`99df45c3f7fcdb4c1302a5a7a7e4cc7f92647019571dbe5433e690c285ff265e`.
It is verified but not installed. Evidence is
`.tmp/stage-two/android-console-evidence.json`, SHA-256
`c24f379706013a555e51ce51416f8cdb5bdeaca2b14d8d5ead72cc7ffb42dd44`,
binding the production sources, positive/negative checks, builds and package.

## Android cutout and configuration recovery, 19 October 2026

Archived `20766e8d1` is adapted to the current standalone Activity. On Android
11 and later SDL permits its surface beneath a camera cutout, but stock HUDs
and in-world GUIs still draw across the full render target. The host now pads
SDL's parent layout by the cutout and paints that strip black. The actual
surface shrinks, preserving one coordinate space for the scene, legacy and
retained UI, and input. Insets passed to the children are reduced by that
padding, retaining the remaining system-bar and IME extents. Cutout removal or
movement replaces the padding rather than accumulating it. Earlier Android
versions retain platform cutout handling.

The Activity also handles density and the current SDL template's grammatical
gender configuration changes, avoiding an Activity recreation around an existing
native SDL session. This does not enable portrait play: `sensorLandscape`
remains selected until the remaining mobile presentation/control work is ready.

The production Activity/storage JVM tests pass on Windows and Linux for API
24/29/30/35/36 selection, left/right/top/bottom cutouts, removal, repeated insets,
system/IME remainder and missing or broken SDL layout. Windows retains the
existing storage symlink skip; Linux exercises it. Compiled controls removing
padding, child-inset translation or the API guard fail as intended. The
SDK 35 debug package builds and passes exact staged payload, seven-library,
ARM64/API 24, native extraction, 16 KiB ELF/ZIP alignment and v2-signature checks.
Its native libraries are byte-identical to the console/Back package above.

APK SHA-256 is
`5f0e4fbbea5e049ad9413e9422ba71593a9074b2d0a83119e3b0e88a4e7abb9c`.
Evidence is `.tmp/stage-two/android-cutout-evidence.json`, SHA-256
`b9d882ee0fe381c09b14b5e7178bd6af15efba8ad22fddf44d4668d937a85ed1`.
The package is not installed. Device rotation, density changes, cutout visuals
and touch/controller/IME behavior remain unqualified; AND-01 and AND-03 stay open.

## Android retail verifier backend, 19 October 2026

`Pk4Checksum` and `RetailData` add a read-only backend for AND-03's guided
setup. They do not load SDL, extract archives, change saves or bypass the
engine's startup checks. The current Activity does not call them yet; folder
selection, import, localized results and retry presentation remain required.

The Java compatibility checksum is derived from the RSA Data Security, Inc.
MD4 Message-Digest Algorithm retained in the engine. It folds little-endian
CRCs of nonempty ZIP entries in central-directory order. A separate full read
checks every entry's actual length and CRC; matching central-directory metadata
alone is never reported as payload verification. Classic ZIP metadata is bounded
before `ZipFile` indexes it, with archive, entry, directory and expanded-size
limits. Progress and thread cancellation interrupt the read without clearing
the interruption flag. Observed file and folder changes invalidate the result.
This historical checksum is a compatibility value, not authentication or a
persistent guarantee that the folder will remain unchanged.

The folder scanner requires all 22 core archives and their engine checksums.
Missing optional archives or base language media produce nonblocking findings.
Language patches do not stand in for an unsuffixed base archive; other recognized
localized editions receive payload checks without claiming a known official
checksum. A checksum disagreement in an optional archive remains a warning,
matching the engine baseline, but corrupt present archives must be repaired or
removed before mounting. Retail game-code packs are ignored by the same naming
rule as the engine. Case aliases, unexpected links, unreadable files and limits
produce structured findings for localization. Catalogue and language-list
agreement with the native engine is checked by the executable host test.

`tools/tests/android_retail_data.py` compiles the production Java classes and
an independent probe using the actual native MD4 implementation. It compares
136 vectors, stored/deflated/reordered and empty archives, damaged payloads,
truncated and oversized archives, case duplicates, cancellation and retry,
required/optional selection and changes across a whole-folder scan. With
`--retail-root`, it also reads the caller's media without modifying it. The
local 32-archive baseline passes every full payload/checksum check and the
complete required-set scan. The default fixture run is registered in the
validation runner and both CI script-smoke jobs.

The original MD4 notices are retained, credited in `CONTRIBUTORS.md` and staged
as `licenses/MD4.txt`. Android Gradle and APK verification require this notice;
the exact asset/library allowlists, signing and alignment gates remain enabled.
Setup integration and physical-device qualification remain open.

Windows JDK 21 and Linux JDK 17 fixture checks pass, including Linux link
rejection. Existing Activity/storage regressions and all seven Android build/
package tests pass on Windows. Windows and ARM64/API 24 native builds and
staging pass; no additional full native-suite result is claimed for this Java
and notice change. The SDK 35 debug APK passes exact payload, seven-library,
native extraction, 16 KiB alignment and v2-signature verification. Its SHA-256
is `bbf4311c6f05a51974410380fbae6f718856b35c53347a4092c4bc01cc69d15c`.
Evidence is `.tmp/stage-two/android-retail-evidence.json`, SHA-256
`e7c22dfe8f88130020fb46764601d0be33dc69f6effe80f1807320bdcd104037`.
This package is not installed; the S25's prepared restore test still waits for
explicit readiness and uses the previously recorded timer/GUI package.

## Android document import backend, 19 October 2026

`RetailImport` and `DocumentSource` implement the data-transfer backend for
AND-03. The source uses Android's
[Storage Access Framework](https://developer.android.com/training/data-storage/shared/documents-files)
to read an already-granted document tree. It enumerates at most 4,096 children,
accepts direct PK4 files or one immediate `q4base` child, keeps provider IDs
opaque and opens documents for reading. Unknown sizes are supported. Revoked
grants, unavailable/virtual documents and cancellation have explicit failure
paths; cursors and asset descriptors close on completion and failure. The
system picker, persisted-grant lifecycle and localized presentation still need
to be connected by the setup Activity.

Imports write a bounded `.part` file under the app's external
`.openq4-import/` directory, sync it, fully verify its ZIP payload and required
retail checksum, then replace the destination with Android's same-volume
rename. Source files, private saves and unrelated configuration are preserved.
A healthy official destination is re-verified and reused. Failed copies,
verification, cancellation or replacement preserve the previous destination;
incomplete files are outside `q4base` and cannot be mounted. Retry reuses complete
archives and restarts the current partial archive, accounting for reclaimed
temporary storage. It does not promise byte-offset resume from arbitrary
document providers. Writers are serialized within the host process, and the
completed installation is scanned before readiness is returned.

The production import fixtures cover copy and verification cancellation,
replacement failure/retry, checksum and CRC rejection, provider disconnection,
unknown/changing sizes, insufficient space, healthy reuse, exact save/config
preservation, case aliases, bounds and concurrent-writer cancellation. Linux
also exercises linked destination, work-directory and partial-file rejection.
The document-source fixtures exercise tree-relative read-only calls, metadata
and enumeration bounds, revoked access, cursor/descriptor cleanup and
cancellation during query/open/read. An end-to-end fixture imports provider
streams through both production classes. These are host API substitutes;
physical Android document-provider behavior remains unqualified.

Windows JDK 21 and Linux JDK 17 checks pass, and the four production
retail/import classes compile against Android SDK 35. Existing Activity/storage
regressions pass on Windows. Both new executable tests are registered in the
validation runner and both CI script-smoke jobs. This remains backend work:
no launcher entry point or persisted setup completion is enabled by it.

The debug APK builds and passes the exact staged payload, seven-library,
ARM64/API 24, native extraction, 16 KiB alignment and v2-signature checks.
Native payload bytes match the preceding verifier package; no new native-suite
result is claimed. APK SHA-256 is
`33cdd7f5bf2548b9e16553dfc1770a0f837efc97e8d15842c740eb2011786f57`.
Evidence is `.tmp/stage-two/android-import-evidence.json`, SHA-256
`1da00da516c042f4fea15b170013d03c2ce2247ea0b0191149d39afade91269e`.
The APK is not installed; the S25 restore check remains unchanged.

## Android language and data setup, 19 October 2026

The public `LauncherActivity` now connects AND-03's retail/import backends to
language and data setup before loading SDL. English, German, Spanish and French
have complete setup resource sets, with a live language sample, localized
recovery messages and explicit Single player/Multiplayer actions after a
successful check. The native game Activity is internal. A changed setup
language is passed to the next prepared engine launch; ordinary launches keep
the game's saved language. No background completion automatically starts play.

The system folder picker requests read access. Offered persistent read grants
are retained; choosing another source releases the previous persisted read
grant. Temporary grants are identified, and lost/revoked access directs the
player to choose the folder again. The worker uses application context and
opaque provider URIs, never an Activity or an inferred filesystem path. It
connects import or installed-data validation to bundled-file preparation only
after the required retail set succeeds. Missing language and optional files
remain warnings. Failure messages name the file and corrective action; storage
shortfalls show exact required/available bytes for the next file.

`SetupTask` retains one operation through view rebinding. It coalesces progress,
delivers on the UI thread, rejects stale callbacks and keeps cancellation in a
stopping state until the worker returns through its cleanup. Provider
cancellation runs off the UI thread. Setup records its selected source and
pending operation for explicit retry after process death. Completed archives
are reverified/reused and the active partial archive restarts; this does not
promise arbitrary provider byte-offset resume. Package extraction now exposes
verification/copy progress, typed insufficient-space failures and interruption
checks, including the final check before replacing a destination.

`StorageSession` excludes setup from a starting, running or closing native
session. SDL's bounded Activity-destruction join does not release the engine's
separate ownership: it lasts until `main()` returns. A game restored by Android
while setup is finishing waits on SDLThread, and a destroyed waiting Activity
does not enter native code. Host tests exercise the actual Activity's lifecycle
hooks and the independent ownership coordinator. They do not establish real
Android Recents/process-death behavior.

The native setup views use the staged Marine/Lowpixel typefaces, vector cut
plates, scrollable content, at least 48 dp actions with 8 dp gaps and safe-area
insets. Pointer movement beyond 8 dp or release outside the action cancels it;
keyboard/controller/accessibility activation stays on Android's Button path.
Font copies in the APK are checked byte-for-byte against the staged `pak0.pk4`.
Four-language completeness and format-argument checks pass. These are source
and packaging results: physical touch, TalkBack, large-text layout, visual
fidelity and translation review remain unaccepted.

Windows JDK 21 and Linux JDK 17 checks pass for task ownership and the complete
provider/import/retail/package pipeline. They cover cancellation between retail
import and bundled-file preparation, retry without reopening a healthy source,
provider cancellation, native exclusion, missing-data admission and exact
private-save preservation. Updated Activity/storage tests pass on both hosts,
including Linux links. All seven Android build/package tests pass on Windows.
Both new executable suites are registered in the validation runner and both CI
script-smoke jobs.

SDK 35 debug packaging succeeds and passes exact payload, seven-library,
ARM64/API 24, native extraction, 16 KiB alignment and v2-signature checks. The
public launcher and non-exported native Activity are confirmed in the packaged
manifest. APK SHA-256 is
`93ad4ca0c40402ed0177d7b33e550fcd2c94dff1f7977959ddf62f6e18bdcd11`.
Native library bytes match the preceding import package; no new native-suite
result is claimed. Evidence is `.tmp/stage-two/android-setup-evidence.json`,
SHA-256 `335db0e247ff6485a578937783964b51e9b516a0a16373ee647fa04ad453cb95`.

The additional full Android lint run remains unsuccessful, with 28 errors in
the existing SDL Java sources: Bluetooth permission calls, the API-29 pen
helper and pre-33 broadcast registration. Inspection finds permission/version
guards at the relevant SDL initialization/dispatch paths; these diagnostics
are retained for dependency review, not reported as reproduced device failures
or silently suppressed. Setup retains all languages offline and delegates
activation to Android's Button; lint still reports language-bundle and custom
touch-delegation advisories. Existing storage-space advisories remain too.

This increment does not close AND-03 or accept the full first-run design.
Controls/live testing, interface-profile/text-size setup, in-game Settings
re-entry, accessible log/save export, release signing continuity and physical
provider/lifecycle/visual/accessibility evidence remain. Other engine languages
are still available in the game but lack setup translations. The APK has not
been installed; the original S25 restore test still uses its recorded package
and waits for explicit readiness.

## Android save and log export, 20 October 2026

AND-03's setup screen now offers saved-game backups and log export without
requiring a successful retail-data check. Android's create-document picker
chooses a new ZIP destination with a temporary write grant. The worker retains
only application context and does not persist the output URI. An interrupted
process offers explicit retry with a new picker destination. The four setup
languages include progress, cancellation, low-space and provider-failure text.

`PrivateExport` reads only each private game's `savegames` or `logs` subtree.
Save descriptions, previews and previous-save files are included when present;
retail archives, bundled packages and settings are excluded. It prepares and
syncs a complete temporary ZIP before opening the destination. The root
`openq4-export.tsv` records the selected kind, per-file SHA-256, length and
relative path. It rejects links, case aliases, unsupported portable names,
more than 4,096 visited entries below the selected folders, paths over 1,024
UTF-8 bytes, folder depth over 16 and more than 2 GiB of source data. A
conservative temporary-space preflight includes archive overhead. File
metadata and whole-folder inventory are checked again for observed changes;
this is not a filesystem-wide atomic snapshot guarantee.

A live snapshot owns `StorageSession` until its temporary file is released.
Native game/import work cannot overlap packing or provider transfer. Cancelled
or failed preparation removes only the task's private temporary file. The
reserved cache directory removes abandoned regular files matching its generated
filename pattern and rejects links or unexpected matching directories; unrelated
cache files remain untouched. No source file is modified by export.

`DocumentExport` writes only the picker-created document using a cancellable
write descriptor. Cancellation requests provider cancellation and closes the
active output off the UI thread. The worker waits for descriptor close before
releasing its storage ownership, including when a write was blocked. Success
requires complete copy, flush and close. For cloud/pipe providers this means
stream acceptance, not proof of remote durability or completed upload. Failed
or cancelled destinations are not silently deleted: localized guidance warns
that they can contain partial ZIPs and asks for a fresh destination on retry.

`SetupDiagnostics` records data-check/import outcomes and bounded exception
details under `baseoq4/logs/setup.log`, with one previous log and 256 KiB rotation.
Messages, cause depth and stack frames are bounded; linked diagnostic paths are
rejected. These diagnostics are included in ordinary log export even when the
game could not start. Only the owning setup worker writes them, outside export.

The actual production snapshot/provider classes pass Windows JDK 21 and Linux
JDK 17 fixtures for exact ZIP contents, independent CRC/manifest checks,
cancelled packing/open/write and late descriptor publication, denied/missing/
invalid destinations, write/flush/close failures, cleanup/retry, low-space
preflight, changed files/folders, bounded diagnostics and native storage
exclusion. Linux exercises links and reserved filenames; this WSL mount is
case-insensitive, so the physical two-case-alias fixture is explicitly skipped
locally and remains enabled on case-sensitive hosts. Existing setup pipeline
checks pass on both hosts, including diagnostic results, and all seven Android
build/package tests pass. Both export suites are registered in the validation
runner and CI script-smoke jobs.

SDK 35 debug packaging and exact payload/seven-library/API 24/ARM64/native
extraction/16 KiB alignment/v2-signature checks pass. APK SHA-256 is
`b7938d1d63e66ed41d83c097ae704ad2a37d65cd18a3fd85216c2c0a6b3cc210`.
Native library bytes match the previous setup package; no new native-suite
result is claimed. Evidence: `.tmp/stage-two/android-export-evidence.json`,
SHA-256 `3fe372efcc25cd904001f1752e5c39089c4858ad20ff4ae1011e7e8f78ca685c`.

The user completed both exports through the S25's actual system picker to local
Downloads. The save ZIP contains all six original save/description/preview files;
each extracted byte, length and SHA-256 matches the source, and the originals
remain unchanged on the phone. The log ZIP contains all three existing native
logs. Independent ZIP CRC, exact inventory and integrity-manifest checks pass
for both. Save ZIP SHA-256 is
`89c819683a4365e4e911c5220900b76f8e2035d620b5fa0e03b009503dbafed1`;
log ZIP SHA-256 is
`783db2f513935dff04bf2493faa7ab41a2fe39412f2debb856faaa5b9afec584`.
The later installed-data check also writes `setup.log` with a successful,
zero-finding result; that log was created after these exports. Host fixtures
establish its inclusion in subsequent log exports.

Other providers, process-death recovery, accessibility/visual acceptance and
release signing continuity remain separate AND-03 gates. The
[player guide](../user/android-setup.md) describes use, limits and the current
absence of backup-ZIP import.

## S25 original-save restore round trip, 20 October 2026

The previously prepared timer/GUI-repair APK, SHA-256
`ba6b01cf8c96d791a353c9ba2f435d832d053c6d3fc51f999127a6ed7e94d242`,
now passes the original-save retry on the test Galaxy S25 (SM-S931B). It loads the unchanged
`Stage2AndroidSmoke` save in the Storage 1 opening sequence, reaches an active
draw, writes `Stage2AndroidRestored`, loads that new save and exits cleanly.
Both 2340x1080 captures come from the engine's `screenshot` command and show
the rendered opening scene. No gameplay input was injected, audio was disabled
and no forced stop was needed. This establishes the specific restore boundary;
it is not campaign, combat, audio or lifecycle acceptance.

The following export-APK update also preserves the original save, description
and preview SHA-256 values. The original save remains
`e9ab176d9df0b279bea580a483248aa528641b127b55b4138098feaad627ad90`.
Before manual setup testing, the three exact test scripts were hash-checked,
copied to the local evidence directory and removed from the app so subsequent
ordinary launches cannot repeat the scripted load/save/quit sequence.

Evidence: `.tmp/stage-two/android-s25-restore-evidence.json`, SHA-256
`d155ff135e500c88c9b2549f54de807ec71591836f155f642a7f548c4bb7111b`.
The record includes original logs, engine captures, the tested package identity,
script hashes and subsequent update/save-preservation evidence. Retail
mixed-case path warnings and unsupported mobile AA/MSAA fallbacks remain.
The export APK contains three changed native libraries relative to this tested
timer/GUI package; its separate restore result follows.

## S25 exported-save restore, 20 October 2026

The export APK identified above also loads the save extracted from the actual
backup ZIP. A verified copy under a new test slot leaves the six existing files
untouched. The user completes **Check installed files** (ready, zero findings)
and selects **Single player**. The scripted probe restores Storage 1's opening
sequence, captures the rendered scene, creates another save, reloads it and
writes a second engine capture. Both captures are 2237x1080 after the current
cutout handling and show the active opening sequence. Audio is disabled and no
gameplay input is injected. This does not establish campaign or combat coverage.

The initial helper incorrectly classified shutdown by waiting for the package
to have no process. Android records the game's PID 9718 exiting itself with
status 0 at 06:17:51.338 local time, then recreates setup as PID 12597. The
helper force-stopped that new launcher at 06:18:24.515 after its false timeout.
The original failed helper result is retained alongside engine logs, Android
exit records and Activity events; the successful restore and normal engine exit
are assessed from those records, without presenting the helper as a passing
run. An earlier Windows ADB stdin transfer also stopped at the first SUB byte;
binary `adb push`, exact readback and the private test-slot copy resolved that
preparation failure. Neither helper issue changed the exported ZIP or originals.

Evidence is included in `.tmp/stage-two/android-export-evidence.json` and
`.tmp/stage-two/android-s25-export-r1/`. Test configuration files, the two test
save slots and device captures are removed after hash verification, with local
evidence retained and the original six files checked again. Backup-ZIP import
is not yet a player-facing feature;
this restore uses a developer-placed copy and qualifies the exported bytes on
the recorded package only. Later packages, release-certificate upgrades,
provider interruption and broader gameplay retain their own gates.

## Android gameplay touch recovery, 20 October 2026

The standalone host now includes the first-party controls recovered from
`486ecf735`, adapted to the current SDL, retained UI and usercmd ownership
contracts. A floating movement stick, independent drag-to-look, Fire,
Jump/Crouch, weapon tap/hold selector and capability-dependent Reload/Zoom
use vector artwork. Pause remains available during cinematics; multiplayer
also exposes scoreboard and chat actions. SigmaTouch retains its separate
host bridge.

Touch motion and button edges have a dedicated, synchronized usercmd channel.
They remain usable with physical joystick input disabled and do not inherit
mouse sensitivity, acceleration or smoothing. Quick taps survive an intervening
render frame; reload/weapon impulses are queued one per game command. Wheel
steering uses an absolute touch offset and does not turn the camera or apply a
second hold delay. Focus, menu, controller/mouse/keyboard handoff, disabled
controls and geometry changes release held input. Device/finger identity and
contact ownership prevent a gameplay contact from becoming a menu click after
the menu opens.

The recovered layout's out-of-bounds enlarged controls, overlapping multiplayer
targets and nonfinite-density geometry are repaired. Hit regions retain at
least 48 physical dp and 8 dp separation across the tested sizes/scales.
Discrete actions use release activation and permanent 8 dp drag cancellation.
The full layout editor, independent saved orientation layouts, portrait
presentation, setup/live preview, haptics and broader accessibility acceptance
remain open; the current fixed layout does not establish those gates.

Validation on the recorded source:

- Native layout/state tests cover 54 phone/tablet/orientation/density/scale
  combinations, invalid geometry, multiple touch devices, contact overflow,
  final-owner release, cancellation and queued usercmd edges/impulses.
- The production-body integration fixture passes on Windows and Linux,
  covering multi-finger move/fire/look, disabled physical controllers,
  quick taps, wheel ownership, cinematic/menu/focus changes and hidden
  weapon capabilities. The existing SigmaTouch input fixture also passes.
- All eight selected Meson touch, lifecycle, engine-thread and native-input
  tests pass. The retained-input fixture and 67 weapon-wheel behavior checks
  pass, including conflicting mouse/controller input during touch steering.
- Windows and Android ARM64/API 24 builds pass. The standalone debug APK,
  SHA-256 `345668f4ff8ffad0fa1b653c234c73a51a9ebd22b1d8b6da5ce3276f4f9e013d`,
  passes exact payload, matching SDL/native metadata, API, signature and
  16 KiB alignment checks. Its same-debug-certificate S25 update preserves
  all six original save, description and preview files.

Evidence is under `.tmp/stage-two/touch/` and
`.tmp/stage-two/android-s25-touch-r1/`; `apk-verification.json` records
the base commit, changed tracked/untracked source hashes and staged native
library hashes. A scripted S25 Hangar 1 run moves 200.8 game units while
firing and turning 11.2 degrees, then settles at an identical position/view
across the released and neutral samples. A short weapon tap advances slot
1 to 2; holding and steering the selector chooses slot 3 on release. Pause
clears every held action. The same run restores the unchanged original
Storage 1 save, writes/reloads a disposable slot and exits itself with status
0 (PID 16518). All six engine captures are 2237x1080. Original settings are
restored and the exact test scripts, slot and captures are retired after
hash verification, while the six original save files remain unchanged.

`probe-assessment.json` retains this as **behavior pass, visual fix required**:
the ordinary gameplay plates overlap the open weapon wheel. The subsequent
draw change suppresses those plates while preserving Pause and the active
selector gesture. Its verified APK, SHA-256
`0116fe65ced53f28426406f8768cdbb0573194669fc14360aacac883d4d9267f`,
preserves the six original files through another debug-certificate update.
The engine's `s25-touch-r2-wheel-clear` capture confirms the unobscured wheel.

The user then completed the three-minute Hangar 1 physical check on the test
Galaxy S25 (SM-S931B, Android 16/API 36): simultaneous movement/look/fire
and release, tap-next and hold/drag weapon selection, audio stopping in the
background, return through Recents to a paused game, and resumed audio/fire
all passed. The recorded response is “All checks passed.” The engine exits
itself with status 0 (PID 20571); original settings are restored, verified test
scripts/captures are removed from the device and all six original save files
remain unchanged.

`.tmp/stage-two/android-s25-touch-r2/` retains the source/package identities,
engine captures/log, Android exit record, `physical-observation.json` and
`manual-cleanup.json`. This establishes the stated gestures and one audible
background/resume cycle on that device and build. Other devices, controllers,
portrait/layout editing, sustained suspension, process death and resumed
network sessions retain their separate gates. Release-certificate continuity
is not established by either debug update.

## Android diagnostic emulator recovery, 20 October 2026

The selected x86_64 work from `023f0eeee` and `1484195c4` is integrated against
the current build and package contracts. A shared ABI table drives the NDK
cross file, dependency builds, module suffixes and ELF machine checks. Meson
stages the seven x86_64 native libraries separately from ARM64 and records
the ABI, package variant, API and SDL version. No toolchain, API or SDL version
was raised.

Gradle requires matching stage metadata and an exact native-library set.
The localized diagnostic app uses `com.darkmatter.openq4.emulator`, separate
output directories and private data; its release APK/bundle variants are
disabled. The APK verifier requires this identity and a debuggable package
only when explicitly selecting `--variant emulator`. The default phone
verification still requires the established ARM64 identity and a nondebuggable
release. Both paths retain signature, 16 KiB ELF/ZIP alignment, exact asset and
notice checks. Old phone metadata without a variant remains accepted solely
as the original phone format.

The API 24 x86_64 SDL 3.4.16/OpenAL 1.24.3 dependencies, all 1,246 native
targets and the debug APK build pass. APK SHA-256:
`055e3972f4eb87c9ebd03d6b308c3f021fbb7ef3aba83b5bf6234a514e5119a3`.
Exact package verification passes with the recorded diagnostic certificate.
An actual emulator `assembleRelease` request is rejected because the task
does not exist. The ARM64 debug build and exact payload/manifest regression
pass; its release task remains available in a dry run. These checks do not
claim a newly signed phone release.

Fourteen Windows build/harness checks pass; Linux passes the same suite with
the Windows-only Meson-wrapper test skipped. They cover mismatched stage,
ABI, ELF machine, library/asset sets, SDL metadata, certificate, package
identity and debug admission, as well as the emulator's capability and
private-file error handling. Logs are retained under
`.tmp/stage-two/emulator/`.

`tools/tests/android_emulator_smoke.py` verifies an exact installed APK or
installs that package without clearing data, hashes independently copied
retail files and runs engine-command SP/touch/save or MP diagnostics. It only admits
an explicitly selected, booted, rooted x86_64 emulator with GLES3. It uses the
internal game Activity, so it is not setup-navigation evidence. The ordinary
Android shell's Windows stdin translation and newer Android shared-storage
restrictions required binary `exec-in` private-file writes with exact
readback. Shell command errors are distinguished from missing files.

API 24 default x86_64 image revision 8 exposes GLES2 with both SwiftShader and
NVIDIA host graphics. The preserved initial run reaches the native fatal
path after EGL rejects ES3; it is stopped and its scripts/configuration are
cleaned up. The corrected probe rejects this image before app/data writes
(`api24-host-preflight/report.json`). This is an explicit unsupported virtual
configuration, not API-floor gameplay evidence. A 4 GiB configuration also
panicked in the old guest kernel before app installation; its 2 GiB
configuration boots. The original failures and both graphics reports remain
with the evidence.

The current Android 17/API 37 Google APIs 16 KiB x86_64 image, revision 6,
boots with emulator 37.2.12 and exposes GLES3 through SwiftShader. The guest
reports 16,384-byte pages. The final SP run (`current-sp-final/report.json`)
enters Hangar 1, moves 244.4 units and turns 12.8 degrees with simultaneous
touch movement/fire/look, then holds an identical released/neutral pose.
A disposable save restores that same pose. The MP run
(`current-mp-r2/report.json`) enters Q4DM1 with `game_mp` and explicit
auto-join, moves 236.0 units, turns 12.8 degrees and settles identically after
release. Both exit themselves with status 0 (SP PID 23583, MP PID 21175).
Each report retains its exact runner, source and package hashes, independently
verified retail archives, engine log, exit record and three 1280x720 engine
captures. Test scripts, captures and the SP save are retired after collection;
the original settings are restored.

These are silent software-rendered virtual-device results. The reviewed
captures show the native world and HUD, but also expose a Pause/rank overlap
in multiplayer and a reload/status overlap in SP. A separate corner-hit
regression reproduces visible button corners outside their touch region.
Those presentation/input findings require fixes before touch-layout acceptance;
this runtime pass does not close the portrait/editor contract. The earlier
portrait SP pass and failed probe attempts remain retained with their original
reports, including the corrected pose-parser reassessment. Physical gestures
and audible resume remain established only by the separate S25 result.

The follow-up fixes inscribe each complete button plate, rail and pressed
marker inside its hit region, reserve space below the active top controls for
the retained HUD, and lift the default Reload plate above the status readouts.
The top reservation accounts for the HUD viewport and drawable density; menus,
backgrounding and physical-input handoff remove it without rewriting saved
HUD preferences. Windows Clang/MSVC and Linux production touch tests pass,
including the original failing corner-hit control, all 54 layout combinations
and the active/inactive HUD handoffs. Windows, ARM64 and x86_64 native builds pass.

APK `73a63e2155ebfa42cd8195d55234793eb6606ac6ccd6dae12bf7e43b09d48800`
passes exact verification and the same emulator's SP/MP checks in
`touch-hud-sp-r2/` and `touch-hud-mp-r2/`. All six engine captures were reviewed:
Pause clears the MP rank display and Reload clears the SP status readouts.
SP moves 134.2 units, MP 246.2; both turn 12.8 degrees and settle at identical
released/neutral poses. SP restores that pose from its disposable save.
Native self-exits are status 0 (SP PID 7458, MP PID 3866), and exact script,
capture/save cleanup restores the original settings. The emulator's previous
rotation settings were restored before shutdown.

`touch-hud-mp-r1/` remains a failed release-settling sample: no action remained
held, but the player travelled about one unit between the 600 ms and later
samples. The runner now uses a fixed Q4DM1 starting pose and allows two seconds
plus 60 engine frames for settling, retaining the strict identical-pose
assertion. This bounded default-layout correction does not qualify custom
layouts, the full Touch HUD preset, portrait presentation or physical gestures
on the new APK. The S25 retains its previously accepted build.

## Android Settings return, 21 October 2026

The standalone Android Settings shell now exposes a localized Android setup
action. The engine sends an SDL host message, and the Activity opens the
existing launcher without finishing the native game. Duplicate requests are
suppressed until return; missing/closing hosts fail safely. Desktop and
SigmaTouch builds keep this action unavailable. Return to game now appears on
both the language and data pages. The existing storage lease still blocks
imports, checks and exports throughout live/closing native ownership.

Production-method JVM checks cover message delegation, duplicate suppression,
re-entry, launch failure and inactive guards. Native Windows/Linux checks cover
the engine message and platform exclusions; five deliberately broken controls
are rejected. Resource/pipeline, fourteen Android build checks, Settings
coverage and the retained UI gate pass. The Linux JVM attempt lacks a JDK and
is retained as an unavailable test. Windows, ARM64 and x86_64 builds/stages pass.

The current 16 KiB Android 17 x86_64 emulator runs APK
`c72a13d2fcc9a3584a27db6f1baedd89f48e4c7aa91971022816ef367dfa7264`.
`emulator/setup-reentry-r3/report.json` records two actual engine Settings
actions from Hangar 1 into setup, with Return to game visible in the Android
accessibility hierarchy. The probe brings the existing game Activity forward
with `am start`; it does not tap that button. Both handoffs preserve native
PID 16360, the map pose and save SHA-256
`4620ca7a113191c801e3fa55bf3a442881edc82a744d2aa30b1c76deb7e466c9`.
After both returns the game resumes and exits itself with status 0. The report's
cleanup force-stop applies only to the separate launcher process recreated
after that exit. Exact test files are retired and the original config restored.

All four engine captures were reviewed: the button is visible in Settings
before and after both returns, followed by normal Hangar 1 gameplay. The
portrait emulator view does not establish the wider portrait-layout contract.
The Windows active-map control confirms the Android action stays invisible
and unreachable. Earlier attempts remain failed: the first used the wrong
asset-root argument; the second completed the product handoffs but used an
outdated Activity-status parser and misclassified normal exit/launcher restart.

The ARM64 debug APK
`b248dc1554e9ac67975c3b5cb4b8c10030d2fc4c1d78da8cf042135dff323955`
passes exact payload, manifest, certificate and 16 KiB packaging checks. It
has not been installed or physically tested on the S25. These results do not
qualify physical taps/audio, release-signing continuity, full controls/interface
setup or all provider/lifecycle cases. AND-01/AND-03 remain open.

Evidence is bound in `.tmp/stage-two/android-setup-reentry-evidence.json`,
SHA-256 `7882bee8a0ff08fdbbf3ccd817a47c45b11fcf6dcc19ec91f2514bb7bcd86ee6`,
including source/package identities, reports, logs, negative controls and the
capture review. All referenced paths are relative to `.tmp/stage-two/`.

## Android controls setup, 21 October 2026

The standalone setup now offers Default, Compact and Left-handed touch
presets, 75–150% button sizing, 0.1–4.0 look sensitivity and a live look-speed
grid. It reports controllers detected when the page opens. Four complete
localized resource sets retain the setup typefaces and 48 dp controls.
The look test uses gameplay's 0.25 degrees per physical dp, without submitting
input to the running game. This is not the full touch layout editor or
interface-profile step.

One versioned preference record is applied atomically. Unapplied drafts
survive setup recreation separately. Failed disk commits restore the previous
in-memory applied record as well as retaining the draft; a failed first apply
keeps the engine's existing preferences. The engine reads the applied record
through the actual Activity/JNI boundary at startup/foreground entry. All CVar
and input changes run on the engine thread. Missing/malformed host data leaves
the current values unchanged, JNI exceptions are cleared and local references
released. Preset or scale changes release held contacts before moving targets.
Once setup choices are applied, they remain authoritative on later game returns.
The Return to game control refreshes on the language and controls pages too,
including when the native game finishes while setup is open. Its refresh does
not depend on the data-installation status view being present.

The native matrix now covers 162 viewport/density/size/preset combinations,
including 48 dp targets, spacing, mesh containment and crosshair clearance.
Mirrored movement preserves drag direction; the look axes do not invert.
MSVC, Windows Clang and Linux checks pass. JVM tests pass 1,920 preference
round trips, malformed recovery, initial/update write failures, draft isolation,
process persistence, the production Activity getter and active/closing/ended/
stopped return-control refresh. Five deliberately broken JNI/transaction/
foreground/return-refresh controls fail as expected. Resource/pipeline
and existing setup ownership tests pass. Windows and both Android native builds
and the phone/emulator APKs pass.

`emulator/touch-setup-r2/report.json` records exact APK
`6b8d7bb65382c44ca2154e35c86c5a67728305ed07d9c9e7f8df2b96d2aa28ec`
on the same Android 17 16 KiB x86_64 emulator. A prepared applied preference
selects left-handed, sensitivity 1.5 and size 115%. The actual Java/JNI path
applies those values at startup and after both engine Settings/setup returns,
replacing deliberately altered test CVars. The map pose and save remain
unchanged. Engine captures show the mirrored controls and smaller Compact
plates; all test scripts/config/preferences are restored or retired after a
normal native exit. The recreated launcher's later cleanup stop is separate.
This does not exercise physical setup taps, controller input or audible output.

The ARM64 debug APK
`19d123703a100e38d63a3024483b0fc77320fe9e0d937a7918d915e2554b8ee6`
passes exact payload, manifest, existing debug-certificate and 16 KiB checks.
S25 installation and physical setup testing are pending. Layout editing,
full portrait/Touch HUD presentation, interface setup and broader controller/
accessibility/lifecycle coverage retain their gates. AND-01/AND-03 remain open.

Evidence: `.tmp/stage-two/android-touch-setup-evidence-r2.json`, SHA-256
`614dd334c0847e75451c0592f8955172e1a4f8cc98a318d735680f65c8aafdff`.
Its bound reports retain source/package identities, native/JVM logs, rejected
controls, engine captures and exact cleanup records.

## Android interface setup, 21 October 2026

The standalone setup now has an Interface page beside Controls. It offers the
[section 13.2 viewing profiles](ui-visual-design.md#132-viewing-profiles):
Automatic (Phone, or Tablet from a 600 dp smallest width), Desk, Couch,
Handheld, Phone and Tablet. UI size (75–200%) and text size (100–200%) can
replace the profile's values. Phone and Tablet text starts from the Android
font scale. A live sample and the resolved sizes are shown, in all four setup
languages.

The page edits a draft. Apply writes one versioned record with a new 32-hex
transaction identity; a failed disk commit restores the previous applied record
and keeps the draft. The Activity resolves profile, screen class and font
scale. The engine reads the record through `getInterfaceSetup()` at
startup/foreground entry. It applies the record to `ui_retainedScale` and
`ui_retainedTextScale` only when its identity differs from the archived
`ui_androidInterfaceApplied`, so later in-game changes survive returns and
restarts until the next Apply. Malformed, out-of-range or oversized records
and JNI exceptions leave the CVars unchanged. No separate viewing-profile CVar
is stored; the engine consumes the profile only through the resolved scales.

`tools/tests/android_interface_setup.py` passes 3,564 preference round trips,
the 13.2 defaults, font-scale bounds, transaction identity, draft isolation,
initial/update commit failures and the actual Activity getter. Its production
JNI reader accepts all 960 Java-produced records and rejects every invalid
payload. The native `openq4-android-interface-setup` test checks 2,730 records,
applied-once identity and truncation/range/format rejection under idlib's
string-function replacements. Seven deliberately broken boundaries fail.
Linux g++/clang++ runs pass the native and JNI halves; WSL has no JDK, so the
JVM half ran on Windows. The Activity storage fixture now compiles the
re-entry, touch and interface APIs. The Windows native suite passes 273/273.

`emulator/interface-setup-r5/report.json` records APK
`cd3e472920ca7838053084f7bd0d12197e150ded9898e97fdf2164c4fa6e8e03` on the
Android 17 16 KiB x86_64 emulator. A prepared Couch record applies once at
startup (150%/125%). In setup cycle 1, uiautomator and `adb shell input` inside
the headless emulator open the actual Interface page and select 90% size and
150% text. The page previews them and Apply writes a new identity. The return
applies 0.90/1.50 exactly once. An in-game 1.1 UI scale survives the second
return without re-application. The touch-setup and Settings re-entry assertions
pass in the same run, and the test preferences/config are restored.

The engine captures of the retained SYSTEM page show visibly larger type after
Apply. In portrait, the root-menu fit to 640x480 dp holds the effective menu
density at the 640 dp width, so UI size does not change that capture. On
landscape phones the same fit caps density at 480 dp of height. UI size is
therefore visible only on larger screens, while text size applies everywhere.
Only retained screens follow these CVars; the classic in-game Settings menu
does not. The previewed SYSTEM page also lists UI scale/text size as 100%
whatever the CVars hold; that display was not investigated. The 13.2 touch
fitting exception (640x360 compact layouts) and an in-game Interface settings
page remain UI work. Earlier runs r1-r4 failed on a short deadline, an emulator
System UI ANR and a probe race, or predate the final strings. Physical S25
interaction is pending; AND-03 remains open.

Evidence: `.tmp/stage-two/android-interface-setup-evidence.json`, SHA-256
`4ab95f6993ed201853bb886bc5ae6b30f80523eebd69a1cf627e1e341d2962fc`.

## Android portrait thumb deck and orientation ownership, 8 November 2026

The built-in touch controls now have the section 13.6 portrait arrangement.
When the window is taller than wide, the band below the 640 u center screen
is the thumb deck. The stick floats in its leading half. Fire sits in the
middle of its trailing half with the weapon control above it, jump and crouch
outside it and zoom and reload inside it. At the 720x1280 dp reference the
columns keep the landscape cluster's spacing and the whole cluster fits the
deck. A 360-412 dp phone has only 180-206 dp of trailing half, so the outer
and inner columns move in and the rows rise; sizes and the 8 dp gaps stay as
in landscape. On a 9:16 phone (360x640 dp) the weapon, jump and zoom controls
rise above the deck; on a 19.5:9 phone (S25 class, about 384x832 dp) only the
weapon control crosses the deck's top, by about 28 dp. At 150% control size the cluster climbs into the view. Each
orientation is computed on its own, so turning the device swaps arrangements
and the player's preset and size apply to both. While the overlay is active in
portrait, the Remastered HUD's bottom readouts rise to the deck's top edge
(`AndroidTouchControls_HudBottomEdge`), as the Touch HUD preset specifies.
Landscape placement is unchanged.

The host also owns the orientation now. The manifest selects
`sensorLandscape`, but SDL's `setOrientationBis` runs when the engine creates
its window and, for openQ4's resizable window without an orientation hint,
requests `FULL_USER`. On the diagnostic AVD the 2 November APK
(`d3c28b451d18442830071498a41d4af80f7ba21323808993bddaa1b1d5ff64be`) reported
`requestedOrientation=SCREEN_ORIENTATION_FULL_USER` after window creation;
the display did not turn during that 45-second check, so the effect on devices
was not observed. `OpenQ4Activity` overrides `setOrientationBis` and applies the
player's choice at creation, on SDL's request and on each return from setup:
`SENSOR_LANDSCAPE` by default, or `FULL_USER` (Android's rotation lock
respected) after **Landscape and portrait** is applied on the Interface setup
page. The Interface record becomes version 2 with the orientation; version 1
records decode as landscape, and the engine's record keeps the sizes only, so
the native reader is unchanged. Strings exist in all four setup languages.

Checks: `openq4-android-touch-controls` adds the deck arrangement at the
reference and four phone shapes, two densities and all three presets (trailing
half, Fire centred, weapon above, outer/inner columns, stick and look regions,
landscape unchanged); removing the portrait placement fails it.
`android_touch_controls.py` covers the bottom reservation's landscape, portrait,
menu and controller cases. `android_interface_setup.py` passes 7,128 round
trips with orientation, version 1 migration, malformed version 2 records and
the Activity's mapping; its JNI half accepts all 960 Java records.
`android_host_storage.py` drives the real Activity through creation, SDL's
request and setup returns; delegating to SDL's request fails it. The Android
setup, touch setup, build, lifecycle, window, save/base path and console host
tests pass.

`android_emulator_smoke.py --mode rotate` is the new silent rotation probe. It
applies the version 2 record, turns the AVD with its `user_rotation` setting
(no input) when the engine asks, drives the portrait deck through
`touchControlsTest`, turns back and saves/loads, then restores the AVD's
rotation settings, the app's setup preferences and configuration. Its report
(SHA-256 `00fc29d331b5fd1164bd73ce1fdb2e925d447f19f59bcae3d6b4496eeb4b5c8f`)
records APK `bf189b254eb1db6b917e4244e1946feb0e0b4caa62312c6701ebcf33676c2195`
on the AOSP API 35 x86_64 AVD (Android 15, 4 KiB pages, SwiftShader). Hangar 1
turned from 1920x1080 to 1080x1920 and back mid-match in one process, with the
player's pose unchanged by either turn. In portrait the stick, a look drag
above the deck and Fire moved the player 208.6 u and turned it 15.4 degrees;
the release settled and the save restored that pose. The portrait capture shows
the view across the width, the status bar just above the deck and the cluster
in the trailing half. With the default record and the AVD set to portrait,
the new APK held `SENSOR_LANDSCAPE` and rotation 0 after window creation.
Evidence is under `.tmp/android-evidence/2026-11-08-portrait/` in the main
checkout. Physical phones, the stock GUIs (Classic HUD, stock menus) in
portrait, the layout editor with per-orientation layouts and readouts behind a
raised weapon control on narrow phones remain open; AND-01 and AND-04 stay open.

## Android touch layout editor, suspension save, network resume and Quest flavor checks, 9 November 2026

**Touch layout editor.** Section 13.6's editor now exists. **Edit layout** on
the Controls setup page, or the game's **Settings > Controls > Touch layout**
(an Android-only row closing the Other tab, session verb
`controlsTouchLayout`, which opens setup straight at the editor through SDL
user message argument 1), shows this device's screen in either orientation
with every control and its name. Dragging moves a control on an 8 dp grid;
**Size** (75-150%), **Opacity** (10-80%) and **Hide this control** apply to the
chosen one; Pause cannot be hidden, since it is also a cinematic's skip. Each
orientation keeps its own edits, and the preset, a per-control reset and an
orientation reset are offered. Every change is a draft in the setup
preferences, so it survives process death; **Apply layout** commits the layout
with the touch settings in one transaction, or neither.

The engine keeps the applied record in archived `in_touchLayout`
(`1|<landscape>|<portrait>`, nine `x,y,size,opacity,hidden` entries; positions
are whole dp from the window's nearest edges). `ComputeLayout` reserves the
placed controls first where the player put them, then gives the others their
preset places or the nearest free ones, so the 48 dp targets, 8 dp gaps, edge
margins and crosshair clearance hold for every control. A record that cannot
fit the window (split-screen, a larger size) shows the preset instead.
Hidden controls leave the visible set; faded ones draw at their opacity and at
80% while pressed. `TouchLayout.java` ports the placement in the same float
arithmetic for the preview, and a drop is stored as the snapped grid point
when it fits there or as the spot placement chose, so the record always says
what the editor showed.

**Suspension save.** Android may end a backgrounded app without another call,
and SDL blocks the engine thread as soon as the background event is handled.
The background transition therefore writes single player as the `Suspended`
autosave (`com_suspendSave`, default 1): no screenshot, which a backgrounded
window cannot draw; it rolls over each time; and CONTINUE, which resumes the
newest save, finds it after a restart. It is skipped silently where a save
would be refused or would not resume cleanly: multiplayer, loading, demos,
cinematics, death, a failed objective or nearly full storage.

**Network resume.** A multiplayer client now learns how long the app was away.
A stay of at least the 40-second time-out reconnects at once, since the server
has dropped the client; before, the client waited out its own time-out behind
a frozen view. After a shorter stay the backlog queued while away is dropped
and only a packet received after the return proves the connection; without
one within `net_resumeProbe` (5 s) the client reconnects. A failed reconnect
shows the ordinary connection message. `net_resumeReconnect 0` turns this off.
A listen server or a multi-view demo never reconnects.

The first emulator run found why the client never heard of the return:
multiplayer frames run `idSessionLocal::GuiFrameEvents`, never
`idSessionLocal::Frame`, so `HandleAppResume` did not run while connected.
The server's reply to the client's first packet then disconnected it with the
ordinary message box. The same gap left Android's paused OpenAL device paused
for the rest of a match: audio returned only after leaving multiplayer.
`GuiFrameEvents` now handles the return too (the suspension is consumed once),
which resumes the audio. A later run showed a second race: a disconnect the
server queued when it dropped the client was read by the first network frame
after the return. The SDL pump's foreground transition, which runs before any
network processing, now tells the client (`Session_AppForeground`), and the
client drops the queued backlog before reconnecting or probing. A third run
showed the dropped channel's disconnect arriving after the reconnect had
started, ending it during the challenge; for 10 seconds after a resume
reconnect, a disconnect that arrives before the new connection exists is
ignored and logged.

**Quest flavor verification.** `verify_android_apk.py --variant quest` checks
the headset build AND-05 will integrate: its own application ID, a
nondebuggable signature, the phone's seven libraries, the `openq4-headset`
marker, and a manifest requiring head tracking, requesting the OpenXR
permission, opening the game with the immersive OpenXR and Quest categories
and naming supported headsets. Phone and emulator APKs must carry none of
these. No stage or APK may carry the OpenXR test runtime.
`-Dandroid_headset=true` stages the variant; the phone Gradle host refuses
that stage until the flavor's Gradle side lands.

Checks: `android_touch_layout.py` runs the production header and
`TouchLayout.java` over 3,402 window/density/size/preset/record cases and
requires the same placements (0.02 px), flags and exact opacities, the same
verdict on 33 record grammar cases (moved/unmoved pairs, percentages off the
5% step, a hidden Pause, oversize records) and 22,680 editor drops whose stored
records reproduce the shown positions; mutating either side's clearance or
opacity fails it. `openq4-android-touch-controls` adds the edit rules (placed
controls keep their spot, the rest move aside, every rule holds, the preset
fallback is exact, malformed records leave the last good one).
`android_touch_controls.py` drives the production glue: hidden controls leave
the visible set and cannot be pressed, each orientation reads its own edits,
a layout change releases held input, a malformed `in_touchLayout` warns and
uses the preset. `android_touch_setup.py` covers the draft/Apply transaction
(failed commits keep the drafts and the applied records), the Activity
getter and the JNI layout read (grammar, size bound, exceptions, references).
`android_setup_task.py` and `android_host_storage.py` cover the setup page
argument. The retained CONTROLS test checks TOUCH LAYOUT is hidden without
touch controls, shown and focusable with them, below the last binding, and
asks for `controlsTouchLayout`; `ui_retained_gate.py` passes.
`android_lifecycle.py` requires the save before SDL blocks, once per stay, no
save on desktop, the client report from the pump and the multiplayer frame's
resume. `async_client_resume.py` covers the reconnect, probe, stale packets,
exclusions and the stale-disconnect window. `android_build.py` adds the Quest
payload and manifest cases. Windows builds every target.

The emulator probes ran on APK
`f557c695e3e170bd9372059e4f0f76af5f5790656615e17555cc4d71bfcef124` (build
2284) on the AOSP API 35 x86_64 AVD (Android 15, 4 KiB pages, SwiftShader).
`--mode lifecycle` (report SHA-256
`4b780eef8653ac685f507c9b214788152c70aecf2efa46830f71dba645231375`): the
applied record placed Fire at the recorded 0.854, 0.806 of the 1920x1080
window at 125%, hid Jump and faded Zoom to 20%; the edited controls moved the
player 229 u; the Home intent backgrounded the app, which wrote the
6,836,350-byte `Suspended` save; `am kill` ended the backgrounded process
(`USER REQUESTED / KILL BACKGROUND`); the relaunched process loaded the save
with the pose unchanged to 0.1 u and the same edited layout, then exited
normally. `--mode resume` against this build's Windows dedicated server
(reached from the AVD at the WSL host address): after 15 s away the client
reported 14,775 ms, the connection survived and no reconnect or message box
followed (report `de23e59908f1ca90003140f3fa117cf227db2efac670efab89a97fb0776b9745`);
after 50 s it reconnected at once ("away 49 s, beyond the time-out"),
ignored three disconnects from the dropped channel during the challenge and
reloaded Q4DM1 without a message box (report
`03fd4d1757c66c64edf7623c49225299d8a044c6fe79171ec452c0b0d73ce378`). The
editor page opens straight from the game's request intent with its
localized text and the labelled preview of the device's landscape screen.

The WSL emulator build had stamped build 0, because its copied tree had no
`.git`, and every save it wrote failed validation; giving that tree the
worktree's git metadata fixed the build number. This affected only that
local loop. Evidence is under `.tmp/android-evidence/2026-11-09-and01/` in
the main checkout (`lifecycle`, `resume-15`, `resume-50`, the editor capture). Not qualified: physical phones (portrait
play, the editor's touch interaction, suspend/process death/network resume
on a real device and network), Adreno/Mali drivers, and the Quest flavor's
Gradle side and headsets (AND-05). The S25 checks below follow.

### S25 physical checks, 9 November 2026

The test Galaxy S25 (SM-S931B, Android 16/API 36, `sun` platform) received a
debug ARM64 build 2297 of this source over USB. The installed copy had been
signed with an earlier round's debug key (`.tmp/a2/android-user/` in the main
checkout), so the APK was re-signed with that key
(`fad563ec2d9702b6ef43bfffbc57d93591e7ad8034946291f631f570cfc28530`) and
updated in place; existing saves and settings were kept. A non-rooted phone
will not let the shell start the internal game Activity, so the player tapped
setup's play and Return to game buttons; everything else came from engine
diagnostic commands, activity-manager intents and the rotation setting. The
probe preserved and restored the setup preferences, configuration, log and
rotation settings and removed its scripts and the test save.

- **Layout, portrait, suspension save and process death** (report
  `0137347aa771a65ef130628975a9ad200633a8401d1cbbebbc14c481759bd2a8`). In
  landscape (2237x1080 after the cutout, 3 px/dp) the applied record placed
  Fire at the recorded 0.785, 0.667 at 125%, hid Jump and faded Zoom to 20%;
  touch commands moved the player 221 u. Turned upright through
  `user_rotation`, the game continued in portrait (1080x2237) with the thumb
  deck and an unedited portrait layout, as the record leaves it; touch moved
  the player 162 u and released cleanly. Home wrote the 6,835,708-byte
  `Suspended` save, `am kill` ended the backgrounded process
  (`USER REQUESTED / KILL BACKGROUND`), and the relaunched game loaded the save
  with the pose unchanged (0.0 u). The phone's captures show the edited
  landscape overlay and the portrait deck; in portrait the raised weapon
  control overlaps the armour readout, which is AND-04's open "readouts behind
  a raised weapon control" item and occurs at 19.5:9 as well as 9:16. The
  relaunch came back upright rather than in landscape, probably because
  unlocking the phone reapplied its rotation.
- **Multiplayer resume** (report
  `69cb1fcfdd6b80e82b74938461bb492b8e9e1c65a40d3be49fa2fa5d08a91397`). Over
  home Wi-Fi to this build's Windows dedicated server, the S25 joined Q4DM1,
  went home for 57 s and returned: it reconnected at once ("away 57 s, beyond
  the time-out"), ignored three disconnects from the dropped channel, received
  a fresh challenge and reloaded the map with no message box.
- **Layout editor by hand.** The first attempt lost a drag and a hide: tapping
  a control to select it was treated as a drop that pinned it in place. With
  taps limited to selection (a drag must exceed 8 dp) and editor logging, the
  player dragged Fire, stepped its size to 125%, selected and hid Jump and
  applied; the log and the saved preference agree
  (`1|-68,168,125,35,0;*,*,100,35,1;...`), the engine's parser accepts that
  record, and at 125% both implementations hold Fire inside the 24 dp edge
  margin. The player's previous setup was restored afterwards.

Evidence is under `.tmp/android-evidence/2026-11-09-and01/s25/` in the main
checkout. Not covered: human-finger gameplay in portrait (movement used the
engine's touch commands; earlier S25 rounds covered finger move/look/fire in
landscape), audible resume (audio was disabled), a short multiplayer stay on
the phone (covered on the emulator) and Adreno/Mali breadth, which AND-02 and
AND-04 own.

## Android device qualification (S25), 10 November 2026

AND-02's device harness, `tools/tests/android_device_qualification.py`, now
plays stock content on a physical phone without OS input. Setup gains a
debuggable-only `diagnostic_launch` extra: `sp` or `mp` starts the game once
setup's own installed-data check succeeds, and `hold` only keeps the screen on.
Release builds ignore it. `android_setup_task.py` checks that gate in the
production Java: debuggable versus release, the two modes, and that the launch
waits for the data check. Setup now also keeps the screen on while it checks or
extracts data. Before this, a phone's display timeout locked it mid-check and
the run stalled behind the lock screen. The OpenAL backend now logs the
device's negotiated attributes. The S25 and the API 35 emulator both report the
OpenSL device at 48 kHz, with a 93 Hz refresh, 255 mono sources and 1 stereo
source.

**Device:** Galaxy S25 (SM-S931B), Android 16/API 36, SM8750 with Adreno 830,
OpenGL ES 3.2, 16384-texel textures and ETC2/EAC. It has 11 GB of RAM and a
4 KiB page size, and runs 2237x1080 after the cutout. It used debug ARM64 build
2316: APK `0061ccff…efaa5` for the cold run and `11106cdf…5ba179` (the
keep-screen-on change) for the warm run. Audio was on, using Android's own
OpenAL Soft 1.25.2 through OpenSL in basic stereo. Both campaign runs
preserved and restored the player's configuration and log, removed every
script, save and capture they made, and left the existing saves untouched.

**Campaign** (stock single player with sound): `hangar1`, `hangar2` and
`mcc_landing` were played through each level's own end trigger, with inventory
carried. Then `airdefense1`, `convoy1` and `process1` were loaded fresh. Each
stage ran a 1,200-frame trace and a 15 s view sweep. Both runs completed with
no fatal error and no low-memory event. The `process1` save reloaded in under
0.7 s with the pose unchanged (0.0 u).

| Stage | Load cold / warm | Mean fps | p99 cold / warm |
| --- | --- | --- | --- |
| hangar1 | 11.6 / 14.6 s | 60 | 23.0 / 21.6 ms |
| hangar2 (trigger) | 7.4 / 7.6 s | 53 | 64.2 / 64.5 ms |
| mcc_landing (trigger) | 14.7 / 12.6 s | 60 | 22.3 / 22.6 ms |
| airdefense1 | 11.3 / 12.3 s | 60 | 21.2 / 22.8 ms |
| convoy1 | 13.7 / 13.1 s | 60 | 27.8 / 28.2 ms |
| process1 | — / 15.0 s | 27 | 82.0 / 82.1 ms |

- **Cold vs warm cache.** The cold run moved the generated image cache aside
  and rebuilt 93 MB of it. The warm run used the player's own cache, which it
  extended from 85 MB to 127 MB. Load times stay within about 25% either way,
  so image-cache generation is not the main load cost on this phone.
- **Frame rate.** Frame rates are identical between the runs. Most stages hold
  the 60 Hz cap. `hangar2`'s sweep crosses a costly view. `process1` is
  render-bound at 27 fps, with draw work around 31 ms; that belongs to AND-04's
  preset scope, not to correctness.
- **Thermal.** Thermal status stayed at 0 (none), with the SoC at most 49.9 °C
  and the battery at 32.2 °C.
- **Memory.** App PSS peaked at 2.83 GB.
- **Audio residency.** OpenAL held all decoded PCM, with no CPU-side PCM copy
  retained. The peak was 171 MiB of PCM beside 81 MiB of compressed streams on
  `mcc_landing`. That is 253 MiB of sample data, just under the 256 MiB audio
  budget, and is the stage to watch.
- **Warnings.** The 366 log warnings per run are mostly uppercase asset paths
  that don't match the files' case on portable filesystems.

The per-model limits in
[`android-device-budgets.json`](android-device-budgets.json) are these
measurements plus headroom, and both runs pass them.

**Sustain and multiplayer (not finished):** the ten-minute sustain phase,
including its memory-trim request, and the Q4DM1 listen server with bots were
under way when the player disconnected the phone. The run was stopped and the
phone restored: scripts removed, configuration checksum and log restored, no
new saves left, app stopped. Both phases ran on the emulator only.

Evidence (reports, logs, captures, traces and APK hashes) is under
`.tmp/android-evidence/2026-11-10-and02/` in the main checkout. Still open for AND-02:

- sustained thermal and frame budgets, the trim-memory reaction and
  multiplayer on the phone;
- an audible check and controller, mouse and keyboard input, which need the
  player;
- a Mali GPU and an API 24-floor device, which are unavailable for now.
