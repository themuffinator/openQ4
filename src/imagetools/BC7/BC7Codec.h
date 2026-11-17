// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef __BC7CODEC_H__
#define __BC7CODEC_H__

/*
================================================================================================
BC7 decoding on the CPU.

A BC7 texture normally goes to the GPU as it is. It has to be decoded here when
the pixels feed an image program (addnormals, heightmap and the rest run on the
CPU) or when the active renderer has no BPTC support, which is the same thing
the DXT decoder already does for DXT data.

The decoder follows the BPTC format as Direct3D 11 and
ARB_texture_compression_bptc define it: all eight modes, the 64 two- and
three-subset partitions, rotation and index selection. A block whose first byte
is zero names no mode and decodes to transparent black, as Direct3D specifies.
================================================================================================
*/

// One 16-byte block to 4x4 RGBA8 texels. outRgba points at the top-left texel and
// rowPitchBytes is the distance between rows of the destination.
void	BC7_DecodeBlock( const byte *block, byte *outRgba, int rowPitchBytes );

// A whole level. width and height are the padded size: multiples of 4, one block
// per 4x4 texels, row by row. Writes width * height * 4 bytes.
void	BC7_DecodeImage( const byte *blocks, byte *outRgba, int width, int height );

#endif // !__BC7CODEC_H__
