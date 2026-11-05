#!/usr/bin/env python3
"""Pin the committed Vulkan shader header to its GLSL sources.

The renderer-vk module embeds SPIR-V from a COMMITTED generated header so
builds never require glslang (CI runners carry no Vulkan SDK). When
glslangValidator IS available, this test regenerates the header and
byte-compares; when it is not, the test skips cleanly.
"""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = REPO_ROOT / "tools" / "build" / "spirv_to_header.py"
COMMITTED = REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "gui_shaders_spv.h"
SHADERS = [
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "gpu_skinning.comp",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "gui.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "gui.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "screen.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "screen.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "sky.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "sky.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "environment.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "environment.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "bumpy_environment.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "bumpy_environment.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "heathaze.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "heathaze.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "heathaze_mask.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "heathaze_vertex.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "heathaze_mask_vertex.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "monochrome.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "monochrome.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "glasswarp.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "glasswarp.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_displacement.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_displacement.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_displacement_two_stage.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_displacement_two_stage.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_ghost_pulling.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_ghost_pulling.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_displacement2.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_displacement2.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_multiply_blend.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_multiply_blend.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_displacement_cube.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_displacement_cube.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_sniper_stretch2.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_sniper_stretch2.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_depth_texture.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_depth_texture.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_blur.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_blur.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_medlabs.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_medlabs.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_depth_texture2.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_depth_texture2.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_al.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_al.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_water.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_water.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "refractive_glass.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "refractive_glass.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_depth_aware_blur.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_depth_aware_blur.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_smaa_edge.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_smaa_edge.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_smaa_weights.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_smaa_weights.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_smaa_blend.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_smaa_blend.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "interaction.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "interaction.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "pbr_probe_environment.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "pbr_baked_environment.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "pbr_baked_probe_environment.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "interaction_shadow.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "interaction_shadow.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "interaction_shadow_point.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "interaction_shadow_point.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_caster.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_caster.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_point_caster.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_point_caster.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_moment_caster.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_moment_caster.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_point_moment_caster.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_point_moment_caster.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_volume.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_volume.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_debug_overlay.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_debug_overlay.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_debug_overlay_point.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "shadow_debug_overlay_text.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "fog.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "fog.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "blend_light.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "blend_light.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "soft_particle.frag",
]

# The full-screen post passes (vk_PostProcess.cpp) embed their own header, so
# only that translation unit carries their SPIR-V.
POST_COMMITTED = REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_shaders_spv.h"
POST_GUARD = "__VK_POST_SHADERS_SPV_H__"
POST_SHADERS = [
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_fullscreen.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_crt.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_ssao.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_bloom_extract.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_bloom_downsample.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_bloom_blur.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_bloom_composite.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_motionblur.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_motionvectors.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_motionvectors.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_motionvectors_previous.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_motionvectors_reactive.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_motionvectors_reactive.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_celoutline.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_underwater.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_debug_view.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "post_skymask.frag",
]

# The scene overlays (vk_SceneEffects.cpp): light grid, player visibility
# effects and cel outline shells.
SCENE_COMMITTED = REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "scene_shaders_spv.h"
SCENE_GUARD = "__VK_SCENE_SHADERS_SPV_H__"
SCENE_SHADERS = [
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "lightgrid_indirect.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "lightgrid_indirect.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "player_outline.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "player_outline.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "player_rimlight.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "player_rimlight.frag",
]

# The debug tools' fixed-function emulation (vk_DebugTools.cpp).
DEBUG_COMMITTED = REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "debug_shaders_spv.h"
DEBUG_GUARD = "__VK_DEBUG_SHADERS_SPV_H__"
DEBUG_SHADERS = [
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "debug_draw.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "debug_draw.frag",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "debug_draw_textured.frag",
]

# Headers embedded by a single translation unit each, with their own guards.
EXTRA_HEADERS = [
    (
        REPO_ROOT / "src/renderer/Vulkan/shaders/hdr_scene_spv.h",
        [REPO_ROOT / "src/renderer/Vulkan/shaders" / name for name in
         ("hdr_scene.vert", "hdr_scene_seed.frag", "hdr_scene_seed_ms.frag",
          "hdr_scene_combine.frag", "hdr_scene_combine_ms.frag", "hdr_scene_test.frag",
          "hdr_scene_test_seed.frag", "hdr_scene_preview.frag", "hdr_scene_preview_ms.frag")],
        "__VK_HDR_SCENE_SPV_H__",
    ),
    (
        REPO_ROOT / "src/renderer/Vulkan/shaders/hdr_luminance_spv.h",
        [REPO_ROOT / "src/renderer/Vulkan/shaders/post_hdr_luminance.frag"],
        "__VK_HDR_LUMINANCE_SPV_H__",
    ),
    ( POST_COMMITTED, POST_SHADERS, POST_GUARD ),
    ( SCENE_COMMITTED, SCENE_SHADERS, SCENE_GUARD ),
    ( DEBUG_COMMITTED, DEBUG_SHADERS, DEBUG_GUARD ),
    (
        REPO_ROOT / "src/renderer/Vulkan/shaders/color_resolve_spv.h",
        [REPO_ROOT / "src/renderer/Vulkan/shaders" / name for name in
         ("color_resolve.vert", "color_resolve.frag")],
        "__VK_COLOR_RESOLVE_SPV_H__",
    ),
    (
        REPO_ROOT / "src/renderer/Vulkan/shaders/target_test_spv.h",
        [REPO_ROOT / "src/renderer/Vulkan/shaders" / name for name in
         ("target_test.vert", "target_test.frag", "target_test_five.frag", "target_test_resolve.frag")],
        "__VK_TARGET_TEST_SPV_H__",
    ),
]


HDR_DOMAIN_COMMITTED = REPO_ROOT / "src/renderer/Vulkan/shaders/hdr_domain_spv.h"
HDR_DOMAIN_SHADERS = [REPO_ROOT / "src/renderer/Vulkan/shaders" / name for name in
    ("gui.frag", "interaction.frag", "interaction_shadow.frag",
     "interaction_shadow_point.frag", "pbr_probe_environment.frag",
     "pbr_baked_environment.frag", "pbr_baked_probe_environment.frag")]
HDR_DOMAIN_OPTIONS = ["--define", "VK_HDR_DOMAIN_MRT", "--symbol-prefix", "vk_hdr_"]


def check_header(committed: pathlib.Path, shaders: list[pathlib.Path], guard: str | None = None,
                 options: list[str] | None = None) -> bool:
    if not committed.is_file():
        print(f"vk_shader_header_pin: missing committed header {committed}", file=sys.stderr)
        return False

    with tempfile.TemporaryDirectory() as tmp:
        regenerated = pathlib.Path(tmp) / committed.name
        result = subprocess.run(
            [
                sys.executable,
                str(GENERATOR),
                "--header-out",
                str(regenerated),
                *( ["--guard", guard] if guard else [] ),
                *(options or []),
                *[str(s) for s in shaders],
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"vk_shader_header_pin: regeneration failed:\n{result.stdout}\n{result.stderr}", file=sys.stderr)
            return False
        # EOL-insensitive: a fresh checkout can materialize the committed
        # header with LF while the generator writes platform line endings
        if regenerated.read_bytes().replace(b"\r\n", b"\n") != committed.read_bytes().replace(b"\r\n", b"\n"):
            shader_args = " ".join(s.relative_to(REPO_ROOT).as_posix() for s in shaders)
            guard_arg = (f"--guard {guard} " if guard else "") + " ".join(options or []) + " "
            print(
                "vk_shader_header_pin: committed header is stale — regenerate with:\n"
                f"  python tools/build/spirv_to_header.py {guard_arg}--header-out {committed.relative_to(REPO_ROOT).as_posix()} "
                f"{shader_args}",
                file=sys.stderr,
            )
            return False
    return True


def main() -> int:
    sys.path.insert(0, str(GENERATOR.parent))
    import spirv_to_header  # noqa: E402

    if spirv_to_header.find_glslang(None) is None:
        print("vk_shader_header_pin: skipped (glslangValidator not available)")
        return 0

    ok = check_header(COMMITTED, SHADERS)
    for committed, shaders, guard in EXTRA_HEADERS:
        ok = check_header(committed, shaders, guard) and ok
    ok = check_header(HDR_DOMAIN_COMMITTED, HDR_DOMAIN_SHADERS,
                      "__VK_HDR_DOMAIN_SPV_H__", HDR_DOMAIN_OPTIONS) and ok
    if not ok:
        return 1

    print("vk_shader_header_pin: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
