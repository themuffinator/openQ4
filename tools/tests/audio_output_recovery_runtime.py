#!/usr/bin/env python3
"""Qualify audio output recovery, live layout changes and the low-memory trim in gameplay.

Two hidden stock-map runs render through OpenAL Soft's wave writer; no physical
device, microphone or host input is used. The stereo run interrupts the output
with s_testDeviceInterruption (every source stops, as a disconnect does) while
original tone fixtures play, including a paused line and Ogg streams without
usable length metadata, then trims audio memory and replays a purged level
sample. The surround run changes s_numberOfSpeakers and s_openALHRTF during a
dialogue line and checks that the device is reset in place and the line
re-routes at its scene clock. Engine reports and the rendered WAVs are checked.
"""
import argparse
import array
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import wave

from audio_dialogue import rendered_tones, tone
from audio_streaming import ogg_pages, reseal
from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
ROW = re.compile(r'^\s*\d+\s+\d+\s+([\d.]+)\s+([\d.]+)\s+(\S+)', re.M)
VOICE = re.compile(r'^SOUND_VOICE source=(\d+) state=(\w+) routed=(\d) queued=(\d) serial=(\d+) offsetMs=([\d.-]+) sample=(\S+)', re.M)
OUTPUT = re.compile(r'^SOUND_OUTPUT serial=(\d+) connected=(\d) centre=(\d) interrupted=(\d)', re.M)
WORLD = re.compile(r'^SOUND_WORLD id=1 paused=(\d) time=(-?\d+) .*?stamp=(-?\d+) now=(-?\d+)', re.M)


def ffmpeg_exe():
    found = shutil.which('ffmpeg')
    if found:
        return found
    from imageio_ffmpeg import get_ffmpeg_exe
    return get_ffmpeg_exe()


def encode(wav, ogg):
    subprocess.run([ffmpeg_exe(), '-hide_banner', '-loglevel', 'error', '-y', '-i', str(wav),
                    '-c:a', 'libvorbis', '-q:a', '5', str(ogg)], check=True)
    wav.unlink()  # only the disposable fixture this harness just generated


def strip_length(path, how):
    """noeos: the end-of-stream flag is gone (stb still finds the length);
    nogranule: the last two pages carry no position (no length at all)."""
    data = bytearray(path.read_bytes())
    pages = ogg_pages(data)
    assert len(pages) > 4 and pages[-1][0] + pages[-1][1] == len(data), path
    targets = pages[-1:] if how == 'noeos' else pages[-2:]
    for offset, length in targets:
        if how == 'noeos':
            data[offset + 5] &= ~4
        else:
            data[offset + 6:offset + 14] = b'\xff' * 8
        reseal(data, offset, length)
    path.write_bytes(bytes(data))


def noisy_tone(path, frequency):
    """A tone over steady noise: Vorbis compresses it like ordinary sound (a pure
    tone shrinks far below the 4:1 the stream selection assumes without a length)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    seed, frames = 12345, []
    for i in range(24000 * 60):
        seed = (seed * 1103515245 + 12345) & 0x7fffffff
        frames.append(round(4096 * math.sin(i * 2 * math.pi * frequency / 24000)) + (seed % 2401) - 1200)
    pcm = array.array('h', frames)
    if sys.byteorder != 'little':
        pcm.byteswap()
    with wave.open(str(path), 'wb') as output:
        output.setparams((1, 2, 24000, len(pcm), 'NONE', 'not compressed'))
        output.writeframes(pcm.tobytes())


def encoded_fixtures(target):
    """The two Ogg stream fixtures; they are deterministic, so a host without ffmpeg can reuse them."""
    for name, frequency in (('noeos', 660), ('nogranule', 550)):
        wav = target / f'{name}.wav'
        noisy_tone(wav, frequency)
        ogg = wav.with_suffix('.ogg')
        encode(wav, ogg)
        strip_length(ogg, name)


def fixtures(game, encoded=None):
    tone(game / 'sound/vo_english/audio_test/dialogue.wav', 440)
    tone(game / 'sound/audio_test/effect.wav', 880)
    if encoded is None:
        encoded_fixtures(game / 'sound/audio_test')
    else:
        for name in ('noeos', 'nogranule'):
            shutil.copyfile(encoded / f'{name}.ogg', game / f'sound/audio_test/{name}.ogg')


def sections(log):
    return {name: body for name, body in re.findall(r'^AUDIO_OUTPUT_(\w+)\s*\n(.*?)(?=^AUDIO_OUTPUT_|\Z)', log, re.M | re.S)}


def playing(section):
    return {Path(sample).stem: float(gain) for gain, _, sample in ROW.findall(section)}


def voices(section):
    return {Path(m[6]).stem: {'state': m[1], 'routed': int(m[2]), 'queued': int(m[3]), 'serial': int(m[4]),
                              'offset': float(m[5])} for m in VOICE.findall(section)}


def output(section):
    m = OUTPUT.search(section)
    return {'serial': int(m[1]), 'connected': int(m[2]), 'centre': int(m[3]), 'interrupted': int(m[4])} if m else {}


def world_time(section):
    m = WORLD.search(section)
    return int(m[2]) if m else None


def scene_time(section):
    """The game world's sound clock when the probe ran. The reported time was stamped
    at the last mixed frame, while voice offsets are read now; a running world's
    clock has moved on by the difference, which a long frame makes hundreds of ms."""
    m = WORLD.search(section)
    if not m:
        return None
    return int(m[2]) + (0 if m[1] == '1' else int(m[4]) - int(m[3]))


def frame_lag(sections_by_name):
    """The longest a probe waited after the last mixed frame. The checks give each
    phase a few frames to react; on a machine too busy to keep a hidden client near
    60 Hz they cannot, and this says so instead of the phase checks failing alone."""
    lags = [int(m[4]) - int(m[3]) for body in sections_by_name.values() for m in WORLD.finditer(body)]
    return max(lags) if lags else None


FRAME_LAG_LIMIT_MS = 250


def run(args, root, speakers, steps):
    game = root / 'save' / 'baseoq4'
    game.mkdir(parents=True, exist_ok=True)
    fixtures(game, args.fixtures)
    (game / 'audio-output.cfg').write_text('\n'.join(steps) + '\n', encoding='utf-8')
    config = root / 'alsoft.conf'
    config.write_text('[general]\ndrivers=wave\nchannels=' + ('surround51' if speakers == 6 else 'stereo') +
                      '\n[wave]\nfile=' + str(root / 'mix.wav') + '\n', encoding='utf-8')
    cvars = {
        'fs_basepath': str(args.assets.resolve()), 'fs_savepath': str(root / 'save'), 'fs_devpath': str(root / 'save'),
        'fs_game': 'baseoq4', 'com_gameMode': 'SP', 'si_gameType': 'singleplayer', 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0', 'r_borderlessDefaultMigrated': '1',
        'win_allowMultipleInstances': '1', 'r_hiddenWindow': '1', 'r_windowWidth': '1280', 'r_windowHeight': '720',
        'r_mode': '-1', 'r_customWidth': '1280', 'r_customHeight': '720', 'r_renderApi': args.renderer, 'r_multiSamples': '0',
        'r_swapInterval': '1', 'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
        's_noSound': '0', 's_muteUnfocused': '0', 'com_skipLogoVideos': '1', 'g_autoSkipCinematics': '1',
        's_numberOfSpeakers': str(speakers), 's_openALHRTF': '0', 's_streamThresholdKB': '512', 's_debugHardware': '1',
        'com_skipLoadingContinue': '1', 'com_maxfps': '60', 'ui_autoJoin': '0', 'ui_retained': '1',
        'sys_lang': 'english', 'g_autoExecAfterMapLoad': 'audio-output.cfg', 'g_autoExecAfterMapLoadDelayMs': '3000',
    }
    exe = args.runtime_dir / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    command = [str(exe)]
    for key, value in cvars.items():
        command += ['+set', key, value]
    command += ['+map', args.map]
    options = {'env': {**os.environ, 'ALSOFT_CONF': str(config), 'ALSOFT_DRIVERS': 'wave'}}
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    with (root / 'process.log').open('w') as out:
        process = subprocess.run(command, cwd=args.runtime_dir, stdout=out, stderr=subprocess.STDOUT, timeout=args.timeout, **options)
    log = (game / 'logs/openq4.log').read_text(encoding='utf-8', errors='replace')
    return process.returncode, log, command


def check_stereo(args, root):
    status = ['echo AUDIO_OUTPUT_{0}', 's_soundWorlds', 'listSoundVoices', 'listPlayingSounds']
    probe = lambda name: [line.format(name) for line in status]
    steps = ['notarget', 'set s_volume 1', 'set s_musicVolume 0', 'set s_dialogueVolume 1',
             'testRadioChatter fixture sound/vo/audio_test/dialogue.wav',
             'testRadioChatter distract sound/audio_test/effect.wav', 'waitMsec 2500']
    steps += probe('BEFORE') + ['s_testDeviceInterruption 1500', 'waitMsec 400']
    steps += probe('DURING') + ['waitMsec 2200'] + probe('AFTER')
    # A line paused across the outage: OpenAL would replay it from its first sample.
    steps += ['testRadioChatter pause', 'waitMsec 300'] + probe('PAUSED') + \
             ['s_testDeviceInterruption 500', 'waitMsec 900', 'testRadioChatter resume', 'waitMsec 700'] + probe('RESUMED')
    # Separate logical channels: the player's body channel (replacing the effect
    # fixture) and the world's direct-play channel.
    steps += ['testRadioChatter distract sound/audio_test/noeos.ogg', 'testSound sound/audio_test/nogranule.ogg', 'waitMsec 2000']
    steps += probe('STREAMS') + ['s_testDeviceInterruption 700', 'waitMsec 1600'] + probe('STREAMS_AFTER')
    # Dialogue Volume holds through an outage: the restarted line keeps the user's level.
    steps += ['set s_dialogueVolume 0.25', 'waitMsec 300'] + probe('QUARTER') + \
             ['s_testDeviceInterruption 600', 'waitMsec 1500'] + probe('QUARTER_AFTER') + ['set s_dialogueVolume 1']
    # The trim answers the operating system's low-memory event, pushed through SDL's queue
    # and event watchers as Android and iOS deliver it; idle 0 makes every idle sample
    # eligible, so the outcome is the same on every run.
    steps += ['echo AUDIO_OUTPUT_TRIM_BEFORE', 'listSounds weapons/blaster', 'set s_trimIdleSeconds 0',
              'testLowMemoryNotice', 'waitMsec 100', 'reset s_trimIdleSeconds',
              'echo AUDIO_OUTPUT_TRIM_AFTER', 'listSounds weapons/blaster', 'listPlayingSounds',
              'testSound sound/weapons/blaster/fire01', 'waitMsec 100',
              'echo AUDIO_OUTPUT_TRIM_REPLAY', 'listSounds weapons/blaster', 'listPlayingSounds',
              'echo AUDIO_OUTPUT_COMPLETE', 'quit']
    code, log, command = run(args, root, 2, steps)
    s = sections(log)
    checks = {'complete': 'AUDIO_OUTPUT_COMPLETE' in log and code == 0}
    lag = frame_lag(s)
    checks['frames_kept_pace'] = lag is not None and lag < FRAME_LAG_LIMIT_MS
    before, during, after = (voices(s.get(k, '')) for k in ('BEFORE', 'DURING', 'AFTER'))
    checks['before_playing'] = {'dialogue', 'effect'} <= set(playing(s.get('BEFORE', '')))
    checks['interruption_silences_output'] = output(s.get('DURING', '')).get('connected') == 0 and \
        not ({'dialogue', 'effect'} & set(playing(s.get('DURING', ''))))
    checks['no_voices_while_interrupted'] = all(v['state'] != 'playing' for v in during.values())
    checks['recovered_after_return'] = {'dialogue', 'effect'} <= set(playing(s.get('AFTER', '')))
    checks['output_returned'] = output(s.get('AFTER', '')).get('connected') == 1 and \
        output(s.get('AFTER', '')).get('serial', 0) > output(s.get('BEFORE', '')).get('serial', 0) and \
        'OpenAL output changed (simulated interruption ended)' in log
    elapsed = (scene_time(s.get('AFTER', '')) or 0) - (scene_time(s.get('BEFORE', '')) or 0)
    moved = after.get('dialogue', {}).get('offset', 0) - before.get('dialogue', {}).get('offset', 0)
    checks['resumed_at_scene_clock'] = bool(before.get('dialogue') and after.get('dialogue')) and \
        abs(moved - elapsed) < 350 and after['dialogue']['offset'] > 2000
    paused, resumed = voices(s.get('PAUSED', '')), voices(s.get('RESUMED', ''))
    paused_elapsed = (scene_time(s.get('RESUMED', '')) or 0) - (scene_time(s.get('PAUSED', '')) or 0)
    paused_moved = resumed.get('dialogue', {}).get('offset', 0) - paused.get('dialogue', {}).get('offset', 0)
    checks['paused_line_resumes_in_place'] = bool(paused.get('dialogue') and resumed.get('dialogue')) and \
        resumed['dialogue']['state'] == 'playing' and abs(paused_moved - paused_elapsed) < 350 and resumed['dialogue']['offset'] > 2000
    checks['stream_without_eos_streams'] = bool(re.search(r'Compressed stream: sound/audio_test/noeos\.ogg, \d+ Hz, 1 channels, \d+ frames\r?$', log, re.M))
    checks['stream_without_length_counted'] = 'Compressed stream: sound/audio_test/nogranule.ogg' in log and \
        bool(re.search(r'nogranule\.ogg, \d+ Hz, 1 channels, \d+ frames \(counted; no length metadata\)', log))
    checks['streams_playing'] = {'noeos', 'nogranule'} <= set(playing(s.get('STREAMS', '')))
    checks['streams_resume_after_interruption'] = {'noeos', 'nogranule', 'dialogue'} <= set(playing(s.get('STREAMS_AFTER', '')))
    quarter, quarter_after = playing(s.get('QUARTER', '')), playing(s.get('QUARTER_AFTER', ''))
    checks['dialogue_volume_kept_across_outage'] = all(
        abs(phase.get('dialogue', 0) - 0.25) < 0.02 and phase.get('noeos', 0) > 0.9 for phase in (quarter, quarter_after)) and \
        voices(s.get('QUARTER_AFTER', '')).get('dialogue', {}).get('serial', 0) > voices(s.get('QUARTER', '')).get('dialogue', {}).get('serial', 0)
    trim = re.search(r'AUDIO_TRIM reason=os low-memory idle=0 freed=(\d+) freedKiB=(\d+) copies=(\d+) copyKiB=(\d+) active=(\d+)', log)
    checks['trim_follows_os_low_memory_event'] = 'testLowMemoryNotice: low-memory event pushed' in log and \
        'low-memory event received' in log and log.count('AUDIO_TRIM reason=os low-memory') == 1
    checks['trim_freed_idle_level_samples'] = bool(trim and int(trim[1]) > 0 and int(trim[2]) > 0)
    checks['trim_kept_active_samples'] = bool(trim and int(trim[5]) >= 4) and \
        {'dialogue', 'noeos', 'nogranule'} <= set(playing(s.get('TRIM_AFTER', '')))
    blaster = lambda key: re.findall(r'^.*sound/weapons/blaster/fire01(.*)$', s.get(key, ''), re.M)
    checks['trim_purged_level_sample'] = bool(blaster('TRIM_BEFORE')) and '(PURGED)' not in blaster('TRIM_BEFORE')[0] and \
        bool(blaster('TRIM_AFTER')) and '(PURGED)' in blaster('TRIM_AFTER')[0]
    checks['purged_sample_reloads_on_play'] = bool(blaster('TRIM_REPLAY')) and '(PURGED)' not in blaster('TRIM_REPLAY')[0] and \
        'fire01' in playing(s.get('TRIM_REPLAY', ''))
    windows = []
    mix = root / 'mix.wav'
    checks['rendered_audio'] = mix.is_file() and mix.stat().st_size > 48000
    if checks['rendered_audio']:
        rendered_tones(mix, windows)
    levels = [max(w) for w in windows]
    # The file starts at the device's open, so a cold load leaves minutes of digital
    # silence before the map; measure the loud level over the audible span only.
    audible = [i for i, level in enumerate(levels) if level > 1e-4]
    span = levels[audible[0]:audible[-1] + 1] if audible else []
    loud = statistics.median(sorted(span)[-max(1, len(span) // 4):]) if span else 0
    quiet = [level < 0.25 * loud for level in levels]
    # 440 Hz plays, stops for the 1.5 s outage (at least four 250 ms windows), then plays again.
    gap = re.search(r'L+Q{4,}L+', ''.join('Q' if q else 'L' for q in quiet)) if loud > 0.01 else None
    checks['rendered_gap_then_resume'] = bool(gap)
    return checks, {'log_sections': {k: {'playing': playing(v), 'voices': voices(v), 'output': output(v), 'world_time': world_time(v),
                                          'scene_time': scene_time(v)} for k, v in s.items()},
                    'probe_frame_lag_ms': lag, 'trim': trim.group(0) if trim else None, 'elapsed_ms': elapsed, 'dialogue_moved_ms': moved,
                    'paused_elapsed_ms': paused_elapsed, 'paused_moved_ms': paused_moved, 'rendered_440_loud': loud,
                    'rendered_440_levels': levels,
                    'command': command, 'returncode': code}


def check_surround(args, root):
    probe = lambda name: [f'echo AUDIO_OUTPUT_{name}', 's_soundWorlds', 'listSoundVoices', 'listPlayingSounds']
    steps = ['notarget', 'set s_volume 1', 'set s_musicVolume 0', 'set s_dialogueVolume 1',
             'testRadioChatter fixture sound/vo/audio_test/dialogue.wav', 'waitMsec 2000']
    steps += probe('SURROUND') + ['set s_numberOfSpeakers 2', 'waitMsec 1600'] + probe('STEREO')
    steps += ['set s_numberOfSpeakers 6', 'waitMsec 1600'] + probe('SURROUND_AGAIN')
    # Dialogue Volume at half from here: it must survive both in-place resets that follow.
    steps += ['set s_dialogueVolume 0.5', 'set s_openALHRTF 2', 'waitMsec 1600'] + probe('HRTF')
    steps += ['set s_openALHRTF 0', 'waitMsec 1600'] + probe('LAYOUT') + ['waitMsec 3000', 'echo AUDIO_OUTPUT_COMPLETE', 'quit']
    code, log, command = run(args, root, 6, steps)
    s = sections(log)
    loaded = log.index('AUDIO_OUTPUT_SURROUND') if 'AUDIO_OUTPUT_SURROUND' in log else len(log)
    expected = {'SURROUND': 1, 'STEREO': 0, 'SURROUND_AGAIN': 1, 'HRTF': 0, 'LAYOUT': 1}
    checks = {'complete': 'AUDIO_OUTPUT_COMPLETE' in log and code == 0}
    lag = frame_lag(s)
    checks['frames_kept_pace'] = lag is not None and lag < FRAME_LAG_LIMIT_MS
    times = []
    for phase, routed in expected.items():
        v = voices(s.get(phase, '')).get('dialogue')
        checks[f'{phase.lower()}_routing'] = bool(v) and v['routed'] == routed and v['state'] == 'playing' and \
            'dialogue' in playing(s.get(phase, '')) and output(s.get(phase, '')).get('centre') == routed
        times.append(world_time(s.get(phase, '')))
    after_load = log[loaded:]
    # Counted from the first probe: the profile's startup layout change also resets in place.
    checks['reset_in_place'] = after_load.count('speaker mode changed; device reset in place') == 2 and \
        after_load.count('HRTF mode changed; device reset in place') == 2
    checks['no_restart_or_reload'] = 'Sound System Restart' not in after_load and 'sound samples reloaded' not in after_load
    checks['hrtf_enabled_then_disabled'] = 'OpenAL HRTF status: enabled' in after_load
    gains = {phase: playing(s.get(phase, '')).get('dialogue', 0) for phase in ('SURROUND_AGAIN', 'HRTF', 'LAYOUT')}
    checks['dialogue_volume_kept_across_reset'] = abs(gains['SURROUND_AGAIN'] - 1) < 0.02 and \
        abs(gains['HRTF'] - 0.5) < 0.02 and abs(gains['LAYOUT'] - 0.5) < 0.02
    checks['rerouted_at_scene_clock'] = all(t is not None for t in times) and \
        bool(re.findall(r'Output change recovery: sound/vo/audio_test/dialogue', after_load))
    windows = []
    mix = root / 'mix.wav'
    tones = rendered_tones(mix, windows) if mix.is_file() and mix.stat().st_size > 48000 else {}
    speech = tones.get('440', [])
    loud = [w for w in windows if len(w) == 6 and speech and w[2] >= .5 * speech[2]]
    ratios = sorted(w[2] / max(w[0], w[1], w[4], w[5], 1e-9) for w in loud)
    ratio = ratios[len(ratios) // 2] if ratios else 0.0
    # The wave writer restarts its file at each reset: this is the last, 5.1, layout.
    checks['rendered_centre_after_transitions'] = len(speech) == 6 and speech[2] > .02 and ratio > 1.5
    return checks, {'log_sections': {k: {'playing': playing(v), 'voices': voices(v), 'output': output(v), 'world_time': world_time(v),
                                          'scene_time': scene_time(v)} for k, v in s.items()},
                    'probe_frame_lag_ms': lag, 'centre_ratio_median': ratio, 'rendered_tones': tones, 'command': command, 'returncode': code}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, help='Read-only Quake 4 install root (the folder holding q4base)')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--runtime-dir', type=Path, default=ROOT / '.install', help='staged or extracted package to run')
    parser.add_argument('--map', default='game/airdefense1')
    parser.add_argument('--renderer', default='gl', choices=['gl', 'vk'])
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--fixtures', type=Path, help='Use the Ogg stream fixtures --write-fixtures made (for a host without ffmpeg)')
    parser.add_argument('--write-fixtures', type=Path, help='Only encode the Ogg stream fixtures into this directory')
    args = parser.parse_args()
    if args.write_fixtures:
        args.write_fixtures.mkdir(parents=True, exist_ok=True)
        encoded_fixtures(args.write_fixtures.resolve())
        print(json.dumps({'fixtures': str(args.write_fixtures.resolve())}))
        return 0
    if args.assets is None or args.output is None:
        parser.error('--assets and --output are required')
    if args.fixtures is not None:
        args.fixtures = args.fixtures.resolve(strict=True)
    args.runtime_dir = args.runtime_dir.resolve()
    verify_runtime_is_current(args.runtime_dir)
    output_root = args.output.resolve()
    report = {'microphone': False, 'host_input': False, 'output': 'OpenAL wave writer', 'runs': {}}
    passed = True
    for name, check in (('stereo', check_stereo), ('surround', check_surround)):
        root = output_root / name
        if root.exists():
            shutil.rmtree(root)
        root.mkdir(parents=True)
        checks, details = check(args, root)
        report['runs'][name] = {'checks': checks, **details}
        passed &= all(checks.values())
        print(json.dumps({name: checks}, indent=2))
    exe = args.runtime_dir / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    report.update(passed=passed, executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  runtime_sha256={str(p.relative_to(args.runtime_dir)): hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in (args.runtime_dir / 'baseoq4').iterdir()
                                  if p.is_file() and p.suffix.lower() in ('.dll', '.so', '.dylib', '.pk4')},
                  harness_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    (output_root / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'result': str(output_root / 'result.json'), 'passed': passed}))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
