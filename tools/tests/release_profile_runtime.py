#!/usr/bin/env python3
"""Clean installs, upgrades, save round trips and lifecycle of one release runtime (REL-01, REL-03).

Opt-in runtime checks that need the retail Quake 4 PK4s. Every launch is a
hidden, windowed client with sound and input devices off; the harness drives it
only through startup commands, ``g_autoExecAfterMapLoad`` configs and engine
``screenshot`` captures. It never synthesises input or captures the desktop.
All writable data stays below ``--output``.

* ``clean-install``: a player's first three starts. The first reaches the title
  menu with no profile: it must detect a preset silently (no system-check
  screen) and write a config. The second plays a level and quits from
  gameplay without dropping an archived value. The third reaches the menu again
  and must leave every archived value as the profile holds it.
* ``upgrade``: the previous release (``--previous``) makes a profile with
  player choices that differ from the defaults (archived CVars across input,
  audio, video and gameplay, key bindings, the Awakening location) and a save
  on airdefense1. The candidate then runs on that profile: it must keep every
  player choice, restore the old save into gameplay and resave it as a portable
  v5 save. Every other archived value the candidate changed is listed as a
  migration for review; the run fails only on lost player choices.
* ``save-roundtrip``: the candidate writes a save after play, then a second
  process restores it, resaves it before game time resumes and again after
  play (savegame_cross_load.py legs).
* ``demo``: records a demo in gameplay, then plays it back with
  ``timeDemoQuit`` in a fresh process, which must render it and quit.
* ``lifecycle``: cold start on an empty profile, warm start on its caches,
  start after its generated cache is corrupted, then one session that resizes
  the window, restarts the renderer, pauses and resumes the world, runs slow
  motion, saves, changes map and quits. The same profile then relaunches on
  Vulkan (the API changes only at engine start), restarts that renderer in
  gameplay, and relaunches on OpenGL again. Every phase
  must reach its end marker, draw its captures and exit with code 0 without a
  crash report; the time from the quit to process exit is recorded.

Each subcommand writes ``report.json`` in ``--output`` and exits non-zero on
any problem. ``self-test`` checks the config parser, the profile comparison and
the command lines without a client.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import savegame_cross_load as saves  # noqa: E402
from renderer_vulkan_qualification import screenshot_is_drawn, screenshot_stats  # noqa: E402

CONFIG_NAME = "openQ4Config.cfg"
GAME_DIR = "baseoq4"
DONE = "OPENQ4_REL_PROFILE_DONE"
QUIT = "OPENQ4_REL_PROFILE_QUIT"
ERROR_PATTERN = re.compile(r"^(?:\^[0-9])?(?:FATAL|ERROR)[: ].*$", re.MULTILINE)
SIGNAL_PATTERN = re.compile(r"signal caught|segmentation fault|Sys_Error:", re.IGNORECASE)
MAP_STARTED = re.compile(r"-+ Game Map Init -+|Game Map Init SaveGame")
RESTORE_STARTED = re.compile(r"Game Map Init SaveGame")
RESTORE_FAILED = re.compile(r"could not restore saved state[^\r\n]*|WARNING:.*Savegame '[^']+' "
                            r"(?:has|was|wire|is|failed)[^\r\n]*")
PRESET_CHECK = re.compile(r"[Pp]erformance ?[Pp]reset[^\r\n]*check[^\r\n]*")
RENDER_API = re.compile(r"^Renderer API: (\S+)", re.MULTILINE)
SETA = re.compile(r'^seta\s+(\S+)\s+"((?:[^"\\]|\\.)*)"\s*$')
BIND = re.compile(r'^bind\s+"?([^"\s]+)"?\s+"((?:[^"\\]|\\.)*)"\s*$')

# Values the harness forces on every launch. They reach the config only because
# the command line set them, so they are never treated as player choices.
LAUNCH_CVARS: dict[str, str] = {
    "win_allowMultipleInstances": "1", "sys_allowMultipleInstances": "1",
    "logFile": "2", "developer": "1",
    "r_fullscreen": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
    "r_mode": "-1", "r_customWidth": "960", "r_customHeight": "540",
    "r_windowWidth": "960", "r_windowHeight": "540",
    "r_hiddenWindow": "1", "r_swapInterval": "0", "com_maxfps": "60",
    "s_noSound": "1", "in_mouse": "0", "in_joystick": "0", "in_tty": "0",
    "com_skipLogoVideos": "1", "com_skipLoadingContinue": "1", "com_loadingContinueAutoAdvance": "1",
    "g_autoSkipCinematics": "1", "g_autoExecAfterMapLoadDelayMs": "0",
    "net_ip": "127.0.0.1", "net_enableIPv6": "0", "net_LANServer": "1",
}
FORCED = set(LAUNCH_CVARS) | {"r_renderApi", "fs_basepath", "fs_savepath", "fs_homepath", "fs_game", "logFileName",
                              "g_autoExecAfterMapLoad", "si_gameType", "com_fixedTic", "g_stopTime"}

# A player's profile, made by the previous release: every value differs from the
# shipped default, and spans the settings an upgrade must keep.
PROFILE_CVARS: dict[str, str] = {
    "sensitivity": "7.25",
    "m_pitch": "-0.022",
    "m_smooth": "3",
    "g_fov": "105",
    "ui_name": "RelProfileTester",
    "ui_autoSwitch": "0",
    "s_volume": "0.85",
    "s_musicVolume": "0.35",
    "r_gamma": "1.3",
    "r_brightness": "1.2",
    "com_showFPS": "1",
    "g_showHud": "1",
    "g_showHudPopups": "0",
    "ui_showGun": "0",
}
# Settings only the multiplayer module declares (or the engine declares for its
# menu), with values that differ from their defaults: what a multiplayer session
# leaves in the config. A single-player or menu start must keep every one.
MODULE_ONLY_CVARS: dict[str, str] = {
    "g_fov": "105",
    "hud_damageNumbers": "1",
    "hud_showSpeed": "1",
    "hud_itemTimers": "0",
    "hud_hitSound": "0",
    "si_roundLimit": "5",
    "bot_minPlayers": "3",
}
PROFILE_BINDS: dict[str, str] = {
    "F7": "echo rel_profile_bind_f7",
    "KP_5": "_impulse13",
    "MOUSE4": "_impulse51",
}


def profile_cfg_lines(awakening: str | None) -> list[str]:
    lines = [f'seta {name} "{value}"' for name, value in PROFILE_CVARS.items()]
    if awakening:
        lines.append(f'seta fs_awakeningpath "{awakening}"')
    lines += [f'bind {key} "{command}"' for key, command in PROFILE_BINDS.items()]
    return lines


def parse_config(text: str) -> tuple[dict[str, str], dict[str, str]]:
    """Archived CVars and key bindings of an openQ4 config file."""
    cvars: dict[str, str] = {}
    binds: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        match = SETA.match(line)
        if match:
            cvars[match.group(1)] = match.group(2)
            continue
        match = BIND.match(line)
        if match:
            binds[match.group(1).upper()] = match.group(2)
    return cvars, binds


def same_value(a: str, b: str) -> bool:
    if a == b:
        return True
    try:
        return abs(float(a) - float(b)) <= 1e-6 * max(1.0, abs(float(a)))
    except ValueError:
        return False


def compare_profiles(before: str, after: str, expected_cvars: dict[str, str],
                     expected_binds: dict[str, str]) -> dict[str, Any]:
    """Player choices kept or lost between two configs, and every other archived change."""
    cvars_a, binds_a = parse_config(before)
    cvars_b, binds_b = parse_config(after)
    lost: list[dict[str, Any]] = []
    not_saved: list[str] = []
    for name, value in expected_cvars.items():
        if name not in cvars_a or not same_value(cvars_a[name], value):
            not_saved.append(name)
            continue
        if name not in cvars_b or not same_value(cvars_b[name], cvars_a[name]):
            lost.append({"cvar": name, "before": cvars_a[name], "after": cvars_b.get(name)})
    for key, command in expected_binds.items():
        if binds_a.get(key.upper()) != command:
            not_saved.append(f"bind {key}")
            continue
        if binds_b.get(key.upper()) != command:
            lost.append({"bind": key, "before": command, "after": binds_b.get(key.upper())})
    migrations = []
    for name in sorted(set(cvars_a) | set(cvars_b)):
        if name in expected_cvars or name in FORCED:
            continue
        a, b = cvars_a.get(name), cvars_b.get(name)
        if a is None or b is None or not same_value(a, b):
            migrations.append({"cvar": name, "before": a, "after": b})
    rebinds = [{"bind": key, "before": binds_a.get(key), "after": binds_b.get(key)}
               for key in sorted(set(binds_a) | set(binds_b))
               if key not in {k.upper() for k in expected_binds} and binds_a.get(key) != binds_b.get(key)]
    return {"lost": lost, "notSavedByPrevious": not_saved, "migrations": migrations, "rebinds": rebinds,
            "cvarsBefore": len(cvars_a), "cvarsAfter": len(cvars_b)}


def client_path(runtime: Path) -> Path:
    return saves.client_path(runtime)


class Launch:
    """One hidden client run with its own log, after-load config and captures."""

    def __init__(self, name: str, runtime: Path, basepath: Path, home: Path, *, renderer: str = "gl",
                 startup: list[str] | None = None, after_load: list[str] | None = None,
                 autoexec: list[str] | None = None, extra: dict[str, str] | None = None, timeout: int = 600,
                 done: re.Pattern[str] | None = None) -> None:
        self.name = name
        self.runtime = runtime
        self.basepath = basepath
        self.home = home
        self.renderer = renderer
        self.startup = startup or []
        self.after_load = after_load
        self.autoexec = autoexec
        self.extra = extra or {}
        self.timeout = timeout
        self.done = done or re.compile(re.escape(DONE))
        self.game = home / GAME_DIR
        self.log = self.game / "logs" / f"rel-{name}.log"
        self.result: dict[str, Any] = {}

    def command(self) -> list[str]:
        cvars = dict(LAUNCH_CVARS)
        cvars.update({"logFileName": f"logs/rel-{self.name}.log", "fs_basepath": str(self.basepath),
                      "fs_savepath": str(self.home), "fs_homepath": str(self.home), "r_renderApi": self.renderer})
        if self.after_load is not None:
            cvars["g_autoExecAfterMapLoad"] = f"rel_profile/{self.name}.cfg"
        cvars.update(self.extra)
        command = [str(client_path(self.runtime))]
        for key, value in cvars.items():
            command += ["+set", key, value]
        return command + self.startup

    def prepare(self) -> None:
        (self.game / "logs").mkdir(parents=True, exist_ok=True)
        (self.game / "screenshots").mkdir(parents=True, exist_ok=True)
        self.log.unlink(missing_ok=True)
        if self.after_load is not None:
            cfg = self.game / "rel_profile" / f"{self.name}.cfg"
            cfg.parent.mkdir(parents=True, exist_ok=True)
            cfg.write_text("\n".join(['set g_autoExecAfterMapLoad ""', *self.after_load, f"echo {DONE}",
                                      f"echo {QUIT}", "quit", ""]), encoding="utf-8")
        autoexec = self.game / "autoexec.cfg"
        if self.autoexec is not None:
            autoexec.write_text("\n".join([*self.autoexec, f"echo {DONE}", f"echo {QUIT}", "quit", ""]),
                                encoding="utf-8")
        else:
            autoexec.unlink(missing_ok=True)

    def run(self) -> dict[str, Any]:
        self.prepare()
        command = self.command()
        crashes = self.runtime / "crashes"
        crashes_before = set(p.name for p in crashes.iterdir()) if crashes.is_dir() else set()
        stdout_path = self.game / "logs" / f"rel-{self.name}.stdout.txt"
        startup = None
        if sys.platform == "win32":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
        marks: dict[str, float] = {}
        started = time.monotonic()
        timed_out = False
        with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout:
            process = subprocess.Popen(command, cwd=self.runtime, stdin=subprocess.DEVNULL, stdout=stdout,
                                       stderr=subprocess.STDOUT, startupinfo=startup)
            while process.poll() is None:
                now = time.monotonic() - started
                if now > self.timeout:
                    timed_out = True
                    break
                text = self.log.read_text(encoding="utf-8", errors="replace") if self.log.is_file() else ""
                if "map" not in marks and MAP_STARTED.search(text):
                    marks["map"] = now
                if "done" not in marks and self.done.search(text):
                    marks["done"] = now
                if "quit" not in marks and QUIT in text:
                    marks["quit"] = now
                if ERROR_PATTERN.search(text) or SIGNAL_PATTERN.search(text) or RESTORE_FAILED.search(text):
                    time.sleep(3)
                    break
                time.sleep(0.25)
            if process.poll() is None:
                process.kill()
                exit_code = process.wait(timeout=30)
                killed = True
            else:
                exit_code = process.returncode
                killed = False
        ended = time.monotonic() - started
        text = self.log.read_text(encoding="utf-8", errors="replace") if self.log.is_file() else ""
        for key, pattern in (("map", MAP_STARTED), ("done", self.done), ("quit", re.compile(re.escape(QUIT)))):
            if key not in marks and pattern.search(text):
                marks[key] = ended
        new_crashes = sorted((set(p.name for p in crashes.iterdir()) if crashes.is_dir() else set()) - crashes_before)
        errors = ERROR_PATTERN.findall(text) + SIGNAL_PATTERN.findall(text) + RESTORE_FAILED.findall(text)
        apis = RENDER_API.findall(text)
        first_line = text.splitlines()[0].strip() if text else ""
        problems = []
        if timed_out:
            problems.append(f"timed out after {self.timeout} s")
        if killed and not timed_out:
            problems.append("stopped after an error in the log")
        if errors:
            problems.append("errors in the log")
        if new_crashes:
            problems.append("crash report written")
        if not killed and exit_code != 0:
            problems.append(f"exit code {exit_code}")
        if not self.done.search(text):
            problems.append("did not reach its end marker")
        self.result = {
            "name": self.name, "renderer": self.renderer, "version": first_line, "exitCode": exit_code,
            "timedOut": timed_out, "seconds": round(ended, 1),
            "marks": {k: round(v, 2) for k, v in marks.items()},
            "quitToExitSeconds": round(ended - marks["quit"], 2) if "quit" in marks and not killed else None,
            "errors": errors[:8], "crashReports": new_crashes, "renderApis": apis[-4:],
            "warnings": sum(1 for line in text.splitlines() if line.startswith("WARNING")),
            "presetCheckLines": PRESET_CHECK.findall(text)[:4], "problems": problems,
            "command": command, "log": str(self.log),
        }
        return self.result

    def screenshot(self, name: str) -> dict[str, Any]:
        path = self.game / "screenshots" / f"{name}.tga"
        if not path.is_file():
            return {"name": name, "present": False, "drawn": False}
        try:
            stats = screenshot_stats(path)
        except ValueError as error:
            return {"name": name, "present": True, "drawn": False, "error": str(error)}
        return {"name": name, "present": True, "drawn": screenshot_is_drawn(stats), **stats}


def shot(name: str) -> str:
    return f'screenshot "screenshots/{name}.tga"'


def finish(output: Path, report: dict[str, Any]) -> int:
    problems = report.setdefault("problems", [])
    for launch in report.get("launches", []):
        problems += [f"{launch['name']}: {problem}" for problem in launch["problems"]]
        for capture in launch.get("captures", []):
            if not capture["drawn"]:
                problems.append(f"{launch['name']}: capture {capture['name']} was not drawn")
    report["passed"] = not problems
    report["platform"] = f"{sys.platform}-{platform.machine().lower()}"
    output.mkdir(parents=True, exist_ok=True)
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "problems": problems[:20]}, indent=2))
    return 0 if not problems else 1


def run_launch(launch: Launch, captures: list[str], report: dict[str, Any], retries: int = 1) -> dict[str, Any]:
    """Run, retrying once only when a GL client never reached the map (the driver's startup hang)."""
    for attempt in range(retries + 1):
        result = launch.run()
        hung = result["timedOut"] and "map" not in result["marks"]
        if not hung or attempt == retries:
            break
        result["retriedAfterStartupHang"] = True
        report.setdefault("retries", []).append(dict(result))
    result["captures"] = [launch.screenshot(name) for name in captures]
    report.setdefault("launches", []).append(result)
    return result


def command_clean_install(args: argparse.Namespace) -> int:
    """A player's first three starts: the menu, a level, the menu again."""
    output = args.output.resolve()
    home = output / "home"
    if home.exists():
        shutil.rmtree(home)
    report: dict[str, Any] = {"mode": "clean-install", "runtime": str(args.runtime.resolve())}
    runtime, basepath = args.runtime.resolve(), args.basepath.resolve()
    config = home / GAME_DIR / CONFIG_NAME

    def snapshot(name: str) -> str:
        text = config.read_text(encoding="utf-8", errors="replace") if config.is_file() else ""
        (output / f"config-{name}.cfg").write_text(text, encoding="utf-8")
        return text

    # The first start reaches the title menu with no profile: it must detect a
    # preset silently, without the system check, and write a config.
    first = run_launch(Launch("first-menu", runtime, basepath, home, autoexec=["wait 360"],
                              timeout=args.timeout), [], report)
    if not config.is_file():
        report.setdefault("problems", []).append("the first start wrote no config")
        return finish(output, report)
    after_first = snapshot("first-menu")
    cvars, _ = parse_config(after_first)
    report["firstConfig"] = {"cvars": len(cvars), "presetCheck": cvars.get("com_performancePresetCheck"),
                             "performancePreset": cvars.get("com_performancePreset")}
    report["firstPresetCheckLines"] = first["presetCheckLines"]
    if not cvars.get("com_performancePresetCheck"):
        report.setdefault("problems", []).append("the first start did not record its preset system check")
    # Then a level, played and quit from gameplay.
    run_launch(Launch("gameplay", runtime, basepath, home, startup=["+map", args.map],
                      after_load=["wait 90", shot("gameplay")], timeout=args.timeout), ["gameplay"], report)
    after_play = snapshot("gameplay")
    changes = compare_profiles(after_first, after_play, {}, {})
    report["gameplayChanges"] = changes["migrations"] + changes["rebinds"]
    # A third start must leave every archived value as the profile now has it.
    run_launch(Launch("menu-again", runtime, basepath, home, autoexec=["wait 360"], timeout=args.timeout),
               [], report)
    after_menu = snapshot("menu-again")
    changes = compare_profiles(after_play, after_menu, {}, {})
    report["menuAgainChanges"] = changes["migrations"] + changes["rebinds"]
    if report["menuAgainChanges"]:
        report.setdefault("problems", []).append(
            f"the third start changed {len(report['menuAgainChanges'])} archived value(s) the profile held")
    lost = [c for c in report["gameplayChanges"] if c.get("after") is None]
    if lost:
        report.setdefault("problems", []).append(f"the level start dropped {len(lost)} archived value(s)")
    # Multiplayer-only settings, as a multiplayer session leaves them, must
    # survive a level start and a menu start that never load that module.
    seeded = after_menu + "".join(f'seta {name} "{value}"\n' for name, value in MODULE_ONLY_CVARS.items())
    config.write_text(seeded, encoding="utf-8")
    run_launch(Launch("module-level", runtime, basepath, home, startup=["+map", args.map],
                      after_load=["wait 60", "seta com_showFPS 1", "wait 5"], timeout=args.timeout), [], report)
    run_launch(Launch("module-menu", runtime, basepath, home, autoexec=["wait 360", "seta com_showFPS 0", "wait 5"],
                      timeout=args.timeout), [], report)
    survived = parse_config(snapshot("module-settings"))[0]
    report["moduleOnlySettings"] = {name: survived.get(name) for name in MODULE_ONLY_CVARS}
    for name, value in MODULE_ONLY_CVARS.items():
        if name not in survived or not same_value(survived[name], value):
            report.setdefault("problems", []).append(
                f"lost the multiplayer setting {name}={value} (now {survived.get(name)!r})")
    return finish(output, report)


def command_upgrade(args: argparse.Namespace) -> int:
    output = args.output.resolve()
    home = output / "home"
    if home.exists():
        shutil.rmtree(home)
    runtime, previous, basepath = args.runtime.resolve(), args.previous.resolve(), args.basepath.resolve()
    report: dict[str, Any] = {"mode": "upgrade", "runtime": str(runtime), "previous": str(previous),
                              "moduleOnlyCvars": MODULE_ONLY_CVARS,
                              "profileCvars": PROFILE_CVARS, "profileBinds": PROFILE_BINDS}
    game = home / GAME_DIR
    game.mkdir(parents=True)
    awakening = args.awakening.resolve().as_posix() if args.awakening else None
    (game / "rel_profile").mkdir(parents=True, exist_ok=True)
    (game / "rel_profile" / "profile.cfg").write_text("\n".join(profile_cfg_lines(awakening)) + "\n", encoding="utf-8")
    expected = dict(PROFILE_CVARS)
    if awakening:
        expected["fs_awakeningpath"] = awakening
    # The previous release starts the map from its own new profile, applies the
    # player's choices, plays and saves; quitting writes its config.
    make = Launch("previous", previous, basepath, home, startup=["+exec", "rel_profile/profile.cfg", "+map", args.map],
                  after_load=["god", "notarget", "wait 150", "saveGame rel_upgrade", "wait 10", shot("previous")],
                  timeout=args.timeout)
    run_launch(make, ["previous"], report)
    config = game / CONFIG_NAME
    if not config.is_file() or not (game / "savegames" / "rel_upgrade.save").is_file():
        report.setdefault("problems", []).append("the previous release left no config or no save")
        return finish(output, report)
    before = config.read_text(encoding="utf-8", errors="replace")
    shutil.copyfile(config, output / "config-previous.cfg")
    old_save = saves.Payload((game / "savegames" / "rel_upgrade.save").read_bytes())
    report["previousSave"] = old_save.summary()
    # The candidate on the same profile: restore the old save, play, resave.
    load = Launch("candidate", runtime, basepath, home, startup=["+loadGame", "rel_upgrade"],
                  after_load=["wait 150", "saveGame rel_upgrade_resave", "wait 10", shot("candidate")],
                  timeout=args.timeout)
    result = run_launch(load, ["candidate"], report)
    text = Path(result["log"]).read_text(encoding="utf-8", errors="replace") if Path(result["log"]).is_file() else ""
    if not RESTORE_STARTED.search(text):
        report.setdefault("problems", []).append("the candidate did not restore the previous release's save")
    resave = game / "savegames" / "rel_upgrade_resave.save"
    if resave.is_file():
        payload = saves.Payload(resave.read_bytes())
        report["candidateResave"] = payload.summary()
        if not saves.portable(payload):
            report.setdefault("problems", []).append("the resave is not portable v5")
    else:
        report.setdefault("problems", []).append("the candidate wrote no resave")
    # Then the menu path players take: a plain start on the profile.
    menu = Launch("candidate-menu", runtime, basepath, home, autoexec=["wait 360"], timeout=args.timeout)
    run_launch(menu, [], report)
    after = config.read_text(encoding="utf-8", errors="replace") if config.is_file() else ""
    shutil.copyfile(config, output / "config-candidate.cfg")
    comparison = compare_profiles(before, after, expected, PROFILE_BINDS)
    report["comparison"] = comparison
    for lost in comparison["lost"]:
        report.setdefault("problems", []).append(f"lost player choice {json.dumps(lost)}")
    if comparison["notSavedByPrevious"]:
        report["notSavedByPrevious"] = comparison["notSavedByPrevious"]
    return finish(output, report)


def command_save_roundtrip(args: argparse.Namespace) -> int:
    output = args.output.resolve()
    report: dict[str, Any] = {"mode": "save-roundtrip", "runtime": str(args.runtime.resolve()), "legs": []}
    home_write, home_load = output / "home-write", output / "home-load"
    for home in (home_write, home_load):
        if home.exists():
            shutil.rmtree(home)
    script = SCRIPT_DIR / "savegame_cross_load.py"
    common = ["--install-root", str(args.runtime.resolve()), "--basepath", str(args.basepath.resolve()),
              "--timeout", str(args.timeout)]
    legs = [
        ["write", *common, "--home", str(home_write), "--tag", "rel_write", "--map", args.map],
        ["load", *common, "--home", str(home_load), "--tag", "rel_load",
         "--input", str(home_write / GAME_DIR / "savegames" / "rel_write.save")],
        ["load", *common, "--home", str(home_load), "--tag", "rel_frozen_load",
         "--input", str(output / "frozen-input" / "rel_load_frozen.save")],
    ]
    for index, leg in enumerate(legs):
        if index == 2:
            frozen = home_load / GAME_DIR / "savegames" / "rel_load_frozen.save"
            if not frozen.is_file():
                report.setdefault("problems", []).append("no frozen resave to reload")
                break
            (output / "frozen-input").mkdir(parents=True, exist_ok=True)
            shutil.copyfile(frozen, output / "frozen-input" / "rel_load_frozen.save")
        completed = subprocess.run([sys.executable, str(script), *leg], capture_output=True, text=True,
                                   timeout=args.timeout + 120)
        brief = completed.stdout.strip().splitlines()[-1] if completed.stdout.strip() else ""
        try:
            parsed = json.loads(brief)
        except ValueError:
            parsed = {"raw": completed.stdout[-2000:], "stderr": completed.stderr[-2000:]}
        report["legs"].append({"leg": leg[0], "exitCode": completed.returncode, "result": parsed})
        if completed.returncode != 0:
            report.setdefault("problems", []).append(f"{leg[0]} leg {index + 1} failed: {parsed.get('problems')}")
            break
    return finish(output, report)


TIMEDEMO = re.compile(r"(\d+) frames rendered in ([\d.]+) seconds = ([\d.]+) fps")


def command_demo(args: argparse.Namespace) -> int:
    """Record a demo in gameplay, then time its playback in a fresh process that quits at the end."""
    output = args.output.resolve()
    home = output / "home"
    if home.exists():
        shutil.rmtree(home)
    runtime, basepath = args.runtime.resolve(), args.basepath.resolve()
    report: dict[str, Any] = {"mode": "demo", "runtime": str(runtime)}
    run_launch(Launch("record", runtime, basepath, home, startup=["+map", args.map],
                      after_load=["wait 30", "recordDemo rel_demo", "wait 240", shot("recording"), "stopRecording",
                                  "wait 10"], timeout=args.timeout), ["recording"], report)
    demos = sorted((home / GAME_DIR / "demos").glob("rel_demo*")) if (home / GAME_DIR / "demos").is_dir() else []
    report["demoFiles"] = [{"name": d.name, "bytes": d.stat().st_size} for d in demos]
    if not demos or not any(d.stat().st_size > 1024 for d in demos):
        report.setdefault("problems", []).append("no recorded demo")
        return finish(output, report)
    playback = run_launch(Launch("playback", runtime, basepath, home, startup=["+timeDemoQuit", "rel_demo"],
                                 timeout=args.timeout, done=TIMEDEMO), [], report)
    text = Path(playback["log"]).read_text(encoding="utf-8", errors="replace") if Path(playback["log"]).is_file() else ""
    match = TIMEDEMO.search(text)
    if match:
        report["timeDemo"] = {"frames": int(match.group(1)), "seconds": float(match.group(2)), "fps": float(match.group(3))}
        if int(match.group(1)) < 100:
            report.setdefault("problems", []).append(f"the demo played only {match.group(1)} frames")
    return finish(output, report)


def corrupt_generated_cache(game: Path, limit: int = 24) -> dict[str, Any]:
    """Damage some of a profile's generated cache files: garbage headers, truncation and a removal."""
    files = sorted(p for p in (game / "generated").rglob("*") if p.is_file()) if (game / "generated").is_dir() else []
    touched = {"garbled": 0, "truncated": 0, "removed": 0, "available": len(files)}
    for index, path in enumerate(files[:limit]):
        data = path.read_bytes()
        if index % 3 == 0:
            path.write_bytes(b"\xde\xad\xbe\xef" * 16 + data[64:])
            touched["garbled"] += 1
        elif index % 3 == 1:
            path.write_bytes(data[: max(1, len(data) // 2)])
            touched["truncated"] += 1
        else:
            path.unlink()
            touched["removed"] += 1
    return touched


def command_lifecycle(args: argparse.Namespace) -> int:
    output = args.output.resolve()
    home = output / "home"
    if home.exists():
        shutil.rmtree(home)
    runtime, basepath = args.runtime.resolve(), args.basepath.resolve()
    report: dict[str, Any] = {"mode": "lifecycle", "runtime": str(runtime)}
    play = ["wait 90"]
    cold = run_launch(Launch("cold", runtime, basepath, home, startup=["+map", args.map],
                             after_load=[*play, shot("cold")], timeout=args.timeout), ["cold"], report)
    warm = run_launch(Launch("warm", runtime, basepath, home, startup=["+map", args.map],
                             after_load=[*play, shot("warm")], timeout=args.timeout), ["warm"], report)
    report["startup"] = {"coldToMap": cold["marks"].get("map"), "warmToMap": warm["marks"].get("map")}
    if cold["marks"].get("map") and warm["marks"].get("map") and warm["marks"]["map"] > cold["marks"]["map"] * 1.5:
        report.setdefault("observations", []).append("the warm start reached the map more slowly than the cold start")
    report["cacheDamage"] = corrupt_generated_cache(home / GAME_DIR)
    if not report["cacheDamage"]["available"]:
        report.setdefault("observations", []).append("the profile had no generated cache to damage")
    run_launch(Launch("damaged-cache", runtime, basepath, home, startup=["+map", args.map],
                      after_load=[*play, shot("damaged-cache")], timeout=args.timeout), ["damaged-cache"], report)
    session = [
        *play, shot("session-start"),
        "set r_customWidth 1280", "set r_customHeight 720", "set r_windowWidth 1280", "set r_windowHeight 720",
        "vid_restart", "wait 60", shot("after-resize"),
        "vid_restart", "wait 90", shot("after-restart"),
        "set g_stopTime 1", "wait 60", shot("paused"), "set g_stopTime 0", "wait 30",
        "set timescale 0.25", "wait 120", shot("slow-motion"), "set timescale 1", "wait 30",
        "saveGame rel_lifecycle", "wait 10", "disconnect", "wait 30", "map " + args.second_map, "wait 120",
    ]
    second = Launch("session", runtime, basepath, home, startup=["+map", args.map], after_load=session,
                    timeout=args.timeout + 300)
    # The second map's after-load config is empty, so the first one ends the run
    # after the map change by quitting from its own tail.
    result = run_launch(second, ["session-start", "after-resize", "after-restart", "paused", "slow-motion"], report)
    sizes = {c["name"]: (c.get("width"), c.get("height")) for c in result["captures"]}
    if sizes.get("after-resize") not in ((1280, 720),):
        report.setdefault("problems", []).append(f"the resized capture is {sizes.get('after-resize')}, not 1280x720")
    # The renderer API changes only at engine start (r_renderApi): relaunch the
    # same profile on Vulkan, restart its renderer in gameplay, and back.
    vulkan = run_launch(Launch("vulkan", runtime, basepath, home, renderer="vulkan", startup=["+map", args.map],
                               after_load=[*play, shot("vulkan"), "vid_restart", "wait 90", shot("vulkan-restart")],
                               timeout=args.timeout), ["vulkan", "vulkan-restart"], report)
    report["vulkanRenderApis"] = vulkan.get("renderApis", [])
    if "vulkan" not in vulkan.get("renderApis", []):
        report.setdefault("problems", []).append(f"the Vulkan relaunch ran {vulkan.get('renderApis')}")
    run_launch(Launch("back-to-gl", runtime, basepath, home, startup=["+map", args.map],
                      after_load=[*play, shot("back-to-gl")], timeout=args.timeout), ["back-to-gl"], report)
    return finish(output, report)


def self_test() -> int:
    config_a = "\n".join([
        'seta sensitivity "7.25"', 'seta ui_name "RelProfileTester"', 'seta r_mode "-1"', 'seta s_volume "0.85"',
        'seta r_shadows "1"', 'bind "F7" "echo rel_profile_bind_f7"', 'bind KP_5 "_impulse13"', 'bind TAB "_impulse19"',
    ])
    config_b = "\n".join([
        'seta sensitivity "7.250000"', 'seta ui_name "RelProfileTester"', 'seta r_mode "-1"', 'seta s_volume "0.5"',
        'seta r_shadows "2"', 'seta com_newThing "1"', 'bind "F7" "echo rel_profile_bind_f7"', 'bind TAB "_impulse19"',
    ])
    cvars, binds = parse_config(config_a)
    assert cvars["sensitivity"] == "7.25" and binds["F7"] == "echo rel_profile_bind_f7" and binds["KP_5"] == "_impulse13"
    result = compare_profiles(config_a, config_b, {"sensitivity": "7.25", "s_volume": "0.85", "ui_name": "RelProfileTester",
                                                   "g_fov": "105"}, {"F7": "echo rel_profile_bind_f7", "KP_5": "_impulse13"})
    lost = {entry.get("cvar") or entry.get("bind") for entry in result["lost"]}
    assert lost == {"s_volume", "KP_5"}, result["lost"]
    assert result["notSavedByPrevious"] == ["g_fov"], result
    assert {m["cvar"] for m in result["migrations"]} == {"r_shadows", "com_newThing"}, result["migrations"]
    assert same_value("1.3", "1.300000") and not same_value("1.3", "1.31") and same_value("abc", "abc")
    # A save from another build restores; only the warning form is a failure.
    assert not RESTORE_FAILED.search("^1Savegame 'savegames/a.save' was produced by build/source 2636/ab (439 files); "
                                     "current schema-compatible build/source is 2662/cd (439 files)")
    assert RESTORE_FAILED.search("^3WARNING: ^1Savegame 'savegames/a.save' has an incompatible layout")
    assert RESTORE_FAILED.search("could not restore saved state: missing object")
    lines = profile_cfg_lines("E:/x")
    assert 'seta fs_awakeningpath "E:/x"' in lines and any(line.startswith("bind MOUSE4") for line in lines)
    for name in PROFILE_CVARS:
        assert name not in FORCED, name
    scratch = ROOT / ".tmp"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="release-profile-", dir=scratch) as temp:
        root = Path(temp)
        launch = Launch("t", root / "runtime", root / "base", root / "home", startup=["+map", "game/airdefense1"],
                        after_load=["wait 5"])
        command = launch.command()
        assert command[command.index("r_hiddenWindow") + 1] == "1" and command[-2:] == ["+map", "game/airdefense1"]
        assert command[command.index("g_autoExecAfterMapLoad") + 1] == "rel_profile/t.cfg"
        assert command[command.index("net_ip") + 1] == "127.0.0.1"
        launch.prepare()
        cfg = (root / "home" / GAME_DIR / "rel_profile" / "t.cfg").read_text(encoding="utf-8").splitlines()
        assert cfg[0] == 'set g_autoExecAfterMapLoad ""' and cfg[-1] == "quit" and f"echo {DONE}" in cfg, cfg
        game = root / "home" / GAME_DIR
        (game / "generated" / "images").mkdir(parents=True)
        for index in range(6):
            (game / "generated" / "images" / f"{index}.bimage").write_bytes(bytes(range(256)) * 2)
        damage = corrupt_generated_cache(game)
        assert damage == {"garbled": 2, "truncated": 2, "removed": 2, "available": 6}, damage
    print("release_profile_runtime: self-test ok")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("clean-install", "upgrade", "save-roundtrip", "lifecycle", "demo"):
        sub = commands.add_parser(name)
        sub.add_argument("--runtime", type=Path, required=True, help="the candidate runtime")
        sub.add_argument("--basepath", type=Path, required=True, help="retail Quake 4 root holding q4base")
        sub.add_argument("--output", type=Path, required=True)
        sub.add_argument("--map", default="game/airdefense1")
        sub.add_argument("--timeout", type=int, default=420, help="seconds per launch")
        if name == "upgrade":
            sub.add_argument("--previous", type=Path, required=True, help="the previous release's runtime")
            sub.add_argument("--awakening", type=Path, help="Awakening location to store in the profile")
        if name == "lifecycle":
            sub.add_argument("--second-map", default="game/airdefense2")
    commands.add_parser("self-test")
    args = parser.parse_args(argv)
    if args.command == "self-test":
        return self_test()
    handlers = {"clean-install": command_clean_install, "upgrade": command_upgrade,
                "save-roundtrip": command_save_roundtrip, "lifecycle": command_lifecycle, "demo": command_demo}
    return handlers[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
