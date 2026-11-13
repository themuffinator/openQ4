#!/usr/bin/env python3
"""Qualify AUD-04 placement and layout transitions on real playback endpoints.

The other AUD-04 harnesses measure OpenAL Soft's wave writer or an OpenAL
loopback context. This one plays through the real system backend (WASAPI on
Windows, PulseAudio/PipeWire on Linux) onto a named output device and records
that device's own loopback, after the operating system's mixer, in the
endpoint's layout. No microphone, host input or wave writer is used.

In stock gameplay (game/airdefense1) it plays a 440 Hz radio dialogue fixture
(the mono global path that is centre-routed on a centre-capable output) and an
880 Hz speaker entity placed 64 units to the player's right, then to the left.
It then forces the other speaker layout and returns to the endpoint's own
(s_numberOfSpeakers 6 on an output without a centre speaker, 2 on one with),
and, given a second endpoint, moves the output to it with s_deviceName during
the line and back again. Each phase is cut from the recordings by the moment
its probe appeared in the engine log.

On an output with a centre speaker it checks exclusive centre dialogue, a silent
LFE and lateral positional sound; on one without, balanced dialogue and lateral
positional sound; in every case that a forced layout keeps the dialogue audible
(through the system downmix when OpenAL renders 5.1 onto a stereo device), that
the device is reset or reopened in place without a sound restart, that the line
follows its scene clock, and that a switch silences the old device and plays on
the new one in the new device's layout.

The tones play audibly on the endpoints you name. Choose virtual or unused
outputs (for example Steam Streaming Speakers) unless you mean to listen.

    python tools/tests/endpoint_loopback.py --list
    python tools/tests/audio_surround_endpoint.py --assets "<Quake 4 install>" \\
        --endpoint "Steam Streaming Speakers" --second-endpoint "Steam Streaming Microphone" \\
        --output .tmp/audio/surround-endpoint
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

import numpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import endpoint_loopback as loopback  # noqa: E402
from audio_dialogue import tone  # noqa: E402
from mp_round_remote_smoke import verify_runtime_is_current  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
EXE_NAME = 'openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64'
RUNTIME = ROOT / '.install'  # --runtime-dir replaces it
VOICE = re.compile(r'^SOUND_VOICE source=(\d+) state=(\w+) routed=(\d) queued=(\d) serial=(\d+) offsetMs=([\d.-]+) sample=(\S+)', re.M)
OUTPUT = re.compile(r'^SOUND_OUTPUT serial=(\d+) connected=(\d) centre=(\d) interrupted=(\d)', re.M)
WORLD = re.compile(r'^SOUND_WORLD id=1 paused=(\d) time=(-?\d+)', re.M)
POSE = re.compile(r'^\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) (-?[\d.]+)\s*$', re.M)
MODE = re.compile(r'^OpenAL output mode active: (.+?)\s*$', re.M)
MODES = {1: {'mono'}, 2: {'stereo', 'basic stereo', 'UHJ stereo', 'HRTF stereo'}, 4: {'quad'},
         6: {'5.1 surround'}, 7: {'6.1 surround'}, 8: {'7.1 surround'}}
LEFT, RIGHT = {'FL', 'BL', 'SL', 'FLC'}, {'FR', 'BR', 'SR', 'FRC'}
# Probe after this long in each phase; analyse the segment that ends just before the probe.
# Device checks run once a second, so a reset lands at most a second into the phase.
PHASE_MSEC, SEGMENT = 3500, (2.0, 0.25)


def openal_devices():
    """The playback device names the engine's own OpenAL reports (ALC_ALL_DEVICES_SPECIFIER)."""
    candidates = [RUNTIME / 'OpenAL32.dll'] if os.name == 'nt' else \
        [RUNTIME / 'libopenal.so.1', RUNTIME / 'libopenal.so']
    path = next((str(p) for p in candidates if p.is_file()), None) or ctypes.util.find_library('openal')
    if not path:
        raise SystemExit('the OpenAL runtime the engine uses was not found')
    al = ctypes.CDLL(path)
    al.alcGetString.restype, al.alcGetString.argtypes = ctypes.c_void_p, [ctypes.c_void_p, ctypes.c_int]
    pointer, names = al.alcGetString(None, 0x1013), []
    while pointer:
        name = ctypes.string_at(pointer)
        if not name:
            break
        names.append(name.decode('utf-8', 'replace'))
        pointer += len(name) + 1
    return names


def openal_name(endpoint, names):
    match = [n for n in names if n == endpoint['name'] or n.endswith(' on ' + endpoint['name'])]
    if len(match) != 1:
        raise SystemExit(f'no single OpenAL device for "{endpoint["name"]}" among {names}')
    return match[0]


def probe(name):
    return [f'echo AUDIO_ENDPOINT_{name}', 's_soundWorlds', 'listSoundVoices', 'listPlayingSounds']


def sections(log):
    return {name: body for name, body in re.findall(r'^AUDIO_ENDPOINT_(\w+)\s*\n(.*?)(?=^AUDIO_ENDPOINT_|\Z)', log, re.M | re.S)}


def voice(section, stem):
    for m in VOICE.findall(section):
        if Path(m[6]).stem == stem:
            return {'state': m[1], 'routed': int(m[2]), 'serial': int(m[4]), 'offset': float(m[5])}
    return None


def world_time(section):
    m = WORLD.search(section)
    return int(m[2]) if m else None


def output_state(section):
    m = OUTPUT.search(section)
    return {'serial': int(m[1]), 'connected': int(m[2]), 'centre': int(m[3])} if m else {}


def side(levels, names, group):
    return sum(level for level, name in zip(levels, names) if name in group)


def placement(levels, names):
    """Where a tone sits: centre share, LFE share, and the right/left balance."""
    level = dict(zip(names, levels))
    loudest = max(levels) if levels else 0.0
    others = [v for n, v in level.items() if n not in ('FC', 'LFE')]
    return {'peak': loudest, 'centre_over_others': level.get('FC', 0.0) / max(max(others, default=0.0), 1e-9),
            'lfe_share': level.get('LFE', 0.0) / max(loudest, 1e-9),
            'right_over_left': side(levels, names, RIGHT) / max(side(levels, names, LEFT), 1e-9)}


def launch(args, root, primary, steps_first, write_second, cvars_extra):
    game = root / 'save' / 'baseoq4'
    game.mkdir(parents=True, exist_ok=True)
    tone(game / 'sound/vo_english/audio_test/dialogue.wav', 440)
    tone(game / 'sound/audio_test/effect.wav', 880)
    (game / 'audio_endpoint.cfg').write_text('\n'.join(steps_first) + '\n', encoding='utf-8')
    second = game / 'audio_endpoint_2.cfg'
    second.unlink(missing_ok=True)
    driver = 'wasapi' if os.name == 'nt' else 'pulse'
    config = root / 'alsoft.conf'
    config.write_text(f'[general]\ndrivers={driver}\n', encoding='utf-8')
    cvars = {
        'fs_basepath': str(args.assets.resolve()), 'fs_savepath': str(root / 'save'), 'fs_devpath': str(root / 'save'),
        'fs_game': 'baseoq4', 'com_gameMode': 'SP', 'si_gameType': 'singleplayer', 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0', 'r_borderlessDefaultMigrated': '1',
        'win_allowMultipleInstances': '1', 'r_hiddenWindow': '1', 'r_windowWidth': '1280', 'r_windowHeight': '720',
        'r_mode': '-1', 'r_customWidth': '1280', 'r_customHeight': '720', 'r_renderApi': args.renderer, 'r_multiSamples': '0',
        'r_swapInterval': '1', 'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
        's_noSound': '0', 's_muteUnfocused': '0', 'com_skipLogoVideos': '1', 'g_autoSkipCinematics': '1',
        's_numberOfSpeakers': '0', 's_openALHRTF': '0', 's_deviceName': primary, 's_debugHardware': '1',
        'com_skipLoadingContinue': '1', 'com_maxfps': '60', 'ui_autoJoin': '0', 'ui_retained': '1',
        'sys_lang': 'english', 'g_autoExecAfterMapLoad': 'audio_endpoint.cfg', 'g_autoExecAfterMapLoadDelayMs': '3000',
        **cvars_extra,
    }
    command = [str(RUNTIME / EXE_NAME)]
    for key, value in cvars.items():
        command += ['+set', key, value]
    command += ['+map', args.map]
    env = {k: v for k, v in os.environ.items() if not k.startswith('ALSOFT_')}
    options = {'env': {**env, 'ALSOFT_CONF': str(config), 'ALSOFT_DRIVERS': driver}}
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    log_path = game / 'logs/openq4.log'
    seen, written = {}, False
    deadline = time.monotonic() + args.timeout
    with (root / 'process.log').open('w') as out:
        process = subprocess.Popen(command, cwd=RUNTIME, stdout=out, stderr=subprocess.STDOUT, **options)
        while True:
            finished = process.poll() is not None
            text = log_path.read_text(encoding='utf-8', errors='replace') if log_path.is_file() else ''
            now = time.perf_counter()
            for name in re.findall(r'^AUDIO_ENDPOINT_(\w+)\s*$', text, re.M):
                seen.setdefault(name, now)
            if not written and 'POSE' in seen:
                head = text[:text.index('AUDIO_ENDPOINT_POSE')]
                poses = POSE.findall(head)
                if poses:
                    second.write_text('\n'.join(write_second(*map(float, poses[-1]))) + '\n', encoding='utf-8')
                    written = True
            if finished:
                break
            if time.monotonic() > deadline:
                process.kill()
                process.wait()
                break
            time.sleep(0.04)
    return process.returncode, log_path.read_text(encoding='utf-8', errors='replace') if log_path.is_file() else '', seen, command


def run(args, root, primary, second):
    has_centre = 'FC' in primary['layout']
    forced = 2 if has_centre else 6
    a_name = primary['openal']
    b_name = second['openal'] if second else None

    def phase_two(x, y, z, yaw):
        r = math.radians(yaw)
        right = (math.sin(r), -math.cos(r))  # idAngles::ToVectors right for pitch = roll = 0
        spot = lambda sign: f'"{x + sign * 64 * right[0]:.2f} {y + sign * 64 * right[1]:.2f} {z:.2f}"'
        speaker = lambda name, sign: (f'spawn speaker name {name} s_shader sound/audio_test/effect.wav origin {spot(sign)} '
                                      's_mindistance 160 s_maxdistance 2000')
        steps = ['testRadioChatter fixture sound/vo/audio_test/dialogue.wav',
                 speaker('audio_endpoint_right', 1), f'waitMsec {PHASE_MSEC}'] + probe('RIGHT')
        steps += ['remove audio_endpoint_right', speaker('audio_endpoint_left', -1), f'waitMsec {PHASE_MSEC}'] + probe('LEFT')
        steps += ['remove audio_endpoint_left', f'set s_numberOfSpeakers {forced}', f'waitMsec {PHASE_MSEC}'] + probe('FORCED')
        steps += ['set s_numberOfSpeakers 0', f'waitMsec {PHASE_MSEC}'] + probe('AUTO')
        if b_name:
            steps += [f'set s_deviceName "{b_name}"', f'waitMsec {PHASE_MSEC}'] + probe('SWITCHED')
            steps += [f'set s_deviceName "{a_name}"', f'waitMsec {PHASE_MSEC}'] + probe('RETURNED')
        return steps + ['echo AUDIO_ENDPOINT_COMPLETE', 'quit']

    first = ['notarget', 'set s_volume 1', 'set s_musicVolume 0', 'set s_dialogueVolume 1',
             'getviewpos', 'echo AUDIO_ENDPOINT_POSE', 'waitMsec 2500', 'exec audio_endpoint_2.cfg']
    recorders = {'primary': loopback.Recorder(primary).start()}
    if second:
        recorders['second'] = loopback.Recorder(second).start()
    try:
        code, log, seen, command = launch(args, root, a_name, first, phase_two, {})
    finally:
        recordings = {key: recorder.stop() for key, recorder in recorders.items()}
    for key, recording in recordings.items():
        recording.write_wav(root / f'{key}.wav')

    s = sections(log)
    loaded = log.find('AUDIO_ENDPOINT_POSE')
    after = log[loaded:] if loaded >= 0 else ''
    phases = [p for p in ('RIGHT', 'LEFT', 'FORCED', 'AUTO', 'SWITCHED', 'RETURNED') if p in seen]
    measured = {}
    for key, recording in recordings.items():
        measured[key] = {}
        for phase in phases:
            start, end = seen[phase] - SEGMENT[0], seen[phase] - SEGMENT[1]
            measured[key][phase] = {str(f): recording.levels(f, start, end) for f in (440, 880)}
    names = {'primary': recordings['primary'].names, **({'second': recordings['second'].names} if second else {})}
    dialogue = lambda key, phase: placement(measured[key].get(phase, {}).get('440', []), names[key])
    effect = lambda key, phase: placement(measured[key].get(phase, {}).get('880', []), names[key])

    def dialogue_placed(key, phase, centre):
        p = dialogue(key, phase)
        if centre:
            return p['peak'] > 0.003 and p['centre_over_others'] >= 1.5 and p['lfe_share'] < 0.05
        return p['peak'] > 0.003 and 0.8 <= p['right_over_left'] <= 1.25

    modes = MODE.findall(after)
    reports = [(m.start(), m[1]) for m in MODE.finditer(log)]
    mode_before = lambda marker: next((name for pos, name in reversed(reports) if pos < log.find(f'AUDIO_ENDPOINT_{marker}')), None) \
        if f'AUDIO_ENDPOINT_{marker}' in log else None
    checks = {'complete': 'AUDIO_ENDPOINT_COMPLETE' in log and code == 0}
    checks['real_backend_on_named_endpoint'] = f'OpenAL active device: {a_name}' in log and \
        len(recordings['primary'].frames) > recordings['primary'].rate * 5
    checks['automatic_layout_matches_endpoint'] = mode_before('RIGHT') in MODES.get(primary['channels'], set())
    right_voice = voice(s.get('RIGHT', ''), 'dialogue')
    checks['dialogue_placement'] = bool(right_voice) and right_voice['routed'] == int(has_centre) and \
        output_state(s.get('RIGHT', '')).get('centre') == int(has_centre) and dialogue_placed('primary', 'RIGHT', has_centre)
    checks['positional_right'] = effect('primary', 'RIGHT')['peak'] > 0.003 and effect('primary', 'RIGHT')['right_over_left'] > 1.5
    checks['positional_left'] = effect('primary', 'LEFT')['peak'] > 0.003 and effect('primary', 'LEFT')['right_over_left'] < 1 / 1.5
    if has_centre:
        checks['positional_not_centred'] = all(effect('primary', p)['centre_over_others'] < 1.0 for p in ('RIGHT', 'LEFT'))
    forced_mode = mode_before('FORCED')
    forced_voice = voice(s.get('FORCED', ''), 'dialogue')
    forced_centre = forced_mode in MODES[6] | MODES[7] | MODES[8]
    reference = dialogue('primary', 'RIGHT')['peak']
    checks['forced_layout_reset_in_place'] = after.count('speaker mode changed; device reset in place') == 2 and \
        'Sound System Restart' not in after and 'sound samples reloaded' not in after
    checks['forced_layout_keeps_dialogue'] = bool(forced_voice) and forced_voice['state'] == 'playing' and \
        forced_voice['routed'] == int(forced_centre) and dialogue('primary', 'FORCED')['peak'] > 0.4 * reference
    if not has_centre:
        # Real stereo devices may refuse 5.1 (OpenAL then renders stereo) or take it and downmix: either way the line stays centred.
        checks['forced_layout_dialogue_balanced'] = 0.8 <= dialogue('primary', 'FORCED')['right_over_left'] <= 1.25
    auto_voice = voice(s.get('AUTO', ''), 'dialogue')
    checks['automatic_layout_restored'] = mode_before('AUTO') in MODES.get(primary['channels'], set()) and \
        bool(auto_voice) and auto_voice['routed'] == int(has_centre) and dialogue_placed('primary', 'AUTO', has_centre)
    clock = lambda a, b: (voice(s.get(b, ''), 'dialogue') or {}).get('offset', 0) - (voice(s.get(a, ''), 'dialogue') or {}).get('offset', 0) - \
        ((world_time(s.get(b, '')) or 0) - (world_time(s.get(a, '')) or 0))
    checks['scene_clock_across_resets'] = all(voice(s.get(p, ''), 'dialogue') for p in ('RIGHT', 'AUTO')) and abs(clock('RIGHT', 'AUTO')) < 350
    details = {}
    if second:
        b_centre = 'FC' in second['layout']
        checks['switch_reopens_in_place'] = f'requested device changed; attempting in-place device reopen to {b_name}' in after and \
            f'OpenAL active device: {b_name}' in after and 'Sound System Restart' not in after
        checks['switch_layout_matches_new_endpoint'] = mode_before('SWITCHED') in MODES.get(second['channels'], set())
        switched_voice = voice(s.get('SWITCHED', ''), 'dialogue')
        checks['switch_plays_on_new_endpoint'] = bool(switched_voice) and switched_voice['state'] == 'playing' and \
            switched_voice['routed'] == int(b_centre) and dialogue_placed('second', 'SWITCHED', b_centre) and \
            dialogue('second', 'SWITCHED')['peak'] > 0.3 * reference
        checks['switch_silences_old_endpoint'] = dialogue('primary', 'SWITCHED')['peak'] < 0.05 * reference
        checks['return_reopens_in_place'] = after.count(f'attempting in-place device reopen to {a_name}') == 1 and \
            mode_before('RETURNED') in MODES.get(primary['channels'], set())
        checks['return_plays_on_first_endpoint'] = dialogue_placed('primary', 'RETURNED', has_centre) and \
            dialogue('primary', 'RETURNED')['peak'] > 0.4 * reference and \
            dialogue('second', 'RETURNED')['peak'] < 0.05 * max(dialogue('second', 'SWITCHED')['peak'], 1e-9)
        checks['scene_clock_across_switches'] = all(voice(s.get(p, ''), 'dialogue') for p in ('AUTO', 'RETURNED')) and \
            abs(clock('AUTO', 'RETURNED')) < 350
        details['scene_clock_error_switch_ms'] = clock('AUTO', 'RETURNED')
    details.update({
        'phases_seen': phases, 'output_modes': modes, 'forced_speakers': forced, 'forced_output_mode': forced_mode,
        'scene_clock_error_resets_ms': clock('RIGHT', 'AUTO'),
        'levels': measured, 'layouts': names,
        'placement': {key: {phase: {'440': dialogue(key, phase), '880': effect(key, phase)} for phase in phases} for key in measured},
        'voices': {phase: voice(s.get(phase, ''), 'dialogue') for phase in phases},
        'recorded_seconds': {key: len(r.frames) / r.rate for key, r in recordings.items()},
        'command': command, 'returncode': code,
    })
    return checks, details


def self_test():
    """The placement analysis on synthetic stereo and 5.1 recordings; no device or engine."""
    rate = 48000
    t = numpy.arange(rate * 2) / rate
    line, effect_tone = numpy.sin(2 * math.pi * 440 * t) * 0.2, numpy.sin(2 * math.pi * 880 * t) * 0.2
    side_gain = math.sqrt(1 - 0.75 ** 2) * 0.5  # SoundRouteMonoToCenter at s_centerFractionVO 0.75
    results = {}
    for mask, line_gains, effect_gains in ((0x3, [0.7, 0.7], [0.3, 0.8]),
                                           (0x60F, [side_gain, side_gain, 0.75, 0, side_gain, side_gain], [0.1, 0.6, 0.05, 0, 0.05, 0.5])):
        frames = numpy.outer(line, line_gains) + numpy.outer(effect_tone, effect_gains)
        recording = loopback.Recording(rate, len(line_gains), mask, frames.astype(numpy.float32), [(0.0, 0), (2.0, len(t))])
        line_place = placement(recording.levels(440, 0.5, 1.5), recording.names)
        effect_place = placement(recording.levels(880, 0.5, 1.5), recording.names)
        centre = 'FC' in recording.names
        results[','.join(recording.names)] = (
            (abs(line_place['centre_over_others'] - 0.75 / side_gain) < 0.05 and line_place['lfe_share'] < 0.01) if centre
            else abs(line_place['right_over_left'] - 1) < 0.01) and effect_place['right_over_left'] > 1.5 and \
            (not centre or effect_place['centre_over_others'] < 1)
    print(json.dumps({'self_test': results}))
    return 0 if all(results.values()) else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--assets', type=Path, help='read-only Quake 4 install root (the folder holding q4base)')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--endpoint', help='the output device to qualify (name substring; see --list)')
    parser.add_argument('--second-endpoint', help='another output device to switch to and back during the line')
    parser.add_argument('--list', action='store_true', help='print the active endpoints, their layouts and OpenAL names')
    parser.add_argument('--map', default='game/airdefense1')
    parser.add_argument('--renderer', default='gl', choices=['gl', 'vk'])
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--runtime-dir', type=Path, default=ROOT / '.install',
                        help='runtime tree to run (default .install); a fixed copy keeps a re-stage from closing the client mid-run')
    parser.add_argument('--self-test', action='store_true', help='check the placement analysis on synthetic recordings and exit')
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    global RUNTIME
    RUNTIME = args.runtime_dir.resolve()
    available = loopback.endpoints()
    names = openal_devices()
    if args.list:
        for e in available:
            e['openal'] = next((n for n in names if n == e['name'] or n.endswith(' on ' + e['name'])), None)
        print(json.dumps(available, indent=2))
        return 0
    if not (args.assets and args.output and args.endpoint):
        parser.error('--assets, --output and --endpoint are required (the tones play on the endpoints you name)')
    primary = loopback.find_endpoint(args.endpoint, available)
    primary['openal'] = openal_name(primary, names)
    second = None
    if args.second_endpoint:
        second = loopback.find_endpoint(args.second_endpoint, available)
        if second['id'] == primary['id']:
            parser.error('--second-endpoint must be a different device')
        second['openal'] = openal_name(second, names)
    if RUNTIME == (ROOT / '.install').resolve():
        verify_runtime_is_current(RUNTIME)  # a fixed copy is identified by the hashes in the report instead
    output = args.output.resolve()
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    executable_before = hashlib.sha256((RUNTIME / EXE_NAME).read_bytes()).hexdigest()
    checks, details = run(args, output, primary, second)
    executable_after = hashlib.sha256((RUNTIME / EXE_NAME).read_bytes()).hexdigest()
    checks['runtime_unchanged_during_run'] = executable_before == executable_after
    passed = all(checks.values())
    report = {
        'passed': passed, 'checks': checks,
        'microphone': False, 'host_input': False, 'wave_writer': False,
        'backend': 'WASAPI shared mode' if os.name == 'nt' else 'PulseAudio/PipeWire',
        'capture': 'WASAPI loopback' if os.name == 'nt' else 'sink monitor (parec)',
        'endpoints': {'primary': primary, **({'second': second} if second else {})},
        'centre_capable_endpoint': 'FC' in primary['layout'] or bool(second and 'FC' in second['layout']),
        'limits': 'Measures the operating system mix for each endpoint, not speakers or ears. '
                  'Endpoint loss, hotplug and system-default changes are not triggered here.',
        **details,
        'runtime_dir': str(RUNTIME), 'executable_sha256': executable_before,
        'runtime_sha256': {str(p.relative_to(RUNTIME)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in (RUNTIME / 'baseoq4').iterdir()
                           if p.is_file() and p.suffix.lower() in ('.dll', '.so', '.dylib')},
        'harness_sha256': {Path(f).name: hashlib.sha256(Path(f).read_bytes()).hexdigest()
                           for f in (__file__, loopback.__file__)},
    }
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'checks': checks}, indent=2))
    print(json.dumps({'result': str(output / 'result.json'), 'passed': passed}))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
