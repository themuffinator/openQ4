#!/usr/bin/env python3
"""Visible, windowed Vulkan pacing and resource profiles (VK-02/VK-06).

Requires permission to show a foreground game window. Never injects input or
captures the desktop. Windows foreground sampling records only whether the
game owns focus. The selected rendering GPU does not prove the display route.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

import renderer_gameplay_benchmark as gameplay
import renderer_budget_contract as timing_contract
import renderer_validation_matrix as matrix
import renderer_vulkan_qualification as qualification
from mp_round_remote_smoke import verify_runtime_is_current

PRESENTED = re.compile(r'Presented frames vkpacing: frames=(\d+) interval '
    r'p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+) ms; queue '
    r'p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+) ms; replaced=(\d+) dropped=(\d+)')


def foreground(pid):
    if os.name != 'nt':
        return None
    user = ctypes.windll.user32
    user.GetForegroundWindow.restype = ctypes.c_void_p
    user.GetWindowThreadProcessId.argtypes = (ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong))
    owner = ctypes.c_ulong()
    window = user.GetForegroundWindow()
    if not window or not user.GetWindowThreadProcessId(window, ctypes.byref(owner)):
        return None
    return owner.value == pid


def power_state():
    if os.name != 'nt':
        return None
    class Status(ctypes.Structure):
        _fields_ = [('ac', ctypes.c_ubyte), ('flags', ctypes.c_ubyte), ('percent', ctypes.c_ubyte),
                    ('saver', ctypes.c_ubyte), ('seconds', ctypes.c_ulong), ('fullSeconds', ctypes.c_ulong)]
    status = Status()
    if not ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(status)):
        return None
    return {name: getattr(status, name) for name, _ in status._fields_}


def other_games(own_pid):
    """Observe competing openQ4 clients without controlling their processes."""
    if os.name != 'nt':
        return []
    from ctypes import wintypes
    class Entry(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('usage', wintypes.DWORD), ('pid', wintypes.DWORD),
                    ('heap', ctypes.c_size_t), ('module', wintypes.DWORD), ('threads', wintypes.DWORD),
                    ('parent', wintypes.DWORD), ('priority', wintypes.LONG), ('flags', wintypes.DWORD),
                    ('name', wintypes.WCHAR * 260)]
    kernel = ctypes.windll.kernel32
    kernel.CreateToolhelp32Snapshot.argtypes = (wintypes.DWORD, wintypes.DWORD)
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Process32FirstW.argtypes = kernel.Process32NextW.argtypes = (wintypes.HANDLE, ctypes.POINTER(Entry))
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    handle = kernel.CreateToolhelp32Snapshot(2, 0)  # TH32CS_SNAPPROCESS
    if handle == ctypes.c_void_p(-1).value:
        raise OSError('cannot observe concurrent game processes')
    found = []
    try:
        entry = Entry()
        entry.size = ctypes.sizeof(entry)
        more = kernel.Process32FirstW(handle, ctypes.byref(entry))
        if not more:
            raise OSError('cannot enumerate concurrent game processes')
        while more:
            if entry.pid != own_pid and entry.name.lower().startswith('openq4-client'):
                found.append(dict(pid=entry.pid, name=entry.name))
            more = kernel.Process32NextW(handle, ctypes.byref(entry))
    finally:
        kernel.CloseHandle(handle)
    return found


def analyze(text, focus, budget, competing=()):
    failures = []
    match = PRESENTED.search(text)
    presentation = None
    if match:
        values = list(map(float, match.groups()))
        presentation = dict(frames=int(values[0]), interval=dict(zip(('p50', 'p95', 'p99', 'max'), values[1:5])),
                            queue=dict(zip(('p50', 'p95', 'p99', 'max'), values[5:9])),
                            replaced=int(values[9]), dropped=int(values[10]))
        if presentation['frames'] < 120 or presentation['dropped']:
            failures.append('too few presented samples or timing samples were dropped')
        if presentation['interval']['p99'] > budget:
            failures.append('presented p99 exceeds the declared budget')
    else:
        failures.append('presented timing unavailable or missing')
    if len(focus) < 10 or any(value is not True for value in focus):
        failures.append('foreground ownership was not continuously observed during sampling')
    if competing:
        failures.append('other openQ4 clients were observed during sampling')
    if 'hidden window renders offscreen' in text:
        failures.append('offscreen rendering cannot qualify presentation')
    for marker in ('VK_PACING_BEGIN', 'VK_PACING_END'):
        if marker not in text:
            failures.append('missing ' + marker)
    poses = []
    for marker in ('VK_PACING_BEGIN', 'VK_PACING_END'):
        pose = re.search(r'origin: \(([^()]+)\) angles: \(([^()]+)\)', text.split(marker, 1)[-1])
        try:
            values = [float(value) for group in pose.groups() for value in group.split()] if pose else []
            if len(values) != 6 or not all(math.isfinite(value) for value in values):
                raise ValueError('missing or malformed pose')
            if any(abs(value - expected) > 1 for value, expected in zip(values, (10200, -6800, 40, 0, 170, 0))):
                raise ValueError('camera left the declared static view')
            poses.append(values)
        except ValueError as error:
            failures.append(marker + ': ' + str(error))
    if 'active=vulkan' not in text:
        failures.append('Vulkan backend was not active')
    if 'Vulkan validation:' in text or re.search(r'(?m)^(?:ERROR|FATAL):', text):
        failures.append('engine/API error during the run')
    resource_lines = [line for line in text.splitlines() if line.startswith('Vulkan executor resources:')]
    resources = dict(re.findall(r'(\w+)=(\d+)', resource_lines[-1])) if resource_lines else {}
    if resources.get('enabled') != '1' or int(resources.get('frames', '0')) == 0:
        failures.append('executor resource profile is missing or disabled')
    timing, timing_failures = timing_contract.parse_timing_evidence([text])
    failures.extend(timing_failures)
    if timing:
        measurement = timing['measurement']
        if (measurement['map'], measurement['backend'], measurement['profile']) != ('game/airdefense1', 'vulkan', 'baseline'):
            failures.append('CPU/GPU timing does not match the declared map/backend/profile')
        if not measurement['gpu']['available'] or min(measurement[k]['samples'] for k in ('cpu', 'gpu')) < 120:
            failures.append('CPU/GPU timing has unavailable or insufficient samples')
    return dict(status='pass' if not failures else 'fail', failures=failures, presentation=presentation,
                executorResources=resources, engine=gameplay.extract_summary(text), foregroundSamples=len(focus),
                foregroundObservations={name: sum(value is state for value in focus)
                    for name, state in (('focused', True), ('unfocused', False), ('unavailable', None))},
                timing=timing, memoryReports=[line for line in text.splitlines() if line.startswith('Vulkan memory:')],
                pipelineReports=[line for line in text.splitlines() if line.startswith('Vulkan pipelines:')],
                cameraEndpoints=poses)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-dir', type=Path, default=qualification.ROOT / '.install')
    parser.add_argument('--basepath', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--devices', type=int, nargs='+', default=[-1])
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--seconds', type=int, default=20)
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--p99-budget-ms', type=float, default=28)
    args = parser.parse_args()
    if args.self_test:
        pose = 'origin: (10200 -6800 40) angles: (0 170 0)\n'
        text = ('Renderer API: requested=vulkan active=vulkan\nVK_PACING_BEGIN\n' + pose + 'VK_PACING_END\n' + pose +
            'Presented frames vkpacing: frames=500 interval p50=8 p95=9 p99=10 max=12 ms; '
            'queue p50=1 p95=2 p99=3 max=4 ms; replaced=5 dropped=0\n'
            'Vulkan executor resources: enabled=1 frames=500 imageHits=1000 imageUpdates=5\n'
            'OPENQ4_FRAME_TIMING_V1 map=game/airdefense1 backend=vulkan profile=baseline '
            'cpuSamples=256 cpuP50Us=3000 cpuP95Us=4000 cpuP99Us=5000 '
            'gpuAvailable=1 gpuSamples=256 gpuP50Us=1000 gpuP95Us=1500 gpuP99Us=2000\n')
        assert analyze(text, [True] * 100, 28)['status'] == 'pass'
        assert analyze(text, [True] * 100, 9)['status'] == 'fail'
        assert analyze(text, [True] * 99 + [False], 28)['status'] == 'fail'
        assert analyze(text, [None] * 100, 28)['status'] == 'fail'
        assert analyze(text.replace('dropped=0', 'dropped=1'), [True] * 100, 28)['status'] == 'fail'
        assert analyze(text + 'hidden window renders offscreen', [True] * 100, 28)['status'] == 'fail'
        assert analyze('', [True] * 100, 28)['status'] == 'fail'
        assert analyze(text, [True] * 100, 28, [{'pid': 1}])['status'] == 'fail'
        assert analyze(text.replace('10200', '10100'), [True] * 100, 28)['status'] == 'fail'
        assert analyze(text.replace('profile=baseline', 'profile=modern'), [True] * 100, 28)['status'] == 'fail'
        assert analyze(text.replace('gpuSamples=256', 'gpuSamples=119'), [True] * 100, 28)['status'] == 'fail'
        assert analyze(text.replace('gpuAvailable=1 gpuSamples=256 gpuP50Us=1000 gpuP95Us=1500 gpuP99Us=2000',
            'gpuAvailable=0 gpuSamples=0 gpuP50Us=-1 gpuP95Us=-1 gpuP99Us=-1'), [True] * 100, 28)['status'] == 'fail'
        print('Visible presentation parser: 12 acceptance/refusal controls passed')
        return 0
    if args.basepath is None or args.output_dir is None:
        parser.error('--basepath and --output-dir are required for runtime tests')
    if args.repeats < 1 or args.seconds < 10 or args.p99_budget_ms <= 0:
        parser.error('positive repeats/budget and at least ten sampled seconds are required')
    if any(device < -1 or device > 255 for device in args.devices):
        parser.error('device indices must be -1 or 0 through 255')
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    if runtime == output or runtime in output.parents:
        parser.error('evidence must be outside the runtime')
    verify_runtime_is_current(runtime)
    output.mkdir(parents=True, exist_ok=False)
    sources = (Path(__file__), Path(gameplay.__file__), Path(matrix.__file__), Path(qualification.__file__),
               Path(timing_contract.__file__))
    harnesses = {source.name: qualification.sha256_file(source) for source in sources}
    (output / 'harness').mkdir()
    for source in sources:
        shutil.copy2(source, output / 'harness' / source.name)
    before = qualification.runtime_inventory(runtime)
    results = []
    for device in args.devices:
        for repeat in range(args.repeats):
            name = f'device-{device}-repeat-{repeat + 1}'
            save = output / name
            game = save / 'baseoq4'
            game.mkdir(parents=True)
            script = ['waitMsec 4000', 'god', 'notarget', 'noclip', 'setviewpos 10200 -6800 40 0 170 0',
                'waitMsec 3000', 'gfxInfo', 'rendererDisplayProbe report',
                'r_vkResourceProfile 1', 'rendererVulkanMemoryInfo reset',
                'r_rendererMetrics 1', 'r_rendererGpuTimers 1', 'framePacingReset', 'rendererPresentTiming reset',
                'echo VK_PACING_BEGIN', 'viewpos', f'waitMsec {args.seconds * 1000}', 'echo VK_PACING_END', 'viewpos',
                'rendererPresentTiming vkpacing', 'framePacingSnapshot vkpacing',
                'rendererBenchmarkCapture "logs/timing.csv"',
                'r_rendererMetrics 0', 'rendererVulkanMemoryInfo', 'rendererDisplayProbe report',
                'screenshot "screenshots/pacing.tga"', 'wait 5', 'quit']
            (game / 'vkpacing.cfg').write_text('\n'.join(script) + '\n')
            cvars = dict(com_performancePreset='balanced', r_rendererBenchmarkPreset='baseline',
                r_renderApi='vulkan', r_vkDevice=str(device), r_vkPresentTiming='1', r_vkValidation='0',
                r_hiddenWindow='0', r_fullscreen='0', r_borderless='0', r_borderlessDefaultMigrated='1',
                r_mode='-1', r_customWidth='1280', r_customHeight='720', r_windowWidth='1280', r_windowHeight='720',
                r_multiSamples='0', r_postAA='0', r_hdrToneMap='0', r_pbrMaterials='0', r_temporalAA='0',
                r_bloom='0', r_ssao='0', r_motionBlur='0', r_celShading='0', r_celShadingWorld='0',
                r_shadows='1', r_useShadowMap='0',
                r_screenFraction='100', r_swapInterval='0', com_maxfps='120', in_mouse='0', in_joystick='0',
                in_nograb='1', s_noSound='1', ui_retained='1', com_skipLoadingContinue='1', g_autoSkipCinematics='1',
                g_autoExecAfterMapLoad='vkpacing.cfg', g_autoExecAfterMapLoadDelayMs='500')
            command = [str(matrix.find_client_executable(runtime)),
                *matrix.common_args(runtime, name, str(args.basepath.resolve()), save, False)]
            for key, value in cvars.items():
                command += ['+set', key, value]
            command += ['+map', 'game/airdefense1']
            if sum(argument.startswith('+') for argument in command) > matrix.ENGINE_MAX_STARTUP_COMMANDS:
                raise ValueError('visible presentation exceeds the engine startup command limit')
            (save / 'command.json').write_text(json.dumps(command, indent=2))
            log = game / 'logs' / ('openq4_validation_' + matrix.sanitize_case_id(name) + '.log')
            start, focus, timed_out, sampled = time.monotonic(), [], False, False
            competing, next_process_check = {}, 0
            initial_power = power_state()
            offset, pending = 0, ''
            with (save / 'process.log').open('w') as stream:
                process = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT)
                try:
                    while process.poll() is None:
                        if log.is_file():
                            with log.open(errors='replace') as engine:
                                engine.seek(offset)
                                chunk = pending + engine.read()
                                offset = engine.tell()
                            lines = chunk.split('\n')
                            pending = lines.pop()
                            for line in lines:
                                line = qualification.strip_colors(line).strip()
                                if line == 'VK_PACING_BEGIN': sampled = True
                                if line == 'VK_PACING_END': sampled = False
                        if sampled:
                            focus.append(foreground(process.pid))
                            if time.monotonic() >= next_process_check:
                                for competitor in other_games(process.pid):
                                    competing[competitor['pid']] = competitor
                                next_process_check = time.monotonic() + 1
                        if time.monotonic() - start > args.timeout:
                            timed_out = True
                            break
                        time.sleep(0.1)
                finally:
                    if process.poll() is None:
                        process.kill()
                    code = process.wait()
            text = qualification.strip_colors(log.read_text(errors='replace')) if log.is_file() else ''
            result = analyze(text, focus, args.p99_budget_ms, competing)
            if code or timed_out:
                result['failures'].append('process failed or timed out')
            capture = game / 'screenshots/pacing.tga'
            try:
                if not qualification.screenshot_is_drawn(qualification.screenshot_stats(capture)):
                    result['failures'].append('blank engine capture')
            except (OSError, ValueError) as error:
                result['failures'].append('missing or invalid engine capture: ' + str(error))
            trace = game / 'logs/timing.csv'
            config = game / 'openQ4Config.cfg'
            if not trace.is_file() or trace.stat().st_size == 0:
                result['failures'].append('CPU/GPU phase trace was not written')
            if not config.is_file() or 'seta com_performancePreset "balanced"' not in config.read_text(errors='replace'):
                result['failures'].append('saved performance preset differs from the declared balanced profile')
            result.update(id=name, command=command, exitCode=code, timedOut=timed_out, powerBefore=initial_power,
                powerAfter=power_state(), selectedDevice=qualification.device_identity('vulkan', qualification.parse_engine_log(text)),
                engineVersion=qualification.parse_engine_log(text)['version'],
                otherGameProcesses=list(competing.values()),
                displayAdapterRoute='unverified', elapsedSeconds=time.monotonic() - start,
                artifacts=[qualification.file_record(path, output) for path in
                           (log, capture, trace, config, save / 'process.log', save / 'command.json') if path.is_file()])
            result['status'] = 'fail' if result['failures'] else 'pass'
            results.append(result)
            (save / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
            print(name + ': ' + result['status'], flush=True)
    unchanged = before == qualification.runtime_inventory(runtime)
    harnesses_unchanged = harnesses == {source.name: qualification.sha256_file(source) for source in sources}
    report = dict(kind='vulkan-visible-presentation', runtimeUnchanged=unchanged, runtime=before,
        harnessSources=harnesses, harnessUnchanged=harnesses_unchanged, p99BudgetMs=args.p99_budget_ms,
        supportPromotion=False, results=results,
        status='pass' if unchanged and harnesses_unchanged and all(result['status'] == 'pass' for result in results) else 'fail')
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(report['status'] != 'pass')


if __name__ == '__main__':
    raise SystemExit(main())
