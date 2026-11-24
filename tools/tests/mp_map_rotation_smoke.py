#!/usr/bin/env python3
"""Qualify the map database's rotation, queue, pick and per-map settings (MP-13).

One pure dedicated server with two bots and one hidden client play Deathmatch
over loopback. The server's `mapdb.txt` lists a rotation of five stock maps,
bounds one of them to eight players or more, gives one a frag limit of its
own, and keeps one out of votes and one out of the queue.

The run must show:

* the override file read, checked and listed by `mapdb`;
* the rotation passing over the map that three players are too few for, and
  the client reading the same next map from the server info;
* the map's own setting in force while it is played and gone afterwards;
* a map vote for a map the database keeps out of votes refused;
* the queue (`mymap`) refusing what it must, keeping one entry a player,
  and being played before the rotation continues;
* the end-of-match pick (`mappick`) offered when a match ends, and the
  client's pick loaded;
* the intermission quiet until `g_intermissionAnnounce 1`, then naming the
  next map once during the scoreboard (not again at the change), the map it
  named loading, and each award its reel read out reaching the client (MP-12).

Peers bind 127.0.0.1 only, so a runtime at a new path raises no firewall prompt.
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

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
ACTIVE = re.compile(r"MP_PLAYER slot=\d+ bot=0 [^\n]*ingame=1")
MAP_OPENED = "\n--------- Map Initialization ---------"
ROTATION = ("mp/q4dm1", "mp/q4dm2", "mp/q4dm3", "mp/q4dm4", "mp/q4dm5")
DATABASE = """// mp_map_rotation_smoke.py
map mp/q4dm2 { players 8 16 }
map mp/q4dm3 {
	set si_fragLimit 7
	set si_map mp/q4dm1      // refused: a map cannot choose the map
}
map mp/q4dm4 { vote 0 }
map mp/q4dm5 { queue 0 }
map mp/not_installed_here { players 2 4 }
rotation { mp/q4dm1 mp/q4dm2 mp/q4dm3 mp/q4dm4 mp/q4dm5 }
"""
# english_openq4.lang; a map name or a list follows each.
TEXT_NOT_QUEUEABLE = "This server keeps that map out of the queue: "
TEXT_UNKNOWN = "This server does not have the map: nosuchmap"
TEXT_CURRENT = "That map is being played now: "
TEXT_QUEUED = "Queued for the next map change: "
TEXT_QUEUE_LIST = "Map queue: 1. "
TEXT_PICKED = "You picked: "
TEXT_PICK_OFFER = "Choose the next map with mappick 1, 2 or 3: "
TEXT_NEXT = "Next map: "
TEXT_VOTE_REFUSED = "That map is not available on this server."


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


class Role:
    """One process's console, driven without input.

    An immutable pump script runs a replaceable selector four times a second.
    Each step retires itself through a cvar before it acts, so a selector that
    is read twice never repeats a step, and a selector that Windows refuses to
    open for a moment only costs one turn of the pump.

    There is exactly one pump a process. The command buffer survives a change
    of map on the server and on the client, so the pump is never started
    again: `vstr` appends its text, and with two pumps the second would read
    the selector before the first one's step had retired itself.
    """

    def __init__(self, name: str, game: Path):
        self.name, self.game = name, game
        self.log = game / "logs/openq4.log"
        self.sequence = 0
        self.selector = game / f"rot_{name}_select.cfg"
        write_script(self.selector, ["// waiting for the first step"])
        write_script(game / f"rot_{name}_pump.cfg",
                     [f"exec {self.selector.name}", "waitMsec 250", f"vstr rot_{name}_pump"])
        self.write_hook()

    def start_lines(self) -> list[str]:
        """What the first map load runs: the first step's cvar, then the pump."""
        return [f'set rot_{self.name}_cmd_1 "exec rot_{self.name}_step_1.cfg"',
                f'set rot_{self.name}_pump "exec rot_{self.name}_pump.cfg"',
                f"echo ROT_{self.name.upper()}_MAP_LOADED", f"exec rot_{self.name}_pump.cfg"]

    def write_hook(self, extra: tuple[str, ...] = ()) -> None:
        """What a later map load runs once armed: mark the load. The pump is still running."""
        write_script(self.game / f"rot_{self.name}_hook.cfg",
                     [f"echo ROT_{self.name.upper()}_MAP_LOADED", *extra])

    def arm(self) -> str:
        # The hook is one-shot: it clears itself after it ran.
        return f"set g_autoExecAfterMapLoad rot_{self.name}_hook.cfg"

    def loads(self) -> int:
        return read_log(self.log).count(f"\nROT_{self.name.upper()}_MAP_LOADED")

    def inject(self, actions: list[str]) -> tuple[str, str]:
        self.sequence += 1
        number = self.sequence
        begin = f"ROT_{self.name.upper()}_BEGIN_{number}"
        end = f"ROT_{self.name.upper()}_END_{number}"
        write_script(self.game / f"rot_{self.name}_step_{number}.cfg", [
            f'set rot_{self.name}_cmd_{number} ""',
            f'set rot_{self.name}_cmd_{number + 1} "exec rot_{self.name}_step_{number + 1}.cfg"',
            "echo " + begin, *actions, "echo " + end])
        write_script(self.selector, [f"vstr rot_{self.name}_cmd_{number}"])
        return begin, end


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--port", type=int, default=28131)
    parser.add_argument("--client-port", type=int, default=28132)
    parser.add_argument("--timeout", type=int, default=120, help="maximum seconds for any one step")
    parser.add_argument("--startup-timeout", type=int, default=300)
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
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"],
                                                     cwd=ROOT, text=True)),
        "runtime_dir": str(runtime), "runtime_sha256": {key: digest(path) for key, path in paths.items()},
        "checks": [], "maps": [], "commands": {}, "exit_codes": {}, "failures": [],
    }
    games = {role: output / role / "baseoq4" for role in ("server", "client")}
    for game in games.values():
        game.mkdir(parents=True)
    (games["server"] / "mapdb.txt").write_text(DATABASE, encoding="utf-8")
    roles = {name: Role(name, game) for name, game in games.items()}
    server, client = roles["server"], roles["client"]
    # Game settings are set once the game module is up; the command line is read before it loads.
    # Two bots added outright, not a fill target: the player count is what the bounds read.
    write_script(games["server"] / "rot_server_start.cfg", [
        "set g_mapRotation 1", "set g_mapQueue 1", "set g_mapPick 0", "set bot_minPlayers 0", "set bot_skill 3",
        "set g_gameReviewPause 20", "addbot", "addbot", "echo ROT_SERVER_READY", *server.start_lines()])
    write_script(games["client"] / "rot_client_start.cfg", client.start_lines())
    common = {
        "fs_basepath": str(assets), "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": "DM",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1", "s_noSound": "1",
        "in_mouse": "0", "in_joystick": "0", "com_skipLoadingContinue": "1", "net_allowCheats": "1",
        "net_LANServer": "1", "net_clientDownload": "0", "net_enableIPv4": "1", "net_enableIPv6": "0",
        "net_ip": "127.0.0.1", "g_autoExecAfterMapLoadDelayMs": "500",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
    }
    processes, streams = {}, []

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
        """Run console actions on a role and return what they printed."""
        begin, end = role.inject(actions)
        text = wait(label, lambda t: "\n" + end in t[role.name])[role.name]
        return text.rpartition("\n" + end)[0].rpartition("\n" + begin)[2]

    def check(name: str, passed: bool, detail: str = "") -> None:
        report["checks"].append({"name": name, "passed": bool(passed), "detail": detail[-600:]})
        print(("ok   " if passed else "FAIL ") + name, flush=True)
        if not passed:
            report["failures"].append(name)

    def poll(role: Role, label: str, actions: list[str], pattern: str, seconds: int = 30) -> str:
        """Repeat a step until its output matches; the server info follows the game by up to a second."""
        deadline = time.monotonic() + seconds
        while True:
            text = step(role, label, actions)
            if re.search(pattern, text) or time.monotonic() >= deadline:
                return text
            time.sleep(0.75)

    def change_map(label: str, trigger: list[str] | None, loaded_extra: tuple[str, ...] = (),
                   seconds: int | None = None) -> str:
        """Arm the server's map-load hook, let the map change, and return the server log it wrote."""
        before = server.loads()
        opened = read_log(client.log).count(MAP_OPENED)
        mark = len(read_log(server.log))
        server.write_hook(loaded_extra)
        server.inject([server.arm(), *(trigger or [])])
        wait(label + ": server map load", lambda t: server.loads() > before, seconds)
        wait(label + ": client map load", lambda t: t["client"].count(MAP_OPENED) > opened, seconds)
        deadline = time.monotonic() + args.timeout
        while not ACTIVE.search(step(client, label + ": client state", ["openq4_reportMPState"])):
            if time.monotonic() >= deadline:
                raise RuntimeError(label + ": the client never came back into the game")
            time.sleep(0.75)
        return read_log(server.log)[mark:]

    def current_map(label: str) -> str:
        text = step(server, label, ["mapdb rotation"])
        found = re.search(r"^> (\S+)", text, re.MULTILINE)
        name = found[1] if found else ""
        report["maps"].append(name)
        return name

    started = time.monotonic()
    try:
        launch("server", dict(common, fs_savepath=str(games["server"].parent), fs_devpath=str(games["server"].parent),
                              net_port=str(args.port), net_serverDedicated="1", si_pure="1", si_name="MP13 rotation",
                              si_maxPlayers="8", si_warmup="0", si_timeLimit="0", si_fragLimit="20", si_mapCycle="",
                              si_allowVoting="1", si_voteFlags="0",
                              g_autoExecAfterMapLoad="rot_server_start.cfg"), ["+spawnServer", ROTATION[0]])
        wait("server startup", lambda t: "\nROT_SERVER_READY" in t["server"], args.startup_timeout)
        startup = read_log(server.log)
        check("the override file is read at start", "map database: mapdb.txt changed 4 maps and lists a rotation of 5" in startup, startup[-400:])
        check("a map that is not installed is reported, not fatal", "map database: mapdb.txt names 1 maps that are not installed" in startup)

        listed = step(server, "database listing", ["mapdb check", "mapdb list", "mapdb settings"])
        check("mapdb check accepts the file",
              "mapdb: mapdb.txt is valid: 4 maps changed, 1 not installed, a rotation of 5 (0 not installed)" in listed, listed)
        check("the listing shows the bounds", re.search(r"mp/q4dm2\s+8-16\s+R V Q", listed) is not None, listed)
        check("the listing shows vote and queue flags",
              re.search(r"mp/q4dm4\s+any\s+R - Q", listed) is not None and re.search(r"mp/q4dm5\s+any\s+R V -", listed) is not None, listed)
        check("stock tourney arenas default to two to four players", re.search(r"mp/q4tourney1\s+2-4\s", listed) is not None, listed)
        check("maps with no free-for-all mode default to four or more", re.search(r"mp/q4ctf1\s+4\+\s", listed) is not None, listed)
        maps = re.search(r"\n(\d+) maps; \* changed by mapdb\.txt", listed)
        check("every stock multiplayer map is discovered", maps is not None and int(maps[1]) >= 20, listed[-200:])
        check("a map may not choose the map", re.search(r'mp/q4dm3\s+si_map "mp/q4dm1"\s+refused: a map cannot change it', listed) is not None, listed)

        launch("client", dict(common, fs_savepath=str(games["client"].parent), fs_devpath=str(games["client"].parent),
                              net_port=str(args.client_port), ui_name="RotPlayer", ui_autoJoin="1", ui_spectate="Play",
                              r_renderApi="gl", r_fullscreen="0", r_fullscreenDesktop="0", r_borderless="0",
                              r_borderlessDefaultMigrated="1", r_hiddenWindow="1", r_mode="-1", r_customWidth="640",
                              r_customHeight="360", r_windowWidth="640", r_windowHeight="360", com_maxfps="60",
                              r_swapInterval="0", ui_retained="0", g_autoExecAfterMapLoad="rot_client_start.cfg"),
               ["+connect", f"127.0.0.1:{args.port}"])
        wait("client map load", lambda t: client.loads() > 0, args.startup_timeout)
        deadline = time.monotonic() + args.timeout
        while not ACTIVE.search(step(client, "client state", ["openq4_reportMPState"])):
            if time.monotonic() >= deadline:
                raise RuntimeError("the client never became a player")
            time.sleep(0.75)

        # --- bounds: three players are too few for q4dm2 -----------------------
        rotation = poll(server, "rotation with three players", ["mapdb rotation"], r", 3 players,", 60)
        check("two bots and the client make three players", ", 3 players, DM" in rotation, rotation)
        check("the rotation marks the map outside its bounds", re.search(r"mp/q4dm2\s+outside its player bounds", rotation) is not None, rotation)
        check("the rotation passes over it", "next: mp/q4dm3 (rotation)" in rotation, rotation)
        told = poll(client, "client reads the next map", ["mapdb next"], r"next map: mp/q4dm3")
        check("the client reads the same next map from the server info", "next map: mp/q4dm3" in told, told)
        refused = step(client, "vote for a map kept out of votes", ["clientCallVote map mp/q4dm4", "waitMsec 1500"])
        check("a vote for a map kept out of votes is refused", TEXT_VOTE_REFUSED in refused, refused)

        written = change_map("rotation", ["serverNextMap"])
        check("the rotation loads the map that fits", "map database: next map mp/q4dm3 (rotation, 3 players)" in written, written)
        check("the map's own setting is applied before it loads", 'map database: mp/q4dm3: si_fragLimit "7" (was "20")' in written, written)
        check("the refused setting is reported", "map database: mp/q4dm3: si_map is not set (a map cannot change it)" in written, written)
        check("the server is on the rotation's map", current_map("map after the rotation") == "mp/q4dm3")
        limit = step(server, "setting in force", ["si_fragLimit", "si_map"])
        check("the map's setting is in force", '"si_fragLimit" is:"7"' in limit, limit)
        check("the refused setting did not move the map", '"si_map" is:"mp/q4dm3"' in limit, limit)

        # --- the queue ---------------------------------------------------------
        queue = step(client, "queue requests", [
            "mymap q4dm5", "waitMsec 1300", "mymap nosuchmap", "waitMsec 1300", "mymap mp/q4dm3", "waitMsec 1300",
            "mymap mp/q4dm1", "waitMsec 1300", "mymap q4dm4", "waitMsec 1300", "mymap", "waitMsec 1300"])
        check("a map kept out of the queue is refused", TEXT_NOT_QUEUEABLE in queue, queue)
        check("an unknown map is refused by name", TEXT_UNKNOWN in queue, queue)
        check("the current map is refused", TEXT_CURRENT in queue, queue)
        check("a queued map is announced", queue.count(TEXT_QUEUED) == 2, queue)
        check("the queue lists one entry a player", TEXT_QUEUE_LIST in queue and "2. " not in queue.rpartition(TEXT_QUEUE_LIST)[2].splitlines()[0], queue)
        held = step(server, "server queue", ["mapdb queue"])
        check("the second request replaced the first", re.search(r"^\s*1\. mp/q4dm4\s+client \d+ RotPlayer", held, re.MULTILINE) is not None
              and "2. " not in held, held)
        told = poll(client, "client reads the queued map", ["mapdb next"], r"next map: mp/q4dm4")
        check("the client reads the queued map as next", "next map: mp/q4dm4" in told, told)
        flood = step(client, "queue flood", ["mymap mp/q4dm1", "mymap mp/q4dm5", "mymap mp/q4dm1", "waitMsec 1500"])
        check("requests faster than one a second are dropped", flood.count(TEXT_QUEUED) + flood.count(TEXT_NOT_QUEUEABLE) <= 1, flood)
        step(client, "queue the map again", ["waitMsec 1300", "mymap q4dm4", "waitMsec 1300"])

        written = change_map("queued map", ["serverNextMap"])
        check("the queued map is played before the rotation", re.search(r"map database: next map mp/q4dm4 \(queued by RotPlayer, 3 players\)", written) is not None, written)
        check("the previous map's setting is given back before the next map loads",
              'map database: mp/q4dm3: si_fragLimit back to "20"' in written, written)
        check("the server is on the queued map", current_map("map after the queue") == "mp/q4dm4")
        after = step(server, "after the queued map", ["si_fragLimit", "mapdb queue", "mapdb settings"])
        check("the previous map's setting went back", '"si_fragLimit" is:"20"' in after, after)
        check("the queue is empty again", "the map queue is empty" in after, after)
        check("no setting is applied on a map without any", "0 applied now for no map" in after, after)

        # --- the end-of-match pick ---------------------------------------------
        step(server, "enable the pick", ["set g_mapPick 1", "set si_fragLimit 1", "rescanSI"])
        offered = wait("the match ends and the pick is offered", lambda t: "map database: pick offered:" in t["server"], 240)
        candidates = re.findall(r"map database: pick offered:((?: \S+)+)", offered["server"])[-1].split()
        report["pick"] = candidates
        check("three maps are offered", len(candidates) == 3 and len(set(candidates)) == 3, " ".join(candidates))
        check("the first is the rotation's own next map", candidates[:1] == ["mp/q4dm5"], " ".join(candidates))
        check("none is the current map or outside its bounds", not {"mp/q4dm4", "mp/q4dm2"} & set(candidates), " ".join(candidates))
        picked = poll(client, "the client picks", ["mapdb next"], r"pick: \S+;\S+;\S+")
        check("the client reads the pick from the server info", "pick: " + ";".join(candidates) in picked, picked)
        picked = step(client, "mappick", ["mappick 2", "waitMsec 1300", "mappick 9"])
        check("the offer reaches the client", TEXT_PICK_OFFER in read_log(client.log))
        check("the pick is acknowledged", TEXT_PICKED in picked, picked)
        check("a pick outside the offer prints the usage", "usage: mappick <1-3>" in picked, picked)
        written = change_map("picked map", None, ("set g_mapPick 0", "set si_fragLimit 0", "rescanSI"), 120)
        check("the picked map is chosen with its vote",
              f"map database: next map {candidates[1]} (picked with 1 votes, 3 players)" in written, written)
        check("the server is on the picked map", current_map("map after the pick") == candidates[1])
        check("the next map is announced to players", TEXT_NEXT in read_log(client.log))

        # --- the intermission names the next map and reads the awards (MP-12) ---
        check("the intermission is quiet until the server turns it on",
              "map database: the intermission names" not in read_log(server.log))
        before_next = read_log(client.log).count(TEXT_NEXT)
        mark = len(read_log(server.log))
        # As with the pick: the map-load hook is armed once the match has ended, since an
        # armed hook also runs on the map being played and would take the limit back.
        # A fresh match ended by its first frag gives that player the Perfect award (a
        # kill and no death), so the reel has something to read.
        step(server, "enable the intermission",
             ["set g_intermissionAnnounce 1", "set si_fragLimit 1", "rescanSI", "serverMapRestart"])
        wait("the match ends and the intermission names the next map",
             lambda t: "map database: the intermission names " in t["server"][mark:], 240)
        # The hook runs as soon as it is armed, during the review: it takes the frag limit
        # back but leaves the reel running; the reel is turned off once the map changed.
        change_map("intermission", None, ("set si_fragLimit 0", "rescanSI"), 120)
        step(server, "intermission off", ["set g_intermissionAnnounce 0"])
        written = read_log(server.log)[mark:]
        named = re.findall(r"map database: the intermission names (\S+)", written)
        chosen = re.findall(r"map database: next map (\S+) \(", written)
        report["intermission"] = {"named": named, "chosen": chosen,
                                  "awards": re.findall(r"^intermission: ([^\n]+)$", written, re.MULTILINE)}
        check("the intermission names the next map once, and it is the map that loads",
              len(named) == 1 and chosen[-1:] == named, json.dumps(report["intermission"]))
        heard = read_log(client.log)
        check("players hear the next map during the scoreboard, and not again at the change",
              heard.count(TEXT_NEXT) == before_next + 1 and heard.rfind(TEXT_NEXT) < heard.rfind(MAP_OPENED), heard[-600:])
        # Every player without a kill has Critical Failure, and the first frag's player a
        # Perfect; others depend on how the match went.
        awards = report["intermission"]["awards"]
        check(f"the reel reads out the match's awards and each reaches the players ({len(awards)} this match)",
              len(awards) >= 1 and all(f"server^0: {award}" in heard for award in awards), "\n".join(awards))

        client.inject(["quit"])
        report["exit_codes"]["client"] = processes["client"].wait(timeout=30)
        server.inject(["quit"])
        report["exit_codes"]["server"] = processes["server"].wait(timeout=30)
        if any(report["exit_codes"].values()):
            report["failures"].append(f"unclean exits: {report['exit_codes']}")
    except (RuntimeError, OSError, subprocess.TimeoutExpired, IndexError) as error:
        report["failures"].append(str(error) or type(error).__name__)
    finally:
        for role in roles.values():
            try:
                write_script(role.game / f"rot_{role.name}_hook.cfg", ["quit"])
                write_script(role.selector, ["quit"])
            except OSError:
                pass
        for name, process in processes.items():
            if process.poll() is None:
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    report["failures"].append(name + " required forced termination")
            report["exit_codes"].setdefault(name, process.returncode)
        for stream in streams:
            stream.close()
    report["elapsed_seconds"] = round(time.monotonic() - started, 2)
    if {key: digest(path) for key, path in paths.items()} != report["runtime_sha256"]:
        report["failures"].append("runtime changed during qualification")
    report["status"] = "fail" if report["failures"] else "pass"
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "maps", "exit_codes", "failures")}, indent=2))
    return int(bool(report["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
