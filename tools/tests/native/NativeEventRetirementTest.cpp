// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Included after complete extracted production queue/slice methods and actual
// EventLoop declaration. Only external facts, allocator and engine sinks are doubles.
using namespace openq4;
using Transfer=sysEventTransfer_t;
using Permit=NativeInputRoute::CancellationPermit;
static_assert(SDL3_MAX_MOUSE_DELTA_PER_EVENT>=7&&SDL3_MAX_MOUSE_WHEEL_STEPS_PER_EVENT>=1,
    "Fixture inputs must remain within the production translator clamps");
static bool Same(const sysEvent_t& a,const sysEvent_t& b) {
    return std::memcmp(&a.evType,&b.evType,sizeof(a.evType))==0 && a.evValue==b.evValue &&
        a.evValue2==b.evValue2 && a.evPtrLength==b.evPtrLength && a.evPtr==b.evPtr;
}
struct RouteSource final:NativeInputRouteSource {
    NativeInputBinding original;
    std::uint64_t route=0;
    NativeInputIssued issued;
    mutable unsigned calls=0;
    bool retired=false;
    std::function<void()> onIssued;
    bool Observe(NativeInputObservation& out)const noexcept override {
        Check(lockDepth==0,"route Source observation outside storage lock");++calls;
        out={original.outer,original.editor.allocation,original.sessionTransition,Sys_EventDispositionEpoch(),
            Sys_EventQueueToken(),original.window,Sys_EventDispositionBoundThread(),true,true,true};return true;
    }
    bool Retirement(std::uint64_t id,const NativeInputBinding& b,NativeInputRetirement& out)const noexcept override {
        Check(lockDepth==0,"retirement proof outside storage lock");++calls;
        if(id!=route||b.editor!=original.editor||b.native!=original.native||b.window!=original.window)return false;
        out.route=id;out.native=b.native;out.window=b.window;
        out.ui=retired?NativeInputUiRetirement::RetiredExact:NativeInputUiRetirement::Unknown;
        out.store=out.provider=retired?NativeInputNativeRetirement::RetiredExact:NativeInputNativeRetirement::Unknown;
        return true;
    }
    bool InspectIssued(std::uint64_t id,const NativeInputBinding&,const sysEventDispositionTag_t&,NativeInputIssued& out)const noexcept override {
        Check(lockDepth==0,"issued ledger/translator inspection outside storage lock");++calls;
        if(id!=route)return false;
        if(onIssued)onIssued();
        out=issued;return true;
    }
};
struct Fixture {
    RouteSource source;
    NativeInputRoute route{source};
    Fixture() {
        Sys_ClearEvents();SDL3_ClearInputQueues();Check(Sys_BindEventDispositionThread()!=0,"original event thread binds");
        auto& b=source.original;b.outer=0x1000;b.sessionTransition=1;b.dispatchEpoch=Sys_EventDispositionEpoch();b.streamToken=Sys_EventQueueToken();
        b.window={0x2000,5,6,7,8,9};b.editor={10,11,12,13,5,14,15,"number.field"};b.native={16,17};
        std::string error;Check(route.Prepare(b,source.route,error),"actual route copies original binding");
    }
    sysEventDispositionTag_t Tag(std::uint64_t emission=40) const {
        const auto& b=source.original;return {b.dispatchEpoch,b.streamToken,b.window.module,source.route,b.window.window,b.window.lifetime,
            20,21,22,23,24,0,emission};
    }
    void Retire(bool retireEpoch=false) {
        Check(route.Revoke(source.route),"original route revokes");source.retired=true;
        if(retireEpoch)Check(Sys_RetireEventDispositionThread(),"active storage epoch retires without clearing queued ownership");
        Check(route.MarkDrainOnly(source.route),"independent exact retirement enables cancellation");
    }
    bool Prepare(const NativeInputHead& h,Permit& p,NativeInputKind kind=NativeInputKind::RouteKey) {
        const auto sink=h.lane==NativeInputLane::Keyboard?NativeInputSink::Keyboard:h.lane==NativeInputLane::Mouse?NativeInputSink::Mouse:NativeInputSink::Session;
        source.issued={h.tag,sink,kind,h.value,true,false,false}; // No Admit success is fabricated or required.
        return route.PrepareCancellation(h,p);
    }
};
static sysEvent_t Event(int value=30,void* payload=nullptr,int bytes=0) {
    return {payload?SE_RETAINED_UI:SE_KEY,value,1,bytes,payload};
}
static void PlatformAndPushed(bool pushed) {
    Fixture f;idEventLoop loop;void* pointer=Allocate(27);auto event=Event(50,pointer,27), original=event;auto tag=f.Tag(),originalTag=tag;
    Check(pushed?loop.PushEventWithDisposition(event,tag):Sys_QueTrackedEvent(event,tag),"actual tracked storage takes event and payload");
    Check(Same(event,sysEvent_t{})&&tag.Empty(),"successful admission consumes only caller ownership");
    NativeInputHead head;Check(loop.PeekEventForRetirement(head)==Transfer::Ready,"aggregate copied exact tagged head");
    Check(head.lane==(pushed?NativeInputLane::Pushed:NativeInputLane::Platform)&&head.tag==originalTag,"correct lane and immutable sidecar");
    Check(head.sequence&&head.value.payload==reinterpret_cast<std::uintptr_t>(pointer)&&head.value.payloadLength==27&&head.value.time==0,"nonreused queue identity and exact owned header");
    Check(head.value.type==static_cast<int>(original.evType)&&head.value.value==original.evValue&&head.value.value2==original.evValue2,"event footprint preserves the original type and both scalar values");
    auto output=Event(99);auto outputTag=f.Tag(100);const auto saved=output;const auto savedTag=outputTag;Permit permit;
    Check(loop.TakeEventForRetirement(f.route,permit,output,outputTag)==Transfer::Refused&&Same(output,saved)&&outputTag==savedTag,"unprepared take preserves outputs and payload");
    f.Retire(true);Check(f.Prepare(head,permit,NativeInputKind::RouteText),"old issued head permitted after epoch retirement without Admit");
    Check(loop.TakeEventWithDisposition(output,outputTag)==Transfer::Refused,"ordinary getter still refuses retired epoch");
    const auto calls=f.source.calls, beforeFree=frees;
    Check(loop.TakeEventForRetirement(f.route,permit,output,outputTag)==Transfer::Ready,"actual old head transfers once");
    Check(f.source.calls==calls&&frees==beforeFree,"take has no Source lookup or free callback");
    Check(Same(output,original)&&outputTag==originalTag&&!Freed(pointer),"caller receives original payload and tag exactly");
    ExpectBytes(pointer,false);Check(loop.TakeEventForRetirement(f.route,permit,event,tag)==Transfer::Empty,"replayed take owns no second entry");
    Sys_ClearEvents();loop.ClearPushedEvents();Check(!Freed(pointer),"clear cannot free transferred payload");Mem_Free(pointer);
    NativeInputHead sentinel;sentinel.sequence=991;const auto sentinelBefore=sentinel;
    Check(loop.PeekEventForRetirement(sentinel)==Transfer::Empty&&sentinel==sentinelBefore,"empty peek preserves output");
}
static void PushedOrderingAndJournal() {
    for(int journal:{0,1,2}) {
        Fixture f;idEventLoop loop;auto platform=Event(70);auto platformTag=f.Tag();Check(Sys_QueTrackedEvent(platform,platformTag),"platform queued");
        f.Retire();NativeInputHead ph;Check(loop.PeekEventForRetirement(ph)==Transfer::Ready,"platform initially first");Permit old;
        Check(f.Prepare(ph,old),"platform permit prepared before pushed insertion");
        auto pushed=Event(71);auto pushedTag=f.Tag(41);Check(loop.PushEventWithDisposition(pushed,pushedTag),"new pushed head owns precedence");
        auto out=Event(101);auto outTag=f.Tag(98);const auto saved=out;const auto savedTag=outTag;
        loop.com_journal.SetInteger(journal);const auto journalBefore=journalReads, realBefore=realEventReads;
        Check(loop.TakeEventForRetirement(f.route,old,out,outTag)==Transfer::Refused&&Same(out,saved)&&outTag==savedTag,"old platform permit cannot bypass pushed head");
        NativeInputHead head;Check(loop.PeekEventForRetirement(head)==Transfer::Ready&&head.lane==NativeInputLane::Pushed,"pushed peek remains first even after journal transition");
        Permit permit;Check(f.Prepare(head,permit),"pushed permit exact");
        Check(loop.TakeEventForRetirement(f.route,permit,out,outTag)==Transfer::Ready&&out.evValue==71,"pushed canceled before platform");
        Check(loop.PeekEventForRetirement(head)==Transfer::Ready&&head==ph,"platform ownership retained intact");
        Check(f.Prepare(head,permit),"fresh platform permit after prior permission invalidated");
        Check(loop.TakeEventForRetirement(f.route,permit,out,outTag)==Transfer::Ready&&out.evValue==70,"platform follows pushed");
        Check(journalReads==journalBefore&&realEventReads==realBefore,"retirement never queries journal or real event/pump boundary");
        loop.com_journal.SetInteger(0);
    }
    Fixture f;idEventLoop loop;auto event=Event(12);auto tag=f.Tag();Check(Sys_QueTrackedEvent(event,tag),"platform tagged pending");
    auto legacy=Event(13);loop.PushEvent(&legacy);NativeInputHead head;head.sequence=99;const auto saved=head;
    Check(loop.PeekEventForRetirement(head)==Transfer::Refused&&head==saved,"untagged pushed prefix obstructs tagged platform");
    Check(loop.GetEvent().evValue==13,"existing untagged pushed FIFO remains intact");
    Check(loop.PeekEventForRetirement(head)==Transfer::Ready&&head.lane==NativeInputLane::Platform,"only actual prefix removal exposes platform");
    f.Retire();Permit permit;Check(!f.Prepare(head,permit,NativeInputKind::WindowIntent),"window intent lacks old GUI cancellation authority");
    sysEvent_t out={};sysEventDispositionTag_t outTag;
    Check(loop.TakeEventForRetirement(f.route,permit,out,outTag)==Transfer::Refused,"system intent cannot be removed via empty permit");
    Check(!f.Prepare(head,permit,NativeInputKind::ProcessIntent),"process intent separately refused");
    Sys_ClearEvents();
    Fixture g;QueueLegacy(SE_CHAR,80);auto tracked=Event(81);auto gt=g.Tag();Check(Sys_QueTrackedEvent(tracked,gt),"tagged behind platform legacy prefix");
    Check(Sys_PeekEventForRetirement(head)==Transfer::Refused,"platform untagged prefix not skipped");
    Check(Sys_GetEvent().evValue==80,"legacy prefix still returned normally");Check(Sys_PeekEventForRetirement(head)==Transfer::Ready,"tagged follows original prefix");Sys_ClearEvents();
}
static void QueueReplayAndFailure(bool pushed) {
    Fixture f;idEventLoop loop;auto event=Event(90);auto tag=f.Tag();
    const auto admit=[&](sysEvent_t& e,sysEventDispositionTag_t& t){return pushed?loop.PushEventWithDisposition(e,t):Sys_QueTrackedEvent(e,t);};
    Check(admit(event,tag),"initial replay target admitted");f.Retire();NativeInputHead head;
    Check(loop.PeekEventForRetirement(head)==Transfer::Ready,"initial replay target observed");Permit permit;Check(f.Prepare(head,permit),"initial replay permit");
    sysEvent_t output={};sysEventDispositionTag_t outputTag;
    Check(loop.TakeEventForRetirement(f.route,permit,output,outputTag)==Transfer::Ready,"initial replay target taken");
    const int capacity=pushed?MAX_PUSHED_EVENTS:MAX_QUED_EVENTS;
    for(int i=1;i<capacity;++i) {
        auto ordinary=Event(i);
        if(pushed)loop.PushEvent(&ordinary);else QueueLegacy(SE_KEY,i);
        (void)loop.GetEvent();
    }
    Check(admit(output,outputTag),"same header/ticket reenters same physical ring slot");
    NativeInputHead newer;Check(loop.PeekEventForRetirement(newer)==Transfer::Ready,"reused ring head observed");
    Check(newer.slot==head.slot&&newer.tag==head.tag&&newer.value==head.value&&newer.sequence!=head.sequence,"identity never aliases matching ring index/header/ticket");
    auto sentinel=Event(99);auto st=f.Tag(50);const auto before=sentinel;const auto beforeTag=st;
    Check(loop.TakeEventForRetirement(f.route,permit,sentinel,st)==Transfer::Refused&&Same(sentinel,before)&&st==beforeTag,"replayed old permit cannot take replacement");
    Check(f.Prepare(newer,permit),"replacement needs new permit");
    if(pushed)loop.com_pushedEvents[newer.slot].evValue++;else eventQue[newer.slot].evValue++;
    Check(loop.TakeEventForRetirement(f.route,permit,sentinel,st)==Transfer::Refused,"changed real record invalidates prepared footprint");
    if(pushed)loop.ClearPushedEvents();else Sys_ClearEvents();
    Check(loop.TakeEventForRetirement(f.route,permit,sentinel,st)==Transfer::Empty,"clear does not replay a removed record");
    // Full admission failure preserves caller payload; existing queue ordering is unchanged.
    Fixture g;for(int i=0;i<capacity;++i){auto e=Event(i);if(pushed)loop.PushEvent(&e);else QueueLegacy(SE_KEY,i);}
    void* ptr=Allocate(14);auto held=Event(100,ptr,14),heldBefore=held;auto ht=g.Tag(),tagBefore=ht;
    Check(!admit(held,ht)&&Same(held,heldBefore)&&ht==tagBefore&&!Freed(ptr),"full tracked refusal retains caller payload and tag");
    if(pushed)loop.ClearPushedEvents();else Sys_ClearEvents();Mem_Free(ptr);
}
static void Polled(bool mouse,bool checked) {
    Fixture f;
    auto keyboard=sysKeyboardInputDisposition_t{K_CTRL,true,32,{f.Tag(),41}};
    auto m=sysMouseInputDisposition_t{M_DELTAX,7,33,f.Tag()};
    Check(mouse?Sys_QueMouseInputWithDisposition(m):Sys_QueKeyboardInputWithDisposition(keyboard),"tracked polled ring admitted");
    NativeInputHead ring;Check((mouse?Sys_PeekMouseInputForRetirement(ring):Sys_PeekKeyboardInputForRetirement(ring))==Transfer::Ready,"ring has stable head identity before polling");
    Check(ring.value.type==(mouse?SE_MOUSE:SE_KEY)&&ring.value.deferredEmission==(mouse?0u:41u),"polled footprint retains store type and exact reserved child");
    f.Retire();Permit previous;Check(f.Prepare(ring,previous,mouse?NativeInputKind::RouteMouse:NativeInputKind::RouteKey),"ring permit prepared");
    sysInputDispositionSlice_t slice;
    if(checked)Check(mouse?Sys_PollMouseInputWithDisposition(slice):Sys_PollKeyboardInputWithDisposition(slice),"actual checked poll moves ownership into slice");
    NativeInputHead head;Check((mouse?Sys_PeekMouseInputForRetirement(head):Sys_PeekKeyboardInputForRetirement(head))==Transfer::Ready,"actual current poll/ring head copied");
    if(checked) {
        Check(head.sequence==slice.serial&&head.sequence!=ring.sequence&&head.value==ring.value,"ring to slice movement mints different shared-namespace identity");
        Check((mouse?Sys_TakeMouseInputForRetirement(f.route,previous,m):Sys_TakeKeyboardInputForRetirement(f.route,previous,keyboard))==Transfer::Refused,"ring permit cannot take later checked slice");
        Check(!(mouse?Sys_EndMouseInputForRetirement(slice):Sys_EndKeyboardInputForRetirement(slice)),"nonempty slice cannot be ended as retirement");
    }
    Permit permit;Check(f.Prepare(head,permit,mouse?NativeInputKind::RouteMouse:NativeInputKind::RouteKey),"actual current footprint permitted");
    if(!mouse) {
        auto& actual=checked?s_polledKeyboardDisposition[s_keyboardDispositionState.next]:s_keyboardDisposition[s_keyboardTail];
        actual.deferredEmission+=2;
        Check(Sys_TakeKeyboardInputForRetirement(f.route,permit,keyboard)==Transfer::Refused,"different valid deferred child cannot reuse parent permit");
        actual.deferredEmission-=2;
    }
    Check(Sys_RetireEventDispositionThread(),"active poll epoch retired");
    const auto calls=f.source.calls;const int queuedBefore=eventHead-eventTail;const auto deferredBefore=deferred,pumpsBefore=pumps;
    Check((mouse?Sys_TakeMouseInputForRetirement(f.route,permit,m):Sys_TakeKeyboardInputForRetirement(f.route,permit,keyboard))==Transfer::Ready,"stale poll/ring transfers on original bound thread");
    Check(f.source.calls==calls&&eventHead-eventTail==queuedBefore&&deferred==deferredBefore&&pumps==pumpsBefore,"no Source lookup, delayed child emission or pump under retirement take");
    if(mouse)Check(m.action==M_DELTAX&&m.value==7&&m.time==33&&m.disposition==f.Tag(),"mouse scalar/tag unchanged");
    else Check(keyboard.key==K_CTRL&&keyboard.down&&keyboard.time==32&&keyboard.disposition.parent==f.Tag()&&keyboard.disposition.deferredEmission==41,"parent and reserved keyboard child transfer together without execution");
    Check((mouse?Sys_TakeMouseInputForRetirement(f.route,permit,m):Sys_TakeKeyboardInputForRetirement(f.route,permit,keyboard))==Transfer::Empty,"polled/ring slot cannot transfer twice");
    if(checked) {
        auto wrong=slice;wrong.serial++;Check(!(mouse?Sys_EndMouseInputForRetirement(wrong):Sys_EndKeyboardInputForRetirement(wrong)),"wrong stale slice cannot release ownership");
        Check(mouse?Sys_EndMouseInputForRetirement(slice):Sys_EndKeyboardInputForRetirement(slice),"exact stale empty slice releases metadata only");
        Check(!(mouse?Sys_EndMouseInputForRetirement(slice):Sys_EndKeyboardInputForRetirement(slice)),"old End receipt cannot end another slice");
    }
    SDL3_ClearInputQueues();
}
static void PrefixesAndThreads() {
    for(bool mouse:{false,true}) {
        Fixture f;
        if(mouse)SDL3_QueueMouseInput(M_DELTAX,3,9);else SDL3_QueueKeyboardInput('a',true,9);
        auto key=sysKeyboardInputDisposition_t{'b',true,10,{f.Tag(),0}};auto m=sysMouseInputDisposition_t{M_DELTAX,4,10,f.Tag()};
        Check(mouse?Sys_QueMouseInputWithDisposition(m):Sys_QueKeyboardInputWithDisposition(key),"tagged follows legacy polled prefix");
        NativeInputHead head;head.sequence=99;const auto saved=head;
        Check((mouse?Sys_PeekMouseInputForRetirement(head):Sys_PeekKeyboardInputForRetirement(head))==Transfer::Refused&&head==saved,"retirement cannot skip untagged ring prefix");
        Check((mouse?Sys_PollMouseInputEvents():Sys_PollKeyboardInputEvents())==1,"legacy poll preserves prefix and stops before tagged item");
        Check((mouse?Sys_PeekMouseInputForRetirement(head):Sys_PeekKeyboardInputForRetirement(head))==Transfer::Refused,"unread legacy slice blocks ring retirement");
        if(mouse){int a=0,v=0;Check(Sys_ReturnMouseInputEvent(0,a,v)==1&&a==M_DELTAX&&v==3,"legacy mouse prefix retained");Sys_EndMouseInputEvents();}
        else {int ch=0;bool down=false;Check(Sys_ReturnKeyboardInputEvent(0,ch,down)=='a'&&down,"legacy keyboard prefix retained");Sys_EndKeyboardInputEvents();}
        Check((mouse?Sys_PeekMouseInputForRetirement(head):Sys_PeekKeyboardInputForRetirement(head))==Transfer::Ready,"only released prefix exposes tagged ring");
        f.Retire();Permit permit;Check(f.Prepare(head,permit,mouse?NativeInputKind::RouteMouse:NativeInputKind::RouteKey),"stale continuity after legacy obstruction can be canceled exactly");
        const auto calls=f.source.calls;const auto before=head;
        std::thread worker([&] {
            Check((mouse?Sys_PeekMouseInputForRetirement(head):Sys_PeekKeyboardInputForRetirement(head))==Transfer::Refused&&head==before,"wrong-thread retirement peek preserves output");
            Check((mouse?Sys_TakeMouseInputForRetirement(f.route,permit,m):Sys_TakeKeyboardInputForRetirement(f.route,permit,key))==Transfer::Refused,"wrong-thread poll retirement refused");
        });worker.join();Check(f.source.calls==calls,"worker cannot call source through storage");
        beforeLock=[&]{if(mouse)s_mouseQueue[s_mouseTail].value++;else s_keyboardQueue[s_keyboardTail].time++;};
        Check((mouse?Sys_TakeMouseInputForRetirement(f.route,permit,m):Sys_TakeKeyboardInputForRetirement(f.route,permit,key))==Transfer::Refused,"head recomputed after acquiring actual owning lock");
        SDL3_ClearInputQueues();
    }
    Fixture f;auto event=Event(42);auto tag=f.Tag();Check(Sys_QueTrackedEvent(event,tag),"thread fixture pending platform");idEventLoop loop;
    NativeInputHead head;Check(loop.PeekEventForRetirement(head)==Transfer::Ready,"platform thread fixture observed");f.Retire();Permit permit;Check(f.Prepare(head,permit),"platform thread fixture permitted");
    const auto saved=head;std::thread worker([&]{Check(loop.PeekEventForRetirement(head)==Transfer::Refused&&head==saved,"wrong-thread aggregate peek refused");Check(loop.TakeEventForRetirement(f.route,permit,event,tag)==Transfer::Refused,"wrong-thread aggregate take refused");
        Check(Sys_PeekEventForRetirement(head)==Transfer::Refused&&head==saved,"wrong-thread direct platform peek refused");Check(Sys_TakeEventForRetirement(f.route,permit,event,tag)==Transfer::Refused,"wrong-thread direct platform take refused");});worker.join();Sys_ClearEvents();
}
static void CheckedSliceOrder() {
    for(bool mouse:{false,true}) {
        Fixture f;
        sysKeyboardInputDisposition_t key{'a',true,12,{f.Tag(),0}};
        sysMouseInputDisposition_t m{M_DELTAX,6,12,f.Tag()};
        Check(mouse?Sys_QueMouseInputWithDisposition(m):Sys_QueKeyboardInputWithDisposition(key),"first checked item queued");
        sysInputDispositionSlice_t slice;Check(mouse?Sys_PollMouseInputWithDisposition(slice):Sys_PollKeyboardInputWithDisposition(slice),"first checked slice acquired");
        key={'b',false,13,{f.Tag(42),0}};m={M_DELTAY,8,13,f.Tag(42)};
        Check(mouse?Sys_QueMouseInputWithDisposition(m):Sys_QueKeyboardInputWithDisposition(key),"later ring item remains behind retained slice");
        f.Retire(true);NativeInputHead first;Check((mouse?Sys_PeekMouseInputForRetirement(first):Sys_PeekKeyboardInputForRetirement(first))==Transfer::Ready,"slice item remains first");
        Check(first.sequence==slice.serial&&first.tag.emission==40,"retained slice exact origin");
        Permit permit;Check(f.Prepare(first,permit,mouse?NativeInputKind::RouteMouse:NativeInputKind::RouteKey),"first checked cancellation prepared");
        Check((mouse?Sys_TakeMouseInputForRetirement(f.route,permit,m):Sys_TakeKeyboardInputForRetirement(f.route,permit,key))==Transfer::Ready,"first checked item transferred");
        NativeInputHead next;next.sequence=901;const auto unchanged=next;
        Check((mouse?Sys_PeekMouseInputForRetirement(next):Sys_PeekKeyboardInputForRetirement(next))==Transfer::Empty&&next==unchanged,"empty retained slice still precedes ring and preserves output");
        for(int field=0;field<5;++field) {
            auto changed=slice;
            if(field==0)changed.epoch++;
            if(field==1)changed.streamToken++;
            if(field==2)changed.serial++;
            if(field==3)changed.count++;
            if(field==4)changed.lane=mouse?sysInputDispositionLane_t::Keyboard:sysInputDispositionLane_t::Mouse;
            Check(!(mouse?Sys_EndMouseInputForRetirement(changed):Sys_EndKeyboardInputForRetirement(changed)),"empty End requires every original slice identity field");
        }
        std::thread other([&]{Check(!(mouse?Sys_EndMouseInputForRetirement(slice):Sys_EndKeyboardInputForRetirement(slice)),"wrong-thread empty End refused");});other.join();
        Check(mouse?Sys_EndMouseInputForRetirement(slice):Sys_EndKeyboardInputForRetirement(slice),"exact stale empty End releases only slice metadata");
        Check((mouse?Sys_PeekMouseInputForRetirement(next):Sys_PeekKeyboardInputForRetirement(next))==Transfer::Ready&&next.tag.emission==42&&next.sequence>first.sequence,"later ring exposed only after empty End with shared nonreused identity");
        Check((mouse?Sys_TakeMouseInputForRetirement(f.route,permit,m):Sys_TakeKeyboardInputForRetirement(f.route,permit,key))==Transfer::Refused,"earlier slice permit cannot take later ring");
        Check(f.Prepare(next,permit,mouse?NativeInputKind::RouteMouse:NativeInputKind::RouteKey),"later ring requires fresh permission");
        Check((mouse?Sys_TakeMouseInputForRetirement(f.route,permit,m):Sys_TakeKeyboardInputForRetirement(f.route,permit,key))==Transfer::Ready,"later ring transferred once");
        // A fresh route after clear does not reset the shared storage namespace.
        const auto previous=next.sequence;
        Fixture fresh;key={'c',true,14,{fresh.Tag(),0}};m={M_DELTAX,1,14,fresh.Tag()};
        Check(mouse?Sys_QueMouseInputWithDisposition(m):Sys_QueKeyboardInputWithDisposition(key),"fresh route admits after clear/rebind");
        Check((mouse?Sys_PeekMouseInputForRetirement(next):Sys_PeekKeyboardInputForRetirement(next))==Transfer::Ready&&next.sequence>previous,"clear/rebind never reuses ring serial");
        Check((mouse?Sys_TakeMouseInputForRetirement(f.route,permit,m):Sys_TakeKeyboardInputForRetirement(f.route,permit,key))==Transfer::Refused,"old route cannot cancel fresh-route record");
        SDL3_ClearInputQueues();
    }
}
static void PolledBoundaryRefusals() {
    Fixture f;SDL3_QueueKeyboardInput('a',true,1);
    sysKeyboardInputDisposition_t key{'b',true,2,{f.Tag(),0}};Check(Sys_QueKeyboardInputWithDisposition(key),"checked mix queued");
    sysInputDispositionSlice_t slice;Check(Sys_PollKeyboardInputWithDisposition(slice),"checked slice retains untagged prefix");
    NativeInputHead head;Check(Sys_PeekKeyboardInputForRetirement(head)==Transfer::Refused,"checked untagged prefix obstructs exact native cancellation");
    sysInputDispositionSlot_t slot;Check(Sys_PeekKeyboardInputWithDisposition(slice,slot,key)==Transfer::Ready&&key.disposition.Empty(),"ordinary checked API still sees exact untagged prefix");
    Check(Sys_TakeKeyboardInputWithDisposition(slot,key)==Transfer::Ready&&key.key=='a',"ordinary prefix taken without retirement inference");
    f.Retire();Check(Sys_PeekKeyboardInputForRetirement(head)==Transfer::Ready,"tagged now first in checked slice");Permit permit;Check(f.Prepare(head,permit),"checked tagged permit");
    for(int field=0;field<4;++field) {
        const auto before=s_keyboardDispositionState.slice;
        if(field==0)s_keyboardDispositionState.slice.epoch++;
        if(field==1)s_keyboardDispositionState.slice.streamToken++;
        if(field==2)s_keyboardDispositionState.slice.lane=sysInputDispositionLane_t::Mouse;
        if(field==3)s_polledKeyboardCount++;
        Check(Sys_TakeKeyboardInputForRetirement(f.route,permit,key)==Transfer::Refused,"invalid actual slice cannot reuse head authority");
        s_keyboardDispositionState.slice=before;if(field==3)--s_polledKeyboardCount;
    }
    Check(Sys_TakeKeyboardInputForRetirement(f.route,permit,key)==Transfer::Ready,"restored exact slice still owns original item");
    Check(Sys_EndKeyboardInputForRetirement(slice),"all actual checked entries taken before End");
    // Saturation must be checked before stealing even an empty normal poll slice.
    const auto counter=s_inputDispositionHighwater;s_inputDispositionHighwater=(std::numeric_limits<std::uint64_t>::max)();
    auto out=slice;Check(!Sys_PollKeyboardInputWithDisposition(out)&&out==slice&&!s_keyboardDispositionState.checked,"poll counter exhaustion preserves caller and storage");s_inputDispositionHighwater=counter;
}
static void ReentrantRetirement() {
    for(int lane=0;lane<4;++lane) {
        Fixture f;idEventLoop loop;auto event=Event(60);auto tag=f.Tag();
        sysKeyboardInputDisposition_t key{'a',true,1,{tag,0}};sysMouseInputDisposition_t mouse{M_DELTAX,1,1,tag};
        Check(lane==0?Sys_QueTrackedEvent(event,tag):lane==1?loop.PushEventWithDisposition(event,tag):
            lane==2?Sys_QueKeyboardInputWithDisposition(key):Sys_QueMouseInputWithDisposition(mouse),"reentry fixture admitted in actual store");
        f.Retire();NativeInputHead head;
        const auto peek=[&]{return lane<2?loop.PeekEventForRetirement(head):lane==2?Sys_PeekKeyboardInputForRetirement(head):Sys_PeekMouseInputForRetirement(head);};
        Check(peek()==Transfer::Ready,"reentry fixture exact head");Permit permit;
        Check(f.Prepare(head,permit,lane==3?NativeInputKind::RouteMouse:NativeInputKind::RouteKey),"old permit prepared");
        const auto previous=permit;
        const auto take=[&](const Permit& p){return lane<2?loop.TakeEventForRetirement(f.route,p,event,tag):lane==2?
            Sys_TakeKeyboardInputForRetirement(f.route,p,key):Sys_TakeMouseInputForRetirement(f.route,p,mouse);};
        f.source.onIssued=[&]{Check(take(previous)==Transfer::Refused,"actual storage cannot take through reentrant route proof");};
        Check(!f.Prepare(head,permit,lane==3?NativeInputKind::RouteMouse:NativeInputKind::RouteKey),"outer proof sees nested storage protocol fault");
        f.source.onIssued={};
        const auto before=head;Check(peek()==Transfer::Ready&&head==before,"failed reentry leaves exact queued ownership intact");
        Check(take(previous)==Transfer::Refused,"old permit invalidated by reentry");
        Check(f.Prepare(head,permit,lane==3?NativeInputKind::RouteMouse:NativeInputKind::RouteKey),"fresh retirement can dispose poisoned old backlog");
        Check(take(permit)==Transfer::Ready,"faulted old head transfers only through fresh permission");
        NativeInputSelection selected;Check(!f.route.Probe(selected),"cleanup never restores live GUI eligibility");
    }
}
static void Saturation() {
    Fixture f;idEventLoop loop;
    for(int kind=0;kind<3;++kind) {
        auto& highwater=kind==0?eventRetirementHighwater:kind==1?pushedRetirementHighwater:s_inputDispositionHighwater;
        highwater=(std::numeric_limits<std::uint64_t>::max)();
        auto event=Event(30),saved=event;auto tag=f.Tag(),savedTag=tag;
        if(kind<2)Check(!(kind==0?Sys_QueTrackedEvent(event,tag):loop.PushEventWithDisposition(event,tag))&&Same(event,saved)&&tag==savedTag,"saturated platform/pushed identity retains caller ownership");
        else {
            sysKeyboardInputDisposition_t k{'a',true,1,{tag,0}};sysMouseInputDisposition_t m{M_DELTAX,1,1,tag};
            Check(!Sys_QueKeyboardInputWithDisposition(k)&&k.disposition.parent==tag&&k.key=='a',"saturated shared SDL counter refuses keyboard transfer");
            Check(!Sys_QueMouseInputWithDisposition(m)&&m.disposition==tag&&m.value==1,"saturated shared SDL counter refuses mouse transfer");
        }
    }
    Sys_ClearEvents();loop.ClearPushedEvents();SDL3_ClearInputQueues();
    Check(eventRetirementHighwater==(std::numeric_limits<std::uint64_t>::max)()&&pushedRetirementHighwater==eventRetirementHighwater&&s_inputDispositionHighwater==eventRetirementHighwater,"clear never resets any identity namespace");
}
int main() {
    PlatformAndPushed(false);PlatformAndPushed(true);PushedOrderingAndJournal();QueueReplayAndFailure(false);QueueReplayAndFailure(true);
    for(bool mouse:{false,true})for(bool checked:{false,true})Polled(mouse,checked);
    PrefixesAndThreads();CheckedSliceOrder();PolledBoundaryRefusals();ReentrantRetirement();Saturation();
    for(const auto& b:blocks)Check(b.freed,"all allocated payload ownership accounted at test end");
    std::printf("NativeEventRetirementTest checks=%u failures=0\n",checks);return 0;
}
