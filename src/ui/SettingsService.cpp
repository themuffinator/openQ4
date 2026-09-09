// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#include "SettingsService.h"

#ifdef ID_DEDICATED
std::uint64_t UI_SettingsCreateOwner() { return 0; }
void UI_SettingsReleaseOwner(std::uint64_t) {}
void UI_SettingsCloseOwner(std::uint64_t) {}
void UI_SettingsFrame(bool) {}
bool UI_SettingsBlocksConfigWrite() { return false; }
bool UI_SettingsStartup(std::string&) { return true; }
bool UI_SettingsInitializeDisplay(std::string&) { return true; }
bool UI_SettingsStartupActive() { return false; }
void UI_SettingsShutdown() {}
UI_SettingsRenderFrame::UI_SettingsRenderFrame() {}
UI_SettingsRenderFrame::~UI_SettingsRenderFrame() {}
void UI_SettingsRenderFrame::Submitting() {}
void UI_SettingsRenderFrame::Presented() {}
#else
#include "application/SystemSettingsHost.h"
#include "SettingsDisplayService.h"
#include <charconv>
#include <chrono>
#include <limits>
#include <memory>
#include <set>

namespace {
using namespace openq4::ui;
struct Service {
    SystemSettingsHost host;
    SettingsTransaction transaction{host};
    EngineSettingsDisplayHost device{host};
    SettingsDisplayController display{transaction,device};
    std::set<std::uint64_t> owners;
    std::set<std::uint64_t> confirmationOwners;
    std::map<std::uint64_t,SettingsResult> results;
    bool abandon = false;
    bool closing = false;
    std::uint64_t waitingOwner = 0;
    std::uint64_t receiptOwner = 0, receiptRequest = 0;
};
std::unique_ptr<Service>& Instance() { static std::unique_ptr<Service> service; return service; }
Service& Settings() { auto& service=Instance(); if (!service) service=std::make_unique<Service>(); return *service; }
std::uint64_t nextOwner = 1; // Survives game/renderer/service shutdown, never reused.
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
double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool NoArguments(const std::string& operation) {
    return operation == "settings.system.begin" || operation == "settings.system.defaults" ||
        operation == "settings.system.cancel" || operation == "settings.system.apply" ||
        operation == "settings.system.confirm" || operation == "settings.system.revert";
}
bool RequestToken(const std::string& value, std::uint64_t& token) {
    if (value.empty() || value.size()>20 || value.front()=='0') return false;
    const auto result=std::from_chars(value.data(),value.data()+value.size(),token);
    return result.ec==std::errc() && result.ptr==value.data()+value.size() && token!=0;
}
bool Supported(Service& service,std::uint64_t owner) {
    const auto effects=SystemSettingsHost::ChangedEffects(service.transaction.Baseline(),service.transaction.Draft());
    return !service.device.RecoveryActive() && (effects & ~unsigned(SystemSettingDisplayRestart))==0 &&
        (!effects || service.confirmationOwners.contains(owner));
}
const char* Message(SettingsCode code, SettingsPhase phase, bool dirty) {
    switch (code) {
        case SettingsCode::Busy: return "#str_229983";
        case SettingsCode::NotOpen: return "#str_229984";
        case SettingsCode::Invalid:
        case SettingsCode::ApplyFailed: return "#str_229985";
        case SettingsCode::Conflict: return "#str_229986";
        case SettingsCode::RollbackFailed: return "#str_229987";
        default: break;
    }
    if (phase == SettingsPhase::Confirming) return "#str_229988";
    return dirty ? "#str_229989" : "#str_229982";
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
void UI_SettingsFrame(bool allowWork) {
    auto& service = Settings();
    service.device.StartupFrame(Now(),allowWork);
    const auto owner = service.transaction.Owner();
    if (service.display.Active()) {
        const auto before=service.display.Stage(); const auto request=service.display.Request();
        service.display.Frame(Now(),service.owners.contains(service.display.Owner()) && !service.closing,allowWork);
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
        if (service.owners.contains(owner)) service.results[owner]=service.display.LastResult();
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
bool UI_SettingsInitializeDisplay(std::string& error) { return Settings().device.InitializeDisplay(error); }
bool UI_SettingsStartupActive() { return Settings().device.StartupActive(); }
void UI_SettingsShutdown() {
    auto& service=Instance(); if (service) { service->device.Shutdown(); service.reset(); }
}
void UI_SettingsConfirmationDocument(std::uint64_t owner, const DocumentModel& document) {
    auto& service=Settings(); service.confirmationOwners.erase(owner);
    if (!owner || !service.owners.contains(owner)) return;
    const auto& schema=UI_SettingsStateSchema();
    for (const char* key:{"settings.request","settings.confirmationVisible","settings.canConfirm","settings.canRevert","settings.canRetry","settings.remaining"}) {
        const auto declaration=document.state.find(key);
        if (declaration==document.state.end() || declaration->second.initial.index()!=schema.at(key) || !declaration->second.cvar.empty()) return;
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
        if (error.empty()) error = "System settings owner is unavailable";
        return false;
    }
    auto& transaction = service.transaction;
    SettingsResult result;
    if (service.device.StartupActive() || (!service.display.Active() && service.device.RecoveryActive())) {
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
            result = transaction.Begin(owner);
            if (result.code == SettingsCode::Ok) service.abandon = false;
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
    else if (action.operation == "settings.system.edit") result = transaction.Edit(owner,action.arguments);
    else if (action.operation == "settings.system.defaults") result = transaction.Defaults(owner);
    else if (action.operation == "settings.system.cancel") result = transaction.Cancel(owner);
    else if (action.operation == "settings.system.confirm") result = transaction.Confirm(owner);
    else if (action.operation == "settings.system.revert") result = transaction.Revert(owner);
    else if (action.operation == "settings.system.apply") {
        if (transaction.Owner() == owner && transaction.Phase() == SettingsPhase::Editing &&
            !Supported(service,owner))
            result = {SettingsCode::Invalid,"System settings batch requires unsupported effects or an owning confirmation view"};
        else if (SystemSettingsHost::ChangedRequiresDisplayRestart(transaction.Baseline(),transaction.Draft()))
            result=service.display.Apply(owner,Now());
        else result = transaction.Apply(owner,Now());
    }
    service.results[owner] = result;
    error = result.diagnostic;
    if (cvarSystem->GetCVarBool("ui_retainedTrace"))
        common->Printf("UI_SETTINGS operation=%s result=%d phase=%d owner=%llu dirty=%d\n",action.operation.c_str(),
            static_cast<int>(result.code),static_cast<int>(transaction.Phase()),static_cast<unsigned long long>(owner),
            transaction.Draft() != transaction.Baseline() ? 1 : 0);
    return result.code == SettingsCode::Ok;
}
const std::map<std::string,std::size_t>& UI_SettingsStateSchema() {
    static const auto schema = [] {
        std::map<std::string,std::size_t> result{{"settings.open",1},{"settings.dirty",1},
            {"settings.busy",1},{"settings.canApply",1},{"settings.message",2},{"settings.phase",0},
            {"settings.request",2},{"settings.canConfirm",1},{"settings.canRevert",1},{"settings.canRetry",1},{"settings.remaining",0},{"settings.confirmationVisible",1}};
        for (const auto& [key,type] : SystemSettingsHost::Schema()) {
            result.emplace("settings.draft."+key,type);
            result.emplace("settings.baseline."+key,type);
        }
        return result;
    }();
    return schema;
}
bool UI_SettingsRead(std::uint64_t owner, StateValues& values) {
    auto& service = Settings();
    if (!owner || !service.owners.contains(owner)) return false;
    const auto& transaction = service.transaction;
    const bool own = transaction.Owner() == owner;
    auto phase = own ? transaction.Phase() : SettingsPhase::Closed;
    if (own && service.display.Stage()==SettingsDisplayStage::Recovery) phase=SettingsPhase::RecoveryRequired;
    const bool dirty = own && transaction.Draft() != transaction.Baseline();
    const auto result = service.results.find(owner);
    const auto code = result == service.results.end() ? SettingsCode::Ok : result->second.code;
    StateValues candidate{{"settings.open",own},{"settings.dirty",dirty},
        {"settings.busy",(transaction.Owner() != 0 && !own) || (own && service.display.Active()) || service.device.StartupActive()},
        {"settings.canApply",own && phase == SettingsPhase::Editing && dirty && !service.display.Active() && Supported(service,owner)},
        {"settings.request",own && service.display.Active()?std::to_string(service.display.Request()):std::string()},
        {"settings.canConfirm",own && service.display.CanConfirm(Now())},
        {"settings.canRevert",own && service.display.CanRevert()},
        {"settings.canRetry",own && service.display.CanRetry()},
        {"settings.remaining",own?service.display.Remaining(Now()):0.0},
        {"settings.confirmationVisible",own && service.display.ConfirmationVisible()},
        {"settings.phase",static_cast<double>(phase)},
        {"settings.message",std::string(Message(code,phase,dirty))}};
    if (own) switch (service.display.Stage()) {
        case SettingsDisplayStage::QueuedApply: case SettingsDisplayStage::AwaitApply:
            candidate["settings.message"]=std::string("#str_229990"); break;
        case SettingsDisplayStage::QueuedRestore: case SettingsDisplayStage::AwaitRestore: case SettingsDisplayStage::FinalizeRestore:
            candidate["settings.message"]=std::string("#str_229991"); break;
        case SettingsDisplayStage::QueuedKeep: case SettingsDisplayStage::FinalizeKeep:
            candidate["settings.message"]=std::string("#str_229992"); break;
        case SettingsDisplayStage::Recovery:
            if (service.display.Approved()) candidate["settings.message"]=std::string("#str_229997"); break;
        default: break;
    }
    if (own) {
        for (const auto& [key,value] : transaction.Draft()) candidate.emplace("settings.draft."+key,value);
        for (const auto& [key,value] : transaction.Baseline()) candidate.emplace("settings.baseline."+key,value);
    }
    values = std::move(candidate); return true;
}
#endif
