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
import renderer_foreground_observer as foreground_observer
import renderer_presentmon as presentmon
from mp_round_remote_smoke import verify_runtime_is_current

PRESENTED = re.compile(r'Presented frames vkpacing: frames=(\d+) interval '
    r'p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+) ms; queue '
    r'p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+) ms; replaced=(\d+) dropped=(\d+)')


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


def competing_workloads(own_pid):
    """Observe game and build contention without controlling other processes."""
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
            name = entry.name.lower()
            game = name.startswith(('openq4-client', 'openq4-ded'))
            build = name in {'ninja.exe', 'msbuild.exe', 'cl.exe', 'link.exe', 'clang-cl.exe', 'clang++.exe'}
            if entry.pid != own_pid and (game or build):
                found.append(dict(pid=entry.pid, name=entry.name))
            more = kernel.Process32NextW(handle, ctypes.byref(entry))
    finally:
        kernel.CloseHandle(handle)
    return found


def analyze(text, focus, budget, competing=(), focus_events=(), require_presented=True):
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
    elif require_presented:
        failures.append('presented timing unavailable or missing')
    if len(focus) < 10 or any(value is not True for value in focus):
        failures.append('foreground ownership was not continuously observed during sampling')
    if any(event['focused'] is not True for event in focus_events):
        failures.append('a foreground event interrupted the sampling interval')
    if competing:
        failures.append('other openQ4 or compiler/build workloads were observed during sampling')
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


def replace_gate(target, content):
    pending = target.with_suffix('.pending')
    pending.write_text(content)
    for attempt in range(250):
        try:
            pending.replace(target)
            return
        except PermissionError:
            time.sleep(0.02)
    raise OSError('engine command gate remained locked: ' + str(target))


def await_idle(seconds, timeout):
    start = time.monotonic()
    readiness = foreground_observer.Readiness(seconds)
    seen = {}
    while True:
        now = time.monotonic()
        competitors = competing_workloads(0)
        for item in competitors:
            seen[item['pid']] = item
        if readiness.update(now, True, competitors):
            return dict(quietSeconds=seconds, waitedSeconds=now - start, priorProcesses=list(seen.values()))
        if now - start >= timeout:
            raise TimeoutError('competing workloads did not leave a quiet prelaunch interval')
        time.sleep(0.25)


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
    parser.add_argument('--ready-seconds', type=float, default=3)
    parser.add_argument('--idle-seconds', type=float, default=15)
    parser.add_argument('--idle-timeout', type=float, default=600)
    parser.add_argument('--request-focus', action='store_true',
                        help='explicitly request focus through the engine; caller must have permission')
    parser.add_argument('--presentmon', type=Path, help='optional portable PresentMon 2.6 console executable')
    parser.add_argument('--timing-source', choices=('engine', 'presentmon'), default='engine',
                        help='required presentation source; available engine timing must also pass')
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
        without_present = PRESENTED.sub('', text)
        assert analyze(without_present, [True] * 100, 28, require_presented=False)['status'] == 'pass'
        assert analyze(without_present, [False] * 100, 28, require_presented=False)['status'] == 'fail'
        assert analyze(text, [True] * 100, 9, require_presented=False)['status'] == 'fail'
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
        assert analyze(text, [True] * 100, 28, focus_events=[dict(time=1, focused=False)])['status'] == 'fail'
        gate = foreground_observer.Readiness(3)
        assert not gate.update(0, True, []) and not gate.update(2, True, [])
        assert not gate.update(3, True, [], interrupted=True)
        assert not gate.update(4, True, []) and gate.update(7, True, [])
        assert not gate.update(8, True, [dict(pid=1)])
        assert not gate.update(9, None, []) and not gate.update(10, False, [])
        assert not gate.update(11, True, []) and gate.update(14, True, [])
        events = [dict(time=1, focused=False), dict(time=3, focused=True), dict(time=5, focused=False)]
        assert foreground_observer.interval_events(events, 2, 4) == [events[1]]
        assert foreground_observer.interval_events(events, None, 4) == []
        presentmon.self_test()
        identity = foreground_observer.ForegroundObserver.__new__(foreground_observer.ForegroundObserver)
        identity.pid, identity.window = 42, None
        identity.owner = lambda window: {10: 42, 11: 42, 12: 99}.get(window)
        assert identity.owns(10) is None
        identity.bind_window(10)
        assert identity.owns(10) is True and identity.owns(11) is False and identity.owns(12) is False
        assert identity.owns(0) is None
        for window in (0, 11, 12):
            try:
                identity.bind_window(window)
            except ValueError:
                pass
            else:
                raise AssertionError('invalid/replaced render window accepted')
        print('Visible presentation parser/readiness: acceptance, focus-event and interrupted-wait controls passed')
        return 0
    if args.basepath is None or args.output_dir is None:
        parser.error('--basepath and --output-dir are required for runtime tests')
    if args.repeats < 1 or args.seconds < 10 or args.timeout <= 0 or not math.isfinite(args.p99_budget_ms) or args.p99_budget_ms <= 0:
        parser.error('positive repeats/budget and at least ten sampled seconds are required')
    if any(device < -1 or device > 255 for device in args.devices):
        parser.error('device indices must be -1 or 0 through 255')
    if os.name != 'nt':
        parser.error('foreground qualification currently requires the Windows event observer')
    if args.timing_source == 'presentmon' and args.presentmon is None:
        parser.error('--timing-source presentmon requires --presentmon')
    if args.presentmon is not None:
        args.presentmon = args.presentmon.resolve()
        if not args.presentmon.is_file():
            parser.error('--presentmon must name an existing portable console executable')
    if any(not math.isfinite(value) or value <= 0 for value in (args.ready_seconds, args.idle_seconds, args.idle_timeout)):
        parser.error('positive foreground/idle intervals are required')
    runtime, output = args.runtime_dir.resolve(), args.output_dir.resolve()
    if runtime == output or runtime in output.parents:
        parser.error('evidence must be outside the runtime')
    verify_runtime_is_current(runtime)
    output.mkdir(parents=True, exist_ok=False)
    sources = (Path(__file__), Path(gameplay.__file__), Path(matrix.__file__), Path(qualification.__file__),
               Path(timing_contract.__file__), Path(foreground_observer.__file__), Path(presentmon.__file__))
    harnesses = {source.name: qualification.sha256_file(source) for source in sources}
    (output / 'harness').mkdir()
    for source in sources:
        shutil.copy2(source, output / 'harness' / source.name)
    before = qualification.runtime_inventory(runtime)
    presentmon_hash = qualification.sha256_file(args.presentmon) if args.presentmon else None
    results = []
    for device in args.devices:
        for repeat in range(args.repeats):
            idle = await_idle(args.idle_seconds, args.idle_timeout)
            name = f'device-{device}-repeat-{repeat + 1}'
            save = output / name
            game = save / 'baseoq4'
            game.mkdir(parents=True)
            script = ['waitMsec 4000', 'god', 'notarget', 'noclip', 'setviewpos 10200 -6800 40 0 170 0',
                'waitMsec 3000', 'gfxInfo', 'rendererDisplayProbe report']
            if args.request_focus:
                script += ['windowFocusTest']
            else:
                script += ['windowFocusTest report']
            script += ['echo VK_PACING_READY', 'exec vkpacing_gate.cfg']
            (game / 'vkpacing_gate.cfg').write_text('wait 6\nexec vkpacing_gate.cfg\n')
            sampling = ['r_vkResourceProfile 1', 'rendererVulkanMemoryInfo reset',
                'r_rendererMetrics 1', 'r_rendererGpuTimers 1', 'framePacingReset', 'rendererPresentTiming reset',
                'echo VK_PACING_BEGIN', 'viewpos', f'waitMsec {args.seconds * 1000}', 'echo VK_PACING_END', 'viewpos',
                'rendererPresentTiming vkpacing', 'framePacingSnapshot vkpacing',
                'rendererBenchmarkCapture "logs/timing.csv"',
                'r_rendererMetrics 0', 'rendererVulkanMemoryInfo', 'rendererDisplayProbe report',
                'screenshot "screenshots/pacing.tga"', 'waitMsec 1000', 'quit']
            (game / 'vkpacing.cfg').write_text('\n'.join(script) + '\n')
            (game / 'vkpacing_sample.cfg').write_text('\n'.join(sampling) + '\n')
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
            ready, released, sample_end = False, None, None
            trace, qpc_begin, qpc_end = None, None, None
            readiness = foreground_observer.Readiness(args.ready_seconds)
            observer, observer_errors, observations, gate_observations = None, [], [], []
            previous_event_count, active_competitors = 0, []
            with (save / 'process.log').open('w') as stream:
                process = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT)
                try:
                    observer = foreground_observer.ForegroundObserver(process.pid)
                    if args.presentmon:
                        trace = presentmon.Capture(args.presentmon, process.pid, save, args.timeout)
                    while process.poll() is None:
                        observer.pump()
                        now = time.monotonic()
                        if log.is_file():
                            with log.open(errors='replace') as engine:
                                engine.seek(offset)
                                chunk = pending + engine.read()
                                offset = engine.tell()
                            lines = chunk.split('\n')
                            pending = lines.pop()
                            for line in lines:
                                line = qualification.strip_colors(line).strip()
                                handle = re.fullmatch(r'windowFocusTest: nativeWindow=(?:0x)?([0-9a-fA-F]+)', line)
                                if handle:
                                    observer.bind_window(int(handle.group(1), 16))
                                if line == 'VK_PACING_READY': ready = True
                                if line == 'VK_PACING_BEGIN': sampled = True
                                if line == 'VK_PACING_END':
                                    sampled = False
                                    sample_end = now
                                    if trace:
                                        qpc_end = presentmon.qpc_milliseconds()
                        focused = observer.foreground()
                        if now >= next_process_check:
                            active_competitors = competing_workloads(process.pid)
                            next_process_check = now + 1
                        if ready and released is None:
                            interrupted = any(event['focused'] is not True for event in observer.events[previous_event_count:])
                            gate_observations.append(dict(seconds=now-start, focused=focused,
                                                          competitors=len(active_competitors), interrupted=interrupted))
                            trace_ready = trace.ready() if trace else True
                            if readiness.update(now, focused if trace_ready else None, active_competitors, interrupted):
                                # Observe from gate release, slightly before the
                                # reset/begin commands, to fail conservatively.
                                released = now
                                if trace:
                                    qpc_begin = presentmon.qpc_milliseconds()
                                replace_gate(game / 'vkpacing_gate.cfg', 'exec vkpacing_sample.cfg\n')
                        previous_event_count = len(observer.events)
                        if sampled:
                            focus.append(focused)
                            observations.append(dict(seconds=now-start, focused=focused,
                                                     competitors=len(active_competitors)))
                            for competitor in active_competitors:
                                competing[competitor['pid']] = competitor
                        if time.monotonic() - start > args.timeout:
                            timed_out = True
                            break
                        time.sleep(0.1)
                except Exception as error:
                    observer_errors.append(str(error))
                finally:
                    if observer is not None:
                        try:
                            observer.close()
                        except Exception as error:
                            observer_errors.append(str(error))
                    if process.poll() is None:
                        process.kill()
                    code = process.wait()
                    if trace:
                        try:
                            trace.close()
                        except Exception as error:
                            observer_errors.append('PresentMon shutdown: ' + str(error))
            text = qualification.strip_colors(log.read_text(errors='replace')) if log.is_file() else ''
            focus_events = foreground_observer.interval_events(observer.events if observer else [], released, sample_end)
            result = analyze(text, focus, args.p99_budget_ms, competing, focus_events,
                             require_presented=args.timing_source == 'engine')
            if trace:
                result['etwPresentation'] = trace.analyze(process.pid, qpc_begin, qpc_end, args.p99_budget_ms)
                result['failures'].extend('ETW: ' + failure for failure in result['etwPresentation']['failures'])
            elif args.presentmon:
                result['failures'].append('PresentMon capture was not started')
            if observer is None or observer.overflow or observer.errors or observer_errors:
                result['failures'].append('foreground event observation failed or overflowed')
            if released is None:
                result['failures'].append('foreground/idle readiness gate was not released')
            observation_path = save / 'foreground.json'
            observation_path.write_text(json.dumps(dict(prelaunch=idle, readySeconds=args.ready_seconds,
                renderWindow=observer.window if observer else None,
                gate=gate_observations, sampled=observations,
                events=[dict(seconds=event['time']-start, focused=event['focused']) for event in focus_events],
                errors=observer_errors + (observer.errors if observer else [])), indent=2) + '\n')
            if code or timed_out:
                result['failures'].append('process failed or timed out')
            capture = game / 'screenshots/pacing.tga'
            try:
                if not qualification.screenshot_is_drawn(qualification.screenshot_stats(capture)):
                    result['failures'].append('blank engine capture')
            except (OSError, ValueError) as error:
                result['failures'].append('missing or invalid engine capture: ' + str(error))
            phase_trace = game / 'logs/timing.csv'
            config = game / 'openQ4Config.cfg'
            if not phase_trace.is_file() or phase_trace.stat().st_size == 0:
                result['failures'].append('CPU/GPU phase trace was not written')
            if not config.is_file() or 'seta com_performancePreset "balanced"' not in config.read_text(errors='replace'):
                result['failures'].append('saved performance preset differs from the declared balanced profile')
            result.update(id=name, command=command, exitCode=code, timedOut=timed_out, powerBefore=initial_power,
                powerAfter=power_state(), selectedDevice=qualification.device_identity('vulkan', qualification.parse_engine_log(text)),
                engineVersion=qualification.parse_engine_log(text)['version'],
                readinessReleased=released is not None, foregroundEventCount=len(focus_events), prelaunchIdle=idle,
                competingProcesses=list(competing.values()),
                displayAdapterRoute='unverified', elapsedSeconds=time.monotonic() - start,
                artifacts=[qualification.file_record(path, output) for path in
                           (log, capture, phase_trace, config, observation_path, game / 'vkpacing.cfg',
                            game / 'vkpacing_sample.cfg', save / 'process.log', save / 'command.json',
                            save / 'presentmon.csv', save / 'presentmon.log') if path.is_file()])
            result['status'] = 'fail' if result['failures'] else 'pass'
            results.append(result)
            (save / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
            print(name + ': ' + result['status'], flush=True)
    unchanged = before == qualification.runtime_inventory(runtime)
    harnesses_unchanged = harnesses == {source.name: qualification.sha256_file(source) for source in sources}
    tool_unchanged = presentmon_hash == (qualification.sha256_file(args.presentmon) if args.presentmon else None)
    report = dict(kind='vulkan-visible-presentation', runtimeUnchanged=unchanged, runtime=before,
        harnessSources=harnesses, harnessUnchanged=harnesses_unchanged, p99BudgetMs=args.p99_budget_ms,
        timingSource=args.timing_source,
        presentmonTool=dict(path=str(args.presentmon), sha256=presentmon_hash) if args.presentmon else None,
        toolUnchanged=tool_unchanged, supportPromotion=False, results=results,
        status='pass' if unchanged and harnesses_unchanged and tool_unchanged and all(result['status'] == 'pass' for result in results) else 'fail')
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(report['status'] != 'pass')


if __name__ == '__main__':
    raise SystemExit(main())
