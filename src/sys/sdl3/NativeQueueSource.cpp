// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "NativeQueueSource.h"
#if defined(USE_SDL3) && defined(OPENQ4_SDL3_CHECKED_NATIVE_QUEUE) && OPENQ4_SDL3_CHECKED_NATIVE_QUEUE
#include <SDL3/SDL_init.h>
std::uint64_t Sys_EventQueueToken();
#endif
namespace openq4 {
bool SdlNativeQueueSource::Observe(NativeQueueStatus& out,std::string& error) {
#if defined(USE_SDL3) && defined(OPENQ4_SDL3_CHECKED_NATIVE_QUEUE) && OPENQ4_SDL3_CHECKED_NATIVE_QUEUE
    if(!epoch || !SDL_IsMainThread()) { error="Native queue source requires its bound main thread"; return false; }
    const auto token=Sys_EventQueueToken();
    const bool healthy=OQ4_WindowsNativeFenceHealthy();
    const auto generation=OQ4_WindowsNativeFenceQueueGeneration();
    NativeQueueStatus candidate;
    candidate.mainThread=true; candidate.healthy=healthy; candidate.providerEpoch=epoch;
    candidate.engineToken=token; candidate.generation=generation;
    candidate.fenceEventType=OQ4_WindowsNativeFenceEventType();
    OQ4_NativeFence fence{};
    if(OQ4_WindowsNativeFencePending(&fence)) candidate.pending=fence;
    if(token!=Sys_EventQueueToken() || healthy!=OQ4_WindowsNativeFenceHealthy() || generation!=OQ4_WindowsNativeFenceQueueGeneration() ||
        (healthy && !generation) || (!healthy && (generation || candidate.pending))) {
        error="Native queue source changed during observation"; return false;
    }
    out=candidate; error.clear(); return true;
#else
    (void)out; (void)epoch;
    error="Checked native queue provider is unavailable"; return false;
#endif
}
int SdlNativeQueueSource::Poll(SDL_Event& event,OQ4_NativeQueueRecord& record) {
#if defined(USE_SDL3) && defined(OPENQ4_SDL3_CHECKED_NATIVE_QUEUE) && OPENQ4_SDL3_CHECKED_NATIVE_QUEUE
    return epoch ? OQ4_WindowsNativeFencePoll(&event,&record) : -1;
#else
    (void)event; (void)record; return -1;
#endif
}
bool SdlNativeQueueSource::CopyFence(const SDL_Event& event,OQ4_NativeFence& out) {
#if defined(USE_SDL3) && defined(OPENQ4_SDL3_CHECKED_NATIVE_QUEUE) && OPENQ4_SDL3_CHECKED_NATIVE_QUEUE
    return epoch && OQ4_WindowsNativeFenceCopy(&event,&out);
#else
    (void)event; (void)out; return false;
#endif
}
} // namespace openq4
