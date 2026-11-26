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
cvar at a mode change either.
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
#include <cstddef>
#include <cstdio>
#include <map>
#include <string>
enum { CVAR_GAME = 1 << 0, CVAR_BOOL = 1 << 1, CVAR_ARCHIVE = 1 << 2, CVAR_SERVERINFO = 1 << 3, CVAR_STATIC = 1 << 30 };
typedef void ( *argCompletion_t )( void );
class idCVar;
// idInternalCVar::Update: a declaration's value becomes the reset value and
// its flags, CVAR_STATIC included, merge into the registered ones; a static
// declaration meeting a registration that is still static with another
// default is "declared multiple times with different initial value".
struct Registered { std::string reset; int flags = 0; int registrations = 0; };
struct CVarSystem {
	std::map<std::string, Registered> vars;
	int declaredTwice = 0;
	void Register( idCVar *cvar );
} cvars, *cvarSystem = &cvars;
class idCVar {
public:
	idCVar( const char *n, const char *v, int f, const char *, argCompletion_t = NULL ) : name( n ), value( v ), flags( f | CVAR_STATIC ) {}
	idCVar( const char *n, const char *v, int f, const char *, const char **, argCompletion_t = NULL ) : name( n ), value( v ), flags( f | CVAR_STATIC ) {}
	const char *Name() const { return name; }
	const char *Default() const { return value; }
	int Flags() const { return flags; }
	// as the engine's: on the registered copy
	void RemoveFlag( int flag ) { cvars.vars[ name ].flags &= ~flag; }
protected:
	const char *name;
	const char *value;
	int flags;
};
void CVarSystem::Register( idCVar *cvar ) {
	Registered &r = vars[ cvar->Name() ];
	if ( r.registrations > 0 && ( r.flags & CVAR_STATIC ) && r.reset != cvar->Default() ) {
		++declaredTwice;
	}
	r.reset = cvar->Default();
	r.flags |= cvar->Flags();
	++r.registrations;
}
@CLASS@
@DEFINITIONS@
idModeCVar pure( "si_pure", "0", CVAR_GAME | CVAR_SERVERINFO | CVAR_BOOL, "1", CVAR_GAME | CVAR_SERVERINFO | CVAR_BOOL, "pure" );
idModeCVar shape( "pm_usecylinder", "0", CVAR_GAME | CVAR_BOOL, "1", CVAR_GAME | CVAR_BOOL | CVAR_ARCHIVE, "shape" );
static bool staticListRegistered = false;
// one idGameLocal::Init: the mode, then idCVar::RegisterStaticVars, which
// registers the static list once per loaded image
static void Init( bool multiplayer ) {
	idModeCVar::ApplyModuleMode( multiplayer );
	if ( !staticListRegistered ) {
		cvarSystem->Register( &pure );
		cvarSystem->Register( &shape );
		staticListRegistered = true;
	}
}
// the late phase of idGameLocal::Shutdown, just before the engine unloads it
static void Unload() {
	idModeCVar::ReleaseModuleMode();
}
int main() {
	// a fresh image per mode: nothing changes for the first load
	Init( false );
	assert( cvars.vars[ "si_pure" ].reset == "0" && cvars.vars[ "pm_usecylinder" ].reset == "0" );
	assert( !( cvars.vars[ "pm_usecylinder" ].flags & CVAR_ARCHIVE ) );
	assert( cvars.vars[ "si_pure" ].registrations == 1 );
	Unload();
	// the platform kept the image loaded: the next Init serves network play
	Init( true );
	assert( cvars.vars[ "si_pure" ].reset == "1" && cvars.vars[ "pm_usecylinder" ].reset == "1" );
	assert( cvars.vars[ "pm_usecylinder" ].flags & CVAR_ARCHIVE );
	assert( cvars.vars[ "si_pure" ].registrations == 2 );
	Unload();
	// ...and the campaign again, with its own defaults
	Init( false );
	assert( cvars.vars[ "si_pure" ].reset == "0" && cvars.vars[ "pm_usecylinder" ].reset == "0" );
	assert( pure.Default() == std::string( "0" ) && !( pure.Flags() & CVAR_ARCHIVE ) );
	assert( cvars.vars[ "si_pure" ].registrations == 3 );
	// a mode's own default is never reported as a second declaration
	assert( cvars.declaredTwice == 0 );
	// ...while the registration is static again once a declaration arrived
	assert( cvars.vars[ "si_pure" ].flags & CVAR_STATIC );
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
    release = source.index("void idModeCVar::ReleaseModuleMode( void ) {", start)
    definitions = source[start:release] + "void idModeCVar::ReleaseModuleMode( void ) " + \
        function_body(source[release:], "void idModeCVar::ReleaseModuleMode(", "SysCvar.cpp")
    shutdown = read("src/game/Game_local.cpp").replace("\r\n", "\n")
    require(shutdown, "cvarSystem->RemoveFlaggedAutoCompletion( CVAR_GAME );\n\t\tidModeCVar::ReleaseModuleMode();",
            "idGameLocal's late shutdown releases the mode cvars")
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
