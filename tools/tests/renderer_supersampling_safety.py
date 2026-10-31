#!/usr/bin/env python3
from pathlib import Path


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def read_repo_file(relative_path):
    return (Path(__file__).resolve().parents[2] / relative_path).read_text(encoding="utf-8")


def test_cvar_and_menu_expose_safe_supersampling_range():
    init_cpp = read_repo_file(Path("src") / "renderer" / "RenderSystem_init.cpp")
    system_gui = read_repo_file(Path("content") / "baseoq4" / "pak0" / "guis" / "menu" / "settings" / "system.gui")

    assert_true('"r_screenFraction", "100"' in init_cpp, "r_screenFraction should keep native resolution as the default")
    assert_true("10, 200" in init_cpp, "r_screenFraction should expose the guarded 10..200 range")
    assert_true('"10%;25%;50%;75%;85%;100%;125%;150%;200%"' in system_gui, "video menu should expose performance and supersampling presets")
    assert_true('"10;25;50;75;85;100;125;150;200"' in system_gui, "video menu preset values should match the displayed resolution scale choices")


def test_legacy_crop_does_not_run_above_native():
    render_system = read_repo_file(Path("src") / "renderer" / "RenderSystem.cpp")
    push = render_system[render_system.index("bool idRenderSystemLocal::PushSceneResolutionScale( const renderView_t &renderView ) {"):]
    push = push[: push.index("\n}\n")]

    # The legacy mode 0 crop wraps the world render only, after the native and
    # temporal-presentation checks, so it never runs at or above native.
    assert_true(
        push.index("if ( screenFraction >= 100 ) {") < push.index("const bool legacyCrop = r_resolutionScaleMode.GetInteger() == 0;"),
        "legacy crop mode should only run below native resolution",
    )
    for token in (
        "R_TemporalPresentation_DynamicResolutionRequested()",
        "R_TemporalPresentation_TemporalAARequested()",
        "R_TemporalPresentation_ScreenSpaceEffectsRequested()",
        "vrFrame.active",
    ):
        assert_true(token in push, f"legacy crop should stand down for {token}")
    assert_true("Supersampling above native" in render_system, "BeginFrame should document that supersampling is handled offscreen")
    # Issue #169: a crop pushed in BeginFrame drew the menu into the corner.
    begin_frame = render_system[render_system.index("void idRenderSystemLocal::BeginFrame("):]
    begin_frame = begin_frame[: begin_frame.index("\n}\n")]
    assert_true("CropRenderSize(" not in begin_frame, "BeginFrame must not crop the whole frame, menu included")


def test_es_resolution_scale_is_scoped_to_the_scene():
    render_system = read_repo_file(Path("src") / "renderer" / "RenderSystem.cpp")
    render_world = read_repo_file(Path("src") / "renderer" / "RenderWorld.cpp")
    gles_backend = read_repo_file(Path("src") / "renderer" / "GLES" / "gles_Backend.cpp")

    # A crop pushed in BeginFrame is the whole frame's coordinate system and
    # reaches idGuiModel::EmitFullScreen, so the HUD and menus render into it
    # too. Scoping the crop to RenderScene is what keeps 2D at native
    # resolution, and it is the whole point of the feature.
    assert_true(
        "tr.PushSceneResolutionScale( parms->renderView )" in render_world and "tr.PopSceneResolutionScale()" in render_world,
        "the ES resolution scale should wrap the world render, not the frame",
    )
    assert_true(
        "glConfig.backendCaps.profile != RENDERER_CONTEXT_PROFILE_ES" in render_system
        and "r_resolutionScaleMode.GetInteger() == 0" in render_system,
        "the resolved scene crop should be limited to ES; mode 0 crops without a resolve",
    )
    # A GUI renderDef draws a model into part of a native-resolution menu. It
    # must neither be cropped nor trigger the ES resolve, which would stretch
    # the display's corner over the whole menu.
    assert_true(
        "RenderViewToViewport( &renderView, &fullViewport );" in render_system
        and "fullViewport.x2 < renderCrops[0].width - 1" in render_system,
        "only a view that fills the display should be cropped",
    )
    assert_true(
        "parms->sceneResolutionScaled = sceneResolutionScaled;" in render_world
        and "!backEnd.viewDef->sceneResolutionScaled" in gles_backend,
        "the ES resolve should lift out only views that were rendered into the crop",
    )
    assert_true(
        "resolutionScaleWidth = renderCrops[currentRenderCrop].width" in render_system,
        "the back end should be handed the crop that was actually pushed, not the requested percentage",
    )
    # A single-sample offscreen scene target is the normal path once MSAA or
    # post AA is on, and it scales fine: the back end resolves the crop inside
    # the bound target before the game samples it. Only a multisampled target,
    # which ES 3.0 refuses as a blit destination, has to decline.
    assert_true(
        "sceneColor->GetOpts().numMSAASamples > 1" in render_system,
        "only a multisampled scene target should decline the crop, not every offscreen target",
    )
    assert_true(
        "tiledViewport[0] > glConfig.vidWidth" in render_system,
        "a larger-than-window tiled capture should decline the crop",
    )
    assert_true(
        "backEnd.viewDef->viewEntitys == NULL || backEnd.viewDef->isSubview" in gles_backend,
        "the resolve should run on the scene view only, so 2D views stay outside the crop",
    )
    # A portal sky is a top-level view, not a subview, and arrives immediately
    # before the view it backs. Resolving after it upscales the sky, the main
    # view then draws over an already-upscaled backdrop, and the second resolve
    # magnifies it again -- a sky 1/fraction too large, panning at that multiple.
    assert_true(
        "backEnd.viewDef->renderFlags & RF_PORTAL_SKY" in gles_backend,
        "the resolve must skip portal-sky views or the sky is upscaled twice",
    )
    assert_true(
        "glGetIntegerv( GL_SAMPLE_BUFFERS, &sampleBuffers )" in gles_backend,
        "the multisample guard must test SAMPLE_BUFFERS; SAMPLES is reported non-zero on single-sampled windows",
    )
    assert_true(
        "tr.resolutionScaleSuppressed = true" in gles_backend,
        "a back end that cannot resolve the crop must stop the front end pushing it, or the scene stays in the corner",
    )
    assert_true(
        "rb_glesResolutionScaleFbo" in gles_backend and "GL_LINEAR" in gles_backend,
        "the upscale should route through a scratch target instead of blitting a framebuffer onto itself",
    )


def test_nearest_neighbour_upscale_mode_is_reachable():
    init_cpp = read_repo_file(Path("src") / "renderer" / "RenderSystem_init.cpp")
    draw_common = read_repo_file(Path("src") / "renderer" / "draw_common.cpp")
    gles_backend = read_repo_file(Path("src") / "renderer" / "GLES" / "gles_Backend.cpp")

    assert_true(
        "0, 3, idCmdSystem::ArgCompletion_Integer<0,3>" in init_cpp,
        "r_resolutionScaleMode should expose the nearest-neighbour mode",
    )
    assert_true(
        "idMath::ClampInt( 0, 3, r_resolutionScaleMode.GetInteger() )" in draw_common,
        "the desktop path must not clamp mode 3 down to 2, which would switch its sharpening on",
    )
    assert_true(
        "RB_RESOLUTION_SCALE_MODE_NEAREST" in gles_backend and "upscaleFilter" in gles_backend,
        "the ES upscale should pick its blit filter from the scale mode",
    )


def test_scene_target_supersampling_is_guarded_and_scales_clipping():
    draw_common = read_repo_file(Path("src") / "renderer" / "draw_common.cpp")

    assert_true("RB_SCREEN_FRACTION_MAX = 200" in draw_common, "renderer should keep a conservative supersampling ceiling")
    assert_true("RB_MaxSceneScaleDimension" in draw_common and "glConfig.maxTextureSize" in draw_common, "supersampling should clamp against GL texture limits")
    assert_true("RB_ScaledSceneTargetRequested" in draw_common, "supersampling should request the scaled scene render target")
    assert_true("!scaledScene && requestedSamples > 1" in draw_common, "scaled scene targets should remain single-sample instead of layering MSAA onto the resized FBO")
    assert_true("RB_BeginSceneScaling" in draw_common, "supersampling should scale the backend scene command")
    assert_true("RB_ScaleTrackedRect( state, &vLight->scissorRect" in draw_common, "light scissors should scale with the supersampled viewport")
    assert_true("RB_ScaleDrawSurfChainScissors" in draw_common, "light draw-surface chains should have scaled scissors")
    assert_true("RB_ScaleTrackedRect( state, &vEntity->scissorRect" in draw_common, "entity scissors should scale with the supersampled viewport")
    assert_true("RB_DrawFullscreenPostProcessQuad( sourceViewportWidth, sourceViewportHeight" in draw_common, "present should sample the supersampled source region when resolving")
    assert_true("targetViewport = scaleState.active ? scaleState.nativeViewport" in draw_common, "present should resolve back to the native viewport")


def main():
    test_cvar_and_menu_expose_safe_supersampling_range()
    test_legacy_crop_does_not_run_above_native()
    test_es_resolution_scale_is_scoped_to_the_scene()
    test_nearest_neighbour_upscale_mode_is_reachable()
    test_scene_target_supersampling_is_guarded_and_scales_clipping()
    print("renderer_supersampling_safety: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
