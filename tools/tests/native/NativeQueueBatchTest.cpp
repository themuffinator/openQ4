// Actual ingress/codec/source and extracted generation query; no native input.
#include "src/sys/sdl3/NativeQueueSource.h"
#include <SDL3/SDL_init.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace openq4;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';std::exit(1);} } while(0)
static bool mainThread=true,providerHealthy=true,oq4_queue_active=true;
static Uint64 oq4_queue_generation=3,engineToken=7;
static unsigned probes=0,polls=0,copies=0,locks=0,unlocks=0;
static std::function<void()> tokenHook;
struct { bool active=true; void* lock=nullptr; } SDL_EventQ;
extern "C" bool SDLCALL SDL_IsMainThread(){++probes;return mainThread;}
static void SDL_LockMutex(void*){++locks;}
static void SDL_UnlockMutex(void*){++unlocks;}
static bool OQ4_WIN_QueueHealthy(){return providerHealthy;}
#include "queue-generation.inc"
static std::optional<OQ4_NativeFence> providerFence;
extern "C" bool SDLCALL OQ4_WindowsNativeFenceHealthy(){++probes;return providerHealthy;}
extern "C" Uint32 SDLCALL OQ4_WindowsNativeFenceEventType(){++probes;return 0x9000;}
extern "C" bool SDLCALL OQ4_WindowsNativeFencePending(OQ4_NativeFence*out){++probes;if(!providerFence)return false;*out=*providerFence;return true;}
extern "C" int SDLCALL OQ4_WindowsNativeFencePoll(SDL_Event*,OQ4_NativeQueueRecord*){++polls;return 0;}
extern "C" bool SDLCALL OQ4_WindowsNativeFenceCopy(const SDL_Event*,OQ4_NativeFence*out){++copies;if(!providerFence)return false;*out=*providerFence;return true;}
std::uint64_t Sys_EventQueueToken(){++probes;const auto result=engineToken;if(tokenHook){auto h=std::move(tokenHook);tokenHook={};h();}return result;}
struct Source final:NativeQueueSource {
    NativeQueueStatus status{true,true,1,3,7,0x9000,{}};
    std::deque<std::pair<SDL_Event,OQ4_NativeQueueRecord>> queue;
    std::function<void()> observeHook,pollHook,copyHook;
    std::string borrowed;
    bool okay=true,copyOkay=true;
    unsigned observes=0,pollCount=0,copyCount=0;
    Uint64 sequence=0;
    bool Observe(NativeQueueStatus& out,std::string&) override {
        ++observes;if(observeHook)observeHook();if(!okay)return false;out=status;return true;
    }
    int Poll(SDL_Event& event,OQ4_NativeQueueRecord& record) override {
        ++pollCount;if(pollHook)pollHook();if(queue.empty())return 0;
        event=queue.front().first;record=queue.front().second;queue.pop_front();return 1;
    }
    bool CopyFence(const SDL_Event&,OQ4_NativeFence& out) override {
        ++copyCount;if(copyHook)copyHook();if(!copyOkay||!status.pending)return false;out=*status.pending;return true;
    }
    void Add(Uint32 type,Uint32 kind=OQ4_QUEUE_OUTSIDE,Uint32 ordinal=0) {
        SDL_Event e{};e.type=type;e.common.timestamp=sequence*10;
        OQ4_NativeQueueRecord r{1,kind,ordinal,0,status.generation,++sequence,0,0};
        if(kind==OQ4_QUEUE_COLLECTION)r.dispatch=status.pending->dispatch;
        if(kind==OQ4_QUEUE_FENCE){r.dispatch=status.pending->dispatch;r.fence_sequence=status.pending->sequence;e.user.code=19;}
        queue.emplace_back(e,r);
    }
    void Group(unsigned count){status.pending=OQ4_NativeFence{1,count,11,12};}
    void Fence(){Add(status.fenceEventType,OQ4_QUEUE_FENCE,status.pending->event_count);}
    void Retired(){status.healthy=false;status.generation=0;status.pending.reset();}
    void Fresh(Uint64 generation){status.healthy=true;status.generation=generation;status.pending.reset();queue.clear();}
};
static std::string error;
using Batch=std::unique_ptr<const NativeQueueBatch>;
static void Stream(){
    Source s;s.Group(2);s.Add(SDL_EVENT_KEY_DOWN);s.Add(SDL_EVENT_POLL_SENTINEL,OQ4_QUEUE_SENTINEL);
    s.sequence+=2;s.Add(SDL_EVENT_TEXT_INPUT,OQ4_QUEUE_COLLECTION,1);
    s.borrowed="value \xc3\xa9";s.queue.back().first.text.text=s.borrowed.c_str();
    s.Add(SDL_EVENT_CLIPBOARD_UPDATE);s.queue.back().first.clipboard.mime_types=reinterpret_cast<const char**>(1);
    s.Add(SDL_EVENT_KEY_UP,OQ4_QUEUE_COLLECTION,2);s.Fence();
    s.pollHook=[&]{if(s.pollCount==4)std::fill(s.borrowed.begin(),s.borrowed.end(),'x');};
    NativeQueueIngress ingress;Batch out;
    CHECK(ingress.Read(s,out,error)==NativeQueueRead::Ready);CHECK(out&&out->Events().size()==6);
    CHECK(out->Events()[2].text=="value \xc3\xa9");CHECK(out->Events()[2].header.text.text==nullptr);
    CHECK(out->Events()[3].ignored&&out->Events()[3].header.clipboard.mime_types==nullptr);
    CHECK(out->Events()[1].record.kind==OQ4_QUEUE_SENTINEL);CHECK(out->Events()[2].record.queue_sequence==5);
    CHECK(s.copyCount==1);const auto receipt=out->Receipt();CHECK(ingress.Validate(s,receipt,error));
    CHECK(!ingress.Finish(s,receipt,error));CHECK(!ingress.NeedsRetirement());
    const auto* address=out.get();CHECK(ingress.Read(s,out,error)==NativeQueueRead::Busy&&out.get()==address);
    NativeQueueIngress other;Source otherSource;otherSource.Add(SDL_EVENT_KEY_DOWN);Batch otherBatch;
    CHECK(other.Read(otherSource,otherBatch,error)==NativeQueueRead::Ready);CHECK(!other.Validate(otherSource,receipt,error));
    s.status.pending.reset();CHECK(ingress.Finish(s,receipt,error));CHECK(!ingress.Finish(s,receipt,error));
    CHECK(ingress.Read(s,out,error)==NativeQueueRead::Empty&&out.get()==address);
    s.Add(SDL_EVENT_DROP_FILE);s.queue.back().first.drop.data=reinterpret_cast<const char*>(1);
    CHECK(ingress.Read(s,out,error)==NativeQueueRead::Ready);CHECK(out->Receipt()!=receipt);
    CHECK(out->Events()[0].ignored&&out->Events()[0].header.drop.data==nullptr);
}
static void Dynamic(){
    Source s;s.Group(2);s.Add(SDL_EVENT_TEXT_EDITING,OQ4_QUEUE_COLLECTION,1);
    std::string preedit="\xf0\x9f\x8e\xae";s.queue.back().first.edit.text=preedit.c_str();s.queue.back().first.edit.start=2;s.queue.back().first.edit.length=-1;
    s.Add(SDL_EVENT_TEXT_EDITING_CANDIDATES,OQ4_QUEUE_COLLECTION,2);
    std::string a="one",b="\xd0\xb4\xd0\xb2\xd0\xb0";const char* strings[]={a.c_str(),b.c_str()};
    s.queue.back().first.edit_candidates.candidates=strings;s.queue.back().first.edit_candidates.num_candidates=2;s.queue.back().first.edit_candidates.selected_candidate=1;s.Fence();
    NativeQueueIngress ingress;Batch out;CHECK(ingress.Read(s,out,error)==NativeQueueRead::Ready);
    preedit="changed";a="new";b="new";strings[0]=nullptr;
    CHECK(out->Events()[0].text=="\xf0\x9f\x8e\xae"&&out->Events()[0].header.edit.start==2&&out->Events()[0].header.edit.length==-1);
    CHECK(out->Events()[1].candidates==std::vector<std::string>({"one","\xd0\xb4\xd0\xb2\xd0\xb0"}));
    CHECK(out->Events()[1].header.edit_candidates.candidates==nullptr);
    Source invalid;invalid.Add(SDL_EVENT_TEXT_INPUT);invalid.queue.back().first.text.text="\xed\xa0\x80";
    NativeQueueIngress refused;const auto* saved=out.get();
    CHECK(refused.Read(invalid,out,error)==NativeQueueRead::RetireRequired&&out.get()==saved);
    CHECK(ingress.Validate(s,out->Receipt(),error));
    for(int bad=0;bad<8;++bad){
        Source f;f.Add(SDL_EVENT_TEXT_EDITING_CANDIDATES);const char* items[]={"okay",nullptr};
        auto&e=f.queue.back().first.edit_candidates;e.candidates=items;e.num_candidates=1;e.selected_candidate=-1;
        if(bad==0)e.num_candidates=-1;
        if(bad==1)e.num_candidates=257;
        if(bad==2)e.candidates=nullptr;
        if(bad==3)e.selected_candidate=1;
        if(bad==4)e.selected_candidate=-2;
        if(bad==5)e.num_candidates=2;
        if(bad==6)items[0]="\xc0\x80";
        if(bad==7){f.borrowed.assign(65537,'a');items[0]=f.borrowed.c_str();}
        NativeQueueIngress x;Batch candidate;CHECK(x.Read(f,candidate,error)==NativeQueueRead::RetireRequired);CHECK(!candidate&&x.NeedsRetirement());
    }
}
static void Malformed(){
    { Source s;s.Group(2);s.Add(SDL_EVENT_KEY_DOWN,OQ4_QUEUE_COLLECTION,2);s.Add(SDL_EVENT_KEY_UP,OQ4_QUEUE_COLLECTION,2);s.Fence();
      NativeQueueIngress ingress;Batch out;CHECK(ingress.Read(s,out,error)==NativeQueueRead::RetireRequired&&!out); }
    for(int bad=0;bad<18;++bad){
        Source s;s.Group(1);s.Add(SDL_EVENT_KEY_DOWN,OQ4_QUEUE_COLLECTION,1);s.Fence();
        auto&e=s.queue.front().first;auto&r=s.queue.front().second;
        if(bad==0)r.version=2;
        if(bad==1)r.reserved=1;
        if(bad==2)++r.generation;
        if(bad==3)r.queue_sequence=0;
        if(bad==4)r.kind=9;
        if(bad==5)++r.dispatch;
        if(bad==6)++r.ordinal;
        if(bad==7)r.fence_sequence=1;
        if(bad==8)e.type=SDL_EVENT_POLL_SENTINEL;
        if(bad==9)e.type=0x9876;
        if(bad==10)s.queue.back().second.ordinal=2;
        if(bad==11)s.queue.back().second.fence_sequence++;
        if(bad==12)s.queue.back().second.dispatch++;
        if(bad==13)s.queue.back().first.type=SDL_EVENT_KEY_UP;
        if(bad==14)s.queue.back().first.user.data1=reinterpret_cast<void*>(1);
        if(bad==15)s.queue.pop_back();
        if(bad==16)s.queue.back().second.queue_sequence=1;
        if(bad==17)s.copyOkay=false;
        NativeQueueIngress ingress;Batch out;CHECK(ingress.Read(s,out,error)==NativeQueueRead::RetireRequired);CHECK(!out&&ingress.NeedsRetirement());
        const auto pollsBefore=s.pollCount;CHECK(ingress.Read(s,out,error)==NativeQueueRead::RetireRequired&&s.pollCount==pollsBefore);
        CHECK(!ingress.ResetAfterRetirement(s,error));s.Retired();
        CHECK(!ingress.ResetAfterRetirement(s,error));
        std::unique_ptr<const NativeQueueQuarantine> quarantine;
        CHECK(ingress.TakeQuarantine(quarantine)&&quarantine);
        s.Fresh(3);CHECK(!ingress.ResetAfterRetirement(s,error));s.Retired();
        CHECK(ingress.ResetAfterRetirement(s,error));
        s.Fresh(3);CHECK(ingress.Read(s,out,error)==NativeQueueRead::RetireRequired);
        s.Retired();CHECK(ingress.ResetAfterRetirement(s,error));s.Fresh(4);s.Add(SDL_EVENT_KEY_UP);
        CHECK(ingress.Read(s,out,error)==NativeQueueRead::Ready);
    }
}
static void ProbeFailures(){
    Source baseline;baseline.Group(1);baseline.Add(SDL_EVENT_KEY_DOWN,OQ4_QUEUE_COLLECTION,1);baseline.Fence();
    NativeQueueIngress normal;Batch old;CHECK(normal.Read(baseline,old,error)==NativeQueueRead::Ready);
    const auto count=baseline.observes;
    for(unsigned point=1;point<=count;++point)for(int field=0;field<7;++field){
        Source s;s.Group(1);s.Add(SDL_EVENT_KEY_DOWN,OQ4_QUEUE_COLLECTION,1);s.Fence();
        s.observeHook=[&]{if(s.observes==point){switch(field){case 0:s.status.healthy=false;break;case 1:s.status.generation++;break;case 2:s.status.engineToken++;break;case 3:s.status.providerEpoch++;break;case 4:s.status.pending->sequence++;break;case 5:s.okay=false;break;case 6:throw std::runtime_error("probe");}}};
        // A different initial identity is legitimate; subsequent record generation
        // or proof mismatches must still refuse the sealed batch.
        if(point==1 && (field==2||field==3))continue;
        NativeQueueIngress ingress;Batch out;CHECK(ingress.Read(s,out,error)==NativeQueueRead::RetireRequired);CHECK(!out);
    }
    for(int phase=0;phase<3;++phase){
        Source s;s.Group(0);s.Fence();NativeQueueIngress ingress;Batch out;
        if(phase==0)s.pollHook=[&]{++s.status.engineToken;};
        if(phase==1)s.copyHook=[&]{++s.status.engineToken;};
        CHECK((ingress.Read(s,out,error)==NativeQueueRead::Ready)==(phase==2));
        if(phase==2){++s.status.engineToken;CHECK(!ingress.Validate(s,out->Receipt(),error));}
        CHECK(ingress.NeedsRetirement());
    }
    Source s;s.Add(SDL_EVENT_KEY_DOWN);NativeQueueIngress ingress;Batch out;
    s.pollHook=[&]{Batch nested;CHECK(ingress.Read(s,nested,error)==NativeQueueRead::RetireRequired);};
    CHECK(ingress.Read(s,out,error)==NativeQueueRead::RetireRequired&&!out);
    std::unique_ptr<const NativeQueueQuarantine> quarantine;
    CHECK(ingress.TakeQuarantine(quarantine)&&quarantine);
    s.Retired();s.pollHook={};s.observeHook=[&]{CHECK(!ingress.ResetAfterRetirement(s,error));};
    CHECK(!ingress.ResetAfterRetirement(s,error));
}
static void BoundsAndFreshEpoch(){
    for(int bound=0;bound<4;++bound){
        Source s;
        if(bound==0){for(std::size_t i=0;i<NativeQueueIngress::MaxEvents+1;++i)s.Add(SDL_EVENT_KEY_DOWN);}
        if(bound==1){s.borrowed.assign(65536,'a');for(int i=0;i<16;++i){s.Add(SDL_EVENT_TEXT_INPUT);s.queue.back().first.text.text=s.borrowed.c_str();}}
        if(bound==2){s.Add(SDL_EVENT_TEXT_INPUT);s.queue.back().first.text.text=nullptr;}
        if(bound==3){s.Group(4097);s.Fence();}
        NativeQueueIngress ingress;Batch out;CHECK(ingress.Read(s,out,error)==NativeQueueRead::RetireRequired&&!out);
    }
    Source s;s.sequence=100;s.Add(SDL_EVENT_KEY_DOWN);NativeQueueIngress ingress;Batch out;
    CHECK(ingress.Read(s,out,error)==NativeQueueRead::Ready);CHECK(ingress.Finish(s,out->Receipt(),error));
    s.Retired();s.status.providerEpoch=2;CHECK(ingress.ResetAfterRetirement(s,error));s.Fresh(1);s.sequence=0;
    CHECK(ingress.Read(s,out,error)==NativeQueueRead::Empty);s.Add(SDL_EVENT_KEY_UP);
    CHECK(ingress.Read(s,out,error)==NativeQueueRead::Ready);CHECK(out->Events()[0].record.queue_sequence==1);
    CHECK(ingress.Finish(s,out->Receipt(),error));s.status.generation=2;
    CHECK(ingress.Read(s,out,error)==NativeQueueRead::RetireRequired);
    s.Retired();CHECK(ingress.ResetAfterRetirement(s,error));s.status.providerEpoch=3;s.Fresh(1);
    CHECK(ingress.Read(s,out,error)==NativeQueueRead::RetireRequired); // Epoch was not observed at retirement.
    s.Retired();CHECK(ingress.ResetAfterRetirement(s,error));s.Fresh(1);s.Add(SDL_EVENT_KEY_DOWN);
    CHECK(ingress.Read(s,out,error)==NativeQueueRead::Ready);
    s.status.pending=OQ4_NativeFence{1,0,90,91};CHECK(!ingress.Finish(s,out->Receipt(),error)&&ingress.NeedsRetirement());
    Source exact;for(std::size_t i=0;i<NativeQueueIngress::MaxEvents;++i)exact.Add(SDL_EVENT_KEY_UP);
    NativeQueueIngress limit;Batch full;CHECK(limit.Read(exact,full,error)==NativeQueueRead::Ready&&full->Events().size()==NativeQueueIngress::MaxEvents);
}
static void ScalarWhitelist(){
    const Uint32 types[]={SDL_EVENT_DISPLAY_ADDED,SDL_EVENT_WINDOW_FOCUS_LOST,SDL_EVENT_QUIT,SDL_EVENT_LOW_MEMORY,
        SDL_EVENT_KEY_UP,SDL_EVENT_MOUSE_MOTION,SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_EVENT_MOUSE_WHEEL,
        SDL_EVENT_GAMEPAD_ADDED,SDL_EVENT_GAMEPAD_BUTTON_DOWN,SDL_EVENT_GAMEPAD_AXIS_MOTION,SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION,
        SDL_EVENT_GAMEPAD_SENSOR_UPDATE,SDL_EVENT_JOYSTICK_ADDED,SDL_EVENT_JOYSTICK_BUTTON_DOWN,SDL_EVENT_JOYSTICK_HAT_MOTION,
        SDL_EVENT_JOYSTICK_AXIS_MOTION,SDL_EVENT_FINGER_MOTION};
    Source s;for(auto type:types){s.Add(type);s.queue.back().first.common.timestamp=111;}
    NativeQueueIngress ingress;Batch out;CHECK(ingress.Read(s,out,error)==NativeQueueRead::Ready);
    for(std::size_t i=0;i<std::size(types);++i)CHECK(out->Events()[i].header.type==types[i]&&out->Events()[i].header.common.timestamp==111&&!out->Events()[i].ignored);
}
static void ConcreteSource(){
    SdlNativeQueueSource source(1);NativeQueueStatus status;
    CHECK(source.Observe(status,error)&&status.generation==3&&status.engineToken==7);
    const auto saved=status;
    tokenHook=[]{++engineToken;};CHECK(!source.Observe(status,error));CHECK(status.engineToken==saved.engineToken);
    providerHealthy=false;CHECK(source.Observe(status,error)&&!status.healthy&&status.generation==0);
    providerHealthy=true;SDL_EventQ.active=false;CHECK(OQ4_WindowsNativeFenceQueueGeneration()==0);SDL_EventQ.active=true;
    oq4_queue_active=false;CHECK(OQ4_WindowsNativeFenceQueueGeneration()==0);oq4_queue_active=true;
    mainThread=false;const auto old=locks;CHECK(OQ4_WindowsNativeFenceQueueGeneration()==0&&locks==old);CHECK(!source.Observe(status,error));mainThread=true;
    CHECK(locks==unlocks);SDL_Event e{};OQ4_NativeQueueRecord r{};CHECK(source.Poll(e,r)==0&&polls==1);
    providerFence=OQ4_NativeFence{1,0,4,5};OQ4_NativeFence f{};CHECK(source.CopyFence(e,f)&&copies==1&&f.sequence==5);providerFence.reset();
    SdlNativeQueueSource invalid(0);const auto calls=probes;CHECK(!invalid.Observe(status,error)&&probes==calls);CHECK(invalid.Poll(e,r)==-1);
}
int main(){Stream();Dynamic();Malformed();ProbeFailures();BoundsAndFreshEpoch();ScalarWhitelist();ConcreteSource();std::cout<<"PASS "<<checks<<" checks\n";}
