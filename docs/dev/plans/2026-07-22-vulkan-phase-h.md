# Vulkan Phase H — render-texture infra, captures, post-process, MSAA, readback (staging plan)

Status: recon complete (docs/dev/plans/phase-h-recon/ — READ THOSE FIRST);
H1 staged.
Parent: [2026-07-16-vulkan-renderer.md](2026-07-16-vulkan-renderer.md) Phase H;
Phase G record: [2026-07-20-vulkan-phase-g.md](2026-07-20-vulkan-phase-g.md).
Milestone: preset-reachable rendering (MSAA 2x + SMAA via the game RT path)
renders correctly on Vulkan; the RC_* render-texture command family works
instead of being dropped; screenshots produce real images; gamma/brightness
pass matches GL; strict out-of-box defaults stay byte-identical.

## H3 implementation update — 9 September 2026

The native executor now applies the final brightness/gamma shader after scene
and GUI composition, before both presentation and screenshot readback. Neutral
settings bypass all pass allocation/copy/drawing. Per-frame-slot scratch images
use existing image ownership and retirement; a composition flag prevents
double correction when readback and presentation share a frame. Resource and
pipeline failures produce a throttled diagnostic and retain the uncorrected
frame.

Production-controller/math and shader-regeneration tests pass. Windowed
SP/OpenGL and MP/Vulkan retained-menu captures at 1280x720/125%, after language
and video restart, agree within one 8-bit channel value at neutral settings and
at brightness 1.25/gamma 1.3. The
[presentation checkpoint](../ui/presentation-aliases.md) records commands,
pixel-oracle results and evidence limits. This qualifies the observed SDR menu
path; it does not close full Phase H, HDR/color-space, driver validation,
all-GPU/platform or release-performance gates. The recon and stage descriptions
below retain their historical scope.

## Decisions locked by recon (phase-h-recon/*)

- Strict out-of-box defaults need ZERO post work: g_renderFastNoPost 1 +
  every post cvar 0 → the game renders direct to backbuffer; the entire
  engine post chain and game RT path are no-ops. E/F/G already deliver
  that frame. So H must not regress defaults — the deterministic-capture
  path stays untouched at 1/1 gamma and multiSamples 0.
- The DANGEROUS gap (H1 priority): vk drops RC_SET_RENDERTEXTURE /
  RC_CLEAR_RENDERTARGET / RC_RESOLVE_MSAA / RC_SET_POSTPROCESS_* /
  RC_COPY_RENDER (vk_Backend.cpp default: branch). The moment a user
  enables r_postAA (SMAA) or r_multiSamples — which the DEFAULT "balanced"
  menu preset does (MSAA 2x + SMAA 1x) — the game emits binds/clears/
  resolves that vanish and the final fullscreen material paints stale/black
  over the scene: the frame is DESTROYED, not merely un-AA'd. Fixing the RT
  infra is the keystone.
- Screenshots are garbage today: R_ReadTiledPixels/CaptureRenderToFile call
  glReadPixels (vk stub) and the swapchain lacks TRANSFER_SRC_BIT. This has
  blocked the visual soak sign-off since Phase D — H1's readback unblocks
  visual verification of every prior phase.
- Gamma/brightness is a SHADER pass, not a ramp: GLimp_SetGamma is a no-op
  on every backend; RB_ApplyColorMappingsToBackBuffer does color =
  pow(clamp(rgb*brightness,0,1), 1/max(gamma,0.001)) at swap, gated on
  non-neutral (skipped at 1/1). Screenshots read AFTER it — so readback
  must capture post-gamma output.
- TG_SCREEN texgen (S/T/Q = rows of modelView*projection) samples
  _currentRender expecting GL bottom-up rows; the vk negative-height
  viewport already flips, so the RC_COPY_RENDER blit must present bottom-up
  for parity.
- The opt-in engine chain (SSAO, bloom+tonemap, motion blur, HDR
  auto-exposure, CRT, supersampling, the _hdrScene FP16 target) is
  default-OFF AND preset-OFF, and lives entirely in the excluded
  draw_common.cpp — native vk reimplementation, LOW priority (H5, may
  defer). Pure-math parity gate exists: tools/tests/hdr_postprocess_math.py.
- No H work needed: cinematics/ROQ (already render via UploadScratch),
  RenderDoc (API-agnostic, module needs nothing), stereo (dead).

## Stages

H1. Render-texture infrastructure + RC_* commands + screenshot readback
    (the keystone; fixes the frame-destroying drop + unblocks visual soak):
    - Swapchain gains VK_IMAGE_USAGE_TRANSFER_SRC_BIT (VulkanDevice.cpp).
    - Real vk idRenderTexture: module-owned color (+optional depth,
      +optional r_multiSamples MSAA) VkImages behind MakeCurrent/
      EnsureDeviceHandle/GetDeviceHandle (replace the vk_Backend.cpp:410
      stubs); the setRenderTargetCommand_t renderTexture+feedbackRenderTexture
      pair semantics (the game marks its scene RT as its own feedback
      target).
    - Wire RB_ExecuteBackEndCommands: RC_SET_RENDERTEXTURE (switch the
      active dynamic-rendering target — reuses the frame-scope suspend/
      resume from F2), RC_CLEAR_RENDERTARGET, RC_RESOLVE_MSAA (MSAA→single
      resolve blit), RC_SET_POSTPROCESS_SOURCE_SIZE/COLOR_SPACE/
      SMAA_QUALITY (trivial backEnd field writes), RC_COPY_RENDER
      (CopyFramebuffer/CopyDepthbuffer → vkCmdBlitImage into the target
      idImage's vk image, bottom-up for TG_SCREEN parity; copyDepth path).
    - Readback: VK_ReadSwapchainPixels (copy the held pre-present,
      post-gamma swapchain image to a host-visible buffer → RGB) behind the
      glReadPixels consumers (R_ReadTiledPixels/CaptureRenderToFile);
      honors tr.takingScreenshot frame hold.
    Exit: a Vulkan screenshot of q4dm2 is a real image (visual soak
    unblocked); RT commands no longer dropped; defaults unchanged;
    validation-clean.
H2. SS_POST_PROCESS surface pass + _currentRender/_currentDepth captures:
    resume the vk ambient walk past the SS_POST_PROCESS cutoff — automatic
    _currentRender capture on first post surface + MF_NEED_CURRENT_RENDER
    pre-post captures (via H1's RC_COPY_RENDER machinery), then the
    post-process material stages (existing stage walk + a TG_SCREEN texgen
    variant sampling _currentRender). Makes glass/refraction/heat-haze and
    the game's SMAA material stages sample real pixels.
H3. Gamma/brightness final pass: a swapchain pass post-GUI/pre-present with
    the exact formula, gated non-neutral (skipped at 1/1 → capture gates
    unaffected); ordered before H1's readback. Parity: hdr_postprocess_math
    apply_display_color_mapping.
H4. MSAA: r_multiSamples through the game RT path (multisampled color RT +
    RC_RESOLVE_MSAA resolve — lands on H1's RT infra, not a multisampled
    swapchain). Gate: renderer_msaa_cvar_safety.py. Target: the "balanced"
    preset (MSAA 2x + SMAA) renders correctly.
H5. (lower priority, may defer to a late-H sub-phase or Phase I) Opt-in
    engine post effects as native vk passes: SSAO, bloom pyramid + tonemap
    composite, motion blur + motion-vector RT, HDR auto-exposure readback,
    CRT, resolution-scale, supersampling, the _hdrScene FP16 scene target +
    depth-aware present. All default+preset OFF; parity math already gated
    by hdr_postprocess_math.py. Scope decision at H4 close.

Exit: preset-reachable MSAA+SMAA renders correctly on Vulkan validation-
clean; RC_* family works; real screenshots; gamma parity; strict defaults
byte-identical; GL default untouched; full gate + Linux leg; H5 either
landed or explicitly documented-deferred. Visual soak (now possible via
real screenshots) feeds the Phase J promotion evidence.
