#!/usr/bin/env python3
"""Static integration contract for Milestone C renderer/GPU animation work."""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src" / "renderer"
GAME_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", str(ROOT.parent / "openQ4-game"))
).resolve()


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required file is missing: {path}")
    return path.read_text(encoding="utf-8")


def require(source: str, token: str, label: str) -> None:
    if token not in source:
        raise AssertionError(f"missing {label}: {token!r}")


def require_all(source: str, tokens: tuple[str, ...], label: str) -> None:
    for token in tokens:
        require(source, token, label)


def reject(source: str, token: str, label: str) -> None:
    if token in source:
        raise AssertionError(f"forbidden {label}: {token!r}")


def require_regex(source: str, pattern: str, label: str) -> None:
    if re.search(pattern, source, re.MULTILINE) is None:
        raise AssertionError(f"missing {label}: /{pattern}/")


def reject_regex(source: str, pattern: str, label: str) -> None:
    if re.search(pattern, source, re.MULTILINE | re.IGNORECASE) is not None:
        raise AssertionError(f"forbidden {label}: /{pattern}/")


def require_order(source: str, tokens: tuple[str, ...], label: str) -> None:
    cursor = -1
    for token in tokens:
        position = source.find(token, cursor + 1)
        if position < 0:
            raise AssertionError(f"missing ordered {label}: {token!r}")
        if position <= cursor:
            raise AssertionError(f"out-of-order {label}: {tokens!r}")
        cursor = position


def braced_body(source: str, marker: str, label: str) -> str:
    start = source.find(marker)
    if start < 0:
        raise AssertionError(f"missing {label}: {marker!r}")
    opening = source.find("{", start + len(marker))
    if opening < 0:
        raise AssertionError(f"missing body for {label}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated body for {label}")


def normalized_source(source: str) -> str:
    return source.replace("\r\n", "\n").replace("\r", "\n")


def validate_shared_renderer_contracts() -> None:
    header = read(RENDERER / "RendererContracts.h")
    source = read(RENDERER / "RendererContracts.cpp")
    native = read(ROOT / "tools" / "tests" / "native" / "RendererContractsTest.cpp")
    meson = read(ROOT / "meson.build")
    gl_executor = read(RENDERER / "ModernGLExecutor.cpp")
    vk_executor = read(RENDERER / "Vulkan" / "vk_GuiExecutor.cpp")

    for backend_token in ("GLuint", "GLenum", "VkBuffer", "VkFormat", "vulkan.h"):
        reject(header, backend_token, "backend type in shared renderer contract")

    require_all(
        header,
        (
            "rendererMaterialPass_t",
            "sourceStageIndex",
            "textureResourceId",
            "rendererRegisterRef_t\t\t\tcondition",
            "rendererRegisterRef_t\t\t\tcolor[ 4 ]",
            "rendererRegisterRef_t\t\t\ttextureMatrix[ 6 ]",
            "rendererBlendState_t",
            "rendererDepthState_t",
            "colorWriteMask",
            "alphaTestEnabled",
            "polygonOffsetEnabled",
            "programFamily",
            "RendererContracts_AppendMaterialPass",
        ),
        "ordered material/pass contract",
    )
    require_all(
        source + native,
        (
            "passes.passes[ 0 ].order != 0",
            "passes.passes[ 1 ].order != 1",
            "repeated texture semantic must append",
            "repeated semantics must remain distinct",
            "bounded material list must fail closed at capacity",
        ),
        "ordered/repeated material self-test",
    )

    require_all(
        header + source,
        (
            "RENDERER_CLIP_DEPTH_NEGATIVE_ONE_TO_ONE",
            "RENDERER_CLIP_DEPTH_ZERO_TO_ONE",
            "RENDERER_VIEWPORT_Y_NEGATIVE",
            "RendererContracts_GLClipSpace",
            "RendererContracts_VulkanClipSpace",
            "RendererContracts_ConvertClipMatrix",
            "RendererContracts_BuildViewport",
            "destination[ z ] = 0.5f * ( canonical[ z ] + canonical[ w ] )",
        ),
        "explicit clip-space convention",
    )
    require_all(
        vk_executor,
        (
            "RendererContracts_ConvertClipMatrix",
            "RendererContracts_GLClipSpace(), RendererContracts_VulkanClipSpace()",
            "RendererContracts_BuildViewport",
            "RendererContracts_LegacyDrawVertLayout",
        ),
        "Vulkan shared clip/layout consumption",
    )
    require(gl_executor, "RendererContracts_LegacyDrawVertLayout", "OpenGL shared vertex layout consumption")

    require_all(
        header + source + native,
        (
            "RENDERER_VERTEX_SEMANTIC_JOINT_INDICES",
            "RENDERER_VERTEX_SEMANTIC_JOINT_WEIGHTS",
            "RENDERER_VERTEX_FORMAT_UINT32X4",
            "RendererContracts_SkinnedDrawVertLayout",
            "bindings[ 1 ].stride == 32",
            "weights->binding == 1 && weights->offset == 16",
            "RENDERER_BUFFER_KIND_JOINT_PALETTE",
            "RENDERER_BUFFER_LIFETIME_FRAME",
            "RENDERER_BUFFER_SLICE_STALE_GENERATION",
            "RENDERER_BUFFER_SLICE_EXPIRED",
            "RENDERER_BUFFER_SLICE_RANGE_OVERFLOW",
            "static_assert( sizeof( rendererBufferHandle_t )",
        ),
        "semantic layout and typed generational buffer contract",
    )
    require(meson, "'openq4-renderer-contracts-test'", "native renderer contract target")
    require(meson, "'src/renderer/RendererContracts.cpp'", "native renderer contract source")


def validate_four_weight_contract() -> None:
    header = read(RENDERER / "GpuSkinning.h")
    source = read(RENDERER / "GpuSkinning.cpp")

    require_all(
        header,
        (
            "GPU_SKINNING_INFLUENCES = 4",
            "uint32\tjointIndices[ GPU_SKINNING_INFLUENCES ]",
            "float\tjointWeights[ GPU_SKINNING_INFLUENCES ]",
            "GPU_SKINNING_JOINT_FLOATS = 12",
            "GPU_SKINNING_FALLBACK_RESIDUAL_WEIGHTS",
            "GPU_SKINNING_FALLBACK_JOINT_INDEX",
            "GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS",
            "GPU_SKINNING_FALLBACK_STENCIL_VOLUME",
            "GPU_SKINNING_FALLBACK_DECAL_OVERLAY",
            "GPU_SKINNING_FALLBACK_STALE_PALETTE",
            "R_GpuSkinning_PackVertexExact",
            "R_GpuSkinning_ValidateSurface",
            "R_GpuSkinning_RunSelfTest",
        ),
        "exact four-weight API",
    )
    packer = braced_body(source, "bool R_GpuSkinning_PackVertexExact(", "exact four-weight packer")
    require_all(
        packer,
        (
            "!std::isfinite( weight )",
            "!allowSignedWeights && weight < 0.0f",
            "influences[i].jointIndex >= static_cast<uint32>( numJoints )",
            "result.meaningfulInfluences > GPU_SKINNING_INFLUENCES",
            "GPU_SKINNING_FALLBACK_RESIDUAL_WEIGHTS",
            "packed.jointIndices[ packedIndex ] = influences[i].jointIndex",
            "packed.jointWeights[ packedIndex ] = weight",
        ),
        "exact packer validation",
    )
    for forbidden in (r"\bsort\s*\(", r"normali[sz]", r"\bclamp"):
        reject_regex(packer, forbidden, "four-weight truncation/renormalization")

    deform = braced_body(source, "bool R_GpuSkinning_DeformVertexCPU(", "CPU parity deform")
    require_order(
        deform,
        (
            "deformed = bindPose;",
            "deformed.xyz = position;",
            "deformed.normal = normal;",
            "deformed.tangents[0] = tangent0;",
            "deformed.tangents[1] = tangent1;",
        ),
        "color-preserving CPU parity deform",
    )
    reject(deform, "SetColor", "CPU parity color mutation")
    require_all(
        source,
        (
            "source[valueIndex]",
            "gpuSkinningJointPalette",
            "gpuSkinningPaletteGeneration = rg_gpuSkinningGeneration",
            "surface.palette.generation != rg_gpuSkinningGeneration",
            "R_BackendGpuSkinning_PrepareAmbientCache",
            '"GPU skinning: enabled=%d',
            "cumulative=1",
            '"GPU skinning fallbacks:',
        ),
        "copied joint palette, backend seam, and stable diagnostics",
    )

    self_test = braced_body(source, "bool R_GpuSkinning_RunSelfTest(", "GPU skinning self-test")
    require_all(
        self_test,
        (
            "R_GpuSkinning_PackVertexExact( influences, 4, 4, false",
            "R_GpuSkinning_PackVertexExact( influences, 5, 4, false",
            "GPU_SKINNING_FALLBACK_RESIDUAL_WEIGHTS",
            "R_GpuSkinning_PackVertexExact( influences, 1, 4, true",
            "deformed.GetColor() != bindPose.GetColor()",
            "R_GpuSkinning_CompareVertexCPU",
        ),
        "rigid/exact/residual/signed/color parity self-test",
    )


def validate_model_integration_and_ownership() -> None:
    model_header = read(RENDERER / "Model.h")
    companion_path = GAME_ROOT / "src" / "renderer" / "Model.h"
    companion_header = read(companion_path)
    if normalized_source(model_header) != normalized_source(companion_header):
        raise AssertionError(
            f"engine and companion srfTriangles_t ABI headers differ: {companion_path}"
        )
    require_all(
        model_header,
        (
            "gpuSkinningBindPoseVerts",
            "gpuSkinningVerts",
            "numGpuSkinningVerts",
            "gpuSkinningJointPalette",
            "gpuSkinningJointPaletteAlloc",
            "numGpuSkinningJoints",
            "gpuSkinningPaletteGeneration",
            "gpuSkinningFallbackReason",
            "gpuSkinningSignedWeights",
        ),
        "mirrored triangle-surface GPU contract ABI",
    )

    geometry_header = read(ROOT / "src" / "render_geo" / "RenderGeometry.h")
    geometry = read(ROOT / "src" / "render_geo" / "RenderGeometryTriSurf.cpp")
    require_all(
        geometry_header + geometry,
        (
            "R_AllocStaticGpuSkinningJointPalette",
            "R_ClearStaticGpuSkinningJointPalette",
            "R_ReferenceStaticGpuSkinning",
            "R_CopyStaticGpuSkinning",
            "gpuSkinningJointPaletteAllocator.Free",
            "gpuSkinningJointPaletteAlloc = NULL",
        ),
        "triangle-surface palette ownership",
    )
    reference = braced_body(
        geometry, "void R_ReferenceStaticGpuSkinning(", "GPU skinning reference ownership"
    )
    reject(reference, "gpuSkinningJointPaletteAlloc = reference", "borrowed palette allocation marker")
    copied = braced_body(
        geometry, "bool R_CopyStaticGpuSkinning(", "GPU skinning copied ownership"
    )
    require(copied, "memcpy( tri->gpuSkinningJointPalette", "deep copied palette")

    model_local = read(RENDERER / "Model_local.h")
    md5 = read(RENDERER / "Model_md5.cpp")
    require_all(
        model_local + md5,
        (
            "idList<idDrawVert>\t\t\tgpuBindPoseVerts",
            "idList<gpuSkinningVertex_t> gpuSkinningVerts",
            "BuildGpuSkinningSidecar",
            "R_GpuSkinning_PackVertexExact",
            "false, gpuSkinningVerts[vertexIndex]",
            "deformInfo->mirroredVerts",
            "R_GpuSkinning_AttachSurfaceContract",
            "GPU_SKINNING_FALLBACK_SKIN_SCALE",
            "SIMDProcessor->TransformVertsNew( tri->verts",
            "R_GpuSkinning_RecordCpuSkinning",
        ),
        "classic MD5 exact sidecar and retained CPU positions",
    )
    update = braced_body(md5, "void idMD5Mesh::UpdateSurface(", "classic MD5 update")
    require_order(
        update,
        (
            "R_GpuSkinning_AttachSurfaceContract(",
            "SIMDProcessor->TransformVertsNew( tri->verts",
            "R_GpuSkinning_RecordCpuSkinning",
        ),
        "classic MD5 GPU contract plus CPU position ownership",
    )
    require_all(
        md5,
        (
            "r_useNewSkinning.GetBool() && !collisionOnly",
            "mesh->UpdateSurface( ent, entJoints, surf, !collisionOnly,",
            "!collisionOnly && gpuJointPaletteReady && !shader->HasGui() );",
        ),
        "classic MD5 collision and GUI surface CPU skinning",
    )

    md5r = read(RENDERER / "Model_md5r.cpp")
    require_all(
        md5r,
        (
            "R_MD5R_GetImplicitBlendWeightIndex",
            "R_MD5R_GetSkinningBlendWeight",
            "idMath::Fabs( blendWeights[ influenceIndex ] )",
            "R_GpuSkinning_PackVertexExact",
            "true,",
            "R_GpuSkinning_AttachSurfaceContract",
            "R_GpuSkinning_RecordCpuSkinning",
        ),
        "MD5R signed exact sidecar and CPU parity",
    )
    signed_weight = braced_body(
        md5r,
        "static ID_INLINE float R_MD5R_GetSkinningBlendWeight(",
        "MD5R signed implicit fourth weight",
    )
    require_order(
        signed_weight,
        (
            "influenceIndex == implicitWeightIndex",
            "blendWeights[ influenceIndex ]",
            "idMath::Fabs( blendWeights[ influenceIndex ] )",
        ),
        "signed implicit/fabs authored MD5R weights",
    )
    md5r_sidecar = braced_body(
        md5r,
        "void rvRenderModelMD5R::BuildGpuSkinningSidecar(",
        "MD5R immutable GPU sidecar",
    )
    reject(md5r_sidecar, ".color2", "MD5R authored secondary-color mutation")
    reject(md5r_sidecar, "SetColor", "MD5R authored primary-color mutation")
    require_all(
        md5r,
        (
            "const bool allowGpuSkinning = !collisionOnly && !shader->HasGui();",
            "UpdateDynamicSurface( mesh, entJoints, *surface, !collisionOnly, skinScale, allowGpuSkinning )",
        ),
        "MD5R collision and GUI surface CPU skinning",
    )


def validate_backend_execution_and_cpu_invariants() -> None:
    shared = read(RENDERER / "GpuSkinning.cpp")
    tr_light = read(RENDERER / "tr_light.cpp")
    gl = read(RENDERER / "OpenGL" / "GpuSkinningGL.cpp")
    vk = read(RENDERER / "Vulkan" / "vk_GuiExecutor.cpp")
    interaction = read(RENDERER / "Interaction.cpp")
    gl_interactions = read(RENDERER / "draw_arb2.cpp")
    vk_interactions = read(RENDERER / "Vulkan" / "vk_Interactions.cpp")
    vk_shader = read(RENDERER / "Vulkan" / "shaders" / "gpu_skinning.comp")
    shader_header = read(RENDERER / "Vulkan" / "shaders" / "gui_shaders_spv.h")
    shader_pin = read(ROOT / "tools" / "tests" / "vk_shader_header_pin.py")

    ambient = braced_body(tr_light, "bool R_CreateAmbientCache(", "ambient cache creation")
    require_order(
        ambient,
        (
            "if ( tri->ambientCache )",
            "R_GpuSkinning_PrepareAmbientCache( tri, needsLighting )",
            "R_DeriveTangents( tri )",
            "vertexCache.Alloc( tri->verts",
        ),
        "GPU ambient admission before complete CPU fallback",
    )
    attach = braced_body(shared, "bool R_GpuSkinning_AttachSurfaceContract(", "surface attachment")
    reject(attach, "tri->verts =", "GPU contract mutation of CPU geometry")
    shadow_volume = read(RENDERER / "tr_turboshadow.cpp")
    require(
        shadow_volume,
        "SIMDProcessor->CreateShadowCache( &shadowVerts->xyz, vertRemap, localLightOrigin, tri->verts",
        "CPU stencil-volume geometry",
    )
    require_regex(
        tr_light + shadow_volume + interaction + gl_interactions + vk_interactions + vk,
        r"R_GpuSkinning_RecordFallback\s*\(\s*GPU_SKINNING_FALLBACK_STENCIL_VOLUME\s*\)",
        "classified CPU stencil-volume fallback",
    )

    require_all(
        gl,
        (
            "void R_BackendGpuSkinning_Init",
            "bool R_BackendGpuSkinning_PrepareAmbientCache",
            "caps.hasCompute",
            "caps.hasSSBO",
            "GL_COMPUTE_SHADER",
            "layout(std430, binding = 0) readonly buffer SourceWords",
            "layout(std430, binding = 1) readonly buffer SkinWords",
            "layout(std430, binding = 2) readonly buffer JointWords",
            "layout(std430, binding = 3) writeonly buffer OutputWords",
            "for (uint word = 0u; word < 16u; ++word)",
            "vertexCache.AllocFrameTemp",
            "glDispatchCompute",
            "GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT",
            "tri->ambientCache = outputCache",
        ),
        "OpenGL bounded compute corridor",
    )
    expected_store_offsets = ["0u", "4u", "8u", "11u"]
    gl_store_offsets = re.findall(
        r"StoreVec3\s*\(\s*outputBase\s*,\s*([0-9]+u)\s*,", gl
    )
    if gl_store_offsets != expected_store_offsets:
        raise AssertionError(
            "OpenGL compute must overwrite only position/normal/tangent words: "
            f"{gl_store_offsets!r}"
        )

    require_all(
        vk + vk_shader + shader_header + shader_pin,
        (
            "vk_gpu_skinning_comp_spv",
            "VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT",
            "VK_GpuSkinning_PrepareView",
            "VK_Ring_Reserve",
            "VK_PIPELINE_STAGE_2_HOST_BIT",
            "VK_ACCESS_2_HOST_WRITE_BIT",
            "VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT",
            "VK_ACCESS_2_SHADER_WRITE_BIT",
            "VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT",
            "VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT",
            "vkCmdDispatch",
            "R_GpuSkinning_RecordBackendPrepared",
            "void R_BackendGpuSkinning_Init",
            "bool R_BackendGpuSkinning_PrepareAmbientCache",
            "gpu_skinning.comp",
        ),
        "Vulkan bounded compute corridor and generated shader pin",
    )
    require_all(
        vk_shader,
        (
            "for ( uint word = 0u; word < 16u; ++word )",
            "outputData.words[ outputBase + word ] = sourceData.words[ sourceBase + word ]",
        ),
        "Vulkan verbatim draw-vertex copy",
    )
    vk_store_offsets = re.findall(
        r"StoreVec3\s*\(\s*outputBase\s*,\s*([0-9]+u)\s*,", vk_shader
    )
    if vk_store_offsets != expected_store_offsets:
        raise AssertionError(
            "Vulkan compute must overwrite only position/normal/tangent words: "
            f"{vk_store_offsets!r}"
        )
    upload_tri = braced_body(
        vk,
        "static bool VK_Exec_UploadTriGeometry(",
        "Vulkan visible geometry upload",
    )
    require(upload_tri, "gpuSkinningMemo", "Vulkan visible GPU deformation memo")
    bind_tri = braced_body(vk, "bool VK_Exec_BindTriGeometry(", "Vulkan visible geometry binding")
    require(bind_tri, "VK_Exec_UploadTriGeometry(", "Vulkan validated visible geometry upload")
    bind_shadow = braced_body(vk, "bool VK_Exec_BindShadowGeometry(", "Vulkan stencil geometry binding")
    require(bind_shadow, "tri->shadowCache", "Vulkan CPU stencil cache")
    for token in ("gpuSkinningMemo", "VK_GpuSkinning_PrepareView", "vkCmdDispatch"):
        reject(bind_shadow, token, "GPU-deformed stencil volume")


def validate_runtime_commands_matrix_and_docs() -> None:
    render_init = read(RENDERER / "RenderSystem_init.cpp")
    matrix = read(ROOT / "tools" / "tests" / "renderer_validation_matrix.py")
    docs = {
        "roadmap": read(ROOT / "docs" / "dev" / "idtech5-modernization-roadmap.md"),
        "capability matrix": read(ROOT / "docs" / "dev" / "engine-capability-matrix.md"),
        "GPU modernization": read(ROOT / "docs" / "dev" / "gpu-skinning-modernization.md"),
        "validation matrix": read(ROOT / "docs" / "dev" / "renderer-validation-matrix.md"),
        "release completion": read(ROOT / "docs" / "dev" / "release-completion.md"),
        "release notes": read(ROOT / "docs" / "dev" / "releases" / "v0.12.0.md"),
    }
    require_all(
        render_init,
        (
            'cmdSystem->AddCommand( "rendererContractsSelfTest"',
            'cmdSystem->AddCommand( "rendererGpuSkinningSelfTest"',
            '"RendererContracts self-test passed\\n"',
            '"RendererGpuSkinning self-test passed\\n"',
            "R_GpuSkinning_ContractInit",
            "R_GpuSkinning_ContractShutdown",
            "R_GpuSkinning_PrintGfxInfo",
        ),
        "opt-in CVar, lifecycle, runtime self-tests, and diagnostics",
    )
    require_regex(
        render_init,
        r'idCVar\s+r_gpuSkinning\s*\(\s*"r_gpuSkinning"\s*,\s*"0"\s*,\s*'
        r'CVAR_RENDERER\s*\|\s*CVAR_ARCHIVE\s*\|\s*CVAR_BOOL',
        "default-off archived GPU skinning CVar",
    )
    require_all(
        matrix,
        (
            '"RendererContracts self-test passed"',
            '"RendererGpuSkinning self-test passed"',
            '"GPU skinning:"',
            '"+rendererContractsSelfTest"',
            '"+rendererGpuSkinningSelfTest"',
        ),
        "renderer validation matrix GPU contract coverage",
    )
    doc_tokens = {
        "roadmap": ("Milestone C", "four-weight", "CPU fallback", "complete"),
        "capability matrix": ("GPU", "skinning", "OpenGL", "Vulkan"),
        "GPU modernization": (
            "r_gpuSkinning=0",
            "r_gpuSkinning=1",
            "--shadow-presets stencil",
            "--shadow-presets mapped",
            "collision",
            "stencil",
            "gpu_skinning_evidence.py",
        ),
        "validation matrix": ("GPU animation", "r_gpuSkinning 0", "r_gpuSkinning 1"),
        "release completion": ("GPU", "skinning", "four-weight"),
        "release notes": ("GPU animation", "r_gpuSkinning 1", "collision"),
    }
    for name, tokens in doc_tokens.items():
        require_all(docs[name], tokens, name)


def validate_source_discovery() -> None:
    source_tool = ROOT / "tools" / "build" / "meson_sources.py"
    for emitter in ("renderer_gl", "renderer_vk"):
        completed = subprocess.run(
            [sys.executable, str(source_tool), "--emit", emitter],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            raise AssertionError(
                f"{emitter} source discovery failed:\n{completed.stdout}{completed.stderr}"
            )
        sources = completed.stdout.splitlines()
        if sources.count("src/renderer/GpuSkinning.cpp") != 1:
            raise AssertionError(f"{emitter} must compile shared GpuSkinning.cpp exactly once")
        if sources.count("src/renderer/RendererContracts.cpp") != 1:
            raise AssertionError(f"{emitter} must compile RendererContracts.cpp exactly once")
        gl_adapter_count = sources.count("src/renderer/OpenGL/GpuSkinningGL.cpp")
        expected = 1 if emitter == "renderer_gl" else 0
        if gl_adapter_count != expected:
            raise AssertionError(
                f"{emitter} OpenGL GPU adapter count is {gl_adapter_count}, expected {expected}"
            )


def validate_paired_evidence_verifier() -> None:
    verifier = ROOT / "tools" / "validation" / "gpu_skinning_evidence.py"
    source = read(verifier)
    require_all(
        source,
        (
            "openq4-gpu-skinning-paired-evidence",
            "minimum-cpu-p95-improvement",
            "GPU skinning fallbacks:",
            "stencilVolume",
            "compare_images",
            "run_self_test",
        ),
        "paired GPU skinning evidence verifier",
    )
    completed = subprocess.run(
        [sys.executable, str(verifier), "--self-test"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0 or "paired evidence self-test: PASS" not in completed.stdout:
        raise AssertionError(
            "GPU skinning paired evidence self-test failed:\n"
            f"{completed.stdout}{completed.stderr}"
        )


def main() -> int:
    validate_shared_renderer_contracts()
    validate_four_weight_contract()
    validate_model_integration_and_ownership()
    validate_backend_execution_and_cpu_invariants()
    validate_runtime_commands_matrix_and_docs()
    validate_source_discovery()
    validate_paired_evidence_verifier()
    print("renderer GPU skinning contracts: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
