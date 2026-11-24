#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Contract checks for the multiplayer damage-number (plum) feature.

Two properties are pinned here.

1. It is MULTIPLAYER ONLY, and not by convention: single player loads game-sp,
   which links no part of mp/HitFeedback.cpp and registers no hud_damageNumber*
   cvar.  Mirroring any of it into src/game/ would put a multiplayer HUD feature
   into the campaign, so the SP tree is asserted to be clean and each entry
   point inside game-mp carries its own gameLocal.isMultiplayer gate.

2. The wire message is written and read in the same field order.  It is an
   unreliable message with no self-describing framing, so a one-sided edit
   silently produces garbage damage values rather than a build error.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MP_CVARS = ROOT / "src/game/gamesys/SysCvar.cpp"
MP_CVARS_HEADER = ROOT / "src/game/gamesys/SysCvar.h"
HITFEEDBACK = ROOT / "src/game/mp/HitFeedback.cpp"
PLAYERVIEW = ROOT / "src/game/PlayerView.cpp"
SP_TREE = ROOT / "src/game"

SP_FORBIDDEN = (
    "hud_damageNumbers",
    "hud_damageNumberStyle",
    "hud_damageNumberScale",
    "rvDamageNumbers",
    "GAME_UNRELIABLE_MESSAGE_HITINFO",
)


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def body(text: str, signature: str) -> str:
    """Everything from a function signature to the first column-zero closing brace."""
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"missing {signature!r}")
    end = text.find("\n}", start)
    if end < 0:
        raise AssertionError(f"could not delimit {signature!r}")
    return text[start:end]


def validate_defaults(cvars: str, header: str) -> None:
    declaration = re.search(
        r'idCVar\s+hud_damageNumbers\(\s*"hud_damageNumbers",\s*"(?P<default>[^"]*)",\s*'
        r'(?P<flags>[^,]+),\s*"(?P<help>[^"]*)",\s*(?P<low>\d+),\s*(?P<high>\d+)\s*\)',
        cvars,
    )
    if declaration is None:
        raise AssertionError("could not parse the hud_damageNumbers declaration")

    if declaration.group("default") != "1":
        raise AssertionError(
            "hud_damageNumbers ships enabled for opponents (default \"1\"); "
            f"found {declaration.group('default')!r}"
        )
    if (declaration.group("low"), declaration.group("high")) != ("0", "2"):
        raise AssertionError("hud_damageNumbers must stay clamped to 0..2")

    flags = declaration.group("flags")
    for flag in ("CVAR_GAME", "CVAR_ARCHIVE", "CVAR_INTEGER"):
        if flag not in flags:
            raise AssertionError(f"hud_damageNumbers must carry {flag}")

    for token in ("hud_damageNumberStyle", "hud_damageNumberScale"):
        require(cvars, f'idCVar {token}(', "damage number cvar block")
        require(header, f"extern idCVar\t{token};", "SysCvar.h")


def validate_single_player_has_none() -> None:
    if not SP_TREE.is_dir():
        raise AssertionError(f"single player source tree not found: {SP_TREE}")

    offenders: list[str] = []
    for path in sorted(SP_TREE.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in (".cpp", ".h"):
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for token in SP_FORBIDDEN:
            if token in text:
                offenders.append(f"{path.relative_to(ROOT)}: {token}")

    if offenders:
        raise AssertionError(
            "damage numbers are a multiplayer-only feature and must not appear in the "
            "single player tree:\n  " + "\n  ".join(offenders)
        )

    for stray in ("HitFeedback.cpp", "HitFeedback.h"):
        for path in SP_TREE.rglob(stray):
            raise AssertionError(f"{path.relative_to(ROOT)} must not exist in the SP tree")


def validate_multiplayer_gates(hitfeedback: str, playerview: str) -> None:
    for signature, context in (
        ("void rvDamageNumbers::Add(", "rvDamageNumbers::Add"),
        ("void rvDamageNumbers::Draw(", "rvDamageNumbers::Draw"),
        ("void rvDamageNumbers::ServerSend(", "rvDamageNumbers::ServerSend"),
    ):
        require(body(hitfeedback, signature), "gameLocal.isMultiplayer", context)

    # The one draw call site is itself guarded, so a single player view never
    # even reaches the subsystem.
    call = playerview.find("damageNumbers.Draw( view )")
    if call < 0:
        raise AssertionError("missing the damage number draw call in idPlayerView")
    if "gameLocal.isMultiplayer" not in playerview[max(0, call - 400):call]:
        raise AssertionError(
            "the damageNumbers.Draw call in idPlayerView must sit under a "
            "gameLocal.isMultiplayer guard"
        )

    # The server never sends what the server operator has switched off.
    require(body(hitfeedback, "void rvDamageNumbers::ServerSend("),
            "g_hitFeedback.GetInteger() <= 0", "rvDamageNumbers::ServerSend")


def validate_wire_symmetry(hitfeedback: str) -> None:
    send = body(hitfeedback, "void rvDamageNumbers::ServerSend(")
    receive = body(hitfeedback, "void rvDamageNumbers::ClientReceive(")

    written = re.findall(r"msg\.Write(Float|Short|Byte)\(", send)
    # the leading byte is the message type, consumed by the dispatcher
    if not written or written[0] != "Byte":
        raise AssertionError("HITINFO must begin with the message type byte")
    written = written[1:]

    read = re.findall(r"msg\.Read(Float|Short|Byte)\(", receive)

    if written != read:
        raise AssertionError(
            "HITINFO write and read field orders disagree; the message is unreliable "
            "and unframed, so a one-sided change silently corrupts every plum.\n"
            f"  written: {written}\n  read:    {read}"
        )
    if written != ["Float", "Float", "Float", "Short", "Byte", "Byte"]:
        raise AssertionError(f"unexpected HITINFO payload shape: {written}")


def main() -> None:
    cvars = read(MP_CVARS)
    header = read(MP_CVARS_HEADER)
    hitfeedback = read(HITFEEDBACK)
    playerview = read(PLAYERVIEW)

    validate_defaults(cvars, header)
    validate_single_player_has_none()
    validate_multiplayer_gates(hitfeedback, playerview)
    validate_wire_symmetry(hitfeedback)

    print("mp_damage_numbers_contract: ok")


if __name__ == "__main__":
    main()
