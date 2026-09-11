// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "NativeInputDispatch.h"
// Selected by the build only when the client native driver is absent. These
// definitions preserve the ordinary path and never initialize native state.
nativeInputSessionResult_t NativeInput_TakeSession(sysEvent_t&) noexcept {return nativeInputSessionResult_t::Legacy;}
nativeInputSessionResult_t NativeInput_TakeDeferredSession(sysEvent_t&) noexcept {return nativeInputSessionResult_t::Stop;}
bool NativeInput_CompleteSession() noexcept {return false;}
void NativeInput_AbortDelivery() noexcept {}
bool NativeInput_SessionCurrent() noexcept {return true;}
void NativeInput_BeginLegacySession() noexcept {}
void NativeInput_EndLegacySession() noexcept {}
bool NativeInput_BeginMouse() noexcept {return false;}
bool NativeInput_NextMouse(int&,int&) noexcept {return false;}
bool NativeInput_CompleteMouse() noexcept {return false;}
void NativeInput_EndMouse() noexcept {}
bool NativeInput_BeginKeyboard() noexcept {return false;}
bool NativeInput_NextKeyboard(int&,bool&) noexcept {return false;}
bool NativeInput_CompleteKeyboard() noexcept {return false;}
void NativeInput_EndKeyboard() noexcept {}
void NativeInput_ContinueDeferred(idEventLoop&) noexcept {}
void NativeInput_BeginFrame(std::uint64_t) noexcept {}
bool NativeInput_OwnsPump() noexcept {return false;}
bool NativeInput_CanPrepareCollection() noexcept {return false;}
bool NativeInput_FatalRetire() noexcept {return true;}
bool NativeInput_Inhibited() noexcept {return false;}
bool NativeInput_TypedPollDelivery() noexcept {return false;}
bool NativeInput_HeldSourceCurrent(std::uint64_t,std::uint64_t) noexcept {return false;}
