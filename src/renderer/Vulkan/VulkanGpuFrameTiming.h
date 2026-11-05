// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __VULKAN_GPU_FRAME_TIMING_H__
#define __VULKAN_GPU_FRAME_TIMING_H__

#include "VulkanDevice.h"

bool VK_GpuFrameTiming_Available( void );
void VK_GpuFrameTiming_BeginFrame( VkCommandBuffer commandBuffer, int frameSlot,
	int frameNumber );
void VK_GpuFrameTiming_EndFrame( VkCommandBuffer commandBuffer, int frameSlot );
void VK_GpuFrameTiming_SubmitFailed( int frameSlot );
void VK_GpuFrameTiming_Shutdown( void );
bool VK_GpuFrameTiming_RunSelfTest( void );

// Optional bounded diagnostic. Query ranges are reset outside rendering at the
// normal frame boundary and read only after its existing retirement fence.
enum vkGpuPhase_t {
	VK_GPU_PHASE_SSAO_DEPTH, VK_GPU_PHASE_SSAO_COLOR, VK_GPU_PHASE_SSAO_DRAW,
	VK_GPU_PHASE_MOTION, VK_GPU_PHASE_BLOOM,
	VK_GPU_PHASE_WORLD_DEPTH, VK_GPU_PHASE_WORLD_LIGHTS,
	VK_GPU_PHASE_WORLD_SHADOW_MAPS,
	VK_GPU_PHASE_WORLD_AMBIENT_PRE, VK_GPU_PHASE_WORLD_COMPOSITION,
	VK_GPU_PHASE_WORLD_FOG, VK_GPU_PHASE_WORLD_AMBIENT_POST, VK_GPU_PHASE_WORLD_MATERIAL_POST,
	VK_GPU_PHASE_SMAA_EDGE, VK_GPU_PHASE_SMAA_WEIGHTS, VK_GPU_PHASE_SMAA_BLEND,
	VK_GPU_PHASE_WORLD_VIEW, VK_GPU_PHASE_PORTAL_VIEW, VK_GPU_PHASE_TARGET_RESOLVE,
	VK_GPU_PHASE_SHADOW_PROJECTED, VK_GPU_PHASE_SHADOW_CACHE, VK_GPU_PHASE_SHADOW_COMPOSE,
	VK_GPU_PHASE_SHADOW_POINT,
	VK_GPU_PHASE_SSAO_SNAPSHOT,		// the prepass world-depth copy SSAO samples
	VK_GPU_PHASE_COUNT
};
bool VK_GpuPhaseTiming_Begin( VkCommandBuffer commandBuffer, vkGpuPhase_t phase );
void VK_GpuPhaseTiming_End( VkCommandBuffer commandBuffer, vkGpuPhase_t phase );

// One selected projected-atlas or cascade span per profiled main view.
// Begin/end stay in the same rendering scope; counters retire with the existing
// frame-slot fence, with no additional wait.
bool VK_GpuProjectedShadowStatistics_Begin( VkCommandBuffer commandBuffer, int scope = 0, int light = -1, int orthographic = -1 );
void VK_GpuProjectedShadowStatistics_End( VkCommandBuffer commandBuffer );

class vkGpuProjectedShadowStatisticsScope {
public:
	vkGpuProjectedShadowStatisticsScope( VkCommandBuffer buffer, bool enabled, int scope = 0, int light = -1, int orthographic = -1 )
		: buffer( buffer ), active( enabled && VK_GpuProjectedShadowStatistics_Begin( buffer, scope, light, orthographic ) ) {}
	~vkGpuProjectedShadowStatisticsScope() { if ( active ) VK_GpuProjectedShadowStatistics_End( buffer ); }
	vkGpuProjectedShadowStatisticsScope( const vkGpuProjectedShadowStatisticsScope& ) = delete;
	vkGpuProjectedShadowStatisticsScope& operator=( const vkGpuProjectedShadowStatisticsScope& ) = delete;
private:
	VkCommandBuffer buffer;
	bool active;
};

class vkGpuPhaseScope {
public:
	vkGpuPhaseScope( VkCommandBuffer buffer, vkGpuPhase_t phase, bool enabled = true )
		: buffer( buffer ), phase( phase ), active( enabled && VK_GpuPhaseTiming_Begin( buffer, phase ) ) {}
	~vkGpuPhaseScope() { End(); }
	void End() {
		if ( active ) {
			VK_GpuPhaseTiming_End( buffer, phase );
			active = false;
		}
	}
	vkGpuPhaseScope( const vkGpuPhaseScope& ) = delete;
	vkGpuPhaseScope& operator=( const vkGpuPhaseScope& ) = delete;
private:
	VkCommandBuffer buffer;
	vkGpuPhase_t phase;
	bool active;
};

#endif /* !__VULKAN_GPU_FRAME_TIMING_H__ */
