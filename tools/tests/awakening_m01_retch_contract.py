#!/usr/bin/env python3
"""The Awakening's Retch ambush in m01 part 2 (docs/dev/awakening-missions.md).

- A fresh m01_stranarus_trench2 compiles scripts/openq4/awakening/m01_retch.script
  and points the m04_building_unit2_seq_5 trigger at oq4_m01_retch::ambush,
  only while the trigger and the scene's cast are the ones it was written for;
  g_awakeningM01Retch 0 keeps the alpha's scene.
- The scene follows the shared clip st_retch_intro: the briefing plays out over
  the marine's loop, the door opens before the lunge (frame 88), the death scream
  lands on the grab, the marine stays down, Tak and Navajas react, and the door
  closes behind the Retch exactly once (m04_building_unit2_seq_6 reopens it).
- The Retch leaves its scripted entrance for its own AI, with the player as enemy.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "content/baseoq4/pak0/scripts/openq4/awakening/m01_retch.script"
HOOK = ROOT / "src/game/awakening/M01Retch.inl"
GAME_LOCAL = ROOT / "src/game/Game_local.cpp"

failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    script = SCRIPT.read_text(encoding="ascii")
    hook = HOOK.read_text(encoding="utf-8")
    game = GAME_LOCAL.read_text(encoding="utf-8", errors="surrogateescape")

    check('"scripts/openq4/awakening/m01_retch.script"' in hook, "M01Retch.inl does not name the scene script")
    check('"g_awakeningM01Retch", "1"' in hook, "g_awakeningM01Retch must default on")
    check('args.Set( "call", "oq4_m01_retch::ambush" )' in hook, "seq_5 is not redirected")
    check('"maps/game/m01_stranarus_trench2"' in hook and "!anchor ||" in hook, "the redirect must check its map and anchor")
    check('#include "awakening/M01Retch.inl"' in game and "Awakening_PrepareM01Retch( mapFile )" in game
          and "Awakening_DirectM01Retch( args )" in game, "Game_local.cpp does not hook the Retch scene")

    ambush = script[script.index("void ambush()"):]
    check("if (started) { return; }" in ambush, "the scene must not run twice")
    order = ["lipsync_atlas6_youfalcsq", '"st_retch_intro", 4', "sys.trigger(retch);",
             "until(killStart + LUNGE - 0.45);", "sys.trigger(door);", "lipsync_atlas6_die",
             "lipsync_tak_whthelltht", "st_retch_intro_end_idle", "lipsync_nava_killit",
             "aiScriptedStop(retch);", "retch.setEnemy($player1);", "marine.kill();", "thread closeBehind();"]
    positions = [ambush.find(token) for token in order]
    check(all(p >= 0 for p in positions), f"missing beats: {[t for t, p in zip(order, positions) if p < 0]}")
    check(positions == sorted(positions), "the ambush beats are out of order")
    check(re.search(r"#define LUNGE\s+3\.67", script) is not None, "the lunge is frame 88 of st_retch_intro (3.67 s)")
    check(script.count("sys.trigger(door);") + script.count("sys.trigger($m04_building_unit2_seq_5_retch_kill_door_anc);") == 2,
          "the door opens once and closes once in this scene")
    check('getVectorKey("origin")' in script, "closeBehind must measure from the closed door")
    check("sys.wait(6)" not in script, "no idle hold on the Retch")

    if failures:
        for failure in failures:
            print("FAIL:", failure)
        return 1
    print("awakening_m01_retch_contract: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
