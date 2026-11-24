#!/usr/bin/env python3
"""Long-session voice soak with real local peers and synthetic PCM only.

A staged dedicated server and two hidden voice clients run for hours on
`mp/q4dm1`. The synthetic 400 Hz test tone (`testVoiceChat`) goes through the
real codec, net channel, server policy and OpenAL playback; no microphone,
host input or desktop capture is used. Each interval records admission,
added-delay, rejection, queue and process-memory counters, so a long session
shows whether frames keep being admitted (no permanent silencing by clock
drift), latency stays bounded, delayed backlog is still rejected after idle
gaps and nothing leaks across a mid-session reconnect.

Processes on one machine share one monotonic clock source, so their mutual
drift is close to zero. A pass qualifies long-session stability on this host;
the simulated 24-hour +/-1000 ppm test in VoicePacketTest stays the drift
evidence. The `baseline_shift_ms` counters record whatever drift exists.

Every process runs an immutable console loop (`soak_loop.cfg`) that executes
`soak_poll.cfg` and `soak_step.cfg` every 250 ms. A step is a new, never
rewritten `soak_cmd_N.cfg`; the harness points `soak_step.cfg` at it and, once
the step's begin marker appears, points it back at an idle file. Each step ends
with a short wait, so the loop cannot run it twice before the harness resets
the pointer; any repeat is still counted and fails the run. (`vstr` appends to
the command buffer, behind a self-inserting loop, so it cannot drive one.) A
failed read of the pointer while it is replaced only skips one poll.
"""
from __future__ import annotations

import argparse
import array
import ctypes
import hashlib
import json
import math
import os
import re
import struct
import subprocess
import time
from pathlib import Path

from mp_round_remote_smoke import verify_runtime_is_current, write_script
from voice_chat_runtime import LocalProxy

ROOT = Path(__file__).resolve().parents[2]
MAX_ADDED_MSEC = 120  # oq4voice::MaxAgeMsec
MAX_QUEUED = 4        # oq4voice::MaxQueuedFrames

# Ten-interval cycle: talk patterns, a silent gap, then a delayed-backlog probe
# after a long gap and a recovery interval that proves the probe trained nothing.
CYCLE = ('host', 'peer', 'both', 'silent', 'both', 'late', 'recover', 'host', 'silent', 'peer')
BURST_SECONDS = {'host': 4, 'peer': 3, 'both': 5, 'silent': 0, 'late': 3, 'recover': 4}


def system_memory():
    """Return (free commit bytes, commit limit bytes) or (None, None)."""
    if os.name != 'nt':
        try:
            values = dict(line.split(':', 1) for line in Path('/proc/meminfo').read_text().splitlines())
            kib = lambda key: int(values[key].split()[0]) * 1024
            return kib('CommitLimit') - kib('Committed_AS'), kib('CommitLimit')
        except (OSError, KeyError, ValueError):
            return None, None

    class Status(ctypes.Structure):
        _fields_ = [('length', ctypes.c_ulong), ('load', ctypes.c_ulong), ('total_phys', ctypes.c_ulonglong),
                    ('avail_phys', ctypes.c_ulonglong), ('total_page', ctypes.c_ulonglong),
                    ('avail_page', ctypes.c_ulonglong), ('total_virtual', ctypes.c_ulonglong),
                    ('avail_virtual', ctypes.c_ulonglong), ('avail_extended', ctypes.c_ulonglong)]
    status = Status()
    status.length = ctypes.sizeof(Status)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        return None, None
    return status.avail_page, status.total_page


class CpuMeter:
    """Whole-machine busy percentage between successive samples."""
    def __init__(self):
        self.last = self.times()

    @staticmethod
    def times():
        if os.name == 'nt':
            idle, kernel, user = (ctypes.c_ulonglong() for _ in range(3))
            if not ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
                return None
            return idle.value, kernel.value + user.value  # kernel time includes idle
        try:
            fields = [int(value) for value in Path('/proc/stat').read_text().split('\n', 1)[0].split()[1:]]
            return fields[3] + fields[4], sum(fields)
        except (OSError, ValueError, IndexError):
            return None

    def sample(self):
        now = self.times()
        previous, self.last = self.last, now
        if not now or not previous or now[1] == previous[1]:
            return None
        return round(100.0 * (1 - (now[0] - previous[0]) / (now[1] - previous[1])), 1)


def process_memory(process):
    """Private (commit) and resident bytes of a child, or None when unavailable."""
    try:
        import psutil  # optional; the harness reports None without it
        info = psutil.Process(process.pid).memory_info()
        return {'private': getattr(info, 'private', getattr(info, 'vms', 0)), 'resident': info.rss}
    except Exception:
        return None


class LogTail:
    """Incrementally read a log the engine flushes per print."""
    def __init__(self, path: Path):
        self.path, self.offset, self.text = path, 0, ''

    def poll(self) -> str:
        try:
            with self.path.open('rb') as stream:
                stream.seek(self.offset)
                data = stream.read()
        except OSError:
            return self.text
        # Only consume complete lines; a partial UTF-8 sequence or line waits.
        end = data.rfind(b'\n') + 1
        if end:
            self.text += data[:end].decode('utf-8', errors='replace')
            self.offset += end
        return self.text


def chunk_tone_peaks(path: Path, seconds: int = 60):
    """400 Hz amplitude of each `seconds` chunk of OpenAL's float WAV output."""
    with path.open('rb') as stream:
        header = stream.read(1 << 16)
    offset, channels, rate, data_offset, data_size = 12, 0, 0, None, 0
    while offset + 8 <= len(header):
        tag, size = struct.unpack_from('<4sI', header, offset)
        payload = offset + 8
        if tag == b'fmt ':
            kind, channels, rate, _, _, bits = struct.unpack_from('<HHIIHH', header, payload)
            assert bits == 32 and (kind == 3 or (kind == 65534 and struct.unpack_from('<I', header, payload + 24)[0] == 3))
        elif tag == b'data':
            data_offset, data_size = payload, size
            break
        offset = payload + size + (size & 1)
    assert data_offset is not None and channels and rate >= 8000
    total = path.stat().st_size - data_offset
    if 0 < data_size < total:
        total = data_size
    stride, count = rate // 8000, 160
    sine = [math.sin(2 * math.pi * 400 * i / 8000) for i in range(count)]
    cosine = [math.cos(2 * math.pi * 400 * i / 8000) for i in range(count)]
    frame_bytes = 4 * channels
    chunk_bytes = seconds * rate * frame_bytes
    peaks = []
    with path.open('rb') as stream:
        for start in range(0, total - chunk_bytes // 2, chunk_bytes):
            stream.seek(data_offset + start)
            pcm = array.array('f', stream.read(min(chunk_bytes, total - start) // 4 * 4))
            peak = 0.0
            # 20 ms windows every 100 ms, first channel, decimated to 8 kHz.
            for window in range(0, len(pcm) - count * stride * channels, rate * channels // 10):
                samples = [pcm[window + i * stride * channels] for i in range(count)]
                real = sum(value * cosine[i] for i, value in enumerate(samples))
                imag = sum(value * sine[i] for i, value in enumerate(samples))
                peak = max(peak, 2 * math.hypot(real, imag) / count)
            peaks.append(round(peak, 4))
    return peaks


def parse_fields(line: str) -> dict:
    return {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)\b', line)}


def parse_section(text: str) -> dict:
    """VOICE_STATUS, VOICE_PLAYBACK, VOICE_ADMISSION and VOICE_SERVER records."""
    status = re.findall(r'^VOICE_STATUS (.*)', text, re.M)
    return {
        'status': parse_fields(status[-1]) if status else None,
        'playback': [parse_fields(line) for line in re.findall(r'^VOICE_PLAYBACK (.*)', text, re.M)],
        'admission': {fields['client']: fields for fields in map(parse_fields, re.findall(r'^VOICE_ADMISSION (.*)', text, re.M))},
        'server': {fields['client']: fields for fields in map(parse_fields, re.findall(r'^VOICE_SERVER (.*)', text, re.M))},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path, default=ROOT / '.install',
                        help='existing staged runtime (keeps executable paths stable across worktrees)')
    parser.add_argument('--output', type=Path, default=ROOT / '.tmp/audio/voice-soak')
    parser.add_argument('--port', type=int, default=28961)
    parser.add_argument('--duration', type=int, default=7200, help='soak wall time in seconds after all peers are in game')
    parser.add_argument('--interval', type=int, default=60, help='seconds per measured interval')
    parser.add_argument('--render-api', default='vulkan', help='client r_renderApi; Vulkan renders hidden windows offscreen')
    parser.add_argument('--startup-timeout', type=int, default=900)
    parser.add_argument('--step-timeout', type=int, default=180)
    parser.add_argument('--min-free-commit-gb', type=float, default=10.0)
    parser.add_argument('--no-reconnect', action='store_true')
    args = parser.parse_args()
    if args.interval < 20 or args.duration < args.interval * 4:
        parser.error('use an interval of at least 20 s and a duration of at least four intervals')

    output, runtime = args.output.resolve(), args.runtime_dir.resolve()
    suffix = '.exe' if os.name == 'nt' else ''
    exe, server_exe = runtime / f'openQ4-client_x64{suffix}', runtime / f'openQ4-ded_x64{suffix}'
    verify_runtime_is_current(runtime)
    verify_runtime_is_current(runtime, server_exe.name)
    free_commit, commit_limit = system_memory()
    if free_commit is not None and free_commit < args.min_free_commit_gb * 2**30:
        print(f'refusing to launch: {free_commit / 2**30:.1f} GB free commit < {args.min_free_commit_gb} GB', flush=True)
        return 2
    output.mkdir(parents=True, exist_ok=True)
    roles = ('server', 'host', 'peer')
    saves = {role: output / role for role in roles}
    games = {role: saves[role] / 'baseoq4' for role in roles}
    tails = {role: LogTail(games[role] / 'logs/openq4.log') for role in roles}
    step_index = {role: 0 for role in roles}
    processes, streams = {}, []
    intervals_path = output / 'intervals.jsonl'
    intervals_path.write_text('')
    proxy = LocalProxy(args.port + 10, args.port)
    cpu = CpuMeter()

    def step_name(role, index):
        return f'soak_cmd_{index:05d}.cfg'

    def point(role, name, content):
        write_script(games[role] / name, [content])

    for role in roles:
        games[role].mkdir(parents=True, exist_ok=True)
        (games[role] / 'logs/openq4.log').unlink(missing_ok=True)
        for stale in games[role].glob('soak_cmd_*.cfg'):
            stale.unlink()
        write_script(games[role] / 'soak_loop.cfg', ['waitMsec 250', 'exec soak_poll.cfg', 'exec soak_step.cfg', 'exec soak_loop.cfg'])
        point(role, 'soak_poll.cfg', 'openq4_reportMPState')
        point(role, 'soak_step.cfg', '// idle')
        write_script(games[role] / 'soak_start.cfg', ['exec soak_loop.cfg'])
        # Mono 16 kHz keeps hours of rendered float output to ~230 MB/hour.
        (saves[role] / 'alsoft.conf').write_text('[general]\ndrivers=wave\nchannels=mono\nfrequency=16000\nhrtf=false\n[wave]\nfile=' +
                                                 str(saves[role] / 'mix.wav') + '\n')

    common = {
        'fs_basepath': str(args.assets.resolve()), 'fs_game': 'baseoq4', 'com_gameMode': 'MP',
        'r_fullscreen': '0', 'r_borderless': '0', 'r_borderlessDefaultMigrated': '1',
        'r_mode': '-1', 'r_customWidth': '640', 'r_customHeight': '360', 'r_windowWidth': '640', 'r_windowHeight': '360',
        'r_hiddenWindow': '1', 'r_renderApi': args.render_api, 'r_multiSamples': '0', 'r_swapInterval': '0', 'com_maxfps': '60',
        'in_mouse': '0', 'in_joystick': '0', 's_noSound': '0', 's_muteUnfocused': '0',
        's_volume': '.5', 's_musicVolume': '0', 's_voiceChatCapture': '0', 's_voiceChatSend': '1', 's_voiceChatReceive': '1',
        's_voiceChatEcho': '0', 's_voiceVolume': '1', 'logFile': '2', 'logFileName': 'logs/openq4.log', 'developer': '1',
        'g_voiceChatDebug': '0', 'ui_retained': '1', 'ui_autoJoin': '1', 'ui_spectate': 'Play', 'ui_ready': 'Ready',
        'net_allowCheats': '1', 'net_ip': '127.0.0.1', 'net_enableIPv6': '0', 'com_skipLoadingContinue': '1',
        'g_autoExecAfterMapLoad': 'soak_start.cfg', 'g_autoExecAfterMapLoadDelayMs': '500',
        'win_allowMultipleInstances' if os.name == 'nt' else 'sys_allowMultipleInstances': '1'
    }

    def launch(role: str) -> None:
        cvars = dict(common, fs_savepath=str(saves[role]), fs_devpath=str(saves[role]), ui_name='soak_' + role,
                     net_port=str(args.port + 1 + roles.index(role)))
        if role == 'server':
            cvars.update(net_port=str(args.port), net_serverDedicated='2', net_LANServer='1', si_pure='0',
                         si_maxPlayers='4', si_gameType='DM', si_minPlayers='1', si_warmup='0', si_useReady='0',
                         si_timeLimit='0', si_fragLimit='0', bot_minPlayers='0', g_matchProfile='casual')
        command = [str(server_exe if role == 'server' else exe)]
        for key, value in cvars.items():
            command += ['+set', key, value]
        assert len(cvars) + 1 <= 64
        command += ['+spawnServer', 'mp/q4dm1'] if role == 'server' else ['+connect', f'127.0.0.1:{args.port + (10 if role == "peer" else 0)}']
        (output / f'{role}-launch.json').write_text(json.dumps(command, indent=2))
        options = {'env': {**os.environ, 'ALSOFT_CONF': str(saves[role] / 'alsoft.conf'), 'ALSOFT_DRIVERS': 'wave'}}
        if os.name == 'nt':
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
        stream = (output / f'{role}-process.log').open('w')
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT, **options)

    def wait_for(predicate, description, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            current = {role: tails[role].poll() for role in roles}
            if predicate(current):
                return current
            dead = [role for role, process in processes.items() if process.poll() is not None]
            if dead:
                raise RuntimeError(f'{description}: processes exited: {dead}')
            time.sleep(.5)
        raise RuntimeError('timeout: ' + description)

    def run_step(tag, commands, participants=roles, timeout=None):
        """Publish one single-use step per role and wait until each has run it."""
        starts = {role: len(tails[role].poll()) for role in participants}
        for role in participants:
            step_index[role] += 1
            name = step_name(role, step_index[role])
            write_script(games[role] / name, [f'echo SOAK_BEGIN_{tag}', *commands.get(role, []), f'echo SOAK_DONE_{tag}', 'waitMsec 2000'])
            point(role, 'soak_step.cfg', 'exec ' + name)
        begun, finished = set(), set()
        deadline = time.monotonic() + (timeout or args.step_timeout)
        while finished != set(participants):
            if time.monotonic() > deadline:
                raise RuntimeError(f'timeout: {tag} (begun {sorted(begun)}, finished {sorted(finished)})')
            for role in participants:
                text = tails[role].poll()[starts[role]:]
                if role not in begun and f'\nSOAK_BEGIN_{tag}' in text:
                    point(role, 'soak_step.cfg', '// idle')
                    begun.add(role)
                if f'\nSOAK_DONE_{tag}' in text:
                    finished.add(role)
            dead = [role for role, process in processes.items() if process.poll() is not None]
            if dead:
                raise RuntimeError(f'{tag}: processes exited: {dead}')
            time.sleep(.2)
        steps_run[tag] = participants
        return {role: tails[role].text[starts[role]:].split(f'SOAK_BEGIN_{tag}', 1)[-1].split(f'SOAK_DONE_{tag}', 1)[0]
                for role in participants}

    def match_running(text):
        """Settled in warmup (1) or game on (3) since the last phase change or
        map restart, so neither a restart nor a countdown swallows speech. With
        no frag or time limit a casual match can stay in warmup for the whole soak."""
        transitions = list(re.finditer(r'MP match phase: \d+ -> (\d+)', text))
        if transitions and transitions[-1][1] not in ('1', '3'):
            return False
        restarts = list(re.finditer(r'Game Map Restart', text))
        start = max(transitions[-1].end() if transitions else 0, restarts[-1].end() if restarts else 0)
        states = re.findall(r'^MP_STATE .*?\bphase=(\d+) ', text[start:], re.M)
        return len(states) >= 4 and len(set(states[-4:])) == 1 and states[-1] in ('1', '3')

    rows, events, checks, steps_run = [], [], {}, {}
    previous = {}
    error = None
    soak_start = None
    soak_end = None
    reconnect = {}
    try:
        launch('server')
        wait_for(lambda logs: 'MP_STATE' in logs['server'], 'dedicated map', args.startup_timeout)
        launch('host')
        wait_for(lambda logs: re.search(r'MP_PLAYER slot=0 .*spectating=0 wantSpectate=0 ingame=1', logs['server']) is not None,
                 'host gameplay', args.startup_timeout)
        launch('peer')
        wait_for(lambda logs: re.search(r'MP_PLAYER slot=1 .*spectating=0 wantSpectate=0 ingame=1', logs['server']) is not None and
                 all(match_running(logs[role]) for role in roles), 'peer gameplay', args.startup_timeout)
        for role in roles:
            point(role, 'soak_poll.cfg', '// idle')
        # Baseline every counter, so the first interval's deltas start at zero.
        baseline = run_step('BASELINE', {'server': ['serverVoiceStats'], 'host': ['listVoiceChat'], 'peer': ['listVoiceChat']})
        previous = {role: parse_section(text) for role, text in baseline.items()}
        soak_start = time.monotonic()
        # Include a measured burst at or after the requested endpoint. Merely
        # scheduling duration/interval bursts starts the last one an interval
        # early, and shutdown/analysis time is not live-session evidence.
        count = math.ceil(args.duration / args.interval) + 1
        reconnect_at = None if args.no_reconnect else count // 2
        schedule_shift = 0.0  # the reconnect pauses the schedule instead of compressing later gaps
        for index in range(count):
            target = soak_start + schedule_shift + index * args.interval
            while time.monotonic() < target:
                time.sleep(min(1.0, target - time.monotonic()))
                for role, process in processes.items():
                    if process.poll() is not None:
                        raise RuntimeError(f'interval {index}: {role} exited')
            kind = CYCLE[index % len(CYCLE)]
            if index == reconnect_at:
                kind = 'pre_reconnect'
            elif reconnect_at is not None and index == reconnect_at + 1:
                kind = 'post_reconnect'
            burst = BURST_SECONDS.get(kind, 5)
            talkers = {'host': ('host',), 'peer': ('peer',), 'silent': ()}.get(kind, ('host', 'peer'))
            if kind == 'late':
                proxy.delay_msec = 250
            tag = f'I{index:04d}'
            if kind == 'pre_reconnect':
                # Scope recipient mute and server moderation to this connection.
                # Apply both before anyone talks: issued in the talk step, the
                # peer's loop can start speaking before the server's loop has
                # run its moderation command, and those first frames are
                # legitimately forwarded.
                run_step(tag + '_MUTE', {'server': ['serverVoiceMute 1 1'], 'host': ['clientVoiceMute soak_peer']},
                         participants=('server', 'host'))
            mid, rest = max(500, burst * 500), burst * 500 + 1500
            client_commands = {}
            for role in ('host', 'peer'):
                lines = []
                if role in talkers:
                    lines.append(f'testVoiceChat {burst}')
                lines += [f'waitMsec {mid}', f'echo SOAK_MID_{tag}', 'listVoiceChat', f'waitMsec {rest}', f'echo SOAK_END_{tag}', 'listVoiceChat', 'testVoiceChat 0']
                client_commands[role] = lines
            client_commands['server'] = [f'waitMsec {mid + rest}', f'echo SOAK_END_{tag}', 'serverVoiceStats']
            started = time.monotonic()
            sections = run_step(tag, client_commands)
            proxy.delay_msec = 0
            parsed = {}
            for role, text in sections.items():
                mid_text, _, end_text = text.partition(f'SOAK_END_{tag}')
                parsed[role] = {'mid': parse_section(mid_text.partition(f'SOAK_MID_{tag}')[2]), 'end': parse_section(end_text)}

            def status_delta(role, key):
                now, before = parsed[role]['end']['status'], previous[role]['status']
                return now[key] - before[key] if now and before else None

            def admission(role, sender):
                """Merged MID/END admission for one sender at one receiver."""
                mid_row, end_row = parsed[role]['mid']['admission'].get(sender), parsed[role]['end']['admission'].get(sender)
                if not end_row:
                    return None
                before = previous[role]['admission'].get(sender)
                same = before and before['generation'] == end_row['generation']
                return {'generation': end_row['generation'],
                        'accepted': end_row['accepted'] - (before['accepted'] if same else 0),
                        'rejected': end_row['rejected'] - (before['rejected'] if same else 0),
                        'max_added_ms': max(end_row['interval_max_added_ms'], mid_row['interval_max_added_ms'] if mid_row else 0),
                        'baseline_shift_ms': end_row['baseline_shift_ms']}

            server_end = parsed['server']['end']['server']
            server_rows = {}
            for client, row in server_end.items():
                before = previous['server']['server'].get(client)
                same = before and before['generation'] == row['generation']
                server_rows[client] = {key: row[key] - (before[key] if same else 0) for key in ('accepted', 'rejected', 'forwarded', 'refused')}
                server_rows[client].update(generation=row['generation'], max_added_ms=row['interval_max_added_ms'],
                                           baseline_shift_ms=row['baseline_shift_ms'])
            row = {
                'interval': index, 'kind': kind, 'burst_seconds': burst if talkers else 0, 'talkers': list(talkers),
                'elapsed_s': round(started - soak_start, 1), 'step_s': round(time.monotonic() - started, 1),
                'cpu_percent': cpu.sample(), 'free_commit_gb': round((system_memory()[0] or 0) / 2**30, 2),
                'captured': {role: status_delta(role, 'captured') for role in ('host', 'peer')},
                'played': {role: status_delta(role, 'played') for role in ('host', 'peer')},
                'rejected': {role: status_delta(role, 'rejected') for role in ('host', 'peer')},
                'playback_drops': {role: status_delta(role, 'playback_drops') for role in ('host', 'peer')},
                'capture_opens': {role: parsed[role]['end']['status']['capture_opens'] for role in ('host', 'peer')},
                'max_queued': max([entry['queued'] for role in ('host', 'peer') for entry in parsed[role]['mid']['playback']] or [0]),
                'receiver_admission': {'host_from_peer': admission('host', 1), 'peer_from_host': admission('peer', 0)},
                'server': {str(client): value for client, value in server_rows.items()},
                'memory_mib': {role: {key: round(value / 2**20, 1) for key, value in (process_memory(process) or {}).items()}
                               for role, process in processes.items()},
                'proxy': {'packets': proxy.packets, 'dropped': proxy.dropped, 'maximum_pending': proxy.max_pending},
            }
            # Expectations for this interval. 10 frames per burst second is 20%
            # of nominal; a starved host still clears it, permanent silence never does.
            floor = 10 * burst
            hears = {'host': 'peer' in talkers, 'peer': 'host' in talkers}
            if kind == 'pre_reconnect':
                hears['host'] = False  # recipient mute plus server moderation of slot 1
            if kind == 'late':
                row['expect'] = {'no_late_playback': row['played']['host'] == 0 and row['played']['peer'] == 0,
                                 'late_rejected': (row['rejected']['peer'] or 0) > 0 and server_rows.get(1, {}).get('rejected', 0) > 0}
            else:
                row['expect'] = {f'{role}_hears': (row['played'][role] >= floor) if hears[role] else (row['played'][role] == 0)
                                 for role in ('host', 'peer')}
            row['expect']['bounded_queue'] = row['max_queued'] <= MAX_QUEUED
            row['expect']['added_delay_bounded'] = all(value['max_added_ms'] <= MAX_ADDED_MSEC for value in
                                                       [*filter(None, row['receiver_admission'].values()), *server_rows.values()])
            row['passed'] = all(row['expect'].values())
            rows.append(row)
            with intervals_path.open('a') as stream:
                stream.write(json.dumps(row) + '\n')
            print(f"interval {index} {kind}: passed={row['passed']} played={row['played']} rejected={row['rejected']} "
                  f"cpu={row['cpu_percent']} step={row['step_s']}s", flush=True)
            previous = {role: parsed[role]['end'] for role in roles}

            if kind == 'pre_reconnect':
                old = {'host_generation': (row['receiver_admission']['host_from_peer'] or {}).get('generation'),
                       'server_generation': server_rows.get(1, {}).get('generation'),
                       'server_accepted_total': server_end.get(1, {}).get('accepted')}
                for role in roles:
                    point(role, 'soak_poll.cfg', 'openq4_reportMPState')
                starts = {role: len(tails[role].poll()) for role in roles}
                step_index['peer'] += 1
                name = step_name('peer', step_index['peer'])
                # If the loop does not survive the new connection, the map-load
                # hook restarts it; a surviving loop starves the appended copy.
                write_script(games['peer'] / name, ['echo SOAK_BEGIN_RECONNECT', 'waitMsec 2000',
                             'set g_autoExecAfterMapLoad soak_start.cfg', 'disconnect', 'waitMsec 1500',
                             'set ui_name soak_returned', f'connect 127.0.0.1:{args.port + 10}'])
                point('peer', 'soak_step.cfg', 'exec ' + name)
                wait_for(lambda logs: '\nSOAK_BEGIN_RECONNECT' in logs['peer'][starts['peer']:], 'reconnect step', args.step_timeout)
                point('peer', 'soak_step.cfg', '// idle')
                steps_run['RECONNECT'] = ('peer',)
                reconnect_started = time.monotonic()
                wait_for(lambda logs: 'soak_returned' in logs['server'][starts['server']:] and
                         re.search(r'MP_PLAYER slot=1 .*spectating=0 wantSpectate=0 ingame=1', logs['server'][starts['server']:]) is not None and
                         all(match_running(logs[role][starts[role]:]) for role in roles), 'reconnected match running', args.startup_timeout)
                for role in roles:
                    point(role, 'soak_poll.cfg', '// idle')
                reconnect = {'old': old, 'seconds': round(time.monotonic() - reconnect_started, 1), 'after_interval': index}
                schedule_shift += time.monotonic() - reconnect_started
                events.append({'event': 'reconnect', 'elapsed_s': round(time.monotonic() - soak_start, 1), **reconnect})
                # The new connection starts its own counters; rebaseline them.
                rebased = run_step('REBASE', {'server': ['serverVoiceStats'], 'host': ['listVoiceChat'], 'peer': ['listVoiceChat']})
                previous = {role: parse_section(text) for role, text in rebased.items()}
            if kind == 'post_reconnect':
                new_host = (row['receiver_admission']['host_from_peer'] or {}).get('generation')
                new_server = server_rows.get(1, {}).get('generation')
                reconnect['new'] = {'host_generation': new_host, 'server_generation': new_server,
                                    'server_accepted_total': server_end.get(1, {}).get('accepted')}
                old = reconnect.get('old', {})
                checks['reconnect_identity'] = (new_host is not None and new_server is not None and
                                                new_host != old.get('host_generation') and new_server != old.get('server_generation'))
                checks['reconnect_clears_mute_and_moderation'] = row['played']['host'] >= floor
                # Admission and its counters belong to the connection: the new
                # one starts from zero rather than continuing the old totals.
                checks['reconnect_restarts_admission'] = (old.get('server_accepted_total') is not None and
                                                          reconnect['new']['server_accepted_total'] is not None and
                                                          reconnect['new']['server_accepted_total'] < old['server_accepted_total'])
        soak_end = time.monotonic()
        checks['complete'] = True
    except Exception as exc:
        soak_end = time.monotonic()
        error = f'{type(exc).__name__}: {exc}'
        checks['complete'] = False
    finally:
        for role, process in processes.items():
            if process.poll() is None:
                step_index[role] += 1
                write_script(games[role] / step_name(role, step_index[role]), ['testVoiceChat 0', 'quit'])
                point(role, 'soak_step.cfg', 'exec ' + step_name(role, step_index[role]))
        for process in processes.values():
            try:
                process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                process.kill()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    pass
        for stream in streams:
            stream.close()
        proxy.close()

    live_seconds = soak_end - soak_start if soak_start is not None and soak_end is not None else 0
    soak_seconds = round(live_seconds, 1)
    talk_rows = [row for row in rows if row['kind'] not in ('late', 'silent')]
    checks['clean_exit'] = all(process.returncode == 0 for process in processes.values())
    checks['duration_met'] = live_seconds >= args.duration
    checks['every_interval_passed'] = bool(rows) and all(row['passed'] for row in rows)
    checks['late_probes_rejected'] = any(row['kind'] == 'late' for row in rows) and all(row['passed'] for row in rows if row['kind'] == 'late')
    checks['still_admitting_at_end'] = bool(talk_rows) and talk_rows[-1]['passed']
    checks['no_microphone'] = all(value == 0 for row in rows for value in row['capture_opens'].values())
    # A step must run exactly once in each participant.
    duplicates = {tag: {role: len(re.findall(rf'^SOAK_BEGIN_{tag}[ \t]*$', tails[role].poll(), re.M)) for role in participants}
                  for tag, participants in steps_run.items()}
    duplicates = {tag: counts for tag, counts in duplicates.items() if any(count != 1 for count in counts.values())}
    checks['steps_ran_once'] = not duplicates
    if not args.no_reconnect:
        checks.setdefault('reconnect_identity', False)
        checks.setdefault('reconnect_clears_mute_and_moderation', False)
        checks.setdefault('reconnect_restarts_admission', False)
    # Memory: compare the warmed first quarter with the final quarter per process.
    memory = {}
    for role in roles:
        series = [(row['interval'], row['memory_mib'].get(role, {}).get('private')) for row in rows]
        series = [(index, value) for index, value in series if value is not None]
        if role == 'peer' and reconnect.get('after_interval') is not None:
            series = [(index, value) for index, value in series if index > reconnect['after_interval'] + 1]
        if len(series) >= 4:
            quarter = max(1, len(series) // 4)
            early, late = max(value for _, value in series[:quarter]), max(value for _, value in series[-quarter:])
            memory[role] = {'early_max_mib': early, 'late_max_mib': late, 'growth_mib': round(late - early, 1)}
    checks['memory_bounded'] = bool(memory) and all(value['growth_mib'] <= 64 for value in memory.values())
    tones = {}
    for role in ('host', 'peer'):
        path = saves[role] / 'mix.wav'
        try:
            tones[role] = chunk_tone_peaks(path) if path.is_file() else []
        except (AssertionError, OSError, struct.error) as exc:
            tones[role] = []
            events.append({'event': 'tone_analysis_failed', 'role': role, 'error': str(exc)})
    checks['rendered_tone_late_in_session'] = all(len(values) >= 2 and max(values[-10:]) > .02 for values in tones.values())

    # Baseline drift per sender path and connection generation. The first
    # frame of a connection fixes the starting offset, so a late first frame
    # shows up as a one-off negative shift; drift is the slope afterwards.
    paths = {}
    for row in rows:
        for name, entry in [*row['receiver_admission'].items(), *(('server_from_' + client, entry) for client, entry in row['server'].items())]:
            if entry and entry.get('generation') is not None:
                paths.setdefault(f"{name}@{entry['generation']}", []).append((row['elapsed_s'], entry['baseline_shift_ms']))
    drift = {}
    for name, series in paths.items():
        span = series[-1][0] - series[0][0]
        if span >= 600:
            settled = [sample for sample in series if sample[0] >= series[0][0] + min(600, span / 4)]
            start, end = settled[0], series[-1]
            drift[name] = {'span_s': round(end[0] - start[0], 1), 'shift_change_ms': end[1] - start[1],
                           'ms_per_hour': round((end[1] - start[1]) * 3600 / max(1.0, end[0] - start[0]), 2),
                           'first_shift_ms': series[0][1], 'last_shift_ms': end[1]}

    def summary(key_path):
        values = [value for value in key_path if value is not None]
        return {'min': min(values), 'max': max(values)} if values else None
    shifts = [entry['baseline_shift_ms'] for row in rows for entry in [*filter(None, row['receiver_admission'].values()),
                                                                         *row['server'].values()]]
    added = [entry['max_added_ms'] for row in rows for entry in [*filter(None, row['receiver_admission'].values()),
                                                                   *row['server'].values()] if row['kind'] != 'late']
    totals = {
        'intervals': len(rows), 'intervals_passed': sum(row['passed'] for row in rows),
        'played': {role: sum(row['played'][role] or 0 for row in rows) for role in ('host', 'peer')},
        'captured': {role: sum(row['captured'][role] or 0 for row in rows) for role in ('host', 'peer')},
        'receiver_rejected_in_talk_intervals': {role: sum(row['rejected'][role] or 0 for row in talk_rows) for role in ('host', 'peer')},
        'receiver_rejected_in_late_probes': {role: sum(row['rejected'][role] or 0 for row in rows if row['kind'] == 'late') for role in ('host', 'peer')},
        'server_accepted': sum(entry['accepted'] for row in rows for entry in row['server'].values()),
        'server_rejected': sum(entry['rejected'] for row in rows for entry in row['server'].values()),
        'server_refused_sends': sum(entry['refused'] for row in rows for entry in row['server'].values()),
        'max_added_ms_outside_late_probes': max(added) if added else None,
        'baseline_shift_ms': summary(shifts),
        'max_queued': max([row['max_queued'] for row in rows] or [0]),
        'cpu_percent': summary([row['cpu_percent'] for row in rows]),
        'free_commit_gb': summary([row['free_commit_gb'] for row in rows]),
    }
    report = {
        'passed': all(checks.values()), 'checks': checks, 'error': error, 'soak_seconds': soak_seconds,
        'requested_duration_s': args.duration, 'interval_s': args.interval, 'render_api': args.render_api,
        'totals': totals, 'memory': memory, 'reconnect': reconnect, 'events': events, 'repeated_steps': duplicates,
        'baseline_drift': drift,
        'tone_peaks_per_minute': tones, 'proxy': {'packets': proxy.packets, 'dropped': proxy.dropped, 'maximum_pending': proxy.max_pending},
        'microphone': False, 'host_input': False, 'output': 'OpenAL wave (mono 16 kHz)',
        'clock_note': 'all peers share one host clock source; observed drift is reported, not simulated',
        'start_free_commit_gb': round((free_commit or 0) / 2**30, 2), 'commit_limit_gb': round((commit_limit or 0) / 2**30, 2),
        'engine_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
        'server_sha256': hashlib.sha256(server_exe.read_bytes()).hexdigest(),
        'game_sha256': hashlib.sha256((runtime / 'baseoq4/game_x64.dll').read_bytes()).hexdigest() if os.name == 'nt' else None,
        'harness_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'result': str(output / 'result.json'), 'checks': checks, 'error': error, 'totals': totals}, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
