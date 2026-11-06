// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
//

/*
===============================================================================

	OpenXR presentation, Vulkan backend.

	The runtime creates this renderer's instance and device
	(XR_KHR_vulkan_enable2; VulkanDevice.cpp offers both through the module
	services), so the session binds to the device the renderer draws with and
	the swapchain images are VkImages on it.

	While a VR frame presents, an eye or the virtual screen stands in for the
	window: the executor's default output (VK_Exec_SetDefaultOutput) points at
	targets this file owns. They are top-down like the window, in its depth
	format, and in the UNORM twin of the runtime's colour format, so the
	gamma-encoded bytes the engine writes reach the runtime's sRGB images
	unconverted. A target gets the window's back-buffer passes (CRT, then
	the executor's r_brightness/r_gamma display mapping) when the frame
	leaves it, so the window mirror of finished targets is not mapped again. At the swap each finished
	target is copied into the swapchain image the engine acquired for it, one
	of them is mirrored into the window, and the images return to the
	colour-attachment layout the runtime expects at release; the frame's
	submission follows before the engine releases them.
	See docs/dev/archive/2026-10-18/plans/2026-10-02-openxr-vr.md.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../TemporalPresentation.h"
#include "volk.h"
#include "vk_mem_alloc.h"
#include "VulkanDevice.h"
#include "vk_ExecutorHooks.h"

void VK_PostProcess_ApplyBackBuffer( void );

// The window's back-buffer passes for the VR target the frame is leaving.
static void VK_VR_FinishTarget( void ) {
	VK_PostProcess_ApplyBackBuffer();
	(void)VK_Exec_MapDefaultOutputColor();
}

typedef struct vkVRTarget_s {
	VkImage				color;
	VkImageView			colorView;
	VmaAllocation		colorAllocation;
	VkImage				depth;
	VkImageView			depthView;
	VmaAllocation		depthAllocation;
	int					width;
	int					height;
	VkFormat			format;
} vkVRTarget_t;

static vkVRTarget_t		vkVRTargets[3];				// virtual screen, left eye, right eye
static int				rb_vrTarget = -2;			// -2 none, -1 virtual screen, 0/1 eye
static int				rb_vrFrameCount = -1;		// renderer frame the backend VR state belongs to
static bool				rb_vrCleared[3];			// target index cleared this frame

// The colour format the targets render in: the runtime's format without its
// sRGB encoding, which a byte copy then supplies.
static VkFormat VK_VR_TargetFormat( int swapchainFormat ) {
	switch ( swapchainFormat ) {
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_R8G8B8A8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case VK_FORMAT_B8G8R8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_UNORM:
			return VK_FORMAT_B8G8R8A8_UNORM;
		default:
			return VK_FORMAT_UNDEFINED;
	}
}

static bool VK_VR_DeviceUsable( void ) {
	return vkCtx.initialized && vkCtx.device != VK_NULL_HANDLE && !vkCtx.presentationBlocked;
}

/*
====================
R_VR_BackendCanPresent
====================
*/
bool R_VR_BackendCanPresent( void ) {
	return glConfig.isInitialized && VK_VR_DeviceUsable() && vkCtx.depthFormat != VK_FORMAT_UNDEFINED;
}

/*
====================
R_VR_QueryGraphicsBinding

The engine binds the session to this device only when its runtime created
it; a device made before VR asked has it restart the renderer.
====================
*/
bool R_VR_QueryGraphicsBinding( renderVRGraphicsBinding_t &binding ) {
	memset( &binding, 0, sizeof( binding ) );
	if ( !R_VR_BackendCanPresent() ) {
		return false;
	}
	binding.kind = RENDER_VR_BINDING_VULKAN;
	binding.vkInstance = (void *)vkCtx.instance;
	binding.vkPhysicalDevice = (void *)vkCtx.physicalDevice;
	binding.vkDevice = (void *)vkCtx.device;
	binding.vkQueueFamilyIndex = vkCtx.graphicsQueueFamily;
	binding.vkQueueIndex = 0;
	binding.vkColorFormat = (int)vkCtx.swapchainFormat;
	return true;
}

/*
===============================================================================

	Targets

===============================================================================
*/

static void VK_VR_DestroyTarget( vkVRTarget_t &target ) {
	if ( vkCtx.initialized && vkCtx.device != VK_NULL_HANDLE ) {
		// the last frames that drew into them may still be in flight
		VK_Device_DeferDestroy( target.color, target.colorView, VK_NULL_HANDLE, target.colorAllocation );
		VK_Device_DeferDestroy( target.depth, target.depthView, VK_NULL_HANDLE, target.depthAllocation );
	}
	memset( &target, 0, sizeof( target ) );
}

static bool VK_VR_CreateImage( VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, int width, int height,
		VkImage &image, VkImageView &view, VmaAllocation &allocation ) {
	VkImageCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = format;
	ici.extent.width = (uint32_t)width;
	ici.extent.height = (uint32_t)height;
	ici.extent.depth = 1;
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = usage;
	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;
	if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci, &image, &allocation, NULL ) != VK_SUCCESS ) {
		image = VK_NULL_HANDLE;
		allocation = NULL;
		return false;
	}
	VkImageViewCreateInfo ivci;
	memset( &ivci, 0, sizeof( ivci ) );
	ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ivci.image = image;
	ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ivci.format = format;
	ivci.subresourceRange.aspectMask = aspect;
	ivci.subresourceRange.levelCount = 1;
	ivci.subresourceRange.layerCount = 1;
	if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &view ) != VK_SUCCESS ) {
		view = VK_NULL_HANDLE;
		return false;
	}
	return true;
}

static const char *const vkVRTargetLabels[3] = { "virtual screen", "left eye", "right eye" };

// Created once and recreated when the runtime's sizes or format change.
static bool VK_VR_EnsureTarget( int index, int width, int height, VkFormat format ) {
	vkVRTarget_t &target = vkVRTargets[index];
	if ( target.color != VK_NULL_HANDLE && target.width == width && target.height == height && target.format == format ) {
		return true;
	}
	VK_VR_DestroyTarget( target );
	const bool created =
		VK_VR_CreateImage( format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
				| VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
			width, height, target.color, target.colorView, target.colorAllocation )
		&& VK_VR_CreateImage( vkCtx.depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
			width, height, target.depth, target.depthView, target.depthAllocation );
	if ( !created ) {
		VK_VR_DestroyTarget( target );
		static bool warned = false;
		if ( !warned ) {
			common->Warning( "Vulkan: the VR %s target (%dx%d) could not be created", vkVRTargetLabels[index], width, height );
			warned = true;
		}
		return false;
	}
	target.width = width;
	target.height = height;
	target.format = format;
	rb_vrCleared[index] = false;
	if ( cvarSystem->GetCVarInteger( "vr_debug" ) > 0 ) {
		common->Printf( "Vulkan: VR %s target %dx%d, format %d\n", vkVRTargetLabels[index], width, height, (int)format );
	}
	return true;
}

bool R_VR_BackendOwnsTargets( void ) {
	return true;
}

idCVar r_vkVRWindowlessTest( "r_vkVRWindowlessTest", "0", CVAR_RENDERER | CVAR_BOOL,
	"diagnostic: Vulkan VR frames open without the window's swapchain image, as a minimized or occluded window makes them" );

/*
====================
VK_VR_FrameMayOpenWithoutWindow

The executor opens a frame on the window's swapchain image. A VR frame
renders into its own targets and the headset paces it, so when the window
cannot present (minimized, occluded, out of date) it goes on without the
image and loses only the mirror.
====================
*/
bool VK_VR_FrameMayOpenWithoutWindow( bool *forced ) {
	const bool vrFrame = tr.vrFrame.active && glConfig.isInitialized;
	if ( forced != NULL ) {
		*forced = vrFrame && r_vkVRWindowlessTest.GetBool();
	}
	return vrFrame;
}

/*
====================
R_VR_BackendPrepareTargets
====================
*/
bool R_VR_BackendPrepareTargets( renderVRFrame_t &frame ) {
	const VkFormat format = VK_VR_TargetFormat( frame.vkImageFormat );
	if ( format == VK_FORMAT_UNDEFINED || frame.vkScreenImage == 0 ) {
		static bool warned = false;
		if ( !warned ) {
			common->Warning( "Vulkan: VR swapchain format %d cannot be presented", frame.vkImageFormat );
			warned = true;
		}
		return false;
	}
	if ( !VK_VR_EnsureTarget( 0, frame.screenWidth, frame.screenHeight, format ) ) {
		return false;
	}
	if ( frame.stereo ) {
		const bool usable = frame.vkEyeImages[0] != 0 && frame.vkEyeImages[1] != 0
			&& VK_VR_EnsureTarget( 1, frame.eyeWidth, frame.eyeHeight, format )
			&& VK_VR_EnsureTarget( 2, frame.eyeWidth, frame.eyeHeight, format );
		if ( !usable ) {
			frame.stereo = false;
		}
	}
	return true;
}

/*
====================
R_VR_BackendReleaseImages

OpenXR may only destroy a swapchain once every submitted command that uses
its images has completed.
====================
*/
void R_VR_BackendReleaseImages( void ) {
	if ( VK_VR_DeviceUsable() ) {
		const VkResult result = vkQueueWaitIdle( vkCtx.graphicsQueue );
		if ( result != VK_SUCCESS ) {
			common->Warning( "Vulkan: waiting for the VR swapchain images failed (%d)", (int)result );
		}
	}
}

void R_VR_ShutdownTargets( void ) {
	for ( int i = 0; i < 3; i++ ) {
		VK_VR_DestroyTarget( vkVRTargets[i] );
	}
	rb_vrTarget = -2;
	memset( rb_vrCleared, 0, sizeof( rb_vrCleared ) );
}

/*
===============================================================================

	Backend

===============================================================================
*/

static vkExecDefaultOutput_t VK_VR_Output( int index ) {
	const vkVRTarget_t &target = vkVRTargets[index];
	vkExecDefaultOutput_t output;
	memset( &output, 0, sizeof( output ) );
	output.color = target.color;
	output.colorView = target.colorView;
	output.colorFormat = target.format;
	output.depth = target.depth;
	output.depthView = target.depthView;
	output.extent.width = (uint32_t)target.width;
	output.extent.height = (uint32_t)target.height;
	// the virtual screen is a layer over the world, composited with premultiplied alpha
	output.premultipliedAlpha = index == 0;
	output.vrTarget = index;
	return output;
}

static void VK_VR_ImageBarrier( VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
		VkImageLayout oldLayout, VkImageLayout newLayout,
		VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
		VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess ) {
	VkImageMemoryBarrier2 barrier;
	memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = srcStage;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dstStage;
	barrier.dstAccessMask = dstAccess;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;
	VkDependencyInfo dependency;
	memset( &dependency, 0, sizeof( dependency ) );
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2( cmd, &dependency );
}

// A target's first use in a frame discards what the last frame left (it is
// cleared next), and orders its writes after that frame's copies out of it.
static void VK_VR_StartTarget( VkCommandBuffer cmd, const vkVRTarget_t &target ) {
	VK_VR_ImageBarrier( cmd, target.color, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT );
	VK_VR_ImageBarrier( cmd, target.depth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT );
}

/*
====================
RB_VR_BeginBackEnd

Every frame starts on the window; RC_VR_TARGET moves a VR frame onto its
targets. A synchronous flush that splits this frame's commands keeps the
target it was on: the executor's frame, and its output, stay open across it.
====================
*/
void RB_VR_BeginBackEnd( void ) {
	if ( tr.vrFrame.active && rb_vrTarget != -2 && rb_vrFrameCount == tr.frameCount ) {
		return;
	}
	rb_vrTarget = -2;
	rb_vrFrameCount = tr.frameCount;
	memset( rb_vrCleared, 0, sizeof( rb_vrCleared ) );
}

/*
====================
RB_VR_SetTarget
====================
*/
void RB_VR_SetTarget( const void *data ) {
	const vrTargetCommand_t *cmd = (const vrTargetCommand_t *)data;
	const int index = cmd->target + 1;
	if ( !tr.vrFrame.active || index < 0 || index > 2 || cmd->width <= 0 || cmd->height <= 0 ) {
		return;
	}
	const vkVRTarget_t &target = vkVRTargets[index];
	if ( target.color == VK_NULL_HANDLE || target.width != cmd->width || target.height != cmd->height
			|| !VK_GuiExecutor_BeginFrame() ) {
		static bool warned = false;
		if ( !warned ) {
			common->Warning( "VR: render target %d is unusable; the frame stays on the previous target", cmd->target );
			warned = true;
		}
		return;
	}

	// an eye is finished when the frame leaves it
	if ( rb_vrTarget >= 0 && rb_vrTarget != cmd->target ) {
		VK_VR_FinishTarget();
	}

	const bool first = !rb_vrCleared[index];
	if ( first ) {
		// barriers record outside the rendering scope; the output switch resumes it
		VkCommandBuffer commands = VK_Exec_SuspendRendering();
		if ( commands == VK_NULL_HANDLE ) {
			return;
		}
		VK_VR_StartTarget( commands, target );
	}
	const vkExecDefaultOutput_t output = VK_VR_Output( index );
	if ( !VK_Exec_SetDefaultOutput( &output ) ) {
		return;
	}

	rb_vrTarget = cmd->target;
	glConfig.vidWidth = cmd->width;
	glConfig.vidHeight = cmd->height;
	R_TemporalPresentation_SetVRTargetState( cmd->width, cmd->height, cmd->temporalAA );
	backEnd.renderTexture = NULL;
	backEnd.feedbackRenderTexture = NULL;

	if ( first ) {
		// the virtual screen is a transparent layer over the world; an eye is opaque
		const float clearColor[4] = { 0.0f, 0.0f, 0.0f, index == 0 ? 0.0f : 1.0f };
		VK_Exec_ClearRenderTarget( true, true, 1.0f, clearColor );
		rb_vrCleared[index] = true;
	}
}

/*
====================
VK_VR_BindWindowStandIn

A windowless VR frame opens here. Work queued between frames runs ahead of
the frame's first RC_VR_TARGET, on the window: a wipe draws the screen and
captures it there (idSessionLocal::StartWipe), as it does under OpenGL. The
virtual screen stands in for the missing window, cleared as a fresh window
image is; the frame's own switch to the screen discards and clears it again,
so nothing drawn here reaches the headset.
====================
*/
bool VK_VR_BindWindowStandIn( void ) {
	const vkVRTarget_t &screen = vkVRTargets[0];
	if ( !tr.vrFrame.active || screen.color == VK_NULL_HANDLE ) {
		return false;
	}
	VkCommandBuffer commands = VK_Exec_SuspendRendering();
	if ( commands == VK_NULL_HANDLE ) {
		return false;
	}
	VK_VR_StartTarget( commands, screen );
	const vkExecDefaultOutput_t output = VK_VR_Output( 0 );
	if ( !VK_Exec_SetDefaultOutput( &output ) ) {
		return false;
	}
	const float opaqueBlack[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	VK_Exec_ClearRenderTarget( true, true, 1.0f, opaqueBlack );
	rb_vrCleared[0] = false;
	return true;
}

// Copies a finished target, byte for byte, into the runtime's image, which
// waits in COLOR_ATTACHMENT_OPTIMAL and is handed back in it.
static void VK_VR_CopyToImage( VkCommandBuffer cmd, const vkVRTarget_t &target, VkImage image ) {
	VK_VR_ImageBarrier( cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT );
	VkImageCopy region;
	memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = 1;
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.layerCount = 1;
	region.extent.width = (uint32_t)target.width;
	region.extent.height = (uint32_t)target.height;
	region.extent.depth = 1;
	vkCmdCopyImage( cmd, target.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
	VK_VR_ImageBarrier( cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT );
}

// Draws the left eye or the virtual screen into the window, cropped to the
// window's aspect so nothing is stretched. The source is a transfer source.
static void VK_VR_DrawMirror( VkCommandBuffer cmd, const vkVRTarget_t *source ) {
	const VkImage window = VK_Exec_SwapchainImage();
	const VkExtent2D extent = vkCtx.swapchainExtent;
	if ( window == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0 ) {
		return;
	}
	VK_VR_ImageBarrier( cmd, window, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT );
	VkClearColorValue black;
	memset( &black, 0, sizeof( black ) );
	black.float32[3] = 1.0f;
	VkImageSubresourceRange range;
	memset( &range, 0, sizeof( range ) );
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.levelCount = 1;
	range.layerCount = 1;
	vkCmdClearColorImage( cmd, window, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range );

	VkFormatProperties properties;
	memset( &properties, 0, sizeof( properties ) );
	vkGetPhysicalDeviceFormatProperties( vkCtx.physicalDevice, vkCtx.swapchainFormat, &properties );
	if ( source != NULL && source->color != VK_NULL_HANDLE
			&& ( properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT ) != 0 ) {
		// crop the source to the window aspect around its centre
		float s0 = 0.0f, t0 = 0.0f, s1 = 1.0f, t1 = 1.0f;
		const float windowAspect = (float)extent.width / (float)extent.height;
		const float sourceAspect = (float)source->width / (float)source->height;
		if ( sourceAspect > windowAspect ) {
			const float keep = windowAspect / sourceAspect;
			s0 = 0.5f - 0.5f * keep;
			s1 = 0.5f + 0.5f * keep;
		} else if ( sourceAspect < windowAspect ) {
			const float keep = sourceAspect / windowAspect;
			t0 = 0.5f - 0.5f * keep;
			t1 = 0.5f + 0.5f * keep;
		}
		VkImageBlit blit;
		memset( &blit, 0, sizeof( blit ) );
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = 1;
		blit.srcOffsets[0].x = (int32_t)( s0 * source->width );
		blit.srcOffsets[0].y = (int32_t)( t0 * source->height );
		blit.srcOffsets[1].x = Max( blit.srcOffsets[0].x + 1, (int32_t)( s1 * source->width ) );
		blit.srcOffsets[1].y = Max( blit.srcOffsets[0].y + 1, (int32_t)( t1 * source->height ) );
		blit.srcOffsets[1].z = 1;
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.layerCount = 1;
		blit.dstOffsets[1].x = (int32_t)extent.width;
		blit.dstOffsets[1].y = (int32_t)extent.height;
		blit.dstOffsets[1].z = 1;
		vkCmdBlitImage( cmd, source->color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			window, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR );
	}
	VK_VR_ImageBarrier( cmd, window, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT );
}

/*
====================
RB_VR_PresentFrame

Runs in place of the window's back-buffer passes at swap time. False when
the frame was not a VR frame.
====================
*/
bool RB_VR_PresentFrame( void ) {
	if ( !tr.vrFrame.active ) {
		return false;
	}
	if ( rb_vrTarget == -2 ) {
		// nothing reached the targets (no frame could open): submit no layers
		tr.vrFrameResult.presented = false;
		tr.vrFrameResult.eyeRendered[0] = tr.vrFrameResult.eyeRendered[1] = false;
		return false;
	}
	if ( tr.takingScreenshot ) {
		// a capture flush that is not presented: the frame goes on drawing into
		// its targets and presents at its own swap
		return true;
	}

	// EndFrame returned the frame to the virtual screen; finish it like a back buffer
	if ( rb_vrTarget != -1 ) {
		VK_VR_FinishTarget();
		const vkExecDefaultOutput_t screen = VK_VR_Output( 0 );
		VK_Exec_SetDefaultOutput( &screen );
		rb_vrTarget = -1;
	}
	VK_VR_FinishTarget();
	VK_Exec_SetDefaultOutput( NULL );
	// the mirror below shows finished targets: no second mapping at present
	VK_Exec_MarkWindowColorMapped();
	rb_vrTarget = -2;
	glConfig.vidWidth = tr.vrWindowWidth;
	glConfig.vidHeight = tr.vrWindowHeight;
	backEnd.renderTexture = NULL;
	backEnd.feedbackRenderTexture = NULL;

	VkCommandBuffer cmd = VK_Exec_SuspendRendering();
	if ( cmd == VK_NULL_HANDLE ) {
		tr.vrFrameResult.presented = false;
		tr.vrFrameResult.eyeRendered[0] = tr.vrFrameResult.eyeRendered[1] = false;
		return true;
	}

	// every target the frame drew becomes a transfer source
	bool used[3];
	used[0] = rb_vrCleared[0];
	for ( int eye = 0; eye < 2; eye++ ) {
		used[eye + 1] = tr.vrFrameResult.eyeRendered[eye] && rb_vrCleared[eye + 1];
	}
	for ( int i = 0; i < 3; i++ ) {
		if ( used[i] ) {
			VK_VR_ImageBarrier( cmd, vkVRTargets[i].color, VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT );
		}
	}

	if ( used[0] ) {
		VK_VR_CopyToImage( cmd, vkVRTargets[0], (VkImage)(uintptr_t)tr.vrFrame.vkScreenImage );
	} else {
		tr.vrFrameResult.presented = false;
	}
	for ( int eye = 0; eye < 2; eye++ ) {
		if ( used[eye + 1] ) {
			VK_VR_CopyToImage( cmd, vkVRTargets[eye + 1], (VkImage)(uintptr_t)tr.vrFrame.vkEyeImages[eye] );
		} else {
			tr.vrFrameResult.eyeRendered[eye] = false;
		}
	}

	const vkVRTarget_t *mirror = NULL;
	if ( tr.vrFrame.mirror == RENDER_VR_MIRROR_EYE && used[1] ) {
		mirror = &vkVRTargets[1];
	} else if ( tr.vrFrame.mirror != RENDER_VR_MIRROR_NONE && used[0] ) {
		mirror = &vkVRTargets[0];
	}
	VK_VR_DrawMirror( cmd, mirror );

	// back to the layout every later use of the targets starts from
	for ( int i = 0; i < 3; i++ ) {
		if ( used[i] ) {
			VK_VR_ImageBarrier( cmd, vkVRTargets[i].color, VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT );
		}
	}
	return true;
}

/*
====================
GL default-target seam

The shared front end and the GL backend name the window through these; the
Vulkan executor redirects its own default output instead (above).
====================
*/
void R_SetDefaultRenderTarget( idRenderTexture *target ) {
	(void)target;
}

idRenderTexture *R_GetDefaultRenderTarget( void ) {
	return NULL;
}

unsigned int R_DefaultFramebufferHandle( void ) {
	return 0;
}

unsigned int R_DefaultColorBuffer( void ) {
	return GL_BACK;
}

void R_SetDefaultDrawAndReadBuffers( void ) {
}

void R_SetDefaultRenderTargetPremultiplied( bool premultiplied ) {
	(void)premultiplied;
}

bool R_PremultipliedDefaultTargetBound( void ) {
	return false;
}

#endif // OPENQ4_RENDERER_VK_MODULE
