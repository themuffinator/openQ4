#!/usr/bin/env python3
"""Two windowed clients verify round turnover or flag captures through engine commands."""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def verify_runtime_has_unified_game_module(runtime: Path):
    """Refuse a stage from before the single game module.

    One `game_<arch>` module now serves single-player and multiplayer. A stage
    whose game directory holds only the old `game-sp_*`/`game-mp_*` modules, or
    no game module at all, comes from an engine that asks for `game-sp_<arch>`
    and stops on a fatal error at the first map. The timestamp check below
    cannot see this when the build lives outside `<stage>/../builddir`.
    """
    if os.environ.get("OPENQ4_ALLOW_STALE_RUNTIME", "").strip() == "1":
        return
    runtime = runtime.resolve()
    game_dir = runtime / "baseoq4"
    if not game_dir.is_dir():
        return
    unified = [path for directory in (game_dir, runtime)
               for path in directory.glob("game_*") if path.is_file()]
    if unified:
        return
    split = sorted(path.name for directory in (game_dir, runtime)
                   for pattern in ("game-sp_*", "game-mp_*")
                   for path in directory.glob(pattern) if path.is_file())
    found = f"only the old split modules ({', '.join(split)})" if split else "no game module"
    restage = ("tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects"
               if os.name == "nt" else
               "meson install -C builddir --no-rebuild --skip-subprojects")
    raise SystemExit(
        f"{runtime} has {found}: it predates the unified game_<arch> module, and "
        "its engine stops on a fatal error looking for game-sp_<arch>.\n\n"
        f"Re-stage it from a current build before running:\n  {restage}\n"
        "Pass --runtime-dir to qualify a different tree, or set "
        "OPENQ4_ALLOW_STALE_RUNTIME=1 to run it deliberately.\n")


def verify_runtime_is_current(runtime: Path, executable_name: str | None = None):
    """Refuse to qualify a staged runtime older than the build it came from.

    These smokes run whatever sits in the staged tree, which `meson install`
    moves and a plain compile does not. Qualifying a stale stage reports
    yesterday's binaries as today's behaviour, and the difference surfaces as a
    product failure rather than a packaging mistake.
    """
    if os.environ.get("OPENQ4_ALLOW_STALE_RUNTIME", "").strip() == "1":
        return
    # A standard stage in another worktree belongs to that worktree's build,
    # not to the checkout containing this harness. Resolve junctions as well.
    runtime = runtime.resolve()
    verify_runtime_has_unified_game_module(runtime)
    build_root = runtime.parent if runtime.name == ".install" else ROOT
    build = build_root / "builddir"
    if not build.is_dir():
        return

    executable_name = executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    pairs = [(runtime / executable_name, build / executable_name)]
    staged_game, built_game = runtime / "baseoq4", build / "baseoq4"
    if staged_game.is_dir() and built_game.is_dir():
        pairs += [(staged_game / built.name, built)
                  for built in sorted(built_game.iterdir())
                  if built.is_file() and (staged_game / built.name).is_file()]

    # One second of slack: install copies rather than hardlinks, so identical
    # content can still differ by the filesystem's timestamp granularity.
    stale = [(staged, built) for staged, built in pairs
             if staged.is_file() and built.is_file()
             and built.stat().st_mtime - staged.stat().st_mtime > 1.0]
    if not stale:
        return

    restage = ("tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects"
               if os.name == "nt" else
               "meson install -C builddir --no-rebuild --skip-subprojects")
    listing = "\n".join(
        f"  {staged.name}: staged {int(staged.stat().st_mtime)} < built {int(built.stat().st_mtime)}"
        for staged, built in stale)
    raise SystemExit(
        f"{runtime} is older than {build} and would qualify a stale build:\n{listing}\n\n"
        f"Re-stage it before running:\n  {restage}\n"
        "Pass --runtime-dir to qualify a different tree, or set "
        "OPENQ4_ALLOW_STALE_RUNTIME=1 to run the stale one deliberately.\n")


def write_script(path: Path, lines: list[str]) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    deadline = time.monotonic() + 5
    while True:
        try:
            temporary.replace(path)
            return
        except PermissionError:
            # Windows can briefly hold the old config while the engine reads
            # it. Preserve atomic publication and retry that sharing conflict.
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.05)


def read_log(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", help="runtime executable basename for an isolated test copy")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-round-remote-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--managed", action="store_true", help="finish the competitive round profile's eight rounds with two humans")
    parser.add_argument("--gametype", choices=("Clan Arena", "Freeze Tag", "Red Rover", "CTF",
                        "One Flag CTF", "Arena CTF", "Arena One Flag CTF"), default="Clan Arena")
    args = parser.parse_args()
    flag_mode = "CTF" in args.gametype
    if args.managed and flag_mode:
        parser.error("--managed currently qualifies round modes")
    round_limit = 8 if args.managed else 2
    take_flag = "teleport " + ("openq4_neutral_flag" if "One Flag" in args.gametype else "mp_ctf_strogg_flag_1")
    score_flag = "teleport " + ("mp_ctf_strogg_flag_1" if "One Flag" in args.gametype else "mp_ctf_marine_flag_1")
    capture = [take_flag, "waitMsec 1500", score_flag]
    active_marker = "MP_REMOTE_CAPTURE_ACTIVE" if flag_mode else "MP_REMOTE_ROUND_TWO_ACTIVE"
    live_shot = "capture_one" if flag_mode else "round_two"
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    verify_runtime_is_current(runtime, getattr(args, "executable_name", ""))
    exe = runtime / (args.executable_name or ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64"))
    if not exe.is_file() or not args.basepath.is_dir():
        parser.error("a staged runtime and installed Quake 4 assets are required")
    saves = {role: output / role for role in ("host", "client")}
    games = {role: path / "baseoq4" for role, path in saves.items()}
    for path in games.values():
        path.mkdir(parents=True, exist_ok=True)
    logs = {role: path / "logs/openq4.log" for role, path in games.items()}
    loops = {role: path / "round_probe.cfg" for role, path in games.items()}
    host_loop = ["waitMsec 250", "openq4_reportMPState", "exec round_probe.cfg"]
    client_loop = ["waitMsec 250", "exec round_probe.cfg"]
    write_script(loops["host"], host_loop)
    write_script(loops["client"], client_loop)
    write_script(games["host"] / "round_second.cfg", ["waitMsec 250", "openq4_reportMPState", "exec round_second.cfg"])
    write_script(games["client"] / "round_wait.cfg", ["waitMsec 250", "exec round_wait.cfg"])
    for path in games.values():
        write_script(path / "round_start.cfg", ["exec round_probe.cfg"])

    common = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "fs_basepath": str(args.basepath.resolve()), "fs_game": "baseoq4", "com_gameMode": "MP",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_mode": "-1", "r_customWidth": "800",
        "r_customHeight": "450", "r_windowWidth": "800", "r_windowHeight": "450",
        "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
        "r_renderApi": "gl", "com_maxfps": "60", "r_swapInterval": "0",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
        "ui_autoJoin": "1", "ui_spectate": "Play", "ui_ready": "Ready", "net_allowCheats": "1",
        "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "round_start.cfg",
        "g_autoExecAfterMapLoadDelayMs": "500",
    }

    def command(role: str) -> list[str]:
        cvars = dict(common, fs_savepath=str(saves[role]), fs_devpath=str(saves[role]),
                     ui_name=role, ui_team="Marine" if role == "host" else "Strogg")
        if role == "host":
            cvars.update(net_port="28841", net_serverDedicated="0", net_LANServer="1", si_pure="0",
                         si_maxPlayers="4", si_gameType=args.gametype, si_minPlayers="2", si_autoBalance="1",
                         si_warmup="1", si_useReady="0", si_countDown="4", si_timeLimit="0",
                         si_roundLimit="2", si_roundWarmupDelay="2", si_roundEndDelay="3",
                         bot_minPlayers="0", g_matchProfile="casual", g_gameReviewPause="10")
            if args.managed:
                cvars.update(g_matchProfile="competitive_round")
            if flag_mode:
                cvars.update(si_captureLimit="2")
        else:
            cvars.update(net_port="28842")
        result = [str(exe)]
        for key, value in cvars.items():
            result += ["+set", key, value]
        assert len(cvars) + 1 <= 64
        result += ["+spawnServer", "mp/q4ctf1" if flag_mode else "mp/q4dm1"] if role == "host" else ["+connect", "127.0.0.1:28841"]
        (output / f"{role}-launch.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        return result

    processes: dict[str, subprocess.Popen] = {}
    streams = []
    stage, failures, codes = 0, [], {}
    force_sent = False
    killed_round = 1
    managed_poll = games["host"] / "round_managed_poll.cfg"
    started = time.time()

    def launch(role: str) -> None:
        stream = (output / f"{role}-console.log").open("w", encoding="utf-8")
        streams.append(stream)
        processes[role] = subprocess.Popen(command(role), cwd=runtime, stdout=stream, stderr=subprocess.STDOUT)

    try:
        launch("host")
        while time.time() - started < args.timeout:
            host, client = read_log(logs["host"]), read_log(logs["client"])
            if re.search(r"could not import casual match rule|competitive rules rejected|Ignoring invalid .*reliable|Ignoring invalid round|FATAL ERROR|ERROR:", host + client):
                failures.append("engine rejected the rules or reported an error")
                break
            if "client" not in processes and "MP match phase: 0 -> 1" in host:
                launch("client")
            current_host = host.rsplit("\nMP_STATE ", 1)[-1]
            if args.managed and not force_sent and all(re.search(
                    rf"MP_PLAYER slot={slot} .*spectating=0 wantSpectate=0 ingame=1", current_host) for slot in (0, 1)):
                ready_wait = games["host"] / "round_ready_wait.cfg"
                write_script(ready_wait, ["waitMsec 250", "openq4_reportMPState", "exec round_ready_wait.cfg"])
                write_script(loops["host"], ["openq4_assertMPClientActive", "forceReady", "echo MP_REMOTE_FORCE_READY", "exec round_ready_wait.cfg"])
                loops["host"] = ready_wait
                force_sent = True
            if stage == 0 and re.search(r"MP_STATE .*phase=3 " + ("" if flag_mode else r".*round=2 round_number=1"), host):
                first_action = [*capture, "waitMsec 1500", "openq4_reportMPState", "echo MP_REMOTE_FIRST_CAPTURE"] if flag_mode else ["echo MP_REMOTE_FIRST_DEATH", "kill 1"]
                write_script(loops["host"], [*first_action, "exec round_second.cfg"])
                stage = 1
            if stage == 1 and ("\nMP_REMOTE_FIRST_CAPTURE" in host if flag_mode else
                    "MP round world restart:" in client and re.search(r"phase=3 .*round=2 round_number=2", current_host)):
                # Wait through the two-second round countdown before checking
                # that the previously eliminated remote player has respawned.
                write_script(loops["client"], ["waitMsec 3500", "openq4_assertMPClientActive",
                             f'screenshot "screenshots/{live_shot}.tga"', f"echo {active_marker}",
                             "exec round_wait.cfg"])
                stage = 2
            if stage == 2 and "\n" + active_marker in client:
                if args.managed:
                    write_script(managed_poll, ["waitMsec 250", "openq4_reportMPState", f"exec {managed_poll.name}"])
                    write_script(games["host"] / "round_second.cfg", ["kill 1", f"exec {managed_poll.name}"])
                    killed_round = 2
                    stage = 5
                else:
                    write_script(games["host"] / "round_second.cfg", ["openq4_reportMPState", *(capture if flag_mode else ["kill 1"]), "waitMsec 5000",
                                 "echo MP_REMOTE_REVIEW", "openq4_reportMPState",
                                 'screenshot "screenshots/review.tga"', "waitMsec 2000", "quit"])
                    stage = 3
            if stage == 5 and re.search(rf"phase=3 .*round=2 round_number={killed_round + 1} ", current_host):
                killed_round += 1
                if killed_round == round_limit:
                    write_script(managed_poll, ["kill 1", "waitMsec 6500", "echo MP_REMOTE_REVIEW",
                                 "openq4_reportMPState", 'screenshot "screenshots/review.tga"', "waitMsec 2000", "quit"])
                    stage = 3
                else:
                    next_poll = games["host"] / ("round_managed_wait.cfg" if managed_poll.name == "round_managed_poll.cfg" else "round_managed_poll.cfg")
                    write_script(next_poll, ["waitMsec 250", "openq4_reportMPState", f"exec {next_poll.name}"])
                    write_script(managed_poll, ["kill 1", f"exec {next_poll.name}"])
                    managed_poll = next_poll
            if stage == 3 and "\nMP_REMOTE_REVIEW" in host:
                write_script(games["client"] / "round_wait.cfg", ["waitMsec 500", 'screenshot "screenshots/review.tga"', "quit"])
                stage = 4
            if processes["host"].poll() is not None or ("client" in processes and processes["client"].poll() is not None):
                if stage != 4:
                    failures.append(f"client or host ended before completing stage {stage}")
                break
            time.sleep(0.2)
        else:
            failures.append(f"timeout at stage {stage}")
    finally:
        for role, process in processes.items():
            try:
                codes[role] = process.wait(timeout=8 if stage == 4 else 1)
            except subprocess.TimeoutExpired:
                process.kill()  # Only a process launched by this test.
                process.wait()
                codes[role] = -1
        for stream in streams:
            stream.close()
    host, client = read_log(logs["host"]), read_log(logs["client"])
    if stage != 4 or set(codes) != {"host", "client"} or any(codes.values()):
        failures.append(f"incomplete two-client run: stage={stage}, exits={codes}")
    if not flag_mode and len(re.findall(r"MP round world restart: gametype=\d+ phase=3 statePreserved=1", client)) != round_limit - 1:
        failures.append(f"remote client did not preserve its match state through {round_limit - 1} round resets")
    if not re.search(rf"MP_REMOTE_REVIEW.*?MP_STATE .*phase=5 .*marine={round_limit} strogg=0", host, re.DOTALL):
        failures.append(f"host did not retain a {round_limit}-0 result in review")
    if "OPENQ4_STOCK_BASELINE_MP_CLIENT_ACTIVE" not in client:
        failures.append("remote player was not active after the first score")
    for role, log in (("host", host), ("client", client)):
        if not logs[role].is_file() or logs[role].stat().st_mtime < started:
            failures.append(f"missing fresh {role} engine log")
        if re.search(r"ERROR:|FATAL ERROR|Ignoring invalid .*reliable|Ignoring invalid round world restart|rejected match.*transition|entity not found|Not allowed in multiplayer", log):
            failures.append(f"{role} reported an engine error or rejected transition")
    shots = [games["host"] / "screenshots/review.tga", games["client"] / f"screenshots/{live_shot}.tga",
             games["client"] / "screenshots/review.tga"]
    if not all(p.is_file() and p.stat().st_size > 18 and p.stat().st_mtime >= started for p in shots):
        failures.append("missing fresh engine screenshots")
    report = {"status": "fail" if failures else "pass", "gametype": args.gametype, "managed": args.managed, "stage": stage,
              "failures": failures, "exits": codes, "logs": {k: str(v) for k, v in logs.items()},
              "screenshots": [str(p) for p in shots]}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
