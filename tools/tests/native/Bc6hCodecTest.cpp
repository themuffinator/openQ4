// BC6H codec conformance.
//
// Decoder: seeded random blocks for every mode, the four reserved modes, and
// both the unsigned and signed variants, compared by CRC-32 with what the GPU
// makes of the same blocks (tools/tests/native/Bc6hGpuReference.cpp
// regenerates the table from the same generator, Bc6hBlocks.h).
//
// Encoder: synthetic HDR images at every quality level. Every block must
// decode within a bound of its source, a higher quality level must never do
// worse than a lower one, and blocks that need no compression (one colour,
// values a mode stores exactly) must round-trip without loss.
//
// usage: openq4-bc6h-codec-test
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
typedef unsigned char byte;
#define __PRECOMPILED_H__
#include "../../../src/idlib/hashing/CRC32.cpp"
#include "../../../src/imagetools/BC6H/BC6HDecoder.cpp"
#include "../../../src/imagetools/BC6H/BC6HEncoder.cpp"
#include "Bc6hBlocks.h"

// tools/tests/native/Bc6hGpuReference.cpp; modes 1-14, then the reserved modes
static const uint32_t referenceCrcUnsigned[BC6H_TEST_GROUPS] = {
	0xa04f54cd, 0x65af7f0e, 0x09087413, 0xa0ffe530, 0x94627e00,
	0xebe2476c, 0x01e4ea53, 0x0d2d8756, 0xaecdce74, 0x5f8b4de3,
	0xc197cc52, 0xe05a7a96, 0xb4c967b4, 0x48340f4c, 0x63855a01
};
static const uint32_t referenceCrcSigned[BC6H_TEST_GROUPS] = {
	0x200586c2, 0x9d0000f9, 0x89647481, 0x27115f21, 0xaadab983,
	0x106b85df, 0x68b0e467, 0x81da3fea, 0xebd7ce74, 0x09bc11f6,
	0x987a061b, 0x6c398254, 0x6f9b0d54, 0xef6c1122, 0x63855a01
};

static int failures = 0;

static void Fail( const char *message ) {
	std::printf( "FAIL %s\n", message );
	failures++;
}

static void TestDecoder() {
	for ( int isSigned = 0; isSigned < 2; isSigned++ ) {
		bc6hTestRandom_t rng( BC6H_TEST_SEED + (uint32_t)isSigned );
		for ( int group = 0; group < BC6H_TEST_GROUPS; group++ ) {
			const int count = BC6H_TestGroupBlocks( group );
			std::vector<unsigned short> texels( (size_t)count * 64 );
			for ( int i = 0; i < count; i++ ) {
				byte block[16];
				BC6H_TestMakeBlock( rng, group, i, block );
				BC6H_DecodeBlock( block, isSigned != 0, texels.data() + (size_t)i * 64, 16 );
			}
			const uint32_t crc = CRC32_BlockChecksum( texels.data(), (int)( texels.size() * 2 ) );
			const uint32_t expected = isSigned ? referenceCrcSigned[ group ] : referenceCrcUnsigned[ group ];
			if ( crc != expected ) {
				std::printf( "FAIL %s group %d: CRC 0x%08x, GPU 0x%08x\n", isSigned ? "SF16" : "UF16", group, crc, expected );
				failures++;
			}
		}
	}

	// the image helpers walk blocks the way the per-block call does
	byte blocks[ 4 * 16 ];
	bc6hTestRandom_t rng( 99 );
	for ( int i = 0; i < 4; i++ ) {
		BC6H_TestMakeBlock( rng, 10 + i, i, blocks + i * 16 );
	}
	std::vector<unsigned short> image( 8 * 8 * 4 );
	BC6H_DecodeImage( blocks, false, image.data(), 8, 8 );
	std::vector<byte> rgba8( 8 * 8 * 4 );
	BC6H_DecodeImageRGBA8( blocks, false, rgba8.data(), 8, 8 );
	for ( int i = 0; i < 4; i++ ) {
		unsigned short single[64];
		BC6H_DecodeBlock( blocks + i * 16, false, single, 16 );
		for ( int texel = 0; texel < 16; texel++ ) {
			const int x = ( i & 1 ) * 4 + ( texel & 3 );
			const int y = ( i >> 1 ) * 4 + ( texel >> 2 );
			for ( int c = 0; c < 4; c++ ) {
				if ( image[ ( y * 8 + x ) * 4 + c ] != single[ texel * 4 + c ] ) {
					Fail( "BC6H_DecodeImage disagrees with BC6H_DecodeBlock" );
					return;
				}
			}
			for ( int c = 0; c < 3; c++ ) {
				float v = BC6H_HalfToFloat( single[ texel * 4 + c ] );
				v = v < 0.0f ? 0.0f : ( v > 1.0f ? 1.0f : v );
				const int expected = (int)( v * 255.0f + 0.5f );
				if ( rgba8[ ( y * 8 + x ) * 4 + c ] != expected || rgba8[ ( y * 8 + x ) * 4 + 3 ] != 255 ) {
					Fail( "BC6H_DecodeImageRGBA8 does not clamp and round the decoded halves" );
					return;
				}
			}
		}
	}
}

static void TestHalfConversions() {
	// every finite half survives a trip through float
	for ( int bits = 0; bits < 0x10000; bits++ ) {
		const unsigned short half = (unsigned short)bits;
		if ( ( half & 0x7c00 ) == 0x7c00 && ( half & 0x3ff ) != 0 ) {
			continue;	// NaN payloads need not survive
		}
		if ( BC6H_FloatToHalf( BC6H_HalfToFloat( half ) ) != half ) {
			std::printf( "FAIL half 0x%04x does not round-trip\n", bits );
			failures++;
			return;
		}
	}
	if ( BC6H_FloatToHalf( 65520.0f ) != 0x7c00 || BC6H_FloatToHalf( 65519.0f ) != 0x7bff ||
		 BC6H_FloatToHalf( 1.0f + 1.0f / 4096.0f ) != 0x3c00 || BC6H_FloatToHalf( 1.0f + 3.0f / 2048.0f ) != 0x3c02 ||
		 BC6H_FloatToHalf( 5.960464477539063e-8f ) != 0x0001 || BC6H_FloatToHalf( 2.98e-8f ) != 0x0000 ) {
		Fail( "float to half rounding" );
	}
}

// the signed magnitude a half decodes to, after clamping as the encoder does
static int SignedBits( unsigned short half, bool isSigned ) {
	int m = half & 0x7fff;
	m = m > 0x7bff ? 0x7bff : m;
	if ( half & 0x8000 ) {
		return isSigned ? -m : 0;
	}
	return m;
}

struct encodeStats_t {
	double sumSq;
	int maxAbs;
};

static encodeStats_t EncodeAndMeasure( const std::vector<unsigned short> &source, int width, int height, bool isSigned, bc6hQuality_t quality ) {
	const int blocksWide = ( width + 3 ) / 4;
	const int blocksHigh = ( height + 3 ) / 4;
	std::vector<byte> blocks( (size_t)blocksWide * blocksHigh * 16 );
	BC6H_EncodeImage( source.data(), width, height, isSigned, quality, blocks.data() );
	encodeStats_t stats = { 0.0, 0 };
	for ( int by = 0; by < blocksHigh; by++ ) {
		for ( int bx = 0; bx < blocksWide; bx++ ) {
			unsigned short decoded[64];
			BC6H_DecodeBlock( blocks.data() + ( (size_t)by * blocksWide + bx ) * 16, isSigned, decoded, 16 );
			for ( int texel = 0; texel < 16; texel++ ) {
				const int x = bx * 4 + ( texel & 3 );
				const int y = by * 4 + ( texel >> 2 );
				if ( x >= width || y >= height ) {
					continue;
				}
				if ( decoded[ texel * 4 + 3 ] != 0x3c00 ) {
					Fail( "encoded block does not decode with alpha 1.0" );
				}
				for ( int c = 0; c < 3; c++ ) {
					const int want = SignedBits( source[ ( (size_t)y * width + x ) * 4 + c ], isSigned );
					const unsigned short got = decoded[ texel * 4 + c ];
					const int gotBits = ( got & 0x8000 ) ? -( got & 0x7fff ) : got;
					const int diff = gotBits - want;
					stats.sumSq += (double)diff * diff;
					stats.maxAbs = diff < 0 ? ( -diff > stats.maxAbs ? -diff : stats.maxAbs ) : ( diff > stats.maxAbs ? diff : stats.maxAbs );
				}
			}
		}
	}
	return stats;
}

static unsigned short H( float v ) {
	return BC6H_FloatToHalf( v );
}

static std::vector<unsigned short> MakeImage( int kind, int width, int height, bool isSigned ) {
	std::vector<unsigned short> image( (size_t)width * height * 4 );
	bc6hTestRandom_t rng( 0xC0FFEEu + (uint32_t)kind );
	for ( int y = 0; y < height; y++ ) {
		for ( int x = 0; x < width; x++ ) {
			float rgb[3];
			const float u = (float)x / (float)( width - 1 );
			const float v = (float)y / (float)( height - 1 );
			switch ( kind ) {
				case 0:	// sky: smooth, bright toward a sun, colour shifting with height
				{
					const float dx = u - 0.7f, dy = v - 0.2f;
					const float sun = 400.0f * std::exp( -( dx * dx + dy * dy ) * 200.0f );
					rgb[0] = 0.3f + 0.5f * v + sun;
					rgb[1] = 0.5f + 0.3f * v + sun * 0.9f;
					rgb[2] = 1.2f - 0.4f * v + sun * 0.7f;
					break;
				}
				case 1:	// lit interior: texture detail times a wide light falloff
				{
					const float detail = 0.5f + 0.5f * (float)( ( rng.Next() >> 8 ) & 0xff ) / 255.0f;
					const float light = 0.02f + 60.0f / ( 1.0f + 40.0f * ( ( u - 0.5f ) * ( u - 0.5f ) + ( v - 0.5f ) * ( v - 0.5f ) ) );
					rgb[0] = detail * light * 1.0f;
					rgb[1] = detail * light * 0.8f;
					rgb[2] = detail * light * 0.6f;
					break;
				}
				case 2:	// hard edges between unrelated colours: needs two regions
				{
					const bool side = ( x + y ) % 7 < 3;
					rgb[0] = side ? 8.0f : 0.05f;
					rgb[1] = side ? 0.1f : 2.0f;
					rgb[2] = side ? 0.5f : 30.0f;
					break;
				}
				default:	// white noise over six orders of magnitude, the worst case
				{
					for ( int c = 0; c < 3; c++ ) {
						rgb[c] = std::pow( 10.0f, -3.0f + 6.0f * (float)( rng.Next() & 0xffff ) / 65535.0f );
					}
					break;
				}
			}
			for ( int c = 0; c < 3; c++ ) {
				float value = rgb[c];
				// signed data changes sign over large areas, one channel
				// against the other two, as a signed field would
				if ( isSigned && ( ( x * 2 >= width ) != ( c == 1 ) ) ) {
					value = -value;
				}
				image[ ( (size_t)y * width + x ) * 4 + c ] = H( value );
			}
			image[ ( (size_t)y * width + x ) * 4 + 3 ] = 0x3c00;
		}
	}
	return image;
}

static void TestEncoder() {
	const char *kindNames[4] = { "sky", "interior", "edges", "noise" };
	// RMS error bounds in half bit patterns (one unit is one ulp of a half,
	// 1/1024 of a doubling), about 10% over what the encoder made of these
	// images when it landed. Sky and interior sit at or under the error of an
	// ideal one-region fit with unquantized endpoints; noise only has to stay
	// bounded. [UF16, SF16][kind]
	const double rmsBound[2][4] = {
		{ 37.0, 33.0, 7.0, 3350.0 },
		{ 46.0, 68.0, 74.0, 3400.0 }
	};
	const char *qualityNames[3] = { "fast", "normal", "high" };
	for ( int isSigned = 0; isSigned < 2; isSigned++ ) {
		for ( int kind = 0; kind < 4; kind++ ) {
			// an odd size exercises the edge-block replication
			const int width = 62, height = 30;
			const std::vector<unsigned short> image = MakeImage( kind, width, height, isSigned != 0 );
			double previous = -1.0;
			for ( int quality = 0; quality < 3; quality++ ) {
				const encodeStats_t stats = EncodeAndMeasure( image, width, height, isSigned != 0, (bc6hQuality_t)quality );
				const double rms = std::sqrt( stats.sumSq / ( width * height * 3 ) );
				std::printf( "%s %-8s %-6s rms %8.2f max %5d\n", isSigned ? "SF16" : "UF16", kindNames[ kind ], qualityNames[ quality ], rms, stats.maxAbs );
				// the bounds are for the default level; fast may miss two-region content
				if ( quality >= BC6H_QUALITY_NORMAL && rms > rmsBound[ isSigned ][ kind ] ) {
					std::printf( "FAIL %s %s %s: rms %.2f over %.2f\n", isSigned ? "SF16" : "UF16", kindNames[ kind ], qualityNames[ quality ], rms, rmsBound[ isSigned ][ kind ] );
					failures++;
				}
				if ( previous >= 0.0 && stats.sumSq > previous ) {
					std::printf( "FAIL %s %s: %s is worse than the level below it\n", isSigned ? "SF16" : "UF16", kindNames[ kind ], qualityNames[ quality ] );
					failures++;
				}
				previous = stats.sumSq;
			}
		}
	}

	// exact cases: a constant block round-trips to within the endpoint
	// precision, and blocks of infinities or NaNs clamp to the largest finite
	// half of their sign, which mode 11 stores exactly
	for ( int isSigned = 0; isSigned < 2; isSigned++ ) {
		const unsigned short constant[3] = { H( 3.25f ), H( 0.001f ), H( isSigned ? -17.0f : 17.0f ) };
		std::vector<unsigned short> image( 16 * 4 );
		for ( int texel = 0; texel < 16; texel++ ) {
			for ( int c = 0; c < 3; c++ ) {
				image[ texel * 4 + c ] = constant[c];
			}
			image[ texel * 4 + 3 ] = 0x3c00;
		}
		if ( EncodeAndMeasure( image, 4, 4, isSigned != 0, BC6H_QUALITY_FAST ).maxAbs > 1 ) {
			Fail( "a constant block does not round-trip" );
		}
		const unsigned short specials[3] = { 0x7c00, 0x7e00, (unsigned short)( isSigned ? 0xfc00 : 0x7c00 ) };
		for ( int s = 0; s < 3; s++ ) {
			for ( int texel = 0; texel < 16; texel++ ) {
				for ( int c = 0; c < 3; c++ ) {
					image[ texel * 4 + c ] = specials[s];
				}
			}
			if ( EncodeAndMeasure( image, 4, 4, isSigned != 0, BC6H_QUALITY_NORMAL ).maxAbs != 0 ) {
				std::printf( "FAIL %s: 0x%04x does not encode as the largest finite half\n", isSigned ? "SF16" : "UF16", specials[s] );
				failures++;
			}
		}
	}
	// negative input to UF16 encodes as zero
	std::vector<unsigned short> negative( 16 * 4, H( -4.0f ) );
	for ( int texel = 0; texel < 16; texel++ ) {
		negative[ texel * 4 + 3 ] = 0x3c00;
	}
	if ( EncodeAndMeasure( negative, 4, 4, false, BC6H_QUALITY_NORMAL ).maxAbs != 0 ) {
		Fail( "negative UF16 input does not encode as zero" );
	}
}

int main() {
	TestHalfConversions();
	TestDecoder();
	TestEncoder();
	if ( failures == 0 ) {
		std::printf( "PASS bc6h codec\n" );
		return 0;
	}
	return 1;
}
