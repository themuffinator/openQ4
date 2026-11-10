#ifndef __SAVEGAMERELEASES_H__
#define __SAVEGAMERELEASES_H__

/*
===============================================================================

	Released savegame builds

	A payload's build number selects how fields added within its format
	version are read. Releases up to and including 0.13.0 were built from a
	shallow checkout and stamped every save with build 1, so their saves are
	recognised by the source snapshot they carry instead, and are read as the
	build their release tag carries: the engine commit count that a full
	checkout stamps, as 0.13.1 (858) and later releases do themselves.

	The engine preflight and both game libraries read this one table.

===============================================================================
*/

struct openQ4SaveGameRelease_t {
	const char *	version;
	const char *	sourceHash;
	int				sourceFileCount;
	int				build;
};

static const openQ4SaveGameRelease_t OPENQ4_SAVEGAME_RELEASES[] = {
	{ "0.10.0",	"19351be39d2d4077a74294c0442707ef9565fc7a2fa9af9b81e05fc9aca8b220", 404, 657 },
	{ "0.11.0",	"484e95b8e36bbe39960e6f2fa4f0805e2f7d19c57ee78ed8d5bef4d72d878a3e", 404, 693 },
	{ "0.12.0",	"f4e6e8c84f7bc1b144489afc15eb4df110c91ffddd2d8a8816735457476ba81e", 404, 719 },
	{ "0.13.0",	"6703f7b6261cde4d425c93ce99167eea1d4acc14f8ec7afa16e5eeeab42d6b8e", 404, 853 },
};

// The build stamped by a shallow release checkout.
static const int OPENQ4_SAVEGAME_SHALLOW_RELEASE_BUILD = 1;

/*
====================
OpenQ4_FindSaveGameRelease

The release that wrote a payload stamped with the shallow-checkout build, or
NULL for any other payload.
====================
*/
static inline const openQ4SaveGameRelease_t *OpenQ4_FindSaveGameRelease( int savedBuild, const char *sourceHash, int sourceFileCount ) {
	if ( savedBuild != OPENQ4_SAVEGAME_SHALLOW_RELEASE_BUILD || sourceHash == NULL ) {
		return NULL;
	}
	for ( int i = 0; i < static_cast<int>( sizeof( OPENQ4_SAVEGAME_RELEASES ) / sizeof( OPENQ4_SAVEGAME_RELEASES[0] ) ); i++ ) {
		const openQ4SaveGameRelease_t &release = OPENQ4_SAVEGAME_RELEASES[i];
		if ( release.sourceFileCount == sourceFileCount && idStr::Icmp( release.sourceHash, sourceHash ) == 0 ) {
			return &release;
		}
	}
	return NULL;
}

/*
	Version 2 (0.6.95 to 0.9.0, builds 545-639) carried no wire-ABI stamp. It
	shipped the Windows x64 raw layout and is read only there. Build 546, the
	0.6.95 tag (22c7c52f), added four fields to each saved sound channel, and
	every v2 payload stamped build 1 is a release from 0.6.95 on.
*/
static const int OPENQ4_SAVEGAME_V2_VERSION = 2;
static const int OPENQ4_SAVEGAME_V2_FIRST_RELEASE_BUILD = 546;
static const char *OPENQ4_SAVEGAME_V2_WIRE_ABI = "windows-msvcabi-x64-le-raw1";

/*
	Version 5 (roadmap Q4-06) writes every field with an explicit width and byte
	order, so one payload serves every 64-bit little-endian build: Windows,
	Linux, macOS and Android, on x64 or ARM64. It carries the stamp below.
	Versions 2 to 4 keep the exact raw1 stamp of the build that wrote them.
*/
static const int OPENQ4_SAVEGAME_PORTABLE_VERSION = 5;
static const char *OPENQ4_SAVEGAME_PORTABLE_WIRE_ABI = "le64-portable1";

/*
====================
OpenQ4_SaveGamePortableWireABI

The stamp a version 5 payload carries and must carry to load. A build outside
the portable family (pointers other than 64 bits, or big-endian) stamps its own
wire ABI instead, so it reads only its own payloads and refuses portable ones
before map teardown.
====================
*/
static inline const char *OpenQ4_SaveGamePortableWireABI( const char *nativeWireABI ) {
#if defined( __BYTE_ORDER__ ) && defined( __ORDER_BIG_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return nativeWireABI;
#else
	return sizeof( void * ) == 8 ? OPENQ4_SAVEGAME_PORTABLE_WIRE_ABI : nativeWireABI;
#endif
}

/*
====================
OpenQ4_SaveGameLayoutBuild

The build a payload's layout is read as: its release's build when a shallow
release checkout stamped it, otherwise the build it carries.
====================
*/
static inline int OpenQ4_SaveGameLayoutBuild( int version, int savedBuild, const char *sourceHash, int sourceFileCount ) {
	const openQ4SaveGameRelease_t *release = OpenQ4_FindSaveGameRelease( savedBuild, sourceHash, sourceFileCount );
	if ( release != NULL ) {
		return release->build;
	}
	if ( version == OPENQ4_SAVEGAME_V2_VERSION && savedBuild == OPENQ4_SAVEGAME_SHALLOW_RELEASE_BUILD ) {
		return OPENQ4_SAVEGAME_V2_FIRST_RELEASE_BUILD;
	}
	return savedBuild;
}

/*
===============================================================================

	Interpolators saved as raw structures

	The animator's joint modifiers and GUI transitions save their
	idInterpolateAccelDecelLinear as raw bytes. Build 1582 (f15a41f03) gave
	idExtrapolate a cache flag, which grew both records by a word, so a payload
	whose layout build predates it carries the structure below instead.

===============================================================================
*/

static const int OPENQ4_SAVEGAME_BUILD_WITH_INTERPOLATOR_CACHE_FLAG = 1582;

template< class type >
struct openQ4SaveGameLegacyExtrapolate_t {
	extrapolation_t		extrapolationType;
	float				startTime;
	float				duration;
	type				startValue;
	type				baseSpeed;
	type				speed;
	float				currentTime;
	type				currentValue;
};

template< class type >
struct openQ4SaveGameLegacyInterpolate_t {
	float				startTime;
	float				accelTime;
	float				linearTime;
	float				decelTime;
	type				startValue;
	type				endValue;
	openQ4SaveGameLegacyExtrapolate_t<type> extrapolate;

	// The saved timing and endpoints; the extrapolator state is derived from them.
	void				ToInterpolate( idInterpolateAccelDecelLinear<type> &lerp ) const {
							lerp.Init( startTime, accelTime, decelTime, accelTime + linearTime + decelTime, startValue, endValue );
						}
};

static inline bool OpenQ4_SaveGameHasInterpolatorCacheFlag( int layoutBuild ) {
	return layoutBuild >= OPENQ4_SAVEGAME_BUILD_WITH_INTERPOLATOR_CACHE_FLAG;
}

/*
===============================================================================

	GUI content revisions

	A GUI's saved state follows the structure of the .gui file that built it,
	so an edit to a GUI that saves leaves older payloads with records in the
	previous structure. Each such file keeps its previous revision under
	guis/savecompat/ (a GUI openQ4 reads from its own definition in place of a
	game's file names that file itself, which is parsed as written). A record
	from a payload whose layout build predates the
	edit, and whose blob does not name a window only the current revision has
	(a build of uncommitted work may already carry the edit), is read into a
	copy built from that revision, and its state carries over to the current
	GUI.

===============================================================================
*/

struct openQ4SaveGameGuiRevision_t {
	const char *	name;
	int				build;				// first build with the current revision
	const char *	savedDefinition;	// the revision earlier builds saved
	const char *	currentWindow;		// a window only the current revision has
};

// A GUI edited more than once lists its revisions oldest first: a record is
// read through the first revision whose build its payload predates.
static const openQ4SaveGameGuiRevision_t OPENQ4_SAVEGAME_GUI_REVISIONS[] = {
	// 38bdc6872: reduced flashing gave the hit indicator a second window and new scripts
	{ "guis/hud.gui",			1562,	"guis/savecompat/hud_r1.gui",			"hit_indicator_reduced" },
	{ "guis/hud_strogg.gui",	1562,	"guis/savecompat/hud_strogg_r1.gui",	"hit_indicator_reduced" },
	// The stock interface's Remastered layout: pickup lines, layout wrappers and the vehicle prompt
	{ "guis/hud.gui",			2280,	"guis/savecompat/hud_r2.gui",			"p_pickup0" },
	{ "guis/hud_strogg.gui",	2280,	"guis/savecompat/hud_strogg_r2.gui",	"p_pickup0" },
	{ "guis/vehicles/hud.gui",	2280,	"guis/savecompat/vehicle_hud_r1.gui",	"oq4_prompt" },
	// The stock interface's HUD refinements: the damage direction's mark, the prompt's lock, and
	// the sizes, offsets, scale anchors and notice stacking that changed these GUIs' expressions
	{ "guis/hud.gui",			2323,	"guis/savecompat/hud_r3.gui",			"hit_chevron" },
	{ "guis/hud_strogg.gui",	2323,	"guis/savecompat/hud_strogg_r3.gui",	"hit_chevron" },
	{ "guis/vehicles/hud.gui",	2323,	"guis/savecompat/vehicle_hud_r2.gui",	"oq4_prompt_padlock" },
	// The Awakening's on-foot HUD is read from openQ4's laid-out definition of it
	// (idLegacyHudLayout::DefinitionFor); earlier records follow the expansion's own file
	{ "guis/q4xhud.gui",		2330,	"guis/q4xhud.gui",						"p_pickup0" },
	// The Awakening's dropship gun, MCC gun and speeder bike HUDs, read from openQ4's definitions likewise
	{ "guis/vehicles/airassault/turretplayerhud.gui",	2334,	"guis/vehicles/airassault/turretplayerhud.gui",	"oq4_prompt" },
	{ "guis/vehicles/mcc/cannonplayerhud.gui",	2334,	"guis/vehicles/mcc/cannonplayerhud.gui",	"oq4_prompt" },
	{ "guis/vehicles/speederplayerhud.gui",	2334,	"guis/vehicles/speederplayerhud.gui",	"oq4_prompt" },
	// The stock vehicle HUD's prompt and exit line follow the game's vehicle_locked, and with it
	// the Awakening definitions generated from that HUD
	{ "guis/vehicles/hud.gui",	2342,	"guis/savecompat/vehicle_hud_r3.gui",	"lock_state_msg" },
	{ "guis/vehicles/airassault/turretplayerhud.gui",	2342,	"guis/savecompat/q4x_turretplayerhud_r1.gui",	"lock_state_msg" },
	{ "guis/vehicles/mcc/cannonplayerhud.gui",	2342,	"guis/savecompat/q4x_cannonplayerhud_r1.gui",	"lock_state_msg" },
	{ "guis/vehicles/speederplayerhud.gui",	2342,	"guis/savecompat/q4x_speederplayerhud_r1.gui",	"lock_state_msg" },
};

static inline const openQ4SaveGameGuiRevision_t *OpenQ4_FindSaveGameGuiRevision( const char *guiName, int layoutBuild ) {
	for ( int i = 0; i < static_cast<int>( sizeof( OPENQ4_SAVEGAME_GUI_REVISIONS ) / sizeof( OPENQ4_SAVEGAME_GUI_REVISIONS[0] ) ); i++ ) {
		const openQ4SaveGameGuiRevision_t &revision = OPENQ4_SAVEGAME_GUI_REVISIONS[i];
		if ( idStr::Icmp( guiName, revision.name ) == 0 && layoutBuild < revision.build ) {
			return &revision;
		}
	}
	return NULL;
}

#ifndef GAME_DLL
/*
The payload a game restore is reading, which the session sets for the duration
of the restore. GUI records the game reads from that file (their transitions)
use the format version and layout of the build that wrote it; every other read,
such as an in-memory GUI snapshot, uses the current ones. Defined in
ui/Window.cpp, which every engine target links.
*/
struct openQ4SaveGameRestore_t {
	const idFile *		file;
	int					layoutBuild;
	int					version;		// payload version; 0 for an unstamped payload
};
extern openQ4SaveGameRestore_t openQ4_saveGameRestore;

static const int OPENQ4_SAVEGAME_CURRENT_LAYOUT = 0x7fffffff;

// The layout build of what `file` holds: the restored payload's, or current.
static inline int OpenQ4_SaveGameRestoreLayoutBuild( const idFile *file ) {
	return file != NULL && file == openQ4_saveGameRestore.file ? openQ4_saveGameRestore.layoutBuild : OPENQ4_SAVEGAME_CURRENT_LAYOUT;
}

// Whether what `file` holds uses the version 5 encodings: the restored payload
// from version 5 on, and anything the current build wrote.
static inline bool OpenQ4_SaveGameRestoreIsPortable( const idFile *file ) {
	return file == NULL || file != openQ4_saveGameRestore.file || openQ4_saveGameRestore.version >= OPENQ4_SAVEGAME_PORTABLE_VERSION;
}
#endif

#endif /* !__SAVEGAMERELEASES_H__ */
