#!/usr/bin/env python3
"""Measure multiplayer bot quality and cost on a stock map with no input.

A hidden, windowed listen server hosts a fixed roster of bots for a fixed
time and the engine log is reduced to the numbers that describe a bot
implementation: how often bots died to the map rather than to a player,
how often navigation failed (no route, stuck, abandoned goals, timed-out
traversals), how much fighting happened, and what the server frame cost.
`bot_profile` adds the bots' own CPU time and navigation-query counts when
the build has it; `com_speeds` supplies main-loop wall time. Its legacy gfr
counter measures the session loop, not the asynchronous multiplayer server,
so it cannot establish MP game-frame CPU time.

The local client is an untargetable, invulnerable observer: public matches
need one active human, and this one never fights or blocks anything.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import statistics
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import verify_runtime_is_current, write_script

try:
    import psutil
except ImportError:  # CPU sampling is optional; the log metrics are not
    psutil = None

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("Cortez", "Rhodes", "Sledge", "Voss", "Strauss", "Kane", "Morris", "Tetzlaff",
         "Marsh", "Hollenbeck", "Sorg", "Gunner")

# player.def's obituary texts and openQ4's liquid deaths (strings/*_openq4.lang).
OBITUARY_KILLED_BY = (r"^(\S+) was (?:pummeled|machinegunned|hyperblasted|nailed|railed|toasted|killed|"
                      r"telefragged|compressed into a singularity) by (\S+)$")
OBITUARY_POSSESSIVE = r"^(\S+) (?:ate|rode|accepted) (\S+?)'s (?:boomstick|pineapple|rocket|discharge)$"
OBITUARY_WORLD = r"^(\S+) (?:died|drowned|dissolved in slime|burned in lava)$"

NAV_EVENTS = {
    "no_route": re.compile(r"^bot [^:]+: no route from", re.M),
    "stuck": re.compile(r"^bot [^:]+: stuck at", re.M),
    "traversal_timeout": re.compile(r"^bot [^:]+: (?:[a-z ]+ )?traversal timed out", re.M),
    "goal_give_up": re.compile(r"^bot [^:]+: giving up on goal", re.M),
    "pickup_refused": re.compile(r"^bot [^:]+: '[^']*' did not pick up", re.M),
    "traversal_done": re.compile(r"^bot [^:]+: completed (?:jump|drop|jump pad|teleport)", re.M),
    "ordered": re.compile(r"^bot [^:]+: ordered to", re.M),
    # A route replaced because the bot was knocked or carried onto another
    # floor.  Each one is a stuck report that no longer has to happen.
    "off_route": re.compile(r"^bot [^:]+: off route at", re.M),
    # Rate-limited to one a second between them, so floors, not counts.
    "snap_report": re.compile(r"^rvNavMesh: no node near route", re.M),
    "leg_report": re.compile(r"^rvNavMesh: final leg .* refused", re.M),
}


def strip_colours(text: str) -> str:
    return re.sub(r"\^[0-9a-zA-Z]", "", text)


class SimulationProgress:
    """Read only complete server-clock lines inside one audit window."""

    def __init__(self):
        self.pending = ""
        self.started = False
        self.valid = True
        self.first = None
        self.last = None
        self.offset = 0

    def feed(self, text: str):
        lines = (self.pending + text).split("\n")
        self.pending = lines.pop()
        for line in lines:
            if "MP_" not in line:
                continue
            line = strip_colours(line).strip()
            if line == "MP_BOT_AUDIT_BEGIN":
                if self.started:
                    self.valid = False  # never borrow time from a restarted audit
                self.started = True
            elif self.started and (match := re.match(r"MP_STATE time=(\d+) ", line)):
                current = int(match[1])
                if self.last is not None and current < self.last:
                    self.valid = False
                if self.first is None:
                    self.first = current
                self.last = current

    def poll(self, path: Path):
        try:
            with path.open("rb") as source:
                source.seek(0, os.SEEK_END)
                if source.tell() < self.offset:
                    self.valid = False
                    return
                source.seek(self.offset)
                text = source.read()
                self.offset = source.tell()
        except OSError:
            return
        self.feed(text.decode("utf-8", errors="replace"))

    @property
    def seconds(self):
        return ((self.last - self.first) / 1000.0
                if self.valid and self.first is not None and self.last is not None else None)


def world_deaths(text: str, bot_names: set[str]) -> list[str]:
    direct = [name for name in re.findall(OBITUARY_WORLD, text, re.M) if name in bot_names]
    killed = re.findall(OBITUARY_KILLED_BY, text, re.M)
    return direct + [victim for victim, killer in killed if killer == "world" and victim in bot_names]


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def log_has_fatal(log_path: Path) -> bool:
    try:
        with log_path.open("rb") as source:
            source.seek(0, os.SEEK_END)
            source.seek(max(0, source.tell() - 16384))
            tail = source.read().decode("utf-8", errors="replace")
    except OSError:
        return False
    return re.search(r"^FATAL:", strip_colours(tail), re.M) is not None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--executable-name", default="openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".tmp/mp-bot-audit")
    parser.add_argument("--gametype", default="DM")
    parser.add_argument("--map", dest="map_name", default="mp/q4dm1")
    parser.add_argument("--bots", type=int, default=8)
    parser.add_argument("--skill", type=int, choices=range(1, 6), default=4)
    parser.add_argument("--debug", type=int, choices=range(1, 5), default=1,
                        help="2 includes bot status/damage, 3 adds edge/traversal traces, 4 adds jump predictions")
    parser.add_argument("--max-world-deaths", type=int, default=None,
                        help="fail the audit if deaths to the map exceed this limit")
    parser.add_argument("--classify-impulses", action="store_true",
                        help="apply the MP-02 policy (mp_bot_impulse_replay.py): replay each death that follows "
                             "a logged enemy impulse and apply --max-world-deaths to navigation deaths only; "
                             "excluded deaths are still reported with their evidence (needs --debug 2 or more)")
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--min-sim-seconds", type=int, default=None,
                        help="require this much observed server time, independent of wall-clock waits")
    parser.add_argument("--extend-to-min-sim", action="store_true",
                        help="after --seconds, keep observing until the logged minimum server time or timeout")
    parser.add_argument("--port", type=int, default=28972)
    parser.add_argument("--timeout", type=int, default=0, help="defaults to seconds + 150")
    parser.add_argument("--profile-period", type=int, default=10)
    parser.add_argument("--max-think-mean-ms", type=float,
                        help="maximum weighted mean bot think time per server frame")
    parser.add_argument("--max-think-p95-ms", type=float,
                        help="maximum conservative p95 in any complete profiling window")
    parser.add_argument("--max-nav-build-ms", type=int,
                        help="maximum one-time navigation generation cost")
    parser.add_argument("--require-uncontended", action="store_true",
                        help="fail timing qualification if another openQ4 runtime or compiler is observed")
    parser.add_argument("--speeds", action="store_true", help="log com_speeds frame timings")
    parser.add_argument("--set", action="append", default=[], metavar="CVAR=VALUE",
                        help="extra server cvar, e.g. si_warmup=1 for Tourney's bracket; repeatable")
    parser.add_argument("--team-order", default="",
                        help="team chat the observer says once the window opens, e.g. 'everyone follow me'; "
                             "the report then tracks how far its bot team-mates keep from it")
    args = parser.parse_args()
    if not 2 <= args.bots <= len(NAMES):
        parser.error(f"use 2 to {len(NAMES)} bots")
    if args.min_sim_seconds is not None and args.min_sim_seconds < 1:
        parser.error("--min-sim-seconds must be positive")
    if args.extend_to_min_sim and args.min_sim_seconds is None:
        parser.error("--extend-to-min-sim requires --min-sim-seconds")
    if args.classify_impulses and (args.debug < 2 or args.max_world_deaths is None):
        parser.error("--classify-impulses needs --debug 2 (BOT_IMPULSE traces) and --max-world-deaths")
    if args.require_uncontended and psutil is None:
        parser.error("--require-uncontended requires psutil")
    if args.debug > 2 and any(value is not None for value in
                              (args.max_think_mean_ms, args.max_think_p95_ms)):
        parser.error("use --debug 1 or 2 for timing qualification; frame-by-frame traces measure logging overhead")
    for value in (args.max_think_mean_ms, args.max_think_p95_ms, args.max_nav_build_ms):
        if value is not None and (not math.isfinite(value) or value <= 0):
            parser.error("timing budgets must be positive")
    timeout = args.timeout or args.seconds + 150
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    exe = runtime / args.executable_name
    if not exe.is_file() or not (args.basepath / "q4base").is_dir():
        parser.error("the staged client and retail q4base assets are required")
    verify_runtime_is_current(runtime, args.executable_name)
    identity_paths = [exe, *sorted((runtime / "baseoq4").glob("game-mp*")),
                      runtime / "baseoq4/pak0.pk4", runtime / "baseoq4/pak1.pk4"]
    runtime_identity = {str(path): file_sha256(path)
                        for path in identity_paths
                        if path.is_file() and path.suffix.lower() not in (".pdb", ".lib", ".exp")}

    save = output / "save"
    game = save / "baseoq4"
    game.mkdir(parents=True, exist_ok=True)
    log_path = game / "logs/openq4.log"
    if log_path.exists():
        log_path.unlink()

    script = ["notarget", "god", f"set bot_debug {args.debug}", f"set bot_profile {args.profile_period}",
              *[f"addbot {name} {args.skill} exact" for name in NAMES[:args.bots]],
              "waitMsec 5000", "botlist", "navmesh info", "echo MP_BOT_AUDIT_BEGIN"]
    if args.team_order:
        script.append(f'sayTeam "{args.team_order}"')
    if args.speeds:
        script.append("set com_speeds 1")
    for _ in range(args.seconds // 5):
        script += ["waitMsec 5000", "openq4_reportMPState"]
    finish = (["set com_speeds 0"] if args.speeds else []) + ["echo MP_BOT_AUDIT_END", "quit"]
    finish_path = game / "bot_audit_finish.cfg"
    if args.extend_to_min_sim:
        # The launcher publishes completion only after reading sufficient
        # server time. Missing logs or a stalled clock reach the normal hard
        # timeout and fail; neither can shorten or waive the duration gate.
        write_script(finish_path, ["waitMsec 5000", "openq4_reportMPState", "exec bot_audit_finish.cfg"])
        script.append("exec bot_audit_finish.cfg")
    else:
        script += finish
    (game / "bot_audit.cfg").write_text("\n".join(script) + "\n", encoding="utf-8")

    cvars = {
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
        "sys_consoleWindow": "0", "in_tty": "0", "in_mouse": "0", "in_joystick": "0",
        "r_fullscreen": "0", "r_borderless": "0", "r_fullscreenDesktop": "0",
        "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "s_noSound": "1",
        "r_mode": "-1", "r_customWidth": "640", "r_customHeight": "480", "r_renderApi": "gl",
        "r_swapInterval": "0", "com_maxfps": "60", "ui_retained": "0",
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(save), "fs_devpath": str(save),
        "fs_game": "baseoq4", "com_gameMode": "MP", "logFile": "2", "logFileName": "logs/openq4.log",
        "developer": "1", "ui_autoJoin": "1", "ui_spectate": "Play", "ui_name": "BotAuditObserver",
        "net_serverDedicated": "0", "net_LANServer": "1", "net_ip": "127.0.0.1", "net_enableIPv6": "0",
        "net_allowCheats": "1", "sv_cheats": "1",
        "net_port": str(args.port), "si_pure": "0", "si_gameType": args.gametype,
        "si_maxPlayers": str(args.bots + 2), "si_minPlayers": "2", "si_warmup": "0", "si_useReady": "0",
        "si_fragLimit": "999", "si_captureLimit": "999", "si_timeLimit": "0",
        "g_matchProfile": "casual", "bot_enable": "1", "bot_minPlayers": "0", "bot_pause": "0",
        "bot_characters": "1", "bot_skillVariance": "0", "bot_chat": "0",
        "com_skipLoadingContinue": "1", "g_autoExecAfterMapLoad": "bot_audit.cfg",
        "g_autoExecAfterMapLoadDelayMs": "500",
    }
    for extra in args.set:
        name, _, value = extra.partition("=")
        if not name or not _:
            parser.error(f"--set expects CVAR=VALUE, not {extra!r}")
        cvars[name] = value
    command = [str(exe)]
    for name, value in cvars.items():
        command += ["+set", name, value]
    command += ["+spawnServer", args.map_name]
    (output / "launch.json").write_text(json.dumps(command, indent=2), encoding="utf-8")

    started = time.time()
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    # (wall seconds, process CPU seconds) samples, so the audit window's CPU
    # cost can be separated from loading and navmesh generation.
    cpu_samples: list[tuple[float, float]] = []
    competing_workloads: set[tuple[int, str]] = set()
    competing_details: dict[tuple[int, float], dict] = {}
    progress = SimulationProgress()
    finish_published = False
    extension_error = None
    next_progress_poll = 0.0
    with (output / "console.log").open("w", encoding="utf-8") as console:
        process = subprocess.Popen(command, cwd=runtime, stdout=console, stderr=subprocess.STDOUT,
                                   creationflags=creationflags)
        watched = psutil.Process(process.pid) if psutil else None
        code = None
        deadline = started + timeout
        while code is None:
            if args.require_uncontended:
                for other in psutil.process_iter(["pid", "name"]):
                    name = (other.info["name"] or "").lower().removesuffix(".exe")
                    if other.info["pid"] != process.pid and (
                            name.startswith("openq4-client") or name.startswith("openq4-ded") or
                            name in ("cl", "clang", "clang++", "cc1", "cc1plus", "ninja", "link", "ld", "ld.lld")):
                        competing_workloads.add((other.info["pid"], other.info["name"]))
                        try:
                            # Preserve process identity and overlap times. Do
                            # not retain arbitrary arguments, which may contain
                            # credentials unrelated to this qualification.
                            created = other.create_time()
                            detail = competing_details.setdefault((other.pid, created), {
                                "pid": other.pid, "name": other.info["name"], "created": created,
                                "executable": other.exe(), "parent_pid": other.ppid(),
                                "first_seen_seconds": round(time.time() - started, 3),
                            })
                            detail["last_seen_seconds"] = round(time.time() - started, 3)
                        except psutil.Error:
                            pass  # The observed overlap still counts if it exits.
            try:
                code = process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                if args.extend_to_min_sim and not finish_published and time.monotonic() >= next_progress_poll:
                    next_progress_poll = time.monotonic() + 5.0
                    progress.poll(log_path)
                    if progress.seconds is not None and progress.seconds >= args.min_sim_seconds:
                        try:
                            write_script(finish_path, finish)
                        except OSError as error:
                            extension_error = str(error)
                            process.kill()
                            code = process.wait()
                            break
                        finish_published = True
                # The fatal-error UI can keep a hidden process alive forever.
                # Its own engine log is sufficient evidence; stop only this
                # audit's child instead of waiting out the startup timeout.
                if log_has_fatal(log_path):
                    process.kill()
                    code = process.wait()
                    break
                if time.time() > deadline:
                    process.kill()
                    process.wait()
                    code = -1
                    break
                if watched:
                    try:
                        times = watched.cpu_times()
                        cpu_samples.append((time.time(), times.user + times.system))
                    except psutil.Error:
                        pass

    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    clean = strip_colours(log)
    warmup, _, measured = clean.partition("\nMP_BOT_AUDIT_BEGIN")
    sample = measured.partition("\nMP_BOT_AUDIT_END")[0]

    failures = []
    if extension_error:
        failures.append("could not publish audit completion: " + extension_error)
    if code != 0 or "\nMP_BOT_AUDIT_END" not in clean:
        failures.append(f"server failed to complete (exit {code})")
    errors = re.findall(r"^.*(?:ERROR:|FATAL:|Assertion failed).*$", clean, re.M)
    failures.extend(errors)

    bot_names = set(NAMES[:args.bots])
    # Console obituaries come from player.def's per-weapon texts ("was railed
    # by", "rode X's rocket", ...), its generic "was killed by" - which names
    # "world" as the killer of a fall or a kill volume - and openQ4's liquid
    # texts.  Matching the generic text alone counted every death to the map
    # as a kill and missed every weapon kill.
    obituaries = re.findall(OBITUARY_KILLED_BY, sample, re.M) + re.findall(OBITUARY_POSSESSIVE, sample, re.M)
    died = world_deaths(sample, bot_names)
    warmup_died = world_deaths(warmup, bot_names)
    all_died = world_deaths(clean, bot_names)
    # Rates remain tied to the measured window. Safety qualification includes
    # startup too: a lethal route is no safer because a bot took it before BEGIN.
    classification = None
    if args.classify_impulses and all_died:
        # Only deaths after a logged enemy impulse whose isolated replay falls
        # with every fixed air input are excluded; everything else counts.
        from mp_bot_impulse_replay import classify_log
        try:
            classification = classify_log(clean, bot_names, args.gametype, args.map_name, runtime, args.executable_name,
                                          args.basepath.resolve(), output / "impulse-replays", args.port + 1)
        except (Exception, SystemExit) as error:
            # Keep the match report even if the replay service fails; an
            # interrupted proof must never exclude a world death.
            classification = {"navigation_world_deaths": len(all_died), "excluded_impulse_deaths": 0, "deaths": []}
            failures.append("impulse classification failed: " + str(error))
        counted = classification["navigation_world_deaths"]
        if counted > args.max_world_deaths:
            failures.append(f"{counted} complete-run navigation/control world deaths "
                            f"({len(all_died)} world deaths, {classification['excluded_impulse_deaths']} "
                            f"excluded as unrecoverable enemy impulses) exceed the limit of {args.max_world_deaths}")
    elif args.max_world_deaths is not None and len(all_died) > args.max_world_deaths:
        failures.append(f"{len(all_died)} complete-run world deaths "
                        f"({len(warmup_died)} during warmup, {len(died)} measured) "
                        f"exceed the limit of {args.max_world_deaths}")
    suicides = [m for m in re.findall(r"^(\S+) committed suicide$", sample, re.M) if m in bot_names]
    suicides += [victim for victim, killer in obituaries if killer == victim and victim in bot_names]
    kills = [(victim, killer) for victim, killer in obituaries if killer not in ("world", victim)]

    nav = {name: len(pattern.findall(sample)) for name, pattern in NAV_EVENTS.items()}

    frames = [float(v) for v in re.findall(r"^frame:\s*\d+\s+all:\s*\d+\s+gfr:\s*(\d+)", sample, re.M)]
    profile = re.findall(
        r"^botprofile: (\d+) bots (\d+) frames think avg ([\d.]+) ms max ([\d.]+) ms \| "
        r"paths (\d+) fail (\d+) quick (\d+) repair (\d+) expand ([\d.]+)/search \| "
        r"fields (\d+) settle ([\d.]+)/field \| nav ([\d.]+) ms/frame(?: \(fields ([\d.]+)\))?"
        r"(?: \| fail snap (\d+) search (\d+) leg (\d+))?", sample, re.M)
    profile_rows = [dict(zip(("bots", "frames", "think_avg_ms", "think_max_ms", "paths", "path_fail",
                              "quick_reject", "repair", "expand_per_search", "fields",
                              "settle_per_field", "nav_ms_per_frame", "field_ms_per_frame", "fail_snap",
                              "fail_search", "fail_leg"), (float(v) if v else 0.0 for v in row)))
                    for row in profile]
    percentile_rows = [dict(zip(("frames", "p50_upper_ms", "p95_upper_ms", "p99_upper_ms"), map(float, row)))
                       for row in re.findall(
                           r"^botprofile-percentiles: (\d+) frames p50_upper ([\d.]+) ms "
                           r"p95_upper ([\d.]+) ms p99_upper ([\d.]+) ms", sample, re.M)]
    total_profile_frames = sum(row["frames"] for row in profile_rows)
    mean_think_ms = (sum(row["frames"] * row["think_avg_ms"] for row in profile_rows) /
                     total_profile_frames if total_profile_frames else None)
    max_window_p95_ms = max((row["p95_upper_ms"] for row in percentile_rows), default=None)
    navmesh = re.search(r"^rvNavMesh: (\d+) nodes, (\d+) links[^\n]*in (\d+) ms", clean, re.M)
    if args.require_uncontended and competing_workloads:
        failures.append("timing run overlapped other runtimes or compilers: " + str(sorted(competing_workloads)))
    if args.max_think_mean_ms is not None and (mean_think_ms is None or mean_think_ms > args.max_think_mean_ms):
        failures.append(f"mean bot think {mean_think_ms} ms exceeds {args.max_think_mean_ms} ms budget")
    if args.max_think_p95_ms is not None and (max_window_p95_ms is None or max_window_p95_ms > args.max_think_p95_ms):
        failures.append(f"worst window bot think p95 {max_window_p95_ms} ms exceeds {args.max_think_p95_ms} ms budget")
    if args.max_nav_build_ms is not None and (not navmesh or int(navmesh.group(3)) > args.max_nav_build_ms):
        failures.append(f"navigation generation exceeds {args.max_nav_build_ms} ms budget or is missing")

    poses: dict[str, list[tuple[float, ...]]] = {}
    for slot, coordinates in re.findall(r"MP_POSE slot=(\d+) entity=\S+ origin=([^\r\n]+?) hidden=", sample):
        poses.setdefault(slot, []).append(tuple(map(float, coordinates.split())))
    live = set(re.findall(r"MP_PLAYER slot=(\d+) bot=1 ", sample))
    path_length = {}
    for slot in live:
        points = poses.get(slot, [])
        path_length[slot] = round(sum(sum((a - b) ** 2 for a, b in zip(p, q)) ** 0.5
                                      for p, q in zip(points, points[1:])), 1)

    # Mean distance from the observer to its bot team-mates at each report: an
    # order to follow should pull it down, one to defend should hold it.
    observer_distance = []
    for block in sample.split("MP_STATE time=")[1:]:
        local = re.search(r"MP_LOCAL client=(\d+) ", block)
        teams = dict(re.findall(r"MP_PLAYER slot=(\d+) bot=\d team=(\d+) ", block))
        bots_in_block = set(re.findall(r"MP_PLAYER slot=(\d+) bot=1 ", block))
        where = {slot: tuple(map(float, c.split())) for slot, c in
                 re.findall(r"MP_POSE slot=(\d+) entity=\S+ origin=([^\r\n]+?) hidden=", block)}
        if not local or local.group(1) not in where:
            continue
        me = local.group(1)
        mates = [slot for slot in bots_in_block if teams.get(slot) == teams.get(me) and slot in where]
        if mates:
            observer_distance.append(round(statistics.fmean(
                sum((a - b) ** 2 for a, b in zip(where[slot], where[me])) ** 0.5 for slot in mates), 1))

    # Objectives and combat: the last report's team scores (CTF captures,
    # DeadZone/One Flag points) and the bots' own scores.
    team_scores = re.findall(r"^MP_STATE time=\d+ .*? marine=(-?\d+) strogg=(-?\d+)", sample, re.M)
    last_report = sample.split("MP_STATE time=")[-1] if "MP_STATE time=" in sample else ""
    bot_scores = {slot: int(score) for slot, score in
                  re.findall(r"^MP_PLAYER slot=(\d+) bot=1 .*? score=(-?\d+) ", last_report, re.M)}

    # The server's own clock against the real-time report interval: below 1
    # the simulation could not keep up with real time.
    game_times = [int(v) for v in re.findall(r"^MP_STATE time=(\d+) ", sample, re.M)]
    sim_speed = None
    simulation_seconds = None
    if len(game_times) >= 2:
        simulation_seconds = (game_times[-1] - game_times[0]) / 1000.0
        sim_speed = round(simulation_seconds / (5.0 * (len(game_times) - 1)), 3)
    if args.min_sim_seconds is not None and (simulation_seconds is None or simulation_seconds < args.min_sim_seconds):
        failures.append(f"observed simulation duration {simulation_seconds} seconds "
                        f"is below the required {args.min_sim_seconds} seconds")

    # CPU over the audit window: the last `seconds` before the run ended.
    window_cpu = None
    if len(cpu_samples) >= 2:
        end_wall, end_cpu = cpu_samples[-1]
        start = next((s for s in cpu_samples if s[0] >= end_wall - args.seconds), cpu_samples[0])
        if end_wall > start[0]:
            window_cpu = round((end_cpu - start[1]) / (end_wall - start[0]), 3)

    loop_frames = [float(v) for v in re.findall(r"^frame:\s*\d+\s+all:\s*(\d+)", sample, re.M)][1:]
    try:
        runtime_unchanged = all(Path(path).is_file() and file_sha256(Path(path)) == digest
                                for path, digest in runtime_identity.items())
    except OSError:
        runtime_unchanged = False
    if not runtime_unchanged:
        failures.append("staged runtime changed during the match or its causal replays")
    rate_window_seconds = 5.0 * len(game_times) if args.extend_to_min_sim else args.seconds
    minutes = max(rate_window_seconds / 60.0, 1e-6)
    bot_minutes = minutes * args.bots
    report = {
        "map": args.map_name, "gametype": args.gametype, "bots": args.bots, "skill": args.skill,
        "seconds": args.seconds, "exit_code": code, "elapsed_seconds": round(time.time() - started, 1),
        "extend_to_min_sim": args.extend_to_min_sim, "rate_window_seconds": rate_window_seconds,
        "runtime_identity": runtime_identity,
        "runtime_unchanged": runtime_unchanged,
        "navmesh": {"nodes": int(navmesh.group(1)), "links": int(navmesh.group(2)),
                    "build_ms": int(navmesh.group(3))} if navmesh else None,
        "navmesh_line": next(iter(re.findall(r"^rvNavMesh: [^\n]*nodes[^\n]*$", clean, re.M)), ""),
        "kills": len(kills),
        "team_scores": ({"marine": int(team_scores[-1][0]), "strogg": int(team_scores[-1][1])}
                        if team_scores else None),
        "bot_scores": bot_scores,
        "world_deaths": len(died), "suicides": len(suicides),
        "warmup_world_deaths": len(warmup_died),
        "complete_run_world_deaths": len(all_died),
        "navigation_world_deaths": (classification["navigation_world_deaths"] if classification else
                                    (len(all_died) if args.classify_impulses else None)),
        "excluded_impulse_deaths": (classification["excluded_impulse_deaths"] if classification else
                                    (0 if args.classify_impulses else None)),
        "death_classification": classification["deaths"] if classification else [],
        "world_deaths_per_bot_minute": round(len(died) / bot_minutes, 3),
        "suicides_per_bot_minute": round(len(suicides) / bot_minutes, 3),
        "kills_per_bot_minute": round(len(kills) / bot_minutes, 3),
        "nav_events": nav,
        "nav_failures_per_bot_minute": round((nav["no_route"] + nav["stuck"] + nav["traversal_timeout"] +
                                              nav["goal_give_up"]) / bot_minutes, 3),
        "sim_speed": sim_speed,
        "simulation_seconds": simulation_seconds,
        "min_sim_seconds": args.min_sim_seconds,
        "cpu_cores_busy": window_cpu,
        "main_loop_frame_ms": {"frames": len(loop_frames),
                               "mean": round(statistics.fmean(loop_frames), 2) if loop_frames else None,
                               "p95": percentile(loop_frames, 0.95), "max": max(loop_frames, default=0)},
        "game_frame_ms": {"available": False, "frames": 0, "mean": None, "p95": None, "max": None,
                          "reason": "com_speeds gfr measures the session loop, not async MP server game frames"},
        "session_game_frame_counter_ms": {"frames": len(frames),
                                           "mean": round(statistics.fmean(frames), 3) if frames else None,
                                           "p95": percentile(frames, 0.95), "max": max(frames, default=0)},
        "bot_profile": profile_rows,
        "bot_profile_percentiles": percentile_rows,
        "timing_qualification": {"weighted_mean_think_ms": mean_think_ms,
                                 "worst_window_p95_upper_ms": max_window_p95_ms,
                                 "max_think_mean_ms": args.max_think_mean_ms,
                                 "max_think_p95_ms": args.max_think_p95_ms,
                                 "max_nav_build_ms": args.max_nav_build_ms,
                                 "uncontended_required": args.require_uncontended,
                                 "competing_workloads": sorted(competing_workloads),
                                 "competing_workload_details": list(competing_details.values())},
        "bot_path_length": path_length,
        "team_order": args.team_order,
        "teammate_distance_to_observer": observer_distance,
        "failures": failures,
        "log": str(log_path),
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    summary = {key: report[key] for key in ("map", "gametype", "bots", "navmesh_line", "kills", "team_scores",
                                             "world_deaths", "warmup_world_deaths", "complete_run_world_deaths",
                                             "navigation_world_deaths", "excluded_impulse_deaths",
                                             "suicides", "nav_events",
                                             "nav_failures_per_bot_minute", "sim_speed", "cpu_cores_busy",
                                             "main_loop_frame_ms", "failures")}
    summary["think_avg_ms"] = [row["think_avg_ms"] for row in profile_rows]
    print(json.dumps(summary, indent=2))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
