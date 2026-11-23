#!/usr/bin/env python3
"""Static contract for the user-selectable default texture sampler."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"Missing {token!r} in {context}")


def main() -> None:
    header = read("src/renderer/Image.h")
    manager = read("src/renderer/ImageManager.cpp")
    gl_image = read("src/renderer/OpenGL/gl_Image.cpp")
    vk_image = read("src/renderer/Vulkan/vk_Image.cpp")
    docs = read("docs/user/display-settings.md")
    validator = read("tools/validation/openq4_validate.py")

    filter_values = (
        "GL_LINEAR_MIPMAP_LINEAR",
        "GL_LINEAR_MIPMAP_NEAREST",
        "GL_NEAREST",
        "GL_LINEAR",
        "GL_NEAREST_MIPMAP_NEAREST",
        "GL_NEAREST_MIPMAP_LINEAR",
    )
    args_start = manager.index("static const char *image_filterArgs[]")
    args_end = manager.index("};", args_start)
    args_block = manager[args_start:args_end]
    positions = [args_block.index(f'"{value}"') for value in filter_values]
    if positions != sorted(positions):
        raise AssertionError("image_filter value order no longer matches imageFilterMode_t")

    require(manager, 'idCVar image_filter(', "image_filter registration")
    require(manager, '"GL_LINEAR_MIPMAP_LINEAR",\n\tCVAR_RENDERER | CVAR_ARCHIVE', "image_filter default and persistence")
    require(manager, "idCmdSystem::ArgCompletion_String<image_filterArgs>", "image_filter completion")
    require(manager, "&image_filter, &image_anisotropy", "live sampler CVar tracking")
    require(manager, "image->GetFilter() == TF_DEFAULT", "authored filter isolation")
    require(manager, "image->RefreshSamplerState();", "live sampler refresh")

    for value in filter_values:
        require(header, f"IMAGE_FILTER_{value.removeprefix('GL_')}", "backend-neutral filter modes")
        require(docs, f"`{value}`", "texture-filter documentation")

    require(header, "imageFilterState_t R_GetDefaultImageFilterState();", "shared filter resolver")
    require(gl_image, "defaultFilter.usesMipmaps", "OpenGL mip selection")
    require(gl_image, "defaultFilter.minLinear", "OpenGL texel selection")
    require(gl_image, "defaultFilter.mipLinear", "OpenGL mip blending")
    require(gl_image, "void idImage::RefreshSamplerState()", "OpenGL live refresh")

    require(vk_image, "defaultFilterMode", "Vulkan sampler cache key")
    require(vk_image, "defaultFilter.magLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST", "Vulkan magnification selection")
    require(vk_image, "defaultFilter.mipLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR", "Vulkan mip blending")
    require(vk_image, "void idImage::RefreshSamplerState()", "Vulkan live refresh")

    # Texture LOD bias: image_lodbias, the temporal-upscaling bias and the
    # Vulkan driver correction all reach default samplers through one refresh.
    require(manager, 'idCVar image_lodbias(', "image_lodbias registration")
    require(manager, 'idCVar r_vkAnisotropicLodBias(', "Vulkan anisotropic correction registration")
    require(manager, "&image_anisotropy, &r_vkAnisotropicLodBias", "live correction tracking")
    require(manager, "WantedLodBiasStep() != appliedLodBiasStep", "live LOD bias refresh")
    require(manager, "int idImage::SamplerLodBiasStep() const", "material-texture LOD bias scope")
    require(gl_image, "glTexParameterf( target, GL_TEXTURE_LOD_BIAS", "OpenGL LOD bias")
    require(gl_image, "glConfig.backendCaps.profile != RENDERER_CONTEXT_PROFILE_ES", "OpenGL ES has no LOD bias parameter")
    require(vk_image, "vkSamplerKeys[ i ].lodBiasStep == lodBiasStep", "Vulkan LOD bias cache key")
    require(vk_image, "vkSamplerKeys[ i ].driverBiasMillis == driverBiasMillis", "Vulkan correction cache key")
    require(vk_image, "VK_DRIVER_ID_NVIDIA_PROPRIETARY", "correction limited to the measured driver")
    require(docs, "`image_lodbias`", "LOD bias documentation")
    require(docs, "`r_vkAnisotropicLodBias 0`", "Vulkan correction documentation")

    require(docs, "seta image_filter GL_NEAREST_MIPMAP_NEAREST", "pixelated-texture example")
    require(
        validator,
        'root / "tools" / "tests" / "renderer_texture_filter.py"',
        "validation-suite registration",
    )

    print("renderer_texture_filter: ok")


if __name__ == "__main__":
    main()
