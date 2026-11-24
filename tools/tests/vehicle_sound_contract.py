#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static contract checks for occupied vehicle looping sounds."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOTS = ("src/game",)


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing function {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    if opening < 0:
        raise AssertionError(f"Missing body for {signature!r} in {context}")

    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"Unterminated body for {signature!r} in {context}")


def require_order(body: str, needles: tuple[str, ...], context: str) -> None:
    cursor = -1
    for needle in needles:
        index = body.find(needle, cursor + 1)
        if index < 0:
            raise AssertionError(f"Missing ordered token {needle!r} in {context}")
        cursor = index


def normalized(body: str) -> str:
    return " ".join(body.split())


def check_source_root(source_root: str) -> dict[str, str]:
    position_path = f"{source_root}/vehicle/VehiclePosition.cpp"
    parts_path = f"{source_root}/vehicle/VehicleParts.cpp"
    position = read(position_path)
    parts = read(parts_path)

    init = function(position, "void rvVehiclePosition::Init ( rvVehicle* parent, const idDict& args )", position_path)
    require_order(
        init,
        (
            'mSoundMaxSpeed = args.GetFloat ( "maxsoundspeed", "0" );',
            'if ( *args.GetString ( "snd_loop", "" ) )',
            "mSoundPart = AddPart ( rvVehicleSound::GetClassType(), args );",
            "static_cast<rvVehicleSound*>(mParts[mSoundPart])->SetAutoActivate ( false );",
            "SelectWeapon ( 0 );",
        ),
        f"{source_root} vehicle-position loop setup",
    )
    reject(init, "vehicle sound loops cause a crash", f"{source_root} stale loop disable")

    post_physics = function(parts, "void rvVehicleSound::RunPostPhysics ( void )", parts_path)
    require(post_physics, "Update ( );", f"{source_root} moving emitter update")
    reject(post_physics, "//Update", f"{source_root} disabled moving emitter update")

    play = function(parts, "void rvVehicleSound::Play ( void )", parts_path)
    require_order(
        play,
        (
            "soundSystem->EmitterForIndex",
            "refSound.referenceSoundHandle = soundSystem->AllocSoundEmitter( SOUNDWORLD_GAME );",
            "Update ( true );",
            "emitter->StartSound",
        ),
        f"{source_root} vehicle sound allocation and start",
    )

    destructor = function(parts, "rvVehicleSound::~rvVehicleSound ( void )", parts_path)
    require_order(
        destructor,
        (
            "Stop();",
            "soundSystem->FreeSoundEmitter( SOUNDWORLD_GAME, refSound.referenceSoundHandle, true );",
            "refSound.referenceSoundHandle = -1;",
        ),
        f"{source_root} vehicle sound cleanup",
    )

    return {
        "init": normalized(init),
        "post_physics": normalized(post_physics),
    }


def main() -> None:
    # one game tree since CORE-07: there is no second copy to drift from
    for source_root in SOURCE_ROOTS:
        check_source_root(source_root)

    # Suite membership and CI execution are checked by game_regression_suite.py.
    print("vehicle_sound_contract: ok")


if __name__ == "__main__":
    main()
