//----------------------------------------------------------------
// MapDatabaseHost.cpp
//
// openQ4: the game's adapter around mpMapDatabase.  See MapDatabaseHost.h.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "GameTypes.h"
#include "MapDatabaseHost.h"

mpMapDatabaseHost	mpMapHost;

idCVar g_mapDatabase(	"g_mapDatabase",	"mapdb.txt",	CVAR_GAME | CVAR_ARCHIVE,
	"the server's map database overrides: player bounds, gametypes, rotation and settings for each map - see the server setup guide" );
idCVar g_mapRotation(	"g_mapRotation",	"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_INTEGER,
	"what follows a finished match: 0 = the same map, 1 = the next map of the rotation that fits the player count, 2 = a shuffled rotation, 3 = the legacy g_mapCycle script",
	0, MP_MAP_ROTATION_COUNT - 1, idCmdSystem::ArgCompletion_Integer<0, MP_MAP_ROTATION_COUNT - 1> );
idCVar g_mapQueue(		"g_mapQueue",		"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL,
	"players may queue one map each with mymap; queued maps are played before the rotation continues" );
idCVar g_mapPick(		"g_mapPick",		"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL,
	"offer three maps at the end of a match; players choose with mappick and the most picked is played next" );
idCVar si_nextMap(		"si_nextMap",		"",				CVAR_GAME | CVAR_SERVERINFO | CVAR_ROM,
	"the map the server would load next, published by the map database" );
idCVar si_mapPick(		"si_mapPick",		"",				CVAR_GAME | CVAR_SERVERINFO | CVAR_ROM,
	"the maps of the open end-of-match pick, semicolon delimited" );

// Localized replies.  The map or the list follows the text.
static const char *const MAPDB_STR_QUEUED			= "#str_232100";	// "Queued for the next map change: "
static const char *const MAPDB_STR_QUEUE_CLEARED	= "#str_232101";	// "Your queued map was removed."
static const char *const MAPDB_STR_QUEUE_NONE		= "#str_232102";	// "You have no map in the queue."
static const char *const MAPDB_STR_UNKNOWN_MAP		= "#str_232103";	// "This server does not have the map: "
static const char *const MAPDB_STR_WRONG_MODE		= "#str_232104";	// "The current game type cannot be played on: "
static const char *const MAPDB_STR_CURRENT_MAP		= "#str_232105";	// "That map is being played now: "
static const char *const MAPDB_STR_DUPLICATE		= "#str_232106";	// "Another player has already queued: "
static const char *const MAPDB_STR_QUEUE_OFF		= "#str_232107";	// "This server does not take map requests."
static const char *const MAPDB_STR_QUEUE_FULL		= "#str_232108";	// "The map queue is full."
static const char *const MAPDB_STR_QUEUE_LIST		= "#str_232109";	// "Map queue: "
static const char *const MAPDB_STR_QUEUE_EMPTY		= "#str_232110";	// "The map queue is empty."
static const char *const MAPDB_STR_NEXT_MAP			= "#str_232111";	// "Next map: "
static const char *const MAPDB_STR_PICK_OFFER		= "#str_232112";	// "Choose the next map with mappick 1, 2 or 3: "
static const char *const MAPDB_STR_PICK_TAKEN		= "#str_232113";	// "You picked: "
static const char *const MAPDB_STR_PICK_CLOSED		= "#str_232114";	// "There is no map pick to vote in."
static const char *const MAPDB_STR_NOT_QUEUEABLE	= "#str_232115";	// "This server keeps that map out of the queue: "

static const int MAPDB_THINK_MSEC = 1000;
// game frames ServerAdvance answers "already changed" for one issued map (about half a second)
static const int MAPDB_ADVANCE_REPEAT_FRAMES = 32;

/*
================
MPMapDecl
================
*/
const idDict *MPMapDecl( int declIndex ) {
	if ( declIndex < 0 ) {
		return NULL;
	}
	return fileSystem->GetMapDecl( declIndex );
}

/*
================
MapDbResolveMode

A gametype of the override file, by its si_gameType name or its short tag.
================
*/
static int MapDbResolveMode( const char *name ) {
	const mpGameTypeInfo_t *info = MPGameTypeByName( name );
	if ( info == NULL ) {
		for ( int type = GAME_SP + 1; type < NUM_GAME_TYPES; type++ ) {
			if ( idStr::Icmp( MPGameType( type )->abbrev, name ) == 0 ) {
				info = MPGameType( type );
				break;
			}
		}
	}
	if ( info == NULL || !MPGameTypeIsSelectable( info->type ) ) {
		return -1;
	}
	return info->type;
}

/*
================
MapDbReadOverrideFile

The operator's own file on disk comes first, read by its real path: a pure
server serves only pack files once a map is up, and mapdb check and mapdb
reload must still see the file that was just edited.  A file shipped inside a
pack is the fallback.
================
*/
/*
================
MapDbBlankByteOrderMark

Windows editors save UTF-8 with a byte order mark. The parser met it as an
unknown word on line 1 and refused the whole file, with a message that named
nothing a person could see in the file.
================
*/
static void MapDbBlankByteOrderMark( idStr &text ) {
	if ( text.Length() >= 3 && static_cast<unsigned char>( text[ 0 ] ) == 0xEF &&
		static_cast<unsigned char>( text[ 1 ] ) == 0xBB && static_cast<unsigned char>( text[ 2 ] ) == 0xBF ) {
		text[ 0 ] = text[ 1 ] = text[ 2 ] = ' ';
	}
}

static bool MapDbReadOverrideFile( const char *fileName, idStr &text ) {
	static const char *const roots[] = { "fs_savepath", "fs_devpath", "fs_basepath", "fs_cdpath" };
	text.Clear();
	for ( int root = 0; root < 4; root++ ) {
		if ( cvarSystem->GetCVarString( roots[ root ] )[ 0 ] == '\0' ) {
			continue;
		}
		idFile *file = fileSystem->OpenExplicitFileRead( fileSystem->RelativePathToOSPath( fileName, roots[ root ] ) );
		if ( file == NULL ) {
			continue;
		}
		const int length = file->Length();
		bool read = length >= 0 && length <= MP_MAPDB_MAX_TEXT_BYTES;
		if ( read && length > 0 ) {
			text.Fill( ' ', length );
			read = file->Read( &text[ 0 ], length ) == length;
		}
		fileSystem->CloseFile( file );
		if ( !read ) {
			gameLocal.Warning( "map database: %s cannot be read or is larger than %d bytes", fileName, MP_MAPDB_MAX_TEXT_BYTES );
			text.Clear();
			return false;
		}
		MapDbBlankByteOrderMark( text );
		return true;
	}
	void *buffer = NULL;
	const int length = fileSystem->ReadFile( fileName, &buffer );
	if ( length < 0 || buffer == NULL ) {
		return false;
	}
	if ( length <= MP_MAPDB_MAX_TEXT_BYTES ) {
		text.Append( static_cast<const char *>( buffer ), length );
		MapDbBlankByteOrderMark( text );
	}
	fileSystem->FreeFile( buffer );
	return length <= MP_MAPDB_MAX_TEXT_BYTES;
}

static bool MapDbRotationSource( mpMapSource_t source ) {
	return source == MP_MAPDB_SOURCE_ROTATION || source == MP_MAPDB_SOURCE_ROTATION_UNBOUNDED ||
		source == MP_MAPDB_SOURCE_RANDOM || source == MP_MAPDB_SOURCE_RANDOM_UNBOUNDED;
}

static const char *MapDbSourceName( mpMapSource_t source ) {
	switch ( source ) {
		case MP_MAPDB_SOURCE_QUEUE:					return "queued";
		case MP_MAPDB_SOURCE_PICK:					return "picked";
		case MP_MAPDB_SOURCE_ROTATION:				return "rotation";
		case MP_MAPDB_SOURCE_ROTATION_UNBOUNDED:	return "rotation, no map fits the player count";
		case MP_MAPDB_SOURCE_RANDOM:				return "shuffled rotation";
		case MP_MAPDB_SOURCE_RANDOM_UNBOUNDED:		return "shuffled rotation, no map fits the player count";
		default:									return "none";
	}
}

/*
===============================================================================

	mpMapDatabaseHost

===============================================================================
*/

mpMapDatabaseHost::mpMapDatabaseHost( void ) {
	built = false;
	discoveredDecls = 0;
	rotationFromFile = false;
	mapChangePending = false;
	hostingNext = false;
	mapSeed = 1;
	nextThinkTime = 0;
	advanceAttempts = 0;
}

const mpMapDatabase &mpMapDatabaseHost::Database( void ) {
	Refresh();
	return database;
}

void mpMapDatabaseHost::Refresh( void ) {
	if ( !built ) {
		Rebuild();
	} else {
		SyncRotation( false );
	}
}

/*
================
mpMapDatabaseHost::Rebuild
================
*/
void mpMapDatabaseHost::Rebuild( void ) {
	// indices do not survive a rebuild; map paths do
	idStrList queuedMaps;
	idList<int> queuedOwners;
	for ( int position = 0; position < database.QueueNum(); position++ ) {
		const mpMapQueueEntry *queued = database.QueueEntry( position );
		queuedMaps.Append( database.Entry( queued->mapIndex )->token );
		queuedOwners.Append( queued->owner );
	}
	idStrList played;
	for ( int position = 0; position < database.HistoryNum(); position++ ) {
		played.Append( database.Entry( database.HistoryEntry( position ) )->token );
	}

	Discover();
	LoadOverrides();
	SyncRotation( true );

	for ( int position = played.Num() - 1; position >= 0; position-- ) {
		database.NotePlayed( database.Find( played[ position ].c_str() ) );
	}
	const int current = CurrentEntry();
	for ( int position = 0; position < queuedMaps.Num(); position++ ) {
		database.QueueAdd( queuedOwners[ position ], 0, database.Find( queuedMaps[ position ].c_str() ),
			gameLocal.gameType, current );
	}
	built = true;
}

/*
================
mpMapDatabaseHost::Discover

A multiplayer map is a mapDef that advertises a gametype this build can
start.  Nothing lists the maps: a pk4 that brings a mapDef brings the map.
================
*/
void mpMapDatabaseHost::Discover( void ) {
	database.Clear();

	uint32_t freeForAll = 0;
	for ( int type = GAME_SP + 1; type < NUM_GAME_TYPES; type++ ) {
		if ( MPGameTypeIsSelectable( type ) && !MPGameTypeHasAny( type, GTF_TEAM ) ) {
			freeForAll |= 1u << type;
		}
	}

	const int numMaps = fileSystem->GetNumMaps();
	int refused = 0;
	for ( int declIndex = 0; declIndex < numMaps; declIndex++ ) {
		const idDict *dict = MPMapDecl( declIndex );
		if ( dict == NULL ) {
			continue;
		}
		uint32_t modes = 0;
		for ( int type = GAME_SP + 1; type < NUM_GAME_TYPES; type++ ) {
			if ( MPGameTypeIsSelectable( type ) && MPMapSupportsGameType( dict, type ) ) {
				modes |= 1u << type;
			}
		}
		if ( modes == 0 ) {
			continue;
		}
		const char *path = dict->GetString( "path" );
		const int index = database.AddMap( path, dict->GetString( "name", path ), modes, declIndex );
		if ( index < 0 ) {
			// a path the database cannot hold (other characters than letters,
			// digits, '_', '-' and '/'), a second decl for one map, or map 257
			if ( refused++ == 0 ) {
				gameLocal.Warning( "map database: '%s' cannot be listed", path );
			}
			continue;
		}
		int minPlayers = 0;
		int maxPlayers = 0;
		mpMapDatabase::DefaultBounds( database.Entry( index )->token, modes, freeForAll, minPlayers, maxPlayers );
		// a map may state its own bounds
		if ( dict->FindKey( "minPlayers" ) != NULL ) {
			minPlayers = dict->GetInt( "minPlayers" );
		}
		if ( dict->FindKey( "maxPlayers" ) != NULL ) {
			maxPlayers = dict->GetInt( "maxPlayers" );
		}
		if ( !database.SetBounds( index, minPlayers, maxPlayers ) ) {
			gameLocal.Warning( "map database: '%s' declares player bounds %d to %d; ignored", path, minPlayers, maxPlayers );
		}
	}
	discoveredDecls = numMaps;
}

/*
================
mpMapDatabaseHost::LoadOverrides
================
*/
void mpMapDatabaseHost::LoadOverrides( void ) {
	rotationFromFile = false;
	overrideRotation.Clear();

	// the server owns the database; a client lists what it has installed and
	// asks the server for the rest
	if ( ClientRole() ) {
		return;
	}
	const char *fileName = g_mapDatabase.GetString();
	if ( fileName[ 0 ] == '\0' ) {
		return;
	}
	if ( idStr::FindText( fileName, ".." ) >= 0 || fileName[ 0 ] == '/' || fileName[ 0 ] == '\\' ||
		idStr::FindChar( fileName, ':' ) >= 0 ) {
		gameLocal.Warning( "map database: g_mapDatabase must name a file inside the game directory" );
		return;
	}
	idStr text;
	if ( !MapDbReadOverrideFile( fileName, text ) ) {
		return;
	}
	mpMapOverrideReport report;
	if ( !database.ApplyOverrides( text.c_str(), text.Length(), MapDbResolveMode, report ) ) {
		gameLocal.Warning( "map database: %s line %d: %s; the file was not applied", fileName, report.line, report.error );
	} else {
		gameLocal.Printf( "map database: %s changed %d maps%s\n", fileName, report.mapsChanged,
			database.HasExplicitRotation() ? va( " and lists a rotation of %d", report.rotationListed ) : "" );
		if ( report.mapsUnknown > 0 || report.rotationUnknown > 0 ) {
			gameLocal.Printf( "map database: %s names %d maps that are not installed\n", fileName,
				report.mapsUnknown + report.rotationUnknown );
		}
		if ( report.settingsDropped > 0 ) {
			gameLocal.Warning( "map database: %s has %d settings more than a map can hold (%d each)", fileName,
				report.settingsDropped, MP_MAPDB_MAX_MAP_SETTINGS );
		}
		rotationFromFile = database.HasExplicitRotation();
		for ( int position = 0; rotationFromFile && position < database.RotationNum(); position++ ) {
			overrideRotation += database.Entry( database.RotationEntry( position ) )->token;
			overrideRotation += ";";
		}
	}
}

/*
================
mpMapDatabaseHost::SyncRotation

si_mapCycle keeps its meaning as a rotation list and wins over the override
file's rotation block; a client reads the server's.
================
*/
void mpMapDatabaseHost::SyncRotation( bool force ) {
	const char *cycle = ClientRole() ? gameLocal.serverInfo.GetString( "si_mapCycle" ) : si_mapCycle.GetString();
	if ( !force && rotationList.Cmp( cycle ) == 0 ) {
		return;
	}
	rotationList = cycle;
	if ( rotationList.Length() > 0 ) {
		int unknown = 0;
		const int placed = database.SetRotationFromList( rotationList.c_str(), &unknown );
		if ( unknown > 0 && !ClientRole() ) {
			gameLocal.Warning( "map database: si_mapCycle names %d maps that are not installed or twice; %d maps rotate", unknown, placed );
		}
	} else if ( rotationFromFile ) {
		database.SetRotationFromList( overrideRotation.c_str() );
	} else {
		database.ClearRotation();
	}
}

void mpMapDatabaseHost::Shutdown( void ) {
	RevertSettings();
	built = false;
	mapChangePending = false;
	hostingNext = false;
	database.Clear();
}

int mpMapDatabaseHost::Find( const char *path ) {
	Refresh();
	return database.Find( path );
}

int mpMapDatabaseHost::FindDecl( int declIndex ) {
	Refresh();
	return database.FindHostIndex( declIndex );
}

const idDict *mpMapDatabaseHost::Decl( int entryIndex ) {
	Refresh();
	const mpMapEntry *entry = database.Entry( entryIndex );
	return entry != NULL ? MPMapDecl( entry->hostIndex ) : NULL;
}

const idDict *mpMapDatabaseHost::DeclForPath( const char *path ) {
	return Decl( Find( path ) );
}

const char *mpMapDatabaseHost::DisplayName( int entryIndex ) {
	Refresh();
	const mpMapEntry *entry = database.Entry( entryIndex );
	if ( entry == NULL ) {
		return "";
	}
	const char *name = common->GetLocalizedString( entry->name );
	return ( name != NULL && name[ 0 ] != '\0' ) ? name : entry->token;
}

bool mpMapDatabaseHost::Supports( const char *path, int gameType ) {
	const int index = Find( path );
	if ( index >= 0 ) {
		return database.Supports( index, gameType );
	}
	// a map outside the database: a single-player map, or one whose path it cannot hold
	const idDeclEntityDef *mapDef = static_cast<const idDeclEntityDef *>(
		declManager->FindType( DECL_MAPDEF, path != NULL ? path : "", false ) );
	return mapDef != NULL && MPMapSupportsGameType( &mapDef->dict, gameType );
}

void mpMapDatabaseHost::CollectDecls( int gameType, int requiredFlags, idList<int> &declIndices ) {
	Refresh();
	declIndices.Clear();
	int indices[ MP_MAPDB_MAX_MAPS ];
	const int found = database.Collect( gameType, requiredFlags, indices, MP_MAPDB_MAX_MAPS );
	for ( int position = 0; position < found; position++ ) {
		declIndices.Append( database.Entry( indices[ position ] )->hostIndex );
	}
}

/*
================
mpMapDatabaseHost::CurrentEntry

The loaded map file is the authority; si_map may already name the next map.
================
*/
int mpMapDatabaseHost::CurrentEntry( void ) {
	const char *loaded = gameLocal.GetMapName();
	if ( loaded == NULL || loaded[ 0 ] == '\0' ) {
		loaded = gameLocal.serverInfo.GetString( "si_map" );
	}
	return database.Find( loaded );
}

// Everyone connected counts, bots too: they play the next map as well.
int mpMapDatabaseHost::PlayerCount( void ) const {
	return gameLocal.mpGame.NumActualClients( true );
}

// Managed matches and the Arena campaign choose their own maps.
bool mpMapDatabaseHost::RotationActive( void ) const {
	return gameLocal.isMultiplayer && gameLocal.isServer && !gameLocal.mpGame.IsManagedMatch() &&
		!gameLocal.mpGame.IsArenaCampaignMatch();
}

/*
================
mpMapDatabaseHost::ClientRole

Whether this process reads the server's database rather than its own file.
gameLocal.isClient still describes the last session while a server prepares
its first map: a player who had joined a game, or played a demo back, and then
hosted one got no mapdb.txt for the whole first map, and a rotation built from
the server they had left.
================
*/
bool mpMapDatabaseHost::ClientRole( void ) const {
	return gameLocal.isClient && !hostingNext;
}

/*
================
mpMapDatabaseHost::NextGameType

The game type the next map loads with. An operator sets si_gameType during a
match to have it at the next map; the rotation chose for the game type being
played instead, and the load that followed refused the map as incompatible
until the request was thrown away.
================
*/
int mpMapDatabaseHost::NextGameType( void ) const {
	const mpGameTypeInfo_t *pending = MPGameTypeByName( si_gameType.GetString() );
	if ( pending != NULL && MPGameTypeIsSelectable( pending->type ) ) {
		return pending->type;
	}
	return gameLocal.gameType;
}

/*
===============================================================================

	Per-map settings

===============================================================================
*/

/*
================
mpMapDatabaseHost::SettingAllowed

A map may set game rules: server info and game settings.  It may not choose
the map, the gametype or the pool it was chosen from, resize or rename the
server, or touch a password.
================
*/
bool mpMapDatabaseHost::SettingAllowed( const char *name, const char **reason ) const {
	static const char *const refused[] = {
		"si_map", "si_gameType", "si_mapCycle", "si_pure", "si_fps", "si_name", "si_maxPlayers",
		"si_nextMap", "si_mapPick", "g_mapDatabase", "g_mapRotation", "g_mapQueue", "g_mapPick", "g_mapCycle",
		NULL
	};
	*reason = "";
	idCVar *cvar = cvarSystem->Find( name );
	if ( cvar == NULL ) {
		*reason = "no such setting";
		return false;
	}
	if ( ( cvar->GetFlags() & ( CVAR_SERVERINFO | CVAR_GAME ) ) == 0 ) {
		*reason = "not a game or server info setting";
		return false;
	}
	if ( ( cvar->GetFlags() & ( CVAR_ROM | CVAR_INIT | CVAR_CHEAT ) ) != 0 ) {
		*reason = "read only, start-up only or a cheat";
		return false;
	}
	for ( int index = 0; refused[ index ] != NULL; index++ ) {
		if ( idStr::Icmp( name, refused[ index ] ) == 0 ) {
			*reason = "a map cannot change it";
			return false;
		}
	}
	if ( idStr::FindText( name, "password", false ) >= 0 || idStr::FindText( name, "rcon", false ) >= 0 ) {
		*reason = "a map cannot change it";
		return false;
	}
	return true;
}

void mpMapDatabaseHost::ApplySettings( int entryIndex ) {
	const mpMapEntry *entry = database.Entry( entryIndex );
	if ( entry == NULL ) {
		return;
	}
	for ( int index = 0; index < entry->settingCount; index++ ) {
		const mpMapSetting *setting = database.Setting( entryIndex, index );
		const char *reason = "";
		if ( !SettingAllowed( setting->key, &reason ) ) {
			gameLocal.Warning( "map database: %s: %s is not set (%s)", entry->token, setting->key, reason );
			continue;
		}
		idCVar *cvar = cvarSystem->Find( setting->key );
		appliedSetting_t applied;
		applied.name = setting->key;
		applied.original = cvar->GetString();
		cvarSystem->SetCVarString( setting->key, setting->value );
		// the setting may clamp or refuse the value; remember what it holds
		applied.applied = cvar->GetString();
		appliedSettings.Append( applied );
		gameLocal.Printf( "map database: %s: %s \"%s\" (was \"%s\")\n", entry->token, setting->key,
			applied.applied.c_str(), applied.original.c_str() );
	}
	appliedMap = entry->token;
}

/*
================
mpMapDatabaseHost::RevertSettings

A setting somebody changed while the map ran is theirs and stays.
================
*/
void mpMapDatabaseHost::RevertSettings( void ) {
	for ( int index = appliedSettings.Num() - 1; index >= 0; index-- ) {
		const appliedSetting_t &applied = appliedSettings[ index ];
		idCVar *cvar = cvarSystem->Find( applied.name.c_str() );
		if ( cvar != NULL && applied.applied.Cmp( cvar->GetString() ) == 0 ) {
			cvarSystem->SetCVarString( applied.name.c_str(), applied.original.c_str() );
			gameLocal.Printf( "map database: %s: %s back to \"%s\"\n", appliedMap.c_str(), applied.name.c_str(),
				applied.original.c_str() );
		} else if ( cvar != NULL ) {
			gameLocal.Printf( "map database: %s: %s was changed to \"%s\" during the map and stays\n", appliedMap.c_str(),
				applied.name.c_str(), cvar->GetString() );
		}
	}
	appliedSettings.Clear();
	appliedMap.Clear();
}

/*
================
mpMapDatabaseHost::ServerPrepareMap

The engine calls this, through idGame::ValidateServerSettings, before it
reads the server info for a change of map, so the settings a map carries are
part of the server info the map starts with.
================
*/
void mpMapDatabaseHost::ServerPrepareMap( const char *map ) {
	// only a server prepares a map (ClientRole)
	hostingNext = true;
	// the override file is read again here: an edit takes effect at the next map
	Rebuild();
	const bool hadSettings = appliedSettings.Num() > 0;
	RevertSettings();
	const int index = database.Find( map );
	// the Arena campaign and managed matches set their own rules
	if ( index >= 0 && !gameLocal.mpGame.IsArenaCampaignMatch() && !gameLocal.mpGame.IsManagedMatch() ) {
		ApplySettings( index );
	}
	if ( hadSettings || appliedSettings.Num() > 0 ) {
		// The rules layer imported the server's limits once and would mirror
		// them over these at map start.  Whatever the previous map left
		// behind, including a value somebody changed by hand, is imported again.
		gameLocal.mpGame.InvalidateCasualRulesImport();
	}
	si_nextMap.SetString( "" );
	si_mapPick.SetString( "" );
	publishedNext.Clear();
	publishedPick.Clear();
	mapChangePending = true;
}

void mpMapDatabaseHost::ServerMapStarted( const char *map ) {
	hostingNext = false;
	Refresh();
	mapChangePending = false;
	advanceIssued.Clear();
	intermissionAnnounced.Clear();
	advanceAttempts = 0;
	database.PickClose();
	database.NotePlayed( database.Find( map ) );
	mapSeed = static_cast<uint32_t>( gameLocal.random.RandomInt( 0x7fffffff ) ) + 1u;
	nextThinkTime = 0;
}

/*
================
mpMapDatabaseHost::ServerMapShutdown

A shutdown that no map change announced is the server stopping: its maps'
settings go back.
================
*/
void mpMapDatabaseHost::ServerMapShutdown( void ) {
	if ( mapChangePending ) {
		return;
	}
	RevertSettings();
	database.QueueClear();
	database.PickClose();
}

void mpMapDatabaseHost::NonServerMapStarted( void ) {
	hostingNext = false;
	mapChangePending = false;
	RevertSettings();
	Rebuild();
}

/*
===============================================================================

	Rotation, queue and pick on the server

===============================================================================
*/

idStr mpMapDatabaseHost::ServerPredictNext( mpMapSelection *selectionOut ) {
	Refresh();
	const int current = CurrentEntry();
	const int mode = g_mapRotation.GetInteger();
	mpMapSelection selection = database.ChooseNext( current, NextGameType(), PlayerCount(),
		mode == MP_MAP_ROTATION_SHUFFLE, mapSeed, false );
	if ( MapDbRotationSource( selection.source ) && mode != MP_MAP_ROTATION_ORDER && mode != MP_MAP_ROTATION_SHUFFLE ) {
		selection.index = -1;
		selection.source = MP_MAPDB_SOURCE_NONE;
	}
	if ( selectionOut != NULL ) {
		*selectionOut = selection;
	}
	if ( selection.index < 0 || selection.index == current ) {
		return "";
	}
	return database.Entry( selection.index )->token;
}

/*
================
mpMapDatabaseHost::ServerAdvance
================
*/
mpMapAdvance_t mpMapDatabaseHost::ServerAdvance( void ) {
	if ( !gameLocal.isMultiplayer || gameLocal.isClient ) {
		return MP_MAP_ADVANCE_STAY;
	}
	if ( gameLocal.mpGame.IsArenaCampaignMatch() ) {
		// the campaign names its own maps and never had a rotation
		return MP_MAP_ADVANCE_SCRIPT;
	}
	if ( !RotationActive() ) {
		// A managed match chooses its own maps (the series, the referee). Every
		// one reached this at the end of its review and the rotation rewrote
		// si_map under it, taking any map queued before the match was managed.
		return MP_MAP_ADVANCE_STAY;
	}
	Refresh();
	const int current = CurrentEntry();

	// The caller asks again every frame until the map changes; one request
	// takes one queued map. The change runs after the engine frame, which can
	// hold several game frames when the server is catching up: four repeats
	// was few enough for a second queued map to be taken and the first lost.
	// The count still ends, so a map that will not load gives way to another.
	if ( advanceIssued.Length() > 0 && advanceIssued.Icmp( si_map.GetString() ) == 0 &&
		++advanceAttempts < MAPDB_ADVANCE_REPEAT_FRAMES ) {
		return MP_MAP_ADVANCE_CHANGED;
	}
	advanceIssued.Clear();
	advanceAttempts = 0;

	const int mode = g_mapRotation.GetInteger();
	const int nextGameType = NextGameType();
	const mpMapSelection selection = database.ChooseNext( current, nextGameType, PlayerCount(),
		mode == MP_MAP_ROTATION_SHUFFLE, mapSeed, true );
	if ( MapDbRotationSource( selection.source ) || selection.index < 0 ) {
		if ( mode == MP_MAP_ROTATION_SCRIPT ) {
			return MP_MAP_ADVANCE_SCRIPT;
		}
		if ( mode == MP_MAP_ROTATION_STAY || selection.index < 0 ) {
			if ( mode != MP_MAP_ROTATION_STAY ) {
				gameLocal.Printf( "map database: no map of the rotation supports %s; staying\n", MPGameTypeName( nextGameType ) );
			}
			return MP_MAP_ADVANCE_STAY;
		}
	}

	const mpMapEntry *entry = database.Entry( selection.index );
	idStr detail = MapDbSourceName( selection.source );
	if ( selection.source == MP_MAPDB_SOURCE_QUEUE && selection.owner >= 0 && selection.owner < MAX_CLIENTS ) {
		detail += va( " by %s", gameLocal.userInfo[ selection.owner ].GetString( "ui_name", "a player" ) );
	} else if ( selection.source == MP_MAPDB_SOURCE_PICK ) {
		detail += va( " with %d votes", selection.votes );
	}
	gameLocal.Printf( "map database: next map %s (%s, %d players)\n", entry->token, detail.c_str(), PlayerCount() );
	if ( selection.index == current ) {
		return MP_MAP_ADVANCE_STAY;
	}
	si_map.SetString( entry->token );
	advanceIssued = entry->token;
	if ( intermissionAnnounced.Icmp( entry->token ) != 0 ) {
		gameLocal.ServerSendChatMessage( -1, "server", MAPDB_STR_NEXT_MAP, DisplayName( selection.index ) );
	}
	return MP_MAP_ADVANCE_CHANGED;
}

/*
================
mpMapDatabaseHost::ServerMatchEnded
================
*/
void mpMapDatabaseHost::ServerMatchEnded( void ) {
	if ( !ServerOfferPick() ) {
		ServerAnnounceNext();
	}
}

/*
================
mpMapDatabaseHost::ServerAnnounceNext

openQ4 MP-12: the intermission names the map that follows, while the final
scoreboard is up, with g_intermissionAnnounce. A pick, when one is offered,
names its own candidates instead.
================
*/
void mpMapDatabaseHost::ServerAnnounceNext( void ) {
	if ( !gameLocal.mpGame.IntermissionAnnounces() || !RotationActive() || gameLocal.mpGame.IsArenaCampaignMatch() ) {
		return;
	}
	mpMapSelection selection;
	if ( ServerPredictNext( &selection ).Length() == 0 || selection.index < 0 ) {
		return;
	}
	gameLocal.ServerSendChatMessage( -1, "server", MAPDB_STR_NEXT_MAP, DisplayName( selection.index ) );
	intermissionAnnounced = database.Entry( selection.index )->token;
	gameLocal.Printf( "map database: the intermission names %s\n", intermissionAnnounced.c_str() );
}

/*
================
mpMapDatabaseHost::ServerOfferPick

The end-of-match pick, where the server offers one; true when it did.
================
*/
bool mpMapDatabaseHost::ServerOfferPick( void ) {
	if ( !RotationActive() || !g_mapPick.GetBool() || gameLocal.gameType == GAME_TOURNEY ) {
		return false;
	}
	const int mode = g_mapRotation.GetInteger();
	if ( mode != MP_MAP_ROTATION_ORDER && mode != MP_MAP_ROTATION_SHUFFLE ) {
		return false;
	}
	Refresh();
	// a queued map is already the players' choice
	if ( database.QueueNum() > 0 || database.Pick().open ) {
		return false;
	}
	if ( !database.PickOpen( CurrentEntry(), NextGameType(), PlayerCount(), mapSeed ) ) {
		return false;
	}
	idStr offer;
	idStr offered;
	const mpMapPick &pick = database.Pick();
	for ( int candidate = 0; candidate < pick.candidateCount; candidate++ ) {
		offer += va( "%s%d. %s", candidate > 0 ? "   " : "", candidate + 1, DisplayName( pick.candidates[ candidate ] ) );
		offered += va( " %s", database.Entry( pick.candidates[ candidate ] )->token );
	}
	gameLocal.Printf( "map database: pick offered:%s\n", offered.c_str() );
	gameLocal.ServerSendChatMessage( -1, "server", MAPDB_STR_PICK_OFFER, offer.c_str() );
	PublishServerInfo( true );
	return true;
}

void mpMapDatabaseHost::ServerThink( void ) {
	if ( gameLocal.time < nextThinkTime && gameLocal.time + MAPDB_THINK_MSEC >= nextThinkTime ) {
		return;
	}
	nextThinkTime = gameLocal.time + MAPDB_THINK_MSEC;
	PublishServerInfo( false );
}

/*
================
mpMapDatabaseHost::PublishServerInfo

si_nextMap and si_mapPick tell every client, and the server browser, what
the rotation holds next.  They change a few times a match at most.
================
*/
void mpMapDatabaseHost::PublishServerInfo( bool force ) {
	if ( mapChangePending || !gameLocal.isServer ) {
		return;
	}
	// a change of map somebody already asked for is not ours to announce early
	if ( idStr::Icmp( si_map.GetString(), gameLocal.serverInfo.GetString( "si_map" ) ) != 0 ) {
		return;
	}
	idStr next;
	idStr offered;
	if ( RotationActive() ) {
		next = ServerPredictNext();
		const mpMapPick &pick = database.Pick();
		for ( int candidate = 0; pick.open && candidate < pick.candidateCount; candidate++ ) {
			offered += candidate > 0 ? ";" : "";
			offered += database.Entry( pick.candidates[ candidate ] )->token;
		}
	}
	if ( !force && next.Cmp( publishedNext ) == 0 && offered.Cmp( publishedPick ) == 0 ) {
		return;
	}
	publishedNext = next;
	publishedPick = offered;
	si_nextMap.SetString( next.c_str() );
	si_mapPick.SetString( offered.c_str() );
	// only these two: rescanSI would also put live any si_ setting typed for the next map
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "publishSI si_nextMap si_mapPick\n" );
}

void mpMapDatabaseHost::ServerClientDisconnect( int clientNum ) {
	database.QueueRemoveOwner( clientNum );
	database.PickRemoveVoter( clientNum );
}

void mpMapDatabaseHost::Reply( int clientNum, const char *textId, const char *parm ) {
	gameLocal.ServerSendChatMessage( clientNum, "server", textId, parm );
}

/*
================
mpMapDatabaseHost::ServerQueueSet
================
*/
void mpMapDatabaseHost::ServerQueueSet( int clientNum, const char *path ) {
	if ( !RotationActive() || !g_mapQueue.GetBool() ) {
		Reply( clientNum, MAPDB_STR_QUEUE_OFF );
		return;
	}
	Refresh();
	char token[ MP_MAPDB_TOKEN_BYTES ];
	int index = mpMapDatabase::NormalizeToken( path, token ) ? database.Find( token ) : -1;
	if ( index < 0 && token[ 0 ] != '\0' && idStr::FindChar( token, '/' ) < 0 ) {
		// "q4dm1" for "mp/q4dm1"
		index = database.Find( va( "mp/%s", token ) );
	}
	if ( index < 0 ) {
		// what the player typed goes back to them alone, stripped to plain characters
		idStr typed = path;
		typed.RemoveColors();
		typed.CapLength( 48 );
		for ( int i = 0; i < typed.Length(); i++ ) {
			if ( static_cast<unsigned char>( typed[ i ] ) < 0x20 || typed[ i ] == '%' ) {
				typed[ i ] = '?';
			}
		}
		Reply( clientNum, MAPDB_STR_UNKNOWN_MAP, typed.c_str() );
		return;
	}
	const mpMapQueueResult_t result = database.QueueAdd( clientNum, 0, index, gameLocal.gameType, CurrentEntry() );
	const char *name = DisplayName( index );
	switch ( result ) {
		case MP_MAPDB_QUEUE_ADDED:
		case MP_MAPDB_QUEUE_REPLACED:
			// everyone sees who asked for what. A muted player's request is taken and
			// shown to them alone: a line under their name every second is a voice.
			gameLocal.ServerSendChatMessage( gameLocal.IsClientTextMuted( clientNum ) ? clientNum : -1,
				gameLocal.userInfo[ clientNum ].GetString( "ui_name", "player" ), MAPDB_STR_QUEUED, name );
			gameLocal.Printf( "map database: client %d queued %s (%d queued)\n", clientNum,
				database.Entry( index )->token, database.QueueNum() );
			PublishServerInfo( false );
			break;
		case MP_MAPDB_QUEUE_NOT_QUEUEABLE:	Reply( clientNum, MAPDB_STR_NOT_QUEUEABLE, name ); break;
		case MP_MAPDB_QUEUE_WRONG_MODE:		Reply( clientNum, MAPDB_STR_WRONG_MODE, name ); break;
		case MP_MAPDB_QUEUE_CURRENT_MAP:	Reply( clientNum, MAPDB_STR_CURRENT_MAP, name ); break;
		case MP_MAPDB_QUEUE_DUPLICATE:		Reply( clientNum, MAPDB_STR_DUPLICATE, name ); break;
		case MP_MAPDB_QUEUE_FULL:			Reply( clientNum, MAPDB_STR_QUEUE_FULL ); break;
		default:							Reply( clientNum, MAPDB_STR_UNKNOWN_MAP, "" ); break;
	}
}

void mpMapDatabaseHost::ServerQueueClear( int clientNum ) {
	if ( database.QueueRemoveOwner( clientNum ) ) {
		Reply( clientNum, MAPDB_STR_QUEUE_CLEARED );
		PublishServerInfo( false );
	} else {
		Reply( clientNum, MAPDB_STR_QUEUE_NONE );
	}
}

void mpMapDatabaseHost::ServerQueueList( int clientNum ) {
	Refresh();
	if ( database.QueueNum() == 0 ) {
		Reply( clientNum, MAPDB_STR_QUEUE_EMPTY );
		return;
	}
	idStr list;
	for ( int position = 0; position < database.QueueNum() && position < 8; position++ ) {
		list += va( "%s%d. %s", position > 0 ? "   " : "", position + 1,
			DisplayName( database.QueueEntry( position )->mapIndex ) );
	}
	Reply( clientNum, MAPDB_STR_QUEUE_LIST, list.c_str() );
}

void mpMapDatabaseHost::ServerPick( int clientNum, int candidate ) {
	if ( !database.PickVote( clientNum, candidate ) ) {
		Reply( clientNum, MAPDB_STR_PICK_CLOSED );
		return;
	}
	Reply( clientNum, MAPDB_STR_PICK_TAKEN, DisplayName( database.Pick().candidates[ candidate ] ) );
}

/*
================
mpMapDatabaseHost::ServerReceiveRequest

GAME_RELIABLE_MESSAGE_MAPDB: one request byte, then its payload and nothing
after it.
================
*/
void mpMapDatabaseHost::ServerReceiveRequest( int clientNum, const idBitMsg &msg ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || clientNum >= MP_MAPDB_MAX_VOTERS ||
		gameLocal.entities[ clientNum ] == NULL || !gameLocal.entities[ clientNum ]->IsType( idPlayer::GetClassType() ) ) {
		return;
	}
	if ( msg.GetRemainingData() < 1 ) {
		gameLocal.Warning( "Ignoring empty map request from client %d", clientNum );
		return;
	}
	const int request = msg.ReadByte();
	switch ( request ) {
		case MP_MAP_REQUEST_QUEUE_LIST:
		case MP_MAP_REQUEST_QUEUE_CLEAR:
			if ( msg.GetRemainingReadBits() != 0 ) {
				break;
			}
			if ( request == MP_MAP_REQUEST_QUEUE_LIST ) {
				ServerQueueList( clientNum );
			} else {
				ServerQueueClear( clientNum );
			}
			return;
		case MP_MAP_REQUEST_QUEUE_SET: {
			char path[ MP_MAPDB_TOKEN_BYTES + 16 ];
			const int remaining = msg.GetRemainingData();
			// the path and its terminator are the whole payload
			if ( remaining < 2 || remaining > static_cast<int>( sizeof( path ) ) ) {
				break;
			}
			if ( msg.ReadString( path, sizeof( path ) ) != remaining - 1 || msg.GetRemainingReadBits() != 0 ) {
				break;
			}
			ServerQueueSet( clientNum, path );
			return;
		}
		case MP_MAP_REQUEST_PICK: {
			if ( msg.GetRemainingReadBits() != 8 ) {
				break;
			}
			ServerPick( clientNum, msg.ReadByte() );
			return;
		}
		default:
			break;
	}
	gameLocal.Warning( "Ignoring malformed map request %d from client %d", request, clientNum );
}

/*
===============================================================================

	Console

===============================================================================
*/

static void MapDbSendRequest( int request, const char *text, int value ) {
	idBitMsg outMsg;
	byte msgBuf[ 128 ];
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_MAPDB );
	outMsg.WriteByte( request );
	if ( request == MP_MAP_REQUEST_QUEUE_SET ) {
		outMsg.WriteString( text, MP_MAPDB_TOKEN_BYTES + 15 );
	} else if ( request == MP_MAP_REQUEST_PICK ) {
		outMsg.WriteByte( value );
	}
	networkSystem->ClientSendReliableMessage( outMsg );
}

/*
================
mpMapDatabaseHost::MyMap_f

mymap             the queue
mymap <map>       queue a map, or replace the one you queued
mymap clear       take your map out of the queue
================
*/
void mpMapDatabaseHost::MyMap_f( const idCmdArgs &args ) {
	if ( !gameLocal.isMultiplayer || gameLocal.GameState() != GAMESTATE_ACTIVE ) {
		common->Printf( "mymap: not in a multiplayer game\n" );
		return;
	}
	const char *argument = args.Argc() > 1 ? args.Argv( 1 ) : "";
	const bool clear = idStr::Icmp( argument, "clear" ) == 0 || idStr::Icmp( argument, "none" ) == 0;
	const int request = argument[ 0 ] == '\0' ? MP_MAP_REQUEST_QUEUE_LIST :
		( clear ? MP_MAP_REQUEST_QUEUE_CLEAR : MP_MAP_REQUEST_QUEUE_SET );
	if ( gameLocal.isClient ) {
		MapDbSendRequest( request, argument, 0 );
		return;
	}
	const int local = gameLocal.localClientNum;
	if ( local < 0 ) {
		// the dedicated console has no player to queue for
		mpMapHost.PrintQueue();
		return;
	}
	switch ( request ) {
		case MP_MAP_REQUEST_QUEUE_LIST:		mpMapHost.ServerQueueList( local ); break;
		case MP_MAP_REQUEST_QUEUE_CLEAR:	mpMapHost.ServerQueueClear( local ); break;
		default:							mpMapHost.ServerQueueSet( local, argument ); break;
	}
}

/*
================
mpMapDatabaseHost::MapPick_f

mappick <1-3>     choose a map of the end-of-match pick
================
*/
void mpMapDatabaseHost::MapPick_f( const idCmdArgs &args ) {
	if ( !gameLocal.isMultiplayer || gameLocal.GameState() != GAMESTATE_ACTIVE ) {
		common->Printf( "mappick: not in a multiplayer game\n" );
		return;
	}
	const int choice = args.Argc() > 1 ? atoi( args.Argv( 1 ) ) : 0;
	if ( choice < 1 || choice > MP_MAPDB_PICK_CANDIDATES ) {
		const char *offered = gameLocal.serverInfo.GetString( "si_mapPick" );
		common->Printf( "usage: mappick <1-%d>%s%s\n", MP_MAPDB_PICK_CANDIDATES,
			offered[ 0 ] != '\0' ? "   the pick: " : "", offered );
		return;
	}
	if ( gameLocal.isClient ) {
		MapDbSendRequest( MP_MAP_REQUEST_PICK, "", choice - 1 );
	} else if ( gameLocal.localClientNum >= 0 ) {
		mpMapHost.ServerPick( gameLocal.localClientNum, choice - 1 );
	}
}

void mpMapDatabaseHost::PrintList( void ) {
	common->Printf( "  # map                      players  R V Q  gametypes\n" );
	for ( int index = 0; index < database.Num(); index++ ) {
		const mpMapEntry *entry = database.Entry( index );
		idStr modes;
		for ( int type = GAME_SP + 1; type < NUM_GAME_TYPES; type++ ) {
			if ( database.Supports( index, type ) ) {
				modes += modes.Length() > 0 ? " " : "";
				modes += MPGameType( type )->abbrev;
			}
		}
		idStr bounds = entry->minPlayers > 0 ? va( "%d", entry->minPlayers ) : "any";
		bounds += entry->maxPlayers > 0 ? va( "-%d", entry->maxPlayers ) : ( entry->minPlayers > 0 ? "+" : "" );
		common->Printf( "%3d %-24s %-8s %c %c %c  %s%s%s\n", index, entry->token, bounds.c_str(),
			( entry->flags & MP_MAPDB_ROTATION ) ? 'R' : '-', ( entry->flags & MP_MAPDB_VOTE ) ? 'V' : '-',
			( entry->flags & MP_MAPDB_QUEUE ) ? 'Q' : '-', modes.c_str(),
			entry->settingCount > 0 ? va( "  [%d settings]", entry->settingCount ) : "",
			( entry->flags & MP_MAPDB_OVERRIDDEN ) ? "  *" : "" );
	}
	common->Printf( "%d maps; * changed by %s\n", database.Num(),
		g_mapDatabase.GetString()[ 0 ] != '\0' ? g_mapDatabase.GetString() : "the override file" );
}

void mpMapDatabaseHost::PrintRotation( void ) {
	const int current = CurrentEntry();
	const int players = gameLocal.isMultiplayer ? PlayerCount() : 0;
	common->Printf( "rotation (%s), %d players, %s:\n",
		database.HasExplicitRotation() ? ( rotationList.Length() > 0 ? "si_mapCycle" : g_mapDatabase.GetString() ) : "every map",
		players, MPGameTypeName( gameLocal.gameType ) );
	for ( int position = 0; position < database.RotationNum(); position++ ) {
		const int index = database.RotationEntry( position );
		const mpMapEntry *entry = database.Entry( index );
		common->Printf( "%s %-24s %s%s\n", index == current ? ">" : " ", entry->token,
			database.Supports( index, gameLocal.gameType ) ? "" : "not this gametype  ",
			database.FitsPlayers( index, players ) ? "" : "outside its player bounds" );
	}
	if ( gameLocal.isMultiplayer && gameLocal.isServer ) {
		mpMapSelection selection;
		const idStr next = ServerPredictNext( &selection );
		common->Printf( "next: %s (%s)\n", next.Length() > 0 ? next.c_str() : "the same map", MapDbSourceName( selection.source ) );
	}
}

void mpMapDatabaseHost::PrintQueue( void ) {
	if ( database.QueueNum() == 0 ) {
		common->Printf( "the map queue is empty\n" );
		return;
	}
	for ( int position = 0; position < database.QueueNum(); position++ ) {
		const mpMapQueueEntry *queued = database.QueueEntry( position );
		common->Printf( "%2d. %-24s client %d %s\n", position + 1, database.Entry( queued->mapIndex )->token, queued->owner,
			gameLocal.userInfo[ queued->owner ].GetString( "ui_name" ) );
	}
}

/*
================
mpMapDatabaseHost::MapDb_f

mapdb [list]      every map: bounds, rotation/vote/queue flags, gametypes
mapdb rotation    the rotation for the current gametype and player count
mapdb queue       the player queue
mapdb next        the map that follows this one, and the open pick
mapdb settings    the settings each map carries
mapdb check       read the override file and report, changing nothing
mapdb reload      discover the maps and read the override file again
================
*/
void mpMapDatabaseHost::MapDb_f( const idCmdArgs &args ) {
	const char *argument = args.Argc() > 1 ? args.Argv( 1 ) : "list";
	if ( idStr::Icmp( argument, "reload" ) == 0 ) {
		if ( gameLocal.isClient ) {
			common->Printf( "mapdb: the server owns the database\n" );
			return;
		}
		mpMapHost.Rebuild();
		mpMapHost.PrintList();
		return;
	}
	mpMapHost.Refresh();
	if ( idStr::Icmp( argument, "list" ) == 0 ) {
		mpMapHost.PrintList();
	} else if ( idStr::Icmp( argument, "rotation" ) == 0 ) {
		mpMapHost.PrintRotation();
	} else if ( idStr::Icmp( argument, "queue" ) == 0 ) {
		mpMapHost.PrintQueue();
	} else if ( idStr::Icmp( argument, "next" ) == 0 ) {
		// a client reads what the server published
		const bool server = gameLocal.isMultiplayer && gameLocal.isServer;
		const idStr next = server ? mpMapHost.ServerPredictNext() : idStr( gameLocal.serverInfo.GetString( "si_nextMap" ) );
		const char *offered = server ? si_mapPick.GetString() : gameLocal.serverInfo.GetString( "si_mapPick" );
		common->Printf( "next map: %s\n", next.Length() > 0 ? next.c_str() : "the same map" );
		if ( offered[ 0 ] != '\0' ) {
			common->Printf( "pick: %s\n", offered );
		}
	} else if ( idStr::Icmp( argument, "settings" ) == 0 ) {
		int listed = 0;
		for ( int index = 0; index < mpMapHost.database.Num(); index++ ) {
			const mpMapEntry *entry = mpMapHost.database.Entry( index );
			for ( int setting = 0; setting < entry->settingCount; setting++ ) {
				const mpMapSetting *value = mpMapHost.database.Setting( index, setting );
				const char *reason = "";
				const bool allowed = mpMapHost.SettingAllowed( value->key, &reason );
				common->Printf( "%-24s %s \"%s\"%s%s\n", entry->token, value->key, value->value,
					allowed ? "" : "  refused: ", reason );
				listed++;
			}
		}
		common->Printf( "%d settings; %d applied now for %s\n", listed, mpMapHost.appliedSettings.Num(),
			mpMapHost.appliedSettings.Num() > 0 ? mpMapHost.appliedMap.c_str() : "no map" );
	} else if ( idStr::Icmp( argument, "check" ) == 0 ) {
		const char *fileName = g_mapDatabase.GetString();
		idStr text;
		if ( fileName[ 0 ] == '\0' || !MapDbReadOverrideFile( fileName, text ) ) {
			common->Printf( "mapdb: no override file '%s'\n", fileName );
			return;
		}
		mpMapDatabase *scratch = new mpMapDatabase( mpMapHost.database );
		mpMapOverrideReport report;
		if ( scratch->ApplyOverrides( text.c_str(), text.Length(), MapDbResolveMode, report ) ) {
			common->Printf( "mapdb: %s is valid: %d maps changed, %d not installed, a rotation of %d (%d not installed)\n",
				fileName, report.mapsChanged, report.mapsUnknown, report.rotationListed, report.rotationUnknown );
		} else {
			common->Printf( "mapdb: %s line %d: %s\n", fileName, report.line, report.error );
		}
		delete scratch;
	} else {
		common->Printf( "usage: mapdb [list|rotation|queue|next|settings|check|reload]\n" );
	}
}

void mpMapDatabaseHost::ArgCompletion_Map( const idCmdArgs &args, void ( *callback )( const char *s ) ) {
	mpMapHost.Refresh();
	for ( int index = 0; index < mpMapHost.database.Num(); index++ ) {
		callback( va( "%s %s", args.Argv( 0 ), mpMapHost.database.Entry( index )->token ) );
	}
}
