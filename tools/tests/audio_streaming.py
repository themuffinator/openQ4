#!/usr/bin/env python3
"""Verify the production bounded Vorbis reader against full decoding.

Generates original audio with ffmpeg (PATH or imageio_ffmpeg) in .tmp/audio.
No installed game content or host audio/input device is used.
"""
from pathlib import Path
import os
import shutil
import subprocess
from sound_settings import body
from audio_stream_budget import production_stream

ROOT = Path(__file__).resolve().parents[2]
CRC_TABLE = []
for index in range(256):
    value = index << 24
    for _ in range(8):
        value = ((value << 1) ^ 0x04C11DB7) if value & 0x80000000 else value << 1
    CRC_TABLE.append(value & 0xffffffff)


def ogg_pages(data):
    """(offset, length) of each Ogg page, in file order."""
    pages, offset = [], 0
    while offset + 27 <= len(data) and data[offset:offset + 4] == b'OggS':
        segments = data[offset + 26]
        length = 27 + segments + sum(data[offset + 27:offset + 27 + segments])
        pages.append((offset, length))
        offset += length
    return pages


def reseal(data, offset, length):
    """Recompute one page's CRC after its header changed."""
    data[offset + 22:offset + 26] = b'\0\0\0\0'
    crc = 0
    for byte in data[offset:offset + length]:
        crc = ((crc << 8) & 0xffffffff) ^ CRC_TABLE[((crc >> 24) & 0xff) ^ byte]
    data[offset + 22:offset + 26] = crc.to_bytes(4, 'little')


def lengthless_variants(path):
    """Original streams without usable length metadata, as some encoders and
    truncating tools leave them: the last two pages carry no position, or the
    file continues with more than the 64 KiB stb_vorbis searches for its end."""
    data = bytearray(path.read_bytes())
    pages = ogg_pages(data)
    assert len(pages) > 4 and pages[-1][0] + pages[-1][1] == len(data), path
    unpositioned = bytearray(data)
    for offset, length in pages[-2:]:
        unpositioned[offset + 6:offset + 14] = b'\xff' * 8
        reseal(unpositioned, offset, length)
    trailing = bytearray(data) + bytes((i * 37 + 11) & 0x7f for i in range(70000))
    outputs = []
    for name, payload in (('nogranule', unpositioned), ('trailing', trailing)):
        target = path.with_name(f'{path.stem}-lengthless-{name}.ogg')
        target.write_bytes(bytes(payload))
        outputs.append(str(target))
    return outputs


def main():
    scratch = ROOT / '.tmp/audio/stream-decoder'
    scratch.mkdir(parents=True, exist_ok=True)
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        from imageio_ffmpeg import get_ffmpeg_exe
        ffmpeg = get_ffmpeg_exe()
    fixtures = []
    for rate, channels, duration in [(22050, 1, 17), (22050, 1, 3.7), (48000, 2, 32), (44100, 2, 0.1), (96000, 1, 3)]:
        path = scratch / f'{rate}-{channels}-{duration}.ogg'
        subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi',
                        '-i', f'aevalsrc=0.3*sin(2*PI*(300+20*t)*t):s={rate}:d={duration}',
                        '-ac', str(channels), '-c:a', 'libvorbis', '-q:a', '5', str(path)], cwd=scratch, check=True)
        fixtures.append(str(path))
    lengthless = lengthless_variants(Path(fixtures[0])) + lengthless_variants(Path(fixtures[2]))
    obj = scratch / ('vorbis.obj' if os.name == 'nt' else 'vorbis.o')
    exe = scratch / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([os.environ.get('CC', 'clang'), '-O2', '-c', str(ROOT / 'subprojects/stb_vorbis/stb_vorbis.c'), '-o', str(obj)], cwd=scratch, check=True)
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT), '-I', str(ROOT / 'subprojects/stb_vorbis'),
                    str(ROOT / 'tools/tests/native/SoundVorbisStreamTest.cpp'), str(obj), '-o', str(exe)], cwd=scratch, check=True)
    subprocess.run([str(exe), *fixtures, *lengthless], cwd=scratch, check=True)
    source = (ROOT / 'src/sound/OpenAL/AL_SoundVoice.cpp').read_text()
    production = '\n'.join(body(source, signature) for signature in [
        'void idSoundVoice_OpenAL::ResetQueuedBufferState()', 'void idSoundVoice_OpenAL::AdvanceQueuedBuffer()',
        'bool idSoundVoice_OpenAL::GetPlayableBufferRange(', 'bool idSoundVoice_OpenAL::SetNextQueuedSampleStart(',
        'int idSoundVoice_OpenAL::StreamBlockFrames(', 'bool idSoundVoice_OpenAL::PrepareStreamBlock(',
        'bool idSoundVoice_OpenAL::StreamBlockPending() const',
        'bool idSoundVoice_OpenAL::QueueNextBuffer(', 'bool idSoundVoice_OpenAL::Update()',
        'bool idSoundVoice_OpenAL::UsesBoundedQueue()', 'int idSoundVoice_OpenAL::SubmitBuffer(',
        'void idSoundVoice_OpenAL::DeleteStreamingBuffers()', 'int idSoundVoice_OpenAL::RoutedQueueFrames(',
        'void idSoundVoice_OpenAL::FlushSourceBuffers()', 'void idSoundVoice_OpenAL::ResetStreamingBufferQueueState()',
        'bool idSoundVoice_OpenAL::SourceHasPlayableBuffer() const'])
    queue_cpp = scratch / 'queue.cpp'
    declaration, stream = production_stream()
    queue_cpp.write_text((ROOT / 'tools/tests/native/SoundStreamQueueTest.cpp').read_text()
                         .replace('// @STREAM@', declaration + stream).replace('// @PRODUCTION@', production))
    queue_exe = scratch / ('queue.exe' if os.name == 'nt' else 'queue')
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT), '-I', str(ROOT / 'subprojects/stb_vorbis'), str(queue_cpp), str(obj), '-o', str(queue_exe)], cwd=scratch, check=True)
    subprocess.run([str(queue_exe), *fixtures[:2]], cwd=scratch, check=True)
    # Production queue transitions and seeks over a stream counted by its decode.
    subprocess.run([str(queue_exe), lengthless[0], lengthless[1]], cwd=scratch, check=True)


if __name__ == '__main__':
    main()
