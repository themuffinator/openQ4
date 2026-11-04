#!/usr/bin/env python3
"""Qualify real local voice peers with synthetic PCM, never microphone or host input."""
from __future__ import annotations

import argparse
import array
import heapq
import hashlib
import json
import math
import os
import re
import select
import socket
import struct
import subprocess
import threading
import time
from pathlib import Path

from mp_round_remote_smoke import read_log, verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]


class LocalProxy:
    """A bounded loopback-only impairment path for this harness's peer."""
    def __init__(self, port, server_port):
        self.front, self.back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM), socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.front.bind(('127.0.0.1', port))
        self.back.bind(('127.0.0.1', 0))
        self.server, self.client = ('127.0.0.1', server_port), None
        self.loss_every = self.delay_msec = self.jitter_msec = 0
        self.packets = self.dropped = self.max_pending = 0
        self.pending = []
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop.is_set():
            readable, _, _ = select.select([self.front, self.back], [], [], .005)
            for source in readable:
                try:
                    data, address = source.recvfrom(65536)
                except ConnectionResetError:
                    continue  # Windows reports ICMP from a peer closing its UDP socket.
                if source is self.front:
                    self.client = address
                    destination, target = self.server, self.back
                else:
                    destination, target = self.client, self.front
                if not destination:
                    continue
                self.packets += 1
                if (self.loss_every and self.packets % self.loss_every == 0) or len(self.pending) >= 256:
                    self.dropped += 1
                    continue
                delay = self.delay_msec + self.jitter_msec * (self.packets % 3)
                heapq.heappush(self.pending, (time.monotonic() + delay / 1000, self.packets, target, destination, data))
                self.max_pending = max(self.max_pending, len(self.pending))
            while self.pending and self.pending[0][0] <= time.monotonic():
                _, _, target, destination, data = heapq.heappop(self.pending)
                try:
                    target.sendto(data, destination)
                except ConnectionResetError:
                    self.dropped += 1

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        self.front.close()
        self.back.close()


def tone_peak(path):
    """Measure rendered 400 Hz PCM in OpenAL's extensible float WAV output."""
    data = path.read_bytes()
    offset, pcm, channels, rate = 12, None, 0, 0
    while offset + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, offset)
        payload = offset + 8
        if tag == b'fmt ':
            kind, channels, rate, _, _, bits = struct.unpack_from('<HHIIHH', data, payload)
            assert bits == 32 and (kind == 3 or (kind == 65534 and struct.unpack_from('<I', data, payload + 24)[0] == 3))
        elif tag == b'data':
            pcm = array.array('f', data[payload:payload + min(size, len(data) - payload)])
        offset = payload + size + (size & 1)
    assert pcm is not None and channels and rate
    # Each 20 ms window contains eight complete synthetic cycles. Downsample
    # only for this frequency measurement; do not rewrite the captured audio.
    stride, count = rate // 8000, 160
    sine = [math.sin(2 * math.pi * 400 * i / 8000) for i in range(count)]
    cosine = [math.cos(2 * math.pi * 400 * i / 8000) for i in range(count)]
    peak = 0.0
    for start in range(0, len(pcm) - count * stride * channels, rate * channels // 10):
        samples = [pcm[start + i * stride * channels] for i in range(count)]
        real = sum(value * cosine[i] for i, value in enumerate(samples))
        imag = sum(value * sine[i] for i, value in enumerate(samples))
        peak = max(peak, 2 * math.hypot(real, imag) / count)
    return peak


def main() -> int:
    harness_sha256 = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / '.tmp/audio/voice-peers')
    parser.add_argument('--port', type=int, default=28941)
    parser.add_argument('--timeout', type=int, default=360)
    parser.add_argument('--dedicated', action='store_true', help='use the staged dedicated server and three separate clients')
    parser.add_argument('--dummy-capture', action='store_true',
                        help='also test local capture with SDL dummy input (silence); never opens a microphone')
    parser.add_argument('--size', default='960x540', help='client window; software rendering (llvmpipe) keeps the 50 Hz '
                        'voice cadence only in a small one')
    parser.add_argument('--render-api', default='gl', help='client r_renderApi; vulkan renders hidden windows offscreen, '
                        'which avoids hidden GL present stalls while the display is off')
    args = parser.parse_args()
    output, runtime = args.output.resolve(), ROOT / '.install'
    width, height = args.size.split('x')
    verify_runtime_is_current(runtime)
    output.mkdir(parents=True, exist_ok=True)
    exe = runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    server_exe = runtime / ('openQ4-ded_x64.exe' if os.name == 'nt' else 'openQ4-ded_x64') if args.dedicated else exe
    verify_runtime_is_current(runtime, server_exe.name)
    roles = ('host', 'peer', 'spectator', 'server') if args.dedicated else ('host', 'peer', 'spectator')
    server_role = 'server' if args.dedicated else 'host'
    saves = {role: output / role for role in roles}
    games = {role: saves[role] / 'baseoq4' for role in roles}
    logs = {role: games[role] / 'logs/openq4.log' for role in roles}
    polls = {role: games[role] / 'voice_poll.cfg' for role in roles}
    processes, streams, phases, checks, clock_drift = {}, [], {}, {}, {}
    proxy = LocalProxy(args.port + 10, args.port)

    def loop(path: Path) -> None:
        write_script(path, ['// Waiting for the next harness phase.'])

    def select_step(path: Path) -> str:
        return f'set voiceHarnessStep "exec {path.name}"'

    def start_script(path: Path) -> list[str]:
        return [select_step(path), 'set voiceHarnessLoop "exec voice_driver.cfg"', 'exec "voice_driver.cfg"']

    for role in roles:
        games[role].mkdir(parents=True, exist_ok=True)
        logs[role].unlink(missing_ok=True)
        loop(polls[role])
        (games[role] / 'voice_unavailable.cfg').unlink(missing_ok=True)
        # Never replace the driver while it is being read. A transient failure
        # to open a replaced phase file returns here and retries next tick;
        # the former self-replacing loop permanently stopped on that failure.
        # vstr appends here (it does not insert). Append both the phase and the
        # next driver call in order, keeping only one iteration queued at a time.
        # Underscores keep the unquoted filename inside each CVar one token.
        write_script(games[role] / 'voice_driver.cfg', ['waitMsec 250', 'openq4_reportMPState',
                                                       'vstr voiceHarnessStep', 'vstr voiceHarnessLoop'])
        write_script(games[role] / 'voice_start.cfg', start_script(polls[role]))
        (saves[role] / 'alsoft.conf').write_text('[general]\ndrivers=wave\nchannels=stereo\nhrtf=false\n[wave]\nfile=' + str(saves[role] / 'mix.wav') + '\n')

    common = {
        'fs_basepath': str(args.assets.resolve()), 'fs_game': 'baseoq4', 'com_gameMode': 'MP',
        'r_fullscreen': '0', 'r_borderless': '0', 'r_fullscreenDesktop': '0', 'r_borderlessDefaultMigrated': '1',
        'r_mode': '-1', 'r_customWidth': width, 'r_customHeight': height, 'r_windowWidth': width, 'r_windowHeight': height,
        'r_hiddenWindow': '1', 'r_renderApi': args.render_api, 'r_multiSamples': '0', 'r_swapInterval': '0', 'com_maxfps': '60',
        'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0', 's_noSound': '0', 's_muteUnfocused': '0',
        's_volume': '.5', 's_musicVolume': '0', 's_voiceChatCapture': '0', 's_voiceChatSend': '1', 's_voiceChatReceive': '1',
        's_voiceChatEcho': '0', 's_voiceVolume': '1', 'logFile': '2', 'logFileName': 'logs/openq4.log', 'developer': '1',
        'g_voiceChatDebug': '3', 'ui_retained': '1', 'ui_autoJoin': '1', 'ui_spectate': 'Play', 'ui_ready': 'Ready',
        'net_allowCheats': '1', 'com_skipLoadingContinue': '1', 'g_autoExecAfterMapLoad': 'voice_start.cfg',
        'g_autoExecAfterMapLoadDelayMs': '500', 'win_allowMultipleInstances' if os.name == 'nt' else 'sys_allowMultipleInstances': '1'
    }

    def launch(role: str) -> None:
        cvars = dict(common, fs_savepath=str(saves[role]), fs_devpath=str(saves[role]), ui_name='voice_' + role,
                     net_port=str(args.port + 1 + roles.index(role)))
        if role == server_role:
            cvars.update(net_port=str(args.port), net_serverDedicated='2' if args.dedicated else '0', net_LANServer='1', net_ip='127.0.0.1', si_pure='0',
                         si_maxPlayers='4', si_gameType='DM', si_minPlayers='1', si_warmup='0', si_useReady='0',
                         si_timeLimit='0', bot_minPlayers='0', g_matchProfile='casual')
        elif role == 'spectator':
            cvars.update(ui_autoJoin='0', ui_spectate='Spectate')
        command = [str(server_exe if role == server_role else exe)]
        for key, value in cvars.items():
            command += ['+set', key, value]
        assert len(cvars) + 1 <= 64
        command += ['+spawnServer', 'mp/q4dm1'] if role == server_role else ['+connect', f'127.0.0.1:{args.port + (10 if role == "peer" else 0)}']
        (output / f'{role}-launch.json').write_text(json.dumps(command, indent=2))
        options = {'env': {**os.environ, 'ALSOFT_CONF': str(saves[role] / 'alsoft.conf'), 'ALSOFT_DRIVERS': 'wave'}}
        if args.dummy_capture:
            # A single explicit driver, with no physical-device fallback.
            options['env'].update(SDL_AUDIO_DRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_AUDIO_DUMMY_TIMESCALE='1')
        if os.name == 'nt':
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
        stream = (output / f'{role}-process.log').open('w')
        streams.append(stream)
        processes[role] = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT, **options)

    deadline = time.monotonic() + args.timeout

    def wait_for(predicate, description):
        while time.monotonic() < deadline:
            current = {role: read_log(logs[role]) for role in roles}
            if predicate(current):
                return current
            dead = [role for role, process in processes.items() if process.poll() is not None]
            if dead:
                raise RuntimeError(f'{description}: processes exited: {dead}')
            time.sleep(.2)
        raise RuntimeError('timeout: ' + description)

    def phase(name, commands, active_msec=1700):
        for role in roles:
            next_poll = games[role] / f'voice_wait_{name}.cfg'
            loop(next_poll)
            write_script(polls[role], [select_step(next_poll), f'echo VOICE_BEGIN_{name}', 'listVoiceChat', *commands.get(role, []),
                         f'waitMsec {active_msec}', f'echo VOICE_ACTIVE_{name}', 'listVoiceChat', 'testVoiceChat 0',
                         'waitMsec 300', f'echo VOICE_END_{name}', 'listVoiceChat', f'echo VOICE_DONE_{name}'])
            polls[role] = next_poll
        current = wait_for(lambda logs: all(f'\nVOICE_DONE_{name}' in logs[role] for role in roles), name)
        data = {}
        for role in roles:
            sections = {}
            for stage in ('BEGIN', 'ACTIVE', 'END'):
                section = re.search(rf'^VOICE_{stage}_{name}[ \t]*\n(.*?)(?=^VOICE_(?:BEGIN|ACTIVE|END|DONE)_|\Z)',
                                    current[role], re.M | re.S)
                assert section, (role, name, stage)
                status = re.search(r'^VOICE_STATUS (.*)', section[1], re.M)
                assert status, (role, name, stage)
                values = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', status[1])}
                clock = re.search(r'^VOICE_CLOCK (.*)', section[1], re.M)
                values['clock'] = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', clock[1])} if clock else {}
                playback = re.findall(r'^VOICE_PLAYBACK (.*)', section[1], re.M)
                values['playback'] = [{key: float(value) for key, value in re.findall(r'(\w+)=(-?[\d.]+)', line)} for line in playback]
                admission = re.findall(r'^VOICE_ADMISSION (.*)', section[1], re.M)
                values['admission'] = [{key: float(value) for key, value in re.findall(r'(\w+)=(-?[\d.]+)', line)} for line in admission]
                sections[stage] = values
            data[role] = sections
            phase_text = current[role].split(f'VOICE_BEGIN_{name}', 1)[1].split(f'VOICE_DONE_{name}', 1)[0]
            sections['snapshots'] = [tuple(map(int, row)) for row in re.findall(
                r'client (\d+): (\d+) snapshots/s \(asked \d+\), avg \d+ bytes, \d+ refused by rate, cap (\d+) B/s', phase_text)]
        phases[name] = data
        print('voice phase completed:', name, flush=True)
        return data

    def delta(data, role):
        return data[role]['END']['played'] - data[role]['BEGIN']['played']

    def server_commands(commands, clients=None):
        result = {role: list(values) for role, values in (clients or {}).items()}
        result.setdefault(server_role, [])[:0] = commands
        return result

    error = None
    try:
        if args.dedicated:
            launch('server')
            wait_for(lambda logs: 'MP_STATE' in logs['server'], 'dedicated map')
        launch('host')
        wait_for(lambda logs: 'MP_PLAYER slot=0' in logs['host'], 'listen host gameplay')
        # Negative control: force one failed exec, then publish its file. The
        # immutable driver must recover without replaying any completed phase.
        missing = games['host'] / 'voice_unavailable.cfg'
        write_script(polls['host'], [select_step(missing)])
        absent = wait_for(lambda logs: "couldn't exec voice_unavailable.cfg" in logs['host'], 'missing phase control')
        boundary = len(absent['host'])
        loop(missing)
        polls['host'] = missing
        wait_for(lambda logs: 'execing voice_unavailable.cfg' in logs['host'][boundary:], 'phase control recovery')
        checks['control_read_recovery'] = True
        launch('peer')
        wait_for(lambda logs: re.search(r'MP_PLAYER slot=1 .*spectating=0 wantSpectate=0 ingame=1', logs['host']) is not None, 'peer gameplay')
        launch('spectator')
        wait_for(lambda logs: 'MP_PLAYER slot=2' in logs['host'] and 'MP_STATE' in logs['spectator'], 'spectator connection')
        if args.dummy_capture:
            denied = phase('CAPTURE_DENIED', {'host': ['set s_voiceChatTest 1']})
            checks['capture_requires_consent'] = denied['host']['END']['capture_opens'] == 0 and denied['host']['END']['captured'] == 0
            local = phase('CAPTURE_LOCAL', {'host': ['set s_voiceChatCapture 1', 'set s_voiceChatEcho 1', 'set s_voiceChatTest 1']})
            checks['local_capture_plays'] = (local['host']['END']['recording'] == 1 and
                                           local['host']['END']['captured'] - local['host']['BEGIN']['captured'] > 20 and delta(local, 'host') > 20)
            off = phase('CAPTURE_OFF', {'host': ['set s_voiceChatTest 0']})
            checks['local_capture_closes'] = (off['host']['END']['recording'] == 0 and
                                             off['host']['END']['captured'] == off['host']['BEGIN']['captured'] and
                                             not off['host']['END']['playback'])
            again = phase('CAPTURE_AGAIN', {'host': ['set s_voiceChatTest 1']})
            checks['local_capture_reopens'] = (again['host']['END']['capture_opens'] > off['host']['END']['capture_opens'] and delta(again, 'host') > 20)
            revoked = phase('CAPTURE_REVOKED', {'host': ['set s_voiceChatCapture 0', 'set s_voiceChatEcho 0']})
            checks['local_capture_revoked'] = (revoked['host']['END']['recording'] == 0 and revoked['host']['END']['status'] == 1 and
                                              revoked['host']['END']['captured'] == revoked['host']['BEGIN']['captured'] and
                                              not revoked['host']['END']['playback'])
            checks['local_capture_never_sent'] = all(delta(data, role) == 0 for data in (denied, local, off, again, revoked) for role in ('peer', 'spectator'))
            capture_log = read_log(logs['host']).split('VOICE_BEGIN_CAPTURE_DENIED', 1)[1].split('VOICE_DONE_CAPTURE_REVOKED', 1)[0]
            checks['local_capture_never_sent'] &= 'VOICE_SEND ' not in capture_log and 'VOICE_FORWARD ' not in capture_log
            checks['capture_dummy_driver'] = ('VOICE_CAPTURE_DRIVER dummy' in capture_log and
                                              re.search(r'VOICE_CAPTURE_DRIVER (?!dummy)', capture_log) is None)
        both = phase('BOTH', {'host': ['testVoiceChat 3'], 'peer': ['testVoiceChat 3']})
        checks['both_directions'] = delta(both, 'host') > 20 and delta(both, 'peer') > 20
        muted = phase('MUTED', {'host': ['testVoiceChat 3'], 'peer': ['clientVoiceMute voice_host']})
        checks['recipient_mute'] = delta(muted, 'peer') == 0
        unmuted = phase('UNMUTED', {'host': ['testVoiceChat 3'], 'peer': ['clientVoiceUnMute voice_host']})
        checks['recipient_unmute'] = delta(unmuted, 'peer') > 20
        off = phase('RECEIVE_OFF', {'host': ['testVoiceChat 3'], 'peer': ['set s_voiceChatReceive 0']})
        checks['receive_off'] = delta(off, 'peer') == 0
        volume = phase('VOLUME', {'host': ['testVoiceChat 3'], 'peer': ['set s_voiceChatReceive 1', 'set s_voiceVolume .25', 'set timescale .5']})
        checks['voice_volume'] = delta(volume, 'peer') > 20 and any(abs(row['gain'] - .125) < .01 for row in volume['peer']['ACTIVE']['playback'])
        checks['realtime_pitch'] = bool(volume['peer']['ACTIVE']['playback']) and all(row.get('pitch') == 1.0 for row in volume['peer']['ACTIVE']['playback'])
        silenced = phase('SILENCED', server_commands(['serverVoiceMute 1 1'], {'peer': ['set timescale 1', 'testVoiceChat 3']}))
        checks['server_moderation'] = delta(silenced, 'host') == 0
        restored = phase('RESTORED', server_commands(['serverVoiceMute 1 0'], {'host': ['s_restart', 'testVoiceChat 3'], 'peer': ['s_restart', 'testVoiceChat 3']}))
        checks['restart'] = delta(restored, 'host') > 20 and delta(restored, 'peer') > 20
        proxy.loss_every, proxy.jitter_msec = 5, 15
        loss = phase('LOSS', {'host': ['testVoiceChat 3'], 'peer': ['testVoiceChat 3']})
        checks['loss_and_reorder'] = delta(loss, 'host') > 10 and delta(loss, 'peer') > 10 and proxy.dropped > 0
        proxy.loss_every = proxy.jitter_msec = 0
        proxy.delay_msec = 250
        late = phase('LATE', {'host': ['testVoiceChat 3'], 'peer': ['testVoiceChat 3']})
        checks['late_frames_dropped'] = delta(late, 'host') == 0 and delta(late, 'peer') == 0
        proxy.delay_msec = 0
        congestion = phase('CONGESTED', server_commands(['set net_serverMaxClientRate 2000', 'set com_showAsyncStats 1'], {'host': ['testVoiceChat 3']}))
        checks['congestion_drops_voice'] = (congestion['peer']['END']['played'] == congestion['peer']['ACTIVE']['played'] and
                                            not congestion['peer']['END']['playback'])
        checks['congestion_preserves_snapshots'] = any(client == 1 and count > 0 and cap == 2000 for client, count, cap in congestion[server_role]['snapshots'])
        recovered = phase('RECOVERED', server_commands(['set net_serverMaxClientRate 128000'], {'host': ['testVoiceChat 3'], 'peer': ['testVoiceChat 3']}))
        checks['recovers_after_congestion'] = delta(recovered, 'host') > 20 and delta(recovered, 'peer') > 20
        old_generations = {row['generation'] for row in recovered['host']['ACTIVE']['playback'] if row['client'] == 1}

        # Independent clocks: the peer's voice timestamps run 0.1% slow and then 0.1% fast
        # through 10 s of speech each, ten times a typical crystal's drift and half the
        # admission's 2 ms/s slew. Check the clock actually encoded by the sender,
        # plus delivery/age/rejection bounds. Receiver baseline movement also
        # contains network jitter and cannot establish the drift's sign alone.
        def admitted(data, stage):
            rows = [row for row in data['host'][stage]['admission'] if row.get('client') == 1]
            return rows[-1] if rows else {}

        def drift(name, rate):
            data = phase(name, {'peer': [f'set s_voiceTestClockRate {rate}', 'testVoiceChat 10']}, active_msec=10500)
            begin, end = admitted(data, 'BEGIN'), admitted(data, 'ACTIVE')
            first, last = data['peer']['BEGIN']['clock'], data['peer']['ACTIVE']['clock']
            source_elapsed = stamp_elapsed = shift = expected = None
            if all('source_ms' in c and 'stamp_ms' in c for c in (first, last)):
                source_elapsed = (last['source_ms'] - first['source_ms']) & 0xffffffff
                stamp_elapsed = (last['stamp_ms'] - first['stamp_ms']) & 0xffffffff
                shift = stamp_elapsed - source_elapsed
                expected = source_elapsed * (rate - 1)
            return {'played': delta(data, 'host'),
                    'accepted': end.get('accepted', 0) - begin.get('accepted', 0),
                    'rejected': end.get('rejected', 0) - begin.get('rejected', 0),
                    'baseline_shift_ms': end.get('baseline_shift_ms', 0) - begin.get('baseline_shift_ms', 0),
                    'max_added_ms': end.get('interval_max_added_ms'),
                    'source_elapsed_ms': source_elapsed, 'stamp_elapsed_ms': stamp_elapsed,
                    'injected_shift_ms': shift, 'expected_shift_ms': expected,
                    'clock_matches': source_elapsed is not None and 0 < source_elapsed < 60000 and abs(shift - expected) <= 2}
        drift_slow, drift_fast = drift('DRIFT_SLOW', 0.999), drift('DRIFT_FAST', 1.001)
        phase('DRIFT_END', {'peer': ['set s_voiceTestClockRate 1']})
        clock_drift['slow'], clock_drift['fast'] = drift_slow, drift_fast
        # At least 4 s of the 10 s spoken arrives even where slow clients emit one frame per
        # rendered frame (a dedicated server and three software-paced clients).
        checks['clock_drift_slow_sender'] = drift_slow['played'] > 200 and drift_slow['rejected'] <= drift_slow['accepted'] * .02 and \
            drift_slow['clock_matches'] and drift_slow['injected_shift_ms'] <= -5 and drift_slow['max_added_ms'] <= 120
        checks['clock_drift_fast_sender'] = drift_fast['played'] > 200 and drift_fast['rejected'] <= drift_fast['accepted'] * .02 and \
            drift_fast['clock_matches'] and drift_fast['injected_shift_ms'] >= 5 and drift_fast['max_added_ms'] <= 120
        # Scope both recipient mute and server moderation to this connection.
        phase('BEFORE_RECONNECT', server_commands(['serverVoiceMute 1 1'], {'host': ['clientVoiceMute voice_peer']}))
        before = {role: len(read_log(logs[role])) for role in roles}
        next_poll = games['peer'] / 'voice_reconnected.cfg'
        loop(next_poll)
        # The immutable driver survives disconnect/map loads. Advance its step
        # before disconnecting, and do not start a second driver on the new map.
        write_script(polls['peer'], [select_step(next_poll), 'set g_autoExecAfterMapLoad ""',
                     'disconnect', 'waitMsec 1200', 'set ui_name voice_returned',
                     f'connect 127.0.0.1:{args.port + 10}'])
        polls['peer'] = next_poll
        wait_for(lambda logs: 'voice_returned' in logs[server_role][before[server_role]:] and
                 re.search(r'MP_PLAYER slot=1 .*spectating=0 wantSpectate=0 ingame=1', logs[server_role][before[server_role]:]) is not None and
                 'MP_STATE' in logs['peer'][before['peer']:], 'new connection in reused slot')

        # The disconnect ends the match; the slot can report in-game during the
        # summary/warmup that follows, before the map restart that starts
        # gameplay. Begin only once the server and both talking clients report
        # a running match after their last restart, so neither the server's
        # nor a client's restart can swallow the synthetic speech.
        def match_running(text):
            transitions = list(re.finditer(r'MP match phase: \d+ -> (\d+)', text))
            if transitions and transitions[-1][1] != '3':
                return False
            restarts = list(re.finditer(r'Game Map Restart', text))
            start = max(transitions[-1].end() if transitions else 0, restarts[-1].end() if restarts else 0)
            states = re.findall(r'^MP_STATE .*?\bphase=(\d+) ', text[start:], re.M)
            return bool(states) and states[-1] == '3'
        wait_for(lambda logs: all(match_running(logs[role][before[role]:]) for role in {server_role, 'host', 'peer'}),
                 'reconnected match running')
        reconnected = phase('RECONNECTED', {'host': ['testVoiceChat 3'], 'peer': ['testVoiceChat 3']})
        new_generations = {row['generation'] for row in reconnected['host']['ACTIVE']['playback'] if row['client'] == 1}
        checks['reconnect_identity'] = (delta(reconnected, 'host') > 20 and delta(reconnected, 'peer') > 20 and
                                        bool(old_generations) and bool(new_generations) and old_generations.isdisjoint(new_generations))
        checks['no_spectator_leak'] = all(delta(data, 'spectator') == 0 for data in phases.values())
        if args.dummy_capture:
            checks['capture_only_on_dummy_host'] = all(data[role]['END']['capture_opens'] == 0 for data in phases.values() for role in roles if role != 'host')
        else:
            checks['no_microphone'] = all(data[role]['END']['capture_opens'] == 0 for data in phases.values() for role in roles)
        checks['bounded_playback'] = all(row['queued'] <= 4 for data in phases.values() for role in roles for row in data[role]['ACTIVE']['playback'])
        checks['single_phase_execution'] = all(len(re.findall(rf'^VOICE_BEGIN_{name}[ \t]*$', read_log(logs[role]), re.M)) == 1
                                               for role in roles for name in phases)
    except Exception as exc:
        error = str(exc)
        checks['complete'] = False
    finally:
        for role, process in processes.items():
            if process.poll() is None:
                write_script(polls[role], ['testVoiceChat 0', 'listVoiceChat', 'quit'])
        for process in processes.values():
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    # Still report: an unkillable child is a clean_exit failure,
                    # not a reason to lose the recorded checks and error.
                    pass
        for stream in streams:
            stream.close()
        proxy.close()
    checks['clean_exit'] = all(process.returncode == 0 for process in processes.values())
    checks['rendered_audio'] = all((saves[role] / 'mix.wav').is_file() and (saves[role] / 'mix.wav').stat().st_size > 48000 for role in ('host', 'peer'))
    rendered_tone = {role: tone_peak(saves[role] / 'mix.wav') for role in ('host', 'peer')} if checks['rendered_audio'] else {}
    checks['rendered_voice_tone'] = bool(rendered_tone) and all(peak > .02 for peak in rendered_tone.values())
    report = {'passed': bool(phases) and all(checks.values()), 'checks': checks, 'error': error, 'phases': phases,
              'rendered_tone_peak': rendered_tone, 'clock_drift': clock_drift, 'proxy': {'packets': proxy.packets, 'dropped': proxy.dropped, 'maximum_pending': proxy.max_pending},
              'microphone': False, 'host_input': False, 'output': 'OpenAL wave',
              'capture_driver': 'SDL dummy (silence)' if args.dummy_capture else None,
              'engine_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
              'dedicated': args.dedicated, 'server_sha256': hashlib.sha256(server_exe.read_bytes()).hexdigest(),
              'game_sha256': hashlib.sha256((runtime / 'baseoq4/game-mp_x64.dll').read_bytes()).hexdigest() if os.name == 'nt' else None,
              'harness_sha256': harness_sha256}
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'result': str(output / 'result.json'), 'checks': checks, 'error': error}, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
