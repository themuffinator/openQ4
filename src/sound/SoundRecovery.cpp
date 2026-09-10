// First-party openQ4 portable audio recovery values. GPL-3.0-or-later.
#include "SoundRecovery.h"
#include <bit>
#include <cstdint>
#include <limits>
#include <tuple>

namespace {
bool Fail(std::string& error,const char* message) noexcept {
    try { error=message; } catch (...) { error.clear(); } return false;
}
bool Utf8(const std::string& value,bool required=false) noexcept {
    if ((required && value.empty()) || value.size()>SoundRecoveryMaxString) return false;
    for (std::size_t i=0;i<value.size();) {
        const auto c=static_cast<unsigned char>(value[i++]);
        if (!c) return false;
        if (c<0x80) continue;
        unsigned n=0; std::uint32_t scalar=0,minimum=0;
        if (c>=0xc2 && c<=0xdf) { n=1;scalar=c&31;minimum=0x80; }
        else if (c>=0xe0 && c<=0xef) { n=2;scalar=c&15;minimum=0x800; }
        else if (c>=0xf0 && c<=0xf4) { n=3;scalar=c&7;minimum=0x10000; }
        else return false;
        if (n>value.size()-i) return false;
        while (n--) { const auto next=static_cast<unsigned char>(value[i++]);if ((next&0xc0)!=0x80) return false;scalar=(scalar<<6)|(next&63); }
        if (scalar<minimum || scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff)) return false;
    }
    return true;
}
bool Policy(SoundSettingsPolicy p) noexcept { return (p.speakers==2 || p.speakers==6) && p.maxEmitterChannels>=1 && p.maxEmitterChannels<=48; }
bool SamePolicy(SoundSettingsPolicy a,SoundSettingsPolicy b) noexcept { return a.speakers==b.speakers && a.efx==b.efx && a.maxEmitterChannels==b.maxEmitterChannels; }
bool Provider(const SoundRecoveryProvider& p) noexcept { return Utf8(p.vendor,true)&&Utf8(p.renderer,true)&&Utf8(p.version,true); }
const char* Mode(SoundRecoveryMode value) noexcept {
    switch(value) {
    case SoundRecoveryMode::Mono:return "mono";case SoundRecoveryMode::Stereo:return "stereo";
    case SoundRecoveryMode::StereoBasic:return "stereo-basic";case SoundRecoveryMode::StereoUhj:return "stereo-uhj";
    case SoundRecoveryMode::StereoHrtf:return "stereo-hrtf";case SoundRecoveryMode::Quad:return "quad";
    case SoundRecoveryMode::Surround51:return "surround-5.1";case SoundRecoveryMode::Surround61:return "surround-6.1";
    case SoundRecoveryMode::Surround71:return "surround-7.1";case SoundRecoveryMode::StereoFamily:return "stereo-family";
    } return nullptr;
}
const char* Hrtf(SoundRecoveryHrtf value) noexcept {
    switch(value) {
    case SoundRecoveryHrtf::ExactOff:return "exact-off";case SoundRecoveryHrtf::ExactOn:return "exact-on";
    case SoundRecoveryHrtf::RequestOff:return "request-off";case SoundRecoveryHrtf::RequestOn:return "request-on";
    case SoundRecoveryHrtf::RequestAuto:return "request-auto";
    } return nullptr;
}
bool Stereo(SoundRecoveryMode mode) noexcept {
    return mode==SoundRecoveryMode::Stereo || mode==SoundRecoveryMode::StereoBasic ||
        mode==SoundRecoveryMode::StereoUhj || mode==SoundRecoveryMode::StereoHrtf;
}
bool Valid(const SoundRecoveryData& d) noexcept {
    if ((d.form!=SoundRecoveryForm::Exact && d.form!=SoundRecoveryForm::Request) || !Policy(d.requested) ||
        d.hrtfPolicy<0 || d.hrtfPolicy>2 || !Provider(d.provider) || !Utf8(d.requestedDevice) ||
        !Utf8(d.actualDevice,true) || !Utf8(d.defaultDevice) || !Utf8(d.hrtfSpecifier) || !Mode(d.mode) || !Hrtf(d.hrtf)) return false;
    const bool on=d.hrtf==SoundRecoveryHrtf::ExactOn || d.hrtf==SoundRecoveryHrtf::RequestOn;
    const bool off=d.hrtf==SoundRecoveryHrtf::ExactOff || d.hrtf==SoundRecoveryHrtf::RequestOff;
    if (d.form==SoundRecoveryForm::Exact) {
        if (d.mode==SoundRecoveryMode::StereoFamily || (!on && !off) ||
            (d.hrtf!=SoundRecoveryHrtf::ExactOn && d.hrtf!=SoundRecoveryHrtf::ExactOff) || d.hrtfReason<0 || d.hrtfReason>65535) return false;
    } else if (d.hrtfReason!=0) return false;
    if (d.hrtf==SoundRecoveryHrtf::ExactOn ? d.hrtfSpecifier.empty() : !d.hrtfSpecifier.empty()) return false;
    if (d.mode==SoundRecoveryMode::StereoHrtf && !on) return false;
    if (on && d.mode!=SoundRecoveryMode::Stereo && d.mode!=SoundRecoveryMode::StereoHrtf && d.mode!=SoundRecoveryMode::StereoFamily) return false;
    if (d.hrtf==SoundRecoveryHrtf::RequestAuto && d.mode!=SoundRecoveryMode::StereoFamily) return false;
    return true;
}
bool AllowedUse(const SoundRecoveryData& d,SoundRecoveryUse use) noexcept {
    switch(use) {
    case SoundRecoveryUse::Restore:case SoundRecoveryUse::ConfirmedTarget:return d.form==SoundRecoveryForm::Exact;
    case SoundRecoveryUse::PendingTarget:return true;
    } return false;
}
bool ExactInt(const SoundRecoveryValue& value,int low,int high,int& result) noexcept {
    const auto* number=std::get_if<double>(&value);if (!number) return false;
    const auto bits=std::bit_cast<std::uint64_t>(*number);
    if ((bits&UINT64_C(0x7ff0000000000000))==UINT64_C(0x7ff0000000000000)) return false;
    if (*number<low || *number>high) return false;
    const int integer=static_cast<int>(*number);
    auto normalized=bits;if ((bits<<1)==0) normalized=0;
    if (normalized!=std::bit_cast<std::uint64_t>(static_cast<double>(integer))) return false;
    result=integer;return true;
}
bool Bounded(const SoundRecoveryFields& fields) noexcept {
    if (fields.size()>19) return false;
    std::size_t bytes=0;
    for (const auto& [key,value]:fields) {
        if (key.size()>96) return false;
        const auto* str=std::get_if<std::string>(&value);
        if (str && !Utf8(*str)) return false;
        bytes+=key.size()+(str?str->size():sizeof(double));
        if (bytes>SoundRecoveryMaxBytes) return false;
    } return true;
}
void Put(SoundRecoveryFields& fields,const char* key,const char* value) {
    fields.emplace(std::piecewise_construct,std::forward_as_tuple(key),
        std::forward_as_tuple(std::in_place_type<std::string>,value));
}
void Put(SoundRecoveryFields& fields,const char* key,const std::string& value) {
    fields.emplace(std::piecewise_construct,std::forward_as_tuple(key),
        std::forward_as_tuple(std::in_place_type<std::string>,value));
}
void Put(SoundRecoveryFields& fields,const char* key,int value) { fields.emplace(key,static_cast<double>(value)); }
void Put(SoundRecoveryFields& fields,const char* key,bool value) { fields.emplace(key,value); }
void Fields(const SoundRecoveryData& d,SoundRecoveryFields& f) {
    Put(f,"version",1);Put(f,"backend","openal-extensions-v1");Put(f,"form",d.form==SoundRecoveryForm::Exact?"exact":"request");Put(f,"effect.kind","none");
    Put(f,"requested.speakers",d.requested.speakers);Put(f,"requested.efx",d.requested.efx);Put(f,"requested.emitterLimit",d.requested.maxEmitterChannels);
    Put(f,"dependency.hrtf",d.hrtfPolicy);Put(f,"dependency.efxDebug",d.efxDebug);
    Put(f,"provider.vendor",d.provider.vendor);Put(f,"provider.renderer",d.provider.renderer);Put(f,"provider.version",d.provider.version);
    Put(f,"device.requested",d.requestedDevice);Put(f,"device.actual",d.actualDevice);Put(f,"device.defaultAtCapture",d.defaultDevice);
    Put(f,"output.mode",Mode(d.mode));Put(f,"hrtf.mode",Hrtf(d.hrtf));Put(f,"hrtf.specifier",d.hrtfSpecifier);
    if (d.form==SoundRecoveryForm::Exact) Put(f,"hrtf.reason",d.hrtfReason);
}
bool SameRoute(const SoundRecoveryData& a,const SoundRecoveryData& b) noexcept {
    // The current default is only a capture diagnostic. Cold recovery selects
    // the saved actual logical route explicitly even if the default changed.
    return a.provider==b.provider && a.requestedDevice==b.requestedDevice && a.actualDevice==b.actualDevice &&
        a.hrtfPolicy==b.hrtfPolicy && a.efxDebug==b.efxDebug;
}
}

bool SoundRecovery_BuildObserved(const SoundRecoveryData& observed,SoundRecoveryRecord& output,std::string& error) {
    try {
        if (observed.form!=SoundRecoveryForm::Exact || !Valid(observed)) return Fail(error,"Invalid exact audio recovery data");
        std::unique_ptr<const SoundRecoveryData> candidate=std::make_unique<SoundRecoveryData>(observed);
        output.data.swap(candidate);error.clear();return true;
    } catch (...) { return Fail(error,"Audio recovery allocation failed"); }
}
bool SoundRecovery_BuildTarget(const SoundRecoveryRecord& baseline,SoundSettingsPolicy target,SoundRecoveryRecord& output,std::string& error) {
    try {
        const auto* before=baseline.Value();
        if (!before || before->form!=SoundRecoveryForm::Exact || !Valid(*before) || !Policy(target)) return Fail(error,"Invalid audio recovery target inputs");
        if (target.efx && target.efx!=before->requested.efx) return Fail(error,"Portable effect-enabled recovery is not qualified");
        auto candidate=std::make_unique<SoundRecoveryData>(*before);candidate->form=SoundRecoveryForm::Request;candidate->hrtfReason=0;candidate->requested=target;
        if (target.speakers!=before->requested.speakers) {
            candidate->hrtfSpecifier.clear();
            if (target.speakers==6) {
                if (before->hrtfPolicy==2) return Fail(error,"Surround and forced HRTF conflict");
                candidate->mode=SoundRecoveryMode::Surround51;candidate->hrtf=SoundRecoveryHrtf::RequestOff;
            } else {
                candidate->mode=before->hrtfPolicy==2?SoundRecoveryMode::StereoHrtf:SoundRecoveryMode::StereoFamily;
                candidate->hrtf=before->hrtfPolicy==2?SoundRecoveryHrtf::RequestOn:before->hrtfPolicy==1?SoundRecoveryHrtf::RequestOff:SoundRecoveryHrtf::RequestAuto;
            }
        }
        if (!Valid(*candidate)) return Fail(error,"Contradictory audio recovery target");
        std::unique_ptr<const SoundRecoveryData> published=std::move(candidate);output.data.swap(published);error.clear();return true;
    } catch (...) { return Fail(error,"Audio recovery allocation failed"); }
}
bool SoundRecovery_ValidateRealized(const SoundRecoveryRecord& request,const SoundRecoveryRecord& actual,std::string& error) {
    const auto* r=request.Value();const auto* a=actual.Value();
    if (!r || !a || !Valid(*r) || !Valid(*a) || a->form!=SoundRecoveryForm::Exact || !SameRoute(*r,*a) || !SamePolicy(r->requested,a->requested))
        return Fail(error,"Audio recovery target identity or requested policy changed");
    if (r->mode==SoundRecoveryMode::StereoFamily ? !Stereo(a->mode) : r->mode!=a->mode) return Fail(error,"Actual audio output differs from the target");
    const bool on=a->hrtf==SoundRecoveryHrtf::ExactOn;
    if (((r->hrtf==SoundRecoveryHrtf::ExactOn || r->hrtf==SoundRecoveryHrtf::RequestOn) && !on) ||
        ((r->hrtf==SoundRecoveryHrtf::ExactOff || r->hrtf==SoundRecoveryHrtf::RequestOff) && on) ||
        (r->hrtf==SoundRecoveryHrtf::ExactOn && r->hrtfSpecifier!=a->hrtfSpecifier)) return Fail(error,"Actual HRTF differs from the target");
    error.clear();return true;
}
bool SoundRecovery_Encode(const SoundRecoveryRecord& record,SoundRecoveryFields& output,std::string& error) {
    try {
        const auto* data=record.Value();if (!data || !Valid(*data)) return Fail(error,"No valid audio recovery record");
        SoundRecoveryFields candidate(std::initializer_list<SoundRecoveryFields::value_type>{});
        Fields(*data,candidate);output.swap(candidate);error.clear();return true;
    } catch (...) { return Fail(error,"Audio recovery allocation failed"); }
}
bool SoundRecovery_Decode(const SoundRecoveryFields& input,SoundRecoveryUse use,SoundRecoveryRecord& output,std::string& error) {
    try {
        if (!Bounded(input)) return Fail(error,"Audio recovery map exceeds its bounds");
        const auto get=[&](const char* key)->const SoundRecoveryValue& {return input.at(key);};
        const auto str=[&](const char* key)->const std::string& {return std::get<std::string>(get(key));};
        const auto number=[&](const char* key,int low,int high,int& value) {return ExactInt(get(key),low,high,value);};
        auto candidate=std::make_unique<SoundRecoveryData>();int version=0;
        if (!number("version",1,1,version) || str("backend")!="openal-extensions-v1" || str("effect.kind")!="none") return Fail(error,"Unsupported audio recovery grammar");
        if (str("form")=="exact") candidate->form=SoundRecoveryForm::Exact;
        else if (str("form")=="request") candidate->form=SoundRecoveryForm::Request;
        else return Fail(error,"Unknown audio recovery form");
        if (input.size()!=(candidate->form==SoundRecoveryForm::Exact?19u:18u)) return Fail(error,"Unknown or missing audio recovery field");
        if (!number("requested.speakers",2,6,candidate->requested.speakers) ||
            !number("requested.emitterLimit",1,48,candidate->requested.maxEmitterChannels) ||
            !number("dependency.hrtf",0,2,candidate->hrtfPolicy) ||
            !number("dependency.efxDebug",(std::numeric_limits<int>::min)(),(std::numeric_limits<int>::max)(),candidate->efxDebug)) return Fail(error,"Invalid audio recovery integer");
        candidate->requested.efx=std::get<bool>(get("requested.efx"));
        candidate->provider.vendor=str("provider.vendor");candidate->provider.renderer=str("provider.renderer");candidate->provider.version=str("provider.version");
        candidate->requestedDevice=str("device.requested");candidate->actualDevice=str("device.actual");candidate->defaultDevice=str("device.defaultAtCapture");candidate->hrtfSpecifier=str("hrtf.specifier");
        bool found=false;
        for (int i=0;i<=int(SoundRecoveryMode::StereoFamily);++i) if (str("output.mode")==Mode(static_cast<SoundRecoveryMode>(i))) {candidate->mode=static_cast<SoundRecoveryMode>(i);found=true;break;}
        if (!found) return Fail(error,"Unknown audio output mode");
        found=false;
        for (int i=0;i<=int(SoundRecoveryHrtf::RequestAuto);++i) if (str("hrtf.mode")==Hrtf(static_cast<SoundRecoveryHrtf>(i))) {candidate->hrtf=static_cast<SoundRecoveryHrtf>(i);found=true;break;}
        if (!found || (candidate->form==SoundRecoveryForm::Exact && !number("hrtf.reason",0,65535,candidate->hrtfReason)) ||
            !Valid(*candidate) || !AllowedUse(*candidate,use)) return Fail(error,"Invalid audio recovery state or semantics");
        std::unique_ptr<const SoundRecoveryData> published=std::move(candidate);output.data.swap(published);error.clear();return true;
    } catch (...) { return Fail(error,"Malformed audio recovery map or allocation failure"); }
}
bool SoundRecovery_Resolve(const SoundRecoveryRecord& record,const SoundRecoveryEnumeration& enumeration,SoundRecoveryResolved& output,std::string& error) {
    try {
        const auto* data=record.Value();
        if (!data || !Valid(*data) || !Provider(enumeration.provider) || !(data->provider==enumeration.provider) ||
            enumeration.devices.empty() || enumeration.devices.size()>SoundRecoveryMaxDevices || enumeration.hrtfs.size()>SoundRecoveryMaxHrtfs ||
            enumeration.hrtfDevice!=data->actualDevice) return Fail(error,"Audio enumeration identity or bounds changed");
        std::size_t bytes=0;unsigned devices=0,hrtfs=0;int index=-1;
        for (const auto& device:enumeration.devices) {
            if (!Utf8(device,true)) return Fail(error,"Invalid audio device specifier");
            bytes+=device.size();if (device==data->actualDevice) ++devices;
        }
        for (std::size_t i=0;i<enumeration.hrtfs.size();++i) {
            const auto& hrtf=enumeration.hrtfs[i];if (!Utf8(hrtf,true)) return Fail(error,"Invalid HRTF specifier");
            bytes+=hrtf.size();if (hrtf==data->hrtfSpecifier) {++hrtfs;index=static_cast<int>(i);}
        }
        if (bytes>SoundRecoveryMaxBytes || devices!=1 || (data->hrtf==SoundRecoveryHrtf::ExactOn && hrtfs!=1) ||
            (data->hrtf==SoundRecoveryHrtf::RequestOn && enumeration.hrtfs.empty())) return Fail(error,"Audio device or HRTF is missing or ambiguous");
        auto candidate=std::make_unique<SoundRecoveryResolvedData>();candidate->plan=*data;
        candidate->hrtfIndex=data->hrtf==SoundRecoveryHrtf::ExactOn?index:-1;
        std::unique_ptr<const SoundRecoveryResolvedData> published=std::move(candidate);output.data.swap(published);error.clear();return true;
    } catch (...) { return Fail(error,"Audio resolution allocation failed"); }
}
