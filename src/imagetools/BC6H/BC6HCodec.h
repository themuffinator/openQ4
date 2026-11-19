// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef __BC6HCODEC_H__
#define __BC6HCODEC_H__

/*
================================================================================================
BC6H on the CPU: decoding and encoding.

BC6H (BPTC float) stores three-channel half-float colour in 16-byte 4x4 blocks,
one eighth of the size of the RGBA16F data it replaces. A BC6H texture
normally goes to the GPU as it is. It is decoded here when the active renderer
cannot sample BPTC, when the pixels feed a CPU image program, and by the tests
that pin the format down.

The decoder follows BC6H as Direct3D 11 and ARB_texture_compression_bptc
define it: all fourteen modes, the 32 two-region partitions, the transformed
(delta) endpoint modes and both the unsigned (UF16) and signed (SF16) variants.
A block naming one of the four reserved modes decodes to black, as Direct3D
specifies. Alpha is not stored and always decodes to 1.0.

Texel values are half-float bit patterns: IEEE binary16, sign-magnitude.
================================================================================================
*/

// One 16-byte block to 4x4 RGBA half texels. outRgbaHalf points at the
// top-left texel and rowPitchHalves is the distance between rows of the
// destination, in halves (4 per texel).
void	BC6H_DecodeBlock( const byte *block, bool isSigned, unsigned short *outRgbaHalf, int rowPitchHalves );

// A whole level. width and height are the padded size: multiples of 4, one
// block per 4x4 texels, row by row. Writes width * height * 4 halves.
void	BC6H_DecodeImage( const byte *blocks, bool isSigned, unsigned short *outRgbaHalf, int width, int height );

// The same, to RGBA8 for consumers that only take 8-bit colour (the CPU image
// programs, a renderer without float textures). Each channel is clamped to
// [0, 1] and rounded; alpha is 255. HDR range above 1.0 is lost.
void	BC6H_DecodeImageRGBA8( const byte *blocks, bool isSigned, byte *outRgba, int width, int height );

enum bc6hQuality_t {
	BC6H_QUALITY_FAST,		// one-region modes only
	BC6H_QUALITY_NORMAL,	// plus the four most promising two-region partitions
	BC6H_QUALITY_HIGH		// plus every two-region partition, refined twice
};

// One 4x4 block of RGBA half texels (alpha ignored) to 16 bytes. Every
// candidate is decoded with BC6H_DecodeBlock and the smallest error wins, so
// whatever this writes decodes exactly as it was scored. For the unsigned
// variant negative values encode as 0; for both, infinities and NaNs encode as
// the largest finite value of their sign.
void	BC6H_EncodeBlock( const unsigned short *rgbaHalf, int rowPitchHalves, bool isSigned, bc6hQuality_t quality, byte *outBlock );

// A whole level of any size; edge blocks repeat the last row and column.
// Writes ( ( width + 3 ) / 4 ) * ( ( height + 3 ) / 4 ) * 16 bytes.
void	BC6H_EncodeImage( const unsigned short *rgbaHalf, int width, int height, bool isSigned, bc6hQuality_t quality, byte *outBlocks );

// binary16 conversions, round to nearest even
float			BC6H_HalfToFloat( unsigned short half );
unsigned short	BC6H_FloatToHalf( float value );

#endif // !__BC6HCODEC_H__
