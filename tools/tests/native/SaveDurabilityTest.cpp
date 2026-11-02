// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Real-file save durability qualification. Compiles the production slot commit
// (Session_MoveSaveFileDurably and the checkpoint commit file operations),
// production recovery (Session_RecoverInterruptedSaveFiles) and production
// CRC-32, then:
//   matrix: kills a child process after every namespace step of a commit and
//           at every staging point, runs recovery, and requires the slot to be
//           wholly the previous save or wholly the new one;
//   bench:  times staged write, sync, the two CRC scans, trailer append and
//           the three-file commit for large payloads.
// A killed process is not a power cut: this qualifies process interruption on
// the filesystem under test, not what the storage device keeps after power loss.
#include "src/framework/DurableFile.h"
#include "src/framework/SaveGameTransaction.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

typedef unsigned char byte;
#include "src/idlib/hashing/CRC32.cpp"

namespace fs = std::filesystem;

// Stand-ins for the engine pieces the extracted production code names.
struct idStr : std::string { using std::string::string; idStr( const std::string& s ) : std::string( s ) {} };
struct Common {
	void Printf( const char*, ... ) {}
	void Warning( const char* format, ... ) { std::fprintf( stderr, "warning: %s\n", format ); }
} commonObject;
static Common* common = &commonObject;
struct idJobContext { void* data; };
enum class idJobPriority { LOW };
enum class idJobSubmitResult { ACCEPTED, EXECUTED_SYNCHRONOUSLY, QUEUE_FULL };
struct idJobList {
	bool AddJob( void ( * )( const idJobContext& ), void* ) { return false; }
	idJobSubmitResult Submit() { return idJobSubmitResult::QUEUE_FULL; }
};
struct JobSystem {
	std::unique_ptr<idJobList> CreateJobList( const char*, int, int, idJobPriority ) { return nullptr; }
} jobSystem;

static fs::path slotRoot;
static fs::path At( const std::string& relative ) { return slotRoot / fs::u8path( relative ); }
static std::string Utf8( const fs::path& path ) {
	const auto bytes = path.u8string();
	return std::string( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
}

#include "CheckpointCommitUnderTest.inc"

static bool Session_RelativeSaveFileExists( const idStr& path ) {
	std::error_code code;
	return fs::is_regular_file( fs::symlink_status( At( path ), code ) );
}
static void Session_RemoveRelativeSaveFile( const idStr& path ) {
	std::error_code code;
	fs::remove( At( path ), code );
}
static bool Session_RenameRelativeSaveFile( const idStr& from, const idStr& to ) {
	std::string unconfirmed, error;
	return Session_MoveSaveFileDurably( Utf8( At( from ) ), Utf8( At( to ) ), unconfirmed, error );
}
static std::string ReadAll( const fs::path& path ) {
	std::string bytes;
	if ( FILE* file = std::fopen( Utf8( path ).c_str(), "rb" ) ) {
		char chunk[64 * 1024];
		std::size_t got;
		while ( ( got = std::fread( chunk, 1, sizeof( chunk ), file ) ) > 0 ) bytes.append( chunk, got );
		std::fclose( file );
	}
	return bytes;
}
static const std::uint32_t IntegrityMagic = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'I' << 24 );
static const std::uint32_t PreviewDeleteMagic = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'D' << 24 );
static std::uint32_t Word( const std::string& bytes, std::size_t at ) {
	std::uint32_t value = 0;
	for ( int i = 3; i >= 0; --i ) value = ( value << 8 ) | static_cast<unsigned char>( bytes[at + i] );
	return value;
}
// The integrity-trailer part of staged validation: length and CRC over the
// protected bytes, as Session_ValidateSaveGamePayload requires.
static bool Session_ValidateStagedSaveGameHeader( const idStr& path ) {
	const std::string bytes = ReadAll( At( path ) );
	if ( bytes.size() < 16 ) return false;
	const std::size_t protectedLength = bytes.size() - 16;
	return Word( bytes, protectedLength ) == IntegrityMagic && Word( bytes, protectedLength + 4 ) == 1 &&
		Word( bytes, protectedLength + 8 ) == protectedLength &&
		Word( bytes, protectedLength + 12 ) == CRC32_BlockChecksum( bytes.data(), static_cast<int>( protectedLength ) );
}
static bool Session_IsSavePreviewDeletionMarker( const idStr& path ) {
	const std::string bytes = ReadAll( At( path ) );
	return bytes.size() == 8 && Word( bytes, 0 ) == PreviewDeleteMagic && Word( bytes, 4 ) == 1;
}

#include "CheckpointRecoveryUnderTest.inc"

namespace {
int checks = 0;
std::string executable;
void Check( bool okay, const std::string& message ) {
	++checks;
	if ( !okay ) { std::cerr << "FAIL: " << message << '\n'; std::exit( 1 ); }
}
#ifdef _WIN32
constexpr int KilledCode = 77;
#else
constexpr int KilledCode = 128 + SIGKILL;
#endif
[[noreturn]] void KillSelf() {
#ifdef _WIN32
	TerminateProcess( GetCurrentProcess(), 77 );
#else
	raise( SIGKILL );
#endif
	std::abort();
}

const char* Names[3] = { "slot.txt", "slot.tga", "slot.save" };
std::string Description( const std::string& generation ) { return generation + " description\n"; }
std::string Preview( const std::string& generation ) { return "TGA " + generation; }
std::string Payload( const std::string& generation, std::size_t bytes ) {
	std::string payload = "GEN-" + generation + ";";
	payload.resize( std::max( bytes, payload.size() ) );
	std::uint32_t state = generation == "new" ? 0x9e3779b9u : 0x7f4a7c15u;
	for ( std::size_t i = generation.size() + 5; i < payload.size(); ++i ) {
		state = state * 1664525u + 1013904223u;
		payload[i] = static_cast<char>( state >> 24 );
	}
	const std::uint32_t trailer[4] = { IntegrityMagic, 1, static_cast<std::uint32_t>( payload.size() ),
		CRC32_BlockChecksum( payload.data(), static_cast<int>( payload.size() ) ) };
	for ( std::uint32_t value : trailer ) for ( int shift = 0; shift < 32; shift += 8 ) payload.push_back( static_cast<char>( value >> shift ) );
	return payload;
}
std::string Generation( const std::string& payload ) {
	const auto end = payload.find( ';' );
	return payload.rfind( "GEN-", 0 ) == 0 && end != std::string::npos ? payload.substr( 4, end - 4 ) : "";
}

// Production-order staged write: content, flush, then the platform sync used
// by idFile_Permanent::Sync. Optionally killed after some bytes reach the OS.
void WriteFile( const fs::path& path, const std::string& bytes, std::size_t killAfter = std::string::npos ) {
	FILE* file = std::fopen( Utf8( path ).c_str(), "wb" );
	Check( file != nullptr, "open " + Utf8( path ) );
	const std::size_t first = std::min( killAfter, bytes.size() );
	Check( std::fwrite( bytes.data(), 1, first, file ) == first, "write" );
	Check( std::fflush( file ) == 0, "flush" );
	if ( killAfter != std::string::npos ) KillSelf();
#ifdef _WIN32
	Check( _commit( _fileno( file ) ) == 0, "commit" );
#else
	Check( fsync( fileno( file ) ) == 0, "fsync" );
#endif
	Check( std::fclose( file ) == 0, "close" );
}

// Session_AppendSaveGameIntegrityTrailer appends to the synced payload.
void AppendFile( const fs::path& path, const char* bytes, std::size_t length ) {
	FILE* file = std::fopen( Utf8( path ).c_str(), "ab" );
	Check( file != nullptr && std::fwrite( bytes, 1, length, file ) == length && std::fflush( file ) == 0, "append" );
#ifdef _WIN32
	Check( _commit( _fileno( file ) ) == 0, "append commit" );
#else
	Check( fsync( fileno( file ) ) == 0, "append fsync" );
#endif
	Check( std::fclose( file ) == 0, "append close" );
}

void Clean() {
	for ( const char* name : Names ) for ( const char* suffix : { "", ".tmp", ".prev" } ) {
		std::error_code code;
		fs::remove( At( std::string( name ) + suffix ), code );
		Check( !code, "clean slot file" );
	}
}
void PopulateOld( int existing ) {
	Clean();
	if ( existing & 1 ) WriteFile( At( Names[0] ), Description( "old" ) );
	if ( existing & 2 ) WriteFile( At( Names[1] ), Preview( "old" ) );
	if ( existing & 4 ) WriteFile( At( Names[2] ), Payload( "old", 4096 ) );
}
std::array<openq4::SaveGameTransactionFile, 3> SlotFiles() {
	std::array<openq4::SaveGameTransactionFile, 3> files;
	for ( int i = 0; i < 3; ++i ) {
		files[i].final = Utf8( At( Names[i] ) );
		files[i].temporary = files[i].final + ".tmp";
		files[i].backup = files[i].final + ".prev";
	}
	return files;
}

// Production checkpoint commit operations, killed after a counted step.
struct KillingOps : sessionSaveCommitFileOps_t {
	int remaining = -1;
	void Step() { if ( remaining >= 0 && remaining-- == 0 ) KillSelf(); }
	bool Move( const std::string& from, const std::string& to, std::string& error ) {
		const bool result = sessionSaveCommitFileOps_t::Move( from, to, error );
		Step();
		return result;
	}
	bool Remove( const std::string& path, std::string& error ) {
		const bool result = sessionSaveCommitFileOps_t::Remove( path, error );
		Step();
		return result;
	}
};

int ChildCommit( int killAfter ) {
	auto files = SlotFiles();
	KillingOps ops;
	ops.remaining = killAfter;
	const auto result = openq4::CommitSaveGame( files, ops, false );
	return result.committed ? 0 : 2;
}

// Staging follows Session::SaveGame's synchronous order: preview capture,
// payload, its integrity trailer, then the description sidecar.
int ChildStage( int point, std::size_t payloadBytes ) {
	const std::string payload = Payload( "new", payloadBytes );
	const std::string body = payload.substr( 0, payload.size() - 16 );
	const fs::path staged = At( std::string( Names[2] ) + ".tmp" );
	WriteFile( At( std::string( Names[1] ) + ".tmp" ), Preview( "new" ) );
	if ( point == 0 ) KillSelf();
	if ( point == 1 ) WriteFile( staged, body, body.size() / 2 );
	WriteFile( staged, body );
	if ( point == 2 ) KillSelf();
	AppendFile( staged, payload.data() + body.size(), 16 );
	if ( point == 3 ) KillSelf();
	WriteFile( At( std::string( Names[0] ) + ".tmp" ), Description( "new" ) );
	if ( point == 4 ) KillSelf();
	return 0;
}

int RunChild( const std::vector<std::string>& arguments ) {
#ifdef _WIN32
	auto quote = []( const std::string& text ) { return L"\"" + fs::u8path( text ).wstring() + L"\""; };
	std::wstring command = quote( executable );
	for ( const auto& argument : arguments ) command += L" " + quote( argument );
	STARTUPINFOW startup{};
	startup.cb = sizeof( startup );
	PROCESS_INFORMATION process{};
	Check( CreateProcessW( fs::u8path( executable ).wstring().c_str(), command.data(), nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process ) != FALSE, "launch child" );
	WaitForSingleObject( process.hProcess, INFINITE );
	DWORD code = 0;
	GetExitCodeProcess( process.hProcess, &code );
	CloseHandle( process.hThread );
	CloseHandle( process.hProcess );
	return static_cast<int>( code );
#else
	const pid_t pid = fork();
	Check( pid >= 0, "fork child" );
	if ( pid == 0 ) {
		std::vector<char*> argv{ const_cast<char*>( executable.c_str() ) };
		for ( const auto& argument : arguments ) argv.push_back( const_cast<char*>( argument.c_str() ) );
		argv.push_back( nullptr );
		execv( executable.c_str(), argv.data() );
		_exit( 127 );
	}
	int status = 0;
	Check( waitpid( pid, &status, 0 ) == pid, "wait child" );
	return WIFEXITED( status ) ? WEXITSTATUS( status ) : 128 + WTERMSIG( status );
#endif
}

// After recovery the slot is one generation: every component that generation
// had, nothing else, and no temporaries or backups.
void CheckWhole( const std::string& expected, int existing, const std::string& context ) {
	Check( Session_RecoverInterruptedSaveFiles( Names[2], Names[0], Names[1] ), context + ": recovery succeeds" );
	for ( int i = 0; i < 3; ++i ) {
		for ( const char* suffix : { ".tmp", ".prev" } ) {
			Check( !fs::exists( At( std::string( Names[i] ) + suffix ) ), context + ": no leftover " + Names[i] + suffix );
		}
	}
	const bool present = expected == "new" || ( existing & 4 );
	Check( fs::exists( At( Names[2] ) ) == present, context + ": payload presence" );
	if ( !present ) return;
	Check( Session_ValidateStagedSaveGameHeader( Names[2] ), context + ": payload validates" );
	Check( Generation( ReadAll( At( Names[2] ) ) ) == expected, context + ": payload is the " + expected + " save" );
	const bool sidecars = expected == "new";
	if ( sidecars || ( existing & 1 ) ) Check( ReadAll( At( Names[0] ) ) == Description( expected ), context + ": description matches payload" );
	else Check( !fs::exists( At( Names[0] ) ), context + ": no foreign description" );
	if ( sidecars || ( existing & 2 ) ) Check( ReadAll( At( Names[1] ) ) == Preview( expected ), context + ": preview matches payload" );
	else Check( !fs::exists( At( Names[1] ) ), context + ": no foreign preview" );
}

void Matrix() {
	int kills = 0;
	for ( int existing : { 0, 4, 5, 6, 7 } ) {
		for ( int killAfter = 0;; ++killAfter ) {
			PopulateOld( existing );
			WriteFile( At( std::string( Names[0] ) + ".tmp" ), Description( "new" ) );
			WriteFile( At( std::string( Names[1] ) + ".tmp" ), Preview( "new" ) );
			WriteFile( At( std::string( Names[2] ) + ".tmp" ), Payload( "new", 64 * 1024 ) );
			const int code = RunChild( { "--child-commit", Utf8( slotRoot ), std::to_string( killAfter ) } );
			const std::string context = "commit existing=" + std::to_string( existing ) + " kill-after=" + std::to_string( killAfter );
			Check( code == 0 || code == KilledCode, context + ": child exit " + std::to_string( code ) );
			// The payload rename is the commit point.
			const bool committed = Generation( ReadAll( At( Names[2] ) ) ) == "new" && !fs::exists( At( std::string( Names[2] ) + ".tmp" ) );
			CheckWhole( committed ? "new" : "old", existing, context );
			if ( code == 0 ) { Check( committed, context + ": completed commit published" ); break; }
			++kills;
		}
		for ( int point = 0; point <= 4; ++point ) {
			PopulateOld( existing );
			const int code = RunChild( { "--child-stage", Utf8( slotRoot ), std::to_string( point ), "262144" } );
			const std::string context = "stage existing=" + std::to_string( existing ) + " point=" + std::to_string( point );
			Check( code == KilledCode, context + ": child killed" );
			CheckWhole( "old", existing, context );
			++kills;
		}
	}
#ifdef _WIN32
	// Another process (a backup tool, an indexer) holding the old payload
	// without delete sharing makes the backup rename fail: the commit must
	// roll back and leave the previous slot readable.
	PopulateOld( 7 );
	WriteFile( At( std::string( Names[0] ) + ".tmp" ), Description( "new" ) );
	WriteFile( At( std::string( Names[1] ) + ".tmp" ), Preview( "new" ) );
	WriteFile( At( std::string( Names[2] ) + ".tmp" ), Payload( "new", 64 * 1024 ) );
	HANDLE lock = CreateFileW( At( Names[2] ).wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr );
	Check( lock != INVALID_HANDLE_VALUE, "lock old payload" );
	auto files = SlotFiles();
	sessionSaveCommitFileOps_t ops;
	const auto result = openq4::CommitSaveGame( files, ops, false );
	CloseHandle( lock );
	Check( !result.committed && !result.recoveryNeeded, "locked payload refuses and rolls back" );
	CheckWhole( "old", 7, "locked payload" );
#endif
	Clean();
	std::cout << "Save durability matrix: " << kills << " interrupted processes recovered to one whole save\n";
}

double Seconds( std::chrono::steady_clock::time_point begin ) {
	return std::chrono::duration<double>( std::chrono::steady_clock::now() - begin ).count();
}
std::uint32_t CrcFile( const fs::path& path, std::size_t length ) {
	FILE* file = std::fopen( Utf8( path ).c_str(), "rb" );
	Check( file != nullptr, "open for checksum" );
	std::vector<unsigned char> buffer( 64 * 1024 );
	std::uint32_t crc = 0;
	CRC32_InitChecksum( crc );
	for ( std::size_t remaining = length; remaining > 0; ) {
		const std::size_t chunk = std::min( remaining, buffer.size() );
		Check( std::fread( buffer.data(), 1, chunk, file ) == chunk, "checksum read" );
		CRC32_UpdateChecksum( crc, buffer.data(), static_cast<int>( chunk ) );
		remaining -= chunk;
	}
	std::fclose( file );
	CRC32_FinishChecksum( crc );
	return crc;
}

struct PlainOps : sessionSaveCommitFileOps_t {
	bool Move( const std::string& from, const std::string& to, std::string& error ) {
		std::error_code code;
		fs::rename( fs::u8path( from ), fs::u8path( to ), code );
		if ( code ) error = code.message();
		return !code;
	}
};

// One JSON object per measurement on stdout.
void Bench( const std::vector<std::size_t>& mebibytes, int repeat ) {
	for ( std::size_t size : mebibytes ) {
		const std::string payload = Payload( "new", size * 1024 * 1024 );
		const std::size_t bodyBytes = payload.size() - 16;
		for ( int run = 0; run < repeat; ++run ) {
			PopulateOld( 7 );
			WriteFile( At( std::string( Names[0] ) + ".tmp" ), Description( "new" ) );
			WriteFile( At( std::string( Names[1] ) + ".tmp" ), Preview( "new" ) );
			const fs::path staged = At( std::string( Names[2] ) + ".tmp" );
			auto begin = std::chrono::steady_clock::now();
			FILE* file = std::fopen( Utf8( staged ).c_str(), "wb" );
			Check( file && std::fwrite( payload.data(), 1, bodyBytes, file ) == bodyBytes && std::fflush( file ) == 0, "bench write" );
			const double write = Seconds( begin );
			begin = std::chrono::steady_clock::now();
#ifdef _WIN32
			Check( _commit( _fileno( file ) ) == 0, "bench sync" );
#else
			Check( fsync( fileno( file ) ) == 0, "bench sync" );
#endif
			const double sync = Seconds( begin );
			std::fclose( file );
			begin = std::chrono::steady_clock::now();
			const std::uint32_t crc = CrcFile( staged, bodyBytes );
			const double checksum = Seconds( begin );
			Check( crc == Word( payload, bodyBytes + 12 ), "bench checksum matches" );
			begin = std::chrono::steady_clock::now();
			AppendFile( staged, payload.data() + bodyBytes, 16 );
			const double trailer = Seconds( begin );
			// Staged validation repeats the protected-length scan.
			begin = std::chrono::steady_clock::now();
			Check( CrcFile( staged, bodyBytes ) == crc, "bench validation" );
			const double validation = Seconds( begin );
			auto files = SlotFiles();
			sessionSaveCommitFileOps_t durable;
			begin = std::chrono::steady_clock::now();
			Check( openq4::CommitSaveGame( files, durable, false ).committed, "bench durable commit" );
			const double commitDurable = Seconds( begin );
			Check( durable.durabilityUnconfirmed.empty(), "filesystem confirmed every rename: " + durable.durabilityUnconfirmed );
			// The pre-change commit for comparison, from the same state: the new
			// files staged again beside the same small previous slot.
			for ( int i = 0; i < 3; ++i ) fs::rename( At( Names[i] ), At( std::string( Names[i] ) + ".tmp" ) );
			WriteFile( At( Names[0] ), Description( "old" ) );
			WriteFile( At( Names[1] ), Preview( "old" ) );
			WriteFile( At( Names[2] ), Payload( "old", 4096 ) );
			files = SlotFiles();
			PlainOps plain;
			begin = std::chrono::steady_clock::now();
			Check( openq4::CommitSaveGame( files, plain, false ).committed, "bench plain commit" );
			const double commitPlain = Seconds( begin );
			begin = std::chrono::steady_clock::now();
			Check( CrcFile( At( Names[2] ), bodyBytes ) == crc, "bench load checksum" );
			const double loadChecksum = Seconds( begin );
			std::printf( "{\"mib\":%zu,\"run\":%d,\"write_ms\":%.3f,\"sync_ms\":%.3f,\"crc_ms\":%.3f,\"trailer_sync_ms\":%.3f,"
				"\"validation_crc_ms\":%.3f,\"commit_durable_ms\":%.3f,\"commit_plain_ms\":%.3f,\"load_crc_warm_ms\":%.3f}\n",
				size, run, write * 1e3, sync * 1e3, checksum * 1e3, trailer * 1e3, validation * 1e3,
				commitDurable * 1e3, commitPlain * 1e3, loadChecksum * 1e3 );
			std::fflush( stdout );
		}
	}
	Clean();
}
} // namespace

int main( int argc, char** argv ) {
	executable = Utf8( fs::absolute( fs::u8path( argv[0] ) ) );
	const std::vector<std::string> args( argv + 1, argv + argc );
	if ( args.size() == 3 && args[0] == "--child-commit" ) {
		slotRoot = fs::u8path( args[1] );
		return ChildCommit( std::atoi( args[2].c_str() ) );
	}
	if ( args.size() == 4 && args[0] == "--child-stage" ) {
		slotRoot = fs::u8path( args[1] );
		return ChildStage( std::atoi( args[2].c_str() ), std::strtoull( args[3].c_str(), nullptr, 10 ) );
	}
	if ( args.size() < 2 ) {
		std::cerr << "usage: SaveDurabilityTest <dir> matrix | <dir> bench <MiB>... [--repeat N]\n";
		return 64;
	}
	slotRoot = fs::u8path( args[0] );
	Check( slotRoot.is_absolute() && fs::is_directory( slotRoot ), "slot directory is an existing absolute path" );
	if ( args[1] == "matrix" && args.size() == 2 ) {
		Matrix();
		std::cout << "Save durability checks passed: " << checks << '\n';
		return 0;
	}
	if ( args[1] == "bench" ) {
		std::vector<std::size_t> sizes;
		int repeat = 1;
		for ( std::size_t i = 2; i < args.size(); ++i ) {
			if ( args[i] == "--repeat" && i + 1 < args.size() ) repeat = std::atoi( args[++i].c_str() );
			else sizes.push_back( std::strtoull( args[i].c_str(), nullptr, 10 ) );
		}
		Bench( sizes, repeat );
		return 0;
	}
	return 64;
}
