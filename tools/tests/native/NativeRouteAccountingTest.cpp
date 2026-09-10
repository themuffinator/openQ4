// Actual fixed-pass ledger and failed-ingress ownership, with counted sources.
#define main SerialDispositionMain
#include "NativeEventDispositionTest.cpp"
#undef main
#include <cstring>

template<class T> concept HasSuccessfulReceipt = requires(const T& value) { value.Receipt(); };
static_assert(!HasSuccessfulReceipt<NativeQueueQuarantine>);

struct PlannedFixture : Fixture {
    NativeDispositionTicket firstKey{}, firstPoll{}, deferred{}, secondKey{}, secondPoll{}, mouse{};
    std::vector<NativeDispositionTicket> wheel;
    PlannedFixture() : Fixture(true) { CHECK(ledger.BeginPlanned(ingress,source,*batch,error)); }
    explicit PlannedFixture(NativeDispositionSchedule schedule) : Fixture(true) {
        CHECK(ledger.BeginPlanned(ingress,source,*batch,schedule,error));
    }
    NativeDispositionTicket Plan(NativeDispositionRecord record,NativeDispositionPass pass,NativeDispositionTicket parent={}) {
        NativeDispositionTicket ticket; CHECK(ledger.Issue(record,{pass,parent},ticket,error)); return ticket;
    }
    NativeDispositionAdmission Admit(NativeDispositionTicket ticket) {
        NativeDispositionAdmission receipt; CHECK(ledger.Admit(ticket,receipt,error));
        CHECK(receipt.ticket==ticket && receipt.serial); return receipt;
    }
    void Translate() {
        auto record=Open(0);
        firstKey=Plan(record,NativeDispositionPass::SessionInitial); firstPoll=Plan(record,NativeDispositionPass::KeyboardPoll);
        deferred=Plan(record,NativeDispositionPass::SessionDeferred,firstPoll);
        Admit(firstKey); Admit(firstPoll); CHECK(ledger.SealRecord(record,NativeRecordDisposition::Emitted,error));
        record=Open(1); CHECK(ledger.SealRecord(record,NativeRecordDisposition::Sentinel,error));
        record=Open(2); secondKey=Plan(record,NativeDispositionPass::SessionInitial); secondPoll=Plan(record,NativeDispositionPass::KeyboardPoll);
        Admit(secondKey);
        Admit(secondPoll); CHECK(ledger.SealRecord(record,NativeRecordDisposition::Emitted,error));
        record=Open(3); CHECK(ledger.SealRecord(record,NativeRecordDisposition::Ignored,error));
        record=Open(4);
        for(unsigned i=0;i<6;++i) { wheel.push_back(Plan(record,NativeDispositionPass::SessionInitial)); Admit(wheel.back()); }
        mouse=Plan(record,NativeDispositionPass::MousePoll); Admit(mouse);
        CHECK(ledger.SealRecord(record,NativeRecordDisposition::Emitted,error));
        record=Open(5); CHECK(ledger.SealRecord(record,NativeRecordDisposition::Fence,error));
    }
    void Initial() {
        Deliver(firstKey); Deliver(secondKey); for(auto ticket:wheel) Deliver(ticket);
        CHECK(ledger.CompletePass(NativeDispositionPass::SessionInitial,error));
    }
    void Mouse() { Deliver(mouse); CHECK(ledger.CompletePass(NativeDispositionPass::MousePoll,error)); }
};
static void SinkOrder() {
    PlannedFixture f; f.Translate(); CHECK(f.ledger.SealTranslation(f.error));
    std::vector<unsigned> actual;
    f.Deliver(f.firstKey); actual.push_back(1); f.Deliver(f.secondKey); actual.push_back(2);
    for(auto ticket:f.wheel)f.Deliver(ticket);
    CHECK(f.ledger.CompletePass(NativeDispositionPass::SessionInitial,f.error));
    f.Deliver(f.mouse); actual.push_back(3); CHECK(f.ledger.CompletePass(NativeDispositionPass::MousePoll,f.error));
    CHECK(f.ledger.BeginDelivery(f.firstPoll,f.error));
    auto admitted=f.Admit(f.deferred); CHECK(admitted.ticket.record==f.firstPoll.record);
    actual.push_back(4); CHECK(f.ledger.CompleteDelivery(f.firstPoll,NativeDeliveryDisposition::Delivered,f.error));
    f.Deliver(f.secondPoll); actual.push_back(5);
    CHECK(f.ledger.CompletePass(NativeDispositionPass::KeyboardPoll,f.error));
    f.Deliver(f.deferred); actual.push_back(6); CHECK(f.ledger.CompletePass(NativeDispositionPass::SessionDeferred,f.error));
    CHECK((actual==std::vector<unsigned>{1,2,3,4,5,6}));
    auto receipt=f.Finish();f.source.status.pending.reset();CHECK(f.ingress.Finish(f.source,f.batch->Receipt(),f.error));
    CHECK(f.ledger.Current(receipt));CHECK(f.ledger.Retire());CHECK(!f.ledger.Current(receipt));
}
static void PlanFailures() {
    for(unsigned which=1;which<15;++which) {
        PlannedFixture f; f.Translate();
        NativeDispositionReceipt receipt{{456,789},23,45};const auto prior=receipt;
        NativeDispositionAdmission admission;admission.serial=10;const auto oldAdmission=admission;
        {
            CHECK(f.ledger.SealTranslation(f.error));
            switch(which) {
            case 1: CHECK(!f.ledger.BeginDelivery(f.secondKey,f.error));break;
            case 2: CHECK(!f.ledger.BeginDelivery(f.mouse,f.error));break;
            case 3: CHECK(!f.ledger.CompletePass(NativeDispositionPass::SessionInitial,f.error));break;
            case 4: CHECK(!f.ledger.Admit(f.deferred,admission,f.error));break;
            case 5: CHECK(!f.ledger.Admit(f.firstKey,admission,f.error));break;
            case 6: f.Initial();f.Mouse();CHECK(f.ledger.BeginDelivery(f.firstPoll,f.error));
                CHECK(!f.ledger.CompleteDelivery(f.firstPoll,NativeDeliveryDisposition::Delivered,f.error));break;
            case 7: f.Initial();f.Mouse();CHECK(f.ledger.BeginDelivery(f.firstPoll,f.error));
                f.Admit(f.deferred);CHECK(f.ledger.CompleteDelivery(f.firstPoll,NativeDeliveryDisposition::Delivered,f.error));
                CHECK(!f.ledger.BeginDelivery(f.deferred,f.error));break;
            case 8: CHECK(!f.ledger.Seal(receipt,f.error));CHECK(receipt==prior);break;
            case 9: {auto wrong=f.firstKey;++wrong.record.queueSequence;CHECK(!f.ledger.BeginDelivery(wrong,f.error));break;}
            case 10: f.source.observe=[&]{CHECK(f.ledger.Retire());};CHECK(!f.ledger.BeginDelivery(f.firstKey,f.error));break;
            case 11: f.source.continuity.Invalidate();CHECK(!f.ledger.BeginDelivery(f.firstKey,f.error));break;
            case 12: f.Initial();f.Mouse();CHECK(!f.ledger.Admit(f.deferred,admission,f.error));break;
            case 13: {auto wrong=f.firstKey;++wrong.record.receipt.ledger;CHECK(!f.ledger.BeginDelivery(wrong,f.error));break;}
            case 14: {PlannedFixture other;other.Translate();CHECK(!f.ledger.BeginDelivery(other.firstKey,f.error));break;}
            }
        }
        CHECK(admission==oldAdmission);CHECK(f.ledger.NeedsRetirement());CHECK(!f.ledger.Seal(receipt,f.error));CHECK(receipt==prior);
    }
    for(unsigned which=0;which<6;++which) {
        PlannedFixture f;auto r=f.Open();auto parent=f.Plan(r,NativeDispositionPass::KeyboardPoll);
        NativeDispositionTicket out{};out.emission=918;const auto old=out;
        NativeEmissionPlan plan{NativeDispositionPass::SessionDeferred,parent};
        if(which==0)++plan.trigger.record.receipt.ledger;
        if(which==1)++plan.trigger.record.index;
        if(which==2)plan.pass=static_cast<NativeDispositionPass>(9);
        if(which==3)plan.pass=NativeDispositionPass::MousePoll;
        if(which==4)f.Plan(r,NativeDispositionPass::SessionDeferred,parent);
        if(which==5)plan.trigger=f.Plan(r,NativeDispositionPass::SessionInitial);
        CHECK(!f.ledger.Issue(r,plan,out,f.error));CHECK(out==old);CHECK(f.ledger.NeedsRetirement());
    }
}
static void AdmissionAndEmptyPasses() {
    PlannedFixture f;auto record=f.Open();auto a=f.Plan(record,NativeDispositionPass::SessionInitial);auto b=f.Plan(record,NativeDispositionPass::SessionInitial);
    NativeDispositionAdmission out{};out.serial=991;const auto old=out;
    CHECK(!f.ledger.Admit(b,out,f.error));CHECK(out==old);CHECK(f.ledger.NeedsRetirement());(void)a;
    PlannedFixture partial;record=partial.Open();a=partial.Plan(record,NativeDispositionPass::SessionInitial);
    b=partial.Plan(record,NativeDispositionPass::KeyboardPoll);partial.Admit(a);
    CHECK(!partial.ledger.SealRecord(record,NativeRecordDisposition::Emitted,partial.error));
    NativeDispositionRecord later;CHECK(!partial.ledger.OpenRecord(1,later,partial.error));CHECK(partial.ledger.NeedsRetirement());
    PlannedFixture reentry;record=reentry.Open();a=reentry.Plan(record,NativeDispositionPass::SessionInitial);
    reentry.source.observe=[&]{NativeDispositionAdmission nested;CHECK(!reentry.ledger.Admit(a,nested,reentry.error));};
    CHECK(!reentry.ledger.Admit(a,out,reentry.error));CHECK(out==old);CHECK(reentry.ledger.NeedsRetirement());
    Fixture empty(false,true);CHECK(empty.ledger.BeginPlanned(empty.ingress,empty.source,*empty.batch,empty.error));record=empty.Open();
    CHECK(empty.ledger.SealRecord(record,NativeRecordDisposition::Fence,empty.error));CHECK(empty.ledger.SealTranslation(empty.error));
    for(auto pass:{NativeDispositionPass::SessionInitial,NativeDispositionPass::MousePoll,NativeDispositionPass::KeyboardPoll,NativeDispositionPass::SessionDeferred})
        CHECK(empty.ledger.CompletePass(pass,empty.error));
    empty.Finish();
    PlannedFixture wrongThread;record=wrongThread.Open();a=wrongThread.Plan(record,NativeDispositionPass::SessionInitial);
    std::thread worker([&]{std::string e;NativeDispositionAdmission value;CHECK(!wrongThread.ledger.Admit(a,value,e));});worker.join();
    CHECK(!wrongThread.ledger.NeedsRetirement());wrongThread.Admit(a);
    Fixture untranslated(false,true);CHECK(untranslated.ledger.BeginPlanned(untranslated.ingress,untranslated.source,*untranslated.batch,untranslated.error));
    record=untranslated.Open();CHECK(untranslated.ledger.SealRecord(record,NativeRecordDisposition::Fence,untranslated.error));
    NativeDispositionReceipt missing;CHECK(!untranslated.ledger.Seal(missing,untranslated.error));
}
static void FailedIngressPrefix() {
    Source source;NativeQueueIngress ingress;std::string error;
    std::string borrowed(300,'x');source.Add(OQ4_QUEUE_OUTSIDE,SDL_EVENT_TEXT_INPUT,1);source.events.back().first.text.text=borrowed.c_str();
    source.Add(OQ4_QUEUE_OUTSIDE,SDL_EVENT_USER+1,2);source.Add(OQ4_QUEUE_OUTSIDE,SDL_EVENT_KEY_UP,3);
    std::unique_ptr<const NativeQueueBatch> output;
    CHECK(ingress.Read(source,output,error)==NativeQueueRead::RetireRequired);CHECK(!output && source.head==2);
    auto boundary=ingress.FailureBoundary();CHECK(boundary.returnedOne && !boundary.copied && boundary.eventType==SDL_EVENT_USER+1 && boundary.copiedEvents==1);
    source.status.healthy=false;source.status.generation=0;auto observed=source.observations;
    CHECK(!ingress.ResetAfterRetirement(source,error));CHECK(source.observations==observed);
    std::unique_ptr<const NativeQueueQuarantine> failure;
    denyAllocation=true;const bool taken=ingress.TakeQuarantine(failure);denyAllocation=false;
    CHECK(taken && failure->Events().size()==1 && failure->Events()[0].text==borrowed && failure->Events()[0].header.text.text==nullptr);
    borrowed.assign(300,'z');CHECK(failure->Events()[0].text==std::string(300,'x'));
    CHECK(!ingress.TakeQuarantine(failure) && failure->Events().size()==1);
    CHECK(!ingress.Validate(source,{1,1},error));CHECK(!ingress.Finish(source,{1,1},error));
    CHECK(ingress.ResetAfterRetirement(source,error));source.status.healthy=true;source.status.generation=14;
    source.events.clear();source.head=0;source.Add(OQ4_QUEUE_OUTSIDE,SDL_EVENT_KEY_UP,1);
    CHECK(ingress.Read(source,output,error)==NativeQueueRead::Ready);CHECK(failure->Events().size()==1);
}
static void PlannedBudgetAndRetirement() {
    Fixture f;CHECK(f.ledger.BeginPlanned(f.ingress,f.source,*f.batch,f.error));auto record=f.Open();
    std::vector<NativeDispositionTicket> tickets(NativeEventDispositionLedger::MaxEmissions);
    NativeDispositionAdmission admission;NativeDispositionReceipt receipt;
    denyAllocation=true;
    for(auto& ticket:tickets) {
        CHECK(f.ledger.Issue(record,{NativeDispositionPass::SessionInitial,{}},ticket,f.error));
        CHECK(f.ledger.Admit(ticket,admission,f.error));
    }
    CHECK(f.ledger.SealRecord(record,NativeRecordDisposition::Emitted,f.error));
    CHECK(f.ledger.SealTranslation(f.error));
    for(auto ticket:tickets)f.Deliver(ticket);
    for(auto pass:{NativeDispositionPass::SessionInitial,NativeDispositionPass::MousePoll,NativeDispositionPass::KeyboardPoll,NativeDispositionPass::SessionDeferred})
        CHECK(f.ledger.CompletePass(pass,f.error));
    CHECK(f.ledger.Seal(receipt,f.error)&&f.ledger.Current(receipt));denyAllocation=false;
    Fixture capped;CHECK(capped.ledger.BeginPlanned(capped.ingress,capped.source,*capped.batch,capped.error));record=capped.Open();
    for(auto& ticket:tickets)CHECK(capped.ledger.Issue(record,{NativeDispositionPass::KeyboardPoll,{}},ticket,capped.error));
    NativeDispositionTicket extra=tickets.front();const auto saved=extra;
    CHECK(!capped.ledger.Issue(record,{NativeDispositionPass::KeyboardPoll,{}},extra,capped.error));CHECK(extra==saved&&capped.ledger.NeedsRetirement());
    for(unsigned point=0;point<3;++point) {
        PlannedFixture late;late.Translate();CHECK(late.ledger.SealTranslation(late.error));late.Initial();late.Mouse();
        CHECK(late.ledger.BeginDelivery(late.firstPoll,late.error));
        if(point==0)late.source.continuity.Invalidate();
        if(point==1)late.source.observe=[&]{late.source.claim=false;};
        if(point==2)late.source.observe=[&]{CHECK(late.ledger.Retire());};
        NativeDispositionAdmission output{{},443};const auto old=output;
        CHECK(!late.ledger.Admit(late.deferred,output,late.error));CHECK(output==old);
        CHECK(!late.ledger.CompleteDelivery(late.firstPoll,NativeDeliveryDisposition::Delivered,late.error));
    }
}
static void IngressBoundaryFailures() {
    for(unsigned which=0;which<6;++which) {
        Source source;NativeQueueIngress ingress;std::string error;std::unique_ptr<const NativeQueueBatch> out;
        source.Add(OQ4_QUEUE_OUTSIDE,SDL_EVENT_KEY_DOWN,1);
        if(which==0)source.failPoll=2;
        if(which==1)source.afterPoll=[&]{source.unavailable=true;};
        if(which==2)source.afterPoll=[&]{source.observe=[&]{std::unique_ptr<const NativeQueueQuarantine> q;CHECK(!ingress.TakeQuarantine(q));};};
        if(which==3){source.status.pending=OQ4_NativeFence{1,0,11,12};source.Add(OQ4_QUEUE_FENCE,source.status.fenceEventType,2);source.copyFailure=true;}
        if(which==4){source.Add(OQ4_QUEUE_OUTSIDE,SDL_EVENT_KEY_UP,2);source.events.back().second.generation++;}
        if(which==5)source.status.pending=OQ4_NativeFence{1,0,11,12};
        CHECK(ingress.Read(source,out,error)==NativeQueueRead::RetireRequired);CHECK(!out);
        std::unique_ptr<const NativeQueueQuarantine> q;
        std::thread worker([&]{CHECK(!ingress.TakeQuarantine(q));CHECK(ingress.FailureBoundary().reason==NativeQueueFailure::None);});worker.join();
        CHECK(!q && ingress.TakeQuarantine(q));CHECK(q->Events().size()==(which==3?2:1));
        CHECK(q->Boundary().copiedEvents==q->Events().size());
        if(which==1||which==2)CHECK(q->Boundary().copied);
        if(which==3)CHECK(q->Boundary().reason==NativeQueueFailure::FenceRejected && q->Boundary().copied);
        if(which==4)CHECK(q->Boundary().reason==NativeQueueFailure::HeaderRejected && !q->Boundary().copied);
        if(which==0)CHECK(q->Boundary().removalUncertain);
    }
}
static void IngressAllocationFailure() {
    // The final owning slot is staged before removal, including debug-STL
    // proxy allocation. No post-copy move may discard this complete fence.
    Source fence;fence.status.pending=OQ4_NativeFence{1,0,9,10};
    fence.Add(OQ4_QUEUE_FENCE,fence.status.fenceEventType,1);fence.denyAfterPoll=1;
    NativeQueueIngress sealed;std::string fenceError;std::unique_ptr<const NativeQueueBatch> complete;
    const auto completed=sealed.Read(fence,complete,fenceError);denyAllocation=false;
    CHECK(completed==NativeQueueRead::Ready&&complete&&complete->Events().size()==1);
    const auto calls=fence.observations;
    std::thread foreign([&]{
        std::string e;std::unique_ptr<const NativeQueueBatch> other;
        CHECK(sealed.Read(fence,other,e)==NativeQueueRead::RetireRequired&&!other);
        CHECK(!sealed.Validate(fence,complete->Receipt(),e));CHECK(!sealed.Finish(fence,complete->Receipt(),e));
        CHECK(!sealed.ResetAfterRetirement(fence,e));
    });foreign.join();
    CHECK(fence.observations==calls&&!sealed.NeedsRetirement());CHECK(sealed.Validate(fence,complete->Receipt(),fenceError));
    Source source;NativeQueueIngress ingress;std::string error;std::unique_ptr<const NativeQueueBatch> out;
    const std::string text(500,'q');source.Add(OQ4_QUEUE_OUTSIDE,SDL_EVENT_KEY_DOWN,1);
    source.Add(OQ4_QUEUE_OUTSIDE,SDL_EVENT_TEXT_INPUT,2);source.events.back().first.text.text=text.c_str();source.denyAfterPoll=2;
    const auto result=ingress.Read(source,out,error);denyAllocation=false;
    CHECK(result==NativeQueueRead::RetireRequired && !out);
    std::unique_ptr<const NativeQueueQuarantine> q;CHECK(ingress.TakeQuarantine(q));
    CHECK(q->Events().size()==1 && q->Boundary().returnedOne && !q->Boundary().copied);
    CHECK(q->Boundary().reason==NativeQueueFailure::AllocationOrSourceException);
    Source early;NativeQueueIngress before;std::string e;
    denyAllocation=true;const auto failed=before.Read(early,out,e);denyAllocation=false;
    CHECK(failed==NativeQueueRead::RetireRequired && early.polls==0 && !out);
    CHECK(before.FailureBoundary().copiedEvents==0 && !before.FailureBoundary().removalUncertain);
    early.status.healthy=false;early.status.generation=0;CHECK(before.ResetAfterRetirement(early,e));
    early.status.healthy=true;early.status.generation=13;CHECK(before.Read(early,out,e)==NativeQueueRead::RetireRequired);
}
static void ScheduledKeyboard(PlannedFixture& f) {
    CHECK(f.ledger.BeginDelivery(f.firstPoll,f.error));f.Admit(f.deferred);
    CHECK(f.ledger.CompleteDelivery(f.firstPoll,NativeDeliveryDisposition::Delivered,f.error));
    f.Deliver(f.secondPoll);CHECK(f.ledger.CompletePass(NativeDispositionPass::KeyboardPoll,f.error));
}
static void BothEntrySchedules() {
    for(auto schedule:{NativeDispositionSchedule::BeforeSessionDrain,NativeDispositionSchedule::AtMousePollEntry}) {
        PlannedFixture f(schedule);f.wheel.reserve(6);
        // The order table adds no allocation to translation or terminal gates.
        denyAllocation=true;f.Translate();CHECK(f.ledger.SealTranslation(f.error));
        if(schedule==NativeDispositionSchedule::BeforeSessionDrain)f.Initial();
        // Zero real Usercmd calls do not consume poll tickets or finish a batch.
        const auto observed=f.source.observations;
        for(unsigned idle=0;idle<3;++idle)CHECK(!f.ledger.Current(f.firstKey.record.receipt));
        CHECK(f.source.observations==observed);
        f.Mouse();ScheduledKeyboard(f);
        // Under AtMousePollEntry all nonempty Session tickets remained pending
        // throughout Mouse and Keyboard, including the newly admitted child.
        if(schedule==NativeDispositionSchedule::AtMousePollEntry)f.Initial();
        f.Deliver(f.deferred);CHECK(f.ledger.CompletePass(NativeDispositionPass::SessionDeferred,f.error));
        auto receipt=f.Finish();denyAllocation=false;
        CHECK(f.ledger.Current(receipt));
    }
}
static void ScheduleFailures() {
    for(unsigned which=0;which<10;++which) {
        PlannedFixture f(NativeDispositionSchedule::AtMousePollEntry);f.Translate();CHECK(f.ledger.SealTranslation(f.error));
        NativeDispositionAdmission out{{},981};const auto old=out;
        switch(which) {
        case 0: CHECK(!f.ledger.BeginDelivery(f.firstKey,f.error));break;
        case 1: CHECK(!f.ledger.CompletePass(NativeDispositionPass::SessionInitial,f.error));break;
        case 2: CHECK(!f.ledger.BeginDelivery(f.firstPoll,f.error));break;
        case 3: f.Mouse();CHECK(!f.ledger.BeginDelivery(f.firstKey,f.error));break;
        case 4: f.Mouse();CHECK(!f.ledger.Admit(f.deferred,out,f.error));CHECK(out==old);break;
        case 5: f.Mouse();CHECK(f.ledger.BeginDelivery(f.firstPoll,f.error));
            CHECK(!f.ledger.CompleteDelivery(f.firstPoll,NativeDeliveryDisposition::Delivered,f.error));break;
        case 6: f.Mouse();ScheduledKeyboard(f);CHECK(!f.ledger.BeginDelivery(f.deferred,f.error));break;
        case 7: f.Mouse();CHECK(!f.ledger.BeginDelivery(f.mouse,f.error));break; // A later real poll cannot redeliver the owned slice.
        case 8: f.Mouse();ScheduledKeyboard(f);CHECK(!f.ledger.CompletePass(NativeDispositionPass::KeyboardPoll,f.error));break;
        case 9: CHECK(!f.ledger.CompletePass(static_cast<NativeDispositionPass>(99),f.error));break;
        }
        CHECK(f.ledger.NeedsRetirement());
    }
    PlannedFixture initial(NativeDispositionSchedule::BeforeSessionDrain);initial.Translate();CHECK(initial.ledger.SealTranslation(initial.error));
    CHECK(!initial.ledger.BeginDelivery(initial.mouse,initial.error));
    for(int value:{-1,2,999})for(bool allocationDenied:{false,true}) {
        Fixture invalid;const auto probes=invalid.source.observations;const auto polls=invalid.source.polls;
        denyAllocation=allocationDenied;
        const bool began=invalid.ledger.BeginPlanned(invalid.ingress,invalid.source,*invalid.batch,static_cast<NativeDispositionSchedule>(value),invalid.error);
        denyAllocation=false;
        CHECK(!began&&invalid.ledger.NeedsRetirement());CHECK(invalid.source.observations==probes&&invalid.source.polls==polls);
    }
    // Changing the caller's enum during source validation cannot change the
    // schedule copied at entry, and a second Begin cannot rebind the instance.
    Fixture captured;auto selected=NativeDispositionSchedule::AtMousePollEntry;
    captured.source.observe=[&]{selected=NativeDispositionSchedule::BeforeSessionDrain;};
    CHECK(captured.ledger.BeginPlanned(captured.ingress,captured.source,*captured.batch,selected,captured.error));
    auto r=captured.Open();NativeDispositionTicket key,mouse;NativeDispositionAdmission admission;
    CHECK(captured.ledger.Issue(r,{NativeDispositionPass::SessionInitial,{}},key,captured.error));
    CHECK(captured.ledger.Issue(r,{NativeDispositionPass::MousePoll,{}},mouse,captured.error));
    CHECK(captured.ledger.Admit(key,admission,captured.error));CHECK(captured.ledger.Admit(mouse,admission,captured.error));
    CHECK(captured.ledger.SealRecord(r,NativeRecordDisposition::Emitted,captured.error));CHECK(captured.ledger.SealTranslation(captured.error));
    CHECK(selected==NativeDispositionSchedule::BeforeSessionDrain);captured.Deliver(mouse);
    CHECK(captured.ledger.CompletePass(NativeDispositionPass::MousePoll,captured.error));
    CHECK(!captured.ledger.BeginPlanned(captured.ingress,captured.source,*captured.batch,selected,captured.error));
}
static void EmptyScheduledLanes() {
    for(auto schedule:{NativeDispositionSchedule::BeforeSessionDrain,NativeDispositionSchedule::AtMousePollEntry}) {
        Fixture empty(false,true);CHECK(empty.ledger.BeginPlanned(empty.ingress,empty.source,*empty.batch,schedule,empty.error));
        auto r=empty.Open();CHECK(empty.ledger.SealRecord(r,NativeRecordDisposition::Fence,empty.error));
        CHECK(empty.ledger.SealTranslation(empty.error));
        if(schedule==NativeDispositionSchedule::BeforeSessionDrain)CHECK(empty.ledger.CompletePass(NativeDispositionPass::SessionInitial,empty.error));
        CHECK(empty.ledger.CompletePass(NativeDispositionPass::MousePoll,empty.error));
        CHECK(empty.ledger.CompletePass(NativeDispositionPass::KeyboardPoll,empty.error));
        if(schedule==NativeDispositionSchedule::AtMousePollEntry)CHECK(empty.ledger.CompletePass(NativeDispositionPass::SessionInitial,empty.error));
        CHECK(empty.ledger.CompletePass(NativeDispositionPass::SessionDeferred,empty.error));empty.Finish();
    }
    Fixture wrong(false,true);CHECK(wrong.ledger.BeginPlanned(wrong.ingress,wrong.source,*wrong.batch,NativeDispositionSchedule::AtMousePollEntry,wrong.error));
    auto empty=wrong.Open();CHECK(wrong.ledger.SealRecord(empty,NativeRecordDisposition::Fence,wrong.error));CHECK(wrong.ledger.SealTranslation(wrong.error));
    CHECK(!wrong.ledger.CompletePass(NativeDispositionPass::SessionInitial,wrong.error)&&wrong.ledger.NeedsRetirement());
    // Empty lanes are explicit proofs from sealed inventory, not fabricated
    // Usercmd calls. A nonempty trailing Session lane still requires delivery.
    Fixture text;CHECK(text.ledger.BeginPlanned(text.ingress,text.source,*text.batch,NativeDispositionSchedule::AtMousePollEntry,text.error));
    auto r=text.Open();NativeDispositionTicket ticket;NativeDispositionAdmission admission;
    CHECK(text.ledger.Issue(r,{NativeDispositionPass::SessionInitial,{}},ticket,text.error));CHECK(text.ledger.Admit(ticket,admission,text.error));
    CHECK(text.ledger.SealRecord(r,NativeRecordDisposition::Emitted,text.error));CHECK(text.ledger.SealTranslation(text.error));
    CHECK(text.ledger.CompletePass(NativeDispositionPass::MousePoll,text.error));CHECK(text.ledger.CompletePass(NativeDispositionPass::KeyboardPoll,text.error));
    text.Deliver(ticket);CHECK(text.ledger.CompletePass(NativeDispositionPass::SessionInitial,text.error));
    CHECK(text.ledger.CompletePass(NativeDispositionPass::SessionDeferred,text.error));text.Finish();
}
static void IssuedRetirementInspection() {
    PlannedFixture f;
    const auto record=f.Open(0);
    const auto session=f.Plan(record,NativeDispositionPass::SessionInitial);
    const auto keyboard=f.Plan(record,NativeDispositionPass::KeyboardPoll);
    const auto child=f.Plan(record,NativeDispositionPass::SessionDeferred,keyboard);
    const auto admitted=f.Admit(session);
    NativeIssuedEmission sentinel;
    sentinel.ticket.emission=999;sentinel.admissionSerial=888;sentinel.terminal=true;
    auto output=sentinel;
    CHECK(!f.ledger.InspectIssuedForRetirement(session,output) && output==sentinel);
    CHECK(f.ledger.Retire());
    // These providers can no longer establish live authority. Historical
    // inspection must not invoke them or infer successful delivery from storage.
    f.source.claim=false;f.source.unavailable=true;f.source.continuity.Invalidate();
    const unsigned observed=f.source.observations;
    denyAllocation=true;
    CHECK(f.ledger.InspectIssuedForRetirement(session,output));
    CHECK(output.ticket==session && output.pass==NativeDispositionPass::SessionInitial &&
        output.admissionSerial==admitted.serial && !output.terminal && !output.inFlight && output.trigger==NativeDispositionTicket{});
    CHECK(f.ledger.InspectIssuedForRetirement(keyboard,output));
    CHECK(output.ticket==keyboard && output.pass==NativeDispositionPass::KeyboardPoll && !output.admissionSerial && !output.terminal);
    CHECK(f.ledger.InspectIssuedForRetirement(child,output));
    CHECK(output.trigger==keyboard && output.pass==NativeDispositionPass::SessionDeferred && !output.admissionSerial);
    denyAllocation=false;
    CHECK(f.source.observations==observed);
    for(unsigned field=0;field<8;++field) {
        auto wrong=keyboard;
        switch(field) {
        case 0:++wrong.record.receipt.batch.ingress;break;case 1:++wrong.record.receipt.batch.serial;break;
        case 2:++wrong.record.receipt.ledger;break;case 3:++wrong.record.receipt.serial;break;
        case 4:++wrong.record.queueSequence;break;case 5:++wrong.record.index;break;
        case 6:wrong.emission=0;break;case 7:wrong.emission=NativeEventDispositionLedger::MaxEmissions+1;break;
        }
        output=sentinel;CHECK(!f.ledger.InspectIssuedForRetirement(wrong,output) && output==sentinel);
    }
    bool foreign=false;output=sentinel;
    std::thread worker([&]{foreign=f.ledger.InspectIssuedForRetirement(keyboard,output);});worker.join();
    CHECK(!foreign && output==sentinel);
    CHECK(f.ledger.Retire());CHECK(f.ledger.InspectIssuedForRetirement(keyboard,output));
    PlannedFixture other;CHECK(other.ledger.Retire());output=sentinel;
    CHECK(!other.ledger.InspectIssuedForRetirement(keyboard,output) && output==sentinel);
    // An exact slot survives a failed Admit after a hypothetical actual storage
    // transfer; this test supplies no storage-transfer or cancellation receipt.
    PlannedFixture partial;auto pr=partial.Open(0);auto pt=partial.Plan(pr,NativeDispositionPass::SessionInitial);
    partial.source.claim=false;NativeDispositionAdmission unused;
    CHECK(!partial.ledger.Admit(pt,unused,partial.error));
    CHECK(partial.ledger.InspectIssuedForRetirement(pt,output) && !output.admissionSerial && !output.terminal);
    PlannedFixture progress;progress.Translate();CHECK(progress.ledger.SealTranslation(progress.error));
    progress.Initial();progress.Mouse();CHECK(progress.ledger.BeginDelivery(progress.firstPoll,progress.error));
    CHECK(progress.ledger.Retire());
    CHECK(progress.ledger.InspectIssuedForRetirement(progress.firstKey,output) && output.terminal && !output.inFlight);
    CHECK(progress.ledger.InspectIssuedForRetirement(progress.firstPoll,output) && output.inFlight && !output.terminal);
    CHECK(progress.ledger.InspectIssuedForRetirement(progress.deferred,output) && !output.inFlight && !output.terminal);
    // Retire inside a source callback cannot expose facts until the outer call
    // unwinds; otherwise a partially executing publisher could lend authority.
    PlannedFixture nested;auto nr=nested.Open(0);auto nt=nested.Plan(nr,NativeDispositionPass::SessionInitial);
    nested.source.observe=[&]{CHECK(nested.ledger.Retire());output=sentinel;
        CHECK(!nested.ledger.InspectIssuedForRetirement(nt,output) && output==sentinel);};
    NativeDispositionTicket rejected;CHECK(!nested.ledger.Issue(nr,{NativeDispositionPass::KeyboardPoll,{}},rejected,nested.error));
    CHECK(nested.ledger.InspectIssuedForRetirement(nt,output));
    Fixture serial;serial.Begin();auto sr=serial.Open();auto st=serial.Issue(sr);CHECK(serial.ledger.Retire());output=sentinel;
    CHECK(!serial.ledger.InspectIssuedForRetirement(st,output) && output==sentinel);
}
int main() {
    SerialDispositionMain();SinkOrder();PlanFailures();AdmissionAndEmptyPasses();PlannedBudgetAndRetirement();FailedIngressPrefix();IngressBoundaryFailures();IngressAllocationFailure();
    BothEntrySchedules();ScheduleFailures();EmptyScheduledLanes();
    IssuedRetirementInspection();
    std::printf("PASS %u checks\n",checks);
}
