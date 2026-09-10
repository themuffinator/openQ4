// First-party openQ4 checked, in-place OpenAL settings boundary. GPL-3.0-or-later.
#ifndef OPENQ4_SOUND_SETTINGS_H
#define OPENQ4_SOUND_SETTINGS_H
#include <cstdint>

struct SoundSettingsPolicy {
	int speakers = 2;
	bool efx = false;
	int maxEmitterChannels = 48;
};
struct SoundSettingsLease {
	std::uint64_t owner = 0, request = 0, token = 0;
};
enum class SoundSettingsPhase : unsigned { Captured, Applied, Restored, Failed, Invalidated };
// These are receipts for successful checked setters, not unsupported OpenAL
// source-filter getter results or a claim about audible mixer output.
struct SoundSettingsSourceReceipt {
	std::uint64_t lifetime = 0;
	unsigned source = 0, directFilter = 0, auxiliaryFilter = 0, slot = 0;
};
struct SoundSettingsObservation {
	SoundSettingsPolicy requested;
	SoundSettingsPhase phase = SoundSettingsPhase::Captured;
	std::uint64_t deviceLifetime = 0, generation = 0, normalUpdate = 0;
	std::uint64_t routingGeneration = 0, routingUpdate = 0;
	std::uintptr_t device = 0, context = 0;
	char requestedDevice[1024] = {}, actualDevice[1024] = {}, defaultDevice[1024] = {};
	int requestedHrtf = 0, outputMode = 0, hrtfStatus = 0, effectType = 0;
	bool hrtf = false, connected = false, efx = false, filters = false, muted = false;
	// False during restore revalidation: effect fields are owned-resource
	// inventory, not verified slot/type state; effectType is -1 if unqueried.
	bool effectsVerified = false;
	unsigned effect = 0, slot = 0, sourceCount = 0;
	SoundSettingsSourceReceipt sources[96] = {};
};

// Policy and lease inputs are immutable value copies made before any native
// callback. The caller may change its original POD storage during a callback.
// All public calls are main-thread-only. No method writes CVars or clears flags.
// Begin precedes the caller's owned CVar patch. Apply/Restore require exact target/
// baseline settings and unchanged dependencies. Failed attempts retain ownership.
// Finish requires a successful subsequent *normal* sound Render for that generation.
// Mute is never changed or restored by this API: concurrent user mute changes are
// preserved, and the subsequent update must set listener gain to that current mute.
// False leaves copied outputs unchanged. Diagnostics are bounded, console-only.
bool SoundSettings_Begin(std::uint64_t owner, std::uint64_t request,
	SoundSettingsPolicy target, SoundSettingsLease& lease,
	SoundSettingsObservation& baseline, char* error, int errorSize);
// Release only an untouched Captured preparation after checking the original
// actual backend and requested dependencies. No native setters or policy adoption.
bool SoundSettings_CancelCaptured(SoundSettingsLease, SoundSettingsObservation&, char*, int);
bool SoundSettings_TryApply(SoundSettingsLease, SoundSettingsObservation&, char*, int);
bool SoundSettings_TryRestore(SoundSettingsLease, SoundSettingsObservation&, char*, int);
// Renew only a changed device-event watermark after proving the original live
// context/device/lifetime/names and every other dependency. Baseline CVars must
// already be restored by their owner. No native writes; no baseline replacement.
// Success invalidates old receipts and permits only TryRestore + normal update +
// Finish. It cannot resume target Apply or release the automatic restart block.
bool SoundSettings_RevalidateRestore(SoundSettingsLease, SoundSettingsObservation&, char*, int);
bool SoundSettings_Query(SoundSettingsLease, SoundSettingsObservation&, char*, int);
// Check the same actual completion as Finish, retaining the lease and legacy
// request caches through the caller's durable decision/configuration work.
// This observation is not a reusable authorization to release a later backend.
bool SoundSettings_CheckCompletion(SoundSettingsLease, SoundSettingsObservation&, char*, int);
bool SoundSettings_Finish(SoundSettingsLease, SoundSettingsObservation&, char*, int);
// Allocation-free abandonment keeps automatic restart blocked until explicit
// restore/Finish or hardware destruction. It never overwrites external settings.
// Nested calls during a request or normal-update validation are rejected unchanged.
void SoundSettings_Abandon(SoundSettingsLease) noexcept;

// Engine-private lifecycle hooks; no request authority is exposed by them.
bool SoundSettings_BlockAutomaticRestart() noexcept;
bool SoundSettings_NativeOperationCurrent() noexcept; // checked voice call guard
void SoundSettings_DeviceDestroyed() noexcept;
void SoundSettings_DeviceInitialized() noexcept;
void SoundSettings_DeviceEvent() noexcept; // device callback thread: atomic only
std::uint64_t SoundSettings_SourceCreated() noexcept;
bool SoundSettings_NormalUpdateBegin() noexcept;
void SoundSettings_NormalUpdateEnd(bool completed) noexcept;
struct SoundSettingsNormalUpdateScope {
	bool tracked=SoundSettings_NormalUpdateBegin(), completed=false;
	~SoundSettingsNormalUpdateScope() { if (tracked) SoundSettings_NormalUpdateEnd(completed); }
};
#endif
