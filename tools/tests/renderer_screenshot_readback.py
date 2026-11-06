#!/usr/bin/env python3
"""Regression contract for composited-window screenshot readback."""

import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(source: str, snippet: str, context: str) -> None:
    if snippet not in source:
        raise AssertionError(f"Missing {snippet!r} in {context}")


def reject(source: str, snippet: str, context: str) -> None:
    if snippet in source:
        raise AssertionError(f"Unexpected {snippet!r} in {context}")


def require_order(source: str, snippets: tuple[str, ...], context: str) -> None:
    positions = [source.find(snippet) for snippet in snippets]
    if any(position < 0 for position in positions) or positions != sorted(positions):
        raise AssertionError(f"Expected ordered snippets in {context}: {snippets!r}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def test_screenshot_reads_the_unpresented_back_buffer() -> None:
    init_cpp = read("src/renderer/RenderSystem_init.cpp")
    require_order(
        init_cpp,
        (
            "session->UpdateScreen();",
            "glReadBuffer( r_frontBuffer.GetBool() ? GL_FRONT : GL_BACK );",
            "glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgbaTemp );",
        ),
        # GL_RGBA, not GL_RGB: ES guarantees only RGBA/UNSIGNED_BYTE for the
        # default framebuffer, and ANGLE rejects the GL_RGB read outright. The
        # read is RGBA everywhere and packed down to RGB on the CPU.
        "regular screenshot readback path",
    )


def test_capture_defers_only_the_window_present() -> None:
    backend_cpp = read("src/renderer/tr_backend.cpp")
    require_order(
        backend_cpp,
        (
            "RB_ApplyResolutionScaleToBackBuffer();",
            "RB_ApplyCRTToBackBuffer();",
            "RB_ApplyColorMappingsToBackBuffer();",
            "if ( !r_frontBuffer.GetBool() && !tr.takingScreenshot )",
            "R_SwapBuffersWithInputTiming();",
        ),
        "RB_SwapBuffers screenshot presentation gate",
    )


def test_vulkan_capture_resumes_the_acquired_back_buffer() -> None:
    backend_cpp = read("src/renderer/Vulkan/vk_Backend.cpp")
    executor_cpp = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    require(
        backend_cpp,
        "if ( !R_SubmittedTakingScreenshot() )",
        "Vulkan swap-buffer screenshot presentation gate",
    )
    read_pixels_start = executor_cpp.index("bool VK_GuiExecutor_ReadPixels")
    submit_start = executor_cpp.index("static bool VK_GuiExecutor_SubmitFrame", read_pixels_start)
    require_order(
        executor_cpp[read_pixels_start:submit_start],
        (
            "const bool resumeAfterReadback = R_SubmittedTakingScreenshot();",
            "VK_GuiExecutor_SubmitFrame( !resumeAfterReadback )",
            "VK_Exec_BeginMainRendering( true );",
        ),
        "Vulkan screenshot readback resume path",
    )


def test_vulkan_swapchain_preserves_legacy_sdr_code_values() -> None:
    device_cpp = read("src/renderer/Vulkan/VulkanDevice.cpp")
    create_swapchain = function_body(
        device_cpp, "static bool VK_Device_CreateSwapchain( void )"
    )
    selection = function_body(
        read("src/renderer/Vulkan/VulkanDeviceSelection.cpp"),
        "VkResult VK_SelectSurfaceFormat(",
    )
    for snippet in (
        "VK_FORMAT_B8G8R8A8_UNORM",
        "VK_FORMAT_R8G8B8A8_UNORM",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "format.format == VK_FORMAT_UNDEFINED",
        "return VK_ERROR_FORMAT_NOT_SUPPORTED;",
    ):
        require(selection, snippet, "Vulkan shared legacy SDR surface selection")
    for snippet in (
        "VK_SelectSurfaceFormat( vkGetPhysicalDeviceSurfaceFormatsKHR,",
        "if ( formatResult != VK_SUCCESS )",
        "Vulkan: surface has no compatible legacy SDR UNORM + SRGB_NONLINEAR format",
        "format=%d colorSpace=%d",
    ):
        require(create_swapchain, snippet, "Vulkan legacy SDR swapchain selection")
    reject(
        selection,
        "selected = formats[ 0 ];",
        "Vulkan arbitrary surface-format fallback",
    )
    reject(
        selection,
        "VK_FORMAT_B8G8R8A8_SRGB",
        "Vulkan double-encoding swapchain format",
    )
    reject(
        selection,
        "VK_FORMAT_R8G8B8A8_SRGB",
        "Vulkan double-encoding swapchain format",
    )
    guard_start = create_swapchain.index("if ( formatResult != VK_SUCCESS )")
    create_start = create_swapchain.index("vkCreateSwapchainKHR")
    require_order(
        create_swapchain[guard_start:create_start],
        (
            "Vulkan: surface has no compatible legacy SDR UNORM + SRGB_NONLINEAR format",
            "return false;",
        ),
        "Vulkan fail-closed surface-format guard",
    )


def test_save_preview_resamples_a_coherent_full_frame() -> None:
    session_cpp = read("src/framework/Session.cpp")
    save_game = function_body(session_cpp, "bool idSessionLocal::SaveGame(")
    require_order(
        save_game,
        (
            "sessionRenderCropGuard_t previewCrop( renderSystem->GetScreenWidth(), renderSystem->GetScreenHeight() );",
            "game->Draw( 0 );",
            "renderSystem->CaptureRenderToFile( tempPreviewFile, true, 320, 240 );",
        ),
		"physical full-frame save-preview capture and CPU resize",
    )
    guard = function_body(session_cpp, "class sessionRenderCropGuard_t")
    require_order(
        guard,
        (
            "renderSystem->CropRenderSize( width, height, false, true );",
            "active = true;",
            "~sessionRenderCropGuard_t()",
            "renderSystem->UnCrop();",
        ),
        "exception-safe physical render crop",
    )

    renderer_header = read("src/renderer/RenderSystem.h")
    require_order(
        renderer_header,
        (
            "CaptureRenderToFile( const char *fileName, bool fixAlpha = false )",
            "SetUnderwaterView( float amount, const idVec3 &tint, float fogDistance )",
            "CaptureRenderToFile( const char *fileName, bool fixAlpha, int outputWidth, int outputHeight )",
        ),
        "append-only resized-capture renderer interface",
    )


def test_capture_uses_aligned_rgba() -> None:
    renderer_cpp = read("src/renderer/RenderSystem.cpp")
    capture = function_body(
        renderer_cpp,
        "void idRenderSystemLocal::CaptureRenderToFile( const char *fileName, bool fixAlpha,",
    )
    require_order(
        capture,
        (
            "R_StaticAlloc( (size_t)sourcePixelCount * 4 )",
            "memset( data2, 0, (size_t)sourcePixelCount * 4 );",
            "glReadPixels( rc->x, rc->y, rc->width, rc->height, GL_RGBA, GL_UNSIGNED_BYTE, data2 );",
            "data2[pixel * 4 + 3] = 0xff;",
        ),
        "aligned RGBA capture with the existing opaque-alpha contract",
    )
    if 'GL_RGB,' in capture or 'sourceStride' in capture:
        raise AssertionError("capture reintroduced the padded RGB intermediate")


def aspect_crop(
    source_width: int,
    source_height: int,
    output_width: int,
    output_height: int,
) -> tuple[int, int, int, int]:
    crop_x = 0
    crop_y = 0
    crop_width = source_width
    crop_height = source_height
    source_product = source_width * output_height
    output_product = source_height * output_width
    if source_product > output_product:
        crop_width = max(source_height * output_width // output_height, 1)
        crop_x = (source_width - crop_width) // 2
    elif source_product < output_product:
        crop_height = max(source_width * output_height // output_width, 1)
        crop_y = (source_height - crop_height) // 2
    return crop_x, crop_y, crop_width, crop_height


def test_capture_center_crops_and_resamples_safely() -> None:
    renderer_cpp = read("src/renderer/RenderSystem.cpp")
    helper = function_body(renderer_cpp, "static byte *R_ResampleCaptureToAspectRGBA(")
    capture = function_body(
        renderer_cpp,
        "void idRenderSystemLocal::CaptureRenderToFile( const char *fileName, bool fixAlpha,",
    )

    require_order(
        helper,
        (
            "const int64 sourceAspectProduct = (int64)sourceWidth * outputHeight;",
            "cropX = ( sourceWidth - cropWidth ) / 2;",
            "cropY = ( sourceHeight - cropHeight ) / 2;",
            "const double sourceY = cropY +",
            "byte *outputRow = output + (size_t)y * (size_t)outputWidth * 4;",
        ),
        "orientation-preserving center-aspect resize",
    )
    require(capture, "outputWidth > MAX_CAPTURE_DIMENSION", "bounded capture output width")
    require(capture, "outputHeight > MAX_CAPTURE_DIMENSION", "bounded capture output height")
    require(renderer_cpp, "static const int64 MAX_CAPTURE_PIXELS = 33554432;", "capture pixel budget")
    require(capture, "const int64 sourcePixelCount = (int64)rc->width * rc->height;", "64-bit source pixel count")
    require(capture, "sourcePixelCount > MAX_CAPTURE_PIXELS", "bounded source pixel count")
    require(capture, "const int64 outputPixelCount = (int64)outputWidth * outputHeight;", "64-bit output pixel count")
    require(capture, "outputPixelCount > MAX_CAPTURE_PIXELS", "bounded output pixel count")
    require(capture, "R_ResampleCaptureToAspectRGBA(", "CPU save-preview resample")
    require_order(
        capture,
        (
            "outputData = R_ResampleCaptureToAspectRGBA(",
            "R_StaticFree( data2 );",
            "R_WriteTGA(",
            "R_StaticFree( outputData );",
        ),
        "bounded capture allocation lifetimes",
    )

    expected_crops = {
        (1280, 720, 320, 240): (160, 0, 960, 720),
        (1024, 768, 320, 240): (0, 0, 1024, 768),
        (720, 1280, 320, 240): (0, 370, 720, 540),
    }
    for dimensions, expected in expected_crops.items():
        actual = aspect_crop(*dimensions)
        if actual != expected:
            raise AssertionError(
                f"aspect crop for {dimensions!r} was {actual!r}, expected {expected!r}"
            )


def capture_viewport(
    crop_size: int, view_width: int, view_height: int
) -> tuple[int, int]:
    """idRenderSystemLocal::RenderViewToViewport for a crop_size capture crop."""
    w_ratio = crop_size / 640.0
    h_ratio = crop_size / 480.0
    x2 = math.floor(view_width * w_ratio + 0.5) - 1
    y1 = crop_size - math.floor(view_height * h_ratio + 0.5)
    return x2 + 1, crop_size - y1


def test_cube_face_captures_span_the_virtual_screen() -> None:
    lightgrid_cpp = read("src/renderer/RenderWorld_lightgrid.cpp")
    renderer_cpp = read("src/renderer/RenderSystem.cpp")
    bake = function_body(
        lightgrid_cpp,
        "bool R_BakeCurrentLightGrids( const lightGridBakeOptions_t &options, const char *jobName )",
    )
    # A render-view rectangle is in SCREEN_WIDTH x SCREEN_HEIGHT units, scaled
    # by the capture crop BeginFrame takes from tr.tiledViewport. Window pixels
    # drew each cube face into a larger viewport and the readback kept only its
    # top-left corner, unless the window was 640x480.
    require_order(
        bake,
        (
            "renderView_t captureView = tr.primaryView->renderView;",
            "captureView.width = SCREEN_WIDTH;",
            "captureView.height = SCREEN_HEIGHT;",
            "captureView.fov_x = 90.0f;",
            "captureView.fov_y = 90.0f;",
        ),
        "light-grid bake capture view",
    )
    reject(bake, "captureView.width = glConfig.vidWidth", "light-grid bake capture view")
    reject(bake, "captureView.height = glConfig.vidHeight", "light-grid bake capture view")
    # the synchronous and the async PBO capture paths share that view
    require(
        lightgrid_cpp,
        "readbackPool.IssueReadback( &captureView, job, side );",
        "async light-grid face capture",
    )
    require(
        lightgrid_cpp,
        "LightGrid_CaptureProbeFacesSync( task, options, captureView, cubeAxes, *job );",
        "synchronous light-grid face capture",
    )
    # envshot and bakeReflectionProbes capture cube faces the same way
    init_cpp = read("src/renderer/RenderSystem_init.cpp")
    for signature, context in (
        ("void R_EnvShot_f( const idCmdArgs &args )", "envshot face view"),
        ("static void R_BakeReflectionProbes_f( const idCmdArgs &args )", "reflection-probe face view"),
    ):
        body = function_body(init_cpp, signature)
        require_order(
            body,
            ("ref.width = SCREEN_WIDTH;", "ref.height = SCREEN_HEIGHT;", "tr.TakeScreenshot( "),
            context,
        )
        reject(body, "ref.width = glConfig.vidWidth", context)
    require(renderer_cpp, "windowWidth = tiledViewport[0];", "capture-sized render crop")
    require(
        renderer_cpp,
        "float wRatio = (float)rc->width / SCREEN_WIDTH;",
        "virtual-screen render-view scaling",
    )

    for size in (16, 32, 64, 128, 256):
        if capture_viewport(size, 640, 480) != (size, size):
            raise AssertionError(f"virtual-screen view does not fill a {size}px face")
    # the cropped faces before the fix: a 64px face at 1280x800 rendered 128x107
    if capture_viewport(64, 1280, 800) != (128, 107):
        raise AssertionError("viewport model no longer matches RenderViewToViewport")


def main() -> None:
    test_screenshot_reads_the_unpresented_back_buffer()
    test_capture_defers_only_the_window_present()
    test_vulkan_capture_resumes_the_acquired_back_buffer()
    test_vulkan_swapchain_preserves_legacy_sdr_code_values()
    test_save_preview_resamples_a_coherent_full_frame()
    test_capture_uses_aligned_rgba()
    test_capture_center_crops_and_resamples_safely()
    test_cube_face_captures_span_the_virtual_screen()
    print("renderer_screenshot_readback: ok")


if __name__ == "__main__":
    main()
