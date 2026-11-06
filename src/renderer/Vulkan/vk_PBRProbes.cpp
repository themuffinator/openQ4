// Copyright (C) 2026 DarkMatter Productions
#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop
#include "../tr_local.h"
#include "../ModernClusteredLighting.h"
#include "../PBREnvironment.h"
#include "vk_PBRProbes.h"
#include "vk_ProbeSource.h"
#include "vk_Image.h"
#include "vk_ExecutorHooks.h"
#include "VulkanDevice.h"
#include "../RendererMetrics.h"

#undef snprintf
#undef vsnprintf
#ifndef INT_MAX
#define INT_MAX 2147483647
#endif
#ifndef INT_MIN
#define INT_MIN ( -2147483647 - 1 )
#endif
#ifndef UINT_MAX
#define UINT_MAX 0xffffffffu
#endif
#include <cstdio>
#include "vk_mem_alloc.h"

static idCVar r_vkPBRProbeFailure( "r_vkPBRProbeFailure", "0", CVAR_RENDERER | CVAR_INTEGER,
	"native probe admission test: 1 source, 2 storage buffer, 3 descriptor", 0, 3 );

// The image manager owns this generated image and reruns its generator after
// image/video reload. Ordinary stock views never request its allocation.
static void VK_PBR_GenerateEnvironment( idImage *image ) {
	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA16F;
	opts.width = opts.height = MODERN_SPECULAR_PROBE_ATLAS_SIZE;
	opts.numLevels = MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP + 1;
	opts.isPersistant = true;
	image->AllocImage( opts, TF_LINEAR, TR_CLAMP );
	if ( !image->IsLoaded() ) {
		return;
	}
	openq4PBR::Cube analytic;
	analytic.size = 64;
	const openq4PBR::AnalyticEnvironment sample;
	for ( int face = 0; face < 6; ++face ) {
		analytic.faces[face].resize( analytic.size * analytic.size );
		for ( int y = 0; y < analytic.size; ++y ) {
			for ( int x = 0; x < analytic.size; ++x ) {
				analytic.faces[face][y * analytic.size + x] = sample( openq4PBR::FaceDirection(
					face, 2.0f * ( x + 0.5f ) / analytic.size - 1.0f,
					2.0f * ( y + 0.5f ) / analytic.size - 1.0f ) );
			}
		}
	}
	for ( int level = 0; level < opts.numLevels; ++level ) {
		const int size = opts.width >> level;
		const int faceSize = MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE >> level;
		std::vector<std::uint16_t> pixels( size * size * 4, 0 );
		const auto tile = [&]( int cell, int tileSize, const std::vector<float> &rgba ) {
			const int originX = ( cell % 8 ) * faceSize, originY = ( cell / 8 ) * faceSize;
			for ( int y = 0; y < tileSize; ++y ) {
				for ( int x = 0; x < tileSize; ++x ) {
					for ( int c = 0; c < 4; ++c ) {
						pixels[(( originY + y ) * size + originX + x ) * 4 + c] =
							openq4PBR::RadianceHalf( rgba[( y * tileSize + x ) * 4 + c] );
					}
				}
			}
		};
		for ( int face = 0; face < 6; ++face ) {
			tile( MODERN_SPECULAR_PROBE_ANALYTIC_SLOT * 6 + face, faceSize,
				openq4PBR::PrefilterFace( analytic, face, faceSize,
					float( level ) / MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP ) );
		}
		if ( level == 0 ) {
			tile( MODERN_SPECULAR_PROBE_DIFFUSE_FIRST_CELL + MODERN_SPECULAR_PROBE_ANALYTIC_SLOT,
				MODERN_SPECULAR_PROBE_DIFFUSE_SIZE,
				openq4PBR::DiffuseIrradiance( analytic, MODERN_SPECULAR_PROBE_DIFFUSE_SIZE ) );
			tile( MODERN_SPECULAR_PROBE_BRDF_CELL, MODERN_SPECULAR_PROBE_BRDF_SIZE,
				openq4PBR::BRDFTable( MODERN_SPECULAR_PROBE_BRDF_SIZE ) );
		}
		image->SubImageUpload( level, 0, 0, 0, size, size, pixels.data() );
		const vkImageEntry_t *entry = VK_Image_GetEntry( image->GetDeviceHandle() );
		if ( entry == NULL || !entry->lastUploadSucceeded ) {
			// A failed mip cannot leave a partially usable environment behind.
			// A later explicit image reload may retry the complete generator.
			image->PurgeImage();
			return;
		}
	}
	// The shaders select prefiltered levels with explicit LODs. The image's
	// TF_LINEAR sampler would otherwise clamp them all to the mirror level.
	if ( !VK_Image_UseExplicitMipChain( image->GetDeviceHandle() ) ) {
		image->PurgeImage();
		return;
	}
	common->Printf( "Vulkan: native PBR filtered environment generated (7 mips, diffuse irradiance, BRDF LUT)\n" );
}

struct vkProbeResident_t {
	const idImage *source;
	vkProbeSourceStamp_t stamp;
	std::uint64_t lastUsedFrame;
	std::uint64_t residency;
	bool valid;
};

// std430: seven vec4 header lanes, 32 seven-vec4 records, then uint pairs.
struct vkProbeGpuHeader_t {
	float grid[4], depth[4], viewOrigin[4], worldToView[3][4], projection[4];
	rendererSpecularProbeRecord_t records[RENDERER_CLUSTER_SPECULAR_PROBE_MAX_RECORDS];
};
assert_sizeof( vkProbeGpuHeader_t, 112 + 32 * 112 );
assert_offsetof( vkProbeGpuHeader_t, records, 112 );

struct vkProbeBuffer_t {
	VkBuffer buffer;
	VmaAllocation allocation;
	void *mapped;
	VkDeviceSize capacity;
};
struct vkProbeFrame_t {
	VkDescriptorPool pool;
	std::vector<vkProbeBuffer_t> buffers;
	unsigned int used;
	bool ready;
};
struct vkProbeView_t {
	const viewDef_t *view;
	VkDescriptorSet set;
};

static const unsigned int VK_PROBE_MAX_VIEWS_PER_FRAME = 64;
static vkProbeFrame_t probeFrames[VK_FRAMES_IN_FLIGHT];
static vkProbeResident_t residents[MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES];
static std::vector<vkProbeView_t> activeViews;
static VkDescriptorSetLayout probeLayout;
static std::uint64_t frameGeneration, residencyGeneration, atlasGeneration;
static unsigned int atlasHandle, atlasImageGeneration;
static int activeSlot = -1;
static bool packetResourceFailure;
static rendererClusteredLightingStats_t probeStats;
static unsigned int uploads, cacheHits, evictions, sourceRejects, resourceRejects;
// Source-cache misses (REN-06): the synchronous readback through the upload
// batch, then CPU prefiltering and the atlas tile uploads. Microseconds.
static unsigned int missReads;
static unsigned long long missReadTotal, missReadMax, missFilterTotal, missFilterMax;
// Background prefilter (r_pbrProbeAsyncPrefilter), one per atlas slot: the
// frame that misses submits the source copy; a later frame collects it once
// its fence has signalled and starts a job that decodes and filters it; later
// frames upload the finished tiles within MODERN_SPECULAR_PROBE_PUBLISH_BUDGET_US
// each. Until the last tile lands the whole set takes its analytic fallback.
struct vkProbePrefilter_t {
	const idImage *source = NULL;
	vkProbeSourceStamp_t stamp = {};
	vkProbeSourceRead_t read;
	unsigned long long startClock = 0;
	unsigned long long readCost = 0;	// frame time of submitting and collecting the copy
	bool active = false;
	bool taken = false;
	int nextTile = 0;
	openq4PBR::PrefilteredTiles tiles;
};
static openq4PBR::AsyncPrefilter prefilterJobs[MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES];
static vkProbePrefilter_t prefilterPending[MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES];
static unsigned int prefilterStarted, prefilterPublished, prefilterCancelled, prefilterWaits, prefilterPendingFrames;
static unsigned long long prefilterLatencyTotal, prefilterLatencyMax;
static unsigned long long publishSliceMax;	// most time one non-capture frame spent reading and publishing probes
static unsigned long long publishSpentThisFrame;
static int publishTilesThisFrame, readsThisFrame;
static bool prefilterWaitThisFrame, prefilterPendingThisFrame;
static const unsigned int VK_PROBE_PREFILTER_WORKERS = 4;

static void VK_PBRProbes_CancelPrefilter( int slot ) {
	if ( prefilterPending[slot].active || prefilterJobs[slot].Running() ) {
		prefilterJobs[slot].Cancel();
		if ( prefilterPending[slot].active ) { ++prefilterCancelled; }
	}
	VK_ProbeSource_CancelRead( prefilterPending[slot].read );
	prefilterPending[slot] = vkProbePrefilter_t{};
}

static unsigned int VK_PBRProbes_PendingPrefilters();

static void VK_PBRProbes_CancelAllPrefilters() {
	for ( int slot = 0; slot < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++slot ) {
		VK_PBRProbes_CancelPrefilter( slot );
	}
}

idImage *VK_PBRProbes_Atlas() {
	return globalImages != NULL
		? globalImages->ImageFromFunction( "_vkPBREnvironment", VK_PBR_GenerateEnvironment ) : NULL;
}

static bool VK_PBRProbes_AtlasReady( idImage *&atlas ) {
	atlas = VK_PBRProbes_Atlas();
	const vkImageEntry_t *entry = atlas != NULL && atlas->IsLoaded()
		? VK_Image_GetEntry( atlas->GetDeviceHandle() ) : NULL;
	// A failed authored tile does not invalidate the analytic region. Its
	// generator purges the image on failure, so a loaded replacement is whole.
	if ( entry == NULL
			|| entry->format != VK_FORMAT_R16G16B16A16_SFLOAT
			|| entry->width != MODERN_SPECULAR_PROBE_ATLAS_SIZE
			|| entry->height != MODERN_SPECULAR_PROBE_ATLAS_SIZE
			|| entry->numMips != MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP + 1 ) { return false; }
	if ( atlasGeneration != atlas->GetStorageGeneration() || atlasHandle != atlas->GetDeviceHandle()
			|| atlasImageGeneration != entry->generation ) {
		// Reload creates a new image. Existing draws retain their retired image;
		// every reservation for this replacement must be populated again.
		VK_PBRProbes_CancelAllPrefilters();
		memset( residents, 0, sizeof( residents ) );
		atlasGeneration = atlas->GetStorageGeneration();
		atlasHandle = atlas->GetDeviceHandle();
		atlasImageGeneration = entry->generation;
	}
	return true;
}

static bool VK_PBRProbes_UploadTile( idImage *atlas, int cell, int level, int size,
		const std::vector<float> &rgba ) {
	std::vector<std::uint16_t> pixels( rgba.size() );
	for ( size_t i = 0; i < rgba.size(); ++i ) { pixels[i] = openq4PBR::RadianceHalf( rgba[i] ); }
	const int cellSize = MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE >> level;
	atlas->SubImageUpload( level, cell % 8 * cellSize, cell / 8 * cellSize, 0, size, size, pixels.data() );
	const vkImageEntry_t *entry = VK_Image_GetEntry( atlas->GetDeviceHandle() );
	return entry != NULL && entry->lastUploadSucceeded;
}

static bool VK_PBRProbes_PublishTiles( idImage *atlas, int slot, const openq4PBR::PrefilteredTiles &tiles ) {
	for ( int level = 0; level <= MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP; ++level ) {
		const int size = MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE >> level;
		for ( int face = 0; face < 6; ++face ) {
			if ( !VK_PBRProbes_UploadTile( atlas, slot * 6 + face, level, size, tiles.specular[ level * 6 + face ] ) ) {
				return false;
			}
		}
	}
	return VK_PBRProbes_UploadTile( atlas, MODERN_SPECULAR_PROBE_DIFFUSE_FIRST_CELL + slot, 0,
		MODERN_SPECULAR_PROBE_DIFFUSE_SIZE, tiles.diffuse );
}

static bool VK_PBRProbes_UploadHalfTile( idImage *atlas, int cell, int level, int size,
		const std::vector<std::uint16_t> &pixels ) {
	const int cellSize = MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE >> level;
	atlas->SubImageUpload( level, cell % 8 * cellSize, cell / 8 * cellSize, 0, size, size, pixels.data() );
	const vkImageEntry_t *entry = VK_Image_GetEntry( atlas->GetDeviceHandle() );
	return entry != NULL && entry->lastUploadSucceeded;
}

static modernSpecularProbeAtlasReject_t VK_PBRProbes_Acquire( const idImage *image,
		modernSpecularProbeAtlasPlacement_t *placement ) {
	if ( placement == NULL ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NULL_OUTPUT; }
	ModernSpecularProbeAtlas_ClearPlacement( *placement );
	idImage *atlas;
	if ( r_vkPBRProbeFailure.GetInteger() == 1 || !VK_PBRProbes_AtlasReady( atlas ) ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UNAVAILABLE;
	}
	int slot = -1;
	for ( int i = 0; i < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++i ) {
		if ( residents[i].source != image || image == NULL ) { continue; }
		if ( residents[i].valid && VK_ProbeSource_Matches( image, residents[i].stamp ) ) {
			slot = i;
			++cacheHits;
			break;
		}
		if ( residents[i].lastUsedFrame == frameGeneration && !prefilterPending[i].active ) {
			return MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED;
		}
		slot = i;
		break;
	}
	if ( slot < 0 ) {
		for ( int i = 0; i < MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES; ++i ) {
			if ( residents[i].lastUsedFrame == frameGeneration ) { continue; }
			if ( slot < 0 || residents[i].lastUsedFrame < residents[slot].lastUsedFrame ) { slot = i; }
		}
	}
	if ( slot < 0 ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_ATLAS_FULL; }
	vkProbeResident_t &resident = residents[slot];
	if ( !resident.valid || resident.source != image || !VK_ProbeSource_Matches( image, resident.stamp ) ) {
		vkProbePrefilter_t &pending = prefilterPending[slot];
		if ( pending.active && ( pending.source != image || !VK_ProbeSource_Matches( image, pending.stamp ) ) ) {
			// The slot was reassigned or its source changed under the job.
			VK_PBRProbes_CancelPrefilter( slot );
		}
		const bool wait = prefilterWaitThisFrame;	// a capture finishes everything now
		const auto stillPending = [&]() {
			resident.lastUsedFrame = frameGeneration;
			prefilterPendingThisFrame = true;
			return MODERN_SPECULAR_PROBE_ATLAS_REJECT_PREFILTER_PENDING;
		};
		const auto chargeFrame = [&]( unsigned long long begin ) {
			const unsigned long long spent = R_RendererMetrics_CpuClock() - begin;
			publishSpentThisFrame += spent;
			if ( !wait ) { publishSliceMax = Max( publishSliceMax, publishSpentThisFrame ); }
			return spent;
		};
		openq4PBR::PrefilteredTiles tiles;
		vkProbeSourceStamp_t stamp;
		unsigned long long filterBegin = 0;
		bool synchronous = false;	// read, filtered and published on this frame
		if ( !pending.active && r_pbrProbeAsyncPrefilter.GetBool() && !wait ) {
			// Submit the source copy; a later frame collects it.
			if ( ( publishTilesThisFrame > 0 || readsThisFrame > 0 )
					&& publishSpentThisFrame >= MODERN_SPECULAR_PROBE_PUBLISH_BUDGET_US ) {
				prefilterPendingThisFrame = true;	// this frame has spent its budget
				return MODERN_SPECULAR_PROBE_ATLAS_REJECT_PREFILTER_PENDING;
			}
			const unsigned long long begin = R_RendererMetrics_CpuClock();
			++readsThisFrame;
			const modernSpecularProbeAtlasReject_t reject = VK_ProbeSource_BeginRead( image, pending.read );
			const unsigned long long cost = chargeFrame( begin );
			if ( reject != MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE ) { return reject; }
			if ( resident.valid && resident.source != image ) { ++evictions; }
			resident.valid = false;
			resident.source = image;	// later frames find the pending read here
			pending.source = image;
			pending.stamp = pending.read.stamp;
			pending.startClock = begin;
			pending.readCost = cost;
			pending.active = true;
			++prefilterStarted;
			return stillPending();
		}
		if ( pending.active && pending.read.active ) {
			// Collect the copy once its fence has signalled; decode and filter it
			// on the job.
			const unsigned long long begin = R_RendererMetrics_CpuClock();
			std::vector<std::uint8_t> encoded;
			const modernSpecularProbeAtlasReject_t reject = VK_ProbeSource_CollectRead( image, pending.read, encoded, wait );
			pending.readCost += chargeFrame( begin );
			if ( reject == MODERN_SPECULAR_PROBE_ATLAS_REJECT_PREFILTER_PENDING ) { return stillPending(); }
			if ( reject != MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE ) {
				VK_PBRProbes_CancelPrefilter( slot );
				return reject;
			}
			++missReads;
			missReadTotal += pending.readCost;
			missReadMax = Max( missReadMax, pending.readCost );
			const bool hdr = pending.read.hdr;
			const int faceSize = pending.stamp.faceSize;
			auto bytes = std::make_shared<std::vector<std::uint8_t>>( std::move( encoded ) );
			if ( !prefilterJobs[slot].StartFrom( [bytes, hdr, faceSize]( openq4PBR::Cube &cube ) {
						return VK_ProbeSource_Decode( *bytes, hdr, faceSize, cube );
					}, MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE, MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP,
					MODERN_SPECULAR_PROBE_DIFFUSE_SIZE, VK_PROBE_PREFILTER_WORKERS, true ) ) {
				// No thread available: finish the filter on this frame.
				const unsigned long long filterStart = R_RendererMetrics_CpuClock();
				openq4PBR::Cube cube;
				if ( !VK_ProbeSource_Decode( *bytes, hdr, faceSize, cube ) ) {
					VK_PBRProbes_CancelPrefilter( slot );
					return MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE;
				}
				pending.tiles = openq4PBR::PrefilterTiles( cube, MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE,
					MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP, MODERN_SPECULAR_PROBE_DIFFUSE_SIZE );
				openq4PBR::PackTilesHalf( pending.tiles );
				pending.taken = true;
				chargeFrame( filterStart );
			}
			if ( !wait && !pending.taken ) { return stillPending(); }
		}
		if ( pending.active ) {
			if ( !pending.taken ) {
				if ( !prefilterJobs[slot].Ready() && !wait ) { return stillPending(); }
				if ( !prefilterJobs[slot].Ready() ) { ++prefilterWaits; }
				const unsigned long long takeClock = R_RendererMetrics_CpuClock();
				pending.tiles = prefilterJobs[slot].Take();
				if ( prefilterJobs[slot].Failed() ) {
					VK_PBRProbes_CancelPrefilter( slot );	// nonfinite or malformed texels
					return MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE;
				}
				pending.taken = true;
				const unsigned long long latency = takeClock - pending.startClock;
				prefilterLatencyTotal += latency;
				prefilterLatencyMax = Max( prefilterLatencyMax, latency );
			}
			// Upload a budgeted slice of tiles; the slot stays unpublished meanwhile.
			const unsigned long long sliceBegin = R_RendererMetrics_CpuClock();
			while ( pending.nextTile < MODERN_SPECULAR_PROBE_TILE_COUNT ) {
				const unsigned long long spent = publishSpentThisFrame + ( R_RendererMetrics_CpuClock() - sliceBegin );
				if ( !wait && publishTilesThisFrame > 0 && spent >= MODERN_SPECULAR_PROBE_PUBLISH_BUDGET_US ) { break; }
				int cell, level, size;
				ModernSpecularProbeAtlas_TileTarget( slot, pending.nextTile, cell, level, size );
				const std::vector<std::uint16_t> &rgba = pending.nextTile < MODERN_SPECULAR_PROBE_TILE_COUNT - 1
					? pending.tiles.specularHalf[pending.nextTile] : pending.tiles.diffuseHalf;
				if ( !VK_PBRProbes_UploadHalfTile( atlas, cell, level, size, rgba ) ) {
					chargeFrame( sliceBegin );
					VK_PBRProbes_CancelPrefilter( slot );
					return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE;
				}
				++pending.nextTile;
				++publishTilesThisFrame;
			}
			missFilterTotal += chargeFrame( sliceBegin );
			if ( pending.nextTile < MODERN_SPECULAR_PROBE_TILE_COUNT ) { return stillPending(); }
			stamp = pending.stamp;
			++prefilterPublished;
			pending = vkProbePrefilter_t{};
		} else {
			// Synchronous: r_pbrProbeAsyncPrefilter 0, or a capture with nothing pending.
			openq4PBR::Cube cube;
			const unsigned long long readBegin = R_RendererMetrics_CpuClock();
			const modernSpecularProbeAtlasReject_t reject = VK_ProbeSource_Read( image, cube, stamp );
			const unsigned long long readCost = chargeFrame( readBegin );
			if ( reject != MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE ) { return reject; }
			++missReads;
			missReadTotal += readCost;
			missReadMax = Max( missReadMax, readCost );
			if ( resident.valid && resident.source != image ) { ++evictions; }
			resident.valid = false;
			filterBegin = R_RendererMetrics_CpuClock();
			synchronous = true;
			tiles = openq4PBR::PrefilterTiles( cube, MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE,
				MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP, MODERN_SPECULAR_PROBE_DIFFUSE_SIZE );
			if ( !VK_PBRProbes_PublishTiles( atlas, slot, tiles ) ) {
				return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE;
			}
		}
		resident.valid = false;
		if ( !VK_ProbeSource_Matches( image, stamp ) ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED; }
		resident.source = image;
		resident.stamp = stamp;
		resident.residency = ++residencyGeneration;
		if ( resident.residency == 0 ) { resident.residency = ++residencyGeneration; }
		resident.valid = true;
		++uploads;
		if ( synchronous ) {
			// A synchronous filter spends its whole cost on this frame.
			const unsigned long long cost = chargeFrame( filterBegin );
			missFilterTotal += cost;
			missFilterMax = Max( missFilterMax, cost );
		}
	}
	resident.lastUsedFrame = frameGeneration;
	if ( !ModernSpecularProbeAtlas_BuildPlacement( slot, resident.stamp.faceSize, *placement ) ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE;
	}
	placement->atlasGeneration = atlasGeneration;
	placement->residencyGeneration = resident.residency;
	placement->sourceStorageGeneration = resident.stamp.storageGeneration;
	return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE;
}

VkDescriptorSetLayout VK_PBRProbes_CreateLayout() {
	if ( probeLayout != VK_NULL_HANDLE ) { return probeLayout; }
	VkDescriptorSetLayoutBinding binding = {};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	info.bindingCount = 1;
	info.pBindings = &binding;
	if ( vkCreateDescriptorSetLayout( vkCtx.device, &info, NULL, &probeLayout ) != VK_SUCCESS ) { return VK_NULL_HANDLE; }
	return probeLayout;
}

void VK_PBRProbes_BeginFrame( int slot ) {
	activeViews.clear();
	packetResourceFailure = false;
	activeSlot = slot;
	if ( ++frameGeneration == 0 ) { ++frameGeneration; }
	vkProbeFrame_t &frame = probeFrames[slot];
	frame.used = 0;
	frame.ready = true;
	if ( frame.pool != VK_NULL_HANDLE ) {
		frame.ready = vkResetDescriptorPool( vkCtx.device, frame.pool, 0 ) == VK_SUCCESS;
	}
}

static VkDescriptorSet VK_PBRProbes_UploadView( const rendererSpecularProbeView_t &view ) {
	if ( activeSlot < 0 || probeLayout == VK_NULL_HANDLE ) { return VK_NULL_HANDLE; }
	vkProbeFrame_t &frame = probeFrames[activeSlot];
	if ( !frame.ready || frame.used >= VK_PROBE_MAX_VIEWS_PER_FRAME ) { return VK_NULL_HANDLE; }
	if ( frame.pool == VK_NULL_HANDLE ) {
		VkDescriptorPoolSize size = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_PROBE_MAX_VIEWS_PER_FRAME };
		VkDescriptorPoolCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		info.maxSets = VK_PROBE_MAX_VIEWS_PER_FRAME;
		info.poolSizeCount = 1;
		info.pPoolSizes = &size;
		if ( vkCreateDescriptorPool( vkCtx.device, &info, NULL, &frame.pool ) != VK_SUCCESS ) { return VK_NULL_HANDLE; }
	}
	const VkDeviceSize bytes = sizeof( vkProbeGpuHeader_t ) + view.indices.size() * sizeof( std::uint32_t );
	if ( bytes > vkCtx.deviceProperties.limits.maxStorageBufferRange || r_vkPBRProbeFailure.GetInteger() == 2 ) { return VK_NULL_HANDLE; }
	if ( frame.used >= frame.buffers.size() ) { frame.buffers.push_back( vkProbeBuffer_t{} ); }
	vkProbeBuffer_t &buffer = frame.buffers[frame.used];
	if ( buffer.capacity < bytes ) {
		// This unused buffer belongs to the slot whose fence BeginFrame waited.
		if ( buffer.buffer != VK_NULL_HANDLE ) { vmaDestroyBuffer( vkCtx.allocator, buffer.buffer, buffer.allocation ); }
		buffer = vkProbeBuffer_t{};
		VkBufferCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		info.size = bytes;
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		VmaAllocationCreateInfo create = {};
		create.usage = VMA_MEMORY_USAGE_AUTO;
		create.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		VmaAllocationInfo mapped = {};
		if ( vmaCreateBuffer( vkCtx.allocator, &info, &create, &buffer.buffer, &buffer.allocation, &mapped ) != VK_SUCCESS ) { return VK_NULL_HANDLE; }
		buffer.capacity = bytes;
		buffer.mapped = mapped.pMappedData;
	}
	if ( buffer.mapped == NULL ) { return VK_NULL_HANDLE; }
	vkProbeGpuHeader_t header;
	memcpy( header.grid, view.grid, sizeof( header.grid ) );
	memcpy( header.depth, view.depth, sizeof( header.depth ) );
	memcpy( header.viewOrigin, view.viewOrigin, sizeof( header.viewOrigin ) );
	memcpy( header.worldToView, view.worldToView, sizeof( header.worldToView ) );
	memcpy( header.projection, view.projection, sizeof( header.projection ) );
	memcpy( header.records, view.records, sizeof( header.records ) );
	memcpy( buffer.mapped, &header, sizeof( header ) );
	memcpy( static_cast<byte *>( buffer.mapped ) + sizeof( header ), view.indices.data(), view.indices.size() * sizeof( std::uint32_t ) );
	if ( vmaFlushAllocation( vkCtx.allocator, buffer.allocation, 0, bytes ) != VK_SUCCESS ) { return VK_NULL_HANDLE; }
	if ( r_vkPBRProbeFailure.GetInteger() == 3 ) { return VK_NULL_HANDLE; }
	VkDescriptorSetAllocateInfo allocate = {};
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorPool = frame.pool;
	allocate.descriptorSetCount = 1;
	allocate.pSetLayouts = &probeLayout;
	VkDescriptorSet set = VK_NULL_HANDLE;
	if ( vkAllocateDescriptorSets( vkCtx.device, &allocate, &set ) != VK_SUCCESS ) { return VK_NULL_HANDLE; }
	VkDescriptorBufferInfo bufferInfo = { buffer.buffer, 0, bytes };
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = set;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write.pBufferInfo = &bufferInfo;
	vkUpdateDescriptorSets( vkCtx.device, 1, &write, 0, NULL );
	++frame.used;
	return set;
}

void VK_PBRProbes_PrepareFrame( const idScenePacketFrame &frame ) {
	activeViews.clear();
	packetResourceFailure = false;
	memset( &probeStats, 0, sizeof( probeStats ) );
	if ( !r_rendererModernQuality.GetBool() || !r_pbrMaterials.GetBool()
			|| !r_rendererReflectionProbes.GetBool() || !r_pbrIBL.GetBool() || r_pbrIBLIntensity.GetFloat() <= 0.0f ) { return; }
	// Open the actual GPU frame before reserving slots. Subsequent command
	// packets share its pins; only a waited frame-slot reset releases them.
	if ( !VK_GuiExecutor_BeginFrame() ) { packetResourceFailure = true; return; }
	std::vector<rendererSpecularProbeView_t> views;
	// A screenshot must show the probes the scene will settle on, so a capture
	// frame finishes any background prefilter instead of falling back.
	prefilterWaitThisFrame = R_PBRProbes_SettleRequested();
	prefilterPendingThisFrame = false;
	publishSpentThisFrame = 0;
	publishTilesThisFrame = 0;
	readsThisFrame = 0;
	for ( int i = 0; i < frame.NumScenes(); ++i ) {
		if ( frame.Scene( i ).temporalCaptureFrame ) { prefilterWaitThisFrame = true; }
	}
	const bool prepared = R_ModernClusteredLighting_PrepareProbes( frame, VK_PBRProbes_Acquire, frameGeneration, views, probeStats );
	if ( R_PBRProbes_SettleRequested() && VK_PBRProbes_PendingPrefilters() == 0 ) {
		R_PBRProbes_SettleDone();
	}
	if ( !prepared ) {
		if ( prefilterPendingThisFrame ) {
			++prefilterPendingFrames;
		} else {
			++sourceRejects;
		}
		return; // same whole-set analytic fallback as GL
	}
	for ( const rendererSpecularProbeView_t &view : views ) {
		VkDescriptorSet set = VK_PBRProbes_UploadView( view );
		if ( set == VK_NULL_HANDLE ) {
			activeViews.clear();
			packetResourceFailure = true;
			++resourceRejects;
			return;
		}
		activeViews.push_back( vkProbeView_t{ view.viewDef, set } );
	}
}

bool VK_PBRProbes_ForView( const viewDef_t *view, VkDescriptorSet &set ) {
	set = VK_NULL_HANDLE;
	if ( packetResourceFailure ) { return false; }
	for ( const vkProbeView_t &record : activeViews ) {
		if ( record.view == view ) { set = record.set; return true; }
	}
	return true;
}

void VK_PBRProbes_Shutdown() {
	// Join the background filters before the module or the atlas can go away.
	VK_PBRProbes_CancelAllPrefilters();
	activeViews.clear();
	for ( vkProbeFrame_t &frame : probeFrames ) {
		if ( frame.pool != VK_NULL_HANDLE ) { vkDestroyDescriptorPool( vkCtx.device, frame.pool, NULL ); }
		for ( const vkProbeBuffer_t &buffer : frame.buffers ) {
			if ( buffer.buffer != VK_NULL_HANDLE ) { vmaDestroyBuffer( vkCtx.allocator, buffer.buffer, buffer.allocation ); }
		}
		frame = vkProbeFrame_t{};
	}
	if ( probeLayout != VK_NULL_HANDLE ) { vkDestroyDescriptorSetLayout( vkCtx.device, probeLayout, NULL ); }
	probeLayout = VK_NULL_HANDLE;
	memset( residents, 0, sizeof( residents ) );
	atlasGeneration = 0;
	atlasHandle = atlasImageGeneration = 0;
	activeSlot = -1;
	packetResourceFailure = false;
}

static unsigned int VK_PBRProbes_PendingPrefilters() {
	unsigned int count = 0;
	for ( const vkProbePrefilter_t &pending : prefilterPending ) { count += pending.active ? 1u : 0u; }
	return count;
}

void VK_PBRProbes_PrintGfxInfo() {
	common->Printf( "Vulkan PBR probes: records=%d references=%d views=%d ready=%d resourceFailure=%d uploads=%u hits=%u evictions=%u sourceRejects=%u resourceRejects=%u missReads=%u missReadMeanUs=%llu missReadMaxUs=%llu missFilterMeanUs=%llu missFilterMaxUs=%llu prefilterAsync=%d prefilterStarted=%u prefilterPublished=%u prefilterPending=%u prefilterCancelled=%u prefilterWaits=%u prefilterPendingFrames=%u prefilterLatencyMeanUs=%llu prefilterLatencyMaxUs=%llu publishFrameMaxUs=%llu\n",
		probeStats.probeCount, probeStats.probeReferences, int( activeViews.size() ),
		probeStats.probeFrameReady ? 1 : 0, packetResourceFailure ? 1 : 0,
		uploads, cacheHits, evictions, sourceRejects, resourceRejects, missReads,
		missReads > 0 ? missReadTotal / missReads : 0ull, missReadMax,
		uploads > 0 ? missFilterTotal / uploads : 0ull, missFilterMax,
		r_pbrProbeAsyncPrefilter.GetBool() ? 1 : 0, prefilterStarted, prefilterPublished,
		VK_PBRProbes_PendingPrefilters(), prefilterCancelled, prefilterWaits, prefilterPendingFrames,
		prefilterPublished > 0 ? prefilterLatencyTotal / prefilterPublished : 0ull, prefilterLatencyMax,
		publishSliceMax );
}

#endif
