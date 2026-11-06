// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan idImage backend (Phase D,
	docs/dev/archive/2026-10-18/plans/2026-07-18-vulkan-phase-d.md).

	Implements the idImage GPU half the GL build keeps in
	OpenGL/gl_Image.cpp. texnum indexes a module-side image table; every
	mip level arrives pre-generated from the CPU side (imagetools), so the
	upload path is staging-buffer copies only. Sampler-side component
	swizzles (fonts' green-alpha, the R8-backed alpha/intensity formats)
	are expressed on the VkImageView.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop
#include "../RendererResourceSettings.h"

#include "../tr_local.h"
#include "../RenderModuleAPI.h"

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
#include "volk.h"
#include "vk_mem_alloc.h"

#include "VulkanDevice.h"
#include "vk_Image.h"
#include "vk_ExecutorHooks.h"

bool idImage::ReadPixelsRGBA8( idList<byte> &pixels ) {
	pixels.Clear();
	if ( !IsLoaded() || opts.textureType != TT_2D || opts.format != FMT_RGBA8
			|| opts.numMSAASamples != 0 || opts.width < 1 || opts.height < 1
			|| opts.width > 8192 || opts.height > 8192 ) {
		return false;
	}
	return VK_Exec_ReadImageScreenshot( this, pixels );
}

extern idCVar image_anisotropy;

/*
====================
Image table
====================
*/
static vkImageEntry_t vkImages[ VK_MAX_IMAGES ];
static unsigned int vkImageGenerationCounter = 1;
static bool vkImageTableWarned = false;

int VK_Image_GetResidentCount( void ) {
	int count = 0;
	for ( int i = 0; i < VK_MAX_IMAGES; ++i ) {
		if ( vkImages[ i ].inUse ) { ++count; }
	}
	return count;
}

vkImageEntry_t *VK_Image_GetEntry( unsigned int texnum ) {
	if ( texnum == 0xFFFFFFFFu /* TEXTURE_NOT_LOADED */ || texnum >= VK_MAX_IMAGES || !vkImages[ texnum ].inUse ) {
		return NULL;
	}
	return &vkImages[ texnum ];
}

static int VK_Image_AllocSlot( void ) {
	for ( int i = 0; i < VK_MAX_IMAGES; i++ ) {
		if ( !vkImages[ i ].inUse ) {
			vkImageTableWarned = false;
			return i;
		}
	}
	if ( !vkImageTableWarned ) {
		vkImageTableWarned = true;
		common->Warning( "Vulkan: image table exhausted (%d)", VK_MAX_IMAGES );
	}
	return -1;
}

/*
====================
Format mapping

CPU-side packing (imagetools BinaryImage) is authoritative; this table only
selects the matching VkFormat and the view swizzle that reproduces the GL
sampler-side component mappings.
====================
*/
typedef struct vkFormatInfo_s {
	VkFormat			format;
	int					bytesPerBlock;	// compressed block or texel size
	int					blockDim;		// 1 for uncompressed, 4 for BC
	VkComponentMapping	swizzle;
	// FMT_RGB565 has no Metal equivalent on Intel/AMD Macs, so the upload
	// expands the CPU-side 16-bit texels to RGBA8 instead. Source data stays
	// 2 bytes per texel; bytesPerBlock describes the destination.
	bool				expandRgb565;
} vkFormatInfo_t;

static const VkComponentMapping VK_SWIZZLE_IDENTITY = {
	VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
	VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };

static bool VK_Image_GetFormatInfo( const idImageOpts &opts,
		textureUsage_t usage, vkFormatInfo_t &info ) {
	info.format = VK_FORMAT_UNDEFINED;
	info.bytesPerBlock = 4;
	info.blockDim = 1;
	info.swizzle = VK_SWIZZLE_IDENTITY;
	info.expandRgb565 = false;

	switch ( opts.format ) {
		case FMT_SRGBA8:
			info.format = VK_FORMAT_R8G8B8A8_SRGB;
			info.bytesPerBlock = 4;
			break;
		case FMT_RGBA8:
		case FMT_XRGB8:
			info.format = VK_FORMAT_R8G8B8A8_UNORM;
			info.bytesPerBlock = 4;
			break;
		case FMT_DXT1:
			info.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			info.bytesPerBlock = 8;
			info.blockDim = 4;
			break;
		case FMT_DXT5:
			info.format = VK_FORMAT_BC3_UNORM_BLOCK;
			info.bytesPerBlock = 16;
			info.blockDim = 4;
			break;
		case FMT_BC7:
			info.format = VK_FORMAT_BC7_UNORM_BLOCK;
			info.bytesPerBlock = 16;
			info.blockDim = 4;
			break;
		case FMT_ALPHA:
			// alpha in the red channel: RGB=1, A=R
			info.format = VK_FORMAT_R8_UNORM;
			info.bytesPerBlock = 1;
			info.swizzle.r = VK_COMPONENT_SWIZZLE_ONE;
			info.swizzle.g = VK_COMPONENT_SWIZZLE_ONE;
			info.swizzle.b = VK_COMPONENT_SWIZZLE_ONE;
			info.swizzle.a = VK_COMPONENT_SWIZZLE_R;
			break;
		case FMT_LUM8:
			// luminance: RGB=R, A=1
			info.format = VK_FORMAT_R8_UNORM;
			info.bytesPerBlock = 1;
			info.swizzle.r = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.g = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.b = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.a = VK_COMPONENT_SWIZZLE_ONE;
			break;
		case FMT_INT8:
			// intensity: RGBA=R
			info.format = VK_FORMAT_R8_UNORM;
			info.bytesPerBlock = 1;
			info.swizzle.r = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.g = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.b = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.a = VK_COMPONENT_SWIZZLE_R;
			break;
		case FMT_L8A8:
			// luminance + alpha: RGB=R, A=G
			info.format = VK_FORMAT_R8G8_UNORM;
			info.bytesPerBlock = 2;
			info.swizzle.r = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.g = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.b = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.a = VK_COMPONENT_SWIZZLE_G;
			break;
		case FMT_RGBA16F:
			info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
			info.bytesPerBlock = 8;
			break;
		case FMT_DEPTH:
			info.format = VK_FORMAT_D32_SFLOAT;
			info.bytesPerBlock = 4;
			break;
		case FMT_DEPTH_STENCIL:
			// Match the device-selected attachment format. Some Vulkan
			// implementations expose D32S8 but not D24S8; render-target
			// creation must follow the same probe as the swapchain depth.
			info.format = vkCtx.depthFormat != VK_FORMAT_UNDEFINED
					? vkCtx.depthFormat : VK_FORMAT_D24_UNORM_S8_UINT;
			info.bytesPerBlock = 4;
			break;
		case FMT_RGB565:
			// CPU packs big-endian byte pairs (GL compensates with
			// UNPACK_SWAP_BYTES); gameplay-only format, byte-swap when the
			// upload path first meets it (Phase E lightgrid work).
			// Packed 16-bit formats are an Apple-GPU-only Metal feature, so
			// portability implementations without R5G6B5 expand to RGBA8
			// rather than losing every colored light cookie.
			if ( vkCtx.packed565Supported ) {
				info.format = VK_FORMAT_R5G6B5_UNORM_PACK16;
				info.bytesPerBlock = 2;
			} else {
				info.format = VK_FORMAT_R8G8B8A8_UNORM;
				info.bytesPerBlock = 4;
				info.expandRgb565 = true;
			}
			break;
		default:
			common->Warning( "Vulkan: unsupported textureFormat_t %d", (int)opts.format );
			return false;
	}

	// colorFormat refinements
	if ( opts.colorFormat == CFM_GREEN_ALPHA ) {
		// RGB=1, A=G (fonts and green-alpha DXT content)
		info.swizzle.r = VK_COMPONENT_SWIZZLE_ONE;
		info.swizzle.g = VK_COMPONENT_SWIZZLE_ONE;
		info.swizzle.b = VK_COMPONENT_SWIZZLE_ONE;
		info.swizzle.a = VK_COMPONENT_SWIZZLE_G;
	} else if ( usage == TD_BUMP && opts.colorFormat != CFM_NORMAL_DXT5 ) {
		// Legacy interaction programs decode Nx from alpha. Match the GL
		// backend by exposing the authored red channel through alpha for
		// bump maps that were not already packed as RXGB/DXT5 normals.
		info.swizzle.a = VK_COMPONENT_SWIZZLE_R;
	}
	return true;
}

/*
====================
Sampler cache
====================
*/
typedef struct vkSamplerKey_s {
	textureFilter_t	filter;
	textureRepeat_t	repeat;
	bool			mips;
	int				anisotropy;
	int				defaultFilterMode;
	bool			explicitMipChain;
} vkSamplerKey_t;

static const int VK_MAX_SAMPLERS = 64;
static vkSamplerKey_t vkSamplerKeys[ VK_MAX_SAMPLERS ];
static VkSampler vkSamplers[ VK_MAX_SAMPLERS ];
static int vkNumSamplers = 0;

static VkSampler VK_Image_GetSampler( textureFilter_t filter, textureRepeat_t repeat, bool mips,
		bool explicitMipChain = false ) {
	const imageFilterState_t defaultFilter = R_GetDefaultImageFilterState();
	const int defaultFilterMode = filter == TF_DEFAULT ? static_cast<int>( defaultFilter.mode ) : -1;
	// Explicit linear/nearest filters select the base level even if the image
	// was uploaded with a mip chain before its sampler state changed. Match
	// GL_LINEAR/GL_NEAREST so reloadImages cannot change which level is used.
	// A generated data chain read with explicit LODs is the exception: its
	// levels are separate data (prefiltered radiance), never a minification.
	explicitMipChain = explicitMipChain && mips && filter == TF_LINEAR;
	if ( !explicitMipChain && ( filter != TF_DEFAULT || !defaultFilter.usesMipmaps ) ) {
		mips = false;
	}
	int anisotropy = 0;
	if ( filter == TF_DEFAULT && mips && defaultFilter.minLinear ) {
		anisotropy = image_anisotropy.GetInteger();
		if ( anisotropy < 0 ) {
			anisotropy = 0;
		}
		if ( anisotropy > (int)vkCtx.deviceProperties.limits.maxSamplerAnisotropy ) {
			anisotropy = (int)vkCtx.deviceProperties.limits.maxSamplerAnisotropy;
		}
	}

	for ( int i = 0; i < vkNumSamplers; i++ ) {
		if ( vkSamplerKeys[ i ].filter == filter && vkSamplerKeys[ i ].repeat == repeat
				&& vkSamplerKeys[ i ].mips == mips && vkSamplerKeys[ i ].anisotropy == anisotropy
				&& vkSamplerKeys[ i ].defaultFilterMode == defaultFilterMode
				&& vkSamplerKeys[ i ].explicitMipChain == explicitMipChain ) {
			return vkSamplers[ i ];
		}
	}
	if ( vkNumSamplers >= VK_MAX_SAMPLERS ) {
		R_ImagePolicyObserveError( "Vulkan sampler cache exhausted" );
		common->Warning( "Vulkan: sampler cache exhausted" );
		return vkSamplers[ 0 ];
	}

	VkSamplerCreateInfo sci;
	memset( &sci, 0, sizeof( sci ) );
	sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	switch ( filter ) {
		case TF_NEAREST:
			sci.magFilter = VK_FILTER_NEAREST;
			sci.minFilter = VK_FILTER_NEAREST;
			sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		case TF_LINEAR:
			sci.magFilter = VK_FILTER_LINEAR;
			sci.minFilter = VK_FILTER_LINEAR;
			sci.mipmapMode = explicitMipChain ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		default:	// TF_DEFAULT
			sci.magFilter = defaultFilter.magLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			sci.minFilter = defaultFilter.minLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			sci.mipmapMode = defaultFilter.mipLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
	}
	sci.maxLod = mips ? VK_LOD_CLAMP_NONE : 0.25f;

	VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	switch ( repeat ) {
		case TR_CLAMP:
			addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		case TR_CLAMP_TO_BORDER:
			addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			break;
		case TR_CLAMP_TO_ZERO:
			addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			break;
		case TR_CLAMP_TO_ZERO_ALPHA:
			addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			sci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			break;
		default:	// TR_REPEAT / TR_MIRRORED_REPEAT
			addressMode = ( repeat == TR_MIRRORED_REPEAT ) ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;
	}
	sci.addressModeU = addressMode;
	sci.addressModeV = addressMode;
	sci.addressModeW = addressMode;

	if ( anisotropy > 1 ) {
		sci.anisotropyEnable = VK_TRUE;
		sci.maxAnisotropy = (float)anisotropy;
	}

	VkSampler sampler = VK_NULL_HANDLE;
	const VkResult samplerResult = vkCreateSampler( vkCtx.device, &sci, NULL, &sampler );
	if ( samplerResult != VK_SUCCESS ) {
		R_ImagePolicyObserveError( "Vulkan sampler creation failed", samplerResult );
		common->Warning( "Vulkan: sampler creation failed" );
		return vkNumSamplers > 0 ? vkSamplers[ 0 ] : VK_NULL_HANDLE;
	}
	vkSamplerKeys[ vkNumSamplers ].filter = filter;
	vkSamplerKeys[ vkNumSamplers ].repeat = repeat;
	vkSamplerKeys[ vkNumSamplers ].mips = mips;
	vkSamplerKeys[ vkNumSamplers ].anisotropy = anisotropy;
	vkSamplerKeys[ vkNumSamplers ].defaultFilterMode = defaultFilterMode;
	vkSamplerKeys[ vkNumSamplers ].explicitMipChain = explicitMipChain;
	vkSamplers[ vkNumSamplers ] = sampler;
	vkNumSamplers++;
	return sampler;
}

/*
====================
VK_Image_AcquireResolveScratch

vkCmdBlitImage cannot read a multisampled source, so a _currentRender capture
taken while the scene renders into an MSAA target has to be resolved first.
vkCmdResolveImage demands a destination of the source's exact format with one
sample, while the capture destination is the front-end's own format, so the
resolve needs its own storage: this image. It is also the blit's source, hence
TRANSFER_SRC as well as TRANSFER_DST. One image, reused every frame, recreated
through the deferred-destroy queue when the render target changes size or
format, and never entered in the image table because it is never sampled.
====================
*/
static vkImageEntry_t vkResolveScratch;

static void VK_Image_ReleaseResolveScratch( bool deferred ) {
	if ( vkResolveScratch.image != VK_NULL_HANDLE ) {
		if ( deferred ) {
			// an in-flight frame may still be reading it
			VK_Device_DeferDestroy( vkResolveScratch.image, VK_NULL_HANDLE,
					VK_NULL_HANDLE, vkResolveScratch.allocation );
		} else if ( vkCtx.allocator != NULL ) {
			vmaDestroyImage( vkCtx.allocator, vkResolveScratch.image,
					vkResolveScratch.allocation );
		}
	}
	memset( &vkResolveScratch, 0, sizeof( vkResolveScratch ) );
}

/*
====================
VK_Image_AcquireDepthResolveScratch

The depth counterpart of the resolve scratch. A multisampled depth attachment
cannot be blitted or copied into single-sample storage; it has to be resolved
by a render pass (VK_Exec_ResolveDepthImage), so this image is a depth
attachment in the source's exact format, and the source of the flipped blit
into the capture after that. The resolve leaves it shader-readable, hence
SAMPLED.
====================
*/
static vkImageEntry_t vkDepthResolveScratch;

static void VK_Image_ReleaseDepthResolveScratch( bool deferred ) {
	if ( vkDepthResolveScratch.image != VK_NULL_HANDLE ) {
		if ( deferred ) {
			VK_Device_DeferDestroy( vkDepthResolveScratch.image,
					vkDepthResolveScratch.attachmentView, VK_NULL_HANDLE,
					vkDepthResolveScratch.allocation );
		} else {
			if ( vkDepthResolveScratch.attachmentView != VK_NULL_HANDLE ) {
				vkDestroyImageView( vkCtx.device, vkDepthResolveScratch.attachmentView, NULL );
			}
			if ( vkCtx.allocator != NULL ) {
				vmaDestroyImage( vkCtx.allocator, vkDepthResolveScratch.image,
						vkDepthResolveScratch.allocation );
			}
		}
	}
	memset( &vkDepthResolveScratch, 0, sizeof( vkDepthResolveScratch ) );
}

vkImageEntry_t *VK_Image_AcquireDepthResolveScratch( int width, int height,
		VkFormat format ) {
	if ( !vkCtx.initialized || width <= 0 || height <= 0
			|| format == VK_FORMAT_UNDEFINED ) {
		return NULL;
	}
	if ( vkDepthResolveScratch.image != VK_NULL_HANDLE
			&& vkDepthResolveScratch.width == width
			&& vkDepthResolveScratch.height == height
			&& vkDepthResolveScratch.format == format ) {
		return &vkDepthResolveScratch;
	}
	VK_Image_ReleaseDepthResolveScratch( true );

	const VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
			| VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	const bool hasStencil = format == VK_FORMAT_D16_UNORM_S8_UINT
			|| format == VK_FORMAT_D24_UNORM_S8_UINT
			|| format == VK_FORMAT_D32_SFLOAT_S8_UINT;

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
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;

	VkImage newImage = VK_NULL_HANDLE;
	VmaAllocation newAllocation = NULL;
	if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci,
			&newImage, &newAllocation, NULL ) != VK_SUCCESS ) {
		static bool warnedCreate = false;
		if ( !warnedCreate ) {
			warnedCreate = true;
			common->Warning( "Vulkan: depth resolve scratch creation failed (%dx%d fmt %d)",
					width, height, (int)format );
		}
		memset( &vkDepthResolveScratch, 0, sizeof( vkDepthResolveScratch ) );
		return NULL;
	}

	VkImageViewCreateInfo vci;
	memset( &vci, 0, sizeof( vci ) );
	vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vci.image = newImage;
	vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vci.format = format;
	vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT
			| ( hasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0 );
	vci.subresourceRange.levelCount = 1;
	vci.subresourceRange.layerCount = 1;
	VkImageView attachmentView = VK_NULL_HANDLE;
	if ( vkCreateImageView( vkCtx.device, &vci, NULL, &attachmentView ) != VK_SUCCESS ) {
		vmaDestroyImage( vkCtx.allocator, newImage, newAllocation );
		memset( &vkDepthResolveScratch, 0, sizeof( vkDepthResolveScratch ) );
		return NULL;
	}

	vkDepthResolveScratch.image = newImage;
	vkDepthResolveScratch.allocation = newAllocation;
	vkDepthResolveScratch.view = VK_NULL_HANDLE;
	vkDepthResolveScratch.attachmentView = attachmentView;
	vkDepthResolveScratch.sampler = VK_NULL_HANDLE;
	vkDepthResolveScratch.format = format;
	vkDepthResolveScratch.usage = usage;
	vkDepthResolveScratch.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	vkDepthResolveScratch.layout = VK_IMAGE_LAYOUT_UNDEFINED;
	vkDepthResolveScratch.samples = VK_SAMPLE_COUNT_1_BIT;
	vkDepthResolveScratch.width = width;
	vkDepthResolveScratch.height = height;
	vkDepthResolveScratch.numMips = 1;
	vkDepthResolveScratch.numLayers = 1;
	vkDepthResolveScratch.isCube = false;
	vkDepthResolveScratch.everUploaded = false;
	vkDepthResolveScratch.inUse = false;
	vkDepthResolveScratch.generation = 0;
	return &vkDepthResolveScratch;
}

vkImageEntry_t *VK_Image_AcquireResolveScratch( int width, int height,
		VkFormat format ) {
	if ( !vkCtx.initialized || width <= 0 || height <= 0
			|| format == VK_FORMAT_UNDEFINED ) {
		return NULL;
	}
	if ( vkResolveScratch.image != VK_NULL_HANDLE
			&& vkResolveScratch.width == width
			&& vkResolveScratch.height == height
			&& vkResolveScratch.format == format ) {
		return &vkResolveScratch;
	}
	VK_Image_ReleaseResolveScratch( true );

	const VkImageUsageFlags usage =
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

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
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;

	VkImage newImage = VK_NULL_HANDLE;
	VmaAllocation newAllocation = NULL;
	if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci,
			&newImage, &newAllocation, NULL ) != VK_SUCCESS ) {
		static bool warnedCreate = false;
		if ( !warnedCreate ) {
			warnedCreate = true;
			common->Warning( "Vulkan: capture resolve scratch creation failed (%dx%d fmt %d)",
					width, height, (int)format );
		}
		memset( &vkResolveScratch, 0, sizeof( vkResolveScratch ) );
		return NULL;
	}

	vkResolveScratch.image = newImage;
	vkResolveScratch.allocation = newAllocation;
	vkResolveScratch.view = VK_NULL_HANDLE;
	vkResolveScratch.attachmentView = VK_NULL_HANDLE;
	vkResolveScratch.sampler = VK_NULL_HANDLE;
	vkResolveScratch.format = format;
	vkResolveScratch.usage = usage;
	vkResolveScratch.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vkResolveScratch.layout = VK_IMAGE_LAYOUT_UNDEFINED;
	vkResolveScratch.samples = VK_SAMPLE_COUNT_1_BIT;
	vkResolveScratch.width = width;
	vkResolveScratch.height = height;
	vkResolveScratch.numMips = 1;
	vkResolveScratch.numLayers = 1;
	vkResolveScratch.isCube = false;
	vkResolveScratch.everUploaded = false;
	vkResolveScratch.inUse = false;
	vkResolveScratch.generation = 0;
	return &vkResolveScratch;
}

/*
====================
VK_Image_ShutdownAll

Device-shutdown hook: destroys every live image and sampler immediately
(the device is idle by contract when this runs).
====================
*/
static void VK_Image_ReleaseCubeAttachmentViews( vkImageEntry_t &entry, bool defer ) {
	for ( int face = 0; face < 6; face++ ) {
		if ( entry.cubeAttachmentViews[ face ] == VK_NULL_HANDLE ) {
			continue;
		}
		if ( defer ) {
			VK_Device_DeferDestroy( VK_NULL_HANDLE, entry.cubeAttachmentViews[ face ],
					VK_NULL_HANDLE, NULL );
		} else {
			vkDestroyImageView( vkCtx.device, entry.cubeAttachmentViews[ face ], NULL );
		}
		entry.cubeAttachmentViews[ face ] = VK_NULL_HANDLE;
	}
}

VkImageView VK_Image_GetAttachmentView( vkImageEntry_t *entry, int cubeFace ) {
	if ( entry == NULL || !entry->inUse || entry->image == VK_NULL_HANDLE
			|| cubeFace < 0 || cubeFace >= 6 ) {
		return VK_NULL_HANDLE;
	}
	if ( !entry->isCube ) {
		return entry->attachmentView;
	}
	if ( entry->cubeAttachmentViews[ cubeFace ] == VK_NULL_HANDLE ) {
		VkImageViewCreateInfo info;
		memset( &info, 0, sizeof( info ) );
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image = entry->image;
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.format = entry->format;
		info.subresourceRange.aspectMask = entry->aspectMask;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.baseArrayLayer = (uint32_t)cubeFace;
		info.subresourceRange.layerCount = 1;
		if ( vkCreateImageView( vkCtx.device, &info, NULL,
				&entry->cubeAttachmentViews[ cubeFace ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: cubemap attachment view creation failed (face %d)", cubeFace );
			return VK_NULL_HANDLE;
		}
	}
	return entry->cubeAttachmentViews[ cubeFace ];
}

bool VK_Image_GetRenderTargetAttachments( const idRenderTexture *target, int cubeFace,
		vkRenderTargetAttachments_t &attachments ) {
	memset( &attachments, 0, sizeof( attachments ) );
	if ( !vkCtx.initialized || target == NULL || cubeFace < 0 || cubeFace >= 6
			|| target->GetNumColorImages() > VK_MAX_COLOR_ATTACHMENTS
			|| (uint32_t)target->GetNumColorImages() > vkCtx.deviceProperties.limits.maxColorAttachments
			|| ( target->GetNumColorImages() == 0 && target->GetDepthImage() == NULL ) ) {
		return false;
	}
	attachments.colorCount = (uint32_t)target->GetNumColorImages();
	for ( uint32_t i = 0; i <= attachments.colorCount; i++ ) {
		const bool isDepth = i == attachments.colorCount;
		idImage *image = isDepth ? target->GetDepthImage() : target->GetColorImage( (int)i );
		if ( isDepth && image == NULL ) {
			continue;
		}
		vkImageEntry_t *entry = image != NULL ? VK_Image_GetEntry( image->GetDeviceHandle() ) : NULL;
		const VkImageUsageFlags usage = isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
				: VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if ( entry == NULL || ( entry->usage & usage ) == 0 || entry->width <= 0 || entry->height <= 0 ) {
			return false;
		}
		if ( i == 0 ) {
			attachments.extent.width = (uint32_t)entry->width;
			attachments.extent.height = (uint32_t)entry->height;
			attachments.samples = entry->samples;
		} else if ( attachments.extent.width != (uint32_t)entry->width
				|| attachments.extent.height != (uint32_t)entry->height
				|| attachments.samples != entry->samples ) {
			return false;
		}
		for ( uint32_t previous = 0; previous < i; previous++ ) {
			if ( attachments.colors[ previous ]->image == entry->image ) {
				return false;
			}
		}
		const VkImageView view = VK_Image_GetAttachmentView( entry, cubeFace );
		if ( view == VK_NULL_HANDLE ) {
			return false;
		}
		if ( isDepth ) {
			attachments.depth = entry;
			attachments.depthView = view;
		} else {
			attachments.colors[ i ] = entry;
			attachments.colorViews[ i ] = view;
		}
	}
	return true;
}

void VK_Image_ShutdownAll( void ) {
	R_RenderThread_Sync( "image" );
	for ( int i = 0; i < VK_MAX_IMAGES; i++ ) {
		if ( !vkImages[ i ].inUse ) {
			continue;
		}
		VK_Image_ReleaseCubeAttachmentViews( vkImages[ i ], false );
		if ( vkImages[ i ].view != VK_NULL_HANDLE ) {
			vkDestroyImageView( vkCtx.device, vkImages[ i ].view, NULL );
		}
		if ( vkImages[ i ].attachmentView != VK_NULL_HANDLE
				&& vkImages[ i ].attachmentView != vkImages[ i ].view ) {
			vkDestroyImageView( vkCtx.device, vkImages[ i ].attachmentView, NULL );
		}
		if ( vkImages[ i ].image != VK_NULL_HANDLE && vkCtx.allocator != NULL ) {
			vmaDestroyImage( vkCtx.allocator, vkImages[ i ].image, vkImages[ i ].allocation );
		}
		memset( &vkImages[ i ], 0, sizeof( vkImages[ i ] ) );
	}
	VK_Image_ReleaseResolveScratch( false );
	VK_Image_ReleaseDepthResolveScratch( false );
	for ( int i = 0; i < vkNumSamplers; i++ ) {
		vkDestroySampler( vkCtx.device, vkSamplers[ i ], NULL );
	}
	vkNumSamplers = 0;
}

/*
====================
idImage::PurgeImage
====================
*/
void idImage::PurgeImage( void ) {
	if ( !R_ImagePolicyContentMutation() ) return;
	R_RenderThread_Sync( "image" );
	vkImageEntry_t *entry = VK_Image_GetEntry( texnum );
	if ( entry != NULL ) {
		// the image may still be referenced by an in-flight frame
		VK_Image_ReleaseCubeAttachmentViews( *entry, true );
		VK_Device_DeferDestroy( entry->image, entry->view, VK_NULL_HANDLE, entry->allocation,
				entry->attachmentView != entry->view ? entry->attachmentView : VK_NULL_HANDLE );
		memset( entry, 0, sizeof( *entry ) );
	}
	texnum = TEXTURE_NOT_LOADED;
}

static VkSampleCountFlagBits VK_Image_SelectSampleCount( const idImageOpts &imageOpts,
		VkFormat format, VkImageAspectFlags aspectMask, VkImageUsageFlags usage,
		VkImageCreateFlags flags, bool attachmentCapable ) {
	if ( !attachmentCapable || imageOpts.textureType != TT_2D
			|| imageOpts.numMSAASamples <= 1 ) {
		return VK_SAMPLE_COUNT_1_BIT;
	}

	// Color and depth/stencil images form one forward render target and must
	// receive an identical count. The game retries lower tiers if the exact
	// color and depth formats disagree, while this global intersection avoids
	// selecting a tier no framebuffer combination can support.
	VkSampleCountFlags supported =
			vkCtx.deviceProperties.limits.framebufferColorSampleCounts
			& vkCtx.deviceProperties.limits.framebufferDepthSampleCounts;
	if ( ( aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT ) != 0 ) {
		supported &= vkCtx.deviceProperties.limits.framebufferStencilSampleCounts;
	}

	VkPhysicalDeviceImageFormatInfo2 formatInfo;
	memset( &formatInfo, 0, sizeof( formatInfo ) );
	formatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
	formatInfo.format = format;
	formatInfo.type = VK_IMAGE_TYPE_2D;
	formatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	formatInfo.usage = usage;
	formatInfo.flags = flags;

	VkImageFormatProperties2 imageProperties;
	memset( &imageProperties, 0, sizeof( imageProperties ) );
	imageProperties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
	if ( vkGetPhysicalDeviceImageFormatProperties2(
			vkCtx.physicalDevice, &formatInfo, &imageProperties ) != VK_SUCCESS ) {
		return VK_SAMPLE_COUNT_1_BIT;
	}
	supported &= imageProperties.imageFormatProperties.sampleCounts;

	static const struct {
		int samples;
		VkSampleCountFlagBits bit;
	} choices[] = {
		{ 8, VK_SAMPLE_COUNT_8_BIT },
		{ 4, VK_SAMPLE_COUNT_4_BIT },
		{ 2, VK_SAMPLE_COUNT_2_BIT },
	};
	for ( int i = 0; i < (int)( sizeof( choices ) / sizeof( choices[ 0 ] ) ); i++ ) {
		if ( choices[ i ].samples <= imageOpts.numMSAASamples && ( supported & choices[ i ].bit ) != 0 ) {
			return choices[ i ].bit;
		}
	}
	return VK_SAMPLE_COUNT_1_BIT;
}

static int VK_Image_SampleCountInteger( VkSampleCountFlagBits samples ) {
	switch ( samples ) {
		case VK_SAMPLE_COUNT_64_BIT: return 64;
		case VK_SAMPLE_COUNT_32_BIT: return 32;
		case VK_SAMPLE_COUNT_16_BIT: return 16;
		case VK_SAMPLE_COUNT_8_BIT: return 8;
		case VK_SAMPLE_COUNT_4_BIT: return 4;
		case VK_SAMPLE_COUNT_2_BIT: return 2;
		default: return 0;
	}
}

/*
====================
idImage::AllocImage

Creates the VkImage + view + sampler from opts. Before the device exists
(engine boots decls/materials first) the image stays TEXTURE_NOT_LOADED,
exactly like the GL half without a context; the InitOpenGL seam reloads.
====================
*/
void idImage::AllocImage( void ) {
	renderImageOperation_t imageOperation( this );
	if ( !imageOperation.Allowed() ) return;
	R_RenderThread_Sync( "image" );

	PurgeImage();
	storageGeneration++;

	if ( !vkCtx.initialized ) {
		return;
	}
	if ( opts.width <= 0 || opts.height <= 0 ) {
		return;
	}

	vkFormatInfo_t info;
	if ( !VK_Image_GetFormatInfo( opts, usage, info ) ) {
		return;
	}

	const bool isCube = opts.textureType == TT_CUBIC;
	const bool isDepth = opts.format == FMT_DEPTH || opts.format == FMT_DEPTH_STENCIL;
	const VkImageAspectFlags sampledAspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	const VkImageAspectFlags attachmentAspect = opts.format == FMT_DEPTH_STENCIL
			? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT ) : sampledAspect;

	VkFormatProperties formatProperties;
	memset( &formatProperties, 0, sizeof( formatProperties ) );
	vkGetPhysicalDeviceFormatProperties( vkCtx.physicalDevice, info.format, &formatProperties );
	const VkFormatFeatureFlags optimalFeatures = formatProperties.optimalTilingFeatures;
	const bool attachmentCapable = isDepth
			? ( optimalFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) != 0
			: ( optimalFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ) != 0;

	VkImageUsageFlags imageUsage =
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if ( optimalFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT ) {
		imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if ( attachmentCapable ) {
		imageUsage |= isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
				: VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	VkImageCreateFlags imageFlags =
			isCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
	const VkSampleCountFlagBits samples = VK_Image_SelectSampleCount(
			opts, info.format, attachmentAspect, imageUsage, imageFlags,
			attachmentCapable );
	if ( isDepth && ( vkCtx.sampleLocationCounts & samples ) != 0 ) {
		imageFlags |= VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT;
	}
	opts.numMSAASamples = VK_Image_SampleCountInteger( samples );
	const int numMips = samples == VK_SAMPLE_COUNT_1_BIT
			? ( opts.numLevels > 0 ? opts.numLevels : 1 ) : 1;

	VkImageCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = info.format;
	ici.extent.width = (uint32_t)opts.width;
	ici.extent.height = (uint32_t)opts.height;
	ici.extent.depth = 1;
	ici.mipLevels = (uint32_t)numMips;
	ici.arrayLayers = isCube ? 6 : 1;
	ici.samples = samples;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = imageUsage;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ici.flags = imageFlags;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;

	const int slot = VK_Image_AllocSlot();
	if ( slot < 0 ) {
		return;
	}
	vkImageEntry_t &entry = vkImages[ slot ];
	memset( &entry, 0, sizeof( entry ) );

	const VkResult imageResult = vmaCreateImage( vkCtx.allocator, &ici, &vaci, &entry.image, &entry.allocation, NULL );
	if ( imageResult != VK_SUCCESS ) {
		R_ImagePolicyObserveError( "Vulkan image resource creation failed", imageResult );
		common->Warning( "Vulkan: image creation failed (%dx%d fmt %d)", opts.width, opts.height, (int)opts.format );
		return;
	}

	VkImageViewCreateInfo ivci;
	memset( &ivci, 0, sizeof( ivci ) );
	ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ivci.image = entry.image;
	ivci.viewType = isCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
	ivci.format = info.format;
	ivci.components = info.swizzle;
	ivci.subresourceRange.aspectMask = sampledAspect;
	ivci.subresourceRange.levelCount = (uint32_t)numMips;
	ivci.subresourceRange.layerCount = isCube ? 6 : 1;
	const VkResult viewResult = vkCreateImageView( vkCtx.device, &ivci, NULL, &entry.view );
	if ( viewResult != VK_SUCCESS ) {
		R_ImagePolicyObserveError( "Vulkan image resource creation failed", viewResult );
		common->Warning( "Vulkan: image view creation failed" );
		vmaDestroyImage( vkCtx.allocator, entry.image, entry.allocation );
		memset( &entry, 0, sizeof( entry ) );
		return;
	}
	entry.attachmentView = entry.view;
	if ( attachmentAspect != sampledAspect ) {
		ivci.subresourceRange.aspectMask = attachmentAspect;
		const VkResult attachmentResult = vkCreateImageView( vkCtx.device, &ivci, NULL, &entry.attachmentView );
	if ( attachmentResult != VK_SUCCESS ) {
		R_ImagePolicyObserveError( "Vulkan image resource creation failed", attachmentResult );
			common->Warning( "Vulkan: depth/stencil attachment view creation failed" );
			vkDestroyImageView( vkCtx.device, entry.view, NULL );
			vmaDestroyImage( vkCtx.allocator, entry.image, entry.allocation );
			memset( &entry, 0, sizeof( entry ) );
			return;
		}
	}

	entry.inUse = true;
	entry.format = info.format;
	entry.usage = ici.usage;
	entry.aspectMask = attachmentAspect;
	entry.layout = VK_IMAGE_LAYOUT_UNDEFINED;
	entry.samples = samples;
	entry.width = opts.width;
	entry.height = opts.height;
	entry.numMips = numMips;
	entry.numLayers = isCube ? 6 : 1;
	entry.isCube = isCube;
	entry.everUploaded = false;
	entry.generation = vkImageGenerationCounter++;
	entry.sampler = VK_Image_GetSampler( filter, repeat, numMips > 1 );
	if ( entry.sampler == VK_NULL_HANDLE ) R_ImagePolicyObserveError( "Vulkan image sampler creation failed" );

	texnum = (unsigned int)slot;

	imageOperation.Succeeded();
}

/*
====================
VK_Image_MakeDepthCopyTarget

Depth copies cannot rely on image blits: Vulkan only guarantees a direct
depth copy when source and destination formats match exactly. Keep the
front-end idImage dimensions current, then replace its backing with a
single-sample sampled/transfer-destination image in the device depth format.
====================
*/
bool VK_Image_MakeDepthCopyTarget( idImage *image, int width, int height,
		VkFormat depthFormat ) {
	if ( image == NULL || !vkCtx.initialized || width <= 0 || height <= 0
			|| depthFormat == VK_FORMAT_UNDEFINED ) {
		return false;
	}
	const idImageOpts &imageOpts = image->GetOpts();
	const bool isCube = imageOpts.textureType == TT_CUBIC;
	if ( imageOpts.textureType != TT_2D && !isCube ) {
		return false;
	}
	const uint32_t numLayers = isCube ? 6u : 1u;

	if ( image->GetUploadWidth() != width || image->GetUploadHeight() != height ) {
		image->Resize( width, height );
	}
	if ( !image->IsLoaded() ) {
		idImageOpts imageOpts = image->GetOpts();
		imageOpts.width = width;
		imageOpts.height = height;
		imageOpts.numLevels = 1;
		imageOpts.numMSAASamples = 0;
		image->AllocImage( imageOpts, image->GetFilter(), image->GetRepeat() );
		if ( !image->IsLoaded() ) {
			return false;
		}
	}

	vkImageEntry_t *entry = VK_Image_GetEntry( image->GetDeviceHandle() );
	if ( entry == NULL ) {
		return false;
	}
	const VkImageUsageFlags requiredUsage =
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if ( entry->format == depthFormat
			&& entry->width == width && entry->height == height
			&& entry->view != VK_NULL_HANDLE
			&& entry->samples == VK_SAMPLE_COUNT_1_BIT
			&& entry->aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT
			&& entry->isCube == isCube && entry->numLayers == (int)numLayers
			&& ( entry->usage & requiredUsage ) == requiredUsage ) {
		return true;
	}

	VkImageCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.flags = isCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
	ici.format = depthFormat;
	ici.extent.width = (uint32_t)width;
	ici.extent.height = (uint32_t)height;
	ici.extent.depth = 1;
	ici.mipLevels = 1;
	ici.arrayLayers = numLayers;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = requiredUsage;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;

	VkImage newImage = VK_NULL_HANDLE;
	VmaAllocation newAllocation = NULL;
	if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci,
			&newImage, &newAllocation, NULL ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: depth-copy target image creation failed (%dx%d)",
				width, height );
		return false;
	}

	VkImageViewCreateInfo ivci;
	memset( &ivci, 0, sizeof( ivci ) );
	ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ivci.image = newImage;
	ivci.viewType = isCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
	ivci.format = depthFormat;
	ivci.components = VK_SWIZZLE_IDENTITY;
	ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	ivci.subresourceRange.levelCount = 1;
	ivci.subresourceRange.layerCount = numLayers;

	VkImageView newView = VK_NULL_HANDLE;
	if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &newView ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: depth-copy target view creation failed" );
		vmaDestroyImage( vkCtx.allocator, newImage, newAllocation );
		return false;
	}

	VK_Image_ReleaseCubeAttachmentViews( *entry, true );
	VK_Device_DeferDestroy( entry->image, entry->view, VK_NULL_HANDLE,
			entry->allocation,
			entry->attachmentView != entry->view ? entry->attachmentView : VK_NULL_HANDLE );

	entry->image = newImage;
	entry->allocation = newAllocation;
	entry->view = newView;
	entry->attachmentView = newView;
	entry->sampler = VK_Image_GetSampler( TF_NEAREST, TR_CLAMP, false );
	entry->format = depthFormat;
	entry->usage = requiredUsage;
	entry->aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	entry->layout = VK_IMAGE_LAYOUT_UNDEFINED;
	entry->samples = VK_SAMPLE_COUNT_1_BIT;
	entry->width = width;
	entry->height = height;
	entry->numMips = 1;
	entry->numLayers = (int)numLayers;
	entry->isCube = isCube;
	entry->everUploaded = false;
	entry->generation = vkImageGenerationCounter++;
	return true;
}

/*
====================
idImage::SubImageUpload

Staging-buffer copy of one mip/face region, immediate-submitted so the data
is resident before the recording frame is submitted.
====================
*/
typedef struct vkUploadContext_s {
	vkImageEntry_t *entry;
	VkBuffer		staging;
	int				mipLevel;
	int				layer;
	int				x, y;
	int				width, height;
	uint32_t		bufferRowLengthTexels;
	VkImageLayout	oldLayout;
} vkUploadContext_t;

static void VK_Image_RecordUpload( VkCommandBuffer cmd, void *user ) {
	const vkUploadContext_t *ctx = (const vkUploadContext_t *)user;

	VkImageMemoryBarrier2 toTransfer;
	memset( &toTransfer, 0, sizeof( toTransfer ) );
	toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	toTransfer.oldLayout = ctx->oldLayout;
	toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toTransfer.image = ctx->entry->image;
	toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toTransfer.subresourceRange.baseMipLevel = 0;
	toTransfer.subresourceRange.levelCount = (uint32_t)ctx->entry->numMips;
	toTransfer.subresourceRange.baseArrayLayer = 0;
	toTransfer.subresourceRange.layerCount = (uint32_t)ctx->entry->numLayers;

	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &toTransfer;
	vkCmdPipelineBarrier2( cmd, &dep );

	VkBufferImageCopy region;
	memset( &region, 0, sizeof( region ) );
	region.bufferRowLength = ctx->bufferRowLengthTexels;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = (uint32_t)ctx->mipLevel;
	region.imageSubresource.baseArrayLayer = (uint32_t)ctx->layer;
	region.imageSubresource.layerCount = 1;
	region.imageOffset.x = ctx->x;
	region.imageOffset.y = ctx->y;
	region.imageExtent.width = (uint32_t)ctx->width;
	region.imageExtent.height = (uint32_t)ctx->height;
	region.imageExtent.depth = 1;
	vkCmdCopyBufferToImage( cmd, ctx->staging, ctx->entry->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

	VkImageMemoryBarrier2 toShader = toTransfer;
	toShader.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	toShader.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	toShader.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	toShader.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
	toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	dep.pImageMemoryBarriers = &toShader;
	vkCmdPipelineBarrier2( cmd, &dep );
}

void idImage::SubImageUpload( int mipLevel, int x, int y, int z, int width, int height, const void *pic, int pixelPitch ) const {
	renderImageOperation_t imageOperation( this, true, mipLevel, z, x == 0 && y == 0 ? width : 0, height );
	if ( !imageOperation.Allowed() ) return;
	R_RenderThread_Sync( "image" );

	vkImageEntry_t *entry = VK_Image_GetEntry( texnum );
	if ( entry != NULL ) {
		entry->lastUploadSucceeded = false;
	}
	if ( entry == NULL || pic == NULL || width <= 0 || height <= 0
			|| entry->samples != VK_SAMPLE_COUNT_1_BIT
			|| ( entry->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT ) == 0 ) {
		return;
	}
	// Reject malformed regions before allocating or recording a GPU copy.
	// In particular, a sixth-face admission bit must never come from a copy
	// to a nonexistent layer, mip, or a region outside that mip's storage.
	if ( mipLevel < 0 || mipLevel >= entry->numMips || mipLevel >= 31
			|| z < 0 || z >= entry->numLayers || x < 0 || y < 0
			|| pixelPitch < 0 || ( pixelPitch != 0 && pixelPitch < width ) ) {
		return;
	}
	const int mipWidth = Max( 1, entry->width >> mipLevel );
	const int mipHeight = Max( 1, entry->height >> mipLevel );
	if ( x >= mipWidth || y >= mipHeight ) {
		return;
	}

	vkFormatInfo_t info;
	if ( !VK_Image_GetFormatInfo( opts, usage, info ) ) {
		return;
	}
	int copyWidth = width, copyHeight = height;
	if ( info.blockDim > 1 ) {
		// BinaryImage supplies whole compressed blocks, including padded 4x4
		// tails for a 2x2/1x1 cube mip. The byte payload includes that padding;
		// VkBufferImageCopy's extent must still stay inside the physical mip.
		const int block = info.blockDim;
		const int remainingWidth = mipWidth - x, remainingHeight = mipHeight - y;
		const int paddedWidth = ( remainingWidth + block - 1 ) / block * block;
		const int paddedHeight = ( remainingHeight + block - 1 ) / block * block;
		if ( x % block != 0 || y % block != 0 || pixelPitch != 0
				|| width > paddedWidth || height > paddedHeight
				|| ( width % block != 0 && width != remainingWidth )
				|| ( height % block != 0 && height != remainingHeight ) ) {
			return;
		}
		copyWidth = Min( width, remainingWidth );
		copyHeight = Min( height, remainingHeight );
	} else if ( width > mipWidth - x || height > mipHeight - y ) {
		return;
	}

	// data size: compressed rows are block rows padded to block alignment
	size_t dataBytes;
	uint32_t rowLengthTexels = 0;
	if ( info.blockDim > 1 ) {
		const int blocksWide = ( width + info.blockDim - 1 ) / info.blockDim;
		const int blocksHigh = ( height + info.blockDim - 1 ) / info.blockDim;
		dataBytes = (size_t)blocksWide * blocksHigh * info.bytesPerBlock;
	} else {
		const int rowTexels = pixelPitch > 0 ? pixelPitch : width;
		rowLengthTexels = pixelPitch > 0 ? (uint32_t)pixelPitch : 0;
		dataBytes = (size_t)rowTexels * height * info.bytesPerBlock;
	}

	// transient staging buffer; on success the upload batch takes ownership
	VkBufferCreateInfo bci;
	memset( &bci, 0, sizeof( bci ) );
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = dataBytes;
	bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;
	vaci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VkBuffer staging = VK_NULL_HANDLE;
	VmaAllocation stagingAlloc = NULL;
	VmaAllocationInfo stagingInfo;
	const VkResult stagingResult = vmaCreateBuffer( vkCtx.allocator, &bci, &vaci, &staging, &stagingAlloc, &stagingInfo );
	if ( stagingResult != VK_SUCCESS ) {
		R_ImagePolicyObserveError( "Vulkan image resource creation failed", stagingResult );
		common->Warning( "Vulkan: staging buffer creation failed (%d bytes)", (int)dataBytes );
		return;
	}
	if ( opts.format == FMT_RGB565 && info.expandRgb565 ) {
		// No packed 16-bit format on this implementation: widen to RGBA8 with
		// the same big-endian source unpacking, replicating the high bits into
		// the low ones so full-scale values stay full-scale.
		const uint8_t *src = (const uint8_t *)pic;
		uint8_t *dst = (uint8_t *)stagingInfo.pMappedData;
		const size_t texels = dataBytes / 4;
		for ( size_t i = 0; i < texels; i++ ) {
			const uint16_t packed = (uint16_t)( ( (uint16_t)src[ i * 2 + 0 ] << 8 ) | src[ i * 2 + 1 ] );
			const uint8_t r5 = (uint8_t)( ( packed >> 11 ) & 0x1f );
			const uint8_t g6 = (uint8_t)( ( packed >> 5 ) & 0x3f );
			const uint8_t b5 = (uint8_t)( packed & 0x1f );
			dst[ i * 4 + 0 ] = (uint8_t)( ( r5 << 3 ) | ( r5 >> 2 ) );
			dst[ i * 4 + 1 ] = (uint8_t)( ( g6 << 2 ) | ( g6 >> 4 ) );
			dst[ i * 4 + 2 ] = (uint8_t)( ( b5 << 3 ) | ( b5 >> 2 ) );
			dst[ i * 4 + 3 ] = 0xff;
		}
	} else if ( opts.format == FMT_RGB565 ) {
		// The CPU-side image path stores RGB565 as big-endian byte pairs;
		// OpenGL uses GL_UNPACK_SWAP_BYTES. Vulkan has no pixel-store
		// equivalent, so feed PACK16 native-endian values explicitly.
		const uint8_t *src = (const uint8_t *)pic;
		uint8_t *dst = (uint8_t *)stagingInfo.pMappedData;
		const size_t texels = dataBytes / 2;
		for ( size_t i = 0; i < texels; i++ ) {
			dst[ i * 2 + 0 ] = src[ i * 2 + 1 ];
			dst[ i * 2 + 1 ] = src[ i * 2 + 0 ];
		}
	} else {
		memcpy( stagingInfo.pMappedData, pic, dataBytes );
	}
	const VkResult flushResult = vmaFlushAllocation(
			vkCtx.allocator, stagingAlloc, 0, (VkDeviceSize)dataBytes );
	if ( flushResult != VK_SUCCESS ) {
		R_ImagePolicyObserveError( "Vulkan image staging flush failed", flushResult );
		common->Warning( "Vulkan: staging buffer flush failed (%d)",
				(int)flushResult );
		VK_Device_DeferDestroy(
				VK_NULL_HANDLE, VK_NULL_HANDLE, staging, stagingAlloc );
		return;
	}

	vkUploadContext_t ctx;
	ctx.entry = entry;
	ctx.staging = staging;
	ctx.mipLevel = mipLevel;
	ctx.layer = entry->isCube ? z : 0;
	ctx.x = x;
	ctx.y = y;
	ctx.width = copyWidth;
	ctx.height = copyHeight;
	ctx.bufferRowLengthTexels = rowLengthTexels;
	ctx.oldLayout = entry->layout;

	uint64_t consumedBatch = 0;
	if ( VK_Device_BatchedUpload( VK_Image_RecordUpload, &ctx, staging, stagingAlloc,
			(VkDeviceSize)dataBytes, &consumedBatch ) ) {
		entry->everUploaded = true;
		entry->lastUploadSucceeded = true;
		entry->materialSampleFlipY = false;
		if ( ++entry->uploadGeneration == 0 ) { ++entry->uploadGeneration; }
		if ( entry->isCube && mipLevel == 0 && x == 0 && y == 0
				&& copyWidth == entry->width && copyHeight == entry->height ) {
			entry->uploadedCubeFaces |= 1u << z;
		}
		entry->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	} else {
		// nothing recorded: release the staging buffer through the normal
		// deferred path, matching the old no-device fallback
		VK_Device_DeferDestroy( VK_NULL_HANDLE, VK_NULL_HANDLE, staging, stagingAlloc );
		return;
	}

	imageOperation.Succeeded(consumedBatch);
}

/*
====================
idImage::SetTexParameters

Filter/repeat changes re-resolve the sampler; the generation bump lets the
executor's descriptor cache re-bind.
====================
*/
void idImage::SetTexParameters( void ) {
	R_RenderThread_Sync( "image" );
	vkImageEntry_t *entry = VK_Image_GetEntry( texnum );
	if ( entry == NULL ) {
		return;
	}
	entry->sampler = VK_Image_GetSampler( filter, repeat, entry->numMips > 1, entry->explicitMipChain );
	entry->generation = vkImageGenerationCounter++;
}

/*
====================
VK_Image_UseExplicitMipChain

The PBR environment atlas stores roughness-prefiltered radiance in each level
and selects levels with explicit textureLod. Its TF_LINEAR sampler otherwise
clamps to the base level (matching GL_LINEAR), which silently replaced every
rough reflection with the mirror level. Keep the chain addressable, including
across later sampler refreshes; reallocation clears the flag again.
====================
*/
bool VK_Image_UseExplicitMipChain( unsigned int texnum ) {
	vkImageEntry_t *entry = VK_Image_GetEntry( texnum );
	if ( entry == NULL || entry->numMips <= 1 ) {
		return false;
	}
	entry->explicitMipChain = true;
	entry->sampler = VK_Image_GetSampler( TF_LINEAR, TR_CLAMP, true, true );
	entry->generation = vkImageGenerationCounter++;
	return entry->sampler != VK_NULL_HANDLE;
}

void idImage::RefreshSamplerState() {
	if ( IsLoaded() ) {
		SetTexParameters();
	}
}

/*
====================
idImage::Resize
====================
*/
void idImage::Resize( int width, int height ) {
	if ( opts.width == width && opts.height == height ) {
		return;
	}
	if ( !R_ImagePolicyContentMutation() ) return;
	R_RenderThread_Sync( "image" );
	opts.width = width;
	opts.height = height;
	AllocImage();
}

void idImage::RespecifyColorCopyTarget( int width, int height ) {
	if ( width <= 0 || height <= 0 || !R_ImagePolicyContentMutation() ) return;
	R_RenderThread_Sync( "image" );
	opts.format = FMT_RGBA8;
	opts.numLevels = 1;
	opts.width = width;
	opts.height = height;
	AllocImage();
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
