/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/



/*
================================================================================================
Contains the Image implementation for OpenGL.
================================================================================================
*/

#include "../tr_local.h"

/*
================================================================================================
ALPHA8, LUMINANCE8, LUMINANCE8_ALPHA8 and INTENSITY8 are compatibility-profile
sized internal formats. OpenGL ES 3.0 has none of them, so the tokens only
compile here because the GLES dispatch header defines their values -- the driver
rejects the upload at runtime and the texture is left without valid storage.

ES needs exactly the R8/RG8-plus-swizzle emulation the desktop core profile
already uses, and ES 3.0 supports GL_TEXTURE_SWIZZLE_* natively, so route the
GLES module through the same branches rather than duplicating them.
================================================================================================
*/
#if defined( USE_CORE_PROFILE ) || defined( OPENQ4_RENDERER_GLES_MODULE )
#define OPENQ4_GL_SWIZZLED_LEGACY_FORMATS 1
#endif

#ifndef GL_SRGB8
#define GL_SRGB8 0x8C41
#endif

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif

// core in ES 3.0 and in desktop GL 4.3, but the desktop GL headers this file
// sees are older than either
#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2 0x9274
#endif
#ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#endif
#ifndef GL_COMPRESSED_RG11_EAC
#define GL_COMPRESSED_RG11_EAC 0x9272
#endif

static int R_CompressedTextureSizeInBytes( textureFormat_t format, int width, int height ) {
	if ( width <= 0 || height <= 0 ) {
		return 0;
	}

	// shared with the cache-file validator in BinaryImage.cpp, so the two cannot
	// drift about how large a compressed level is
	const int bytesPerBlock = BytesPerBlockForFormat( format );
	if ( bytesPerBlock <= 0 ) {
		idLib::Error( "Invalid compressed texture format %d", format );
	}

	const int64 blocksWide = Max( (int64)1, ( (int64)width + 3 ) >> 2 );
	const int64 blocksHigh = Max( (int64)1, ( (int64)height + 3 ) >> 2 );
	const int64 compressedSize = blocksWide * blocksHigh * (int64)bytesPerBlock;
	if ( compressedSize <= 0 || compressedSize > 0x7fffffff ) {
		idLib::Error( "Compressed texture %dx%d format %d is too large", width, height, format );
	}
	return (int)compressedSize;
}

/*
========================
idImage::SubImageUpload
========================
*/
void idImage::SubImageUpload( int mipLevel, int x, int y, int z, int width, int height, const void * pic, int pixelPitch ) const {
	assert( x >= 0 && y >= 0 && mipLevel >= 0 && width >= 0 && height >= 0 && mipLevel < opts.numLevels );

	int compressedSize = 0;

	if ( IsCompressed() ) {
		assert( !(x&3) && !(y&3) );

		// compressed size may be larger than the dimensions due to padding to quads
		compressedSize = R_CompressedTextureSizeInBytes( opts.format, width, height );

		int padW = ( opts.width + 3 ) & ~3;
		int padH = ( opts.height + 3 ) & ~3;
		(void)padH;
		(void)padW;
		assert( x + width <= padW && y + height <= padH );
		// upload the non-aligned value, OpenGL understands that there
		// will be padding
		if ( x + width > opts.width ) {
			width = opts.width - x;
		}
		if ( y + height > opts.height ) {
			height = opts.height - y;
		}
	} else {
		assert( x + width <= opts.width && y + height <= opts.height );
	}

	int target;
	int uploadTarget;
	if ( opts.textureType == TT_2D ) {
		target = GL_TEXTURE_2D;
		uploadTarget = GL_TEXTURE_2D;
	} else if ( opts.textureType == TT_CUBIC ) {
		target = GL_TEXTURE_CUBE_MAP_EXT;
		uploadTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT + z;
	} else {
		assert( !"invalid opts.textureType" );
		target = GL_TEXTURE_2D;
		uploadTarget = GL_TEXTURE_2D;
	}

	R_BindTextureForDirectAccess( target, texnum );

	if ( pixelPitch != 0 ) {
		glPixelStorei( GL_UNPACK_ROW_LENGTH, pixelPitch );
	}

	// BinaryImage writes FMT_RGB565 big-endian (high byte first), so a
	// little-endian host needs the 16-bit pairs reversed before
	// GL_UNSIGNED_SHORT_5_6_5 reads them.
	//
	// GL_UNPACK_SWAP_BYTES is compatibility/desktop only -- OpenGL ES has no such
	// pixel-store parameter, so on ES the call raises GL_INVALID_ENUM and the swap
	// silently does not happen, scrambling the 5/6/5 bit fields. Swap in software
	// there instead.
	const void *uploadPic = pic;
#if defined( OPENQ4_RENDERER_GLES_MODULE )
	const int swapRowPixels = ( pixelPitch != 0 ) ? pixelPitch : width;
	const int swapPixelCount =
		( opts.format == FMT_RGB565 && !Swap_IsBigEndian() && !IsCompressed() && pic != NULL && swapRowPixels > 0 && height > 0 )
			? swapRowPixels * ( height - 1 ) + width
			: 0;
	idTempArray<byte> swappedPic( (unsigned int)( swapPixelCount * 2 ) );
	if ( swapPixelCount > 0 ) {
		const byte *src = (const byte *)pic;
		byte *dst = swappedPic.Ptr();
		for ( int i = 0; i < swapPixelCount; i++ ) {
			dst[ i * 2 + 0 ] = src[ i * 2 + 1 ];
			dst[ i * 2 + 1 ] = src[ i * 2 + 0 ];
		}
		uploadPic = dst;
	}
#else
	if ( opts.format == FMT_RGB565 ) {
		glPixelStorei( GL_UNPACK_SWAP_BYTES, GL_TRUE );
	}
#endif
#ifdef DEBUG
	GL_CheckErrors();
#endif
	if ( IsCompressed() ) {
		glCompressedTexSubImage2DARB( uploadTarget, mipLevel, x, y, width, height, internalFormat, compressedSize, pic );
	} else {

		// make sure the pixel store alignment is correct so that lower mips get created
		// properly for odd shaped textures - this fixes the mip mapping issues with
		// fonts
		int unpackAlignment = width * BitsForFormat( (textureFormat_t)opts.format ) / 8;
		if ( ( unpackAlignment & 3 ) == 0 ) {
			glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
		} else {
			glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		}

		glTexSubImage2D( uploadTarget, mipLevel, x, y, width, height, dataFormat, dataType, uploadPic );
	}
#ifdef DEBUG
	GL_CheckErrors();
#endif
#if !defined( OPENQ4_RENDERER_GLES_MODULE )
	if ( opts.format == FMT_RGB565 ) {
		glPixelStorei( GL_UNPACK_SWAP_BYTES, GL_FALSE );
	}
#endif
	if ( pixelPitch != 0 ) {
		glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
	}
}

/*
========================
idImage::SetPixel
========================
*/
void idImage::SetPixel( int mipLevel, int x, int y, const void * data, int dataSize ) {
	SubImageUpload( mipLevel, x, y, 0, 1, 1, data );
}

/*
========================
idImage::SetTexParameters
========================
*/
void idImage::SetTexParameters() {
	int target = GL_TEXTURE_2D;
	switch ( opts.textureType ) {
		case TT_2D:
			target = GL_TEXTURE_2D;
			break;
		case TT_CUBIC:
			target = GL_TEXTURE_CUBE_MAP_EXT;
			break;
		default:
			idLib::FatalError( "%s: bad texture type %d", GetName(), opts.textureType );
			return;
	}

	// Quake 4 normal maps are typically sampled from RGB, but some legacy ARB programs
	// read Nx from alpha. Keep alpha in sync for TD_BUMP without altering RGB channels.
	const bool duplicateBumpXToAlpha = ( usage == TD_BUMP && opts.colorFormat != CFM_NORMAL_DXT5 );

	// ALPHA, LUMINANCE, LUMINANCE_ALPHA, and INTENSITY have been removed
	// in OpenGL 3.2. In order to mimic those modes, we use the swizzle operators
	// Exported GL entry points do not imply texture swizzle support on a
	// legacy desktop context. ES 3.0 has swizzles in core under its own
	// version numbering; desktop contexts retain the actual-context gate.
	const bool esTextureSwizzle = glConfig.backendCaps.profile == RENDERER_CONTEXT_PROFILE_ES &&
		glConfig.backendCaps.glVersion >= 3.0f;
	if ( esTextureSwizzle || glConfig.backendCaps.glVersion >= 3.3f ||
		GLCapabilityProbe_HasExtension( "GL_ARB_texture_swizzle" ) ||
		GLCapabilityProbe_HasExtension( "GL_EXT_texture_swizzle" ) ) {
#if defined( OPENQ4_GL_SWIZZLED_LEGACY_FORMATS )
	if ( opts.colorFormat == CFM_GREEN_ALPHA ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_GREEN );
	} else if ( opts.format == FMT_LUM8 ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_ONE );
	} else if ( opts.format == FMT_L8A8 ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_GREEN );
	} else if ( opts.format == FMT_ALPHA ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_RED );
	} else if ( opts.format == FMT_INT8 ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_RED );
	} else if ( opts.format == FMT_XRGB8 ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_GREEN );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_BLUE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_ONE );
	} else if ( duplicateBumpXToAlpha ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_GREEN );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_BLUE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_RED );
	} else {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_GREEN );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_BLUE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_ALPHA );
	}
#else
	if ( opts.colorFormat == CFM_GREEN_ALPHA ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_GREEN );
	} else if ( opts.format == FMT_ALPHA ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_RED );
	} else if ( duplicateBumpXToAlpha ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_GREEN );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_BLUE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_RED );
	}
#endif
	}

	const bool hasMipChain = opts.numLevels > 1;

	const imageFilterState_t defaultFilter = R_GetDefaultImageFilterState();
	switch( filter ) {
		case TF_DEFAULT:
			if ( hasMipChain && defaultFilter.usesMipmaps ) {
				const int minFilter = defaultFilter.minLinear
					? ( defaultFilter.mipLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST )
					: ( defaultFilter.mipLinear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST );
				glTexParameterf( target, GL_TEXTURE_MIN_FILTER, minFilter );
			} else {
				glTexParameterf( target, GL_TEXTURE_MIN_FILTER, defaultFilter.minLinear ? GL_LINEAR : GL_NEAREST );
			}
			glTexParameterf( target, GL_TEXTURE_MAG_FILTER, defaultFilter.magLinear ? GL_LINEAR : GL_NEAREST );
			break;
		case TF_LINEAR:
			glTexParameterf( target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameterf( target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			break;
		case TF_NEAREST:
			glTexParameterf( target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			glTexParameterf( target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			break;
		default:
			common->FatalError( "%s: bad texture filter %d", GetName(), filter );
	}

	{
		// only do aniso filtering on mip mapped images
		if ( filter == TF_DEFAULT && hasMipChain && defaultFilter.usesMipmaps && defaultFilter.minLinear ) {
			const float requestedAniso = static_cast<float>( Max( 1, cvarSystem->GetCVarInteger( "image_anisotropy" ) ) );
			const float aniso = Min( requestedAniso, Max( 1.0f, glConfig.maxTextureAnisotropy ) );
			glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso );
		} else if ( glConfig.anisotropicAvailable ) {
			glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );
		}
	}
	//if ( glConfig.textureLODBiasAvailable && ( usage != TD_FONT ) ) {
	//	// use a blurring LOD bias in combination with high anisotropy to fix our aliasing grate textures...
	//	glTexParameterf(target, GL_TEXTURE_LOD_BIAS_EXT, r_lodBias.GetFloat() );
	//}

	// Border clamp is optional on ES 3.0. When absent, use the authored
	// texture edge instead of leaving the driver's default repeat mode active.
	const bool borderClampSupported = glConfig.backendCaps.profile != RENDERER_CONTEXT_PROFILE_ES ||
		glConfig.backendCaps.glVersion >= 3.2f ||
		GLCapabilityProbe_HasExtension( "GL_EXT_texture_border_clamp" ) ||
		GLCapabilityProbe_HasExtension( "GL_OES_texture_border_clamp" );
	const GLenum borderWrap = borderClampSupported ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE;

	// set the wrap/clamp modes
	switch( repeat ) {
		case TR_REPEAT:
			glTexParameterf( target, GL_TEXTURE_WRAP_S, GL_REPEAT );
			glTexParameterf( target, GL_TEXTURE_WRAP_T, GL_REPEAT );
			break;
		case TR_MIRRORED_REPEAT:
			glTexParameterf( target, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT );
			glTexParameterf( target, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT );
			break;
		case TR_CLAMP_TO_ZERO: {
			float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			if ( borderClampSupported ) {
				glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, color );
			}
			glTexParameterf( target, GL_TEXTURE_WRAP_S, borderWrap );
			glTexParameterf( target, GL_TEXTURE_WRAP_T, borderWrap );
			}
			break;
		case TR_CLAMP_TO_ZERO_ALPHA: {
			float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if ( borderClampSupported ) {
				glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, color );
			}
			glTexParameterf( target, GL_TEXTURE_WRAP_S, borderWrap );
			glTexParameterf( target, GL_TEXTURE_WRAP_T, borderWrap );
			}
			break;
		case TR_CLAMP:
			glTexParameterf( target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameterf( target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
			break;
		default:
			common->FatalError( "%s: bad texture repeat %d", GetName(), repeat );
	}
}

void idImage::RefreshSamplerState() {
	if ( !IsLoaded() ) {
		return;
	}
	R_BindTextureForDirectAccess( ( opts.textureType == TT_CUBIC ) ? GL_TEXTURE_CUBE_MAP_EXT : GL_TEXTURE_2D, texnum );
	SetTexParameters();
}

/*
========================
idImage::AllocImage

Every image will pass through this function. Allocates all the necessary MipMap levels for the 
Image, but doesn't put anything in them.

This should not be done during normal game-play, if you can avoid it.
========================
*/
void idImage::AllocImage() {
	GL_CheckErrors();
	PurgeImage();
	storageGeneration++;

	// openQ4 still follows the stock Quake 4 renderer's legacy SDR lighting path.
	// Enabling selective sRGB decode without a full renderer-wide linear workflow
	// changes the baseline image significantly, so keep stock texture sampling
	// behavior for now and reserve strict sRGB texture decode for future work.
	const bool useSRGBTextureDecode = false;

	switch ( opts.format ) {
	case FMT_RGBA8:
		internalFormat = useSRGBTextureDecode ? GL_SRGB8_ALPHA8 : GL_RGBA8;
		dataFormat = GL_RGBA;
		dataType = GL_UNSIGNED_BYTE;
		break;
	case FMT_RGBA16F:
		internalFormat = GL_RGBA16F;
		dataFormat = GL_RGBA;
		dataType = GL_HALF_FLOAT;
		break;
	case FMT_XRGB8:
#if defined( OPENQ4_RENDERER_GLES_MODULE )
		// XRGB source/cache pixels have four bytes. ES requires the upload
		// format to match storage; allocate RGBA and swizzle alpha to one.
		internalFormat = useSRGBTextureDecode ? GL_SRGB8_ALPHA8 : GL_RGBA8;
#else
		internalFormat = useSRGBTextureDecode ? GL_SRGB8 : GL_RGB;
#endif
		dataFormat = GL_RGBA;
		dataType = GL_UNSIGNED_BYTE;
		break;
	case FMT_RGB565:
		internalFormat = GL_RGB;
		dataFormat = GL_RGB;
		dataType = GL_UNSIGNED_SHORT_5_6_5;
		break;
	case FMT_ALPHA:
#if defined( OPENQ4_GL_SWIZZLED_LEGACY_FORMATS )
		internalFormat = GL_R8;
		dataFormat = GL_RED;
#else
		internalFormat = GL_ALPHA8;
		dataFormat = GL_ALPHA;
#endif
		dataType = GL_UNSIGNED_BYTE;
		break;
	case FMT_L8A8:
#if defined( OPENQ4_GL_SWIZZLED_LEGACY_FORMATS )
		internalFormat = GL_RG8;
		dataFormat = GL_RG;
#else
		internalFormat = GL_LUMINANCE8_ALPHA8;
		dataFormat = GL_LUMINANCE_ALPHA;
#endif
		dataType = GL_UNSIGNED_BYTE;
		break;
	case FMT_LUM8:
#if defined( OPENQ4_GL_SWIZZLED_LEGACY_FORMATS )
		internalFormat = GL_R8;
		dataFormat = GL_RED;
#else
		internalFormat = GL_LUMINANCE8;
		dataFormat = GL_LUMINANCE;
#endif
		dataType = GL_UNSIGNED_BYTE;
		break;
	case FMT_INT8:
#if defined( OPENQ4_GL_SWIZZLED_LEGACY_FORMATS )
		internalFormat = GL_R8;
		dataFormat = GL_RED;
#else
		internalFormat = GL_INTENSITY8;
		dataFormat = GL_LUMINANCE;
#endif
		dataType = GL_UNSIGNED_BYTE;
		break;
	case FMT_DXT1:
		internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
		dataFormat = GL_RGBA;
		dataType = GL_UNSIGNED_BYTE;
		break;
	case FMT_DXT5:
		internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
		dataFormat = GL_RGBA;
		dataType = GL_UNSIGNED_BYTE;
		break;
	case FMT_BC7:
		if ( !glConfig.bptcTextureCompressionAvailable ) {
			// This used to be a fatal idLib::Error raised from inside texture
			// upload, i.e. in the middle of a map load. Every other BC7 gate in
			// the tree warns and degrades (R_BinaryImageHeaderSupportedByRenderer,
			// the lightgrid chunk loader), so match them: an unexpected BC7 image
			// on a driver without BPTC must cost one texture, not the session.
			common->Warning( "%s holds BC7/BPTC data but this renderer does not expose BPTC; uploading as uncompressed RGBA8", GetName() );
			internalFormat = GL_RGBA8;
			dataFormat = GL_RGBA;
			dataType = GL_UNSIGNED_BYTE;
			break;
		}
		internalFormat = GL_COMPRESSED_RGBA_BPTC_UNORM;
		dataFormat = GL_RGBA;
		dataType = GL_UNSIGNED_BYTE;
		break;
	case FMT_ETC2_RGB8:
	case FMT_ETC2_RGBA8:
	case FMT_EAC_RG11:
		if ( !glConfig.etc2TextureCompressionAvailable ) {
			// Same shape as the BC7 gate above: degrade one texture rather than
			// take the session down from inside a mid-load upload. Reaching here
			// means a generated cache file outlived the context that produced
			// it, which R_BinaryImageHeaderSupportedByRenderer should have
			// caught first.
			common->Warning( "%s holds ETC2/EAC data but this renderer does not expose ETC2; uploading as uncompressed RGBA8", GetName() );
			internalFormat = GL_RGBA8;
			dataFormat = GL_RGBA;
			dataType = GL_UNSIGNED_BYTE;
			break;
		}
		if ( opts.format == FMT_ETC2_RGB8 ) {
			internalFormat = GL_COMPRESSED_RGB8_ETC2;
		} else if ( opts.format == FMT_ETC2_RGBA8 ) {
			internalFormat = GL_COMPRESSED_RGBA8_ETC2_EAC;
		} else {
			internalFormat = GL_COMPRESSED_RG11_EAC;
		}
		dataFormat = GL_RGBA;
		dataType = GL_UNSIGNED_BYTE;
		break;
	case FMT_DEPTH:
#if defined( OPENQ4_RENDERER_GLES_MODULE )
		// ES rejects UNSIGNED_BYTE depth uploads, including storage-only NULL
		// uploads. Use a sized renderable depth format with a valid data type.
		internalFormat = GL_DEPTH_COMPONENT24;
		dataFormat = GL_DEPTH_COMPONENT;
		dataType = GL_UNSIGNED_INT;
#else
		internalFormat = GL_DEPTH_COMPONENT;
		dataFormat = GL_DEPTH_COMPONENT;
		dataType = GL_UNSIGNED_BYTE;
#endif
		break;
	case FMT_DEPTH_STENCIL:
		internalFormat = GL_DEPTH24_STENCIL8;
		dataFormat = GL_DEPTH_STENCIL;
		dataType = GL_UNSIGNED_INT_24_8;
		break;
	case FMT_X16:
		internalFormat = GL_INTENSITY16;
		dataFormat = GL_LUMINANCE;
		dataType = GL_UNSIGNED_SHORT;
		break;
	case FMT_Y16_X16:
		internalFormat = GL_LUMINANCE16_ALPHA16;
		dataFormat = GL_LUMINANCE_ALPHA;
		dataType = GL_UNSIGNED_SHORT;
		break;
	default:
		idLib::Error( "Unhandled image format %d in %s\n", opts.format, GetName() );
	}

	// if we don't have a rendering context, just return after we
	// have filled in the parms.  We must have the values set, or
	// an image match from a shader before OpenGL starts would miss
	// the generated texture
	if ( !tr.IsOpenGLRunning()) {
		return;
	}

	// generate the texture number
	glGenTextures( 1, (GLuint *)&texnum );
	assert( texnum != TEXTURE_NOT_LOADED );

	//----------------------------------------------------
	// allocate all the mip levels with NULL data
	//----------------------------------------------------

	int numSides;
	int target;
	int uploadTarget;
	bool wantsMSAA = ( opts.textureType == TT_2D && opts.numMSAASamples > 0 );
	if ( wantsMSAA ) {
		if ( glTexImage2DMultisample == NULL || !( glConfig.backendCaps.glVersion >= 3.2f ||
			GLCapabilityProbe_HasExtension( "GL_ARB_texture_multisample" ) ) ) {
			common->Warning( "MSAA textures not supported, disabling for %s", GetName() );
			opts.numMSAASamples = 0;
			wantsMSAA = false;
		}
	}
	if ( opts.textureType == TT_2D ) {
// jmarshall
		if ( !wantsMSAA ) {
			target = uploadTarget = GL_TEXTURE_2D;
		}
		else {
			target = uploadTarget = GL_TEXTURE_2D_MULTISAMPLE;
		}
// jmarshall end
		numSides = 1;
	} else if ( opts.textureType == TT_CUBIC ) {
		target = GL_TEXTURE_CUBE_MAP_EXT;
		uploadTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT;
		numSides = 6;
	} else {
		assert( !"opts.textureType" );
		target = uploadTarget = GL_TEXTURE_2D;
		numSides = 1;
	}

	R_BindTextureForDirectAccess( target, texnum );

	if ( wantsMSAA ) {
		int samples = opts.numMSAASamples;
#ifdef GL_MAX_SAMPLES
		GLint maxSamples = 0;
		glGetIntegerv( GL_MAX_SAMPLES, &maxSamples );
		if ( maxSamples > 0 && samples > maxSamples ) {
			common->Warning( "Requested %d MSAA samples, clamping to %d for %s", samples, maxSamples, GetName() );
			samples = maxSamples;
			opts.numMSAASamples = maxSamples;
		}
#endif
		glTexImage2DMultisample( GL_TEXTURE_2D_MULTISAMPLE, samples, internalFormat, opts.width, opts.height, GL_TRUE );
		GL_CheckErrors();
		return;
	}

	for ( int side = 0; side < numSides; side++ ) {
		int w = opts.width;
		int h = opts.height;
		if ( opts.textureType == TT_CUBIC ) {
			h = w;
		}
		for ( int level = 0; level < opts.numLevels; level++ ) {

			// clear out any previous error
			GL_CheckErrors();

			if ( IsCompressed() ) {
				const int compressedSize = R_CompressedTextureSizeInBytes( opts.format, w, h );

				// Even though the OpenGL specification allows the 'data' pointer to be NULL, for some
				// drivers we actually need to upload data to get it to allocate the texture.
				// However, on 32-bit systems we may fail to allocate a large block of memory for large
				// textures. We handle this case by using HeapAlloc directly and allowing the allocation
				// to fail in which case we simply pass down NULL to glCompressedTexImage2D and hope for the best.
				// As of 2011-10-6 using NVIDIA hardware and drivers we have to allocate the memory with HeapAlloc
				// with the exact size otherwise large image allocation (for instance for physical page textures)
				// may fail on Vista 32-bit.
				void * data = NULL;
#if defined(_WIN32)
				data = HeapAlloc( GetProcessHeap(), 0, compressedSize );
#else
				data = malloc( compressedSize );
#endif
				glCompressedTexImage2DARB( uploadTarget+side, level, internalFormat, w, h, 0, compressedSize, data );
				if ( data != NULL ) {
#if defined(_WIN32)
					HeapFree( GetProcessHeap(), 0, data );
#else
					free( data );
#endif
				}
			} else {
				glTexImage2D( uploadTarget + side, level, internalFormat, w, h, 0, dataFormat, dataType, NULL );
			}

			GL_CheckErrors();

			w = Max( 1, w >> 1 );
			h = Max( 1, h >> 1 );
		}
	}

	glTexParameteri( target, GL_TEXTURE_MAX_LEVEL, opts.numLevels - 1 );

	// see if we messed anything up
	GL_CheckErrors();

	SetTexParameters();

	GL_CheckErrors();
}

/*
========================
idImage::PurgeImage
========================
*/
void idImage::PurgeImage() {
	if ( texnum != TEXTURE_NOT_LOADED ) {
		glDeleteTextures( 1, (GLuint *)&texnum );	// this should be the ONLY place it is ever called!
		texnum = TEXTURE_NOT_LOADED;
	}
	// clear all the current binding caches, so the next bind will do a real one
	for ( int i = 0 ; i < MAX_MULTITEXTURE_UNITS ; i++ ) {
		backEnd.glState.tmu[i].current2DMap = TEXTURE_NOT_LOADED;
		backEnd.glState.tmu[i].currentCubeMap = TEXTURE_NOT_LOADED;
	}
}

/*
========================
idImage::Resize
========================
*/
void idImage::Resize( int width, int height ) {
	if ( opts.width == width && opts.height == height ) {
		return;
	}
	opts.width = width;
	opts.height = height;
	AllocImage();
}
