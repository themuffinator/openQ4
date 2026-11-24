#!/usr/bin/env python3
"""Qualify player identity on a running dedicated server (MP-17).

A pure dedicated server plays Team DM with two bots and three hidden clients
over loopback: Alice and Carol with identity on (the default), Bob with it off.
Later connections are each a fresh hidden client; Alice's and Carol's returns
use their first client's save folder, so the same identity.key. The run must
show:

* Alice and Carol proving an identity, each with an identity.key made in their
  own save folder, and Bob none; `status` listing both identities and a dash;
* Bob, and only Bob, told a ghost code;
* Alice leaving a live match on the Strogg team, returning with the same
  identity under ui_team Marine, and the server putting her back on Strogg;
* Bob leaving and coming back with `ghost <code>`, and the server restoring
  his place;
* the match's end rating Alice and Carol in ranking.txt and `rankings`;
* the match in Carol's own profile/history.ndjson;
* `ban id:<Carol>` removing Carol, written to banlist.txt as an identity ban,
  her return refused once she proves the identity, and `unban id:<Carol>`
  lifting it; the admin log naming the identity;
* `identityReset` asking for confirm, and `identityReset confirm` replacing the
  key.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path

from mp_map_rotation_smoke import MAP_OPENED, Role
from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
# english_code.lang and english_openq4.lang
TEXT_BANNED = "You have been banned from this server."
TEXT_GHOST = "keep your team and score: ghost "
TEXT_RESTORED = "Welcome back: your team and score are restored."
TEXT_RESET_ASK = "Type identityReset confirm to do it."
TEXT_RESET_DONE = "Your identity was reset"
MAP = "mp/q4dm1"
IDENTITY = re.compile(r"client (\d+) \(([^\n]*?)\^0\) has identity ([0-9a-f]{16}) on this server")
RETURNED = re.compile(r"client (\d+) \(([^\n]*?)\^0\) returned: team (-?\d+), score (-?\d+)")
STATE = re.compile(r"MP_STATE [^\n]*phase=(\d+)")


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--port", type=int, default=28171)
    parser.add_argument("--first-client-port", type=int, default=28172)
    parser.add_argument("--timeout", type=int, default=120, help="maximum seconds for any one step")
    parser.add_argument("--startup-timeout", type=int, default=300)
    parser.add_argument("--time-limit", type=int, default=30,
                        help="the match's minutes; the run ends the match sooner with a frag limit once the returns are done")
    args = parser.parse_args()
    runtime, output, assets = args.runtime_dir.resolve(), args.output_dir.resolve(), args.basepath.resolve()
    verify_runtime_is_current(runtime)
    paths = {
        "engine": runtime / f"openQ4-client_x64{SUFFIX}",
        "dedicated": runtime / f"openQ4-ded_x64{SUFFIX}",
        "game_mp": runtime / "baseoq4" / ("game_x64" + (".dll" if os.name == "nt" else ".so")),
    }
    if any(not path.is_file() for path in paths.values()) or not (assets / "q4base").is_dir():
        parser.error("a complete staged runtime and the retail PK4 assets are required")
    output.mkdir(parents=True, exist_ok=False)
    report = {
        "status": "fail",
        "source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "runtime_dir": str(runtime), "runtime_sha256": {key: digest(path) for key, path in paths.items()},
        "checks": [], "commands": {}, "exit_codes": {}, "failures": [], "identities": {},
    }
    # Alice's and Carol's returns share their first client's save folder (and identity.key).
    folders = {"server": "server", "alice": "alice", "carol": "carol", "bob": "bob", "alice2": "alice", "bob2": "bob2",
               "carol2": "carol"}
    games = {}
    for role, folder in folders.items():
        games[role] = output / folder / "baseoq4"
        games[role].mkdir(parents=True, exist_ok=True)
    roles = {name: Role(name, game) for name, game in games.items()}
    for name in ("alice2", "carol2"):
        roles[name].log = games[name] / f"logs/openq4-{name}.log"
    server = roles["server"]
    write_script(games["server"] / "rot_server_start.cfg", [
        "set bot_minPlayers 0", "set bot_skill 2", "set g_gameReviewPause 5", "set g_ranking 1", "set g_identity 1",
        "set g_reconnectRestore 300", "addbot", "addbot", "echo ROT_SERVER_READY", *server.start_lines()])
    for name in folders:
        if name != "server":
            write_script(games[name] / f"rot_{name}_start.cfg", roles[name].start_lines())
    common = {
        "fs_basepath": str(assets), "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": "Team DM",
        "logFile": "2", "developer": "1", "s_noSound": "1",
        "in_mouse": "0", "in_joystick": "0", "com_skipLoadingContinue": "1", "net_allowCheats": "1",
        "net_LANServer": "1", "net_clientDownload": "0", "net_enableIPv4": "1", "net_enableIPv6": "0",
        "net_ip": "127.0.0.1", "g_autoExecAfterMapLoadDelayMs": "500",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
    }
    window = {
        "ui_autoJoin": "1", "ui_spectate": "Play", "r_renderApi": "gl", "r_fullscreen": "0", "r_fullscreenDesktop": "0",
        "r_borderless": "0", "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_mode": "-1",
        "r_customWidth": "640", "r_customHeight": "360", "r_windowWidth": "640", "r_windowHeight": "360",
        "com_maxfps": "60", "r_swapInterval": "0", "ui_retained": "0",
    }
    processes, streams = {}, []
    address = f"127.0.0.1:{args.port}"
    ports = iter(range(args.first_client_port, args.first_client_port + 20))

    def launch(role: str, cvars: dict[str, str], tail: list[str]) -> None:
        if len(cvars) + 1 > 64:
            raise RuntimeError("launch exceeds the engine's startup command capacity")
        executable = paths["dedicated"] if role == "server" else paths["engine"]
        command = [str(executable)] + [part for key, value in cvars.items() for part in ("+set", key, value)] + tail
        report["commands"][role] = command
        stream = (output / f"{role}-console.log").open("w", encoding="utf-8")
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

    def logs() -> dict[str, str]:
        return {name: read_log(role.log) for name, role in roles.items()}

    def wait(label: str, predicate, seconds: int | None = None) -> dict[str, str]:
        until = time.monotonic() + (seconds or args.timeout)
        while time.monotonic() < until:
            texts = logs()
            if re.search(r"FATAL ERROR|Unknown command", "\n".join(texts.values())):
                raise RuntimeError("engine error during " + label)
            ended = [name for name, process in processes.items() if process.poll() is not None]
            if ended:
                raise RuntimeError(f"{', '.join(ended)} ended during {label}")
            if predicate(texts):
                return texts
            time.sleep(0.25)
        raise RuntimeError("timeout during " + label)

    def step(role: Role, label: str, actions: list[str]) -> str:
        begin, end = role.inject(actions)
        text = wait(label, lambda t: "\n" + end in t[role.name])[role.name]
        return text.rpartition("\n" + end)[0].rpartition("\n" + begin)[2]

    def check(name: str, passed: bool, detail: str = "") -> None:
        report["checks"].append({"name": name, "passed": bool(passed), "detail": detail[-600:]})
        print(("ok   " if passed else "FAIL ") + name, flush=True)
        if not passed:
            report["failures"].append(name)

    def join(name: str, player: str, label: str, extra: dict[str, str], expect: str = MAP_OPENED) -> None:
        """A fresh hidden client; started again (twice at most) if it stops in renderer start-up."""
        game = games[name]
        log_name = roles[name].log.relative_to(game).as_posix()
        for attempt in (1, 2, 3):
            launch(name, dict(common | window | extra, fs_savepath=str(game.parent), fs_devpath=str(game.parent),
                              net_port=str(next(ports)), ui_name=player, logFileName=log_name),
                   ["+connect", address, "+exec", f"rot_{name}_start.cfg"])
            try:
                wait(label, lambda t: expect in t[name], args.startup_timeout if attempt == 3 else min(120, args.startup_timeout))
                return
            except RuntimeError as error:
                if attempt == 3 or not str(error).startswith("timeout") or "\nRenderer bootstrap:" in read_log(roles[name].log):
                    raise
                stalled = processes.pop(name)
                stalled.kill()
                stalled.wait()
                roles[name].log.unlink(missing_ok=True)
                report.setdefault("client_restarts", []).append(name)

    def retire(name: str) -> None:
        roles[name].inject(["quit"])
        report["exit_codes"][name] = processes[name].wait(timeout=30)
        del processes[name]

    def identities(text: str) -> dict[str, str]:
        return {player: fingerprint for _slot, player, fingerprint in IDENTITY.findall(text)}

    def phase() -> int:
        found = STATE.findall(step(server, "server state", ["openq4_reportMPState"]))
        return int(found[-1]) if found else -1

    started = time.monotonic()
    try:
        launch("server", dict(common, fs_savepath=str(games["server"].parent), fs_devpath=str(games["server"].parent),
                              logFileName="logs/openq4.log", net_port=str(args.port), net_serverDedicated="1", si_pure="1",
                              si_name="MP17 identity", si_maxPlayers="8", si_warmup="0", si_timeLimit=str(args.time_limit),
                              si_fragLimit="0", si_autobalance="0", si_mapCycle="", g_mapRotation="0",
                              g_autoExecAfterMapLoad="rot_server_start.cfg"), ["+spawnServer", MAP])
        wait("server startup", lambda t: "\nROT_SERVER_READY" in t["server"], args.startup_timeout)

        join("alice", "Alice", "Alice joins", {"ui_team": "Strogg", "ui_identity": "1"})
        join("carol", "Carol", "Carol joins", {"ui_team": "Marine", "ui_identity": "1"})
        join("bob", "Bob", "Bob joins", {"ui_team": "Marine", "ui_identity": "0"})
        texts = wait("identities proved", lambda t: {"Alice", "Carol"} <= set(identities(t["server"])))
        found = identities(texts["server"])
        alice_id, carol_id = found["Alice"], found["Carol"]
        report["identities"] = found
        check("Alice and Carol prove an identity, two different ones", alice_id != carol_id, json.dumps(found))
        check("Bob, with ui_identity 0, proves none", "Bob" not in found, json.dumps(found))
        keys = {name: (output / name / "baseoq4" / "identity.key") for name in ("alice", "carol", "bob")}
        check("identity.key is made in Alice's and Carol's save folders and not Bob's",
              keys["alice"].is_file() and keys["carol"].is_file() and not keys["bob"].is_file()
              and keys["alice"].read_text(encoding="utf-8").startswith("openq4-identity 1\n"))
        # Bob's player spawns a moment after his client has the map
        deadline = time.monotonic() + args.timeout
        status = step(server, "status", ["status"])
        while re.search(r" - +Bob", status) is None and time.monotonic() < deadline:
            time.sleep(1)
            status = step(server, "status", ["status"])
        check("status lists both identities and a dash for Bob",
              alice_id in status and carol_id in status and re.search(r" - +Bob", status) is not None, status)
        bob = wait("Bob's ghost code", lambda t: TEXT_GHOST in t["bob"])["bob"]
        code = re.search(TEXT_GHOST + r"([A-Z]{6})", bob)
        check("Bob is told a ghost code", code is not None, bob[-400:])
        check("Alice and Carol are told none", TEXT_GHOST not in read_log(roles["alice"].log) + read_log(roles["carol"].log))

        deadline = time.monotonic() + args.startup_timeout
        while phase() != 3:
            if time.monotonic() > deadline:
                raise RuntimeError("the match never went live")
            time.sleep(1)

        # --- Alice leaves and comes back on the wrong team -----------------------
        retire("alice")
        join("alice2", "Alice", "Alice returns", {"ui_team": "Marine", "ui_identity": "1"})
        texts = wait("Alice restored", lambda t: re.search(r"\(Alice\^0\) returned: team 1,", t["server"]) is not None)
        again = [fingerprint for _slot, player, fingerprint in IDENTITY.findall(texts["server"]) if player == "Alice"]
        check("Alice returns with the same identity", len(again) >= 2 and again[-1] == alice_id, json.dumps(again))
        check("the server puts Alice back on Strogg", RETURNED.search(texts["server"]) is not None)
        wait("Alice is told", lambda t: TEXT_RESTORED in t["alice2"])
        check("Alice is told her place is back", True)

        # --- Bob leaves and comes back with his ghost code --------------------------
        retire("bob")
        join("bob2", "Bob", "Bob returns", {"ui_team": "Strogg", "ui_identity": "0"})
        if code is not None:
            step(roles["bob2"], "Bob's ghost", [f"ghost {code.group(1).lower()}", "waitMsec 1500"])
            texts = wait("Bob restored", lambda t: TEXT_RESTORED in t["bob2"])
            check("Bob's ghost code gives his place back", re.search(r"\(Bob\^0\) returned: team 0,", texts["server"]) is not None)
            again = step(roles["bob2"], "Bob's ghost again", [f"ghost {code.group(1)}", "waitMsec 1500"])
            texts = wait("the code is spent", lambda t: "That return code is not known here" in t["bob2"])
            check("a ghost code is claimed once", True)

        # --- the match ends and rates Alice and Carol ------------------------------
        # A frag limit applies at once; the bots' next frag ends the match.
        rated_before = read_log(server.log).count("ranking: ")
        step(server, "end the match", ["set si_fragLimit 1", "rescanSI"])
        texts = wait("the match ends", lambda t: t["server"].count("ranking: ") >= rated_before + 2, args.startup_timeout)
        moved = re.findall(r"ranking: (Alice|Carol)\^0 team_dm [0-9.]+ \(([+-][0-9.]+)\)", texts["server"])[-2:]
        check("the match's result moves Alice's and Carol's ratings, opposite ways (they are on opposite teams)",
              len(moved) == 2 and float(moved[0][1]) == -float(moved[1][1]) and float(moved[0][1]) != 0.0, json.dumps(moved))
        ranking = games["server"] / "ranking.txt"
        table = ranking.read_text(encoding="utf-8") if ranking.is_file() else ""
        check("ranking.txt rates Alice and Carol in Team DM",
              table.startswith("openq4-ranking 1\n") and f"rating team_dm {alice_id} " in table and f"rating team_dm {carol_id} " in table
              and "Bob" not in table, table)
        listing = step(server, "rankings", ["rankings"])
        check("rankings lists them", alice_id in listing and carol_id in listing and "2 rated player(s)" in listing, listing)
        history = output / "carol" / "baseoq4" / "profile/history.ndjson"
        wait("Carol's history", lambda t: history.is_file())
        lines = [json.loads(line) for line in history.read_text(encoding="utf-8").splitlines() if line.strip()]
        check("the match is in Carol's own history", len(lines) >= 1 and lines[-1].get("gametype") == "Team DM"
              and lines[-1].get("result") in ("won", "lost", "drew") and lines[-1].get("name") == "Carol", json.dumps(lines))

        # one match is enough: without the limit the server plays on instead of restarting every frag
        step(server, "no frag limit", ["set si_fragLimit 0", "rescanSI"])

        # --- an identity ban ----------------------------------------------------
        step(server, "ban Carol", [f"ban id:{carol_id} 10m smoke test", "waitMsec 1500"])
        # a removed client waits in its disconnect dialog; it does not exit
        wait("Carol removed", lambda t: TEXT_BANNED in t["carol"])
        banlist = (games["server"] / "banlist.txt").read_text(encoding="utf-8")
        check("the identity ban is written to banlist.txt", f"ban identity {carol_id} " in banlist and '"smoke test"' in banlist, banlist)
        banned = processes.pop("carol")
        banned.kill()  # Only the process this test launched; its dialog takes no console input.
        banned.wait()
        join("carol2", "Carol", "Carol tries again", {"ui_team": "Marine", "ui_identity": "1"})
        texts = wait("Carol refused", lambda t: f"identity: {carol_id} is banned" in t["server"])
        wait("Carol told", lambda t: TEXT_BANNED in t["carol2"])
        check("Carol's return is refused once she proves the banned identity", True)
        retire("carol2")
        unban = step(server, "unban Carol", [f"unban id:{carol_id}", "banlist"])
        check("unban id: lifts the identity ban", f"unbanned id:{carol_id}" in unban and carol_id not in
              (games["server"] / "banlist.txt").read_text(encoding="utf-8"), unban)
        admin_log = games["server"] / "logs/admin.ndjson"
        events = [json.loads(line) for line in admin_log.read_text(encoding="utf-8").splitlines()] if admin_log.is_file() else []
        check("the admin log names the identity it banned and lifted",
              any(e.get("event") == "ban" and e.get("identity") == carol_id for e in events)
              and any(e.get("event") == "unban" and e.get("identity") == carol_id for e in events), json.dumps(events[-4:]))

        # --- identityReset ------------------------------------------------------
        before = keys["alice"].read_bytes()
        asked = step(roles["alice2"], "identityReset", ["identityReset"])
        check("identityReset asks for confirm and changes nothing", TEXT_RESET_ASK in asked and keys["alice"].read_bytes() == before, asked)
        done = step(roles["alice2"], "identityReset confirm", ["identityReset confirm"])
        check("identityReset confirm replaces the key", TEXT_RESET_DONE in done and keys["alice"].read_bytes() != before, done)
        retire("alice2")
        retire("bob2")
        server.inject(["quit"])
        report["exit_codes"]["server"] = processes.pop("server").wait(timeout=30)
    except Exception as error:  # noqa: BLE001 - the report records the failure
        report["failures"].append(f"exception: {error}")
        print(f"FAIL {error}", flush=True)
    finally:
        for name, process in list(processes.items()):
            if process.poll() is None:
                process.kill()  # Only the processes this test launched.
                process.wait()
        for stream in streams:
            stream.close()
    report["seconds"] = round(time.monotonic() - started, 1)
    report["status"] = "pass" if not report["failures"] else "fail"
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"status": report["status"], "failures": report["failures"], "seconds": report["seconds"]}, indent=2), flush=True)
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
