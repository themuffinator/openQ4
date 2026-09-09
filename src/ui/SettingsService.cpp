// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#include "SettingsService.h"

#ifdef ID_DEDICATED
std::uint64_t UI_SettingsCreateOwner() { return 0; }
void UI_SettingsReleaseOwner(std::uint64_t) {}
void UI_SettingsCloseOwner(std::uint64_t) {}
void UI_SettingsFrame() {}
bool UI_SettingsBlocksConfigWrite() { return false; }
#else
#include "application/SystemSettingsHost.h"
#include <chrono>
#include <limits>
#include <set>

namespace {
using namespace openq4::ui;
struct Service {
    SystemSettingsHost host;
    SettingsTransaction transaction{host};
    std::set<std::uint64_t> owners;
    std::map<std::uint64_t,SettingsResult> results;
    std::uint64_t nextOwner = 1;
    bool abandon = false;
    bool closing = false;
    std::uint64_t waitingOwner = 0;
};
Service& Settings() { static Service service; return service; }
double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool NoArguments(const std::string& operation) {
    return operation == "settings.system.begin" || operation == "settings.system.defaults" ||
        operation == "settings.system.cancel" || operation == "settings.system.apply" ||
        operation == "settings.system.confirm" || operation == "settings.system.revert";
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
    if (!service.nextOwner) return 0; // Never recycle a stale owner token.
    const auto owner = service.nextOwner++;
    service.owners.insert(owner);
    return owner;
}
void UI_SettingsReleaseOwner(std::uint64_t owner) {
    auto& service = Settings();
    UI_SettingsCloseOwner(owner);
    service.owners.erase(owner); service.results.erase(owner);
}
void UI_SettingsCloseOwner(std::uint64_t owner) {
    auto& service = Settings();
    if (service.waitingOwner == owner) service.waitingOwner = 0;
    if (!owner || service.transaction.Owner() != owner) return;
    if (service.transaction.Phase() == SettingsPhase::Editing) {
        // Discarding a draft performs no host read or write, and allows a new
        // owner to open during the same Session lifecycle transition.
        service.transaction.Abandon(owner); service.abandon = service.closing = false;
    } else service.abandon = service.closing = true;
}
void UI_SettingsFrame() {
    auto& service = Settings();
    const auto owner = service.transaction.Owner();
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
    const auto phase = Settings().transaction.Phase();
    return phase == SettingsPhase::Confirming || phase == SettingsPhase::RecoveryRequired;
}
bool UI_SettingsOperation(const Action& action, std::string& error) {
    if (NoArguments(action.operation) && action.arguments.empty()) return true;
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
    if (action.operation == "settings.system.begin") {
        if (service.closing && transaction.Owner()) {
            // A failed close may outlive its GUI. An explicit open can request
            // one recovery attempt on the engine frame; it never steals the
            // old baseline or overwrites externally changed values. Success
            // opens the waiting owner; failure remains visible until retried.
            if (!service.waitingOwner || service.waitingOwner == owner) {
                service.waitingOwner = owner; service.abandon = true;
            }
            result = {SettingsCode::Busy,"Settings owner recovery is pending"};
        } else {
            result = transaction.Begin(owner);
            if (result.code == SettingsCode::Ok) service.abandon = false;
        }
    }
    else if (action.operation == "settings.system.edit") result = transaction.Edit(owner,action.arguments);
    else if (action.operation == "settings.system.defaults") result = transaction.Defaults(owner);
    else if (action.operation == "settings.system.cancel") result = transaction.Cancel(owner);
    else if (action.operation == "settings.system.confirm") result = transaction.Confirm(owner);
    else if (action.operation == "settings.system.revert") result = transaction.Revert(owner);
    else if (action.operation == "settings.system.apply") {
        // A CVar readback is not a device result. Until the typed restart /
        // image/audio recovery route is connected, reject that complete batch
        // before any live write. The draft and normal immediate settings still
        // use the production transaction, without pretending a restart worked.
        if (transaction.Owner() == owner && transaction.Phase() == SettingsPhase::Editing &&
            service.host.RequiresDeviceWork(transaction.Baseline(),transaction.Draft()))
            result = {SettingsCode::Invalid,"System settings batch requires device work without a qualified result route"};
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
            {"settings.busy",1},{"settings.canApply",1},{"settings.message",2},{"settings.phase",0}};
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
    const auto phase = own ? transaction.Phase() : SettingsPhase::Closed;
    const bool dirty = own && transaction.Draft() != transaction.Baseline();
    const auto result = service.results.find(owner);
    const auto code = result == service.results.end() ? SettingsCode::Ok : result->second.code;
    StateValues candidate{{"settings.open",own},{"settings.dirty",dirty},
        {"settings.busy",transaction.Owner() != 0 && !own},
        {"settings.canApply",own && phase == SettingsPhase::Editing && dirty &&
            !service.host.RequiresDeviceWork(transaction.Baseline(),transaction.Draft())},
        {"settings.phase",static_cast<double>(phase)},
        {"settings.message",std::string(Message(code,phase,dirty))}};
    if (own) {
        for (const auto& [key,value] : transaction.Draft()) candidate.emplace("settings.draft."+key,value);
        for (const auto& [key,value] : transaction.Baseline()) candidate.emplace("settings.baseline."+key,value);
    }
    values = std::move(candidate); return true;
}
#endif
