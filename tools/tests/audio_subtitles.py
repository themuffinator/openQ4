#!/usr/bin/env python3
"""Execute production subtitle selection and bounded font-measured pagination."""
from pathlib import Path
import os
import subprocess
from sound_settings import body

ROOT = Path(__file__).resolve().parents[2]


def main():
    target = ROOT / '.tmp/audio/subtitle-tests'
    target.mkdir(parents=True, exist_ok=True)
    source = (ROOT / 'src/sound/snd_emitter.cpp').read_text()
    # The transcript lookup the overlay and the s_logSpeech audit share, then the cue selection
    # with authored captions, untranscribed markers and held tails.
    (target / 'SoundSubtitlesUnderTest.inc').write_text('\n'.join(body(source, signature) for signature in (
        'static const rvDeclLipSync* SoundDeclaredLipSync(', 'static bool SoundSubtitleTextUsable(',
        'static const char* SoundTranscriptText(', 'static bool SoundChannelSubtitle(', 'static void SoundKeepNewestSubtitle(',
        'void idSoundWorldLocal::RememberSubtitleTail(', 'void idSoundWorldLocal::ForgetSubtitleTails(',
        'int idSoundWorldLocal::GetSubtitles(', 'void SoundDialogueVoices_f(')))
    compiler = os.environ.get('CXX', 'clang++')
    for name in ('SoundSubtitlesTest', 'SubtitlePagingTest'):
        exe = target / (name + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([compiler, '-std=c++17', '-O2', '-I', str(ROOT), '-I', str(target),
                        str(ROOT / 'tools/tests/native' / (name + '.cpp')), '-o', str(exe)], check=True, cwd=ROOT)
        subprocess.run([str(exe)], check=True, cwd=ROOT)


if __name__ == '__main__':
    main()
