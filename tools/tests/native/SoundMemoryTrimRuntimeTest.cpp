// Copyright (C) 2026 openQ4 contributors. SPDX-License-Identifier: GPL-3.0-or-later
// Execute the production low-memory traversal, including unjoined sample jobs.
#include "src/sound/SoundMemoryTrim.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
using int64 = std::int64_t;
template<class T> T Min(T a, T b) { return std::min(a, b); }
static void Check(bool ok) { if (!ok) std::abort(); }
template<class T> struct idList : std::vector<T> {
    using std::vector<T>::vector;
    int Num() const { return static_cast<int>(this->size()); }
    int FindIndex(T value) const { auto it = std::find(this->begin(), this->end(), value); return it == this->end() ? -1 : static_cast<int>(it - this->begin()); }
    void AddUnique(T value) { if (FindIndex(value) < 0) this->push_back(value); }
    void Remove(T value) { auto it = std::find(this->begin(), this->end(), value); if (it != this->end()) this->erase(it); }
};
static int Sys_Milliseconds() { return 100000; }
struct Common { void Printf(const char*, ...) {} } commonInstance;
static Common* common = &commonInstance;
struct idSoundSample {
    bool pending = false, loaded = true, defaulted = false, never = false, level = false, stream = false, copy = false;
    int played = 0, freed = 0, copies = 0, cancelled = 0, joined = 0;
    bool IsLoaded() const { return loaded; }
    bool IsDefault() const { return defaulted; }
    bool GetNeverPurge() const { return never; }
    bool GetLevelLoadReferenced() const { return level; }
    bool IsStreamed() const { return stream; }
    bool HasCpuCopy() const { return copy && !pending; }
    int GetLastPlayedTime() const { return played; }
    int BufferSize() const { return 1024; }
    void FreeData() { ++freed; if (pending) ++cancelled; pending = loaded = false; }
    void FinishDecode() { if (pending) ++joined; pending = false; }
    int ReleaseCpuCopy() { Check(HasCpuCopy()); ++copies; copy = false; return 1024; }
};
struct idSoundChannel { idSoundSample* leadinSample = nullptr; idSoundSample* loopingSample = nullptr; };
struct idSoundEmitterLocal { idList<idSoundChannel*> channels; };
struct idSoundWorldLocal { idList<idSoundEmitterLocal*> emitters; };
struct Session { void* rw = nullptr; };
static Session* session = nullptr;
struct idSoundSystemLocal {
    idList<idSoundSample*> samples, decodingSamples;
    idList<idSoundWorldLocal*> soundWorlds;
    bool insideLevelLoad = false;
    int finishPasses = 0;
    struct Reverb { void LoadLevel(const char*, void*) {} } reverb;
    void FinishSampleDecodes(bool) { ++finishPasses; for (auto* sample : decodingSamples) sample->FinishDecode(); decodingSamples.clear(); }
    void TrimMemory(const char*, int);
    void EndLevelLoad(const char*);
};
static idSoundSystemLocal soundSystemLocal;

// @PRODUCTION@

int main() {
    idSoundSample old, idle, active, looping, menu, loading, streamed, retained, recent;
    for (auto* s : {&old, &idle, &active, &looping, &menu, &loading, &streamed}) s->pending = true;
    idle.level = active.level = looping.level = loading.level = streamed.level = recent.level = true;
    menu.never = true; streamed.stream = true; retained.never = retained.copy = true;
    recent.played = 99900; recent.copy = true;
    idSoundChannel a{&active, &looping};
    idSoundEmitterLocal emitter{{nullptr, &a}};
    idSoundWorldLocal world{{nullptr, &emitter}};
    idSoundSystemLocal system;
    system.soundWorlds = {nullptr, &world};
    system.samples = {&old, &idle, &active, &looping, &menu, &loading, &streamed, &retained, &recent};
    system.decodingSamples = {&old, &idle, &active, &looping, &menu, &loading, &streamed};
    system.insideLevelLoad = true;
    system.TrimMemory("test", 30);
    Check(old.pending && idle.pending && loading.pending && system.decodingSamples.Num() == 7);
    Check(retained.copies == 1 && recent.copies == 1);
    system.insideLevelLoad = false;
    system.TrimMemory("test", 30);
    Check(old.cancelled == 1 && idle.cancelled == 1 && loading.cancelled == 1);
    Check(system.decodingSamples.Num() == 4);
    for (auto* s : {&active, &looping, &menu, &streamed}) Check(s->pending && !s->freed);
    Check(recent.loaded && !recent.freed);
    system.TrimMemory("test", 30);
    Check(old.freed == 1 && idle.freed == 1 && retained.copies == 1);
    // A negative idle limit retains this level but still removes an old level's job.
    idSoundSample old2, current;
    old2.pending = current.pending = current.level = true;
    system.samples = {&old2, &current}; system.decodingSamples = {&old2, &current};
    system.TrimMemory("test", -1);
    Check(old2.cancelled == 1 && current.pending && system.decodingSamples.Num() == 1);
    Check(system.finishPasses == 0); // Memory notices never join/upload retained jobs.
    idSoundSample stale, live;
    stale.pending = live.pending = live.level = true;
    soundSystemLocal.samples = {&stale, &live};
    soundSystemLocal.decodingSamples = {&stale, &live};
    SoundMemoryReclaim();
    Check(stale.cancelled == 1 && stale.joined == 0 && live.joined == 1 && !live.freed);
    // Reclaim can be called during a pending-list traversal; it must not edit it.
    Check(soundSystemLocal.decodingSamples.Num() == 2);
    idSoundSample discardedMap, retainedMap, menuMap;
    discardedMap.pending = retainedMap.pending = menuMap.pending = true;
    retainedMap.level = menuMap.never = true;
    system.samples = {&discardedMap, &retainedMap, &menuMap}; system.decodingSamples = system.samples;
    system.insideLevelLoad = true;
    system.EndLevelLoad("game/test");
    Check(discardedMap.cancelled == 1 && discardedMap.joined == 0);
    Check(retainedMap.joined == 1 && menuMap.joined == 1 && !retainedMap.freed && !menuMap.freed);
    Check(!system.insideLevelLoad && system.decodingSamples.Num() == 0 && system.finishPasses == 1);
    std::puts("PASS: low-memory traversal cancels disposable jobs without uploads; active, loop, menu and loading ownership preserved");
}
