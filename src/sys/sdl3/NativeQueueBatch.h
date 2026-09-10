// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <SDL3/SDL_openq4_native_fence.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openq4 {
struct NativeQueueStatus {
    bool mainThread = false, healthy = false;
    std::uint64_t providerEpoch = 0, generation = 0, engineToken = 0;
    Uint32 fenceEventType = 0;
    std::optional<OQ4_NativeFence> pending;
};
class NativeQueueSource {
public:
    virtual ~NativeQueueSource() = default;
    virtual bool Observe(NativeQueueStatus& out, std::string& error) = 0;
    virtual int Poll(SDL_Event& event, OQ4_NativeQueueRecord& record) = 0;
    virtual bool CopyFence(const SDL_Event& event, OQ4_NativeFence& out) = 0;
};
struct OwnedNativeQueueEvent {
    OQ4_NativeQueueRecord record{};
    // Only the selected, audited SDL member is copied. Dynamic pointers are
    // always null: text and candidates below own the corresponding bytes.
    SDL_Event header{};
    std::string text;
    std::vector<std::string> candidates;
    bool ignored = false;
};
struct NativeQueueReceipt {
    std::uint64_t ingress = 0, serial = 0;
    bool operator==(const NativeQueueReceipt&) const = default;
};
class NativeQueueBatch {
public:
    NativeQueueBatch(const NativeQueueBatch&) = delete;
    NativeQueueBatch& operator=(const NativeQueueBatch&) = delete;
    const std::vector<OwnedNativeQueueEvent>& Events() const { return events; }
    const NativeQueueStatus& Status() const { return status; }
    NativeQueueReceipt Receipt() const { return receipt; }
private:
    NativeQueueBatch() = default;
    friend class NativeQueueIngress;
    NativeQueueStatus status;
    NativeQueueReceipt receipt;
    std::vector<OwnedNativeQueueEvent> events;
};
enum class NativeQueueRead { Ready, Empty, Busy, RetireRequired };

// Queue membership proves ordering only. It is neither native text origin nor
// permission to insert text, mutate an editor, replay input or select a GUI.
// No pumping, ACK, enable/disable, GUI or native-editor calls occur here.
class NativeQueueIngress {
public:
    static constexpr std::size_t MaxEvents = 8192, MaxCollectionEvents = 4096;
    static constexpr std::size_t MaxPayloadBytes = 1024*1024, MaxCandidates = 256;
    NativeQueueIngress();
    NativeQueueIngress(const NativeQueueIngress&) = delete;
    NativeQueueIngress& operator=(const NativeQueueIngress&) = delete;
    // out is unchanged except on Ready. Source must supply readable SDL-owned
    // borrowed data; a bounded C string scan cannot validate arbitrary pointers.
    NativeQueueRead Read(NativeQueueSource&, std::unique_ptr<const NativeQueueBatch>& out, std::string& error);
    // Recheck before every later effect. The caller separately checks GUI/editor
    // ownership and native provenance. An exact external ACK must follow effects.
    bool Validate(NativeQueueSource&, NativeQueueReceipt, std::string& error);
    // Releases the published slot only after the source observes that exact
    // pending fence absent (ACK is external). Retained batch bytes stay immutable.
    bool Finish(NativeQueueSource&, NativeQueueReceipt, std::string& error);
    // Requires actual unavailable generation 0, followed by a strictly new queue
    // generation before reuse. Root owns verified module-epoch transitions.
    bool ResetAfterRetirement(NativeQueueSource&, std::string& error);
    bool NeedsRetirement() const { return phase == Phase::Retire; }
private:
    enum class Phase { Idle, Published, Retire };
    bool Fail(std::string& error, const char* message);
    bool CheckSource(NativeQueueSource&, const NativeQueueStatus&, bool fence, std::string&);
    Phase phase = Phase::Idle;
    bool calling = false, reentered = false, requireFreshGeneration = false;
    std::uint64_t identity = 0, serial = 0, lastEpoch = 0, lastGeneration = 0, lastSequence = 0, retirementEpoch = 0;
    NativeQueueStatus published;
};
} // namespace openq4
