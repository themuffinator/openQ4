#!/usr/bin/env python3
"""Exercise paused forward/backward MVD seeks without controlling user input."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--renderer", choices=("gl", "vk"), default="gl")
    parser.add_argument("--follow", type=int, default=0)
    parser.add_argument("--times", type=float, nargs="+", default=[12, 26, 46, 66, 86, 12, 46])
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--observe", action="store_true", help="record missing weapon observations without failing")
    args = parser.parse_args()
    runtime, output, demo = args.runtime_dir.resolve(), args.output_dir.resolve(), args.demo.resolve()
    if not demo.is_file() or not (args.basepath / "q4base").is_dir():
        parser.error("a recorded MVD and installed Quake 4 assets are required")
    if (not 0 <= args.follow < 32 or not args.times or
            any(not math.isfinite(t) or t <= 0 for t in args.times) or args.timeout <= 0):
        parser.error("use a player slot from 0 to 31 and positive seek times")
    verify_runtime_is_current(runtime)
    exe = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    paths = [exe, demo, *sorted((runtime / "baseoq4").glob("game_*")),
             *sorted(runtime.glob("renderer-*")),
             runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4", runtime / "baseoq4/mod.json"]
    paths = [path for path in paths if path.is_file() and path.suffix not in (".pdb", ".lib", ".exp")]

    def identity() -> dict[str, str]:
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}

    initial_identity = identity()
    game = output / "save/baseoq4"
    (game / "demos").mkdir(parents=True, exist_ok=False)
    shutil.copy2(demo, game / "demos/seek_probe.mvd")
    script = ["playMVD seek_probe", "waitMsec 3000", f"mvdFollow {args.follow}", "mvdPause 1"]
    for index, seconds in enumerate(args.times):
        script += [f"mvdSeek {seconds:.3f}", "waitMsec 3000", f"echo MVD_SEEK_CHECKPOINT {index}",
                   "mvdStatus", "r_actualRenderApi", "openq4_reportMPState", f"openq4_reportMPWorld player{args.follow + 1}",
                   "listClientEntities", f'screenshot "screenshots/seek_{index}.tga"', "waitMsec 200",
                   "mvdStatus", f"echo MVD_SEEK_CHECKPOINT_END {index}"]
    script += ["stopMVD", "echo MVD_SEEK_COMPLETE", "quit"]
    write_script(game / "seek_smoke.cfg", script)
    values = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "sys_consoleWindow": "0", "in_tty": "0", "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_mode": "-1",
        "r_customWidth": "960", "r_customHeight": "540", "r_windowWidth": "960", "r_windowHeight": "540",
        "r_renderApi": "vulkan" if args.renderer == "vk" else "gl",
        "r_swapInterval": "0", "com_maxfps": "60", "s_noSound": "1",
        "ui_retained": "1", "ui_autoJoin": "1", "fs_game": "baseoq4", "com_gameMode": "MP",
        "com_nextGameModule": "game_mp",
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(game.parent), "fs_devpath": str(game.parent),
        "net_allowCheats": "1", "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
        "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "",
    }
    command = [str(exe), *[part for key, value in values.items() for part in ("+set", key, value)],
               "+exec", "seek_smoke.cfg"]
    (output / "launch.json").write_text(json.dumps(command, indent=2) + "\n", encoding="utf-8")
    with (output / "console.log").open("w", encoding="utf-8") as console:
        process = subprocess.Popen(command, cwd=runtime, stdout=console, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            code = process.wait()
    log_path = game / "logs/openq4.log"
    log = read_log(log_path)
    failures, observations, checkpoints = [], [], []
    if identity() != initial_identity:
        failures.append("the runtime or recording changed during qualification")
    if code != 0 or "\nMVD_SEEK_COMPLETE" not in log:
        failures.append(f"playback did not complete (exit {code})")
    if re.search(r"^.*(?:ERROR:|FATAL:|Unknown command).*$", log, re.M):
        failures.append("engine log contains an error")
    expected_api = "vulkan" if args.renderer == "vk" else "gl"
    if not re.search(rf"Renderer API: {expected_api} \(", log):
        failures.append(f"requested renderer {expected_api} was not activated")
    for index, seconds in enumerate(args.times):
        block = re.search(rf"\nMVD_SEEK_CHECKPOINT {index}\s*\n(.*?)\nMVD_SEEK_CHECKPOINT_END {index}\b", log, re.S)
        if not block:
            failures.append(f"missing checkpoint {index}")
            continue
        text = block[1]
        if f'"r_actualRenderApi" is:"{expected_api}"' not in text:
            failures.append(f"checkpoint {index}: active renderer does not match {expected_api}")
        states = re.findall(r"MVD_STATUS playing=(\d+) paused=(\d+) seeking=(\d+) time=(\d+) duration=(\d+) follow=(-?\d+)", text)
        if len(states) != 2 or any(list(map(int, s[:3])) != [1, 1, 0] or
                int(s[3]) != round(seconds * 1000) or int(s[5]) != args.follow for s in states):
            failures.append(f"checkpoint {index}: seek, pause, time or followed player mismatch: {states}")
        player = re.search(rf"MP_PLAYER slot={args.follow} .*?health=(-?\d+) spectating=(\d+)", text)
        model = re.search(rf"MP_WORLD_VIEWMODEL entity={args.follow} exists=(\d+) present=(\d+) hidden=(\d+) stale=(\d+) model=(\S+) view=(\d+)", text)
        alive = bool(player and int(player[1]) > 0 and player[2] == "0")
        if not model:
            failures.append(f"checkpoint {index}: missing view-model diagnostic")
        elif alive and (list(map(int, model.groups()[:4])) != [1, 1, 0, 0] or int(model[6]) != args.follow + 1):
            observations.append(f"checkpoint {index}: living followed player has no visible weapon: {model[0]}")
        weapon = re.search(rf"MP_WORLD_WEAPON entity={args.follow} clip=(\d+) ready=(\d+) reloading=(\d+)", text)
        if alive and weapon and int(weapon[1]) > 0 and weapon.groups()[1:] == ("0", "0"):
            observations.append(f"checkpoint {index}: loaded weapon is still raising after a paused seek")
        shot = game / f"screenshots/seek_{index}.tga"
        if not shot.is_file() or shot.stat().st_size < 1024:
            failures.append(f"checkpoint {index}: missing engine capture")
        debris = len(re.findall(r"^.*rvClientMoveable.*$", text, re.M))
        if debris:
            failures.append(f"checkpoint {index}: {debris} frozen client moveables survived seek")
        checkpoints.append({"index": index, "seconds": seconds, "alive": alive, "states": states,
                            "model": model[0] if model else None, "weapon": weapon[0] if weapon else None,
                            "debris": debris, "capture": str(shot)})
    if not any(row["alive"] for row in checkpoints):
        failures.append("the recording never supplied a living followed player")
    if not args.observe:
        failures.extend(observations)
    report = {"status": "fail" if failures else "observation" if args.observe else "pass",
              "renderer": args.renderer, "exit": code,
              "identity": initial_identity, "checkpoints": checkpoints, "observations": observations,
              "failures": failures, "log": str(log_path)}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "failures": failures, "observations": observations,
                      "report": str(output / "report.json")}, indent=2))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
