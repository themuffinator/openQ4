#!/usr/bin/env python3
"""Static policy checks for MoltenVK-backed Vulkan on macOS.

macOS has no native Vulkan driver. The Vulkan renderer module runs there through
MoltenVK, a Vulkan-on-Metal translation layer bundled with the package. This test
pins the contract that makes that safe, because every part of it is invisible
from a Windows or Linux build and none of it is exercised by the existing macOS
sweep:

* Vulkan Portability negotiation (instance enumeration flag, device subset
  extension) must stay presence-gated -- hardcoding either way breaks one of the
  two supported MoltenVK loading shapes.
* The engine (SDL) and the renderer module (volk) must resolve the SAME Vulkan
  library, in the same order, or a VkInstance crosses two images.
* The macOS module must export only its entry point, because the macOS client
  still links a full second copy of the renderer statically.
* Shadow-map and depth-writing shaders must avoid the two GLSL constructs whose
  SPIRV-Cross -> MSL translation is rejected by Metal.
* MoltenVK must never be described as native Metal or as native Vulkan.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing function signature {signature!r}")
    depth = 0
    index = source.index("{", start)
    for position in range(index, len(source)):
        if source[position] == "{":
            depth += 1
        elif source[position] == "}":
            depth -= 1
            if depth == 0:
                return source[start : position + 1]
    raise AssertionError(f"Unterminated function body for {signature!r}")


def validate_portability_negotiation() -> None:
    source = read("src/renderer/Vulkan/VulkanDevice.cpp")
    context = "Vulkan portability negotiation"

    # The Khronos loader REQUIRES both the extension and the flag before it will
    # enumerate MoltenVK; a directly loaded libMoltenVK.dylib does not advertise
    # the extension at all and fails instance creation if it is requested. Only a
    # presence check is correct for both, so the probe must stay.
    require(source, "VK_Device_InstanceExtensionSupported", context)
    require(source, "VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME", context)
    require(source, "VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR", context)

    init_body = function_body(source, "bool VK_Device_Init( const renderWindowServices_s *windowServices ) {")
    require(
        init_body,
        "const bool portabilityEnumeration =\n\t\t\tVK_Device_InstanceExtensionSupported( VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME );",
        "portability enumeration presence gate",
    )
    require(
        init_body,
        "if ( portabilityEnumeration ) {\n\t\tici.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;",
        "portability enumeration flag paired with its extension",
    )

    # VUID-VkDeviceCreateInfo-pProperties-04451: the subset extension must be
    # enabled when advertised, and must not be requested when it is not.
    require(source, "VK_Device_DeviceExtensionSupported", context)
    require(
        init_body,
        "vkCtx.portabilitySubset = VK_Device_DeviceExtensionSupported(\n\t\t\tvkCtx.physicalDevice, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME );",
        "portability subset device-extension probe",
    )
    require(init_body, "deviceExtensions[ deviceExtensionCount++ ] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;", context)
    require(init_body, "dci.enabledExtensionCount = deviceExtensionCount;", "device extension count follows the built list")

    # An enabled subset extension with no chained feature struct leaves every
    # optional portability behavior disabled, so the queried struct must be the
    # one handed to vkCreateDevice.
    require(
        init_body,
        "features2.pNext = vkCtx.portabilitySubset ? (void *)&portabilityFeatures : (void *)&features13;",
        "portability subset features chained on device creation",
    )

    # Hard 1.3 floor: ~135 core-1.3 entry points are called unconditionally, so a
    # device below the floor must fail closed with a readable message instead of
    # faulting on a NULL volk pointer during the first frame.
    require(init_body, "vkCtx.deviceProperties.apiVersion < VK_API_VERSION_1_3", "Vulkan 1.3 floor check")
    require(init_body, "this renderer requires Vulkan 1.3", "Vulkan 1.3 floor diagnostic")

    # 8 bound descriptor sets is exactly the ceiling on Metal-backed devices.
    require(
        read("src/renderer/Vulkan/VulkanDevice.h"),
        "VK_REQUIRED_BOUND_DESCRIPTOR_SETS = 8",
        "bound descriptor set requirement",
    )
    require(init_body, "limits.maxBoundDescriptorSets < VK_REQUIRED_BOUND_DESCRIPTOR_SETS", "descriptor set ceiling check")


def validate_loader_agreement() -> None:
    # The engine creates the surface through SDL and the module creates the
    # instance through volk. If they resolve different Vulkan libraries the
    # VkInstance is handed across two images, which is undefined. They agree
    # because both consult SDL_VULKAN_LIBRARY first, then the bundled dylib at
    # the same app-bundle and two loose-package paths, then the system loader.
    device = read("src/renderer/Vulkan/VulkanDevice.cpp")
    loader = function_body(device, "static bool VK_Device_InitLoader( void ) {")
    require(loader, 'getenv( "SDL_VULKAN_LIBRARY" )', "module honors the SDL Vulkan library pin")
    require(loader, '"/../Frameworks/libMoltenVK.dylib"', "module bundle-relative MoltenVK path")
    require(loader, '"/Frameworks/libMoltenVK.dylib"', "module loose Frameworks MoltenVK path")
    require(loader, '"/libMoltenVK.dylib"', "module adjacent MoltenVK path")
    require(loader, "return volkInitialize() == VK_SUCCESS;", "module falls back to the system Vulkan loader")
    require(device, "volkInitializeCustom( getInstanceProcAddr );", "module adopts the resolved loader through volk")

    backend = read("src/sys/sdl3/sdl3_backend.cpp")
    pin = function_body(backend, "static void SDL3_PinBundledMoltenVKLibrary(void) {")
    require(pin, "SDL_GetHint(SDL_HINT_VULKAN_LIBRARY)", "engine defers to an existing Vulkan library pin")
    require(pin, '"/../Frameworks/libMoltenVK.dylib"', "engine bundle-relative MoltenVK path")
    require(pin, '"/Frameworks/libMoltenVK.dylib"', "engine loose Frameworks MoltenVK path")
    require(pin, '"/libMoltenVK.dylib"', "engine adjacent MoltenVK path")
    require(pin, "SDL_SetHint(SDL_HINT_VULKAN_LIBRARY", "engine pins the bundled MoltenVK for SDL")

    create = function_body(
        backend,
        "static bool SDL3_WindowServices_CreateWindowForFramebuffer(",
    )
    require(create, "SDL3_PinBundledMoltenVKLibrary();", "MoltenVK pin runs before the Vulkan window is created")
    require(create, "requestedSurfaceKind == RENDER_SURFACE_VULKAN", "MoltenVK pin is scoped to Vulkan windows")


def validate_module_link_contract() -> None:
    # The macOS client keeps its statically linked OpenGL renderer, so a loaded
    # renderer module carries a second copy of renderer/idlib symbols. Only the
    # entry point may be exported or the two copies interpose at dlopen time.
    exports = read("tools/build/macos_renderer_module.exp")
    require(exports, "_GetRenderAPI", "macOS renderer module export list")
    for symbol in ("_GetGameAPI", "_main"):
        reject(exports, symbol, "macOS renderer module export list")

    meson = read("meson.build")
    require(meson, "tools/build/macos_renderer_module.exp", "macOS renderer module export list is wired")
    require(meson, "-Wl,-exported_symbols_list,", "macOS renderer module export enforcement")
    require(meson, "-Wl,-install_name,@loader_path/", "macOS renderer module install name")
    require(meson, "name_suffix: 'dylib',", "macOS renderer module dylib suffix")

    # Test the supported-host matrix, rather than pinning how the condition is
    # formatted. Android has its own GLES-only target; Darwin SDL3 must remain
    # eligible for the MoltenVK renderer.
    gate = re.search(r"if build_renderer_vk and ([^\n]+)\n\s*build_renderer_vk = false", meson)
    if gate is None:
        raise AssertionError("Missing Vulkan module platform/backend gate")
    for host in ("windows", "linux", "darwin", "android"):
        for backend in ("sdl3", "native", "legacy_win32"):
            disabled = eval(gate.group(1), {"__builtins__": {}}, {"host_system": host, "platform_backend": backend})
            if disabled != (host == "android" or backend != "sdl3"):
                raise AssertionError(f"Unexpected Vulkan module gate for {host}/{backend}: disabled={disabled}")
    require(meson, "MoltenVK is not a native Metal renderer", "meson MoltenVK policy note")


def validate_shader_metal_translation() -> None:
    # SPIRV-Cross gates shadow-sampler LOD forms on MSL version, never on GPU
    # family, so a bias or gradient argument on a shadow sampler emits MSL that
    # Metal rejects on Intel/AMD Macs -- a pipeline-creation failure at runtime,
    # invisible at build time. And Metal makes early_fragment_tests plus a depth
    # write a hard compile error.
    shader_dir = ROOT / "src" / "renderer" / "Vulkan" / "shaders"
    shaders = sorted(shader_dir.glob("*.frag")) + sorted(shader_dir.glob("*.vert"))
    if not shaders:
        raise AssertionError(f"No Vulkan shader sources found under {shader_dir}")

    shadow_sampler = re.compile(r"\buniform\s+sampler(?:2D|Cube|2DArray)Shadow\s+(\w+)")
    for path in shaders:
        source = path.read_text(encoding="utf-8")
        context = path.relative_to(ROOT).as_posix()

        for name in shadow_sampler.findall(source):
            for forbidden in (f"textureGrad({name}", f"textureGrad( {name}"):
                if forbidden in source:
                    raise AssertionError(
                        f"{context}: textureGrad on shadow sampler {name!r} translates to an MSL "
                        "gradient sample_compare that Metal rejects on Intel/AMD Macs; use the "
                        "implicit-LOD texture() form"
                    )
            # texture(sampler, coord, bias) -- a third argument on a shadow
            # sampler is the bias form, equally rejected.
            for match in re.finditer(rf"\btexture\(\s*{re.escape(name)}\s*,", source):
                tail = source[match.end() :]
                depth = 1
                index = 0
                commas = 0
                while index < len(tail) and depth > 0:
                    character = tail[index]
                    if character in "([":
                        depth += 1
                    elif character in ")]":
                        depth -= 1
                    elif character == "," and depth == 1:
                        commas += 1
                    index += 1
                if commas >= 1:
                    raise AssertionError(
                        f"{context}: texture({name}, ...) passes a bias argument to a shadow "
                        "sampler; MSL forbids bias with sample_compare on non-Apple GPUs"
                    )

        if "gl_FragDepth" in source and "early_fragment_tests" in source:
            raise AssertionError(
                f"{context}: early_fragment_tests combined with a gl_FragDepth write is a hard "
                "MSL compile error"
            )
        if "gl_FragDepth" in source:
            for layout in ("layout(depth_greater)", "layout(depth_less)", "layout(depth_unchanged)"):
                reject(source, layout, f"{context} conditional depth layout (unpredictable under Metal)")


def validate_translation_layer_wording() -> None:
    # MoltenVK is a translation layer. Marketing it as native Metal or native
    # Vulkan is exactly what docs/dev/macos-renderer-backend-policy.md forbids.
    forbidden = (
        re.compile(r"native\s+Metal\s+renderer\s+on\s+macOS", re.IGNORECASE),
        re.compile(r"MoltenVK[^.\n]{0,40}\bnative\s+(?:Metal|Vulkan)\b", re.IGNORECASE),
        re.compile(r"native\s+Vulkan[^.\n]{0,40}\bmacOS\b", re.IGNORECASE),
    )
    scanned = [
        "README.md",
        "BUILDING.md",
        "docs/dev/platform-support.md",
        "docs/user/getting-started.md",
    ]
    for relative_path in scanned:
        path = ROOT / relative_path
        if not path.is_file():
            continue
        source = path.read_text(encoding="utf-8")
        for pattern in forbidden:
            match = pattern.search(source)
            if match:
                raise AssertionError(
                    f"{relative_path}: MoltenVK described as a native backend: {match.group(0)!r}"
                )


def main() -> None:
    validate_portability_negotiation()
    validate_loader_agreement()
    validate_module_link_contract()
    validate_shader_metal_translation()
    validate_translation_layer_wording()
    print("macos_moltenvk_policy: ok")


if __name__ == "__main__":
    main()
