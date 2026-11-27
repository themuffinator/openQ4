// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef __OPENXRRUNTIMEPROBE_H__
#define __OPENXRRUNTIMEPROBE_H__

/*
===============================================================================

	Stage one of VR headset detection: which OpenXR runtime the Khronos loader
	would load, found the way the loader finds it and read without loading it.

	Creating an OpenXR instance can start a runtime's server (SteamVR launches
	with its first client), so a desktop start the player did not opt into
	never creates one. This probe only reads an environment variable, the
	Windows registry or a manifest file:

	- XR_RUNTIME_JSON, which the loader uses exclusively when it is set;
	- on Windows, HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime, and the
	  AvailableRuntimes beside it for the log;
	- on Linux, active_runtime.<arch>.json and then active_runtime.json under
	  openxr/1/ in $XDG_CONFIG_HOME (~/.config), each of $XDG_CONFIG_DIRS
	  (/etc/xdg) and /etc;
	- on Android, the client is the standalone headset flavor (or its
	  emulator): the headset is the device.

	The parsing and search order are engine-free, so the native test
	(tools/tests/native/OpenXRRuntimeProbeTest.cpp) runs them directly.

===============================================================================
*/

#include <string>
#include <vector>

namespace openq4 {
namespace xr {

enum class RuntimeSource {
	None,				// no active runtime
	Environment,		// XR_RUNTIME_JSON
	Registry,			// Windows: ActiveRuntime
	ConfigFile,			// Linux: an active_runtime.json
	AndroidHeadset		// the standalone headset flavor
};

struct RuntimeProbe {
	RuntimeSource		source = RuntimeSource::None;
	std::string			manifest;			// the active runtime's manifest
	std::string			name;				// its runtime.name, or the library's file name
	std::string			library;			// its runtime.library_path
	bool				manifestReadable = false;
	int					available = 0;		// runtimes registered beside the active one (Windows)
	std::string			note;				// why there is none, for the log
};

inline const char *RuntimeSourceName( RuntimeSource source ) {
	switch ( source ) {
		case RuntimeSource::Environment:	return "XR_RUNTIME_JSON";
		case RuntimeSource::Registry:		return "registry";
		case RuntimeSource::ConfigFile:		return "active_runtime.json";
		case RuntimeSource::AndroidHeadset:	return "standalone headset";
		default:							return "none";
	}
}

// The value of the first string member `key` after `from` in a JSON text,
// with its escapes undone; false when there is none.
inline bool JsonStringMember( const std::string &text, const char *key, size_t from, size_t to, std::string &value ) {
	const std::string quoted = std::string( "\"" ) + key + "\"";
	for ( size_t at = text.find( quoted, from ); at != std::string::npos && at < to; at = text.find( quoted, at + 1 ) ) {
		size_t cursor = at + quoted.size();
		while ( cursor < text.size() && ( text[cursor] == ' ' || text[cursor] == '\t' || text[cursor] == '\r' || text[cursor] == '\n' ) ) {
			++cursor;
		}
		if ( cursor >= text.size() || text[cursor] != ':' ) {
			continue;
		}
		++cursor;
		while ( cursor < text.size() && ( text[cursor] == ' ' || text[cursor] == '\t' || text[cursor] == '\r' || text[cursor] == '\n' ) ) {
			++cursor;
		}
		if ( cursor >= text.size() || text[cursor] != '"' ) {
			continue;
		}
		value.clear();
		for ( ++cursor; cursor < text.size(); ++cursor ) {
			const char c = text[cursor];
			if ( c == '"' ) {
				return true;
			}
			if ( c == '\\' && cursor + 1 < text.size() ) {
				const char escaped = text[++cursor];
				value += escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped;
			} else {
				value += c;
			}
		}
		return false;
	}
	return false;
}

// The file name at the end of a path, either separator.
inline std::string PathFileName( const std::string &path ) {
	const size_t slash = path.find_last_of( "/\\" );
	return slash == std::string::npos ? path : path.substr( slash + 1 );
}

// A runtime manifest's name and library: the members of its "runtime"
// object. A manifest without a name is named after its library's file.
inline bool ParseRuntimeManifest( const std::string &text, std::string &name, std::string &library ) {
	name.clear();
	library.clear();
	const size_t runtime = text.find( "\"runtime\"" );
	if ( runtime == std::string::npos ) {
		return false;
	}
	const size_t open = text.find( '{', runtime );
	if ( open == std::string::npos ) {
		return false;
	}
	// the end of the runtime object: its own braces, outside strings
	size_t close = std::string::npos;
	int depth = 0;
	bool inString = false;
	for ( size_t i = open; i < text.size(); ++i ) {
		const char c = text[i];
		if ( inString ) {
			if ( c == '\\' ) {
				++i;
			} else if ( c == '"' ) {
				inString = false;
			}
		} else if ( c == '"' ) {
			inString = true;
		} else if ( c == '{' ) {
			++depth;
		} else if ( c == '}' && --depth == 0 ) {
			close = i;
			break;
		}
	}
	if ( close == std::string::npos ) {
		return false;
	}
	JsonStringMember( text, "library_path", open, close, library );
	if ( !JsonStringMember( text, "name", open, close, name ) || name.empty() ) {
		name = PathFileName( library );
	}
	return !library.empty();
}

// The files the Linux loader reads for the active runtime, in its order:
// the architecture's own name and then the generic one, each through
// $XDG_CONFIG_HOME (or ~/.config), every $XDG_CONFIG_DIRS entry (or
// /etc/xdg) and /etc. Unset or empty variables take their defaults.
inline std::vector<std::string> ActiveRuntimeCandidates( const char *xdgConfigHome, const char *home, const char *xdgConfigDirs,
	const char *arch ) {
	std::vector<std::string> directories;
	if ( xdgConfigHome != nullptr && xdgConfigHome[0] != '\0' ) {
		directories.push_back( xdgConfigHome );
	} else if ( home != nullptr && home[0] != '\0' ) {
		directories.push_back( std::string( home ) + "/.config" );
	}
	const std::string dirs = xdgConfigDirs != nullptr && xdgConfigDirs[0] != '\0' ? xdgConfigDirs : "/etc/xdg";
	for ( size_t start = 0; start <= dirs.size(); ) {
		const size_t colon = dirs.find( ':', start );
		const std::string entry = dirs.substr( start, colon == std::string::npos ? std::string::npos : colon - start );
		if ( !entry.empty() ) {
			directories.push_back( entry );
		}
		if ( colon == std::string::npos ) {
			break;
		}
		start = colon + 1;
	}
	directories.push_back( "/etc" );
	std::vector<std::string> names;
	if ( arch != nullptr && arch[0] != '\0' ) {
		names.push_back( std::string( "active_runtime." ) + arch + ".json" );
	}
	names.push_back( "active_runtime.json" );
	std::vector<std::string> candidates;
	for ( const std::string &fileName : names ) {
		for ( std::string directory : directories ) {
			while ( directory.size() > 1 && directory.back() == '/' ) {
				directory.pop_back();
			}
			candidates.push_back( directory + "/openxr/1/" + fileName );
		}
	}
	return candidates;
}

// The probe itself: environment, registry and manifest reads only.
RuntimeProbe ProbeRuntime( void );

} // namespace xr
} // namespace openq4

#endif /* !__OPENXRRUNTIMEPROBE_H__ */
