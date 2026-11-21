#!/usr/bin/env python3
"""Static contract for parallax occlusion mapping (REN-11).

The runtime qualification is tools/tests/renderer_parallax_occlusion.py; this
pins what must stay true between its runs:

- the feature is default-off, and off compiles every OpenGL program from the
  same token stream as before (each sample macro is the plain texture2D call)
  while every Vulkan sample helper falls back to the original texture() call;
- one kernel (ParallaxOcclusion.h) serves both backends, so every pass that
  lights a surface traces the same ray;
- every Vulkan PBR fragment entry point begins the trace, and both the direct
  and the environment draws write the same parallax inputs;
- the height rides in the ORM alpha (packORMH), and the admission declines the
  surfaces whose other passes could not follow the ray.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src" / "renderer"
SHADERS = RENDERER / "Vulkan" / "shaders"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def reject(text: str, token: str, context: str) -> None:
    if token in text:
        raise AssertionError(f"unexpected {token!r} in {context}")


def test_cvars_default_off() -> None:
    init = read(RENDERER / "RenderSystem_init.cpp")
    for token in ('idCVar r_parallaxOcclusion( "r_parallaxOcclusion", "0",',
                  'idCVar r_parallaxSamples( "r_parallaxSamples", "32",',
                  'idCVar r_parallaxShadows( "r_parallaxShadows", "0",',
                  "R_Parallax_PrintInfo();"):
        require(init, token, "renderer cvars and gfxInfo")


def test_shared_kernel() -> None:
    kernel = read(RENDERER / "ParallaxOcclusion.h")
    for token in ("float OpenQ4ParallaxHeight(vec2 uv, vec2 dx, vec2 dy);",
                  "vec4 OpenQ4ParallaxTrace(", "float OpenQ4ParallaxShadow(",
                  "static const char OPENQ4_PARALLAX_GLSL[] ="):
        require(kernel, token, "shared parallax kernel")
    # Stringified for OpenGL: the macro body must not carry a preprocessor line.
    body = kernel.split("#define OPENQ4_PARALLAX_FUNCTIONS", 1)[1].split("#ifdef __cplusplus", 1)[0]
    if re.search(r"^\s*#", body.split("\n", 1)[1], re.M):
        raise AssertionError("the parallax kernel macro must not contain preprocessor directives")
    require(read(SHADERS / "pbr_direct.glsl"), '#include "../../ParallaxOcclusion.h"', "Vulkan PBR library")
    owner = read(RENDERER / "draw_pbr.cpp")
    require(owner, "source += OPENQ4_PARALLAX_GLSL;", "OpenGL PBR library")


def test_off_is_the_old_sampling() -> None:
    owner = read(RENDERER / "draw_pbr.cpp")
    for kind in ("BUMP", "ALBEDO", "DATA"):
        require(owner, f'"#define PBR_{kind}_TEXTURE( map, uv ) texture2D( map, uv )\\n"', "OpenGL flat sampling")
    require(owner, '"#define PBRParallaxBegin()\\n"', "OpenGL flat sampling")
    require(owner, "return RB_GLPBR_ParallaxCompiled()", "OpenGL environment variant")
    require(owner, "&& program.parallax == RB_GLPBR_ParallaxCompiled();", "OpenGL program cache key")
    require(read(RENDERER / "draw_arb2.cpp"), "attemptParallax == parallax", "OpenGL shadow-mapped PBR variants")
    direct = read(SHADERS / "pbr_direct.glsl")
    for helper in ("PBRBumpTexture", "PBRAlbedoTexture", "PBRDataTexture"):
        body = direct.split(f"vec4 {helper}(sampler2D map, vec2 texCoord) {{", 1)[1].split("\n}", 1)[0]
        require(body, "return texture(map, texCoord);", f"Vulkan {helper} flat path")
    require(direct, "if (pc.a.w < 0.5) {\n        float height = texture(bumpMap, vBumpTexCoord).r;",
            "retail Parallaxbump single step")
    for name in ("pbr_direct.glsl", "pbr_environment.glsl", "pbr_debug.glsl"):
        source = read(SHADERS / name)
        # The classic baked-grid receiver is not a PBR material: it keeps texture().
        if "vec3 EvaluateBakedClassic() {" in source:
            head, rest = source.split("vec3 EvaluateBakedClassic() {", 1)
            source = head + rest.split("\n}\n", 1)[1]
        for sampler in ("diffuseMap", "specularMap", "bumpMap"):
            if re.search(rf"\btexture\({sampler}, (?:vDiffuseTexCoord|vSpecularTexCoord|albedoTexCoord|dataTexCoord)\)", source):
                raise AssertionError(f"{name} samples {sampler} outside the parallax helpers")


def test_every_pass_traces() -> None:
    for name in ("interaction.frag", "interaction_shadow.frag", "interaction_shadow_point.frag"):
        require(read(SHADERS / name), "PBRParallaxBegin(pc.d.x > 0.5);", name)
        require(read(SHADERS / name), "vec2 offset = RetailParallaxOffset();", name)
    require(read(SHADERS / "pbr_probe_environment.frag"), "PBRParallaxBegin(true);", "probe environment")
    require(read(SHADERS / "pbr_baked_pass.glsl"), "PBRParallaxBegin(pc.d.x < 5.5);", "baked environment")
    vulkan = read(RENDERER / "Vulkan" / "vk_Interactions.cpp")
    if vulkan.count("VK_PBRWriteParallax( block,") < 2:
        raise AssertionError("both the Vulkan direct and environment draws must write the parallax inputs")
    require(vulkan, "R_PBRNative_ResolveParallax( surf, out.dataFlags, out.dataImage,", "Vulkan admission")
    owner = read(RENDERER / "draw_pbr.cpp")
    require(owner, "R_PBRNative_ParallaxParameters( material, params, fade );", "OpenGL material uniforms")
    for token in ('"vec3 OpenQ4PBRDirect( vec3 shadow ) {\\n"\n\t\t"\tPBRParallaxBegin();\\n"',
                  '"void main() {\\n"\n\t\t"\tPBRParallaxBegin();\\n"'):
        require(owner, token, "OpenGL PBR entry points")


def test_material_and_admission() -> None:
    material = read(RENDERER / "Material.cpp")
    for token in ('!token.Icmp( "heightMap" )', '!token.Icmp( "heightScale" )', '!token.Icmp( "heightReference" )',
                  'idStr program = "packORMH( ";', "pbrInfo.parallax = true;"):
        require(material, token, "PBR material parser")
    program = read(ROOT / "src" / "imagetools" / "Image_program.cpp")
    require(program, 'if (!token.Icmp("packORMH")) {', "image programs")
    require(program, "(*pic)[p * 4 + 3] = sources[1][p * 4];", "packORMH height in alpha")
    contract = read(RENDERER / "PBRNativeContract.cpp")
    for reason in ('"no packed height"', '"perforated coverage"', '"emissive"', '"zero heightScale"'):
        require(contract, f"R_Parallax_Decline( {reason} );", "parallax admission")
    require(contract, "if ( !info.parallax || !r_parallaxOcclusion.GetBool() ) {", "parallax admission")


def main() -> int:
    for test in (test_cvars_default_off, test_shared_kernel, test_off_is_the_old_sampling,
                 test_every_pass_traces, test_material_and_admission):
        test()
    print("parallax_occlusion_contract: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
