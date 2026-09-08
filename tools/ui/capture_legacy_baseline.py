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
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def interaction_script(path: Path) -> str:
    """Allow semantic runtime operations and bounded waits; no device commands."""
    source = path.read_text(encoding='utf-8')
    if len(source) > 32768:
        raise ValueError('retained interaction script exceeds 32 KiB')
    # The engine lexer splits punctuation in unquoted IDs (notably '-').
    identifier = r'"[A-Za-z0-9_.-]{1,128}"'
    patterns = [rf'ui_retained(?:Focus|State) {identifier}', rf'ui_retainedEnabled {identifier} [01]',
                rf'ui_retainedModal push {identifier}', r'ui_retainedModal pop', r'ui_retainedEvents',
                r'ui_retainedMenu (?:next|previous|up|down|left|right|accept|back) [01]', r'wait [1-9][0-9]{0,2}']
    lines = [line.strip() for line in source.splitlines() if line.strip() and not line.strip().startswith('//')]
    if len(lines) > 256 or any(not any(re.fullmatch(pattern, line) for pattern in patterns) for line in lines):
        raise ValueError('retained script must contain only semantic control commands and bounded waits')
    if sum(int(line.split()[1]) for line in lines if line.startswith('wait ')) > 3600:
        raise ValueError('retained script waits exceed 3600 frames')
    return '\n'.join(lines)+'\n'


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
    preview = ''
    if args.retained_document:
        fixture = args.retained_document.resolve()
        staged_name = 'retained-smoke' + fixture.suffix.lower()
        (game / staged_name).write_bytes(fixture.read_bytes())
        play = f'ui_retainedPlay "{args.timeline}"\n' if args.timeline else ''
        profile_command = f'ui_retainedProfile {args.profile_frames}\n' if args.profile_frames else ''
        settle = max(30, args.profile_frames + 2)
        script = 'wait 2\n'+interaction_script(args.retained_script) if args.retained_script else ''
        preview = f'ui_retainedPreview "{staged_name}"\n' + profile_command + play + script + f'wait {settle}\n'
        if args.video_restart:
            preview += 'vid_restart windowed\nwait 2\n' + profile_command + play + script + f'wait {settle}\n'
    cfg_path.write_text(preview + 'gfxInfo\nscreenshot "screenshots/ui-baseline.tga"\necho UI_BASELINE_CAPTURE_COMPLETE\nquit\n', encoding='utf-8')
    overrides = {
        'fs_basepath': str(args.assets.resolve()), 'fs_savepath': str(savepath), 'fs_devpath': str(savepath),
        'fs_game': 'baseoq4', 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0',
        'r_borderlessDefaultMigrated': '1', 'r_hiddenWindow': '1',
        'r_windowWidth': str(args.width), 'r_windowHeight': str(args.height),
        'r_mode': '-1', 'r_customWidth': str(args.width), 'r_customHeight': str(args.height),
        'r_renderApi': args.renderer, 'r_rendererSharedGui': '1' if args.shared_gui else '0', 'r_rendererSharedInWorldGui': '0',
        'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
        'g_autoScreenshot': '0', 'g_autoSkipCinematics': '1',
        'g_autoExecAfterMapLoad': 'ui-baseline.cfg', 'g_autoExecAfterMapLoadDelayMs': '3000',
        'com_skipLoadingContinue': '1', 'com_loadingContinueAutoAdvance': '1',
        'com_maxfps': '60', 'ui_autoJoin': '1' if args.mode == 'mp' else '0',
        'ui_retainedScale': str(args.ui_scale), 'ui_retainedDensity': str(args.density),
        'ui_retainedReducedMotion': '1' if args.reduced_motion else '0',
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
        'windowed': True, 'hidden_window': True, 'host_input_injection': False, 'shared_gui': args.shared_gui,
        'command': command, 'cwd': str(runtime), 'cfg_sha256': digest(cfg_path),
        'binaries': {str(p.relative_to(runtime)): digest(p) for p in binaries if p.is_file()},
    }
    report_path = output / 'capture.json'
    if args.retained_document:
        metadata['retained_preview'] = {'source': str(args.retained_document),
                                        'sha256': digest(args.retained_document),
                                        'density_override': args.density, 'ui_scale': args.ui_scale,
                                        'settle_frames': settle, 'video_restart': args.video_restart,
                                        'profile_frames': args.profile_frames,
                                        'timeline': args.timeline, 'reduced_motion': args.reduced_motion,
                                        'replacement_acceptance': False}
        if args.retained_script:
            metadata['retained_preview']['interaction_script'] = {'source': str(args.retained_script), 'sha256': digest(args.retained_script)}

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
    plain_log = re.sub(r'\^[0-9]', '', log)
    diagnostics = [line for line in plain_log.splitlines() if 'WARNING:' in line or 'ERROR:' in line]
    metadata['diagnostics'] = {'warnings': sum('WARNING:' in line for line in diagnostics),
                               'errors': sum('ERROR:' in line for line in diagnostics)}
    valid = metadata['returncode'] == 0 and screenshot.is_file() and 'UI_BASELINE_CAPTURE_COMPLETE' in log
    active_apis = re.findall(r'Renderer API: requested=\S+ active=(\S+) disposition=(\S+)', plain_log)
    metadata['active_renderer'] = active_apis[-1][0] if active_apis else None
    valid = valid and metadata['active_renderer'] == args.renderer
    if args.retained_document:
        retained_diagnostics = [line for line in diagnostics if 'retained UI:' in line or '_retained' in line]
        retained_diagnostics += [line for line in plain_log.splitlines() if line.startswith('usage: ui_retained')]
        metadata['retained_preview']['diagnostics'] = retained_diagnostics
        profiles = [json.loads(line.split('Retained UI profile: ', 1)[1]) for line in plain_log.splitlines() if 'Retained UI profile: ' in line]
        metadata['retained_preview']['profiles'] = profiles
        metadata['retained_preview']['interaction_trace'] = [line for line in plain_log.splitlines()
            if line.startswith(('Retained UI control:', 'Retained UI action:', 'Retained UI actions:'))]
        if args.profile_frames and (len(profiles) != (2 if args.video_restart else 1)
                                   or any(p.get('frames') != args.profile_frames for p in profiles)):
            retained_diagnostics.append('retained CPU profile did not complete for the requested frame count')
        valid = valid and 'Retained UI preview loaded:' in plain_log and not retained_diagnostics
        if args.timeline:
            played = plain_log.count(f'Retained UI timeline played: {args.timeline}')
            metadata['retained_preview']['timeline_play_count'] = played
            valid = valid and played == (2 if args.video_restart else 1)
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
    parser.add_argument('--shared-gui', action='store_true', help='exercise the shared GUI renderer domain')
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--retained-document', type=Path, help='Optional Q4UI or RML integration fixture, copied into the isolated savepath.')
    parser.add_argument('--retained-script', type=Path, help='Optional semantic control script; does not send device input.')
    parser.add_argument('--timeline', help='Canonical timeline to play before capture, and again after an optional video restart.')
    parser.add_argument('--reduced-motion', action='store_true')
    parser.add_argument('--density', type=float, default=0, help='Test density override; zero uses SDL display scale.')
    parser.add_argument('--ui-scale', type=float, default=1)
    parser.add_argument('--video-restart', action='store_true', help='Restart the windowed renderer with the preview loaded before capturing.')
    parser.add_argument('--profile-frames', type=int, default=0, help='Measure 1..3600 rendered UI frames before capture; zero disables profiling.')
    args = parser.parse_args()
    if args.width < 1 or args.height < 1 or args.timeout < 1:
        parser.error('dimensions and timeout must be positive')
    if not math.isfinite(args.density) or not 0 <= args.density <= 8:
        parser.error('density must be zero (automatic) or a positive value up to 8')
    if not math.isfinite(args.ui_scale) or not .75 <= args.ui_scale <= 2:
        parser.error('UI scale must be between 0.75 and 2')
    if args.video_restart and not args.retained_document:
        parser.error('--video-restart requires --retained-document')
    if args.retained_script and (not args.retained_document or args.retained_document.suffix.lower() != '.q4ui'):
        parser.error('--retained-script requires a .q4ui document')
    if not 0 <= args.profile_frames <= 3600 or (args.profile_frames and not args.retained_document):
        parser.error('--profile-frames requires a retained document and a count from 1 to 3600')
    if args.retained_document and args.retained_document.suffix.lower() not in ('.rml', '.q4ui'):
        parser.error('retained document must be .rml or .q4ui')
    if args.timeline and (not args.retained_document or args.retained_document.suffix.lower() != '.q4ui'
                          or not re.fullmatch(r'[A-Za-z0-9_.-]{1,128}', args.timeline)):
        parser.error('--timeline requires a .q4ui document and a valid stable timeline ID')
    try:
        return capture(args)
    except (OSError, ValueError, StopIteration) as error:
        print(f'Baseline capture failed: {error}', flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
