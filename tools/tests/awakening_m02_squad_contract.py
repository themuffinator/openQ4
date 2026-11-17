#!/usr/bin/env python3
"""The Awakening's M02 script corrections (docs/dev/awakening-missions.md).

MissionScripts.inl patches the player's installed maps/game/m02_trianfac.script
in memory. Saves record which corrected program they ran, so each version's
output is fixed once shipped: v1 and v2 must stay byte for byte, and v3 adds
the squad sequences of 17 November 2026:

- leaving the generator lift no longer sends Gray back to it, so he works the
  generator console where the Retch drops and dies there;
- Tak picks the spike gun up, examines it and hands it over, holding the gun
  on the joint that carries his own machinegun;
- the Tank introduction's stand-in Dekard stays hidden during the fight
  instead of standing in sight at the extraction mark;
- the stuck door blocks players until it is fully raised, Darnell and Mitchell
  hold each lift pose and stand exactly on their marks, the door drops only once
  raised, and it closes solid behind the player.

With the expansion installed (OPENQ4_AWAKENING_PATH, the q4xbase parent), the
corrections are applied to the reviewed script and every output is hashed.
"""

from __future__ import annotations

import hashlib
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/game/awakening/MissionScripts.inl"

INSTALLED_SHA256 = "52f91083b62daf55c64cce1ced7b61a502b7748110a96b8a9ce3eccdd50746fa"
VERSION_SHA256 = {
    1: "b1bba9304845c944e6ef87c6c380030c45cb80130008e42ed53a7a13f22e94c6",
    2: "2c2350132c409ffe2168dcfd04345e8b3aae5436ba99c8373dec60f640fdd523",
    3: "6a98c9c4eb13e459035f230df2ab1b566cfab0f0f9eceb3feeb05c375a4acc20",
}

failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def literal(group: str) -> str:
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', group)
    return "".join(part.encode("latin-1").decode("unicode_escape") for part in parts)


def corrections(text: str) -> list[tuple[int, str, str]]:
    body = text[text.index("static const correction_t corrections[] = {"):]
    body = body[:body.index("\n\t};")]
    pattern = re.compile(r'\{\s*(\d+),((?:\s*"(?:[^"\\]|\\.)*")+)\s*,((?:\s*"(?:[^"\\]|\\.)*")+)\s*\}')
    return [(int(m[1]), literal(m[2]), literal(m[3])) for m in pattern.finditer(body)]


def apply(source: str, table: list[tuple[int, str, str]], version: int) -> str | None:
    for entry_version, before, after in table:
        if entry_version > version:
            continue
        if source.count(before) != 1:
            failures.append(f"v{version}: correction does not match exactly once: {before[:60]!r}")
            return None
        source = source.replace(before, after)
    return source


def installed_script() -> Path | None:
    roots = [os.environ.get("OPENQ4_AWAKENING_PATH", ""), str(ROOT / ".home")]
    for root in filter(None, roots):
        path = Path(root) / "q4xbase/maps/game/m02_trianfac.script"
        if path.is_file():
            return path
    return None


def main() -> int:
    text = SOURCE.read_text(encoding="utf-8")
    table = corrections(text)
    counts = {v: sum(1 for entry in table if entry[0] == v) for v in (1, 2, 3)}
    check(counts[1] == 8 and counts[2] == 2, f"v1/v2 correction counts changed: {counts}")
    check([entry[0] for entry in table] == sorted(entry[0] for entry in table),
          "corrections must stay grouped by version, in order")
    check('"maps/game/m02_trianfac.openq4-v3.script"' in text, "missing the v3 virtual source name")
    check("Awakening_LoadMissionScript( awakeningM02Version3, source )" in text and
          "return awakeningM02Version3;" in text, "fresh M02 maps must compile v3")
    for name in ("awakeningM02Version1", "awakeningM02Version2"):
        check(f"normalizedName.Icmp( {name} ) == 0" in text, f"saves naming {name} must still load")

    v3 = "\n".join(after for version, _, after in table if version == 3)
    expectations = {
        "the lift arrival must not reissue orders after the generator room starts":
            "if ( !openq4GeneratorRoomStarted ) {\n\t\taiScriptedMove( $tak, $tak_pos_27",
        "TeamToGeneratorRoom must mark its start": "openq4GeneratorRoomStarted = 1;",
        "Tak must carry the spike gun on his machinegun joint":
            '$weapon_spikegun_1.bindToJoint( $tak, "MG_stock_joint", 1 );',
        "Tak's own machinegun must be hidden while he holds the spike gun":
            '$tak.hideSurface( "models/weapons/mp_guns/w_mg_lo" );',
        "the handoff must use the armed gift animation": '"player_gift_loop"',
        "Tak must examine the gun": '"weapon_check_1"',
        "the stand-in Dekard must be hidden after the Tank introduction":
            "$dekard.setOrigin( $dekard_ending_pos_4.getOrigin() );\n\t$dekard.hide();",
        "the stand-in Dekard must reappear for the ending":
            "$dekard.setOrigin( $dekard_ending_pos_1.getOrigin() );\n\t$dekard.show();",
        "the stuck door must block players and projectiles from the start":
            "$stuck_door.setContents( CONTENTS_PLAYERCLIP | 16777216 );",
        "the first lift pose must be held": 'openq4HoldStuckDoor( teammate, "tr_doorlift_loop_1", 1 );',
        "the second lift pose must be held": 'openq4HoldStuckDoor( teammate, "tr_doorlift_loop_2", 2 );',
        "a held pose must not drift": "teammate.setOrigin( spot );",
        "a hold must end with its stage": "while ( openq4StuckDoorStage == stage ) {",
        "the door may drop only once raised": "while ( !openq4StuckDoorRaised ) {",
        "Darnell and Mitchell must stand on their marks":
            "$darnell.setOrigin( $darnell_door_pos.getWorldOrigin() );",
        "the dropped door must be solid": "$stuck_door.setContents( CONTENTS_SOLID );",
    }
    for message, needle in expectations.items():
        check(needle in v3, message)
    check("player_gift2" not in v3, "the spike gun handoff must not use the empty-handed gift")
    check(v3.index('"stand_to_crouch"') < v3.index("bindToJoint") < v3.index('"weapon_check_1"') <
          v3.index('"player_gift_start"') < v3.index("$weapon_spikegun_1.remove();") <
          v3.index('$tak.showSurface( "models/weapons/mp_guns/w_mg_lo" );'),
          "the handoff beats are out of order")

    script = installed_script()
    if script is None:
        print("awakening_m02_squad_contract: installed expansion not found; table checks only")
    else:
        raw = script.read_bytes()
        if hashlib.sha256(raw).hexdigest() != INSTALLED_SHA256:
            print(f"awakening_m02_squad_contract: {script} is not the reviewed script; table checks only")
        else:
            installed = raw.decode("latin-1").replace("\r\n", "\n")
            for version, expected in VERSION_SHA256.items():
                output = apply(installed, table, version)
                if output is not None:
                    digest = hashlib.sha256(output.encode("latin-1")).hexdigest()
                    check(digest == expected, f"v{version} program changed ({digest}); "
                          "a shipped version must stay fixed, add a new one instead")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("awakening_m02_squad_contract: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
