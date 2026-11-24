#!/usr/bin/env python3
"""Opt-in campaign gameplay/save checks using user-supplied game content.

Uses the staged client, windowed hidden rendering and console commands. It
never injects input or captures the desktop. All writable data stays in .tmp.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AWAKENING_MAPS = (
    "m01_stranarus_trench1", "m01_stranarus_trench2", "m02_trianfac",
    "m03_airassault", "m04_prison", "m05_bio", "m06_mcc", "m06_mcc_invasion",
    "m07_race", "m07_race1", "m07_race2", "m08_cryofac", "m09_valkaryne",
)
STOCK_MAPS = ("airdefense1", "mcc_landing", "convoy1", "hub1", "walker")
ERROR = re.compile(r"(?mi)^(?:\^\d)?(?:ERROR:|FATAL ERROR:).*$")


def task_path(path: Path) -> Path:
    absolute = path.resolve()
    if not absolute.is_relative_to(ROOT / ".tmp"):
        raise ValueError(f"runtime output must be inside {ROOT / '.tmp'}")
    for parent in (path, *path.parents):
        if parent == ROOT:
            break
        if parent.is_symlink() or (parent.exists() and
                getattr(parent.stat(follow_symlinks=False), "st_file_attributes", 0) & 0x400):
            raise ValueError(f"runtime output traverses a link: {parent}")
    return absolute


def pack_content(source: Path, destination: Path) -> None:
    """Independent read-only asset copy; never include executable modules."""
    if destination.with_name(destination.stem + '-00.pk4').exists():
        return
    source = source.resolve(strict=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    # idTech's legacy ZIP interface is bounded by signed 32-bit offsets.
    # Split the test copy rather than creating an unreadable ZIP64 archive.
    archive = None
    size = 0
    index = 0
    try:
        for path in sorted(source.rglob("*")):
            if path.is_symlink():
                raise ValueError(f"asset source contains a link: {path}")
            if not path.is_file() or path.suffix.lower() in (".dll", ".exe", ".pdb", ".log"):
                continue
            length = path.stat().st_size
            if length > 1024 * 1024 * 1024:
                raise ValueError(f"asset exceeds the test pack limit: {path}")
            if archive is None or size + length > 1024 * 1024 * 1024:
                if archive is not None:
                    archive.close()
                target = destination.with_name(destination.stem + f'-{index:02d}.pk4')
                archive = zipfile.ZipFile(target, "w", compression=zipfile.ZIP_STORED, allowZip64=False)
                index += 1
                size = 0
            archive.write(path, path.relative_to(source).as_posix())
            size += length + 1024
    finally:
        if archive is not None:
            archive.close()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_case(args, save_root: Path, game: str, map_name: str, load: bool) -> dict:
    label = f"{game}_{map_name}_{'load' if load else 'save'}"
    game_root = save_root / game
    game_root.mkdir(parents=True, exist_ok=True)
    marker = f"CAMPAIGN_RUNTIME_{label}"
    cfg_name = f"campaign_runtime_{args.phase}.cfg"
    save_name = f"campaign_{map_name}"
    lines = ["waitMsec 3000", f"openq4_assertMapState game/{map_name}", f"echo {marker}"]
    if not load:
        lines += [f"saveGame {save_name}", "waitMsec 500"]
    lines += ["quit"]
    (game_root / cfg_name).write_text("\n".join(lines) + "\n", encoding="ascii")
    exe = args.exe.resolve(strict=True)
    log_relative = f"logs/{args.phase}_{label}.log"
    command = [str(exe)]
    settings = {
        "win_allowMultipleInstances": "1", "r_hiddenWindow": "1", "r_fullscreen": "0",
        "s_noSound": "1", "logFile": "2", "logFileName": log_relative,
        "in_mouse": "0", "in_joystick": "0",
        "developer": "1", "com_skipLoadingContinue": "1", "g_autoSkipCinematics": "1",
        "fs_basepath": str(args.retail.resolve(strict=True)), "fs_savepath": str(save_root),
        "fs_devpath": str(save_root), "fs_cachepath": str(save_root / 'cache'),
        "r_fullscreenDesktop": "0", "r_borderless": "0", "r_borderlessDefaultMigrated": "1",
        "fs_game": game, "si_gameType": "singleplayer", "ui_autoJoin": "1",
        "r_renderApi": args.renderer, "g_autoExecAfterMapLoad": cfg_name,
        "g_autoExecAfterMapLoadDelayMs": "500",
    }
    if args.awakening_root:
        settings['fs_awakeningpath'] = str(args.awakening_root.resolve(strict=True))
    settings['win_allowMultipleInstances' if os.name == 'nt' else 'sys_allowMultipleInstances'] = '1'
    for key, value in settings.items():
        command += ["+set", key, value]
    command += ["+loadGame", save_name] if load else ["+map", "game/" + map_name]
    stdout_path = game_root / "logs" / f"{args.phase}_{label}.stdout"
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    with stdout_path.open("w", encoding="utf-8") as output:
        options = {}
        if os.name == 'nt':
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
        process = subprocess.Popen(command, cwd=exe.parent, stdin=subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT, **options)
        try:
            code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            code = "timeout"
    log_path = game_root / log_relative
    log = log_path.read_text(encoding="latin1") if log_path.exists() else ""
    relevant = log[log.rfind("loading a v"):] if load and "loading a v" in log else log
    errors = ERROR.findall(relevant)
    save_path = game_root / "savegames" / (save_name + ".save")
    passed = code == 0 and marker in relevant and not errors and save_path.is_file()
    result = {"game": game, "map": map_name, "operation": "load" if load else "save",
              "passed": passed, "exit": code, "seconds": round(time.monotonic() - started, 2),
              "errors": errors, "marker": marker in relevant, "log": str(log_path),
              "save": str(save_path)}
    print(f"{args.phase} {game} {map_name} {result['operation']}: {'PASS' if passed else 'FAIL'} "
          f"({result['seconds']}s) {errors[:1]}", flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--awakening-content", type=Path)
    parser.add_argument("--awakening-root", type=Path, help="read-only installed content root or q4xbase directory")
    parser.add_argument("--exe", type=Path, default=ROOT / ".install/openQ4-client_x64.exe")
    parser.add_argument("--save-root", type=Path, default=ROOT / ".tmp/game-consolidation/saves")
    parser.add_argument("--phase", default="candidate")
    parser.add_argument("--renderer", choices=("gl", "vulkan"), default="gl")
    parser.add_argument("--game", choices=("baseoq4", "q4xbase", "both"), default="both")
    parser.add_argument("--load-only", action="store_true")
    parser.add_argument("--maps", nargs="*")
    parser.add_argument("--timeout", type=int, default=240)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9_-]+", args.phase):
        parser.error("phase must be a filename-safe identifier")
    if args.maps is not None and any(not re.fullmatch(r"[a-zA-Z0-9_-]+", name) for name in args.maps):
        parser.error("maps must be campaign map identifiers")
    save_root = task_path(args.save_root)
    save_root.mkdir(parents=True, exist_ok=True)
    if args.awakening_content:
        pack_content(args.awakening_content, save_root / "q4xbase/awakening-runtime-test.pk4")
    results = []
    report_path = save_root / (args.phase + "-" + args.renderer + ".json")
    report = {"schema": 1, "phase": args.phase, "renderer": args.renderer,
              "executable_sha256": sha256(args.exe),
              "module_sha256": {path.name: sha256(path) for path in
                                sorted((args.exe.parent / "baseoq4").glob("game_*"))
                                if path.suffix.lower() in (".dll", ".so", ".dylib")},
              "complete": False, "results": results}
    def write_report():
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_report()
    for game in ("baseoq4", "q4xbase"):
        if args.game != "both" and args.game != game:
            continue
        maps = args.maps if args.maps is not None else (STOCK_MAPS if game == "baseoq4" else AWAKENING_MAPS)
        for name in maps:
            if not args.load_only:
                results.append(run_case(args, save_root, game, name, False))
                write_report()
            if args.load_only or results[-1]["passed"]:
                results.append(run_case(args, save_root, game, name, True))
                write_report()
    report["complete"] = True
    write_report()
    return 0 if results and all(result["passed"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
