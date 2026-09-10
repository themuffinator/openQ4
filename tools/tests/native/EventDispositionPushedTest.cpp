// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Included after actual EventLoop.cpp and its existing counted journal fixture.
#include <thread>
static sysEventDispositionTag_t Tag() {
    return {Sys_EventDispositionEpoch(),Sys_EventQueueToken(),7,8,9,10,11,12,13,14,15,0,1};
}
static void PushedTransfer() {
    Reset();eventLoopLocal.Init();auto event=Live(SE_RETAINED_UI,Text("owned"));auto tag=Tag();const auto original=event;const auto originalTag=tag;
    Check(eventLoopLocal.PushEventWithDisposition(event,tag),"pushed admission succeeds");
    Check(Same(event,sysEvent_t{}) && tag.Empty(),"pushed success consumes both caller inputs");
    sysEvent_t output={};output.evValue=99;auto outputTag=Tag();
    Check(eventLoopLocal.TakeEventWithDisposition(output,outputTag)==sysEventTransfer_t::Ready && Same(output,original) && outputTag==originalTag,
        "pushed exact event and original route/window tag transfer together");
    Mem_Free(output.evPtr);const auto before=output;const auto beforeTag=outputTag;
    Check(eventLoopLocal.TakeEventWithDisposition(output,outputTag)==sysEventTransfer_t::Empty && Same(output,before) && outputTag==beforeTag,"empty pushed/platform take preserves outputs");
    // The source stub supplies ordinary events only; the full platform unit
    // independently tests tagged platform admission/dequeue. No success stub.
    queued.push_back(Live(SE_CHAR));queued.back().evValue=77;
    Check(eventLoopLocal.TakeEventWithDisposition(output,outputTag)==sysEventTransfer_t::Ready && output.evValue==77 && outputTag.Empty(),"empty pushed queue delegates to counted platform source once");
    Check(queued.empty(),"delegated platform event consumed exactly once");eventLoopLocal.Shutdown();
}
static void PushedLegacyGuard() {
    Reset();eventLoopLocal.Init();auto event=Live(SE_CONSOLE,Text("secret_password old-route"));auto tag=Tag();const auto pointer=event.evPtr;
    const auto originalTag=tag;Check(eventLoopLocal.PushEventWithDisposition(event,tag),"tracked console admitted");
    const auto token=Sys_EventQueueToken();Check(eventLoopLocal.GetEvent().evType==SE_NONE,"legacy EventLoop getter cannot reclassify tagged head");
    Check(Sys_EventQueueToken()!=token && owned.count(pointer)==1 && eventLoopLocal.com_pushedEventsHead-eventLoopLocal.com_pushedEventsTail==1,
        "legacy refusal preserves pending payload and invalidates continuity");
    sysEvent_t output={};output.evValue=94;auto outputTag=Tag();const auto before=output;const auto beforeTag=outputTag;
    Check(eventLoopLocal.TakeEventWithDisposition(output,outputTag)==sysEventTransfer_t::Refused && Same(output,before) && outputTag==beforeTag,
        "failed head cannot transfer after stream invalidation");
    Check(eventLoopLocal.com_pushedDisposition[eventLoopLocal.com_pushedEventsTail&(MAX_PUSHED_EVENTS-1)]==originalTag,"original immutable route remains on refused head");
    eventLoopLocal.RunEventLoop();Check(sessionObject.events.empty(),"normal delivery is disconnected from tagged queues");
    eventLoopLocal.Shutdown();Check(owned.empty() && releases==1,"shutdown frees pending tagged payload exactly once");
    Check(std::all_of(lastReleased.begin(),lastReleased.end(),[](unsigned char c){return c==0;}),"shutdown erases pending console bytes without external parser");
    Check(Sys_EventDispositionEpoch()==0,"shutdown retires dispatch epoch before later reuse");
}
static void PushedAdmissionFailures() {
    for(int which=0;which<7;++which) {
        Reset();eventLoopLocal.Init();auto event=Live(SE_RETAINED_UI,Text("caller"));auto tag=Tag();
        if(which==0)++tag.dispatchEpoch;
        if(which==1)++tag.streamToken;
        if(which==2)tag.route=0;
        if(which==3)eventLoopLocal.com_journal.SetInteger(1);
        if(which==4)eventLoopLocal.com_journal.SetInteger(2);
        if(which==5)event.evPtrLength=-1;
        if(which==6){const int type=99;std::memcpy(&event.evType,&type,sizeof(type));}
        const auto before=event;const auto beforeTag=tag;
        Check(!eventLoopLocal.PushEventWithDisposition(event,tag) && Same(event,before) && tag==beforeTag,"invalid/stale/journal admission preserves caller values");
        Check(eventLoopLocal.com_pushedEventsHead==eventLoopLocal.com_pushedEventsTail && owned.count(event.evPtr)==1,"failed admission transfers no payload");
        Mem_Free(event.evPtr);eventLoopLocal.com_journal.SetInteger(0);eventLoopLocal.Shutdown();
    }
    Reset();eventLoopLocal.Init();
    for(int n=0;n<MAX_PUSHED_EVENTS;++n){auto event=Live(SE_CHAR);event.evValue=n;eventLoopLocal.PushEvent(&event);}
    auto event=Live(SE_RETAINED_UI,Text("full"));auto tag=Tag();const auto before=event;const auto beforeTag=tag;const auto token=Sys_EventQueueToken();
    Check(!eventLoopLocal.PushEventWithDisposition(event,tag) && Same(event,before) && tag==beforeTag,"full pushed admission leaves caller ownership intact");
    Check(Sys_EventQueueToken()!=token && eventLoopLocal.com_pushedEventsHead-eventLoopLocal.com_pushedEventsTail==MAX_PUSHED_EVENTS,"full refusal invalidates without evicting any prior entry");
    Mem_Free(event.evPtr);for(int n=0;n<MAX_PUSHED_EVENTS;++n)Check(eventLoopLocal.GetEvent().evValue==n,"failed pushed admission preserves original FIFO");
    eventLoopLocal.Shutdown();
}
static void PushedThreadAndReset() {
    Reset();eventLoopLocal.Init();const auto epoch=Sys_EventDispositionEpoch();auto event=Live(SE_RETAINED_UI,Text("main"));auto tag=Tag();
    const auto before=event;const auto beforeTag=tag;bool admitted=true;sysEventTransfer_t taken=sysEventTransfer_t::Ready;
    std::thread worker([&]{admitted=eventLoopLocal.PushEventWithDisposition(event,tag);taken=eventLoopLocal.TakeEventWithDisposition(event,tag);});worker.join();
    Check(!admitted && taken==sysEventTransfer_t::Refused && Same(event,before) && tag==beforeTag,"worker cannot transfer pushed ownership");
    Check(Sys_EventDispositionEpoch()==epoch && eventLoopLocal.com_pushedEventsHead==eventLoopLocal.com_pushedEventsTail,"worker cannot mutate main epoch or queue");
    Check(eventLoopLocal.PushEventWithDisposition(event,tag),"main remains current after worker refusal");
    eventLoopLocal.Init();Check(owned.empty() && releases==1 && Sys_EventDispositionEpoch()>epoch,"reinitialization clears only pending old payload and starts fresh epoch");
    Check(eventLoopLocal.com_pushedEventsHead==0 && eventLoopLocal.com_pushedEventsTail==0,"pushed storage reset is empty");
    eventLoopLocal.Shutdown();
    // Local EventLoop instances start with fully initialized queue ownership.
    idEventLoop local;Check(local.com_pushedEventsHead==0 && local.com_pushedEventsTail==0,"non-global event loop storage initialized");
    for(const auto& current:local.com_pushedDisposition)Check(current.Empty(),"fresh pushed tag is empty");
}
static void PushedOverflowAndJournal() {
    Reset();eventLoopLocal.Init();auto event=Live(SE_RETAINED_UI,Text("victim"));auto tag=Tag();
    Check(eventLoopLocal.PushEventWithDisposition(event,tag),"tracked overflow victim admitted");
    for(int n=1;n<MAX_PUSHED_EVENTS;++n){event=Live(SE_CHAR);event.evValue=n;eventLoopLocal.PushEvent(&event);}
    const auto token=Sys_EventQueueToken();event=Live(SE_CHAR);event.evValue=MAX_PUSHED_EVENTS;eventLoopLocal.PushEvent(&event);
    Check(owned.empty() && releases==1 && Sys_EventQueueToken()!=token,"legacy overflow invalidates and frees tagged victim");
    for(int n=1;n<=MAX_PUSHED_EVENTS;++n)Check(eventLoopLocal.GetEvent().evValue==n,"legacy pushed FIFO survives tracked eviction");
    event=Live(SE_RETAINED_UI,Text("journal-blocked"));tag=Tag();Check(eventLoopLocal.PushEventWithDisposition(event,tag),"tracked head retained before journal transition");
    idFile journal;eventLoopLocal.com_journalFile=&journal;eventLoopLocal.com_journal.SetInteger(1);
    sysEvent_t output={};output.evValue=9;auto outputTag=Tag();const auto before=output;const auto beforeTag=outputTag;
    Check(eventLoopLocal.TakeEventWithDisposition(output,outputTag)==sysEventTransfer_t::Refused && Same(output,before) && outputTag==beforeTag,
        "tracked getter refuses journal mode without serialization or ownership transfer");
    Check(eventLoopLocal.GetEvent().evType==SE_NONE && journal.bytes.empty(),"legacy journal route cannot write tagged head");
    eventLoopLocal.Shutdown();Check(owned.empty() && releases==2,"shutdown cleans blocked journal-mode ownership");eventLoopLocal.com_journal.SetInteger(0);
}
int main() {
    LegacyJournalMain();PushedTransfer();PushedLegacyGuard();PushedAdmissionFailures();PushedThreadAndReset();PushedOverflowAndJournal();Reset();
    std::printf("PASS %u pushed disposition checks\n",checks);
}
