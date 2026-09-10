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





#include "../renderer/Image.h"
#include "ImageTools.h"
#include "DXT/DXTCodec.h"

idCVar image_usePrecompressedTextures(
	"image_usePrecompressedTextures",
	"1",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"automatic DDS texture replacements: 0 = off, 1 = stock progimg plus supported DDS formats, 2 = BC7/BPTC DDS only; custom dhewm3-compatible replacements belong under dds/",
	0,
	2 );

idCVar image_showPrecompressedTextures(
	"image_showPrecompressedTextures",
	"0",
	CVAR_RENDERER | CVAR_BOOL,
	"log the DDS replacement selected for each image, and every candidate path rejected when none is selected" );

/*

This file only has a single entry point:

void R_LoadImage( const char *name, byte **pic, int *width, int *height, bool makePowerOf2 );

*/

/*
 * Include file for users of JPEG library.
 * You will need to have included system headers that define at least
 * the typedefs FILE and size_t before you can include jpeglib.h.
 * (stdio.h is sufficient on ANSI-conforming systems.)
 * You may also wish to include "jerror.h".
 */

extern "C" {
#include "jpeg-6/jpeglib.h"


// hooks from jpeg lib to our system

void jpg_Error( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[2048];

	va_start (argptr,fmt);
	idStr::vsnPrintf( msg, sizeof( msg ), fmt, argptr );
	va_end (argptr);

	common->FatalError( "%s", msg );
}

void jpg_Printf( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[2048];

	va_start (argptr,fmt);
	idStr::vsnPrintf( msg, sizeof( msg ), fmt, argptr );
	va_end (argptr);

	common->Printf( "%s", msg );
}
};

/*
================
R_WriteTGA
================
*/
bool R_WriteTGA( const char *filename, const byte *data, int width, int height, bool flipVertical, const char * basePath ) {
	byte	*buffer;
	int		i;
	int		bufferSize = width*height*4 + 18;
	int     imgStart = 18;

	idTempArray<byte> buf( bufferSize );
	buffer = (byte *)buf.Ptr();
	memset( buffer, 0, 18 );
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width&255;
	buffer[13] = width>>8;
	buffer[14] = height&255;
	buffer[15] = height>>8;
	buffer[16] = 32;	// pixel size
	if ( !flipVertical ) {
		buffer[17] = (1<<5);	// flip bit, for normal top to bottom raster order
	}

	// swap rgb to bgr
	for ( i=imgStart ; i<bufferSize ; i+=4 ) {
		buffer[i] = data[i-imgStart+2];		// blue
		buffer[i+1] = data[i-imgStart+1];		// green
		buffer[i+2] = data[i-imgStart+0];		// red
		buffer[i+3] = data[i-imgStart+3];		// alpha
	}

	return fileSystem->WriteFile( filename, buffer, bufferSize, basePath ) == bufferSize;
}

static void LoadTGA( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp );
static void LoadJPG( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp );
static void LoadDDS( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool decodeRXGBNormalMap );

/*
========================================================================

TGA files are used for 24/32 bit images

========================================================================
*/

typedef struct _TargaHeader {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;

static ID_INLINE unsigned short R_ReadTargaUInt16( const byte *data ) {
	return static_cast<unsigned short>( data[0] | ( static_cast<unsigned int>( data[1] ) << 8 ) );
}


/*
=========================================================

TARGA LOADING

=========================================================
*/

/*
=============
LoadTGA
=============
*/
static void LoadTGA( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp ) {
	int		columns, rows, numPixels, fileSize, numBytes;
	byte	*pixbuf;
	int		row, column;
	byte	*buf_p;
	byte	*buffer;
	TargaHeader	targa_header;
	byte		*targa_rgba;

	if ( !pic ) {
		fileSystem->ReadFile( name, NULL, timestamp );
		return;	// just getting timestamp
	}

	*pic = NULL;

	//
	// load the file
	//
	fileSize = fileSystem->ReadFile( name, (void **)&buffer, timestamp );
	if ( !buffer ) {
		return;
	}

	buf_p = buffer;

	if ( fileSize < 18 ) {
		fileSystem->FreeFile( buffer );
		common->Error( "LoadTGA( %s ): incomplete file\n", name );
	}

	targa_header.id_length = *buf_p++;
	targa_header.colormap_type = *buf_p++;
	targa_header.image_type = *buf_p++;
	
	targa_header.colormap_index = R_ReadTargaUInt16( buf_p );
	buf_p += 2;
	targa_header.colormap_length = R_ReadTargaUInt16( buf_p );
	buf_p += 2;
	targa_header.colormap_size = *buf_p++;
	targa_header.x_origin = R_ReadTargaUInt16( buf_p );
	buf_p += 2;
	targa_header.y_origin = R_ReadTargaUInt16( buf_p );
	buf_p += 2;
	targa_header.width = R_ReadTargaUInt16( buf_p );
	buf_p += 2;
	targa_header.height = R_ReadTargaUInt16( buf_p );
	buf_p += 2;
	targa_header.pixel_size = *buf_p++;
	targa_header.attributes = *buf_p++;

	if ( fileSize < 18 + targa_header.id_length ) {
		fileSystem->FreeFile( buffer );
		common->Error( "LoadTGA( %s ): incomplete file\n", name );
	}

	if ( targa_header.image_type != 2 && targa_header.image_type != 10 && targa_header.image_type != 3 ) {
		common->Error( "LoadTGA( %s ): Only type 2 (RGB), 3 (gray), and 10 (RGB) TGA images supported\n", name );
	}

	if ( targa_header.colormap_type != 0 ) {
		common->Error( "LoadTGA( %s ): colormaps not supported\n", name );
	}

	if ( ( targa_header.pixel_size != 32 && targa_header.pixel_size != 24 ) && targa_header.image_type != 3 ) {
		common->Error( "LoadTGA( %s ): Only 32 or 24 bit images supported (no colormaps)\n", name );
	}

	// width * height * 4 can overflow a 32-bit int on crafted headers, wrapping the
	// allocation below while the decode loops still write the full extent
	if ( (int64)targa_header.width * targa_header.height * 4 > 0x7FFFFFFF ) {
		fileSystem->FreeFile( buffer );
		common->Error( "LoadTGA( %s ): dimensions too large (%i x %i)\n", name, targa_header.width, targa_header.height );
	}

	if ( targa_header.image_type == 2 || targa_header.image_type == 3 ) {
		numBytes = targa_header.width * targa_header.height * ( targa_header.pixel_size >> 3 );
		if ( numBytes > fileSize - 18 - targa_header.id_length ) {
			common->Error( "LoadTGA( %s ): incomplete file\n", name );
		}
	}

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;

	if ( width ) {
		*width = columns;
	}
	if ( height ) {
		*height = rows;
	}

	targa_rgba = (byte *)R_StaticAlloc(numPixels*4);
	*pic = targa_rgba;

	if ( targa_header.id_length != 0 ) {
		buf_p += targa_header.id_length;  // skip TARGA image comment
	}
	
	if ( targa_header.image_type == 2 || targa_header.image_type == 3 )
	{ 
		// Uncompressed RGB or gray scale image
		for( row = rows - 1; row >= 0; row-- )
		{
			pixbuf = targa_rgba + row*columns*4;
			for( column = 0; column < columns; column++)
			{
				unsigned char red,green,blue,alphabyte;
				switch( targa_header.pixel_size )
				{
					
				case 8:
					blue = *buf_p++;
					green = blue;
					red = blue;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = 255;
					break;

				case 24:
					blue = *buf_p++;
					green = *buf_p++;
					red = *buf_p++;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = 255;
					break;
				case 32:
					blue = *buf_p++;
					green = *buf_p++;
					red = *buf_p++;
					alphabyte = *buf_p++;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = alphabyte;
					break;
				default:
					common->Error( "LoadTGA( %s ): illegal pixel_size '%d'\n", name, targa_header.pixel_size );
					break;
				}
			}
		}
	}
	else if ( targa_header.image_type == 10 ) {   // Runlength encoded RGB images
		unsigned char red,green,blue,alphabyte,packetHeader,packetSize,j;
		const byte	*buf_end = buffer + fileSize;

		red = 0;
		green = 0;
		blue = 0;
		alphabyte = 0xff;

		for( row = rows - 1; row >= 0; row-- ) {
			pixbuf = targa_rgba + row*columns*4;
			for( column = 0; column < columns; ) {
				if ( buf_p >= buf_end ) {
					R_StaticFree( targa_rgba );
					*pic = NULL;
					fileSystem->FreeFile( buffer );
					common->Error( "LoadTGA( %s ): incomplete file\n", name );
				}
				packetHeader= *buf_p++;
				packetSize = 1 + (packetHeader & 0x7f);
				if ( packetHeader & 0x80 ) {        // run-length packet
					if ( buf_p + ( targa_header.pixel_size >> 3 ) > buf_end ) {
						R_StaticFree( targa_rgba );
						*pic = NULL;
						fileSystem->FreeFile( buffer );
						common->Error( "LoadTGA( %s ): incomplete file\n", name );
					}
					switch( targa_header.pixel_size ) {
						case 24:
								blue = *buf_p++;
								green = *buf_p++;
								red = *buf_p++;
								alphabyte = 255;
								break;
						case 32:
								blue = *buf_p++;
								green = *buf_p++;
								red = *buf_p++;
								alphabyte = *buf_p++;
								break;
						default:
							common->Error( "LoadTGA( %s ): illegal pixel_size '%d'\n", name, targa_header.pixel_size );
							break;
					}
	
					for( j = 0; j < packetSize; j++ ) {
						*pixbuf++=red;
						*pixbuf++=green;
						*pixbuf++=blue;
						*pixbuf++=alphabyte;
						column++;
						if ( column == columns ) { // run spans across rows
							column = 0;
							if ( row > 0) {
								row--;
							}
							else {
								goto breakOut;
							}
							pixbuf = targa_rgba + row*columns*4;
						}
					}
				}
				else {                            // non run-length packet
					if ( buf_p + packetSize * ( targa_header.pixel_size >> 3 ) > buf_end ) {
						R_StaticFree( targa_rgba );
						*pic = NULL;
						fileSystem->FreeFile( buffer );
						common->Error( "LoadTGA( %s ): incomplete file\n", name );
					}
					for( j = 0; j < packetSize; j++ ) {
						switch( targa_header.pixel_size ) {
							case 24:
									blue = *buf_p++;
									green = *buf_p++;
									red = *buf_p++;
									*pixbuf++ = red;
									*pixbuf++ = green;
									*pixbuf++ = blue;
									*pixbuf++ = 255;
									break;
							case 32:
									blue = *buf_p++;
									green = *buf_p++;
									red = *buf_p++;
									alphabyte = *buf_p++;
									*pixbuf++ = red;
									*pixbuf++ = green;
									*pixbuf++ = blue;
									*pixbuf++ = alphabyte;
									break;
							default:
								common->Error( "LoadTGA( %s ): illegal pixel_size '%d'\n", name, targa_header.pixel_size );
								break;
						}
						column++;
						if ( column == columns ) { // pixel packet run spans across rows
							column = 0;
							if ( row > 0 ) {
								row--;
							}
							else {
								goto breakOut;
							}
							pixbuf = targa_rgba + row*columns*4;
						}						
					}
				}
			}
			breakOut: ;
		}
	}

	if ( (targa_header.attributes & (1<<5)) ) {			// image flp bit
		if ( width != NULL && height != NULL ) {
			R_VerticalFlip( *pic, *width, *height );
		}
	}

	fileSystem->FreeFile( buffer );
}

/*
=========================================================

JPG LOADING

Interfaces with the huge libjpeg
=========================================================
*/

/*
=============
LoadJPG
=============
*/
static void LoadJPG( const char *filename, unsigned char **pic, int *width, int *height, ID_TIME_T *timestamp ) {
  /* This struct contains the JPEG decompression parameters and pointers to
   * working space (which is allocated as needed by the JPEG library).
   */
  struct jpeg_decompress_struct cinfo;
  /* We use our private extension JPEG error handler.
   * Note that this struct must live as long as the main JPEG parameter
   * struct, to avoid dangling-pointer problems.
   */
  /* This struct represents a JPEG error handler.  It is declared separately
   * because applications often want to supply a specialized error handler
   * (see the second half of this file for an example).  But here we just
   * take the easy way out and use the standard error handler, which will
   * print a message on stderr and call exit() if compression fails.
   * Note that this struct must live as long as the main JPEG parameter
   * struct, to avoid dangling-pointer problems.
   */
  struct jpeg_error_mgr jerr;
  /* More stuff */
  JSAMPARRAY buffer;		/* Output row buffer */
  int row_stride;		/* physical row width in output buffer */
  unsigned char *out;
  byte	*fbuffer;
  byte  *bbuf;

  /* In this example we want to open the input file before doing anything else,
   * so that the setjmp() error recovery below can assume the file is open.
   * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
   * requires it in order to read binary files.
   */

	// JDC: because fill_input_buffer() blindly copies INPUT_BUF_SIZE bytes,
	// we need to make sure the file buffer is padded or it may crash
  if ( pic ) {
	*pic = NULL;		// until proven otherwise
  }
  {
		int		len;
		idFile *f;

		f = fileSystem->OpenFileRead( filename );
		if ( !f ) {
			return;
		}
		len = f->Length();
		if ( timestamp ) {
			*timestamp = f->Timestamp();
		}
		if ( !pic ) {
			fileSystem->CloseFile( f );
			return;	// just getting timestamp
		}
		// Only the 4096-byte tail needs zeroing (fill_input_buffer over-reads
		// past the payload); the body is fully overwritten by the read. Zeroing
		// just the pad avoids a full-buffer clear on multi-MB JPEGs. The
		// short-read check is load-bearing, not cosmetic: with an uncleared
		// body a truncated file would otherwise leave uninitialized heap for
		// libjpeg to parse.
		fbuffer = (byte *)Mem_Alloc( len + 4096 );
		memset( fbuffer + len, 0, 4096 );
		if ( f->Read( fbuffer, len ) != len ) {
			Mem_Free( fbuffer );
			fileSystem->CloseFile( f );
			common->Warning( "LoadJPG( %s ): truncated file\n", filename );
			return;
		}
		fileSystem->CloseFile( f );
  }


  /* Step 1: allocate and initialize JPEG decompression object */

  /* We have to set up the error handler first, in case the initialization
   * step fails.  (Unlikely, but it could happen if you are out of memory.)
   * This routine fills in the contents of struct jerr, and returns jerr's
   * address which we place into the link field in cinfo.
   */
  cinfo.err = jpeg_std_error(&jerr);

  /* Now we can initialize the JPEG decompression object. */
  jpeg_create_decompress(&cinfo);

  /* Step 2: specify data source (eg, a file) */

  jpeg_stdio_src(&cinfo, fbuffer);

  /* Step 3: read file parameters with jpeg_read_header() */

  (void) jpeg_read_header(&cinfo, true );
  /* We can ignore the return value from jpeg_read_header since
   *   (a) suspension is not possible with the stdio data source, and
   *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   * See libjpeg.doc for more info.
   */

  /* Step 4: set parameters for decompression */

  /* In this example, we don't need to change any of the defaults set by
   * jpeg_read_header(), so we do nothing here.
   */

  /* Step 5: Start decompressor */

  (void) jpeg_start_decompress(&cinfo);
  /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

  /* We may need to do some setup of our own at this point before reading
   * the data.  After jpeg_start_decompress() we have the correct scaled
   * output image dimensions available, as well as the output colormap
   * if we asked for color quantization.
   * In this example, we need to make an output work buffer of the right size.
   */ 
  /* JSAMPLEs per row in output buffer */
  row_stride = cinfo.output_width * cinfo.output_components;

  if (cinfo.output_components!=4) {
		common->DWarning( "JPG %s is unsupported color depth (%d)",
			filename, cinfo.output_components);
  }
  // output_width/height are JDIMENSION (unsigned); the product * 4 wraps a
  // 32-bit computation before widening to the allocation size, so the scanline
  // loop below would then write far past a tiny buffer. Reject oversize
  // dimensions up front, mirroring the TGA loader (libjpeg permits dimensions
  // well beyond 2^31/4 pixels, so this is reachable on crafted files).
  if ( (int64)cinfo.output_width * cinfo.output_height * 4 > 0x7FFFFFFF ) {
		jpeg_destroy_decompress( &cinfo );
		Mem_Free( fbuffer );
		common->Error( "LoadJPG( %s ): dimensions too large (%i x %i)\n", filename, cinfo.output_width, cinfo.output_height );
  }
  out = (byte *)R_StaticAlloc(cinfo.output_width*cinfo.output_height*4);

  *pic = out;
  *width = cinfo.output_width;
  *height = cinfo.output_height;

  /* Step 6: while (scan lines remain to be read) */
  /*           jpeg_read_scanlines(...); */

  /* Here we use the library's state variable cinfo.output_scanline as the
   * loop counter, so that we don't have to keep track ourselves.
   */
  while (cinfo.output_scanline < cinfo.output_height) {
    /* jpeg_read_scanlines expects an array of pointers to scanlines.
     * Here the array is only one element long, but you could ask for
     * more than one scanline at a time if that's more convenient.
     */
	bbuf = ((out+(row_stride*cinfo.output_scanline)));
	buffer = &bbuf;
    (void) jpeg_read_scanlines(&cinfo, buffer, 1);
  }

  // clear all the alphas to 255
  {
	  int	i, j;
		byte	*buf;

		buf = *pic;

	  j = cinfo.output_width * cinfo.output_height * 4;
	  for ( i = 3 ; i < j ; i+=4 ) {
		  buf[i] = 255;
	  }
  }

  /* Step 7: Finish decompression */

  (void) jpeg_finish_decompress(&cinfo);
  /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

  /* Step 8: Release JPEG decompression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_decompress(&cinfo);

  /* After finish_decompress, we can close the input file.
   * Here we postpone it until after no more JPEG errors are possible,
   * so as to simplify the setjmp error logic above.  (Actually, I don't
   * think that jpeg_destroy can do an error exit, but why assume anything...)
   */
  Mem_Free( fbuffer );

  /* At this point you may want to check to see whether any corrupt-data
   * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
   */

  /* And we're done! */
}

//===================================================================

static ID_INLINE uint32 R_ReadLittleUInt32( const byte *data ) {
	return ( (uint32)data[0] ) | ( (uint32)data[1] << 8 ) | ( (uint32)data[2] << 16 ) | ( (uint32)data[3] << 24 );
}

static ID_INLINE void R_WriteLittleUInt32( byte *data, uint32 value ) {
	data[0] = (byte)( value & 0xff );
	data[1] = (byte)( ( value >> 8 ) & 0xff );
	data[2] = (byte)( ( value >> 16 ) & 0xff );
	data[3] = (byte)( ( value >> 24 ) & 0xff );
}

static ID_INLINE uint32 R_MakeFourCC( const char a, const char b, const char c, const char d ) {
	return ( (uint32)(byte)a ) | ( (uint32)(byte)b << 8 ) | ( (uint32)(byte)c << 16 ) | ( (uint32)(byte)d << 24 );
}

static const uint32 DDS_PIXELFORMAT_FOURCC = 0x00000004;
static const uint32 DDS_HEADER_FLAG_MIPMAPCOUNT = 0x00020000;
static const int DDS_HEADER_BYTES = 128;
static const int DDS_DXT10_HEADER_BYTES = 20;
static const int DDS_DXGI_FORMAT_BC7_UNORM = 98;
static const int DDS_DXGI_FORMAT_BC7_UNORM_SRGB = 99;
static const int DDS_DX10_RESOURCE_DIMENSION_TEXTURE2D = 3;
static const uint32 DDS_DX10_RESOURCE_MISC_TEXTURECUBE = 0x00000004;
static const int DDS_MAX_MIP_LEVELS = 32;
static const uint32 DDS_CAPS2_CUBEMAP = 0x00000200;
static const uint32 DDS_CAPS2_VOLUME = 0x00200000;

enum ddsStoredFormat_t {
	DDS_STORED_FORMAT_INVALID,
	DDS_STORED_FORMAT_DXT1,
	DDS_STORED_FORMAT_DXT5,
	DDS_STORED_FORMAT_RXGB,
	DDS_STORED_FORMAT_BC7
};

struct ddsFileInfo_t {
	ddsStoredFormat_t format;
	int dataOffset;
	int fileSize;
	int numLevels;
	uint32 width;
	uint32 height;
};

static int R_DDSBlockSizeForStoredFormat( ddsStoredFormat_t format ) {
	switch ( format ) {
		case DDS_STORED_FORMAT_DXT1:
			return 8;
		case DDS_STORED_FORMAT_DXT5:
		case DDS_STORED_FORMAT_RXGB:
		case DDS_STORED_FORMAT_BC7:
			return 16;
		default:
			return 0;
	}
}

static int R_DDSMaxMipLevelsForSize( uint32 width, uint32 height ) {
	if ( width == 0 || height == 0 ) {
		return 0;
	}

	int levels = 1;
	while ( ( width > 1 || height > 1 ) && levels < DDS_MAX_MIP_LEVELS ) {
		width = Max( (uint32)1, width >> 1 );
		height = Max( (uint32)1, height >> 1 );
		levels++;
	}
	return levels;
}

static int R_DDSStoredMipLevelCount( uint32 headerFlags, uint32 mipMapCount, uint32 width, uint32 height ) {
	if ( ( headerFlags & DDS_HEADER_FLAG_MIPMAPCOUNT ) == 0 || mipMapCount == 0 ) {
		return 1;
	}

	const int maxLevelsForSize = R_DDSMaxMipLevelsForSize( width, height );
	if ( maxLevelsForSize <= 0 ) {
		return 0;
	}

	const int headerLevels = mipMapCount > (uint32)DDS_MAX_MIP_LEVELS ? DDS_MAX_MIP_LEVELS : (int)mipMapCount;
	return Min( headerLevels, maxLevelsForSize );
}

static bool R_DDSComputeLevelLayout( int fileSize, int dataOffset, uint32 width, uint32 height, ddsStoredFormat_t format, int numLevels, idList<int> *levelOffsets, idList<int> *levelSizes ) {
	const int blockSize = R_DDSBlockSizeForStoredFormat( format );
	if ( blockSize <= 0 || numLevels <= 0 || numLevels > DDS_MAX_MIP_LEVELS || width == 0 || height == 0 ||
		 width > 0x7fffffff || height > 0x7fffffff ) {
		return false;
	}

	if ( levelOffsets != NULL ) {
		levelOffsets->SetNum( numLevels );
	}
	if ( levelSizes != NULL ) {
		levelSizes->SetNum( numLevels );
	}

	int offset = dataOffset;
	uint32 levelWidth = width;
	uint32 levelHeight = height;
	for ( int level = 0; level < numLevels; level++ ) {
		const int64 blocksWide = Max( (int64)1, ( (int64)levelWidth + 3 ) >> 2 );
		const int64 blocksHigh = Max( (int64)1, ( (int64)levelHeight + 3 ) >> 2 );
		const int64 levelSize = blocksWide * blocksHigh * blockSize;
		if ( levelSize <= 0 || levelSize > 0x7fffffff || offset < 0 || offset > fileSize - (int)levelSize ) {
			return false;
		}

		if ( levelOffsets != NULL ) {
			( *levelOffsets )[ level ] = offset;
		}
		if ( levelSizes != NULL ) {
			( *levelSizes )[ level ] = (int)levelSize;
		}

		offset += (int)levelSize;
		levelWidth = Max( (uint32)1, levelWidth >> 1 );
		levelHeight = Max( (uint32)1, levelHeight >> 1 );
	}

	return true;
}

static bool R_ParseDDSFileInfo( const byte *header, int headerBytes, int fileSize, ddsFileInfo_t &info ) {
	memset( &info, 0, sizeof( info ) );
	info.format = DDS_STORED_FORMAT_INVALID;
	if ( header == NULL || headerBytes < DDS_HEADER_BYTES || fileSize < DDS_HEADER_BYTES ) {
		return false;
	}

	const uint32 magic = R_ReadLittleUInt32( header + 0 );
	const uint32 headerSize = R_ReadLittleUInt32( header + 4 );
	const uint32 headerFlags = R_ReadLittleUInt32( header + 8 );
	const uint32 ddsHeight = R_ReadLittleUInt32( header + 12 );
	const uint32 ddsWidth = R_ReadLittleUInt32( header + 16 );
	const uint32 ddsMipMapCount = R_ReadLittleUInt32( header + 28 );
	const uint32 pixelFormatSize = R_ReadLittleUInt32( header + 76 );
	const uint32 pixelFormatFlags = R_ReadLittleUInt32( header + 80 );
	const uint32 fourCC = R_ReadLittleUInt32( header + 84 );
	const uint32 caps2 = R_ReadLittleUInt32( header + 112 );

	if ( magic != R_MakeFourCC( 'D', 'D', 'S', ' ' ) ||
		 headerSize != 124 ||
		 pixelFormatSize != 32 ||
		 ddsWidth == 0 ||
		 ddsHeight == 0 ||
		 ddsWidth > 0x7fffffff ||
		 ddsHeight > 0x7fffffff ||
		 ( caps2 & ( DDS_CAPS2_CUBEMAP | DDS_CAPS2_VOLUME ) ) != 0 ||
		 ( pixelFormatFlags & DDS_PIXELFORMAT_FOURCC ) == 0 ) {
		return false;
	}

	int localDataOffset = DDS_HEADER_BYTES;
	ddsStoredFormat_t format = DDS_STORED_FORMAT_INVALID;
	if ( fourCC == R_MakeFourCC( 'D', 'X', 'T', '1' ) ) {
		format = DDS_STORED_FORMAT_DXT1;
	} else if ( fourCC == R_MakeFourCC( 'D', 'X', 'T', '5' ) ) {
		format = DDS_STORED_FORMAT_DXT5;
	} else if ( fourCC == R_MakeFourCC( 'R', 'X', 'G', 'B' ) ) {
		format = DDS_STORED_FORMAT_RXGB;
	} else if ( fourCC == R_MakeFourCC( 'D', 'X', '1', '0' ) ) {
		if ( headerBytes < DDS_HEADER_BYTES + DDS_DXT10_HEADER_BYTES || fileSize < DDS_HEADER_BYTES + DDS_DXT10_HEADER_BYTES ) {
			return false;
		}
		const uint32 dxgiFormat = R_ReadLittleUInt32( header + DDS_HEADER_BYTES );
		const uint32 resourceDimension = R_ReadLittleUInt32( header + DDS_HEADER_BYTES + 4 );
		const uint32 miscFlag = R_ReadLittleUInt32( header + DDS_HEADER_BYTES + 8 );
		const uint32 arraySize = R_ReadLittleUInt32( header + DDS_HEADER_BYTES + 12 );
		if ( ( dxgiFormat == DDS_DXGI_FORMAT_BC7_UNORM || dxgiFormat == DDS_DXGI_FORMAT_BC7_UNORM_SRGB ) &&
			 resourceDimension == DDS_DX10_RESOURCE_DIMENSION_TEXTURE2D &&
			 ( miscFlag & DDS_DX10_RESOURCE_MISC_TEXTURECUBE ) == 0 &&
			 arraySize == 1 ) {
			format = DDS_STORED_FORMAT_BC7;
			localDataOffset += DDS_DXT10_HEADER_BYTES;
		}
	} else if ( fourCC == R_MakeFourCC( 'B', 'C', '7', '0' ) ||
				fourCC == R_MakeFourCC( 'B', 'C', '7', 'L' ) ||
				fourCC == R_MakeFourCC( 'B', 'C', '7', ' ' ) ) {
		format = DDS_STORED_FORMAT_BC7;
	}

	const int numLevels = R_DDSStoredMipLevelCount( headerFlags, ddsMipMapCount, ddsWidth, ddsHeight );
	if ( format == DDS_STORED_FORMAT_INVALID || numLevels <= 0 ||
		 !R_DDSComputeLevelLayout( fileSize, localDataOffset, ddsWidth, ddsHeight, format, numLevels, NULL, NULL ) ) {
		return false;
	}

	info.format = format;
	info.dataOffset = localDataOffset;
	info.fileSize = fileSize;
	info.numLevels = numLevels;
	info.width = ddsWidth;
	info.height = ddsHeight;
	return true;
}

static bool R_ReadDDSFileInfoUncached( const char *name, ddsFileInfo_t &info, ID_TIME_T *timestamp ) {
	if ( timestamp != NULL ) {
		*timestamp = FILE_NOT_FOUND_TIMESTAMP;
	}
	idFileLocal file( fileSystem->OpenFileRead( name ) );
	if ( file == NULL ) {
		return false;
	}

	const int fileSize = file->Length();
	byte header[ DDS_HEADER_BYTES + DDS_DXT10_HEADER_BYTES ];
	const int headerBytes = Min( fileSize, (int)sizeof( header ) );
	if ( headerBytes < DDS_HEADER_BYTES || file->Read( header, headerBytes ) != headerBytes ) {
		return false;
	}

	if ( !R_ParseDDSFileInfo( header, headerBytes, fileSize, info ) ) {
		return false;
	}
	if ( timestamp != NULL ) {
		*timestamp = file->Timestamp();
	}
	return true;
}

/*
=============
DDS probe memoization

A level load resolves the same DDS replacement candidates several times per
image (source selection, the staleness timestamp probe, generated-cache
validation, and the decode pass), and every probe is a real file open across
all search paths. Search-path contents cannot change during the synchronous
BeginLevelLoad..EndLevelLoad window, so per-candidate probe results are
memoized there. Outside the window every call probes the filesystem directly,
which keeps reloadImages and hot-reload workflows seeing freshly dropped
files. Image loading is single-threaded (see parseBuffer in
Image_program.cpp), so plain statics are safe here.
=============
*/
struct ddsProbeCacheEntry_t {
	idStr			name;
	bool			found;
	ddsFileInfo_t	info;
	ID_TIME_T		timestamp;
};

static bool ddsProbeCacheActive = false;
static idHashIndex ddsProbeCacheHash;
static idList<ddsProbeCacheEntry_t> ddsProbeCacheEntries;

void R_SetDDSProbeCacheActive( bool active ) {
	ddsProbeCacheActive = active;
	ddsProbeCacheHash.Free();
	ddsProbeCacheEntries.Clear();
}

static bool R_ReadDDSFileInfo( const char *name, ddsFileInfo_t &info, ID_TIME_T *timestamp ) {
	if ( !ddsProbeCacheActive ) {
		return R_ReadDDSFileInfoUncached( name, info, timestamp );
	}
	const int hashKey = ddsProbeCacheHash.GenerateKey( name, false );
	for ( int i = ddsProbeCacheHash.First( hashKey ); i >= 0; i = ddsProbeCacheHash.Next( i ) ) {
		const ddsProbeCacheEntry_t &entry = ddsProbeCacheEntries[ i ];
		if ( entry.name.Icmp( name ) == 0 ) {
			if ( timestamp != NULL ) {
				*timestamp = entry.found ? entry.timestamp : FILE_NOT_FOUND_TIMESTAMP;
			}
			if ( entry.found ) {
				info = entry.info;
			}
			return entry.found;
		}
	}
	ddsProbeCacheEntry_t entry;
	entry.name = name;
	entry.timestamp = FILE_NOT_FOUND_TIMESTAMP;
	entry.found = R_ReadDDSFileInfoUncached( name, entry.info, &entry.timestamp );
	if ( timestamp != NULL ) {
		*timestamp = entry.found ? entry.timestamp : FILE_NOT_FOUND_TIMESTAMP;
	}
	if ( entry.found ) {
		info = entry.info;
	}
	ddsProbeCacheHash.Add( hashKey, ddsProbeCacheEntries.Append( entry ) );
	return entry.found;
}

static bool R_ImageNameHasDDSShadowPrefix( const idStr &name ) {
	return name.Length() >= 4 && name[0] == 'd' && name[1] == 'd' && name[2] == 's' && name[3] == '/';
}

static bool R_ImageNameIsProgramOperator( const idToken &token ) {
	return !token.Icmp( "heightmap" ) ||
		!token.Icmp( "addnormals" ) ||
		!token.Icmp( "smoothnormals" ) ||
		!token.Icmp( "add" ) ||
		!token.Icmp( "scale" ) ||
		!token.Icmp( "invertAlpha" ) ||
		!token.Icmp( "invertColor" ) ||
		!token.Icmp( "makeIntensity" ) ||
		!token.Icmp( "downsize" ) ||
		!token.Icmp( "makeAlpha" );
}

static bool R_IsPlainImageSourceName( const char *imageName ) {
	if ( imageName == NULL || imageName[0] == '\0' ) {
		return false;
	}

	idLexer src;
	src.LoadMemory( imageName, idLib::SizeToInt( strlen( imageName ), "R_IsPlainImageSourceName" ), imageName );
	src.SetFlags( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );

	idToken token;
	if ( !src.ReadToken( &token ) || R_ImageNameIsProgramOperator( token ) ) {
		src.FreeSource();
		return false;
	}

	idToken nextToken;
	const bool hasExtraToken = src.ReadToken( &nextToken );
	src.FreeSource();
	return !hasExtraToken;
}

/*
=============
R_ImageProgramToCompressedFileName

Matches dhewm3's canonical dds/ naming scheme, including composed image
programs. idStr keeps the conversion bounds-safe for long material programs.
=============
*/
static void R_ImageProgramToCompressedFileName( const char *imageProgram, idStr &fileName ) {
	fileName = "dds/";
	int depth = 0;
	for ( const char *s = imageProgram; *s != '\0'; s++ ) {
		const char c = *s;
		if ( c == '/' || c == '\\' || c == '(' ) {
			fileName.Append( depth < 4 ? '/' : ' ' );
			depth++;
		} else if ( c == '<' || c == '>' || c == ':' || c == '|' || c == '"' || c == '.' ) {
			fileName.Append( '_' );
		} else if ( c == ' ' && fileName[ fileName.Length() - 1 ] == '/' ) {
			continue;
		} else if ( c == ')' || c == ',' ) {
			continue;
		} else {
			fileName.Append( c );
		}
	}
	fileName += ".dds";
	fileName.BackSlashesToSlashes();
	fileName.ToLower();
}

/*
=============
R_ImageProgramToRetailCompressedFileName

Quake 4 ships precomputed image-program results below progimg/. The retail
name encoding differs from Doom 3/dhewm3's dds/ convention: the outer
function becomes a directory, nested functions use a double underscore, and
arguments are joined with single underscores.
=============
*/
static void R_ImageProgramToRetailCompressedFileName( const char *imageProgram, idStr &fileName ) {
	fileName = "progimg/";
	const char *s = imageProgram;

	// A leading chain of image-program operators becomes directories. For
	// example downsize(addnormals(...), 1) starts with
	// progimg/downsize/addnormals/.
	while ( s != NULL ) {
		while ( *s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' ) {
			s++;
		}
		const char *operatorStart = s;
		while ( idStr::CharIsAlpha( (byte)*s ) || idStr::CharIsNumeric( (byte)*s ) || *s == '_' ) {
			s++;
		}
		const char *operatorEnd = s;
		while ( *s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' ) {
			s++;
		}
		if ( operatorEnd == operatorStart || *s != '(' ) {
			s = operatorStart;
			break;
		}
		fileName.Append( operatorStart, operatorEnd - operatorStart );
		fileName += "/";
		s++;
	}

	int nestedParenthesisDepth = 0;
	for ( ; s != NULL && *s != '\0'; s++ ) {
		const char c = *s;
		if ( c == '(' ) {
			nestedParenthesisDepth++;
			fileName += "__";
		} else if ( c == ')' ) {
			if ( nestedParenthesisDepth > 0 ) {
				nestedParenthesisDepth--;
			}
		} else if ( c == ',' ) {
			fileName += "_";
		} else if ( c == ' ' || c == '\t' || c == '\r' || c == '\n' ) {
			continue;
		} else if ( c == '/' || c == '\\' ) {
			fileName += nestedParenthesisDepth == 0 ? "/" : "_";
		} else if ( c == '.' &&
				( idStr::Icmpn( s, ".tga", 4 ) == 0 ||
				  idStr::Icmpn( s, ".jpg", 4 ) == 0 ||
				  idStr::Icmpn( s, ".dds", 4 ) == 0 ) ) {
			s += 3;
		} else {
			fileName.Append( c );
		}
	}

	fileName += ".dds";
	fileName.BackSlashesToSlashes();
	fileName.ToLower();
}

static bool R_ImageHasRetailProgramDDSName( const char *imageProgram ) {
	if ( imageProgram == NULL || imageProgram[0] == '\0' ) {
		return false;
	}
	// Retail progimg/ mirrors image-program results. Plain source files have no
	// entry there, except under gfx/env/ - and those are cube sides, which must
	// never take the retail replacement (see R_BuildDDSCandidates).
	return strchr( imageProgram, '(' ) != NULL;
}

static bool R_TryResolvePreferredDDSImageSource( const idStr &candidateName, idStr &ddsName, ID_TIME_T *timestamp, bool allowPrecompressedDDS, bool *precompressedDDS, const char **rejectReason = NULL ) {
	if ( rejectReason != NULL ) {
		*rejectReason = "no such file";
	}

	ID_TIME_T ddsTimestamp = FILE_NOT_FOUND_TIMESTAMP;
	ddsFileInfo_t info;
	if ( !R_ReadDDSFileInfo( candidateName.c_str(), info, &ddsTimestamp ) ) {
		return false;
	}
	const int usePrecompressedTextures = image_usePrecompressedTextures.GetInteger();

	if ( info.format == DDS_STORED_FORMAT_DXT1 ||
		 info.format == DDS_STORED_FORMAT_DXT5 ||
		 info.format == DDS_STORED_FORMAT_RXGB ) {
		if ( usePrecompressedTextures >= 2 ) {
			if ( rejectReason != NULL ) {
				*rejectReason = "DXT/RXGB data ignored because image_usePrecompressedTextures is 2 (BC7 only)";
			}
			return false;
		}
		ddsName = candidateName;
		if ( timestamp != NULL ) {
			*timestamp = ddsTimestamp;
		}
		if ( precompressedDDS != NULL ) {
			*precompressedDDS = allowPrecompressedDDS && ImageTools_GetCompressionCaps().textureCompressionAvailable;
		}
		return true;
	}

	if ( info.format == DDS_STORED_FORMAT_BC7 ) {
		if ( !allowPrecompressedDDS ) {
			if ( rejectReason != NULL ) {
				*rejectReason = "BC7 data, but this image is an image-program input that must be decoded on the CPU";
			}
			return false;
		}
		if ( !ImageTools_GetCompressionCaps().bptcTextureCompressionAvailable ) {
			if ( rejectReason != NULL ) {
				*rejectReason = "BC7 data, but the active renderer reports no BPTC support";
			}
			return false;
		}
		ddsName = candidateName;
		if ( timestamp != NULL ) {
			*timestamp = ddsTimestamp;
		}
		if ( precompressedDDS != NULL ) {
			*precompressedDDS = true;
		}
		return true;
	}

	if ( rejectReason != NULL ) {
		*rejectReason = "unsupported DDS pixel format";
	}
	return false;
}

static void R_AppendDDSCandidate( idStrList &candidates, idStrList &origins, const idStr &name, const char *origin ) {
	if ( name.Length() == 0 ) {
		return;
	}
	for ( int i = 0; i < candidates.Num(); i++ ) {
		if ( candidates[i].Icmp( name ) == 0 ) {
			return;
		}
	}
	candidates.Append( name );
	origins.Append( idStr( origin ) );
}

/*
=============
Retail progimg/ and cube faces

progimg/ holds Raven's ENGINE OUTPUT for each image, and for the six faces of a
cameraCubeMap that output already went through the camera -> native conversion
R_LoadCubeImages applies below: forward/up/down transposed, left flipped
vertically, right flipped horizontally, back transposed and flipped both ways.
Retail never read those files back for cube maps - that is what id's "FIXME:
precompressed cube map files" note stands for - so the faces were always
re-derived from the .tga sources.

Accepting the retail replacement for a face would run that conversion a second
time and leave the whole skybox mis-oriented, so cube-face loads skip the retail
tree. Replacements under dds/ and .dds files sitting beside the source (Quake
4's own gfx/env/mp_sky/) are authored in source orientation, so they stay
eligible.
=============
*/
static int ddsRetailProgramSuppressed = 0;

class idSuppressRetailProgramDDS {
public:
	idSuppressRetailProgramDDS() { ddsRetailProgramSuppressed++; }
	~idSuppressRetailProgramDDS() { ddsRetailProgramSuppressed--; }
};

/*
=============
R_BuildDDSCandidates

Ordered list of DDS files that may stand in for an image source. User-supplied
replacements are probed before the retail progimg/ tree: the retail tree exists
for nearly every image-program result, so probing it first made a user's dds/
pack a silent no-op for every bump map and most speculars.
=============
*/
static void R_BuildDDSCandidates( const char *cname, idStrList &candidates, idStrList &origins ) {
	idStr canonicalDDSName;
	R_ImageProgramToCompressedFileName( cname, canonicalDDSName );
	R_AppendDDSCandidate( candidates, origins, canonicalDDSName, "dds/ (dhewm3 naming)" );

	if ( R_IsPlainImageSourceName( cname ) ) {
		idStr sourceName = cname;
		sourceName.DefaultFileExtension( ".tga" );
		sourceName.BackSlashesToSlashes();
		sourceName.ToLower();

		idStr ext;
		sourceName.ExtractFileExtension( ext );
		if ( ext == "tga" || ext == "jpg" ) {
			if ( !R_ImageNameHasDDSShadowPrefix( sourceName ) ) {
				idStr shadowDDSName = "dds/";
				shadowDDSName += sourceName;
				shadowDDSName.StripFileExtension();
				shadowDDSName.DefaultFileExtension( ".dds" );
				R_AppendDDSCandidate( candidates, origins, shadowDDSName, "dds/ (asset path)" );
			}

			idStr siblingDDSName = sourceName;
			siblingDDSName.StripFileExtension();
			siblingDDSName.DefaultFileExtension( ".dds" );
			R_AppendDDSCandidate( candidates, origins, siblingDDSName, "alongside the original asset (Quake 4 style)" );
		}
	}

	// stock retail data last, so any user replacement wins
	if ( ddsRetailProgramSuppressed == 0 && R_ImageHasRetailProgramDDSName( cname ) ) {
		idStr retailDDSName;
		R_ImageProgramToRetailCompressedFileName( cname, retailDDSName );
		R_AppendDDSCandidate( candidates, origins, retailDDSName, "retail progimg/" );
	}
}

/*
=============
R_ResolvePreferredDDSImageSource
=============
*/
bool R_ResolvePreferredDDSImageSource( const char *cname, idStr &ddsName, ID_TIME_T *timestamp, bool allowPrecompressedDDS, bool *precompressedDDS ) {
	if ( precompressedDDS != NULL ) {
		*precompressedDDS = false;
	}
	if ( timestamp != NULL ) {
		*timestamp = FILE_NOT_FOUND_TIMESTAMP;
	}
	if ( cname == NULL || cname[0] == '\0' ) {
		return false;
	}

	if ( image_usePrecompressedTextures.GetInteger() <= 0 ) {
		return false;
	}

	idStrList candidates;
	idStrList origins;
	R_BuildDDSCandidates( cname, candidates, origins );

	const bool logMisses = image_showPrecompressedTextures.GetBool();
	idStr rejections;

	for ( int i = 0; i < candidates.Num(); i++ ) {
		const char *rejectReason = NULL;
		if ( R_TryResolvePreferredDDSImageSource( candidates[i], ddsName, timestamp, allowPrecompressedDDS, precompressedDDS, &rejectReason ) ) {
			return true;
		}
		if ( logMisses ) {
			rejections += va( "   %-64s [%s] %s\n", candidates[i].c_str(), origins[i].c_str(),
				rejectReason != NULL ? rejectReason : "rejected" );
		}
	}

	if ( logMisses && candidates.Num() > 0 ) {
		// a miss is what needs explaining; logging only hits leaves a pack
		// author with no way to discover the name the engine expects
		idLib::Printf( "no DDS replacement for '%s':\n%s", cname, rejections.c_str() );
	}
	return false;
}

/*
=============
R_LoadPrecompressedDDS
=============
*/
bool R_LoadPrecompressedDDS( const char *cname, idBinaryImage &image, ID_TIME_T *timestamp, textureUsage_t usage, const imageDownsizePolicy_t &downsizePolicy, bool useMipmaps, imageReductionResult_t* reduction ) {
	if ( cname == NULL || cname[0] == '\0' ) {
		return false;
	}

	idStr name = cname;
	name.BackSlashesToSlashes();

	idStr ext;
	name.ExtractFileExtension( ext );
	if ( ext.Icmp( "dds" ) != 0 ) {
		return false;
	}

	// Read into a buffer this library owns rather than fileSystem->ReadFile: the
	// mip payloads stay behind as views into it inside the idBinaryImage, and its
	// eventual Mem_Free in idBinaryImage::Clear must pair with this binary's
	// allocator (the renderer modules carry their own idlib heap).
	idFile *ddsFile = fileSystem->OpenFileRead( name.c_str() );
    struct CloseFile { idFile*& file; ~CloseFile() { if (file) fileSystem->CloseFile(file); } } closeFile{ddsFile};
	if ( ddsFile == NULL ) {
		if ( timestamp != NULL ) {
			*timestamp = FILE_NOT_FOUND_TIMESTAMP;
		}
		return false;
	}
	if ( timestamp != NULL ) {
		*timestamp = ddsFile->Timestamp();
	}
	const int fileSize = ddsFile->Length();
	byte *buffer = fileSize >= DDS_HEADER_BYTES ? (byte *)Mem_Alloc( fileSize ) : NULL;
    struct ReleaseBuffer { byte*& bytes; ~ReleaseBuffer() { if (bytes) Mem_Free(bytes); } } releaseBuffer{buffer};
	const bool readOk = buffer != NULL && ddsFile->Read( buffer, fileSize ) == fileSize;
	fileSystem->CloseFile( ddsFile );
    ddsFile = NULL;
	if ( !readOk ) return false;

	bool loaded = false;
	do {
		ddsFileInfo_t info;
		if ( !R_ParseDDSFileInfo( buffer, fileSize, fileSize, info ) ) {
			idLib::Warning( "Image file '%s' has an invalid or incomplete supported DDS layout", name.c_str() );
			break;
		}

		if ( info.format == DDS_STORED_FORMAT_BC7 && !ImageTools_GetCompressionCaps().bptcTextureCompressionAvailable ) {
			idLib::Warning( "Image file '%s' uses BC7/BPTC DDS data, but the active renderer does not support BPTC texture compression", name.c_str() );
			break;
		}
		if ( ( info.format == DDS_STORED_FORMAT_DXT1 ||
			   info.format == DDS_STORED_FORMAT_DXT5 ||
			   info.format == DDS_STORED_FORMAT_RXGB ) && !ImageTools_GetCompressionCaps().textureCompressionAvailable ) {
			break;
		}

		idList<int> levelOffsets;
		idList<int> levelSizes;
		if ( !R_DDSComputeLevelLayout( fileSize, info.dataOffset, info.width, info.height, info.format, info.numLevels, &levelOffsets, &levelSizes ) ) {
			idLib::Warning( "Image file '%s' has an incomplete DDS mip chain", name.c_str() );
			break;
		}

		// Precompressed data already carries a filtered mip chain, so honor the
		// downsize policy by starting at a lower level rather than decompressing
		// and resampling. That matches the raw loader whenever the file carries a
		// full chain; a replacement exported without one simply runs out of levels
		// and stays larger, which the caller reports under
		// image_showPrecompressedTextures.
        imageReductionResult_t selected{};
        if (!R_ResolveImageReduction(downsizePolicy, info.width, info.height, info.numLevels, selected)) break;
        const int selectedWidth = selected.selectedWidth;
        const int selectedHeight = selected.selectedHeight;
        const int firstLevel = selected.firstLevel;

		const int selectedLevels = useMipmaps ? info.numLevels - firstLevel : 1;
		textureFormat_t textureFormat = FMT_NONE;
		switch ( info.format ) {
			case DDS_STORED_FORMAT_DXT1:
				textureFormat = FMT_DXT1;
				break;
			case DDS_STORED_FORMAT_DXT5:
			case DDS_STORED_FORMAT_RXGB:
				textureFormat = FMT_DXT5;
				break;
			case DDS_STORED_FORMAT_BC7:
				textureFormat = FMT_BC7;
				break;
			default:
				break;
		}
		if ( textureFormat == FMT_NONE || selectedLevels <= 0 ) {
			break;
		}

		// RXGB and DXT5nm store the normal's X in alpha because DXT5 gives alpha
		// its own higher-precision interpolator; the renderer then skips the
		// alpha<-red swizzle and the interaction shader reads X straight out of
		// alpha. BC7 has no such convention - every common encoder writes plain
		// XYZ in RGB - so forcing that layout on BC7 made ordinary BC7 normal
		// maps sample X = 1.0 everywhere and flattened all bump lighting.
		const bool rxgbNormal =
			info.format == DDS_STORED_FORMAT_RXGB ||
			( usage == TD_BUMP && textureFormat == FMT_DXT5 );
		const textureColor_t colorFormat = rxgbNormal ? CFM_NORMAL_DXT5 : CFM_DEFAULT;
		image.Load2DFromOwnedCompressedData( selectedWidth, selectedHeight, selectedLevels, textureFormat, colorFormat,
			buffer, levelOffsets.Ptr() + firstLevel, levelSizes.Ptr() + firstLevel );
		buffer = NULL;	// the binary image now owns the file buffer
        if (reduction) *reduction = selected;
		loaded = true;
	} while ( false );

	return loaded;
}

/*
=============
R_ImageDDS_RunSelfTest
=============
*/
bool R_ImageDDS_RunSelfTest() {
	struct imageProgramNameCase_t {
		const char *imageProgram;
		const char *expectedDDSName;
	};
	const imageProgramNameCase_t nameCases[] = {
		{ "textures/stone/foo", "dds/textures/stone/foo.dds" },
		{ "textures/stone/foo.tga", "dds/textures/stone/foo_tga.dds" },
		{ "makeIntensity(gfx/lights/squarelight1)", "dds/makeintensity/gfx/lights/squarelight1.dds" },
		{ "heightmap(models/monsters/foo_local.tga, 4)", "dds/heightmap/models/monsters/foo_local_tga 4.dds" },
		{ "addnormals( textures/a, heightmap( textures/b, 4 ) )", "dds/addnormals/textures/a heightmap/textures/b 4  .dds" }
	};

	for ( int i = 0; i < (int)( sizeof( nameCases ) / sizeof( nameCases[0] ) ); i++ ) {
		idStr actualDDSName;
		R_ImageProgramToCompressedFileName( nameCases[i].imageProgram, actualDDSName );
		if ( actualDDSName.Icmp( nameCases[i].expectedDDSName ) != 0 ) {
			common->Warning( "Image DDS self-test failed: '%s' mapped to '%s' instead of '%s'",
				nameCases[i].imageProgram, actualDDSName.c_str(), nameCases[i].expectedDDSName );
			return false;
		}
	}

	const imageProgramNameCase_t retailNameCases[] = {
		{ "gfx/env/act_2/act2_forward.tga", "progimg/gfx/env/act_2/act2_forward.dds" },
		{ "downsize( models/mapobjects/strogg/stroggos/clouds, 1)", "progimg/downsize/models/mapobjects/strogg/stroggos/clouds_1.dds" },
		{ "makealpha( gfx/effects/armorvieweffect2)", "progimg/makealpha/gfx/effects/armorvieweffect2.dds" },
		{ "heightmap( textures/rock/rock05a_h.tga, 8)", "progimg/heightmap/textures/rock/rock05a_h_8.dds" },
		{ "addnormals( models/mapobjects/strogg/stroggos/stroggos_local, heightmap( models/mapobjects/strogg/stroggos/stroggos_h, 8))",
		  "progimg/addnormals/models/mapobjects/strogg/stroggos/stroggos_local_heightmap__models_mapobjects_strogg_stroggos_stroggos_h_8.dds" },
		{ "downsize( addnormals( models/vehicles/marine_fighter/marine_fighter_local, heightmap( models/vehicles/marine_fighter/marine_fighter_h, 2)), 1)",
		  "progimg/downsize/addnormals/models/vehicles/marine_fighter/marine_fighter_local_heightmap__models_vehicles_marine_fighter_marine_fighter_h_2_1.dds" }
	};

	for ( int i = 0; i < (int)( sizeof( retailNameCases ) / sizeof( retailNameCases[0] ) ); i++ ) {
		idStr actualDDSName;
		R_ImageProgramToRetailCompressedFileName( retailNameCases[i].imageProgram, actualDDSName );
		if ( actualDDSName.Icmp( retailNameCases[i].expectedDDSName ) != 0 ) {
			common->Warning( "Image DDS self-test failed: retail '%s' mapped to '%s' instead of '%s'",
				retailNameCases[i].imageProgram, actualDDSName.c_str(), retailNameCases[i].expectedDDSName );
			return false;
		}
	}

	byte header[ DDS_HEADER_BYTES + DDS_DXT10_HEADER_BYTES ];
	memset( header, 0, sizeof( header ) );
	R_WriteLittleUInt32( header + 0, R_MakeFourCC( 'D', 'D', 'S', ' ' ) );
	R_WriteLittleUInt32( header + 4, 124 );
	R_WriteLittleUInt32( header + 8, DDS_HEADER_FLAG_MIPMAPCOUNT );
	R_WriteLittleUInt32( header + 12, 4 );
	R_WriteLittleUInt32( header + 16, 8 );
	R_WriteLittleUInt32( header + 28, 4 );
	R_WriteLittleUInt32( header + 76, 32 );
	R_WriteLittleUInt32( header + 80, DDS_PIXELFORMAT_FOURCC );
	R_WriteLittleUInt32( header + 84, R_MakeFourCC( 'D', 'X', '1', '0' ) );
	R_WriteLittleUInt32( header + DDS_HEADER_BYTES, DDS_DXGI_FORMAT_BC7_UNORM );
	R_WriteLittleUInt32( header + DDS_HEADER_BYTES + 4, DDS_DX10_RESOURCE_DIMENSION_TEXTURE2D );
	R_WriteLittleUInt32( header + DDS_HEADER_BYTES + 12, 1 );

	// 8x4 BC7 has four valid mip levels consuming 32 + 16 + 16 + 16 bytes.
	const int completeFileSize = (int)sizeof( header ) + 80;
	ddsFileInfo_t info;
	if ( !R_ParseDDSFileInfo( header, sizeof( header ), completeFileSize, info ) ||
		 info.format != DDS_STORED_FORMAT_BC7 || info.dataOffset != (int)sizeof( header ) ||
		 info.width != 8 || info.height != 4 || info.numLevels != 4 ) {
		common->Warning( "Image DDS self-test failed: valid DX10 BC7 metadata was rejected" );
		return false;
	}

	R_WriteLittleUInt32( header + DDS_HEADER_BYTES + 12, 2 );
	if ( R_ParseDDSFileInfo( header, sizeof( header ), completeFileSize, info ) ) {
		common->Warning( "Image DDS self-test failed: DX10 texture array was accepted" );
		return false;
	}
	R_WriteLittleUInt32( header + DDS_HEADER_BYTES + 12, 1 );
	if ( R_ParseDDSFileInfo( header, sizeof( header ), completeFileSize - 1, info ) ) {
		common->Warning( "Image DDS self-test failed: truncated BC7 mip data was accepted" );
		return false;
	}

	memset( header, 0, DDS_HEADER_BYTES );
	R_WriteLittleUInt32( header + 0, R_MakeFourCC( 'D', 'D', 'S', ' ' ) );
	R_WriteLittleUInt32( header + 4, 124 );
	R_WriteLittleUInt32( header + 8, DDS_HEADER_FLAG_MIPMAPCOUNT );
	R_WriteLittleUInt32( header + 12, 4 );
	R_WriteLittleUInt32( header + 16, 4 );
	R_WriteLittleUInt32( header + 28, 3 );
	R_WriteLittleUInt32( header + 76, 32 );
	R_WriteLittleUInt32( header + 80, DDS_PIXELFORMAT_FOURCC );
	R_WriteLittleUInt32( header + 84, R_MakeFourCC( 'R', 'X', 'G', 'B' ) );
	const int completeRXGBFileSize = DDS_HEADER_BYTES + 16 + 16 + 16;
	if ( !R_ParseDDSFileInfo( header, DDS_HEADER_BYTES, completeRXGBFileSize, info ) ||
		 info.format != DDS_STORED_FORMAT_RXGB || info.numLevels != 3 ) {
		common->Warning( "Image DDS self-test failed: valid retail RXGB metadata was rejected" );
		return false;
	}

	common->Printf( "Image DDS self-test passed\n" );
	return true;
}

/*
=============
LoadDDS
=============
*/
static void LoadDDS( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool decodeRXGBNormalMap ) {
	int fileSize;
	byte *buffer;

	if ( !pic ) {
		fileSystem->ReadFile( name, NULL, timestamp );
		return;	// just getting timestamp
	}

	*pic = NULL;

	fileSize = fileSystem->ReadFile( name, (void **)&buffer, timestamp );
	if ( !buffer ) {
		return;
	}

	// DDS magic + 124-byte header
	if ( fileSize < 128 ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	const uint32 magic = R_ReadLittleUInt32( buffer + 0 );
	const uint32 headerSize = R_ReadLittleUInt32( buffer + 4 );
	if ( magic != R_MakeFourCC( 'D', 'D', 'S', ' ' ) || headerSize != 124 ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	const uint32 ddsHeight = R_ReadLittleUInt32( buffer + 12 );
	const uint32 ddsWidth = R_ReadLittleUInt32( buffer + 16 );
	const uint32 pixelFormatFlags = R_ReadLittleUInt32( buffer + 80 );
	const uint32 fourCC = R_ReadLittleUInt32( buffer + 84 );

	if ( ddsWidth == 0 || ddsHeight == 0 || ( pixelFormatFlags & 0x4 ) == 0 ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	int blockSize = 0;
	enum ddsCompression_t {
		DDS_COMPRESSION_DXT1,
		DDS_COMPRESSION_DXT5
	} compression;
	bool storedRXGBNormalMap = false;

	if ( fourCC == R_MakeFourCC( 'D', 'X', 'T', '1' ) ) {
		blockSize = 8;
		compression = DDS_COMPRESSION_DXT1;
	} else if ( fourCC == R_MakeFourCC( 'D', 'X', 'T', '5' ) ) {
		blockSize = 16;
		compression = DDS_COMPRESSION_DXT5;
	} else if ( fourCC == R_MakeFourCC( 'R', 'X', 'G', 'B' ) ) {
		blockSize = 16;
		compression = DDS_COMPRESSION_DXT5;
		storedRXGBNormalMap = true;
	} else {
		fileSystem->FreeFile( buffer );
		return;
	}

	const int blocksWide = Max( 1, (int)( ( ddsWidth + 3 ) >> 2 ) );
	const int blocksHigh = Max( 1, (int)( ( ddsHeight + 3 ) >> 2 ) );
	const int levelSize = blocksWide * blocksHigh * blockSize;
	const int dataOffset = 128;

	if ( fileSize < ( dataOffset + levelSize ) ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	const int realWidth = (int)ddsWidth;
	const int realHeight = (int)ddsHeight;
	const int paddedWidth = blocksWide * 4;
	const int paddedHeight = blocksHigh * 4;
	const int64 realBytes = (int64)realWidth * realHeight * 4;
	const int64 paddedBytes = (int64)paddedWidth * paddedHeight * 4;
	if ( realBytes <= 0 || realBytes > 0x7FFFFFFF || paddedBytes <= 0 || paddedBytes > 0x7FFFFFFF ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	byte *decodedRgba = (byte *)R_StaticAlloc( (int)paddedBytes );
	idDxtDecoder decoder;
	if ( compression == DDS_COMPRESSION_DXT1 ) {
		decoder.DecompressImageDXT1( buffer + dataOffset, decodedRgba, paddedWidth, paddedHeight );
	} else if ( storedRXGBNormalMap || decodeRXGBNormalMap ) {
		decoder.DecompressNormalMapDXT5( buffer + dataOffset, decodedRgba, paddedWidth, paddedHeight );
	} else {
		decoder.DecompressImageDXT5( buffer + dataOffset, decodedRgba, paddedWidth, paddedHeight );
	}

	byte *ddsRgba = decodedRgba;
	if ( paddedWidth != realWidth || paddedHeight != realHeight ) {
		ddsRgba = (byte *)R_StaticAlloc( (int)realBytes );
		for ( int y = 0; y < realHeight; y++ ) {
			memcpy( ddsRgba + y * realWidth * 4, decodedRgba + y * paddedWidth * 4, realWidth * 4 );
		}
		R_StaticFree( decodedRgba );
	}

	*pic = ddsRgba;
	if ( width ) {
		*width = realWidth;
	}
	if ( height ) {
		*height = realHeight;
	}

	fileSystem->FreeFile( buffer );
}

//===================================================================

static bool R_LoadImageSucceeded( byte **pic, ID_TIME_T *timestamp ) {
	if ( pic != NULL ) {
		return *pic != NULL;
	}
	if ( timestamp != NULL ) {
		return *timestamp != FILE_NOT_FOUND_TIMESTAMP;
	}
	return false;
}

/*
=================
R_LoadImage

Loads any of the supported image types into a cannonical
32 bit format.

Automatically attempts to load .jpg / .dds files if .tga files fail to load.

*pic will be NULL if the load failed.

Anything that is going to make this into a texture would use
makePowerOf2 = true, but something loading an image as a lookup
table of some sort would leave it in identity form.

It is important to do this at image load time instead of texture load
time for bump maps.

Timestamp may be NULL if the value is going to be ignored

If pic is NULL, the image won't actually be loaded, it will just find the
timestamp.
=================
*/
static void R_LoadImageInternal( const char *cname, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool makePowerOf2, textureUsage_t usage ) {
	idStr name = cname;
	const bool decodeRXGBNormalMap = ( usage == TD_BUMP );

	if ( pic ) {
		*pic = NULL;
	}
	if ( timestamp ) {
		*timestamp = FILE_NOT_FOUND_TIMESTAMP;
	}
	if ( width ) {
		*width = 0;
	}
	if ( height ) {
		*height = 0;
	}

	name.DefaultFileExtension( ".tga" );

	if (name.Length()<5) {
		return;
	}

	name.ToLower();
	idStr ext;
	name.ExtractFileExtension( ext );

	idStr preferredDDSName;
	ID_TIME_T preferredDDSTime = FILE_NOT_FOUND_TIMESTAMP;
	if ( ext != "dds" && R_ResolvePreferredDDSImageSource( name.c_str(), preferredDDSName, &preferredDDSTime, false, NULL ) ) {
		if ( pic == NULL ) {
			// timestamp-only probe: the resolve already opened this file and read
			// its timestamp, so don't open it a second time
			if ( timestamp != NULL ) {
				*timestamp = preferredDDSTime;
			}
		} else {
			LoadDDS( preferredDDSName.c_str(), pic, width, height, timestamp, decodeRXGBNormalMap );
		}
		if ( R_LoadImageSucceeded( pic, timestamp ) ) {
			return;
		}
	}

	if ( ext == "tga" ) {
		LoadTGA( name.c_str(), pic, width, height, timestamp );            // try tga first
		if ( ( pic && *pic == 0 ) || ( timestamp && *timestamp == -1 ) ) { //-V595
			name.StripFileExtension();
			name.DefaultFileExtension( ".jpg" );
			LoadJPG( name.c_str(), pic, width, height, timestamp );
		}
		if ( ( pic && *pic == 0 ) || ( timestamp && *timestamp == -1 ) ) { //-V595
			name.StripFileExtension();
			name.DefaultFileExtension( ".dds" );
			LoadDDS( name.c_str(), pic, width, height, timestamp, decodeRXGBNormalMap );
		}
	} else if ( ext == "jpg" ) {
		LoadJPG( name.c_str(), pic, width, height, timestamp );
		if ( ( pic && *pic == 0 ) || ( timestamp && *timestamp == -1 ) ) { //-V595
			name.StripFileExtension();
			name.DefaultFileExtension( ".dds" );
			LoadDDS( name.c_str(), pic, width, height, timestamp, decodeRXGBNormalMap );
		}
	} else if ( ext == "dds" ) {
		LoadDDS( name.c_str(), pic, width, height, timestamp, decodeRXGBNormalMap );
	}

	if ( ( width && *width < 1 ) || ( height && *height < 1 ) ) {
		if ( pic && *pic ) {
			R_StaticFree( *pic );
			*pic = 0;
		}
	}

	//
	// convert to exact power of 2 sizes
	//
	/*
	if ( pic && *pic && makePowerOf2 ) {
		int		w, h;
		int		scaled_width, scaled_height;
		byte	*resampledBuffer;

		w = *width;
		h = *height;

		for (scaled_width = 1 ; scaled_width < w ; scaled_width<<=1)
			;
		for (scaled_height = 1 ; scaled_height < h ; scaled_height<<=1)
			;

		if ( scaled_width != w || scaled_height != h ) {
			resampledBuffer = R_ResampleTexture( *pic, w, h, scaled_width, scaled_height );
			R_StaticFree( *pic );
			*pic = resampledBuffer;
			*width = scaled_width;
			*height = scaled_height;
		}
	}
	*/
}

void R_LoadImage( const char *cname, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool makePowerOf2 ) {
	R_LoadImageInternal( cname, pic, width, height, timestamp, makePowerOf2, TD_DEFAULT );
}

void R_LoadImageForUsage( const char *cname, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool makePowerOf2, textureUsage_t usage ) {
	R_LoadImageInternal( cname, pic, width, height, timestamp, makePowerOf2, usage );
}


/*
=======================
R_LoadCubeImages

Loads six files with proper extensions
=======================
*/
bool R_LoadCubeImages( const char *imgName, cubeFiles_t extensions, byte *pics[6], int *outSize, ID_TIME_T *timestamp ) {
	int		i, j;
	char	*cameraSides[6] =  { "_forward.tga", "_back.tga", "_left.tga", "_right.tga", 
		"_up.tga", "_down.tga" };
	char	*axisSides[6] =  { "_px.tga", "_nx.tga", "_py.tga", "_ny.tga", 
		"_pz.tga", "_nz.tga" };
	char	**sides;
	char	fullName[MAX_IMAGE_NAME];
	int		width, height, size = 0;

	if ( extensions == CF_CAMERA ) {
		sides = cameraSides;
	} else {
		sides = axisSides;
	}

	// FIXME: precompressed cube map files
	if ( pics ) {
		memset( pics, 0, 6*sizeof(pics[0]) );
	}
	if ( timestamp ) {
		*timestamp = 0;
	}

	// the retail progimg/ replacements for these faces are already converted;
	// see the note above R_BuildDDSCandidates
	idSuppressRetailProgramDDS noRetailProgramFaces;

	for ( i = 0 ; i < 6 ; i++ ) {
		idStr::snPrintf( fullName, sizeof( fullName ), "%s%s", imgName, sides[i] );

		ID_TIME_T thisTime;
		if ( !pics ) {
			// just checking timestamps
			R_LoadImageProgram( fullName, NULL, &width, &height, &thisTime );
		} else {
			R_LoadImageProgram( fullName, &pics[i], &width, &height, &thisTime );
		}
		if ( thisTime == FILE_NOT_FOUND_TIMESTAMP || (pics && !pics[i]) ) {
			break;
		}
		if ( i == 0 ) {
			size = width;
		}
		if ( width != size || height != size ) {
			common->Warning( "Mismatched sizes on cube map '%s'", imgName );
			break;
		}
		if ( timestamp ) {
			if ( thisTime > *timestamp ) {
				*timestamp = thisTime;
			}
		}
		if ( pics && extensions == CF_CAMERA ) {
			// convert from "camera" images to native cube map images
			switch( i ) {
			case 0:	// forward
				R_RotatePic( pics[i], width);
				break;
			case 1:	// back
				R_RotatePic( pics[i], width);
				R_HorizontalFlip( pics[i], width, height );
				R_VerticalFlip( pics[i], width, height );
				break;
			case 2:	// left
				R_VerticalFlip( pics[i], width, height );
				break;
			case 3:	// right
				R_HorizontalFlip( pics[i], width, height );
				break;
			case 4:	// up
				R_RotatePic( pics[i], width);
				break;
			case 5: // down
				R_RotatePic( pics[i], width);
				break;
			}
		}
	}

	if ( i != 6 ) {
		// we had an error, so free everything
		// pics[i] is live on the size-mismatch break, NULL on the not-found break
		if ( pics ) {
			for ( j = 0 ; j <= i && j < 6 ; j++ ) {
				if ( pics[j] ) {
					R_StaticFree( pics[j] );
					pics[j] = NULL;
				}
			}
		}

		if ( timestamp ) {
			*timestamp = 0;
		}
		return false;
	}

	if ( outSize ) {
		*outSize = size;
	}
	return true;
}
