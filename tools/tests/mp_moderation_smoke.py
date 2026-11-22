#!/usr/bin/env python3
"""Qualify server moderation on a running dedicated server (MP-18).

One pure dedicated server with two bots and one hidden client play Deathmatch
over loopback. Later connections are each a fresh hidden client: one from the
banned address, one after the ban is lifted, and one that tries to share the
address. (A client that was refused or removed sits in a dialog whose network
loop waits for a click, so the same process cannot simply connect again.) The
server's save folder starts with a `banlist.txt` in Quake 4's binary form.

The run must show:

* the Quake 4 list read once, its GUID entry kept as a comment, and the file
  rewritten as text;
* `status` naming the client's address and the bot;
* a text mute silencing the player, telling them so, surviving a reconnect and
  being lifted;
* the chat flood window silencing a player who passes it, and letting them
  speak again afterwards;
* the name policy refusing a reserved name whatever its colours;
* a range that is too wide refused, and a range ban listed and lifted;
* a ban removing the player, refusing their next connection with the reason,
  written to `banlist.txt`, and lifted by `unban`;
* `net_serverMaxClientsPerAddress` refusing a second client from one address;
* `kick <slot>` typed at the console recorded like every other removal;
* a vote to mute passing while kick votes are forbidden, and silencing its
  target for `g_voteMuteMinutes`;
* `serverShutdown` announced, called off, and then waiting for the match to
  end before the server says goodbye and its process exits by itself;
* every one of those events as a line of valid JSON in `logs/admin.ndjson`.

Peers bind 127.0.0.1 only, so a runtime at a new path raises no firewall prompt.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import time
from pathlib import Path

from mp_map_rotation_smoke import ACTIVE, MAP_OPENED, Role
from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""
# english_code.lang and english_openq4.lang
TEXT_BANNED = "You have been banned from this server."
TEXT_TOO_MANY = "Too many players are connected from your address."
TEXT_MUTED = "You are muted on this server. Your chat reaches nobody."
TEXT_FLOOD = "You are sending messages too fast. Wait a few seconds."
TEXT_UNMUTED = "You are no longer muted."
TEXT_KICKED = "Kicked from server."
TEXT_SHUTDOWN_MATCH = "The server will shut down when this match ends."
TEXT_SHUTDOWN_IN = "The server will shut down in "
TEXT_SHUTDOWN_OFF = "The server shutdown was called off."
TEXT_SHUTDOWN_NOW = "The server is shutting down."
BANLIST_HEADER = "openq4-banlist 1"


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def quake4_banlist(entries: list[tuple[str, str]]) -> bytes:
    """Quake 4's banlist.txt: a name and a GUID, each a 32-bit length and that many bytes."""
    data = b""
    for name, guid in entries:
        for field in (name, guid):
            raw = field.encode("ascii")
            data += struct.pack("<i", len(raw)) + raw
    return data


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--port", type=int, default=28151)
    parser.add_argument("--client-port", type=int, default=28152)
    parser.add_argument("--second-port", type=int, default=28153)
    parser.add_argument("--timeout", type=int, default=120, help="maximum seconds for any one step")
    parser.add_argument("--startup-timeout", type=int, default=300)
    parser.add_argument("--skip-address-limit", action="store_true",
                        help="leave out the second client and the per-address connection limit")
    args = parser.parse_args()
    runtime, output, assets = args.runtime_dir.resolve(), args.output_dir.resolve(), args.basepath.resolve()
    verify_runtime_is_current(runtime)
    paths = {
        "engine": runtime / f"openQ4-client_x64{SUFFIX}",
        "dedicated": runtime / f"openQ4-ded_x64{SUFFIX}",
        "game_mp": runtime / "baseoq4" / ("game-mp_x64" + (".dll" if os.name == "nt" else ".so")),
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
        "checks": [], "commands": {}, "exit_codes": {}, "failures": [], "admin_log": [],
    }
    names = ("server", "client", "banned", "returning", "kicked") + (() if args.skip_address_limit else ("second",))
    games = {role: output / role / "baseoq4" for role in names}
    for game in games.values():
        game.mkdir(parents=True)
    banlist = games["server"] / "banlist.txt"
    banlist.write_bytes(quake4_banlist([("OldCheater", "ABCDEFGHIJK")]))
    admin_log = games["server"] / "logs/admin.ndjson"
    roles = {name: Role(name, game) for name, game in games.items()}
    server, client = roles["server"], roles["client"]
    # Game settings are set once the game module is up; the command line is read before it loads.
    # A short flood silence keeps the run short; the window itself keeps its defaults.
    write_script(games["server"] / "rot_server_start.cfg", [
        # With no frag or time limit the server idles in warmup, which keeps the moderation stages
        # still: a live match is abandoned and restarted when its only human reconnects. The
        # shutdown stage starts a real match. Two bots give that match its frags.
        "set bot_minPlayers 0", "set bot_skill 3", "set g_chatFloodWait 4", "set g_gameReviewPause 5", "addbot", "addbot",
        "echo ROT_SERVER_READY",
        *server.start_lines()])
    for name in names[1:]:
        write_script(games[name] / f"rot_{name}_start.cfg", roles[name].start_lines())
    common = {
        "fs_basepath": str(assets), "fs_game": "baseoq4", "com_gameMode": "MP", "si_gameType": "DM",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1", "s_noSound": "1",
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

    def become_player(label: str, role: Role) -> None:
        deadline = time.monotonic() + args.timeout
        while not ACTIVE.search(step(role, label + ": client state", ["openq4_reportMPState"])):
            if time.monotonic() >= deadline:
                raise RuntimeError(label + ": the client never became a player")
            time.sleep(0.75)

    def start_client(name: str, player: str, port: int) -> None:
        """A fresh hidden client that connects at once; its console pump starts with it, joined or refused."""
        game = games[name]
        launch(name, dict(common | window, fs_savepath=str(game.parent), fs_devpath=str(game.parent),
                          net_port=str(port), ui_name=player), ["+connect", address, "+exec", f"rot_{name}_start.cfg"])

    def retire(name: str) -> None:
        """Quit a client on purpose, so the wait loop does not take its exit for a crash."""
        roles[name].inject(["quit"])
        report["exit_codes"][name] = processes[name].wait(timeout=30)
        del processes[name]

    def events() -> list[dict]:
        """Every line of the admin log, which must each be one JSON object."""
        parsed = []
        for line in admin_log.read_text(encoding="utf-8").splitlines() if admin_log.is_file() else []:
            parsed.append(json.loads(line))
        return parsed

    def say(label: str, lines: list[str], gap: int = 700) -> str:
        """Chat from the client, one line every `gap` ms, and what its console showed for it."""
        actions = []
        for line in lines:
            actions += ["say " + line, f"waitMsec {gap}"]
        return step(client, label, actions + ["waitMsec 1200"])

    started = time.monotonic()
    address = f"127.0.0.1:{args.port}"
    try:
        launch("server", dict(common, fs_savepath=str(games["server"].parent), fs_devpath=str(games["server"].parent),
                              net_port=str(args.port), net_serverDedicated="1", si_pure="1", si_name="MP18 moderation",
                              si_maxPlayers="8", si_warmup="0", si_timeLimit="0", si_fragLimit="0", si_mapCycle="",
                              si_allowVoting="1", si_voteFlags="0",
                              g_autoExecAfterMapLoad="rot_server_start.cfg"), ["+spawnServer", "mp/q4dm1"])
        wait("server startup", lambda t: "\nROT_SERVER_READY" in t["server"], args.startup_timeout)

        start_client("client", "ModPlayer", args.client_port)
        # the pump starts with the process; the multiplayer module, and its commands, arrive with the map
        wait("client map load", lambda t: MAP_OPENED in t["client"], args.startup_timeout)
        become_player("first join", client)

        # --- the Quake 4 list is read once and rewritten as text ---------------
        startup = read_log(server.log)
        check("the Quake 4 ban list is read once and its GUID entry reported",
              "ban list: 1 Quake 4 GUID ban(s) in banlist.txt are kept as comments" in startup, startup[-400:])
        text = banlist.read_text(encoding="utf-8")
        check("banlist.txt is rewritten as text", text.startswith(BANLIST_HEADER + "\n"), text[:200])
        check("the GUID entry survives as a comment", '# Quake 4 GUID ban, never enforced (clients send no GUID): "OldCheater" ABCDEFGHIJK' in text, text)
        check("a GUID entry bans nobody", re.search(r"^ban ", text, re.MULTILINE) is None, text)

        # --- status ------------------------------------------------------------
        status = step(server, "status", ["status"])
        row = re.search(r"^\s*(\d+)\s+\d+\s+\S+\s+no\s+127\.0\.0\.1\s+-\s+ModPlayer\s*$", status, re.MULTILINE)
        check("status names the client's address", row is not None, status)
        check("status marks the bot", re.search(r"^\s*\d+\s+\d+\s+\S+\s+no\s+bot\s+-\s+\S", status, re.MULTILINE) is not None, status)
        check("status counts the player and both bots", "\n3 client(s)" in status, status)
        slot = row[1] if row else "0"
        remote = step(client, "moderation commands on a client", ["ban 0", "mute 0", "status", "banlist"])
        check("a client's console cannot ban, mute or list", remote.count("only the server runs this") == 4, remote)

        # --- chat and the text mute --------------------------------------------
        heard = say("chat before a mute", ["plainline1"])
        check("a chat line comes back to its sender", "plainline1" in heard, heard)
        muted = step(server, "mute", ["mute ModPlayer"])
        check("mute reports the player", "muted ModPlayer (until unmute)" in muted, muted)
        wait("the mute notice", lambda t: TEXT_MUTED in t["client"], 20)
        heard = say("chat while muted", ["hiddenline2"])
        check("a muted player's line reaches nobody", "hiddenline2" not in heard.replace("say hiddenline2", ""), heard)
        status = step(server, "status while muted", ["status"])
        check("status shows the mute", re.search(r"\byes\s+127\.0\.0\.1\s+-\s+ModPlayer", status) is not None, status)

        opened = read_log(client.log).count(MAP_OPENED)
        client.inject(["reconnect"])
        wait("reconnect while muted: client map load", lambda t: t["client"].count(MAP_OPENED) > opened, args.startup_timeout)
        become_player("reconnect while muted", client)
        heard = say("chat after a reconnect", ["evadeline3"])
        check("the mute follows the address across a reconnect", "evadeline3" not in heard.replace("say evadeline3", ""), heard)
        refused = step(server, "mute edge cases", ["mute NoSuchPlayer", "mute ModPlayer nonsense", "unmute ModPlayer", "unmute ModPlayer"])
        check("mute names a missing player", "mute: no connected player matches 'NoSuchPlayer'" in refused, refused)
        check("mute refuses a time it cannot read", "mute: 'nonsense' is not a time" in refused, refused)
        check("unmute lifts the mute once", "unmuted ModPlayer" in refused and "unmute: that player is not muted" in refused, refused)
        wait("the unmute notice", lambda t: TEXT_UNMUTED in t["client"], 20)
        heard = say("chat after the unmute", ["backline4"])
        check("an unmuted player is heard again", "backline4" in heard.replace("say backline4", ""), heard)

        # --- the flood window: four lines in four seconds, then silence --------
        mark = len(read_log(client.log))
        heard = say("flood", [f"floodline{n}" for n in range(1, 7)], gap=600)
        shown = [n for n in range(1, 7) if f"floodline{n}" in heard.replace(f"say floodline{n}", "")]
        check("four lines pass and the rest are dropped", shown == [1, 2, 3, 4], f"shown {shown}\n{heard}")
        check("the sender is told once", read_log(client.log)[mark:].count(TEXT_FLOOD) == 1, heard)
        step(client, "wait out the silence", ["waitMsec 4500"])
        heard = say("chat after the silence", ["calmline7"])
        check("the silence ends", "calmline7" in heard.replace("say calmline7", ""), heard)

        # --- the name policy ---------------------------------------------------
        step(client, "ask for a reserved name", ['set ui_name "^1Ad^2min"', "waitMsec 2500"])
        status = step(server, "status after the name change", ["status"])
        check("a reserved name is refused whatever its colours", re.search(r"127\.0\.0\.1\s+-\s+\^1Ad\^2min_\s*$", status, re.MULTILINE) is not None, status)
        step(client, "take the name back", ['set ui_name "ModPlayer"', "waitMsec 2500"])

        # --- ranges ------------------------------------------------------------
        ranges = step(server, "range bans", [
            "ban 10.0.0.0/4", "ban nonsense", "ban 192.0.2.77/24 perm test range", "ban [2001:db8:1::5/48] 2h",
            "ban 2001:db8:1::5/48", "banlist", "unban 192.0.2.0/24", "unban [2001:db8:1::]/48", "unban 203.0.113.1", "banlist"])
        check("a range that is too wide is refused", "ban: refused; a range may be no wider than /8 (IPv4) or /16 (IPv6)" in ranges, ranges)
        check("text that is neither a player nor an address is refused", "ban: 'nonsense' is neither a connected player nor an address" in ranges, ranges)
        check("an IPv6 address outside brackets is refused, not misread",
              "ban: '2001' is neither a connected player nor an address; write an IPv6 address in brackets" in ranges, ranges)
        check("a range is stored masked with its reason", "banned 192.0.2.0/24 (permanent): test range" in ranges, ranges)
        check("an IPv6 range in brackets takes a time", "banned 2001:db8:1::/48 (2h 0m)" in ranges, ranges)
        check("banlist shows both", re.search(r"^\s*1\s+192\.0\.2\.0/24\s+permanent\s+: test range", ranges, re.MULTILINE) is not None
              and re.search(r"^\s*2\s+2001:db8:1::/48\s+\dh \d+m", ranges, re.MULTILINE) is not None and "\n2 ban(s)" in ranges, ranges)
        check("unban takes a range in either family", "unbanned 192.0.2.0/24" in ranges and "unbanned 2001:db8:1::/48" in ranges, ranges)
        check("unban names what it cannot find", "unban: no ban matches '203.0.113.1'" in ranges, ranges)
        check("the list is empty again", ranges.rstrip().endswith("0 ban(s)"), ranges[-200:])

        # --- a ban removes the player and refuses the next connection ----------
        mark = len(read_log(client.log))
        banned = step(server, "ban the player", [f"ban {slot} 10m smoke test"])
        check("ban reports the address, the time and the reason", "banned 127.0.0.1/32 (10m): smoke test" in banned, banned)
        wait("the banned player is dropped", lambda t: TEXT_BANNED in t["client"][mark:], 30)
        check("the player is told they were banned", True)
        status = step(server, "status after the ban", ["status", "banlist"])
        check("the player is gone", "ModPlayer" not in status.partition("ban(s)")[0].partition("client(s)")[0], status)
        check("banlist names the player and the reason", re.search(r"^\s*1\s+127\.0\.0\.1/32\s+\d+m\s+ModPlayer : smoke test", status, re.MULTILINE) is not None, status)
        text = banlist.read_text(encoding="utf-8")
        check("the ban is written to banlist.txt", re.search(r'^ban address 127\.0\.0\.1/32 \d+ \d+ "ModPlayer" "smoke test"$', text, re.MULTILINE) is not None, text)
        check("the comment is kept across the rewrite", "OldCheater" in text, text)

        # the removed client sits in its dialog; a fresh one tries from the same address
        retire("client")
        server_mark = len(read_log(server.log))
        start_client("banned", "ModPlayer", args.client_port)
        wait("the banned address is refused", lambda t: TEXT_BANNED in t["banned"], args.startup_timeout)
        refusal = read_log(server.log)[server_mark:]
        check("the server refuses the banned address and says why",
              "refused a connection from 127.0.0.1: banned (127.0.0.1/32)" in refusal, refusal[-400:])
        # the client's connection attempts repeat; the log takes one line a minute for an address
        step(server, "let the client retry", ["waitMsec 4000"])
        check("a refused address is logged once a minute", sum(event["event"] == "refused" for event in events()) == 1)
        retire("banned")

        lifted = step(server, "unban the player", ["unban 1"])
        check("unban takes a row of the list", "unbanned 127.0.0.1/32" in lifted, lifted)
        check("banlist.txt no longer holds it", re.search(r"^ban ", banlist.read_text(encoding="utf-8"), re.MULTILINE) is None)
        returning = roles["returning"]
        start_client("returning", "ModPlayer", args.client_port)
        wait("join after the unban: client map load", lambda t: MAP_OPENED in t["returning"], args.startup_timeout)
        become_player("join after the unban", returning)
        check("the unbanned address plays again", True)

        # --- one address, one client -------------------------------------------
        if not args.skip_address_limit:
            step(server, "limit clients for an address", ["set net_serverMaxClientsPerAddress 1"])
            start_client("second", "SecondPlayer", args.second_port)
            wait("the second client is refused", lambda t: TEXT_TOO_MANY in t["second"], args.startup_timeout)
            check("a second client from one address is refused and told why", True)
            status = step(server, "status with the limit", ["status"])
            check("only the first client holds the address", "SecondPlayer" not in status and "ModPlayer" in status, status)
            step(server, "lift the limit", ["set net_serverMaxClientsPerAddress 0"])
            retire("second")

        # --- a kick typed at the console ---------------------------------------
        kicked = roles["kicked"]
        start_client("kicked", "KickMe", args.second_port)
        wait("the player to kick joins", lambda t: MAP_OPENED in t["kicked"], args.startup_timeout)
        become_player("the player to kick", kicked)
        status = step(server, "status before the kick", ["status"])
        row = re.search(r"^\s*(\d+)\s+\d+\s+\S+\s+no\s+127\.0\.0\.1\s+-\s+KickMe\s*$", status, re.MULTILINE)
        check("the second player is listed", row is not None, status)
        step(server, "kick from the console", [f"kick {row[1] if row else 99}"])
        wait("the kicked player is told", lambda t: TEXT_KICKED in t["kicked"], 30)
        kicks = [event for event in events() if event["event"] == "kick"]
        check("a console kick is recorded with who and where", len(kicks) == 2 and kicks[-1].get("by") == "console"
              and kicks[-1].get("name") == "KickMe" and kicks[-1].get("address") == "127.0.0.1" and "reason" not in kicks[-1],
              json.dumps(kicks))
        retire("kicked")

        # --- a vote to mute, with kick votes forbidden -------------------------
        step(server, "forbid kick votes", ["set si_voteFlags 16", "rescanSI"])
        status = step(server, "status before the vote", ["status"])
        bot = re.search(r"^\s*\d+\s+\d+\s+\S+\s+no\s+bot\s+-\s+(\S+)\s*$", status, re.MULTILINE)
        check("the bot is listed and not muted", bot is not None, status)
        bot_name = bot[1] if bot else "nobody"
        step(returning, "vote to mute the bot", ["callvote mute " + bot_name, "waitMsec 3500"])
        status = step(server, "status after the vote", ["status"])
        check("a lone voter's mute vote passes while kick votes are forbidden",
              re.search(r"\byes\s+bot\s+-\s+" + re.escape(bot_name) + r"\s*$", status, re.MULTILINE) is not None, status)
        check("the bot was silenced, not removed", "\n3 client(s)" in status, status)
        voted = [event for event in events() if event["event"] == "mute" and event.get("by") == "vote"]
        check("the vote's mute is recorded with its length", len(voted) == 1 and voted[0].get("name") == bot_name
              and voted[0].get("seconds") == 600, json.dumps(voted))

        # --- graceful shutdown --------------------------------------------------
        idle = step(server, "shutdown state", ["serverShutdown"])
        check("no shutdown waits by default", "no shutdown is waiting" in idle, idle)
        mark = len(read_log(returning.log))
        timed = step(server, "shutdown in five minutes", ["serverShutdown 5m update tonight", "serverShutdown", "serverShutdown soon"])
        check("a timed shutdown is scheduled", "server shutdown: in 5m" in timed and "the server shuts down in 5m" in timed, timed)
        check("a word that is no time is refused", "serverShutdown: 'soon' is not match, now, cancel or a time" in timed, timed)
        told = wait("players are told when and why", lambda t: TEXT_SHUTDOWN_IN + "5m" in t["returning"][mark:]
                    and "update tonight" in t["returning"][mark:], 20)["returning"][mark:]
        check("players are told when and why", TEXT_SHUTDOWN_IN + "5m" in told)
        called_off = step(server, "call the shutdown off", ["serverShutdown cancel", "serverShutdown cancel"])
        check("a shutdown is called off once", "server shutdown: called off" in called_off
              and "serverShutdown: no shutdown is waiting" in called_off, called_off)
        wait("players are told it is off", lambda t: TEXT_SHUTDOWN_OFF in t["returning"][mark:], 20)
        check("players are told it is off", True)

        # a match that can end: a frag limit, which takes effect when the map restarts
        live_matches = read_log(server.log).count("MP match phase: 2 -> 3")
        step(server, "start a match that can end", ["set si_fragLimit 2", "serverMapRestart"])
        wait("the match goes live", lambda t: t["server"].count("MP match phase: 2 -> 3") > live_matches, 120)
        become_player("the last match", returning)
        check("a real match is running before the shutdown is asked for", True)
        mark = len(read_log(returning.log))
        step(server, "shut down after the match", ["serverShutdown match"])
        wait("players are told the match is the last", lambda t: TEXT_SHUTDOWN_MATCH in t["returning"][mark:], 20)
        check("players are told the match is the last", True)
        wait("the match ends and the server stops", lambda t: "\nserver shutdown: stopping" in t["server"], 600)
        ended = read_log(server.log)
        check("the server waited for the match", ended.rindex("server shutdown: when this match ends") < ended.rindex("server shutdown: stopping"))
        # the dedicated server's process quits by itself; take it out of the watch before it does
        report["exit_codes"]["server"] = processes.pop("server").wait(timeout=60)
        check("the server process exits by itself", report["exit_codes"]["server"] == 0, str(report["exit_codes"]["server"]))
        check("players get the goodbye before the server goes", TEXT_SHUTDOWN_NOW in read_log(returning.log)[mark:])
        after = read_log(server.log).rpartition("server shutdown: stopping")[2]
        check("nothing starts after the goodbye: no next map, no new match", "map database: next map" not in after
              and "MP match phase: 0 -> 1" not in after and MAP_OPENED not in after, after[:800])
        retire("returning")

        # --- the admin log -----------------------------------------------------
        logged = events()
        report["admin_log"] = logged
        kinds = [event["event"] for event in logged]
        check("every admin log line is one JSON object with a time", all(
            re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", event.get("time", "")) for event in logged) and len(logged) > 0)
        check("the log holds each kind of event in order",
              [kind for kind in kinds if kind != "kick"] == [
                  "mute", "unmute", "flood", "ban", "ban", "unban", "unban", "ban", "refused", "unban", "mute",
                  "shutdown_scheduled", "shutdown_cancelled", "shutdown_scheduled", "shutdown"], " ".join(kinds))
        ban = next((event for event in logged if event["event"] == "ban" and event.get("range") == "127.0.0.1/32"), {})
        check("a ban records who, what and until when", ban.get("by") == "console" and ban.get("name") == "ModPlayer"
              and ban.get("reason") == "smoke test" and "expires" in ban, json.dumps(ban))
        kick = next((event for event in logged if event["event"] == "kick"), {})
        check("the removal that follows a ban is recorded with the address",
              kick.get("address") == "127.0.0.1" and kick.get("reason") == "banned" and kick.get("name") == "ModPlayer", json.dumps(kick))
        mute = next((event for event in logged if event["event"] == "mute"), {})
        check("a mute records the player and the address", mute.get("name") == "ModPlayer" and mute.get("address") == "127.0.0.1", json.dumps(mute))
        flood = next((event for event in logged if event["event"] == "flood"), {})
        check("a chat flood is recorded", flood.get("by") == "server" and flood.get("seconds") == 4, json.dumps(flood))
        scheduled = [event for event in logged if event["event"] == "shutdown_scheduled"]
        check("a scheduled shutdown records when and why", len(scheduled) == 2 and scheduled[0].get("when") == "time"
              and scheduled[0].get("seconds") == 300 and scheduled[0].get("reason") == "update tonight"
              and scheduled[1].get("when") == "match", json.dumps(scheduled))

        if any(report["exit_codes"].values()):
            report["failures"].append(f"unclean exits: {report['exit_codes']}")
    except (RuntimeError, OSError, subprocess.TimeoutExpired, IndexError, ValueError) as error:
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
    print(json.dumps({key: report[key] for key in ("status", "exit_codes", "failures")}, indent=2))
    return int(bool(report["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
