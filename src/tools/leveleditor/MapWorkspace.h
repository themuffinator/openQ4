// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_EDITOR_MAP_WORKSPACE_H
#define OPENQ4_EDITOR_MAP_WORKSPACE_H

#include "MapDocument.h"

namespace oq4editor {

// Storage reads the effective asset path; writes always go to the authoring
// overlay. Implementations must bound reads to MapDocument::MaxSourceBytes.
class MapStorage {
public:
	virtual ~MapStorage() = default;
	virtual bool Read( const std::string &path, std::string &text, bool &exists, std::string &error ) = 0;
	virtual bool WriteAndSync( const std::string &path, const std::string &text, std::string &error ) = 0;
	virtual bool Promote( const std::string &from, const std::string &to, std::string &error ) = 0;
	virtual void Remove( const std::string &path ) = 0;
};

class MapWorkspace {
public:
	explicit MapWorkspace( MapStorage &files ) : storage( files ) {}
	MapDocument &Document() { return document; }
	const MapDocument &Document() const { return document; }
	const std::string &Path() const { return path; }
	bool New( std::string &error, bool discard = false );
	// Explicit discard permits replacement only after successful read/parse.
	bool Open( const std::string &requestedPath, std::string &error, bool discard = false );
	bool Close( bool discard, std::string &error );
	bool Save( const std::string &requestedPath, bool copy, std::string &error );
	bool Recover( const std::string &requestedPath, std::string &error );
	// Compares the file on disk with the bytes last read or saved. A change
	// means another program edited the map; saving over it is refused.
	bool ExternalChange( bool &changed, std::string &error );
	bool AutoSave( std::string &error );
	std::string RecoveryPath() const;
	static bool MapPath( const std::string &requestedPath, std::string &normalized, std::string &error );

private:
	bool CanReplace( std::string &error ) const;
	bool Publish( const std::string &target, const std::string &text, std::string &error );
	MapStorage &storage;
	MapDocument document;
	std::string path, baseline;
	bool baselineExists = false;
	std::uint64_t recoveryRevision = 0;
};

}
#endif
