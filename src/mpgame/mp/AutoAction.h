// Copyright (C) 2026 DarkMatter Productions. Quake 4 SDK EULA applies to this game-module source.
//
// openQ4 MP-22: autoaction. What a player (ui_autoAction) or a server
// (si_autoAction) does for every match: record a demo from the start of live
// play to the result, take a screenshot of the result, write the result's
// stats. The words and the file name every action shares are worked out here,
// with no engine dependency, so the native test can check them.

#ifndef __MP_AUTOACTION_H__
#define __MP_AUTOACTION_H__

#include <cctype>
#include <cstring>

enum mpAutoAction_t {
	MP_AUTOACTION_DEMO		= 1 << 0,	// "demo": record the match
	MP_AUTOACTION_SHOT		= 1 << 1,	// "ss" or "screenshot": a screenshot of the result
	MP_AUTOACTION_STATS		= 1 << 2	// "stats": the result's stats as JSON
};

// A match's files are named after it; the demo recorder keeps 64 characters.
static const int MP_MATCH_BASENAME_MAX = 64;

// idlib replaces strcmp and snprintf in the game build; these stand alone.
inline bool MPAutoActionSame( const char *a, const char *b ) {
	while ( *a != '\0' && *a == *b ) {
		a++;
		b++;
	}
	return *a == *b;
}

inline void MPAutoActionDigits( char *out, int &length, int value, int digits ) {
	for ( int i = digits - 1; i >= 0; i-- ) {
		out[ length + i ] = static_cast<char>( '0' + value % 10 );
		value /= 10;
	}
	length += digits;
}

/*
================
MPAutoActionWords

The actions a setting names: words separated by spaces or commas, any case.
Unknown words are ignored, so a later action does not break an older game.
================
*/
inline int MPAutoActionWords( const char *text ) {
	int actions = 0;
	if ( text == NULL ) {
		return 0;
	}
	const char *cursor = text;
	while ( *cursor != '\0' ) {
		while ( *cursor == ' ' || *cursor == ',' || *cursor == '\t' || *cursor == ';' ) {
			cursor++;
		}
		char word[16];
		int length = 0;
		while ( *cursor != '\0' && *cursor != ' ' && *cursor != ',' && *cursor != '\t' && *cursor != ';' ) {
			if ( length < static_cast<int>( sizeof( word ) ) - 1 ) {
				word[ length++ ] = static_cast<char>( std::tolower( static_cast<unsigned char>( *cursor ) ) );
			}
			cursor++;
		}
		word[ length ] = '\0';
		if ( MPAutoActionSame( word, "demo" ) ) {
			actions |= MP_AUTOACTION_DEMO;
		} else if ( MPAutoActionSame( word, "ss" ) || MPAutoActionSame( word, "screenshot" ) ) {
			actions |= MP_AUTOACTION_SHOT;
		} else if ( MPAutoActionSame( word, "stats" ) ) {
			actions |= MP_AUTOACTION_STATS;
		}
	}
	return actions;
}

/*
================
MPMatchBasenameAppend

One part of a match name: letters, digits, '-' and '.' kept and lowered,
colour codes dropped, everything else one '_', no leading, trailing or
doubled '_'.
================
*/
inline void MPMatchBasenameAppend( char *out, int size, int &length, const char *part ) {
	if ( part == NULL || *part == '\0' || length >= size - 2 ) {
		return;
	}
	const int separator = length;
	if ( length > 0 ) {
		out[ length++ ] = '_';
	}
	const int start = length;
	for ( const char *c = part; *c != '\0' && length < size - 1; c++ ) {
		if ( *c == '^' && c[ 1 ] != '\0' ) {	// a colour code
			c++;
			continue;
		}
		const unsigned char ch = static_cast<unsigned char>( *c );
		if ( std::isalnum( ch ) || ch == '-' || ch == '.' ) {
			out[ length++ ] = static_cast<char>( std::tolower( ch ) );
		} else if ( length > start && out[ length - 1 ] != '_' ) {
			out[ length++ ] = '_';
		}
	}
	while ( length > start && out[ length - 1 ] == '_' ) {
		length--;
	}
	if ( length == start ) {
		length = separator;	// nothing of this part survived: drop its separator too
	}
	out[ length ] = '\0';
}

/*
================
MPBuildMatchBasename

The name a match's demo, screenshot and stats share:
YYYYMMDD-HHMMSS_<game type>_<map>[_<player>], e.g.
20261123-161502_team_dm_q4dm1_recorder. The map loses its folder.
================
*/
inline void MPBuildMatchBasename( char *out, int size, int year, int month, int day, int hour, int minute, int second,
		const char *gameType, const char *map, const char *player ) {
	if ( out == NULL || size <= 0 ) {
		return;
	}
	const int limit = size < MP_MATCH_BASENAME_MAX + 1 ? size : MP_MATCH_BASENAME_MAX + 1;
	if ( limit < 16 || year < 0 || year > 9999 || month < 0 || month > 99 || day < 0 || day > 99 ||
		 hour < 0 || hour > 99 || minute < 0 || minute > 99 || second < 0 || second > 99 ) {
		out[ 0 ] = '\0';
		return;
	}
	int length = 0;
	MPAutoActionDigits( out, length, year, 4 );
	MPAutoActionDigits( out, length, month, 2 );
	MPAutoActionDigits( out, length, day, 2 );
	out[ length++ ] = '-';
	MPAutoActionDigits( out, length, hour, 2 );
	MPAutoActionDigits( out, length, minute, 2 );
	MPAutoActionDigits( out, length, second, 2 );
	out[ length ] = '\0';
	MPMatchBasenameAppend( out, limit, length, gameType );
	if ( map != NULL ) {
		const char *slash = std::strrchr( map, '/' );
		MPMatchBasenameAppend( out, limit, length, slash != NULL ? slash + 1 : map );
	}
	MPMatchBasenameAppend( out, limit, length, player );
}

#endif	// __MP_AUTOACTION_H__
