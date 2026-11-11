// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __ARB_HEAT_HAZE_DEPTH_REJECT_H__
#define __ARB_HEAT_HAZE_DEPTH_REJECT_H__

#include <cstddef>
#include <cstring>

/*
===============================================================================

	Depth rejection for the stock heat-haze fragment programs.

	heatHaze.vfp and heatHazeWithMask.vfp refract the scene by reading
	_currentRender at the fragment's own screen position plus an offset taken
	from a normal map. _currentRender is copied after every opaque surface has
	drawn, the first-person weapon included, so a haze surface beside the
	weapon reads weapon pixels and paints a refracted copy of the gun into the
	world behind it. The same happens with any world geometry in front of the
	haze: a pillar between the camera and a fire smears into the fire's haze.

	RewriteWithDepthReject adds one test before the program's final
	_currentRender read. It fetches the scene depth at the refracted
	coordinate, and when that depth is nearer than the haze fragment itself,
	the sample came from something in front of the haze, so the read falls
	back to the undistorted coordinate: the pixel straight behind the haze
	fragment, which passed the depth test and is therefore behind it. Samples
	from behind the haze keep their offset, so the refraction itself is the
	retail one.

	The renderer binds the scene depth on texture[TEXTURE_UNIT] and fills
	program.local[LOCAL_INDEX] with ( viewport width / depth texture width,
	viewport height / depth texture height, depth bias, 0 ). Binding a white
	texture there instead turns the test off: a depth of 1 is never nearer.

	It uses no idlib types, so tools/tests/native/ARBHeatHazeDepthRejectTest.cpp
	drives this same code.

===============================================================================
*/

namespace oq4arbhaze {

inline constexpr int TEXTURE_UNIT = 7;
inline constexpr int LOCAL_INDEX = 7;

// Every name the rewrite introduces starts with this.
inline constexpr char NAME_PREFIX[] = "oq4Haze";

enum rewriteResult_t {
	REWRITE_DONE,			// out holds the rewritten program
	REWRITE_NO_SCREEN_READ,	// no final "TEX result.color, <temp>, texture[0], 2D"
	REWRITE_UNSUPPORTED,	// not a fragment program, or it already uses NAME_PREFIX
	REWRITE_NO_ROOM			// out is smaller than RewriteBufferSize() asked for
};

namespace detail {

inline bool IsSpace( char c ) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

inline bool IsIdentifierChar( char c ) {
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_';
}

// idlib's Str.h turns strncmp into a compile error inside the engine.
inline bool StartsWith( const char *text, const char *prefix ) {
	for ( ; *prefix != '\0'; ++text, ++prefix ) {
		if ( *text != *prefix ) {
			return false;
		}
	}
	return true;
}

// Skips whitespace and '#' comments, which run to the end of the line.
inline const char *SkipBlank( const char *p ) {
	for ( ;; ) {
		while ( IsSpace( *p ) ) {
			++p;
		}
		if ( *p != '#' ) {
			return p;
		}
		while ( *p != '\0' && *p != '\n' ) {
			++p;
		}
	}
}

// The ';' ending the statement that starts at p, or nullptr once only END is
// left. A comment can hold a ';' without ending anything.
inline const char *StatementEnd( const char *p ) {
	for ( ; *p != '\0'; ++p ) {
		if ( *p == '#' ) {
			while ( p[1] != '\0' && p[1] != '\n' ) {
				++p;
			}
		} else if ( *p == ';' ) {
			return p;
		}
	}
	return nullptr;
}

// Splits the operands of the statement [p, end) into blank-free strings.
// Returns the operand count, or -1 when one does not fit.
inline int Operands( const char *p, const char *end, char operands[4][64] ) {
	int count = 0;
	std::size_t length = 0;
	operands[0][0] = '\0';
	for ( ; p < end; ++p ) {
		if ( *p == '#' ) {
			while ( p < end && *p != '\n' ) {
				++p;
			}
			continue;
		}
		if ( *p == ',' ) {
			if ( ++count >= 4 ) {
				return -1;
			}
			length = 0;
			operands[count][0] = '\0';
			continue;
		}
		if ( IsSpace( *p ) ) {
			continue;
		}
		if ( length + 1 >= sizeof( operands[0] ) ) {
			return -1;
		}
		operands[count][length++] = *p;
		operands[count][length] = '\0';
	}
	return count + 1;
}

inline bool Equals( const char *a, const char *b ) {
	return StartsWith( a, b ) && a[std::strlen( b )] == '\0';
}

inline bool IsPlainIdentifier( const char *text ) {
	if ( text[0] == '\0' || ( text[0] >= '0' && text[0] <= '9' ) ) {
		return false;
	}
	for ( ; *text != '\0'; ++text ) {
		if ( !IsIdentifierChar( *text ) ) {
			return false;
		}
	}
	return true;
}

class Writer {
public:
	Writer( char *target, std::size_t targetSize ) : out( target ), outSize( targetSize ) {}

	void Append( const char *text, std::size_t length ) {
		if ( overflowed || length >= outSize - used ) {
			overflowed = true;
			return;
		}
		std::memcpy( out + used, text, length );
		used += length;
		out[used] = '\0';
	}

	void Append( const char *text ) {
		Append( text, std::strlen( text ) );
	}

	void AppendInt( int value ) {
		char digits[16];
		int count = 0;
		do {
			digits[count++] = static_cast<char>( '0' + value % 10 );
			value /= 10;
		} while ( value > 0 && count < 15 );
		while ( count > 0 ) {
			Append( &digits[--count], 1 );
		}
	}

	bool Overflowed( void ) const {
		return overflowed;
	}

private:
	char *out;
	std::size_t outSize;
	std::size_t used = 0;
	bool overflowed = false;
};

// The statements inserted ahead of the final screen read of coordinate c.
inline void AppendRejectBlock( Writer &writer, const char *c ) {
	writer.Append( "TEMP oq4HazeScreen, oq4HazeBase, oq4HazeDepth;\n" );
	// back from _currentRender texture space to 0..1 screen space, then into
	// the depth texture's own space
	writer.Append( "MOV oq4HazeScreen, 0.0;\n" );
	writer.Append( "RCP oq4HazeScreen.x, program.env[0].x;\n" );
	writer.Append( "RCP oq4HazeScreen.y, program.env[0].y;\n" );
	writer.Append( "MUL oq4HazeScreen.xy, oq4HazeScreen, " );
	writer.Append( c );
	writer.Append( ";\nMUL oq4HazeScreen.xy, oq4HazeScreen, program.local[" );
	writer.AppendInt( LOCAL_INDEX );
	writer.Append( "];\nTEX oq4HazeDepth, oq4HazeScreen, texture[" );
	writer.AppendInt( TEXTURE_UNIT );
	writer.Append( "], 2D;\n" );
	// the undistorted coordinate, built the way the stock programs build theirs
	writer.Append( "MUL oq4HazeBase, fragment.position, program.env[1];\n" );
	writer.Append( "MUL oq4HazeBase, oq4HazeBase, program.env[0];\n" );
	// negative when the refracted sample is nearer than this fragment
	writer.Append( "ADD oq4HazeDepth.x, oq4HazeDepth.x, -fragment.position.z;\n" );
	writer.Append( "ADD oq4HazeDepth.x, oq4HazeDepth.x, program.local[" );
	writer.AppendInt( LOCAL_INDEX );
	writer.Append( "].z;\nCMP " );
	writer.Append( c );
	writer.Append( ", oq4HazeDepth.x, oq4HazeBase, " );
	writer.Append( c );
	writer.Append( ";\n" );
}

} // namespace detail

/*
====================
RewriteBufferSize

Bytes RewriteWithDepthReject may need for this program, terminator included.
====================
*/
inline std::size_t RewriteBufferSize( const char *program ) {
	return ( program != nullptr ? std::strlen( program ) : 0 ) + 1024;
}

/*
====================
RewriteWithDepthReject

program is one fragment program, "!!ARBfp1.0" through END. Its last statement
of the form "TEX result.color<mask>, <temp>, texture[0], 2D" is the screen
read; on REWRITE_DONE out holds the program with the depth test inserted just
before it, which may rewrite <temp>. Anything else leaves out empty, and the
caller submits the program unchanged.
====================
*/
inline rewriteResult_t RewriteWithDepthReject( const char *program, char *out, std::size_t outSize ) {
	if ( out == nullptr || outSize == 0 ) {
		return REWRITE_NO_ROOM;
	}
	out[0] = '\0';
	if ( program == nullptr || !detail::StartsWith( program, "!!ARBfp" ) ) {
		return REWRITE_UNSUPPORTED;
	}
	if ( std::strstr( program, NAME_PREFIX ) != nullptr ) {
		return REWRITE_UNSUPPORTED;
	}

	// find the last screen read
	const char *readStart = nullptr;
	char coordinate[64] = { 0 };
	for ( const char *p = program, *end = detail::StatementEnd( p ); end != nullptr;
		p = end + 1, end = detail::StatementEnd( p ) ) {
		const char *opcode = detail::SkipBlank( p );
		if ( p == program ) {
			// the "!!ARBfp1.0" header is not part of the first statement
			while ( *opcode != '\0' && !detail::IsSpace( *opcode ) ) {
				++opcode;
			}
			opcode = detail::SkipBlank( opcode );
		}
		const char *opcodeEnd = opcode;
		while ( detail::IsIdentifierChar( *opcodeEnd ) ) {
			++opcodeEnd;
		}
		if ( opcodeEnd - opcode != 3 || !detail::StartsWith( opcode, "TEX" ) ) {
			continue;
		}
		char operands[4][64];
		if ( detail::Operands( opcodeEnd, end, operands ) != 4
			|| !detail::StartsWith( operands[0], "result.color" )
			|| !detail::Equals( operands[2], "texture[0]" )
			|| !detail::Equals( operands[3], "2D" )
			|| !detail::IsPlainIdentifier( operands[1] ) ) {
			continue;
		}
		readStart = opcode;
		std::memcpy( coordinate, operands[1], sizeof( coordinate ) );
	}
	if ( readStart == nullptr ) {
		return REWRITE_NO_SCREEN_READ;
	}

	detail::Writer writer( out, outSize );
	writer.Append( program, static_cast<std::size_t>( readStart - program ) );
	detail::AppendRejectBlock( writer, coordinate );
	writer.Append( readStart );

	if ( writer.Overflowed() ) {
		out[0] = '\0';
		return REWRITE_NO_ROOM;
	}
	return REWRITE_DONE;
}

} // namespace oq4arbhaze

#endif /* !__ARB_HEAT_HAZE_DEPTH_REJECT_H__ */
