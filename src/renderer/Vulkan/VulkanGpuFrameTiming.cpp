// Copyright (C) 2026 DarkMatter Productions
//

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../GpuFrameTimingCore.h"
#include "../RendererMetrics.h"
#include "VulkanGpuFrameTiming.h"

static idCVar r_vkGpuPhaseProfile( "r_vkGpuPhaseProfile", "0", CVAR_RENDERER | CVAR_INTEGER,
	"capture up to 4096 Vulkan GPU scene/post-processing frames to diagnostics/gpu-phases.csv; 0 stops", 0, 4096 );
static idCVar r_vkGpuProjectedStatisticsScope( "r_vkGpuProjectedStatisticsScope", "0", CVAR_RENDERER | CVAR_INTEGER,
	"pipeline counter scope: 0 all projected maps, 1-4 first LOCAL cascade, 5-8 first GLOBAL cascade; scoped CSV suffix", 0, 8 );
static const int VK_GPU_QUERIES_PER_FRAME = 2 + VK_GPU_PHASE_COUNT * 2;
static const char *vkGpuPhaseNames[] = { "ssaoDepth", "ssaoColor", "ssaoDraw", "motion", "bloom", "worldDepth", "worldLights", "worldShadowMaps",
	"worldAmbientPre", "worldComposition", "worldFog", "worldAmbientPost", "worldMaterialPost", "smaaEdge", "smaaWeights", "smaaBlend",
	"worldView", "portalView", "targetResolve", "shadowProjected", "shadowCache", "shadowCompose", "shadowPoint",
	"ssaoSnapshot" };
static_assert( sizeof( vkGpuPhaseNames ) / sizeof( vkGpuPhaseNames[0] ) == VK_GPU_PHASE_COUNT, "GPU phase names must match query pairs" );
static_assert( VK_GPU_PHASE_COUNT <= 32, "GPU phase masks must contain every query pair" );
// Result order follows increasing VkQueryPipelineStatisticFlagBits values.
static const VkQueryPipelineStatisticFlags vkGpuPipelineCounters =
	VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
	VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
	VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
	VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
	VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
	VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
static const char *vkGpuPipelineNames[] = { "projectedVertices", "projectedPrimitives", "projectedVertexInvocations",
	"projectedClipInvocations", "projectedClipPrimitives", "projectedFragmentInvocations" };
static constexpr int VK_GPU_PIPELINE_COUNTER_COUNT = sizeof( vkGpuPipelineNames ) / sizeof( vkGpuPipelineNames[0] );
struct vkGpuPhaseSample_t {
	int frame;
	unsigned mask;
	std::uint64_t total;
	std::uint64_t phases[VK_GPU_PHASE_COUNT];
	bool pipelineValid;
	int pipelineScope, pipelineLight, pipelineOrthographic;
	std::uint64_t pipeline[VK_GPU_PIPELINE_COUNTER_COUNT];
};
static std::vector<vkGpuPhaseSample_t> vkGpuPhaseSamples;
static int vkGpuPhaseRequested = 0;
static int vkGpuPhaseActiveSlot = -1;
static int vkGpuPipelineScopeRequested = 0;

typedef struct vkGpuFrameTimingSlot_s {
	bool			open;
	bool			pending;
	int				frameNumber;
	unsigned int	generation;
	unsigned int	phaseOpen;
	unsigned int	phaseComplete;
	bool			profile;
	bool			pipelineOpen;
	bool			pipelineComplete;
	int				pipelineScope, pipelineLight, pipelineOrthographic;
} vkGpuFrameTimingSlot_t;

static VkQueryPool vkGpuTimingQueryPool = VK_NULL_HANDLE;
static VkQueryPool vkGpuPipelineQueryPool = VK_NULL_HANDLE;
static VkDevice vkGpuTimingOwnerDevice = VK_NULL_HANDLE;
static vkGpuFrameTimingSlot_t vkGpuTimingSlots[ VK_FRAMES_IN_FLIGHT ];
static bool vkGpuTimingEnableStateKnown = false;
static bool vkGpuTimingEnabledLastFrame = false;

bool VK_GpuFrameTiming_Available( void ) {
	return vkCtx.initialized
		&& vkCtx.device != VK_NULL_HANDLE
		&& vkCtx.graphicsTimestampValidBits > 0
		&& vkCtx.deviceProperties.limits.timestampPeriod > 0.0f
		&& vkCreateQueryPool != NULL
		&& vkDestroyQueryPool != NULL
		&& vkGetQueryPoolResults != NULL
		&& vkCmdResetQueryPool != NULL
		&& vkCmdWriteTimestamp != NULL;
}

static bool VK_GpuFrameTiming_EnsureQueryPool( void ) {
	if ( vkGpuTimingQueryPool != VK_NULL_HANDLE && vkGpuTimingOwnerDevice == vkCtx.device ) {
		return true;
	}
	if ( vkGpuTimingOwnerDevice != VK_NULL_HANDLE && vkGpuTimingOwnerDevice != vkCtx.device ) {
		// The old device is already gone. Its non-dispatchable query-pool handle
		// must not be presented to the replacement device.
		vkGpuTimingQueryPool = VK_NULL_HANDLE;
		vkGpuPipelineQueryPool = VK_NULL_HANDLE;
		memset( vkGpuTimingSlots, 0, sizeof( vkGpuTimingSlots ) );
		R_RendererMetrics_ResetGpuFrameTiming( "Vulkan device generation changed" );
	}

	VkQueryPoolCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	createInfo.queryCount = VK_FRAMES_IN_FLIGHT * VK_GPU_QUERIES_PER_FRAME;
	if ( vkCreateQueryPool( vkCtx.device, &createInfo, NULL, &vkGpuTimingQueryPool ) != VK_SUCCESS ) {
		vkGpuTimingQueryPool = VK_NULL_HANDLE;
		vkGpuTimingOwnerDevice = VK_NULL_HANDLE;
		return false;
	}
	vkGpuTimingOwnerDevice = vkCtx.device;
	return true;
}

static bool VK_GpuFrameTiming_EnsurePipelineQueryPool() {
	if ( !vkCtx.pipelineStatisticsEnabled || !vkCmdBeginQuery || !vkCmdEndQuery ) return false;
	if ( vkGpuPipelineQueryPool != VK_NULL_HANDLE ) return true;
	VkQueryPoolCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
	info.queryCount = VK_FRAMES_IN_FLIGHT;
	info.pipelineStatistics = vkGpuPipelineCounters;
	if ( vkCreateQueryPool( vkCtx.device, &info, NULL, &vkGpuPipelineQueryPool ) != VK_SUCCESS ) {
		vkGpuPipelineQueryPool = VK_NULL_HANDLE;
		return false;
	}
	return true;
}

static void VK_GpuFrameTiming_ResolveRetiredSlot( int frameSlot, int currentFrameNumber ) {
	vkGpuFrameTimingSlot_t &slot = vkGpuTimingSlots[ frameSlot ];
	if ( !slot.pending ) {
		return;
	}

	std::uint64_t timestamps[ 2 ] = { 0, 0 };
	const VkResult result = vkGetQueryPoolResults( vkCtx.device, vkGpuTimingQueryPool,
		static_cast<std::uint32_t>( frameSlot * VK_GPU_QUERIES_PER_FRAME ), 2, sizeof( timestamps ), timestamps,
		sizeof( timestamps[ 0 ] ), VK_QUERY_RESULT_64_BIT );
	// Deliberately no VK_QUERY_RESULT_WAIT_BIT: this function is reached only
	// after the normal frame-slot fence has retired, but readiness is still
	// treated as fallible and never escalated into an extra wait.
	if ( result == VK_NOT_READY ) {
		R_RendererMetrics_RecordGpuFrameTimingUnavailable();
		R_RendererMetrics_RecordGpuFrameTimingDropped();
		slot.pending = false;
		return;
	}
	if ( result != VK_SUCCESS ) {
		R_RendererMetrics_RecordGpuFrameTimingDropped();
		slot.pending = false;
		return;
	}

	const std::uint64_t ticks = GpuFrameTimingCore_TimestampDelta(
		timestamps[ 0 ], timestamps[ 1 ], vkCtx.graphicsTimestampValidBits );
	const std::uint64_t elapsedMicroseconds = GpuFrameTimingCore_TicksToMicroseconds(
		ticks, static_cast<double>( vkCtx.deviceProperties.limits.timestampPeriod ) );
	R_RendererMetrics_RecordGpuFrameTimingResolved( RENDER_GPU_TIMING_BACKEND_VULKAN,
		slot.frameNumber, slot.generation, elapsedMicroseconds, currentFrameNumber );
	if ( slot.profile && vkGpuPhaseRequested > 0 && r_vkGpuPhaseProfile.GetInteger() == vkGpuPhaseRequested
			&& slot.generation == R_RendererMetrics_GpuFrameTimingGeneration() ) {
		vkGpuPhaseSample_t sample = {};
		sample.frame = slot.frameNumber;
		sample.total = elapsedMicroseconds;
		sample.pipelineScope = slot.pipelineScope;
		sample.pipelineLight = slot.pipelineLight;
		sample.pipelineOrthographic = slot.pipelineOrthographic;
		for ( int phase = 0; phase < VK_GPU_PHASE_COUNT; ++phase ) {
			if ( !(slot.phaseComplete & (1u << phase)) ) continue;
			std::uint64_t pair[2] = {};
			if ( vkGetQueryPoolResults( vkCtx.device, vkGpuTimingQueryPool,
				frameSlot * VK_GPU_QUERIES_PER_FRAME + 2 + phase * 2, 2, sizeof(pair), pair,
				sizeof(pair[0]), VK_QUERY_RESULT_64_BIT ) != VK_SUCCESS ) continue;
			sample.mask |= 1u << phase;
			sample.phases[phase] = GpuFrameTimingCore_TicksToMicroseconds(
				GpuFrameTimingCore_TimestampDelta( pair[0], pair[1], vkCtx.graphicsTimestampValidBits ),
				vkCtx.deviceProperties.limits.timestampPeriod );
		}
		if ( slot.pipelineComplete && vkGpuPipelineQueryPool != VK_NULL_HANDLE ) {
			sample.pipelineValid = vkGetQueryPoolResults( vkCtx.device, vkGpuPipelineQueryPool,
				frameSlot, 1, sizeof( sample.pipeline ), sample.pipeline, sizeof( sample.pipeline ),
				VK_QUERY_RESULT_64_BIT ) == VK_SUCCESS;
			if ( !sample.pipelineValid ) memset( sample.pipeline, 0, sizeof( sample.pipeline ) );
		}
		vkGpuPhaseSamples.push_back(sample);
		if ( static_cast<int>(vkGpuPhaseSamples.size()) == vkGpuPhaseRequested ) {
			const idStr profilePath = vkGpuPipelineScopeRequested == 0 ? "diagnostics/gpu-phases.csv" :
				va( "diagnostics/gpu-phases-scope%d.csv", vkGpuPipelineScopeRequested );
			idFile *file = fileSystem->OpenFileWrite( profilePath.c_str() );
			if (file) {
				file->Printf( "frame,mask,totalUs" );
				for ( const char *name : vkGpuPhaseNames ) file->Printf( ",%sUs", name );
				file->Printf( ",projectedPipelineValid,projectedScope,projectedLight,projectedOrthographic" );
				for ( const char *name : vkGpuPipelineNames ) file->Printf( ",%s", name );
				file->Printf( "\n" );
				for (const auto& value:vkGpuPhaseSamples) {
					file->Printf("%d,%u,%llu",value.frame,value.mask,static_cast<unsigned long long>(value.total));
					for (auto duration:value.phases) file->Printf(",%llu",static_cast<unsigned long long>(duration));
					file->Printf( ",%d,%d,%d,%d", value.pipelineValid ? 1 : 0,
						value.pipelineScope, value.pipelineLight, value.pipelineOrthographic );
					for ( auto counter : value.pipeline ) file->Printf( ",%llu", static_cast<unsigned long long>( counter ) );
					file->Printf("\n");
				}
				fileSystem->CloseFile(file);
			}
			for (int phase = 0; phase < VK_GPU_PHASE_COUNT; ++phase) {
				std::uint64_t total = 0, maximum = 0; int count = 0;
				for (const auto& value:vkGpuPhaseSamples) if (value.mask & (1u << phase)) {
					total += value.phases[phase]; maximum = Max(maximum,value.phases[phase]); ++count;
				}
				common->Printf("Vulkan GPU phase=%s samples=%d mean_us=%.3f max_us=%llu\n",vkGpuPhaseNames[phase],count,
					count ? double(total)/count : 0.0,static_cast<unsigned long long>(maximum));
			}
			common->Printf("Vulkan GPU phase trace: %s\n",file ? profilePath.c_str() : "write failed");
			r_vkGpuPhaseProfile.SetInteger(0);
		}
	}
	slot.pending = false;
}

void VK_GpuFrameTiming_BeginFrame( VkCommandBuffer commandBuffer, int frameSlot,
		int frameNumber ) {
	const bool supported = VK_GpuFrameTiming_Available();
	vkGpuPhaseActiveSlot = -1;
	const int request = r_vkGpuPhaseProfile.GetInteger();
	const int pipelineScope = r_vkGpuProjectedStatisticsScope.GetInteger();
	if ( request != vkGpuPhaseRequested || pipelineScope != vkGpuPipelineScopeRequested ) {
		vkGpuPhaseRequested = request;
		vkGpuPipelineScopeRequested = pipelineScope;
		vkGpuPhaseSamples.clear();
		if (request > 0) vkGpuPhaseSamples.reserve(request);
		// Already submitted queries belong to the previous request.
		for (auto& slot:vkGpuTimingSlots) slot.profile = false;
	}
	R_RendererMetrics_SetGpuFrameTimingBackend( RENDER_GPU_TIMING_BACKEND_VULKAN, supported );
	const bool enabled = supported
		&& ( r_rendererGpuTimers.GetBool()
			|| r_rendererDynamicResolution.GetBool() || request > 0 );
	if ( vkGpuTimingEnableStateKnown && enabled != vkGpuTimingEnabledLastFrame ) {
		R_RendererMetrics_ResetGpuFrameTiming( enabled
			? "Vulkan whole-frame timing enabled" : "Vulkan whole-frame timing disabled" );
	}
	vkGpuTimingEnableStateKnown = true;
	vkGpuTimingEnabledLastFrame = enabled;

	if ( !enabled || commandBuffer == VK_NULL_HANDLE || frameSlot < 0
			|| frameSlot >= VK_FRAMES_IN_FLIGHT ) {
		return;
	}
	if ( !VK_GpuFrameTiming_EnsureQueryPool() ) {
		R_RendererMetrics_RecordGpuFrameTimingDropped();
		return;
	}

	VK_GpuFrameTiming_ResolveRetiredSlot( frameSlot, frameNumber );
	vkGpuFrameTimingSlot_t &slot = vkGpuTimingSlots[ frameSlot ];
	const std::uint32_t firstQuery = static_cast<std::uint32_t>( frameSlot * VK_GPU_QUERIES_PER_FRAME );
	vkCmdResetQueryPool( commandBuffer, vkGpuTimingQueryPool, firstQuery, VK_GPU_QUERIES_PER_FRAME );
	vkCmdWriteTimestamp( commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		vkGpuTimingQueryPool, firstQuery );
	slot.open = true;
	slot.pending = false;
	slot.frameNumber = frameNumber;
	slot.generation = R_RendererMetrics_GpuFrameTimingGeneration();
	slot.phaseOpen = slot.phaseComplete = 0;
	slot.pipelineOpen = slot.pipelineComplete = false;
	slot.pipelineScope = pipelineScope;
	slot.pipelineLight = slot.pipelineOrthographic = -1;
	slot.profile = r_vkGpuPhaseProfile.GetInteger() > 0;
	if ( slot.profile && VK_GpuFrameTiming_EnsurePipelineQueryPool() ) {
		vkCmdResetQueryPool( commandBuffer, vkGpuPipelineQueryPool, frameSlot, 1 );
	}
	vkGpuPhaseActiveSlot = frameSlot;
}

bool VK_GpuProjectedShadowStatistics_Begin( VkCommandBuffer commandBuffer, int scope, int light, int orthographic ) {
	if ( vkGpuPhaseActiveSlot < 0 || commandBuffer == VK_NULL_HANDLE || vkGpuPipelineQueryPool == VK_NULL_HANDLE ) return false;
	if ( scope != vkGpuPipelineScopeRequested ) return false;
	auto& slot = vkGpuTimingSlots[vkGpuPhaseActiveSlot];
	if ( !slot.open || !slot.profile || slot.pipelineOpen || slot.pipelineComplete ) return false;
	vkCmdBeginQuery( commandBuffer, vkGpuPipelineQueryPool, vkGpuPhaseActiveSlot, 0 );
	slot.pipelineOpen = true;
	slot.pipelineScope = scope;
	slot.pipelineLight = light;
	slot.pipelineOrthographic = orthographic;
	return true;
}

void VK_GpuProjectedShadowStatistics_End( VkCommandBuffer commandBuffer ) {
	if ( vkGpuPhaseActiveSlot < 0 || commandBuffer == VK_NULL_HANDLE ) return;
	auto& slot = vkGpuTimingSlots[vkGpuPhaseActiveSlot];
	if ( !slot.open || !slot.pipelineOpen ) return;
	vkCmdEndQuery( commandBuffer, vkGpuPipelineQueryPool, vkGpuPhaseActiveSlot );
	slot.pipelineOpen = false;
	slot.pipelineComplete = true;
}

bool VK_GpuPhaseTiming_Begin( VkCommandBuffer commandBuffer, vkGpuPhase_t phase ) {
	if (vkGpuPhaseActiveSlot < 0 || commandBuffer == VK_NULL_HANDLE || phase < 0 || phase >= VK_GPU_PHASE_COUNT) return false;
	auto& slot = vkGpuTimingSlots[vkGpuPhaseActiveSlot];
	const unsigned bit = 1u << phase;
	if (!slot.open || !slot.profile || ((slot.phaseOpen | slot.phaseComplete) & bit)) return false;
	vkCmdWriteTimestamp(commandBuffer,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,vkGpuTimingQueryPool,
		vkGpuPhaseActiveSlot * VK_GPU_QUERIES_PER_FRAME + 2 + phase * 2);
	slot.phaseOpen |= bit;
	return true;
}

void VK_GpuPhaseTiming_End( VkCommandBuffer commandBuffer, vkGpuPhase_t phase ) {
	if (vkGpuPhaseActiveSlot < 0 || commandBuffer == VK_NULL_HANDLE || phase < 0 || phase >= VK_GPU_PHASE_COUNT) return;
	auto& slot = vkGpuTimingSlots[vkGpuPhaseActiveSlot];
	const unsigned bit = 1u << phase;
	if (!slot.open || !(slot.phaseOpen & bit)) return;
	vkCmdWriteTimestamp(commandBuffer,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,vkGpuTimingQueryPool,
		vkGpuPhaseActiveSlot * VK_GPU_QUERIES_PER_FRAME + 3 + phase * 2);
	slot.phaseOpen &= ~bit;
	slot.phaseComplete |= bit;
}

void VK_GpuFrameTiming_EndFrame( VkCommandBuffer commandBuffer, int frameSlot ) {
	if ( vkGpuTimingQueryPool == VK_NULL_HANDLE || commandBuffer == VK_NULL_HANDLE
			|| frameSlot < 0 || frameSlot >= VK_FRAMES_IN_FLIGHT ) {
		return;
	}
	vkGpuFrameTimingSlot_t &slot = vkGpuTimingSlots[ frameSlot ];
	if ( !slot.open ) {
		return;
	}
	vkCmdWriteTimestamp( commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		vkGpuTimingQueryPool, static_cast<std::uint32_t>( frameSlot * VK_GPU_QUERIES_PER_FRAME + 1 ) );
	slot.open = false;
	slot.pending = true;
}

void VK_GpuFrameTiming_SubmitFailed( int frameSlot ) {
	if ( frameSlot < 0 || frameSlot >= VK_FRAMES_IN_FLIGHT ) {
		return;
	}
	vkGpuFrameTimingSlot_t &slot = vkGpuTimingSlots[ frameSlot ];
	if ( slot.open || slot.pending ) {
		R_RendererMetrics_RecordGpuFrameTimingDropped();
	}
	slot.open = false;
	slot.pending = false;
}

void VK_GpuFrameTiming_Shutdown( void ) {
	if ( vkGpuTimingQueryPool != VK_NULL_HANDLE && vkGpuTimingOwnerDevice != VK_NULL_HANDLE ) {
		// Shutdown/vid_restart already accepts device retirement. Keeping the wait
		// here prevents destroying a pool referenced by an in-flight command
		// buffer; normal frame sampling never calls a device/queue wait.
		vkDeviceWaitIdle( vkGpuTimingOwnerDevice );
		if ( vkGpuPipelineQueryPool != VK_NULL_HANDLE ) vkDestroyQueryPool( vkGpuTimingOwnerDevice, vkGpuPipelineQueryPool, NULL );
		vkDestroyQueryPool( vkGpuTimingOwnerDevice, vkGpuTimingQueryPool, NULL );
	}
	vkGpuTimingQueryPool = VK_NULL_HANDLE;
	vkGpuPipelineQueryPool = VK_NULL_HANDLE;
	vkGpuTimingOwnerDevice = VK_NULL_HANDLE;
	memset( vkGpuTimingSlots, 0, sizeof( vkGpuTimingSlots ) );
	vkGpuTimingEnableStateKnown = false;
	vkGpuTimingEnabledLastFrame = false;
	vkGpuPhaseActiveSlot = -1;
	vkGpuPhaseRequested = 0;
	vkGpuPipelineScopeRequested = 0;
	vkGpuPhaseSamples.clear();
}

bool VK_GpuFrameTiming_RunSelfTest( void ) {
	if ( !VK_GpuFrameTiming_Available() ) {
		common->Printf( "RendererGpuTimer self-test skipped: Vulkan timestamps are unavailable\n" );
		return true;
	}
	VkQueryPoolCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	createInfo.queryCount = 2;
	VkQueryPool testPool = VK_NULL_HANDLE;
	if ( vkCreateQueryPool( vkCtx.device, &createInfo, NULL, &testPool ) != VK_SUCCESS ) {
		common->Printf( "RendererGpuTimer self-test failed: Vulkan query-pool creation failed\n" );
		return false;
	}
	vkDestroyQueryPool( vkCtx.device, testPool, NULL );
	common->Printf( "RendererGpuTimer self-test passed (Vulkan timestampPeriod=%.6fns validBits=%u waits=0)\n",
		vkCtx.deviceProperties.limits.timestampPeriod, vkCtx.graphicsTimestampValidBits );
	return true;
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
