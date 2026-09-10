/* openQ4 private SDL extension. Copyright (C) 2026 DarkMatter Productions.
 * Distributed under SDL's zlib license. Not an upstream SDL or TSF API. */
#ifndef SDL_openq4_native_fence_h_
#define SDL_openq4_native_fence_h_
#include <SDL3/SDL_events.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum OQ4_NativeQueueKind {
    OQ4_QUEUE_OUTSIDE = 0, OQ4_QUEUE_SENTINEL = 1,
    OQ4_QUEUE_COLLECTION = 2, OQ4_QUEUE_FENCE = 3
} OQ4_NativeQueueKind;
typedef struct OQ4_NativeQueueRecord {
    Uint32 version, kind, ordinal, reserved;
    Uint64 generation, queue_sequence, dispatch, fence_sequence;
} OQ4_NativeQueueRecord;
/* Main-thread, no implicit pump. 1 actual FIFO record, 0 empty, -1 unavailable.
 * Both outputs are unchanged on 0/-1. Copy dynamic payload immediately under
 * SDL's normal borrowed-payload lifetime contract. Buffer the entire ordered
 * collection through its verified Fence before native/GUI effects. Queue sequence
 * gaps may reflect harmless sentinel maintenance; use kind/group ordinal instead
 * of inferring membership from total counts or contiguous queue sequences.
 * SDL queue allocator/filter/log callbacks must return normally. */
extern SDL_DECLSPEC int SDLCALL OQ4_WindowsNativeFencePoll(SDL_Event *event, OQ4_NativeQueueRecord *record);

typedef struct OQ4_NativeFence {
    Uint32 version, event_count;
    Uint64 dispatch, sequence;
} OQ4_NativeFence;
typedef enum OQ4_NativeCollectionKind {
    OQ4_COLLECTION_PUMP = 1, OQ4_COLLECTION_LIFECYCLE = 2
} OQ4_NativeCollectionKind;
typedef struct OQ4_NativeCollectionContext {
    Uint32 version, kind;
    Uint64 generation, dispatch;
} OQ4_NativeCollectionContext;
typedef bool (SDLCALL *OQ4_NativeCollectionPrepare)(void *userdata, const OQ4_NativeCollectionContext *context);
typedef bool (SDLCALL *OQ4_NativeCollectionFinish)(void *userdata, const OQ4_NativeCollectionContext *context, bool aborted);
typedef bool (SDLCALL *OQ4_NativeCollectionWork)(void *userdata, const OQ4_NativeCollectionContext *context);
typedef struct OQ4_NativeCollectionHooks {
    Uint32 version;
    void *userdata;
    OQ4_NativeCollectionPrepare Prepare;
    OQ4_NativeCollectionFinish Finish;
} OQ4_NativeCollectionHooks;
/* Copied table; userdata remains caller-owned until successful unregister.
 * Register/unregister (NULL) only while disabled, on the main thread, with no
 * pump/emission/callback/cleanup outstanding. Both callbacks are required.
 * Prepare runs after queue collection admission and before native processing.
 * Finish runs once for EVERY attempted Prepare, even failed/retired/unwound,
 * before queue close/marker publication. Track whether your Prepare opened a
 * native store scope; failed Prepare must not close an older scope.
 * Callbacks may retire; no nested pump/lifecycle or registration is permitted.
 * They must not enter a GUI/Session. Context values are observation/order only,
 * not physical input, text ownership or a completed native fence. */
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFenceRegisterHooks(const OQ4_NativeCollectionHooks *hooks);
/* Exact current context only, including during Prepare/work/Finish. No implicit
 * pump, ownership or transaction. Preserves a fence for zero-SDL-event work. */
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFenceMarkActivity(const OQ4_NativeCollectionContext *context);
/* Enabled healthy provider only. One checked lifecycle collection, no Peek.
 * Forces a held fence even for no public events; never drains or acknowledges.
 * Work/Prepare/Finish failure faults the provider; Retire is then required.
 * MSVC SEH cleanup preserves callback pairing; callbacks must not longjmp. */
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFenceRunLifecycle(OQ4_NativeCollectionWork work, void *userdata);
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
/* Main thread, under the SDL queue lock. Healthy active generation, or zero
 * when unavailable. Does not pump, enable, consume or acknowledge anything. */
extern SDL_DECLSPEC Uint64 SDLCALL OQ4_WindowsNativeFenceQueueGeneration(void);
extern SDL_DECLSPEC Uint32 SDLCALL OQ4_WindowsNativeFenceEventType(void);
/* No allocation or partial outputs. Pending does not grant acknowledgement;
 * Copy must first validate the actual successfully queued marker. Timestamp
 * changes are allowed; type/window/code/reserved/data changes are not. An
 * arbitrary filter/flush cannot release the hold; lost markers require Retire.
 * event_count is the exact count of Collection entries admitted to SDL's queue.
 * Preexisting/worker events are Outside; poll sentinels are Sentinel. Only checked
 * Poll can consume non-sentinel entries while enabled without invalidating health.
 * Copy/Ack additionally require an actual checked Fence receipt after every group
 * ordinal. Matching fabricated event bytes alone cannot authorize acknowledgement.
 * Every consumer must also check provider health before applying native data. */
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFencePending(OQ4_NativeFence *out);
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFenceCopy(const SDL_Event *marker, OQ4_NativeFence *out);
extern SDL_DECLSPEC bool SDLCALL OQ4_WindowsNativeFenceAck(Uint64 dispatch, Uint64 sequence);
#ifdef __cplusplus
}
#endif
#endif
