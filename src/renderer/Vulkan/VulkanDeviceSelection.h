// Copyright (C) 2026 DarkMatter Productions
#ifndef __VULKAN_DEVICE_SELECTION_H__
#define __VULKAN_DEVICE_SELECTION_H__

#include <vulkan/vulkan.h>

// The shadowed interaction layout uses six images, one UBO and one shadow set.
static const int VK_REQUIRED_BOUND_DESCRIPTOR_SETS = 8;

// Instance-level queries only. Selection neither creates a logical device nor
// changes global renderer state; callers publish the result after success.
struct vkDeviceSelectionApi_t {
	PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices;
	PFN_vkGetPhysicalDeviceProperties getProperties;
	PFN_vkGetPhysicalDeviceFeatures2 getFeatures2;
	PFN_vkEnumerateDeviceExtensionProperties enumerateExtensions;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueueFamilies;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR getSurfaceSupport;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getSurfaceCapabilities;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getSurfaceFormats;
	PFN_vkGetPhysicalDeviceFormatProperties getFormatProperties;
	// Shared preference policy and stable physical-device identity.
	PFN_vkGetPhysicalDeviceMemoryProperties getMemoryProperties = nullptr;
	PFN_vkGetPhysicalDeviceProperties2 getProperties2 = nullptr;
};

struct vkDeviceSelection_t {
	VkPhysicalDevice device;
	VkPhysicalDeviceProperties properties;
	uint32_t index;
	uint32_t queueFamily;
	uint32_t timestampValidBits;
	char id[33];
	uint64_t score;
	// Read from the complete admission extension list, so device creation never
	// consults a second, separately bounded lookup.
	bool hasPortabilitySubset;
	bool hasSampleLocations;
	// VK_EXT_depth_clip_control (r_vkDepthClipControl)
	bool hasDepthClipControl;
	// VK_KHR_present_id and VK_KHR_present_wait (r_vkPresentTiming)
	bool hasPresentId;
	bool hasPresentWait;
};

typedef void (*vkDeviceRejection_t)( uint32_t index, const VkPhysicalDeviceProperties &properties,
		const char *reason, VkResult result, void *user );

// Automatic selection ranks admitted hardware before choosing. firstIndex is a
// rank, NOT an enumeration index: retries must reach lower-index GPUs too.
// A diagnostic index overrides the saved UUID. Explicit choices never substitute.
VkResult VK_SelectPhysicalDevice( const vkDeviceSelectionApi_t &api, VkInstance instance,
		VkSurfaceKHR surface, int forcedIndex, vkDeviceSelection_t &selected,
		vkDeviceRejection_t report = nullptr, void *user = nullptr, uint32_t firstIndex = 0,
		const char *preferredId = "auto" );

// Startup tries at most this many adapters before it gives up on Vulkan.
static const int VK_MAX_STARTUP_ADAPTER_ATTEMPTS = 4;

// Complete instance-level inventories with the same bounded retry as device
// lists. found[i] reports names[i]; a failed or persistently changing list
// returns its error and reports nothing found rather than a truncated subset.
VkResult VK_FindInstanceLayers( PFN_vkEnumerateInstanceLayerProperties query,
		const char *const *names, uint32_t count, bool *found );
VkResult VK_FindInstanceExtensions( PFN_vkEnumerateInstanceExtensionProperties query,
		const char *layer, const char *const *names, uint32_t count, bool *found );

// Bit (1 << mode) for every supported core present mode below 32 (immediate,
// mailbox, FIFO, FIFO relaxed); extension modes are ignored. A failed list
// clears the mask and returns its error.
VkResult VK_QuerySurfacePresentModes( PFN_vkGetPhysicalDeviceSurfacePresentModesKHR query,
		VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t &modeMask );

// Shared by admission and swapchain creation, including the legacy undefined
// format case. Never silently picks an sRGB attachment for display-coded output.
VkResult VK_SelectSurfaceFormat( PFN_vkGetPhysicalDeviceSurfaceFormatsKHR query,
		VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR &selected );

// Capability inventory for the startup gate and verbose diagnostic probe.
// Optional features are reported, never added to the renderer's admission floor.
struct vkDeviceProbeInfo_t {
	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties props;
	uint32_t index;
	uint32_t graphicsQueueFamily;
	uint32_t transferQueueFamily;
	VkDeviceSize deviceLocalBytes;
	bool hasGraphicsQueue;
	bool hasDedicatedTransferQueue;
	bool hasSwapchain;
	bool hasPortabilitySubset;
	bool hasDynamicRendering;
	bool hasSynchronization2;
	bool hasTimelineSemaphore;
	bool hasDescriptorIndexing;
	bool hasBufferDeviceAddress;
	bool hasSamplerAnisotropy;
	bool hasTextureCompressionBC;
	bool hasDepthBounds;
	bool meetsRequirements;
	uint64_t score;
	char id[33];
};

typedef void (*vkDeviceProbeReport_t)( const vkDeviceProbeInfo_t &info, VkResult queryResult, void *user );

// Quiet activation and verbose diagnostics use exactly the same preference.
// Forced indices are queried alone; failure clears selected and never substitutes.
VkResult VK_SelectProbeDevice( const vkDeviceSelectionApi_t &api, VkInstance instance,
		int forcedIndex, bool verbose, vkDeviceProbeInfo_t &selected,
		vkDeviceProbeReport_t report = nullptr, void *user = nullptr, const char *preferredId = "auto" );

struct renderDeviceCatalog_t;
VkResult VK_QueryDeviceCatalog( const vkDeviceSelectionApi_t &api, VkInstance instance,
		renderDeviceCatalog_t &catalog );

#endif
