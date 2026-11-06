// Copyright (C) 2026 DarkMatter Productions
#include "VulkanDeviceSelection.h"
#include "../RendererDeviceCatalog.h"
#include <vulkan/vulkan_beta.h>
#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

namespace {

void DeviceIdentity(const vkDeviceSelectionApi_t& api, VkPhysicalDevice device,
		const VkPhysicalDeviceProperties& props, char (&id)[33]) {
	id[0] = '\0';
	if (!api.getProperties2 || props.apiVersion < VK_API_VERSION_1_1) return;
	VkPhysicalDeviceIDProperties identity = {};
	identity.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
	VkPhysicalDeviceProperties2 properties = {};
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties.pNext = &identity;
	api.getProperties2(device, &properties);
	unsigned nonzero = 0;
	for (unsigned i=0; i<VK_UUID_SIZE; ++i) {
		nonzero |= identity.deviceUUID[i];
		id[2*i] = "0123456789abcdef"[identity.deviceUUID[i] >> 4];
		id[2*i+1] = "0123456789abcdef"[identity.deviceUUID[i] & 15];
	}
	id[32] = '\0';
	if (!nonzero) id[0] = '\0';
}

VkDeviceSize LocalMemory(const vkDeviceSelectionApi_t& api, VkPhysicalDevice device) {
	if (!api.getMemoryProperties) return 0;
	VkPhysicalDeviceMemoryProperties memory = {};
	api.getMemoryProperties(device, &memory);
	VkDeviceSize bytes = 0;
	// The largest local heap avoids counting aliases, shared host heaps or
	// budget headroom twice. On UMA it is not dedicated VRAM.
	for (uint32_t i=0; i<memory.memoryHeapCount; ++i)
		if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
			bytes = (std::max)(bytes, memory.memoryHeaps[i].size);
	return bytes;
}

uint64_t Preference(VkPhysicalDeviceType type, VkDeviceSize localBytes) {
	// Strict classes prevent huge UMA/software heaps outranking hardware.
	// Within discrete GPUs dedicated capacity is a conservative tie-breaker,
	// not a claimed benchmark of GPU speed. Explicit selection remains available.
	uint64_t category = type==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4 :
		type==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3 :
		type==VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2 :
		type==VK_PHYSICAL_DEVICE_TYPE_CPU ? 0 : 1;
	return (category << 48) | (type==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ?
		(std::min)(uint64_t(localBytes / (1024*1024)), (uint64_t(1)<<48)-1) : 0);
}

bool ExplicitId(const char* id) { return id && std::strcmp(id,"auto")!=0; }

bool Preferred(uint64_t score, const char* id, uint64_t otherScore, const char* otherId) {
	return score > otherScore || (score==otherScore && std::strcmp(id,otherId)<0);
}

template<class T, class Query>
VkResult Enumerate( Query query, std::vector<T> &values ) {
	// Counts can change between the two calls. Retry a fresh enumeration rather
	// than selecting from a truncated list or consuming uninitialized entries.
	for ( int attempt = 0; attempt < 4; ++attempt ) {
		uint32_t count = 0;
		VkResult result = query( &count, nullptr );
		if ( result != VK_SUCCESS ) { return result; }
		values.resize( count );
		if ( count == 0 ) { return VK_SUCCESS; }
		result = query( &count, values.data() );
		if ( result == VK_INCOMPLETE ) { continue; }
		if ( result != VK_SUCCESS ) { return result; }
		values.resize( count );
		return VK_SUCCESS;
	}
	return VK_INCOMPLETE;
}

VkResult CheckDeviceLimits( const VkPhysicalDeviceProperties &properties, const char *&reason ) {
	reason = "this renderer requires Vulkan 1.3";
	if ( properties.apiVersion < VK_API_VERSION_1_3 ) { return VK_ERROR_INCOMPATIBLE_DRIVER; }
	reason = "insufficient bound descriptor sets (requires 8)";
	if ( properties.limits.maxBoundDescriptorSets < VK_REQUIRED_BOUND_DESCRIPTOR_SETS ) {
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}
	return VK_SUCCESS;
}

VkResult CheckCandidate( const vkDeviceSelectionApi_t &api, VkSurfaceKHR surface,
		vkDeviceSelection_t &candidate, const char *&reason ) {
	const VkPhysicalDevice device = candidate.device;
	VkResult result = CheckDeviceLimits( candidate.properties, reason );
	if ( result != VK_SUCCESS ) { return result; }
	VkPhysicalDeviceVulkan13Features features13 = {};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	VkPhysicalDeviceFeatures2 features = {};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features.pNext = &features13;
	api.getFeatures2( device, &features );
	reason = "dynamic rendering unsupported";
	if ( !features13.dynamicRendering ) { return VK_ERROR_FEATURE_NOT_PRESENT; }
	reason = "synchronization2 unsupported";
	if ( !features13.synchronization2 ) { return VK_ERROR_FEATURE_NOT_PRESENT; }

	std::vector<VkExtensionProperties> extensions;
	reason = "device-extension enumeration failed";
	result = Enumerate( [&]( uint32_t *count, VkExtensionProperties *data ) {
		return api.enumerateExtensions( device, nullptr, count, data );
	}, extensions );
	if ( result != VK_SUCCESS ) { return result; }
	bool swapchain = false;
	for ( const auto &extension : extensions ) {
		if ( std::strcmp( extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME ) == 0 ) { swapchain = true; }
		if ( std::strcmp( extension.extensionName, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME ) == 0 ) { candidate.hasPortabilitySubset = true; }
		if ( std::strcmp( extension.extensionName, VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME ) == 0 ) { candidate.hasSampleLocations = true; }
		if ( std::strcmp( extension.extensionName, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME ) == 0 ) { candidate.hasDepthClipControl = true; }
		if ( std::strcmp( extension.extensionName, VK_KHR_PRESENT_ID_EXTENSION_NAME ) == 0 ) { candidate.hasPresentId = true; }
		if ( std::strcmp( extension.extensionName, VK_KHR_PRESENT_WAIT_EXTENSION_NAME ) == 0 ) { candidate.hasPresentWait = true; }
	}
	reason = "VK_KHR_swapchain unsupported";
	if ( !swapchain ) { return VK_ERROR_EXTENSION_NOT_PRESENT; }

	uint32_t count = 0;
	api.getQueueFamilies( device, &count, nullptr );
	std::vector<VkQueueFamilyProperties> queues( count );
	if ( count > 0 ) { api.getQueueFamilies( device, &count, queues.data() ); }
	bool found = false;
	result = VK_ERROR_FEATURE_NOT_PRESENT;
	for ( uint32_t i = 0; i < count; ++i ) {
		if ( queues[i].queueCount == 0 || ( queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) == 0 ) { continue; }
		VkBool32 presentable = VK_FALSE;
		const VkResult query = api.getSurfaceSupport( device, i, surface, &presentable );
		if ( query != VK_SUCCESS ) { result = query; continue; }
		if ( !presentable ) { continue; }
		candidate.queueFamily = i;
		candidate.timestampValidBits = queues[i].timestampValidBits;
		found = true;
		break;
	}
	reason = "no graphics+present queue (or surface-support query failed)";
	if ( !found ) { return result; }

	VkSurfaceCapabilitiesKHR caps = {};
	reason = "surface-capability query failed";
	result = api.getSurfaceCapabilities( device, surface, &caps );
	if ( result != VK_SUCCESS ) { return result; }
	reason = "surface does not support color attachments";
	if ( ( caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) == 0 ) { return VK_ERROR_FORMAT_NOT_SUPPORTED; }
	VkSurfaceFormatKHR format = {};
	reason = "surface has no compatible legacy SDR UNORM + SRGB_NONLINEAR format (or query failed)";
	result = VK_SelectSurfaceFormat( api.getSurfaceFormats, device, surface, format );
	if ( result != VK_SUCCESS ) { return result; }

	reason = "no depth/stencil attachment format available";
	for ( VkFormat depth : { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT } ) {
		VkFormatProperties properties = {};
		api.getFormatProperties( device, depth, &properties );
		if ( properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) {
			return VK_SUCCESS;
		}
	}
	return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

VkResult QueryProbeDevice( const vkDeviceSelectionApi_t &api, vkDeviceProbeInfo_t &info ) {
	const VkPhysicalDevice device = info.physicalDevice;
	api.getProperties( device, &info.props );
	info.deviceLocalBytes = LocalMemory(api,device);
	DeviceIdentity(api,device,info.props,info.id);
	info.graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
	info.transferQueueFamily = VK_QUEUE_FAMILY_IGNORED;
	uint32_t count = 0;
	api.getQueueFamilies( device, &count, nullptr );
	std::vector<VkQueueFamilyProperties> queues( count );
	if ( count > 0 ) { api.getQueueFamilies( device, &count, queues.data() ); }
	for ( uint32_t i = 0; i < count; ++i ) {
		if ( queues[i].queueCount == 0 ) { continue; }
		const VkQueueFlags flags = queues[i].queueFlags;
		if ( !info.hasGraphicsQueue && ( flags & VK_QUEUE_GRAPHICS_BIT ) ) {
			info.hasGraphicsQueue = true;
			info.graphicsQueueFamily = i;
		}
		if ( !info.hasDedicatedTransferQueue && ( flags & VK_QUEUE_TRANSFER_BIT )
				&& ( flags & ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ) ) == 0 ) {
			info.hasDedicatedTransferQueue = true;
			info.transferQueueFamily = i;
		}
	}
	if ( !info.hasDedicatedTransferQueue ) { info.transferQueueFamily = info.graphicsQueueFamily; }

	VkPhysicalDeviceVulkan13Features features13 = {};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	// Older adapters cannot satisfy our floor. Keep diagnostic queries within
	// their advertised core version rather than relying on newer query structs.
	features12.pNext = info.props.apiVersion >= VK_API_VERSION_1_3 ? &features13 : nullptr;
	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &features12;
	if ( info.props.apiVersion >= VK_API_VERSION_1_2 ) { api.getFeatures2( device, &features2 ); }
	info.hasDynamicRendering = features13.dynamicRendering == VK_TRUE;
	info.hasSynchronization2 = features13.synchronization2 == VK_TRUE;
	info.hasTimelineSemaphore = features12.timelineSemaphore == VK_TRUE;
	info.hasDescriptorIndexing = features12.descriptorIndexing == VK_TRUE
			&& features12.runtimeDescriptorArray == VK_TRUE
			&& features12.descriptorBindingPartiallyBound == VK_TRUE
			&& features12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE;
	info.hasBufferDeviceAddress = features12.bufferDeviceAddress == VK_TRUE;
	info.hasSamplerAnisotropy = features2.features.samplerAnisotropy == VK_TRUE;
	info.hasTextureCompressionBC = features2.features.textureCompressionBC == VK_TRUE;
	info.hasDepthBounds = features2.features.depthBounds == VK_TRUE;

	std::vector<VkExtensionProperties> extensions;
	const VkResult result = Enumerate( [&]( uint32_t *count, VkExtensionProperties *data ) {
		return api.enumerateExtensions( device, nullptr, count, data );
	}, extensions );
	if ( result != VK_SUCCESS ) { return result; }
	for ( const auto &extension : extensions ) {
		if ( std::strcmp( extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME ) == 0 ) { info.hasSwapchain = true; }
		if ( std::strcmp( extension.extensionName, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME ) == 0 ) { info.hasPortabilitySubset = true; }
	}
	const char *reason = nullptr;
	info.meetsRequirements = CheckDeviceLimits( info.props, reason ) == VK_SUCCESS
			&& info.hasGraphicsQueue && info.hasSwapchain && info.hasDynamicRendering && info.hasSynchronization2;
	// Suitability is tested separately from preference, even for very large heaps.
	info.score = Preference(info.props.deviceType,info.deviceLocalBytes);
	return VK_SUCCESS;
}

}

VkResult VK_SelectSurfaceFormat( PFN_vkGetPhysicalDeviceSurfaceFormatsKHR query,
		VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR &selected ) {
	selected = {};
	try {
		std::vector<VkSurfaceFormatKHR> formats;
		const VkResult result = Enumerate( [&]( uint32_t *count, VkSurfaceFormatKHR *data ) {
			return query( device, surface, count, data );
		}, formats );
		if ( result != VK_SUCCESS ) { return result; }
		for ( const auto &format : formats ) {
			if ( ( format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM )
					&& format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
				selected = format;
				return VK_SUCCESS;
			}
		}
		for ( const auto &format : formats ) {
			if ( format.format == VK_FORMAT_UNDEFINED && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
				selected = { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
				return VK_SUCCESS;
			}
		}
		return VK_ERROR_FORMAT_NOT_SUPPORTED;
	} catch ( const std::bad_alloc & ) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
}

VkResult VK_SelectPhysicalDevice( const vkDeviceSelectionApi_t &api, VkInstance instance,
		VkSurfaceKHR surface, int forcedIndex, vkDeviceSelection_t &selected,
		vkDeviceRejection_t report, void *user, uint32_t firstIndex, const char *preferredId ) {
	selected = {};
	// An explicit choice that already failed has no substitute to resume with.
	if ((forcedIndex >= 0 || ExplicitId(preferredId)) && firstIndex > 0) { return VK_ERROR_INITIALIZATION_FAILED; }
	if (forcedIndex < 0 && !RenderDevicePreferenceValid(preferredId)) return VK_ERROR_INITIALIZATION_FAILED;
	try {
		std::vector<VkPhysicalDevice> devices;
		VkResult result = Enumerate( [&]( uint32_t *count, VkPhysicalDevice *data ) {
			return api.enumeratePhysicalDevices( instance, count, data );
		}, devices );
		if ( result != VK_SUCCESS ) { return result; }
		if ( forcedIndex >= 0 && static_cast<size_t>( forcedIndex ) >= devices.size() ) {
			return VK_ERROR_INITIALIZATION_FAILED;
		}
		std::vector<vkDeviceSelection_t> admitted;
		for ( uint32_t i = 0; i < devices.size(); ++i ) {
			if ( forcedIndex >= 0 && i != static_cast<uint32_t>( forcedIndex ) ) { continue; }
			vkDeviceSelection_t candidate = {};
			candidate.device = devices[i];
			candidate.index = i;
			api.getProperties( candidate.device, &candidate.properties );
			DeviceIdentity(api,candidate.device,candidate.properties,candidate.id);
			if (forcedIndex < 0 && ExplicitId(preferredId) && std::strcmp(candidate.id,preferredId)!=0) continue;
			const char *reason = nullptr;
			result = CheckCandidate( api, surface, candidate, reason );
			if ( result == VK_SUCCESS ) {
				candidate.score = Preference(candidate.properties.deviceType,LocalMemory(api,candidate.device));
				admitted.push_back(candidate); continue;
			}
			if ( report != nullptr ) { report( i, candidate.properties, reason, result, user ); }
		}
		std::stable_sort(admitted.begin(),admitted.end(),[](const auto& a,const auto& b) {
			return Preferred(a.score,a.id,b.score,b.id);
		});
		if (firstIndex >= admitted.size()) return VK_ERROR_FEATURE_NOT_PRESENT;
		selected = admitted[firstIndex]; return VK_SUCCESS;
	} catch ( const std::bad_alloc & ) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
}

VkResult VK_SelectProbeDevice( const vkDeviceSelectionApi_t &api, VkInstance instance,
		int forcedIndex, bool verbose, vkDeviceProbeInfo_t &selected,
		vkDeviceProbeReport_t report, void *user, const char *preferredId ) {
	selected = {};
	(void)verbose;
	if (forcedIndex < 0 && !RenderDevicePreferenceValid(preferredId)) return VK_ERROR_INITIALIZATION_FAILED;
	try {
		std::vector<VkPhysicalDevice> devices;
		const VkResult result = Enumerate( [&]( uint32_t *count, VkPhysicalDevice *data ) {
			return api.enumeratePhysicalDevices( instance, count, data );
		}, devices );
		if ( result != VK_SUCCESS ) { return result; }
		if ( forcedIndex >= 0 && static_cast<size_t>( forcedIndex ) >= devices.size() ) {
			return VK_ERROR_INITIALIZATION_FAILED;
		}
		vkDeviceProbeInfo_t best = {};
		for ( uint32_t i = 0; i < devices.size(); ++i ) {
			if ( forcedIndex >= 0 && i != static_cast<uint32_t>( forcedIndex ) ) { continue; }
			vkDeviceProbeInfo_t candidate = {};
			candidate.physicalDevice = devices[i];
			candidate.index = i;
			const VkResult queryResult = QueryProbeDevice( api, candidate );
			if ( report != nullptr ) { report( candidate, queryResult, user ); }
			if ( queryResult != VK_SUCCESS || !candidate.meetsRequirements ) { continue; }
			if (forcedIndex < 0 && ExplicitId(preferredId) && std::strcmp(candidate.id,preferredId)!=0) continue;
			if ( best.physicalDevice == VK_NULL_HANDLE || Preferred(candidate.score,candidate.id,best.score,best.id) ) { best = candidate; }
		}
		if ( best.physicalDevice == VK_NULL_HANDLE ) { return VK_ERROR_FEATURE_NOT_PRESENT; }
		selected = best;
		return VK_SUCCESS;
	} catch ( const std::bad_alloc & ) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
}

VkResult VK_QueryDeviceCatalog(const vkDeviceSelectionApi_t& api, VkInstance instance, renderDeviceCatalog_t& catalog) {
	catalog = {};
	struct Context { renderDeviceCatalog_t value{}; bool overflow=false; } context;
	vkDeviceProbeInfo_t selected = {};
	const auto result = VK_SelectProbeDevice(api,instance,-1,true,selected,
		[](const vkDeviceProbeInfo_t& info,VkResult query,void* opaque) {
			auto& c=*static_cast<Context*>(opaque);
			// Multiple ICDs may expose the same physical UUID. It is still one
			// user-facing GPU; retain a usable route when either driver admits it.
			for (unsigned i=0; info.id[0] && i<c.value.count; ++i) {
				if (std::strcmp(info.id,c.value.devices[i].id)==0) {
					c.value.devices[i].usable |= query==VK_SUCCESS && info.meetsRequirements; return;
				}
			}
			if (c.value.count==RENDER_DEVICE_LIMIT) { c.overflow=true; return; }
			auto& item=c.value.devices[c.value.count++];
			std::memcpy(item.id,info.id,sizeof(item.id));
			std::memcpy(item.name,info.props.deviceName,sizeof(item.name));
			item.name[sizeof(item.name)-1]='\0';
			item.usable=query==VK_SUCCESS && info.meetsRequirements && info.id[0];
		},&context);
	if (context.overflow || (result!=VK_SUCCESS && result!=VK_ERROR_FEATURE_NOT_PRESENT)) return context.overflow ? VK_INCOMPLETE : result;
	for (unsigned i=0; i<context.value.count; ++i)
		context.value.devices[i].preferred = selected.id[0] && std::strcmp(context.value.devices[i].id,selected.id)==0;
	catalog=context.value; return VK_SUCCESS;
}

namespace {

template<class T>
VkResult FindNames( const std::vector<T> &list, const char *( *name )( const T & ),
		const char *const *names, uint32_t count, bool *found ) {
	for ( const auto &entry : list ) {
		for ( uint32_t i = 0; i < count; ++i ) {
			if ( std::strcmp( name( entry ), names[i] ) == 0 ) { found[i] = true; }
		}
	}
	return VK_SUCCESS;
}

}

VkResult VK_FindInstanceLayers( PFN_vkEnumerateInstanceLayerProperties query,
		const char *const *names, uint32_t count, bool *found ) {
	for ( uint32_t i = 0; i < count; ++i ) { found[i] = false; }
	try {
		std::vector<VkLayerProperties> layers;
		const VkResult result = Enumerate( [&]( uint32_t *size, VkLayerProperties *data ) {
			return query( size, data );
		}, layers );
		if ( result != VK_SUCCESS ) { return result; }
		return FindNames<VkLayerProperties>( layers, []( const VkLayerProperties &layer ) -> const char * {
			return layer.layerName;
		}, names, count, found );
	} catch ( const std::bad_alloc & ) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
}

VkResult VK_FindInstanceExtensions( PFN_vkEnumerateInstanceExtensionProperties query,
		const char *layer, const char *const *names, uint32_t count, bool *found ) {
	for ( uint32_t i = 0; i < count; ++i ) { found[i] = false; }
	try {
		std::vector<VkExtensionProperties> extensions;
		const VkResult result = Enumerate( [&]( uint32_t *size, VkExtensionProperties *data ) {
			return query( layer, size, data );
		}, extensions );
		if ( result != VK_SUCCESS ) { return result; }
		return FindNames<VkExtensionProperties>( extensions, []( const VkExtensionProperties &extension ) -> const char * {
			return extension.extensionName;
		}, names, count, found );
	} catch ( const std::bad_alloc & ) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
}

VkResult VK_QuerySurfacePresentModes( PFN_vkGetPhysicalDeviceSurfacePresentModesKHR query,
		VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t &modeMask ) {
	modeMask = 0;
	try {
		std::vector<VkPresentModeKHR> modes;
		const VkResult result = Enumerate( [&]( uint32_t *count, VkPresentModeKHR *data ) {
			return query( device, surface, count, data );
		}, modes );
		if ( result != VK_SUCCESS ) { return result; }
		for ( const VkPresentModeKHR mode : modes ) {
			if ( static_cast<uint32_t>( mode ) < 32u ) { modeMask |= 1u << static_cast<uint32_t>( mode ); }
		}
		return VK_SUCCESS;
	} catch ( const std::bad_alloc & ) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
}
