#!/usr/bin/env python3
"""Audio memory budgets by device class and the Android trim-level policy (AUD-03).

Compiles the production headers (src/sound/SoundMemoryClass.h and
SoundMemoryTrim.h) and checks how a device is classed, which budgets each class
gets, how a budget cvar overrides them, how an Android onTrimMemory level picks
what a low-memory notice frees, and how levels reported before the engine
answers fold together. It also checks that the activity's onTrimMemory hands its
level to the engine through the JNI entry point the backend exports, before SDL
turns it into SDL_EVENT_LOW_MEMORY. No device, window or audio output is used.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include "src/sound/SoundMemoryClass.h"
#include "src/sound/SoundMemoryTrim.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static int failures = 0;
static void check(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; } }
// The policy is constexpr: it must also hold at compile time.
static_assert(SoundMemoryClassify(false, 2048) == SoundMemoryClass::Desktop, "desktop is never constrained by its memory");
static_assert(SoundMemoryClassBudgets(SoundMemoryClass::Mobile).payloadMB == 256, "phones with room keep the desktop payload budget");
static_assert(SoundTrimIdleSecondsForLevel(SOUND_TRIM_LEVEL_COMPLETE, 30) == 0, "a process about to be killed frees every idle sample");
int main() {
    using C = SoundMemoryClass;
    // Classing: desktop whatever its memory; phones by system memory, unknown counted as constrained.
    check(SoundMemoryClassify(false, 0) == C::Desktop, "desktop with unknown memory");
    check(SoundMemoryClassify(false, 65536) == C::Desktop, "desktop with lots of memory");
    check(SoundMemoryClassify(true, 0) == C::ConstrainedMobile, "phone with unknown memory");
    check(SoundMemoryClassify(true, 3891) == C::ConstrainedMobile, "4 GiB phone (reported below nominal)");
    check(SoundMemoryClassify(true, SOUND_MEMORY_CONSTRAINED_RAM_MB) == C::ConstrainedMobile, "exactly 4 GiB");
    check(SoundMemoryClassify(true, SOUND_MEMORY_CONSTRAINED_RAM_MB + 1) == C::Mobile, "just above 4 GiB");
    check(SoundMemoryClassify(true, 11264) == C::Mobile, "12 GiB phone");
    // Budgets: every class bounds payloads, decode and streams; phones keep 48 MiB of decode,
    // above the largest stock/Awakening input and PCM pair (47 MiB), so streaming is optional.
    for (C c : {C::Desktop, C::Mobile, C::ConstrainedMobile}) {
        const SoundMemoryBudgets b = SoundMemoryClassBudgets(c);
        check(b.payloadMB >= 32 && b.payloadMB <= 2048, "payload budget within s_audioMemoryMB's range");
        check(b.decodeMB >= 48 && b.decodeMB <= 1024, "decode budget admits the largest non-streamed track");
        check(b.streamMB >= 8 && b.streamMB <= 256, "stream budget within s_streamMemoryMB's range");
        // 2.4 MiB per stream: room for at least the seven streams an opening has held, plus two.
        check(b.streamMB * 1024 >= 9 * 2448, "stream budget holds nine streams");
        check(b.providerMB > 0 && b.providerMB <= 64, "provider allowance declared");
        check(SoundMemoryClassName(c)[0] != 0, "class named");
        C parsed = C::Desktop;
        check(SoundMemoryClassFromName(SoundMemoryClassName(c), parsed) && parsed == c, "class name round-trips");
    }
    check(SoundMemoryClassBudgets(C::Desktop).payloadMB == 256 && SoundMemoryClassBudgets(C::Desktop).decodeMB == 64 &&
          SoundMemoryClassBudgets(C::Desktop).streamMB == 32, "desktop defaults unchanged");
    check(SoundMemoryClassBudgets(C::ConstrainedMobile).payloadMB < SoundMemoryClassBudgets(C::Mobile).payloadMB,
          "a constrained phone holds less");
    C unused = C::Mobile;
    check(!SoundMemoryClassFromName("auto", unused) && !SoundMemoryClassFromName("", unused) &&
          !SoundMemoryClassFromName(nullptr, unused) && !SoundMemoryClassFromName("mobile ", unused) && unused == C::Mobile,
          "auto and unknown names follow the device");
    // Overrides: 0 (and negatives) follow the class; anything else is the player's, clamped.
    check(SoundMemoryBudgetMB(0, 192, 32, 2048) == 192 && SoundMemoryBudgetMB(-5, 192, 32, 2048) == 192, "0 follows the class");
    check(SoundMemoryBudgetMB(256, 192, 32, 2048) == 256, "an archived 256 stays 256");
    check(SoundMemoryBudgetMB(1, 192, 32, 2048) == 32 && SoundMemoryBudgetMB(9999, 192, 32, 2048) == 2048, "overrides clamp");
    // Trim levels (ComponentCallbacks2): light, normal and severe.
    struct { int level; int configured; int expected; const char* what; } cases[] = {
        {SOUND_TRIM_LEVEL_UNKNOWN, 30, 30, "no level (iOS, desktop test): s_trimIdleSeconds"},
        {SOUND_TRIM_LEVEL_UNKNOWN, 0, 0, "no level, harness's 0"},
        {SOUND_TRIM_LEVEL_RUNNING_MODERATE, 30, -1, "RUNNING_MODERATE: earlier levels and CPU copies only"},
        {SOUND_TRIM_LEVEL_RUNNING_LOW, 30, 30, "RUNNING_LOW: idle samples"},
        {SOUND_TRIM_LEVEL_RUNNING_CRITICAL, 30, 0, "RUNNING_CRITICAL: every idle sample"},
        {SOUND_TRIM_LEVEL_UI_HIDDEN, 30, -1, "UI_HIDDEN (every trip to the background): light"},
        {SOUND_TRIM_LEVEL_BACKGROUND, 30, 30, "BACKGROUND: idle samples"},
        {SOUND_TRIM_LEVEL_MODERATE, 30, 0, "MODERATE: every idle sample"},
        {SOUND_TRIM_LEVEL_COMPLETE, 30, 0, "COMPLETE: every idle sample"},
        {SOUND_TRIM_LEVEL_COMPLETE, -1, -1, "s_trimIdleSeconds -1 keeps this level's samples even then"},
        {SOUND_TRIM_LEVEL_UI_HIDDEN, 0, -1, "a light level stays light"},
        {0, 30, -1, "level 0 is light"},
        {100, 30, 0, "an unknown higher level is severe"},
    };
    for (const auto& c : cases) check(SoundTrimIdleSecondsForLevel(c.level, c.configured) == c.expected, c.what);
    // Folding: SDL keeps one queued notice; the engine answers the most severe level reported.
    check(SoundTrimFoldLevel(SOUND_TRIM_LEVEL_UNKNOWN, SOUND_TRIM_LEVEL_UI_HIDDEN) == SOUND_TRIM_LEVEL_UI_HIDDEN, "first level kept");
    check(SoundTrimFoldLevel(SOUND_TRIM_LEVEL_UI_HIDDEN, SOUND_TRIM_LEVEL_BACKGROUND) == SOUND_TRIM_LEVEL_BACKGROUND, "harder level wins");
    check(SoundTrimFoldLevel(SOUND_TRIM_LEVEL_COMPLETE, SOUND_TRIM_LEVEL_UI_HIDDEN) == SOUND_TRIM_LEVEL_COMPLETE, "milder later level does not lower it");
    check(SoundTrimFoldLevel(SOUND_TRIM_LEVEL_RUNNING_CRITICAL, SOUND_TRIM_LEVEL_UI_HIDDEN) == SOUND_TRIM_LEVEL_RUNNING_CRITICAL,
          "RUNNING_CRITICAL outranks the numerically higher UI_HIDDEN");
    check(SoundTrimFoldLevel(SOUND_TRIM_LEVEL_BACKGROUND, SOUND_TRIM_LEVEL_UNKNOWN) == SOUND_TRIM_LEVEL_BACKGROUND, "unknown keeps the pending level");
    if (failures) return 1;
    std::puts("Audio memory classes: desktop/mobile/constrained budgets, cvar overrides, Android trim levels and folding passed");
    return 0;
}
'''


def host_contract():
    """The activity hands its level over before SDL queues the notice, through the exported symbol."""
    activity = (ROOT / 'mobile/android/app/src/main/java/com/darkmatter/openq4/OpenQ4Activity.java').read_text(encoding='utf-8')
    backend = (ROOT / 'src/sys/sdl3/sdl3_backend.cpp').read_text(encoding='utf-8')
    package = re.search(r'^package ([\w.]+);', activity, re.M).group(1)
    symbol = 'Java_' + package.replace('.', '_') + '_OpenQ4Activity_nativeTrimMemoryLevel'
    assert 'private static native void nativeTrimMemoryLevel(int level);' in activity, 'activity declares the entry point'
    method = activity[activity.index('public void onTrimMemory(int level) {'):]
    method = method[:method.index('\n    }\n') if '\n    }\n' in method else method.index('\r\n    }\r\n')]
    assert method.index('nativeTrimMemoryLevel(level);') < method.index('super.onTrimMemory(level);'), \
        'the level reaches the engine before SDL queues SDL_EVENT_LOW_MEMORY'
    assert 'mBrokenLibraries' in method and 'UnsatisfiedLinkError' in method, 'a missing engine library cannot crash the activity'
    assert f'extern "C" JNIEXPORT void JNICALL {symbol}(JNIEnv *, jclass, jint level)' in backend, 'JNI symbol matches the activity'
    watch = backend[backend.index('static bool SDLCALL SDL3_LifecycleEventWatch('):]
    watch = watch[:watch.index('\nstatic ')]
    low = watch[watch.index('case SDL_EVENT_LOW_MEMORY:'):]
    assert low.index('Posix_IsMainThread()') < low.index('SDL3_AnswerLowMemoryNotice(') < low.index('SDL3_SetLifecyclePendingFlag('), \
        'Android answers a notice on the engine thread at once, before deferring'


def main():
    host_contract()
    scratch = Path(tempfile.mkdtemp(prefix='audio-memory-class-', dir=ROOT / '.tmp'))
    try:
        source = scratch / 'audio_memory_class.cpp'
        source.write_text(HARNESS, encoding='utf-8')
        exe = scratch / ('audio_memory_class.exe' if os.name == 'nt' else 'audio_memory_class')
        subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT),
                        str(source), '-o', str(exe)], check=True, cwd=scratch)
        subprocess.run([str(exe)], check=True, cwd=scratch)
    finally:
        for path in sorted(scratch.rglob('*'), reverse=True):
            path.unlink() if path.is_file() else path.rmdir()
        scratch.rmdir()


if __name__ == '__main__':
    main()
