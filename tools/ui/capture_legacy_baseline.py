#!/usr/bin/env python3
"""Capture initial SP/MP GUI references through the engine screenshot command.

Uses the matching VS Code map launch profile with an isolated savepath, hidden
window and disabled mouse/controller input. Requires the guarded current UI
branch build; this is a reference capture, not a replacement acceptance test.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(args: argparse.Namespace) -> int:
    output = args.output.resolve()
    if output.exists():
        raise ValueError('use a new output directory to preserve previous capture evidence')
    runtime = args.runtime.resolve()
    executable = runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    if not executable.is_file():
        raise ValueError(f'missing staged client: {executable}')
    configs = json.loads((ROOT / '.vscode/launch.json').read_text(encoding='utf-8-sig'))['configurations']
    prefix = '(SP) airdefense1 ' if args.mode == 'sp' else '(MP) q4dm1 '
    profile = next(c for c in configs if c['name'].startswith(prefix) and c['name'].endswith('GL'))
    savepath = output / 'save'
    game = savepath / 'baseoq4'
    game.mkdir(parents=True)
    cfg_path = game / 'ui-baseline.cfg'
    cfg_path.write_text('gfxInfo\nscreenshot "screenshots/ui-baseline.tga"\necho UI_BASELINE_CAPTURE_COMPLETE\nquit\n', encoding='utf-8')
    overrides = {
        'fs_basepath': str(args.assets.resolve()), 'fs_savepath': str(savepath), 'fs_devpath': str(savepath),
        'fs_game': 'baseoq4', 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0',
        'r_borderlessDefaultMigrated': '1', 'r_hiddenWindow': '1',
        'r_windowWidth': str(args.width), 'r_windowHeight': str(args.height),
        'r_mode': '-1', 'r_customWidth': str(args.width), 'r_customHeight': str(args.height),
        'r_renderApi': args.renderer, 'r_rendererSharedGui': '0', 'r_rendererSharedInWorldGui': '0',
        'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
        'g_autoScreenshot': '0', 'g_autoSkipCinematics': '1',
        'g_autoExecAfterMapLoad': 'ui-baseline.cfg', 'g_autoExecAfterMapLoadDelayMs': '3000',
        'com_skipLoadingContinue': '1', 'com_loadingContinueAutoAdvance': '1',
        'com_maxfps': '60', 'ui_autoJoin': '1' if args.mode == 'mp' else '0',
    }
    override_keys = {key.lower() for key in overrides}
    retained = []
    original = profile['args']
    i = 0
    while i < len(original):
        if original[i].lower() in ('+set', '+seta') and i + 2 < len(original):
            if original[i + 1].lower() not in override_keys:
                retained.extend(original[i:i + 3])
            i += 3
        else:
            retained.append(original[i])
            i += 1
    command = [str(executable)]
    for key, value in overrides.items():
        command.extend(['+set', key, value])
    command.extend(value.replace('${workspaceFolder}', str(ROOT)) for value in retained)
    binaries = [executable] + sorted((runtime / 'baseoq4').glob('game-*'))
    binaries += sorted(runtime.glob('renderer-*'))
    metadata = {
        'status': 'running', 'profile': profile['name'], 'mode': args.mode, 'renderer': args.renderer,
        'capture_method': 'engine screenshot command after 3 seconds of active map drawing',
        'windowed': True, 'hidden_window': True, 'host_input_injection': False,
        'command': command, 'cwd': str(runtime), 'cfg_sha256': digest(cfg_path),
        'binaries': {str(p.relative_to(runtime)): digest(p) for p in binaries if p.is_file()},
    }
    report_path = output / 'capture.json'

    def save_report():
        report_path.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')

    save_report()
    started = time.monotonic()
    with (output / 'process.log').open('w', encoding='utf-8') as process_log:
        kwargs = {}
        if os.name == 'nt':
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            kwargs = {'startupinfo': startup, 'creationflags': subprocess.CREATE_NO_WINDOW}
        process = subprocess.Popen(command, cwd=runtime, stdout=process_log, stderr=subprocess.STDOUT, **kwargs)
        print(f'Baseline {args.mode}/{args.renderer}: PID {process.pid}; hidden, windowed, mouse disabled.', flush=True)
        try:
            metadata['returncode'] = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            metadata['status'] = 'timeout'
            metadata['seconds'] = round(time.monotonic() - started, 3)
            save_report()
            return 1
    metadata['seconds'] = round(time.monotonic() - started, 3)
    screenshot = game / 'screenshots/ui-baseline.tga'
    log_path = game / 'logs/openq4.log'
    log = log_path.read_text(encoding='utf-8', errors='replace') if log_path.exists() else ''
    valid = metadata['returncode'] == 0 and screenshot.is_file() and 'UI_BASELINE_CAPTURE_COMPLETE' in log
    if screenshot.is_file():
        image = screenshot.read_bytes()
        if len(image) >= 18:
            width, height = struct.unpack_from('<HH', image, 12)
            metadata['screenshot'] = {'path': str(screenshot.relative_to(output)), 'width': width,
                                      'height': height, 'sha256': digest(screenshot)}
            valid = valid and width == args.width and height == args.height
        else:
            valid = False
    metadata['status'] = 'captured_pending_visual_review' if valid else 'failed'
    metadata['log_sha256'] = digest(log_path) if log_path.exists() else None
    save_report()
    print(f'{metadata["status"]}: {report_path}', flush=True)
    return 0 if valid else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('sp', 'mp'), required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), required=True)
    parser.add_argument('--assets', type=Path, required=True, help='Installation root containing q4base.')
    parser.add_argument('--runtime', type=Path, default=ROOT / '.install')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    parser.add_argument('--timeout', type=int, default=180)
    args = parser.parse_args()
    if args.width < 1 or args.height < 1 or args.timeout < 1:
        parser.error('dimensions and timeout must be positive')
    try:
        return capture(args)
    except (OSError, ValueError, StopIteration) as error:
        print(f'Baseline capture failed: {error}', flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
