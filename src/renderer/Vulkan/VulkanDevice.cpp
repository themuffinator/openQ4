// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Persistent Vulkan device + swapchain context (Phase C).

	See VulkanDevice.h. All window/surface operations cross the engine's
	renderWindowServices_t; the module never links the windowing library.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../RenderModuleAPI.h"

// VMA consumption needs the CRT declarations the engine PCH poisons/undefs
#undef snprintf
#undef vsnprintf
#ifndef INT_MAX
#define INT_MAX		2147483647
#endif
#ifndef INT_MIN
#define INT_MIN		( -2147483647 - 1 )
#endif
#ifndef UINT_MAX
#define UINT_MAX	0xffffffffu
#endif
#include <cstdio>
#if defined( MACOS_X )
#include <dlfcn.h>
#endif
#include "volk.h"
#include "vk_mem_alloc.h"

#include "VulkanDevice.h"
#include "../RendererMetrics.h"

vkDeviceContext_t vkCtx;

static const renderWindowServices_t *vkWindowServices = NULL;

void VK_Device_BlockPresentation( renderDisplayOutcome_t outcome, VkResult error, const char *operation ) {
	if ( vkCtx.presentationBlocked ) return;
	vkCtx.presentationBlocked = true;
	R_DisplayPresentationFailed( outcome, static_cast<int32_t>( error ) );
	R_DisplayPresentationShutdown();
	common->Warning( "Vulkan: %s failed (%d); presentation requires a full device restart", operation, (int)error );
}

extern idCVar r_vkValidation;
extern idCVar r_vkDevice;
extern idCVar r_swapInterval;

int VK_Device_RequestedSwapInterval( void ) {
	if ( R_IsRecoverableRendererRestart() ) {
		const auto *request = R_GetRecoverableWindowRequest();
		if ( request != NULL ) return request->swapInterval;
	}
	// Compare source values, not IsModified: loading-screen bypass toggles the
	// modified flag without changing the player's setting. Same-value writes
	// are idCVar no-ops and do not relinquish a typed request's ownership.
	if ( vkCtx.strictSwapInterval && r_swapInterval.GetInteger() != vkCtx.strictSwapIntervalCvar ) {
		vkCtx.strictSwapInterval = false;
	}
	return vkCtx.strictSwapInterval ? vkCtx.strictSwapIntervalValue : R_GetEffectiveSwapInterval();
}

static void VK_Device_RecordSwapInterval( int interval ) {
	vkCtx.swapInterval = interval;
	if ( R_IsRecoverableRendererRestart() ) {
		vkCtx.strictSwapInterval = true;
		vkCtx.strictSwapIntervalValue = interval;
		vkCtx.strictSwapIntervalCvar = r_swapInterval.GetInteger();
	}
}

/*
====================
VK_DebugMessengerCallback
====================
*/
static VKAPI_ATTR VkBool32 VKAPI_CALL VK_DebugMessengerCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT types,
		const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
		void *userData ) {
	(void)types;
	(void)userData;
	if ( callbackData == NULL || callbackData->pMessage == NULL ) {
		return VK_FALSE;
	}
	if ( severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) {
		common->Warning( "Vulkan validation: %s", callbackData->pMessage );
	} else if ( severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ) {
		common->Warning( "Vulkan validation: %s", callbackData->pMessage );
	} else {
		common->DPrintf( "Vulkan validation: %s\n", callbackData->pMessage );
	}
	return VK_FALSE;
}

/*
====================
VK_Device_InitLoader

volk resolves the loader itself on Windows and Linux. On macOS its Apple branch
dlopens bare leaf names (libvulkan.dylib, libMoltenVK.dylib, ...), which dyld
resolves against DYLD paths and /usr/local/lib but never against the app
bundle -- so a MoltenVK shipped in openQ4.app/Contents/Frameworks is invisible
to it, while SDL binds that same bundled copy because its own search list starts
at @executable_path/../Frameworks/libMoltenVK.dylib.

The engine creates the surface through SDL and the module creates the instance
through volk, so the two MUST resolve to the same image; two libraries behind
one VkInstance is undefined behavior. This mirrors SDL's precedence exactly:

  1. SDL_VULKAN_LIBRARY (the environment backing of SDL_HINT_VULKAN_LIBRARY)
  2. the bundled MoltenVK in an app bundle, a loose Frameworks directory,
     then beside the executable
  3. whatever volk finds on its own (a system Vulkan loader)

so every case lands on one library. Developers who want validation layers point
SDL_VULKAN_LIBRARY at an SDK loader and both halves follow.
====================
*/
#if defined( MACOS_X )
static bool VK_Device_AdoptLoaderAt( const char *path ) {
	void *handle = dlopen( path, RTLD_NOW | RTLD_LOCAL );
	if ( handle == NULL ) {
		return false;
	}
	PFN_vkGetInstanceProcAddr getInstanceProcAddr =
			(PFN_vkGetInstanceProcAddr)dlsym( handle, "vkGetInstanceProcAddr" );
	if ( getInstanceProcAddr == NULL ) {
		dlclose( handle );
		return false;
	}
	volkInitializeCustom( getInstanceProcAddr );
	common->Printf( "Vulkan: loader %s\n", path );
	return true;
}
#endif

static bool VK_Device_InitLoader( void ) {
#if defined( MACOS_X )
	const char *pinned = getenv( "SDL_VULKAN_LIBRARY" );
	if ( pinned != NULL && pinned[ 0 ] != '\0' && VK_Device_AdoptLoaderAt( pinned ) ) {
		return true;
	}

	idStr exeDir = Sys_EXEPath();
	exeDir.StripFilename();

	// openQ4.app/Contents/MacOS/openQ4 -> openQ4.app/Contents/Frameworks, then
	// either supported loose package layout: Frameworks/ or executable-adjacent.
	const idStr bundled = exeDir + "/../Frameworks/libMoltenVK.dylib";
	if ( VK_Device_AdoptLoaderAt( bundled.c_str() ) ) {
		return true;
	}
	const idStr looseFramework = exeDir + "/Frameworks/libMoltenVK.dylib";
	if ( VK_Device_AdoptLoaderAt( looseFramework.c_str() ) ) {
		return true;
	}
	const idStr adjacent = exeDir + "/libMoltenVK.dylib";
	if ( VK_Device_AdoptLoaderAt( adjacent.c_str() ) ) {
		return true;
	}
#endif
	return volkInitialize() == VK_SUCCESS;
}

/*
====================
VK_Device_InstanceExtensionSupported

Vulkan Portability negotiation. VK_KHR_portability_enumeration is a *loader*
extension: the Khronos loader advertises it and REQUIRES both the extension and
VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR before it will enumerate a
portability ICD such as MoltenVK (otherwise vkCreateInstance fails with
VK_ERROR_INCOMPATIBLE_DRIVER). A directly loaded libMoltenVK.dylib does NOT
advertise it, and requesting it there fails with VK_ERROR_EXTENSION_NOT_PRESENT.
Probing instead of hardcoding keeps one code path correct for both, and is a
no-op on native Windows/Linux drivers.
====================
*/
static bool VK_Device_InstanceExtensionSupported( const char *name ) {
	uint32_t count = 0;
	if ( vkEnumerateInstanceExtensionProperties( NULL, &count, NULL ) != VK_SUCCESS || count == 0 ) {
		return false;
	}
	if ( count > 512 ) {
		count = 512;
	}
	VkExtensionProperties *properties = (VkExtensionProperties *)Mem_Alloc( count * sizeof( VkExtensionProperties ) );
	if ( properties == NULL ) {
		return false;
	}
	bool found = false;
	if ( vkEnumerateInstanceExtensionProperties( NULL, &count, properties ) == VK_SUCCESS ) {
		for ( uint32_t i = 0; i < count; i++ ) {
			if ( idStr::Cmp( properties[ i ].extensionName, name ) == 0 ) {
				found = true;
				break;
			}
		}
	}
	Mem_Free( properties );
	return found;
}

/*
====================
VK_Device_DeviceExtensionSupported

VK_KHR_portability_subset must be enabled whenever the device advertises it
(VUID-VkDeviceCreateInfo-pProperties-04451), and must NOT be requested when it
does not -- MoltenVK's advertiseExtensions configuration can suppress it.
====================
*/
static bool VK_Device_DeviceExtensionSupported( VkPhysicalDevice device, const char *name ) {
	uint32_t count = 0;
	if ( vkEnumerateDeviceExtensionProperties( device, NULL, &count, NULL ) != VK_SUCCESS || count == 0 ) {
		return false;
	}
	if ( count > 1024 ) {
		count = 1024;
	}
	VkExtensionProperties *properties = (VkExtensionProperties *)Mem_Alloc( count * sizeof( VkExtensionProperties ) );
	if ( properties == NULL ) {
		return false;
	}
	bool found = false;
	if ( vkEnumerateDeviceExtensionProperties( device, NULL, &count, properties ) == VK_SUCCESS ) {
		for ( uint32_t i = 0; i < count; i++ ) {
			if ( idStr::Cmp( properties[ i ].extensionName, name ) == 0 ) {
				found = true;
				break;
			}
		}
	}
	Mem_Free( properties );
	return found;
}

/*
====================
VK_Device_DepthFormatHasStencil
====================
*/
static bool VK_Device_DepthFormatHasStencil( VkFormat format ) {
	switch ( format ) {
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return true;
		default:
			return false;
	}
}

/*
====================
VK_Device_SelectShadowDepthFormat

Shadow maps are depth attachments and sampled compare images; projected-cache
reuse also copies depth tiles to and from the live atlas. Probe that combined
usage independently from the main depth/stencil attachment so a depth-only
format remains a valid fallback. Vulkan 1.3's comparison feature bit prevents
selecting a generally sampleable format that sampler*Shadow cannot use.
Preserve candidate priority within each tier, but prefer a linearly filterable
format before accepting the nearest-filter fallback.
====================
*/
static void VK_Device_SelectShadowDepthFormat( void ) {
	vkCtx.shadowDepthFormat = VK_FORMAT_UNDEFINED;
	vkCtx.shadowDepthHasStencil = false;
	vkCtx.shadowDepthFilterLinear = false;

	static const VkFormat candidates[] = {
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_X8_D24_UNORM_PACK32,
		VK_FORMAT_D16_UNORM,
		VK_FORMAT_D16_UNORM_S8_UINT
	};
	const VkFormatFeatureFlags2 requiredFeatures =
			VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT |
			VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT |
			VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
			VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
	VkFormat nearestFallback = VK_FORMAT_UNDEFINED;
	bool nearestFallbackHasStencil = false;

	for ( int i = 0; i < (int)( sizeof( candidates ) / sizeof( candidates[ 0 ] ) ); i++ ) {
		VkFormatProperties3 props3;
		memset( &props3, 0, sizeof( props3 ) );
		props3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
		VkFormatProperties2 props2;
		memset( &props2, 0, sizeof( props2 ) );
		props2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
		props2.pNext = &props3;
		vkGetPhysicalDeviceFormatProperties2( vkCtx.physicalDevice, candidates[ i ], &props2 );

		// VkFormatProperties3 is core 1.3 and every conformant implementation at
		// this renderer's API floor populates it (MoltenVK included, since 1.2.7).
		// If one ever leaves it zeroed, fall back to the 1.0 flags rather than
		// silently rejecting every candidate and disabling shadows: the depth
		// comparison bit has no 1.0 equivalent, so it is approximated by the
		// sampled-image + depth-attachment pair that implies it in practice.
		VkFormatFeatureFlags2 optimalFeatures = props3.optimalTilingFeatures;
		if ( optimalFeatures == 0 ) {
			const VkFormatFeatureFlags legacy = props2.formatProperties.optimalTilingFeatures;
			optimalFeatures = (VkFormatFeatureFlags2)legacy;
			const VkFormatFeatureFlags legacyCompareProxy =
					VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
			if ( ( legacy & legacyCompareProxy ) == legacyCompareProxy ) {
				optimalFeatures |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
			}
		}

		if ( ( optimalFeatures & requiredFeatures ) != requiredFeatures ) {
			continue;
		}

		const bool linearFilter =
				( optimalFeatures & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT ) != 0;
		if ( !linearFilter ) {
			if ( nearestFallback == VK_FORMAT_UNDEFINED ) {
				nearestFallback = candidates[ i ];
				nearestFallbackHasStencil = VK_Device_DepthFormatHasStencil( candidates[ i ] );
			}
			continue;
		}

		vkCtx.shadowDepthFormat = candidates[ i ];
		vkCtx.shadowDepthHasStencil = VK_Device_DepthFormatHasStencil( candidates[ i ] );
		vkCtx.shadowDepthFilterLinear = true;
		break;
	}

	if ( vkCtx.shadowDepthFormat == VK_FORMAT_UNDEFINED && nearestFallback != VK_FORMAT_UNDEFINED ) {
		vkCtx.shadowDepthFormat = nearestFallback;
		vkCtx.shadowDepthHasStencil = nearestFallbackHasStencil;
	}

	if ( vkCtx.shadowDepthFormat == VK_FORMAT_UNDEFINED ) {
		common->Warning( "Vulkan: no attachment+sampled+transfer compare-capable depth format available; shadow maps disabled" );
		return;
	}

	common->Printf( "Vulkan: shadow depth format=%d, compare filtering=%s\n",
			(int)vkCtx.shadowDepthFormat, vkCtx.shadowDepthFilterLinear ? "linear" : "nearest" );
}

/*
====================
VK_Device_DestroySwapchainObjects
====================
*/
static void VK_Device_DestroySwapchainObjects( void ) {
	for ( uint32_t i = 0; i < vkCtx.swapchainImageCount; i++ ) {
		if ( vkCtx.swapchainViews[ i ] != VK_NULL_HANDLE ) {
			vkDestroyImageView( vkCtx.device, vkCtx.swapchainViews[ i ], NULL );
			vkCtx.swapchainViews[ i ] = VK_NULL_HANDLE;
		}
		if ( vkCtx.renderFinishedSemaphores[ i ] != VK_NULL_HANDLE ) {
			vkDestroySemaphore( vkCtx.device, vkCtx.renderFinishedSemaphores[ i ], NULL );
			vkCtx.renderFinishedSemaphores[ i ] = VK_NULL_HANDLE;
		}
	}
	if ( vkCtx.swapchain != VK_NULL_HANDLE ) {
		vkDestroySwapchainKHR( vkCtx.device, vkCtx.swapchain, NULL );
		vkCtx.swapchain = VK_NULL_HANDLE;
	}
	vkCtx.swapchainImageCount = 0;
	vkCtx.swapchainTransferSrc = false;

	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( vkCtx.depthViews[ i ] != VK_NULL_HANDLE ) {
			vkDestroyImageView( vkCtx.device, vkCtx.depthViews[ i ], NULL );
			vkCtx.depthViews[ i ] = VK_NULL_HANDLE;
		}
		if ( vkCtx.depthImages[ i ] != VK_NULL_HANDLE ) {
			vmaDestroyImage( vkCtx.allocator, vkCtx.depthImages[ i ], vkCtx.depthAllocations[ i ] );
			vkCtx.depthImages[ i ] = VK_NULL_HANDLE;
			vkCtx.depthAllocations[ i ] = NULL;
		}
	}
}

/*
====================
VK_Device_CreateDepthImages

Per-frame-slot depth/stencil attachments sized to the swapchain. Stencil is
carried now so the Phase G stencil-shadow work needs no format churn.
====================
*/
static bool VK_Device_CreateDepthImages( void ) {
	if ( vkCtx.depthFormat == VK_FORMAT_UNDEFINED ) {
		const VkFormat candidates[ 2 ] = { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT };
		for ( int i = 0; i < 2; i++ ) {
			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties( vkCtx.physicalDevice, candidates[ i ], &props );
			if ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) {
				vkCtx.depthFormat = candidates[ i ];
				break;
			}
		}
		if ( vkCtx.depthFormat == VK_FORMAT_UNDEFINED ) {
			common->Warning( "Vulkan: no depth/stencil attachment format available" );
			return false;
		}
	}

	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		VkImageCreateInfo ici;
		memset( &ici, 0, sizeof( ici ) );
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = vkCtx.depthFormat;
		ici.extent.width = vkCtx.swapchainExtent.width;
		ici.extent.height = vkCtx.swapchainExtent.height;
		ici.extent.depth = 1;
		ici.mipLevels = 1;
		ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		VmaAllocationCreateInfo vaci;
		memset( &vaci, 0, sizeof( vaci ) );
		vaci.usage = VMA_MEMORY_USAGE_AUTO;

		if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci, &vkCtx.depthImages[ i ],
				&vkCtx.depthAllocations[ i ], NULL ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: depth image creation failed (%ux%u)",
					vkCtx.swapchainExtent.width, vkCtx.swapchainExtent.height );
			return false;
		}

		VkImageViewCreateInfo ivci;
		memset( &ivci, 0, sizeof( ivci ) );
		ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ivci.image = vkCtx.depthImages[ i ];
		ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivci.format = vkCtx.depthFormat;
		ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		ivci.subresourceRange.levelCount = 1;
		ivci.subresourceRange.layerCount = 1;
		if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &vkCtx.depthViews[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: depth image view creation failed" );
			return false;
		}
	}
	return true;
}

/*
====================
VK_Device_CreateSwapchain
====================
*/
static bool VK_Device_SelectPresentMode( int requestedInterval, bool strict, VkPresentModeKHR &selected ) {
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if ( requestedInterval == 0 || ( strict && requestedInterval == -1 ) ) {
		uint32_t modeCount = 0;
		VkResult modeResult = vkGetPhysicalDeviceSurfacePresentModesKHR( vkCtx.physicalDevice, vkCtx.surface, &modeCount, NULL );
		if ( modeResult != VK_SUCCESS ) { R_DisplayPresentationFailed( RDP_RECREATE_FAILED, (int32_t)modeResult ); return false; }
		if ( modeCount > 16 ) modeCount = 16;
		VkPresentModeKHR modes[ 16 ];
		modeResult = vkGetPhysicalDeviceSurfacePresentModesKHR( vkCtx.physicalDevice, vkCtx.surface, &modeCount, modes );
		if ( modeResult != VK_SUCCESS && modeResult != VK_INCOMPLETE ) { R_DisplayPresentationFailed( RDP_RECREATE_FAILED, (int32_t)modeResult ); return false; }
		for ( uint32_t i = 0; i < modeCount; i++ ) {
			if ( requestedInterval == -1 ) {
				if ( modes[ i ] == VK_PRESENT_MODE_FIFO_RELAXED_KHR ) presentMode = modes[ i ];
				continue;
			}
			if ( modes[ i ] == VK_PRESENT_MODE_IMMEDIATE_KHR ) { presentMode = modes[ i ]; break; }
			if ( modes[ i ] == VK_PRESENT_MODE_MAILBOX_KHR ) presentMode = modes[ i ];
		}
	}
	if ( strict && requestedInterval != 1 && presentMode == VK_PRESENT_MODE_FIFO_KHR ) {
		common->Warning( "Vulkan: requested swap interval %d has no supported present mode", requestedInterval );
		return false;
	}
	selected = presentMode;
	return true;
}

static bool VK_Device_CreateSwapchain( void ) {
	VkSurfaceCapabilitiesKHR caps;
	if ( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vkCtx.physicalDevice, vkCtx.surface, &caps ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed" );
		return false;
	}

	// The legacy renderer already produces display-coded SDR values.  Use a
	// non-sRGB UNORM attachment with the standard nonlinear presentation colour
	// space so attachment writes do not encode those values a second time.
	uint32_t formatCount = 0;
	VkResult formatResult = vkGetPhysicalDeviceSurfaceFormatsKHR(
		vkCtx.physicalDevice, vkCtx.surface, &formatCount, NULL );
	if ( formatResult != VK_SUCCESS ) {
		common->Warning( "Vulkan: surface-format count query failed (%d)", (int)formatResult );
		return false;
	}
	if ( formatCount == 0 ) {
		common->Warning( "Vulkan: surface reports no formats" );
		return false;
	}
	if ( formatCount > 64 ) {
		formatCount = 64;
	}
	VkSurfaceFormatKHR formats[ 64 ];
	formatResult = vkGetPhysicalDeviceSurfaceFormatsKHR(
		vkCtx.physicalDevice, vkCtx.surface, &formatCount, formats );
	if ( formatResult != VK_SUCCESS && formatResult != VK_INCOMPLETE ) {
		common->Warning( "Vulkan: surface-format query failed (%d)", (int)formatResult );
		return false;
	}
	VkSurfaceFormatKHR chosen;
	memset( &chosen, 0, sizeof( chosen ) );
	chosen.format = VK_FORMAT_UNDEFINED;
	bool compatibleSurfaceFormat = false;
	for ( uint32_t i = 0; i < formatCount; i++ ) {
		if ( ( formats[ i ].format == VK_FORMAT_B8G8R8A8_UNORM || formats[ i ].format == VK_FORMAT_R8G8B8A8_UNORM )
				&& formats[ i ].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
			chosen = formats[ i ];
			compatibleSurfaceFormat = true;
			break;
		}
	}
	if ( !compatibleSurfaceFormat ) {
		for ( uint32_t i = 0; i < formatCount; i++ ) {
			if ( formats[ i ].format == VK_FORMAT_UNDEFINED
					&& formats[ i ].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
				chosen.format = VK_FORMAT_B8G8R8A8_UNORM;
				chosen.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
				compatibleSurfaceFormat = true;
				break;
			}
		}
	}
	if ( !compatibleSurfaceFormat ) {
		common->Warning( "Vulkan: surface has no compatible legacy SDR UNORM + SRGB_NONLINEAR format" );
		return false;
	}

	// Keep the applied typed interval across resize/out-of-date recreation.
	// Legacy configuration resumes only when its source CVar value changes.
	const auto* strictRequest = R_GetRecoverableWindowRequest();
	if ( R_IsRecoverableRendererRestart() && ( strictRequest == NULL || strictRequest->swapInterval < -1 || strictRequest->swapInterval > 1 ) ) return false;
	const int requestedInterval = VK_Device_RequestedSwapInterval();
	const bool strict = R_IsRecoverableRendererRestart() || vkCtx.strictSwapInterval;
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if ( !VK_Device_SelectPresentMode( requestedInterval, strict, presentMode ) ) return false;

	VkExtent2D extent = caps.currentExtent;
	if ( extent.width == 0xFFFFFFFFu ) {
		// surface size is window-driven; poll the engine window
		renderModuleWindowInfo_t info;
		memset( &info, 0, sizeof( info ) );
		if ( vkWindowServices != NULL && vkWindowServices->RefreshNativeWindowHandles != NULL ) {
			vkWindowServices->RefreshNativeWindowHandles( &info );
		}
		extent.width = info.pixelWidth > 0 ? (uint32_t)info.pixelWidth : 640u;
		extent.height = info.pixelHeight > 0 ? (uint32_t)info.pixelHeight : 480u;
	}
	if ( extent.width < caps.minImageExtent.width ) {
		extent.width = caps.minImageExtent.width;
	}
	if ( extent.height < caps.minImageExtent.height ) {
		extent.height = caps.minImageExtent.height;
	}
	if ( caps.maxImageExtent.width > 0 && extent.width > caps.maxImageExtent.width ) {
		extent.width = caps.maxImageExtent.width;
	}
	if ( caps.maxImageExtent.height > 0 && extent.height > caps.maxImageExtent.height ) {
		extent.height = caps.maxImageExtent.height;
	}
	if ( extent.width == 0 || extent.height == 0 ) {
		// minimized window; keep the old swapchain until a real size shows up
		return false;
	}

	uint32_t imageCount = caps.minImageCount + 1;
	if ( caps.maxImageCount > 0 && imageCount > caps.maxImageCount ) {
		imageCount = caps.maxImageCount;
	}
	if ( imageCount > 8 ) {
		imageCount = 8;
	}

	VkSwapchainCreateInfoKHR sci;
	memset( &sci, 0, sizeof( sci ) );
	sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	sci.surface = vkCtx.surface;
	sci.minImageCount = imageCount;
	sci.imageFormat = chosen.format;
	sci.imageColorSpace = chosen.colorSpace;
	sci.imageExtent = extent;
	sci.imageArrayLayers = 1;
	sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	const bool transferSrcSupported = ( caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) != 0;
	if ( transferSrcSupported ) {
		sci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	sci.preTransform = caps.currentTransform;
	sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	sci.presentMode = presentMode;
	sci.clipped = VK_TRUE;
	sci.oldSwapchain = vkCtx.swapchain;

	VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
	const VkResult res = vkCreateSwapchainKHR( vkCtx.device, &sci, NULL, &newSwapchain );
	if ( res != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkCreateSwapchainKHR failed (%d)", (int)res );
		R_DisplayPresentationFailed( RDP_RECREATE_FAILED, (int32_t)res );
		return false;
	}

	// the old swapchain (if any) is retired by the create; destroy our
	// per-image objects and the old handle
	VK_Device_DestroySwapchainObjects();
	vkCtx.swapchain = newSwapchain;
	vkCtx.swapchainFormat = chosen.format;
	vkCtx.swapchainExtent = extent;
	vkCtx.presentMode = presentMode;
	vkCtx.swapchainTransferSrc = transferSrcSupported;

	uint32_t count = 0;
	if ( vkGetSwapchainImagesKHR( vkCtx.device, vkCtx.swapchain, &count, NULL ) != VK_SUCCESS || count == 0 ) {
		common->Warning( "Vulkan: swapchain image count query failed" );
		VK_Device_DestroySwapchainObjects();
		return false;
	}
	if ( count > 8 ) {
		// swapchainImages/Views/renderFinishedSemaphores are fixed at 8 and are
		// indexed by the image index vkAcquireNextImageKHR returns, which can be
		// any value below the actual image count. An implementation that creates
		// more than requested would index them out of bounds, so fail closed
		// rather than clamp the count while the driver still owns more images.
		common->Warning( "Vulkan: swapchain created %u images, exceeding the supported maximum of 8", count );
		VK_Device_DestroySwapchainObjects();
		return false;
	}
	if ( vkGetSwapchainImagesKHR( vkCtx.device, vkCtx.swapchain, &count, vkCtx.swapchainImages ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: swapchain image retrieval failed" );
		VK_Device_DestroySwapchainObjects();
		return false;
	}
	vkCtx.swapchainImageCount = count;

	for ( uint32_t i = 0; i < count; i++ ) {
		VkImageViewCreateInfo ivci;
		memset( &ivci, 0, sizeof( ivci ) );
		ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ivci.image = vkCtx.swapchainImages[ i ];
		ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivci.format = vkCtx.swapchainFormat;
		ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ivci.subresourceRange.levelCount = 1;
		ivci.subresourceRange.layerCount = 1;
		if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &vkCtx.swapchainViews[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: swapchain image view creation failed" );
			// no half-built swapchain may survive: the frame path uses the
			// per-image objects and depth images unconditionally
			VK_Device_DestroySwapchainObjects();
			return false;
		}

		VkSemaphoreCreateInfo semci;
		memset( &semci, 0, sizeof( semci ) );
		semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		if ( vkCreateSemaphore( vkCtx.device, &semci, NULL, &vkCtx.renderFinishedSemaphores[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: render-finished semaphore creation failed" );
			VK_Device_DestroySwapchainObjects();
			return false;
		}
	}

	if ( !VK_Device_CreateDepthImages() ) {
		VK_Device_DestroySwapchainObjects();
		return false;
	}

	common->Printf( "Vulkan: created swapchain %ux%u format=%d colorSpace=%d images=%u presentMode=%d\n",
			extent.width, extent.height, (int)chosen.format, (int)chosen.colorSpace,
			count, (int)presentMode );
	if ( !vkCtx.swapchainTransferSrc ) {
		common->Warning( "Vulkan: swapchain does not support transfer-source captures; screenshots and backbuffer feedback are unavailable" );
	}
	const int actualInterval = presentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR ? -1 :
		presentMode == VK_PRESENT_MODE_FIFO_KHR ? 1 : 0;
	VK_Device_RecordSwapInterval( requestedInterval );
	R_DisplayPresentationParameters( 0, actualInterval, (int32_t)presentMode,
		RDP_PARAMETER_SAMPLES | RDP_PARAMETER_SWAP_INTERVAL | RDP_PARAMETER_PRESENT_MODE );
	return true;
}

/*
====================
VK_Device_CountDrawIndexed
====================
*/
void VK_Device_CountDrawIndexed( int indexCount, int vertexCount ) {
	backEnd.pc.c_drawElements++;
	backEnd.pc.c_drawIndexes += indexCount;
	backEnd.pc.c_drawVertexes += vertexCount;
}

/*
====================
Pipeline cache

The driver keeps its compiled pipelines in an opaque blob. Without one, every
session re-compiles every pipeline the moment its material/state combination is
first drawn, which shows up as first-encounter hitching rather than as a lower
average frame rate. The blob is a disposable generated cache under fs_savepath:
it is read from the savepath only, never from a PK4, and a blob that does not
match this device is discarded rather than handed to the driver.
====================
*/
static const char *VK_PIPELINE_CACHE_FILE = "generated/vulkan/pipeline.cache";

// VkPipelineCacheHeaderVersionOne: 32 bytes, little-endian.
static const int VK_PIPELINE_CACHE_HEADER_BYTES = 32;

static bool VK_Device_PipelineCacheBlobMatchesDevice( const byte *blob, int blobBytes ) {
	if ( blob == NULL || blobBytes < VK_PIPELINE_CACHE_HEADER_BYTES ) {
		return false;
	}
	uint32_t headerSize = 0;
	uint32_t headerVersion = 0;
	uint32_t vendorID = 0;
	uint32_t deviceID = 0;
	memcpy( &headerSize, blob + 0, sizeof( headerSize ) );
	memcpy( &headerVersion, blob + 4, sizeof( headerVersion ) );
	memcpy( &vendorID, blob + 8, sizeof( vendorID ) );
	memcpy( &deviceID, blob + 12, sizeof( deviceID ) );
	if ( headerSize != (uint32_t)VK_PIPELINE_CACHE_HEADER_BYTES ||
			headerVersion != (uint32_t)VK_PIPELINE_CACHE_HEADER_VERSION_ONE ) {
		return false;
	}
	if ( vendorID != vkCtx.deviceProperties.vendorID ||
			deviceID != vkCtx.deviceProperties.deviceID ) {
		return false;
	}
	return memcmp( blob + 16, vkCtx.deviceProperties.pipelineCacheUUID, VK_UUID_SIZE ) == 0;
}

static bool VK_Device_CreatePipelineCache( void ) {
	VkPipelineCacheCreateInfo pcci;
	memset( &pcci, 0, sizeof( pcci ) );
	pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

	byte *blob = NULL;
	int blobBytes = 0;
	const char *osPath = fileSystem->RelativePathToOSPath( VK_PIPELINE_CACHE_FILE, "fs_savepath" );
	idFile *file = osPath != NULL ? fileSystem->OpenExplicitFileRead( osPath ) : NULL;
	if ( file != NULL ) {
		const int length = file->Length();
		if ( length >= VK_PIPELINE_CACHE_HEADER_BYTES && length < ( 256 << 20 ) ) {
			blob = (byte *)Mem_Alloc( length );
			if ( file->Read( blob, length ) == length ) {
				blobBytes = length;
			} else {
				Mem_Free( blob );
				blob = NULL;
			}
		}
		fileSystem->CloseFile( file );
	}

	if ( blob != NULL && !VK_Device_PipelineCacheBlobMatchesDevice( blob, blobBytes ) ) {
		// a blob from another GPU or driver build: start empty rather than hand
		// the driver data the spec does not require it to reject
		common->Printf( "Vulkan: discarding pipeline cache written by a different device/driver\n" );
		Mem_Free( blob );
		blob = NULL;
		blobBytes = 0;
	}
	if ( blob != NULL ) {
		pcci.initialDataSize = (size_t)blobBytes;
		pcci.pInitialData = blob;
	}

	const VkResult res = vkCreatePipelineCache( vkCtx.device, &pcci, NULL, &vkCtx.pipelineCache );
	if ( blob != NULL ) {
		Mem_Free( blob );
	}
	if ( res != VK_SUCCESS ) {
		// not fatal: pipeline creation accepts VK_NULL_HANDLE
		vkCtx.pipelineCache = VK_NULL_HANDLE;
		common->Warning( "Vulkan: pipeline cache creation failed (%d); pipelines will not be cached",
				static_cast<int>( res ) );
		return false;
	}
	common->Printf( "Vulkan: pipeline cache %s\n", blobBytes > 0 ? "restored" : "created empty" );
	return true;
}

static void VK_Device_SavePipelineCache( void ) {
	if ( vkCtx.device == VK_NULL_HANDLE || vkCtx.pipelineCache == VK_NULL_HANDLE ) {
		return;
	}
	size_t blobBytes = 0;
	if ( vkGetPipelineCacheData( vkCtx.device, vkCtx.pipelineCache, &blobBytes, NULL ) != VK_SUCCESS
			|| blobBytes < (size_t)VK_PIPELINE_CACHE_HEADER_BYTES ) {
		return;
	}
	byte *blob = (byte *)Mem_Alloc( (int)blobBytes );
	if ( vkGetPipelineCacheData( vkCtx.device, vkCtx.pipelineCache, &blobBytes, blob ) != VK_SUCCESS ) {
		Mem_Free( blob );
		return;
	}
	idFile *file = fileSystem->OpenFileWrite( VK_PIPELINE_CACHE_FILE, "fs_savepath" );
	if ( file != NULL ) {
		file->Write( blob, (int)blobBytes );
		fileSystem->CloseFile( file );
	}
	Mem_Free( blob );
}

/*
====================
VK_Device_RecreateSwapchain
====================
*/
bool VK_Device_RecreateSwapchain( void ) {
	if ( !vkCtx.initialized || vkCtx.presentationBlocked ) {
		return false;
	}
	renderDisplayChangeScope_t presentation( RDP_RECREATE_FAILED );
	const VkResult waited = vkDeviceWaitIdle( vkCtx.device );
	if ( waited != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_WAIT_FAILED, waited, "swapchain idle wait" ); return false; }
	R_RendererMetrics_ResetGpuFrameTiming( "Vulkan swapchain recreation" );
	if ( !VK_Device_CreateSwapchain() ) return false;
	presentation.Succeeded(); return true;
}

/*
====================
VK_Device_Init
====================
*/
bool VK_Device_Init( const renderWindowServices_s *windowServices ) {
	renderDisplayChangeScope_t presentation( RDP_INIT_FAILED );
	memset( &vkCtx, 0, sizeof( vkCtx ) );
	vkWindowServices = windowServices;

	if ( windowServices == NULL || windowServices->CreateVulkanSurface == NULL
			|| windowServices->GetVulkanInstanceExtensions == NULL ) {
		common->Warning( "Vulkan: window services carry no Vulkan surface support" );
		return false;
	}

	if ( !VK_Device_InitLoader() ) {
		common->Warning( "Vulkan: no Vulkan loader available (volkInitialize failed)" );
		return false;
	}

	// instance extensions: the window system's requirements plus debug utils
	// when validation is requested
	const char *extensions[ 24 ];
	int extensionCount = 0;
	if ( !windowServices->GetVulkanInstanceExtensions( extensions, 20, &extensionCount ) || extensionCount <= 0 ) {
		common->Warning( "Vulkan: window system reports no instance extensions" );
		return false;
	}
	if ( extensionCount > 20 ) {
		common->Warning( "Vulkan: instance extension list truncated (%d)", extensionCount );
		extensionCount = 20;
	}

	const bool wantValidation = r_vkValidation.GetBool();
	if ( wantValidation ) {
		extensions[ extensionCount++ ] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}

	// portability enumeration: required by the Khronos loader before it will
	// surface MoltenVK, rejected by a directly loaded MoltenVK, absent-but-
	// harmless nowhere -- so it is strictly presence-gated. The flag bit is
	// only ever set together with the extension
	// (VUID-VkInstanceCreateInfo-flags-06559).
	const bool portabilityEnumeration =
			VK_Device_InstanceExtensionSupported( VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME );
	if ( portabilityEnumeration ) {
		extensions[ extensionCount++ ] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
	}

	const char *validationLayer = "VK_LAYER_KHRONOS_validation";

	VkApplicationInfo appInfo;
	memset( &appInfo, 0, sizeof( appInfo ) );
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "openQ4";
	appInfo.pEngineName = "openQ4";
	appInfo.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ici.pApplicationInfo = &appInfo;
	ici.enabledExtensionCount = (uint32_t)extensionCount;
	ici.ppEnabledExtensionNames = extensions;
	if ( portabilityEnumeration ) {
		ici.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}
	if ( wantValidation ) {
		ici.enabledLayerCount = 1;
		ici.ppEnabledLayerNames = &validationLayer;
	}

	VkResult res = vkCreateInstance( &ici, NULL, &vkCtx.instance );
	if ( res != VK_SUCCESS && wantValidation ) {
		common->Warning( "Vulkan: instance creation with validation failed (%d); retrying without", (int)res );
		ici.enabledLayerCount = 0;
		ici.ppEnabledLayerNames = NULL;
		res = vkCreateInstance( &ici, NULL, &vkCtx.instance );
	}
	if ( res != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkCreateInstance failed (%d)", (int)res );
		return false;
	}
	volkLoadInstance( vkCtx.instance );

	if ( wantValidation && vkCreateDebugUtilsMessengerEXT != NULL ) {
		VkDebugUtilsMessengerCreateInfoEXT dmci;
		memset( &dmci, 0, sizeof( dmci ) );
		dmci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		dmci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		dmci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		dmci.pfnUserCallback = VK_DebugMessengerCallback;
		vkCreateDebugUtilsMessengerEXT( vkCtx.instance, &dmci, NULL, &vkCtx.debugMessenger );
	}

	// surface through the engine's window services
	unsigned long long surfaceHandle = 0;
	if ( !windowServices->CreateVulkanSurface( (void *)vkCtx.instance, &surfaceHandle ) || surfaceHandle == 0 ) {
		common->Warning( "Vulkan: surface creation through the window services failed" );
		VK_Device_Shutdown();
		return false;
	}
	vkCtx.surface = (VkSurfaceKHR)surfaceHandle;

	// physical device: honor r_vkDevice when set, else first suitable
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices( vkCtx.instance, &deviceCount, NULL );
	if ( deviceCount == 0 ) {
		common->Warning( "Vulkan: no physical devices" );
		VK_Device_Shutdown();
		return false;
	}
	if ( deviceCount > 16 ) {
		deviceCount = 16;
	}
	VkPhysicalDevice devices[ 16 ];
	vkEnumeratePhysicalDevices( vkCtx.instance, &deviceCount, devices );

	const int forcedDevice = r_vkDevice.GetInteger();
	int chosenDevice = -1;
	uint32_t chosenQueueFamily = 0;
	uint32_t chosenTimestampValidBits = 0;

	for ( uint32_t d = 0; d < deviceCount; d++ ) {
		if ( forcedDevice >= 0 && (int)d != forcedDevice ) {
			continue;
		}
		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties( devices[ d ], &familyCount, NULL );
		if ( familyCount > 16 ) {
			familyCount = 16;
		}
		VkQueueFamilyProperties families[ 16 ];
		vkGetPhysicalDeviceQueueFamilyProperties( devices[ d ], &familyCount, families );
		for ( uint32_t f = 0; f < familyCount; f++ ) {
			if ( !( families[ f ].queueFlags & VK_QUEUE_GRAPHICS_BIT ) ) {
				continue;
			}
			VkBool32 presentable = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR( devices[ d ], f, vkCtx.surface, &presentable );
			if ( presentable ) {
				chosenDevice = (int)d;
				chosenQueueFamily = f;
				chosenTimestampValidBits = families[ f ].timestampValidBits;
				break;
			}
		}
		if ( chosenDevice >= 0 ) {
			break;
		}
	}
	if ( chosenDevice < 0 ) {
		common->Warning( "Vulkan: no graphics+present capable device%s",
				forcedDevice >= 0 ? " (r_vkDevice selection rejected)" : "" );
		VK_Device_Shutdown();
		return false;
	}
	vkCtx.physicalDevice = devices[ chosenDevice ];
	vkCtx.graphicsQueueFamily = chosenQueueFamily;
	vkCtx.graphicsTimestampValidBits = chosenTimestampValidBits;
	vkGetPhysicalDeviceProperties( vkCtx.physicalDevice, &vkCtx.deviceProperties );
	common->Printf( "Vulkan: device %d '%s' (queue family %u, timestampValidBits=%u, timestampPeriod=%.6fns)\n",
			chosenDevice, vkCtx.deviceProperties.deviceName, chosenQueueFamily,
			chosenTimestampValidBits, vkCtx.deviceProperties.limits.timestampPeriod );

	// Hard API floor. The back end calls ~135 core-1.3 entry points
	// (vkCmdBeginRendering, vkCmdPipelineBarrier2, vkQueueSubmit2 and the
	// extended-dynamic-state setters); on a device below the floor volk leaves
	// them NULL and the first frame faults. Fail here with a diagnosable
	// message instead, so the loader's fail-closed ladder drops back to GL.
	// MoltenVK advertises 1.3 from 1.3.0 onward (1.4 by default today, but the
	// advertised version is user-tunable downward, so this is a >= test).
	if ( vkCtx.deviceProperties.apiVersion < VK_API_VERSION_1_3 ) {
		common->Warning( "Vulkan: device '%s' reports API %u.%u.%u; this renderer requires Vulkan 1.3 "
				"(dynamic rendering, synchronization2, extended dynamic state)",
				vkCtx.deviceProperties.deviceName,
				VK_API_VERSION_MAJOR( vkCtx.deviceProperties.apiVersion ),
				VK_API_VERSION_MINOR( vkCtx.deviceProperties.apiVersion ),
				VK_API_VERSION_PATCH( vkCtx.deviceProperties.apiVersion ) );
		VK_Device_Shutdown();
		return false;
	}

	// The shadowed-interaction pipeline layout binds 8 descriptor sets, which is
	// exactly the ceiling on Metal-backed implementations. Report it here rather
	// than failing opaquely inside vkCreatePipelineLayout much later.
	if ( vkCtx.deviceProperties.limits.maxBoundDescriptorSets < VK_REQUIRED_BOUND_DESCRIPTOR_SETS ) {
		common->Warning( "Vulkan: device '%s' supports only %u bound descriptor sets; this renderer needs %d",
				vkCtx.deviceProperties.deviceName,
				vkCtx.deviceProperties.limits.maxBoundDescriptorSets,
				VK_REQUIRED_BOUND_DESCRIPTOR_SETS );
		VK_Device_Shutdown();
		return false;
	}

	// light cookies are the only packed-16-bit users; widen them when the
	// implementation has no R5G6B5 (Metal exposes it on Apple GPUs only)
	{
		VkFormatProperties packedProps;
		memset( &packedProps, 0, sizeof( packedProps ) );
		vkGetPhysicalDeviceFormatProperties( vkCtx.physicalDevice,
				VK_FORMAT_R5G6B5_UNORM_PACK16, &packedProps );
		const VkFormatFeatureFlags needed =
				VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
				| VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
				| VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
		vkCtx.packed565Supported = ( packedProps.optimalTilingFeatures & needed ) == needed;
		if ( !vkCtx.packed565Supported ) {
			common->Printf( "Vulkan: R5G6B5 unsupported; light cookies expand to RGBA8\n" );
		}
	}

	VK_Device_SelectShadowDepthFormat();

	// logical device: swapchain + the VK 1.3 dynamic-rendering/sync2 floor
	const float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo qci;
	memset( &qci, 0, sizeof( qci ) );
	qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	qci.queueFamilyIndex = vkCtx.graphicsQueueFamily;
	qci.queueCount = 1;
	qci.pQueuePriorities = &queuePriority;

	// Portability implementations (MoltenVK) must have VK_KHR_portability_subset
	// enabled when they advertise it, and their optional subset features read
	// back through the chained feature struct. Native drivers advertise neither,
	// and every portability capability then stays at its permissive default.
	const char *deviceExtensions[ 2 ];
	uint32_t deviceExtensionCount = 0;
	deviceExtensions[ deviceExtensionCount++ ] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

	vkCtx.portabilitySubset = VK_Device_DeviceExtensionSupported(
			vkCtx.physicalDevice, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME );
	vkCtx.portabilityImageViewFormatSwizzle = true;
	vkCtx.portabilityMutableComparisonSamplers = true;

	VkPhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures;
	memset( &portabilityFeatures, 0, sizeof( portabilityFeatures ) );
	portabilityFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
	if ( vkCtx.portabilitySubset ) {
		deviceExtensions[ deviceExtensionCount++ ] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;

		VkPhysicalDeviceFeatures2 portabilityQuery;
		memset( &portabilityQuery, 0, sizeof( portabilityQuery ) );
		portabilityQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		portabilityQuery.pNext = &portabilityFeatures;
		vkGetPhysicalDeviceFeatures2( vkCtx.physicalDevice, &portabilityQuery );

		vkCtx.portabilityImageViewFormatSwizzle =
				portabilityFeatures.imageViewFormatSwizzle == VK_TRUE;
		vkCtx.portabilityMutableComparisonSamplers =
				portabilityFeatures.mutableComparisonSamplers == VK_TRUE;
		common->Printf( "Vulkan: portability subset active (imageViewFormatSwizzle=%d mutableComparisonSamplers=%d)\n",
				vkCtx.portabilityImageViewFormatSwizzle ? 1 : 0,
				vkCtx.portabilityMutableComparisonSamplers ? 1 : 0 );
	}

	VkPhysicalDeviceVulkan13Features features13;
	memset( &features13, 0, sizeof( features13 ) );
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;
	if ( vkCtx.portabilitySubset ) {
		// the subset feature struct must stay chained on device creation so the
		// implementation sees which optional behaviors the app relies on
		portabilityFeatures.pNext = &features13;
	}

	// Base features: anisotropic filtering for the sampler cache, depth clamp
	// for point-shadow casters, and depth bounds for stencil-volume fill
	// reduction. Every optional feature remains disabled when unsupported.
	VkPhysicalDeviceFeatures supported;
	vkGetPhysicalDeviceFeatures( vkCtx.physicalDevice, &supported );
	VkPhysicalDeviceFeatures2 features2;
	memset( &features2, 0, sizeof( features2 ) );
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	// With VK_KHR_portability_subset enabled, an omitted subset feature struct
	// leaves every optional portability behavior DISABLED, so the queried struct
	// (which already carries what the implementation supports) is chained ahead
	// of the 1.3 features to enable them back.
	features2.pNext = vkCtx.portabilitySubset ? (void *)&portabilityFeatures : (void *)&features13;
	features2.features.samplerAnisotropy = supported.samplerAnisotropy;
	features2.features.depthClamp = supported.depthClamp;
	features2.features.depthBounds = supported.depthBounds;
	// Stock Quake 4 ships BC1/BC3/BC7 art and R_BinaryImageHeaderSupportedByRenderer
	// rejects generated .bimage files when the renderer denies compression, so the
	// feature is enabled whenever the device has it and reported honestly downstream.
	features2.features.textureCompressionBC = supported.textureCompressionBC;
	vkCtx.depthClampSupported = supported.depthClamp == VK_TRUE;
	vkCtx.depthBoundsSupported = supported.depthBounds == VK_TRUE;
	vkCtx.textureCompressionBCSupported = supported.textureCompressionBC == VK_TRUE;
	common->Printf( "Vulkan: optional depth features clamp=%d bounds=%d, BC texture compression=%d\n",
			vkCtx.depthClampSupported ? 1 : 0,
			vkCtx.depthBoundsSupported ? 1 : 0,
			vkCtx.textureCompressionBCSupported ? 1 : 0 );

	VkDeviceCreateInfo dci;
	memset( &dci, 0, sizeof( dci ) );
	dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dci.pNext = &features2;
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	dci.enabledExtensionCount = deviceExtensionCount;
	dci.ppEnabledExtensionNames = deviceExtensions;

	res = vkCreateDevice( vkCtx.physicalDevice, &dci, NULL, &vkCtx.device );
	if ( res != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkCreateDevice failed (%d)", (int)res );
		VK_Device_Shutdown();
		return false;
	}
	volkLoadDevice( vkCtx.device );
	vkGetDeviceQueue( vkCtx.device, vkCtx.graphicsQueueFamily, 0, &vkCtx.graphicsQueue );

	// command pool + per-slot sync
	VkCommandPoolCreateInfo cpci;
	memset( &cpci, 0, sizeof( cpci ) );
	cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cpci.queueFamilyIndex = vkCtx.graphicsQueueFamily;
	if ( vkCreateCommandPool( vkCtx.device, &cpci, NULL, &vkCtx.commandPool ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: command pool creation failed" );
		VK_Device_Shutdown();
		return false;
	}

	VkCommandBufferAllocateInfo cbai;
	memset( &cbai, 0, sizeof( cbai ) );
	cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cbai.commandPool = vkCtx.commandPool;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbai.commandBufferCount = VK_FRAMES_IN_FLIGHT;
	if ( vkAllocateCommandBuffers( vkCtx.device, &cbai, vkCtx.commandBuffers ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: command buffer allocation failed" );
		VK_Device_Shutdown();
		return false;
	}

	// VMA allocator over the volk-loaded entry points
	{
		VmaVulkanFunctions vmaFunctions;
		memset( &vmaFunctions, 0, sizeof( vmaFunctions ) );
		vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo aci;
		memset( &aci, 0, sizeof( aci ) );
		aci.physicalDevice = vkCtx.physicalDevice;
		aci.device = vkCtx.device;
		aci.instance = vkCtx.instance;
		aci.pVulkanFunctions = &vmaFunctions;
		aci.vulkanApiVersion = VK_API_VERSION_1_3;
		if ( vmaCreateAllocator( &aci, &vkCtx.allocator ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: VMA allocator creation failed" );
			VK_Device_Shutdown();
			return false;
		}
	}

	// dedicated synchronous upload path
	{
		VkCommandBufferAllocateInfo ucbai;
		memset( &ucbai, 0, sizeof( ucbai ) );
		ucbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		ucbai.commandPool = vkCtx.commandPool;
		ucbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		ucbai.commandBufferCount = 1;
		VkFenceCreateInfo ufci;
		memset( &ufci, 0, sizeof( ufci ) );
		ufci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		if ( vkAllocateCommandBuffers( vkCtx.device, &ucbai, &vkCtx.uploadCommandBuffer ) != VK_SUCCESS
				|| vkCreateFence( vkCtx.device, &ufci, NULL, &vkCtx.uploadFence ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: upload path creation failed" );
			VK_Device_Shutdown();
			return false;
		}
	}

	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		VkSemaphoreCreateInfo semci;
		memset( &semci, 0, sizeof( semci ) );
		semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkFenceCreateInfo fci;
		memset( &fci, 0, sizeof( fci ) );
		fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		if ( vkCreateSemaphore( vkCtx.device, &semci, NULL, &vkCtx.acquireSemaphores[ i ] ) != VK_SUCCESS
				|| vkCreateFence( vkCtx.device, &fci, NULL, &vkCtx.frameFences[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: frame sync object creation failed" );
			VK_Device_Shutdown();
			return false;
		}
	}

	// not fatal if it fails: pipeline creation accepts VK_NULL_HANDLE
	VK_Device_CreatePipelineCache();

	if ( !VK_Device_CreateSwapchain() ) {
		VK_Device_Shutdown();
		return false;
	}

	vkCtx.initialized = true;
	presentation.Succeeded();
	return true;
}

/*
====================
VK_Device_Shutdown
====================
*/
void VK_Device_Shutdown( void ) {
	R_DisplayPresentationShutdown();
	if ( vkCtx.device != VK_NULL_HANDLE ) {
		vkDeviceWaitIdle( vkCtx.device );
	}
	if ( vkCtx.pipelineCache != VK_NULL_HANDLE ) {
		VK_Device_SavePipelineCache();
		vkDestroyPipelineCache( vkCtx.device, vkCtx.pipelineCache, NULL );
		vkCtx.pipelineCache = VK_NULL_HANDLE;
	}
	if ( vkCtx.device != VK_NULL_HANDLE && vkCtx.allocator != NULL ) {
		// an open batch is abandoned, never submitted: the images it records
		// into are torn down below, and the command buffer dies with the pool
		for ( int i = 0; i < vkCtx.numUploadBatchPending; i++ ) {
			vmaDestroyBuffer( vkCtx.allocator, vkCtx.uploadBatchPendingBuffers[ i ],
					vkCtx.uploadBatchPendingAllocations[ i ] );
		}
		vkCtx.numUploadBatchPending = 0;
		vkCtx.uploadBatchPendingBytes = 0;
		vkCtx.uploadBatchOpen = false;
		// the wait-idle above already retired any submitted batch
		for ( int i = 0; i < vkCtx.numUploadBatchInFlight; i++ ) {
			vmaDestroyBuffer( vkCtx.allocator, vkCtx.uploadBatchInFlightBuffers[ i ],
					vkCtx.uploadBatchInFlightAllocations[ i ] );
		}
		vkCtx.numUploadBatchInFlight = 0;
		vkCtx.uploadBatchInFlight = false;
	}
	if ( vkCtx.device != VK_NULL_HANDLE ) {
		// release the D-layer consumers first (images, executor, buffers)
		{
			extern void VK_Image_ShutdownAll( void );
			extern void VK_GuiExecutor_Shutdown( void );
			extern void VK_VertexCache_Shutdown( void );
			VK_GuiExecutor_Shutdown();
			VK_Image_ShutdownAll();
			VK_VertexCache_Shutdown();
		}
		for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
			VK_Device_FlushDeferredDestroys( i );
		}
		VK_Device_DestroySwapchainObjects();
		for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
			if ( vkCtx.acquireSemaphores[ i ] != VK_NULL_HANDLE ) {
				vkDestroySemaphore( vkCtx.device, vkCtx.acquireSemaphores[ i ], NULL );
			}
			if ( vkCtx.frameFences[ i ] != VK_NULL_HANDLE ) {
				vkDestroyFence( vkCtx.device, vkCtx.frameFences[ i ], NULL );
			}
		}
		if ( vkCtx.uploadFence != VK_NULL_HANDLE ) {
			vkDestroyFence( vkCtx.device, vkCtx.uploadFence, NULL );
		}
		if ( vkCtx.allocator != NULL ) {
			vmaDestroyAllocator( vkCtx.allocator );
		}
		if ( vkCtx.commandPool != VK_NULL_HANDLE ) {
			vkDestroyCommandPool( vkCtx.device, vkCtx.commandPool, NULL );
		}
		vkDestroyDevice( vkCtx.device, NULL );
	}
	if ( vkCtx.instance != VK_NULL_HANDLE ) {
		if ( vkCtx.surface != VK_NULL_HANDLE ) {
			vkDestroySurfaceKHR( vkCtx.instance, vkCtx.surface, NULL );
		}
		if ( vkCtx.debugMessenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT != NULL ) {
			vkDestroyDebugUtilsMessengerEXT( vkCtx.instance, vkCtx.debugMessenger, NULL );
		}
		vkDestroyInstance( vkCtx.instance, NULL );
	}
	memset( &vkCtx, 0, sizeof( vkCtx ) );
}

/*
====================
VK_Device_PresentClearFrame
====================
*/
void VK_Device_PresentClearFrame( const float clearColor[ 4 ] ) {
	if ( !vkCtx.initialized || vkCtx.presentationBlocked ) {
		return;
	}

	// swap-interval changes require a swapchain rebuild
	if ( VK_Device_RequestedSwapInterval() != vkCtx.swapInterval ) {
		if ( !VK_Device_RecreateSwapchain() ) {
			return;
		}
	}

	const int slot = vkCtx.frameSlot;
	vkCtx.frameSlot = ( vkCtx.frameSlot + 1 ) % VK_FRAMES_IN_FLIGHT;
	vkCtx.recordingSlot = slot;

	const VkResult waited = vkWaitForFences( vkCtx.device, 1, &vkCtx.frameFences[ slot ], VK_TRUE, UINT64_MAX );
	if ( waited != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_WAIT_FAILED, waited, "clear frame fence wait" ); return; }
	// deferred destroys must never run while a submitted upload batch could
	// still reference their images
	VK_Device_WaitUploadBatch();
	if ( vkCtx.presentationBlocked ) return;
	VK_Device_FlushDeferredDestroys( slot );

	uint32_t imageIndex = 0;
	VkResult res = vkAcquireNextImageKHR( vkCtx.device, vkCtx.swapchain, UINT64_MAX,
			vkCtx.acquireSemaphores[ slot ], VK_NULL_HANDLE, &imageIndex );
	if ( res == VK_ERROR_OUT_OF_DATE_KHR ) {
		if ( !VK_Device_RecreateSwapchain() ) {
			return;
		}
		res = vkAcquireNextImageKHR( vkCtx.device, vkCtx.swapchain, UINT64_MAX,
				vkCtx.acquireSemaphores[ slot ], VK_NULL_HANDLE, &imageIndex );
	}
	if ( res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR ) {
		if ( res == VK_ERROR_DEVICE_LOST ) VK_Device_BlockPresentation( RDP_ACQUIRE_FAILED, res, "clear frame acquire" );
		else R_DisplayPresentationFailed( RDP_ACQUIRE_FAILED, (int32_t)res );
		return;
	}

	VkCommandBuffer cmd = vkCtx.commandBuffers[ slot ];
	res = vkResetCommandBuffer( cmd, 0 );
	if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_RECORD_FAILED, res, "clear command reset" ); return; }

	VkCommandBufferBeginInfo cbbi;
	memset( &cbbi, 0, sizeof( cbbi ) );
	cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	res = vkBeginCommandBuffer( cmd, &cbbi );
	if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_RECORD_FAILED, res, "clear command begin" ); return; }
	res = vkResetFences( vkCtx.device, 1, &vkCtx.frameFences[ slot ] );
	if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_WAIT_FAILED, res, "clear fence reset" ); return; }

	// UNDEFINED -> COLOR_ATTACHMENT
	VkImageMemoryBarrier2 toColor;
	memset( &toColor, 0, sizeof( toColor ) );
	toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toColor.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	toColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	toColor.image = vkCtx.swapchainImages[ imageIndex ];
	toColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toColor.subresourceRange.levelCount = 1;
	toColor.subresourceRange.layerCount = 1;

	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &toColor;
	vkCmdPipelineBarrier2( cmd, &dep );

	// dynamic-rendering clear pass
	VkRenderingAttachmentInfo color;
	memset( &color, 0, sizeof( color ) );
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = vkCtx.swapchainViews[ imageIndex ];
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.clearValue.color.float32[ 0 ] = clearColor[ 0 ];
	color.clearValue.color.float32[ 1 ] = clearColor[ 1 ];
	color.clearValue.color.float32[ 2 ] = clearColor[ 2 ];
	color.clearValue.color.float32[ 3 ] = clearColor[ 3 ];

	VkRenderingInfo ri;
	memset( &ri, 0, sizeof( ri ) );
	ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	ri.renderArea.extent = vkCtx.swapchainExtent;
	ri.layerCount = 1;
	ri.colorAttachmentCount = 1;
	ri.pColorAttachments = &color;

	vkCmdBeginRendering( cmd, &ri );
	vkCmdEndRendering( cmd );

	// COLOR_ATTACHMENT -> PRESENT
	VkImageMemoryBarrier2 toPresent;
	memset( &toPresent, 0, sizeof( toPresent ) );
	toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	toPresent.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
	toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	toPresent.image = vkCtx.swapchainImages[ imageIndex ];
	toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toPresent.subresourceRange.levelCount = 1;
	toPresent.subresourceRange.layerCount = 1;
	dep.pImageMemoryBarriers = &toPresent;
	vkCmdPipelineBarrier2( cmd, &dep );

	res = vkEndCommandBuffer( cmd );
	if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_RECORD_FAILED, res, "clear command end" ); return; }

	VkSemaphoreSubmitInfo waitInfo;
	memset( &waitInfo, 0, sizeof( waitInfo ) );
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitInfo.semaphore = vkCtx.acquireSemaphores[ slot ];
	waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSemaphoreSubmitInfo signalInfo;
	memset( &signalInfo, 0, sizeof( signalInfo ) );
	signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalInfo.semaphore = vkCtx.renderFinishedSemaphores[ imageIndex ];
	signalInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkCommandBufferSubmitInfo cmdInfo;
	memset( &cmdInfo, 0, sizeof( cmdInfo ) );
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.commandBuffer = cmd;

	VkSubmitInfo2 si;
	memset( &si, 0, sizeof( si ) );
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	si.waitSemaphoreInfoCount = 1;
	si.pWaitSemaphoreInfos = &waitInfo;
	si.commandBufferInfoCount = 1;
	si.pCommandBufferInfos = &cmdInfo;
	si.signalSemaphoreInfoCount = 1;
	si.pSignalSemaphoreInfos = &signalInfo;

	// the upload batch must precede any frame submission in queue order so
	// this frame's fence covers it
	VK_Device_FlushUploadBatch();
	if ( vkCtx.presentationBlocked ) return;
	res = vkQueueSubmit2( vkCtx.graphicsQueue, 1, &si, vkCtx.frameFences[ slot ] );
	if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_SUBMIT_FAILED, res, "clear frame submit" ); return; }
	R_DisplayPresentationSubmitted();

	VkPresentInfoKHR pi;
	memset( &pi, 0, sizeof( pi ) );
	pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores = &vkCtx.renderFinishedSemaphores[ imageIndex ];
	pi.swapchainCount = 1;
	pi.pSwapchains = &vkCtx.swapchain;
	pi.pImageIndices = &imageIndex;

	res = vkQueuePresentKHR( vkCtx.graphicsQueue, &pi );
	if ( res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR ) R_DisplayPresentationPresented();
	else if ( res == VK_ERROR_OUT_OF_DATE_KHR ) R_DisplayPresentationFailed( RDP_PRESENT_FAILED, (int32_t)res );
	else { VK_Device_BlockPresentation( RDP_PRESENT_FAILED, res, "clear frame present" ); return; }
	if ( res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR ) {
		if ( !VK_Device_RecreateSwapchain() ) common->Warning( "Vulkan: swapchain recreation after clear present failed" );
	}
}

/*
====================
VK_Device_WaitUploadBatch / VK_Device_FlushUploadBatch / VK_Device_BatchedUpload
====================
*/

// staging bytes a single upload batch may own before it is force-flushed
static const VkDeviceSize VK_UPLOAD_BATCH_BYTE_BUDGET = 64u << 20;

void VK_Device_WaitUploadBatch( void ) {
	if ( !vkCtx.uploadBatchInFlight || vkCtx.presentationBlocked ) {
		return;
	}
	VkResult res = vkWaitForFences( vkCtx.device, 1, &vkCtx.uploadFence, VK_TRUE, UINT64_MAX );
	if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_WAIT_FAILED, res, "upload fence wait" ); return; }
	res = vkResetFences( vkCtx.device, 1, &vkCtx.uploadFence );
	if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_WAIT_FAILED, res, "upload fence reset" ); return; }
	for ( int i = 0; i < vkCtx.numUploadBatchInFlight; i++ ) {
		vmaDestroyBuffer( vkCtx.allocator, vkCtx.uploadBatchInFlightBuffers[ i ],
				vkCtx.uploadBatchInFlightAllocations[ i ] );
	}
	vkCtx.numUploadBatchInFlight = 0;
	vkCtx.uploadBatchInFlight = false;
}

void VK_Device_FlushUploadBatch( void ) {
	if ( !vkCtx.uploadBatchOpen || vkCtx.presentationBlocked ) {
		return;
	}
	VkResult res = vkEndCommandBuffer( vkCtx.uploadCommandBuffer );
	vkCtx.uploadBatchOpen = false;
	if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_RECORD_FAILED, res, "upload command end" ); return; }

	VkSubmitInfo si;
	memset( &si, 0, sizeof( si ) );
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &vkCtx.uploadCommandBuffer;
	res = vkQueueSubmit( vkCtx.graphicsQueue, 1, &si, vkCtx.uploadFence );
	if ( res != VK_SUCCESS ) {
		// Device loss does not prove no work was enqueued. Keep staging owned
		// until full shutdown has retired the device instead of freeing it here.
		VK_Device_BlockPresentation( RDP_SUBMIT_FAILED, res, "upload batch submit" );
		return;
	}
	// opening a batch waits out the previous one first, so the in-flight list
	// is always empty here
	for ( int i = 0; i < vkCtx.numUploadBatchPending; i++ ) {
		vkCtx.uploadBatchInFlightBuffers[ i ] = vkCtx.uploadBatchPendingBuffers[ i ];
		vkCtx.uploadBatchInFlightAllocations[ i ] = vkCtx.uploadBatchPendingAllocations[ i ];
	}
	vkCtx.numUploadBatchInFlight = vkCtx.numUploadBatchPending;
	vkCtx.numUploadBatchPending = 0;
	vkCtx.uploadBatchPendingBytes = 0;
	vkCtx.uploadBatchInFlight = true;
}

bool VK_Device_BatchedUpload( vkImmediateRecord_t record, void *user,
		VkBuffer staging, VmaAllocation stagingAllocation, VkDeviceSize stagingBytes ) {
	if ( vkCtx.device == VK_NULL_HANDLE || vkCtx.uploadCommandBuffer == VK_NULL_HANDLE || vkCtx.presentationBlocked ) {
		return false;
	}

	if ( !vkCtx.uploadBatchOpen ) {
		VK_Device_WaitUploadBatch();
		if ( vkCtx.presentationBlocked ) return false;
		VkResult res = vkResetCommandBuffer( vkCtx.uploadCommandBuffer, 0 );
		if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_RECORD_FAILED, res, "upload command reset" ); return false; }
		VkCommandBufferBeginInfo cbbi;
		memset( &cbbi, 0, sizeof( cbbi ) );
		cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		res = vkBeginCommandBuffer( vkCtx.uploadCommandBuffer, &cbbi );
		if ( res != VK_SUCCESS ) {
			VK_Device_BlockPresentation( RDP_RECORD_FAILED, res, "upload command begin" );
			return false;
		}
		vkCtx.uploadBatchOpen = true;
	}

	record( vkCtx.uploadCommandBuffer, user );

	if ( staging != VK_NULL_HANDLE ) {
		vkCtx.uploadBatchPendingBuffers[ vkCtx.numUploadBatchPending ] = staging;
		vkCtx.uploadBatchPendingAllocations[ vkCtx.numUploadBatchPending ] = stagingAllocation;
		vkCtx.numUploadBatchPending++;
		vkCtx.uploadBatchPendingBytes += stagingBytes;
	}
	if ( vkCtx.numUploadBatchPending >= VK_MAX_UPLOAD_BATCH_STAGING
			|| vkCtx.uploadBatchPendingBytes >= VK_UPLOAD_BATCH_BYTE_BUDGET ) {
		VK_Device_FlushUploadBatch();
	}
	return true;
}

/*
====================
VK_Device_DeferDestroy / VK_Device_FlushDeferredDestroys
====================
*/
void VK_Device_DeferDestroy( VkImage image, VkImageView view, VkBuffer buffer, VmaAllocation allocation,
		VkImageView secondaryView ) {
	if ( image == VK_NULL_HANDLE && view == VK_NULL_HANDLE && secondaryView == VK_NULL_HANDLE
			&& buffer == VK_NULL_HANDLE && allocation == NULL ) {
		return;
	}
	const int slot = vkCtx.recordingSlot;
	if ( vkCtx.numDeferredDestroys[ slot ] >= VK_MAX_DEFERRED_DESTROYS ) {
		// queue full: block for safety rather than leak or free early. An open
		// unsubmitted upload batch must be submitted first: destroying a
		// resource its recorded commands reference would invalidate the
		// command buffer, and wait-idle only retires submitted work.
		VK_Device_FlushUploadBatch();
		VK_Device_WaitUploadBatch();
		vkDeviceWaitIdle( vkCtx.device );
		for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
			VK_Device_FlushDeferredDestroys( i );
		}
	}
	vkDeferredDestroy_t &entry = vkCtx.deferredDestroys[ slot ][ vkCtx.numDeferredDestroys[ slot ]++ ];
	entry.image = image;
	entry.view = view;
	entry.secondaryView = secondaryView;
	entry.buffer = buffer;
	entry.allocation = allocation;
}

void VK_Device_FlushDeferredDestroys( int slot ) {
	for ( int i = 0; i < vkCtx.numDeferredDestroys[ slot ]; i++ ) {
		vkDeferredDestroy_t &entry = vkCtx.deferredDestroys[ slot ][ i ];
		if ( entry.view != VK_NULL_HANDLE ) {
			vkDestroyImageView( vkCtx.device, entry.view, NULL );
		}
		if ( entry.secondaryView != VK_NULL_HANDLE && entry.secondaryView != entry.view ) {
			vkDestroyImageView( vkCtx.device, entry.secondaryView, NULL );
		}
		if ( entry.image != VK_NULL_HANDLE && vkCtx.allocator != NULL ) {
			vmaDestroyImage( vkCtx.allocator, entry.image, entry.allocation );
		} else if ( entry.buffer != VK_NULL_HANDLE && vkCtx.allocator != NULL ) {
			vmaDestroyBuffer( vkCtx.allocator, entry.buffer, entry.allocation );
		}
	}
	vkCtx.numDeferredDestroys[ slot ] = 0;
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
