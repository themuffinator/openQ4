// First-party portable grammar and actual capture regressions. No native device.
// The runner inserts the counted query additions before the actual production.
// @QUERY_DOUBLES@
namespace RecoveryFake {
std::string vendor="OpenAL Community",renderer="OpenAL Soft",version="1.24 test";
std::string names=std::string("Physical\0Other\0\0",16),specifier="Default HRTF";
std::vector<std::string> hrtfs={"Default HRTF","Alternate HRTF"};
int countOverride=-1,failQuery=0;bool enumeration=true,providerNull=false;
void Query(){Fake::Tick();if(failQuery && Fake::calls==failQuery)Fake::alcError=ALC_INVALID_VALUE;}
const ALCchar* ALC_APIENTRY Stringi(ALCdevice*,ALCenum,ALCsizei i) noexcept {
    Query();return i>=0 && std::size_t(i)<hrtfs.size()?hrtfs[std::size_t(i)].c_str():nullptr;
}
}
extern "C" const ALchar* AL_APIENTRY alGetString(ALenum p) noexcept {
    RecoveryFake::Query();if(RecoveryFake::providerNull)return nullptr;
    return p==AL_VENDOR?RecoveryFake::vendor.c_str():p==AL_RENDERER?RecoveryFake::renderer.c_str():RecoveryFake::version.c_str();
}
// @TESTS@
#include <bit>
#include <limits>
#if defined(__SSE__ ) || defined(_M_X64)
#include <xmmintrin.h>
#endif
static void RecoveryReset() {
    ResetFixture();soundSystemLocal.hardware.voices.clear();Fake::sources.clear();Fake::filters.clear();
    RecoveryFake::vendor="OpenAL Community";RecoveryFake::renderer="OpenAL Soft";RecoveryFake::version="1.24 test";
    RecoveryFake::names.assign("Physical\0Other\0\0",16);RecoveryFake::specifier="Default HRTF";
    RecoveryFake::hrtfs={"Default HRTF","Alternate HRTF"};RecoveryFake::countOverride=-1;
    RecoveryFake::failQuery=0;RecoveryFake::enumeration=true;RecoveryFake::providerNull=false;
}
static SoundRecoveryData Data() {
    SoundRecoveryData d;d.requested={2,false,48};d.provider={"Vendor","Renderer","Version"};
    d.actualDevice="Logical device";d.defaultDevice="Captured default";return d;
}
static SoundRecoveryRecord Record(const SoundRecoveryData& d) {
    SoundRecoveryRecord r;std::string e(0,'\0');CHECK(SoundRecovery_BuildObserved(d,r,e));return r;
}
static SoundRecoveryEnumeration Enumeration(const SoundRecoveryData& d) {
    SoundRecoveryEnumeration e;e.provider=d.provider;e.devices={"Other",d.actualDevice};e.hrtfDevice=d.actualDevice;
    e.hrtfs={"Another",d.hrtfSpecifier.empty()?"Default":d.hrtfSpecifier};return e;
}
static void PortableGrammar() {
    std::string e(0,'\0');SoundRecoveryFields f;auto d=Data();auto r=Record(d);SoundRecoveryRecord decoded;
    CHECK(SoundRecovery_Encode(r,f,e));CHECK(f.size()==19);
    for(auto use:{SoundRecoveryUse::Restore,SoundRecoveryUse::PendingTarget,SoundRecoveryUse::ConfirmedTarget}) {
        CHECK(SoundRecovery_Decode(f,use,decoded,e));SoundRecoveryFields round;CHECK(SoundRecovery_Encode(decoded,round,e));CHECK(round==f);
    }
    CHECK(f.count("muted")==0&&f.count("device.pointer")==0&&f.count("hrtf.index")==0);
    // Every closed native output survives exact round-trip; requested surround
    // may already have fallen back to any observed mode before this transaction.
    for(int mode=0;mode<int(SoundRecoveryMode::StereoFamily);++mode) {
        d=Data();d.requested={6,true,1};d.mode=SoundRecoveryMode(mode);
        if(d.mode==SoundRecoveryMode::StereoHrtf){d.hrtf=SoundRecoveryHrtf::ExactOn;d.hrtfSpecifier="Measured \xf0\x9f\x8e\xa7";}
        r=Record(d);CHECK(SoundRecovery_Encode(r,f,e));CHECK(SoundRecovery_Decode(f,SoundRecoveryUse::Restore,decoded,e));
        CHECK(decoded.Value()->mode==d.mode&&decoded.Value()->requested.efx&&decoded.Value()->requested.speakers==6);
    }
    d=Data();d.efxDebug=(std::numeric_limits<int>::min)();d.hrtfReason=65535;r=Record(d);CHECK(SoundRecovery_Encode(r,f,e));
    CHECK(SoundRecovery_Decode(f,SoundRecoveryUse::Restore,decoded,e));CHECK(decoded.Value()->efxDebug==d.efxDebug);
    auto sentinel=Record(Data());const auto* prior=sentinel.Value();const auto good=f;
    for(const auto& [key,value]:good) {
        (void)value;auto bad=good;bad.erase(key);CHECK(!SoundRecovery_Decode(bad,SoundRecoveryUse::Restore,sentinel,e));CHECK(sentinel.Value()==prior);
        bad=good;bad.at(key)=42.5;CHECK(!SoundRecovery_Decode(bad,SoundRecoveryUse::Restore,sentinel,e));CHECK(sentinel.Value()==prior);
    }
    for(int i=0;i<23;++i) {
        auto bad=good;
        switch(i) {
        case 0:bad["unknown"]=true;break;case 1:bad["backend"]=std::string("other");break;
        case 2:bad["effect.kind"]=std::string("reverb");break;case 3:bad["version"]=2.0;break;
        case 4:bad["form"]=std::string("pending");break;case 5:bad["output.mode"]=std::string("any");break;
        case 6:bad["requested.speakers"]=3.0;break;case 7:bad["requested.emitterLimit"]=49.0;break;
        case 8:bad["requested.emitterLimit"]=0.0;break;case 9:bad["dependency.hrtf"]=3.0;break;
        case 10:bad["provider.vendor"]=std::string(1024,'x');break;case 11:bad["device.actual"]=std::string("");break;
        case 12:bad["device.actual"]=std::string("bad\0name",8);break;case 13:bad["device.actual"]=std::string("\xc0\xaf");break;
        case 14:bad["device.actual"]=std::string("\xed\xa0\x80");break;case 15:bad["device.actual"]=std::string("\xf4\x90\x80\x80");break;
        case 16:bad["hrtf.mode"]=std::string("request-auto");break;case 17:bad["hrtf.specifier"]=std::string("Unused");break;
        case 18:bad["hrtf.reason"]=-1.0;break;case 19:bad["hrtf.reason"]=65536.0;break;
        case 20:bad["hrtf.reason"]=std::bit_cast<double>(UINT64_C(1));break;
        case 21:bad["dependency.efxDebug"]=2147483648.0;break;case 22:bad["dependency.efxDebug"]=-2147483649.0;break;
        }
        CHECK(!SoundRecovery_Decode(bad,SoundRecoveryUse::Restore,sentinel,e));CHECK(sentinel.Value()==prior);
    }
    for(std::uint64_t bits:{UINT64_C(0x7ff0000000000000),UINT64_C(0xfff0000000000000),UINT64_C(0x7ff8000000000001),UINT64_C(1),UINT64_C(0x8000000000000001)}) {
        auto bad=good;bad["hrtf.reason"]=std::bit_cast<double>(bits);CHECK(!SoundRecovery_Decode(bad,SoundRecoveryUse::Restore,sentinel,e));
    }
#if defined(__SSE__) || defined(_M_X64)
    const auto csr=_mm_getcsr();_mm_setcsr(csr|0x8040);
    auto bad=good;bad["hrtf.reason"]=std::bit_cast<double>(UINT64_C(1));CHECK(!SoundRecovery_Decode(bad,SoundRecoveryUse::Restore,sentinel,e));_mm_setcsr(csr);
#endif
    auto zero=good;zero["hrtf.reason"]=-0.0;CHECK(SoundRecovery_Decode(zero,SoundRecoveryUse::Restore,decoded,e));
    CHECK(decoded.Value()->hrtfReason==0);
    for(const std::string& value:{std::string("\xc2\x80"),std::string("\xe0\xa0\x80"),std::string("\xf4\x8f\xbf\xbf"),std::string(1023,'x')}) {
        d=Data();d.actualDevice=value;r=Record(d);CHECK(SoundRecovery_Encode(r,f,e));CHECK(SoundRecovery_Decode(f,SoundRecoveryUse::Restore,decoded,e));
    }
}
static void TargetsAndResolution() {
    std::string e(0,'\0');auto d=Data();SoundRecoveryRecord target,actual;SoundRecoveryFields f;
    // Unchanged requested surround retains the actual stereo fallback exactly.
    d.requested={6,true,48};d.mode=SoundRecoveryMode::StereoUhj;auto r=Record(d);
    CHECK(SoundRecovery_BuildTarget(r,{6,true,12},target,e));CHECK(target.Value()->mode==d.mode&&target.Value()->hrtf==d.hrtf);
    CHECK(SoundRecovery_Encode(target,f,e));CHECK(f.size()==18&&!f.count("hrtf.reason"));
    auto extra=f;extra.emplace("unknown",true);const auto* unchanged=target.Value();
    CHECK(!SoundRecovery_Decode(extra,SoundRecoveryUse::PendingTarget,target,e));CHECK(target.Value()==unchanged);
    actual=Record(d);const auto* prior=actual.Value();CHECK(!SoundRecovery_Decode(f,SoundRecoveryUse::Restore,actual,e));CHECK(actual.Value()==prior);
    CHECK(!SoundRecovery_Decode(f,SoundRecoveryUse::ConfirmedTarget,actual,e));CHECK(actual.Value()==prior);
    CHECK(SoundRecovery_Decode(f,SoundRecoveryUse::PendingTarget,actual,e));
    d.requested.maxEmitterChannels=12;actual=Record(d);CHECK(SoundRecovery_ValidateRealized(target,actual,e));
    d.defaultDevice="New system default";actual=Record(d);CHECK(SoundRecovery_ValidateRealized(target,actual,e));
    d.actualDevice="Wrong actual route";actual=Record(d);CHECK(!SoundRecovery_ValidateRealized(target,actual,e));d.actualDevice="Logical device";
    d.mode=SoundRecoveryMode::StereoBasic;actual=Record(d);CHECK(!SoundRecovery_ValidateRealized(target,actual,e));
    for(int h=0;h<=2;++h) {
        d=Data();d.requested.speakers=6;d.mode=SoundRecoveryMode::Surround51;d.hrtfPolicy=h;r=Record(d);
        CHECK(SoundRecovery_BuildTarget(r,{2,false,8},target,e));
        for(int mode=0;mode<9;++mode)for(bool on:{false,true}) {
            auto a=d;a.requested={2,false,8};a.mode=SoundRecoveryMode(mode);a.hrtf=on?SoundRecoveryHrtf::ExactOn:SoundRecoveryHrtf::ExactOff;
            a.hrtfSpecifier=on?"Default":"";SoundRecoveryRecord observed;
            if(!SoundRecovery_BuildObserved(a,observed,e))continue;
            bool stereo=mode>=int(SoundRecoveryMode::Stereo)&&mode<=int(SoundRecoveryMode::StereoHrtf);
            const bool expected=h==2?(a.mode==SoundRecoveryMode::StereoHrtf&&on):stereo&&(h!=1||!on);
            CHECK(SoundRecovery_ValidateRealized(target,observed,e)==expected);
        }
    }
    d=Data();r=Record(d);prior=target.Value();CHECK(!SoundRecovery_BuildTarget(r,{2,true,48},target,e));CHECK(target.Value()==prior);
    d.hrtfPolicy=2;d.hrtf=SoundRecoveryHrtf::ExactOn;d.hrtfSpecifier="Specific";r=Record(d);
    CHECK(!SoundRecovery_BuildTarget(r,{6,false,48},target,e));
    CHECK(SoundRecovery_BuildTarget(r,{2,false,47},target,e));
    d.requested.maxEmitterChannels=47;d.hrtfReason=3;actual=Record(d);CHECK(SoundRecovery_ValidateRealized(target,actual,e));
    d.hrtfSpecifier="Other";actual=Record(d);CHECK(!SoundRecovery_ValidateRealized(target,actual,e));
    d=Data();d.hrtf=SoundRecoveryHrtf::ExactOn;d.hrtfSpecifier="Saved";r=Record(d);
    auto enumeration=Enumeration(d);SoundRecoveryResolved resolved;CHECK(SoundRecovery_Resolve(r,enumeration,resolved,e));CHECK(resolved.Value()->hrtfIndex==1);
    const auto* old=resolved.Value();enumeration.hrtfs={"Saved","Another"};CHECK(SoundRecovery_Resolve(r,enumeration,resolved,e));CHECK(resolved.Value()->hrtfIndex==0);
    old=resolved.Value();
    for(int i=0;i<11;++i) {
        auto invalid=Enumeration(d);
        switch(i){case 0:invalid.devices.push_back(d.actualDevice);break;case 1:invalid.devices.clear();break;case 2:invalid.devices[1]="Missing";break;
        case 3:invalid.hrtfs.push_back("Saved");break;case 4:invalid.hrtfs.clear();break;case 5:invalid.hrtfDevice="Other";break;
        case 6:invalid.provider.version="Other";break;case 7:invalid.devices.resize(257,"Other");break;case 8:invalid.hrtfs.resize(257,"Other");break;
        case 9:invalid.devices[0]=std::string("\x80");break;case 10:invalid.devices.assign(100,std::string(1023,'x'));break;}
        CHECK(!SoundRecovery_Resolve(r,invalid,resolved,e));CHECK(resolved.Value()==old);
    }
    enumeration.devices.clear();CHECK(resolved.Value()->plan.actualDevice==d.actualDevice&&resolved.Value()->hrtfIndex==0);
}
static void Capture() {
    std::string e(0,'\0');SoundSettingsObservation b,o;SoundRecoveryRecord result;SoundRecoveryFields fields;
    for(bool mute:{false,true}) {
        RecoveryReset();soundSystemLocal.muted=mute;auto lease=Begin({6,false,8},b);const int writes=Fake::writes;
        CHECK(SoundSettings_CaptureRecovery(lease,result,error,sizeof(error)));CHECK(Fake::writes==writes&&state.lease.token==lease.token);
        CHECK(result.Value()->actualDevice=="Physical"&&result.Value()->provider.renderer=="OpenAL Soft");
        CHECK(SoundRecovery_Encode(result,fields,e));CHECK(!fields.count("muted")&&soundSystemLocal.muted==mute);
        Patch({6,false,8});CHECK(SoundSettings_TryApply(lease,o,error,sizeof(error)));const auto* prior=result.Value();
        CHECK(!SoundSettings_CaptureRecovery(lease,result,error,sizeof(error)));CHECK(result.Value()==prior);
        soundSystemLocal.Render();CHECK(SoundSettings_CaptureRecovery(lease,result,error,sizeof(error)));CHECK(result.Value()->mode==SoundRecoveryMode::Surround51);
        CHECK(SoundSettings_BlockAutomaticRestart());Patch(b.requested);CHECK(SoundSettings_TryRestore(lease,o,error,sizeof(error)));soundSystemLocal.Render();
        CHECK(SoundSettings_CaptureRecovery(lease,result,error,sizeof(error)));CHECK(result.Value()->mode==SoundRecoveryMode::Stereo);
    }
    RecoveryReset();s_useEAXReverb.value=1;s_numberOfSpeakers.value=6;s_openALHRTF.value=2;Fake::mode=ALC_STEREO_HRTF_SOFT;Fake::hrtf=1;Fake::status=ALC_HRTF_ENABLED_SOFT;
    auto lease=Begin({6,true,8},b);CHECK(SoundSettings_CaptureRecovery(lease,result,error,sizeof(error)));
    CHECK(result.Value()->requested.efx&&result.Value()->requested.speakers==6&&result.Value()->mode==SoundRecoveryMode::StereoHrtf&&result.Value()->hrtfSpecifier=="Default HRTF");
    RecoveryReset();ResetFixture();soundSystemLocal.hardware.efxFiltersAvailable=false;Fake::efxSupport=false;
    for(auto& v:soundSystemLocal.hardware.voices){v.openalDirectFilter=0;v.openalAuxFilter=0;}
    lease=Begin({2,false,8},b);CHECK(b.sourceCount==3);CHECK(SoundSettings_CaptureRecovery(lease,result,error,sizeof(error)));
    Patch({2,false,8});CHECK(SoundSettings_TryApply(lease,o,error,sizeof(error)));soundSystemLocal.Render();
    CHECK(SoundSettings_CaptureRecovery(lease,result,error,sizeof(error)));CHECK(state.lease.token==lease.token&&state.routing.sourceCount==3);
    for(int i=0;i<18;++i) {
        RecoveryReset();if(i==0){s_useEAXReverb.value=1;soundSystemLocal.hardware.efxEnabled=true;soundSystemLocal.hardware.auxEffectSlot=10;soundSystemLocal.hardware.auxReverbEffect=11;Fake::slots[10]=11;Fake::effects[11]=AL_EFFECT_REVERB;}
        if(i==1){soundSystemLocal.hardware.auxEffectSlot=10;Fake::slots[10]=0;}
        if(i==2){soundSystemLocal.hardware.auxReverbEffect=11;Fake::effects[11]=AL_EFFECT_REVERB;}
        if(i==3){ResetFixture();}
        if(i>=16){soundSystemLocal.hardware.voices.resize(1);auto& v=soundSystemLocal.hardware.voices[0];if(i==16)v.openalDirectFilter=700;else v.openalAuxFilter=701;}
        lease=Begin({2,i==0||i==4,8},b);const auto* prior=result.Value();
        switch(i){case 5:RecoveryFake::providerNull=true;break;case 6:RecoveryFake::names.assign("Physical\0Physical\0\0",19);break;
        case 7:RecoveryFake::enumeration=false;break;case 8:RecoveryFake::countOverride=257;break;case 9:RecoveryFake::countOverride=-2;break;
        case 10:RecoveryFake::vendor.assign(1024,'x');break;case 11:RecoveryFake::names.assign("Other\0\0",7);break;
        case 12:mainThread=false;break;case 13:++lease.owner;break;case 14:SoundSettings_Abandon(lease);break;case 15:SoundSettings_DeviceEvent();break;}
        const int writes=Fake::writes;CHECK(!SoundSettings_CaptureRecovery(lease,result,error,sizeof(error)));CHECK(result.Value()==prior&&Fake::writes==writes);
    }
    // Every boundary in an HRTF capture rejects event/context/owner drift, while
    // an altered caller-owned lease cannot alter the already copied authority.
    auto prepare=[&] {RecoveryReset();s_openALHRTF.value=2;Fake::mode=ALC_STEREO_HRTF_SOFT;Fake::hrtf=1;Fake::status=ALC_HRTF_ENABLED_SOFT;return Begin({2,false,8},b);};
    for(int i=0;i<6;++i) {
        lease=prepare();const auto* prior=result.Value();
        switch(i){case 0:RecoveryFake::hrtfs={"Other"};break;case 1:RecoveryFake::hrtfs.push_back("Default HRTF");break;
        case 2:RecoveryFake::specifier.clear();break;case 3:Fake::missingProc="alcGetStringiSOFT";break;
        case 4:RecoveryFake::hrtfs[1].assign(1024,'x');break;case 5:RecoveryFake::version=std::string("\xc0\xaf");break;}
        CHECK(!SoundSettings_CaptureRecovery(lease,result,error,sizeof(error)));CHECK(result.Value()==prior);
    }
    lease=prepare();const int start=Fake::calls;CHECK(SoundSettings_CaptureRecovery(lease,result,error,sizeof(error)));const int total=Fake::calls-start;
    for(int kind:{1,2,4,7,8,10})for(int offset=1;offset<=total;++offset) {
        lease=prepare();Fake::callerLease=&lease;Fake::callbackKind=kind;Fake::callbackAt=Fake::calls+offset;const auto* prior=result.Value();const int writes=Fake::writes;
        const bool ok=SoundSettings_CaptureRecovery(lease,result,error,sizeof(error));CHECK(Fake::callbackAt==0);
        CHECK(ok==(kind==10));if(!ok)CHECK(result.Value()==prior);CHECK(Fake::writes==writes);
    }
}
static const char* denialOperation="none";static long denialLimit=-1;
template<class F> static void Denials(const char* name,F operation) {
    denialOperation=name;
    bool succeeded=false;for(long limit=0;limit<1024;++limit) {denialLimit=limit;allocCountdown=limit;const bool ok=operation();allocCountdown=-1;if(ok){succeeded=true;break;}}
    denialOperation="none";
    CHECK(succeeded);
}
static void AllocationFailures() {
    auto d=Data();d.provider.vendor.assign(300,'v');d.actualDevice.assign(300,'d');auto record=Record(d);SoundRecoveryRecord out=Record(Data());
    SoundRecoveryFields fields;std::string e(0,'\0');CHECK(SoundRecovery_Encode(record,fields,e));auto enumeration=Enumeration(d);SoundRecoveryResolved resolved;
    auto recordCall=[&](auto operation){const auto* previous=out.Value();bool ok=operation();if(!ok)CHECK(out.Value()==previous);return ok;};
    Denials("observed",[&]{return recordCall([&]{return SoundRecovery_BuildObserved(d,out,e);});});
    Denials("target",[&]{return recordCall([&]{return SoundRecovery_BuildTarget(record,{6,false,12},out,e);});});
    Denials("decode",[&]{return recordCall([&]{return SoundRecovery_Decode(fields,SoundRecoveryUse::Restore,out,e);});});
    Denials("resolve",[&]{const auto* previous=resolved.Value();bool ok=SoundRecovery_Resolve(record,enumeration,resolved,e);if(!ok)CHECK(previous==resolved.Value());return ok;});
    SoundRecoveryFields encoded;encoded.emplace("sentinel",true);
    Denials("encode",[&]{bool ok=SoundRecovery_Encode(record,encoded,e);if(!ok)CHECK(encoded.size()==1&&encoded.begin()->first=="sentinel");return ok;});
    RecoveryReset();SoundSettingsObservation b;auto lease=Begin({2,false,12},b);const int writes=Fake::writes;
    Denials("capture",[&]{return recordCall([&]{return SoundSettings_CaptureRecovery(lease,out,error,sizeof(error));});});CHECK(Fake::writes==writes);
}
int main(){std::set_terminate([]{std::fprintf(stderr,"unexpected terminate check %d operation %s allocation %ld\n",checks,denialOperation,denialLimit);std::_Exit(90);});LegacyMain();PortableGrammar();TargetsAndResolution();Capture();AllocationFailures();std::printf("%d cumulative checks passed\n",checks);return 0;}
