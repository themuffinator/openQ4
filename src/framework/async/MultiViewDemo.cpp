/*
===========================================================================

openQ4 multi-view demo recording and playback.

The container intentionally does not reuse Quake 4's packet-oriented
.netdemo format.  MVD records are length-delimited and independently
checksummed so later readers can skip optional records without guessing
their layout, and corrupt/truncated streams fail at a bounded record.

===========================================================================
*/

#include "AsyncNetwork.h"

#include "../Session_local.h"
#include "../../idlib/NumericString.h"
#include "MvdStorageBudget.h"
#include "MvdNavigation.h"
#include "RetailNetDemo.h"
#include "../../sound/SoundCapture.h"

#include <time.h>

namespace {

static const byte MVD_MAGIC[8] = { 'O', 'Q', '4', 'M', 'V', 'D', 0x1a, '\n' };
static const unsigned int MVD_ENDIAN_MARKER = 0x12345678u;
static const unsigned int MVD_RECORD_SYNC = 0x5244564du; // "MVDR" on disk

static const unsigned short MVD_FORMAT_MAJOR = 1;
static const unsigned short MVD_FORMAT_MINOR = 3;
static const unsigned int MVD_HEADER_BYTES = 64;
static const unsigned short MVD_RECORD_HEADER_BYTES = 24;
static const int MVD_MAX_HEADER_BYTES = 4096;
static const int MVD_MAX_RECORD_HEADER_BYTES = 256;
static const int MVD_MAX_RECORD_BYTES = 16 * 1024 * 1024;
// Quake 4 game reliables and each initial-state bit message are written from
// MAX_GAME_MESSAGE_SIZE buffers in the game module. Keep this explicit at the
// container boundary so an otherwise CRC-valid file cannot make those readers
// copy more than their fixed 8 KiB buffers can hold.
static const int MVD_MAX_GAME_MESSAGE_BYTES = 8192;
// The on-disk directory is intentionally sparse. It is a quick duration and
// navigation summary; independently restorable keyframes are separate records.
static const int MVD_INDEX_INTERVAL_MS = 1000;
// A forward seek replays records rather than reloading the map for a keyframe
// unless the keyframe saves at least this much replay.
static const int MVD_KEYFRAME_FORWARD_MIN_MS = 20000;
static const int MVD_MAX_USERINFO_BYTES = 64 * 1024;
// Bounds of the game's keyframe delta baseline. They mirror GENTITYNUM_BITS,
// MAX_ENTITY_STATE_SIZE and ENTITY_PVS_SIZE in both game modules, whose
// reader enforces its own copies.
static const int MVD_BASELINE_MAX_ENTITIES = 1 << 12;
static const int MVD_BASELINE_MAX_STATE_BYTES = 512;
static const int MVD_BASELINE_PVS_WORDS = ( ( 1 << 12 ) + 31 ) >> 5;
static const int MVD_MAX_INDEX_ENTRIES = 100000;
static const int MVD_MAX_STREAM_RECORDS = 5000000;
static const int MVD_MAX_INITIALIZATION_RECORDS = 256;
static const int MVD_MAX_RECORDS_PER_FRAME = 4096;
static const int MVD_MAX_INSTANCES = 8;
// The game's start-state powerup mask is POWERUP_MAX bits wide: 16 before The
// Awakening's two powerups were added, 18 since. The engine accepts any width a
// 32-bit mask allows and lets the record layout choose one.
static const int MVD_START_STATE_MIN_POWERUP_BITS = 16;
static const int MVD_START_STATE_MAX_POWERUP_BITS = 32;
// Formats 1.0/1.1 stored the whole game-DLL ABI version even though their
// record grammar did not change. API 40 only appended the versioned MVD
// callbacks and retained the API-39 snapshot/reliable readers. The prior
// engine also explicitly accepted API 42, whose intervening API changes did
// not alter those legacy readers. API 44 differs from API 45 only at the
// engine/game presentation interface and leaves the recorded grammar intact.
// Preserve each known-compatible version
// explicitly instead of accepting an unsafe range of historical API numbers.
static const unsigned int MVD_LEGACY_GAME_API_1_0_1_1 = 39u;
static const unsigned int MVD_LEGACY_GAME_API_PRE_SHUTDOWN_SPLIT = 42u;
static const unsigned int MVD_LEGACY_GAME_API_PRE_PRESENTATION_STATE = 44u;

enum mvdFeature_t {
	MVD_FEATURE_RECORD_CRC			= 1u << 0,
	MVD_FEATURE_LENGTH_DELIMITED		= 1u << 1,
	MVD_FEATURE_SERVER_SNAPSHOTS		= 1u << 2,
	MVD_FEATURE_RELIABLE_ROUTING		= 1u << 3,
	MVD_FEATURE_FULL_WORLD_INSTANCES	= 1u << 4,
	MVD_FEATURE_INSTANCE_ROUTING		= 1u << 5,
	// openQ4 MP-22: recorded by a client of the server. Its snapshots are the ones
	// that client received, so it shows that player's view and nothing else.
	MVD_FEATURE_CLIENT_VIEW			= 1u << 6
};

// What a server's full-world recording requires.
static const unsigned int MVD_SERVER_VIEW_REQUIRED_FEATURES =
	MVD_FEATURE_RECORD_CRC |
	MVD_FEATURE_LENGTH_DELIMITED |
	MVD_FEATURE_SERVER_SNAPSHOTS |
	MVD_FEATURE_RELIABLE_ROUTING |
	MVD_FEATURE_FULL_WORLD_INSTANCES |
	MVD_FEATURE_INSTANCE_ROUTING;

// What a client-view recording requires. A reader without MVD_FEATURE_CLIENT_VIEW
// rejects the file at its header instead of feeding client snapshots to the
// server-demo reader.
static const unsigned int MVD_CLIENT_VIEW_REQUIRED_FEATURES =
	MVD_FEATURE_RECORD_CRC |
	MVD_FEATURE_LENGTH_DELIMITED |
	MVD_FEATURE_CLIENT_VIEW;

static const unsigned int MVD_SUPPORTED_REQUIRED_FEATURES =
	MVD_SERVER_VIEW_REQUIRED_FEATURES | MVD_FEATURE_CLIENT_VIEW;

static const unsigned int MVD_FORMAT_1_2_REQUIRED_FEATURES =
	MVD_FEATURE_FULL_WORLD_INSTANCES |
	MVD_FEATURE_INSTANCE_ROUTING;

enum mvdOptionalFeature_t {
	MVD_OPTIONAL_CLEAN_END			= 1u << 0,
	MVD_OPTIONAL_TIMELINE_INDEX		= 1u << 1,
	MVD_OPTIONAL_USERINFO			= 1u << 2,
	MVD_OPTIONAL_MARKERS				= 1u << 3,
	MVD_OPTIONAL_KEYFRAMES			= 1u << 4
};

// Format 1.3 adds only optional records. Earlier readers skip them and play
// the unchanged snapshot/reliable stream exactly as before.
enum mvdRecordType_t {
	MVD_RECORD_METADATA				= 1,
	MVD_RECORD_MAP_STATE				= 2,
	MVD_RECORD_NETWORK_STATE			= 3,
	MVD_RECORD_RELIABLE				= 10,
	MVD_RECORD_SNAPSHOT				= 11,
	MVD_RECORD_USERINFO				= 12,
	MVD_RECORD_KEYFRAME				= 13,
	MVD_RECORD_MARKER				= 14,
	MVD_RECORD_INDEX					= 20,
	MVD_RECORD_MARKER_TABLE			= 21,
	MVD_RECORD_KEYFRAME_TABLE		= 22,
	MVD_RECORD_SUMMARY				= 23,	// openQ4 MP-22, optional: who was seen, before the end record
	// openQ4 MP-22: a client-view recording. Each payload opens with the
	// recording's own clock (frame, time); the network state record is version 2.
	MVD_RECORD_CLIENT_SNAPSHOT		= 30,	// the whole unreliable snapshot message, after the recorder's frame
	MVD_RECORD_CLIENT_APPLY			= 31,	// a snapshot the server acknowledged (SERVER_RELIABLE_MESSAGE_APPLYSNAPSHOT)
	MVD_RECORD_CLIENT_RELIABLE		= 32,	// a game reliable, as the game reads it
	MVD_RECORD_CLIENT_USERCMD		= 33,	// the recorder's own command for one frame
	MVD_RECORD_CLIENT_KEYFRAME		= 34,	// optional: a keyframe's three blocks, the client's network state in the second
	MVD_RECORD_END					= 250
};

// The recorder's network state record version.
static const unsigned short MVD_NETWORK_STATE_CLIENT_VERSION = 2;
// Bounds of the game's client network info. They mirror MAX_GENTITIES,
// ENTITY_PVS_SIZE, MAX_CLIENTS and MAX_ENTITY_STATE_SIZE in the multiplayer module.
static const int MVD_CLIENT_STATE_ENTITIES = 1 << 12;
static const int MVD_CLIENT_STATE_PVS_WORDS = ( ( 1 << 12 ) + 31 ) >> 5;
static const int MVD_CLIENT_STATE_ENTITY_BITS = 12;
static const int MVD_CLIENT_STATE_MAX_SNAPSHOTS = 64;
static const int MVD_CLIENT_STATE_MAX_USERCMDS = 256 * 32;	// MAX_USERCMD_BACKUP * MAX_ASYNC_CLIENTS

enum mvdRecordFlags_t {
	MVD_RECORD_FLAG_REQUIRED			= 1u << 0
};

// These values are part of the server-demo message queue written by the game
// module. Keep them append-only and synchronized with Game_local.h.
enum mvdUnreliableRoute_t {
	MVD_UNRELIABLE_RECORD_CLIENTNUM		= 0,
	MVD_UNRELIABLE_RECORD_AREAS			= 1,
	MVD_UNRELIABLE_RECORD_AREAS_INSTANCE	= 2
};

idCVar mvd_snapshotDelay(
	"mvd_snapshotDelay", "50",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"milliseconds between full-world MVD snapshots",
	16, 1000 );

idCVar mvd_maxSnapshotMB(
	"mvd_maxSnapshotMB", "4",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"maximum uncompressed size of one MVD snapshot",
	1, 16 );

idCVar mvd_maxSizeMB(
	"mvd_maxSizeMB", "1024",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"maximum MVD file size in MiB; 0 disables the limit",
	0, 2047 );

idCVar mvd_maxDurationMinutes(
	"mvd_maxDurationMinutes", "360",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"maximum MVD recording duration in minutes; 0 disables the limit",
	0, 1440 );

idCVar mvd_libraryQuotaMB(
	"mvd_libraryQuotaMB", "0",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"maximum writable MVD library size in MiB; refuses new recordings when full, never deletes existing recordings; 0 disables",
	0, 1048576 );

idCVar mvd_enforceContent(
	"mvd_enforceContent", "1",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"refuse MVD playback when the declaration checksum differs" );

// Viewer-side playback controls. Quake 4 treats every plain cvar as a cheat,
// and playback restores the recording server's network-synced cvars, so a demo
// from a server without cheats would otherwise refuse pause, speed and seek
// changes. None of these alter what was recorded.
idCVar mvd_scale(
	"mvd_scale", "1",
	CVAR_SYSTEM | CVAR_FLOAT | CVAR_NOCHEAT,
	"MVD playback speed",
	0.05f, 16.0f );

idCVar mvd_paused(
	"mvd_paused", "0",
	CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT,
	"pause MVD playback" );

idCVar mvd_seekBudgetMS(
	"mvd_seekBudgetMS", "12",
	CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT,
	"maximum work per frame while replay-seeking an MVD",
	1, 100 );

// Published to the game while a seek replays records without presenting them.
// Client-only debris spawned then would never move or expire before the seek
// lands, so the game drops it rather than showing it frozen where it spawned.
idCVar mvd_seeking(
	"mvd_seeking", "0",
	CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT | CVAR_ROM,
	"true while an MVD seek replays records" );

idCVar mvd_keyframeInterval(
	"mvd_keyframeInterval", "10",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"seconds between independently restorable MVD keyframes; 0 records none",
	0, 300 );

idCVar mvd_useKeyframes(
	"mvd_useKeyframes", "1",
	CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT,
	"seek through recorded MVD keyframes instead of replaying from the start" );

idCVar mvd_markerLeadMS(
	"mvd_markerLeadMS", "2000",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"milliseconds of lead-in before a marker when seeking to it",
	0, 30000 );

const char *MVD_MarkerKindName( int kind ) {
	static const char * const names[ SERVER_MVD_MARKER_KIND_COUNT ] = {
		"none", "phase", "round", "pause", "score", "kill", "flag", "powerup", "note"
	};
	return kind >= 0 && kind < SERVER_MVD_MARKER_KIND_COUNT ? names[ kind ] : "unknown";
}

// Auto-tracking belongs to the multiplayer game module, which owns these
// cvars. A camera the operator picks by hand ends it, as in other MVD players.
void MVD_StopAutoTrack() {
	idCVar *autoTrack = cvarSystem->Find( "demo_autoTrack" );
	if ( autoTrack != NULL && autoTrack->GetInteger() != 0 ) {
		autoTrack->SetInteger( 0 );
		common->Printf( "MVD auto-tracking off\n" );
	}
}

// 0 for any kind, a kind, or -1 when the name is unknown.
static int MVD_MarkerKindFromName( const char *name ) {
	if ( name == NULL || name[0] == '\0' || !idStr::Icmp( name, "any" ) ) {
		return 0;
	}
	for ( int kind = 1; kind < SERVER_MVD_MARKER_KIND_COUNT; kind++ ) {
		if ( !idStr::Icmp( name, MVD_MarkerKindName( kind ) ) ) {
			return kind;
		}
	}
	return -1;
}

static bool MVD_WriteExact( idFile *file, const void *data, int length ) {
	return file != NULL && length >= 0 && file->Write( data, length ) == length;
}

static bool MVD_ReadExact( idFile *file, void *data, int length ) {
	return file != NULL && length >= 0 && file->Read( data, length ) == length;
}

static unsigned int MVD_ReadLittleUInt( const byte *data ) {
	unsigned int value;
	memcpy( &value, data, sizeof( value ) );
	return LittleLong( value );
}

static unsigned short MVD_ReadLittleUShort( const byte *data ) {
	unsigned short value;
	memcpy( &value, data, sizeof( value ) );
	return LittleShort( value );
}

static bool MVD_PathExists( const idStr &path ) {
	if ( fileSystem->ReadFile( path.c_str(), NULL, NULL ) >= 0 ) {
		return true;
	}
	// openQ4 MP-22: a client on a pure server cannot see loose files through the
	// file system, its own recordings included, and a recording name that looks
	// free there would replace an earlier recording. Ask the save folder itself.
	std::error_code error;
	return std::filesystem::exists( std::filesystem::u8path(
		fileSystem->RelativePathToOSPath( path.c_str(), "fs_savepath" ) ), error );
}

/*
==================
MVD_PackDifference

openQ4 MP-22: compares the packs a recording kept in its metadata ("packs":
"<dir>/<pack>:<checksum>;...") with the ones loaded now, and names what
differs: a pack with another checksum, one missing here, one only here.
==================
*/
static idStr MVD_PackDifference( const char *recorded ) {
	if ( recorded == NULL || recorded[0] == '\0' ) {
		return "The demo does not say which packs it was recorded with (it predates that record).";
	}
	idStrList now;
	FS_ListLoadedPacks( now );
	idStrList then;
	idStr list = recorded;
	int start = 0;
	for ( int i = 0; i <= list.Length(); i++ ) {
		if ( i == list.Length() || list[i] == ';' ) {
			if ( i > start ) {
				then.Append( list.Mid( start, i - start ) );
			}
			start = i + 1;
		}
	}
	auto name = []( const idStr &entry ) {
		const int colon = entry.Last( ':' );
		return colon > 0 ? entry.Left( colon ) : entry;
	};
	idStr changed;
	idStr missing;
	idStr extra;
	for ( int i = 0; i < then.Num(); i++ ) {
		bool found = false;
		for ( int j = 0; j < now.Num() && !found; j++ ) {
			if ( name( then[i] ).Icmp( name( now[j] ) ) == 0 ) {
				found = true;
				if ( then[i].Icmp( now[j] ) != 0 ) {
					changed += va( " %s (recorded %s, here %s)", name( then[i] ).c_str(),
						then[i].c_str() + name( then[i] ).Length() + 1, now[j].c_str() + name( now[j] ).Length() + 1 );
				}
			}
		}
		if ( !found ) {
			missing += va( " %s", name( then[i] ).c_str() );
		}
	}
	for ( int j = 0; j < now.Num(); j++ ) {
		bool found = false;
		for ( int i = 0; i < then.Num() && !found; i++ ) {
			found = name( then[i] ).Icmp( name( now[j] ) ) == 0;
		}
		if ( !found ) {
			extra += va( " %s", name( now[j] ).c_str() );
		}
	}
	if ( changed.IsEmpty() && missing.IsEmpty() && extra.IsEmpty() ) {
		return "Every pack matches the recording's; loose files or a mod changed the game data.";
	}
	idStr out = "Packs that differ from the recording's:";
	if ( !changed.IsEmpty() ) {
		out += va( " changed:%s.", changed.c_str() );
	}
	if ( !missing.IsEmpty() ) {
		out += va( " missing here:%s.", missing.c_str() );
	}
	if ( !extra.IsEmpty() ) {
		out += va( " only here:%s.", extra.c_str() );
	}
	return out;
}

/*
==================
MVD_ReadSummary

openQ4 MP-22: the summary dictionary's players. A damaged summary loses the
names, never the demo.
==================
*/
static bool MVD_ReadDict( idFile *file, idDict &dict, idStr &error );

static void MVD_ReadSummary( const idList<byte> &payload, idStr &players ) {
	idFile_Memory file( "MVD summary", reinterpret_cast<const char *>( payload.Ptr() ), payload.Num() );
	idDict summary;
	idStr error;
	if ( MVD_ReadDict( &file, summary, error ) && file.Tell() == file.Length() ) {
		players = summary.GetString( "players" );
	}
}

static bool MVD_IsClientRecord( unsigned short type ) {
	return type == MVD_RECORD_CLIENT_SNAPSHOT || type == MVD_RECORD_CLIENT_APPLY ||
		type == MVD_RECORD_CLIENT_RELIABLE || type == MVD_RECORD_CLIENT_USERCMD;
}

static bool MVD_IsLiveRecord( unsigned short type ) {
	// the client-view records are 30 to 33 (MVD_IsClientRecord)
	return type == MVD_RECORD_RELIABLE || type == MVD_RECORD_SNAPSHOT || type == MVD_RECORD_END ||
		( type >= MVD_RECORD_CLIENT_SNAPSHOT && type <= MVD_RECORD_CLIENT_USERCMD );
}

static bool MVD_RecordSchemaSupported( unsigned short type, unsigned short version ) {
	switch ( type ) {
		case MVD_RECORD_RELIABLE:
		case MVD_RECORD_SNAPSHOT:
			return version == 1 || version == 2;
		case MVD_RECORD_NETWORK_STATE:
			return version == 1 || version == MVD_NETWORK_STATE_CLIENT_VERSION;
		case MVD_RECORD_METADATA:
		case MVD_RECORD_MAP_STATE:
		case MVD_RECORD_USERINFO:
		case MVD_RECORD_KEYFRAME:
		case MVD_RECORD_MARKER:
		case MVD_RECORD_INDEX:
		case MVD_RECORD_MARKER_TABLE:
		case MVD_RECORD_KEYFRAME_TABLE:
		case MVD_RECORD_END:
		case MVD_RECORD_CLIENT_SNAPSHOT:
		case MVD_RECORD_CLIENT_APPLY:
		case MVD_RECORD_CLIENT_RELIABLE:
		case MVD_RECORD_CLIENT_USERCMD:
		case MVD_RECORD_CLIENT_KEYFRAME:
		case MVD_RECORD_SUMMARY:
			return version == 1;
		default:
			return false;
	}
}

/*
==================
MVD_ScanClientRecord

openQ4 MP-22: the structural check of one client-view record, shared by the
library summary and the playback index. Counts what the end record counts.
==================
*/
static bool MVD_ScanClientRecord( unsigned short type, const idList<byte> &payload, int &lastFrame, int &lastTime,
		int &endTime, int &snapshots, int &reliables, idStr &error ) {
	const int minimum = type == MVD_RECORD_CLIENT_SNAPSHOT ? 12 + 5 :
		type == MVD_RECORD_CLIENT_APPLY ? 12 :
		type == MVD_RECORD_CLIENT_RELIABLE ? 9 : 13;
	const int maximum = type == MVD_RECORD_CLIENT_SNAPSHOT ? 12 + MAX_MESSAGE_SIZE :
		type == MVD_RECORD_CLIENT_APPLY ? 12 :
		type == MVD_RECORD_CLIENT_RELIABLE ? 8 + MVD_MAX_GAME_MESSAGE_BYTES : 12 + 128;
	if ( payload.Num() < minimum || payload.Num() > maximum ) {
		error = va( "invalid MVD client record %u size %d", type, payload.Num() );
		return false;
	}
	int frame = 0;
	int time = 0;
	memcpy( &frame, payload.Ptr(), sizeof( frame ) );
	memcpy( &time, payload.Ptr() + 4, sizeof( time ) );
	frame = LittleLong( frame );
	time = LittleLong( time );
	if ( frame < lastFrame || time < lastTime ) {
		error = va( "out-of-order MVD client record %u (%d, %d)", type, frame, time );
		return false;
	}
	lastFrame = frame;
	lastTime = time;
	endTime = Max( endTime, time );
	if ( type == MVD_RECORD_CLIENT_SNAPSHOT ) {
		snapshots++;
	} else if ( type == MVD_RECORD_CLIENT_APPLY || type == MVD_RECORD_CLIENT_RELIABLE ) {
		reliables++;
	}
	return true;
}

static bool MVD_RecordShouldSkip( unsigned short type, unsigned short version, unsigned short flags ) {
	return ( flags & MVD_RECORD_FLAG_REQUIRED ) == 0 &&
		( !MVD_RecordSchemaSupported( type, version ) ||
		  ( flags & ~MVD_RECORD_FLAG_REQUIRED ) != 0 );
}

static unsigned int MVD_PackSchemaVersion( int major, int minor ) {
	return ( static_cast<unsigned int>( major ) << 16 ) |
		static_cast<unsigned int>( minor );
}

static int MVD_SchemaMajor( unsigned int version ) {
	return static_cast<int>( version >> 16 );
}

static int MVD_SchemaMinor( unsigned int version ) {
	return static_cast<int>( version & 0xffffu );
}

static bool MVD_IsLegacyGameAPICompatible( unsigned int version ) {
	return version == MVD_LEGACY_GAME_API_1_0_1_1 ||
		version == MVD_LEGACY_GAME_API_PRE_SHUTDOWN_SPLIT ||
		version == MVD_LEGACY_GAME_API_PRE_PRESENTATION_STATE ||
		version == static_cast<unsigned int>( GAME_API_VERSION );
}

static bool MVD_ReadCString( idFile *file, idStr &value, int maxLength ) {
	value.Clear();
	for ( int i = 0; i < maxLength; i++ ) {
		char ch = '\0';
		if ( file->Read( &ch, 1 ) != 1 ) {
			return false;
		}
		if ( ch == '\0' ) {
			return true;
		}
		value.Append( ch );
	}
	return false;
}

static bool MVD_ReadDict( idFile *file, idDict &dict, idStr &error ) {
	static const int MVD_MAX_DICT_PAIRS = 16384;

	int count = 0;
	dict.Clear();
	if ( file->ReadInt( count ) != sizeof( count ) || count < 0 || count > MVD_MAX_DICT_PAIRS ) {
		error = va( "invalid MVD dictionary pair count %d", count );
		return false;
	}
	for ( int i = 0; i < count; i++ ) {
		idStr key;
		idStr value;
		if ( !MVD_ReadCString( file, key, MAX_STRING_CHARS ) ||
			 !MVD_ReadCString( file, value, MAX_STRING_CHARS ) ) {
			error = va( "truncated MVD dictionary at pair %d", i );
			return false;
		}
		dict.Set( key, value );
	}
	return true;
}

static bool MVD_ValidateSyncedCVars( const idDict &recorded, idStr &error ) {
	const idDict *networkSynced = cvarSystem->MoveCVarsToDict( CVAR_NETWORKSYNC );
	if ( networkSynced == NULL ) {
		error = "could not enumerate network-synchronized cvars";
		return false;
	}
	for ( int i = 0; i < recorded.GetNumKeyVals(); i++ ) {
		const idKeyValue *kv = recorded.GetKeyVal( i );
		if ( kv == NULL || networkSynced->FindKey( kv->GetKey() ) == NULL ) {
			error = va( "MVD map state contains non-network cvar '%s'",
				kv != NULL ? kv->GetKey().c_str() : "<invalid>" );
			return false;
		}
	}
	return true;
}

static bool MVD_ParseTimelineIndex(
	const idList<byte> &payloadBytes,
	int streamStartOffset,
	int indexRecordOffset,
	idList<int> &times,
	idList<int> &offsets,
	idList<int> &sequences,
	int &indexedEndTime ) {
	times.Clear();
	offsets.Clear();
	sequences.Clear();
	indexedEndTime = 0;

	if ( payloadBytes.Num() < 8 ) {
		return false;
	}
	const unsigned int count = MVD_ReadLittleUInt( payloadBytes.Ptr() );
	const int64_t expectedBytes = 8 + static_cast<int64_t>( count ) * 12;
	if ( count > static_cast<unsigned int>( MVD_MAX_INDEX_ENTRIES ) ||
		 expectedBytes != payloadBytes.Num() ) {
		return false;
	}

	int previousTime = idMath::INT_MIN;
	int previousOffset = streamStartOffset - 1;
	int previousSequence = 0;
	const byte *cursor = payloadBytes.Ptr() + 4;
	for ( unsigned int i = 0; i < count; i++, cursor += 12 ) {
		const int gameTime = static_cast<int>( MVD_ReadLittleUInt( cursor ) );
		const int fileOffset = static_cast<int>( MVD_ReadLittleUInt( cursor + 4 ) );
		const int sequence = static_cast<int>( MVD_ReadLittleUInt( cursor + 8 ) );
		if ( gameTime <= previousTime ||
			 fileOffset <= previousOffset ||
			 fileOffset < streamStartOffset ||
			 fileOffset >= indexRecordOffset ||
			 sequence <= previousSequence ) {
			return false;
		}
		times.Append( gameTime );
		offsets.Append( fileOffset );
		sequences.Append( sequence );
		previousTime = gameTime;
		previousOffset = fileOffset;
		previousSequence = sequence;
	}

	indexedEndTime = static_cast<int>( MVD_ReadLittleUInt(
		payloadBytes.Ptr() + payloadBytes.Num() - 4 ) );
	return count == 0 || indexedEndTime >= previousTime;
}

static bool MVD_ValidateReliablePayload( const idList<byte> &payload, unsigned short version, idStr &error,
		bool retailRoutes = false ) {
	const int routeBytes = version == 2 ? 3 : 2;
	if ( ( version != 1 && version != 2 ) ||
		 payload.Num() < 8 + routeBytes + 1 ||
		 payload.Num() - 8 - routeBytes > MVD_MAX_GAME_MESSAGE_BYTES ) {
		error = va( "invalid MVD reliable payload size %d", payload.Num() );
		return false;
	}
	const int routeType = payload[8];
	const int routeClient = static_cast<signed char>( payload[9] );
	const int maxRouteType = version == 2 ? DEMO_RECORD_INSTANCE : DEMO_RECORD_EXCLUDE;
	// openQ4 MP-22: a retail server demo also routes to its own viewer's slot,
	// MAX_ASYNC_CLIENTS (DEMO_RECORD_CLIENTNUM only)
	const int maxRouteClient = retailRoutes && routeType == DEMO_RECORD_CLIENTNUM ? MAX_ASYNC_CLIENTS : MAX_ASYNC_CLIENTS - 1;
	if ( routeType < DEMO_RECORD_CLIENTNUM || routeType > maxRouteType ||
		 routeClient < -1 || routeClient > maxRouteClient ) {
		error = va( "invalid MVD reliable route (%d, %d)", routeType, routeClient );
		return false;
	}
	if ( version == 2 ) {
		const int routeInstance = static_cast<signed char>( payload[10] );
		if ( ( routeType == DEMO_RECORD_INSTANCE &&
			  ( routeInstance < 0 || routeInstance >= MVD_MAX_INSTANCES ) ) ||
			 ( routeType != DEMO_RECORD_INSTANCE && routeInstance != -1 ) ) {
			error = va( "invalid MVD reliable instance route (%d, %d)",
				routeType, routeInstance );
			return false;
		}
	}
	return true;
}

/*
==================
MVD_StartStateMatchesPlayers

Whether a multiplayer start state, read with this powerup width, is exactly the
network-state player list: the match time, one record per listed client in
ascending order with a valid instance, then the MAX_CLIENTS sentinel and nothing
but byte padding. The first failure is described in error.
==================
*/
static bool MVD_StartStateMatchesPlayers( const byte *data, int length, int powerupBits,
		const bool seenClient[MAX_ASYNC_CLIENTS], int playerCount, idStr &error ) {
	idBitMsg startState;
	startState.Init( data, length );
	startState.SetSize( length );
	startState.BeginReading();
	if ( startState.GetRemainingReadBits() < 48 ) {
		if ( error.IsEmpty() ) {
			error = "short MVD multiplayer start state";
		}
		return false;
	}
	startState.ReadLong();
	int previousStartClient = -1;
	int startClientCount = 0;
	for ( int i = 0; i <= MAX_ASYNC_CLIENTS; i++ ) {
		if ( startState.GetRemainingReadBits() < 16 ) {
			break;
		}
		const int clientNum = startState.ReadShort();
		if ( clientNum == MAX_ASYNC_CLIENTS ) {
			if ( startClientCount == playerCount && startState.GetRemainingReadBits() < 8 ) {
				return true;
			}
			break;
		}
		if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ||
			 !seenClient[clientNum] || clientNum <= previousStartClient ||
			 startState.GetRemainingReadBits() < powerupBits + 5 ) {
			if ( error.IsEmpty() ) {
				error = va( "invalid MVD multiplayer start-state client %d", clientNum );
			}
			return false;
		}
		startState.ReadBits( powerupBits );
		const int instance = startState.ReadBits( 4 );
		if ( instance < 0 || instance >= MVD_MAX_INSTANCES ) {
			if ( error.IsEmpty() ) {
				error = va( "invalid MVD multiplayer instance %d", instance );
			}
			return false;
		}
		startState.ReadBits( 1 );
		previousStartClient = clientNum;
		startClientCount++;
	}
	if ( error.IsEmpty() ) {
		error = "unterminated or oversized MVD multiplayer start state";
	}
	return false;
}

static bool MVD_ValidateSnapshotPayload( const idList<byte> &payload, unsigned short version, idStr &error ) {
	if ( version != 1 && version != 2 ) {
		error = va( "unsupported MVD snapshot version %u", version );
		return false;
	}
	// Snapshot envelope: frame/time/sequence, ASYNC_WRITE_TAGS seed,
	// idMsgQueue byte count and queue data, then a verification tag and the
	// delta snapshot. Validate the queue before idMsgQueue::ReadFrom copies it
	// into its fixed 16 KiB buffer.
	if ( payload.Num() < 12 + 4 + 2 + 4 + 2 ) {
		error = "short MVD snapshot envelope";
		return false;
	}
	const byte *body = payload.Ptr() + 12;
	const int bodyBytes = payload.Num() - 12;
	const int queueBytes = MVD_ReadLittleUShort( body + 4 );
	if ( queueBytes < 0 || queueBytes > 16383 ||
		 queueBytes > bodyBytes - 6 ) {
		error = va( "invalid MVD snapshot message-queue size %d", queueBytes );
		return false;
	}

	int cursor = 6;
	const int queueEnd = cursor + queueBytes;
	while ( cursor < queueEnd ) {
		if ( queueEnd - cursor < 2 ) {
			error = "truncated MVD snapshot message-queue entry";
			return false;
		}
		const int messageBytes = MVD_ReadLittleUShort( body + cursor );
		cursor += 2;
		if ( messageBytes <= 0 || messageBytes > MVD_MAX_GAME_MESSAGE_BYTES ||
			 messageBytes > queueEnd - cursor ) {
			error = va( "invalid MVD snapshot queued-message size %d", messageBytes );
			return false;
		}

		const int routeType = body[cursor];
		if ( routeType == MVD_UNRELIABLE_RECORD_CLIENTNUM ) {
			if ( messageBytes < 3 ) {
				error = "short client-routed MVD unreliable message";
				return false;
			}
			const int routeClient = static_cast<signed char>( body[cursor + 1] );
			if ( routeClient < -1 || routeClient >= MAX_ASYNC_CLIENTS ) {
				error = va( "invalid MVD unreliable target %d", routeClient );
				return false;
			}
		} else if ( routeType == MVD_UNRELIABLE_RECORD_AREAS ) {
			if ( messageBytes < 10 ) {
				error = "short area-routed MVD unreliable message";
				return false;
			}
		} else if ( version == 2 && routeType == MVD_UNRELIABLE_RECORD_AREAS_INSTANCE ) {
			if ( messageBytes < 11 ) {
				error = "short instance-routed MVD unreliable message";
				return false;
			}
			const int routeInstance = static_cast<signed char>( body[cursor + 9] );
			if ( routeInstance < 0 || routeInstance >= MVD_MAX_INSTANCES ) {
				error = va( "invalid MVD unreliable instance %d", routeInstance );
				return false;
			}
		} else {
			error = va( "invalid MVD unreliable route type %d", routeType );
			return false;
		}
		cursor += messageBytes;
	}

	if ( cursor != queueEnd || bodyBytes - queueEnd < 6 ) {
		error = "truncated MVD snapshot after its message queue";
		return false;
	}
	return true;
}

}

mvdFileInfo_t::mvdFileInfo_t() {
	valid = false;
	compatible = false;
	cleanEnd = false;
	hasTimelineIndex = false;
	formatMajor = 0;
	formatMinor = 0;
	protocolMajor = 0;
	protocolMinor = 0;
	durationMS = 0;
	snapshotCount = 0;
	reliableCount = 0;
	recordCount = 0;
	markerCount = 0;
	keyframeCount = 0;
	fileSize = 0;
	retail = false;
}

/*
==================
idMultiViewDemo::idMultiViewDemo
==================
*/
idMultiViewDemo::idMultiViewDemo() {
	memset( &recordingResult, 0, sizeof( recordingResult ) );
	recordingResultValid = false;
	// The global instance is built with AsyncNetwork.cpp's statics, which may run before
	// this file's mvd_seeking is constructed: GCC's link order on Linux crashed every
	// client and server before main. Reset only the members here.
	ClearState();
}

/*
==================
idMultiViewDemo::Clear
==================
*/
void idMultiViewDemo::Clear() {
	ClearState();
	mvd_seeking.SetBool( false );
}

/*
==================
idMultiViewDemo::ClearState
==================
*/
void idMultiViewDemo::ClearState() {
	state = MVD_IDLE;
	file = NULL;
	fileName.Clear();
	tempFileName.Clear();
	lastError.Clear();
	memset( &header, 0, sizeof( header ) );

	recordingStartRealTime = 0;
	recordingLibraryByteLimit = 0;
	lastSnapshotGameFrame = 0;
	lastSnapshotGameTime = 0;
	lastSnapshotFileOffset = 0;
	snapshotSequence = 0;
	snapshotCount = 0;
	reliableCount = 0;
	recordCount = 0;
	finalizing = false;

	playbackLastRealTime = 0;
	playbackGameTime = 0.0;
	latestSnapshotGameFrame = 0;
	latestSnapshotGameTime = 0;
	predictionGameFrame = 0;
	haveSnapshot = false;
	havePendingRecord = false;
	sawCleanEnd = false;
	resettingPlayback = false;
	seekInProgress = false;
	forcePresentationFrame = false;
	seekTargetGameTime = 0;
	preferredFollowClient = DEMO_FOLLOW_FREE;
	playbackStreamOffset = 0;
	playbackInitializationRecordCount = 0;
	playbackEndGameTime = 0;
	playbackLastRecordGameFrame = 0;
	playbackLastRecordGameTime = 0;
	pendingRecord.payload.Clear();
	playbackMapState.payload.Clear();
	playbackNetworkState.payload.Clear();
	recordingIndex.Clear();
	clientView = false;
	viewClient = MAX_ASYNC_CLIENTS;
	latestSnapshotClientFrame = 0;
	latestSnapshotStampTime = 0;
	clientStartSequence = 0;
	clientStartSnapshotFrame = 0;
	clientStartSnapshotTime = 0;
	clientStartGameFrame = 0;
	memset( playbackUsercmds, 0, sizeof( playbackUsercmds ) );
	timeDemo = false;
	timeDemoQuit = false;
	timeDemoFrames = 0;
	timeDemoStartTime = 0;
	timeDemoLastFrameTime = 0;
	timeDemoSlowestFrame = 0;
	markers.clear();
	exporting = false;
	exportFps = 0;
	exportSteps = 0;
	exportFrames = 0;
	exportSound = false;
	exportDirectory.Clear();
	recordingPlayers.Clear();
	thumbnailTaken = false;
	pendingThumbnail.Clear();
	keyframes.clear();
	markersDropped = 0;
	lastKeyframeGameTime = 0;
	lastMarkerIndex = -1;
	nextPlaybackMarker = 0;
	keyframeWritesDisabled = false;
	retailSource = false;
	retailEntityDefs = 0;
	retailStream.Clear();
	openq4_retail::ClearDeclRemap();
}

/*
==================
idMultiViewDemo::Init
==================
*/
void idMultiViewDemo::Init() {
	cmdSystem->AddCommand( "recordMVD", Record_f, CMD_FL_SYSTEM, "records a server-side multi-view demo" );
	cmdSystem->AddCommand( "stopMVD", Stop_f, CMD_FL_SYSTEM, "stops MVD recording or playback" );
	cmdSystem->AddCommand( "playMVD", Play_f, CMD_FL_SYSTEM, "plays a multi-view demo" );
	cmdSystem->AddCommand( "demoExport", Export_f, CMD_FL_SYSTEM, "writes a multi-view demo as PNG frames and a WAV sound track: demoExport <demo>.mvd [fps] [quit]" );
	cmdSystem->AddCommand( "mvdInfo", Info_f, CMD_FL_SYSTEM, "validates and describes a multi-view demo" );
	cmdSystem->AddCommand( "mvdRecover", Recover_f, CMD_FL_SYSTEM, "salvages the complete part of an interrupted .mvd.part recording into a new demo" );
	cmdSystem->AddCommand( "mvdStatus", Status_f, CMD_FL_SYSTEM, "reports multi-view playback time and camera state" );
	cmdSystem->AddCommand( "mvdLibrary", Library_f, CMD_FL_SYSTEM, "reports writable MVD library storage and its configured quota" );
	cmdSystem->AddCommand( "mvdPause", Pause_f, CMD_FL_SYSTEM, "toggles MVD playback pause" );
	cmdSystem->AddCommand( "mvdSeek", Seek_f, CMD_FL_SYSTEM, "seeks to an MVD time in seconds" );
	cmdSystem->AddCommand( "mvdSkip", Skip_f, CMD_FL_SYSTEM, "moves relative to the current MVD time in seconds" );
	cmdSystem->AddCommand( "mvdSpeed", Speed_f, CMD_FL_SYSTEM, "sets MVD playback speed" );
	cmdSystem->AddCommand( "mvdStep", Step_f, CMD_FL_SYSTEM, "advances a paused MVD by simulation frames" );
	cmdSystem->AddCommand( "mvdFollowNext", FollowNext_f, CMD_FL_SYSTEM, "follows the next player in an MVD" );
	cmdSystem->AddCommand( "mvdFollowPrev", FollowPrev_f, CMD_FL_SYSTEM, "follows the previous player in an MVD" );
	cmdSystem->AddCommand( "mvdFollow", Follow_f, CMD_FL_SYSTEM, "follows an MVD player: mvdFollow [next|prev|free|<slot>|<name>]" );
	cmdSystem->AddCommand( "mvdFreeRoam", FreeRoam_f, CMD_FL_SYSTEM, "leaves player follow mode for free flight" );
	cmdSystem->AddCommand( "mvdMark", Mark_f, CMD_FL_SYSTEM, "adds an operator note marker to the active MVD recording" );
	cmdSystem->AddCommand( "mvdMarkers", Markers_f, CMD_FL_SYSTEM, "lists the playing MVD's structured markers: mvdMarkers [kind]" );
	cmdSystem->AddCommand( "mvdMarker", Marker_f, CMD_FL_SYSTEM, "seeks to an MVD marker: mvdMarker next|prev [kind]" );
	cmdSystem->AddCommand( "mvdAutoTrack", AutoTrack_f, CMD_FL_SYSTEM, "sets MVD camera auto-tracking: mvdAutoTrack [off|killer|action] [marine|strogg|any]" );

	// Quake 4-compatible command names.  The file format remains .mvd rather
	// than pretending to be the packet-oriented legacy .netdemo format.
	cmdSystem->AddCommand( "recordNetDemo", Record_f, CMD_FL_SYSTEM, "records a multiplayer demo: the whole match when hosting, your own view on a server you joined" );
	// openQ4 MP-22: the short names players know
	cmdSystem->AddCommand( "record", Record_f, CMD_FL_SYSTEM, "records a multiplayer demo: the whole match when hosting, your own view on a server you joined" );
	cmdSystem->AddCommand( "stopRecord", Stop_f, CMD_FL_SYSTEM, "stops a multiplayer demo recording (or MVD playback)" );
	cmdSystem->AddCommand( "stopNetDemo", Stop_f, CMD_FL_SYSTEM, "stops MVD recording or playback" );
	cmdSystem->AddCommand( "playNetDemo", Play_f, CMD_FL_SYSTEM, "plays a multi-view demo or a Quake 4 1.4.2 network demo" );
	// openQ4 MP-22: Quake 4's own name for timing a network demo
	cmdSystem->AddCommand( "playNetTimeDemo", TimeDemo_f, CMD_FL_SYSTEM, "times a multi-view demo or a Quake 4 1.4.2 network demo" );
}

/*
==================
idMultiViewDemo::Shutdown
==================
*/
void idMultiViewDemo::Shutdown() {
	SessionStop();
}

/*
==================
idMultiViewDemo::SessionStop
==================
*/
void idMultiViewDemo::SessionStop() {
	if ( resettingPlayback ) {
		return;
	}
	if ( state == MVD_RECORDING ) {
		StopRecording( "session ended", true );
	} else if ( state == MVD_PLAYING ) {
		StopPlayback( "session ended" );
	}
}

bool idMultiViewDemo::IsRecording() const {
	return state == MVD_RECORDING;
}

/*
==================
idMultiViewDemo::StartNamedRecording
==================
*/
bool idMultiViewDemo::StartNamedRecording( const char *name ) {
	idCmdArgs args;
	args.AppendArg( "recordMVD" );
	if ( name != NULL && name[0] != '\0' ) {
		args.AppendArg( name );
	}
	return StartRecording( args );
}

/*
==================
idMultiViewDemo::StopRecordingCleanly
==================
*/
bool idMultiViewDemo::StopRecordingCleanly( const char *reason ) {
	return StopRecording(
		( reason != NULL && reason[0] != '\0' ) ? reason : "server request", true );
}

/*
==================
idMultiViewDemo::CopyRecordingQPath
==================
*/
bool idMultiViewDemo::CopyRecordingQPath( char *buffer, int bufferSize ) const {
	if ( buffer == NULL || bufferSize <= 0 ) {
		return false;
	}
	buffer[0] = '\0';
	if ( state != MVD_RECORDING || fileName.IsEmpty() || fileName.Length() >= bufferSize ) {
		return false;
	}
	idStr::Copynz( buffer, fileName.c_str(), bufferSize );
	return true;
}

/*
==================
idMultiViewDemo::CopyRecordingResult
==================
*/
bool idMultiViewDemo::CopyRecordingResult(
		serverMVDRecordingResult_t &result ) const {
	if ( !recordingResultValid ) {
		memset( &result, 0, sizeof( result ) );
		return false;
	}
	result = recordingResult;
	return true;
}

/*
==================
idMultiViewDemo::SetRecordingResult
==================
*/
void idMultiViewDemo::SetRecordingResult( serverMVDResultState_t resultState,
		serverMVDResultReason_t resultReason, const char *finalQPath,
		const char *partialQPath ) {
	memset( &recordingResult, 0, sizeof( recordingResult ) );
	recordingResult.state = resultState;
	recordingResult.reason = resultReason;
	if ( finalQPath != NULL ) {
		idStr::Copynz( recordingResult.finalQPath, finalQPath,
			sizeof( recordingResult.finalQPath ) );
	}
	if ( partialQPath != NULL ) {
		idStr::Copynz( recordingResult.partialQPath, partialQPath,
			sizeof( recordingResult.partialQPath ) );
	}
	recordingResultValid = true;
}

bool idMultiViewDemo::IsPlaying() const {
	return state == MVD_PLAYING;
}

bool idMultiViewDemo::IsPaused() const {
	return state == MVD_PLAYING && mvd_paused.GetBool();
}

bool idMultiViewDemo::IsSeeking() const {
	return state == MVD_PLAYING && seekInProgress;
}

float idMultiViewDemo::GetPlaybackScale() const {
	return idMath::ClampFloat( 0.05f, 16.0f, mvd_scale.GetFloat() );
}

int idMultiViewDemo::GetPlaybackTimeMS() const {
	if ( state != MVD_PLAYING ) {
		return 0;
	}
	return Max( 0, static_cast<int>( playbackGameTime ) - header.startGameTime );
}

int idMultiViewDemo::GetPlaybackDurationMS() const {
	return state == MVD_PLAYING ? Max( 0, playbackEndGameTime - header.startGameTime ) : 0;
}

float idMultiViewDemo::GetPlaybackFraction() const {
	const int duration = GetPlaybackDurationMS();
	return duration > 0 ? idMath::ClampFloat( 0.0f, 1.0f, static_cast<float>( GetPlaybackTimeMS() ) / duration ) : 0.0f;
}

const char *idMultiViewDemo::GetPlaybackName() const {
	return state == MVD_PLAYING ? fileName.c_str() : "";
}

int idMultiViewDemo::GetFollowClient() const {
	return state == MVD_PLAYING && game != NULL ? game->GetDemoFollowClient() : -1;
}

void idMultiViewDemo::SetPaused( bool paused ) {
	if ( state != MVD_PLAYING ) {
		return;
	}
	mvd_paused.SetBool( paused );
	playbackLastRealTime = Sys_Milliseconds();
	if ( sessLocal.sw != NULL ) {
		if ( paused || seekInProgress ) {
			sessLocal.sw->Pause();
		} else {
			sessLocal.sw->UnPause();
		}
	}
}

void idMultiViewDemo::TogglePaused() {
	SetPaused( !IsPaused() );
}

void idMultiViewDemo::SetPlaybackScale( float scale ) {
	mvd_scale.SetFloat( idMath::ClampFloat( 0.05f, 16.0f, scale ) );
	playbackLastRealTime = Sys_Milliseconds();
	if ( sessLocal.sw != NULL ) {
		sessLocal.sw->SetSlowmoSpeed( GetPlaybackScale() );
	}
}

/*
==================
MVD_ClientViewCamera

openQ4 MP-22: a client-view recording holds what its recorder saw and no more,
so its camera cannot follow another player or fly free.
==================
*/
static bool MVD_ClientViewCamera( bool clientView ) {
	if ( clientView ) {
		common->Printf( "This demo was recorded by a player and shows their view only\n" );
	}
	return clientView;
}

void idMultiViewDemo::FollowNext() {
	// Replay controls must work while paused and across Tourney arenas.
	// Select an eligible recorded POV directly; live spectator input has a
	// game-time debounce and mode-specific bindings that do not apply here.
	if ( state == MVD_PLAYING && MVD_ClientViewCamera( clientView ) ) {
		return;
	}
	if ( state == MVD_PLAYING && game != NULL && game->SetDemoFollowClient( DEMO_FOLLOW_NEXT ) ) {
		preferredFollowClient = game->GetDemoFollowClient();
		forcePresentationFrame = true;
		MVD_StopAutoTrack();
	}
}

void idMultiViewDemo::FollowPrev() {
	if ( state == MVD_PLAYING && MVD_ClientViewCamera( clientView ) ) {
		return;
	}
	if ( state == MVD_PLAYING && game != NULL && game->SetDemoFollowClient( DEMO_FOLLOW_PREV ) ) {
		preferredFollowClient = game->GetDemoFollowClient();
		forcePresentationFrame = true;
		MVD_StopAutoTrack();
	}
}

/*
==================
idMultiViewDemo::FollowClient

A recorded player chosen by slot. The game restores a saved camera without an
eligibility check, because a round reset can precede the target's next active
snapshot, so a direct choice first requires the slot to hold a recorded player.
UpdateSpectating still releases a camera the recording does not allow before
it is presented.
==================
*/
bool idMultiViewDemo::FollowClient( int clientNum ) {
	if ( state == MVD_PLAYING && MVD_ClientViewCamera( clientView ) ) {
		return false;
	}
	if ( state != MVD_PLAYING || game == NULL || clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ||
		game->GetUserInfo( clientNum ) == NULL || !game->SetDemoFollowClient( clientNum ) ) {
		return false;
	}
	preferredFollowClient = clientNum;
	forcePresentationFrame = true;
	MVD_StopAutoTrack();
	return true;
}

/*
==================
idMultiViewDemo::FindFollowClient

A slot number, or a player name: an exact match ignoring colours and case,
otherwise the only name that contains the text. Returns -1 when no recorded
player matches and -2 when several names do.
==================
*/
int idMultiViewDemo::FindFollowClient( const char *text ) const {
	if ( state != MVD_PLAYING || game == NULL || text == NULL || text[ 0 ] == '\0' ) {
		return -1;
	}
	int slot = 0;
	if ( idNumericString::ParseUnsignedBounded( text, MAX_ASYNC_CLIENTS - 1, slot ) ) {
		return game->GetUserInfo( slot ) != NULL ? slot : -1;
	}
	idStr wanted = text;
	wanted.RemoveColors();
	wanted.ToLower();
	if ( wanted.IsEmpty() ) {
		return -1;
	}
	int partial = -1;
	int partialCount = 0;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		const idDict *info = game->GetUserInfo( i );
		if ( info == NULL ) {
			continue;
		}
		idStr name = info->GetString( "ui_name" );
		name.RemoveColors();
		name.ToLower();
		if ( name == wanted ) {
			return i;
		}
		if ( name.Find( wanted.c_str() ) >= 0 ) {
			partial = i;
			partialCount++;
		}
	}
	if ( partialCount > 1 ) {
		return -2;
	}
	return partial;
}

/*
==================
idMultiViewDemo::CopyFollowName

The recorded player's name without colour codes, for the playback overlay.
==================
*/
bool idMultiViewDemo::CopyFollowName( int clientNum, idStr &name ) const {
	name.Clear();
	if ( state != MVD_PLAYING || game == NULL || clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ) {
		return false;
	}
	const idDict *info = game->GetUserInfo( clientNum );
	if ( info == NULL ) {
		return false;
	}
	name = info->GetString( "ui_name" );
	name.RemoveColors();
	name.StripLeading( ' ' );
	name.StripTrailing( ' ' );
	return !name.IsEmpty();
}

void idMultiViewDemo::FreeRoam() {
	if ( state == MVD_PLAYING && MVD_ClientViewCamera( clientView ) ) {
		return;
	}
	if ( state == MVD_PLAYING && game != NULL && game->SetDemoFollowClient( DEMO_FOLLOW_FREE ) ) {
		preferredFollowClient = DEMO_FOLLOW_FREE;
		forcePresentationFrame = true;
		MVD_StopAutoTrack();
	}
}

/*
==================
idMultiViewDemo::BuildRecordingName
==================
*/
idStr idMultiViewDemo::BuildRecordingName( const idCmdArgs &args ) const {
	idStr base = args.Argc() >= 2 ? args.Argv( 1 ) : "mvd";
	base.BackSlashesToSlashes();
	base.StripPath();
	base.StripFileExtension();

	idStr safe;
	for ( int i = 0; i < base.Length() && safe.Length() < 64; i++ ) {
		const char c = base[i];
		if ( ( c >= 'a' && c <= 'z' ) ||
			 ( c >= 'A' && c <= 'Z' ) ||
			 ( c >= '0' && c <= '9' ) ||
			 c == '-' || c == '_' || c == '.' ) {
			safe.Append( c );
		} else {
			safe.Append( '_' );
		}
	}
	if ( safe.IsEmpty() ) {
		safe = "mvd";
	}

	for ( int slot = -1; slot < 999; slot++ ) {
		idStr candidate;
		if ( slot < 0 ) {
			candidate = va( "demos/%s.mvd", safe.c_str() );
		} else {
			candidate = va( "demos/%s_%03d.mvd", safe.c_str(), slot );
		}
		idStr partial = candidate;
		partial += ".part";
		if ( !MVD_PathExists( candidate ) && !MVD_PathExists( partial ) ) {
			return candidate;
		}
	}

	return "";
}

/*
==================
idMultiViewDemo::BuildPlaybackName
==================
*/
idStr idMultiViewDemo::BuildPlaybackName( const char *name ) const {
	idStr path = name ? name : "";
	path.BackSlashesToSlashes();
	if ( path.IsEmpty() || path.Find( ".." ) >= 0 || path.Find( ":" ) >= 0 ||
		 path[0] == '/' || path[0] == '\\' ) {
		return "";
	}
	if ( idStr::Icmpn( path.c_str(), "demos/", 6 ) != 0 ) {
		path = "demos/" + path;
	}
	path.DefaultFileExtension( ".mvd" );
	return path;
}

/*
==================
idMultiViewDemo::WriteHeader
==================
*/
bool idMultiViewDemo::WriteHeader() {
	idFile_Memory data( "MVD header" );
	data.Write( MVD_MAGIC, sizeof( MVD_MAGIC ) );
	data.WriteUnsignedInt( MVD_HEADER_BYTES );
	data.WriteUnsignedShort( header.formatMajor );
	data.WriteUnsignedShort( header.formatMinor );
	data.WriteUnsignedInt( MVD_ENDIAN_MARKER );
	data.WriteUnsignedInt( header.requiredFeatures );
	data.WriteUnsignedInt( header.optionalFeatures );
	data.WriteUnsignedShort( header.protocolMajor );
	data.WriteUnsignedShort( header.protocolMinor );
	data.WriteUnsignedInt( header.gameSchemaVersion );
	data.WriteUnsignedInt( header.usercmdHz );
	data.WriteInt( header.startGameFrame );
	data.WriteInt( header.startGameTime );
	data.WriteUnsignedInt( header.snapshotDelay );
	data.WriteUnsignedInt( header.contentChecksum );
	data.WriteUnsignedInt( header.indexOffset );

	if ( data.Length() != static_cast<int>( MVD_HEADER_BYTES - sizeof( unsigned int ) ) ) {
		lastError = va( "internal header size mismatch (%d)", data.Length() );
		return false;
	}

	const unsigned int crc = CRC32_BlockChecksum( data.GetDataPtr(), data.Length() );
	data.WriteUnsignedInt( crc );
	if ( data.Length() != static_cast<int>( MVD_HEADER_BYTES ) ||
		 !MVD_WriteExact( file, data.GetDataPtr(), data.Length() ) ) {
		lastError = "failed to write MVD header";
		return false;
	}
	return true;
}

/*
==================
idMultiViewDemo::ReadHeader
==================
*/
bool idMultiViewDemo::ReadHeader( idFile *source, header_t &outHeader ) {
	byte prefix[12];
	if ( !MVD_ReadExact( source, prefix, sizeof( prefix ) ) ) {
		lastError = "truncated MVD header";
		return false;
	}
	if ( memcmp( prefix, MVD_MAGIC, sizeof( MVD_MAGIC ) ) != 0 ) {
		lastError = "not an openQ4 MVD file";
		return false;
	}

	const unsigned int headerBytes = MVD_ReadLittleUInt( prefix + sizeof( MVD_MAGIC ) );
	if ( headerBytes < MVD_HEADER_BYTES || headerBytes > MVD_MAX_HEADER_BYTES ) {
		lastError = va( "invalid MVD header size %u", headerBytes );
		return false;
	}

	idList<byte> bytes;
	bytes.SetNum( headerBytes );
	memcpy( bytes.Ptr(), prefix, sizeof( prefix ) );
	if ( !MVD_ReadExact( source, bytes.Ptr() + sizeof( prefix ), headerBytes - sizeof( prefix ) ) ) {
		lastError = "truncated extended MVD header";
		return false;
	}

	const unsigned int storedCRC = MVD_ReadLittleUInt( bytes.Ptr() + headerBytes - sizeof( unsigned int ) );
	const unsigned int actualCRC = CRC32_BlockChecksum( bytes.Ptr(), headerBytes - sizeof( unsigned int ) );
	if ( storedCRC != actualCRC ) {
		lastError = va( "MVD header checksum mismatch (stored %08x, calculated %08x)", storedCRC, actualCRC );
		return false;
	}

	idFile_Memory data( "MVD header", reinterpret_cast<const char *>( bytes.Ptr() ), headerBytes - sizeof( unsigned int ) );
	byte magic[sizeof( MVD_MAGIC )];
	unsigned int parsedHeaderBytes;
	unsigned int endianMarker;
	if ( data.Read( magic, sizeof( magic ) ) != sizeof( magic ) ||
		 data.ReadUnsignedInt( parsedHeaderBytes ) != sizeof( parsedHeaderBytes ) ||
		 data.ReadUnsignedShort( outHeader.formatMajor ) != sizeof( outHeader.formatMajor ) ||
		 data.ReadUnsignedShort( outHeader.formatMinor ) != sizeof( outHeader.formatMinor ) ||
		 data.ReadUnsignedInt( endianMarker ) != sizeof( endianMarker ) ||
		 data.ReadUnsignedInt( outHeader.requiredFeatures ) != sizeof( outHeader.requiredFeatures ) ||
		 data.ReadUnsignedInt( outHeader.optionalFeatures ) != sizeof( outHeader.optionalFeatures ) ||
		 data.ReadUnsignedShort( outHeader.protocolMajor ) != sizeof( outHeader.protocolMajor ) ||
		 data.ReadUnsignedShort( outHeader.protocolMinor ) != sizeof( outHeader.protocolMinor ) ||
		 data.ReadUnsignedInt( outHeader.gameSchemaVersion ) != sizeof( outHeader.gameSchemaVersion ) ||
		 data.ReadUnsignedInt( outHeader.usercmdHz ) != sizeof( outHeader.usercmdHz ) ||
		 data.ReadInt( outHeader.startGameFrame ) != sizeof( outHeader.startGameFrame ) ||
		 data.ReadInt( outHeader.startGameTime ) != sizeof( outHeader.startGameTime ) ||
		 data.ReadUnsignedInt( outHeader.snapshotDelay ) != sizeof( outHeader.snapshotDelay ) ||
		 data.ReadUnsignedInt( outHeader.contentChecksum ) != sizeof( outHeader.contentChecksum ) ||
		 data.ReadUnsignedInt( outHeader.indexOffset ) != sizeof( outHeader.indexOffset ) ) {
		lastError = "MVD header fields are truncated";
		return false;
	}

	if ( parsedHeaderBytes != headerBytes || endianMarker != MVD_ENDIAN_MARKER ) {
		lastError = "invalid MVD byte order marker";
		return false;
	}
	if ( outHeader.formatMajor != MVD_FORMAT_MAJOR ) {
		lastError = va( "unsupported MVD format %u.%u (reader supports %u.x)",
			outHeader.formatMajor, outHeader.formatMinor, MVD_FORMAT_MAJOR );
		return false;
	}
	const unsigned int unsupported = outHeader.requiredFeatures & ~MVD_SUPPORTED_REQUIRED_FEATURES;
	if ( unsupported != 0 ) {
		lastError = va( "MVD requires unsupported feature bits 0x%08x", unsupported );
		return false;
	}
	if ( ( outHeader.requiredFeatures & MVD_FEATURE_CLIENT_VIEW ) != 0 ) {
		// openQ4 MP-22: a client-view recording has none of the server view's
		// snapshot and routing features, and came with format 1.3.
		if ( ( outHeader.requiredFeatures & ~MVD_CLIENT_VIEW_REQUIRED_FEATURES ) != 0 ||
			 outHeader.formatMinor < 3 ) {
			lastError = "MVD client-view recording also claims server-view features";
			return false;
		}
	} else if ( outHeader.formatMinor >= 2 &&
		 ( outHeader.requiredFeatures & MVD_FORMAT_1_2_REQUIRED_FEATURES ) !=
		 MVD_FORMAT_1_2_REQUIRED_FEATURES ) {
		lastError = "MVD format 1.2+ is missing its full-world instance features";
		return false;
	}
	if ( outHeader.protocolMajor != ASYNC_PROTOCOL_MAJOR ) {
		lastError = va( "MVD network protocol %u.%u is incompatible with %d.%d",
			outHeader.protocolMajor, outHeader.protocolMinor, ASYNC_PROTOCOL_MAJOR, ASYNC_PROTOCOL_MINOR );
		return false;
	}
	if ( outHeader.usercmdHz < 10 || outHeader.usercmdHz > 1000 ) {
		lastError = va( "invalid MVD simulation rate %u Hz", outHeader.usercmdHz );
		return false;
	}
	return true;
}

/*
==================
idMultiViewDemo::WriteRecord
==================
*/
bool idMultiViewDemo::WriteRecord( unsigned short type, unsigned short version, unsigned short flags, const void *payload, int payloadLength, bool enforceLimits ) {
	if ( file == NULL || payloadLength < 0 || payloadLength > MVD_MAX_RECORD_BYTES ||
		 ( payloadLength > 0 && payload == NULL ) ) {
		lastError = va( "invalid MVD record %u length %d", type, payloadLength );
		return false;
	}

	const int totalBytes = MVD_RECORD_HEADER_BYTES + payloadLength;
	if ( enforceLimits && WouldExceedLimits( totalBytes ) ) {
		lastError = "MVD recording size limit reached";
		return false;
	}

	const unsigned int payloadCRC = CRC32_BlockChecksum( payload, payloadLength );
	idFile_Memory recordHeader( "MVD record header" );
	recordHeader.WriteUnsignedInt( MVD_RECORD_SYNC );
	recordHeader.WriteUnsignedShort( MVD_RECORD_HEADER_BYTES );
	recordHeader.WriteUnsignedShort( type );
	recordHeader.WriteUnsignedShort( version );
	recordHeader.WriteUnsignedShort( flags );
	recordHeader.WriteUnsignedInt( payloadLength );
	recordHeader.WriteUnsignedInt( payloadCRC );
	const unsigned int headerCRC = CRC32_BlockChecksum( recordHeader.GetDataPtr(), recordHeader.Length() );
	recordHeader.WriteUnsignedInt( headerCRC );

	if ( recordHeader.Length() != MVD_RECORD_HEADER_BYTES ||
		 !MVD_WriteExact( file, recordHeader.GetDataPtr(), recordHeader.Length() ) ||
		 !MVD_WriteExact( file, payload, payloadLength ) ) {
		lastError = va( "short write while recording MVD record %u", type );
		return false;
	}

	recordCount++;
	return true;
}

/*
==================
idMultiViewDemo::ReadRecord
==================
*/
idMultiViewDemo::readResult_t idMultiViewDemo::ReadRecord( idFile *source, record_t &record ) {
	byte prefix[8];
	const int prefixRead = source->Read( prefix, sizeof( prefix ) );
	if ( prefixRead == 0 ) {
		return MVD_READ_EOF;
	}
	if ( prefixRead != sizeof( prefix ) ) {
		lastError = "truncated MVD record header";
		return MVD_READ_ERROR;
	}

	const unsigned int sync = MVD_ReadLittleUInt( prefix );
	const unsigned short headerBytes = MVD_ReadLittleUShort( prefix + sizeof( unsigned int ) );
	if ( sync != MVD_RECORD_SYNC ) {
		lastError = va( "bad MVD record sync at offset %d", source->Tell() - static_cast<int>( sizeof( prefix ) ) );
		return MVD_READ_ERROR;
	}
	if ( headerBytes < MVD_RECORD_HEADER_BYTES || headerBytes > MVD_MAX_RECORD_HEADER_BYTES ) {
		lastError = va( "invalid MVD record header size %u", headerBytes );
		return MVD_READ_ERROR;
	}

	idList<byte> headerData;
	headerData.SetNum( headerBytes );
	memcpy( headerData.Ptr(), prefix, sizeof( prefix ) );
	if ( !MVD_ReadExact( source, headerData.Ptr() + sizeof( prefix ), headerBytes - sizeof( prefix ) ) ) {
		lastError = "truncated extended MVD record header";
		return MVD_READ_ERROR;
	}

	const unsigned int storedHeaderCRC = MVD_ReadLittleUInt( headerData.Ptr() + headerBytes - sizeof( unsigned int ) );
	const unsigned int actualHeaderCRC = CRC32_BlockChecksum( headerData.Ptr(), headerBytes - sizeof( unsigned int ) );
	if ( storedHeaderCRC != actualHeaderCRC ) {
		lastError = va( "MVD record header checksum mismatch at offset %d", source->Tell() - headerBytes );
		return MVD_READ_ERROR;
	}

	idFile_Memory parsed( "MVD record header", reinterpret_cast<const char *>( headerData.Ptr() ), headerBytes - sizeof( unsigned int ) );
	unsigned int parsedSync;
	unsigned short parsedHeaderBytes;
	unsigned int payloadLength;
	unsigned int payloadCRC;
	if ( parsed.ReadUnsignedInt( parsedSync ) != sizeof( parsedSync ) ||
		 parsed.ReadUnsignedShort( parsedHeaderBytes ) != sizeof( parsedHeaderBytes ) ||
		 parsed.ReadUnsignedShort( record.type ) != sizeof( record.type ) ||
		 parsed.ReadUnsignedShort( record.version ) != sizeof( record.version ) ||
		 parsed.ReadUnsignedShort( record.flags ) != sizeof( record.flags ) ||
		 parsed.ReadUnsignedInt( payloadLength ) != sizeof( payloadLength ) ||
		 parsed.ReadUnsignedInt( payloadCRC ) != sizeof( payloadCRC ) ) {
		lastError = "truncated MVD record fields";
		return MVD_READ_ERROR;
	}
	if ( parsedSync != MVD_RECORD_SYNC || parsedHeaderBytes != headerBytes ||
		 payloadLength > MVD_MAX_RECORD_BYTES ) {
		lastError = va( "invalid MVD record %u payload length %u", record.type, payloadLength );
		return MVD_READ_ERROR;
	}

	record.payload.SetNum( payloadLength );
	if ( payloadLength > 0 && !MVD_ReadExact( source, record.payload.Ptr(), payloadLength ) ) {
		lastError = va( "truncated MVD record %u payload", record.type );
		return MVD_READ_ERROR;
	}
	const unsigned int actualPayloadCRC = CRC32_BlockChecksum( record.payload.Ptr(), record.payload.Num() );
	if ( actualPayloadCRC != payloadCRC ) {
		lastError = va( "MVD record %u checksum mismatch at offset %d", record.type, source->Tell() - record.payload.Num() );
		return MVD_READ_ERROR;
	}
	return MVD_READ_OK;
}

/*
==================
idMultiViewDemo::RecordIsSupported
==================
*/
bool idMultiViewDemo::RecordIsSupported( const record_t &record, bool duringPlayback ) {
	const unsigned short unknownFlags = record.flags & ~MVD_RECORD_FLAG_REQUIRED;
	if ( unknownFlags != 0 ) {
		if ( record.flags & MVD_RECORD_FLAG_REQUIRED ) {
			lastError = va( "MVD record type %u requires unsupported flag bits 0x%04x", record.type, unknownFlags );
			return false;
		}
		return true;
	}

	bool known = false;
	switch ( record.type ) {
		case MVD_RECORD_METADATA:
		case MVD_RECORD_MAP_STATE:
		case MVD_RECORD_NETWORK_STATE:
		case MVD_RECORD_RELIABLE:
		case MVD_RECORD_SNAPSHOT:
		case MVD_RECORD_USERINFO:
		case MVD_RECORD_KEYFRAME:
		case MVD_RECORD_MARKER:
		case MVD_RECORD_INDEX:
		case MVD_RECORD_MARKER_TABLE:
		case MVD_RECORD_KEYFRAME_TABLE:
		case MVD_RECORD_END:
		case MVD_RECORD_CLIENT_SNAPSHOT:
		case MVD_RECORD_CLIENT_APPLY:
		case MVD_RECORD_CLIENT_RELIABLE:
		case MVD_RECORD_CLIENT_USERCMD:
		case MVD_RECORD_CLIENT_KEYFRAME:
		case MVD_RECORD_SUMMARY:
			known = true;
			break;
		default:
			break;
	}

	if ( !known ) {
		if ( record.flags & MVD_RECORD_FLAG_REQUIRED ) {
			lastError = va( "MVD contains unknown required record type %u", record.type );
			return false;
		}
		return true;
	}
	if ( !MVD_RecordSchemaSupported( record.type, record.version ) ) {
		if ( record.flags & MVD_RECORD_FLAG_REQUIRED ) {
			lastError = va( "MVD record type %u requires unsupported version %u", record.type, record.version );
			return false;
		}
		return true;
	}
	if ( duringPlayback &&
		 ( record.type == MVD_RECORD_MAP_STATE || record.type == MVD_RECORD_NETWORK_STATE ) ) {
		lastError = va( "MVD contains a mid-stream initialization record (%u)", record.type );
		return false;
	}
	return true;
}

/*
==================
idMultiViewDemo::WriteMetadataRecord
==================
*/
bool idMultiViewDemo::WriteMetadataRecord() {
	idDict metadata;
	metadata.Set( "format", "openQ4 MVD" );
	metadata.Set( "engineVersion", cvarSystem->GetCVarString( "si_version" ) );
	metadata.Set( "gameModule", cvarSystem->GetCVarString( "com_activeGameModule" ) );
	metadata.Set( "map", sessLocal.mapSpawnData.serverInfo.GetString( "si_map" ) );
	metadata.Set( "gameType", sessLocal.mapSpawnData.serverInfo.GetString( "si_gameType" ) );
	metadata.Set( "serverName", sessLocal.mapSpawnData.serverInfo.GetString( "si_name" ) );
	metadata.Set( "recordedAt", Sys_TimeStampToStr( static_cast<ID_TIME_T>( time( NULL ) ) ) );
	// openQ4 MP-22: the packs the game data came from, so a refusal can name the one that differs
	idStrList packs;
	FS_ListLoadedPacks( packs );
	idStr packList;
	for ( int i = 0; i < packs.Num() && packList.Length() < 8000; i++ ) {
		if ( i > 0 ) {
			packList += ";";
		}
		packList += packs[i];
	}
	metadata.Set( "packs", packList );
	if ( clientView ) {
		metadata.Set( "view", "client" );
		metadata.SetInt( "viewClient", viewClient );
		metadata.Set( "viewName", sessLocal.mapSpawnData.userInfo[ viewClient ].GetString( "ui_name" ) );
	}

	idFile_Memory payload( "MVD metadata" );
	metadata.WriteToFileHandle( &payload );
	return WriteRecord( MVD_RECORD_METADATA, 1, 0, payload.GetDataPtr(), payload.Length() );
}

/*
==================
idMultiViewDemo::WriteMapStateRecord
==================
*/
bool idMultiViewDemo::WriteMapStateRecord() {
	idFile_Memory payload( "MVD map state" );
	WriteMapStatePayload( &payload );
	return WriteRecord( MVD_RECORD_MAP_STATE, 1, MVD_RECORD_FLAG_REQUIRED, payload.GetDataPtr(), payload.Length() );
}

/*
==================
idMultiViewDemo::WriteMapStatePayload
==================
*/
void idMultiViewDemo::WriteMapStatePayload( idFile *payload ) const {
	idDict serverInfo = sessLocal.mapSpawnData.serverInfo;
	// Demos are routinely shared.  Authentication secrets are never playback
	// state and must not become part of the recording.
	serverInfo.Delete( "si_password" );
	serverInfo.Delete( "password" );

	payload->WriteUnsignedInt( MAX_ASYNC_CLIENTS );
	serverInfo.WriteToFileHandle( payload );
	sessLocal.mapSpawnData.syncedCVars.WriteToFileHandle( payload );
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		sessLocal.mapSpawnData.userInfo[i].WriteToFileHandle( payload );
	}
}

/*
==================
idMultiViewDemo::WriteNetworkStateRecord
==================
*/
bool idMultiViewDemo::WriteNetworkStateRecord() {
	idFile_Memory payload( "MVD network state" );
	game->WriteNetworkInfo( &payload, MAX_ASYNC_CLIENTS );
	if ( payload.Length() > MVD_MAX_RECORD_BYTES ) {
		lastError = va( "initial MVD network state is too large (%d bytes)", payload.Length() );
		return false;
	}
	return WriteRecord( MVD_RECORD_NETWORK_STATE, 1, MVD_RECORD_FLAG_REQUIRED, payload.GetDataPtr(), payload.Length() );
}

/*
==================
idMultiViewDemo::WriteClientNetworkStateRecord

openQ4 MP-22: what the connected client knows when its recording starts, the
start of a client-view recording. The engine's part comes first: the slot, the
last snapshot read and the client's own frame, and the commands in flight from
that snapshot's frame on; the game's client network info (the snapshot states
it holds and the multiplayer start state) follows.
==================
*/
bool idMultiViewDemo::WriteClientNetworkStateRecord() {
	idFile_Memory payload( "MVD client network state" );
	if ( !BuildClientNetworkState( payload ) ) {
		return false;
	}
	return WriteRecord( MVD_RECORD_NETWORK_STATE, MVD_NETWORK_STATE_CLIENT_VERSION, MVD_RECORD_FLAG_REQUIRED,
		payload.GetDataPtr(), payload.Length() );
}

/*
==================
idMultiViewDemo::BuildClientNetworkState

The client network state a recording starts from, and every client keyframe.
==================
*/
bool idMultiViewDemo::BuildClientNetworkState( idFile_Memory &payload ) {
	const idAsyncClient &client = idAsyncNetwork::client;
	payload.WriteUnsignedChar( static_cast<unsigned char>( viewClient ) );
	payload.WriteInt( client.GetSnapshotSequence() );
	payload.WriteInt( client.GetSnapshotGameFrame() );
	payload.WriteInt( client.GetSnapshotGameTime() );
	payload.WriteInt( client.GetGameFrame() );
	payload.WriteInt( client.GetGameTime() );

	// the commands the prediction from that snapshot uses
	const int first = client.GetSnapshotGameFrame();
	const int last = Min( client.GetGameFrame() + MAX_USERCMD_RELAY, first + MAX_USERCMD_BACKUP - 1 );
	idList<byte> commands;
	int count = 0;
	for ( int frame = first; frame <= last; frame++ ) {
		for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
			const usercmd_t &cmd = client.GetUserCmd( frame, i );
			if ( cmd.gameFrame != frame ) {
				continue;
			}
			byte buffer[128];
			idBitMsg msg;
			msg.Init( buffer, sizeof( buffer ) );
			idAsyncNetwork::WriteUserCmdDelta( msg, cmd, NULL );
			const int start = commands.Num();
			commands.SetNum( start + 7 + msg.GetSize() );
			const int littleFrame = LittleLong( frame );
			memcpy( commands.Ptr() + start, &littleFrame, 4 );
			commands[ start + 4 ] = static_cast<byte>( i );
			commands[ start + 5 ] = static_cast<byte>( msg.GetSize() & 0xff );
			commands[ start + 6 ] = static_cast<byte>( msg.GetSize() >> 8 );
			memcpy( commands.Ptr() + start + 7, msg.GetData(), msg.GetSize() );
			count++;
		}
	}
	payload.WriteInt( count );
	if ( commands.Num() > 0 ) {
		payload.Write( commands.Ptr(), commands.Num() );
	}

	game->WriteNetworkInfo( &payload, viewClient );
	if ( payload.Length() > MVD_MAX_RECORD_BYTES ) {
		lastError = va( "initial MVD client network state is too large (%d bytes)", payload.Length() );
		return false;
	}
	idStr error;
	if ( !ValidateClientNetworkState( reinterpret_cast<const byte *>( payload.GetDataPtr() ), payload.Length(), error ) ) {
		lastError = va( "the game's client network state does not validate: %s", error.c_str() );
		return false;
	}
	return true;
}

/*
==================
idMultiViewDemo::WriteClientKeyframe

openQ4 MP-22: a client-view recording's keyframe. The layout is a server
keyframe's (counts, then map state, network info and a third block), with the
client network state as its network info and a version word as its third
block, in a record of its own type so a server-view reader never mistakes it.
==================
*/
bool idMultiViewDemo::WriteClientKeyframe( int gameFrame, int gameTime ) {
	idFile_Memory mapState( "MVD client keyframe map state" );
	WriteMapStatePayload( &mapState );
	idFile_Memory networkState( "MVD client keyframe network state" );
	if ( !BuildClientNetworkState( networkState ) ) {
		common->Warning( "MVD keyframes disabled for this recording: %s", lastError.c_str() );
		keyframeWritesDisabled = true;
		return true;
	}
	idFile_Memory payload( "MVD client keyframe" );
	payload.WriteInt( gameFrame );
	payload.WriteInt( gameTime );
	payload.WriteInt( snapshotCount );
	payload.WriteInt( reliableCount );
	payload.WriteInt( recordCount );
	payload.WriteUnsignedInt( mapState.Length() );
	payload.Write( mapState.GetDataPtr(), mapState.Length() );
	payload.WriteUnsignedInt( networkState.Length() );
	payload.Write( networkState.GetDataPtr(), networkState.Length() );
	payload.WriteUnsignedInt( 4 );
	payload.WriteUnsignedInt( 1 );	// the client keyframe's version
	if ( payload.Length() > MVD_MAX_RECORD_BYTES ) {
		common->Warning( "MVD keyframes disabled for this recording: a keyframe of %d bytes is too large", payload.Length() );
		keyframeWritesDisabled = true;
		return true;
	}
	if ( !WriteRecord( MVD_RECORD_CLIENT_KEYFRAME, 1, 0, payload.GetDataPtr(), payload.Length() ) ) {
		const idStr failure = lastError;
		StopRecording( failure.c_str(), false );
		return false;
	}
	lastKeyframeGameTime = gameTime;
	return true;
}

/*
==================
idMultiViewDemo::ValidateClientNetworkState

openQ4 MP-22: walks the whole client network state before the game reads any
of it. idGameLocal::ReadNetworkInfo copies each state into a fixed buffer and
raises a fatal error for an entity definition it does not have, so every
length and every state's definition number is checked here.
==================
*/
bool idMultiViewDemo::ValidateClientNetworkState( const byte *bytes, int length, idStr &error ) const {
	static_assert( MVD_CLIENT_STATE_MAX_USERCMDS == MAX_USERCMD_BACKUP * MAX_ASYNC_CLIENTS,
		"the client command bound follows the engine's command table" );
	error.Clear();
	int cursor = 0;
	auto need = [&]( int count ) {
		return count >= 0 && cursor <= length - count;
	};
	auto readInt = [&]( int &value ) {
		if ( !need( 4 ) ) {
			return false;
		}
		value = static_cast<int>( MVD_ReadLittleUInt( bytes + cursor ) );
		cursor += 4;
		return true;
	};
	// openQ4 MP-22: a retail recording numbers only the entity definitions of its own paks
	const int entityDefs = retailSource ? retailEntityDefs : declManager->GetNumDecls( DECL_ENTITYDEF );
	const int entityDefBits = idMath::BitsForInteger( entityDefs ) + 1;
	auto validState = [&]( int stateSize ) {
		if ( stateSize < 1 || stateSize > MVD_BASELINE_MAX_STATE_BYTES || !need( stateSize ) ) {
			return false;
		}
		idBitMsg state;
		state.Init( bytes + cursor, stateSize );
		state.SetSize( stateSize );
		state.BeginReading();
		state.ReadBits( 32 - MVD_CLIENT_STATE_ENTITY_BITS );
		const int entityDef = state.ReadBits( entityDefBits );
		cursor += stateSize;
		return !state.IsReadOverflowed() && entityDef >= 0 && entityDef < entityDefs;
	};

	if ( !need( 1 + 5 * 4 ) ) {
		error = "short MVD client network state";
		return false;
	}
	const int slot = bytes[ cursor++ ];
	if ( slot >= MAX_ASYNC_CLIENTS ) {
		error = va( "invalid MVD client slot %d", slot );
		return false;
	}
	int sequence, snapshotFrame, snapshotTime, clientFrame, clientTime, commands;
	readInt( sequence );
	readInt( snapshotFrame );
	readInt( snapshotTime );
	readInt( clientFrame );
	readInt( clientTime );
	if ( sequence < 0 || snapshotFrame < 0 || snapshotTime < 0 || clientFrame < snapshotFrame || clientTime < 0 ||
		 !readInt( commands ) || commands < 0 || commands > MVD_CLIENT_STATE_MAX_USERCMDS ) {
		error = "invalid MVD client timing or command count";
		return false;
	}
	for ( int i = 0; i < commands; i++ ) {
		if ( !need( 7 ) ) {
			error = "truncated MVD client command";
			return false;
		}
		const int client = bytes[ cursor + 4 ];
		const int size = bytes[ cursor + 5 ] | ( bytes[ cursor + 6 ] << 8 );
		cursor += 7;
		if ( client >= MAX_ASYNC_CLIENTS || size < 1 || size > 128 || !need( size ) ) {
			error = "invalid MVD client command";
			return false;
		}
		cursor += size;
	}

	// the game's part: the states it holds, its PVS words, the players, the
	// snapshots not yet acknowledged, then the multiplayer start and game state
	for ( int i = 0; i < MVD_CLIENT_STATE_ENTITIES; i++ ) {
		if ( !need( 1 ) ) {
			error = "truncated MVD client entity states";
			return false;
		}
		if ( bytes[ cursor++ ] == 0 ) {
			continue;
		}
		int number, size;
		if ( !readInt( number ) || !readInt( size ) || number != i || !validState( size ) ) {
			error = va( "invalid MVD client entity state %d", i );
			return false;
		}
	}
	if ( !need( MAX_ASYNC_CLIENTS * MVD_CLIENT_STATE_PVS_WORDS * 4 ) ) {
		error = "truncated MVD client PVS";
		return false;
	}
	cursor += MAX_ASYNC_CLIENTS * MVD_CLIENT_STATE_PVS_WORDS * 4;

	int players;
	if ( !readInt( players ) || players < 1 || players > MAX_ASYNC_CLIENTS || !need( players * 8 ) ) {
		error = va( "invalid MVD client player count %d", players );
		return false;
	}
	const int playerStart = cursor;
	bool viewPresent = false;
	for ( int i = 0, previous = -1; i < players; i++ ) {
		const int client = static_cast<int>( MVD_ReadLittleUInt( bytes + cursor ) );
		cursor += 8;
		if ( client <= previous || client >= MAX_ASYNC_CLIENTS ) {
			error = va( "invalid MVD client player %d", client );
			return false;
		}
		viewPresent = viewPresent || client == slot;
		previous = client;
	}
	if ( !viewPresent ) {
		error = "the MVD recorder is not among its players";
		return false;
	}

	int snapshots;
	if ( !readInt( snapshots ) || snapshots < 0 || snapshots > MVD_CLIENT_STATE_MAX_SNAPSHOTS ) {
		error = va( "invalid MVD client snapshot count %d", snapshots );
		return false;
	}
	for ( int s = 0; s < snapshots; s++ ) {
		int snapshotSequence, states;
		if ( !readInt( snapshotSequence ) || !readInt( states ) || states < 0 || states > MVD_CLIENT_STATE_ENTITIES ) {
			error = va( "invalid MVD client snapshot %d", s );
			return false;
		}
		for ( int i = 0; i < states; i++ ) {
			int number, size;
			if ( !readInt( number ) || !readInt( size ) || number < 0 || number >= MVD_CLIENT_STATE_ENTITIES ||
				 !validState( size ) ) {
				error = va( "invalid state in MVD client snapshot %d", s );
				return false;
			}
		}
		if ( !need( MVD_CLIENT_STATE_PVS_WORDS * 4 ) ) {
			error = "truncated MVD client snapshot PVS";
			return false;
		}
		cursor += MVD_CLIENT_STATE_PVS_WORDS * 4;
	}

	// The multiplayer part is the same as a server recording's after its players.
	idList<byte> tail;
	tail.SetNum( 4 + players * 8 + ( length - cursor ) );
	const int littlePlayers = LittleLong( players );
	memcpy( tail.Ptr(), &littlePlayers, 4 );
	memcpy( tail.Ptr() + 4, bytes + playerStart, players * 8 );
	if ( length > cursor ) {
		memcpy( tail.Ptr() + 4 + players * 8, bytes + cursor, length - cursor );
	}
	return ValidateNetworkStatePayload( tail.Ptr(), tail.Num(), error );
}

/*
==================
idMultiViewDemo::WriteIndexRecord
==================
*/
bool idMultiViewDemo::WriteIndexRecord() {
	if ( snapshotSequence > 0 &&
		 ( recordingIndex.Num() == 0 ||
		   recordingIndex[recordingIndex.Num() - 1].sequence != snapshotSequence ) ) {
		indexEntry_t finalEntry;
		finalEntry.gameTime = lastSnapshotGameTime;
		finalEntry.fileOffset = lastSnapshotFileOffset;
		finalEntry.sequence = snapshotSequence;
		if ( recordingIndex.Num() >= MVD_MAX_INDEX_ENTRIES ) {
			recordingIndex[recordingIndex.Num() - 1] = finalEntry;
		} else {
			recordingIndex.Append( finalEntry );
		}
	}
	if ( recordingIndex.Num() <= 0 ) {
		return true;
	}

	idFile_Memory payload( "MVD timeline index" );
	payload.WriteUnsignedInt( recordingIndex.Num() );
	for ( int i = 0; i < recordingIndex.Num(); i++ ) {
		payload.WriteInt( recordingIndex[i].gameTime );
		payload.WriteInt( recordingIndex[i].fileOffset );
		payload.WriteInt( recordingIndex[i].sequence );
	}
	payload.WriteInt( lastSnapshotGameTime );

	header.indexOffset = file->Tell();
	if ( !WriteRecord( MVD_RECORD_INDEX, 1, 0, payload.GetDataPtr(), payload.Length(), false ) ) {
		return false;
	}

	const int returnOffset = file->Tell();
	if ( file->Seek( 0, FS_SEEK_SET ) != 0 || !WriteHeader() ||
		 file->Seek( returnOffset, FS_SEEK_SET ) != 0 ) {
		lastError = "failed to backpatch the MVD timeline index";
		return false;
	}
	return true;
}

/*
==================
idMultiViewDemo::ReadMapStateRecord
==================
*/
bool idMultiViewDemo::ReadMapStateRecord( const record_t &record ) {
	if ( record.version != 1 ) {
		lastError = va( "unsupported MVD map-state version %u", record.version );
		return false;
	}

	idDict serverInfo;
	idDict syncedCVars;
	idDict userInfo[MAX_ASYNC_CLIENTS];
	if ( !ReadMapStatePayload( record.payload.Ptr(), record.payload.Num(), serverInfo, syncedCVars, userInfo ) ) {
		return false;
	}
	sessLocal.mapSpawnData.serverInfo = serverInfo;
	sessLocal.mapSpawnData.syncedCVars = syncedCVars;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		sessLocal.mapSpawnData.userInfo[i] = userInfo[i];
		sessLocal.mapSpawnData.persistentPlayerInfo[i].Clear();
	}
	memset( sessLocal.mapSpawnData.mapSpawnUsercmd, 0, sizeof( sessLocal.mapSpawnData.mapSpawnUsercmd ) );
	sessLocal.numClients = 0;
	return true;
}

/*
==================
idMultiViewDemo::ReadMapStatePayload

Parses a map-state block completely before anything is applied, so a keyframe
with a damaged block fails before playback resets its world.
==================
*/
bool idMultiViewDemo::ReadMapStatePayload( const byte *data, int length, idDict &serverInfo,
		idDict &syncedCVars, idDict userInfo[MAX_ASYNC_CLIENTS] ) {
	idFile_Memory payload( "MVD map state", reinterpret_cast<const char *>( data ), length );
	unsigned int clientSlots = 0;
	if ( payload.ReadUnsignedInt( clientSlots ) != sizeof( clientSlots ) ||
		 clientSlots == 0 || clientSlots > MAX_ASYNC_CLIENTS ) {
		lastError = va( "invalid MVD map-state client count %u", clientSlots );
		return false;
	}

	if ( !MVD_ReadDict( &payload, serverInfo, lastError ) ||
		 !MVD_ReadDict( &payload, syncedCVars, lastError ) ) {
		return false;
	}
	if ( !MVD_ValidateSyncedCVars( syncedCVars, lastError ) ) {
		return false;
	}
	for ( unsigned int i = 0; i < clientSlots; i++ ) {
		if ( !MVD_ReadDict( &payload, userInfo[i], lastError ) ) {
			return false;
		}
	}
	for ( unsigned int i = clientSlots; i < MAX_ASYNC_CLIENTS; i++ ) {
		userInfo[i].Clear();
	}

	if ( payload.Tell() != payload.Length() ) {
		lastError = va( "MVD map-state record has %d unexpected trailing bytes", payload.Length() - payload.Tell() );
		return false;
	}
	return true;
}

/*
==================
idMultiViewDemo::ValidateNetworkStateRecord

The multiplayer server-demo form of idGameLocal::WriteNetworkInfo is:
player count, (client, spawn id) pairs, start-state message, and game-state
message. Validate every outer length before the native game reader sees it.
==================
*/
bool idMultiViewDemo::ValidateNetworkStateRecord( const record_t &record, idStr &error ) const {
	error.Clear();
	if ( record.version != 1 ) {
		error = "short or unsupported MVD network-state record";
		return false;
	}
	return ValidateNetworkStatePayload( record.payload.Ptr(), record.payload.Num(), error );
}

/*
==================
idMultiViewDemo::ValidateNetworkStatePayload

The same game network info opens the stream and every keyframe.
==================
*/
bool idMultiViewDemo::ValidateNetworkStatePayload( const byte *bytes, int length, idStr &error ) const {
	error.Clear();
	if ( bytes == NULL || length < 4 ) {
		error = "short or unsupported MVD network-state record";
		return false;
	}

	int cursor = 0;
	const int playerCount = static_cast<int>( MVD_ReadLittleUInt( bytes + cursor ) );
	cursor += 4;
	if ( playerCount < 0 || playerCount > MAX_ASYNC_CLIENTS ||
		 cursor + playerCount * 8 > length ) {
		error = va( "invalid MVD network-state player count %d", playerCount );
		return false;
	}

	bool seenClient[MAX_ASYNC_CLIENTS];
	memset( seenClient, 0, sizeof( seenClient ) );
	int previousClient = -1;
	for ( int i = 0; i < playerCount; i++ ) {
		const int clientNum = static_cast<int>( MVD_ReadLittleUInt( bytes + cursor ) );
		cursor += 8; // client number plus spawn id
		if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ||
			 seenClient[clientNum] || clientNum <= previousClient ) {
			error = va( "invalid or duplicate MVD network-state client %d", clientNum );
			return false;
		}
		seenClient[clientNum] = true;
		previousClient = clientNum;
	}

	for ( int message = 0; message < 2; message++ ) {
		if ( cursor + 4 > length ) {
			error = "truncated MVD network-state message length";
			return false;
		}
		const int messageBytes = static_cast<int>( MVD_ReadLittleUInt( bytes + cursor ) );
		cursor += 4;
		if ( messageBytes <= 0 || messageBytes > MVD_MAX_GAME_MESSAGE_BYTES ||
			 messageBytes > length - cursor ) {
			error = va( "invalid MVD network-state message size %d", messageBytes );
			return false;
		}

		if ( message == 0 ) {
			// Current protocol start-state body: match time followed by sorted
			// client records and a MAX_CLIENTS sentinel. Validate the client
			// indexes before ClientReadStartState dereferences entities[client].
			// A record is a client short, the game's powerup mask, a 4-bit
			// instance and a spectate bit. The mask is POWERUP_MAX bits wide,
			// which the engine cannot know and which grew from 16 to 18 with The
			// Awakening's powerups, so accept the width that reads as exactly
			// the player list above.
			idStr startError;
			bool parsed = false;
			for ( int powerupBits = MVD_START_STATE_MIN_POWERUP_BITS;
				  !parsed && powerupBits <= MVD_START_STATE_MAX_POWERUP_BITS; powerupBits++ ) {
				parsed = MVD_StartStateMatchesPlayers( bytes + cursor, messageBytes, powerupBits,
					seenClient, playerCount, startError );
			}
			if ( !parsed ) {
				error = startError;
				return false;
			}
		} else if ( messageBytes < ( retailSource ? 1 : 16 ) ) {
			// openQ4 MP-22: Quake 4 1.4.2's game state is a few bytes shorter than openQ4's
			error = "short MVD multiplayer game state";
			return false;
		}
		cursor += messageBytes;
	}

	if ( cursor != length ) {
		error = va( "MVD network-state record has %d unexpected trailing bytes", length - cursor );
		return false;
	}
	return true;
}

/*
==================
idMultiViewDemo::WouldExceedLimits
==================
*/
bool idMultiViewDemo::WouldExceedLimits( int additionalBytes ) const {
	if ( file == NULL ) {
		return true;
	}
	if ( openq4_mvd::WouldExceedLibraryBudget( file->Length(), additionalBytes,
			recordingIndex.Num(), recordingLibraryByteLimit ) ) {
		return true;
	}
	const int maxSizeMB = mvd_maxSizeMB.GetInteger();
	if ( maxSizeMB <= 0 ) {
		return false;
	}
	const int64_t maxBytes = static_cast<int64_t>( maxSizeMB ) * 1024 * 1024;
	return static_cast<int64_t>( file->Length() ) + additionalBytes > maxBytes;
}

/*
==================
idMultiViewDemo::StartRecording
==================
*/
bool idMultiViewDemo::StartRecording( const idCmdArgs &args ) {
	if ( state != MVD_IDLE ) {
		common->Printf( "MVD system is already busy\n" );
		return false;
	}
	// openQ4 MP-22: a player on a server they joined records their own view.
	const bool asClient = !idAsyncNetwork::server.IsActive() && idAsyncNetwork::client.IsInGame() &&
		sessLocal.IsMapSpawned() && game != NULL && game->IsMultiplayer();
	if ( !asClient &&
		 ( !idAsyncNetwork::server.IsActive() || !sessLocal.IsMapSpawned() || game == NULL || !game->IsMultiplayer() ) ) {
		SetRecordingResult( SERVER_MVD_RESULT_FAILED,
			SERVER_MVD_REASON_START_REJECTED, NULL, NULL );
		common->Printf( "%s needs a multiplayer match: host one to record the whole match, or join one to record your view\n",
			args.Argv( 0 ) );
		return false;
	}

	const int quotaMB = mvd_libraryQuotaMB.GetInteger();
	if ( quotaMB > 0 ) {
		openq4_mvd::LibraryUsage usage;
		if ( !openq4_mvd::ScanLibrary( std::filesystem::u8path(
				fileSystem->RelativePathToOSPath( "demos", "fs_savepath" ) ), usage ) ) {
			SetRecordingResult( SERVER_MVD_RESULT_FAILED, SERVER_MVD_REASON_START_REJECTED, NULL, NULL );
			common->Warning( "MVD library quota could not measure the writable recording directory" );
			return false;
		}
		recordingLibraryByteLimit = static_cast<int64_t>( quotaMB ) * 1024 * 1024 - usage.bytes;
		if ( recordingLibraryByteLimit <= MVD_HEADER_BYTES + 100 ) {
			SetRecordingResult( SERVER_MVD_RESULT_FAILED, SERVER_MVD_REASON_START_REJECTED, NULL, NULL );
			common->Warning( "MVD library quota is full; archive recordings explicitly before recording again" );
			Clear();
			return false;
		}
	}

	fileName = BuildRecordingName( args );
	if ( fileName.IsEmpty() ||
		fileName.Length() + static_cast<int>( strlen( ".part" ) ) >
			SERVER_MVD_RESULT_QPATH_BYTES ) {
		SetRecordingResult( SERVER_MVD_RESULT_FAILED,
			SERVER_MVD_REASON_NAME_UNAVAILABLE, NULL, NULL );
		common->Warning( "No unused MVD filename is available" );
		Clear();
		return false;
	}
	tempFileName = fileName;
	tempFileName += ".part";
	SetRecordingResult( SERVER_MVD_RESULT_PENDING, SERVER_MVD_REASON_NONE,
		NULL, tempFileName.c_str() );
	file = fileSystem->OpenFileWrite( tempFileName.c_str(), "fs_savepath" );
	if ( file == NULL ) {
		SetRecordingResult( SERVER_MVD_RESULT_FAILED,
			SERVER_MVD_REASON_OPEN_FAILED, NULL, NULL );
		common->Warning( "Could not open '%s' for MVD recording", tempFileName.c_str() );
		Clear();
		return false;
	}

	state = MVD_RECORDING;
	clientView = asClient;
	viewClient = asClient ? idAsyncNetwork::client.GetClientNum() : MAX_ASYNC_CLIENTS;
	header.formatMajor = MVD_FORMAT_MAJOR;
	header.formatMinor = MVD_FORMAT_MINOR;
	header.requiredFeatures = asClient ? MVD_CLIENT_VIEW_REQUIRED_FEATURES : MVD_SERVER_VIEW_REQUIRED_FEATURES;
	header.optionalFeatures = MVD_OPTIONAL_CLEAN_END | MVD_OPTIONAL_TIMELINE_INDEX |
		MVD_OPTIONAL_USERINFO | MVD_OPTIONAL_MARKERS | MVD_OPTIONAL_KEYFRAMES;
	if ( asClient ) {
		// openQ4 MP-22: a client-view recording has no index, markers or keyframes
		header.optionalFeatures = MVD_OPTIONAL_CLEAN_END | MVD_OPTIONAL_USERINFO | MVD_OPTIONAL_KEYFRAMES | MVD_OPTIONAL_MARKERS;
	}
	header.protocolMajor = ASYNC_PROTOCOL_MAJOR;
	header.protocolMinor = ASYNC_PROTOCOL_MINOR;
	int schemaMajor = 0;
	int schemaMinor = 0;
	game->GetMVDSchemaVersion( schemaMajor, schemaMinor );
	if ( schemaMajor <= 0 || schemaMajor > 0xffff ||
			schemaMinor < 0 || schemaMinor > 0xffff ) {
		SetRecordingResult( SERVER_MVD_RESULT_FAILED,
			SERVER_MVD_REASON_SCHEMA_INVALID, NULL, tempFileName.c_str() );
		common->Warning( "Game module reported invalid MVD schema %d.%d",
			schemaMajor, schemaMinor );
		fileSystem->CloseFile( file );
		Clear();
		return false;
	}
	header.gameSchemaVersion = MVD_PackSchemaVersion( schemaMajor, schemaMinor );
	header.usercmdHz = common->GetUserCmdHz();
	// A client-view recording keeps its own clock from the client's frame at the
	// start; its records are stamped with the real time since.
	header.startGameFrame = asClient ? idAsyncNetwork::client.GetGameFrame() : idAsyncNetwork::server.GetGameFrame();
	header.startGameTime = asClient ? idAsyncNetwork::client.GetGameTime() : idAsyncNetwork::server.GetGameTime();
	header.snapshotDelay = idMath::ClampInt( 16, 1000, mvd_snapshotDelay.GetInteger() );
	header.contentChecksum = declManager->GetChecksum();
	header.indexOffset = 0;
	recordingStartRealTime = Sys_Milliseconds();
	lastSnapshotGameFrame = header.startGameFrame;
	lastSnapshotGameTime = header.startGameTime;
	lastKeyframeGameTime = header.startGameTime;

	if ( !asClient ) {
		game->SetDemoState( DEMO_RECORDING, true, false );
	}
	if ( !WriteHeader() || !WriteMetadataRecord() || !WriteMapStateRecord() ||
		 !( asClient ? WriteClientNetworkStateRecord() : WriteNetworkStateRecord() ) ) {
		common->Warning( "Could not start MVD recording: %s", lastError.c_str() );
		StopRecording( "initialization failed", false,
			SERVER_MVD_REASON_INITIALIZATION_FAILED );
		return false;
	}

	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		NoteRecordingPlayer( sessLocal.mapSpawnData.userInfo[i] );
	}
	if ( asClient ) {
		common->Printf( "Recording your view to '%s'\n", fileName.c_str() );
	} else {
		common->Printf( "Recording multi-view demo to '%s'\n", fileName.c_str() );
	}
	return true;
}

/*
==================
idMultiViewDemo::ClientStamp

openQ4 MP-22: a client-view recording's clock, the real time since it started.
The client's own game frame steps back whenever a snapshot resynchronizes it,
and a recording's timeline has to move forward only.
==================
*/
void idMultiViewDemo::ClientStamp( int &frame, int &time ) const {
	const int elapsed = Max( 0, Sys_Milliseconds() - recordingStartRealTime );
	time = header.startGameTime + elapsed;
	frame = header.startGameFrame + elapsed / Max( 1, common->GetUserCmdMSec() );
}

bool idMultiViewDemo::WriteClientRecord( unsigned short type, const void *body, int bodyLength, bool required ) {
	int frame = 0;
	int time = 0;
	ClientStamp( frame, time );
	idFile_Memory payload( "MVD client record" );
	payload.WriteInt( frame );
	payload.WriteInt( time );
	if ( bodyLength > 0 ) {
		payload.Write( body, bodyLength );
	}
	if ( !WriteRecord( type, 1, required ? MVD_RECORD_FLAG_REQUIRED : 0, payload.GetDataPtr(), payload.Length() ) ) {
		const idStr failure = lastError;
		StopRecording( failure.c_str(), false );
		return false;
	}
	return true;
}

/*
==================
idMultiViewDemo::NoteRecordingPlayer

openQ4 MP-22: every name seen while recording goes in the summary the library
shows and searches.
==================
*/
void idMultiViewDemo::NoteRecordingPlayer( const idDict &info ) {
	idStr name = info.GetString( "ui_name" );
	name.RemoveColors();
	name.StripLeading( ' ' );
	name.StripTrailing( ' ' );
	if ( name.IsEmpty() || recordingPlayers.Num() >= 64 ) {
		return;
	}
	for ( int i = 0; i < recordingPlayers.Num(); i++ ) {
		if ( recordingPlayers[i].Icmp( name ) == 0 ) {
			return;
		}
	}
	recordingPlayers.Append( name );
}

/*
==================
idMultiViewDemo::TakeRecordingThumbnail

openQ4 MP-22: three seconds into a recording made where there is a view, a
320x240 picture of it goes beside the demo as demos/<name>_thumb.png, which
the library shows in place of the map's levelshot. A dedicated server has no
view and takes none.
==================
*/
void idMultiViewDemo::TakeRecordingThumbnail() {
	if ( thumbnailTaken || state != MVD_RECORDING || finalizing || Sys_Milliseconds() - recordingStartRealTime < 3000 ) {
		return;
	}
	thumbnailTaken = true;
	if ( idAsyncNetwork::serverDedicated.GetInteger() != 0 || renderSystem == NULL ) {
		return;
	}
	pendingThumbnail = fileName;
	pendingThumbnail.StripFileExtension();
	pendingThumbnail += "_thumb.png";
}

/*
==================
idMultiViewDemo::ServiceThumbnail

Called at the start of the network frame, the point where console commands
run: a command appended to the buffer could wait behind a script's waitMsec
until the recording was over.
==================
*/
void idMultiViewDemo::ServiceThumbnail() {
	if ( pendingThumbnail.IsEmpty() ) {
		return;
	}
	const idStr thumbnail = pendingThumbnail;
	pendingThumbnail.Clear();
	if ( state == MVD_RECORDING && !finalizing ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "screenshot \"%s\" 320 240\n", thumbnail.c_str() ) );
	}
}

/*
==================
idMultiViewDemo::WriteSummaryRecord

openQ4 MP-22: the names seen while recording, before the end record.
==================
*/
bool idMultiViewDemo::WriteSummaryRecord() {
	idDict summary;
	idStr names;
	for ( int i = 0; i < recordingPlayers.Num(); i++ ) {
		names += i > 0 ? ", " : "";
		names += recordingPlayers[i];
	}
	summary.Set( "players", names );
	idFile_Memory payload( "MVD summary" );
	summary.WriteToFileHandle( &payload );
	return WriteRecord( MVD_RECORD_SUMMARY, 1, 0, payload.GetDataPtr(), payload.Length(), false );
}

bool idMultiViewDemo::IsClientRecording() const {
	return state == MVD_RECORDING && clientView && !finalizing;
}

void idMultiViewDemo::CaptureClientSnapshot( const idBitMsg &msg, int messageStart, int clientGameFrame ) {
	if ( !IsClientRecording() ) {
		return;
	}
	const int messageBytes = msg.GetSize() - messageStart;
	if ( messageStart < 0 || messageBytes <= 0 || messageBytes > MAX_MESSAGE_SIZE ) {
		StopRecording( va( "snapshot of %d bytes", messageBytes ), false );
		return;
	}
	const int maxMinutes = mvd_maxDurationMinutes.GetInteger();
	if ( maxMinutes > 0 && Sys_Milliseconds() - recordingStartRealTime > maxMinutes * 60000 ) {
		StopRecording( "duration limit reached", true );
		return;
	}
	idFile_Memory body( "MVD client snapshot" );
	body.WriteInt( clientGameFrame );
	body.Write( msg.GetData() + messageStart, messageBytes );
	if ( !WriteClientRecord( MVD_RECORD_CLIENT_SNAPSHOT, body.GetDataPtr(), body.Length(), true ) ) {
		return;
	}
	snapshotCount++;
	TakeRecordingThumbnail();
	// a keyframe follows a snapshot, when the game holds it as its latest state
	const int interval = mvd_keyframeInterval.GetInteger();
	int frame = 0;
	int time = 0;
	ClientStamp( frame, time );
	if ( interval > 0 && !keyframeWritesDisabled && time - lastKeyframeGameTime >= interval * 1000 ) {
		WriteClientKeyframe( frame, time );
	}
}

void idMultiViewDemo::CaptureClientApplySnapshot( int sequence ) {
	if ( !IsClientRecording() ) {
		return;
	}
	idFile_Memory body( "MVD client apply" );
	body.WriteInt( sequence );
	if ( WriteClientRecord( MVD_RECORD_CLIENT_APPLY, body.GetDataPtr(), body.Length(), true ) ) {
		reliableCount++;
	}
}

void idMultiViewDemo::CaptureClientReliable( const idBitMsg &msg ) {
	if ( !IsClientRecording() ) {
		return;
	}
	// what the game reads: the message after the engine's own identifier
	const int offset = msg.GetReadCount();
	const int bytes = msg.GetSize() - offset;
	if ( bytes <= 0 || bytes > MVD_MAX_GAME_MESSAGE_BYTES ) {
		StopRecording( va( "game reliable of %d bytes", bytes ), false );
		return;
	}
	if ( WriteClientRecord( MVD_RECORD_CLIENT_RELIABLE, msg.GetData() + offset, bytes, true ) ) {
		reliableCount++;
	}
}

void idMultiViewDemo::CaptureClientUserInfo( int clientNum, const idDict &info ) {
	if ( !IsClientRecording() || clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ) {
		return;
	}
	NoteRecordingPlayer( info );
	idFile_Memory body( "MVD client user info" );
	body.WriteUnsignedChar( static_cast<unsigned char>( clientNum ) );
	info.WriteToFileHandle( &body );
	if ( body.Length() + 8 > MVD_MAX_USERINFO_BYTES ) {
		common->Warning( "Ignoring oversized MVD user info for client %d", clientNum );
		return;
	}
	WriteClientRecord( MVD_RECORD_USERINFO, body.GetDataPtr(), body.Length(), false );
}

void idMultiViewDemo::CaptureClientUsercmd( const usercmd_t &cmd ) {
	if ( !IsClientRecording() ) {
		return;
	}
	byte buffer[128];
	idBitMsg msg;
	msg.Init( buffer, sizeof( buffer ) );
	idAsyncNetwork::WriteUserCmdDelta( msg, cmd, NULL );
	byte body[4 + sizeof( buffer )];
	const int frame = LittleLong( cmd.gameFrame );
	memcpy( body, &frame, 4 );
	memcpy( body + 4, msg.GetData(), msg.GetSize() );
	WriteClientRecord( MVD_RECORD_CLIENT_USERCMD, body, 4 + msg.GetSize(), false );
}

void idMultiViewDemo::OnClientGameEnded( const char *reason ) {
	if ( state == MVD_RECORDING && clientView ) {
		StopRecording( reason, true );
	}
}

/*
==================
idMultiViewDemo::CommitRecording
==================
*/
bool idMultiViewDemo::CommitRecording() {
	if ( fileSystem->PromoteFile( tempFileName.c_str(), fileName.c_str(),
			"fs_savepath" ) ) {
		return true;
	}
	common->Warning( "Could not finalize MVD '%s'; recoverable stream remains at '%s'",
		fileName.c_str(), tempFileName.c_str() );
	return false;
}

/*
==================
idMultiViewDemo::StopRecording
==================
*/
bool idMultiViewDemo::StopRecording( const char *reason, bool finalize,
		serverMVDResultReason_t failureReason ) {
	if ( state != MVD_RECORDING || finalizing ) {
		return false;
	}
	finalizing = true;

	bool clean = finalize;
	serverMVDResultReason_t terminalReason = finalize ?
		SERVER_MVD_REASON_NONE : failureReason;
	if ( finalize && file != NULL ) {
		clean = WriteIndexRecord() && WriteNavigationRecords();
		// openQ4 MP-22: who was seen, for the library to show and search
		clean = clean && WriteSummaryRecord();
		idFile_Memory payload( "MVD end" );
		int endFrame = idAsyncNetwork::server.GetGameFrame();
		int endTime = idAsyncNetwork::server.GetGameTime();
		if ( clientView ) {
			ClientStamp( endFrame, endTime );
		}
		payload.WriteInt( endFrame );
		payload.WriteInt( endTime );
		payload.WriteInt( snapshotCount );
		payload.WriteInt( reliableCount );
		payload.WriteInt( recordCount );
		if ( clean ) {
			clean = WriteRecord( MVD_RECORD_END, 1, 0, payload.GetDataPtr(), payload.Length(), false );
		}
		if ( !clean ) {
			terminalReason = SERVER_MVD_REASON_FINALIZE_WRITE_FAILED;
		} else if ( !file->Sync() ) {
			clean = false;
			terminalReason = SERVER_MVD_REASON_SYNC_FAILED;
		}
	}

	if ( !clientView ) {
		game->SetDemoState( DEMO_NONE, false, false );
	}
	const int finalBytes = file != NULL ? file->Length() : 0;
	if ( file != NULL ) {
		fileSystem->CloseFile( file );
		file = NULL;
	}

	bool committed = false;
	if ( clean ) {
		committed = CommitRecording();
		if ( !committed ) {
			terminalReason = SERVER_MVD_REASON_PROMOTE_FAILED;
		}
	}
	if ( committed ) {
		SetRecordingResult( SERVER_MVD_RESULT_COMMITTED,
			SERVER_MVD_REASON_NONE, fileName.c_str(), NULL );
		idStr size;
		size.BestUnit( "%.2f", static_cast<float>( finalBytes ), MEASURE_SIZE );
		common->Printf( "Stopped MVD recording (%s): '%s', %d snapshots, %s\n",
			reason ? reason : "complete", fileName.c_str(), snapshotCount, size.c_str() );
	} else {
		SetRecordingResult( SERVER_MVD_RESULT_FAILED, terminalReason,
			NULL, tempFileName.c_str() );
	}
	if ( !clean ) {
		common->Warning( "MVD recording stopped without a clean end marker (%s); partial data remains at '%s'",
			reason ? reason : "write failure", tempFileName.c_str() );
	}

	Clear();
	return committed;
}

/*
==================
idMultiViewDemo::CaptureReliableMessage
==================
*/
void idMultiViewDemo::CaptureReliableMessage( const idBitMsg &msg, int routeType, int routeClient, int routeInstance ) {
	if ( state != MVD_RECORDING || finalizing ) {
		return;
	}
	if ( routeType < DEMO_RECORD_CLIENTNUM || routeType >= DEMO_RECORD_COUNT ) {
		common->Warning( "Ignoring invalid MVD reliable route type %d", routeType );
		return;
	}
	if ( routeClient < -1 || routeClient >= MAX_ASYNC_CLIENTS ||
		 ( routeType == DEMO_RECORD_INSTANCE &&
		   ( routeInstance < 0 || routeInstance >= MVD_MAX_INSTANCES ) ) ||
		 ( routeType != DEMO_RECORD_INSTANCE && routeInstance != -1 ) ||
		 msg.GetSize() <= 0 || msg.GetSize() > MVD_MAX_GAME_MESSAGE_BYTES ) {
		common->Warning( "Ignoring invalid MVD reliable route/client/instance or payload size (%d, %d, %d, %d)",
			routeType, routeClient, routeInstance, msg.GetSize() );
		return;
	}

	idFile_Memory payload( "MVD reliable" );
	payload.WriteInt( idAsyncNetwork::server.GetGameFrame() );
	payload.WriteInt( idAsyncNetwork::server.GetGameTime() );
	payload.WriteUnsignedChar( static_cast<unsigned char>( routeType ) );
	payload.WriteChar( static_cast<char>( routeClient ) );
	payload.WriteChar( static_cast<char>( routeInstance ) );
	payload.Write( msg.GetData(), msg.GetSize() );

	if ( !WriteRecord( MVD_RECORD_RELIABLE, 2, MVD_RECORD_FLAG_REQUIRED, payload.GetDataPtr(), payload.Length() ) ) {
		const idStr failure = lastError;
		StopRecording( failure.c_str(), false );
		return;
	}
	reliableCount++;
}

/*
==================
idMultiViewDemo::CaptureServerFrame
==================
*/
void idMultiViewDemo::CaptureServerFrame( int gameFrame, int gameTime ) {
	if ( state != MVD_RECORDING || finalizing ) {
		return;
	}
	TakeRecordingThumbnail();

	const int maxMinutes = mvd_maxDurationMinutes.GetInteger();
	if ( maxMinutes > 0 && Sys_Milliseconds() - recordingStartRealTime >= maxMinutes * 60 * 1000 ) {
		StopRecording( "duration limit reached", true );
		return;
	}
	// openQ4: paced on the frames' real times. The game clock counts 16 ms a frame
	// where frames are 16.67 ms apart, so 50 ms took four frames, not three, and
	// the default delay recorded 15 snapshots a second instead of 20.
	if ( snapshotSequence > 0 &&
		common->GetUserCmdTime( gameFrame ) - common->GetUserCmdTime( lastSnapshotGameFrame ) <
			static_cast<int>( header.snapshotDelay ) ) {
		return;
	}

	const int snapshotBytes = idMath::ClampInt( 1, 16, mvd_maxSnapshotMB.GetInteger() ) * 1024 * 1024;
	idList<byte> buffer;
	buffer.SetNum( snapshotBytes );
	idBitMsg msg;
	msg.Init( buffer.Ptr(), buffer.Num() );
	msg.SetAllowOverflow( true );
	msg.BeginWriting();

	const int sequence = snapshotSequence + 1;
	game->ServerWriteServerDemoSnapshot( sequence, msg, lastSnapshotGameFrame, true );
	if ( msg.IsOverflowed() ) {
		lastError = va( "MVD snapshot exceeded %d MiB", mvd_maxSnapshotMB.GetInteger() );
		StopRecording( lastError.c_str(), false );
		return;
	}

	idFile_Memory payload( "MVD snapshot" );
	payload.WriteInt( gameFrame );
	payload.WriteInt( gameTime );
	payload.WriteInt( sequence );
	payload.Write( msg.GetData(), msg.GetSize() );
	const int snapshotOffset = file->Tell();
	if ( !WriteRecord( MVD_RECORD_SNAPSHOT, 2, MVD_RECORD_FLAG_REQUIRED, payload.GetDataPtr(), payload.Length() ) ) {
		const idStr failure = lastError;
		StopRecording( failure.c_str(), false );
		return;
	}

	snapshotSequence = sequence;
	snapshotCount++;
	lastSnapshotGameFrame = gameFrame;
	lastSnapshotGameTime = gameTime;
	lastSnapshotFileOffset = snapshotOffset;
	if ( recordingIndex.Num() == 0 ||
		 ( gameTime - recordingIndex[recordingIndex.Num() - 1].gameTime >= MVD_INDEX_INTERVAL_MS &&
		   recordingIndex.Num() < MVD_MAX_INDEX_ENTRIES ) ) {
		indexEntry_t index;
		index.gameTime = gameTime;
		index.fileOffset = snapshotOffset;
		index.sequence = sequence;
		recordingIndex.Append( index );
	}

	const int keyframeInterval = mvd_keyframeInterval.GetInteger();
	if ( keyframeInterval > 0 && !keyframeWritesDisabled &&
		 static_cast<int>( keyframes.size() ) < openq4_mvd::MAX_KEYFRAMES &&
		 gameTime - lastKeyframeGameTime >= keyframeInterval * 1000 ) {
		WriteKeyframeRecord( gameFrame, gameTime );
	}
}

/*
==================
idMultiViewDemo::WriteKeyframeRecord

Written immediately after a snapshot. It holds what a recording started at this
moment would hold (map state with current user info, and the game's network
info) plus the delta baseline the recorder will encode the next snapshot
against. Restoring it lets playback decode that snapshot exactly as a
continuous replay would. A keyframe the game cannot produce is skipped: it is
an optional record, and without it a seek only replays further.
==================
*/
bool idMultiViewDemo::WriteKeyframeRecord( int gameFrame, int gameTime ) {
	idFile_Memory mapState( "MVD keyframe map state" );
	WriteMapStatePayload( &mapState );
	idFile_Memory networkInfo( "MVD keyframe network info" );
	game->WriteNetworkInfo( &networkInfo, MAX_ASYNC_CLIENTS );
	idFile_Memory baseline( "MVD keyframe baseline" );
	idStr error;
	if ( !game->ServerWriteServerDemoBaseline( snapshotSequence, &baseline ) ||
		 !ValidateNetworkStatePayload( reinterpret_cast<const byte *>( networkInfo.GetDataPtr() ),
			networkInfo.Length(), error ) ||
		 !openq4_mvd::ValidateBaseline( reinterpret_cast<const std::uint8_t *>( baseline.GetDataPtr() ),
			baseline.Length(), snapshotSequence, MVD_BASELINE_MAX_ENTITIES,
			MVD_BASELINE_MAX_STATE_BYTES, MVD_BASELINE_PVS_WORDS ) ) {
		common->Warning( "MVD keyframes disabled for this recording: the game produced no valid baseline%s%s",
			error.IsEmpty() ? "" : ": ", error.c_str() );
		keyframeWritesDisabled = true;
		return true;
	}

	idFile_Memory payload( "MVD keyframe" );
	payload.WriteInt( gameFrame );
	payload.WriteInt( gameTime );
	payload.WriteInt( snapshotSequence );
	payload.WriteInt( reliableCount );
	payload.WriteInt( recordCount );
	payload.WriteUnsignedInt( mapState.Length() );
	payload.Write( mapState.GetDataPtr(), mapState.Length() );
	payload.WriteUnsignedInt( networkInfo.Length() );
	payload.Write( networkInfo.GetDataPtr(), networkInfo.Length() );
	payload.WriteUnsignedInt( baseline.Length() );
	payload.Write( baseline.GetDataPtr(), baseline.Length() );
	if ( payload.Length() > MVD_MAX_RECORD_BYTES ) {
		common->Warning( "MVD keyframes disabled for this recording: a keyframe of %d bytes is too large", payload.Length() );
		keyframeWritesDisabled = true;
		return true;
	}

	const int keyframeOffset = file->Tell();
	if ( !WriteRecord( MVD_RECORD_KEYFRAME, 1, 0, payload.GetDataPtr(), payload.Length() ) ) {
		const idStr failure = lastError;
		StopRecording( failure.c_str(), false );
		return false;
	}
	openq4_mvd::Keyframe keyframe;
	keyframe.gameTime = gameTime;
	keyframe.fileOffset = keyframeOffset;
	keyframe.sequence = snapshotSequence;
	keyframes.push_back( keyframe );
	lastKeyframeGameTime = gameTime;
	return true;
}

/*
==================
idMultiViewDemo::CaptureUserInfo

The engine broadcasts user-info changes outside the game's reliable channel,
so a player who joins or renames during a recording would otherwise reach
playback without a name.
==================
*/
void idMultiViewDemo::CaptureUserInfo( int clientNum, const idDict &info ) {
	if ( state != MVD_RECORDING || finalizing || clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ) {
		return;
	}
	NoteRecordingPlayer( info );
	idFile_Memory payload( "MVD user info" );
	payload.WriteInt( idAsyncNetwork::server.GetGameFrame() );
	payload.WriteInt( idAsyncNetwork::server.GetGameTime() );
	payload.WriteUnsignedChar( static_cast<unsigned char>( clientNum ) );
	info.WriteToFileHandle( &payload );
	if ( payload.Length() > MVD_MAX_USERINFO_BYTES ) {
		common->Warning( "Ignoring oversized MVD user info for client %d", clientNum );
		return;
	}
	if ( !WriteRecord( MVD_RECORD_USERINFO, 1, 0, payload.GetDataPtr(), payload.Length() ) ) {
		const idStr failure = lastError;
		StopRecording( failure.c_str(), false );
	}
}

/*
==================
idMultiViewDemo::RecordMarker
==================
*/
bool idMultiViewDemo::RecordMarker( const serverMVDMarker_t &input ) {
	if ( state != MVD_RECORDING || finalizing ) {
		return false;
	}
	openq4_mvd::Marker marker;
	int markerFrame = idAsyncNetwork::server.GetGameFrame();
	marker.gameTime = idAsyncNetwork::server.GetGameTime();
	if ( clientView ) {
		// openQ4 MP-22: a client-view recording's own clock
		ClientStamp( markerFrame, marker.gameTime );
	}
	marker.kind = input.kind;
	marker.client = input.client;
	marker.instance = input.instance;
	marker.value0 = input.value0;
	marker.value1 = input.value1;
	int length = 0;
	for ( ; length < openq4_mvd::MARKER_TEXT_BYTES && input.text[length] != '\0'; length++ ) {
		const unsigned char ch = static_cast<unsigned char>( input.text[length] );
		marker.text[length] = ( ch < 0x20 || ch == 0x7f ) ? ' ' : input.text[length];
	}
	marker.text[length] = '\0';
	if ( !openq4_mvd::MarkerFieldsValid( marker, SERVER_MVD_MARKER_KIND_COUNT, MAX_ASYNC_CLIENTS, MVD_MAX_INSTANCES ) ) {
		common->Warning( "Ignoring invalid MVD marker kind %d client %d instance %d",
			input.kind, input.client, input.instance );
		return false;
	}
	if ( static_cast<int>( markers.size() ) >= openq4_mvd::MAX_MARKERS ) {
		markersDropped++;
		return false;
	}

	std::vector<std::uint8_t> payload;
	openq4_mvd::AppendU32( payload, static_cast<std::uint32_t>( markerFrame ) );
	openq4_mvd::AppendMarkerBody( payload, marker );
	if ( !WriteRecord( MVD_RECORD_MARKER, 1, 0, payload.data(), static_cast<int>( payload.size() ) ) ) {
		const idStr failure = lastError;
		StopRecording( failure.c_str(), false );
		return false;
	}
	markers.push_back( marker );
	return true;
}

/*
==================
idMultiViewDemo::WriteNavigationRecords

The marker and keyframe directories sit between the timeline index and the
end record, where the indexed playback path already reads ahead for the end.
==================
*/
bool idMultiViewDemo::WriteNavigationRecords() {
	if ( !markers.empty() ) {
		std::vector<std::uint8_t> payload;
		openq4_mvd::AppendU32( payload, static_cast<std::uint32_t>( markers.size() ) );
		for ( const openq4_mvd::Marker &marker : markers ) {
			openq4_mvd::AppendMarkerBody( payload, marker );
		}
		if ( !WriteRecord( MVD_RECORD_MARKER_TABLE, 1, 0, payload.data(), static_cast<int>( payload.size() ), false ) ) {
			return false;
		}
	}
	if ( !keyframes.empty() ) {
		std::vector<std::uint8_t> payload;
		openq4_mvd::AppendKeyframeTable( payload, keyframes );
		if ( !WriteRecord( MVD_RECORD_KEYFRAME_TABLE, 1, 0, payload.data(), static_cast<int>( payload.size() ), false ) ) {
			return false;
		}
	}
	if ( markersDropped > 0 ) {
		common->Warning( "MVD dropped %d markers beyond its %d-marker limit", markersDropped, openq4_mvd::MAX_MARKERS );
	}
	return true;
}

/*
==================
idMultiViewDemo::NoteNavigationRecord

Collects a marker or keyframe met while scanning a stream that has no usable
directory: format 1.3 without a timeline index, a damaged index, or recovery.
False means the record is malformed; it is then simply not navigable.
==================
*/
bool idMultiViewDemo::NoteNavigationRecord( const record_t &record, int fileOffset ) {
	if ( record.version != 1 || ( record.flags & MVD_RECORD_FLAG_REQUIRED ) != 0 ) {
		return false;
	}
	if ( record.type == MVD_RECORD_MARKER ) {
		openq4_mvd::Marker marker;
		int gameFrame = 0;
		if ( static_cast<int>( markers.size() ) >= openq4_mvd::MAX_MARKERS ||
			 !openq4_mvd::ParseMarkerRecord( record.payload.Ptr(), record.payload.Num(), marker, gameFrame,
				SERVER_MVD_MARKER_KIND_COUNT, MAX_ASYNC_CLIENTS, MVD_MAX_INSTANCES ) ||
			 ( !markers.empty() && marker.gameTime < markers.back().gameTime ) ) {
			return false;
		}
		markers.push_back( marker );
		return true;
	}
	if ( record.type == MVD_RECORD_KEYFRAME || record.type == MVD_RECORD_CLIENT_KEYFRAME ) {
		openq4_mvd::KeyframeHeader keyframeHeader;
		if ( static_cast<int>( keyframes.size() ) >= openq4_mvd::MAX_KEYFRAMES ||
			 !openq4_mvd::ParseKeyframeHeader( record.payload.Ptr(), record.payload.Num(), keyframeHeader ) ||
			 ( !keyframes.empty() &&
			   ( keyframeHeader.gameTime <= keyframes.back().gameTime ||
				 keyframeHeader.sequence <= keyframes.back().sequence ) ) ) {
			return false;
		}
		openq4_mvd::Keyframe keyframe;
		keyframe.gameTime = keyframeHeader.gameTime;
		keyframe.fileOffset = fileOffset;
		keyframe.sequence = keyframeHeader.sequence;
		keyframes.push_back( keyframe );
		return true;
	}
	return false;
}

/*
==================
idMultiViewDemo::ParseNavigationTable
==================
*/
bool idMultiViewDemo::ParseNavigationTable( const record_t &record, int streamStart, int tableOffset ) {
	if ( record.version != 1 || ( record.flags & MVD_RECORD_FLAG_REQUIRED ) != 0 ) {
		return false;
	}
	if ( record.type == MVD_RECORD_MARKER_TABLE ) {
		return openq4_mvd::ParseMarkerTable( record.payload.Ptr(), record.payload.Num(), markers,
			SERVER_MVD_MARKER_KIND_COUNT, MAX_ASYNC_CLIENTS, MVD_MAX_INSTANCES );
	}
	if ( record.type == MVD_RECORD_KEYFRAME_TABLE ) {
		return openq4_mvd::ParseKeyframeTable( record.payload.Ptr(), record.payload.Num(),
			streamStart, tableOffset, keyframes );
	}
	return false;
}

/*
==================
idMultiViewDemo::OnServerMapChange
==================
*/
void idMultiViewDemo::OnServerMapChange() {
	if ( state == MVD_RECORDING ) {
		StopRecording( "map changed", true );
	}
}

/*
==================
idMultiViewDemo::BuildPlaybackIndex
==================
*/
bool idMultiViewDemo::BuildPlaybackIndex() {
	if ( file == NULL || playbackStreamOffset <= 0 ||
		 file->Seek( playbackStreamOffset, FS_SEEK_SET ) != 0 ) {
		lastError = "could not seek to the MVD live stream";
		return false;
	}

	playbackEndGameTime = header.startGameTime;
	markers.clear();
	keyframes.clear();
	bool sawHeaderIndex = header.indexOffset == 0;
	bool usableHeaderIndex = header.indexOffset == 0;
	idList<int> indexedTimes;
	idList<int> indexedOffsets;
	idList<int> indexedSequences;
	int indexedEndTime = 0;
	bool cleanEnd = false;

	// Version 1.1+ can answer the duration/count query without synchronously
	// reading an entire match. Every live record remains CRC-checked and
	// sequence-checked as it is consumed.
	if ( header.indexOffset != 0 &&
		 header.indexOffset >= static_cast<unsigned int>( playbackStreamOffset ) &&
		 header.indexOffset < static_cast<unsigned int>( file->Length() ) &&
		 file->Seek( static_cast<int>( header.indexOffset ), FS_SEEK_SET ) == 0 ) {
		const int indexRecordOffset = file->Tell();
		record_t indexRecord;
		const readResult_t indexResult = ReadRecord( file, indexRecord );
		sawHeaderIndex =
			indexResult == MVD_READ_OK &&
			indexRecord.type == MVD_RECORD_INDEX;
		usableHeaderIndex =
			sawHeaderIndex &&
			indexRecord.version == 1 &&
			( indexRecord.flags & MVD_RECORD_FLAG_REQUIRED ) == 0 &&
			MVD_ParseTimelineIndex(
				indexRecord.payload,
				playbackStreamOffset,
				indexRecordOffset,
				indexedTimes,
				indexedOffsets,
				indexedSequences,
				indexedEndTime );

		for ( int i = 0; usableHeaderIndex && !cleanEnd && i < 64; i++ ) {
			record_t record;
			const readResult_t result = ReadRecord( file, record );
			if ( result != MVD_READ_OK || !RecordIsSupported( record, false ) ) {
				usableHeaderIndex = false;
				break;
			}
			if ( record.type != MVD_RECORD_END ) {
				if ( record.flags & MVD_RECORD_FLAG_REQUIRED ) {
					usableHeaderIndex = false;
				} else if ( ( record.type == MVD_RECORD_MARKER_TABLE || record.type == MVD_RECORD_KEYFRAME_TABLE ) &&
					!ParseNavigationTable( record, playbackStreamOffset, indexRecordOffset ) ) {
					// A damaged directory loses navigation, never playback.
					common->Warning( "MVD '%s' has an invalid %s directory; it is ignored", fileName.c_str(),
						record.type == MVD_RECORD_MARKER_TABLE ? "marker" : "keyframe" );
				}
				continue;
			}
			if ( record.version != 1 || record.payload.Num() != 20 ) {
				usableHeaderIndex = false;
				break;
			}
			const int endTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
			const int reportedSnapshots = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 8 ) );
			const int reportedReliables = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 12 ) );
			const int reportedRecords = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 16 ) );
			if ( endTime < header.startGameTime || endTime < indexedEndTime ||
				 reportedSnapshots < indexedSequences.Num() ||
				 ( indexedSequences.Num() > 0 &&
				   indexedSequences[indexedSequences.Num() - 1] > reportedSnapshots ) ||
				 reportedReliables < 0 ||
				 reportedRecords < reportedSnapshots + reportedReliables ) {
				usableHeaderIndex = false;
				break;
			}
			playbackEndGameTime = endTime;
			cleanEnd = true;
		}
	}

	if ( cleanEnd && usableHeaderIndex ) {
		if ( file->Seek( playbackStreamOffset, FS_SEEK_SET ) != 0 ) {
			lastError = "could not rewind the indexed MVD stream";
			return false;
		}
		return true;
	}

	if ( header.indexOffset != 0 ) {
		common->Warning( "MVD '%s' has an invalid optional timeline index; validating its live stream",
			fileName.c_str() );
		header.indexOffset = 0;
	}
	markers.clear();
	keyframes.clear();
	if ( file->Seek( playbackStreamOffset, FS_SEEK_SET ) != 0 ) {
		lastError = "could not rewind the MVD stream for validation";
		return false;
	}

	// Format 1.0 and damaged optional indexes use the bounded compatibility
	// path. This never stores one heap entry per snapshot.
	cleanEnd = false;
	int validatedRecords = 0;
	int validatedSnapshots = 0;
	int validatedReliables = 0;
	int lastSequence = 0;
	int lastRecordFrame = header.startGameFrame;
	int lastRecordTime = header.startGameTime;
	playbackEndGameTime = header.startGameTime;
	while ( validatedRecords < MVD_MAX_STREAM_RECORDS ) {
		const int recordOffset = file->Tell();
		record_t record;
		const readResult_t result = ReadRecord( file, record );
		if ( result == MVD_READ_EOF ) {
			break;
		}
		if ( result == MVD_READ_ERROR || !RecordIsSupported( record, true ) ) {
			return false;
		}
		validatedRecords++;

		if ( MVD_RecordShouldSkip( record.type, record.version, record.flags ) ) {
			continue;
		}
		if ( cleanEnd ) {
			lastError = va( "MVD contains record %u after its end marker", record.type );
			return false;
		}
		if ( record.type == MVD_RECORD_MARKER || record.type == MVD_RECORD_KEYFRAME ||
			 ( clientView && record.type == MVD_RECORD_CLIENT_KEYFRAME ) ) {
			NoteNavigationRecord( record, recordOffset );
			continue;
		}
		if ( clientView ? ( record.type == MVD_RECORD_RELIABLE || record.type == MVD_RECORD_SNAPSHOT ) :
			 MVD_IsClientRecord( record.type ) ) {
			lastError = va( "MVD record %u does not belong to this recording's view", record.type );
			return false;
		}
		if ( MVD_IsClientRecord( record.type ) ) {
			if ( !MVD_ScanClientRecord( record.type, record.payload, lastRecordFrame, lastRecordTime,
					playbackEndGameTime, validatedSnapshots, validatedReliables, lastError ) ) {
				return false;
			}
			continue;
		}

		if ( record.type == MVD_RECORD_RELIABLE ) {
			if ( !MVD_ValidateReliablePayload( record.payload, record.version, lastError, retailSource ) ) {
				return false;
			}
			const int gameFrame = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) );
			const int gameTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
			if ( gameFrame < lastRecordFrame || gameTime < lastRecordTime ) {
				lastError = "out-of-order MVD reliable timestamp";
				return false;
			}
			lastRecordFrame = gameFrame;
			lastRecordTime = gameTime;
			validatedReliables++;
		} else if ( record.type == MVD_RECORD_SNAPSHOT ) {
			if ( !MVD_ValidateSnapshotPayload( record.payload, record.version, lastError ) ) {
				return false;
			}
			const int gameFrame = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) );
			const int gameTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
			const int sequence = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 8 ) );
			if ( sequence != lastSequence + 1 || gameFrame < lastRecordFrame ||
				 gameTime < lastRecordTime ) {
				lastError = va( "invalid MVD timeline at snapshot %d", sequence );
				return false;
			}
			lastSequence = sequence;
			lastRecordFrame = gameFrame;
			lastRecordTime = gameTime;
			playbackEndGameTime = gameTime;
			validatedSnapshots++;
		} else if ( record.type == MVD_RECORD_END ) {
			if ( record.payload.Num() != 20 ) {
				lastError = "invalid MVD end record while validating";
				return false;
			}
			const int endTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
			const int reportedSnapshots = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 8 ) );
			const int reportedReliables = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 12 ) );
			const int reportedRecords = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 16 ) );
			const int endFrame = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) );
			if ( endFrame < lastRecordFrame || endTime < lastRecordTime ||
				 reportedSnapshots != validatedSnapshots ||
				 reportedReliables != validatedReliables ||
				 reportedRecords != playbackInitializationRecordCount + validatedRecords - 1 ) {
				lastError = "MVD end record does not match the validated stream";
				return false;
			}
			playbackEndGameTime = endTime;
			cleanEnd = true;
		}
	}

	if ( validatedRecords >= MVD_MAX_STREAM_RECORDS ) {
		lastError = "MVD contains too many records for the compatibility scan";
		return false;
	}
	if ( !cleanEnd ) {
		lastError = "MVD stream ended without a clean end marker";
		return false;
	}
	if ( file->Seek( playbackStreamOffset, FS_SEEK_SET ) != 0 ) {
		lastError = "could not rewind the validated MVD stream";
		return false;
	}
	return true;
}

/*
==================
idMultiViewDemo::StartPlayback
==================
*/
bool idMultiViewDemo::StartPlayback( const idCmdArgs &args ) {
	if ( state != MVD_IDLE ) {
		common->Printf( "MVD system is already busy\n" );
		return false;
	}
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: %s <demo>\n", args.Argv( 0 ) );
		return false;
	}

	fileName = BuildPlaybackName( args.Argv( 1 ) );
	if ( fileName.IsEmpty() ) {
		common->Warning( "Unsafe or empty MVD path" );
		Clear();
		return false;
	}
	if ( openq4_retail::IsRetailNetDemoName( fileName.c_str() ) ) {
		// openQ4 MP-22: a retail Quake 4 network demo, decoded into records first
		if ( !OpenRetailNetDemo() ) {
			common->Warning( "Cannot play '%s': %s", fileName.c_str(), lastError.c_str() );
			openq4_retail::ClearDeclRemap();
			Clear();
			return false;
		}
	} else {
		file = fileSystem->OpenFileRead( fileName.c_str(), true );
	}
	if ( file == NULL ) {
		common->Printf( "MVD file not found: %s\n", fileName.c_str() );
		Clear();
		return false;
	}

	if ( !ReadHeader( file, header ) ) {
		common->Warning( "Cannot play '%s': %s", fileName.c_str(), lastError.c_str() );
		fileSystem->CloseFile( file );
		Clear();
		return false;
	}
	if ( !game->ValidateDemoProtocol( ASYNC_PROTOCOL_MINOR, header.protocolMinor ) ) {
		common->Warning( "MVD network protocol %u.%u is not accepted by this game module",
			header.protocolMajor, header.protocolMinor );
		fileSystem->CloseFile( file );
		Clear();
		return false;
	}
	const bool gameSchemaCompatible =
		header.formatMinor <= 1
			? MVD_IsLegacyGameAPICompatible( header.gameSchemaVersion )
			: game->IsMVDSchemaCompatible(
				MVD_SchemaMajor( header.gameSchemaVersion ),
				MVD_SchemaMinor( header.gameSchemaVersion ) );
	if ( !gameSchemaCompatible ) {
		if ( header.formatMinor <= 1 ) {
			common->Warning( "Legacy MVD game API %u is incompatible with this game module API %u",
				header.gameSchemaVersion, static_cast<unsigned int>( GAME_API_VERSION ) );
		} else {
			common->Warning( "MVD game schema %d.%d is incompatible with this game module",
				MVD_SchemaMajor( header.gameSchemaVersion ),
				MVD_SchemaMinor( header.gameSchemaVersion ) );
		}
		fileSystem->CloseFile( file );
		Clear();
		return false;
	}
	if ( header.usercmdHz != static_cast<unsigned int>( common->GetUserCmdHz() ) ) {
		common->Warning( "MVD simulation rate %u Hz is incompatible with the current %d Hz",
			header.usercmdHz, common->GetUserCmdHz() );
		fileSystem->CloseFile( file );
		Clear();
		return false;
	}
	if ( mvd_enforceContent.GetBool() && header.contentChecksum != static_cast<unsigned int>( declManager->GetChecksum() ) ) {
		common->Warning( "MVD content checksum %08x differs from the current content %08x; set mvd_enforceContent 0 to override",
			header.contentChecksum, static_cast<unsigned int>( declManager->GetChecksum() ) );
		// openQ4 MP-22: which pack, from the list the recording kept
		record_t metadataRecord;
		mvdFileInfo_t described;
		const bool haveMetadata = ReadRecord( file, metadataRecord ) == MVD_READ_OK &&
			ReadMetadataRecord( metadataRecord, described );
		const idStr packs = MVD_PackDifference( haveMetadata ? described.packs.c_str() : NULL );
		common->Warning( "%s", packs.c_str() );
		fileSystem->CloseFile( file );
		Clear();
		return false;
	}

	record_t mapState;
	record_t networkState;
	bool haveMapState = false;
	bool haveNetworkState = false;
	playbackInitializationRecordCount = 0;
	while ( !haveMapState || !haveNetworkState ) {
		if ( playbackInitializationRecordCount >= MVD_MAX_INITIALIZATION_RECORDS ) {
			lastError = "MVD contains too many initialization records";
			common->Warning( "Cannot play '%s': %s", fileName.c_str(), lastError.c_str() );
			fileSystem->CloseFile( file );
			Clear();
			return false;
		}
		record_t record;
		const readResult_t result = ReadRecord( file, record );
		if ( result != MVD_READ_OK ) {
			if ( result == MVD_READ_EOF ) {
				lastError = "MVD ended before initialization completed";
			}
			common->Warning( "Cannot play '%s': %s", fileName.c_str(), lastError.c_str() );
			fileSystem->CloseFile( file );
			Clear();
			return false;
		}
		playbackInitializationRecordCount++;
		if ( !RecordIsSupported( record, false ) ) {
			common->Warning( "Cannot play '%s': %s", fileName.c_str(), lastError.c_str() );
			fileSystem->CloseFile( file );
			Clear();
			return false;
		}
		if ( MVD_RecordShouldSkip( record.type, record.version, record.flags ) ) {
			continue;
		}
		if ( record.type == MVD_RECORD_MAP_STATE ) {
			if ( haveMapState ) {
				lastError = "duplicate MVD map-state record";
				common->Warning( "Cannot play '%s': %s", fileName.c_str(), lastError.c_str() );
				fileSystem->CloseFile( file );
				Clear();
				return false;
			}
			mapState = record;
			haveMapState = true;
		} else if ( record.type == MVD_RECORD_NETWORK_STATE ) {
			if ( haveNetworkState ) {
				lastError = "duplicate MVD network-state record";
				common->Warning( "Cannot play '%s': %s", fileName.c_str(), lastError.c_str() );
				fileSystem->CloseFile( file );
				Clear();
				return false;
			}
			networkState = record;
			haveNetworkState = true;
		} else if ( MVD_IsLiveRecord( record.type ) ) {
			lastError = "MVD stream began before initialization completed";
			common->Warning( "Cannot play '%s': %s", fileName.c_str(), lastError.c_str() );
			fileSystem->CloseFile( file );
			Clear();
			return false;
		}
	}

	playbackMapState = mapState;
	playbackNetworkState = networkState;
	clientView = ( header.requiredFeatures & MVD_FEATURE_CLIENT_VIEW ) != 0;
	if ( clientView ? !( networkState.version == MVD_NETWORK_STATE_CLIENT_VERSION &&
			ValidateClientNetworkState( networkState.payload.Ptr(), networkState.payload.Num(), lastError ) ) :
		 !ValidateNetworkStateRecord( networkState, lastError ) ) {
		if ( lastError.IsEmpty() ) {
			lastError = "MVD network state does not match the recording's view";
		}
		common->Warning( "Cannot play '%s': %s", fileName.c_str(), lastError.c_str() );
		fileSystem->CloseFile( file );
		Clear();
		return false;
	}
	playbackStreamOffset = file->Tell();
	if ( !BuildPlaybackIndex() ) {
		common->Warning( "Cannot play '%s': %s", fileName.c_str(), lastError.c_str() );
		fileSystem->CloseFile( file );
		Clear();
		return false;
	}

	session->Stop();
	state = MVD_PLAYING;
	if ( !ReadMapStateRecord( mapState ) ) {
		common->Warning( "Cannot initialize '%s': %s", fileName.c_str(), lastError.c_str() );
		StopPlayback( "map-state failure" );
		return false;
	}

	if ( !ApplyPlaybackNetworkState( networkState, "initial" ) ) {
		const idStr failure = lastError;
		FinishPlayback( failure.c_str(), true );
		return false;
	}

	playbackLastRealTime = Sys_Milliseconds();
	playbackGameTime = header.startGameTime;
	latestSnapshotGameFrame = header.startGameFrame;
	latestSnapshotGameTime = header.startGameTime;
	predictionGameFrame = header.startGameFrame;
	playbackLastRecordGameFrame = header.startGameFrame;
	playbackLastRecordGameTime = header.startGameTime;
	snapshotSequence = 0;
	snapshotCount = 0;
	reliableCount = 0;
	recordCount = playbackInitializationRecordCount;
	haveSnapshot = false;
	havePendingRecord = false;
	sawCleanEnd = false;
	if ( clientView ) {
		// the client's own frames go on from the snapshot it last read
		latestSnapshotGameFrame = clientStartSnapshotFrame;
		latestSnapshotGameTime = clientStartSnapshotTime;
		latestSnapshotClientFrame = clientStartGameFrame;
		latestSnapshotStampTime = header.startGameTime;
		predictionGameFrame = clientStartSnapshotFrame;
		snapshotSequence = clientStartSequence;
		haveSnapshot = true;
	}
	mvd_scale.SetFloat( 1.0f );
	mvd_paused.SetBool( false );
	if ( sessLocal.sw != NULL ) {
		sessLocal.sw->SetSlowmoSpeed( 1.0f );
		if ( sessLocal.sw->IsPaused() ) {
			sessLocal.sw->UnPause();
		}
	}
	common->Printf( "Playing %s '%s' (format %u.%u, protocol %u.%u)\n",
		clientView ? va( "the recorded view of client %d", viewClient ) : "multi-view demo",
		fileName.c_str(), header.formatMajor, header.formatMinor, header.protocolMajor, header.protocolMinor );
	return true;
}

/*
==================
idMultiViewDemo::ApplyPlaybackNetworkState

Loads the recorded map and gives the game its starting state, for the start of
playback and for every rewind. openQ4 MP-22: a client-view recording plays as
its recorder's client, from the engine's part of its network state.
==================
*/
bool idMultiViewDemo::ApplyPlaybackNetworkState( const record_t &networkState, const char *context ) {
	int prefix = 0;
	int clientGameTime = header.startGameTime;
	if ( clientView ) {
		const byte *bytes = networkState.payload.Ptr();
		const int length = networkState.payload.Num();
		if ( length < 25 ) {
			lastError = "short MVD client network state";
			return false;
		}
		viewClient = bytes[0];
		clientStartSequence = static_cast<int>( MVD_ReadLittleUInt( bytes + 1 ) );
		clientStartSnapshotFrame = static_cast<int>( MVD_ReadLittleUInt( bytes + 5 ) );
		clientStartSnapshotTime = static_cast<int>( MVD_ReadLittleUInt( bytes + 9 ) );
		clientStartGameFrame = static_cast<int>( MVD_ReadLittleUInt( bytes + 13 ) );
		clientGameTime = static_cast<int>( MVD_ReadLittleUInt( bytes + 17 ) );
		const int commands = static_cast<int>( MVD_ReadLittleUInt( bytes + 21 ) );
		prefix = 25;
		memset( playbackUsercmds, 0, sizeof( playbackUsercmds ) );
		const int ticMsec = Max( 1, common->GetUserCmdMSec() );
		for ( int i = 0; i < commands; i++ ) {
			// ValidateClientNetworkState checked every length
			const int frame = static_cast<int>( MVD_ReadLittleUInt( bytes + prefix ) );
			const int client = bytes[ prefix + 4 ];
			const int size = bytes[ prefix + 5 ] | ( bytes[ prefix + 6 ] << 8 );
			idBitMsg msg;
			msg.Init( bytes + prefix + 7, size );
			msg.SetSize( size );
			msg.BeginReading();
			usercmd_t cmd;
			if ( idAsyncNetwork::ReadUserCmdDelta( msg, cmd, NULL ) ) {
				cmd.gameFrame = frame;
				cmd.gameTime = frame * ticMsec;
				cmd.duplicateCount = 0;
				playbackUsercmds[ frame & ( MAX_USERCMD_BACKUP - 1 ) ][ client ] = cmd;
			}
			prefix += 7 + size;
		}
	}

	cvarSystem->SetCVarsFromDictByFlags( sessLocal.mapSpawnData.syncedCVars, CVAR_NETWORKSYNC );
	sessLocal.ExecuteMapChange();
	game->SetDemoState( DEMO_PLAYING, !clientView, false );
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		game->SetUserInfo( i, sessLocal.mapSpawnData.userInfo[i], true );
	}

	idFile_Memory initialState( "MVD network state",
		reinterpret_cast<const char *>( networkState.payload.Ptr() + prefix ), networkState.payload.Num() - prefix );
	game->ReadNetworkInfo( clientView ? clientGameTime : header.startGameTime, &initialState,
		clientView ? viewClient : MAX_ASYNC_CLIENTS );
	if ( initialState.Tell() != initialState.Length() ) {
		lastError = va( "MVD %s network state left %d unread bytes", context,
			initialState.Length() - initialState.Tell() );
		return false;
	}
	return true;
}

bool idMultiViewDemo::IsClientViewPlayback() const {
	return state == MVD_PLAYING && clientView;
}

/*
==================
idMultiViewDemo::StartTimeDemo

openQ4 MP-22: plays a multi-view demo, or a recording of one player's view, as
a benchmark: every frame advances the match by one simulation tic whatever
the time it took, frames are not capped, and the end reports the frame rate
and the slowest frame. The same file gives the same frames on every run.
==================
*/
bool idMultiViewDemo::StartTimeDemo( const char *name, bool quitAfter ) {
	idCmdArgs args;
	args.AppendArg( "timeDemo" );
	args.AppendArg( name );
	if ( !StartPlayback( args ) ) {
		return false;
	}
	timeDemo = true;
	timeDemoQuit = quitAfter;
	timeDemoFrames = 0;
	timeDemoStartTime = 0;
	timeDemoLastFrameTime = 0;
	timeDemoSlowestFrame = 0;
	common->Printf( "Timing '%s': one simulation tic a frame, uncapped\n", fileName.c_str() );
	return true;
}

bool idMultiViewDemo::IsTimeDemo() const {
	// an export also runs uncapped
	return state == MVD_PLAYING && ( timeDemo || exporting );
}

/*
==================
idMultiViewDemo::StartExport

openQ4 MP-22: writes a multi-view demo, or a recording of one player's view,
as a PNG sequence, exports/<demo>/frame00000.png on: every frame moves the
match on by 1000/fps milliseconds (in whole milliseconds that add up, 33, 33
and 34 at 30 a second), whatever the time it took, and the next frame's command
buffer takes the screenshot of the state it shows.

The sound track, exports/<demo>/<demo>.wav, comes from a loopback mixer the
sound system switches to before the map loads (SoundCapture.h): each exported
frame mixes exactly its share, so n pictures last as long as the track's
n * 1000/fps milliseconds. The playback device opens again when the export ends.
==================
*/
bool idMultiViewDemo::StartExport( const char *name, int fps, bool quitAfter ) {
	if ( state != MVD_IDLE ) {
		common->Printf( "MVD system is already busy\n" );
		return false;
	}
	idStr base = BuildPlaybackName( name );
	base.StripPath();
	base.StripFileExtension();
	const idStr directory = va( "exports/%s", base.c_str() );
	const idStr soundTrack = va( "%s/%s.wav", directory.c_str(), base.c_str() );
	// before the map loads, so its sounds load straight onto the loopback mixer
	char soundRefusal[160];
	const bool sound = !base.IsEmpty() && Sound_BeginCapture( soundTrack.c_str(), soundRefusal, sizeof( soundRefusal ) );

	idCmdArgs args;
	args.AppendArg( "demoExport" );
	args.AppendArg( name );
	if ( !StartPlayback( args ) ) {
		if ( sound ) {
			soundCaptureResult_t discarded;
			Sound_EndCapture( discarded );
			fileSystem->RemoveFile( soundTrack.c_str() );
		}
		return false;
	}
	exporting = true;
	exportSound = sound;
	timeDemoQuit = quitAfter;
	exportFps = idMath::ClampInt( 1, 120, fps );
	exportSteps = 0;
	exportFrames = 0;
	exportDirectory = directory;
	common->Printf( "Exporting '%s' to %s/ at %d frames a second\n", fileName.c_str(), exportDirectory.c_str(),
		exportFps );
	if ( !sound ) {
		common->Printf( "The export has no sound track: %s\n", base.IsEmpty() ? "no demo name" : soundRefusal );
		common->Printf( "MVD_EXPORT_SOUND written=0 reason=\"%s\"\n", base.IsEmpty() ? "no demo name" : soundRefusal );
	}
	return true;
}

/*
==================
idMultiViewDemo::FinishExportSound

Finishes the sound track of an export, however the export ended, and opens
the playback device again.
==================
*/
void idMultiViewDemo::FinishExportSound() {
	if ( !exportSound ) {
		return;
	}
	exportSound = false;
	soundCaptureResult_t track;
	if ( !Sound_EndCapture( track ) ) {
		return;
	}
	if ( !track.written ) {
		common->Warning( "The export's sound track was not written: %s", track.reason );
		common->Printf( "MVD_EXPORT_SOUND written=0 reason=\"%s\"\n", track.reason );
		return;
	}
	common->Printf( "Wrote the sound track %s: %.2f seconds, %d Hz, %d channels\n", track.path,
		static_cast<double>( track.msec ) * 0.001, track.rate, track.channels );
	if ( !track.complete ) {
		common->Warning( "The export's sound track is not complete: %s", track.reason );
	}
	common->Printf( "MVD_EXPORT_SOUND written=1 file=%s rate=%d channels=%d frames=%lld msec=%lld peak=%d rms=%.1f complete=%d\n",
		track.path, track.rate, track.channels, static_cast<long long>( track.frames ), static_cast<long long>( track.msec ),
		track.peak, track.rms, track.complete ? 1 : 0 );
}

void idMultiViewDemo::Export_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "usage: demoExport <demo>.mvd [fps] [quit]\n" );
		return;
	}
	const int fps = args.Argc() > 2 ? atoi( args.Argv( 2 ) ) : 30;
	const bool quitAfter = args.Argc() > 3 && !idStr::Icmp( args.Argv( 3 ), "quit" );
	idAsyncNetwork::multiViewDemo.StartExport( args.Argv( 1 ), fps, quitAfter );
}

int idMultiViewDemo::GetPlaybackLocalClient() const {
	return clientView ? viewClient : MAX_ASYNC_CLIENTS;
}

/*
==================
idMultiViewDemo::ProcessClientPlaybackRecord

openQ4 MP-22: a client-view recording's records, given to the game as the
connected client gave them (idAsyncClient::ProcessUnreliableServerMessage and
ProcessReliableServerMessages).
==================
*/
bool idMultiViewDemo::ProcessClientPlaybackRecord( const record_t &record, bool &handled ) {
	handled = MVD_IsClientRecord( record.type );
	if ( !handled ) {
		return true;
	}
	const byte *bytes = record.payload.Ptr();
	const int stampFrame = static_cast<int>( MVD_ReadLittleUInt( bytes ) );
	const int stampTime = static_cast<int>( MVD_ReadLittleUInt( bytes + 4 ) );
	const int ticMsec = Max( 1, common->GetUserCmdMSec() );

	if ( record.type == MVD_RECORD_CLIENT_SNAPSHOT ) {
		const int clientFrame = static_cast<int>( MVD_ReadLittleUInt( bytes + 8 ) );
		idBitMsg msg;
		msg.Init( bytes + 12, record.payload.Num() - 12 );
		msg.SetSize( record.payload.Num() - 12 );
		msg.BeginReading();
		msg.ReadLong();	// the server's game id, which matched when it was recorded
		const int id = msg.ReadByte();
		const int sequence = msg.ReadLong();
		const int gameFrame = msg.ReadLong();
		const int gameTime = msg.ReadLong();
		const int duplicated = msg.ReadByte();
		const int aheadOfServer = msg.ReadShort();
		msg.ReadShort();	// the command slack, which the client only logs
		if ( msg.IsReadOverflowed() || id != SERVER_UNRELIABLE_MESSAGE_SNAPSHOT || gameFrame < 0 || gameTime < 0 ||
			 clientFrame < 0 ) {
			lastError = "invalid MVD client snapshot header";
			return false;
		}
		if ( !game->ClientReadSnapshot( viewClient, sequence, gameFrame, gameTime, duplicated, aheadOfServer, msg ) ) {
			lastError = va( "game module rejected MVD client snapshot %d", sequence );
			return false;
		}
		// the other clients' commands the server relayed with the snapshot
		// (openQ4 MP-22: in Quake 4 1.4.2's codec for a retail recording, which
		// ends the list with any slot past the last client)
		usercmd_t *last = NULL;
		while ( true ) {
			const int client = msg.ReadByte();
			if ( msg.IsReadOverflowed() || ( client > MAX_ASYNC_CLIENTS && !retailSource ) ) {
				lastError = va( "MVD client snapshot %d has an invalid command list", sequence );
				return false;
			}
			if ( client >= MAX_ASYNC_CLIENTS ) {
				break;
			}
			const int count = msg.ReadByte();
			const int maxCount = retailSource ? openq4_retail::NETDEMO_MAX_RELAYED_CMDS : MAX_USERCMD_RELAY;
			if ( msg.IsReadOverflowed() || count < ( retailSource ? 0 : 1 ) || count > maxCount ) {
				lastError = va( "MVD client snapshot %d has an invalid command count", sequence );
				return false;
			}
			for ( int j = 0; j < count; j++ ) {
				usercmd_t &cmd = playbackUsercmds[ ( gameFrame + j ) & ( MAX_USERCMD_BACKUP - 1 ) ][ client ];
				if ( retailSource ? !openq4_retail::ReadRelayedUsercmd( msg, cmd, last ) :
					 !idAsyncNetwork::ReadUserCmdDelta( msg, cmd, last ) ) {
					lastError = va( "MVD client snapshot %d has a truncated command", sequence );
					return false;
				}
				cmd.gameFrame = gameFrame + j;
				cmd.duplicateCount = 0;
				if ( retailSource ) {
					cmd.gameTime = gameTime + j * ticMsec;	// 1.4.2 sends no command times
				}
				last = &cmd;
			}
			for ( int j = count; j < MAX_USERCMD_BACKUP; j++ ) {
				usercmd_t &cmd = playbackUsercmds[ ( gameFrame + j ) & ( MAX_USERCMD_BACKUP - 1 ) ][ client ];
				cmd.gameFrame = 0;
				cmd.gameTime = 0;
			}
		}
		snapshotSequence = sequence;
		latestSnapshotGameFrame = gameFrame;
		latestSnapshotGameTime = gameTime;
		latestSnapshotClientFrame = clientFrame;
		latestSnapshotStampTime = stampTime;
		predictionGameFrame = gameFrame;
		haveSnapshot = true;
		snapshotCount++;
	} else if ( record.type == MVD_RECORD_CLIENT_APPLY ) {
		const int sequence = static_cast<int>( MVD_ReadLittleUInt( bytes + 8 ) );
		if ( !game->ClientApplySnapshot( viewClient, sequence ) ) {
			lastError = va( "could not apply MVD client snapshot %d", sequence );
			return false;
		}
		reliableCount++;
	} else if ( record.type == MVD_RECORD_CLIENT_RELIABLE ) {
		idBitMsg msg;
		msg.Init( bytes + 8, record.payload.Num() - 8 );
		msg.SetSize( record.payload.Num() - 8 );
		msg.BeginReading();
		game->ClientProcessReliableMessage( viewClient, msg );
		reliableCount++;
	} else {
		const int frame = static_cast<int>( MVD_ReadLittleUInt( bytes + 8 ) );
		idBitMsg msg;
		msg.Init( bytes + 12, record.payload.Num() - 12 );
		msg.SetSize( record.payload.Num() - 12 );
		msg.BeginReading();
		usercmd_t cmd;
		if ( frame >= 0 && idAsyncNetwork::ReadUserCmdDelta( msg, cmd, NULL ) ) {
			cmd.gameFrame = frame;
			cmd.gameTime = frame * ticMsec;
			cmd.duplicateCount = 0;
			playbackUsercmds[ frame & ( MAX_USERCMD_BACKUP - 1 ) ][ viewClient ] = cmd;
		} else {
			common->Warning( "Ignoring a malformed MVD client command" );
		}
	}
	playbackLastRecordGameFrame = stampFrame;
	playbackLastRecordGameTime = stampTime;
	return true;
}

/*
==================
idMultiViewDemo::RunClientPrediction

openQ4 MP-22: idAsyncClient::RunFrame's prediction, from the last snapshot to
the recorder's frame at this point of the recording, with the commands it had.
==================
*/
void idMultiViewDemo::RunClientPrediction() {
	if ( !haveSnapshot ) {
		return;
	}
	const int ticMsec = Max( 1, common->GetUserCmdMSec() );
	int targetFrame = latestSnapshotClientFrame +
		Max( 0, static_cast<int>( playbackGameTime ) - latestSnapshotStampTime ) / ticMsec;
	if ( forcePresentationFrame ) {
		targetFrame = Max( targetFrame, predictionGameFrame + 1 );
	}
	const int maxPredictionFrames = 64;
	int predicted = 0;
	while ( predictionGameFrame < targetFrame && predicted < maxPredictionFrames ) {
		const int previous = ( predictionGameFrame - 1 ) & ( MAX_USERCMD_BACKUP - 1 );
		const int current = predictionGameFrame & ( MAX_USERCMD_BACKUP - 1 );
		for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
			idAsyncNetwork::DuplicateUsercmd( playbackUsercmds[ previous ][ i ], playbackUsercmds[ current ][ i ],
				predictionGameFrame, predictionGameFrame * ticMsec );
		}
		const bool last = predictionGameFrame + 1 >= targetFrame;
		const gameReturn_t result = game->ClientPrediction( viewClient, playbackUsercmds[ current ], last );
		idAsyncNetwork::ExecuteSessionCommand( result.sessionCommand );
		predictionGameFrame++;
		predicted++;
	}
}

/*
==================
idMultiViewDemo::ResetPlaybackStream
==================
*/
bool idMultiViewDemo::ResetPlaybackStream() {
	if ( state != MVD_PLAYING || file == NULL ) {
		return false;
	}

#ifndef ID_DEDICATED
	const bool restoreDemoMenu =
		sessLocal.guiDemoMenu != NULL && sessLocal.guiActive == sessLocal.guiDemoMenu;
#endif
	const int restoreFollowClient = clientView ? DEMO_FOLLOW_FREE : preferredFollowClient >= 0 ?
		preferredFollowClient : game->GetDemoFollowClient();
	resettingPlayback = true;
	game->SetDemoState( DEMO_NONE, false, false );
	session->Stop();

	if ( !ReadMapStateRecord( playbackMapState ) ) {
		resettingPlayback = false;
		return false;
	}

	if ( !ApplyPlaybackNetworkState( playbackNetworkState, "reset" ) ) {
		resettingPlayback = false;
		return false;
	}
	if ( !clientView && !game->SetDemoFollowClient( restoreFollowClient ) ) {
		lastError = va( "could not restore MVD viewpoint %d after rewind", restoreFollowClient );
		resettingPlayback = false;
		return false;
	}
	if ( file->Seek( playbackStreamOffset, FS_SEEK_SET ) != 0 ) {
		lastError = "could not rewind MVD stream for seeking";
		resettingPlayback = false;
		return false;
	}

	playbackGameTime = header.startGameTime;
	latestSnapshotGameFrame = header.startGameFrame;
	latestSnapshotGameTime = header.startGameTime;
	predictionGameFrame = header.startGameFrame;
	playbackLastRecordGameFrame = header.startGameFrame;
	playbackLastRecordGameTime = header.startGameTime;
	snapshotSequence = 0;
	snapshotCount = 0;
	reliableCount = 0;
	recordCount = playbackInitializationRecordCount;
	haveSnapshot = false;
	havePendingRecord = false;
	pendingRecord.payload.Clear();
	sawCleanEnd = false;
	if ( clientView ) {
		latestSnapshotGameFrame = clientStartSnapshotFrame;
		latestSnapshotGameTime = clientStartSnapshotTime;
		latestSnapshotClientFrame = clientStartGameFrame;
		latestSnapshotStampTime = header.startGameTime;
		predictionGameFrame = clientStartSnapshotFrame;
		snapshotSequence = clientStartSequence;
		haveSnapshot = true;
	}
	playbackLastRealTime = Sys_Milliseconds();
	if ( sessLocal.sw != NULL ) {
		sessLocal.sw->SetSlowmoSpeed( GetPlaybackScale() );
		if ( mvd_paused.GetBool() ) {
			sessLocal.sw->Pause();
		}
	}
	resettingPlayback = false;
#ifndef ID_DEDICATED
	if ( restoreDemoMenu && sessLocal.guiDemoMenu != NULL ) {
		sessLocal.SetGUI( sessLocal.guiDemoMenu, NULL );
		sessLocal.UpdateDemoMenuGui();
	}
#endif
	return true;
}

/*
==================
idMultiViewDemo::SeekToMS
==================
*/
bool idMultiViewDemo::SeekToMS( int relativeTimeMS ) {
	if ( state != MVD_PLAYING || playbackEndGameTime <= header.startGameTime ) {
		return false;
	}

	const int duration = playbackEndGameTime - header.startGameTime;
	const int currentFollowClient = game->GetDemoFollowClient();
	if ( currentFollowClient >= 0 || preferredFollowClient < 0 ) {
		preferredFollowClient = currentFollowClient;
	}
	const int clampedRelative = idMath::ClampInt( 0, Max( 0, duration - 1 ), relativeTimeMS );
	const int target = header.startGameTime + clampedRelative;
	seekTargetGameTime = target;
	seekInProgress = true;
	mvd_seeking.SetBool( true );
	const bool backward = target < static_cast<int>( playbackGameTime );
	bool positioned = false;
	// A keyframe replaces replay from the start for a backward seek, and for a
	// forward seek far enough ahead to be worth a map reload.
	const int keyframeIndex = mvd_useKeyframes.GetBool() ?
		openq4_mvd::KeyframeAtOrBefore( keyframes, target ) : -1;
	if ( keyframeIndex >= 0 &&
		 ( backward ||
		   keyframes[keyframeIndex].gameTime - static_cast<int>( playbackGameTime ) >= MVD_KEYFRAME_FORWARD_MIN_MS ) ) {
		bool worldReset = false;
		positioned = JumpToKeyframe( keyframeIndex, worldReset );
		if ( !positioned ) {
			common->Warning( "MVD keyframe at %.1f s could not be restored (%s); seeking by replay instead",
				( keyframes[keyframeIndex].gameTime - header.startGameTime ) * 0.001f, lastError.c_str() );
			// Do not retry a damaged directory on every seek.
			keyframes.clear();
			if ( worldReset && !ResetPlaybackStream() ) {
				const idStr failure = lastError;
				FinishPlayback( failure.c_str(), true );
				return false;
			}
			positioned = worldReset;
		}
	}
	if ( !positioned && backward ) {
		if ( !ResetPlaybackStream() ) {
			const idStr failure = lastError;
			FinishPlayback( failure.c_str(), true );
			return false;
		}
	}
	// A paused seek still needs one presentation pass after replay has reached
	// its target.  Snapshots and round resets can replace the camera meanwhile.
	forcePresentationFrame = true;

	if ( sessLocal.sw != NULL ) {
		sessLocal.sw->Pause();
	}
	playbackLastRealTime = Sys_Milliseconds();
	return true;
}

bool idMultiViewDemo::SeekByMS( int deltaMS ) {
	return SeekToMS( GetPlaybackTimeMS() + deltaMS );
}

/*
==================
idMultiViewDemo::JumpToKeyframe

Restores a keyframe as the stream's new starting point. Everything is parsed
and validated before the world is reset; worldReset reports whether a failure
happened after that point, when only a full replay can repair the world.
==================
*/
bool idMultiViewDemo::JumpToKeyframe( int index, bool &worldReset ) {
	if ( clientView ) {
		return JumpToClientKeyframe( index, worldReset );
	}
	worldReset = false;
	if ( state != MVD_PLAYING || file == NULL || index < 0 || index >= static_cast<int>( keyframes.size() ) ) {
		lastError = "no such keyframe";
		return false;
	}
	const openq4_mvd::Keyframe entry = keyframes[index];
	record_t record;
	if ( file->Seek( entry.fileOffset, FS_SEEK_SET ) != 0 || ReadRecord( file, record ) != MVD_READ_OK ) {
		lastError = va( "could not read the keyframe at offset %d", entry.fileOffset );
		return false;
	}
	const int resumeOffset = file->Tell();
	openq4_mvd::KeyframeHeader keyframeHeader;
	if ( record.type != MVD_RECORD_KEYFRAME || record.version != 1 || record.flags != 0 ||
		 !openq4_mvd::ParseKeyframeHeader( record.payload.Ptr(), record.payload.Num(), keyframeHeader ) ||
		 keyframeHeader.gameTime != entry.gameTime || keyframeHeader.sequence != entry.sequence ||
		 keyframeHeader.gameTime < header.startGameTime || keyframeHeader.gameTime > playbackEndGameTime ||
		 keyframeHeader.gameFrame < header.startGameFrame ) {
		lastError = "the keyframe does not match its directory entry";
		return false;
	}
	const byte *payload = record.payload.Ptr();
	idStr error;
	if ( !ValidateNetworkStatePayload( payload + keyframeHeader.networkInfoOffset,
			keyframeHeader.networkInfoBytes, error ) ) {
		lastError = error;
		return false;
	}
	if ( !openq4_mvd::ValidateBaseline( payload + keyframeHeader.baselineOffset, keyframeHeader.baselineBytes,
			keyframeHeader.sequence, MVD_BASELINE_MAX_ENTITIES, MVD_BASELINE_MAX_STATE_BYTES,
			MVD_BASELINE_PVS_WORDS ) ) {
		lastError = "invalid keyframe baseline";
		return false;
	}
	idDict serverInfo;
	idDict syncedCVars;
	idDict userInfo[MAX_ASYNC_CLIENTS];
	if ( !ReadMapStatePayload( payload + keyframeHeader.mapStateOffset, keyframeHeader.mapStateBytes,
			serverInfo, syncedCVars, userInfo ) ) {
		return false;
	}

#ifndef ID_DEDICATED
	const bool restoreDemoMenu =
		sessLocal.guiDemoMenu != NULL && sessLocal.guiActive == sessLocal.guiDemoMenu;
#endif
	const int restoreFollowClient = clientView ? DEMO_FOLLOW_FREE : preferredFollowClient >= 0 ?
		preferredFollowClient : game->GetDemoFollowClient();
	worldReset = true;
	resettingPlayback = true;
	game->SetDemoState( DEMO_NONE, false, false );
	session->Stop();

	sessLocal.mapSpawnData.serverInfo = serverInfo;
	sessLocal.mapSpawnData.syncedCVars = syncedCVars;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		sessLocal.mapSpawnData.userInfo[i] = userInfo[i];
		sessLocal.mapSpawnData.persistentPlayerInfo[i].Clear();
	}
	memset( sessLocal.mapSpawnData.mapSpawnUsercmd, 0, sizeof( sessLocal.mapSpawnData.mapSpawnUsercmd ) );
	sessLocal.numClients = 0;
	cvarSystem->SetCVarsFromDictByFlags( sessLocal.mapSpawnData.syncedCVars, CVAR_NETWORKSYNC );
	sessLocal.ExecuteMapChange();
	game->SetDemoState( DEMO_PLAYING, true, false );
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		game->SetUserInfo( i, sessLocal.mapSpawnData.userInfo[i], true );
	}

	idFile_Memory networkInfo( "MVD keyframe network info",
		reinterpret_cast<const char *>( payload + keyframeHeader.networkInfoOffset ),
		keyframeHeader.networkInfoBytes );
	game->ReadNetworkInfo( keyframeHeader.gameTime, &networkInfo, MAX_ASYNC_CLIENTS );
	idFile_Memory baseline( "MVD keyframe baseline",
		reinterpret_cast<const char *>( payload + keyframeHeader.baselineOffset ),
		keyframeHeader.baselineBytes );
	if ( networkInfo.Tell() != networkInfo.Length() ||
		 !game->ClientReadServerDemoBaseline( keyframeHeader.sequence, &baseline ) ||
		 baseline.Tell() != baseline.Length() ) {
		lastError = "the game module rejected the keyframe state";
		resettingPlayback = false;
		return false;
	}
	// The followed player may not exist yet at this keyframe. Free flight
	// stands in until prediction can present the preferred slot again.
	if ( !game->SetDemoFollowClient( restoreFollowClient ) ) {
		game->SetDemoFollowClient( DEMO_FOLLOW_FREE );
	}
	if ( file->Seek( resumeOffset, FS_SEEK_SET ) != 0 ) {
		lastError = "could not resume the MVD stream after its keyframe";
		resettingPlayback = false;
		return false;
	}

	playbackGameTime = keyframeHeader.gameTime;
	latestSnapshotGameFrame = keyframeHeader.gameFrame;
	latestSnapshotGameTime = keyframeHeader.gameTime;
	predictionGameFrame = keyframeHeader.gameFrame;
	playbackLastRecordGameFrame = keyframeHeader.gameFrame;
	playbackLastRecordGameTime = keyframeHeader.gameTime;
	snapshotSequence = keyframeHeader.sequence;
	snapshotCount = keyframeHeader.sequence;
	reliableCount = keyframeHeader.reliableCount;
	recordCount = keyframeHeader.recordCount + 1;
	haveSnapshot = true;
	havePendingRecord = false;
	pendingRecord.payload.Clear();
	sawCleanEnd = false;
	playbackLastRealTime = Sys_Milliseconds();
	if ( sessLocal.sw != NULL ) {
		sessLocal.sw->SetSlowmoSpeed( GetPlaybackScale() );
		if ( mvd_paused.GetBool() ) {
			sessLocal.sw->Pause();
		}
	}
	resettingPlayback = false;
#ifndef ID_DEDICATED
	if ( restoreDemoMenu && sessLocal.guiDemoMenu != NULL ) {
		sessLocal.SetGUI( sessLocal.guiDemoMenu, NULL );
		sessLocal.UpdateDemoMenuGui();
	}
#endif
	common->Printf( "MVD_KEYFRAME restored time=%d sequence=%d\n",
		keyframeHeader.gameTime - header.startGameTime, keyframeHeader.sequence );
	return true;
}

/*
==================
idMultiViewDemo::JumpToClientKeyframe

openQ4 MP-22: a client-view recording's keyframe as its new starting point.
Everything is parsed and validated before the world is reset.
==================
*/
bool idMultiViewDemo::JumpToClientKeyframe( int index, bool &worldReset ) {
	worldReset = false;
	if ( state != MVD_PLAYING || file == NULL || index < 0 || index >= static_cast<int>( keyframes.size() ) ) {
		lastError = "no such keyframe";
		return false;
	}
	const openq4_mvd::Keyframe entry = keyframes[index];
	record_t record;
	if ( file->Seek( entry.fileOffset, FS_SEEK_SET ) != 0 || ReadRecord( file, record ) != MVD_READ_OK ) {
		lastError = va( "could not read the keyframe at offset %d", entry.fileOffset );
		return false;
	}
	const int resumeOffset = file->Tell();
	openq4_mvd::KeyframeHeader keyframeHeader;
	if ( record.type != MVD_RECORD_CLIENT_KEYFRAME || record.version != 1 || record.flags != 0 ||
		 !openq4_mvd::ParseKeyframeHeader( record.payload.Ptr(), record.payload.Num(), keyframeHeader ) ||
		 keyframeHeader.gameTime != entry.gameTime || keyframeHeader.sequence != entry.sequence ||
		 keyframeHeader.gameTime < header.startGameTime || keyframeHeader.gameTime > playbackEndGameTime ||
		 keyframeHeader.gameFrame < header.startGameFrame || keyframeHeader.baselineBytes != 4 ||
		 MVD_ReadLittleUInt( record.payload.Ptr() + keyframeHeader.baselineOffset ) != 1 ) {
		lastError = "the keyframe does not match its directory entry";
		return false;
	}
	const byte *payload = record.payload.Ptr();
	idStr error;
	if ( !ValidateClientNetworkState( payload + keyframeHeader.networkInfoOffset, keyframeHeader.networkInfoBytes, error ) ) {
		lastError = error;
		return false;
	}
	idDict serverInfo;
	idDict syncedCVars;
	idDict userInfo[MAX_ASYNC_CLIENTS];
	if ( !ReadMapStatePayload( payload + keyframeHeader.mapStateOffset, keyframeHeader.mapStateBytes,
			serverInfo, syncedCVars, userInfo ) ) {
		return false;
	}
	record_t networkState;
	networkState.type = MVD_RECORD_NETWORK_STATE;
	networkState.version = MVD_NETWORK_STATE_CLIENT_VERSION;
	networkState.flags = MVD_RECORD_FLAG_REQUIRED;
	networkState.payload.SetNum( keyframeHeader.networkInfoBytes );
	memcpy( networkState.payload.Ptr(), payload + keyframeHeader.networkInfoOffset, keyframeHeader.networkInfoBytes );

#ifndef ID_DEDICATED
	const bool restoreDemoMenu =
		sessLocal.guiDemoMenu != NULL && sessLocal.guiActive == sessLocal.guiDemoMenu;
#endif
	worldReset = true;
	resettingPlayback = true;
	game->SetDemoState( DEMO_NONE, false, false );
	session->Stop();
	sessLocal.mapSpawnData.serverInfo = serverInfo;
	sessLocal.mapSpawnData.syncedCVars = syncedCVars;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		sessLocal.mapSpawnData.userInfo[i] = userInfo[i];
		sessLocal.mapSpawnData.persistentPlayerInfo[i].Clear();
	}
	memset( sessLocal.mapSpawnData.mapSpawnUsercmd, 0, sizeof( sessLocal.mapSpawnData.mapSpawnUsercmd ) );
	sessLocal.numClients = 0;
	if ( !ApplyPlaybackNetworkState( networkState, "keyframe" ) ) {
		resettingPlayback = false;
		return false;
	}
	if ( file->Seek( resumeOffset, FS_SEEK_SET ) != 0 ) {
		lastError = "could not resume the MVD stream after its keyframe";
		resettingPlayback = false;
		return false;
	}

	playbackGameTime = keyframeHeader.gameTime;
	latestSnapshotGameFrame = clientStartSnapshotFrame;
	latestSnapshotGameTime = clientStartSnapshotTime;
	latestSnapshotClientFrame = clientStartGameFrame;
	latestSnapshotStampTime = keyframeHeader.gameTime;
	predictionGameFrame = clientStartSnapshotFrame;
	snapshotSequence = clientStartSequence;
	playbackLastRecordGameFrame = keyframeHeader.gameFrame;
	playbackLastRecordGameTime = keyframeHeader.gameTime;
	snapshotCount = keyframeHeader.sequence;
	reliableCount = keyframeHeader.reliableCount;
	recordCount = keyframeHeader.recordCount + 1;
	haveSnapshot = true;
	havePendingRecord = false;
	pendingRecord.payload.Clear();
	sawCleanEnd = false;
	playbackLastRealTime = Sys_Milliseconds();
	if ( sessLocal.sw != NULL ) {
		sessLocal.sw->SetSlowmoSpeed( GetPlaybackScale() );
		if ( mvd_paused.GetBool() ) {
			sessLocal.sw->Pause();
		}
	}
	resettingPlayback = false;
#ifndef ID_DEDICATED
	if ( restoreDemoMenu && sessLocal.guiDemoMenu != NULL ) {
		sessLocal.SetGUI( sessLocal.guiDemoMenu, NULL );
		sessLocal.UpdateDemoMenuGui();
	}
#endif
	common->Printf( "MVD_KEYFRAME restored time=%d sequence=%d\n",
		keyframeHeader.gameTime - header.startGameTime, keyframeHeader.sequence );
	return true;
}

/*
==================
idMultiViewDemo::GetMarkerCount
==================
*/
int idMultiViewDemo::GetMarkerCount() const {
	return state == MVD_PLAYING ? static_cast<int>( markers.size() ) : 0;
}

/*
==================
idMultiViewDemo::SeekToMarker
==================
*/
bool idMultiViewDemo::SeekToMarker( int direction, int kindFilter ) {
	if ( state != MVD_PLAYING || markers.empty() ) {
		return false;
	}
	const int leadMS = mvd_markerLeadMS.GetInteger();
	const int now = seekInProgress ? seekTargetGameTime : static_cast<int>( playbackGameTime );
	const int index = openq4_mvd::FindMarker( markers, now, direction, kindFilter, leadMS, lastMarkerIndex );
	if ( index < 0 ) {
		return false;
	}
	lastMarkerIndex = index;
	const openq4_mvd::Marker &marker = markers[index];
	const int targetRelative = Max( 0, marker.gameTime - leadMS - header.startGameTime );
	common->Printf( "MVD_MARKER index=%d kind=%s time=%d client=%d value0=%d value1=%d text=\"%s\"\n",
		index, MVD_MarkerKindName( marker.kind ), marker.gameTime - header.startGameTime,
		marker.client, marker.value0, marker.value1, marker.text );
	return SeekToMS( targetRelative );
}

void idMultiViewDemo::StepFrames( int frames ) {
	if ( state != MVD_PLAYING ) {
		return;
	}
	SetPaused( true );
	// openQ4 MP-22: a negative count steps back, from the keyframe before it
	const int step = ( frames == 0 ? 1 : frames ) * Max( 1, common->GetUserCmdMSec() );
	forcePresentationFrame = true;
	if ( !SeekByMS( step ) ) {
		forcePresentationFrame = false;
	}
}

/*
==================
idMultiViewDemo::ProcessSeekBudget
==================
*/
bool idMultiViewDemo::ProcessSeekBudget() {
	if ( state != MVD_PLAYING || !seekInProgress ) {
		return false;
	}

	const int deadline = Sys_Milliseconds() + idMath::ClampInt( 1, 100, mvd_seekBudgetMS.GetInteger() );
	int processed = 0;
	while ( state == MVD_PLAYING && processed < 1024 && Sys_Milliseconds() <= deadline ) {
		if ( !havePendingRecord && !LoadNextPlaybackRecord() ) {
			return true;
		}

		int recordGameTime = 0;
		if ( RecordTimestamp( pendingRecord, recordGameTime ) &&
			 recordGameTime > seekTargetGameTime ) {
			seekInProgress = false;
			mvd_seeking.SetBool( false );
			playbackGameTime = seekTargetGameTime;
			playbackLastRealTime = Sys_Milliseconds();
			if ( sessLocal.sw != NULL && !mvd_paused.GetBool() ) {
				sessLocal.sw->UnPause();
			}
			return true;
		}

		bool finished = false;
		if ( !ProcessPlaybackRecord( pendingRecord, finished ) ) {
			const idStr failure = lastError;
			FinishPlayback( failure.c_str(), true );
			return true;
		}
		if ( RecordTimestamp( pendingRecord, recordGameTime ) ) {
			playbackGameTime = Min( seekTargetGameTime, recordGameTime );
		}
		havePendingRecord = false;
		pendingRecord.payload.Clear();
		processed++;
		if ( finished ) {
			FinishPlayback( "complete", false );
			return true;
		}
	}

	if ( state == MVD_PLAYING && static_cast<int>( playbackGameTime ) >= seekTargetGameTime ) {
		seekInProgress = false;
		mvd_seeking.SetBool( false );
		playbackGameTime = seekTargetGameTime;
		playbackLastRealTime = Sys_Milliseconds();
		if ( sessLocal.sw != NULL && !mvd_paused.GetBool() ) {
			sessLocal.sw->UnPause();
		}
	}
	return true;
}

/*
==================
idMultiViewDemo::StopPlayback
==================
*/
void idMultiViewDemo::StopPlayback( const char *reason ) {
	if ( state != MVD_PLAYING ) {
		return;
	}
	const idStr stoppedFile = fileName;
	const idStr stoppedReason = reason != NULL ? reason : "stopped";
	// an export stopped early keeps the sound track of what it wrote
	FinishExportSound();
	game->SetDemoState( DEMO_NONE, false, false );
	if ( sessLocal.sw != NULL ) {
		sessLocal.sw->SetSlowmoSpeed( 1.0f );
		if ( sessLocal.sw->IsPaused() ) {
			sessLocal.sw->UnPause();
		}
	}
	if ( file != NULL ) {
		fileSystem->CloseFile( file );
		file = NULL;
	}
	Clear();
	common->Printf( "Stopped MVD playback of '%s' (%s)\n", stoppedFile.c_str(), stoppedReason.c_str() );
}

/*
==================
idMultiViewDemo::FinishPlayback
==================
*/
void idMultiViewDemo::FinishPlayback( const char *reason, bool warning ) {
	if ( state != MVD_PLAYING ) {
		return;
	}
	if ( warning ) {
		common->Warning( "MVD playback stopped: %s", reason ? reason : "stream error" );
	}
	const bool timed = timeDemo || exporting;
	const bool quitAfter = timeDemoQuit;
	if ( exporting ) {
		FinishExportSound();
		common->Printf( "MVD_EXPORT frames=%d directory=%s fps=%d complete=%d\n", exportFrames,
			exportDirectory.c_str(), exportFps, warning ? 0 : 1 );
	}
	if ( timeDemo && timeDemoFrames > 0 ) {
		const float seconds = Max( 1, timeDemoLastFrameTime - timeDemoStartTime ) * 0.001f;
		common->Printf( "MVD_TIMEDEMO frames=%d seconds=%.2f fps=%.1f slowest=%dms match=%.1fs complete=%d\n",
			timeDemoFrames, seconds, timeDemoFrames / seconds, timeDemoSlowestFrame,
			( playbackLastRecordGameTime - header.startGameTime ) * 0.001f, warning ? 0 : 1 );
		common->Printf( "%d frames rendered in %3.1f seconds = %3.1f fps\n", timeDemoFrames, seconds, timeDemoFrames / seconds );
	}
	StopPlayback( reason );
	if ( timed && quitAfter ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
		return;
	}
	session->Stop();
	session->StartMenu();
}

/*
==================
idMultiViewDemo::RecordTimestamp
==================
*/
bool idMultiViewDemo::RecordTimestamp( const record_t &record, int &gameTime ) const {
	if ( ( record.flags & ~MVD_RECORD_FLAG_REQUIRED ) != 0 ) {
		return false;
	}
	if ( record.type == MVD_RECORD_RELIABLE ) {
		const int minimum = record.version == 2 ? 12 : 11;
		if ( ( record.version != 1 && record.version != 2 ) ||
			 record.payload.Num() < minimum ) {
			return false;
		}
	} else if ( record.type == MVD_RECORD_SNAPSHOT ) {
		if ( ( record.version != 1 && record.version != 2 ) ||
			 record.payload.Num() < 12 ) {
			return false;
		}
	} else if ( record.type == MVD_RECORD_END ) {
		if ( record.version != 1 || record.payload.Num() != 20 ) {
			return false;
		}
	} else if ( MVD_IsClientRecord( record.type ) ) {
		if ( record.version != 1 || record.payload.Num() < 12 ) {
			return false;
		}
	} else if ( record.type == MVD_RECORD_CLIENT_KEYFRAME ) {
		if ( record.version != 1 || record.payload.Num() < openq4_mvd::KEYFRAME_FIXED_BYTES ) {
			return false;
		}
	} else if ( record.type == MVD_RECORD_USERINFO || record.type == MVD_RECORD_MARKER ||
				record.type == MVD_RECORD_KEYFRAME ) {
		const int minimum = record.type == MVD_RECORD_MARKER ? openq4_mvd::MARKER_FIXED_BYTES :
			record.type == MVD_RECORD_KEYFRAME ? openq4_mvd::KEYFRAME_FIXED_BYTES : 13;
		if ( record.version != 1 || record.payload.Num() < minimum ) {
			return false;
		}
	} else {
		return false;
	}
	gameTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + sizeof( int ) ) );
	return true;
}

/*
==================
idMultiViewDemo::LoadNextPlaybackRecord
==================
*/
bool idMultiViewDemo::LoadNextPlaybackRecord() {
	if ( state != MVD_PLAYING || havePendingRecord ) {
		return state == MVD_PLAYING;
	}
	const readResult_t result = ReadRecord( file, pendingRecord );
	if ( result == MVD_READ_EOF ) {
		FinishPlayback( "stream ended without a clean end marker", true );
		return false;
	}
	if ( result == MVD_READ_ERROR ) {
		const idStr failure = lastError;
		FinishPlayback( failure.c_str(), true );
		return false;
	}
	if ( !RecordIsSupported( pendingRecord, true ) ) {
		const idStr failure = lastError;
		FinishPlayback( failure.c_str(), true );
		return false;
	}
	recordCount++;
	int recordGameTime = 0;
	if ( RecordTimestamp( pendingRecord, recordGameTime ) ) {
		const int recordGameFrame = static_cast<int>( MVD_ReadLittleUInt( pendingRecord.payload.Ptr() ) );
		if ( recordGameFrame < playbackLastRecordGameFrame ||
			 recordGameTime < playbackLastRecordGameTime ||
			 recordGameTime > playbackEndGameTime ) {
			const idStr failure = va( "invalid MVD record timeline (%d, %d)", recordGameFrame, recordGameTime );
			lastError = failure;
			FinishPlayback( failure.c_str(), true );
			return false;
		}
	}
	havePendingRecord = true;
	return true;
}

/*
==================
idMultiViewDemo::ProcessPlaybackRecord
==================
*/
bool idMultiViewDemo::ProcessPlaybackRecord( const record_t &record, bool &finished ) {
	finished = false;
	if ( MVD_RecordShouldSkip( record.type, record.version, record.flags ) ) {
		// An unknown optional version was already approved by
		// RecordIsSupported and is intentionally skipped.
		return true;
	}
	// openQ4 MP-22: each view has its own snapshot and reliable records
	if ( clientView ? ( record.type == MVD_RECORD_RELIABLE || record.type == MVD_RECORD_SNAPSHOT ) :
		 MVD_IsClientRecord( record.type ) ) {
		lastError = va( "MVD record %u does not belong to this recording's view", record.type );
		return false;
	}
	if ( clientView ) {
		bool handled = false;
		if ( !ProcessClientPlaybackRecord( record, handled ) ) {
			return false;
		}
		if ( handled ) {
			return true;
		}
	}

	if ( record.type == MVD_RECORD_RELIABLE ) {
		if ( !MVD_ValidateReliablePayload( record.payload, record.version, lastError, retailSource ) ) {
			return false;
		}
		const int gameFrame = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) );
		const int gameTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
		const byte *messageData = record.payload.Ptr() + 8;
		int messageBytes = record.payload.Num() - 8;
		idList<byte> normalizedRoute;
		if ( record.version == 2 && record.payload[8] != DEMO_RECORD_INSTANCE ) {
			// The v2 container always stores an instance byte. The stable game
			// route grammar only consumes one for DEMO_RECORD_INSTANCE, so
			// remove the -1 placeholder for legacy CLIENTNUM/EXCLUDE routes.
			normalizedRoute.SetNum( messageBytes - 1 );
			normalizedRoute[0] = record.payload[8];
			normalizedRoute[1] = record.payload[9];
			memcpy( normalizedRoute.Ptr() + 2, record.payload.Ptr() + 11,
				record.payload.Num() - 11 );
			messageData = normalizedRoute.Ptr();
			messageBytes = normalizedRoute.Num();
		}
		idBitMsg msg;
		msg.Init( messageData, messageBytes );
		msg.SetSize( messageBytes );
		msg.BeginReading();
		game->ClientProcessReliableMessage( MAX_ASYNC_CLIENTS, msg );
		playbackLastRecordGameFrame = gameFrame;
		playbackLastRecordGameTime = gameTime;
		reliableCount++;
		return true;
	}

	if ( record.type == MVD_RECORD_SNAPSHOT ) {
		if ( !MVD_ValidateSnapshotPayload( record.payload, record.version, lastError ) ) {
			return false;
		}
		const int gameFrame = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) );
		const int gameTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
		const int sequence = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 8 ) );
		if ( sequence != snapshotSequence + 1 ||
			 gameFrame < playbackLastRecordGameFrame ||
			 gameTime < playbackLastRecordGameTime ) {
			lastError = va( "invalid MVD snapshot timeline (%d, %d, %d)",
				sequence, gameFrame, gameTime );
			return false;
		}

		idBitMsg msg;
		msg.Init( record.payload.Ptr() + 12, record.payload.Num() - 12 );
		msg.SetSize( record.payload.Num() - 12 );
		msg.BeginReading();
		if ( retailSource && cvarSystem->GetCVarInteger( "net_retailDemoDebug" ) > 2 ) {
			idStr bytes;
			for ( int b = 0; b < 24 && 12 + b < record.payload.Num(); b++ ) {
				bytes += va( "%02x", record.payload[ 12 + b ] );
			}
			common->Printf( "retail server snapshot %d (%d bytes): %s\n", sequence, record.payload.Num() - 12, bytes.c_str() );
		}
		if ( !game->ClientReadServerDemoSnapshot(
				sequence, gameFrame, gameTime, msg, record.version >= 2 ) ) {
			lastError = va( "game module rejected MVD snapshot %d", sequence );
			return false;
		}

		snapshotSequence = sequence;
		latestSnapshotGameFrame = gameFrame;
		latestSnapshotGameTime = gameTime;
		playbackLastRecordGameFrame = gameFrame;
		playbackLastRecordGameTime = gameTime;
		predictionGameFrame = gameFrame;
		haveSnapshot = true;
		snapshotCount++;
		return true;
	}

	if ( record.type == MVD_RECORD_END ) {
		if ( record.payload.Num() != 20 ) {
			lastError = "invalid MVD end record";
			return false;
		}
		const int endTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
		const int reportedSnapshots = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 8 ) );
		const int reportedReliables = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 12 ) );
		const int reportedRecords = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 16 ) );
		const int endFrame = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) );
		if ( endFrame < playbackLastRecordGameFrame ||
			 endTime < playbackLastRecordGameTime ||
			 reportedSnapshots != snapshotCount ||
			 reportedReliables != reliableCount ||
			 reportedRecords != recordCount - 1 ) {
			lastError = "MVD end record does not match playback state";
			return false;
		}
		playbackLastRecordGameFrame = endFrame;
		playbackLastRecordGameTime = endTime;
		sawCleanEnd = true;
		finished = true;
		return true;
	}

	if ( record.type == MVD_RECORD_USERINFO ) {
		// A malformed optional record leaves the previous name in place.
		if ( !ApplyUserInfoRecord( record ) ) {
			common->Warning( "Ignoring malformed MVD user info: %s", lastError.c_str() );
		}
		return true;
	}

	if ( record.type == MVD_RECORD_MARKER ) {
		openq4_mvd::Marker marker;
		int gameFrame = 0;
		if ( openq4_mvd::ParseMarkerRecord( record.payload.Ptr(), record.payload.Num(), marker, gameFrame,
				SERVER_MVD_MARKER_KIND_COUNT, MAX_ASYNC_CLIENTS, MVD_MAX_INSTANCES ) &&
			 marker.kind == SERVER_MVD_MARKER_NOTE && !seekInProgress ) {
			common->Printf( "MVD note at %s: %s\n",
				va( "%d:%02d", ( marker.gameTime - header.startGameTime ) / 60000,
					( ( marker.gameTime - header.startGameTime ) / 1000 ) % 60 ), marker.text );
		}
		return true;
	}

	// Metadata, keyframes passed during continuous playback, directories and
	// unknown optional records are safe to ignore here.
	return true;
}

/*
==================
idMultiViewDemo::ApplyUserInfoRecord
==================
*/
bool idMultiViewDemo::ApplyUserInfoRecord( const record_t &record ) {
	if ( record.version != 1 || record.payload.Num() < 13 || record.payload.Num() > MVD_MAX_USERINFO_BYTES ) {
		lastError = "invalid MVD user-info record size";
		return false;
	}
	const int clientNum = record.payload[8];
	if ( clientNum >= MAX_ASYNC_CLIENTS ) {
		lastError = va( "invalid MVD user-info client %d", clientNum );
		return false;
	}
	idFile_Memory payload( "MVD user info",
		reinterpret_cast<const char *>( record.payload.Ptr() + 9 ), record.payload.Num() - 9 );
	idDict info;
	if ( !MVD_ReadDict( &payload, info, lastError ) ) {
		return false;
	}
	if ( payload.Tell() != payload.Length() ) {
		lastError = "MVD user-info record has trailing bytes";
		return false;
	}
	sessLocal.mapSpawnData.userInfo[clientNum] = info;
	game->SetUserInfo( clientNum, info, true );
	return true;
}

/*
==================
idMultiViewDemo::RunPlaybackFrame
==================
*/
void idMultiViewDemo::RunPlaybackFrame() {
	if ( state != MVD_PLAYING ) {
		return;
	}

	// Remember the chosen POV before snapshots or a round reset temporarily
	// make its player inactive, during both normal playback and seeking.
	if ( !clientView ) {
		const int currentFollowClient = game->GetDemoFollowClient();
		if ( currentFollowClient >= 0 || preferredFollowClient < 0 ) {
			preferredFollowClient = currentFollowClient;
		}
	}

	if ( seekInProgress ) {
		ProcessSeekBudget();
		if ( state != MVD_PLAYING || seekInProgress ) {
			return;
		}
	}

	const int now = Sys_Milliseconds();
	int elapsed = idMath::ClampInt( 0, 100, now - playbackLastRealTime );
	playbackLastRealTime = now;
	if ( exporting ) {
		// openQ4 MP-22: an export frame is 1000/fps of the match; the screenshot of
		// the frame shown runs first in the next frame's command buffer
		elapsed = DemoExportStepMsec( exportSteps++, exportFps );
		if ( playbackGameTime > header.startGameTime ) {
			// this frame's sound mixes the picture's share of the track, so the
			// track lasts exportFrames * 1000 / fps milliseconds
			Sound_CaptureAdvance( DemoExportStepMsec( exportFrames, exportFps ) );
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND,
				va( "screenshot \"%s/frame%05d.png\"\n", exportDirectory.c_str(), exportFrames++ ) );
		}
	} else if ( timeDemo ) {
		// openQ4 MP-22: a benchmark frame is one simulation tic of the match
		elapsed = Max( 1, common->GetUserCmdMSec() );
		if ( timeDemoStartTime == 0 ) {
			timeDemoStartTime = now;
		} else {
			timeDemoFrames++;
			timeDemoSlowestFrame = Max( timeDemoSlowestFrame, now - timeDemoLastFrameTime );
		}
		timeDemoLastFrameTime = now;
	}
	if ( !mvd_paused.GetBool() ) {
		playbackGameTime += elapsed * ( timeDemo || exporting ? 1.0f : GetPlaybackScale() );
	}

	const int recordDeadline = Sys_Milliseconds() +
		idMath::ClampInt( 1, 8, mvd_seekBudgetMS.GetInteger() );
	int processedRecords = 0;
	while ( state == MVD_PLAYING &&
		 processedRecords < MVD_MAX_RECORDS_PER_FRAME &&
		 Sys_Milliseconds() <= recordDeadline ) {
		if ( !havePendingRecord && !LoadNextPlaybackRecord() ) {
			return;
		}

		int recordGameTime = 0;
		if ( RecordTimestamp( pendingRecord, recordGameTime ) &&
			 recordGameTime > static_cast<int>( playbackGameTime ) ) {
			break;
		}

		bool finished = false;
		if ( !ProcessPlaybackRecord( pendingRecord, finished ) ) {
			const idStr failure = lastError;
			FinishPlayback( failure.c_str(), true );
			return;
		}
		havePendingRecord = false;
		pendingRecord.payload.Clear();
		processedRecords++;
		if ( finished ) {
			FinishPlayback( "complete", false );
			return;
		}
	}

	if ( state != MVD_PLAYING ) {
		return;
	}
	if ( mvd_paused.GetBool() && !forcePresentationFrame ) {
		usercmdGen->GetDirectUsercmd();
		return;
	}

	game->ClientRun();
	if ( clientView ) {
		RunClientPrediction();
	} else if ( haveSnapshot ) {
		const int ticMsec = Max( 1, common->GetUserCmdMSec() );
		int targetFrame = latestSnapshotGameFrame +
			Max( 0, static_cast<int>( playbackGameTime ) - latestSnapshotGameTime ) / ticMsec;
		if ( forcePresentationFrame ) {
			targetFrame = Max( targetFrame, predictionGameFrame + 1 );
		}
		const int maxPredictionFrames = 64;
		int predicted = 0;
		while ( predictionGameFrame < targetFrame && predicted < maxPredictionFrames ) {
			usercmd_t commands[MAX_ASYNC_CLIENTS + 1];
			memset( commands, 0, sizeof( commands ) );
			commands[MAX_ASYNC_CLIENTS] = usercmdGen->GetDirectUsercmd();
			commands[MAX_ASYNC_CLIENTS].gameFrame = predictionGameFrame;
			commands[MAX_ASYNC_CLIENTS].gameTime = predictionGameFrame * ticMsec;
			// A restart can precede the next active player snapshot. Restore the
			// chosen slot when prediction can present it again. Explicit camera
			// input or a missing target cancels restoration. A continuous valid
			// view needs no reset, preserving its normal crouch-height smoothing.
			if ( preferredFollowClient >= 0 &&
				( commands[MAX_ASYNC_CLIENTS].upmove > 0 ||
					( commands[MAX_ASYNC_CLIENTS].buttons & BUTTON_ATTACK ) != 0 ||
					( ( forcePresentationFrame || game->GetDemoFollowClient() != preferredFollowClient ) &&
						!game->SetDemoFollowClient( preferredFollowClient ) ) ) ) {
				preferredFollowClient = DEMO_FOLLOW_FREE;
			}

			const bool last = predictionGameFrame + 1 >= targetFrame;
			const gameReturn_t result = game->ClientPrediction( MAX_ASYNC_CLIENTS, commands, last );
			idAsyncNetwork::ExecuteSessionCommand( result.sessionCommand );
			// A camera the game moved to a player itself (auto-tracking) is the
			// new choice; only a release to free flight is restored above.
			const int gameFollowClient = game->GetDemoFollowClient();
			if ( gameFollowClient >= 0 ) {
				preferredFollowClient = gameFollowClient;
			}
			predictionGameFrame++;
			predicted++;
		}
	}
	game->ClientEndFrame();
	if ( sessLocal.rw != NULL ) {
		sessLocal.rw->PushMarkedDefs();
	}
	forcePresentationFrame = false;
}

/*
==================
idMultiViewDemo::ReadMetadataRecord
==================
*/
bool idMultiViewDemo::ReadMetadataRecord( const record_t &record, mvdFileInfo_t &info ) {
	if ( record.type != MVD_RECORD_METADATA || record.version != 1 ) {
		return false;
	}
	idFile_Memory payload( "MVD metadata",
		reinterpret_cast<const char *>( record.payload.Ptr() ), record.payload.Num() );
	idDict metadata;
	idStr error;
	if ( !MVD_ReadDict( &payload, metadata, error ) || payload.Tell() != payload.Length() ) {
		return false;
	}
	info.mapName = metadata.GetString( "map" );
	info.gameType = metadata.GetString( "gameType" );
	info.serverName = metadata.GetString( "serverName" );
	info.recordedAt = metadata.GetString( "recordedAt" );
	info.viewName = metadata.GetString( "viewName" );
	info.packs = metadata.GetString( "packs" );
	return true;
}

/*
==================
idMultiViewDemo::QueryFileInfo
==================
*/
bool idMultiViewDemo::QueryFileInfo( const char *name, mvdFileInfo_t &info ) {
	if ( openq4_retail::IsRetailNetDemoName( name ) ) {
		return QueryRetailFileInfo( name, info );
	}
	info = mvdFileInfo_t();
	lastError.Clear();
	const idStr path = BuildPlaybackName( name );
	if ( path.IsEmpty() ) {
		info.error = "unsafe or empty MVD path";
		return false;
	}

	idFile *source = fileSystem->OpenFileRead( path.c_str(), true );
	if ( source == NULL ) {
		info.error = "file not found";
		return false;
	}
	info.fileSize = source->Length();

	header_t inspectedHeader;
	if ( !ReadHeader( source, inspectedHeader ) ) {
		info.error = lastError;
		fileSystem->CloseFile( source );
		return false;
	}
	info.formatMajor = inspectedHeader.formatMajor;
	info.formatMinor = inspectedHeader.formatMinor;
	info.clientView = ( inspectedHeader.requiredFeatures & MVD_FEATURE_CLIENT_VIEW ) != 0;
	info.protocolMajor = inspectedHeader.protocolMajor;
	info.protocolMinor = inspectedHeader.protocolMinor;
	const bool protocolCompatible =
		game->IsDemoProtocolCompatible( ASYNC_PROTOCOL_MINOR, inspectedHeader.protocolMinor );
	const bool schemaCompatible =
		inspectedHeader.formatMinor <= 1
			? MVD_IsLegacyGameAPICompatible( inspectedHeader.gameSchemaVersion )
			: game->IsMVDSchemaCompatible(
				MVD_SchemaMajor( inspectedHeader.gameSchemaVersion ),
				MVD_SchemaMinor( inspectedHeader.gameSchemaVersion ) );
	const bool simulationCompatible =
		inspectedHeader.usercmdHz == static_cast<unsigned int>( common->GetUserCmdHz() );
	const bool contentCompatible =
		!mvd_enforceContent.GetBool() ||
		inspectedHeader.contentChecksum == static_cast<unsigned int>( declManager->GetChecksum() );
	info.compatible =
		protocolCompatible && schemaCompatible && simulationCompatible && contentCompatible;
	if ( !protocolCompatible ) {
		info.error = va( "incompatible MVD network protocol %u.%u",
			inspectedHeader.protocolMajor, inspectedHeader.protocolMinor );
	} else if ( !schemaCompatible ) {
		info.error = inspectedHeader.formatMinor <= 1
			? va( "incompatible legacy MVD game API %u", inspectedHeader.gameSchemaVersion )
			: va( "incompatible MVD game schema %d.%d",
				MVD_SchemaMajor( inspectedHeader.gameSchemaVersion ),
				MVD_SchemaMinor( inspectedHeader.gameSchemaVersion ) );
	} else if ( !simulationCompatible ) {
		info.error = va( "incompatible MVD simulation rate %u Hz", inspectedHeader.usercmdHz );
	} else if ( !contentCompatible ) {
		info.error = "incompatible MVD content checksum";
	}

	const int firstRecordOffset = source->Tell();
	if ( inspectedHeader.indexOffset >= static_cast<unsigned int>( firstRecordOffset ) &&
		 inspectedHeader.indexOffset < static_cast<unsigned int>( info.fileSize ) ) {
		bool summaryValid = true;
		bool haveMapState = false;
		bool haveNetworkState = false;
		for ( int initializationRecords = 0;
			  initializationRecords < MVD_MAX_INITIALIZATION_RECORDS;
			  initializationRecords++ ) {
			record_t record;
			const readResult_t result = ReadRecord( source, record );
			if ( result != MVD_READ_OK || !RecordIsSupported( record, false ) ) {
				summaryValid = false;
				break;
			}
			if ( MVD_RecordShouldSkip( record.type, record.version, record.flags ) ) {
				continue;
			}
			if ( record.type == MVD_RECORD_METADATA ) {
				ReadMetadataRecord( record, info );
			} else if ( record.type == MVD_RECORD_MAP_STATE ) {
				haveMapState = true;
			} else if ( record.type == MVD_RECORD_NETWORK_STATE ) {
				haveNetworkState = info.clientView ? record.version == MVD_NETWORK_STATE_CLIENT_VERSION &&
					ValidateClientNetworkState( record.payload.Ptr(), record.payload.Num(), lastError ) :
					ValidateNetworkStateRecord( record, lastError );
				if ( !haveNetworkState ) {
					summaryValid = false;
					break;
				}
			} else if ( MVD_IsLiveRecord( record.type ) ) {
				break;
			}
		}
		summaryValid = summaryValid && haveMapState && haveNetworkState;

		if ( summaryValid &&
			 source->Seek( static_cast<int>( inspectedHeader.indexOffset ), FS_SEEK_SET ) == 0 ) {
			record_t indexRecord;
			const readResult_t indexResult = ReadRecord( source, indexRecord );
			idList<int> times;
			idList<int> offsets;
			idList<int> sequences;
			int indexedEndTime = 0;
			summaryValid =
				indexResult == MVD_READ_OK &&
				indexRecord.type == MVD_RECORD_INDEX &&
				indexRecord.version == 1 &&
				( indexRecord.flags & MVD_RECORD_FLAG_REQUIRED ) == 0 &&
				MVD_ParseTimelineIndex(
					indexRecord.payload,
					firstRecordOffset,
					static_cast<int>( inspectedHeader.indexOffset ),
					times,
					offsets,
					sequences,
					indexedEndTime );

			for ( int postIndexRecords = 0;
				  summaryValid && !info.cleanEnd && postIndexRecords < 64;
				  postIndexRecords++ ) {
				record_t record;
				const readResult_t result = ReadRecord( source, record );
				if ( result != MVD_READ_OK || !RecordIsSupported( record, false ) ) {
					summaryValid = false;
					break;
				}
				if ( record.type != MVD_RECORD_END ) {
					if ( record.flags & MVD_RECORD_FLAG_REQUIRED ) {
						summaryValid = false;
					} else if ( record.version == 1 && record.type == MVD_RECORD_SUMMARY ) {
						MVD_ReadSummary( record.payload, info.players );
					} else if ( record.version == 1 && record.type == MVD_RECORD_MARKER_TABLE ) {
						std::vector<openq4_mvd::Marker> tableMarkers;
						if ( openq4_mvd::ParseMarkerTable( record.payload.Ptr(), record.payload.Num(), tableMarkers,
								SERVER_MVD_MARKER_KIND_COUNT, MAX_ASYNC_CLIENTS, MVD_MAX_INSTANCES ) ) {
							info.markerCount = static_cast<int>( tableMarkers.size() );
						}
					} else if ( record.version == 1 && record.type == MVD_RECORD_KEYFRAME_TABLE ) {
						std::vector<openq4_mvd::Keyframe> tableKeyframes;
						if ( openq4_mvd::ParseKeyframeTable( record.payload.Ptr(), record.payload.Num(),
								firstRecordOffset, static_cast<int>( inspectedHeader.indexOffset ), tableKeyframes ) ) {
							info.keyframeCount = static_cast<int>( tableKeyframes.size() );
						}
					}
					continue;
				}
				if ( record.version != 1 || record.payload.Num() != 20 ) {
					summaryValid = false;
					break;
				}
				const int endTime = static_cast<int>(
					MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
				const int reportedSnapshots = static_cast<int>(
					MVD_ReadLittleUInt( record.payload.Ptr() + 8 ) );
				const int reportedReliables = static_cast<int>(
					MVD_ReadLittleUInt( record.payload.Ptr() + 12 ) );
				const int reportedRecords = static_cast<int>(
					MVD_ReadLittleUInt( record.payload.Ptr() + 16 ) );
				if ( endTime < inspectedHeader.startGameTime ||
					 endTime < indexedEndTime ||
					 reportedSnapshots < times.Num() ||
					 ( sequences.Num() > 0 &&
					   sequences[sequences.Num() - 1] > reportedSnapshots ) ||
					 reportedReliables < 0 ||
					 reportedRecords < reportedSnapshots + reportedReliables ) {
					summaryValid = false;
					break;
				}
				info.snapshotCount = reportedSnapshots;
				info.reliableCount = reportedReliables;
				info.recordCount = reportedRecords + 1;
				info.durationMS = endTime - inspectedHeader.startGameTime;
				info.cleanEnd = true;
			}

			if ( summaryValid && info.cleanEnd ) {
				info.hasTimelineIndex = true;
				info.valid = true;
				fileSystem->CloseFile( source );
				return true;
			}
		}

		// The index is optional.  A malformed or stale index falls back to a
		// bounded linear summary rather than making an otherwise valid stream
		// unplayable.
		lastError.Clear();
		info.cleanEnd = false;
		info.hasTimelineIndex = false;
		info.snapshotCount = 0;
		info.reliableCount = 0;
		info.recordCount = 0;
		info.markerCount = 0;
		info.keyframeCount = 0;
		info.durationMS = 0;
		if ( source->Seek( firstRecordOffset, FS_SEEK_SET ) != 0 ) {
			info.error = "could not rewind MVD while reading its summary";
			fileSystem->CloseFile( source );
			return false;
		}
	}

	int lastGameTime = inspectedHeader.startGameTime;
	int lastGameFrame = inspectedHeader.startGameFrame;
	int lastSnapshotSequence = 0;
	bool valid = true;
	bool haveMapState = false;
	bool haveNetworkState = false;
	bool liveStreamStarted = false;
	bool sawIndexAtHeaderOffset = inspectedHeader.indexOffset == 0;
	bool validIndexAtHeaderOffset = inspectedHeader.indexOffset == 0;
	while ( true ) {
		if ( info.recordCount >= MVD_MAX_STREAM_RECORDS ) {
			lastError = "MVD contains too many records";
			valid = false;
			break;
		}
		const int recordOffset = source->Tell();
		record_t record;
		const readResult_t result = ReadRecord( source, record );
		if ( result == MVD_READ_EOF ) {
			break;
		}
		if ( result == MVD_READ_ERROR || !RecordIsSupported( record, false ) ) {
			valid = false;
			break;
		}
		info.recordCount++;
		if ( MVD_RecordShouldSkip( record.type, record.version, record.flags ) ) {
			continue;
		}
		if ( info.cleanEnd ) {
			lastError = va( "MVD contains record %u after its end marker", record.type );
			valid = false;
			break;
		}
		if ( inspectedHeader.indexOffset != 0 &&
			 static_cast<unsigned int>( recordOffset ) == inspectedHeader.indexOffset ) {
			sawIndexAtHeaderOffset = record.type == MVD_RECORD_INDEX;
			if ( sawIndexAtHeaderOffset ) {
				idList<int> times;
				idList<int> offsets;
				idList<int> sequences;
				int indexedEndTime = 0;
				validIndexAtHeaderOffset = MVD_ParseTimelineIndex(
					record.payload,
					firstRecordOffset,
					recordOffset,
					times,
					offsets,
					sequences,
					indexedEndTime );
			}
		}
		if ( record.type == MVD_RECORD_METADATA ) {
			if ( !ReadMetadataRecord( record, info ) ) {
				lastError = "invalid MVD metadata record";
				valid = false;
				break;
			}
		} else if ( record.type == MVD_RECORD_MAP_STATE ) {
			if ( haveMapState || liveStreamStarted ) {
				lastError = "duplicate MVD map-state record";
				valid = false;
				break;
			}
			haveMapState = true;
		} else if ( record.type == MVD_RECORD_NETWORK_STATE ) {
			if ( liveStreamStarted || haveNetworkState ||
				 !( info.clientView ? record.version == MVD_NETWORK_STATE_CLIENT_VERSION &&
					ValidateClientNetworkState( record.payload.Ptr(), record.payload.Num(), lastError ) :
					ValidateNetworkStateRecord( record, lastError ) ) ) {
				if ( lastError.IsEmpty() ) {
					lastError = "duplicate MVD network-state record";
				}
				valid = false;
				break;
			}
			haveNetworkState = true;
		} else if ( record.type == MVD_RECORD_SNAPSHOT ) {
			liveStreamStarted = true;
			if ( info.clientView || !haveMapState || !haveNetworkState ||
				 !MVD_ValidateSnapshotPayload( record.payload, record.version, lastError ) ) {
				if ( lastError.IsEmpty() ) {
					lastError = "MVD live stream began before initialization completed";
				}
				valid = false;
				break;
			}
			const int gameFrame = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) );
			const int gameTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
			const int sequence = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 8 ) );
			if ( sequence != lastSnapshotSequence + 1 ||
				 gameFrame < lastGameFrame || gameTime < lastGameTime ) {
				lastError = va( "invalid MVD timeline at snapshot %d", sequence );
				valid = false;
				break;
			}
			lastSnapshotSequence = sequence;
			lastGameFrame = gameFrame;
			lastGameTime = gameTime;
			info.snapshotCount++;
		} else if ( record.type == MVD_RECORD_RELIABLE ) {
			liveStreamStarted = true;
			if ( info.clientView || !haveMapState || !haveNetworkState ||
				 !MVD_ValidateReliablePayload( record.payload, record.version, lastError, retailSource ) ) {
				if ( lastError.IsEmpty() ) {
					lastError = "MVD live stream began before initialization completed";
				}
				valid = false;
				break;
			}
			const int gameFrame = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) );
			const int gameTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
			if ( gameFrame < lastGameFrame || gameTime < lastGameTime ) {
				lastError = "out-of-order MVD reliable timestamp";
				valid = false;
				break;
			}
			lastGameFrame = gameFrame;
			lastGameTime = gameTime;
			info.reliableCount++;
		} else if ( MVD_IsClientRecord( record.type ) ) {
			liveStreamStarted = true;
			int ignoredEnd = 0;
			if ( !info.clientView || !haveMapState || !haveNetworkState ||
				 !MVD_ScanClientRecord( record.type, record.payload, lastGameFrame, lastGameTime, ignoredEnd,
					info.snapshotCount, info.reliableCount, lastError ) ) {
				if ( lastError.IsEmpty() ) {
					lastError = "MVD client record outside a client-view recording";
				}
				valid = false;
				break;
			}
		} else if ( record.type == MVD_RECORD_MARKER ) {
			info.markerCount++;
		} else if ( record.type == MVD_RECORD_KEYFRAME || record.type == MVD_RECORD_CLIENT_KEYFRAME ) {
			info.keyframeCount++;
		} else if ( record.type == MVD_RECORD_SUMMARY ) {
			MVD_ReadSummary( record.payload, info.players );
		} else if ( record.type == MVD_RECORD_END ) {
			liveStreamStarted = true;
			if ( record.version != 1 || record.payload.Num() != 20 ) {
				lastError = "invalid MVD end record";
				valid = false;
				break;
			}
			const int endFrame = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) );
			const int endTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
			const int reportedSnapshots = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 8 ) );
			const int reportedReliables = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 12 ) );
			const int reportedRecords = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 16 ) );
			if ( !haveMapState || !haveNetworkState ||
				 endFrame < lastGameFrame || endTime < lastGameTime ||
				 reportedSnapshots != info.snapshotCount ||
				 reportedReliables != info.reliableCount ||
				 reportedRecords != info.recordCount - 1 ) {
				lastError = "MVD end record does not match the validated stream";
				valid = false;
				break;
			}
			lastGameFrame = endFrame;
			lastGameTime = endTime;
			info.cleanEnd = true;
		}
	}
	fileSystem->CloseFile( source );

	info.hasTimelineIndex = inspectedHeader.indexOffset != 0 &&
		sawIndexAtHeaderOffset && validIndexAtHeaderOffset;
	info.durationMS = Max( 0, lastGameTime - inspectedHeader.startGameTime );
	info.valid = valid && info.cleanEnd && haveMapState && haveNetworkState;
	if ( !info.valid ) {
		if ( lastError.IsEmpty() ) {
			lastError = !haveMapState || !haveNetworkState
				? "MVD initialization records are incomplete"
				: "MVD has no clean end marker";
		}
		info.error = lastError;
	}
	return info.valid;
}

/*
==================
idMultiViewDemo::Inspect
==================
*/
bool idMultiViewDemo::Inspect( const idCmdArgs &args ) {
	if ( state != MVD_IDLE ) {
		common->Printf( "Stop the active MVD before inspecting another file\n" );
		return false;
	}
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: %s <demo>\n", args.Argv( 0 ) );
		return false;
	}

	const idStr path = BuildPlaybackName( args.Argv( 1 ) );
	if ( path.IsEmpty() ) {
		common->Warning( "Unsafe or empty MVD path" );
		return false;
	}
	if ( openq4_retail::IsRetailNetDemoName( path.c_str() ) ) {
		// openQ4 MP-22: a retail network demo's container summary
		mvdFileInfo_t info;
		if ( !QueryRetailFileInfo( path.c_str(), info ) ) {
			common->Warning( "Invalid network demo '%s': %s", path.c_str(), info.error.c_str() );
			return false;
		}
		common->Printf( "Quake 4 network demo '%s'\n", path.c_str() );
		common->Printf( "  version %u, protocol %u.%u, %s\n", info.formatMajor, info.protocolMajor, info.protocolMinor,
			info.clientView ? va( "recorded by %s (their view only)", info.viewName.c_str() ) : "recorded by the server (every player)" );
		common->Printf( "  map: %s, game type: %s, server: %s\n", info.mapName.c_str(), info.gameType.c_str(), info.serverName.c_str() );
		common->Printf( "  duration: %.1f seconds, players: %s\n", info.durationMS / 1000.0f, info.players.c_str() );
		common->Printf( "  playable: %s%s%s\n", info.compatible ? "yes" : "no", info.compatible ? "" : " - ", info.error.c_str() );
		return info.compatible;
	}
	idFile *source = fileSystem->OpenFileRead( path.c_str(), true );
	if ( source == NULL ) {
		common->Printf( "MVD file not found: %s\n", path.c_str() );
		return false;
	}

	header_t inspectedHeader;
	if ( !ReadHeader( source, inspectedHeader ) ) {
		common->Warning( "Invalid MVD '%s': %s", path.c_str(), lastError.c_str() );
		fileSystem->CloseFile( source );
		return false;
	}

	int records = 0;
	int snapshots = 0;
	int reliables = 0;
	int markerRecords = 0;
	int keyframeRecords = 0;
	int userInfoRecords = 0;
	int clientCommands = 0;
	int optionalUnknown = 0;
	int firstGameTime = -1;
	int lastGameTime = inspectedHeader.startGameTime;
	bool cleanEnd = false;
	bool valid = true;
	while ( true ) {
		record_t record;
		const readResult_t result = ReadRecord( source, record );
		if ( result == MVD_READ_EOF ) {
			break;
		}
		if ( result == MVD_READ_ERROR || !RecordIsSupported( record, false ) ) {
			valid = false;
			break;
		}
		records++;
		if ( record.type == MVD_RECORD_SNAPSHOT || record.type == MVD_RECORD_CLIENT_SNAPSHOT ) {
			snapshots++;
		} else if ( record.type == MVD_RECORD_RELIABLE || record.type == MVD_RECORD_CLIENT_APPLY ||
					record.type == MVD_RECORD_CLIENT_RELIABLE ) {
			reliables++;
		} else if ( record.type == MVD_RECORD_CLIENT_USERCMD ) {
			clientCommands++;
		} else if ( record.type == MVD_RECORD_END ) {
			cleanEnd = true;
		} else if ( record.type == MVD_RECORD_MARKER ) {
			markerRecords++;
		} else if ( record.type == MVD_RECORD_KEYFRAME || record.type == MVD_RECORD_CLIENT_KEYFRAME ) {
			keyframeRecords++;
		} else if ( record.type == MVD_RECORD_USERINFO ) {
			userInfoRecords++;
		} else if ( record.type != MVD_RECORD_METADATA &&
					record.type != MVD_RECORD_MAP_STATE &&
					record.type != MVD_RECORD_NETWORK_STATE &&
					record.type != MVD_RECORD_INDEX &&
					record.type != MVD_RECORD_MARKER_TABLE &&
					record.type != MVD_RECORD_KEYFRAME_TABLE ) {
			optionalUnknown++;
		}
		int recordTime;
		if ( RecordTimestamp( record, recordTime ) ) {
			if ( firstGameTime < 0 ) {
				firstGameTime = recordTime;
			}
			lastGameTime = Max( lastGameTime, recordTime );
		}
	}

	const int length = source->Length();
	fileSystem->CloseFile( source );
	if ( !valid ) {
		common->Warning( "Invalid MVD '%s': %s", path.c_str(), lastError.c_str() );
		return false;
	}

	idStr size;
	size.BestUnit( "%.2f", static_cast<float>( length ), MEASURE_SIZE );
	common->Printf(
		"MVD '%s'\n"
		"  format: %u.%u, protocol: %u.%u, simulation: %u Hz\n"
		"  size: %s, records: %d, snapshots: %d, reliables: %d\n"
		"  markers: %d, keyframes: %d, user-info updates: %d\n"
		"  duration: %.1f seconds, clean end: %s, optional unknown records: %d\n"
		"  view: %s, recorder's commands: %d\n",
		path.c_str(),
		inspectedHeader.formatMajor, inspectedHeader.formatMinor,
		inspectedHeader.protocolMajor, inspectedHeader.protocolMinor,
		inspectedHeader.usercmdHz,
		size.c_str(), records, snapshots, reliables,
		markerRecords, keyframeRecords, userInfoRecords,
		Max( 0, lastGameTime - inspectedHeader.startGameTime ) * 0.001f,
		cleanEnd ? "yes" : "no", optionalUnknown,
		( inspectedHeader.requiredFeatures & MVD_FEATURE_CLIENT_VIEW ) != 0 ? "one player's (client-view recording)" :
			"the whole match", clientCommands );
	return true;
}

/*
==================
idMultiViewDemo::Recover

Salvages an interrupted recording. A .mvd.part is the staged stream a recording
leaves behind when it never reached its clean end. Every record is length-
delimited and checksummed, so its complete prefix is still valid. This copies
the header and every complete record, up to the first truncated or corrupt one,
into a new staged stream, rebuilds the timeline index, appends the end record
the recorder would have written at that point and publishes the result under a
new name. The source is never modified and no existing demo is overwritten.
==================
*/
bool idMultiViewDemo::Recover( const idCmdArgs &args ) {
	if ( state != MVD_IDLE ) {
		common->Printf( "Stop the active MVD before recovering a recording\n" );
		return false;
	}
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: %s <demo.mvd.part>\n", args.Argv( 0 ) );
		return false;
	}
	// The writer state below is borrowed from an idle recorder; start it clean.
	Clear();

	idStr sourcePath = args.Argv( 1 );
	sourcePath.BackSlashesToSlashes();
	if ( sourcePath.IsEmpty() || sourcePath.Find( ".." ) >= 0 || sourcePath.Find( ":" ) >= 0 ||
		 sourcePath[0] == '/' ) {
		common->Warning( "Unsafe or empty MVD path" );
		return false;
	}
	if ( idStr::Icmpn( sourcePath.c_str(), "demos/", 6 ) != 0 ) {
		sourcePath = "demos/" + sourcePath;
	}
	const char *partSuffix = ".part";
	const int partLength = static_cast<int>( strlen( partSuffix ) );
	if ( sourcePath.Length() <= partLength ||
		 idStr::Icmp( sourcePath.c_str() + sourcePath.Length() - partLength, partSuffix ) != 0 ) {
		sourcePath.DefaultFileExtension( ".mvd" );
		sourcePath += partSuffix;
	}

	idFile *source = fileSystem->OpenFileRead( sourcePath.c_str(), true );
	if ( source == NULL ) {
		common->Printf( "MVD_RECOVER missing source=\"%s\"\n", sourcePath.c_str() );
		return false;
	}
	header_t sourceHeader;
	if ( !ReadHeader( source, sourceHeader ) ) {
		common->Printf( "MVD_RECOVER failed source=\"%s\" reason=\"header: %s\"\n", sourcePath.c_str(), lastError.c_str() );
		fileSystem->CloseFile( source );
		return false;
	}

	// demos/<stem>_recovered.mvd, or the next free numbered name.
	idStr stem = sourcePath;
	stem.StripPath();
	stem.StripFileExtension();
	stem.StripFileExtension();
	stem += "_recovered";
	idCmdArgs nameArgs;
	nameArgs.AppendArg( args.Argv( 0 ) );
	nameArgs.AppendArg( stem.c_str() );
	fileName = BuildRecordingName( nameArgs );
	if ( fileName.IsEmpty() ) {
		common->Printf( "MVD_RECOVER failed source=\"%s\" reason=\"no unused name\"\n", sourcePath.c_str() );
		fileSystem->CloseFile( source );
		Clear();
		return false;
	}
	tempFileName = fileName;
	tempFileName += partSuffix;
	file = fileSystem->OpenFileWrite( tempFileName.c_str(), "fs_savepath" );
	if ( file == NULL ) {
		common->Printf( "MVD_RECOVER failed source=\"%s\" reason=\"cannot open %s\"\n", sourcePath.c_str(), tempFileName.c_str() );
		fileSystem->CloseFile( source );
		Clear();
		return false;
	}

	header = sourceHeader;
	header.indexOffset = 0;
	lastSnapshotGameFrame = header.startGameFrame;
	lastSnapshotGameTime = header.startGameTime;
	int lastLiveFrame = header.startGameFrame;
	int lastLiveTime = header.startGameTime;
	int discarded = 0;
	idStr tail = "end of file";
	bool ok = WriteHeader();
	while ( ok ) {
		record_t record;
		const readResult_t result = ReadRecord( source, record );
		if ( result == MVD_READ_EOF ) {
			break;
		}
		if ( result == MVD_READ_ERROR ) {
			// The interrupted tail: everything before it is intact.
			tail = lastError;
			break;
		}
		if ( record.type == MVD_RECORD_INDEX || record.type == MVD_RECORD_END ||
			 record.type == MVD_RECORD_MARKER_TABLE || record.type == MVD_RECORD_KEYFRAME_TABLE ) {
			// A finalize that failed part-way leaves these; all are rebuilt below.
			discarded++;
			continue;
		}
		if ( recordCount >= MVD_MAX_STREAM_RECORDS ) {
			tail = "stream record limit";
			break;
		}
		const int outputOffset = file->Tell();
		if ( !WriteRecord( record.type, record.version, record.flags, record.payload.Ptr(), record.payload.Num(), false ) ) {
			ok = false;
			break;
		}
		if ( MVD_IsLiveRecord( record.type ) && record.payload.Num() >= 8 ) {
			lastLiveFrame = Max( lastLiveFrame, static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) ) );
			lastLiveTime = Max( lastLiveTime, static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) ) );
		}
		if ( record.type == MVD_RECORD_MARKER || record.type == MVD_RECORD_KEYFRAME ) {
			// Copied verbatim, so its offset and counts still describe the output.
			NoteNavigationRecord( record, outputOffset );
		}
		if ( record.type == MVD_RECORD_RELIABLE ) {
			reliableCount++;
		} else if ( record.type == MVD_RECORD_CLIENT_APPLY || record.type == MVD_RECORD_CLIENT_RELIABLE ) {
			// openQ4 MP-22: a client-view recording counts as its recorder did
			reliableCount++;
		} else if ( record.type == MVD_RECORD_CLIENT_SNAPSHOT ) {
			snapshotCount++;
		} else if ( record.type == MVD_RECORD_SNAPSHOT && record.payload.Num() >= 12 ) {
			// The same index the recorder builds in CaptureServerFrame.
			const int gameTime = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 4 ) );
			snapshotSequence = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() + 8 ) );
			snapshotCount++;
			lastSnapshotGameFrame = static_cast<int>( MVD_ReadLittleUInt( record.payload.Ptr() ) );
			lastSnapshotGameTime = gameTime;
			lastSnapshotFileOffset = outputOffset;
			if ( recordingIndex.Num() == 0 ||
				 ( gameTime - recordingIndex[recordingIndex.Num() - 1].gameTime >= MVD_INDEX_INTERVAL_MS &&
				   recordingIndex.Num() < MVD_MAX_INDEX_ENTRIES ) ) {
				indexEntry_t index;
				index.gameTime = gameTime;
				index.fileOffset = outputOffset;
				index.sequence = snapshotSequence;
				recordingIndex.Append( index );
			}
		}
	}
	const int sourceBytes = source->Length();
	fileSystem->CloseFile( source );

	if ( ok && snapshotCount == 0 ) {
		lastError = "no complete snapshot to recover";
		ok = false;
	}
	if ( ok ) {
		ok = WriteIndexRecord() && WriteNavigationRecords();
	}
	if ( ok ) {
		idFile_Memory payload( "MVD end" );
		payload.WriteInt( lastLiveFrame );
		payload.WriteInt( lastLiveTime );
		payload.WriteInt( snapshotCount );
		payload.WriteInt( reliableCount );
		payload.WriteInt( recordCount );
		ok = WriteRecord( MVD_RECORD_END, 1, 0, payload.GetDataPtr(), payload.Length(), false );
	}
	if ( ok && !file->Sync() ) {
		lastError = "sync failed";
		ok = false;
	}
	fileSystem->CloseFile( file );
	file = NULL;
	if ( ok && !fileSystem->PromoteFile( tempFileName.c_str(), fileName.c_str(), "fs_savepath" ) ) {
		lastError = "could not publish the recovered stream";
		ok = false;
	}

	if ( ok ) {
		common->Printf( "MVD_RECOVER recovered source=\"%s\" output=\"%s\" snapshots=%d reliables=%d records=%d duration=%.1f discarded=%d tail=\"%s\" sourceBytes=%d\n",
			sourcePath.c_str(), fileName.c_str(), snapshotCount, reliableCount, recordCount,
			Max( 0, lastLiveTime - header.startGameTime ) * 0.001f, discarded, tail.c_str(), sourceBytes );
	} else {
		common->Printf( "MVD_RECOVER failed source=\"%s\" reason=\"%s\"\n", sourcePath.c_str(), lastError.c_str() );
		fileSystem->RemoveFile( tempFileName.c_str(), "fs_savepath" );
	}
	Clear();
	return ok;
}

/*
==================
Command callbacks
==================
*/
void idMultiViewDemo::Record_f( const idCmdArgs &args ) {
	idAsyncNetwork::multiViewDemo.StartRecording( args );
}

void idMultiViewDemo::Stop_f( const idCmdArgs &args ) {
	if ( idAsyncNetwork::multiViewDemo.IsRecording() ) {
		idAsyncNetwork::multiViewDemo.StopRecording( "console command", true );
	} else if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		idAsyncNetwork::multiViewDemo.FinishPlayback( "console command", false );
	} else {
		common->Printf( "MVD system is idle\n" );
	}
}

void idMultiViewDemo::Play_f( const idCmdArgs &args ) {
	const char *activeModule = cvarSystem->GetCVarString( "com_activeGameModule" );
	if ( idStr::Icmp( activeModule, "game_mp" ) != 0 ) {
		if ( args.Argc() != 2 ) {
			common->Printf( "usage: %s <demo>\n", args.Argv( 0 ) );
			return;
		}
		cvarSystem->SetCVarString( "com_nextGameModule", "game_mp" );
		idCmdArgs reloadArgs;
		reloadArgs.AppendArg( "playMVD" );
		reloadArgs.AppendArg( args.Argv( 1 ) );
		cmdSystem->SetupReloadGameModule( reloadArgs );
		return;
	}
	if ( !idAsyncNetwork::multiViewDemo.StartPlayback( args ) ) {
#ifndef ID_DEDICATED
		if ( sessLocal.guiDemoMenu != NULL ) {
			sessLocal.OpenDemoMenu( true );
		}
#endif
	}
}

/*
==================
idMultiViewDemo::TimeDemo_f

openQ4 MP-22: playNetTimeDemo, from the multiplayer game module as playNetDemo.
==================
*/
void idMultiViewDemo::TimeDemo_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: %s <demo>\n", args.Argv( 0 ) );
		return;
	}
	if ( idStr::Icmp( cvarSystem->GetCVarString( "com_activeGameModule" ), "game_mp" ) != 0 ) {
		cvarSystem->SetCVarString( "com_nextGameModule", "game_mp" );
		idCmdArgs reloadArgs;
		reloadArgs.AppendArg( "playNetTimeDemo" );
		reloadArgs.AppendArg( args.Argv( 1 ) );
		cmdSystem->SetupReloadGameModule( reloadArgs );
		return;
	}
	idAsyncNetwork::multiViewDemo.StartTimeDemo( args.Argv( 1 ), false );
}

void idMultiViewDemo::Info_f( const idCmdArgs &args ) {
	idAsyncNetwork::multiViewDemo.Inspect( args );
}

void idMultiViewDemo::Recover_f( const idCmdArgs &args ) {
	idAsyncNetwork::multiViewDemo.Recover( args );
}

void idMultiViewDemo::Status_f( const idCmdArgs &args ) {
	const idMultiViewDemo &demo = idAsyncNetwork::multiViewDemo;
	common->Printf( "MVD_STATUS playing=%d paused=%d seeking=%d time=%d duration=%d follow=%d\n",
		demo.IsPlaying(), demo.IsPaused(), demo.IsSeeking(), demo.GetPlaybackTimeMS(),
		demo.GetPlaybackDurationMS(), demo.GetFollowClient() );
	// openQ4 MP-22: a client-view recording's player and how far its snapshots went
	if ( demo.IsClientViewPlayback() ) {
		common->Printf( "MVD_VIEW client=%d sequence=%d snapshots=%d frame=%d predicted=%d\n", demo.viewClient,
			demo.snapshotSequence, demo.snapshotCount, demo.latestSnapshotGameFrame, demo.predictionGameFrame );
	}
	// A server operator can see a recording is healthy without stopping it,
	// and what became of the last one.
	if ( demo.state == MVD_RECORDING ) {
		common->Printf( "MVD_RECORDING view=%s path=\"%s\" staged=\"%s\" bytes=%d elapsed=%d snapshots=%d reliable=%d records=%d\n",
			demo.clientView ? "client" : "server", demo.fileName.c_str(), demo.tempFileName.c_str(), demo.file != NULL ? demo.file->Tell() : 0,
			Sys_Milliseconds() - demo.recordingStartRealTime, demo.snapshotCount, demo.reliableCount,
			demo.recordCount );
		common->Printf( "MVD_RECORDING_BUDGET libraryBytes=%lld\n",
			static_cast<long long>( demo.recordingLibraryByteLimit ) );
	}
	if ( demo.recordingResultValid ) {
		static const char * const states[] = { "pending", "committed", "failed" };
		const int resultState = demo.recordingResult.state;
		common->Printf( "MVD_LAST_RESULT state=%s reason=%d final=\"%s\" partial=\"%s\"\n",
			resultState >= 0 && resultState < SERVER_MVD_RESULT_STATE_COUNT ? states[ resultState ] : "unknown",
			static_cast<int>( demo.recordingResult.reason ), demo.recordingResult.finalQPath,
			demo.recordingResult.partialQPath );
	}
}

void idMultiViewDemo::Library_f( const idCmdArgs &args ) {
	openq4_mvd::LibraryUsage usage;
	if ( !openq4_mvd::ScanLibrary( std::filesystem::u8path(
			fileSystem->RelativePathToOSPath( "demos", "fs_savepath" ) ), usage ) ) {
		common->Printf( "MVD_LIBRARY unavailable: directory is linked, unreadable or exceeds the scan limit\n" );
		return;
	}
	common->Printf( "MVD_LIBRARY recordings=%u bytes=%lld quotaMB=%d automaticDeletion=0\n",
		usage.recordings, static_cast<long long>( usage.bytes ), mvd_libraryQuotaMB.GetInteger() );
}

void idMultiViewDemo::Pause_f( const idCmdArgs &args ) {
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	if ( args.Argc() > 2 ) {
		common->Printf( "usage: %s [0|1]\n", args.Argv( 0 ) );
		return;
	}
	if ( args.Argc() == 2 ) {
		idAsyncNetwork::multiViewDemo.SetPaused( atoi( args.Argv( 1 ) ) != 0 );
	} else {
		idAsyncNetwork::multiViewDemo.TogglePaused();
	}
	common->Printf( "MVD playback %s\n",
		idAsyncNetwork::multiViewDemo.IsPaused() ? "paused" : "resumed" );
}

void idMultiViewDemo::Seek_f( const idCmdArgs &args ) {
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: %s <seconds>\n", args.Argv( 0 ) );
		return;
	}
	const double seconds = atof( args.Argv( 1 ) );
	const double durationMS = static_cast<double>( idAsyncNetwork::multiViewDemo.GetPlaybackDurationMS() );
	const double boundedMS = Min( durationMS, Max( 0.0, seconds * 1000.0 ) );
	idAsyncNetwork::multiViewDemo.SeekToMS( static_cast<int>( boundedMS ) );
}

void idMultiViewDemo::Skip_f( const idCmdArgs &args ) {
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: %s <seconds>\n", args.Argv( 0 ) );
		return;
	}
	const double seconds = Min( 86400.0, Max( -86400.0, atof( args.Argv( 1 ) ) ) );
	idAsyncNetwork::multiViewDemo.SeekByMS( static_cast<int>( seconds * 1000.0 ) );
}

void idMultiViewDemo::Speed_f( const idCmdArgs &args ) {
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	if ( args.Argc() == 1 ) {
		common->Printf( "MVD playback speed: %.2fx\n",
			idAsyncNetwork::multiViewDemo.GetPlaybackScale() );
		return;
	}
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: %s <0.05-16>\n", args.Argv( 0 ) );
		return;
	}
	idAsyncNetwork::multiViewDemo.SetPlaybackScale( static_cast<float>( atof( args.Argv( 1 ) ) ) );
	common->Printf( "MVD playback speed: %.2fx\n",
		idAsyncNetwork::multiViewDemo.GetPlaybackScale() );
}

void idMultiViewDemo::Step_f( const idCmdArgs &args ) {
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	if ( args.Argc() > 2 ) {
		common->Printf( "usage: %s [frames]\n", args.Argv( 0 ) );
		return;
	}
	const int frames = args.Argc() == 2 ? idMath::ClampInt( 1, 1000, atoi( args.Argv( 1 ) ) ) : 1;
	idAsyncNetwork::multiViewDemo.StepFrames( frames );
}

void idMultiViewDemo::FollowNext_f( const idCmdArgs &args ) {
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	idAsyncNetwork::multiViewDemo.FollowNext();
}

void idMultiViewDemo::FollowPrev_f( const idCmdArgs &args ) {
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	idAsyncNetwork::multiViewDemo.FollowPrev();
}

void idMultiViewDemo::Follow_f( const idCmdArgs &args ) {
	idMultiViewDemo &demo = idAsyncNetwork::multiViewDemo;
	if ( !demo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	if ( args.Argc() == 1 || !idStr::Icmp( args.Argv( 1 ), "next" ) ) {
		demo.FollowNext();
		return;
	}
	if ( !idStr::Icmp( args.Argv( 1 ), "prev" ) ) {
		demo.FollowPrev();
		return;
	}
	if ( !idStr::Icmp( args.Argv( 1 ), "free" ) ) {
		demo.FreeRoam();
		return;
	}
	// A name may contain spaces, so everything after the command is the target.
	const char *target = args.Args( 1, -1 );
	const int clientNum = demo.FindFollowClient( target );
	if ( clientNum == -2 ) {
		common->Printf( "MVD_FOLLOW ambiguous target=\"%s\": more than one player name contains it\n", target );
		return;
	}
	if ( clientNum < 0 || !demo.FollowClient( clientNum ) ) {
		common->Printf( "MVD_FOLLOW unavailable target=\"%s\"\n", target );
		return;
	}
	idStr name;
	demo.CopyFollowName( clientNum, name );
	common->Printf( "MVD_FOLLOW client=%d name=\"%s\"\n", clientNum, name.c_str() );
}

void idMultiViewDemo::FreeRoam_f( const idCmdArgs &args ) {
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	idAsyncNetwork::multiViewDemo.FreeRoam();
}

void idMultiViewDemo::Mark_f( const idCmdArgs &args ) {
	if ( !idAsyncNetwork::multiViewDemo.IsRecording() ) {
		common->Printf( "No MVD is recording\n" );
		return;
	}
	if ( args.Argc() < 2 ) {
		common->Printf( "usage: %s <note>\n", args.Argv( 0 ) );
		return;
	}
	serverMVDMarker_t marker;
	memset( &marker, 0, sizeof( marker ) );
	marker.kind = SERVER_MVD_MARKER_NOTE;
	marker.client = -1;
	marker.instance = -1;
	idStr::Copynz( marker.text, args.Args( 1, -1 ), sizeof( marker.text ) );
	if ( idAsyncNetwork::multiViewDemo.RecordMarker( marker ) ) {
		common->Printf( "MVD_MARK recorded text=\"%s\"\n", marker.text );
	} else {
		common->Printf( "MVD_MARK refused\n" );
	}
}

void idMultiViewDemo::Markers_f( const idCmdArgs &args ) {
	const idMultiViewDemo &demo = idAsyncNetwork::multiViewDemo;
	if ( !demo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	const int kindFilter = args.Argc() > 1 ? MVD_MarkerKindFromName( args.Argv( 1 ) ) : 0;
	if ( kindFilter < 0 ) {
		common->Printf( "usage: %s [phase|round|pause|score|kill|flag|powerup|note]\n", args.Argv( 0 ) );
		return;
	}
	int listed = 0;
	for ( int i = 0; i < static_cast<int>( demo.markers.size() ); i++ ) {
		const openq4_mvd::Marker &marker = demo.markers[i];
		if ( !openq4_mvd::MarkerMatches( marker, kindFilter ) ) {
			continue;
		}
		const int relative = marker.gameTime - demo.header.startGameTime;
		common->Printf( "MVD_MARKER index=%d time=%d (%d:%02d) kind=%s client=%d value0=%d value1=%d text=\"%s\"\n",
			i, relative, relative / 60000, ( relative / 1000 ) % 60, MVD_MarkerKindName( marker.kind ),
			marker.client, marker.value0, marker.value1, marker.text );
		listed++;
	}
	common->Printf( "MVD_MARKERS listed=%d total=%d keyframes=%d\n",
		listed, static_cast<int>( demo.markers.size() ), static_cast<int>( demo.keyframes.size() ) );
}

void idMultiViewDemo::Marker_f( const idCmdArgs &args ) {
	idMultiViewDemo &demo = idAsyncNetwork::multiViewDemo;
	if ( !demo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	const char *direction = args.Argc() > 1 ? args.Argv( 1 ) : "next";
	const int kindFilter = args.Argc() > 2 ? MVD_MarkerKindFromName( args.Argv( 2 ) ) : 0;
	const bool next = !idStr::Icmp( direction, "next" );
	if ( kindFilter < 0 || ( !next && idStr::Icmp( direction, "prev" ) ) ) {
		common->Printf( "usage: %s next|prev [phase|round|pause|score|kill|flag|powerup|note]\n", args.Argv( 0 ) );
		return;
	}
	if ( !demo.SeekToMarker( next ? 1 : -1, kindFilter ) ) {
		common->Printf( "MVD_MARKER none\n" );
	}
}

void idMultiViewDemo::AutoTrack_f( const idCmdArgs &args ) {
	idCVar *autoTrack = cvarSystem->Find( "demo_autoTrack" );
	idCVar *autoTrackTeam = cvarSystem->Find( "demo_autoTrackTeam" );
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() || autoTrack == NULL || autoTrackTeam == NULL ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	if ( MVD_ClientViewCamera( idAsyncNetwork::multiViewDemo.IsClientViewPlayback() ) ) {
		return;
	}
	static const char * const modes[] = { "off", "killer", "action" };
	if ( args.Argc() > 1 ) {
		int mode = -1;
		for ( int i = 0; i < 3; i++ ) {
			if ( !idStr::Icmp( args.Argv( 1 ), modes[i] ) ) {
				mode = i;
			}
		}
		const char *team = args.Argc() > 2 ? args.Argv( 2 ) : autoTrackTeam->GetString();
		if ( mode < 0 || ( idStr::Icmp( team, "any" ) && idStr::Icmp( team, "marine" ) &&
			 idStr::Icmp( team, "strogg" ) && team[0] != '\0' ) ) {
			common->Printf( "usage: %s [off|killer|action] [any|marine|strogg]\n", args.Argv( 0 ) );
			return;
		}
		autoTrackTeam->SetString( team[0] != '\0' ? team : "any" );
		autoTrack->SetInteger( mode );
	}
	const int mode = idMath::ClampInt( 0, 2, autoTrack->GetInteger() );
	common->Printf( "MVD_AUTOTRACK mode=%s team=%s\n", modes[mode], autoTrackTeam->GetString() );
}

/*
==================
idMultiViewDemo::OpenRetailNetDemo

openQ4 MP-22: a retail Quake 4 1.4.2 network demo. Every length in the file is
checked and its packets decoded first (RetailNetDemo.cpp), then its events are
written as this player's own records, in memory, and played from there: a
client demo as a client-view recording, a server demo as a server-view one.
The game module reads the recorded game data in the 1.4.2 layout, which the
recording's protocol, 2.85, selects, and the recording's decl numbering is
rebuilt from the paks it names.
==================
*/
bool idMultiViewDemo::OpenRetailNetDemo() {
	idFile *source = fileSystem->OpenFileRead( fileName.c_str(), true );
	if ( source == NULL ) {
		lastError = "file not found";
		return false;
	}
	const int length = source->Length();
	if ( length <= 0 || length > openq4_retail::NETDEMO_MAX_FILE_BYTES ) {
		fileSystem->CloseFile( source );
		lastError = "the file is empty or larger than any network demo openQ4 reads";
		return false;
	}
	idList<byte> bytes;
	bytes.SetNum( length );
	const int read = source->Read( bytes.Ptr(), length );
	fileSystem->CloseFile( source );
	if ( read != length ) {
		lastError = "could not read the network demo";
		return false;
	}

	openq4_retail::retailNetDemo_t demo;
	if ( !openq4_retail::ParseContainer( bytes.Ptr(), length, demo, lastError ) ) {
		return false;
	}
	const int fps = demo.serverInfo.GetInt( "si_fps", "60" );
	if ( fps != common->GetUserCmdHz() ) {
		lastError = va( "recorded at %d game frames a second, and openQ4 runs %d", fps, common->GetUserCmdHz() );
		return false;
	}
	idStr missing;
	if ( openq4_retail::MissingPaks( demo, missing ) > 0 ) {
		lastError = va( "recorded with paks this installation does not have: %s", missing.c_str() );
		return false;
	}
	if ( !openq4_retail::Decode( bytes.Ptr(), length, demo, Max( 1, common->GetUserCmdMSec() ), lastError ) ) {
		return false;
	}

	openq4_retail::BuildDeclRemap( demo );
	retailEntityDefs = openq4_retail::RetailDeclCount( DECL_ENTITYDEF );
	idFile_Memory stream( "retail network demo" );
	if ( !BuildRetailStream( demo, stream ) ) {
		openq4_retail::ClearDeclRemap();
		return false;
	}
	retailStream.SetNum( stream.Length() );
	memcpy( retailStream.Ptr(), stream.GetDataPtr(), stream.Length() );
	file = new idFile_Memory( fileName.c_str(), reinterpret_cast<const char *>( retailStream.Ptr() ), retailStream.Num() );
	retailSource = true;
	common->Printf( "Read Quake 4 1.4.2 %s demo '%s': %d packets, %d snapshots, %d reliable messages%s%s\n",
		demo.serverDemo ? "server" : "client", fileName.c_str(), static_cast<int>( demo.packets.size() ),
		demo.snapshotCount, demo.reliableCount,
		demo.droppedPackets > 0 ? va( ", %d packets the recording client dropped", demo.droppedPackets ) : "",
		idStr::Icmp( demo.endReason, "complete" ) ? va( ", ending where %s", demo.endReason.c_str() ) : "" );
	return true;
}

/*
==================
idMultiViewDemo::BuildRetailStream

Writes a decoded retail demo as a format 1.3 recording: the header, metadata,
map state and network state, then one record per event and the end record.
==================
*/
bool idMultiViewDemo::BuildRetailStream( openq4_retail::retailNetDemo_t &demo, idFile_Memory &out ) {
	idFile *const savedFile = file;
	file = &out;
	recordCount = 0;
	auto finish = [&]( bool result ) {
		file = savedFile;
		recordCount = 0;
		return result;
	};

	const bool server = demo.serverDemo;
	int schemaMajor = 1;
	int schemaMinor = 0;
	game->GetMVDSchemaVersion( schemaMajor, schemaMinor );
	header.formatMajor = MVD_FORMAT_MAJOR;
	header.formatMinor = MVD_FORMAT_MINOR;
	header.requiredFeatures = server ? MVD_SERVER_VIEW_REQUIRED_FEATURES : MVD_CLIENT_VIEW_REQUIRED_FEATURES;
	header.optionalFeatures = MVD_OPTIONAL_CLEAN_END | MVD_OPTIONAL_USERINFO;
	header.protocolMajor = static_cast<unsigned short>( demo.protocolMajor );
	header.protocolMinor = static_cast<unsigned short>( demo.protocolMinor );
	header.gameSchemaVersion = MVD_PackSchemaVersion( schemaMajor, schemaMinor );
	header.usercmdHz = static_cast<unsigned int>( common->GetUserCmdHz() );
	header.startGameFrame = demo.gameFrame;
	header.startGameTime = demo.gameTime;
	header.snapshotDelay = 0;
	header.contentChecksum = static_cast<unsigned int>( declManager->GetChecksum() );
	header.indexOffset = 0;
	if ( !WriteHeader() ) {
		return finish( false );
	}

	// metadata: what the library and the deck show
	idDict metadata;
	metadata.Set( "format", "Quake 4 1.4.2 network demo" );
	metadata.Set( "engineVersion", demo.serverInfo.GetString( "si_version" ) );
	metadata.Set( "map", demo.serverInfo.GetString( "si_map" ) );
	metadata.Set( "gameType", demo.serverInfo.GetString( "si_gameType" ) );
	metadata.Set( "serverName", demo.serverInfo.GetString( "si_name" ) );
	if ( !server ) {
		metadata.Set( "view", "client" );
		metadata.SetInt( "viewClient", demo.clientNum );
		metadata.Set( "viewName", demo.userInfo[ demo.clientNum ].GetString( "ui_name" ) );
	}
	idFile_Memory metadataPayload( "retail metadata" );
	metadata.WriteToFileHandle( &metadataPayload );
	if ( !WriteRecord( MVD_RECORD_METADATA, 1, 0, metadataPayload.GetDataPtr(), metadataPayload.Length(), false ) ) {
		return finish( false );
	}

	// map state: the server info, the synced cvars openQ4 also synchronizes, the players
	idDict serverInfo = demo.serverInfo;
	serverInfo.Delete( "si_password" );
	serverInfo.Delete( "password" );
	idDict synced;
	const idDict *networkSynced = cvarSystem->MoveCVarsToDict( CVAR_NETWORKSYNC );
	for ( int i = 0; i < demo.syncedCVars.GetNumKeyVals(); i++ ) {
		const idKeyValue *kv = demo.syncedCVars.GetKeyVal( i );
		if ( kv != NULL && networkSynced != NULL && networkSynced->FindKey( kv->GetKey() ) != NULL ) {
			synced.Set( kv->GetKey(), kv->GetValue() );
		}
	}
	idFile_Memory mapState( "retail map state" );
	mapState.WriteUnsignedInt( MAX_ASYNC_CLIENTS );
	serverInfo.WriteToFileHandle( &mapState );
	synced.WriteToFileHandle( &mapState );
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		demo.userInfo[i].WriteToFileHandle( &mapState );
	}
	if ( !WriteRecord( MVD_RECORD_MAP_STATE, 1, MVD_RECORD_FLAG_REQUIRED, mapState.GetDataPtr(), mapState.Length(), false ) ) {
		return finish( false );
	}

	// network state: a client demo's engine part first, then the game's bytes as 1.4.2 wrote them
	const int ticMsec = Max( 1, common->GetUserCmdMSec() );
	idFile_Memory networkState( "retail network state" );
	if ( !server ) {
		networkState.WriteUnsignedChar( static_cast<unsigned char>( demo.clientNum ) );
		networkState.WriteInt( demo.snapshotSequence );
		networkState.WriteInt( demo.snapshotGameFrame );
		networkState.WriteInt( demo.snapshotGameTime );
		networkState.WriteInt( demo.gameFrame );
		networkState.WriteInt( demo.gameTime );
		idList<byte> commands;
		int count = 0;
		for ( size_t i = 0; i < demo.initialUsercmds.size(); i++ ) {
			const openq4_retail::retailUsercmd_t &retailCmd = demo.initialUsercmds[i];
			if ( retailCmd.gameFrame < demo.snapshotGameFrame ||
				 retailCmd.gameFrame >= demo.snapshotGameFrame + MAX_USERCMD_BACKUP ) {
				continue;
			}
			usercmd_t cmd;
			openq4_retail::ToUsercmd( retailCmd, cmd );
			byte buffer[128];
			idBitMsg msg;
			msg.Init( buffer, sizeof( buffer ) );
			idAsyncNetwork::WriteUserCmdDelta( msg, cmd, NULL );
			const int start = commands.Num();
			commands.SetNum( start + 7 + msg.GetSize() );
			const int littleFrame = LittleLong( retailCmd.gameFrame );
			memcpy( commands.Ptr() + start, &littleFrame, 4 );
			commands[ start + 4 ] = static_cast<byte>( demo.initialUsercmdClients[i] );
			commands[ start + 5 ] = static_cast<byte>( msg.GetSize() & 0xff );
			commands[ start + 6 ] = static_cast<byte>( msg.GetSize() >> 8 );
			memcpy( commands.Ptr() + start + 7, msg.GetData(), msg.GetSize() );
			count++;
		}
		networkState.WriteInt( count );
		if ( commands.Num() > 0 ) {
			networkState.Write( commands.Ptr(), commands.Num() );
		}
	}
	networkState.Write( demo.gameNetworkInfo.Ptr(), demo.gameNetworkInfo.Num() );
	if ( !WriteRecord( MVD_RECORD_NETWORK_STATE, server ? 1 : MVD_NETWORK_STATE_CLIENT_VERSION, MVD_RECORD_FLAG_REQUIRED,
			networkState.GetDataPtr(), networkState.Length(), false ) ) {
		return finish( false );
	}

	// the events
	const int headerBytes = 4 + 1 + 4 + 4 + 4 + 1 + 2;	// the snapshot's engine header
	int snapshots = 0;
	int reliables = 0;
	int lastFrame = demo.gameFrame;
	int lastTime = demo.gameTime;
	for ( size_t e = 0; e < demo.events.size(); e++ ) {
		const openq4_retail::retailEvent_t &event = demo.events[e];
		const int frame = Max( lastFrame, event.frame );
		const int time = Max( lastTime, event.time );
		lastFrame = frame;
		lastTime = time;
		idFile_Memory payload( "retail event" );
		payload.WriteInt( frame );
		payload.WriteInt( time );
		unsigned short type = 0;
		switch ( event.type ) {
			case openq4_retail::RETAIL_EVENT_SNAPSHOT:
				if ( event.data.Num() < headerBytes ) {
					lastError = "a decoded snapshot is shorter than its header";
					return finish( false );
				}
				if ( server ) {
					type = MVD_RECORD_SNAPSHOT;
					payload.WriteInt( ++snapshots );
					payload.Write( event.data.Ptr() + headerBytes, event.data.Num() - headerBytes );
				} else {
					// the client's frame, then the message with openQ4's command slack
					// (none reported) after the 1.4.2 header
					type = MVD_RECORD_CLIENT_SNAPSHOT;
					payload.WriteInt( frame );
					payload.Write( event.data.Ptr(), headerBytes );
					const byte noSlack[2] = { 0xff, 0x7f };
					payload.Write( noSlack, sizeof( noSlack ) );
					payload.Write( event.data.Ptr() + headerBytes, event.data.Num() - headerBytes );
					snapshots++;
				}
				break;
			case openq4_retail::RETAIL_EVENT_GAME_RELIABLE:
				if ( server ) {
					// a server demo's game reliables begin with their route
					if ( event.data.Num() < 3 ) {
						continue;
					}
					type = MVD_RECORD_RELIABLE;
					// 1.4.2 also routed each player's copy to the demo's own slot
					// (32), which the game shows only to a free camera; its reader
					// took any other client outside 0 to 31 as no client, as -1 is
					const int routeClient = static_cast<signed char>( event.data[1] );
					if ( routeClient < -1 || routeClient > MAX_ASYNC_CLIENTS ||
						 ( routeClient == MAX_ASYNC_CLIENTS && event.data[0] != DEMO_RECORD_CLIENTNUM ) ) {
						idList<byte> routed = event.data;
						routed[1] = static_cast<byte>( -1 );
						payload.Write( routed.Ptr(), routed.Num() );
						reliables++;
						break;
					}
				} else {
					type = MVD_RECORD_CLIENT_RELIABLE;
				}
				payload.Write( event.data.Ptr(), event.data.Num() );
				reliables++;
				break;
			case openq4_retail::RETAIL_EVENT_APPLY_SNAPSHOT:
				if ( server ) {
					continue;
				}
				type = MVD_RECORD_CLIENT_APPLY;
				payload.WriteInt( event.sequence );
				reliables++;
				break;
			case openq4_retail::RETAIL_EVENT_USERINFO:
				if ( event.client < 0 || event.client >= MAX_ASYNC_CLIENTS ) {
					continue;
				}
				type = MVD_RECORD_USERINFO;
				payload.WriteUnsignedChar( static_cast<unsigned char>( event.client ) );
				event.info.WriteToFileHandle( &payload );
				break;
			case openq4_retail::RETAIL_EVENT_USERCMD: {
				if ( server ) {
					continue;
				}
				type = MVD_RECORD_CLIENT_USERCMD;
				usercmd_t cmd;
				openq4_retail::ToUsercmd( event.cmd, cmd );
				cmd.gameTime = event.cmd.gameFrame * ticMsec;
				byte buffer[128];
				idBitMsg msg;
				msg.Init( buffer, sizeof( buffer ) );
				idAsyncNetwork::WriteUserCmdDelta( msg, cmd, NULL );
				payload.WriteInt( event.cmd.gameFrame );
				payload.Write( msg.GetData(), msg.GetSize() );
				break;
			}
			default:
				continue;
		}
		if ( !WriteRecord( type, 1, 0, payload.GetDataPtr(), payload.Length(), false ) ) {
			return finish( false );
		}
	}

	idFile_Memory end( "retail end" );
	end.WriteInt( Max( lastFrame, demo.endFrame ) );
	end.WriteInt( Max( lastTime, demo.endTime ) );
	end.WriteInt( snapshots );
	end.WriteInt( reliables );
	end.WriteInt( recordCount );
	if ( !WriteRecord( MVD_RECORD_END, 1, 0, end.GetDataPtr(), end.Length(), false ) ) {
		return finish( false );
	}
	return finish( true );
}

/*
==================
idMultiViewDemo::QueryRetailFileInfo

The library's summary of a retail network demo, from its container alone.
==================
*/
bool idMultiViewDemo::QueryRetailFileInfo( const char *name, mvdFileInfo_t &info ) {
	info = mvdFileInfo_t();
	info.retail = true;
	const idStr path = BuildPlaybackName( name );
	if ( path.IsEmpty() ) {
		info.error = "unsafe or empty demo path";
		return false;
	}
	idFile *source = fileSystem->OpenFileRead( path.c_str(), true );
	if ( source == NULL ) {
		info.error = "file not found";
		return false;
	}
	info.fileSize = source->Length();
	if ( info.fileSize <= 0 || info.fileSize > openq4_retail::NETDEMO_MAX_FILE_BYTES ) {
		fileSystem->CloseFile( source );
		info.error = "the file is empty or larger than any network demo openQ4 reads";
		return false;
	}
	idList<byte> bytes;
	bytes.SetNum( info.fileSize );
	const int read = source->Read( bytes.Ptr(), info.fileSize );
	fileSystem->CloseFile( source );
	if ( read != info.fileSize ) {
		info.error = "could not read the network demo";
		return false;
	}
	openq4_retail::retailNetDemo_t demo;
	demo.protocolMajor = 0;
	demo.protocolMinor = 0;
	if ( !openq4_retail::ParseContainer( bytes.Ptr(), info.fileSize, demo, info.error ) ) {
		// the header still says which release recorded it
		info.protocolMajor = static_cast<unsigned short>( Max( 0, demo.protocolMajor ) );
		info.protocolMinor = static_cast<unsigned short>( Max( 0, demo.protocolMinor ) );
		return false;
	}
	info.valid = true;
	info.formatMajor = static_cast<unsigned short>( demo.version );
	info.protocolMajor = static_cast<unsigned short>( demo.protocolMajor );
	info.protocolMinor = static_cast<unsigned short>( demo.protocolMinor );
	info.clientView = !demo.serverDemo;
	info.cleanEnd = true;
	info.durationMS = Max( 0, demo.packets.back().time - demo.packets.front().time );
	info.mapName = demo.serverInfo.GetString( "si_map" );
	info.gameType = demo.serverInfo.GetString( "si_gameType" );
	info.serverName = demo.serverInfo.GetString( "si_name" );
	if ( info.clientView ) {
		info.viewName = demo.userInfo[ demo.clientNum ].GetString( "ui_name" );
	}
	for ( int i = 0; i < demo.playerSlots.Num(); i++ ) {
		const char *player = demo.userInfo[ demo.playerSlots[i] ].GetString( "ui_name" );
		if ( player[0] != '\0' ) {
			if ( !info.players.IsEmpty() ) {
				info.players += ", ";
			}
			info.players += player;
		}
	}
	const int fps = demo.serverInfo.GetInt( "si_fps", "60" );
	idStr missing;
	if ( fps != common->GetUserCmdHz() ) {
		info.error = va( "recorded at %d game frames a second", fps );
	} else if ( openq4_retail::MissingPaks( demo, missing ) > 0 ) {
		info.error = va( "needs paks this installation does not have: %s", missing.c_str() );
	} else if ( !game->IsDemoProtocolCompatible( ASYNC_PROTOCOL_MINOR, demo.protocolMinor ) ) {
		info.error = "the game module does not read Quake 4 1.4.2 game data";
	} else {
		info.compatible = true;
	}
	return true;
}
