#!/usr/bin/env python3
"""Qualify content-only mods and trusted local game-module admission on a pure server.

Every scenario starts a fresh pure dedicated server and one client on loopback,
with hidden windows and no operating-system input. The server runs a test mod
(`oq4modsmoke`) whose package is named `pak*` so the pure list always carries it.

* matching content: a client with the same package (plus an extra local
  package the server lacks) joins, and the extra package stays outside the pure
  search path; both peers load the baseoq4 game module;
* a client launched without the mod switches to the server's game directory;
* a client missing the package, or holding an altered package of the same name,
  is refused and never enters the game;
* game code is never taken from the save path or from inside a package: a
  garbage `game-mp` module in both places is ignored;
* a mod module in the trusted executable root is refused for pure startup
  until the operator sets `net_serverAllowServerMod 1`, then loads from the mod;
* a mod with a broken `mod.json` is ignored and the server runs the normal
  openQ4 game (its strings resolve), which an ordinary client joins.

The trusted-root scenario writes `<runtime>/oq4modsmoke/` and removes it again.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import time
import zipfile
from pathlib import Path

from mp_lan_reconnect_smoke import digest
from mp_round_remote_smoke import write_script

ROOT = Path(__file__).resolve().parents[2]
MOD = "oq4modsmoke"
PACKAGE = "pak000_oq4modsmoke.pk4"
EXTRA = "pak001_oq4modsmoke_local.pk4"
SUFFIX = ".exe" if os.name == "nt" else ""
MODULE = "game-mp_x64" + (".dll" if os.name == "nt" else ".so")
REFUSAL = re.compile(r"(?i)[^\n]*(?:missing|not in sync|checksum|pure|pak|denied|rejected|refus)[^\n]*")


def package(path: Path, text: str, extra: dict[str, bytes] | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as pk4:
        info = zipfile.ZipInfo(f"def/{path.stem}.def", (2026, 11, 3, 0, 0, 0))
        pk4.writestr(info, "entityDef %s_marker {\n\t\"editor_usage\" \"%s\"\n}\n" % (path.stem, text))
        for name, data in (extra or {}).items():
            pk4.writestr(zipfile.ZipInfo(name, (2026, 11, 3, 0, 0, 0)), data)


def manifest(folder: Path) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    # Every field the engine's manifest parser requires; without them it
    # ignores fs_game and the server quietly runs the base game instead.
    (folder / "mod.json").write_text(json.dumps({
        "name": "MP-08 mod smoke", "version": "1.0", "releaseDate": "2026-11-03",
        "website": "https://www.darkmatter-quake.com", "author": "openQ4 tests",
        "requiredopenQ4Version": "0.13.2"}), encoding="utf-8")


class Scenario:
    def __init__(self, args, name: str, port: int):
        self.args, self.name, self.port = args, name, port
        self.base = args.output_dir / name
        self.saves = {role: self.base / role for role in ("server", "client")}
        self.processes, self.streams = {}, []

    def game(self, role: str, game_dir: str) -> Path:
        return self.saves[role] / game_dir

    def log(self, role: str) -> str:
        texts = []
        for path in sorted(self.saves[role].glob("*/logs/openq4.log")):
            texts.append(path.read_text(encoding="utf-8", errors="replace"))
        return "\n".join(texts)

    def launch(self, role: str, game_dir: str, extra: dict[str, str] | None = None) -> None:
        server = role == "server"
        runtime = self.args.runtime_dir.resolve()
        exe = runtime / (f"openQ4-ded_x64{SUFFIX}" if server else f"openQ4-client_x64{SUFFIX}")
        # A client may switch to the server's game directory, so arm both.
        for folder in (MOD, "baseoq4"):
            self.game(role, folder).mkdir(parents=True, exist_ok=True)
            write_script(self.game(role, folder) / "mod_loop.cfg",
                         ["waitMsec 750", "openq4_reportMPState", "exec mod_step.cfg", "exec mod_loop.cfg"])
            write_script(self.game(role, folder) / "mod_step.cfg", ["// idle"])
        cvars = {
            "fs_basepath": str(self.args.basepath.resolve()), "fs_savepath": str(self.saves[role]),
            "fs_devpath": str(self.saves[role]), "fs_game": game_dir, "com_gameMode": "MP",
            "r_fullscreen": "0", "r_borderless": "0", "r_mode": "-1", "r_customWidth": "640",
            "r_customHeight": "360", "r_hiddenWindow": "1", "r_renderApi": "gl", "r_swapInterval": "0",
            "com_maxfps": "60", "in_mouse": "0", "in_joystick": "0", "s_noSound": "1",
            "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1", "ui_autoJoin": "1",
            "ui_spectate": "Play", "ui_name": f"mod_{role}", "net_allowCheats": "1", "net_ip": "127.0.0.1",
            "net_enableIPv6": "0", "net_clientDownload": "0", "com_skipLoadingContinue": "1",
            "g_autoExecAfterMapLoad": "mod_loop.cfg", "g_autoExecAfterMapLoadDelayMs": "500",
            "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
            "net_port": str(self.port + (0 if server else 1)),
        }
        if server:
            cvars.update(net_serverDedicated="1", net_LANServer="1", si_pure="1", si_maxPlayers="4",
                         si_gameType="DM", si_warmup="0", si_timeLimit="0", si_fragLimit="0", bot_minPlayers="0")
        cvars.update(extra or {})
        command = [str(exe)]
        for key, value in cvars.items():
            command += ["+set", key, value]
        command += ["+spawnServer", "mp/q4dm1"] if server else ["+connect", f"127.0.0.1:{self.port}"]
        self.base.mkdir(parents=True, exist_ok=True)
        (self.base / f"{role}-launch.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        stream = (self.base / f"{role}-console.log").open("w", encoding="utf-8")
        self.streams.append(stream)
        self.processes[role] = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT)

    def wait(self, predicate, timeout: float) -> bool:
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            if predicate():
                return True
            if self.processes.get("server") is not None and self.processes["server"].poll() is not None:
                return False
            time.sleep(0.5)
        return False

    def step(self, role: str, game_dir: str, lines: list[str], marker: str, timeout: float = 30) -> bool:
        write_script(self.game(role, game_dir) / "mod_step.cfg", [*lines, "echo " + marker])
        done = self.wait(lambda: "\n" + marker in self.log(role), timeout)
        write_script(self.game(role, game_dir) / "mod_step.cfg", ["// idle"])
        return done

    def close(self) -> dict[str, int]:
        exits = {}
        # A refused client never loads a map, and a refused server never loads
        # one, so neither runs the quit loop; the harness stops them. Record
        # that they were still running rather than crashed.
        self.alive_at_close = {role: process.poll() is None for role, process in self.processes.items()}
        for role, process in self.processes.items():
            if process.poll() is None:
                for folder in self.saves[role].iterdir() if self.saves[role].is_dir() else ():
                    if (folder / "mod_step.cfg").is_file():
                        write_script(folder / "mod_step.cfg", ["quit"])
        for role, process in self.processes.items():
            try:
                exits[role] = process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                exits[role] = -1
        for stream in self.streams:
            stream.close()
        return exits


def module_line(text: str) -> str:
    match = re.findall(r"Loading game DLL: '([^']+)'", text)
    return match[-1] if match else ""


def joined(text: str) -> bool:
    return re.search(r"MP_PLAYER slot=0 [^\n]*ingame=1", text.rsplit("\nMP_STATE ", 1)[-1]) is not None


def run_scenario(args, number: int, name: str, setup, expect_join: bool, *, client_game: str = MOD,
                 server_extra: dict[str, str] | None = None, expect_server_map: bool = True) -> dict:
    scenario = Scenario(args, name, args.port + number * 10)
    record = {"scenario": name, "expect_join": expect_join, "server_map_loaded": False, "joined": False}
    try:
        setup(scenario)
        scenario.launch("server", MOD, server_extra)
        server_ready = scenario.wait(lambda: "MP_STATE" in scenario.log("server"), args.startup_timeout)
        record["server_map_loaded"] = server_ready
        if not expect_server_map:
            # Pure startup must refuse before loading the map, and say why.
            scenario.wait(lambda: "active mod supplies game code" in scenario.log("server"), 20)
            return record
        if not server_ready:
            raise RuntimeError("server never loaded the map")
        scenario.launch("client", client_game)
        record["joined"] = scenario.wait(lambda: joined(scenario.log("server")), args.join_timeout)
        if record["joined"]:
            scenario.step("client", MOD, ["path"], "MOD_PATH_DONE")
        else:
            time.sleep(3)
    except (RuntimeError, OSError) as error:
        record["error"] = str(error)
    finally:
        record["exits"] = scenario.close()
        record["alive_at_close"] = scenario.alive_at_close
        server, client = scenario.log("server"), scenario.log("client")
        record["server_mod_active"] = f"Ignoring fs_game '{MOD}'" not in server
        record["server_game_dirs"] = sorted({p.parent.parent.name for p in scenario.saves["server"].glob("*/logs/openq4.log")})
        record["server_unknown_strings"] = len(re.findall(r"Unknown string id", server))
        record["server_module"] = module_line(server)
        record["client_module"] = module_line(client)
        record["server_warnings"] = sorted(set(re.findall(
            r"[^\n]*(?:active mod supplies game code|No trusted openQ4 game module|Refusing game module)[^\n]*", server)))
        record["client_refusal"] = sorted(set(m[:200] for m in REFUSAL.findall(client)))[:16]
        record["client_pure_refused"] = "Can't connect to the pure server" in client
        record["client_game_dirs"] = sorted({p.parent.parent.name for p in scenario.saves["client"].glob("*/logs/openq4.log")})
        path_output = client.rsplit("\nMOD_PATH_DONE", 1)[0].rsplit("Current search path:", 1)[-1] if "MOD_PATH_DONE" in client else ""
        record["client_package_pure_status"] = pure_status(path_output, PACKAGE)
        record["client_extra_pure_status"] = pure_status(path_output, EXTRA)
    return record


def pure_status(path_output: str, name: str) -> str:
    # `path` prints each package, then whether the pure list carries it.
    lines = path_output.splitlines()
    for index, line in enumerate(lines):
        if name in line:
            following = lines[index + 1].strip() if index + 1 < len(lines) else ""
            return {"on the pure list": "on", "not on the pure list": "off"}.get(following, "unlisted")
    return "absent"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"))
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-mod-admission")
    parser.add_argument("--port", type=int, default=29271)
    parser.add_argument("--startup-timeout", type=int, default=120)
    parser.add_argument("--join-timeout", type=int, default=120)
    parser.add_argument("--only", nargs="+", default=[], metavar="SCENARIO", help="run only these scenarios")
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    runtime = args.runtime_dir.resolve()
    if not (runtime / f"openQ4-ded_x64{SUFFIX}").is_file() or not args.basepath.is_dir():
        parser.error("a staged runtime and installed stock Quake 4 assets are required")
    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        parser.error("output-dir must be empty to prevent stale runtime evidence")
    trusted = runtime / MOD
    if trusted.exists():
        parser.error(f"{trusted} already exists; remove it before running")

    def matching(s: Scenario):
        for role in ("server", "client"):
            manifest(s.game(role, MOD))
            package(s.game(role, MOD) / PACKAGE, "MP-08 content")
        package(s.game("client", MOD) / EXTRA, "client-only content")

    def missing(s: Scenario):
        manifest(s.game("server", MOD))
        package(s.game("server", MOD) / PACKAGE, "MP-08 content")
        manifest(s.game("client", MOD))

    def altered(s: Scenario):
        missing(s)
        package(s.game("client", MOD) / PACKAGE, "MP-08 altered content")

    def untrusted(s: Scenario):
        garbage = b"not a module; loading this would fail\n"
        for role in ("server", "client"):
            manifest(s.game(role, MOD))
            package(s.game(role, MOD) / PACKAGE, "MP-08 content", {MODULE: garbage})
            (s.game(role, MOD) / MODULE).write_bytes(garbage)

    def broken_manifest(s: Scenario):
        folder = s.game("server", MOD)
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "mod.json").write_text(json.dumps({"name": "MP-08 broken manifest"}), encoding="utf-8")
        package(folder / PACKAGE, "MP-08 content")

    def trusted_module(s: Scenario):
        matching(s)
        trusted.mkdir()
        shutil.copy2(runtime / "baseoq4" / MODULE, trusted / MODULE)

    scenarios = [
        (0, "content-match", matching, True, {}),
        (1, "client-follows-server-mod", matching, True, {"client_game": "baseoq4"}),
        (2, "content-missing", missing, False, {}),
        (3, "content-altered", altered, False, {}),
        (4, "untrusted-module-locations", untrusted, True, {}),
        (7, "broken-manifest-fallback", broken_manifest, True, {"client_game": "baseoq4"}),
        (5, "mod-module-refused", trusted_module, False, {"expect_server_map": False}),
        (6, "mod-module-admitted", trusted_module, True, {"server_extra": {"net_serverAllowServerMod": "1"}}),
    ]
    unknown = set(args.only) - {name for _, name, _, _, _ in scenarios}
    if unknown:
        parser.error(f"unknown scenarios: {sorted(unknown)}")
    results = []
    try:
        for number, name, setup, expect_join, options in scenarios:
            if args.only and name not in args.only:
                continue
            results.append(run_scenario(args, number, name, setup, expect_join, **options))
            # The trusted-root module is created per scenario.
            if trusted.is_dir() and not trusted.is_symlink():
                shutil.rmtree(trusted)
    finally:
        if trusted.is_dir() and not trusted.is_symlink():
            shutil.rmtree(trusted)

    base_module = str(runtime / "baseoq4" / MODULE)
    mod_module = str(trusted / MODULE)
    by_name = {r["scenario"]: r for r in results}
    checks = {}
    for record in results:
        name = record["scenario"]
        if name == "broken-manifest-fallback":
            # The broken mod is refused, and the server runs the normal openQ4
            # game rather than retail-only content.
            checks[name + ":mod_refused"] = not record["server_mod_active"]
            checks[name + ":openq4_game_dir"] = record["server_game_dirs"] == ["baseoq4"]
            checks[name + ":openq4_strings_resolve"] = record["server_unknown_strings"] == 0
            checks[name + ":join_as_expected"] = record.get("joined") is True and "error" not in record
            checks[name + ":clean_exits"] = all(code == 0 for code in record["exits"].values())
            continue
        checks[name + ":mod_active"] = record["server_mod_active"]
        if name == "mod-module-refused":
            checks[name + ":map_refused"] = not record["server_map_loaded"]
            checks[name + ":reason_logged"] = any("active mod supplies game code" in w for w in record["server_warnings"])
            checks[name + ":server_survived"] = record["alive_at_close"].get("server", False)
            continue
        checks[name + ":join_as_expected"] = record.get("joined") == record["expect_join"] and "error" not in record
        expected_module = mod_module if name == "mod-module-admitted" else base_module
        checks[name + ":server_module"] = Path(record["server_module"]).resolve() == Path(expected_module).resolve() \
            if record["server_module"] else False
        if record["expect_join"]:
            checks[name + ":client_module"] = Path(record["client_module"]).resolve() == Path(expected_module).resolve() \
                if record["client_module"] else False
            checks[name + ":clean_exits"] = all(code == 0 for code in record["exits"].values())
        else:
            checks[name + ":refusal_reported"] = record["client_pure_refused"]
            checks[name + ":server_clean_exit"] = record["exits"].get("server") == 0
            checks[name + ":client_survived"] = record["alive_at_close"].get("client", False)
    match = by_name.get("content-match")
    if match is not None:
        checks["content-match:package_on_pure_list"] = match["client_package_pure_status"] == "on"
        checks["content-match:extra_off_pure_list"] = match["client_extra_pure_status"] in ("off", "absent")
    status = "pass" if checks and all(checks.values()) else "fail"
    identity = {name: runtime / name for name in (
        f"openQ4-client_x64{SUFFIX}", f"openQ4-ded_x64{SUFFIX}", f"baseoq4/{MODULE}", "baseoq4/pak0.pk4",
        "baseoq4/pak1.pk4")}
    report = {"status": status, "checks": checks, "results": results,
              "runtime": str(runtime), "basepath": str(args.basepath),
              "source": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip(),
              "runtime_sha256": {name: digest(path) for name, path in identity.items() if path.is_file()}}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"status": status, "failed": [k for k, v in checks.items() if not v]}, indent=2))
    return 0 if status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
