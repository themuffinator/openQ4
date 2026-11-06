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
		if ( residents[i].lastUsedFrame == frameGeneration ) {
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
		openq4PBR::Cube cube;
		vkProbeSourceStamp_t stamp;
		const unsigned long long readBegin = R_RendererMetrics_CpuClock();
		const modernSpecularProbeAtlasReject_t reject = VK_ProbeSource_Read( image, cube, stamp );
		if ( reject != MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE ) { return reject; }
		const unsigned long long filterBegin = R_RendererMetrics_CpuClock();
		if ( resident.valid && resident.source != image ) { ++evictions; }
		resident.valid = false;
		const openq4PBR::PrefilteredTiles tiles = openq4PBR::PrefilterTiles( cube,
			MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE, MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP,
			MODERN_SPECULAR_PROBE_DIFFUSE_SIZE );
		for ( int level = 0; level <= MODERN_SPECULAR_PROBE_ATLAS_MAX_MIP; ++level ) {
			const int size = MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE >> level;
			for ( int face = 0; face < 6; ++face ) {
				if ( !VK_PBRProbes_UploadTile( atlas, slot * 6 + face, level, size, tiles.specular[ level * 6 + face ] ) ) {
					return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE;
				}
			}
		}
		if ( !VK_PBRProbes_UploadTile( atlas, MODERN_SPECULAR_PROBE_DIFFUSE_FIRST_CELL + slot, 0,
				MODERN_SPECULAR_PROBE_DIFFUSE_SIZE, tiles.diffuse ) ) {
			return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE;
		}
		if ( !VK_ProbeSource_Matches( image, stamp ) ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED; }
		resident.source = image;
		resident.stamp = stamp;
		resident.residency = ++residencyGeneration;
		if ( resident.residency == 0 ) { resident.residency = ++residencyGeneration; }
		resident.valid = true;
		++uploads;
		const unsigned long long filterEnd = R_RendererMetrics_CpuClock();
		++missReads;
		missReadTotal += filterBegin - readBegin;
		missReadMax = Max( missReadMax, filterBegin - readBegin );
		missFilterTotal += filterEnd - filterBegin;
		missFilterMax = Max( missFilterMax, filterEnd - filterBegin );
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
	if ( !R_ModernClusteredLighting_PrepareProbes( frame, VK_PBRProbes_Acquire, frameGeneration, views, probeStats ) ) {
		++sourceRejects;
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

void VK_PBRProbes_PrintGfxInfo() {
	common->Printf( "Vulkan PBR probes: records=%d references=%d views=%d ready=%d resourceFailure=%d uploads=%u hits=%u evictions=%u sourceRejects=%u resourceRejects=%u missReads=%u missReadMeanUs=%llu missReadMaxUs=%llu missFilterMeanUs=%llu missFilterMaxUs=%llu\n",
		probeStats.probeCount, probeStats.probeReferences, int( activeViews.size() ),
		probeStats.probeFrameReady ? 1 : 0, packetResourceFailure ? 1 : 0,
		uploads, cacheHits, evictions, sourceRejects, resourceRejects, missReads,
		missReads > 0 ? missReadTotal / missReads : 0ull, missReadMax,
		missReads > 0 ? missFilterTotal / missReads : 0ull, missFilterMax );
}

#endif
