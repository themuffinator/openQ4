# Vendored Vulkan C headers

- Source: Vulkan SDK 1.4.363.0 (Vulkan-Headers, Khronos Group)
- License: Apache-2.0 OR MIT (see header banners)
- Contents: C API headers only (`vulkan/`, `vk_video/`); the C++ bindings are
  deliberately not vendored.
- Consumed by the `renderer-vk_<arch>` module through the Volk loader with
  `VK_NO_PROTOTYPES`; nothing links against `vulkan-1`/`libvulkan`.
- Update by copying the matching headers from a newer Vulkan SDK `Include/`
  tree and recording the SDK version here.
