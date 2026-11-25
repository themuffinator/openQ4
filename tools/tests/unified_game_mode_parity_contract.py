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
"""

from __future__ import annotations

import re
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


def main() -> int:
    check_campaign_saves()
    check_campaign_keys_stay_out_of_network_play()
    check_network_play_script_runtime()
    print("unified_game_mode_parity_contract: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
