#!/usr/bin/env python3
"""Pin that the sys.setMatSort script event applies every sort it names, in both modes.

The stock mcc_1 briefing (briefingStartGui) re-sorts the marines' uniform decals
(models/characters/marine/fatiguesdecal*, marine_officer/generaldecal) to
SS_PREGUI so they draw before the see-through hologram GUI standing in front of
the squad, then puts them back to SS_DECAL when the table closes. An early port
commented SS_PREGUI and SS_MIN out of the single-player copy, and the unified
module kept that as a multiplayer-only gate, so in the campaign the decals stayed
at SS_DECAL and drew on top of the hologram (#256). Every name must map to its
own sort with no mode gate, as in the Quake 4 SDK.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace")


def function_body(text: str, head: str) -> str:
    match = re.search(re.escape(head) + r"[^;{}]*\{", text)
    if match is None:
        raise AssertionError(f"Missing definition of {head!r}")
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.start() : index + 1]
    raise AssertionError(f"Unbalanced body for {head!r}")


def material_sorts() -> list[str]:
    header = read("src/renderer/Material.h")
    start = header.find("SS_MIN = -10000")
    end = header.find("} materialSort_t;", start)
    if start < 0 or end < 0:
        raise AssertionError("Material.h: materialSort_t enum not found")
    return re.findall(r"\b(SS_[A-Z_]+)\b", header[start:end])


def validate_set_mat_sort() -> None:
    body = function_body(read("src/game/script/Script_Thread.cpp"), "void idThread::Event_SetMatSort(")
    code = "\n".join(line.split("//", 1)[0] for line in body.splitlines())

    for forbidden in ("IsMultiplayerMode", "isMultiplayer", "isServer", "isClient"):
        if forbidden in code:
            raise AssertionError(f"Event_SetMatSort: sort mapping is gated on {forbidden}")

    branches = re.findall(
        r'idStr::Icmp\(\s*val\s*,\s*"(SS_[A-Z_]+)"\s*\)\s*==\s*0\s*\)\s*\{\s*srt\s*=\s*(SS_[A-Z_]+)\s*;\s*\}',
        code,
    )
    mapped = dict(branches)
    for name, value in mapped.items():
        if name != value:
            raise AssertionError(f"Event_SetMatSort: {name} maps to {value}")

    expected = [name for name in material_sorts() if name != "SS_MAX"]
    missing = [name for name in expected if name not in mapped]
    if missing:
        raise AssertionError(f"Event_SetMatSort: names without an unconditional mapping: {missing}")
    if "mat->SetSort( srt )" not in code:
        raise AssertionError("Event_SetMatSort: no longer applies the sort")


def validate_registration() -> None:
    validator = read("tools/validation/openq4_validate.py")
    if '"script_set_mat_sort_contract.py"' not in validator:
        raise AssertionError("openq4_validate.py: contract not registered")


def main() -> None:
    validate_set_mat_sort()
    validate_registration()


if __name__ == "__main__":
    main()
