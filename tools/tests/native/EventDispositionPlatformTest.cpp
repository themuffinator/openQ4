// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Included after actual platform queue methods and the existing counted fixture.
#include <array>
#include <thread>

static sysEventDispositionTag_t Tag() {
    return {Sys_EventDispositionEpoch(), Sys_EventQueueToken(), 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1};
}
static bool SameEvent(const sysEvent_t& a, const sysEvent_t& b) {
    return std::memcmp(&a.evType,&b.evType,sizeof(a.evType))==0 && a.evValue==b.evValue && a.evValue2==b.evValue2 &&
        a.evPtrLength==b.evPtrLength && a.evPtr==b.evPtr;
}
static sysEvent_t Event(void* pointer=nullptr,int length=0) {
    sysEvent_t event{}; event.evType=SE_RETAINED_UI; event.evValue=41; event.evValue2=42;
    event.evPtr=pointer; event.evPtrLength=length; return event;
}
static void TrackedRoundTrip() {
    Sys_ClearEvents(); Check(Sys_BindEventDispositionThread()!=0,"initial constructing-thread epoch binds");
    const auto epoch=Sys_EventDispositionEpoch();
    Queue(SE_KEY,10,0,nullptr);
    auto event=Event(Allocate(18),18); auto tag=Tag(); const auto original=event; const auto originalTag=tag;
    Check(Sys_QueTrackedEvent(event,tag),"tracked admission transfers caller ownership");
    Check(SameEvent(event,sysEvent_t{}) && tag.Empty(),"successful admission clears caller event/tag together");
    Queue(SE_CHAR,30,0,nullptr);
    sysEvent_t out=Event(); auto outTag=Tag();
    Check(Sys_TakeEventWithDisposition(out,outTag)==sysEventTransfer_t::Ready && out.evValue==10 && outTag.Empty(),"checked consumer preserves preceding legacy FIFO entry");
    Check(Sys_TakeEventWithDisposition(out,outTag)==sysEventTransfer_t::Ready && SameEvent(out,original) && outTag==originalTag,"actual queue transfers exact payload and immutable sidecar");
    Check(!Freed(out.evPtr),"dequeue has not freed transferred payload"); Mem_Free(out.evPtr);
    Check(Sys_TakeEventWithDisposition(out,outTag)==sysEventTransfer_t::Ready && out.evValue==30 && outTag.Empty(),"checked consumer preserves following legacy FIFO entry");
    const auto before=out; const auto beforeTag=outTag;
    Check(Sys_TakeEventWithDisposition(out,outTag)==sysEventTransfer_t::Empty && SameEvent(out,before) && outTag==beforeTag,"empty take preserves outputs");
    Sys_ClearEvents(); Check(Sys_EventDispositionEpoch()==epoch,"queue invalidation does not reuse dispatch epoch");
}
static void AdmissionFailures() {
    for (int field=0;field<13;++field) {
        Sys_ClearEvents(); auto event=Event(Allocate(11),11); auto tag=Tag();
        std::array<std::uint64_t*,13> fields={&tag.dispatchEpoch,&tag.streamToken,&tag.providerEpoch,&tag.route,&tag.window,&tag.windowLifetime,
            &tag.ingress,&tag.batchSerial,&tag.ledger,&tag.ledgerSerial,&tag.queueSequence,&tag.recordIndex,&tag.emission};
        *fields[field]=field==11?8192:0;
        const auto before=event; const auto beforeTag=tag; const auto token=Sys_EventQueueToken();
        Check(!Sys_QueTrackedEvent(event,tag),"incomplete/out-of-range sidecar rejected");
        Check(SameEvent(event,before) && tag==beforeTag && eventHead==eventTail && !Freed(event.evPtr),"refusal preserves complete caller ownership and queue");
        Check(Sys_EventQueueToken()==token,"unadmitted malformed ticket cannot invalidate another valid queue"); Mem_Free(event.evPtr);
    }
    for (int invalid=0;invalid<5;++invalid) {
        Sys_ClearEvents(); auto event=Event(Allocate(11),11); auto tag=Tag(); const auto pointer=event.evPtr;
        if(invalid==0)event.evPtrLength=-1;
        if(invalid==1)event.evPtrLength=1024*1024+1;
        if(invalid==2)event.evPtrLength=0;
        if(invalid==3)event.evPtr=nullptr;
        if(invalid==4){const int type=99;std::memcpy(&event.evType,&type,sizeof(type));}
        const auto before=event;const auto beforeTag=tag;
        Check(!Sys_QueTrackedEvent(event,tag) && SameEvent(event,before) && tag==beforeTag,"invalid event shape refuses without partial publication");
        Check(eventHead==eventTail && !Freed(pointer),"invalid payload remains caller-owned"); Mem_Free(pointer);
    }
    Sys_ClearEvents(); auto event=Event();auto tag=Tag();++tag.dispatchEpoch;
    Check(!Sys_QueTrackedEvent(event,tag),"stale epoch cannot admit");tag=Tag();++tag.streamToken;
    Check(!Sys_QueTrackedEvent(event,tag),"stale continuity cannot admit");
}
static void LegacyRefusalAndClear() {
    Sys_ClearEvents();auto event=Event(Allocate(17),17);event.evType=SE_CONSOLE;auto tag=Tag();
    const auto original=event;const auto originalTag=tag;
    Check(Sys_QueTrackedEvent(event,tag),"tagged console queued");const auto token=Sys_EventQueueToken();
    Check(Sys_GetEvent().evType==SE_NONE,"legacy getter cannot expose tagged head");
    Check(Sys_EventQueueToken()!=token && eventHead-eventTail==1 && !Freed(original.evPtr),"legacy bypass invalidates and preserves original FIFO ownership");
    auto out=Event();auto outTag=Tag();const auto before=out;const auto beforeTag=outTag;
    Check(Sys_TakeEventWithDisposition(out,outTag)==sysEventTransfer_t::Refused && SameEvent(out,before) && outTag==beforeTag,"stale head cannot silently become tracked or legacy input");
    Check(eventDispositionTags[eventTail & MASK_QUED_EVENTS]==originalTag,"failed take leaves exact old route/window identity");
    Sys_ClearEvents();Check(Freed(original.evPtr),"explicit clear frees pending tagged payload once");ExpectBytes(original.evPtr,true);
    for(const auto& current:eventDispositionTags)Check(current.Empty(),"cleared or transferred slot never retains a tag");
}
static void FullAndOverflow() {
    Sys_ClearEvents();for(int i=0;i<MAX_QUED_EVENTS;++i)Queue(SE_CHAR,i,0,nullptr);
    auto event=Event(Allocate(21),21);auto tag=Tag();const auto before=event;const auto beforeTag=tag;
    const auto token=Sys_EventQueueToken();const auto oldWarnings=warnings;
    Check(!Sys_QueTrackedEvent(event,tag),"full tracked admission refuses");
    Check(SameEvent(event,before) && tag==beforeTag && !Freed(event.evPtr),"full refusal leaves caller payload/tag intact");
    Check(eventHead-eventTail==MAX_QUED_EVENTS && warnings==oldWarnings && Sys_EventQueueToken()!=token,"full refusal invalidates with no eviction or callback");
    for(int i=0;i<MAX_QUED_EVENTS;++i)Check(Sys_GetEvent().evValue==i,"failed tracked admission preserved original ordinary FIFO");
    Mem_Free(event.evPtr);
    Sys_ClearEvents();event=Event(Allocate(17),17);tag=Tag();const auto pointer=event.evPtr;
    Check(Sys_QueTrackedEvent(event,tag),"tracked overflow victim admitted");
    for(int i=1;i<MAX_QUED_EVENTS;++i)Queue(SE_CHAR,i,0,nullptr);
    const auto beforeOverflow=Sys_EventQueueToken();Queue(SE_CHAR,MAX_QUED_EVENTS,0,nullptr);
    Check(Freed(pointer) && Sys_EventQueueToken()!=beforeOverflow,"ordinary overflow retires and releases evicted tagged ownership");
    for(int i=1;i<=MAX_QUED_EVENTS;++i)Check(Sys_GetEvent().evValue==i,"legacy overflow behavior retains each surviving event");
    Sys_ClearEvents();
}
static void ThreadAndEpoch() {
    auto event=Event(Allocate(13),13);auto tag=Tag();const auto before=event;const auto beforeTag=tag;
    const auto epoch=Sys_EventDispositionEpoch();bool bind=true,retire=true,admit=true;sysEventTransfer_t taken=sysEventTransfer_t::Ready;
    std::thread worker([&]{bind=Sys_BindEventDispositionThread()!=0;retire=Sys_RetireEventDispositionThread();admit=Sys_QueTrackedEvent(event,tag);
        taken=Sys_TakeEventWithDisposition(event,tag);});worker.join();
    Check(!bind && !retire && !admit && taken==sysEventTransfer_t::Refused,"worker cannot bind/retire/admit/take on main storage epoch");
    Check(Sys_EventDispositionEpoch()==epoch && SameEvent(event,before) && tag==beforeTag && eventHead==eventTail,"worker refusal leaves all main ownership unchanged");
    Check(Sys_QueTrackedEvent(event,tag),"main owner still admits after worker refusal");
    Check(Sys_RetireEventDispositionThread() && Sys_EventDispositionEpoch()==0,"shutdown retires epoch");
    event=Event();tag=beforeTag;Check(Sys_TakeEventWithDisposition(event,tag)==sysEventTransfer_t::Refused,"retired head awaits explicit cleanup");
    Check(Sys_BindEventDispositionThread()>epoch,"new lifecycle uses strictly fresh epoch");
    Check(Sys_TakeEventWithDisposition(event,tag)==sysEventTransfer_t::Refused,"rebind cannot adopt old tagged queue");
    Sys_ClearEvents();Check(Freed(before.evPtr),"retired pending payload freed by explicit clear only");
    // This unit includes the actual epoch TU; bounded state setup reaches the
    // real saturating branch without billions of calls or a production test API.
    dispositionHighwater=(std::numeric_limits<std::uint64_t>::max)()-1;
    Check(Sys_BindEventDispositionThread()==(std::numeric_limits<std::uint64_t>::max)(),"last epoch is usable");
    Check(Sys_BindEventDispositionThread()==0 && Sys_EventDispositionEpoch()==0,"epoch exhaustion permanently refuses reuse");
    Check(Sys_RetireEventDispositionThread() && Sys_BindEventDispositionThread()==0,"retirement cannot reset highwater");
    Queue(SE_CHAR,7,0,nullptr);Check(Sys_GetEvent().evValue==7,"untracked legacy path works without a native storage epoch");Sys_ClearEvents();
}
int main() {
    LegacyQueueMain();TrackedRoundTrip();AdmissionFailures();LegacyRefusalAndClear();FullAndOverflow();ThreadAndEpoch();
    Check(frees==blocks.size(),"all new/legacy payload allocations released exactly once");
    std::printf("PASS %u disposition storage checks\n",checks);
}
