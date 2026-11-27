// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
//
// Stage one of VR headset detection (OpenXRRuntimeProbe.h): the active
// OpenXR runtime, read from the environment, the registry or a manifest
// without loading it.

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "OpenXRRuntimeProbe.h"

#if defined( _WIN32 )
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>

namespace openq4 {
namespace xr {

namespace {

#if !defined( __ANDROID__ )
// A manifest's text, bounded: runtime manifests are a few hundred bytes.
bool ReadSmallFile( const std::string &path, std::string &text ) {
	text.clear();
	FILE *file = fopen( path.c_str(), "rb" );
	if ( file == nullptr ) {
		return false;
	}
	char buffer[4096];
	size_t total = 0;
	for ( size_t read; ( read = fread( buffer, 1, sizeof( buffer ), file ) ) > 0 && total < 65536; total += read ) {
		text.append( buffer, read );
	}
	fclose( file );
	return true;
}

void ReadManifest( RuntimeProbe &probe ) {
	std::string text;
	probe.manifestReadable = ReadSmallFile( probe.manifest, text );
	if ( probe.manifestReadable ) {
		ParseRuntimeManifest( text, probe.name, probe.library );
	}
	if ( probe.name.empty() ) {
		probe.name = PathFileName( probe.manifest );
	}
}
#endif

#if defined( _WIN32 )
const char *const KHRONOS_KEY = "SOFTWARE\\Khronos\\OpenXR\\1";

// The 64-bit view for a 64-bit client, as the loader in it reads.
bool ReadActiveRuntime( std::string &manifest ) {
	char value[MAX_PATH * 2];
	DWORD size = sizeof( value );
	const LONG result = RegGetValueA( HKEY_LOCAL_MACHINE, KHRONOS_KEY, "ActiveRuntime", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
		nullptr, value, &size );
	if ( result != ERROR_SUCCESS || value[0] == '\0' ) {
		return false;
	}
	manifest = value;
	return true;
}

// The runtimes installed beside the active one: AvailableRuntimes names
// each manifest, the value 0 when it is enabled.
int CountAvailableRuntimes( void ) {
	HKEY key = nullptr;
	if ( RegOpenKeyExA( HKEY_LOCAL_MACHINE, ( std::string( KHRONOS_KEY ) + "\\AvailableRuntimes" ).c_str(), 0, KEY_READ, &key ) != ERROR_SUCCESS ) {
		return 0;
	}
	DWORD values = 0;
	if ( RegQueryInfoKeyA( key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr ) != ERROR_SUCCESS ) {
		values = 0;
	}
	RegCloseKey( key );
	return static_cast<int>( values );
}
#endif

#if !defined( _WIN32 ) && !defined( __ANDROID__ )
const char *LoaderArchitecture( void ) {
#if defined( __x86_64__ ) || defined( _M_X64 )
	return "x86_64";
#elif defined( __aarch64__ ) || defined( _M_ARM64 )
	return "aarch64";
#elif defined( __i386__ ) || defined( _M_IX86 )
	return "i686";
#else
	return "";
#endif
}
#endif

} // namespace

RuntimeProbe ProbeRuntime( void ) {
	RuntimeProbe probe;
#if defined( __ANDROID__ )
	// OpenXR is built into the standalone headset flavor and its emulator only
	probe.source = RuntimeSource::AndroidHeadset;
	probe.name = "Android OpenXR";
	return probe;
#else
	// the loader takes XR_RUNTIME_JSON alone when it is set
	const char *environment = getenv( "XR_RUNTIME_JSON" );
	if ( environment != nullptr && environment[0] != '\0' ) {
		probe.manifest = environment;
		ReadManifest( probe );
		if ( probe.manifestReadable ) {
			probe.source = RuntimeSource::Environment;
		} else {
			probe.note = "XR_RUNTIME_JSON names a manifest that cannot be read";
		}
		return probe;
	}
#if defined( _WIN32 )
	probe.available = CountAvailableRuntimes();
	if ( !ReadActiveRuntime( probe.manifest ) ) {
		probe.note = probe.available > 0 ? "runtimes are installed but none is active" : "no OpenXR runtime is registered";
		return probe;
	}
	ReadManifest( probe );
	if ( probe.manifestReadable ) {
		probe.source = RuntimeSource::Registry;
	} else {
		probe.note = "the active runtime's manifest cannot be read";
	}
	return probe;
#else
	const std::vector<std::string> candidates = ActiveRuntimeCandidates( getenv( "XDG_CONFIG_HOME" ), getenv( "HOME" ),
		getenv( "XDG_CONFIG_DIRS" ), LoaderArchitecture() );
	for ( const std::string &candidate : candidates ) {
		std::string text;
		if ( !ReadSmallFile( candidate, text ) ) {
			continue;
		}
		probe.manifest = candidate;
		probe.manifestReadable = true;
		ParseRuntimeManifest( text, probe.name, probe.library );
		if ( probe.name.empty() ) {
			probe.name = PathFileName( candidate );
		}
		probe.source = RuntimeSource::ConfigFile;
		return probe;
	}
	probe.note = "no active_runtime.json in the XDG configuration directories";
	return probe;
#endif
#endif
}

} // namespace xr
} // namespace openq4
