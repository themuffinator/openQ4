# Shared Render-Demo and Raven Special-Frame Transaction

## Status

This Milestone D corridor is **Implemented and qualified for individual
promotion (default-off; REN-01 runtime qualification on Windows OpenGL/Vulkan
recorded 13 November 2026; non-Windows platforms and human appearance review
belong to platform qualification)**; see
[REN-01 qualification](#ren-01-qualification-13-november-2026).
`r_rendererSharedSpecialFrame 1` gives OpenGL and Vulkan the same sealed
ownership boundary for two otherwise exceptional classic-frame paths:

- a complete 3D frame being played from the active render-demo session stream;
  and
- an exact `RC_DRAW_SPECIAL_EFFECTS` Raven controller command, including its
  later blur composite and projected AL-light work.

The implementation is original openQ4 work. It incorporates no external source
code and changes no stock Quake 4 asset.

## Transaction boundary

A negative `viewID` is not sufficient evidence of render-demo playback: the
portal-sky camera uses that convention too. Front-end scene packets therefore
mark a render-demo view only when the live `session->readDemo` stream owns the
same render world. The record then seals the ordinary root-view identity,
demo-version state, full source surface count, exact packet associations, and
depth, interaction, and ambient pass presence.

Raven special effects are command-only work. Their packet preserves the exact
controller view and the admitted `SPECIAL_EFFECT_BLUR` / `SPECIAL_EFFECT_AL`
mask from `RC_DRAW_SPECIAL_EFFECTS`. The visible work can finish later: OpenGL
reports the blur composite and actual AL lights after their established paths
complete; Vulkan reports its normal or resolve-backed completion after its
pending controller is consumed. Both bits must complete before the record is
owned.

Neither route replaces any mature rendering behavior. A render-demo record
dispatches the complete normal 3D view executor. Special effects retain their
normal renderer-resource preparation, depth/color/resolve handling, blur, and
projected-light paths. The shared transaction seals provenance and completion;
it is not a second demo decoder or a replacement BSE/effect renderer.

## Eligibility and fallback

Render-demo ownership accepts only a complete ordinary root 3D view from the
active session stream. Portal-sky, subview, mirror, x-ray, editor, clip-plane,
and malformed/partial packet cases are rejected. Special-frame ownership accepts
only the exact normal-root Raven controller command with a nonempty admitted
mask. Packet overflow, source mismatch, stale/missing session state,
unsupported view shape, incomplete effect output, resolve/capture state, or a
backend rejection leaves the whole item on the classic fallback path.

Ownership is reported only after complete coverage: a demo must reconcile its
entire source-surface count, while Raven blur and AL coverage are accumulated
until they match the sealed mask. An unreported record is finalized as a named
classic fallback, so a partial shared report can never silently hide an
unfinished special effect.

## Control and diagnostics

`r_rendererSharedSpecialFrame` is independent of GUI, cinematic/post, world
ambient, interaction, fog/blend, subview, and deform ownership:

- `0` (default): render-demo and Raven special-frame work use their normal
  backend paths;
- `1`: an eligible sealed transaction may report OpenGL/Vulkan ownership after
  the same complete executor finishes; all other work remains classic-owned.

`gfxInfo` reports source scenes, demo/effect records, admitted effect mask,
ready/fallback views, semantic hash, and per-backend ownership, fallback,
coverage-mismatch, and duplicate-report totals. Focused dependency-light
validation is:

```text
rendererScenePacketSelfTest
rendererClassicSpecialFrameDomainSelfTest
gfxInfo
```

`tools/tests/renderer_classic_special_frame_domain.py` guards exact session
provenance, portal-sky exclusion, controller-mask capture, source validation,
OpenGL/Vulkan completion handoff, conservative defaults, release-harness
isolation, diagnostics, and CI registration.

## Release-qualification boundary

Before promotion, retain engine-written `screenshot` captures—not OS
captures—for a stock render-demo and a stock Raven blur/AL scene with the
setting off and on, separately on OpenGL and Vulkan. Require nonzero reconciled
ownership where the content is eligible, exact same-settings output, an
incomplete-mask zero-commit fallback, clean backend diagnostics, and fresh
staged-package and target-platform/driver evidence.

The special-subview transaction now covers direct mirror/clip-plane and
dynamic mirror/reflection/x-ray handoffs; see [Shared Special-Subview
Transaction](classic-subview-domain-modernization.md). Its eligible nested trees
also couple separately sealed authored-post tails to the same atomic root
completion and rollback decision.

Together with the other independently guarded shared corridors, this completes
Milestone D's scoped implementation. It does not make every classic-frame
category or the aggregate modern-visible renderer shared-owned.

## REN-01 qualification, 13 November 2026

The [shared-domain qualification harness](renderer-validation-matrix.md#shared-classic-domain-qualification-ren-01)
owns both forms on OpenGL and Vulkan, under the default preset and with the
post chain off:

- **Raven blur.** Stock content drives Raven's renderer controller only from
  the multiplayer game (its join screen and Arena depth of field);
  single-player scripts route `setSpecialEffect` to the game's own blur post
  material instead. The harness therefore drives the same controller with
  `r_forceSpecialEffects 1` over a frozen `game/airdefense1` view, which needs
  no listen server. The controller command is owned with complete blur
  coverage and the frame is byte-identical to the classic one; turning the
  effect off is the visible control.
- **Render-demo playback.** A demo recorded from the frozen, lit
  `maps/tools/mv2` floor plays back with every frame owned and byte-identical
  to classic playback on OpenGL and Vulkan, under the default preset, with 4x
  MSAA and with the post chain off. Classic OpenGL playback under the default
  preset was not stable at first: playback draws through the renderer's own
  scene target, which cleared only depth and stencil, so the void above the
  horizon kept each frame's bloom and brightened frame by frame. That target
  now starts every frame cleared to the window's `r_clear` color.

The rollback is the qualification diagnostic `r_rendererSharedFaultInject 2`:
both backends run the complete classic executor and record a named fallback.
Raven ownership is reported after the classic controller finishes, and a
command that already took its fallback used to be counted a coverage mismatch
on OpenGL when the later blur report arrived, while Vulkan recorded no
fallback at all; both backends now record one named fallback and never report
that command owned. Human appearance review and non-Windows platforms belong
to platform qualification.
