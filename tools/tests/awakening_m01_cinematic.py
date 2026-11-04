#!/usr/bin/env python3
"""Bounded, input-free m01 playback using the engine's screenshot command.

Requires a matching staged runtime and user-supplied retail/Awakening content.
Never changes installed content, captures the desktop or sends input events.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import re
import subprocess
import time
from savegame_cross_load import stage_input


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--awakening', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache', type=Path, help='Reuse a task-owned generated asset cache')
    parser.add_argument('--duration', type=int, default=330)
    parser.add_argument('--capture-start', type=int, default=140)
    parser.add_argument('--interval', type=int, default=2)
    parser.add_argument('--overlay', type=Path, help='Candidate pak0 source tree, copied into this test profile')
    parser.add_argument('--audio', action='store_true', help='Record via the OpenAL Soft wave writer; no speaker output')
    parser.add_argument('--hrtf', action='store_true', help='Force the engine HRTF output mode')
    parser.add_argument('--reduced-flashing', action='store_true')
    parser.add_argument('--skip', action='store_true')
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--focus', action='store_true', help='Test-only map script enters the cabin directly')
    parser.add_argument('--save-roundtrip', action='store_true')
    parser.add_argument('--api', choices=['gl', 'vk'], default='gl')
    parser.add_argument('--shake', type=float, default=1)
    parser.add_argument('--fps', type=int, default=60)
    parser.add_argument('--timescale', type=float, default=1,
                        help='Accelerated lifecycle checks only; use 1 for audiovisual qualification')
    parser.add_argument('--dispatch-ms', type=int, default=1000,
                        help='Maximum dispatch latency for phase actions')
    parser.add_argument('--no-render', action='store_true', help='Lifecycle checks only; skip 3D drawing')
    parser.add_argument('--finish-after-handoff', action='store_true',
                        help='Finish after six seconds of gameplay instead of the duration cap')
    parser.add_argument('--trace', action='store_true', help='Trace sound lifecycle and camera envelopes')
    parser.add_argument('--checkpoint-beat', help='Save/reload when this logged scene beat occurs')
    parser.add_argument('--checkpoint-all', action='store_true',
                        help='Save/reload each supported phase; retain one fresh-load fixture per phase')
    parser.add_argument('--skip-beat', help='Use the native cinematic skip at this scene beat')
    parser.add_argument('--pause-beat', help='Freeze/resume simulation at this scene beat')
    parser.add_argument('--burst-beat', action='append', default=[],
                        help='Record 20 engine screenshots at 50 ms intervals after this beat')
    parser.add_argument('--load-save', type=Path, help='Restore a saved scene in a fresh process')
    parser.add_argument('--set', nargs=2, action='append', default=[], metavar=('CVAR', 'VALUE'), help='Additional isolated-profile diagnostic setting')
    parser.add_argument('--after-load', action='append', default=[], help='Additional engine diagnostic command after loading')
    args = parser.parse_args()
    if args.duration <= 0 or args.interval <= 0 or args.dispatch_ms <= 0:
        parser.error('Duration and interval must be positive.')
    if not 0 <= args.shake <= 1 or args.fps <= 0 or not 0.1 <= args.timescale <= 10:
        parser.error('Shake must be 0..1, timescale 0.1..10 and FPS positive.')
    protected = {'r_fullscreen', 'r_fullscreenDesktop', 'r_hiddenWindow',
                 'in_mouse', 'in_joystick', 'in_joystickRumble',
                 'fs_savepath', 'fs_devpath', 'fs_basepath', 'fs_awakeningpath'}
    if any(key.lower() in {name.lower() for name in protected} for key, _ in args.set):
        parser.error('Diagnostic settings cannot override input, window or filesystem isolation.')
    runtime, output = args.runtime.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    game = output / 'profile' / 'q4xbase'
    game.mkdir(parents=True, exist_ok=True)
    overlay_identity = {}
    if args.overlay:
        for source in sorted(args.overlay.resolve().rglob('*')):
            if source.is_file():
                relative = source.relative_to(args.overlay.resolve())
                destination = game / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
                overlay_identity[str(relative)] = hashlib.sha256(source.read_bytes()).hexdigest()
    sidecar = game / 'maps/game/m01_stranarus_trench1.script'
    if not args.focus and sidecar.exists():
        parser.error('Use a fresh output directory for a full run; this profile contains the focus sidecar.')
    if args.focus:
        # A test-only sidecar is compiled by worldspawn. Production runs exercise
        # the original hangar/flyout and do not install this sidecar.
        setup = ['void main() {', 'sys.wait(0.5);',
                 'sys.killthread("m04_stranarus_trench::intro_unit1_seq_*");',
                 'sys.killthread("intro_unit1_seq_*");',
                 '$intro_unit1_seq_1_trigger_seq_2.remove();']
        for name in ['tak', 'navajas', 'mitchell', 'darnell', 'townsend', 'atkins', 'dekard']:
            actor = '$intro_unit1_seq_6_' + name
            setup += [f'sys.trigger({actor});',
                      f'{actor}.hideSurface("models/weapons/mp_guns/w_mg_lo");',
                      f'aiScriptedAnimLoop({actor}, "st_dropship_chair", 8);']
        for name in ['markswell', 'markswell_sitting']:
            setup += [f'$intro_unit1_seq_6_{name}.hideSurface("models/weapons/mp_guns/w_mg_lo");']
        setup += ['sys.trigger($intro_unit1_seq_5_trigger_seq_6);', '}']
        sidecar.parent.mkdir(parents=True, exist_ok=True)
        sidecar.write_text('\n'.join(setup) + '\n', encoding='ascii')
    exe = runtime / 'openQ4-client_x64.exe'
    settings = {
        'win_allowMultipleInstances': '1', 'r_hiddenWindow': '1',
        'r_fullscreen': '0', 'r_fullscreenDesktop': '0',
        'r_borderless': '0', 'r_borderlessDefaultMigrated': '1',
        'r_mode': '-1', 'r_customWidth': '960', 'r_customHeight': '540',
        'r_windowWidth': '960', 'r_windowHeight': '540', 'r_renderApi': args.api,
        'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
        's_volume': '1' if args.audio else '0', 's_noSound': '0',
        's_openALHRTF': '2' if args.hrtf else '1',
        's_muteUnfocused': '0', 'ui_reducedFlashing': str(int(args.reduced_flashing)),
        'ui_cinematicShake': str(args.shake), 'ui_retained': '1', 'ui_autoJoin': '0',
        'g_debugM01Feedback': str(int(args.trace)), 's_showStartSound': str(int(args.trace)),
        'bse_speeds': str(int(args.trace)),
        'logFile': '2', 'logFileName': 'logs/m01.log',
        'fs_basepath': str(args.retail.resolve()),
        'fs_awakeningpath': str(args.awakening.resolve()),
        'fs_savepath': str(game.parent), 'fs_devpath': str(game.parent),
        'fs_cachepath': str(args.cache.resolve() if args.cache else output / 'cache'),
        'fs_game': 'q4xbase', 'si_gameType': 'singleplayer',
        'com_skipLoadingContinue': '1', 'com_loadingContinueAutoAdvance': '1',
        'com_maxfps': str(args.fps), 'g_autoSkipCinematics': str(int(args.skip)),
        'timescale': str(args.timescale),
        'r_skipRender': str(int(args.no_render)),
        'g_awakeningM01Cinematic': str(int(not args.baseline)),
        'developer': '1', 'g_autoExecAfterMapLoad': 'm01-check.cfg',
        'g_autoExecAfterMapLoadDelayMs': '500',
    }
    settings.update(dict(args.set))
    dispatch = game / 'm01-dispatch.cfg'
    dispatch.write_text('// idle\n')
    commands = ['echo M01_CHECK_BEGIN', 'set g_autoExecAfterMapLoad ""', *args.after_load]
    for second in range(0, args.duration, args.interval):
        commands += ['exec "m01-dispatch.cfg"', 'testM01Feedback']
        if second >= args.capture_start:
            if args.trace:
                commands += ['s_soundDistances oq4_m01v3_weld', 'testMission oq4_m01_welder',
                             'testMission oq4_m01_damage']
            if args.audio:
                commands += ['listPlayingSounds']
            commands += [f'echo M01_CAPTURE_{second:03d}',
                         f'screenshot "screenshots/m01-{second:03d}.tga"']
        remaining = args.interval * 1000
        while remaining > 0:
            step = min(remaining, args.dispatch_ms)
            commands += [f'waitMsec {step}']
            remaining -= step
            if remaining:
                commands += ['exec "m01-dispatch.cfg"']
    commands += ['testM01Feedback', 'listPlayingSounds', 'listThreads', 'echo M01_CHECK_PLAYBACK_END']
    if args.save_roundtrip:
        commands += ['set g_autoExecAfterMapLoad ""', 'saveGame m01_direction',
                     'waitMsec 500', 'loadGame m01_direction', 'waitMsec 2000',
                     'screenshot "screenshots/m01-restored.tga"', 'listThreads',
                     'echo M01_CHECK_RESTORED']
    commands += ['echo M01_CHECK_COMPLETE', 'quit']
    # The engine command buffer is 64 KiB. Chain small files so fine dispatch
    # polling and screenshot bursts cannot truncate the end of a long run.
    chunks = [commands[i:i+200] for i in range(0, len(commands), 200)]
    for index, chunk in enumerate(chunks):
        if index + 1 < len(chunks):
            chunk += [f'exec "m01-step-{index+1:03d}.cfg"']
        filename = 'm01-check.cfg' if index == 0 else f'm01-step-{index:03d}.cfg'
        (game / filename).write_text('\n'.join(chunk) + '\n', encoding='ascii')
    command = [str(exe)]
    for key, value in settings.items():
        command += ['+set', key, value]
    if args.load_save:
        stage_input(args.load_save.resolve(), game/'savegames', 'm01_in')
        command += ['+loadGame', 'm01_in']
    else:
        command += ['+map', 'game/m01_stranarus_trench1']
    identity = {}
    for path in [exe, runtime/'baseoq4/game-sp_x64.dll', runtime/'baseoq4/pak0.pk4']:
        identity[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    environment = os.environ.copy()
    audio_path = output / 'm01-mix.wav'
    if args.audio:
        if audio_path.exists():
            parser.error('Use a fresh output directory for audio; never overwrite a recording.')
        # OpenAL Soft's documented wave backend renders directly to a file.
        # https://github.com/kcat/openal-soft/blob/master/alsoftrc.sample
        config = output / 'alsoft.ini'
        config.write_text('[general]\nchannels=stereo\nsample-type=int16\nfrequency=48000\n'
                          + '[wave]\nfile=' + str(audio_path) + '\n', encoding='utf-8')
        environment.update(ALSOFT_DRIVERS='wave', ALSOFT_CONF=str(config),
                           ALSOFT_LOGFILE=str(output / 'openal.log'), ALSOFT_LOGLEVEL='3')
    beat_observations = {}
    checkpoint_phases = ['cabin', 'incoming', 'breach', 'rescue_warning', 'evacuate',
                         'weld_on', 'engine_failure', 'descent'] if args.checkpoint_all else []
    actions = {}
    pending = None
    def issue(kind, body):
        nonlocal pending
        pending = kind
        text = '\n'.join([f'echo M01_DISPATCH_BEGIN_{kind}', *body,
                          f'echo M01_DISPATCH_END_{kind}']) + '\n'
        temporary = dispatch.with_suffix('.tmp')
        temporary.write_text(text, encoding='ascii')
        temporary.replace(dispatch)
        actions[kind] = {'issued': True, 'completed': False}
    started = time.monotonic()
    # A rerun must not mistake the previous process's fatal error for a new one.
    previous_log = game / 'logs/m01.log'
    if previous_log.exists():
        previous_log.unlink()
    with (output / 'console.log').open('w', encoding='utf-8') as console:
        process = subprocess.Popen(command, cwd=runtime, stdin=subprocess.DEVNULL,
                                   stdout=console, stderr=subprocess.STDOUT,
                                   startupinfo=startup, env=environment, creationflags=subprocess.CREATE_NO_WINDOW)
        print(f'Hidden playback PID {process.pid}; output {output}', flush=True)
        (output/'launch.json').write_text(json.dumps({'pid': process.pid, 'command': command,
                                                    'identity': identity, 'overlay_identity': overlay_identity}, indent=2))
        while process.poll() is None:
            log_path = game / 'logs/m01.log'
            current_log = log_path.read_text(errors='replace') if log_path.exists() else ''
            for beat, game_time in re.findall(r'oq4_m01: (\w+) @ ([\d.]+)', current_log):
                if beat not in beat_observations:
                    beat_observations[beat] = {'game_time': float(game_time),
                        'elapsed_seconds': round(time.monotonic()-started, 2),
                        'audio_bytes': audio_path.stat().st_size if args.audio and audio_path.exists() else None}
            if pending and not actions[pending].get('dispatched') and f'M01_DISPATCH_BEGIN_{pending}' in current_log:
                # Clear promptly after dispatch so a restore cannot issue twice.
                temporary = dispatch.with_suffix('.tmp')
                temporary.write_text('// idle\n')
                try:
                    temporary.replace(dispatch)
                    actions[pending]['dispatched'] = True
                except PermissionError:
                    pass  # Retry after an engine read releases the file.
            if pending and f'M01_DISPATCH_END_{pending}' in current_log:
                actions[pending]['completed'] = True
                pending = None
            if not pending:
                checkpoint = next((b for b in checkpoint_phases if b in beat_observations
                                   and 'checkpoint_'+b not in actions), None)
                if checkpoint:
                    issue('checkpoint_'+checkpoint, ['testM01Feedback', f'testAutoSave m01_{checkpoint}',
                          'waitMsec 300', f'loadGame m01_{checkpoint}', 'waitMsec 300', 'testM01Feedback'])
                elif args.checkpoint_beat in beat_observations and 'checkpoint' not in actions:
                    issue('checkpoint', ['testM01Feedback', 'testAutoSave m01_phase', 'waitMsec 300',
                          'loadGame m01_phase', 'waitMsec 300', 'testM01Feedback'])
                elif args.skip_beat in beat_observations and 'skip' not in actions:
                    issue('skip', ['testM01Feedback', 'testM01Feedback skip'])
                elif args.pause_beat in beat_observations and 'pause' not in actions:
                    issue('pause', ['set g_stopTime 1', 'testM01Feedback', 'waitMsec 1200',
                          'testM01Feedback', 'set g_stopTime 0'])
                else:
                    for beat in args.burst_beat:
                        kind = 'burst_' + beat
                        if beat in beat_observations and kind not in actions:
                            body = ['testM01Feedback']
                            for frame in range(20):
                                body += [f'screenshot "screenshots/{kind}-{frame:02d}.tga"', 'waitMsec 50']
                            issue(kind, body)
                            break
            if not pending and args.finish_after_handoff and 'finish' not in actions and \
                'handoff_complete' in beat_observations and \
                time.monotonic()-started > beat_observations['handoff_complete']['elapsed_seconds']+6:
                issue('finish', ['testM01Feedback', 'listPlayingSounds', 'listThreads',
                    'echo M01_CHECK_PLAYBACK_END', 'echo M01_CHECK_COMPLETE', 'quit'])
            if 'ERROR:' in current_log or 'FATAL:' in current_log or time.monotonic() - started > args.duration * 2 + 180:
                process.kill()  # only the process this harness created
                break
            time.sleep(0.1)
        code = process.wait()
    log_path = game / 'logs/m01.log'
    log = log_path.read_text(errors='replace') if log_path.exists() else ''
    beats = re.findall(r'oq4_m01: (\w+) @ ([\d.]+)', log)
    failures = re.findall(r'^.*(?:ERROR:|FATAL:|Compile failed|script runtime error).*$',
                          log, re.M | re.I)
    complete = 'M01_CHECK_COMPLETE' in log
    if complete and 'finish' in actions:
        actions['finish']['completed'] = True
    handoff = args.baseline or any(beat == 'handoff_complete' for beat, _ in beats)
    saved = not args.save_roundtrip or (
        (game/'savegames/m01_direction.save').exists()
        and 'loadingSaveGame=1' in log and 'M01_CHECK_RESTORED' in log)
    audio_ok = not args.audio or (audio_path.exists() and audio_path.stat().st_size > 48000
        and 'OpenAL Soft on Wave File Writer' in (output/'openal.log').read_text(errors='replace'))
    actions_ok = all(actions.get(key, {}).get('completed', False) for key, beat in
        [('checkpoint', args.checkpoint_beat), ('skip', args.skip_beat), ('pause', args.pause_beat)] if beat)
    actions_ok = actions_ok and all(actions.get('burst_'+beat, {}).get('completed', False)
                                    for beat in args.burst_beat)
    if args.checkpoint_all:
        actions_ok = actions_ok and all(actions.get('checkpoint_'+beat, {}).get('completed', False)
            and (game/f'savegames/m01_{beat}.save').is_file()
            and f'TEST_AUTOSAVE slot=m01_{beat} saved=1' in log for beat in checkpoint_phases)
    if args.checkpoint_beat:
        actions_ok = actions_ok and (game/'savegames/m01_phase.save').is_file() and \
            'TEST_AUTOSAVE slot=m01_phase saved=1' in log and 'loadingSaveGame=1' in log
    states = re.findall(r'M01_STATE .*', log)
    cleanup = bool(states and re.search(
        r'cinematic=0 helper=0 .*weld=0 glow=0 fill=0 damage=0 exitLight=0 breachLight=0', states[-1]))
    result = {'exit_code': code, 'elapsed_seconds': round(time.monotonic()-started, 2),
              'api': args.api, 'shake': args.shake, 'fps': args.fps,
              'timescale': args.timescale, 'rendered': not args.no_render, 'actions': actions,
              'actions_ok': actions_ok, 'cleanup': cleanup,
              'focus': args.focus, 'skip': args.skip, 'baseline': args.baseline,
              'complete': complete, 'handoff': handoff, 'saved': saved,
              'audio': str(audio_path) if args.audio else None, 'audio_ok': audio_ok, 'overlay_identity': overlay_identity,
              'hrtf': args.hrtf,
              'beats': beats, 'beat_observations': beat_observations, 'errors': failures, 'identity': identity}
    (output/'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2), flush=True)
    return 0 if code == 0 and complete and handoff and saved and audio_ok and actions_ok and cleanup and not failures else 1


if __name__ == '__main__':
    raise SystemExit(main())
