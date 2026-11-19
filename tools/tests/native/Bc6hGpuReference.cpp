// Regenerates the BC6H reference digests in Bc6hCodecTest.cpp by decoding the
// test blocks on the GPU, which Direct3D 11 and ARB_texture_compression_bptc
// require to decode BC6H bit-exactly. Software decoders were no use as a
// reference: Pillow and texture2ddecoder only return 8-bit colour, and
// texture2ddecoder has no signed BC6H at all. The table was generated on an
// Intel Iris Xe and an NVIDIA RTX 4060 (the exports below pick the discrete
// GPU on a hybrid laptop), which agreed on every texel.
//
// Windows only, run by hand; it needs a GL 4.2 or BPTC-capable driver.
//   clang++ -O2 -D_CRT_SECURE_NO_WARNINGS tools/tests/native/Bc6hGpuReference.cpp -lopengl32 -luser32 -lgdi32 -o bc6h_gpu_reference.exe
//   bc6h_gpu_reference.exe
#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "Bc6hBlocks.h"

typedef unsigned char byte;
#define __PRECOMPILED_H__
#include "../../../src/idlib/hashing/CRC32.cpp"

extern "C" __declspec( dllexport ) unsigned long NvOptimusEnablement = 1;
extern "C" __declspec( dllexport ) int AmdPowerXpressRequestHighPerformance = 1;

typedef void ( APIENTRY *compressedTexImage2D_t )( GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void * );

static const GLenum GL_BPTC_SIGNED_FLOAT = 0x8E8E;
static const GLenum GL_BPTC_UNSIGNED_FLOAT = 0x8E8F;
static const GLenum GL_HALF_FLOAT_TYPE = 0x140B;

int main() {
	WNDCLASSA wc = {};
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandle( NULL );
	wc.lpszClassName = "bc6hGpuReference";
	RegisterClassA( &wc );
	HWND window = CreateWindowA( wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, NULL, NULL, wc.hInstance, NULL );
	HDC dc = GetDC( window );
	PIXELFORMATDESCRIPTOR pfd = { sizeof( pfd ), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
	SetPixelFormat( dc, ChoosePixelFormat( dc, &pfd ), &pfd );
	HGLRC rc = wglCreateContext( dc );
	if ( rc == NULL || !wglMakeCurrent( dc, rc ) ) {
		std::fprintf( stderr, "no OpenGL context\n" );
		return 1;
	}
	std::fprintf( stderr, "decoding on %s\n", (const char *)glGetString( GL_RENDERER ) );
	compressedTexImage2D_t compressedTexImage2D = (compressedTexImage2D_t)wglGetProcAddress( "glCompressedTexImage2D" );
	if ( compressedTexImage2D == NULL ) {
		std::fprintf( stderr, "no glCompressedTexImage2D\n" );
		return 1;
	}

	for ( int isSigned = 0; isSigned < 2; isSigned++ ) {
		bc6hTestRandom_t rng( BC6H_TEST_SEED + (uint32_t)isSigned );
		std::printf( "static const uint32_t referenceCrc%s[BC6H_TEST_GROUPS] = {\n\t", isSigned ? "Signed" : "Unsigned" );
		for ( int group = 0; group < BC6H_TEST_GROUPS; group++ ) {
			const int count = BC6H_TestGroupBlocks( group );
			std::vector<byte> blocks( (size_t)count * 16 );
			for ( int i = 0; i < count; i++ ) {
				BC6H_TestMakeBlock( rng, group, i, &blocks[ (size_t)i * 16 ] );
			}
			// one texture, a row of blocks; texels back out in block order
			std::vector<unsigned short> pixels( (size_t)count * 64 );
			GLuint texture = 0;
			glGenTextures( 1, &texture );
			glBindTexture( GL_TEXTURE_2D, texture );
			compressedTexImage2D( GL_TEXTURE_2D, 0, isSigned ? GL_BPTC_SIGNED_FLOAT : GL_BPTC_UNSIGNED_FLOAT, count * 4, 4, 0, count * 16, blocks.data() );
			glPixelStorei( GL_PACK_ALIGNMENT, 1 );
			glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_HALF_FLOAT_TYPE, pixels.data() );
			if ( glGetError() != GL_NO_ERROR ) {
				std::fprintf( stderr, "\nupload or readback failed\n" );
				return 1;
			}
			glDeleteTextures( 1, &texture );
			std::vector<unsigned short> ordered( pixels.size() );
			for ( int b = 0; b < count; b++ ) {
				for ( int texel = 0; texel < 16; texel++ ) {
					const int x = b * 4 + ( texel & 3 );
					const int y = texel >> 2;
					memcpy( &ordered[ ( (size_t)b * 16 + texel ) * 4 ], &pixels[ ( (size_t)y * count * 4 + x ) * 4 ], 8 );
				}
			}
			std::printf( "0x%08x%s", CRC32_BlockChecksum( ordered.data(), (int)( ordered.size() * 2 ) ),
				group + 1 < BC6H_TEST_GROUPS ? ( group % 5 == 4 ? ",\n\t" : ", " ) : "\n" );
		}
		std::printf( "};\n" );
	}
	wglMakeCurrent( NULL, NULL );
	wglDeleteContext( rc );
	return 0;
}
