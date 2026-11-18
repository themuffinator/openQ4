// Copyright (C) 2026 DarkMatter Productions
//

// the engine PCH poisons snprintf/vsnprintf toward idStr; this bring-up TU
// keeps its C-style formatting (services-only surface, no idStr dependency)
#undef snprintf
#undef vsnprintf
#include <climits>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "volk.h"
#include "vk_mem_alloc.h"

#include "../RenderModuleAPI.h"
#include "VulkanBringup.h"
#include "VulkanDeviceSelection.h"

/*
===============================================================================

	Phase A bring-up probe. Every step checks its VkResult and reports
	through the engine services table; a missing Vulkan loader or an
	unsuitable device must produce a clear message, never a crash, so the
	engine-side fallback ladder can always land on OpenGL.

===============================================================================
*/

static const renderModuleServices_t *vk_services = NULL;
static vkBringupLoaderInit_t vk_loaderInit = NULL;
static bool vk_volkInitialized = false;
// the instance of the last passed gate probe (VK_Bringup_RunDeviceSelfTest),
// alive until VK_Bringup_ReleaseHeldInstance
static VkInstance vk_heldInstance = VK_NULL_HANDLE;

// the feature floor the real Vulkan renderer is designed against
// (docs/dev/archive/2026-10-18/plans/2026-07-16-vulkan-renderer.md, "Vulkan technical design")
#define VK_BRINGUP_REQUIRED_API_VERSION		VK_API_VERSION_1_3
// Share the renderer's limit without depending on its device context.
#define VK_BRINGUP_REQUIRED_BOUND_DESCRIPTOR_SETS	VK_REQUIRED_BOUND_DESCRIPTOR_SETS

void VK_Bringup_SetServices( const renderModuleServices_t *services ) {
	vk_services = services;
}

void VK_Bringup_SetLoaderInit( vkBringupLoaderInit_t initLoader ) {
	vk_loaderInit = initLoader;
}

static void VK_Printf( const char *fmt, ... ) {
	va_list	args;
	char	text[ 2048 ];

	if ( vk_services == NULL || vk_services->Printf == NULL ) {
		return;
	}
	va_start( args, fmt );
	vsnprintf( text, sizeof( text ), fmt, args );
	va_end( args );
	text[ sizeof( text ) - 1 ] = '\0';
	vk_services->Printf( "%s", text );
}

static void VK_Warning( const char *fmt, ... ) {
	va_list	args;
	char	text[ 2048 ];

	if ( vk_services == NULL || vk_services->Warning == NULL ) {
		return;
	}
	va_start( args, fmt );
	vsnprintf( text, sizeof( text ), fmt, args );
	va_end( args );
	text[ sizeof( text ) - 1 ] = '\0';
	vk_services->Warning( "%s", text );
}

static const char *VK_ResultName( VkResult result ) {
	switch ( result ) {
		case VK_SUCCESS:							return "VK_SUCCESS";
		case VK_INCOMPLETE:						return "VK_INCOMPLETE";
		case VK_ERROR_OUT_OF_HOST_MEMORY:			return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:			return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED:		return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_LAYER_NOT_PRESENT:			return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT:		return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT:			return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER:			return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_DEVICE_LOST:					return "VK_ERROR_DEVICE_LOST";
		case VK_TIMEOUT:							return "VK_TIMEOUT";
		default:									return "VK_ERROR (unlisted)";
	}
}

static const char *VK_DeviceTypeName( VkPhysicalDeviceType type ) {
	switch ( type ) {
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:		return "discrete";
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:	return "integrated";
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:		return "virtual";
		case VK_PHYSICAL_DEVICE_TYPE_CPU:				return "cpu";
		default:										return "other";
	}
}

static bool VK_CVarGetBool( const char *name ) {
	if ( vk_services == NULL || vk_services->CVar_GetBool == NULL ) {
		return false;
	}
	return vk_services->CVar_GetBool( name );
}

static const char *VK_CVarGetString(const char *name) {
	return vk_services && vk_services->CVar_GetString ? vk_services->CVar_GetString(name) : "auto";
}

static int VK_CVarGetInteger( const char *name ) {
	if ( vk_services == NULL || vk_services->CVar_GetInteger == NULL ) {
		return -1;
	}
	return vk_services->CVar_GetInteger( name );
}

/*
====================
VK_Bringup_ReportPortability

Records the portability facts a Metal-backed implementation decides at runtime.
This is the evidence artifact the macOS MoltenVK lane publishes: every capability
the renderer negotiates rather than assumes is printed here so a CI log answers
them without needing a debugger on an Apple machine.
====================
*/
static void VK_Bringup_ReportPortability( VkPhysicalDevice physicalDevice, const vkDeviceProbeInfo_t &info ) {
	const bool subset = info.hasPortabilitySubset;

	VK_Printf( "  portability: subset=%s maxBoundDescriptorSets=%u (need %d)\n",
			subset ? "yes" : "no",
			info.props.limits.maxBoundDescriptorSets,
			VK_BRINGUP_REQUIRED_BOUND_DESCRIPTOR_SETS );

	if ( subset ) {
		VkPhysicalDevicePortabilitySubsetFeaturesKHR portability;
		memset( &portability, 0, sizeof( portability ) );
		portability.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
		VkPhysicalDeviceFeatures2 query;
		memset( &query, 0, sizeof( query ) );
		query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		query.pNext = &portability;
		vkGetPhysicalDeviceFeatures2( physicalDevice, &query );

		VK_Printf( "  portability subset: imageViewFormatSwizzle=%d imageViewFormatReinterpretation=%d "
				"mutableComparisonSamplers=%d samplerMipLodBias=%d triangleFans=%d "
				"vertexAttributeAccessBeyondStride=%d constantAlphaColorBlendFactors=%d separateStencilMaskRef=%d\n",
				portability.imageViewFormatSwizzle ? 1 : 0,
				portability.imageViewFormatReinterpretation ? 1 : 0,
				portability.mutableComparisonSamplers ? 1 : 0,
				portability.samplerMipLodBias ? 1 : 0,
				portability.triangleFans ? 1 : 0,
				portability.vertexAttributeAccessBeyondStride ? 1 : 0,
				portability.constantAlphaColorBlendFactors ? 1 : 0,
				portability.separateStencilMaskRef ? 1 : 0 );
	}

	// formats the back end uses without a fallback today
	static const struct {
		VkFormat		format;
		const char *	name;
	} probedFormats[] = {
		{ VK_FORMAT_R5G6B5_UNORM_PACK16,	"R5G6B5 (light cookies)" },
		{ VK_FORMAT_BC1_RGB_UNORM_BLOCK,	"BC1" },
		{ VK_FORMAT_BC3_UNORM_BLOCK,		"BC3" },
		{ VK_FORMAT_BC7_UNORM_BLOCK,		"BC7" },
		{ VK_FORMAT_BC6H_UFLOAT_BLOCK,		"BC6H" },
		{ VK_FORMAT_BC6H_SFLOAT_BLOCK,		"BC6H signed" },
		{ VK_FORMAT_R16G16B16A16_SFLOAT,	"RGBA16F (HDR scene)" },
	};
	for ( int i = 0; i < (int)( sizeof( probedFormats ) / sizeof( probedFormats[ 0 ] ) ); i++ ) {
		VkFormatProperties props;
		memset( &props, 0, sizeof( props ) );
		vkGetPhysicalDeviceFormatProperties( physicalDevice, probedFormats[ i ].format, &props );
		VK_Printf( "  format %s: sampled=%d filterLinear=%d colorAttachment=%d\n",
				probedFormats[ i ].name,
				( props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ) != 0 ? 1 : 0,
				( props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT ) != 0 ? 1 : 0,
				( props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ) != 0 ? 1 : 0 );
	}

	// VkFormatProperties3 population (core 1.3): the shadow depth-format probe
	// depends on it, and the depth-comparison bit has no 1.0 equivalent
	VkFormatProperties3 props3;
	memset( &props3, 0, sizeof( props3 ) );
	props3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
	VkFormatProperties2 props2;
	memset( &props2, 0, sizeof( props2 ) );
	props2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
	props2.pNext = &props3;
	if ( info.props.apiVersion >= VK_API_VERSION_1_3 ) {
		vkGetPhysicalDeviceFormatProperties2( physicalDevice, VK_FORMAT_D32_SFLOAT, &props2 );
	}
	VK_Printf( "  formatProperties3 populated=%d depthComparison(D32_SFLOAT)=%d\n",
			props3.optimalTilingFeatures != 0 ? 1 : 0,
			( props3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT ) != 0 ? 1 : 0 );

	VkPhysicalDeviceDepthStencilResolveProperties resolveProperties;
	memset( &resolveProperties, 0, sizeof( resolveProperties ) );
	resolveProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES;
	VkPhysicalDeviceProperties2 properties2;
	memset( &properties2, 0, sizeof( properties2 ) );
	properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties2.pNext = &resolveProperties;
	if ( info.props.apiVersion >= VK_API_VERSION_1_2 ) {
		vkGetPhysicalDeviceProperties2( physicalDevice, &properties2 );
	}
	VK_Printf( "  depthResolveModes=0x%x stencilResolveModes=0x%x sampleCounts(color)=0x%x\n",
			(unsigned)resolveProperties.supportedDepthResolveModes,
			(unsigned)resolveProperties.supportedStencilResolveModes,
			(unsigned)info.props.limits.framebufferColorSampleCounts );
}

/*
====================
VK_Bringup_DescribeUnmetRequirements

Names every requirement from the capability probe that the device misses. The
loader records this as the reason it fell back to OpenGL, so it has to say
which requirement failed rather than just that one did.
====================
*/
static void VK_Bringup_AppendListItem( char *out, size_t outSize, const char *item ) {
	if ( out[ 0 ] != '\0' ) {
		strncat( out, ", ", outSize - strlen( out ) - 1 );
	}
	strncat( out, item, outSize - strlen( out ) - 1 );
}

static void VK_Bringup_DescribeUnmetRequirements( const vkDeviceProbeInfo_t &info, char *out, size_t outSize ) {
	char item[ 64 ];

	out[ 0 ] = '\0';
	if ( info.props.apiVersion < VK_BRINGUP_REQUIRED_API_VERSION ) {
		snprintf( item, sizeof( item ), "Vulkan 1.3 (device has %u.%u)",
				VK_API_VERSION_MAJOR( info.props.apiVersion ), VK_API_VERSION_MINOR( info.props.apiVersion ) );
		VK_Bringup_AppendListItem( out, outSize, item );
	}
	if ( !info.hasGraphicsQueue ) {
		VK_Bringup_AppendListItem( out, outSize, "a graphics queue" );
	}
	if ( !info.hasSwapchain ) {
		VK_Bringup_AppendListItem( out, outSize, VK_KHR_SWAPCHAIN_EXTENSION_NAME );
	}
	if ( !info.hasDynamicRendering ) {
		VK_Bringup_AppendListItem( out, outSize, "dynamicRendering" );
	}
	if ( !info.hasSynchronization2 ) {
		VK_Bringup_AppendListItem( out, outSize, "synchronization2" );
	}
	if ( info.props.limits.maxBoundDescriptorSets < (uint32_t)VK_BRINGUP_REQUIRED_BOUND_DESCRIPTOR_SETS ) {
		snprintf( item, sizeof( item ), "%d bound descriptor sets (device has %u)",
				VK_BRINGUP_REQUIRED_BOUND_DESCRIPTOR_SETS, info.props.limits.maxBoundDescriptorSets );
		VK_Bringup_AppendListItem( out, outSize, item );
	}
}

/*
====================
VK_Bringup_ReportFormatSupport
====================
*/
static void VK_Bringup_ReportFormatSupport( VkPhysicalDevice physicalDevice, bool verbose ) {
	typedef struct {
		VkFormat				format;
		const char *			name;
		VkFormatFeatureFlags	requiredOptimalFeatures;
	} formatCheck_t;

	static const formatCheck_t checks[] = {
		{ VK_FORMAT_BC1_RGB_UNORM_BLOCK,	"BC1(DXT1)",		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
		{ VK_FORMAT_BC3_UNORM_BLOCK,		"BC3(DXT5)",		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
		{ VK_FORMAT_BC7_UNORM_BLOCK,		"BC7",				VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
		{ VK_FORMAT_BC6H_UFLOAT_BLOCK,		"BC6H",				VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
		{ VK_FORMAT_BC6H_SFLOAT_BLOCK,		"BC6H signed",		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
		{ VK_FORMAT_R16G16B16A16_SFLOAT,	"RGBA16F",			VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
		{ VK_FORMAT_D24_UNORM_S8_UINT,		"D24S8",			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT },
		{ VK_FORMAT_D32_SFLOAT_S8_UINT,		"D32FS8",			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT },
		{ VK_FORMAT_D32_SFLOAT,				"D32F",				VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT },
	};

	char supported[ 256 ];
	char missing[ 256 ];
	supported[ 0 ] = '\0';
	missing[ 0 ] = '\0';

	for ( size_t i = 0; i < sizeof( checks ) / sizeof( checks[ 0 ] ); i++ ) {
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties( physicalDevice, checks[ i ].format, &formatProps );
		const bool ok = ( formatProps.optimalTilingFeatures & checks[ i ].requiredOptimalFeatures ) == checks[ i ].requiredOptimalFeatures;
		char *target = ok ? supported : missing;
		const size_t targetSize = ok ? sizeof( supported ) : sizeof( missing );
		if ( target[ 0 ] != '\0' ) {
			strncat( target, " ", targetSize - strlen( target ) - 1 );
		}
		strncat( target, checks[ i ].name, targetSize - strlen( target ) - 1 );
	}

	if ( verbose ) {
		VK_Printf( "  formats supported: %s\n", supported[ 0 ] != '\0' ? supported : "(none)" );
	}
	if ( missing[ 0 ] != '\0' ) {
		VK_Printf( "  formats MISSING: %s\n", missing );
	}
}

struct vkBringupProbeReport_t {
	bool verbose;
	char *summary;
	int summaryLength;
};

static void VK_Bringup_ReportDevice( const vkDeviceProbeInfo_t &info, VkResult queryResult, void *user ) {
	const vkBringupProbeReport_t &report = *static_cast<vkBringupProbeReport_t *>( user );
	if ( report.verbose ) {
		VK_Printf( "Vulkan device %u: %s (%s) api=%u.%u.%u vram=%llu MB queues: gfx=%d xfer=%s\n",
				info.index, info.props.deviceName, VK_DeviceTypeName( info.props.deviceType ),
				VK_API_VERSION_MAJOR( info.props.apiVersion ), VK_API_VERSION_MINOR( info.props.apiVersion ), VK_API_VERSION_PATCH( info.props.apiVersion ),
				( unsigned long long )( info.deviceLocalBytes / ( 1024ull * 1024ull ) ),
				info.hasGraphicsQueue ? ( int )info.graphicsQueueFamily : -1,
				info.hasDedicatedTransferQueue ? "dedicated" : "shared" );
		VK_Printf( "  features: swapchain=%d dynamicRendering=%d sync2=%d timelineSemaphore=%d descriptorIndexing=%d bufferDeviceAddress=%d anisotropy=%d bc=%d depthBounds=%d -> %s\n",
				info.hasSwapchain ? 1 : 0,
				info.hasDynamicRendering ? 1 : 0, info.hasSynchronization2 ? 1 : 0, info.hasTimelineSemaphore ? 1 : 0,
				info.hasDescriptorIndexing ? 1 : 0, info.hasBufferDeviceAddress ? 1 : 0, info.hasSamplerAnisotropy ? 1 : 0,
				info.hasTextureCompressionBC ? 1 : 0, info.hasDepthBounds ? 1 : 0,
				info.meetsRequirements ? "SUITABLE" : "unsuitable" );
		if ( queryResult == VK_SUCCESS ) {
			VK_Bringup_ReportPortability( info.physicalDevice, info );
			VK_Bringup_ReportFormatSupport( info.physicalDevice, true );
		}
	}
	if ( queryResult != VK_SUCCESS || !info.meetsRequirements ) {
		char unmet[ 256 ];
		if ( queryResult != VK_SUCCESS ) {
			snprintf( unmet, sizeof( unmet ), "a complete capability query (%s)", VK_ResultName( queryResult ) );
		} else {
			VK_Bringup_DescribeUnmetRequirements( info, unmet, sizeof( unmet ) );
		}
		VK_Printf( "Vulkan bring-up: skipping device %u '%s'; it lacks %s\n", info.index, info.props.deviceName, unmet );
		if ( report.summary != NULL && report.summaryLength > 0 ) {
			snprintf( report.summary, report.summaryLength, "device '%s' lacks %s", info.props.deviceName, unmet );
		}
	}
}

/*
====================
VK_Bringup_RunProbeInternal
====================
*/
static bool VK_Bringup_RunProbeInternal( bool verbose, char *outSummary, int summaryLength, bool holdInstance,
		renderDeviceCatalog_t *catalog = NULL ) {
	VkResult result;

	if ( outSummary != NULL && summaryLength > 0 ) {
		outSummary[ 0 ] = '\0';
	}

	// 1. loader: the library the renderer itself will load
	if ( !vk_volkInitialized ) {
		if ( vk_loaderInit != NULL ) {
			result = vk_loaderInit() ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
		} else {
			result = volkInitialize();
		}
		if ( result != VK_SUCCESS ) {
			VK_Printf( "Vulkan bring-up: no Vulkan loader available on this system (%s)\n", VK_ResultName( result ) );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "no Vulkan loader" );
			}
			return false;
		}
		vk_volkInitialized = true;
	}

	uint32_t loaderVersion = VK_API_VERSION_1_0;
	if ( vkEnumerateInstanceVersion != NULL ) {
		vkEnumerateInstanceVersion( &loaderVersion );
	}
	if ( verbose ) {
		VK_Printf( "Vulkan loader: instance API %u.%u.%u\n",
				VK_API_VERSION_MAJOR( loaderVersion ), VK_API_VERSION_MINOR( loaderVersion ), VK_API_VERSION_PATCH( loaderVersion ) );
	}

	// 2. layers / instance extensions: complete inventories through the shared
	// admission library. A failed or still-changing list is reported, never
	// read as an empty or truncated one.
	bool validationRequested = VK_CVarGetBool( "r_vkValidation" );
	bool validationAvailable = false;
	{
		const char *validationLayer = "VK_LAYER_KHRONOS_validation";
		result = VK_FindInstanceLayers( vkEnumerateInstanceLayerProperties, &validationLayer, 1, &validationAvailable );
		if ( result != VK_SUCCESS && ( validationRequested || verbose ) ) {
			VK_Warning( "Vulkan bring-up: instance-layer enumeration failed (%s); validation is unavailable", VK_ResultName( result ) );
		}
	}
	if ( validationRequested && !validationAvailable ) {
		VK_Warning( "r_vkValidation is set but VK_LAYER_KHRONOS_validation is not installed; continuing without validation" );
	}
	const bool enableValidation = validationRequested && validationAvailable;

	bool hasSurfaceExtension = false;
	bool hasPlatformSurfaceExtension = false;
	bool hasDebugUtils = false;
	bool hasPortabilityEnumeration = false;
	{
		static const char *const names[] = {
			VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
			VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, "VK_KHR_win32_surface", "VK_KHR_xlib_surface",
			"VK_KHR_xcb_surface", "VK_KHR_wayland_surface", "VK_EXT_metal_surface"
		};
		const uint32_t nameCount = (uint32_t)( sizeof( names ) / sizeof( names[ 0 ] ) );
		bool found[ sizeof( names ) / sizeof( names[ 0 ] ) ] = {};
		result = VK_FindInstanceExtensions( vkEnumerateInstanceExtensionProperties, NULL, names, nameCount, found );
		if ( result != VK_SUCCESS ) {
			// The renderer refuses to start on the same failure, so the gate
			// must not report a usable Vulkan from an unread inventory.
			VK_Printf( "Vulkan bring-up: instance-extension enumeration failed (%s)\n", VK_ResultName( result ) );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "instance-extension enumeration failed: %s", VK_ResultName( result ) );
			}
			return false;
		}
		hasSurfaceExtension = found[ 0 ];
		hasDebugUtils = found[ 1 ];
		hasPortabilityEnumeration = found[ 2 ];
		for ( uint32_t i = 3; i < nameCount; i++ ) {
			hasPlatformSurfaceExtension = hasPlatformSurfaceExtension || found[ i ];
		}
	}
	if ( verbose ) {
		VK_Printf( "Vulkan instance: validation=%s (requested=%d available=%d) surface=%d platformSurface=%d debugUtils=%d\n",
				enableValidation ? "on" : "off", validationRequested ? 1 : 0, validationAvailable ? 1 : 0,
				hasSurfaceExtension ? 1 : 0, hasPlatformSurfaceExtension ? 1 : 0, hasDebugUtils ? 1 : 0 );
	}
	if ( !hasSurfaceExtension || !hasPlatformSurfaceExtension ) {
		VK_Warning( "Vulkan bring-up: presentation extensions incomplete (VK_KHR_surface=%d, platform surface=%d); the full renderer will need them",
				hasSurfaceExtension ? 1 : 0, hasPlatformSurfaceExtension ? 1 : 0 );
	}

	// 3. instance
	VkApplicationInfo appInfo;
	memset( &appInfo, 0, sizeof( appInfo ) );
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "openQ4";
	appInfo.pEngineName = "openQ4";
	appInfo.apiVersion = VK_BRINGUP_REQUIRED_API_VERSION;

	const char *enabledLayers[ 1 ];
	uint32_t enabledLayerCount = 0;
	if ( enableValidation ) {
		enabledLayers[ enabledLayerCount++ ] = "VK_LAYER_KHRONOS_validation";
	}
	const char *enabledExtensions[ 2 ];
	uint32_t enabledExtensionCount = 0;
	if ( hasDebugUtils && enableValidation ) {
		enabledExtensions[ enabledExtensionCount++ ] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}
	// mirror the real device path: without portability enumeration the Khronos
	// loader hides portability ICDs (MoltenVK) and this probe would report a
	// driverless machine on a perfectly working Mac
	if ( hasPortabilityEnumeration ) {
		enabledExtensions[ enabledExtensionCount++ ] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
	}

	VkInstanceCreateInfo instanceInfo;
	memset( &instanceInfo, 0, sizeof( instanceInfo ) );
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &appInfo;
	instanceInfo.enabledLayerCount = enabledLayerCount;
	instanceInfo.ppEnabledLayerNames = enabledLayerCount > 0 ? enabledLayers : NULL;
	instanceInfo.enabledExtensionCount = enabledExtensionCount;
	instanceInfo.ppEnabledExtensionNames = enabledExtensionCount > 0 ? enabledExtensions : NULL;
	if ( hasPortabilityEnumeration ) {
		instanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}

	VkInstance instance = VK_NULL_HANDLE;
	result = vkCreateInstance( &instanceInfo, NULL, &instance );
	if ( result != VK_SUCCESS ) {
		VK_Printf( "Vulkan bring-up: vkCreateInstance failed (%s)\n", VK_ResultName( result ) );
		if ( outSummary != NULL ) {
			snprintf( outSummary, summaryLength, "instance creation failed: %s", VK_ResultName( result ) );
		}
		return false;
	}
	volkLoadInstance( instance );

	bool probePassed = false;
	VkDevice device = VK_NULL_HANDLE;
	VmaAllocator allocator = VK_NULL_HANDLE;

	// scope with explicit teardown at the bottom; every failure jumps there
	do {
		// 4. Query complete lists through the same admission library as the
		// renderer. Quiet activation and verbose diagnostics both prefer the
		// highest-ranked suitable GPU, independent of enumeration order.
		vkDeviceSelectionApi_t probeApi = {};
		probeApi.enumeratePhysicalDevices = vkEnumeratePhysicalDevices;
		probeApi.getProperties = vkGetPhysicalDeviceProperties;
		probeApi.getFeatures2 = vkGetPhysicalDeviceFeatures2;
		probeApi.enumerateExtensions = vkEnumerateDeviceExtensionProperties;
		probeApi.getQueueFamilies = vkGetPhysicalDeviceQueueFamilyProperties;
		probeApi.getMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
		probeApi.getProperties2 = vkGetPhysicalDeviceProperties2;
		if (catalog) {
			probePassed = hasSurfaceExtension && hasPlatformSurfaceExtension &&
				VK_QueryDeviceCatalog(probeApi,instance,*catalog)==VK_SUCCESS;
			break;
		}
		const int overrideIndex = VK_CVarGetInteger( "r_vkDevice" );
		vkDeviceProbeInfo_t selected = {};
		vkBringupProbeReport_t report = { verbose, outSummary, summaryLength };
		result = VK_SelectProbeDevice( probeApi, instance, overrideIndex, verbose, selected,
				VK_Bringup_ReportDevice, &report, VK_CVarGetString("r_renderDevice") );
		if ( result != VK_SUCCESS ) {
			VK_Printf( "Vulkan bring-up: no suitable device for r_vkDevice %d (%s)\n", overrideIndex, VK_ResultName( result ) );
			if ( outSummary != NULL && summaryLength > 0 && outSummary[ 0 ] == '\0' ) {
				snprintf( outSummary, summaryLength, "no suitable device for r_vkDevice %d (%s)", overrideIndex, VK_ResultName( result ) );
			}
			break;
		}
		if ( verbose && overrideIndex >= 0 ) {
			VK_Printf( "Vulkan device selection: r_vkDevice override -> %u\n", selected.index );
		}
		VK_Printf( "Vulkan selected device: %s (%s)\n", selected.props.deviceName, VK_DeviceTypeName( selected.props.deviceType ) );
		// Startup's real device path owns the ranked, bounded adapter retries.
		// A throwaway logical device here must not veto those retries. The
		// explicit diagnostic probe below still exercises allocations and sync.
		if (holdInstance) { probePassed = true; break; }

		// 5. logical device with the renderer's required feature set
		float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueInfos[ 2 ];
		memset( queueInfos, 0, sizeof( queueInfos ) );
		uint32_t queueInfoCount = 0;
		queueInfos[ queueInfoCount ].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfos[ queueInfoCount ].queueFamilyIndex = selected.graphicsQueueFamily;
		queueInfos[ queueInfoCount ].queueCount = 1;
		queueInfos[ queueInfoCount ].pQueuePriorities = &queuePriority;
		queueInfoCount++;
		if ( selected.hasDedicatedTransferQueue ) {
			queueInfos[ queueInfoCount ].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfos[ queueInfoCount ].queueFamilyIndex = selected.transferQueueFamily;
			queueInfos[ queueInfoCount ].queueCount = 1;
			queueInfos[ queueInfoCount ].pQueuePriorities = &queuePriority;
			queueInfoCount++;
		}

		VkPhysicalDeviceVulkan13Features enable13;
		memset( &enable13, 0, sizeof( enable13 ) );
		enable13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		enable13.dynamicRendering = VK_TRUE;
		enable13.synchronization2 = VK_TRUE;

		VkPhysicalDeviceVulkan12Features enable12;
		memset( &enable12, 0, sizeof( enable12 ) );
		enable12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		enable12.pNext = &enable13;
		// every optional 1.2 feature is requested only where the device reported
		// it, so a portability implementation that lacks update-after-bind
		// descriptor indexing still creates a probe device instead of failing
		enable12.timelineSemaphore = selected.hasTimelineSemaphore ? VK_TRUE : VK_FALSE;
		enable12.descriptorIndexing = selected.hasDescriptorIndexing ? VK_TRUE : VK_FALSE;
		enable12.runtimeDescriptorArray = selected.hasDescriptorIndexing ? VK_TRUE : VK_FALSE;
		enable12.descriptorBindingPartiallyBound = selected.hasDescriptorIndexing ? VK_TRUE : VK_FALSE;
		enable12.descriptorBindingSampledImageUpdateAfterBind = selected.hasDescriptorIndexing ? VK_TRUE : VK_FALSE;
		enable12.bufferDeviceAddress = selected.hasBufferDeviceAddress ? VK_TRUE : VK_FALSE;

		VkPhysicalDeviceFeatures2 enableFeatures2;
		memset( &enableFeatures2, 0, sizeof( enableFeatures2 ) );
		enableFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		enableFeatures2.pNext = &enable12;
		enableFeatures2.features.samplerAnisotropy = selected.hasSamplerAnisotropy ? VK_TRUE : VK_FALSE;
		enableFeatures2.features.textureCompressionBC = selected.hasTextureCompressionBC ? VK_TRUE : VK_FALSE;
		enableFeatures2.features.depthBounds = selected.hasDepthBounds ? VK_TRUE : VK_FALSE;

		// portability devices (MoltenVK) require their subset extension to be
		// enabled whenever they advertise it
		const char *probeDeviceExtensions[ 1 ];
		uint32_t probeDeviceExtensionCount = 0;
		if ( selected.hasPortabilitySubset ) {
			probeDeviceExtensions[ probeDeviceExtensionCount++ ] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
		}

		VkDeviceCreateInfo deviceInfo;
		memset( &deviceInfo, 0, sizeof( deviceInfo ) );
		deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceInfo.pNext = &enableFeatures2;
		deviceInfo.queueCreateInfoCount = queueInfoCount;
		deviceInfo.pQueueCreateInfos = queueInfos;
		deviceInfo.enabledExtensionCount = probeDeviceExtensionCount;
		deviceInfo.ppEnabledExtensionNames = probeDeviceExtensionCount > 0 ? probeDeviceExtensions : NULL;

		result = vkCreateDevice( selected.physicalDevice, &deviceInfo, NULL, &device );
		if ( result != VK_SUCCESS ) {
			VK_Printf( "Vulkan bring-up: vkCreateDevice failed (%s)\n", VK_ResultName( result ) );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "device creation failed: %s", VK_ResultName( result ) );
			}
			break;
		}
		volkLoadDevice( device );

		VkQueue graphicsQueue = VK_NULL_HANDLE;
		vkGetDeviceQueue( device, selected.graphicsQueueFamily, 0, &graphicsQueue );
		if ( graphicsQueue == VK_NULL_HANDLE ) {
			VK_Printf( "Vulkan bring-up: graphics queue retrieval failed\n" );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "graphics queue retrieval failed" );
			}
			break;
		}

		// 6. VMA + a device-local and a host-visible allocation
		VmaVulkanFunctions vmaFunctions;
		memset( &vmaFunctions, 0, sizeof( vmaFunctions ) );
		vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo allocatorInfo;
		memset( &allocatorInfo, 0, sizeof( allocatorInfo ) );
		allocatorInfo.physicalDevice = selected.physicalDevice;
		allocatorInfo.device = device;
		allocatorInfo.instance = instance;
		allocatorInfo.vulkanApiVersion = VK_BRINGUP_REQUIRED_API_VERSION;
		allocatorInfo.pVulkanFunctions = &vmaFunctions;
		if ( selected.hasBufferDeviceAddress ) {
			allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
		}

		result = vmaCreateAllocator( &allocatorInfo, &allocator );
		if ( result != VK_SUCCESS ) {
			VK_Printf( "Vulkan bring-up: vmaCreateAllocator failed (%s)\n", VK_ResultName( result ) );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "VMA init failed: %s", VK_ResultName( result ) );
			}
			break;
		}

		VkBufferCreateInfo bufferInfo;
		memset( &bufferInfo, 0, sizeof( bufferInfo ) );
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = 1024 * 1024;
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		VmaAllocationCreateInfo allocInfo;
		memset( &allocInfo, 0, sizeof( allocInfo ) );
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

		VkBuffer deviceLocalBuffer = VK_NULL_HANDLE;
		VmaAllocation deviceLocalAllocation = VK_NULL_HANDLE;
		result = vmaCreateBuffer( allocator, &bufferInfo, &allocInfo, &deviceLocalBuffer, &deviceLocalAllocation, NULL );
		if ( result != VK_SUCCESS ) {
			VK_Printf( "Vulkan bring-up: device-local buffer allocation failed (%s)\n", VK_ResultName( result ) );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "device-local allocation failed: %s", VK_ResultName( result ) );
			}
			break;
		}

		bufferInfo.size = 64 * 1024;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VmaAllocation stagingAllocation = VK_NULL_HANDLE;
		VmaAllocationInfo stagingInfo;
		memset( &stagingInfo, 0, sizeof( stagingInfo ) );
		result = vmaCreateBuffer( allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, &stagingInfo );
		bool stagingOk = false;
		if ( result == VK_SUCCESS && stagingInfo.pMappedData != NULL ) {
			memset( stagingInfo.pMappedData, 0xA4, 256 );
			stagingOk = true;
		}
		if ( stagingBuffer != VK_NULL_HANDLE ) {
			vmaDestroyBuffer( allocator, stagingBuffer, stagingAllocation );
		}
		vmaDestroyBuffer( allocator, deviceLocalBuffer, deviceLocalAllocation );
		if ( !stagingOk ) {
			VK_Printf( "Vulkan bring-up: host-visible mapped staging allocation failed (%s)\n", VK_ResultName( result ) );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "staging allocation failed: %s", VK_ResultName( result ) );
			}
			break;
		}
		if ( verbose ) {
			VK_Printf( "Vulkan memory: VMA allocator + device-local and mapped staging buffers OK\n" );
		}

		// 7. timeline semaphore host signal/query round trip
		VkSemaphoreTypeCreateInfo semaphoreTypeInfo;
		memset( &semaphoreTypeInfo, 0, sizeof( semaphoreTypeInfo ) );
		semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		semaphoreTypeInfo.initialValue = 0;

		VkSemaphoreCreateInfo semaphoreInfo;
		memset( &semaphoreInfo, 0, sizeof( semaphoreInfo ) );
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		semaphoreInfo.pNext = &semaphoreTypeInfo;

		VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
		result = vkCreateSemaphore( device, &semaphoreInfo, NULL, &timelineSemaphore );
		bool timelineOk = false;
		if ( result == VK_SUCCESS ) {
			VkSemaphoreSignalInfo signalInfo;
			memset( &signalInfo, 0, sizeof( signalInfo ) );
			signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
			signalInfo.semaphore = timelineSemaphore;
			signalInfo.value = 1;
			if ( vkSignalSemaphore( device, &signalInfo ) == VK_SUCCESS ) {
				uint64_t value = 0;
				if ( vkGetSemaphoreCounterValue( device, timelineSemaphore, &value ) == VK_SUCCESS && value == 1 ) {
					timelineOk = true;
				}
			}
			vkDestroySemaphore( device, timelineSemaphore, NULL );
		}
		if ( !timelineOk ) {
			VK_Printf( "Vulkan bring-up: timeline semaphore round trip failed\n" );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "timeline semaphore round trip failed" );
			}
			break;
		}
		if ( verbose ) {
			VK_Printf( "Vulkan sync: timeline semaphore host signal/query OK\n" );
		}

		if ( outSummary != NULL ) {
			snprintf( outSummary, summaryLength, "device '%s' passed bring-up (api %u.%u, %llu MB VRAM)",
					selected.props.deviceName,
					VK_API_VERSION_MAJOR( selected.props.apiVersion ), VK_API_VERSION_MINOR( selected.props.apiVersion ),
					( unsigned long long )( selected.deviceLocalBytes / ( 1024ull * 1024ull ) ) );
		}
		probePassed = true;
	} while ( false );

	// teardown in reverse order
	if ( allocator != VK_NULL_HANDLE ) {
		vmaDestroyAllocator( allocator );
	}
	if ( device != VK_NULL_HANDLE ) {
		vkDestroyDevice( device, NULL );
	}
	if ( holdInstance && probePassed ) {
		// the renderer creates its own instance next; destroying this one
		// first would unload every Vulkan driver just for it to load them again
		VK_Bringup_ReleaseHeldInstance();
		vk_heldInstance = instance;
	} else {
		vkDestroyInstance( instance, NULL );
	}
	// volk keeps only the loader handle after this; safe for repeat probes

	return probePassed;
}

/*
====================
VK_Bringup_RunProbe
====================
*/
bool VK_Bringup_RunProbe( bool verbose ) {
	return VK_Bringup_RunProbeInternal( verbose, NULL, 0, false );
}

bool VK_Bringup_QueryCatalog(renderDeviceCatalog_t* catalog) {
	return catalog && VK_Bringup_RunProbeInternal(false,NULL,0,false,catalog);
}

/*
====================
VK_Bringup_RunDeviceSelfTest
====================
*/
bool VK_Bringup_RunDeviceSelfTest( char *outSummary, int summaryLength ) {
	return VK_Bringup_RunProbeInternal( false, outSummary, summaryLength, true );
}

/*
====================
VK_Bringup_ReleaseHeldInstance
====================
*/
void VK_Bringup_ReleaseHeldInstance( void ) {
	if ( vk_heldInstance != VK_NULL_HANDLE ) {
		// volk's instance entry points may belong to a newer instance by now;
		// loader and MoltenVK entry points both dispatch on the handle
		vkDestroyInstance( vk_heldInstance, NULL );
		vk_heldInstance = VK_NULL_HANDLE;
	}
}

/*
====================
VK_Bringup_Shutdown
====================
*/
void VK_Bringup_Shutdown( void ) {
	// only a passed gate probe leaves anything behind: its instance
	VK_Bringup_ReleaseHeldInstance();
	vk_services = NULL;
	vk_loaderInit = NULL;
}
