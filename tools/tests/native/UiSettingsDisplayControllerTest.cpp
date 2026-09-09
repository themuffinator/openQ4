// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/application/SettingsDisplayController.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace openq4::ui;
namespace {
int checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
void Code(const SettingsResult& result, SettingsCode expected, const char* message) {
    if (result.code != expected) std::fprintf(stderr,"Result %d expected %d: %s\n",
        static_cast<int>(result.code),static_cast<int>(expected),result.diagnostic.c_str());
    Check(result.code == expected,message);
}
StateValues Initial() { return {{"width",1280.0},{"samples",0.0},{"volume",0.5},{"label",std::string("baseline")}}; }
struct Boundary {
    std::vector<std::string> trace;
    bool journal = false;
};
struct Storage final : SettingsHost {
    Boundary& boundary;
    StateValues live = Initial();
    int writes = 0, reads = 0;
    bool readOkay = true, validateOkay = true;
    std::function<bool(const StateValues&,std::string&)> writeHook;
    explicit Storage(Boundary& boundary) : boundary(boundary) {}
    bool Read(StateValues& out, std::string& error) override {
        ++reads; boundary.trace.push_back("read");
        if (!readOkay) { error = "injected read refusal"; return false; }
        out = live; return true;
    }
    bool Defaults(StateValues& out, std::string&) override { out = Initial(); return true; }
    bool Validate(const StateValues&, const StateValues& candidate, std::string& error) override {
        if (!validateOkay || candidate.size() != Initial().size() ||
            std::get<double>(candidate.at("width")) < 640 || std::get<double>(candidate.at("samples")) < 0) {
            error = "injected validation refusal"; return false;
        }
        return true;
    }
    bool Write(const StateValues& patch, std::string& error) override {
        ++writes; boundary.trace.push_back("write");
        Check(boundary.journal,"every real transaction write follows journal preparation");
        if (writeHook) return writeHook(patch,error);
        for (const auto& [key,value] : patch) live.at(key) = value;
        return true;
    }
    bool NeedsConfirmation(const StateValues&, const StateValues&) const override { return true; }
};
struct Display final : SettingsDisplayHost {
    Boundary& boundary;
    SettingsDisplayController* controller = nullptr;
    SettingsDisplayObservation current{1,1,10,10,0,true,false,true,false};
    int preparations = 0, cancellations = 0, applies = 0, restores = 0, observations = 0, persists = 0, finishes = 0;
    bool prepareOkay = true, cancelOkay = true, applyOkay = true, restoreOkay = true;
    bool observeOkay = true, persistOkay = true, finishOkay = true;
    std::function<void()> prepareHook, restartHook, persistHook, finishHook;
    explicit Display(Boundary& boundary) : boundary(boundary) {}
    bool Prepare(const SettingsAttempt& attempt, std::string& error) override {
        ++preparations; boundary.trace.push_back("prepare");
        Check(attempt.owner && attempt.request && !attempt.patch.empty(),"host receives immutable identified attempt");
        boundary.journal = true; // False may still have published durable evidence.
        if (prepareHook) prepareHook();
        if (!prepareOkay) error = "injected uncertain journal preparation";
        return prepareOkay;
    }
    bool CancelPreparation(std::string& error) override {
        ++cancellations; boundary.trace.push_back("cancel");
        if (!cancelOkay) { error = "injected journal cancellation failure"; return false; }
        boundary.journal = false; return true;
    }
    bool Restart(bool restoring, SettingsDisplayObservation& out, std::string& error) override {
        Check(boundary.journal,"device restart retains durable journal");
        ++(restoring ? restores : applies); boundary.trace.push_back(restoring ? "restore-device" : "apply-device");
        ++current.generation; current.ready = restoring ? restoreOkay : applyOkay;
        out = current;
        if (restartHook) restartHook();
        if (!current.ready) error = "injected renderer restart failure";
        return current.ready;
    }
    bool Observe(bool restoring, SettingsDisplayObservation& out, std::string& error) override {
        ++observations; boundary.trace.push_back(restoring ? "observe-restore" : "observe-apply");
        out = current;
        if (!observeOkay) error = "injected actual tuple mismatch";
        return observeOkay;
    }
    bool PersistConfirmation(const SettingsAttempt& attempt, std::string& error) override {
        ++persists; boundary.trace.push_back("persist");
        Check(boundary.journal && attempt.owner && attempt.request,"Keep persistence retains identified journal");
        if (persistHook) persistHook();
        if (!persistOkay) error = "injected uncertain Confirmed/config publication";
        return persistOkay;
    }
    bool Finish(bool restoring, std::string& error) override {
        ++finishes; boundary.trace.push_back(restoring ? "finish-restore" : "finish-keep");
        Check(controller && controller->Active() && boundary.journal,"configuration remains guarded until journal cleanup completes");
        if (finishHook) finishHook();
        if (!finishOkay) { error = "injected journal removal failure"; return false; }
        boundary.journal = false; return true;
    }
    void Present() { ++current.submitted; ++current.presented; }
};
struct Fixture {
    static constexpr std::uint64_t owner = 17;
    Boundary boundary;
    Storage storage{boundary};
    SettingsTransaction transaction{storage};
    Display display{boundary};
    SettingsDisplayController controller{transaction,display};
    Fixture() {
        display.controller = &controller;
        Code(transaction.Begin(owner),SettingsCode::Ok,"open owner transaction");
        Code(transaction.Edit(owner,{{"width",960.0},{"samples",4.0}}),SettingsCode::Ok,"stage display edit");
        boundary.trace.clear();
    }
    void Queue(double now = 0) {
        Code(controller.Apply(owner,now),SettingsCode::Ok,"queue display apply");
        Check(controller.Active() && controller.Stage() == SettingsDisplayStage::QueuedApply &&
            transaction.Phase() == SettingsPhase::Applying,"queued apply retains transaction and config guard");
        Check(storage.writes == 0 && display.preparations == 0 && display.applies == 0,"Apply action executes no effects");
    }
    void Execute(double now = 0) {
        Queue(now); controller.Frame(now,true);
        Check(controller.Stage() == SettingsDisplayStage::AwaitApply,"restart awaits owning view presentation");
    }
    void Confirming(double now = 1) {
        Execute(0); controller.OwnerDrawn(owner,controller.Request()); display.Present(); controller.Frame(now,true);
        Check(controller.CanConfirm(now) && transaction.Phase() == SettingsPhase::Confirming,"fresh owning-view present enables confirmation");
    }
    void RestoreDone(double now = 3) {
        controller.Frame(now,true);
        Check(controller.Stage() == SettingsDisplayStage::AwaitRestore,"restore waits for first restored presentation");
        display.Present(); controller.Frame(now+1,true);
        Check(!controller.Active() && !transaction.AsyncPending() && !boundary.journal,"verified restore releases all ownership");
    }
};

void OrderingAndIdentity() {
    Fixture f;
    Code(f.controller.Apply(0,0),SettingsCode::Invalid,"zero owner rejected");
    Code(f.controller.Apply(19,0),SettingsCode::Busy,"different owner rejected");
    Check(!f.controller.Active(),"bad owner does not claim controller");
    f.Queue(); const auto request = f.controller.Request();
    Code(f.controller.Apply(f.owner,0),SettingsCode::Busy,"duplicate apply cannot replay");
    f.controller.Frame(0,true,false);
    Check(f.display.preparations == 0 && f.storage.writes == 0,"nested frame cannot prepare journal or write");
    f.controller.Frame(0,true);
    const auto& t = f.boundary.trace;
    Check(std::find(t.begin(),t.end(),"prepare") < std::find(t.begin(),t.end(),"write") &&
        std::find(t.begin(),t.end(),"write") < std::find(t.begin(),t.end(),"apply-device"),"journal then patch then device ordering");
    Check(!f.controller.CanConfirm(0) && f.controller.ConfirmationVisible(),"visible confirmation cannot yet be accepted");
    f.display.Present(); f.controller.Frame(1,true);
    Check(!f.controller.CanConfirm(1),"a scene present without owning confirmation draw is insufficient");
    f.controller.OwnerDrawn(99,request); f.controller.OwnerDrawn(f.owner,request+1);
    f.display.Present(); f.controller.Frame(2,true);
    Check(!f.controller.CanConfirm(2),"stale draw notifications cannot enable Keep");
    f.controller.OwnerDrawn(f.owner,request); f.controller.Frame(3,true);
    Check(!f.controller.CanConfirm(3),"draw without subsequent API-present is insufficient");
    ++f.display.current.submitted; f.controller.Frame(4,true);
    Check(!f.controller.CanConfirm(4),"nonpresent screenshot submission is insufficient");
    ++f.display.current.presented; f.controller.Frame(5,true);
    Check(f.controller.CanConfirm(5) && f.controller.Remaining(5) == 15,"deadline begins after owning draw and first present");
    Code(f.controller.Keep(99,request,5),SettingsCode::Busy,"stale Keep owner rejected");
    Code(f.controller.Keep(f.owner,request+1,5),SettingsCode::Busy,"stale Keep request rejected");
    Code(f.controller.Revert(99,request),SettingsCode::Busy,"stale restore owner rejected");
    f.controller.Close(99); Check(!f.controller.Closing(),"different owner close is isolated");
    Code(f.controller.Keep(f.owner,request,5),SettingsCode::Ok,"owning Keep queues work");
    Check(f.display.persists == 0 && f.controller.Active(),"Keep action does not persist inline");
    f.controller.Frame(5,true,false); Check(f.display.persists == 0,"nested frame cannot persist Keep");
    f.controller.Frame(5,true);
    Check(!f.controller.Active() && f.display.persists == 1 && f.display.finishes == 1 &&
        f.transaction.Phase() == SettingsPhase::Editing && f.transaction.Baseline() == f.storage.live,"Keep completes exactly once after persistence");
    f.controller.Frame(6,true); Check(f.display.persists == 1 && f.storage.writes == 1,"idle frames cannot replay actions");
    Code(f.controller.Keep(f.owner,request,6),SettingsCode::Busy,"completed request is stale");
}

void PreparationAndClose() {
    { Fixture f; Code(f.transaction.Edit(f.owner,Initial()),SettingsCode::Ok,"stage no-op");
      Code(f.controller.Apply(f.owner,0),SettingsCode::Ok,"no-op apply succeeds");
      Check(!f.controller.Active() && !f.transaction.AsyncPending() && f.display.preparations == 0,"no-op creates no journal"); }
    { Fixture f; f.Queue(); f.controller.Close(f.owner); f.controller.Frame(0,true);
      Check(!f.controller.Active() && f.transaction.Phase() == SettingsPhase::Closed && f.storage.writes == 0 &&
        f.display.preparations == 0,"closing queued apply abandons without any write or journal"); }
    { Fixture f; f.Queue(); f.display.prepareOkay = false; f.controller.Frame(0,true);
      Check(f.controller.Stage() == SettingsDisplayStage::Recovery && f.boundary.journal && f.storage.writes == 0,
        "uncertain journal preparation blocks before all settings writes");
      f.controller.Frame(1,true); f.controller.Frame(2,true);
      Check(f.display.preparations == 1 && f.display.cancellations == 0,"preparation failure has no blind retry");
      f.display.cancelOkay = false; Code(f.controller.Revert(f.owner,f.controller.Request()),SettingsCode::Ok,"explicit preparation recovery");
      f.controller.Frame(3,true); Check(f.controller.Active() && f.transaction.AsyncPending(),"failed preparation cleanup retains ownership");
      f.display.cancelOkay = true; Code(f.controller.Revert(f.owner,f.controller.Request()),SettingsCode::Ok,"retry exact preparation cleanup");
      f.controller.Frame(4,true); Check(!f.controller.Active() && f.transaction.Phase() == SettingsPhase::Editing &&
        f.storage.writes == 0 && !f.boundary.journal,"unwritten preparation cancellation returns editable draft"); }
    { Fixture f; f.Confirming(); f.controller.Close(f.owner); f.RestoreDone();
      Check(f.transaction.Phase() == SettingsPhase::Closed && f.storage.live == Initial(),"closing confirmation restores and closes owner"); }
    { Fixture f; f.Execute(); f.controller.Frame(1,false,false);
      Check(f.controller.Stage() == SettingsDisplayStage::QueuedRestore && f.storage.writes == 1,"nested owner loss queues but does not execute recovery");
      f.RestoreDone(); Check(f.transaction.Phase() == SettingsPhase::Closed,"lost owner closes after verified restoration"); }
}

void PresentationAndDeadlines() {
    for (int defect = 0; defect < 7; ++defect) {
        Fixture f; f.Execute(); f.controller.OwnerDrawn(f.owner,f.controller.Request()); f.display.Present();
        switch (defect) {
        case 0: ++f.display.current.epoch; break;
        case 1: ++f.display.current.generation; break;
        case 2: ++f.display.current.failures; break;
        case 3: f.display.current.ready = false; break;
        case 4: f.display.current.submitted = 0; break;
        case 5: f.display.current.presented = 0; break;
        case 6: f.display.observeOkay = false; break;
        }
        f.controller.Frame(1,true,false);
        Check(f.controller.Stage() == SettingsDisplayStage::QueuedRestore && !f.controller.CanConfirm(1),
            "wrong epoch/generation/failure/readiness/counters/actual tuple never confirms");
        Check(f.display.restores == 0,"nested frame queues identity failure recovery only");
    }
    for (const bool minimized : {false,true}) {
        Fixture f; f.Confirming(); f.display.current.focused = minimized; f.display.current.minimized = minimized;
        f.controller.Frame(2,true,false);
        Check(f.controller.Stage() == SettingsDisplayStage::QueuedRestore,"visible focus loss/minimization queues recovery");
    }
    { Fixture f; f.Execute(); f.display.current.hidden = true; f.display.current.focused = false;
      f.controller.OwnerDrawn(f.owner,f.controller.Request()); f.display.Present(); f.controller.Frame(1,true);
      Check(f.controller.CanConfirm(1),"explicit hidden harness observation does not require OS focus"); }
    { Fixture f; f.Queue(); f.controller.Frame(20,true,false);
      Check(f.display.preparations == 0 && f.storage.writes == 0,"expired queued request never writes in nested frame");
      f.controller.Frame(20,true); f.controller.Frame(21,true);
      Check(!f.controller.Active() && f.storage.writes == 0,"queued request expiry cancels without device work"); }
    for (const double now : {20.0,21.0}) {
        Fixture f; f.Execute(); f.controller.OwnerDrawn(f.owner,f.controller.Request()); f.display.Present();
        f.controller.Frame(now,true,false);
        Check(f.controller.Stage() == SettingsDisplayStage::QueuedRestore && !f.controller.CanConfirm(now),
            "first present at or after readiness deadline cannot start confirmation");
    }
    { Fixture f; f.Confirming(); const auto request = f.controller.Request();
      Code(f.controller.Keep(f.owner,request,16),SettingsCode::Busy,"Keep rejects exact confirmation deadline");
      f.controller.Frame(16,true,false); Check(f.controller.Stage() == SettingsDisplayStage::QueuedRestore && f.display.restores == 0,
        "nested frame timeout queues recovery without restart");
      f.RestoreDone(17); Check(f.storage.live == Initial(),"confirmation timeout restores exact original settings"); }
    { Fixture f; f.Confirming(); Code(f.controller.Keep(f.owner,f.controller.Request(),15.9),SettingsCode::Ok,"Keep just before deadline queues");
      f.controller.Frame(16,true,false); Check(f.controller.Stage() == SettingsDisplayStage::QueuedRestore && f.display.persists == 0,
        "expired queued Keep cannot persist after deadline"); }
    { Fixture f; f.Confirming(); Code(f.controller.Revert(f.owner,f.controller.Request()),SettingsCode::Ok,"queue restore deadline test");
      f.controller.Frame(2,true); f.display.Present(); f.controller.Frame(22,true,false);
      Check(f.controller.Stage() == SettingsDisplayStage::Recovery && f.controller.Active() && f.display.finishes == 0,
        "first restored present at deadline does not silently finish recovery"); }
    { Fixture f; f.Execute(); f.controller.Frame(std::numeric_limits<double>::quiet_NaN(),true,false);
      Check(f.controller.Stage() == SettingsDisplayStage::QueuedRestore && f.controller.LastResult().code == SettingsCode::Invalid,
        "invalid clock queues recovery"); }
    { Fixture f; Code(f.controller.Apply(f.owner,std::numeric_limits<double>::infinity()),SettingsCode::Invalid,"nonfinite Apply rejected");
      Check(!f.controller.Active() && !f.transaction.AsyncPending(),"invalid initial clock leaves no pending operation"); }
}

void PartialFailureAndRecovery() {
    { Fixture f; f.Queue(); f.storage.writeHook = [&](const StateValues& patch,std::string& error) {
        f.storage.live["width"] = patch.at("width"); error = "partial write"; return false;
      }; f.controller.Frame(0,true);
      Check(f.controller.Stage() == SettingsDisplayStage::QueuedRestore && f.display.applies == 0 && f.boundary.journal,
        "partial CVar apply does not restart or discard journal");
      f.storage.writeHook = {}; f.RestoreDone();
      Check(f.storage.live == Initial() && std::get<double>(f.transaction.Draft().at("width")) == 960 &&
        f.controller.LastResult().code == SettingsCode::ApplyFailed,"successful apply-failure restoration preserves attempted draft and error"); }
    { Fixture f; f.Queue(); f.display.applyOkay = false; f.controller.Frame(0,true);
      Check(f.controller.Stage() == SettingsDisplayStage::QueuedRestore && f.storage.writes == 1,"failed renderer apply queues explicit restoration");
      f.RestoreDone(); Check(f.display.applies == 1 && f.display.restores == 1,"failed apply uses one separate restoring restart"); }
    { Fixture f; f.Confirming(); f.storage.live["width"] = 800.0; f.storage.live["volume"] = 0.8;
      Code(f.controller.Revert(f.owner,f.controller.Request()),SettingsCode::Ok,"queue conflicting restoration");
      f.controller.Frame(2,true);
      Check(f.controller.Stage() == SettingsDisplayStage::Recovery && f.controller.LastResult().code == SettingsCode::Conflict &&
        std::get<double>(f.storage.live.at("width")) == 800 && std::get<double>(f.storage.live.at("samples")) == 0 &&
        std::get<double>(f.storage.live.at("volume")) == 0.8 && f.display.restores == 0,"conflict restores only owned keys and blocks device success");
      const auto writes = f.storage.writes; f.controller.Frame(3,true); f.controller.Frame(4,true);
      Check(f.storage.writes == writes,"conflicted recovery has no automatic destructive retry");
      f.storage.live["width"] = 960.0;
      Code(f.controller.Revert(f.owner,f.controller.Request()),SettingsCode::Ok,"explicit retry after resolving external conflict");
      f.RestoreDone(5); Check(std::get<double>(f.transaction.Baseline().at("volume")) == 0.8,"restoration preserves unrelated external values"); }
    { Fixture f; f.Confirming(); f.display.restoreOkay = false;
      Code(f.controller.Revert(f.owner,f.controller.Request()),SettingsCode::Ok,"queue renderer restore refusal");
      f.controller.Frame(2,true); Check(f.controller.Stage() == SettingsDisplayStage::Recovery && f.controller.Active(),"failed device restore retains config guard");
      f.controller.Frame(3,true); Check(f.display.restores == 1,"failed renderer restore does not retry blindly");
      f.display.restoreOkay = true; Code(f.controller.Revert(f.owner,f.controller.Request()),SettingsCode::Ok,"explicit renderer restore retry");
      f.RestoreDone(4); Check(f.display.restores == 2,"retry uses a new verified restoring generation"); }
    { Fixture f; f.Confirming(); Code(f.controller.Revert(f.owner,f.controller.Request()),SettingsCode::Ok,"queue partial restore");
      f.storage.writeHook = [&](const StateValues& patch,std::string& error) {
        f.storage.live["width"] = patch.at("width"); error = "partial restore"; return false;
      }; f.controller.Frame(2,true);
      Check(f.controller.Stage() == SettingsDisplayStage::Recovery && f.display.restores == 0,"partial restore cannot complete or restart");
      f.storage.writeHook = {}; Code(f.controller.Revert(f.owner,f.controller.Request()),SettingsCode::Ok,"explicit partial restoration retry");
      f.RestoreDone(3); Check(f.storage.live == Initial(),"partial restore retries remaining owned keys"); }
}

void KeepUncertaintyAndFinish() {
    { Fixture f; f.Confirming(); const auto prior = f.controller.Request();
      f.display.persistOkay = false; Code(f.controller.Keep(f.owner,prior,2),SettingsCode::Ok,"queue uncertain Keep");
      f.controller.Frame(2,true); const auto confirmation = f.controller.Request();
      Check(confirmation != prior && f.controller.Stage() == SettingsDisplayStage::Recovery && f.controller.Active() &&
        f.transaction.AsyncPending() && f.display.persists == 1,"uncertain confirmation retains new request and config guard");
      f.controller.Close(f.owner); f.controller.Frame(30,false);
      Check(f.display.restores == 0 && f.display.persists == 1,"uncertain Keep cannot roll back on timeout/close or blindly republish");
      Code(f.controller.Revert(f.owner,prior),SettingsCode::Busy,"old request cannot recover current confirmation");
      f.display.persistOkay = true; Code(f.controller.Revert(f.owner,confirmation),SettingsCode::Busy,"Revert must never commit a previously approved Keep");
      Check(f.controller.Approved() && !f.controller.CanRevert() && f.controller.CanRetry(),"uncertain Keep exposes Retry instead of Revert");
      Code(f.controller.Retry(f.owner,confirmation),SettingsCode::Ok,"explicit uncertain Keep recovery queues finalization");
      f.controller.Frame(31,false);
      Check(!f.controller.Active() && f.transaction.Phase() == SettingsPhase::Closed && f.display.persists == 2 &&
        f.display.restores == 0 && std::get<double>(f.storage.live.at("width")) == 960,"uncertain Keep retry finalizes target despite expired countdown"); }
    for (const bool restoring : {false,true}) {
        Fixture f; f.Confirming(); f.display.finishOkay = false;
        if (restoring) { Code(f.controller.Revert(f.owner,f.controller.Request()),SettingsCode::Ok,"queue restore finalization");
            f.controller.Frame(2,true); f.display.Present(); f.controller.Frame(3,true); }
        else { Code(f.controller.Keep(f.owner,f.controller.Request(),2),SettingsCode::Ok,"queue Keep finalization"); f.controller.Frame(2,true); }
        Check(f.controller.Stage() == SettingsDisplayStage::Recovery && f.controller.Active() && !f.transaction.AsyncPending() &&
            f.boundary.journal,"cleanup failure guards config after typed transaction completion");
        const auto writes = f.storage.writes, restarts = f.display.applies+f.display.restores, persists = f.display.persists;
        f.controller.Frame(4,true); Check(f.display.finishes == 1,"failed cleanup does not retry every frame");
        f.display.finishOkay = true; Code(f.controller.Retry(f.owner,f.controller.Request()),SettingsCode::Ok,"explicit cleanup retry");
        f.controller.Frame(5,true);
        Check(!f.controller.Active() && f.display.finishes == 2 && f.storage.writes == writes &&
            f.display.applies+f.display.restores == restarts && f.display.persists == persists,"cleanup retry never replays writes, restart or persistence");
    }
    { Fixture f; f.Confirming(); f.display.persistHook = [&] { f.storage.live["width"] = 800.0; };
      Code(f.controller.Keep(f.owner,f.controller.Request(),2),SettingsCode::Ok,"queue confirmation readback conflict");
      f.controller.Frame(2,true); Check(f.controller.Stage() == SettingsDisplayStage::Recovery && f.controller.Active() &&
        f.transaction.AsyncPending() && f.display.finishes == 0,"changes during persistence cannot silently rebase transaction");
      f.controller.Frame(3,true); Check(f.display.restores == 0,"post-persistence conflict never blindly restores old settings"); }
}

void CallbackClose() {
    Fixture f; f.Queue();
    f.display.prepareHook = [&] {
        Code(f.controller.Keep(f.owner,f.controller.Request(),0),SettingsCode::Busy,"host callback cannot reenter Keep");
        Code(f.controller.Revert(f.owner,f.controller.Request()),SettingsCode::Busy,"host callback cannot reenter recovery");
        f.controller.Close(f.owner);
    };
    f.controller.Frame(0,true);
    Check(f.storage.writes == 0 && f.display.applies == 0,"owner close during journal preparation prevents subsequent settings/device apply");
    f.controller.Frame(1,false);
    Check(!f.controller.Active() && f.transaction.Phase() == SettingsPhase::Closed && !f.boundary.journal,
        "callback close cancels prepared journal and closes owner");
    { Fixture restarted; restarted.Queue(); restarted.display.restartHook = [&] { restarted.controller.Close(restarted.owner); };
      restarted.controller.Frame(0,true);
      Check(restarted.controller.Stage() == SettingsDisplayStage::QueuedRestore && restarted.controller.Closing(),
        "owner close during device reconstruction cannot be overwritten by AwaitApply");
      restarted.display.restartHook = {}; restarted.RestoreDone(1);
      Check(restarted.transaction.Phase() == SettingsPhase::Closed,"close during restart restores and closes exactly once"); }
}

void CrossInstanceAndRestoreObservation() {
    { Fixture first, second; first.Execute(); second.Execute();
      const auto one = first.controller.Request(), two = second.controller.Request();
      Check(one != two,"independent controllers receive distinct process-wide request identities");
      first.controller.OwnerDrawn(first.owner,two); first.display.Present(); first.controller.Frame(1,true);
      Check(!first.controller.CanConfirm(1),"peer request cannot confirm another instance even with same owner token");
      second.controller.OwnerDrawn(second.owner,two); second.display.Present(); second.controller.Frame(1,true);
      Check(second.controller.CanConfirm(1) && first.controller.Stage() == SettingsDisplayStage::AwaitApply,
        "independent instance progresses without changing peer"); }
    for (int defect = 0; defect < 4; ++defect) {
        Fixture f; f.Confirming(); const auto old = f.controller.Request();
        Code(f.controller.Revert(f.owner,old),SettingsCode::Ok,"queue restore observation test");
        f.controller.Frame(2,true); const auto restore = f.controller.Request();
        Check(restore != old,"restoration uses a fresh request identity");
        Code(f.controller.Revert(f.owner,old),SettingsCode::Busy,"old apply request cannot operate restoring instance");
        f.display.Present();
        if (defect == 0) ++f.display.current.epoch;
        if (defect == 1) ++f.display.current.generation;
        if (defect == 2) ++f.display.current.failures;
        if (defect == 3) f.display.observeOkay = false;
        f.controller.Frame(3,true,false);
        Check(f.controller.Stage() == SettingsDisplayStage::Recovery && f.display.finishes == 0 && f.transaction.AsyncPending(),
            "invalid restoration identity/failure/actual tuple cannot clear journal or typed ownership");
        const auto restarts = f.display.restores; f.controller.Frame(4,true);
        Check(f.display.restores == restarts,"failed restoration observation requires explicit recovery");
    }
    { Fixture f; f.Confirming(); const double nan = std::numeric_limits<double>::quiet_NaN();
      Check(!f.controller.CanConfirm(nan) && f.controller.Remaining(nan) == 0,"invalid read-only countdown time is unavailable");
      f.controller.Frame(0.5,true,false);
      Check(f.controller.Stage() == SettingsDisplayStage::QueuedRestore && f.controller.LastResult().code == SettingsCode::Invalid,
        "backwards clock queues recovery without effect recursion"); }
}

void ThrowingBoundaries() {
    { Fixture f; f.Queue(); f.display.prepareHook = [] { throw std::runtime_error("prepare exception"); };
      bool escaped = false; try { f.controller.Frame(0,true); } catch (...) { escaped = true; }
      Check(!escaped && f.controller.Stage() == SettingsDisplayStage::Recovery && f.boundary.journal && f.storage.writes == 0,
        "throwing preparation retains uncertain journal in explicit recovery");
      f.controller.Frame(1,true); Check(f.display.preparations == 1,"throwing preparation cannot blindly retry on next frame"); }
    { Fixture f; f.Confirming(); f.display.persistHook = [] { throw std::runtime_error("persist exception"); };
      Code(f.controller.Keep(f.owner,f.controller.Request(),2),SettingsCode::Ok,"queue throwing persistence");
      bool escaped = false; try { f.controller.Frame(2,true); } catch (...) { escaped = true; }
      Check(!escaped && f.controller.Stage() == SettingsDisplayStage::Recovery && f.controller.Active(),
        "throwing persistence retains uncertain Keep intent and configuration guard");
      f.controller.Frame(3,true); Check(f.display.persists == 1 && f.display.restores == 0,"throwing Keep cannot blindly persist or restore"); }
}
} // namespace

int main() {
    OrderingAndIdentity(); PreparationAndClose(); PresentationAndDeadlines();
    PartialFailureAndRecovery(); KeepUncertaintyAndFinish(); CallbackClose();
    CrossInstanceAndRestoreObservation(); ThrowingBoundaries();
    std::printf("UiSettingsDisplayControllerTest passed: %d checks\n",checks);
}
