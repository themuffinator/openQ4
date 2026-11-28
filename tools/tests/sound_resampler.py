#!/usr/bin/env python3
"""Pin the resampler `s_resampler auto` picks, and what it does to 22.05 kHz sound.

Most Quake 4 sounds are recorded at 22.05 kHz, so the resampler's passband is
the game's treble. The first half compiles the production choice
(src/sound/SoundResampler.h) and checks it against the resampler lists of the
OpenAL Soft releases in use. The second half needs an OpenAL Soft library (the
staged one by default): it renders tones through that library's loopback device
with the chosen resampler and checks the treble it keeps and the images it
leaves, with the short sinc as a control that the measurement sees the loss.

s_retailUpsampling is the other way to bring those sounds up: repeating each
sample to 44.1 kHz, as the original game's mixer did (src/sound/
SoundSampleRepeat.h). Its production rule is compiled and checked too, the
source is checked to send every buffer and position through it, and on the
library a repeated tone must come out with the droop and the image that
repetition gives.
No playback endpoint, input or installed assets are used.
"""
from pathlib import Path
import argparse
import ctypes as C
import hashlib
import json
import math
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE_RATE, OUTPUT_RATE = 22050, 48000

# AL_SOFT_source_resampler lists, by OpenAL Soft release.
RUNTIMES = {
    'openal_soft_1_25': ['Nearest', 'Linear', 'Cubic Spline', '4-point Gaussian', '11th order Sinc (fast)', '11th order Sinc',
                         '23rd order Sinc (fast)', '23rd order Sinc', '47th order Sinc (fast)', '47th order Sinc'],
    'openal_soft_1_23': ['Nearest', 'Linear', 'Cubic', '11th order Sinc (fast)', '11th order Sinc',
                         '23rd order Sinc (fast)', '23rd order Sinc'],
    'openal_soft_1_19': ['Point', 'Linear', 'Sinc4', 'BSinc12', 'BSinc24'],
    'short_sinc_only': ['Nearest', 'Linear', 'Cubic', '11th order Sinc (fast)', '11th order Sinc'],
    'fast_only': ['Linear', '23rd order Sinc (fast)', '11th order Sinc'],
    'long_only': ['Linear', '11th order Sinc', '47th order Sinc (fast)', '47th order Sinc'],
    'empty': [],
}
EXPECTED = {
    'openal_soft_1_25': '23rd order Sinc', 'openal_soft_1_23': '23rd order Sinc', 'openal_soft_1_19': None,
    'short_sinc_only': None, 'fast_only': '23rd order Sinc (fast)', 'long_only': '47th order Sinc', 'empty': None,
}


def production_choices(output: Path) -> dict:
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('clang++ or g++ is required')
    tables = '\n'.join(
        'static const char* const %s[] = { %s };' % (name, ', '.join(['"%s"' % entry for entry in names] + ['nullptr']))
        for name, names in RUNTIMES.items())
    calls = '\n'.join('\tstd::printf("%s %%d\\n", SoundResamplerAutoChoice(%s, %d));' % (name, name, len(names))
                      for name, names in RUNTIMES.items())
    source = output / 'choice.cpp'
    source.write_text('#include "src/sound/SoundResampler.h"\n#include <cstdio>\n%s\nint main() {\n%s\n'
                      '\tconst char* const holes[] = { nullptr, "23rd order Sinc" };\n'
                      '\tstd::printf("null_entry %%d\\n", SoundResamplerAutoChoice(holes, 2));\n\treturn 0;\n}\n' % (tables, calls))
    executable = output / ('choice.exe' if os.name == 'nt' else 'choice')
    subprocess.run([compiler, '-std=c++17', '-O0', '-I', str(ROOT), str(source), '-o', str(executable)], check=True)
    printed = subprocess.run([str(executable)], check=True, capture_output=True, text=True).stdout
    return {name: int(index) for name, index in (line.split() for line in printed.splitlines())}


REPEAT_PROGRAM = r'''
#include "src/sound/SoundSampleRepeat.h"
#include <cstdio>
#include <initializer_list>
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAILED line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
int main() {
	// the two rates the original mixer repeated, to 44.1 kHz; every other rate is the resampler's
	CHECK(SoundRetailRepeat(22050) == 2 && SoundRetailRepeat(11025) == 4);
	for (int rate : {0, 8000, 11024, 11026, 16000, 22049, 22051, 24000, 32000, 44100, 48000, 96000}) CHECK(SoundRetailRepeat(rate) == 1);
	CHECK(22050 * SoundRetailRepeat(22050) == 44100 && 11025 * SoundRetailRepeat(11025) == 44100);
	const short mono[3] = {7, -9, 32767};
	short out[32];
	for (short& value : out) value = 1234;
	SoundRepeatFrames(mono, 3, 1, 2, out);
	const short twice[6] = {7, 7, -9, -9, 32767, 32767};
	for (int i = 0; i < 6; ++i) CHECK(out[i] == twice[i]);
	CHECK(out[6] == 1234);
	SoundRepeatFrames(mono, 3, 1, 4, out);
	for (int i = 0; i < 12; ++i) CHECK(out[i] == mono[i / 4]);
	CHECK(out[12] == 1234);
	// interleaved channels repeat as whole frames
	const short stereo[4] = {1, -1, 2, -2};
	for (short& value : out) value = 1234;
	SoundRepeatFrames(stereo, 2, 2, 2, out);
	const short frames[8] = {1, -1, 1, -1, 2, -2, 2, -2};
	for (int i = 0; i < 8; ++i) CHECK(out[i] == frames[i]);
	CHECK(out[8] == 1234);
	SoundRepeatFrames(stereo, 2, 2, 1, out);
	for (int i = 0; i < 4; ++i) CHECK(out[i] == stereo[i]);
	std::printf("%s\n", failures ? "FAILED" : "repeat ok");
	return failures != 0;
}
'''


def production_repeat(output: Path) -> bool:
    compiler = shutil.which('clang++') or shutil.which('g++')
    source = output / 'repeat.cpp'
    source.write_text(REPEAT_PROGRAM)
    executable = output / ('repeat.exe' if os.name == 'nt' else 'repeat')
    subprocess.run([compiler, '-std=c++17', '-O0', '-I', str(ROOT), str(source), '-o', str(executable)], check=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    if result.returncode:
        print(result.stdout)
    return result.returncode == 0


def tone(hz: float, frames: int):
    return (C.c_short * frames)(*[round(16000 * math.sin(2 * math.pi * hz * i / SOURCE_RATE)) for i in range(frames)])


def level_db(samples, hz: float) -> float:
    """Goertzel: the level of one frequency in a block of output."""
    coefficient = 2 * math.cos(2 * math.pi * hz / OUTPUT_RATE)
    previous = before = 0.0
    for value in samples:
        previous, before = value + coefficient * previous - before, previous
    power = previous * previous + before * before - coefficient * previous * before
    return 10 * math.log10(max(power, 1e-30) / (len(samples) / 2) ** 2)


class Provider:
    def __init__(self, library: Path):
        if os.name == 'nt':
            self.directory = os.add_dll_directory(str(library.parent))
        self.al = C.CDLL(str(library))
        ptr, uint, integer = C.c_void_p, C.c_uint, C.c_int
        self.integer, self.uint = integer, uint
        getproc = self.fn('alcGetProcAddress', ptr, ptr, C.c_char_p)
        self.open = C.CFUNCTYPE(ptr, C.c_char_p)(getproc(None, b'alcLoopbackOpenDeviceSOFT'))
        self.render = C.CFUNCTYPE(None, ptr, ptr, integer)(getproc(None, b'alcRenderSamplesSOFT'))
        self.create = self.fn('alcCreateContext', ptr, ptr, C.POINTER(integer))
        self.current = self.fn('alcMakeContextCurrent', C.c_char, ptr)
        self.destroy = self.fn('alcDestroyContext', None, ptr)
        self.close = self.fn('alcCloseDevice', C.c_char, ptr)
        self.device = self.open(None)
        if not self.device:
            raise RuntimeError('ALC_SOFT_loopback unavailable')
        # ALC_FORMAT_CHANNELS_SOFT stereo, ALC_FORMAT_TYPE_SOFT float, ALC_FREQUENCY, ALC_HRTF_SOFT off
        self.context = self.create(self.device, (integer * 9)(0x1990, 0x1501, 0x1991, 0x1406, 0x1007, OUTPUT_RATE, 0x1992, 0, 0))
        if not self.context:
            raise RuntimeError('Float loopback context unavailable')
        self.current(self.context)
        if not self.fn('alIsExtensionPresent', C.c_char, C.c_char_p)(b'AL_SOFT_source_resampler'):
            raise RuntimeError('AL_SOFT_source_resampler unavailable')
        name = C.CFUNCTYPE(C.c_char_p, integer, integer)(self.fn('alGetProcAddress', ptr, C.c_char_p)(b'alGetStringiSOFT'))
        count = self.fn('alGetInteger', integer, integer)(0x1210)      # AL_NUM_RESAMPLERS_SOFT
        self.names = [name(0x1213, index).decode() for index in range(count)]   # AL_RESAMPLER_NAME_SOFT
        self.version = self.fn('alGetString', C.c_char_p, integer)(0xB002).decode()

    def fn(self, name, result, *params):
        call = getattr(self.al, name)
        call.restype, call.argtypes = result, params
        return call

    def play(self, resampler: int, hz: float, repeat: int = 1) -> list:
        """The left channel of one second of a 22.05 kHz tone, after the filter has settled. With repeat, the
        buffer holds each sample that many times at that multiple of the rate, as s_retailUpsampling uploads it."""
        source, buffer = self.uint(), self.uint()
        self.fn('alGenSources', None, self.integer, C.POINTER(self.uint))(1, C.byref(source))
        self.fn('alGenBuffers', None, self.integer, C.POINTER(self.uint))(1, C.byref(buffer))
        pcm = tone(hz, SOURCE_RATE)
        if repeat > 1:
            pcm = (C.c_short * (SOURCE_RATE * repeat))(*[value for value in pcm for _ in range(repeat)])
        self.fn('alBufferData', None, self.uint, self.integer, C.c_void_p, self.integer, self.integer)(
            buffer, 0x1101, pcm, C.sizeof(pcm), SOURCE_RATE * repeat)  # AL_FORMAT_MONO16
        seti = self.fn('alSourcei', None, self.uint, self.integer, self.integer)
        seti(source, 0x1009, buffer.value)                             # AL_BUFFER
        seti(source, 0x1007, 1)                                        # AL_LOOPING
        seti(source, 0x1212, resampler)                                # AL_SOURCE_RESAMPLER_SOFT
        self.fn('alSourcePlay', None, self.uint)(source)
        block = (C.c_float * (OUTPUT_RATE * 2))()
        self.render(self.device, block, OUTPUT_RATE // 4)
        self.render(self.device, block, OUTPUT_RATE)
        left = list(block[0::2])
        self.fn('alDeleteSources', None, self.integer, C.POINTER(self.uint))(1, C.byref(source))
        self.fn('alDeleteBuffers', None, self.integer, C.POINTER(self.uint))(1, C.byref(buffer))
        if self.fn('alGetError', self.integer)():
            raise RuntimeError('OpenAL error while rendering')
        return left

    def response(self, resampler: int) -> dict:
        reference = level_db(self.play(resampler, 1000), 1000)
        result = {}
        for hz in (6000, 8000, 9000):
            out = self.play(resampler, hz)
            result[f'{hz}_hz_db'] = round(level_db(out, hz) - reference, 2)
            if hz == 9000:
                # the first image of a 9 kHz tone sampled at 22.05 kHz
                result['image_13050_hz_db'] = round(level_db(out, SOURCE_RATE - hz) - level_db(out, hz), 2)
        return result

    def repeated(self, resampler: int) -> dict:
        """A 9 kHz tone at 22.05 kHz, repeated to 44.1 kHz: its level against 1 kHz, and its first image against it."""
        reference = level_db(self.play(resampler, 1000, 2), 1000)
        out = self.play(resampler, 9000, 2)
        return {'9000_hz_db': round(level_db(out, 9000) - reference, 2),
                'image_13050_hz_db': round(level_db(out, SOURCE_RATE - 9000) - level_db(out, 9000), 2)}

    def shutdown(self):
        self.current(None)
        self.destroy(self.context)
        self.close(self.device)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=ROOT / '.install' / ('OpenAL32.dll' if os.name == 'nt' else 'libopenal.so.1'))
    parser.add_argument('--require-provider', action='store_true', help='fail when the OpenAL library is missing')
    parser.add_argument('--output', type=Path, default=ROOT / '.tmp/audio/resampler')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    chosen = production_choices(output)
    checks, values = {}, {'choices': {}}
    for runtime, names in RUNTIMES.items():
        picked = names[chosen[runtime]] if chosen[runtime] >= 0 else None
        values['choices'][runtime] = picked
        checks[f'choice_{runtime}'] = picked == EXPECTED[runtime]
    checks['choice_skips_null_entry'] = chosen['null_entry'] == 1

    hardware = (ROOT / 'src/sound/OpenAL/AL_SoundHardware.cpp').read_text(encoding='utf-8', errors='replace')
    voice = (ROOT / 'src/sound/OpenAL/AL_SoundVoice.cpp').read_text(encoding='utf-8', errors='replace')
    checks['auto_is_default'] = 'idCVar s_resampler( "s_resampler", "auto"' in hardware
    checks['auto_uses_production_choice'] = 'chosen = SoundResamplerAutoChoice( names.Ptr(), count );' in hardware
    checks['voices_apply_choice'] = 'alSourcei( openalSource, AL_SOURCE_RESAMPLER_SOFT, hardware.GetSourceResampler() );' in voice

    # s_retailUpsampling: the rule, and every buffer and sample position going through it
    checks['repeat_rule'] = production_repeat(output)
    sample = (ROOT / 'src/sound/OpenAL/AL_SoundSample.cpp').read_text(encoding='utf-8', errors='replace')
    checks['repeat_off_by_default'] = 'idCVar s_retailUpsampling( "s_retailUpsampling", "0", CVAR_ARCHIVE | CVAR_BOOL' in hardware
    checks['repeat_latched_with_device'] = ('retailUpsampling = s_retailUpsampling.GetBool();' in hardware and
                                            'return retailUpsampling ? SoundRetailRepeat( sampleRate ) : 1;' in hardware)
    # the queue's four uploads use the helper; the only raw uploads left in the voice are the helper's own two
    checks['repeat_queue_uploads'] = voice.count('SoundVoice_BufferData( streamBuffer,') == 4 and voice.count('alBufferData(') == 2
    checks['repeat_queue_sized_by_upload'] = voice.count('* soundSystemLocal.hardware.SampleRepeat( ') == 3
    checks['repeat_positions'] = ('alSourcei( openalSource, AL_SAMPLE_OFFSET, startSample * sample->openalRepeat );' in voice and
                                  'alSourcei( openalSource, AL_SAMPLE_OFFSET, offset * sample->openalRepeat );' in voice and
                                  voice.count('AL_SAMPLE_OFFSET') == 2)
    checks['repeat_resident_buffer'] = (
        'openalRepeat = repeat;' in sample and 'openalReserved = uploadSize;' in sample and
        'alBufferData( openalBuffer, GetOpenALBufferFormat(), repeated, uploadSize, format.basic.samplesPerSec * repeat );' in sample)

    # the player's switch: Original Game Sound on the Audio page, retained and stock, in every language
    menu = (ROOT / 'src/framework/Session_menu.cpp').read_text(encoding='utf-8', errors='replace')
    retained = (ROOT / 'tools/ui/retained_audio.py').read_text(encoding='utf-8')
    stock = (ROOT / 'content/baseoq4/pak0/guis/menu/settings/audio.gui').read_text(encoding='utf-8', errors='replace')
    checks['row_retained'] = '("toggle", "audio_retail", "#str_233572", "audioRetailUpsampling", "audio.retailUpsampling", None),' in retained
    checks['row_tooltip'] = '"audio_retail": "#str_233573",' in (ROOT / 'tools/ui/retained_tooltips.py').read_text(encoding='utf-8')
    # the session sets the setting and restarts the sound system, so every sample is uploaded the chosen way
    checks['row_session'] = ('cvarSystem->SetCVarBool( "s_retailUpsampling", value != 0 );\n\t\t\tHandleMainMenuCommands( "sound drivar" );' in menu.replace('\r\n', '\n') and
                             'gui->SetStateBool( "audio.retailUpsampling", cvarSystem->GetCVarBool( "s_retailUpsampling" ) );' in menu)
    checks['row_stock'] = re.search(r'choiceDef set_audio_retail_val\s*\{[^}]*cvar\ts_retailUpsampling\b[^}]*\}\s*onActionRelease \{\s*set "cmd" "sound drivar" ;', stock) is not None
    for document in ('audio.rui', 'audio_awakening.rui'):
        page = (ROOT / 'content/baseoq4/pak0/guis/menu/settings' / document).read_text(encoding='utf-8')
        checks[f'row_generated_{document}'] = '"audio_retail"' in page and 'audioRetailUpsampling' in page and '#str_233573' in page
    for language in ('brazilian', 'czech', 'english', 'french', 'german', 'hungarian', 'italian', 'polish', 'russian', 'spanish',
                     'turkish', 'ukrainian'):
        table = (ROOT / f'content/baseoq4/pak0/strings/{language}_openq4.lang').read_text(encoding='utf-8')
        label = re.search(r'^\t"#str_233572"\t"([^"\n]+)"$', table, re.M)
        tip = re.search(r'^\t"#str_233573"\t"([^"\n]+)"$', table, re.M)
        # the page generator wraps labels only at spaces
        checks[f'row_strings_{language}'] = label is not None and ' ' in label.group(1) and tip is not None and len(tip.group(1)) > 20

    report = {'checks': checks, 'values': values, 'provider': None,
              'choice_sha256': hashlib.sha256((ROOT / 'src/sound/SoundResampler.h').read_bytes()).hexdigest(),
              'repeat_sha256': hashlib.sha256((ROOT / 'src/sound/SoundSampleRepeat.h').read_bytes()).hexdigest()}
    if args.library.is_file():
        provider = Provider(args.library.resolve())
        try:
            report['provider'] = {'version': provider.version, 'resamplers': provider.names,
                                  'sha256': hashlib.sha256(args.library.read_bytes()).hexdigest()}
            # Run the production choice on this library's own list.
            RUNTIMES.clear()
            RUNTIMES['provider'] = provider.names
            index = production_choices(output)['provider']
            values['provider_choice'] = provider.names[index] if index >= 0 else None
            checks['provider_has_choice'] = index >= 0
            if index >= 0:
                kept = values['provider_response'] = provider.response(index)
                # Flat through 8 kHz, most of 9 kHz, and no audible image.
                checks['keeps_6_khz'] = kept['6000_hz_db'] > -0.5
                checks['keeps_8_khz'] = kept['8000_hz_db'] > -1.0
                checks['keeps_9_khz'] = kept['9000_hz_db'] > -5.0
                checks['no_image'] = kept['image_13050_hz_db'] < -50.0
            if index >= 0:
                # Repeating to 44.1 kHz is a two-sample average there: cos(pi f / 44100). The tone keeps more
                # than the sinc alone leaves it, and its image stands a few dB under it instead of being removed.
                held = values['repeated_response'] = provider.repeated(index)
                gain = lambda hz: 20 * math.log10(abs(math.cos(math.pi * hz / 44100)))
                values['repeated_expected'] = {'9000_hz_db': round(gain(9000) - gain(1000), 2),
                                               'image_13050_hz_db': round(gain(13050) - gain(9000), 2)}
                checks['repeat_keeps_tone'] = abs(held['9000_hz_db'] - values['repeated_expected']['9000_hz_db']) < 0.7
                checks['repeat_leaves_image'] = abs(held['image_13050_hz_db'] - values['repeated_expected']['image_13050_hz_db']) < 0.7
            if '11th order Sinc' in provider.names:
                short = values['short_sinc_response'] = provider.response(provider.names.index('11th order Sinc'))
                # The control: the measurement has to see the loss this test exists for.
                checks['control_sees_short_sinc_loss'] = short['8000_hz_db'] < -6.0 and short['9000_hz_db'] < -12.0
        finally:
            provider.shutdown()
    elif args.require_provider:
        checks['provider_available'] = False

    report['passed'] = all(checks.values())
    report['harness_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
