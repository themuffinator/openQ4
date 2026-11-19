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
#include "LegacyScopeMask.h"
#include "DXT/DXTCodec.h"
#include "BC6H/BC6HCodec.h"
#include "BC7/BC7Codec.h"
#include "PNG/PNGCodec.h"

idCVar image_usePrecompressedTextures(
	"image_usePrecompressedTextures",
	"1",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"automatic DDS texture replacements: 0 = off, 1 = stock progimg plus supported DDS formats, 2 = BC7/BPTC, BC4 and BC5 DDS only (DXT/RXGB ignored); custom dhewm3-compatible replacements belong under dds/",
	0,
	2 );

idCVar image_pngCompressionLevel(
	"image_pngCompressionLevel",
	"4",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"compression for PNG screenshots and other PNG files the engine writes: 0 = store, 1 = fastest, 9 = smallest; 4 balances speed and size",
	0,
	9 );

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
#include "../external/jpeg-6/jpeglib.h"


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

/*
================
R_WritePNG

data holds width x height pixels of channels (3 or 4) bytes, top row first
unless flipVertical is set. The encoder keeps the smallest lossless colour
type, so opaque RGBA data is written without an alpha channel.
================
*/
bool R_WritePNG( const char *filename, const byte *data, int width, int height, int channels, bool flipVertical, const char *basePath ) {
	pngEncodeOptions_t options;
	options.compressionLevel = image_pngCompressionLevel.GetInteger();
	options.format = PNG_WRITE_AUTO;
	options.flipVertical = flipVertical;

	uint8_t *file = NULL;
	size_t fileSize = 0;
	const pngResult_t result = PNG_Encode( data, width, height, channels, 0, &options, &file, &fileSize );
	if ( result != PNG_OK ) {
		common->Warning( "R_WritePNG( %s ): %s", filename, PNG_ResultString( result ) );
		return false;
	}
	const bool written = fileSize <= 0x7FFFFFFF && fileSystem->WriteFile( filename, file, (int)fileSize, basePath ) == (int)fileSize;
	PNG_FreeEncoded( file );
	return written;
}

/*
================
R_WriteImageFile

Writes RGBA data as PNG when filename ends in .png, otherwise as TGA.
================
*/
bool R_WriteImageFile( const char *filename, const byte *data, int width, int height, bool flipVertical, const char *basePath ) {
	idStr name( filename );
	if ( name.CheckExtension( ".png" ) ) {
		return R_WritePNG( filename, data, width, height, 4, flipVertical, basePath );
	}
	return R_WriteTGA( filename, data, width, height, flipVertical, basePath );
}

static void LoadTGA( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp );
static void LoadJPG( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp );
static void LoadPNG( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp );
static void LoadDDS( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool decodeRXGBNormalMap );
static bool R_LoadBC6HAsHalfFloat( idBinaryImage &image, const byte *fileBuffer, const int *levelOffsets, int width, int height, int numLevels, bool isSigned );

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
		fileSystem->ReadFileContentStamp( name, NULL, timestamp );
		return;	// just getting timestamp
	}

	*pic = NULL;

	//
	// load the file
	//
	fileSize = fileSystem->ReadFileContentStamp( name, (void **)&buffer, timestamp );
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
  int	fbufferLength = 0;	// openQ4: the file's bytes in fbuffer, without its padding
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
			*timestamp = f->ContentStamp();
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
		fbufferLength = len;
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

  jpeg_stdio_src(&cinfo, fbuffer, fbufferLength);

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

/*
=========================================================

PNG LOADING

=========================================================
*/

/*
=============
LoadPNG

Every PNG colour type, bit depth and interlace mode decodes to RGBA8. A
damaged file warns and loads nothing, so the caller falls back to the next
source or the default image.
=============
*/
static void LoadPNG( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp ) {
	if ( !pic ) {
		fileSystem->ReadFileContentStamp( name, NULL, timestamp );
		return;	// just getting timestamp
	}
	*pic = NULL;

	byte *buffer = NULL;
	const int fileSize = fileSystem->ReadFileContentStamp( name, (void **)&buffer, timestamp );
	if ( !buffer ) {
		return;
	}

	pngInfo_t info;
	pngResult_t result = PNG_ReadInfo( buffer, (size_t)( fileSize > 0 ? fileSize : 0 ), info );
	// the same 2 GiB RGBA ceiling as the TGA and JPG loaders
	if ( result == PNG_OK && (uint64)info.width * info.height * 4 > 0x7FFFFFFF ) {
		result = PNG_ERR_TOO_LARGE;
	}
	if ( result != PNG_OK ) {
		common->Warning( "LoadPNG( %s ): %s", name, PNG_ResultString( result ) );
		fileSystem->FreeFile( buffer );
		return;
	}

	const size_t rgbaBytes = (size_t)info.width * info.height * 4;
	byte *rgba = (byte *)R_StaticAlloc( rgbaBytes );
	result = PNG_DecodeRGBA8( buffer, (size_t)fileSize, rgba, rgbaBytes, NULL );
	fileSystem->FreeFile( buffer );
	if ( result != PNG_OK ) {
		common->Warning( "LoadPNG( %s ): %s", name, PNG_ResultString( result ) );
		R_StaticFree( rgba );
		return;
	}

	*pic = rgba;
	if ( width ) {
		*width = (int)info.width;
	}
	if ( height ) {
		*height = (int)info.height;
	}
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

static const uint32 DDS_PIXELFORMAT_ALPHAPIXELS = 0x00000001;
static const uint32 DDS_PIXELFORMAT_FOURCC = 0x00000004;
static const uint32 DDS_PIXELFORMAT_RGB = 0x00000040;
static const uint32 DDS_HEADER_FLAG_MIPMAPCOUNT = 0x00020000;
static const int DDS_HEADER_BYTES = 128;
static const int DDS_DXT10_HEADER_BYTES = 20;
// The DXGI formats a DX10 header may carry. Modern exporters (texconv, NVTT 3,
// Compressonator) put BC1-BC3 behind a DX10 header whenever an sRGB variant is
// asked for, and write BC4/BC5 either that way or under ATI1/ATI2. The classic
// renderer samples every colour texture as gamma-space data, so an _SRGB
// variant reads exactly as its UNORM twin, as Quake 4's own DXT does.
static const int DDS_DXGI_FORMAT_R8G8B8A8_TYPELESS = 27;
static const int DDS_DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29;
static const int DDS_DXGI_FORMAT_BC1_TYPELESS = 70;
static const int DDS_DXGI_FORMAT_BC1_UNORM_SRGB = 72;
static const int DDS_DXGI_FORMAT_BC2_TYPELESS = 73;
static const int DDS_DXGI_FORMAT_BC2_UNORM_SRGB = 75;
static const int DDS_DXGI_FORMAT_BC3_TYPELESS = 76;
static const int DDS_DXGI_FORMAT_BC3_UNORM = 77;
static const int DDS_DXGI_FORMAT_BC3_UNORM_SRGB = 78;
static const int DDS_DXGI_FORMAT_BC4_TYPELESS = 79;
static const int DDS_DXGI_FORMAT_BC4_UNORM = 80;
static const int DDS_DXGI_FORMAT_BC5_TYPELESS = 82;
static const int DDS_DXGI_FORMAT_BC5_UNORM = 83;
static const int DDS_DXGI_FORMAT_B8G8R8A8_UNORM = 87;
static const int DDS_DXGI_FORMAT_B8G8R8X8_UNORM = 88;
static const int DDS_DXGI_FORMAT_B8G8R8A8_TYPELESS = 90;
static const int DDS_DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91;
static const int DDS_DXGI_FORMAT_B8G8R8X8_TYPELESS = 92;
static const int DDS_DXGI_FORMAT_B8G8R8X8_UNORM_SRGB = 93;
static const int DDS_DXGI_FORMAT_BC6H_TYPELESS = 94;
static const int DDS_DXGI_FORMAT_BC6H_UF16 = 95;
static const int DDS_DXGI_FORMAT_BC6H_SF16 = 96;
static const int DDS_DXGI_FORMAT_BC7_TYPELESS = 97;
static const int DDS_DXGI_FORMAT_BC7_UNORM = 98;
static const int DDS_DXGI_FORMAT_BC7_UNORM_SRGB = 99;
static const int DDS_DX10_RESOURCE_DIMENSION_TEXTURE2D = 3;
static const uint32 DDS_DX10_RESOURCE_MISC_TEXTURECUBE = 0x00000004;
static const int DDS_MAX_MIP_LEVELS = 32;
static const uint32 DDS_CAPS2_CUBEMAP = 0x00000200;
static const uint32 DDS_CAPS2_VOLUME = 0x00200000;

// DXT1, DXT5, RXGB, BC7 and BC6H can go to the GPU as they are. BC4, BC5 and
// the uncompressed layouts have no textureFormat_t of their own, so LoadDDS
// decodes them and the destination image stores the result the way it stores
// any decoded source (see R_DDSStoredFormatIsDirect).
enum ddsStoredFormat_t {
	DDS_STORED_FORMAT_INVALID,
	DDS_STORED_FORMAT_DXT1,
	DDS_STORED_FORMAT_DXT5,
	DDS_STORED_FORMAT_RXGB,
	DDS_STORED_FORMAT_BC7,
	DDS_STORED_FORMAT_BC4,			// one channel, read as luminance
	DDS_STORED_FORMAT_BC5,			// two channels; Z is rebuilt as for a normal map
	DDS_STORED_FORMAT_UNCOMPRESSED,	// 24 or 32 bpp, one byte per channel
	DDS_STORED_FORMAT_BC6H_UF16,	// half-float RGB; decoded to RGBA16F without BPTC
	DDS_STORED_FORMAT_BC6H_SF16
};

struct ddsFileInfo_t {
	ddsStoredFormat_t format;
	int dataOffset;
	int fileSize;
	int numLevels;
	uint32 width;
	uint32 height;
	// DDS_STORED_FORMAT_UNCOMPRESSED only: bytes per pixel, and the byte of a
	// pixel each of R, G, B and A sits at (-1 when there is no alpha)
	int bytesPerPixel;
	int channelOffset[4];
};

static int R_DDSBlockSizeForStoredFormat( ddsStoredFormat_t format ) {
	switch ( format ) {
		case DDS_STORED_FORMAT_DXT1:
		case DDS_STORED_FORMAT_BC4:
			return 8;
		case DDS_STORED_FORMAT_DXT5:
		case DDS_STORED_FORMAT_RXGB:
		case DDS_STORED_FORMAT_BC7:
		case DDS_STORED_FORMAT_BC5:
		case DDS_STORED_FORMAT_BC6H_UF16:
		case DDS_STORED_FORMAT_BC6H_SF16:
			return 16;
		default:
			return 0;
	}
}

// the formats R_LoadPrecompressedDDS uploads without decoding them first
static bool R_DDSStoredFormatIsDirect( ddsStoredFormat_t format ) {
	return format == DDS_STORED_FORMAT_DXT1 || format == DDS_STORED_FORMAT_DXT5 ||
		format == DDS_STORED_FORMAT_RXGB || format == DDS_STORED_FORMAT_BC7 ||
		format == DDS_STORED_FORMAT_BC6H_UF16 || format == DDS_STORED_FORMAT_BC6H_SF16;
}

// the bytes of one whole level of a stored format
static int64 R_DDSLevelBytes( ddsStoredFormat_t format, int bytesPerPixel, uint32 width, uint32 height ) {
	if ( format == DDS_STORED_FORMAT_UNCOMPRESSED ) {
		return (int64)width * height * bytesPerPixel;
	}
	const int64 blocksWide = Max( (int64)1, ( (int64)width + 3 ) >> 2 );
	const int64 blocksHigh = Max( (int64)1, ( (int64)height + 3 ) >> 2 );
	return blocksWide * blocksHigh * R_DDSBlockSizeForStoredFormat( format );
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

static bool R_DDSComputeLevelLayout( int fileSize, int dataOffset, uint32 width, uint32 height, ddsStoredFormat_t format, int numLevels, idList<int> *levelOffsets, idList<int> *levelSizes, int bytesPerPixel = 0 ) {
	const bool validFormat = format == DDS_STORED_FORMAT_UNCOMPRESSED ?
		( bytesPerPixel == 3 || bytesPerPixel == 4 ) : R_DDSBlockSizeForStoredFormat( format ) > 0;
	if ( !validFormat || numLevels <= 0|| numLevels > DDS_MAX_MIP_LEVELS || width == 0 || height == 0 ||
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
		const int64 levelSize = R_DDSLevelBytes( format, bytesPerPixel, levelWidth, levelHeight );
		if ( levelSize <= 0|| levelSize > 0x7fffffff || offset < 0 || offset > fileSize - (int)levelSize ) {
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

// DXT3 (BC2) files are left to LoadDDS (see there), so R_ParseDDSFileInfo
// rejects them. dataOffset, when given, receives where the first level starts.
static bool R_DDSFileIsDXT3( const byte *file, int fileSize, int *dataOffset = NULL ) {
	if ( fileSize < DDS_HEADER_BYTES ||
		 R_ReadLittleUInt32( file + 0 ) != R_MakeFourCC( 'D', 'D', 'S', ' ' ) ||
		 ( R_ReadLittleUInt32( file + 80 ) & DDS_PIXELFORMAT_FOURCC ) == 0 ) {
		return false;
	}
	const uint32 fourCC = R_ReadLittleUInt32( file + 84 );
	if ( fourCC == R_MakeFourCC( 'D', 'X', 'T', '3' ) ) {
		if ( dataOffset != NULL ) {
			*dataOffset = DDS_HEADER_BYTES;
		}
		return true;
	}
	if ( fourCC == R_MakeFourCC( 'D', 'X', '1', '0' ) && fileSize >= DDS_HEADER_BYTES + DDS_DXT10_HEADER_BYTES ) {
		const uint32 dxgiFormat = R_ReadLittleUInt32( file + DDS_HEADER_BYTES );
		if ( dxgiFormat >= DDS_DXGI_FORMAT_BC2_TYPELESS && dxgiFormat <= DDS_DXGI_FORMAT_BC2_UNORM_SRGB &&
			 R_ReadLittleUInt32( file + DDS_HEADER_BYTES + 4 ) == DDS_DX10_RESOURCE_DIMENSION_TEXTURE2D &&
			 R_ReadLittleUInt32( file + DDS_HEADER_BYTES + 12 ) == 1 ) {
			if ( dataOffset != NULL ) {
				*dataOffset = DDS_HEADER_BYTES + DDS_DXT10_HEADER_BYTES;
			}
			return true;
		}
	}
	return false;
}

/*
=============
R_DecodeBC4Channel

Decodes BC4 blocks (one 8-byte block per 4x4 texels, the same layout as a
DXT5 alpha block) into one byte of each RGBA texel. blockStride steps from one
block to the next, so BC5's interleaved red and green blocks decode with the
same routine. The interpolation follows the format's UNORM rule, rounded to
the nearest byte.
=============
*/
static void R_DecodeBC4Channel( const byte *blocks, int blockStride, byte *rgba, int channel, int paddedWidth, int paddedHeight ) {
	const int blocksWide = paddedWidth / 4;
	const int blocksHigh = paddedHeight / 4;
	for ( int by = 0; by < blocksHigh; by++ ) {
		for ( int bx = 0; bx < blocksWide; bx++ ) {
			const byte *block = blocks + (size_t)( by * blocksWide + bx ) * blockStride;
			const int e0 = block[0];
			const int e1 = block[1];
			int values[8];
			values[0] = e0;
			values[1] = e1;
			if ( e0 > e1 ) {
				for ( int i = 1; i <= 6; i++ ) {
					values[i + 1] = ( ( 7 - i ) * e0 + i * e1 + 3 ) / 7;
				}
			} else {
				for ( int i = 1; i <= 4; i++ ) {
					values[i + 1] = ( ( 5 - i ) * e0 + i * e1 + 2 ) / 5;
				}
				values[6] = 0;
				values[7] = 255;
			}
			uint64_t indices = 0;
			for ( int i = 0; i < 6; i++ ) {
				indices |= (uint64_t)block[2 + i] << ( 8 * i );
			}
			for ( int texel = 0; texel < 16; texel++ ) {
				const int x = bx * 4 + ( texel & 3 );
				const int y = by * 4 + ( texel >> 2 );
				rgba[ ( (size_t)y * paddedWidth + x ) * 4 + channel ] = (byte)values[ ( indices >> ( 3 * texel ) ) & 7 ];
			}
		}
	}
}

/*
=============
R_DecodeBC4BC5

BC4 becomes luminance (the channel copied to R, G and B), which is how a
specular or height mask reads in Quake 4's materials. BC5 becomes a normal:
X and Y from its two channels and Z rebuilt as sqrt( 1 - x^2 - y^2 ), the
layout every other decoded normal map arrives in.
=============
*/
static void R_DecodeBC4BC5( const byte *blocks, bool twoChannel, byte *rgba, int paddedWidth, int paddedHeight ) {
	const int stride = twoChannel ? 16 : 8;
	R_DecodeBC4Channel( blocks, stride, rgba, 0, paddedWidth, paddedHeight );
	if ( twoChannel ) {
		R_DecodeBC4Channel( blocks + 8, stride, rgba, 1, paddedWidth, paddedHeight );
	}
	const int texels = paddedWidth * paddedHeight;
	for ( int i = 0; i < texels; i++ ) {
		byte *texel = rgba + (size_t)i * 4;
		if ( twoChannel ) {
			const float x = texel[0] * ( 2.0f / 255.0f ) - 1.0f;
			const float y = texel[1] * ( 2.0f / 255.0f ) - 1.0f;
			const float zz = 1.0f - x * x - y * y;
			const float z = zz > 0.0f ? sqrtf( zz ) : 0.0f;
			const int blue = (int)( ( z * 0.5f + 0.5f ) * 255.0f + 0.5f );
			texel[2] = (byte)( blue < 0 ? 0 : ( blue > 255 ? 255 : blue ) );
		} else {
			texel[1] = texel[2] = texel[0];
		}
		texel[3] = 255;
	}
}

// Copies an uncompressed DDS level into RGBA by the byte offsets its header gave.
static void R_DecodeUncompressedDDS( const byte *pixels, const ddsFileInfo_t &info, byte *rgba, int width, int height ) {
	const int64 texels = (int64)width * height;
	for ( int64 i = 0; i < texels; i++ ) {
		const byte *source = pixels + i * info.bytesPerPixel;
		byte *dest = rgba + i * 4;
		dest[0] = source[ info.channelOffset[0] ];
		dest[1] = source[ info.channelOffset[1] ];
		dest[2] = source[ info.channelOffset[2] ];
		dest[3] = info.channelOffset[3] >= 0 ? source[ info.channelOffset[3] ] : 255;
	}
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
		 ( pixelFormatFlags & ( DDS_PIXELFORMAT_FOURCC | DDS_PIXELFORMAT_RGB ) ) == 0 ) {
		return false;
	}

	int localDataOffset = DDS_HEADER_BYTES;
	ddsStoredFormat_t format = DDS_STORED_FORMAT_INVALID;
	int bytesPerPixel = 0;
	int channelOffset[4] = { -1, -1, -1, -1 };
	if ( ( pixelFormatFlags & DDS_PIXELFORMAT_FOURCC ) == 0 ) {
		// Uncompressed: accepted when every channel is one whole byte, which
		// covers what image tools write (A8R8G8B8, X8R8G8B8, A8B8G8R8, R8G8B8).
		const uint32 bitCount = R_ReadLittleUInt32( header + 88 );
		const uint32 masks[4] = {
			R_ReadLittleUInt32( header + 92 ), R_ReadLittleUInt32( header + 96 ), R_ReadLittleUInt32( header + 100 ),
			( pixelFormatFlags & DDS_PIXELFORMAT_ALPHAPIXELS ) != 0 ? R_ReadLittleUInt32( header + 104 ) : 0u };
		if ( bitCount != 24 && bitCount != 32 ) {
			return false;
		}
		bytesPerPixel = (int)bitCount / 8;
		for ( int channel = 0; channel < 4; channel++ ) {
			if ( channel == 3 && masks[channel] == 0 ) {
				continue;	// no alpha: read as opaque
			}
			for ( int byteIndex = 0; byteIndex < bytesPerPixel; byteIndex++ ) {
				if ( masks[channel] == ( 0xffu << ( byteIndex * 8 ) ) ) {
					channelOffset[channel] = byteIndex;
				}
			}
			if ( channelOffset[channel] < 0 ) {
				return false;
			}
		}
		format = DDS_STORED_FORMAT_UNCOMPRESSED;
	} else if ( fourCC == R_MakeFourCC( 'D', 'X', 'T', '1' ) ) {
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
		if ( resourceDimension == DDS_DX10_RESOURCE_DIMENSION_TEXTURE2D &&
			 ( miscFlag & DDS_DX10_RESOURCE_MISC_TEXTURECUBE ) == 0 &&
			 arraySize == 1 ) {
			if ( dxgiFormat >= DDS_DXGI_FORMAT_BC1_TYPELESS && dxgiFormat <= DDS_DXGI_FORMAT_BC1_UNORM_SRGB ) {
				format = DDS_STORED_FORMAT_DXT1;
			} else if ( dxgiFormat >= DDS_DXGI_FORMAT_BC3_TYPELESS && dxgiFormat <= DDS_DXGI_FORMAT_BC3_UNORM_SRGB ) {
				format = DDS_STORED_FORMAT_DXT5;
			} else if ( dxgiFormat == DDS_DXGI_FORMAT_BC4_TYPELESS || dxgiFormat == DDS_DXGI_FORMAT_BC4_UNORM ) {
				format = DDS_STORED_FORMAT_BC4;	// the _SNORM variants stay rejected: nothing here is signed
			} else if ( dxgiFormat == DDS_DXGI_FORMAT_BC5_TYPELESS || dxgiFormat == DDS_DXGI_FORMAT_BC5_UNORM ) {
				format = DDS_STORED_FORMAT_BC5;
			} else if ( dxgiFormat >= DDS_DXGI_FORMAT_BC7_TYPELESS && dxgiFormat <= DDS_DXGI_FORMAT_BC7_UNORM_SRGB ) {
				format = DDS_STORED_FORMAT_BC7;
			} else if ( dxgiFormat == DDS_DXGI_FORMAT_BC6H_TYPELESS || dxgiFormat == DDS_DXGI_FORMAT_BC6H_UF16 ) {
				format = DDS_STORED_FORMAT_BC6H_UF16;	// a typeless file is read as unsigned, as texconv writes it
			} else if ( dxgiFormat == DDS_DXGI_FORMAT_BC6H_SF16 ) {
				format = DDS_STORED_FORMAT_BC6H_SF16;
			} else if ( dxgiFormat >= DDS_DXGI_FORMAT_R8G8B8A8_TYPELESS && dxgiFormat <= DDS_DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ) {
				format = DDS_STORED_FORMAT_UNCOMPRESSED;
				bytesPerPixel = 4;
				channelOffset[0] = 0; channelOffset[1] = 1; channelOffset[2] = 2; channelOffset[3] = 3;
			} else if ( dxgiFormat == DDS_DXGI_FORMAT_B8G8R8A8_UNORM || dxgiFormat == DDS_DXGI_FORMAT_B8G8R8A8_TYPELESS ||
						dxgiFormat == DDS_DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || dxgiFormat == DDS_DXGI_FORMAT_B8G8R8X8_UNORM ||
						dxgiFormat == DDS_DXGI_FORMAT_B8G8R8X8_TYPELESS || dxgiFormat == DDS_DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ) {
				const bool hasAlpha = dxgiFormat == DDS_DXGI_FORMAT_B8G8R8A8_UNORM || dxgiFormat == DDS_DXGI_FORMAT_B8G8R8A8_TYPELESS ||
					dxgiFormat == DDS_DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
				format = DDS_STORED_FORMAT_UNCOMPRESSED;
				bytesPerPixel = 4;
				channelOffset[0] = 2; channelOffset[1] = 1; channelOffset[2] = 0; channelOffset[3] = hasAlpha ? 3 : -1;
			}
			// BC2 stays with LoadDDS, as DXT3 does; any other DXGI format is unsupported
			if ( format != DDS_STORED_FORMAT_INVALID ) {
				localDataOffset += DDS_DXT10_HEADER_BYTES;
			}
		}
	} else if ( fourCC == R_MakeFourCC( 'B', 'C', '7', '0' ) ||
				fourCC == R_MakeFourCC( 'B', 'C', '7', 'L' ) ||
				fourCC == R_MakeFourCC( 'B', 'C', '7', ' ' ) ) {
		format = DDS_STORED_FORMAT_BC7;
	} else if ( fourCC == R_MakeFourCC( 'B', 'C', '6', 'H' ) ) {
		// no standard FourCC exists; a few older exporters wrote this one
		format = DDS_STORED_FORMAT_BC6H_UF16;
	} else if ( fourCC == R_MakeFourCC( 'A', 'T', 'I', '1' ) || fourCC == R_MakeFourCC( 'B', 'C', '4', 'U' ) ) {
		format = DDS_STORED_FORMAT_BC4;
	} else if ( fourCC == R_MakeFourCC( 'A', 'T', 'I', '2' ) || fourCC == R_MakeFourCC( 'B', 'C', '5', 'U' ) ) {
		// NVTT, texconv and Compressonator all write ATI2 with X in the first
		// block, which is BC5's order, so the two FourCCs read alike
		format = DDS_STORED_FORMAT_BC5;
	}

	const int numLevels = R_DDSStoredMipLevelCount( headerFlags, ddsMipMapCount, ddsWidth, ddsHeight );
	if ( format == DDS_STORED_FORMAT_INVALID || numLevels <= 0 ||
		 !R_DDSComputeLevelLayout( fileSize, localDataOffset, ddsWidth, ddsHeight, format, numLevels, NULL, NULL, bytesPerPixel ) ) {
		return false;
	}

	info.format = format;
	info.dataOffset = localDataOffset;
	info.fileSize = fileSize;
	info.numLevels = numLevels;
	info.width = ddsWidth;
	info.height = ddsHeight;
	info.bytesPerPixel = bytesPerPixel;
	for ( int channel = 0; channel < 4; channel++ ) {
		info.channelOffset[channel] = channelOffset[channel];
	}
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
		*timestamp = file->ContentStamp();
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
		!token.Icmp( "makeAlpha" ) ||
		!token.Icmp( "packORM" );
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
		// Like DXT, BC7 goes up as it is when it can, and LoadDDS decodes it on
		// the CPU when it feeds an image program or the renderer has no BPTC.
		ddsName = candidateName;
		if ( timestamp != NULL ) {
			*timestamp = ddsTimestamp;
		}
		if ( precompressedDDS != NULL ) {
			*precompressedDDS = allowPrecompressedDDS && ImageTools_GetCompressionCaps().bptcTextureCompressionAvailable;
		}
		return true;
	}

	if ( info.format == DDS_STORED_FORMAT_BC6H_UF16 || info.format == DDS_STORED_FORMAT_BC6H_SF16 ) {
		// HDR data. It always takes the direct path: up as it is where the
		// renderer has BPTC, else decoded there to RGBA16F, which keeps its
		// range. Only an image program (CPU, 8-bit) decodes it through LoadDDS,
		// clamped to [0, 1].
		ddsName = candidateName;
		if ( timestamp != NULL ) {
			*timestamp = ddsTimestamp;
		}
		if ( precompressedDDS != NULL ) {
			*precompressedDDS = allowPrecompressedDDS;
		}
		return true;
	}

	if ( info.format == DDS_STORED_FORMAT_BC4 || info.format == DDS_STORED_FORMAT_BC5 ) {
		// Texture packs ship BC5 normal maps and BC4 masks. No renderer format
		// stands for them, so LoadDDS always decodes them; the result is cached
		// and stored like any other decoded source (image_useNormalCompression
		// re-encodes a BC5 normal map as DXT5 RXGB).
		ddsName = candidateName;
		if ( timestamp != NULL ) {
			*timestamp = ddsTimestamp;
		}
		if ( precompressedDDS != NULL ) {
			*precompressedDDS = false;
		}
		return true;
	}

	if ( rejectReason != NULL ) {
		// Quake 4 ships an uncompressed DDS beside the .tga of 18 HUD images, and
		// the .tga stays their source: such a file is read only when named outright.
		*rejectReason = info.format == DDS_STORED_FORMAT_UNCOMPRESSED ?
			"uncompressed DDS is read only when a material names it" : "unsupported DDS pixel format";
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

static const char *const R_RETAIL_PROGRAM_DDS_ORIGIN = "retail progimg/";

class idSuppressRetailProgramDDS {
public:
	idSuppressRetailProgramDDS() { ddsRetailProgramSuppressed++; }
	~idSuppressRetailProgramDDS() { ddsRetailProgramSuppressed--; }
};

// the dds/ shadow and the Quake 4-style sibling of a plain image name
static void R_AppendPlainImageDDSCandidates( const char *imageName, idStrList &candidates, idStrList &origins, const char *shadowOrigin, const char *siblingOrigin ) {
	idStr sourceName = imageName;
	sourceName.DefaultFileExtension( ".tga" );
	sourceName.BackSlashesToSlashes();
	sourceName.ToLower();

	idStr ext;
	sourceName.ExtractFileExtension( ext );
	if ( ext != "tga" && ext != "jpg" && ext != "png" ) {
		return;
	}
	if ( !R_ImageNameHasDDSShadowPrefix( sourceName ) ) {
		idStr shadowDDSName = "dds/";
		shadowDDSName += sourceName;
		shadowDDSName.StripFileExtension();
		shadowDDSName.DefaultFileExtension( ".dds" );
		R_AppendDDSCandidate( candidates, origins, shadowDDSName, shadowOrigin );
	}

	idStr siblingDDSName = sourceName;
	siblingDDSName.StripFileExtension();
	siblingDDSName.DefaultFileExtension( ".dds" );
	R_AppendDDSCandidate( candidates, origins, siblingDDSName, siblingOrigin );
}

/*
=============
R_DownsizeProgramInput

The plain image inside "downsize( <image>, <levels> )", or false for anything
else. openQ4's downsize() returns its input unchanged (image_downSize and its
limits do the shrinking for every image alike), so a DDS of that input is
exactly what the program would produce.
=============
*/
static bool R_DownsizeProgramInput( const char *imageProgram, idStr &input ) {
	if ( imageProgram == NULL || imageProgram[0] == '\0' ) {
		return false;
	}

	idLexer src;
	src.LoadMemory( imageProgram, idLib::SizeToInt( strlen( imageProgram ), "R_DownsizeProgramInput" ), imageProgram );
	src.SetFlags( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );

	idToken token;
	idToken image;
	idToken levels;
	const bool matched = src.ReadToken( &token ) && token.Icmp( "downsize" ) == 0 &&
		src.ReadToken( &token ) && token == "(" &&
		src.ReadToken( &image ) && !R_ImageNameIsProgramOperator( image ) &&
		src.ReadToken( &token ) && token == "," &&
		src.ReadToken( &levels ) && levels.type == TT_NUMBER &&
		src.ReadToken( &token ) && token == ")" &&
		!src.ReadToken( &token );
	src.FreeSource();
	if ( !matched ) {
		return false;
	}
	input = image;
	return true;
}

/*
=============
R_BuildDDSCandidates

Ordered list of DDS files that may stand in for an image source. User-supplied
replacements are probed before the retail progimg/ tree: the retail tree exists
for nearly every image-program result, so probing it first made a user's dds/
pack a silent no-op for every bump map and most speculars.

downsize( <image>, N ) also takes a DDS of its input, ahead of the retail
result. Quake 4's materials wrap most character, weapon and monster textures in
downsize(), so without this a texture pack that replaces those images by their
own names never reached the screen: its BC7 files were turned away as image
program inputs, and the stock progimg/downsize/ file won.
=============
*/
static void R_BuildDDSCandidates( const char *cname, idStrList &candidates, idStrList &origins ) {
	idStr canonicalDDSName;
	R_ImageProgramToCompressedFileName( cname, canonicalDDSName );
	R_AppendDDSCandidate( candidates, origins, canonicalDDSName, "dds/ (dhewm3 naming)" );

	if ( R_IsPlainImageSourceName( cname ) ) {
		R_AppendPlainImageDDSCandidates( cname, candidates, origins,
			"dds/ (asset path)", "alongside the original asset (Quake 4 style)" );
	}

	idStr downsizeInput;
	if ( R_DownsizeProgramInput( cname, downsizeInput ) ) {
		idStr inputDDSName;
		R_ImageProgramToCompressedFileName( downsizeInput, inputDDSName );
		R_AppendDDSCandidate( candidates, origins, inputDDSName, "downsize() input, dds/ (dhewm3 naming)" );
		R_AppendPlainImageDDSCandidates( downsizeInput, candidates, origins,
			"downsize() input, dds/ (asset path)", "downsize() input, alongside the original asset" );
	}

	// stock retail data last, so any user replacement wins
	if ( ddsRetailProgramSuppressed == 0 && R_ImageHasRetailProgramDDSName( cname ) ) {
		idStr retailDDSName;
		R_ImageProgramToRetailCompressedFileName( cname, retailDDSName );
		R_AppendDDSCandidate( candidates, origins, retailDDSName, R_RETAIL_PROGRAM_DDS_ORIGIN );
	}
}

static bool R_HasUncompressedLegacyScopeMask( const char *name ) {
	if ( !R_IsLegacyScopeDistortionMask( name ) ) {
		return false;
	}
	idStr source = name;
	source.BackSlashesToSlashes();
	source.DefaultFileExtension( ".tga" );
	return fileSystem->ReadFile( source, NULL ) > 0;
}

// R_RetailProgramInputReplaced resolves inputs through the resolver below;
// their misses are not what the log was asked about
static int ddsResolveQuiet = 0;

/*
=============
R_ImageProgramInputNames

The images an image program reads: every name in it that is not an operator,
a number or punctuation. The _white, _black and _flat stand-ins are left out.
=============
*/
static void R_ImageProgramInputNames( const char *imageProgram, idStrList &inputs ) {
	inputs.Clear();
	if ( imageProgram == NULL || imageProgram[0] == '\0' ) {
		return;
	}

	idLexer src;
	src.LoadMemory( imageProgram, idLib::SizeToInt( strlen( imageProgram ), "R_ImageProgramInputNames" ), imageProgram );
	src.SetFlags( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );

	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token.type != TT_NAME || R_ImageNameIsProgramOperator( token ) || token[0] == '_' ) {
			continue;
		}
		inputs.AddUnique( token );
	}
	src.FreeSource();
}

/*
=============
R_ResolveImageSourceFile

The file R_LoadImageInternal reads for a plain image name: a DDS replacement
first, then the source formats in the order it tries them.
=============
*/
static bool R_ResolveImageSourceFile( const char *imageName, idStr &path ) {
	idStr name = imageName;
	name.DefaultFileExtension( ".tga" );
	name.BackSlashesToSlashes();
	name.ToLower();

	idStr ext;
	name.ExtractFileExtension( ext );
	if ( ext != "dds" ) {
		idStr ddsName;
		ddsResolveQuiet++;
		const bool replaced = R_ResolvePreferredDDSImageSource( name.c_str(), ddsName, NULL, false, NULL );
		ddsResolveQuiet--;
		if ( replaced ) {
			path = ddsName;
			return true;
		}
	}

	static const char *const tgaOrder[] = { "tga", "jpg", "png", "dds", NULL };
	static const char *const jpgOrder[] = { "jpg", "tga", "dds", NULL };
	static const char *const pngOrder[] = { "png", "tga", "jpg", "dds", NULL };
	static const char *const ddsOrder[] = { "dds", NULL };
	const char *const *order = ext == "jpg" ? jpgOrder : ext == "png" ? pngOrder : ext == "dds" ? ddsOrder : tgaOrder;
	idStr stem = name;
	stem.StripFileExtension();
	for ( int i = 0; order[i] != NULL; i++ ) {
		idStr candidate = stem;
		candidate += ".";
		candidate += order[i];
		if ( fileSystem->ReadFile( candidate.c_str(), NULL, NULL ) > 0 ) {
			path = candidate;
			return true;
		}
	}
	return false;
}

/*
=============
R_DescribeLooseImageSources

Names the files on disk that make a texture pack's DDS look out of date. A pk4
member never does: its stamp orders as 0, so only a loose file, with a real
modification time, can be newer than a DDS inside a pack.
=============
*/
int R_DescribeLooseImageSources( const char *imageProgram, idStr &paths ) {
	paths.Clear();
	idStrList inputs;
	R_ImageProgramInputNames( imageProgram, inputs );
	int found = 0;
	for ( int i = 0; i < inputs.Num(); i++ ) {
		idStr path;
		if ( !R_ResolveImageSourceFile( inputs[i], path ) ) {
			continue;
		}
		ID_TIME_T stamp = FILE_NOT_FOUND_TIMESTAMP;
		fileSystem->ReadFileContentStamp( path.c_str(), NULL, &stamp );
		if ( stamp == FILE_NOT_FOUND_TIMESTAMP || FS_IsPakMemberContentStamp( stamp ) ) {
			continue;
		}
		idFile *file = fileSystem->OpenFileRead( path.c_str(), false );
		if ( file == NULL ) {
			continue;
		}
		if ( found > 0 ) {
			paths += ", ";
		}
		paths += file->GetFullPath();
		fileSystem->CloseFile( file );
		found++;
	}
	return found;
}

/*
=============
R_RetailProgramInputReplaced

Quake 4's progimg/ files are image programs computed from Quake 4's own
images. Once any input comes from somewhere else, such as a texture pack's
_local or _h replacement, the stored result no longer matches what the program
makes, and it must be computed again from the replacement.
=============
*/
static bool R_RetailProgramInputReplaced( const char *imageProgram, idStr &replacedPath ) {
	idStrList inputs;
	R_ImageProgramInputNames( imageProgram, inputs );
	for ( int i = 0; i < inputs.Num(); i++ ) {
		idStr path;
		if ( R_ResolveImageSourceFile( inputs[i], path ) && fileSystem->GetFileOrigin( path.c_str() ) == FS_FILE_ORIGIN_ADDED ) {
			replacedPath = path;
			return true;
		}
	}
	return false;
}

/*
=============
R_ResolvePreferredDDSImageSource
=============
*/
bool R_ResolvePreferredDDSImageSource( const char *cname, idStr &ddsName, ID_TIME_T *timestamp, bool allowPrecompressedDDS, bool *precompressedDDS, bool *retailResultSuperseded ) {
	if ( precompressedDDS != NULL ) {
		*precompressedDDS = false;
	}
	if ( retailResultSuperseded != NULL ) {
		*retailResultSuperseded = false;
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

	const bool logMisses = image_showPrecompressedTextures.GetBool() && ddsResolveQuiet == 0;
	idStr rejections;
	const bool preferScopeSource = R_HasUncompressedLegacyScopeMask( cname );

	for ( int i = 0; i < candidates.Num(); i++ ) {
		// Keep intentional dds/ replacement packs eligible. Only the retail
		// sibling DDS loses priority to the precise source mask.
		if ( preferScopeSource && candidates[i].IcmpPrefix( "dds/" ) != 0 ) {
			continue;
		}
		const char *rejectReason = NULL;
		if ( R_TryResolvePreferredDDSImageSource( candidates[i], ddsName, timestamp, allowPrecompressedDDS, precompressedDDS, &rejectReason ) ) {
			idStr replacedInput;
			if ( origins[i] == R_RETAIL_PROGRAM_DDS_ORIGIN &&
				 fileSystem->GetFileOrigin( candidates[i].c_str() ) == FS_FILE_ORIGIN_SHIPPED &&
				 R_RetailProgramInputReplaced( cname, replacedInput ) ) {
				ddsName.Clear();
				if ( timestamp != NULL ) {
					*timestamp = FILE_NOT_FOUND_TIMESTAMP;
				}
				if ( precompressedDDS != NULL ) {
					*precompressedDDS = false;
				}
				if ( retailResultSuperseded != NULL ) {
					*retailResultSuperseded = true;
				}
				if ( logMisses ) {
					idStr reason = "Quake 4's own result, but its input ";
					reason += replacedInput;
					reason += " comes from outside the shipped packs";
					rejections += va( "   %-64s [%s] %s\n", candidates[i].c_str(), origins[i].c_str(), reason.c_str() );
				}
				continue;
			}
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

static imageDDSPrefetchTake_t ddsPrefetchTake = NULL;

void R_SetDDSPrefetchSource( imageDDSPrefetchTake_t take ) {
	ddsPrefetchTake = take;
}

/*
=============
R_LoadPrecompressedDDS
=============
*/
bool R_LoadPrecompressedDDS( const char *cname, idBinaryImage &image, ID_TIME_T *timestamp, textureUsage_t usage, const imageDownsizePolicy_t &downsizePolicy, bool useMipmaps, imageReductionResult_t* reduction, const imageFileContent_t* expected ) {
    const imageFileContent_t requested = expected ? *expected : imageFileContent_t{};
    if (expected && (!R_ImageFileContentValid(requested) || requested.kind != IFC_DIRECT_DDS)) return false;
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

    if (expected && strcmp(name.c_str(),requested.qpath) != 0) return false;
	// Read into a buffer this library owns rather than fileSystem->ReadFile: the
	// mip payloads stay behind as views into it inside the idBinaryImage, and its
	// eventual Mem_Free in idBinaryImage::Clear must pair with this binary's
	// allocator (the renderer modules carry their own idlib heap).
	byte *buffer = NULL;
    struct ReleaseBuffer { byte*& bytes; ~ReleaseBuffer() { if (bytes) Mem_Free(bytes); } } releaseBuffer{buffer};
	int fileSize = 0;
    imageFileContent_t actualContent;
    bool contentObserved = false;
	// A level load may already have read and fingerprinted this exact file on a
	// worker thread; that buffer also comes from this binary's heap.
	const bool prefetched = expected == NULL && ddsPrefetchTake != NULL
		&& ddsPrefetchTake( name.c_str(), &buffer, &fileSize, timestamp, &actualContent, &contentObserved );
	if ( !prefetched ) {
		idFile *ddsFile = fileSystem->OpenFileRead( name.c_str() );
	    struct CloseFile { idFile*& file; ~CloseFile() { if (file) fileSystem->CloseFile(file); } } closeFile{ddsFile};
		if ( ddsFile == NULL ) {
			if ( timestamp != NULL ) {
				*timestamp = FILE_NOT_FOUND_TIMESTAMP;
			}
			return false;
		}
		if ( timestamp != NULL ) {
			*timestamp = ddsFile->ContentStamp();
		}
		fileSize = ddsFile->Length();
	    if (expected && (fileSize < 0 || std::uint64_t(fileSize) != requested.bytes)) return false;
		buffer = fileSize >= DDS_HEADER_BYTES ? (byte *)Mem_Alloc( fileSize ) : NULL;
		const bool readOk = buffer != NULL && ddsFile->Read( buffer, fileSize ) == fileSize;
		fileSystem->CloseFile( ddsFile );
	    ddsFile = NULL;
		if ( !readOk ) return false;
	    contentObserved = R_MakeImageFileContent(IFC_DIRECT_DDS,name.c_str(),buffer,fileSize,actualContent);
	}
    if (expected && (!contentObserved || !R_ImageFileContentEqual(requested,actualContent))) return false;

	bool loaded = false;
	do {
		ddsFileInfo_t info;
		if ( !R_ParseDDSFileInfo( buffer, fileSize, fileSize, info ) ) {
			// DXT3 is valid but never uploaded as it is: LoadDDS decodes it
			if ( !R_DDSFileIsDXT3( buffer, fileSize ) ) {
				idLib::Warning( "Image file '%s' has an invalid or incomplete supported DDS layout", name.c_str() );
			}
			break;
		}

		if ( !R_DDSStoredFormatIsDirect( info.format ) ) {
			break;	// BC4, BC5 and uncompressed data are LoadDDS's to decode
		}
		const bool bc6h = info.format == DDS_STORED_FORMAT_BC6H_UF16 || info.format == DDS_STORED_FORMAT_BC6H_SF16;
		const bool decodeBC6H = bc6h && !ImageTools_GetCompressionCaps().bptcTextureCompressionAvailable;
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
			case DDS_STORED_FORMAT_BC6H_UF16:
				textureFormat = FMT_BC6H_UF16;
				break;
			case DDS_STORED_FORMAT_BC6H_SF16:
				textureFormat = FMT_BC6H_SF16;
				break;
			default:
				break;
		}
		if ( textureFormat == FMT_NONE || selectedLevels <= 0 ) {
			break;
		}

		if ( decodeBC6H ) {
			// No BPTC: decode to RGBA16F, which every renderer samples, rather
			// than to 8-bit through LoadDDS, which would clip the HDR range
			if ( !R_LoadBC6HAsHalfFloat( image, buffer, levelOffsets.Ptr() + firstLevel, selectedWidth, selectedHeight, selectedLevels,
					info.format == DDS_STORED_FORMAT_BC6H_SF16 ) ) {
				idLib::Warning( "Image file '%s' could not be decoded from BC6H", name.c_str() );
				break;
			}
			// the file buffer is released on return; the binary image owns the halves
	        if (contentObserved) image.ObserveFileContent(actualContent);
	        if (reduction) *reduction = selected;
			loaded = true;
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
        if (contentObserved) image.ObserveFileContent(actualContent);
        if (reduction) *reduction = selected;
		loaded = true;
	} while ( false );

	return loaded;
}

/*
=============
R_LoadBC6HAsHalfFloat

Decodes the selected BC6H levels of a DDS file to RGBA16F, for a renderer with
no BPTC. Same picture and range, eight times the memory. levelOffsets points
at the first selected level's offset into fileBuffer.
=============
*/
static bool R_LoadBC6HAsHalfFloat( idBinaryImage &image, const byte *fileBuffer, const int *levelOffsets, int width, int height, int numLevels, bool isSigned ) {
	idList<int> halfOffsets;
	idList<int> halfSizes;
	halfOffsets.SetNum( numLevels );
	halfSizes.SetNum( numLevels );
	int64 totalBytes = 0;
	for ( int level = 0; level < numLevels; level++ ) {
		const int64 w = Max( 1, width >> level );
		const int64 h = Max( 1, height >> level );
		halfOffsets[ level ] = (int)totalBytes;
		halfSizes[ level ] = (int)( w * h * 8 );
		totalBytes += w * h * 8;
		if ( totalBytes > 0x7fffffff ) {
			return false;
		}
	}
	byte *halves = (byte *)Mem_Alloc( (int)totalBytes );
	if ( halves == NULL ) {
		return false;
	}
	for ( int level = 0; level < numLevels; level++ ) {
		const int w = Max( 1, width >> level );
		const int h = Max( 1, height >> level );
		const int paddedW = ( w + 3 ) & ~3;
		const int paddedH = ( h + 3 ) & ~3;
		unsigned short *out = (unsigned short *)( halves + halfOffsets[ level ] );
		if ( paddedW == w && paddedH == h ) {
			BC6H_DecodeImage( fileBuffer + levelOffsets[ level ], isSigned, out, w, h );
			continue;
		}
		idTempArray<unsigned short> padded( paddedW * paddedH * 4 );
		BC6H_DecodeImage( fileBuffer + levelOffsets[ level ], isSigned, padded.Ptr(), paddedW, paddedH );
		for ( int y = 0; y < h; y++ ) {
			memcpy( out + y * w * 4, padded.Ptr() + y * paddedW * 4, w * 4 * sizeof( unsigned short ) );
		}
	}
	// Load2DFromOwnedCompressedData takes any level layout; the name is history
	image.Load2DFromOwnedCompressedData( width, height, numLevels, FMT_RGBA16F, CFM_DEFAULT, halves, halfOffsets.Ptr(), halfSizes.Ptr() );
	return true;
}

/*
=============
R_WriteDXT3Alpha

DXT3 (BC2) blocks carry the same colour half as DXT5 ones, so LoadDDS decodes
their colour as DXT5 and then writes the real alpha over what that decode made
of the first eight bytes: sixteen explicit 4-bit values, row by row, low nibble
first.
=============
*/
static void R_WriteDXT3Alpha( const byte *blocks, byte *rgba, const int paddedWidth, const int paddedHeight ) {
	const int blocksWide = paddedWidth / 4;
	const int blocksHigh = paddedHeight / 4;
	for ( int by = 0; by < blocksHigh; by++ ) {
		for ( int bx = 0; bx < blocksWide; bx++ ) {
			const byte *block = blocks + ( by * blocksWide + bx ) * 16;
			for ( int texel = 0; texel < 16; texel++ ) {
				const int nibble = ( block[ texel >> 1 ] >> ( ( texel & 1 ) * 4 ) ) & 0x0f;
				const int x = bx * 4 + ( texel & 3 );
				const int y = by * 4 + ( texel >> 2 );
				rgba[ ( y * paddedWidth + x ) * 4 + 3 ] = (byte)( nibble * 17 );
			}
		}
	}
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

	// BC6H: the same 16-byte blocks, so the same layout; typeless reads as
	// unsigned, and both variants go up as they are
	const int bc6hFormats[3] = { DDS_DXGI_FORMAT_BC6H_TYPELESS, DDS_DXGI_FORMAT_BC6H_UF16, DDS_DXGI_FORMAT_BC6H_SF16 };
	for ( int i = 0; i < 3; i++ ) {
		R_WriteLittleUInt32( header + DDS_HEADER_BYTES, (uint32)bc6hFormats[i] );
		const ddsStoredFormat_t expected = i == 2 ? DDS_STORED_FORMAT_BC6H_SF16 : DDS_STORED_FORMAT_BC6H_UF16;
		if ( !R_ParseDDSFileInfo( header, sizeof( header ), completeFileSize, info ) ||
			 info.format != expected || info.numLevels != 4 || !R_DDSStoredFormatIsDirect( info.format ) ) {
			common->Warning( "Image DDS self-test failed: valid DX10 BC6H metadata (DXGI format %d) was rejected", bc6hFormats[i] );
			return false;
		}
		if ( R_ParseDDSFileInfo( header, sizeof( header ), completeFileSize - 1, info ) ) {
			common->Warning( "Image DDS self-test failed: truncated BC6H mip data was accepted" );
			return false;
		}
	}
	R_WriteLittleUInt32( header + DDS_HEADER_BYTES, DDS_DXGI_FORMAT_BC7_UNORM );

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

	// DXT3 never goes up as it is: Quake 4's font atlases are DXT3 and have to
	// load from the .tga beside each one. LoadDDS decodes DXT3 instead.
	R_WriteLittleUInt32( header + 84, R_MakeFourCC( 'D', 'X', 'T', '3' ) );
	if ( R_ParseDDSFileInfo( header, DDS_HEADER_BYTES, completeRXGBFileSize, info ) ) {
		common->Warning( "Image DDS self-test failed: DXT3 data was accepted for direct upload" );
		return false;
	}

	// one DXT3 block: alphas 0 to 15, row by row, over solid red
	byte dxt3Block[16];
	for ( int texel = 0; texel < 16; texel += 2 ) {
		dxt3Block[ texel >> 1 ] = (byte)( texel | ( ( texel + 1 ) << 4 ) );
	}
	dxt3Block[8] = 0x00;	// color0 0xf800, red
	dxt3Block[9] = 0xf8;
	memset( dxt3Block + 10, 0, 6 );	// color1 black, every index color0
	byte dxt3Rgba[4 * 4 * 4];
	idDxtDecoder dxt3Decoder;
	dxt3Decoder.DecompressImageDXT5( dxt3Block, dxt3Rgba, 4, 4 );
	R_WriteDXT3Alpha( dxt3Block, dxt3Rgba, 4, 4 );
	for ( int texel = 0; texel < 16; texel++ ) {
		const byte *rgba = dxt3Rgba + texel * 4;
		if ( rgba[0] != 255 || rgba[1] != 0 || rgba[2] != 0 || rgba[3] != texel * 17 ) {
			common->Warning( "Image DDS self-test failed: DXT3 texel %d decoded to %d %d %d %d", texel, rgba[0], rgba[1], rgba[2], rgba[3] );
			return false;
		}
	}

	// BC1 and BC3 behind a DX10 header (texconv's sRGB output) read as DXT1/DXT5
	memset( header, 0, sizeof( header ) );
	R_WriteLittleUInt32( header + 0, R_MakeFourCC( 'D', 'D', 'S', ' ' ) );
	R_WriteLittleUInt32( header + 4, 124 );
	R_WriteLittleUInt32( header + 12, 4 );
	R_WriteLittleUInt32( header + 16, 4 );
	R_WriteLittleUInt32( header + 76, 32 );
	R_WriteLittleUInt32( header + 80, DDS_PIXELFORMAT_FOURCC );
	R_WriteLittleUInt32( header + 84, R_MakeFourCC( 'D', 'X', '1', '0' ) );
	R_WriteLittleUInt32( header + DDS_HEADER_BYTES + 4, DDS_DX10_RESOURCE_DIMENSION_TEXTURE2D );
	R_WriteLittleUInt32( header + DDS_HEADER_BYTES + 12, 1 );
	const struct dx10Case_t { int dxgi; ddsStoredFormat_t format; int levelBytes; } dx10Cases[] = {
		{ DDS_DXGI_FORMAT_BC1_UNORM_SRGB, DDS_STORED_FORMAT_DXT1, 8 },
		{ DDS_DXGI_FORMAT_BC3_UNORM, DDS_STORED_FORMAT_DXT5, 16 },
		{ DDS_DXGI_FORMAT_BC4_UNORM, DDS_STORED_FORMAT_BC4, 8 },
		{ DDS_DXGI_FORMAT_BC5_UNORM, DDS_STORED_FORMAT_BC5, 16 },
		{ DDS_DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DDS_STORED_FORMAT_UNCOMPRESSED, 64 },
		{ 81, DDS_STORED_FORMAT_INVALID, 8 },		// BC4_SNORM
		{ DDS_DXGI_FORMAT_BC2_UNORM_SRGB, DDS_STORED_FORMAT_INVALID, 16 }
	};
	for ( int i = 0; i < (int)( sizeof( dx10Cases ) / sizeof( dx10Cases[0] ) ); i++ ) {
		R_WriteLittleUInt32( header + DDS_HEADER_BYTES, dx10Cases[i].dxgi );
		const bool accepted = R_ParseDDSFileInfo( header, sizeof( header ), (int)sizeof( header ) + dx10Cases[i].levelBytes, info );
		if ( accepted != ( dx10Cases[i].format != DDS_STORED_FORMAT_INVALID ) ||
			 ( accepted && ( info.format != dx10Cases[i].format || info.dataOffset != (int)sizeof( header ) ) ) ) {
			common->Warning( "Image DDS self-test failed: DX10 DXGI format %d read as stored format %d", dx10Cases[i].dxgi, accepted ? (int)info.format : -1 );
			return false;
		}
	}
	// the last case left BC2 in the header: R_DDSFileIsDXT3 must claim it for LoadDDS
	int dxt3Offset = 0;
	if ( !R_DDSFileIsDXT3( header, sizeof( header ), &dxt3Offset ) || dxt3Offset != (int)sizeof( header ) ) {
		common->Warning( "Image DDS self-test failed: DX10 BC2 was not left to the DXT3 decoder" );
		return false;
	}

	// legacy A8R8G8B8 (the layout of Quake 4's own uncompressed HUD DDS files)
	memset( header, 0, DDS_HEADER_BYTES );
	R_WriteLittleUInt32( header + 0, R_MakeFourCC( 'D', 'D', 'S', ' ' ) );
	R_WriteLittleUInt32( header + 4, 124 );
	R_WriteLittleUInt32( header + 12, 2 );
	R_WriteLittleUInt32( header + 16, 2 );
	R_WriteLittleUInt32( header + 76, 32 );
	R_WriteLittleUInt32( header + 80, DDS_PIXELFORMAT_RGB | DDS_PIXELFORMAT_ALPHAPIXELS );
	R_WriteLittleUInt32( header + 88, 32 );
	R_WriteLittleUInt32( header + 92, 0x00ff0000 );
	R_WriteLittleUInt32( header + 96, 0x0000ff00 );
	R_WriteLittleUInt32( header + 100, 0x000000ff );
	R_WriteLittleUInt32( header + 104, 0xff000000 );
	if ( !R_ParseDDSFileInfo( header, DDS_HEADER_BYTES, DDS_HEADER_BYTES + 16, info ) ||
		 info.format != DDS_STORED_FORMAT_UNCOMPRESSED || info.bytesPerPixel != 4 ||
		 info.channelOffset[0] != 2 || info.channelOffset[1] != 1 || info.channelOffset[2] != 0 || info.channelOffset[3] != 3 ) {
		common->Warning( "Image DDS self-test failed: A8R8G8B8 metadata was rejected or misread" );
		return false;
	}
	{
		const byte bgra[4] = { 0x10, 0x20, 0x30, 0x40 };
		byte rgba[4];
		R_DecodeUncompressedDDS( bgra, info, rgba, 1, 1 );
		if ( rgba[0] != 0x30 || rgba[1] != 0x20 || rgba[2] != 0x10 || rgba[3] != 0x40 ) {
			common->Warning( "Image DDS self-test failed: A8R8G8B8 texel decoded to %d %d %d %d", rgba[0], rgba[1], rgba[2], rgba[3] );
			return false;
		}
	}
	R_WriteLittleUInt32( header + 92, 0x00ff8000 );
	if ( R_ParseDDSFileInfo( header, DDS_HEADER_BYTES, DDS_HEADER_BYTES + 16, info ) ) {
		common->Warning( "Image DDS self-test failed: a channel mask that is not one byte was accepted" );
		return false;
	}

	// BC4 and BC5: both endpoint modes, and BC5's rebuilt Z
	{
		// mode 1 (e0 > e1): 255 and 0 with indices 0..7 across the first row
		// and index 1 (the second endpoint) everywhere else
		byte bc4[8] = { 255, 0 };
		uint64 indices = 0;
		for ( int texel = 0; texel < 16; texel++ ) {
			indices |= (uint64)( texel < 8 ? texel : 1 ) << ( 3 * texel );
		}
		for ( int i = 0; i < 6; i++ ) {
			bc4[2 + i] = (byte)( indices >> ( 8 * i ) );
		}
		byte bc4Rgba[4 * 4 * 4];
		R_DecodeBC4BC5( bc4, false, bc4Rgba, 4, 4 );
		const int expected[8] = { 255, 0, 219, 182, 146, 109, 73, 36 };
		for ( int texel = 0; texel < 16; texel++ ) {
			const int want = texel < 8 ? expected[texel] : 0;
			const byte *rgba = bc4Rgba + texel * 4;
			if ( rgba[0] != want || rgba[1] != want || rgba[2] != want || rgba[3] != 255 ) {
				common->Warning( "Image DDS self-test failed: BC4 texel %d decoded to %d %d %d %d, wanted %d", texel, rgba[0], rgba[1], rgba[2], rgba[3], want );
				return false;
			}
		}
		// mode 2 (e0 <= e1): indices 6 and 7 are the constants 0 and 255
		byte bc4b[8] = { 10, 60, 0 };
		indices = (uint64)6 | ( (uint64)7 << 3 ) | ( (uint64)2 << 6 );
		for ( int i = 0; i < 6; i++ ) {
			bc4b[2 + i] = (byte)( indices >> ( 8 * i ) );
		}
		R_DecodeBC4BC5( bc4b, false, bc4Rgba, 4, 4 );
		if ( bc4Rgba[0] != 0 || bc4Rgba[4] != 255 || bc4Rgba[8] != 20 || bc4Rgba[12] != 10 ) {
			common->Warning( "Image DDS self-test failed: BC4 six-value block decoded to %d %d %d %d", bc4Rgba[0], bc4Rgba[4], bc4Rgba[8], bc4Rgba[12] );
			return false;
		}
		// BC5 with X and Y both at the 128 midpoint: a flat normal, Z = 255
		byte bc5[16] = { 128, 128, 0, 0, 0, 0, 0, 0, 128, 128, 0, 0, 0, 0, 0, 0 };
		R_DecodeBC4BC5( bc5, true, bc4Rgba, 4, 4 );
		for ( int texel = 0; texel < 16; texel++ ) {
			const byte *rgba = bc4Rgba + texel * 4;
			if ( rgba[0] != 128 || rgba[1] != 128 || rgba[2] != 255 || rgba[3] != 255 ) {
				common->Warning( "Image DDS self-test failed: BC5 flat normal decoded to %d %d %d %d", rgba[0], rgba[1], rgba[2], rgba[3] );
				return false;
			}
		}
		// X at full positive: Z falls to the midpoint
		bc5[0] = bc5[1] = 255;
		R_DecodeBC4BC5( bc5, true, bc4Rgba, 4, 4 );
		if ( bc4Rgba[0] != 255 || bc4Rgba[1] != 128 || bc4Rgba[2] < 127 || bc4Rgba[2] > 129 ) {
			common->Warning( "Image DDS self-test failed: BC5 +X normal decoded to %d %d %d", bc4Rgba[0], bc4Rgba[1], bc4Rgba[2] );
			return false;
		}
	}

	// downsize( <image>, N ) offers its input's DDS files ahead of the retail result
	struct downsizeCase_t {
		const char *imageProgram;
		const char *expectedInput;
	};
	const downsizeCase_t downsizeCases[] = {
		{ "downsize( models/characters/marine/helmet_d, 1)", "models/characters/marine/helmet_d" },
		{ "downsize( textures/a.tga, 0 )", "textures/a.tga" },
		{ "downsize( addnormals( textures/a, heightmap( textures/b, 2)), 1)", NULL },
		{ "downsize( textures/a, 1) extra", NULL },
		{ "textures/a", NULL }
	};
	for ( int i = 0; i < (int)( sizeof( downsizeCases ) / sizeof( downsizeCases[0] ) ); i++ ) {
		idStr input;
		const bool parsed = R_DownsizeProgramInput( downsizeCases[i].imageProgram, input );
		if ( parsed != ( downsizeCases[i].expectedInput != NULL ) ||
			 ( parsed && input.Icmp( downsizeCases[i].expectedInput ) != 0 ) ) {
			common->Warning( "Image DDS self-test failed: downsize input of '%s' read as '%s'",
				downsizeCases[i].imageProgram, parsed ? input.c_str() : "(none)" );
			return false;
		}
	}
	idStrList downsizeCandidates;
	idStrList downsizeOrigins;
	R_BuildDDSCandidates( "downsize( models/characters/marine/helmet_d, 1)", downsizeCandidates, downsizeOrigins );
	const char *expectedDownsizeCandidates[] = {
		"dds/downsize/models/characters/marine/helmet_d 1.dds",
		"dds/models/characters/marine/helmet_d.dds",
		"models/characters/marine/helmet_d.dds",
		"progimg/downsize/models/characters/marine/helmet_d_1.dds"
	};
	const int expectedDownsizeCount = (int)( sizeof( expectedDownsizeCandidates ) / sizeof( expectedDownsizeCandidates[0] ) );
	bool downsizeOrderMatches = downsizeCandidates.Num() == expectedDownsizeCount;
	for ( int i = 0; downsizeOrderMatches && i < expectedDownsizeCount; i++ ) {
		downsizeOrderMatches = downsizeCandidates[i].Icmp( expectedDownsizeCandidates[i] ) == 0;
	}
	if ( !downsizeOrderMatches ) {
		common->Warning( "Image DDS self-test failed: downsize() DDS candidates are out of order (%d found)", downsizeCandidates.Num() );
		for ( int i = 0; i < downsizeCandidates.Num(); i++ ) {
			common->Printf( "   %s [%s]\n", downsizeCandidates[i].c_str(), downsizeOrigins[i].c_str() );
		}
		return false;
	}

	// the inputs whose origin decides whether Quake 4's progimg/ result stands
	idStrList programInputs;
	R_ImageProgramInputNames( "downsize( addnormals( models/a/b_local, heightmap( models/a/b_h.tga, 2 ) ), 1)", programInputs );
	if ( programInputs.Num() != 2 || programInputs[0].Icmp( "models/a/b_local" ) != 0 || programInputs[1].Icmp( "models/a/b_h.tga" ) != 0 ) {
		common->Warning( "Image DDS self-test failed: image program inputs read as %d names", programInputs.Num() );
		return false;
	}
	R_ImageProgramInputNames( "heightmap( _flat, 4 )", programInputs );
	if ( programInputs.Num() != 0 ) {
		common->Warning( "Image DDS self-test failed: '_flat' was read as an image program input" );
		return false;
	}

	// one mode 6 BC7 block, decoded the way LoadDDS decodes a level: both
	// endpoints 0x80 0x40 0x20 0xfe, every index 0
	byte bc7Block[16];
	memset( bc7Block, 0, sizeof( bc7Block ) );
	{
		const byte color[4] = { 0x80, 0x40, 0x20, 0xfe };
		uint64 low = 0x40;
		int position = 7;
		for ( int channel = 0; channel < 4; channel++ ) {
			low |= (uint64)( color[channel] >> 1 ) << position;
			low |= (uint64)( color[channel] >> 1 ) << ( position + 7 );
			position += 14;
		}
		for ( int i = 0; i < 8; i++ ) {
			bc7Block[i] = (byte)( low >> ( i * 8 ) );
		}
	}
	byte bc7Rgba[4 * 4 * 4];
	BC7_DecodeImage( bc7Block, bc7Rgba, 4, 4 );
	for ( int texel = 0; texel < 16; texel++ ) {
		const byte *rgba = bc7Rgba + texel * 4;
		if ( rgba[0] != 0x80 || rgba[1] != 0x40 || rgba[2] != 0x20 || rgba[3] != 0xfe ) {
			common->Warning( "Image DDS self-test failed: BC7 texel %d decoded to %d %d %d %d", texel, rgba[0], rgba[1], rgba[2], rgba[3] );
			return false;
		}
	}

	common->Printf( "Image DDS self-test passed\n" );
	return true;
}

/*
=============
LoadDDS

Decodes DXT1, DXT3, DXT5, RXGB, BC7, BC4, BC5 and uncompressed 24/32-bit files
on the CPU, with a legacy or a DX10 header. BC4, BC5 and uncompressed data only
ever come through here: no renderer format stands for them. DXT3 is only ever read
here, never uploaded as it is: Quake 4 ships a DXT3 .dds beside the .tga of
each font atlas, and those atlases must keep coming from the .tga (see
idImage::DeriveOpts). A DXT3 file is used only when a material names it outright
or nothing else by its name exists, as with the Awakening expansion's three.

BC7 comes through here when it feeds an image program, such as a texture pack's
replacement for an addnormals() or heightmap() input, or when the renderer has
no BPTC support. Only the top level is read; the destination image builds its
own mips, as it does for every other source.
=============
*/
static void LoadDDS( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool decodeRXGBNormalMap ) {
	int fileSize;
	byte *buffer;

	if ( !pic ) {
		fileSystem->ReadFileContentStamp( name, NULL, timestamp );
		return;	// just getting timestamp
	}

	*pic = NULL;

	fileSize = fileSystem->ReadFileContentStamp( name, (void **)&buffer, timestamp );
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

	if ( ddsWidth == 0 || ddsHeight == 0 || ( pixelFormatFlags & ( DDS_PIXELFORMAT_FOURCC | DDS_PIXELFORMAT_RGB ) ) == 0 ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	int blockSize = 0;
	int dataOffset = DDS_HEADER_BYTES;
	enum ddsCompression_t {
		DDS_COMPRESSION_DXT1,
		DDS_COMPRESSION_DXT3,
		DDS_COMPRESSION_DXT5,
		DDS_COMPRESSION_BC7,
		DDS_COMPRESSION_BC4,
		DDS_COMPRESSION_BC5,
		DDS_COMPRESSION_BC6H_UF16,
		DDS_COMPRESSION_BC6H_SF16,
		DDS_COMPRESSION_NONE
	} compression;
	bool storedRXGBNormalMap = false;

	// A file whose whole layout parses is read by what the parser found: that
	// covers BC7, BC4, BC5, uncompressed data and BC1/BC3 behind a DX10 header.
	// The FourCC switch below stays for files whose header promises more mip
	// levels than they hold, which have always decoded from their top level.
	ddsFileInfo_t parsedInfo;
	const bool parsed = R_ParseDDSFileInfo( buffer, fileSize, fileSize, parsedInfo );
	int dxt3Offset = DDS_HEADER_BYTES;
	if ( parsed && parsedInfo.format != DDS_STORED_FORMAT_DXT1 && parsedInfo.format != DDS_STORED_FORMAT_DXT5 ) {
		dataOffset = parsedInfo.dataOffset;
		switch ( parsedInfo.format ) {
			case DDS_STORED_FORMAT_BC7:	compression = DDS_COMPRESSION_BC7; blockSize = 16; break;
			case DDS_STORED_FORMAT_BC4:	compression = DDS_COMPRESSION_BC4; blockSize = 8; break;
			case DDS_STORED_FORMAT_BC5:	compression = DDS_COMPRESSION_BC5; blockSize = 16; break;
			// HDR clamped to [0, 1]: this path feeds 8-bit consumers only
			case DDS_STORED_FORMAT_BC6H_UF16:	compression = DDS_COMPRESSION_BC6H_UF16; blockSize = 16; break;
			case DDS_STORED_FORMAT_BC6H_SF16:	compression = DDS_COMPRESSION_BC6H_SF16; blockSize = 16; break;
			case DDS_STORED_FORMAT_RXGB:
				compression = DDS_COMPRESSION_DXT5;
				blockSize = 16;
				storedRXGBNormalMap = true;
				break;
			default:
				compression = DDS_COMPRESSION_NONE;
				blockSize = 0;
				break;
		}
	} else if ( parsed ) {
		// DXT1 or DXT5, possibly behind a DX10 header
		dataOffset = parsedInfo.dataOffset;
		blockSize = parsedInfo.format == DDS_STORED_FORMAT_DXT1 ? 8 : 16;
		compression = parsedInfo.format == DDS_STORED_FORMAT_DXT1 ? DDS_COMPRESSION_DXT1 : DDS_COMPRESSION_DXT5;
	} else if ( R_DDSFileIsDXT3( buffer, fileSize, &dxt3Offset ) ) {
		blockSize = 16;
		compression = DDS_COMPRESSION_DXT3;
		dataOffset = dxt3Offset;
	} else if ( fourCC == R_MakeFourCC( 'D', 'X', 'T', '1' ) ) {
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

	// uncompressed levels have no blocks to pad to; the parser already checked
	// that the whole chain is present
	const bool blockCompressed = compression != DDS_COMPRESSION_NONE;
	const int blocksWide = Max( 1, (int)( ( ddsWidth + 3 ) >> 2 ) );
	const int blocksHigh = Max( 1, (int)( ( ddsHeight + 3 ) >> 2 ) );
	const int64 levelSize = blockCompressed ? (int64)blocksWide * blocksHigh * blockSize
		: (int64)ddsWidth * ddsHeight * parsedInfo.bytesPerPixel;

	if ( levelSize <= 0 || (int64)fileSize < ( dataOffset + levelSize ) ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	const int realWidth = (int)ddsWidth;
	const int realHeight = (int)ddsHeight;
	const int paddedWidth = blockCompressed ? blocksWide * 4 : realWidth;
	const int paddedHeight = blockCompressed ? blocksHigh * 4 : realHeight;
	const int64 realBytes = (int64)realWidth * realHeight * 4;
	const int64 paddedBytes = (int64)paddedWidth * paddedHeight * 4;
	if ( realBytes <= 0 || realBytes > 0x7FFFFFFF || paddedBytes <= 0 || paddedBytes > 0x7FFFFFFF ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	byte *decodedRgba = (byte *)R_StaticAlloc( (int)paddedBytes );
	idDxtDecoder decoder;
	if ( compression == DDS_COMPRESSION_NONE ) {
		R_DecodeUncompressedDDS( buffer + dataOffset, parsedInfo, decodedRgba, realWidth, realHeight );
	} else if ( compression == DDS_COMPRESSION_BC4 || compression == DDS_COMPRESSION_BC5 ) {
		R_DecodeBC4BC5( buffer + dataOffset, compression == DDS_COMPRESSION_BC5, decodedRgba, paddedWidth, paddedHeight );
	} else if ( compression == DDS_COMPRESSION_BC7 ) {
		BC7_DecodeImage( buffer + dataOffset, decodedRgba, paddedWidth, paddedHeight );
	} else if ( compression == DDS_COMPRESSION_BC6H_UF16 || compression == DDS_COMPRESSION_BC6H_SF16 ) {
		BC6H_DecodeImageRGBA8( buffer + dataOffset, compression == DDS_COMPRESSION_BC6H_SF16, decodedRgba, paddedWidth, paddedHeight );
	} else if ( compression == DDS_COMPRESSION_DXT1 ) {
		decoder.DecompressImageDXT1( buffer + dataOffset, decodedRgba, paddedWidth, paddedHeight );
	} else if ( compression == DDS_COMPRESSION_DXT3 ) {
		decoder.DecompressImageDXT5( buffer + dataOffset, decodedRgba, paddedWidth, paddedHeight );
		R_WriteDXT3Alpha( buffer + dataOffset, decodedRgba, paddedWidth, paddedHeight );
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

Automatically attempts to load .jpg / .png / .dds files if .tga files fail to
load, and the other source formats when a named .jpg or .png is missing.

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

	// Image programs recurse through this CPU loader, bypassing the renderer's
	// intrinsic image cache. Supply the same uncompressed source values here so
	// heightmap(_white), smoothnormals(_flat), etc. have real inputs rather than
	// silently becoming the missing-image checker. Usage-specific swizzling is
	// still performed by the destination image after the program is evaluated.
	const bool white = name.Icmp( "_white" ) == 0;
	const bool black = name.Icmp( "_black" ) == 0;
	const bool flat = name.Icmp( "_flat" ) == 0;
	if ( white || black || flat ) {
		if ( width ) { *width = 16; }
		if ( height ) { *height = 16; }
		if ( timestamp ) { *timestamp = 0; }
		if ( pic ) {
			*pic = static_cast<byte *>( R_StaticAlloc( 16 * 16 * 4 ) );
			for ( int i = 0; i < 16 * 16; ++i ) {
				(*pic)[i * 4] = (*pic)[i * 4 + 1] = white ? 255 : ( flat ? 128 : 0 );
				(*pic)[i * 4 + 2] = black ? 0 : 255;
				(*pic)[i * 4 + 3] = 255;
			}
		}
		return;
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
			name.DefaultFileExtension( ".png" );
			LoadPNG( name.c_str(), pic, width, height, timestamp );
		}
		if ( ( pic && *pic == 0 ) || ( timestamp && *timestamp == -1 ) ) { //-V595
			name.StripFileExtension();
			name.DefaultFileExtension( ".dds" );
			LoadDDS( name.c_str(), pic, width, height, timestamp, decodeRXGBNormalMap );
		}
	} else if ( ext == "jpg" ) {
		LoadJPG( name.c_str(), pic, width, height, timestamp );
		if ( ( pic && *pic == 0 ) || ( timestamp && *timestamp == -1 ) ) { //-V595
			// the mirror of the .tga-to-.jpg fallback: the Awakening expansion's
			// Valkaryne names its limb textures .jpg and ships them as .tga
			name.StripFileExtension();
			name.DefaultFileExtension( ".tga" );
			LoadTGA( name.c_str(), pic, width, height, timestamp );
		}
		if ( ( pic && *pic == 0 ) || ( timestamp && *timestamp == -1 ) ) { //-V595
			name.StripFileExtension();
			name.DefaultFileExtension( ".dds" );
			LoadDDS( name.c_str(), pic, width, height, timestamp, decodeRXGBNormalMap );
		}
	} else if ( ext == "png" ) {
		LoadPNG( name.c_str(), pic, width, height, timestamp );
		if ( ( pic && *pic == 0 ) || ( timestamp && *timestamp == -1 ) ) { //-V595
			name.StripFileExtension();
			name.DefaultFileExtension( ".tga" );
			LoadTGA( name.c_str(), pic, width, height, timestamp );
		}
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
	const char	*cameraSides[6] =  { "_forward.tga", "_back.tga", "_left.tga", "_right.tga", 
		"_up.tga", "_down.tga" };
	const char	*axisSides[6] =  { "_px.tga", "_nx.tga", "_py.tga", "_ny.tga", 
		"_pz.tga", "_nz.tga" };
	const char	**sides;
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
			*timestamp = FS_CombineContentStamps( *timestamp, thisTime );
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
