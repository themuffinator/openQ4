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
// A file that is there and cannot be used (too large, cut short, neither form,
// or holding more than this build can keep) is never written over: the list is
// the operator's record, and a server that could not read it must not replace
// it with what little it understood. Saving stays refused until a load works.
//
// Include after the game's precompiled header.
//----------------------------------------------------------------

#ifndef __GAME_MP_MODERATIONFILE_H__
#define __GAME_MP_MODERATIONFILE_H__

#include "Moderation.h"

#define MP_MOD_BANLIST_FILENAME		"banlist.txt"
// the list is written here in full, then put in the list's place in one step
#define MP_MOD_BANLIST_STAGED		MP_MOD_BANLIST_FILENAME ".new"
// the explanation SaveBanFile writes under the header; never read back
#define MP_MOD_BANLIST_HELP_PREFIX	"#:"

static const int MP_MOD_BANLIST_MAX_BYTES = 512 * 1024;
static const int MP_MOD_BANLIST_MAX_KEPT_LINES = 256;

typedef struct mpModBanFileResult_s {
	bool		found;			// there was a file
	int			bans;			// bans read
	int			kept;			// lines this build does not understand, written back as they were
	int			legacy;			// Quake 4 GUID entries turned into comments
	int			overflow;		// bans past the MP_MOD_MAX_BANS the list holds: not enforced
	bool		unreadable;		// the file is there and cannot be used; it is left as it is and saving is refused
	const char *why;			// what was wrong with it, for the operator
	bool		rewrite;		// the caller should save the list in the text form
} mpModBanFileResult_t;

// True after a load found a file it could not use, until a load works: a save
// would write over the operator's record.
inline bool &MPModBanFileBlocked( void ) {
	static bool blocked = false;
	return blocked;
}

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
	result.why = "";
	keptLines.Clear();
	MPModBanFileBlocked() = false;

	idFile *file = fileSystem->OpenExplicitFileRead( fileSystem->RelativePathToOSPath( MP_MOD_BANLIST_FILENAME, "fs_savepath" ) );
	if ( file == NULL ) {
		return;
	}
	result.found = true;
	const int length = file->Length();
	idStr text;
	bool read = length >= 0 && length <= MP_MOD_BANLIST_MAX_BYTES;
	if ( read && length > 0 ) {
		text.Fill( ' ', length );
		read = file->Read( &text[ 0 ], length ) == length;
	}
	fileSystem->CloseFile( file );
	if ( !read ) {
		result.unreadable = true;
		result.why = length > MP_MOD_BANLIST_MAX_BYTES ? "it is larger than 512 KB" : "it could not be read to its end";
		MPModBanFileBlocked() = true;
		return;
	}

	// an editor's byte order mark is not part of the first line
	int at = 0;
	if ( length >= 3 && static_cast<unsigned char>( text[ 0 ] ) == 0xEF && static_cast<unsigned char>( text[ 1 ] ) == 0xBB &&
		static_cast<unsigned char>( text[ 2 ] ) == 0xBF ) {
		at = 3;
	}
	// Text holds no zero byte. Quake 4's list is 32-bit lengths, whose high bytes
	// are zero, and so does a file an editor saved as UTF-16.
	bool isText = true;
	for ( int i = at; i < length; i++ ) {
		if ( text[ i ] == '\0' ) {
			isText = false;
			break;
		}
	}

	if ( isText ) {
		while ( at < length ) {
			// a line ends at either line-end byte, so a file with bare carriage returns is still lines
			int end = at;
			while ( end < length && text[ end ] != '\n' && text[ end ] != '\r' ) {
				end++;
			}
			char line[ MP_MOD_LINE_BYTES ];
			int lineLength = 0;
			for ( int i = at; i < end && lineLength < MP_MOD_LINE_BYTES - 1; i++ ) {
				line[ lineLength++ ] = text[ i ];
			}
			line[ lineLength ] = '\0';
			const bool cut = end - at > MP_MOD_LINE_BYTES - 1;
			at = end + 1;

			const char *start = line;
			while ( *start == ' ' || *start == '\t' ) {
				start++;
			}
			if ( *start == '\0' || idStr::Cmp( start, MP_MOD_BANLIST_HEADER ) == 0 ||
				idStr::Cmpn( start, MP_MOD_BANLIST_HELP_PREFIX, static_cast<int>( strlen( MP_MOD_BANLIST_HELP_PREFIX ) ) ) == 0 ) {
				continue;
			}
			if ( cut ) {
				// written back, it would be a different line
				result.unreadable = true;
				result.why = "a line is longer than 511 bytes";
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
				} else {
					result.overflow++;
				}
				continue;
			}
			if ( keptLines.Num() < MP_MOD_BANLIST_MAX_KEPT_LINES ) {
				keptLines.Append( idStr( start ) );
			} else {
				result.unreadable = true;
				result.why = "it holds more than 256 lines that are not bans";
			}
			if ( *start != '#' ) {
				result.kept++;
			}
		}
		if ( result.overflow > 0 ) {
			// the first MP_MOD_MAX_BANS are enforced; a rewrite would delete the rest
			result.unreadable = true;
			result.why = "it holds more bans than the list can (512)";
		}
		MPModBanFileBlocked() = result.unreadable;
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
		// neither form, perhaps text an editor saved as UTF-16: leave it for the operator
		result.legacy = 0;
		result.unreadable = true;
		result.why = "it is neither a text ban list nor Quake 4's";
		keptLines.Clear();
		MPModBanFileBlocked() = true;
		return;
	}
	result.rewrite = true;
}

/*
================
MPModSaveBanFile

The whole list goes to a staged file first and takes the list's place in one
step (idFileSystem::PromoteFile), so a crash during the write leaves the
previous list as it was rather than a list cut short. Writes are buffered and
report nothing when the disk is full, so the staged file is read back for its
length before it is promoted.
================
*/
inline bool MPModSaveBanFile( const mpModBanList_t &list, const idStrList &keptLines ) {
	if ( MPModBanFileBlocked() ) {
		return false;
	}
	idFile *file = fileSystem->OpenFileWrite( MP_MOD_BANLIST_STAGED, "fs_savepath" );
	if ( file == NULL ) {
		return false;
	}
	int expected = 0;
	expected += file->Printf( "%s\n", MP_MOD_BANLIST_HEADER );
	expected += file->Printf( "%s One ban a line. Times are seconds since 1970 (UTC); an expiry of 0 never ends.\n", MP_MOD_BANLIST_HELP_PREFIX );
	expected += file->Printf( "%s ban address <address or address/prefix> <created> <expires> \"<name>\" \"<reason>\"\n", MP_MOD_BANLIST_HELP_PREFIX );
	for ( int i = 0; i < keptLines.Num(); i++ ) {
		expected += file->Printf( "%s\n", keptLines[ i ].c_str() );
	}
	for ( int i = 0; i < list.count; i++ ) {
		char line[ MP_MOD_LINE_BYTES ];
		if ( MPModBanFormatLine( list.bans[ i ], line, sizeof( line ) ) > 0 ) {
			expected += file->Printf( "%s\n", line );
		}
	}
	fileSystem->CloseFile( file );

	int written = -1;
	idFile *check = fileSystem->OpenExplicitFileRead( fileSystem->RelativePathToOSPath( MP_MOD_BANLIST_STAGED, "fs_savepath" ) );
	if ( check != NULL ) {
		written = check->Length();
		fileSystem->CloseFile( check );
	}
	if ( written != expected || !fileSystem->PromoteFile( MP_MOD_BANLIST_STAGED, MP_MOD_BANLIST_FILENAME, "fs_savepath" ) ) {
		fileSystem->RemoveFileChecked( MP_MOD_BANLIST_STAGED, "fs_savepath" );
		return false;
	}
	return true;
}

#endif /* !__GAME_MP_MODERATIONFILE_H__ */
