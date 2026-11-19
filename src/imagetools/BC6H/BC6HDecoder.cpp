// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../../idlib/precompiled.h"
#include "BC6HCodec.h"
#include "BC6HLayout.h"

/*
================
BC6H_HalfToFloat
================
*/
float BC6H_HalfToFloat( unsigned short half ) {
	const unsigned int sign = ( half & 0x8000u ) << 16;
	const unsigned int exponent = ( half >> 10 ) & 0x1f;
	unsigned int mantissa = half & 0x3ffu;
	unsigned int bits;
	if ( exponent == 0 ) {
		if ( mantissa == 0 ) {
			bits = sign;
		} else {
			// subnormal: normalize into a float exponent
			int e = -1;
			do {
				e++;
				mantissa <<= 1;
			} while ( ( mantissa & 0x400u ) == 0 );
			bits = sign | (unsigned int)( 127 - 15 - e ) << 23 | ( mantissa & 0x3ffu ) << 13;
		}
	} else if ( exponent == 31 ) {
		bits = sign | 0x7f800000u | mantissa << 13;
	} else {
		bits = sign | ( exponent + ( 127 - 15 ) ) << 23 | mantissa << 13;
	}
	float value;
	memcpy( &value, &bits, sizeof( value ) );
	return value;
}

/*
================
BC6H_FloatToHalf
================
*/
unsigned short BC6H_FloatToHalf( float value ) {
	unsigned int bits;
	memcpy( &bits, &value, sizeof( bits ) );
	const unsigned short sign = (unsigned short)( ( bits >> 16 ) & 0x8000u );
	const unsigned int exponent = ( bits >> 23 ) & 0xff;
	const unsigned int mantissa = bits & 0x7fffffu;
	if ( exponent == 0xff ) {
		return (unsigned short)( sign | 0x7c00u | ( mantissa != 0 ? 0x200u : 0u ) );
	}
	const int e = (int)exponent - 127 + 15;
	if ( e >= 31 ) {
		return (unsigned short)( sign | 0x7c00u );
	}
	if ( e <= 0 ) {
		if ( e < -10 ) {
			return sign;
		}
		// subnormal: shift the implicit one in, round to nearest even
		const unsigned int full = mantissa | 0x800000u;
		const int shift = 14 - e;
		unsigned int half = full >> shift;
		const unsigned int remainder = full & ( ( 1u << shift ) - 1 );
		const unsigned int halfway = 1u << ( shift - 1 );
		if ( remainder > halfway || ( remainder == halfway && ( half & 1 ) != 0 ) ) {
			half++;
		}
		return (unsigned short)( sign | half );
	}
	unsigned int half = (unsigned int)e << 10 | mantissa >> 13;
	const unsigned int remainder = mantissa & 0x1fffu;
	if ( remainder > 0x1000u || ( remainder == 0x1000u && ( half & 1 ) != 0 ) ) {
		half++;	// may carry into the exponent, up to infinity, which is right
	}
	return (unsigned short)( sign | half );
}

class idBC6HBitReader {
public:
	explicit idBC6HBitReader( const byte *block ) : data( block ), position( 0 ) {}

	int Read( int count ) {
		int value = 0;
		for ( int i = 0; i < count; i++, position++ ) {
			value |= ( ( data[ position >> 3 ] >> ( position & 7 ) ) & 1 ) << i;
		}
		return value;
	}

	void Seek( int bit ) { position = bit; }

private:
	const byte *	data;
	int				position;
};

/*
================
BC6H_DecodeBlock
================
*/
void BC6H_DecodeBlock( const byte *block, bool isSigned, unsigned short *outRgbaHalf, int rowPitchHalves ) {
	const unsigned short one = 0x3c00;
	const int modeIndex = BC6H_ModeIndex( block );
	if ( modeIndex < 0 ) {
		for ( int y = 0; y < 4; y++ ) {
			unsigned short *row = outRgbaHalf + y * rowPitchHalves;
			for ( int x = 0; x < 4; x++ ) {
				row[ x * 4 + 0 ] = 0;
				row[ x * 4 + 1 ] = 0;
				row[ x * 4 + 2 ] = 0;
				row[ x * 4 + 3 ] = one;
			}
		}
		return;
	}

	const bc6hMode_t &mode = bc6hModes[ modeIndex ];
	idBC6HBitReader bits( block );
	bits.Seek( mode.modeBits );

	int fields[12] = { 0 };
	for ( int i = 0; i < mode.numSegments; i++ ) {
		const bc6hSegment_t &segment = mode.segments[i];
		fields[ segment.field ] |= bits.Read( segment.count ) << segment.lsb;
	}

	int endpoints[12];
	BC6H_ResolveEndpoints( mode, isSigned, fields, endpoints );

	int partition = 0;
	int indexBits = 4;
	if ( mode.regions == 2 ) {
		partition = bits.Read( 5 );	// bits 77-81
		indexBits = 3;
	}
	// the index of each region's anchor texel drops its top bit, which is zero
	const int anchor1 = mode.regions == 2 ? bc6hAnchor2[ partition ] : -1;
	const int *weights = indexBits == 3 ? bc6hWeights3 : bc6hWeights4;

	for ( int texel = 0; texel < 16; texel++ ) {
		const int count = ( texel == 0 || texel == anchor1 ) ? indexBits - 1 : indexBits;
		const int weight = weights[ bits.Read( count ) ];
		const int region = mode.regions == 2 ? BC6H_PartitionRegion( partition, texel ) : 0;
		const int *a = endpoints + region * 6;
		const int *b = a + 3;
		unsigned short *out = outRgbaHalf + ( texel >> 2 ) * rowPitchHalves + ( texel & 3 ) * 4;
		for ( int channel = 0; channel < 3; channel++ ) {
			out[ channel ] = BC6H_FinishUnquantize( BC6H_Interpolate( a[ channel ], b[ channel ], weight ), isSigned );
		}
		out[3] = one;
	}
}

/*
================
BC6H_DecodeImage
================
*/
void BC6H_DecodeImage( const byte *blocks, bool isSigned, unsigned short *outRgbaHalf, int width, int height ) {
	const int blocksWide = width / 4;
	const int blocksHigh = height / 4;
	const int rowPitchHalves = width * 4;
	for ( int by = 0; by < blocksHigh; by++ ) {
		for ( int bx = 0; bx < blocksWide; bx++ ) {
			BC6H_DecodeBlock( blocks + ( by * blocksWide + bx ) * 16, isSigned,
				outRgbaHalf + ( by * 4 ) * rowPitchHalves + bx * 16, rowPitchHalves );
		}
	}
}

/*
================
BC6H_DecodeImageRGBA8
================
*/
void BC6H_DecodeImageRGBA8( const byte *blocks, bool isSigned, byte *outRgba, int width, int height ) {
	const int blocksWide = width / 4;
	const int blocksHigh = height / 4;
	unsigned short texels[64];
	for ( int by = 0; by < blocksHigh; by++ ) {
		for ( int bx = 0; bx < blocksWide; bx++ ) {
			BC6H_DecodeBlock( blocks + ( by * blocksWide + bx ) * 16, isSigned, texels, 16 );
			for ( int texel = 0; texel < 16; texel++ ) {
				byte *out = outRgba + ( ( by * 4 + ( texel >> 2 ) ) * width + bx * 4 + ( texel & 3 ) ) * 4;
				for ( int channel = 0; channel < 3; channel++ ) {
					const float value = BC6H_HalfToFloat( texels[ texel * 4 + channel ] );
					// NaN fails both comparisons and lands on 0
					out[ channel ] = value >= 1.0f ? 255 : ( value > 0.0f ? (byte)( value * 255.0f + 0.5f ) : 0 );
				}
				out[3] = 255;
			}
		}
	}
}
