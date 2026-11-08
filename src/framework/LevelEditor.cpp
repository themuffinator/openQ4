// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LevelEditor.h"
#include "LevelEditorSession.h"

#if defined( USE_SDL3 )
#include <SDL3/SDL_process.h>
#elif !defined( _WIN32 )
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
extern char **environ;
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>

static idCVar editorExperimental_autoSaveSeconds( "editorExperimental_autoSaveSeconds", "120", CVAR_TOOL | CVAR_ARCHIVE | CVAR_INTEGER,
	"seconds between dirty editor recovery saves (0 disables periodic saves)", 0, 3600 );
static idCVar editorExperimental_playArgs( "editorExperimental_playArgs", "", CVAR_TOOL,
	"extra command-line arguments for the editor's isolated play-test client" );
static idCVar editorExperimental_buildTimeout( "editorExperimental_buildTimeout", "3600", CVAR_TOOL | CVAR_ARCHIVE | CVAR_INTEGER,
	"seconds before an editor build is stopped", 30, 86400 );
static idCVar editorExperimental_onBuildDone( "editorExperimental_onBuildDone", "", CVAR_TOOL,
	"console command run when an editor build finishes (automation)" );
static idCVar editorExperimental_onPlayDone( "editorExperimental_onPlayDone", "", CVAR_TOOL,
	"console command run when the editor's play-test client exits (automation)" );
static idCVar editorExperimental_externalCheckSeconds( "editorExperimental_externalCheckSeconds", "3", CVAR_TOOL | CVAR_INTEGER,
	"seconds between checks for edits made to the open map by other programs (0 disables)", 0, 600 );
static idCVar editorExperimental_indexBudget( "editorExperimental_indexBudget", "2", CVAR_TOOL | CVAR_FLOAT,
	"milliseconds per frame the editor may spend indexing asset names", 0.25f, 16.0f );

using namespace oq4editor;

double LevelEditor_Milliseconds() {
	using clock = std::chrono::steady_clock;
	static const clock::time_point start = clock::now();
	return std::chrono::duration<double, std::milli>( clock::now() - start ).count();
}

namespace {

constexpr std::size_t MaxBuildFileBytes = MapBuild::MaxOutputBytes;

class EngineMapStorage final : public MapStorage, public BuildFiles {
public:
	bool Read( const std::string &path, std::string &text, bool &exists, std::string &error ) override {
		return ReadFile( fileSystem->OpenFileRead( path.c_str() ), path, MapDocument::MaxSourceBytes, text, exists, error );
	}
	bool WriteAndSync( const std::string &path, const std::string &text, std::string &error ) override {
		idFile *file = fileSystem->OpenFileWrite( path.c_str(), "fs_savepath" );
		if ( !file ) { error = "could not open editor output: " + path; return false; }
		const bool complete = file->Write( text.data(), static_cast<int>( text.size() ) ) == static_cast<int>( text.size() );
		const bool synced = complete && file->Sync();
		fileSystem->CloseFile( file );
		if ( !synced ) { error = "could not write and sync editor output: " + path; }
		return synced;
	}
	bool Promote( const std::string &from, const std::string &to, std::string &error ) override {
		if ( fileSystem->PromoteFile( from.c_str(), to.c_str(), "fs_savepath" ) ) { return true; }
		error = "could not replace editor output: " + to;
		return false;
	}
	void Remove( const std::string &path ) override { fileSystem->RemoveFileChecked( path.c_str(), "fs_savepath" ); }

	// BuildFiles reads only the writable save path: a compiler output must
	// never be confused with a packed file of the same name.
	bool ReadFrom( const std::string &path, std::size_t offset, std::size_t limit, std::string &text ) override {
		text.clear();
		idFile *file = fileSystem->OpenExplicitFileRead( OSPath( path ).c_str() );
		if ( !file ) { return false; }
		const int length = file->Length();
		if ( length > 0 && offset < static_cast<std::size_t>( length ) ) {
			const std::size_t count = ( std::min )( limit, static_cast<std::size_t>( length ) - offset );
			text.resize( count );
			file->Seek( static_cast<long>( offset ), FS_SEEK_SET );
			text.resize( static_cast<std::size_t>( ( std::max )( 0, file->Read( &text[0], static_cast<int>( count ) ) ) ) );
		}
		fileSystem->CloseFile( file );
		return true;
	}
	std::vector<std::string> List( const std::string &directory, const std::string &prefix ) override {
		idStrList files;
		std::vector<std::string> result;
		Sys_ListFiles( OSPath( directory ).c_str(), "", files );
		for ( int i = 0; i < files.Num(); ++i ) {
			const std::string name = files[i].c_str();
			if ( name == "." || name == ".." || name.compare( 0, prefix.size(), prefix ) != 0 ) { continue; }
			result.push_back( name );
		}
		return result;
	}
	std::string OSPath( const std::string &relative ) override {
		std::string path = fileSystem->RelativePathToOSPath( relative.c_str(), "fs_savepath" );
		while ( !path.empty() && ( path.back() == '/' || path.back() == '\\' ) ) { path.pop_back(); }
		return path;
	}
	bool ReadBuildFile( const std::string &path, std::string &text, bool &exists, std::string &error ) {
		return ReadFile( fileSystem->OpenExplicitFileRead( OSPath( path ).c_str() ), path, MaxBuildFileBytes, text, exists, error );
	}
private:
	static bool ReadFile( idFile *file, const std::string &path, std::size_t limit, std::string &text, bool &exists, std::string &error ) {
		text.clear();
		exists = file != NULL;
		if ( !file ) { return true; }
		const int length = file->Length();
		if ( length < 0 || static_cast<std::size_t>( length ) > limit ) {
			fileSystem->CloseFile( file ); error = "file exceeds the editor limit: " + path; return false;
		}
		text.resize( length );
		const bool ok = length == 0 || file->Read( &text[0], length ) == length;
		fileSystem->CloseFile( file );
		if ( !ok ) { error = "incomplete read: " + path; }
		return ok;
	}
};

// Build outputs can exceed the map source limit, and must come from the
// sandbox itself, so builds read through the explicit-path variant.
class BuildStorage final : public BuildFiles {
public:
	explicit BuildStorage( EngineMapStorage &files ) : storage( files ) {}
	bool Read( const std::string &path, std::string &text, bool &exists, std::string &error ) override { return storage.ReadBuildFile( path, text, exists, error ); }
	bool ReadFrom( const std::string &path, std::size_t offset, std::size_t limit, std::string &text ) override { return storage.ReadFrom( path, offset, limit, text ); }
	bool WriteAndSync( const std::string &path, const std::string &text, std::string &error ) override { return storage.WriteAndSync( path, text, error ); }
	bool Promote( const std::string &from, const std::string &to, std::string &error ) override { return storage.Promote( from, to, error ); }
	void Remove( const std::string &path ) override { storage.Remove( path ); }
	std::vector<std::string> List( const std::string &directory, const std::string &prefix ) override { return storage.List( directory, prefix ); }
	std::string OSPath( const std::string &relative ) override { return storage.OSPath( relative ); }
private:
	EngineMapStorage &storage;
};

}

// A child engine process. Clients use SDL3's process API (Windows, Linux,
// macOS); dedicated servers built without SDL3 use posix_spawn on POSIX.
class idLevelEditorChild {
public:
	~idLevelEditorChild() { Kill(); Release(); }
	bool Start( const std::vector<std::string> &arguments, std::string &error ) {
		Release();
		std::vector<const char *> argv;
		for ( const auto &argument : arguments ) { argv.push_back( argument.c_str() ); }
		argv.push_back( nullptr );
#if defined( USE_SDL3 )
		process = SDL_CreateProcess( argv.data(), false );
		if ( !process ) { error = std::string( "could not start " ) + arguments.front() + ": " + SDL_GetError(); return false; }
#elif !defined( _WIN32 )
		const int result = posix_spawnp( &pid, argv[0], nullptr, nullptr, const_cast<char *const *>( argv.data() ), environ );
		if ( result != 0 ) { pid = -1; error = std::string( "could not start " ) + arguments.front() + ": " + strerror( result ); return false; }
#else
		error = "this build cannot start child processes";
		return false;
#endif
		exited = false;
		return true;
	}
	bool Running( int &exitCode ) {
		if ( !exited ) {
#if defined( USE_SDL3 )
			exited = !process || SDL_WaitProcess( process, false, &code );
#elif !defined( _WIN32 )
			int status = 0;
			const pid_t done = pid > 0 ? waitpid( pid, &status, WNOHANG ) : pid;
			if ( done == pid || done < 0 ) { exited = true; code = done < 0 ? -1 : WIFEXITED( status ) ? WEXITSTATUS( status ) : -1; }
#else
			exited = true;
#endif
		}
		exitCode = code;
		return !exited;
	}
	void Kill() {
		if ( exited ) { return; }
#if defined( USE_SDL3 )
		if ( process ) { SDL_KillProcess( process, true ); exited = SDL_WaitProcess( process, true, &code ); }
#elif !defined( _WIN32 )
		if ( pid > 0 ) { kill( pid, SIGKILL ); int status = 0; waitpid( pid, &status, 0 ); code = -1; exited = true; }
#endif
	}
private:
	void Release() {
#if defined( USE_SDL3 )
		if ( process ) { SDL_DestroyProcess( process ); process = nullptr; }
#elif !defined( _WIN32 )
		pid = -1;
#endif
	}
#if defined( USE_SDL3 )
	SDL_Process *process = nullptr;
#elif !defined( _WIN32 )
	pid_t pid = -1;
#endif
	bool exited = true;
	int code = 0;
};

namespace {

class ChildBuildProcess final : public BuildProcess {
public:
	bool Start( const std::vector<std::string> &arguments, std::string &error ) override { return child.Start( arguments, error ); }
	bool Running( int &exitCode ) override { return child.Running( exitCode ); }
	void Kill() override { child.Kill(); }
private:
	idLevelEditorChild child;
};

EngineMapStorage editorStorage;
BuildStorage buildStorage( editorStorage );
std::unique_ptr<idLevelEditorSession> editorSession;
unsigned int lastRecoveryCheck = 0;

std::string ExecutableDirectory() {
	idStr path = Sys_EXEPath();
	path.StripFilename();
	return path.c_str();
}

std::string ExecutableName() {
	idStr path = Sys_EXEPath(), name;
	path.ExtractFileName( name );
	return name.c_str();
}

// The dedicated server compiles without opening a window. Its name follows
// the client's ("openQ4-client_x64.exe" -> "openQ4-ded_x64.exe").
std::string CompilerExecutable() {
	std::string name = ExecutableName();
	const auto client = name.find( "-client" );
	if ( client != std::string::npos ) {
		std::string ded = name;
		ded.replace( client, 7, "-ded" );
		const std::string path = ExecutableDirectory() + "/" + ded;
		idFile *probe = fileSystem->OpenExplicitFileRead( path.c_str() );
		if ( probe ) { fileSystem->CloseFile( probe ); return path; }
	}
	return Sys_EXEPath();
}

std::vector<std::string> ChildBaseArguments( const std::string &executable ) {
	std::vector<std::string> args = { executable };
	const char *basepath = cvarSystem->GetCVarString( "fs_basepath" );
	if ( basepath && *basepath ) { args.insert( args.end(), { "+set", "fs_basepath", basepath } ); }
	for ( const char *name : { "fs_game", "fs_game_base" } ) {
		const char *value = cvarSystem->GetCVarString( name );
		if ( value && *value ) { args.insert( args.end(), { "+set", name, value } ); }
	}
	return args;
}

std::string LowerCopy( std::string text ) {
	for ( char &c : text ) { if ( c >= 'A' && c <= 'Z' ) { c = static_cast<char>( c - 'A' + 'a' ); } }
	return text;
}

}

idLevelEditorSession::idLevelEditorSession() {
	const char *names[LATENCY_COUNT] = { "startup", "edit", "select", "view3d", "ortho", "validate", "refresh" };
	// Budgets for the stock airdefense1 source (2,195 entities, 11,793
	// primitives) on the qualification machine; see level-editor-modernization.md.
	const double budgets[LATENCY_COUNT] = { 6000, 100, 16, 16, 16, 250, 100 };
	for ( int i = 0; i < LATENCY_COUNT; ++i ) { latency[i].name = names[i]; latency[i].budgetMs = budgets[i]; }
	latency[LATENCY_VIEW3D].percentile = latency[LATENCY_ORTHO].percentile = true;
	editor.IsEntityKey = [this]( const std::string &classname, const std::string &key ) {
		const ClassSchema *schema = Schema( classname );
		const PropertySpec *spec = schema ? schema->Find( key ) : nullptr;
		return spec && spec->type == PropertyType::Entity;
	};
}

idLevelEditorSession::~idLevelEditorSession() {
	play.reset(); // kills a play test still running
}

MapWorkspace &idLevelEditorSession::Workspace() {
	if ( !workspace ) { workspace.reset( new MapWorkspace( editorStorage ) ); }
	return *workspace;
}

const MapGeometryCache &idLevelEditorSession::Geometry() {
	const MapDocument &document = Document();
	if ( document.Revision() != geometryRevision || geometryPath != Workspace().Path() ) {
		geometry.Update( document );
		geometryRevision = document.Revision();
		geometryPath = Workspace().Path();
	}
	return geometry;
}

bool idLevelEditorSession::LayerHidden( const Entity &entity ) const {
	const std::string layer = EntityLayer( entity );
	return !layer.empty() && view.hiddenLayers.count( layer );
}

bool idLevelEditorSession::LayerLocked( const Entity &entity ) const {
	const std::string layer = EntityLayer( entity );
	return !layer.empty() && view.lockedLayers.count( layer );
}

bool idLevelEditorSession::Visible( const SelectionItem &item ) const {
	const Entity *entity = workspace ? workspace->Document().Find( item.entity ) : nullptr;
	return entity && !LayerHidden( *entity );
}

bool idLevelEditorSession::Selectable( const SelectionItem &item ) const {
	const Entity *entity = workspace ? workspace->Document().Find( item.entity ) : nullptr;
	return entity && !LayerHidden( *entity ) && !LayerLocked( *entity );
}

bool idLevelEditorSession::IsSelected( const SelectionItem &item ) const {
	for ( const auto &selected : selection ) {
		if ( selected.entity == item.entity && ( selected.primitive == item.primitive || selected.primitive == WholeEntity ) ) { return true; }
	}
	return false;
}

void idLevelEditorSession::Select( const SelectionItem &item, bool add ) {
	if ( !add ) { selection.clear(); }
	if ( !Selectable( item ) ) { return; }
	for ( auto it = selection.begin(); it != selection.end(); ++it ) {
		if ( *it == item ) { if ( add ) { selection.erase( it ); } return; } // toggle
	}
	selection.push_back( item );
}

void idLevelEditorSession::PruneSelection() {
	const MapDocument &document = Document();
	selection.erase( std::remove_if( selection.begin(), selection.end(), [&]( const SelectionItem &item ) {
		const Entity *entity = document.Find( item.entity );
		return !entity || ( item.primitive != WholeEntity && item.primitive >= entity->primitiveRanges.size() );
	} ), selection.end() );
}

EntityId idLevelEditorSession::PrimaryEntity() const {
	return selection.empty() ? 0 : selection.front().entity;
}

bool idLevelEditorSession::SelectionPivot( Vec3 &pivot ) {
	Vec3 mins, maxs;
	if ( !editor.Bounds( Document(), selection, mins, maxs ) ) { return false; }
	pivot = ( mins + maxs ) * 0.5;
	// Keep pivots on the grid so repeated rotations stay aligned.
	for ( int i = 0; i < 3; ++i ) { pivot[i] = SnapValue( pivot[i], view.grid > 0 ? view.grid * 0.5 : 1 ); }
	return true;
}

bool idLevelEditorSession::Transform( const Affine &transform, std::string &error ) {
	PruneSelection();
	for ( const auto &item : selection ) {
		if ( !Selectable( item ) ) { error = "the selection includes a locked or hidden layer"; return false; }
	}
	const double start = LevelEditor_Milliseconds();
	const bool ok = editor.Transform( Document(), selection, transform, view.textureLock, error );
	latency[LATENCY_EDIT].Add( LevelEditor_Milliseconds() - start );
	return ok;
}

bool idLevelEditorSession::Translate( const Vec3 &delta, std::string &error ) { return Transform( Affine::Translation( delta ), error ); }

bool idLevelEditorSession::Rotate( int axis, double degrees, std::string &error ) {
	Vec3 pivot;
	if ( axis < 0 || axis > 2 || !std::isfinite( degrees ) ) { error = "rotation needs an axis 0-2 and finite degrees"; return false; }
	if ( !SelectionPivot( pivot ) ) { error = "nothing is selected"; return false; }
	return Transform( Affine::Rotation( axis, degrees, pivot ), error );
}

bool idLevelEditorSession::Scale( const Vec3 &factors, std::string &error ) {
	Vec3 pivot;
	for ( int i = 0; i < 3; ++i ) { if ( !( factors[i] > 0.001 && factors[i] < 1000 ) ) { error = "scale factors range from 0.001 to 1000"; return false; } }
	if ( !SelectionPivot( pivot ) ) { error = "nothing is selected"; return false; }
	return Transform( Affine::Scale( factors, pivot ), error );
}

const ClassSchema *idLevelEditorSession::Schema( const std::string &classname ) {
	if ( classname.empty() ) { return nullptr; }
	const std::string key = LowerCopy( classname );
	auto found = schemas.find( key );
	if ( found != schemas.end() ) { return found->second.get(); }
	auto schema = std::make_unique<ClassSchema>();
	// Parsing an entityDef reads its text only; models and sounds it names
	// are not loaded.
	idLevelEditorLookup lookup;
	const idDecl *decl = declManager->FindType( DECL_ENTITYDEF, classname.c_str(), false );
	const idDeclEntityDef *def = decl ? static_cast<const idDeclEntityDef *>( decl ) : nullptr;
	if ( def && def->GetState() != DS_DEFAULTED ) {
		schema->known = true;
		schema->usage = def->dict.GetString( "editor_usage" );
		for ( const idKeyValue *kv = def->dict.MatchPrefix( "editor_" ); kv; kv = def->dict.MatchPrefix( "editor_", kv ) ) {
			const std::string declaration = kv->GetKey().c_str();
			const auto space = declaration.find( ' ' );
			if ( space == std::string::npos ) { continue; }
			PropertyType type;
			if ( !TypeFromEditorKey( declaration.substr( 7, space - 7 ), type ) ) { continue; }
			std::string property = declaration.substr( space + 1 );
			while ( !property.empty() && property.back() == ' ' ) { property.pop_back(); }
			if ( property.empty() || schema->Find( property ) ) { continue; }
			schema->properties.push_back( { property, type, kv->GetValue().c_str() } );
		}
	}
	return ( schemas[key] = std::move( schema ) ).get();
}

ValidatorOptions idLevelEditorSession::ValidationOptions() {
	ValidatorOptions options;
	options.schema = [this]( const std::string &classname ) { return Schema( classname ); };
	if ( assets.Complete() && !assets.GetStats().cancelled ) {
		options.assetExists = [this]( PropertyType type, const std::string &name ) {
			if ( type == PropertyType::Material ) { return assets.Contains( AssetIndex::Material, name ); }
			if ( type == PropertyType::Sound ) { return assets.Contains( AssetIndex::Sound, name ) || name.find( '/' ) != std::string::npos; }
			if ( type == PropertyType::Model ) {
				// Models may name a file or a model declaration.
				if ( assets.Contains( AssetIndex::Model, name ) ) { return true; }
				idLevelEditorLookup lookup;
				return declManager->FindType( DECL_MODELDEF, name.c_str(), false ) != NULL;
			}
			return true;
		};
	}
	const std::string path = Workspace().Path();
	options.requirePlayerStart = path.compare( 0, 8, "maps/mp/" ) != 0;
	return options;
}

void idLevelEditorSession::Validate() {
	const double start = LevelEditor_Milliseconds();
	diagnostics = ValidateMap( Document(), &Geometry(), ValidationOptions() );
	latency[LATENCY_VALIDATE].Add( LevelEditor_Milliseconds() - start );
	if ( build && !build->Diagnostics().empty() && builtPath == Workspace().Path() ) {
		for ( const auto &diagnostic : build->Diagnostics() ) { diagnostics.push_back( diagnostic ); }
	}
}

bool idLevelEditorSession::BuildCurrent() const {
	return build && build->GetState() == MapBuild::State::Succeeded && workspace &&
		builtPath == workspace->Path() && builtRevision == workspace->Document().Revision();
}

bool idLevelEditorSession::StartBuild( bool aas, std::string &error ) {
	MapWorkspace &ws = Workspace();
	if ( !ws.Document().IsOpen() || ws.Path().empty() ) { error = "save the map before building"; return false; }
	if ( ws.Document().IsDirty() ) { error = "save the map before building; builds compile the saved file"; return false; }
	if ( build && build->Active() ) { error = "a build is already running"; return false; }
	BuildRequest request;
	request.mapPath = ws.Path();
	request.mapText = ws.Document().Serialize();
	request.aas = aas;
	request.launch = ChildBaseArguments( CompilerExecutable() );
	request.timeoutMs = static_cast<unsigned>( editorExperimental_buildTimeout.GetInteger() ) * 1000u;
	CompileIndex index;
	index.Build( ws.Document() );
	build.reset( new MapBuild( buildStorage, std::make_unique<ChildBuildProcess>() ) );
	if ( !build->Start( request, index, static_cast<unsigned>( Sys_Milliseconds() ), error ) ) { return false; }
	builtPath = ws.Path();
	builtRevision = ws.Document().Revision();
	buildReported = false;
	common->Printf( "editor build started: %s (%s)\n", request.mapPath.c_str(), request.launch.front().c_str() );
	return true;
}

void idLevelEditorSession::CancelBuild() {
	if ( build && build->Active() ) { build->Cancel(); }
}

bool idLevelEditorSession::Playing() {
	if ( !play ) { return false; }
	if ( !play->Running( playExit ) ) {
		play.reset();
		common->Printf( "editor play finished: exit=%d\n", playExit );
		if ( *editorExperimental_onPlayDone.GetString() ) { cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "%s\n", editorExperimental_onPlayDone.GetString() ) ); }
		return false;
	}
	return true;
}

bool idLevelEditorSession::Play( std::string &error ) {
	if ( Playing() ) { error = "a play test is already running"; return false; }
	if ( !BuildCurrent() ) { error = "build the saved map first; play uses the last successful build"; return false; }
	std::string name = builtPath.substr( 5, builtPath.size() - 9 ); // strip "maps/" and ".map"
	std::vector<std::string> args = ChildBaseArguments( Sys_EXEPath() );
	// The play client gets the build sandbox as its whole writable profile:
	// its configuration, saves and logs never touch the editor's.
	args.insert( args.end(), { "+set", "fs_savepath", buildStorage.OSPath( build->SandboxRoot() ),
		"+set", "logFile", "2", "+set", "logFileName", "logs/editor_play.log" } );
	// Split on whitespace with double quotes grouping, as a command line
	// would; the console tokenizer would break "+set" into two tokens.
	std::string current;
	bool quoted = false, any = false;
	for ( const char c : std::string( editorExperimental_playArgs.GetString() ) ) {
		if ( c == '"' ) { quoted = !quoted; any = true; continue; }
		if ( !quoted && ( c == ' ' || c == '\t' ) ) {
			if ( any ) { args.push_back( current ); current.clear(); any = false; }
			continue;
		}
		current += c; any = true;
	}
	if ( any ) { args.push_back( current ); }
	if ( name.compare( 0, 3, "mp/" ) == 0 ) {
		args.insert( args.end(), { "+set", "si_map", name, "+spawnServer" } );
	} else {
		args.insert( args.end(), { "+devmap", name } );
	}
	SaveViewState();
	play = std::make_unique<idLevelEditorChild>();
	if ( !play->Start( args, error ) ) { play.reset(); error = "could not start the play client: " + error; return false; }
	common->Printf( "editor play started: %s in %s (%zu arguments)\n", name.c_str(), args[0].c_str(), args.size() );
	return true;
}

void idLevelEditorSession::StartAssetIndex() {
	if ( assetIndexStarted ) { return; }
	assetIndexStarted = true;
	modelFiles.clear();
	mapFiles.clear();
	const auto decls = []( declType_t type ) {
		return [type]( std::size_t index, std::string &name ) {
			if ( index >= static_cast<std::size_t>( declManager->GetNumDecls( type ) ) ) { return false; }
			const idDecl *decl = declManager->DeclByIndex( type, static_cast<int>( index ), false );
			if ( decl ) { name = decl->GetName(); }
			return true;
		};
	};
	// File trees are listed once, on their first poll, then read in slices.
	const auto files = [this]( std::vector<std::string> &target, const char *root, std::vector<const char *> extensions ) {
		return [this, &target, root, extensions]( std::size_t index, std::string &name ) {
			if ( index == 0 ) {
				target.clear();
				for ( const char *extension : extensions ) {
					idFileList *list = fileSystem->ListFilesTree( root, extension, true );
					for ( int i = 0; i < list->GetNumFiles(); ++i ) { target.push_back( list->GetFile( i ) ); }
					fileSystem->FreeFileList( list );
				}
			}
			if ( index >= target.size() ) { return false; }
			name = target[index];
			return true;
		};
	};
	assets.Begin( {
		{ AssetIndex::EntityDef, decls( DECL_ENTITYDEF ) },
		{ AssetIndex::Material, decls( DECL_MATERIAL ) },
		{ AssetIndex::Sound, decls( DECL_SOUND ) },
		{ AssetIndex::Model, files( modelFiles, "models", { ".lwo", ".ase", ".md5mesh", ".md5r", ".obj" } ) },
		{ AssetIndex::Map, files( mapFiles, "maps", { ".map" } ) } } );
}

double idLevelEditorSession::Latency::Percentile( double fraction ) const {
	if ( recent.empty() ) { return 0; }
	std::vector<float> sorted = recent;
	std::sort( sorted.begin(), sorted.end() );
	return sorted[( std::min )( sorted.size() - 1, static_cast<std::size_t>( fraction * ( sorted.size() - 1 ) + 0.5 ) )];
}

void idLevelEditorSession::PrintLatency() const {
	for ( const auto &entry : latency ) {
		const double p95 = entry.Percentile( 0.95 );
		const double judged = entry.percentile ? p95 : entry.maxMs;
		common->Printf( "editor latency: %s samples=%d last=%.2fms max=%.2fms mean=%.2fms p95=%.2fms budget=%.0fms(%s) %s\n", entry.name, entry.samples,
			entry.lastMs, entry.maxMs, entry.samples ? entry.totalMs / entry.samples : 0.0, p95, entry.budgetMs, entry.percentile ? "p95" : "max",
			entry.samples == 0 ? "unmeasured" : judged <= entry.budgetMs ? "within" : "over" );
	}
}

void idLevelEditorSession::SaveViewState() {
	if ( !workspace || !workspace->Document().IsOpen() || !viewLoaded ) { return; }
	// Selections persist as document indexes; handles are session-local.
	view.selection.clear();
	const auto &entities = workspace->Document().Entities();
	for ( const auto &item : selection ) {
		for ( std::size_t i = 0; i < entities.size(); ++i ) {
			if ( entities[i].id != item.entity ) { continue; }
			view.selection.push_back( item.primitive == WholeEntity ? std::to_string( i ) : std::to_string( i ) + ":" + std::to_string( item.primitive ) );
		}
	}
	view.valid = true;
	std::string error;
	const std::string path = ViewStatePath( viewPath );
	const std::string staged = path + ".editor-tmp";
	if ( !editorStorage.WriteAndSync( staged, view.Serialize(), error ) || !editorStorage.Promote( staged, path, error ) ) {
		editorStorage.Remove( staged );
		common->Printf( "editor view state not saved: %s\n", error.c_str() );
	}
}

void idLevelEditorSession::SyncDocument() {
	MapWorkspace &ws = Workspace();
	if ( !ws.Document().IsOpen() ) { return; }
	if ( viewLoaded && viewPath == ws.Path() ) { return; }
	if ( viewLoaded ) { SaveViewState(); }
	viewPath = ws.Path();
	viewLoaded = true;
	selection.clear();
	diagnostics.clear();
	ViewState loaded;
	std::string text, error;
	bool exists = false;
	if ( editorStorage.Read( ViewStatePath( viewPath ), text, exists, error ) && exists && loaded.Parse( text ) ) {
		view = loaded;
		const auto &entities = ws.Document().Entities();
		for ( const auto &item : view.selection ) {
			const std::size_t entity = std::strtoul( item.c_str(), nullptr, 10 );
			const auto colon = item.find( ':' );
			if ( entity >= entities.size() ) { continue; }
			const std::size_t primitive = colon == std::string::npos ? WholeEntity : std::strtoul( item.c_str() + colon + 1, nullptr, 10 );
			if ( primitive != WholeEntity && primitive >= entities[entity].primitiveRanges.size() ) { continue; }
			selection.push_back( { entities[entity].id, primitive } );
		}
		common->Printf( "editor view state restored: %s mode=%d selection=%zu\n", viewPath.empty() ? "<unnamed>" : viewPath.c_str(), view.mode, selection.size() );
	} else {
		view = ViewState();
	}
}

void idLevelEditorSession::Frame() {
	if ( build && build->Active() ) {
		build->Poll( static_cast<unsigned>( Sys_Milliseconds() ) );
	}
	if ( build && !build->Active() && !buildReported ) {
		buildReported = true;
		const char *states[] = { "idle", "running", "succeeded", "failed", "cancelled" };
		common->Printf( "editor build %s: %s outputs=%zu diagnostics=%zu %s\n", states[static_cast<int>( build->GetState() )],
			builtPath.c_str(), build->Outputs().size(), build->Diagnostics().size(), build->Failure().c_str() );
		for ( const auto &diagnostic : build->Diagnostics() ) {
			common->Printf( "editor build diagnostic: %s entity=%llu primitive=%lld %s\n", SeverityName( diagnostic.severity ),
				static_cast<unsigned long long>( diagnostic.entity ), diagnostic.primitive == WholeEntity ? -1LL : static_cast<long long>( diagnostic.primitive ),
				diagnostic.message.c_str() );
		}
		if ( build->GetState() == MapBuild::State::Succeeded ) {
			// The running engine may hold the previous outputs' contents.
			common->Printf( "editor build published: %s\n", builtPath.c_str() );
		}
		if ( *editorExperimental_onBuildDone.GetString() ) { cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "%s\n", editorExperimental_onBuildDone.GetString() ) ); }
	}
	const unsigned now = static_cast<unsigned>( Sys_Milliseconds() );
	const int externalSeconds = editorExperimental_externalCheckSeconds.GetInteger();
	if ( workspace && externalSeconds > 0 && !workspace->Path().empty() && now - lastExternalCheck >= static_cast<unsigned>( externalSeconds ) * 1000u ) {
		lastExternalCheck = now;
		bool changed = false;
		std::string error;
		if ( workspace->ExternalChange( changed, error ) && changed != externalChange ) {
			externalChange = changed;
			if ( changed ) { common->Printf( "editor external change: %s was modified by another program\n", workspace->Path().c_str() ); }
		}
	}
	Playing();
	if ( assetIndexStarted && !assets.Complete() ) {
		const double start = LevelEditor_Milliseconds(), budget = editorExperimental_indexBudget.GetFloat();
		while ( !assets.Step( 64 ) && LevelEditor_Milliseconds() - start < budget ) {}
		if ( assets.Complete() ) {
			const auto &stats = assets.GetStats();
			common->Printf( "editor asset index: entries=%zu bytes=%zu steps=%zu truncated=%d\n", stats.entries, stats.bytes, stats.steps, stats.truncated );
		}
	}
}

bool idLevelEditorSession::Reload( bool discard, std::string &error ) {
	MapWorkspace &ws = Workspace();
	if ( ws.Path().empty() ) { error = "the document has no file to reload"; return false; }
	SaveViewState();
	const std::string path = ws.Path();
	if ( !ws.Open( path, error, discard ) ) { return false; }
	externalChange = false;
	viewLoaded = false;
	SyncDocument();
	return true;
}

void idLevelEditorSession::Shutdown() {
	SaveViewState();
	CancelBuild();
}

idLevelEditorSession &LevelEditor_Session() {
	if ( !editorSession ) { editorSession.reset( new idLevelEditorSession() ); }
	return *editorSession;
}

oq4editor::MapWorkspace &LevelEditor_Workspace() { return LevelEditor_Session().Workspace(); }

namespace {

void PrintStatus() {
	auto &s = LevelEditor_Session();
	const auto &document = s.Document();
	std::size_t primitives = 0;
	for ( const auto &entity : document.Entities() ) { primitives += entity.primitiveCount; }
	common->Printf( "editor status: open=%d dirty=%d entities=%zu primitives=%zu undo=%d redo=%d historyBytes=%zu selection=%zu path=%s\n",
		document.IsOpen(), document.IsDirty(), document.Entities().size(), primitives,
		document.CanUndo(), document.CanRedo(), document.HistoryBytes(), s.selection.size(),
		s.Workspace().Path().empty() ? "<unnamed>" : s.Workspace().Path().c_str() );
}

bool ParseEntityId( const char *text, EntityId &id ) {
	id = 0;
	if ( !text || !*text ) { return false; }
	EntityId candidate = 0;
	for ( const char *scan = text; *scan; ++scan ) {
		if ( *scan < '0' || *scan > '9' || candidate > ( ( std::numeric_limits<EntityId>::max )() - ( *scan - '0' ) ) / 10 ) { return false; }
		candidate = candidate * 10 + ( *scan - '0' );
	}
	id = candidate;
	return id != 0;
}

// Handles are session-local; scripts can also name "world" (worldspawn)
// and "selected" (the first selected entity).
bool ResolveEntity( const std::string &text, EntityId &id ) {
	auto &s = LevelEditor_Session();
	if ( text == "world" ) { id = s.Document().Entities().empty() ? 0 : s.Document().Entities().front().id; return id != 0; }
	if ( text == "selected" ) { id = s.PrimaryEntity(); return id != 0; }
	if ( text.compare( 0, 5, "name:" ) == 0 ) {
		for ( const auto &entity : s.Document().Entities() ) {
			if ( EqualNoCase( entity.Value( "name" ), text.substr( 5 ) ) ) { id = entity.id; return true; }
		}
		return false;
	}
	return ParseEntityId( text.c_str(), id );
}

// "<handle>" or "<handle>:<primitive>"
bool ParseItem( const char *text, SelectionItem &item ) {
	std::string value = text ? text : "";
	if ( value.compare( 0, 5, "name:" ) == 0 ) {
		// Entity names may contain anything but quotes: no primitive suffix.
		EntityId id;
		if ( !ResolveEntity( value, id ) ) { return false; }
		item = { id, WholeEntity };
		return true;
	}
	const auto colon = value.find( ':' );
	EntityId id;
	if ( !ResolveEntity( value.substr( 0, colon ), id ) ) { return false; }
	item = { id, WholeEntity };
	if ( colon != std::string::npos ) {
		const std::string primitive = value.substr( colon + 1 );
		if ( primitive.empty() || primitive.find_first_not_of( "0123456789" ) != std::string::npos || primitive.size() > 9 ) { return false; }
		item.primitive = static_cast<std::size_t>( std::strtoul( primitive.c_str(), nullptr, 10 ) );
	}
	return true;
}

bool ParseNumber( const char *text, double &value ) {
	char *end = nullptr;
	value = std::strtod( text, &end );
	return end && end != text && !*end && std::isfinite( value );
}

bool ParseVec( const idCmdArgs &args, int first, Vec3 &value ) {
	return args.Argc() >= first + 3 && ParseNumber( args.Argv( first ), value.x ) && ParseNumber( args.Argv( first + 1 ), value.y ) && ParseNumber( args.Argv( first + 2 ), value.z );
}

void PrintDiagnostics( const std::vector<Diagnostic> &diagnostics ) {
	int counts[3] = {};
	for ( const auto &d : diagnostics ) {
		++counts[static_cast<int>( d.severity )];
		common->Printf( "editor diagnostic: %s %s entity=%llu primitive=%lld %s\n", SeverityName( d.severity ), d.code.c_str(),
			static_cast<unsigned long long>( d.entity ), d.primitive == WholeEntity ? -1LL : static_cast<long long>( d.primitive ), d.message.c_str() );
	}
	common->Printf( "editor diagnostics: errors=%d warnings=%d info=%d\n", counts[2], counts[1], counts[0] );
}

const char *helpText =
	"Experimental level editor: 'editorExperimental' opens the workspace. Legacy 'editor' is unchanged.\n"
	"mapEdit new | open <map> | close [discard] | status\n"
	"mapEdit entities [filter] | get <handle>\n"
	"mapEdit set <handle> <key> <value> | unset <handle> <key>\n"
	"mapEdit create <classname> <name> | delete <handle> | undo | redo\n"
	"mapEdit save [map] | savecopy <new-map> | autosave | recover [map] | reload [discard] | external\n"
	"Selections: <handle> selects an entity, <handle>:<n> its nth primitive; 'world', 'selected' and 'name:<name>' name handles.\n"
	"mapEdit select <item>... | add <item>... | none | selection\n"
	"mapEdit move <x> <y> <z> | rotate <axis 0-2> <degrees> | scale <x> <y> <z> | snap | grid <units> | texlock <0|1>\n"
	"mapEdit brush <x1> <y1> <z1> <x2> <y2> <z2> [material] | patch <x1> <y1> <z1> <x2> <y2> <z2> [material]\n"
	"mapEdit duplicate [x y z] | remove | group | ungroup | material <name>\n"
	"mapEdit layer <name|-> | layers | hidelayer <name> | showlayer <name> | locklayer <name> | unlocklayer <name>\n"
	"mapEdit rename <handle> <name> | refs <name> | prefab save <file> | prefab insert <file> <x> <y> <z>\n"
	"mapEdit validate | build [noaas] | buildstatus | cancel | outputs | play | playstatus\n"
	"mapEdit index | find <entitydef|material|model|sound|map> <filter> | latency\n"
	"Writes go to fs_savepath/baseoq4; builds compile the saved map in a sandbox and publish beside it.\n";

bool PrefabPath( const std::string &name, std::string &path, std::string &error ) {
	if ( name.empty() || name.size() > 120 || name.find_first_not_of( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" ) != std::string::npos ) {
		error = "prefab names use 1 to 120 letters, digits, '_' and '-'"; return false;
	}
	path = "prefabs/" + name + ".map";
	return true;
}

void EditorCommand( const idCmdArgs &args ) {
	if ( args.Argc() == 1 || ( args.Argc() == 2 && idStr::Icmp( args.Argv(1), "workspace" ) == 0 ) ) {
		LevelEditor_OpenWorkspace(); return;
	}
	if ( idStr::Icmp( args.Argv(1), "help" ) == 0 ) { common->Printf( "%s", helpText ); return; }
	auto &s = LevelEditor_Session();
	auto &workspace = s.Workspace();
	auto &document = workspace.Document();
	const std::string command = args.Argv(1);
	std::string error;
	bool result = false;
	EntityId id = 0;
	if ( args.Argc() > 2 ) { ResolveEntity( args.Argv(2), id ); }
	const double start = LevelEditor_Milliseconds();
	if ( command == "new" && args.Argc() == 2 ) { result = workspace.New( error ); }
	else if ( command == "open" && args.Argc() == 3 ) { result = workspace.Open( args.Argv(2), error ); }
	else if ( command == "close" && ( args.Argc() == 2 || ( args.Argc() == 3 && idStr::Cmp( args.Argv(2), "discard" ) == 0 ) ) ) {
		s.SaveViewState();
		result = workspace.Close( args.Argc() == 3, error );
	} else if ( command == "status" && args.Argc() == 2 ) { PrintStatus(); return; }
	else if ( command == "entities" && args.Argc() <= 3 ) {
		for ( const auto &entity : document.Entities() ) {
			const std::string label = entity.Value( "name" ) + " " + entity.Value( "classname" );
			if ( args.Argc() == 3 && idStr::FindText( label.c_str(), args.Argv(2), false ) < 0 ) { continue; }
			common->Printf( "editor entity: %llu class=%s name=%s primitives=%zu layer=%s\n",
				static_cast<unsigned long long>( entity.id ), entity.Value( "classname" ).c_str(), entity.Value( "name" ).c_str(),
				entity.primitiveCount, EntityLayer( entity ).c_str() );
		}
		return;
	} else if ( command == "get" && args.Argc() == 3 ) {
		const auto *entity = document.Find( id );
		if ( entity ) {
			const ValidatorOptions options = s.ValidationOptions();
			for ( const auto &property : entity->properties ) {
				PropertyType type;
				const bool typed = PropertyTypeFor( options, *entity, property.key, type );
				common->Printf( "  %s = %s%s%s\n", property.key.c_str(), property.value.c_str(), typed ? "  : " : "", typed ? TypeName( type ) : "" );
			}
			return;
		}
		error = "unknown entity handle";
	} else if ( command == "set" && args.Argc() == 5 ) { result = document.SetProperty( id, args.Argv(3), args.Argv(4), error ); }
	else if ( command == "unset" && args.Argc() == 4 ) { result = document.RemoveProperty( id, args.Argv(3), error ); }
	else if ( command == "create" && args.Argc() == 4 ) {
		result = document.AddEntity( args.Argv(2), args.Argv(3), id, error );
		if ( result ) { common->Printf( "editor created: %llu\n", static_cast<unsigned long long>( id ) ); s.Select( { id, WholeEntity }, false ); }
	} else if ( command == "delete" && args.Argc() == 3 ) { result = document.DeleteEntity( id, error ); }
	else if ( command == "undo" && args.Argc() == 2 ) { result = document.Undo( error ); }
	else if ( command == "redo" && args.Argc() == 2 ) { result = document.Redo( error ); }
	else if ( command == "save" && args.Argc() <= 3 ) { result = workspace.Save( args.Argc() == 3 ? args.Argv(2) : "", false, error ); if ( result ) { s.SyncDocument(); s.SaveViewState(); } }
	else if ( command == "savecopy" && args.Argc() == 3 ) { result = workspace.Save( args.Argv(2), true, error ); }
	else if ( command == "autosave" && args.Argc() == 2 ) { result = workspace.AutoSave( error ); }
	else if ( command == "recover" && args.Argc() <= 3 ) { result = workspace.Recover( args.Argc() == 3 ? args.Argv(2) : "", error ); }
	else if ( command == "reload" && ( args.Argc() == 2 || ( args.Argc() == 3 && idStr::Cmp( args.Argv(2), "discard" ) == 0 ) ) ) {
		result = s.Reload( args.Argc() == 3, error );
	} else if ( command == "external" && args.Argc() == 2 ) {
		bool changed = false;
		result = workspace.ExternalChange( changed, error );
		if ( result ) { common->Printf( "editor external: changed=%d\n", changed ); return; }
	}
	else if ( ( command == "select" || command == "add" ) && args.Argc() >= 3 ) {
		if ( command == "select" ) { s.ClearSelection(); }
		result = true;
		for ( int i = 2; i < args.Argc() && result; ++i ) {
			SelectionItem item;
			const Entity *entity = ParseItem( args.Argv( i ), item ) ? document.Find( item.entity ) : nullptr;
			if ( !entity || ( item.primitive != WholeEntity && item.primitive >= entity->primitiveRanges.size() ) ) { error = "unknown selection item"; result = false; break; }
			if ( !s.Selectable( item ) ) { error = "item is on a hidden or locked layer"; result = false; break; }
			s.Select( item, true );
		}
		const double elapsed = LevelEditor_Milliseconds() - start;
		s.latency[idLevelEditorSession::LATENCY_SELECT].Add( elapsed );
	} else if ( command == "none" && args.Argc() == 2 ) { s.ClearSelection(); result = true; }
	else if ( command == "selection" && args.Argc() == 2 ) {
		for ( const auto &item : s.selection ) {
			common->Printf( "editor selected: %llu%s\n", static_cast<unsigned long long>( item.entity ),
				item.primitive == WholeEntity ? "" : va( ":%zu", item.primitive ) );
		}
		Vec3 mins, maxs;
		if ( s.editor.Bounds( document, s.selection, mins, maxs ) ) {
			common->Printf( "editor selection bounds: %s %s\n", FormatVector( mins ).c_str(), FormatVector( maxs ).c_str() );
		}
		return;
	} else if ( command == "move" && args.Argc() == 5 ) {
		Vec3 delta;
		result = ParseVec( args, 2, delta ) ? s.Translate( delta, error ) : ( error = "move needs three numbers", false );
	} else if ( command == "rotate" && args.Argc() == 4 ) {
		double axis, degrees;
		result = ParseNumber( args.Argv(2), axis ) && ParseNumber( args.Argv(3), degrees ) ? s.Rotate( static_cast<int>( axis ), degrees, error ) : ( error = "rotate needs an axis and degrees", false );
	} else if ( command == "scale" && args.Argc() == 5 ) {
		Vec3 factors;
		result = ParseVec( args, 2, factors ) ? s.Scale( factors, error ) : ( error = "scale needs three numbers", false );
	} else if ( command == "snap" && args.Argc() == 2 ) { result = s.editor.Snap( document, s.selection, s.view.grid, s.view.textureLock, error ); }
	else if ( command == "grid" && args.Argc() == 3 ) {
		double grid;
		result = ParseNumber( args.Argv(2), grid ) && grid >= 0.125 && grid <= 1024;
		if ( result ) { s.view.grid = grid; } else { error = "grid ranges from 0.125 to 1024"; }
	} else if ( command == "texlock" && args.Argc() == 3 ) { s.view.textureLock = atoi( args.Argv(2) ) != 0; result = true; }
	else if ( ( command == "brush" || command == "patch" ) && ( args.Argc() == 8 || args.Argc() == 9 ) ) {
		Vec3 a, b;
		if ( !ParseVec( args, 2, a ) || !ParseVec( args, 5, b ) || document.Entities().empty() ) { error = "brush needs two corners"; }
		else {
			const Vec3 mins( ( std::min )( a.x, b.x ), ( std::min )( a.y, b.y ), ( std::min )( a.z, b.z ) );
			const Vec3 maxs( ( std::max )( a.x, b.x ), ( std::max )( a.y, b.y ), ( std::max )( a.z, b.z ) );
			const std::string material = args.Argc() == 9 ? args.Argv(8) : "textures/common/caulk";
			SelectionItem created;
			const EntityId owner = document.Entities().front().id;
			result = command == "brush" ? s.editor.CreateBrush( document, owner, mins, maxs, material, created, error ) :
				s.editor.CreatePatch( document, owner, mins, maxs, material, created, error );
			if ( result ) { s.Select( created, false ); common->Printf( "editor created: %llu:%zu\n", static_cast<unsigned long long>( created.entity ), created.primitive ); }
		}
	} else if ( command == "duplicate" && ( args.Argc() == 2 || args.Argc() == 5 ) ) {
		Vec3 offset( s.view.grid, s.view.grid, 0 );
		Selection created;
		if ( args.Argc() == 5 && !ParseVec( args, 2, offset ) ) { error = "duplicate offset needs three numbers"; }
		else if ( ( result = s.editor.Duplicate( document, s.selection, offset, created, error ) ) ) { s.selection = created; }
	} else if ( command == "remove" && args.Argc() == 2 ) {
		result = s.editor.Delete( document, s.selection, error );
		if ( result ) { s.ClearSelection(); }
	} else if ( command == "group" && args.Argc() == 2 ) {
		EntityId group = 0;
		result = s.editor.Group( document, s.selection, group, error );
		if ( result ) { s.Select( { group, WholeEntity }, false ); common->Printf( "editor group: %llu\n", static_cast<unsigned long long>( group ) ); }
	} else if ( command == "ungroup" && args.Argc() == 2 ) {
		const EntityId group = s.PrimaryEntity();
		result = s.editor.Ungroup( document, group, error );
		if ( result ) { s.ClearSelection(); }
	} else if ( command == "material" && args.Argc() == 3 ) { result = s.editor.SetMaterial( document, s.selection, args.Argv(2), error ); }
	else if ( command == "layer" && args.Argc() == 3 ) {
		Selection layered;
		result = s.editor.SetLayer( document, s.selection, idStr::Cmp( args.Argv(2), "-" ) == 0 ? "" : args.Argv(2), layered, error );
		if ( result ) { s.selection = layered; }
	} else if ( command == "layers" && args.Argc() == 2 ) {
		std::map<std::string, int> layers;
		for ( const auto &entity : document.Entities() ) { const std::string layer = EntityLayer( entity ); if ( !layer.empty() ) { ++layers[layer]; } }
		for ( const auto &[layer, count] : layers ) {
			common->Printf( "editor layer: %s entities=%d hidden=%d locked=%d\n", layer.c_str(), count, s.view.hiddenLayers.count( layer ) != 0, s.view.lockedLayers.count( layer ) != 0 );
		}
		return;
	} else if ( ( command == "hidelayer" || command == "showlayer" || command == "locklayer" || command == "unlocklayer" ) && args.Argc() == 3 ) {
		const std::string layer = args.Argv(2);
		if ( command == "hidelayer" ) { s.view.hiddenLayers.insert( layer ); }
		else if ( command == "showlayer" ) { s.view.hiddenLayers.erase( layer ); }
		else if ( command == "locklayer" ) { s.view.lockedLayers.insert( layer ); }
		else { s.view.lockedLayers.erase( layer ); }
		s.selection.erase( std::remove_if( s.selection.begin(), s.selection.end(), [&]( const SelectionItem &item ) { return !s.Selectable( item ); } ), s.selection.end() );
		result = true;
	} else if ( command == "rename" && args.Argc() == 4 ) {
		std::size_t updated = 0;
		result = s.editor.Rename( document, id, args.Argv(3), updated, error );
		if ( result ) { common->Printf( "editor renamed: references=%zu\n", updated ); }
	} else if ( command == "refs" && args.Argc() == 3 ) {
		for ( const auto &reference : s.editor.References( document, args.Argv(2) ) ) {
			common->Printf( "editor reference: %llu %s\n", static_cast<unsigned long long>( reference.entity ), reference.key.c_str() );
		}
		return;
	} else if ( command == "prefab" && args.Argc() >= 4 && idStr::Cmp( args.Argv(2), "save" ) == 0 && args.Argc() == 4 ) {
		std::string path, text;
		if ( PrefabPath( args.Argv(3), path, error ) && s.editor.ExportPrefab( document, s.selection, text, error ) ) {
			const std::string staged = path + ".editor-tmp";
			result = editorStorage.WriteAndSync( staged, text, error ) && editorStorage.Promote( staged, path, error );
			if ( !result ) { editorStorage.Remove( staged ); }
		}
	} else if ( command == "prefab" && args.Argc() == 7 && idStr::Cmp( args.Argv(2), "insert" ) == 0 ) {
		std::string path, text;
		bool exists = false;
		Vec3 at;
		Selection created;
		if ( !ParseVec( args, 4, at ) ) { error = "prefab insert needs a position"; }
		else if ( PrefabPath( args.Argv(3), path, error ) && editorStorage.Read( path, text, exists, error ) ) {
			if ( !exists ) { error = "no prefab " + path; }
			else if ( ( result = s.editor.InsertPrefab( document, text, at, created, error ) ) ) { s.selection = created; }
		}
	} else if ( command == "validate" && args.Argc() == 2 ) { s.Validate(); PrintDiagnostics( s.diagnostics ); return; }
	else if ( command == "build" && ( args.Argc() == 2 || ( args.Argc() == 3 && idStr::Icmp( args.Argv(2), "noaas" ) == 0 ) ) ) {
		result = s.StartBuild( args.Argc() == 2, error );
	} else if ( command == "buildstatus" && args.Argc() == 2 ) {
		const MapBuild *build = s.Build();
		const char *states[] = { "idle", "running", "succeeded", "failed", "cancelled" };
		common->Printf( "editor build: state=%s stage=%s progress=%.2f current=%d diagnostics=%zu %s\n",
			build ? states[static_cast<int>( build->GetState() )] : "idle", build ? build->Stage().c_str() : "",
			build ? build->Progress() : 0.0f, s.BuildCurrent(), build ? build->Diagnostics().size() : 0, build ? build->Failure().c_str() : "" );
		return;
	} else if ( command == "cancel" && args.Argc() == 2 ) { s.CancelBuild(); result = true; }
	else if ( command == "play" && args.Argc() == 2 ) { result = s.Play( error ); }
	else if ( command == "playstatus" && args.Argc() == 2 ) {
		const bool playing = s.Playing();
		common->Printf( "editor play: running=%d lastExit=%d\n", playing, s.LastPlayExit() );
		return;
	} else if ( command == "index" && args.Argc() == 2 ) {
		s.StartAssetIndex();
		const auto &stats = s.assets.GetStats();
		common->Printf( "editor asset index: complete=%d entries=%zu bytes=%zu steps=%zu truncated=%d entityDefs=%zu materials=%zu models=%zu sounds=%zu maps=%zu\n",
			stats.complete, stats.entries, stats.bytes, stats.steps, stats.truncated, s.assets.Count( AssetIndex::EntityDef ), s.assets.Count( AssetIndex::Material ),
			s.assets.Count( AssetIndex::Model ), s.assets.Count( AssetIndex::Sound ), s.assets.Count( AssetIndex::Map ) );
		return;
	} else if ( command == "find" && args.Argc() == 4 ) {
		int kind = -1;
		for ( int k = 0; k < AssetIndex::KindCount; ++k ) { if ( !idStr::Icmp( args.Argv(2), AssetIndex::KindName( static_cast<AssetIndex::Kind>( k ) ) ) ) { kind = k; } }
		if ( kind < 0 ) { error = "unknown asset kind"; }
		else {
			for ( const auto &name : s.assets.Search( static_cast<AssetIndex::Kind>( kind ), args.Argv(3), 64 ) ) { common->Printf( "editor asset: %s\n", name.c_str() ); }
			return;
		}
	} else if ( command == "latency" && args.Argc() == 2 ) { s.PrintLatency(); return; }
	else if ( command == "outputs" && args.Argc() == 2 ) {
		// Compiler outputs beside the saved map, with content hashes, so a
		// failed or cancelled build can be shown to have left them alone.
		const std::string path = workspace.Path();
		if ( path.empty() ) { error = "the document has no saved path"; }
		else {
			const auto slash = path.rfind( '/' );
			const std::string directory = path.substr( 0, slash ), base = path.substr( slash + 1, path.size() - slash - 5 );
			int count = 0;
			for ( const auto &file : buildStorage.List( directory, base + "." ) ) {
				const std::string extension = file.substr( base.size() + 1 );
				if ( extension != "proc" && extension != "cm" && extension.compare( 0, 3, "aas" ) != 0 ) { continue; }
				std::string text;
				bool exists = false;
				if ( !buildStorage.Read( directory + "/" + file, text, exists, error ) || !exists ) { continue; }
				std::uint64_t hash = 1469598103934665603ULL;
				for ( const unsigned char c : text ) { hash = ( hash ^ c ) * 1099511628211ULL; }
				common->Printf( "editor output: %s/%s bytes=%zu fnv=%016llx\n", directory.c_str(), file.c_str(), text.size(), static_cast<unsigned long long>( hash ) );
				++count;
			}
			common->Printf( "editor outputs: %d\n", count );
			return;
		}
	}
	else { error = "invalid arguments; use 'editorExperimental help'"; }
	if ( !result ) { common->Printf( "editor rejected: %s\n", error.c_str() ); return; }
	s.PruneSelection();
	common->Printf( "editor %s: OK\n", command.c_str() );
	PrintStatus();
}

void EditorComplete( const idCmdArgs &args, void( *callback )( const char *s ) ) {
	static const char *commands[] = { "help", "new", "open", "close", "status", "entities", "get", "set", "unset", "create", "delete", "undo", "redo",
		"save", "savecopy", "autosave", "recover", "reload", "external", "select", "add", "none", "selection", "move", "rotate", "scale", "snap", "grid", "texlock",
		"brush", "patch", "duplicate", "remove", "group", "ungroup", "material", "layer", "layers", "hidelayer", "showlayer", "locklayer",
		"unlocklayer", "rename", "refs", "prefab", "validate", "build", "buildstatus", "cancel", "outputs", "play", "playstatus", "index", "find", "latency", NULL };
	if ( args.Argc() <= 2 ) {
		for ( int i = 0; commands[i]; ++i ) { callback( va( "%s %s", args.Argv(0), commands[i] ) ); }
	} else if ( idStr::Icmp( args.Argv(1), "open" ) == 0 ) {
		// The completion helper consumes argv[1] as the path. Preserve the
		// two-word command as a single token rather than completing "open".
		idCmdArgs mapArgs;
		mapArgs.AppendArg( va( "%s open", args.Argv(0) ) );
		mapArgs.AppendArg( args.Argv(2) );
		cmdSystem->ArgCompletion_FolderExtension( mapArgs, callback, "maps/", true, ".map", NULL );
	}
}

}

void LevelEditor_InitCommands() {
	// The legacy editor command remains owned by Radiant in tool-enabled builds.
	cmdSystem->AddCommand( "mapEdit", EditorCommand, CMD_FL_TOOL, "experimental map authoring document service", EditorComplete );
	cmdSystem->AddCommand( "editorExperimental", EditorCommand, CMD_FL_TOOL, "opens the experimental level editor", EditorComplete );
}

void LevelEditor_Frame() {
	if ( editorSession ) { editorSession->Frame(); }
	LevelEditor_UIFrame();
	if ( !editorSession || !editorSession->Document().IsDirty() || editorExperimental_autoSaveSeconds.GetInteger() <= 0 ) { return; }
	const unsigned int now = static_cast<unsigned int>( Sys_Milliseconds() );
	if ( now - lastRecoveryCheck < static_cast<unsigned int>( editorExperimental_autoSaveSeconds.GetInteger() ) * 1000U ) { return; }
	lastRecoveryCheck = now;
	std::string error;
	if ( !editorSession->Workspace().AutoSave( error ) ) { common->Printf( "editor recovery failed: %s\n", error.c_str() ); }
}

void LevelEditor_Shutdown() {
	LevelEditor_UIShutdown();
	if ( !editorSession ) { return; }
	editorSession->Shutdown();
	std::string error;
	if ( !editorSession->Workspace().AutoSave( error ) ) { common->Printf( "editor shutdown recovery failed: %s\n", error.c_str() ); }
	editorSession.reset();
}
