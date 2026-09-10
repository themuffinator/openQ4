// First-party openQ4 portable audio recovery values. GPL-3.0-or-later.
#pragma once
#include "SoundSettings.h"
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

// Identical value/map representation to the settings journal, without a UI or
// JSON dependency. This module validates one audio map, never journal ownership.
using SoundRecoveryValue=std::variant<double,bool,std::string>;
using SoundRecoveryFields=std::map<std::string,SoundRecoveryValue>;
enum class SoundRecoveryUse { Restore, PendingTarget, ConfirmedTarget };
enum class SoundRecoveryForm { Exact, Request };
enum class SoundRecoveryMode { Mono, Stereo, StereoBasic, StereoUhj, StereoHrtf,
    Quad, Surround51, Surround61, Surround71, StereoFamily };
enum class SoundRecoveryHrtf { ExactOff, ExactOn, RequestOff, RequestOn, RequestAuto };
struct SoundRecoveryProvider {
    std::string vendor{""}, renderer{""}, version{""};
    bool operator==(const SoundRecoveryProvider&) const = default;
};
struct SoundRecoveryData {
    SoundRecoveryForm form=SoundRecoveryForm::Exact;
    SoundSettingsPolicy requested;
    int hrtfPolicy=0, efxDebug=0;
    SoundRecoveryProvider provider;
    std::string requestedDevice{""}, actualDevice{""}, defaultDevice{""};
    SoundRecoveryMode mode=SoundRecoveryMode::Stereo;
    SoundRecoveryHrtf hrtf=SoundRecoveryHrtf::ExactOff;
    std::string hrtfSpecifier{""};
    int hrtfReason=0; // Exact-only diagnostic, not a recreation equality promise.
};

// Immutable, noncopyable owned value. Default/moved records are empty. All
// builders allocate/stage before noexcept pointer-swap publication; false keeps
// output unchanged. Inputs/error/output storage must not alias.
class SoundRecoveryRecord {
public:
    SoundRecoveryRecord() noexcept=default;
    SoundRecoveryRecord(SoundRecoveryRecord&&) noexcept=default;
    SoundRecoveryRecord& operator=(SoundRecoveryRecord&&) noexcept=default;
    SoundRecoveryRecord(const SoundRecoveryRecord&)=delete;
    SoundRecoveryRecord& operator=(const SoundRecoveryRecord&)=delete;
    const SoundRecoveryData* Value() const noexcept { return data.get(); }
private:
    std::unique_ptr<const SoundRecoveryData> data;
    friend bool SoundRecovery_BuildObserved(const SoundRecoveryData&,SoundRecoveryRecord&,std::string&);
    friend bool SoundRecovery_BuildTarget(const SoundRecoveryRecord&,SoundSettingsPolicy,SoundRecoveryRecord&,std::string&);
    friend bool SoundRecovery_Decode(const SoundRecoveryFields&,SoundRecoveryUse,SoundRecoveryRecord&,std::string&);
};

constexpr std::size_t SoundRecoveryMaxString=1023;
constexpr std::size_t SoundRecoveryMaxBytes=64*1024;
constexpr std::size_t SoundRecoveryMaxDevices=256;
constexpr std::size_t SoundRecoveryMaxHrtfs=256;

// None is the only effect grammar in v1. Actual effect/slot/direct/auxiliary
// filter resources require a separately qualified parameter-history extension.
// These pure functions are data validation, not native capture/authority.
bool SoundRecovery_BuildObserved(const SoundRecoveryData&,SoundRecoveryRecord&,std::string&);
bool SoundRecovery_BuildTarget(const SoundRecoveryRecord&,SoundSettingsPolicy,SoundRecoveryRecord&,std::string&);
bool SoundRecovery_ValidateRealized(const SoundRecoveryRecord& request,const SoundRecoveryRecord& actual,std::string&);
bool SoundRecovery_Encode(const SoundRecoveryRecord&,SoundRecoveryFields&,std::string&);
bool SoundRecovery_Decode(const SoundRecoveryFields&,SoundRecoveryUse,SoundRecoveryRecord&,std::string&);

// Copied enumeration for one provider and one selected logical device. A unique
// exact specifier match is not physical endpoint identity or live authority.
// No current system-default equality is required to resolve an explicit saved
// actual route. Caller must revalidate native ownership around execution.
struct SoundRecoveryEnumeration {
    SoundRecoveryProvider provider;
    std::vector<std::string> devices=std::vector<std::string>(std::size_t{0});
    std::string hrtfDevice{""};
    std::vector<std::string> hrtfs=std::vector<std::string>(std::size_t{0});
};
struct SoundRecoveryResolvedData {
    SoundRecoveryData plan;
    int hrtfIndex=-1; // Valid only for this copied enumeration, never persisted.
};
class SoundRecoveryResolved {
public:
    SoundRecoveryResolved() noexcept=default;
    SoundRecoveryResolved(SoundRecoveryResolved&&) noexcept=default;
    SoundRecoveryResolved& operator=(SoundRecoveryResolved&&) noexcept=default;
    SoundRecoveryResolved(const SoundRecoveryResolved&)=delete;
    SoundRecoveryResolved& operator=(const SoundRecoveryResolved&)=delete;
    const SoundRecoveryResolvedData* Value() const noexcept { return data.get(); }
private:
    std::unique_ptr<const SoundRecoveryResolvedData> data;
    friend bool SoundRecovery_Resolve(const SoundRecoveryRecord&,const SoundRecoveryEnumeration&,SoundRecoveryResolved&,std::string&);
};
bool SoundRecovery_Resolve(const SoundRecoveryRecord&,const SoundRecoveryEnumeration&,SoundRecoveryResolved&,std::string&);
