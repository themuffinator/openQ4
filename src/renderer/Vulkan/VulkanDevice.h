// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __VULKANDEVICE_H__
#define __VULKANDEVICE_H__

/*
===============================================================================

	Persistent Vulkan device + swapchain context (Phase C,
	docs/dev/archive/2026-10-18/plans/2026-07-17-vulkan-phase-c.md).

	Owns the instance, surface (created through the engine's window
	services), physical/logical device, queues, VMA allocator, swapchain,
	and the frames-in-flight synchronization. Phase C drives it to an
	animated clear; later phases attach the real draw path.

===============================================================================
*/

#include "volk.h"
#include "VulkanDeviceSelection.h"
#include "../DisplayPresentation.h"

// VMA handles as opaque forward declarations; TUs that call VMA include
// vk_mem_alloc.h themselves (with the PCH-poison compensations)
struct VmaAllocator_T;
typedef struct VmaAllocator_T *VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T *VmaAllocation;

struct renderWindowServices_s;

// per-slot frame synchronization (frames in flight)
static const int VK_FRAMES_IN_FLIGHT = 2;

// vkCmdSetScissor for view-derived rectangles. A tiled capture shifts its
// views by tr.viewportOffset, as OpenGL does, so their rectangles can start
// left of or above the attachment; Vulkan forbids negative scissor offsets,
// so the part outside is cut off, which rasterizes exactly the same pixels.
static inline void vkCmdSetScissorClamped( VkCommandBuffer cmd, uint32_t firstScissor,
		uint32_t scissorCount, const VkRect2D *scissors ) {
	VkRect2D clamped[ 4 ];
	if ( scissorCount == 0 || scissorCount > 4 ) {
		vkCmdSetScissor( cmd, firstScissor, scissorCount, scissors );
		return;
	}
	for ( uint32_t i = 0; i < scissorCount; ++i ) {
		VkRect2D rect = scissors[ i ];
		if ( rect.offset.x < 0 ) {
			const uint32_t cut = static_cast<uint32_t>( -static_cast<int64_t>( rect.offset.x ) );
			rect.extent.width = rect.extent.width > cut ? rect.extent.width - cut : 0;
			rect.offset.x = 0;
		}
		if ( rect.offset.y < 0 ) {
			const uint32_t cut = static_cast<uint32_t>( -static_cast<int64_t>( rect.offset.y ) );
			rect.extent.height = rect.extent.height > cut ? rect.extent.height - cut : 0;
			rect.offset.y = 0;
		}
		clamped[ i ] = rect;
	}
	vkCmdSetScissor( cmd, firstScissor, scissorCount, clamped );
}

// Explicit startup fault injection, inactive during normal rendering.
bool VK_Device_InjectStartupFailure( int stage );

// deferred GPU-object destruction: resources retired while their frame may
// still be in flight are queued per slot and destroyed once that slot's
// fence has been waited on
typedef struct vkDeferredDestroy_s {
	VkImage				image;
	VkImageView			view;
	VkImageView			secondaryView;
	VkBuffer			buffer;
	VmaAllocation		allocation;
} vkDeferredDestroy_t;

static const int VK_MAX_DEFERRED_DESTROYS = 512;

// staging buffers a single upload batch may own before it is force-flushed
static const int VK_MAX_UPLOAD_BATCH_STAGING = 1024;

// Per-image objects are indexed by the acquired image index. A swapchain the
// driver creates with more images than this fails closed instead.
static const uint32_t VK_MAX_SWAPCHAIN_IMAGES = 16;

typedef struct vkDeviceContext_s {
	bool				initialized;
	// A failed post-acquire operation may leave semaphores signaled or queue
	// state unknown. Never reuse them or wait an unsubmitted frame fence;
	// only a new full device lifetime releases this latch.
	bool				presentationBlocked;
	// The automatic renderer restart for a latched device has been queued.
	bool				presentationRecoveryQueued;
	// The surface reported a zero extent (minimized); swapchain recreation
	// waits for a real size instead of idling the device every attempt.
	bool				surfaceExtentZero;
	// A surface query or swapchain create reported VK_ERROR_SURFACE_LOST_KHR;
	// the next recreation rebuilds the surface first.
	bool				surfaceLost;

	VkInstance			instance;
	// OpenXR's runtime created the instance and the device (render API 32
	// module services), on the GPU the headset renders with
	bool				instanceFromVR;
	bool				deviceFromVR;
	VkDebugUtilsMessengerEXT debugMessenger;
	VkSurfaceKHR		surface;
	VkPhysicalDevice	physicalDevice;
	// enumeration index of the admitted device (r_vkDevice uses the same order)
	uint32_t			physicalDeviceIndex;
	char				physicalDeviceId[33];
	VkPhysicalDeviceProperties deviceProperties;
	// Fixed for the device lifetime. Zero retains the driver's native pattern.
	// All multisample pipelines and depth-image transitions use the same pattern.
	VkSampleCountFlags sampleLocationCounts;
	bool				depthClampSupported;
	// Clip-space z follows OpenGL's [-w, w] in hardware
	// (VK_EXT_depth_clip_control negativeOneToOne on every graphics pipeline):
	// projections are then used unmodified, and window depth rounds exactly as
	// glDepthRange(0, 1) does. Fixed for the device lifetime.
	bool				depthClipNegativeOneToOne;
	bool				depthBoundsSupported;
	// Opt-in diagnostic feature, fixed for the device lifetime.
	bool				pipelineStatisticsEnabled;
	// line widths and point sizes other than 1, for the debug tools
	// (glLineWidth / glPointSize); both optional, 1 when missing
	bool				wideLinesSupported;
	// polygonMode LINE: only the r_showShadows wireframe uses it.
	bool				fillModeNonSolidSupported;
	bool				largePointsSupported;
	// Vulkan Portability (MoltenVK on macOS): the device is a portability
	// implementation whose optional subset features must be honored instead of
	// assumed. False on native drivers, where the whole subset reads as
	// supported.
	bool				portabilitySubset;
	bool				portabilityImageViewFormatSwizzle;
	bool				portabilityMutableComparisonSamplers;
	bool				textureCompressionBCSupported;
	// VK_FORMAT_R5G6B5_UNORM_PACK16 backs the light projection/falloff cookies.
	// Metal only exposes the packed 16-bit formats on Apple GPUs, so this is
	// probed and the image path widens to RGBA8 when it is missing.
	bool				packed565Supported;
	VkDevice			device;
	uint32_t			graphicsQueueFamily;	// also the present family (required)
	uint32_t			graphicsTimestampValidBits;
	VkQueue				graphicsQueue;

	VkSwapchainKHR		swapchain;
	VkFormat			swapchainFormat;
	VkExtent2D			swapchainExtent;
	VkPresentModeKHR	presentMode;
	uint32_t			swapchainImageCount;
	VkImage				swapchainImages[ VK_MAX_SWAPCHAIN_IMAGES ];
	VkImageView			swapchainViews[ VK_MAX_SWAPCHAIN_IMAGES ];
	bool				swapchainTransferSrc;
	// A hidden window (r_hiddenWindow) is never displayed, so its frames skip
	// the presentation engine: each renders into its frame slot's offscreen
	// image, which screenshots read as they read a swapchain image, and is
	// neither acquired nor presented. With the display off (modern standby, a
	// sleeping monitor) a hidden window's engine handed images back only every
	// quarter to whole second, and an acquire often waited out its timeout.
	// The swapchain is still created and recreated, unused, so present-mode
	// requests and reports behave as before. These images live and die with
	// it; r_vkHiddenWindowPresent 1 presents hidden frames again.
	bool				offscreenFrames;
	VkImage				offscreenImages[ VK_FRAMES_IN_FLIGHT ];
	VkImageView			offscreenViews[ VK_FRAMES_IN_FLIGHT ];
	VmaAllocation		offscreenAllocations[ VK_FRAMES_IN_FLIGHT ];

	VkCommandPool		commandPool;
	VkCommandBuffer		commandBuffers[ VK_FRAMES_IN_FLIGHT ];
	VkSemaphore			acquireSemaphores[ VK_FRAMES_IN_FLIGHT ];
	// one render-finished semaphore per swapchain image: present may still
	// read the semaphore of an image the acquire slot has already recycled
	VkSemaphore			renderFinishedSemaphores[ VK_MAX_SWAPCHAIN_IMAGES ];
	VkFence				frameFences[ VK_FRAMES_IN_FLIGHT ];
	int					frameSlot;
	// Slot whose command buffer is currently recording. frameSlot advances
	// when a slot is claimed, so mid-frame resource retirement must use
	// this value to remain behind the recording frame's fence.
	int					recordingSlot;

	// requested swap interval the swapchain was created with; a change
	// triggers recreation at the next present
	int					swapInterval;
	// A typed device request owns its interval until the configured CVar value
	// changes. Loading-screen invalidation is not an explicit configuration edit.
	bool				strictSwapInterval;
	int					strictSwapIntervalValue;
	int					strictSwapIntervalCvar;

	// Driver-owned pipeline blob, seeded from and written back to a
	// disposable fs_savepath cache so a session does not re-compile every
	// pipeline the last one already built.
	VkPipelineCache		pipelineCache;
	// Creation cost since the blob was last written, and for the whole device.
	int					pipelineCreationsSincePersist;
	uint64				pipelineCreateMicrosecondsSincePersist;
	uint64				pipelineCreateMaxMicrosecondsSincePersist;
	int					pipelineSlowCreationsSincePersist;
	int					pipelineCreations;
	uint64				pipelineCreateMicroseconds;
	uint64				pipelineCreateMaxMicroseconds;
	int					pipelineSlowCreations;
	size_t				pipelineCacheBytes;	// size of the blob last read or written

	// --- Phase D ---
	VmaAllocator		allocator;

	// --- Phase E ---
	// per-frame-slot depth/stencil attachment (two frames can overlap on the
	// GPU, so a single shared depth image would race); recreated with the
	// swapchain, transient contents (cleared per 3D view, never stored)
	VkFormat			depthFormat;			// probed D24S8 or D32S8
	// Shadow maps need attachment + sampled-image + tile-transfer support.
	// Keep their format independent from the main depth/stencil attachment so
	// depth-only fallbacks remain available on devices with narrower support.
	VkFormat			shadowDepthFormat;
	bool				shadowDepthHasStencil;
	bool				shadowDepthFilterLinear;
	VkImage				depthImages[ VK_FRAMES_IN_FLIGHT ];
	VkImageView			depthViews[ VK_FRAMES_IN_FLIGHT ];
	VmaAllocation		depthAllocations[ VK_FRAMES_IN_FLIGHT ];

	// batched upload path: uploads record into uploadCommandBuffer; the batch
	// is submitted before every frame/clear-frame submission (so consuming
	// frames execute after their uploads), when the staging budget fills, or
	// with a CPU wait at wait-idle teardown points
	VkCommandBuffer		uploadCommandBuffer;
	VkFence				uploadFence;
	uint64_t            uploadBatchSerial, uploadBatchCompletedSerial;
	bool				uploadBatchOpen;		// commands recorded, not yet submitted
	bool				uploadBatchInFlight;	// submitted, uploadFence not yet waited
	int					numUploadBatchPending;
	VkDeviceSize		uploadBatchPendingBytes;
	VkBuffer			uploadBatchPendingBuffers[ VK_MAX_UPLOAD_BATCH_STAGING ];
	VmaAllocation		uploadBatchPendingAllocations[ VK_MAX_UPLOAD_BATCH_STAGING ];
	int					numUploadBatchInFlight;
	VkBuffer			uploadBatchInFlightBuffers[ VK_MAX_UPLOAD_BATCH_STAGING ];
	VmaAllocation		uploadBatchInFlightAllocations[ VK_MAX_UPLOAD_BATCH_STAGING ];

	vkDeferredDestroy_t	deferredDestroys[ VK_FRAMES_IN_FLIGHT ][ VK_MAX_DEFERRED_DESTROYS ];
	int					numDeferredDestroys[ VK_FRAMES_IN_FLIGHT ];
} vkDeviceContext_t;

// the module-wide device context; valid while initialized is true
extern vkDeviceContext_t vkCtx;

bool VK_Device_SampleLocations( VkSampleCountFlagBits samples, VkSampleLocationsInfoEXT &info );

// resolves the Vulkan library into volk exactly the way SDL will for the
// surface (SDL_VULKAN_LIBRARY, the bundled MoltenVK, then the system loader);
// the bring-up probe shares it so both load the same image
bool	VK_Device_InitLoader( void );

// What one VK_Device_Init attempt reached. A failure after admission belongs
// to that adapter, so automatic selection may resume after it; a failure before
// admission (loader, instance, surface, no candidate) is not specific to a GPU.
typedef struct vkDeviceAttempt_s {
	bool				admitted;
	bool				automatic;		// no diagnostic index or explicit UUID preference
	uint32_t			index;
	char				deviceName[ VK_MAX_PHYSICAL_DEVICE_NAME_SIZE ];
} vkDeviceAttempt_t;

// full bring-up through the window services: instance (+validation when
// r_vkValidation), surface, device selection honoring r_vkDevice, queues,
// swapchain, per-frame sync. firstCandidate is a high-performance preference rank.
// Returns false with everything torn down on any failure so the loader's
// fail-closed ladder stays reachable; attempt says how far it got.
bool	VK_Device_Init( const renderWindowServices_s *windowServices, uint32_t firstCandidate,
			vkDeviceAttempt_t &attempt );
void	VK_Device_Shutdown( void );
// Startup only: tears down an adapter that failed after admission without
// writing its pipeline cache, so the next candidate starts from a clean slate.
void	VK_Device_AbandonAdapter( void );

// recreates the swapchain (resize / OUT_OF_DATE / swap-interval change);
// reads the current window pixel size through the services
bool	VK_Device_RecreateSwapchain( void );
// VK_ERROR_SURFACE_LOST_KHR: rebuild the surface and swapchain in place;
// latches presentation when the window can no longer provide a surface.
bool	VK_Device_RecoverSurface( void );
// Once per frame: queue the automatic renderer restart a latched device
// needs, within a per-session budget (see VK_Device_BlockPresentation).
void	VK_Device_ServicePresentationRecovery( void );
// The surface's current extent differs from the swapchain's.
bool	VK_Device_SurfaceExtentChanged( void );
int		VK_Device_RequestedSwapInterval( void );
void	VK_Device_BlockPresentation( renderDisplayOutcome_t outcome, VkResult error, const char *operation );

// Accounts one ordinary indexed draw the way the OpenGL backend's
// RB_DrawElementsWithCounters does, so both backends report the same
// workload. Shadow-volume draws keep their own c_shadow* counters, which
// is also what GL does, so they must not be passed here.
void	VK_Device_CountDrawIndexed( int indexCount, int vertexCount );

// per-heap VMA usage, budget and live allocations (gfxInfo,
// rendererVulkanMemoryInfo)
void	VK_Device_PrintMemoryInfo( void );

// Pipelines are created synchronously at first use, so their creation time is
// time the frame that first draws a state combination waits. Every
// vkCreate*Pipelines call reports its duration here.
void	VK_Device_RecordPipelineCreation( uint64 microseconds );
// Writes the pipeline cache blob back to fs_savepath through a staged file and
// an atomic replace (a crash mid-write never leaves a truncated blob), and logs
// what was created since the last write. Called at shutdown and before each
// level load, so a session that does not exit cleanly keeps what earlier maps
// compiled.
void	VK_Device_PersistPipelineCache( const char *reason );
void	VK_Device_PrintPipelineInfo( void );

// Presented-frame timing (r_vkPresentTiming, VK_KHR_present_wait): the id to
// chain on the next present (0 = none, timing is off or between swapchains),
// and the report that the present carrying it was queued.
uint64_t	VK_PresentTiming_NextId( void );
void		VK_PresentTiming_Presented( uint64_t id );

// Called before each frame's acquisition. r_vkPresentationFailureTest 3 takes
// every swapchain image the presentation engine would hand out, as a display
// that is off withholds them; clearing the drill rebuilds the swapchain to
// return them. False when that rebuild fails.
bool	VK_Device_ServiceWithheldImages( void );
// Main thread, after joining a frame: service window/surface recovery that the
// worker requested. A failed acquire never calls SDL from the render thread.
void	VK_Device_CompleteThreadedRecovery( void );

typedef void ( *vkImmediateRecord_t )( VkCommandBuffer cmd, void *user );
// records upload commands into the shared upload command buffer, opening a
// batch if none is open (recycling any in-flight batch first). On true the
// batch takes ownership of the staging buffer/allocation; the commands reach
// the GPU before the next frame or clear-frame submission. Returns false
// without recording when no device/upload command buffer exists.
bool	VK_Device_BatchedUpload( vkImmediateRecord_t record, void *user,
			VkBuffer staging, VmaAllocation stagingAllocation, VkDeviceSize stagingBytes, uint64_t* acceptedBatch = nullptr );
// submits the open upload batch (if any) without a CPU wait; must run before
// every queue submission so consuming work executes after its uploads
void	VK_Device_FlushUploadBatch( void );
// waits out the last submitted batch and releases its staging buffers; must
// run before deferred destroys are flushed and before wait-idle teardown
void	VK_Device_WaitUploadBatch( void );

// queues GPU objects for destruction once the current frame slot's fence
// has cycled; any handle may be VK_NULL_HANDLE
void	VK_Device_DeferDestroy( VkImage image, VkImageView view, VkBuffer buffer, VmaAllocation allocation,
			VkImageView secondaryView = VK_NULL_HANDLE );

// drains the destroy queue for a slot whose fence has just been waited on
void	VK_Device_FlushDeferredDestroys( int slot );

#endif /* !__VULKANDEVICE_H__ */
