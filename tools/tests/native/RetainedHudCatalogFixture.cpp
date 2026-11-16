// Executes the retained HUD's artwork catalog and the game's HUD presentation precache.
//
// tools/tests/ui_hud_catalog_state.py splices, verbatim from production:
// - src/ui/RetainedHud.cpp: the level's catalog (Catalog, SharedCatalog, PrefetchCatalog),
//   idRetainedHud::Impl and idRetainedHud::Prepare, and the UI manager's GeneratedHud and
//   PrecacheHudPrimitives;
// - src/mpgame/Player.cpp and src/game/Player.cpp: idPlayer::PrecacheHudPresentation.
//
// Every retained HUD (each stock HUD GUI, and each source the game draws itself, such as
// native/damage-numbers) builds its view from guis/hud/artwork.rui, 8 MB of vector art.
// Each used to read and parse it on its first draw, so the first damage number of a match
// held a client's frame for a few hundred milliseconds; a client sends no commands while its
// frame stalls, and the server's copy of its aim stood still. The catalog is now parsed once
// a level, on a worker during the load, and the local player builds the game's own views as
// it spawns.

#include <cstdarg>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// ---- the retained UI's document and views -------------------------------------------------
namespace openq4 { namespace ui {
struct Diagnostic { std::string pointer, message; };
struct Parse { std::string source; int id = 0; };
class Document {
public:
	bool Load( const std::string &source, std::vector<Diagnostic> &diagnostics );
	const std::string &Source() const { static const std::string none; return impl ? impl->source : none; }
	int Id() const { return impl ? impl->id : 0; }
	std::shared_ptr<Parse> impl;
};
} }

static int mainThreadParses = 0;
static int workerParses = 0;
static int nextParse = 1;
static std::shared_ptr<openq4::ui::Parse> ParseArtwork( const std::string &source, std::vector<openq4::ui::Diagnostic> &diagnostics ) {
	if ( source == "bad artwork" ) {
		diagnostics.push_back( { "/root", "artwork does not validate" } );
		return nullptr;
	}
	auto parse = std::make_shared<openq4::ui::Parse>();
	parse->source = source;
	parse->id = nextParse++;
	return parse;
}
bool openq4::ui::Document::Load( const std::string &source, std::vector<Diagnostic> &diagnostics ) {
	mainThreadParses++;
	impl = ParseArtwork( source, diagnostics );
	return impl != nullptr;
}

struct retainedUIView_t { int document = 0; std::string path; };
static int viewsBuilt = 0;
static int viewsDestroyed = 0;
static retainedUIView_t *RetainedUI_CreateView() { return new retainedUIView_t; }
static bool RetainedUI_LoadParsedView( retainedUIView_t *view, const openq4::ui::Document &document, const std::string &path,
	std::vector<openq4::ui::Diagnostic> &diagnostics ) {
	diagnostics.clear();
	if ( !view || !document.impl ) return false;
	view->document = document.Id();
	view->path = path;
	viewsBuilt++;
	return true;
}
// What a HUD did before the catalog: parse the source itself.
static bool RetainedUI_LoadView( retainedUIView_t *view, const std::string &source, const std::string &path,
	std::vector<openq4::ui::Diagnostic> &diagnostics ) {
	openq4::ui::Document document;
	return document.Load( source, diagnostics ) && RetainedUI_LoadParsedView( view, document, path, diagnostics );
}
static void RetainedUI_DestroyView( retainedUIView_t *view ) {
	if ( view ) {
		viewsDestroyed++;
		delete view;
	}
}

// ---- engine services ------------------------------------------------------------------------
static std::vector<std::string> printed, warnings;
struct FakeCommon {
	void Printf( const char *format, ... ) { va_list a; va_start( a, format ); Record( printed, format, a ); va_end( a ); }
	void DPrintf( const char *format, ... ) { va_list a; va_start( a, format ); Record( printed, format, a ); va_end( a ); }
	void Warning( const char *format, ... ) { va_list a; va_start( a, format ); Record( warnings, format, a ); va_end( a ); }
	static void Record( std::vector<std::string> &into, const char *format, va_list args ) {
		char text[512];
		std::vsnprintf( text, sizeof( text ), format, args );
		into.push_back( text );
	}
} commonStorage, *common = &commonStorage;

static bool retainedInterface = true;
static bool animatedCrosshair = true;
static bool hitMarker = true;
static int damageNumbers = 1;
struct FakeCvars {
	bool GetCVarBool( const char *name ) const { return std::string( name ) == "ui_retained" && retainedInterface; }
} cvarStorage, *cvarSystem = &cvarStorage;
static int Sys_Milliseconds() { return 0; }

static std::map<std::string, std::string> files;
static std::map<std::string, int> reads;
static std::set<std::string> modOwned;
struct idLegacyHudLayout {
	static bool ModOwns( const char *source ) { return modOwned.count( source ) != 0; }
};

// UI_RetainedPrefetchDocuments / UI_RetainedTakePrefetched (UserInterfaceRetained.cpp): parses
// made on a worker during the level load, taken by a load whose source is unchanged.
struct Prefetched { std::string path, source; std::shared_ptr<openq4::ui::Parse> parse; };
static std::vector<Prefetched> prefetched;
static bool workersAvailable = true;
static void UI_RetainedPrefetchDocuments( const char *const *paths, int count ) {
	if ( !workersAvailable ) return;
	for ( int i = 0; i < count; i++ ) {
		const auto file = files.find( paths[i] );
		if ( file == files.end() ) continue;
		std::vector<openq4::ui::Diagnostic> diagnostics;
		workerParses++;
		prefetched.push_back( { paths[i], file->second, ParseArtwork( file->second, diagnostics ) } );
	}
}
// The production take compares the length and content stamp of the file as it
// is now; the fixture compares the file's current text.
typedef long ID_TIME_T;
static const ID_TIME_T FILE_NOT_FOUND_TIMESTAMP = -1;
static bool UI_RetainedTakePrefetched( const std::string &path, int size, std::string &source, ID_TIME_T &stamp, openq4::ui::Document &document ) {
	for ( size_t i = 0; i < prefetched.size(); i++ ) {
		if ( prefetched[i].path != path ) continue;
		const Prefetched entry = prefetched[i];
		prefetched.erase( prefetched.begin() + i );
		const auto file = files.find( path );
		if ( !entry.parse || file == files.end() || static_cast<int>( entry.source.size() ) != size || entry.source != file->second ) return false;
		source = entry.source;
		stamp = 0;
		document.impl = entry.parse;
		return true;
	}
	return false;
}

// ---- the catalog, spliced from src/ui/RetainedHud.cpp ----------------------------------------
namespace {
constexpr const char *Artwork = "guis/hud/artwork.rui";
constexpr const char *Registry = "guis/hud/coverage.json";
bool Read( const char *path, std::string &result ) {
	reads[path]++;
	const auto file = files.find( path );
	if ( file == files.end() ) return false;
	result = file->second;
	return true;
}
// PRODUCTION_CATALOG_STRUCT
// PRODUCTION_CATALOG_VARIABLE
// The registry's JSON reading is not under test: an ordinary registry maps one material.
bool ReadRegistry( const std::string &registry, Catalog &loaded ) {
	if ( registry == "bad registry" ) {
		loaded.error = "invalid retained HUD coverage registry";
		return false;
	}
	loaded.materials.emplace( "gfx/guis/hud/crosshair", "crosshair" );
	return true;
}
// PRODUCTION_SHARED_CATALOG
// PRODUCTION_PREFETCH_CATALOG
}

class idRetainedHud {
public:
	idRetainedHud();
	~idRetainedHud();
	bool Prepare( const char *source );
	struct Impl;
	std::unique_ptr<Impl> impl;
};
// PRODUCTION_IMPL
idRetainedHud::idRetainedHud() : impl( std::make_unique<Impl>() ) {}
idRetainedHud::~idRetainedHud() = default;
// PRODUCTION_PREPARE

// ---- the UI manager's generated HUDs --------------------------------------------------------
template <typename T> struct idList {
	std::vector<T> items;
	int Num() const { return static_cast<int>( items.size() ); }
	int Append( const T &item ) { items.push_back( item ); return Num() - 1; }
	T &operator[]( int index ) { return items[index]; }
	~idList() { for ( auto *item : items ) delete item; }
};
struct idStrList {
	std::vector<std::string> items;
	int FindIndex( const char *text ) const {
		for ( size_t i = 0; i < items.size(); i++ ) if ( items[i] == text ) return static_cast<int>( i );
		return -1;
	}
	int Append( const char *text ) { items.push_back( text ); return static_cast<int>( items.size() ) - 1; }
};
struct idUserInterfaceManagerLocal {
	idRetainedHud *GeneratedHud( const char *source );
	void PrecacheHudPrimitives( const char *source );
	idList<idRetainedHud *> generatedHuds;
	idStrList generatedHudNames;
};
// PRODUCTION_GENERATED_HUD
// PRODUCTION_PRECACHE_PRIMITIVES

// ---- the game's precache, spliced from both game modules ------------------------------------
static std::vector<std::string> precached;
struct FakeUiManager {
	void PrecacheHudPrimitives( const char *source ) { precached.push_back( source ); }
} uiStorage, *uiManager = &uiStorage;
struct idCVar {
	bool *flag; int *value;
	bool GetBool() const { return *flag; }
	int GetInteger() const { return *value; }
};
static idCVar hud_hitMarker{ &hitMarker, nullptr };
static idCVar hud_damageNumbers{ nullptr, &damageNumbers };
static const int DAMAGENUMBERS_OFF = 0;
struct rvAnimatedCrosshair {
	static bool Enabled( void ) { return animatedCrosshair && retainedInterface; }
};
struct { int localClientNum = 0; } gameLocal;

namespace mpgame {
struct idPlayer { int entityNumber = 0; void PrecacheHudPresentation( void ); };
// PRODUCTION_MP_PRESENTATION
}
namespace spgame {
struct idPlayer { int entityNumber = 0; void PrecacheHudPresentation( void ); };
// PRODUCTION_SP_PRESENTATION
}

// ---- checks ---------------------------------------------------------------------------------
static int failures = 0;
static void Expect( bool condition, const char *what ) {
	if ( !condition ) {
		std::printf( "FAILED: %s\n", what );
		failures++;
	}
}
static int Count( const std::vector<std::string> &lines, const std::string &text ) {
	int count = 0;
	for ( const auto &line : lines ) count += line.find( text ) != std::string::npos;
	return count;
}
static void Reset() {
	mainThreadParses = workerParses = viewsBuilt = viewsDestroyed = 0;
	reads.clear();
	printed.clear();
	warnings.clear();
	prefetched.clear();
	files = { { Artwork, "artwork v1" }, { Registry, "registry" } };
	workersAvailable = retainedInterface = true;
	modOwned.clear();
}
static void LevelLoad() { PrefetchCatalog(); }

static void CheckOneParsePerLevel() {
	Reset();
	LevelLoad();
	Expect( workerParses == 1, "a level load parses the artwork on a worker" );
	{
		idRetainedHud marine, multiplayer, numbers;
		Expect( marine.Prepare( "guis/hud.gui" ) && multiplayer.Prepare( "guis/mphud.gui" ) && numbers.Prepare( "native/damage-numbers" ),
			"three HUDs build their views" );
		Expect( mainThreadParses == 0, "no HUD parses the artwork on the main thread" );
		Expect( reads[Artwork] == 1, "the level reads the artwork once" );
		Expect( viewsBuilt == 3, "each HUD builds one view" );
		Expect( marine.impl->view->document != 0 && marine.impl->view->document == multiplayer.impl->view->document &&
			multiplayer.impl->view->document == numbers.impl->view->document, "every view is built from the worker's one parse" );
		Expect( Count( printed, "RETAINED_HUD_LOADED " ) == 3, "each built view is logged" );
		Expect( Count( printed, "(artwork parse prefetched)" ) == 1 && Count( printed, "(artwork shared)" ) == 2,
			"the log says which HUD took the parse and which shared it" );
		Expect( numbers.Prepare( "native/damage-numbers" ) && viewsBuilt == 3, "a prepared HUD builds no second view" );
		Expect( warnings.empty(), "nothing failed" );

		// The next level: the artwork changed meanwhile, and a view the last level built keeps its parse.
		files[Artwork] = "artwork v2";
		LevelLoad();
		idRetainedHud next;
		Expect( next.Prepare( "native/damage-numbers" ), "the next level's HUD builds its view" );
		Expect( next.impl->catalog->artwork.Source() == "artwork v2", "the next level reads the artwork again" );
		Expect( marine.impl->catalog->artwork.Source() == "artwork v1", "a kept view keeps the parse it was built from" );
		Expect( workerParses == 2 && mainThreadParses == 0, "each level parses the artwork once, on a worker" );
	}
	Expect( viewsDestroyed == 4, "every view is destroyed with its HUD" );
}

static void CheckWithoutWorkers() {
	Reset();
	workersAvailable = false;
	LevelLoad();
	idRetainedHud first, second;
	first.Prepare( "guis/hud.gui" );
	second.Prepare( "native/hit-marker" );
	Expect( mainThreadParses == 1, "without workers the first HUD parses the artwork, once" );
	Expect( Count( printed, "(artwork parsed here)" ) == 1, "the log says the HUD parsed it" );
	Expect( first.impl->view->document == second.impl->view->document, "the second HUD shares that parse" );
}

static void CheckFailures() {
	Reset();
	files[Artwork] = "bad artwork";
	workersAvailable = false;
	LevelLoad();
	idRetainedHud first, second;
	Expect( !first.Prepare( "guis/hud.gui" ) && !second.Prepare( "native/crosshair" ), "an artwork that does not validate builds no view" );
	Expect( mainThreadParses == 1 && reads[Artwork] == 1, "a failed catalog is not read or parsed again for each HUD" );
	Expect( Count( warnings, "guis/hud.gui: artwork does not validate" ) == 1 &&
		Count( warnings, "native/crosshair: artwork does not validate" ) == 1, "each HUD reports why" );

	Reset();
	files.erase( Artwork );
	LevelLoad();
	idRetainedHud missing;
	Expect( !missing.Prepare( "guis/hud.gui" ) && Count( warnings, "missing retained artwork catalog" ) == 1,
		"a missing catalog is reported" );

	Reset();
	files[Registry] = "bad registry";
	LevelLoad();
	idRetainedHud registry;
	Expect( !registry.Prepare( "guis/hud.gui" ) && Count( warnings, "invalid retained HUD coverage registry" ) == 1,
		"an invalid registry is reported" );

	Reset();
	modOwned.insert( "guis/hud.gui" );
	LevelLoad();
	idRetainedHud owned;
	Expect( !owned.Prepare( "guis/hud.gui" ) && reads[Artwork] == 0 && viewsBuilt == 0, "a mod's own HUD never loads the catalog" );

	Reset();
	retainedInterface = false;
	LevelLoad();
	Expect( workerParses == 0, "the stock interface parses no artwork" );
}

static void CheckManager() {
	Reset();
	LevelLoad();
	idUserInterfaceManagerLocal manager;
	manager.PrecacheHudPrimitives( "native/damage-numbers" );
	Expect( manager.generatedHuds.Num() == 1 && viewsBuilt == 1, "precaching a source builds its view" );
	Expect( manager.GeneratedHud( "native/damage-numbers" ) == manager.generatedHuds[0], "the draw finds the precached HUD" );
	Expect( manager.GeneratedHud( "native/damage-numbers" )->Prepare( "native/damage-numbers" ) && viewsBuilt == 1,
		"the first draw builds nothing" );
	retainedInterface = false;
	manager.PrecacheHudPrimitives( "native/hit-marker" );
	Expect( manager.generatedHuds.Num() == 1, "the stock interface precaches nothing" );
	retainedInterface = true;
	for ( int i = 0; i < 20; i++ ) manager.GeneratedHud( ( "native/source-" + std::to_string( i ) ).c_str() );
	Expect( manager.generatedHuds.Num() == 16, "at most 16 generated HUDs" );
}

template <typename Player> static std::vector<std::string> Spawn( int entity, int local ) {
	precached.clear();
	gameLocal.localClientNum = local;
	Player player;
	player.entityNumber = entity;
	player.PrecacheHudPresentation();
	return precached;
}
static void CheckGamePrecache() {
	using List = std::vector<std::string>;
	retainedInterface = animatedCrosshair = hitMarker = true;
	damageNumbers = 1;
	Expect( Spawn<mpgame::idPlayer>( 2, 2 ) == List{ "native/crosshair", "native/damage-numbers" },
		"multiplayer: the animated crosshair (which draws the hit marker) and the damage numbers" );
	animatedCrosshair = false;
	Expect( Spawn<mpgame::idPlayer>( 2, 2 ) == List{ "native/hit-marker", "native/damage-numbers" },
		"multiplayer without the animated crosshair: the hit marker and the damage numbers" );
	hitMarker = false;
	damageNumbers = 0;
	Expect( Spawn<mpgame::idPlayer>( 2, 2 ).empty(), "multiplayer with neither shown: nothing" );
	animatedCrosshair = hitMarker = true;
	damageNumbers = 2;
	Expect( Spawn<mpgame::idPlayer>( 3, 2 ).empty(), "another client's player precaches nothing" );
	Expect( Spawn<mpgame::idPlayer>( 0, -1 ).empty(), "a dedicated server precaches nothing" );
	retainedInterface = false;
	Expect( Spawn<mpgame::idPlayer>( 2, 2 ).empty(), "the stock interface precaches nothing" );
	retainedInterface = true;
	Expect( Spawn<spgame::idPlayer>( 0, 0 ) == List{ "native/crosshair" }, "single player: the animated crosshair" );
	animatedCrosshair = false;
	Expect( Spawn<spgame::idPlayer>( 0, 0 ) == List{ "native/hit-marker" }, "single player without it: the hit marker" );
	animatedCrosshair = true;
}

int main() {
	CheckOneParsePerLevel();
	CheckWithoutWorkers();
	CheckFailures();
	CheckManager();
	CheckGamePrecache();
	if ( failures ) {
		std::printf( "%d failure(s)\n", failures );
		return 1;
	}
	std::printf( "retained HUD catalog and presentation precache: ok\n" );
	return 0;
}
