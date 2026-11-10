// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#include "SettingsDisplayService.h"
#ifndef ID_DEDICATED
#include "../framework/SettingsPersistence.h"
#include "../sound/SoundRestart.h"
#include <cmath>
#include <limits>

using namespace openq4;
using namespace openq4::ui;
bool EngineSettingsDisplayHost::SupportsMultisampling() const {
	rendererDisplayState_t state{};
	if (!R_RendererModule_QueryDisplay(&state) || !state.rendererReady || !state.windowValid || !state.presentation.available)
		return false;
	// Vulkan multisamples the scene targets its device restart re-creates.
	return WindowMultisampling() || R_RendererModule_GetStatus().activeApi == RENDER_MODULE_API_VULKAN;
}
bool EngineSettingsDisplayHost::WindowMultisampling() const {
	const auto api = R_RendererModule_GetStatus().activeApi;
	return api == RENDER_MODULE_API_GL || api == RENDER_MODULE_API_GL_MODULE || api == RENDER_MODULE_API_GLES;
}
bool EngineSettingsDisplayHost::LightGridLoad(renderLightGridLoadReceipt_t& receipt, std::uint64_t& serial) const {
	std::uint64_t epoch = 0;
	return R_RendererModule_QueryLightGridLoad(receipt,serial,epoch);
}
bool EngineSettingsDisplayHost::ReadyForAutomatic() const {
	rendererDisplayState_t state{};
	return R_RendererModule_QueryDisplay(&state) && state.rendererReady && state.windowValid && state.presentation.available;
}
namespace {
// Renderer names compare as the renderer reads them: ASCII, case-insensitive.
bool SameRendererName(const char* a, const char* b) {
	for (; *a && *b; ++a, ++b) {
		const unsigned char x = static_cast<unsigned char>(*a), y = static_cast<unsigned char>(*b);
		if ((x >= 'A' && x <= 'Z' ? x + 32 : x) != (y >= 'A' && y <= 'Z' ? y + 32 : y)) return false;
	}
	return *a == *b;
}
}
bool EngineSettingsDisplayHost::SupportsRendererSelection() const {
	if (!ReadyForAutomatic()) return false;
	const auto api = R_RendererModule_GetStatus().activeApi;
	// Vulkan has one back end; r_renderer chooses among the OpenGL ones.
	if (api != RENDER_MODULE_API_GL && api != RENDER_MODULE_API_GL_MODULE) return false;
	renderRendererSelection_t selection{}; std::uint64_t serial = 0, epoch = 0;
	return R_RendererModule_QueryRendererSelection(selection,serial,epoch);
}
bool EngineSettingsDisplayHost::SupportsCombined(const StateValues& baseline, const StateValues& target) const {
	if (!ReadyForAutomatic()) return false;
	const auto before = baseline.find("r_renderer"), after = target.find("r_renderer");
	const bool renderer = before == baseline.end() || after == target.end() || !SettingsValueEqual(before->second,after->second);
	return !renderer || SupportsRendererSelection();
}
bool EngineSettingsDisplayHost::DeviceWork() const {
	return kind == Kind::Display || kind == Kind::Renderer || (kind == Kind::Combined && (effects.plan.domainMask & 19u) != 0);
}
bool EngineSettingsDisplayHost::DisplayOwned() const {
	return kind == Kind::Display || (kind == Kind::Combined && (effects.plan.domainMask & SystemSettingDisplayRestart) != 0);
}
bool EngineSettingsDisplayHost::RendererOwned() const {
	return kind == Kind::Renderer || (kind == Kind::Combined && effects.patch.contains("r_renderer"));
}
void EngineSettingsDisplayHost::RestartSound(bool restoring) {
	soundRequested = false;
	// The device reads the speaker layout and EFX when it opens. The emitter
	// limit is read by every sound update and needs no restart.
	if (kind != Kind::Combined || (!effects.patch.contains("s_numberOfSpeakers") && !effects.patch.contains("s_useEAXReverb"))) return;
	// A restart still pending from the other direction serves this one too.
	soundRequested = Sound_RequestOutputRestart() || Sound_OutputState() == SOUND_OUTPUT_RESTARTING;
	if (cvarSystem->GetCVarBool("ui_retainedTrace"))
		common->Printf("UI_SETTINGS_SOUND restore=%d requested=%d\n",restoring?1:0,soundRequested?1:0);
}
bool EngineSettingsDisplayHost::SoundReady(bool& ready, std::string& error) const {
	ready = true;
	if (!soundRequested) return true;
	switch (Sound_OutputState()) {
	case SOUND_OUTPUT_RESTARTING: ready = false; return true;
	case SOUND_OUTPUT_FAILED: error = "The sound device did not reopen under the new settings"; return false;
	default: return true;
	}
}
bool EngineSettingsDisplayHost::RendererSelection(renderRendererSelection_t& selection, std::uint64_t& serial) const {
	std::uint64_t epoch = 0;
	return R_RendererModule_QueryRendererSelection(selection,serial,epoch);
}
void EngineSettingsDisplayHost::CatalogStamp(SystemDisplayCatalogStamp& stamp) const {
	SystemDisplayCatalogStamp result;
#if defined(USE_SDL3)
	result.generation = Sys_DisplayTopologyGeneration();
#endif
	rendererDisplayState_t state{};
	if (R_RendererModule_QueryDisplay(&state) && state.windowValid) result.currentDisplay = state.window.displayId;
	// Labels as the language tables now read; a string they lack keeps English.
	const idLangDict* strings = common->GetLanguageDict();
	const auto localized = [&](const char* key, std::string& value) {
		const char* text = strings ? strings->GetString(key) : nullptr;
		if (text && *text && idStr::Cmp(text,key) != 0) value = text;
	};
	localized("#str_229914",result.text.automatic); localized("#str_229973",result.text.desktop);
	localized("#str_229974",result.text.custom); localized("#str_230083",result.text.size);
	localized("#str_230084",result.text.qualified); localized("#str_230085",result.text.rate);
	localized("#str_230086",result.text.display); localized("#str_230087",result.text.missing);
	stamp = std::move(result);
}
bool EngineSettingsDisplayHost::CaptureCatalogInput(const SystemDisplayCatalogStamp& stamp, SystemDisplayCatalogInput& input, std::string& error) const {
	SystemDisplayCatalogInput candidate;
	if (!CaptureDisplayTopology(candidate.topology,error)) return false;
	// Auto describes the window's display, else the primary.
	candidate.currentDisplay = stamp.currentDisplay ? stamp.currentDisplay : candidate.topology.primary;
	candidate.text = stamp.text;
	input = std::move(candidate); error.clear(); return true;
}
bool EngineSettingsDisplayHost::RendererAgrees(const std::string& request, std::uint64_t& serial, std::string& error) const {
	renderRendererSelection_t selection{}; std::uint64_t epoch = 0;
	if (!R_RendererModule_QueryRendererSelection(selection,serial,epoch)) {
		error = "The renderer has not reported its selection"; return false;
	}
	// The requested name, then either that back end or a reported fallback to
	// the automatic pick. "best" takes the automatic pick; an explicit name
	// never promotes the modern path.
	const bool best = SameRendererName(request.c_str(),"best");
	const bool fallback = selection.fallback == RENDER_SELECTION_UNAVAILABLE || selection.fallback == RENDER_SELECTION_LEGACY;
	if (request.empty() || !SameRendererName(selection.requested,request.c_str()) ||
		(selection.fallback != RENDER_SELECTION_REQUESTED && !fallback) ||
		((fallback || best) && selection.selected != selection.automatic) || (!best && selection.promotionActive)) {
		error = "The renderer's selection does not match the requested renderer"; return false;
	}
	return true;
}
bool EngineSettingsDisplayHost::RendererCurrent(bool restoring, std::string& error) const {
	std::uint64_t serial = 0;
	if (!RendererAgrees(RendererRequest(restoring),serial,error)) return false;
	if (serial != selectionSerial) { error = "The renderer selected again during the settings change"; return false; }
	return true;
}
std::string EngineSettingsDisplayHost::RendererRequest(bool restoring) const {
	// A renderer record names its request; a combined record keeps each
	// domain's settings under their own names.
	const auto& map = restoring ? effects.resourceRestore : effects.resourceTarget;
	const auto found = map.find(kind == Kind::Combined ? "r_renderer" : "request");
	return found != map.end() && std::holds_alternative<std::string>(found->second) ? std::get<std::string>(found->second) : std::string();
}
const StateValues& EngineSettingsDisplayHost::SavedDisplay(bool restoring) const {
	if (kind == Kind::Renderer || kind == Kind::Combined) return restoring ? effects.displayRestore : effects.displayTarget;
	return restoring ? journal.displayRestore : journal.displayTarget;
}
namespace {
bool Fail(std::string& error, const char* message) { error = message; return false; }
bool ObserveDevice(rendererDisplayState_t& state, SettingsDisplayObservation& output, std::string& error) {
	if (!R_RendererModule_QueryDisplay(&state)) return Fail(error,"Actual renderer display observation is unavailable");
	const auto& p = state.presentation;
	output = {state.moduleEpoch,p.generation,p.submittedSequence,p.presentedSequence,p.failureSequence,
		state.rendererReady && state.windowValid && p.available != 0,state.window.hidden,state.window.focused,state.window.minimized};
	return true;
}
void AddPlacement(StateValues& fields, const char* prefix, const sysWindowPlacementSnapshot_t& p) {
	const std::string key = prefix;
	fields[key+"x"] = double(p.x); fields[key+"y"] = double(p.y);
	fields[key+"width"] = double(p.width); fields[key+"height"] = double(p.height);
	fields[key+"normalX"] = double(p.normalX); fields[key+"normalY"] = double(p.normalY);
	fields[key+"normalWidth"] = double(p.normalWidth); fields[key+"normalHeight"] = double(p.normalHeight);
	fields[key+"normalValid"] = p.normalValid;
}
bool ReadPlacement(const StateValues& fields, const char* prefix, sysWindowPlacementSnapshot_t& output) {
	const std::string key = prefix; sysWindowPlacementSnapshot_t p;
	const auto integer = [&](const char* name,int& out) {
		const auto i=fields.find(key+name);
		if (i==fields.end() || !std::holds_alternative<double>(i->second)) return false;
		const double value=std::get<double>(i->second);
		if (!std::isfinite(value) || std::floor(value)!=value || value<(std::numeric_limits<int>::min)() || value>(std::numeric_limits<int>::max)()) return false;
		out=int(value); return true;
	};
	const auto valid=fields.find(key+"normalValid");
	if (valid==fields.end() || !std::holds_alternative<bool>(valid->second) ||
		!integer("x",p.x) || !integer("y",p.y) || !integer("width",p.width) || !integer("height",p.height) ||
		!integer("normalX",p.normalX) || !integer("normalY",p.normalY) || !integer("normalWidth",p.normalWidth) || !integer("normalHeight",p.normalHeight)) return false;
	p.normalValid=std::get<bool>(valid->second);
	if (p.width<320 || p.width>16384 || p.height<240 || p.height>16384 ||
		(p.normalValid && (p.normalWidth<=0 || p.normalWidth>16384 || p.normalHeight<=0 || p.normalHeight>16384))) return false;
	output=p; return true;
}
// Each participating domain's settings in both directions, under the names
// the catalog gives them: the portable policy a combined record carries.
void CombinedPolicy(SettingsEffectRecoveryJournal& record) {
	for (const auto& item:SystemSettingsHost::Catalog()) {
		StateValues* restore=nullptr; StateValues* target=nullptr;
		switch (item.effects) {
		case SystemSettingImageReload: restore=&record.imageRestore; target=&record.imageTarget; break;
		case SystemSettingAudioRestart: restore=&record.audioRestore; target=&record.audioTarget; break;
		case SystemSettingNextMap: restore=&record.deferredRestore; target=&record.deferredTarget; break;
		case SystemSettingRendererResources: restore=&record.resourceRestore; target=&record.resourceTarget; break;
		default: continue;
		}
		if (!(record.plan.domainMask & item.effects)) continue;
		restore->emplace(item.key,record.baseline.at(item.key)); target->emplace(item.key,record.target.at(item.key));
	}
}
bool CombinedPolicyAgrees(const SettingsEffectRecoveryJournal& record) {
	SettingsEffectRecoveryJournal expected;
	expected.plan=record.plan; expected.baseline=record.baseline; expected.target=record.target;
	CombinedPolicy(expected);
	return SettingsValuesEqual(expected.imageRestore,record.imageRestore) && SettingsValuesEqual(expected.imageTarget,record.imageTarget) &&
		SettingsValuesEqual(expected.audioRestore,record.audioRestore) && SettingsValuesEqual(expected.audioTarget,record.audioTarget) &&
		SettingsValuesEqual(expected.deferredRestore,record.deferredRestore) && SettingsValuesEqual(expected.deferredTarget,record.deferredTarget) &&
		SettingsValuesEqual(expected.resourceRestore,record.resourceRestore) && SettingsValuesEqual(expected.resourceTarget,record.resourceTarget);
}
bool RecoveryMetadata(const SettingsRecoveryJournal& journal,
	const sysWindowPlacementSnapshot_t& before,const sysWindowPlacementSnapshot_t& after,bool combined,std::string& error) {
	const auto dimensions=[](const sysWindowPlacementSnapshot_t& placement,const StateValues& values) {
		return double(placement.width)==std::get<double>(values.at("r_windowWidth")) &&
			double(placement.height)==std::get<double>(values.at("r_windowHeight"));
	};
	const unsigned effects=SystemSettingsHost::ChangedEffects(journal.baseline,journal.target);
	// A schema-1 record changes the display alone; a combined one changes it among other domains.
	if (!dimensions(before,journal.baseline) || !dimensions(after,journal.target) ||
		(combined ? (effects & SystemSettingDisplayRestart)==0 : effects!=SystemSettingDisplayRestart))
		return Fail(error,"Saved placement or effect batch contradicts the settings catalog");
	// Unused historical hardware need not still be connected, but both saved
	// records must be structurally valid and agree with the frozen target.
	return ValidateDisplayRecoveryPair(journal.displayRestore,journal.displayTarget,journal.target,error);
}
}
bool EngineSettingsDisplayHost::Paths(std::string& error) {
	return Common_SettingsPersistencePaths(journalPath,lockPath,error);
}
void EngineSettingsDisplayHost::Clear() {
	processLease.Release(); journal={}; effects={}; kind=Kind::Display; catalogStartup=false; selectionSerial=0;
	targetPlan={}; restorePlan={}; baselineDevice={}; currentDevice={};
	placement={}; expectedPlacement={}; committedPlacement={}; startupTarget.clear();
	placementToken=0; writtenBytes.clear(); attemptedBytes.clear(); recoveryError.clear();
	ownsJournal=placed=blocked=startup=startupReady=startupConfirmed=false; startupDeadline=0; startupLastTime=-1;
	soundRequested=false;
}
bool EngineSettingsDisplayHost::VerifyJournal(bool allowMissing, std::string& error) {
	if (!processLease.IsHeld()) return Fail(error,"Settings recovery lease is unavailable");
	std::string bytes;
	const auto read=DurableReadExact(journalPath,SettingsJournalMaxBytes,bytes,error);
	if (read==DurableReadResult::Missing) return allowMissing || Fail(error,"The settings recovery journal disappeared");
	if (read!=DurableReadResult::Present) return false;
	if (!ownsJournal || (bytes!=writtenBytes && bytes!=attemptedBytes)) return Fail(error,"The settings recovery journal changed outside this attempt");
	return true;
}
bool EngineSettingsDisplayHost::WriteJournal(std::string& error) {
	if (!VerifyJournal(!ownsJournal,error)) return false;
	if (!(kind!=Kind::Display ? EncodeSettingsEffectJournal(effects,SystemSettingsHost::Schema(),attemptedBytes,error) :
		EncodeSettingsJournal(journal,SystemSettingsHost::Schema(),attemptedBytes,error))) return false;
	ownsJournal=true; // Publication can succeed even if a later durability barrier fails.
	if (!DurableReplaceExact(journalPath,attemptedBytes,error)) return false;
	writtenBytes=attemptedBytes;
	const auto state=kind!=Kind::Display?effects.state:journal.state;
	if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("UI_SETTINGS_JOURNAL state=%s durable=1 schema=%d\n",
		state==SettingsJournalState::Confirmed?"confirmed":"pending",kind!=Kind::Display?2:1);
	return true;
}
bool EngineSettingsDisplayHost::ValidateLive(const StateValues& target, std::string& error) {
	StateValues current;
	return settings.Read(current,error) && (SettingsValuesEqual(current,target) || Fail(error,"Settings changed outside the frozen display operation"));
}
bool EngineSettingsDisplayHost::CommitConfiguration(std::string& error) {
	std::string currentJournal,currentLock;
	if (!VerifyJournal(false,error) || !Common_SettingsPersistencePaths(currentJournal,currentLock,error)) return false;
	if (currentJournal!=journalPath || currentLock!=lockPath) return Fail(error,"Settings save root changed during the recovery lease");
	return Common_WriteSettingsConfiguration(true,error);
}
bool EngineSettingsDisplayHost::NewAttemptIdentity(std::string& attempt, std::string& error) {
	unsigned char random[16];
	if (!Sys_GetSecureRandomBytes(random,sizeof(random))) return Fail(error,"Cannot create a persistent settings attempt identity");
	attempt.clear();
	for (unsigned char c:random) { attempt += "0123456789abcdef"[c>>4]; attempt += "0123456789abcdef"[c&15]; }
	return true;
}
bool EngineSettingsDisplayHost::Prepare(const SettingsAttempt& attempt, std::string& error) {
	if (RecoveryActive() || placementToken) return Fail(error,"A previous settings recovery has not completed");
	// The attempt's class decides its executor; its completion must agree
	// before any lease or journal exists.
	const auto applyClass=SystemSettingsHost::ApplyClassOf(attempt.baseline,attempt.target);
	const bool deferred=applyClass==SystemApplyClass::Deferred, renderer=applyClass==SystemApplyClass::Renderer;
	const bool combined=applyClass==SystemApplyClass::Combined;
	if (applyClass!=SystemApplyClass::Display && !deferred && !renderer && !combined) return Fail(error,"The settings attempt has no executor");
	// Only a change to the display itself asks the user to keep it.
	const bool automatic=deferred || renderer || (combined && !SystemSettingsHost::ChangedRequiresDisplayRestart(attempt.baseline,attempt.target));
	if (automatic ? attempt.completion!=SettingsCompletion::Automatic : attempt.completion!=SettingsCompletion::UserConfirmation)
		return Fail(error,"The settings attempt's effects and completion disagree");
	if (!Paths(error) || !processLease.TryAcquire(lockPath,error)) return false;
	std::string bytes;
	const auto read=DurableReadExact(journalPath,SettingsJournalMaxBytes,bytes,error);
	if (read!=DurableReadResult::Missing) { blocked=true; return read==DurableReadResult::Failed?false:Fail(error,"An existing settings recovery journal requires startup recovery"); }
	if (deferred) return PrepareDeferred(attempt,error);
	if (renderer) return PrepareRenderer(attempt,error);
	if (combined) return PrepareCombined(attempt,error);
	if (!ValidateLive(attempt.baseline,error) || !R_RendererModule_QueryDisplay(&baselineDevice))
		return Fail(error,error.empty()?"Actual baseline display is unavailable":error.c_str());
	SystemDisplayTopology topology;
	if (!CaptureDisplayTopology(topology,error) || !BuildDisplayRestore(baselineDevice,topology,restorePlan,error) ||
		!BuildDisplayRequest(attempt.target,baselineDevice,topology,targetPlan,error,WindowMultisampling())) return false;
	SettingsRecoveryJournal candidate; candidate.baseline=attempt.baseline; candidate.target=attempt.target; candidate.patch=attempt.patch;
	if (!NewAttemptIdentity(candidate.attempt,error)) return false;
	if (!CaptureDisplayRecovery(restorePlan,topology,candidate.displayRestore,error) ||
		!CaptureDisplayRecovery(targetPlan,topology,candidate.displayTarget,error)) return false;
	char diagnostic[512]{};
	if (!Sys_BeginWindowPlacementLease(attempt.request,&placement,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	placementToken=attempt.request; expectedPlacement=placement;
	if (double(placement.width)!=std::get<double>(attempt.baseline.at("r_windowWidth")) ||
		double(placement.height)!=std::get<double>(attempt.baseline.at("r_windowHeight"))) return Fail(error,"Window placement changed while preparing settings");
	AddPlacement(candidate.placement,"baseline.",placement);
	committedPlacement=placement;
	committedPlacement.width=int(std::get<double>(attempt.target.at("r_windowWidth")));
	committedPlacement.height=int(std::get<double>(attempt.target.at("r_windowHeight")));
	AddPlacement(candidate.placement,"target.",committedPlacement);
	journal=std::move(candidate);
	return WriteJournal(error);
}
bool EngineSettingsDisplayHost::PrepareDeferred(const SettingsAttempt& attempt, std::string& error) {
	SettingsDisplayObservation ready;
	if (!ValidateLive(attempt.baseline,error)) return false;
	if (!ObserveDevice(baselineDevice,ready,error) || !ready.ready) return Fail(error,error.empty()?"No presenting renderer can prove the settings change":error.c_str());
	SettingsEffectRecoveryJournal candidate;
	candidate.baseline=attempt.baseline; candidate.target=attempt.target; candidate.patch=attempt.patch;
	if (!BuildSettingsEffectPlan(attempt.baseline,attempt.target,SystemSettingsHost::Schema(),candidate.plan,error)) return false;
	if (candidate.plan.domainMask!=SystemSettingNextMap) return Fail(error,"A deferred attempt carries only the next-map domain");
	// The portable policy: what the next level load uses in each direction.
	candidate.deferredRestore.emplace("lightGridPreload",attempt.baseline.at("r_lightGridPreload"));
	candidate.deferredTarget.emplace("lightGridPreload",attempt.target.at("r_lightGridPreload"));
	if (!NewAttemptIdentity(candidate.attempt,error)) return false;
	kind=Kind::Deferred; effects=std::move(candidate); currentDevice=baselineDevice;
	return WriteJournal(error);
}
bool EngineSettingsDisplayHost::PrepareRenderer(const SettingsAttempt& attempt, std::string& error) {
	if (!SupportsRendererSelection()) return Fail(error,"The active renderer cannot report its selection");
	if (!ValidateLive(attempt.baseline,error) || !R_RendererModule_QueryDisplay(&baselineDevice))
		return Fail(error,error.empty()?"Actual baseline display is unavailable":error.c_str());
	// The renderer restarts on the display it has now, in both directions.
	SystemDisplayTopology topology;
	if (!CaptureDisplayTopology(topology,error) || !BuildDisplayRestore(baselineDevice,topology,restorePlan,error)) return false;
	targetPlan=restorePlan;
	SettingsEffectRecoveryJournal candidate;
	candidate.baseline=attempt.baseline; candidate.target=attempt.target; candidate.patch=attempt.patch;
	if (!BuildSettingsEffectPlan(attempt.baseline,attempt.target,SystemSettingsHost::Schema(),candidate.plan,error)) return false;
	if (candidate.plan.domainMask!=SystemSettingRendererResources) return Fail(error,"A renderer attempt carries only the renderer domain");
	if (!NewAttemptIdentity(candidate.attempt,error)) return false;
	if (!CaptureDisplayRecovery(restorePlan,topology,candidate.displayRestore,error) ||
		!CaptureDisplayRecovery(targetPlan,topology,candidate.displayTarget,error) ||
		!ValidateDisplayPreserveActualPair(candidate.displayRestore,candidate.displayTarget,attempt.baseline,attempt.target,error)) return false;
	candidate.resourceRestore.emplace("request",attempt.baseline.at("r_renderer"));
	candidate.resourceTarget.emplace("request",attempt.target.at("r_renderer"));
	char diagnostic[512]{};
	if (!Sys_BeginWindowPlacementLease(attempt.request,&placement,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	placementToken=attempt.request; expectedPlacement=placement;
	if (double(placement.width)!=std::get<double>(attempt.baseline.at("r_windowWidth")) ||
		double(placement.height)!=std::get<double>(attempt.baseline.at("r_windowHeight"))) return Fail(error,"Window placement changed while preparing settings");
	// No window geometry changes: both directions keep the leased placement.
	AddPlacement(candidate.placement,"baseline.",placement);
	AddPlacement(candidate.placement,"target.",placement);
	committedPlacement=placement;
	kind=Kind::Renderer; effects=std::move(candidate); currentDevice=baselineDevice;
	return WriteJournal(error);
}
bool EngineSettingsDisplayHost::PrepareCombined(const SettingsAttempt& attempt, std::string& error) {
	SettingsDisplayObservation ready;
	if (!ValidateLive(attempt.baseline,error)) return false;
	if (!ObserveDevice(baselineDevice,ready,error) || !ready.ready) return Fail(error,error.empty()?"No presenting renderer can prove the settings change":error.c_str());
	if (attempt.patch.contains("r_renderer") && !SupportsRendererSelection()) return Fail(error,"The active renderer cannot report its selection");
	SettingsEffectRecoveryJournal candidate;
	candidate.baseline=attempt.baseline; candidate.target=attempt.target; candidate.patch=attempt.patch;
	if (!BuildSettingsEffectPlan(attempt.baseline,attempt.target,SystemSettingsHost::Schema(),candidate.plan,error)) return false;
	CombinedPolicy(candidate);
	// One device restart covers the display, image and renderer resource
	// domains. Without a display change it runs on the display the renderer
	// has now, in both directions.
	const bool device=(candidate.plan.domainMask & 19u)!=0, display=(candidate.plan.domainMask & SystemSettingDisplayRestart)!=0;
	if (device) {
		SystemDisplayTopology topology;
		if (!CaptureDisplayTopology(topology,error) || !BuildDisplayRestore(baselineDevice,topology,restorePlan,error)) return false;
		if (!display) targetPlan=restorePlan;
		else if (!BuildDisplayRequest(attempt.target,baselineDevice,topology,targetPlan,error,WindowMultisampling())) return false;
		if (!CaptureDisplayRecovery(restorePlan,topology,candidate.displayRestore,error) ||
			!CaptureDisplayRecovery(targetPlan,topology,candidate.displayTarget,error)) return false;
		if (!display && !ValidateDisplayPreserveActualPair(candidate.displayRestore,candidate.displayTarget,attempt.baseline,attempt.target,error)) return false;
	}
	if (!NewAttemptIdentity(candidate.attempt,error)) return false;
	if (device) {
		char diagnostic[512]{};
		if (!Sys_BeginWindowPlacementLease(attempt.request,&placement,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
		placementToken=attempt.request; expectedPlacement=placement;
		if (double(placement.width)!=std::get<double>(attempt.baseline.at("r_windowWidth")) ||
			double(placement.height)!=std::get<double>(attempt.baseline.at("r_windowHeight"))) return Fail(error,"Window placement changed while preparing settings");
		AddPlacement(candidate.placement,"baseline.",placement);
		committedPlacement=placement;
		committedPlacement.width=int(std::get<double>(attempt.target.at("r_windowWidth")));
		committedPlacement.height=int(std::get<double>(attempt.target.at("r_windowHeight")));
		AddPlacement(candidate.placement,"target.",committedPlacement);
	}
	kind=Kind::Combined; effects=std::move(candidate); currentDevice=baselineDevice;
	return WriteJournal(error);
}
bool EngineSettingsDisplayHost::Place(const sysWindowPlacementSnapshot_t& finalState, std::string& error) {
	char diagnostic[512]{};
	if (!Sys_ApplyWindowPlacementLease(placementToken,&expectedPlacement,&finalState,diagnostic,sizeof(diagnostic))) {
		// A refusing setter may follow an earlier successful setter. Retain only
		// values this call could own; divergent external changes remain conflicts.
		sysWindowPlacementSnapshot_t observed; char readError[512]{};
		if (Sys_ReadWindowPlacementLease(placementToken,&observed,readError,sizeof(readError))) {
			const auto owned=[](auto value,auto before,auto target) { return value==before || value==target; };
			if (owned(observed.x,expectedPlacement.x,finalState.x) && owned(observed.y,expectedPlacement.y,finalState.y) &&
				owned(observed.width,expectedPlacement.width,finalState.width) && owned(observed.height,expectedPlacement.height,finalState.height) &&
				observed.normalX==expectedPlacement.normalX && observed.normalY==expectedPlacement.normalY &&
				observed.normalWidth==expectedPlacement.normalWidth && observed.normalHeight==expectedPlacement.normalHeight &&
				observed.normalValid==expectedPlacement.normalValid) expectedPlacement=observed;
		}
		return Fail(error,diagnostic);
	}
	expectedPlacement=finalState; placed=true; return true;
}
bool EngineSettingsDisplayHost::ReleasePlacement(std::string& error) {
	if (!placementToken) return true;
	char diagnostic[512]{};
	if (!Sys_FinishWindowPlacementLease(placementToken,&expectedPlacement,&expectedPlacement,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	placementToken=0; return true;
}
bool EngineSettingsDisplayHost::FinishJournal(std::string& error) {
	if (ownsJournal && (!VerifyJournal(true,error) || !DurableRemoveExact(journalPath,error))) return false;
	// No event pump or host callback is allowed between the conflict-checked
	// placement write, journal removal and releasing the native geometry lease.
	if (!ReleasePlacement(error)) return false;
	Clear(); return true;
}
bool EngineSettingsDisplayHost::CancelPreparation(std::string& error) {
	if (placementToken && !Place(placement,error)) return false;
	if (!processLease.IsHeld()) { Clear(); return true; }
	if (blocked && !ownsJournal) {
		if (!ReleasePlacement(error)) return false;
		processLease.Release(); return true; // Foreign evidence remains authoritative.
	}
	return FinishJournal(error);
}
bool EngineSettingsDisplayHost::Restart(bool restoring, SettingsDisplayObservation& output, std::string& error) {
	if (!VerifyJournal(false,error)) return false;
	if (!DeviceWork()) {
		// The next level load reads the committed policy; nothing restarts. The
		// controller still waits for a later presented frame on this device. A
		// combined attempt without renderer work may still reopen the sound device.
		rendererDisplayState_t observed{}; SettingsDisplayObservation candidate;
		if (!ObserveDevice(observed,candidate,error) || !candidate.ready || candidate.epoch!=baselineDevice.moduleEpoch)
			return Fail(error,error.empty()?"The renderer stopped presenting during the settings change":error.c_str());
		RestartSound(restoring);
		currentDevice=observed; output=candidate;
		if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("UI_SETTINGS_DEFERRED restore=%d epoch=%llu generation=%llu submitted=%llu presented=%llu\n",
			restoring?1:0,static_cast<unsigned long long>(candidate.epoch),static_cast<unsigned long long>(candidate.generation),
			static_cast<unsigned long long>(candidate.submitted),static_cast<unsigned long long>(candidate.presented));
		return true;
	}
	SystemDisplayTopology topology; SystemDisplayPlan plan;
	if (!CaptureDisplayTopology(topology,error) || !ResolveDisplayRecovery(SavedDisplay(restoring),topology,plan,error)) return false;
	// Catalog writes have completed. Only their window dimensions may change;
	// x/y and the normal-placement cache remain under the full-duration lease.
	expectedPlacement.width=cvarSystem->GetCVarInteger("r_windowWidth");
	expectedPlacement.height=cvarSystem->GetCVarInteger("r_windowHeight");
	char diagnostic[512]{}; sysWindowPlacementSnapshot_t unchanged;
	if (!Sys_ReadWindowPlacementLease(placementToken,&unchanged,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	if (!Sys_ApplyWindowPlacementLease(placementToken,&expectedPlacement,&expectedPlacement,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	rendererDisplayState_t before{};
	if (!R_RendererModule_QueryDisplay(&before) || before.moduleEpoch!=baselineDevice.moduleEpoch) return Fail(error,"The renderer module changed during display recovery");
	std::uint64_t selectionBefore=0;
	if (RendererOwned()) {
		renderRendererSelection_t ignored{}; std::uint64_t epoch=0;
		if (!R_RendererModule_QueryRendererSelection(ignored,selectionBefore,epoch)) selectionBefore=0;
	}
	if (!R_RendererModule_TryDeviceRestart(&plan.request,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	rendererDisplayState_t observed{}; SettingsDisplayObservation candidate;
	if (!ObserveDevice(observed,candidate,error) || candidate.epoch!=baselineDevice.moduleEpoch ||
		candidate.generation<=before.presentation.generation || !MatchesDisplay(plan,observed,error))
		return Fail(error,error.empty()?"The renderer did not create the requested new device":error.c_str());
	if (RendererOwned()) {
		// The new device resolved r_renderer again and reported it.
		std::uint64_t serial=0;
		if (!RendererAgrees(RendererRequest(restoring),serial,error)) return false;
		if (serial<=selectionBefore) return Fail(error,"The restarted renderer did not report a new selection");
		selectionSerial=serial;
		if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("UI_SETTINGS_RENDERER restore=%d request=%s serial=%llu\n",
			restoring?1:0,RendererRequest(restoring).c_str(),static_cast<unsigned long long>(serial));
	}
	if (restoring) restorePlan=std::move(plan); else targetPlan=std::move(plan);
	// The new device loaded every image and its upload resources under the
	// settings now in effect; the sound device follows on the next sound frame.
	RestartSound(restoring);
	currentDevice=observed; output=candidate;
	if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("UI_SETTINGS_DEVICE restore=%d epoch=%llu generation=%llu submitted=%llu presented=%llu failures=%llu width=%d height=%d\n",
		restoring?1:0,static_cast<unsigned long long>(candidate.epoch),static_cast<unsigned long long>(candidate.generation),
		static_cast<unsigned long long>(candidate.submitted),static_cast<unsigned long long>(candidate.presented),static_cast<unsigned long long>(candidate.failures),
		observed.window.logicalWidth,observed.window.logicalHeight);
	return true;
}
bool EngineSettingsDisplayHost::Observe(bool restoring, SettingsDisplayObservation& output, std::string& error) {
	rendererDisplayState_t observed{}; SettingsDisplayObservation candidate;
	if (!DeviceWork()) {
		if (!ObserveDevice(observed,candidate,error) || !SoundReady(candidate.effectsReady,error)) return false;
		output=candidate; return true;
	}
	if (!ObserveDevice(observed,candidate,error) || !MatchesDisplay(restoring?restorePlan:targetPlan,observed,error)) return false;
	if (RendererOwned()) {
		if (!startup) { if (!RendererCurrent(restoring,error)) return false; }
		else {
			// A cold start has no restart of its own to name: the started device
			// reports the recovered request. Vulkan has one back end.
			const auto api=R_RendererModule_GetStatus().activeApi; std::uint64_t serial=0;
			if ((api==RENDER_MODULE_API_GL || api==RENDER_MODULE_API_GL_MODULE) && !RendererAgrees(RendererRequest(restoring),serial,error)) return false;
		}
	}
	if (!SoundReady(candidate.effectsReady,error)) return false;
	output=candidate; return true;
}
bool EngineSettingsDisplayHost::PersistConfirmation(const SettingsAttempt& attempt, std::string& error) {
	if (!ValidateLive(attempt.target,error) || !VerifyJournal(false,error)) return false;
	if (!DeviceWork()) {
		effects.state=SettingsJournalState::Confirmed;
		if (!WriteJournal(error) || !ValidateLive(attempt.target,error)) return false;
		return CommitConfiguration(error);
	}
	rendererDisplayState_t actual{}; SettingsDisplayObservation observed;
	if (!ObserveDevice(actual,observed,error) || actual.moduleEpoch!=currentDevice.moduleEpoch ||
		actual.presentation.generation!=currentDevice.presentation.generation ||
		actual.presentation.failureSequence!=currentDevice.presentation.failureSequence || !MatchesDisplay(targetPlan,actual,error)) return false;
	if (RendererOwned() && !RendererCurrent(false,error)) return false;
	if (!DisplayOwned()) {
		// The display and its placement are unchanged.
		committedPlacement=placement; effects.state=SettingsJournalState::Confirmed;
		if (!WriteJournal(error) || !Place(committedPlacement,error) || !ValidateLive(attempt.target,error)) return false;
		return CommitConfiguration(error);
	}
	char diagnostic[512]{};
	if (!Sys_BuildWindowPlacementCommit(placementToken,&expectedPlacement,&actual.window,&committedPlacement,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	if (double(committedPlacement.width)!=std::get<double>(attempt.target.at("r_windowWidth")) ||
		double(committedPlacement.height)!=std::get<double>(attempt.target.at("r_windowHeight"))) return Fail(error,"Actual normal window dimensions differ from the confirmed target");
	// Geometry outside the catalog is included before the confirmed marker. Its
	// exact values are checked and applied while automatic geometry writes stay leased.
	AddPlacement(kind==Kind::Display?journal.placement:effects.placement,"target.",committedPlacement);
	(kind==Kind::Display?journal.state:effects.state)=SettingsJournalState::Confirmed;
	if (!WriteJournal(error) || !Place(committedPlacement,error) || !ValidateLive(attempt.target,error)) return false;
	return CommitConfiguration(error);
}
bool EngineSettingsDisplayHost::Finish(bool restoring, std::string& error) {
	if (!DeviceWork()) return FinishJournal(error);
	auto finalState=restoring?placement:committedPlacement;
	if (restoring) {
		// The catalog transaction already restored its owned keys and rebased
		// untouched external values. Geometry cleanup must not overwrite those.
		finalState.width=expectedPlacement.width; finalState.height=expectedPlacement.height;
	}
	if (!Place(finalState,error)) return false;
	return FinishJournal(error);
}

bool EngineSettingsDisplayHost::Startup(std::string& error) {
	if (RecoveryActive() || placementToken) return Fail(error,"Previous in-process settings recovery is unresolved");
	if (!Paths(error) || !processLease.TryAcquire(lockPath,error)) { blocked=true; return false; }
	std::string bytes;
	const auto read=DurableReadExact(journalPath,SettingsJournalMaxBytes,bytes,error);
	if (read==DurableReadResult::Missing) { Clear(); return true; }
	blocked=true;
	if (read!=DurableReadResult::Present) return false;
	// A schema-2 record is a deferred, renderer or combined attempt; schema 1
	// keeps its display route.
	{
		SettingsJournalRecord record; std::string schemaError;
		if (DecodeSettingsJournalRecord(bytes,SystemSettingsHost::Schema(),record,schemaError) && record.Schema()==2) {
			const auto& effect=std::get<SettingsEffectRecoveryJournal>(*record.Value());
			// The record's own snapshots class it, as its attempt was classed.
			switch (SystemSettingsHost::ApplyClassOf(effect.baseline,effect.target)) {
			case SystemApplyClass::Renderer: return StartupRenderer(bytes,effect,error);
			case SystemApplyClass::Deferred: return StartupDeferred(bytes,effect,error);
			case SystemApplyClass::Combined: return StartupCombined(bytes,effect,error);
			default: return Fail(error,"Unsupported settings effect recovery record");
			}
		}
	}
	StateValues currentCatalog;
	if (!settings.Read(currentCatalog,error) || !DecodeSystemSettingsJournal(bytes,currentCatalog,journal,error)) return false;
	return StartupDisplay(bytes,false,error);
}
bool EngineSettingsDisplayHost::StartupDisplay(const std::string& bytes, bool combined, std::string& error) {
	ownsJournal=true; writtenBytes=bytes;
	if (journal.placement.size()!=18 || !ReadPlacement(journal.placement,"baseline.",placement) ||
		!ReadPlacement(journal.placement,"target.",committedPlacement)) return Fail(error,"Invalid settings recovery placement metadata");
	if (!settings.ValidateSavedTarget(journal.baseline,journal.target,error) || !RecoveryMetadata(journal,placement,committedPlacement,combined,error)) return false;
	startupConfirmed=journal.state==SettingsJournalState::Confirmed;
	const auto* services=Sys_GetRenderWindowServices();
	if (!services || !services->PrepareWindowSystem || !services->PrepareWindowSystem()) return Fail(error,"Cannot enumerate displays for settings startup recovery");
	SystemDisplayTopology topology;
	if (!CaptureDisplayTopology(topology,error) || !ResolveDisplayRecovery(startupConfirmed?journal.displayTarget:journal.displayRestore,
		topology,startupConfirmed?targetPlan:restorePlan,error)) return false;
	StateValues live;
	if (!settings.Read(live,error)) return false;
	const auto& desired=startupConfirmed?journal.target:journal.baseline;
	const double resolvedIndex=double((startupConfirmed?targetPlan:restorePlan).request.displayIndex);
	const bool explicitDisplay=std::get<double>(desired.at("r_screen"))>=0;
	startupTarget=live; StateValues patch;
	for (const auto& [key,value]:journal.patch) {
		const bool remappedSelection=key=="r_screen" && explicitDisplay && live.at(key)==StateValue(resolvedIndex);
		if (!SettingsValueEqual(live.at(key),journal.baseline.at(key)) && !SettingsValueEqual(live.at(key),value) && !remappedSelection) return Fail(error,"A settings recovery key changed outside the saved attempt");
		startupTarget[key]=desired.at(key);
		if (!SettingsValueEqual(live.at(key),desired.at(key))) patch[key]=desired.at(key);
	}
	// Numeric monitor indexes can change across processes. Recover the recorded
	// unique descriptor, then archive its current index without touching a new
	// external selection. Auto remains Auto.
	if (explicitDisplay) {
		if (live.at("r_screen")!=journal.baseline.at("r_screen") && live.at("r_screen")!=journal.target.at("r_screen") && live.at("r_screen")!=StateValue(resolvedIndex))
			return Fail(error,"The display selection changed outside the saved settings attempt");
		startupTarget["r_screen"]=resolvedIndex;
		if (startupTarget.at("r_screen")!=live.at("r_screen")) patch["r_screen"]=startupTarget.at("r_screen");
		else patch.erase("r_screen"); // A prior config commit may already contain the remap.
	}
	auto finalPlacement=startupConfirmed?committedPlacement:placement;
	finalPlacement.width=int(std::get<double>(startupTarget.at("r_windowWidth")));
	finalPlacement.height=int(std::get<double>(startupTarget.at("r_windowHeight")));
	if (!settings.ValidateRollback(journal.baseline,live,startupTarget,error)) return false;
	// A fresh process token is deliberately unrelated to the random journal ID.
	// This host is the only settings owner before UI initialization.
	constexpr std::uint64_t StartupPlacementToken=(std::numeric_limits<std::uint64_t>::max)();
	char diagnostic[512]{};
	if (!Sys_BeginWindowPlacementLease(StartupPlacementToken,&expectedPlacement,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	placementToken=StartupPlacementToken;
	// New external x/y values are never overwritten. Values matching either side
	// are recoverable; cached compositor rectangles are process-local and unused.
	if ((expectedPlacement.x!=placement.x && expectedPlacement.x!=committedPlacement.x) ||
		(expectedPlacement.y!=placement.y && expectedPlacement.y!=committedPlacement.y)) return Fail(error,"Window placement changed outside the saved settings attempt");
	if ((!patch.empty() && !settings.Write(patch,error)) || !ValidateLive(startupTarget,error)) return false;
	expectedPlacement.width=int(std::get<double>(startupTarget.at("r_windowWidth")));
	expectedPlacement.height=int(std::get<double>(startupTarget.at("r_windowHeight")));
	if (!Place(finalPlacement,error)) return false;
	startup=true; blocked=false; return true;
}
bool EngineSettingsDisplayHost::ValidateOwned(const StateValues& owned, const StateValues& expected, std::string& error) {
	StateValues live;
	if (!settings.Read(live,error)) return false;
	for (const auto& item:owned) {
		const auto actual=live.find(item.first);
		const auto wanted=expected.find(item.first);
		if (actual==live.end() || wanted==expected.end() || !SettingsValueEqual(actual->second,wanted->second))
			return Fail(error,"A recovered setting changed before it was saved");
	}
	return true;
}
bool EngineSettingsDisplayHost::StartupDeferred(const std::string& bytes, const SettingsEffectRecoveryJournal& saved, std::string& error) {
	// Only the light-grid preload has a deferred executor. Its record holds that
	// one portable policy in each direction, agreeing with the snapshots.
	const auto restore=saved.deferredRestore.find("lightGridPreload"), target=saved.deferredTarget.find("lightGridPreload");
	if (saved.plan.domainMask!=SystemSettingNextMap || saved.deferredRestore.size()!=1 || saved.deferredTarget.size()!=1 ||
		restore==saved.deferredRestore.end() || target==saved.deferredTarget.end() ||
		!SettingsValueEqual(restore->second,saved.baseline.at("r_lightGridPreload")) ||
		!SettingsValueEqual(target->second,saved.target.at("r_lightGridPreload")) ||
		SystemSettingsHost::ApplyClassOf(saved.baseline,saved.target)!=SystemApplyClass::Deferred)
		return Fail(error,"Unsupported settings effect recovery record");
	if (!settings.ValidateSavedTarget(saved.baseline,saved.target,error)) return false;
	StateValues live;
	if (!settings.Read(live,error)) return false;
	const bool approved=saved.state==SettingsJournalState::Confirmed;
	const auto& chosen=approved?saved.target:saved.baseline;
	startupTarget=live; StateValues patch;
	for (const auto& [key,value]:saved.patch) {
		if (!SettingsValueEqual(live.at(key),saved.baseline.at(key)) && !SettingsValueEqual(live.at(key),value))
			return Fail(error,"A settings recovery key changed outside the saved attempt");
		startupTarget[key]=chosen.at(key);
		if (!SettingsValueEqual(live.at(key),chosen.at(key))) patch[key]=chosen.at(key);
	}
	if (!settings.ValidateRollback(saved.baseline,live,startupTarget,error)) return false;
	if ((!patch.empty() && !settings.Write(patch,error)) || !ValidateLive(startupTarget,error)) return false;
	kind=Kind::Deferred; effects=saved; ownsJournal=true; writtenBytes=bytes;
	startupConfirmed=approved; catalogStartup=true; blocked=false; return true;
}
bool EngineSettingsDisplayHost::StartupRenderer(const std::string& bytes, const SettingsEffectRecoveryJournal& record, std::string& error) {
	// The requests agree with the snapshots, and the saved display is only
	// structural evidence: a renderer change never moved the display, so the
	// engine starts the normal way on whatever display it finds now.
	const auto from=record.resourceRestore.find("request"), to=record.resourceTarget.find("request");
	if (record.plan.domainMask!=SystemSettingRendererResources || record.resourceRestore.size()!=1 || record.resourceTarget.size()!=1 ||
		from==record.resourceRestore.end() || to==record.resourceTarget.end() ||
		!SettingsValueEqual(from->second,record.baseline.at("r_renderer")) || !SettingsValueEqual(to->second,record.target.at("r_renderer")) ||
		SystemSettingsHost::ApplyClassOf(record.baseline,record.target)!=SystemApplyClass::Renderer)
		return Fail(error,"Unsupported settings effect recovery record");
	if (!ValidateDisplayPreserveActualPair(record.displayRestore,record.displayTarget,record.baseline,record.target,error) ||
		!settings.ValidateSavedTarget(record.baseline,record.target,error)) return false;
	StateValues live;
	if (!settings.Read(live,error)) return false;
	const bool approved=record.state==SettingsJournalState::Confirmed;
	const auto& wanted=approved?record.target:record.baseline;
	startupTarget=live; StateValues replay;
	for (const auto& [key,value]:record.patch) {
		if (!SettingsValueEqual(live.at(key),record.baseline.at(key)) && !SettingsValueEqual(live.at(key),value))
			return Fail(error,"A settings recovery key changed outside the saved attempt");
		startupTarget[key]=wanted.at(key);
		if (!SettingsValueEqual(live.at(key),wanted.at(key))) replay[key]=wanted.at(key);
	}
	if (!settings.ValidateRollback(record.baseline,live,startupTarget,error)) return false;
	// The renderer that starts next resolves the recovered request.
	if ((!replay.empty() && !settings.Write(replay,error)) || !ValidateLive(startupTarget,error)) return false;
	kind=Kind::Renderer; effects=record; ownsJournal=true; writtenBytes=bytes;
	startupConfirmed=approved; catalogStartup=true; blocked=false; return true;
}
bool EngineSettingsDisplayHost::StartupCombined(const std::string& bytes, const SettingsEffectRecoveryJournal& record, std::string& error) {
	// Each domain's policy is what the snapshots say, so replaying the owned
	// settings before the renderer and the sound system start recovers every
	// domain: both read their settings as they open.
	const unsigned mask=record.plan.domainMask;
	if (SystemSettingsHost::ApplyClassOf(record.baseline,record.target)!=SystemApplyClass::Combined || !CombinedPolicyAgrees(record))
		return Fail(error,"Unsupported settings effect recovery record");
	if (!settings.ValidateSavedTarget(record.baseline,record.target,error)) return false;
	if (mask & SystemSettingDisplayRestart) {
		// The attempt moved the display: recover it as a display attempt does,
		// on the saved display with its placement, and commit after it presents.
		journal={}; journal.state=record.state; journal.attempt=record.attempt;
		journal.baseline=record.baseline; journal.target=record.target; journal.patch=record.patch;
		journal.displayRestore=record.displayRestore; journal.displayTarget=record.displayTarget; journal.placement=record.placement;
		kind=Kind::Combined; effects=record;
		if (StartupDisplay(bytes,true,error)) return true;
		kind=Kind::Display; effects={}; return false;
	}
	// The display did not move, so its saved pair is structural evidence only
	// and the engine starts the normal way on whatever display it finds.
	if ((mask & 19u) && !ValidateDisplayPreserveActualPair(record.displayRestore,record.displayTarget,record.baseline,record.target,error)) return false;
	StateValues live;
	if (!settings.Read(live,error)) return false;
	const bool approved=record.state==SettingsJournalState::Confirmed;
	const auto& wanted=approved?record.target:record.baseline;
	startupTarget=live; StateValues replay;
	for (const auto& [key,value]:record.patch) {
		if (!SettingsValueEqual(live.at(key),record.baseline.at(key)) && !SettingsValueEqual(live.at(key),value))
			return Fail(error,"A settings recovery key changed outside the saved attempt");
		startupTarget[key]=wanted.at(key);
		if (!SettingsValueEqual(live.at(key),wanted.at(key))) replay[key]=wanted.at(key);
	}
	if (!settings.ValidateRollback(record.baseline,live,startupTarget,error)) return false;
	if ((!replay.empty() && !settings.Write(replay,error)) || !ValidateLive(startupTarget,error)) return false;
	kind=Kind::Combined; effects=record; ownsJournal=true; writtenBytes=bytes;
	startupConfirmed=approved; catalogStartup=true; blocked=false; return true;
}
bool EngineSettingsDisplayHost::InitializeDisplay(std::string& error) {
	if (!startup) return true;
	auto& plan=startupConfirmed?targetPlan:restorePlan; char diagnostic[512]{};
	// A record written under OpenGL can recover under Vulkan, whose window is
	// single-sample (its scene targets take r_multiSamples).
	if (!WindowMultisampling()) plan.request.parms.multiSamples=0;
	if (!R_RendererModule_TryInitializeDisplay(&plan.request,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	SettingsDisplayObservation observed;
	if (!ObserveDevice(currentDevice,observed,error) || !observed.ready || !MatchesDisplay(plan,currentDevice,error)) return false;
	startupReady=true; startupDeadline=0; return true;
}
void EngineSettingsDisplayHost::StartupFrame(double now, bool allowWork) {
	if (catalogStartup) {
		// The recovered policy is already live and the next load uses it.
		// Persist it on a full frame, then remove the evidence. Renderer and
		// window startup may settle keys: the configuration archives what they
		// chose for keys the record does not own (the window size, an MSAA
		// fallback), and owned keys go back to the recovered choice (a platform
		// frame-cap default, for instance).
		if (blocked || !allowWork) return;
		std::string error; const bool approved=startupConfirmed;
		// Finishing clears the executor, so name it first.
		const char* const executor=kind==Kind::Renderer?"renderer":kind==Kind::Combined?"combined":"deferred";
		if (RendererOwned()) {
			const auto api=R_RendererModule_GetStatus().activeApi;
			std::uint64_t serial=0;
			// Vulkan has one back end; r_renderer chooses nothing to prove there.
			if ((api==RENDER_MODULE_API_GL || api==RENDER_MODULE_API_GL_MODULE) && !RendererAgrees(RendererRequest(!approved),serial,error)) {
				blocked=true; recoveryError=error; common->Warning("UI settings startup recovery: %s",error.c_str()); return;
			}
		}
		StateValues live, drift;
		if (settings.Read(live,error)) {
			for (const auto& item:effects.patch) {
				const auto current=live.find(item.first);
				const auto chosen=startupTarget.find(item.first);
				if (current!=live.end() && chosen!=startupTarget.end() && !SettingsValueEqual(current->second,chosen->second))
					drift[item.first]=chosen->second;
			}
		}
		if (!drift.empty() && cvarSystem->GetCVarBool("ui_retainedTrace"))
			common->Printf("UI_SETTINGS_STARTUP %s=1 reasserted=%d\n",executor,static_cast<int>(drift.size()));
		if ((!drift.empty() && !settings.Write(drift,error)) || !ValidateOwned(effects.patch,startupTarget,error) ||
			!CommitConfiguration(error) || !FinishJournal(error)) {
			blocked=true; recoveryError=error; common->Warning("UI settings startup recovery: %s",error.c_str()); return;
		}
		if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("UI_SETTINGS_STARTUP approved=%d %s=1\n",approved?1:0,executor);
		common->Printf("UI_SETTINGS startup_recovery=complete\n");
		return;
	}
	if (!startup || blocked || !startupReady) return;
	if (!std::isfinite(now) || now<0 || now<startupLastTime || !std::isfinite(now+20.0)) {
		blocked=true; recoveryError="Startup recovery clock moved backwards or is invalid";
		common->Warning("UI settings startup recovery: %s",recoveryError.c_str()); return;
	}
	startupLastTime=now;
	if (startupDeadline==0) startupDeadline=now+20.0;
	SettingsDisplayObservation observed; std::string error;
	const bool okay=Observe(!startupConfirmed,observed,error) && observed.epoch==currentDevice.moduleEpoch &&
		observed.generation==currentDevice.presentation.generation && observed.failures==currentDevice.presentation.failureSequence &&
		ValidateLive(startupTarget,error);
	if (!okay || now>=startupDeadline) {
		blocked=true; recoveryError=error.empty()?"Startup recovery did not present before its deadline":error;
		common->Warning("UI settings startup recovery: %s",recoveryError.c_str()); return;
	}
	if (observed.submitted<=currentDevice.presentation.submittedSequence || observed.presented<=currentDevice.presentation.presentedSequence) return;
	if (!allowWork) return;
	// Both recovery directions commit the already verified recovered live frame.
	// Pending has never persisted its unconfirmed candidate; Confirmed finishes
	// the approved choice. Unrelated live keys are preserved by patch replay. A
	// renderer record's Observe above already holds the device to its report.
	const auto initial=currentDevice; const bool approved=startupConfirmed;
	if (!CommitConfiguration(error) || !FinishJournal(error)) {
		blocked=true; recoveryError=error; common->Warning("UI settings startup persistence: %s",error.c_str());
	} else {
		if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("UI_SETTINGS_STARTUP approved=%d epoch=%llu generation=%llu initialSubmitted=%llu initialPresented=%llu submitted=%llu presented=%llu failures=%llu width=%d height=%d\n",
			approved?1:0,static_cast<unsigned long long>(observed.epoch),static_cast<unsigned long long>(observed.generation),
			static_cast<unsigned long long>(initial.presentation.submittedSequence),static_cast<unsigned long long>(initial.presentation.presentedSequence),
			static_cast<unsigned long long>(observed.submitted),static_cast<unsigned long long>(observed.presented),static_cast<unsigned long long>(observed.failures),
			initial.window.logicalWidth,initial.window.logicalHeight);
		common->Printf("UI_SETTINGS startup_recovery=complete\n");
	}
}
void EngineSettingsDisplayHost::Shutdown() {
	// Renderer/UI teardown cannot establish a fresh presentation. Keep durable
	// evidence for the next startup; merely release process-owned native handles.
	if (placementToken) {
		char diagnostic[512]{}; sysWindowPlacementSnapshot_t current;
		if (Sys_ReadWindowPlacementLease(placementToken,&current,diagnostic,sizeof(diagnostic))) {
			Sys_FinishWindowPlacementLease(placementToken,&current,&current,diagnostic,sizeof(diagnostic));
		}
	}
	Clear();
}
#endif
