#!/usr/bin/env python3
"""Pin where OpenGL's MSAA lives when the window does not carry it.

OpenGL used to create its window with r_multiSamples samples, so an MSAA change
needed a new window: a renderer restart. By default the desktop OpenGL window
is now single-sample (r_glWindowMultisample 0) and the samples live in render
targets, which follow the setting on the running device. This pins the pieces
that keep the pictures the same:

- the policy: Apple keeps the multisampled window by default (its OpenGL 2.1
  context has no multisample textures), OpenGL ES always does, and the answer
  is latched when a device creates its window;
- 3D views drawn straight to a single-sample window (GUI renderDef windows,
  remote cameras, mirrors) draw into a multisampled target that stands in for
  the window, seeded from it and resolved back into it, and that target never
  engages where the window carries the samples, under VR, or for a view that
  already has a render texture;
- the renderer's own scene target takes its samples from the setting when the
  window carries none;
- a capture out of the multisampled stand-in resolves with a blit;
- the Display page restarts the device for an MSAA change only where the
  window carries the samples.
"""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def body(source: str, signature: str) -> str:
    """The braced body after a signature, skipping braces inside literals and comments."""
    start = source.index(signature)
    index = brace = source.index("{", start + len(signature) - 1)
    depth = 0
    while index < len(source):
        char = source[index]
        if source.startswith("//", index):
            index = source.index("\n", index)
            continue
        if source.startswith("/*", index):
            index = source.index("*/", index) + 2
            continue
        if char in "\"'":
            index += 1
            while source[index] != char:
                index += 2 if source[index] == "\\" else 1
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
        index += 1
    raise AssertionError(f"unterminated body: {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    init = read("src/renderer/RenderSystem_init.cpp")
    samples = read("src/renderer/OpenGL/FramebufferSamples.h")
    draw = read("src/renderer/draw_common.cpp")
    images = read("src/renderer/Image_load.cpp")
    service = read("src/ui/SettingsDisplayService.cpp")

    # The policy and its latch.
    default = init[init.index("#if defined( __APPLE__ )\n#define R_GL_WINDOW_MULTISAMPLE_DEFAULT"):init.index("idCVar r_glWindowMultisample(")]
    require('#define R_GL_WINDOW_MULTISAMPLE_DEFAULT "1"\n#else\n#define R_GL_WINDOW_MULTISAMPLE_DEFAULT "0"' in default,
            "only Apple keeps the multisampled window by default")
    require('idCVar r_glWindowMultisample( "r_glWindowMultisample", R_GL_WINDOW_MULTISAMPLE_DEFAULT, CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL' in init,
            "the policy is an archived renderer setting")
    latch = body(init, "int R_WindowMultiSamplesForNewDevice( void ) {")
    require("#if defined( __ANDROID__ )\n\tr_windowCarriesMultisampling = true;" in latch and
            "r_windowCarriesMultisampling = r_glesContext.GetBool() || r_glWindowMultisample.GetBool();" in latch and
            "return r_windowCarriesMultisampling ? r_multiSamples.GetInteger() : 0;" in latch,
            "a new window is asked for samples only where the policy says it carries them")
    require(init.count("parms.multiSamples = R_WindowMultiSamplesForNewDevice();") == 1,
            "the device's window request goes through the policy")
    require("parms.multiSamples = r_windowCarriesMultisampling ? r_multiSamples.GetInteger() : 0;" in init,
            "a partial restart keeps the samples the device gave its window")
    require("parms.multiSamples = r_multiSamples.GetInteger();" not in init,
            "no window request may read the setting past the policy")

    # Sample counts.
    default_samples = body(samples, "inline int R_DefaultFramebufferSamples() {")
    stand_in = body(read("src/renderer/OpenGL/gl_RenderTexture.cpp"), "int R_DefaultRenderTargetSamples( void ) {")
    require("return R_DefaultRenderTargetSamples();" in default_samples and
            "r_defaultRenderTarget->GetColorImage( 0 )" in stand_in and "return samples > 1 ? samples : 0;" in stand_in,
            "a stand-in for the window reports its own samples")
    view_samples = body(samples, "inline int R_ViewFramebufferSamples() {")
    require("window != 0 || R_DefaultFramebufferHandle() != 0 || R_WindowCarriesMultisampling()" in view_samples and
            "return r_multiSamples.GetInteger() > 1 ? r_multiSamples.GetInteger() : 0;" in view_samples,
            "a single-sample window leaves the count to the setting; a window that carries samples and a VR stand-in keep theirs")
    ensure = body(draw, "static bool RB_EnsureSceneRenderTexture( const viewDef_t *sceneTargetView ) {")
    require("const int requestedSamples = Max( 0, R_ViewFramebufferSamples() );" in ensure,
            "the renderer's scene target follows the samples its views get")

    # The window view.
    begin = body(draw, "static void RB_WindowView_Begin( const viewDef_t *viewDef, rbWindowViewState_t &state ) {")
    for term in ("!r_glWindowViewMultisample.GetBool()", "samples <= 1", "viewDef->viewEntitys == NULL",
                 "backEnd.renderTexture != NULL", "R_DefaultFramebufferHandle() != 0",
                 "R_WindowCarriesMultisampling()", "R_DefaultFramebufferSamples() != 0"):
        require(term in begin, f"the window view must not engage without checking {term}")
    seed = begin.index("RB_WindowView_Blit( state );")
    stand = begin.index("R_SetDefaultRenderTarget( rbWindowViewRenderTexture );")
    require(begin.index("glBindFramebuffer( GL_DRAW_FRAMEBUFFER, rbWindowViewRenderTexture->GetDeviceHandle() );") < seed < stand,
            "the target is seeded from the window before it stands in for it")
    require("RB_WindowView_Refuse(" in begin and begin.index("R_SetDefaultRenderTarget( NULL );") > stand,
            "a failed seed leaves the window as the target")
    end = body(draw, "static void RB_WindowView_End( rbWindowViewState_t &state ) {")
    require(end.index("R_SetDefaultRenderTarget( NULL );") < end.index("glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );")
            < end.rindex("RB_WindowView_Blit( state );") < end.index("idRenderTexture::BindNull();"),
            "the view's rectangle is resolved back into the window and the window is bound again")
    captured = end[end.index("if ( state.captured ) {"):end.index("glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );")]
    require("glBindFramebuffer( GL_DRAW_FRAMEBUFFER, rbWindowViewResolveRenderTexture->GetDeviceHandle() );" in captured and
            "RB_WindowView_Blit( state );" in captured and
            "glBindFramebuffer( GL_READ_FRAMEBUFFER, rbWindowViewResolveRenderTexture->GetDeviceHandle() );" in captured,
            "a subview's samples are averaged into a single-sample target before the copy to the window")
    require("state.captured = viewDef->isSubview;" in begin, "only a subview is captured into a texture afterwards")
    mirror = body(draw, "static void RB_WindowView_MirrorSamplePositions( unsigned int framebuffer, int samples ) {")
    require("positions[i * 2 + 1] = 1.0f - positions[i * 2 + 1];" in mirror and
            "glFramebufferSampleLocationsfvARB( GL_DRAW_FRAMEBUFFER, 0, samples, positions );" in mirror and
            "GLEW_ARB_sample_locations" in mirror and "GL_FALSE" in mirror,
            "the stand-in takes the window's sample pattern where positions are programmable, and its own otherwise")
    require(begin.index("RB_WindowView_MirrorSamplePositions(") < begin.index("RB_WindowView_Blit( state );"),
            "the pattern is set before anything is drawn into the stand-in")
    blit = body(draw, "static void RB_WindowView_Blit( const rbWindowViewState_t &state ) {")
    require("glDisable( GL_SCISSOR_TEST );" in blit and "GL_COLOR_BUFFER_BIT, GL_NEAREST" in blit,
            "the copy covers the whole rectangle, whatever scissor a light left")
    target = body(draw, "static bool RB_WindowView_EnsureTarget( int width, int height, int samples ) {")
    require("colorOpts.numMSAASamples = samples;" in target and "depthOpts.format = FMT_DEPTH_STENCIL;" in target and
            "rbWindowViewColorImage->GetOpts().numMSAASamples <= 1" in target,
            "the stand-in is a multisampled colour and depth-stencil pair, refused when it comes back single-sample")
    view = body(draw, "void\tRB_STD_DrawView( void ) {")
    opened = view.index("RB_WindowView_Begin( backEnd.viewDef, windowView );")
    closed = view.index("RB_WindowView_End( windowView );")
    require(view.index("if ( !sceneRenderTargetReady ) {") < opened < view.index("RB_BeginDrawingView();") < closed
            < view.index("RB_CapturePortalSkyBackdrop( backEnd.viewDef );"),
            "the window view wraps the whole view, and only a view the scene target did not take")
    require("RB_DestroyPostProcessRenderTexture( rbWindowViewRenderTexture );" in draw,
            "the stand-in is released with the other post-process targets")

    # The capture out of a multisampled stand-in.
    copy = body(images, "bool idImage::CopyFramebuffer(")
    require("R_DefaultFramebufferHandle() != 0 && R_DefaultFramebufferSamples() > 1" in copy and
            "( readingFromRenderTexture || readingFromMultisampledStandIn )" in copy and
            "? backEnd.renderTexture->GetDeviceHandle() : R_DefaultFramebufferHandle() );" in copy,
            "a colour copy out of a multisampled stand-in resolves with a blit")

    # The Display page.
    window = body(service, "bool EngineSettingsDisplayHost::WindowMultisampling() const {")
    require("if (api == RENDER_MODULE_API_GLES) return true;" in window and
            'cvarSystem->GetCVarBool("r_glWindowMultisample")' in window and "RENDER_MODULE_API_VULKAN" not in window,
            "only a window that carries the samples makes an MSAA change a device restart")
    supports = body(service, "bool EngineSettingsDisplayHost::SupportsMultisampling() const {")
    require("WindowMultisampling()" not in supports, "MSAA support does not depend on where the samples live")

    registry = ROOT / "tools" / "validation" / "openq4_validate.py"
    if registry.exists():
        require("renderer_gl_window_view_contract.py" in read("tools/validation/openq4_validate.py"),
                "register this test in tools/validation/openq4_validate.py")
    print("renderer_gl_window_view_contract: ok")


if __name__ == "__main__":
    main()
