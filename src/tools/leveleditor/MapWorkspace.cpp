// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapWorkspace.h"

#include <algorithm>

namespace oq4editor {

bool MapWorkspace::MapPath( const std::string &requested, std::string &normalized, std::string &error ) {
	if ( requested.empty() || requested.size() > 220 ) { error = "supply a map path of 1 to 220 bytes"; return false; }
	if ( requested.back() == ' ' || requested.back() == '.' ) { error = "map path cannot end with a space or dot"; return false; }
	normalized = requested;
	std::replace( normalized.begin(), normalized.end(), '\\', '/' );
	for ( const unsigned char c : normalized ) {
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
			( c >= '0' && c <= '9' ) || c == '/' || c == '_' || c == '-' || c == '.' || c == ' ' ) ) {
			error = "map paths accept letters, numbers, spaces, '/', '_', '-' and '.'"; return false;
		}
	}
	if ( normalized.size() < 4 || !EqualNoCase( normalized.substr( normalized.size() - 4 ), ".map" ) ) { normalized += ".map"; }
	if ( normalized.compare( 0, 5, "maps/" ) != 0 ) { normalized = "maps/" + normalized; }
	std::size_t begin = 0;
	while ( begin < normalized.size() ) {
		const auto end = normalized.find( '/', begin );
		const auto part = normalized.substr( begin, end == std::string::npos ? end : end - begin );
		const auto base = part.substr( 0, part.find( '.' ) );
		const bool device = EqualNoCase( base, "con" ) || EqualNoCase( base, "prn" ) || EqualNoCase( base, "aux" ) || EqualNoCase( base, "nul" ) ||
			( base.size() == 4 && ( EqualNoCase( base.substr( 0, 3 ), "com" ) || EqualNoCase( base.substr( 0, 3 ), "lpt" ) ) && base[3] >= '0' && base[3] <= '9' );
		if ( part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ' || base.empty() || device ) {
			error = "map path contains an empty, relative or nonportable filename component"; return false;
		}
		if ( end == std::string::npos ) { break; }
		begin = end + 1;
	}
	return true;
}

bool MapWorkspace::CanReplace( std::string &error ) const {
	if ( document.IsDirty() ) { error = "document has unsaved changes; save it or use 'mapEdit close discard' first"; return false; }
	return true;
}

bool MapWorkspace::New( std::string &error, bool discard ) {
	error.clear();
	if ( !discard && !CanReplace( error ) ) { return false; }
	document.New(); path.clear(); baseline.clear(); baselineExists = false; recoveryRevision = 0;
	return true;
}

bool MapWorkspace::Open( const std::string &requestedPath, std::string &error, bool discard ) {
	error.clear();
	std::string target, text;
	bool exists = false;
	if ( ( !discard && !CanReplace( error ) ) || !MapPath( requestedPath, target, error ) || !storage.Read( target, text, exists, error ) ) { return false; }
	if ( !exists ) { error = "map not found: " + target; return false; }
	if ( !document.OpenText( text, error ) ) { return false; }
	path = target; baseline = std::move( text ); baselineExists = true; recoveryRevision = 0;
	return true;
}

bool MapWorkspace::Close( bool discard, std::string &error ) {
	error.clear();
	if ( !discard && !CanReplace( error ) ) { return false; }
	document.Close(); path.clear(); baseline.clear(); baselineExists = false; recoveryRevision = 0;
	return true;
}

bool MapWorkspace::Publish( const std::string &target, const std::string &text, std::string &error ) {
	const std::string staged = target + ".editor-tmp";
	if ( !storage.WriteAndSync( staged, text, error ) || !storage.Promote( staged, target, error ) ) {
		storage.Remove( staged );
		return false;
	}
	return true;
}

bool MapWorkspace::Save( const std::string &requestedPath, bool copy, std::string &error ) {
	error.clear();
	if ( !document.IsOpen() ) { error = "no open document"; return false; }
	std::string target;
	if ( !MapPath( requestedPath.empty() ? path : requestedPath, target, error ) ) { return false; }
	if ( copy && EqualNoCase( target, path ) ) { error = "savecopy needs a different destination"; return false; }
	std::string current;
	bool exists = false;
	if ( !storage.Read( target, current, exists, error ) ) { return false; }
	if ( target == path && !copy ) {
		if ( exists != baselineExists || current != baseline ) {
			error = "map changed outside the editor; save to a new path or reopen it"; return false;
		}
	} else if ( exists ) {
		error = "destination already exists; open it first or choose a new path"; return false;
	}
	const std::string text = document.Serialize();
	// Protect the last on-disk version before publishing a changed document.
	if ( exists && current != text && !Publish( target + ".bak", current, error ) ) { return false; }
	if ( !Publish( target, text, error ) ) { return false; }
	if ( !copy ) {
		path = target; baseline = text; baselineExists = true; document.MarkSaved(); recoveryRevision = 0;
	}
	return true;
}

bool MapWorkspace::ExternalChange( bool &changed, std::string &error ) {
	error.clear();
	changed = false;
	if ( !document.IsOpen() || path.empty() ) { return true; }
	std::string current;
	bool exists = false;
	if ( !storage.Read( path, current, exists, error ) ) { return false; }
	changed = exists != baselineExists || current != baseline;
	return true;
}

std::string MapWorkspace::RecoveryPath() const {
	return "editor_experimental/recovery/" + ( path.empty() ? "unnamed.map" : path );
}

bool MapWorkspace::AutoSave( std::string &error ) {
	error.clear();
	if ( !document.IsDirty() || document.Revision() == recoveryRevision ) { return true; }
	if ( !Publish( RecoveryPath(), document.Serialize(), error ) ) { return false; }
	recoveryRevision = document.Revision();
	return true;
}

bool MapWorkspace::Recover( const std::string &requestedPath, std::string &error ) {
	error.clear();
	if ( !CanReplace( error ) ) { return false; }
	std::string target;
	if ( !requestedPath.empty() && !MapPath( requestedPath, target, error ) ) { return false; }
	const std::string recovery = "editor_experimental/recovery/" + ( target.empty() ? "unnamed.map" : target );
	std::string text;
	bool exists = false;
	if ( !storage.Read( recovery, text, exists, error ) ) { return false; }
	if ( !exists ) { error = "no recovery file: " + recovery; return false; }
	if ( !document.OpenText( text, error ) ) { return false; }
	// Recovery never claims that the working map contains these edits. A new
	// save path is required, so recovery cannot overwrite newer external work.
	path.clear(); baseline.clear(); baselineExists = false; recoveryRevision = 0;
	// OpenText starts clean; changing only the saved marker keeps source intact.
	// Use a dedicated dirty marker instead of introducing a synthetic undo edit.
	document.MarkUnsaved();
	return true;
}

}
