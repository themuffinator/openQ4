#!/usr/bin/env python3
"""Regression contract for composited-window screenshot readback."""

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
            "GLimp_SwapBuffers();",
        ),
        "RB_SwapBuffers screenshot presentation gate",
    )


def test_vulkan_capture_resumes_the_acquired_back_buffer() -> None:
    backend_cpp = read("src/renderer/Vulkan/vk_Backend.cpp")
    executor_cpp = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    require(
        backend_cpp,
        "if ( !tr.takingScreenshot )",
        "Vulkan swap-buffer screenshot presentation gate",
    )
    read_pixels_start = executor_cpp.index("bool VK_GuiExecutor_ReadPixels")
    submit_start = executor_cpp.index("static bool VK_GuiExecutor_SubmitFrame", read_pixels_start)
    require_order(
        executor_cpp[read_pixels_start:submit_start],
        (
            "const bool resumeAfterReadback = tr.takingScreenshot;",
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
    for snippet in (
        "VK_FORMAT_B8G8R8A8_UNORM",
        "VK_FORMAT_R8G8B8A8_UNORM",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "formats[ i ].format == VK_FORMAT_UNDEFINED",
        "chosen.format = VK_FORMAT_B8G8R8A8_UNORM;",
        "if ( !compatibleSurfaceFormat )",
        "Vulkan: surface has no compatible legacy SDR UNORM + SRGB_NONLINEAR format",
        "format=%d colorSpace=%d",
    ):
        require(create_swapchain, snippet, "Vulkan legacy SDR swapchain selection")
    reject(
        create_swapchain,
        "chosen = formats[ 0 ];",
        "Vulkan arbitrary surface-format fallback",
    )
    reject(
        create_swapchain,
        "VK_FORMAT_B8G8R8A8_SRGB",
        "Vulkan double-encoding swapchain format",
    )
    reject(
        create_swapchain,
        "VK_FORMAT_R8G8B8A8_SRGB",
        "Vulkan double-encoding swapchain format",
    )
    guard_start = create_swapchain.index("if ( !compatibleSurfaceFormat )")
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


def test_capture_skips_rgb_pack_padding() -> None:
    renderer_cpp = read("src/renderer/RenderSystem.cpp")
    capture = function_body(
        renderer_cpp,
        "void idRenderSystemLocal::CaptureRenderToFile( const char *fileName, bool fixAlpha,",
    )
    require_order(
        capture,
        (
            "const int sourceStride = ( rc->width * 3 + 3 ) & ~3;",
            "const size_t sourceBytes = (size_t)sourceStride * (size_t)rc->height;",
            "R_StaticAlloc( sourceBytes )",
            "memset( data, 0, sourceBytes );",
            "glReadPixels(",
            "const byte *sourceRow = data + (size_t)y * sourceStride;",
            "byte *destinationRow = data2 + (size_t)y * rc->width * 4;",
            "R_StaticFree( data );",
        ),
		"zeroed padded GL_RGB capture conversion",
    )

    width = 3
    rows = (
        bytes((1, 2, 3, 4, 5, 6, 7, 8, 9)),
        bytes((11, 12, 13, 14, 15, 16, 17, 18, 19)),
    )
    stride = (width * 3 + 3) & ~3
    packed = b"".join(row + bytes(stride - len(row)) for row in rows)
    converted = b"".join(
        packed[offset : offset + width * 3]
        for offset in range(0, len(packed), stride)
    )
    if converted != b"".join(rows):
        raise AssertionError("row-stride model included GL_RGB pack padding")


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
            "R_StaticFree( data );",
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


def main() -> None:
    test_screenshot_reads_the_unpresented_back_buffer()
    test_capture_defers_only_the_window_present()
    test_vulkan_capture_resumes_the_acquired_back_buffer()
    test_vulkan_swapchain_preserves_legacy_sdr_code_values()
    test_save_preview_resamples_a_coherent_full_frame()
    test_capture_skips_rgb_pack_padding()
    test_capture_center_crops_and_resamples_safely()
    print("renderer_screenshot_readback: ok")


if __name__ == "__main__":
    main()
