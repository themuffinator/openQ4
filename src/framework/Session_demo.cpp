/*
===========================================================================

openQ4 demo library and format-neutral playback controls.

===========================================================================
*/

#include "Session_local.h"
#include "DemoFile.h"
#include "async/AsyncNetwork.h"
#include "../ui/ListGUILocal.h"

namespace {

static const char *DEMO_FILTER_ALL = "all";
static const char *DEMO_FILTER_MVD = "mvd";
static const char *DEMO_FILTER_RENDER = "render";
static const char *DEMO_FILTER_LEGACY = "legacy";
static const char *DEMO_FILTER_INCOMPLETE = "incomplete";

static idCVar demo_uiSeek(
	"demo_uiSeek", "0",
	CVAR_SYSTEM | CVAR_GUI | CVAR_FLOAT,
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

static idStr DemoSanitizeText( const char *text, int maxLength = 96 ) {
	idStr source = text ? text : "";
	source.RemoveColors();

	idStr result;
	for ( int i = 0; i < source.Length() && result.Length() < maxLength; i++ ) {
		const unsigned char ch = static_cast<unsigned char>( source[i] );
		if ( ch == '\t' || ch == '\r' || ch == '\n' ) {
			result.Append( ' ' );
		} else if ( ch >= 32 && ch != 127 ) {
			result.Append( static_cast<char>( ch ) );
		}
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
	if ( result.Length() <= maxLength ) {
		return result;
	}
	if ( maxLength <= 3 ) {
		result.CapLength( maxLength );
		return result;
	}
	result.CapLength( maxLength - 3 );
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
	return Max( 0, session.currentDemoRenderView.time - session.renderDemoStartViewTime );
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

	if ( DemoEntryCanDelete( entry.path ) ) {
		entry.capabilities |= DEMO_CAP_DELETE;
	} else if ( !entry.details.IsEmpty() ) {
		entry.details += "\n";
		entry.details += DemoLocalized( "#str_41554" );
	}
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
		idAsyncNetwork::multiViewDemo.FollowNext();
	}
}

static void Session_DemoFreeRoam_f( const idCmdArgs &args ) {
	if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
		idAsyncNetwork::multiViewDemo.FreeRoam();
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
	cmdSystem->AddCommand( "demoFollow", Session_DemoFollow_f, CMD_FL_SYSTEM, "follows the next MVD player" );
	cmdSystem->AddCommand( "demoFreeRoam", Session_DemoFreeRoam_f, CMD_FL_SYSTEM, "enters MVD free-roam mode" );
	cmdSystem->AddCommand( "demoStop", Session_DemoStop_f, CMD_FL_SYSTEM, "stops unified demo playback" );
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

	if ( guiDemoList == NULL || guiDemoMenu == NULL ) {
		return;
	}

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
		view = idAsyncNetwork::multiViewDemo.GetFollowClient() >= 0
			? va( "%s %d", DemoLocalized( "#str_41560" ),
				idAsyncNetwork::multiViewDemo.GetFollowClient() + 1 )
			: DemoLocalized( "#str_41539" );
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
		view = DemoLocalized( "#str_41551" );
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
			if ( readDemo == NULL ) {
				OpenDemoMenu( true );
			}
		} else if ( entry.type == DEMO_LIBRARY_COMMAND ) {
			idStr commandName = entry.path;
			if ( idStr::Icmpn( commandName.c_str(), "demos/", 6 ) == 0 ) {
				commandName = commandName.c_str() + 6;
			}
			StartPlayingCmdDemo( commandName.c_str() );
			if ( cmdDemoFile == NULL ) {
				OpenDemoMenu( true );
			}
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

		const idStr saveDemoRoot = fileSystem->RelativePathToOSPath( "demos", "fs_savepath" );
		const idStr explicitPath = fileSystem->RelativePathToOSPath( entry.path.c_str(), "fs_savepath" );
		if ( explicitPath.Icmpn( saveDemoRoot.c_str(), saveDemoRoot.Length() ) != 0 ||
			 !DemoEntryCanDelete( entry.path ) ) {
			common->Warning( "Refusing to delete demo outside the save-path demo directory" );
			return true;
		}
		if ( fileSystem->RemoveExplicitFile( explicitPath.c_str() ) != 0 ) {
			common->Warning( "Could not delete demo '%s'", entry.path.c_str() );
		}
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
	if ( !idStr::Icmp( cmd, "demoFreeRoam" ) ) {
		if ( idAsyncNetwork::multiViewDemo.IsPlaying() ) {
			idAsyncNetwork::multiViewDemo.FreeRoam();
		}
		return true;
	}
	if ( !idStr::Icmp( cmd, "demoStop" ) ) {
		StopDemoPlayback();
		return true;
	}
	}
	return handled;
}
