#!/usr/bin/env python3
"""One client process plays the campaign, network play and the campaign again.

CORE-07 (docs/dev/unified-game-module.md): one library, game_<arch>, serves the
campaign (mode game_sp) and network play (mode game_mp), and the engine reloads
it whenever the next session needs the other mode. This smoke drives a single
hidden, silent client through

    airdefense1 (game_sp) -> listen server on q4dm1 (game_mp) -> hub1 (game_sp)

with g_autoExecAfterMapLoad cfgs, so each phase runs only once its map is live,
and passes when

  * the loader selects game_sp, game_mp, game_sp, each from game_<arch>;
  * all three maps load, the game initialises three times and every phase's
    cfg runs in gameplay (the network phase also checks it is a live server);
  * the mode cvars (idModeCVar) take each mode's default: si_pure 0/1/0, and
    pm_usecylinder differs between the modes and is the same in both campaigns;
  * pm_usecylinder, which nothing in the run sets, also takes each mode's
    default as its value, so the listen server uses network play's cylinder
    rather than the campaign's box;
  * nothing logs an error, and no mode cvar is reported as declared twice;
  * the config the run leaves holds no pm_usecylinder 0 carried over from the
    campaign (network play archives the cvar; the campaign must not save the
    box it follows into it).

With --loader-trace on Linux the dynamic loader's log must also show the module
initialised three times and finalised at both mode changes, so each mode starts
from a fresh image.

Every phase writes its own log, because the engine reopens logFileName on a
module reload. A module swap resets command-line testing cvars such as
com_skipLoadingContinue, so each phase's cfg sets them again before its map.

Usage:
    game_mode_switch_smoke.py --install-root .install --basepath "<Quake 4>" --out .tmp/mode-switch
    game_mode_switch_smoke.py --self-test
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

LOGS = ("phase1.log", "phase2.log", "phase3.log")
PHASES = ("SP1", "MP", "SP2")
MAPS = ("game/airdefense1", "mp/q4dm1", "game/hub1")
MODES = ("game_sp", "game_mp", "game_sp")
MODE_CVARS = ("pm_usecylinder", "si_pure", "si_gameType")
REASSERT = ("set com_skipLoadingContinue 1", "set com_loadingContinueAutoAdvance 1500")
COLOR = re.compile(r"\^\d")


def arch_tag() -> str:
    import platform
    machine = platform.machine().lower()
    return "arm64" if machine in ("arm64", "aarch64") else "x64"


def write_cfgs(game: Path, port: int) -> None:
    prints = [*MODE_CVARS]
    cfgs = {
        "ms_sp1.cfg": ["echo MS_PHASE_SP1", *prints, "echo MS_PHASE_SP1_END",
                       "set logFileName logs/phase2.log", "set g_autoExecAfterMapLoad ms_mp.cfg",
                       "set si_gameType DM", "set si_map mp/q4dm1", "set net_LANServer 1",
                       f"set net_port {port}", *REASSERT, "spawnServer mp/q4dm1"],
        "ms_mp.cfg": ["echo MS_PHASE_MP", *prints, "serverInfo", "echo MS_PHASE_MP_END", "waitMsec 3000",
                      "set logFileName logs/phase3.log", "set g_autoExecAfterMapLoad ms_sp2.cfg",
                      "disconnect", "set si_gameType singleplayer", *REASSERT, "map game/hub1"],
        "ms_sp2.cfg": ["echo MS_PHASE_SP2", *prints, "echo MS_PHASE_SP2_END", "waitMsec 2000", "quit"],
    }
    for name, lines in cfgs.items():
        (game / name).write_text("\n".join(lines) + "\n", encoding="ascii")


def gather(game: Path) -> str:
    return "".join((game / "logs" / n).read_text(encoding="latin-1", errors="replace")
                   for n in LOGS if (game / "logs" / n).exists())


def phase_block(text: str, phase: str) -> str:
    m = re.search(rf"^MS_PHASE_{phase}[ \t]*\r?\n(.*?)^MS_PHASE_{phase}_END", text, re.S | re.M)
    return m.group(1) if m else ""


def cvar_field(block: str, name: str, field: str) -> str | None:
    for line in block.splitlines():
        line = COLOR.sub("", line).strip()
        if line.startswith(f'"{name}"'):
            m = re.search(rf'{field}:"([^"]*)"', line)
            return m.group(1) if m else None
    return None


def cvar_default(block: str, name: str) -> str | None:
    return cvar_field(block, name, "default")


def archived_shapes(config: str) -> list[str]:
    return re.findall(r'^\s*seta\s+pm_usecylinder\s+"?([^"\s]*)"?', config, re.M | re.I)


def evaluate(text: str, binary: str, loader: dict | None = None, config: str | None = None) -> dict:
    modules = re.findall(r"Selected game module: logical='(\w+)' binary='([\w-]+)'", text)
    # map loads only; the collision world logs its own "msec to load collision"
    loads = re.findall(r"msec to load (\S+/\S+)", text)
    phases = [p for p in PHASES if phase_block(text, p)]
    errors = [COLOR.sub("", l).strip() for l in text.splitlines() if re.search(r"FATAL|ERROR:", l)]
    twice = sorted({COLOR.sub("", l).strip() for l in text.splitlines()
                    if "declared multiple times" in l and any(f"'{c}'" in l for c in MODE_CVARS)})
    inits = text.count("------------- Initializing Game -------------")
    defaults = {p: {c: cvar_default(phase_block(text, p), c) for c in MODE_CVARS} for p in PHASES}
    # pm_usecylinder follows the mode while nothing sets it: the campaign's box,
    # network play's cylinder, the box again
    shapes = [cvar_field(phase_block(text, p), "pm_usecylinder", "is") for p in PHASES]
    mp_block = phase_block(text, "MP")
    # serverInfo answers only on a running server: "server '<name>' IP = ..." and "si_map = <map>"
    live_server = bool(re.search(r"^server '", mp_block, re.M) and re.search(r"^si_map = mp/q4dm1\s*$", mp_block, re.M | re.I))
    failures = []
    if [m[0] for m in modules] != list(MODES):
        failures.append(f"module sequence {[m[0] for m in modules]}")
    if any(m[1] != binary for m in modules):
        failures.append(f"binaries {[m[1] for m in modules]}, expected {binary}")
    if loads != list(MAPS):
        failures.append(f"loads {loads}")
    if inits != 3:
        failures.append(f"{inits} game inits")
    if phases != list(PHASES):
        failures.append(f"phases that ran in gameplay {phases}")
    if not live_server:
        failures.append("the network phase is not a live server on mp/q4dm1")
    sp1, mp, sp2 = (defaults[p] for p in PHASES)
    if (sp1["si_pure"], mp["si_pure"], sp2["si_pure"]) != ("0", "1", "0"):
        failures.append(f"si_pure defaults {sp1['si_pure']}/{mp['si_pure']}/{sp2['si_pure']}")
    if None in (sp1["pm_usecylinder"], mp["pm_usecylinder"]) or sp1["pm_usecylinder"] != sp2["pm_usecylinder"] \
            or sp1["pm_usecylinder"] == mp["pm_usecylinder"]:
        failures.append(f"pm_usecylinder defaults {sp1['pm_usecylinder']}/{mp['pm_usecylinder']}/{sp2['pm_usecylinder']}")
    if None in shapes or shapes[0] != sp1["pm_usecylinder"] or shapes[1] != mp["pm_usecylinder"]             or shapes[2] != sp2["pm_usecylinder"]:
        failures.append(f"pm_usecylinder values {shapes} do not follow the mode")
    if errors:
        failures.append(f"errors: {errors[:3]}")
    if twice:
        failures.append(f"mode cvars declared twice: {twice[:3]}")
    saved = archived_shapes(config) if config is not None else None
    if saved and saved[-1] == "0":
        failures.append("the config saved the campaign's pm_usecylinder 0")
    if loader is not None and (loader.get("init") != 3 or loader.get("fini", 0) < 2):
        failures.append(f"loader trace init {loader.get('init')} fini {loader.get('fini')}")
    return {"passed": not failures, "failures": failures, "modules": modules, "loads": loads,
            "phases": phases, "gameInits": inits, "modeCvarDefaults": defaults, "shapes": shapes,
            "savedShapes": saved,
            "loader": loader}


def read_loader_trace(home: Path, module_file: str) -> dict:
    loader = {"init": 0, "fini": 0, "file": None}
    for path in sorted(home.glob("ld-debug.*")):
        content = path.read_text(encoding="latin-1", errors="replace")
        if module_file not in content:
            continue
        loader["file"] = path.name
        for line in content.splitlines():
            if module_file in line and "calling init:" in line:
                loader["init"] += 1
            if module_file in line and "calling fini:" in line:
                loader["fini"] += 1
        break
    return loader


def run(args: argparse.Namespace) -> int:
    win = os.name == "nt"
    tag = arch_tag()
    binary = f"game_{tag}"
    root = args.install_root.resolve()
    client = root / (f"openQ4-client_{tag}.exe" if win else f"openQ4-client_{tag}")
    if not client.is_file():
        print(f"no client at {client}", file=sys.stderr)
        return 2
    home = args.out.resolve()
    if home.exists():
        shutil.rmtree(home)
    game = home / "baseoq4"
    game.mkdir(parents=True)
    write_cfgs(game, args.port)
    cvars = {
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(home), "fs_homepath": str(home),
        "fs_cachepath": str(home / "cache"), "logFile": "2", "logFileName": "logs/phase1.log",
        "developer": "1", "s_noSound": "1", "in_mouse": "0", "in_joystick": "0",
        "com_skipLoadingContinue": "1", "com_loadingContinueAutoAdvance": "1500",
        "g_autoSkipCinematics": "1", "com_skipLogoVideos": "1",
        ("win_allowMultipleInstances" if win else "sys_allowMultipleInstances"): "1",
        "r_renderApi": "gl", "r_fullscreen": "0", "r_fullscreenDesktop": "0", "r_borderless": "0",
        "r_borderlessDefaultMigrated": "1", "r_hiddenWindow": "1", "r_mode": "-1",
        "r_customWidth": "640", "r_customHeight": "360", "r_windowWidth": "640", "r_windowHeight": "360",
        "r_swapInterval": "0", "ui_retained": "1", "ui_autoJoin": "1", "net_ip": "127.0.0.1",
        "net_port": str(args.port), "si_gameType": "singleplayer",
        "g_autoExecAfterMapLoad": "ms_sp1.cfg", "g_autoExecAfterMapLoadDelayMs": "1500",
    }
    cmd = [str(client)]
    if args.xvfb:
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24", *cmd]
    for key, value in cvars.items():
        cmd += ["+set", key, value]
    cmd += ["+map", MAPS[0]]
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", ALSOFT_DRIVERS="null")
    if args.loader_trace and not win:
        env.update(LD_DEBUG="files", LD_DEBUG_OUTPUT=str(home / "ld-debug"))
    started = time.monotonic()
    with (home / "console.log").open("w") as console:
        proc = subprocess.Popen(cmd, cwd=root, stdin=subprocess.DEVNULL, stdout=console,
                                stderr=subprocess.STDOUT, env=env, start_new_session=not win,
                                creationflags=subprocess.CREATE_NO_WINDOW if win else 0)
        try:
            while time.monotonic() - started < args.timeout and proc.poll() is None:
                time.sleep(2)
                if re.search(r"FATAL|ERROR:", gather(game)):
                    break
        finally:
            # this process only: never by image name, other sessions share the machine
            if proc.poll() is None:
                time.sleep(5)
            if proc.poll() is None:
                if win:
                    proc.kill()
                else:
                    os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
    loader = read_loader_trace(home, f"{binary}.so") if args.loader_trace and not win else None
    config_path = game / "openQ4Config.cfg"
    config = config_path.read_text(encoding="latin-1", errors="replace") if config_path.is_file() else ""
    report = evaluate(gather(game), binary, loader, config)
    report["seconds"] = round(time.monotonic() - started, 1)
    report["exitCode"] = proc.returncode
    (home / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("passed", "failures", "seconds", "modules", "loads",
                                             "phases", "gameInits", "modeCvarDefaults", "shapes",
                                             "savedShapes")}))
    return 0 if report["passed"] else 1


def synthetic_log(mp_pure: str = "1", modules=MODES, twice: bool = False, error: bool = False,
                  mp_shape: str = "1") -> str:
    out = []
    pure = {"SP1": "0", "MP": mp_pure, "SP2": "0"}
    cyl = {"SP1": "0", "MP": "1", "SP2": "0"}
    shape = {"SP1": "0", "MP": mp_shape, "SP2": "0"}
    for phase, module, map_name in zip(PHASES, modules, MAPS):
        out.append(f"Selected game module: logical='{module}' binary='game_x64' path='x'")
        out.append("------------- Initializing Game -------------")
        if twice and phase == "MP":
            out.append("WARNING: CVar 'si_pure' declared multiple times with different initial value")
        out.append("    12 msec to load collision")
        out.append(f"  1234 msec to load {map_name}")
        out.append(f"MS_PHASE_{phase}")
        out.append(f'"pm_usecylinder" is:"{shape[phase]}"^7 default:"{cyl[phase]}"')
        out.append(f'"si_pure" is:"{pure[phase]}"^7 default:"{pure[phase]}"')
        if phase == "MP":
            out.append("server 'openQ4 Server' IP = localhost:28713")
            out.append("si_map = mp/q4dm1")
        out.append(f"MS_PHASE_{phase}_END")
        if error and phase == "SP2":
            out.append("ERROR: something broke")
    return "\n".join(out) + "\n"


def self_test() -> int:
    good = evaluate(synthetic_log(), "game_x64")
    assert good["passed"], good["failures"]
    assert not evaluate(synthetic_log(mp_pure="0"), "game_x64")["passed"], "mode default not checked"
    assert not evaluate(synthetic_log(modules=("game_sp", "game_sp", "game_sp")), "game_x64")["passed"]
    assert not evaluate(synthetic_log(twice=True), "game_x64")["passed"], "declared-twice warning not checked"
    assert not evaluate(synthetic_log(error=True), "game_x64")["passed"], "errors not checked"
    assert not evaluate(synthetic_log(mp_shape="0"), "game_x64")["passed"], "the carried-over box not checked"
    assert not evaluate(synthetic_log(), "game-sp_x64")["passed"], "binary name not checked"
    assert not evaluate(synthetic_log().replace("MS_PHASE_SP2_END", ""), "game_x64")["passed"]
    assert not evaluate(synthetic_log().replace("si_map = mp/q4dm1", ""), "game_x64")["passed"]
    assert not evaluate(synthetic_log(), "game_x64", {"init": 1, "fini": 0})["passed"], "loader trace not checked"
    assert evaluate(synthetic_log(), "game_x64", {"init": 3, "fini": 2})["passed"]
    assert evaluate(synthetic_log(), "game_x64", config='seta ui_name "x"\nseta pm_usecylinder "1"\n')["passed"]
    assert evaluate(synthetic_log(), "game_x64", config='seta ui_name "x"\n')["passed"]
    assert not evaluate(synthetic_log(), "game_x64", config='seta pm_usecylinder "0"\n')["passed"], \
        "the carried-over box in the config not checked"
    print("simulated pass")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true", help="Check the log evaluation only.")
    parser.add_argument("--install-root", type=Path, help="Staged package root (.install).")
    parser.add_argument("--basepath", type=Path, help="Retail Quake 4 root (q4base PK4s).")
    parser.add_argument("--out", type=Path, help="Scratch savepath for this run, under .tmp/.")
    parser.add_argument("--port", type=int, default=28713)
    parser.add_argument("--timeout", type=float, default=1500.0)
    parser.add_argument("--xvfb", action="store_true", help="Run the client under xvfb-run (Linux).")
    parser.add_argument("--loader-trace", action="store_true",
                        help="Linux: require LD_DEBUG to show the module loaded fresh in each mode.")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if not (args.install_root and args.basepath and args.out):
        parser.error("--install-root, --basepath and --out are required")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
