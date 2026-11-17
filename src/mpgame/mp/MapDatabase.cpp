//----------------------------------------------------------------
// MapDatabase.cpp
//
// openQ4: the multiplayer map database core.  See MapDatabase.h.
//----------------------------------------------------------------

#if defined( MP_MAP_DATABASE_STANDALONE_TEST )
	#include "MapDatabase.h"
	#include <string.h>
#else
	#include "../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MapDatabase.h"
#endif

namespace {

static int MapDbLower( int value ) {
	if ( value >= 'A' && value <= 'Z' ) {
		return value + ( 'a' - 'A' );
	}
	return value;
}

static bool MapDbEquals( const char *lhs, const char *rhs ) {
	if ( lhs == NULL || rhs == NULL ) {
		return false;
	}
	while ( *lhs != '\0' && *rhs != '\0' ) {
		if ( MapDbLower( static_cast<unsigned char>( *lhs ) ) !=
			MapDbLower( static_cast<unsigned char>( *rhs ) ) ) {
			return false;
		}
		++lhs;
		++rhs;
	}
	return *lhs == *rhs;
}

static int MapDbLength( const char *text, int maximum ) {
	int length = 0;
	while ( text != NULL && length < maximum && text[ length ] != '\0' ) {
		++length;
	}
	return length;
}

static void MapDbCopy( char *destination, int bytes, const char *source ) {
	int length = 0;
	if ( source != NULL ) {
		while ( length < bytes - 1 && source[ length ] != '\0' ) {
			destination[ length ] = source[ length ];
			++length;
		}
	}
	for ( int index = length; index < bytes; ++index ) {
		destination[ index ] = '\0';
	}
}

static bool MapDbContains( const char *text, const char *word ) {
	const int wordLength = MapDbLength( word, MP_MAPDB_TOKEN_BYTES );
	for ( const char *cursor = text; *cursor != '\0'; ++cursor ) {
		int matched = 0;
		while ( matched < wordLength && cursor[ matched ] != '\0' &&
			MapDbLower( static_cast<unsigned char>( cursor[ matched ] ) ) == word[ matched ] ) {
			++matched;
		}
		if ( matched == wordLength ) {
			return true;
		}
	}
	return false;
}

// xorshift32; a zero state is replaced so the sequence never sticks.
static uint32_t MapDbNextRandom( uint32_t &state ) {
	if ( state == 0 ) {
		state = 0x9e3779b9u;
	}
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

static const char *const mapDbStockTokens[] = {
	"mp/q4dm1", "mp/q4dm2", "mp/q4dm3", "mp/q4dm4", "mp/q4dm5", "mp/q4dm6",
	"mp/q4dm7", "mp/q4dm8", "mp/q4dm9", "mp/q4dm10", "mp/q4dm11", "mp/q4dm11v1",
	"mp/q4ctf1", "mp/q4ctf2", "mp/q4ctf3", "mp/q4ctf4", "mp/q4ctf5", "mp/q4ctf6",
	"mp/q4ctf7", "mp/q4ctf8", "mp/q4tourney1",
	"mp/q4xctf6", "mp/q4xdm10", "mp/q4xdm11", "mp/q4xdm13", "mp/q4xdm14", "mp/q4xdm15",
	"mp/q4xtourney1", "mp/q4xtourney2",
	"mp/q4dz1", "mp/q4dz2", "mp/q4dz3", "mp/q4dz4",
};

/*
===============================================================================

	Override text reader

	Words are separated by white space; a word in double quotes may hold
	spaces.  Braces stand alone.  // and block comments are skipped.

===============================================================================
*/

static const int MAPDB_WORD_BYTES = 256;

class mapDbReader {
public:
	mapDbReader( const char *sourceText, int sourceLength ) :
		text( sourceText ), length( sourceLength ), cursor( 0 ), line( 1 ), failed( false ) {
	}

	// False at the end of the text or after an error.
	bool Read( char word[ MAPDB_WORD_BYTES ], bool &quoted ) {
		quoted = false;
		word[ 0 ] = '\0';
		if ( failed || !SkipSpace() ) {
			return false;
		}
		const char first = text[ cursor ];
		if ( first == '{' || first == '}' ) {
			word[ 0 ] = first;
			word[ 1 ] = '\0';
			++cursor;
			return true;
		}
		int written = 0;
		if ( first == '"' ) {
			quoted = true;
			++cursor;
			while ( cursor < length && text[ cursor ] != '"' ) {
				if ( text[ cursor ] == '\n' || text[ cursor ] == '\r' || text[ cursor ] == '\0' ||
					written >= MAPDB_WORD_BYTES - 1 ) {
					failed = true;
					return false;
				}
				word[ written++ ] = text[ cursor++ ];
			}
			if ( cursor >= length ) {
				failed = true;
				return false;
			}
			++cursor;
			word[ written ] = '\0';
			return true;
		}
		while ( cursor < length ) {
			const char value = text[ cursor ];
			if ( value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\0' ||
				value == '{' || value == '}' || value == '"' ) {
				break;
			}
			if ( value == '/' && cursor + 1 < length &&
				( text[ cursor + 1 ] == '/' || text[ cursor + 1 ] == '*' ) ) {
				break;
			}
			if ( written >= MAPDB_WORD_BYTES - 1 ) {
				failed = true;
				return false;
			}
			word[ written++ ] = value;
			++cursor;
		}
		word[ written ] = '\0';
		return written > 0;
	}

	int Line( void ) const { return line; }
	bool Failed( void ) const { return failed; }

private:
	bool SkipSpace( void ) {
		while ( cursor < length ) {
			const char value = text[ cursor ];
			if ( value == '\n' ) {
				++line;
				++cursor;
			} else if ( value == ' ' || value == '\t' || value == '\r' ) {
				++cursor;
			} else if ( value == '\0' ) {
				cursor = length;
			} else if ( value == '/' && cursor + 1 < length && text[ cursor + 1 ] == '/' ) {
				while ( cursor < length && text[ cursor ] != '\n' ) {
					++cursor;
				}
			} else if ( value == '/' && cursor + 1 < length && text[ cursor + 1 ] == '*' ) {
				cursor += 2;
				const int openedAt = line;
				bool closed = false;
				while ( cursor < length ) {
					if ( text[ cursor ] == '\n' ) {
						++line;
					} else if ( text[ cursor ] == '*' && cursor + 1 < length && text[ cursor + 1 ] == '/' ) {
						cursor += 2;
						closed = true;
						break;
					}
					++cursor;
				}
				if ( !closed ) {
					// name the line the comment opens on, not the end of the file
					line = openedAt;
					failed = true;
					return false;
				}
			} else {
				return true;
			}
		}
		return false;
	}

	const char *	text;
	int				length;
	int				cursor;
	int				line;
	bool			failed;
};

static bool MapDbParseInteger( const char *word, int minimum, int maximum, int &value ) {
	if ( word == NULL || word[ 0 ] == '\0' ) {
		return false;
	}
	int result = 0;
	int digits = 0;
	for ( const char *cursor = word; *cursor != '\0'; ++cursor ) {
		if ( *cursor < '0' || *cursor > '9' || ++digits > 6 ) {
			return false;
		}
		result = result * 10 + ( *cursor - '0' );
	}
	if ( result < minimum || result > maximum ) {
		return false;
	}
	value = result;
	return true;
}

static bool MapDbFail( mpMapOverrideReport &report, int line, const char *message, const char *detail = NULL ) {
	report.line = line;
	int written = 0;
	for ( const char *cursor = message; *cursor != '\0' && written < MP_MAPDB_ERROR_BYTES - 1; ++cursor ) {
		report.error[ written++ ] = *cursor;
	}
	if ( detail != NULL ) {
		const char *const prefix = ": ";
		for ( const char *cursor = prefix; *cursor != '\0' && written < MP_MAPDB_ERROR_BYTES - 1; ++cursor ) {
			report.error[ written++ ] = *cursor;
		}
		for ( const char *cursor = detail; *cursor != '\0' && written < MP_MAPDB_ERROR_BYTES - 1; ++cursor ) {
			// the detail is file text; keep the message printable
			const unsigned char value = static_cast<unsigned char>( *cursor );
			report.error[ written++ ] = ( value >= 0x20 && value < 0x7f && value != '%' ) ? *cursor : '?';
		}
	}
	report.error[ written ] = '\0';
	return false;
}

} // namespace

/*
===============================================================================

	mpMapDatabase

===============================================================================
*/

mpMapDatabase::mpMapDatabase( void ) {
	revision = 0;
	Clear();
}

/*
================
mpMapDatabase::Clear

Forgets every map and everything that refers to one.  The revision keeps
counting so a reader can tell a rebuilt database from the one it cached.
================
*/
void mpMapDatabase::Clear( void ) {
	memset( entries, 0, sizeof( entries ) );
	memset( settingPool, 0, sizeof( settingPool ) );
	memset( settingUsed, 0, sizeof( settingUsed ) );
	memset( rotation, 0, sizeof( rotation ) );
	memset( queue, 0, sizeof( queue ) );
	memset( history, 0, sizeof( history ) );
	count = 0;
	rotationCount = 0;
	rotationExplicit = false;
	queueCount = 0;
	historyCount = 0;
	PickClose();
	++revision;
}

/*
================
mpMapDatabase::IsSafeToken

The rule of mpCompetitionSeries::IsSafeMapToken, so every database map can
enter a series pool: letters, digits, '_', '-' and single interior slashes.
================
*/
bool mpMapDatabase::IsSafeToken( const char *token ) {
	if ( token == NULL || token[ 0 ] == '\0' ) {
		return false;
	}
	int length = 0;
	char previous = '\0';
	for ( const char *cursor = token; *cursor != '\0'; ++cursor ) {
		const unsigned char value = static_cast<unsigned char>( *cursor );
		if ( ++length >= MP_MAPDB_TOKEN_BYTES ) {
			return false;
		}
		if ( !( ( value >= 'a' && value <= 'z' ) || ( value >= 'A' && value <= 'Z' ) ||
			( value >= '0' && value <= '9' ) || value == '_' || value == '-' || value == '/' ) ) {
			return false;
		}
		if ( value == '/' && ( previous == '\0' || previous == '/' ) ) {
			return false;
		}
		previous = static_cast<char>( value );
	}
	return previous != '/';
}

/*
================
mpMapDatabase::NormalizeToken

"maps/mp/q4dm1.map", "mp\q4dm1" and "mp/q4dm1" name one map.  Case is kept:
a loose map file on a case-sensitive disk loads by the spelling it was
declared with, and lookups compare without case.
================
*/
bool mpMapDatabase::NormalizeToken( const char *path, char token[ MP_MAPDB_TOKEN_BYTES ] ) {
	memset( token, 0, MP_MAPDB_TOKEN_BYTES );
	if ( path == NULL ) {
		return false;
	}
	char buffer[ MP_MAPDB_TOKEN_BYTES + 16 ];
	int length = 0;
	for ( const char *cursor = path; *cursor != '\0'; ++cursor ) {
		if ( length >= static_cast<int>( sizeof( buffer ) ) - 1 ) {
			return false;
		}
		buffer[ length++ ] = ( *cursor == '\\' ) ? '/' : *cursor;
	}
	buffer[ length ] = '\0';

	// the extension is whatever follows the last dot of the leaf name
	for ( int index = length - 1; index >= 0 && buffer[ index ] != '/'; --index ) {
		if ( buffer[ index ] == '.' ) {
			buffer[ index ] = '\0';
			length = index;
			break;
		}
	}

	const char *start = buffer;
	if ( length >= 5 && MapDbLower( buffer[ 0 ] ) == 'm' && MapDbLower( buffer[ 1 ] ) == 'a' &&
		MapDbLower( buffer[ 2 ] ) == 'p' && MapDbLower( buffer[ 3 ] ) == 's' && buffer[ 4 ] == '/' ) {
		start = buffer + 5;
	}
	if ( !IsSafeToken( start ) ) {
		return false;
	}
	MapDbCopy( token, MP_MAPDB_TOKEN_BYTES, start );
	return true;
}

/*
================
mpMapDatabase::IsStockToken
================
*/
bool mpMapDatabase::IsStockToken( const char *token ) {
	const int stockCount = static_cast<int>( sizeof( mapDbStockTokens ) / sizeof( mapDbStockTokens[ 0 ] ) );
	for ( int index = 0; index < stockCount; ++index ) {
		if ( MapDbEquals( token, mapDbStockTokens[ index ] ) ) {
			return true;
		}
	}
	return false;
}

/*
================
mpMapDatabase::DefaultBounds

Bounds for a map that neither its mapDef nor the server's override file
describes.  Only what the map itself says is used: a tourney arena (and the
"v1" duel cut of a larger map) holds two to four players, and a map with no
free-for-all mode is an objective map that needs two a side.  Everything else
is unbounded; a server that knows its maps better says so in the override file.
================
*/
void mpMapDatabase::DefaultBounds( const char *token, uint32_t modes, uint32_t freeForAllModes,
		int &minPlayers, int &maxPlayers ) {
	minPlayers = 0;
	maxPlayers = 0;
	if ( token == NULL ) {
		return;
	}
	const char *leaf = token;
	for ( const char *cursor = token; *cursor != '\0'; ++cursor ) {
		if ( *cursor == '/' ) {
			leaf = cursor + 1;
		}
	}
	const int leafLength = MapDbLength( leaf, MP_MAPDB_TOKEN_BYTES );
	const bool duelCut = leafLength > 2 && MapDbLower( leaf[ leafLength - 2 ] ) == 'v' && leaf[ leafLength - 1 ] == '1';
	if ( MapDbContains( leaf, "tourney" ) || duelCut ) {
		minPlayers = 2;
		maxPlayers = 4;
		return;
	}
	if ( modes != 0 && ( modes & freeForAllModes ) == 0 ) {
		minPlayers = 4;
	}
}

/*
================
mpMapDatabase::AddMap
================
*/
int mpMapDatabase::AddMap( const char *path, const char *name, uint32_t modes, int hostIndex ) {
	char token[ MP_MAPDB_TOKEN_BYTES ];
	if ( count >= MP_MAPDB_MAX_MAPS || !NormalizeToken( path, token ) || Find( token ) >= 0 ) {
		return -1;
	}
	mpMapEntry &entry = entries[ count ];
	memset( &entry, 0, sizeof( entry ) );
	MapDbCopy( entry.token, MP_MAPDB_TOKEN_BYTES, token );
	MapDbCopy( entry.name, MP_MAPDB_NAME_BYTES, ( name != NULL && name[ 0 ] != '\0' ) ? name : token );
	entry.modes = modes;
	entry.flags = MP_MAPDB_DEFAULT_FLAGS | ( IsStockToken( token ) ? MP_MAPDB_STOCK : 0 );
	entry.hostIndex = hostIndex;
	++revision;
	return count++;
}

bool mpMapDatabase::SetBounds( int index, int minPlayers, int maxPlayers ) {
	if ( index < 0 || index >= count || minPlayers < 0 || maxPlayers < 0 ||
		minPlayers > MP_MAPDB_MAX_PLAYERS || maxPlayers > MP_MAPDB_MAX_PLAYERS ||
		( maxPlayers != 0 && minPlayers > maxPlayers ) ) {
		return false;
	}
	entries[ index ].minPlayers = minPlayers;
	entries[ index ].maxPlayers = maxPlayers;
	++revision;
	return true;
}

bool mpMapDatabase::SetFlags( int index, int flags ) {
	if ( index < 0 || index >= count ) {
		return false;
	}
	entries[ index ].flags = flags;
	++revision;
	return true;
}

int mpMapDatabase::AllocateSetting( void ) {
	for ( int index = 0; index < MP_MAPDB_MAX_SETTINGS; ++index ) {
		if ( !settingUsed[ index ] ) {
			settingUsed[ index ] = true;
			return index;
		}
	}
	return -1;
}

/*
================
mpMapDatabase::AddSetting

A later value for a key replaces the earlier one.  False when the key or
value does not fit, or the map or the pool is full.
================
*/
bool mpMapDatabase::AddSetting( int index, const char *key, const char *value ) {
	if ( index < 0 || index >= count || key == NULL || value == NULL || key[ 0 ] == '\0' ||
		MapDbLength( key, MP_MAPDB_SETTING_KEY_BYTES ) >= MP_MAPDB_SETTING_KEY_BYTES ||
		MapDbLength( value, MP_MAPDB_SETTING_VALUE_BYTES ) >= MP_MAPDB_SETTING_VALUE_BYTES ) {
		return false;
	}
	mpMapEntry &entry = entries[ index ];
	for ( int setting = 0; setting < entry.settingCount; ++setting ) {
		mpMapSetting &existing = settingPool[ entry.settings[ setting ] ];
		if ( MapDbEquals( existing.key, key ) ) {
			MapDbCopy( existing.value, MP_MAPDB_SETTING_VALUE_BYTES, value );
			++revision;
			return true;
		}
	}
	if ( entry.settingCount >= MP_MAPDB_MAX_MAP_SETTINGS ) {
		return false;
	}
	const int slot = AllocateSetting();
	if ( slot < 0 ) {
		return false;
	}
	MapDbCopy( settingPool[ slot ].key, MP_MAPDB_SETTING_KEY_BYTES, key );
	MapDbCopy( settingPool[ slot ].value, MP_MAPDB_SETTING_VALUE_BYTES, value );
	entry.settings[ entry.settingCount++ ] = slot;
	++revision;
	return true;
}

void mpMapDatabase::ClearSettings( int index ) {
	if ( index < 0 || index >= count ) {
		return;
	}
	mpMapEntry &entry = entries[ index ];
	for ( int setting = 0; setting < entry.settingCount; ++setting ) {
		settingUsed[ entry.settings[ setting ] ] = false;
	}
	entry.settingCount = 0;
	++revision;
}

const mpMapEntry *mpMapDatabase::Entry( int index ) const {
	return ( index >= 0 && index < count ) ? &entries[ index ] : NULL;
}

int mpMapDatabase::Find( const char *path ) const {
	char token[ MP_MAPDB_TOKEN_BYTES ];
	if ( !NormalizeToken( path, token ) ) {
		return -1;
	}
	for ( int index = 0; index < count; ++index ) {
		if ( MapDbEquals( entries[ index ].token, token ) ) {
			return index;
		}
	}
	return -1;
}

int mpMapDatabase::FindHostIndex( int hostIndex ) const {
	if ( hostIndex < 0 ) {
		return -1;
	}
	for ( int index = 0; index < count; ++index ) {
		if ( entries[ index ].hostIndex == hostIndex ) {
			return index;
		}
	}
	return -1;
}

const mpMapSetting *mpMapDatabase::Setting( int index, int setting ) const {
	if ( index < 0 || index >= count || setting < 0 || setting >= entries[ index ].settingCount ) {
		return NULL;
	}
	return &settingPool[ entries[ index ].settings[ setting ] ];
}

bool mpMapDatabase::Supports( int index, int gameType ) const {
	if ( index < 0 || index >= count || gameType < 0 || gameType >= 32 ) {
		return false;
	}
	return ( entries[ index ].modes & ( 1u << gameType ) ) != 0;
}

bool mpMapDatabase::FitsPlayers( int index, int playerCount ) const {
	if ( index < 0 || index >= count ) {
		return false;
	}
	const mpMapEntry &entry = entries[ index ];
	return ( entry.minPlayers == 0 || playerCount >= entry.minPlayers ) &&
		( entry.maxPlayers == 0 || playerCount <= entry.maxPlayers );
}

int mpMapDatabase::Collect( int gameType, int requiredFlags, int *indices, int maximum ) const {
	int found = 0;
	for ( int index = 0; index < count; ++index ) {
		if ( ( entries[ index ].flags & requiredFlags ) != requiredFlags ) {
			continue;
		}
		if ( gameType >= 0 ? !Supports( index, gameType ) : entries[ index ].modes == 0 ) {
			continue;
		}
		if ( indices != NULL && found < maximum ) {
			indices[ found ] = index;
		}
		++found;
	}
	return ( indices != NULL && found > maximum ) ? maximum : found;
}

/*
===============================================================================

	Overrides

===============================================================================
*/

/*
================
mpMapDatabase::ApplyOverrides

	defaults { players 2 12  rotation 1  vote 1  queue 1 }
	map mp/q4dm1 {
		players 2 8
		modes "DM, Team DM, Duel"
		name "The Fragging Yard"
		rotation 1
		vote 1
		queue 1
		set si_fragLimit 30
	}
	rotation { mp/q4dm1 mp/q4dm2 mp/q4ctf1 }

The whole text is applied to a copy first, so a file with an error changes
nothing.
================
*/
bool mpMapDatabase::ApplyOverrides( const char *text, int length, mpMapModeResolver_t resolveMode,
		mpMapOverrideReport &report ) {
	memset( &report, 0, sizeof( report ) );
	if ( text == NULL || length < 0 || length > MP_MAPDB_MAX_TEXT_BYTES ) {
		return MapDbFail( report, 1, "the override text is missing or too large" );
	}

	mpMapDatabase *draft = new mpMapDatabase( *this );
	mapDbReader reader( text, length );
	char word[ MAPDB_WORD_BYTES ];
	char argument[ MAPDB_WORD_BYTES ];
	char second[ MAPDB_WORD_BYTES ];
	bool quoted = false;
	bool ok = true;

	while ( ok && reader.Read( word, quoted ) ) {
		const bool isMap = MapDbEquals( word, "map" );
		const bool isDefaults = MapDbEquals( word, "defaults" );
		const bool isRotation = MapDbEquals( word, "rotation" );
		if ( quoted || ( !isMap && !isDefaults && !isRotation ) ) {
			ok = MapDbFail( report, reader.Line(), "expected map, defaults or rotation", word );
			break;
		}

		int target = -1;
		if ( isMap ) {
			if ( !reader.Read( argument, quoted ) || argument[ 0 ] == '{' || argument[ 0 ] == '}' ) {
				ok = MapDbFail( report, reader.Line(), "map needs a map path" );
				break;
			}
			char token[ MP_MAPDB_TOKEN_BYTES ];
			if ( !NormalizeToken( argument, token ) ) {
				ok = MapDbFail( report, reader.Line(), "not a map path", argument );
				break;
			}
			target = draft->Find( token );
			if ( target < 0 ) {
				++report.mapsUnknown;
			} else {
				++report.mapsChanged;
				draft->entries[ target ].flags |= MP_MAPDB_OVERRIDDEN;
			}
		}
		if ( !reader.Read( word, quoted ) || quoted || word[ 0 ] != '{' ) {
			ok = MapDbFail( report, reader.Line(), "expected {" );
			break;
		}

		if ( isRotation ) {
			draft->ClearRotation();
			draft->rotationExplicit = true;
			for ( ;; ) {
				if ( !reader.Read( word, quoted ) ) {
					ok = MapDbFail( report, reader.Line(), "the rotation block is not closed" );
					break;
				}
				if ( !quoted && word[ 0 ] == '}' ) {
					break;
				}
				if ( !quoted && word[ 0 ] == '{' ) {
					ok = MapDbFail( report, reader.Line(), "unexpected {" );
					break;
				}
				char token[ MP_MAPDB_TOKEN_BYTES ];
				if ( !NormalizeToken( word, token ) ) {
					ok = MapDbFail( report, reader.Line(), "not a map path", word );
					break;
				}
				if ( draft->Find( token ) < 0 ) {
					++report.rotationUnknown;
				} else if ( draft->AppendRotation( token ) ) {
					++report.rotationListed;
				}
			}
			continue;
		}

		// map and defaults blocks share their statements; defaults reaches every map
		const int first = isDefaults ? 0 : target;
		const int last = isDefaults ? draft->count - 1 : target;
		for ( ;; ) {
			if ( !reader.Read( word, quoted ) ) {
				ok = MapDbFail( report, reader.Line(), "the block is not closed" );
				break;
			}
			if ( !quoted && word[ 0 ] == '}' ) {
				break;
			}
			if ( quoted || word[ 0 ] == '{' ) {
				ok = MapDbFail( report, reader.Line(), "expected a statement", word );
				break;
			}
			const int statementLine = reader.Line();
			bool argumentQuoted = false;
			if ( !reader.Read( argument, argumentQuoted ) ||
				( !argumentQuoted && ( argument[ 0 ] == '{' || argument[ 0 ] == '}' ) ) ) {
				ok = MapDbFail( report, statementLine, "the statement needs a value", word );
				break;
			}

			if ( MapDbEquals( word, "players" ) ) {
				int minimum = 0;
				int maximum = 0;
				bool secondQuoted = false;
				if ( !reader.Read( second, secondQuoted ) ||
					!MapDbParseInteger( argument, 0, MP_MAPDB_MAX_PLAYERS, minimum ) ||
					!MapDbParseInteger( second, 0, MP_MAPDB_MAX_PLAYERS, maximum ) ||
					( maximum != 0 && minimum > maximum ) ) {
					ok = MapDbFail( report, statementLine, "players needs a minimum and a maximum from 0 to 64, 0 for no bound" );
					break;
				}
				for ( int index = first; index <= last && index >= 0; ++index ) {
					draft->SetBounds( index, minimum, maximum );
				}
			} else if ( MapDbEquals( word, "rotation" ) || MapDbEquals( word, "vote" ) || MapDbEquals( word, "queue" ) ) {
				int enabled = 0;
				if ( !MapDbParseInteger( argument, 0, 1, enabled ) ) {
					ok = MapDbFail( report, statementLine, "the value is 0 or 1", word );
					break;
				}
				const int flag = MapDbEquals( word, "rotation" ) ? MP_MAPDB_ROTATION :
					( MapDbEquals( word, "vote" ) ? MP_MAPDB_VOTE : MP_MAPDB_QUEUE );
				for ( int index = first; index <= last && index >= 0; ++index ) {
					int &flags = draft->entries[ index ].flags;
					flags = enabled ? ( flags | flag ) : ( flags & ~flag );
				}
			} else if ( MapDbEquals( word, "modes" ) && isMap ) {
				uint32_t modes = 0;
				int start = 0;
				for ( int cursor = 0; ; ++cursor ) {
					if ( argument[ cursor ] != ',' && argument[ cursor ] != '\0' ) {
						continue;
					}
					int nameStart = start;
					int nameEnd = cursor;
					while ( nameStart < nameEnd && argument[ nameStart ] == ' ' ) {
						++nameStart;
					}
					while ( nameEnd > nameStart && argument[ nameEnd - 1 ] == ' ' ) {
						--nameEnd;
					}
					char modeName[ MAPDB_WORD_BYTES ];
					memcpy( modeName, argument + nameStart, static_cast<size_t>( nameEnd - nameStart ) );
					modeName[ nameEnd - nameStart ] = '\0';
					const int gameType = ( resolveMode != NULL && modeName[ 0 ] != '\0' ) ? resolveMode( modeName ) : -1;
					if ( gameType < 0 || gameType >= 32 ) {
						ok = MapDbFail( report, statementLine, "unknown gametype", modeName );
						break;
					}
					modes |= 1u << gameType;
					start = cursor + 1;
					if ( argument[ cursor ] == '\0' ) {
						break;
					}
				}
				if ( !ok ) {
					break;
				}
				if ( target >= 0 ) {
					draft->entries[ target ].modes = modes;
				}
			} else if ( MapDbEquals( word, "name" ) && isMap ) {
				if ( argument[ 0 ] == '\0' || MapDbLength( argument, MAPDB_WORD_BYTES ) >= MP_MAPDB_NAME_BYTES ) {
					ok = MapDbFail( report, statementLine, "the name is empty or longer than 63 bytes" );
					break;
				}
				if ( target >= 0 ) {
					MapDbCopy( draft->entries[ target ].name, MP_MAPDB_NAME_BYTES, argument );
				}
			} else if ( MapDbEquals( word, "set" ) && isMap ) {
				bool secondQuoted = false;
				if ( !reader.Read( second, secondQuoted ) ||
					( !secondQuoted && ( second[ 0 ] == '{' || second[ 0 ] == '}' ) ) ) {
					ok = MapDbFail( report, statementLine, "set needs a setting and a value" );
					break;
				}
				if ( MapDbLength( argument, MAPDB_WORD_BYTES ) >= MP_MAPDB_SETTING_KEY_BYTES ||
					MapDbLength( second, MAPDB_WORD_BYTES ) >= MP_MAPDB_SETTING_VALUE_BYTES ) {
					ok = MapDbFail( report, statementLine, "the setting or its value is too long", argument );
					break;
				}
				if ( target >= 0 && !draft->AddSetting( target, argument, second ) ) {
					++report.settingsDropped;
				}
			} else {
				ok = MapDbFail( report, statementLine, "unknown statement", word );
				break;
			}
		}
	}

	if ( ok && reader.Failed() ) {
		ok = MapDbFail( report, reader.Line(), "an unterminated string, an unclosed comment or an overlong word" );
	}
	if ( ok ) {
		const uint32_t nextRevision = revision + 1;
		*this = *draft;
		revision = nextRevision;
	} else {
		report.mapsChanged = 0;
		report.rotationListed = 0;
	}
	delete draft;
	return ok;
}

/*
===============================================================================

	Rotation

===============================================================================
*/

void mpMapDatabase::ClearRotation( void ) {
	rotationCount = 0;
	rotationExplicit = false;
	++revision;
}

/*
================
mpMapDatabase::AppendRotation

A map takes one place: a list that names it twice used to send the walk back
to its first place every time, so the maps after the second were never played.
================
*/
bool mpMapDatabase::AppendRotation( const char *path ) {
	const int index = Find( path );
	if ( index < 0 || rotationCount >= MP_MAPDB_MAX_MAPS ) {
		return false;
	}
	for ( int position = 0; position < rotationCount; ++position ) {
		if ( rotation[ position ] == index ) {
			return false;
		}
	}
	rotation[ rotationCount++ ] = index;
	rotationExplicit = true;
	++revision;
	return true;
}

int mpMapDatabase::SetRotationFromList( const char *list, int *unknown ) {
	ClearRotation();
	if ( unknown != NULL ) {
		*unknown = 0;
	}
	if ( list == NULL ) {
		return 0;
	}
	int placed = 0;
	char word[ MP_MAPDB_TOKEN_BYTES + 16 ];
	int written = 0;
	bool overlong = false;
	bool any = false;
	for ( const char *cursor = list; ; ++cursor ) {
		const char value = *cursor;
		const bool separator = value == ';' || value == ' ' || value == '\t' || value == '\r' ||
			value == '\n' || value == '"' || value == '\0';
		if ( !separator ) {
			if ( written < static_cast<int>( sizeof( word ) ) - 1 ) {
				word[ written++ ] = value;
			} else {
				overlong = true;
			}
		} else {
			if ( written > 0 ) {
				any = true;
				word[ written ] = '\0';
				// a map that is not installed and a map named twice are both left out
				if ( !overlong && AppendRotation( word ) ) {
					++placed;
				} else if ( unknown != NULL ) {
					++( *unknown );
				}
			}
			written = 0;
			overlong = false;
		}
		if ( value == '\0' ) {
			break;
		}
	}
	// a list of nothing but unknown maps is still an explicit, empty rotation
	rotationExplicit = any;
	return placed;
}

int mpMapDatabase::RotationSlots( void ) const {
	return rotationExplicit ? rotationCount : count;
}

int mpMapDatabase::RotationSlot( int position ) const {
	return rotationExplicit ? rotation[ position ] : position;
}

int mpMapDatabase::RotationNum( void ) const {
	if ( rotationExplicit ) {
		return rotationCount;
	}
	int listed = 0;
	for ( int index = 0; index < count; ++index ) {
		if ( ( entries[ index ].flags & MP_MAPDB_ROTATION ) != 0 && entries[ index ].modes != 0 ) {
			++listed;
		}
	}
	return listed;
}

int mpMapDatabase::RotationEntry( int position ) const {
	if ( position < 0 ) {
		return -1;
	}
	if ( rotationExplicit ) {
		return position < rotationCount ? rotation[ position ] : -1;
	}
	for ( int index = 0; index < count; ++index ) {
		if ( ( entries[ index ].flags & MP_MAPDB_ROTATION ) != 0 && entries[ index ].modes != 0 ) {
			if ( position-- == 0 ) {
				return index;
			}
		}
	}
	return -1;
}

int mpMapDatabase::RotationPosition( int index ) const {
	const int slots = RotationSlots();
	for ( int position = 0; position < slots; ++position ) {
		if ( RotationSlot( position ) == index ) {
			return position;
		}
	}
	return -1;
}

// An explicit list is the operator's word; the rotation flag only governs the
// implicit rotation over every map.
bool mpMapDatabase::RotationEligible( int index, int gameType ) const {
	if ( !Supports( index, gameType ) ) {
		return false;
	}
	return rotationExplicit || ( entries[ index ].flags & MP_MAPDB_ROTATION ) != 0;
}

/*
================
mpMapDatabase::NextInRotation

The walk starts after the current map and wraps.  It takes the first map that
supports the gametype and fits the player count; the current map itself if it
is the only one that fits; then the same two steps without the bounds, so a
rotation whose bounds exclude everything still turns.
================
*/
mpMapSelection mpMapDatabase::NextInRotation( int currentIndex, int gameType, int playerCount ) const {
	mpMapSelection selection;
	selection.index = -1;
	selection.source = MP_MAPDB_SOURCE_NONE;
	selection.owner = -1;
	selection.votes = 0;

	const int slots = RotationSlots();
	const int start = RotationPosition( currentIndex );
	const bool currentEligible = start >= 0 && RotationEligible( currentIndex, gameType );
	for ( int pass = 0; pass < 2; ++pass ) {
		const bool bounded = pass == 0;
		for ( int step = 1; step <= slots; ++step ) {
			const int position = ( start + step ) % slots;
			const int index = RotationSlot( position );
			if ( index == currentIndex || !RotationEligible( index, gameType ) ) {
				continue;
			}
			if ( bounded && !FitsPlayers( index, playerCount ) ) {
				continue;
			}
			selection.index = index;
			selection.source = bounded ? MP_MAPDB_SOURCE_ROTATION : MP_MAPDB_SOURCE_ROTATION_UNBOUNDED;
			return selection;
		}
		if ( currentEligible && ( !bounded || FitsPlayers( currentIndex, playerCount ) ) ) {
			selection.index = currentIndex;
			selection.source = bounded ? MP_MAPDB_SOURCE_ROTATION : MP_MAPDB_SOURCE_ROTATION_UNBOUNDED;
			return selection;
		}
	}
	return selection;
}

/*
================
mpMapDatabase::RandomFromRotation

Draws among the rotation's maps that fit, leaving out the current map and the
recently played ones; the recent maps come back before the bounds give way.
================
*/
mpMapSelection mpMapDatabase::RandomFromRotation( int currentIndex, int gameType, int playerCount, uint32_t seed ) const {
	mpMapSelection selection;
	selection.index = -1;
	selection.source = MP_MAPDB_SOURCE_NONE;
	selection.owner = -1;
	selection.votes = 0;

	const int slots = RotationSlots();
	int candidates[ MP_MAPDB_MAX_MAPS ];
	for ( int pass = 0; pass < 4; ++pass ) {
		const bool bounded = pass < 2;
		const bool avoidRecent = ( pass & 1 ) == 0;
		int found = 0;
		for ( int position = 0; position < slots; ++position ) {
			const int index = RotationSlot( position );
			if ( index == currentIndex || !RotationEligible( index, gameType ) ) {
				continue;
			}
			if ( ( bounded && !FitsPlayers( index, playerCount ) ) || ( avoidRecent && RecentlyPlayed( index ) ) ) {
				continue;
			}
			candidates[ found++ ] = index;
		}
		if ( found > 0 ) {
			uint32_t state = seed;
			selection.index = candidates[ MapDbNextRandom( state ) % static_cast<uint32_t>( found ) ];
			selection.source = bounded ? MP_MAPDB_SOURCE_RANDOM : MP_MAPDB_SOURCE_RANDOM_UNBOUNDED;
			return selection;
		}
	}
	if ( RotationPosition( currentIndex ) >= 0 && RotationEligible( currentIndex, gameType ) ) {
		selection.index = currentIndex;
		selection.source = FitsPlayers( currentIndex, playerCount ) ? MP_MAPDB_SOURCE_RANDOM : MP_MAPDB_SOURCE_RANDOM_UNBOUNDED;
	}
	return selection;
}

/*
===============================================================================

	Player queue

===============================================================================
*/

mpMapQueueResult_t mpMapDatabase::QueueAdd( int owner, uint64_t ownerId, int mapIndex, int gameType, int currentIndex ) {
	if ( owner < 0 || owner >= MP_MAPDB_MAX_VOTERS ) {
		return MP_MAPDB_QUEUE_BAD_OWNER;
	}
	if ( mapIndex < 0 || mapIndex >= count ) {
		return MP_MAPDB_QUEUE_UNKNOWN_MAP;
	}
	if ( ( entries[ mapIndex ].flags & MP_MAPDB_QUEUE ) == 0 ) {
		return MP_MAPDB_QUEUE_NOT_QUEUEABLE;
	}
	if ( !Supports( mapIndex, gameType ) ) {
		return MP_MAPDB_QUEUE_WRONG_MODE;
	}
	if ( mapIndex == currentIndex ) {
		return MP_MAPDB_QUEUE_CURRENT_MAP;
	}
	int own = -1;
	for ( int position = 0; position < queueCount; ++position ) {
		if ( queue[ position ].owner == owner ) {
			own = position;
		} else if ( queue[ position ].mapIndex == mapIndex ) {
			return MP_MAPDB_QUEUE_DUPLICATE;
		}
	}
	if ( own >= 0 ) {
		queue[ own ].mapIndex = mapIndex;
		queue[ own ].ownerId = ownerId;
		return MP_MAPDB_QUEUE_REPLACED;
	}
	if ( queueCount >= MP_MAPDB_MAX_QUEUE ) {
		return MP_MAPDB_QUEUE_FULL;
	}
	queue[ queueCount ].mapIndex = mapIndex;
	queue[ queueCount ].owner = owner;
	queue[ queueCount ].ownerId = ownerId;
	++queueCount;
	return MP_MAPDB_QUEUE_ADDED;
}

bool mpMapDatabase::QueueRemoveOwner( int owner ) {
	for ( int position = 0; position < queueCount; ++position ) {
		if ( queue[ position ].owner == owner ) {
			for ( int next = position + 1; next < queueCount; ++next ) {
				queue[ next - 1 ] = queue[ next ];
			}
			--queueCount;
			memset( &queue[ queueCount ], 0, sizeof( queue[ queueCount ] ) );
			return true;
		}
	}
	return false;
}

int mpMapDatabase::QueuePrune( bool ( *isCurrent )( int owner, uint64_t ownerId ) ) {
	if ( isCurrent == NULL ) {
		return 0;
	}
	int removed = 0;
	for ( int position = queueCount - 1; position >= 0; --position ) {
		if ( !isCurrent( queue[ position ].owner, queue[ position ].ownerId ) ) {
			QueueRemoveOwner( queue[ position ].owner );
			++removed;
		}
	}
	return removed;
}

void mpMapDatabase::QueueClear( void ) {
	memset( queue, 0, sizeof( queue ) );
	queueCount = 0;
}

const mpMapQueueEntry *mpMapDatabase::QueueEntry( int position ) const {
	return ( position >= 0 && position < queueCount ) ? &queue[ position ] : NULL;
}

int mpMapDatabase::QueueFindOwner( int owner ) const {
	for ( int position = 0; position < queueCount; ++position ) {
		if ( queue[ position ].owner == owner ) {
			return position;
		}
	}
	return -1;
}

/*
===============================================================================

	End-of-match pick

===============================================================================
*/

void mpMapDatabase::PickClose( void ) {
	memset( &pick, 0, sizeof( pick ) );
	for ( int voter = 0; voter < MP_MAPDB_MAX_VOTERS; ++voter ) {
		pick.choice[ voter ] = -1;
	}
	for ( int candidate = 0; candidate < MP_MAPDB_PICK_CANDIDATES; ++candidate ) {
		pick.candidates[ candidate ] = -1;
	}
}

/*
================
mpMapDatabase::PickOpen

False, with the pick left closed, when fewer than two maps can be offered:
one candidate is no choice.
================
*/
bool mpMapDatabase::PickOpen( int currentIndex, int gameType, int playerCount, uint32_t seed ) {
	PickClose();
	const mpMapSelection first = NextInRotation( currentIndex, gameType, playerCount );
	if ( first.index < 0 || first.index == currentIndex ) {
		return false;
	}
	int chosen[ MP_MAPDB_PICK_CANDIDATES ];
	int chosenCount = 0;
	chosen[ chosenCount++ ] = first.index;

	const int slots = RotationSlots();
	uint32_t state = seed;
	// While the rotation has a map that fits the player count, only maps that
	// fit are offered beside it, fresh ones first: a pick must not put up what
	// the rotation would pass over.  When nothing fits, the rotation ignores
	// the bounds and so does the offer.
	const int firstPass = first.source == MP_MAPDB_SOURCE_ROTATION ? 0 : 2;
	for ( int pass = firstPass; pass < firstPass + 2 && chosenCount < MP_MAPDB_PICK_CANDIDATES; ++pass ) {
		const bool bounded = pass < 2;
		const bool avoidRecent = ( pass & 1 ) == 0;
		int pool[ MP_MAPDB_MAX_MAPS ];
		int found = 0;
		for ( int position = 0; position < slots; ++position ) {
			const int index = RotationSlot( position );
			if ( index == currentIndex || !RotationEligible( index, gameType ) ||
				( entries[ index ].flags & MP_MAPDB_VOTE ) == 0 ) {
				continue;
			}
			if ( ( bounded && !FitsPlayers( index, playerCount ) ) || ( avoidRecent && RecentlyPlayed( index ) ) ) {
				continue;
			}
			bool taken = false;
			for ( int candidate = 0; candidate < chosenCount; ++candidate ) {
				taken |= chosen[ candidate ] == index;
			}
			if ( !taken ) {
				pool[ found++ ] = index;
			}
		}
		while ( found > 0 && chosenCount < MP_MAPDB_PICK_CANDIDATES ) {
			const int draw = static_cast<int>( MapDbNextRandom( state ) % static_cast<uint32_t>( found ) );
			chosen[ chosenCount++ ] = pool[ draw ];
			pool[ draw ] = pool[ --found ];
		}
	}
	if ( chosenCount < 2 ) {
		return false;
	}
	pick.open = true;
	pick.candidateCount = chosenCount;
	for ( int candidate = 0; candidate < chosenCount; ++candidate ) {
		pick.candidates[ candidate ] = chosen[ candidate ];
	}
	return true;
}

bool mpMapDatabase::PickVote( int voter, int candidate ) {
	if ( !pick.open || voter < 0 || voter >= MP_MAPDB_MAX_VOTERS ||
		candidate < 0 || candidate >= pick.candidateCount ) {
		return false;
	}
	PickRemoveVoter( voter );
	pick.choice[ voter ] = candidate;
	++pick.votes[ candidate ];
	return true;
}

void mpMapDatabase::PickRemoveVoter( int voter ) {
	if ( voter < 0 || voter >= MP_MAPDB_MAX_VOTERS || pick.choice[ voter ] < 0 ) {
		return;
	}
	--pick.votes[ pick.choice[ voter ] ];
	pick.choice[ voter ] = -1;
}

int mpMapDatabase::PickLeader( uint32_t seed, int *votes ) const {
	if ( votes != NULL ) {
		*votes = 0;
	}
	if ( !pick.open || pick.candidateCount <= 0 ) {
		return -1;
	}
	int best = 0;
	for ( int candidate = 0; candidate < pick.candidateCount; ++candidate ) {
		if ( pick.votes[ candidate ] > best ) {
			best = pick.votes[ candidate ];
		}
	}
	if ( best == 0 ) {
		return pick.candidates[ 0 ];
	}
	int leaders[ MP_MAPDB_PICK_CANDIDATES ];
	int leaderCount = 0;
	for ( int candidate = 0; candidate < pick.candidateCount; ++candidate ) {
		if ( pick.votes[ candidate ] == best ) {
			leaders[ leaderCount++ ] = candidate;
		}
	}
	uint32_t state = seed;
	const int winner = leaders[ leaderCount > 1 ? MapDbNextRandom( state ) % static_cast<uint32_t>( leaderCount ) : 0 ];
	if ( votes != NULL ) {
		*votes = best;
	}
	return pick.candidates[ winner ];
}

/*
================
mpMapDatabase::ChooseNext
================
*/
mpMapSelection mpMapDatabase::ChooseNext( int currentIndex, int gameType, int playerCount, bool shuffle,
		uint32_t seed, bool consume ) {
	mpMapSelection selection;
	selection.index = -1;
	selection.source = MP_MAPDB_SOURCE_NONE;
	selection.owner = -1;
	selection.votes = 0;

	// A queued map the gametype can no longer play (the gametype changed since)
	// or that is now the current map is passed over, and dropped when consuming.
	for ( int position = 0; position < queueCount; ) {
		const mpMapQueueEntry entry = queue[ position ];
		if ( Supports( entry.mapIndex, gameType ) && entry.mapIndex != currentIndex ) {
			selection.index = entry.mapIndex;
			selection.source = MP_MAPDB_SOURCE_QUEUE;
			selection.owner = entry.owner;
			if ( consume ) {
				QueueRemoveOwner( entry.owner );
				PickClose();
			}
			return selection;
		}
		if ( consume ) {
			QueueRemoveOwner( entry.owner );
		} else {
			++position;
		}
	}

	if ( pick.open ) {
		int votes = 0;
		const int leader = PickLeader( seed, &votes );
		if ( consume ) {
			PickClose();
		}
		if ( leader >= 0 && Supports( leader, gameType ) ) {
			selection.index = leader;
			selection.source = MP_MAPDB_SOURCE_PICK;
			selection.votes = votes;
			return selection;
		}
	}

	return shuffle ? RandomFromRotation( currentIndex, gameType, playerCount, seed ) :
		NextInRotation( currentIndex, gameType, playerCount );
}

/*
===============================================================================

	History

===============================================================================
*/

void mpMapDatabase::NotePlayed( int index ) {
	if ( index < 0 || index >= count ) {
		return;
	}
	int existing = -1;
	for ( int position = 0; position < historyCount; ++position ) {
		if ( history[ position ] == index ) {
			existing = position;
			break;
		}
	}
	int last = existing >= 0 ? existing : ( historyCount < MP_MAPDB_HISTORY ? historyCount++ : MP_MAPDB_HISTORY - 1 );
	for ( ; last > 0; --last ) {
		history[ last ] = history[ last - 1 ];
	}
	history[ 0 ] = index;
}

/*
================
mpMapDatabase::RecentlyPlayed

A small rotation must not rule itself out: only as many recent maps count as
leave half of the rotation fresh. The window was half of every installed map,
so a four-map si_mapCycle on a full install called all of its maps recent and
the shuffle fell back to "anything but this map": A, B, A, B.
================
*/
bool mpMapDatabase::RecentlyPlayed( int index ) const {
	const int pool = rotationExplicit ? rotationCount : count;
	const int considered = ( pool / 2 < historyCount ) ? pool / 2 : historyCount;
	for ( int position = 0; position < considered; ++position ) {
		if ( history[ position ] == index ) {
			return true;
		}
	}
	return false;
}

int mpMapDatabase::HistoryEntry( int position ) const {
	return ( position >= 0 && position < historyCount ) ? history[ position ] : -1;
}

/*
================
mpMapDatabase::ValidateInvariants
================
*/
bool mpMapDatabase::ValidateInvariants( void ) const {
	if ( count < 0 || count > MP_MAPDB_MAX_MAPS || rotationCount < 0 || rotationCount > MP_MAPDB_MAX_MAPS ||
		queueCount < 0 || queueCount > MP_MAPDB_MAX_QUEUE || historyCount < 0 || historyCount > MP_MAPDB_HISTORY ) {
		return false;
	}
	bool settingSeen[ MP_MAPDB_MAX_SETTINGS ];
	memset( settingSeen, 0, sizeof( settingSeen ) );
	for ( int index = 0; index < count; ++index ) {
		const mpMapEntry &entry = entries[ index ];
		if ( !IsSafeToken( entry.token ) || entry.name[ 0 ] == '\0' ||
			entry.token[ MP_MAPDB_TOKEN_BYTES - 1 ] != '\0' || entry.name[ MP_MAPDB_NAME_BYTES - 1 ] != '\0' ) {
			return false;
		}
		if ( entry.minPlayers < 0 || entry.maxPlayers < 0 || entry.minPlayers > MP_MAPDB_MAX_PLAYERS ||
			entry.maxPlayers > MP_MAPDB_MAX_PLAYERS || ( entry.maxPlayers != 0 && entry.minPlayers > entry.maxPlayers ) ) {
			return false;
		}
		for ( int other = 0; other < index; ++other ) {
			if ( MapDbEquals( entries[ other ].token, entry.token ) ) {
				return false;
			}
		}
		if ( entry.settingCount < 0 || entry.settingCount > MP_MAPDB_MAX_MAP_SETTINGS ) {
			return false;
		}
		for ( int setting = 0; setting < entry.settingCount; ++setting ) {
			const int slot = entry.settings[ setting ];
			if ( slot < 0 || slot >= MP_MAPDB_MAX_SETTINGS || !settingUsed[ slot ] || settingSeen[ slot ] ||
				settingPool[ slot ].key[ 0 ] == '\0' ) {
				return false;
			}
			settingSeen[ slot ] = true;
		}
	}
	for ( int slot = 0; slot < MP_MAPDB_MAX_SETTINGS; ++slot ) {
		if ( settingUsed[ slot ] != settingSeen[ slot ] ) {
			return false;
		}
	}
	for ( int position = 0; position < rotationCount; ++position ) {
		if ( rotation[ position ] < 0 || rotation[ position ] >= count ) {
			return false;
		}
		for ( int other = 0; other < position; ++other ) {
			if ( rotation[ other ] == rotation[ position ] ) {
				return false;
			}
		}
	}
	if ( !rotationExplicit && rotationCount != 0 ) {
		return false;
	}
	for ( int position = 0; position < queueCount; ++position ) {
		if ( queue[ position ].mapIndex < 0 || queue[ position ].mapIndex >= count ||
			queue[ position ].owner < 0 || queue[ position ].owner >= MP_MAPDB_MAX_VOTERS ) {
			return false;
		}
		for ( int other = 0; other < position; ++other ) {
			if ( queue[ other ].owner == queue[ position ].owner ||
				queue[ other ].mapIndex == queue[ position ].mapIndex ) {
				return false;
			}
		}
	}
	for ( int position = 0; position < historyCount; ++position ) {
		if ( history[ position ] < 0 || history[ position ] >= count ) {
			return false;
		}
	}
	if ( pick.candidateCount < 0 || pick.candidateCount > MP_MAPDB_PICK_CANDIDATES ||
		( pick.open && pick.candidateCount < 2 ) || ( !pick.open && pick.candidateCount != 0 ) ) {
		return false;
	}
	int tallies[ MP_MAPDB_PICK_CANDIDATES ];
	memset( tallies, 0, sizeof( tallies ) );
	for ( int voter = 0; voter < MP_MAPDB_MAX_VOTERS; ++voter ) {
		if ( pick.choice[ voter ] < -1 || pick.choice[ voter ] >= pick.candidateCount ) {
			return false;
		}
		if ( pick.choice[ voter ] >= 0 ) {
			++tallies[ pick.choice[ voter ] ];
		}
	}
	for ( int candidate = 0; candidate < MP_MAPDB_PICK_CANDIDATES; ++candidate ) {
		if ( tallies[ candidate ] != pick.votes[ candidate ] ) {
			return false;
		}
		if ( candidate < pick.candidateCount ) {
			if ( pick.candidates[ candidate ] < 0 || pick.candidates[ candidate ] >= count ) {
				return false;
			}
			for ( int other = 0; other < candidate; ++other ) {
				if ( pick.candidates[ other ] == pick.candidates[ candidate ] ) {
					return false;
				}
			}
		}
	}
	return true;
}
