/*
===========================================================================

openQ4 in-game updater
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#include "Updater.h"

#ifdef ID_DEDICATED

void Updater_Init( void ) {}
void Updater_Frame( void ) {}
void Updater_Check( bool ) {}

#else

#include "async/AsyncNetwork.h"

#include "../idlib/CryptoHash.h"
#include "../sys/sys_update.h"
#include "../updater/UpdateCore.h"
#include "../updater/UpdatePlan.h"
#include "../updater/UpdateSigningKey.h"
#include "openq4_paks_generated.h"
#include "openq4_build_id_generated.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

/*
===============================================================================

	How an update travels

	1. A check (the Updates button, the updateCheck command, or once at
	   startup in release builds) fetches the latest release's signed
	   manifest on a worker thread.
	2. On the title screen the player is told what it found. A newer release
	   can be installed in place when this is a release build, the manifest's
	   signature verifies against the compiled key, the release has an update
	   archive for this platform, nothing else manages the files (macOS app
	   bundle, AppImage, Flatpak, Snap, Android) and the install folder is
	   writable. Otherwise the player is offered the release page.
	3. Installing downloads the update archive and any bulk pack this build
	   does not have, behind the download progress box, checks each against
	   the signed SHA-256, unpacks it into <install>/.update/stage/ and writes
	   the plan.
	4. The game starts openQ4-updater from .update/ and quits; the helper
	   swaps the files, records the result and starts the game again, which
	   reports it and removes .update/.

===============================================================================
*/

namespace fs = std::filesystem;
namespace oqu = openq4::update;

static idCVar com_updateCheck( "com_updateCheck", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"check the openQ4 releases for a newer version when the game starts (release builds only)" );
static idCVar com_updateSkipVersion( "com_updateSkipVersion", "", CVAR_SYSTEM | CVAR_ARCHIVE,
	"a release you declined at startup; the startup check stays quiet about it (the Updates button still offers it)" );

namespace {

const char *Text( const char *id ) {
	return common->GetLanguageDict()->GetString( id );
}

const char *UpdatesTitle( void ) {
	return Text( "#str_104837" );
}

bool IsReleaseBuild( void ) {
	return idStr::Cmp( OPENQ4_VERSION_TRACK, "stable" ) == 0 && OPENQ4_VERSION_GIT_DIRTY == 0;
}

const char *ArchName( void ) {
#if defined( _M_X64 ) || defined( __x86_64__ )
	return "x64";
#elif defined( _M_ARM64 ) || defined( __aarch64__ )
	return "arm64";
#else
	return "unknown";
#endif
}

std::string PlatformKey( void ) {
#if defined( _WIN32 )
	return std::string( "windows-" ) + ArchName();
#elif defined( __ANDROID__ )
	return std::string( "android-" ) + ArchName();
#elif defined( __APPLE__ )
	return std::string( "macos-" ) + ArchName();
#else
	return std::string( "linux-" ) + ArchName();
#endif
}

std::string HelperName( void ) {
#if defined( _WIN32 )
	return std::string( "openQ4-updater_" ) + ArchName() + ".exe";
#else
	return std::string( "openQ4-updater_" ) + ArchName();
#endif
}

std::string SizeText( std::uint64_t bytes ) {
	idStr text;
	text.BestUnit( "%.1f", static_cast<float>( bytes ), MEASURE_SIZE );
	return text.c_str();
}

// The bulk pack this build was made with, from the generated pack table.
const char *CompiledPackMD5( const std::string &name ) {
	struct packRow_t {
		const char *	name;
		const char *	md5;
		long long		sizeBytes;
		int				fileCount;
		bool			clientOnly;
		bool			bulk;
		const char *	contentsString;
		const char *	contents;
	};
	static const packRow_t rows[] = { OPENQ4_PACK_TABLE_ENTRIES };
	for ( const packRow_t &row : rows ) {
		if ( name == row.name ) {
			return row.md5;
		}
	}
	return nullptr;
}

fs::path InstallDirectory( void ) {
	return Sys_UpdateExecutablePath().parent_path();
}

void ClearUpdateDirectory( const fs::path &updateDir ) {
	std::error_code ec;
	for ( const char *part : { oqu::STAGE_DIRECTORY, oqu::BACKUP_DIRECTORY, oqu::DOWNLOAD_DIRECTORY } ) {
		fs::remove_all( updateDir / part, ec );
	}
	for ( const char *file : { oqu::PLAN_FILE, oqu::RESULT_FILE } ) {
		fs::remove( updateDir / file, ec );
	}
	// The helper copy may still be exiting; it goes next time if not now.
	fs::remove_all( updateDir, ec );
}

/*
===============================================================================

	Check

===============================================================================
*/

struct checkResult_t {
	std::atomic<bool>	done { false };
	bool				fromMenu = false;	// main thread only
	bool				reached = false;	// a well-formed manifest arrived
	bool				signatureValid = false;
	std::string			error;
	oqu::Manifest		manifest;
};

bool FetchText( const std::string &url, std::size_t maxBytes, std::string &body, std::string &error ) {
	body.clear();
	const sysHttpsResult_t result = Sys_UpdateHttpsGet( url, maxBytes,
		[&body]( const void *data, std::size_t bytes ) {
			body.append( static_cast<const char *>( data ), bytes );
			return true;
		}, sysHttpsProgressFn() );
	if ( !result.ok ) {
		error = result.error;
	}
	return result.ok;
}

// Worker thread: touches only its own result.
void RunCheck( std::shared_ptr<checkResult_t> check ) {
	std::string reason;
	if ( !Sys_UpdateHttpsAvailable( reason ) ) {
		check->error = reason;
		check->done = true;
		return;
	}
	std::string body;
	if ( !FetchText( oqu::LatestManifestURL( PROJECT_REPO, oqu::MANIFEST_ASSET ), oqu::MAX_MANIFEST_BYTES, body, check->error ) ||
			!oqu::ParseManifest( body, check->manifest, check->error ) ) {
		check->done = true;
		return;
	}
	check->reached = true;

	std::uint8_t publicKey[ 32 ];
	if ( oqu::DecodePublicKey( OPENQ4_UPDATE_SIGNING_PUBLIC_KEY, publicKey ) ) {
		// Read the signature from the manifest's own release, so a release
		// published between the two requests cannot pair mismatched files.
		std::string signature;
		std::string signatureError;
		if ( FetchText( oqu::ReleaseAssetURL( PROJECT_REPO, check->manifest.tag, oqu::SIGNATURE_ASSET ),
				oqu::MAX_SIGNATURE_BYTES, signature, signatureError ) ) {
			check->signatureValid = oqu::VerifyManifestSignature( body, signature, publicKey );
			if ( !check->signatureValid ) {
				check->error = "the manifest signature does not verify";
			}
		} else {
			check->error = "no manifest signature: " + signatureError;
		}
	} else {
		check->error = "this build has no update signing key";
	}
	check->done = true;
}

/*
===============================================================================

	Install

===============================================================================
*/

struct installJob_t {
	oqu::StageRequest		request;
	oqu::UpdatePlan			plan;		// launch details filled by the main thread
	backgroundDownload_t *	progress = nullptr;
	oqu::StageResult		result;
};

// Worker thread: stages the release; touches only its job and the progress box's counters.
void RunInstall( installJob_t *job ) {
	backgroundDownload_t &progress = *job->progress;
	const std::uint64_t limit = static_cast<std::uint64_t>( ( std::numeric_limits<int>::max )() );
	const std::string tag = job->request.manifest.tag;
	job->result = oqu::StageRelease( job->request, job->plan,
		[&tag, &progress]( const std::string &asset, std::uint64_t maxBytes,
				const std::function<bool( const void *, std::size_t )> &onData, bool &cancelled, std::string &error ) {
			const sysHttpsResult_t fetched = Sys_UpdateHttpsGet( oqu::ReleaseAssetURL( PROJECT_REPO, tag, asset ), maxBytes,
				onData, [&progress]( std::uint64_t, std::uint64_t ) { return progress.url.status != DL_ABORTING; } );
			cancelled = fetched.cancelled;
			error = fetched.error;
			return fetched.ok;
		},
		[&progress, limit]( std::uint64_t done, std::uint64_t total ) {
			const std::uint64_t shownTotal = ( std::min )( total, limit );
			progress.url.dltotal = static_cast<int>( shownTotal );
			progress.url.dlnow = static_cast<int>( ( std::min )( done, shownTotal ) );
			return progress.url.status != DL_ABORTING;
		},
		[]( const fs::path &archive, const fs::path &destination, std::string &error ) {
			return Sys_UpdateExtractTar( archive, destination, error );
		} );
	progress.url.status = job->result.ok ? DL_DONE : DL_FAILED;
	progress.completed = true;
}

/*
===============================================================================

	Main thread

===============================================================================
*/

bool								presenting = false;	// a box of ours is up; nested frames must not start another
std::shared_ptr<checkResult_t>		pendingCheck;		// running on a worker
std::shared_ptr<checkResult_t>		unpresentedCheck;	// finished, waiting for the title
bool								installedReportDone = false;
bool								startupCheckDone = false;

// The title screen, with no map, no network session and no intro running.
bool TitleIsIdle( void ) {
	return common->IsInitialized() && session->IsGUIActive() && !session->IsMainMenuIntroPlaying() &&
		!session->IsLoadingSaveGame() && !idAsyncNetwork::IsActive() && session->GetCurrentMapName()[ 0 ] == '\0';
}

// NULL when another box is already up; "" for no; "yes" for yes.
const char *AskYesNo( const char *message ) {
	return session->MessageBox( MSG_YESNO, message, UpdatesTitle(), true, "yes", "" );
}

bool Tell( const char *message ) {
	return session->MessageBox( MSG_OK, message, UpdatesTitle(), true, "ok" ) != NULL;
}

void OpenReleasePage( const oqu::Manifest *manifest ) {
	if ( manifest != nullptr && !manifest->tag.empty() ) {
		sys->OpenURL( oqu::ReleasePageURL( PROJECT_REPO, manifest->tag ).c_str(), false );
	} else {
		sys->OpenURL( PROJECT_RELEASES_URL, false );
	}
}

// Empty when an in-place install is possible; otherwise why not, for the log.
std::string InstallBlocker( const checkResult_t &check, const fs::path &install ) {
	if ( !IsReleaseBuild() ) {
		return "this is a development build";
	}
	if ( !check.signatureValid ) {
		return check.error.empty() ? "the manifest is not signed" : check.error;
	}
	if ( const char *managedBy = Sys_UpdateManagedBy() ) {
		return std::string( "this install is managed by its " ) + managedBy;
	}
	if ( check.manifest.packages.find( PlatformKey() ) == check.manifest.packages.end() ) {
		return "the release has no update archive for " + PlatformKey();
	}
	if ( install.empty() ) {
		return "the install folder is unknown";
	}
	std::error_code ec;
	const fs::path updateDir = install / oqu::UPDATE_DIRECTORY;
	fs::create_directories( updateDir, ec );
	const fs::path probe = updateDir / "write-test";
	bool writable = false;
	{
		std::ofstream stream( probe, std::ios::binary | std::ios::trunc );
		writable = stream && ( stream << "openQ4" );
	}
	fs::remove( probe, ec );
	fs::remove( updateDir, ec );	// only if empty; an install fills it again
	return writable ? std::string() : std::string( "the install folder is not writable" );
}

// The download or staging failed and openQ4 is unchanged.
void OfferReleasePageAfterFailure( const oqu::Manifest &manifest ) {
	const char *answer = AskYesNo( va( Text( "#str_232507" ), PROJECT_VERSION ) );
	if ( answer != NULL && answer[ 0 ] != '\0' ) {
		OpenReleasePage( &manifest );
	}
}

void StartHelperAndQuit( const oqu::Manifest &manifest, const fs::path &updateDir ) {
	std::error_code ec;
	// Run the new release's helper, from outside the files it replaces.
	const fs::path helper = updateDir / HelperName();
	fs::copy_file( updateDir / oqu::STAGE_DIRECTORY / HelperName(), helper, fs::copy_options::overwrite_existing, ec );
	if ( ec ) {
		common->Warning( "Update: cannot prepare the updater: %s", ec.message().c_str() );
		ClearUpdateDirectory( updateDir );
		OfferReleasePageAfterFailure( manifest );
		return;
	}
	Sys_UpdateMakeExecutable( helper );
	std::string error;
	const std::vector<std::string> arguments = {
		"--apply", oqu::PathToUtf8( updateDir ),
		"--wait-pid", std::to_string( Sys_UpdateProcessId() )
	};
	if ( !Sys_UpdateStartDetached( helper, arguments, error ) ) {
		common->Warning( "Update: %s", error.c_str() );
		ClearUpdateDirectory( updateDir );
		OfferReleasePageAfterFailure( manifest );
		return;
	}
	common->Printf( "Update: openQ4 %s is staged; closing so openQ4-updater can install it\n", manifest.versionText.c_str() );
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
}

bool PackIsCurrent( const fs::path &install, const oqu::PackEntry &pack ) {
	std::error_code ec;
	const fs::path local = install / "baseoq4" / pack.name;
	const char *compiled = CompiledPackMD5( pack.name );
	return compiled != nullptr && pack.md5 == compiled && fs::is_regular_file( local, ec ) &&
		fs::file_size( local, ec ) == pack.size;
}

oqu::StageRequest MakeStageRequest( const oqu::Manifest &manifest, const fs::path &install ) {
	oqu::StageRequest request;
	request.manifest = manifest;
	request.platformKey = PlatformKey();
	request.install = install;
	// The update must carry the program the helper starts afterwards, and the
	// helper itself.
	request.requiredFiles = { oqu::PathToUtf8( Sys_UpdateExecutablePath().filename() ), HelperName() };
	std::sort( request.requiredFiles.begin(), request.requiredFiles.end() );
	request.packs = oqu::SelectPacks( manifest, [&install]( const oqu::PackEntry &pack ) {
		return PackIsCurrent( install, pack );
	} );
	return request;
}

void Install( const checkResult_t &check, const fs::path &install ) {
	installJob_t job;
	job.request = MakeStageRequest( check.manifest, install );
	const oqu::Manifest &manifest = job.request.manifest;
	const fs::path updateDir = install / oqu::UPDATE_DIRECTORY;

	oqu::UpdatePlan &plan = job.plan;
	plan.version = manifest.versionText;
	plan.fromVersion = PROJECT_VERSION;
	plan.installDir = oqu::PathToUtf8( install );
	plan.relaunch = oqu::PathToUtf8( Sys_UpdateExecutablePath() );
	plan.workingDir = oqu::PathToUtf8( Sys_UpdateWorkingDirectory() );
	Sys_UpdateLaunchArguments( plan.arguments, plan.commandTail );
	plan.restartFailedMessage = Text( "#str_232512" );

	backgroundDownload_t progress;
	progress.completed = false;
	progress.url.status = DL_INPROGRESS;
	progress.url.dltotal = 0;
	progress.url.dlnow = 0;
	job.progress = &progress;

	common->Printf( "Update: downloading openQ4 %s (%s)\n", manifest.versionText.c_str(),
		SizeText( oqu::StageBytes( job.request ) ).c_str() );
	std::thread worker( RunInstall, &job );
	session->DownloadProgressBox( &progress, va( Text( "#str_232506" ), manifest.versionText.c_str() ) );
	worker.join();

	if ( job.result.cancelled ) {
		common->Printf( "Update: cancelled\n" );
		ClearUpdateDirectory( updateDir );
		Tell( Text( "#str_232508" ) );
		return;
	}
	if ( !job.result.ok ) {
		common->Warning( "Update: %s", job.result.error.c_str() );
		ClearUpdateDirectory( updateDir );
		OfferReleasePageAfterFailure( manifest );
		return;
	}
	Tell( va( Text( "#str_232509" ), manifest.versionText.c_str() ) );
	StartHelperAndQuit( manifest, updateDir );
}

// Returns false when the box could not be shown yet.
bool Present( checkResult_t &check ) {
	oqu::Version current;
	oqu::ParseVersion( OPENQ4_VERSION_BASE, current );

	if ( !check.reached ) {
		common->Printf( "Update: could not check for updates: %s\n", check.error.c_str() );
		if ( !check.fromMenu ) {
			return true;
		}
		const char *answer = AskYesNo( Text( "#str_232505" ) );
		if ( answer == NULL ) {
			return false;
		}
		if ( answer[ 0 ] != '\0' ) {
			OpenReleasePage( nullptr );
		}
		return true;
	}

	const oqu::Manifest &manifest = check.manifest;
	const int order = oqu::CompareVersions( manifest.version, current );
	if ( order <= 0 ) {
		common->Printf( "Update: openQ4 %s is current (latest release %s)\n", PROJECT_VERSION, manifest.versionText.c_str() );
		if ( !check.fromMenu ) {
			return true;
		}
		if ( IsReleaseBuild() ) {
			return Tell( va( Text( "#str_232501" ), PROJECT_VERSION ) );
		}
		return Tell( va( Text( "#str_232502" ), PROJECT_VERSION, manifest.versionText.c_str() ) );
	}

	if ( !check.fromMenu && idStr::Cmp( com_updateSkipVersion.GetString(), manifest.versionText.c_str() ) == 0 ) {
		common->Printf( "Update: openQ4 %s is available; you declined it at startup before\n", manifest.versionText.c_str() );
		return true;
	}

	const fs::path install = InstallDirectory();
	const std::string blocker = InstallBlocker( check, install );
	if ( !blocker.empty() ) {
		common->Printf( "Update: openQ4 %s is available; installing it here is not possible (%s)\n",
			manifest.versionText.c_str(), blocker.c_str() );
		const char *answer = AskYesNo( va( Text( "#str_232504" ), manifest.versionText.c_str(), PROJECT_VERSION ) );
		if ( answer == NULL ) {
			return false;
		}
		if ( answer[ 0 ] != '\0' ) {
			OpenReleasePage( &manifest );
		} else if ( !check.fromMenu ) {
			com_updateSkipVersion.SetString( manifest.versionText.c_str() );
		}
		return true;
	}

	const std::uint64_t downloadBytes = oqu::StageBytes( MakeStageRequest( manifest, install ) );
	const char *answer = AskYesNo( va( Text( "#str_232503" ), manifest.versionText.c_str(), PROJECT_VERSION,
		SizeText( downloadBytes ).c_str() ) );
	if ( answer == NULL ) {
		return false;
	}
	if ( answer[ 0 ] == '\0' ) {
		if ( !check.fromMenu ) {
			com_updateSkipVersion.SetString( manifest.versionText.c_str() );
		}
		return true;
	}
	Install( check, install );
	return true;
}

// After openQ4-updater ran: tidy up, then say what happened. The folder goes
// first, so quitting from the message does not repeat it next time.
bool ReportInstalledUpdate( void ) {
	static bool read = false;
	static std::unique_ptr<oqu::UpdateResult> pending;
	if ( !read ) {
		read = true;
		const fs::path updateDir = InstallDirectory() / oqu::UPDATE_DIRECTORY;
		std::error_code ec;
		if ( !fs::is_directory( updateDir, ec ) ) {
			return true;
		}
		std::string text;
		std::string error;
		auto result = std::make_unique<oqu::UpdateResult>();
		if ( oqu::ReadSmallFile( updateDir / oqu::RESULT_FILE, 64 * 1024, text ) && oqu::ParseResult( text, *result, error ) ) {
			if ( result->succeeded ) {
				common->Printf( "Update: openQ4 was updated from %s to %s\n", result->fromVersion.c_str(), result->version.c_str() );
			} else {
				common->Warning( "Update: installing openQ4 %s failed: %s", result->version.c_str(), result->detail.c_str() );
			}
			pending = std::move( result );
		} else {
			// A download that never finished, or a plan the helper never ran.
			common->Printf( "Update: removing an unfinished update\n" );
		}
		ClearUpdateDirectory( updateDir );
	}
	if ( !pending ) {
		return true;
	}
	if ( pending->succeeded ) {
		if ( !Tell( va( Text( "#str_232510" ), pending->version.c_str() ) ) ) {
			return false;
		}
	} else {
		const char *answer = AskYesNo( va( Text( "#str_232511" ), pending->version.c_str(), PROJECT_VERSION ) );
		if ( answer == NULL ) {
			return false;
		}
		if ( answer[ 0 ] != '\0' ) {
			sys->OpenURL( oqu::ReleasePageURL( PROJECT_REPO, "v" + pending->version ).c_str(), false );
		}
	}
	pending.reset();
	return true;
}

void UpdateCheck_f( const idCmdArgs & ) {
	Updater_Check( true );
}

} // namespace

void Updater_Init( void ) {
	cmdSystem->AddCommand( "updateCheck", UpdateCheck_f, CMD_FL_SYSTEM, "checks the openQ4 releases for a newer version" );
}

void Updater_Check( bool fromMenu ) {
	if ( pendingCheck ) {
		pendingCheck->fromMenu = pendingCheck->fromMenu || fromMenu;
		return;
	}
	unpresentedCheck.reset();
	pendingCheck = std::make_shared<checkResult_t>();
	pendingCheck->fromMenu = fromMenu;
	common->Printf( "Update: checking %s\n", PROJECT_RELEASES_URL );
	std::thread( RunCheck, pendingCheck ).detach();
}

void Updater_Frame( void ) {
	if ( presenting || !TitleIsIdle() ) {
		return;
	}
	if ( pendingCheck && pendingCheck->done ) {
		unpresentedCheck = std::move( pendingCheck );
	}
	presenting = true;
	if ( !installedReportDone ) {
		installedReportDone = ReportInstalledUpdate();
	} else if ( !startupCheckDone ) {
		startupCheckDone = true;
		if ( com_updateCheck.GetBool() && IsReleaseBuild() && !unpresentedCheck ) {
			Updater_Check( false );
		}
	} else if ( unpresentedCheck ) {
		const std::shared_ptr<checkResult_t> check = unpresentedCheck;
		if ( Present( *check ) && unpresentedCheck == check ) {
			unpresentedCheck.reset();
		}
	}
	presenting = false;
}

#endif /* !ID_DEDICATED */
