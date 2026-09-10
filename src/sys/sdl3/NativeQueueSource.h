// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "NativeQueueBatch.h"
namespace openq4 {
// Root supplies a checked, non-reused module epoch. This object lives within one
// SDL module load only; constructing another object cannot legitimize stale data.
// Unload/reload requires root's verified retirement and a new module epoch.
class SdlNativeQueueSource final : public NativeQueueSource {
public:
    explicit SdlNativeQueueSource(std::uint64_t checkedProviderEpoch):epoch(checkedProviderEpoch) {}
    bool Observe(NativeQueueStatus& out,std::string& error) override;
    int Poll(SDL_Event& event,OQ4_NativeQueueRecord& record) override;
    bool CopyFence(const SDL_Event& event,OQ4_NativeFence& out) override;
private:
    const std::uint64_t epoch;
};
} // namespace openq4
