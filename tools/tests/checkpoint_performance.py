#!/usr/bin/env python3
"""Measure checkpoint phases and round-trip the last checkpoint in stock SP.

Uses a window, engine commands and render-target screenshots; never captures or
injects input. Requests 2560x1440 with Quality graphics; the desktop may constrain
the client extent. Native fullscreen qualification uses airdefense_performance.
Evidence stays under .tmp, and the stable staged runtime is checked before use.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import zlib
import os

import renderer_gameplay_benchmark as benchmark
from mp_round_remote_smoke import verify_runtime_is_current, write_script
from airdefense_performance import QUALITY_PROFILES

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--render-api', choices=('gl', 'vk'), default='vk')
    parser.add_argument('--preset', choices=tuple(QUALITY_PROFILES), default='quality')
    parser.add_argument('--width', type=int, default=2560)
    parser.add_argument('--height', type=int, default=1440)
    parser.add_argument('--developer', type=int, choices=(0,1), default=1, help='Keep crash-debug logging by default; use 0 to measure its observer cost separately.')
    parser.add_argument('--log-mode', type=int, choices=(1,2), default=2, help='1 buffers output, 2 preserves the crash-debug launch. Record comparisons separately.')
    parser.add_argument('--lifecycle', action='store_true', help='Also load and quit immediately after requesting checkpoints, then reopen the final save.')
    parser.add_argument('--jobs-synchronous', action='store_true', help='Qualify the scheduler-disabled storage fallback.')
    parser.add_argument('--verify-script-checksum', action='store_true', help='Recompute script checksums as an independent cache oracle; diagnostic timings include the extra hashing.')
    parser.add_argument('--profile-frame-work', action='store_true', help='Capture CPU frame phases for diagnosis, with the CSV flush after the checkpoint windows.')
    parser.add_argument('--profile-dictionaries', action='store_true', help='Time dictionary serialization separately; diagnostic timings include clock overhead.')
    parser.add_argument('--commit-worker', action='store_true', help='Exercise background checkpoint publication.')
    parser.add_argument('--gui-cache-mode', type=int, choices=(0,1,2), default=None, help='Per-snapshot legacy GUI reuse: 0 reference, 1 cache, 2 verify every reused payload.')
    parser.add_argument('--dictionary-cache-mode', type=int, choices=(0,1,2), default=None, help='Override dictionary reuse: 0 reference, 1 cache, 2 compare cache hits with the original writer.')
    parser.add_argument('--fail-commit', action='store_true', help='Windows: deny preview rename in this test profile and verify transaction rollback and reload.')
    parser.add_argument('--fail-overwrite', action='store_true', help='Block both staged file names with directory fixtures and verify the committed slots survive.')
    parser.add_argument('--fail-overwrite-kind', choices=('payload', 'description', 'preview'), default='payload')
    parser.add_argument('--corrupt-load', action='store_true', help='Damage a copy of a completed checkpoint and verify independent load-time CRC rejection, then load the valid slot again.')
    options = parser.parse_args()
    quality = QUALITY_PROFILES[options.preset]
    if options.fail_commit and (os.name != 'nt' or not options.commit_worker):
        parser.error('--fail-commit requires Windows and --commit-worker.')
    output = options.output.resolve()
    if not output.is_relative_to(ROOT / '.tmp') or output.exists():
        parser.error('Use a new evidence directory under project .tmp.')
    runtime = ROOT / '.install'
    verify_runtime_is_current(runtime)
    game = output / 'save' / 'baseoq4'
    game.mkdir(parents=True)
    spec = benchmark.RunSpec('sp-airdefense1', 'SP', 'game/airdefense1',
        'game/airdefense1', 'checkpoint', 'rotating', 'auto', '0', '0',
        'windowed', 'default', 'best', options.render_api)
    args = benchmark.common_args(ROOT, runtime, output / 'save', 'openq4.log', '',
        spec, options.width, options.height, 'baseline', False, False,
        (('in_mouse', '0'), ('in_joystick', '0'), ('ui_retained', '1'),
         ('r_multiSamples', str(quality['r_multiSamples'])), ('image_anisotropy', str(quality['image_anisotropy'])),
         ('image_downSize', '0'), ('image_downSizeBump', '0'),
         ('image_downSizeSpecular', '0'), ('image_ignoreHighQuality', '0'),
         ('image_picmip', '0')), 'checkpoint.cfg')
    benchmark.append_set(args, 'developer', str(options.developer))
    benchmark.append_set(args, 'logFile', str(options.log_mode))
    benchmark.append_set(args, 'si_gameType', 'singleplayer')
    benchmark.append_set(args, 'com_gameMode', 'SP')
    if options.jobs_synchronous:
        benchmark.append_set(args, 'jobs_enable', '0')
    if options.commit_worker:
        benchmark.append_set(args, 'com_saveGameCommitOnWorker', '1')
    if options.gui_cache_mode is not None:
        benchmark.append_set(args, 'g_saveUserInterfaceCache', str(options.gui_cache_mode))
    if options.dictionary_cache_mode is not None:
        benchmark.append_set(args, 'g_saveDictionaryCache', str(options.dictionary_cache_mode))
    benchmark.append_set(args, 'r_hiddenWindow', '0')
    benchmark.append_command(args, 'map', 'game/airdefense1')
    assert sum(item.startswith('+') for item in args) <= 64
    lines = ['god', 'notarget', f'applyPerformancePreset {options.preset}', 'com_maxfps 0',
        'com_saveGameCopyOnWorker', 'com_saveGameCommitOnWorker', 'g_saveDictionaryCache', 'g_saveUserInterfaceCache',
        f'com_saveGameProfile {3 if options.profile_dictionaries else 2 if options.verify_script_checksum else 1}', 'benchmarkViewSweep 360 12000', 'waitMsec 15000',
        'gfxInfo', 'viewpos', 'benchmarkViewSweep 720 24000']
    # Pin and record the same graphics/HUD workload as fullscreen qualification.
    # Readback finishes before any checkpoint timing window starts.
    lines[2:2] = [f'{key} {value}' for key, value in quality.items()]
    lines[lines.index('gfxInfo'):lines.index('gfxInfo')] = list(quality)
    if options.profile_frame_work:
        lines += ['profileFrameWork 250', 'wait 1']
    for index in range(3):
        lines += ['framePacingReset', 'saveGame checkPoint', 'waitMsec 1500',
                  f'framePacingSnapshot checkpoint{index}']
    if options.profile_frame_work:
        lines += ['waitMsec 4500']
    lines += ['set g_autoExecAfterMapLoad restored.cfg']
    if options.lifecycle:
        lines += ['saveGame checkPoint', 'loadGame Checkpoint1']
    else:
        lines += ['loadGame Checkpoint0']
    write_script(game / 'checkpoint.cfg', lines)
    restored = ['echo CHECKPOINT_RESTORED', 'waitMsec 1500',
        'screenshot "screenshots/restored.tga"', 'wait 3']
    if options.lifecycle:
        restored += ['saveGame checkPoint']
    write_script(game / 'restored.cfg', restored + ['quit'])
    report = dict(command=args, preset=options.preset, quality=quality, developer=options.developer, log_mode=options.log_mode, runtime={str(path.relative_to(runtime)):
        hashlib.sha256(path.read_bytes()).hexdigest() for path in
        (runtime / 'openQ4-client_x64.exe', runtime / 'baseoq4/game_x64.dll', runtime / f'renderer-{options.render_api}_x64.dll')})
    (output / 'plan.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    code, timeout, elapsed = benchmark.launch_and_wait(runtime / 'openQ4-client_x64.exe',
        args, runtime, output / 'stdout.txt', output / 'stderr.txt', 180)
    text = (game / 'logs/openq4.log').read_text(encoding='utf-8', errors='replace')
    report['quality_failures'] = [f'{key}={value}' for key, value in quality.items()
        if re.findall(rf'^"{re.escape(key)}" is:"([^"\n]+)"', text, re.M) != [str(value)]]
    report.update(exit_code=code, timed_out=timeout, elapsed=elapsed,
        phases=[dict(slot=slot, phase=phase, ms=float(ms)) for slot, phase, ms in
            re.findall(r'Save profile: slot=(\S+) phase=(\S+) ms=([\d.]+)', text)],
        frames=re.findall(r'^Frame pacing.*$', text, re.M), saves=[])
    report['dictionary_profiles'] = [dict(ms=float(ms), count=int(count), bytes=int(size))
        for ms, count, size in re.findall(r'Save dictionary profile: ([\d.]+) ms count=(\d+) bytes=(\d+)', text)]
    report['dictionary_diagnostic'] = options.profile_dictionaries
    report['dictionary_cache'] = [dict(hits=int(hits), misses=int(misses), verified=int(verified), entries=int(entries), bytes=int(size))
        for hits,misses,verified,entries,size in re.findall(r'Save dictionary cache: hits=(\d+) misses=(\d+) verified=(\d+) entries=(\d+) bytes=(\d+)',text)]
    report['dictionary_cache_mode'] = options.dictionary_cache_mode
    report['dictionary_population'] = [dict(batches=int(batches), records=int(records), discarded=int(discarded), total_ms=float(total), max_ms=float(maximum))
        for batches,records,discarded,total,maximum in re.findall(r'Save dictionary population: batches=(\d+) records=(\d+) discarded=(\d+) total_ms=([\d.]+) max_ms=([\d.]+)',text)]
    shot = game / 'screenshots/restored.tga'
    report['requested_extent'] = [options.width, options.height]
    report['captured_extent'] = list(struct.unpack_from('<HH', shot.read_bytes(), 12)) if shot.exists() else None
    for path in (game / 'savegames').glob('Checkpoint*.save'):
        data = path.read_bytes()
        version, length, checksum = struct.unpack_from('<III', data, len(data)-12)
        report['saves'].append(dict(name=path.name, bytes=len(data),
            integrity=data[-16:-12] == b'OQ4I' and version == 1 and length == len(data)-16 and
            checksum == zlib.crc32(data[:-16]),
            preview=path.with_suffix('.tga').exists() and
                path.with_suffix('.tga').read_bytes()[12:17] == struct.pack('<HHB', 320, 240, 32)))
    report['passed'] = (code == 0 and not timeout and not report['quality_failures'] and text.count('CHECKPOINT_RESTORED') == 1
        and text.count('Game Map Init SaveGame') == 1 and 'ERROR:' not in text
        and len([phase for phase in report['phases'] if phase['phase'] == 'total']) == (5 if options.lifecycle else 3)
        and len(report['saves']) == 2 and all(save['integrity'] and save['preview'] for save in report['saves'])
        and (game / 'screenshots/restored.tga').exists())
    if options.profile_dictionaries:
        report['passed'] &= len(report['dictionary_profiles']) == (5 if options.lifecycle else 3)
    if options.dictionary_cache_mode in (1,2):
        samples=report['dictionary_cache']
        report['passed'] &= len(samples)==(5 if options.lifecycle else 3) and samples[0]['hits']==0 and samples[1]['hits']>0
        report['passed'] &= all(sample['bytes']<=16*1024*1024 for sample in samples)
        if options.dictionary_cache_mode==2:
            report['passed'] &= all(sample['hits']==sample['verified'] for sample in samples)
        population=report['dictionary_population']
        report['passed'] &= len(population)==len(samples) and len(population)>=2 and population[0]['records']==0 and population[1]['records']>0
    preview_jobs = re.findall(r'Checkpoint preview worker: ([\d.]+) ms verified=(\d)', text)
    report['preview_workers'] = [dict(ms=float(ms), verified=int(verified)) for ms, verified in preview_jobs]
    report['passed'] = report['passed'] and (not preview_jobs if options.jobs_synchronous else
        len(preview_jobs) == (5 if options.lifecycle else 3) and all(verified == '1' for _, verified in preview_jobs))
    if options.verify_script_checksum:
        checks = re.findall(r'Script save checksum: cached=(\d) matched=(\d) reference=([\d.]+) ms statements=(\d+)', text)
        report['script_checksum_oracle'] = [dict(cached=int(hit), matched=int(match), reference_ms=float(ms), statements=int(count))
            for hit, match, ms, count in checks]
        report['passed'] = report['passed'] and len(checks) == (5 if options.lifecycle else 3) and all(hit == match == '1' for hit, match, _, _ in checks)
    if options.lifecycle:
        write_script(game / 'reopen.cfg', ['echo CHECKPOINT_REOPENED_AFTER_QUIT', 'waitMsec 1000',
            'screenshot "screenshots/reopened.tga"', 'wait 3', 'quit'])
        reopen = args.copy()
        reopen[reopen.index('g_autoExecAfterMapLoad')+1] = 'reopen.cfg'
        reopen[reopen.index('logFileName')+1] = 'logs/reopened.log'
        reopen[-2:] = ['+loadGame', 'Checkpoint0']
        reopen_code, reopen_timeout, _ = benchmark.launch_and_wait(runtime / 'openQ4-client_x64.exe',
            reopen, runtime, output / 'reopened-stdout.txt', output / 'reopened-stderr.txt', 180)
        reopened = (game / 'logs/reopened.log').read_text(encoding='utf-8', errors='replace')
        report['reopened'] = (reopen_code == 0 and not reopen_timeout and
            reopened.count('CHECKPOINT_REOPENED_AFTER_QUIT') == 1 and
            reopened.count('Game Map Init SaveGame') == 1 and 'ERROR:' not in reopened and
            (game / 'screenshots/reopened.tga').exists())
        report['passed'] = report['passed'] and report['reopened']
    if options.corrupt_load:
        source = game / 'savegames/Checkpoint0.save'
        original = source.read_bytes()
        damaged = bytearray(original)
        damaged[len(damaged)//2] ^= 0x40
        (game / 'savegames/CorruptCheckpoint.save').write_bytes(damaged)
        write_script(game / 'corrupt.cfg', ['echo CHECKPOINT_CORRUPTION_START',
            'loadGame CorruptCheckpoint', 'echo CHECKPOINT_CORRUPTION_RETURNED',
            'set g_autoExecAfterMapLoad corrupt_recovered.cfg', 'loadGame Checkpoint0'])
        write_script(game / 'corrupt_recovered.cfg', ['echo CHECKPOINT_CORRUPTION_VALID_AGAIN', 'quit'])
        corruption = args.copy()
        corruption[corruption.index('g_autoExecAfterMapLoad')+1] = 'corrupt.cfg'
        corruption[corruption.index('logFileName')+1] = 'logs/corruption.log'
        corruption[-2:] = ['+loadGame', 'Checkpoint0']
        corruption_code, corruption_timeout, _ = benchmark.launch_and_wait(runtime / 'openQ4-client_x64.exe',
            corruption, runtime, output / 'corruption-stdout.txt', output / 'corruption-stderr.txt', 180)
        corruption_log = (game / 'logs/corruption.log').read_text(encoding='utf-8', errors='replace')
        report['corrupt_load_rejected'] = (corruption_code == 0 and not corruption_timeout and
            all(corruption_log.count(marker) == 1 for marker in ('CHECKPOINT_CORRUPTION_START',
                'CHECKPOINT_CORRUPTION_RETURNED', 'CHECKPOINT_CORRUPTION_VALID_AGAIN')) and
            "CorruptCheckpoint.save' failed its integrity check" in corruption_log and
            corruption_log.count('Game Map Init SaveGame') == 2 and
            source.read_bytes() == original and
            (game / 'savegames/CorruptCheckpoint.save').read_bytes() == damaged)
        report['passed'] = report['passed'] and report['corrupt_load_rejected']
    if options.verify_script_checksum:
        # Change compiled statements twice, including branch fixups and constant
        # folding, then reload the untouched checkpoint to truncate the snippets.
        # The diagnostic oracle must match after every change and after restart.
        write_script(game / 'checksum_compile.cfg', ['com_saveGameProfile 2',
            'script "float checksumProbe = 1 + 2; if ( checksumProbe == 3 ) { checksumProbe = checksumProbe + 1; } sys.println(checksumProbe)"',
            'wait 3', 'saveGame ChecksumProbe0', 'waitMsec 1500',
            'script "float checksumProbe = 0; while ( checksumProbe < 3 ) { checksumProbe = checksumProbe + 1; } sys.println(checksumProbe)"',
            'wait 3', 'saveGame ChecksumProbe1', 'waitMsec 1500',
            'set g_autoExecAfterMapLoad checksum_restart.cfg', 'loadGame Checkpoint0'])
        write_script(game / 'checksum_restart.cfg', ['saveGame ChecksumProbe2', 'waitMsec 1500',
            'echo CHECKPOINT_CHECKSUM_RESTARTED', 'quit'])
        compiled = args.copy()
        compiled[compiled.index('g_autoExecAfterMapLoad')+1] = 'checksum_compile.cfg'
        compiled[compiled.index('logFileName')+1] = 'logs/checksum_compile.log'
        compiled[-2:] = ['+loadGame', 'Checkpoint0']
        compiled_code, compiled_timeout, _ = benchmark.launch_and_wait(runtime / 'openQ4-client_x64.exe',
            compiled, runtime, output / 'checksum-stdout.txt', output / 'checksum-stderr.txt', 180)
        compiled_log = (game / 'logs/checksum_compile.log').read_text(encoding='utf-8', errors='replace')
        checks = re.findall(r'Script save checksum: cached=(\d) matched=(\d) reference=([\d.]+) ms statements=(\d+)', compiled_log)
        baseline_count = report['script_checksum_oracle'][0]['statements'] if report['script_checksum_oracle'] else 0
        report['script_compilation_oracle'] = [dict(cached=int(hit), matched=int(match), reference_ms=float(ms), statements=int(count))
            for hit, match, ms, count in checks]
        report['script_compilation_passed'] = (compiled_code == 0 and not compiled_timeout and
            compiled_log.count('CHECKPOINT_CHECKSUM_RESTARTED') == 1 and
            compiled_log.count('Game Map Init SaveGame') == 2 and 'ERROR:' not in compiled_log and
            len(checks) == 3 and all(hit == match == '1' for hit, match, _, _ in checks) and
            int(checks[1][3]) > int(checks[0][3]) > baseline_count == int(checks[2][3]))
        report['passed'] = report['passed'] and report['script_compilation_passed']
    report['commit_workers'] = [dict(ms=float(ms), committed=int(c), cleanup=int(cl), recovery=int(r))
        for ms,c,cl,r in re.findall(r'Checkpoint commit worker: ([\d.]+) ms committed=(\d) cleanup=(\d) recovery=(\d)', text)]
    commit_setting = re.findall(r'^"com_saveGameCommitOnWorker" is:"([01])"', text, re.M)
    if options.jobs_synchronous:
        report['passed'] = report['passed'] and not report['commit_workers']
    elif options.commit_worker or commit_setting == ['1']:
        report['passed'] = report['passed'] and len(report['commit_workers']) == (5 if options.lifecycle else 3) and all(
            row['committed'] == row['cleanup'] == 1 and row['recovery'] == 0 for row in report['commit_workers'])
    report['gui_cache'] = [dict(hits=int(h), misses=int(m), verified=int(v), bytes=int(b))
        for h, m, v, b in re.findall(r'Save GUI cache: hits=(\d+) misses=(\d+) verified=(\d+) bytes=(\d+)', text)]
    if options.gui_cache_mode in (1, 2):
        report['gui_cache_exercised'] = len(report['gui_cache']) >= 3 and all(
            row['hits'] > 0 and (options.gui_cache_mode != 2 or row['verified'] == row['hits'])
            for row in report['gui_cache'])
        report['passed'] = report['passed'] and report['gui_cache_exercised']
    if options.fail_overwrite:
        committed = sorted(path for path in (game / 'savegames').glob('Checkpoint*') if path.is_file())
        before = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in committed}
        # These empty collision directories are retained as part of the failure
        # fixture; no existing path is modified or removed by this test.
        extension = dict(payload='save', description='txt', preview='tga')[options.fail_overwrite_kind]
        report['failed_overwrite_kind'] = options.fail_overwrite_kind
        for name in (f'Checkpoint0.{extension}.tmp', f'Checkpoint1.{extension}.tmp'):
            (game / 'savegames' / name).mkdir()
        write_script(game / 'failure.cfg', ['saveGame checkPoint', 'waitMsec 2500',
            'echo CHECKPOINT_FAILURE_CHECKED', 'quit'])
        failure = args.copy()
        failure[failure.index('g_autoExecAfterMapLoad')+1] = 'failure.cfg'
        failure[failure.index('logFileName')+1] = 'logs/failure.log'
        failure[-2:] = ['+loadGame', 'Checkpoint0']
        failure_code, failure_timeout, _ = benchmark.launch_and_wait(runtime / 'openQ4-client_x64.exe',
            failure, runtime, output / 'failure-stdout.txt', output / 'failure-stderr.txt', 180)
        failure_log = (game / 'logs/failure.log').read_text(encoding='utf-8', errors='replace')
        after = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in committed}
        report['failed_overwrite_before'] = before
        report['failed_overwrite_after'] = after
        report['failed_overwrite_preserved_slots'] = (failure_code == 0 and not failure_timeout and
            failure_log.count('CHECKPOINT_FAILURE_CHECKED') == 1 and before == after and
            "Saved 'Checkpoint" not in failure_log and
            any(message in failure_log for message in ('Failed to persist checkpoint',
                'Failed to persist staged savegame', 'Failed to persist description file',
                'Failed to stage preview deletion marker', 'Could not recover interrupted save files')))
        report['passed'] = report['passed'] and report['failed_overwrite_preserved_slots']
    if options.fail_commit:
        # Only this isolated profile's preview files are held. Sharing allows
        # reads but denies deletion/rename, forcing backup of the description
        # to roll back when the next component cannot move.
        import ctypes
        from ctypes import wintypes
        native = ctypes.WinDLL('kernel32', use_last_error=True)
        native.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
            wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        native.CreateFileW.restype = wintypes.HANDLE
        native.CloseHandle.argtypes = [wintypes.HANDLE]
        native.CloseHandle.restype = wintypes.BOOL
        committed = sorted(path for path in (game / 'savegames').glob('Checkpoint*') if path.is_file())
        before = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in committed}
        previews = [p for p in committed if p.suffix == '.tga']
        assert len(previews) == 2 and all(p.resolve().is_relative_to(output) for p in previews)
        handles = []
        try:
            for path in previews:
                handle = native.CreateFileW(str(path), 0x80000000, 1, None, 3, 0, None)
                if handle == ctypes.c_void_p(-1).value:
                    raise OSError(ctypes.get_last_error(), 'Could not hold checkpoint preview fixture')
                handles.append(handle)
            write_script(game / 'commit_failure.cfg', ['com_saveGameProfile 1', 'com_saveGameCommitOnWorker 1',
                'saveGame checkPoint', 'waitMsec 2500', 'echo CHECKPOINT_COMMIT_FAILURE_CHECKED',
                'set g_autoExecAfterMapLoad commit_failure_restored.cfg', 'loadGame Checkpoint0'])
            write_script(game / 'commit_failure_restored.cfg', ['echo CHECKPOINT_COMMIT_FAILURE_RESTORED',
                'waitMsec 1000', 'screenshot "screenshots/commit-failure-restored.tga"', 'wait 3', 'quit'])
            failure = args.copy()
            failure[failure.index('g_autoExecAfterMapLoad')+1] = 'commit_failure.cfg'
            failure[failure.index('logFileName')+1] = 'logs/commit_failure.log'
            failure[-2:] = ['+loadGame', 'Checkpoint0']
            failure_code, failure_timeout, _ = benchmark.launch_and_wait(runtime / 'openQ4-client_x64.exe',
                failure, runtime, output / 'commit-failure-stdout.txt', output / 'commit-failure-stderr.txt', 180)
        finally:
            for handle in handles:
                native.CloseHandle(handle)
        failure_log = (game / 'logs/commit_failure.log').read_text(encoding='utf-8', errors='replace')
        after = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in committed}
        workers = re.findall(r'Checkpoint commit worker: ([\d.]+) ms committed=(\d) cleanup=(\d) recovery=(\d)', failure_log)
        report['failed_commit_before'] = before
        report['failed_commit_after'] = after
        report['failed_commit_workers'] = workers
        report['failed_commit_transaction_files'] = sorted(path.name for path in (game / 'savegames').glob('Checkpoint*')
            if path.suffix in ('.tmp', '.prev'))
        report['failed_commit_preserved_slots'] = (failure_code == 0 and not failure_timeout and
            before == after and not report['failed_commit_transaction_files'] and
            failure_log.count('CHECKPOINT_COMMIT_FAILURE_RESTORED') == 1 and
            len(workers) == 1 and workers[0][1:] == ('0', '1', '0') and
            "Failed to commit checkpoint" in failure_log and "Saved 'Checkpoint" not in failure_log and
            (game / 'screenshots/commit-failure-restored.tga').exists())
        report['passed'] = report['passed'] and report['failed_commit_preserved_slots']
    (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
