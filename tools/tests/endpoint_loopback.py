#!/usr/bin/env python3
"""Record what a real playback endpoint plays, in the endpoint's own channel layout.

This is the measuring side of the real-endpoint audio harnesses: it hears the
operating system's mix for one output device, after OpenAL's backend and the
system mixer, instead of OpenAL Soft's wave writer.

Windows: WASAPI shared-mode loopback through ctypes; no package is needed.
Linux: the PulseAudio or PipeWire monitor source of the sink, through `parec`.
macOS has no system loopback without a third-party virtual device; it is not
supported.

Each recording keeps a timeline of (perf_counter seconds, frames recorded) so a
caller can cut the segment that played between two moments it observed.

    python tools/tests/endpoint_loopback.py --list
    python tools/tests/endpoint_loopback.py --record "Steam Streaming Speakers" --seconds 5 --output out.wav
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import threading
import time
import uuid

import numpy

# WAVEFORMATEXTENSIBLE speaker bits, in channel order.
SPEAKERS = [(0x1, 'FL'), (0x2, 'FR'), (0x4, 'FC'), (0x8, 'LFE'), (0x10, 'BL'), (0x20, 'BR'),
            (0x40, 'FLC'), (0x80, 'FRC'), (0x100, 'BC'), (0x200, 'SL'), (0x400, 'SR')]
PULSE_POSITIONS = {'front-left': 0x1, 'front-right': 0x2, 'front-center': 0x4, 'lfe': 0x8,
                   'rear-left': 0x10, 'rear-right': 0x20, 'front-left-of-center': 0x40,
                   'front-right-of-center': 0x80, 'rear-center': 0x100, 'side-left': 0x200,
                   'side-right': 0x400, 'mono': 0x4}


def channel_names(mask, channels):
    names = [name for bit, name in SPEAKERS if mask & bit]
    if len(names) != channels:
        return [f'ch{i}' for i in range(channels)] if channels != 1 else ['FC']
    return names


def default_mask(channels):
    return {1: 0x4, 2: 0x3, 4: 0x33, 6: 0x60F, 7: 0x70F, 8: 0x63F}.get(channels, 0)


class Recording:
    """Float frames (frames x channels), the layout, and the arrival timeline."""

    def __init__(self, rate, channels, mask, frames, timeline):
        self.rate, self.channels, self.mask = rate, channels, mask
        self.frames, self.timeline = frames, timeline
        self.names = channel_names(mask, channels)

    def frame_at(self, moment):
        """The recorded frame that played at a perf_counter moment (interpolated between packets)."""
        if not self.timeline:
            return 0
        times = [t for t, _ in self.timeline]
        counts = [n for _, n in self.timeline]
        return int(round(numpy.interp(moment, times, counts)))

    def segment(self, start, end):
        a, b = self.frame_at(start), self.frame_at(end)
        return self.frames[max(0, a):max(0, b)]

    def levels(self, frequency, start=None, end=None):
        """Per-channel amplitude of one tone in a segment (the whole recording by default)."""
        data = self.frames if start is None else self.segment(start, end)
        if len(data) < self.rate // 20:
            return [0.0] * self.channels
        t = numpy.arange(len(data)) / self.rate
        window = numpy.hanning(len(data))
        ref = numpy.exp(-2j * numpy.pi * frequency * t) * window
        return [float(2 * abs(numpy.dot(data[:, c], ref)) / window.sum()) for c in range(self.channels)]

    def write_wav(self, path):
        """32-bit float WAVE_FORMAT_EXTENSIBLE, so other tools see the endpoint's layout."""
        data = numpy.ascontiguousarray(self.frames, dtype='<f4').tobytes()
        block = 4 * self.channels
        subformat = uuid.UUID('00000003-0000-0010-8000-00aa00389b71').bytes_le
        fmt = struct.pack('<HHIIHHHHI', 0xFFFE, self.channels, self.rate, self.rate * block, block, 32, 22, 32, self.mask) + subformat
        with open(path, 'wb') as out:
            out.write(b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(data)) + b'WAVE')
            out.write(b'fmt ' + struct.pack('<I', len(fmt)) + fmt)
            out.write(b'data' + struct.pack('<I', len(data)) + data)


def _decode(raw, tag, bits, subformat):
    float_sub = uuid.UUID('00000003-0000-0010-8000-00aa00389b71').bytes_le
    pcm_sub = uuid.UUID('00000001-0000-0010-8000-00aa00389b71').bytes_le
    is_float = tag == 3 or (tag == 0xFFFE and subformat == float_sub)
    is_pcm = tag == 1 or (tag == 0xFFFE and subformat == pcm_sub)
    if is_float and bits == 32:
        return numpy.frombuffer(raw, dtype='<f4')
    if is_pcm and bits == 16:
        return numpy.frombuffer(raw, dtype='<i2').astype(numpy.float32) / 32768.0
    if is_pcm and bits == 32:
        return numpy.frombuffer(raw, dtype='<i4').astype(numpy.float32) / 2147483648.0
    if is_pcm and bits == 24:
        b = numpy.frombuffer(raw, dtype=numpy.uint8).reshape(-1, 3).astype(numpy.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        return (numpy.where(v & 0x800000, v - 0x1000000, v)).astype(numpy.float32) / 8388608.0
    raise RuntimeError(f'unsupported endpoint mix format tag={tag:#x} bits={bits}')


if os.name == 'nt':
    import ctypes as C
    from ctypes import wintypes as W

    class GUID(C.Structure):
        _fields_ = [('bytes', C.c_ubyte * 16)]

    def _guid(text):
        g = GUID()
        C.memmove(C.byref(g), uuid.UUID(text).bytes_le, 16)
        return g

    class PROPERTYKEY(C.Structure):
        _fields_ = [('fmtid', GUID), ('pid', W.DWORD)]

    class PROPVARIANT(C.Structure):
        _fields_ = [('vt', C.c_ushort), ('r1', C.c_ushort), ('r2', C.c_ushort), ('r3', C.c_ushort),
                    ('value', C.c_void_p), ('extra', C.c_void_p)]

    CLSID_MMDeviceEnumerator = _guid('BCDE0395-E52F-467C-8E3D-C4579291692E')
    IID_IMMDeviceEnumerator = _guid('A95664D2-9614-4F35-A746-DE8DB63617E6')
    IID_IAudioClient = _guid('1CB9AD4C-DBFA-4C32-B178-C2F568A703B2')
    IID_IAudioCaptureClient = _guid('C8ADBD64-E71E-48A0-A4DE-185C395CD317')
    PKEY_Device_FriendlyName = PROPERTYKEY(_guid('A45C254E-DF1C-4EFD-8020-67D146A850E0'), 14)
    CLSCTX_ALL, E_RENDER, DEVICE_STATE_ACTIVE, STGM_READ, VT_LPWSTR = 0x17, 0, 1, 0, 31
    AUDCLNT_STREAMFLAGS_LOOPBACK, AUDCLNT_BUFFERFLAGS_SILENT = 0x00020000, 0x2
    _ole32 = C.OleDLL('ole32')
    _free = C.windll.ole32.CoTaskMemFree
    _free.argtypes, _free.restype = [C.c_void_p], None

    def _method(obj, index, *argtypes, restype=C.HRESULT):
        vtable = C.cast(obj, C.POINTER(C.POINTER(C.c_void_p))).contents
        function = C.WINFUNCTYPE(restype, C.c_void_p, *argtypes)(vtable[index])
        return lambda *args: function(obj, *args)

    def _release(obj):
        if obj and obj.value:
            _method(obj, 2, restype=C.c_ulong)()

    def _com_init():
        try:
            _ole32.CoInitializeEx(None, 0)  # COINIT_MULTITHREADED
        except OSError:
            pass  # already initialised in another mode on this thread

    def _mix_format(client):
        pointer = C.c_void_p()
        _method(client, 8, C.POINTER(C.c_void_p))(C.byref(pointer))
        try:
            head = C.string_at(pointer, 18)
            tag, channels, rate, _, block, bits, extra = struct.unpack('<HHIIHHH', head)
            full = C.string_at(pointer, 18 + extra)
            mask, subformat = default_mask(channels), b''
            if tag == 0xFFFE and extra >= 22:
                mask = struct.unpack_from('<I', full, 20)[0]
                subformat = full[24:40]
            return {'tag': tag, 'channels': channels, 'rate': rate, 'block': block, 'bits': bits,
                    'mask': mask, 'subformat': subformat}, pointer
        except Exception:
            _free(pointer)
            raise

    def _devices():
        """Active render endpoints as (id, friendly name, IMMDevice); the caller releases each device."""
        enumerator, collection = C.c_void_p(), C.c_void_p()
        _ole32.CoCreateInstance(C.byref(CLSID_MMDeviceEnumerator), None, CLSCTX_ALL,
                                C.byref(IID_IMMDeviceEnumerator), C.byref(enumerator))
        try:
            _method(enumerator, 3, C.c_int, W.DWORD, C.POINTER(C.c_void_p))(E_RENDER, DEVICE_STATE_ACTIVE, C.byref(collection))
            count = C.c_uint()
            _method(collection, 3, C.POINTER(C.c_uint))(C.byref(count))
            found = []
            for i in range(count.value):
                device, store, text = C.c_void_p(), C.c_void_p(), C.c_void_p()
                _method(collection, 4, C.c_uint, C.POINTER(C.c_void_p))(i, C.byref(device))
                _method(device, 5, C.POINTER(C.c_void_p))(C.byref(text))
                identifier = C.wstring_at(text)
                _free(text)
                _method(device, 4, W.DWORD, C.POINTER(C.c_void_p))(STGM_READ, C.byref(store))
                value = PROPVARIANT()
                _method(store, 5, C.POINTER(PROPERTYKEY), C.POINTER(PROPVARIANT))(C.byref(PKEY_Device_FriendlyName), C.byref(value))
                name = C.wstring_at(value.value) if value.vt == VT_LPWSTR and value.value else identifier
                _ole32.PropVariantClear(C.byref(value))
                _release(store)
                found.append((identifier, name, device))
            return found
        finally:
            _release(collection)
            _release(enumerator)

    def _activate(device):
        client = C.c_void_p()
        _method(device, 3, C.POINTER(GUID), W.DWORD, C.c_void_p, C.POINTER(C.c_void_p))(
            C.byref(IID_IAudioClient), CLSCTX_ALL, None, C.byref(client))
        return client

    def endpoints():
        _com_init()
        result = []
        for identifier, name, device in _devices():
            try:
                client = _activate(device)
                try:
                    fmt, pointer = _mix_format(client)
                    _free(pointer)
                finally:
                    _release(client)
                result.append({'name': name, 'id': identifier, 'channels': fmt['channels'], 'rate': fmt['rate'],
                               'mask': fmt['mask'], 'layout': channel_names(fmt['mask'], fmt['channels'])})
            finally:
                _release(device)
        return result

    class Recorder:
        """WASAPI loopback of one render endpoint, on its own thread."""

        def __init__(self, endpoint):
            self.endpoint = endpoint
            self._stop, self._ready = threading.Event(), threading.Event()
            self._error, self._result = None, None
            self._thread = threading.Thread(target=self._run, daemon=True)

        def start(self, timeout=10):
            self._thread.start()
            if not self._ready.wait(timeout):
                raise RuntimeError(f'loopback did not start on {self.endpoint["name"]}')
            if self._error:
                raise self._error
            return self

        def stop(self):
            self._stop.set()
            self._thread.join(15)
            if self._error:
                raise self._error
            return self._result

        def _run(self):
            client = capture = pointer = None
            try:
                _com_init()
                devices = _devices()
                try:
                    match = [d for d in devices if d[0] == self.endpoint['id']]
                    if not match:
                        raise RuntimeError(f'endpoint {self.endpoint["name"]} is no longer active')
                    client = _activate(match[0][2])
                finally:
                    for _, _, device in devices:
                        _release(device)
                fmt, pointer = _mix_format(client)
                _method(client, 3, C.c_int, W.DWORD, C.c_longlong, C.c_longlong, C.c_void_p, C.c_void_p)(
                    0, AUDCLNT_STREAMFLAGS_LOOPBACK, 2_000_000, 0, pointer, None)
                capture = C.c_void_p()
                _method(client, 14, C.POINTER(GUID), C.POINTER(C.c_void_p))(C.byref(IID_IAudioCaptureClient), C.byref(capture))
                next_size = _method(capture, 5, C.POINTER(C.c_uint32))
                get_buffer = _method(capture, 3, C.POINTER(C.c_void_p), C.POINTER(C.c_uint32), C.POINTER(W.DWORD),
                                     C.POINTER(C.c_uint64), C.POINTER(C.c_uint64))
                release_buffer = _method(capture, 4, C.c_uint32)
                _method(client, 10)()  # Start
                self._ready.set()
                block, rate = fmt['block'], fmt['rate']
                chunks, timeline, total, expected = [], [], 0, None
                size, data, frames, flags = C.c_uint32(), C.c_void_p(), C.c_uint32(), W.DWORD()
                position, qpc = C.c_uint64(), C.c_uint64()
                while not self._stop.is_set():
                    next_size(C.byref(size))
                    while size.value:
                        get_buffer(C.byref(data), C.byref(frames), C.byref(flags), C.byref(position), C.byref(qpc))
                        now, n = time.perf_counter(), frames.value
                        raw = bytes(n * block) if flags.value & AUDCLNT_BUFFERFLAGS_SILENT or not data.value else C.string_at(data, n * block)
                        release_buffer(n)
                        # An idle endpoint sends no packets; keep the device clock's gap as silence.
                        gap = 0 if expected is None else position.value - expected
                        if 0 < gap < rate * 30:
                            chunks.append(bytes(gap * block))
                            total += gap
                        chunks.append(raw)
                        total += n
                        expected = position.value + n
                        timeline.append((now, total))
                        next_size(C.byref(size))
                    time.sleep(0.004)
                _method(client, 11)()  # Stop
                samples = _decode(b''.join(chunks), fmt['tag'], fmt['bits'], fmt['subformat'])
                self._result = Recording(rate, fmt['channels'], fmt['mask'],
                                         samples.reshape(-1, fmt['channels']).copy(), timeline)
            except Exception as error:  # reported to the caller's thread
                self._error = error
                self._ready.set()
            finally:
                if pointer:
                    _free(pointer)
                _release(capture)
                _release(client)

else:
    def _pactl_sinks():
        if not shutil.which('pactl') or not shutil.which('parec'):
            raise RuntimeError('real-endpoint loopback on this system needs pactl and parec (PulseAudio or PipeWire)')
        text = subprocess.run(['pactl', 'list', 'sinks'], check=True, capture_output=True, text=True).stdout
        sinks, current = [], None
        for line in text.splitlines():
            stripped = line.strip()
            if line.startswith('Sink #'):
                current = {}
                sinks.append(current)
            elif current is not None and ':' in stripped:
                key, _, value = stripped.partition(':')
                current.setdefault(key.strip(), value.strip())
        return sinks

    def endpoints():
        if sys.platform == 'darwin':
            raise RuntimeError('macOS has no system loopback without a third-party virtual device')
        result = []
        for sink in _pactl_sinks():
            spec = sink.get('Sample Specification', '').split()
            channels = int(spec[1].rstrip('ch')) if len(spec) > 1 else 2
            rate = int(spec[2].rstrip('Hz')) if len(spec) > 2 else 48000
            positions = [p.strip() for p in sink.get('Channel Map', '').split(',') if p.strip()]
            mask = 0
            for p in positions:
                mask |= PULSE_POSITIONS.get(p, 0)
            if bin(mask).count('1') != channels:
                mask = default_mask(channels)
            result.append({'name': sink.get('Description', sink.get('Name', '')), 'id': sink.get('Name', ''),
                           'channels': channels, 'rate': rate, 'mask': mask,
                           'layout': channel_names(mask, channels), 'channel_map': ','.join(positions)})
        return result

    class Recorder:
        """The sink's monitor source through parec, in WAVE channel order."""

        def __init__(self, endpoint):
            self.endpoint = endpoint
            self._chunks, self._timeline, self._total = [], [], 0
            self._process = self._thread = None

        def start(self, timeout=10):
            e = self.endpoint
            pulse ={v: k for k, v in PULSE_POSITIONS.items() if k != 'mono'}
            order = [pulse[bit] for bit, _ in SPEAKERS if e['mask'] & bit]
            command = ['parec', f'--device={e["id"]}.monitor', '--format=float32le', f'--rate={e["rate"]}',
                       f'--channels={e["channels"]}', '--raw', '--latency-msec=20']
            if len(order) == e['channels']:
                command.append('--channel-map=' + ','.join(order))
            self._process = subprocess.Popen(command, stdout=subprocess.PIPE)
            self._thread = threading.Thread(target=self._read, daemon=True)
            self._thread.start()
            return self

        def _read(self):
            block = 4 * self.endpoint['channels']
            pending = b''
            while True:
                data = self._process.stdout.read1(65536)
                if not data:
                    break
                pending += data
                usable = len(pending) - len(pending) % block
                if usable:
                    self._chunks.append(pending[:usable])
                    pending = pending[usable:]
                    self._total += usable // block
                    self._timeline.append((time.perf_counter(), self._total))

        def stop(self):
            self._process.terminate()
            self._process.wait(10)
            self._thread.join(10)
            e = self.endpoint
            samples = numpy.frombuffer(b''.join(self._chunks), dtype='<f4')
            return Recording(e['rate'], e['channels'], e['mask'], samples.reshape(-1, e['channels']).copy(), self._timeline)


def find_endpoint(text, available=None):
    """The one active endpoint whose name contains text (case-insensitive), or an exact id."""
    available = endpoints() if available is None else available
    exact = [e for e in available if e['id'] == text or e['name'].lower() == text.lower()]
    if exact:
        return exact[0]
    matches = [e for e in available if text.lower() in e['name'].lower()]
    if len(matches) != 1:
        names = ', '.join(e['name'] for e in available)
        raise SystemExit(f'"{text}" matches {len(matches)} active endpoints; choose one of: {names}')
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--list', action='store_true', help='print the active playback endpoints and their layouts')
    parser.add_argument('--record', help='endpoint name (substring) to record')
    parser.add_argument('--seconds', type=float, default=5.0)
    parser.add_argument('--output', help='write the recording as a float WAV')
    args = parser.parse_args()
    if args.list or not args.record:
        print(json.dumps(endpoints(), indent=2))
        return 0
    recorder = Recorder(find_endpoint(args.record)).start()
    time.sleep(args.seconds)
    recording = recorder.stop()
    if args.output:
        recording.write_wav(args.output)
    peak = numpy.abs(recording.frames).max(axis=0).tolist() if len(recording.frames) else []
    print(json.dumps({'frames': len(recording.frames), 'rate': recording.rate, 'layout': recording.names, 'peak': peak}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
