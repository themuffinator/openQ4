#!/usr/bin/env python3
"""Regression checks for the weapon wheel's transient slow-motion channel."""

from __future__ import annotations

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")).resolve()


def read(path: Path) -> str:
    data = path.read_bytes()
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("cp1252")


def require(source: str, token: str, context: str) -> None:
    if token not in source:
        raise AssertionError(f"Missing {token!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"Could not find the end of {signature!r}")


def main() -> None:
    engine_header = read(ROOT / "src/framework/Common.h")
    game_header = read(GAME_ROOT / "src/framework/Common.h")
    if engine_header != game_header:
        raise AssertionError("Engine and GameLib Common.h copies must remain identical")

    require(engine_header, "SetGameTimeScale(float scale)", "idCommon transient time-scale API")
    require(engine_header, "GetGameTimeScale(void) const", "idCommon transient time-scale API")

    common = read(ROOT / "src/framework/Common.cpp")
    setter = function_body(common, "void idCommonLocal::SetGameTimeScale( float scale ) {")
    require(setter, "openQ4_gameTimeScale.store", "atomic transient time-scale publication")
    require(setter, "soundWorld->SetSlowmoSpeed( scale );", "transient audio slowdown")
    require(
        common,
        "com_timescale.GetFloat() * openQ4_gameTimeScale.load",
        "engine effective time-scale composition",
    )

    player = read(GAME_ROOT / "src/game/Player.cpp")
    if re.search(r"SetCVar(?:Float|String|Integer)\s*\(\s*\"timescale\"", player):
        raise AssertionError("The weapon wheel must not write the public timescale CVar")
    if "weaponWheelBaseTimescale" in player or "weaponWheelBaseTimescale" in read(
        GAME_ROOT / "src/game/Player.h"
    ):
        raise AssertionError("The weapon wheel must not cache ownership of the public timescale CVar")

    effects = function_body(player, "void idPlayer::UpdateWeaponWheelEffects( void ) {")
    require(effects, "common->SetGameTimeScale", "weapon-wheel transient time scale")
    require(effects, "WeaponWheelLerp( 1.0f, WEAPON_WHEEL_TIMESCALE_SCALE, blend )", "wheel blend")
    reset = function_body(player, "void idPlayer::ResetWeaponWheel( bool instantRestore ) {")
    require(reset, "common->SetGameTimeScale( 1.0f );", "weapon-wheel transient reset")
    handle_esc = function_body(player, "bool idPlayer::HandleESC( void ) {")
    require(handle_esc, "ResetWeaponWheel( true );", "Escape transient reset")

    adjust_speed = function_body(player, "void idPlayer::AdjustSpeed( void ) {")
    require(
        adjust_speed,
        "!physicsObj.OnLadder() && ( usercmd.buttons & BUTTON_RUN )",
        "single-player walk/run selection",
    )

    game_local = read(GAME_ROOT / "src/game/Game_local.cpp")
    run_frame = function_body(game_local, "gameReturn_t idGameLocal::RunFrame(")
    require(run_frame, "cvarSystem->GetCVarFloat( \"timescale\" ) * common->GetGameTimeScale()", "SP simulation scale")

    game_api = read(GAME_ROOT / "src/game/Game.h")
    require(game_api, "const int GAME_API_VERSION\t\t= 47;", "updated engine/GameLib ABI")

    print("single-player time-scale and walking CVar checks passed")


if __name__ == "__main__":
    main()
