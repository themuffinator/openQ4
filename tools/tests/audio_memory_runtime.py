#!/usr/bin/env python3
"""Force audio payload pressure in a loaded map, without microphones or host input."""
import argparse
import array
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import sys
import wave
from audio_dialogue import rendered_tones
from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ui'))
import capture_retained_screens as capture


def tone_bytes(frequency, length):
    # 24 kHz gives integral 440/880 Hz cycles in this 600-frame block.
    block = array.array('h', (round(4096 * math.sin(i * 2 * math.pi * frequency / 24000)) for i in range(600)))
    if sys.byteorder != 'little': block.byteswap()
    data = block.tobytes()
    result = io.BytesIO()
    with wave.open(result, 'wb') as wav:
        wav.setparams((1, 2, 24000, 0, 'NONE', 'not compressed'))
        for offset in range(0, length, len(data)): wav.writeframesraw(data[:min(len(data), length-offset)])
    return result.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('choose a new output directory under the project .tmp')
    args.runtime, args.renderer, args.gate, args.timeout, args.set = ROOT / '.install', 'gl', '1', 240, []
    args.size, args.language = '1280x720', 'english'
    verify_runtime_is_current(args.runtime)
    args.output.mkdir(parents=True)
    config = args.output / 'alsoft.conf'
    config.write_text('[general]\ndrivers=wave\nchannels=stereo\nhrtf=false\n[wave]\nfile=' + str(args.output / 'mix.wav') + '\n')
    os.environ.update(ALSOFT_DRIVERS='wave', ALSOFT_CONF=str(config))
    steps = ['notarget', 'set s_volume 1', 'set s_dialogueVolume 1', 'set s_musicVolume 0',
             'testRadioChatter fixture sound/vo/audio_memory/active.wav', 'waitMsec 300']
    def probe(name):
        steps.extend([f'echo AUDIO_MEMORY_{name}', 'listSounds audio_memory', 'listPlayingSounds'])
    probe('BASELINE')
    steps += ['set s_audioMemoryMB 32', 'testRadioChatter distract sound/audio_memory/large.wav', 'waitMsec 300']
    probe('REFUSED')
    steps += ['set s_audioMemoryMB 256', 'testRadioChatter distract sound/audio_memory/large.wav', 'waitMsec 500']
    probe('RETRY')
    for i in range(2):
        steps += ['s_restart', 'waitMsec 500']
        probe('RESTART' + str(i + 1))
    steps += ['testRadioChatter stop', 'screenshot "screenshots/audio-memory.tga"', 'echo AUDIO_MEMORY_COMPLETE']
    capture.SCENARIOS['pressure'] = ('game/airdefense1', steps)
    capture.SEEDS['pressure'] = {
        'sound/vo_english/audio_memory/active.wav': lambda _: tone_bytes(440, 24000*2*90),
        # Larger than available upload headroom, but CPU data fits after retry.
        'sound/audio_memory/large.wav': lambda _: tone_bytes(880, 64*1024*1024),
    }
    capture.SCENARIO_CVARS['pressure'] = {
        'com_gameMode': 'SP', 'si_gameType': 'singleplayer', 's_noSound': '0', 's_muteUnfocused': '0',
        's_audioMemoryMB': '256', 's_decodeMemoryMB': '16', 's_numberOfSpeakers': '2', 's_openALHRTF': '1',
        's_debugHardware': '1', 'win_allowMultipleInstances': '1'}
    result = capture.run(args, 'pressure')
    log = (ROOT / result['log']).read_text(errors='replace') if result['log'] else ''
    phases = {}
    for name, text in re.findall(r'^AUDIO_MEMORY_(\w+)\s*\n(.*?)(?=^AUDIO_MEMORY_|\Z)', log, re.M | re.S):
        match = re.search(r'Audio admission: reserved=(\d+) KiB peak=(\d+) KiB budget=(\d+) MiB refused=(\d+)', text)
        phases[name] = {'memory': list(map(int, match.groups())) if match else None,
                        'playing': re.findall(r'^\s*\d+\s+\d+\s+[\d.]+\s+[\d.]+\s+(\S*audio_memory/\S+)', text, re.M),
                        'defaulted': bool(re.search(r'audio_memory/\S+\(DEFAULTED\)', text)),
                        'purged_large': 'sound/audio_memory/large(PURGED)' in text}
    def playing(phase, sample): return any('/' + sample + '.' in name for name in phases.get(phase, {}).get('playing', []))
    retry = [phases.get(p, {}).get('memory') for p in ('RETRY', 'RESTART1', 'RESTART2')]
    checks = {
        'completed_gameplay': result['complete'] and result['returncode'] == 0 and not result['errors'],
        'initial_audio': playing('BASELINE', 'active'),
        'pressure_retains_active_audio': playing('REFUSED', 'active') and not playing('REFUSED', 'large'),
        'refused_sample_remains_retryable': phases.get('REFUSED', {}).get('purged_large', False),
        'retry_and_restarts_play_both': all(playing(p, 'active') and playing(p, 'large') for p in ('RETRY','RESTART1','RESTART2')),
        'no_replacement_beep': not any(p['defaulted'] for p in phases.values()),
        'bounded_peak': all(m and m[0] <= m[1] <= 256*1024 for m in retry),
        'no_restart_reservation_leak': all(retry) and max(m[0] for m in retry)-min(m[0] for m in retry) < 1024,
        'reported_refusal': (phases.get('REFUSED', {}).get('memory') or [0,0,0,0])[3] > 0,
        'one_budget_warning': log.count('Audio payload budget exhausted') == 1,
        'pressure_not_reported_as_corrupt_media': 'could not read PCM data' not in log and 'could not decode ADPCM' not in log,
        'engine_capture': len(result['screenshots']) == 1,
    }
    tones = rendered_tones(args.output / 'mix.wav') if (args.output / 'mix.wav').exists() else {}
    checks['rendered_both_tones'] = len(tones) == 2 and all(max(values) > .02 for values in tones.values())
    # Budget reductions retain previously admitted active data; they stop new
    # admissions until it fits. The earlier peak is deliberately not reset.
    result.update(checks=checks, phases=phases, rendered_tones=tones, passed=all(checks.values()),
                  physical_microphone=False, host_input=False,
                  engine_sha256=capture.digest(args.runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')),
                  runtime_sha256={str(p.relative_to(args.runtime)): capture.digest(p) for p in (args.runtime / 'baseoq4').iterdir()
                                  if p.suffix.lower() in ('.dll','.so','.dylib','.pk4')},
                  harness_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'checks': checks, 'phases': phases, 'passed': result['passed']}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
