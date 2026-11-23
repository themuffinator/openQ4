# Shadowed volumetric lighting (REN-09)

Per-light shadowed scattering through a homogeneous haze, on OpenGL and
Vulkan. Default off (`r_rendererFroxelVolumetrics 0`), with exact rollback.
The player guide is
[advanced screen-space lighting](../user/advanced-screen-space-lighting.md#volumetric-lighting);
the plan entry is REN-09 in the [roadmap](roadmap.md#modern-rendering-engine).
This document is the evidence for it. The design summary is in the roadmap
entry and in `src/renderer/VolumetricLighting.h`.

Status: **Partial**. The engineering and the Windows OpenGL/Vulkan
qualification below are done. Open: the per-tier cost on a quiet reference
machine against the FPS-01 budgets, the owner's visual review of the defaults,
and the physical Linux, macOS and Android runs (platform qualification).

## Harnesses

All are opt-in, local and hardware-bound. They drive the staged client with
console commands, `setviewpos`, `benchmarkViewSweep` and spawned stock
entities; none synthesizes input.

| Harness | Covers |
|---|---|
| `tools/tests/renderer_volumetric_lighting.py` | Frozen stock views (Air Defense 1 outdoors and corridor, Process 1): effect, exact master rollback, status, GPU timing sample. Passed 21 November 2026, also with TAA and 4x MSAA; passed again on 23 November on the changed engine (six of six on OpenGL and Vulkan, and the outdoor view with 4x MSAA). |
| `tools/tests/renderer_volumetric_motion.py` | Moving casters and a moving flashlight, temporal history, resolution scale and dynamic resolution, mirrors and remote cameras, and a backend decline. |
| `tools/tests/openxr_vr_volumetric_smoke.py` | VR stereo on the OpenXR test runtime. |
| `tools/tests/renderer_volumetric_tiers.py` | The per-tier cost ladder (`--mode cost`) and the tuning candidates with a contact sheet per scene (`--mode tuning`). |

```text
python tools/tests/renderer_volumetric_motion.py --runtime-dir .install
python tools/tests/openxr_vr_volumetric_smoke.py --basepath <Quake 4> --build-dir <build> --render-api gl
python tools/tests/renderer_volumetric_tiers.py --mode cost --runtime-dir .install
python tools/tests/renderer_volumetric_tiers.py --mode tuning --runtime-dir .install
```

The launcher of the motion and tier harnesses relaunches a client whose log
stops growing before the script's first capture. On a machine shared with
other renderer work a start-up can take minutes; a relaunch is never counted
as a result.

## What changed on 23 November 2026

- **Scratch targets are pooled by view size.** Each backend kept one depth
  copy and one scattering target and resized them for every view. With mirrors
  or remote cameras in sight the views of one frame differ in size (the main
  view, 256x256 captures), so both were reallocated several times a frame: the
  motion harness counted 204 allocations over 34 frozen frames in the
  validation room with four subviews. Each backend now keeps four pairs and a
  view takes the pair of its own scene size and resolution divisor, the least
  recently used pair otherwise (`R_Volumetric_ChooseTargetSlot`). The same
  rooms now allocate nothing while the views hold still. Dynamic resolution
  gets the same benefit: a scale it has visited in its last four keeps its
  targets.
- **Status counts views.** `rendererVolumetricStatus` reports `views` (views
  that scattered and composited this frame), `subviews` (of those, mirrors,
  remote cameras and other subviews) and `viewsDeclined`, on both backends
  alike, and the running `targetAllocations`. OpenGL used to report a declined
  subview nowhere and Vulkan counted it as a view.
- **`r_volumetricDebug 2`.** The scattered light alone with no shadow term,
  from the same lights in the same order as `1`. It is the reference the
  shadow maps are judged against: shadowed scattering can never be brighter.

No default changed. With the leaf off the frame is unchanged
(`base_again` identical to `base` in every case below).

## Motion, scale, subviews and decline, 23 November 2026

`renderer_volumetric_motion.py`, 1280x720, hidden window, `com_fixedTic 1`,
the Quality shadow configuration (`r_useShadowMap 1`, `r_shadowMapCSM 1`,
`r_shadowMapPointLights 1`), worktree build of `d30280aa2` plus this change.
All five cases pass on OpenGL and Vulkan.

### Moving casters and a moving flashlight (`caster`)

Air Defense 2, the start corridor. A stock door pair is spawned across it
with a spawned shadow-casting light behind it, and the camera carries the
flashlight (`testFlashlight 1`). The camera pans 540 degrees in four seconds
through each door movement; at each paused state the scattered light alone is
captured from the motion-warmed shadow caches, from fresh maps
(`r_shadowMapStaticCache 0`) and unshadowed (`r_volumetricDebug 2`).

| | OpenGL | Vulkan |
|---|---:|---:|
| Lights scattering, closed (shadow-mapped / unshadowed) | 11 / 6 | 11 / 6 |
| Lights scattering while panning with the doors moving | 14 (9 mapped) | 13 (8 mapped) |
| Scattered light, closed / open / reclosed (mean level) | 45.57 / 41.37 / 43.50 | 45.56 / 41.37 / 43.50 |
| Unshadowed reference, closed / open / reclosed | 108.68 / 107.76 / 108.25 | 108.68 / 107.76 / 108.24 |
| Cached against fresh maps, pixels differing by more than 2 levels | 0 / 0 / 0.02% | 0 / 0 / 0 |
| Shadowed brighter than unshadowed (leak) | 0 | 0 |
| Pixels the doors move the scattered light in by more than 16 levels | 8.1% | 8.1% |
| There: closed to open / closed to reclosed (mean levels) | 39.77 / 10.29 | 39.74 / 10.31 |

The shadow in the medium follows the doors and returns with them; nothing
stale is left from the motion; no pixel scatters more shadowed than
unshadowed. The backends agree to 0.01 levels. The residual 10.3 between the
closed and reclosed frames is the fixture, not the shadow: the carried
flashlight sways with the idle weapon, and stock lights and GUIs keep running
between the states (the leaf-off frames differ by 1.0 level on their own).

Two things about the fixture cost time and are fixed in the harness. The
start corridor's own door (`func_door_274`/`275`) is opened once by a
`trigger_once` at the player start, so a capture taken in the first seconds
shows it mid-swing; the harness opens it and waits. And the flashlight keeps
swinging behind the view for a few tics after a pan stops, so the pose
settles for 40 tics before the freeze.

### Temporal history (`smear`)

The same doors under `r_temporalAA 1`, frozen halfway through opening. The
resolved history is read back 1 to 16 frames after the freeze and once
settled. Residual is the mean level difference from the settled history.

| Frames after the freeze | 1 | 2 | 4 | 8 | 16 | settled frame to frame |
|---|---:|---:|---:|---:|---:|---:|
| OpenGL, leaf off | 0.368 | 0.299 | 0.240 | 0.154 | 0.106 | 0.121 |
| OpenGL, leaf on | 0.432 | 0.345 | 0.234 | 0.133 | 0.089 | 0.113 |
| Vulkan, leaf off | 0.434 | 0.344 | 0.261 | 0.150 | 0.105 | 0.121 |
| Vulkan, leaf on | 0.521 | 0.405 | 0.273 | 0.138 | 0.094 | 0.115 |

With the leaf the history holds a brighter frame (mean 66.6 against 26.3)
and clears at least as fast: after 16 frames 21% (OpenGL) and 18% (Vulkan) of
the first frame's residual is left, against 29% and 24% without the leaf, and
both are at the settled frame-to-frame level. The moving shaft leaves no
trail of its own.

### Resolution scale and dynamic resolution (`scale`)

Air Defense 1 outdoors, `r_temporalAA 2`, `r_resolutionScaleMode 1`.

| Step | Scene | Scattering target | Effect, OpenGL / Vulkan (mean level) |
|---|---|---|---:|
| Native | 1280x720 | 640x360 | 11.75 / 11.32 |
| `r_screenFraction 50` | 640x360 | 320x180 | 12.03 / 11.67 |
| Dynamic resolution forced down | 640x360 | 320x180 | 12.03 / 11.67 |
| Dynamic resolution released | 1280x720 | 640x360 | 11.75 / 11.33 |
| Native again | 1280x720 | 640x360 | identical to the first |

The target follows the scene at half resolution at every step, the leaf
stays effective, the native frame after the scale changes is byte-identical
to the one before, and so is the leaf-off frame. Stepping dynamic resolution
from 100% to 50% allocated ten target pairs, one per scale it passed; coming
back allocated one.

### Mirrors and remote cameras (`subview`)

The REN-01 validation rooms
([shared subview domain](classic-subview-domain-modernization.md#ren-01-qualification-13-november-2026)):
room A has a reflection, a capture-backed mirror, a remote camera and a
direct mirror side by side; room B a direct mirror facing a capture-backed
mirror.

| | Room A, OpenGL | Room A, Vulkan | Room B, OpenGL | Room B, Vulkan |
|---|---:|---:|---:|---:|
| Views scattering with `r_volumetricSubviews 1` (subviews) | 5 (4) | 5 (4) | 3 (2) | 3 (2) |
| With `r_volumetricSubviews 0` (declined) | 1 (4) | 1 (4) | 1 (2) | 1 (2) |
| Picture changed inside the subviews | 9.6% of pixels | 6.8% | 6.7% | not comparable |
| Target allocations over 30 frozen frames | 0 | 0 | 0 | 0 |
| Leaf off afterwards | identical | identical | identical | identical |

Every subview scatters, `r_volumetricSubviews 0` leaves them clear and says
so, and the scratch targets hold still. In room B on Vulkan the direct
mirror shows no reflection with the leaf off either (mean 1.7 inside it
against 17.6 on OpenGL, with shadow maps on or off), so there is no picture
inside it for the leaf to change: the harness records that as a note on the
baseline. It is a Vulkan direct-mirror matter outside this leaf and is
listed under follow-ups.

### Decline (`decline`)

With `r_skipPostProcess 1` both backends report the leaf requested, not
effective, with `decline=post-process-skipped`, and the frame is
byte-identical to the frame without the leaf. The GLSL 1.20 (Apple GL 2.1)
and Apple corridor reasons decline through the same function
(`RB_Volumetric_GL_BackendDecline`), and GLES never reaches the leaf; no
driver on this machine can be made to report them (the engine refuses to
start without fragment programs), so those reasons stay with the physical
platform runs.

## VR stereo, 23 November 2026

`openxr_vr_volumetric_smoke.py` on the OpenXR test runtime, 800x880 per eye,
Air Defense 1, game time stopped, Quality shadow configuration.

| | OpenGL | Vulkan |
|---|---:|---:|
| Main views scattering a frame (subviews, declined) | 2 (0, 0) | 2 (0, 0) |
| Shadow-mapped light passes over both eyes | 12 | 12 |
| Scattering target | 400x440 | 400x440 |
| Change from the leaf, left / right eye (mean level) | 13.32 / 11.03 | 15.18 / 12.79 |
| Scattered light alone, left / right | 16.12 / 14.93 | 19.68 / 18.07 |
| Eye parallax with the leaf / without | 23.69 / 20.15 | 25.51 / 20.01 |
| Parallax of the scattered light alone | 12.30 | 14.82 |
| Target allocations over 60 frames | 0 | 0 |
| Leaf off afterwards | both eyes identical | both eyes identical |

Each eye is a view of its own and scatters through its own depth and
projection; the eyes keep their parallax, and the leaf off restores both
byte for byte. The two backends were launched separately and stop game time
at different moments of a scene with smoke and aircraft, so their levels are
not comparable with each other (the frozen cases above are).

## Tuning candidates, awaiting review

`renderer_volumetric_tiers.py --mode tuning`, OpenGL, 1280x720, leaf-off
frame beside each candidate, one contact sheet per scene
(`<output>/tuning/gl-<scene>/tuning.png`; the fourth scene is
`--scenes tram`, whose tram keeps moving under `g_stopTime`, so its
leaf-off frames are not identical). Added brightness is in 8-bit levels over
the leaf-off frame.

| Scene | Candidate | Mean added | Median | 99th percentile | Pixels over +8 | Pixels over +32 |
|---|---|---:|---:|---:|---:|---:|
| Air Defense 1 outdoors | default | 9.09 | 2.25 | 87.00 | 25.5% | 10.7% |
| Air Defense 1 outdoors | intensity 3 | -1.97 | 0.25 | 18.50 | 5.1% | 0.0% |
| Air Defense 1 outdoors | intensity 6 | 1.64 | 1.00 | 39.25 | 15.7% | 2.1% |
| Air Defense 1 outdoors | intensity 24 | 22.04 | 5.75 | 162.75 | 41.0% | 21.6% |
| Air Defense 1 outdoors | sun 0.04 | 7.45 | 1.75 | 79.75 | 23.6% | 8.7% |
| Air Defense 1 outdoors | sun 0.16 | 12.40 | 3.25 | 114.25 | 29.9% | 15.4% |
| Air Defense 1 outdoors | anisotropy 0.6 | 4.58 | 1.00 | 71.25 | 19.2% | 6.3% |
| Air Defense 1 outdoors | stencil lights | 9.09 | 2.25 | 87.00 | 25.5% | 10.7% |
| Process 1 | default | 2.52 | 0.00 | 62.00 | 6.1% | 1.6% |
| Process 1 | intensity 3 | 0.30 | 0.00 | 13.50 | 1.2% | 0.5% |
| Process 1 | intensity 6 | 1.01 | 0.00 | 28.75 | 3.5% | 1.0% |
| Process 1 | intensity 24 | 5.07 | 0.00 | 112.25 | 10.4% | 4.2% |
| Process 1 | sun 0.04 | 2.49 | 0.00 | 61.50 | 6.1% | 1.5% |
| Process 1 | sun 0.16 | 2.58 | 0.00 | 62.50 | 6.2% | 1.8% |
| Process 1 | anisotropy 0.6 | 2.03 | 0.00 | 48.75 | 5.0% | 1.1% |
| Process 1 | stencil lights | 2.52 | 0.00 | 62.00 | 6.1% | 1.6% |
| Air Defense 2 corridor, flashlight | default | 38.63 | 31.50 | 138.00 | 99.9% | 47.8% |
| Air Defense 2 corridor, flashlight | intensity 3 | 9.41 | 7.50 | 40.50 | 40.3% | 2.4% |
| Air Defense 2 corridor, flashlight | intensity 6 | 19.35 | 15.25 | 82.25 | 98.6% | 9.2% |
| Air Defense 2 corridor, flashlight | intensity 24 | 74.98 | 64.75 | 193.50 | 100.0% | 99.2% |
| Air Defense 2 corridor, flashlight | sun 0.04 | 38.63 | 31.50 | 138.00 | 99.9% | 47.8% |
| Air Defense 2 corridor, flashlight | sun 0.16 | 38.63 | 31.50 | 138.00 | 99.9% | 47.8% |
| Air Defense 2 corridor, flashlight | anisotropy 0.6 | 29.62 | 19.75 | 127.25 | 99.5% | 25.8% |
| Air Defense 2 corridor, flashlight | stencil lights | 38.63 | 31.50 | 138.00 | 99.9% | 47.8% |
| Tram 1 opening view | default | 66.41 | 80.00 | 116.75 | 92.6% | 72.2% |
| Tram 1 opening view | intensity 3 | 21.48 | 22.25 | 54.25 | 72.3% | 22.1% |
| Tram 1 opening view | intensity 6 | 41.34 | 43.25 | 92.75 | 81.4% | 65.0% |
| Tram 1 opening view | intensity 24 | 84.70 | 104.25 | 129.25 | 98.3% | 83.1% |
| Tram 1 opening view | sun 0.04 | 66.42 | 80.00 | 116.75 | 92.7% | 72.2% |
| Tram 1 opening view | sun 0.16 | 66.40 | 80.00 | 116.50 | 92.7% | 72.2% |
| Tram 1 opening view | anisotropy 0.6 | 57.19 | 54.00 | 123.25 | 89.7% | 70.2% |
| Tram 1 opening view | stencil lights | 66.36 | 80.00 | 116.50 | 92.6% | 72.2% |

What the captures show:

- **Outdoors** the default reads as a light haze over the distance with the
  sun's haze following the buildings, and a glow around the lamp on the
  bunker.
- **The Process 1 view** barely changes at the default: the room is dark and
  its lights are small.
- **The busy Air Defense 2 corridor** (17 lights scattering, 11 shadow-mapped)
  is lifted as a whole: 31 levels at the median and more than 8 levels in
  99.9% of pixels. Many overlapping room lights each fill their own volume,
  so the haze reads as a grey wash over the room rather than as beams, and
  the shafts through the door are the only contrast.

- **The Tram 1 opening view** is lost at the default: a wide shaft under
  bright lights turns into a milky wall, 80 levels at the median on OpenGL and
  86 on Vulkan. `r_volumetricIntensity 3` is the first candidate at which the
  shaft reads (22 at the median). The REN-05/06 session saw the same view
  change by 151 levels on Vulkan in its own run.
- **Lower intensities.** `r_volumetricIntensity 6` halves the corridor's lift
  (15 levels at the median) and 3 brings it to 7.5. Outdoors at 3 the picture
  gets darker on average (-2.0): the haze dims the bright distance by more
  than it scatters back.

No default was changed. The same `r_volumetricIntensity` is faint in one
stock room, heavy in another and a white-out in a third, and the candidates
pull the views apart: 3 rescues the tram shaft and the busy corridor, leaves
Process 1 without any haze and turns the outdoors into a dimming filter unless
`r_volumetricSunIntensity` rises with it. So the choice is not one number.
The evidence points at 12 being too high for stock interiors with large or
many lights, and at the model wanting a bound on what one large light can add
along a long view ray; which way to take it is a visual decision for the
owner. The stencil-light candidate equals the
default in all three views because every shadow-casting light in them has a
map under the Quality configuration.

## Cost per tier, shared machine

`renderer_volumetric_tiers.py --mode cost`. Whole-frame delayed GPU time
(`r_rendererGpuTimers 1`), median of nine samples per tier, frozen view, cost
over the mean of three leaf-off medians taken before, between and after the
tiers. The window was asked for 2560x1440; its client area, and so the
scene, came out at 2538x1312.

**These are shared-machine numbers and are not budget evidence.** They were
taken in a hidden window while other sessions were building and running
clients on the same RTX 4060 Laptop GPU; the leaf-off frame itself drifted by
the amount in the "leaf off" rows while nothing changed. The FPS-01 budgets
are warm means of the performance suite at 2560x1440 fullscreen on a quiet
machine; the gate for REN-09 stays open until the tiers are run that way.

| Backend | Scene | Tier | Frame ms | Cost ms | Sample spread ms | Scattering target | Lights in view | Passes |
|---|---|---|---:|---:|---:|---|---:|---:|
| OpenGL | Air Defense 1 outdoors | leaf off (2538x1312) | 6.67 | - | 1.29 (drift) | - | - | - |
| OpenGL | Air Defense 1 outdoors | half-8 | 7.58 | +0.90 | 2.15 | 1269x656 | 25 | 25 |
| OpenGL | Air Defense 1 outdoors | half-12 (default) | 7.63 | +0.96 | 1.33 | 1269x656 | 25 | 25 |
| OpenGL | Air Defense 1 outdoors | half-16 | 9.15 | +2.47 | 4.18 | 1269x656 | 25 | 25 |
| OpenGL | Air Defense 1 outdoors | full-8 | 10.84 | +4.17 | 4.00 | 2538x1312 | 25 | 25 |
| OpenGL | Air Defense 1 outdoors | full-12 | 10.38 | +3.70 | 2.57 | 2538x1312 | 25 | 25 |
| OpenGL | Air Defense 1 outdoors | full-16 | 10.72 | +4.05 | 2.58 | 2538x1312 | 25 | 25 |
| OpenGL | Air Defense 1 outdoors | half-12, 4 lights | 7.62 | +0.95 | 6.86 | 1269x656 | 25 | 4 |
| OpenGL | Air Defense 1 outdoors | half-12, 8 lights | 8.10 | +1.43 | 3.37 | 1269x656 | 25 | 8 |
| OpenGL | Process 1 | leaf off (2538x1312) | 5.11 | - | 0.82 (drift) | - | - | - |
| OpenGL | Process 1 | half-8 | 6.64 | +1.53 | 2.05 | 1269x656 | 13 | 13 |
| OpenGL | Process 1 | half-12 (default) | 6.06 | +0.95 | 1.80 | 1269x656 | 13 | 13 |
| OpenGL | Process 1 | half-16 | 6.44 | +1.33 | 2.75 | 1269x656 | 13 | 13 |
| OpenGL | Process 1 | full-8 | 6.77 | +1.65 | 1.35 | 2538x1312 | 13 | 13 |
| OpenGL | Process 1 | full-12 | 7.41 | +2.29 | 1.33 | 2538x1312 | 13 | 13 |
| OpenGL | Process 1 | full-16 | 7.80 | +2.69 | 1.87 | 2538x1312 | 13 | 13 |
| OpenGL | Process 1 | half-12, 4 lights | 5.61 | +0.50 | 1.82 | 1269x656 | 13 | 4 |
| OpenGL | Process 1 | half-12, 8 lights | 6.08 | +0.97 | 1.12 | 1269x656 | 13 | 8 |
| OpenGL | Air Defense 2 corridor, flashlight | leaf off (2538x1312) | 6.69 | - | 4.97 (drift) | - | - | - |
| OpenGL | Air Defense 2 corridor, flashlight | half-8 | 6.37 | -0.32 | 0.77 | 1269x656 | 16 | 16 |
| OpenGL | Air Defense 2 corridor, flashlight | half-12 (default) | 6.55 | -0.14 | 0.64 | 1269x656 | 16 | 16 |
| OpenGL | Air Defense 2 corridor, flashlight | half-16 | 6.64 | -0.05 | 1.11 | 1269x656 | 16 | 16 |
| OpenGL | Air Defense 2 corridor, flashlight | full-8 | 7.62 | +0.94 | 1.61 | 2538x1312 | 16 | 16 |
| OpenGL | Air Defense 2 corridor, flashlight | full-12 | 8.00 | +1.32 | 0.74 | 2538x1312 | 16 | 16 |
| OpenGL | Air Defense 2 corridor, flashlight | full-16 | 8.17 | +1.48 | 1.08 | 2538x1312 | 16 | 16 |
| OpenGL | Air Defense 2 corridor, flashlight | half-12, 4 lights | 6.50 | -0.19 | 2.38 | 1269x656 | 16 | 4 |
| OpenGL | Air Defense 2 corridor, flashlight | half-12, 8 lights | 6.82 | +0.13 | 5.10 | 1269x656 | 16 | 8 |
| Vulkan | Air Defense 1 outdoors | leaf off (2538x1312) | 4.54 | - | 0.19 (drift) | - | - | - |
| Vulkan | Air Defense 1 outdoors | half-8 | 4.76 | +0.22 | 1.90 | 1269x656 | 25 | 25 |
| Vulkan | Air Defense 1 outdoors | half-12 (default) | 4.70 | +0.16 | 0.01 | 1269x656 | 25 | 25 |
| Vulkan | Air Defense 1 outdoors | half-16 | 4.88 | +0.33 | 0.03 | 1269x656 | 25 | 25 |
| Vulkan | Air Defense 1 outdoors | full-8 | 5.80 | +1.25 | 0.61 | 2538x1312 | 25 | 25 |
| Vulkan | Air Defense 1 outdoors | full-12 | 7.20 | +2.66 | 0.81 | 2538x1312 | 25 | 25 |
| Vulkan | Air Defense 1 outdoors | full-16 | 8.02 | +3.47 | 1.40 | 2538x1312 | 25 | 25 |
| Vulkan | Air Defense 1 outdoors | half-12, 4 lights | 4.96 | +0.42 | 1.01 | 1269x656 | 25 | 4 |
| Vulkan | Air Defense 1 outdoors | half-12, 8 lights | 5.31 | +0.77 | 1.50 | 1269x656 | 25 | 8 |
| Vulkan | Process 1 | leaf off (2538x1312) | 3.55 | - | 0.18 (drift) | - | - | - |
| Vulkan | Process 1 | half-8 | 4.29 | +0.73 | 0.13 | 1269x656 | 13 | 13 |
| Vulkan | Process 1 | half-12 (default) | 4.29 | +0.73 | 0.01 | 1269x656 | 13 | 13 |
| Vulkan | Process 1 | half-16 | 4.43 | +0.87 | 0.64 | 1269x656 | 13 | 13 |
| Vulkan | Process 1 | full-8 | 4.36 | +0.80 | 0.34 | 2538x1312 | 13 | 13 |
| Vulkan | Process 1 | full-12 | 4.58 | +1.02 | 0.81 | 2538x1312 | 13 | 13 |
| Vulkan | Process 1 | full-16 | 5.56 | +2.01 | 2.32 | 2538x1312 | 13 | 13 |
| Vulkan | Process 1 | half-12, 4 lights | 7.05 | +3.50 | 5.06 | 1269x656 | 13 | 4 |
| Vulkan | Process 1 | half-12, 8 lights | 6.38 | +2.82 | 5.35 | 1269x656 | 13 | 8 |
| Vulkan | Air Defense 2 corridor, flashlight | leaf off (2538x1312) | 6.62 | - | 3.18 (drift) | - | - | - |
| Vulkan | Air Defense 2 corridor, flashlight | half-8 | 7.52 | +0.90 | 4.98 | 1269x656 | 16 | 16 |
| Vulkan | Air Defense 2 corridor, flashlight | half-12 (default) | 7.57 | +0.95 | 3.12 | 1269x656 | 16 | 16 |
| Vulkan | Air Defense 2 corridor, flashlight | half-16 | 6.58 | -0.04 | 4.83 | 1269x656 | 16 | 16 |
| Vulkan | Air Defense 2 corridor, flashlight | full-8 | 9.38 | +2.76 | 5.27 | 2538x1312 | 16 | 16 |
| Vulkan | Air Defense 2 corridor, flashlight | full-12 | 10.52 | +3.89 | 3.30 | 2538x1312 | 16 | 16 |
| Vulkan | Air Defense 2 corridor, flashlight | full-16 | 9.45 | +2.83 | 3.75 | 2538x1312 | 16 | 16 |
| Vulkan | Air Defense 2 corridor, flashlight | half-12, 4 lights | 5.45 | -1.18 | 0.42 | 1269x656 | 15 | 4 |
| Vulkan | Air Defense 2 corridor, flashlight | half-12, 8 lights | 5.83 | -0.80 | 0.25 | 1269x656 | 16 | 8 |

Read with the drift in mind. The two runs whose leaf-off frame held within
0.2 ms (Vulkan outdoors and Process 1) put the default half-resolution tier
at +0.2 to +0.7 ms and full resolution at +0.8 to +3.5 ms. In the Air Defense
2 rows the leaf-off frame drifted by more than the tiers cost, and a light
cap that removes passes reads as slower than no cap, which only the other
load on the machine explains.

For scale, the FPS-01 Ultra budgets are 6.70 ms (OpenGL) and 7.65 ms
(Vulkan) for `outdoor_airdefense1`, and 9.36 to 10.97 ms for the indoor
scenarios ([performance-budgets.json](performance-budgets.json)).

## Open

- **Cost on a quiet machine.** Run the tier ladder, and the performance
  suite with the leaf on, on the idle reference machine at 2560x1440
  fullscreen and price each tier against the FPS-01 budgets before any
  preset enables the leaf.
- **Visual review of the defaults** by the owner, from the contact sheets or
  in play: `r_volumetricIntensity 12`, `r_volumetricSunIntensity 0.08`,
  `r_volumetricAnisotropy 0.35`, `r_froxelVolumetricDensity 0.00012`. The
  Tram 1 opening view is the case to settle first.
- **Physical platforms** (platform qualification): Linux and macOS OpenGL and
  Vulkan, Apple GL 2.1 and GLES declines, a headset.

## Follow-ups outside the leaf

- **Vulkan direct mirror.** In the REN-01 validation rooms the direct mirror
  shows no reflection on Vulkan under the default presentation (leaf off,
  shadow maps on or off), while OpenGL shows the room. Found by the subview
  case; not caused by the leaf.
