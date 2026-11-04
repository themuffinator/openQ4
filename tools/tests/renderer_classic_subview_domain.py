#!/usr/bin/env python3
"""Static regression contract for sealed direct, capture, and nested subview ownership."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TEST_PATH = "tools/tests/renderer_classic_subview_domain.py"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def braced_body(source: str, marker: str, context: str) -> str:
    start = source.find(marker)
    if start == -1:
        raise AssertionError(f"Missing {marker!r} in {context}")
    opening = source.find("{", start + len(marker))
    if opening == -1:
        raise AssertionError(f"Missing opening brace after {marker!r} in {context}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"Could not find closing brace for {marker!r} in {context}")


def validate_packet_capture_edge() -> None:
    header = read("src/renderer/ScenePackets.h")
    source = read("src/renderer/ScenePackets.cpp")
    for token in (
        "SCENE_PACKET_MAX_SUBVIEW_CAPTURES",
        "SCENE_PACKET_OVERFLOW_SUBVIEW_CAPTURES",
        "sceneSubviewCapture_t",
        "viewScenePacketIndex",
        "AddSubviewCapture",
        "NumSubviewCaptures",
        "SubviewCapture",
        "R_ScenePackets_AddCopyRender( idImage *image, int x, int y",
    ):
        require(header, token, "packet-level capture edge")
    for token in (
        "rg_scenePacketLastViews[R_ScenePackets_FrameSlot()]",
        "R_ScenePackets_FrontEndCaptureRequired",
        "r_rendererSharedSubview.GetBool()",
        "R_ScenePackets_AddCopyRender( idImage *image, int x, int y",
        "rg_scenePacketFrames[R_ScenePackets_FrameSlot()].AddSubviewCapture",
        "rg_scenePacketLastViews[R_ScenePackets_FrameSlot()] = NULL;",
        "R_ScenePackets_BuildLegacyCommandStream",
        "packetFrame.AddSubviewCapture( lastDrawView",
        "subviewCaptures=%d",
    ):
        require(source, token, "front-end and legacy capture association")


def validate_domain_contract() -> None:
    header = read("src/renderer/ClassicSubviewDomain.h")
    source = read("src/renderer/ClassicSubviewDomain.cpp")
    combined = header + source
    for token in (
        "CLASSIC_SUBVIEW_DOMAIN_KIND_DIRECT_MIRROR",
        "CLASSIC_SUBVIEW_DOMAIN_KIND_REMOTE_CAMERA",
        "CLASSIC_SUBVIEW_DOMAIN_KIND_MIRROR",
        "CLASSIC_SUBVIEW_DOMAIN_KIND_REFLECTION",
        "CLASSIC_SUBVIEW_DOMAIN_KIND_REFRACTION",
        "CLASSIC_SUBVIEW_DOMAIN_KIND_XRAY",
        "CLASSIC_SUBVIEW_DOMAIN_CAPTURE_COLOR_2D",
        "CLASSIC_SUBVIEW_DOMAIN_CAPTURE_COLOR_CUBEMAP",
        "CLASSIC_SUBVIEW_DOMAIN_CAPTURE_DEPTH_2D",
        "CLASSIC_SUBVIEW_DOMAIN_CAPTURE_DEPTH_CUBEMAP",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_MISSING_CAPTURE",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_CAPTURE_VIEWPORT_MISMATCH",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_UNSUPPORTED_CAPTURE_TARGET",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_UNEXPECTED_CAPTURE",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_UNSUPPORTED_SPECIAL_SEMANTICS",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_VIEW_SEMANTICS_MISMATCH",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_MISSING_NESTED_PARENT",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_NESTED_COMMAND_ORDER",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_NESTED_PARENT_FALLBACK",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_NESTED_CHILD_FALLBACK",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_NESTED_CINEMATIC_POST_FALLBACK",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_NESTING_INCOMPLETE",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_NESTED_CINEMATIC_POST_INCOMPLETE",
        "CLASSIC_SUBVIEW_DOMAIN_FAILURE_BACKEND_CAPTURE_MISMATCH",
        "CLASSIC_SUBVIEW_DOMAIN_BACKEND_GL",
        "CLASSIC_SUBVIEW_DOMAIN_BACKEND_VULKAN",
        "R_ClassicSubviewDomain_ResetFrame",
        "R_ClassicSubviewDomain_PrepareFrame",
        "R_ClassicSubviewDomain_CaptureMatches",
        "R_ClassicSubviewDomain_IsCaptureBacked",
        "R_ClassicSubviewDomain_IsDirect",
        "R_ClassicSubviewDomain_ViewSemanticsMatch",
        "R_ClassicSubviewDomain_ReadyForBackend",
        "R_ClassicSubviewDomain_RecordOwned",
        "R_ClassicSubviewDomain_RecordDirectOwned",
        "R_ClassicSubviewDomain_RecordBackendFallback",
        "RendererClassicSubviewDomain_RunSelfTest",
    ):
        require(combined, token, "shared subview domain API")
    for token in (
        "FindScenePacket( packetFrame, view.parentViewDef )",
        "ParentContainsSurface",
        "FindDirectKind",
        "FindCaptureKind",
        "ClassifyCaptureTarget",
        "DI_REMOTE_RENDER",
        "DI_MIRROR_RENDER",
        "DI_REFLECTION_RENDER",
        "DI_REFRACTION_RENDER",
        "DI_XRAY_RENDER",
        "HasSupportedSpecialSemantics",
        "CaptureViewSemantics",
        "ViewSemanticsMatch",
        "ResolveNestedOwnership",
        "parentViewIndex",
        "rootViewIndex",
        "nestingDepth",
        "subtreeViewCount",
        "backendCompleted",
        "FinalizeTransaction",
        "PublishOwned",
        "RecordSingleFallback",
        "ownedNestedTransactions",
        "fallbackNestedTransactions",
        "nestedTopology",
        "semanticClipPlanes",
        "viewDef->isMirror",
        "viewDef->isXraySubview",
        "capture.copyDepth",
        "capture.cubeFace",
        "TT_CUBIC",
        "FMT_DEPTH_STENCIL",
        "captureTextureType",
        "captureTextureFormat",
        "captureCopyDepth",
        "ownedDepthCubemapCaptures",
        "capture.x != viewDef->viewport.x1",
        "ready = true",
        "fallbackViews",
        "nestedCommandOrder",
        "backendNestingIncomplete",
        "nestedCinematicPostFallback",
        "backendNestedCinematicPostIncomplete",
        "!domain.stats.prepared || domain.stats.overflow",
        "R_ClassicCinematicPostDomain_SubviewTransactionReady",
        "R_ClassicCinematicPostDomain_SubviewTransactionCompleted",
        "R_ClassicCinematicPostDomain_PublishSubviewTransactionOwned",
        "R_ClassicCinematicPostDomain_RecordSubviewTransactionFallback",
        "classic subview and cinematic/post GL backend ordinals must match",
        "classic subview and cinematic/post Vulkan backend ordinals must match",
        "classic subview and cinematic/post backend counts must match",
        "emptyCinematicFrame.MarkBackendDerived()",
        "R_ClassicCinematicPostDomain_PrepareFrame( emptyCinematicFrame )",
    ):
        require(source, token, "bounded subview admission and fallback")
    ready = braced_body(
        source,
        "bool R_ClassicSubviewDomain_ReadyForBackend(",
        "subview backend readiness",
    )
    require(ready, "view.backendCompleted[backend]", "completed-view readiness seal")
    for marker, context in (
        ("bool R_ClassicSubviewDomain_RecordOwned(", "capture ownership reporting"),
        ("bool R_ClassicSubviewDomain_RecordDirectOwned(", "direct ownership reporting"),
    ):
        reporter = braced_body(source, marker, context)
        require(
            reporter,
            "|| view->backendCompleted[backend]",
            f"{context} pre-publication duplicate guard",
        )
    require(source, "nestedChildReadyInitially", "nested duplicate native regression")
    require(source, "nestedChildNotReadyTwice", "nested duplicate native regression")
    require(source, "nestedDuplicateSealed", "nested duplicate native regression")


def validate_backend_capture_ownership() -> None:
    gl = read("src/renderer/tr_backend.cpp")
    vk = read("src/renderer/Vulkan/vk_Backend.cpp")
    image_gl = read("src/renderer/Image_load.cpp")
    image_vk = read("src/renderer/Vulkan/vk_Image.cpp")
    executor_vk = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    for source, backend in ((gl, "GL"), (vk, "Vulkan")):
        for token in (
            "R_ClassicSubviewDomain_PrepareFrame( *scenePackets );",
            "R_ClassicSubviewDomain_CaptureMatches",
            "R_ClassicSubviewDomain_RecordOwned",
            "R_ClassicSubviewDomain_IsCaptureBacked",
            "R_ClassicSubviewDomain_IsDirect",
            "R_ClassicSubviewDomain_RecordDirectOwned",
            "R_ClassicSubviewDomain_RecordBackendFallback",
            "R_ClassicSubviewDomain_ReadyForBackend",
            "pendingSharedSubview",
            "== CLASSIC_SUBVIEW_DOMAIN_BACKEND_UNRECORDED",
        ):
            require(source, token, f"{backend} capture ownership")
        copy_case = braced_body(source, "case RC_COPY_RENDER:", f"{backend} copy transaction")
        require(copy_case, "sharedCaptureMatches", f"{backend} sealed copy decision")
        require(copy_case, "pendingSharedSubview->captureImage", f"{backend} sealed capture image")
        require(copy_case, "RecordOwned", f"{backend} post-copy ownership")
    require(gl, "RB_ClassicSubview_CopyOwned", "GL sealed capture consumer")
    require(gl, "RB_DrawSharedDirectSubview", "GL direct subview consumer")
    for token in (
        "CopyFramebuffer( view.captureX, view.captureY",
        "CopyDepthbuffer( view.captureX, view.captureY",
        "view.captureCubeFace",
        "CopyFramebuffer( cmd->x, cmd->y, cmd->imageWidth",
        "cmd->cubeFace",
    ):
        require(gl, token, "GL exact capture dispatch")
    for token in (
        "GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT + cubeFace",
        "CopyDepthbuffer",
        "glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, copyTarget",
    ):
        require(image_gl, token, "GL cubemap/depth transfer")
    require(vk, "VK_Exec_CopyRender(", "Vulkan capture consumer")
    require(vk, "VK_DrawSharedDirectSubview", "Vulkan direct subview consumer")
    for token in (
        "VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT",
        "VK_IMAGE_VIEW_TYPE_CUBE",
        "entry->isCube = isCube",
    ):
        require(image_vk, token, "Vulkan depth cubemap target")
    for token in (
        "targetIsCube",
        "targetIsDepth",
        "cubeFace < 0 || cubeFace >= 6",
        "rows[ row ].dstSubresource.baseArrayLayer = targetIsCube",
        "region.dstSubresource.baseArrayLayer = targetIsCube",
    ):
        require(executor_vk, token, "Vulkan exact target/aspect/face transfer")


def validate_controls_diagnostics_and_registration() -> None:
    init = read("src/renderer/RenderSystem_init.cpp")
    local = read("src/renderer/tr_local.h")
    bootstrap = read("src/renderer/RendererBootstrap.cpp")
    matrix = read("tools/tests/renderer_validation_matrix.py")
    baseline = read("tools/validation/stock_asset_baseline.py")
    benchmark = read("tools/tests/renderer_gameplay_benchmark.py")
    registry = read("tools/validation/openq4_validate.py")
    docs = read("docs/dev/classic-subview-domain-modernization.md")
    for token in (
        'r_rendererSharedSubview( "r_rendererSharedSubview", "0"',
        '"rendererClassicSubviewDomainSelfTest"',
        "RendererClassicSubviewDomain self-test passed",
        "classicSubviewDomain requested=",
        "classicSubviewDomain backend=",
        "colorCubemap=",
        "depthCubemap=",
        "nestedTransactions=",
        "maxNestingDepth=",
    ):
        require(init, token, "default-off subview control and diagnostics")
    require(local, "extern idCVar r_rendererSharedSubview;", "renderer cvar API")
    require(bootstrap, '"r_rendererSharedSubview", &r_rendererSharedSubview, 0', "bootstrap default")
    for source, context in (
        (matrix, "renderer validation matrix"),
        (baseline, "stock-baseline isolation"),
        (benchmark, "benchmark isolation"),
    ):
        require(source, "r_rendererSharedSubview", context)
    require(registry, '"renderer_classic_subview_domain.py"', "validation registry")
    for token in (
        "capture-backed",
        "cubemap",
        "depth capture",
        "unsupportedCaptureTarget",
        "direct",
        "remote-camera",
        "reflection",
        "x-ray",
        "refraction",
        "mirror",
        "r_rendererSharedSubview 1",
        "nested",
        "atomic",
        "classic fallback",
    ):
        require(docs, token, "subview contract documentation")
    for workflow_path in (
        ".github/workflows/commit-validation.yml",
        ".github/workflows/push-verification.yml",
    ):
        workflow = read(workflow_path)
        if workflow.count(TEST_PATH) != 2:
            raise AssertionError(
                f"Expected compile and execution registration in {workflow_path}"
            )


def main() -> int:
    validate_packet_capture_edge()
    validate_domain_contract()
    validate_backend_capture_ownership()
    validate_controls_diagnostics_and_registration()
    print("renderer_classic_subview_domain: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
