#!/usr/bin/env python3
"""Discover a real LAN server, inspect its browser row, and join stock gameplay."""
from __future__ import annotations

import argparse
import json
import os
import re
import socket
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, write_script

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--server-name", default="openQ4-ded_x64.exe" if os.name == "nt" else "openQ4-ded_x64")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-browser-smoke")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    games = {role: output / role / "baseoq4" for role in ("server", "client")}
    logs = {role: game / "logs/openq4.log" for role, game in games.items()}
    loops, streams, processes, failures, exits = {}, [], {}, [], {}
    for role, game in games.items():
        game.mkdir(parents=True, exist_ok=True)
        loops[role] = game / "browser_idle.cfg"
        write_script(loops[role], ["waitMsec 250", "exec browser_idle.cfg"])
    # The engine's actual LAN discovery probes only these eight ports.
    port = None
    for candidate in range(28004, 28012):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            try:
                probe.bind(("0.0.0.0", candidate))
                port = candidate
                break
            except OSError:
                pass
    if port is None:
        parser.error("all eight LAN discovery ports are in use")
    sequence, stage, selected_endpoint = 0, "startup", ""
    arena_name = "Refinement Arena " + str(time.time_ns())[-8:]
    deadline = time.monotonic() + args.timeout

    def launch(role: str) -> None:
        cvars = {
            "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
            "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(games[role].parent),
            "fs_devpath": str(games[role].parent), "fs_game": "baseoq4", "com_gameMode": "MP",
            "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
            "ui_autoJoin": "1", "ui_spectate": "Play", "ui_name": "BrowserPlayer",
            "net_allowCheats": "1", "com_skipLoadingContinue": "1",
            "com_skipLogoVideos": "1",
        }
        if role == "server":
            cvars.update(net_port=str(port), net_serverDedicated="1", net_LANServer="1",
                         si_name="^2" + arena_name, si_pure="0", si_maxPlayers="4",
                         si_gameType="DM", si_warmup="0", bot_minPlayers="0")
        else:
            cvars.update(net_port="28981", r_fullscreen="0", r_borderless="0", r_fullscreenDesktop="0",
                         r_borderlessDefaultMigrated="1", r_mode="-1", r_customWidth="960",
                         r_customHeight="540", r_windowWidth="960", r_windowHeight="540",
                         r_hiddenWindow="1", in_mouse="0", in_joystick="0", s_noSound="1",
                         r_renderApi=args.renderer, com_maxfps="60", r_swapInterval="0",
                         gui_filter_password="0", gui_filter_players="0", gui_filter_gameType="0",
                         gui_filter_idle="1", gui_filter_game="0", gui_filter_mod="")
            cvars.update(g_autoExecAfterMapLoad="browser_joined.cfg", g_autoExecAfterMapLoadDelayMs="500")
        executable = runtime / (args.server_name if role == "server" else args.executable_name)
        command = [str(executable)]
        for key, value in cvars.items():
            command += ["+set", key, value]
        if role == "server":
            command += ["+spawnServer", "mp/q4dm1"]
        command += ["+exec", loops[role].name]
        (output / (role + "-launch.json")).write_text(json.dumps(command, indent=2), encoding="utf-8")
        stream = (output / (role + "-console.log")).open("w", encoding="utf-8")
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

    def wait(label: str, predicate, seconds: int = 45):
        until = min(deadline, time.monotonic() + seconds)
        while time.monotonic() < until:
            texts = {role: read_log(path) for role, path in logs.items()}
            if re.search(r"FATAL ERROR|ERROR:|Unknown command|usage: openq4_|\.gui, line", "\n".join(texts.values())):
                raise RuntimeError("engine/GUI error during " + label)
            if any(p.poll() is not None for p in processes.values()):
                raise RuntimeError("process ended during " + label)
            if predicate(texts):
                return texts
            time.sleep(0.2)
        raise RuntimeError("timeout during " + label)

    def inject(role: str, actions: list[str]) -> str:
        nonlocal sequence
        sequence += 1
        following = games[role] / f"browser_idle_{sequence}.cfg"
        write_script(following, ["waitMsec 250", f"exec {following.name}"])
        marker = f"MP_BROWSER_DONE_{sequence}"
        write_script(loops[role], [*actions, "echo " + marker, f"exec {following.name}"])
        loops[role] = following
        return marker

    def perform(label: str, actions: list[str], report: bool = True) -> str:
        nonlocal stage
        stage = label
        begin = f"MP_BROWSER_BEGIN_{sequence + 1}"
        marker = inject("client", ["echo " + begin, *actions, *(["openq4_browser report"] if report else [])])
        text = wait(label, lambda t: "\n" + marker + " " in t["client"])["client"]
        section = text.partition("\n" + begin + " ")[2].partition("\n" + marker + " ")[0]
        with (output / "browser-steps.log").open("a", encoding="utf-8") as evidence:
            evidence.write(label + "\n" + section + "\n")
        return section

    def arena_row(text: str) -> int:
        found = re.search(r"BROWSER_ROW index=(\d+) text=[^\r\n]*" + re.escape(arena_name), text)
        if not found:
            raise RuntimeError("LAN scan did not populate the actual GUI list with the server")
        return int(found[1])

    try:
        launch("server")
        wait("dedicated stock map", lambda t: f"Server spawned on port {port}." in t["server"])
        launch("client")
        menu_deadline = min(deadline, time.monotonic() + 60)
        while True:
            menu = perform("await active main menu", ["waitMsec 750"])
            if "BROWSER_STATE " in menu and re.search(
                    r"BROWSER_WINDOW key=desktop::active found=1 value=0(?:\.0*)?\s", menu):
                break
            if time.monotonic() >= menu_deadline:
                raise RuntimeError("main menu never became ready for browser navigation")
        perform("open LAN browser", ["openq4_browser open", "waitMsec 1800",
            "openq4_browser source lan", "openq4_browser action updateServers", "waitMsec 2500"])
        found = perform("LAN results", ["waitMsec 1500"])
        row = arena_row(found)
        selected = perform("select server", [f"openq4_browser select {row}", "waitMsec 250",
            'screenshot "screenshots/browser-selected.tga"'])
        address = re.search(r"BROWSER_DETAIL key=server_IP value=([^\r\n]+)", selected)
        if not address or not address[1].strip().endswith(":" + str(port)):
            raise RuntimeError("selected row does not resolve the discovered server endpoint")
        selected_endpoint = address[1].strip()
        if not re.search(r"BROWSER_WINDOW key=p_mp_browse::visible found=1 value=1(?:\.0*)?\s", selected):
            raise RuntimeError("the populated browser panel is not visible")
        sorted_rows = perform("sort retains endpoint", ["openq4_browser action sortPing"])
        if "value=" + selected_endpoint not in sorted_rows:
            raise RuntimeError("sorting changed the selected endpoint")
        perform("save favorite", ["openq4_browser action toggleFavorite"])
        favorites = games["client"] / "server_favorites.list"
        if not favorites.is_file() or selected_endpoint not in favorites.read_text(encoding="utf-8"):
            raise RuntimeError("favorite endpoint was not persisted")
        perform("remove favorite", ["openq4_browser action toggleFavorite"])
        if selected_endpoint in favorites.read_text(encoding="utf-8"):
            raise RuntimeError("favorite endpoint was not removed")
        hidden = perform("filter clears unavailable selection", ["set gui_filter_players 2",
            "openq4_browser action FilterServers", "waitMsec 250"])
        if re.search(r"BROWSER_STATE [^\r\n]*selected=1", hidden):
            raise RuntimeError("filter retained a joinable hidden selection")
        restored = perform("restore empty servers", ["set gui_filter_players 0", "openq4_browser action FilterServers"])
        stage = "join selected LAN server"
        following = games["client"] / "browser_play_idle.cfg"
        write_script(following, ["waitMsec 250", "openq4_reportMPState", f"exec {following.name}"])
        write_script(games["client"] / "browser_joined.cfg", ["echo MP_BROWSER_MAP_LOADED", f"exec {following.name}"])
        # A first connection from the main menu may reload game_sp to game_mp.
        # Let this finite script drain so the queued module reload can execute;
        # an endlessly inserted idle loop would starve the appended handoff.
        write_script(loops["client"], [f"openq4_browser select {arena_row(restored)}",
            "openq4_browser action connect"])
        loops["client"] = following
        wait(stage, lambda t: "\nMP_BROWSER_MAP_LOADED " in t["client"] and re.search(
            r"MP_PLAYER slot=\d+ .*spectating=0 wantSpectate=0 ingame=1",
            t["client"].rsplit("\nMP_STATE ", 1)[-1]), seconds=75)
        perform("joined gameplay", ["openq4_reportMPState", "openq4_assertMPClientActive",
            'screenshot "screenshots/browser-gameplay.tga"'], report=False)
        log = read_log(logs["client"])
        if not re.search(r"MP_PLAYER slot=\d+ .*spectating=0 wantSpectate=0 ingame=1", log):
            raise RuntimeError("browser connection never reached active map gameplay")
        if not re.search(r"client \d+ connected\.", read_log(logs["server"])):
            raise RuntimeError("the dedicated server did not accept the browser client")
        stage = "complete"
    except (OSError, RuntimeError) as error:
        failures.append(str(error))
    finally:
        for role, process in processes.items():
            if process.poll() is None:
                try:
                    inject(role, ["quit"])
                except OSError:
                    pass
        for role, process in processes.items():
            try:
                exits[role] = process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                exits[role] = -1
        for stream in streams:
            stream.close()
    if stage != "complete" or len(exits) != 2 or any(exits.values()):
        failures.append(f"incomplete run: stage={stage}, exits={exits}")
    screenshots = list((games["client"] / "screenshots").glob("*.tga"))
    if len(screenshots) != 2 or any(p.stat().st_size <= 18 for p in screenshots):
        failures.append("missing engine screenshots")
    result = {"status": "fail" if failures else "pass", "failures": failures, "stage": stage,
              "exits": exits, "renderer": args.renderer, "endpoint": selected_endpoint,
              "logs": {r: str(p) for r, p in logs.items()}, "screenshots": [str(p) for p in screenshots]}
    (output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
