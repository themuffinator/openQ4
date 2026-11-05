#!/usr/bin/env python3
"""Qualify the client-only content packs against a server that has none (PKG-02).

pak1 (light grids) and pak2 (loadscreens) are presentation data. A dedicated
server must start and host with only the core pack, pak0, and a client that
joins it on a pure server must keep reading its own pak1 and pak2: they never
enter a pure list, so the server's lack of them does not hide them.

The server runs from a fixed, reusable core-only runtime (default
.tmp/runtime/server-core) refreshed from the staged package by hard link,
without pak1 and pak2. The client runs from the staged package itself.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "build"))
from openq4_pak import OPENQ4_CLIENT_ONLY_PACK_NAMES, OPENQ4_CONTENT_PACKS  # noqa: E402

SUFFIX = ".exe" if os.name == "nt" else ""
MODULE = ".dll" if os.name == "nt" else ".so"
ACTIVE = re.compile(r"MP_PLAYER slot=\d+ bot=0 [^\n]*spectating=0 wantSpectate=0 ingame=1")


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def link_or_copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        if destination.is_file() and os.path.samefile(source, destination):
            return
        destination.unlink()
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)


def stage_core_server(package: Path, server_runtime: Path) -> list[str]:
    """Refresh the core-only server runtime: the dedicated server, its game module and pak0."""
    server_runtime.mkdir(parents=True, exist_ok=True)
    wanted = [Path(f"openQ4-ded_x64{SUFFIX}"), Path("baseoq4/mod.json"), Path(f"baseoq4/game-mp_x64{MODULE}")]
    wanted += [Path("baseoq4") / spec.name for spec in OPENQ4_CONTENT_PACKS if not spec.client_only]
    if os.name == "nt":
        wanted += [path.relative_to(package) for path in package.glob("*.dll") if path.name.lower() == "openal32.dll"]
    for relative in wanted:
        source = package / relative
        if not source.is_file():
            raise RuntimeError(f"staged package is missing {relative}")
        link_or_copy(source, server_runtime / relative)
    for name in OPENQ4_CLIENT_ONLY_PACK_NAMES:
        stale = server_runtime / "baseoq4" / name
        if stale.exists():
            stale.unlink()
    return [relative.as_posix() for relative in wanted]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install", help="staged package the client runs from")
    parser.add_argument("--server-runtime", type=Path, default=ROOT / ".tmp/runtime/server-core")
    parser.add_argument("--basepath", type=Path, required=True, help="Quake 4 install root holding q4base")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--map", default="mp/q4dm1")
    parser.add_argument("--port", type=int, default=28104)
    parser.add_argument("--client-port", type=int, default=28992)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--startup-timeout", type=int, default=300)
    args = parser.parse_args()

    package, output, assets = args.runtime_dir.resolve(), args.output_dir.resolve(), args.basepath.resolve()
    server_runtime = args.server_runtime.resolve()
    verify_runtime_is_current(package)
    if not (assets / "q4base").is_dir():
        parser.error("the retail Quake 4 assets are required")
    client_exe = package / f"openQ4-client_x64{SUFFIX}"
    for name in OPENQ4_CLIENT_ONLY_PACK_NAMES:
        if not (package / "baseoq4" / name).is_file():
            parser.error(f"the staged client package is missing {name}")
    output.mkdir(parents=True, exist_ok=False)

    report: dict = {
        "status": "fail",
        "source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "map": args.map,
        "server_runtime": str(server_runtime),
        "client_runtime": str(package),
        "checks": {},
        "failures": [],
    }
    report["server_files"] = stage_core_server(package, server_runtime)
    report["server_runtime_sha256"] = {
        path: digest(server_runtime / path) for path in report["server_files"]
    }
    report["client_pack_sha256"] = {
        spec.name: digest(package / "baseoq4" / spec.name) for spec in OPENQ4_CONTENT_PACKS
    }

    games = {role: output / role / "baseoq4" for role in ("server", "client")}
    logs = {role: game / "logs/openq4.log" for role, game in games.items()}
    for game in games.values():
        game.mkdir(parents=True)
    common = {
        "fs_basepath": str(assets), "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": "DM",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1", "s_noSound": "1",
        "in_mouse": "0", "in_joystick": "0", "com_skipLoadingContinue": "1", "net_allowCheats": "1",
        "net_LANServer": "1", "net_clientDownload": "0", "net_enableIPv4": "1", "net_enableIPv6": "0",
        "net_ip": "127.0.0.1",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
    }
    processes: dict[str, subprocess.Popen] = {}
    streams = []
    server_commands = games["server"] / "split_server_cmd.cfg"
    write_script(server_commands, ["// idle"])
    write_script(games["server"] / "split_server_pump.cfg", ["exec split_server_cmd.cfg", "waitMsec 500", "exec split_server_pump.cfg"])
    write_script(games["server"] / "split_server_start.cfg", ["echo SPLIT_SERVER_READY", "exec split_server_pump.cfg"])
    client_loop = games["client"] / "split_c0.cfg"
    write_script(client_loop, ["waitMsec 250", "exec split_c0.cfg"])
    sequence = 0

    def launch(role: str, executable: Path, cwd: Path, cvars: dict[str, str], tail: list[str]) -> None:
        command = [str(executable)] + [part for key, value in cvars.items() for part in ("+set", key, value)] + tail
        report.setdefault("commands", {})[role] = command
        stream = (output / f"{role}-console.log").open("w", encoding="utf-8")
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

    def wait(label: str, predicate, seconds: int | None = None) -> dict[str, str]:
        until = time.monotonic() + (seconds or args.timeout)
        while time.monotonic() < until:
            texts = {role: read_log(path) for role, path in logs.items()}
            if re.search(r"FATAL ERROR|Unknown command", "\n".join(texts.values())):
                raise RuntimeError("engine error during " + label)
            ended = [role for role, process in processes.items() if process.poll() is not None]
            if ended:
                raise RuntimeError(f"{', '.join(ended)} ended during {label}")
            if predicate(texts):
                return texts
            time.sleep(0.25)
        raise RuntimeError("timeout during " + label)

    def inject(actions: list[str], finite: bool = False) -> str:
        nonlocal sequence, client_loop
        sequence += 1
        marker = f"SPLIT_STEP_DONE_{sequence}"
        following = games["client"] / f"split_c{sequence}.cfg"
        write_script(following, ["waitMsec 250", f"exec {following.name}"])
        if finite:
            write_script(games["client"] / "split_loaded.cfg", ["echo SPLIT_MAP_LOADED", f"exec {following.name}"])
            write_script(client_loop, ["set g_autoExecAfterMapLoad split_loaded.cfg", *actions, "echo " + marker])
        else:
            write_script(client_loop, [*actions, "echo " + marker, f"exec {following.name}"])
        client_loop = following
        return marker

    def step(label: str, actions: list[str]) -> str:
        begin = f"SPLIT_STEP_BEGIN_{sequence + 1}"
        marker = inject(["echo " + begin, *actions])
        text = wait(label, lambda t: "\n" + marker in t["client"])["client"]
        return text.rpartition("\n" + marker)[0].rpartition("\n" + begin)[2]

    def check(name: str, passed: bool, detail: str = "") -> None:
        report["checks"][name] = {"passed": bool(passed), **({"detail": detail} if detail else {})}
        if not passed:
            report["failures"].append(f"{name}{': ' + detail if detail else ''}")

    started = time.monotonic()
    try:
        server = dict(common, fs_savepath=str(games["server"].parent), fs_devpath=str(games["server"].parent),
                      net_port=str(args.port), net_serverDedicated="1", si_pure="1", si_name="PKG-02 core-only server",
                      si_maxPlayers="4", bot_minPlayers="0", si_warmup="0", si_timeLimit="0", si_fragLimit="0",
                      g_autoExecAfterMapLoad="split_server_start.cfg", g_autoExecAfterMapLoadDelayMs="500")
        launch("server", server_runtime / f"openQ4-ded_x64{SUFFIX}", server_runtime, server, ["+spawnServer", args.map])
        server_text = wait("server startup", lambda t: "\nSPLIT_SERVER_READY" in t["server"], args.startup_timeout)["server"]
        check("server spawned without client-only packs", f"Server spawned on port {args.port}." in server_text)
        for spec in OPENQ4_CONTENT_PACKS:
            if spec.client_only:
                line = f"Client-only pk4 baseoq4/{spec.name} ({spec.contents}) is not installed; this server does not need it"
                check(f"server reports {spec.name} as not needed", line in server_text)
        check("server never mounts a client-only pack",
              not any(f"baseoq4/{name}" in line and "Loaded pk4" in line
                      for line in server_text.splitlines() for name in OPENQ4_CLIENT_ONLY_PACK_NAMES))

        client = dict(common, fs_savepath=str(games["client"].parent), fs_devpath=str(games["client"].parent),
                      net_port=str(args.client_port), ui_name="SplitPlayer", ui_autoJoin="1", ui_spectate="Play",
                      r_renderApi="gl", r_fullscreen="0", r_fullscreenDesktop="0", r_borderless="0",
                      r_borderlessDefaultMigrated="1", r_hiddenWindow="1", r_mode="-1", r_customWidth="640",
                      r_customHeight="360", r_windowWidth="640", r_windowHeight="360", com_maxfps="60",
                      r_swapInterval="0", r_useLightGrid="1", g_autoExecAfterMapLoad="split_loaded.cfg",
                      g_autoExecAfterMapLoadDelayMs="500")
        launch("client", client_exe, package, client, ["+exec", "split_c0.cfg"])
        wait("client startup", lambda t: "split_c0.cfg" in t["client"] or "\nSPLIT_STEP" in t["client"], args.startup_timeout)

        loads = read_log(logs["client"]).count("\nSPLIT_MAP_LOADED")
        inject([f'connect "127.0.0.1:{args.port}"'], finite=True)
        wait("client map load", lambda t: t["client"].count("\nSPLIT_MAP_LOADED") > loads)
        deadline = time.monotonic() + args.timeout
        while True:
            state = step("player state", ["openq4_reportMPState"]).rsplit("\nMP_STATE ", 1)[-1]
            if ACTIVE.search(state):
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("the client never became an active player")
            time.sleep(0.75)
        check("client joined the pure core-only server", True)

        paths = step("search path", ["path"])
        for spec in OPENQ4_CONTENT_PACKS:
            pattern = re.compile(re.escape(f"baseoq4/{spec.name}") + r"[^\n]*\n\s+(on|not on) the pure list")
            found = pattern.search(paths.replace("\\", "/"))
            expected = "not on" if spec.client_only else "on"
            check(f"client keeps {spec.name} {expected} the pure list", bool(found) and found.group(1) == expected,
                  found.group(0).strip() if found else "pack not in the search path")
        listing = step("loadscreen listing", ["dir gfx/guis/loadscreens .dds"])
        loadscreens = re.findall(r"^\S+\.dds$", listing, re.MULTILINE)
        check("client still reads its loadscreens on the pure server", len(loadscreens) > 0, f"{len(loadscreens)} DDS tiles visible")
        map_name = args.map.rsplit("/", 1)[-1]
        grids = step("light-grid listing", ["dir maps/mp .lightgridpack"])
        check("client still reads its light grids on the pure server", f"{map_name}.lightgridpack" in grids)
        client_text = read_log(logs["client"])
        check("client lit the map from its packed light grid",
              f"LightGrid setup for maps/{args.map} used packed data" in client_text or
              f"LightGrid setup for {args.map} used packed data" in client_text)

        inject(["quit"])
        report["exit_codes"] = {"client": processes["client"].wait(timeout=30)}
        write_script(server_commands, ["quit"])
        report["exit_codes"]["server"] = processes["server"].wait(timeout=30)
        if any(report["exit_codes"].values()):
            report["failures"].append(f"unclean exits: {report['exit_codes']}")
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        report["failures"].append(str(error))
    finally:
        write_script(server_commands, ["quit"])
        try:
            inject(["quit"])
        except OSError:
            pass
        for role, process in processes.items():
            if process.poll() is None:
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    report["failures"].append(role + " required forced termination")
        for stream in streams:
            stream.close()
    report["elapsed_seconds"] = round(time.monotonic() - started, 2)
    report["status"] = "fail" if report["failures"] else "pass"
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "checks", "failures")}, indent=2))
    return int(bool(report["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
