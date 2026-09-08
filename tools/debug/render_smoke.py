#!/usr/bin/env python3
"""Windowed stock-map renderer smoke using the engine screenshot command.

Portable counterpart to emileb's render_shot.sh. This does not inject input or
capture a desktop window. Each run has an isolated save/cache/config directory.
"""

import argparse
import json
import os
from pathlib import Path
import platform
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def inspect_screenshot(path):
    """Check the engine's uncompressed TGA output before accepting a smoke."""
    result = {"valid": False, "nonBlack": False, "colorVariation": False}
    if not path.is_file():
        return result
    data = path.read_bytes()
    if len(data) < 18 or data[1] != 0 or data[2] != 2 or data[16] not in (24, 32):
        return result
    width, height = struct.unpack_from("<HH", data, 12)
    stride = data[16] // 8
    start, end = 18 + data[0], 18 + data[0] + width * height * stride
    if not width or not height or len(data) < end:
        return result
    ranges = [(min(channel), max(channel)) for channel in
              (data[start + component:end:stride] for component in range(3))]
    result.update({"valid": True, "width": width, "height": height,
                   "nonBlack": any(high > 0 for _, high in ranges),
                   "colorVariation": any(low != high for low, high in ranges)})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, default=ROOT / ".install")
    parser.add_argument("--basepath", type=Path, required=True)
    parser.add_argument("--render-api", choices=("gl", "gles", "vk"), default="gles")
    parser.add_argument("--mode", choices=("sp", "mp"), default="sp")
    parser.add_argument("--map", default="")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--delay-ms", type=int, default=3000)
    parser.add_argument("--set-cvar", action="append", default=[], metavar="NAME=VALUE")
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    arch = "arm64" if platform.machine().lower() in ("aarch64", "arm64") else "x64"
    executable = runtime / (f"openQ4-client_{arch}" + (".exe" if os.name == "nt" else ""))
    if not executable.is_file():
        parser.error(f"Missing client: {executable}")
    output = (args.output or ROOT / ".tmp/android-gles-smoke" / f"{args.render_api}-{args.mode}-{time.time_ns()}").resolve()
    output.mkdir(parents=True, exist_ok=False)
    save = output / "save"
    game = save / "baseoq4"
    game.mkdir(parents=True)
    screenshot = game / "screenshots/android-gles-smoke.tga"
    (game / "android-gles-smoke.cfg").write_text(
        'gfxInfo\nr_actualRenderApi\nrendererTierSelfTest\n'
        'screenshot "screenshots/android-gles-smoke.tga"\n'
        'echo OPENQ4_SMOKE_DONE\nquit\n', encoding="utf-8")
    cvars = {
        "fs_basepath": str(args.basepath.resolve()), "fs_savepath": str(save),
        "fs_cachepath": str(output / "cache"), "fs_game": "baseoq4",
        "r_renderApi": "vulkan" if args.render_api == "vk" else args.render_api,
        "r_renderer": "glesd3" if args.render_api == "gles" else "arb2",
        "r_mode": "-1", "r_customWidth": "960", "r_customHeight": "540",
        "logFile": "2", "logFileName": "logs/openq4.log", "developer": "1",
        "com_skipIntroVideos": "1", "com_skipLoadingContinue": "1",
        "g_autoSkipCinematics": "1", "g_autoScreenshot": "0",
        "g_autoExecAfterMapLoad": "android-gles-smoke.cfg",
        "g_autoExecAfterMapLoadDelayMs": str(args.delay_ms),
        "ui_autoJoin": "1", "net_serverDedicated": "0", "si_pure": "0",
        "si_gameType": "singleplayer" if args.mode == "sp" else "DM",
    }
    for setting in args.set_cvar:
        name, separator, value = setting.partition("=")
        if not separator or not name or any(c.isspace() for c in name):
            parser.error("--set-cvar expects NAME=VALUE")
        cvars[name] = value
    # Diagnostic runs never acquire the user's input or open fullscreen.
    cvars.update({"r_fullscreen": "0", "r_hiddenWindow": "1", "in_mouse": "0", "in_joystick": "0", "in_tty": "0"})
    command = [str(executable)]
    for name, value in cvars.items():
        command.extend(("+set", name, value))
    map_name = args.map or ("game/hangar1" if args.mode == "sp" else "mp/q4dm1")
    command.extend(("+map" if args.mode == "sp" else "+spawnServer", map_name))
    (output / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    timed_out = False
    with (output / "console.log").open("wb") as console:
        try:
            result = subprocess.run(command, cwd=runtime, stdin=subprocess.DEVNULL, stdout=console,
                                    stderr=subprocess.STDOUT, timeout=args.timeout)
            exit_code = result.returncode
        except subprocess.TimeoutExpired:
            timed_out, exit_code = True, -1
    log_path = game / "logs/openq4.log"
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    screenshot_check = inspect_screenshot(screenshot)
    complete = "OPENQ4_SMOKE_DONE" in log and all(
        screenshot_check[key] for key in ("valid", "nonBlack", "colorVariation"))
    active_match = re.search(r'"r_actualRenderApi" is:"([^"\r\n]+)"', log)
    active_api = active_match.group(1) if active_match else None
    complete = complete and active_api == cvars["r_renderApi"]
    if args.render_api == "gles":
        complete = complete and re.search(r"Active renderer path:\s+glesd3\b", log, re.IGNORECASE) is not None
    report = {"exitCode": exit_code, "timeout": timed_out, "complete": complete,
              "map": map_name, "renderApi": args.render_api, "activeApi": active_api,
              "log": str(log_path), "screenshot": str(screenshot), "screenshotCheck": screenshot_check}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if complete and exit_code == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
