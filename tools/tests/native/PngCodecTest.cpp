// PNG/zlib codec conformance: real-zlib fixtures decoded by openQ4, openQ4's
// deflate and PNG writer round-tripped and handed to tools/tests/png_codec.py
// for an independent decode, and damaged input that must fail cleanly.
//
// usage: openq4-png-codec-test <output dir>      (normally run by png_codec.py --run)
//        openq4-png-codec-test --bench [file.png]
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
typedef unsigned char byte;
#define __PRECOMPILED_H__
#include "../../../src/idlib/hashing/CRC32.cpp"
#include "../../../src/imagetools/PNG/Inflate.cpp"
#include "../../../src/imagetools/PNG/Deflate.cpp"
#include "../../../src/imagetools/PNG/PNGFilters.cpp"
#include "../../../src/imagetools/PNG/PNGDecoder.cpp"
#include "../../../src/imagetools/PNG/PNGEncoder.cpp"
#include "PngFixtures.inc"

static int failures = 0;

#define CHECK( condition, ... ) \
	do { \
		if ( !( condition ) ) { \
			std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); \
			std::printf( __VA_ARGS__ ); \
			std::printf( "\n" ); \
			failures++; \
		} \
	} while ( 0 )

static uint32_t rngState = 0x2468ACE1u;
static uint32_t NextRandom() {
	rngState ^= rngState << 13;
	rngState ^= rngState >> 17;
	rngState ^= rngState << 5;
	return rngState;
}

static bool WriteBinary( const std::string &path, const void *data, size_t size ) {
	FILE *f = std::fopen( path.c_str(), "wb" );
	if ( f == NULL ) {
		return false;
	}
	const bool ok = size == 0 || std::fwrite( data, 1, size, f ) == size;
	return std::fclose( f ) == 0 && ok;
}

static std::vector<uint8_t> ReadBinary( const char *path ) {
	std::vector<uint8_t> data;
	FILE *f = std::fopen( path, "rb" );
	if ( f == NULL ) {
		return data;
	}
	std::fseek( f, 0, SEEK_END );
	const long size = std::ftell( f );
	std::fseek( f, 0, SEEK_SET );
	if ( size > 0 ) {
		data.resize( (size_t)size );
		if ( std::fread( data.data(), 1, data.size(), f ) != data.size() ) {
			data.clear();
		}
	}
	std::fclose( f );
	return data;
}

static void TestFixtures() {
	int decoded = 0;
	for ( const pngFixture_t &fixture : pngFixtures ) {
		pngInfo_t info;
		const pngResult_t infoResult = PNG_ReadInfo( fixture.png, fixture.pngSize, info );
		std::vector<uint8_t> rgba( (size_t)fixture.width * fixture.height * 4 + 64, 0xCD );
		const pngResult_t result = PNG_DecodeRGBA8( fixture.png, fixture.pngSize, rgba.data(), rgba.size(), NULL );
		CHECK( result == fixture.result, "%s: result %d (%s), expected %d", fixture.label, result, PNG_ResultString( result ), fixture.result );
		if ( fixture.result != PNG_OK || result != PNG_OK ) {
			continue;
		}
		CHECK( infoResult == PNG_OK && info.width == fixture.width && info.height == fixture.height,
			   "%s: header %ux%u", fixture.label, info.width, info.height );
		CHECK( std::memcmp( rgba.data(), fixture.rgba, fixture.rgbaSize ) == 0, "%s: pixels differ", fixture.label );
		for ( size_t i = fixture.rgbaSize; i < rgba.size(); i++ ) {
			if ( rgba[i] != 0xCD ) {
				CHECK( false, "%s: wrote past the image", fixture.label );
				break;
			}
		}
		decoded++;

		// CRC skipping changes nothing for an intact file
		pngDecodeOptions_t options = {};
		options.skipCRC = true;
		std::vector<uint8_t> again( fixture.rgbaSize );
		CHECK( PNG_DecodeRGBA8( fixture.png, fixture.pngSize, again.data(), again.size(), &options ) == PNG_OK &&
			   std::memcmp( again.data(), fixture.rgba, fixture.rgbaSize ) == 0, "%s: skipCRC decode differs", fixture.label );
		options.skipCRC = false;
		options.maxPixels = (uint64_t)fixture.width * fixture.height - 1;
		if ( options.maxPixels > 0 ) {
			CHECK( PNG_DecodeRGBA8( fixture.png, fixture.pngSize, again.data(), again.size(), &options ) == PNG_ERR_TOO_LARGE,
				   "%s: maxPixels ignored", fixture.label );
		}
	}
	std::printf( "decoded %d real-zlib PNG fixtures\n", decoded );
}

static void TestInflateFixtures() {
	std::vector<uint8_t> out( sizeof( zlibCorpus ) );
	for ( const zlibFixture_t &fixture : zlibFixtures ) {
		std::memset( out.data(), 0, out.size() );
		const zlibResult_t result = Zlib_Inflate( fixture.data, fixture.size, out.data(), out.size(), true );
		CHECK( result == ZLIB_OK && std::memcmp( out.data(), zlibCorpus, out.size() ) == 0,
			   "inflate fixture (%zu bytes): %s", fixture.size, Zlib_ResultString( result ) );
		// every truncation must fail without reading or writing out of bounds
		for ( size_t cut = 0; cut + 4 < fixture.size; cut += 1 + fixture.size / 97 ) {
			std::vector<uint8_t> head( fixture.data, fixture.data + cut );
			const zlibResult_t partial = Zlib_Inflate( head.data(), head.size(), out.data(), out.size(), true );
			CHECK( partial != ZLIB_OK, "inflate accepted a stream cut at %zu", cut );
		}
		// a smaller destination keeps the prefix and reports the excess
		std::vector<uint8_t> small( 1000 );
		size_t written = 0;
		const zlibResult_t excess = Zlib_InflateRaw( fixture.data + 2, fixture.size - 2, small.data(), small.size(), &written, NULL );
		CHECK( excess == ZLIB_OK_EXCESS && written == small.size() && std::memcmp( small.data(), zlibCorpus, small.size() ) == 0,
			   "short destination: %s, %zu bytes", Zlib_ResultString( excess ), written );
	}
	std::printf( "inflated %zu real-zlib streams\n", sizeof( zlibFixtures ) / sizeof( zlibFixtures[0] ) );
}

// random bit flips: any result is fine as long as nothing crashes or overruns
static void TestDamage() {
	for ( const pngFixture_t &fixture : pngFixtures ) {
		if ( fixture.result != PNG_OK ) {
			continue;
		}
		std::vector<uint8_t> file( fixture.png, fixture.png + fixture.pngSize );
		std::vector<uint8_t> rgba( fixture.rgbaSize );
		for ( int trial = 0; trial < 24; trial++ ) {
			std::vector<uint8_t> broken = file;
			const int flips = 1 + (int)( NextRandom() % 4 );
			for ( int f = 0; f < flips; f++ ) {
				broken[8 + NextRandom() % ( broken.size() - 8 )] ^= (uint8_t)( 1u << ( NextRandom() % 8 ) );
			}
			pngDecodeOptions_t options = {};
			options.skipCRC = ( trial & 1 ) != 0;
			pngInfo_t info;
			if ( PNG_ReadInfo( broken.data(), broken.size(), info ) == PNG_OK &&
				 (uint64_t)info.width * info.height * 4 <= rgba.size() ) {
				PNG_DecodeRGBA8( broken.data(), broken.size(), rgba.data(), rgba.size(), &options );
			}
			PNG_DecodeRGBA8( broken.data(), broken.size() / 2, rgba.data(), rgba.size(), &options );
		}
	}
	// random garbage behind a zlib header
	std::vector<uint8_t> garbage( 4096 ), out( 1 << 16 );
	for ( int trial = 0; trial < 2000; trial++ ) {
		for ( uint8_t &b : garbage ) {
			b = (uint8_t)NextRandom();
		}
		garbage[0] = 0x78;
		garbage[1] = 0x9C;
		Zlib_Inflate( garbage.data(), 2 + NextRandom() % ( garbage.size() - 2 ), out.data(), out.size(), true );
	}
	std::printf( "damaged input handled\n" );
}

static std::vector<uint8_t> MakeCorpus( int kind, size_t size ) {
	std::vector<uint8_t> data( size );
	for ( size_t i = 0; i < size; i++ ) {
		switch ( kind ) {
		case 0: data[i] = (uint8_t)NextRandom(); break;									// incompressible
		case 1: data[i] = 0; break;														// one long run
		case 2: data[i] = (uint8_t)( "openQ4 deflate corpus "[i % 22] ); break;			// short period
		case 3: data[i] = (uint8_t)( ( i / 97 ) * 31 + ( NextRandom() % 3 ) ); break;	// noisy ramps
		default: data[i] = (uint8_t)( ( i % 7 ) == 0 ? NextRandom() : data[i ? i - 1 : 0] ); break;
		}
	}
	return data;
}

static void TestDeflate( const std::string &dir ) {
	static const size_t sizes[] = { 0, 1, 2, 3, 4, 257, 258, 259, 32767, 32768, 32769, 65535, 65536, 200000, 1200000 };
	int streams = 0;
	for ( int kind = 0; kind < 5; kind++ ) {
		for ( size_t size : sizes ) {
			const std::vector<uint8_t> source = MakeCorpus( kind, size );
			for ( int level = 0; level <= 9; level++ ) {
				std::vector<uint8_t> compressed( Zlib_CompressBound( size ) );
				const size_t bytes = Zlib_Compress( source.data(), size, compressed.data(), level );
				CHECK( bytes > 0 && bytes <= compressed.size(), "deflate kind %d size %zu level %d: %zu bytes", kind, size, level, bytes );
				std::vector<uint8_t> back( size + 1 );
				const zlibResult_t result = Zlib_Inflate( compressed.data(), bytes, back.data(), size, true );
				CHECK( result == ZLIB_OK && ( size == 0 || std::memcmp( back.data(), source.data(), size ) == 0 ),
					   "round trip kind %d size %zu level %d: %s", kind, size, level, Zlib_ResultString( result ) );
				if ( kind == 0 && size > 1000 ) {
					CHECK( bytes <= size + size / 2000 + 16, "incompressible data grew to %zu from %zu at level %d", bytes, size, level );
				}
				if ( kind == 1 && size >= 65536 && level > 0 ) {
					CHECK( bytes < size / 200, "a zero run of %zu compressed to %zu at level %d", size, bytes, level );
				}
				if ( !dir.empty() && ( size == 0 || size == 3 || size == 65536 || size == 200000 ) ) {
					char name[96];
					std::snprintf( name, sizeof( name ), "/zlib_k%d_s%zu_l%d", kind, size, level );
					WriteBinary( dir + name + ".bin", compressed.data(), bytes );
					WriteBinary( dir + name + ".raw", source.data(), size );
				}
				streams++;
			}
		}
	}
	std::printf( "round-tripped %d deflate streams\n", streams );
}

static std::vector<uint8_t> MakeImage( int width, int height, int channels, int style ) {
	std::vector<uint8_t> pixels( (size_t)width * height * channels );
	for ( int y = 0; y < height; y++ ) {
		for ( int x = 0; x < width; x++ ) {
			uint8_t *p = &pixels[( (size_t)y * width + x ) * channels];
			const uint8_t noise = (uint8_t)( NextRandom() & 7 );
			uint8_t r = (uint8_t)( x * 255 / ( width > 1 ? width - 1 : 1 ) ), g = (uint8_t)( y * 3 + noise ), b = (uint8_t)( ( x ^ y ) + noise + 128 );
			if ( style == 1 || style == 3 ) {
				g = b = r;			// greyscale
			}
			p[0] = r;
			p[1] = g;
			p[2] = b;
			if ( channels == 4 ) {
				p[3] = ( style >= 2 ) ? (uint8_t)( ( x + y ) * 5 ) : 255;
			}
		}
	}
	return pixels;
}

static int ExpectedColorType( int channels, int style, pngWriteFormat_t format ) {
	if ( format == PNG_WRITE_RGB ) return PNG_COLOR_RGB;
	if ( format == PNG_WRITE_RGBA ) return PNG_COLOR_RGBA;
	const bool gray = style == 1 || style == 3;
	const bool alpha = channels == 4 && style >= 2;
	return gray ? ( alpha ? PNG_COLOR_GRAY_ALPHA : PNG_COLOR_GRAY ) : ( alpha ? PNG_COLOR_RGBA : PNG_COLOR_RGB );
}

static void TestEncoder( const std::string &dir ) {
	static const int sizes[][2] = { { 1, 1 }, { 2, 3 }, { 7, 5 }, { 64, 1 }, { 1, 64 }, { 33, 17 }, { 320, 240 } };
	int encoded = 0;
	int fileIndex = 0;
	for ( const auto &size : sizes ) {
		for ( int channels = 3; channels <= 4; channels++ ) {
			for ( int style = 0; style < 4; style++ ) {
				for ( int formatIndex = 0; formatIndex < 3; formatIndex++ ) {
					for ( int level : { 0, 1, 6, 9 } ) {
						const int width = size[0], height = size[1];
						const pngWriteFormat_t format = (pngWriteFormat_t)formatIndex;
						const std::vector<uint8_t> pixels = MakeImage( width, height, channels, style );
						// padded rows exercise rowStride
						const size_t stride = (size_t)width * channels + 3;
						std::vector<uint8_t> padded( stride * height, 0xEE );
						for ( int y = 0; y < height; y++ ) {
							std::memcpy( &padded[stride * y], &pixels[(size_t)width * channels * y], (size_t)width * channels );
						}
						for ( int flip = 0; flip < 2; flip++ ) {
							pngEncodeOptions_t options = {};
							options.compressionLevel = level;
							options.format = format;
							options.flipVertical = flip != 0;
							uint8_t *file = NULL;
							size_t fileSize = 0;
							const pngResult_t result = PNG_Encode( padded.data(), width, height, channels, stride, &options, &file, &fileSize );
							CHECK( result == PNG_OK, "encode %dx%d c%d s%d f%d l%d: %s", width, height, channels, style, formatIndex, level, PNG_ResultString( result ) );
							if ( result != PNG_OK ) {
								continue;
							}
							// expected RGBA in top-down order
							std::vector<uint8_t> expected( (size_t)width * height * 4 );
							for ( int y = 0; y < height; y++ ) {
								const int sy = flip ? height - 1 - y : y;
								for ( int x = 0; x < width; x++ ) {
									const uint8_t *s = &pixels[( (size_t)sy * width + x ) * channels];
									uint8_t *d = &expected[( (size_t)y * width + x ) * 4];
									d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
									d[3] = ( channels == 4 && format != PNG_WRITE_RGB ) ? s[3] : 255;
								}
							}
							pngInfo_t info;
							std::vector<uint8_t> decoded( expected.size() );
							CHECK( PNG_ReadInfo( file, fileSize, info ) == PNG_OK && info.colorType == ExpectedColorType( channels, style, format ),
								   "encode %dx%d c%d s%d f%d: colour type %d", width, height, channels, style, formatIndex, info.colorType );
							CHECK( PNG_DecodeRGBA8( file, fileSize, decoded.data(), decoded.size(), NULL ) == PNG_OK && decoded == expected,
								   "encode %dx%d c%d s%d f%d l%d flip%d: decode differs", width, height, channels, style, formatIndex, level, flip );
							if ( !dir.empty() && ( level == 6 || ( level == 0 && style == 0 ) ) && flip == ( style & 1 ) ) {
								char name[64];
								std::snprintf( name, sizeof( name ), "/enc_%04d", fileIndex++ );
								WriteBinary( dir + name + ".png", file, fileSize );
								std::vector<uint8_t> reference( 9 + expected.size() );
								const uint32_t w = (uint32_t)width, h = (uint32_t)height;
								std::memcpy( &reference[0], &w, 4 );
								std::memcpy( &reference[4], &h, 4 );
								reference[8] = (uint8_t)info.colorType;
								std::memcpy( &reference[9], expected.data(), expected.size() );
								WriteBinary( dir + name + ".rgba", reference.data(), reference.size() );
							}
							PNG_FreeEncoded( file );
							encoded++;
						}
					}
				}
			}
		}
	}
	uint8_t *file = NULL;
	size_t fileSize = 0;
	const uint8_t pixel[4] = { 1, 2, 3, 4 };
	CHECK( PNG_Encode( pixel, 0, 1, 4, 0, NULL, &file, &fileSize ) == PNG_ERR_ARGUMENT, "zero width accepted" );
	CHECK( PNG_Encode( pixel, 1, 1, 2, 0, NULL, &file, &fileSize ) == PNG_ERR_ARGUMENT, "two channels accepted" );
	CHECK( PNG_Encode( pixel, 1, 1, 4, 3, NULL, &file, &fileSize ) == PNG_ERR_ARGUMENT, "short stride accepted" );
	std::printf( "encoded and decoded %d images\n", encoded );
}

static double Seconds( std::chrono::steady_clock::time_point start ) {
	return std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
}

// screenshot-like content: smooth gradients, flat panels and sensor-like noise
static void Bench( const char *path ) {
	std::vector<uint8_t> rgba;
	int width = 1920, height = 1080;
	if ( path != NULL ) {
		const std::vector<uint8_t> file = ReadBinary( path );
		pngInfo_t info;
		if ( file.empty() || PNG_ReadInfo( file.data(), file.size(), info ) != PNG_OK ) {
			std::printf( "cannot read %s\n", path );
			return;
		}
		width = (int)info.width;
		height = (int)info.height;
		rgba.resize( (size_t)width * height * 4 );
		const auto start = std::chrono::steady_clock::now();
		int runs = 0;
		do {
			PNG_DecodeRGBA8( file.data(), file.size(), rgba.data(), rgba.size(), NULL );
			runs++;
		} while ( Seconds( start ) < 1.0 );
		const double t = Seconds( start ) / runs;
		std::printf( "decode %s (%dx%d, %zu bytes): %.2f ms, %.0f MP/s\n", path, width, height, file.size(), t * 1e3, width * (double)height / t / 1e6 );
	} else {
		rgba.resize( (size_t)width * height * 4 );
		for ( int y = 0; y < height; y++ ) {
			for ( int x = 0; x < width; x++ ) {
				uint8_t *p = &rgba[( (size_t)y * width + x ) * 4];
				const bool panel = ( x / 240 + y / 180 ) % 3 == 0;
				const uint8_t n = (uint8_t)( NextRandom() % 6 );
				p[0] = panel ? 40 : (uint8_t)( x * 255 / width + n );
				p[1] = panel ? 44 : (uint8_t)( y * 255 / height + n );
				p[2] = panel ? 52 : (uint8_t)( ( x + y ) / 12 + n );
				p[3] = 255;
			}
		}
	}
	for ( int level : { 1, 4, 6, 9 } ) {
		pngEncodeOptions_t options = {};
		options.compressionLevel = level;
		uint8_t *file = NULL;
		size_t fileSize = 0;
		auto start = std::chrono::steady_clock::now();
		int runs = 0;
		do {
			PNG_FreeEncoded( file );
			PNG_Encode( rgba.data(), width, height, 4, 0, &options, &file, &fileSize );
			runs++;
		} while ( Seconds( start ) < 1.0 );
		const double encode = Seconds( start ) / runs;
		std::vector<uint8_t> decoded( rgba.size() );
		start = std::chrono::steady_clock::now();
		runs = 0;
		do {
			PNG_DecodeRGBA8( file, fileSize, decoded.data(), decoded.size(), NULL );
			runs++;
		} while ( Seconds( start ) < 1.0 );
		const double decode = Seconds( start ) / runs;
		std::printf( "level %d: %zu bytes (%.1f%% of RGBA), encode %.1f ms, decode %.2f ms (%.0f MP/s)\n", level, fileSize,
					 100.0 * fileSize / rgba.size(), encode * 1e3, decode * 1e3, width * (double)height / decode / 1e6 );
		PNG_FreeEncoded( file );
	}
}

int main( int argc, char **argv ) {
	if ( argc >= 2 && std::strcmp( argv[1], "--bench" ) == 0 ) {
		Bench( argc >= 3 ? argv[2] : NULL );
		return 0;
	}
	const std::string dir = argc >= 2 ? argv[1] : "";
	TestFixtures();
	TestInflateFixtures();
	TestDamage();
	TestDeflate( dir );
	TestEncoder( dir );
	if ( failures != 0 ) {
		std::printf( "%d PNG codec checks failed\n", failures );
		return 1;
	}
	std::puts( "PNG codec passed" );
	return 0;
}
