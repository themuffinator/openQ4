// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __VULKANDEVICE_H__
#define __VULKANDEVICE_H__

/*
===============================================================================

	Persistent Vulkan device + swapchain context (Phase C,
	docs/dev/plans/2026-07-17-vulkan-phase-c.md).

	Owns the instance, surface (created through the engine's window
	services), physical/logical device, queues, VMA allocator, swapchain,
	and the frames-in-flight synchronization. Phase C drives it to an
	animated clear; later phases attach the real draw path.

===============================================================================
*/

#include "volk.h"
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

// The widest pipeline layout the back end builds is the shadowed-interaction
// one: six reused per-image sampler sets, the interaction UBO set, and the
// shadow set. Metal-backed implementations cap maxBoundDescriptorSets at
// exactly this value, so it is checked once at device creation.
static const int VK_REQUIRED_BOUND_DESCRIPTOR_SETS = 8;

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

typedef struct vkDeviceContext_s {
	bool				initialized;
	// A failed post-acquire operation may leave semaphores signaled or queue
	// state unknown. Never reuse them or wait an unsubmitted frame fence;
	// only a new full device lifetime releases this latch.
	bool				presentationBlocked;

	VkInstance			instance;
	VkDebugUtilsMessengerEXT debugMessenger;
	VkSurfaceKHR		surface;
	VkPhysicalDevice	physicalDevice;
	VkPhysicalDeviceProperties deviceProperties;
	bool				depthClampSupported;
	bool				depthBoundsSupported;
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
	VkImage				swapchainImages[ 8 ];
	VkImageView			swapchainViews[ 8 ];
	bool				swapchainTransferSrc;

	VkCommandPool		commandPool;
	VkCommandBuffer		commandBuffers[ VK_FRAMES_IN_FLIGHT ];
	VkSemaphore			acquireSemaphores[ VK_FRAMES_IN_FLIGHT ];
	// one render-finished semaphore per swapchain image: present may still
	// read the semaphore of an image the acquire slot has already recycled
	VkSemaphore			renderFinishedSemaphores[ 8 ];
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

// full bring-up through the window services: instance (+validation when
// r_vkValidation), surface, device selection honoring r_vkDevice, queues,
// swapchain, per-frame sync. Returns false with everything torn down on any
// failure so the loader's fail-closed ladder stays reachable.
bool	VK_Device_Init( const renderWindowServices_s *windowServices );
void	VK_Device_Shutdown( void );

// recreates the swapchain (resize / OUT_OF_DATE / swap-interval change);
// reads the current window pixel size through the services
bool	VK_Device_RecreateSwapchain( void );
int		VK_Device_RequestedSwapInterval( void );
void	VK_Device_BlockPresentation( renderDisplayOutcome_t outcome, VkResult error, const char *operation );

// Accounts one ordinary indexed draw the way the OpenGL backend's
// RB_DrawElementsWithCounters does, so both backends report the same
// workload. Shadow-volume draws keep their own c_shadow* counters, which
// is also what GL does, so they must not be passed here.
void	VK_Device_CountDrawIndexed( int indexCount, int vertexCount );

// acquires, records a dynamic-rendering clear with the given color, and
// presents; handles OUT_OF_DATE/SUBOPTIMAL by recreating and retrying once
void	VK_Device_PresentClearFrame( const float clearColor[ 4 ] );

typedef void ( *vkImmediateRecord_t )( VkCommandBuffer cmd, void *user );
// records upload commands into the shared upload command buffer, opening a
// batch if none is open (recycling any in-flight batch first). On true the
// batch takes ownership of the staging buffer/allocation; the commands reach
// the GPU before the next frame or clear-frame submission. Returns false
// without recording when no device/upload command buffer exists.
bool	VK_Device_BatchedUpload( vkImmediateRecord_t record, void *user,
			VkBuffer staging, VmaAllocation stagingAllocation, VkDeviceSize stagingBytes );
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
