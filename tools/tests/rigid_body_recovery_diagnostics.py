#!/usr/bin/env python3
"""Regression checks for recovered rigid-body diagnostic severity."""

from __future__ import annotations

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)
).resolve()
RIGID_BODY_SOURCES = (
    "src/game/physics/Physics_RigidBody.cpp",
    "src/game/physics/Physics_RigidBody.cpp",
)

DROP_DIAGNOSTIC = (
    "rigid body rest heuristic did not settle entity '%s' type '%s' at (%s); "
    "forcing rest\\n"
)
INERTIA_DIAGNOSTIC = (
    "idPhysics_RigidBody::SetClipModel: clamping unbalanced inertia tensor for "
    "entity '%s' type '%s'\\n"
)


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject_regex(text: str, pattern: str, context: str) -> None:
    if re.search(pattern, text):
        raise AssertionError(f"Unexpected pattern {pattern!r} in {context}")


def require_order(text: str, needles: tuple[str, ...], context: str) -> None:
    cursor = -1
    for needle in needles:
        index = text.find(needle, cursor + 1)
        if index < 0:
            raise AssertionError(f"Missing {needle!r} in {context}")
        if index <= cursor:
            raise AssertionError(f"Expected ordered token {needle!r} in {context}")
        cursor = index


def function_body(source: str, signature: str, context: str) -> str:
    signature_index = source.find(signature)
    if signature_index < 0:
        raise AssertionError(f"Missing function signature {signature!r} in {context}")

    opening_brace = source.find("{", signature_index + len(signature))
    if opening_brace < 0:
        raise AssertionError(f"Missing function body for {signature!r} in {context}")

    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1 : index]

    raise AssertionError(f"Unbalanced function body for {signature!r} in {context}")


def block_body(text: str, prefix: str, context: str) -> str:
    prefix_index = text.find(prefix)
    if prefix_index < 0:
        raise AssertionError(f"Missing block prefix {prefix!r} in {context}")

    opening_brace = text.find("{", prefix_index + len(prefix))
    if opening_brace < 0:
        raise AssertionError(f"Missing block body after {prefix!r} in {context}")

    depth = 0
    for index in range(opening_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening_brace + 1 : index]

    raise AssertionError(f"Unbalanced block after {prefix!r} in {context}")


def validate_rigid_body_source(relative_path: str) -> None:
    source_path = GAME_LIBS_ROOT / relative_path
    source = read(source_path)

    drop_function = function_body(
        source,
        "void idPhysics_RigidBody::DropToFloorAndRest( void )",
        relative_path,
    )
    drop_recovery = block_body(
        drop_function,
        "if ( tr.fraction == 0.0f )",
        f"{relative_path} drop-to-floor recovery",
    )

    require(drop_recovery, "gameLocal.DPrintf(", f"{relative_path} drop diagnostic")
    require(drop_recovery, DROP_DIAGNOSTIC, f"{relative_path} drop diagnostic wording")
    reject_regex(
        drop_recovery,
        r"gameLocal\s*\.\s*DWarning\s*\(",
        f"{relative_path} recovered drop condition",
    )
    require_order(
        drop_recovery,
        (DROP_DIAGNOSTIC, "Rest();", "dropToFloor = false;"),
        f"{relative_path} recovered drop ordering",
    )

    # Starting in solid and escaping the world are not recovered placement
    # heuristics; retain their warning severity.
    require(
        drop_function,
        'gameLocal.DWarning( "rigid body in solid for entity',
        f"{relative_path} in-solid warning",
    )
    require(
        drop_function,
        'gameLocal.Warning( "rigid body outside world bounds for entity',
        f"{relative_path} outside-world warning",
    )

    clip_function = function_body(
        source,
        "void idPhysics_RigidBody::SetClipModel( idClipModel *model, const float density, int id, bool freeOld )",
        relative_path,
    )
    inertia_recovery = block_body(
        clip_function,
        "if ( inertiaScale[0][0] > MAX_INERTIA_SCALE",
        f"{relative_path} inertia recovery",
    )

    require(inertia_recovery, "gameLocal.DPrintf(", f"{relative_path} inertia diagnostic")
    require(inertia_recovery, INERTIA_DIAGNOSTIC, f"{relative_path} inertia diagnostic wording")
    reject_regex(
        inertia_recovery,
        r"gameLocal\s*\.\s*DWarning\s*\(",
        f"{relative_path} recovered inertia condition",
    )
    require_order(
        inertia_recovery,
        (
            INERTIA_DIAGNOSTIC,
            "float min = inertiaTensor[minIndex][minIndex] * MAX_INERTIA_SCALE;",
            "inertiaTensor *= inertiaScale;",
        ),
        f"{relative_path} inertia clamp ordering",
    )

    require(
        clip_function,
        'gameLocal.Warning( "idPhysics_RigidBody::SetClipModel: invalid mass for entity',
        f"{relative_path} invalid-mass warning",
    )


def validate_runner_wiring() -> None:
    validator = read(ROOT / "tools" / "validation" / "openq4_validate.py")
    require(
        validator,
        'root / "tools" / "tests" / "rigid_body_recovery_diagnostics.py",',
        "validation runner",
    )


def main() -> None:
    for relative_path in RIGID_BODY_SOURCES:
        validate_rigid_body_source(relative_path)
    validate_runner_wiring()
    print("rigid_body_recovery_diagnostics: ok")


if __name__ == "__main__":
    main()
