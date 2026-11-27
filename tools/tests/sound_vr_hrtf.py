#!/usr/bin/env python3
"""Pin when the game asks its output for HRTF (3D headphone audio).

On the desktop it is the player's choice and off by default, because OpenAL
Soft's automatic mode trusts any endpoint labelled headphones. A running VR
headset session asks for it by itself (vr_hrtf), and keeps it through a short
stop so the output device is not reset twice. This compiles the production rule
(src/sound/SoundHrtfPolicy.h) and checks it, then checks that the device code,
the settings transaction and the defaults all go through it. No audio device,
headset or installed assets are used.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]

PROGRAM = r'''
#include "src/sound/SoundHrtfPolicy.h"
#include <cstdio>
#include <climits>
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAILED line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
int main() {
	// the desktop: the player's choice, whatever vr_hrtf says
	for (int chosen = 0; chosen <= 2; ++chosen) {
		CHECK(SoundRequestedHrtfMode(chosen, true, false) == chosen);
		CHECK(SoundRequestedHrtfMode(chosen, false, false) == chosen);
		// a headset session with vr_hrtf off follows the choice too
		CHECK(SoundRequestedHrtfMode(chosen, false, true) == chosen);
		// a headset session asks for HRTF
		CHECK(SoundRequestedHrtfMode(chosen, true, true) == 2);
	}
	CHECK(SoundRequestedHrtfMode(-5, true, false) == 0 && SoundRequestedHrtfMode(9, true, false) == 2);

	soundVrHrtfState_t state;
	// no headset has run: never engaged, however long that lasts
	CHECK(!SoundVrHrtfEngaged(state, false, 0) && !SoundVrHrtfEngaged(state, false, 100000));
	// a session engages at once
	CHECK(SoundVrHrtfEngaged(state, true, 200000));
	// a stop holds for the hold time, then lets go, and stays let go
	CHECK(SoundVrHrtfEngaged(state, false, 200000 + SOUND_VR_HRTF_HOLD_MSEC - 1));
	CHECK(!SoundVrHrtfEngaged(state, false, 200000 + SOUND_VR_HRTF_HOLD_MSEC));
	CHECK(!SoundVrHrtfEngaged(state, false, 200000 + 1));
	// a session that comes back inside the hold never let go, and restarts the hold
	CHECK(SoundVrHrtfEngaged(state, true, 300000));
	CHECK(SoundVrHrtfEngaged(state, false, 300000 + 3000));
	CHECK(SoundVrHrtfEngaged(state, true, 300000 + 4000));
	CHECK(SoundVrHrtfEngaged(state, false, 300000 + 4000 + SOUND_VR_HRTF_HOLD_MSEC - 1));
	CHECK(!SoundVrHrtfEngaged(state, false, 300000 + 4000 + SOUND_VR_HRTF_HOLD_MSEC));
	// the millisecond clock wraps after 24 days: a hold across the wrap still ends
	soundVrHrtfState_t wrap;
	CHECK(SoundVrHrtfEngaged(wrap, true, INT_MAX - 1000));
	CHECK(SoundVrHrtfEngaged(wrap, false, INT_MIN + 1000));
	CHECK(!SoundVrHrtfEngaged(wrap, false, INT_MIN + SOUND_VR_HRTF_HOLD_MSEC));
	CHECK(SOUND_VR_HRTF_HOLD_MSEC >= 2000 && SOUND_VR_HRTF_HOLD_MSEC <= 15000);
	std::printf("%s\n", failures ? "FAILED" : "policy ok");
	return failures != 0;
}
'''


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8', errors='replace')


def main() -> int:
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('clang++ or g++ is required')
    scratch = ROOT / '.tmp'
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sound-vr-hrtf-', dir=scratch) as work:
        source = Path(work) / 'policy.cpp'
        source.write_text(PROGRAM, encoding='utf-8')
        executable = Path(work) / ('policy.exe' if os.name == 'nt' else 'policy')
        subprocess.run([compiler, '-std=c++17', '-O0', '-I', str(ROOT), str(source), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
    print(result.stdout.strip())
    failures = [] if result.returncode == 0 else ['the production rule failed its cases']

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    system = read('src/sound/snd_system.cpp')
    hardware = read('src/sound/OpenAL/AL_SoundHardware.cpp')
    settings = read('src/sound/SoundSettings.cpp')
    vr = read('src/framework/VRSystem.cpp')
    common = read('src/framework/Common.cpp')

    # defaults: off on the desktop, on in a headset
    require('idCVar s_openALHRTF( "s_openALHRTF", "1", CVAR_ARCHIVE | CVAR_INTEGER' in system, 'Headphone HRTF is not off by default')
    require('idCVar vr_hrtf( "vr_hrtf", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL' in vr, 'vr_hrtf is not on by default')
    # one rule, fed by the running session
    require('return SoundRequestedHrtfMode( s_openALHRTF.GetInteger(), vr_hrtf.GetBool(), Sound_HeadsetEngaged() );' in system,
            'Sound_RequestedHrtfMode does not use the production rule')
    require('SoundVrHrtfEngaged( soundVrHrtf, vrSystem != NULL && vrSystem->IsPacing(), Sys_Milliseconds() )' in system,
            'the headset hold is not fed by the running session')
    # every reader goes through it: the device attributes and monitor, and the settings transaction
    require('OPENQ4_OPENAL_HRTF_ON, Sound_RequestedHrtfMode() );' in hardware, 'the device does not ask for the requested mode')
    require('s_openALHRTF.GetInteger()' not in hardware and 's_openALHRTF.GetInteger()' not in settings,
            'a reader bypasses Sound_RequestedHrtfMode')
    require(settings.count('Sound_RequestedHrtfMode()') == 5, 'the settings transaction lost a requested-mode read')
    # the one-time move of an archived Auto
    require('static const int OPENQ4_AUDIO_DEFAULTS_MIGRATION_STEPS = 3;' in common, 'the audio migration has no HRTF step')
    require(re.search(r'applied < 3 && cvarSystem->GetCVarInteger\( "s_openALHRTF" \) == 0 \) \{\s*common->Printf\( "Migrating legacy audio config: '
                      r's_openALHRTF 0 -> 1[^\n]*\n\s*cvarSystem->SetCVarInteger\( "s_openALHRTF", 1, CVAR_ARCHIVE \);', common) is not None,
            'an archived automatic HRTF is not moved to off')

    for failure in failures:
        print('FAILED:', failure)
    print('sound_vr_hrtf:', 'ok' if not failures else 'failed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
