/* openQ4 private Windows native dispatch fence. Copyright (C) 2026 DarkMatter
 * Productions. Distributed under SDL's zlib license. No GUI/Session calls. */
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
static const SDL_Event *oq4_fence_admission, *oq4_fence_queue_admission;
static OQ4_NativeCollectionHooks oq4_fence_hooks;
static OQ4_NativeCollectionContext oq4_fence_context;
static bool oq4_fence_context_active, oq4_fence_prepare_attempted;
static unsigned oq4_fence_callback_depth;

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
static bool OQ4_FenceContextMatches(const OQ4_NativeCollectionContext *context)
{
    return context && oq4_fence_context_active && context->version == 1 &&
        context->kind == oq4_fence_context.kind && context->generation == oq4_fence_context.generation &&
        context->dispatch == oq4_fence_context.dispatch;
}
bool OQ4_WindowsNativeFenceRegisterHooks(const OQ4_NativeCollectionHooks *hooks)
{
    if (!OQ4_FenceMain() || oq4_fence_enabled || oq4_fence_in_pump || oq4_fence_collecting ||
        oq4_fence_emitting || oq4_fence_callback_depth || oq4_fence_prepare_attempted || oq4_fence_context_active) return false;
    if (hooks && (hooks->version != 1 || !hooks->Prepare || !hooks->Finish)) return false;
    if (hooks) oq4_fence_hooks = *hooks; else SDL_zero(oq4_fence_hooks);
    return true;
}
bool OQ4_WindowsNativeFenceMarkActivity(const OQ4_NativeCollectionContext *context)
{
    if (!OQ4_FenceMain() || !OQ4_FenceContextMatches(context) || !oq4_fence_enabled || !oq4_fence_collecting ||
        !oq4_fence_healthy || !OQ4_WIN_QueueHealthy() ||
        OQ4_WindowsNativeFenceQueueGeneration() != context->generation) return false;
    oq4_fence_activity = true;
    return true;
}
/* Pairing survives retirement clearing public pending state. No new registration
 * can replace the copied callbacks/userdata until this cleanup has unwound. */
static bool OQ4_FenceCloseHooks(bool aborted)
{
    bool returned = false, accepted = true;
    OQ4_NativeCollectionContext context = oq4_fence_context;
#ifdef _MSC_VER
    __try {
#endif
        if (oq4_fence_prepare_attempted) {
            oq4_fence_prepare_attempted = false;
            ++oq4_fence_callback_depth;
#ifdef _MSC_VER
            __try {
#endif
                accepted = oq4_fence_hooks.Finish(oq4_fence_hooks.userdata, &context, aborted);
#ifdef _MSC_VER
            } __finally {
#endif
                --oq4_fence_callback_depth;
#ifdef _MSC_VER
            }
#endif
            accepted = accepted && OQ4_FenceContextMatches(&context);
        }
        returned = true;
#ifdef _MSC_VER
    } __finally {
#endif
        oq4_fence_prepare_attempted = false;
        oq4_fence_context_active = false;
        oq4_fence_in_pump = false;
        if (aborted || !returned || !accepted) oq4_fence_collecting = false;
        if (!returned || !accepted) OQ4_FenceFault();
#ifdef _MSC_VER
    }
#endif
    return accepted;
}
bool OQ4_WindowsNativeFenceRetire(void)
{
    if (!OQ4_FenceMain()) return false;
    OQ4_WIN_QueueRetire();
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
    if (enabled && (oq4_fence_emitting || oq4_fence_in_pump || oq4_fence_callback_depth || oq4_fence_prepare_attempted || oq4_fence_context_active)) return false;
    if (!enabled) return OQ4_WindowsNativeFenceRetire();
    if (oq4_fence_enabled) return oq4_fence_healthy && OQ4_WIN_QueueHealthy();
    if (oq4_fence_collecting || oq4_fence_held || oq4_fence_dispatch == SDL_MAX_UINT64 ||
        oq4_fence_sequence == SDL_MAX_UINT64 || oq4_fence_marker == 0x7fffffffu) return false;
    if (!oq4_fence_type) {
        oq4_fence_type = SDL_RegisterEvents(1);
        if (!oq4_fence_type) return false;
        SDL_SetAtomicInt(&oq4_fence_public_type, (int)oq4_fence_type);
    }
    if (!OQ4_WIN_QueueEnable()) return false;
    oq4_fence_enabled = oq4_fence_healthy = true;
    return true;
}
bool OQ4_WindowsNativeFenceHealthy(void) { return OQ4_FenceMain() && oq4_fence_enabled && oq4_fence_healthy && OQ4_WIN_QueueHealthy(); }
Uint32 OQ4_WindowsNativeFenceEventType(void) { return OQ4_FenceMain() ? oq4_fence_type : 0; }
bool OQ4_WindowsNativeFencePending(OQ4_NativeFence *out)
{
    if (!OQ4_FenceMain() || !out || !oq4_fence_enabled || (!oq4_fence_healthy || !OQ4_WIN_QueueHealthy()) || !oq4_fence_held ||
        !oq4_fence_published || oq4_fence_collecting || oq4_fence_emitting) return false;
    *out = oq4_fence_pending;
    return true;
}
bool OQ4_WindowsNativeFenceCopy(const SDL_Event *marker, OQ4_NativeFence *out)
{
    if (!OQ4_FenceMain() || !out || !oq4_fence_enabled || (!oq4_fence_healthy || !OQ4_WIN_QueueHealthy()) || !oq4_fence_held ||
        !oq4_fence_published || oq4_fence_collecting || oq4_fence_emitting || !OQ4_FenceMarkerMatches(marker) ||
        !OQ4_WIN_QueueVerified(oq4_fence_pending.dispatch, oq4_fence_pending.sequence)) return false;
    *out = oq4_fence_pending;
    oq4_fence_copied = true;
    return true;
}
bool OQ4_WindowsNativeFenceAck(Uint64 dispatch, Uint64 sequence)
{
    if (!OQ4_FenceMain() || !oq4_fence_enabled || (!oq4_fence_healthy || !OQ4_WIN_QueueHealthy()) || !oq4_fence_held ||
        !oq4_fence_published || !oq4_fence_copied || oq4_fence_collecting || oq4_fence_emitting ||
        !dispatch || dispatch != oq4_fence_pending.dispatch || !sequence || sequence != oq4_fence_pending.sequence) return false;
    if (!OQ4_WIN_QueueAcknowledge(dispatch, sequence)) return false;
    oq4_fence_held = oq4_fence_published = oq4_fence_copied = false;
    SDL_zero(oq4_fence_pending);
    return true;
}
bool OQ4_WIN_NativeFenceEnabled(void) { return SDL_IsMainThread() && oq4_fence_enabled; }
bool OQ4_WIN_NativeFenceBlocked(void)
{
    if (!SDL_IsMainThread()) return true;
    return oq4_fence_in_pump || oq4_fence_emitting || oq4_fence_callback_depth || oq4_fence_prepare_attempted ||
        (oq4_fence_enabled && ((!oq4_fence_healthy || !OQ4_WIN_QueueHealthy()) || oq4_fence_collecting || oq4_fence_held));
}
static bool OQ4_FenceBeginCollection(Uint32 kind)
{
    bool ready = false;
    if (!SDL_IsMainThread()) return false;
    if (!oq4_fence_enabled) return true;
    if (OQ4_WIN_NativeFenceBlocked()) return false;
    if (oq4_fence_dispatch == SDL_MAX_UINT64) { OQ4_FenceFault(); return false; }
    SDL_zero(oq4_fence_pending);
    oq4_fence_pending.version = 1;
    oq4_fence_pending.dispatch = ++oq4_fence_dispatch;
    if (!OQ4_WIN_QueueBegin(oq4_fence_pending.dispatch)) { OQ4_FenceFault(); return false; }
    oq4_fence_in_pump = oq4_fence_collecting = true;
    oq4_fence_activity = false;
    oq4_fence_context.version = 1;
    oq4_fence_context.kind = kind;
    oq4_fence_context.generation = OQ4_WindowsNativeFenceQueueGeneration();
    oq4_fence_context.dispatch = oq4_fence_pending.dispatch;
    oq4_fence_context_active = true;
#ifdef _MSC_VER
    __try {
#endif
        bool accepted = true;
        if (oq4_fence_hooks.Prepare) {
            OQ4_NativeCollectionContext context = oq4_fence_context;
            oq4_fence_prepare_attempted = true;
            ++oq4_fence_callback_depth;
#ifdef _MSC_VER
            __try {
#endif
                accepted = oq4_fence_hooks.Prepare(oq4_fence_hooks.userdata, &context);
#ifdef _MSC_VER
            } __finally {
#endif
                --oq4_fence_callback_depth;
#ifdef _MSC_VER
            }
#endif
            accepted = accepted && OQ4_FenceContextMatches(&context);
        }
        ready = accepted && oq4_fence_enabled && oq4_fence_collecting && oq4_fence_healthy && OQ4_WIN_QueueHealthy() &&
            oq4_fence_context.generation && OQ4_WindowsNativeFenceQueueGeneration() == oq4_fence_context.generation;
#ifdef _MSC_VER
    } __finally {
#endif
        if (!ready) {
            OQ4_FenceFault();
            (void)OQ4_FenceCloseHooks(true);
            oq4_fence_collecting = false;
        }
#ifdef _MSC_VER
    }
#endif
    return ready;
}
bool OQ4_WIN_BeginNativeCollection(void) { return OQ4_FenceBeginCollection(OQ4_COLLECTION_PUMP); }
void OQ4_WIN_EndNativeCollection(bool removed_message)
{
    SDL_Event marker;
    bool queued = false, intact = false, returned = false;
    if (!SDL_IsMainThread()) return;
    if (oq4_fence_callback_depth) { OQ4_FenceFault(); return; }
    if (!OQ4_FenceCloseHooks(!oq4_fence_enabled || !oq4_fence_collecting || !oq4_fence_healthy || !OQ4_WIN_QueueHealthy())) return;
    if (!oq4_fence_enabled || !oq4_fence_collecting) return;
    oq4_fence_collecting = false;
    if ((!oq4_fence_healthy || !OQ4_WIN_QueueHealthy())) return;
    if (!OQ4_WIN_QueueEnd(oq4_fence_pending.dispatch, &oq4_fence_pending.event_count)) { OQ4_FenceFault(); return; }
    if (!removed_message && !oq4_fence_activity && !oq4_fence_pending.event_count) {
        OQ4_WIN_QueueEmpty(oq4_fence_pending.dispatch);
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
        oq4_fence_admission = oq4_fence_queue_admission = NULL;
        oq4_fence_emitting = false;
        if (!returned) OQ4_FenceFault();
#ifdef _MSC_VER
    }
#endif
    if (!oq4_fence_enabled || (!oq4_fence_healthy || !OQ4_WIN_QueueHealthy()) || !queued || !intact) { OQ4_FenceFault(); return; }
    oq4_fence_published = true;
}
void OQ4_WIN_AbortNativeCollection(void)
{
    if (!SDL_IsMainThread()) return;
    if (oq4_fence_callback_depth) { OQ4_FenceFault(); return; }
    (void)OQ4_FenceCloseHooks(true);
    oq4_fence_collecting = false;
    if (oq4_fence_enabled) OQ4_FenceFault();
}
bool OQ4_WindowsNativeFenceRunLifecycle(OQ4_NativeCollectionWork work, void *userdata)
{
    bool completed = false;
    OQ4_NativeCollectionContext context;
    if (!OQ4_FenceMain() || !work || !oq4_fence_enabled || OQ4_WIN_NativeFenceBlocked()) return false;
    if (!OQ4_FenceBeginCollection(OQ4_COLLECTION_LIFECYCLE)) return false;
    context = oq4_fence_context;
#ifdef _MSC_VER
    __try {
#endif
        ++oq4_fence_callback_depth;
#ifdef _MSC_VER
        __try {
#endif
            completed = work(userdata, &context);
#ifdef _MSC_VER
        } __finally {
#endif
            --oq4_fence_callback_depth;
#ifdef _MSC_VER
        }
#endif
        completed = completed && OQ4_FenceContextMatches(&context) && oq4_fence_enabled && oq4_fence_collecting &&
            oq4_fence_healthy && OQ4_WIN_QueueHealthy();
#ifdef _MSC_VER
    } __finally {
#endif
        if (completed) OQ4_WIN_EndNativeCollection(true); else OQ4_WIN_AbortNativeCollection();
#ifdef _MSC_VER
    }
#endif
    return completed && oq4_fence_enabled && oq4_fence_healthy && OQ4_WIN_QueueHealthy() && oq4_fence_published;
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
    if (!oq4_fence_collecting || (!oq4_fence_healthy || !OQ4_WIN_QueueHealthy())) { OQ4_FenceFault(); return; }
    oq4_fence_activity = true;
}
Uint64 OQ4_WIN_CurrentNativeDispatch(void)
{
    return SDL_IsMainThread() && oq4_fence_enabled && oq4_fence_healthy && OQ4_WIN_QueueHealthy() && oq4_fence_collecting ? oq4_fence_pending.dispatch : 0;
}
bool OQ4_WIN_BeginFenceAdmission(const SDL_Event *event)
{
    if (!SDL_IsMainThread() || !oq4_fence_enabled || (!oq4_fence_healthy || !OQ4_WIN_QueueHealthy()) || !oq4_fence_emitting ||
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
        if (!oq4_fence_enabled || (!oq4_fence_healthy || !OQ4_WIN_QueueHealthy()) || !oq4_fence_emitting || !OQ4_FenceMarkerMatches(event)) {
            OQ4_FenceFault(); return false;
        }
        oq4_fence_queue_admission = event;
    } else if (event && oq4_fence_type && event->type == oq4_fence_type) {
        /* Reserved private markers have exactly one emission admission. */
        if (oq4_fence_enabled) OQ4_FenceFault();
        return false;
    }
    return true;
}
bool OQ4_WIN_FenceCanPoll(void)
{
    return SDL_IsMainThread() && oq4_fence_enabled && oq4_fence_healthy && OQ4_WIN_QueueHealthy() &&
        !oq4_fence_in_pump && !oq4_fence_collecting && !oq4_fence_emitting;
}
int OQ4_WIN_ClaimFenceQueueAdmission(const SDL_Event *event, Uint64 *dispatch, Uint64 *sequence)
{
    const Uint32 type = (Uint32)SDL_GetAtomicInt(&oq4_fence_public_type);
    if (!type || event->type != type) return 0;
    if (!SDL_IsMainThread()) return -1;
    if (!oq4_fence_enabled || !oq4_fence_healthy || !OQ4_WIN_QueueHealthy() || !oq4_fence_emitting ||
        event != oq4_fence_queue_admission || !OQ4_FenceMarkerMatches(event)) return -1;
    oq4_fence_queue_admission = NULL;
    *dispatch = oq4_fence_pending.dispatch; *sequence = oq4_fence_pending.sequence;
    return 1;
}
void OQ4_WIN_CompleteFenceEvent(const SDL_Event *event, bool accepted)
{
    if (!SDL_IsMainThread() || !oq4_fence_enabled || !oq4_fence_collecting) return;
    /* Actual successful admission is counted at SDL_AddEvent, including Peep.
     * Public filter rejection still poisons this native collection. */
    if (!accepted || !event) OQ4_FenceFault();
}
#endif
