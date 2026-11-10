#!/usr/bin/env python3
"""AND-02 qualification of a physical Android device: capabilities, campaign, sustained play and multiplayer.

Opt-in and silent: no OS pointer/keyboard input reaches the device. The game is
started through setup's debuggable-only `diagnostic_launch` extra after setup's
own installed-data check, and engine scripts drive play. Phases:

  capabilities  device, GPU, OpenAL provider and storage facts (needs a game run; recorded by every phase)
  campaign      stock single player with sound: hangar1 -> hangar2 -> mcc_landing through the
                levels' own end triggers (inventory carried), then fresh loads of airdefense1
                (outdoor), convoy1 (vehicle) and process1 (industrial lighting), each with a
                timed view sweep and frame trace, audio residency (listSounds) and a capture,
                and a save/load on process1. --cold first moves the generated image cache aside.
  sustain       --sustain-minutes of view sweeps on --sustain-map with one frame trace, the
                device's thermal status, temperatures, app PSS and battery sampled every 15 s,
                and an operating-system memory-trim request half way through.
  mp            a Q4DM1 listen server with --bots bots, a frame trace and sweeps.

The player's config, log and (for --cold) cache are preserved and restored, and
every script, save, capture and trace it creates is removed after collection.
Reports land in --output (under this checkout's .tmp). Budgets and assessment
thresholds live in docs/dev/android-device-budgets.json (--check-budgets).
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import math
import re
import subprocess
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUDGETS = ROOT / 'docs/dev/android-device-budgets.json'
SAVE_ROOT = 'files/saves/baseoq4'
SETTINGS = """set r_fullscreen 0
set ui_retained 1
set ui_autoJoin 0
set in_touchscreen 1
set in_touchControls 1
set s_noSound 0
set developer 1
set com_skipLoadingContinue 1
set g_autoExecAfterMapLoadDelayMs 500
"""
# Campaign: (map, how to leave it). A trigger name leaves through the level's own
# target_endlevel, carrying the player's inventory; a map name loads it fresh.
CAMPAIGN = (
    ('game/hangar1', ('trigger', 'elevNorthPass1GoToHangar2')),
    ('game/hangar2', ('trigger', 'endLevel')),
    ('game/mcc_landing', ('devmap', 'game/airdefense1')),
    ('game/airdefense1', ('devmap', 'game/convoy1')),
    ('game/convoy1', ('devmap', 'game/process1')),
    ('game/process1', ('save', None)),
)
SWEEP_MS = 15000
TRACE_FRAMES = 1200


def utc() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def stage_name(map_name: str) -> str:
    return map_name.split('/')[-1]


def campaign_scripts() -> dict[str, str]:
    scripts = {'autoexec.cfg': SETTINGS + 'set g_autoExecAfterMapLoad and02_0.cfg\nwait 10\ndevmap game/hangar1\n'}
    for index, (map_name, (how, target)) in enumerate(CAMPAIGN):
        name = stage_name(map_name)
        body = [f'echo AND02_STAGE {name}', 'god', 'notarget', 'waitMsec 4000']
        if index == 0:
            body += ['gfxInfo']
        body += ['viewpos', f'screenshot "screenshots/and02-{name}.tga"',
                 f'profileFrameWork {TRACE_FRAMES} "diagnostics/and02-{name}.csv"',
                 f'benchmarkViewSweep 360 {SWEEP_MS}', f'waitMsec {SWEEP_MS + 6000}',
                 # Ends the trace and writes it even when a slow device has not reached the frame count.
                 'profileFrameWork 0', 'waitMsec 1000', 'listSounds', f'echo AND02_STAGE_DONE {name}']
        if how == 'trigger':
            body += [f'set g_autoExecAfterMapLoad and02_{index + 1}.cfg', f'trigger {target}']
        elif how == 'devmap':
            body += [f'set g_autoExecAfterMapLoad and02_{index + 1}.cfg', f'devmap {target}']
        else:
            body += ['viewpos', 'echo AND02_SAVE', 'saveGame AND02Qualification', 'waitMsec 1000',
                     'set g_autoExecAfterMapLoad and02_restored.cfg', 'loadGame AND02Qualification']
        scripts[f'and02_{index}.cfg'] = '\n'.join(body) + '\n'
    scripts['and02_restored.cfg'] = 'echo AND02_RESTORED\nwaitMsec 1000\nviewpos\necho AND02_COMPLETE\nquit\n'
    return scripts


def sustain_scripts(map_name: str, minutes: int) -> dict[str, str]:
    loops = max(1, round(minutes * 60 / 20))
    fps_cap = 120
    body = ['echo AND02_STAGE sustain', 'god', 'notarget', 'waitMsec 4000', 'gfxInfo', 'listSounds',
            f'profileFrameWork {min(60000, int(loops * 20 * fps_cap))} "diagnostics/and02-sustain.csv"']
    for i in range(loops):
        body += [f'benchmarkViewSweep 360 20000', 'waitMsec 20000', f'echo AND02_SUSTAIN_TICK {i}']
        if i == loops // 2:
            body += ['echo AND02_TRIM_REQUEST', 'waitMsec 3000', 'listSounds']
    body += ['profileFrameWork 0', 'waitMsec 1000', 'listSounds', 'echo AND02_STAGE_DONE sustain', 'echo AND02_COMPLETE', 'quit']
    return {'autoexec.cfg': SETTINGS + f'set g_autoExecAfterMapLoad and02_sustain.cfg\nwait 10\ndevmap {map_name}\n',
            'and02_sustain.cfg': '\n'.join(body) + '\n'}


def mp_scripts(bots: int) -> dict[str, str]:
    autoexec = SETTINGS + ('wait 10\nset net_allowCheats 1\nset si_pure 0\nset net_enableIPv6 0\nset ui_autoJoin 1\n'
                           'set ui_spectate Play\nset si_gameType DM\nset si_map mp/q4dm1\nset si_timeLimit 0\nset si_fragLimit 0\n'
                           'set g_autoExecAfterMapLoad and02_mp.cfg\nspawnServer\n')
    body = ['echo AND02_STAGE q4dm1', 'waitMsec 4000', 'gfxInfo'] + ['addbot'] * bots + [
        'waitMsec 8000', f'profileFrameWork {TRACE_FRAMES * 2} "diagnostics/and02-q4dm1.csv"',
        f'benchmarkViewSweep 360 {SWEEP_MS}', f'waitMsec {SWEEP_MS}', f'benchmarkViewSweep 360 {SWEEP_MS}',
        f'waitMsec {SWEEP_MS + 8000}', 'profileFrameWork 0', 'waitMsec 1000', 'listSounds', 'echo AND02_STAGE_DONE q4dm1',
        'echo AND02_COMPLETE', 'quit']
    return {'autoexec.cfg': autoexec, 'and02_mp.cfg': '\n'.join(body) + '\n'}


class Device:
    def __init__(self, args):
        self.args = args
        self.package = args.package

    def adb(self, *a, check=True, timeout=120, data=None):
        r = subprocess.run([str(self.args.adb), '-s', self.args.serial, *a], capture_output=True, input=data, timeout=timeout)
        if check and r.returncode:
            raise RuntimeError(f'adb {a}: {(r.stdout + r.stderr).decode(errors="replace")}')
        return r

    def shell(self, command: str, timeout=60) -> str:
        return self.adb('shell', command, check=False, timeout=timeout).stdout.decode(errors='replace')

    def exists(self, path):
        return self.adb('shell', 'run-as', self.package, 'sh', '-c', f"'test -e {path} && echo yes || echo no'",
                        check=False).stdout.strip() == b'yes'

    def read(self, path):
        return self.adb('exec-out', 'run-as', self.package, 'cat', path).stdout if self.exists(path) else None

    def write(self, path, data: bytes):
        self.adb('exec-in', 'run-as', self.package, 'sh', '-c', f'cat > {path}', data=data)
        if self.read(path) != data:
            raise RuntimeError(f'write mismatch {path}')

    def remove(self, path):
        self.adb('shell', 'run-as', self.package, 'rm', '-f', path)

    def pid(self):
        return self.adb('shell', 'pidof', self.package, check=False).stdout.decode().strip()

    def awake_and_unlocked(self) -> bool:
        power = self.shell('dumpsys power')
        window = self.shell('dumpsys window')
        awake = 'mWakefulness=Awake' in power
        locked = re.search(r'(?:mShowingLockscreen|isKeyguardShowing|mKeyguardShowing)=true', window) is not None
        return awake and not locked and 'NotificationShade' not in self.focus()

    def saves(self) -> set[str]:
        listing = self.adb('shell', 'run-as', self.package, 'ls', f'{SAVE_ROOT}/savegames', check=False).stdout.decode()
        return set(listing.split())

    def focus(self) -> str:
        text = self.shell('dumpsys window')
        return text.split('mCurrentFocus=', 1)[-1].split('\n', 1)[0] if 'mCurrentFocus=' in text else ''


def sample(device: Device) -> dict:
    """One host-side sample: thermal status and temperatures, app PSS, battery."""
    out = {'utc': utc()}
    thermal = device.shell('dumpsys thermalservice')
    status = re.search(r'Thermal Status: (\d+)', thermal)
    out['thermal_status'] = int(status.group(1)) if status else None
    temps = {}
    current = thermal.split('Current temperatures from HAL:', 1)[-1].split('Current cooling devices', 1)[0]
    for value, kind, name in re.findall(r'mValue=([-0-9.]+), mType=(\d+), mName=([^,]+),', current):
        temps[name] = float(value)
    out['temperatures_c'] = temps
    meminfo = device.shell(f'dumpsys meminfo {device.package}')
    pss = re.search(r'TOTAL PSS:\s+(\d+)', meminfo) or re.search(r'(?m)^\s+TOTAL\s+(\d+)', meminfo)
    out['pss_kib'] = int(pss.group(1)) if pss else None
    battery = device.shell('dumpsys battery')
    for key in ('level', 'temperature'):
        m = re.search(rf'(?m)^\s+{key}: (\d+)', battery)
        out['battery_' + key] = int(m.group(1)) if m else None
    if out.get('battery_temperature') is not None:
        out['battery_temperature'] = out['battery_temperature'] / 10.0
    return out


def frame_summaries(log: str) -> dict:
    """The engine's own profileFrameWork summary after each stage marker."""
    result = {}
    for block in log.split('AND02_STAGE ')[1:]:
        name = block.split()[0]
        m = re.search(r'Frame work phase=total samples=(\d+) mean_ms=([0-9.]+) p95_ms=([0-9.]+) p99_ms=([0-9.]+) max_ms=([0-9.]+)', block)
        if m:
            samples, mean, p95, p99, peak = int(m.group(1)), *map(float, m.groups()[1:])
            result[name] = {'samples': samples, 'mean_ms': mean, 'fps_mean': 1000.0 / mean if mean else None,
                            'p95_ms': p95, 'p99_ms': p99, 'max_ms': peak}
    return result


def audio_residency(log: str) -> list[dict]:
    out = []
    for block in re.split(r'(?m)^\s*\d+ total sounds\s*$', log)[1:]:
        entry = {}
        for key, pattern in (('samples_loaded', r'(\d+) total samples loaded'), ('ogg_kib', r'(\d+) kB OGG samples loaded'),
                             ('pcm_kib', r'(\d+) kB PCM samples loaded'), ('total_kib', r'(\d+) kB total sample data'),
                             ('cpu_kib', r'(\d+) kB held in CPU memory'), ('openal_kib', r'\((\d+) kB released to OpenAL\)'),
                             ('streamed_kib', r'(\d+) kB decoded track size streamed'),
                             ('process_private_kib', r'Process memory: private=(\d+) KiB'),
                             ('process_resident_kib', r'resident=(\d+) KiB')):
            m = re.search(pattern, block[:2000])
            if m:
                entry[key] = int(m.group(1))
        if entry:
            out.append(entry)
    return out


def engine_facts(log: str) -> dict:
    facts = {}
    for key, pattern in (('gl_vendor', r'GL_VENDOR: ([^\n]*)'), ('gl_renderer', r'GL_RENDERER: ([^\n]*)'),
                         ('gl_version', r'GL_VERSION: ([^\n]*)'), ('gl_max_texture', r'GL_MAX_TEXTURE_SIZE: (\d+)'),
                         ('texture_compression', r'\.\.\.using ([^\n]*texture compression[^\n]*)'),
                         ('openal_vendor', r'OpenAL vendor: ([^\n]*)'), ('openal_renderer', r'OpenAL renderer: ([^\n]*)'),
                         ('openal_version', r'OpenAL version: ([^\n]*)'), ('openal_device', r'OpenAL active device: ([^\n]*)'),
                         ('openal_attributes', r'OpenAL device attributes: ([^\n]*)'),
                         ('openal_output_mode', r'OpenAL output mode active: ([^\n]*)'), ('openal_hrtf', r'OpenAL HRTF status: ([^\n]*)'),
                         ('renderer_path', r'Requested renderer path: ([^\n]*)'), ('mode', r'MODE: ([^\n]*)')):
        m = re.search(pattern, log)
        if m:
            facts[key] = m.group(1).strip()
    # In load order: a save restored on the same map loads it again.
    facts['loads'] = [{'map': m, 'ms': int(ms)} for ms, m in re.findall(r'(\d+) msec to load (\S+)', log) if '/' in m]
    facts['low_memory_events'] = len(re.findall(r'low-memory event received', log))
    facts['audio_trims'] = re.findall(r'AUDIO_TRIM [^\n]*', log)
    facts['warnings'] = len(re.findall(r'(?m)^\^3WARNING', log))
    facts['fatal'] = bool(re.search(r'(?:^|\n)(?:ERROR:|FATAL:|Fatal)', log))
    return facts


def run_phase(device: Device, phase: str, out: Path, report: dict):
    args = device.args
    scripts = {'campaign': campaign_scripts, 'sustain': lambda: sustain_scripts(args.sustain_map, args.sustain_minutes),
               'mp': lambda: mp_scripts(args.bots)}[phase]()
    phase_out = out / phase
    phase_out.mkdir()
    created = {}
    preserved = {}
    cache_moved = False
    samples = []
    stop = threading.Event()
    result = {'phase': phase, 'started_utc': utc()}
    try:
        for name in ('openQ4Config.cfg', 'logs/openq4.log'):
            preserved[name] = device.read(f'{SAVE_ROOT}/{name}')
            if preserved[name] is not None:
                (phase_out / ('before-' + name.replace('/', '-'))).write_bytes(preserved[name])
        for name in scripts:
            if device.exists(f'{SAVE_ROOT}/{name}'):
                raise RuntimeError(f'existing file would be overwritten: {name}')
        # Setup cannot run its data check behind the lock screen, and nothing here may wake it.
        if not device.awake_and_unlocked():
            raise RuntimeError('the device is asleep or locked: unlock it and keep the screen on, then rerun')
        saves_before = device.saves()
        result['saves_before'] = sorted(saves_before)
        if phase == 'campaign' and args.cold:
            if device.exists('cache.and02-preserved'):
                raise RuntimeError('a preserved cache from an earlier run exists; restore it first')
            device.adb('shell', 'run-as', device.package, 'mv', 'cache', 'cache.and02-preserved')
            device.adb('shell', 'run-as', device.package, 'mkdir', 'cache')
            cache_moved = True
            result['cache'] = 'cold (generated cache moved aside)'
        if preserved['logs/openq4.log'] is not None:
            device.remove(f'{SAVE_ROOT}/logs/openq4.log')
        for name, text in scripts.items():
            created[f'{SAVE_ROOT}/{name}'] = text.encode()
            device.write(f'{SAVE_ROOT}/{name}', text.encode())
        device.adb('shell', 'am', 'start', '-W', '-n', f'{device.package}/com.darkmatter.openq4.LauncherActivity',
                   '--es', 'diagnostic_launch', 'mp' if phase == 'mp' else 'sp')
        result['storage_before'] = storage(device)

        def sampler():
            while not stop.is_set():
                try:
                    samples.append(sample(device))
                except Exception as error:  # a failed sample must not end the run
                    samples.append({'utc': utc(), 'error': str(error)})
                stop.wait(15)
        thread = threading.Thread(target=sampler, daemon=True)
        thread.start()
        deadline = time.monotonic() + args.timeout
        seen_game, trimmed, log = False, False, b''
        launch_deadline = time.monotonic() + 300
        while time.monotonic() < deadline:
            focus = device.focus()
            seen_game = seen_game or 'OpenQ4Activity' in focus
            if not seen_game and time.monotonic() > launch_deadline:
                raise RuntimeError(f'the game did not start within 5 minutes (focus: {focus.strip()[:120]})')
            log = device.read(f'{SAVE_ROOT}/logs/openq4.log') or log
            if re.search(rb'(?:^|\n)(?:ERROR:|FATAL:|Fatal)', log):
                raise RuntimeError('engine fatal error')
            if b'AND02_TRIM_REQUEST' in log and not trimmed:
                # An operating-system memory-trim request, as Android sends under pressure.
                trimmed = True
                result['trim_request'] = device.shell(f'am send-trim-memory {device.package} RUNNING_CRITICAL').strip()
                result['trim_request_utc'] = utc()
            if b'AND02_COMPLETE' in log and seen_game and 'OpenQ4Activity' not in focus:
                break
            time.sleep(3)
        else:
            raise RuntimeError(f'{phase} did not complete within {args.timeout} s (game seen: {seen_game})')
        time.sleep(2)
        log = device.read(f'{SAVE_ROOT}/logs/openq4.log') or log
        (phase_out / 'openq4.log').write_bytes(log)
        result['storage_after'] = storage(device)
        text = log.decode(errors='replace')
        result['engine'] = engine_facts(text)
        result['frames'] = frame_summaries(text)
        result['audio_residency'] = audio_residency(text)
        if phase == 'campaign':
            saved = text.split('AND02_STAGE_DONE process1', 1)[-1]
            poses = re.findall(r'origin: \(([^)]+)\) angles: \(([^)]+)\)', saved)
            if len(poses) >= 2:
                before = [float(v) for g in poses[0] for v in g.split()]
                after = [float(v) for g in poses[-1] for v in g.split()]
                result['save_restore_max_delta'] = max(abs(a - b) for a, b in zip(before, after))
            result['transitions'] = re.findall(r'AND02_STAGE (\S+)', text)
        result['status'] = 'complete'
    except Exception as error:
        result['status'] = 'fail'
        result['error'] = str(error)
    finally:
        stop.set()
        result['samples'] = samples
        if device.pid():
            device.adb('shell', 'am', 'force-stop', device.package)
        if device.awake_and_unlocked():
            # Keep the screen on while this phase is collected, so the next one can start.
            device.adb('shell', 'am', 'start', '-n', f'{device.package}/com.darkmatter.openq4.LauncherActivity',
                       '--es', 'diagnostic_launch', 'hold', check=False)
        log = device.read(f'{SAVE_ROOT}/logs/openq4.log')
        if log is not None and not (phase_out / 'openq4.log').exists():
            (phase_out / 'openq4.log').write_bytes(log)
        for path, data in created.items():
            if device.read(path) == data:
                device.remove(path)
        # Saves this run created (the qualification slot and the campaign's own
        # transition autosaves) are evidence; the player's earlier saves are untouched.
        if 'saves_before' in result:
            for name in sorted(device.saves() - set(result['saves_before'])):
                data = device.read(f'{SAVE_ROOT}/savegames/{name}')
                if data is not None:
                    (phase_out / name).write_bytes(data)
                device.remove(f'{SAVE_ROOT}/savegames/{name}')
        for directory, pattern in (('screenshots', r'and02-[a-z0-9_]+\.tga'), ('diagnostics', r'and02-[a-z0-9_]+\.csv')):
            listing = device.adb('shell', 'run-as', device.package, 'ls', f'{SAVE_ROOT}/{directory}', check=False).stdout.decode()
            for name in listing.split():
                if re.fullmatch(pattern, name):
                    data = device.read(f'{SAVE_ROOT}/{directory}/{name}')
                    if data is not None:
                        (phase_out / name).write_bytes(data)
                    device.remove(f'{SAVE_ROOT}/{directory}/{name}')
        if preserved.get('openQ4Config.cfg') is not None:
            device.write(f'{SAVE_ROOT}/openQ4Config.cfg', preserved['openQ4Config.cfg'])
        if preserved.get('logs/openq4.log') is not None:
            device.write(f'{SAVE_ROOT}/logs/openq4.log', preserved['logs/openq4.log'])
        if cache_moved:
            result['cold_cache_kib'] = du(device, 'cache')
            device.adb('shell', 'run-as', device.package, 'rm', '-rf', 'cache')
            device.adb('shell', 'run-as', device.package, 'mv', 'cache.and02-preserved', 'cache')
        result['finished_utc'] = utc()
        (phase_out / 'result.json').write_text(json.dumps(result, indent=2))
        report['phases'][phase] = {k: v for k, v in result.items() if k != 'samples'}
        report['phases'][phase]['sample_summary'] = summarize_samples(samples)


def du(device: Device, path: str) -> int | None:
    text = device.adb('shell', 'run-as', device.package, 'du', '-sk', path, check=False).stdout.decode()
    m = re.match(r'(\d+)', text.strip())
    return int(m.group(1)) if m else None


def storage(device: Device) -> dict:
    apk = device.shell(f'pm path {device.package}').strip().split(':', 1)[-1]
    size = re.search(r'(\d+)', device.shell(f'stat -c %s {apk}')) if apk else None
    external = device.shell(f'du -sk /sdcard/Android/data/{device.package}/files')
    free = device.shell('df -k /data').strip().splitlines()[-1].split() if device.shell('df -k /data').strip() else []
    return {'apk_kib': int(size.group(1)) // 1024 if size else None, 'private_files_kib': du(device, 'files'),
            'cache_kib': du(device, 'cache'),
            'external_files_kib': int(external.split()[0]) if external.strip()[:1].isdigit() else None,
            'data_free_kib': int(free[3]) if len(free) >= 4 and free[3].isdigit() else None}


def summarize_samples(samples: list[dict]) -> dict:
    good = [s for s in samples if 'error' not in s]
    if not good:
        return {}
    temps = {}
    for s in good:
        for name, value in s.get('temperatures_c', {}).items():
            temps.setdefault(name, []).append(value)
    pss = [s['pss_kib'] for s in good if s.get('pss_kib')]
    status = [s['thermal_status'] for s in good if s.get('thermal_status') is not None]
    return {'count': len(good), 'thermal_status_max': max(status) if status else None,
            'temperature_max_c': {k: max(v) for k, v in sorted(temps.items())},
            'temperature_first_c': {k: v[0] for k, v in sorted(temps.items())},
            'pss_peak_kib': max(pss) if pss else None, 'pss_last_kib': pss[-1] if pss else None,
            'battery_temperature_max_c': max((s['battery_temperature'] for s in good if s.get('battery_temperature') is not None), default=None),
            'battery_level_first': good[0].get('battery_level'), 'battery_level_last': good[-1].get('battery_level')}


def device_facts(device: Device) -> dict:
    facts = {}
    for prop in ('ro.product.model', 'ro.product.manufacturer', 'ro.build.version.release', 'ro.build.version.sdk',
                 'ro.build.fingerprint', 'ro.soc.manufacturer', 'ro.soc.model', 'ro.board.platform', 'ro.hardware.egl',
                 'ro.hardware.vulkan', 'ro.opengles.version', 'ro.product.cpu.abi', 'ro.kernel.qemu'):
        facts[prop] = device.shell(f'getprop {prop}').strip()
    gles = re.search(r'GLES: ([^\n]*)', device.shell('dumpsys SurfaceFlinger'))
    facts['surfaceflinger_gles'] = gles.group(1).strip() if gles else None
    mem = re.search(r'MemTotal:\s+(\d+)', device.shell('cat /proc/meminfo'))
    facts['ram_kib'] = int(mem.group(1)) if mem else None
    facts['page_size'] = device.shell('getconf PAGE_SIZE').strip()
    facts['display'] = {'size': device.shell('wm size').strip(), 'density': device.shell('wm density').strip()}
    version = re.search(r'versionName=(\S+)', device.shell(f'dumpsys package {device.package}'))
    facts['package_version'] = version.group(1) if version else None
    return facts


def check_budgets(report: dict) -> list[str]:
    """Compare against docs/dev/android-device-budgets.json for this device model, if it has one."""
    if not BUDGETS.is_file():
        return ['no budget file']
    budgets = json.loads(BUDGETS.read_text(encoding='utf-8'))
    model = report['device'].get('ro.product.model')
    entry = budgets.get('devices', {}).get(model)
    if not entry:
        return [f'no budgets for {model}']
    failures = []
    for phase, limits in entry.get('phases', {}).items():
        got = report['phases'].get(phase)
        if not got or got.get('status') != 'complete':
            continue
        for stage, limit in limits.get('frames', {}).items():
            frame = got.get('frames', {}).get(stage)
            if frame and frame['p99_ms'] > limit['p99_ms']:
                failures.append(f'{phase}/{stage}: p99 {frame["p99_ms"]:.1f} ms > {limit["p99_ms"]} ms')
        summary = got.get('sample_summary', {})
        if 'pss_peak_kib' in limits and summary.get('pss_peak_kib') and summary['pss_peak_kib'] > limits['pss_peak_kib']:
            failures.append(f'{phase}: PSS peak {summary["pss_peak_kib"]} KiB > {limits["pss_peak_kib"]} KiB')
        if 'thermal_status_max' in limits and (summary.get('thermal_status_max') or 0) > limits['thermal_status_max']:
            failures.append(f'{phase}: thermal status {summary["thermal_status_max"]} > {limits["thermal_status_max"]}')
        for stage, limit in limits.get('load_ms', {}).items():
            loads = [load['ms'] for load in got.get('engine', {}).get('loads', []) if load['map'] == stage]
            if loads and loads[0] > limit:
                failures.append(f'{phase}: {stage} loaded in {loads[0]} ms > {limit} ms')
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--adb', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', default='com.darkmatter.openq4')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--phases', default='campaign,sustain,mp')
    parser.add_argument('--apk', type=Path, help='install this debuggable APK first (adb install -r -d; data kept)')
    parser.add_argument('--cold', action='store_true', help='campaign: move the generated image cache aside first')
    parser.add_argument('--sustain-minutes', type=int, default=10)
    parser.add_argument('--sustain-map', default='game/airdefense1')
    parser.add_argument('--bots', type=int, default=3)
    parser.add_argument('--timeout', type=int, default=2400)
    parser.add_argument('--check-budgets', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('output must stay under this checkout\'s .tmp')
    phases = [p for p in args.phases.split(',') if p]
    if any(p not in ('campaign', 'sustain', 'mp') for p in phases):
        parser.error('phases: campaign, sustain, mp')
    out.mkdir(parents=True, exist_ok=False)
    (out / 'runner.py').write_bytes(Path(__file__).read_bytes())
    device = Device(args)
    if device.pid():
        parser.error('the app is running on the device; close it first')
    report = {'started_utc': utc(), 'serial': args.serial, 'phases': {},
              'input': 'engine scripts and activity-manager intents; no OS input'}
    if args.apk:
        report['apk_sha256'] = hashlib.sha256(args.apk.read_bytes()).hexdigest()
        r = device.adb('install', '-r', '-d', str(args.apk), check=False, timeout=900)
        if b'Success' not in r.stdout:
            raise SystemExit('install failed: ' + (r.stdout + r.stderr).decode(errors='replace'))
    report['device'] = device_facts(device)
    try:
        for phase in phases:
            print(f'phase {phase}...', flush=True)
            run_phase(device, phase, out, report)
            print(f'phase {phase}: {report["phases"][phase]["status"]}', flush=True)
        if args.check_budgets:
            report['budget_failures'] = check_budgets(report)
    finally:
        if device.pid():
            device.adb('shell', 'am', 'force-stop', device.package)	# ends the screen hold
        report['finished_utc'] = utc()
        (out / 'report.json').write_text(json.dumps(report, indent=2))
    failed = [p for p, r in report['phases'].items() if r['status'] != 'complete'] + report.get('budget_failures', [])
    print('FAIL: ' + '; '.join(failed) if failed else 'PASS: ' + ', '.join(phases))
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main()
