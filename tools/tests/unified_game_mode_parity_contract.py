#!/usr/bin/env python3
"""Each mode of the unified game module behaves as its old library did (CORE-07).

The single-player and multiplayer libraries were merged into one module whose
mode is fixed at load. Where only one old library had a behaviour, the merged
code gates it so the other mode keeps its own: the campaign's Awakening content
keys stay out of network play, network play keeps the multiplayer library's
script stack and warnings, and campaign saves keep the values and spellings the
single-player library wrote. A 25 November 2026 A/B of every official map
against the pre-merge build (docs/dev/unified-game-module.md) found each of
these; this check keeps the gates from being dropped by later edits.

The few cvars whose default or flags differ per mode (idModeCVar) must follow
the mode on every load, including when the platform keeps the loaded image
across a mode change (dyld never unloads an image with thread-local variables),
so the registration is also run as a fixture against a stand-in cvar system.
A mode's own default must never be reported as a second declaration of the
cvar at a mode change either. A pm_usecylinder value the player never set takes
the mode's default at each load (a config's archived default, or a server's
synced value, is not a choice), while one the player set is kept.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="latin-1")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def function_body(text: str, signature: str, context: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    brace = text.find("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:index + 1]
    raise AssertionError(f"Unterminated {signature!r} in {context}")


def check_campaign_saves() -> None:
    network = function_body(read("src/game/Game_network.cpp"), "void idGameLocal::InitAsyncNetwork(", "Game_network.cpp")
    assign = network.find("entityDefBits = idMath::BitsForInteger( declManager->GetNumDecls( DECL_ENTITYDEF ) ) + 1;")
    negate = network.find("if ( !IsMultiplayerMode() ) {\r\n\t\tentityDefBits = -entityDefBits;")
    if negate < 0:
        negate = network.find("if ( !IsMultiplayerMode() ) {\n\t\tentityDefBits = -entityDefBits;")
    if assign < 0 or negate < assign:
        raise AssertionError("InitAsyncNetwork must store the campaign's negated entityDefBits after the network value")
    require(read("src/game/Player.cpp"),
            'hud->SetStateInt( gameLocal.isMultiplayer ? "player_idealWeapon" : "player_idealweapon", hud_idealWeapon );',
            "Player.cpp")


def check_campaign_keys_stay_out_of_network_play() -> None:
    projectile = read("src/game/Projectile.cpp")
    require(projectile, 'impactEntity				= gameLocal.isMultiplayer ? spawnArgs.GetString("def_impactEntity","")',
            "Projectile.cpp")
    require(projectile, 'applyRotation		= gameLocal.isMultiplayer || spawnArgs.GetBool( "applyRotation", "1" );',
            "Projectile.cpp")
    force = function_body(projectile, "void idProjectile::ApplyImpactForce(", "Projectile.cpp")
    if force.find("if ( gameLocal.isMultiplayer ) {") < 0 or \
            force.find("if ( gameLocal.isMultiplayer ) {") > force.find('spawnArgs.GetFloat( "impactForce", "0" )'):
        raise AssertionError("ApplyImpactForce must return in network play before reading impactForce")
    require(read("src/game/Trigger.cpp"),
            'const float velScale = gameLocal.isMultiplayer ? 1.0f : spawnArgs.GetFloat( "velscale", "1" );',
            "Trigger.cpp")
    require(read("src/game/Game_local.cpp"),
            '} else if ( MATCH( "def_" ) || ( !IsMultiplayerMode() && !kv->GetKey().Icmp( "impactEntity" ) ) ) {',
            "Game_local.cpp")
    weapon = read("src/game/Weapon.cpp")
    require(weapon, "const bool altFlash = wsfl.zoom && !gameLocal.isMultiplayer;", "Weapon.cpp")
    for key in ("fx_altmuzzleflash", "fx_altmuzzleflash_world"):
        require(weapon, f'( altFlash && weaponDef->dict.GetString( "{key}" )[0] )', "Weapon.cpp")
    require(read("src/game/Entity.cpp"), "if ( originalClassname.Length() && !gameLocal.isMultiplayer ) {", "Entity.cpp")
    light = function_body(read("src/game/Light.cpp"), "bool idGameEdit::ParseSpawnArgsToRenderLight(", "Light.cpp")
    gate = light.find("if ( !gameLocal.IsMultiplayerMode() ) {")
    read_key = light.find('args->GetBool( "globalLight", "0", renderLight->globalLight );')
    if gate < 0 or read_key < gate:
        raise AssertionError("ParseSpawnArgsToRenderLight must read globalLight only in the campaign")


def check_network_play_script_runtime() -> None:
    header = read("src/game/script/Script_Interpreter.h")
    require(header, "#define LOCALSTACK_SIZE_MULTIPLAYER	6144", "Script_Interpreter.h")
    for push in ("void idInterpreter::Push(", "void idInterpreter::PushVector(", "void idInterpreter::PushString("):
        body = function_body(header, push, "Script_Interpreter.h")
        if "> localstackLimit" not in body or "LOCALSTACK_SIZE" in body:
            raise AssertionError(f"{push} must check the mode's localstackLimit")
    interpreter = read("src/game/script/Script_Interpreter.cpp")
    require(function_body(interpreter, "idInterpreter::idInterpreter()", "Script_Interpreter.cpp"),
            "localstackLimit = gameLocal.IsMultiplayerMode() ? LOCALSTACK_SIZE_MULTIPLAYER : LOCALSTACK_SIZE;",
            "idInterpreter::idInterpreter")
    require(function_body(interpreter, "void idInterpreter::EnterFunction(", "Script_Interpreter.cpp"),
            "if ( localstackUsed + c > localstackLimit ) {", "idInterpreter::EnterFunction")
    require(interpreter,
            "const bool warnName = gameLocal.isMultiplayer || ( lastVarName != NULL && lastVarName[0] == '$' );",
            "Script_Interpreter.cpp")
    program = function_body(read("src/game/script/Script_Program.cpp"), "bool idProgram::CompileText(",
                            "Script_Program.cpp")
    require(program, "for( i = 0; gameLocal.IsMultiplayerMode() && i < varDefs.Num(); i++ ) {",
            "idProgram::CompileText")
    if re.search(r"LOCALSTACK_SIZE\s*\(\s*6144\s*\*\s*2\s*\)", header) is None:
        raise AssertionError("the campaign keeps its 12288-byte locals stack")


MODE_CVAR_HARNESS = r"""
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
enum { CVAR_GAME = 1 << 0, CVAR_BOOL = 1 << 1, CVAR_ARCHIVE = 1 << 2, CVAR_SERVERINFO = 1 << 3,
	CVAR_NETWORKSYNC = 1 << 4, CVAR_MODIFIED = 1 << 5, CVAR_STATIC = 1 << 30 };
typedef void ( *argCompletion_t )( void );
struct idStr {
	static int Cmp( const char *a, const char *b ) { return std::strcmp( a, b ); }
	static int Icmp( const char *a, const char *b ) {
		for ( ; *a && std::tolower( (unsigned char)*a ) == std::tolower( (unsigned char)*b ); a++, b++ ) {}
		return std::tolower( (unsigned char)*a ) - std::tolower( (unsigned char)*b );
	}
};
struct idDict {
	std::map<std::string, std::string> values;
	void Set( const char *key, const char *value ) { values[ key ] = value; }
};
struct Common { void DPrintf( const char *, ... ) {} } commonObject, *common = &commonObject;
struct GameLocal { bool multiplayer = false; bool IsMultiplayerMode() const { return multiplayer; } } gameLocal;
class idCVar;
// The engine's registered copies (idInternalCVar); a new one starts modified.
// idInternalCVar::Update: a declaration's value becomes the reset value, its
// flags (CVAR_STATIC included) merge in and the value stays; a static
// declaration meeting a copy that is still static with another default is
// "declared multiple times with different initial value".
struct Registered { std::string reset, value; int flags = 0; int registrations = 0; };
struct CVarSystem {
	std::map<std::string, Registered> vars;
	std::map<std::string, idCVar *> handles;
	int declaredTwice = 0;
	void Register( idCVar *cvar );
	idCVar *Find( const char *name );
	// the console, a cfg, a server this process hosts
	void Set( const char *name, const char *value ) {
		Registered &r = vars[ name ];
		if ( r.value != value ) {
			r.value = value;
			r.flags |= CVAR_MODIFIED;
		}
	}
	bool SetCVarsFromDictByFlags( const idDict &dict, int ) {
		for ( const auto &kv : dict.values ) {
			Set( kv.first.c_str(), kv.second.c_str() );
		}
		return true;
	}
	// a config's seta (CVAR_ARCHIVE) or a command-line set before the first load
	void Create( const char *name, const char *value, int flags ) {
		Registered &r = vars[ name ];
		r.value = r.reset = value;
		r.flags = flags | CVAR_MODIFIED;
	}
} cvars, *cvarSystem = &cvars;
class idCVar {
public:
	idCVar( const char *n, const char *v, int f, const char *, argCompletion_t = NULL ) : name( n ), value( v ), flags( f | CVAR_STATIC ) {}
	idCVar( const char *n, const char *v, int f, const char *, const char **, argCompletion_t = NULL ) : name( n ), value( v ), flags( f | CVAR_STATIC ) {}
	explicit idCVar( const char *n ) : name( n ), value( "" ), flags( 0 ) {}
	const char *Name() const { return name; }
	const char *Default() const { return value; }
	int Flags() const { return flags; }
	// as the engine's: on the registered copy
	const char *GetName() const { return name; }
	const char *GetString() const { return cvars.vars[ name ].value.c_str(); }
	int GetFlags() const { return cvars.vars[ name ].flags; }
	bool IsModified() const { return ( GetFlags() & CVAR_MODIFIED ) != 0; }
	void ClearModified() { cvars.vars[ name ].flags &= ~CVAR_MODIFIED; }
	void SetString( const char *v ) { cvars.Set( name, v ); }
	void RemoveFlag( int flag ) { cvars.vars[ name ].flags &= ~flag; }
protected:
	const char *name;
	const char *value;
	int flags;
};
void CVarSystem::Register( idCVar *cvar ) {
	auto found = vars.find( cvar->Name() );
	if ( found == vars.end() ) {
		Registered &r = vars[ cvar->Name() ];
		r.value = r.reset = cvar->Default();
		r.flags = cvar->Flags() | CVAR_MODIFIED;
		r.registrations = 1;
		return;
	}
	Registered &r = found->second;
	if ( r.registrations > 0 && ( r.flags & CVAR_STATIC ) && r.reset != cvar->Default() ) {
		++declaredTwice;
	}
	r.reset = cvar->Default();
	r.flags |= cvar->Flags();
	++r.registrations;
}
idCVar *CVarSystem::Find( const char *name ) {
	if ( vars.find( name ) == vars.end() ) {
		return NULL;
	}
	idCVar *&handle = handles[ name ];
	if ( handle == NULL ) {
		handle = new idCVar( name );
	}
	return handle;
}
@CLASS@
@DEFINITIONS@
idModeCVar pure( "si_pure", "0", CVAR_GAME | CVAR_SERVERINFO | CVAR_BOOL, "1", CVAR_GAME | CVAR_SERVERINFO | CVAR_BOOL, "pure" );
idModeCVar shape( "pm_usecylinder", "0", CVAR_GAME | CVAR_NETWORKSYNC, "1", CVAR_GAME | CVAR_NETWORKSYNC | CVAR_ARCHIVE, "shape" );
static bool staticListRegistered = false;
// one idGameLocal::Init: the mode, idCVar::RegisterStaticVars (once per loaded
// image), then the mode defaults
static void Init( bool multiplayer ) {
	gameLocal.multiplayer = multiplayer;
	idModeCVar::ApplyModuleMode( multiplayer );
	if ( !staticListRegistered ) {
		cvarSystem->Register( &pure );
		cvarSystem->Register( &shape );
		staticListRegistered = true;
	}
	idModeCVar::ApplyModeDefaults();
}
// the late phase of idGameLocal::Shutdown, just before the engine unloads it
static void Unload() {
	idModeCVar::ReleaseModuleMode();
}
static std::string Value( const char *name ) { return cvars.vars[ name ].value; }
// a new process: the cvar system starts empty (the image stays, as dyld keeps it)
static void NewProcess() { cvars.vars.clear(); }
int main() {
	// a fresh process loads the campaign
	Init( false );
	assert( cvars.vars[ "si_pure" ].reset == "0" && cvars.vars[ "pm_usecylinder" ].reset == "0" );
	assert( !( cvars.vars[ "pm_usecylinder" ].flags & CVAR_ARCHIVE ) );
	assert( cvars.vars[ "si_pure" ].registrations == 1 );
	Unload();
	// the platform kept the image loaded: the next Init serves network play,
	// and a shape the player never set takes network play's cylinder
	Init( true );
	assert( cvars.vars[ "si_pure" ].reset == "1" && cvars.vars[ "pm_usecylinder" ].reset == "1" );
	assert( cvars.vars[ "pm_usecylinder" ].flags & CVAR_ARCHIVE );
	assert( cvars.vars[ "si_pure" ].registrations == 2 );
	assert( Value( "pm_usecylinder" ) == "1" );
	// si_pure keeps its value: the Create Server page shows and sets it
	assert( Value( "si_pure" ) == "0" );
	Unload();
	// ...and the campaign again, with its own defaults and its box
	Init( false );
	assert( cvars.vars[ "si_pure" ].reset == "0" && cvars.vars[ "pm_usecylinder" ].reset == "0" );
	assert( pure.Default() == std::string( "0" ) && !( pure.Flags() & CVAR_ARCHIVE ) );
	assert( cvars.vars[ "si_pure" ].registrations == 3 );
	assert( Value( "pm_usecylinder" ) == "0" );
	// a mode's own default is never reported as a second declaration
	assert( cvars.declaredTwice == 0 );
	// ...while the registration is static again once a declaration arrived
	assert( cvars.vars[ "si_pure" ].flags & CVAR_STATIC );
	Unload();

	// a value the player sets is the player's, in both modes from then on
	Init( true );
	cvarSystem->Set( "pm_usecylinder", "0" );
	Unload();
	Init( false );
	assert( Value( "pm_usecylinder" ) == "0" );
	Unload();
	Init( true );
	assert( Value( "pm_usecylinder" ) == "0" );
	Unload();

	// a client session: the server set the value, so the mode's default returns
	NewProcess();
	Init( true );
	idModeCVar::NoteClientSession();
	idDict synced;
	synced.Set( "pm_usecylinder", "8" );
	cvarSystem->SetCVarsFromDictByFlags( synced, CVAR_NETWORKSYNC );
	Unload();
	Init( false );
	assert( Value( "pm_usecylinder" ) == "0" );
	Unload();
	Init( true );
	assert( Value( "pm_usecylinder" ) == "1" );
	Unload();

	// a config archived the campaign's box from a session started at the menu
	// (before this rule): that is a mode's default, not a choice
	NewProcess();
	cvarSystem->Create( "pm_usecylinder", "0", CVAR_ARCHIVE );
	Init( false );
	assert( Value( "pm_usecylinder" ) == "0" );
	Unload();
	Init( true );
	assert( Value( "pm_usecylinder" ) == "1" );
	Unload();
	// ...and network play's archived cylinder does not reach the campaign
	NewProcess();
	cvarSystem->Create( "pm_usecylinder", "1", CVAR_ARCHIVE );
	Init( false );
	assert( Value( "pm_usecylinder" ) == "0" );
	Unload();

	// any other archived value, and the command line, are the player's
	NewProcess();
	cvarSystem->Create( "pm_usecylinder", "12", CVAR_ARCHIVE );
	Init( false );
	assert( Value( "pm_usecylinder" ) == "12" );
	Unload();
	Init( true );
	assert( Value( "pm_usecylinder" ) == "12" );
	Unload();
	NewProcess();
	cvarSystem->Create( "pm_usecylinder", "0", 0 );
	Init( true );
	assert( Value( "pm_usecylinder" ) == "0" );
	Unload();
	std::puts( "mode cvars across loads of one image: PASS" );
	return 0;
}
"""


def check_mode_cvars_follow_every_load() -> None:
    header = read("src/game/gamesys/SysCvar.h").replace("\r\n", "\n")
    source = read("src/game/gamesys/SysCvar.cpp").replace("\r\n", "\n")
    begin = header.index("class idModeCVar : public idCVar {")
    declaration = header[begin:header.index("\n};", begin) + 3]
    start = source.index("idModeCVar *idModeCVar::modeCVars = NULL;")
    last = source.index("void idModeCVar::NoteClientSession( void ) {", start)
    definitions = source[start:last] + "void idModeCVar::NoteClientSession( void ) " + \
        function_body(source[last:], "void idModeCVar::NoteClientSession(", "SysCvar.cpp")
    shutdown = read("src/game/Game_local.cpp").replace("\r\n", "\n")
    require(shutdown, "cvarSystem->RemoveFlaggedAutoCompletion( CVAR_GAME );\n\t\tidModeCVar::ReleaseModuleMode();",
            "idGameLocal's late shutdown releases the mode cvars")
    require(shutdown, "idCVar::RegisterStaticVars();\n\t// a mode cvar the player never set takes this mode's default\n"
            "\tidModeCVar::ApplyModeDefaults();",
            "idGameLocal::Init gives the mode cvars the mode's defaults after registering them")
    require(shutdown, "this->isClient = isClient;\n\tif ( isClient ) {\n\t\tidModeCVar::NoteClientSession();",
            "a client or demo map notes its session for the mode cvars")
    network = read("src/game/Game_network.cpp").replace("\r\n", "\n")
    require(network, "isClient = true;\n\tidModeCVar::NoteClientSession();", "InitLocalClient notes its session")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mode-cvars-", dir=ROOT / ".tmp") as name:
        directory = Path(name)
        cpp, exe = directory / "fixture.cpp", directory / ("fixture.exe" if os.name == "nt" else "fixture")
        cpp.write_text(MODE_CVAR_HARNESS.replace("@CLASS@", declaration).replace("@DEFINITIONS@", definitions),
                       encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-shadow", str(cpp), "-o", str(exe)],
                       cwd=directory, check=True)
        result = subprocess.run([str(exe)], cwd=directory, capture_output=True, text=True)
        if result.returncode != 0:
            raise AssertionError(f"mode cvar fixture failed: {result.stdout}{result.stderr}")
        print(result.stdout, end="")


def main() -> int:
    check_campaign_saves()
    check_campaign_keys_stay_out_of_network_play()
    check_network_play_script_runtime()
    check_mode_cvars_follow_every_load()
    print("unified_game_mode_parity_contract: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
