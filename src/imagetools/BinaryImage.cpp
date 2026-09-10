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

	idBinaryImage

================================================================================================
*/

#include "../renderer/Image.h"
#include "../idlib/CryptoHash.h"
#include "DXT/DXTCodec.h"
#include "ETC/ETCCodec.h"
#include "Color/ColorSpace.h"

idCVar image_highQualityCompression( "image_highQualityCompression", "0", CVAR_BOOL, "Use high quality (slow) compression" );
idCVar image_writeGeneratedImages( "image_writeGeneratedImages", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "write generated binary image cache files during runtime loads" );
idCVar image_showGeneratedImageWrites( "image_showGeneratedImageWrites", "0", CVAR_RENDERER | CVAR_BOOL, "print each generated binary image cache write" );

static const int MAX_BINARY_IMAGE_DIMENSION = 32768;
static const int MAX_BINARY_IMAGE_LEVELS = 32;
static const int MAX_BINARY_IMAGE_DATA_SIZE = 1 << 30;

/*
========================
R_ShouldWriteGeneratedImages

Only the CPU decode/compress path reaches WriteGeneratedFile - anything served
straight from a precompressed DDS bypasses the generated file entirely - so this
caches exactly the images that are expensive to produce. On stock retail data
that is a few dozen image programs (~17 MB for a large single-player map);
without a usable DDS fast path (no S3TC, image_usePrecompressedTextures 0 or 2,
cube maps, custom content) it is the whole set.

This defaults on because the cache is otherwise never populated: no generated/
tree ships, and com_productionMode is 0 for players, so leaving writes off meant
every load re-decoded and re-compressed the same sources and threw the result
away. Measured on maps/game/mcc_1, steady-state loads: ~10.5s -> ~7.8s with
retail DDS available, and ~19.1s -> ~6.4s without it. The cache costs ~17 MB per
map on retail data, but ~550 MB per map when the whole set has to go through the
CPU path, and each distinct downsize signature stores its own copy.
========================
*/
static bool R_ShouldWriteGeneratedImages() {
	return image_writeGeneratedImages.GetBool() || cvarSystem->GetCVarBool( "com_makingBuild" );
}

/*
========================
R_MakeCompactBinaryImageFileName

The ordinary generated qpath remains the public/cache-package identity. This
fixed-length secondary identity is consulted only after that cache is absent or
invalid, and is written only after the ordinary savepath cannot be opened. This
lets deeply nested save roots populate a runtime cache without changing existing
generated-tree priority. The version covers both normalization and path layout;
bump it if either changes.
========================
*/
static void R_MakeCompactBinaryImageFileName( idStr &compactFileName, const char *logicalName ) {
	idStr normalizedName = logicalName != NULL ? logicalName : "";
	normalizedName.BackSlashesToSlashes();
	normalizedName.ToLower();

	std::uint8_t digest[ idCrypto::SHA256_DIGEST_BYTES ];
	idCrypto::SHA256( normalizedName.c_str(), normalizedName.Length(), digest );

	static const char hexDigits[] = "0123456789abcdef";
	char digestHex[ 16 * 2 + 1 ];
	for ( int i = 0; i < 16; i++ ) {
		digestHex[ i * 2 + 0 ] = hexDigits[ digest[ i ] >> 4 ];
		digestHex[ i * 2 + 1 ] = hexDigits[ digest[ i ] & 15 ];
	}
	digestHex[ sizeof( digestHex ) - 1 ] = '\0';
	compactFileName = va( "generated/images/_compact/v1/%s.bimage", digestHex );
}

static bool R_BinaryImageFormatIsBlockCompressed( textureFormat_t format ) {
	return BytesPerBlockForFormat( format ) > 0;
}

static int R_BinaryImageMinimumDataSize( textureFormat_t format, int width, int height ) {
	if ( width <= 0 || height <= 0 ) {
		return 0;
	}

	const int bitsForFormat = BitsForFormat( format );
	if ( bitsForFormat <= 0 ) {
		return 0;
	}

	int64 dataSize = 0;
	if ( R_BinaryImageFormatIsBlockCompressed( format ) ) {
		const int64 blocksWide = Max( (int64)1, ( (int64)width + 3 ) >> 2 );
		const int64 blocksHigh = Max( (int64)1, ( (int64)height + 3 ) >> 2 );
		const int64 bytesPerBlock = BytesPerBlockForFormat( format );
		dataSize = blocksWide * blocksHigh * bytesPerBlock;
	} else {
		dataSize = ( (int64)width * height * bitsForFormat + 7 ) / 8;
	}

	if ( dataSize <= 0 || dataSize > MAX_BINARY_IMAGE_DATA_SIZE ) {
		return 0;
	}
	return (int)dataSize;
}

/*
========================
idBinaryImage::Clear
========================
*/
void idBinaryImage::Clear() {
	images.Clear();
	if ( loadedFileData != NULL ) {
		Mem_Free( loadedFileData );
		loadedFileData = NULL;
	}
	memset( &fileData, 0, sizeof( fileData ) );
}

static void R_PadRGBAImageTo4x4Blocks( const byte *src, int width, int height,
		byte *dest, int paddedWidth, int paddedHeight ) {
	assert( width > 0 && height > 0 && paddedWidth >= width && paddedHeight >= height );
	for ( int y = 0; y < paddedHeight; ++y ) {
		const byte *sourceRow = src + (size_t)Min( y, height - 1 ) * width * 4;
		byte *destRow = dest + (size_t)y * paddedWidth * 4;
		memcpy( destRow, sourceRow, (size_t)width * 4 );
		for ( int x = width; x < paddedWidth; ++x ) {
			memcpy( destRow + (size_t)x * 4, sourceRow + (size_t)( width - 1 ) * 4, 4 );
		}
	}
}

/*
========================
idBinaryImage::Load2DFromMemory
========================
*/
void idBinaryImage::Load2DFromMemory( int width, int height, const byte * pic_const, int numLevels, textureFormat_t & textureFormat, textureColor_t & colorFormat, bool gammaMips, bool filterNeutralAlpha ) {
	Clear();

	fileData.textureType = TT_2D;
	fileData.format = textureFormat;
	fileData.colorFormat = colorFormat;
	fileData.width = width;
	fileData.height = height;
	fileData.numLevels = numLevels;

	// Work from the caller's buffer directly; take a mutable copy only when a
	// color-format conversion has to rewrite the source pixels. 'pic' becomes
	// owned at the first buffer this function allocates (the conversion copy or
	// the first mip downsample).
	const byte * pic = pic_const;
	bool picOwned = false;

	const bool needsMutableCopy =
		( colorFormat == CFM_YCOCG_DXT5 ) ||
		( colorFormat == CFM_NORMAL_DXT5 && !image_highQualityCompression.GetBool() ) ||
		( colorFormat == CFM_GREEN_ALPHA );
	if ( needsMutableCopy ) {
		byte * converted = (byte *)Mem_Alloc( width * height * 4 );
		memcpy( converted, pic_const, width * height * 4 );
		if ( colorFormat == CFM_YCOCG_DXT5 ) {
			// convert the image data to YCoCg and use the YCoCgDXT5 compressor
			idColorSpace::ConvertRGBToCoCg_Y( converted, converted, width, height );
		} else if ( colorFormat == CFM_NORMAL_DXT5 ) {
			// Blah, HQ swizzles automatically, Fast doesn't
			for ( int i = 0; i < width * height; i++ ) {
				converted[i*4+3] = converted[i*4+0];
				converted[i*4+0] = 0;
				converted[i*4+2] = 0;
			}
		} else {
			for ( int i = 0; i < width * height; i++ ) {
				converted[i*4+1] = converted[i*4+3];
				converted[i*4+0] = 0;
				converted[i*4+2] = 0;
				converted[i*4+3] = 0;
			}
		}
		pic = converted;
		picOwned = true;
	}

	int	scaledWidth = width;
	int scaledHeight = height;
	images.SetNum( numLevels );
	for ( int level = 0; level < images.Num(); level++ ) {
		idBinaryImageData &img = images[ level ];
		const byte *uploadPic = pic;

		if ( filterNeutralAlpha ) {
			byte * filtered = (byte *)Mem_Alloc( scaledWidth * scaledHeight * 4 );
			memcpy( filtered, pic, scaledWidth * scaledHeight * 4 );
			R_ApplyFilterNeutralAlpha( filtered, scaledWidth * scaledHeight );
			uploadPic = filtered;
		}

		// Images that are going to be block compressed and aren't multiples of 4
		// need to be padded out before compressing. ETC2 and EAC use the same
		// 4x4 blocks as DXT, so they take the same padding.
		const byte * dxtPic = uploadPic;
		int	dxtWidth = 0;
		int	dxtHeight = 0;
		if ( textureFormat == FMT_DXT5 || textureFormat == FMT_DXT1 ||
			 textureFormat == FMT_ETC2_RGB8 || textureFormat == FMT_ETC2_RGBA8 ||
			 textureFormat == FMT_EAC_RG11 ) {
			if ( ( scaledWidth & 3 ) || ( scaledHeight & 3 ) ) {
				dxtWidth = ( scaledWidth + 3 ) & ~3;
				dxtHeight = ( scaledHeight + 3 ) & ~3;
				byte * padded = (byte *)Mem_Alloc( dxtWidth*4*dxtHeight );
				// Out-of-image texels still influence the block fit. Replicate the
				// edge instead of introducing black/transparent samples, especially
				// for the 1x1 and 2x2 mip levels used by distant surfaces.
				R_PadRGBAImageTo4x4Blocks( uploadPic, scaledWidth, scaledHeight, padded, dxtWidth, dxtHeight );
				dxtPic = padded;
			} else {
				dxtPic = uploadPic;
				dxtWidth = scaledWidth;
				dxtHeight = scaledHeight;
			}
		}

		img.level = level;
		img.destZ = 0;
		img.width = scaledWidth;
		img.height = scaledHeight;

		// compress data or convert floats as necessary
		if ( textureFormat == FMT_DXT1 ) {
			idDxtEncoder dxt;
			img.Alloc( dxtWidth * dxtHeight / 2 );
			if ( image_highQualityCompression.GetBool() ) {
				dxt.CompressImageDXT1HQ( dxtPic, img.data, dxtWidth, dxtHeight );
			} else {
				dxt.CompressImageDXT1Fast( dxtPic, img.data, dxtWidth, dxtHeight );
			}
		} else if ( textureFormat == FMT_DXT5 ) {
			idDxtEncoder dxt;
			img.Alloc( dxtWidth * dxtHeight );
			if ( colorFormat == CFM_NORMAL_DXT5 ) {
				if ( image_highQualityCompression.GetBool() ) {
					dxt.CompressNormalMapDXT5HQ( dxtPic, img.data, dxtWidth, dxtHeight );
				} else {
					dxt.CompressNormalMapDXT5Fast( dxtPic, img.data, dxtWidth, dxtHeight );
				}
			} else if ( colorFormat == CFM_YCOCG_DXT5 ) {
				if ( image_highQualityCompression.GetBool() ) {
					dxt.CompressYCoCgDXT5HQ( dxtPic, img.data, dxtWidth, dxtHeight );
				} else {
					dxt.CompressYCoCgDXT5Fast( dxtPic, img.data, dxtWidth, dxtHeight );
				}
			} else {
				fileData.colorFormat = colorFormat = CFM_DEFAULT;
				if ( image_highQualityCompression.GetBool() ) {
					dxt.CompressImageDXT5HQ( dxtPic, img.data, dxtWidth, dxtHeight );
				} else {
					dxt.CompressImageDXT5Fast( dxtPic, img.data, dxtWidth, dxtHeight );
				}
			}
		} else if ( textureFormat == FMT_ETC2_RGB8 ) {
			idEtcEncoder etc;
			img.Alloc( dxtWidth * dxtHeight / 2 );
			etc.CompressImageETC2_RGB8( dxtPic, img.data, dxtWidth, dxtHeight );
		} else if ( textureFormat == FMT_ETC2_RGBA8 ) {
			idEtcEncoder etc;
			img.Alloc( dxtWidth * dxtHeight );
			etc.CompressImageETC2_RGBA8( dxtPic, img.data, dxtWidth, dxtHeight );
		} else if ( textureFormat == FMT_EAC_RG11 ) {
			// Normal maps only, and they reach here with X already in red and Y
			// in green: idDxtDecoder::DecompressNormalMapDXT5 writes the decoded
			// RXGB that way, and R_HeightmapToNormalMap builds it that way. The
			// CFM_NORMAL_DXT5 pre-swizzle above would move X into alpha, but
			// DeriveOpts only ever pairs this format with CFM_DEFAULT, so it
			// does not run.
			idEtcEncoder etc;
			img.Alloc( dxtWidth * dxtHeight );
			etc.CompressImageEAC_RG11( dxtPic, img.data, dxtWidth, dxtHeight );
		} else if ( textureFormat == FMT_LUM8 || textureFormat == FMT_INT8 ) {
			// LUM8 and INT8 just read the red channel
			img.Alloc( scaledWidth * scaledHeight );
			for ( int i = 0; i < img.dataSize; i++ ) {
				img.data[ i ] = uploadPic[ i * 4 ];
			}
		} else if ( textureFormat == FMT_ALPHA ) {
			// ALPHA reads the alpha channel
			img.Alloc( scaledWidth * scaledHeight );
			for ( int i = 0; i < img.dataSize; i++ ) {
				img.data[ i ] = uploadPic[ i * 4 + 3 ];
			}
		} else if ( textureFormat == FMT_L8A8 ) {
			// L8A8 reads the alpha and red channels
			img.Alloc( scaledWidth * scaledHeight * 2 );
			for ( int i = 0; i < img.dataSize / 2; i++ ) {
				img.data[ i * 2 + 0 ] = uploadPic[ i * 4 + 0 ];
				img.data[ i * 2 + 1 ] = uploadPic[ i * 4 + 3 ];
			}
		} else if ( textureFormat == FMT_RGB565 ) {
			img.Alloc( scaledWidth * scaledHeight * 2 );
			for ( int i = 0; i < img.dataSize / 2; i++ ) {
				unsigned short color = ( ( uploadPic[ i * 4 + 0 ] >> 3 ) << 11 ) | ( ( uploadPic[ i * 4 + 1 ] >> 2 ) << 5 ) | ( uploadPic[ i * 4 + 2 ] >> 3 );
				img.data[ i * 2 + 0 ] = ( color >> 8 ) & 0xFF;
				img.data[ i * 2 + 1 ] = color & 0xFF;
			}
		} else {
			fileData.format = textureFormat = FMT_RGBA8;
			img.Alloc( scaledWidth * scaledHeight * 4 );
			memcpy( img.data, uploadPic, img.dataSize );
		}

		// if we had to pad to quads, free the padded version
		if ( uploadPic != dxtPic ) {
			Mem_Free( (void *)dxtPic );
			dxtPic = NULL;
		}
		if ( uploadPic != pic ) {
			Mem_Free( (void *)uploadPic );
			uploadPic = NULL;
		}

		// downsample for the next level; the final level has no next level to feed
		if ( level + 1 < images.Num() ) {
			byte * shrunk = NULL;
			if ( gammaMips ) {
				shrunk = R_MipMapWithGamma( pic, scaledWidth, scaledHeight );
			} else {
				shrunk = R_MipMap( pic, scaledWidth, scaledHeight );
			}
			if ( picOwned ) {
				Mem_Free( (void *)pic );
			}
			pic = shrunk;
			picOwned = true;
		}

		scaledWidth = Max( 1, scaledWidth >> 1 );
		scaledHeight = Max( 1, scaledHeight >> 1 );
	}

	if ( picOwned ) {
		Mem_Free( (void *)pic );
	}
}

/*
========================
PadImageTo4x4

DXT Compression requres a complete 4x4 block, even if the GPU will only be sampling
a subset of it, so pad to 4x4 with replicated texels to maximize compression.
========================
*/
static void PadImageTo4x4( const byte *src, int width, int height, byte dest[64] ) {
	// we probably will need to support this for non-square images, but I'll address
	// that when needed
	assert( width <= 4 && height <= 4 );
	assert( width > 0 && height > 0 );

	for ( int y = 0 ; y < 4 ; y++ ) {
		int	sy = y % height;
		for ( int x = 0 ; x < 4 ; x++ ) {
			int	sx = x % width;
			for ( int c = 0 ; c < 4 ; c++ ) {
				dest[(y*4+x)*4+c] = src[(sy*width+sx)*4+c];
			}
		}
	}
}

/*
========================
idBinaryImage::Load2DFromOwnedCompressedData

Takes ownership of fileBuffer, which must come from this binary's Mem_Alloc
family: Clear() releases it with Mem_Free (never hand over a
fileSystem->ReadFile buffer - in renderer-module builds that is a different
heap). Each level becomes a view into the buffer, mirroring
LoadFromGeneratedFile, so no per-level copies are made.
========================
*/
void idBinaryImage::Load2DFromOwnedCompressedData( int width, int height, int numLevels, textureFormat_t textureFormat, textureColor_t colorFormat, byte *fileBuffer, const int *levelOffsets, const int *levelSizes ) {
	Clear();

	fileData.textureType = TT_2D;
	fileData.format = textureFormat;
	fileData.colorFormat = colorFormat;
	fileData.width = width;
	fileData.height = height;
	fileData.numLevels = numLevels;

	loadedFileData = fileBuffer;

	images.SetNum( numLevels );
	int levelWidth = width;
	int levelHeight = height;
	for ( int level = 0; level < numLevels; level++ ) {
		idBinaryImageData &img = images[ level ];
		img.level = level;
		img.destZ = 0;
		img.width = levelWidth;
		img.height = levelHeight;
		img.SetExternalData( loadedFileData + levelOffsets[ level ], levelSizes[ level ] );

		levelWidth = Max( 1, levelWidth >> 1 );
		levelHeight = Max( 1, levelHeight >> 1 );
	}
}

/*
========================
idBinaryImage::LoadCubeFromMemory
========================
*/
void idBinaryImage::LoadCubeFromMemory( int width, const byte * pics[6], int numLevels, textureFormat_t & textureFormat, bool gammaMips ) {
	Clear();

	fileData.textureType = TT_CUBIC;
	fileData.format = textureFormat;
	fileData.colorFormat = CFM_DEFAULT;
	fileData.height = fileData.width = width;
	fileData.numLevels = numLevels;

	images.SetNum( fileData.numLevels * 6 );

	for ( int side = 0; side < 6; side++ ) {
		const byte *orig = pics[side];
		const byte *pic = orig;
		int	scaledWidth = fileData.width;
		for ( int level = 0; level < fileData.numLevels; level++ ) {
			// compress data or convert floats as necessary
			idBinaryImageData &img = images[ level * 6 + side ];

			// handle padding blocks less than 4x4 for the DXT compressors
			ALIGN16( byte padBlock[64] );
			int		padSize;
			const byte *padSrc;
			if ( scaledWidth < 4 && ( textureFormat == FMT_DXT1 || textureFormat == FMT_DXT5 ) ) {
				PadImageTo4x4( pic, scaledWidth, scaledWidth, padBlock );
				padSize = 4;
				padSrc = padBlock;
			} else {
				padSize = scaledWidth;
				padSrc = pic;
			}

			img.level = level;
			img.destZ = side;
			// The header describes the logical mip; only the compressed payload
			// is rounded up to complete blocks. Cache validation and uploads use
			// these dimensions even for the final 2x2 and 1x1 cube levels.
			img.width = scaledWidth;
			img.height = scaledWidth;
			if ( textureFormat == FMT_DXT1 ) {
				img.Alloc( padSize * padSize / 2 );
				idDxtEncoder dxt;
				dxt.CompressImageDXT1Fast( padSrc, img.data, padSize, padSize );
			} else if ( textureFormat == FMT_DXT5 ) {
				img.Alloc( padSize * padSize );
				idDxtEncoder dxt;
				dxt.CompressImageDXT5Fast( padSrc, img.data, padSize, padSize );
			} else {
				fileData.format = textureFormat = FMT_RGBA8;
				img.Alloc( padSize * padSize * 4 );
				memcpy( img.data, pic, img.dataSize );
			}

			// downsample for the next level; the final level has no next level to feed
			if ( level + 1 < fileData.numLevels ) {
				byte * shrunk = NULL;
				if ( gammaMips ) {
					shrunk = R_MipMapWithGamma( pic, scaledWidth, scaledWidth );
				} else {
					shrunk = R_MipMap( pic, scaledWidth, scaledWidth );
				}
				if ( pic != orig ) {
					Mem_Free( (void *)pic );
					pic = NULL;
				}
				pic = shrunk;
			}

			scaledWidth = Max( 1, scaledWidth >> 1 );
		}
		if ( pic != orig ) {
			// free the down sampled version
			Mem_Free( (void *)pic );
			pic = NULL;
		}
	}
}

/*
========================
idBinaryImage::WriteToFile
========================
*/
bool idBinaryImage::WriteToFile( idFile *file, ID_TIME_T sourceFileTime ) {
	if ( file == NULL ) {
		return false;
	}

	fileData.headerMagic = BIMAGE_MAGIC;
	fileData.sourceFileTime = sourceFileTime;

	if ( file->WriteBig( fileData.sourceFileTime ) != sizeof( fileData.sourceFileTime ) ||
		 file->WriteBig( fileData.headerMagic ) != sizeof( fileData.headerMagic ) ||
		 file->WriteBig( fileData.textureType ) != sizeof( fileData.textureType ) ||
		 file->WriteBig( fileData.format ) != sizeof( fileData.format ) ||
		 file->WriteBig( fileData.colorFormat ) != sizeof( fileData.colorFormat ) ||
		 file->WriteBig( fileData.width ) != sizeof( fileData.width ) ||
		 file->WriteBig( fileData.height ) != sizeof( fileData.height ) ||
		 file->WriteBig( fileData.numLevels ) != sizeof( fileData.numLevels ) ) {
		return false;
	}

	for ( int i = 0; i < images.Num(); i++ ) {
		idBinaryImageData &img = images[ i ];
		if ( file->WriteBig( img.level ) != sizeof( img.level ) ||
			 file->WriteBig( img.destZ ) != sizeof( img.destZ ) ||
			 file->WriteBig( img.width ) != sizeof( img.width ) ||
			 file->WriteBig( img.height ) != sizeof( img.height ) ||
			 file->WriteBig( img.dataSize ) != sizeof( img.dataSize ) ) {
			return false;
		}
		if ( file->Write( img.data, img.dataSize ) != img.dataSize ) {
			return false;
		}
	}
	return true;
}

/*
========================
idBinaryImage::WriteGeneratedFile
========================
*/
ID_TIME_T idBinaryImage::WriteGeneratedFile( ID_TIME_T sourceFileTime ) {
	if ( !R_ShouldWriteGeneratedImages() ) {
		return FILE_NOT_FOUND_TIMESTAMP;
	}

	idStr binaryFileName;
	MakeGeneratedFileName( binaryFileName );
	// fs_cachepath, not fs_savepath: this tree is entirely regenerable and is by
	// far the largest thing the engine writes -- hundreds of MB per map when no
	// DDS fast path is available. It still keeps long image-program names off
	// fs_basepath, which can sit under a Windows path limit in "Program Files".
	// fs_cachepath resolves to fs_savepath unless the host pointed it somewhere.
	idStr writeFileName = binaryFileName;
	idFile *outputFile = fileSystem->OpenFileWrite( writeFileName, "fs_cachepath" );
	if ( outputFile == NULL ) {
		R_MakeCompactBinaryImageFileName( writeFileName, GetName() );
		outputFile = fileSystem->OpenFileWrite( writeFileName, "fs_cachepath" );
	}
	idFileLocal file( outputFile );
	if ( file == NULL ) {
		idLib::Warning( "idBinaryImage: Could not open generated cache '%s' or compact fallback '%s'",
			binaryFileName.c_str(), writeFileName.c_str() );
		return FILE_NOT_FOUND_TIMESTAMP;
	}
	if ( image_showGeneratedImageWrites.GetBool() ) {
		idLib::Printf( "Writing %s\n", writeFileName.c_str() );
	}

	if ( !WriteToFile( file, sourceFileTime ) ) {
		return FILE_NOT_FOUND_TIMESTAMP;
	}
	return file->Timestamp();
}

/*
==========================
idBinaryImage::LoadFromGeneratedFile

Load the preprocessed image from the generated folder.
==========================
*/
ID_TIME_T idBinaryImage::LoadFromGeneratedFile( ID_TIME_T sourceFileTime ) {
	idStr binaryFileName;
	MakeGeneratedFileName( binaryFileName );
	idFileLocal bFile = fileSystem->OpenFileRead( binaryFileName );
	if ( bFile != NULL && LoadFromGeneratedFile( bFile, sourceFileTime, true ) ) {
		return bFile->Timestamp();
	}

	idStr compactFileName;
	R_MakeCompactBinaryImageFileName( compactFileName, GetName() );
	idFileLocal compactFile = fileSystem->OpenFileRead( compactFileName );
	if ( compactFile != NULL && LoadFromGeneratedFile( compactFile, sourceFileTime, true ) ) {
		return compactFile->Timestamp();
	}
	return FILE_NOT_FOUND_TIMESTAMP;
}

/*
==========================
idBinaryImage::LoadFromGeneratedFileUnchecked

Loads an existing generated image before source timestamp validation. Callers
must compare the header timestamp before using the data outside production mode.
==========================
*/
ID_TIME_T idBinaryImage::LoadFromGeneratedFileUnchecked() {
	idStr binaryFileName;
	MakeGeneratedFileName( binaryFileName );
	idFileLocal bFile = fileSystem->OpenFileRead( binaryFileName );
	if ( bFile != NULL && LoadFromGeneratedFile( bFile, FILE_NOT_FOUND_TIMESTAMP, false ) ) {
		return bFile->Timestamp();
	}
	return FILE_NOT_FOUND_TIMESTAMP;
}

/*
==========================
idBinaryImage::LoadFromCompactGeneratedFileUnchecked

Loads the fixed-length recovery identity after the ordinary generated cache was
missing or rejected. The VFS read preserves pure-server directory policy; the
ordinary generated identity remains authoritative because callers probe it first.
==========================
*/
ID_TIME_T idBinaryImage::LoadFromCompactGeneratedFileUnchecked() {
	idStr compactFileName;
	R_MakeCompactBinaryImageFileName( compactFileName, GetName() );
	idFileLocal compactFile = fileSystem->OpenFileRead( compactFileName );
	if ( compactFile != NULL && LoadFromGeneratedFile( compactFile, FILE_NOT_FOUND_TIMESTAMP, false ) ) {
		return compactFile->Timestamp();
	}
	return FILE_NOT_FOUND_TIMESTAMP;
}

/*
==========================
idBinaryImage::LoadFromFile
==========================
*/
bool idBinaryImage::LoadFromFile( idFile *file, int dataBytes ) {
	return LoadFromGeneratedFile( file, FILE_NOT_FOUND_TIMESTAMP, false, dataBytes );
}

/*
==========================
idBinaryImage::LoadFromGeneratedFile

Load the preprocessed image from the generated folder.
==========================
*/
bool idBinaryImage::LoadFromGeneratedFile( idFile * bFile, ID_TIME_T sourceFileTime, bool validateSourceFileTime, int dataBytes ) {
	Clear();

	const int fileStart = bFile->Tell();
	const int remainingBytes = bFile->Length() - fileStart;
	const int fileLength = dataBytes >= 0 ? dataBytes : remainingBytes;
	if ( fileLength > remainingBytes ) {
		return false;
	}
	if ( fileLength < (int)sizeof( fileData ) || fileLength > MAX_BINARY_IMAGE_DATA_SIZE ) {
		return false;
	}

	// Read each binary image or packed chunk once. Mip payloads then remain as
	// views into this single backing allocation instead of separate allocations.
	loadedFileData = (byte *)Mem_Alloc( fileLength );
	if ( loadedFileData == NULL ) {
		return false;
	}
	if ( bFile->Read( loadedFileData, fileLength ) != fileLength ) {
		Clear();
		return false;
	}

	byte *cursor = loadedFileData;
	const byte *fileEnd = loadedFileData + fileLength;
	memcpy( &fileData, cursor, sizeof( fileData ) );
	cursor += sizeof( fileData );

	idSwapClass<bimageFile_t> swap;
	swap.Big( fileData.sourceFileTime );
	swap.Big( fileData.headerMagic );
	swap.Big( fileData.textureType );
	swap.Big( fileData.format );
	swap.Big( fileData.colorFormat );
	swap.Big( fileData.width );
	swap.Big( fileData.height );
	swap.Big( fileData.numLevels );

	if ( BIMAGE_MAGIC != fileData.headerMagic ) {
		Clear();
		return false;
	}
	if ( validateSourceFileTime && fileData.sourceFileTime != sourceFileTime && !fileSystem->InProductionMode()) {
		Clear();
		return false;
	}
	if ( fileData.textureType != TT_2D && fileData.textureType != TT_CUBIC ) {
		Clear();
		return false;
	}
	if ( fileData.format <= FMT_NONE || fileData.format > FMT_MAX_VALID || BitsForFormat( (textureFormat_t)fileData.format ) <= 0 ) {
		Clear();
		return false;
	}
	if ( fileData.colorFormat < CFM_DEFAULT || fileData.colorFormat > CFM_GREEN_ALPHA ) {
		Clear();
		return false;
	}
	if ( fileData.width <= 0 || fileData.width > MAX_BINARY_IMAGE_DIMENSION ||
		 fileData.height <= 0 || fileData.height > MAX_BINARY_IMAGE_DIMENSION ||
		 fileData.numLevels <= 0 || fileData.numLevels > MAX_BINARY_IMAGE_LEVELS ) {
		Clear();
		return false;
	}
	if ( fileData.textureType == TT_CUBIC && fileData.width != fileData.height ) {
		Clear();
		return false;
	}

	int numImages = fileData.numLevels;
	if ( fileData.textureType == TT_CUBIC ) {
		numImages *= 6;
	}

	images.SetNum( numImages );
	bool seenImages[6][MAX_BINARY_IMAGE_LEVELS];
	memset( seenImages, 0, sizeof( seenImages ) );

	for ( int i = 0; i < numImages; i++ ) {
		idBinaryImageData &img = images[ i ];
		if ( fileEnd - cursor < (int)sizeof( bimageImage_t ) ) {
			Clear();
			return false;
		}
		memcpy( static_cast<bimageImage_t *>( &img ), cursor, sizeof( bimageImage_t ) );
		cursor += sizeof( bimageImage_t );
		idSwapClass<bimageImage_t> swap;
		swap.Big( img.level );
		swap.Big( img.destZ );
		swap.Big( img.width );
		swap.Big( img.height );
		swap.Big( img.dataSize );
		if ( img.level < 0 || img.level >= fileData.numLevels ) {
			Clear();
			return false;
		}
		if ( fileData.textureType == TT_2D ) {
			if ( img.destZ != 0 ) {
				Clear();
				return false;
			}
		} else if ( img.destZ < 0 || img.destZ >= 6 ) {
			Clear();
			return false;
		}
		const int imageSide = fileData.textureType == TT_2D ? 0 : img.destZ;
		if ( seenImages[ imageSide ][ img.level ] ) {
			Clear();
			return false;
		}
		seenImages[ imageSide ][ img.level ] = true;

		const int expectedWidth = Max( 1, fileData.width >> img.level );
		const int expectedHeight = fileData.textureType == TT_CUBIC ? expectedWidth : Max( 1, fileData.height >> img.level );
		if ( img.width <= 0 || img.width > MAX_BINARY_IMAGE_DIMENSION || img.height <= 0 || img.height > MAX_BINARY_IMAGE_DIMENSION ) {
			Clear();
			return false;
		}
		if ( img.width != expectedWidth || img.height != expectedHeight ) {
			Clear();
			return false;
		}
		if ( img.dataSize <= 0 || img.dataSize > MAX_BINARY_IMAGE_DATA_SIZE ) {
			Clear();
			return false;
		}
		const int expectedDataSize = R_BinaryImageMinimumDataSize( (textureFormat_t)fileData.format, expectedWidth, expectedHeight );
		if ( expectedDataSize <= 0 || img.dataSize != expectedDataSize ) {
			Clear();
			return false;
		}
		if ( fileEnd - cursor < img.dataSize ) {
			Clear();
			return false;
		}
		img.SetExternalData( cursor, img.dataSize );
		cursor += img.dataSize;
	}

	if ( cursor != fileEnd ) {
		Clear();
		return false;
	}

	return true;
}

/*
==========================
idBinaryImage::MakeGeneratedFileName
==========================
*/
void idBinaryImage::MakeGeneratedFileName( idStr & gfn ) {
	GetGeneratedFileName( gfn, GetName() );
}
/*
==========================
idBinaryImage::GetGeneratedFileName
==========================
*/
static bool R_BinaryImageNameNeedsHashedPath( const char *name, const idStr &legacyPath ) {
	if ( name == NULL ) {
		return true;
	}
	if ( legacyPath.Length() > 180 ) {
		return true;
	}
	for ( const char *s = name; *s != '\0'; s++ ) {
		if ( *s == '(' || *s == ')' || *s == ',' || *s == ' ' ) {
			return true;
		}
	}
	return false;
}

static void R_BinaryImageSanitizedPrefix( idStr &prefix, const idStr &name ) {
	prefix.Clear();

	bool lastWasSeparator = false;
	for ( int i = 0; i < name.Length() && prefix.Length() < 48; i++ ) {
		const char c = name[ i ];
		if ( idStr::CharIsAlpha( (byte)c ) || idStr::CharIsNumeric( (byte)c ) ) {
			prefix.Append( c );
			lastWasSeparator = false;
		} else if ( !lastWasSeparator && prefix.Length() > 0 ) {
			prefix.Append( '_' );
			lastWasSeparator = true;
		}
	}

	while ( prefix.Length() > 0 && prefix[ prefix.Length() - 1 ] == '_' ) {
		prefix.CapLength( prefix.Length() - 1 );
	}
	if ( prefix.IsEmpty() ) {
		prefix = "image_program";
	}
}

void idBinaryImage::GetGeneratedFileName( idStr & gfn, const char *name ) {
	gfn = va( "generated/images/%s.bimage", name );
	gfn.Replace( "(", "/" );
	gfn.Replace( ",", "/" );
	gfn.Replace( ")", "" );
	gfn.Replace( " ", "" );
	gfn.ToLower();

	if ( !R_BinaryImageNameNeedsHashedPath( name, gfn ) ) {
		return;
	}

	idStr normalizedName = name != NULL ? name : "";
	normalizedName.ToLower();

	idStr prefix;
	R_BinaryImageSanitizedPrefix( prefix, normalizedName );

	const uint32_t crc = CRC32_BlockChecksum( normalizedName.c_str(), normalizedName.Length() );
	gfn = va( "generated/images/_programs/%s_%08x.bimage", prefix.c_str(), static_cast<unsigned int>( crc ) );
}
