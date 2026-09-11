// Actual storage and transfer methods precede this test; external native/UI
// retirement remains a counted contract. No native input or GUI is invoked.
using Status=NativeInputTransferStatus;
using Delivery=NativeInputDeliveryStatus;
struct TerminalFacts final:NativeInputRouteSource {
    NativeInputBinding binding;
    NativeInputEmissionInventory* inventory=nullptr;
    std::uint64_t id=0;
    bool retired=false,busy=false;
    mutable std::function<void()> callback;
    bool Observe(NativeInputObservation& out)const noexcept override {
        CHECK(lockDepth==0);auto call=std::move(callback);if(call)call();
        out={binding.outer,binding.editor.allocation,binding.sessionTransition,Sys_EventDispositionEpoch(),Sys_EventQueueToken(),binding.window,true,true,true,true};return true;
    }
    bool Retirement(std::uint64_t route,const NativeInputBinding& original,NativeInputRetirement& out)const noexcept override {
        CHECK(lockDepth==0);auto call=std::move(callback);if(call)call();
        if(route!=id || original.editor!=binding.editor || original.native!=binding.native || original.window!=binding.window)return false;
        out={id,binding.native,binding.window,busy?NativeInputUiRetirement::Busy:retired?NativeInputUiRetirement::AbsentOriginal:NativeInputUiRetirement::Unknown,
            retired?NativeInputNativeRetirement::RetiredExact:NativeInputNativeRetirement::Unknown,
            retired?NativeInputNativeRetirement::RetiredExact:NativeInputNativeRetirement::Unknown,retired,retired,inventory&&inventory->BacklogDisposed()};return true;
    }
    bool InspectIssued(std::uint64_t route,const NativeInputBinding& original,const sysEventDispositionTag_t& tag,NativeInputIssued& out)const noexcept override {
        CHECK(lockDepth==0);return inventory&&inventory->Inspect(route,original,tag,out);
    }
};
struct TerminalFixture {
    Source source;NativeQueueIngress ingress;std::unique_ptr<const NativeQueueBatch> batch;
    NativeEventDispositionLedger ledger{source};NativeInputEmissionInventory inventory;
    TerminalFacts facts;NativeInputRoute route{facts};idEventLoop loop;
    NativeInputTransfers transfers{inventory,route,loop};std::string error;
    explicit TerminalFixture(bool claim=true) {
        Sys_ClearEvents();SDL3_ClearInputQueues();CHECK(Sys_BindEventDispositionThread());
        while(source.continuity.Token()<Sys_EventQueueToken())source.continuity.Invalidate();
        source.Add(OQ4_QUEUE_OUTSIDE,SDL_EVENT_KEY_DOWN,1);
        CHECK(ingress.Read(source,batch,error)==NativeQueueRead::Ready);
        auto& b=facts.binding;b.outer=0x1000;b.sessionTransition=1;b.dispatchEpoch=Sys_EventDispositionEpoch();b.streamToken=Sys_EventQueueToken();
        b.window={0x2000,5,6,7,8,9};b.editor={10,11,12,13,5,14,15,"number.field"};b.native={16,17};facts.inventory=&inventory;
        CHECK(route.Prepare(b,facts.id,error));CHECK(ledger.BeginPlanned(ingress,source,*batch,error));
        CHECK(inventory.Bind(route,facts.id,b,ledger,*batch,error));if(claim)CHECK(transfers.Claim(error));
    }
    ~TerminalFixture(){onFree={};beforeLock={};Sys_ClearEvents();SDL3_ClearInputQueues();}
    NativeDispositionRecord Open(){NativeDispositionRecord record;CHECK(ledger.OpenRecord(0,record,error));return record;}
    void Retire(){facts.retired=true;CHECK(inventory.Retire());}
    void Finish(){NativeInputDisposalReceipt receipt;CHECK(inventory.SealDisposal(receipt,error));CHECK(inventory.DisposalCurrent(receipt));
        CHECK(route.Release(facts.id));CHECK(transfers.Release());CHECK(!inventory.DisposalCurrent(receipt));CHECK(!transfers.Release());}
};
static sysEvent_t Key(int key='a',bool payload=false) {
    sysEvent_t e{SE_KEY,key,1,0,nullptr};
    if(payload){auto bytes=EncodeKeyEventMetadata({true,true,false,false});e.evPtr=Allocate(bytes.size());e.evPtrLength=static_cast<int>(bytes.size());std::memcpy(e.evPtr,bytes.data(),bytes.size());}
    return e;
}
static void AllStored(bool polled) {
    TerminalFixture f;const auto record=f.Open();auto platform=Key('a',true),pushed=Key('b',true);void* a=platform.evPtr;void* b=pushed.evPtr;
    NativeTranslatedEmission first,second,mouse;NativeTranslatedKeyboard keyboard;
    CHECK(f.transfers.SessionKey(record,platform,NativeInputSessionQueue::Platform,first,f.error)==Status::StoredAdmitted);
    CHECK(f.transfers.SessionKey(record,pushed,NativeInputSessionQueue::Pushed,second,f.error)==Status::StoredAdmitted);
    CHECK(!platform.evPtr&&!pushed.evPtr);CHECK(f.transfers.Keyboard(record,K_CTRL,true,17,keyboard,f.error)==Status::StoredAdmitted);
    CHECK(keyboard.deferred.ticket.emission);CHECK(f.transfers.Mouse(record,M_DELTAX,4,19,mouse,f.error)==Status::StoredAdmitted);
    CHECK(f.ledger.SealRecord(record,NativeRecordDisposition::Emitted,f.error));
    sysInputDispositionSlice_t ks,ms;
    if(polled){CHECK(Sys_PollKeyboardInputWithDisposition(ks));CHECK(Sys_PollMouseInputWithDisposition(ms));}
    f.Retire();CHECK(Sys_RetireEventDispositionThread());
    NativeInputDisposalReceipt premature;CHECK(!f.inventory.SealDisposal(premature,f.error));CHECK(!f.route.Release(f.facts.id));
    NativeInputHead head;const auto p=pumps;denyAllocation=true;
    CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Ready&&head.tag==second.tag);CHECK(Freed(b)&&!Freed(a));
    CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Ready&&head.tag==first.tag);CHECK(Freed(a));
    CHECK(f.transfers.CancelMouse(head,f.error)==sysEventTransfer_t::Ready&&head.tag==mouse.tag);
    CHECK(f.transfers.CancelKeyboard(head,f.error)==sysEventTransfer_t::Ready&&head.tag==keyboard.parent.tag);
    CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Empty);
    CHECK(f.transfers.CancelKeyboard(head,f.error)==sysEventTransfer_t::Empty);
    if(polled){CHECK(Sys_EndKeyboardInputForRetirement(ks));CHECK(Sys_EndMouseInputForRetirement(ms));}
    f.Finish();denyAllocation=false;CHECK(pumps==p);
}
static void DeferredAndPartialDelivery() {
    TerminalFixture f;auto record=f.Open();NativeTranslatedKeyboard keyboard;NativeTranslatedEmission ordinary,child;
    auto event=Key('x',true);void* payload=event.evPtr;
    CHECK(f.transfers.SessionKey(record,event,NativeInputSessionQueue::Platform,ordinary,f.error)==Status::StoredAdmitted);
    CHECK(f.transfers.Keyboard(record,K_ALT,true,50,keyboard,f.error)==Status::StoredAdmitted);
    CHECK(f.ledger.SealRecord(record,NativeRecordDisposition::Emitted,f.error));CHECK(f.ledger.SealTranslation(f.error));
    sysEvent_t delivered;NativeTranslatedEmission taken;
    CHECK(f.transfers.BeginSessionDelivery(delivered,taken,f.error)==Delivery::OwnedDelivery&&taken.tag==ordinary.tag);
    Mem_Free(delivered.evPtr); // Counted successful original sink.
    CHECK(f.ledger.CompleteDelivery(ordinary.ticket,NativeDeliveryDisposition::Delivered,f.error));
    CHECK(f.ledger.CompletePass(NativeDispositionPass::SessionInitial,f.error));CHECK(f.ledger.CompletePass(NativeDispositionPass::MousePoll,f.error));
    sysInputDispositionSlice_t slice;sysInputDispositionSlot_t slot;sysKeyboardInputDisposition_t value;
    CHECK(Sys_PollKeyboardInputWithDisposition(slice));CHECK(Sys_PeekKeyboardInputWithDisposition(slice,slot,value)==sysEventTransfer_t::Ready);
    CHECK(f.transfers.BeginKeyboardDelivery(slot,value,taken,f.error)==Delivery::OwnedDelivery&&taken.tag==keyboard.parent.tag);
    CHECK(f.transfers.Deferred(keyboard,child,f.error)==Status::StoredAdmitted);
    CHECK(f.ledger.CompleteDelivery(keyboard.parent.ticket,NativeDeliveryDisposition::Delivered,f.error));
    CHECK(Sys_EndKeyboardInputWithDisposition(slice));f.Retire();NativeInputHead head;
    CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Ready&&head.tag==child.tag);
    CHECK(Freed(payload));f.Finish();
}
static void FailureOwnership() {
    {TerminalFixture f;auto record=f.Open();auto event=Key('a',true);auto original=EventValue(event);NativeTranslatedEmission emitted;
     // Actual queue refusal retains the caller; a private never-transfer witness
     // may release the route but may not free caller-owned storage.
     f.loop.com_journal.SetInteger(1);
     CHECK(f.transfers.SessionKey(record,event,NativeInputSessionQueue::Pushed,emitted,f.error)==Status::Unchanged);
     CHECK(EventValue(event)==original&&!Freed(event.evPtr));f.Retire();f.Finish();Mem_Free(event.evPtr);f.loop.com_journal.SetInteger(0);}
    {TerminalFixture f;auto record=f.Open();auto event=Key('a',true);void* pointer=event.evPtr;NativeTranslatedEmission emitted;
     f.source.observe=[&]{CHECK(!f.transfers.Claim(f.error));f.inventory.Retire();};
     // The callback at ledger Issue runs before storage. No ownership is invented.
     CHECK(f.transfers.SessionKey(record,event,NativeInputSessionQueue::Platform,emitted,f.error)==Status::Unchanged);
     CHECK(event.evPtr==pointer&&!Freed(pointer));f.Retire();f.Finish();Mem_Free(pointer);}
    {TerminalFixture f;auto record=f.Open();NativeTranslatedKeyboard keyboard;
     // The queue section callback occurs after issue but before actual admission;
     // queue transfer still succeeds and must be retained after reentrant retirement.
     beforeLock=[&]{NativeInputHead output;CHECK(f.transfers.CancelKeyboard(output,f.error)==sysEventTransfer_t::Refused);};
     CHECK(f.transfers.Keyboard(record,'a',true,3,keyboard,f.error)==Status::StoredRetired);
     f.Retire();NativeInputHead head;CHECK(f.transfers.CancelKeyboard(head,f.error)==sysEventTransfer_t::Ready);f.Finish();}
    {TerminalFixture f;auto record=f.Open();auto event=Key();NativeTranslatedEmission emitted;
     CHECK(f.transfers.SessionKey(record,event,NativeInputSessionQueue::Platform,emitted,f.error)==Status::StoredAdmitted);
     f.Retire();Sys_ClearEvents();NativeInputHead head;CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Empty);
     NativeInputDisposalReceipt receipt;CHECK(!f.inventory.SealDisposal(receipt,f.error));CHECK(!f.route.Release(f.facts.id));}
    {TerminalFixture f(false);auto record=f.Open();NativeTranslatedEmission emitted;
     CHECK(f.inventory.SessionMouse(record,1,2,emitted,f.error));f.Retire();NativeInputDisposalReceipt receipt;
     CHECK(!f.inventory.SealDisposal(receipt,f.error));CHECK(!f.transfers.Claim(f.error));}
}
static void RefusalsAndReentry() {
    {TerminalFixture f;auto record=f.Open();auto event=Key('a',true);NativeTranslatedEmission emitted;
     CHECK(f.transfers.SessionKey(record,event,NativeInputSessionQueue::Platform,emitted,f.error)==Status::StoredAdmitted);
     NativeInputHead head;head.sequence=991;const auto saved=head;
     CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Refused&&head==saved); // native/UI not retired
     f.Retire();f.facts.busy=true;CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Refused&&head==saved);f.facts.busy=false;
     std::thread worker([&]{std::string e;CHECK(f.transfers.CancelSession(head,e)==sysEventTransfer_t::Refused&&head==saved);CHECK(!f.transfers.Release());});worker.join();
     onFree=[&]{CHECK(lockDepth==0);NativeInputHead nested;CHECK(f.transfers.CancelSession(nested,f.error)==sysEventTransfer_t::Refused);};
     CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Refused&&head==saved); // disposal completed, callback poisoned outer success
     CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Empty);f.Finish();}
    {TerminalFixture f;auto record=f.Open();auto event=Key();NativeTranslatedEmission emitted;CHECK(f.transfers.SessionKey(record,event,NativeInputSessionQueue::Platform,emitted,f.error)==Status::StoredAdmitted);
     sysEvent_t legacy{SE_CHAR,'z',0,0,nullptr};f.loop.PushEvent(&legacy);f.Retire();NativeInputHead head;
     CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Refused);CHECK(f.loop.GetEvent().evValue=='z');
     CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Ready);f.Finish();}
    {TerminalFixture f;auto record=f.Open();NativeTranslatedKeyboard keyboard;CHECK(f.transfers.Keyboard(record,K_CTRL,true,3,keyboard,f.error)==Status::StoredAdmitted);
     CHECK(f.ledger.SealRecord(record,NativeRecordDisposition::Emitted,f.error));CHECK(f.ledger.SealTranslation(f.error));
     CHECK(f.ledger.CompletePass(NativeDispositionPass::SessionInitial,f.error));CHECK(f.ledger.CompletePass(NativeDispositionPass::MousePoll,f.error));
     sysInputDispositionSlice_t slice;sysInputDispositionSlot_t slot;sysKeyboardInputDisposition_t value;NativeTranslatedEmission taken;CHECK(Sys_PollKeyboardInputWithDisposition(slice));CHECK(Sys_PeekKeyboardInputWithDisposition(slice,slot,value)==sysEventTransfer_t::Ready);
     CHECK(f.transfers.BeginKeyboardDelivery(slot,value,taken,f.error)==Delivery::OwnedDelivery);f.Retire();NativeInputDisposalReceipt receipt;
     CHECK(!f.inventory.SealDisposal(receipt,f.error));CHECK(!f.route.Release(f.facts.id));CHECK(Sys_EndKeyboardInputForRetirement(slice));}
    {TerminalFixture f;auto record=f.Open();NativeDispositionTicket extra;CHECK(f.ledger.Issue(record,{NativeDispositionPass::SessionInitial,{}},extra,f.error));f.Retire();NativeInputDisposalReceipt receipt;CHECK(!f.inventory.SealDisposal(receipt,f.error));}
}
static void DeliveryOwnership() {
    {TerminalFixture f;auto record=f.Open();auto event=Key('a',true);void* payload=event.evPtr;NativeTranslatedEmission emission,taken;
     NativeTranslatedEmission mouse;CHECK(f.transfers.SessionKey(record,event,NativeInputSessionQueue::Pushed,emission,f.error)==Status::StoredAdmitted);
     CHECK(f.transfers.Mouse(record,M_DELTAY,9,55,mouse,f.error)==Status::StoredAdmitted);
     CHECK(f.ledger.SealRecord(record,NativeRecordDisposition::Emitted,f.error));CHECK(f.ledger.SealTranslation(f.error));
     sysEvent_t output{};CHECK(f.transfers.BeginSessionDelivery(output,taken,f.error)==Delivery::OwnedDelivery&&taken.tag==emission.tag);
     CHECK(output.evPtr==payload);Mem_Free(output.evPtr);CHECK(f.ledger.CompleteDelivery(taken.ticket,NativeDeliveryDisposition::Delivered,f.error));
     CHECK(f.ledger.CompletePass(NativeDispositionPass::SessionInitial,f.error));
     sysInputDispositionSlice_t slice;sysInputDispositionSlot_t slot;sysMouseInputDisposition_t value;
     CHECK(Sys_PollMouseInputWithDisposition(slice));CHECK(Sys_PeekMouseInputWithDisposition(slice,slot,value)==sysEventTransfer_t::Ready);
     CHECK(f.transfers.BeginMouseDelivery(slot,value,taken,f.error)==Delivery::OwnedDelivery&&taken.tag==mouse.tag);
     CHECK(value.value==9&&value.time==55);CHECK(f.ledger.CompleteDelivery(taken.ticket,NativeDeliveryDisposition::AuditedDiscard,f.error));
     CHECK(Sys_EndMouseInputWithDisposition(slice));CHECK(f.ledger.CompletePass(NativeDispositionPass::MousePoll,f.error));
     CHECK(f.ledger.CompletePass(NativeDispositionPass::KeyboardPoll,f.error));CHECK(f.ledger.CompletePass(NativeDispositionPass::SessionDeferred,f.error));
     NativeDispositionReceipt complete;CHECK(f.ledger.Seal(complete,f.error)&&f.ledger.Current(complete));f.Retire();f.Finish();}
    {TerminalFixture f;auto record=f.Open();auto event=Key();NativeTranslatedEmission emission;
     CHECK(f.transfers.SessionKey(record,event,NativeInputSessionQueue::Platform,emission,f.error)==Status::StoredAdmitted);
     CHECK(f.ledger.SealRecord(record,NativeRecordDisposition::Emitted,f.error));CHECK(f.ledger.SealTranslation(f.error));
     // A reported sink completion did not Take the real queued input.
     CHECK(f.ledger.BeginDelivery(emission.ticket,f.error));CHECK(f.ledger.CompleteDelivery(emission.ticket,NativeDeliveryDisposition::Delivered,f.error));
     f.Retire();NativeInputDisposalReceipt receipt;CHECK(!f.inventory.SealDisposal(receipt,f.error));CHECK(!f.route.Release(f.facts.id));}
    {TerminalFixture f;auto record=f.Open();auto event=Key('a',true);void* pointer=event.evPtr;NativeTranslatedEmission emission,taken;
     CHECK(f.transfers.SessionKey(record,event,NativeInputSessionQueue::Platform,emission,f.error)==Status::StoredAdmitted);
     CHECK(f.ledger.SealRecord(record,NativeRecordDisposition::Emitted,f.error));CHECK(f.ledger.SealTranslation(f.error));
     f.source.observe=[&]{CHECK(!f.transfers.Claim(f.error));};
     sysEvent_t output{};CHECK(f.transfers.BeginSessionDelivery(output,taken,f.error)==Delivery::OwnedRetired);
     CHECK(output.evPtr==pointer&&taken.tag==emission.tag);Mem_Free(output.evPtr);f.Retire();
     NativeInputDisposalReceipt receipt;CHECK(!f.inventory.SealDisposal(receipt,f.error));CHECK(!f.route.Release(f.facts.id));}
    {TerminalFixture f;auto record=f.Open();NativeTranslatedKeyboard keyboard;NativeTranslatedEmission taken;sysKeyboardInputDisposition_t value;
     CHECK(f.transfers.Keyboard(record,'a',true,4,keyboard,f.error)==Status::StoredAdmitted);
     CHECK(f.ledger.SealRecord(record,NativeRecordDisposition::Emitted,f.error));CHECK(f.ledger.SealTranslation(f.error));
     sysInputDispositionSlice_t slice;sysInputDispositionSlot_t slot;CHECK(Sys_PollKeyboardInputWithDisposition(slice));
     CHECK(Sys_PeekKeyboardInputWithDisposition(slice,slot,value)==sysEventTransfer_t::Ready);value.key=991;
     CHECK(f.transfers.BeginKeyboardDelivery(slot,value,taken,f.error)==Delivery::Unchanged&&value.key==991); // wrong fixed pass
     f.Retire();NativeInputHead head;CHECK(f.transfers.CancelKeyboard(head,f.error)==sysEventTransfer_t::Ready);CHECK(Sys_EndKeyboardInputForRetirement(slice));f.Finish();}
}
static void OwnerBoundary() {
    {NativeInputDisposalReceipt old;
     {TerminalFixture f;f.Retire();CHECK(f.inventory.SealDisposal(old,f.error));
      std::thread worker([&]{std::string error;CHECK(!f.inventory.SealDisposal(old,error));});worker.join();
      CHECK(f.inventory.DisposalCurrent(old));CHECK(!f.transfers.Release());f.Finish();}
     TerminalFixture next;next.Retire();CHECK(!next.inventory.DisposalCurrent(old));next.Finish();}
    {TerminalFixture f;auto record=f.Open();auto event=Key();NativeTranslatedEmission emitted,taken;
     CHECK(f.transfers.SessionKey(record,event,NativeInputSessionQueue::Platform,emitted,f.error)==Status::StoredAdmitted);
     CHECK(f.ledger.SealRecord(record,NativeRecordDisposition::Emitted,f.error));CHECK(f.ledger.SealTranslation(f.error));
     f.facts.callback=[&]{sysEvent_t prefix{SE_CHAR,'z',0,0,nullptr};f.loop.PushEvent(&prefix);};
     sysEvent_t output{SE_CHAR,991,0,0,nullptr};
     CHECK(f.transfers.BeginSessionDelivery(output,taken,f.error)==Delivery::Unchanged&&output.evValue==991);
     CHECK(f.loop.GetEvent().evValue=='z');f.Retire();NativeInputHead head;CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Ready);f.Finish();}
    {TerminalFixture f;auto record=f.Open();NativeTranslatedKeyboard keyboard;NativeTranslatedEmission attempted;
     // An arbitrary callback during queue entry cannot use the owner's Issue authority.
     beforeLock=[&]{CHECK(!f.inventory.SessionCharacter(record,'x',attempted,f.error));};
     CHECK(f.transfers.Keyboard(record,'a',true,4,keyboard,f.error)==Status::StoredRetired);
     f.Retire();NativeInputHead head;CHECK(f.transfers.CancelKeyboard(head,f.error)==sysEventTransfer_t::Ready);f.Finish();}
    {TerminalFixture f;auto record=f.Open();NativeTranslatedKeyboard keyboard;
     CHECK(f.transfers.Keyboard(record,K_CTRL,true,4,keyboard,f.error)==Status::StoredAdmitted);
     NativeTranslatedEmission child;child.ticket.emission=991;
     CHECK(f.transfers.Deferred(keyboard,child,f.error)==Status::Unchanged&&child.ticket.emission==991);
     f.Retire();NativeInputHead head;CHECK(f.transfers.CancelKeyboard(head,f.error)==sysEventTransfer_t::Ready);f.Finish();}
    {TerminalFixture f;f.Retire();NativeInputDisposalReceipt receipt;
     std::thread worker([&]{std::string e;CHECK(!f.inventory.SealDisposal(receipt,e));CHECK(!f.inventory.BacklogDisposed());});worker.join();
     // A nested publication retires the outer proof. A fresh cleanup still works.
     f.facts.callback=[&]{NativeInputDisposalReceipt nested;CHECK(!f.inventory.SealDisposal(nested,f.error));};
     CHECK(!f.inventory.SealDisposal(receipt,f.error));f.Finish();}
    {TerminalFixture f;auto record=f.Open();auto event=Key('a',true);NativeTranslatedEmission emitted;
     CHECK(f.transfers.SessionKey(record,event,NativeInputSessionQueue::Platform,emitted,f.error)==Status::StoredAdmitted);
     f.Retire();NativeInputHead head;head.sequence=99;const auto old=head;
     onFree=[] {throw std::bad_alloc();};CHECK(f.transfers.CancelSession(head,f.error)==sysEventTransfer_t::Refused&&head==old);
     NativeInputDisposalReceipt receipt;CHECK(!f.inventory.SealDisposal(receipt,f.error));CHECK(!f.route.Release(f.facts.id));
     // The counted throw precedes the real free; a retained indeterminate entry
     // deliberately cannot guess this detail. Test owns final allocator cleanup.
     Mem_Free(reinterpret_cast<void*>(emitted.value.payload));}
}
int main(){
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    CHECK(SDL3_MAX_MOUSE_DELTA_PER_EVENT>=4&&SDL3_MAX_MOUSE_WHEEL_STEPS_PER_EVENT>=1);
    const auto legacyDeferred=deferred;
    AllStored(false);AllStored(true);DeferredAndPartialDelivery();FailureOwnership();RefusalsAndReentry();DeliveryOwnership();OwnerBoundary();
    CHECK(deferred==legacyDeferred);
    std::printf("NativeInputTerminalTest PASS %u checks; actual stores/owner/ledger, counted external facts only\n",checks);return 0;
}
