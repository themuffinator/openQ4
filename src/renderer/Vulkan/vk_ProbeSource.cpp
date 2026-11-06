// Copyright (C) 2026 DarkMatter Productions
#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop
#include "../tr_local.h"
#include "../HDRExposureCore.h"
#include "vk_ProbeSource.h"
#include "vk_Image.h"
#include "vk_ExecutorHooks.h"
#include "VulkanDevice.h"

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

static modernSpecularProbeAtlasReject_t VK_ProbeSource_Validate(
		const idImage *image, vkImageEntry_t *&entry ) {
	entry = NULL;
	if ( image == NULL ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NULL_IMAGE; }
	if ( !vkCtx.initialized || vkCtx.allocator == NULL || vkCtx.uploadCommandBuffer == VK_NULL_HANDLE ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UNAVAILABLE;
	}
	if ( !image->IsLoaded() ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_LOADED; }
	if ( image->IsDefaulted() ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_DEFAULTED; }
	if ( R_IsMutableRenderImage( image ) ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_MUTABLE; }
	if ( image->GetOpts().textureType != TT_CUBIC ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_CUBE; }
	const int size = image->GetUploadWidth();
	if ( size <= 0 || size != image->GetUploadHeight() ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NON_SQUARE; }
	if ( size > MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_OVERSIZED; }
	entry = VK_Image_GetEntry( const_cast<idImage *>( image )->GetDeviceHandle() );
	if ( entry == NULL || entry->image == VK_NULL_HANDLE || !entry->isCube
			|| entry->numLayers != 6 || entry->numMips < 1 || entry->width != size || entry->height != size
			|| entry->samples != VK_SAMPLE_COUNT_1_BIT || entry->aspectMask != VK_IMAGE_ASPECT_COLOR_BIT
			|| !( entry->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT )
			|| entry->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			|| !entry->lastUploadSucceeded || entry->uploadedCubeFaces != 0x3f
			|| entry->uploadGeneration == 0 || image->GetStorageGeneration() == 0 ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE;
	}
	// Authored probe loading uses TD_HIGH_QUALITY (uncompressed RGBA8).
	// Linear HDR generators use RGBA16F. Reject any other encoding explicitly;
	// never reinterpret compressed blocks or swizzled data as color bytes.
	const textureFormat_t format = image->GetOpts().format;
	const bool hdr = format == FMT_RGBA16F && entry->format == VK_FORMAT_R16G16B16A16_SFLOAT;
	const bool ldr = ( ( format == FMT_RGBA8 || format == FMT_XRGB8 )
			&& entry->format == VK_FORMAT_R8G8B8A8_UNORM )
		|| ( format == FMT_SRGBA8 && entry->format == VK_FORMAT_R8G8B8A8_SRGB );
	if ( !( hdr || ldr ) || image->GetOpts().colorFormat != CFM_DEFAULT ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE;
	}
	return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE;
}

static vkProbeSourceStamp_t VK_ProbeSource_Stamp( const idImage *image, const vkImageEntry_t &entry ) {
	vkProbeSourceStamp_t stamp = {};
	stamp.storageGeneration = image->GetStorageGeneration();
	stamp.uploadGeneration = entry.uploadGeneration;
	stamp.handle = const_cast<idImage *>( image )->GetDeviceHandle();
	stamp.imageGeneration = entry.generation;
	stamp.faceSize = entry.width;
	return stamp;
}

bool VK_ProbeSource_Matches( const idImage *image, const vkProbeSourceStamp_t &stamp ) {
	vkImageEntry_t *entry;
	if ( VK_ProbeSource_Validate( image, entry ) != MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE ) { return false; }
	const vkProbeSourceStamp_t current = VK_ProbeSource_Stamp( image, *entry );
	return current.storageGeneration == stamp.storageGeneration
		&& current.uploadGeneration == stamp.uploadGeneration && current.handle == stamp.handle
		&& current.imageGeneration == stamp.imageGeneration && current.faceSize == stamp.faceSize;
}

struct vkProbeSourceCopy_t {
	VkImage image;
	VkBuffer buffer;
	uint32_t size;
	uint32_t mip;
};

static void VK_ProbeSource_RecordCopy( VkCommandBuffer cmd, void *user ) {
	const vkProbeSourceCopy_t &copy = *static_cast<const vkProbeSourceCopy_t *>( user );
	VkImageMemoryBarrier2 source = {};
	source.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	source.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	source.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
	source.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	source.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
	source.srcQueueFamilyIndex = source.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	source.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	source.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	source.image = copy.image;
	source.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	source.subresourceRange.baseMipLevel = copy.mip;
	source.subresourceRange.levelCount = 1;
	source.subresourceRange.layerCount = 6;
	VkDependencyInfo dependency = {};
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &source;
	vkCmdPipelineBarrier2( cmd, &dependency );

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = copy.mip;
	region.imageSubresource.layerCount = 6;
	region.imageExtent.width = region.imageExtent.height = copy.size;
	region.imageExtent.depth = 1;
	vkCmdCopyImageToBuffer( cmd, copy.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, copy.buffer, 1, &region );

	// Restore the source before any already-recorded scene samples it. Only
	// mip zero was transitioned; the image table's whole-image layout is unchanged.
	source.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	source.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
	source.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	source.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
	source.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	source.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkBufferMemoryBarrier2 host = {};
	host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	host.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
	host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
	host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	host.buffer = copy.buffer;
	host.size = VK_WHOLE_SIZE;
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers = &host;
	vkCmdPipelineBarrier2( cmd, &dependency );
}

// The batch that recorded this read's copy is still open or still executing.
static bool VK_ProbeSource_BatchBusy( const vkProbeSourceRead_t &read ) {
	return vkCtx.uploadBatchSerial == read.batch && ( vkCtx.uploadBatchOpen || vkCtx.uploadBatchInFlight );
}

void VK_ProbeSource_CancelRead( vkProbeSourceRead_t &read ) {
	if ( read.buffer != VK_NULL_HANDLE && vkCtx.allocator != NULL ) {
		if ( VK_ProbeSource_BatchBusy( read ) ) {
			// The copy is recorded or executing; retire the buffer with the frame
			// slot (whose flush submits an open batch first).
			VK_Device_DeferDestroy( VK_NULL_HANDLE, VK_NULL_HANDLE, read.buffer, read.allocation );
		} else {
			vmaDestroyBuffer( vkCtx.allocator, read.buffer, read.allocation );
		}
	}
	read = vkProbeSourceRead_t();
}

modernSpecularProbeAtlasReject_t VK_ProbeSource_BeginRead( const idImage *image, vkProbeSourceRead_t &read ) {
	VK_ProbeSource_CancelRead( read );
	vkImageEntry_t *entry;
	const modernSpecularProbeAtlasReject_t validation = VK_ProbeSource_Validate( image, entry );
	if ( validation != MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE ) { return validation; }
	read.stamp = VK_ProbeSource_Stamp( image, *entry );
	read.hdr = entry->format == VK_FORMAT_R16G16B16A16_SFLOAT;
	VkBufferCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = VkDeviceSize( entry->width * entry->height ) * 6 * ( read.hdr ? 8 : 4 );
	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	VmaAllocationCreateInfo create = {};
	create.usage = VMA_MEMORY_USAGE_AUTO;
	create.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	VmaAllocationInfo mapped = {};
	if ( vmaCreateBuffer( vkCtx.allocator, &info, &create, &read.buffer, &read.allocation, &mapped ) != VK_SUCCESS ) {
		read = vkProbeSourceRead_t();
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE;
	}
	read.mapped = mapped.pMappedData;
	if ( read.mapped == NULL ) {
		VK_ProbeSource_CancelRead( read );
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE;
	}
	vkProbeSourceCopy_t copy = { entry->image, read.buffer, uint32_t( entry->width ), 0 };
	// Same graphics queue as image uploads and scene submission. Appending the
	// read after pending uploads avoids reading unsubmitted or stale source data.
	// This readback owns its host buffer; the upload batch must not free it.
	// The copy joins the open batch, which the frame submits with its own
	// uploads; flushing it here would make the frame's next upload wait for it.
	if ( !VK_Device_BatchedUpload( VK_ProbeSource_RecordCopy, &copy, VK_NULL_HANDLE, NULL, 0, &read.batch )
			|| !VK_ProbeSource_BatchBusy( read ) ) {
		VK_ProbeSource_CancelRead( read );
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE;
	}
	read.active = true;
	return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE;
}

modernSpecularProbeAtlasReject_t VK_ProbeSource_CollectRead( const idImage *image, vkProbeSourceRead_t &read,
		std::vector<std::uint8_t> &encoded, bool wait ) {
	encoded.clear();
	if ( !read.active || vkCtx.presentationBlocked ) {
		VK_ProbeSource_CancelRead( read );
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE;
	}
	// Opening a newer batch waits the previous one, so another serial (or our
	// batch neither open nor in flight) means the copy has completed.
	if ( vkCtx.uploadBatchSerial == read.batch && vkCtx.uploadBatchOpen ) {
		if ( !wait ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_PREFILTER_PENDING; }
		VK_Device_FlushUploadBatch();
	}
	if ( vkCtx.uploadBatchSerial == read.batch && vkCtx.uploadBatchInFlight ) {
		if ( !wait && vkGetFenceStatus( vkCtx.device, vkCtx.uploadFence ) != VK_SUCCESS ) {
			return MODERN_SPECULAR_PROBE_ATLAS_REJECT_PREFILTER_PENDING;
		}
		VK_Device_WaitUploadBatch();
	}
	if ( vkCtx.presentationBlocked || VK_ProbeSource_BatchBusy( read ) ) {
		VK_ProbeSource_CancelRead( read );
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE;
	}
	modernSpecularProbeAtlasReject_t result = MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE;
	if ( vmaInvalidateAllocation( vkCtx.allocator, read.allocation, 0, VK_WHOLE_SIZE ) != VK_SUCCESS ) {
		result = MODERN_SPECULAR_PROBE_ATLAS_REJECT_UPLOAD_UNAVAILABLE;
	} else if ( !VK_ProbeSource_Matches( image, read.stamp ) ) {
		result = MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED;
	} else {
		const size_t bytes = size_t( read.stamp.faceSize ) * read.stamp.faceSize * 6 * ( read.hdr ? 8 : 4 );
		encoded.resize( bytes );
		memcpy( encoded.data(), read.mapped, bytes );
	}
	vmaDestroyBuffer( vkCtx.allocator, read.buffer, read.allocation );
	read.buffer = VK_NULL_HANDLE;
	read.allocation = NULL;
	read.mapped = NULL;
	read.active = false;
	return result;
}

bool VK_ProbeSource_Decode( const std::vector<std::uint8_t> &encoded, bool hdr, int faceSize, openq4PBR::Cube &cube ) {
	cube = openq4PBR::Cube();
	const int pixelsPerFace = faceSize * faceSize;
	if ( faceSize <= 0 || encoded.size() != size_t( pixelsPerFace ) * 6 * ( hdr ? 8 : 4 ) ) { return false; }
	openq4PBR::Cube candidate;
	candidate.size = faceSize;
	const std::uint8_t *bytes = encoded.data();
	for ( int face = 0; face < 6; ++face ) {
		candidate.faces[face].resize( pixelsPerFace );
		for ( int pixel = 0; pixel < pixelsPerFace; ++pixel ) {
			float color[3] = {};
			for ( int component = 0; component < 3; ++component ) {
				const int index = ( face * pixelsPerFace + pixel ) * 4 + component;
				if ( hdr ) {
					unsigned short bits;
					memcpy( &bits, bytes + index * 2, sizeof( bits ) );
					float value;
					if ( !HDRExposure_DecodeHalf( bits, value ) ) { return false; }
					color[component] = Max( 0.0f, value );
				} else {
					color[component] = openq4PBRMath::PBRSRGBToLinear( bytes[index] / 255.0f );
				}
			}
			candidate.faces[face][pixel] = { color[0], color[1], color[2] };
		}
	}
	std::swap( cube, candidate );
	return true;
}

modernSpecularProbeAtlasReject_t VK_ProbeSource_Read( const idImage *image,
		openq4PBR::Cube &cube, vkProbeSourceStamp_t &stamp ) {
	cube = openq4PBR::Cube();
	stamp = {};
	vkProbeSourceRead_t read;
	modernSpecularProbeAtlasReject_t result = VK_ProbeSource_BeginRead( image, read );
	if ( result != MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE ) { return result; }
	std::vector<std::uint8_t> encoded;
	result = VK_ProbeSource_CollectRead( image, read, encoded, true );
	if ( result != MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE ) { return result; }
	openq4PBR::Cube candidate;
	if ( !VK_ProbeSource_Decode( encoded, read.hdr, read.stamp.faceSize, candidate ) ) {
		return MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE;
	}
	if ( !VK_ProbeSource_Matches( image, read.stamp ) ) { return MODERN_SPECULAR_PROBE_ATLAS_REJECT_SOURCE_CHANGED; }
	std::swap( cube, candidate );
	stamp = read.stamp;
	return MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE;
}

// GPU regression: expected radiance comes from deliberately asymmetric face,
// row and column patterns with independently specified encoded/linear values.
// It never compares a decoder against another invocation of that decoder.
static textureFormat_t vkProbeTestFormat = FMT_RGBA8;
static int vkProbeTestSize = 8;
static int vkProbeTestFaces = 6;
static bool vkProbeTest2D = false;
static const byte vkProbeTestBytes[4] = { 0, 64, 128, 255 };
static const float vkProbeTestLinear[4] = { 0.0f, 0.0512694584f, 0.2158605001f, 1.0f };
static const unsigned short vkProbeTestHalves[6] = { 0, 0x3400, 0x4000, 0x6c00, 0x7bff, 0xbc00 };
static const float vkProbeTestHDR[6] = { 0.0f, 0.25f, 2.0f, 4096.0f, 65504.0f, 0.0f };

static int VK_ProbeSource_TestIndex( int face, int x, int y, int component, bool hdr ) {
	// Each RGB component carries a different face bit, so all six faces
	// differ even at 1x1. Mix coordinates without a short repeating ramp;
	// flips, transposes and four-pixel shifts must change the larger patterns.
	const int spatial = ( x * 73 ) ^ ( y * 151 ) ^ ( ( x * y * 19 ) >> 2 )
		^ ( ( x >> 2 ) * 43 ) ^ ( ( y >> 3 ) * 101 );
	const int faceCode = ( ( face >> component ) & 1 ) * ( hdr ? 3 : 2 );
	return ( spatial + component + faceCode ) % ( hdr ? 6 : 4 );
}

static void VK_ProbeSource_TestGenerate( idImage *image ) {
	idImageOpts opts;
	opts.textureType = vkProbeTest2D ? TT_2D : TT_CUBIC;
	opts.format = vkProbeTestFormat;
	opts.width = opts.height = vkProbeTestSize;
	opts.numLevels = vkProbeTestSize >= 8 ? 3 : 1;
	opts.isPersistant = true;
	image->AllocImage( opts, TF_LINEAR, TR_CLAMP );
	const bool hdr = opts.format == FMT_RGBA16F;
	for ( int level = 0; level < opts.numLevels; ++level ) {
		const int size = Max( 1, opts.width >> level );
		for ( int face = 0; face < ( vkProbeTest2D ? 1 : vkProbeTestFaces ); ++face ) {
			std::vector<byte> rgba( size * size * 4 );
			std::vector<unsigned short> half( size * size * 4 );
			for ( int y = 0; y < size; ++y ) {
				for ( int x = 0; x < size; ++x ) {
					for ( int c = 0; c < 4; ++c ) {
						const int index = ( y * size + x ) * 4 + c;
						const int pattern = VK_ProbeSource_TestIndex( face, x, y, c, hdr );
						if ( hdr ) { half[index] = level == 0 ? vkProbeTestHalves[pattern] : 0x3c00; }
						else { rgba[index] = level == 0 ? vkProbeTestBytes[pattern] : 7; }
					}
				}
			}
			image->SubImageUpload( level, 0, 0, face, size, size, hdr
				? static_cast<const void *>( half.data() ) : static_cast<const void *>( rgba.data() ) );
		}
	}
}

struct vkProbeSourceTest_t {
	bool passed = true;
	int reads = 0;
	int rejections = 0;
	int texels = 0;
	void Check( bool value, const char *label ) {
		if ( !value ) { common->Printf( "Vulkan probe source mismatch: %s\n", label ); }
		passed = value && passed;
	}
	void Reject( const idImage *image, modernSpecularProbeAtlasReject_t expected, const char *label ) {
		openq4PBR::Cube output;
		output.size = 1;
		output.faces[0].push_back( { 1.0f, 2.0f, 3.0f } );
		vkProbeSourceStamp_t stamp = { 1, 1, 1, 1, 1 };
		const modernSpecularProbeAtlasReject_t actual = VK_ProbeSource_Read( image, output, stamp );
		bool clear = output.size == 0 && stamp.storageGeneration == 0 && stamp.uploadGeneration == 0
			&& stamp.handle == 0 && stamp.imageGeneration == 0 && stamp.faceSize == 0;
		for ( int face = 0; face < 6; ++face ) { clear = clear && output.faces[face].empty(); }
		Check( actual == expected && clear, label );
		++rejections;
	}
	void ReadPattern( const idImage *image ) {
		openq4PBR::Cube output;
		vkProbeSourceStamp_t stamp;
		const bool hdr = image->GetOpts().format == FMT_RGBA16F;
		const int size = image->GetUploadWidth();
		const bool ready = VK_ProbeSource_Read( image, output, stamp ) == MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE
			&& output.size == size && VK_ProbeSource_Matches( image, stamp );
		Check( ready, "complete cube read and fingerprint" );
		if ( !ready ) { return; }
		++reads;
		for ( int face = 0; face < 6; ++face ) {
			Check( output.faces[face].size() == size_t( size * size ), "face extent" );
			if ( output.faces[face].size() != size_t( size * size ) ) { continue; }
			bool matches = true;
			for ( int y = 0; y < size; ++y ) {
				for ( int x = 0; x < size; ++x ) {
					const openq4PBR::Vector value = output.faces[face][y * size + x];
					const float actual[3] = { value.x, value.y, value.z };
					for ( int c = 0; c < 3; ++c ) {
						const int index = VK_ProbeSource_TestIndex( face, x, y, c, hdr );
						const float expected = hdr ? vkProbeTestHDR[index] : vkProbeTestLinear[index];
						matches = matches && idMath::Fabs( actual[c] - expected ) < 0.000001f;
					}
					++texels;
				}
			}
			Check( matches, "face/row/column radiance and mip-zero selection" );
		}
	}
};

static textureFormat_t vkProbeTestCompressedFormat = FMT_DXT1;

static void VK_ProbeSource_TestCompressedGenerate( idImage *image ) {
	idImageOpts opts;
	opts.textureType = TT_CUBIC;
	opts.format = vkProbeTestCompressedFormat;
	opts.width = opts.height = 8;
	opts.numLevels = 4;
	opts.isPersistant = true;
	image->AllocImage( opts, TF_LINEAR, TR_CLAMP );
	const int blockBytes = opts.format == FMT_DXT1 ? 8 : 16;
	for ( int level = 0; level < opts.numLevels; ++level ) {
		const int paddedSize = Max( 4, opts.width >> level );
		std::vector<byte> bytes( ( paddedSize / 4 ) * ( paddedSize / 4 ) * blockBytes );
		for ( int face = 0; face < 6; ++face ) {
			for ( size_t i = 0; i < bytes.size(); ++i ) { bytes[i] = byte( face * 37 + level * 17 + i ); }
			image->SubImageUpload( level, 0, 0, face, paddedSize, paddedSize, bytes.data() );
		}
	}
}

static void VK_ProbeSource_TestCompressed( vkProbeSourceTest_t &test ) {
	if ( !vkCtx.textureCompressionBCSupported ) {
		common->Printf( "Vulkan compressed cube upload control unavailable: device has no BC support\n" );
		return;
	}
	idImage *image = globalImages->ImageFromFunction( "_vkProbeSourceCompressedTest", VK_ProbeSource_TestCompressedGenerate );
	if ( image == NULL ) { test.Check( false, "compressed image allocation" ); return; }
	int checks = 0;
	for ( int format = 0; format < 2 && test.passed; ++format ) {
		vkProbeTestCompressedFormat = format == 0 ? FMT_DXT1 : FMT_DXT5;
		image->Reload( true );
		const vkImageEntry_t *entry = VK_Image_GetEntry( image->GetDeviceHandle() );
		test.Check( entry != NULL && entry->lastUploadSucceeded, "compressed padded tail admitted" );
		if ( entry == NULL || !entry->lastUploadSucceeded ) { break; }
		const int blockBytes = format == 0 ? 8 : 16;
		for ( int level = 0; level < 4 && test.passed; ++level ) {
			const int size = 8 >> level;
			const int faceBytes = Max( 1, ( size + 3 ) / 4 ) * Max( 1, ( size + 3 ) / 4 ) * blockBytes;
			VkBufferCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			info.size = 6 * faceBytes;
			info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			VmaAllocationCreateInfo create = {};
			create.usage = VMA_MEMORY_USAGE_AUTO;
			create.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation = NULL;
			VmaAllocationInfo mapped = {};
			if ( vmaCreateBuffer( vkCtx.allocator, &info, &create, &buffer, &allocation, &mapped ) != VK_SUCCESS ) {
				test.Check( false, "compressed readback allocation" ); break;
			}
			vkProbeSourceCopy_t copy = { entry->image, buffer, uint32_t( size ), uint32_t( level ) };
			bool ready = VK_Device_BatchedUpload( VK_ProbeSource_RecordCopy, &copy, VK_NULL_HANDLE, NULL, 0 );
			VK_Device_FlushUploadBatch();
			ready = ready && vkCtx.uploadBatchInFlight
				&& vkWaitForFences( vkCtx.device, 1, &vkCtx.uploadFence, VK_TRUE, UINT64_MAX ) == VK_SUCCESS;
			if ( !ready ) {
				VK_Device_DeferDestroy( VK_NULL_HANDLE, VK_NULL_HANDLE, buffer, allocation );
				test.Check( false, "compressed readback submission" ); break;
			}
			VK_Device_WaitUploadBatch();
			ready = mapped.pMappedData != NULL
				&& vmaInvalidateAllocation( vkCtx.allocator, allocation, 0, VK_WHOLE_SIZE ) == VK_SUCCESS;
			const byte *bytes = static_cast<const byte *>( mapped.pMappedData );
			for ( int face = 0; face < 6 && ready; ++face ) {
				for ( int i = 0; i < faceBytes && ready; ++i ) {
					ready = bytes[face * faceBytes + i] == byte( face * 37 + level * 17 + i );
				}
			}
			vmaDestroyBuffer( vkCtx.allocator, buffer, allocation );
			test.Check( ready, "compressed GPU mip storage" );
			++checks;
		}
		test.Reject( image, MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE,
			"compressed probe encoding is explicitly unsupported" );
		const uint64_t generation = entry->uploadGeneration;
		const int pending = vkCtx.numUploadBatchPending;
		const byte block[16] = {};
		image->SubImageUpload( 0, 1, 0, 0, 4, 4, block );
		test.Check( !entry->lastUploadSucceeded && entry->uploadGeneration == generation
			&& vkCtx.numUploadBatchPending == pending, "unaligned compressed offset rejected" );
		image->SubImageUpload( 0, 0, 0, 0, 3, 4, block );
		test.Check( !entry->lastUploadSucceeded && entry->uploadGeneration == generation
			&& vkCtx.numUploadBatchPending == pending, "unaligned compressed extent rejected" );
	}
	image->PurgeImage();
	common->Printf( "Vulkan compressed cube upload controls: checkedMips=%d (BC1/BC3, all faces, padded 2x2/1x1 tails)\n", checks );
}

bool VK_ProbeSource_RunSelfTest( void ) {
	if ( !vkCtx.initialized || globalImages == NULL ) { return false; }
	vkProbeSourceTest_t test;
	idImage *source = globalImages->ImageFromFunction( "_vkProbeSourceTest", VK_ProbeSource_TestGenerate );
	if ( source == NULL ) { return false; }
	const textureFormat_t formats[3] = { FMT_RGBA8, FMT_SRGBA8, FMT_RGBA16F };
	const int sizes[3] = { 1, 8, MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE };
	for ( int format = 0; format < 3 && test.passed; ++format ) {
		for ( int extent = 0; extent < 3 && test.passed; ++extent ) {
			vkProbeTestFormat = formats[format];
			vkProbeTestSize = sizes[extent];
			source->Reload( true );
			for ( int phase = 0; phase < 3 && test.passed; ++phase ) {
				vkImageEntry_t *entry = VK_Image_GetEntry( source->GetDeviceHandle() );
				test.Check( entry != NULL, "source allocation" );
				if ( entry == NULL ) { break; }
				const vkProbeSourceStamp_t oldStamp = VK_ProbeSource_Stamp( source, *entry );
				if ( phase == 1 ) { source->Reload( true ); }
				if ( phase == 2 ) {
					source->PurgeImage();
					test.Check( !VK_ProbeSource_Matches( source, oldStamp ), "purged stamp" );
					test.Reject( source, MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_LOADED, "purged source clears output" );
					source->ActuallyLoadImage( false );
				}
				if ( phase != 0 ) { test.Check( !VK_ProbeSource_Matches( source, oldStamp ), "storage replacement stamp" ); }
				test.ReadPattern( source );
			}
		}
	}
	if ( !test.passed ) {
		vkProbeTestFormat = FMT_RGBA8;
		vkProbeTestSize = 1;
		source->PurgeImage();
		common->Printf( "Vulkan probe source self-test FAILED (source allocation or radiance)\n" );
		return false;
	}
	vkProbeTestFormat = FMT_RGBA8;
	vkProbeTestSize = 8;
	source->Reload( true );
	openq4PBR::Cube output;
	vkProbeSourceStamp_t stamp;
	test.Check( VK_ProbeSource_Read( source, output, stamp ) == MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE, "update reference" );
	const byte change[4] = { 255, 128, 64, 0 };
	source->SubImageUpload( 0, 3, 2, 4, 1, 1, change );
	test.Check( !VK_ProbeSource_Matches( source, stamp ), "in-place upload invalidates stamp" );
	test.Check( VK_ProbeSource_Read( source, output, stamp ) == MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE,
		"partial overwrite preserves complete source admission" );
	if ( output.size == 8 && output.faces[4].size() == 64 ) {
		const openq4PBR::Vector value = output.faces[4][2 * 8 + 3];
		test.Check( value.x == 1.0f && idMath::Fabs( value.y - vkProbeTestLinear[2] ) < 0.000001f
			&& idMath::Fabs( value.z - vkProbeTestLinear[1] ) < 0.000001f, "latest GPU contents after overwrite" );
	}

	// An open scene recording must survive a source-cache miss unchanged.
	test.Check( VK_GuiExecutor_BeginFrame(), "open scene recording" );
	const int slot = VK_Exec_ActiveFrameSlot();
	test.Check( VK_ProbeSource_Read( source, output, stamp ) == MODERN_SPECULAR_PROBE_ATLAS_REJECT_NONE,
		"source read during scene recording" );
	test.Check( VK_GuiExecutor_FrameIsOpen() && VK_Exec_ActiveFrameSlot() == slot, "scene recording retained" );
	test.Check( VK_GuiExecutor_EndFrameAndPresent(), "scene submission after source read" );

	test.Reject( NULL, MODERN_SPECULAR_PROBE_ATLAS_REJECT_NULL_IMAGE, "null source" );
	vkProbeTest2D = true;
	source->Reload( true );
	test.Reject( source, MODERN_SPECULAR_PROBE_ATLAS_REJECT_NOT_CUBE, "2D source" );
	vkProbeTest2D = false;
	vkProbeTestSize = MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE * 2;
	source->Reload( true );
	test.Reject( source, MODERN_SPECULAR_PROBE_ATLAS_REJECT_OVERSIZED, "oversized source" );
	vkProbeTestSize = 8;
	vkProbeTestFaces = 5;
	source->Reload( true );
	test.Reject( source, MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE, "missing base face" );
	vkProbeTestFaces = 6;
	vkProbeTestFormat = FMT_LUM8;
	source->Reload( true );
	test.Reject( source, MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE, "unsupported swizzled storage" );
	vkProbeTestFormat = FMT_RGBA8;
	source->Reload( true );
	idImageOpts mutableOpts = source->GetOpts();
	idImage *mutableSource = globalImages->ScratchImage( "_vkProbeSourceMutableTest", &mutableOpts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );
	test.Reject( mutableSource, MODERN_SPECULAR_PROBE_ATLAS_REJECT_MUTABLE, "mutable render image" );
	if ( mutableSource != NULL ) { mutableSource->PurgeImage(); }

	// Invalid upload dimensions must leave both the queue and content serial
	// untouched, while preventing admission after the reported upload failure.
	const int invalid[][7] = {
		{ -1, 0, 0, 0, 1, 1, 0 }, { 3, 0, 0, 0, 1, 1, 0 },
		{ 0, 0, 0, -1, 1, 1, 0 }, { 0, 0, 0, 6, 1, 1, 0 },
		{ 0, -1, 0, 0, 1, 1, 0 }, { 0, 0, -1, 0, 1, 1, 0 },
		{ 0, 8, 0, 0, 1, 1, 0 }, { 0, 0, 8, 0, 1, 1, 0 },
		{ 0, 0, 0, 0, 9, 1, 0 }, { 0, 0, 0, 0, 1, 9, 0 },
		{ 0, 0, 0, 0, 2, 1, 1 }, { 0, 0, 0, 0, 1, 1, -1 }
	};
	for ( const auto &region : invalid ) {
		const vkImageEntry_t *entry = VK_Image_GetEntry( source->GetDeviceHandle() );
		const uint64_t generation = entry->uploadGeneration;
		const int pending = vkCtx.numUploadBatchPending;
		const VkDeviceSize pendingBytes = vkCtx.uploadBatchPendingBytes;
		source->SubImageUpload( region[0], region[1], region[2], region[3], region[4], region[5], change, region[6] );
		test.Check( !entry->lastUploadSucceeded && entry->uploadGeneration == generation
			&& vkCtx.numUploadBatchPending == pending && vkCtx.uploadBatchPendingBytes == pendingBytes,
			"invalid upload rejected before recording" );
		test.Reject( source, MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE, "failed upload source" );
	}
	source->Reload( true );
	test.ReadPattern( source );
	vkProbeTestFormat = FMT_RGBA16F;
	source->Reload( true );
	const unsigned short nonfinite[3] = { 0x7c00, 0xfc00, 0x7e00 };
	for ( unsigned short bits : nonfinite ) {
		const unsigned short pixel[4] = { 0x3c00, bits, 0x3c00, 0x3c00 };
		source->SubImageUpload( 0, 7, 7, 5, 1, 1, pixel );
		test.Reject( source, MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE, "nonfinite HDR rejects whole cube" );
	}
	source->Reload( true );
	test.ReadPattern( source );
	VK_ProbeSource_TestCompressed( test );
	// Future reloadImages calls may revisit this diagnostic generator. Keep its
	// dormant configuration small and valid after the test has released storage.
	vkProbeTestFormat = FMT_RGBA8;
	vkProbeTestSize = 1;
	source->PurgeImage();
	common->Printf( "Vulkan probe source controls: patterns=%d rejected=%d checkedTexels=%d\n",
		test.reads, test.rejections, test.texels );
	common->Printf( "Vulkan probe source self-test %s (GPU cube readback, face orientation, sRGB/HDR, upload/reload generations and rejection)\n",
		test.passed ? "passed" : "FAILED" );
	return test.passed;
}

#endif
