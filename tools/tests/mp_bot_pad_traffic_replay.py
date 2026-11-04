#!/usr/bin/env python3
"""Reproduce CTF3's two-bot pad collision without weapon or observer input.

The recorded approach states recreate the long-match fall at (3248, 3220).
This positive regression requires both bots to complete the pad and remain
alive, with no player collision before both pad flights finish. Later ordinary
navigation contacts remain reported. It never classifies or excludes match deaths.
"""
from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path

from mp_bot_impulse_replay import replay_identity, replay_session, WORLD_DEATH

ROOT = Path(__file__).resolve().parents[2]
TRIAL = {
    "name": "pad_hop_reservation",
    "observe_msec": 12000,
    "sample_msec": 64,
    "setup": [
        "set bot_debug 3", "addbot Sledge 4 exact", "waitMsec 64",
        "bot_probePadTraffic 1 3670 2800 -287.75 0 0 0 2916 2916 -32 func_jumppad_20 3540 3516 -277.75",
        "bot_probePadTraffic 2 3515.37 3450.69 -306.76 25.071 24.968 11.209 2916 2916 -32 func_jumppad_20 3540 3516 -277.75",
        "set bot_pause 0", "waitMsec 208", "set bot_pause 1",
        "echo MP_PAD_TRAFFIC_RELEASE",
    ],
    "command": "bot_probePadTraffic 1 3641.52 3483.44 -287.75 60.535 -13.577 0 2916 2916 -32 func_jumppad_20 3540 3516 -277.75",
}


def assess(log: str) -> dict:
    begins = list(re.finditer(r"^MP_BOT_TRIAL_BEGIN pad_hop_reservation$", log, re.M))
    releases = list(re.finditer(r"^MP_PAD_TRAFFIC_RELEASE$", log, re.M))
    ends = list(re.finditer(r"^MP_BOT_TRIAL_END pad_hop_reservation$", log, re.M))
    if (len(begins) != 1 or len(releases) != 1 or len(ends) != 1 or
            not begins[0].end() < releases[0].start() < ends[0].start()):
        return {"passed": False, "reason": "missing or out-of-order markers"}
    setup = log[begins[0].end():releases[0].start()]
    body = log[releases[0].end():ends[0].start()]
    armed = (len(re.findall(r"^BOT_PAD_PROBE client=1 ", setup, re.M)) == 1 and
             len(re.findall(r"^BOT_PAD_PROBE client=2 ", setup, re.M)) == 1 and
             len(re.findall(r"^BOT_PAD_PROBE client=1 ", body, re.M)) == 1 and
             not re.search(r"^BOT_PAD_PROBE client=2 ", body, re.M))
    deaths = [match.group(0) for match in WORLD_DEATH.finditer(setup + body)]
    contacts = re.findall(r"^BOT_PLAYER_CONTACT .*$", setup + body, re.M)
    results = []
    completion_ends = []
    for slot, name in ((1, "Rhodes"), (2, "Sledge")):
        states = re.findall(rf"^MP_PHYSICS slot={slot} grounded=([01]) health=(-?\d+) velocity=.*$", body, re.M)
        completion = re.search(rf"^bot {name}: completed jump pad .* to 2916 2916 -32 .*$", body, re.M)
        completed = completion is not None
        if completion:
            completion_ends.append(completion.end())
        alive = bool(states and int(states[-1][1]) > 0)
        grounded = bool(states and states[-1][0] == "1")
        results.append({"slot": slot, "completed_pad": completed, "alive": alive,
                        "final_grounded": grounded})
    # Continuing normal navigation can put the terminal frame in another
    # traversal. Completion of the original pad and no death are mandatory;
    # the reported grounded flag is not used to reclassify any match death.
    pad_body = body[:max(completion_ends)] if len(completion_ends) == 2 else body
    pad_contacts = re.findall(r"^BOT_PLAYER_CONTACT .*$", setup + pad_body, re.M)
    return {"passed": armed and not deaths and not pad_contacts and
            all(row["completed_pad"] and row["alive"] for row in results),
            "armed": armed, "world_deaths": deaths, "pad_contacts": pad_contacts,
            "all_player_contacts": contacts, "bots": results}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-bot-pad-traffic")
    parser.add_argument("--port", type=int, default=29345)
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    executable = "openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"
    before = replay_identity(runtime, executable)
    complete, log, code = replay_session(runtime, executable, args.basepath.resolve(), output,
                                         "mp/q4ctf3", "CTF", [TRIAL], args.port, 300)
    result = assess(log)
    unchanged = before == replay_identity(runtime, executable)
    report = {"complete": complete, "exit_code": code, "runtime_identity": before,
              "runtime_unchanged": unchanged, "assessment": result,
              "passed": complete and unchanged and result["passed"]}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
