/*
===========================================================================

openQ4 platform services for the in-game updater (Linux, macOS, Android)
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#ifndef ID_DEDICATED

#include "../sys_update.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>

#include <dlfcn.h>
#if !defined( __ANDROID__ )
// Android packages are replaced whole and never start the helper; posix_spawn
// arrives only with API 28, above openQ4's API 24 floor.
#include <spawn.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined( __APPLE__ )
#include <mach-o/dyld.h>
#endif

extern char **environ;

/*
===============================================================================

	libcurl, loaded at run time

	Only the stable easy interface is used, declared here by its documented
	option numbers so no curl headers are needed to build.

===============================================================================
*/

namespace {

typedef void CURL;
typedef int CURLcode;
typedef long long curl_off_t;

static constexpr long CURL_GLOBAL_DEFAULT_FLAGS = 3;
static constexpr int CURLOPT_WRITEDATA = 10001;
static constexpr int CURLOPT_URL = 10002;
static constexpr int CURLOPT_ERRORBUFFER = 10010;
static constexpr int CURLOPT_USERAGENT = 10018;
static constexpr int CURLOPT_LOW_SPEED_LIMIT = 19;
static constexpr int CURLOPT_LOW_SPEED_TIME = 20;
static constexpr int CURLOPT_SSLVERSION = 32;
static constexpr int CURLOPT_NOPROGRESS = 43;
static constexpr int CURLOPT_FOLLOWLOCATION = 52;
static constexpr int CURLOPT_XFERINFODATA = 10057;
static constexpr int CURLOPT_SSL_VERIFYPEER = 64;
static constexpr int CURLOPT_MAXREDIRS = 68;
static constexpr int CURLOPT_CONNECTTIMEOUT = 78;
static constexpr int CURLOPT_SSL_VERIFYHOST = 81;
static constexpr int CURLOPT_NOSIGNAL = 99;
static constexpr int CURLOPT_PROTOCOLS = 181;
static constexpr int CURLOPT_REDIR_PROTOCOLS = 182;
static constexpr int CURLOPT_WRITEFUNCTION = 20011;
static constexpr int CURLOPT_XFERINFOFUNCTION = 20219;
static constexpr int CURLOPT_PROTOCOLS_STR = 10318;
static constexpr int CURLOPT_REDIR_PROTOCOLS_STR = 10319;
static constexpr int CURLINFO_RESPONSE_CODE = 0x200002;
static constexpr long CURLPROTO_HTTPS = 1 << 1;
static constexpr long CURL_SSLVERSION_TLSv1_2 = 6;
static constexpr int CURLE_OK = 0;
static constexpr int CURL_ERROR_SIZE = 256;

typedef CURLcode ( *curlGlobalInit_t )( long );
typedef CURL *( *curlEasyInit_t )( void );
typedef CURLcode ( *curlEasySetopt_t )( CURL *, int, ... );
typedef CURLcode ( *curlEasyPerform_t )( CURL * );
typedef CURLcode ( *curlEasyGetinfo_t )( CURL *, int, ... );
typedef void ( *curlEasyCleanup_t )( CURL * );
typedef const char *( *curlEasyStrerror_t )( CURLcode );

struct curlApi_t {
	void *				library = nullptr;
	curlGlobalInit_t	globalInit = nullptr;
	curlEasyInit_t		easyInit = nullptr;
	curlEasySetopt_t	easySetopt = nullptr;
	curlEasyPerform_t	easyPerform = nullptr;
	curlEasyGetinfo_t	easyGetinfo = nullptr;
	curlEasyCleanup_t	easyCleanup = nullptr;
	curlEasyStrerror_t	easyStrerror = nullptr;
	std::string			failure;
};

const curlApi_t &CurlApi() {
	static curlApi_t api;
	static std::once_flag loaded;
	std::call_once( loaded, [] {
		static const char *const names[] = {
#if defined( __APPLE__ )
			"/usr/lib/libcurl.4.dylib", "libcurl.4.dylib", "libcurl.dylib",
#else
			"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl-nss.so.4", "libcurl.so",
#endif
		};
		for ( const char *name : names ) {
			api.library = dlopen( name, RTLD_NOW | RTLD_LOCAL );
			if ( api.library != nullptr ) {
				break;
			}
		}
		if ( api.library == nullptr ) {
			api.failure = "this system has no libcurl";
			return;
		}
		api.globalInit = reinterpret_cast<curlGlobalInit_t>( dlsym( api.library, "curl_global_init" ) );
		api.easyInit = reinterpret_cast<curlEasyInit_t>( dlsym( api.library, "curl_easy_init" ) );
		api.easySetopt = reinterpret_cast<curlEasySetopt_t>( dlsym( api.library, "curl_easy_setopt" ) );
		api.easyPerform = reinterpret_cast<curlEasyPerform_t>( dlsym( api.library, "curl_easy_perform" ) );
		api.easyGetinfo = reinterpret_cast<curlEasyGetinfo_t>( dlsym( api.library, "curl_easy_getinfo" ) );
		api.easyCleanup = reinterpret_cast<curlEasyCleanup_t>( dlsym( api.library, "curl_easy_cleanup" ) );
		api.easyStrerror = reinterpret_cast<curlEasyStrerror_t>( dlsym( api.library, "curl_easy_strerror" ) );
		if ( !api.globalInit || !api.easyInit || !api.easySetopt || !api.easyPerform || !api.easyGetinfo ||
				!api.easyCleanup || !api.easyStrerror ) {
			api.failure = "this system's libcurl is incomplete";
			dlclose( api.library );
			api.library = nullptr;
			return;
		}
		// curl_global_init is not thread-safe before 7.84; this runs once.
		if ( api.globalInit( CURL_GLOBAL_DEFAULT_FLAGS ) != CURLE_OK ) {
			api.failure = "libcurl failed to initialise";
			dlclose( api.library );
			api.library = nullptr;
		}
	} );
	return api;
}

struct transfer_t {
	const sysHttpsDataFn *		onData;
	const sysHttpsProgressFn *	onProgress;
	std::uint64_t				maxBytes;
	std::uint64_t				received;
	bool						cancelled;
	bool						tooLarge;
};

size_t WriteBody( char *data, size_t size, size_t count, void *user ) {
	transfer_t &transfer = *static_cast<transfer_t *>( user );
	const size_t bytes = size * count;
	transfer.received += bytes;
	if ( transfer.received > transfer.maxBytes ) {
		transfer.tooLarge = true;
		return 0;
	}
	if ( *transfer.onData && !( *transfer.onData )( data, bytes ) ) {
		transfer.cancelled = true;
		return 0;
	}
	return bytes;
}

int Progress( void *user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t ) {
	transfer_t &transfer = *static_cast<transfer_t *>( user );
	if ( total > 0 && static_cast<std::uint64_t>( total ) > transfer.maxBytes ) {
		transfer.tooLarge = true;
		return 1;
	}
	if ( *transfer.onProgress && !( *transfer.onProgress )( static_cast<std::uint64_t>( now > 0 ? now : 0 ),
			static_cast<std::uint64_t>( total > 0 ? total : 0 ) ) ) {
		transfer.cancelled = true;
		return 1;
	}
	return 0;
}

} // namespace

bool Sys_UpdateHttpsAvailable( std::string &reason ) {
	const curlApi_t &api = CurlApi();
	reason = api.failure;
	return api.library != nullptr;
}

sysHttpsResult_t Sys_UpdateHttpsGet( const std::string &url, std::uint64_t maxBytes,
		const sysHttpsDataFn &onData, const sysHttpsProgressFn &onProgress ) {
	sysHttpsResult_t result;
	const curlApi_t &api = CurlApi();
	if ( api.library == nullptr ) {
		result.error = api.failure;
		return result;
	}
	if ( url.compare( 0, 8, "https://" ) != 0 ) {
		result.error = "not an https URL";
		return result;
	}
	CURL *curl = api.easyInit();
	if ( curl == nullptr ) {
		result.error = "libcurl could not start a transfer";
		return result;
	}
	char errorBuffer[ CURL_ERROR_SIZE ] = {};
	transfer_t transfer = { &onData, &onProgress, maxBytes, 0, false, false };
	api.easySetopt( curl, CURLOPT_URL, url.c_str() );
	api.easySetopt( curl, CURLOPT_ERRORBUFFER, errorBuffer );
	api.easySetopt( curl, CURLOPT_USERAGENT, "openQ4-updater/1" );
	api.easySetopt( curl, CURLOPT_NOSIGNAL, 1L );
	api.easySetopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
	api.easySetopt( curl, CURLOPT_MAXREDIRS, 8L );
	// HTTPS only, redirects included. The string form exists from 7.85; older
	// libcurl ignores it and honours the bitmask form.
	api.easySetopt( curl, CURLOPT_PROTOCOLS_STR, "https" );
	api.easySetopt( curl, CURLOPT_REDIR_PROTOCOLS_STR, "https" );
	api.easySetopt( curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS );
	api.easySetopt( curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS );
	api.easySetopt( curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2 );
	api.easySetopt( curl, CURLOPT_SSL_VERIFYPEER, 1L );
	api.easySetopt( curl, CURLOPT_SSL_VERIFYHOST, 2L );
	api.easySetopt( curl, CURLOPT_CONNECTTIMEOUT, 15L );
	api.easySetopt( curl, CURLOPT_LOW_SPEED_LIMIT, 1L );
	api.easySetopt( curl, CURLOPT_LOW_SPEED_TIME, 30L );
	api.easySetopt( curl, CURLOPT_WRITEFUNCTION, &WriteBody );
	api.easySetopt( curl, CURLOPT_WRITEDATA, &transfer );
	api.easySetopt( curl, CURLOPT_XFERINFOFUNCTION, &Progress );
	api.easySetopt( curl, CURLOPT_XFERINFODATA, &transfer );
	api.easySetopt( curl, CURLOPT_NOPROGRESS, 0L );

	const CURLcode code = api.easyPerform( curl );
	long status = 0;
	api.easyGetinfo( curl, CURLINFO_RESPONSE_CODE, &status );
	api.easyCleanup( curl );
	result.status = static_cast<int>( status );

	if ( transfer.cancelled ) {
		result.cancelled = true;
		result.error = "cancelled";
	} else if ( transfer.tooLarge ) {
		result.error = "the response is larger than expected";
	} else if ( code != CURLE_OK ) {
		result.error = errorBuffer[ 0 ] != '\0' ? errorBuffer : api.easyStrerror( code );
	} else if ( status < 200 || status >= 300 ) {
		result.error = "the server answered HTTP " + std::to_string( status );
	} else {
		result.ok = true;
	}
	return result;
}

std::filesystem::path Sys_UpdateExecutablePath( void ) {
#if defined( __APPLE__ )
	uint32_t size = 0;
	_NSGetExecutablePath( nullptr, &size );
	std::string path( size, '\0' );
	if ( _NSGetExecutablePath( path.data(), &size ) != 0 ) {
		return std::filesystem::path();
	}
	path.resize( std::strlen( path.c_str() ) );
	std::error_code ec;
	return std::filesystem::weakly_canonical( path, ec );
#else
	std::error_code ec;
	return std::filesystem::read_symlink( "/proc/self/exe", ec );
#endif
}

std::filesystem::path Sys_UpdateWorkingDirectory( void ) {
	std::error_code ec;
	return std::filesystem::current_path( ec );
}

void Sys_UpdateLaunchArguments( std::vector<std::string> &arguments, std::string &commandTail ) {
	arguments.clear();
	commandTail.clear();
#if !defined( __APPLE__ )
	std::ifstream stream( "/proc/self/cmdline", std::ios::binary );
	const std::string all( ( std::istreambuf_iterator<char>( stream ) ), std::istreambuf_iterator<char>() );
	size_t start = 0;
	bool first = true;
	while ( start < all.size() ) {
		size_t end = all.find( '\0', start );
		if ( end == std::string::npos ) {
			end = all.size();
		}
		if ( !first ) {
			arguments.push_back( all.substr( start, end - start ) );
		}
		first = false;
		start = end + 1;
	}
#endif
}

std::uint64_t Sys_UpdateProcessId( void ) {
	return static_cast<std::uint64_t>( getpid() );
}

#if defined( __ANDROID__ )

bool Sys_UpdateStartDetached( const std::filesystem::path &, const std::vector<std::string> &, std::string &error ) {
	error = "Android packages are updated whole";
	return false;
}

bool Sys_UpdateExtractTar( const std::filesystem::path &, const std::filesystem::path &, std::string &error ) {
	error = "Android packages are updated whole";
	return false;
}

#else

bool Sys_UpdateStartDetached( const std::filesystem::path &executable,
		const std::vector<std::string> &arguments, std::string &error ) {
	std::vector<std::string> all;
	all.push_back( executable.string() );
	all.insert( all.end(), arguments.begin(), arguments.end() );
	std::vector<char *> argv;
	for ( std::string &argument : all ) {
		argv.push_back( argument.data() );
	}
	argv.push_back( nullptr );
	pid_t pid = 0;
	const int started = posix_spawn( &pid, all.front().c_str(), nullptr, nullptr, argv.data(), environ );
	if ( started != 0 ) {
		error = std::string( "starting the updater failed: " ) + std::strerror( started );
		return false;
	}
	return true;
}

bool Sys_UpdateExtractTar( const std::filesystem::path &archive, const std::filesystem::path &destination, std::string &error ) {
	// The player unpacked the original .tar.xz package, so tar and xz are
	// present. The archive's SHA-256 was checked against the signed manifest
	// before this runs, and the staged tree is checked for links afterwards.
	std::string archiveText = archive.string();
	std::string destinationText = destination.string();
	char tarName[] = "tar";
	char extract[] = "-xJf";
	char into[] = "-C";
	char *argv[] = { tarName, extract, archiveText.data(), into, destinationText.data(), nullptr };
	pid_t pid = 0;
	const int started = posix_spawnp( &pid, "tar", nullptr, nullptr, argv, environ );
	if ( started != 0 ) {
		error = std::string( "cannot run tar: " ) + std::strerror( started );
		return false;
	}
	int status = 0;
	while ( waitpid( pid, &status, 0 ) < 0 ) {
		if ( errno != EINTR ) {
			error = "lost track of tar";
			return false;
		}
	}
	if ( !WIFEXITED( status ) || WEXITSTATUS( status ) != 0 ) {
		error = "tar could not unpack the update archive";
		return false;
	}
	return true;
}

#endif /* __ANDROID__ */

bool Sys_UpdateMakeExecutable( const std::filesystem::path &path ) {
	std::error_code ec;
	std::filesystem::permissions( path,
		std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
		std::filesystem::perm_options::add, ec );
	return !ec;
}

const char *Sys_UpdateManagedBy( void ) {
#if defined( __ANDROID__ )
	return "Android package";
#elif defined( __APPLE__ )
	return "macOS app bundle";
#else
	if ( std::getenv( "APPIMAGE" ) != nullptr ) {
		return "AppImage";
	}
	if ( std::getenv( "FLATPAK_ID" ) != nullptr || access( "/.flatpak-info", F_OK ) == 0 ) {
		return "Flatpak";
	}
	if ( std::getenv( "SNAP" ) != nullptr ) {
		return "Snap";
	}
	return nullptr;
#endif
}

#endif /* !ID_DEDICATED */
