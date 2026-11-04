"""Optional PresentMon console evidence for windowed presentation tests.

Invokes a tester-provided portable tool without installing a service, tracking
input, injecting code, registering hotkeys or altering another trace session.
See https://github.com/GameTechDev/PresentMon/blob/v2.6.0/README-ConsoleApplication.md
"""
from collections import Counter
import csv
import ctypes
import math
import os
from pathlib import Path
import re
import subprocess


def qpc_milliseconds():
    counter, frequency = ctypes.c_longlong(), ctypes.c_longlong()
    kernel = ctypes.windll.kernel32
    if not kernel.QueryPerformanceCounter(ctypes.byref(counter)) or not kernel.QueryPerformanceFrequency(ctypes.byref(frequency)):
        raise OSError('QPC clock unavailable')
    if frequency.value <= 0:
        raise OSError('invalid QPC clock frequency')
    return counter.value * 1000 / frequency.value


def percentiles(values):
    ordered = sorted(values)
    return {name: ordered[min(len(ordered) - 1, math.ceil(len(ordered) * fraction) - 1)]
            for name, fraction in (('p50', .5), ('p95', .95), ('p99', .99), ('max', 1))} if ordered else None


def analyze_rows(rows, pid, begin, end, budget):
    failures, selected, displayed = [], [], []
    lost = dict(EtwEventsLost=None, EtwBuffersLost=None, OverflowedPresents=None)
    if begin is None or end is None or not all(math.isfinite(value) for value in (begin, end)) or end <= begin:
        return dict(status='fail', failures=['missing/invalid QPC sampling interval'])
    try:
        for row in rows:
            for key in lost:
                if key not in row:
                    continue  # Unified 2.6 rows omit these; the status log is mandatory below.
                value = int(row[key])
                if value < 0:
                    raise ValueError('negative ETW loss counter')
                lost[key] = max(lost[key] or 0, value)
            if int(row['ProcessID']) != pid:
                raise ValueError('CSV contains a different process')
            start = float(row['CPUStartQPCTime'])
            if not math.isfinite(start):
                raise ValueError('non-finite CPU frame timestamp')
            if begin <= start <= end:
                selected.append(row)
            if row['DisplayedTime'] == 'NA':
                continue
            latency, duration = float(row['DisplayLatency']), float(row['DisplayedTime'])
            if not all(math.isfinite(value) and value >= 0 for value in (latency, duration)):
                raise ValueError('invalid display timing')
            display = start + latency
            if duration > 0 and begin <= display and display + duration <= end:
                displayed.append(dict(time=display, duration=duration, row=row))
    except (KeyError, ValueError, TypeError) as error:
        failures.append('invalid PresentMon CSV: ' + str(error))
    if any(lost.values()):
        failures.append('ETW events, buffers or pending presents were lost')
    chains = sorted({item['row'].get('SwapChainAddress', '') for item in displayed})
    if len(chains) != 1 or not chains[0]:
        failures.append('displayed interval does not identify exactly one PresentMon stream key')
    address_available = False
    if len(chains) == 1:
        try:
            address_available = int(chains[0], 0) > 0
        except ValueError:
            failures.append('PresentMon stream key is not a numeric address')
    displayed.sort(key=lambda item: item['time'])
    durations = [item['duration'] for item in displayed]
    interval = percentiles(durations)
    if len(displayed) < 120:
        failures.append('fewer than 120 displayed frames')
    if interval and interval['p99'] > budget:
        failures.append('displayed p99 exceeds the declared budget')
    coverage = sum(durations) / (end - begin)
    if not .95 <= coverage <= 1.01:
        failures.append('displayed intervals do not cover the sample without overlap')
    if displayed and (displayed[0]['time'] - begin > 250 or end - displayed[-1]['time'] - displayed[-1]['duration'] > 250):
        failures.append('display timing is missing at a sampling boundary')
    if any(abs(left['time'] + left['duration'] - right['time']) > 1
           for left, right in zip(displayed, displayed[1:])):
        failures.append('display timing has an interior gap or overlap')
    return dict(status='fail' if failures else 'pass', failures=failures, frames=len(displayed),
        measurement='displayed frame duration (PresentMon v2)',
        cpuWindowRows=len(selected), undisplayedRows=sum(row['DisplayedTime'] == 'NA' for row in selected),
        interval=interval, coverage=coverage, qpcIntervalMs=[begin, end], swapchains=chains,
        swapchainAddressAvailable=address_available, identityScope='process and PresentMon stream key',
        csvLossCounters=lost,
        presentModes=dict(Counter(item['row'].get('PresentMode', 'missing') for item in displayed)),
        hybridPresent=dict(Counter(item['row'].get('HybridPresent', 'unavailable') for item in displayed)))


def decode_log(data):
    # The Windows console executable redirects wide stdout as BOM-marked
    # UTF-16. Preserve strict decoding so corrupt evidence cannot pass.
    return data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')


def analyze_loss(text):
    # 2.6's unified CSV writer does not emit loss columns. Its one-second
    # status timer reports all three counters, and normal shutdown reports
    # any final loss since the last timer. Neither evidence may be absent.
    rows = re.findall(r'\[ETW Status\].*EventsLost=(\d+) BuffersLost=(\d+), OverflowedPresents=(\d+)', text)
    counts = dict(zip(('events', 'buffers', 'presents'),
                      (max(int(row[index]) for row in rows) for index in range(3)))) if rows else None
    failures = []
    if len(rows) < 2:
        failures.append('ETW loss-status observations are missing or insufficient')
    if counts and any(counts.values()):
        failures.append('ETW events, buffers or pending presents were lost')
    if re.search(r'(?im)\b(?:error|lost|overflow(?:ed)?)\b', text):
        failures.append('PresentMon reported an error or lost/overflowed events')
    return dict(observations=len(rows), maximumCounters=counts, failures=failures)


class Capture:
    def __init__(self, executable, pid, directory, timeout):
        self.executable = Path(executable).resolve()
        self.directory = directory
        self.csv = directory / 'presentmon.csv'
        self.log = directory / 'presentmon.log'
        self.session = f'OpenQ4Vk-{os.getpid()}-{pid}'
        self.command = [str(self.executable), '--process_id', str(pid), '--session_name', self.session,
            '--output_file', str(self.csv), '--no_track_input', '--no_console_stats', '--qpc_time_ms',
            '--v2_metrics', '--track_hybrid_present', '--track_etw_status', '--write_display_metadata',
            '--set_circular_buffer_size', '8192',
            '--timed', str(timeout + 10), '--terminate_after_timed', '--terminate_on_proc_exit']
        self.stream = self.log.open('w')
        try:
            self.process = subprocess.Popen(self.command, cwd=directory, stdout=self.stream, stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        except Exception:
            self.stream.close()
            raise
        self.errors = []

    def ready(self):
        if self.process.poll() is not None:
            raise RuntimeError('PresentMon exited before the measured interval')
        return self.csv.is_file() and self.csv.stat().st_size > 0

    def close(self):
        try:
            self.process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            # Only stop the unique session created by this child. Never use
            # stop_existing_session when starting or terminate a generic name.
            try:
                result = subprocess.run([str(self.executable), '--session_name', self.session,
                    '--terminate_existing_session'], cwd=self.directory,
                    stdout=self.stream, stderr=subprocess.STDOUT, timeout=5,
                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                if result.returncode:
                    self.errors.append('could not stop the owned PresentMon session')
                self.process.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired) as error:
                self.errors.append(str(error))
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            self.stream.close()
        if self.process.returncode:
            self.errors.append('PresentMon exited with code ' + str(self.process.returncode))

    def analyze(self, pid, begin, end, budget):
        try:
            with self.csv.open(newline='', encoding='utf-8-sig') as stream:
                result = analyze_rows(csv.DictReader(stream), pid, begin, end, budget)
        except OSError as error:
            result = dict(status='fail', failures=['PresentMon CSV unavailable: ' + str(error)])
        try:
            result['eventLoss'] = analyze_loss(decode_log(self.log.read_bytes()))
        except (OSError, UnicodeError) as error:
            result['eventLoss'] = dict(failures=['PresentMon status log unavailable/invalid: ' + str(error)])
        result['failures'].extend(result['eventLoss']['failures'])
        result['failures'].extend(self.errors)
        result.update(command=self.command, exitCode=self.process.returncode,
                      status='fail' if result['failures'] else 'pass')
        return result


def self_test():
    rows = [dict(ProcessID='7', CPUStartQPCTime=str(i * 10), DisplayLatency='1', DisplayedTime='10',
                 SwapChainAddress='0x1', PresentMode='Composed: Flip', HybridPresent='1',
                 EtwEventsLost='0', EtwBuffersLost='0', OverflowedPresents='0') for i in range(201)]
    assert analyze_rows(rows, 7, 1, 2001, 28)['status'] == 'pass'
    assert analyze_rows(rows, 7, 1, 2001, 9)['status'] == 'fail'
    assert analyze_rows(rows, 8, 1, 2001, 28)['status'] == 'fail'
    assert analyze_rows(rows[:90], 7, 1, 2001, 28)['status'] == 'fail'
    assert analyze_rows(rows, 7, None, 2001, 28)['status'] == 'fail'
    implicit = [dict(row, SwapChainAddress='0x0', PresentMode='Composed: Copy with GPU GDI') for row in rows]
    assert analyze_rows(implicit, 7, 1, 2001, 28)['status'] == 'pass'
    assert not analyze_rows(implicit, 7, 1, 2001, 28)['swapchainAddressAvailable']
    assert analyze_rows(rows, 7, 1, 2001, 28)['swapchainAddressAvailable']
    for field, value in (('CPUStartQPCTime', 'nan'), ('DisplayedTime', '-1'),
                         ('SwapChainAddress', '0x2'), ('DisplayedTime', 'NA'), ('EtwEventsLost', '1'),
                         ('EtwBuffersLost', '1'), ('OverflowedPresents', '1')):
        changed = [dict(row) for row in rows]
        changed[50][field] = value
        assert analyze_rows(changed, 7, 1, 2001, 28)['status'] == 'fail'
    status = '[ETW Status] EventsLost=0 BuffersLost=0, OverflowedPresents=0\n' * 2
    assert not analyze_loss(status)['failures']
    for encoding in ('utf-8', 'utf-8-sig', 'utf-16'):
        assert not analyze_loss(decode_log(status.encode(encoding)))['failures']
        assert analyze_loss(decode_log(status.replace('EventsLost=0', 'EventsLost=1').encode(encoding)))['failures']
    assert analyze_loss('')['failures']
    for key in ('EventsLost', 'BuffersLost', 'OverflowedPresents'):
        assert analyze_loss(status.replace(key + '=0', key + '=1'))['failures']
    assert analyze_loss(status + 'warning: 1 ETW events were lost.')['failures']
    print('PresentMon parser: QPC boundaries, identity, budget, coverage and lost-interval controls passed')


if __name__ == '__main__':
    self_test()
