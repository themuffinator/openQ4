#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static contract checks for pushers that move further than a trace can sweep."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOTS = ("src/game",)
TEST_PATH = "tools/tests/push_teleport_contract.py"
LIVE_PUSH_MARKER = "#else /* !NEW_PUSH */"
CLIP_TRANSLATIONAL_PUSH = "float idPush::ClipTranslationalPush( trace_t &results, idEntity *pusher, const int flags,"
TELEPORT_GUARD = "if ( !( flags & PUSHFL_CLIP ) && translation.LengthSqr() > Square( CM_MAX_TRACE_DIST ) ) {"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


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


def check_source_root(source_root: str) -> str:
    header_path = f"{source_root}/physics/Push.h"
    require(read(header_path), "//#define NEW_PUSH", f"{header_path} (the !NEW_PUSH pusher is the compiled one)")

    push_path = f"{source_root}/physics/Push.cpp"
    push = read(push_path)
    live = push.find(LIVE_PUSH_MARKER)
    if live < 0:
        raise AssertionError(f"Missing {LIVE_PUSH_MARKER!r} in {push_path}")

    # A translation the collision system refuses to sweep used to reach TestHugeTranslation,
    # which asserts and reports a fake collision that blocked the pusher's whole bind team
    # (the mcc_landing ship's func_movers snapping into the flyby animation).
    body = function(push[live:], CLIP_TRANSLATIONAL_PUSH, f"{push_path} (!NEW_PUSH)")
    require_order(
        body,
        (
            "if ( translation == vec3_origin ) {",
            TELEPORT_GUARD,
            "return totalMass;",
            "pushBounds.FromBoundsTranslation(",
            "gameLocal.EntitiesTouchingBounds(",
            "if ( flags & PUSHFL_CLIP ) {",
            "gameLocal.Translation(",
            "TryTranslatePushEntity(",
        ),
        f"{source_root} teleport guard ahead of every swept push trace",
    )

    clip_path = f"{source_root}/physics/Clip.cpp"
    huge = function(read(clip_path), "ID_INLINE bool TestHugeTranslation(", clip_path)
    require(
        huge,
        "( end - start ).LengthSqr() > Square( CM_MAX_TRACE_DIST )",
        f"{clip_path} huge translation limit the teleport guard mirrors",
    )

    return normalized(body)


def main() -> None:
    # one game tree since CORE-07: there is no second copy to drift from
    for source_root in SOURCE_ROOTS:
        check_source_root(source_root)

    # Suite membership and CI execution are checked by game_regression_suite.py.
    print("push_teleport_contract: ok")


if __name__ == "__main__":
    main()
