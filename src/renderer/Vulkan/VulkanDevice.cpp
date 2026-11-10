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
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#if defined( MACOS_X )
#include <dlfcn.h>
#endif
#include "volk.h"
#include "vk_mem_alloc.h"

#include "VulkanDevice.h"
#include "vk_ExecutorHooks.h"
#include "../RendererMetrics.h"
#include "../RendererModule.h"
#include "../RendererDeviceCatalog.h"
#include "VulkanBringup.h"

vkDeviceContext_t vkCtx;

bool VK_Device_QueryCatalog(renderDeviceCatalog_t* catalog) {
	if (!catalog) return false;
	*catalog = {};
	// Never create another instance or repoint volk on the live renderer.
	if (!vkCtx.initialized) return VK_Bringup_QueryCatalog(catalog);
	vkDeviceSelectionApi_t api = {};
	api.enumeratePhysicalDevices = vkEnumeratePhysicalDevices;
	api.getProperties = vkGetPhysicalDeviceProperties;
	api.getFeatures2 = vkGetPhysicalDeviceFeatures2;
	api.enumerateExtensions = vkEnumerateDeviceExtensionProperties;
	api.getQueueFamilies = vkGetPhysicalDeviceQueueFamilyProperties;
	api.getMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
	api.getProperties2 = vkGetPhysicalDeviceProperties2;
	if (VK_QueryDeviceCatalog(api,vkCtx.instance,*catalog)!=VK_SUCCESS) return false;
	// The active identity belongs to the device we created, even if a later
	// driver enumeration changes order (for example after attaching an eGPU).
	idStr::Copynz(catalog->activeId,vkCtx.physicalDeviceId,sizeof(catalog->activeId));
	return true;
}

static idCVar r_vkGpuPipelineStatistics( "r_vkGpuPipelineStatistics", "0", CVAR_RENDERER | CVAR_BOOL | CVAR_INIT,
	"enable optional projected-shadow pipeline counters in r_vkGpuPhaseProfile; set before renderer startup" );

// Keep overflow outside the memset-reset device context. Resources referenced
// by the command buffer still being recorded cannot be retired by wait-idle.
static idList<vkDeferredDestroy_t> vkDeferredDestroyOverflow[ VK_FRAMES_IN_FLIGHT ];

static const renderWindowServices_t *vkWindowServices = NULL;

void VK_Device_BlockPresentation( renderDisplayOutcome_t outcome, VkResult error, const char *operation ) {
	if ( vkCtx.presentationBlocked ) return;
	vkCtx.presentationBlocked = true;
	R_DisplayPresentationFailed( outcome, static_cast<int32_t>( error ) );
	R_DisplayPresentationShutdown();
	common->Warning( "Vulkan: %s failed (%d); presentation requires a full device restart", operation, (int)error );
}

// A latched device (device loss, or a submit/present/record failure that
// leaves semaphores and queue state unknown) cannot draw another frame. The
// full renderer restart vid_restart performs releases the whole device and
// builds a new one, so it is queued automatically. A device that keeps
// failing ends in the same next-launch OpenGL recovery a failed restart uses,
// instead of a frozen window. The budget is per process: the module, and
// this counter, survive renderer restarts.
static idCVar r_vkPresentationRecoveries( "r_vkPresentationRecoveries", "3", CVAR_RENDERER | CVAR_INTEGER,
	"automatic renderer restarts allowed per session when Vulkan presentation fails (device loss); 0 disables", 0, 16 );
static idCVar r_vkPresentationFailureTest( "r_vkPresentationFailureTest", "0", CVAR_RENDERER | CVAR_INTEGER,
	"diagnostic: 1 latches the next frame as a lost device, 2 reports the surface lost, 3 withholds every swapchain "
	"image until set back to 0 (as a display that is off does), to exercise Vulkan recovery", 0, 3 );
static idCVar r_vkHiddenWindowPresent( "r_vkHiddenWindowPresent", "0", CVAR_RENDERER | CVAR_BOOL,
	"diagnostic: 1 acquires and presents a hidden window's frames through its swapchain instead of rendering them "
	"offscreen; applies when the swapchain is next created (vid_restart)" );
static int vkPresentationRecoveries = 0;

/*
===============================================================================

	Presented-frame timing (HF-07/HF-08, r_vkPresentTiming)

	With VK_KHR_present_id and VK_KHR_present_wait every present carries an id,
	and a waiter thread blocks in vkWaitForPresentKHR until the presentation
	engine reports that id presented, then stamps the time. That is the cadence
	frames reach the display, and how long each waited after vkQueuePresentKHR
	returned, rather than when the engine drew them; it is what the frame-pacing
	budget is really about. Off by default (a diagnostic for reference runs).

	The waiter never touches a retired or destroyed swapchain: every swapchain
	transition first quiesces it (dropping pending ids and waiting out its
	current, short wait) and resumes it on the new swapchain. A wait that
	completes after a later id was presented (a replaced mailbox image) shares
	that later present's time and counts as replaced.

===============================================================================
*/

static idCVar r_vkPresentTiming( "r_vkPresentTiming", "0", CVAR_RENDERER | CVAR_BOOL,
	"diagnostic: measure when each frame reaches the display (VK_KHR_present_wait) for rendererPresentTiming; "
	"applies when the Vulkan device starts (vid_restart)" );

static const int VK_PRESENT_TIMING_PENDING = 64;
static const int VK_PRESENT_TIMING_HISTORY = 4096;
// one bounded wait; a swapchain transition waits at most this long for the waiter
static const uint64_t VK_PRESENT_TIMING_WAIT_NS = 4000000ull;

typedef struct vkPresentTimingPending_s {
	uint64_t	id;
	double		submittedMsec;		// when vkQueuePresentKHR returned
} vkPresentTimingPending_t;

static std::mutex vkPresentTimingMutex;
static std::condition_variable vkPresentTimingWake;
static std::thread vkPresentTimingThread;
static bool vkPresentTimingRunning = false;		// the device enabled both extensions
static bool vkPresentTimingStop = false;
static bool vkPresentTimingPaused = true;		// no swapchain the waiter may use
static bool vkPresentTimingWaiting = false;		// the waiter is inside vkWaitForPresentKHR
static VkDevice vkPresentTimingDevice = VK_NULL_HANDLE;
static VkSwapchainKHR vkPresentTimingSwapchain = VK_NULL_HANDLE;
static uint64_t vkPresentTimingNextId = 1;
static vkPresentTimingPending_t vkPresentTimingPending[ VK_PRESENT_TIMING_PENDING ];
static int vkPresentTimingPendingFirst = 0;
static int vkPresentTimingPendingCount = 0;
// results since rendererPresentTiming reset: presented-to-presented interval
// and submitted-to-presented wait, in ms
static float vkPresentTimingInterval[ VK_PRESENT_TIMING_HISTORY ];
static float vkPresentTimingQueueWait[ VK_PRESENT_TIMING_HISTORY ];
static int vkPresentTimingCount = 0;
static int vkPresentTimingNext = 0;
static int vkPresentTimingReplaced = 0;
static int vkPresentTimingDropped = 0;
static double vkPresentTimingLastPresentedMsec = -1.0;

static double VK_PresentTiming_NowMsec( void ) {
	return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

// called with the mutex held
static void VK_PresentTiming_DropPending( void ) {
	vkPresentTimingDropped += vkPresentTimingPendingCount;
	vkPresentTimingPendingFirst = 0;
	vkPresentTimingPendingCount = 0;
	vkPresentTimingLastPresentedMsec = -1.0;
}

static void VK_PresentTiming_Waiter( void ) {
	std::unique_lock<std::mutex> lock( vkPresentTimingMutex );
	while ( true ) {
		vkPresentTimingWake.wait( lock, [] {
			return vkPresentTimingStop || ( !vkPresentTimingPaused && vkPresentTimingPendingCount > 0 );
		} );
		if ( vkPresentTimingStop ) {
			return;
		}
		const vkPresentTimingPending_t pending = vkPresentTimingPending[ vkPresentTimingPendingFirst ];
		const VkDevice device = vkPresentTimingDevice;
		const VkSwapchainKHR swapchain = vkPresentTimingSwapchain;
		vkPresentTimingWaiting = true;
		lock.unlock();
		const VkResult result = vkWaitForPresentKHR( device, swapchain, pending.id, VK_PRESENT_TIMING_WAIT_NS );
		const double presentedMsec = VK_PresentTiming_NowMsec();
		lock.lock();
		vkPresentTimingWaiting = false;
		vkPresentTimingWake.notify_all();
		// a transition dropped this id while the wait ran; nothing to record
		if ( vkPresentTimingPaused || vkPresentTimingPendingCount == 0
				|| vkPresentTimingPending[ vkPresentTimingPendingFirst ].id != pending.id ) {
			continue;
		}
		if ( result == VK_TIMEOUT ) {
			continue;
		}
		vkPresentTimingPendingFirst = ( vkPresentTimingPendingFirst + 1 ) % VK_PRESENT_TIMING_PENDING;
		--vkPresentTimingPendingCount;
		if ( result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR ) {
			// out of date, surface or device lost: the frames will not be presented
			++vkPresentTimingDropped;
			VK_PresentTiming_DropPending();
			continue;
		}
		const double interval = vkPresentTimingLastPresentedMsec >= 0.0 ? presentedMsec - vkPresentTimingLastPresentedMsec : -1.0;
		vkPresentTimingLastPresentedMsec = presentedMsec;
		if ( interval < 0.0 ) {
			continue;
		}
		if ( interval < 0.05 ) {
			++vkPresentTimingReplaced;
		}
		vkPresentTimingInterval[ vkPresentTimingNext ] = static_cast<float>( interval );
		vkPresentTimingQueueWait[ vkPresentTimingNext ] = static_cast<float>( Max( 0.0, presentedMsec - pending.submittedMsec ) );
		vkPresentTimingNext = ( vkPresentTimingNext + 1 ) % VK_PRESENT_TIMING_HISTORY;
		vkPresentTimingCount = Min( vkPresentTimingCount + 1, VK_PRESENT_TIMING_HISTORY );
	}
}

static void VK_PresentTiming_Start( VkDevice device ) {
	std::lock_guard<std::mutex> lock( vkPresentTimingMutex );
	vkPresentTimingDevice = device;
	vkPresentTimingSwapchain = VK_NULL_HANDLE;
	vkPresentTimingStop = false;
	vkPresentTimingPaused = true;
	vkPresentTimingWaiting = false;
	vkPresentTimingNextId = 1;
	vkPresentTimingPendingFirst = 0;
	vkPresentTimingPendingCount = 0;
	vkPresentTimingLastPresentedMsec = -1.0;
	vkPresentTimingThread = std::thread( VK_PresentTiming_Waiter );
	vkPresentTimingRunning = true;
}

static void VK_PresentTiming_Stop( void ) {
	{
		std::lock_guard<std::mutex> lock( vkPresentTimingMutex );
		if ( !vkPresentTimingRunning ) {
			return;
		}
		vkPresentTimingStop = true;
		vkPresentTimingRunning = false;
	}
	vkPresentTimingWake.notify_all();
	if ( vkPresentTimingThread.joinable() ) {
		vkPresentTimingThread.join();
	}
	std::lock_guard<std::mutex> lock( vkPresentTimingMutex );
	VK_PresentTiming_DropPending();
	vkPresentTimingDevice = VK_NULL_HANDLE;
	vkPresentTimingSwapchain = VK_NULL_HANDLE;
}

// Before a swapchain is retired or destroyed: the waiter lets go of it.
static void VK_PresentTiming_Quiesce( void ) {
	std::unique_lock<std::mutex> lock( vkPresentTimingMutex );
	if ( !vkPresentTimingRunning ) {
		return;
	}
	vkPresentTimingPaused = true;
	VK_PresentTiming_DropPending();
	vkPresentTimingWake.wait( lock, [] { return !vkPresentTimingWaiting; } );
	vkPresentTimingSwapchain = VK_NULL_HANDLE;
}

static void VK_PresentTiming_Resume( VkSwapchainKHR swapchain ) {
	std::lock_guard<std::mutex> lock( vkPresentTimingMutex );
	if ( !vkPresentTimingRunning ) {
		return;
	}
	vkPresentTimingSwapchain = swapchain;
	vkPresentTimingPaused = swapchain == VK_NULL_HANDLE;
}

uint64_t VK_PresentTiming_NextId( void ) {
	std::lock_guard<std::mutex> lock( vkPresentTimingMutex );
	if ( !vkPresentTimingRunning || vkPresentTimingPaused ) {
		return 0;
	}
	return vkPresentTimingNextId++;
}

void VK_PresentTiming_Presented( uint64_t id ) {
	if ( id == 0 ) {
		return;
	}
	const double submittedMsec = VK_PresentTiming_NowMsec();
	{
		std::lock_guard<std::mutex> lock( vkPresentTimingMutex );
		if ( !vkPresentTimingRunning || vkPresentTimingPaused ) {
			return;
		}
		if ( vkPresentTimingPendingCount == VK_PRESENT_TIMING_PENDING ) {
			// the waiter fell a whole ring behind; start its timeline again
			VK_PresentTiming_DropPending();
		}
		vkPresentTimingPending_t &pending =
			vkPresentTimingPending[ ( vkPresentTimingPendingFirst + vkPresentTimingPendingCount ) % VK_PRESENT_TIMING_PENDING ];
		pending.id = id;
		pending.submittedMsec = submittedMsec;
		++vkPresentTimingPendingCount;
	}
	vkPresentTimingWake.notify_all();
}

static idStr VK_PresentTiming_Percentiles( const float *values, int count ) {
	if ( count <= 0 ) {
		return "none";
	}
	idList<float> sorted;
	sorted.SetNum( count );
	for ( int i = 0; i < count; i++ ) {
		sorted[ i ] = values[ i ];
	}
	std::sort( sorted.Ptr(), sorted.Ptr() + count );
	const auto at = [&]( float fraction ) { return sorted[ Min( count - 1, static_cast<int>( fraction * ( count - 1 ) + 0.5f ) ) ]; };
	return va( "p50=%.3f p95=%.3f p99=%.3f max=%.3f ms", at( 0.5f ), at( 0.95f ), at( 0.99f ), sorted[ count - 1 ] );
}

/*
====================
R_RendererVulkanPresentTiming_f

rendererPresentTiming [reset | <reason>]: when frames reached the display since
the last reset, from VK_KHR_present_wait (r_vkPresentTiming 1).
====================
*/
void R_RendererVulkanPresentTiming_f( const idCmdArgs &args ) {
	idStr reason = args.Argc() > 1 ? args.Args( 1, -1 ) : "";
	std::lock_guard<std::mutex> lock( vkPresentTimingMutex );
	if ( reason.Icmp( "reset" ) == 0 ) {
		vkPresentTimingCount = 0;
		vkPresentTimingNext = 0;
		vkPresentTimingReplaced = 0;
		vkPresentTimingDropped = 0;
		common->Printf( "Presented frames reset\n" );
		return;
	}
	if ( !vkPresentTimingRunning ) {
		common->Printf( "Presented frames%s%s: unavailable (%s)\n", reason.Length() ? " " : "", reason.c_str(),
			r_vkPresentTiming.GetBool() ? "VK_KHR_present_wait unsupported by this device" : "r_vkPresentTiming 0" );
		return;
	}
	const int count = vkPresentTimingCount;
	common->Printf( "Presented frames%s%s: frames=%d interval %s; queue %s; replaced=%d dropped=%d\n",
		reason.Length() ? " " : "", reason.c_str(), count,
		VK_PresentTiming_Percentiles( vkPresentTimingInterval, count ).c_str(),
		VK_PresentTiming_Percentiles( vkPresentTimingQueueWait, count ).c_str(),
		vkPresentTimingReplaced, vkPresentTimingDropped );
}

void VK_Device_ServicePresentationRecovery( void ) {
	if ( !vkCtx.initialized ) {
		return;
	}
	// Drill 3 is not one-shot; VK_Device_ServiceWithheldImages serves it.
	const int drill = r_vkPresentationFailureTest.GetInteger();
	if ( ( drill == 1 || drill == 2 ) && !vkCtx.presentationBlocked ) {
		r_vkPresentationFailureTest.SetInteger( 0 );
		if ( drill == 1 ) {
			VK_Device_BlockPresentation( RDP_SUBMIT_FAILED, VK_ERROR_DEVICE_LOST, "simulated device loss" );
		} else {
			// Serviced where a real loss is: before the next acquire.
			common->Printf( "Vulkan: simulated surface loss\n" );
			vkCtx.surfaceLost = true;
		}
	}
	if ( !vkCtx.presentationBlocked || vkCtx.presentationRecoveryQueued ) {
		return;
	}
	vkCtx.presentationRecoveryQueued = true;
	const int budget = r_vkPresentationRecoveries.GetInteger();
	if ( vkPresentationRecoveries < budget ) {
		++vkPresentationRecoveries;
		common->Warning( "Vulkan: restarting the renderer to recover presentation (%d of %d this session)",
			vkPresentationRecoveries, budget );
		// Ahead of anything already queued (a running script, a quit), so
		// rendering returns before the next queued command needs it.
		cmdSystem->BufferCommandText( CMD_EXEC_INSERT, "vid_restart\n" );
		return;
	}
	if ( budget <= 0 ) {
		common->Warning( "Vulkan: automatic presentation recovery is disabled (r_vkPresentationRecoveries 0); use vid_restart" );
		return;
	}
	const bool nextLaunchUsesGL = R_RendererModule_ResetApiAfterDeviceFailure();
	common->FatalError( "Vulkan presentation failed again after %d renderer restarts; %s", vkPresentationRecoveries,
		nextLaunchUsesGL ? "r_renderApi has been reset to gl, so the next launch uses OpenGL"
			: "launch with +set r_renderApi gl to use OpenGL" );
}

extern idCVar r_vkValidation;
extern idCVar r_vkDevice;
extern idCVar r_vkSampleLocations;
static idCVar r_vkDepthClipControl( "r_vkDepthClipControl", "1", CVAR_RENDERER | CVAR_BOOL,
	"clip-space depth in OpenGL's [-1, 1] convention through VK_EXT_depth_clip_control, so window depth rounds "
	"as OpenGL's does; 0 remaps every projection's depth row instead. Applies when the device is created." );
extern idCVar r_swapInterval;

// Multisampled image attachments use GL window coordinates and a positive
// viewport height. Keep the standard sample indices in that same convention;
// barriers and depth resolves use these exact locations as well.
bool VK_Device_SampleLocations( VkSampleCountFlagBits samples, VkSampleLocationsInfoEXT &info ) {
	static const VkSampleLocationEXT locations2[] = { { .75f, .75f }, { .25f, .25f } };
	static const VkSampleLocationEXT locations4[] = {
		{ .375f, .125f }, { .875f, .375f }, { .125f, .625f }, { .625f, .875f }
	};
	static const VkSampleLocationEXT locations8[] = {
		{ .5625f, .3125f }, { .4375f, .6875f }, { .8125f, .5625f }, { .3125f, .1875f },
		{ .1875f, .8125f }, { .0625f, .4375f }, { .6875f, .9375f }, { .9375f, .0625f }
	};
	memset( &info, 0, sizeof( info ) );
	if ( ( vkCtx.sampleLocationCounts & samples ) == 0 ) { return false; }
	switch ( samples ) {
		case VK_SAMPLE_COUNT_2_BIT: info.pSampleLocations = locations2; break;
		case VK_SAMPLE_COUNT_4_BIT: info.pSampleLocations = locations4; break;
		case VK_SAMPLE_COUNT_8_BIT: info.pSampleLocations = locations8; break;
		default: return false;
	}
	info.sType = VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT;
	info.sampleLocationsPerPixel = samples;
	info.sampleLocationGridSize.width = info.sampleLocationGridSize.height = 1;
	info.sampleLocationsCount = (uint32_t)samples;
	return true;
}

static void VK_Device_RestrictSampleLocationDepthFormats( void ) {
	// Only enable counts for which every public depth format can preserve these
	// locations. Color and depth must never select different raster patterns.
	const VkFormat formats[] = { VK_FORMAT_D32_SFLOAT, vkCtx.depthFormat };
	for ( int i = 0; i < 2 && vkCtx.sampleLocationCounts != 0; ++i ) {
		VkFormatProperties properties;
		vkGetPhysicalDeviceFormatProperties( vkCtx.physicalDevice, formats[ i ], &properties );
		VkPhysicalDeviceImageFormatInfo2 query = {};
		query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
		query.format = formats[ i ];
		query.type = VK_IMAGE_TYPE_2D;
		query.tiling = VK_IMAGE_TILING_OPTIMAL;
		query.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
			| VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if ( ( properties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT ) != 0 ) {
			query.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}
		query.flags = VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT;
		VkImageFormatProperties2 result = {};
		result.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
		if ( vkGetPhysicalDeviceImageFormatProperties2( vkCtx.physicalDevice, &query, &result ) != VK_SUCCESS ) {
			vkCtx.sampleLocationCounts = 0;
		} else {
			vkCtx.sampleLocationCounts &= result.imageFormatProperties.sampleCounts;
		}
	}
	common->Printf( "Vulkan: MSAA sample locations lower-left-standard counts=0x%x native-standard=%d\n",
		(unsigned int)vkCtx.sampleLocationCounts, vkCtx.deviceProperties.limits.standardSampleLocations ? 1 : 0 );
}

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

The bring-up probe resolves its library through this function too, because the
engine runs that probe to decide whether to activate this module at all; a probe
that looked only where volk looks would find no MoltenVK inside the app bundle
and send every packaged Mac back to OpenGL.
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

bool VK_Device_InitLoader( void ) {
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
VK_Device_CreateInstance / VK_Device_CreateLogicalDevice

OpenXR on Vulkan (XR_KHR_vulkan_enable2): while VR wants this renderer, the
runtime creates the instance and the device from the renderer's create
infos, adding the extensions it needs, so the session can bind to them
(vk_VRPresentation.cpp). Otherwise the renderer creates them itself.
====================
*/
static VkResult VK_Device_CreateInstance( const VkInstanceCreateInfo &ici ) {
	void *created = NULL;
	int result = VK_SUCCESS;
	if ( R_RendererModule_CreateVulkanInstanceForVR( (void *)vkGetInstanceProcAddr, &ici, &created, &result ) ) {
		vkCtx.instance = (VkInstance)created;
		vkCtx.instanceFromVR = result == VK_SUCCESS && created != NULL;
		return (VkResult)result;
	}
	vkCtx.instanceFromVR = false;
	return vkCreateInstance( &ici, NULL, &vkCtx.instance );
}

static VkResult VK_Device_CreateLogicalDevice( const VkDeviceCreateInfo &dci, bool forVR ) {
	if ( forVR ) {
		void *created = NULL;
		int result = VK_SUCCESS;
		if ( R_RendererModule_CreateVulkanDeviceForVR( (void *)vkGetInstanceProcAddr, (void *)vkCtx.physicalDevice,
				&dci, &created, &result ) ) {
			vkCtx.device = (VkDevice)created;
			vkCtx.deviceFromVR = result == VK_SUCCESS && created != NULL;
			return (VkResult)result;
		}
	}
	vkCtx.deviceFromVR = false;
	return vkCreateDevice( vkCtx.physicalDevice, &dci, NULL, &vkCtx.device );
}

// The enumeration index of a physical device, -1 when it is not listed.
static int VK_Device_PhysicalDeviceIndex( VkPhysicalDevice physicalDevice ) {
	uint32_t count = 0;
	if ( vkEnumeratePhysicalDevices( vkCtx.instance, &count, NULL ) != VK_SUCCESS || count == 0 || count > 64 ) {
		return -1;
	}
	VkPhysicalDevice devices[64];
	if ( vkEnumeratePhysicalDevices( vkCtx.instance, &count, devices ) != VK_SUCCESS ) {
		return -1;
	}
	for ( uint32_t i = 0; i < count; i++ ) {
		if ( devices[i] == physicalDevice ) {
			return (int)i;
		}
	}
	return -1;
}

/*
====================
VK_Device_FindInstanceExtensions

Vulkan Portability negotiation. VK_KHR_portability_enumeration is a *loader*
extension: the Khronos loader advertises it and REQUIRES both the extension and
VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR before it will enumerate a
portability ICD such as MoltenVK (otherwise vkCreateInstance fails with
VK_ERROR_INCOMPATIBLE_DRIVER). A directly loaded libMoltenVK.dylib does NOT
advertise it, and requesting it there fails with VK_ERROR_EXTENSION_NOT_PRESENT.
Probing instead of hardcoding keeps one code path correct for both, and is a
no-op on native Windows/Linux drivers.

The inventory is complete or the start fails: a failed or still-changing list
must not read as "absent", which would hide a portability ICD.

VK_KHR_portability_subset, the device half, must be enabled whenever the device
advertises it (VUID-VkDeviceCreateInfo-pProperties-04451), and must NOT be
requested when it does not -- MoltenVK's advertiseExtensions configuration can
suppress it. Admission reads it from the same complete device-extension list it
checks the swapchain extension in (vkDeviceSelection_t::hasPortabilitySubset).
====================
*/
static bool VK_Device_FindInstanceExtensions( const char *layer, const char *const *names, uint32_t count, bool *found ) {
	const VkResult result = VK_FindInstanceExtensions( vkEnumerateInstanceExtensionProperties, layer, names, count, found );
	if ( result != VK_SUCCESS && layer == NULL ) {
		common->Warning( "Vulkan: instance-extension enumeration failed (%d)", (int)result );
	}
	return result == VK_SUCCESS;
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

// Swapchain images r_vkPresentationFailureTest 3 holds, one acquisition fence
// each. They return to the presentation engine with their swapchain.
static VkFence vkWithheldImageFences[ VK_MAX_SWAPCHAIN_IMAGES ];
static uint32_t vkNumWithheldImages = 0;

static void VK_Device_ReleaseWithheldImages( void ) {
	for ( uint32_t i = 0; i < vkNumWithheldImages; i++ ) {
		// Each fence signals once the engine has finished with its image.
		(void)vkWaitForFences( vkCtx.device, 1, &vkWithheldImageFences[ i ], VK_TRUE, 1000000000ull );
		vkDestroyFence( vkCtx.device, vkWithheldImageFences[ i ], NULL );
		vkWithheldImageFences[ i ] = VK_NULL_HANDLE;
	}
	vkNumWithheldImages = 0;
}

/*
====================
VK_Device_DestroySwapchainObjects
====================
*/
static void VK_Device_DestroySwapchainObjects( void ) {
	VK_PresentTiming_Quiesce();
	VK_Device_ReleaseWithheldImages();
	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( vkCtx.offscreenViews[ i ] != VK_NULL_HANDLE ) {
			vkDestroyImageView( vkCtx.device, vkCtx.offscreenViews[ i ], NULL );
			vkCtx.offscreenViews[ i ] = VK_NULL_HANDLE;
		}
		if ( vkCtx.offscreenImages[ i ] != VK_NULL_HANDLE ) {
			vmaDestroyImage( vkCtx.allocator, vkCtx.offscreenImages[ i ], vkCtx.offscreenAllocations[ i ] );
			vkCtx.offscreenImages[ i ] = VK_NULL_HANDLE;
			vkCtx.offscreenAllocations[ i ] = NULL;
		}
	}
	vkCtx.offscreenFrames = false;
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
VK_Device_CreateOffscreenImages

A hidden window's per-frame-slot color images (vkDeviceContext_t::
offscreenFrames), standing in for swapchain images with the same format,
extent and usage. One per slot, like the depth images, so a frame never writes
an image the previous frame may still be rendering.
====================
*/
static bool VK_Device_CreateOffscreenImages( void ) {
	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		VkImageCreateInfo ici;
		memset( &ici, 0, sizeof( ici ) );
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = vkCtx.swapchainFormat;
		ici.extent.width = vkCtx.swapchainExtent.width;
		ici.extent.height = vkCtx.swapchainExtent.height;
		ici.extent.depth = 1;
		ici.mipLevels = 1;
		ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		VmaAllocationCreateInfo vaci;
		memset( &vaci, 0, sizeof( vaci ) );
		vaci.usage = VMA_MEMORY_USAGE_AUTO;

		if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci, &vkCtx.offscreenImages[ i ],
				&vkCtx.offscreenAllocations[ i ], NULL ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: hidden-window frame image creation failed (%ux%u)",
					vkCtx.swapchainExtent.width, vkCtx.swapchainExtent.height );
			return false;
		}

		VkImageViewCreateInfo ivci;
		memset( &ivci, 0, sizeof( ivci ) );
		ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ivci.image = vkCtx.offscreenImages[ i ];
		ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivci.format = vkCtx.swapchainFormat;
		ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ivci.subresourceRange.levelCount = 1;
		ivci.subresourceRange.layerCount = 1;
		if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &vkCtx.offscreenViews[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: hidden-window frame image view creation failed" );
			return false;
		}
	}
	return true;
}

// The window's visibility comes from the request that placed it: a strict
// restart's own request, otherwise r_hiddenWindow, which window creation and
// every vid_restart hand to the window services. The window itself is
// created hidden and only shown afterwards, so its flags cannot tell.
static bool VK_Device_WindowHidden( void ) {
	if ( R_IsRecoverableRendererRestart() ) {
		const auto *request = R_GetRecoverableWindowRequest();
		if ( request != NULL ) return request->parms.hiddenWindow;
	}
	return r_hiddenWindow.GetBool();
}

/*
====================
VK_Device_CreateSwapchain
====================
*/
static bool VK_Device_SelectPresentMode( int requestedInterval, bool strict, VkPresentModeKHR &selected ) {
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if ( requestedInterval == 0 || ( strict && requestedInterval == -1 ) ) {
		// The complete list, never a truncated or partial one: a mode beyond a
		// fixed array must not read as unsupported.
		uint32_t modes = 0;
		const VkResult modeResult = VK_QuerySurfacePresentModes( vkGetPhysicalDeviceSurfacePresentModesKHR,
				vkCtx.physicalDevice, vkCtx.surface, modes );
		if ( modeResult != VK_SUCCESS ) { R_DisplayPresentationFailed( RDP_RECREATE_FAILED, (int32_t)modeResult ); return false; }
		const auto supported = [modes]( VkPresentModeKHR mode ) { return ( modes & ( 1u << (uint32_t)mode ) ) != 0; };
		if ( requestedInterval == -1 ) {
			if ( supported( VK_PRESENT_MODE_FIFO_RELAXED_KHR ) ) presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
		} else if ( supported( VK_PRESENT_MODE_IMMEDIATE_KHR ) ) {
			presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		} else if ( supported( VK_PRESENT_MODE_MAILBOX_KHR ) ) {
			presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
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
	const VkResult capsResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vkCtx.physicalDevice, vkCtx.surface, &caps );
	if ( capsResult != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed (%d)", (int)capsResult );
		if ( capsResult == VK_ERROR_SURFACE_LOST_KHR ) {
			vkCtx.surfaceLost = true;
		}
		return false;
	}

	// The legacy renderer already produces display-coded SDR values.  Use a
	// non-sRGB UNORM attachment with the standard nonlinear presentation colour
	// space so attachment writes do not encode those values a second time.
	VkSurfaceFormatKHR chosen = {};
	const VkResult formatResult = VK_SelectSurfaceFormat( vkGetPhysicalDeviceSurfaceFormatsKHR,
			vkCtx.physicalDevice, vkCtx.surface, chosen );
	if ( formatResult != VK_SUCCESS ) {
		if ( formatResult == VK_ERROR_FORMAT_NOT_SUPPORTED ) {
			common->Warning( "Vulkan: surface has no compatible legacy SDR UNORM + SRGB_NONLINEAR format" );
		} else {
			common->Warning( "Vulkan: surface-format enumeration failed (%d)", (int)formatResult );
		}
		return false;
	}

	// Keep the applied typed interval across resize/out-of-date recreation.
	// Legacy configuration resumes only when its source CVar value changes.
	const auto* strictRequest = R_GetRecoverableWindowRequest();
	if ( R_IsRecoverableRendererRestart() && ( strictRequest == NULL || strictRequest->swapInterval < -1 || strictRequest->swapInterval > 1 ) ) return false;
	const int requestedInterval = VK_Device_RequestedSwapInterval();
	const bool strict = R_IsRecoverableRendererRestart() || vkCtx.strictSwapInterval;
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if ( !VK_Device_SelectPresentMode( requestedInterval, strict, presentMode ) ) {
		// An explicit device request reports its unsupported interval. A typed
		// interval that a later display or surface change can no longer
		// provide must not stop every frame: present with FIFO, which every
		// surface supports. The request keeps ownership, so a later recreation
		// that finds its mode again resumes it.
		if ( R_IsRecoverableRendererRestart() || !vkCtx.strictSwapInterval ) return false;
		common->Warning( "Vulkan: swap interval %d is not available on this surface; presenting with vertical sync until it is", requestedInterval );
		presentMode = VK_PRESENT_MODE_FIFO_KHR;
	}

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
		vkCtx.surfaceExtentZero = true;
		return false;
	}
	vkCtx.surfaceExtentZero = false;

	uint32_t imageCount = caps.minImageCount + 1;
	if ( caps.maxImageCount > 0 && imageCount > caps.maxImageCount ) {
		imageCount = caps.maxImageCount;
	}
	if ( imageCount > VK_MAX_SWAPCHAIN_IMAGES ) {
		imageCount = VK_MAX_SWAPCHAIN_IMAGES;
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
	// creating with oldSwapchain retires it: the present-timing waiter lets go first
	VK_PresentTiming_Quiesce();
	const VkResult res = vkCreateSwapchainKHR( vkCtx.device, &sci, NULL, &newSwapchain );
	if ( res != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkCreateSwapchainKHR failed (%d)", (int)res );
		R_DisplayPresentationFailed( RDP_RECREATE_FAILED, (int32_t)res );
		// The old swapchain is retired even when the create fails, so it can
		// neither present nor be passed as oldSwapchain again. Callers only get
		// here with an idle device and no open frame; the next frame rebuilds.
		VK_Device_DestroySwapchainObjects();
		if ( res == VK_ERROR_SURFACE_LOST_KHR ) {
			vkCtx.surfaceLost = true;
		}
		return false;
	}

	// the old swapchain (if any) is retired by the create; destroy our
	// per-image objects and the old handle
	VK_Device_DestroySwapchainObjects();
	vkCtx.swapchain = newSwapchain;
	vkCtx.swapchainFormat = chosen.format;
	vkCtx.swapchainExtent = extent;
	VK_PresentTiming_Resume( newSwapchain );
	vkCtx.presentMode = presentMode;
	vkCtx.swapchainTransferSrc = transferSrcSupported;

	uint32_t count = 0;
	if ( vkGetSwapchainImagesKHR( vkCtx.device, vkCtx.swapchain, &count, NULL ) != VK_SUCCESS || count == 0 ) {
		common->Warning( "Vulkan: swapchain image count query failed" );
		VK_Device_DestroySwapchainObjects();
		return false;
	}
	if ( count > VK_MAX_SWAPCHAIN_IMAGES ) {
		// swapchainImages/Views/renderFinishedSemaphores are fixed arrays indexed
		// by the image index vkAcquireNextImageKHR returns, which can be any value
		// below the actual image count. An implementation that creates more than
		// requested would index them out of bounds, so fail closed rather than
		// clamp the count while the driver still owns more images.
		common->Warning( "Vulkan: swapchain created %u images, exceeding the supported maximum of %u", count,
				VK_MAX_SWAPCHAIN_IMAGES );
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
	const bool offscreenFrames = VK_Device_WindowHidden() && !r_vkHiddenWindowPresent.GetBool();
	if ( offscreenFrames && !VK_Device_CreateOffscreenImages() ) {
		VK_Device_DestroySwapchainObjects();
		return false;
	}
	vkCtx.offscreenFrames = offscreenFrames;

	common->Printf( "Vulkan: created swapchain %ux%u format=%d colorSpace=%d images=%u presentMode=%d\n",
			extent.width, extent.height, (int)chosen.format, (int)chosen.colorSpace,
			count, (int)presentMode );
	if ( offscreenFrames ) {
		common->Printf( "Vulkan: hidden window renders offscreen and does not present (r_vkHiddenWindowPresent 0)\n" );
	}
	if ( !vkCtx.swapchainTransferSrc ) {
		common->Warning( "Vulkan: swapchain does not support transfer-source captures; screenshots and backbuffer feedback are unavailable" );
	}
	// Acquire/present recreation after a live resize happens inside the
	// executor; the front end sizes its views from glConfig, so publish the
	// new drawable size here for every recreation path.
	glConfig.vidWidth = (int)extent.width;
	glConfig.vidHeight = (int)extent.height;
	// a VR frame restores the window's extent when it finishes: keep that current
	R_VR_PublishWindowSize( (int)extent.width, (int)extent.height );
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
VK_Device_PrintMemoryInfo

gfxInfo and rendererVulkanMemoryInfo: VMA's view of every memory heap, the
usage against the driver's budget plus the live allocation count and bytes.
A long session that keeps growing allocations is leaking GPU resources.
====================
*/
void VK_Device_PrintMemoryInfo( void ) {
	if ( vkCtx.allocator == VK_NULL_HANDLE ) {
		common->Printf( "Vulkan memory: unavailable (no allocator)\n" );
		return;
	}
	const VkPhysicalDeviceMemoryProperties *properties = NULL;
	vmaGetMemoryProperties( vkCtx.allocator, &properties );
	if ( properties == NULL ) {
		return;
	}
	VmaBudget budgets[ VK_MAX_MEMORY_HEAPS ];
	memset( budgets, 0, sizeof( budgets ) );
	vmaGetHeapBudgets( vkCtx.allocator, budgets );
	const double mib = 1.0 / ( 1024.0 * 1024.0 );
	for ( uint32_t heap = 0 ; heap < properties->memoryHeapCount && heap < VK_MAX_MEMORY_HEAPS ; heap++ ) {
		const VmaBudget &budget = budgets[ heap ];
		common->Printf( "Vulkan memory: heap %u %s used=%.1f MiB budget=%.1f MiB blocks=%u (%.1f MiB) allocations=%u (%.1f MiB)\n",
			heap, ( properties->memoryHeaps[ heap ].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ) != 0 ? "device-local" : "host",
			(double)budget.usage * mib, (double)budget.budget * mib,
			budget.statistics.blockCount, (double)budget.statistics.blockBytes * mib,
			budget.statistics.allocationCount, (double)budget.statistics.allocationBytes * mib );
	}
}

/*
====================
Pipeline cache

The driver keeps its compiled pipelines in an opaque blob. Without one, every
session re-compiles every pipeline the moment its material/state combination is
first drawn, which shows up as first-encounter hitching rather than as a lower
average frame rate. The blob is a disposable generated cache under fs_savepath:
it is read from the savepath only, never from a PK4, and a blob that does not
match this device is discarded rather than handed to the driver. It is written
back at shutdown and before every level load, through a staged file and an
atomic replace. Measured 2026-10-06 (Intel Xe, airdefense1 plus a full camera
sweep): the whole session creates 33 pipelines, 49 ms in total with a slowest
creation of 11 ms from an empty cache, and 4 ms in total (slowest 0.5 ms) from
a restored one, so stock content needs no background or prewarmed compilation.
====================
*/
static const char *VK_PIPELINE_CACHE_FILE = "generated/vulkan/pipeline.cache";
static const char *VK_PIPELINE_CACHE_STAGED_FILE = "generated/vulkan/pipeline.cache.partial";
// a creation this slow is a visible hitch at any refresh rate
static const uint64 VK_PIPELINE_SLOW_CREATE_US = 4000;

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
	vkCtx.pipelineCacheBytes = blobBytes > 0 ? static_cast<size_t>( blobBytes ) : 0;
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
	if ( blobBytes >= static_cast<size_t>( 256 << 20 ) ) {
		return;	// the loader would refuse it anyway
	}
	byte *blob = (byte *)Mem_Alloc( (int)blobBytes );
	if ( vkGetPipelineCacheData( vkCtx.device, vkCtx.pipelineCache, &blobBytes, blob ) != VK_SUCCESS ) {
		Mem_Free( blob );
		return;
	}
	// Stage, then replace atomically: the loader only checks the header, and a
	// blob truncated by a crash mid-write would reach the driver behind a
	// valid one.
	if ( fileSystem->WriteFile( VK_PIPELINE_CACHE_STAGED_FILE, blob, (int)blobBytes, "fs_savepath" )
				== (int)blobBytes
			&& fileSystem->PromoteFile( VK_PIPELINE_CACHE_STAGED_FILE, VK_PIPELINE_CACHE_FILE,
				"fs_savepath" ) ) {
		vkCtx.pipelineCacheBytes = blobBytes;
	} else {
		fileSystem->RemoveFileChecked( VK_PIPELINE_CACHE_STAGED_FILE, "fs_savepath" );
		common->Warning( "Vulkan: could not write the pipeline cache" );
	}
	Mem_Free( blob );
}

void VK_Device_RecordPipelineCreation( uint64 microseconds ) {
	vkCtx.pipelineCreationsSincePersist++;
	vkCtx.pipelineCreateMicrosecondsSincePersist += microseconds;
	vkCtx.pipelineCreateMaxMicrosecondsSincePersist =
			Max( vkCtx.pipelineCreateMaxMicrosecondsSincePersist, microseconds );
	vkCtx.pipelineCreations++;
	vkCtx.pipelineCreateMicroseconds += microseconds;
	vkCtx.pipelineCreateMaxMicroseconds = Max( vkCtx.pipelineCreateMaxMicroseconds, microseconds );
	if ( microseconds >= VK_PIPELINE_SLOW_CREATE_US ) {
		vkCtx.pipelineSlowCreationsSincePersist++;
		vkCtx.pipelineSlowCreations++;
	}
}

void VK_Device_PersistPipelineCache( const char *reason ) {
	if ( vkCtx.device == VK_NULL_HANDLE || vkCtx.pipelineCache == VK_NULL_HANDLE
			|| vkCtx.pipelineCreationsSincePersist <= 0 ) {
		return;	// nothing new to keep
	}
	common->Printf( "Vulkan: %d pipelines created %s (%.1f ms total, slowest %.1f ms, %d over %.0f ms)\n",
		vkCtx.pipelineCreationsSincePersist, reason != NULL ? reason : "",
		static_cast<double>( vkCtx.pipelineCreateMicrosecondsSincePersist ) / 1000.0,
		static_cast<double>( vkCtx.pipelineCreateMaxMicrosecondsSincePersist ) / 1000.0,
		vkCtx.pipelineSlowCreationsSincePersist,
		static_cast<double>( VK_PIPELINE_SLOW_CREATE_US ) / 1000.0 );
	VK_Device_SavePipelineCache();
	vkCtx.pipelineCreationsSincePersist = 0;
	vkCtx.pipelineCreateMicrosecondsSincePersist = 0;
	vkCtx.pipelineCreateMaxMicrosecondsSincePersist = 0;
	vkCtx.pipelineSlowCreationsSincePersist = 0;
}

void VK_Device_PrintPipelineInfo( void ) {
	common->Printf( "Vulkan pipelines: created=%d totalMs=%.1f maxMs=%.1f slow=%d sincePersist=%d cache=%s (%llu KiB)\n",
		vkCtx.pipelineCreations,
		static_cast<double>( vkCtx.pipelineCreateMicroseconds ) / 1000.0,
		static_cast<double>( vkCtx.pipelineCreateMaxMicroseconds ) / 1000.0,
		vkCtx.pipelineSlowCreations, vkCtx.pipelineCreationsSincePersist,
		vkCtx.pipelineCache != VK_NULL_HANDLE ? "active" : "unavailable",
		static_cast<unsigned long long>( vkCtx.pipelineCacheBytes / 1024 ) );
}

void VK_Device_WaitIdleForTiming( void ) {
	R_RenderThread_Sync( "device" );
	if ( vkCtx.initialized && vkCtx.device != VK_NULL_HANDLE && !vkCtx.presentationBlocked ) {
		vkDeviceWaitIdle( vkCtx.device );
	}
}

// The swapchain's extent, the size every recreation publishes to glConfig.
bool VK_Device_DrawableSize( int &width, int &height ) {
	if ( !vkCtx.initialized || vkCtx.swapchainExtent.width == 0 || vkCtx.swapchainExtent.height == 0 ) {
		return false;
	}
	width = (int)vkCtx.swapchainExtent.width;
	height = (int)vkCtx.swapchainExtent.height;
	return true;
}

bool VK_Device_SurfaceExtentChanged( void ) {
	if ( !vkCtx.initialized || vkCtx.surface == VK_NULL_HANDLE ) return false;
	VkSurfaceCapabilitiesKHR caps;
	if ( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vkCtx.physicalDevice, vkCtx.surface, &caps ) != VK_SUCCESS ) return true;
	if ( caps.currentExtent.width == 0xFFFFFFFFu ) return false;	// window-driven; the swap-time poll owns it
	return caps.currentExtent.width != vkCtx.swapchainExtent.width
		|| caps.currentExtent.height != vkCtx.swapchainExtent.height;
}

/*
====================
VK_Device_RecreateSwapchain
====================
*/
// The worker may discover out-of-date/surface-loss during acquire or present.
// Window extent queries and surface creation belong to the main thread on
// SDL/Wayland/macOS. The unfinished frame is skipped and recovery runs after
// the main thread collects it, never by waiting back on that thread.
static bool vkThreadedRecreatePending = false;

bool VK_Device_RecreateSwapchain( void ) {
	R_RenderThread_Sync( "device" );
	if ( R_RenderThread_OnRenderThread() ) {
		vkThreadedRecreatePending = true;
		return false;
	}
	if ( !vkCtx.initialized || vkCtx.presentationBlocked ) {
		return false;
	}
	if ( vkCtx.surfaceLost ) {
		return VK_Device_RecoverSurface();
	}
	// A minimized window keeps reporting a zero extent. Check that cheaply
	// before idling the device; each frame (and each view) would otherwise
	// repeat a full device wait for nothing.
	if ( vkCtx.surfaceExtentZero ) {
		VkSurfaceCapabilitiesKHR caps;
		if ( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vkCtx.physicalDevice, vkCtx.surface, &caps ) == VK_SUCCESS
				&& ( caps.currentExtent.width == 0 || caps.currentExtent.height == 0 ) ) {
			return false;
		}
	}
	// Waiting for the device only covers submitted work. Retiring attachment
	// views while an executor scope is still recording invalidates that
	// command buffer even when every queue is idle.
	if ( VK_GuiExecutor_FrameIsOpen() ) {
		common->Warning( "Vulkan: swapchain recreation requires a submitted frame" );
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
VK_Device_RecoverSurface

VK_ERROR_SURFACE_LOST_KHR leaves the window but not its surface. The swapchain
belongs to the surface, so both are rebuilt; the device, its resources and
the game state survive. A window that cannot provide a new surface latches
presentation, which queues the full renderer restart.
====================
*/
bool VK_Device_RecoverSurface( void ) {
	R_RenderThread_Sync( "device" );
	if ( R_RenderThread_OnRenderThread() ) {
		vkCtx.surfaceLost = true;
		vkThreadedRecreatePending = true;
		return false;
	}
	if ( !vkCtx.initialized || vkCtx.presentationBlocked ) return false;
	if ( VK_GuiExecutor_FrameIsOpen() ) return false;
	if ( vkWindowServices == NULL || vkWindowServices->CreateVulkanSurface == NULL ) {
		VK_Device_BlockPresentation( RDP_RECREATE_FAILED, VK_ERROR_SURFACE_LOST_KHR, "surface recovery" );
		return false;
	}
	const VkResult waited = vkDeviceWaitIdle( vkCtx.device );
	if ( waited != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_WAIT_FAILED, waited, "surface recovery idle wait" ); return false; }
	VK_Device_DestroySwapchainObjects();
	if ( vkCtx.surface != VK_NULL_HANDLE ) {
		vkDestroySurfaceKHR( vkCtx.instance, vkCtx.surface, NULL );
		vkCtx.surface = VK_NULL_HANDLE;
	}
	unsigned long long surfaceHandle = 0;
	VkBool32 presentable = VK_FALSE;
	if ( !vkWindowServices->CreateVulkanSurface( (void *)vkCtx.instance, &surfaceHandle ) || surfaceHandle == 0 ) {
		VK_Device_BlockPresentation( RDP_RECREATE_FAILED, VK_ERROR_SURFACE_LOST_KHR, "surface recreation" );
		return false;
	}
	vkCtx.surface = (VkSurfaceKHR)surfaceHandle;
	if ( vkGetPhysicalDeviceSurfaceSupportKHR( vkCtx.physicalDevice, vkCtx.graphicsQueueFamily, vkCtx.surface, &presentable ) != VK_SUCCESS
			|| !presentable ) {
		VK_Device_BlockPresentation( RDP_RECREATE_FAILED, VK_ERROR_SURFACE_LOST_KHR, "recreated surface presentation support" );
		return false;
	}
	vkCtx.surfaceExtentZero = false;
	vkCtx.surfaceLost = false;
	common->Printf( "Vulkan: surface recreated after surface loss\n" );
	// A zero extent here is a minimized window; the next frame retries.
	return VK_Device_CreateSwapchain();
}

void VK_Device_CompleteThreadedRecovery( void ) {
	if ( !vkThreadedRecreatePending ) {
		return;
	}
	vkThreadedRecreatePending = false;
	(void)VK_Device_RecreateSwapchain();
}

/*
====================
VK_Device_ServiceWithheldImages

r_vkPresentationFailureTest 3 reproduces a presentation engine that hands out
no images, as one does while the display is off, without turning anything off:
before each acquisition it takes every image the engine will give, so the
frame's own acquisition finds none. Presenting is the only other way to hand
an acquired image back, and these were never rendered, so clearing the drill
returns them with a swapchain rebuild.
====================
*/
bool VK_Device_ServiceWithheldImages( void ) {
	if ( r_vkPresentationFailureTest.GetInteger() != 3 ) {
		return vkNumWithheldImages == 0 || VK_Device_RecreateSwapchain();
	}
	if ( vkCtx.swapchain == VK_NULL_HANDLE ) {
		return true;
	}
	if ( vkNumWithheldImages == 0 ) {
		common->Printf( "Vulkan: withholding swapchain images (r_vkPresentationFailureTest 3)\n" );
	}
	VkFenceCreateInfo fci;
	memset( &fci, 0, sizeof( fci ) );
	fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	while ( vkNumWithheldImages < vkCtx.swapchainImageCount ) {
		VkFence fence = VK_NULL_HANDLE;
		if ( vkCreateFence( vkCtx.device, &fci, NULL, &fence ) != VK_SUCCESS ) {
			break;
		}
		// A zero timeout only takes images that are free now; more than the
		// engine can spare is legal to request when the wait is bounded.
		uint32_t imageIndex = 0;
		const VkResult res = vkAcquireNextImageKHR( vkCtx.device, vkCtx.swapchain, 0,
				VK_NULL_HANDLE, fence, &imageIndex );
		if ( res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR ) {
			vkDestroyFence( vkCtx.device, fence, NULL );
			break;
		}
		vkWithheldImageFences[ vkNumWithheldImages++ ] = fence;
	}
	return true;
}

/*
====================
VK_Device_Init
====================
*/
bool VK_Device_InjectStartupFailure( int stage ) {
	extern idCVar r_vkStartupFailure;
	extern idCVar r_vkStartupFailureDevice;
	static const char *names[] = { "off", "window", "surface", "swapchain", "renderer resources" };
	if ( stage <= 0 || stage >= (int)( sizeof( names ) / sizeof( names[ 0 ] ) )
			|| r_vkStartupFailure.GetInteger() != stage ) {
		return false;
	}
	// A device-specific drill fires only on the admitted device it names, so an
	// automatic start can be shown to resume on the next compatible adapter.
	// Window and surface failures precede admission and belong to no device.
	const int device = r_vkStartupFailureDevice.GetInteger();
	if ( device >= 0 ) {
		if ( stage < 3 || vkCtx.physicalDevice == VK_NULL_HANDLE || vkCtx.physicalDeviceIndex != (uint32_t)device ) {
			return false;
		}
		common->Printf( "Vulkan startup failure injected: %s on device %d\n", names[ stage ], device );
		return true;
	}
	common->Printf( "Vulkan startup failure injected: %s\n", names[ stage ] );
	return true;
}

bool VK_Device_Init( const renderWindowServices_s *windowServices, uint32_t firstCandidate,
		vkDeviceAttempt_t &attempt ) {
	renderDisplayChangeScope_t presentation( RDP_INIT_FAILED );
	memset( &vkCtx, 0, sizeof( vkCtx ) );
	memset( &attempt, 0, sizeof( attempt ) );
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

	// instance extensions: the window system's complete requirement list, read
	// at the size it reports, plus debug utils when validation is requested
	int windowExtensionCount = 0;
	if ( !windowServices->GetVulkanInstanceExtensions( NULL, 0, &windowExtensionCount ) || windowExtensionCount <= 0 ) {
		common->Warning( "Vulkan: window system reports no instance extensions" );
		return false;
	}
	idList<const char *> extensions;
	extensions.SetNum( windowExtensionCount );
	int reportedExtensionCount = 0;
	if ( !windowServices->GetVulkanInstanceExtensions( extensions.Ptr(), windowExtensionCount, &reportedExtensionCount )
			|| reportedExtensionCount != windowExtensionCount ) {
		common->Warning( "Vulkan: window-system instance extensions changed while being read (%d, then %d)",
				windowExtensionCount, reportedExtensionCount );
		return false;
	}

	// portability enumeration: required by the Khronos loader before it will
	// surface MoltenVK, rejected by a directly loaded MoltenVK, absent-but-
	// harmless nowhere -- so it is strictly presence-gated. The flag bit is
	// only ever set together with the extension
	// (VUID-VkInstanceCreateInfo-flags-06559).
	static const char *const optionalInstanceExtensions[] = {
		VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME
	};
	bool loaderExtensions[ 2 ] = { false, false };
	if ( !VK_Device_FindInstanceExtensions( NULL, optionalInstanceExtensions, 2, loaderExtensions ) ) {
		return false;
	}
	const bool portabilityEnumeration = loaderExtensions[ 0 ];

	const char *validationLayer = "VK_LAYER_KHRONOS_validation";
	const bool wantValidation = r_vkValidation.GetBool();
	// Debug utils normally comes from the loader. Otherwise the validation layer
	// provides it, and then only while that layer is enabled, so the retry
	// without the layer below must not keep requesting it.
	bool layerDebugUtils = false;
	if ( wantValidation && !loaderExtensions[ 1 ] ) {
		const char *debugUtils = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		VK_Device_FindInstanceExtensions( validationLayer, &debugUtils, 1, &layerDebugUtils );
	}
	int debugUtilsIndex = -1;
	if ( wantValidation && ( loaderExtensions[ 1 ] || layerDebugUtils ) ) {
		debugUtilsIndex = extensions.Append( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
	}
	if ( portabilityEnumeration ) {
		extensions.Append( VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME );
	}

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
	ici.enabledExtensionCount = (uint32_t)extensions.Num();
	ici.ppEnabledExtensionNames = extensions.Ptr();
	if ( portabilityEnumeration ) {
		ici.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}
	if ( wantValidation ) {
		ici.enabledLayerCount = 1;
		ici.ppEnabledLayerNames = &validationLayer;
	}

	VkResult res = VK_Device_CreateInstance( ici );
	if ( res != VK_SUCCESS && wantValidation ) {
		common->Warning( "Vulkan: instance creation with validation failed (%d); retrying without", (int)res );
		ici.enabledLayerCount = 0;
		ici.ppEnabledLayerNames = NULL;
		if ( layerDebugUtils && debugUtilsIndex >= 0 ) {
			extensions.RemoveIndex( debugUtilsIndex );
			debugUtilsIndex = -1;
			ici.enabledExtensionCount = (uint32_t)extensions.Num();
			ici.ppEnabledExtensionNames = extensions.Ptr();
		}
		res = VK_Device_CreateInstance( ici );
	}
	if ( res != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkCreateInstance failed (%d)", (int)res );
		return false;
	}
	volkLoadInstance( vkCtx.instance );

	if ( wantValidation && debugUtilsIndex >= 0 && vkCreateDebugUtilsMessengerEXT != NULL ) {
		VkDebugUtilsMessengerCreateInfoEXT dmci;
		memset( &dmci, 0, sizeof( dmci ) );
		dmci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		dmci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		dmci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		dmci.pfnUserCallback = VK_DebugMessengerCallback;
		const VkResult debugResult = vkCreateDebugUtilsMessengerEXT( vkCtx.instance, &dmci, NULL, &vkCtx.debugMessenger );
		if ( debugResult != VK_SUCCESS ) {
			common->Warning( "Vulkan: validation debug messenger creation failed (%d)", (int)debugResult );
		}
	}
	if ( wantValidation ) {
		if ( ici.enabledLayerCount > 0 && vkCtx.debugMessenger != VK_NULL_HANDLE ) {
			common->Printf( "Vulkan: validation enabled (VK_LAYER_KHRONOS_validation, debug messenger active)\n" );
		} else {
			common->Warning( "Vulkan: requested validation is not active" );
		}
	}

	// surface through the engine's window services
	unsigned long long surfaceHandle = 0;
	if ( VK_Device_InjectStartupFailure( 2 )
			|| !windowServices->CreateVulkanSurface( (void *)vkCtx.instance, &surfaceHandle ) || surfaceHandle == 0 ) {
		common->Warning( "Vulkan: surface creation through the window services failed" );
		VK_Device_Shutdown();
		return false;
	}
	vkCtx.surface = (VkSurfaceKHR)surfaceHandle;

	// Admit each candidate before publishing it. A graphics/present queue alone
	// is insufficient: an earlier 1.2 or feature-limited adapter must not hide a
	// later usable one. An explicit r_vkDevice selection never substitutes GPUs.
	const vkDeviceSelectionApi_t selectionApi = {
		vkEnumeratePhysicalDevices, vkGetPhysicalDeviceProperties,
		vkGetPhysicalDeviceFeatures2, vkEnumerateDeviceExtensionProperties,
		vkGetPhysicalDeviceQueueFamilyProperties, vkGetPhysicalDeviceSurfaceSupportKHR,
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR, vkGetPhysicalDeviceSurfaceFormatsKHR,
		vkGetPhysicalDeviceFormatProperties, vkGetPhysicalDeviceMemoryProperties, vkGetPhysicalDeviceProperties2
	};
	vkDeviceSelection_t selected = {};
	const vkDeviceRejection_t reportSkipped = []( uint32_t index, const VkPhysicalDeviceProperties &properties,
			const char *reason, VkResult result, void * ) {
		common->Printf( "Vulkan: skipping device %u '%s' (API %u.%u.%u, descriptor sets %u): %s (%d)\n",
				index, properties.deviceName, VK_API_VERSION_MAJOR( properties.apiVersion ),
				VK_API_VERSION_MINOR( properties.apiVersion ), VK_API_VERSION_PATCH( properties.apiVersion ),
				properties.limits.maxBoundDescriptorSets, reason, (int)result );
	};
	int forcedDevice = r_vkDevice.GetInteger();
	const char *preferredDevice = cvarSystem->GetCVarString("r_bootRenderDevice");
	// OpenXR: the headset renders with the GPU its runtime names
	int vrDevice = -1;
	if ( vkCtx.instanceFromVR ) {
		void *physical = NULL;
		if ( R_RendererModule_GetVulkanPhysicalDeviceForVR( (void *)vkCtx.instance, &physical ) && physical != NULL ) {
			vrDevice = VK_Device_PhysicalDeviceIndex( (VkPhysicalDevice)physical );
		}
		if ( vrDevice >= 0 ) {
			if ( forcedDevice >= 0 && forcedDevice != vrDevice ) {
				common->Printf( "Vulkan: r_vkDevice %d is ignored while VR runs on the headset's device %d\n", forcedDevice, vrDevice );
			}
			forcedDevice = vrDevice;
		} else {
			common->Warning( "Vulkan: the VR runtime named no usable device; VR stays off on the device chosen instead" );
		}
	}
	attempt.automatic = forcedDevice < 0 && idStr::Cmp(preferredDevice,"auto")==0;
	res = VK_SelectPhysicalDevice( selectionApi, vkCtx.instance, vkCtx.surface,
			forcedDevice, selected, reportSkipped, NULL, firstCandidate, preferredDevice );
	if ( res != VK_SUCCESS && vrDevice >= 0 ) {
		common->Warning( "Vulkan: the headset's device %d cannot run the renderer; VR stays off on the device chosen instead", vrDevice );
		vrDevice = -1;
		forcedDevice = r_vkDevice.GetInteger();
		attempt.automatic = forcedDevice < 0 && idStr::Cmp(preferredDevice,"auto")==0;
		res = VK_SelectPhysicalDevice( selectionApi, vkCtx.instance, vkCtx.surface,
				forcedDevice, selected, reportSkipped, NULL, firstCandidate, preferredDevice );
	}
	if ( res != VK_SUCCESS ) {
		if ( firstCandidate > 0 ) {
			common->Warning( "Vulkan: no further compatible physical device after rank %u (result=%d)",
					firstCandidate - 1, (int)res );
		} else {
			common->Warning( "Vulkan: no compatible physical device (r_vkDevice=%d, result=%d)", forcedDevice, (int)res );
		}
		VK_Device_Shutdown();
		return false;
	}
	vkCtx.physicalDevice = selected.device;
	vkCtx.physicalDeviceIndex = selected.index;
	idStr::Copynz(vkCtx.physicalDeviceId,selected.id,sizeof(vkCtx.physicalDeviceId));
	attempt.admitted = true;
	attempt.index = selected.index;
	idStr::Copynz( attempt.deviceName, selected.properties.deviceName, sizeof( attempt.deviceName ) );
	vkCtx.graphicsQueueFamily = selected.queueFamily;
	vkCtx.graphicsTimestampValidBits = selected.timestampValidBits;
	vkCtx.deviceProperties = selected.properties;
	common->Printf( "Vulkan: device %u '%s' (queue family %u, timestampValidBits=%u, timestampPeriod=%.6fns)\n",
			selected.index, vkCtx.deviceProperties.deviceName, selected.queueFamily,
			selected.timestampValidBits, vkCtx.deviceProperties.limits.timestampPeriod );

	// Qualification evidence names the exact driver a run used
	// (tools/tests/renderer_vulkan_qualification.py reads this line). Driver
	// properties are core in Vulkan 1.2, below the 1.3 floor checked above.
	{
		VkPhysicalDeviceDriverProperties driver = {};
		driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
		VkPhysicalDeviceProperties2 query = {};
		query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		query.pNext = &driver;
		vkGetPhysicalDeviceProperties2( vkCtx.physicalDevice, &query );
		driver.driverName[ VK_MAX_DRIVER_NAME_SIZE - 1 ] = '\0';
		driver.driverInfo[ VK_MAX_DRIVER_INFO_SIZE - 1 ] = '\0';
		common->Printf( "Vulkan: driver '%s' '%s' (driverID %d, driverVersion 0x%08x, vendorID 0x%04x, deviceID 0x%04x, deviceType %d, api %u.%u.%u)\n",
				driver.driverName, driver.driverInfo, (int)driver.driverID,
				(unsigned int)vkCtx.deviceProperties.driverVersion,
				(unsigned int)vkCtx.deviceProperties.vendorID,
				(unsigned int)vkCtx.deviceProperties.deviceID,
				(int)vkCtx.deviceProperties.deviceType,
				VK_API_VERSION_MAJOR( vkCtx.deviceProperties.apiVersion ),
				VK_API_VERSION_MINOR( vkCtx.deviceProperties.apiVersion ),
				VK_API_VERSION_PATCH( vkCtx.deviceProperties.apiVersion ) );
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
	const char *deviceExtensions[ 6 ];
	uint32_t deviceExtensionCount = 0;
	deviceExtensions[ deviceExtensionCount++ ] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	if ( r_vkSampleLocations.GetBool() && vkGetPhysicalDeviceMultisamplePropertiesEXT != NULL
			&& selected.hasSampleLocations ) {
		VkPhysicalDeviceSampleLocationsPropertiesEXT locations = {};
		locations.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT;
		VkPhysicalDeviceProperties2 query = {};
		query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		query.pNext = &locations;
		vkGetPhysicalDeviceProperties2( vkCtx.physicalDevice, &query );
		// Without variable sample locations, pipeline locations must match the
		// ones a render pass declared at begin (VUID 01525). Dynamic rendering
		// has no such declaration, so those devices keep their native pattern.
		const int maxShift = locations.variableSampleLocations == VK_TRUE ? 3 : 0;
		if ( maxShift == 0 ) {
			common->Printf( "Vulkan: MSAA sample locations stay native (no variableSampleLocations for dynamic rendering)\n" );
		}
		for ( int shift = 1; shift <= maxShift; ++shift ) {
			const VkSampleCountFlagBits samples = (VkSampleCountFlagBits)( 1u << shift );
			const unsigned int precision = shift + 1;
			const float minimum = 1.0f / (float)( 1u << precision );
			if ( ( locations.sampleLocationSampleCounts & samples ) == 0
					|| locations.sampleLocationSubPixelBits < precision
					|| locations.sampleLocationCoordinateRange[ 0 ] > minimum
					|| locations.sampleLocationCoordinateRange[ 1 ] < 1.0f - minimum ) { continue; }
			VkMultisamplePropertiesEXT grid = {};
			grid.sType = VK_STRUCTURE_TYPE_MULTISAMPLE_PROPERTIES_EXT;
			vkGetPhysicalDeviceMultisamplePropertiesEXT( vkCtx.physicalDevice, samples, &grid );
			if ( grid.maxSampleLocationGridSize.width > 0 && grid.maxSampleLocationGridSize.height > 0 ) {
				vkCtx.sampleLocationCounts |= samples;
			}
		}
		if ( vkCtx.sampleLocationCounts != 0 ) {
			deviceExtensions[ deviceExtensionCount++ ] = VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME;
		}
	}

	vkCtx.portabilitySubset = selected.hasPortabilitySubset;
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

	// r_vkPresentTiming: present ids and waits, chained after the 1.3 features
	VkPhysicalDevicePresentIdFeaturesKHR presentIdFeatures;
	memset( &presentIdFeatures, 0, sizeof( presentIdFeatures ) );
	presentIdFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
	VkPhysicalDevicePresentWaitFeaturesKHR presentWaitFeatures;
	memset( &presentWaitFeatures, 0, sizeof( presentWaitFeatures ) );
	presentWaitFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
	bool presentTiming = false;
	if ( r_vkPresentTiming.GetBool() ) {
		if ( selected.hasPresentId && selected.hasPresentWait ) {
			presentIdFeatures.pNext = &presentWaitFeatures;
			VkPhysicalDeviceFeatures2 presentQuery;
			memset( &presentQuery, 0, sizeof( presentQuery ) );
			presentQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			presentQuery.pNext = &presentIdFeatures;
			vkGetPhysicalDeviceFeatures2( vkCtx.physicalDevice, &presentQuery );
			presentTiming = presentIdFeatures.presentId == VK_TRUE && presentWaitFeatures.presentWait == VK_TRUE;
		}
		if ( presentTiming ) {
			deviceExtensions[ deviceExtensionCount++ ] = VK_KHR_PRESENT_ID_EXTENSION_NAME;
			deviceExtensions[ deviceExtensionCount++ ] = VK_KHR_PRESENT_WAIT_EXTENSION_NAME;
			features13.pNext = &presentIdFeatures;
		}
		common->Printf( "Vulkan: presented-frame timing %s\n", presentTiming ? "enabled (VK_KHR_present_wait)" : "unavailable on this device" );
	}
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
	features2.features.pipelineStatisticsQuery = supported.pipelineStatisticsQuery && r_vkGpuPipelineStatistics.GetBool();
	vkCtx.pipelineStatisticsEnabled = features2.features.pipelineStatisticsQuery == VK_TRUE;
	if ( r_vkGpuPipelineStatistics.GetBool() ) {
		common->Printf( "Vulkan pipeline statistics: requested=1 enabled=%d\n", vkCtx.pipelineStatisticsEnabled ? 1 : 0 );
	}
	// Stock Quake 4 ships BC1/BC3/BC7 art and R_BinaryImageHeaderSupportedByRenderer
	// rejects generated .bimage files when the renderer denies compression, so the
	// feature is enabled whenever the device has it and reported honestly downstream.
	features2.features.textureCompressionBC = supported.textureCompressionBC;
	// the debug tools' glLineWidth / glPointSize; they draw at 1 without these
	features2.features.wideLines = supported.wideLines;
	features2.features.largePoints = supported.largePoints;
	// r_showShadows 1/3 draws the volumes as wireframe, like GLS_POLYMODE_LINE
	features2.features.fillModeNonSolid = supported.fillModeNonSolid;
	vkCtx.depthClampSupported = supported.depthClamp == VK_TRUE;

	// OpenGL-convention clip depth in hardware. Remapping each projection's
	// depth row reaches the same window depth but rounds differently, which
	// flipped single shadow-boundary and depth-equal pixels against OpenGL.
	VkPhysicalDeviceDepthClipControlFeaturesEXT depthClipFeatures;
	memset( &depthClipFeatures, 0, sizeof( depthClipFeatures ) );
	depthClipFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT;
	bool depthClipControl = false;
	if ( r_vkDepthClipControl.GetBool() && selected.hasDepthClipControl ) {
		VkPhysicalDeviceFeatures2 depthClipQuery;
		memset( &depthClipQuery, 0, sizeof( depthClipQuery ) );
		depthClipQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		depthClipQuery.pNext = &depthClipFeatures;
		vkGetPhysicalDeviceFeatures2( vkCtx.physicalDevice, &depthClipQuery );
		depthClipFeatures.pNext = NULL;
		depthClipControl = depthClipFeatures.depthClipControl == VK_TRUE;
	}
	if ( depthClipControl ) {
		deviceExtensions[ deviceExtensionCount++ ] = VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME;
		depthClipFeatures.pNext = features2.pNext;
		features2.pNext = &depthClipFeatures;
	}
	common->Printf( "Vulkan: clip-space depth %s\n", depthClipControl
			? "[-1, 1] (VK_EXT_depth_clip_control)" : "[0, 1] (remapped projections)" );
	vkCtx.depthBoundsSupported = supported.depthBounds == VK_TRUE;
	vkCtx.textureCompressionBCSupported = supported.textureCompressionBC == VK_TRUE;
	vkCtx.wideLinesSupported = supported.wideLines == VK_TRUE;
	vkCtx.fillModeNonSolidSupported = supported.fillModeNonSolid == VK_TRUE;
	vkCtx.largePointsSupported = supported.largePoints == VK_TRUE;
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

	res = VK_Device_CreateLogicalDevice( dci, vrDevice >= 0 );
	if ( res != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkCreateDevice failed (%d)", (int)res );
		VK_Device_Shutdown();
		return false;
	}
	volkLoadDevice( vkCtx.device );
	vkCtx.depthClipNegativeOneToOne = depthClipControl;
	if ( presentTiming && vkWaitForPresentKHR != NULL ) {
		VK_PresentTiming_Start( vkCtx.device );
	}
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

	if ( VK_Device_InjectStartupFailure( 3 ) || !VK_Device_CreateSwapchain() ) {
		VK_Device_Shutdown();
		return false;
	}

	VK_Device_RestrictSampleLocationDepthFormats();
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
	vkThreadedRecreatePending = false;
	R_DisplayPresentationShutdown();
	// before the swapchain and device it waits on go away
	VK_PresentTiming_Stop();
	if ( vkCtx.device != VK_NULL_HANDLE ) {
		vkDeviceWaitIdle( vkCtx.device );
	}
	if ( vkCtx.pipelineCache != VK_NULL_HANDLE ) {
		VK_Device_PersistPipelineCache( "this session" );
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
VK_Device_AbandonAdapter

Startup's adapter retry. Pipelines a failed adapter compiled are not worth
keeping, and writing them would replace the one cache file with a blob that
the adapter about to be tried discards as foreign.
====================
*/
void VK_Device_AbandonAdapter( void ) {
	vkCtx.pipelineCreationsSincePersist = 0;
	VK_Device_Shutdown();
}

/*
====================
VK_Device_WaitUploadBatch / VK_Device_FlushUploadBatch / VK_Device_BatchedUpload
====================
*/

// staging bytes a single upload batch may own before it is force-flushed
static const VkDeviceSize VK_UPLOAD_BATCH_BYTE_BUDGET = 64u << 20;

void VK_Device_WaitUploadBatch( void ) {
	// the queue and the upload command buffer belong to the render thread while a frame is in flight
	R_RenderThread_Sync( "device" );
	if ( !vkCtx.uploadBatchInFlight || vkCtx.presentationBlocked ) {
		return;
	}
	const uint64_t completedBatch = vkCtx.uploadBatchSerial;
	const VkDevice completedDevice = vkCtx.device;
	VkResult res = vkWaitForFences( vkCtx.device, 1, &vkCtx.uploadFence, VK_TRUE, UINT64_MAX );
	if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_WAIT_FAILED, res, "upload fence wait" ); return; }
    if (!completedBatch || vkCtx.uploadBatchSerial != completedBatch || vkCtx.device != completedDevice ||
        !vkCtx.uploadBatchInFlight || vkCtx.presentationBlocked) {
        VK_Device_BlockPresentation(RDP_WAIT_FAILED, VK_ERROR_UNKNOWN, "upload completion identity changed"); return;
    }
	res = vkResetFences( vkCtx.device, 1, &vkCtx.uploadFence );
	if ( res != VK_SUCCESS ) { VK_Device_BlockPresentation( RDP_WAIT_FAILED, res, "upload fence reset" ); return; }
    if (!completedBatch || vkCtx.uploadBatchSerial != completedBatch || vkCtx.device != completedDevice ||
        !vkCtx.uploadBatchInFlight || vkCtx.presentationBlocked) {
        VK_Device_BlockPresentation(RDP_WAIT_FAILED, VK_ERROR_UNKNOWN, "upload completion identity changed"); return;
    }
	for ( int i = 0; i < vkCtx.numUploadBatchInFlight; i++ ) {
		vmaDestroyBuffer( vkCtx.allocator, vkCtx.uploadBatchInFlightBuffers[ i ],
				vkCtx.uploadBatchInFlightAllocations[ i ] );
	}
    if (!completedBatch || vkCtx.uploadBatchSerial != completedBatch || vkCtx.device != completedDevice ||
        !vkCtx.uploadBatchInFlight || vkCtx.presentationBlocked) {
        VK_Device_BlockPresentation(RDP_WAIT_FAILED, VK_ERROR_UNKNOWN, "upload completion identity changed"); return;
    }
	vkCtx.numUploadBatchInFlight = 0;
	vkCtx.uploadBatchInFlight = false;
	vkCtx.uploadBatchCompletedSerial = completedBatch;
}

void VK_Device_FlushUploadBatch( void ) {
	R_RenderThread_Sync( "device" );
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
		VkBuffer staging, VmaAllocation stagingAllocation, VkDeviceSize stagingBytes, uint64_t* acceptedBatch ) {
	R_RenderThread_Sync( "device" );
	if ( vkCtx.device == VK_NULL_HANDLE || vkCtx.uploadCommandBuffer == VK_NULL_HANDLE || vkCtx.presentationBlocked ) {
		return false;
	}

	if ( !vkCtx.uploadBatchOpen ) {
		VK_Device_WaitUploadBatch();
		if ( vkCtx.presentationBlocked ) return false;
        const uint64_t nextBatch = R_ImagePolicyNewResourceIdentity();
        if (!nextBatch) {
            VK_Device_BlockPresentation(RDP_RECORD_FAILED, VK_ERROR_UNKNOWN, "upload observation serial exhausted");
            return false;
        }
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
        vkCtx.uploadBatchSerial = nextBatch;
		vkCtx.uploadBatchOpen = true;
	}

	record( vkCtx.uploadCommandBuffer, user );
	if (acceptedBatch) *acceptedBatch = vkCtx.uploadBatchSerial;

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
	R_RenderThread_Sync( "device" );
	if ( image == VK_NULL_HANDLE && view == VK_NULL_HANDLE && secondaryView == VK_NULL_HANDLE
			&& buffer == VK_NULL_HANDLE && allocation == NULL ) {
		return;
	}
	const int slot = vkCtx.recordingSlot;
	vkDeferredDestroy_t entry;
	entry.image = image;
	entry.view = view;
	entry.secondaryView = secondaryView;
	entry.buffer = buffer;
	entry.allocation = allocation;
	if ( vkCtx.numDeferredDestroys[ slot ] < VK_MAX_DEFERRED_DESTROYS ) {
		vkCtx.deferredDestroys[ slot ][ vkCtx.numDeferredDestroys[ slot ]++ ] = entry;
	} else {
		// Overflow follows the same slot fence as the fixed queue. Waiting for
		// the device here would not submit or protect the current frame.
		vkDeferredDestroyOverflow[ slot ].Append( entry );
	}
}

void VK_Device_FlushDeferredDestroys( int slot ) {
	// an upload batch that is still open has recorded copies into images this
	// slot may be about to release; the slot fence does not cover it, because
	// it is only submitted with the next frame. Submit and retire it first.
	if ( vkCtx.uploadBatchOpen && ( vkCtx.numDeferredDestroys[ slot ] > 0
			|| vkDeferredDestroyOverflow[ slot ].Num() > 0 ) ) {
		VK_Device_FlushUploadBatch();
		VK_Device_WaitUploadBatch();
	}
	const int fixedCount = vkCtx.numDeferredDestroys[ slot ];
	for ( int i = 0; i < fixedCount + vkDeferredDestroyOverflow[ slot ].Num(); i++ ) {
		const vkDeferredDestroy_t &entry = i < fixedCount
				? vkCtx.deferredDestroys[ slot ][ i ] : vkDeferredDestroyOverflow[ slot ][ i - fixedCount ];
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
	vkDeferredDestroyOverflow[ slot ].Clear();
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
