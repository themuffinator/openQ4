// Copyright (C) 2026 openQ4 contributors. SPDX-License-Identifier: GPL-3.0-or-later
// Production decode/join/cancel code on the real job system, with a deterministic
// codec boundary so cancellation races do not depend on CPU speed or a device.
#include "src/framework/ParallelJobSystem.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

using byte = unsigned char;
using uint64 = std::uint64_t;
template<class T> T Min(T a, T b) { return std::min(a, b); }
template<class T> T Max(T a, T b) { return std::max(a, b); }
static void Check(bool ok) { if (!ok) std::abort(); }
struct SoundVorbisStream { static constexpr int ArenaBytes = 2*1024*1024, ChunkFrames = 4096; };
static constexpr unsigned Frames = 32768;
struct stb_vorbis { unsigned cursor = 0; };
struct stb_vorbis_alloc { char* alloc_buffer; int alloc_buffer_length_in_bytes; };
struct stb_vorbis_info { int channels = 1; unsigned sample_rate = 24000; };
static std::atomic<int> opened{0}, closed{0}, freed{0};
static std::mutex gateMutex;
static std::condition_variable gate;
static bool blockRead = false, inRead = false, releaseRead = false;
static bool badHeader = false;
static int fixtureChannels = 1;
static short Pcm(unsigned frame) { return static_cast<short>((frame * 71) % 32000 - 16000); }
static stb_vorbis* stb_vorbis_open_memory(byte*, int, int*, stb_vorbis_alloc*) { ++opened; return new stb_vorbis; }
static stb_vorbis_info stb_vorbis_get_info(stb_vorbis*) { return {fixtureChannels + (badHeader ? 1 : 0), 24000}; }
static unsigned stb_vorbis_stream_length_in_samples(stb_vorbis*) { return Frames; }
static void stb_vorbis_close(stb_vorbis* v) { ++closed; delete v; }
static int Read(stb_vorbis* v, short* pcm, int count, int channels) {
    {
        std::unique_lock<std::mutex> lock(gateMutex);
        if (blockRead) {
            inRead = true; gate.notify_all();
            Check(gate.wait_for(lock, std::chrono::seconds(5), [] { return releaseRead; }));
            blockRead = false;
        }
    }
    count = Min(count, static_cast<int>(Frames - v->cursor));
    for (int i = 0; i < count * channels; ++i) pcm[i] = Pcm(v->cursor * channels + i);
    v->cursor += count;
    return count;
}
static int stb_vorbis_get_frame_short_interleaved(stb_vorbis* v, int channels, short* out, int count) { return Read(v, out, Min(count / channels, 512), channels); }
static int stb_vorbis_get_samples_short_interleaved(stb_vorbis* v, int channels, short* out, int count) { return Read(v, out, count / channels, channels); }
static void FreeBuffer(void* p) { ++freed; delete[] static_cast<byte*>(p); }
static uint64 soundSamplePendingDecodeBytes = 0, arenaBytes = 0;
static int soundSamplePendingDecodes = 0, uploaded = 0, reloaded = 0;
static void SoundMemoryRelease(uint64 count) { Check(arenaBytes >= count); arenaBytes -= count; }

// @DECODE@

class idSoundSample_OpenAL {
public:
    idSoundSampleDecode* pendingDecode = nullptr;
    const char* pendingDecodeName = "original-fixture";
    void FinishDecode();
    void CancelDecode();
    void LoadResource() { ++reloaded; }
    bool FinishOggDecode(const char*, unsigned frames) { Check(frames == Frames); return true; }
    void CreateOpenALBuffer() { ++uploaded; }
};

// @JOIN@

static idSoundSampleDecode* NewDecode(bool envelope, std::vector<short>& pcm, std::vector<byte>& amp, int channels = 1) {
    auto* d = new idSoundSampleDecode;
    d->fileData = new byte[16]; d->fileLen = 16;
    d->capacityFrames = Frames; d->channels = fixtureChannels = channels; d->sampleRate = 24000;
    d->pcm = pcm.data();
    if (envelope) { d->envelope = amp.data(); d->envelopeEntries = static_cast<int>(amp.size()); }
    d->reservedBytes = SoundVorbisStream::ArenaBytes + 16;
    soundSamplePendingDecodeBytes += d->reservedBytes;
    arenaBytes += SoundVorbisStream::ArenaBytes;
    ++soundSamplePendingDecodes;
    d->list = jobSystem.CreateJobList("audio-cancel-test", 1);
    return d;
}

int main() {
    idJobSystemConfig config; config.workerThreads = 1;
    Check(jobSystem.Initialize(config));
    for (int channels : {1, 2}) for (bool envelope : {false, true}) {
        for (bool discard : {false, true}) {
            for (bool beforeStart : {false, true}) {
                std::vector<short> pcm(Frames * channels, 0);
                std::vector<byte> amp((Frames * 60 + 23999) / 24000, 0), expected(amp.size(), 0);
                idSoundSample_OpenAL sample;
                auto* d = sample.pendingDecode = NewDecode(envelope, pcm, amp, channels);
                const int priorOpens = opened, priorFrees = freed;
                if (beforeStart) {
                    d->list->Cancel();
                    idJobContext context{d, 0, d->list->GetCancellationToken()};
                    SoundSample_DecodeJob(context);
                    Check(opened == priorOpens && d->decodedFrames == 0);
                } else {
                    { std::lock_guard<std::mutex> lock(gateMutex); blockRead = true; inRead = releaseRead = false; }
                    Check(d->list->AddJob(SoundSample_DecodeJob, d));
                    Check(d->list->Submit() == idJobSubmitResult::ACCEPTED);
                    {
                        std::unique_lock<std::mutex> lock(gateMutex);
                        Check(gate.wait_for(lock, std::chrono::seconds(5), [] { return inRead; }));
                        jobSystem.CancelAll();
                        releaseRead = true; gate.notify_all();
                    }
                    d->list->Wait();
                    Check(d->decodedFrames == (envelope ? 4096u : 512u));
                    Check(d->vorbis && d->fileData && !d->headerMismatch);
                }
                // Cancellation alone must not release bytes the sample still owns.
                Check(freed == priorFrees && arenaBytes == SoundVorbisStream::ArenaBytes);
                Check(soundSamplePendingDecodes == 1 && soundSamplePendingDecodeBytes == d->reservedBytes);
                if (discard) {
                    sample.CancelDecode(); sample.CancelDecode();
                } else {
                    sample.FinishDecode(); sample.FinishDecode();
                    for (unsigned i = 0; i < Frames; ++i) {
                        int peak = 0;
                        for (int channel = 0; channel < channels; ++channel) {
                            const unsigned index = i * channels + channel;
                            if (!envelope) Check(pcm[index] == Pcm(index));
                            peak = Max(peak, std::abs(static_cast<int>(Pcm(index))));
                        }
                        const unsigned entry = ((i + 1) * 60 - 1) / 24000;
                        expected[entry] = Max(expected[entry], static_cast<byte>(Min(255, (peak * 255 + 16383) / 32767)));
                    }
                    if (envelope) Check(amp == expected);
                }
                Check(!sample.pendingDecode && opened == closed && freed == priorFrees + 1);
                Check(arenaBytes == 0 && soundSamplePendingDecodeBytes == 0 && soundSamplePendingDecodes == 0);
            }
        }
    }
    Check(uploaded == 4 && reloaded == 0);
    // A changed header is still corruption, never a cancellation or partial upload.
    std::vector<short> pcm(Frames); std::vector<byte> amp;
    idSoundSample_OpenAL sample;
    sample.pendingDecode = NewDecode(false, pcm, amp);
    badHeader = true;
    sample.FinishDecode();
    Check(reloaded == 1 && uploaded == 4 && opened == closed && arenaBytes == 0);
    jobSystem.Shutdown();
    std::puts("PASS: real-worker mono/stereo cancellation before setup and during PCM/envelope decode; discard, exact resume and ownership");
}
