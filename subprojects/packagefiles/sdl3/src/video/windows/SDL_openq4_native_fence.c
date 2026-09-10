/* openQ4 private Windows native dispatch fence. Copyright (C) 2026 DarkMatter
 * Productions. Distributed under SDL's zlib license. No GUI/COM callbacks. */
#include "SDL_internal.h"
#if defined(SDL_VIDEO_DRIVER_WINDOWS) && !defined(SDL_PLATFORM_XBOXONE) && !defined(SDL_PLATFORM_XBOXSERIES)
#include "SDL_windowsvideo.h"
#include "SDL_openq4_native_fence.h"

#define OQ4_FENCE_MAX_EVENTS 4096
static bool oq4_fence_enabled, oq4_fence_healthy, oq4_fence_collecting;
static bool oq4_fence_in_pump, oq4_fence_held, oq4_fence_published, oq4_fence_copied, oq4_fence_activity, oq4_fence_emitting;
static Uint32 oq4_fence_type, oq4_fence_marker;
static SDL_AtomicInt oq4_fence_public_type;
static Uint64 oq4_fence_dispatch, oq4_fence_sequence;
static OQ4_NativeFence oq4_fence_pending;
static const SDL_Event *oq4_fence_admission;

static bool OQ4_FenceMain(void)
{
    return SDL_IsMainThread() || SDL_SetError("openQ4 native fence requires the main thread");
}
static void OQ4_FenceFault(void)
{
    oq4_fence_healthy = false;
    if (oq4_fence_enabled) oq4_fence_held = true;
    SDL_SetError("openQ4 native collection requires retirement and reconciliation");
}
static bool OQ4_FenceMarkerMatches(const SDL_Event *event)
{
    return event && oq4_fence_type && event->type == oq4_fence_type &&
        event->user.code > 0 && (Uint32)event->user.code == oq4_fence_marker &&
        !event->user.windowID && !event->user.reserved && !event->user.data1 && !event->user.data2;
}
bool OQ4_WindowsNativeFenceRetire(void)
{
    if (!OQ4_FenceMain()) return false;
    oq4_fence_enabled = oq4_fence_healthy = oq4_fence_collecting = false;
    oq4_fence_held = oq4_fence_published = oq4_fence_copied = oq4_fence_activity = false;
    /* A reentrant native teardown may invalidate the emitting marker, but must
     * not release its stack admission or enable again before Push returns. */
    SDL_zero(oq4_fence_pending);
    return true;
}
bool OQ4_WindowsNativeFenceEnable(bool enabled)
{
    if (!OQ4_FenceMain()) return false;
    if (enabled && (oq4_fence_emitting || oq4_fence_in_pump)) return false;
    if (!enabled) return OQ4_WindowsNativeFenceRetire();
    if (oq4_fence_enabled) return oq4_fence_healthy;
    if (oq4_fence_collecting || oq4_fence_held || oq4_fence_dispatch == SDL_MAX_UINT64 ||
        oq4_fence_sequence == SDL_MAX_UINT64 || oq4_fence_marker == 0x7fffffffu) return false;
    if (!oq4_fence_type) {
        oq4_fence_type = SDL_RegisterEvents(1);
        if (!oq4_fence_type) return false;
        SDL_SetAtomicInt(&oq4_fence_public_type, (int)oq4_fence_type);
    }
    oq4_fence_enabled = oq4_fence_healthy = true;
    return true;
}
bool OQ4_WindowsNativeFenceHealthy(void) { return OQ4_FenceMain() && oq4_fence_enabled && oq4_fence_healthy; }
Uint32 OQ4_WindowsNativeFenceEventType(void) { return OQ4_FenceMain() ? oq4_fence_type : 0; }
bool OQ4_WindowsNativeFencePending(OQ4_NativeFence *out)
{
    if (!OQ4_FenceMain() || !out || !oq4_fence_enabled || !oq4_fence_healthy || !oq4_fence_held ||
        !oq4_fence_published || oq4_fence_collecting || oq4_fence_emitting) return false;
    *out = oq4_fence_pending;
    return true;
}
bool OQ4_WindowsNativeFenceCopy(const SDL_Event *marker, OQ4_NativeFence *out)
{
    if (!OQ4_FenceMain() || !out || !oq4_fence_enabled || !oq4_fence_healthy || !oq4_fence_held ||
        !oq4_fence_published || oq4_fence_collecting || oq4_fence_emitting || !OQ4_FenceMarkerMatches(marker)) return false;
    *out = oq4_fence_pending;
    oq4_fence_copied = true;
    return true;
}
bool OQ4_WindowsNativeFenceAck(Uint64 dispatch, Uint64 sequence)
{
    if (!OQ4_FenceMain() || !oq4_fence_enabled || !oq4_fence_healthy || !oq4_fence_held ||
        !oq4_fence_published || !oq4_fence_copied || oq4_fence_collecting || oq4_fence_emitting ||
        !dispatch || dispatch != oq4_fence_pending.dispatch || !sequence || sequence != oq4_fence_pending.sequence) return false;
    oq4_fence_held = oq4_fence_published = oq4_fence_copied = false;
    SDL_zero(oq4_fence_pending);
    return true;
}
bool OQ4_WIN_NativeFenceEnabled(void) { return SDL_IsMainThread() && oq4_fence_enabled; }
bool OQ4_WIN_NativeFenceBlocked(void)
{
    if (!SDL_IsMainThread()) return true;
    return oq4_fence_in_pump || oq4_fence_emitting ||
        (oq4_fence_enabled && (!oq4_fence_healthy || oq4_fence_collecting || oq4_fence_held));
}
bool OQ4_WIN_BeginNativeCollection(void)
{
    if (!SDL_IsMainThread()) return false;
    if (!oq4_fence_enabled) return true;
    if (OQ4_WIN_NativeFenceBlocked()) return false;
    if (oq4_fence_dispatch == SDL_MAX_UINT64) { OQ4_FenceFault(); return false; }
    SDL_zero(oq4_fence_pending);
    oq4_fence_pending.version = 1;
    oq4_fence_pending.dispatch = ++oq4_fence_dispatch;
    oq4_fence_in_pump = oq4_fence_collecting = true;
    oq4_fence_activity = false;
    return true;
}
void OQ4_WIN_EndNativeCollection(bool removed_message)
{
    SDL_Event marker;
    bool queued = false, intact = false, returned = false;
    if (!SDL_IsMainThread()) return;
    oq4_fence_in_pump = false;
    if (!oq4_fence_enabled || !oq4_fence_collecting) return;
    oq4_fence_collecting = false;
    if (!oq4_fence_healthy) return;
    if (!removed_message && !oq4_fence_activity && !oq4_fence_pending.event_count) {
        SDL_zero(oq4_fence_pending); return;
    }
    if (oq4_fence_marker == 0x7fffffffu || oq4_fence_sequence == SDL_MAX_UINT64) { OQ4_FenceFault(); return; }
    oq4_fence_pending.sequence = ++oq4_fence_sequence;
    oq4_fence_held = true;
    oq4_fence_published = oq4_fence_copied = false;
    SDL_zero(marker); marker.type = oq4_fence_type; marker.user.code = (Sint32)++oq4_fence_marker;
    oq4_fence_admission = &marker;
    oq4_fence_emitting = true;
#ifdef _MSC_VER
    __try {
#endif
        queued = SDL_PushEvent(&marker);
        intact = OQ4_FenceMarkerMatches(&marker);
        returned = true;
#ifdef _MSC_VER
    } __finally {
#endif
        oq4_fence_admission = NULL;
        oq4_fence_emitting = false;
        if (!returned) OQ4_FenceFault();
#ifdef _MSC_VER
    }
#endif
    if (!oq4_fence_enabled || !oq4_fence_healthy || !queued || !intact) { OQ4_FenceFault(); return; }
    oq4_fence_published = true;
}
void OQ4_WIN_AbortNativeCollection(void)
{
    if (!SDL_IsMainThread()) return;
    oq4_fence_in_pump = oq4_fence_collecting = false;
    if (oq4_fence_enabled) OQ4_FenceFault();
}
void OQ4_WIN_NativeFenceLifecycle(void)
{
    if (SDL_IsMainThread() && oq4_fence_enabled) (void)OQ4_WindowsNativeFenceRetire();
}
void OQ4_WIN_NativeFenceShutdown(void)
{
    if (!SDL_IsMainThread()) return;
    (void)OQ4_WindowsNativeFenceRetire();
    oq4_fence_type = 0; /* SDL may recycle registered event types after shutdown. */
    SDL_SetAtomicInt(&oq4_fence_public_type, 0);
}
void OQ4_WIN_NativeFenceMessage(unsigned int message)
{
    if (!SDL_IsMainThread() || !oq4_fence_enabled) return;
    if (message == WM_DESTROY || message == WM_NCDESTROY) { OQ4_WIN_NativeFenceLifecycle(); return; }
    if (!oq4_fence_collecting || !oq4_fence_healthy) { OQ4_FenceFault(); return; }
    oq4_fence_activity = true;
}
Uint64 OQ4_WIN_CurrentNativeDispatch(void)
{
    return SDL_IsMainThread() && oq4_fence_enabled && oq4_fence_healthy && oq4_fence_collecting ? oq4_fence_pending.dispatch : 0;
}
bool OQ4_WIN_BeginFenceAdmission(const SDL_Event *event)
{
    if (!SDL_IsMainThread() || !oq4_fence_enabled || !oq4_fence_healthy || !oq4_fence_emitting ||
        event != oq4_fence_admission || !OQ4_FenceMarkerMatches(event)) return false;
    oq4_fence_admission = NULL;
    return true;
}
bool OQ4_WIN_ValidateFenceAdmission(const SDL_Event *event, bool admission)
{
    if (!SDL_IsMainThread()) {
        const Uint32 type = (Uint32)SDL_GetAtomicInt(&oq4_fence_public_type);
        return !event || !type || event->type != type;
    }
    if (admission) {
        if (!oq4_fence_enabled || !oq4_fence_healthy || !oq4_fence_emitting || !OQ4_FenceMarkerMatches(event)) {
            OQ4_FenceFault(); return false;
        }
    } else if (event && oq4_fence_type && event->type == oq4_fence_type) {
        /* Reserved private markers have exactly one emission admission. */
        if (oq4_fence_enabled) OQ4_FenceFault();
        return false;
    }
    return true;
}
void OQ4_WIN_CompleteFenceEvent(const SDL_Event *event, bool accepted)
{
    if (!SDL_IsMainThread() || !oq4_fence_enabled || !oq4_fence_collecting || !oq4_fence_healthy) return;
    if (!accepted || !event || oq4_fence_pending.event_count == OQ4_FENCE_MAX_EVENTS || oq4_fence_sequence == SDL_MAX_UINT64) {
        OQ4_FenceFault(); return;
    }
    ++oq4_fence_sequence;
    ++oq4_fence_pending.event_count;
}
#endif
