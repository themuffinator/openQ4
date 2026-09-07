/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include "Unzip.h"
#include "GameDirPolicy.h"
#include "LevelLoadCacheManager.h"
#include "openq4_paks_generated.h"
#include "../sys/URLPolicy.h"

#include <errno.h>
#include <stdint.h>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#if defined( USE_SDL3 )
	#include <SDL3/SDL_filesystem.h>
	#include <SDL3/SDL_error.h>
#endif

#ifdef WIN32
	#include <windows.h>
	#include <io.h>	// for _read
	#include <direct.h> // for _getcwd
#else
	#if !defined( __MACH__ ) && defined( __MWERKS__ )
		#include <types.h>
		#include <stat.h>
	#else
		#include <sys/types.h>
		#include <sys/stat.h>
	#endif
	#include <unistd.h>
#endif

#if ID_ENABLE_CURL
	#include "../curl/include/curl/curl.h"
	#if defined( LIBCURL_VERSION_NUM ) && LIBCURL_VERSION_NUM >= 0x071304 && defined( CURL_VERSION_ASYNCHDNS )
		#define OPENQ4_CURL_CAPABLE_BUILD 1
	#else
		#define OPENQ4_CURL_CAPABLE_BUILD 0
	#endif
#endif

int Com_GetNumStartupCommandLines( void );
const idCmdArgs *Com_GetStartupCommandLine( int index );

/*
========================
FS_IsWindowsDeviceQPathSegment

Windows resolves these names to devices even when an extension is present.
Reject both the legacy single-byte and UTF-8 encodings of the superscript
digits that Windows also treats as COM/LPT device numbers.
========================
*/
static bool FS_IsWindowsDeviceQPathSegment( const char *segment, int segmentLength ) {
	int stemLength = 0;
	while ( stemLength < segmentLength && segment[ stemLength ] != '.' ) {
		stemLength++;
	}

	if ( stemLength == 3 ) {
		return idStr::Icmpn( segment, "con", 3 ) == 0 ||
			idStr::Icmpn( segment, "prn", 3 ) == 0 ||
			idStr::Icmpn( segment, "aux", 3 ) == 0 ||
			idStr::Icmpn( segment, "nul", 3 ) == 0;
	}

	const bool portPrefix = stemLength >= 4 &&
		( idStr::Icmpn( segment, "com", 3 ) == 0 || idStr::Icmpn( segment, "lpt", 3 ) == 0 );
	if ( !portPrefix ) {
		return false;
	}

	const unsigned char digit = static_cast<unsigned char>( segment[ 3 ] );
	if ( stemLength == 4 ) {
		return ( digit >= '1' && digit <= '9' ) || digit == 0xB9 || digit == 0xB2 || digit == 0xB3;
	}

	return stemLength == 5 && digit == 0xC2 &&
		( static_cast<unsigned char>( segment[ 4 ] ) == 0xB9 ||
		  static_cast<unsigned char>( segment[ 4 ] ) == 0xB2 ||
		  static_cast<unsigned char>( segment[ 4 ] ) == 0xB3 );
}

/*
========================
FS_HasParentOSPathSegment

Explicit OS paths may legitimately contain repeated dots in a filename or
directory name.  Reject only a complete parent-directory component so an
absolute save path cannot be used to make CreateOSPath back up the hierarchy.
========================
*/
static bool FS_HasParentOSPathSegment( const char *OSPath ) {
	const char *segmentStart = OSPath;
	for ( const char *scan = OSPath; ; scan++ ) {
		const char c = *scan;
		if ( c != '\0' && c != '/' && c != '\\' ) {
			continue;
		}

		if ( scan - segmentStart == 2 && segmentStart[ 0 ] == '.' && segmentStart[ 1 ] == '.' ) {
			return true;
		}
		if ( c == '\0' ) {
			return false;
		}
		segmentStart = scan + 1;
	}
}

/*
========================
FS_ValidateRelativeWritePath

Mutation APIs accept portable qpaths, not OS paths.  Validate before joining
the caller-controlled value to a writable root so platform normalization can
never reinterpret a segment or escape the game directory.
========================
*/
static bool FS_ValidateRelativeWritePath( const char *relativePath, const char **reason ) {
	if ( reason != NULL ) {
		*reason = NULL;
	}

	if ( relativePath == NULL || relativePath[ 0 ] == '\0' ) {
		if ( reason != NULL ) {
			*reason = "path is empty";
		}
		return false;
	}
	if ( relativePath[ 0 ] == '/' || relativePath[ 0 ] == '\\' ) {
		if ( reason != NULL ) {
			*reason = "path is rooted";
		}
		return false;
	}

	const char *segmentStart = relativePath;
	for ( const char *scan = relativePath; ; scan++ ) {
		const unsigned char c = static_cast<unsigned char>( *scan );
		if ( c == '\\' || c == ':' ) {
			if ( reason != NULL ) {
				*reason = "path contains an OS path separator or volume marker";
			}
			return false;
		}
		if ( c != '\0' && ( c < 32 || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' ) ) {
			if ( reason != NULL ) {
				*reason = "path contains a non-portable filename character";
			}
			return false;
		}
		if ( c != '/' && c != '\0' ) {
			continue;
		}

		const int segmentLength = static_cast<int>( scan - segmentStart );
		if ( segmentLength == 0 ) {
			if ( reason != NULL ) {
				*reason = "path contains an empty segment";
			}
			return false;
		}
		if ( ( segmentLength == 1 && segmentStart[ 0 ] == '.' ) ||
			 ( segmentLength == 2 && segmentStart[ 0 ] == '.' && segmentStart[ 1 ] == '.' ) ) {
			if ( reason != NULL ) {
				*reason = "path contains a dot directory segment";
			}
			return false;
		}
		if ( segmentStart[ 0 ] == ' ' ||
			 segmentStart[ segmentLength - 1 ] == '.' ||
			 segmentStart[ segmentLength - 1 ] == ' ' ) {
			if ( reason != NULL ) {
				*reason = "path segment starts or ends in a character normalized by Windows";
			}
			return false;
		}
		if ( FS_IsWindowsDeviceQPathSegment( segmentStart, segmentLength ) ) {
			if ( reason != NULL ) {
				*reason = "path contains a Windows device name";
			}
			return false;
		}

		if ( c == '\0' ) {
			return true;
		}
		segmentStart = scan + 1;
	}
}

/*
=============================================================================

DOOM FILESYSTEM

All of Doom's data access is through a hierarchical file system, but the contents of 
the file system can be transparently merged from several sources.

A "relativePath" is a reference to game file data, which must include a terminating zero.
Dot directory segments, empty segments, OS separators and volume markers are
illegal in qpaths used for mutation, preventing references outside the Doom
directory system and platform-specific filename aliases.

The "base path" is the path to the directory holding all the game directories and
usually the executable. It defaults to the current directory, but can be overridden
with "+set fs_basepath c:\doom" on the command line. The base path cannot be modified
at all after startup.

The "home path" is the user-writable root path for openQ4 data. It can be overridden
with "+set fs_homepath c:\users\you\saved games\openq4" on the command line.

The "save path" is the path to the directory where game files will be saved. It defaults
to the home path, but can be overridden with a "+set fs_savepath c:\doom" on the
command line. Any files that are created during the game (demos, screenshots, etc.) will
be created reletive to the save path.

The "cd path" is the path to an alternate hierarchy that will be searched if a file
is not located in the base path. A user can do a partial install that copies some
data to a base path created on their hard drive and leave the rest on the cd. It defaults
to the process current directory and is locked at startup.

If a user runs the game directly from a CD, the base path would be on the CD. This
should still function correctly, but all file writes will fail (harmlessly).

The "base game" is the directory under the paths where data comes from by default, and
can be either "base" or "demo".

The "current game" may be the same as the base game, or it may be the name of another
directory under the paths that should be searched for files before looking in the base
game. The game directory is set with "+set fs_game myaddon" on the command line. This is
the basis for addons.

No other directories outside of the base game and current game will ever be referenced by
filesystem functions.

To save disk space and speed up file loading, directory trees can be collapsed into zip
files. The files use a ".pk4" extension to prevent users from unzipping them accidentally,
but otherwise they are simply normal zip files. A game directory can have multiple zip
files of the form "pak0.pk4", "pak1.pk4", etc. Zip files are searched in decending order
from the highest number to the lowest, and will always take precedence over the filesystem.
This allows a pk4 distributed as a patch to override all existing data.

Because we will have updated executables freely available online, there is no point to
trying to restrict demo / oem versions of the game with code changes. Demo / oem versions
should be exactly the same executables as release versions, but with different data that
automatically restricts where game media can come from to prevent add-ons from working.

After the paths are initialized, Doom will look for the product.txt file. If not found
and verified, the game will run in restricted mode. In restricted mode, only files
contained in demo/pak0.pk4 will be available for loading, and only if the zip header is
verified to not have been modified. A single exception is made for DoomConfig.cfg. Files
can still be written out in restricted mode, so screenshots and demos are allowed.
Restricted mode can be tested by setting "+set fs_restrict 1" on the command line, even
if there is a valid product.txt under the basepath or cdpath.

If the "fs_copyfiles" cvar is set to 1, then every time a file is sourced from the cd
path, it will be copied over to the save path. This is a development aid to help build
test releases and to copy working sets of files.

If the "fs_copyfiles" cvar is set to 2, any file found in fs_cdpath that is newer than
it's fs_savepath version will be copied to fs_savepath (in addition to the fs_copyfiles 1
behaviour).

If the "fs_copyfiles" cvar is set to 3, files from both basepath and cdpath will be copied
over to the save path. This is useful when copying working sets of files mainly from base
path with an additional cd path (which can be a slower network drive for instance).

If the "fs_copyfiles" cvar is set to 4, files that exist in the cd path but NOT the base path
will be copied to the save path

NOTE: fs_copyfiles and case sensitivity. On fs_caseSensitiveOS 0 filesystems ( win32 ), the
copied files may change casing when copied over.

The relative path "sound/newstuff/test.wav" would be searched for in the following places:

for save path, base path, cd path:
	for current game, base game:
		search directory
		search zip files

downloaded files, to be written to save path + current game's directory

The filesystem can be safely shutdown and reinitialized with different
basedir / cddir / game combinations, but all other subsystems that rely on it
(sound, video) must also be forced to restart.


"fs_caseSensitiveOS":
This cvar is set on operating systems that use case sensitive filesystems (Linux and OSX)
It is a common situation to have the media reference filenames, whereas the file on disc 
only matches in a case-insensitive way. When "fs_caseSensitiveOS" is set, the filesystem
will always do a case insensitive search.
Directory segments are also resolved case-insensitively when they already exist on disk.
When "com_developer" is 1, the filesystem will warn when it catches bad directory
situations (regardless of the "fs_caseSensitiveOS" setting). Missing directories are
left unchanged so write paths can still create new content and failed reads report the
unresolved segment in debug output instead of relying on lowercase assumptions.

"additional mod path search":
fs_game_base can be used to set an additional search path
in search order, fs_game, fs_game_base, BASEGAME
for instance to base a mod of openQ4 + D3XP assets, fs_game mymod, fs_game_base baseoq4

=============================================================================
*/



// define to fix special-cases for GetPackStatus so that files that shipped in 
// the wrong place for openQ4 don't break pure servers.
#define DOOM3_PURE_SPECIAL_CASES	

typedef bool (*pureExclusionFunc_t)( const struct pureExclusion_s &excl, int l, const idStr &name );

typedef struct pureExclusion_s {
	int					nameLen;
	int					extLen;
	const char *		name;
	const char *		ext;
	pureExclusionFunc_t	func;
} pureExclusion_t;

bool excludeExtension( const pureExclusion_t &excl, int l, const idStr &name ) {
	if ( l > excl.extLen && !idStr::Icmp( name.c_str() + l - excl.extLen, excl.ext ) ) {
		return true;
	}
	return false;
}

bool excludePathPrefixAndExtension( const pureExclusion_t &excl, int l, const idStr &name ) {
	if ( l > excl.nameLen && !idStr::Icmp( name.c_str() + l - excl.extLen, excl.ext ) && !name.IcmpPrefixPath( excl.name ) ) {
		return true;
	}
	return false;
}

bool excludeFullName( const pureExclusion_t &excl, int l, const idStr &name ) {
	if ( l == excl.nameLen && !name.Icmp( excl.name ) ) {
		return true;
	}
	return false;
}

static pureExclusion_t pureExclusions[] = {
	{ 0,	0,	NULL,											"/",		excludeExtension },
	{ 0,	0,	NULL,											"\\",		excludeExtension },
	{ 0,	0,	NULL,											".pda",		excludeExtension },
	{ 0,	0,	NULL,											".gui",		excludeExtension },
	{ 0,	0,	NULL,											".pd",		excludeExtension },
	{ 0,	0,	NULL,											".lang",	excludeExtension },
	{ 0,	0,	"sound/VO",										".ogg",		excludePathPrefixAndExtension },
	{ 0,	0,	"sound/VO",										".wav",		excludePathPrefixAndExtension },
#if	defined DOOM3_PURE_SPECIAL_CASES	
	// add any special-case files or paths for pure servers here
	{ 0,	0,	"sound/ed/marscity/vo_intro_cutscene.ogg",		NULL,		excludeFullName },
	{ 0,	0,	"sound/weapons/soulcube/energize_01.ogg",		NULL,		excludeFullName },
	{ 0,	0,	"sound/xian/creepy/vocal_fx",					".ogg",		excludePathPrefixAndExtension },
	{ 0,	0,	"sound/xian/creepy/vocal_fx",					".wav",		excludePathPrefixAndExtension },
	{ 0,	0,	"sound/feedback",								".ogg",		excludePathPrefixAndExtension },
	{ 0,	0,	"sound/feedback",								".wav",		excludePathPrefixAndExtension },
	{ 0,	0,	"guis/assets/mainmenu/chnote.tga",				NULL,		excludeFullName },
	{ 0,	0,	"sound/levels/alphalabs2/uac_better_place.ogg",	NULL,		excludeFullName },
	{ 0,	0,	"textures/bigchars.tga",						NULL,		excludeFullName },
	{ 0,	0,	"dds/textures/bigchars.dds",					NULL,		excludeFullName },
	{ 0,	0,	"fonts",										".tga",		excludePathPrefixAndExtension },
	{ 0,	0,	"dds/fonts",									".dds",		excludePathPrefixAndExtension },
	{ 0,	0,	"default.cfg",									NULL,		excludeFullName },
	// russian zpak001.pk4
	{ 0,	0,  "fonts",										".dat",		excludePathPrefixAndExtension },
	{ 0,	0,	"guis/temp.guied",								NULL,		excludeFullName },
#endif
	{ 0,	0,	NULL,											NULL,		NULL }
};

// ensures that lengths for pure exclusions are correct
class idInitExclusions {
public:
	idInitExclusions() {
		for ( int i = 0; pureExclusions[i].func != NULL; i++ ) {
			if ( pureExclusions[i].name ) {
				pureExclusions[i].nameLen = idStr::Length( pureExclusions[i].name );
			}
			if ( pureExclusions[i].ext ) {
				pureExclusions[i].extLen = idStr::Length( pureExclusions[i].ext );
			}
		}
	}
};

static idInitExclusions	initExclusions;

typedef struct {
	const char *	name;
	unsigned int	checksum;
	bool			required;
	bool			pureBase;
} officialPk4Info_t;

static officialPk4Info_t officialPk4s[] = {
	// core retail media baseline for Quake 4
	{ "pak001.pk4",				0xf2cbc998,	true,	true },
	{ "pak002.pk4",				0x7f8d80d1,	true,	true },
	{ "pak003.pk4",				0x1b57b207,	true,	true },
	{ "pak004.pk4",				0x385aa578,	true,	true },
	{ "pak005.pk4",				0x60d50a1d,	true,	true },
	{ "pak006.pk4",				0x9099ed11,	true,	true },
	{ "pak007.pk4",				0xaf301fff,	true,	true },
	{ "pak008.pk4",				0x4ac6f6d9,	true,	true },
	{ "pak009.pk4",				0x36030c7d,	true,	true },
	{ "pak010.pk4",				0x4b80fbda,	true,	true },
	{ "pak011.pk4",				0x8acf4cfa,	true,	true },
	{ "pak012.pk4",				0xbe4120b0,	true,	true },
	{ "pak013.pk4",				0x6ad67f40,	true,	true },
	{ "pak014.pk4",				0xee51cd59,	true,	true },
	{ "pak015.pk4",				0xf5bf4e0c,	true,	true },
	{ "pak016.pk4",				0x2196f58c,	true,	true },
	{ "pak017.pk4",				0x91118a35,	true,	true },
	{ "pak018.pk4",				0x98a14f03,	true,	true },
	{ "pak019.pk4",				0xbc82ac79,	true,	true },
	{ "pak020.pk4",				0xce74cda5,	true,	true },
	{ "pak021.pk4",				0x2ba6e70c,	true,	true },
	{ "pak022.pk4",				0x4e390eec,	true,	true },

	// official patch/menu media, but not required by openQ4 startup
	{ "pak023.pk4",				0x7c1fd3a5,	false,	true },
	{ "pak024.pk4",				0x5546d551,	false,	true },
	{ "pak025.pk4",				0xcaeec1fd,	false,	true },

	// official but optional
	{ "q4cmp_pak001.pk4",		0xd0813943,	false,	false },
	{ "zpak_english.pk4",		0x5868f530,	false,	false },
	{ "zpak_english_01.pk4",	0xd9f04b8b,	false,	false },
	{ "zpak_english_02.pk4",	0x9dbd91fd,	false,	false },
	{ "zpak_english_03.pk4",	0x02eb6ad8,	false,	false },
	{ "zpak_english_04.pk4",	0xd3fefaa1,	false,	false },
	{ "zpak_english_05.pk4",	0x8596af60,	false,	false },
	{ "zpak_spanish.pk4",		0xb706e2b8,	false,	false },

	{ NULL,						0,			false,	false }
};

static bool FS_IsIgnoredOfficialGameBinaryPk4( const char *pakName ) {
	idStr name;

	if ( !pakName || !pakName[ 0 ] ) {
		return false;
	}

	name = pakName;
	name.StripPath();

	if ( !name.Icmp( "game000.pk4" ) ||
		 !name.Icmp( "game100.pk4" ) ||
		 !name.Icmp( "game200.pk4" ) ||
		 !name.Icmp( "game300.pk4" ) ) {
		return true;
	}

	return idStr::Filter( "gamex*.pk4", name.c_str(), false );
}

static const officialPk4Info_t *FindOfficialPk4Info( const char *pakName ) {
	for ( int i = 0; officialPk4s[ i ].name != NULL; i++ ) {
		if ( !idStr::Icmp( officialPk4s[ i ].name, pakName ) ) {
			return &officialPk4s[ i ];
		}
	}
	return NULL;
}

typedef struct {
	int		number;
	int		digitCount;
} numberedPakName_t;

static bool FS_ParseNumberedPakName( const char *pakName, numberedPakName_t &numberedPak ) {
	const char	*baseName;
	const char	*digits;
	const char	*p;
	int			number;
	int			digitCount;

	numberedPak.number = 0;
	numberedPak.digitCount = 0;

	if ( !pakName || !pakName[ 0 ] ) {
		return false;
	}

	baseName = pakName;
	for ( p = pakName; *p != '\0'; p++ ) {
		if ( *p == '/' || *p == '\\' ) {
			baseName = p + 1;
		}
	}
	pakName = baseName;

	if ( idStr::Icmpn( pakName, "pak", 3 ) ) {
		return false;
	}

	digits = pakName + 3;
	if ( digits[ 0 ] < '0' || digits[ 0 ] > '9' ) {
		return false;
	}

	number = 0;
	digitCount = 0;
	for ( p = digits; *p >= '0' && *p <= '9'; p++ ) {
		if ( number < 1000000 ) {
			number = ( number * 10 ) + ( *p - '0' );
		}
		digitCount++;
	}

	if ( idStr::Icmp( p, ".pk4" ) ) {
		return false;
	}

	numberedPak.number = number;
	numberedPak.digitCount = digitCount;
	return true;
}

static int FS_ComparePk4LoadOrder( const idStrPtr *a, const idStrPtr *b ) {
	numberedPakName_t	aPak;
	numberedPakName_t	bPak;
	const idStr			&aName = **a;
	const idStr			&bName = **b;
	const bool			aNumberedPak = FS_ParseNumberedPakName( aName.c_str(), aPak );
	const bool			bNumberedPak = FS_ParseNumberedPakName( bName.c_str(), bPak );

	if ( aNumberedPak && bNumberedPak ) {
		// AddGameDirectory inserts each later-loaded archive closer to the head of
		// the search path. Process wider numbered forms first so pak1.pk4 wins over
		// pak01.pk4/pak001.pk4 when both naming schemes are present.
		if ( aPak.digitCount != bPak.digitCount ) {
			return bPak.digitCount - aPak.digitCount;
		}
		if ( aPak.number != bPak.number ) {
			return aPak.number - bPak.number;
		}
	}

	if ( aNumberedPak != bNumberedPak ) {
		const char *aKey = aNumberedPak ? "pak" : aName.c_str();
		const char *bKey = bNumberedPak ? "pak" : bName.c_str();
		const int cmp = idStr::Icmp( aKey, bKey );
		if ( cmp != 0 ) {
			return cmp;
		}
		return aNumberedPak ? -1 : 1;
	}

	return aName.Icmp( bName );
}

static void FS_SortPk4FilesForLoadOrder( idStrList &pakfiles ) {
	idList<idStr>		other;
	idList<idStrPtr>	pointerList;

	if ( pakfiles.Num() <= 1 ) {
		return;
	}

	pointerList.SetNum( pakfiles.Num() );
	for ( int i = 0; i < pakfiles.Num(); i++ ) {
		pointerList[ i ] = &pakfiles[ i ];
	}
	pointerList.Sort( FS_ComparePk4LoadOrder );

	other.SetNum( pakfiles.Num() );
	other.SetGranularity( pakfiles.GetGranularity() );
	for ( int i = 0; i < other.Num(); i++ ) {
		other[ i ] = *pointerList[ i ];
	}

	pakfiles.Swap( other );
}

static const char *fsLanguagePackOrder[] = {
	"english",
	"spanish",
	"french",
	"italian",
	"german",
	"russian",
	"polish",
	"korean",
	"japanese",
	"chinese",
	NULL
};

static bool FS_IsKnownLanguage( const char *language ) {
	if ( language == NULL || language[ 0 ] == '\0' ) {
		return false;
	}

	for ( int i = 0; fsLanguagePackOrder[ i ] != NULL; ++i ) {
		if ( !idStr::Icmp( language, fsLanguagePackOrder[ i ] ) ) {
			return true;
		}
	}

	return false;
}

static bool FS_LanguageListContains( const idStrList &languages, const char *language ) {
	if ( language == NULL || language[ 0 ] == '\0' ) {
		return false;
	}
	for ( int i = 0; i < languages.Num(); ++i ) {
		if ( !languages[ i ].Icmp( language ) ) {
			return true;
		}
	}
	return false;
}

static void FS_AppendUniqueLanguage( idStrList &languages, const char *language ) {
	if ( language == NULL || language[ 0 ] == '\0' || FS_LanguageListContains( languages, language ) ) {
		return;
	}
	languages.Append( language );
}

static bool FS_ParseLanguagePackName( const char *pakFilename, idStr &language, bool allowPatchArchives = true ) {
	idStr fileName;
	int suffix;

	if ( pakFilename == NULL || pakFilename[ 0 ] == '\0' ) {
		return false;
	}

	fileName = pakFilename;
	fileName.StripPath();
	fileName.ToLower();
	if ( !fileName.CheckExtension( ".pk4" ) || fileName.Icmpn( "zpak_", 5 ) ) {
		return false;
	}

	fileName.StripFileExtension();
	language = fileName.Right( fileName.Length() - 5 );
	suffix = language.Find( '_' );
	if ( suffix == 0 ) {
		return false;
	}
	if ( suffix > 0 ) {
		if ( !allowPatchArchives ) {
			return false;
		}
		language.CapLength( suffix );
	}

	return FS_IsKnownLanguage( language.c_str() );
}

static bool FS_PakPathIsInGameDir( const char *pakFilename, const char *gameDir ) {
	idStr directory;
	idStr dirName;

	if ( pakFilename == NULL || gameDir == NULL ) {
		return false;
	}

	directory = pakFilename;
	directory.StripFilename();
	dirName = directory;
	dirName.StripPath();
	return !dirName.Icmp( gameDir );
}

static void FS_OrderLanguagePackList( idStrList &languages ) {
	idStrList ordered;

	for ( int i = 0; fsLanguagePackOrder[ i ] != NULL; ++i ) {
		if ( FS_LanguageListContains( languages, fsLanguagePackOrder[ i ] ) ) {
			ordered.Append( fsLanguagePackOrder[ i ] );
		}
	}

	for ( int i = 0; i < languages.Num(); ++i ) {
		FS_AppendUniqueLanguage( ordered, languages[ i ].c_str() );
	}

	languages.Swap( ordered );
}

static bool FS_FileExists( const char *path ) {
	FILE *f;
	if ( !path || !path[ 0 ] ) {
		return false;
	}
	f = fopen( path, "rb" );
	if ( f ) {
		fclose( f );
		return true;
	}
	return false;
}

static void FS_AddUniquePath( idStrList &paths, const char *path ) {
	idStr normalized;
	idStr existing;

	if ( !path || !path[ 0 ] ) {
		return;
	}
	normalized = path;
	normalized.Replace( "\\\\", "\\" );
	normalized.BackSlashesToSlashes();
	normalized.StripTrailing( '/' );
	if ( !normalized.Length() ) {
		return;
	}
	for ( int i = 0; i < paths.Num(); i++ ) {
		existing = paths[ i ];
		existing.Replace( "\\\\", "\\" );
		existing.BackSlashesToSlashes();
		existing.StripTrailing( '/' );
		if ( !idStr::IcmpPath( existing.c_str(), normalized.c_str() ) ) {
			return;
		}
	}
	paths.Append( normalized );
}

static bool FS_IsEnvPathListSeparator( char c ) {
#ifdef WIN32
	return c == ';';
#else
	return c == ':' || c == ';';
#endif
}

static void FS_AppendEnvPathList( idStrList &paths, const char *envName ) {
	const char *value = getenv( envName );
	if ( !value || !value[ 0 ] ) {
		return;
	}

	idStr valueList = value;
	const int length = valueList.Length();
	int start = 0;
	for ( int i = 0; i <= length; i++ ) {
		if ( i == length || FS_IsEnvPathListSeparator( valueList[ i ] ) ) {
			if ( i > start ) {
				idStr path = valueList.Mid( start, i - start );
				path.Strip( ' ' );
				path.Strip( '\t' );
				path.Strip( '\"' );
				FS_AddUniquePath( paths, path.c_str() );
			}
			start = i + 1;
		}
	}
}

static void FS_LogPathList( const char *label, const idStrList &paths ) {
	if ( common == NULL ) {
		return;
	}
	common->Printf( "%s (%d):\n", label, paths.Num() );
	for ( int i = 0; i < paths.Num(); i++ ) {
		common->Printf( "  %s\n", paths[ i ].c_str() );
	}
}

static bool FS_HasGameFilesAtGameDirPath( const char *gameDirPath ) {
	idStr pakPath;

	if ( !gameDirPath || !gameDirPath[ 0 ] ) {
		return false;
	}

	pakPath = gameDirPath;
	pakPath.AppendPath( "pak001.pk4" );
	return FS_FileExists( pakPath.c_str() );
}

static bool FS_TryResolveBasePathCandidate( const char *candidatePath, idStr &resolvedBasePath ) {
	idStr normalized;
	idStr gameDirPath;
	idStr parentPath;

	if ( !candidatePath || !candidatePath[ 0 ] ) {
		return false;
	}

	normalized = candidatePath;
	normalized.Replace( "\\\\", "\\" );
	normalized.BackSlashesToSlashes();
	normalized.StripTrailing( '/' );
	if ( !normalized.Length() ) {
		return false;
	}

	// Candidate is an install root containing BASE_GAMEDIR.
	gameDirPath = normalized;
	gameDirPath.AppendPath( BASE_GAMEDIR );
	if ( FS_HasGameFilesAtGameDirPath( gameDirPath.c_str() ) ) {
		resolvedBasePath = normalized;
		return true;
	}

	// Candidate may already point directly at BASE_GAMEDIR.
	if ( FS_HasGameFilesAtGameDirPath( normalized.c_str() ) ) {
		parentPath = normalized;
		parentPath.StripFilename();
		parentPath.StripTrailing( '/' );
		if ( parentPath.Length() ) {
			resolvedBasePath = parentPath;
			return true;
		}
	}

	return false;
}

static bool FS_HasGameFilesAtBasePath( const char *basePath ) {
	idStr resolvedBasePath;
	return FS_TryResolveBasePathCandidate( basePath, resolvedBasePath );
}

static bool FS_GetCurrentWorkingDirectory( idStr &cwd ) {
	char buf[ MAX_OSPATH ];
#ifdef WIN32
	if ( !_getcwd( buf, sizeof( buf ) - 1 ) ) {
		return false;
	}
#else
	if ( !getcwd( buf, sizeof( buf ) - 1 ) ) {
		return false;
	}
#endif
	buf[ sizeof( buf ) - 1 ] = '\0';
	cwd = buf;
	return true;
}

static void FS_ExtractQuotedTokens( const char *line, idStrList &tokens ) {
	const char	*p;
	const char	*start;
	char		token[ 2048 ];
	int			len;

	tokens.Clear();
	if ( !line ) {
		return;
	}
	p = line;
	while ( ( p = strchr( p, '\"' ) ) != NULL ) {
		start = ++p;
		while ( *p && *p != '\"' ) {
			p++;
		}
		if ( *p != '\"' ) {
			break;
		}
		len = (int)( p - start );
		if ( len > 0 ) {
			if ( len >= (int)sizeof( token ) ) {
				len = sizeof( token ) - 1;
			}
			memcpy( token, start, len );
			token[ len ] = '\0';
			tokens.Append( token );
		}
		p++;
	}
}

#ifdef WIN32
static bool FS_ReadRegistryString( HKEY root, const char *subKey, const char *valueName, REGSAM accessFlags, idStr &result ) {
	HKEY	hKey;
	LONG	status;
	BYTE	buffer[ 4096 ];
	DWORD	type;
	DWORD	size;

	result.Clear();
	if ( !subKey || !subKey[ 0 ] || !valueName || !valueName[ 0 ] ) {
		return false;
	}

	status = RegOpenKeyExA( root, subKey, 0, KEY_READ | accessFlags, &hKey );
	if ( status != ERROR_SUCCESS ) {
		return false;
	}

	type = 0;
	size = sizeof( buffer ) - 1;
	status = RegQueryValueExA( hKey, valueName, NULL, &type, buffer, &size );
	RegCloseKey( hKey );
	if ( status != ERROR_SUCCESS || size == 0 ) {
		return false;
	}
	if ( type != REG_SZ && type != REG_EXPAND_SZ ) {
		return false;
	}

	buffer[ size < sizeof( buffer ) ? size : ( sizeof( buffer ) - 1 ) ] = '\0';
	result = (const char *)buffer;
	if ( type == REG_EXPAND_SZ ) {
		char expanded[ 4096 ];
		DWORD expandedLen = ExpandEnvironmentStringsA( result.c_str(), expanded, sizeof( expanded ) );
		if ( expandedLen > 0 && expandedLen <= sizeof( expanded ) ) {
			expanded[ sizeof( expanded ) - 1 ] = '\0';
			result = expanded;
		}
	}
	result.Replace( "\\\\", "\\" );
	result.BackSlashesToSlashes();
	result.StripTrailing( '/' );
	return result.Length() > 0;
}

static void FS_AppendGogPathsFromRegistryGamesBranch( HKEY root, const char *branch, idStrList &candidates, REGSAM accessFlags ) {
	HKEY	hKey;
	LONG	status;
	DWORD	index;
	char	subKeyName[ 256 ];
	DWORD	subKeyNameLen;
	idStr	subKeyPath;
	idStr	pathValue;

	if ( !branch || !branch[ 0 ] ) {
		return;
	}

	status = RegOpenKeyExA( root, branch, 0, KEY_READ | accessFlags, &hKey );
	if ( status != ERROR_SUCCESS ) {
		return;
	}

	index = 0;
	for ( ;; ) {
		subKeyNameLen = sizeof( subKeyName );
		status = RegEnumKeyExA( hKey, index, subKeyName, &subKeyNameLen, NULL, NULL, NULL, NULL );
		if ( status != ERROR_SUCCESS ) {
			break;
		}

		subKeyPath = branch;
		subKeyPath += "\\";
		subKeyPath += subKeyName;
		if ( FS_ReadRegistryString( root, subKeyPath.c_str(), "path", accessFlags, pathValue ) ) {
			FS_AddUniquePath( candidates, pathValue.c_str() );
		}

		index++;
	}

	RegCloseKey( hKey );
}

static void FS_AppendGogPathsFromRegistryUninstallBranch( HKEY root, const char *branch, idStrList &candidates, REGSAM accessFlags ) {
	HKEY	hKey;
	LONG	status;
	DWORD	index;
	char	subKeyName[ 256 ];
	DWORD	subKeyNameLen;
	idStr	subKeyPath;
	idStr	displayName;
	idStr	publisher;
	idStr	installLocation;
	bool	matchesQuake4;
	bool	matchesGog;

	if ( !branch || !branch[ 0 ] ) {
		return;
	}

	status = RegOpenKeyExA( root, branch, 0, KEY_READ | accessFlags, &hKey );
	if ( status != ERROR_SUCCESS ) {
		return;
	}

	index = 0;
	for ( ;; ) {
		subKeyNameLen = sizeof( subKeyName );
		status = RegEnumKeyExA( hKey, index, subKeyName, &subKeyNameLen, NULL, NULL, NULL, NULL );
		if ( status != ERROR_SUCCESS ) {
			break;
		}

		subKeyPath = branch;
		subKeyPath += "\\";
		subKeyPath += subKeyName;
		if ( !FS_ReadRegistryString( root, subKeyPath.c_str(), "InstallLocation", accessFlags, installLocation ) ) {
			index++;
			continue;
		}

		displayName.Clear();
		publisher.Clear();
		FS_ReadRegistryString( root, subKeyPath.c_str(), "DisplayName", accessFlags, displayName );
		FS_ReadRegistryString( root, subKeyPath.c_str(), "Publisher", accessFlags, publisher );

		matchesQuake4 =
			( displayName.Find( "Quake 4", false ) >= 0 ) ||
			( displayName.Find( "Quake IV", false ) >= 0 ) ||
			( subKeyPath.Find( "Quake 4", false ) >= 0 ) ||
			( subKeyPath.Find( "Quake IV", false ) >= 0 );
		matchesGog =
			( publisher.Find( "GOG", false ) >= 0 ) ||
			( subKeyPath.Find( "GOG", false ) >= 0 );

		if ( matchesQuake4 || ( matchesGog && installLocation.Find( "Quake", false ) >= 0 ) ) {
			FS_AddUniquePath( candidates, installLocation.c_str() );
		}

		index++;
	}

	RegCloseKey( hKey );
}

static void FS_AppendGogInstallCandidatesFromRegistry( idStrList &candidates ) {
	FS_AppendGogPathsFromRegistryGamesBranch( HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games", candidates, KEY_WOW64_64KEY );
	FS_AppendGogPathsFromRegistryGamesBranch( HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games", candidates, KEY_WOW64_32KEY );
	FS_AppendGogPathsFromRegistryGamesBranch( HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\GOG.com\\Games", candidates, 0 );
	FS_AppendGogPathsFromRegistryGamesBranch( HKEY_CURRENT_USER, "SOFTWARE\\GOG.com\\Games", candidates, KEY_WOW64_64KEY );
	FS_AppendGogPathsFromRegistryGamesBranch( HKEY_CURRENT_USER, "SOFTWARE\\GOG.com\\Games", candidates, KEY_WOW64_32KEY );

	FS_AppendGogPathsFromRegistryUninstallBranch( HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", candidates, KEY_WOW64_64KEY );
	FS_AppendGogPathsFromRegistryUninstallBranch( HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", candidates, KEY_WOW64_32KEY );
	FS_AppendGogPathsFromRegistryUninstallBranch( HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", candidates, KEY_WOW64_64KEY );
	FS_AppendGogPathsFromRegistryUninstallBranch( HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", candidates, KEY_WOW64_32KEY );
}
#endif

static void FS_AppendSteamLibrariesFromVdf( const char *steamRoot, idStrList &libraryRoots ) {
	idStr		vdfPath;
	FILE		*f;
	char		line[ 4096 ];
	idStrList	quoted;
	idStr		key;
	idStr		value;

	if ( !steamRoot || !steamRoot[ 0 ] ) {
		return;
	}

	vdfPath = steamRoot;
	vdfPath.AppendPath( "steamapps" );
	vdfPath.AppendPath( "libraryfolders.vdf" );
	f = fopen( vdfPath.c_str(), "rb" );
	if ( !f ) {
		return;
	}

	while ( fgets( line, sizeof( line ), f ) != NULL ) {
		FS_ExtractQuotedTokens( line, quoted );
		if ( quoted.Num() < 2 ) {
			continue;
		}
		key = quoted[ 0 ];
		value = quoted[ 1 ];
		value.Replace( "\\\\", "\\" );
		value.BackSlashesToSlashes();
		value.StripTrailing( '/' );

		if ( !key.Icmp( "path" ) ) {
			FS_AddUniquePath( libraryRoots, value.c_str() );
			continue;
		}

		if ( idStr::IsNumeric( key.c_str() ) &&
			( value.Find( "/" ) >= 0 || value.Find( "\\" ) >= 0 || value.Find( ":" ) >= 0 ) ) {
			FS_AddUniquePath( libraryRoots, value.c_str() );
		}
	}
	fclose( f );
}

static void FS_BuildSteamInstallCandidates( idStrList &candidates ) {
	idStrList	steamRoots;
	idStrList	explicitLibraryRoots;
	idStrList	discoveryLibraryRoots;
	idStrList	libraryRoots;
	idStr		path;
	const char	*envPath;

	candidates.Clear();

	FS_AppendEnvPathList( candidates, "OPENQ4_QUAKE4_PATH" );
	FS_AppendEnvPathList( candidates, "OPENQ4_QUAKE4_ROOT" );
	FS_AppendEnvPathList( steamRoots, "OPENQ4_STEAM_ROOT" );
	FS_AppendEnvPathList( steamRoots, "OPENQ4_STEAM_ROOTS" );
	FS_AppendEnvPathList( steamRoots, "STEAM_COMPAT_CLIENT_INSTALL_PATH" );
	FS_AppendEnvPathList( explicitLibraryRoots, "OPENQ4_STEAM_LIBRARY" );
	FS_AppendEnvPathList( explicitLibraryRoots, "OPENQ4_STEAM_LIBRARIES" );

#ifdef WIN32
	FS_AddUniquePath( steamRoots, "C:/Program Files (x86)/Steam" );
	FS_AddUniquePath( steamRoots, "C:/Program Files/Steam" );

	envPath = getenv( "ProgramFiles(x86)" );
	if ( envPath && envPath[ 0 ] ) {
		path = envPath;
		path.AppendPath( "Steam" );
		FS_AddUniquePath( steamRoots, path.c_str() );
	}
	envPath = getenv( "ProgramFiles" );
	if ( envPath && envPath[ 0 ] ) {
		path = envPath;
		path.AppendPath( "Steam" );
		FS_AddUniquePath( steamRoots, path.c_str() );
	}
	envPath = getenv( "ProgramW6432" );
	if ( envPath && envPath[ 0 ] ) {
		path = envPath;
		path.AppendPath( "Steam" );
		FS_AddUniquePath( steamRoots, path.c_str() );
	}
	envPath = getenv( "LOCALAPPDATA" );
	if ( envPath && envPath[ 0 ] ) {
		path = envPath;
		path.AppendPath( "Steam" );
		FS_AddUniquePath( steamRoots, path.c_str() );
	}
#elif defined( __APPLE__ )
	envPath = getenv( "HOME" );
	if ( envPath && envPath[ 0 ] ) {
		path = envPath;
		path.AppendPath( "Library" );
		path.AppendPath( "Application Support" );
		path.AppendPath( "Steam" );
		FS_AddUniquePath( steamRoots, path.c_str() );
	}
#else
	envPath = getenv( "XDG_DATA_HOME" );
	if ( envPath && envPath[ 0 ] ) {
		path = envPath;
		path.AppendPath( "Steam" );
		FS_AddUniquePath( steamRoots, path.c_str() );
	}
	envPath = getenv( "HOME" );
	if ( envPath && envPath[ 0 ] ) {
		path = envPath;
		path.AppendPath( ".steam" );
		path.AppendPath( "steam" );
		FS_AddUniquePath( steamRoots, path.c_str() );

		path = envPath;
		path.AppendPath( ".steam" );
		path.AppendPath( "root" );
		FS_AddUniquePath( steamRoots, path.c_str() );

		path = envPath;
		path.AppendPath( ".local" );
		path.AppendPath( "share" );
		path.AppendPath( "Steam" );
		FS_AddUniquePath( steamRoots, path.c_str() );

		path = envPath;
		path.AppendPath( ".var" );
		path.AppendPath( "app" );
		path.AppendPath( "com.valvesoftware.Steam" );
		path.AppendPath( ".local" );
		path.AppendPath( "share" );
		path.AppendPath( "Steam" );
		FS_AddUniquePath( steamRoots, path.c_str() );
	}
#endif

	for ( int i = 0; i < explicitLibraryRoots.Num(); i++ ) {
		FS_AddUniquePath( discoveryLibraryRoots, explicitLibraryRoots[ i ] );
		path = explicitLibraryRoots[ i ];
		path.AppendPath( "steamapps" );
		path.AppendPath( "common" );
		path.AppendPath( "Quake 4" );
		FS_AddUniquePath( candidates, path.c_str() );
	}

	for ( int i = 0; i < steamRoots.Num(); i++ ) {
		libraryRoots.Clear();
		FS_AddUniquePath( libraryRoots, steamRoots[ i ] );
		FS_AppendSteamLibrariesFromVdf( steamRoots[ i ], libraryRoots );
		for ( int j = 0; j < libraryRoots.Num(); j++ ) {
			FS_AddUniquePath( discoveryLibraryRoots, libraryRoots[ j ] );
			path = libraryRoots[ j ];
			path.AppendPath( "steamapps" );
			path.AppendPath( "common" );
			path.AppendPath( "Quake 4" );
			FS_AddUniquePath( candidates, path.c_str() );
		}
	}

	FS_LogPathList( "Steam install discovery roots", steamRoots );
	FS_LogPathList( "Steam explicit library roots", explicitLibraryRoots );
	FS_LogPathList( "Steam library roots to probe", discoveryLibraryRoots );
	FS_LogPathList( "Steam Quake 4 install candidates", candidates );
}

static void FS_BuildGogInstallCandidates( idStrList &candidates ) {
	idStr		path;
	const char	*envPath;

	candidates.Clear();

#ifdef WIN32
	FS_AppendGogInstallCandidatesFromRegistry( candidates );

	FS_AddUniquePath( candidates, "C:/Program Files (x86)/GOG Galaxy/Games/Quake 4" );
	FS_AddUniquePath( candidates, "C:/Program Files/GOG Galaxy/Games/Quake 4" );
	FS_AddUniquePath( candidates, "C:/GOG Games/Quake 4" );

	envPath = getenv( "ProgramFiles(x86)" );
	if ( envPath && envPath[ 0 ] ) {
		path = envPath;
		path.AppendPath( "GOG Galaxy" );
		path.AppendPath( "Games" );
		path.AppendPath( "Quake 4" );
		FS_AddUniquePath( candidates, path.c_str() );
	}
	envPath = getenv( "ProgramFiles" );
	if ( envPath && envPath[ 0 ] ) {
		path = envPath;
		path.AppendPath( "GOG Galaxy" );
		path.AppendPath( "Games" );
		path.AppendPath( "Quake 4" );
		FS_AddUniquePath( candidates, path.c_str() );
	}
	envPath = getenv( "SystemDrive" );
	if ( envPath && envPath[ 0 ] ) {
		path = envPath;
		path.AppendPath( "GOG Games" );
		path.AppendPath( "Quake 4" );
		FS_AddUniquePath( candidates, path.c_str() );
	}
#else
	envPath = getenv( "HOME" );
	if ( envPath && envPath[ 0 ] ) {
		path = envPath;
		path.AppendPath( "GOG Games" );
		path.AppendPath( "Quake 4" );
		FS_AddUniquePath( candidates, path.c_str() );

		path = envPath;
		path.AppendPath( "Games" );
		path.AppendPath( "GOG Games" );
		path.AppendPath( "Quake 4" );
		FS_AddUniquePath( candidates, path.c_str() );
	}
#endif
}

static bool FS_FindFirstValidInstallPath( const idStrList &candidates, idStr &result ) {
	idStr resolvedBasePath;

	for ( int i = 0; i < candidates.Num(); i++ ) {
		if ( FS_TryResolveBasePathCandidate( candidates[ i ], resolvedBasePath ) ) {
			result = resolvedBasePath;
			return true;
		}
	}
	return false;
}

static bool FS_AutoDiscoverBasePath( idStr &basePath ) {
	idStr		cwd;
	idStrList	candidates;
	idStr		resolvedBasePath;

	if ( FS_GetCurrentWorkingDirectory( cwd ) && FS_TryResolveBasePathCandidate( cwd.c_str(), resolvedBasePath ) ) {
		basePath = resolvedBasePath;
		return true;
	}

	FS_BuildSteamInstallCandidates( candidates );
	if ( FS_FindFirstValidInstallPath( candidates, basePath ) ) {
		return true;
	}

	FS_BuildGogInstallCandidates( candidates );
	if ( FS_FindFirstValidInstallPath( candidates, basePath ) ) {
		return true;
	}

	return false;
}

#define MAX_ZIPPED_FILE_NAME	2048
#define FILE_HASH_SIZE			1024

typedef struct fileInPack_s {
	idStr				name;						// name of the file
	uint32_t			pos;						// classic-ZIP central-directory position
	struct fileInPack_s * next;						// next file in the hash
} fileInPack_t;

typedef enum {
	BINARY_UNKNOWN = 0,
	BINARY_YES,
	BINARY_NO
} binaryStatus_t;

typedef enum {
	PURE_UNKNOWN = 0,	// need to run the pak through GetPackStatus
	PURE_NEUTRAL,	// neutral regarding pureness. gets in the pure list if referenced
	PURE_ALWAYS,	// always referenced - for pak* named files, unless NEVER
	PURE_NEVER		// VO paks. may be referenced, won't be in the pure lists
} pureStatus_t;

typedef struct {
	idList<int>			depends;
	idList<idDict *>	mapDecls;
} addonInfo_t;

typedef struct {
	idStr				pakFilename;				// c:\doom\base\pak0.pk4
	unzFile				handle;
	int					checksum;
	int					numfiles;
	int					length;
	bool				referenced;
	binaryStatus_t		binary;
	bool				addon;						// this is an addon pack - addon_search tells if it's 'active'
	bool				addon_search;				// is in the search list
	addonInfo_t			*addon_info;
	pureStatus_t		pureStatus;
	bool				isNew;						// for downloaded paks
	fileInPack_t		*hashTable[FILE_HASH_SIZE];
	fileInPack_t		*buildBuffer;
} pack_t;

typedef struct {
	idStr				path;						// c:\doom
	idStr				gamedir;					// base
} directory_t;

typedef struct searchpath_s {
	pack_t *			pack;						// only one of pack / dir will be non NULL
	directory_t *		dir;
	struct searchpath_s *next;
} searchpath_t;

// search flags when opening a file
#define FSFLAG_SEARCH_DIRS		( 1 << 0 )
#define FSFLAG_SEARCH_PAKS		( 1 << 1 )
#define FSFLAG_PURE_NOREF		( 1 << 2 )
#define FSFLAG_BINARY_ONLY		( 1 << 3 )
#define FSFLAG_SEARCH_ADDONS	( 1 << 4 )

// 3 search path (fs_savepath fs_basepath fs_cdpath)
// + .jpg and .tga
#define MAX_CACHED_DIRS 6

#define BINARY_CONFIG "binary.conf"
#define ADDON_CONFIG "addon.conf"

// Protocol 2.41 identifies compatible game code with the official 1.4.2
// q4base/game300.pk4 checksum. openQ4 never loads executable code from that
// archive; the unchanged value is a platform-independent wire token for a
// module that FindDLL resolved from the trusted package/module roots.
static const int OPENQ4_Q4_142_GAME300_PAK_CHECKSUM = 0x68fb90b1;

typedef enum {
	GAME_MODULE_ORIGIN_NONE = 0,
	GAME_MODULE_ORIGIN_ACTIVE_MOD,
	GAME_MODULE_ORIGIN_BASE_GAME,
	GAME_MODULE_ORIGIN_PACKAGE_ROOT
} gameModuleOrigin_t;

class idDEntry : public idStrList {
public:
						idDEntry() {}
	virtual				~idDEntry() {}

	bool				Matches( const char *directory, const char *extension ) const;
	void				Init( const char *directory, const char *extension, const idStrList &list );
	void				Clear( void );

private:
	idStr				directory;
	idStr				extension;
};

typedef enum modManifestStatus_s {
	MOD_MANIFEST_MISSING,
	MOD_MANIFEST_VALID,
	MOD_MANIFEST_INVALID
} modManifestStatus_t;

class idLevelLoadCacheManager;

class idFileSystemLocal : public idFileSystem {
public:
							idFileSystemLocal( void );

	virtual void			Init( void );
	virtual void			StartBackgroundDownloadThread( void );
	virtual void			StopBackgroundDownloadThread( void );
	virtual void			Restart( void );
	virtual void			Shutdown( bool reloading );
	virtual bool			IsInitialized( void ) const;
	virtual bool			PerformingCopyFiles( void ) const;
	virtual idModList *		ListMods( void );
	virtual bool			GetModInfo( const char *modDir, idModInfo &modInfo, idStr *reason = NULL );
	virtual void			FreeModList( idModList *modList );
	virtual idFileList *	ListFiles( const char *relativePath, const char *extension, bool sort = false, bool fullRelativePath = false, const char* gamedir = NULL );
	virtual idFileList *	ListFilesTree( const char *relativePath, const char *extension, bool sort = false, const char* gamedir = NULL );
	virtual void			FreeFileList( idFileList *fileList );
	virtual void			ListAvailableLanguagePacks( idStrList &languages );
	bool					HasBaseLanguageMediaPack( void );
	virtual const char *	OSPathToRelativePath( const char *OSPath );
	virtual const char *	RelativePathToOSPath( const char *relativePath, const char *basePath );
	virtual const char *	BuildOSPath( const char *base, const char *game, const char *relativePath );
	virtual void			CreateOSPath( const char *OSPath );
	virtual bool			FileIsInPAK( const char *relativePath );
	virtual bool			InProductionMode() { return /*(resourceFiles.Num() > 0) |*/ (com_productionMode.GetInteger() != 0); }
	virtual void			UpdatePureServerChecksums( void );
	virtual bool			UpdateGamePakChecksums( void );
	virtual fsPureReply_t	SetPureServerChecksums( const int pureChecksums[ MAX_PURE_PAKS ], int gamePakChecksum, int missingChecksums[ MAX_PURE_PAKS ], int *missingGamePakChecksum );
	virtual void			GetPureServerChecksums( int checksums[ MAX_PURE_PAKS ], int OS, int *gamePakChecksum );
	virtual void			SetRestartChecksums( const int pureChecksums[ MAX_PURE_PAKS ], int gamePakChecksum );
	virtual	void			ClearPureChecksums( void );
	virtual int				GetOSMask( void );
	virtual int				ReadFile( const char *relativePath, void **buffer, ID_TIME_T *timestamp );
	virtual void			FreeFile( void *buffer );
	virtual int				WriteFile( const char *relativePath, const void *buffer, int size, const char *basePath = "fs_savepath" );
	virtual bool			PromoteFile( const char *stagedRelativePath, const char *finalRelativePath,
								const char *basePath = "fs_savepath" );
	virtual bool			RemoveFileChecked( const char *relativePath,
								const char *basePath = "fs_savepath" );
	virtual void			RemoveFile( const char *relativePath, const char *basePath = "fs_savepath" );
	virtual int				RemoveExplicitFile( const char *OSPath );
	virtual void			SetIsFileLoadingAllowed( bool mode );
	virtual bool			GetIsFileLoadingAllowed() const;
	virtual void			SetAssetLogName( const char *logName );
	virtual void			WriteAssetLog();
	virtual void			ClearAssetLog();
	virtual const char *	GetAssetLogName();
	virtual void			BeginLevelLoadCache( const char *mapKey, const char *gameMode,
								const char *entityFilter, const char *settingsKey );
	virtual void			FinishLevelLoadCache( bool successful );
	void					ReleaseLevelLoadCache( void );
	virtual void			CancelLevelLoadCache( void );
	virtual void			RecordLevelLoadResource( levelLoadResourceType_t type,
								const char *name, const char *options,
								unsigned int flags, unsigned int priority );
	virtual idFile *		OpenGeneratedCacheRead( generatedCacheKind_t kind,
								const char *sourcePath, unsigned int parserVersion,
								const char *settingsKey );
	virtual bool			WriteGeneratedCache( generatedCacheKind_t kind,
								const char *sourcePath, unsigned int parserVersion,
								const char *settingsKey, const void *payload,
								unsigned int payloadBytes );
	virtual void			DiscardGeneratedCache( generatedCacheKind_t kind,
								const char *sourcePath, unsigned int parserVersion,
								const char *settingsKey );
	virtual idFile *		GetNewFileMemory( void );
	virtual idFile *		GetNewFilePermanent( void );
	virtual idFile *		OpenFileReadFlags( const char *relativePath, int searchFlags, pack_t **foundInPak = NULL, bool allowCopyFiles = true, const char* gamedir = NULL );
	virtual idFile *		OpenFileRead( const char *relativePath, bool allowCopyFiles = true, const char* gamedir = NULL );
	virtual idFile *		OpenFileReadFromPak( const char *relativePath, bool allowCopyFiles = true, const char* gamedir = NULL );
	virtual idFile *		OpenFileWrite( const char *relativePath, const char *basePath = "fs_savepath" );
	virtual idFile *		OpenFileAppend( const char *relativePath, bool sync = false, const char *basePath = "fs_basepath"   );
	virtual idFile *		OpenFileByMode( const char *relativePath, fsMode_t mode );
	virtual idFile *		OpenExplicitFileRead( const char *OSPath );
	virtual idFile *		OpenExplicitFileWrite( const char *OSPath );
	virtual void			CloseFile( idFile *f );
	virtual void			BackgroundDownload( backgroundDownload_t *bgl );
	virtual void			ResetReadCount( void );
	virtual void			AddToReadCount( int c );
	virtual int				GetReadCount( void );
	virtual void			FindDLL( const char *basename, char dllPath[ MAX_OSPATH ], bool updateChecksum );
	virtual void			ClearDirCache( void );
	virtual bool			HasD3XP( void );
	virtual bool			RunningD3XP( void );
	virtual void			CopyFile( const char *fromOSPath, const char *toOSPath );
	virtual int				ValidateDownloadPakForChecksum( int checksum, char path[ MAX_STRING_CHARS ], bool isBinary );
	virtual idFile *		MakeTemporaryFile( void );
	virtual int				AddZipFile( const char *path );
	virtual findFile_t		FindFile( const char *path, bool scheduleAddons );
	virtual int				GetNumMaps();
	virtual const idDict *	GetMapDecl( int i );
	virtual void			FindMapScreenshot( const char *path, char *buf, int len );
	virtual bool			FilenameCompare( const char *s1, const char *s2 ) const;

	static void				Dir_f( const idCmdArgs &args );
	static void				DirTree_f( const idCmdArgs &args );
	static void				Path_f( const idCmdArgs &args );
	static void				TouchFile_f( const idCmdArgs &args );
	static void				TouchFileList_f( const idCmdArgs &args );

private:
	friend dword 			BackgroundDownloadThread( void *parms );

	searchpath_t *			searchPaths;
	int						readCount;			// total bytes read
	// bytes read since the last loading-pacifier offer, see AddToReadCount
	static const int		READCOUNT_PACIFIER_INTERVAL_BYTES = 1024 * 1024;
	int						readCountPacifierBytes;
	std::mutex				readCountMutex;
	std::thread::id			fileSystemMainThread;
	int						loadCount;			// total files read
	int						loadStack;			// total files in memory
	idStr					gameFolder;			// this will be a single name without separators

	searchpath_t			*addonPaks;			// not loaded up, but we saw them

	idDict					mapDict;			// for GetMapDecl

	static idCVar			fs_debug;
	static idCVar			fs_restrict;
	static idCVar			fs_copyfiles;
	static idCVar			fs_basepath;
	static idCVar			fs_homepath;
	static idCVar			fs_savepath;
	static idCVar			fs_cdpath;
	static idCVar			fs_game;
	static idCVar			fs_game_base;
	static idCVar			fs_caseSensitiveOS;
	static idCVar			fs_searchAddons;
	static idCVar			fs_validateOfficialPaks;

	backgroundDownload_t *	backgroundDownloads;
	backgroundDownload_t	defaultBackgroundDownload;
	xthreadInfo				backgroundThread;

	idList<pack_t *>		serverPaks;
	bool					loadedFileFromDir;		// set to true once a file was loaded from a directory - can't switch to pure anymore
	idList<int>				restartChecksums;		// used during a restart to set things in right order
	idList<int>				addonChecksums;			// list of checksums that should go to the search list directly ( for restarts )
	int						restartGamePakChecksum;
	int						gameDLLChecksum;		// the checksum of the last loaded game DLL
	int						gamePakChecksum;		// the checksum of the pak holding the loaded game DLL
	gameModuleOrigin_t		gameModuleOrigin;		// trusted root and active-mod provenance
	bool					isFileLoadingAllowed;
	idStr					currentAssetLog;
	idStr					currentAssetLogUnfiltered;
	idStrList				assetLog;
	idLevelLoadCacheManager *levelLoadCache;

	int						gamePakForOS[ MAX_GAME_OS ];

	idDEntry				dir_cache[ MAX_CACHED_DIRS ]; // fifo
	int						dir_cache_index;
	int						dir_cache_count;

	int						d3xp;	// 0: didn't check, -1: not installed, 1: installed

private:
	void					ReplaceSeparators( idStr &path, char sep = PATHSEPERATOR_CHAR );
	bool					FindCaseInsensitiveOSPathEntry( const char *directory, const char *segment, bool directoryOnly, idStr &resolvedSegment );
	bool					ResolveCaseInsensitiveOSPath( const char *path, idStr &resolvedPath, bool finalSegmentIsFile );
	int						HashFileName( const char *fname ) const;
	int						ListOSFiles( const char *directory, const char *extension, idStrList &list );
	FILE *					OpenOSFile( const char *name, const char *mode, idStr *caseSensitiveName = NULL );
	FILE *					OpenOSFileCorrectName( idStr &path, const char *mode );
	int						DirectFileLength( FILE *o );
	void					CopyFile( idFile *src, const char *toOSPath );
	void					AddAssetLogEntry( const char *relativePath );
	void					BuildLevelLoadContentKey( idStr &key ) const;
	void					RecordOpenedLevelLoadSource( const char *relativePath, idFile *file );
	idFile *				UsePreloadedLevelLoadSource( const char *relativePath, idFile *file );
	int						AddUnique( const char *name, idStrList &list, idHashIndex &hashIndex ) const;
	void					GetExtensionList( const char *extension, idStrList &extensionList ) const;
	int						GetFileList( const char *relativePath, const idStrList &extensions, idStrList &list, idHashIndex &hashIndex, bool fullRelativePath, const char* gamedir = NULL );

	int						GetFileListTree( const char *relativePath, const idStrList &extensions, idStrList &list, idHashIndex &hashIndex, const char* gamedir = NULL );
	pack_t *				LoadZipFile( const char *zipfile );
	void					AddGameDirectory( const char *path, const char *dir );
	void					SetupGameDirectories( const char *gameName );
	bool					IsGameDirPack( const pack_t *pak, const char *gameDir ) const;
	bool					IsBaseGamePack( const pack_t *pak ) const;
	bool					IsOpenQ4PurePack( const pack_t *pak ) const;
	pack_t *				FindGamePackByName( const char *name, const char *gameDir ) const;
	pack_t *				FindBaseGamePackByName( const char *name ) const;
	void					PrintContentSearchDiagnostics( void );
	bool					FindMisplacedOfficialPaks( idStr &errors ) const;
	bool					ValidateOpenQ4Paks( idStr &errors ) const;
	bool					ValidateRequiredOfficialPaks( idStr &errors ) const;
	void					Startup( void );
	void					SetRestrictions( void );
							// some files can be obtained from directories without compromising si_pure
	bool					FileAllowedFromDir( const char *path );
							// searches all the paks, no pure check
	pack_t *				GetPackForChecksum( int checksum, bool searchAddons = false );
							// searches all the paks, no pure check
	pack_t *				FindPakForFileChecksum( const char *relativePath, int fileChecksum, bool bReference );
	idFile_InZip *			ReadFileFromZip( pack_t *pak, fileInPack_t *pakFile, const char *relativePath );
	int						GetFileChecksum( idFile *file );
	pureStatus_t			GetPackStatus( pack_t *pak );
	addonInfo_t *			ParseAddonDef( const char *buf, const int len );
	void					FollowAddonDependencies( pack_t *pak );
	modManifestStatus_t		ReadModManifestFromSearchPath( const char *searchPath, const char *modDir, idModInfo &modInfo, idStr *reason = NULL );
	modManifestStatus_t		ReadModManifestFile( const char *manifestPath, idModInfo &modInfo, idStr *reason = NULL );
	bool					ValidateConfiguredGameDir( const char *gameDir, idStr *reason = NULL );
	bool					NormalizeMapPath( const char *mapName, idStr &relativePath ) const;
	bool					AddonPackProvidesMap( const pack_t *pak, const char *relativeMapPath ) const;
	void					FreePack( pack_t *pack ) const;
	void					StageStartupAddonPaks( void );

	static size_t			CurlWriteFunction( void *ptr, size_t size, size_t nmemb, void *stream );
							// curl_progress_callback in curl.h
	static int				CurlProgressFunction( void *clientp, double dltotal, double dlnow, double ultotal, double ulnow );
};

idCVar	idFileSystemLocal::fs_restrict( "fs_restrict", "", CVAR_SYSTEM | CVAR_INIT | CVAR_BOOL, "" );
idCVar	idFileSystemLocal::fs_debug( "fs_debug", "0", CVAR_SYSTEM | CVAR_INTEGER, "", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar	idFileSystemLocal::fs_copyfiles( "fs_copyfiles", "0", CVAR_SYSTEM | CVAR_INIT | CVAR_INTEGER, "", 0, 4, idCmdSystem::ArgCompletion_Integer<0,3> );
idCVar	idFileSystemLocal::fs_basepath( "fs_basepath", "", CVAR_SYSTEM | CVAR_INIT, "" );
idCVar	idFileSystemLocal::fs_homepath( "fs_homepath", "", CVAR_SYSTEM | CVAR_INIT, "" );
idCVar	idFileSystemLocal::fs_savepath( "fs_savepath", "", CVAR_SYSTEM | CVAR_INIT, "" );
idCVar	idFileSystemLocal::fs_cdpath( "fs_cdpath", "", CVAR_SYSTEM | CVAR_INIT, "" );
idCVar	idFileSystemLocal::fs_game( "fs_game", OPENQ4_GAMEDIR, CVAR_SYSTEM | CVAR_INIT | CVAR_SERVERINFO, "mod path" );
idCVar  idFileSystemLocal::fs_game_base( "fs_game_base", "", CVAR_SYSTEM | CVAR_INIT | CVAR_SERVERINFO, "alternate mod path, searched after the main fs_game path, before the basedir" );
#ifdef WIN32
idCVar	idFileSystemLocal::fs_caseSensitiveOS( "fs_caseSensitiveOS", "0", CVAR_SYSTEM | CVAR_BOOL, "" );
#else
idCVar	idFileSystemLocal::fs_caseSensitiveOS( "fs_caseSensitiveOS", "1", CVAR_SYSTEM | CVAR_BOOL, "" );
#endif
idCVar	idFileSystemLocal::fs_searchAddons( "fs_searchAddons", "0", CVAR_SYSTEM | CVAR_BOOL, "search all addon pk4s ( disables addon functionality )" );
idCVar	idFileSystemLocal::fs_validateOfficialPaks( "fs_validateOfficialPaks", "1", CVAR_SYSTEM | CVAR_INIT | CVAR_BOOL, "verify required official q4base media pk4 checksums on startup" );

idFileSystemLocal	fileSystemLocal;
idFileSystem *		fileSystem = &fileSystemLocal;

/*
================
idFileSystemLocal::idFileSystemLocal
================
*/
idFileSystemLocal::idFileSystemLocal( void ) {
	searchPaths = NULL;
	fileSystemMainThread = std::this_thread::get_id();
	readCount = 0;
	readCountPacifierBytes = 0;
	loadCount = 0;
	loadStack = 0;
	dir_cache_index = 0;
	dir_cache_count = 0;
	d3xp = 0;
	loadedFileFromDir = false;
	restartGamePakChecksum = 0;
	gameDLLChecksum = 0;
	gamePakChecksum = 0;
	gameModuleOrigin = GAME_MODULE_ORIGIN_NONE;
	memset( gamePakForOS, 0, sizeof( gamePakForOS ) );
	isFileLoadingAllowed = false;
	currentAssetLog.Clear();
	currentAssetLogUnfiltered.Clear();
	assetLog.Clear();
	levelLoadCache = NULL;
	backgroundDownloads = NULL;
	defaultBackgroundDownload.next = NULL;
	defaultBackgroundDownload.opcode = DLTYPE_FILE;
	defaultBackgroundDownload.f = NULL;
	defaultBackgroundDownload.file.position = 0;
	defaultBackgroundDownload.file.length = 0;
	defaultBackgroundDownload.file.buffer = NULL;
	defaultBackgroundDownload.url.url.Clear();
	defaultBackgroundDownload.url.dlerror[ 0 ] = '\0';
	defaultBackgroundDownload.url.expectedSize = 0;
	defaultBackgroundDownload.url.dltotal = 0;
	defaultBackgroundDownload.url.dlnow = 0;
	defaultBackgroundDownload.url.dlstatus = 0;
	defaultBackgroundDownload.url.status = DL_WAIT;
	defaultBackgroundDownload.completed = true;
	memset( &backgroundThread, 0, sizeof( backgroundThread ) );
	addonPaks = NULL;
}

/*
================
idFileSystemLocal::BuildLevelLoadContentKey

Produces an installation-root-independent fingerprint input for the exact
ordered active PK4/search set.  Per-file loose-source timestamps are still
validated separately by each manifest/cache source identity.
================
*/
void idFileSystemLocal::BuildLevelLoadContentKey( idStr &key ) const {
	key = va( "game=%s|base=%s|", fs_game.GetString(), fs_game_base.GetString() );
	int order = 0;
	for ( const searchpath_t *search = searchPaths; search != NULL; search = search->next, order++ ) {
		if ( search->pack != NULL ) {
			idStr fileName;
			search->pack->pakFilename.ExtractFileName( fileName );
			fileName.ToLower();
			key += va( "p%d=%s:%08x:%d:%d:%d|", order, fileName.c_str(),
				static_cast<unsigned int>( search->pack->checksum ),
				search->pack->length, search->pack->numfiles,
				search->pack->addon_search ? 1 : 0 );
		} else if ( search->dir != NULL ) {
			idStr gameDir = search->dir->gamedir;
			gameDir.ToLower();
			key += va( "d%d=%s|", order, gameDir.c_str() );
		}
	}
	key += "pure=";
	for ( int i = 0; i < serverPaks.Num(); i++ ) {
		key += va( "%08x,", static_cast<unsigned int>( serverPaks[ i ]->checksum ) );
	}
}

void idFileSystemLocal::RecordOpenedLevelLoadSource( const char *relativePath, idFile *file ) {
	if ( levelLoadCache != NULL ) {
		levelLoadCache->RecordOpenedSource( relativePath, file );
	}
}

idFile *idFileSystemLocal::UsePreloadedLevelLoadSource( const char *relativePath, idFile *file ) {
	if ( levelLoadCache == NULL || file == NULL ) {
		return file;
	}
	idFile *preloaded = levelLoadCache->OpenPreloadedSource( relativePath, file );
	if ( preloaded == NULL ) {
		return file;
	}
	CloseFile( file );
	return preloaded;
}

void idFileSystemLocal::BeginLevelLoadCache( const char *mapKey, const char *gameMode,
		const char *entityFilter, const char *settingsKey ) {
	if ( levelLoadCache == NULL ) {
		return;
	}
	idStr contentKey;
	BuildLevelLoadContentKey( contentKey );
	levelLoadCache->Begin( mapKey, gameMode, entityFilter, contentKey.c_str(), settingsKey );
}

void idFileSystemLocal::FinishLevelLoadCache( const bool successful ) {
	if ( levelLoadCache != NULL ) {
		levelLoadCache->Finish( successful );
	}
}

void idFileSystemLocal::ReleaseLevelLoadCache( void ) {
	if ( levelLoadCache != NULL ) {
		levelLoadCache->Release();
	}
}

// Engine-only lifecycle hook. Keeping this outside the public filesystem ABI
// lets Session retain replay bytes through all EndLevelLoad consumers without
// exposing coordinator internals to renderer or game modules.
void FS_ReleaseLevelLoadCache( void ) {
	fileSystemLocal.ReleaseLevelLoadCache();
}

void idFileSystemLocal::CancelLevelLoadCache( void ) {
	if ( levelLoadCache != NULL ) {
		levelLoadCache->Cancel();
	}
}

void idFileSystemLocal::RecordLevelLoadResource( const levelLoadResourceType_t type,
		const char *name, const char *options, const unsigned int flags,
		const unsigned int priority ) {
	if ( levelLoadCache != NULL ) {
		levelLoadCache->RecordSemantic( type, name, options, flags, priority );
	}
}

idFile *idFileSystemLocal::OpenGeneratedCacheRead( const generatedCacheKind_t kind,
		const char *sourcePath, const unsigned int parserVersion, const char *settingsKey ) {
	if ( levelLoadCache == NULL ) {
		return NULL;
	}
	idStr contentKey;
	BuildLevelLoadContentKey( contentKey );
	return levelLoadCache->OpenGeneratedCacheRead( kind, sourcePath, parserVersion,
		settingsKey, contentKey.c_str() );
}

bool idFileSystemLocal::WriteGeneratedCache( const generatedCacheKind_t kind,
		const char *sourcePath, const unsigned int parserVersion, const char *settingsKey,
		const void *payload, const unsigned int payloadBytes ) {
	if ( levelLoadCache == NULL ) {
		return false;
	}
	idStr contentKey;
	BuildLevelLoadContentKey( contentKey );
	return levelLoadCache->WriteGeneratedCache( kind, sourcePath, parserVersion,
		settingsKey, payload, payloadBytes, contentKey.c_str() );
}

void idFileSystemLocal::DiscardGeneratedCache( const generatedCacheKind_t kind,
		const char *sourcePath, const unsigned int parserVersion, const char *settingsKey ) {
	if ( levelLoadCache == NULL ) {
		return;
	}
	idStr contentKey;
	BuildLevelLoadContentKey( contentKey );
	levelLoadCache->DiscardGeneratedCache( kind, sourcePath, parserVersion,
		settingsKey, contentKey.c_str() );
}

/*
================
idFileSystemLocal::ResetReadCount

Level-load workers may inflate independently opened PK4 streams. Protect the
shared progress counter, but keep presentation calls on the main thread.
================
*/
void idFileSystemLocal::ResetReadCount( void ) {
	std::lock_guard<std::mutex> lock( readCountMutex );
	readCount = 0;
	readCountPacifierBytes = 0;
}

int idFileSystemLocal::GetReadCount( void ) {
	std::lock_guard<std::mutex> lock( readCountMutex );
	return readCount;
}

void idFileSystemLocal::AddToReadCount( const int count ) {
	if ( count <= 0 ) {
		return;
	}

	bool offerPacifier = false;
	{
		std::lock_guard<std::mutex> lock( readCountMutex );
		const int maxCount = ( std::numeric_limits<int>::max )();
		readCount = count > maxCount - readCount ? maxCount : readCount + count;
		readCountPacifierBytes = count > maxCount - readCountPacifierBytes
			? maxCount
			: readCountPacifierBytes + count;
		if ( readCountPacifierBytes >= READCOUNT_PACIFIER_INTERVAL_BYTES ) {
			readCountPacifierBytes = 0;
			offerPacifier = true;
		}
	}

	if ( offerPacifier && std::this_thread::get_id() == fileSystemMainThread && session != NULL ) {
		session->PacifierUpdate();
	}
}

/*
================
idFileSystemLocal::HashFileName

return a hash value for the filename
================
*/
int idFileSystemLocal::HashFileName( const char *fname ) const {
	int		i;
	uint32_t	hash;
	byte	letter;

	hash = 0;
	i = 0;
	while( fname[i] != '\0' ) {
		letter = static_cast<byte>( idStr::ToLower( fname[i] ) );
		if ( letter == '.' ) {
			break;				// don't include extension
		}
		if ( letter == '\\' ) {
			letter = '/';		// damn path names
		}
		hash += static_cast<uint32_t>( letter ) * ( static_cast<uint32_t>( i ) + 119u );
		i++;
	}
	return static_cast<int>( hash & ( FILE_HASH_SIZE - 1 ) );
}

/*
===========
idFileSystemLocal::FilenameCompare

Ignore case and separator char distinctions
===========
*/
bool idFileSystemLocal::FilenameCompare( const char *s1, const char *s2 ) const {
	int		c1, c2;
	
	do {
		c1 = *s1++;
		c2 = *s2++;

		if ( c1 >= 'a' && c1 <= 'z' ) {
			c1 -= ('a' - 'A');
		}
		if ( c2 >= 'a' && c2 <= 'z' ) {
			c2 -= ('a' - 'A');
		}

		if ( c1 == '\\' || c1 == ':' ) {
			c1 = '/';
		}
		if ( c2 == '\\' || c2 == ':' ) {
			c2 = '/';
		}
		
		if ( c1 != c2 ) {
			return true;		// strings not equal
		}
	} while( c1 );
	
	return false;		// strings are equal
}

/*
================
idFileSystemLocal::OpenOSFile
optional caseSensitiveName is set to case sensitive file name as found on disc (fs_caseSensitiveOS only)
================
*/
FILE *idFileSystemLocal::OpenOSFile( const char *fileName, const char *mode, idStr *caseSensitiveName ) {
	int i;
	FILE *fp;
	idStr fpath, entry;
	idStrList list;

#ifndef __MWERKS__
#ifndef WIN32 
	// some systems will let you fopen a directory
	struct stat buf;
	if ( stat( fileName, &buf ) != -1 && !S_ISREG(buf.st_mode) ) {
		return NULL;
	}
#endif
#endif
	fp = fopen( fileName, mode );
	if ( !fp && fs_caseSensitiveOS.GetBool() ) {
		idStr resolvedFileName;
		if ( ResolveCaseInsensitiveOSPath( fileName, resolvedFileName, true ) ) {
			fp = fopen( resolvedFileName, mode );
			if ( fp ) {
				if ( caseSensitiveName ) {
					*caseSensitiveName = resolvedFileName;
					caseSensitiveName->StripPath();
				}
				if ( fs_debug.GetInteger() ) {
					common->Printf( "idFileSystemLocal::OpenFileRead: changed %s to %s\n", fileName, resolvedFileName.c_str() );
				}
				return fp;
			} else {
				common->Warning( "idFileSystemLocal::OpenFileRead: fs_caseSensitiveOS 1 resolved %s to %s but could not open it", fileName, resolvedFileName.c_str() );
			}
		}

		fpath = fileName;
		fpath.StripFilename();
		fpath.StripTrailing( PATHSEPERATOR_CHAR );
		idStr resolvedPath;
		if ( ResolveCaseInsensitiveOSPath( fpath.c_str(), resolvedPath, false ) ) {
			fpath = resolvedPath;
		}
		if ( ListOSFiles( fpath, NULL, list ) == -1 ) {
			return NULL;
		}
		
		for ( i = 0; i < list.Num(); i++ ) {
			entry = fpath + PATHSEPERATOR_CHAR + list[i];
			if ( !entry.Icmp( fileName ) ) {
				fp = fopen( entry, mode );
				if ( fp ) {
					if ( caseSensitiveName ) {
						*caseSensitiveName = entry;
						caseSensitiveName->StripPath();
					}
					if ( fs_debug.GetInteger() ) {
						common->Printf( "idFileSystemLocal::OpenFileRead: changed %s to %s\n", fileName, entry.c_str() );
					}
					break;
				} else {
					// not supposed to happen if ListOSFiles is doing it's job correctly
					common->Warning( "idFileSystemLocal::OpenFileRead: fs_caseSensitiveOS 1 could not open %s", entry.c_str() );
				}
			}
		}
	} else if ( caseSensitiveName ) {
		*caseSensitiveName = fileName;
		caseSensitiveName->StripPath();
	}
	return fp;
}

/*
================
idFileSystemLocal::OpenOSFileCorrectName
================
*/
FILE *idFileSystemLocal::OpenOSFileCorrectName( idStr &path, const char *mode ) {
	idStr caseName;
	FILE *f = OpenOSFile( path.c_str(), mode, &caseName );
	if ( f ) {
		path.StripFilename();
		path += PATHSEPERATOR_STR;
		path += caseName;
	}
	return f;
}

/*
================
idFileSystemLocal::DirectFileLength
================
*/
int idFileSystemLocal::DirectFileLength( FILE *o ) {
	int		pos;
	int		end;

	pos = ftell( o );
	fseek( o, 0, SEEK_END );
	end = ftell( o );
	fseek( o, pos, SEEK_SET );
	return end;
}

/*
============
idFileSystemLocal::CreateOSPath

Creates any directories needed to store the given filename
============
*/
void idFileSystemLocal::CreateOSPath( const char *OSPath ) {
	char	*ofs;
	
	// make absolutely sure that it can't back up the path
	if ( FS_HasParentOSPathSegment( OSPath ) || strstr( OSPath, "::" ) ) {
#ifdef _DEBUG		
		common->DPrintf( "refusing to create relative path \"%s\"\n", OSPath );
#endif
		return;
	}

	idStr path( OSPath );
	for( ofs = &path[ 1 ]; *ofs ; ofs++ ) {
		if ( *ofs == PATHSEPERATOR_CHAR ) {	
			// create the directory
			*ofs = 0;
			Sys_Mkdir( path );
			*ofs = PATHSEPERATOR_CHAR;
		}
	}
}

/*
=================
idFileSystemLocal::CopyFile

Copy a fully specified file from one place to another
=================
*/
void idFileSystemLocal::CopyFile( const char *fromOSPath, const char *toOSPath ) {
	FILE	*f;
	int		len;
	byte	*buf;

	common->Printf( "copy %s to %s\n", fromOSPath, toOSPath );
	f = OpenOSFile( fromOSPath, "rb" );
	if ( !f ) {
		return;
	}
	fseek( f, 0, SEEK_END );
	len = ftell( f );
	fseek( f, 0, SEEK_SET );

	buf = (byte *)Mem_Alloc( len );
	if ( fread( buf, 1, len, f ) != (unsigned int)len ) {
		common->FatalError( "short read in idFileSystemLocal::CopyFile()\n" );
	}
	fclose( f );

	CreateOSPath( toOSPath );
	f = OpenOSFile( toOSPath, "wb" );
	if ( !f ) {
		common->Printf( "could not create destination file\n" );
		Mem_Free( buf );
		return;
	}
	if ( fwrite( buf, 1, len, f ) != (unsigned int)len ) {
		common->FatalError( "short write in idFileSystemLocal::CopyFile()\n" );
	}
	fclose( f );
	Mem_Free( buf );
}

/*
=================
idFileSystemLocal::CopyFile
=================
*/
void idFileSystemLocal::CopyFile( idFile *src, const char *toOSPath ) {
	FILE	*f;
	int		len;
	byte	*buf;

	common->Printf( "copy %s to %s\n", src->GetName(), toOSPath );
	src->Seek( 0, FS_SEEK_END );
	len = src->Tell();
	src->Seek( 0, FS_SEEK_SET );

	buf = (byte *)Mem_Alloc( len );
	if ( src->Read( buf, len ) != len ) {
		common->FatalError( "Short read in idFileSystemLocal::CopyFile()\n" );
	}

	CreateOSPath( toOSPath );
	f = OpenOSFile( toOSPath, "wb" );
	if ( !f ) {
		common->Printf( "could not create destination file\n" );
		Mem_Free( buf );
		return;
	}
	if ( fwrite( buf, 1, len, f ) != (unsigned int)len ) {
		common->FatalError( "Short write in idFileSystemLocal::CopyFile()\n" );
	}
	fclose( f );
	Mem_Free( buf );
}

/*
====================
idFileSystemLocal::ReplaceSeparators

Fix things up differently for win/unix/mac
====================
*/
void idFileSystemLocal::ReplaceSeparators( idStr &path, char sep ) {
	char *s;

	for( s = &path[ 0 ]; *s ; s++ ) {
		if ( *s == '/' || *s == '\\' ) {
			*s = sep;
		}
	}
}

/*
===================
idFileSystemLocal::FindCaseInsensitiveOSPathEntry
===================
*/
bool idFileSystemLocal::FindCaseInsensitiveOSPathEntry( const char *directory, const char *segment, bool directoryOnly, idStr &resolvedSegment ) {
	idStrList entries;
	const char *listDirectory = directory;
	const char *extension = directoryOnly ? "/" : "";

	if ( !listDirectory || !listDirectory[0] ) {
		listDirectory = ".";
	}

	if ( !segment || !segment[0] ) {
		return false;
	}

	if ( Sys_ListFiles( listDirectory, extension, entries ) == -1 ) {
		return false;
	}

	for ( int i = 0; i < entries.Num(); i++ ) {
		if ( entries[i].Cmp( segment ) == 0 ) {
			resolvedSegment = entries[i];
			return true;
		}
	}

	for ( int i = 0; i < entries.Num(); i++ ) {
		if ( entries[i].Icmp( segment ) == 0 ) {
			resolvedSegment = entries[i];
			return true;
		}
	}

	return false;
}

/*
===================
idFileSystemLocal::ResolveCaseInsensitiveOSPath
===================
*/
bool idFileSystemLocal::ResolveCaseInsensitiveOSPath( const char *path, idStr &resolvedPath, bool finalSegmentIsFile ) {
	if ( !path ) {
		resolvedPath.Clear();
		return false;
	}

	if ( !path[0] ) {
		resolvedPath.Clear();
		return false;
	}

	idStr normalized = path;
	ReplaceSeparators( normalized, '/' );

	while ( normalized.Length() > 1 && normalized[ normalized.Length() - 1 ] == '/' ) {
		normalized.CapLength( normalized.Length() - 1 );
	}

	const bool absolutePath = ( normalized[0] == '/' );
	const int pathLength = normalized.Length();
	int index = absolutePath ? 1 : 0;
	bool changed = false;

	resolvedPath = absolutePath ? "/" : "";

	while ( index < pathLength ) {
		while ( index < pathLength && normalized[index] == '/' ) {
			index++;
		}
		if ( index >= pathLength ) {
			break;
		}

		const int segmentStart = index;
		while ( index < pathLength && normalized[index] != '/' ) {
			index++;
		}

		idStr segment = normalized.Mid( segmentStart, index - segmentStart );
		const bool hasMoreSegments = ( index < pathLength );
		const bool directoryOnly = hasMoreSegments || !finalSegmentIsFile;
		const char *parentDirectory = resolvedPath.IsEmpty() ? "." : resolvedPath.c_str();
		idStr resolvedSegment;

		// Most mixed-case stock paths already use the exact on-disk spelling.  A
		// direct stat avoids enumerating every parent directory for every loose-
		// file probe (including probes for assets that ultimately live in PK4s).
#ifndef WIN32
		idStr exactPath = resolvedPath;
		exactPath.AppendPath( segment );
		struct stat exactStat;
		const bool exactEntryMatches = stat( exactPath.c_str(), &exactStat ) == 0 &&
			( directoryOnly ? S_ISDIR( exactStat.st_mode ) : !S_ISDIR( exactStat.st_mode ) );
#else
		// Windows has no S_ISDIR and does not need this optimization on its
		// case-insensitive filesystem. Preserve the existing enumeration fallback
		// if case recovery is explicitly enabled there.
		const bool exactEntryMatches = false;
#endif
		if ( exactEntryMatches ) {
			resolvedSegment = segment;
		} else if ( !FindCaseInsensitiveOSPathEntry( parentDirectory, segment.c_str(), directoryOnly, resolvedSegment ) ) {
			if ( fs_debug.GetBool() ) {
				common->Printf( "idFileSystemLocal::ResolveCaseInsensitiveOSPath: could not resolve %s segment '%s' under '%s' while resolving '%s'\n",
					directoryOnly ? "directory" : "file",
					segment.c_str(),
					parentDirectory,
					path );
			}
			resolvedPath = normalized;
			ReplaceSeparators( resolvedPath );
			return false;
		}

		if ( resolvedSegment.Cmp( segment.c_str() ) != 0 ) {
			changed = true;
		}
		resolvedPath.AppendPath( resolvedSegment );
	}

	ReplaceSeparators( resolvedPath );
	return changed;
}

/*
===================
idFileSystemLocal::BuildOSPath
===================
*/
const char *idFileSystemLocal::BuildOSPath( const char *base, const char *game, const char *relativePath ) {
	static char OSPath[MAX_STRING_CHARS];
	idStr newPath;
	bool hasUpperDirectory = false;
	idStr strBase = base;
	strBase.StripTrailing( '/' );
	strBase.StripTrailing( '\\' );
	newPath = strBase;
	newPath.AppendPath( game );
	newPath.AppendPath( relativePath );
	ReplaceSeparators( newPath );

	if ( fs_caseSensitiveOS.GetBool() || com_developer.GetBool() ) {
		// extract the directory path and warn about non-portable casing
		idStr testPath;

		testPath = game;
		testPath.AppendPath( relativePath );
		testPath.StripFilename();

		if ( testPath.HasUpper() ) {
			hasUpperDirectory = true;
			bool warn = true;

			// On case-insensitive OSes, avoid warning for top-level non-game folders
			// that are only being probed during discovery (e.g. CrashReports, Docs).
			if ( !fs_caseSensitiveOS.GetBool() ) {
				if ( !relativePath || !relativePath[0] ) {
					if ( idStr::Icmp( game, gameFolder.c_str() ) != 0 &&
						 idStr::Icmp( game, fs_game.GetString() ) != 0 &&
						 idStr::Icmp( game, fs_game_base.GetString() ) != 0 ) {
						warn = false;
					}
				}
			}

			if ( warn ) {
				common->Warning( "Non-portable: path contains uppercase characters: %s", testPath.c_str() );
			}
		}
	}

	if ( fs_caseSensitiveOS.GetBool() && hasUpperDirectory ) {
		const int relativeLength = relativePath ? idStr::Length( relativePath ) : 0;
		const bool finalSegmentIsFile = ( relativeLength > 0 && relativePath[ relativeLength - 1 ] != '/' && relativePath[ relativeLength - 1 ] != '\\' );
		idStr directoryPath = newPath;
		idStr fileName;

		if ( finalSegmentIsFile ) {
			fileName = directoryPath;
			fileName.StripPath();
			directoryPath.StripFilename();
		}

		while ( directoryPath.Length() > 1 && directoryPath[ directoryPath.Length() - 1 ] == PATHSEPERATOR_CHAR ) {
			directoryPath.CapLength( directoryPath.Length() - 1 );
		}

		idStr resolvedDirectory;
		if ( !directoryPath.IsEmpty() && ResolveCaseInsensitiveOSPath( directoryPath.c_str(), resolvedDirectory, false ) ) {
			if ( finalSegmentIsFile ) {
				resolvedDirectory.AppendPath( fileName );
			}
			newPath = resolvedDirectory;
			ReplaceSeparators( newPath );
			common->DPrintf( "Resolved case-sensitive path to %s\n", newPath.c_str() );
		}
	}

	idStr::Copynz( OSPath, newPath, sizeof( OSPath ) );
	return OSPath;
}

/*
================
idFileSystemLocal::OSPathToRelativePath

takes a full OS path, as might be found in data from a media creation
program, and converts it to a relativePath by stripping off directories

Returns false if the osPath tree doesn't match any of the existing
search paths.

================
*/
const char *idFileSystemLocal::OSPathToRelativePath( const char *OSPath ) {
	static char relativePath[MAX_STRING_CHARS];
	char *s, *base;

	// skip a drive letter?

	// search for anything with "base" in it
	// Ase files from max may have the form of:
	// "//Purgatory/purgatory/doom/base/models/mapobjects/bitch/hologirl.tga"
	// which won't match any of our drive letter based search paths
	bool ignoreWarning = false;
#ifdef ID_DEMO_BUILD
	base = strstr( OSPath, BASE_GAMEDIR );	
	idStr tempStr = OSPath;
	tempStr.ToLower();
	if ( ( strstr( tempStr, "//" ) || strstr( tempStr, "w:" ) ) && strstr( tempStr, "/doom/base/") ) {
		// will cause a warning but will load the file. ase models have
		// hard coded doom/base/ in the material names
		base = strstr( OSPath, "base" );
		ignoreWarning = true;
	}
#else
	// look for the first complete directory name
	base = (char *)strstr( OSPath, BASE_GAMEDIR );
	while ( base ) {
		char c1 = '\0', c2;
		if ( base > OSPath ) {
			c1 = *(base - 1);
		}
		c2 = *( base + strlen( BASE_GAMEDIR ) );
		if ( ( base == OSPath || c1 == '/' || c1 == '\\' ) &&
			( c2 == '\0' || c2 == '/' || c2 == '\\' ) ) {
			break;
		}
		base = strstr( base + 1, BASE_GAMEDIR );
	}
#endif
	// fs_game and fs_game_base support - look for first complete name with a mod path
	// ( fs_game searched before fs_game_base )
	const char *fsgame = NULL;
	int igame = 0;
	for ( igame = 0; igame < 2; igame++ ) {
		if ( igame == 0 ) {
			fsgame = fs_game.GetString();
		} else if ( igame == 1 ) {
			fsgame = fs_game_base.GetString();
		}
		if ( base == NULL && fsgame && strlen( fsgame ) ) {
			base = (char *)strstr( OSPath, fsgame );
			while ( base ) {
				char c1 = '\0', c2;
				if ( base > OSPath ) {
					c1 = *(base - 1);
				}
				c2 = *( base + strlen( fsgame ) );
				if ( ( base == OSPath || c1 == '/' || c1 == '\\' ) &&
					( c2 == '\0' || c2 == '/' || c2 == '\\' ) ) {
					break;
				}
				base = strstr( base + 1, fsgame );
			}
		}
	}

	if ( base ) {
		s = strstr( base, "/" );
		if ( !s ) {
			s = strstr( base, "\\" );
		}
		if ( s ) {
			idStr::Copynz( relativePath, s + 1, sizeof( relativePath ) );
			if ( fs_debug.GetInteger() > 1 ) {
				common->Printf( "idFileSystem::OSPathToRelativePath: %s becomes %s\n", OSPath, relativePath );
			}
			return relativePath;
		}
		// A qpath containing only the game-directory segment is valid and maps
		// to the VFS root; do not report it as an OS-path conversion failure.
		relativePath[0] = '\0';
		return relativePath;
	}

	if ( !ignoreWarning ) {
		common->Warning( "idFileSystem::OSPathToRelativePath failed on %s", OSPath );
	}
	relativePath[0] = '\0';
	return relativePath;
}

/*
=====================
idFileSystemLocal::RelativePathToOSPath

Returns a fully qualified path that can be used with stdio libraries
=====================
*/
const char *idFileSystemLocal::RelativePathToOSPath( const char *relativePath, const char *basePath ) {
	const char *path = cvarSystem->GetCVarString( basePath );
	if ( !path[0] ) {
		path = fs_savepath.GetString();
	}
	return BuildOSPath( path, gameFolder, relativePath );
}

/*
=================
idFileSystemLocal::RemoveFileChecked

Removes a validated qpath from exactly one writable root.  An already-absent
file is a successful cleanup; every other host error is reported to the caller.
=================
*/
bool idFileSystemLocal::RemoveFileChecked( const char *relativePath,
		const char *basePath ) {
	if ( !searchPaths ) {
		common->Warning( "idFileSystemLocal::RemoveFileChecked: filesystem is not initialized" );
		return false;
	}
	const char *invalidReason = NULL;
	if ( !FS_ValidateRelativeWritePath( relativePath, &invalidReason ) ) {
		common->Warning( "idFileSystemLocal::RemoveFileChecked: refusing unsafe relative path (%s)",
			invalidReason != NULL ? invalidReason : "invalid path" );
		return false;
	}
	if ( basePath == NULL || basePath[ 0 ] == '\0' ) {
		common->Warning( "idFileSystemLocal::RemoveFileChecked: writable root is empty" );
		return false;
	}

	const char *root = cvarSystem->GetCVarString( basePath );
	if ( root == NULL || root[ 0 ] == '\0' ) {
		if ( idStr::Icmp( basePath, "fs_savepath" ) != 0 ) {
			common->Warning( "idFileSystemLocal::RemoveFileChecked: writable root '%s' is unset",
				basePath );
			return false;
		}
		root = fs_savepath.GetString();
	}
	if ( root == NULL || root[ 0 ] == '\0' ) {
		common->Warning( "idFileSystemLocal::RemoveFileChecked: fs_savepath is unset" );
		return false;
	}

	const idStr OSPath = BuildOSPath( root, gameFolder, relativePath );
	errno = 0;
	const int removalResult = remove( OSPath.c_str() );
	const int removalError = errno;
	if ( removalResult == 0 || removalError == ENOENT ) {
		ClearDirCache();
		return true;
	}
	common->Warning( "Could not remove '%s' from '%s' (error %d)",
		relativePath, basePath, removalError );
	return false;
}

/*
=================
idFileSystemLocal::RemoveFile
=================
*/
void idFileSystemLocal::RemoveFile( const char *relativePath, const char *basePath ) {
	idStr OSPath;
	const char *invalidReason;
	if ( !FS_ValidateRelativeWritePath( relativePath, &invalidReason ) ) {
		common->Warning( "idFileSystemLocal::RemoveFile: refusing unsafe relative path (%s)", invalidReason );
		return;
	}

	if ( idStr::Icmp( basePath, "fs_savepath" ) != 0 ) {
		const char *path = cvarSystem->GetCVarString( basePath );
		if ( path[0] ) {
			OSPath = BuildOSPath( path, gameFolder, relativePath );
			remove( OSPath );
		}
		ClearDirCache();
		return;
	}

	if ( fs_cdpath.GetString()[0] ) {
		OSPath = BuildOSPath( fs_cdpath.GetString(), gameFolder, relativePath );
		remove( OSPath );
	}

	if ( fs_savepath.GetString()[0] && idStr::Icmp( fs_savepath.GetString(), fs_cdpath.GetString() ) != 0 ) {
		OSPath = BuildOSPath( fs_savepath.GetString(), gameFolder, relativePath );
		remove( OSPath );
	}

	ClearDirCache();
}

/*
=================
idFileSystemLocal::RemoveExplicitFile
=================
*/
int idFileSystemLocal::RemoveExplicitFile( const char *OSPath ) {
	const int result = remove( OSPath );
	ClearDirCache();
	return result;
}

/*
================
idFileSystemLocal::FileIsInPAK
================
*/
bool idFileSystemLocal::FileIsInPAK( const char *relativePath ) {
	searchpath_t	*search;
	pack_t			*pak;
	fileInPack_t	*pakFile;
	int				hash;

	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}

	if ( !relativePath ) {
		common->FatalError( "idFileSystemLocal::FileIsInPAK: NULL 'relativePath' parameter passed\n" );
	}

	// qpaths are not supposed to have a leading slash
	if ( relativePath[0] == '/' || relativePath[0] == '\\' ) {
		relativePath++;
	}

	// make absolutely sure that it can't back up the path.
	// The searchpaths do guarantee that something will always
	// be prepended, so we don't need to worry about "c:" or "//limbo" 
	if ( strstr( relativePath, ".." ) || strstr( relativePath, "::" ) ) {
		return false;
	}

	//
	// search through the path, one element at a time
	//

	hash = HashFileName( relativePath );

	for ( search = searchPaths; search; search = search->next ) {
		// is the element a pak file?
		if ( search->pack && search->pack->hashTable[hash] ) {

			// disregard if it doesn't match one of the allowed pure pak files - or is a localization file
			if ( serverPaks.Num() ) {
				GetPackStatus( search->pack );
				if ( search->pack->pureStatus != PURE_NEVER && !serverPaks.Find( search->pack ) ) {
					continue; // not on the pure server pak list
				}
			}

			// look through all the pak file elements
			pak = search->pack;
			pakFile = pak->hashTable[hash];
			do {
				// case and separator insensitive comparisons
				if ( !FilenameCompare( pakFile->name, relativePath ) ) {
					return true;
				}
				pakFile = pakFile->next;
			} while( pakFile != NULL );
		}
	}
	return false;
}

/*
============
idFileSystemLocal::ReadFile

Filename are relative to the search path
a null buffer will just return the file length and time without loading
timestamp can be NULL if not required
============
*/
int idFileSystemLocal::ReadFile( const char *relativePath, void **buffer, ID_TIME_T *timestamp ) {
	idFile *	f;
	byte *		buf;
	int			len;
	bool		isConfig;
	static bool warnedEmptyReadPath = false;

	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}

	if ( timestamp ) {
		*timestamp = FILE_NOT_FOUND_TIMESTAMP;
	}

	if ( buffer ) {
		*buffer = NULL;
	}

	if ( !relativePath || !relativePath[0] ) {
		if ( !warnedEmptyReadPath ) {
			common->DWarning( "idFileSystemLocal::ReadFile called with empty name; treating as missing file" );
			warnedEmptyReadPath = true;
		}
		return -1;
	}

	buf = NULL;	// quiet compiler warning

	// if this is a .cfg file and we are playing back a journal, read
	// it from the journal file
	if ( strstr( relativePath, ".cfg" ) == relativePath + strlen( relativePath ) - 4 ) {
		isConfig = true;
		if ( eventLoop && eventLoop->JournalLevel() == 2 ) {
			int		r;

			loadCount++;
			loadStack++;

			common->DPrintf( "Loading %s from journal file.\n", relativePath );
			len = 0;
			r = eventLoop->com_journalDataFile->Read( &len, sizeof( len ) );
			if ( r != sizeof( len ) ) {
				*buffer = NULL;
				return -1;
			}
			buf = (byte *)Mem_ClearedAlloc(len+1);
			*buffer = buf;
			r = eventLoop->com_journalDataFile->Read( buf, len );
			if ( r != len ) {
				common->FatalError( "Read from journalDataFile failed" );
			}

			// guarantee that it will have a trailing 0 for string operations
			buf[len] = 0;

			return len;
		}
	} else {
		isConfig = false;
	}

	// look for it in the filesystem or pack files
	f = OpenFileRead( relativePath, ( buffer != NULL ) );
	if ( f == NULL ) {
		if ( buffer ) {
			*buffer = NULL;
		}
		return -1;
	}
	len = f->Length();

	if ( timestamp ) {
		*timestamp = f->Timestamp();
	}
	
	if ( !buffer ) {
		CloseFile( f );
		return len;
	}

	loadCount++;
	loadStack++;

	buf = (byte *)Mem_ClearedAlloc(len+1);
	*buffer = buf;

	f->Read( buf, len );

	// guarantee that it will have a trailing 0 for string operations
	buf[len] = 0;
	CloseFile( f );

	// if we are journalling and it is a config file, write it to the journal file
	if ( isConfig && eventLoop && eventLoop->JournalLevel() == 1 ) {
		common->DPrintf( "Writing %s to journal file.\n", relativePath );
		eventLoop->com_journalDataFile->Write( &len, sizeof( len ) );
		eventLoop->com_journalDataFile->Write( buf, len );
		eventLoop->com_journalDataFile->Flush();
	}

	return len;
}

/*
=============
idFileSystemLocal::FreeFile
=============
*/
void idFileSystemLocal::FreeFile( void *buffer ) {
	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}
	if ( !buffer ) {
		common->FatalError( "idFileSystemLocal::FreeFile( NULL )" );
	}
	loadStack--;

	Mem_Free( buffer );
}

/*
============
idFileSystemLocal::WriteFile

Filenames are relative to the search path
============
*/
int idFileSystemLocal::WriteFile( const char *relativePath, const void *buffer, int size, const char *basePath ) {
	idFile *f;

	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}

	if ( !relativePath || !buffer ) {
		common->FatalError( "idFileSystemLocal::WriteFile: NULL parameter" );
	}
	const char *invalidReason;
	if ( !FS_ValidateRelativeWritePath( relativePath, &invalidReason ) ) {
		common->Warning( "idFileSystemLocal::WriteFile: refusing unsafe relative path (%s)", invalidReason );
		return -1;
	}

	f = idFileSystemLocal::OpenFileWrite( relativePath, basePath );
	if ( !f ) {
		common->Printf( "Failed to open %s\n", relativePath );
		return -1;
	}

	size = f->Write( buffer, size );

	CloseFile( f );

	return size;
}

/*
============
idFileSystemLocal::PromoteFile

Promotes a staged qpath with the host's atomic replace primitive.  Copying is
deliberately not a fallback: an interrupted copy could destroy the prior final
artifact and would violate the caller's transaction guarantee.
============
*/
bool idFileSystemLocal::PromoteFile( const char *stagedRelativePath,
		const char *finalRelativePath, const char *basePath ) {
	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}

	const char *stagedReason = NULL;
	const char *finalReason = NULL;
	if ( !FS_ValidateRelativeWritePath( stagedRelativePath, &stagedReason ) ||
		 !FS_ValidateRelativeWritePath( finalRelativePath, &finalReason ) ) {
		common->Warning( "idFileSystemLocal::PromoteFile: refusing unsafe qpath (%s)",
			stagedReason != NULL ? stagedReason : ( finalReason != NULL ? finalReason : "invalid path" ) );
		return false;
	}
	if ( idStr::Icmp( stagedRelativePath, finalRelativePath ) == 0 ) {
		common->Warning( "idFileSystemLocal::PromoteFile: staged and final qpaths are identical" );
		return false;
	}

	const char *root = cvarSystem->GetCVarString( basePath );
	if ( root == NULL || root[ 0 ] == '\0' ) {
		root = fs_savepath.GetString();
	}
	idStr stagedOS = BuildOSPath( root, gameFolder, stagedRelativePath );
	idStr finalOS = BuildOSPath( root, gameFolder, finalRelativePath );
	CreateOSPath( finalOS );

	bool promoted = false;
#if defined( USE_SDL3 )
	promoted = SDL_RenamePath( stagedOS.c_str(), finalOS.c_str() );
	if ( !promoted ) {
		common->Warning( "Could not atomically promote '%s' to '%s': %s",
			stagedRelativePath, finalRelativePath, SDL_GetError() );
	}
#elif defined( WIN32 )
	promoted = MoveFileExA( stagedOS.c_str(), finalOS.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) != FALSE;
	if ( !promoted ) {
		common->Warning( "Could not atomically promote '%s' to '%s' (error %lu)",
			stagedRelativePath, finalRelativePath, static_cast<unsigned long>( GetLastError() ) );
	}
#else
	promoted = rename( stagedOS.c_str(), finalOS.c_str() ) == 0;
	if ( !promoted ) {
		common->Warning( "Could not atomically promote '%s' to '%s' (error %d)",
			stagedRelativePath, finalRelativePath, errno );
	}
#endif

	if ( promoted ) {
		ClearDirCache();
	}
	return promoted;
}

/*
============
idFileSystemLocal::SetIsFileLoadingAllowed
============
*/
void idFileSystemLocal::SetIsFileLoadingAllowed( bool mode ) {
	isFileLoadingAllowed = mode;
}

/*
============
idFileSystemLocal::GetIsFileLoadingAllowed
============
*/
bool idFileSystemLocal::GetIsFileLoadingAllowed() const {
	return isFileLoadingAllowed;
}

/*
============
idFileSystemLocal::SetAssetLogName
============
*/
void idFileSystemLocal::SetAssetLogName( const char *logName ) {
	currentAssetLog = "assetlogs/";
	if ( logName != NULL ) {
		currentAssetLog += logName;
	}
	currentAssetLog.BackSlashesToSlashes();
	currentAssetLog.StripFileExtension();
	currentAssetLogUnfiltered = currentAssetLog;

	const char *entityFilter = cvarSystem->GetCVarString( "si_entityFilter" );
	if ( entityFilter != NULL && entityFilter[0] != '\0' ) {
		currentAssetLog += "-";
		currentAssetLog += entityFilter;
	}

	common->Printf( "Asset log: %s\n", currentAssetLog.c_str() );
}

/*
============
idFileSystemLocal::GetAssetLogName
============
*/
const char *idFileSystemLocal::GetAssetLogName() {
	return currentAssetLog.c_str();
}

/*
============
idFileSystemLocal::ClearAssetLog
============
*/
void idFileSystemLocal::ClearAssetLog() {
	assetLog.Clear();
}

/*
============
idFileSystemLocal::AddAssetLogEntry
============
*/
void idFileSystemLocal::AddAssetLogEntry( const char *relativePath ) {
	if ( !isFileLoadingAllowed || relativePath == NULL || relativePath[0] == '\0' ) {
		return;
	}
	assetLog.AddUnique( relativePath );
}

/*
============
idFileSystemLocal::WriteAssetLog
============
*/
void idFileSystemLocal::WriteAssetLog() {
	if ( currentAssetLog.Length() == 0 ) {
		return;
	}

	idStr assetLogName = currentAssetLog;
	assetLogName.SetFileExtension( ".assets" );

	idFile *file = OpenFileWrite( assetLogName.c_str(), "fs_savepath" );
	if ( file == NULL ) {
		common->Warning( "Could not open asset log '%s' for writing", assetLogName.c_str() );
		return;
	}

	for ( int i = 0; i < assetLog.Num(); i++ ) {
		file->Printf( "%s\n", assetLog[i].c_str() );
	}

	CloseFile( file );
}

/*
============
idFileSystemLocal::GetNewFileMemory
============
*/
idFile *idFileSystemLocal::GetNewFileMemory( void ) {
	return new idFile_Memory();
}

/*
============
idFileSystemLocal::GetNewFilePermanent
============
*/
idFile *idFileSystemLocal::GetNewFilePermanent( void ) {
	return new idFile_Permanent();
}

/*
=================
idFileSystemLocal::ParseAddonDef
=================
*/
addonInfo_t *idFileSystemLocal::ParseAddonDef( const char *buf, const int len ) {
	idLexer		src;
	idToken		token, token2;
	addonInfo_t	*info;

	src.LoadMemory( buf, len, "<addon.conf>" );
	src.SetFlags( DECL_LEXER_FLAGS );
	if ( !src.SkipUntilString( "addonDef" ) ) {
		src.Warning( "ParseAddonDef: no addonDef" );
		return NULL;
	}
	if ( !src.ReadToken( &token ) ) {
		src.Warning( "Expected {" );
		return NULL;
	}
	info = new addonInfo_t;
	// read addonDef
	while ( 1 ) {
		if ( !src.ReadToken( &token ) ) {
			delete info;
			return NULL;
		}
		if ( !token.Icmp( "}" ) ) {
			break;
		}
		if ( token.type != TT_STRING ) {
			src.Warning( "Expected quoted string, but found '%s'", token.c_str() );
			delete info;
			return NULL;
		}
		unsigned int checksum;
		if ( sscanf( token.c_str(), "0x%x", &checksum ) != 1 && sscanf( token.c_str(), "%x", &checksum ) != 1 ) {
			src.Warning( "Could not parse checksum '%s'", token.c_str() );
			delete info;
			return NULL;
		}
		info->depends.Append( static_cast<int>( checksum ) );
	}
	// read any number of mapDef entries
	while ( 1 ) {
		if ( !src.SkipUntilString( "mapDef" ) ) {
			return info;
		}
		if ( !src.ReadToken( &token ) ) {
			src.Warning( "Expected map path" );
			info->mapDecls.DeleteContents( true );
			delete info;
			return NULL;
		}
		idDict *dict = new idDict;
		dict->Set( "path", token.c_str() );
		if ( !src.ReadToken( &token ) ) {
			src.Warning( "Expected {" );
			info->mapDecls.DeleteContents( true );
			delete dict;
			delete info;
			return NULL;
		}
		while ( 1 ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			if ( !token.Icmp( "}" ) ) {
				break;
			}
			if ( token.type != TT_STRING ) {
				src.Warning( "Expected quoted string, but found '%s'", token.c_str() );
				info->mapDecls.DeleteContents( true );
				delete dict;
				delete info;
				return NULL;
			}

			if ( !src.ReadToken( &token2 ) ) {
				src.Warning( "Unexpected end of file" );
				info->mapDecls.DeleteContents( true );
				delete dict;
				delete info;
				return NULL;
			}

			if ( dict->FindKey( token ) ) {
				src.Warning( "'%s' already defined", token.c_str() );
			}
			dict->Set( token, token2 );
		}
		info->mapDecls.Append( dict );
	}
	assert( false );
	return NULL;
}

/*
=================
idFileSystemLocal::LoadZipFile
=================
*/
pack_t *idFileSystemLocal::LoadZipFile( const char *zipfile ) {
	fileInPack_t *	buildBuffer;
	pack_t *		pack;
	unzFile			uf;
	int				err;
	unz_global_info gi;
	char			filename_inzip[MAX_ZIPPED_FILE_NAME];
	unz_file_info	file_info;
	int				i;
	int				hash;
	int				fs_numHeaderLongs;
	int *			fs_headerLongs;
	FILE			*f;
	int				len;
	int				confHash;
	fileInPack_t	*pakFile;
	long			fileLength;

	f = OpenOSFile( zipfile, "rb" );
	if ( !f ) {
		common->Warning( "Could not open pk4 '%s': %s", zipfile, strerror( errno ) );
		return NULL;
	}
	if ( fseek( f, 0, SEEK_END ) != 0 ) {
		common->Warning( "Could not seek pk4 '%s': %s", zipfile, strerror( errno ) );
		fclose( f );
		return NULL;
	}
	fileLength = ftell( f );
	fclose( f );
	if ( fileLength < 0 || fileLength > idMath::INT_MAX ) {
		common->Warning( "PK4 '%s' is too large for the legacy file interface", zipfile );
		return NULL;
	}
	len = static_cast<int>( fileLength );

	fs_numHeaderLongs = 0;

	uf = unzOpen( zipfile );
	if ( !uf ) {
		common->Warning( "Could not open pk4 zip '%s'", zipfile );
		return NULL;
	}
	err = unzGetGlobalInfo( uf, &gi );

	if ( err != UNZ_OK ) {
		common->Warning( "Could not read pk4 central directory '%s'", zipfile );
		unzClose( uf );
		return NULL;
	}
	if ( gi.number_entry > static_cast<unsigned long>( idMath::INT_MAX ) ) {
		common->Warning( "PK4 '%s' has too many files", zipfile );
		unzClose( uf );
		return NULL;
	}

	buildBuffer = new fileInPack_t[static_cast<size_t>( gi.number_entry )];
	pack = new pack_t;
	for( i = 0; i < FILE_HASH_SIZE; i++ ) {
		pack->hashTable[i] = NULL;
	}

	pack->pakFilename = zipfile;
	pack->handle = uf;
	pack->numfiles = static_cast<int>( gi.number_entry );
	pack->buildBuffer = buildBuffer;
	pack->referenced = false;
	pack->binary = BINARY_UNKNOWN;
	pack->addon = false;
	pack->addon_search = false;
	pack->addon_info = NULL;
	pack->pureStatus = PURE_UNKNOWN;
	pack->isNew = false;

	pack->length = len;

	err = unzGoToFirstFile( uf );
	bool zipIndexValid = ( pack->numfiles == 0 || err == UNZ_OK );
	fs_headerLongs = static_cast<int *>( Mem_ClearedAlloc(
		static_cast<size_t>( pack->numfiles ) * sizeof( int ) ) );
	for ( i = 0; zipIndexValid && i < pack->numfiles; i++ ) {
		err = unzGetCurrentFileInfo( uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0 );
		if ( err != UNZ_OK ) {
			common->Warning( "Could not read file %d from pk4 central directory '%s'", i, zipfile );
			zipIndexValid = false;
			break;
		}
		if ( file_info.uncompressed_size > static_cast<unsigned long>( idMath::INT_MAX ) ) {
			common->Warning( "File %d in pk4 '%s' is too large for the legacy file interface", i, zipfile );
			zipIndexValid = false;
			break;
		}
		if ( file_info.uncompressed_size > 0 ) {
			fs_headerLongs[fs_numHeaderLongs++] = LittleLong( file_info.crc );
		}
		hash = HashFileName( filename_inzip );
		buildBuffer[i].name = filename_inzip;
		buildBuffer[i].name.ToLower();
		buildBuffer[i].name.BackSlashesToSlashes();
		// Minizip exposes a native unsigned long, but classic ZIP stores this
		// central-directory position in exactly 32 bits.
		unsigned long fileInfoPosition = 0;
		err = unzGetCurrentFileInfoPosition( uf, &fileInfoPosition );
		if ( err != UNZ_OK || fileInfoPosition > static_cast<unsigned long>( UINT32_MAX ) ) {
			common->Warning( "Invalid file %d position in pk4 central directory '%s'", i, zipfile );
			zipIndexValid = false;
			break;
		}
		buildBuffer[i].pos = static_cast<uint32_t>( fileInfoPosition );
		// add the file to the hash
		buildBuffer[i].next = pack->hashTable[hash];
		pack->hashTable[hash] = &buildBuffer[i];
		// go to the next file in the zip
		if ( i + 1 < pack->numfiles && unzGoToNextFile( uf ) != UNZ_OK ) {
			common->Warning( "Could not advance pk4 central directory '%s'", zipfile );
			zipIndexValid = false;
		}
	}

	if ( !zipIndexValid || i != pack->numfiles ) {
		Mem_Free( fs_headerLongs );
		delete[] buildBuffer;
		unzClose( uf );
		delete pack;
		return NULL;
	}

	// check if this is an addon pak
	pack->addon = false;
	confHash = HashFileName( ADDON_CONFIG );
	for ( pakFile = pack->hashTable[confHash]; pakFile; pakFile = pakFile->next ) {
		if ( !FilenameCompare( pakFile->name, ADDON_CONFIG ) ) {			
			pack->addon = true;			
			idFile_InZip *file = ReadFileFromZip( pack, pakFile, ADDON_CONFIG );
			// may be just an empty file if you don't bother about the mapDef
			if ( file && file->Length() ) {
				char *buf;
				buf = new char[ file->Length() + 1 ];
				file->Read( (void *)buf, file->Length() );
				buf[ file->Length() ] = '\0';
				pack->addon_info = ParseAddonDef( buf, file->Length() );
				delete[] buf;
			}
			if ( file ) {
				CloseFile( file );
			}
			break;
		}
	}

	pack->checksum = MD4_BlockChecksum( fs_headerLongs, 4 * fs_numHeaderLongs );
	pack->checksum = LittleLong( pack->checksum );

	Mem_Free( fs_headerLongs );

	return pack;
}

/*
===============
idFileSystemLocal::AddZipFile
adds a downloaded pak file to the list so we can work out what we have and what we still need
the isNew flag is set to true, indicating that we cannot add this pak to the search lists without a restart
===============
*/
int idFileSystemLocal::AddZipFile( const char *path ) {
	idStr			fullpath = fs_savepath.GetString();
	pack_t			*pak;
	searchpath_t	*search, *last;

	fullpath.AppendPath( path );
	pak = LoadZipFile( fullpath );
	if ( !pak ) {
		common->Warning( "AddZipFile %s failed\n", path );
		return 0;
	}
	// insert the pak at the end of the search list - temporary until we restart
	pak->isNew = true;
	search = new searchpath_t;
	search->dir = NULL;
	search->pack = pak;
	search->next = NULL;
	last = searchPaths;
	while ( last->next ) {
		last = last->next;
	}
	last->next = search;
	common->Printf( "Appended pk4 %s with checksum 0x%x\n", pak->pakFilename.c_str(), pak->checksum );
	return pak->checksum;
}

/*
===============
idFileSystemLocal::AddUnique
===============
*/
int idFileSystemLocal::AddUnique( const char *name, idStrList &list, idHashIndex &hashIndex ) const {
	int i, hashKey;

	hashKey = hashIndex.GenerateKey( name );
	for ( i = hashIndex.First( hashKey ); i >= 0; i = hashIndex.Next( i ) ) {
		if ( list[i].Icmp( name ) == 0 ) {
			return i;
		}
	}
	i = list.Append( name );
	hashIndex.Add( hashKey, i );
	return i;
}

/*
===============
idFileSystemLocal::GetExtensionList
===============
*/
void idFileSystemLocal::GetExtensionList( const char *extension, idStrList &extensionList ) const {
	int s, e, l;

	l = idStr::Length( extension );
	s = 0;
	while( 1 ) {
		e = idStr::FindChar( extension, '|', s, l );
		if ( e != -1 ) {
			extensionList.Append( idStr( extension, s, e ) );
			s = e + 1;
		} else {
			extensionList.Append( idStr( extension, s, l ) );
			break;
		}
	}
}

/*
===============
idFileSystemLocal::GetFileList

Does not clear the list first so this can be used to progressively build a file list.
When 'sort' is true only the new files added to the list are sorted.
===============
*/
int idFileSystemLocal::GetFileList( const char *relativePath, const idStrList &extensions, idStrList &list, idHashIndex &hashIndex, bool fullRelativePath, const char* gamedir ) {
	searchpath_t *	search;
	fileInPack_t *	buildBuffer;
	int				i, j;
	int				pathLength;
	int				length;
	const char *	name;
	pack_t *		pak;
	idStr			work;

	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}

	if ( !extensions.Num() ) {
		return 0;
	}

	if ( !relativePath ) {
		return 0;
	}
	const size_t relativePathLength = strlen( relativePath );
	pathLength = relativePathLength == 0 ? 0 :
		idLib::SizeToInt( relativePathLength + 1, "idFileSystemLocal::GetFileList" ); // include the trailing '/'

	// search through the path, one element at a time, adding to list
	for( search = searchPaths; search != NULL; search = search->next ) {
		if ( search->dir ) {
			if(gamedir && strlen(gamedir)) {
				if(search->dir->gamedir != gamedir) {
					continue;
				}
			}

			idStrList	sysFiles;
			idStr		netpath;

			netpath = BuildOSPath( search->dir->path, search->dir->gamedir, relativePath );

			for ( i = 0; i < extensions.Num(); i++ ) {

				// scan for files in the filesystem
				ListOSFiles( netpath, extensions[i], sysFiles );

				// if we are searching for directories, remove . and ..
				if ( extensions[i][0] == '/' && extensions[i][1] == 0 ) {
					sysFiles.Remove( "." );
					sysFiles.Remove( ".." );
				}

				for( j = 0; j < sysFiles.Num(); j++ ) {
					// unique the match
					if ( fullRelativePath ) {
						work = relativePath;
						work += "/";
						work += sysFiles[j];
						AddUnique( work, list, hashIndex );
					}
					else {
						AddUnique( sysFiles[j], list, hashIndex );
					}
				}
			}
		} else if ( search->pack ) {
			// look through all the pak file elements

			// exclude any extra packs if we have server paks to search
			if ( serverPaks.Num() ) {
				GetPackStatus( search->pack );
				if ( search->pack->pureStatus != PURE_NEVER && !serverPaks.Find( search->pack ) ) {
					continue; // not on the pure server pak list
				}
			}

			pak = search->pack;
			buildBuffer = pak->buildBuffer;
			for( i = 0; i < pak->numfiles; i++ ) {

				length = buildBuffer[i].name.Length();

				// if the name is not long anough to at least contain the path
				if ( length <= pathLength ) {
					continue;
				}

				name = buildBuffer[i].name;


				// check for a path match without the trailing '/'
				if ( pathLength && idStr::Icmpn( name, relativePath, pathLength - 1 ) != 0 ) {
					continue;
				}
 
				// ensure we have a path, and not just a filename containing the path
				if ( name[ pathLength ] == '\0' || name[pathLength - 1] != '/' ) {
					continue;
				}
 
				// make sure the file is not in a subdirectory
				for ( j = pathLength; name[j+1] != '\0'; j++ ) {
					if ( name[j] == '/' ) {
						break;
					}
				}
				if ( name[j+1] ) {
					continue;
				}

				// check for extension match
				for ( j = 0; j < extensions.Num(); j++ ) {
					if ( length >= extensions[j].Length() && extensions[j].Icmp( name + length - extensions[j].Length() ) == 0 ) {
						break;
					}
				}
				if ( j >= extensions.Num() ) {
					continue;
				}

				// unique the match
				if ( fullRelativePath ) {
					work = relativePath;
					work += "/";
					work += name + pathLength;
					work.StripTrailing( '/' );
					AddUnique( work, list, hashIndex );
				} else {
					work = name + pathLength;
					work.StripTrailing( '/' );
					AddUnique( work, list, hashIndex );
				}
			}
		}
	}

	return list.Num();
}

/*
===============
idFileSystemLocal::ListFiles
===============
*/
idFileList *idFileSystemLocal::ListFiles( const char *relativePath, const char *extension, bool sort, bool fullRelativePath, const char* gamedir ) {
	idHashIndex hashIndex( 4096, 4096 );
	idStrList extensionList;

	idFileList *fileList = new idFileList;
	fileList->basePath = relativePath;

	GetExtensionList( extension, extensionList );

	GetFileList( relativePath, extensionList, fileList->list, hashIndex, fullRelativePath, gamedir );

	if ( sort ) {
		idStrListSortPaths( fileList->list );
	}

	return fileList;
}

/*
===============
idFileSystemLocal::GetFileListTree
===============
*/
int idFileSystemLocal::GetFileListTree( const char *relativePath, const idStrList &extensions, idStrList &list, idHashIndex &hashIndex, const char* gamedir ) {
	int i;
	idStrList slash, folders( 128 );
	idHashIndex folderHashIndex( 1024, 128 );

	// recurse through the subdirectories
	slash.Append( "/" );
	GetFileList( relativePath, slash, folders, folderHashIndex, true, gamedir );
	for ( i = 0; i < folders.Num(); i++ ) {
		if ( folders[i][0] == '.' ) {
			continue;
		}
		if ( folders[i].Icmp( relativePath ) == 0 ){
			continue;
		}
		GetFileListTree( folders[i], extensions, list, hashIndex, gamedir );
	}

	// list files in the current directory
	GetFileList( relativePath, extensions, list, hashIndex, true, gamedir );

	return list.Num();
}

/*
===============
idFileSystemLocal::ListFilesTree
===============
*/
idFileList *idFileSystemLocal::ListFilesTree( const char *relativePath, const char *extension, bool sort, const char* gamedir ) {
	idHashIndex hashIndex( 4096, 4096 );
	idStrList extensionList;

	idFileList *fileList = new idFileList();
	fileList->basePath = relativePath;
	fileList->list.SetGranularity( 4096 );

	GetExtensionList( extension, extensionList );

	GetFileListTree( relativePath, extensionList, fileList->list, hashIndex, gamedir );

	if ( sort ) {
		idStrListSortPaths( fileList->list );
	}

	return fileList;
}

/*
===============
idFileSystemLocal::FreeFileList
===============
*/
void idFileSystemLocal::FreeFileList( idFileList *fileList ) {
	delete fileList;
}

/*
===============
idFileSystemLocal::ListAvailableLanguagePacks
===============
*/
void idFileSystemLocal::ListAvailableLanguagePacks( idStrList &languages ) {
	idStr language;

	languages.Clear();

	for ( searchpath_t *search = searchPaths; search != NULL; search = search->next ) {
		if ( search->pack == NULL ) {
			continue;
		}
		if ( !FS_PakPathIsInGameDir( search->pack->pakFilename.c_str(), BASE_GAMEDIR ) ) {
			continue;
		}
		if ( FS_ParseLanguagePackName( search->pack->pakFilename.c_str(), language ) ) {
			FS_AppendUniqueLanguage( languages, language.c_str() );
		}
	}

	FS_OrderLanguagePackList( languages );
}

/*
===============
idFileSystemLocal::HasBaseLanguageMediaPack

The numbered zpak_<language>_NN.pk4 archives are patches. They can identify a
language for dictionary selection, but they do not replace the unsuffixed base
archive that carries the retail campaign dialogue.
===============
*/
bool idFileSystemLocal::HasBaseLanguageMediaPack( void ) {
	idStr language;

	for ( searchpath_t *search = searchPaths; search != NULL; search = search->next ) {
		if ( search->pack == NULL ) {
			continue;
		}
		if ( !FS_PakPathIsInGameDir( search->pack->pakFilename.c_str(), BASE_GAMEDIR ) ) {
			continue;
		}
		if ( FS_ParseLanguagePackName( search->pack->pakFilename.c_str(), language, false ) ) {
			return true;
		}
	}

	return false;
}

static const char *OPENQ4_MOD_MANIFEST_FILENAME = "mod.json";

/*
===============
idModInfoCompare
===============
*/
static int idModInfoCompare( const idModInfo *a, const idModInfo *b ) {
	const int displayNameCompare = a->displayName.Icmp( b->displayName );
	if ( displayNameCompare != 0 ) {
		return displayNameCompare;
	}

	return a->directory.Icmp( b->directory );
}

/*
===============
FS_FindModDirectory
===============
*/
static int FS_FindModDirectory( const idList<idModInfo> &mods, const char *directory ) {
	for ( int i = 0; i < mods.Num(); ++i ) {
		if ( !mods[i].directory.Icmp( directory ) ) {
			return i;
		}
	}

	return -1;
}

/*
===============
FS_SkipJsonWhitespace
===============
*/
static const char *FS_SkipJsonWhitespace( const char *cursor ) {
	while ( cursor != NULL && ( *cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' ) ) {
		++cursor;
	}

	return cursor;
}

/*
===============
FS_IsJsonHexDigit
===============
*/
static bool FS_IsJsonHexDigit( const char c ) {
	return ( c >= '0' && c <= '9' ) ||
		   ( c >= 'a' && c <= 'f' ) ||
		   ( c >= 'A' && c <= 'F' );
}

/*
===============
FS_JsonHexValue
===============
*/
static int FS_JsonHexValue( const char c ) {
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return 10 + c - 'a';
	}
	if ( c >= 'A' && c <= 'F' ) {
		return 10 + c - 'A';
	}
	return -1;
}

/*
===============
FS_AppendJsonCodePoint
===============
*/
static bool FS_AppendJsonCodePoint( idStr &value, const unsigned int codePoint, idStr &errorOut ) {
	if ( codePoint == 0 ) {
		errorOut = "NUL characters are not supported in JSON strings";
		return false;
	}

	if ( codePoint <= 0x7F ) {
		value += static_cast<char>( codePoint );
		return true;
	}

	char utf8[ 4 ];
	int utf8Length = 0;
	if ( codePoint <= 0x7FF ) {
		utf8[ 0 ] = static_cast<char>( 0xC0 | ( codePoint >> 6 ) );
		utf8[ 1 ] = static_cast<char>( 0x80 | ( codePoint & 0x3F ) );
		utf8Length = 2;
	} else if ( codePoint <= 0xFFFF ) {
		utf8[ 0 ] = static_cast<char>( 0xE0 | ( codePoint >> 12 ) );
		utf8[ 1 ] = static_cast<char>( 0x80 | ( ( codePoint >> 6 ) & 0x3F ) );
		utf8[ 2 ] = static_cast<char>( 0x80 | ( codePoint & 0x3F ) );
		utf8Length = 3;
	} else if ( codePoint <= 0x10FFFF ) {
		utf8[ 0 ] = static_cast<char>( 0xF0 | ( codePoint >> 18 ) );
		utf8[ 1 ] = static_cast<char>( 0x80 | ( ( codePoint >> 12 ) & 0x3F ) );
		utf8[ 2 ] = static_cast<char>( 0x80 | ( ( codePoint >> 6 ) & 0x3F ) );
		utf8[ 3 ] = static_cast<char>( 0x80 | ( codePoint & 0x3F ) );
		utf8Length = 4;
	} else {
		errorOut = "JSON unicode escape is outside the valid Unicode range";
		return false;
	}

	value.Append( utf8, utf8Length );
	return true;
}

/*
===============
FS_ParseJsonHex4
===============
*/
static bool FS_ParseJsonHex4( const char *&cursor, unsigned int &codePoint, idStr &errorOut ) {
	codePoint = 0;
	for ( int i = 0; i < 4; ++i ) {
		if ( cursor == NULL || !FS_IsJsonHexDigit( *cursor ) ) {
			errorOut = "invalid JSON unicode escape";
			return false;
		}
		codePoint = ( codePoint << 4 ) | FS_JsonHexValue( *cursor );
		++cursor;
	}

	return true;
}

/*
===============
FS_ParseJsonUnicodeEscape
===============
*/
static bool FS_ParseJsonUnicodeEscape( const char *&cursor, idStr &value, idStr &errorOut ) {
	unsigned int codePoint = 0;
	if ( !FS_ParseJsonHex4( cursor, codePoint, errorOut ) ) {
		return false;
	}

	if ( codePoint >= 0xD800 && codePoint <= 0xDBFF ) {
		if ( cursor == NULL || cursor[ 0 ] != '\\' || cursor[ 1 ] != 'u' ) {
			errorOut = "high surrogate JSON unicode escape is missing a low surrogate";
			return false;
		}
		cursor += 2;

		unsigned int lowSurrogate = 0;
		if ( !FS_ParseJsonHex4( cursor, lowSurrogate, errorOut ) ) {
			return false;
		}
		if ( lowSurrogate < 0xDC00 || lowSurrogate > 0xDFFF ) {
			errorOut = "high surrogate JSON unicode escape is not followed by a low surrogate";
			return false;
		}

		codePoint = 0x10000 + ( ( codePoint - 0xD800 ) << 10 ) + ( lowSurrogate - 0xDC00 );
	} else if ( codePoint >= 0xDC00 && codePoint <= 0xDFFF ) {
		errorOut = "low surrogate JSON unicode escape appears without a high surrogate";
		return false;
	}

	return FS_AppendJsonCodePoint( value, codePoint, errorOut );
}

/*
===============
FS_ParseJsonString
===============
*/
static bool FS_ParseJsonString( const char *&cursor, idStr &value, idStr &errorOut ) {
	value.Clear();

	cursor = FS_SkipJsonWhitespace( cursor );
	if ( cursor == NULL || *cursor != '"' ) {
		errorOut = "expected JSON string";
		return false;
	}

	++cursor;
	while ( *cursor != '\0' ) {
		if ( *cursor == '"' ) {
			++cursor;
			return true;
		}

		if ( *cursor == '\\' ) {
			++cursor;
			if ( *cursor == '\0' ) {
				errorOut = "unterminated escape sequence in JSON string";
				return false;
			}

			switch ( *cursor ) {
				case '"':
				case '\\':
				case '/':
					value += *cursor;
					++cursor;
					break;
				case 'b':
					value += '\b';
					++cursor;
					break;
				case 'f':
					value += '\f';
					++cursor;
					break;
				case 'n':
					value += '\n';
					++cursor;
					break;
				case 'r':
					value += '\r';
					++cursor;
					break;
				case 't':
					value += '\t';
					++cursor;
					break;
				case 'u':
					++cursor;
					if ( !FS_ParseJsonUnicodeEscape( cursor, value, errorOut ) ) {
						return false;
					}
					break;
				default:
					errorOut = va( "unsupported JSON escape '\\%c'", *cursor );
					return false;
			}

			continue;
		}

		if ( static_cast<unsigned char>( *cursor ) < 0x20 ) {
			errorOut = "unescaped control character in JSON string";
			return false;
		}

		value += *cursor;
		++cursor;
	}

	errorOut = "unterminated JSON string";
	return false;
}

static bool FS_SkipJsonValue( const char *&cursor, idStr &errorOut );

/*
===============
FS_SkipJsonLiteral
===============
*/
static bool FS_SkipJsonLiteral( const char *&cursor, const char *literal, idStr &errorOut ) {
	const char *scan = cursor;
	for ( const char *expected = literal; *expected != '\0'; ++expected, ++scan ) {
		if ( scan == NULL || *scan != *expected ) {
			errorOut = va( "expected JSON literal '%s'", literal );
			return false;
		}
	}

	cursor = scan;
	return true;
}

/*
===============
FS_SkipJsonNumber
===============
*/
static bool FS_SkipJsonNumber( const char *&cursor, idStr &errorOut ) {
	const char *scan = cursor;
	if ( *scan == '-' ) {
		++scan;
	}

	if ( *scan == '0' ) {
		++scan;
	} else if ( *scan >= '1' && *scan <= '9' ) {
		do {
			++scan;
		} while ( *scan >= '0' && *scan <= '9' );
	} else {
		errorOut = "invalid JSON number";
		return false;
	}

	if ( *scan == '.' ) {
		++scan;
		if ( *scan < '0' || *scan > '9' ) {
			errorOut = "invalid JSON number fraction";
			return false;
		}
		do {
			++scan;
		} while ( *scan >= '0' && *scan <= '9' );
	}

	if ( *scan == 'e' || *scan == 'E' ) {
		++scan;
		if ( *scan == '+' || *scan == '-' ) {
			++scan;
		}
		if ( *scan < '0' || *scan > '9' ) {
			errorOut = "invalid JSON number exponent";
			return false;
		}
		do {
			++scan;
		} while ( *scan >= '0' && *scan <= '9' );
	}

	cursor = scan;
	return true;
}

/*
===============
FS_SkipJsonArray
===============
*/
static bool FS_SkipJsonArray( const char *&cursor, idStr &errorOut ) {
	++cursor;
	cursor = FS_SkipJsonWhitespace( cursor );
	if ( cursor == NULL ) {
		errorOut = "unterminated JSON array";
		return false;
	}
	if ( *cursor == ']' ) {
		++cursor;
		return true;
	}

	while ( true ) {
		if ( !FS_SkipJsonValue( cursor, errorOut ) ) {
			return false;
		}

		cursor = FS_SkipJsonWhitespace( cursor );
		if ( cursor == NULL || *cursor == '\0' ) {
			errorOut = "unterminated JSON array";
			return false;
		}
		if ( *cursor == ',' ) {
			++cursor;
			continue;
		}
		if ( *cursor == ']' ) {
			++cursor;
			return true;
		}

		errorOut = "expected ',' or ']' after JSON array value";
		return false;
	}
}

/*
===============
FS_SkipJsonObject
===============
*/
static bool FS_SkipJsonObject( const char *&cursor, idStr &errorOut ) {
	++cursor;
	cursor = FS_SkipJsonWhitespace( cursor );
	if ( cursor == NULL ) {
		errorOut = "unterminated JSON object";
		return false;
	}
	if ( *cursor == '}' ) {
		++cursor;
		return true;
	}

	while ( true ) {
		idStr key;
		if ( !FS_ParseJsonString( cursor, key, errorOut ) ) {
			return false;
		}

		cursor = FS_SkipJsonWhitespace( cursor );
		if ( cursor == NULL || *cursor != ':' ) {
			errorOut = "missing ':' after JSON object key";
			return false;
		}
		++cursor;

		if ( !FS_SkipJsonValue( cursor, errorOut ) ) {
			return false;
		}

		cursor = FS_SkipJsonWhitespace( cursor );
		if ( cursor == NULL || *cursor == '\0' ) {
			errorOut = "unterminated JSON object";
			return false;
		}
		if ( *cursor == ',' ) {
			++cursor;
			continue;
		}
		if ( *cursor == '}' ) {
			++cursor;
			return true;
		}

		errorOut = "expected ',' or '}' after JSON object value";
		return false;
	}
}

/*
===============
FS_SkipJsonValue
===============
*/
static bool FS_SkipJsonValue( const char *&cursor, idStr &errorOut ) {
	cursor = FS_SkipJsonWhitespace( cursor );
	if ( cursor == NULL || *cursor == '\0' ) {
		errorOut = "expected JSON value";
		return false;
	}

	if ( *cursor == '"' ) {
		idStr ignored;
		return FS_ParseJsonString( cursor, ignored, errorOut );
	}
	if ( *cursor == '{' ) {
		return FS_SkipJsonObject( cursor, errorOut );
	}
	if ( *cursor == '[' ) {
		return FS_SkipJsonArray( cursor, errorOut );
	}
	if ( *cursor == 't' ) {
		return FS_SkipJsonLiteral( cursor, "true", errorOut );
	}
	if ( *cursor == 'f' ) {
		return FS_SkipJsonLiteral( cursor, "false", errorOut );
	}
	if ( *cursor == 'n' ) {
		return FS_SkipJsonLiteral( cursor, "null", errorOut );
	}
	if ( *cursor == '-' || ( *cursor >= '0' && *cursor <= '9' ) ) {
		return FS_SkipJsonNumber( cursor, errorOut );
	}

	errorOut = "expected JSON value";
	return false;
}

/*
===============
FS_ModManifestKeyIsKnown
===============
*/
static bool FS_ModManifestKeyIsKnown( const idStr &key ) {
	return !key.Icmp( "name" ) ||
		   !key.Icmp( "version" ) ||
		   !key.Icmp( "releaseDate" ) ||
		   !key.Icmp( "website" ) ||
		   !key.Icmp( "author" ) ||
		   !key.Icmp( "requiredopenQ4Version" );
}

/*
===============
FS_BuildModListLabel
===============
*/
static idStr FS_BuildModListLabel( const idModInfo &modInfo ) {
	idStr label = modInfo.displayName;
	label.Replace( "\t", " " );
	label.Replace( "\r", " " );
	label.Replace( "\n", " " );

	idStr version = modInfo.version;
	version.Replace( "\t", " " );
	version.Replace( "\r", " " );
	version.Replace( "\n", " " );

	return va( "%s\t%s", label.c_str(), version.c_str() );
}

/*
===============
FS_FinalizeModInfo
===============
*/
static void FS_FinalizeModInfo( idModInfo &modInfo ) {
	modInfo.listLabel = FS_BuildModListLabel( modInfo );
}

typedef struct openQ4BaseVersion_s {
	int major;
	int minor;
	int patch;
} openQ4BaseVersion_t;

/*
===============
FS_ParseopenQ4BaseVersion
===============
*/
static bool FS_ParseopenQ4BaseVersion( const char *version, openQ4BaseVersion_t &parsed ) {
	int values[3] = { 0, 0, 0 };

	if ( version == NULL || version[0] == '\0' ) {
		return false;
	}

	const char *cursor = version;
	for ( int part = 0; part < 3; ++part ) {
		if ( *cursor < '0' || *cursor > '9' ) {
			return false;
		}

		int value = 0;
		while ( *cursor >= '0' && *cursor <= '9' ) {
			value = ( value * 10 ) + ( *cursor - '0' );
			++cursor;
		}

		values[part] = value;

		if ( part < 2 ) {
			if ( *cursor != '.' ) {
				return false;
			}
			++cursor;
		}
	}

	if ( *cursor != '\0' ) {
		return false;
	}

	parsed.major = values[0];
	parsed.minor = values[1];
	parsed.patch = values[2];
	return true;
}

/*
===============
FS_CompareopenQ4BaseVersions
===============
*/
static int FS_CompareopenQ4BaseVersions( const openQ4BaseVersion_t &left, const openQ4BaseVersion_t &right ) {
	if ( left.major != right.major ) {
		return left.major < right.major ? -1 : 1;
	}
	if ( left.minor != right.minor ) {
		return left.minor < right.minor ? -1 : 1;
	}
	if ( left.patch != right.patch ) {
		return left.patch < right.patch ? -1 : 1;
	}
	return 0;
}

/*
===============
FS_ParseModManifest
===============
*/
static bool FS_ParseModManifest( const char *jsonText, idModInfo &modInfo, idStr &errorOut ) {
	errorOut.Clear();
	modInfo.displayName.Clear();
	modInfo.version.Clear();
	modInfo.releaseDate.Clear();
	modInfo.website.Clear();
	modInfo.author.Clear();
	modInfo.requiredopenQ4Version.Clear();
	modInfo.listLabel.Clear();

	if ( jsonText == NULL ) {
		errorOut = "manifest content is null";
		return false;
	}

	const char *cursor = FS_SkipJsonWhitespace( jsonText );
	if ( cursor == NULL || *cursor != '{' ) {
		errorOut = "manifest must begin with '{'";
		return false;
	}

	++cursor;
	while ( true ) {
		idStr key;
		idStr value;

		cursor = FS_SkipJsonWhitespace( cursor );
		if ( cursor == NULL || *cursor == '\0' ) {
			errorOut = "manifest ended before closing '}'";
			return false;
		}
		if ( *cursor == '}' ) {
			++cursor;
			break;
		}

		if ( !FS_ParseJsonString( cursor, key, errorOut ) ) {
			return false;
		}

		cursor = FS_SkipJsonWhitespace( cursor );
		if ( *cursor != ':' ) {
			errorOut = va( "missing ':' after '%s'", key.c_str() );
			return false;
		}
		++cursor;

		if ( FS_ModManifestKeyIsKnown( key ) ) {
			cursor = FS_SkipJsonWhitespace( cursor );
			if ( cursor == NULL || *cursor != '"' ) {
				errorOut = va( "field '%s' must be a JSON string", key.c_str() );
				return false;
			}
			if ( !FS_ParseJsonString( cursor, value, errorOut ) ) {
				errorOut = va( "invalid value for '%s': %s", key.c_str(), errorOut.c_str() );
				return false;
			}

			if ( !key.Icmp( "name" ) ) {
				modInfo.displayName = value;
			} else if ( !key.Icmp( "version" ) ) {
				modInfo.version = value;
			} else if ( !key.Icmp( "releaseDate" ) ) {
				modInfo.releaseDate = value;
			} else if ( !key.Icmp( "website" ) ) {
				modInfo.website = value;
			} else if ( !key.Icmp( "author" ) ) {
				modInfo.author = value;
			} else if ( !key.Icmp( "requiredopenQ4Version" ) ) {
				modInfo.requiredopenQ4Version = value;
			}
		} else if ( !FS_SkipJsonValue( cursor, errorOut ) ) {
			errorOut = va( "invalid value for '%s': %s", key.c_str(), errorOut.c_str() );
			return false;
		}

		cursor = FS_SkipJsonWhitespace( cursor );
		if ( *cursor == ',' ) {
			++cursor;
			cursor = FS_SkipJsonWhitespace( cursor );
			if ( cursor == NULL || *cursor == '\0' ) {
				errorOut = "manifest ended after trailing comma";
				return false;
			}
			if ( *cursor == '}' ) {
				errorOut = "trailing comma before closing '}'";
				return false;
			}
			continue;
		}
		if ( *cursor == '}' ) {
			++cursor;
			break;
		}
		if ( *cursor == '\0' ) {
			errorOut = "manifest ended before closing '}'";
			return false;
		}

		errorOut = "expected ',' or '}' after manifest value";
		return false;
	}

	cursor = FS_SkipJsonWhitespace( cursor );
	if ( cursor == NULL || *cursor != '\0' ) {
		errorOut = "unexpected data after manifest object";
		return false;
	}

	if ( modInfo.displayName.IsEmpty() ) {
		errorOut = "missing required field 'name'";
		return false;
	}
	if ( modInfo.version.IsEmpty() ) {
		errorOut = "missing required field 'version'";
		return false;
	}
	if ( modInfo.releaseDate.IsEmpty() ) {
		errorOut = "missing required field 'releaseDate'";
		return false;
	}
	if ( modInfo.website.IsEmpty() ) {
		errorOut = "missing required field 'website'";
		return false;
	}
	if ( modInfo.author.IsEmpty() ) {
		errorOut = "missing required field 'author'";
		return false;
	}
	if ( modInfo.requiredopenQ4Version.IsEmpty() ) {
		errorOut = "missing required field 'requiredopenQ4Version'";
		return false;
	}

	FS_FinalizeModInfo( modInfo );
	return true;
}

/*
===============
idFileSystemLocal::ListMods
===============
*/
idModList *idFileSystemLocal::ListMods( void ) {
	idStrList	dirs;
	idModList	*list = new idModList;

	const char	*search[ 3 ];
	int			isearch;

	search[0] = fs_cdpath.GetString();
	search[1] = fs_basepath.GetString();
	search[2] = fs_savepath.GetString();

	for ( isearch = 0; isearch < 3; isearch++ ) {
		if ( !search[ isearch ] || !search[ isearch ][ 0 ] ) {
			continue;
		}

		dirs.Clear();

		// scan for directories
		ListOSFiles( search[ isearch ], "/", dirs );

		dirs.Remove( "." );
		dirs.Remove( ".." );
		dirs.Remove( "base" );
		dirs.Remove( "pb" );

		for ( int i = dirs.Num() - 1; i >= 0; --i ) {
			if ( dirs[ i ].HasUpper() ) {
				dirs.RemoveIndex( i );
			}
		}

		for ( int i = 0; i < dirs.Num(); i++ ) {
			if ( FS_FindModDirectory( list->mods, dirs[ i ] ) >= 0 ) {
				continue;
			}

			idModInfo modInfo;
			idStr reason;
			const modManifestStatus_t manifestStatus = ReadModManifestFromSearchPath( search[ isearch ], dirs[ i ], modInfo, &reason );
			if ( manifestStatus == MOD_MANIFEST_VALID ) {
				list->mods.Append( modInfo );
				continue;
			}

			if ( manifestStatus == MOD_MANIFEST_INVALID && reason.Length() ) {
				common->Warning( "Skipping mod '%s': %s", dirs[ i ].c_str(), reason.c_str() );
			}
		}
	}

	list->mods.Sort( idModInfoCompare );

	return list;
}

/*
===============
idFileSystemLocal::ReadModManifestFile
===============
*/
modManifestStatus_t idFileSystemLocal::ReadModManifestFile( const char *manifestPath, idModInfo &modInfo, idStr *reason ) {
	if ( reason != NULL ) {
		reason->Clear();
	}

	FILE *file = OpenOSFile( manifestPath, "rb" );
	if ( file == NULL ) {
		return MOD_MANIFEST_MISSING;
	}

	const int length = DirectFileLength( file );
	if ( length <= 0 ) {
		if ( reason != NULL ) {
			*reason = va( "manifest '%s' is empty", manifestPath );
		}
		fclose( file );
		return MOD_MANIFEST_INVALID;
	}

	idList<char> buffer;
	buffer.SetNum( length + 1 );
	const int bytesRead = static_cast<int>( fread( buffer.Ptr(), 1, length, file ) );
	buffer[ length ] = '\0';
	fclose( file );

	if ( bytesRead != length ) {
		if ( reason != NULL ) {
			*reason = va( "failed to read manifest '%s'", manifestPath );
		}
		return MOD_MANIFEST_INVALID;
	}

	idStr parseError;
	if ( !FS_ParseModManifest( buffer.Ptr(), modInfo, parseError ) ) {
		if ( reason != NULL ) {
			*reason = va( "%s: %s", manifestPath, parseError.c_str() );
		}
		return MOD_MANIFEST_INVALID;
	}

	openQ4BaseVersion_t requiredVersion;
	if ( !FS_ParseopenQ4BaseVersion( modInfo.requiredopenQ4Version.c_str(), requiredVersion ) ) {
		if ( reason != NULL ) {
			*reason = va(
				"%s has invalid required openQ4 version '%s' (expected major.minor.patch)",
				modInfo.displayName.c_str(),
				modInfo.requiredopenQ4Version.c_str() );
		}
		return MOD_MANIFEST_INVALID;
	}

	openQ4BaseVersion_t engineVersion;
	if ( !FS_ParseopenQ4BaseVersion( OPENQ4_VERSION_BASE, engineVersion ) ) {
		if ( reason != NULL ) {
			*reason = va( "this build has invalid openQ4 version '%s'", OPENQ4_VERSION_BASE );
		}
		return MOD_MANIFEST_INVALID;
	}

	if ( FS_CompareopenQ4BaseVersions( engineVersion, requiredVersion ) < 0 ) {
		if ( reason != NULL ) {
			*reason = va(
				"%s requires openQ4 %s or newer but this build is %s",
				modInfo.displayName.c_str(),
				modInfo.requiredopenQ4Version.c_str(),
				OPENQ4_VERSION_BASE );
		}
		return MOD_MANIFEST_INVALID;
	}

	return MOD_MANIFEST_VALID;
}

/*
===============
idFileSystemLocal::ReadModManifestFromSearchPath
===============
*/
modManifestStatus_t idFileSystemLocal::ReadModManifestFromSearchPath( const char *searchPath, const char *modDir, idModInfo &modInfo, idStr *reason ) {
	if ( !searchPath || !searchPath[ 0 ] || !modDir || !modDir[ 0 ] ) {
		if ( reason != NULL ) {
			reason->Clear();
		}
		return MOD_MANIFEST_MISSING;
	}

	const idStr manifestPath = BuildOSPath( searchPath, modDir, OPENQ4_MOD_MANIFEST_FILENAME );
	const modManifestStatus_t status = ReadModManifestFile( manifestPath.c_str(), modInfo, reason );
	if ( status != MOD_MANIFEST_VALID ) {
		return status;
	}

	modInfo.directory = modDir;
	return MOD_MANIFEST_VALID;
}

/*
===============
idFileSystemLocal::GetModInfo
===============
*/
bool idFileSystemLocal::GetModInfo( const char *modDir, idModInfo &modInfo, idStr *reason ) {
	if ( reason != NULL ) {
		reason->Clear();
	}

	if ( modDir == NULL || modDir[ 0 ] == '\0' ) {
		if ( reason != NULL ) {
			*reason = "mod directory is empty";
		}
		return false;
	}
	if ( !idGameDirPolicy::IsPortableSegment( modDir ) ) {
		if ( reason != NULL ) {
			*reason = "mod directory must be one portable directory segment";
		}
		return false;
	}

	const char *search[ 3 ];
	search[ 0 ] = fs_cdpath.GetString();
	search[ 1 ] = fs_basepath.GetString();
	search[ 2 ] = fs_savepath.GetString();

	idStr failureReason;
	for ( int i = 0; i < 3; ++i ) {
		idStr localReason;
		const modManifestStatus_t status = ReadModManifestFromSearchPath( search[ i ], modDir, modInfo, &localReason );
		if ( status == MOD_MANIFEST_VALID ) {
			return true;
		}
		if ( status == MOD_MANIFEST_INVALID && localReason.Length() ) {
			failureReason = localReason;
		}
	}

	if ( reason != NULL ) {
		if ( failureReason.Length() ) {
			*reason = failureReason;
		} else {
			*reason = va( "missing %s", OPENQ4_MOD_MANIFEST_FILENAME );
		}
	}

	return false;
}

/*
===============
idFileSystemLocal::ValidateConfiguredGameDir
===============
*/
bool idFileSystemLocal::ValidateConfiguredGameDir( const char *gameDir, idStr *reason ) {
	if ( reason != NULL ) {
		reason->Clear();
	}

	if ( gameDir == NULL || gameDir[ 0 ] == '\0' ) {
		return true;
	}

	if ( !idStr::Icmp( gameDir, BASE_GAMEDIR ) ) {
		return true;
	}

	idModInfo modInfo;
	return GetModInfo( gameDir, modInfo, reason );
}

/*
===============
idFileSystemLocal::FreeModList
===============
*/
void idFileSystemLocal::FreeModList( idModList *modList ) {
	delete modList;
}

/*
===============
idDEntry::Matches
===============
*/
bool idDEntry::Matches(const char *directory, const char *extension) const {
	if ( !idDEntry::directory.Icmp( directory ) && !idDEntry::extension.Icmp( extension ) ) {
		return true;
	}
	return false;
}

/*
===============
idDEntry::Init
===============
*/
void idDEntry::Init( const char *directory, const char *extension, const idStrList &list ) {
	idDEntry::directory = directory;
	idDEntry::extension = extension;
	idStrList::operator=(list);
}

/*
===============
idDEntry::Clear
===============
*/
void idDEntry::Clear( void ) {
	directory.Clear();
	extension.Clear();
	idStrList::Clear();
}

/*
===============
idFileSystemLocal::ListOSFiles

 call to the OS for a listing of files in an OS directory
 optionally, perform some caching of the entries
===============
*/
int	idFileSystemLocal::ListOSFiles( const char *directory, const char *extension, idStrList &list ) {
	int i, j, ret;
	const char *cacheDirectory = directory;
	idStr resolvedDirectory;

	if ( !extension ) {
		extension = "";
	}

	if ( !fs_caseSensitiveOS.GetBool() ) {
		return Sys_ListFiles( directory, extension, list );
	}

	// try in cache
	i = dir_cache_index - 1;
	while( i >= dir_cache_index - dir_cache_count ) {
		j = (i+MAX_CACHED_DIRS) % MAX_CACHED_DIRS;
		if ( dir_cache[j].Matches( directory, extension ) ) {
			if ( fs_debug.GetInteger() ) {
				//common->Printf( "idFileSystemLocal::ListOSFiles: cache hit: %s\n", directory );
			}
			list = dir_cache[j];
			return list.Num();
		}
		i--;
	}

	if ( fs_debug.GetInteger() ) {
		//common->Printf( "idFileSystemLocal::ListOSFiles: cache miss: %s\n", directory );
	}	

	ret = Sys_ListFiles( directory, extension, list );
	if ( ret == -1 && ResolveCaseInsensitiveOSPath( directory, resolvedDirectory, false ) ) {
		cacheDirectory = resolvedDirectory.c_str();
		if ( fs_debug.GetInteger() ) {
			common->Printf( "idFileSystemLocal::ListOSFiles: changed %s to %s\n", directory, cacheDirectory );
		}
		ret = Sys_ListFiles( cacheDirectory, extension, list );
	}

	if ( ret == -1 ) {
		return -1;
	}

	// push a new entry
	dir_cache[dir_cache_index].Init( cacheDirectory, extension, list );
	dir_cache_index = ( dir_cache_index + 1 ) % MAX_CACHED_DIRS;
	if ( dir_cache_count < MAX_CACHED_DIRS ) {
		dir_cache_count++;
	}

	return ret;
}

/*
================
idFileSystemLocal::Dir_f
================
*/
void idFileSystemLocal::Dir_f( const idCmdArgs &args ) {
	idStr		relativePath;
	idStr		extension;
	idFileList *fileList;
	int			i;

	if ( args.Argc() < 2 || args.Argc() > 3 ) {
		common->Printf( "usage: dir <directory> [extension]\n" );
		return;
	}

	if ( args.Argc() == 2 ) {
		relativePath = args.Argv( 1 );
		extension = "";
	}
	else {
		relativePath = args.Argv( 1 );
		extension = args.Argv( 2 );
		if ( extension[0] != '.' ) {
			common->Warning( "extension should have a leading dot" );
		}
	}
	relativePath.BackSlashesToSlashes();
	relativePath.StripTrailing( '/' );

	common->Printf( "Listing of %s/*%s\n", relativePath.c_str(), extension.c_str() );
	common->Printf( "---------------\n" );

	fileList = fileSystemLocal.ListFiles( relativePath, extension );

	for ( i = 0; i < fileList->GetNumFiles(); i++ ) {
		common->Printf( "%s\n", fileList->GetFile( i ) );
	}
	common->Printf( "%d files\n", fileList->list.Num() );

	fileSystemLocal.FreeFileList( fileList );
}

/*
================
idFileSystemLocal::DirTree_f
================
*/
void idFileSystemLocal::DirTree_f( const idCmdArgs &args ) {
	idStr		relativePath;
	idStr		extension;
	idFileList *fileList;
	int			i;

	if ( args.Argc() < 2 || args.Argc() > 3 ) {
		common->Printf( "usage: dirtree <directory> [extension]\n" );
		return;
	}

	if ( args.Argc() == 2 ) {
		relativePath = args.Argv( 1 );
		extension = "";
	}
	else {
		relativePath = args.Argv( 1 );
		extension = args.Argv( 2 );
		if ( extension[0] != '.' ) {
			common->Warning( "extension should have a leading dot" );
		}
	}
	relativePath.BackSlashesToSlashes();
	relativePath.StripTrailing( '/' );

	common->Printf( "Listing of %s/*%s /s\n", relativePath.c_str(), extension.c_str() );
	common->Printf( "---------------\n" );

	fileList = fileSystemLocal.ListFilesTree( relativePath, extension );

	for ( i = 0; i < fileList->GetNumFiles(); i++ ) {
		common->Printf( "%s\n", fileList->GetFile( i ) );
	}
	common->Printf( "%d files\n", fileList->list.Num() );

	fileSystemLocal.FreeFileList( fileList );
}

/*
============
idFileSystemLocal::Path_f
============
*/
void idFileSystemLocal::Path_f( const idCmdArgs &args ) {
	searchpath_t *sp;
	int i;
	idStr status;

	common->Printf( "Current search path:\n" );
	for ( sp = fileSystemLocal.searchPaths; sp; sp = sp->next ) {
		if ( sp->pack ) {
			if ( com_developer.GetBool() ) {
				status = va( "%s (%i files - 0x%x %s", sp->pack->pakFilename.c_str(), sp->pack->numfiles, sp->pack->checksum, sp->pack->referenced ? "referenced" : "not referenced" );
				if ( sp->pack->addon ) {
					status += " - addon)\n";
				} else {
					status += ")\n";
				}
				common->Printf( "%s", status.c_str() );
			} else {
				common->Printf( "%s (%i files)\n", sp->pack->pakFilename.c_str(), sp->pack->numfiles );
			}
			if ( fileSystemLocal.serverPaks.Num() ) {
				if ( fileSystemLocal.serverPaks.Find( sp->pack ) ) {
					common->Printf( "    on the pure list\n" );
				} else {
					common->Printf( "    not on the pure list\n" );
				}
			}
		} else {
			common->Printf( "%s/%s\n", sp->dir->path.c_str(), sp->dir->gamedir.c_str() );
		}
	}
	common->Printf( "game DLL: 0x%x in pak: 0x%x\n", fileSystemLocal.gameDLLChecksum, fileSystemLocal.gamePakChecksum );
#if ID_FAKE_PURE
	common->Printf( "Note: ID_FAKE_PURE is enabled\n" );
#endif
	for( i = 0; i < MAX_GAME_OS; i++ ) {
		if ( fileSystemLocal.gamePakForOS[ i ] ) {
			common->Printf( "OS %d - pak 0x%x\n", i, fileSystemLocal.gamePakForOS[ i ] );
		}
	}
	// show addon packs that are *not* in the search lists
	common->Printf( "Addon pk4s:\n" );
	for ( sp = fileSystemLocal.addonPaks; sp; sp = sp->next ) {
		if ( com_developer.GetBool() ) {
			common->Printf( "%s (%i files - 0x%x)\n", sp->pack->pakFilename.c_str(), sp->pack->numfiles, sp->pack->checksum );
		} else {
			common->Printf( "%s (%i files)\n", sp->pack->pakFilename.c_str(), sp->pack->numfiles );
		}		
	}
}

/*
============
idFileSystemLocal::GetOSMask
============
*/
int idFileSystemLocal::GetOSMask( void ) {
	int i, ret = 0;
	for( i = 0; i < MAX_GAME_OS; i++ ) {
		if ( fileSystemLocal.gamePakForOS[ i ] ) {
			ret |= ( 1 << i );
		}
	}
	if ( !ret ) {
		return -1;
	}
	return ret;
}

/*
============
idFileSystemLocal::TouchFile_f

The only purpose of this function is to allow game script files to copy
arbitrary files furing an "fs_copyfiles 1" run.
============
*/
void idFileSystemLocal::TouchFile_f( const idCmdArgs &args ) {
	idFile *f;

	if ( args.Argc() != 2 ) {
		common->Printf( "Usage: touchFile <file>\n" );
		return;
	}

	f = fileSystemLocal.OpenFileRead( args.Argv( 1 ) );
	if ( f ) {
		fileSystemLocal.CloseFile( f );
	}
}

/*
============
idFileSystemLocal::TouchFileList_f

Takes a text file and touches every file in it, use one file per line.
============
*/
void idFileSystemLocal::TouchFileList_f( const idCmdArgs &args ) {
	
	if ( args.Argc() != 2 ) {
		common->Printf( "Usage: touchFileList <filename>\n" );
		return;
	}

	const char *buffer = NULL;
	idParser src( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT );
	if ( fileSystem->ReadFile( args.Argv( 1 ), ( void** )&buffer, NULL ) && buffer ) {
		src.LoadMemory( buffer, idLib::SizeToInt( strlen( buffer ), "idFileSystemLocal::TouchFileList_f" ), args.Argv( 1 ) );
		if ( src.IsLoaded() ) {
			idToken token;
			while( src.ReadToken( &token ) ) {
				common->Printf( "%s\n", token.c_str() );
				session->UpdateScreen();
				idFile *f = fileSystemLocal.OpenFileRead( token );
				if ( f ) {
					fileSystemLocal.CloseFile( f );
				}
			}
		}
	}

}


/*
================
idFileSystemLocal::AddGameDirectory

	Adds the directory to the head of the search paths, then loads any pk4 files.
================
*/
void idFileSystemLocal::AddGameDirectory( const char *path, const char *dir ) {
	int				i;
	searchpath_t *	search;
	pack_t *		pak;
	idStr			pakfile;
	idStrList		pakfiles;

	// check if the search path already exists
	for ( search = searchPaths; search; search = search->next ) {
		// if this element is a pak file
		if ( !search->dir ) {
			continue;
		}
		if ( search->dir->path.Cmp( path ) == 0 && search->dir->gamedir.Cmp( dir ) == 0 ) {
			return;
		}
	}

	//
	// add the directory to the search path
	//
	search = new searchpath_t;
	search->dir = new directory_t;
	search->pack = NULL;

	search->dir->path = path;
	search->dir->gamedir = dir;
	search->next = searchPaths;
	searchPaths = search;

	// find all pak files in this directory
	pakfile = BuildOSPath( path, dir, "" );
	pakfile.StripTrailing( '/' );
	pakfile.StripTrailing( '\\' );

	ListOSFiles( pakfile, ".pk4", pakfiles );
	if ( fs_debug.GetInteger() ) {
		common->Printf( "Found %d pk4 file(s) in %s\n", pakfiles.Num(), pakfile.c_str() );
	}

	// Sort them so later entries override earlier ones after they are inserted
	// into the search path. openQ4 reserves short pakN.pk4 names for its own
	// content, so those win over wider retail-style pakNNN.pk4 names.
	FS_SortPk4FilesForLoadOrder( pakfiles );

	for ( i = 0; i < pakfiles.Num(); i++ ) {
		if ( !idStr::Icmp( dir, BASE_GAMEDIR ) && FS_IsIgnoredOfficialGameBinaryPk4( pakfiles[ i ] ) ) {
			common->Printf( "Ignoring unneeded game binary pk4 %s\n", BuildOSPath( path, dir, pakfiles[ i ] ) );
			continue;
		}

		pakfile = BuildOSPath( path, dir, pakfiles[i] );
		pak = LoadZipFile( pakfile );
		if ( !pak ) {
			continue;
		}
		// insert the pak after the directory it comes from
		search = new searchpath_t;
		search->dir = NULL;
		search->pack = pak;
		search->next = searchPaths->next;
		searchPaths->next = search;
		common->Printf( "Loaded pk4 %s with checksum 0x%x\n", pakfile.c_str(), pak->checksum );
	}
}

/*
================
idFileSystemLocal::SetupGameDirectories

  Takes care of the correct search order.
================
*/
void idFileSystemLocal::SetupGameDirectories( const char *gameName ) {
	// setup savepath
	if ( fs_savepath.GetString()[0] ) {
		AddGameDirectory( fs_savepath.GetString(), gameName );
	}

	// setup basepath
	if ( fs_basepath.GetString()[0] ) {
		AddGameDirectory( fs_basepath.GetString(), gameName );
	}

	// setup cdpath last so it has highest search priority
	if ( fs_cdpath.GetString()[0] ) {
		AddGameDirectory( fs_cdpath.GetString(), gameName );
	}
}

/*
================
idFileSystemLocal::NormalizeMapPath
================
*/
static bool FS_IsMapFilterWhitespace( const char ch ) {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void FS_RemoveEmbeddedMapEntityFilter( idStr &mapName ) {
	mapName.BackSlashesToSlashes();
	mapName.Strip( ' ' );
	mapName.Strip( '\t' );
	mapName.StripTrailingWhitespace();
	mapName.StripQuotes();

	int split = -1;
	for ( int i = mapName.Length() - 1; i >= 0; --i ) {
		if ( FS_IsMapFilterWhitespace( mapName[ i ] ) ) {
			split = i;
			break;
		}
	}
	if ( split <= 0 ) {
		return;
	}

	idStr mapPart = mapName.Left( split );
	idStr filterPart = mapName.Right( mapName.Length() - split - 1 );
	mapPart.Strip( ' ' );
	mapPart.Strip( '\t' );
	mapPart.StripTrailingWhitespace();
	mapPart.StripQuotes();
	filterPart.Strip( ' ' );
	filterPart.Strip( '\t' );
	filterPart.StripTrailingWhitespace();
	filterPart.StripQuotes();

	if ( mapPart.Length() > 0 && filterPart.Length() > 0 ) {
		mapName = mapPart;
	}
}

bool idFileSystemLocal::NormalizeMapPath( const char *mapName, idStr &relativePath ) const {
	relativePath.Clear();

	if ( !mapName || !mapName[0] ) {
		return false;
	}

	relativePath = mapName;
	FS_RemoveEmbeddedMapEntityFilter( relativePath );
	relativePath.StripFileExtension();
	if ( relativePath.Length() <= 0 ) {
		return false;
	}

	if ( idStr::Icmpn( relativePath.c_str(), "maps/", 5 ) != 0 ) {
		relativePath = va( "maps/%s", relativePath.c_str() );
	}

	relativePath += ".map";
	relativePath.ToLower();
	return true;
}

/*
================
idFileSystemLocal::AddonPackProvidesMap
================
*/
bool idFileSystemLocal::AddonPackProvidesMap( const pack_t *pak, const char *relativeMapPath ) const {
	if ( !pak || !pak->addon || !relativeMapPath || !relativeMapPath[0] ) {
		return false;
	}

	const int hash = HashFileName( relativeMapPath );
	for ( const fileInPack_t *pakFile = pak->hashTable[ hash ]; pakFile; pakFile = pakFile->next ) {
		if ( !FilenameCompare( pakFile->name, relativeMapPath ) ) {
			return true;
		}
	}

	if ( !pak->addon_info ) {
		return false;
	}

	for ( int i = 0; i < pak->addon_info->mapDecls.Num(); ++i ) {
		const idDict *mapDecl = pak->addon_info->mapDecls[ i ];
		if ( mapDecl == NULL ) {
			continue;
		}

		const char *mapPath = mapDecl->GetString( "path" );
		if ( mapPath[ 0 ] && !FilenameCompare( mapPath, relativeMapPath ) ) {
			return true;
		}
	}

	return false;
}

/*
================
idFileSystemLocal::FreePack
================
*/
void idFileSystemLocal::FreePack( pack_t *pack ) const {
	if ( pack == NULL ) {
		return;
	}

	if ( pack->handle ) {
		unzClose( pack->handle );
	}

	delete[] pack->buildBuffer;
	if ( pack->addon_info ) {
		pack->addon_info->mapDecls.DeleteContents( true );
		delete pack->addon_info;
	}
	delete pack;
}

/*
================
idFileSystemLocal::StageStartupAddonPaks
================
*/
void idFileSystemLocal::StageStartupAddonPaks( void ) {
	if ( fs_searchAddons.GetBool() ) {
		return;
	}

	idStrList startupMaps;
	idStr pendingServerMap;
	const int numStartupCommands = Com_GetNumStartupCommandLines();

	for ( int i = 0; i < numStartupCommands; ++i ) {
		const idCmdArgs *args = Com_GetStartupCommandLine( i );
		if ( args == NULL || args->Argc() <= 0 ) {
			continue;
		}

		const char *command = args->Argv( 0 );
		if ( !idStr::Icmp( command, "set" ) ) {
			if ( args->Argc() >= 3 && !idStr::Icmp( args->Argv( 1 ), "si_map" ) ) {
				NormalizeMapPath( args->Argv( 2 ), pendingServerMap );
			}
			continue;
		}

		idStr relativeMapPath;
		if ( !idStr::Icmp( command, "spawnServer" ) ) {
			if ( args->Argc() > 1 ) {
				NormalizeMapPath( args->Argv( 1 ), relativeMapPath );
			} else if ( pendingServerMap.Length() > 0 ) {
				relativeMapPath = pendingServerMap;
			}
		} else if ( !idStr::Icmp( command, "map" ) ||
					!idStr::Icmp( command, "devmap" ) ||
					!idStr::Icmp( command, "testmap" ) ||
					!idStr::Icmp( command, "openq4_startSingleplayer" ) ) {
			if ( args->Argc() > 1 ) {
				NormalizeMapPath( args->Argv( 1 ), relativeMapPath );
			}
		}

		if ( relativeMapPath.Length() <= 0 ) {
			continue;
		}

		bool duplicate = false;
		for ( int j = 0; j < startupMaps.Num(); ++j ) {
			if ( !FilenameCompare( startupMaps[ j ], relativeMapPath ) ) {
				duplicate = true;
				break;
			}
		}
		if ( !duplicate ) {
			startupMaps.Append( relativeMapPath );
		}
	}

	if ( startupMaps.Num() <= 0 ) {
		return;
	}

	for ( searchpath_t *search = searchPaths; search; search = search->next ) {
		if ( !search->pack || !search->pack->addon ) {
			continue;
		}

		for ( int mapIndex = 0; mapIndex < startupMaps.Num(); ++mapIndex ) {
			if ( !AddonPackProvidesMap( search->pack, startupMaps[ mapIndex ] ) ) {
				continue;
			}

			if ( addonChecksums.FindIndex( search->pack->checksum ) < 0 ) {
				addonChecksums.Append( search->pack->checksum );
				common->Printf( "Queued addon pk4 %s with checksum 0x%x for startup map %s\n",
					search->pack->pakFilename.c_str(), search->pack->checksum, startupMaps[ mapIndex ].c_str() );
			}
			break;
		}
	}
}

/*
================
idFileSystemLocal::IsGameDirPack
================
*/
bool idFileSystemLocal::IsGameDirPack( const pack_t *pak, const char *gameDir ) const {
	idStr path;
	idStr gameDirSegment;

	if ( !pak || !gameDir || !gameDir[ 0 ] ) {
		return false;
	}

	path = pak->pakFilename;
	path.BackSlashesToSlashes();
	path.ToLower();

	gameDirSegment = "/";
	gameDirSegment += gameDir;
	gameDirSegment += "/";
	gameDirSegment.ToLower();

	if ( path.Find( gameDirSegment.c_str() ) >= 0 ) {
		return true;
	}

	gameDirSegment.StripLeading( '/' );
	return !idStr::Icmpn( path.c_str(), gameDirSegment.c_str(), gameDirSegment.Length() );
}

/*
================
idFileSystemLocal::IsBaseGamePack
================
*/
bool idFileSystemLocal::IsBaseGamePack( const pack_t *pak ) const {
	return IsGameDirPack( pak, BASE_GAMEDIR );
}

/*
================
idFileSystemLocal::IsOpenQ4PurePack
================
*/
bool idFileSystemLocal::IsOpenQ4PurePack( const pack_t *pak ) const {
	idStr name;

	if ( !IsGameDirPack( pak, OPENQ4_GAMEDIR ) ) {
		return false;
	}

	pak->pakFilename.ExtractFileName( name );
	return !name.Icmp( "pak0.pk4" ) || !name.Icmp( "pak1.pk4" );
}

/*
================
idFileSystemLocal::FindGamePackByName
================
*/
pack_t *idFileSystemLocal::FindGamePackByName( const char *name, const char *gameDir ) const {
	searchpath_t *search;
	idStr			pakName;

	for ( search = searchPaths; search; search = search->next ) {
		if ( !search->pack || !IsGameDirPack( search->pack, gameDir ) ) {
			continue;
		}
		search->pack->pakFilename.ExtractFileName( pakName );
		if ( !pakName.Icmp( name ) ) {
			return search->pack;
		}
	}
	return NULL;
}

/*
================
idFileSystemLocal::FindBaseGamePackByName
================
*/
pack_t *idFileSystemLocal::FindBaseGamePackByName( const char *name ) const {
	return FindGamePackByName( name, BASE_GAMEDIR );
}

/*
================
idFileSystemLocal::FindMisplacedOfficialPaks
================
*/
bool idFileSystemLocal::FindMisplacedOfficialPaks( idStr &errors ) const {
	const officialPk4Info_t	*info;
	pack_t					*basePack;
	pack_t					*openQ4Pack;

	errors.Clear();
	for ( int i = 0; officialPk4s[ i ].name != NULL; i++ ) {
		info = &officialPk4s[ i ];
		basePack = FindGamePackByName( info->name, BASE_GAMEDIR );
		openQ4Pack = FindGamePackByName( info->name, OPENQ4_GAMEDIR );
		if ( !openQ4Pack ) {
			continue;
		}

		if ( (unsigned int)openQ4Pack->checksum != info->checksum ) {
			errors += va( "%s was found in %s with checksum 0x%08x but belongs in %s (expected 0x%08x from %s)\n",
				info->name, OPENQ4_GAMEDIR, (unsigned int)openQ4Pack->checksum, BASE_GAMEDIR,
				info->checksum, openQ4Pack->pakFilename.c_str() );
			continue;
		}

		if ( basePack ) {
			errors += va( "%s was found in both %s and %s; remove the copy from %s (%s)\n",
				info->name, BASE_GAMEDIR, OPENQ4_GAMEDIR, OPENQ4_GAMEDIR, openQ4Pack->pakFilename.c_str() );
		} else {
			errors += va( "%s was found in %s but belongs in %s (%s)\n",
				info->name, OPENQ4_GAMEDIR, BASE_GAMEDIR, openQ4Pack->pakFilename.c_str() );
		}
	}

	return ( errors.Length() != 0 );
}

/*
================
idFileSystemLocal::ValidateOpenQ4Paks
================
*/
bool idFileSystemLocal::ValidateOpenQ4Paks( idStr &errors ) const {
	struct expectedOpenQ4Pak_t {
		const char *name;
		const char *md5;
	};
	static const expectedOpenQ4Pak_t expectedPaks[] = {
		{ "pak0.pk4", OPENQ4_PAK0_MD5 },
		{ "pak1.pk4", OPENQ4_PAK1_MD5 },
		{ NULL, NULL }
	};

	errors.Clear();

	if ( idStr::Icmp( fs_game.GetString(), OPENQ4_GAMEDIR ) &&
		 idStr::Icmp( fs_game_base.GetString(), OPENQ4_GAMEDIR ) ) {
		return true;
	}

	for ( int i = 0; expectedPaks[ i ].name != NULL; i++ ) {
		const expectedOpenQ4Pak_t &expected = expectedPaks[ i ];
		pack_t *pack = FindGamePackByName( expected.name, OPENQ4_GAMEDIR );
		char actualMD5[33];

		if ( !pack ) {
			errors += va( "missing %s/%s (expected checksum %s)\n", OPENQ4_GAMEDIR, expected.name, expected.md5 );
			continue;
		}

		if ( !MD5_FileChecksum( pack->pakFilename.c_str(), actualMD5 ) ) {
			errors += va( "could not read %s for checksum validation\n", pack->pakFilename.c_str() );
			continue;
		}

		if ( idStr::Icmp( actualMD5, expected.md5 ) ) {
			errors += va( "checksum mismatch for %s/%s (expected %s, got %s from %s)\n",
				OPENQ4_GAMEDIR, expected.name, expected.md5, actualMD5, pack->pakFilename.c_str() );
		}
	}

	return ( errors.Length() == 0 );
}

/*
================
idFileSystemLocal::ValidateRequiredOfficialPaks
================
*/
bool idFileSystemLocal::ValidateRequiredOfficialPaks( idStr &errors ) const {
	const officialPk4Info_t	*info;
	pack_t					*pack;

	errors.Clear();
	for ( int i = 0; officialPk4s[ i ].name != NULL; i++ ) {
		info = &officialPk4s[ i ];
		if ( !info->required ) {
			continue;
		}
		pack = FindBaseGamePackByName( info->name );
		if ( !pack ) {
			errors += va( "missing %s (expected 0x%08x)\n", info->name, info->checksum );
			continue;
		}
		if ( (unsigned int)pack->checksum != info->checksum ) {
			errors += va( "checksum mismatch for %s (expected 0x%08x, got 0x%08x from %s)\n",
				info->name, info->checksum, (unsigned int)pack->checksum, pack->pakFilename.c_str() );
		}
	}
	return ( errors.Length() == 0 );
}

/*
===============
idFileSystemLocal::PrintContentSearchDiagnostics

The startup content checks abort the engine, so the log must say where the
engine actually looked and what it found there. Without this the user only
learns that something is "missing or modified"; the dominant Linux cause is a
case-mismatched 'q4base'/'pakNNN.pk4' tree copied from a Windows install, which
no amount of checksum text can reveal.
===============
*/
void idFileSystemLocal::PrintContentSearchDiagnostics( void ) {
	const char *roots[ 3 ];
	const char *rootNames[ 3 ];
	const char *dirs[ 2 ];
	idStrList	found;
	idStr		osPath;
	int			i, j, k;

	roots[ 0 ] = fs_basepath.GetString();	rootNames[ 0 ] = "fs_basepath";
	roots[ 1 ] = fs_savepath.GetString();	rootNames[ 1 ] = "fs_savepath";
	roots[ 2 ] = fs_cdpath.GetString();		rootNames[ 2 ] = "fs_cdpath";

	dirs[ 0 ] = BASE_GAMEDIR;
	dirs[ 1 ] = OPENQ4_GAMEDIR;

	common->Printf( "----- content search diagnostics -----\n" );
	common->Printf( "fs_game      = '%s'\n", fs_game.GetString() );
	common->Printf( "fs_game_base = '%s'\n", fs_game_base.GetString() );

	for ( i = 0; i < 3; i++ ) {
		if ( roots[ i ] == NULL || roots[ i ][ 0 ] == '\0' ) {
			common->Printf( "%s = <unset>\n", rootNames[ i ] );
			continue;
		}
		common->Printf( "%s = '%s'\n", rootNames[ i ], roots[ i ] );

		for ( j = 0; j < 2; j++ ) {
			// skip a duplicate root rather than listing the same tree twice
			for ( k = 0; k < i; k++ ) {
				if ( roots[ k ] != NULL && roots[ k ][ 0 ] != '\0' && idStr::Icmp( roots[ k ], roots[ i ] ) == 0 ) {
					break;
				}
			}
			if ( k < i ) {
				continue;
			}

			osPath = BuildOSPath( roots[ i ], dirs[ j ], "" );
			osPath.StripTrailing( '/' );
			osPath.StripTrailing( '\\' );

			found.Clear();
			if ( ListOSFiles( osPath, ".pk4", found ) == -1 ) {
				common->Printf( "   %s: directory not found (note: paths are case sensitive on Linux)\n", osPath.c_str() );
				continue;
			}

			common->Printf( "   %s: %d pk4 file(s)\n", osPath.c_str(), found.Num() );
			for ( k = 0; k < found.Num(); k++ ) {
				common->Printf( "      %s\n", found[ k ].c_str() );
			}
		}
	}
	common->Printf( "-------------------------------------\n" );
}

/*
===============
idFileSystemLocal::FollowDependencies
===============
*/
void idFileSystemLocal::FollowAddonDependencies( pack_t *pak ) {
	assert( pak );
	if ( !pak->addon_info || !pak->addon_info->depends.Num() ) {
		return;
	}
	int i, num = pak->addon_info->depends.Num();
	for ( i = 0; i < num; i++ ) {
		pack_t *deppak = GetPackForChecksum( pak->addon_info->depends[ i ], true );
		if ( deppak ) {
			// make sure it hasn't been marked for search already
			if ( !deppak->addon_search ) {
				// must clean addonChecksums as we go
				int addon_index = addonChecksums.FindIndex( deppak->checksum );
				if ( addon_index >= 0 ) {
					addonChecksums.RemoveIndex( addon_index );
				}
				deppak->addon_search = true;
				common->Printf( "Addon pk4 %s 0x%x depends on pak %s 0x%x, will be searched\n",
								pak->pakFilename.c_str(), pak->checksum,
								deppak->pakFilename.c_str(), deppak->checksum );
				FollowAddonDependencies( deppak );
			}
		} else {
			common->Printf( "Addon pk4 %s 0x%x depends on unknown pak 0x%x\n",
							pak->pakFilename.c_str(), pak->checksum, pak->addon_info->depends[ i ] );
		}
	}
}

/*
================
idFileSystemLocal::Startup
================
*/
void idFileSystemLocal::Startup( void ) {
	searchpath_t	**search;
	int				i;
	pack_t			*pak;
	int				addon_index;

	common->Printf( "------ Initializing File System ------\n" );

	if ( restartChecksums.Num() ) {
		common->Printf( "restarting in pure mode with %d pak files\n", restartChecksums.Num() );
	}
	if ( addonChecksums.Num() ) {
		common->Printf( "restarting filesystem with %d addon pak file(s) to include\n", addonChecksums.Num() );
	}

	idStr invalidReason;
	if ( fs_game_base.GetString()[ 0 ] &&
		 idStr::Icmp( fs_game_base.GetString(), BASE_GAMEDIR ) &&
		 !ValidateConfiguredGameDir( fs_game_base.GetString(), &invalidReason ) ) {
		common->Warning( "Ignoring fs_game_base '%s': %s", fs_game_base.GetString(), invalidReason.c_str() );
		fs_game_base.SetString( "" );
	}

	if ( fs_game.GetString()[ 0 ] &&
		 idStr::Icmp( fs_game.GetString(), BASE_GAMEDIR ) &&
		 !ValidateConfiguredGameDir( fs_game.GetString(), &invalidReason ) ) {
		if ( !idStr::Icmp( fs_game.GetString(), OPENQ4_GAMEDIR ) ) {
			PrintContentSearchDiagnostics();
			common->FatalError(
				"openQ4 runtime directory '%s' is missing a compatible mod.json.\n\n%s\n"
				"Rebuild or reinstall openQ4 so '<openQ4 package root>/%s/mod.json' is present and matches this engine version. Do not replace '%s' with retail Quake 4 assets.",
				fs_game.GetString(), invalidReason.c_str(), OPENQ4_GAMEDIR, OPENQ4_GAMEDIR );
		}

		common->Warning( "Ignoring fs_game '%s': %s", fs_game.GetString(), invalidReason.c_str() );
		fs_game.SetString( "" );
	}

	// File writes should use the selected game directory even while search paths
	// are still being populated. Pak-load diagnostics can open logFile as soon
	// as the first q4base search path exists.
	if ( fs_game.GetString()[ 0 ] && idStr::Icmp( fs_game.GetString(), BASE_GAMEDIR ) ) {
		gameFolder = fs_game.GetString();
	} else if ( fs_game_base.GetString()[ 0 ] && idStr::Icmp( fs_game_base.GetString(), BASE_GAMEDIR ) ) {
		gameFolder = fs_game_base.GetString();
	} else {
		gameFolder = BASE_GAMEDIR;
	}

	SetupGameDirectories( BASE_GAMEDIR );

	// fs_game_base override
	if ( fs_game_base.GetString()[0] &&
		 idStr::Icmp( fs_game_base.GetString(), BASE_GAMEDIR ) ) {
		SetupGameDirectories( fs_game_base.GetString() );
	}

	// fs_game override
	if ( fs_game.GetString()[0] &&
		 idStr::Icmp( fs_game.GetString(), BASE_GAMEDIR ) &&
		 idStr::Icmp( fs_game.GetString(), fs_game_base.GetString() ) ) {
		SetupGameDirectories( fs_game.GetString() );
	}

	idStr openQ4PakErrors;
	if ( !ValidateOpenQ4Paks( openQ4PakErrors ) ) {
		PrintContentSearchDiagnostics();
		common->FatalError(
			"openQ4 runtime content packs in '%s' are missing or modified.\n\n%s\n"
			"Rebuild or reinstall openQ4 so '<openQ4 package root>/%s/pak0.pk4' and '<openQ4 package root>/%s/pak1.pk4' match this engine. Retail Quake 4 PK4s belong in '%s', not '%s'.",
			OPENQ4_GAMEDIR, openQ4PakErrors.c_str(), OPENQ4_GAMEDIR, OPENQ4_GAMEDIR, BASE_GAMEDIR, OPENQ4_GAMEDIR );
	}

	if ( fs_validateOfficialPaks.GetBool() ) {
		idStr misplacedErrors;
		if ( FindMisplacedOfficialPaks( misplacedErrors ) ) {
			PrintContentSearchDiagnostics();
			common->FatalError(
				"Retail Quake 4 media pk4 files must be installed in '%s', not '%s'.\n\n%s\n"
				"Move the listed files into '<Quake 4 install root>/%s', or remove them from '%s' and launch with +set fs_basepath pointing at a Quake 4 install root that contains '%s'. "
				"The '%s' directory is reserved for openQ4 runtime files such as pak0.pk4, pak1.pk4, mod.json, and game modules.",
				BASE_GAMEDIR, OPENQ4_GAMEDIR, misplacedErrors.c_str(), BASE_GAMEDIR, OPENQ4_GAMEDIR, BASE_GAMEDIR, OPENQ4_GAMEDIR );
		}

		idStr validationErrors;
		if ( !ValidateRequiredOfficialPaks( validationErrors ) ) {
			PrintContentSearchDiagnostics();
			common->FatalError(
				"Required official Quake 4 media pk4 files are missing from '%s' or modified.\n\n%s\n"
				"openQ4 reads the retail Quake 4 assets from '<Quake 4 install root>/%s'. Put pak001.pk4 through pak022.pk4 in that folder, or launch with +set fs_basepath pointing at the install root that contains it. "
				"Do not put retail pk4 files in '%s'; that directory is reserved for openQ4 runtime files.",
				BASE_GAMEDIR, validationErrors.c_str(), BASE_GAMEDIR, OPENQ4_GAMEDIR );
		}

#ifndef ID_DEDICATED
		// The core pak001-pak022 media set is shared by every retail language and
		// deliberately remains the only fatal validation baseline. Character VO is
		// different: it lives in the edition-specific zpak_<language>.pk4 archives,
		// so a partial copy can pass that baseline while every conversation is
		// silent. Keep alternate physical/localized editions valid, but make the
		// missing media actionable on clients instead of failing one sample at a time.
		if ( !HasBaseLanguageMediaPack() ) {
			common->Warning(
				"No recognized base Quake 4 language media archive (zpak_<language>.pk4, without a numeric suffix) was found in '%s'; character dialogue will be incomplete or silent. "
				"Numbered patch archives such as zpak_english_01.pk4 do not replace the base archive. Install or verify the unsuffixed retail language archive in '<Quake 4 install root>/%s', or launch with +set fs_basepath pointing at the install root that contains it.",
				BASE_GAMEDIR, BASE_GAMEDIR );
		}
#endif
	}

	// Startup map commands run after initialization, but addon filtering happens here.
	// Pre-stage any addon pk4s that contain those maps before the search list is finalized.
	StageStartupAddonPaks();

	// currently all addons are in the search list - deal with filtering out and dependencies now
	// scan through and deal with dependencies
	search = &searchPaths;
	while ( *search ) {
		if ( !( *search )->pack || !( *search )->pack->addon ) {
			search = &( ( *search )->next );
			continue;
		}
		pak = ( *search )->pack;
		if ( fs_searchAddons.GetBool() ) {
			// when we have fs_searchAddons on we should never have addonChecksums
			assert( !addonChecksums.Num() );
			pak->addon_search = true;
			search = &( ( *search )->next );
			continue;
		}
		addon_index = addonChecksums.FindIndex( pak->checksum );
		if ( addon_index >= 0 ) {
			assert( !pak->addon_search );	// any pak getting flagged as addon_search should also have been removed from addonChecksums already
			pak->addon_search = true;
			addonChecksums.RemoveIndex( addon_index );
			FollowAddonDependencies( pak );
		}
		search = &( ( *search )->next );
	}

	// now scan to filter out addons not marked addon_search
	search = &searchPaths;
	while ( *search ) {
		if ( !( *search )->pack || !( *search )->pack->addon ) {
			search = &( ( *search )->next );
			continue;
		}
		assert( !( *search )->dir );
		pak = ( *search )->pack;
		if ( pak->addon_search ) {
			common->Printf( "Addon pk4 %s with checksum 0x%x is on the search list\n",
							pak->pakFilename.c_str(), pak->checksum );
			search = &( ( *search )->next );
		} else {
			// remove from search list, put in addons list
			searchpath_t *paksearch = *search;
			*search = ( *search )->next;
			paksearch->next = addonPaks;
			addonPaks = paksearch;
			common->Printf( "Addon pk4 %s with checksum 0x%x is on addon list\n",
							pak->pakFilename.c_str(), pak->checksum );				
		}
	}

	// all addon paks found and accounted for
	assert( !addonChecksums.Num() );
	addonChecksums.Clear();	// just in case

	if ( restartChecksums.Num() ) {
		search = &searchPaths;
		while ( *search ) {
			if ( !( *search )->pack ) {
				search = &( ( *search )->next );
				continue;
			}
			if ( ( i = restartChecksums.FindIndex( ( *search )->pack->checksum ) ) != -1 ) {
				if ( i == 0 ) {
					// this pak is the next one in the pure search order
					serverPaks.Append( ( *search )->pack );
					restartChecksums.RemoveIndex( 0 );
					if ( !restartChecksums.Num() ) {
						break; // early out, we're done
					}
					search = &( ( *search )->next );
					continue;
				} else {
					// this pak will be on the pure list, but order is not right yet
					searchpath_t	*aux;
					aux = ( *search )->next;
					if ( !aux ) {
						// last of the list can't be swapped back
						if ( fs_debug.GetBool() ) {
							common->Printf( "found pure checksum %x at index %d, but the end of search path is reached\n", ( *search )->pack->checksum, i );
							idStr checks;
							checks.Clear();
							for ( i = 0; i < serverPaks.Num(); i++ ) {
								checks += va( "%p ", serverPaks[ i ] );
							}
							common->Printf( "%d pure paks - %s \n", serverPaks.Num(), checks.c_str() );
							checks.Clear();
							for ( i = 0; i < restartChecksums.Num(); i++ ) {
								checks += va( "%x ", restartChecksums[ i ] );
							}
							common->Printf( "%d paks left - %s\n", restartChecksums.Num(), checks.c_str() );
						}
						common->FatalError( "Failed to restart with pure mode restrictions for server connect" );
					}
					// put this search path at the end of the list
					searchpath_t *search_end;
					search_end = ( *search )->next;
					while ( search_end->next ) {
						search_end = search_end->next;
					}
					search_end->next = *search;
					*search = ( *search )->next;
					search_end->next->next = NULL;
					continue;
				}
			}
			// this pak is not on the pure list
			search = &( ( *search )->next );
		}
		// the list must be empty
		if ( restartChecksums.Num() ) {
			if ( fs_debug.GetBool() ) {
				idStr checks;
				checks.Clear();
				for ( i = 0; i < serverPaks.Num(); i++ ) {
					checks += va( "%p ", serverPaks[ i ] );
				}
				common->Printf( "%d pure paks - %s \n", serverPaks.Num(), checks.c_str() );
				checks.Clear();
				for ( i = 0; i < restartChecksums.Num(); i++ ) {
					checks += va( "%x ", restartChecksums[ i ] );
				}
				common->Printf( "%d paks left - %s\n", restartChecksums.Num(), checks.c_str() );
			}
			common->FatalError( "Failed to restart with pure mode restrictions for server connect" );
		}
		// also the game pak checksum
		// we could check if the game pak is actually present, but we would not be restarting if there wasn't one @ first pure check
		gamePakChecksum = restartGamePakChecksum;
	}

	// add our commands
	cmdSystem->AddCommand( "dir", Dir_f, CMD_FL_SYSTEM, "lists a folder", idCmdSystem::ArgCompletion_FileName );
	cmdSystem->AddCommand( "dirtree", DirTree_f, CMD_FL_SYSTEM, "lists a folder with subfolders" );
	cmdSystem->AddCommand( "path", Path_f, CMD_FL_SYSTEM, "lists search paths" );
	cmdSystem->AddCommand( "touchFile", TouchFile_f, CMD_FL_SYSTEM, "touches a file" );
	cmdSystem->AddCommand( "touchFileList", TouchFileList_f, CMD_FL_SYSTEM, "touches a list of files" );

	// print the current search paths
	Path_f( idCmdArgs() );

	common->Printf( "file system initialized.\n" );
	common->Printf( "--------------------------------------\n" );
}

/*
===================
idFileSystemLocal::SetRestrictions

Looks for product keys and restricts media add on ability
if the full version is not found
===================
*/
void idFileSystemLocal::SetRestrictions( void ) {
#ifdef ID_DEMO_BUILD
	common->Printf( "\nRunning in restricted demo mode.\n\n" );
	// make sure that the pak file has the header checksum we expect
	searchpath_t	*search;
	for ( search = searchPaths; search; search = search->next ) {
		if ( search->pack ) {
			// a tiny attempt to keep the checksum from being scannable from the exe
			if ( ( search->pack->checksum ^ 0x84268436u ) != ( DEMO_PAK_CHECKSUM ^ 0x84268436u ) ) {
				common->FatalError( "Corrupted %s: 0x%x", search->pack->pakFilename.c_str(), search->pack->checksum );
			}
		}
	}
	cvarSystem->SetCVarBool( "fs_restrict", true );
#endif
}

/*
=====================
idFileSystemLocal::UpdatePureServerChecksums
=====================
*/
void idFileSystemLocal::UpdatePureServerChecksums( void ) {
	searchpath_t	*search;
	int				i;
	pureStatus_t	status;

	serverPaks.Clear();
	for ( search = searchPaths; search; search = search->next ) {
		// is the element a referenced pak file?
		if ( !search->pack ) {
			continue;
		}
		status = GetPackStatus( search->pack );
		if ( status == PURE_NEVER ) {
			continue;
		}
		if ( status == PURE_NEUTRAL && !search->pack->referenced ) {
			continue;
		}
		serverPaks.Append( search->pack );
		if ( serverPaks.Num() >= MAX_PURE_PAKS ) {
			common->FatalError( "MAX_PURE_PAKS ( %d ) exceeded\n", MAX_PURE_PAKS );
		}
	}
	if ( fs_debug.GetBool() ) {
		idStr checks;
		for ( i = 0; i < serverPaks.Num(); i++ ) {
			checks += va( "%x ", serverPaks[ i ]->checksum );
		}
		common->Printf( "set pure list - %d paks ( %s)\n", serverPaks.Num(), checks.c_str() );
	}
}

/*
=====================
idFileSystemLocal::UpdateGamePakChecksums
=====================
*/
bool idFileSystemLocal::UpdateGamePakChecksums( void ) {
	memset( gamePakForOS, 0, sizeof( gamePakForOS ) );
	if ( gameDLLChecksum == 0 || gamePakChecksum != OPENQ4_Q4_142_GAME300_PAK_CHECKSUM ||
		 gameModuleOrigin == GAME_MODULE_ORIGIN_NONE ) {
		common->Warning( "No trusted openQ4 game module is loaded for pure-server startup" );
		return false;
	}
	if ( gameModuleOrigin == GAME_MODULE_ORIGIN_ACTIVE_MOD &&
		 !cvarSystem->GetCVarBool( "net_serverAllowServerMod" ) ) {
		common->Warning( "The active mod supplies game code (net_serverAllowServerMod is off)" );
		return false;
	}

	// Protocol 2.41 defines Windows, Linux and macOS as OS IDs 0..2. The
	// compatibility token is deliberately platform-independent; executable
	// bytes remain outside the downloadable/pure-PK4 path.
	for ( int os = 0; os <= 2; ++os ) {
		gamePakForOS[ os ] = OPENQ4_Q4_142_GAME300_PAK_CHECKSUM;
	}

	return true;
}

/*
=====================
idFileSystemLocal::GetPackForChecksum
=====================
*/
pack_t* idFileSystemLocal::GetPackForChecksum( int checksum, bool searchAddons ) {
	searchpath_t	*search;
	for ( search = searchPaths; search; search = search->next ) {
		if ( !search->pack ) {
			continue;
		}
		if ( search->pack->checksum == checksum ) {
			return search->pack;
		}
	}
	if ( searchAddons ) {
		for ( search = addonPaks; search; search = search->next ) {
			assert( search->pack && search->pack->addon );
			if ( search->pack->checksum == checksum ) {
				return search->pack;
			}
		}
	}
	return NULL;
}

/*
===============
idFileSystemLocal::ValidateDownloadPakForChecksum
===============
*/
int idFileSystemLocal::ValidateDownloadPakForChecksum( int checksum, char path[ MAX_STRING_CHARS ], bool isBinary ) {
	int			i;
	idStrList	testList;
	idStr		name;
	idStr		relativePath;
	bool		pakBinary;
	pack_t		*pak = GetPackForChecksum( checksum );

	if ( !pak ) {
		return 0;
	}

	// validate this pak for a potential download
	// ignore pak*.pk4 for download. those are reserved to distribution and cannot be downloaded
	name = pak->pakFilename;
	name.StripPath();
	if ( strstr( name.c_str(), "pak" ) == name.c_str() ) {
		common->DPrintf( "%s is not a donwloadable pak\n", pak->pakFilename.c_str() );
		return 0;
	}
	// check the binary
	// a pure server sets the binary flag when starting the game
	if ( pak->binary == BINARY_UNKNOWN ) {
		const int confHash = HashFileName( BINARY_CONFIG );
		pak->binary = BINARY_NO;
		for ( fileInPack_t *pakFile = pak->hashTable[ confHash ]; pakFile; pakFile = pakFile->next ) {
			if ( !FilenameCompare( pakFile->name, BINARY_CONFIG ) ) {
				pak->binary = BINARY_YES;
				break;
			}
		}
	}
	assert( pak->binary != BINARY_UNKNOWN );
	pakBinary = ( pak->binary == BINARY_YES ) ? true : false;
	if ( isBinary != pakBinary ) {
		common->DPrintf( "%s binary flag mismatch\n", pak->pakFilename.c_str() );
		return 0;
	}

	// extract a path that includes the fs_game: != OSPathToRelativePath
	testList.Append( fs_cdpath.GetString() );
	testList.Append( fs_basepath.GetString() );
	testList.Append( fs_savepath.GetString() );
	for ( i = 0; i < testList.Num(); i ++ ) {
		if ( testList[ i ].Length() && !testList[ i ].Icmpn( pak->pakFilename, testList[ i ].Length() ) ) {
			relativePath = pak->pakFilename.c_str() + testList[ i ].Length() + 1;
			break;
		}
	}
	if ( i == testList.Num() ) {
		common->Warning( "idFileSystem::ValidateDownloadPak: failed to extract relative path for %s", pak->pakFilename.c_str() );
		return 0;
	}
	idStr::Copynz( path, relativePath, MAX_STRING_CHARS );
	return pak->length;
}

/*
=====================
idFileSystemLocal::ClearPureChecksums
=====================
*/
void idFileSystemLocal::ClearPureChecksums( void ) {
	common->DPrintf( "Cleared pure server lock\n" );
	serverPaks.Clear();
	memset( gamePakForOS, 0, sizeof( gamePakForOS ) );
}

/*
=====================
idFileSystemLocal::SetPureServerChecksums
set the pure paks according to what the server asks
if that's not possible, identify why and build an answer
can be:
  loadedFileFromDir - some files were loaded from directories instead of paks (a restart in pure pak-only is required)
  missing/wrong checksums - some pak files would need to be installed/updated (downloaded for instance)
  some pak files currently referenced are not referenced by the server
  wrong order - if the pak order doesn't match, means some stuff could have been loaded from somewhere else
server referenced files are prepended to the list if possible ( that doesn't break pureness )
Game code:
  protocol 2.41 carries the official q4base/game300.pk4 checksum as a
  compatibility token, but openQ4 resolves executable modules only from its
  trusted package/module roots and never from a server-selected PK4
=====================
*/
fsPureReply_t idFileSystemLocal::SetPureServerChecksums( const int pureChecksums[ MAX_PURE_PAKS ], int _gamePakChecksum, int missingChecksums[ MAX_PURE_PAKS ], int *missingGamePakChecksum ) {
	pack_t			*pack;
	int				i, j, imissing;
	bool			success = true;
	bool			canPrepend = true;

	imissing = 0;
	missingChecksums[ 0 ] = 0;
	assert( missingGamePakChecksum );
	*missingGamePakChecksum = 0;

	// Validate the legacy game-code field before touching the server-selected
	// asset list. It is only a wire-compatibility token in openQ4; it must never
	// select, download, extract, or restart into executable code.
	if ( _gamePakChecksum != OPENQ4_Q4_142_GAME300_PAK_CHECKSUM ||
		 gamePakChecksum != OPENQ4_Q4_142_GAME300_PAK_CHECKSUM ||
		 gameDLLChecksum == 0 || gameModuleOrigin == GAME_MODULE_ORIGIN_NONE ) {
		if ( fs_debug.GetBool() ) {
			common->Printf( "pure server supplied unsupported game-code token 0x%x\n", _gamePakChecksum );
		}
		return PURE_NODLL;
	}

	if ( pureChecksums[ 0 ] == 0 ) {
		ClearPureChecksums();
		return PURE_OK;
	}

	if ( !serverPaks.Num() ) {
		// there was no pure lockdown yet - lock to what we already have
		UpdatePureServerChecksums();
	}
	i = 0; j = 0;
	while ( pureChecksums[ i ] ) {
		if ( j < serverPaks.Num() && serverPaks[ j ]->checksum == pureChecksums[ i ] ) {
			canPrepend = false; // once you start matching into the list there is no prepending anymore
			i++; j++; // the pak is matched, is in the right order, continue..
		} else {
			pack = GetPackForChecksum( pureChecksums[ i ], true );
			if ( pack && pack->addon && !pack->addon_search ) {
				// this is an addon pack, and it's not on our current search list
				// setting success to false meaning that a restart including this addon is required
				if ( fs_debug.GetBool() ) {
					common->Printf( "pak %s checksumed 0x%x is on addon list. Restart required.\n", pack->pakFilename.c_str(), pack->checksum );
				}
				success = false;
			}
			if ( pack && pack->isNew ) {
				// that's a downloaded pack, we will need to restart
				if ( fs_debug.GetBool() ) {
					common->Printf( "pak %s checksumed 0x%x is a newly downloaded file. Restart required.\n", pack->pakFilename.c_str(), pack->checksum );
				}
				success = false;
			}
			if ( pack ) {
				if ( canPrepend ) {
					// we still have a chance
					if ( fs_debug.GetBool() ) {
						common->Printf( "prepend pak %s checksumed 0x%x at index %d\n", pack->pakFilename.c_str(), pack->checksum, j );
					}
					// NOTE: there is a light possibility this adds at the end of the list if UpdatePureServerChecksums didn't set anything
					serverPaks.Insert( pack, j );
					i++; j++; // continue..
				} else {
					success = false;
					if ( fs_debug.GetBool() ) {
						// verbose the situation
						if ( serverPaks.Find( pack ) ) {
							common->Printf( "pak %s checksumed 0x%x is in the pure list at wrong index. Current index is %d, found at %d\n", pack->pakFilename.c_str(), pack->checksum, j, serverPaks.FindIndex( pack ) );
						} else {
							common->Printf( "pak %s checksumed 0x%x can't be added to pure list because of search order\n", pack->pakFilename.c_str(), pack->checksum );
						}
					}
					i++; // advance server checksums only
				}
			} else {
				// didn't find a matching checksum
				success = false;
				missingChecksums[ imissing++ ] = pureChecksums[ i ];
				missingChecksums[ imissing ] = 0;
				if ( fs_debug.GetBool() ) {
					common->Printf( "checksum not found - 0x%x\n", pureChecksums[ i ] );
				}
				i++; // advance the server checksums only
			}
		}
	}
	while ( j < serverPaks.Num() ) {
		success = false; // just in case some extra pak files are referenced at the end of our local list
		if ( fs_debug.GetBool() ) {
			common->Printf( "pak %s checksumed 0x%x is an extra reference at the end of local pure list\n", serverPaks[ j ]->pakFilename.c_str(), serverPaks[ j ]->checksum );
		}
		j++;
	}

	// we reply to missing after DLL check so it can be part of the list
	if ( imissing ) {
		return PURE_MISSING;
	}

	// one last check
	if ( loadedFileFromDir ) {
		success = false;
		if ( fs_debug.GetBool() ) {
			common->Printf( "SetPureServerChecksums: there are files loaded from dir\n" );
		}
	}
	return ( success ? PURE_OK : PURE_RESTART );
}

/*
=====================
idFileSystemLocal::GetPureServerChecksums
=====================
*/
void idFileSystemLocal::GetPureServerChecksums( int checksums[ MAX_PURE_PAKS ], int OS, int *_gamePakChecksum ) {
	int i;

	for ( i = 0; i < serverPaks.Num(); i++ ) {
		checksums[ i ] = serverPaks[ i ]->checksum;
	}
	checksums[ i ] = 0;
	if ( _gamePakChecksum ) {
		if ( OS >= 0 && OS < MAX_GAME_OS ) {
			*_gamePakChecksum = gamePakForOS[ OS ];
		} else if ( OS == -1 ) {
			*_gamePakChecksum = gamePakChecksum;
		} else {
			*_gamePakChecksum = 0;
		}
	}
}

/*
=====================
idFileSystemLocal::SetRestartChecksums
=====================
*/
void idFileSystemLocal::SetRestartChecksums( const int pureChecksums[ MAX_PURE_PAKS ], int gamePakChecksum ) {
	int		i;
	pack_t	*pack;

	restartChecksums.Clear();
	i = 0;
	while ( pureChecksums[ i ] ) {
		pack = GetPackForChecksum( pureChecksums[ i ], true );
		if ( !pack ) {
			common->FatalError( "SetRestartChecksums failed: no pak for checksum 0x%x\n", pureChecksums[i] );
		}
		if ( pack->addon && addonChecksums.FindIndex( pack->checksum ) < 0 ) {
			// can't mark it pure if we're not even gonna search it :-)
			addonChecksums.Append( pack->checksum );
		}
		restartChecksums.Append( pureChecksums[ i ] );
		i++;
	}
	restartGamePakChecksum = gamePakChecksum;
}

/*
================
idFileSystemLocal::Init

Called only at inital startup, not when the filesystem
is resetting due to a game change
================
*/
void idFileSystemLocal::Init( void ) {
	fileSystemMainThread = std::this_thread::get_id();
	// allow command line parms to override our defaults
	// we have to specially handle this, because normal command
	// line variable sets don't happen until after the filesystem
	// has already been initialized
	common->StartupVariable( "fs_basepath", false );
	common->StartupVariable( "fs_homepath", false );
	common->StartupVariable( "fs_savepath", false );
	common->StartupVariable( "fs_game", false );
	common->StartupVariable( "fs_game_base", false );
	common->StartupVariable( "fs_copyfiles", false );
	common->StartupVariable( "fs_restrict", false );
	common->StartupVariable( "fs_searchAddons", false );

#if !ID_ALLOW_D3XP
	if ( fs_game.GetString()[0] && !idStr::Icmp( fs_game.GetString(), "d3xp" ) ) {
		 fs_game.SetString( NULL );
	}
	if ( fs_game_base.GetString()[0] && !idStr::Icmp( fs_game_base.GetString(), "d3xp" ) ) {
		  fs_game_base.SetString( NULL );
	}
#endif	

	// fs_basepath auto-discovery order:
	// 1) valid fs_basepath override
	// 2) current working directory
	// 3) Steam install paths, including explicit OPENQ4_* environment overrides
	// 4) GOG install paths
	if ( fs_basepath.GetString()[0] ) {
		if ( !FS_HasGameFilesAtBasePath( fs_basepath.GetString() ) ) {
			common->Warning( "fs_basepath '%s' has no %s game files, auto-discovery will be attempted", fs_basepath.GetString(), BASE_GAMEDIR );
			fs_basepath.SetString( "" );
		}
	}
	if ( fs_basepath.GetString()[0] == '\0' ) {
		idStr discoveredBasePath;
		if ( FS_AutoDiscoverBasePath( discoveredBasePath ) ) {
			fs_basepath.SetString( discoveredBasePath.c_str() );
			common->Printf( "Auto-detected fs_basepath: %s\n", fs_basepath.GetString() );
		} else {
			fs_basepath.SetString( Sys_DefaultBasePath() );
		}
	}

	// fs_homepath is the user-writable root; fs_savepath follows it when unset.
	if ( fs_homepath.GetString()[0] == '\0' ) {
		if ( fs_savepath.GetString()[0] != '\0' ) {
			fs_homepath.SetString( fs_savepath.GetString() );
		} else {
			fs_homepath.SetString( Sys_DefaultSavePath() );
		}
	}
	if ( fs_savepath.GetString()[0] == '\0' ) {
		fs_savepath.SetString( fs_homepath.GetString() );
	}
	// fs_cdpath is locked to the platform content root (the app's Resources
	// directory on macOS, otherwise the process current directory).
	fs_cdpath.SetString( Sys_DefaultCDPath() );
	common->Printf(
		"Filesystem paths: fs_basepath='%s' fs_homepath='%s' fs_savepath='%s' fs_cdpath='%s' fs_game='%s' fs_game_base='%s'\n",
		fs_basepath.GetString(),
		fs_homepath.GetString(),
		fs_savepath.GetString(),
		fs_cdpath.GetString(),
		fs_game.GetString(),
		fs_game_base.GetString() );

	// try to start up normally
	Startup( );

	// see if we are going to allow add-ons
	SetRestrictions();
	if ( levelLoadCache == NULL ) {
		levelLoadCache = new idLevelLoadCacheManager( this );
	}

	// spawn a thread to handle background file reads
	StartBackgroundDownloadThread();

	currentAssetLog = "assetlogs/default";
	currentAssetLogUnfiltered = "assetlogs/default";

	// if we can't find default.cfg, assume that the paths are
	// busted and error out now, rather than getting an unreadable
	// graphics screen when the font fails to load
	// Dedicated servers can run with no outside files at all
	if ( ReadFile( "default.cfg", NULL, NULL ) <= 0 ) {
		common->FatalError(
			"openQ4 startup config 'default.cfg' could not be loaded.\n\n"
			"Rebuild or reinstall openQ4 so '%s/pak0.pk4' contains the runtime config files, and keep retail Quake 4 media PK4s in '%s'.",
			OPENQ4_GAMEDIR, BASE_GAMEDIR );
	}
}

/*
================
idFileSystemLocal::Restart
================
*/
void idFileSystemLocal::Restart( void ) {
	// free anything we currently have loaded
	Shutdown( true );

	Startup( );

	// see if we are going to allow add-ons
	SetRestrictions();
	if ( levelLoadCache == NULL ) {
		levelLoadCache = new idLevelLoadCacheManager( this );
	}

	// Shutdown( true ) stops the worker, so filesystem restarts must restore it
	// before another background file read or pure-pack download is queued.
	StartBackgroundDownloadThread();

	// if we can't find default.cfg, assume that the paths are
	// busted and error out now, rather than getting an unreadable
	// graphics screen when the font fails to load
	if ( ReadFile( "default.cfg", NULL, NULL ) <= 0 ) {
		common->FatalError(
			"openQ4 startup config 'default.cfg' could not be loaded after filesystem restart.\n\n"
			"Rebuild or reinstall openQ4 so '%s/pak0.pk4' contains the runtime config files, and keep retail Quake 4 media PK4s in '%s'.",
			OPENQ4_GAMEDIR, BASE_GAMEDIR );
	}
}

/*
================
idFileSystemLocal::Shutdown

Frees all resources and closes all files
================
*/
void idFileSystemLocal::Shutdown( bool reloading ) {
	searchpath_t *sp, *next, *loop;
	if ( levelLoadCache != NULL ) {
		levelLoadCache->Cancel();
		if ( !reloading ) {
			delete levelLoadCache;
			levelLoadCache = NULL;
		}
	}

	StopBackgroundDownloadThread();

	gameFolder.Clear();

	serverPaks.Clear();
	if ( reloading && addonChecksums.Num() == 0 ) {
		for ( sp = searchPaths; sp; sp = sp->next ) {
			if ( sp->pack && sp->pack->addon && sp->pack->addon_search &&
				 addonChecksums.FindIndex( sp->pack->checksum ) < 0 ) {
				addonChecksums.Append( sp->pack->checksum );
				common->Printf( "Preserving addon pk4 %s with checksum 0x%x for reload\n",
					sp->pack->pakFilename.c_str(), sp->pack->checksum );
			}
		}
	}
	if ( !reloading ) {
		restartChecksums.Clear();
		addonChecksums.Clear();
	}
	loadedFileFromDir = false;
	gameDLLChecksum = 0;
	gamePakChecksum = 0;
	gameModuleOrigin = GAME_MODULE_ORIGIN_NONE;
	memset( gamePakForOS, 0, sizeof( gamePakForOS ) );

	ClearDirCache();

	// free everything - loop through searchPaths and addonPaks
	for ( loop = searchPaths; loop; loop == searchPaths ? loop = addonPaks : loop = NULL ) {
		for ( sp = loop; sp; sp = next ) {
			next = sp->next;

			if ( sp->pack ) {
				FreePack( sp->pack );
			}
			if ( sp->dir ) {
				delete sp->dir;
			}
			delete sp;
		}
	}

	// any FS_ calls will now be an error until reinitialized
	searchPaths = NULL;
	addonPaks = NULL;

	cmdSystem->RemoveCommand( "path" );
	cmdSystem->RemoveCommand( "dir" );
	cmdSystem->RemoveCommand( "dirtree" );
	cmdSystem->RemoveCommand( "touchFile" );

	mapDict.Clear();
}

/*
================
idFileSystemLocal::IsInitialized
================
*/
bool idFileSystemLocal::IsInitialized( void ) const {
	return ( searchPaths != NULL );
}


/*
=================================================================================

Opening files

=================================================================================
*/

/*
===========
idFileSystemLocal::FileAllowedFromDir
===========
*/
bool idFileSystemLocal::FileAllowedFromDir( const char *path ) {
	size_t l;

	if ( path == NULL ) {
		return false;
	}

	l = strlen( path );
	if ( l == 0 ) {
		return false;
	}

	if ( ( l >= 4 && !strcmp( path + l - 4, ".cfg" ) )		// for config files
		|| ( l >= 4 && !strcmp( path + l - 4, ".dat" ) )		// for journal files
		|| ( l >= 4 && !strcmp( path + l - 4, ".dll" ) )		// dynamic modules are handled a different way for pure
		|| ( l >= 3 && !strcmp( path + l - 3, ".so" ) )
		|| ( l >= 6 && !strcmp( path + l - 6, ".dylib" ) )
		|| ( l >= 10 && !strcmp( path + l - 10, ".scriptcfg" ) )	// configuration script, such as map cycle
#if ID_PURE_ALLOWDDS
		 || ( l >= 4 && !strcmp( path + l - 4, ".dds" ) )
#endif
		 ) {
		// note: cd and xp keys, as well as config.spec are opened through an explicit OS path and don't hit this
		return true;
	}
	// savegames
	if ( strstr( path, "savegames" ) == path &&
		( ( l >= 4 && !strcmp( path + l - 4, ".tga" ) ) || ( l >= 4 && !strcmp( path + l -4, ".txt" ) ) || ( l >= 5 && !strcmp( path + l - 5, ".save" ) ) ) ) {
		return true;
	}
	// screen shots
	if ( strstr( path, "screenshots" ) == path && l >= 4 && !strcmp( path + l - 4, ".tga" ) ) {
		return true;
	}
	// objective tgas
	if ( strstr( path, "maps/game" ) == path &&
		l >= 4 && !strcmp( path + l - 4, ".tga" ) ) {
		return true;
	}
	// splash screens extracted from addons
	if ( strstr( path, "guis/assets/splash/addon" ) == path &&
		 l >= 4 && !strcmp( path + l -4, ".tga" ) ) {
		return true;
	}

	return false;
}

/*
===========
idFileSystemLocal::GetPackStatus
===========
*/
pureStatus_t idFileSystemLocal::GetPackStatus( pack_t *pak ) {
	int				i, l, hashindex;
	fileInPack_t	*file;
	bool			abrt;
	idStr			name;
	const officialPk4Info_t *officialInfo;

	if ( pak->pureStatus != PURE_UNKNOWN ) {
		return pak->pureStatus;
	}

	// Keep openQ4's canonical runtime pack in the pure list no matter its content mix.
	pak->pakFilename.ExtractFileName( name );
	if ( IsOpenQ4PurePack( pak ) ) {
		pak->pureStatus = PURE_ALWAYS;
		return PURE_ALWAYS;
	}

	// Keep the stock Quake 4 base media in the pure list no matter their contents.
	officialInfo = FindOfficialPk4Info( name.c_str() );
	if ( officialInfo && officialInfo->pureBase ) {
		pak->pureStatus = PURE_ALWAYS;
		return PURE_ALWAYS;
	}

	// check content for PURE_NEVER
	i = 0;
	file = pak->buildBuffer;
	for ( hashindex = 0; hashindex < FILE_HASH_SIZE; hashindex++ ) {
		abrt = false;
		file = pak->hashTable[ hashindex ];
		while ( file ) {
			abrt = true;
			l = file->name.Length();
			for ( int j = 0; pureExclusions[j].func != NULL; j++ ) {
				if ( pureExclusions[j].func( pureExclusions[j], l, file->name ) ) {
					abrt = false;
					break;
				}
			}
			if ( abrt ) {
				common->DPrintf( "pak '%s' candidate for pure: '%s'\n", pak->pakFilename.c_str(), file->name.c_str() );
				break;
			}
			file = file->next;
			i++;
		}
		if ( abrt ) {
			break;
		}
	}
	if ( i == pak->numfiles ) {
		pak->pureStatus = PURE_NEVER;
		return PURE_NEVER;
	}

	// check pak name for PURE_ALWAYS
	if ( !name.IcmpPrefixPath( "pak" ) ) {
		pak->pureStatus = PURE_ALWAYS;
		return PURE_ALWAYS;
	}

	pak->pureStatus = PURE_NEUTRAL;
	return PURE_NEUTRAL;
}

/*
===========
idFileSystemLocal::ReadFileFromZip
===========
*/
idFile_InZip * idFileSystemLocal::ReadFileFromZip( pack_t *pak, fileInPack_t *pakFile, const char *relativePath ) {
	unz_s *			zfi;
	FILE *			fp;
	idFile_InZip *file = new idFile_InZip();

	// open a new file on the pakfile
	file->z = unzReOpen( pak->pakFilename, pak->handle );
	if ( file->z == NULL ) {
		common->FatalError( "Couldn't reopen %s", pak->pakFilename.c_str() );
	}
	file->name = relativePath;
	file->fullPath = pak->pakFilename + "/" + relativePath;
	zfi = (unz_s *)file->z;
	// in case the file was new
	fp = zfi->file;
	// set the file position in the zip file (also sets the current file info)
	if ( unzSetCurrentFileInfoPosition( pak->handle, static_cast<unsigned long>( pakFile->pos ) ) != UNZ_OK ) {
		common->Warning( "Could not seek to '%s' in pk4 '%s'", relativePath, pak->pakFilename.c_str() );
		delete file;
		return NULL;
	}
	// copy the file info into the unzip structure
	memcpy( zfi, pak->handle, sizeof(unz_s) );
	// we copy this back into the structure
	zfi->file = fp;
	// open the file in the zip
	if ( unzOpenCurrentFile( file->z ) != UNZ_OK ) {
		common->Warning( "Could not open '%s' in pk4 '%s'", relativePath, pak->pakFilename.c_str() );
		delete file;
		return NULL;
	}
	file->zipFilePos = pakFile->pos;
	file->fileSize = static_cast<int>( zfi->cur_file_info.uncompressed_size );
	file->containerChecksum = pak->checksum;
	return file;
}

/*
===========
idFileSystemLocal::OpenFileReadFlags

Finds the file in the search path, following search flag recommendations
Returns filesize and an open FILE pointer.
Used for streaming data out of either a
separate file or a ZIP file.
===========
*/
idFile *idFileSystemLocal::OpenFileReadFlags( const char *relativePath, int searchFlags, pack_t **foundInPak, bool allowCopyFiles, const char* gamedir ) {
	searchpath_t *	search;
	idStr			netpath;
	pack_t *		pak;
	fileInPack_t *	pakFile;
	directory_t *	dir;
	int				hash;
	FILE *			fp;
	
	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}

	if ( !relativePath ) {
		common->FatalError( "idFileSystemLocal::OpenFileRead: NULL 'relativePath' parameter passed\n" );
	}

	if ( foundInPak ) {
		*foundInPak = NULL;
	}

	// qpaths are not supposed to have a leading slash
	if ( relativePath[0] == '/' || relativePath[0] == '\\' ) {
		relativePath++;
	}

	// make absolutely sure that it can't back up the path.
	// The searchpaths do guarantee that something will always
	// be prepended, so we don't need to worry about "c:" or "//limbo" 
	if ( strstr( relativePath, ".." ) || strstr( relativePath, "::" ) ) {
		return NULL;
	}
	
	// edge case
	if ( relativePath[0] == '\0' ) {
		return NULL;
	}
	
	//
	// search through the path, one element at a time
	//

	hash = HashFileName( relativePath );

	for ( search = searchPaths; search; search = search->next ) {
		if ( search->dir && ( searchFlags & FSFLAG_SEARCH_DIRS ) ) {
			// check a file in the directory tree

			// if we are running restricted, the only files we
			// will allow to come from the directory are .cfg files
			if ( fs_restrict.GetBool() || serverPaks.Num() ) {
				if ( !FileAllowedFromDir( relativePath ) ) {
					continue;
				}
			}

			dir = search->dir;

			if(gamedir && strlen(gamedir)) {
				if(dir->gamedir != gamedir) {
					continue;
				}
			}
			
			netpath = BuildOSPath( dir->path, dir->gamedir, relativePath );
			fp = OpenOSFileCorrectName( netpath, "rb" );
			if ( !fp ) {
				continue;
			}

			idFile_Permanent *file = new idFile_Permanent();
			file->o = fp;
			file->name = relativePath;
			file->fullPath = netpath;
			file->mode = ( 1 << FS_READ );
			file->fileSize = DirectFileLength( file->o );
			if ( fs_debug.GetInteger() ) {
				common->Printf( "idFileSystem::OpenFileRead: %s (found in '%s/%s')\n", relativePath, dir->path.c_str(), dir->gamedir.c_str() );
			}

			if ( !loadedFileFromDir && !FileAllowedFromDir( relativePath ) ) {
				if ( restartChecksums.Num() ) {
					common->FatalError( "'%s' loaded from directory: Failed to restart with pure mode restrictions for server connect", relativePath );
				}
				common->DPrintf( "filesystem: switching to pure mode will require a restart. '%s' loaded from directory.\n", relativePath );
				loadedFileFromDir = true;
			}

			// if fs_copyfiles is set
			if ( allowCopyFiles && fs_copyfiles.GetInteger() ) {

				idStr copypath;
				idStr name;
				copypath = BuildOSPath( fs_savepath.GetString(), dir->gamedir, relativePath );
				netpath.ExtractFileName( name );
				copypath.StripFilename( );
				copypath += PATHSEPERATOR_STR;
				copypath += name;

				bool isFromCDPath = !dir->path.Cmp( fs_cdpath.GetString() );
				bool isFromSavePath = !dir->path.Cmp( fs_savepath.GetString() );
				bool isFromBasePath = !dir->path.Cmp( fs_basepath.GetString() );

				switch ( fs_copyfiles.GetInteger() ) {
					case 1:
						// copy from cd path only
						if ( isFromCDPath ) {
							CopyFile( netpath, copypath );
						}
						break;
					case 2:
						// from cd path + timestamps
						if ( isFromCDPath ) {
							CopyFile( netpath, copypath );
						} else if ( isFromSavePath || isFromBasePath ) {
							idStr sourcepath;
							sourcepath = BuildOSPath( fs_cdpath.GetString(), dir->gamedir, relativePath );
							FILE *f1 = OpenOSFile( sourcepath, "r" );
							if ( f1 ) {
								ID_TIME_T t1 = Sys_FileTimeStamp( f1 );
								fclose( f1 );
								FILE *f2 = OpenOSFile( copypath, "r" );
								if ( f2 ) {
									ID_TIME_T t2 = Sys_FileTimeStamp( f2 );
									fclose( f2 );
									if ( t1 > t2 ) {
										CopyFile( sourcepath, copypath );
									}
								}
							}
						}
						break;
					case 3:
						if ( isFromCDPath || isFromBasePath ) {
							CopyFile( netpath, copypath );
						}
						break;
					case 4:
						if ( isFromCDPath && !isFromBasePath ) {
							CopyFile( netpath, copypath );
						}
						break;
				}
			}

			AddAssetLogEntry( relativePath );
			RecordOpenedLevelLoadSource( relativePath, file );
			return UsePreloadedLevelLoadSource( relativePath, file );
		} else if ( search->pack && ( searchFlags & FSFLAG_SEARCH_PAKS ) ) {

			if ( !search->pack->hashTable[hash] ) {
				continue;
			}

			// disregard if it doesn't match one of the allowed pure pak files
			if ( serverPaks.Num() ) {
				GetPackStatus( search->pack );
				if ( search->pack->pureStatus != PURE_NEVER && !serverPaks.Find( search->pack ) ) {
					continue; // not on the pure server pak list
				}
			}

			// look through all the pak file elements
			pak = search->pack;

			if ( searchFlags & FSFLAG_BINARY_ONLY ) {
				// make sure this pak is tagged as a binary file
				if ( pak->binary == BINARY_UNKNOWN ) {
					int				confHash;
					fileInPack_t	*pakFile;
					confHash = HashFileName( BINARY_CONFIG );
					pak->binary = BINARY_NO;
					for ( pakFile = search->pack->hashTable[confHash]; pakFile; pakFile = pakFile->next ) {
						if ( !FilenameCompare( pakFile->name, BINARY_CONFIG ) ) {
							pak->binary = BINARY_YES;
							break;
						}
					}
				}
				if ( pak->binary == BINARY_NO ) {
					continue; // not a binary pak, skip
				}
			}

			for ( pakFile = pak->hashTable[hash]; pakFile; pakFile = pakFile->next ) {
				// case and separator insensitive comparisons
				if ( !FilenameCompare( pakFile->name, relativePath ) ) {
					idFile_InZip *file = ReadFileFromZip( pak, pakFile, relativePath );
					if ( file == NULL ) {
						continue;
					}

					if ( foundInPak ) {
						*foundInPak = pak;
					}

					if ( !pak->referenced && !( searchFlags & FSFLAG_PURE_NOREF ) ) {
						// mark this pak referenced
						if ( fs_debug.GetInteger( ) ) {
							common->Printf( "idFileSystem::OpenFileRead: %s -> adding %s to referenced paks\n", relativePath, pak->pakFilename.c_str() );
						}
						pak->referenced = true;
					}

					if ( fs_debug.GetInteger( ) ) {
						common->Printf( "idFileSystem::OpenFileRead: %s (found in '%s')\n", relativePath, pak->pakFilename.c_str() );
					}
					AddAssetLogEntry( relativePath );
					RecordOpenedLevelLoadSource( relativePath, file );
					return UsePreloadedLevelLoadSource( relativePath, file );
				}
			}
		}
	}

	if ( searchFlags & FSFLAG_SEARCH_ADDONS ) {
		for ( search = addonPaks; search; search = search->next ) {
			assert( search->pack );
			fileInPack_t	*pakFile;
			pak = search->pack;
			for ( pakFile = pak->hashTable[hash]; pakFile; pakFile = pakFile->next ) {
				if ( !FilenameCompare( pakFile->name, relativePath ) ) {
					idFile_InZip *file = ReadFileFromZip( pak, pakFile, relativePath );
					if ( file == NULL ) {
						continue;
					}
					if ( foundInPak ) {
						*foundInPak = pak;
					}
					// we don't toggle pure on paks found in addons - they can't be used without a reloadEngine anyway
					if ( fs_debug.GetInteger( ) ) {
						common->Printf( "idFileSystem::OpenFileRead: %s (found in addon pk4 '%s')\n", relativePath, search->pack->pakFilename.c_str() );
					}
					AddAssetLogEntry( relativePath );
					RecordOpenedLevelLoadSource( relativePath, file );
					return UsePreloadedLevelLoadSource( relativePath, file );
				}
			}
		}
	}
	
	if ( fs_debug.GetInteger( ) ) {
		common->Printf( "Can't find %s\n", relativePath );
	}
	
	return NULL;
}

/*
===========
idFileSystemLocal::OpenFileRead
===========
*/
idFile *idFileSystemLocal::OpenFileRead( const char *relativePath, bool allowCopyFiles, const char* gamedir ) {
	return OpenFileReadFlags( relativePath, FSFLAG_SEARCH_DIRS | FSFLAG_SEARCH_PAKS, NULL, allowCopyFiles, gamedir );
}

/*
===========
idFileSystemLocal::OpenFileReadFromPak
===========
*/
idFile *idFileSystemLocal::OpenFileReadFromPak( const char *relativePath, bool allowCopyFiles, const char* gamedir ) {
	return OpenFileReadFlags( relativePath, FSFLAG_SEARCH_PAKS, NULL, allowCopyFiles, gamedir );
}

/*
===========
idFileSystemLocal::OpenFileWrite
===========
*/
idFile *idFileSystemLocal::OpenFileWrite( const char *relativePath, const char *basePath ) {
	const char *path;
	idStr OSpath;
	idFile_Permanent *f;

	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}
	const char *invalidReason;
	if ( !FS_ValidateRelativeWritePath( relativePath, &invalidReason ) ) {
		common->Warning( "idFileSystemLocal::OpenFileWrite: refusing unsafe relative path (%s)", invalidReason );
		return NULL;
	}

	path = cvarSystem->GetCVarString( basePath );
	if ( !path[0] ) {
		path = fs_savepath.GetString();
	}

	OSpath = BuildOSPath( path, gameFolder, relativePath );

	if ( fs_debug.GetInteger() ) {
		common->Printf( "idFileSystem::OpenFileWrite: %s\n", OSpath.c_str() );
	}

	// if the dir we are writing to is in our current list, it will be outdated
	// so just flush everything
	ClearDirCache();

	common->DPrintf( "writing to: %s\n", OSpath.c_str() );
	CreateOSPath( OSpath );

	f = new idFile_Permanent();
	f->o = OpenOSFile( OSpath, "wb" );
	if ( !f->o ) {
		delete f;
		return NULL;
	}
	f->name = relativePath;
	f->fullPath = OSpath;
	f->mode = ( 1 << FS_WRITE );
	f->handleSync = false;
	f->fileSize = 0;

	return f;
}

/*
===========
idFileSystemLocal::OpenExplicitFileRead
===========
*/
idFile *idFileSystemLocal::OpenExplicitFileRead( const char *OSPath ) {
	idFile_Permanent *f;

	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}

	if ( fs_debug.GetInteger() ) {
		common->Printf( "idFileSystem::OpenExplicitFileRead: %s\n", OSPath );
	}

	common->DPrintf( "idFileSystem::OpenExplicitFileRead - reading from: %s\n", OSPath );

	f = new idFile_Permanent();
	f->o = OpenOSFile( OSPath, "rb" );
	if ( !f->o ) {
		delete f;
		return NULL;
	}
	f->name = OSPath;
	f->fullPath = OSPath;
	f->mode = ( 1 << FS_READ );
	f->handleSync = false;
	f->fileSize = DirectFileLength( f->o );

	return f;
}

/*
===========
idFileSystemLocal::OpenExplicitFileWrite
===========
*/
idFile *idFileSystemLocal::OpenExplicitFileWrite( const char *OSPath ) {
	idFile_Permanent *f;

	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}

	if ( fs_debug.GetInteger() ) {
		common->Printf( "idFileSystem::OpenExplicitFileWrite: %s\n", OSPath );
	}

	common->DPrintf( "writing to: %s\n", OSPath );
	CreateOSPath( OSPath );

	f = new idFile_Permanent();
	f->o = OpenOSFile( OSPath, "wb" );
	if ( !f->o ) {
		delete f;
		return NULL;
	}
	f->name = OSPath;
	f->fullPath = OSPath;
	f->mode = ( 1 << FS_WRITE );
	f->handleSync = false;
	f->fileSize = 0;

	return f;
}

/*
===========
idFileSystemLocal::OpenFileAppend
===========
*/
idFile *idFileSystemLocal::OpenFileAppend( const char *relativePath, bool sync, const char *basePath ) {
	const char *path;
	idStr OSpath;
	idFile_Permanent *f;

	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}
	const char *invalidReason;
	if ( !FS_ValidateRelativeWritePath( relativePath, &invalidReason ) ) {
		common->Warning( "idFileSystemLocal::OpenFileAppend: refusing unsafe relative path (%s)", invalidReason );
		return NULL;
	}

	path = cvarSystem->GetCVarString( basePath );
	if ( !path[0] ) {
		path = fs_savepath.GetString();
	}

	OSpath = BuildOSPath( path, gameFolder, relativePath );
	CreateOSPath( OSpath );

	if ( fs_debug.GetInteger() ) {
		common->Printf( "idFileSystem::OpenFileAppend: %s\n", OSpath.c_str() );
	}

	f = new idFile_Permanent();
	f->o = OpenOSFile( OSpath, "ab" );
	if ( !f->o ) {
		delete f;
		return NULL;
	}
	f->name = relativePath;
	f->fullPath = OSpath;
	f->mode = ( 1 << FS_WRITE ) + ( 1 << FS_APPEND );
	f->handleSync = sync;
	f->fileSize = DirectFileLength( f->o );

	return f;
}

/*
================
idFileSystemLocal::OpenFileByMode
================
*/
idFile *idFileSystemLocal::OpenFileByMode( const char *relativePath, fsMode_t mode ) {
	if ( mode == FS_READ ) {
		return OpenFileRead( relativePath );
	}
	if ( mode == FS_WRITE ) {
		return OpenFileWrite( relativePath );
	}
	if ( mode == FS_APPEND ) {
		return OpenFileAppend( relativePath, true );
	}
	common->FatalError( "idFileSystemLocal::OpenFileByMode: bad mode" );
	return NULL;
}

/*
==============
idFileSystemLocal::CloseFile
==============
*/
void idFileSystemLocal::CloseFile( idFile *f ) {
	if ( !searchPaths ) {
		common->FatalError( "Filesystem call made without initialization\n" );
	}
	delete f;
}


/*
=================================================================================

back ground loading

=================================================================================
*/

/*
=================
idFileSystemLocal::CurlWriteFunction
=================
*/
struct curlDownloadContext_t {
	backgroundDownload_t *download;
	const size_t maximumBytes;
	size_t bytesWritten;
};

size_t idFileSystemLocal::CurlWriteFunction( void *ptr, size_t size, size_t nmemb, void *stream ) {
	curlDownloadContext_t *context = static_cast<curlDownloadContext_t *>( stream );
	backgroundDownload_t *bgl = context->download;
	const size_t maximumSize = static_cast<size_t>( -1 );
	if ( size != 0 && nmemb > maximumSize / size ) {
		idStr::Copynz( bgl->url.dlerror, "download byte count overflow", MAX_STRING_CHARS );
		return 0;
	}

	const size_t byteCount = size * nmemb;
	if ( context->bytesWritten > maximumSize - byteCount ) {
		idStr::Copynz( bgl->url.dlerror, "download byte count overflow", MAX_STRING_CHARS );
		return 0;
	}
	if ( context->maximumBytes != 0 &&
		 ( context->bytesWritten > context->maximumBytes || byteCount > context->maximumBytes - context->bytesWritten ) ) {
		idStr::Copynz( bgl->url.dlerror, "download exceeds advertised size", MAX_STRING_CHARS );
		return 0;
	}
	if ( !bgl->f ) {
		return byteCount;
	}

	const size_t written = fwrite( ptr, 1, byteCount, static_cast<idFile_Permanent *>( bgl->f )->GetFilePtr() );
	context->bytesWritten += written;
	return written;
}

/*
=================
idFileSystemLocal::CurlProgressFunction
=================
*/
int idFileSystemLocal::CurlProgressFunction( void *clientp, double dltotal, double dlnow, double ultotal, double ulnow ) {
	backgroundDownload_t *bgl = (backgroundDownload_t *)clientp;
	if ( bgl->url.status == DL_ABORTING || Sys_IsCurrentThreadStopRequested() ) {
		return 1;
	}
	const int reportedTotal = dltotal >= idMath::INT_MAX ? idMath::INT_MAX :
		( dltotal > 0.0 ? static_cast<int>( dltotal ) : 0 );
	const int reportedNow = dlnow >= idMath::INT_MAX ? idMath::INT_MAX :
		( dlnow > 0.0 ? static_cast<int>( dlnow ) : 0 );
	if ( bgl->url.expectedSize > 0 ) {
		bgl->url.dltotal = bgl->url.expectedSize;
		bgl->url.dlnow = Min( reportedNow, bgl->url.expectedSize );
	} else {
		bgl->url.dltotal = reportedTotal;
		bgl->url.dlnow = reportedNow;
	}
	return 0;
}

#if ID_ENABLE_CURL
static bool fsCurlGlobalInitialized = false;
static bool fsCurlHTTPReady = false;

static void FS_ShutdownCurl() {
	fsCurlHTTPReady = false;
	if ( fsCurlGlobalInitialized ) {
		curl_global_cleanup();
		fsCurlGlobalInitialized = false;
	}
}

static bool FS_InitializeCurl() {
#if !OPENQ4_CURL_CAPABLE_BUILD
	common->Warning( "HTTP downloads disabled: libcurl 7.19.4 or newer with asynchronous DNS is required" );
	return false;
#else
	if ( fsCurlHTTPReady ) {
		return true;
	}

	const CURLcode initResult = curl_global_init( CURL_GLOBAL_DEFAULT );
	if ( initResult != CURLE_OK ) {
		common->Warning( "HTTP downloads disabled: curl_global_init failed (%s)", curl_easy_strerror( initResult ) );
		return false;
	}
	fsCurlGlobalInitialized = true;

	const curl_version_info_data *versionInfo = curl_version_info( CURLVERSION_NOW );
	bool hasHTTP = false;
	if ( versionInfo != NULL && versionInfo->protocols != NULL ) {
		for ( const char *const *protocol = versionInfo->protocols; *protocol != NULL; ++protocol ) {
			if ( idStr::Icmp( *protocol, "http" ) == 0 ) {
				hasHTTP = true;
				break;
			}
		}
	}
	if ( versionInfo == NULL || versionInfo->version_num < 0x071304 ||
		 ( versionInfo->features & CURL_VERSION_ASYNCHDNS ) == 0 || !hasHTTP ) {
		common->Warning( "HTTP downloads disabled: libcurl lacks the required version, HTTP, or asynchronous-DNS capability" );
		FS_ShutdownCurl();
		return false;
	}

	fsCurlHTTPReady = true;
	return true;
#endif
}

class idScopedCurlEasySession {
public:
	explicit idScopedCurlEasySession( CURL *session ) : session( session ) {}
	~idScopedCurlEasySession() {
		curl_easy_cleanup( session );
	}

private:
	idScopedCurlEasySession( const idScopedCurlEasySession & ) = delete;
	idScopedCurlEasySession &operator=( const idScopedCurlEasySession & ) = delete;
	CURL *session;
};

static CURLcode FS_ConfigureBoundedHTTPTransfer( CURL *session ) {
#if !OPENQ4_CURL_CAPABLE_BUILD
	return CURLE_FAILED_INIT;
#else
	CURLcode result;

	// Keep package transfer and shutdown behavior bounded even when a remote
	// endpoint accepts a connection and then stops making progress. Protocol
	// masks are repeated here so future callers cannot bypass URLPolicy by
	// reaching libcurl directly.
	result = curl_easy_setopt( session, CURLOPT_PROTOCOLS, static_cast<long>( CURLPROTO_HTTP | CURLPROTO_HTTPS ) );
	if ( result != CURLE_OK ) {
		return result;
	}
	result = curl_easy_setopt( session, CURLOPT_REDIR_PROTOCOLS, static_cast<long>( CURLPROTO_HTTP | CURLPROTO_HTTPS ) );
	if ( result != CURLE_OK ) {
		return result;
	}
	result = curl_easy_setopt( session, CURLOPT_FOLLOWLOCATION, 0L );
	if ( result != CURLE_OK ) {
		return result;
	}
	result = curl_easy_setopt( session, CURLOPT_MAXREDIRS, 0L );
	if ( result != CURLE_OK ) {
		return result;
	}
	result = curl_easy_setopt( session, CURLOPT_CONNECTTIMEOUT, 15L );
	if ( result != CURLE_OK ) {
		return result;
	}
	result = curl_easy_setopt( session, CURLOPT_LOW_SPEED_LIMIT, 1024L );
	if ( result != CURLE_OK ) {
		return result;
	}
	result = curl_easy_setopt( session, CURLOPT_LOW_SPEED_TIME, 30L );
	if ( result != CURLE_OK ) {
		return result;
	}
	result = curl_easy_setopt( session, CURLOPT_TIMEOUT, 3600L );
	if ( result != CURLE_OK ) {
		return result;
	}
	return curl_easy_setopt( session, CURLOPT_NOSIGNAL, 1L );
#endif
}
#endif

/*
===================
BackgroundDownload

Reads part of a file from a background thread.
===================
*/
dword BackgroundDownloadThread( void *parms ) {
	while( 1 ) {
		if ( Sys_IsCurrentThreadStopRequested() ) {
			return 0;
		}

		Sys_EnterCriticalSection();
		backgroundDownload_t	*bgl = fileSystemLocal.backgroundDownloads;
		if ( !bgl ) {
			Sys_LeaveCriticalSection();
			Sys_WaitForEvent();
			continue;
		}
		// remove this from the list
		fileSystemLocal.backgroundDownloads = bgl->next;
		Sys_LeaveCriticalSection();

		bgl->next = NULL;

		if ( bgl->opcode == DLTYPE_FILE ) {
			// use the low level read function, because fread may allocate memory
			fread(bgl->file.buffer, bgl->file.length, 1, static_cast<idFile_Permanent*>(bgl->f)->GetFilePtr());
			bgl->completed = true;
		} else if ( bgl->opcode == DLTYPE_URL ) {
#if ID_ENABLE_CURL
			dlStatus_t expectedStatus = DL_WAIT;
			if ( !bgl->url.status.compare_exchange_strong( expectedStatus, DL_INPROGRESS ) ) {
				bgl->url.dlstatus = -1;
				bgl->url.status = DL_FAILED;
				idStr::Copynz( bgl->url.dlerror,
					expectedStatus == DL_ABORTING ? "download cancelled" : "invalid download state",
					MAX_STRING_CHARS );
				bgl->completed = true;
				continue;
			}
			if ( !fsCurlHTTPReady ) {
				bgl->url.dlstatus = CURLE_FAILED_INIT;
				bgl->url.status = DL_FAILED;
				idStr::Copynz( bgl->url.dlerror,
					"HTTP download support is unavailable with this libcurl runtime",
					MAX_STRING_CHARS );
				bgl->completed = true;
				continue;
			}
			// DLTYPE_URL
			// use a local buffer for curl error since the size define is local
			char error_buf[ CURL_ERROR_SIZE ];
			error_buf[ 0 ] = '\0';
			bgl->url.dlerror[ 0 ] = '\0';
			const size_t maximumBytes = bgl->url.expectedSize > 0 ? static_cast<size_t>( bgl->url.expectedSize ) : 0;
			curlDownloadContext_t downloadContext = { bgl, maximumBytes, 0 };
			CURL *session = curl_easy_init();
			CURLcode ret;
			if ( !session ) {
				bgl->url.dlstatus = CURLE_FAILED_INIT;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			idScopedCurlEasySession scopedSession( session );
			ret = curl_easy_setopt( session, CURLOPT_ERRORBUFFER, error_buf );
			if ( ret ) {
				bgl->url.dlstatus = ret;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			ret = curl_easy_setopt( session, CURLOPT_URL, bgl->url.url.c_str() );
			if ( ret ) {
				bgl->url.dlstatus = ret;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			ret = FS_ConfigureBoundedHTTPTransfer( session );
			if ( ret ) {
				bgl->url.dlstatus = ret;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			ret = curl_easy_setopt( session, CURLOPT_FAILONERROR, 1L );
			if ( ret ) {
				bgl->url.dlstatus = ret;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			ret = curl_easy_setopt( session, CURLOPT_WRITEFUNCTION, idFileSystemLocal::CurlWriteFunction );
			if ( ret ) {
				bgl->url.dlstatus = ret;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			ret = curl_easy_setopt( session, CURLOPT_WRITEDATA, &downloadContext );
			if ( ret ) {
				bgl->url.dlstatus = ret;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			ret = curl_easy_setopt( session, CURLOPT_NOPROGRESS, 0L );
			if ( ret ) {
				bgl->url.dlstatus = ret;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			ret = curl_easy_setopt( session, CURLOPT_PROGRESSFUNCTION, idFileSystemLocal::CurlProgressFunction );
			if ( ret ) {
				bgl->url.dlstatus = ret;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			ret = curl_easy_setopt( session, CURLOPT_PROGRESSDATA, bgl );
			if ( ret ) {
				bgl->url.dlstatus = ret;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			bgl->url.dlnow = 0;
			bgl->url.dltotal = 0;
			ret = curl_easy_perform( session );
			if ( ret ) {
				const char *errorMessage = bgl->url.dlerror[ 0 ] != '\0' ? bgl->url.dlerror :
					( error_buf[ 0 ] != '\0' ? error_buf : curl_easy_strerror( ret ) );
				Sys_Printf( "curl_easy_perform failed: %s\n", errorMessage );
				if ( bgl->url.dlerror[ 0 ] == '\0' ) {
					idStr::Copynz( bgl->url.dlerror, errorMessage, MAX_STRING_CHARS );
				}
				bgl->url.dlstatus = ret;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			if ( downloadContext.maximumBytes != 0 && downloadContext.bytesWritten != downloadContext.maximumBytes ) {
				idStr::snPrintf( bgl->url.dlerror, MAX_STRING_CHARS,
					"download size mismatch: received %zu bytes, expected %zu",
					downloadContext.bytesWritten, downloadContext.maximumBytes );
				Sys_Printf( "%s\n", bgl->url.dlerror );
				bgl->url.dlstatus = CURLE_PARTIAL_FILE;
				bgl->url.status = DL_FAILED;
				bgl->completed = true;
				continue;
			}
			expectedStatus = DL_INPROGRESS;
			if ( !bgl->url.status.compare_exchange_strong( expectedStatus, DL_DONE ) ) {
				bgl->url.dlstatus = -1;
				bgl->url.status = DL_FAILED;
				idStr::Copynz( bgl->url.dlerror,
					expectedStatus == DL_ABORTING ? "download cancelled" : "invalid download completion state",
					MAX_STRING_CHARS );
			}
			bgl->completed = true;
#else
			bgl->url.status = DL_FAILED;
			bgl->url.dlstatus = -1;
			idStr::Copynz( bgl->url.dlerror, "HTTP download support is unavailable in this build", MAX_STRING_CHARS );
			bgl->completed = true;
#endif
		} else {
			bgl->url.status = DL_FAILED;
			bgl->url.dlstatus = -1;
			idStr::Copynz( bgl->url.dlerror, "unsupported background download operation", MAX_STRING_CHARS );
			bgl->completed = true;
		}
	}
	return 0;
}

/*
=================
idFileSystemLocal::StartBackgroundReadThread
=================
*/
void idFileSystemLocal::StartBackgroundDownloadThread() {
	if ( !backgroundThread.threadHandle ) {
#if ID_ENABLE_CURL
		FS_InitializeCurl();
#endif
		Sys_CreateThread( (xthread_t)BackgroundDownloadThread, NULL, THREAD_NORMAL, backgroundThread, "backgroundDownload", g_threads, &g_thread_count );
		if ( !backgroundThread.threadHandle ) {
			common->Warning( "idFileSystemLocal::StartBackgroundDownloadThread: failed" );
#if ID_ENABLE_CURL
			FS_ShutdownCurl();
#endif
		}
	} else {
		common->Printf( "background thread already running\n" );
	}
}

/*
=================
idFileSystemLocal::StopBackgroundDownloadThread
=================
*/
void idFileSystemLocal::StopBackgroundDownloadThread() {
	if ( backgroundThread.threadHandle ) {
		Sys_DestroyThread( backgroundThread );
	}

	Sys_EnterCriticalSection();
	backgroundDownload_t *bgl = backgroundDownloads;
	backgroundDownloads = NULL;
	Sys_LeaveCriticalSection();

	while ( bgl != NULL ) {
		backgroundDownload_t *next = bgl->next;
		bgl->next = NULL;
		if ( bgl->opcode == DLTYPE_URL ) {
			bgl->url.status = DL_FAILED;
			idStr::Copynz( bgl->url.dlerror, "filesystem shutting down", MAX_STRING_CHARS );
		}
		bgl->completed = true;
		bgl = next;
	}

#if ID_ENABLE_CURL
	FS_ShutdownCurl();
#endif
}

/*
=================
idFileSystemLocal::BackgroundDownload
=================
*/
void idFileSystemLocal::BackgroundDownload( backgroundDownload_t *bgl ) {
	if ( bgl == NULL ) {
		return;
	}
	if ( bgl->opcode != DLTYPE_FILE && bgl->opcode != DLTYPE_URL ) {
		bgl->url.status = DL_FAILED;
		bgl->url.dlstatus = -1;
		idStr::Copynz( bgl->url.dlerror, "unsupported background download operation", sizeof( bgl->url.dlerror ) );
		bgl->completed = true;
		return;
	}
	if ( bgl->opcode == DLTYPE_URL && !idURLPolicy::IsAllowedHTTPURL( bgl->url.url.c_str() ) ) {
		// Keep the generic downloader fail-closed too. Today the async pure-pack
		// client is the sole URL producer, but a future caller must not silently
		// re-enable file:, SMB, or another libcurl-supported protocol.
		bgl->url.status = DL_FAILED;
		bgl->url.dlstatus = -1;
		idStr::Copynz( bgl->url.dlerror,
			"download URL must be bounded HTTP or HTTPS with a host",
			sizeof( bgl->url.dlerror ) );
		bgl->completed = true;
		return;
	}
	if ( bgl->opcode == DLTYPE_FILE ) {
		if ( dynamic_cast<idFile_Permanent *>(bgl->f) ) {
			// add the bgl to the background download list
			Sys_EnterCriticalSection();
			bgl->next = backgroundDownloads;
			backgroundDownloads = bgl;
			Sys_TriggerEvent();
			Sys_LeaveCriticalSection();
		} else {
			// read zipped file directly
			bgl->f->Seek( bgl->file.position, FS_SEEK_SET );
			bgl->f->Read( bgl->file.buffer, bgl->file.length );
			bgl->completed = true;
		}
	} else {
		Sys_EnterCriticalSection();
		bgl->next = backgroundDownloads;
		backgroundDownloads = bgl;
		Sys_TriggerEvent();
		Sys_LeaveCriticalSection();
	}
}

/*
=================
idFileSystemLocal::PerformingCopyFiles
=================
*/
bool idFileSystemLocal::PerformingCopyFiles( void ) const {
	return fs_copyfiles.GetInteger() > 0;
}

/*
=================
idFileSystemLocal::FindPakForFileChecksum
=================
*/
pack_t *idFileSystemLocal::FindPakForFileChecksum( const char *relativePath, int findChecksum, bool bReference ) {
	searchpath_t	*search;
	pack_t			*pak;
	fileInPack_t	*pakFile;
	int				hash;
	assert( !serverPaks.Num() );
	hash = HashFileName( relativePath );
	for ( search = searchPaths; search; search = search->next ) {
		if ( search->pack && search->pack->hashTable[ hash ] ) {
			pak = search->pack;
			for ( pakFile = pak->hashTable[ hash ]; pakFile; pakFile = pakFile->next ) {
				if ( !FilenameCompare( pakFile->name, relativePath ) ) {
					idFile_InZip *file = ReadFileFromZip( pak, pakFile, relativePath );
					if ( file == NULL ) {
						continue;
					}
					if ( findChecksum == GetFileChecksum( file ) ) {
						if ( fs_debug.GetBool() ) {
							common->Printf( "found '%s' with checksum 0x%x in pak '%s'\n", relativePath, findChecksum, pak->pakFilename.c_str() );
						}
						if ( bReference ) {
							pak->referenced = true;
							// FIXME: use dependencies for pak references
						}
						CloseFile( file );
						return pak;
					} else if ( fs_debug.GetBool() ) {
						common->Printf( "'%s' in pak '%s' has != checksum %x\n", relativePath, pak->pakFilename.c_str(), GetFileChecksum( file ) );
					}
					CloseFile( file );
				}
			}
		}
	}
	if ( fs_debug.GetBool() ) {
		common->Printf( "no pak file found for '%s' checksumed %x\n", relativePath, findChecksum );
	}
	return NULL;
}

/*
=================
idFileSystemLocal::GetFileChecksum
=================
*/
int idFileSystemLocal::GetFileChecksum( idFile *file ) {
	int len, ret;
	byte *buf;

	file->Seek( 0, FS_SEEK_END );
	len = file->Tell();
	file->Seek( 0, FS_SEEK_SET );
	buf = (byte *)Mem_Alloc( len );
	if ( file->Read( buf, len ) != len ) {
		common->FatalError( "Short read in idFileSystemLocal::GetFileChecksum()\n" );
	}
	ret = MD4_BlockChecksum( buf, len );
	Mem_Free( buf );
	return ret;
}

static idFile *FS_OpenGameModuleFromExeDir( idFileSystemLocal *fileSystemLocal, const idStr &exeDir, const char *gameDir, const char *dllName, idStr &dllPath ) {
	if ( !gameDir || !gameDir[0] ) {
		return NULL;
	}

	dllPath = exeDir;
	dllPath.AppendPath( gameDir );
	dllPath.AppendPath( dllName );
	return fileSystemLocal->OpenExplicitFileRead( dllPath );
}

// Keep failed game-module searches diagnosable without weakening the trusted
// executable/package-root loading policy.  This is especially useful for a
// moved or damaged macOS app bundle, where the modules live in
// Contents/Frameworks instead of next to the game data.
static void FS_AppendGameModuleSearchPath( idStr &searchPaths, const idStr &root, const char *gameDir, const char *dllName ) {
	idStr candidatePath = root;
	if ( gameDir && gameDir[0] ) {
		candidatePath.AppendPath( gameDir );
	}
	candidatePath.AppendPath( dllName );
	if ( searchPaths.Length() > 0 ) {
		searchPaths += " | ";
	}
	searchPaths += candidatePath;
}

/*
=================
idFileSystemLocal::FindDLL
=================
*/
void idFileSystemLocal::FindDLL( const char *name, char _dllPath[ MAX_OSPATH ], bool updateChecksum ) {
	idFile			*dllFile = NULL;
	char			dllName[MAX_OSPATH];
	idStr			dllPath;
	gameModuleOrigin_t resolvedOrigin = GAME_MODULE_ORIGIN_NONE;

	sys->DLL_GetFileName( name, dllName, MAX_OSPATH );

	idStr exeDir = Sys_EXEPath();
	exeDir.StripFilename();

	// Only load openQ4 game modules staged next to the executable or from the
	// platform's trusted module root (self-contained macOS apps use the flat
	// Contents/Frameworks code directory; legacy packages use their adjacent
	// package root). Mods may provide their own module, but content-only mods
	// inherit baseoq4 modules. Do not load executable code from PK4s,
	// fs_savepath, pure-server code paks, or loose files outside the
	// executable/package root.
	idStr moduleSearchRoots[3];
	int numModuleSearchRoots = 0;
	moduleSearchRoots[numModuleSearchRoots++] = exeDir;
	char packageRoot[MAX_OSPATH];
	if ( Sys_GetPackageRootDirectory( packageRoot, sizeof( packageRoot ) ) && exeDir.Icmp( packageRoot ) != 0 ) {
		moduleSearchRoots[numModuleSearchRoots++] = packageRoot;
	}
	char moduleRoot[MAX_OSPATH];
	if ( Sys_GetGameModuleRootDirectory( moduleRoot, sizeof( moduleRoot ) ) ) {
		bool duplicateRoot = false;
		for ( int i = 0; i < numModuleSearchRoots; ++i ) {
			if ( moduleSearchRoots[i].Icmp( moduleRoot ) == 0 ) {
				duplicateRoot = true;
				break;
			}
		}
		if ( !duplicateRoot ) {
			moduleSearchRoots[numModuleSearchRoots++] = moduleRoot;
		}
	}

	const char *moduleGameDir = fs_game.GetString();
	if ( !moduleGameDir[0] ) {
		moduleGameDir = OPENQ4_GAMEDIR;
	}
	if ( !idGameDirPolicy::IsPortableSegment( moduleGameDir ) ) {
		common->Warning( "Refusing game module lookup for unsafe fs_game '%s'", moduleGameDir );
		if ( updateChecksum ) {
			gameDLLChecksum = 0;
			gamePakChecksum = 0;
			gameModuleOrigin = GAME_MODULE_ORIGIN_NONE;
		}
		_dllPath[ 0 ] = '\0';
		return;
	}
	idStr trustedModuleRoots;
	idStr attemptedModulePaths;
	for ( int i = 0; i < numModuleSearchRoots; ++i ) {
		if ( trustedModuleRoots.Length() > 0 ) {
			trustedModuleRoots += " | ";
		}
		trustedModuleRoots += moduleSearchRoots[i];
	}
	for ( int i = 0; !dllFile && i < numModuleSearchRoots; i++ ) {
		FS_AppendGameModuleSearchPath( attemptedModulePaths, moduleSearchRoots[i], moduleGameDir, dllName );
		dllFile = FS_OpenGameModuleFromExeDir( this, moduleSearchRoots[i], moduleGameDir, dllName, dllPath );
		if ( dllFile ) {
			resolvedOrigin = idStr::Icmp( moduleGameDir, OPENQ4_GAMEDIR ) == 0 ?
				GAME_MODULE_ORIGIN_BASE_GAME : GAME_MODULE_ORIGIN_ACTIVE_MOD;
		}
	}

	if ( !dllFile && idStr::Icmp( moduleGameDir, OPENQ4_GAMEDIR ) != 0 ) {
		for ( int i = 0; !dllFile && i < numModuleSearchRoots; i++ ) {
			FS_AppendGameModuleSearchPath( attemptedModulePaths, moduleSearchRoots[i], OPENQ4_GAMEDIR, dllName );
			dllFile = FS_OpenGameModuleFromExeDir( this, moduleSearchRoots[i], OPENQ4_GAMEDIR, dllName, dllPath );
			if ( dllFile ) {
				resolvedOrigin = GAME_MODULE_ORIGIN_BASE_GAME;
			}
		}
		if ( dllFile ) {
			common->DPrintf( "Game DLL '%s' not found in mod directory '%s'; falling back to '%s'.\n", dllName, moduleGameDir, OPENQ4_GAMEDIR );
		}
	}

	for ( int i = 0; !dllFile && i < numModuleSearchRoots; i++ ) {
		FS_AppendGameModuleSearchPath( attemptedModulePaths, moduleSearchRoots[i], NULL, dllName );
		dllPath = moduleSearchRoots[i];
		dllPath.AppendPath( dllName );
		dllFile = OpenExplicitFileRead( dllPath );
		if ( dllFile ) {
			resolvedOrigin = GAME_MODULE_ORIGIN_PACKAGE_ROOT;
		}
	}

	if ( updateChecksum ) {
		if ( dllFile ) {
			gameDLLChecksum = GetFileChecksum( dllFile );
		} else {
			gameDLLChecksum = 0;
		}
		gameModuleOrigin = resolvedOrigin;
		gamePakChecksum = dllFile ? OPENQ4_Q4_142_GAME300_PAK_CHECKSUM : 0;
	}
	if ( dllFile ) {
		dllPath = dllFile->GetFullPath( );
		CloseFile( dllFile );
		dllFile = NULL;
	} else {
		common->Printf(
			"Game module search failed: binary='%s' requestedGameDir='%s' trustedRoots='%s' attemptedPaths='%s'\n",
			dllName,
			moduleGameDir,
			trustedModuleRoots.c_str(),
			attemptedModulePaths.c_str() );
		dllPath = "";
	}
	idStr::snPrintf( _dllPath, MAX_OSPATH, "%s", dllPath.c_str() );
}

/*
================
idFileSystemLocal::ClearDirCache
================
*/
void idFileSystemLocal::ClearDirCache( void ) {
	int i;

	dir_cache_index = 0;
	dir_cache_count = 0;
	for( i = 0; i < MAX_CACHED_DIRS; i++ ) {
		dir_cache[ i ].Clear();
	}
}

/*
===============
idFileSystemLocal::HasD3XP
===============
*/
bool idFileSystemLocal::HasD3XP( void ) {
#if ID_ALLOW_D3XP
	int			i;
#endif
	idStrList	dirs, pk4s;
	idStr		gamepath;

	if ( d3xp == -1 ) {
		return false;
	} else if ( d3xp == 1 ) {
		return true;
	}
	
#if 0
	// check for a d3xp directory with a pk4 file
	// copied over from ListMods - only looks in basepath
	ListOSFiles( fs_basepath.GetString(), "/", dirs );
	for ( i = 0; i < dirs.Num(); i++ ) {
		if ( dirs[i].Icmp( "d3xp" ) == 0 ) {
			gamepath = BuildOSPath( fs_basepath.GetString(), dirs[ i ], "" );
			ListOSFiles( gamepath, ".pk4", pk4s );
			if ( pk4s.Num() ) {
				d3xp = 1;
				return true;
			}
		}
	}
#elif ID_ALLOW_D3XP
	// check for d3xp's d3xp/pak000.pk4 in any search path
	// checking wether the pak is loaded by checksum wouldn't be enough:
	// we may have a different fs_game right now but still need to reply that it's installed
	const char	*search[3];
	idFile	  	*pakfile;
	search[0] = fs_cdpath.GetString();
	search[1] = fs_basepath.GetString();
	search[2] = fs_savepath.GetString();
	for ( i = 0; i < 3; i++ ) {
		if ( !search[ i ] || !search[ i ][ 0 ] ) {
			continue;
		}
		pakfile = OpenExplicitFileRead( BuildOSPath( search[ i ], "d3xp", "pak000.pk4" ) );
		if ( pakfile ) {
			CloseFile( pakfile );
			d3xp = 1;
			return true;
		}
	}
#endif

#if ID_ALLOW_D3XP
	// if we didn't find a pk4 file then the user might have unpacked so look for default.cfg file
	// that's the old way mostly used during developement. don't think it hurts to leave it there
	ListOSFiles( fs_basepath.GetString(), "/", dirs );
	for ( i = 0; i < dirs.Num(); i++ ) {
		if ( dirs[i].Icmp( "d3xp" ) == 0 ) {
			
			gamepath = BuildOSPath( fs_savepath.GetString(), dirs[ i ], "default.cfg" );
			idFile* cfg = OpenExplicitFileRead(gamepath);
			if(cfg) {
				CloseFile(cfg);
				d3xp = 1;
				return true;
			}
		}
	}
#endif
	d3xp = -1;
	return false;
}

/*
===============
idFileSystemLocal::RunningD3XP
===============
*/
bool idFileSystemLocal::RunningD3XP( void ) {
	// TODO: mark the checksum of the gold XP and check for it being referenced ( for double mod support )
	// a simple fs_game check should be enough for now..
	if ( !idStr::Icmp( fs_game.GetString(), "d3xp" ) ||
		 !idStr::Icmp( fs_game_base.GetString(), "d3xp" ) ) {
		return true;
	}
	return false;
}

/*
===============
idFileSystemLocal::MakeTemporaryFile
===============
*/
idFile * idFileSystemLocal::MakeTemporaryFile( void ) {
	FILE *f = tmpfile();
	if ( !f ) {
		common->Warning( "idFileSystem::MakeTemporaryFile failed: %s", strerror( errno ) );
		return NULL;
	}
	idFile_Permanent *file = new idFile_Permanent();
	file->o = f;
	file->name = "<tempfile>";
	file->fullPath = "<tempfile>";
	file->mode = ( 1 << FS_READ ) + ( 1 << FS_WRITE );
	file->fileSize = 0;
	return file;
}

/*
===============
idFileSystemLocal::FindFile
===============
*/
 findFile_t idFileSystemLocal::FindFile( const char *path, bool scheduleAddons ) {
	pack_t *pak;
	idFile *f = OpenFileReadFlags( path, FSFLAG_SEARCH_DIRS | FSFLAG_SEARCH_PAKS | FSFLAG_SEARCH_ADDONS, &pak );
	if ( !f ) {
		return FIND_NO;
	}
	if ( !pak ) {
		// found in FS, not even in paks
		delete f;
		return FIND_YES;
	}
	// marking addons for inclusion on reload - may need to do that even when already in the search path
	if ( scheduleAddons && pak->addon && addonChecksums.FindIndex( pak->checksum ) < 0 ) {
		addonChecksums.Append( pak->checksum );			
	}
	// an addon that's not on search list yet? that will require a restart
	if ( pak->addon && !pak->addon_search ) {
		delete f;
		return FIND_ADDON;
	}
	delete f;
	return FIND_YES;
}

/*
===============
idFileSystemLocal::GetNumMaps
account for actual decls and for addon maps
===============
*/
int idFileSystemLocal::GetNumMaps() {
	int				i;
	searchpath_t	*search = NULL;
	int				ret = declManager->GetNumDecls( DECL_MAPDEF );
	
	// add to this all addon decls - coming from all addon packs ( searched or not )
	for ( i = 0; i < 2; i++ ) {
		if ( i == 0 ) {
			search = searchPaths;
		} else if ( i == 1 ) {
			search = addonPaks;
		}
		for ( ; search ; search = search->next ) {
			if ( !search->pack || !search->pack->addon || !search->pack->addon_info ) {
				continue;
			}
			ret += search->pack->addon_info->mapDecls.Num();
		}
	}
	return ret;
}

/*
===============
idFileSystemLocal::GetMapDecl
retrieve the decl dictionary, add a 'path' value
===============
*/
const idDict * idFileSystemLocal::GetMapDecl( int idecl ) {
	int 					i;
	const idDecl			*mapDecl;
	const idDeclEntityDef	*mapDef;
	int						numdecls = declManager->GetNumDecls( DECL_MAPDEF );
	searchpath_t			*search = NULL;
	
	if ( idecl < numdecls ) {
		mapDecl = declManager->DeclByIndex( DECL_MAPDEF, idecl );
		mapDef = static_cast<const idDeclEntityDef *>( mapDecl );
		if ( !mapDef ) {
			common->Error( "idFileSystemLocal::GetMapDecl %d: not found\n", idecl );
		}
		mapDict = mapDef->dict;
		mapDict.Set( "path", mapDef->GetName() );
		return &mapDict;
	}
	idecl -= numdecls;
	for ( i = 0; i < 2; i++ ) {
		if ( i == 0 ) {
			search = searchPaths;
		} else if ( i == 1 ) {
			search = addonPaks;
		}
		for ( ; search ; search = search->next ) {
			if ( !search->pack || !search->pack->addon || !search->pack->addon_info ) {
				continue;
			}
			// each addon may have a bunch of map decls
			if ( idecl < search->pack->addon_info->mapDecls.Num() ) {
				mapDict = *search->pack->addon_info->mapDecls[ idecl ];
				return &mapDict;
			}
			idecl -= search->pack->addon_info->mapDecls.Num();
			assert( idecl >= 0 );
		}
	}
	return NULL;
}

/*
===============
idFileSystemLocal::FindMapScreenshot
===============
*/
void idFileSystemLocal::FindMapScreenshot( const char *path, char *buf, int len ) {
	idFile	*file;
	idStr	mapname = path;

	mapname.StripPath();
	mapname.StripFileExtension();
	
	idStr::snPrintf( buf, len, "gfx/guis/loadscreens/%s.tga", mapname.c_str() );
	if ( ReadFile( buf, NULL, NULL ) == -1 ) {
		// try to extract from an addon
		file = OpenFileReadFlags( buf, FSFLAG_SEARCH_ADDONS );
		if ( file ) {
			// save it out to an addon splash directory
			int dlen = file->Length();
			char *data = new char[ dlen ];
			file->Read( data, dlen );
			CloseFile( file );
			idStr::snPrintf( buf, len, "guis/assets/splash/addon/%s.tga", mapname.c_str() );
			WriteFile( buf, data, dlen );
			delete[] data;
		} else {
			idStr::Copynz( buf, "gfx/guis/loadscreens/generic.tga", len );
		}
	}
}
