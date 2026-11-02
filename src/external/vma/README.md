# Vendored Vulkan Memory Allocator (VMA)

- Source: VulkanMemoryAllocator 3.5.0 (AMD), the upstream release, used with
  the Vulkan SDK 1.4.363.0 headers in `src/external/vulkan`
- License: MIT (see file banner)
- Header-only; the implementation is compiled in
  `src/renderer/Vulkan/VulkanVmaImpl.cpp` with
  `VMA_STATIC_VULKAN_FUNCTIONS=0` / `VMA_DYNAMIC_VULKAN_FUNCTIONS=1`
  (function pointers supplied by Volk).
- Update together with `src/external/vulkan`; the VMA release must support the
  vendored header version.
