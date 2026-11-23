/*
===========================================================================

openQ4 demo library and format-neutral playback controls.

===========================================================================
*/

#include "Session_local.h"
#include "DemoFile.h"
#include "NativeInputPublications.h"
#include "async/AsyncNetwork.h"
#include "async/RetailNetDemo.h"
#include "../ui/ListGUILocal.h"

namespace {

static const char *DEMO_FILTER_ALL = "all";
static const char *DEMO_FILTER_MVD = "mvd";
static const char *DEMO_FILTER_RENDER = "render";
static const char *DEMO_FILTER_LEGACY = "legacy";
static const char *DEMO_FILTER_INCOMPLETE = "incomplete";

// Not a cheat: MVD playback restores a cheat-free server's cvars.
static idCVar demo_uiSeek(
	"demo_uiSeek", "0",
	CVAR_SYSTEM | CVAR_GUI | CVAR_FLOAT | CVAR_NOCHEAT,
	"normalized preview position used by the demo playback timeline",
	0.0f, 1.0f );

static idCVar demo_renderSeekBudgetMS(
	"demo_renderSeekBudgetMS", "10",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"cooperative render-demo seek budget checked between stream commands; indivisible map/renderer work may exceed it",
	1, 50 );

// Recording ends itself rather than fill the disk the campaign saves to, or
// grow without bound when left running.
static idCVar demo_recordMinFreeMB(
	"demo_recordMinFreeMB", "256",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT,
	"a demo recording stops cleanly when the save path's disk has less than this many megabytes free; 0 never stops for it",
	0, 1048576 );

static idCVar demo_recordMaxSizeMB(
	"demo_recordMaxSizeMB", "0",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT,
	"a demo recording stops cleanly when its file reaches this many megabytes; 0 sets no limit",
	0, 1048576 );

static idCVar demo_recordKeyframeSeconds(
	"demo_recordKeyframeSeconds", "10",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT,
	"seconds of recording between keyframes, the points inside a level a seek can start from and playback can resume at after damage; 0 records none",
	0, 600 );

static idCVar demo_useKeyframes(
	"demo_useKeyframes", "1",
	CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT,
	"a seek in a render demo starts from the keyframe before its target; 0 replays from the level's start, for troubleshooting" );

static const char *DemoLocalized( const char *key ) {
	return common->GetLanguageDict()->GetString( key );
}

static bool DemoPathEndsWith( const idStr &path, const char *suffix ) {
	const int suffixLength = static_cast<int>( strlen( suffix ) );
	return path.Length() >= suffixLength &&
		idStr::Icmp( path.c_str() + path.Length() - suffixLength, suffix ) == 0;
}

static bool DemoPathIsSafe( const idStr &path ) {
	return path.Length() > 6 &&
		idStr::Icmpn( path.c_str(), "demos/", 6 ) == 0 &&
		path.Find( ".." ) < 0 &&
		path.Find( ":" ) < 0 &&
		path.Find( "\\" ) < 0 &&
		path[0] != '/' &&
		path[0] != '\\';
}

// Text lengths here count characters, not bytes. Demo names and the localized
// labels are UTF-8, and cutting inside a multi-byte sequence leaves a dangling
// lead byte that draws as a stray glyph ("Grabaci?..."), so every cut lands on
// a character boundary.
static int DemoCharacterBytes( const idStr &text, int offset ) {
	unsigned int codePoint = 0;
	return Max( 1, LangDict_NextCodePoint( text.c_str() + offset, text.Length() - offset, codePoint ) );
}

static int DemoCharacterCount( const idStr &text ) {
	int count = 0;
	for ( int i = 0; i < text.Length(); i += DemoCharacterBytes( text, i ) ) {
		count++;
	}
	return count;
}

static void DemoCapCharacters( idStr &text, int maxCharacters ) {
	int offset = 0;
	for ( int count = 0; offset < text.Length() && count < maxCharacters; count++ ) {
		offset += DemoCharacterBytes( text, offset );
	}
	text.CapLength( Min( offset, text.Length() ) );
}

static idStr DemoSanitizeText( const char *text, int maxLength = 96 ) {
	idStr source = text ? text : "";
	source.RemoveColors();

	idStr result;
	int characters = 0;
	for ( int i = 0; i < source.Length() && characters < maxLength; ) {
		const unsigned char ch = static_cast<unsigned char>( source[i] );
		const int bytes = DemoCharacterBytes( source, i );
		if ( ch == '\t' || ch == '\r' || ch == '\n' ) {
			result.Append( ' ' );
			characters++;
		} else if ( ch >= 32 && ch != 127 ) {
			for ( int k = 0; k < bytes; k++ ) {
				result.Append( source[i + k] );
			}
			characters++;
		}
		i += bytes;
	}
	while ( result.Find( "  " ) >= 0 ) {
		result.Replace( "  ", " " );
	}
	result.StripLeading( ' ' );
	result.StripTrailing( ' ' );
	return result;
}

// Browser rows deliberately prefer a stable column edge over displaying a
// filename that runs into the next field.  The full selected name remains
// available in the detail card beneath the list.
static idStr DemoEllipsizeText( const char *text, int maxLength ) {
	idStr result = DemoSanitizeText( text, maxLength + 1 );
	if ( DemoCharacterCount( result ) <= maxLength ) {
		return result;
	}
	if ( maxLength <= 3 ) {
		DemoCapCharacters( result, maxLength );
		return result;
	}
	DemoCapCharacters( result, maxLength - 3 );
	result += "...";
	return result;
}

static const char *DemoMVDProblemString( const idStr &error ) {
	if ( error.Find( "network protocol" ) >= 0 ) {
		return "#str_41561";
	}
	if ( error.Find( "game API" ) >= 0 ) {
		return "#str_41562";
	}
	if ( error.Find( "simulation rate" ) >= 0 ) {
		return "#str_41563";
	}
	if ( error.Find( "content checksum" ) >= 0 ) {
		return "#str_41564";
	}
	if ( error.Find( "unsupported" ) >= 0 ||
		 error.Find( "incompatible" ) >= 0 ) {
		return "#str_41553";
	}
	return "#str_41565";
}

// openQ4 MP-22: why a retail Quake 4 network demo cannot play
static const char *DemoRetailProblemString( const idStr &error ) {
	if ( error.Find( "another Quake 4 release" ) >= 0 ) {
		return "#str_232881";
	}
	if ( error.Find( "paks" ) >= 0 ) {
		return "#str_232882";
	}
	if ( error.Find( "game frames a second" ) >= 0 ) {
		return "#str_41563";
	}
	if ( error.Find( "game module" ) >= 0 ) {
		return "#str_41561";
	}
	return "#str_41565";
}

static idStr DemoFormatTime( int milliseconds, bool unknownAllowed = true ) {
	if ( milliseconds < 0 ) {
		return unknownAllowed ? "--:--" : "00:00";
	}
	const int totalSeconds = milliseconds / 1000;
	const int hours = totalSeconds / 3600;
	const int minutes = ( totalSeconds / 60 ) % 60;
	const int seconds = totalSeconds % 60;
	if ( hours > 0 ) {
		return va( "%d:%02d:%02d", hours, minutes, seconds );
	}
	return va( "%02d:%02d", minutes, seconds );
}

static int DemoCurrentRenderTimeMS( const idSessionLocal &session ) {
	return session.RenderDemoCurrentTimeMS();
}

// A campaign render demo (v11) as the library and the content check see it,
// read from its first records and its summary trailer without decoding frames.
struct demoRenderSessionInfo_t {
	int				streamVersion = 0;
	bool			session = false;	// a v11+ stream with its header
	idDict			header;
	idStr			firstMap;
	idStr			firstEntityFilter;
	bool			summarized = false;	// a clean stop wrote the trailer
	idDict			summary;
	// v14: the length its block headers give when it has no trailer
	bool			timeline = false;
	int				timelineDurationMS = -1;
};

/*
================
DemoReadRetailRenderMap

The level a retail Quake 4 render demo opens with. Retail starts every
recording with its version, the sound world's state and then the map load;
the state is read through (its savegame layout) to reach the map's name.
================
*/
static bool DemoReadRetailRenderMap( idDemoFile &probe, idStr &mapName ) {
	mapName.Clear();
	unsigned char token = 0, command = 0;
	int value = 0;
	auto skip = [&]( int bytes ) -> bool {
		byte scratch[128];
		while ( bytes > 0 ) {
			const int step = Min( bytes, static_cast<int>( sizeof( scratch ) ) );
			if ( probe.Read( scratch, step ) != step ) {
				return false;
			}
			bytes -= step;
		}
		return true;
	};
	if ( probe.Read( &token, 1 ) != 1 || token != DS_VERSION || probe.ReadInt( value ) != sizeof( value ) ||
		 probe.Read( &token, 1 ) != 1 || token != DS_SOUND || probe.Read( &command, 1 ) != 1 || command != 0 ) {
		return false;
	}
	// listener origin, axis, id, time, sound clocks
	int emitters = 0;
	if ( !skip( 12 + 36 + 4 * 4 ) || probe.ReadInt( emitters ) != sizeof( emitters ) || emitters < 0 || emitters > 8192 ) {
		return false;
	}
	for ( int i = 1; i < emitters; i++ ) {
		int handle = 0;
		if ( probe.ReadInt( handle ) != sizeof( handle ) ) {
			return false;
		}
		if ( handle < 0 ) {
			continue;
		}
		// origin, listener, parms, amplitude, eight channel slots, distances, flags
		if ( !skip( 12 + 4 + 40 + 4 + 4 + 8 * 125 + 4 + 1 + 4 + 4 + 1 + 4 + 4 + 12 ) ) {
			return false;
		}
		for ( int listed = 0; ; listed++ ) {
			int slot = -1;
			if ( probe.ReadInt( slot ) != sizeof( slot ) || listed > 8 ) {
				return false;
			}
			if ( slot < 0 ) {
				break;
			}
			for ( int name = 0; name < 2; name++ ) {
				int length = 0;
				if ( probe.ReadInt( length ) != sizeof( length ) || length < 0 || length >= MAX_STRING_CHARS || !skip( length ) ) {
					return false;
				}
			}
		}
	}
	char name[256];
	if ( probe.Read( &token, 1 ) != 1 || token != DS_RENDER || probe.Read( &command, 1 ) != 1 || command != 13 ||
		 !skip( 4 * 4 ) || probe.Read( name, sizeof( name ) ) != static_cast<int>( sizeof( name ) ) ) {
		return false;
	}
	name[ sizeof( name ) - 1 ] = '\0';
	mapName = name;
	return true;
}

static bool DemoReadRenderSessionInfo( const char *path, demoRenderSessionInfo_t &info ) {
	idDemoFile probe;
	if ( !probe.OpenForReading( path, false, true ) ) {
		return false;
	}
	if ( probe.IsRetailRenderStream() ) {
		// no openQ4 header: nothing of a campaign to check
		probe.Close();
		return true;
	}
	int token = DS_FINISHED;
	if ( probe.ReadInt( token ) != sizeof( token ) || token != DS_VERSION ||
		 probe.ReadInt( info.streamVersion ) != sizeof( info.streamVersion ) ) {
		probe.Close();
		return true;
	}
	int record = 0;
	if ( info.streamVersion >= OPENQ4_RENDERDEMO_SESSION_VERSION &&
		 probe.ReadInt( token ) == sizeof( token ) && token == DS_SESSION &&
		 probe.ReadInt( record ) == sizeof( record ) && record == DSC_HEADER ) {
		probe.ReadDict( info.header );
		info.session = probe.IsOpen();
		// The first segment follows at once: nothing is written while armed.
		idDict segment;
		if ( info.session &&
			 probe.ReadInt( token ) == sizeof( token ) && token == DS_SESSION &&
			 probe.ReadInt( record ) == sizeof( record ) && record == DSC_SEGMENT ) {
			if ( info.streamVersion >= OPENQ4_RENDERDEMO_BLOCK_VERSION ) {
				// v14: the segment's string table begins with its record
				probe.ResetHashStrings();
			}
			probe.ReadDict( segment );
			if ( probe.IsOpen() ) {
				info.firstMap = segment.GetString( "map" );
				info.firstEntityFilter = segment.GetString( "entityFilter" );
			}
		}
	}
	probe.Close();
	if ( info.session ) {
		info.summarized = idDemoFile::ReadTrailer( path, info.summary ) &&
			info.summary.GetInt( "version" ) >= OPENQ4_RENDERDEMO_SESSION_VERSION;
		// A recording that was cut short has no trailer; its blocks still say
		// how long it is and how many levels it reached.
		demoTimeline_t timeline;
		if ( !info.summarized && idDemoFile::ReadTimeline( path, timeline ) && timeline.timed && timeline.frames > 0 ) {
			info.timeline = true;
			info.timelineDurationMS = static_cast<int>( Min( timeline.durationUS / 1000, static_cast<int64>( idMath::INT_MAX ) ) );
		}
	}
	return true;
}

// The campaign a game directory holds, as players know it.
static idStr DemoCampaignName( const char *gameDir ) {
	if ( !idStr::Icmp( gameDir, "q4xbase" ) ) {
		return DemoLocalized( "#str_41585" );
	}
	if ( gameDir[0] == '\0' || !idStr::Icmp( gameDir, OPENQ4_GAMEDIR ) ) {
		return DemoLocalized( "#str_41584" );
	}
	return DemoSanitizeText( gameDir, 32 );
}

// Whether a recording's levels and assets are the ones mounted now: its game
// directory and, since a mod can play over either campaign from one directory,
// its campaign. Streams that name neither make no claim.
static bool DemoRecordedContentMatches( const idDict &header ) {
	const char *recordedGame = header.GetString( "game", "" );
	if ( recordedGame[0] != '\0' && idStr::Icmp( recordedGame, fileSystem->GetActiveGameDir() ) ) {
		return false;
	}
	const char *recordedCampaign = header.GetString( "campaign", "" );
	return recordedCampaign[0] == '\0' || !idStr::Icmp( recordedCampaign, FS_AwakeningActive() ? "awakening" : "quake4" );
}

// The campaign a recording holds, as players know it: its game directory's,
// or, when that is the mod running now, the campaign the mod played over.
static idStr DemoRecordedCampaignName( const idDict &header ) {
	const char *recordedGame = header.GetString( "game", "" );
	if ( recordedGame[0] != '\0' && !idStr::Icmp( recordedGame, fileSystem->GetActiveGameDir() ) ) {
		return DemoCampaignName( !idStr::Icmp( header.GetString( "campaign", "" ), "awakening" ) ? "q4xbase" : OPENQ4_GAMEDIR );
	}
	return DemoCampaignName( recordedGame );
}

// "startFrame reason map entityFilter [startMS]", as StopRecordingRenderDemo
// writes it; v13 adds the start time, -1 when absent.
static bool DemoParseSegmentSummary( const char *text, idSessionLocal::renderDemoSegment_t &segment ) {
	idStrList fields;
	idStr field;
	for ( const char *c = text; ; c++ ) {
		if ( *c == ' ' || *c == '\0' ) {
			if ( !field.IsEmpty() ) {
				fields.Append( field );
				field.Clear();
			}
			if ( *c == '\0' ) {
				break;
			}
		} else {
			field.Append( *c );
		}
	}
	if ( fields.Num() < 3 || !fields[0].IsNumeric() ) {
		return false;
	}
	segment.startFrame = atoi( fields[0].c_str() );
	segment.reason = fields[1];
	segment.map = fields[2];
	segment.entityFilter.Clear();
	if ( fields.Num() > 3 && fields[3] != "-" ) {
		segment.entityFilter = fields[3];
	}
	segment.startMS = fields.Num() > 4 && fields[4].IsNumeric() ? atoi( fields[4].c_str() ) : -1;
	return segment.startFrame >= 0;
}

// A render demo's length, from its summary trailer: v13 records it, earlier
// recordings are two tics a frame.
static int DemoSummaryDurationMS( const idDict &summary ) {
	const int frames = summary.GetInt( "frames" );
	if ( frames <= 0 ) {
		return -1;
	}
	if ( summary.GetInt( "version" ) >= OPENQ4_RENDERDEMO_FRAME_TIME_VERSION ) {
		return summary.GetInt( "durationMS", "-1" );
	}
	return Session_RenderDemoFramesToMS( frames - 1, summary.GetInt( "userCmdHz" ) );
}

// The high-resolution clock in microseconds: the recording stamps its frames
// with it and playback runs its clock from it.
static double DemoClockUS() {
	return Sys_GetClockTicks() * 1000000.0 / Sys_ClockTicksPerSecond();
}

static bool DemoEntryMatchesFilter( const demoLibraryEntry_t &entry, const idStr &filter ) {
	if ( filter.IsEmpty() || !filter.Icmp( DEMO_FILTER_ALL ) ) {
		return true;
	}
	if ( !filter.Icmp( DEMO_FILTER_MVD ) ) {
		return entry.type == DEMO_LIBRARY_MVD;
	}
	if ( !filter.Icmp( DEMO_FILTER_RENDER ) ) {
		return entry.type == DEMO_LIBRARY_RENDER;
	}
	if ( !filter.Icmp( DEMO_FILTER_LEGACY ) ) {
		return entry.type == DEMO_LIBRARY_COMMAND ||
			entry.type == DEMO_LIBRARY_LEGACY_NET ||
			entry.type == DEMO_LIBRARY_LEGACY_RENDER;
	}
	if ( !filter.Icmp( DEMO_FILTER_INCOMPLETE ) ) {
		return entry.type == DEMO_LIBRARY_INCOMPLETE;
	}
	return true;
}

static bool DemoEntryCanDelete( const idStr &path ) {
	if ( !DemoPathIsSafe( path ) ) {
		return false;
	}
	const idStr savePath = fileSystem->RelativePathToOSPath( path.c_str(), "fs_savepath" );
	idFile *explicitFile = fileSystem->OpenExplicitFileRead( savePath.c_str() );
	if ( explicitFile == NULL ) {
		return false;
	}
	fileSystem->CloseFile( explicitFile );
	return true;
}

static void DemoAddUniquePath( idStrList &paths, const idStr &path ) {
	idStr normalized = path;
	normalized.BackSlashesToSlashes();
	if ( idStr::Icmpn( normalized.c_str(), "demos/", 6 ) != 0 ) {
		normalized = "demos/" + normalized;
	}
	if ( !DemoPathIsSafe( normalized ) ) {
		return;
	}
	for ( int i = 0; i < paths.Num(); i++ ) {
		if ( !paths[i].Icmp( normalized ) ) {
			return;
		}
	}
	paths.Append( normalized );
}

static void DemoCollectExtension( idStrList &paths, const char *extension ) {
	idFileList *files = fileSystem->ListFiles( "demos", extension, true, true );
	if ( files == NULL ) {
		return;
	}
	for ( int i = 0; i < files->GetNumFiles(); i++ ) {
		DemoAddUniquePath( paths, files->GetFile( i ) );
	}
	fileSystem->FreeFileList( files );
}

static bool DemoReadPrefix( const idStr &path, byte *prefix, int prefixSize, int &bytesRead ) {
	bytesRead = 0;
	memset( prefix, 0, prefixSize );
	idFile *file = fileSystem->OpenFileRead( path.c_str(), false );
	if ( file == NULL ) {
		return false;
	}
	bytesRead = file->Read( prefix, prefixSize );
	fileSystem->CloseFile( file );
	return bytesRead > 0;
}

static void DemoProbeEntry( demoLibraryEntry_t &entry ) {
	entry.displayName = entry.path;
	entry.displayName.StripPath();
	if ( DemoPathEndsWith( entry.displayName, ".mvd.part" ) ) {
		entry.displayName.StripFileExtension();
		entry.displayName.StripFileExtension();
	} else {
		entry.displayName.StripFileExtension();
	}
	entry.displayName = DemoSanitizeText( entry.displayName );
	if ( entry.displayName.IsEmpty() ) {
		entry.displayName = DemoLocalized( "#str_41500" );
	}

	// a recording can pass the 2 GiB an int counts to
	int64 fileBytes = 0;
	entry.sizeBytes = idDemoFile::FileInfo( entry.path.c_str(), fileBytes, entry.timestamp )
		? static_cast<int>( Min( fileBytes, static_cast<int64>( idMath::INT_MAX ) ) ) : -1;
	if ( entry.sizeBytes < 0 ) {
		entry.status = DemoLocalized( "#str_41548" );
		entry.details = DemoLocalized( "#str_41548" );
		return;
	}

	if ( DemoPathEndsWith( entry.path, ".mvd.part" ) ||
		 DemoPathEndsWith( entry.path, ".ucmd" ) ||
		 DemoPathEndsWith( entry.path, ".part" ) ) {
		entry.type = DEMO_LIBRARY_INCOMPLETE;
		entry.format = DemoLocalized( "#str_41545" );
		entry.status = DemoLocalized( "#str_41545" );
		entry.details = DemoLocalized( "#str_41545" );
		entry.compatible = false;
	} else if ( DemoPathEndsWith( entry.path, ".mvd" ) ) {
		entry.type = DEMO_LIBRARY_MVD;
		entry.format = DemoLocalized( "#str_41540" );
		mvdFileInfo_t info;
		if ( idAsyncNetwork::multiViewDemo.QueryFileInfo( entry.path.c_str(), info ) ) {
			entry.playable = info.compatible;
			entry.compatible = info.compatible;
			entry.cleanEnd = info.cleanEnd;
			entry.durationMS = info.durationMS;
			entry.mapName = DemoSanitizeText( info.mapName );
			entry.gameType = DemoSanitizeText( info.gameType );
			entry.status = DemoLocalized( info.compatible ? "#str_41546" : "#str_41547" );
			entry.details = DemoLocalized(
				info.compatible ? "#str_41550" : DemoMVDProblemString( info.error ) );
			entry.format = va( "OQ4MVD %u.%u / %u.%u",
				info.formatMajor, info.formatMinor,
				info.protocolMajor, info.protocolMinor );
			// openQ4 MP-22: whose view or who played, and what demoFind searches
			entry.serverName = DemoSanitizeText( info.serverName );
			entry.players = DemoSanitizeText( info.players );
			entry.recorder = DemoSanitizeText( info.viewName );
			entry.clientView = info.clientView;
			if ( info.compatible && info.clientView && !entry.recorder.IsEmpty() ) {
				entry.details = va( DemoLocalized( "#str_232844" ), entry.recorder.c_str() );
			} else if ( info.compatible && !entry.players.IsEmpty() ) {
				entry.details = va( DemoLocalized( "#str_232843" ), entry.players.c_str() );
			}
			if ( info.compatible ) {
				entry.capabilities =
					DEMO_CAP_PLAY | DEMO_CAP_PAUSE | DEMO_CAP_RATE |
					DEMO_CAP_SEEK | DEMO_CAP_STEP | DEMO_CAP_FREE_ROAM |
					DEMO_CAP_FOLLOW | DEMO_CAP_FULL_WORLD;
				// openQ4 MP-22: a player's recording of their own view has no other camera
				if ( info.clientView ) {
					entry.capabilities &= ~( DEMO_CAP_FREE_ROAM | DEMO_CAP_FOLLOW | DEMO_CAP_FULL_WORLD );
				}
			}
		} else {
			entry.status = DemoLocalized( "#str_41548" );
			entry.details = DemoLocalized( DemoMVDProblemString( info.error ) );
			common->DPrintf( "Demo library rejected '%s': %s\n",
				entry.path.c_str(), info.error.c_str() );
		}
	} else if ( DemoPathEndsWith( entry.path, ".demo" ) ) {
		byte prefix[32];
		int prefixBytes = 0;
		const bool havePrefix = DemoReadPrefix( entry.path, prefix, sizeof( prefix ), prefixBytes );
		static const char AMBIGUOUS_MAGIC[] = "Quake4 RDEMO";
		const bool ambiguousWrapper = havePrefix && prefixBytes >= static_cast<int>( sizeof( AMBIGUOUS_MAGIC ) ) &&
			memcmp( prefix, AMBIGUOUS_MAGIC, sizeof( AMBIGUOUS_MAGIC ) ) == 0;

		bool decodedOpenQ4Stream = false;
		bool supportedOpenQ4Stream = false;
		bool retailStream = false;
		idStr retailMap;
		int streamVersion = 1;
		idDemoFile probe;
		if ( probe.OpenForReading( entry.path.c_str(), false, true ) && probe.IsRetailRenderStream() ) {
			retailStream = true;
			DemoReadRetailRenderMap( probe, retailMap );
		} else if ( probe.IsOpen() ) {
			int firstToken = DS_FINISHED;
			if ( probe.ReadInt( firstToken ) == sizeof( firstToken ) ) {
				if ( firstToken == DS_VERSION ) {
					decodedOpenQ4Stream =
						probe.ReadInt( streamVersion ) == sizeof( streamVersion );
					supportedOpenQ4Stream =
						decodedOpenQ4Stream &&
						streamVersion >= 1 &&
						streamVersion <= OPENQ4_RENDERDEMO_CURRENT_VERSION;
				} else if ( firstToken == DS_RENDER || firstToken == DS_SOUND ) {
					// Pre-version render demos use the original v1 command stream.
					decodedOpenQ4Stream = true;
					supportedOpenQ4Stream = true;
				}
			}
		}
		probe.Close();

		if ( retailStream ) {
			// MP-22: retail Quake 4's render demos play through their own reader
			entry.type = DEMO_LIBRARY_RENDER;
			entry.format = DemoLocalized( "#str_232870" );
			entry.status = DemoLocalized( "#str_41546" );
			entry.details = DemoLocalized( "#str_41551" );
			entry.playable = true;
			entry.compatible = true;
			entry.capabilities =
				DEMO_CAP_PLAY | DEMO_CAP_PAUSE | DEMO_CAP_RATE |
				DEMO_CAP_SEEK | DEMO_CAP_STEP;
			if ( !retailMap.IsEmpty() ) {
				idStr map = retailMap;
				map.StripLeadingOnce( "maps/" );
				map.StripFileExtension();
				entry.mapName = DemoSanitizeText( Session_LevelDisplayName( map, "" ) );
			}
		} else if ( ambiguousWrapper && !decodedOpenQ4Stream ) {
			entry.type = DEMO_LIBRARY_LEGACY_RENDER;
			entry.format = DemoLocalized( "#str_41544" );
			entry.status = DemoLocalized( "#str_41547" );
			entry.details = DemoLocalized( "#str_41547" );
		} else if ( supportedOpenQ4Stream ) {
			entry.type = DEMO_LIBRARY_RENDER;
			entry.format = streamVersion > 1
				? va( "%s v%d", DemoLocalized( "#str_41541" ), streamVersion )
				: DemoLocalized( "#str_41541" );
			entry.status = DemoLocalized( "#str_41546" );
			entry.details = DemoLocalized( "#str_41551" );
			entry.playable = true;
			entry.compatible = true;
			entry.capabilities =
				DEMO_CAP_PLAY | DEMO_CAP_PAUSE | DEMO_CAP_RATE |
				DEMO_CAP_SEEK | DEMO_CAP_STEP;

			demoRenderSessionInfo_t info;
			if ( streamVersion >= OPENQ4_RENDERDEMO_SESSION_VERSION &&
				 DemoReadRenderSessionInfo( entry.path.c_str(), info ) && info.session ) {
				const idStr campaign = DemoRecordedCampaignName( info.header );
				if ( !idStr::Icmp( info.header.GetString( "mode" ), "singleplayer" ) ) {
					entry.format = DemoLocalized( "#str_41576" );
				}
				entry.gameType = info.header.GetString( "mode" );
				if ( !info.firstMap.IsEmpty() ) {
					entry.mapName = DemoSanitizeText( Session_LevelDisplayName( info.firstMap, info.firstEntityFilter ) );
				}
				const int levels = info.summarized ? info.summary.GetInt( "segments" ) : 0;
				if ( info.summarized ) {
					entry.durationMS = DemoSummaryDurationMS( info.summary );
					entry.cleanEnd = true;
				}
				idStr recordingPath = sessLocal.renderDemoRecordPath;
				recordingPath.BackSlashesToSlashes();
				if ( sessLocal.IsRenderDemoRecording() && !recordingPath.Icmp( entry.path ) ) {
					// The file being written now: never offer it half-written.
					entry.playable = false;
					entry.compatible = false;
					entry.capabilities = 0;
					entry.status = DemoLocalized( "#str_41581" );
					entry.details = entry.status;
				} else if ( !DemoRecordedContentMatches( info.header ) ) {
					// Its levels and assets belong to another campaign.
					entry.playable = false;
					entry.compatible = false;
					entry.capabilities = 0;
					entry.status = DemoLocalized( "#str_41577" );
					entry.details = va( DemoLocalized( "#str_41578" ), campaign.c_str() );
				} else if ( !info.summarized ) {
					// Interrupted: every complete frame before the cut still plays.
					entry.status = DemoLocalized( "#str_41579" );
					entry.details = DemoLocalized( "#str_41580" );
					if ( info.timeline ) {
						// its blocks still give its length
						entry.durationMS = info.timelineDurationMS;
					}
				} else {
					entry.details = levels > 0
						? va( "%s / %s", campaign.c_str(), va( DemoLocalized( "#str_41582" ), levels ) )
						: campaign;
				}
			}
		} else {
			entry.type = DEMO_LIBRARY_RENDER;
			entry.format =
				decodedOpenQ4Stream && streamVersion > 0
					? va( "%s v%d", DemoLocalized( "#str_41541" ), streamVersion )
					: DemoLocalized( "#str_41541" );
			const bool unsupportedVersion =
				decodedOpenQ4Stream &&
				streamVersion > OPENQ4_RENDERDEMO_CURRENT_VERSION;
			entry.status = DemoLocalized(
				unsupportedVersion ? "#str_41547" : "#str_41548" );
			entry.details = entry.status;
		}
	} else if ( DemoPathEndsWith( entry.path, ".cdemo" ) ) {
		entry.type = DEMO_LIBRARY_COMMAND;
		entry.format = DemoLocalized( "#str_41542" );
		entry.details = DemoLocalized( "#str_41549" );
		idStr error;
		entry.playable =
			entry.sizeBytes > 0 &&
			sessLocal.ValidateCmdDemoFile( entry.path.c_str(), error );
		entry.compatible = entry.playable;
		if ( entry.playable ) {
			entry.status = DemoLocalized( "#str_41546" );
			entry.capabilities = DEMO_CAP_PLAY;
		} else {
			entry.status = DemoLocalized( "#str_41548" );
			entry.details = DemoLocalized( "#str_41566" );
			common->DPrintf( "Demo library rejected '%s': %s\n",
				entry.path.c_str(), error.c_str() );
		}
	} else if ( openq4_retail::IsRetailNetDemoName( entry.path.c_str() ) ) {
		// openQ4 MP-22: a retail Quake 4 network demo (.ndmo85 from 1.4.2, .netdemo
		// before it). 1.4.2's plays through the MVD player; the header names any other.
		entry.type = DEMO_LIBRARY_LEGACY_NET;
		// .netdemo is what Quake 4 wrote before 1.4.2 named its demos by protocol
		const char *formatLabel = DemoPathEndsWith( entry.path, ".netdemo" ) ? "#str_41543" : "#str_232880";
		entry.format = DemoLocalized( formatLabel );
		mvdFileInfo_t info;
		const bool read = idAsyncNetwork::multiViewDemo.QueryFileInfo( entry.path.c_str(), info );
		if ( info.protocolMajor > 0 ) {
			entry.format = va( "%s %u.%u", DemoLocalized( formatLabel ), info.protocolMajor, info.protocolMinor );
		}
		if ( read ) {
			entry.playable = info.compatible;
			entry.compatible = info.compatible;
			entry.cleanEnd = info.cleanEnd;
			entry.durationMS = info.durationMS;
			entry.mapName = DemoSanitizeText( info.mapName );
			entry.gameType = DemoSanitizeText( info.gameType );
			entry.serverName = DemoSanitizeText( info.serverName );
			entry.players = DemoSanitizeText( info.players );
			entry.recorder = DemoSanitizeText( info.viewName );
			entry.clientView = info.clientView;
			entry.status = DemoLocalized( info.compatible ? "#str_41546" : "#str_41547" );
			entry.details = DemoLocalized( DemoRetailProblemString( info.error ) );
			if ( info.compatible ) {
				if ( info.clientView && !entry.recorder.IsEmpty() ) {
					entry.details = va( DemoLocalized( "#str_232844" ), entry.recorder.c_str() );
				} else if ( !entry.players.IsEmpty() ) {
					entry.details = va( DemoLocalized( "#str_232843" ), entry.players.c_str() );
				} else {
					entry.details = DemoLocalized( info.clientView ? "#str_41551" : "#str_41550" );
				}
				entry.capabilities =
					DEMO_CAP_PLAY | DEMO_CAP_PAUSE | DEMO_CAP_RATE |
					DEMO_CAP_SEEK | DEMO_CAP_STEP | DEMO_CAP_FREE_ROAM |
					DEMO_CAP_FOLLOW | DEMO_CAP_FULL_WORLD;
				if ( info.clientView ) {
					entry.capabilities &= ~( DEMO_CAP_FREE_ROAM | DEMO_CAP_FOLLOW | DEMO_CAP_FULL_WORLD );
				}
			}
		} else {
			const bool otherRelease = info.error.Find( "another Quake 4 release" ) >= 0;
			entry.status = DemoLocalized( otherRelease ? "#str_41547" : "#str_41548" );
			entry.details = DemoLocalized( DemoRetailProblemString( info.error ) );
			common->DPrintf( "Demo library rejected '%s': %s\n",
				entry.path.c_str(), info.error.c_str() );
		}
	}

	// Never offer to delete the file a recording is writing.
	idStr activeRecording = sessLocal.renderDemoRecordPath;
	activeRecording.BackSlashesToSlashes();
	const bool beingRecorded = sessLocal.IsRenderDemoRecording() && !activeRecording.IsEmpty() &&
		!activeRecording.Icmp( entry.path );
	if ( DemoEntryCanDelete( entry.path ) ) {
		if ( !beingRecorded ) {
			entry.capabilities |= DEMO_CAP_DELETE;
		}
	} else if ( !entry.details.IsEmpty() ) {
		entry.details += "\n";
		entry.details += DemoLocalized( "#str_41554" );
	}
}

// With no argument, cycle off, killer, action; the playback deck does the same.
static void Session_CycleDemoAutoTrack() {
	idCVar *autoTrack = cvarSystem->Find( "demo_autoTrack" );
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() || autoTrack == NULL ) {
		return;
	}
	idCmdArgs args;
	args.AppendArg( "mvdAutoTrack" );
	static const char * const modes[] = { "off", "killer", "action" };
	args.AppendArg( modes[ ( idMath::ClampInt( 0, 2, autoTrack->GetInteger() ) + 1 ) % 3 ] );
	idMultiViewDemo::AutoTrack_f( args );
}

#ifndef ID_DEDICATED
static void Session_DemoMenu_f( const idCmdArgs &args ) {
	sessLocal.OpenDemoMenu( !sessLocal.IsDemoPlaybackActive() );
}

static void Session_DemoPause_f( const idCmdArgs &args ) {
	if ( args.Argc() == 2 ) {
		sessLocal.SetDemoPaused( atoi( args.Argv( 1 ) ) != 0 );
	} else {
		sessLocal.ToggleDemoPaused();
	}
}

static void Session_DemoSpeed_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: demoSpeed <0.05-16>\n" );
		return;
	}
	sessLocal.SetDemoSpeed( static_cast<float>( atof( args.Argv( 1 ) ) ) );
}

static void Session_DemoSeek_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: demoSeek <seconds>\n" );
		return;
	}
	sessLocal.SeekDemoMS( static_cast<int>( atof( args.Argv( 1 ) ) * 1000.0 ) );
}

static void Session_DemoSkip_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: demoSkip <seconds>\n" );
		return;
	}
	sessLocal.SkipDemoMS( static_cast<int>( atof( args.Argv( 1 ) ) * 1000.0 ) );
}

static void Session_DemoStep_f( const idCmdArgs &args ) {
	// openQ4 MP-22: demoStep [frames], negative to step back
	sessLocal.StepDemo( args.Argc() > 1 ? atoi( args.Argv( 1 ) ) : 1 );
}

// openQ4 MP-22: demos whose name, map, mode, server, players or recorder contain the text
static void Session_DemoFind_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "usage: demoFind <text>\n" );
		return;
	}
	const idStr text = args.Args( 1, -1 );
	sessLocal.RefreshDemoLibrary();
	int found = 0;
	for ( int i = 0; i < sessLocal.demoLibrary.Num(); i++ ) {
		const demoLibraryEntry_t &entry = sessLocal.demoLibrary[i];
		const char *fields[] = { entry.displayName.c_str(), entry.mapName.c_str(), entry.gameType.c_str(),
			entry.serverName.c_str(), entry.players.c_str(), entry.recorder.c_str() };
		bool match = false;
		for ( int f = 0; f < static_cast<int>( sizeof( fields ) / sizeof( fields[0] ) ) && !match; f++ ) {
			match = idStr::FindText( fields[f], text.c_str(), false ) >= 0;
		}
		if ( match ) {
			common->Printf( "DEMO_FIND %s  %s  %s  %s%s%s\n", entry.path.c_str(), entry.mapName.c_str(),
				entry.gameType.c_str(), entry.clientView ? "view of " : "", entry.clientView ? entry.recorder.c_str() : "",
				entry.clientView ? "" : entry.players.c_str() );
			found++;
		}
	}
	common->Printf( "DEMO_FIND %d of %d demos match \"%s\"\n", found, sessLocal.demoLibrary.Num(), text.c_str() );
}

// openQ4 MP-22: the recorded players of the playing multi-view demo
static void Session_DemoPlayers_f( const idCmdArgs &args ) {
	if ( !idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		common->Printf( "No multi-view demo is playing\n" );
		return;
	}
	const int followed = idAsyncNetwork::multiViewDemo.GetFollowClient();
	int listed = 0;
	for ( int slot = 0; slot < MAX_ASYNC_CLIENTS; slot++ ) {
		idStr name;
		if ( idAsyncNetwork::multiViewDemo.CopyFollowName( slot, name ) ) {
			common->Printf( "%c %2d  %s\n", slot == followed ? '>' : ' ', slot, name.c_str() );
			listed++;
		}
	}
	common->Printf( "%d players; demoFollow <slot> or demoFollow <name> follows one\n", listed );
}

static void Session_DemoFollow_f( const idCmdArgs &args ) {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		// demoFollow [next|prev|free|<slot>|<name>]; no argument keeps the
		// historical "follow the next player".
		idMultiViewDemo::Follow_f( args );
	}
}

static void Session_DemoFreeRoam_f( const idCmdArgs &args ) {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		idAsyncNetwork::multiViewDemo.FreeRoam();
	}
}

static void Session_DemoMarker_f( const idCmdArgs &args ) {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		idMultiViewDemo::Marker_f( args );
	} else if ( sessLocal.readDemo != NULL ) {
		// A campaign demo's markers are the starts of its levels.
		const char *direction = args.Argc() > 1 ? args.Argv( 1 ) : "next";
		if ( idStr::Icmp( direction, "next" ) && idStr::Icmp( direction, "prev" ) ) {
			common->Printf( "usage: demoMarker next|prev\n" );
			return;
		}
		if ( !sessLocal.SeekRenderDemoSegment( !idStr::Icmp( direction, "next" ) ? 1 : -1 ) ) {
			common->Printf( "demoMarker: no %s level in this demo\n", direction );
		}
	}
}

static void Session_DemoRecordStatus_f( const idCmdArgs &args ) {
	sessLocal.PrintRenderDemoRecordStatus();
}

static void Session_DemoAutoTrack_f( const idCmdArgs &args ) {
	if ( args.Argc() > 1 ) {
		idMultiViewDemo::AutoTrack_f( args );
	} else {
		Session_CycleDemoAutoTrack();
	}
}

static void Session_DemoStop_f( const idCmdArgs &args ) {
	sessLocal.StopDemoPlayback();
}

static void Session_OpenQ4DemoLibrary_f( const idCmdArgs &args ) {
	idUserInterface *gui = session->GetActiveGUI();
	if ( gui == NULL || gui != sessLocal.guiDemoMenu || !sessLocal.demoBrowserMode ||
		sessLocal.guiDemoList == NULL ) {
		common->Printf( "openq4_demoLibrary requires the active demo library\n" );
		return;
	}
	const int selected = sessLocal.guiDemoList->GetSelection( NULL, 0 );
	if ( args.Argc() == 2 && !idStr::Icmp( args.Argv( 1 ), "report" ) ) {
		const int rows = Min( sessLocal.demoLibrary.Num(), 256 );
		common->Printf( "DEMO_LIBRARY count=%d visible=%d selected=%d canPlay=%d truncated=%d\n",
			sessLocal.demoLibrary.Num(), gui->State().GetInt( "demo_count" ), selected,
			gui->State().GetBool( "demo_canPlay" ), rows < sessLocal.demoLibrary.Num() );
		for ( int i = 0; i < rows; ++i ) {
			const demoLibraryEntry_t &entry = sessLocal.demoLibrary[i];
			const idStr path = DemoSanitizeText( entry.path, MAX_OSPATH );
			common->Printf( "DEMO_LIBRARY_ENTRY index=%d type=%d playable=%d capabilities=%d path=%s\n",
				i, static_cast<int>( entry.type ), entry.playable, entry.capabilities, path.c_str() );
			common->Printf( "DEMO_LIBRARY_DETAIL index=%d durationMS=%d cleanEnd=%d status=%s\n",
				i, entry.durationMS, entry.cleanEnd, DemoSanitizeText( entry.status ).c_str() );
		}
		return;
	}
	if ( args.Argc() == 2 && !idStr::Icmp( args.Argv( 1 ), "play" ) ) {
		if ( selected < 0 || selected >= sessLocal.demoLibrary.Num() ||
			( sessLocal.demoLibrary[selected].capabilities & DEMO_CAP_PLAY ) == 0 ) {
			common->Printf( "openq4_demoLibrary play requires a playable selected recording\n" );
			return;
		}
		// Invoke the same engine action as the library's Play button, without
		// generating keyboard or mouse events for an automated runtime check.
		sessLocal.HandleDemoMenuCommand( "demoPlay" );
		return;
	}
	common->Printf( "usage: openq4_demoLibrary report | play\n" );
}
#endif

}

bool Session_DemoEntryMatchesFilter( const demoLibraryEntry_t &entry, const char *filter ) {
	return DemoEntryMatchesFilter( entry, filter != NULL ? filter : DEMO_FILTER_ALL );
}

// The row caps its columns as the legacy browser's list does; the detail
// lines keep the format's player-facing revision, the status and the first
// line of the details.
void Session_DemoEntryText( const demoLibraryEntry_t &entry, sessionDemoText_t &out ) {
	out.name = DemoEllipsizeText( entry.displayName, 28 );
	out.map = DemoEllipsizeText( entry.mapName.IsEmpty() ? "-" : entry.mapName.c_str(), 18 );
	if ( entry.timestamp > 0 ) {
		out.date = DemoSanitizeText( Sys_TimeStampToStr( entry.timestamp ), 32 );
		out.date.CapLength( 10 );
	} else {
		out.date = "-";
	}
	out.length = DemoFormatTime( entry.durationMS );
	idStr kind = entry.format;
	const int separator = kind.Find( " / " );
	if ( separator >= 0 ) {
		kind.CapLength( separator );
	}
	out.kind = DemoEllipsizeText( kind, 28 );
	out.status = DemoEllipsizeText( entry.status, 28 );
	idStr detail = entry.details;
	const int newline = detail.Find( '\n' );
	if ( newline >= 0 ) {
		detail.CapLength( newline );
	}
	out.detail = DemoEllipsizeText( detail, 28 );
}

/*
================
idSessionLocal::InitDemoSystem
================
*/
void idSessionLocal::InitDemoSystem() {
	demoLibraryFilter = DEMO_FILTER_ALL;

#ifndef ID_DEDICATED
	guiDemoMenu = uiManager->FindGui( "guis/demo_menu.gui", true, false, true );
	guiDemoList = uiManager->AllocListGUI();
	if ( guiDemoList != NULL && guiDemoMenu != NULL ) {
		guiDemoList->Config( guiDemoMenu, "demoList" );
	}

	cmdSystem->AddCommand( "demoMenu", Session_DemoMenu_f, CMD_FL_SYSTEM, "opens the demo library or playback controls" );
	cmdSystem->AddCommand( "openq4_demoLibrary", Session_OpenQ4DemoLibrary_f, CMD_FL_SYSTEM | CMD_FL_CHEAT, "reports the active demo library or invokes its selected Play action for runtime validation" );
	cmdSystem->AddCommand( "demoPause", Session_DemoPause_f, CMD_FL_SYSTEM, "toggles unified demo playback pause" );
	cmdSystem->AddCommand( "demoSpeed", Session_DemoSpeed_f, CMD_FL_SYSTEM, "sets unified demo playback speed" );
	cmdSystem->AddCommand( "demoSeek", Session_DemoSeek_f, CMD_FL_SYSTEM, "seeks to a unified demo time in seconds" );
	cmdSystem->AddCommand( "demoSkip", Session_DemoSkip_f, CMD_FL_SYSTEM, "moves relative to the current demo time in seconds" );
	cmdSystem->AddCommand( "demoStep", Session_DemoStep_f, CMD_FL_SYSTEM, "steps a paused demo: demoStep [frames], negative to step back" );
	cmdSystem->AddCommand( "demoFollow", Session_DemoFollow_f, CMD_FL_SYSTEM, "follows an MVD player: demoFollow [next|prev|free|<slot>|<name>]" );
	cmdSystem->AddCommand( "demoPlayers", Session_DemoPlayers_f, CMD_FL_SYSTEM, "lists a multi-view demo's recorded players, to follow with demoFollow" );
	cmdSystem->AddCommand( "demoFind", Session_DemoFind_f, CMD_FL_SYSTEM, "lists the demos whose name, map, mode, server, players or recorder contain the text" );
	cmdSystem->AddCommand( "demoFreeRoam", Session_DemoFreeRoam_f, CMD_FL_SYSTEM, "enters MVD free-roam mode" );
	cmdSystem->AddCommand( "demoMarker", Session_DemoMarker_f, CMD_FL_SYSTEM, "seeks to an MVD marker: demoMarker next|prev [kind]" );
	cmdSystem->AddCommand( "demoAutoTrack", Session_DemoAutoTrack_f, CMD_FL_SYSTEM, "sets or cycles MVD auto-tracking: demoAutoTrack [off|killer|action] [any|marine|strogg]" );
	cmdSystem->AddCommand( "demoStop", Session_DemoStop_f, CMD_FL_SYSTEM, "stops unified demo playback" );
	cmdSystem->AddCommand( "demoRecordStatus", Session_DemoRecordStatus_f, CMD_FL_SYSTEM, "reports the render-demo recording: its state, file, frames and levels" );
#endif
}

/*
================
idSessionLocal::ShutdownDemoSystem
================
*/
void idSessionLocal::ShutdownDemoSystem() {
	if ( guiDemoList != NULL ) {
		guiDemoList->Shutdown();
		uiManager->FreeListGUI( guiDemoList );
		guiDemoList = NULL;
	}
	guiDemoMenu = NULL;
	demoLibrary.Clear();
}

/*
================
idSessionLocal::RefreshDemoLibrary
================
*/
void idSessionLocal::RefreshDemoLibrary() {
	ScanDemoLibrary();
	if ( guiDemoList == NULL || guiDemoMenu == NULL ) {
		return;
	}
	RefreshDemoLibraryList();
}

/*
================
idSessionLocal::ScanDemoLibrary

Every recording under demos/, probed, newest first.
================
*/
void idSessionLocal::ScanDemoLibrary() {
	idStrList paths;
	DemoCollectExtension( paths, ".mvd" );
	DemoCollectExtension( paths, ".demo" );
	DemoCollectExtension( paths, ".cdemo" );
	DemoCollectExtension( paths, ".netdemo" );
	DemoCollectExtension( paths, ".ndmo85" );
	DemoCollectExtension( paths, ".part" );
	DemoCollectExtension( paths, ".ucmd" );

	demoLibrary.Clear();
	for ( int i = 0; i < paths.Num(); i++ ) {
		demoLibraryEntry_t entry;
		entry.path = paths[i];
		DemoProbeEntry( entry );

		int insertAt = demoLibrary.Num();
		while ( insertAt > 0 ) {
			const demoLibraryEntry_t &previous = demoLibrary[insertAt - 1];
			if ( previous.timestamp > entry.timestamp ||
				 ( previous.timestamp == entry.timestamp &&
				   previous.displayName.Icmp( entry.displayName ) <= 0 ) ) {
				break;
			}
			insertAt--;
		}
		demoLibrary.Insert( entry, insertAt );
	}
}

/*
================
idSessionLocal::RefreshDemoLibraryList

The legacy browser's list, from the scanned library.
================
*/
void idSessionLocal::RefreshDemoLibraryList() {
	idListGUILocal *list = static_cast<idListGUILocal *>( guiDemoList );
	list->SetStateChanges( false );
	list->Clear();

	int selectedRow = -1;
	int visibleRows = 0;
	for ( int i = 0; i < demoLibrary.Num(); i++ ) {
		const demoLibraryEntry_t &entry = demoLibrary[i];
		if ( !DemoEntryMatchesFilter( entry, demoLibraryFilter ) ) {
			continue;
		}
		idStr row = DemoEllipsizeText( entry.displayName, 28 );
		row += "\t";
		// The table needs the recording format, not the internal protocol tail.
		// Showing only the former makes MVD versions legible in their narrow
		// legacy-style column; the selected-demo detail retains full metadata.
		idStr listFormat = entry.format;
		const int protocolSeparator = listFormat.Find( " / " );
		if ( protocolSeparator >= 0 ) {
			listFormat.CapLength( protocolSeparator );
		}
		row += DemoEllipsizeText( listFormat, 11 );
		row += "\t";
		row += DemoEllipsizeText( entry.mapName.IsEmpty() ? "-" : entry.mapName.c_str(), 18 );
		row += "\t";
		if ( entry.timestamp > 0 ) {
			idStr listDate = DemoSanitizeText( Sys_TimeStampToStr( entry.timestamp ), 32 );
			listDate.CapLength( 10 );
			row += listDate;
		} else {
			row += "-";
		}
		row += "\t";
		row += DemoFormatTime( entry.durationMS );
		row += "\t";
		// Treat the compact table state as a readable badge.  Long diagnostic
		// reasons remain intact in the selected row instead of becoming a noisy
		// sequence of ellipses in every browser line.
		const char *statusKey = entry.playable ? "#str_41546" :
			( entry.type == DEMO_LIBRARY_INCOMPLETE ? "#str_41570" : "#str_41569" );
		row += DemoEllipsizeText( DemoLocalized( statusKey ), 13 );
		list->Add( i, row );
		if ( !entry.path.Icmp( demoSelectedPath ) ) {
			selectedRow = visibleRows;
		}
		visibleRows++;
	}
	list->SetStateChanges( true );

	if ( selectedRow < 0 && visibleRows > 0 ) {
		selectedRow = 0;
	}
	if ( selectedRow >= 0 ) {
		list->SetSelection( selectedRow );
	}
	guiDemoMenu->SetStateInt( "demo_count", visibleRows );
	UpdateDemoMenuGui();
}

/*
================
idSessionLocal::UpdateDemoMenuGui
================
*/
void idSessionLocal::UpdateDemoMenuGui() {
	if ( guiDemoMenu == NULL ) {
		return;
	}

	const bool mvdPlaying = idAsyncNetwork::multiViewDemo.IsPlaying();
	const bool renderPlaying = readDemo != NULL;
	const bool commandPlaying = cmdDemoFile != NULL;
	const bool playing = mvdPlaying || renderPlaying || commandPlaying;
	int capabilities = 0;
	int timeMS = 0;
	int durationMS = 0;
	float speed = 1.0f;
	bool paused = false;
	bool seeking = false;
	idStr title;
	idStr view;

	if ( mvdPlaying ) {
		demoActiveType = DEMO_LIBRARY_MVD;
		capabilities =
			DEMO_CAP_PLAY | DEMO_CAP_PAUSE | DEMO_CAP_RATE |
			DEMO_CAP_SEEK | DEMO_CAP_STEP | DEMO_CAP_FREE_ROAM |
			DEMO_CAP_FOLLOW | DEMO_CAP_FULL_WORLD;
		timeMS = idAsyncNetwork::multiViewDemo.GetPlaybackTimeMS();
		durationMS = idAsyncNetwork::multiViewDemo.GetPlaybackDurationMS();
		speed = idAsyncNetwork::multiViewDemo.GetPlaybackScale();
		paused = idAsyncNetwork::multiViewDemo.IsPaused();
		seeking = idAsyncNetwork::multiViewDemo.IsSeeking();
		title = idAsyncNetwork::multiViewDemo.GetPlaybackName();
		title.StripPath();
		// openQ4 MP-22: a player's recording of their own view shows that player
		const bool clientView = idAsyncNetwork::multiViewDemo.IsClientViewPlayback();
		if ( clientView ) {
			capabilities &= ~( DEMO_CAP_FREE_ROAM | DEMO_CAP_FOLLOW | DEMO_CAP_FULL_WORLD );
		}
		const int followClient = clientView ? idAsyncNetwork::multiViewDemo.GetPlaybackLocalClient() :
			idAsyncNetwork::multiViewDemo.GetFollowClient();
		idStr followName;
		if ( followClient < 0 ) {
			view = DemoLocalized( "#str_41539" );
		} else if ( idAsyncNetwork::multiViewDemo.CopyFollowName( followClient, followName ) ) {
			view = va( "%s %s", DemoLocalized( "#str_41560" ), followName.c_str() );
		} else {
			view = va( "%s %d", DemoLocalized( "#str_41560" ), followClient + 1 );
		}
	} else if ( renderPlaying ) {
		demoActiveType = DEMO_LIBRARY_RENDER;
		capabilities = DEMO_CAP_PLAY;
		if ( !timeDemo && !aviCaptureMode ) {
			capabilities |=
				DEMO_CAP_PAUSE | DEMO_CAP_RATE |
				DEMO_CAP_SEEK | DEMO_CAP_STEP;
		}
		timeMS = DemoCurrentRenderTimeMS( *this );
		durationMS = renderDemoKnownDurationMS;
		speed = renderDemoPlaybackRate;
		paused = renderDemoPaused;
		seeking = renderDemoSeeking;
		title = activeRenderDemoName;
		title.StripPath();
		// A campaign demo names the level playing; others keep the camera note.
		view = renderDemoFrameClock && renderDemoPlaySegment >= 0
			? RenderDemoSegmentLabel() : idStr( DemoLocalized( "#str_41551" ) );
	} else if ( commandPlaying ) {
		demoActiveType = DEMO_LIBRARY_COMMAND;
		capabilities = DEMO_CAP_PLAY;
		title = DemoLocalized( "#str_41542" );
		view = DemoLocalized( "#str_41549" );
	} else {
		demoActiveType = DEMO_LIBRARY_UNKNOWN;
		demoOverlayVisible = false;
		guiDemoMenu->SetStateBool( "demo_active", false );
		guiDemoMenu->SetStateBool( "demo_paused", false );
		guiDemoMenu->SetStateBool( "demo_seeking", false );
		guiDemoMenu->SetStateBool( "demo_canPause", false );
		guiDemoMenu->SetStateBool( "demo_canRate", false );
		guiDemoMenu->SetStateBool( "demo_canSeek", false );
		guiDemoMenu->SetStateBool( "demo_canStep", false );
		guiDemoMenu->SetStateBool( "demo_canFreeRoam", false );
		guiDemoMenu->SetStateBool( "demo_canFollow", false );
		guiDemoMenu->SetStateBool( "demo_canMarker", false );
	}

	if ( seeking ) {
		capabilities &= ~DEMO_CAP_STEP;
	}
	if ( playing ) {
		const float fraction = durationMS > 0
			? idMath::ClampFloat( 0.0f, 1.0f, static_cast<float>( timeMS ) / durationMS )
			: 0.0f;
		if ( guiActive != guiDemoMenu || !guiDemoMenu->State().GetBool( "demo_scrubbing" ) ) {
			demo_uiSeek.SetFloat( fraction );
		}
		guiDemoMenu->SetStateString( "demo_title", DemoSanitizeText( title ) );
		guiDemoMenu->SetStateString( "demo_time", DemoFormatTime( timeMS, false ) );
		guiDemoMenu->SetStateString( "demo_duration", DemoFormatTime( durationMS ) );
		guiDemoMenu->SetStateString( "demo_speed", va( "%.2fx", speed ) );
		guiDemoMenu->SetStateString( "demo_view", DemoSanitizeText( view ) );
		guiDemoMenu->SetStateString( "demo_status", DemoSanitizeText( title ) );
		guiDemoMenu->SetStateFloat( "demo_position", fraction );
		guiDemoMenu->SetStateBool( "demo_active", true );
		guiDemoMenu->SetStateBool( "demo_paused", paused );
		guiDemoMenu->SetStateBool( "demo_seeking", seeking );
		guiDemoMenu->SetStateBool( "demo_canPlay", true );
		guiDemoMenu->SetStateBool( "demo_canPause", ( capabilities & DEMO_CAP_PAUSE ) != 0 );
		guiDemoMenu->SetStateBool( "demo_canRate", ( capabilities & DEMO_CAP_RATE ) != 0 );
		guiDemoMenu->SetStateBool( "demo_canSeek", ( capabilities & DEMO_CAP_SEEK ) != 0 );
		guiDemoMenu->SetStateBool( "demo_canStep", ( capabilities & DEMO_CAP_STEP ) != 0 );
		guiDemoMenu->SetStateBool( "demo_canFreeRoam", ( capabilities & DEMO_CAP_FREE_ROAM ) != 0 );
		guiDemoMenu->SetStateBool( "demo_canFollow", ( capabilities & DEMO_CAP_FOLLOW ) != 0 );
		// A campaign demo's markers are its level starts.
		guiDemoMenu->SetStateBool( "demo_canMarker",
			( mvdPlaying && idAsyncNetwork::multiViewDemo.GetMarkerCount() > 0 ) ||
			( renderPlaying && renderDemoFrameClock && renderDemoPlaySegments.Num() > 1 &&
			  ( capabilities & DEMO_CAP_SEEK ) != 0 ) );
		// openQ4 MP-22: the recorded players, to follow one by name
		demoPlayerRows = 0;
		const int followed = mvdPlaying ? idAsyncNetwork::multiViewDemo.GetFollowClient() : -1;
		for ( int slot = 0; slot < MAX_ASYNC_CLIENTS && demoPlayerRows < DEMO_PLAYER_ROWS; slot++ ) {
			idStr name;
			if ( mvdPlaying && ( capabilities & DEMO_CAP_FOLLOW ) != 0 &&
				 idAsyncNetwork::multiViewDemo.CopyFollowName( slot, name ) ) {
				guiDemoMenu->SetStateString( va( "demo_player%d_name", demoPlayerRows ), DemoSanitizeText( name ) );
				guiDemoMenu->SetStateBool( va( "demo_player%d_on", demoPlayerRows ), slot == followed );
				demoPlayerRowSlots[ demoPlayerRows++ ] = slot;
			}
		}
		for ( int row = 0; row < DEMO_PLAYER_ROWS; row++ ) {
			guiDemoMenu->SetStateBool( va( "demo_player%d_shown", row ), row < demoPlayerRows );
		}
		if ( demoPlayerRows == 0 ) {
			demoPlayersOpen = false;
		}
		guiDemoMenu->SetStateBool( "demo_canPlayers", demoPlayerRows > 0 );
		guiDemoMenu->SetStateBool( "demo_playersOpen", demoPlayersOpen );
		const idCVar *autoTrack = cvarSystem->Find( "demo_autoTrack" );
		static const char * const autoTrackLabels[] = { "#str_41573", "#str_41574", "#str_41575" };
		guiDemoMenu->SetStateString( "demo_autoTrackLabel", DemoLocalized( autoTrackLabels[
			autoTrack != NULL ? idMath::ClampInt( 0, 2, autoTrack->GetInteger() ) : 0 ] ) );
	}

	if ( demoBrowserMode && guiDemoList != NULL ) {
		const int selection = guiDemoList->GetSelection( NULL, 0 );
		const demoLibraryEntry_t *entry =
			selection >= 0 && selection < demoLibrary.Num() ? &demoLibrary[selection] : NULL;
		if ( entry != NULL ) {
			demoSelectedPath = entry->path;
			idStr details = DemoEllipsizeText( entry->details, 36 );
			idStr metadata = DemoEllipsizeText( entry->mapName, 20 );
			if ( !entry->format.IsEmpty() ) {
				// The table needs the full format/protocol pair, while this concise
				// summary only needs the player-facing format revision.  Keeping the
				// first segment avoids ending a narrow detail line on a dangling slash.
				idStr displayFormat = entry->format;
				const int protocolSeparator = displayFormat.Find( " / " );
				if ( protocolSeparator >= 0 ) {
					displayFormat.CapLength( protocolSeparator );
				}
				if ( !metadata.IsEmpty() ) {
					metadata += " / ";
				}
				metadata += DemoEllipsizeText( displayFormat, 12 );
			}
			if ( !details.IsEmpty() && !metadata.IsEmpty() ) {
				details += " / ";
			}
			details += metadata;
			guiDemoMenu->SetStateString( "demo_selectedName", DemoEllipsizeText( entry->displayName, 43 ) );
			guiDemoMenu->SetStateString( "demo_selectedMap", DemoSanitizeText( entry->mapName ) );
			guiDemoMenu->SetStateString( "demo_status", DemoSanitizeText( entry->status ) );
			guiDemoMenu->SetStateString( "demo_details", DemoEllipsizeText( details, 52 ) );
			guiDemoMenu->SetStateBool( "demo_canPlay", ( entry->capabilities & DEMO_CAP_PLAY ) != 0 );
			guiDemoMenu->SetStateBool( "demo_canDelete", ( entry->capabilities & DEMO_CAP_DELETE ) != 0 );
			guiDemoMenu->SetStateBool( "demo_canPause", ( entry->capabilities & DEMO_CAP_PAUSE ) != 0 );
			guiDemoMenu->SetStateBool( "demo_canRate", ( entry->capabilities & DEMO_CAP_RATE ) != 0 );
			guiDemoMenu->SetStateBool( "demo_canSeek", ( entry->capabilities & DEMO_CAP_SEEK ) != 0 );
			guiDemoMenu->SetStateBool( "demo_canStep", ( entry->capabilities & DEMO_CAP_STEP ) != 0 );
			guiDemoMenu->SetStateBool( "demo_canFreeRoam", ( entry->capabilities & DEMO_CAP_FREE_ROAM ) != 0 );
			guiDemoMenu->SetStateBool( "demo_canFollow", ( entry->capabilities & DEMO_CAP_FOLLOW ) != 0 );
		} else {
			guiDemoMenu->SetStateString( "demo_selectedName", DemoLocalized( "#str_41552" ) );
			guiDemoMenu->SetStateString( "demo_selectedMap", "" );
			guiDemoMenu->SetStateString( "demo_status", "" );
			guiDemoMenu->SetStateString( "demo_details", DemoLocalized( "#str_41552" ) );
			guiDemoMenu->SetStateBool( "demo_canPlay", false );
			guiDemoMenu->SetStateBool( "demo_canDelete", false );
		}
	}

	guiDemoMenu->SetStateBool( "demo_browserMode", demoBrowserMode );
	guiDemoMenu->SetStateBool( "demo_interactive", guiActive == guiDemoMenu );
	guiDemoMenu->SetStateString( "demo_filter", demoLibraryFilter.c_str() );
	// A GUI expression compares numbers only: against a string every filter
	// read as the chosen one. The tags' rails follow this index.
	int filterIndex = 0;
	if ( !demoLibraryFilter.Icmp( DEMO_FILTER_MVD ) ) {
		filterIndex = 1;
	} else if ( !demoLibraryFilter.Icmp( DEMO_FILTER_RENDER ) ) {
		filterIndex = 2;
	} else if ( !demoLibraryFilter.Icmp( DEMO_FILTER_LEGACY ) ) {
		filterIndex = 3;
	} else if ( !demoLibraryFilter.Icmp( DEMO_FILTER_INCOMPLETE ) ) {
		filterIndex = 4;
	}
	guiDemoMenu->SetStateInt( "demo_filterIndex", filterIndex );
	guiDemoMenu->StateChanged( common->GetPresentationTime() );
}

/*
================
idSessionLocal::OpenDemoMenu
================
*/
void idSessionLocal::OpenDemoMenu( bool browser ) {
#ifdef ID_DEDICATED
	return;
#else
	if ( guiDemoMenu == NULL ) {
		return;
	}
	if ( guiActive == guiDemoMenu ) {
		UpdateDemoMenuGui();
		return;
	}

	demoReturnGui = guiActive;
	demoBrowserMode = browser && !IsDemoPlaybackActive();
	demoMenuOpenedOverPlayback = IsDemoPlaybackActive();
	demoResumeAfterMenu = false;
	if ( demoMenuOpenedOverPlayback ) {
		const bool wasPaused = idAsyncNetwork::multiViewDemo.IsPlaying()
			? idAsyncNetwork::multiViewDemo.IsPaused()
			: ( readDemo != NULL && renderDemoPaused );
		demoResumeAfterMenu = !wasPaused;
		SetDemoPaused( true );
		demoOverlayVisible = true;
	}

	if ( demoBrowserMode ) {
		RefreshDemoLibrary();
	}
	// Set the mode state before SetGUI activates the menu.  The browser's
	// onActivate script selects its entrance treatment from demo_browserMode.
	UpdateDemoMenuGui();
	SetGUI( guiDemoMenu, NULL );
	UpdateDemoMenuGui();
#endif
}

/*
================
idSessionLocal::CloseDemoMenu
================
*/
void idSessionLocal::CloseDemoMenu() {
	if ( guiActive == guiDemoMenu ) {
		guiDemoMenu->Activate( false, common->GetPresentationTime() );
		guiActive = NULL;
	}

	if ( demoMenuOpenedOverPlayback && IsDemoPlaybackActive() ) {
		SetPlayingSoundWorld( sw );
		if ( demoResumeAfterMenu ) {
			SetDemoPaused( false );
		}
		demoMenuOpenedOverPlayback = false;
		demoResumeAfterMenu = false;
		return;
	}

	demoMenuOpenedOverPlayback = false;
	demoResumeAfterMenu = false;
	if ( demoReturnGui != NULL && demoReturnGui != guiDemoMenu ) {
		SetGUI( demoReturnGui, NULL );
	} else if ( guiMainMenu != NULL ) {
		StartMenu( false );
	}
	demoReturnGui = NULL;
}

/*
================
idSessionLocal::IsDemoPlaybackActive
================
*/
bool idSessionLocal::IsDemoPlaybackActive() const {
	return idAsyncNetwork::multiViewDemo.IsPlaying() || readDemo != NULL || cmdDemoFile != NULL;
}

/*
================
idSessionLocal::SetDemoPaused
================
*/
void idSessionLocal::SetDemoPaused( bool paused ) {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		idAsyncNetwork::multiViewDemo.SetPaused( paused );
	} else if ( readDemo != NULL && !timeDemo && !aviCaptureMode ) {
		renderDemoPaused = paused;
		// the clock resumes where it stopped, not where real time went meanwhile
		renderDemoPlayRealClockUS = -1.0;
		if ( sw != NULL ) {
			if ( paused ) {
				sw->Pause();
			} else {
				sw->UnPause();
			}
		}
	}
	UpdateDemoMenuGui();
}

void idSessionLocal::ToggleDemoPaused() {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		idAsyncNetwork::multiViewDemo.TogglePaused();
	} else if ( readDemo != NULL && !timeDemo && !aviCaptureMode ) {
		SetDemoPaused( !renderDemoPaused );
	}
	UpdateDemoMenuGui();
}

void idSessionLocal::SetDemoSpeed( float speed ) {
	const float clamped = idMath::ClampFloat( 0.05f, 16.0f, speed );
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		idAsyncNetwork::multiViewDemo.SetPlaybackScale( clamped );
	} else if ( readDemo != NULL && !timeDemo && !aviCaptureMode ) {
		renderDemoPlaybackRate = clamped;
		if ( sw != NULL ) {
			sw->SetSlowmoSpeed( clamped );
		}
	}
	UpdateDemoMenuGui();
}

/*
================
idSessionLocal::FinishRenderDemoSeek
================
*/
void idSessionLocal::FinishRenderDemoSeek() {
	if ( renderDemoSeekMuteOwned ) {
		soundSystem->SetMute( renderDemoSeekRestoreMute );
	}
	if ( renderDemoSeeking && readDemo != NULL && DemoCurrentRenderTimeMS( *this ) >= renderDemoSeekTargetMS ) {
		common->DPrintf( "render demo: seek to %d ms reached frame %d in %d ms\n", renderDemoSeekTargetMS, numDemoFrames,
			Sys_Milliseconds() - renderDemoSeekStartedMsec );
	}
	renderDemoSeekMuteOwned = false;
	renderDemoSeeking = false;
	renderDemoSeekRestartPending = false;
	renderDemoSeekRestartPresentationPending = false;
	renderDemoSeekJumpEntry = -1;
	renderDemoSeekWorkDeferred = false;
	renderDemoSeekTargetMS = 0;
	renderDemoSeekDeadlineMS = 0;
	// playback goes on from the frame the seek reached
	SyncRenderDemoPlayClock();
}

/*
================
idSessionLocal::ProcessRenderDemoSeekBudget

Render and sound demo commands mutate main-thread state, so seeking remains on
the main thread but is split across presentation frames to keep the UI live.
================
*/
void idSessionLocal::ProcessRenderDemoSeekBudget() {
	if ( !renderDemoSeeking ) {
		return;
	}
	if ( readDemo == NULL ) {
		FinishRenderDemoSeek();
		return;
	}

	// Rewinding is deferred until the next presentation frame so the seeking
	// state can be drawn before a render-demo map/first-frame reload. A single
	// renderer command may still perform indivisible work, but command-stream
	// replay after that point is cooperatively budgeted below.
	if ( renderDemoSeekRestartPending &&
		 renderDemoSeekRestartPresentationPending ) {
		renderDemoSeekRestartPresentationPending = false;
		UpdateDemoMenuGui();
		return;
	}
	if ( renderDemoSeekRestartPending && renderDemoSeekJumpEntry >= 0 ) {
		// A v14 recording: read on from the level start or keyframe before
		// the target. The frame being read ends first, so the jump leaves
		// nothing half applied.
		const int entry = renderDemoSeekJumpEntry;
		renderDemoSeekJumpEntry = -1;
		renderDemoSeekDeadlineMS = 0;
		renderDemoSeekWorkDeferred = false;
		while ( readDemo != NULL && renderDemoFramePayloadPending ) {
			const int before = numDemoFrames;
			AdvanceRenderDemo( true );
			if ( readDemo != NULL && numDemoFrames == before && renderDemoFramePayloadPending ) {
				break;
			}
		}
		if ( readDemo == NULL ) {
			// the stream ended on the way: its teardown restored the mute
			return;
		}
		if ( !JumpRenderDemoToEntry( entry ) ) {
			// replay from the first frame instead
			return;
		}
		renderDemoSeekRestartPending = false;
		// the first frame from there: a level's load is one indivisible step
		AdvanceRenderDemo( true );
		if ( readDemo == NULL ) {
			return;
		}
		if ( DemoCurrentRenderTimeMS( *this ) >= renderDemoSeekTargetMS ) {
			FinishRenderDemoSeek();
		}
		UpdateDemoMenuGui();
		return;
	}
	if ( renderDemoSeekRestartPending ) {
		const int target = renderDemoSeekTargetMS;
		const bool restoreMute = renderDemoSeekRestoreMute;
		const bool wasPaused = renderDemoPaused;
		const float oldRate = renderDemoPlaybackRate;
		const bool restoreDemoMenu = guiDemoMenu != NULL && guiActive == guiDemoMenu;
		const idStr demoName = activeRenderDemoName;

		FinishRenderDemoSeek();
		soundSystem->SetMute( true );
		StartPlayingRenderDemo( demoName );
		if ( readDemo == NULL ) {
			soundSystem->SetMute( restoreMute );
			if ( restoreDemoMenu ) {
				OpenDemoMenu( true );
			}
			UpdateDemoMenuGui();
			return;
		}

		renderDemoPlaybackRate = oldRate;
		renderDemoPaused = wasPaused;
		renderDemoStepPending = false;
		if ( sw != NULL ) {
			sw->SetSlowmoSpeed( oldRate );
			if ( wasPaused ) {
				sw->Pause();
			} else if ( sw->IsPaused() ) {
				sw->UnPause();
			}
		}
		if ( restoreDemoMenu && guiDemoMenu != NULL && guiActive != guiDemoMenu ) {
			// Keep the activation script from reading the previous browser/playback
			// mode while a render-demo seek restores the overlay.
			UpdateDemoMenuGui();
			SetGUI( guiDemoMenu, NULL );
		}

		renderDemoSeekTargetMS = target;
		renderDemoSeeking = true;
		renderDemoSeekMuteOwned = true;
		renderDemoSeekRestoreMute = restoreMute;
		soundSystem->SetMute( true );
		if ( DemoCurrentRenderTimeMS( *this ) >= renderDemoSeekTargetMS ) {
			FinishRenderDemoSeek();
		}
		UpdateDemoMenuGui();
		return;
	}

	const int deadline = Sys_Milliseconds() +
		idMath::ClampInt( 1, 50, demo_renderSeekBudgetMS.GetInteger() );
	renderDemoSeekDeadlineMS = deadline;
	int frames = 0;
	while ( readDemo != NULL &&
		 DemoCurrentRenderTimeMS( *this ) < renderDemoSeekTargetMS &&
		 frames < 512 && Sys_Milliseconds() <= deadline ) {
		const int beforeFrame = numDemoFrames;
		const int beforeTime = DemoCurrentRenderTimeMS( *this );
		renderDemoSeekWorkDeferred = false;
		AdvanceRenderDemo( true );
		frames++;

		if ( readDemo == NULL ) {
			// EOF/error teardown restores the owned mute state.
			return;
		}
		if ( renderDemoSeekWorkDeferred ) {
			break;
		}
		if ( numDemoFrames == beforeFrame &&
			 DemoCurrentRenderTimeMS( *this ) <= beforeTime ) {
			// A one-frame demo intentionally remains open at EOF. Its duration
			// is exactly zero, so a positive seek target cannot be reached.
			FinishRenderDemoSeek();
			UpdateDemoMenuGui();
			return;
		}
	}

	if ( readDemo == NULL ||
		 DemoCurrentRenderTimeMS( *this ) >= renderDemoSeekTargetMS ) {
		FinishRenderDemoSeek();
	} else {
		renderDemoSeekDeadlineMS = 0;
	}
	UpdateDemoMenuGui();
}

/*
================
idSessionLocal::SeekDemoMS
================
*/
bool idSessionLocal::SeekDemoMS( int timeMS ) {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		return idAsyncNetwork::multiViewDemo.SeekToMS( timeMS );
	}
	if ( readDemo == NULL || activeRenderDemoName.IsEmpty() ||
		 timeDemo || aviCaptureMode ) {
		return false;
	}

	int target = Max( 0, timeMS );
	if ( renderDemoKnownDurationMS >= 0 ) {
		target = Min( target, Max( 0, renderDemoKnownDurationMS - 1 ) );
	}
	if ( target == DemoCurrentRenderTimeMS( *this ) ) {
		if ( renderDemoSeeking ) {
			FinishRenderDemoSeek();
			UpdateDemoMenuGui();
		}
		return true;
	}

	// A v14 recording opens at the last level start or keyframe before the
	// target: a seek back replays the seconds since it, and a seek ahead skips
	// what lies before it. Any other recording replays from its first frame
	// to go back.
	const int targetEntry = RenderDemoTimelineEntryAt( target );
	if ( target < DemoCurrentRenderTimeMS( *this ) ||
		 ( targetEntry >= 0 && renderDemoPlayTimeline.entries[targetEntry].startFrame > numDemoFrames ) ) {
		const bool restoreMute = renderDemoSeekMuteOwned
			? renderDemoSeekRestoreMute : soundSystem->IsMuted();
		FinishRenderDemoSeek();
		renderDemoSeekRestoreMute = restoreMute;
		renderDemoSeekRestartPending = true;
		renderDemoSeekRestartPresentationPending = true;
		renderDemoSeekJumpEntry = targetEntry;
	} else {
		const bool restoreMute = renderDemoSeekMuteOwned
			? renderDemoSeekRestoreMute : soundSystem->IsMuted();
		if ( renderDemoSeekRestartPending ) {
			FinishRenderDemoSeek();
		}
		renderDemoSeekRestoreMute = restoreMute;
	}

	renderDemoStepPending = false;
	renderDemoSeekTargetMS = target;
	if ( !renderDemoSeeking ) {
		renderDemoSeekStartedMsec = Sys_Milliseconds();
	}
	renderDemoSeeking = true;
	renderDemoSeekMuteOwned = true;
	soundSystem->SetMute( true );
	UpdateDemoMenuGui();
	return true;
}

bool idSessionLocal::SkipDemoMS( int deltaMS ) {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		return idAsyncNetwork::multiViewDemo.SeekByMS( deltaMS );
	}
	if ( readDemo != NULL ) {
		return SeekDemoMS( DemoCurrentRenderTimeMS( *this ) + deltaMS );
	}
	return false;
}

void idSessionLocal::StepDemo( int frames ) {
	if ( frames == 0 ) {
		frames = 1;
	}
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		if ( !idAsyncNetwork::multiViewDemo.IsSeeking() ) {
			idAsyncNetwork::multiViewDemo.StepFrames( frames );
		}
	} else if ( readDemo != NULL && !renderDemoSeeking &&
			   !timeDemo && !aviCaptureMode ) {
		SetDemoPaused( true );
		if ( frames > 0 ) {
			renderDemoStepPending = true;
		} else {
			// openQ4 MP-22: back to the frame before this one, from its keyframe.
			// A seek stops at the first frame at or after its target, so on a
			// frame timeline the target is that frame's own time.
			int target = DemoCurrentRenderTimeMS( *this ) - 1;
			if ( renderDemoFrameClock && renderDemoPrevFrameTimeUS < renderDemoFrameTimeUS ) {
				target = static_cast<int>( renderDemoPrevFrameTimeUS / 1000 );
			}
			SeekDemoMS( Max( 0, target ) );
		}
	}
	UpdateDemoMenuGui();
}

/*
================
idSessionLocal::StopDemoPlayback
================
*/
void idSessionLocal::StopDemoPlayback() {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		idCmdArgs stop;
		stop.AppendArg( "stopMVD" );
		idMultiViewDemo::Stop_f( stop );
		return;
	}
	if ( readDemo != NULL || cmdDemoFile != NULL ) {
		Stop();
		StartMenu( false );
	}
	demoOverlayVisible = false;
}

/*
================
idSessionLocal::PlayDemoLibraryEntry

Plays a library entry, as the browser's Play does. False when it cannot
play or a render or command demo could not start; the menus are then left
to the caller.
================
*/
bool idSessionLocal::PlayDemoLibraryEntry( int index ) {
	if ( index < 0 || index >= demoLibrary.Num() || ( demoLibrary[index].capabilities & DEMO_CAP_PLAY ) == 0 ) {
		return false;
	}
	const demoLibraryEntry_t entry = demoLibrary[index];
	guiActive = NULL;
	demoReturnGui = NULL;
	demoBrowserMode = false;
	demoOverlayVisible = true;
	if ( entry.type == DEMO_LIBRARY_MVD || entry.type == DEMO_LIBRARY_LEGACY_NET ) {
		// openQ4 MP-22: a retail network demo plays through the MVD player too
		idCmdArgs play;
		play.AppendArg( "playMVD" );
		play.AppendArg( entry.path.c_str() );
		idMultiViewDemo::Play_f( play );
	} else if ( entry.type == DEMO_LIBRARY_RENDER ) {
		StartPlayingRenderDemo( entry.path );
		return readDemo != NULL;
	} else if ( entry.type == DEMO_LIBRARY_COMMAND ) {
		idStr commandName = entry.path;
		if ( idStr::Icmpn( commandName.c_str(), "demos/", 6 ) == 0 ) {
			commandName = commandName.c_str() + 6;
		}
		StartPlayingCmdDemo( commandName.c_str() );
		return cmdDemoFile != NULL;
	}
	return true;
}

/*
================
idSessionLocal::DeleteDemoLibraryFile

Deletes a library entry's recording, which must lie in the save path's
demo directory. The caller asked first and rescans after.
================
*/
bool idSessionLocal::DeleteDemoLibraryFile( const demoLibraryEntry_t &entry ) {
	if ( ( entry.capabilities & DEMO_CAP_DELETE ) == 0 || !DemoPathIsSafe( entry.path ) ) {
		return false;
	}
	const idStr saveDemoRoot = fileSystem->RelativePathToOSPath( "demos", "fs_savepath" );
	const idStr explicitPath = fileSystem->RelativePathToOSPath( entry.path.c_str(), "fs_savepath" );
	if ( explicitPath.Icmpn( saveDemoRoot.c_str(), saveDemoRoot.Length() ) != 0 ||
		 !DemoEntryCanDelete( entry.path ) ) {
		common->Warning( "Refusing to delete demo outside the save-path demo directory" );
		return false;
	}
	if ( fileSystem->RemoveExplicitFile( explicitPath.c_str() ) != 0 ) {
		common->Warning( "Could not delete demo '%s'", entry.path.c_str() );
		return false;
	}
	// openQ4 MP-22: and its thumbnail
	idStr thumbnail = entry.path;
	thumbnail.StripFileExtension();
	thumbnail += "_thumb.png";
	if ( fileSystem->ReadFile( thumbnail.c_str(), NULL, NULL ) > 0 ) {
		fileSystem->RemoveExplicitFile( fileSystem->RelativePathToOSPath( thumbnail.c_str(), "fs_savepath" ) );
	}
	return true;
}

/*
================
idSessionLocal::HandleDemoMenuCommand
================
*/
bool idSessionLocal::HandleDemoMenuCommand( const char *menuCommand ) {
	idCmdArgs args;
	args.TokenizeString( menuCommand, false );
	if ( args.Argc() <= 0 ) {
		return false;
	}

	bool handled = false;
	for ( int icmd = 0; icmd < args.Argc(); ) {
		const char *cmd = args.Argv( icmd++ );
		if ( !idStr::Icmp( cmd, ";" ) ) {
			continue;
		}
		if ( !idStr::Icmp( cmd, "play" ) ) {
			if ( icmd < args.Argc() ) {
				idStr sound = args.Argv( icmd++ );
				int channel = 1;
				if ( sound.Length() == 1 && idStr::IsNumeric( sound.c_str() ) &&
					 icmd < args.Argc() ) {
					channel = atoi( sound.c_str() );
					sound = args.Argv( icmd++ );
				}
				if ( menuSoundWorld != NULL ) {
					menuSoundWorld->PlayShaderDirectly( sound, channel );
				}
			}
			handled = true;
			continue;
		}

	if ( !idStr::Icmp( cmd, "demoOpen" ) ) {
		OpenDemoMenu( true );
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoRefresh" ) ) {
		RefreshDemoLibrary();
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoFilter" ) ) {
		const char *filter = icmd < args.Argc() ? args.Argv( icmd++ ) : DEMO_FILTER_ALL;
		if ( idStr::Icmp( filter, DEMO_FILTER_ALL ) &&
			 idStr::Icmp( filter, DEMO_FILTER_MVD ) &&
			 idStr::Icmp( filter, DEMO_FILTER_RENDER ) &&
			 idStr::Icmp( filter, DEMO_FILTER_LEGACY ) &&
			 idStr::Icmp( filter, DEMO_FILTER_INCOMPLETE ) ) {
			filter = DEMO_FILTER_ALL;
		}
		demoLibraryFilter = filter;
		RefreshDemoLibrary();
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoSelect" ) ) {
		UpdateDemoMenuGui();
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoPlay" ) ) {
		if ( guiDemoList == NULL ) {
			return true;
		}
		const int selection = guiDemoList->GetSelection( NULL, 0 );
		if ( selection < 0 || selection >= demoLibrary.Num() ) {
			return true;
		}
		const demoLibraryEntry_t entry = demoLibrary[selection];
		if ( ( entry.capabilities & DEMO_CAP_PLAY ) == 0 ) {
			MessageBox( MSG_OK, entry.details.c_str(), entry.status.c_str(), true );
			return true;
		}
		if ( !PlayDemoLibraryEntry( selection ) ) {
			OpenDemoMenu( true );
		}
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoDelete" ) ) {
		if ( guiDemoList == NULL ) {
			return true;
		}
		const int selection = guiDemoList->GetSelection( NULL, 0 );
		if ( selection < 0 || selection >= demoLibrary.Num() ) {
			return true;
		}
		const demoLibraryEntry_t entry = demoLibrary[selection];
		if ( ( entry.capabilities & DEMO_CAP_DELETE ) == 0 ||
			 !DemoPathIsSafe( entry.path ) ) {
			return true;
		}
		const char *answer = MessageBox(
			MSG_YESNO,
			va( DemoLocalized( "#str_41555" ), entry.displayName.c_str() ),
			DemoLocalized( "#str_41556" ),
			true, "yes" );
		if ( answer == NULL || answer[0] == '\0' ) {
			return true;
		}

		DeleteDemoLibraryFile( entry );
		demoSelectedPath.Clear();
		RefreshDemoLibrary();
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoClose" ) ) {
		CloseDemoMenu();
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoPause" ) ) {
		ToggleDemoPaused();
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoSkip" ) ) {
		if ( icmd < args.Argc() ) {
			SkipDemoMS( static_cast<int>( atof( args.Argv( icmd++ ) ) * 1000.0 ) );
		}
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoSeek" ) ) {
		int duration = 0;
		if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
			duration = idAsyncNetwork::multiViewDemo.GetPlaybackDurationMS();
		} else if ( readDemo != NULL ) {
			duration = renderDemoKnownDurationMS;
		}
		if ( icmd < args.Argc() ) {
			SeekDemoMS( static_cast<int>( atof( args.Argv( icmd++ ) ) * 1000.0 ) );
		} else if ( duration > 0 ) {
			SeekDemoMS( static_cast<int>( demo_uiSeek.GetFloat() * duration ) );
		}
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoStep" ) ) {
		StepDemo( icmd < args.Argc() ? atoi( args.Argv( icmd++ ) ) : 1 );
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoSpeed" ) ) {
		if ( icmd < args.Argc() ) {
			SetDemoSpeed( static_cast<float>( atof( args.Argv( icmd++ ) ) ) );
		}
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoFollow" ) ) {
		if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
			idAsyncNetwork::multiViewDemo.FollowNext();
		}
		return true;
	}
	// openQ4 MP-22: the deck's player list
	if ( !idStr::Icmp( cmd, "demoPlayers" ) ) {
		demoPlayersOpen = !demoPlayersOpen && demoPlayerRows > 0;
		UpdateDemoMenuGui();
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoFollowRow" ) ) {
		const int row = icmd < args.Argc() ? atoi( args.Argv( icmd++ ) ) : -1;
		if ( row >= 0 && row < demoPlayerRows && idAsyncNetwork::multiViewDemo.IsPlaying() ) {
			idAsyncNetwork::multiViewDemo.FollowClient( demoPlayerRowSlots[ row ] );
		}
		demoPlayersOpen = false;
		UpdateDemoMenuGui();
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoFollowPrev" ) ) {
		if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
			idAsyncNetwork::multiViewDemo.FollowPrev();
		}
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoFreeRoam" ) ) {
		if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
			idAsyncNetwork::multiViewDemo.FreeRoam();
		}
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoMarkerNext" ) || !idStr::Icmp( cmd, "demoMarkerPrev" ) ) {
		if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
			idAsyncNetwork::multiViewDemo.SeekToMarker( !idStr::Icmp( cmd, "demoMarkerNext" ) ? 1 : -1, 0 );
			UpdateDemoMenuGui();
		} else if ( readDemo != NULL ) {
			SeekRenderDemoSegment( !idStr::Icmp( cmd, "demoMarkerNext" ) ? 1 : -1 );
			UpdateDemoMenuGui();
		}
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoAutoTrack" ) ) {
		Session_CycleDemoAutoTrack();
		UpdateDemoMenuGui();
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoStop" ) ) {
		StopDemoPlayback();
		return true;
	}
	}
	return handled;
}

/*
===============================================================================

	Campaign render demos (render demo v11)

	One recording spans a whole campaign session. Each level it reaches is a
	self-contained segment: a DSC_SEGMENT record, a fresh hash-string table,
	the complete sound state, the level's world and portal state, and every
	visible definition again on its first frame. A map change, save load,
	death reload or restart suspends the recording while the level loads; the
	next level starts the next segment. Only gameplay draws end frames, so the
	recording holds no menus, pauses or loading screens, and its timeline counts
	frames rather than the game clock each level restarts.

===============================================================================
*/

int Session_RenderDemoFramesToMS( int frames, int userCmdHz ) {
	if ( frames <= 0 || userCmdHz <= 0 ) {
		return 0;
	}
	const int64 msec = static_cast<int64>( frames ) * USERCMD_PER_DEMO_FRAME * 1000 / userCmdHz;
	return msec > idMath::INT_MAX ? idMath::INT_MAX : static_cast<int>( msec );
}

bool Session_RenderDemoContentMatches( const char *path, idStr &reason ) {
	reason.Clear();
	demoRenderSessionInfo_t info;
	if ( path == NULL || !DemoReadRenderSessionInfo( path, info ) || !info.session ) {
		// Older streams make no claim; their own decoder checks remain.
		return true;
	}
	if ( DemoRecordedContentMatches( info.header ) ) {
		return true;
	}
	reason = va( DemoLocalized( "#str_41578" ), DemoRecordedCampaignName( info.header ).c_str() );
	return false;
}

/*
================
idSessionLocal::RenderDemoFramesToMS

The time of a frame of a recording made before v13, two tics a frame.
================
*/
int idSessionLocal::RenderDemoFramesToMS( int frames ) const {
	return Session_RenderDemoFramesToMS( frames, renderDemoUserCmdHz > 0 ? renderDemoUserCmdHz : common->GetUserCmdHz() );
}

/*
================
idSessionLocal::RenderDemoCurrentTimeMS

Where the frame on screen is on the demo's timeline.
================
*/
int idSessionLocal::RenderDemoCurrentTimeMS() const {
	if ( readDemo == NULL ) {
		return 0;
	}
	if ( renderDemoFrameClock ) {
		// A campaign demo's levels restart their game clocks; its frames do not.
		return static_cast<int>( Min( Max( renderDemoFrameTimeUS, static_cast<int64>( 0 ) ) / 1000,
			static_cast<int64>( idMath::INT_MAX ) ) );
	}
	return Max( 0, currentDemoRenderView.time - renderDemoStartViewTime );
}

/*
================
idSessionLocal::CompleteRenderDemoFrame

A frame's DC_END_FRAME was read: count it and place it on the timeline. A v13
frame is its DSC_FRAME_TIME after the frame before it; an earlier one is two
tics at the recording's tic rate. False when a v13 frame came without its time.
================
*/
bool idSessionLocal::CompleteRenderDemoFrame() {
	const int deltaUS = renderDemoPendingFrameDeltaUS;
	renderDemoPendingFrameDeltaUS = -1;
	if ( renderDemoTimedFrames && deltaUS < 0 ) {
		return false;
	}
	numDemoFrames++;
	renderDemoPrevFrameTimeUS = renderDemoFrameTimeUS;
	if ( numDemoFrames == 1 ) {
		renderDemoPrevFrameTimeUS = 0;
		renderDemoFrameTimeUS = 0;
	} else if ( renderDemoTimedFrames ) {
		renderDemoFrameTimeUS += deltaUS;
	} else {
		const int userCmdHz = Max( 1, renderDemoUserCmdHz > 0 ? renderDemoUserCmdHz : common->GetUserCmdHz() );
		renderDemoFrameTimeUS = static_cast<int64>( numDemoFrames - 1 ) * USERCMD_PER_DEMO_FRAME * 1000000 / userCmdHz;
	}
	// The levels met before this frame whose starts the trailer did not give,
	// one that drew nothing included, start at this frame.
	for ( int i = 0; i < renderDemoPlaySegments.Num(); i++ ) {
		renderDemoSegment_t &segment = renderDemoPlaySegments[i];
		if ( segment.startMS < 0 && segment.startFrame == numDemoFrames - 1 ) {
			segment.startMS = RenderDemoCurrentTimeMS();
		}
	}
	return true;
}

/*
================
idSessionLocal::SyncRenderDemoPlayClock

Puts the playback clock in the middle of the frame on screen, which runs from
the previous frame's time to its own, so the next frame is due after half
this one's duration. Halfway, not at the start: a clock on a frame boundary
would make each later update a coin flip between two frames whenever the
display runs at the recording's rate, or capture steps by its frame time.
================
*/
void idSessionLocal::SyncRenderDemoPlayClock() {
	if ( aviCaptureMode ) {
		// Capture steps a fixed interval: half of one before the frame on
		// screen ends, every step samples the middle of an interval.
		renderDemoPlayClockUS = static_cast<double>( renderDemoFrameTimeUS ) - 0.5 * RenderDemoCaptureStepUS();
	} else {
		renderDemoPlayClockUS = 0.5 * ( static_cast<double>( renderDemoPrevFrameTimeUS ) +
			static_cast<double>( renderDemoFrameTimeUS ) );
	}
	renderDemoPlayRealClockUS = -1.0;
}

/*
================
idSessionLocal::RenderDemoCaptureStepUS

The recording time one captured frame covers: com_aviDemoTics tics.
================
*/
double idSessionLocal::RenderDemoCaptureStepUS() const {
	const int userCmdHz = Max( 1, renderDemoUserCmdHz > 0 ? renderDemoUserCmdHz : common->GetUserCmdHz() );
	return com_aviDemoTics.GetInteger() * 1000000.0 / userCmdHz;
}

/*
================
idSessionLocal::RenderDemoPlaybackElapsedUS

Real time since the last playback update, for the playback clock. A long
stall (a level load, a hitch) counts as a quarter of a second at most, so
playback never races to catch up with it. Capture steps a fixed
com_aviDemoTics tics a frame instead.
================
*/
double idSessionLocal::RenderDemoPlaybackElapsedUS() {
	if ( aviCaptureMode ) {
		return RenderDemoCaptureStepUS();
	}
	static const double MAX_PLAYBACK_STEP_US = 250000.0;
	const double now = DemoClockUS();
	if ( renderDemoPlayRealClockUS < 0.0 ) {
		renderDemoPlayRealClockUS = now;
		return 0.0;
	}
	const double elapsed = Min( Max( now - renderDemoPlayRealClockUS, 0.0 ), MAX_PLAYBACK_STEP_US );
	renderDemoPlayRealClockUS = now;
	return elapsed;
}

/*
================
idSessionLocal::EndRenderDemoRecordFrame

Ends the gameplay frame just drawn as a recorded frame: its 2D, its time and
DC_END_FRAME. Every gameplay draw is a frame, so the recording keeps the frame
rate the game was played at. A frame's time is the real time since the
previous recorded frame when that was the draw before this one; after a menu,
a pause or a level load, which leave no frames, it is one tic. A hitch counts
as a quarter of a second at most, more than the game itself catches up on.
================
*/
void idSessionLocal::EndRenderDemoRecordFrame() {
	if ( writeDemo == NULL ) {
		return;
	}
	static const double MAX_RECORDED_FRAME_US = 250000.0;
	renderSystem->WriteDemoPics();

	const double now = DemoClockUS();
	int deltaUS = 0;
	if ( renderDemoRecordFrames > 0 ) {
		if ( renderDemoRecordLastFrameDraw == renderDemoDrawSerial - 1 ) {
			deltaUS = static_cast<int>( Min( Max( now - renderDemoRecordLastFrameClockUS, 0.0 ), MAX_RECORDED_FRAME_US ) );
		} else {
			deltaUS = 1000000 / Max( 1, common->GetUserCmdHz() );
		}
	}
	writeDemo->WriteInt( DS_SESSION );
	writeDemo->WriteInt( DSC_FRAME_TIME );
	writeDemo->WriteInt( deltaUS );
	renderSystem->WriteDemoEndFrame();
	// the frame is whole: its block may close here, and carries its time
	writeDemo->EndFrame( deltaUS );

	renderDemoRecordTimeUS += deltaUS;
	renderDemoRecordFrames++;
	renderDemoRecordLastFrameClockUS = now;
	renderDemoRecordLastFrameDraw = renderDemoDrawSerial;
	// The levels begun since the last frame start with this one, including a
	// level that was left again before it drew anything.
	const int startMS = static_cast<int>( Min( renderDemoRecordTimeUS / 1000, static_cast<int64>( idMath::INT_MAX ) ) );
	for ( int i = renderDemoRecordSegments.Num() - 1; i >= 0 && renderDemoRecordSegments[i].startMS < 0; i-- ) {
		renderDemoRecordSegments[i].startMS = startMS;
	}
	const int keyframeSeconds = demo_recordKeyframeSeconds.GetInteger();
	if ( keyframeSeconds > 0 &&
		 renderDemoRecordTimeUS - renderDemoRecordKeyframeUS >= static_cast<int64>( keyframeSeconds ) * 1000000 ) {
		WriteRenderDemoKeyframe();
	}
	CheckRenderDemoRecordHealth();
}

/*
================
idSessionLocal::WriteRenderDemoKeyframe

Between two frames, gives the level being recorded a second way in: the
records a segment starts with (reason "keyframe") and every definition
playback holds, in blocks of their own that playback in order skips. A seek
then replays the seconds since the keyframe instead of the level so far, and
playback that meets a damaged block resumes at the next one.

The keyframe has a hash-string table of its own. The stream after it starts
another, announced by DSC_RESYNC, which both kinds of playback read.
================
*/
void idSessionLocal::WriteRenderDemoKeyframe() {
	if ( writeDemo == NULL || renderDemoRecordFile == NULL || renderDemoRecordSegments.Num() == 0 ||
		 rw == NULL || sw == NULL ) {
		return;
	}
	const int startMsec = Sys_Milliseconds();
	const int64 streamBefore = renderDemoRecordFile->StreamBytesWritten();
	const renderDemoSegment_t &level = renderDemoRecordSegments[renderDemoRecordSegments.Num() - 1];
	idDict record;
	record.SetInt( "index", renderDemoRecordSegments.Num() - 1 );
	record.SetInt( "frame", renderDemoRecordFrames );
	record.SetInt( "levelFrame", level.startFrame );
	record.Set( "map", level.map );
	record.Set( "entityFilter", level.entityFilter );
	record.Set( "reason", "keyframe" );
	record.SetInt( "skill", cvarSystem->GetCVarInteger( "g_skill" ) );

	renderDemoRecordFile->BeginKeyframe();
	renderDemoRecordFile->WriteInt( DS_SESSION );
	renderDemoRecordFile->WriteInt( DSC_SEGMENT );
	renderDemoRecordFile->WriteDict( record );
	sw->StartWritingDemo( writeDemo );
	rw->WriteDemoKeyframe( false );
	renderDemoRecordFile->WriteInt( DS_SESSION );
	renderDemoRecordFile->WriteInt( DSC_SEGMENT_READY );
	renderDemoRecordFile->WriteDict( idDict() );
	// the definitions come after the level's load, as they do in a level
	// played from its start
	rw->WriteDemoKeyframe( true );
	renderDemoRecordFile->EndKeyframe();

	renderDemoRecordFile->WriteInt( DS_SESSION );
	renderDemoRecordFile->WriteInt( DSC_RESYNC );
	renderDemoRecordFile->WriteDict( idDict() );

	renderDemoRecordKeyframeUS = renderDemoRecordTimeUS;
	common->DPrintf( "render demo: keyframe at frame %d, %d s: %d KB of stream written in %d ms\n", renderDemoRecordFrames,
		static_cast<int>( renderDemoRecordTimeUS / 1000000 ),
		static_cast<int>( ( renderDemoRecordFile->StreamBytesWritten() - streamBefore ) / 1024 ),
		Sys_Milliseconds() - startMsec );
}

/*
================
idSessionLocal::CheckRenderDemoRecordHealth

Ends the recording cleanly, with its summary, before it can be cut off: when
a block could not be written, when the save path's disk runs low (a campaign
must still be able to save) or when the file reaches its size limit. Called
when a recorded frame is whole and nothing after it in the draw writes to the
recording, so the recording ends here and now; a queued command would wait
behind whatever script is running.
================
*/
void idSessionLocal::CheckRenderDemoRecordHealth() {
	if ( renderDemoRecordFile == NULL ) {
		return;
	}
	idStr reason;
	if ( renderDemoRecordFile->WriteFailed() ) {
		reason = renderDemoRecordFile->WriteError();
	} else {
		static const int HEALTH_INTERVAL_MSEC = 2000;
		const int now = Sys_Milliseconds();
		if ( now - renderDemoRecordHealthMsec < HEALTH_INTERVAL_MSEC ) {
			return;
		}
		renderDemoRecordHealthMsec = now;
		const int maxMB = demo_recordMaxSizeMB.GetInteger();
		const int minFreeMB = demo_recordMinFreeMB.GetInteger();
		if ( maxMB > 0 && renderDemoRecordFile->BytesWritten() >= static_cast<int64>( maxMB ) * 1024 * 1024 ) {
			reason = va( "the file reached demo_recordMaxSizeMB (%d MB)", maxMB );
		} else if ( minFreeMB > 0 ) {
			const int freeMB = Sys_GetDriveFreeSpace( cvarSystem->GetCVarString( "fs_savepath" ) );
			if ( freeMB >= 0 && freeMB < minFreeMB ) {
				reason = va( "the disk has %d MB free, less than demo_recordMinFreeMB (%d MB)", freeMB, minFreeMB );
			}
		}
	}
	if ( reason.IsEmpty() ) {
		return;
	}
	common->Warning( "Recording %s ends here: %s", renderDemoRecordPath.c_str(), reason.c_str() );
	StopRecordingRenderDemo();
}

/*
================
idSessionLocal::BeginRenderDemoSegment

Starts the level that is now spawned as the recording's next segment.
================
*/
void idSessionLocal::BeginRenderDemoSegment( const char *reason ) {
	if ( rw == NULL || sw == NULL ) {
		return;
	}
	// An armed recording's file opens with its first level.
	if ( renderDemoRecordFile == NULL && !OpenRenderDemoRecordFile() ) {
		renderDemoRecordState = RENDER_DEMO_RECORD_IDLE;
		renderDemoRecordPath.Clear();
		renderDemoNextSegmentReason.Clear();
		return;
	}
	renderDemoSegment_t segment;
	segment.startFrame = renderDemoRecordFrames;
	segment.startMS = -1;		// its first frame's time, once drawn
	// the level load in between is no time on the timeline
	renderDemoRecordLastFrameDraw = idMath::INT_MIN;
	// a level's start is a way in already: the next keyframe is an interval on
	renderDemoRecordKeyframeUS = renderDemoRecordTimeUS;
	segment.map = mapSpawnData.serverInfo.GetString( "si_map", currentMapName.c_str() );
	segment.entityFilter = mapSpawnData.serverInfo.GetString( "si_entityFilter", "" );
	segment.reason = reason != NULL && reason[0] != '\0' ? reason : "level";

	idDict record;
	record.SetInt( "index", renderDemoRecordSegments.Num() );
	record.SetInt( "frame", segment.startFrame );
	record.Set( "map", segment.map );
	record.Set( "entityFilter", segment.entityFilter );
	record.Set( "reason", segment.reason );
	record.SetInt( "skill", cvarSystem->GetCVarInteger( "g_skill" ) );
	// The segment begins its own block and its own string table, the record
	// that announces it included (v14). Nothing in it refers to anything
	// before it, so the table stays bounded across a long campaign and
	// playback can open the file here.
	renderDemoRecordFile->BeginSegment();
	renderDemoRecordFile->WriteInt( DS_SESSION );
	renderDemoRecordFile->WriteInt( DSC_SEGMENT );
	renderDemoRecordFile->WriteDict( record );

	writeDemo = renderDemoRecordFile;
	renderDemoRecordState = RENDER_DEMO_RECORD_RECORDING;
	// the level's complete sound and world state, then every visible
	// definition again as it is drawn
	sw->StartWritingDemo( writeDemo );
	rw->StartWritingDemo( writeDemo );
	renderDemoRecordFile->WriteInt( DS_SESSION );
	renderDemoRecordFile->WriteInt( DSC_SEGMENT_READY );
	renderDemoRecordFile->WriteDict( idDict() );

	renderDemoRecordSegments.Append( segment );
	renderDemoNextSegmentReason.Clear();
	common->Printf( "RENDER_DEMO_SEGMENT record index=%d frame=%d reason=%s map=%s filter=%s\n",
		renderDemoRecordSegments.Num() - 1, segment.startFrame, segment.reason.c_str(),
		segment.map.c_str(), segment.entityFilter.c_str() );
}

/*
================
idSessionLocal::SuspendRenderDemoRecording

A single-player level is going away; nothing is written until the next one.
================
*/
void idSessionLocal::SuspendRenderDemoRecording() {
	if ( renderDemoRecordState != RENDER_DEMO_RECORD_RECORDING ) {
		return;
	}
	if ( sw != NULL ) {
		sw->StopWritingDemo();
	}
	if ( rw != NULL ) {
		rw->StopWritingDemo();
	}
	writeDemo = NULL;
	renderDemoRecordState = RENDER_DEMO_RECORD_SUSPENDED;
	common->DPrintf( "render demo %s suspended after %d frames\n", renderDemoRecordPath.c_str(), renderDemoRecordFrames );
}

/*
================
idSessionLocal::ContinueRenderDemoAfterMapChange

ExecuteMapChange has spawned a level. A campaign recording that is armed or
between levels records it as its next segment; anything but single-player
play ends the recording instead.
================
*/
void idSessionLocal::ContinueRenderDemoAfterMapChange() {
	if ( renderDemoRecordState != RENDER_DEMO_RECORD_ARMED &&
		 renderDemoRecordState != RENDER_DEMO_RECORD_SUSPENDED ) {
		return;
	}
	if ( IsMultiplayer() || idAsyncNetwork::multiViewDemo.IsPlaying() || readDemo != NULL || !mapSpawned ) {
		common->Printf( "Campaign recording ends: %s is not a single-player level.\n",
			mapSpawnData.serverInfo.GetString( "si_map", "the new map" ) );
		StopRecordingRenderDemo();
		return;
	}
	BeginRenderDemoSegment( renderDemoNextSegmentReason.c_str() );
}

/*
================
idSessionLocal::PrintRenderDemoRecordStatus
================
*/
void idSessionLocal::PrintRenderDemoRecordStatus() {
	static const char *const states[] = { "idle", "armed", "recording", "suspended" };
	const int state = idMath::ClampInt( 0, 3, static_cast<int>( renderDemoRecordState ) );
	const renderDemoSegment_t *segment = renderDemoRecordSegments.Num() > 0
		? &renderDemoRecordSegments[renderDemoRecordSegments.Num() - 1] : NULL;
	common->Printf( "RENDER_DEMO_RECORDING state=%s path=%s frames=%d seconds=%d segments=%d map=%s elapsed=%d bytes=%lld streamBytes=%lld\n",
		states[state], renderDemoRecordPath.IsEmpty() ? "-" : renderDemoRecordPath.c_str(),
		renderDemoRecordFrames, static_cast<int>( renderDemoRecordTimeUS / 1000000 ),
		renderDemoRecordSegments.Num(), segment != NULL ? segment->map.c_str() : "-",
		IsRenderDemoRecording() ? ( Sys_Milliseconds() - renderDemoRecordStartMsec ) / 1000 : 0,
		renderDemoRecordFile != NULL ? static_cast<long long>( renderDemoRecordFile->BytesWritten() ) : 0LL,
		renderDemoRecordFile != NULL ? static_cast<long long>( renderDemoRecordFile->StreamBytesWritten() ) : 0LL );
}

/*
================
idSessionLocal::ResetRenderDemoPlaybackSession

Called as playback opens activeRenderDemoName. A clean campaign demo's
trailer supplies its duration and level starts before any are reached.
================
*/
void idSessionLocal::ResetRenderDemoPlaybackSession() {
	CloseRenderDemoLevelLoad();
	renderDemoFrameClock = false;
	renderDemoUserCmdHz = 0;
	renderDemoPlayHeader.Clear();
	renderDemoPlaySegments.Clear();
	renderDemoPlaySegment = -1;
	renderDemoPlayMap.Clear();
	renderDemoSeekJumpEntry = -1;
	// A v14 recording's blocks place its levels and keyframes in the file and
	// on the timeline, stopped cleanly or not.
	renderDemoPlayTimelineValid = idDemoFile::ReadTimeline( activeRenderDemoName.c_str(), renderDemoPlayTimeline ) &&
		renderDemoPlayTimeline.timed && renderDemoPlayTimeline.frames > 0;
	if ( !renderDemoPlayTimelineValid ) {
		renderDemoPlayTimeline.entries.Clear();
	}

	idDict summary;
	if ( !idDemoFile::ReadTrailer( activeRenderDemoName.c_str(), summary ) ||
		 summary.GetInt( "version" ) < OPENQ4_RENDERDEMO_SESSION_VERSION ) {
		if ( renderDemoPlayTimelineValid ) {
			// Cut short: the blocks still give its length and where each level
			// starts, so it seeks and has level markers like any other. A
			// level is named once playback reaches it.
			renderDemoPlayHeader.SetInt( "frames", renderDemoPlayTimeline.frames );
			renderDemoPlayHeader.SetInt( "durationMS",
				static_cast<int>( Min( renderDemoPlayTimeline.durationUS / 1000, static_cast<int64>( idMath::INT_MAX ) ) ) );
			for ( int i = 0; i < renderDemoPlayTimeline.entries.Num(); i++ ) {
				const demoTimelineEntry_t &entry = renderDemoPlayTimeline.entries[i];
				if ( entry.keyframe ) {
					continue;
				}
				renderDemoSegment_t segment;
				segment.startFrame = entry.startFrame;
				segment.startMS = static_cast<int>( Min( entry.startTimeUS / 1000, static_cast<int64>( idMath::INT_MAX ) ) );
				renderDemoPlaySegments.Append( segment );
			}
		}
		return;
	}
	renderDemoUserCmdHz = idMath::ClampInt( 0, 1000, summary.GetInt( "userCmdHz" ) );
	renderDemoPlayHeader.SetInt( "frames", Max( 0, summary.GetInt( "frames" ) ) );
	renderDemoPlayHeader.SetInt( "durationMS", DemoSummaryDurationMS( summary ) );
	const bool timedFrames = summary.GetInt( "version" ) >= OPENQ4_RENDERDEMO_FRAME_TIME_VERSION;
	const int count = idMath::ClampInt( 0, 4096, summary.GetInt( "segments" ) );
	for ( int i = 0; i < count; i++ ) {
		renderDemoSegment_t segment;
		if ( !DemoParseSegmentSummary( summary.GetString( va( "segment%d", i ) ), segment ) ) {
			// A malformed list is only a convenience lost; segments are still met.
			renderDemoPlaySegments.Clear();
			break;
		}
		if ( !timedFrames ) {
			segment.startMS = RenderDemoFramesToMS( segment.startFrame );
		}
		if ( renderDemoPlaySegments.Num() > 0 &&
			 segment.startFrame < renderDemoPlaySegments[renderDemoPlaySegments.Num() - 1].startFrame ) {
			renderDemoPlaySegments.Clear();
			break;
		}
		renderDemoPlaySegments.Append( segment );
	}
}

/*
================
idSessionLocal::CloseRenderDemoLevelLoad

Ends the media load a segment's first records opened, purging what the
previous level used and this one does not.
================
*/
void idSessionLocal::CloseRenderDemoLevelLoad() {
	if ( !renderDemoLevelLoadOpen ) {
		return;
	}
	renderDemoLevelLoadOpen = false;
	renderSystem->EndLevelLoad();
	soundSystem->EndLevelLoad( va( "maps/%s", renderDemoPlayMap.c_str() ) );
	declManager->EndLevelLoad();
	if ( bse ) {
		bse->EndLevelLoad();
	}
}

/*
================
idSessionLocal::ProcessRenderDemoSessionRecord

Reads the DS_SESSION record that follows its token. False when malformed;
finished is set at a clean end, after which nothing is read.
================
*/
bool idSessionLocal::ProcessRenderDemoSessionRecord( bool &finished ) {
	finished = false;
	int record = 0;
	if ( readDemo->ReadInt( record ) != sizeof( record ) ) {
		return false;
	}
	if ( record == DSC_FRAME_TIME ) {
		// v13: the frame being read lasts this long after the one before it.
		// One per frame, before its DC_END_FRAME; its single int is no dictionary.
		static const int MAX_FRAME_TIME_US = 60 * 1000000;
		int deltaUS = -1;
		if ( !renderDemoTimedFrames || readDemo->ReadInt( deltaUS ) != sizeof( deltaUS ) ||
			 deltaUS < 0 || deltaUS > MAX_FRAME_TIME_US || renderDemoPendingFrameDeltaUS >= 0 ) {
			return false;
		}
		renderDemoPendingFrameDeltaUS = deltaUS;
		return true;
	}
	const bool segmentOwnsItsRecord = renderdemoVersion >= OPENQ4_RENDERDEMO_BLOCK_VERSION;
	if ( ( record == DSC_SEGMENT || record == DSC_RESYNC ) && segmentOwnsItsRecord ) {
		// v14: a segment's string table begins with the record that announces
		// it, and the table starts again after a keyframe
		readDemo->ResetHashStrings();
	}
	idDict dict;
	readDemo->ReadDict( dict );
	if ( !readDemo->IsOpen() ) {
		return false;
	}

	switch ( record ) {
	case DSC_HEADER: {
		// Trailer values already read stay; the header adds the rest.
		renderDemoPlayHeader.Copy( dict );
		const int userCmdHz = dict.GetInt( "userCmdHz" );
		if ( userCmdHz < 10 || userCmdHz > 1000 ) {
			return false;
		}
		renderDemoUserCmdHz = userCmdHz;
		renderDemoFrameClock = true;
		return true;
	}
	case DSC_SEGMENT: {
		if ( !segmentOwnsItsRecord ) {
			readDemo->ResetHashStrings();
		}
		renderDemoSegment_t segment;
		segment.startFrame = numDemoFrames;
		// a v13 level starts at its first frame's time (CompleteRenderDemoFrame)
		segment.startMS = renderDemoTimedFrames ? -1 : RenderDemoFramesToMS( numDemoFrames );
		segment.map = dict.GetString( "map" );
		segment.entityFilter = dict.GetString( "entityFilter" );
		segment.reason = dict.GetString( "reason", "level" );
		if ( segment.map.IsEmpty() || dict.GetInt( "frame", "-1" ) != numDemoFrames ) {
			// Its frame count must agree with the frames played: a segment
			// out of step means the stream lost or gained frames.
			return false;
		}
		// A keyframe is a way into a level that began earlier: the level is
		// the segment, not the keyframe.
		const bool keyframe = !segment.reason.Icmp( "keyframe" );
		if ( keyframe ) {
			segment.startFrame = idMath::ClampInt( 0, numDemoFrames, dict.GetInt( "levelFrame", va( "%d", numDemoFrames ) ) );
			segment.startMS = -1;
			segment.reason = "level";
			for ( int i = 0; renderDemoPlayTimelineValid && i < renderDemoPlayTimeline.entries.Num(); i++ ) {
				const demoTimelineEntry_t &entry = renderDemoPlayTimeline.entries[i];
				if ( !entry.keyframe && entry.startFrame == segment.startFrame ) {
					segment.startMS = static_cast<int>( Min( entry.startTimeUS / 1000, static_cast<int64>( idMath::INT_MAX ) ) );
				}
			}
		}
		int index = -1;
		for ( int i = 0; i < renderDemoPlaySegments.Num(); i++ ) {
			if ( renderDemoPlaySegments[i].startFrame == segment.startFrame ) {
				index = i;
				break;
			}
		}
		if ( index < 0 ) {
			index = 0;
			while ( index < renderDemoPlaySegments.Num() &&
					renderDemoPlaySegments[index].startFrame < segment.startFrame ) {
				index++;
			}
			renderDemoPlaySegments.Insert( segment, index );
		} else if ( renderDemoPlaySegments[index].map.IsEmpty() ) {
			// known from the block headers of a recording without its summary: now it has a name
			renderDemoPlaySegments[index].map = segment.map;
			renderDemoPlaySegments[index].entityFilter = segment.entityFilter;
			renderDemoPlaySegments[index].reason = segment.reason;
		}
		renderDemoPlaySegment = index;

		// A previous segment that never reached its ready record still ends its load.
		CloseRenderDemoLevelLoad();
		const bool newLevel = segment.map.Icmp( renderDemoPlayMap ) != 0;
		renderDemoPlayMap = segment.map;
		if ( newLevel ) {
			if ( numDemoFrames > 0 && !renderDemoSeeking && !timeDemo && !aviCaptureMode && guiLoading != NULL ) {
				// The level loads in one indivisible step: say which one first.
				guiLoading->SetStateString( "demo", Session_LevelDisplayName( segment.map, segment.entityFilter ).c_str() );
				guiLoading->StateChanged( common->GetPresentationTime() );
				openq4::NativeInputBeforeSessionChange();
				insideExecuteMapChange = true;
				UpdateScreen();
				openq4::NativeInputBeforeSessionChange();
				insideExecuteMapChange = false;
				guiLoading->SetStateString( "demo", "" );
			}
			// The outgoing level's sounds stop before their shaders can be purged;
			// the segment's sound state restarts this level's.
			sw->StopAllSounds();
			declManager->BeginLevelLoad();
			renderSystem->BeginLevelLoad();
			soundSystem->BeginLevelLoad();
			if ( bse ) {
				bse->BeginLevelLoad();
			}
			renderDemoLevelLoadOpen = true;
		}
		if ( keyframe ) {
			common->DPrintf( "render demo: entered level %d at a keyframe, frame %d\n", index, numDemoFrames );
		} else {
			common->Printf( "RENDER_DEMO_SEGMENT play index=%d frame=%d reason=%s map=%s\n",
				index, segment.startFrame, segment.reason.c_str(), segment.map.c_str() );
		}
		return true;
	}
	case DSC_RESYNC:
		// its string table reset is all there is to it
		return true;
	case DSC_SEGMENT_READY:
		CloseRenderDemoLevelLoad();
		// the level load took real time the recording never had
		renderDemoPlayRealClockUS = -1.0;
		return true;
	case DSC_END:
		CloseRenderDemoLevelLoad();
		finished = true;
		return true;
	default:
		// A later minor revision's record: its dictionary was read, so skip it.
		common->DPrintf( "Render demo '%s': skipped session record %d\n", activeRenderDemoName.c_str(), record );
		return true;
	}
}

/*
================
idSessionLocal::RenderDemoTimelineEntryAt

Where a seek to a time in a v14 recording starts reading: the last level
start or keyframe at or before it, as an index into the block timeline. -1
when the recording has no block timeline, and every seek back replays from its
first frame.
================
*/
int idSessionLocal::RenderDemoTimelineEntryAt( int timeMS ) const {
	if ( readDemo == NULL || !readDemo->IsBlockStream() || !renderDemoPlayTimelineValid ||
		 renderdemoVersion < OPENQ4_RENDERDEMO_BLOCK_VERSION || !renderDemoFrameClock ) {
		return -1;
	}
	const bool keyframes = demo_useKeyframes.GetBool();
	int index = -1;
	for ( int i = 0; i < renderDemoPlayTimeline.entries.Num(); i++ ) {
		// the frames an entry leads to come after the time it is stamped with
		if ( i > 0 && renderDemoPlayTimeline.entries[i].startTimeUS / 1000 > timeMS ) {
			break;
		}
		if ( keyframes || !renderDemoPlayTimeline.entries[i].keyframe ) {
			index = i;
		}
	}
	return index;
}

/*
================
idSessionLocal::JumpRenderDemoToEntry

Continues playback at the first block of a level or at a keyframe. Either
decodes and plays with nothing before it, as a level does when playback
reaches it in order, so only the frame count and the timeline move there.
================
*/
bool idSessionLocal::JumpRenderDemoToEntry( int timelineEntry ) {
	if ( readDemo == NULL || !renderDemoPlayTimelineValid || timelineEntry < 0 ||
		 timelineEntry >= renderDemoPlayTimeline.entries.Num() ) {
		return false;
	}
	const demoTimelineEntry_t &entry = renderDemoPlayTimeline.entries[timelineEntry];
	if ( !readDemo->SeekToEntry( entry ) ) {
		return false;
	}
	numDemoFrames = entry.startFrame;
	renderDemoFrameTimeUS = entry.startTimeUS;
	renderDemoPrevFrameTimeUS = entry.startTimeUS;
	renderDemoPendingFrameDeltaUS = -1;
	renderDemoFramePayloadPending = false;
	renderDemoStepPending = false;
	common->DPrintf( "render demo: opened at %s of level %d, frame %d, %lld ms\n", entry.keyframe ? "a keyframe" : "the start",
		entry.level, numDemoFrames, static_cast<long long>( renderDemoFrameTimeUS / 1000 ) );
	return true;
}

/*
================
idSessionLocal::ResumeRenderDemoAfterDamage

The stream stopped at a block that is cut or fails its checksum. Everything
after it leans on it, up to the next level start or keyframe, which leans on
nothing: playback goes on from there, having lost the frames in between.
================
*/
bool idSessionLocal::ResumeRenderDemoAfterDamage() {
	if ( readDemo == NULL || !readDemo->IsBlockStream() || !renderDemoPlayTimelineValid ||
		 renderdemoVersion < OPENQ4_RENDERDEMO_BLOCK_VERSION ) {
		return false;
	}
	const int64 damagedAt = readDemo->DamagedAt();
	if ( damagedAt < 0 ) {
		return false;
	}
	for ( int i = 0; i < renderDemoPlayTimeline.entries.Num(); i++ ) {
		if ( renderDemoPlayTimeline.entries[i].fileOffset <= damagedAt ) {
			continue;
		}
		const int lostFrom = numDemoFrames;
		if ( !JumpRenderDemoToEntry( i ) ) {
			return false;
		}
		common->Warning( "Render demo '%s' resumes at %s after the damage: frames %d to %d are lost",
			activeRenderDemoName.c_str(), DemoFormatTime( static_cast<int>( renderDemoFrameTimeUS / 1000 ), false ).c_str(),
			lostFrom + 1, numDemoFrames );
		return true;
	}
	return false;
}

/*
================
idSessionLocal::SeekRenderDemoSegment

Moves to the start of the next level, or back to the start of this one (the
previous one when already near its start), as a media player's chapters do.
================
*/
bool idSessionLocal::SeekRenderDemoSegment( int direction ) {
	if ( readDemo == NULL || !renderDemoFrameClock || renderDemoPlaySegments.Num() == 0 ||
		 timeDemo || aviCaptureMode ) {
		return false;
	}
	const int reference = renderDemoSeeking ? renderDemoSeekTargetMS : DemoCurrentRenderTimeMS( *this );
	int target = -1;
	if ( direction > 0 ) {
		for ( int i = 0; i < renderDemoPlaySegments.Num(); i++ ) {
			const int start = renderDemoPlaySegments[i].startMS;
			if ( start > reference ) {
				target = start;
				break;
			}
		}
	} else {
		static const int RESTART_LEVEL_WINDOW_MS = 2000;
		for ( int i = renderDemoPlaySegments.Num() - 1; i >= 0; i-- ) {
			// a level whose start is not known yet (-1) is never before this one
			const int start = renderDemoPlaySegments[i].startMS;
			if ( start >= 0 && start < reference - RESTART_LEVEL_WINDOW_MS ) {
				target = start;
				break;
			}
		}
		if ( target < 0 ) {
			target = 0;
		}
	}
	if ( target < 0 ) {
		return false;
	}
	return SeekDemoMS( target );
}

/*
================
idSessionLocal::RenderDemoSegmentLabel

"Level 2/5: Hangar Defense" for the playback overlay.
================
*/
idStr idSessionLocal::RenderDemoSegmentLabel() const {
	if ( renderDemoPlaySegment < 0 || renderDemoPlaySegment >= renderDemoPlaySegments.Num() ) {
		return DemoLocalized( "#str_41551" );
	}
	const renderDemoSegment_t &segment = renderDemoPlaySegments[renderDemoPlaySegment];
	const idStr level = Session_LevelDisplayName( segment.map, segment.entityFilter );
	// how many levels there are: a v14 recording's blocks say, cut short or not
	const int known = renderDemoPlayTimelineValid ? renderDemoPlayTimeline.levels
		: ( renderDemoPlayHeader.GetInt( "frames" ) > 0 ? renderDemoPlaySegments.Num() : 0 );
	if ( known > 0 ) {
		return va( "%s %d/%d: %s", DemoLocalized( "#str_41583" ), renderDemoPlaySegment + 1, known, level.c_str() );
	}
	return va( "%s %d: %s", DemoLocalized( "#str_41583" ), renderDemoPlaySegment + 1, level.c_str() );
}
