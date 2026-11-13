#!/usr/bin/env python3
"""Measure dialogue isolation in stock-map gameplay without host input or a mic.

Fixtures are generated tones in the isolated save path. OpenAL renders to a WAV;
the engine reports the actual source gains, and its screenshot command captures
the Audio settings page. No installed assets are changed.
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
import struct
import subprocess
import sys
import wave
from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]


def rendered_tones(path, windows=None):
    """Matched-tone amplitudes in the actual OpenAL float WAV, not API state.

    windows, when given, receives every 440 Hz window as per-channel amplitudes."""
    data = path.read_bytes()
    offset, pcm, rate, channels = 12, None, 0, 0
    while offset + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, offset)
        payload = offset + 8
        if tag == b'fmt ':
            kind, channels, rate, _, _, bits = struct.unpack_from('<HHIIHH', data, payload)
            assert bits == 32 and (kind == 3 or (kind == 65534 and struct.unpack_from('<I', data, payload + 24)[0] == 3))
        elif tag == b'data':
            pcm = array.array('f', data[payload:payload + min(size, len(data) - payload)])
            if sys.byteorder != 'little':
                pcm.byteswap()
        offset = payload + size + (size & 1)
    assert pcm is not None and rate and channels
    result = {}
    # 100 ms contains 44/88 cycles. Inspect every speaker every 250 ms.
    stride = max(1, rate // 8000)
    count = rate // (10 * stride)
    for frequency in [440, 880]:
        sine = [math.sin(2 * math.pi * frequency * i * stride / rate) for i in range(count)]
        cosine = [math.cos(2 * math.pi * frequency * i * stride / rate) for i in range(count)]
        peaks = [0.0] * channels
        for start in range(0, len(pcm) - count * stride * channels, rate * channels // 4):
            window = []
            for channel in range(channels):
                values = [pcm[start + i * stride * channels + channel] for i in range(count)]
                real = sum(v * c for v, c in zip(values, cosine))
                imag = sum(v * s for v, s in zip(values, sine))
                window.append(2 * math.hypot(real, imag) / count)
                peaks[channel] = max(peaks[channel], window[-1])
            if windows is not None and frequency == 440:
                windows.append(window)
        result[str(frequency)] = peaks
    return result


def tone(path, frequency, seconds=60):
    path.parent.mkdir(parents=True, exist_ok=True)
    pcm = array.array('h', (round(4096 * math.sin(i * 2 * math.pi * frequency / 24000)) for i in range(round(24000 * seconds))))
    if sys.byteorder != 'little':
        pcm.byteswap()
    with wave.open(str(path), 'wb') as output:
        output.setparams((1, 2, 24000, len(pcm), 'NONE', 'not compressed'))
        output.writeframes(pcm.tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--language', default='english')
    parser.add_argument('--awakening', type=Path, help='Read-only installed Awakening content root')
    parser.add_argument('--map', default='game/airdefense1')
    parser.add_argument('--decode-memory', type=int, default=64)
    parser.add_argument('--audio-memory', type=int, default=256)
    parser.add_argument('--compressed', action='store_true', help='Encode original fixtures as Ogg and qualify bounded streaming')
    parser.add_argument('--speakers', type=int, choices=[2, 6], default=2)
    args = parser.parse_args()
    verify_runtime_is_current(ROOT / '.install')
    root = args.output.resolve()
    game_dir = 'q4xbase' if args.awakening else 'baseoq4'
    game = root / 'save' / game_dir
    game.mkdir(parents=True, exist_ok=True)
    tone(game / 'sound/vo_english/audio_test/dialogue.wav', 440)
    tone(game / 'sound/audio_test/weapon.wav', 880)
    if args.compressed:
        ffmpeg = shutil.which('ffmpeg')
        if not ffmpeg:
            from imageio_ffmpeg import get_ffmpeg_exe
            ffmpeg = get_ffmpeg_exe()
        for relative in ['sound/vo_english/audio_test/dialogue.wav', 'sound/audio_test/weapon.wav']:
            wav = game / relative
            subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', str(wav),
                            '-c:a', 'libvorbis', '-q:a', '5', str(wav.with_suffix('.ogg'))], cwd=root, check=True)
            wav.unlink()  # Only the just-created disposable fixture, never installed assets.
    # sound/vo is canonical; the loader must resolve the English VO fallback
    # even when the selected text language does not have a voice track.
    steps = ['notarget', 'set s_volume 1', 'set s_musicVolume 0', 'set s_dialogueVolume 1',
             'testRadioChatter fixture sound/vo/audio_test/dialogue.wav',
             'testRadioChatter distract sound/audio_test/weapon.wav']
    def probe(name):
        steps.extend(['wait 5', f'echo AUDIO_DIALOGUE_{name}', 'listPlayingSounds'])
    probe('FULL')
    steps.append('set s_dialogueVolume 0.25')
    probe('QUARTER')
    steps.append('set s_dialogueVolume 0')
    probe('MUTE')
    steps.append('set s_dialogueVolume 0.6')
    probe('RETURN')
    steps.extend(['testRadioChatter pause', 'waitMsec 250', 'testRadioChatter resume', 'waitMsec 600'])
    probe('RESUME')
    steps.extend(['set timescale 0.5'])
    probe('SLOW')
    steps.extend(['set timescale 1', 's_restart'])
    probe('RESTART')
    steps.extend(['set g_autoExecAfterMapLoad ""', 'echo AUDIO_DIALOGUE_SAVE', 'saveGame audio_dialogue', 'waitMsec 500',
                  'loadGame audio_dialogue', 'waitMsec 1000'])
    probe('RESTORE')
    steps.extend(['listSounds audio_test', 'writeConfig "audio-dialogue-saved.cfg"', 'testRadioChatter stop',
                  'testGUI guis/mainmenu.gui', 'waitMsec 1000',
                  'openq4_guiAction main_b_settings', 'waitMsec 1000',
                  'openq4_guiAction set_b_audio', 'waitMsec 500',
                  'screenshot "screenshots/audio-dialogue.tga"', 'testGUI',
                  'echo AUDIO_DIALOGUE_COMPLETE', 'quit'])
    (game / 'audio-dialogue.cfg').write_text('\n'.join(steps) + '\n', encoding='utf-8')
    config = root / 'alsoft.conf'
    config.write_text('[general]\ndrivers=wave\nchannels=' + ('surround51' if args.speakers == 6 else 'stereo') +
                      '\nhrtf=false\n[wave]\nfile=' + str(root / 'mix.wav') + '\n', encoding='utf-8')
    cvars = {
        'fs_basepath': str(args.assets.resolve()), 'fs_savepath': str(root / 'save'), 'fs_devpath': str(root / 'save'),
        'fs_game': game_dir, 'com_gameMode': 'SP', 'si_gameType': 'singleplayer', 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0', 'r_borderlessDefaultMigrated': '1',
        'win_allowMultipleInstances': '1',
        'r_hiddenWindow': '1', 'r_windowWidth': '1280', 'r_windowHeight': '720', 'r_mode': '-1',
        'r_customWidth': '1280', 'r_customHeight': '720', 'r_renderApi': 'gl', 'r_multiSamples': '0',
        'r_swapInterval': '1', 'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
        # Both maps open in a cinematic, which refuses the save the RESTORE phase needs.
        's_noSound': '0', 's_muteUnfocused': '0', 'com_skipLogoVideos': '1', 'g_autoSkipCinematics': '1',
        's_decodeMemoryMB': str(args.decode_memory),
        's_audioMemoryMB': str(args.audio_memory),
        's_numberOfSpeakers': str(args.speakers), 's_openALHRTF': '0',
        'com_skipLoadingContinue': '1', 'com_maxfps': '60', 'ui_autoJoin': '0', 'ui_retained': '1',
        'sys_lang': args.language, 'g_autoExecAfterMapLoad': 'audio-dialogue.cfg', 'g_autoExecAfterMapLoadDelayMs': '3000'
    }
    if args.compressed:
        cvars.update(s_streamThresholdKB='512', s_debugHardware='1')
    if args.speakers == 6:
        cvars.update(s_debugHardware='1')
    if args.awakening:
        cvars['fs_awakeningpath'] = str(args.awakening.resolve(strict=True))
    exe = ROOT / '.install' / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
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
    with (root / 'process.log').open('w') as output:
        run = subprocess.run(command, cwd=ROOT / '.install', stdout=output, stderr=subprocess.STDOUT, timeout=240, **options)
    log = (game / 'logs/openq4.log').read_text(encoding='utf-8', errors='replace')
    rows = {}
    for name, section in re.findall(r'^AUDIO_DIALOGUE_(\w+)\s*\n(.*?)(?=^AUDIO_DIALOGUE_|\Z)', log, re.M | re.S):
        rows[name] = {Path(sample).stem: [float(gain), float(pitch)] for gain, pitch, sample in
                      re.findall(r'^\s*\d+\s+\d+\s+([\d.]+)\s+([\d.]+)\s+(\S*audio_test/\S+)', section, re.M)}
    checks = {'complete': 'AUDIO_DIALOGUE_COMPLETE' in log and run.returncode == 0}
    full = rows.get('FULL', {})
    for name, expected in [('QUARTER', .25), ('RETURN', .6), ('RESUME', .6), ('SLOW', .6), ('RESTART', .6), ('RESTORE', .6)]:
        row = rows.get(name, {})
        checks[name + '_speech'] = bool(full.get('dialogue') and row.get('dialogue') and
                                      abs(row['dialogue'][0] / full['dialogue'][0] - expected) < .025)
        checks[name + '_effect'] = bool(full.get('weapon') and row.get('weapon') and abs(row['weapon'][0] - full['weapon'][0]) < .01)
    checks['mute_speech_only'] = 'dialogue' not in rows.get('MUTE', {}) and 'weapon' in rows.get('MUTE', {})
    checks['slow_motion_pitch'] = bool(rows.get('SLOW', {}).get('dialogue') and abs(rows['SLOW']['dialogue'][1] - .5) < .01)
    # A real save and load: a cinematic refuses the save, which would leave RESTORE
    # measuring the same session rather than a restored one.
    # A failed load also reloads the level, so it is ruled out by its own warnings.
    checks['saved_and_loaded'] = "Can't save during a cinematic" not in log and 'could not restore saved state' not in log and \
        "Couldn't open savegame" not in log and bool(re.search(
        r"^AUDIO_DIALOGUE_SAVE\s*\n(?:.*\n)*?Saved 'audio_dialogue'\s*\n(?:.*\n)*?Map: ", log, re.M))
    # Engine settings are deliberately shared by the two SP campaigns.
    saved = root / 'save/baseoq4/audio-dialogue-saved.cfg'
    checks['archived'] = saved.exists() and bool(re.search(r'seta s_dialogueVolume "0\.6"', saved.read_text()))
    checks['engine_capture'] = (game / 'screenshots/audio-dialogue.tga').is_file()
    checks['rendered_audio'] = (root / 'mix.wav').is_file() and (root / 'mix.wav').stat().st_size > 48000
    speech_windows = []
    tones = rendered_tones(root / 'mix.wav', speech_windows) if checks['rendered_audio'] else {}
    checks['rendered_speech_and_effect'] = len(tones) == 2 and all(max(values) > .02 for values in tones.values())
    if args.speakers == 6:
        checks['engine_center_routing'] = 'Centre routing: sound/vo/audio_test/dialogue' in log
        speech = tones.get('440', [])
        # Compare channels within the same windows while the speech fixture is
        # loud. Whole-file peaks mixed moments: the map's own ambience adds
        # some 440 Hz energy to a surround channel, which once outweighed the
        # fixture's routed share when the run (and so the recording) was short.
        loud = [w for w in speech_windows if len(w) == 6 and speech and w[2] >= .5 * speech[2]]
        ratios = sorted(w[2] / max(w[0], w[1], w[4], w[5], 1e-9) for w in loud)
        center_ratio = ratios[len(ratios) // 2] if ratios else 0.0
        checks['rendered_center_speech'] = len(speech) == 6 and speech[2] > .02 and center_ratio > 1.5
    memory = re.search(r'Vorbis admission: pending=(\d+) KiB peak=(\d+) KiB budget=(\d+) MiB waits=(\d+)', log)
    checks['decode_budget'] = bool(memory and int(memory[1]) == 0 and int(memory[2]) <= int(memory[3]) * 1024)
    checks['no_decode_rejections'] = 'exceed s_decodeMemoryMB' not in log and 'exceeds s_decodeMemoryMB' not in log
    aggregate = re.search(r'Audio admission: reserved=(\d+) KiB peak=(\d+) KiB budget=(\d+) MiB refused=(\d+)', log)
    checks['aggregate_budget'] = bool(aggregate and int(aggregate[1]) <= int(aggregate[2]) <= int(aggregate[3]) * 1024)
    checks['no_aggregate_refusals'] = bool(aggregate and int(aggregate[4]) == 0)
    if args.compressed:
        checks['compressed_fixtures'] = all(re.search(r'Compressed stream: sound/[^\n]*audio_test/' + name + r'\.ogg', log)
                                            for name in ['dialogue', 'weapon'])
        streams = re.search(r'Vorbis streaming: reserved=(\d+) KiB peak=(\d+) KiB budget=(\d+) MiB rejected=(\d+)', log)
        checks['stream_budget'] = bool(streams and 0 < int(streams[1]) <= int(streams[2]) <= int(streams[3]) * 1024 and int(streams[4]) == 0)
        checks['no_stream_decode_failure'] = 'cannot open within its fixed decoder arena' not in log
    report = {'checks': checks, 'gains': rows, 'rendered_tones': tones,
              'center_ratio_median': center_ratio if args.speakers == 6 else None, 'speakers': args.speakers, 'language': args.language, 'compressed': args.compressed, 'returncode': run.returncode,
              'executable_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'command': command,
              'runtime_sha256': {str(p.relative_to(ROOT / '.install')): hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in (ROOT / '.install/baseoq4').iterdir()
                                 if p.is_file() and p.suffix.lower() in ('.dll', '.so', '.dylib', '.pk4')},
              'harness_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'microphone': False, 'host_input': False, 'output': 'OpenAL wave', 'passed': all(checks.values())}
    (root / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'result': str(root / 'result.json'), 'checks': checks}, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
