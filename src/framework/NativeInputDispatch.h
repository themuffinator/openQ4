// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <cstdint>
typedef struct sysEvent_s sysEvent_t;
class idEventLoop;

// Engine-private C++17 facade. No native provider is enabled here. An absent
// owner preserves the existing input path; a faulted installed owner blocks it.
// All calls are made on the bound input thread and outside storage locks.
enum class nativeInputSessionResult_t { Legacy, Owned, Stop };
nativeInputSessionResult_t NativeInput_TakeSession(sysEvent_t&) noexcept;
nativeInputSessionResult_t NativeInput_TakeDeferredSession(sysEvent_t&) noexcept;
bool NativeInput_CompleteSession() noexcept; // after ProcessEvent payload scope
void NativeInput_AbortDelivery() noexcept;
bool NativeInput_SessionCurrent() noexcept; // before EACH foreign Session call
void NativeInput_BeginLegacySession() noexcept;
void NativeInput_EndLegacySession() noexcept;
bool NativeInput_BeginMouse() noexcept;
bool NativeInput_NextMouse(int& action, int& value) noexcept;
bool NativeInput_CompleteMouse() noexcept;
void NativeInput_EndMouse() noexcept;
bool NativeInput_BeginKeyboard() noexcept;
bool NativeInput_NextKeyboard(int& key, bool& down) noexcept;
bool NativeInput_CompleteKeyboard() noexcept;
void NativeInput_EndKeyboard() noexcept;
// Runs only original SessionDeferred work; never pumps or runs commands/tics.
void NativeInput_ContinueDeferred(idEventLoop&) noexcept;
void NativeInput_BeginFrame(std::uint64_t presentation) noexcept;
// True owns ALL message collection, including raw Win32 fallback/housekeeping.
bool NativeInput_OwnsPump() noexcept;
// Checked pre-effect observation for the next native group. No pump/lease is
// granted; the coordinator completion and original owner are checked again.
bool NativeInput_CanPrepareCollection() noexcept;
bool NativeInput_FatalRetire() noexcept;
bool NativeInput_Inhibited() noexcept;
bool NativeInput_TypedPollDelivery() noexcept;
// Private sink observation only, valid during the original driver operation.
bool NativeInput_HeldSourceCurrent(std::uint64_t route,std::uint64_t window) noexcept;
// Local usercmd state only; preserve INHIBIT_SESSION and other owners' bits.
void Usercmd_NativeInputChanged() noexcept;
void Usercmd_NativeInputSource(std::uint64_t route,std::uint64_t window,std::uint64_t device,
    unsigned source,int key,bool down) noexcept;
