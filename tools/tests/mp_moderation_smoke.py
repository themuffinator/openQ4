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
* the name policy refusing a reserved name whatever its colours, under a
  five-byte colour code as well, and removing a caret left at a name's end;
* a muted player held to the name the mute found them under;
* an admin's login going on across a change of map without a second login;
* the host listing the admins, ending one's login, changing the password (which
  logs everyone out and is taken at once) and turning admin logins off;
* a console command sent by rcon logged with the address it came from, and its
  line arriving as it was typed;
* a range that is too wide refused, and a range ban listed and lifted, with an
  IPv6 range typed without brackets, one whose digits the console's lexer used
  to damage, and a reason that begins with a digit all read as they were typed;
* a `banlist.txt` the server cannot use left as it was, with the bans in memory
  still in force and nothing written over the file;
* a ban removing the player, refusing their next connection with the reason,
  written to `banlist.txt`, and lifted by `unban`;
* `net_serverMaxClientsPerAddress` refusing a second client from one address;
* `kick <slot>` typed at the console recorded like every other removal;
* a player with the server's admin password (`net_serverAdminPassword`) logging in after
  a wrong password is refused, then muting, kicking and banning connected
  players from their own console, the ban cut to `g_adminMaxBanMinutes`, with a
  bot, the admin's own slot and everything that takes an address refused, each
  action in the admin log beside the admin's name, and the right gone after
  `admin logout`;
* a vote to mute passing while kick votes are forbidden, and silencing its
  target for `g_voteMuteMinutes`;
* `serverShutdown` announced, called off, and then waiting for the match to
  end before the server says goodbye and its process exits by itself;
* the admin and referee passwords given on the server's command line redacted in
  its log and found in none of its files;
* every one of those events as a line of valid JSON in `logs/admin.ndjson`;
* `g_matchLog` appending the run's stream to a `logs/match.ndjson` that already
  held one, begun with the UTC time and holding the match's kills and standings;
* `banlist.txt` never left beside a half-written copy.

Peers bind loopback addresses only, so a runtime at a new path raises no firewall
prompt. The players an admin acts on connect from 127.0.0.2: a ban is of an
address, and one of 127.0.0.1 would remove the admin too (which the server
refuses).
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
TEXT_ADMIN_NOW = "You are now a server admin."
TEXT_ADMIN_GONE = "You are no longer a server admin."
TEXT_ADMIN_REFUSED = "The server refused the admin login."
TEXT_NOT_ADMIN = "You are not logged in as a server admin."
TEXT_ADMIN_DONE = "Done: "
TEXT_ADMIN_NO = "Refused: that cannot be done to that player."
ADMIN_PASSWORD = "smoke-admin-password-1"
NEW_ADMIN_PASSWORD = "smoke-admin-password-2"
REF_PASSWORD = "smoke-referee-password-1"
RCON_PASSWORD = "smoke-rcon-password-1"
FIRST_MAP = "mp/q4dm1"
SECOND_MAP = "mp/q4dm2"		# the admin stage changes to it
# where an rcon line came from, as the engine prints an address: loopback is "localhost"
RCON_SOURCE = re.compile(r"(127[.]0[.]0[.]1|localhost):[0-9]+")
OTHER_ADDRESS = "127.0.0.2"
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
    parser.add_argument("--skip-admin", action="store_true",
                        help="leave out the admin stage, which needs a second loopback address (127.0.0.2)")
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
        "checks": [], "commands": {}, "exit_codes": {}, "failures": [], "admin_log": [],
    }
    names = (("server", "client", "banned", "returning", "kicked") + (() if args.skip_address_limit else ("second",))
             + (() if args.skip_admin else ("target", "nuisance")))
    games = {role: output / role / "baseoq4" for role in names}
    for game in games.values():
        game.mkdir(parents=True)
    banlist = games["server"] / "banlist.txt"
    banlist.write_bytes(quake4_banlist([("OldCheater", "ABCDEFGHIJK")]))
    admin_log = games["server"] / "logs/admin.ndjson"
    # an earlier run's stream: the match log must keep it and write after it
    match_log = games["server"] / "logs/match.ndjson"
    match_log.parent.mkdir(parents=True)
    match_log.write_text('{"v":1,"seq":0,"t":0,"type":"stream","game":"openQ4","source":"server","map":"mp/earlier",'
                         '"gameType":"DM","utc":"2026-01-01T00:00:00Z","players":[]}\n', encoding="utf-8")
    roles = {name: Role(name, game) for name, game in games.items()}
    server, client = roles["server"], roles["client"]
    # Game settings are set once the game module is up; the command line is read before it loads.
    # A short flood silence keeps the run short; the window itself keeps its defaults.
    write_script(games["server"] / "rot_server_start.cfg", [
        # With no frag or time limit the server idles in warmup, which keeps the moderation stages
        # still: a live match is abandoned and restarted when its only human reconnects. The
        # shutdown stage starts a real match. Two bots give that match its frags.
        "set bot_minPlayers 0", "set bot_skill 3", "set g_chatFloodWait 4", "set g_gameReviewPause 5", "set g_matchLog 1",
        "set g_matchEventStream 1",
        "addbot", "addbot",
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

    def start_client(name: str, player: str, port: int, ip: str = "127.0.0.1") -> None:
        """A fresh hidden client that connects at once; its console pump starts with it, joined or refused."""
        game = games[name]
        launch(name, dict(common | window, fs_savepath=str(game.parent), fs_devpath=str(game.parent),
                          net_port=str(port), net_ip=ip, ui_name=player),
               ["+connect", address, "+exec", f"rot_{name}_start.cfg"])

    def join(name: str, player: str, port: int, label: str, expect: str = MAP_OPENED, ip: str = "127.0.0.1") -> None:
        """Start a fresh client and wait until its log shows `expect`: the map, or why it was refused.

        A hidden client now and then stops in the renderer's start-up, before it has asked the server
        for anything, when the machine's graphics card is busy with other work. That is no part of what
        this run qualifies, so such a client is closed and started again, twice at most, and the report
        names it each time.
        """
        for attempt in (1, 2, 3):
            start_client(name, player, port, ip=ip)
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
                print(f"note {name}: the client stopped in renderer start-up and was started again", flush=True)

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
                              si_allowVoting="1", si_voteFlags="0", net_serverAdminPassword=ADMIN_PASSWORD, g_refPassword=REF_PASSWORD,
                              net_serverRemoteConsolePassword=RCON_PASSWORD,
                              g_autoExecAfterMapLoad="rot_server_start.cfg"), ["+spawnServer", FIRST_MAP])
        wait("server startup", lambda t: "\nROT_SERVER_READY" in t["server"], args.startup_timeout)

        # the pump starts with the process; the multiplayer module, and its commands, arrive with the map
        join("client", "ModPlayer", args.client_port, "client map load")
        become_player("first join", client)

        # --- the Quake 4 list is read once and rewritten as text ---------------
        startup = read_log(server.log)
        check("the Quake 4 ban list is read once and its GUID entry reported",
              "ban list: 1 Quake 4 GUID ban(s) in banlist.txt are kept as comments" in startup, startup[-400:])
        text = banlist.read_text(encoding="utf-8")
        check("banlist.txt is rewritten as text", text.startswith(BANLIST_HEADER + "\n"), text[:200])
        check("the GUID entry survives as a comment", '# Quake 4 GUID ban, never enforced (clients send no GUID): "OldCheater" ABCDEFGHIJK' in text, text)
        check("a GUID entry bans nobody", re.search(r"^ban ", text, re.MULTILINE) is None, text)

        # --- passwords on the command line -------------------------------------
        # the log echoes the parsed command line (developer 1, logFile 2); both passwords were on it
        check("the server's log redacts the admin, referee and rcon passwords it was started with",
              startup.count("<private command redacted>") == 3
              and not any(secret in startup for secret in (ADMIN_PASSWORD, REF_PASSWORD, RCON_PASSWORD)),
              startup[startup.find("Command line (parsed)"):][:1500])
        kept = [str(path.relative_to(games["server"])) for path in games["server"].rglob("*") if path.is_file()
                and not path.name.startswith("rot_")
                and any(secret.encode() in path.read_bytes() for secret in (ADMIN_PASSWORD, REF_PASSWORD, RCON_PASSWORD))]
        check("no password is in any file the server wrote", not kept, " ".join(kept))

        # --- status ------------------------------------------------------------
        status = step(server, "status", ["status"])
        row = re.search(r"^\s*(\d+)\s+\d+\s+\S+\s+no\s+127\.0\.0\.1\s+-\s+ModPlayer\s*$", status, re.MULTILINE)
        check("status names the client's address", row is not None, status)
        check("status marks the bot", re.search(r"^\s*\d+\s+\d+\s+\S+\s+no\s+bot\s+-\s+\S", status, re.MULTILINE) is not None, status)
        check("status counts the player and both bots", "\n3 client(s)" in status, status)
        slot = row[1] if row else "0"
        remote = step(client, "server commands on a client", ["status", "banlist", "unban 1", "serverShutdown now"])
        check("a client's console cannot list, lift a ban or stop the server", remote.count("only the server runs this") == 4, remote)
        # ban, mute and gameKick typed on a client are an admin's request; this player never logged in
        mark = len(read_log(client.log))
        step(client, "an admin's commands without a login", ["mute 0", "waitMsec 900", "ban 0", "waitMsec 900", "gameKick 0", "waitMsec 900"])
        wait("the server answers each request", lambda t: t["client"][mark:].count(TEXT_NOT_ADMIN) >= 3, 20)
        check("without a login the server mutes, bans and kicks for nobody", True)
        status = step(server, "status after the refused requests", ["status"])
        check("the refused requests changed nothing", "\n3 client(s)" in status and re.search(r"\byes\s+bot\b", status) is None
              and not events(), status)

        # --- chat and the text mute --------------------------------------------
        heard = say("chat before a mute", ["plainline1"])
        check("a chat line comes back to its sender", "plainline1" in heard, heard)
        muted = step(server, "mute", ["mute ModPlayer"])
        check("mute reports the player", "muted ModPlayer (until unmute)" in muted, muted)
        wait("the mute notice", lambda t: TEXT_MUTED in t["client"], 20)
        heard = say("chat while muted", ["hiddenline2"])
        check("a muted player's line reaches nobody", "hiddenline2" not in heard.replace("say hiddenline2", ""), heard)
        step(client, "a muted player asks for another name", ['set ui_name "Sneaky words"', "waitMsec 2500"])
        status = step(server, "status while muted", ["status"])
        check("status shows the mute", re.search(r"\byes\s+127\.0\.0\.1\s+-\s+ModPlayer", status) is not None, status)
        check("a muted player is held to the name the mute found them under", "Sneaky" not in status, status)
        step(client, "the muted player's own name again", ['set ui_name "ModPlayer"', "waitMsec 1500"])

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
        # 900 ms apart: four lines inside four seconds, the fifth inside the window and so dropped, the
        # sixth inside the silence. Closer together, a stalled frame on a busy machine puts two lines
        # inside the engine's own 500 ms spacing and one of them is dropped before the window sees it.
        heard = say("flood", [f"floodline{n}" for n in range(1, 7)], gap=900)
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
        step(client, "ask for a reserved name under a five-byte colour code", ['set ui_name "^c900Admin"', "waitMsec 2500"])
        status = step(server, "status after the long colour code", ["status"])
        check("a reserved name is refused under a five-byte colour code", re.search(r"127\.0\.0\.1\s+-\s+\^c900Admin_\s*$", status, re.MULTILINE) is not None, status)
        step(client, "ask for a name that ends in a caret", ['set ui_name "Tail^c"', "waitMsec 2500"])
        status = step(server, "status after the trailing caret", ["status"])
        check("a caret that would swallow the text after a name is removed", re.search(r"127\.0\.0\.1\s+-\s+Tailc\s*$", status, re.MULTILINE) is not None, status)
        step(client, "take the name back", ['set ui_name "ModPlayer"', "waitMsec 2500"])

        # --- ranges ------------------------------------------------------------
        # The commands read their line as it was typed. The console's lexer cut "192.0.2.77/24" and
        # "2h" in two, split an IPv6 address at its colons, read "3rd" as a number and a word, and took
        # "1e" in "2001:1ef0::" for a float, losing the "f" after it.
        ranges = step(server, "range bans", [
            "ban 10.0.0.0/4", "ban nonsense", "ban 192.0.2.77/24 perm test range", "ban [2001:db8:1::5/48] 2h",
            "ban 2001:db8:1::5/48 3rd offence", "ban [2001:1ef0::/32] 1h", "ban 3rd", "banlist",
            "unban 192.0.2.0/24", "unban [2001:db8:1::]/48", "unban 2001:1ef0::/32", "unban 203.0.113.1", "banlist"])
        check("a range that is too wide is refused", "ban: refused; a range may be no wider than /8 (IPv4) or /16 (IPv6)" in ranges, ranges)
        check("text that is neither a player nor an address is refused", "ban: 'nonsense' is neither a connected player nor an address" in ranges
              and "ban: '3rd' is neither a connected player nor an address" in ranges, ranges)
        check("a range is stored masked with its reason", "banned 192.0.2.0/24 (permanent): test range" in ranges, ranges)
        check("an IPv6 range in brackets takes a time", "banned 2001:db8:1::/48 (2h 0m)" in ranges, ranges)
        check("an IPv6 range without brackets is read whole, and a reason that begins with a digit is the reason",
              "banned 2001:db8:1::/48 (permanent): 3rd offence" in ranges, ranges)
        check("an address the console's lexer used to damage is the one that is banned", "banned 2001:1ef0::/32 (1h 0m)" in ranges, ranges)
        check("banlist shows all three", re.search(r"^\s*1\s+192\.0\.2\.0/24\s+permanent\s+: test range", ranges, re.MULTILINE) is not None
              and re.search(r"^\s*2\s+2001:db8:1::/48\s+permanent\s+: 3rd offence", ranges, re.MULTILINE) is not None
              and re.search(r"^\s*3\s+2001:1ef0::/32\s+(1h 0m|\d+m)", ranges, re.MULTILINE) is not None and "\n3 ban(s)" in ranges, ranges)
        check("unban takes a range in either family", "unbanned 192.0.2.0/24" in ranges and "unbanned 2001:db8:1::/48" in ranges
              and "unbanned 2001:1ef0::/32" in ranges, ranges)
        check("unban names what it cannot find", "unban: no ban matches '203.0.113.1'" in ranges, ranges)
        check("the list is empty again", ranges.rstrip().endswith("0 ban(s)"), ranges[-200:])

        # --- a ban list the server cannot use is left alone ---------------------
        kept = step(server, "a ban before the file is spoiled", ["ban 203.0.113.5 perm kept in memory"])
        check("the ban is made", "banned 203.0.113.5/32 (permanent): kept in memory" in kept, kept)
        good = banlist.read_bytes()
        spoiled = 'ban address 203.0.113.99/32 0 0 "x" ""\r\n'.encode("utf-16")
        banlist.write_bytes(spoiled)
        unusable = step(server, "reload a file saved as UTF-16", ["banlist reload", "ban 203.0.113.6"])
        check("a file that is neither form is refused and said to be", "banlist.txt was not taken: it is neither a text ban list nor Quake 4's" in unusable, unusable)
        check("the bans in memory still hold", re.search(r"^\s*1\s+203\.0\.113\.5/32\s+permanent", unusable, re.MULTILINE) is not None
              and "\n1 ban(s)" in unusable, unusable)
        check("a ban made meanwhile is in force and said not to be saved", "banned 203.0.113.6/32 (permanent)" in unusable
              and "ban list: not saved" in unusable, unusable)
        check("the file is not written over", banlist.read_bytes() == spoiled and not (games["server"] / "banlist.txt.new").exists())
        banlist.write_bytes(good + b'ban address 203.0.113.77/32 0 0 "added by hand" ""\r\n')
        mended = step(server, "reload the mended file", ["banlist reload", "unban 203.0.113.5", "unban 203.0.113.77", "banlist"])
        check("a mended file is read again, and it is the record", re.search(r"203\.0\.113\.77/32\s+permanent\s+added by hand", mended) is not None
              and "203.0.113.6" not in mended and "\n2 ban(s)" in mended and mended.rstrip().endswith("0 ban(s)"), mended)
        check("saving works again", "OldCheater" in banlist.read_text(encoding="utf-8")
              and re.search(r"^ban ", banlist.read_text(encoding="utf-8"), re.MULTILINE) is None)

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
        join("banned", "ModPlayer", args.client_port, "the banned address is refused", expect=TEXT_BANNED)
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
        join("returning", "ModPlayer", args.client_port, "join after the unban: client map load")
        become_player("join after the unban", returning)
        check("the unbanned address plays again", True)

        # --- one address, one client -------------------------------------------
        if not args.skip_address_limit:
            step(server, "limit clients for an address", ["set net_serverMaxClientsPerAddress 1"])
            join("second", "SecondPlayer", args.second_port, "the second client is refused", expect=TEXT_TOO_MANY)
            check("a second client from one address is refused and told why", True)
            status = step(server, "status with the limit", ["status"])
            check("only the first client holds the address", "SecondPlayer" not in status and "ModPlayer" in status, status)
            step(server, "lift the limit", ["set net_serverMaxClientsPerAddress 0"])
            retire("second")

        # --- a kick typed at the console ---------------------------------------
        kicked = roles["kicked"]
        join("kicked", "KickMe", args.second_port, "the player to kick joins")
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

        # --- an admin: a player with the server's second password ---------------
        if not args.skip_admin:
            admin, target = returning, roles["target"]

            def answered(label: str, actions: list[str], expected: str, role: Role = admin, seconds: int = 30) -> str:
                """Console actions on the admin's client, and what its console showed up to the server's answer."""
                mark = len(read_log(role.log))
                # the server takes one request in half a second from a connection and drops a faster one unanswered
                step(role, label, ["waitMsec 700", *actions])
                return wait(label + ": the answer", lambda t: expected in t[role.name][mark:], seconds)[role.name][mark:]

            def listed(player: str, where: str, label: str) -> str:
                status = step(server, label, ["status"])
                row = re.search(r"^\s*(\d+)\s+\d+\s+\S+\s+\S+\s+" + re.escape(where) + r"\s+-\s+" + re.escape(player) + r"\s*$",
                                status, re.MULTILINE)
                check(f"{player} is listed from {where}", row is not None, status)
                return status

            # too short a password is not taken, and the one the server started with still stands
            short = step(server, "an admin password that is too short", ["set net_serverAdminPassword short", "waitMsec 500"])
            wait("the short password is refused", lambda t: "ignored an admin password shorter than 8 bytes; the earlier password still stands" in t["server"], 20)
            check("an admin password shorter than eight bytes is not taken", True, short)

            join("target", "Troublemaker", args.second_port, "the admin's first target joins", ip=OTHER_ADDRESS)
            become_player("the admin's first target", target)
            status = listed("Troublemaker", OTHER_ADDRESS, "status before the admin stage")
            bot = re.search(r"^\s*\d+\s+\d+\s+\S+\s+no\s+bot\s+-\s+(\S+)\s*$", status, re.MULTILINE)
            bot_name = bot[1] if bot else "nobody"

            # the password never goes on the command line: the cvar is private, and the command reads it
            wrong = answered("a wrong admin password", ['set net_clientAdminPassword "not-the-password"', "admin"], TEXT_ADMIN_REFUSED)
            check("a wrong password is refused", TEXT_ADMIN_NOW not in wrong, wrong)
            answered("a request after the refused login", ["mute Troublemaker"], TEXT_NOT_ADMIN)
            check("a refused login gives no right", True)
            step(admin, "wait before the next login", ["waitMsec 2500"])
            right = answered("the admin password", [f'set net_clientAdminPassword "{ADMIN_PASSWORD}"', "admin"], TEXT_ADMIN_NOW)
            check("the right password makes the player an admin", TEXT_ADMIN_REFUSED not in right, right)
            check("the server's console names the new admin", "(ModPlayer^0) logged in as an admin" in read_log(server.log))
            def holding(game: Path, secret: str) -> list[str]:
                """The game's own files that hold the text; the harness's rot_*.cfg scripts are how it was typed."""
                return [str(path.relative_to(game)) for path in game.rglob("*")
                        if path.is_file() and not path.name.startswith("rot_") and secret.encode() in path.read_bytes()]

            leaked = holding(games[admin.name], ADMIN_PASSWORD) + holding(games[admin.name], "not-the-password")
            check("neither password reaches the client's log, console history or config", not leaked, " ".join(leaked))
            leaked = holding(games["server"], ADMIN_PASSWORD) + holding(games["server"], REF_PASSWORD)
            check("the passwords are nowhere in the server's files once they are read", not leaked, " ".join(leaked))

            # A change of map re-initialises every client. The grant is the connection's and goes on;
            # the admin logs in once. The same map again would only be a restart, which loads nothing
            # on the clients, so the server moves to another map and the run stays there.
            opened = {name: read_log(roles[name].log).count(MAP_OPENED) for name in ("returning", "target")}
            before = server.loads()
            server.inject([server.arm(), "spawnServer " + SECOND_MAP])
            wait("map change under the admin: server", lambda t: server.loads() > before, args.startup_timeout)
            wait("map change under the admin: clients",
                 lambda t: all(t[name].count(MAP_OPENED) > count for name, count in opened.items()), args.startup_timeout)
            become_player("the admin after the map change", admin)
            become_player("the target after the map change", target)
            listed("Troublemaker", OTHER_ADDRESS, "status after the map change")

            # mute and unmute
            mark = len(read_log(target.log))
            said = answered("an admin mutes", ["mute Troublemaker 5m"], TEXT_ADMIN_DONE + "Troublemaker")
            check("the admin's login goes on across a change of map", TEXT_NOT_ADMIN not in said, said)
            wait("the muted player is told", lambda t: TEXT_MUTED in t["target"][mark:], 20)
            check("an admin's mute silences the player and tells them", True, said)
            said = answered("an admin lifts the mute", ["unmute Troublemaker"], TEXT_ADMIN_DONE + "Troublemaker")
            wait("the player is told the mute is lifted", lambda t: TEXT_UNMUTED in t["target"][mark:], 20)
            said = answered("an admin lifts a mute twice", ["unmute Troublemaker"], TEXT_ADMIN_NO)
            check("lifting a mute that is not there changes nothing and says so", True, said)

            # who an admin cannot reach, and what an admin cannot do
            answered("an admin mutes a bot", ["mute " + bot_name], TEXT_ADMIN_NO)
            answered("an admin kicks themselves", ["gameKick ModPlayer"], TEXT_ADMIN_NO)
            check("a bot and the admin's own slot are refused", True)
            local = step(admin, "what an admin's console still cannot do",
                         ["ban 192.0.2.7", "ban 192.0.2.0/24", "unban 1", "banlist", "status", "serverShutdown now"])
            check("an admin bans connected players only: an address or a range is not sent",
                  local.count("no connected player matches") == 2, local)
            check("an admin cannot lift a ban, list bans, see addresses or stop the server",
                  local.count("only the server runs this") == 4, local)
            status = step(server, "status after the refusals", ["status", "banlist"])
            check("the refusals changed nothing", "\n4 client(s)" in status and "\n0 ban(s)" in status
                  and re.search(r"\byes\s", status.partition("client(s)")[0]) is None, status)

            # kick, with a reason
            mark = len(read_log(target.log))
            answered("an admin kicks", ["gameKick Troublemaker spamming the chat"], TEXT_ADMIN_DONE + "Troublemaker")
            wait("the kicked player is told", lambda t: TEXT_KICKED in t["target"][mark:], 30)
            check("an admin's kick removes the player", True)
            retire("target")

            # a ban: of the player's address, no longer than the host allows
            nuisance = roles["nuisance"]
            join("nuisance", "Nuisance", args.second_port, "the admin's second target joins", ip=OTHER_ADDRESS)
            become_player("the admin's second target", nuisance)
            listed("Nuisance", OTHER_ADDRESS, "status before the admin's ban")
            step(server, "forbid admin bans", ["set g_adminMaxBanMinutes 0"])
            answered("an admin bans where the host forbids it", ["ban Nuisance 10m"], "Admins cannot ban on this server.")
            step(server, "allow admins five minutes", ["set g_adminMaxBanMinutes 5"])
            mark = len(read_log(nuisance.log))
            answered("an admin bans for ever", ["ban Nuisance perm being a nuisance"], TEXT_ADMIN_DONE + "Nuisance")
            wait("the banned player is dropped", lambda t: TEXT_BANNED in t["nuisance"][mark:], 30)
            text = banlist.read_text(encoding="utf-8")
            row = re.search(r'^ban address 127\.0\.0\.2/32 (\d+) (\d+) "Nuisance" "being a nuisance"$', text, re.MULTILINE)
            check("an admin's ban is of the one address, with the reason", row is not None, text)
            check("an admin's ban ends when the host's limit says, whatever was asked",
                  row is not None and int(row[2]) - int(row[1]) == 300, text)
            status = step(server, "status after the admin's ban", ["status"])
            check("the admin, at another address, stays", "ModPlayer" in status and "Nuisance" not in status, status)
            retire("nuisance")
            lifted = step(server, "the host lifts the admin's ban", ["unban 1", "set g_adminMaxBanMinutes 1440"])
            check("only the host lifts it", "unbanned 127.0.0.2/32" in lifted, lifted)

            # the host sees who is an admin and ends the login
            who = step(server, "who is an admin", ["admin"])
            check("the host's console lists the admins", "admin logins are on" in who and re.search(r"^\s*\d+\s+ModPlayer", who, re.MULTILINE) is not None
                  and "\n1 admin(s)" in who, who)
            mark = len(read_log(admin.log))
            revoked = step(server, "the host ends the login", ["admin revoke ModPlayer", "admin revoke ModPlayer", "admin"])
            wait("the admin is told", lambda t: TEXT_ADMIN_GONE in t[admin.name][mark:], 20)
            check("the host ends an admin's login once", "ModPlayer^0 is no longer an admin" in revoked
                  and "admin: that player is not an admin" in revoked and "\n0 admin(s)" in revoked, revoked)
            answered("a request after the host ended the login", ["mute " + bot_name], TEXT_NOT_ADMIN)
            answered("the admin logs in again", ["admin"], TEXT_ADMIN_NOW)

            # giving the right up
            answered("admin logout", ["admin logout"], TEXT_ADMIN_GONE)
            answered("a request after the logout", ["mute " + bot_name], TEXT_NOT_ADMIN)
            check("after a logout the player is no admin", True)

            # the password typed after the command is refused, and not kept
            slip = step(admin, "the password typed after the command", ["admin typed-by-mistake-1"])
            check("a password after the command is not taken as one", "the password is not typed after the command" in slip, slip)
            check("the mistyped line reaches none of the client's files", not holding(games[admin.name], "typed-by-mistake-1"))

            # a new password is taken at once and logs everyone out; the old one no longer works
            answered("log in before the password changes", ["admin"], TEXT_ADMIN_NOW)
            mark = len(read_log(admin.log))
            step(server, "the host changes the password", [f'set net_serverAdminPassword "{NEW_ADMIN_PASSWORD}"', "waitMsec 500"])
            wait("the admin is logged out by the new password", lambda t: TEXT_ADMIN_GONE in t[admin.name][mark:], 20)
            step(admin, "wait before the next login", ["waitMsec 2500"])
            old = answered("the old password", ["admin"], TEXT_ADMIN_REFUSED)
            check("a new password is taken at once and the old one no longer works", TEXT_ADMIN_NOW not in old, old)
            step(admin, "wait before the next login", ["waitMsec 2500"])
            answered("the new password", [f'set net_clientAdminPassword "{NEW_ADMIN_PASSWORD}"', "admin"], TEXT_ADMIN_NOW)
            check("the new password logs the admin in", True)

            # the host turns admin logins off
            mark = len(read_log(admin.log))
            off = step(server, "the host turns admin logins off", ["admin off", "admin"])
            wait("the admin is logged out when logins are turned off", lambda t: TEXT_ADMIN_GONE in t[admin.name][mark:], 20)
            check("admin logins can be turned off while the server runs", "admin logins are off" in off and "\n0 admin(s)" in off, off)
            step(admin, "wait before the next login", ["waitMsec 2500"])
            answered("a login when logins are off", ["admin"], TEXT_ADMIN_REFUSED)
            check("nobody logs in while admin logins are off", True)
            leaked = [name for secret in (ADMIN_PASSWORD, NEW_ADMIN_PASSWORD) for name in holding(games["server"], secret) + holding(games[admin.name], secret)]
            check("neither admin password is in any file the server or the admin's game wrote", not leaked, " ".join(leaked))

            acts = [event for event in events() if "admin_slot" in event]
            check("everything an admin did is logged beside who did it",
                  [(event["event"], event.get("name")) for event in acts] == [
                      ("mute", "Troublemaker"), ("unmute", "Troublemaker"), ("kick", "Troublemaker"),
                      ("ban", "Nuisance"), ("kick", "Nuisance")]
                  and all(event.get("by") == "admin" and event.get("admin_name") == "ModPlayer"
                          and event.get("admin_address") == "127.0.0.1" for event in acts), json.dumps(acts))
            check("the admin's mute, kick and ban carry their length and reasons",
                  len(acts) == 5 and acts[0].get("seconds") == 300 and acts[2].get("reason") == "spamming the chat"
                  and acts[3].get("reason") == "being a nuisance" and acts[3].get("range") == "127.0.0.2/32", json.dumps(acts))
            logins = [(event["event"], event.get("name"), event.get("address")) for event in events()
                      if event["event"].startswith("admin_")]
            mine = ("ModPlayer", "127.0.0.1")
            check("every failed login, login and logout is logged with the player's address",
                  logins == [("admin_login_failed", *mine), ("admin_login", *mine), ("admin_logout", *mine), ("admin_login", *mine),
                             ("admin_logout", *mine), ("admin_login", *mine), ("admin_logout", *mine), ("admin_login_failed", *mine),
                             ("admin_login", *mine), ("admin_logout", *mine), ("admin_logins_off", None, None),
                             ("admin_login_failed", *mine)], json.dumps(logins))
            reasons = [event.get("reason") for event in events() if event["event"] in ("admin_logout", "admin_login_failed")]
            check("the log says why a login ended or failed",
                  reasons == [None, "revoked", None, "the password changed", None, "admin logins were turned off",
                              "no admin password is set"], json.dumps(reasons))

        # --- rcon: a console line that came from elsewhere says so in the log ----
        status = step(server, "status before the rcon stage", ["status"])
        rcon_bot = re.search(r"^\s*\d+\s+\d+\s+\S+\s+no\s+bot\s+-\s+(\S+)\s*$", status, re.MULTILINE)
        check("a bot is listed for the rcon stage", rcon_bot is not None, status)
        rcon_target = rcon_bot[1] if rcon_bot else "nobody"
        local = len(events())
        step(server, "the same command typed at the console", ["mute " + rcon_target, "unmute " + rcon_target])
        step(returning, "the command sent by rcon", [
            f'set net_clientRemoteConsoleAddress "{address}"', f'set net_clientRemoteConsolePassword "{RCON_PASSWORD}"',
            "rcon mute " + rcon_target, "waitMsec 2500", "rcon unmute " + rcon_target, "waitMsec 2500",
            "rcon ban 198.51.100.7/24 3rd offence", "waitMsec 2500", "rcon unban 198.51.100.0/24", "waitMsec 2500"])
        wait("the rcon commands reach the admin log", lambda t: len(events()) >= local + 6, 30)
        sent = events()[local + 4:local + 6]
        check("a line sent by rcon arrives as it was typed", [event["event"] for event in sent] == ["ban", "unban"]
              and sent[0].get("range") == "198.51.100.0/24" and sent[0].get("reason") == "3rd offence" and "expires" not in sent[0]
              and all(RCON_SOURCE.fullmatch(event.get("rcon", "")) for event in sent), json.dumps(sent))
        typed, remote = events()[local:local + 2], events()[local + 2:local + 4]
        check("a command typed at the console has no rcon source", [event["event"] for event in typed] == ["mute", "unmute"]
              and all(event.get("by") == "console" and "rcon" not in event for event in typed), json.dumps(typed))
        check("a command sent by rcon is logged with the address it came from",
              [event["event"] for event in remote] == ["mute", "unmute"] and all(
                  event.get("by") == "console" and RCON_SOURCE.fullmatch(event.get("rcon", "")) for event in remote),
              json.dumps(remote))
        leaked = [str(path.relative_to(games["returning"])) for path in games["returning"].rglob("*") if path.is_file()
                  and not path.name.startswith("rot_") and RCON_PASSWORD.encode() in path.read_bytes()]
        check("the rcon password is in no file the client wrote", not leaked, " ".join(leaked))

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
        admin_kinds = [] if args.skip_admin else [
            "admin_login_failed", "admin_login", "mute", "unmute", "ban", "unban", "admin_logout", "admin_login", "admin_logout",
            "admin_login", "admin_logout", "admin_login_failed", "admin_login", "admin_logout", "admin_logins_off",
            "admin_login_failed"]
        check("the log holds each kind of event in order",
              [kind for kind in kinds if kind != "kick"] == [
                  "mute", "unmute", "flood",
                  "ban", "ban", "ban", "ban", "unban", "unban", "unban",		# ranges
                  "ban", "ban", "unban", "unban",								# the unusable file
                  "ban", "refused", "unban", *admin_kinds,
                  "mute", "unmute", "mute", "unmute", "ban", "unban", "mute",	# console, rcon, the vote
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

        # --- the match log -----------------------------------------------------
        lines = [json.loads(line) for line in match_log.read_text(encoding="utf-8").splitlines()]
        report["match_log_lines"] = len(lines)
        begins = [index for index, line in enumerate(lines) if line.get("type") == "stream"]
        check("the match log is one JSON object a line", len(lines) > 1 and all(line.get("v") == 1 for line in lines), str(lines[:1]))
        check("an earlier stream is kept and this run's is written after it",
              len(begins) >= 2 and begins[0] == 0 and lines[0].get("map") == "mp/earlier" and begins[1] == 1, str(begins))
        ours = begins[1:]
        check("each stream says when it began, where and by whom", len(ours) > 0 and all(
            re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", lines[index].get("utc", "")) and lines[index].get("map") in (FIRST_MAP, SECOND_MAP)
            and lines[index].get("source") == "server" and lines[index].get("seq") == 0 for index in ours), str([lines[i] for i in ours])[:600])
        numbered = all(lines[i]["seq"] == lines[i - 1]["seq"] + 1 for i in range(1, len(lines)) if i not in begins)
        check("sequence numbers rise by one within a stream", numbered)
        last = lines[begins[-1]:] if begins else []
        kinds = [line.get("type") for line in last]
        check("the stream holds the match: live, its kills, the review and the standings",
              any(line.get("type") == "phase" and line.get("to") == "live" for line in last) and kinds.count("kill") >= 2
              and any(line.get("type") == "phase" and line.get("to") == "review" for line in last) and "standings" in kinds,
              " ".join(kind or "?" for kind in kinds)[-600:])
        # the live file shares the match log's writer: it still starts again and carries no wall-clock time
        live_file = games["server"] / "match-events/live.ndjson"
        live_lines = [json.loads(line) for line in live_file.read_text(encoding="utf-8").splitlines()] if live_file.is_file() else []
        check("the live event file still begins with its own stream line, without a wall-clock time",
              len(live_lines) > 1 and live_lines[0].get("type") == "stream" and "utc" not in live_lines[0]
              and [line.get("type") for line in live_lines].count("stream") == 1, str(live_lines[:1])[:300])
        check("the live event file and the match log record the same match",
              [line for line in live_lines if line.get("type") == "kill"] == [line for line in last if line.get("type") == "kill"])
        text = match_log.read_text(encoding="utf-8")
        check("the match log names players and holds no address", "ModPlayer" in text and "127.0.0.1" not in text)
        check("no half-written ban list is left behind", not (games["server"] / "banlist.txt.new").exists())

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
    report.setdefault("client_restarts", [])
    report["elapsed_seconds"] = round(time.monotonic() - started, 2)
    if {key: digest(path) for key, path in paths.items()} != report["runtime_sha256"]:
        report["failures"].append("runtime changed during qualification")
    report["status"] = "fail" if report["failures"] else "pass"
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "exit_codes", "failures", "client_restarts")}, indent=2))
    return int(bool(report["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
