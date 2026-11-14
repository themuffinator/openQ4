// Copyright (C) 2026 DarkMatter Productions
//

#include "../../idlib/precompiled.h"
#include "PNGCodec.h"
#include "PNGFilters.h"
#include "ZlibStream.h"
#include "../../idlib/hashing/CRC32.h"

#include <stdlib.h>
#include <string.h>

/*
===============================================================================

	PNG decoder

	1. Walk the chunks: IHDR, PLTE and tRNS set up the conversion, IDAT
	   chunks are gathered (in place when there is only one).
	2. Inflate the whole filtered image in one call into a buffer of its
	   exact size.
	3. Per row: undo the filter, then convert to RGBA8. Non-interlaced 8-bit
	   RGBA rows are unfiltered straight into the destination, with the
	   destination's previous row as the prior row, so that common case
	   makes a single pass over the pixels.

===============================================================================
*/

static const uint8_t PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
static const uint32_t PNG_MAX_DIMENSION = 0x7FFFFFFFu;

static inline uint32_t ReadBE32( const uint8_t *p ) {
	return ( (uint32_t)p[0] << 24 ) | ( (uint32_t)p[1] << 16 ) | ( (uint32_t)p[2] << 8 ) | p[3];
}

static inline uint32_t ReadBE16( const uint8_t *p ) {
	return ( (uint32_t)p[0] << 8 ) | p[1];
}

static const uint32_t CHUNK_IHDR = 0x49484452u;
static const uint32_t CHUNK_PLTE = 0x504C5445u;
static const uint32_t CHUNK_tRNS = 0x74524E53u;
static const uint32_t CHUNK_IDAT = 0x49444154u;
static const uint32_t CHUNK_IEND = 0x49454E44u;

typedef struct {
	uint32_t		type;
	uint32_t		length;
	const uint8_t *	data;
} pngChunk_t;

// Reads the chunk at *cursor. Returns false when the file ends inside it.
static bool NextChunk( const uint8_t *file, size_t size, size_t *cursor, pngChunk_t &chunk ) {
	if ( size - *cursor < 12 ) {
		return false;
	}
	const uint8_t *p = file + *cursor;
	chunk.length = ReadBE32( p );
	chunk.type = ReadBE32( p + 4 );
	chunk.data = p + 8;
	if ( chunk.length > 0x7FFFFFFFu || (uint64_t)chunk.length + 12 > size - *cursor ) {
		return false;
	}
	*cursor += 12 + (size_t)chunk.length;
	return true;
}

static bool ChunkCRCValid( const pngChunk_t &chunk ) {
	const uint32_t stored = ReadBE32( chunk.data + chunk.length );
	return CRC32_BlockChecksum( chunk.data - 4, (int)( chunk.length + 4 ) ) == stored;
}

static inline bool ChunkIsCritical( uint32_t type ) {
	return ( type & 0x20000000u ) == 0;
}

static bool ValidBitDepth( uint32_t colorType, uint32_t depth ) {
	switch ( colorType ) {
	case PNG_COLOR_GRAY:		return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
	case PNG_COLOR_PALETTE:		return depth == 1 || depth == 2 || depth == 4 || depth == 8;
	case PNG_COLOR_RGB:
	case PNG_COLOR_GRAY_ALPHA:
	case PNG_COLOR_RGBA:		return depth == 8 || depth == 16;
	}
	return false;
}

static int ChannelCount( uint32_t colorType ) {
	switch ( colorType ) {
	case PNG_COLOR_RGB:			return 3;
	case PNG_COLOR_GRAY_ALPHA:	return 2;
	case PNG_COLOR_RGBA:		return 4;
	}
	return 1;
}

static pngResult_t ParseHeader( const uint8_t *data, size_t size, size_t *cursor, pngInfo_t &info, bool checkCRC ) {
	if ( !PNG_HasSignature( data, size ) ) {
		return PNG_ERR_SIGNATURE;
	}
	*cursor = sizeof( PNG_SIGNATURE );
	pngChunk_t chunk;
	if ( !NextChunk( data, size, cursor, chunk ) ) {
		return PNG_ERR_TRUNCATED;
	}
	if ( chunk.type != CHUNK_IHDR || chunk.length != 13 ) {
		return PNG_ERR_HEADER;
	}
	if ( checkCRC && !ChunkCRCValid( chunk ) ) {
		return PNG_ERR_CRC;
	}
	const uint8_t *h = chunk.data;
	info.width = ReadBE32( h );
	info.height = ReadBE32( h + 4 );
	info.bitDepth = h[8];
	info.colorType = h[9];
	info.interlaced = h[12];
	info.hasAlpha = info.colorType == PNG_COLOR_GRAY_ALPHA || info.colorType == PNG_COLOR_RGBA;
	if ( info.width == 0 || info.height == 0 || info.width > PNG_MAX_DIMENSION || info.height > PNG_MAX_DIMENSION ||
		 !ValidBitDepth( info.colorType, info.bitDepth ) || h[10] != 0 || h[11] != 0 || info.interlaced > 1 ) {
		return PNG_ERR_HEADER;
	}
	return PNG_OK;
}

bool PNG_HasSignature( const uint8_t *data, size_t size ) {
	return data != NULL && size >= sizeof( PNG_SIGNATURE ) && memcmp( data, PNG_SIGNATURE, sizeof( PNG_SIGNATURE ) ) == 0;
}

/*
================
PNG_ReadInfo
================
*/
pngResult_t PNG_ReadInfo( const uint8_t *data, size_t size, pngInfo_t &info ) {
	size_t cursor;
	const pngResult_t result = ParseHeader( data, size, &cursor, info, true );
	if ( result != PNG_OK ) {
		return result;
	}
	// tRNS, when present, precedes the image data
	pngChunk_t chunk;
	while ( !info.hasAlpha && NextChunk( data, size, &cursor, chunk ) && chunk.type != CHUNK_IDAT && chunk.type != CHUNK_IEND ) {
		if ( chunk.type == CHUNK_tRNS && info.colorType != PNG_COLOR_GRAY_ALPHA && info.colorType != PNG_COLOR_RGBA ) {
			info.hasAlpha = true;
		}
	}
	return PNG_OK;
}

/*
===============================================================================

	Row conversion to RGBA8

===============================================================================
*/

typedef struct {
	uint32_t	colorType;
	uint32_t	bitDepth;
	uint32_t	bitsPerPixel;
	bool		hasKey;					// tRNS colour key (greyscale / truecolour)
	uint16_t	key[3];
	uint8_t		lut[256 * 4];			// palette or low-depth greyscale, sample -> RGBA
} pngConvert_t;

static inline uint8_t Reduce16( uint32_t value ) {
	return (uint8_t)( ( value * 255u + 32767u ) / 65535u );
}

static void ConvertRow( const pngConvert_t &cv, const uint8_t *src, uint32_t width, uint8_t *dst ) {
	const uint32_t depth = cv.bitDepth;
	switch ( cv.colorType ) {
	case PNG_COLOR_RGBA:
		if ( depth == 8 ) {
			memcpy( dst, src, (size_t)width * 4 );
		} else {
			for ( uint32_t x = 0; x < width; x++, src += 8, dst += 4 ) {
				dst[0] = Reduce16( ReadBE16( src ) );
				dst[1] = Reduce16( ReadBE16( src + 2 ) );
				dst[2] = Reduce16( ReadBE16( src + 4 ) );
				dst[3] = Reduce16( ReadBE16( src + 6 ) );
			}
		}
		return;
	case PNG_COLOR_RGB:
		if ( depth == 8 ) {
			if ( cv.hasKey ) {
				for ( uint32_t x = 0; x < width; x++, src += 3, dst += 4 ) {
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
					dst[3] = ( src[0] == cv.key[0] && src[1] == cv.key[1] && src[2] == cv.key[2] ) ? 0 : 255;
				}
			} else {
				for ( uint32_t x = 0; x < width; x++, src += 3, dst += 4 ) {
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
					dst[3] = 255;
				}
			}
		} else {
			for ( uint32_t x = 0; x < width; x++, src += 6, dst += 4 ) {
				const uint32_t r = ReadBE16( src ), g = ReadBE16( src + 2 ), b = ReadBE16( src + 4 );
				dst[0] = Reduce16( r );
				dst[1] = Reduce16( g );
				dst[2] = Reduce16( b );
				dst[3] = ( cv.hasKey && r == cv.key[0] && g == cv.key[1] && b == cv.key[2] ) ? 0 : 255;
			}
		}
		return;
	case PNG_COLOR_GRAY_ALPHA:
		if ( depth == 8 ) {
			for ( uint32_t x = 0; x < width; x++, src += 2, dst += 4 ) {
				dst[0] = dst[1] = dst[2] = src[0];
				dst[3] = src[1];
			}
		} else {
			for ( uint32_t x = 0; x < width; x++, src += 4, dst += 4 ) {
				dst[0] = dst[1] = dst[2] = Reduce16( ReadBE16( src ) );
				dst[3] = Reduce16( ReadBE16( src + 2 ) );
			}
		}
		return;
	case PNG_COLOR_GRAY:
		if ( depth == 16 ) {
			for ( uint32_t x = 0; x < width; x++, src += 2, dst += 4 ) {
				const uint32_t g = ReadBE16( src );
				dst[0] = dst[1] = dst[2] = Reduce16( g );
				dst[3] = ( cv.hasKey && g == cv.key[0] ) ? 0 : 255;
			}
			return;
		}
		break;
	}

	// palette indices and greyscale up to 8 bits go through the lookup table
	if ( depth == 8 ) {
		for ( uint32_t x = 0; x < width; x++, dst += 4 ) {
			memcpy( dst, cv.lut + src[x] * 4, 4 );
		}
		return;
	}
	const uint32_t perByte = 8 / depth;
	const uint32_t mask = ( 1u << depth ) - 1;
	uint32_t x = 0;
	while ( x < width ) {
		const uint32_t packed = *src++;
		const uint32_t count = width - x < perByte ? width - x : perByte;
		for ( uint32_t i = 0; i < count; i++, dst += 4 ) {
			const uint32_t sample = ( packed >> ( 8 - depth * ( i + 1 ) ) ) & mask;
			memcpy( dst, cv.lut + sample * 4, 4 );
		}
		x += count;
	}
}

static pngResult_t BuildConversion( pngConvert_t &cv, const pngInfo_t &info, const pngChunk_t *palette, const pngChunk_t *transparency ) {
	memset( &cv, 0, sizeof( cv ) );
	cv.colorType = info.colorType;
	cv.bitDepth = info.bitDepth;
	cv.bitsPerPixel = (uint32_t)ChannelCount( info.colorType ) * info.bitDepth;

	if ( info.colorType == PNG_COLOR_PALETTE ) {
		if ( palette == NULL ) {
			return PNG_ERR_FORMAT;
		}
		const uint32_t entries = palette->length / 3;
		for ( uint32_t i = 0; i < 256; i++ ) {
			uint8_t *entry = cv.lut + i * 4;
			if ( i < entries ) {
				entry[0] = palette->data[i * 3 + 0];
				entry[1] = palette->data[i * 3 + 1];
				entry[2] = palette->data[i * 3 + 2];
			}
			// out-of-range indices are an encoder error; they decode opaque black
			entry[3] = ( transparency != NULL && i < transparency->length && i < entries ) ? transparency->data[i] : 255;
		}
		return PNG_OK;
	}

	if ( transparency != NULL ) {
		if ( info.colorType == PNG_COLOR_GRAY && transparency->length >= 2 ) {
			cv.hasKey = true;
			cv.key[0] = (uint16_t)ReadBE16( transparency->data );
		} else if ( info.colorType == PNG_COLOR_RGB && transparency->length >= 6 ) {
			cv.hasKey = true;
			cv.key[0] = (uint16_t)ReadBE16( transparency->data );
			cv.key[1] = (uint16_t)ReadBE16( transparency->data + 2 );
			cv.key[2] = (uint16_t)ReadBE16( transparency->data + 4 );
		}
	}

	if ( info.colorType == PNG_COLOR_GRAY && info.bitDepth <= 8 ) {
		const uint32_t maxValue = ( 1u << info.bitDepth ) - 1;
		for ( uint32_t v = 0; v <= maxValue; v++ ) {
			uint8_t *entry = cv.lut + v * 4;
			entry[0] = entry[1] = entry[2] = (uint8_t)( v * 255u / maxValue );
			entry[3] = ( cv.hasKey && v == cv.key[0] ) ? 0 : 255;
		}
	}
	return PNG_OK;
}

/*
===============================================================================

	Image decode

===============================================================================
*/

typedef struct {
	uint32_t	x0, y0, dx, dy;
} adam7Pass_t;

static const adam7Pass_t adam7Passes[7] = {
	{ 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 },
	{ 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 }
};

typedef struct {
	uint32_t	width;
	uint32_t	height;
	uint32_t	x0, y0, dx, dy;
	uint64_t	rowBytes;
} pngPass_t;

static inline uint64_t RowBytes( uint64_t width, uint32_t bitsPerPixel ) {
	return ( width * bitsPerPixel + 7 ) >> 3;
}

static int PlanPasses( const pngInfo_t &info, uint32_t bitsPerPixel, pngPass_t passes[7], uint64_t *filteredBytes ) {
	int count = 0;
	uint64_t total = 0;
	if ( !info.interlaced ) {
		pngPass_t &p = passes[count++];
		p.width = info.width;
		p.height = info.height;
		p.x0 = p.y0 = 0;
		p.dx = p.dy = 1;
		p.rowBytes = RowBytes( info.width, bitsPerPixel );
		total = (uint64_t)info.height * ( p.rowBytes + 1 );
	} else {
		for ( int i = 0; i < 7; i++ ) {
			const adam7Pass_t &a = adam7Passes[i];
			if ( info.width <= a.x0 || info.height <= a.y0 ) {
				continue;		// an empty pass stores nothing, not even filter bytes
			}
			pngPass_t &p = passes[count++];
			p.width = ( info.width - a.x0 + a.dx - 1 ) / a.dx;
			p.height = ( info.height - a.y0 + a.dy - 1 ) / a.dy;
			p.x0 = a.x0;
			p.y0 = a.y0;
			p.dx = a.dx;
			p.dy = a.dy;
			p.rowBytes = RowBytes( p.width, bitsPerPixel );
			total += (uint64_t)p.height * ( p.rowBytes + 1 );
		}
	}
	*filteredBytes = total;
	return count;
}

static pngResult_t MapZlibResult( zlibResult_t result ) {
	switch ( result ) {
	case ZLIB_OK:
	case ZLIB_OK_EXCESS:		return PNG_OK;
	case ZLIB_ERR_TRUNCATED:
	case ZLIB_ERR_SHORT:		return PNG_ERR_TRUNCATED;
	case ZLIB_ERR_NO_MEMORY:	return PNG_ERR_NO_MEMORY;
	default:					return PNG_ERR_FORMAT;
	}
}

/*
================
PNG_DecodeRGBA8
================
*/
pngResult_t PNG_DecodeRGBA8( const uint8_t *data, size_t size, uint8_t *rgba, size_t rgbaBytes, const pngDecodeOptions_t *options ) {
	const bool checkCRC = options == NULL || !options->skipCRC;
	if ( rgba == NULL ) {
		return PNG_ERR_ARGUMENT;
	}

	pngInfo_t info;
	size_t cursor;
	pngResult_t result = ParseHeader( data, size, &cursor, info, checkCRC );
	if ( result != PNG_OK ) {
		return result;
	}
	const uint64_t pixels = (uint64_t)info.width * info.height;
	if ( options != NULL && options->maxPixels != 0 && pixels > options->maxPixels ) {
		return PNG_ERR_TOO_LARGE;
	}
	if ( pixels > ( (uint64_t)SIZE_MAX >> 2 ) ) {
		return PNG_ERR_TOO_LARGE;
	}
	if ( rgbaBytes < pixels * 4 ) {
		return PNG_ERR_ARGUMENT;
	}

	// gather the chunks
	pngChunk_t palette, transparency, chunk;
	bool havePalette = false, haveTransparency = false;
	const uint8_t *firstData = NULL;
	uint64_t compressedBytes = 0;
	uint32_t numDataChunks = 0;
	size_t dataCursor = 0;		// first IDAT chunk position for the gather pass
	while ( cursor < size ) {
		const size_t chunkStart = cursor;
		if ( !NextChunk( data, size, &cursor, chunk ) ) {
			if ( numDataChunks == 0 ) {
				return PNG_ERR_TRUNCATED;
			}
			break;		// tolerate trailing garbage or a lost IEND after the image data
		}
		const bool critical = ChunkIsCritical( chunk.type );
		if ( chunk.type == CHUNK_IEND ) {
			break;
		}
		if ( chunk.type == CHUNK_IDAT ) {
			if ( checkCRC && !ChunkCRCValid( chunk ) ) {
				return PNG_ERR_CRC;
			}
			if ( numDataChunks == 0 ) {
				firstData = chunk.data;
				dataCursor = chunkStart;
			}
			numDataChunks++;
			compressedBytes += chunk.length;
			continue;
		}
		if ( chunk.type == CHUNK_PLTE ) {
			if ( checkCRC && !ChunkCRCValid( chunk ) ) {
				return PNG_ERR_CRC;
			}
			if ( havePalette || numDataChunks != 0 || chunk.length == 0 || chunk.length % 3 != 0 || chunk.length > 768 ||
				 info.colorType == PNG_COLOR_GRAY || info.colorType == PNG_COLOR_GRAY_ALPHA ) {
				return PNG_ERR_FORMAT;
			}
			palette = chunk;
			havePalette = true;
			continue;
		}
		if ( chunk.type == CHUNK_tRNS ) {
			// tRNS is ancillary: a damaged or misplaced one is ignored
			if ( !haveTransparency && numDataChunks == 0 && ( !checkCRC || ChunkCRCValid( chunk ) ) &&
				 info.colorType != PNG_COLOR_GRAY_ALPHA && info.colorType != PNG_COLOR_RGBA ) {
				transparency = chunk;
				haveTransparency = true;
			}
			continue;
		}
		if ( critical ) {
			return PNG_ERR_UNSUPPORTED;
		}
	}
	if ( numDataChunks == 0 ) {
		return PNG_ERR_FORMAT;
	}
	if ( info.colorType == PNG_COLOR_PALETTE && !havePalette ) {
		return PNG_ERR_FORMAT;
	}
	if ( compressedBytes > (uint64_t)SIZE_MAX ) {
		return PNG_ERR_TOO_LARGE;
	}

	pngConvert_t *cv = (pngConvert_t *)malloc( sizeof( pngConvert_t ) );
	if ( cv == NULL ) {
		return PNG_ERR_NO_MEMORY;
	}
	result = BuildConversion( *cv, info, havePalette ? &palette : NULL, haveTransparency ? &transparency : NULL );
	if ( result != PNG_OK ) {
		free( cv );
		return result;
	}

	pngPass_t passes[7];
	uint64_t filteredBytes = 0;
	const int numPasses = PlanPasses( info, cv->bitsPerPixel, passes, &filteredBytes );
	uint64_t maxRowBytes = 0;
	for ( int i = 0; i < numPasses; i++ ) {
		if ( passes[i].rowBytes > maxRowBytes ) {
			maxRowBytes = passes[i].rowBytes;
		}
	}
	if ( filteredBytes > (uint64_t)SIZE_MAX || maxRowBytes > ( (uint64_t)SIZE_MAX >> 2 ) ) {
		free( cv );
		return PNG_ERR_TOO_LARGE;
	}

	// one IDAT inflates in place; several are joined first
	uint8_t *joined = NULL;
	const uint8_t *compressed = firstData;
	if ( numDataChunks > 1 ) {
		joined = (uint8_t *)malloc( (size_t)compressedBytes );
		if ( joined == NULL ) {
			free( cv );
			return PNG_ERR_NO_MEMORY;
		}
		size_t walk = dataCursor;
		size_t offset = 0;
		uint32_t found = 0;
		while ( found < numDataChunks && NextChunk( data, size, &walk, chunk ) ) {
			if ( chunk.type == CHUNK_IDAT ) {
				memcpy( joined + offset, chunk.data, chunk.length );
				offset += chunk.length;
				found++;
			}
		}
		compressed = joined;
	}

	const size_t scratchBytes = (size_t)filteredBytes;
	const size_t rowScratch = (size_t)maxRowBytes + 16;
	uint8_t *filtered = (uint8_t *)malloc( scratchBytes );
	uint8_t *zeroRow = (uint8_t *)calloc( 1, rowScratch );
	uint8_t *passRow = info.interlaced ? (uint8_t *)malloc( (size_t)info.width * 4 ) : NULL;
	if ( filtered == NULL || zeroRow == NULL || ( info.interlaced && passRow == NULL ) ) {
		free( filtered );
		free( zeroRow );
		free( passRow );
		free( joined );
		free( cv );
		return PNG_ERR_NO_MEMORY;
	}

	result = MapZlibResult( Zlib_Inflate( compressed, (size_t)compressedBytes, filtered, scratchBytes, true ) );
	free( joined );

	const uint32_t bytesPerPixel = cv->bitsPerPixel >= 8 ? cv->bitsPerPixel >> 3 : 1;
	const size_t outStride = (size_t)info.width * 4;
	uint8_t *row = filtered;
	for ( int p = 0; p < numPasses && result == PNG_OK; p++ ) {
		const pngPass_t &pass = passes[p];
		const size_t rowBytes = (size_t)pass.rowBytes;
		const bool direct = !info.interlaced && info.colorType == PNG_COLOR_RGBA && info.bitDepth == 8;
		const uint8_t *prior = zeroRow;
		for ( uint32_t y = 0; y < pass.height; y++, row += rowBytes + 1 ) {
			const uint8_t filter = row[0];
			uint8_t *line = row + 1;
			if ( direct ) {
				uint8_t *dst = rgba + (size_t)y * outStride;
				if ( !PNG_UnfilterRow( filter, dst, line, prior, rowBytes, bytesPerPixel ) ) {
					result = PNG_ERR_FORMAT;
					break;
				}
				prior = dst;
				continue;
			}
			if ( !PNG_UnfilterRow( filter, line, line, prior, rowBytes, bytesPerPixel ) ) {
				result = PNG_ERR_FORMAT;
				break;
			}
			prior = line;
			if ( !info.interlaced ) {
				ConvertRow( *cv, line, pass.width, rgba + (size_t)y * outStride );
				continue;
			}
			ConvertRow( *cv, line, pass.width, passRow );
			uint8_t *dst = rgba + (size_t)( pass.y0 + y * pass.dy ) * outStride + (size_t)pass.x0 * 4;
			const size_t step = (size_t)pass.dx * 4;
			for ( uint32_t x = 0; x < pass.width; x++, dst += step ) {
				memcpy( dst, passRow + x * 4, 4 );
			}
		}
	}

	free( filtered );
	free( zeroRow );
	free( passRow );
	free( cv );
	return result;
}

const char *PNG_ResultString( pngResult_t result ) {
	switch ( result ) {
	case PNG_OK:				return "ok";
	case PNG_ERR_SIGNATURE:		return "not a PNG file";
	case PNG_ERR_TRUNCATED:		return "file is truncated";
	case PNG_ERR_CRC:			return "chunk CRC mismatch";
	case PNG_ERR_HEADER:		return "invalid IHDR header";
	case PNG_ERR_FORMAT:		return "invalid or corrupt image data";
	case PNG_ERR_UNSUPPORTED:	return "unknown critical chunk";
	case PNG_ERR_TOO_LARGE:		return "image is too large";
	case PNG_ERR_NO_MEMORY:		return "out of memory";
	case PNG_ERR_ARGUMENT:		return "invalid argument";
	}
	return "unknown error";
}
