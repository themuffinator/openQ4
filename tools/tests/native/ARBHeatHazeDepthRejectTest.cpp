/*
===========================================================================

openQ4 ARB heat-haze depth rejection native tests

Copyright (C) 2026 DarkMatter Productions
SPDX-License-Identifier: GPL-3.0-or-later

Drives src/renderer/ARBHeatHazeDepthReject.h, the rewrite R_LoadARBProgram
applies to the stock heat-haze fragment programs so their refracted
_currentRender read cannot pick up the first-person weapon. The programs below
follow the stock programs' shape (normal map, screen coordinate, offset,
non-power-of-two scale, final screen read) without copying them.

===========================================================================
*/

#include "src/renderer/ARBHeatHazeDepthReject.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace haze = oq4arbhaze;

static int failures = 0;

static void Expect( const bool condition, const std::string &label ) {
	if ( !condition ) {
		std::fprintf( stderr, "ARB heat-haze depth reject failed: %s\n", label.c_str() );
		failures++;
	}
}

static haze::rewriteResult_t Rewrite( const char *program, std::string &out ) {
	std::vector<char> buffer( haze::RewriteBufferSize( program ) );
	const haze::rewriteResult_t result = haze::RewriteWithDepthReject( program, buffer.data(), buffer.size() );
	out = buffer.data();
	return result;
}

static const char hazeProgram[] =
	"!!ARBfp1.0\n"
	"OPTION ARB_precision_hint_fastest;\n"
	"# texture 0 is _currentRender; a ';' in a comment ends nothing\n"
	"TEMP\tn, coord;\n"
	"TEX\tn, fragment.texcoord[1], texture[1], 2D;\n"
	"MUL\tcoord, fragment.position, program.env[1];\n"
	"MAD_SAT\tcoord, n, fragment.texcoord[2], coord;\n"
	"MUL\tcoord, coord, program.env[0];\n"
	"TEX\t\tresult.color.xyz, coord, texture[0], 2D;\n"
	"\n"
	"END\n";

// The masked shape reads three textures and kills first.
static const char maskedProgram[] =
	"!!ARBfp1.0 \n"
	"TEMP\tmask, n, R0;\n"
	"TEX\tmask, fragment.texcoord[0], texture[2], 2D;\n"
	"KIL\tmask;\n"
	"TEX\tn, fragment.texcoord[1], texture[1], 2D;\n"
	"MUL\tR0, fragment.position, program.env[1];\n"
	"MAD_SAT\tR0, n, fragment.texcoord[2], R0;\n"
	"MUL\tR0, R0, program.env[0];\n"
	"TEX result.color, R0, texture[0], 2D;\n"
	"END\n";

static void TestRewritesBeforeFinalScreenRead( const char *program, const char *coordinate, const char *read ) {
	std::string out;
	const haze::rewriteResult_t result = Rewrite( program, out );
	Expect( result == haze::REWRITE_DONE, std::string( coordinate ) + ": rewrite done" );

	const size_t block = out.find( "TEMP oq4HazeScreen" );
	const size_t readAt = out.find( read );
	Expect( block != std::string::npos && readAt != std::string::npos && block < readAt,
		std::string( coordinate ) + ": test inserted before the screen read" );
	Expect( out.find( "texture[7], 2D" ) != std::string::npos, std::string( coordinate ) + ": depth on unit 7" );
	Expect( out.find( "program.local[7]" ) != std::string::npos, std::string( coordinate ) + ": scale in local 7" );
	Expect( out.find( "fragment.position.z" ) != std::string::npos, std::string( coordinate ) + ": compares fragment depth" );

	const std::string select = std::string( "CMP " ) + coordinate + ", oq4HazeDepth.x, oq4HazeBase, " + coordinate + ";";
	Expect( out.find( select ) != std::string::npos && out.find( select ) < readAt,
		std::string( coordinate ) + ": selects the undistorted coordinate for nearer samples" );

	// Everything before the inserted block, and from the read on, is the original.
	const size_t originalRead = std::string( program ).find( read );
	Expect( out.compare( 0, block, program, block ) == 0 && block == originalRead,
		std::string( coordinate ) + ": prefix unchanged" );
	Expect( out.substr( readAt ) == std::string( program ).substr( originalRead ),
		std::string( coordinate ) + ": suffix unchanged" );
	Expect( out.rfind( "END" ) != std::string::npos, std::string( coordinate ) + ": END kept" );
}

static void TestRejections( void ) {
	std::string out;
	Expect( Rewrite( "!!ARBvp1.0\nMOV result.position, vertex.position;\nEND\n", out ) == haze::REWRITE_UNSUPPORTED && out.empty(),
		"vertex program refused" );
	Expect( Rewrite( "!!ARBfp1.0\nTEMP oq4HazeX;\nTEX result.color, oq4HazeX, texture[0], 2D;\nEND\n", out )
		== haze::REWRITE_UNSUPPORTED, "program using the prefix refused" );
	Expect( Rewrite( "!!ARBfp1.0\nMOV result.color, fragment.color;\nEND\n", out ) == haze::REWRITE_NO_SCREEN_READ && out.empty(),
		"no screen read" );
	Expect( Rewrite( "!!ARBfp1.0\nTEX result.color, fragment.texcoord[0], texture[0], 2D;\nEND\n", out )
		== haze::REWRITE_NO_SCREEN_READ, "attribute coordinate is not a computed screen read" );
	Expect( Rewrite( "!!ARBfp1.0\nTEMP c;\nTEX result.color, c, texture[1], 2D;\nEND\n", out )
		== haze::REWRITE_NO_SCREEN_READ, "a read of another unit is not the screen read" );
	Expect( Rewrite( "!!ARBfp1.0\n# TEX result.color, c, texture[0], 2D;\nMOV result.color, 0.0;\nEND\n", out )
		== haze::REWRITE_NO_SCREEN_READ, "a commented-out read does not count" );

	char tiny[16];
	Expect( haze::RewriteWithDepthReject( hazeProgram, tiny, sizeof( tiny ) ) == haze::REWRITE_NO_ROOM && tiny[0] == '\0',
		"small buffer reports no room" );
}

static void TestLastReadWins( void ) {
	static const char twoReads[] =
		"!!ARBfp1.0\n"
		"TEMP a, b;\n"
		"TEX result.color, a, texture[0], 2D;\n"
		"TEX result.color, b, texture[0], 2D;\n"
		"END\n";
	std::string out;
	Expect( Rewrite( twoReads, out ) == haze::REWRITE_DONE, "two reads: done" );
	Expect( out.find( "CMP b," ) != std::string::npos && out.find( "CMP a," ) == std::string::npos,
		"two reads: the final read is the one tested" );
}

int main( void ) {
	TestRewritesBeforeFinalScreenRead( hazeProgram, "coord", "TEX\t\tresult.color.xyz, coord" );
	TestRewritesBeforeFinalScreenRead( maskedProgram, "R0", "TEX result.color, R0" );
	TestRejections();
	TestLastReadWins();
	if ( failures != 0 ) {
		std::fprintf( stderr, "ARB heat-haze depth reject: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "ARB heat-haze depth reject: ok\n" );
	return 0;
}
