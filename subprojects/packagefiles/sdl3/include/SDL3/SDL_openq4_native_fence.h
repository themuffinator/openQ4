/* openQ4 private SDL extension. Copyright (C) 2026 DarkMatter Productions.
 * Distributed under SDL's zlib license. Not an upstream SDL or TSF API. */
#ifndef SDL_openq4_native_fence_h_
#define SDL_openq4_native_fence_h_
#include <SDL3/SDL_events.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct OQ4_NativeFence {
    Uint32 version, event_count;
    Uint64 dispatch, sequence;
} OQ4_NativeFence;
/* Desktop Windows, main thread only. Disabled by default. A collection is one
 * guarded PeekMessage plus optional Translate/Dispatch of its returned message,
 * including its existing input housekeeping and ALL synchronous callbacks. This is deliberately
 * not one WindowProc callback, SDL event, or rendered frame.
 *
 * Enable requires no pending collection/fence. Faults remain blocked until an
 * explicit Retire; enable then starts a new admission lifetime. The engine must
 * retire/reconcile its old native document and queues before enabling again.
 * Retire does not invoke a GUI/Session or preserve a live composition lease.
 * Dispatch/sequence/marker highwaters never reset within this provider lifetime.
 * Provider unload/replay needs an additional engine-owned epoch. */
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFenceEnable(bool enabled);
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFenceRetire(void);
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFenceHealthy(void);
extern SDL_DECLSPEC Uint32 SDLCALL OQ4_WindowsNativeFenceEventType(void);
/* No allocation or partial outputs. Pending does not grant acknowledgement;
 * Copy must first validate the actual successfully queued marker. Timestamp
 * changes are allowed; type/window/code/reserved/data changes are not. An
 * arbitrary filter/flush cannot release the hold; lost markers require Retire.
 * event_count is diagnostic Push-admission count, NOT consumed-event proof.
 * Removing an earlier public event while retaining its marker is not detected
 * by this first fence boundary. Preexisting records, direct Peep additions and
 * coalescing make count comparison insufficient. Copy validates a published
 * marker value, not exclusive queue consumption. Do not activate native delivery
 * until a checked SDL consumer/discard boundary has been integrated.
 * Every consumer must also check provider health before applying native data. */
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFencePending(OQ4_NativeFence *out);
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFenceCopy(const SDL_Event *marker, OQ4_NativeFence *out);
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFenceAck(Uint64 dispatch, Uint64 sequence);
#ifdef __cplusplus
}
#endif
#endif
