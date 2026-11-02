# Vendored Volk

- Source: Vulkan SDK 1.4.363.0 (volk, Arseny Kapoulkine), header version 363
- License: MIT (see file banners)
- Runtime meta-loader for Vulkan: `dlopen`/`LoadLibrary` of the ICD loader at
  `volkInitialize()` time, so `renderer-vk_<arch>` loads cleanly on machines
  without any Vulkan driver and the engine fallback ladder can land on GL.
- Update together with `src/external/vulkan` from the same SDK.
