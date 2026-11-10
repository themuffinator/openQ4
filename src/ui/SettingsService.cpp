// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#include "SettingsService.h"

#ifdef ID_DEDICATED
std::uint64_t UI_SettingsCreateOwner() { return 0; }
void UI_SettingsReleaseOwner(std::uint64_t) {}
void UI_SettingsCloseOwner(std::uint64_t) {}
bool UI_SettingsExitReady(std::uint64_t) { return false; }
bool UI_SettingsConsumeExit(std::uint64_t) { return false; }
void UI_SettingsFrame(bool) {}
bool UI_SettingsBlocksConfigWrite() { return false; }
bool UI_SettingsStartup(std::string&) { return true; }
bool UI_SettingsInitializeDisplay(std::string&) { return true; }
bool UI_SettingsStartupActive() { return false; }
bool UI_SettingsRecoveryPending() { return false; }
void UI_SettingsShutdown() {}
bool UI_SettingsLevelLoadPolicy(bool&, std::uint64_t&) { return false; }
void UI_SettingsLevelUnloaded() {}
UI_SettingsRenderFrame::UI_SettingsRenderFrame() {}
UI_SettingsRenderFrame::~UI_SettingsRenderFrame() {}
void UI_SettingsRenderFrame::Submitting() {}
void UI_SettingsRenderFrame::Presented() {}
#else
#include "application/SystemSettingsHost.h"
#include "SettingsDisplayService.h"
#include "RenderingSettings.h"
#include <charconv>
#include <chrono>
#include <limits>
#include <memory>
#include <set>

namespace {
using namespace openq4::ui;
double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// The SYSTEM display lists. One topology capture serves while the stamp holds
// (the display topology, the window's display and the labels), and the lists
// rebuild when the owner or a draft key they depend on changes. The token
// names the pickable entries and their labels, so a pick applies only the
// list the page showed; a change of selection alone keeps it.
struct DisplayCatalogCache {
    bool captured = false, inputValid = false;
    SystemDisplayCatalogStamp stamp;
    SystemDisplayCatalogInput input;
    bool built = false;
    std::uint64_t owner = 0, token = 0;
    StateValues key;
    SystemDisplayCatalog catalog;
    // The display a pick chose, by descriptor, while that r_screen change is pending.
    bool picked = false;
    int pickedIndex = -1;
    SystemDisplayDescriptor pickedDescriptor;
};
struct Service {
	RenderingSettings rendering;
    bool rendererRestartQueued = false;
    SystemSettingsHost host;
    SettingsTransaction transaction{host};
    EngineSettingsDisplayHost device{host};
    SettingsDisplayController display{transaction,device,Now};
    std::set<std::uint64_t> owners;
    std::set<std::uint64_t> confirmationOwners;
    std::map<std::uint64_t,SettingsResult> results;
    bool abandon = false;
    bool closing = false;
    std::uint64_t waitingOwner = 0;
    std::uint64_t receiptOwner = 0, receiptRequest = 0;
    struct ExitIdentity { std::uint64_t owner = 0, request = 0; };
    ExitIdentity exitIntent, exitReceipt;
    DisplayCatalogCache catalog;
};
std::unique_ptr<Service>& Instance() { static std::unique_ptr<Service> service; return service; }
Service& Settings() { auto& service=Instance(); if (!service) service=std::make_unique<Service>(); return *service; }
std::uint64_t nextOwner = 1; // Survives game/renderer/service shutdown, never reused.
// Level-load policy tokens survive the service too, so a receipt from an
// earlier service can never match a later request. A receipt describes the
// map on screen only for the latest token, issued after the last unload.
std::uint64_t levelLoadToken = 0, levelUnloads = 0, unloadedToken = 0;
// Display list tokens survive the service too, so a token is never reused.
std::uint64_t catalogTokens = 0;
const char* const CatalogDraftKeys[] = {"r_screen","r_mode","r_customWidth","r_customHeight","r_displayRefresh"};
struct RenderFrame {
    unsigned depth = 0;
    bool valid = false, drawn = false, submitting = false;
    std::uint64_t owner = 0, request = 0;
    SettingsDisplayObservation before;
} renderFrame;
bool SameDisplay(const SettingsDisplayObservation& a, const SettingsDisplayObservation& b) {
    return a.ready && b.ready && a.epoch && a.generation && a.epoch == b.epoch &&
        a.generation == b.generation && a.failures == b.failures;
}
bool SameCounters(const SettingsDisplayObservation& a, const SettingsDisplayObservation& b) {
    return SameDisplay(a,b) && a.submitted == b.submitted && a.presented == b.presented;
}
bool FrameOwner(const Service& service) {
    return !service.closing && service.display.Stage() == SettingsDisplayStage::AwaitApply &&
        service.owners.contains(renderFrame.owner) && service.confirmationOwners.contains(renderFrame.owner) &&
        service.display.Owner() == renderFrame.owner && service.display.Request() == renderFrame.request;
}
bool NoArguments(const std::string& operation) {
    return operation == "settings.system.restartRenderer" || operation == "settings.system.begin" || operation == "settings.system.defaults" || operation == "settings.system.autodetect" ||
        operation == "settings.system.cancel" || operation == "settings.system.apply" ||
        operation == "settings.system.applyExit" ||
        operation == "settings.system.confirm" || operation == "settings.system.revert";
}
bool RequestToken(const std::string& value, std::uint64_t& token) {
    if (value.empty() || value.size()>20 || value.front()=='0') return false;
    const auto result=std::from_chars(value.data(),value.data()+value.size(),token);
    return result.ec==std::errc() && result.ptr==value.data()+value.size() && token!=0;
}
void RefreshCatalog(Service& service, std::uint64_t owner) {
    auto& cache = service.catalog;
    SystemDisplayCatalogStamp stamp;
    service.device.CatalogStamp(stamp);
    if (!cache.captured || !(cache.stamp == stamp)) {
        std::string error;
        cache.captured = true; cache.stamp = std::move(stamp);
        cache.inputValid = service.device.CaptureCatalogInput(cache.stamp,cache.input,error);
        cache.built = false;
    }
    const auto& draft = service.transaction.Draft();
    StateValues key;
    for (const char* name : CatalogDraftKeys) {
        const auto value = draft.find(name);
        if (value != draft.end()) key.emplace(name,value->second);
    }
    if (cache.built && cache.owner == owner && SettingsValuesEqual(cache.key,key)) return;
    SystemDisplayCatalog catalog; std::string error;
    if (!cache.inputValid || !BuildSystemDisplayCatalog(cache.input,draft,catalog,error)) catalog = {};
    // Selections and the reserved slots are not entries a pick can name.
    const auto mapping = [](SystemDisplayCatalog value) {
        value.deviceSelected = value.modeSelected = value.refreshSelected = 0;
        value.autoDisplay = -1; // Follows the stamp, whose change already issues a new token.
        value.unlistedMode.clear(); value.unlistedRefresh.clear(); return value;
    };
    if (!cache.built || cache.owner != owner || !(mapping(catalog) == mapping(cache.catalog))) {
        cache.token = ++catalogTokens;
        if (cvarSystem->GetCVarBool("ui_retainedTrace"))
            common->Printf("UI_SETTINGS_CATALOG token=%llu available=%d displays=%d devices=%d modes=%d refresh=%d\n",
                static_cast<unsigned long long>(cache.token),catalog.available?1:0,catalog.count,static_cast<int>(catalog.devices.size()),
                static_cast<int>(catalog.modes.size()),static_cast<int>(catalog.refresh.size()));
    }
    cache.catalog = std::move(catalog); cache.built = true; cache.owner = owner; cache.key = std::move(key);
}
// A display picked from the list must still be the monitor it named. The
// record lasts while that pick is the pending r_screen change.
bool PickedDisplayCurrent(Service& service, std::uint64_t owner) {
    auto& cache = service.catalog;
    const auto& draft = service.transaction.Draft(); const auto& baseline = service.transaction.Baseline();
    const auto screen = draft.find("r_screen"), before = baseline.find("r_screen");
    if (!cache.picked || screen == draft.end() || before == baseline.end() || SettingsValueEqual(screen->second,before->second) ||
        !std::holds_alternative<double>(screen->second) || std::get<double>(screen->second) != double(cache.pickedIndex)) {
        cache.picked = false; return true;
    }
    RefreshCatalog(service,owner);
    const auto& displays = cache.input.topology.displays;
    return cache.inputValid && cache.pickedIndex >= 0 && cache.pickedIndex < static_cast<int>(displays.size()) &&
        SameSystemDisplay(displays[static_cast<size_t>(cache.pickedIndex)],cache.pickedDescriptor);
}
// The display lists for the page, owner-only. Slots past a list's count carry
// empty labels; a list's reserved last slot names an unlisted current value.
void DisplayCatalogStatus(Service& service, std::uint64_t owner, StateValues& values) {
    RefreshCatalog(service,owner);
    const auto& catalog = service.catalog.catalog;
    values["settings.display.available"] = catalog.available;
    values["settings.display.spanAvailable"] = catalog.spanAvailable;
    values["settings.display.catalog"] = std::to_string(service.catalog.token);
    values["settings.display.count"] = static_cast<double>(catalog.count);
    values["settings.display.optionCount"] = static_cast<double>(1 + catalog.devices.size());
    for (int i = 0; i < SystemDisplayDeviceSlots; ++i)
        values["settings.display."+std::to_string(i)+".label"] = i < static_cast<int>(catalog.devices.size()) ? catalog.devices[size_t(i)] : std::string();
    values["settings.display.mode.optionCount"] = static_cast<double>(catalog.modes.size());
    values["settings.display.mode.selected"] = static_cast<double>(catalog.modeSelected);
    for (int i = 0; i < SystemDisplayModeSlots; ++i)
        values["settings.display.mode."+std::to_string(i)+".label"] = i < static_cast<int>(catalog.modes.size()) ? catalog.modes[size_t(i)].label :
            i == SystemDisplayModeSlots-1 && !catalog.unlistedMode.empty() ? catalog.unlistedMode.front().label : std::string();
    values["settings.display.refresh.optionCount"] = static_cast<double>(catalog.refresh.size());
    values["settings.display.refresh.selected"] = static_cast<double>(catalog.refreshSelected);
    for (int i = 0; i < SystemDisplayRefreshSlots; ++i)
        values["settings.display.refresh."+std::to_string(i)+".label"] = i < static_cast<int>(catalog.refresh.size()) ? catalog.refresh[size_t(i)].label :
            i == SystemDisplayRefreshSlots-1 && !catalog.unlistedRefresh.empty() ? catalog.unlistedRefresh.front().label : std::string();
}
bool DisplayOperation(const std::string& operation) {
    return operation == "settings.system.display" || operation == "settings.system.displayMode" || operation == "settings.system.displayRefresh";
}
bool Supported(Service& service,std::uint64_t owner,bool* invalidDraft = nullptr) {
    if (invalidDraft) *invalidDraft = false;
    const auto kind=SystemSettingsHost::ApplyClassOf(service.transaction.Baseline(),service.transaction.Draft());
    const auto samples=service.transaction.Draft().find("r_multiSamples");
    if (samples!=service.transaction.Draft().end() && std::get<double>(samples->second)!=0 &&
        !SettingsValueEqual(samples->second,service.transaction.Baseline().at("r_multiSamples")) &&
        !service.device.SupportsMultisampling()) return false;
    if (service.device.RecoveryActive()) return false;
    switch (kind) {
    case SystemApplyClass::None: case SystemApplyClass::Immediate: break;
    // Display confirmations and automatic attempts report through an owning view.
    case SystemApplyClass::Display: if (!service.confirmationOwners.contains(owner)) return false; break;
    case SystemApplyClass::Deferred:
        if (!service.confirmationOwners.contains(owner) || !service.device.ReadyForAutomatic()) return false;
        break;
    // The renderer fallback restarts the device and proves itself with the
    // renderer's selection report.
    case SystemApplyClass::Renderer:
        if (!service.confirmationOwners.contains(owner) || !service.device.SupportsRendererSelection()) return false;
        break;
    // A preset, or any batch that mixes classes: one journaled attempt that
    // restarts the renderer device and the sound device as its changes need.
    case SystemApplyClass::Combined:
        if (!service.confirmationOwners.contains(owner) ||
            !service.device.SupportsCombined(service.transaction.Baseline(),service.transaction.Draft())) return false;
        break;
    }
    if (service.transaction.Owner() == owner && !PickedDisplayCurrent(service,owner)) {
        if (invalidDraft) *invalidDraft = true;
        return false;
    }
    std::string error;
    bool valid = false;
    try { valid = service.host.Validate(service.transaction.Baseline(),service.transaction.Draft(),error); }
    catch (...) { valid = false; }
    if (invalidDraft) *invalidDraft = !valid;
    return valid;
}
void TraceExit(const Service::ExitIdentity& identity, const char* event) {
    if (cvarSystem->GetCVarBool("ui_retainedTrace"))
        common->Printf("UI_SETTINGS_EXIT owner=%llu request=%llu event=%s\n",
            static_cast<unsigned long long>(identity.owner),
            static_cast<unsigned long long>(identity.request),event);
}
void CancelExitIdentity(Service::ExitIdentity& identity, std::uint64_t owner) {
    if (owner && identity.owner == owner) { TraceExit(identity,"canceled"); identity = {}; }
}
void CancelExit(Service& service, std::uint64_t owner) {
    CancelExitIdentity(service.exitIntent,owner);
    CancelExitIdentity(service.exitReceipt,owner);
}
bool ForwardExitStage(SettingsDisplayStage stage) {
    return stage == SettingsDisplayStage::QueuedApply || stage == SettingsDisplayStage::AwaitApply ||
        stage == SettingsDisplayStage::Confirming || stage == SettingsDisplayStage::QueuedKeep ||
        stage == SettingsDisplayStage::QueuedAutomaticCommit || stage == SettingsDisplayStage::FinalizeKeep;
}
SettingsResult CompleteExit(Service& service, Service::ExitIdentity identity) {
    // Callers must already have witnessed a successful Apply or completed Keep.
    // These invariants guard that result; an empty/clean draft is not authority.
    if (!identity.owner || !service.owners.contains(identity.owner) || service.closing || service.abandon ||
        service.display.Active() || service.transaction.Owner() != identity.owner ||
        service.transaction.Phase() != SettingsPhase::Editing ||
        service.transaction.Dirty()) {
        CancelExit(service,identity.owner);
        return {SettingsCode::Busy,"The completed settings transaction cannot close"};
    }
    const auto result = service.transaction.Cancel(identity.owner);
    if (result.code == SettingsCode::Ok && service.transaction.Phase() == SettingsPhase::Closed &&
        !service.transaction.Owner()) {
        service.exitIntent = {};
        service.exitReceipt = identity; TraceExit(identity,"ready");
    } else CancelExit(service,identity.owner);
    return result;
}
// The light-grid preload a level load must use: an open attempt's baseline
// until its choice is saved, otherwise the live catalog value.
bool CommittedPreload(Service& service, bool& preload) {
    StateValue value; std::string error;
    if (service.display.Active() && !service.display.Persisted()) {
        // The attempt could still be undone: its baseline is the committed choice.
        const auto found = service.transaction.Baseline().find("r_lightGridPreload");
        if (found == service.transaction.Baseline().end()) return false;
        value = found->second;
    } else if (!service.host.ReadValue("r_lightGridPreload",value,error)) return false;
    const auto* flag = std::get_if<bool>(&value);
    if (!flag) return false;
    preload = *flag; return true;
}
// What the loaded map did with its light grids, and whether the committed
// choice waits for the next load. Only the receipt of the latest policy
// request since the last unload describes the map on screen.
void LightGridStatus(Service& service, StateValues& values) {
    bool committed = false, mapLoaded = false, consumed = false, effective = false, pending = false;
    const bool known = CommittedPreload(service,committed);
    renderLightGridLoadReceipt_t receipt{}; std::uint64_t serial = 0;
    if (levelLoadToken && levelLoadToken > unloadedToken && service.device.LightGridLoad(receipt,serial) &&
        receipt.token == levelLoadToken && receipt.outcome != RENDER_LIGHTGRID_RENDERER_STOPPED) {
        mapLoaded = true;
        consumed = receipt.outcome == RENDER_LIGHTGRID_STREAMED || receipt.outcome == RENDER_LIGHTGRID_PRELOADED ||
            receipt.outcome == RENDER_LIGHTGRID_PRELOAD_INCOMPLETE;
        effective = consumed && receipt.preload;
        pending = consumed && known && receipt.preload != committed;
    }
    values["settings.lightGrid.committed"] = committed; values["settings.lightGrid.mapLoaded"] = mapLoaded;
    values["settings.lightGrid.consumed"] = consumed; values["settings.lightGrid.effective"] = effective;
    values["settings.lightGrid.pending"] = pending;
}
// The renderer row: whether the renderer fallback can apply here, and whether
// the running renderer reports a fallback (an unavailable or retired request
// running the automatic pick).
void RendererStatus(Service& service, StateValues& values) {
    renderRendererSelection_t selection{}; std::uint64_t serial = 0;
    const bool fallback = service.device.RendererSelection(selection,serial) &&
        (selection.fallback == RENDER_SELECTION_UNAVAILABLE || selection.fallback == RENDER_SELECTION_LEGACY);
    values["settings.renderer.available"] = service.device.SupportsRendererSelection();
    values["settings.renderer.fallback"] = fallback;
}
const char* Message(SettingsCode code, SettingsPhase phase, bool dirty) {
    switch (code) {
        case SettingsCode::Busy: return "#str_230008";
        case SettingsCode::NotOpen: return "#str_230009";
        case SettingsCode::Invalid:
        case SettingsCode::ApplyFailed: return "#str_230010";
        case SettingsCode::Conflict: return "#str_230011";
        case SettingsCode::RollbackFailed: return "#str_230012";
        default: break;
    }
    if (phase == SettingsPhase::Confirming) return "#str_230013";
    return dirty ? "#str_230014" : "#str_230007";
}
}

std::uint64_t UI_SettingsCreateOwner() {
    auto& service = Settings();
    if (!nextOwner) return 0;
    const auto owner = nextOwner++;
    service.owners.insert(owner);
    return owner;
}
void UI_SettingsReleaseOwner(std::uint64_t owner) {
    auto& service = Settings();
    UI_SettingsCloseOwner(owner);
    service.owners.erase(owner); service.results.erase(owner); service.confirmationOwners.erase(owner);
}
void UI_SettingsCloseOwner(std::uint64_t owner) {
    auto& service = Settings();
    CancelExit(service,owner);
    if (service.waitingOwner == owner) service.waitingOwner = 0;
    if (!owner || service.transaction.Owner() != owner) return;
    if (service.display.Active()) {
        service.display.Close(owner); service.closing=true; return;
    }
    if (service.transaction.Phase() == SettingsPhase::Editing) {
        // Discarding a draft performs no host read or write, and allows a new
        // owner to open during the same Session lifecycle transition.
        service.transaction.Abandon(owner); service.abandon = service.closing = false;
    } else service.abandon = service.closing = true;
}
bool UI_SettingsExitReady(std::uint64_t owner) {
    const auto& service = Instance();
    return service && owner && service->owners.contains(owner) && service->exitReceipt.owner == owner &&
        service->transaction.Phase() == SettingsPhase::Closed && !service->transaction.Owner() &&
        !service->display.Active() && !service->closing && !service->abandon && !service->device.RecoveryActive();
}
bool UI_SettingsConsumeExit(std::uint64_t owner) {
    if (!UI_SettingsExitReady(owner)) return false;
    auto& service = *Instance();
    const auto receipt = service.exitReceipt; service.exitReceipt = {};
    TraceExit(receipt,"consumed"); return true;
}
void UI_SettingsFrame(bool allowWork) {
    auto& service = Settings();
    service.device.StartupFrame(Now(),allowWork);
    const auto owner = service.transaction.Owner();
    if (service.display.Active()) {
        const auto before=service.display.Stage(); const auto request=service.display.Request();
        const bool exitMatches = service.exitIntent.owner == owner && service.exitIntent.request == request &&
            service.display.Owner() == owner && service.owners.contains(owner) && !service.closing;
        service.display.Frame(Now(),service.owners.contains(service.display.Owner()) && !service.closing,allowWork);
        auto result = service.display.LastResult();
        if (service.exitIntent.owner) {
            if (!exitMatches || result.code != SettingsCode::Ok || service.closing) {
                CancelExit(service,service.exitIntent.owner);
            } else if (!service.display.Active()) {
                // PrepareConfirm may renew the internal token and complete in
                // this single synchronous Frame. Only an attempt that committed
                // its target can authorize exit; restoration also returns Ok/Editing.
                if (service.display.LastCommitted()) result = CompleteExit(service,service.exitIntent);
                else CancelExit(service,owner);
            } else if (!ForwardExitStage(service.display.Stage()) || service.display.Owner() != owner) {
                CancelExit(service,owner);
            } else if (service.display.Request() != request) {
                if (before == SettingsDisplayStage::QueuedKeep && service.display.Stage() == SettingsDisplayStage::FinalizeKeep)
                    service.exitIntent.request = service.display.Request();
                else CancelExit(service,owner);
            }
        }
        if (cvarSystem->GetCVarBool("ui_retainedTrace") && (before!=service.display.Stage() || request!=service.display.Request())) {
            common->Printf("UI_SETTINGS_DISPLAY stage=%d owner=%llu request=%llu result=%d blocked=%d detail=%s\n",
                static_cast<int>(service.display.Stage()),static_cast<unsigned long long>(owner),
                static_cast<unsigned long long>(service.display.Request()),static_cast<int>(service.display.LastResult().code),
                UI_SettingsBlocksConfigWrite()?1:0,service.display.LastResult().diagnostic.c_str());
            SettingsDisplayObservation presented; std::string error;
            if (service.display.Stage()==SettingsDisplayStage::Confirming && service.device.Observe(false,presented,error))
                common->Printf("UI_SETTINGS_PRESENT owner=%llu request=%llu epoch=%llu generation=%llu submitted=%llu presented=%llu failures=%llu\n",
                    static_cast<unsigned long long>(owner),static_cast<unsigned long long>(service.display.Request()),
                    static_cast<unsigned long long>(presented.epoch),static_cast<unsigned long long>(presented.generation),
                    static_cast<unsigned long long>(presented.submitted),static_cast<unsigned long long>(presented.presented),static_cast<unsigned long long>(presented.failures));
        }
        if (service.owners.contains(owner)) service.results[owner]=result;
        if (!service.display.Active() && service.closing) {
            service.closing=service.abandon=false;
            if (service.waitingOwner && service.owners.contains(service.waitingOwner))
                service.results[service.waitingOwner]=service.transaction.Begin(service.waitingOwner);
            service.waitingOwner=0;
        }
        return;
    }
    if (!allowWork) return;
    if (!owner) return;
    if (service.abandon) {
        auto result = service.transaction.Abandon(owner);
        // Recovery failure stays visible and blocks persistence; do not retry
        // writes every frame or transfer the transaction to another GUI.
        service.abandon = false;
        if (result.code != SettingsCode::Ok) {
            common->Warning("UI settings owner recovery: %s",result.diagnostic.c_str());
        } else {
            service.closing = false;
            if (service.waitingOwner && service.owners.contains(service.waitingOwner))
                result = service.transaction.Begin(service.waitingOwner);
        }
        if (service.waitingOwner && service.owners.contains(service.waitingOwner))
            service.results[service.waitingOwner] = result;
        else if (service.owners.contains(owner)) service.results[owner] = result;
        service.waitingOwner = 0;
    } else {
        const auto phase = service.transaction.Phase();
        const auto result = service.transaction.Tick(Now());
        if (service.transaction.Phase() != phase && service.owners.contains(owner))
            service.results[owner] = result;
    }
}
bool UI_SettingsBlocksConfigWrite() {
    const auto& service=Settings(); const auto phase = service.transaction.Phase();
    return service.display.Active() || service.device.RecoveryActive() || phase == SettingsPhase::Confirming ||
        phase == SettingsPhase::RecoveryRequired || phase==SettingsPhase::Applying || phase==SettingsPhase::Restoring;
}
bool UI_SettingsStartup(std::string& error) { return Settings().device.Startup(error); }
bool UI_SettingsLevelLoadPolicy(bool& preload, std::uint64_t& token) {
    bool committed = false;
    if (!CommittedPreload(Settings(),committed) || levelLoadToken == (std::numeric_limits<std::uint64_t>::max)()) return false;
    preload = committed; token = ++levelLoadToken;
    return true;
}
void UI_SettingsLevelUnloaded() {
    if (levelUnloads != (std::numeric_limits<std::uint64_t>::max)()) ++levelUnloads;
    unloadedToken = levelLoadToken;
}
bool UI_SettingsInitializeDisplay(std::string& error) { return Settings().device.InitializeDisplay(error); }
bool UI_SettingsStartupActive() { return Settings().device.StartupActive(); }
bool UI_SettingsRecoveryPending() { return Settings().device.RecoveryActive(); }
void UI_SettingsShutdown() {
    auto& service=Instance(); if (service) { service->device.Shutdown(); service.reset(); }
}
void UI_SettingsConfirmationDocument(std::uint64_t owner, const DocumentModel& document) {
    auto& service=Settings(); service.confirmationOwners.erase(owner);
    if (!owner || !service.owners.contains(owner)) return;
    const auto& schema=UI_SettingsStateSchema();
    for (const char* key:{"settings.request","settings.confirmationVisible","settings.canConfirm","settings.canRevert","settings.canRetry","settings.remaining"}) {
        const auto declaration=document.state.find(key);
        if (declaration==document.state.end() || declaration->second.initial.index()!=schema.at(key) || declaration->second.HostSourced()) return;
    }
    for (const auto& [id,operation]:std::map<std::string,std::string>{{"settings_keep","settings.system.confirm"},{"settings_revert","settings.system.revert"},{"settings_retry","settings.system.retry"}}) {
        const auto* node=document.FindNode(id);
        if (!node || !node->control || node->control->label.empty()) return;
        const auto action=document.actions.find(node->control->action);
        if (action==document.actions.end() || action->second.operation!=operation || action->second.arguments.size()!=1) return;
        const auto request=action->second.arguments.find("request");
        if (request==action->second.arguments.end() || request->second.type!=2 || !request->second.op.empty() ||
            request->second.state!="settings.request" || !request->second.presentation.empty() || !request->second.args.empty()) return;
    }
    service.confirmationOwners.insert(owner);
}
void UI_SettingsOwnerDrawn(std::uint64_t owner, const std::string& displayedRequest) {
    auto& instance=Instance(); std::uint64_t request=0;
    if (!instance || renderFrame.depth != 1 || !renderFrame.valid || renderFrame.submitting ||
        owner != renderFrame.owner || !RequestToken(displayedRequest,request) || request != renderFrame.request ||
        !FrameOwner(*instance)) return;
    SettingsDisplayObservation observed; std::string error;
    if (!instance->device.Observe(false,observed,error) || !SameCounters(observed,renderFrame.before)) {
        renderFrame.valid=false; return;
    }
    renderFrame.drawn=true;
}
UI_SettingsRenderFrame::UI_SettingsRenderFrame() {
    if (++renderFrame.depth != 1) { renderFrame.valid=false; return; }
    renderFrame = RenderFrame{}; renderFrame.depth=1;
    auto& instance=Instance();
    if (!instance || instance->display.Stage()!=SettingsDisplayStage::AwaitApply) return;
    auto& service=*instance;
    renderFrame.owner=service.display.Owner(); renderFrame.request=service.display.Request();
    if (!FrameOwner(service)) return;
    // Keep the first proven receipt immutable. The controller intentionally
    // requires one further API present before starting its countdown.
    if (service.receiptOwner==renderFrame.owner && service.receiptRequest==renderFrame.request) return;
    try {
        std::string error;
        renderFrame.valid=service.device.Observe(false,renderFrame.before,error) &&
            renderFrame.before.ready && renderFrame.before.epoch && renderFrame.before.generation;
    } catch (...) {
        renderFrame=RenderFrame{}; throw;
    }
}
UI_SettingsRenderFrame::~UI_SettingsRenderFrame() {
    renderFrame.valid=false;
    if (renderFrame.depth && --renderFrame.depth==0) renderFrame=RenderFrame{};
}
void UI_SettingsRenderFrame::Submitting() {
    auto& instance=Instance();
    if (!instance || renderFrame.depth!=1 || !renderFrame.valid || !renderFrame.drawn || renderFrame.submitting ||
        !FrameOwner(*instance)) { renderFrame.valid=false; return; }
    SettingsDisplayObservation observed; std::string error;
    if (!instance->device.Observe(false,observed,error) || !SameCounters(observed,renderFrame.before)) {
        renderFrame.valid=false; return;
    }
    renderFrame.submitting=true;
}
void UI_SettingsRenderFrame::Presented() {
    auto& instance=Instance();
    const bool eligible=instance && renderFrame.depth==1 && renderFrame.valid && renderFrame.drawn &&
        renderFrame.submitting && FrameOwner(*instance);
    renderFrame.valid=false; // A receipt can be consumed only once, including failure.
    if (!eligible) return;
    auto& service=*instance;
    SettingsDisplayObservation observed, acknowledged; std::string error;
    if (!service.device.Observe(false,observed,error) || !SameDisplay(observed,renderFrame.before) ||
        observed.submitted<=renderFrame.before.submitted || observed.presented<=renderFrame.before.presented ||
        observed.submitted-renderFrame.before.submitted!=1 || observed.presented-renderFrame.before.presented!=1) return;
    // EndFrame executes backend commands and the GL/VK API present synchronously.
    // Exact increments exclude skipped/capture-only frames and additional
    // readbacks without requiring historically equal submission/present totals.
    if (!service.display.OwnerDrawn(renderFrame.owner,renderFrame.request,&acknowledged)) return;
    service.receiptOwner=renderFrame.owner; service.receiptRequest=renderFrame.request;
    if (cvarSystem->GetCVarBool("ui_retainedTrace"))
        common->Printf("UI_SETTINGS_VIEW owner=%llu request=%llu epoch=%llu generation=%llu submitted=%llu presented=%llu failures=%llu\n",
            static_cast<unsigned long long>(renderFrame.owner),static_cast<unsigned long long>(renderFrame.request),
            static_cast<unsigned long long>(acknowledged.epoch),static_cast<unsigned long long>(acknowledged.generation),
            static_cast<unsigned long long>(acknowledged.submitted),static_cast<unsigned long long>(acknowledged.presented),static_cast<unsigned long long>(acknowledged.failures));
}
bool UI_SettingsOperation(const Action& action, std::string& error) {
    if (NoArguments(action.operation) && action.arguments.empty()) return true;
    if ((action.operation=="settings.system.confirm" || action.operation=="settings.system.revert" || action.operation=="settings.system.retry") &&
        action.arguments.size()==1 && action.arguments.contains("request") && action.arguments.at("request").type==2) return true;
    if (action.operation=="settings.system.preset" && action.arguments.size()==1 &&
        action.arguments.contains("name") && action.arguments.at("name").type==2) return true;
    if (action.operation=="settings.system.backend" && action.arguments.size()==1 &&
        action.arguments.contains("api") && action.arguments.at("api").type==2) return true;
    if (action.operation=="settings.system.renderDevice" && action.arguments.size()==2 &&
        action.arguments.contains("index") && action.arguments.at("index").type==0 &&
        action.arguments.contains("catalog") && action.arguments.at("catalog").type==2) return true;
    // A display list pick: its slot index and the list token it saw.
    if (DisplayOperation(action.operation) && action.arguments.size()==2 &&
        action.arguments.contains("index") && action.arguments.at("index").type==0 &&
        action.arguments.contains("catalog") && action.arguments.at("catalog").type==2) return true;
    if (action.operation == "settings.system.edit" && !action.arguments.empty()) {
        const auto& schema = SystemSettingsHost::Schema();
        for (const auto& [key,value] : action.arguments) {
            const auto field = schema.find(key);
            if (field == schema.end() || field->second != value.type) {
                error = "Invalid system settings field or argument type: " + key; return false;
            }
        }
        return true;
    }
    error = "Unsupported system settings operation or argument shape"; return false;
}
bool UI_SettingsInvocation(const ActionInvocation& action, std::string& error) {
    Action descriptor; descriptor.operation = action.operation;
    for (const auto& [key,value] : action.arguments) {
        if (!ValidStateValue(value)) { error = "Invalid system settings value: " + key; return false; }
        Expression expression; expression.type = value.index();
        descriptor.arguments.emplace(key,std::move(expression));
    }
    return UI_SettingsOperation(descriptor,error);
}
bool UI_SettingsDispatch(std::uint64_t owner, const ActionInvocation& action, std::string& error) {
    auto& service = Settings();
    if (!owner || !service.owners.contains(owner) || !UI_SettingsInvocation(action,error)) {
        CancelExit(service,owner);
        if (error.empty()) error = "System settings owner is unavailable";
        return false;
    }
    // A subsequent operation cannot spend a prior receipt. Only matching Keep
    // may continue an existing intent; Retry recovers data, never exit intent.
    CancelExitIdentity(service.exitReceipt,owner);
    if (action.operation != "settings.system.confirm") CancelExitIdentity(service.exitIntent,owner);
    auto& transaction = service.transaction;
    SettingsResult result;
    if (service.rendererRestartQueued) {
        error="Renderer restart is already queued"; service.results[owner]={SettingsCode::Busy,error}; return false;
    }
    if (service.device.StartupActive() || (!service.display.Active() && service.device.RecoveryActive())) {
        CancelExit(service,owner);
        error="Settings startup recovery is unresolved"; service.results[owner]={SettingsCode::Busy,error}; return false;
    }
    if (action.operation == "settings.system.begin") {
        if (service.closing && transaction.Owner()) {
            // A failed close may outlive its GUI. An explicit open can request
            // one recovery attempt on the engine frame; it never steals the
            // old baseline or overwrites externally changed values. Success
            // opens the waiting owner; failure remains visible until retried.
            if (!service.waitingOwner || service.waitingOwner == owner) {
                service.waitingOwner = owner;
                if (service.display.Active()) {
                    if (service.display.CanRetry()) service.display.Retry(service.display.Owner(),service.display.Request());
                    else service.display.Close(service.display.Owner());
                }
                else service.abandon = true;
            }
            result = {SettingsCode::Busy,"Settings owner recovery is pending"};
        } else {
            // A second begin while editing keeps the draft and its picked
            // display; a new session starts without one.
            const bool opening = transaction.Phase() == SettingsPhase::Closed;
            result = transaction.Begin(owner);
            if (result.code == SettingsCode::Ok) {
                service.abandon = false;
                if (opening) { service.catalog.picked = false; service.rendering.Reset(); }
                CancelExitIdentity(service.exitReceipt,service.exitReceipt.owner);
            }
        }
    }
    else if (service.display.Active()) {
        if (action.operation=="settings.system.confirm" || action.operation=="settings.system.revert" || action.operation=="settings.system.retry") {
            std::uint64_t request=0;
            if (!action.arguments.contains("request") || !RequestToken(std::get<std::string>(action.arguments.at("request")),request))
                result={SettingsCode::Invalid,"Display actions require the displayed request identity"};
            else if (action.operation=="settings.system.confirm") result=service.display.Keep(owner,request,Now());
            else if (action.operation=="settings.system.retry") result=service.display.Retry(owner,request);
            else result=service.display.Revert(owner,request);
        } else if (action.operation=="settings.system.cancel") result=service.display.Revert(owner,service.display.Request());
        else result={SettingsCode::Busy,"Display confirmation or recovery is pending"};
    }
    else if (action.operation=="settings.system.retry" ||
        ((action.operation=="settings.system.confirm" || action.operation=="settings.system.revert") && !action.arguments.empty()))
        result={SettingsCode::Busy,"The display action belongs to a completed or stale request"};
    else if (action.operation=="settings.system.preset" || action.operation=="settings.system.autodetect") {
        if(service.closing || service.abandon) result={SettingsCode::Busy,"Settings owner is closing"};
        else if(action.operation=="settings.system.autodetect" && RenderingSettings::Pending(transaction.Draft()))
            result={SettingsCode::Busy,"Restart with the selected backend and GPU before detecting quality"};
        else result=transaction.EditGenerated(owner,[&](StateValues& patch,std::string& diagnostic){
            const bool expanded=action.operation=="settings.system.preset" ?
                service.host.BuildPreset(std::get<std::string>(action.arguments.at("name")),patch,diagnostic) :
                service.host.BuildDetectedPreset(patch,diagnostic);
            if(!expanded)return false;
            if(!service.owners.contains(owner) || service.closing || service.abandon){
                diagnostic="Settings owner changed during profile observation";return false;
            }
            return true;
        });
    }
    else if (action.operation=="settings.system.backend" || action.operation=="settings.system.renderDevice") {
        if(service.closing || service.abandon) result={SettingsCode::Busy,"Settings owner is closing"};
        else result=transaction.EditGenerated(owner,[&](StateValues& patch,std::string& diagnostic) {
            const bool expanded=action.operation=="settings.system.backend" ?
                service.rendering.Backend(std::get<std::string>(action.arguments.at("api")),patch,diagnostic) :
                service.rendering.Select(std::get<double>(action.arguments.at("index")),
                    std::get<std::string>(action.arguments.at("catalog")),patch,diagnostic);
            if(!expanded)return false;
            if(!service.owners.contains(owner) || service.closing || service.abandon){
                diagnostic="Settings owner changed during GPU observation";return false;
            }
            return true;
        });
    }
    else if (action.operation=="settings.system.restartRenderer") {
        std::string diagnostic;
        if(service.closing || service.abandon || transaction.Owner()!=owner || transaction.Phase()!=SettingsPhase::Editing || transaction.Dirty())
            result={SettingsCode::Busy,"Apply or discard changes before restarting"};
        else if(!RenderingSettings::Pending(transaction.Draft())) result={SettingsCode::Invalid,"No renderer restart is pending"};
        else if(!RenderingSettings::Restart(diagnostic)) result={SettingsCode::ApplyFailed,diagnostic};
        else service.rendererRestartQueued=true;
    }
    else if (DisplayOperation(action.operation)) {
        const auto list = action.operation=="settings.system.display" ? SystemDisplayList::Device :
            action.operation=="settings.system.displayMode" ? SystemDisplayList::Mode : SystemDisplayList::Refresh;
        const double index = std::get<double>(action.arguments.at("index"));
        std::uint64_t token = 0;
        if (service.closing || service.abandon) result={SettingsCode::Busy,"Settings owner is closing"};
        else if (transaction.Owner() != owner) result={SettingsCode::NotOpen,"Open settings before choosing a display"};
        else if (!std::isfinite(index) || std::floor(index) != index || index < -1 || index >= SystemDisplayModeSlots)
            result={SettingsCode::Invalid,"A display list pick names a whole slot"};
        else {
            RefreshCatalog(service,owner);
            auto& cache = service.catalog;
            // A pick names the list it saw; a list rebuilt since then is stale.
            if (!RequestToken(std::get<std::string>(action.arguments.at("catalog")),token) || token != cache.token)
                result={SettingsCode::Conflict,"The display list changed; choose again"};
            else {
                const auto catalog = cache.catalog;
                result=transaction.EditGenerated(owner,[&](StateValues& patch,std::string& diagnostic){
                    return SystemDisplaySelectionPatch(catalog,list,static_cast<int>(index),patch,diagnostic);
                });
                if (result.code==SettingsCode::Ok && list==SystemDisplayList::Device) {
                    cache.picked = index >= 0 && index < catalog.count;
                    cache.pickedIndex = static_cast<int>(index);
                    cache.pickedDescriptor = cache.picked ? catalog.descriptors[static_cast<size_t>(index)] : SystemDisplayDescriptor{};
                }
            }
        }
    }
    else if (action.operation == "settings.system.edit") result = transaction.Edit(owner,action.arguments);
    else if (action.operation == "settings.system.defaults") result = transaction.Defaults(owner);
    else if (action.operation == "settings.system.cancel") {
        // Local Number drafts can outlive a completed service cancel when a
        // callback invalidates their exact discard snapshot. A later explicit
        // discard may finish without reopening or writing accepted settings.
        // Display/startup/recovery and registered-owner checks still run above.
        result = !transaction.Owner() && transaction.Phase() == SettingsPhase::Closed ?
            SettingsResult{SettingsCode::Ok,{}} : transaction.Cancel(owner);
        if (result.code == SettingsCode::Ok) service.catalog.picked = false;
    }
    else if (action.operation == "settings.system.confirm") result = transaction.Confirm(owner);
    else if (action.operation == "settings.system.revert") {
        result = transaction.Revert(owner);
        if (result.code == SettingsCode::Ok) service.catalog.picked = false;
    }
    else if (action.operation == "settings.system.apply" || action.operation == "settings.system.applyExit") {
        if (transaction.Owner() == owner && transaction.Phase() == SettingsPhase::Editing &&
            !Supported(service,owner))
            result = {SettingsCode::Invalid,"System settings batch is invalid, requires unsupported effects or lacks an owning confirmation view"};
        else {
            const auto kind = SystemSettingsHost::ApplyClassOf(transaction.Baseline(),transaction.Draft());
            if (kind == SystemApplyClass::Display) result = service.display.Apply(owner,Now());
            // The next map's light-grid policy and the renderer fallback need no
            // confirmation: each completes automatically once its journal, write,
            // effect and a later frame are proved.
            else if (kind == SystemApplyClass::Deferred || kind == SystemApplyClass::Renderer)
                result = service.display.Apply(owner,Now(),SettingsCompletion::Automatic);
            // A combined batch asks for confirmation only when it changes the display.
            else if (kind == SystemApplyClass::Combined)
                result = service.display.Apply(owner,Now(),service.host.NeedsConfirmation(transaction.Baseline(),transaction.Draft()) ?
                    SettingsCompletion::UserConfirmation : SettingsCompletion::Automatic);
            else result = transaction.Apply(owner,Now());
        }
    }
    if (result.code != SettingsCode::Ok) CancelExit(service,owner);
    else if (action.operation == "settings.system.applyExit") {
        if (service.display.Active() && service.display.Owner() == owner && !service.closing) {
            service.exitIntent = {owner,service.display.Request()};
            TraceExit(service.exitIntent,"armed");
        } else result = CompleteExit(service,{owner,0});
    }
    service.results[owner] = result;
    error = result.diagnostic;
    if (cvarSystem->GetCVarBool("ui_retainedTrace"))
        common->Printf("UI_SETTINGS operation=%s result=%d phase=%d owner=%llu dirty=%d\n",action.operation.c_str(),
            static_cast<int>(result.code),static_cast<int>(transaction.Phase()),static_cast<unsigned long long>(owner),
            transaction.Dirty() ? 1 : 0);
    return result.code == SettingsCode::Ok;
}
const std::map<std::string,std::size_t>& UI_SettingsStateSchema() {
    static const auto schema = [] {
        std::map<std::string,std::size_t> result{{"settings.open",1},{"settings.dirty",1},
            {"settings.busy",1},{"settings.canApply",1},{"settings.message",2},{"settings.phase",0},{"settings.msaaAvailable",1},
            {"settings.request",2},{"settings.canConfirm",1},{"settings.canRevert",1},{"settings.canRetry",1},{"settings.remaining",0},{"settings.confirmationVisible",1},
            {"settings.lightGrid.committed",1},{"settings.lightGrid.mapLoaded",1},{"settings.lightGrid.consumed",1},
            {"settings.lightGrid.effective",1},{"settings.lightGrid.pending",1},
            {"settings.renderer.available",1},{"settings.renderer.fallback",1},
            {"settings.display.available",1},{"settings.display.spanAvailable",1},{"settings.display.catalog",2},
            {"settings.display.count",0},{"settings.display.optionCount",0},
            {"settings.display.mode.optionCount",0},{"settings.display.mode.selected",0},
            {"settings.display.refresh.optionCount",0},{"settings.display.refresh.selected",0}};
        for (int i = 0; i < SystemDisplayDeviceSlots; ++i) result.emplace("settings.display."+std::to_string(i)+".label",2);
        for (int i = 0; i < SystemDisplayModeSlots; ++i) result.emplace("settings.display.mode."+std::to_string(i)+".label",2);
        for (int i = 0; i < SystemDisplayRefreshSlots; ++i) result.emplace("settings.display.refresh."+std::to_string(i)+".label",2);
        for (const auto& [key,type] : SystemSettingsHost::Schema()) {
            result.emplace("settings.draft."+key,type);
            result.emplace("settings.baseline."+key,type);
        }
        for (const auto& [key,type] : RenderingSettings::Schema()) result.emplace(key,type);
        return result;
    }();
    return schema;
}
bool UI_SettingsRead(std::uint64_t owner, StateValues& values) {
    auto& service = Settings();
    if (!owner || !service.owners.contains(owner)) return false;
    auto& transaction = service.transaction;
    const bool own = transaction.Owner() == owner;
    auto phase = own ? transaction.Phase() : SettingsPhase::Closed;
    if (own && service.display.Stage()==SettingsDisplayStage::Recovery) phase=SettingsPhase::RecoveryRequired;
    // Readback also precedes queued-control dispatch. Refresh before publishing
    // so changed values invalidate old proposals and preserve local Number text
    // through the runtime's existing conflict handling. Never rebase recovery.
    SettingsResult refresh;
    if (own && phase == SettingsPhase::Editing && !service.display.Active() &&
        !service.device.StartupActive() && !service.device.RecoveryActive() && !service.closing && !service.abandon)
        refresh = transaction.Refresh(owner);
    const bool dirty = own && transaction.Dirty();
    const auto result = service.results.find(owner);
    const auto code = refresh.code != SettingsCode::Ok ? refresh.code :
        result == service.results.end() ? SettingsCode::Ok : result->second.code;
    bool invalidDraft = false;
    const bool canApply = own && phase == SettingsPhase::Editing && dirty && refresh.code == SettingsCode::Ok && !service.display.Active() &&
        Supported(service,owner,&invalidDraft);
    StateValues candidate{{"settings.open",own},{"settings.dirty",dirty},
        {"settings.busy",(transaction.Owner() != 0 && !own) || (own && service.display.Active()) || service.device.StartupActive()},
        {"settings.canApply",canApply},
        {"settings.msaaAvailable",service.device.SupportsMultisampling()},
        {"settings.request",own && service.display.Active()?std::to_string(service.display.Request()):std::string()},
        {"settings.canConfirm",own && service.display.CanConfirm(Now())},
        {"settings.canRevert",own && service.display.CanRevert()},
        {"settings.canRetry",own && service.display.CanRetry()},
        {"settings.remaining",own?service.display.Remaining(Now()):0.0},
        {"settings.confirmationVisible",own && service.display.ConfirmationVisible()},
        {"settings.phase",static_cast<double>(phase)},
        {"settings.message",std::string(Message(code,phase,dirty))}};
    if (invalidDraft && code == SettingsCode::Ok) candidate["settings.message"] = std::string("#str_230023");
    // Apply writes these two, but declarations already parsed keep the old
    // value: the draft says so while it holds such a change.
    else if (own && dirty && code == SettingsCode::Ok && !service.display.Active() &&
        SystemSettingsHost::ChangedNeedsNextStart(transaction.Baseline(),transaction.Draft()))
        candidate["settings.message"] = std::string("#str_230076");
    // An automatic attempt changes no display, so its status names settings.
    const bool automatic = service.display.Completion() == SettingsCompletion::Automatic;
    if (own) switch (service.display.Stage()) {
        case SettingsDisplayStage::QueuedApply: case SettingsDisplayStage::AwaitApply: case SettingsDisplayStage::QueuedAutomaticCommit:
            candidate["settings.message"]=std::string(automatic?"#str_230073":"#str_229990"); break;
        case SettingsDisplayStage::QueuedRestore: case SettingsDisplayStage::AwaitRestore: case SettingsDisplayStage::FinalizeRestore:
            candidate["settings.message"]=std::string(automatic?"#str_230074":"#str_229991"); break;
        case SettingsDisplayStage::QueuedKeep: case SettingsDisplayStage::FinalizeKeep:
            candidate["settings.message"]=std::string(automatic?"#str_230075":"#str_229992"); break;
        case SettingsDisplayStage::Recovery:
            if (service.display.Approved()) candidate["settings.message"]=std::string("#str_229997"); break;
        default: break;
    }
    if (own) {
        LightGridStatus(service,candidate);
        RendererStatus(service,candidate);
        service.rendering.Read(transaction.Draft(),!dirty && phase==SettingsPhase::Editing &&
            !service.display.Active() && !service.closing && !service.abandon && !service.rendererRestartQueued,candidate);
        DisplayCatalogStatus(service,owner,candidate);
        for (const auto& [key,value] : transaction.Draft()) candidate.emplace("settings.draft."+key,value);
        for (const auto& [key,value] : transaction.Baseline()) candidate.emplace("settings.baseline."+key,value);
    }
    values = std::move(candidate); return true;
}
#endif
