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

#include <time.h>

namespace {

static const byte MVD_MAGIC[8] = { 'O', 'Q', '4', 'M', 'V', 'D', 0x1a, '\n' };
static const unsigned int MVD_ENDIAN_MARKER = 0x12345678u;
static const unsigned int MVD_RECORD_SYNC = 0x5244564du; // "MVDR" on disk

static const unsigned short MVD_FORMAT_MAJOR = 1;
static const unsigned short MVD_FORMAT_MINOR = 2;
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
// navigation summary, not a set of independently decodable keyframes.
static const int MVD_INDEX_INTERVAL_MS = 1000;
static const int MVD_MAX_INDEX_ENTRIES = 100000;
static const int MVD_MAX_STREAM_RECORDS = 5000000;
static const int MVD_MAX_INITIALIZATION_RECORDS = 256;
static const int MVD_MAX_RECORDS_PER_FRAME = 4096;
static const int MVD_MAX_INSTANCES = 8;
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
	MVD_FEATURE_INSTANCE_ROUTING		= 1u << 5
};

static const unsigned int MVD_SUPPORTED_REQUIRED_FEATURES =
	MVD_FEATURE_RECORD_CRC |
	MVD_FEATURE_LENGTH_DELIMITED |
	MVD_FEATURE_SERVER_SNAPSHOTS |
	MVD_FEATURE_RELIABLE_ROUTING |
	MVD_FEATURE_FULL_WORLD_INSTANCES |
	MVD_FEATURE_INSTANCE_ROUTING;

static const unsigned int MVD_FORMAT_1_2_REQUIRED_FEATURES =
	MVD_FEATURE_FULL_WORLD_INSTANCES |
	MVD_FEATURE_INSTANCE_ROUTING;

enum mvdOptionalFeature_t {
	MVD_OPTIONAL_CLEAN_END			= 1u << 0,
	MVD_OPTIONAL_TIMELINE_INDEX		= 1u << 1
};

enum mvdRecordType_t {
	MVD_RECORD_METADATA				= 1,
	MVD_RECORD_MAP_STATE				= 2,
	MVD_RECORD_NETWORK_STATE			= 3,
	MVD_RECORD_RELIABLE				= 10,
	MVD_RECORD_SNAPSHOT				= 11,
	MVD_RECORD_INDEX					= 20,
	MVD_RECORD_END					= 250
};

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

idCVar mvd_enforceContent(
	"mvd_enforceContent", "1",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"refuse MVD playback when the declaration checksum differs" );

idCVar mvd_scale(
	"mvd_scale", "1",
	CVAR_SYSTEM | CVAR_FLOAT,
	"MVD playback speed",
	0.05f, 16.0f );

idCVar mvd_paused(
	"mvd_paused", "0",
	CVAR_SYSTEM | CVAR_BOOL,
	"pause MVD playback" );

idCVar mvd_seekBudgetMS(
	"mvd_seekBudgetMS", "12",
	CVAR_SYSTEM | CVAR_INTEGER,
	"maximum work per frame while replay-seeking an MVD",
	1, 100 );

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
	return fileSystem->ReadFile( path.c_str(), NULL, NULL ) >= 0;
}

static bool MVD_IsLiveRecord( unsigned short type ) {
	return type == MVD_RECORD_RELIABLE || type == MVD_RECORD_SNAPSHOT || type == MVD_RECORD_END;
}

static bool MVD_RecordSchemaSupported( unsigned short type, unsigned short version ) {
	switch ( type ) {
		case MVD_RECORD_RELIABLE:
		case MVD_RECORD_SNAPSHOT:
			return version == 1 || version == 2;
		case MVD_RECORD_METADATA:
		case MVD_RECORD_MAP_STATE:
		case MVD_RECORD_NETWORK_STATE:
		case MVD_RECORD_INDEX:
		case MVD_RECORD_END:
			return version == 1;
		default:
			return false;
	}
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

static bool MVD_ValidateReliablePayload( const idList<byte> &payload, unsigned short version, idStr &error ) {
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
	if ( routeType < DEMO_RECORD_CLIENTNUM || routeType > maxRouteType ||
		 routeClient < -1 || routeClient >= MAX_ASYNC_CLIENTS ) {
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
	fileSize = 0;
}

/*
==================
idMultiViewDemo::idMultiViewDemo
==================
*/
idMultiViewDemo::idMultiViewDemo() {
	memset( &recordingResult, 0, sizeof( recordingResult ) );
	recordingResultValid = false;
	Clear();
}

/*
==================
idMultiViewDemo::Clear
==================
*/
void idMultiViewDemo::Clear() {
	state = MVD_IDLE;
	file = NULL;
	fileName.Clear();
	tempFileName.Clear();
	lastError.Clear();
	memset( &header, 0, sizeof( header ) );

	recordingStartRealTime = 0;
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
	cmdSystem->AddCommand( "mvdInfo", Info_f, CMD_FL_SYSTEM, "validates and describes a multi-view demo" );
	cmdSystem->AddCommand( "mvdStatus", Status_f, CMD_FL_SYSTEM, "reports multi-view playback time and camera state" );
	cmdSystem->AddCommand( "mvdPause", Pause_f, CMD_FL_SYSTEM, "toggles MVD playback pause" );
	cmdSystem->AddCommand( "mvdSeek", Seek_f, CMD_FL_SYSTEM, "seeks to an MVD time in seconds" );
	cmdSystem->AddCommand( "mvdSkip", Skip_f, CMD_FL_SYSTEM, "moves relative to the current MVD time in seconds" );
	cmdSystem->AddCommand( "mvdSpeed", Speed_f, CMD_FL_SYSTEM, "sets MVD playback speed" );
	cmdSystem->AddCommand( "mvdStep", Step_f, CMD_FL_SYSTEM, "advances a paused MVD by simulation frames" );
	cmdSystem->AddCommand( "mvdFollowNext", FollowNext_f, CMD_FL_SYSTEM, "follows the next player in an MVD" );
	cmdSystem->AddCommand( "mvdFreeRoam", FreeRoam_f, CMD_FL_SYSTEM, "leaves player follow mode for free flight" );

	// Quake 4-compatible command names.  The file format remains .mvd rather
	// than pretending to be the packet-oriented legacy .netdemo format.
	cmdSystem->AddCommand( "recordNetDemo", Record_f, CMD_FL_SYSTEM, "records a server-side multi-view demo" );
	cmdSystem->AddCommand( "stopNetDemo", Stop_f, CMD_FL_SYSTEM, "stops MVD recording or playback" );
	cmdSystem->AddCommand( "playNetDemo", Play_f, CMD_FL_SYSTEM, "plays a multi-view demo" );
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

void idMultiViewDemo::FollowNext() {
	// Replay controls must work while paused and across Tourney arenas.
	// Select an eligible recorded POV directly; live spectator input has a
	// game-time debounce and mode-specific bindings that do not apply here.
	if ( state == MVD_PLAYING && game != NULL && game->SetDemoFollowClient( DEMO_FOLLOW_NEXT ) ) {
		preferredFollowClient = game->GetDemoFollowClient();
		forcePresentationFrame = true;
	}
}

void idMultiViewDemo::FreeRoam() {
	if ( state == MVD_PLAYING && game != NULL && game->SetDemoFollowClient( DEMO_FOLLOW_FREE ) ) {
		preferredFollowClient = DEMO_FOLLOW_FREE;
		forcePresentationFrame = true;
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
	if ( outHeader.formatMinor >= 2 &&
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
		case MVD_RECORD_INDEX:
		case MVD_RECORD_END:
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
	idDict serverInfo = sessLocal.mapSpawnData.serverInfo;
	// Demos are routinely shared.  Authentication secrets are never playback
	// state and must not become part of the recording.
	serverInfo.Delete( "si_password" );
	serverInfo.Delete( "password" );

	idFile_Memory payload( "MVD map state" );
	payload.WriteUnsignedInt( MAX_ASYNC_CLIENTS );
	serverInfo.WriteToFileHandle( &payload );
	sessLocal.mapSpawnData.syncedCVars.WriteToFileHandle( &payload );
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		sessLocal.mapSpawnData.userInfo[i].WriteToFileHandle( &payload );
	}
	return WriteRecord( MVD_RECORD_MAP_STATE, 1, MVD_RECORD_FLAG_REQUIRED, payload.GetDataPtr(), payload.Length() );
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

	idFile_Memory payload( "MVD map state", reinterpret_cast<const char *>( record.payload.Ptr() ), record.payload.Num() );
	unsigned int clientSlots = 0;
	if ( payload.ReadUnsignedInt( clientSlots ) != sizeof( clientSlots ) ||
		 clientSlots == 0 || clientSlots > MAX_ASYNC_CLIENTS ) {
		lastError = va( "invalid MVD map-state client count %u", clientSlots );
		return false;
	}

	if ( !MVD_ReadDict( &payload, sessLocal.mapSpawnData.serverInfo, lastError ) ||
		 !MVD_ReadDict( &payload, sessLocal.mapSpawnData.syncedCVars, lastError ) ) {
		return false;
	}
	if ( !MVD_ValidateSyncedCVars( sessLocal.mapSpawnData.syncedCVars, lastError ) ) {
		return false;
	}
	for ( unsigned int i = 0; i < clientSlots; i++ ) {
		if ( !MVD_ReadDict( &payload, sessLocal.mapSpawnData.userInfo[i], lastError ) ) {
			return false;
		}
	}
	for ( unsigned int i = clientSlots; i < MAX_ASYNC_CLIENTS; i++ ) {
		sessLocal.mapSpawnData.userInfo[i].Clear();
	}
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		sessLocal.mapSpawnData.persistentPlayerInfo[i].Clear();
	}
	memset( sessLocal.mapSpawnData.mapSpawnUsercmd, 0, sizeof( sessLocal.mapSpawnData.mapSpawnUsercmd ) );
	sessLocal.numClients = 0;

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
	if ( record.version != 1 || record.payload.Num() < 4 ) {
		error = "short or unsupported MVD network-state record";
		return false;
	}

	const byte *bytes = record.payload.Ptr();
	const int length = record.payload.Num();
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
			idBitMsg startState;
			startState.Init( bytes + cursor, messageBytes );
			startState.SetSize( messageBytes );
			startState.BeginReading();
			if ( startState.GetRemainingReadBits() < 48 ) {
				error = "short MVD multiplayer start state";
				return false;
			}
			startState.ReadLong();
			int previousStartClient = -1;
			int startClientCount = 0;
			bool terminated = false;
			for ( int i = 0; i <= MAX_ASYNC_CLIENTS; i++ ) {
				if ( startState.GetRemainingReadBits() < 16 ) {
					break;
				}
				const int clientNum = startState.ReadShort();
				if ( clientNum == MAX_ASYNC_CLIENTS ) {
					terminated = true;
					break;
				}
				if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ||
					 !seenClient[clientNum] || clientNum <= previousStartClient ||
					 startState.GetRemainingReadBits() < 21 ) {
					error = va( "invalid MVD multiplayer start-state client %d", clientNum );
					return false;
				}
				startState.ReadShort();
				const int instance = startState.ReadBits( 4 );
				if ( instance < 0 || instance >= 8 ) {
					error = va( "invalid MVD multiplayer instance %d", instance );
					return false;
				}
				startState.ReadBits( 1 );
				previousStartClient = clientNum;
				startClientCount++;
			}
			if ( !terminated || startClientCount != playerCount ||
				 startState.GetRemainingReadBits() >= 8 ) {
				error = "unterminated or oversized MVD multiplayer start state";
				return false;
			}
		} else if ( messageBytes < 16 ) {
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
	if ( !idAsyncNetwork::server.IsActive() || !sessLocal.IsMapSpawned() || game == NULL || !game->IsMultiplayer() ) {
		SetRecordingResult( SERVER_MVD_RESULT_FAILED,
			SERVER_MVD_REASON_START_REJECTED, NULL, NULL );
		common->Printf( "recordMVD requires an active multiplayer server map\n" );
		return false;
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
	header.formatMajor = MVD_FORMAT_MAJOR;
	header.formatMinor = MVD_FORMAT_MINOR;
	header.requiredFeatures = MVD_SUPPORTED_REQUIRED_FEATURES;
	header.optionalFeatures = MVD_OPTIONAL_CLEAN_END | MVD_OPTIONAL_TIMELINE_INDEX;
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
	header.startGameFrame = idAsyncNetwork::server.GetGameFrame();
	header.startGameTime = idAsyncNetwork::server.GetGameTime();
	header.snapshotDelay = idMath::ClampInt( 16, 1000, mvd_snapshotDelay.GetInteger() );
	header.contentChecksum = declManager->GetChecksum();
	header.indexOffset = 0;
	recordingStartRealTime = Sys_Milliseconds();
	lastSnapshotGameFrame = header.startGameFrame;
	lastSnapshotGameTime = header.startGameTime;

	game->SetDemoState( DEMO_RECORDING, true, false );
	if ( !WriteHeader() || !WriteMetadataRecord() || !WriteMapStateRecord() || !WriteNetworkStateRecord() ) {
		common->Warning( "Could not start MVD recording: %s", lastError.c_str() );
		StopRecording( "initialization failed", false,
			SERVER_MVD_REASON_INITIALIZATION_FAILED );
		return false;
	}

	common->Printf( "Recording multi-view demo to '%s'\n", fileName.c_str() );
	return true;
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
		clean = WriteIndexRecord();
		idFile_Memory payload( "MVD end" );
		payload.WriteInt( idAsyncNetwork::server.GetGameFrame() );
		payload.WriteInt( idAsyncNetwork::server.GetGameTime() );
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

	game->SetDemoState( DEMO_NONE, false, false );
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

	const int maxMinutes = mvd_maxDurationMinutes.GetInteger();
	if ( maxMinutes > 0 && Sys_Milliseconds() - recordingStartRealTime >= maxMinutes * 60 * 1000 ) {
		StopRecording( "duration limit reached", true );
		return;
	}
	if ( snapshotSequence > 0 && gameTime - lastSnapshotGameTime < static_cast<int>( header.snapshotDelay ) ) {
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

		if ( record.type == MVD_RECORD_RELIABLE ) {
			if ( !MVD_ValidateReliablePayload( record.payload, record.version, lastError ) ) {
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
	file = fileSystem->OpenFileRead( fileName.c_str(), true );
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
	if ( !ValidateNetworkStateRecord( networkState, lastError ) ) {
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

	cvarSystem->SetCVarsFromDictByFlags( sessLocal.mapSpawnData.syncedCVars, CVAR_NETWORKSYNC );
	sessLocal.ExecuteMapChange();
	game->SetDemoState( DEMO_PLAYING, true, false );
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		game->SetUserInfo( i, sessLocal.mapSpawnData.userInfo[i], true );
	}

	idFile_Memory initialState( "MVD network state",
		reinterpret_cast<const char *>( networkState.payload.Ptr() ), networkState.payload.Num() );
	game->ReadNetworkInfo( header.startGameTime, &initialState, MAX_ASYNC_CLIENTS );
	if ( initialState.Tell() != initialState.Length() ) {
		const idStr failure = va( "MVD initial network state left %d unread bytes",
			initialState.Length() - initialState.Tell() );
		lastError = failure;
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
	mvd_scale.SetFloat( 1.0f );
	mvd_paused.SetBool( false );
	if ( sessLocal.sw != NULL ) {
		sessLocal.sw->SetSlowmoSpeed( 1.0f );
		if ( sessLocal.sw->IsPaused() ) {
			sessLocal.sw->UnPause();
		}
	}
	common->Printf( "Playing multi-view demo '%s' (format %u.%u, protocol %u.%u)\n",
		fileName.c_str(), header.formatMajor, header.formatMinor, header.protocolMajor, header.protocolMinor );
	return true;
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
	const int restoreFollowClient = preferredFollowClient >= 0 ?
		preferredFollowClient : game->GetDemoFollowClient();
	resettingPlayback = true;
	game->SetDemoState( DEMO_NONE, false, false );
	session->Stop();

	if ( !ReadMapStateRecord( playbackMapState ) ) {
		resettingPlayback = false;
		return false;
	}

	cvarSystem->SetCVarsFromDictByFlags( sessLocal.mapSpawnData.syncedCVars, CVAR_NETWORKSYNC );
	sessLocal.ExecuteMapChange();
	game->SetDemoState( DEMO_PLAYING, true, false );
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		game->SetUserInfo( i, sessLocal.mapSpawnData.userInfo[i], true );
	}

	idFile_Memory initialState( "MVD network state",
		reinterpret_cast<const char *>( playbackNetworkState.payload.Ptr() ),
		playbackNetworkState.payload.Num() );
	game->ReadNetworkInfo( header.startGameTime, &initialState, MAX_ASYNC_CLIENTS );
	if ( initialState.Tell() != initialState.Length() ) {
		lastError = va( "MVD reset network state left %d unread bytes",
			initialState.Length() - initialState.Tell() );
		resettingPlayback = false;
		return false;
	}
	if ( !game->SetDemoFollowClient( restoreFollowClient ) ) {
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
	if ( target < static_cast<int>( playbackGameTime ) ) {
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

void idMultiViewDemo::StepFrames( int frames ) {
	if ( state != MVD_PLAYING ) {
		return;
	}
	SetPaused( true );
	const int step = Max( 1, frames ) * Max( 1, common->GetUserCmdMSec() );
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
	StopPlayback( reason );
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

	if ( record.type == MVD_RECORD_RELIABLE ) {
		if ( !MVD_ValidateReliablePayload( record.payload, record.version, lastError ) ) {
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

	// Metadata and unknown optional records are safe to ignore during playback.
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
	const int currentFollowClient = game->GetDemoFollowClient();
	if ( currentFollowClient >= 0 || preferredFollowClient < 0 ) {
		preferredFollowClient = currentFollowClient;
	}

	if ( seekInProgress ) {
		ProcessSeekBudget();
		if ( state != MVD_PLAYING || seekInProgress ) {
			return;
		}
	}

	const int now = Sys_Milliseconds();
	const int elapsed = idMath::ClampInt( 0, 100, now - playbackLastRealTime );
	playbackLastRealTime = now;
	if ( !mvd_paused.GetBool() ) {
		playbackGameTime += elapsed * GetPlaybackScale();
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
	if ( haveSnapshot ) {
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
	return true;
}

/*
==================
idMultiViewDemo::QueryFileInfo
==================
*/
bool idMultiViewDemo::QueryFileInfo( const char *name, mvdFileInfo_t &info ) {
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
				haveNetworkState = ValidateNetworkStateRecord( record, lastError );
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
				 !ValidateNetworkStateRecord( record, lastError ) ) {
				if ( lastError.IsEmpty() ) {
					lastError = "duplicate MVD network-state record";
				}
				valid = false;
				break;
			}
			haveNetworkState = true;
		} else if ( record.type == MVD_RECORD_SNAPSHOT ) {
			liveStreamStarted = true;
			if ( !haveMapState || !haveNetworkState ||
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
			if ( !haveMapState || !haveNetworkState ||
				 !MVD_ValidateReliablePayload( record.payload, record.version, lastError ) ) {
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
		if ( record.type == MVD_RECORD_SNAPSHOT ) {
			snapshots++;
		} else if ( record.type == MVD_RECORD_RELIABLE ) {
			reliables++;
		} else if ( record.type == MVD_RECORD_END ) {
			cleanEnd = true;
		} else if ( record.type != MVD_RECORD_METADATA &&
					record.type != MVD_RECORD_MAP_STATE &&
					record.type != MVD_RECORD_NETWORK_STATE &&
					record.type != MVD_RECORD_INDEX ) {
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
		"  duration: %.1f seconds, clean end: %s, optional unknown records: %d\n",
		path.c_str(),
		inspectedHeader.formatMajor, inspectedHeader.formatMinor,
		inspectedHeader.protocolMajor, inspectedHeader.protocolMinor,
		inspectedHeader.usercmdHz,
		size.c_str(), records, snapshots, reliables,
		Max( 0, lastGameTime - inspectedHeader.startGameTime ) * 0.001f,
		cleanEnd ? "yes" : "no", optionalUnknown );
	return true;
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

void idMultiViewDemo::Info_f( const idCmdArgs &args ) {
	idAsyncNetwork::multiViewDemo.Inspect( args );
}

void idMultiViewDemo::Status_f( const idCmdArgs &args ) {
	const idMultiViewDemo &demo = idAsyncNetwork::multiViewDemo;
	common->Printf( "MVD_STATUS playing=%d paused=%d seeking=%d time=%d duration=%d follow=%d\n",
		demo.IsPlaying(), demo.IsPaused(), demo.IsSeeking(), demo.GetPlaybackTimeMS(),
		demo.GetPlaybackDurationMS(), demo.GetFollowClient() );
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

void idMultiViewDemo::FreeRoam_f( const idCmdArgs &args ) {
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		common->Printf( "No MVD is playing\n" );
		return;
	}
	idAsyncNetwork::multiViewDemo.FreeRoam();
}
