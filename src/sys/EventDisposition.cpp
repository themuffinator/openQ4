// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "EventDisposition.h"
#include "EventQueueContinuity.h"
#include <limits>
#include <mutex>
#include <thread>

namespace {
std::mutex dispositionMutex;
std::thread::id dispositionThread;
std::uint64_t dispositionEpoch = 0, dispositionHighwater = 0;
bool dispositionBound = false;
}
std::uint64_t Sys_BindEventDispositionThread() noexcept {
    try {
        std::lock_guard<std::mutex> lock(dispositionMutex);
        const auto current = std::this_thread::get_id();
        if (dispositionBound && dispositionThread != current) return 0;
        if (dispositionHighwater == (std::numeric_limits<std::uint64_t>::max)()) {
            dispositionEpoch = 0; return 0;
        }
        dispositionThread = current; dispositionBound = true;
        dispositionEpoch = ++dispositionHighwater;
        return dispositionEpoch;
    } catch (...) { return 0; }
}
bool Sys_RetireEventDispositionThread() noexcept {
    try {
        std::lock_guard<std::mutex> lock(dispositionMutex);
        if (!dispositionBound || dispositionThread != std::this_thread::get_id()) return false;
        dispositionEpoch = 0; return true;
    } catch (...) { return false; }
}
std::uint64_t Sys_EventDispositionEpoch() noexcept {
    try {
        std::lock_guard<std::mutex> lock(dispositionMutex);
        return dispositionBound && dispositionThread == std::this_thread::get_id() ? dispositionEpoch : 0;
    } catch (...) { return 0; }
}
bool Sys_EventDispositionBoundThread() noexcept {
    try {
        std::lock_guard<std::mutex> lock(dispositionMutex);
        return dispositionBound && dispositionThread == std::this_thread::get_id();
    } catch (...) { return false; }
}
bool Sys_EventDispositionTagCurrent(const sysEventDispositionTag_t& tag) noexcept {
    return tag.ShapeValid() && tag.dispatchEpoch == Sys_EventDispositionEpoch() && tag.streamToken == Sys_EventQueueToken();
}
