// BC7 decoder conformance: seeded random blocks for every mode, compared by
// CRC-32 with what two independent decoders (texture2ddecoder and Pillow) make
// of the same blocks. tools/tests/bc7_reference.py regenerates the table and
// uses the same generator, bit for bit.
//
// usage: openq4-bc7-codec-test
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
typedef unsigned char byte;
#define __PRECOMPILED_H__
#include "../../../src/idlib/hashing/CRC32.cpp"
#include "../../../src/imagetools/BC7/BC7Decoder.cpp"

static const int BLOCKS_PER_MODE = 4096;
static const int RESERVED_BLOCKS = 16;

// python tools/tests/bc7_reference.py; modes 0-7, then blocks with no mode bit
static const uint32_t referenceCrc[9] = {
	0x1b9d73b9, 0x37958695, 0x48b08d7d, 0xd5a853fb, 0xc7806938, 0xf516f59d, 0xf2093d22, 0xc536d96f, 0xefb5af2e
};

static uint32_t rngState = 0x2468ACE1u;
static uint32_t NextRandom() {
	rngState ^= rngState << 13;
	rngState ^= rngState >> 17;
	rngState ^= rngState << 5;
	return rngState;
}

int main() {
	int failures = 0;

	for ( int mode = 0; mode < 9; mode++ ) {
		const int count = mode < 8 ? BLOCKS_PER_MODE : RESERVED_BLOCKS;
		std::vector<byte> rgba( (size_t)count * 64 );
		for ( int i = 0; i < count; i++ ) {
			byte block[16];
			for ( int word = 0; word < 4; word++ ) {
				const uint32_t value = NextRandom();
				block[ word * 4 + 0 ] = (byte)( value & 0xff );
				block[ word * 4 + 1 ] = (byte)( ( value >> 8 ) & 0xff );
				block[ word * 4 + 2 ] = (byte)( ( value >> 16 ) & 0xff );
				block[ word * 4 + 3 ] = (byte)( ( value >> 24 ) & 0xff );
			}
			if ( mode < 8 ) {
				block[0] = (byte)( ( block[0] & ~( ( 1 << ( mode + 1 ) ) - 1 ) ) | ( 1 << mode ) );
			} else {
				block[0] = 0;
			}
			BC7_DecodeBlock( block, rgba.data() + (size_t)i * 64, 16 );
		}
		const uint32_t crc = CRC32_BlockChecksum( rgba.data(), (int)rgba.size() );
		if ( crc != referenceCrc[ mode ] ) {
			std::printf( "FAIL mode %d: CRC 0x%08x, reference 0x%08x\n", mode, crc, referenceCrc[ mode ] );
			failures++;
		}
	}

	// BC7_DecodeImage lays blocks out row by row: an 8x4 image of two mode 6
	// blocks, each a single colour (both endpoints equal, every index 0)
	byte image[2][16];
	memset( image, 0, sizeof( image ) );
	const byte colors[2][4] = { { 0xfe, 0x00, 0x80, 0xfe }, { 0x10, 0xfe, 0x20, 0x40 } };
	for ( int b = 0; b < 2; b++ ) {
		// mode 6: 7 mode bits, then R0 R1 G0 G1 B0 B1 A0 A1 at 7 bits, two p-bits
		uint64_t low = 0x40;
		int position = 7;
		for ( int channel = 0; channel < 4; channel++ ) {
			const uint64_t value = colors[b][channel] >> 1;
			low |= value << position;
			position += 7;
			low |= value << position;
			position += 7;
		}
		for ( int i = 0; i < 8; i++ ) {
			image[b][i] = (byte)( low >> ( i * 8 ) );
		}
	}
	byte decoded[8 * 4 * 4];
	BC7_DecodeImage( &image[0][0], decoded, 8, 4 );
	for ( int y = 0; y < 4; y++ ) {
		for ( int x = 0; x < 8; x++ ) {
			const byte *texel = decoded + ( y * 8 + x ) * 4;
			const byte *expected = colors[ x >> 2 ];
			if ( memcmp( texel, expected, 4 ) != 0 ) {
				std::printf( "FAIL image texel %d,%d: %d %d %d %d\n", x, y, texel[0], texel[1], texel[2], texel[3] );
				failures++;
			}
		}
	}

	std::printf( failures == 0 ? "BC7 codec test passed\n" : "BC7 codec test failed\n" );
	return failures == 0 ? 0 : 1;
}
