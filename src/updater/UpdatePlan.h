/*
===========================================================================

openQ4 update plan
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#ifndef __OPENQ4_UPDATE_PLAN_H__
#define __OPENQ4_UPDATE_PLAN_H__

/*
===============================================================================

	The hand-off between the game and openQ4-updater.

	The game downloads and verifies an update, unpacks it into
	<install>/.update/stage/ and writes <install>/.update/plan.txt last. The
	helper waits for the game to exit, moves each staged file over the install
	(keeping what it replaces in .update/backup/ until every move succeeded),
	writes .update/result.txt and starts the game again. The game reports the
	result in its own, localised interface and removes .update/.

	Both files are UTF-8 text, one "key<TAB>value" pair per line, with "\\",
	"\t" and "\n" escaped in values. A plan is complete only when its last
	line is "end", so a plan the game did not finish writing is never applied.

	Header-only: the helper links nothing from the engine.

===============================================================================
*/

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace openq4 {
namespace update {

static constexpr const char *UPDATE_DIRECTORY = ".update";
static constexpr const char *STAGE_DIRECTORY = "stage";
static constexpr const char *BACKUP_DIRECTORY = "backup";
static constexpr const char *DOWNLOAD_DIRECTORY = "download";
static constexpr const char *PLAN_FILE = "plan.txt";
static constexpr const char *RESULT_FILE = "result.txt";
static constexpr const char *HELPER_LOG_FILE = "updater.log";
static constexpr const char *PLAN_FORMAT = "openq4-update-plan 1";
static constexpr const char *RESULT_FORMAT = "openq4-update-result 1";

struct UpdatePlan {
	std::string					version;		// the release being installed, "0.15.0"
	std::string					fromVersion;	// the version the game was running
	std::string					installDir;		// absolute, UTF-8
	std::string					relaunch;		// absolute path of the executable to start afterwards
	std::string					workingDir;		// the directory the game was started in
	std::vector<std::string>	arguments;		// POSIX: argv[1..] of the game
	std::string					commandTail;	// Windows: the command line after the program name
	std::vector<std::string>	files;			// relative paths staged under stage/, '/' separated
	std::vector<std::string>	removals;		// relative paths to retire (moved to backup/)
	std::string					restartFailedMessage;	// shown, in the player's language, if the game cannot start again
};

struct UpdateResult {
	bool						succeeded = false;
	std::string					version;
	std::string					fromVersion;
	std::string					detail;			// English, for the log; the game words its own message
};

// A relative path the helper may write: '/' separated, no empty, "." or ".."
// component, no drive, colon, backslash or control character, and not inside
// .update itself.
inline bool IsSafeRelativePath( std::string_view path ) {
	if ( path.empty() || path.size() > 512 || path.front() == '/' ) {
		return false;
	}
	std::size_t start = 0;
	bool first = true;
	while ( start <= path.size() ) {
		const std::size_t end = path.find( '/', start );
		const std::string_view part = path.substr( start, end == std::string_view::npos ? std::string_view::npos : end - start );
		if ( part.empty() || part == "." || part == ".." ) {
			return false;
		}
		for ( const char c : part ) {
			const unsigned char u = static_cast<unsigned char>( c );
			if ( u < 0x20 || u == 0x7f || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ) {
				return false;
			}
		}
		if ( part.back() == '.' || part.back() == ' ' ) {
			return false;	// Windows would silently strip them
		}
		if ( first && part == UPDATE_DIRECTORY ) {
			return false;
		}
		first = false;
		if ( end == std::string_view::npos ) {
			break;
		}
		start = end + 1;
	}
	return true;
}

inline std::string EscapePlanValue( std::string_view value ) {
	std::string escaped;
	escaped.reserve( value.size() );
	for ( const char c : value ) {
		switch ( c ) {
			case '\\': escaped += "\\\\"; break;
			case '\t': escaped += "\\t"; break;
			case '\n': escaped += "\\n"; break;
			case '\r': escaped += "\\r"; break;
			default: escaped += c; break;
		}
	}
	return escaped;
}

inline bool UnescapePlanValue( std::string_view value, std::string &out ) {
	out.clear();
	for ( std::size_t i = 0; i < value.size(); ++i ) {
		if ( value[ i ] != '\\' ) {
			out += value[ i ];
			continue;
		}
		if ( ++i >= value.size() ) {
			return false;
		}
		switch ( value[ i ] ) {
			case '\\': out += '\\'; break;
			case 't': out += '\t'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			default: return false;
		}
	}
	return true;
}

// Splits "key<TAB>value" lines, stopping at "end". Returns false on a
// malformed line or when "end" never arrives.
template<typename Visit>
inline bool ReadPlanLines( std::string_view text, std::string_view format, Visit &&visit, std::string &error ) {
	std::size_t start = 0;
	bool sawFormat = false;
	while ( start < text.size() ) {
		std::size_t end = text.find( '\n', start );
		if ( end == std::string_view::npos ) {
			end = text.size();
		}
		std::string_view line = text.substr( start, end - start );
		start = end + 1;
		if ( !line.empty() && line.back() == '\r' ) {
			line.remove_suffix( 1 );
		}
		if ( !sawFormat ) {
			if ( line != format ) {
				error = "not a " + std::string( format ) + " file";
				return false;
			}
			sawFormat = true;
			continue;
		}
		if ( line == "end" ) {
			return true;
		}
		const std::size_t tab = line.find( '\t' );
		if ( tab == std::string_view::npos ) {
			error = "malformed line: " + std::string( line.substr( 0, 64 ) );
			return false;
		}
		std::string value;
		if ( !UnescapePlanValue( line.substr( tab + 1 ), value ) ) {
			error = "malformed value: " + std::string( line.substr( 0, 64 ) );
			return false;
		}
		if ( !visit( line.substr( 0, tab ), value, error ) ) {
			return false;
		}
	}
	error = sawFormat ? "incomplete file (no end line)" : "empty file";
	return false;
}

inline std::string FormatPlan( const UpdatePlan &plan ) {
	std::string text = std::string( PLAN_FORMAT ) + "\n";
	const auto line = [&text]( const char *key, std::string_view value ) {
		text += key;
		text += '\t';
		text += EscapePlanValue( value );
		text += '\n';
	};
	line( "version", plan.version );
	line( "from", plan.fromVersion );
	line( "install", plan.installDir );
	line( "relaunch", plan.relaunch );
	line( "cwd", plan.workingDir );
	line( "commandtail", plan.commandTail );
	line( "restartfailed", plan.restartFailedMessage );
	for ( const std::string &argument : plan.arguments ) {
		line( "arg", argument );
	}
	for ( const std::string &file : plan.files ) {
		line( "file", file );
	}
	for ( const std::string &removal : plan.removals ) {
		line( "remove", removal );
	}
	text += "end\n";
	return text;
}

inline bool ParsePlan( std::string_view text, UpdatePlan &plan, std::string &error ) {
	plan = UpdatePlan();
	const bool read = ReadPlanLines( text, PLAN_FORMAT, [&plan]( std::string_view key, const std::string &value, std::string &lineError ) {
		if ( key == "version" ) { plan.version = value; }
		else if ( key == "from" ) { plan.fromVersion = value; }
		else if ( key == "install" ) { plan.installDir = value; }
		else if ( key == "relaunch" ) { plan.relaunch = value; }
		else if ( key == "cwd" ) { plan.workingDir = value; }
		else if ( key == "commandtail" ) { plan.commandTail = value; }
		else if ( key == "restartfailed" ) { plan.restartFailedMessage = value; }
		else if ( key == "arg" ) { plan.arguments.push_back( value ); }
		else if ( key == "file" || key == "remove" ) {
			if ( !IsSafeRelativePath( value ) ) {
				lineError = "unsafe path in plan: " + value.substr( 0, 128 );
				return false;
			}
			( key == "file" ? plan.files : plan.removals ).push_back( value );
		}
		// Unknown keys are ignored so a newer game can add fields.
		return true;
	}, error );
	if ( !read ) {
		return false;
	}
	if ( plan.version.empty() || plan.installDir.empty() || plan.relaunch.empty() || plan.files.empty() ) {
		error = "plan is missing its version, install folder, executable or files";
		return false;
	}
	return true;
}

inline std::string FormatResult( const UpdateResult &result ) {
	std::string text = std::string( RESULT_FORMAT ) + "\n";
	text += "status\t";
	text += result.succeeded ? "ok" : "failed";
	text += "\nversion\t" + EscapePlanValue( result.version );
	text += "\nfrom\t" + EscapePlanValue( result.fromVersion );
	text += "\ndetail\t" + EscapePlanValue( result.detail );
	text += "\nend\n";
	return text;
}

inline bool ParseResult( std::string_view text, UpdateResult &result, std::string &error ) {
	result = UpdateResult();
	bool sawStatus = false;
	const bool read = ReadPlanLines( text, RESULT_FORMAT, [&result, &sawStatus]( std::string_view key, const std::string &value, std::string & ) {
		if ( key == "status" ) { result.succeeded = value == "ok"; sawStatus = true; }
		else if ( key == "version" ) { result.version = value; }
		else if ( key == "from" ) { result.fromVersion = value; }
		else if ( key == "detail" ) { result.detail = value; }
		return true;
	}, error );
	if ( read && !sawStatus ) {
		error = "result has no status";
		return false;
	}
	return read;
}

} // namespace update
} // namespace openq4

#endif /* !__OPENQ4_UPDATE_PLAN_H__ */
