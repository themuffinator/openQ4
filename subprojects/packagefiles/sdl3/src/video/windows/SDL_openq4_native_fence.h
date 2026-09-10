/* openQ4 private implementation declarations; zlib license. */
#ifndef SDL_openq4_windows_native_fence_h_
#define SDL_openq4_windows_native_fence_h_
#include <SDL3/SDL_openq4_native_fence.h>
/* Queue methods are internally synchronized by SDL_EventQ.lock. Health is an
 * atomic snapshot; worker admission/removal never touches main provider fields. */
bool OQ4_WIN_QueueEnable(void);
void OQ4_WIN_QueueRetire(void);
bool OQ4_WIN_QueueHealthy(void);
bool OQ4_WIN_QueueBegin(Uint64 dispatch);
bool OQ4_WIN_QueueEnd(Uint64 dispatch, Uint32 *count);
void OQ4_WIN_QueueEmpty(Uint64 dispatch);
bool OQ4_WIN_QueueVerified(Uint64 dispatch, Uint64 sequence);
bool OQ4_WIN_QueueAcknowledge(Uint64 dispatch, Uint64 sequence);
bool OQ4_WIN_FenceCanPoll(void);
int OQ4_WIN_ClaimFenceQueueAdmission(const SDL_Event *event, Uint64 *dispatch, Uint64 *sequence);
bool OQ4_WIN_NativeFenceEnabled(void);
bool OQ4_WIN_NativeFenceBlocked(void);
bool OQ4_WIN_BeginNativeCollection(void);
void OQ4_WIN_EndNativeCollection(bool removed_message);
void OQ4_WIN_AbortNativeCollection(void);
void OQ4_WIN_NativeFenceLifecycle(void);
void OQ4_WIN_NativeFenceShutdown(void);
void OQ4_WIN_NativeFenceMessage(unsigned int message);
Uint64 OQ4_WIN_CurrentNativeDispatch(void);
/* Captured before public event watchers. A synthetic/reentrant Push cannot
 * claim the emitting fence's admission, even if it reuses its event pointer. */
bool OQ4_WIN_BeginFenceAdmission(const SDL_Event *event);
bool OQ4_WIN_ValidateFenceAdmission(const SDL_Event *event, bool admission);
void OQ4_WIN_CompleteFenceEvent(const SDL_Event *event, bool accepted);
#endif
