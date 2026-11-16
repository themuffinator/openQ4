/*
===========================================================================

openQ4 updater helper
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

/*
===============================================================================

	openQ4-updater_<arch>: applies an update the game has already downloaded,
	verified and unpacked, then starts the game again.

		openQ4-updater_<arch> --apply <install>/.update [--wait-pid <pid>]

	The game starts a copy of this program from .update/ and quits. Windows
	cannot replace a running executable or a loaded library, so the files are
	swapped only once the game has exited. Each replaced file is moved to
	.update/backup/ before its new copy moves in; if any step fails, every
	step taken so far is undone in reverse and the install is left as it was.
	The outcome goes to .update/result.txt, which the restarted game reports
	in the player's language. This program shows nothing itself, except the
	game-supplied message when the game cannot be started again.

	It has no network access and links nothing from the engine.

===============================================================================
*/

#include "UpdatePlan.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined( _WIN32 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace openq4::update;

namespace {

static constexpr int WAIT_FOR_GAME_SECONDS = 120;
static constexpr int MOVE_RETRY_MILLISECONDS = 10000;

fs::path FromUtf8( const std::string &text ) {
	return fs::path( std::u8string( reinterpret_cast<const char8_t *>( text.data() ), text.size() ) );
}

std::string ToUtf8( const fs::path &path ) {
	const std::u8string text = path.u8string();
	return std::string( reinterpret_cast<const char *>( text.data() ), text.size() );
}

class Log {
public:
	void Open( const fs::path &path ) {
		stream.open( path, std::ios::binary | std::ios::app );
	}
	void Line( const std::string &text ) {
		char stamp[ 32 ] = "";
		const std::time_t now = std::time( nullptr );
		std::tm local {};
#if defined( _WIN32 )
		localtime_s( &local, &now );
#else
		localtime_r( &now, &local );
#endif
		std::strftime( stamp, sizeof( stamp ), "%Y-%m-%d %H:%M:%S", &local );
		if ( stream ) {
			stream << stamp << "  " << text << "\n";
			stream.flush();
		}
	}
private:
	std::ofstream stream;
};

Log logFile;

bool ReadText( const fs::path &path, std::string &text ) {
	std::ifstream stream( path, std::ios::binary );
	if ( !stream ) {
		return false;
	}
	text.assign( std::istreambuf_iterator<char>( stream ), std::istreambuf_iterator<char>() );
	return !stream.bad() && text.size() < 4 * 1024 * 1024;
}

bool WriteText( const fs::path &path, const std::string &text ) {
	const fs::path temporary = fs::path( path ).concat( ".partial" );
	{
		std::ofstream stream( temporary, std::ios::binary | std::ios::trunc );
		if ( !stream || !stream.write( text.data(), static_cast<std::streamsize>( text.size() ) ) ) {
			return false;
		}
	}
	std::error_code ec;
	fs::rename( temporary, path, ec );
	return !ec;
}

// Waits for the game to exit. True once it has (or was never running).
bool WaitForProcess( unsigned long long pid ) {
	if ( pid == 0 ) {
		return true;
	}
#if defined( _WIN32 )
	HANDLE process = OpenProcess( SYNCHRONIZE, FALSE, static_cast<DWORD>( pid ) );
	if ( process == nullptr ) {
		return true;	// already gone
	}
	const DWORD waited = WaitForSingleObject( process, WAIT_FOR_GAME_SECONDS * 1000 );
	CloseHandle( process );
	return waited == WAIT_OBJECT_0;
#else
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( WAIT_FOR_GAME_SECONDS );
	while ( std::chrono::steady_clock::now() < deadline ) {
		if ( kill( static_cast<pid_t>( pid ), 0 ) != 0 && errno == ESRCH ) {
			return true;
		}
		std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	}
	return false;
#endif
}

// Antivirus scanners and indexers can hold a file just written for a moment,
// so a refused move is retried for a while before it counts as a failure.
bool MoveWithRetry( const fs::path &from, const fs::path &to, std::string &error ) {
	std::error_code ec;
	fs::create_directories( to.parent_path(), ec );
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( MOVE_RETRY_MILLISECONDS );
	for ( ;; ) {
		fs::rename( from, to, ec );
		if ( !ec ) {
			return true;
		}
		if ( std::chrono::steady_clock::now() >= deadline ) {
			error = "cannot move " + ToUtf8( from ) + " to " + ToUtf8( to ) + ": " + ec.message();
			return false;
		}
		std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	}
}

struct Step {
	std::string	relative;
	bool		backedUp = false;	// the previous file now sits in backup/
	bool		installed = false;	// the staged file now sits in the install
};

void Undo( std::vector<Step> &steps, const fs::path &install, const fs::path &stage, const fs::path &backup ) {
	for ( auto step = steps.rbegin(); step != steps.rend(); ++step ) {
		const fs::path relative = FromUtf8( step->relative );
		std::string error;
		if ( step->installed && !MoveWithRetry( install / relative, stage / relative, error ) ) {
			logFile.Line( "rollback: " + error );
		}
		if ( step->backedUp && !MoveWithRetry( backup / relative, install / relative, error ) ) {
			logFile.Line( "rollback: " + error );
		}
	}
	steps.clear();
}

bool Apply( const UpdatePlan &plan, const fs::path &updateDir, std::string &error ) {
	const fs::path install = updateDir.parent_path();
	const fs::path stage = updateDir / STAGE_DIRECTORY;
	const fs::path backup = updateDir / BACKUP_DIRECTORY;
	std::error_code ec;

	for ( const std::string &file : plan.files ) {
		if ( !fs::is_regular_file( fs::symlink_status( stage / FromUtf8( file ), ec ) ) ) {
			error = "staged file is missing: " + file;
			return false;
		}
	}
	fs::remove_all( backup, ec );

	std::vector<Step> steps;
	steps.reserve( plan.files.size() + plan.removals.size() );
	for ( const std::string &file : plan.files ) {
		const fs::path relative = FromUtf8( file );
		Step step;
		step.relative = file;
		const fs::file_status existing = fs::symlink_status( install / relative, ec );
		if ( fs::exists( existing ) ) {
			if ( fs::is_directory( existing ) ) {
				error = "a folder is in the way of " + file;
				Undo( steps, install, stage, backup );
				return false;
			}
			if ( !MoveWithRetry( install / relative, backup / relative, error ) ) {
				Undo( steps, install, stage, backup );
				return false;
			}
			step.backedUp = true;
		}
		if ( !MoveWithRetry( stage / relative, install / relative, error ) ) {
			steps.push_back( step );
			Undo( steps, install, stage, backup );
			return false;
		}
		step.installed = true;
		steps.push_back( step );
	}
	for ( const std::string &file : plan.removals ) {
		const fs::path relative = FromUtf8( file );
		if ( !fs::is_regular_file( fs::symlink_status( install / relative, ec ) ) ) {
			continue;
		}
		Step step;
		step.relative = file;
		if ( !MoveWithRetry( install / relative, backup / relative, error ) ) {
			Undo( steps, install, stage, backup );
			return false;
		}
		step.backedUp = true;
		steps.push_back( step );
	}
	logFile.Line( "moved " + std::to_string( plan.files.size() ) + " files in, retired " + std::to_string( plan.removals.size() ) );
	return true;
}

#if defined( _WIN32 )
std::wstring Widen( const std::string &text ) {
	if ( text.empty() ) {
		return std::wstring();
	}
	const int length = MultiByteToWideChar( CP_UTF8, 0, text.data(), static_cast<int>( text.size() ), nullptr, 0 );
	std::wstring wide( static_cast<std::size_t>( length ), L'\0' );
	MultiByteToWideChar( CP_UTF8, 0, text.data(), static_cast<int>( text.size() ), wide.data(), length );
	return wide;
}
#endif

bool Relaunch( const UpdatePlan &plan, std::string &error ) {
	const fs::path executable = FromUtf8( plan.relaunch );
#if defined( _WIN32 )
	std::wstring commandLine = L"\"" + executable.wstring() + L"\"";
	if ( !plan.commandTail.empty() ) {
		commandLine += L" " + Widen( plan.commandTail );
	}
	const std::wstring workingDir = plan.workingDir.empty() ? executable.parent_path().wstring() : FromUtf8( plan.workingDir ).wstring();
	STARTUPINFOW startup {};
	startup.cb = sizeof( startup );
	PROCESS_INFORMATION process {};
	if ( !CreateProcessW( executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
			workingDir.c_str(), &startup, &process ) ) {
		error = "cannot start " + plan.relaunch + " (error " + std::to_string( GetLastError() ) + ")";
		return false;
	}
	CloseHandle( process.hThread );
	CloseHandle( process.hProcess );
	return true;
#else
	std::vector<std::string> arguments;
	arguments.push_back( plan.relaunch );
	arguments.insert( arguments.end(), plan.arguments.begin(), plan.arguments.end() );
	std::vector<char *> argv;
	for ( std::string &argument : arguments ) {
		argv.push_back( argument.data() );
	}
	argv.push_back( nullptr );
	const pid_t child = fork();
	if ( child < 0 ) {
		error = "cannot start " + plan.relaunch + ": fork failed";
		return false;
	}
	if ( child == 0 ) {
		const std::string workingDir = plan.workingDir.empty() ? ToUtf8( executable.parent_path() ) : plan.workingDir;
		if ( chdir( workingDir.c_str() ) != 0 && chdir( ToUtf8( executable.parent_path() ).c_str() ) != 0 ) {
			// Start from wherever we are; the game finds its files from its own path.
		}
		setsid();
		execv( plan.relaunch.c_str(), argv.data() );
		_exit( 127 );
	}
	return true;
#endif
}

void TellPlayer( const std::string &message ) {
	if ( message.empty() ) {
		return;
	}
#if defined( _WIN32 )
	MessageBoxW( nullptr, Widen( message ).c_str(), L"openQ4", MB_OK | MB_ICONWARNING );
#else
	std::fprintf( stderr, "%s\n", message.c_str() );
#endif
}

int Run( const std::vector<std::string> &arguments ) {
	fs::path updateDir;
	unsigned long long pid = 0;
	for ( std::size_t i = 1; i < arguments.size(); ++i ) {
		if ( arguments[ i ] == "--apply" && i + 1 < arguments.size() ) {
			updateDir = FromUtf8( arguments[ ++i ] );
		} else if ( arguments[ i ] == "--wait-pid" && i + 1 < arguments.size() ) {
			pid = std::strtoull( arguments[ ++i ].c_str(), nullptr, 10 );
		}
	}
	if ( updateDir.empty() ) {
		std::fprintf( stderr, "usage: openQ4-updater --apply <install>/.update [--wait-pid <pid>]\n" );
		return 2;
	}
	std::error_code ec;
	updateDir = fs::weakly_canonical( fs::absolute( updateDir, ec ), ec );
	if ( updateDir.filename() != UPDATE_DIRECTORY || !fs::is_directory( updateDir, ec ) ) {
		std::fprintf( stderr, "openQ4-updater: %s is not an openQ4 update folder\n", ToUtf8( updateDir ).c_str() );
		return 2;
	}
	logFile.Open( updateDir / HELPER_LOG_FILE );
	logFile.Line( "openQ4-updater started for " + ToUtf8( updateDir ) );

	UpdatePlan plan;
	UpdateResult result;
	std::string planText;
	std::string error;
	if ( !ReadText( updateDir / PLAN_FILE, planText ) || !ParsePlan( planText, plan, error ) ) {
		logFile.Line( "no usable plan: " + error );
		return 1;
	}
	result.version = plan.version;
	result.fromVersion = plan.fromVersion;

	const fs::path install = updateDir.parent_path();
	const fs::path planInstall = fs::weakly_canonical( FromUtf8( plan.installDir ), ec );
	const fs::path relaunch = fs::weakly_canonical( FromUtf8( plan.relaunch ), ec );
	if ( planInstall != install || relaunch.parent_path() != install ) {
		error = "the plan names a different install folder";
	} else if ( !WaitForProcess( pid ) ) {
		error = "openQ4 did not close within " + std::to_string( WAIT_FOR_GAME_SECONDS ) + " seconds";
	} else {
		logFile.Line( "game closed; installing " + plan.version );
		result.succeeded = Apply( plan, updateDir, error );
	}
	result.detail = result.succeeded ? "installed" : error;
	logFile.Line( result.succeeded ? "installed " + plan.version : "failed: " + error );
	if ( !WriteText( updateDir / RESULT_FILE, FormatResult( result ) ) ) {
		logFile.Line( "cannot write the result file" );
	}
	// The plan is spent either way; a later start must not apply it again.
	fs::remove( updateDir / PLAN_FILE, ec );

	if ( error.rfind( "openQ4 did not close", 0 ) == 0 ) {
		return 1;	// the game is still running; starting another copy would only confuse
	}
	std::string launchError;
	if ( !Relaunch( plan, launchError ) ) {
		logFile.Line( launchError );
		TellPlayer( plan.restartFailedMessage );
		return 1;
	}
	logFile.Line( "started " + plan.relaunch );
	return result.succeeded ? 0 : 1;
}

} // namespace

#if defined( _WIN32 )
int WINAPI wWinMain( HINSTANCE, HINSTANCE, PWSTR, int ) {
	int count = 0;
	LPWSTR *wide = CommandLineToArgvW( GetCommandLineW(), &count );
	std::vector<std::string> arguments;
	for ( int i = 0; wide != nullptr && i < count; ++i ) {
		arguments.push_back( ToUtf8( fs::path( wide[ i ] ) ) );
	}
	if ( wide != nullptr ) {
		LocalFree( wide );
	}
	return Run( arguments );
}
#else
int main( int argc, char **argv ) {
	std::vector<std::string> arguments( argv, argv + argc );
	return Run( arguments );
}
#endif
