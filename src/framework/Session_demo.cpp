/*
===========================================================================

openQ4 demo library and format-neutral playback controls.

===========================================================================
*/

#include "Session_local.h"
#include "DemoFile.h"
#include "NativeInputPublications.h"
#include "async/AsyncNetwork.h"
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

static const char *DemoLocalized( const char *key ) {
	return common->GetLanguageDict()->GetString( key );
}

static bool DemoPathEndsWith( const idStr &path, const char *suffix ) {
	const int suffixLength = static_cast<int>( strlen( suffix ) );
	return path.Length() >= suffixLength &&
		idStr::Icmp( path.c_str() + path.Length() - suffixLength, suffix ) == 0;
}

static unsigned int DemoReadLittleUInt( const byte *data ) {
	unsigned int value = 0;
	memcpy( &value, data, sizeof( value ) );
	return LittleLong( value );
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
	if ( session.readDemo == NULL ) {
		return 0;
	}
	if ( session.renderDemoFrameClock ) {
		// A campaign demo's levels restart their game clocks; its frames do not.
		return session.RenderDemoFramesToMS( Max( 0, session.numDemoFrames - 1 ) );
	}
	return Max( 0, session.currentDemoRenderView.time - session.renderDemoStartViewTime );
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
};

static bool DemoReadRenderSessionInfo( const char *path, demoRenderSessionInfo_t &info ) {
	idDemoFile probe;
	if ( !probe.OpenForReading( path, false, true ) ) {
		return false;
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

// "startFrame reason map entityFilter", as StopRecordingRenderDemo writes it.
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
	return segment.startFrame >= 0;
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

	entry.sizeBytes = fileSystem->ReadFile( entry.path.c_str(), NULL, &entry.timestamp );
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
			if ( info.compatible ) {
				entry.capabilities =
					DEMO_CAP_PLAY | DEMO_CAP_PAUSE | DEMO_CAP_RATE |
					DEMO_CAP_SEEK | DEMO_CAP_STEP | DEMO_CAP_FREE_ROAM |
					DEMO_CAP_FOLLOW | DEMO_CAP_FULL_WORLD;
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
		int streamVersion = 1;
		idDemoFile probe;
		if ( probe.OpenForReading( entry.path.c_str(), false, true ) ) {
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

		if ( ambiguousWrapper && !decodedOpenQ4Stream ) {
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
				const char *recordedGame = info.header.GetString( "game", "" );
				const idStr campaign = DemoCampaignName( recordedGame );
				if ( !idStr::Icmp( info.header.GetString( "mode" ), "singleplayer" ) ) {
					entry.format = DemoLocalized( "#str_41576" );
				}
				entry.gameType = info.header.GetString( "mode" );
				if ( !info.firstMap.IsEmpty() ) {
					entry.mapName = DemoSanitizeText( Session_LevelDisplayName( info.firstMap, info.firstEntityFilter ) );
				}
				const int levels = info.summarized ? info.summary.GetInt( "segments" ) : 0;
				if ( info.summarized ) {
					const int frames = info.summary.GetInt( "frames" );
					entry.durationMS = frames > 0
						? Session_RenderDemoFramesToMS( frames - 1, info.summary.GetInt( "userCmdHz" ) )
						: -1;
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
				} else if ( recordedGame[0] != '\0' && idStr::Icmp( recordedGame, fileSystem->GetActiveGameDir() ) ) {
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
	} else if ( DemoPathEndsWith( entry.path, ".netdemo" ) ) {
		byte prefix[20];
		int prefixBytes = 0;
		DemoReadPrefix( entry.path, prefix, sizeof( prefix ), prefixBytes );
		entry.type = DEMO_LIBRARY_LEGACY_NET;
		entry.format = DemoLocalized( "#str_41543" );
		entry.status = DemoLocalized( "#str_41547" );
		entry.details = DemoLocalized( "#str_41547" );
		if ( prefixBytes >= 4 && memcmp( prefix, "NDMO", 4 ) == 0 && prefixBytes >= 12 ) {
			const int version = static_cast<int>( DemoReadLittleUInt( prefix + 4 ) );
			const int protocol = static_cast<int>( DemoReadLittleUInt( prefix + 8 ) );
			entry.format = va( "NDMO v%d / %d", version, protocol );
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
	sessLocal.StepDemo();
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
	cmdSystem->AddCommand( "demoStep", Session_DemoStep_f, CMD_FL_SYSTEM, "steps a paused demo by one frame" );
	cmdSystem->AddCommand( "demoFollow", Session_DemoFollow_f, CMD_FL_SYSTEM, "follows an MVD player: demoFollow [next|prev|free|<slot>|<name>]" );
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
		const int followClient = idAsyncNetwork::multiViewDemo.GetFollowClient();
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
		renderDemoFrameAccumulator = 0.0;
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
		renderDemoFrameAccumulator = 0.0;
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
	renderDemoSeekMuteOwned = false;
	renderDemoSeeking = false;
	renderDemoSeekRestartPending = false;
	renderDemoSeekRestartPresentationPending = false;
	renderDemoSeekWorkDeferred = false;
	renderDemoSeekTargetMS = 0;
	renderDemoSeekDeadlineMS = 0;
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
		renderDemoFrameAccumulator = 0.0;
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

	if ( target < DemoCurrentRenderTimeMS( *this ) ) {
		const bool restoreMute = renderDemoSeekMuteOwned
			? renderDemoSeekRestoreMute : soundSystem->IsMuted();
		FinishRenderDemoSeek();
		renderDemoSeekRestoreMute = restoreMute;
		renderDemoSeekRestartPending = true;
		renderDemoSeekRestartPresentationPending = true;
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

void idSessionLocal::StepDemo() {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		if ( !idAsyncNetwork::multiViewDemo.IsSeeking() ) {
			idAsyncNetwork::multiViewDemo.StepFrames( 1 );
		}
	} else if ( readDemo != NULL && !renderDemoSeeking &&
			   !timeDemo && !aviCaptureMode ) {
		SetDemoPaused( true );
		renderDemoStepPending = true;
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
	if ( entry.type == DEMO_LIBRARY_MVD ) {
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
		StepDemo();
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
	const char *recordedGame = info.header.GetString( "game", "" );
	if ( recordedGame[0] == '\0' || !idStr::Icmp( recordedGame, fileSystem->GetActiveGameDir() ) ) {
		return true;
	}
	reason = va( DemoLocalized( "#str_41578" ), DemoCampaignName( recordedGame ).c_str() );
	return false;
}

/*
================
idSessionLocal::RenderDemoFramesToMS
================
*/
int idSessionLocal::RenderDemoFramesToMS( int frames ) const {
	return Session_RenderDemoFramesToMS( frames, renderDemoUserCmdHz > 0 ? renderDemoUserCmdHz : common->GetUserCmdHz() );
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
	renderDemoRecordFile->WriteInt( DS_SESSION );
	renderDemoRecordFile->WriteInt( DSC_SEGMENT );
	renderDemoRecordFile->WriteDict( record );
	// Nothing in the segment refers to a string written before it, so its
	// table stays bounded across a long campaign and playback can begin a
	// segment from scratch.
	renderDemoRecordFile->ResetHashStrings();

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
	common->Printf( "RENDER_DEMO_RECORDING state=%s path=%s frames=%d seconds=%d segments=%d map=%s elapsed=%d\n",
		states[state], renderDemoRecordPath.IsEmpty() ? "-" : renderDemoRecordPath.c_str(),
		renderDemoRecordFrames, Session_RenderDemoFramesToMS( renderDemoRecordFrames, common->GetUserCmdHz() ) / 1000,
		renderDemoRecordSegments.Num(), segment != NULL ? segment->map.c_str() : "-",
		IsRenderDemoRecording() ? ( Sys_Milliseconds() - renderDemoRecordStartMsec ) / 1000 : 0 );
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

	idDict summary;
	if ( !idDemoFile::ReadTrailer( activeRenderDemoName.c_str(), summary ) ||
		 summary.GetInt( "version" ) < OPENQ4_RENDERDEMO_SESSION_VERSION ) {
		return;
	}
	renderDemoUserCmdHz = idMath::ClampInt( 0, 1000, summary.GetInt( "userCmdHz" ) );
	renderDemoPlayHeader.SetInt( "frames", Max( 0, summary.GetInt( "frames" ) ) );
	const int count = idMath::ClampInt( 0, 4096, summary.GetInt( "segments" ) );
	for ( int i = 0; i < count; i++ ) {
		renderDemoSegment_t segment;
		if ( !DemoParseSegmentSummary( summary.GetString( va( "segment%d", i ) ), segment ) ) {
			// A malformed list is only a convenience lost; segments are still met.
			renderDemoPlaySegments.Clear();
			break;
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
		readDemo->ResetHashStrings();
		renderDemoSegment_t segment;
		segment.startFrame = numDemoFrames;
		segment.map = dict.GetString( "map" );
		segment.entityFilter = dict.GetString( "entityFilter" );
		segment.reason = dict.GetString( "reason", "level" );
		if ( segment.map.IsEmpty() || dict.GetInt( "frame", "-1" ) != numDemoFrames ) {
			// Its frame count must agree with the frames played: a segment
			// out of step means the stream lost or gained frames.
			return false;
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
		common->Printf( "RENDER_DEMO_SEGMENT play index=%d frame=%d reason=%s map=%s\n",
			index, segment.startFrame, segment.reason.c_str(), segment.map.c_str() );
		return true;
	}
	case DSC_SEGMENT_READY:
		CloseRenderDemoLevelLoad();
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
			const int start = RenderDemoFramesToMS( renderDemoPlaySegments[i].startFrame );
			if ( start > reference ) {
				target = start;
				break;
			}
		}
	} else {
		static const int RESTART_LEVEL_WINDOW_MS = 2000;
		for ( int i = renderDemoPlaySegments.Num() - 1; i >= 0; i-- ) {
			const int start = RenderDemoFramesToMS( renderDemoPlaySegments[i].startFrame );
			if ( start < reference - RESTART_LEVEL_WINDOW_MS ) {
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
	const int known = renderDemoPlayHeader.GetInt( "frames" ) > 0 ? renderDemoPlaySegments.Num() : 0;
	if ( known > 0 ) {
		return va( "%s %d/%d: %s", DemoLocalized( "#str_41583" ), renderDemoPlaySegment + 1, known, level.c_str() );
	}
	return va( "%s %d: %s", DemoLocalized( "#str_41583" ), renderDemoPlaySegment + 1, level.c_str() );
}
