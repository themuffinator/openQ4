// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#include "SettingsDisplayService.h"
#ifndef ID_DEDICATED
#include "../framework/SettingsPersistence.h"
#include <cmath>
#include <limits>

using namespace openq4;
using namespace openq4::ui;
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
bool RecoveryMetadata(const SettingsRecoveryJournal& journal,
	const sysWindowPlacementSnapshot_t& before,const sysWindowPlacementSnapshot_t& after,std::string& error) {
	const auto dimensions=[](const sysWindowPlacementSnapshot_t& placement,const StateValues& values) {
		return double(placement.width)==std::get<double>(values.at("r_windowWidth")) &&
			double(placement.height)==std::get<double>(values.at("r_windowHeight"));
	};
	const unsigned effects=SystemSettingsHost::ChangedEffects(journal.baseline,journal.target);
	if (!dimensions(before,journal.baseline) || !dimensions(after,journal.target) || effects!=SystemSettingDisplayRestart)
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
	processLease.Release(); journal={}; targetPlan={}; restorePlan={}; baselineDevice={}; currentDevice={};
	placement={}; expectedPlacement={}; committedPlacement={}; startupTarget.clear();
	placementToken=0; writtenBytes.clear(); attemptedBytes.clear(); recoveryError.clear();
	ownsJournal=placed=blocked=startup=startupReady=startupConfirmed=false; startupDeadline=0; startupLastTime=-1;
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
	if (!EncodeSettingsJournal(journal,SystemSettingsHost::Schema(),attemptedBytes,error)) return false;
	ownsJournal=true; // Publication can succeed even if a later durability barrier fails.
	if (!DurableReplaceExact(journalPath,attemptedBytes,error)) return false;
	writtenBytes=attemptedBytes;
	if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("UI_SETTINGS_JOURNAL state=%s durable=1\n",journal.state==SettingsJournalState::Confirmed?"confirmed":"pending");
	return true;
}
bool EngineSettingsDisplayHost::ValidateLive(const StateValues& target, std::string& error) {
	StateValues current;
	return settings.Read(current,error) && (current==target || Fail(error,"Settings changed outside the frozen display operation"));
}
bool EngineSettingsDisplayHost::CommitConfiguration(std::string& error) {
	std::string currentJournal,currentLock;
	if (!VerifyJournal(false,error) || !Common_SettingsPersistencePaths(currentJournal,currentLock,error)) return false;
	if (currentJournal!=journalPath || currentLock!=lockPath) return Fail(error,"Settings save root changed during the recovery lease");
	return Common_WriteSettingsConfiguration(true,error);
}
bool EngineSettingsDisplayHost::Prepare(const SettingsAttempt& attempt, std::string& error) {
	if (RecoveryActive() || placementToken) return Fail(error,"A previous settings recovery has not completed");
	if (!Paths(error) || !processLease.TryAcquire(lockPath,error)) return false;
	std::string bytes;
	const auto read=DurableReadExact(journalPath,SettingsJournalMaxBytes,bytes,error);
	if (read!=DurableReadResult::Missing) { blocked=true; return read==DurableReadResult::Failed?false:Fail(error,"An existing settings recovery journal requires startup recovery"); }
	if (!ValidateLive(attempt.baseline,error) || !R_RendererModule_QueryDisplay(&baselineDevice))
		return Fail(error,error.empty()?"Actual baseline display is unavailable":error.c_str());
	SystemDisplayTopology topology;
	if (!CaptureDisplayTopology(topology,error) || !BuildDisplayRestore(baselineDevice,topology,restorePlan,error) ||
		!BuildDisplayRequest(attempt.target,baselineDevice,topology,targetPlan,error)) return false;
	SettingsRecoveryJournal candidate; candidate.baseline=attempt.baseline; candidate.target=attempt.target; candidate.patch=attempt.patch;
	unsigned char random[16];
	if (!Sys_GetSecureRandomBytes(random,sizeof(random))) return Fail(error,"Cannot create a persistent settings attempt identity");
	for (unsigned char c:random) { candidate.attempt += "0123456789abcdef"[c>>4]; candidate.attempt += "0123456789abcdef"[c&15]; }
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
	SystemDisplayTopology topology; SystemDisplayPlan plan;
	if (!CaptureDisplayTopology(topology,error) || !ResolveDisplayRecovery(restoring?journal.displayRestore:journal.displayTarget,topology,plan,error)) return false;
	// Catalog writes have completed. Only their window dimensions may change;
	// x/y and the normal-placement cache remain under the full-duration lease.
	expectedPlacement.width=cvarSystem->GetCVarInteger("r_windowWidth");
	expectedPlacement.height=cvarSystem->GetCVarInteger("r_windowHeight");
	char diagnostic[512]{}; sysWindowPlacementSnapshot_t unchanged;
	if (!Sys_ReadWindowPlacementLease(placementToken,&unchanged,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	if (!Sys_ApplyWindowPlacementLease(placementToken,&expectedPlacement,&expectedPlacement,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	rendererDisplayState_t before{};
	if (!R_RendererModule_QueryDisplay(&before) || before.moduleEpoch!=baselineDevice.moduleEpoch) return Fail(error,"The renderer module changed during display recovery");
	if (!R_RendererModule_TryDeviceRestart(&plan.request,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	rendererDisplayState_t observed{}; SettingsDisplayObservation candidate;
	if (!ObserveDevice(observed,candidate,error) || candidate.epoch!=baselineDevice.moduleEpoch ||
		candidate.generation<=before.presentation.generation || !MatchesDisplay(plan,observed,error))
		return Fail(error,error.empty()?"The renderer did not create the requested new device":error.c_str());
	if (restoring) restorePlan=std::move(plan); else targetPlan=std::move(plan);
	currentDevice=observed; output=candidate;
	if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("UI_SETTINGS_DEVICE restore=%d epoch=%llu generation=%llu submitted=%llu presented=%llu failures=%llu width=%d height=%d\n",
		restoring?1:0,static_cast<unsigned long long>(candidate.epoch),static_cast<unsigned long long>(candidate.generation),
		static_cast<unsigned long long>(candidate.submitted),static_cast<unsigned long long>(candidate.presented),static_cast<unsigned long long>(candidate.failures),
		observed.window.logicalWidth,observed.window.logicalHeight);
	return true;
}
bool EngineSettingsDisplayHost::Observe(bool restoring, SettingsDisplayObservation& output, std::string& error) {
	rendererDisplayState_t observed{}; SettingsDisplayObservation candidate;
	if (!ObserveDevice(observed,candidate,error) || !MatchesDisplay(restoring?restorePlan:targetPlan,observed,error)) return false;
	output=candidate; return true;
}
bool EngineSettingsDisplayHost::PersistConfirmation(const SettingsAttempt& attempt, std::string& error) {
	if (!ValidateLive(attempt.target,error) || !VerifyJournal(false,error)) return false;
	rendererDisplayState_t actual{}; SettingsDisplayObservation observed;
	if (!ObserveDevice(actual,observed,error) || actual.moduleEpoch!=currentDevice.moduleEpoch ||
		actual.presentation.generation!=currentDevice.presentation.generation ||
		actual.presentation.failureSequence!=currentDevice.presentation.failureSequence || !MatchesDisplay(targetPlan,actual,error)) return false;
	char diagnostic[512]{};
	if (!Sys_BuildWindowPlacementCommit(placementToken,&expectedPlacement,&actual.window,&committedPlacement,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	if (double(committedPlacement.width)!=std::get<double>(attempt.target.at("r_windowWidth")) ||
		double(committedPlacement.height)!=std::get<double>(attempt.target.at("r_windowHeight"))) return Fail(error,"Actual normal window dimensions differ from the confirmed target");
	// Geometry outside the catalog is included before the confirmed marker. Its
	// exact values are checked and applied while automatic geometry writes stay leased.
	AddPlacement(journal.placement,"target.",committedPlacement);
	journal.state=SettingsJournalState::Confirmed;
	if (!WriteJournal(error) || !Place(committedPlacement,error) || !ValidateLive(attempt.target,error)) return false;
	return CommitConfiguration(error);
}
bool EngineSettingsDisplayHost::Finish(bool restoring, std::string& error) {
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
	if (read!=DurableReadResult::Present || !DecodeSettingsJournal(bytes,SystemSettingsHost::Schema(),journal,error)) return false;
	ownsJournal=true; writtenBytes=bytes;
	if (journal.placement.size()!=18 || !ReadPlacement(journal.placement,"baseline.",placement) ||
		!ReadPlacement(journal.placement,"target.",committedPlacement)) return Fail(error,"Invalid settings recovery placement metadata");
	if (!settings.ValidateSavedTarget(journal.baseline,journal.target,error) || !RecoveryMetadata(journal,placement,committedPlacement,error)) return false;
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
		if (live.at(key)!=journal.baseline.at(key) && live.at(key)!=value && !remappedSelection) return Fail(error,"A settings recovery key changed outside the saved attempt");
		startupTarget[key]=desired.at(key);
		if (live.at(key)!=desired.at(key)) patch[key]=desired.at(key);
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
bool EngineSettingsDisplayHost::InitializeDisplay(std::string& error) {
	if (!startup) return true;
	const auto& plan=startupConfirmed?targetPlan:restorePlan; char diagnostic[512]{};
	if (!R_RendererModule_TryInitializeDisplay(&plan.request,diagnostic,sizeof(diagnostic))) return Fail(error,diagnostic);
	SettingsDisplayObservation observed;
	if (!ObserveDevice(currentDevice,observed,error) || !observed.ready || !MatchesDisplay(plan,currentDevice,error)) return false;
	startupReady=true; startupDeadline=0; return true;
}
void EngineSettingsDisplayHost::StartupFrame(double now, bool allowWork) {
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
	// the approved choice. Unrelated live keys are preserved by patch replay.
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
