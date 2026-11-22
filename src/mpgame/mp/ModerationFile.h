//----------------------------------------------------------------
// ModerationFile.h
//
// openQ4 MP-18: banlist.txt in the save folder, read and written through the
// engine's file system. Both game modules include this: the multiplayer module
// enforces the list, and the Create Server page reads and trims it through
// whichever module the menu is running.
//
// The file is text, one ban a line (mp/Moderation.h). Quake 4 wrote a binary
// list of names and GUIDs under the same name; clients never sent a GUID, so
// those bans matched nobody. Such a file is read once, its entries are kept as
// comment lines, and it is written back in the text form.
//
// Include after the game's precompiled header.
//----------------------------------------------------------------

#ifndef __GAME_MP_MODERATIONFILE_H__
#define __GAME_MP_MODERATIONFILE_H__

#include "Moderation.h"

#define MP_MOD_BANLIST_FILENAME		"banlist.txt"
// where a file that is neither form is copied before it is replaced
#define MP_MOD_BANLIST_UNREADABLE	"banlist.unreadable.txt"
// the explanation SaveBanFile writes under the header; never read back
#define MP_MOD_BANLIST_HELP_PREFIX	"#:"

static const int MP_MOD_BANLIST_MAX_BYTES = 512 * 1024;
static const int MP_MOD_BANLIST_MAX_KEPT_LINES = 256;

typedef struct mpModBanFileResult_s {
	bool	found;			// there was a file
	int		bans;			// bans read
	int		kept;			// lines this build does not understand, written back as they were
	int		legacy;			// Quake 4 GUID entries turned into comments
	bool	unreadable;		// neither form: copied to MP_MOD_BANLIST_UNREADABLE
	bool	rewrite;		// the caller should save the list in the text form
} mpModBanFileResult_t;

/*
================
MPModLoadBanFile

keptLines receives every line that is not a ban this build reads (comments,
and kinds a later build wrote), so a rewrite loses nothing.

A pure server reads loose files only around a map change, so the file is
opened by its OS path in the save folder, never through the search path.
================
*/
inline void MPModLoadBanFile( mpModBanList_t &list, idStrList &keptLines, mpModBanFileResult_t &result ) {
	memset( &list, 0, sizeof( list ) );
	memset( &result, 0, sizeof( result ) );
	keptLines.Clear();

	idFile *file = fileSystem->OpenExplicitFileRead( fileSystem->RelativePathToOSPath( MP_MOD_BANLIST_FILENAME, "fs_savepath" ) );
	if ( file == NULL ) {
		return;
	}
	const int length = file->Length();
	if ( length < 0 || length > MP_MOD_BANLIST_MAX_BYTES ) {
		fileSystem->CloseFile( file );
		common->Warning( "ban list: %s is larger than %d bytes and was not read", MP_MOD_BANLIST_FILENAME, MP_MOD_BANLIST_MAX_BYTES );
		return;
	}
	idStr text;
	bool read = true;
	if ( length > 0 ) {
		text.Fill( ' ', length );
		read = file->Read( &text[ 0 ], length ) == length;
	}
	fileSystem->CloseFile( file );
	if ( !read ) {
		common->Warning( "ban list: %s could not be read", MP_MOD_BANLIST_FILENAME );
		return;
	}
	result.found = true;

	// an editor's byte order mark is not part of the first line
	int at = 0;
	if ( length >= 3 && static_cast<unsigned char>( text[ 0 ] ) == 0xEF && static_cast<unsigned char>( text[ 1 ] ) == 0xBB &&
		static_cast<unsigned char>( text[ 2 ] ) == 0xBF ) {
		at = 3;
	}
	int first = at;
	while ( first < length && ( text[ first ] == ' ' || text[ first ] == '\t' || text[ first ] == '\r' || text[ first ] == '\n' ) ) {
		first++;
	}
	const bool isText = first >= length || text[ first ] == '#' ||
		idStr::Cmpn( text.c_str() + first, "ban ", 4 ) == 0 ||
		idStr::Cmpn( text.c_str() + first, MP_MOD_BANLIST_HEADER, static_cast<int>( strlen( MP_MOD_BANLIST_HEADER ) ) ) == 0;

	if ( isText ) {
		while ( at < length ) {
			int end = at;
			while ( end < length && text[ end ] != '\n' ) {
				end++;
			}
			char line[ MP_MOD_LINE_BYTES ];
			int lineLength = 0;
			for ( int i = at; i < end && lineLength < MP_MOD_LINE_BYTES - 1; i++ ) {
				if ( text[ i ] != '\r' && text[ i ] != '\0' ) {
					line[ lineLength++ ] = text[ i ];
				}
			}
			line[ lineLength ] = '\0';
			at = end + 1;

			const char *start = line;
			while ( *start == ' ' || *start == '\t' ) {
				start++;
			}
			if ( *start == '\0' || idStr::Cmp( start, MP_MOD_BANLIST_HEADER ) == 0 ||
				idStr::Cmpn( start, MP_MOD_BANLIST_HELP_PREFIX, static_cast<int>( strlen( MP_MOD_BANLIST_HELP_PREFIX ) ) ) == 0 ) {
				continue;
			}
			mpModBan_t ban;
			if ( *start != '#' && MPModBanParseLine( start, ban ) ) {
				int index = MPModBanFindRange( list, ban.address, ban.prefixBits );
				if ( index < 0 && list.count < MP_MOD_MAX_BANS ) {
					index = list.count++;
				}
				if ( index >= 0 ) {
					list.bans[ index ] = ban;
					result.bans++;
					continue;
				}
			}
			if ( keptLines.Num() < MP_MOD_BANLIST_MAX_KEPT_LINES ) {
				keptLines.Append( idStr( start ) );
			}
			if ( *start != '#' ) {
				result.kept++;
			}
		}
		return;
	}

	// Quake 4's list: a name and a GUID, each a 32-bit length and that many bytes
	bool whole = true;
	at = 0;
	while ( at < length ) {
		idStr fields[ 2 ];
		for ( int field = 0; field < 2 && whole; field++ ) {
			if ( at + 4 > length ) {
				whole = false;
				break;
			}
			const unsigned int size = static_cast<unsigned char>( text[ at ] ) |
				( static_cast<unsigned int>( static_cast<unsigned char>( text[ at + 1 ] ) ) << 8 ) |
				( static_cast<unsigned int>( static_cast<unsigned char>( text[ at + 2 ] ) ) << 16 ) |
				( static_cast<unsigned int>( static_cast<unsigned char>( text[ at + 3 ] ) ) << 24 );
			at += 4;
			if ( size > 1024 || size > static_cast<unsigned int>( length - at ) ) {
				whole = false;
				break;
			}
			char clean[ MP_MOD_REASON_BYTES ];
			idStr raw;
			raw.Append( text.c_str() + at, static_cast<int>( size ) );
			MPModCopyText( clean, sizeof( clean ), raw.c_str() );
			fields[ field ] = clean;
			at += static_cast<int>( size );
		}
		if ( !whole ) {
			break;
		}
		result.legacy++;
		if ( keptLines.Num() < MP_MOD_BANLIST_MAX_KEPT_LINES ) {
			keptLines.Append( va( "# Quake 4 GUID ban, never enforced (clients send no GUID): \"%s\" %s", fields[ 0 ].c_str(), fields[ 1 ].c_str() ) );
		}
	}
	if ( !whole ) {
		// not a list in either form: keep what was there before it is replaced
		result.legacy = 0;
		result.unreadable = true;
		keptLines.Clear();
		fileSystem->WriteFile( MP_MOD_BANLIST_UNREADABLE, text.c_str(), length, "fs_savepath" );
	}
	result.rewrite = true;
}

/*
================
MPModSaveBanFile
================
*/
inline bool MPModSaveBanFile( const mpModBanList_t &list, const idStrList &keptLines ) {
	idFile *file = fileSystem->OpenFileWrite( MP_MOD_BANLIST_FILENAME, "fs_savepath" );
	if ( file == NULL ) {
		return false;
	}
	file->Printf( "%s\n", MP_MOD_BANLIST_HEADER );
	file->Printf( "%s One ban a line. Times are seconds since 1970 (UTC); an expiry of 0 never ends.\n", MP_MOD_BANLIST_HELP_PREFIX );
	file->Printf( "%s ban address <address or address/prefix> <created> <expires> \"<name>\" \"<reason>\"\n", MP_MOD_BANLIST_HELP_PREFIX );
	for ( int i = 0; i < keptLines.Num(); i++ ) {
		file->Printf( "%s\n", keptLines[ i ].c_str() );
	}
	for ( int i = 0; i < list.count; i++ ) {
		char line[ MP_MOD_LINE_BYTES ];
		if ( MPModBanFormatLine( list.bans[ i ], line, sizeof( line ) ) > 0 ) {
			file->Printf( "%s\n", line );
		}
	}
	fileSystem->CloseFile( file );
	return true;
}

#endif /* !__GAME_MP_MODERATIONFILE_H__ */
