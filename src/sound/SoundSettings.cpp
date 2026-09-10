// First-party openQ4 checked audio settings. GPL-3.0-or-later.
#include "SoundSettings.h"
#include "SoundRecovery.h"
#include <atomic>
#include <cstring>
#include <limits>

#if defined(USE_OPENAL) && defined(USE_SDL3) && !defined(ID_DEDICATED)
#include "snd_local.h"
#include <SDL3/SDL_init.h>
#endif

#if defined(USE_OPENAL) && defined(USE_SDL3) && !defined(ID_DEDICATED) && \
	defined(ALC_HRTF_SOFT) && defined(ALC_OUTPUT_MODE_SOFT) && defined(ALC_CONNECTED) && \
	defined(AL_EFFECT_EAXREVERB) && defined(AL_FILTER_LOWPASS) && defined(ALC_STEREO_HRTF_SOFT)

extern idCVar s_noSound, s_useOpenAL, s_deviceName, s_useEAXReverb;
extern idCVar s_openALHRTF, s_numberOfSpeakers, s_maxEmitterChannels, s_openALEfxDebugMode;

namespace {
bool Fail(char* out, int size, const char* text) noexcept {
	if (out && size>0) { int i=0; for (; i<size-1 && text[i]; ++i) out[i]=text[i]; out[i]=0; }
	return false;
}
bool Copy(char* out, const char* text) noexcept {
	if (!text) return false;
	std::size_t n=0; while (n<1024 && text[n]) ++n;
	if (n==1024) return false;
	std::memcpy(out,text,n+1); return true;
}
bool Same(const SoundSettingsPolicy& a,const SoundSettingsPolicy& b) noexcept {
	return a.speakers==b.speakers && a.efx==b.efx && a.maxEmitterChannels==b.maxEmitterChannels;
}
bool Valid(const SoundSettingsPolicy& p) noexcept {
	return (p.speakers==2 || p.speakers==6) && p.maxEmitterChannels>=1 && p.maxEmitterChannels<=48;
}
SoundSettingsPolicy Policy() noexcept {
	return {s_numberOfSpeakers.GetInteger(),s_useEAXReverb.GetBool(),s_maxEmitterChannels.GetInteger()};
}
bool Same(const SoundSettingsLease& a,const SoundSettingsLease& b) noexcept {
	return a.token && a.token==b.token && a.owner==b.owner && a.request==b.request;
}
std::atomic<std::uint64_t> eventSerial{0};
std::uint64_t tokenSerial=0, deviceLifetimeSerial=1, sourceSerial=0, generation=0, updateSerial=0;
struct State {
	SoundSettingsLease lease;
	SoundSettingsPolicy target;
	SoundSettingsPolicy operationPolicy;
	SoundSettingsObservation baseline, expected, routing;
	SoundSettingsPhase phase=SoundSettingsPhase::Captured;
	std::uint64_t events=0, attemptUpdate=0, updateGeneration=0;
	std::uint64_t operationToken=0;
	int debug=0;
	bool busy=false, updating=false, validUpdate=false;
	bool restoreOnly=false;
} state;
bool Advance(std::uint64_t& n) noexcept {
	if (n==(std::numeric_limits<std::uint64_t>::max)()) return false;
	++n; return true;
}
struct Entry {
	bool entered;
	Entry() : entered(SDL_IsMainThread() && !state.busy && !state.updating) { if (entered) state.busy=true; }
	~Entry() { if (entered) state.busy=false; }
};
bool DependenciesAt(std::uint64_t events) noexcept {
	return !s_noSound.GetBool() && s_useOpenAL.GetBool() && !soundSystemLocal.needsRestart &&
		s_openALHRTF.GetInteger()==state.baseline.requestedHrtf &&
		s_openALEfxDebugMode.GetInteger()==state.debug &&
		strcmp(s_deviceName.GetString(),state.baseline.requestedDevice)==0 &&
		deviceLifetimeSerial && deviceLifetimeSerial==state.baseline.deviceLifetime &&
		events!=(std::numeric_limits<std::uint64_t>::max)() && eventSerial.load(std::memory_order_acquire)==events;
}
bool Dependencies() noexcept { return DependenciesAt(state.events); }
bool Identity(const SoundSettingsObservation& a,const SoundSettingsObservation& b) noexcept {
	return a.device==b.device && a.context==b.context && a.deviceLifetime==b.deviceLifetime &&
		strcmp(a.actualDevice,b.actualDevice)==0 && strcmp(a.defaultDevice,b.defaultDevice)==0 && a.connected;
}
bool Actual(const SoundSettingsObservation& a,const SoundSettingsObservation& b) noexcept {
	return Identity(a,b) && a.outputMode==b.outputMode && a.hrtf==b.hrtf && a.hrtfStatus==b.hrtfStatus &&
		a.efx==b.efx && a.filters==b.filters && a.slot==b.slot && a.effect==b.effect && a.effectType==b.effectType &&
		a.effectsVerified && b.effectsVerified;
}
bool Owned() noexcept {
	return (state.busy || state.updating) && state.operationToken && state.lease.token==state.operationToken && state.phase!=SoundSettingsPhase::Invalidated &&
		Dependencies() && Same(Policy(),state.operationPolicy) && reinterpret_cast<std::uintptr_t>(soundSystemLocal.hardware.GetOpenALDevice())==state.baseline.device &&
		reinterpret_cast<std::uintptr_t>(soundSystemLocal.hardware.GetOpenALContext())==state.baseline.context;
}
template<class F> bool Native(F&& operation) {
	if (!SoundSettings_NativeOperationCurrent()) return false;
	const bool result=operation();
	return SoundSettings_NativeOperationCurrent() && result;
}
bool OutputMatches(int observed,int requested) noexcept {
	if (requested==ALC_STEREO_SOFT) return observed==ALC_STEREO_SOFT || observed==ALC_STEREO_BASIC_SOFT ||
		observed==ALC_STEREO_UHJ_SOFT || observed==ALC_STEREO_HRTF_SOFT;
	return observed==requested;
}
bool KnownOutput(int mode) noexcept {
	return OutputMatches(mode,ALC_STEREO_SOFT) || mode==ALC_MONO_SOFT || mode==ALC_QUAD_SOFT ||
		mode==ALC_SURROUND_5_1_SOFT || mode==ALC_SURROUND_6_1_SOFT || mode==ALC_SURROUND_7_1_SOFT;
}
}

// The only privileged backend access. All native calls are checked, with copied
// state returned only after the complete query. Slot/effect queries are real;
// source routes below deliberately use checked setter receipts instead.
struct SoundSettingsAccess {
	static bool PortableNone() noexcept {
		const auto& h=soundSystemLocal.hardware;
		if (h.efxEnabled || h.auxEffectSlot || h.auxReverbEffect || h.voices.Num()>96) return false;
		// Retained filters on an idle voice are resources too. BaselineSources
		// intentionally inventories active source receipts only.
		for (int i=0;i<h.voices.Num();++i)
			if (h.voices[i].openalDirectFilter || h.voices[i].openalAuxFilter) return false;
		return true;
	}
	static bool Read(SoundSettingsObservation& out, bool effectProof=true) {
		auto& h=soundSystemLocal.hardware;
		const auto startedLifetime=deviceLifetimeSerial, startedEvents=eventSerial.load(std::memory_order_acquire), startedToken=state.lease.token;
		auto* const d=h.openalDevice; auto* const context=h.openalContext;
		auto unchanged=[&] { return deviceLifetimeSerial==startedLifetime && h.openalDevice==d && h.openalContext==context &&
			state.lease.token==startedToken && eventSerial.load(std::memory_order_acquire)==startedEvents; };
		auto read=[&](auto&& f) {
			if (!unchanged()) return false;
			auto* current=alcGetCurrentContext();
			if (!unchanged() || current!=context) return false;
			const bool result=f();
			if (!unchanged()) return false;
			current=alcGetCurrentContext(); return unchanged() && current==context && result;
		};
		if (!d || !context || h.initFailed || h.deferredUpdatesActive) return false;
#define SOUND_READ_TEST(expr) do { if (!read([&] { return bool(expr); })) return false; } while (false)
#define SOUND_READ_CALL(expr) SOUND_READ_TEST(((expr),true))
		SOUND_READ_TEST(alcGetCurrentContext()==context);
		SOUND_READ_TEST(alcGetContextsDevice(context)==d);
#if defined(ALC_HRTF_SOFT) && defined(ALC_OUTPUT_MODE_SOFT) && defined(ALC_CONNECTED)
		SOUND_READ_TEST(alcGetError(d)==ALC_NO_ERROR);
		SOUND_READ_TEST(alGetError()==AL_NO_ERROR);
		SOUND_READ_TEST(alcIsExtensionPresent(d,"ALC_SOFT_HRTF"));
		SOUND_READ_TEST(alcIsExtensionPresent(d,"ALC_SOFT_output_mode"));
		SOUND_READ_TEST(alcIsExtensionPresent(d,"ALC_EXT_disconnect"));
		SoundSettingsObservation c;
		c.requested=Policy(); c.requestedHrtf=s_openALHRTF.GetInteger(); c.deviceLifetime=deviceLifetimeSerial;
		c.device=reinterpret_cast<std::uintptr_t>(d); c.context=reinterpret_cast<std::uintptr_t>(h.openalContext);
		c.generation=generation; c.normalUpdate=updateSerial; c.muted=soundSystemLocal.IsMuted();
		bool all=false; SOUND_READ_CALL(all=alcIsExtensionPresent(d,"ALC_ENUMERATE_ALL_EXT")!=ALC_FALSE);
		if (!Copy(c.requestedDevice,s_deviceName.GetString())) return false;
		const char* name=nullptr;
		SOUND_READ_CALL(name=alcGetString(d,all?ALC_ALL_DEVICES_SPECIFIER:ALC_DEVICE_SPECIFIER)); if (!Copy(c.actualDevice,name)) return false;
		SOUND_READ_CALL(name=alcGetString(nullptr,all?ALC_DEFAULT_ALL_DEVICES_SPECIFIER:ALC_DEFAULT_DEVICE_SPECIFIER)); if (!Copy(c.defaultDevice,name)) return false;
		ALCint connected=0, hrtf=0;
		SOUND_READ_CALL(alcGetIntegerv(d,ALC_CONNECTED,1,&connected));
		SOUND_READ_CALL(alcGetIntegerv(d,ALC_OUTPUT_MODE_SOFT,1,&c.outputMode));
		SOUND_READ_CALL(alcGetIntegerv(d,ALC_HRTF_SOFT,1,&hrtf));
		SOUND_READ_CALL(alcGetIntegerv(d,ALC_HRTF_STATUS_SOFT,1,&c.hrtfStatus));
		SOUND_READ_TEST(alcGetError(d)==ALC_NO_ERROR); SOUND_READ_TEST(alcGetError(nullptr)==ALC_NO_ERROR);
		if (connected!=ALC_TRUE || (hrtf!=ALC_TRUE && hrtf!=ALC_FALSE) || !KnownOutput(c.outputMode) ||
			c.hrtfStatus<ALC_HRTF_DISABLED_SOFT || c.hrtfStatus>ALC_HRTF_UNSUPPORTED_FORMAT_SOFT) return false;
		c.connected=true; c.hrtf=hrtf==ALC_TRUE;
		c.filters=h.efxFiltersAvailable; c.efx=h.efxEnabled; c.slot=h.auxEffectSlot; c.effect=h.auxReverbEffect;
		c.effectsVerified=effectProof; if (c.effect && !effectProof) c.effectType=-1;
		if (c.filters) { SOUND_READ_TEST(alcIsExtensionPresent(d,"ALC_EXT_EFX")); }
		if (c.slot && effectProof) {
			LPALGETAUXILIARYEFFECTSLOTI query=nullptr;
			SOUND_READ_CALL(query=reinterpret_cast<LPALGETAUXILIARYEFFECTSLOTI>(alGetProcAddress("alGetAuxiliaryEffectSloti")));
			if (!query) return false;
			ALint bound=-1; SOUND_READ_CALL(query(c.slot,AL_EFFECTSLOT_EFFECT,&bound));
			SOUND_READ_TEST(alGetError()==AL_NO_ERROR);
			if (bound!=static_cast<ALint>(c.efx?c.effect:0)) return false;
		} else if (!c.slot && c.efx && effectProof) return false;
		if (c.efx && (!c.filters || !c.effect)) return false;
		if (c.effect && effectProof) {
			LPALGETEFFECTI queryEffect=nullptr;
			SOUND_READ_CALL(queryEffect=reinterpret_cast<LPALGETEFFECTI>(alGetProcAddress("alGetEffecti")));
			if (!queryEffect) return false;
			SOUND_READ_CALL(queryEffect(c.effect,AL_EFFECT_TYPE,&c.effectType)); SOUND_READ_TEST(alGetError()==AL_NO_ERROR);
			if (c.effectType!=AL_EFFECT_EAXREVERB && c.effectType!=AL_EFFECT_REVERB) return false;
		}
		SOUND_READ_TEST(alcGetError(d)==ALC_NO_ERROR); SOUND_READ_TEST(alGetError()==AL_NO_ERROR);
		SOUND_READ_TEST(alcGetCurrentContext()==context);
		if (!Same(c.requested,Policy()) || c.requestedHrtf!=s_openALHRTF.GetInteger() || strcmp(c.requestedDevice,s_deviceName.GetString())) return false;
		out=c; return true;
#else
		(void)out; return false;
#endif
#undef SOUND_READ_TEST
#undef SOUND_READ_CALL
	}
	static bool Reset(const SoundSettingsObservation& desired, bool automaticStereo) {
#if defined(ALC_HRTF_SOFT) && defined(ALC_OUTPUT_MODE_SOFT)
		auto& h=soundSystemLocal.hardware;
		LPALCRESETDEVICESOFT reset=nullptr;
		if (!Native([&] {reset=reinterpret_cast<LPALCRESETDEVICESOFT>(alcGetProcAddress(h.openalDevice,"alcResetDeviceSOFT"));return reset!=nullptr;}) ||
			!Native([&] {return alcGetError(h.openalDevice)==ALC_NO_ERROR;})) return false;
		const ALCint attrs[]={ALC_HRTF_SOFT,automaticStereo?ALC_DONT_CARE_SOFT:(desired.hrtf?ALC_TRUE:ALC_FALSE),ALC_OUTPUT_MODE_SOFT,desired.outputMode,0};
		bool accepted=false;
		if (!Native([&] {accepted=reset(h.openalDevice,attrs)==ALC_TRUE;return true;})) return false;
		const bool noError=Native([&] {return alcGetError(h.openalDevice)==ALC_NO_ERROR;});
		return accepted && noError;
#else
		(void)desired; (void)automaticStereo; return false;
#endif
	}
	static bool Efx(bool enabled) {
		auto& h=soundSystemLocal.hardware;
		if (!Owned()) return false;
		if (!h.efxFiltersAvailable) return !enabled && !h.efxEnabled && !h.auxEffectSlot && !h.auxReverbEffect;
		LPALGENEFFECTS genEffect=nullptr; LPALDELETEEFFECTS delEffect=nullptr; LPALEFFECTI effecti=nullptr;
		LPALGENAUXILIARYEFFECTSLOTS genSlot=nullptr; LPALDELETEAUXILIARYEFFECTSLOTS delSlot=nullptr; LPALAUXILIARYEFFECTSLOTI sloti=nullptr;
#define SOUND_PROC(target,type,name) if (!Native([&] { target=reinterpret_cast<type>(alGetProcAddress(name)); return target!=nullptr; })) return false
		SOUND_PROC(genEffect,LPALGENEFFECTS,"alGenEffects"); SOUND_PROC(delEffect,LPALDELETEEFFECTS,"alDeleteEffects");
		SOUND_PROC(effecti,LPALEFFECTI,"alEffecti"); SOUND_PROC(genSlot,LPALGENAUXILIARYEFFECTSLOTS,"alGenAuxiliaryEffectSlots");
		SOUND_PROC(delSlot,LPALDELETEAUXILIARYEFFECTSLOTS,"alDeleteAuxiliaryEffectSlots"); SOUND_PROC(sloti,LPALAUXILIARYEFFECTSLOTI,"alAuxiliaryEffectSloti");
#undef SOUND_PROC
		if (!Native([] {return alGetError()==AL_NO_ERROR;})) return false;
		if (enabled && !h.auxEffectSlot && h.auxReverbEffect) return false;
		if (enabled && !h.auxEffectSlot) {
			// Local outputs cannot publish handles into a replacement hardware
			// lifetime during driver reentry. A surviving old context retains
			// ownership even if a hotplug event revoked request authority.
			const auto oldLifetime=deviceLifetimeSerial; auto* const oldContext=h.openalContext;
			ALuint effect=0;
			const bool effectCall=Native([&] {genEffect(1,&effect);return true;});
			if (deviceLifetimeSerial==oldLifetime && h.openalContext==oldContext) h.auxReverbEffect=effect;
			if (!effectCall || !Native([] {return alGetError()==AL_NO_ERROR;}) || !effect) return false;
			if (!Native([&] {effecti(effect,AL_EFFECT_TYPE,AL_EFFECT_EAXREVERB);return true;}) || !Native([] {return alGetError()==AL_NO_ERROR;})) return false;
			ALuint slot=0;
			const bool slotCall=Native([&] {genSlot(1,&slot);return true;});
			if (deviceLifetimeSerial==oldLifetime && h.openalContext==oldContext) h.auxEffectSlot=slot;
			if (!slotCall || !Native([] {return alGetError()==AL_NO_ERROR;}) || !slot) return false;
		}
		if (h.auxEffectSlot && (!Native([&] {sloti(h.auxEffectSlot,AL_EFFECTSLOT_EFFECT,enabled?h.auxReverbEffect:AL_EFFECT_NULL);return true;}) ||
			!Native([] {return alGetError()==AL_NO_ERROR;}))) return false;
		h.efxEnabled=enabled; return true;
	}
	static bool BaselineSources(SoundSettingsObservation& out) {
		auto& h=soundSystemLocal.hardware;
		if (h.voices.Num()>96) return false;
		const auto token=state.lease.token,events=eventSerial.load(std::memory_order_acquire);
		auto same=[&] {return deviceLifetimeSerial==out.deviceLifetime && reinterpret_cast<std::uintptr_t>(h.openalDevice)==out.device &&
			reinterpret_cast<std::uintptr_t>(h.openalContext)==out.context && state.lease.token==token && eventSerial.load(std::memory_order_acquire)==events;};
		auto read=[&](auto&& f) {
			if(!same())return false;
			auto* context=alcGetCurrentContext();
			if(!same() || reinterpret_cast<std::uintptr_t>(context)!=out.context)return false;
			const bool result=f();if(!same())return false;
			context=alcGetCurrentContext();return same() && reinterpret_cast<std::uintptr_t>(context)==out.context && result;
		};
		LPALISFILTER isFilter=nullptr;
		if (h.efxFiltersAvailable && !read([&] {isFilter=reinterpret_cast<LPALISFILTER>(alGetProcAddress("alIsFilter"));return isFilter!=nullptr;})) return false;
		for (int i=0;i<h.voices.Num();++i) {
			const auto& v=h.voices[i];if(!v.openalSource)continue;
			const SoundSettingsSourceReceipt item{v.soundSettingsSourceGeneration,v.openalSource,v.openalDirectFilter,v.openalAuxFilter,0};
			if (!h.efxFiltersAvailable && (item.directFilter || item.auxiliaryFilter)) return false;
			if (!item.lifetime || !read([&] {return alIsSource(item.source);})) return false;
			if (h.efxFiltersAvailable && (!item.directFilter || !item.auxiliaryFilter ||
				!read([&] {return isFilter(item.directFilter);}) || !read([&] {return isFilter(item.auxiliaryFilter); }))) return false;
			if (!read([] {return alGetError()==AL_NO_ERROR;})) return false;
			// Inventory only; routingGeneration remains zero until checked setters.
			out.sources[out.sourceCount++]=item;
		}
		return true;
	}
	static bool RestoreResourceOwnership(const SoundSettingsObservation& baseline) {
		auto& h=soundSystemLocal.hardware;
		if (!Owned()) return false;
		if (baseline.slot || baseline.effect) return h.auxEffectSlot==baseline.slot && h.auxReverbEffect==baseline.effect;
		if (h.efxEnabled) return false;
		LPALDELETEAUXILIARYEFFECTSLOTS delSlot=nullptr; LPALDELETEEFFECTS delEffect=nullptr;
		if (!Native([&] {delSlot=reinterpret_cast<LPALDELETEAUXILIARYEFFECTSLOTS>(alGetProcAddress("alDeleteAuxiliaryEffectSlots"));return delSlot || !h.auxEffectSlot;}) ||
			!Native([&] {delEffect=reinterpret_cast<LPALDELETEEFFECTS>(alGetProcAddress("alDeleteEffects"));return delEffect || !h.auxReverbEffect;}) ||
			!Native([] {return alGetError()==AL_NO_ERROR;})) return false;
		if (h.auxEffectSlot) {
			const ALuint slot=h.auxEffectSlot;
			if (!Native([&] {delSlot(1,&slot);return true;}) || !Native([] {return alGetError()==AL_NO_ERROR;})) return false;
			h.auxEffectSlot=0;
		}
		if (h.auxReverbEffect) {
			const ALuint effect=h.auxReverbEffect;
			if (!Native([&] {delEffect(1,&effect);return true;}) || !Native([] {return alGetError()==AL_NO_ERROR;})) return false;
			h.auxReverbEffect=0;
		}
		return true;
	}
	static bool Route(SoundSettingsObservation& out) {
		auto& h=soundSystemLocal.hardware;
		if (h.voices.Num()>96) return false;
		SoundSettingsObservation c=out; c.sourceCount=0;
		for (int i=0;i<h.voices.Num();++i) {
			auto& v=h.voices[i]; if (!v.openalSource) continue;
			SoundSettingsSourceReceipt receipt;
			if (!v.ApplyWetDryRoutingChecked(h.efxFiltersAvailable,h.efxEnabled,h.auxEffectSlot,receipt)) return false;
			c.sources[c.sourceCount++]=receipt;
		}
		c.routingGeneration=generation; c.routingUpdate=updateSerial; out=c; return true;
	}
	static void AdoptRequestedPolicy() noexcept {
		// These legacy monitoring fields are explicitly REQUEST caches. Actual
		// output remains separately queried, including an existing legacy fallback.
		soundSystemLocal.hardware.openedSpeakerCount=s_numberOfSpeakers.GetInteger();
		soundSystemLocal.hardware.openedHrtfMode=s_openALHRTF.GetInteger();
	}
};

namespace {
bool Inspect(SoundSettingsObservation& c, bool effectProof=true) {
	if (!Dependencies()) { state.phase=SoundSettingsPhase::Invalidated; return false; }
	if (!SoundSettingsAccess::Read(c,effectProof)) { state.phase=SoundSettingsPhase::Failed; return false; }
	if (!Identity(c,state.baseline)) { state.phase=SoundSettingsPhase::Invalidated; return false; }
	return true;
}
bool Attempt(SoundSettingsLease lease,bool restore,SoundSettingsObservation& out,char* error,int size) {
	Entry entry;
	if (!entry.entered || !Same(lease,state.lease)) return Fail(error,size,"Audio settings owner is not current.");
	if (!restore && state.restoreOnly) return Fail(error,size,"Audio settings recovery permits restoration only.");
	state.operationToken=lease.token;
	if (state.phase==SoundSettingsPhase::Invalidated) return Fail(error,size,"Audio device or dependency changed.");
	const auto& policy=restore?state.baseline.requested:state.target;
	if (!Same(Policy(),policy)) return Fail(error,size,"Audio settings values no longer match the owned patch.");
	state.operationPolicy=policy;
	SoundSettingsObservation current;
	if (!Inspect(current,state.phase!=SoundSettingsPhase::Failed)) return Fail(error,size,"Audio device or dependency is unavailable.");
	if (state.phase!=SoundSettingsPhase::Failed && !Actual(current,state.expected)) {
		state.phase=SoundSettingsPhase::Invalidated; return Fail(error,size,"Audio output changed outside the owned request.");
	}
	const bool automaticStereo=!restore && state.target.speakers==2 && state.target.speakers!=state.baseline.requested.speakers && state.baseline.requestedHrtf==0;
	auto desired=state.baseline;
	if (!restore) {
		if (state.target.speakers!=state.baseline.requested.speakers) {
			desired.outputMode=state.target.speakers==6?ALC_SURROUND_5_1_SOFT:ALC_STEREO_SOFT;
			desired.hrtf=false;
			if (state.baseline.requestedHrtf==2) { desired.outputMode=ALC_STEREO_HRTF_SOFT; desired.hrtf=true; }
		}
		if (state.target.efx!=state.baseline.requested.efx) desired.efx=state.target.efx;
	}
	if (!Advance(generation)) { state.phase=SoundSettingsPhase::Invalidated; return Fail(error,size,"Audio generation exhausted."); }
	state.phase=SoundSettingsPhase::Failed; state.validUpdate=false; state.attemptUpdate=updateSerial;
	if ((desired.outputMode!=current.outputMode || desired.hrtf!=current.hrtf) && !SoundSettingsAccess::Reset(desired,automaticStereo))
		return Fail(error,size,"Audio device reset failed; explicit restoration is required.");
	if (!SoundSettingsAccess::Efx(desired.efx)) return Fail(error,size,"Audio effect transition failed.");
	SoundSettingsObservation result;
	if (!Inspect(result,!(restore && !state.baseline.effect)) || !(restore?result.outputMode==desired.outputMode:OutputMatches(result.outputMode,desired.outputMode)) ||
		(!automaticStereo && result.hrtf!=desired.hrtf) || result.efx!=desired.efx ||
		!SoundSettingsAccess::Route(result) || !Dependencies()) return Fail(error,size,"Audio backend did not realize the requested state.");
	if (restore) {
		if (!SoundSettingsAccess::RestoreResourceOwnership(state.baseline)) return Fail(error,size,"Audio effect resources could not be restored.");
		SoundSettingsObservation verified;
		if (!Inspect(verified) || verified.outputMode!=desired.outputMode || verified.hrtf!=desired.hrtf || verified.efx!=desired.efx)
			return Fail(error,size,"Audio restored readback changed.");
		result.slot=verified.slot; result.effect=verified.effect; result.effectType=verified.effectType; result.effectsVerified=verified.effectsVerified;
	}
	// Reset may change the reported HRTF reason; bool + exact output policy is
	// the apply criterion. The observed reason is retained for later drift checks.
	if (!Owned()) return Fail(error,size,"Audio ownership changed before publication.");
	state.expected=result; state.routing=result;
	state.phase=restore?SoundSettingsPhase::Restored:SoundSettingsPhase::Applied;
	result.phase=state.phase; out=result; return true;
}
}

bool SoundSettings_Begin(std::uint64_t owner,std::uint64_t request,SoundSettingsPolicy target,
	SoundSettingsLease& lease,SoundSettingsObservation& baseline,char* error,int size) {
	Entry entry;
	if (!entry.entered || state.lease.token || !owner || !request || !Valid(target) || !Valid(Policy()) ||
		s_noSound.GetBool() || !s_useOpenAL.GetBool() || soundSystemLocal.needsRestart)
		return Fail(error,size,"Audio settings cannot acquire the current backend.");
	SoundSettingsObservation captured;
	const auto events=eventSerial.load(std::memory_order_acquire);
	if (!deviceLifetimeSerial || events==(std::numeric_limits<std::uint64_t>::max)() || !SoundSettingsAccess::Read(captured) || !SoundSettingsAccess::BaselineSources(captured) || events!=eventSerial.load(std::memory_order_acquire) ||
		captured.requestedHrtf<0 || captured.requestedHrtf>2 ||
		(target.speakers!=captured.requested.speakers && target.speakers==6 && captured.requestedHrtf==2) ||
		(target.efx!=captured.requested.efx && target.efx && !captured.filters) ||
		!Same(Policy(),captured.requested) || s_openALHRTF.GetInteger()!=captured.requestedHrtf ||
		strcmp(s_deviceName.GetString(),captured.requestedDevice) || !Advance(tokenSerial))
		return Fail(error,size,"Audio settings are unsupported or the baseline is unverifiable.");
	state.lease={owner,request,tokenSerial}; state.target=target; state.baseline=captured;
	state.expected=captured; state.routing={}; state.phase=SoundSettingsPhase::Captured;
	state.events=events; state.debug=s_openALEfxDebugMode.GetInteger(); state.validUpdate=false;
	state.restoreOnly=false;
	lease=state.lease; baseline=captured; return true;
}
bool SoundSettings_TryApply(SoundSettingsLease l,SoundSettingsObservation& o,char* e,int n) { return Attempt(l,false,o,e,n); }
bool SoundSettings_TryRestore(SoundSettingsLease l,SoundSettingsObservation& o,char* e,int n) { return Attempt(l,true,o,e,n); }
bool SoundSettings_RevalidateRestore(SoundSettingsLease lease,SoundSettingsObservation& out,char* error,int size) {
	Entry entry;
	if (!entry.entered || !Same(lease,state.lease)) return Fail(error,size,"Audio settings owner is not current.");
	const auto events=eventSerial.load(std::memory_order_acquire);
	if (events<=state.events || !DependenciesAt(events) || !Same(Policy(),state.baseline.requested))
		return Fail(error,size,"Audio restoration dependencies are not unchanged.");
	SoundSettingsObservation current;
	// A failed owned transition may have staged an unbound/unfinished effect.
	// Read actual output and exact device identity without adopting that effect
	// as a new baseline. TryRestore must still restore and prove the old state.
	if (!SoundSettingsAccess::Read(current,false) || !Identity(current,state.baseline) ||
		!Same(lease,state.lease) || !DependenciesAt(events) || !Same(Policy(),state.baseline.requested) ||
		!Advance(generation)) return Fail(error,size,"The original audio backend cannot be revalidated.");
	state.events=events; state.restoreOnly=true; state.phase=SoundSettingsPhase::Failed;
	state.expected={}; state.routing={}; state.validUpdate=false; state.operationToken=0;
	state.updateGeneration=0; state.attemptUpdate=updateSerial;
	current.phase=state.phase; current.generation=generation;
	out=current; return true;
}
bool SoundSettings_Query(SoundSettingsLease lease,SoundSettingsObservation& out,char* error,int size) {
	Entry entry;
	if (!entry.entered || !Same(lease,state.lease)) return Fail(error,size,"Audio settings owner is not current.");
	SoundSettingsObservation c;
	if (!SoundSettingsAccess::Read(c)) return Fail(error,size,"Audio backend readback is unavailable.");
	if (!Dependencies() || !Identity(c,state.baseline) ||
		(state.phase!=SoundSettingsPhase::Failed && !Actual(c,state.expected))) state.phase=SoundSettingsPhase::Invalidated;
	c.phase=state.phase; c.routingGeneration=state.routing.routingGeneration; c.routingUpdate=state.routing.routingUpdate;
	c.sourceCount=state.routing.sourceCount; std::memcpy(c.sources,state.routing.sources,sizeof(c.sources));
	out=c; return true;
}
namespace {
bool Completion(SoundSettingsLease lease,SoundSettingsObservation& out,char* error,int size) {
	if (!Same(lease,state.lease) ||
		(state.phase!=SoundSettingsPhase::Applied && state.phase!=SoundSettingsPhase::Restored) || !state.validUpdate ||
		state.routing.routingGeneration!=generation || state.routing.routingUpdate<=state.attemptUpdate)
		return Fail(error,size,"Audio settings still need a successful normal update.");
	const auto& policy=state.phase==SoundSettingsPhase::Applied?state.target:state.baseline.requested;
	SoundSettingsObservation c;
	if (!Same(Policy(),policy) || !Inspect(c) || !Actual(c,state.expected)) return Fail(error,size,"Audio state changed before completion.");
	SoundSettingsObservation inventory=c; inventory.sourceCount=0;
	if (!SoundSettingsAccess::BaselineSources(inventory) || inventory.sourceCount!=state.routing.sourceCount)
		return Fail(error,size,"Audio sources changed after the normal update.");
	for (unsigned i=0;i<inventory.sourceCount;++i) {
		const auto& live=inventory.sources[i]; const auto& routed=state.routing.sources[i];
		if (live.source!=routed.source || live.lifetime!=routed.lifetime || live.directFilter!=routed.directFilter)
			return Fail(error,size,"Audio source identity changed after the normal update.");
	}
	ALfloat listener=-1;
	if (!Native([&] {alGetListenerf(AL_GAIN,&listener);return true;}) || !Native([] {return alGetError()==AL_NO_ERROR;}) ||
		listener!=(soundSystemLocal.IsMuted()?0.0f:1.0f) || !Same(lease,state.lease) || !Dependencies() || !Same(Policy(),policy))
		return Fail(error,size,"Audio ownership or mute changed before completion.");
	c.muted=soundSystemLocal.IsMuted();
	c.phase=state.phase; c.routingGeneration=state.routing.routingGeneration; c.routingUpdate=state.routing.routingUpdate;
	c.sourceCount=state.routing.sourceCount; std::memcpy(c.sources,state.routing.sources,sizeof(c.sources));
	out=c; return true;
}
}
bool SoundSettings_CancelCaptured(SoundSettingsLease lease,SoundSettingsObservation& out,char* error,int size) {
	Entry entry;
	if (!entry.entered || !Same(lease,state.lease) || state.phase!=SoundSettingsPhase::Captured || state.restoreOnly)
		return Fail(error,size,"Audio preparation has already changed or lost its owner.");
	SoundSettingsObservation current;
	if (!Dependencies() || !Same(Policy(),state.baseline.requested) || !SoundSettingsAccess::Read(current) ||
		!Actual(current,state.baseline) || !Dependencies() || !Same(Policy(),state.baseline.requested) ||
		!Same(lease,state.lease) || state.phase!=SoundSettingsPhase::Captured)
		return Fail(error,size,"The untouched audio baseline can no longer be proved.");
	current.phase=SoundSettingsPhase::Captured;
	out=current; state.lease={}; state.operationToken=0; state.validUpdate=false; return true;
}
bool SoundSettings_CheckCompletion(SoundSettingsLease lease,SoundSettingsObservation& out,char* error,int size) {
	Entry entry;
	if (!entry.entered) return Fail(error,size,"Audio settings owner is not current.");
	return Completion(lease,out,error,size);
}
bool SoundSettings_CaptureRecovery(SoundSettingsLease lease,SoundRecoveryRecord& out,char* error,int size) {
	Entry entry;
	if (!entry.entered || !Same(lease,state.lease) ||
		(state.phase!=SoundSettingsPhase::Captured && state.phase!=SoundSettingsPhase::Applied && state.phase!=SoundSettingsPhase::Restored))
		return Fail(error,size,"Audio recovery capture has no current completed owner.");
	if (state.target.efx && state.target.efx!=state.baseline.requested.efx)
		return Fail(error,size,"Portable effect-enabled targets are not qualified.");
	const auto phase=state.phase;
	const auto serial=generation;
	state.operationToken=lease.token;
	state.operationPolicy=phase==SoundSettingsPhase::Applied?state.target:state.baseline.requested;
	const auto current=[&] {return Same(lease,state.lease) && state.phase==phase && generation==serial && Owned();};
	try {
		SoundSettingsObservation observed;
		if (phase==SoundSettingsPhase::Captured) {
			if (!current() || !SoundSettingsAccess::Read(observed) || !current() || !Actual(observed,state.baseline))
				return Fail(error,size,"The actual audio preparation baseline changed.");
		} else if (!Completion(lease,observed,error,size) || !current()) return false;
		const auto inventory=[&](SoundSettingsObservation& observation) {
			if (observation.efx || observation.effect || observation.slot || !observation.effectsVerified || !SoundSettingsAccess::PortableNone()) return false;
			observation.sourceCount=0;
			if (!SoundSettingsAccess::BaselineSources(observation) || !current()) return false;
			for (unsigned i=0;i<observation.sourceCount;++i)
				if (observation.sources[i].directFilter || observation.sources[i].auxiliaryFilter) return false;
			return SoundSettingsAccess::PortableNone();
		};
		if (!inventory(observed)) return Fail(error,size,"Portable effect and filter resource history is not qualified.");
		SoundRecoveryData data;data.requested=observed.requested;data.hrtfPolicy=observed.requestedHrtf;data.efxDebug=state.debug;
		data.requestedDevice=observed.requestedDevice;data.actualDevice=observed.actualDevice;data.defaultDevice=observed.defaultDevice;
		data.hrtf=observed.hrtf?SoundRecoveryHrtf::ExactOn:SoundRecoveryHrtf::ExactOff;data.hrtfReason=observed.hrtfStatus;
		switch(observed.outputMode) {
		case ALC_MONO_SOFT:data.mode=SoundRecoveryMode::Mono;break;case ALC_STEREO_SOFT:data.mode=SoundRecoveryMode::Stereo;break;
		case ALC_STEREO_BASIC_SOFT:data.mode=SoundRecoveryMode::StereoBasic;break;case ALC_STEREO_UHJ_SOFT:data.mode=SoundRecoveryMode::StereoUhj;break;
		case ALC_STEREO_HRTF_SOFT:data.mode=SoundRecoveryMode::StereoHrtf;break;case ALC_QUAD_SOFT:data.mode=SoundRecoveryMode::Quad;break;
		case ALC_SURROUND_5_1_SOFT:data.mode=SoundRecoveryMode::Surround51;break;case ALC_SURROUND_6_1_SOFT:data.mode=SoundRecoveryMode::Surround61;break;
		case ALC_SURROUND_7_1_SOFT:data.mode=SoundRecoveryMode::Surround71;break;
		default:return Fail(error,size,"Audio output has no exact portable meaning.");
		}
		auto* const device=reinterpret_cast<ALCdevice*>(observed.device);
		const auto call=[&](auto&& operation) {return current() && Native(operation) && current();};
		const auto string=[&](auto&& operation,std::string& result) {
			const char* value=nullptr;char copied[1024]{};
			if (!call([&] {value=operation();return value!=nullptr;}) || !Copy(copied,value) || !current()) return false;
			result=copied;return current();
		};
		if (!string([] {return reinterpret_cast<const char*>(alGetString(AL_VENDOR));},data.provider.vendor) ||
			!string([] {return reinterpret_cast<const char*>(alGetString(AL_RENDERER));},data.provider.renderer) ||
			!string([] {return reinterpret_cast<const char*>(alGetString(AL_VERSION));},data.provider.version))
			return Fail(error,size,"Audio provider identity is unavailable.");
		if (observed.hrtf && !string([&] {return alcGetString(device,ALC_HRTF_SPECIFIER_SOFT);},data.hrtfSpecifier))
			return Fail(error,size,"Active HRTF identity is unavailable.");
		SoundRecoveryEnumeration enumeration;enumeration.provider=data.provider;enumeration.hrtfDevice=data.actualDevice;
		// Allocate empty storage before copying strings: debug STL's noexcept
		// string move can allocate during vector growth and terminate on denial.
		enumeration.devices.reserve(SoundRecoveryMaxDevices);
		enumeration.hrtfs.reserve(SoundRecoveryMaxHrtfs);
		bool all=false,enumerationAvailable=false;
		if (!call([&] {all=alcIsExtensionPresent(nullptr,"ALC_ENUMERATE_ALL_EXT")!=ALC_FALSE;return true;}) ||
			(!all && !call([&] {enumerationAvailable=alcIsExtensionPresent(nullptr,"ALC_ENUMERATION_EXT")!=ALC_FALSE;return true;})) || (!all && !enumerationAvailable))
			return Fail(error,size,"Unique logical audio device enumeration is unavailable.");
		const char* names=nullptr;
		if (!call([&] {names=alcGetString(nullptr,all?ALC_ALL_DEVICES_SPECIFIER:ALC_DEVICE_SPECIFIER);return names!=nullptr;}))
			return Fail(error,size,"Audio device enumeration failed.");
		std::size_t used=0;
		while (used<SoundRecoveryMaxBytes && names[used]) {
			const std::size_t start=used;
			while (used<SoundRecoveryMaxBytes && used-start<=SoundRecoveryMaxString && names[used]) ++used;
			if (used==SoundRecoveryMaxBytes || used-start>SoundRecoveryMaxString || enumeration.devices.size()==SoundRecoveryMaxDevices)
				return Fail(error,size,"Audio device enumeration exceeds its bound.");
			enumeration.devices.emplace_back(names+start,used-start);++used;
		}
		if (used==SoundRecoveryMaxBytes || !current()) return Fail(error,size,"Audio device enumeration changed.");
		ALCint hrtfCount=0;
		if (!call([&] {alcGetIntegerv(device,ALC_NUM_HRTF_SPECIFIERS_SOFT,1,&hrtfCount);return true;}) ||
			hrtfCount<0 || hrtfCount>static_cast<int>(SoundRecoveryMaxHrtfs)) return Fail(error,size,"HRTF enumeration exceeds its bound.");
		LPALCGETSTRINGISOFT getStringi=nullptr;
		if (hrtfCount && !call([&] {getStringi=reinterpret_cast<LPALCGETSTRINGISOFT>(alcGetProcAddress(device,"alcGetStringiSOFT"));return getStringi!=nullptr;}))
			return Fail(error,size,"HRTF enumeration entry point is unavailable.");
		for (ALCint i=0;i<hrtfCount;++i) {
			std::string name(0,'\0');
			if (!string([&] {return getStringi(device,ALC_HRTF_SPECIFIER_SOFT,i);},name)) return Fail(error,size,"HRTF enumeration failed.");
			enumeration.hrtfs.push_back(name);
		}
		if (!call([&] {return alcGetError(device)==ALC_NO_ERROR;}) || !call([] {return alcGetError(nullptr)==ALC_NO_ERROR;}) ||
			!call([] {return alGetError()==AL_NO_ERROR;})) return Fail(error,size,"Audio identity query failed.");
		std::string diagnostic(0,'\0');SoundRecoveryRecord candidate;SoundRecoveryResolved resolved;
		if (!SoundRecovery_BuildObserved(data,candidate,diagnostic) || !SoundRecovery_Resolve(candidate,enumeration,resolved,diagnostic))
			return Fail(error,size,diagnostic.c_str());
		SoundSettingsObservation final;
		if (!current() || !SoundSettingsAccess::Read(final) || !current() || !Actual(final,observed) || !inventory(final))
			return Fail(error,size,"Audio state changed while capturing portable recovery.");
		std::string hrtf(0,'\0');
		if (observed.hrtf && (!string([&] {return alcGetString(device,ALC_HRTF_SPECIFIER_SOFT);},hrtf) || hrtf!=data.hrtfSpecifier))
			return Fail(error,size,"Active HRTF changed while capturing recovery.");
		if (!call([&] {return alcGetError(device)==ALC_NO_ERROR;}) || !current()) return Fail(error,size,"Audio capture lost its owner.");
		out=std::move(candidate);return true;
	} catch (...) { return Fail(error,size,"Portable audio capture allocation failed."); }
}
bool SoundSettings_Finish(SoundSettingsLease lease,SoundSettingsObservation& out,char* error,int size) {
	Entry entry;
	SoundSettingsObservation current;
	if (!entry.entered || !Completion(lease,current,error,size)) return false;
	SoundSettingsAccess::AdoptRequestedPolicy(); out=current; state.lease={}; state.validUpdate=false; return true;
}
void SoundSettings_Abandon(SoundSettingsLease lease) noexcept {
	if (SDL_IsMainThread() && !state.busy && !state.updating && Same(lease,state.lease)) { state.phase=SoundSettingsPhase::Failed; state.validUpdate=false; }
}
bool SoundSettings_BlockAutomaticRestart() noexcept { return state.lease.token!=0; }
bool SoundSettings_NativeOperationCurrent() noexcept {
	if (!SDL_IsMainThread() || !Owned()) return false;
	auto* context=alcGetCurrentContext();
	return Owned() && reinterpret_cast<std::uintptr_t>(context)==state.baseline.context;
}
void SoundSettings_DeviceDestroyed() noexcept {
	if (deviceLifetimeSerial && !Advance(deviceLifetimeSerial)) deviceLifetimeSerial=0;
	state.phase=SoundSettingsPhase::Invalidated; state.validUpdate=false; state.lease={};
}
void SoundSettings_DeviceInitialized() noexcept { if (deviceLifetimeSerial && !Advance(deviceLifetimeSerial)) deviceLifetimeSerial=0; }
void SoundSettings_DeviceEvent() noexcept {
	auto n=eventSerial.load(std::memory_order_relaxed);
	while (n!=(std::numeric_limits<std::uint64_t>::max)() &&
		!eventSerial.compare_exchange_weak(n,n+1,std::memory_order_release,std::memory_order_relaxed)) {}
}
std::uint64_t SoundSettings_SourceCreated() noexcept { return Advance(sourceSerial)?sourceSerial:0; }
bool SoundSettings_NormalUpdateBegin() noexcept {
	if (!state.lease.token) return false;
	if (state.busy || state.updating || !SDL_IsMainThread()) { state.phase=SoundSettingsPhase::Invalidated; return false; }
	state.updating=true; state.updateGeneration=generation; state.operationToken=state.lease.token; state.validUpdate=false;
	state.operationPolicy=state.phase==SoundSettingsPhase::Applied?state.target:state.baseline.requested;
	return true;
}
void SoundSettings_NormalUpdateEnd(bool completed) noexcept {
	if (!state.updating) return;
	struct UpdateExit { ~UpdateExit() { state.updating=false; } } exit;
	if (!completed) { state.phase=SoundSettingsPhase::Failed; return; }
	if (!state.lease.token || state.updateGeneration!=generation ||
		(state.phase!=SoundSettingsPhase::Applied && state.phase!=SoundSettingsPhase::Restored)) return;
	const auto& policy=state.phase==SoundSettingsPhase::Applied?state.target:state.baseline.requested;
	SoundSettingsObservation c;
	if (!Same(Policy(),policy) || !Inspect(c) || !Actual(c,state.expected) || !Advance(updateSerial) ||
		!SoundSettingsAccess::Route(c)) { if (state.phase!=SoundSettingsPhase::Invalidated) state.phase=SoundSettingsPhase::Failed; return; }
	ALfloat listener=-1;
	if (!Native([&] {alGetListenerf(AL_GAIN,&listener);return true;}) || !Native([] {return alGetError()==AL_NO_ERROR;}) || listener!=(soundSystemLocal.IsMuted()?0.0f:1.0f) || !Dependencies()) {
		state.phase=SoundSettingsPhase::Invalidated; return;
	}
	state.routing=c; state.validUpdate=true;
}

#else
namespace { bool Unsupported(char* e,int n) { if (e && n>0) e[0]=0; return false; } }
bool SoundSettings_Begin(std::uint64_t,std::uint64_t,SoundSettingsPolicy,SoundSettingsLease&,SoundSettingsObservation&,char* e,int n) { return Unsupported(e,n); }
bool SoundSettings_CancelCaptured(SoundSettingsLease,SoundSettingsObservation&,char* e,int n) { return Unsupported(e,n); }
bool SoundSettings_TryApply(SoundSettingsLease,SoundSettingsObservation&,char* e,int n) { return Unsupported(e,n); }
bool SoundSettings_TryRestore(SoundSettingsLease,SoundSettingsObservation&,char* e,int n) { return Unsupported(e,n); }
bool SoundSettings_RevalidateRestore(SoundSettingsLease,SoundSettingsObservation&,char* e,int n) { return Unsupported(e,n); }
bool SoundSettings_Query(SoundSettingsLease,SoundSettingsObservation&,char* e,int n) { return Unsupported(e,n); }
bool SoundSettings_CheckCompletion(SoundSettingsLease,SoundSettingsObservation&,char* e,int n) { return Unsupported(e,n); }
bool SoundSettings_CaptureRecovery(SoundSettingsLease,SoundRecoveryRecord&,char* e,int n) { return Unsupported(e,n); }
bool SoundSettings_Finish(SoundSettingsLease,SoundSettingsObservation&,char* e,int n) { return Unsupported(e,n); }
void SoundSettings_Abandon(SoundSettingsLease) noexcept {}
bool SoundSettings_BlockAutomaticRestart() noexcept { return false; }
bool SoundSettings_NativeOperationCurrent() noexcept { return false; }
void SoundSettings_DeviceDestroyed() noexcept {}
void SoundSettings_DeviceInitialized() noexcept {}
void SoundSettings_DeviceEvent() noexcept {}
std::uint64_t SoundSettings_SourceCreated() noexcept { return 0; }
bool SoundSettings_NormalUpdateBegin() noexcept { return false; }
void SoundSettings_NormalUpdateEnd(bool) noexcept {}
#endif
