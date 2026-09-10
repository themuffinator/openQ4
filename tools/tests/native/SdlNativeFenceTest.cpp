// Production Windows pump/fence/queue methods; all native calls are counted doubles.
// No OS input, window, IME, clipboard, or game operation.
#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <string>
#include <vector>
using Uint32=uint32_t; using Sint32=int32_t; using Uint64=uint64_t; using Sint64=int64_t;
using DWORD=uint32_t; using UINT=unsigned; using WPARAM=uintptr_t; using LPARAM=intptr_t;
using LRESULT=intptr_t; using HWND=void*; using SDL_MouseID=unsigned;
#define CALLBACK
#define SDL_DECLSPEC
#define SDLCALL
#define SDL_VIDEO_DRIVER_WINDOWS 1
#define SDL_MAX_UINT64 UINT64_MAX
#define SDL_zero(x) std::memset(&(x),0,sizeof(x))
#define SDL_EVENT_TEXT_INPUT 0x303u
#define SDL_ADDEVENT 0
#define SDL_GETEVENT 1
#define SDL_PEEKEVENT 2
#define SDL_EVENT_POLL_SENTINEL 0x7f00u
#define SDL_MAX_QUEUED_EVENTS 65535
#define SDL_copyp(a,b) std::memcpy((a),(b),sizeof(*(a)))
#define SDL_memcmp std::memcmp
#define SDL_assert assert
#define CHECK_PARAM(x) if(x)
#define SDL_arraysize(a) int(sizeof(a)/sizeof((a)[0]))
using SDL_EventAction=int;
#define SDL_EVENT_FIRST 0
#define SDL_EVENT_LAST 65535
#define SDL_MAX_SINT64 INT64_MAX
#define SDL_min(a,b) ((a)<(b)?(a):(b))
#define WM_DESTROY 2u
#define WM_NCDESTROY 130u
#define WM_MOUSEMOVE 512u
#define WM_KEYDOWN 256u
#define WM_SYSKEYDOWN 260u
#define PM_REMOVE 1u
#define PM_NOREMOVE 0u
#define VK_MENU 18
#define SDL_TICKS_PASSED(a,b) ((Sint32)((b)-(a))<=0)
#define SDL_NS_TO_MS(x) ((x)/1000000)
#define INFINITE 0xffffffffu
#define FALSE 0
#define QS_ALLINPUT 0xff
#define WAIT_OBJECT_0 0
#define SDL_WINDOW_KEYBOARD_GRABBED 1
#define SDL_WINDOW_MOUSE_CAPTURE 2
#define SDL_SCANCODE_LSHIFT 0
#define SDL_SCANCODE_RSHIFT 1
#define SDL_SCANCODE_LGUI 2
#define SDL_SCANCODE_RGUI 3
#define VK_LSHIFT 160
#define VK_RSHIFT 161
#define VK_LWIN 91
#define VK_RWIN 92
#define SDL_GLOBAL_KEYBOARD_ID 0
#define SDL_GLOBAL_MOUSE_ID 0
#define SM_SWAPBUTTON 23
#define VK_LBUTTON 1
#define VK_RBUTTON 2
#define VK_MBUTTON 4
#define VK_XBUTTON1 5
#define VK_XBUTTON2 6
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_X1 4
#define SDL_BUTTON_X2 5
struct SDL_AtomicInt { std::atomic<int> value{0}; };
static int SDL_GetAtomicInt(SDL_AtomicInt*p){return p->value.load();}
static int SDL_AddAtomicInt(SDL_AtomicInt*p,int v){return p->value.fetch_add(v);}
static void SDL_SetAtomicInt(SDL_AtomicInt*p,int v){p->value=v;}
struct SDL_UserEvent {Uint32 type,reserved; Uint64 timestamp; Uint32 windowID; Sint32 code; void*data1;void*data2;};
union SDL_Event {Uint32 type;SDL_UserEvent user;SDL_UserEvent common;Uint64 padding[16];};
struct MSG {HWND hwnd{};UINT message{};WPARAM wParam{};LPARAM lParam{};DWORD time{};};
struct POINT {long x{},y{};};
struct SDL_Window;
struct SDL_WindowData {HWND hwnd{};bool clipcursor_queued{},postpone_clipcursor{},mouse_tracked{};SDL_Window*window{};};
struct SDL_Window {int flags{};SDL_WindowData*internal{};SDL_Window*next{};};
struct SDL_VideoData {void*gameinput_context{};};
struct SDL_VideoDevice {SDL_VideoData*internal{};SDL_Window*windows{};void*wakeup_window{};int(*WaitEventTimeout)(SDL_VideoDevice*,Sint64){};};
struct OQ4_TextScope {int placeholder{};};
static int checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static bool mainThread=true;static std::string error;
static const auto initialThread=std::this_thread::get_id();
static bool SDL_IsMainThread(){return mainThread&&std::this_thread::get_id()==initialThread;}
static bool SDL_SetError(const char*s,...){error=s;return false;}
static void SDL_ClearError(){error.clear();}
static Uint64 SDL_GetTicksNS(){return 11;}
static Uint32 nextType=0x9000;
static Uint32 SDL_RegisterEvents(int){return nextType++;}
static bool SDL_PushEvent(SDL_Event*);
#include "fence-api.inc"
#include "fence-internal.inc"
struct SDL_Mutex {std::recursive_mutex mutex;};
struct SDL_TemporaryMemory {};
using SDL_EventFilter=bool(*)(void*,SDL_Event*);
struct {SDL_Mutex*lock=nullptr;struct{SDL_EventFilter callback=nullptr;void*userdata=nullptr;}filter;}SDL_event_watchers;
static SDL_Mutex*SDL_CreateMutex(){return new SDL_Mutex;}
static void SDL_LockMutex(SDL_Mutex*m){if(m)m->mutex.lock();}
static void SDL_UnlockMutex(SDL_Mutex*m){if(m)m->mutex.unlock();}
static void SDL_DestroyMutex(SDL_Mutex*m){delete m;}
static SDL_AtomicInt SDL_sentinel_pending;
static Uint64 SDL_last_event_id=0;
static int SDL_EventLoggingVerbosity=0;
static void*SDL_disabled_events[4]{};
static bool queueFails=false;
static std::function<void()> admissionCallback,transferCallback;
static void*SDL_malloc(size_t n){if(admissionCallback){auto c=std::move(admissionCallback);admissionCallback={};c();}return queueFails?nullptr:std::malloc(n);}
static void SDL_free(void*p){std::free(p);}
static const char*SDL_GetHint(const char*){return nullptr;}
static int SDL_atoi(const char*p){return std::atoi(p);}
static void SDL_Log(const char*,...){}
static void SDL_LogEvent(SDL_Event*){}
static bool SDL_InvalidParamError(const char*p){return SDL_SetError(p);}
static bool SDL_InitEventWatchList(decltype(SDL_event_watchers)*){return true;}
static void SDL_QuitEventWatchList(decltype(SDL_event_watchers)*){SDL_event_watchers.filter={};}
static void SDL_InitWindowEventWatch(){}
static void SDL_QuitWindowEventWatch(){}
#include "queue-types.inc"
static void SDL_TransferTemporaryMemoryToEvent(SDL_EventEntry*){}
static void SDL_TransferTemporaryMemoryFromEvent(SDL_EventEntry*){if(transferCallback){auto c=std::move(transferCallback);transferCallback={};c();}}
static void SDL_SendWakeupEvent(){}
#include "queue-state.inc"
#include "fence.inc"
#include "queue-methods.inc"
static int SDL_PeepEvents(SDL_Event*e,int n,SDL_EventAction a,Uint32 lo,Uint32 hi){return SDL_PeepEventsInternal(e,n,a,lo,hi,false);}
struct EventsView {
 size_t size()const{return size_t(SDL_GetAtomicInt(&SDL_EventQ.count));}
 bool empty()const{return !SDL_EventQ.head;}
 SDL_Event&back(){CHECK(SDL_EventQ.tail);return SDL_EventQ.tail->event;}
 SDL_Event&operator[](size_t i){auto*p=SDL_EventQ.head;while(i--&&p)p=p->next;CHECK(p);return p->event;}
 void clear(){SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);}
};
static std::deque<MSG> nativeQueue;
static EventsView events;
static std::function<void()> inPeek,inDispatch,inHousekeeping;
static std::function<bool(SDL_Event*)> filter;
static int peeks=0,dispatches=0,translations=0,waits=0,gameUpdates=0,tailUpdates=0,queueAttempts=0;
static bool g_WindowsEnableMessageLoop=true,SDL_processing_messages=false;
static DWORD SDL_last_warp_time=0;
static bool(*g_WindowsMessageHook)(void*,MSG*)=nullptr;
static void*g_WindowsMessageHookData=nullptr;
static bool keys[4]{};
static SDL_Window*focus=nullptr;
static SDL_VideoData videoData;
static SDL_VideoDevice video{&videoData,nullptr};
static DWORD GetTickCount(){return 10;}
static DWORD GetMessageTime(){return 0;}
static bool PeekMessage(MSG*out,HWND,UINT,UINT,UINT mode){
 ++peeks;auto callback=std::move(inPeek);inPeek={};if(callback)callback();
 if(nativeQueue.empty())return false;*out=nativeQueue.front();if(mode==PM_REMOVE)nativeQueue.pop_front();return true;
}
static void TranslateMessage(MSG*){++translations;}
static LRESULT WIN_WindowProcObserved(HWND,UINT,WPARAM,LPARAM){++dispatches;if(inDispatch)inDispatch();return 37;}
static int scopeDepth=0;
static OQ4_TextScope OQ4_WIN_SaveTextScope(){return {scopeDepth++};}
static void OQ4_WIN_RestoreTextScope(OQ4_TextScope prior){scopeDepth=prior.placeholder;}
#include "window-scope.inc"
static LRESULT DispatchMessage(MSG*msg){return WIN_WindowProc(msg->hwnd,msg->message,msg->wParam,msg->lParam);}
static void WIN_SetMessageTick(DWORD){}
static DWORD MsgWaitForMultipleObjects(int,void*,int,DWORD,int){++waits;return 0;}
static void WIN_UpdateGameInput(SDL_VideoDevice*){++gameUpdates;}
static const bool*SDL_GetKeyboardState(void*){return keys;}
static int GetKeyState(int){return 0;}
static SDL_Window*SDL_GetKeyboardFocus(){return focus;}
static void Emit(Uint32 type=600){SDL_Event ev{};ev.type=type;SDL_PushEvent(&ev);}
static void SDL_SendKeyboardKey(int,int,int,int,bool){Emit(601);}
static void WIN_UpdateClipCursor(SDL_Window*){}
static bool GetCursorPos(POINT*){return false;}
static bool ScreenToClient(HWND,POINT*){return false;}
static int GetSystemMetrics(int){return 0;}
static int GetAsyncKeyState(int){return 0;}
static Uint64 WIN_GetEventTimestamp(){return 0;}
static void SDL_SendMouseMotion(Uint64,SDL_Window*,SDL_MouseID,bool,float,float){Emit(602);}
static void SDL_SendMouseButton(Uint64,SDL_Window*,SDL_MouseID,int,bool){Emit(603);}
static void WIN_UpdateIMECandidates(SDL_VideoDevice*){++tailUpdates;if(inHousekeeping)inHousekeeping();}
static OQ4_TextScope OQ4_WIN_BeginTextAdmission(SDL_Event*){return {};}
static bool OQ4_WIN_ValidateTextMarker(SDL_Event*){return true;}
static void OQ4_WIN_AssociateText(SDL_Event*,OQ4_TextScope){}
static void OQ4_WIN_DropTextAssociation(SDL_Event*){}
static bool SDL_CallEventWatchers(SDL_Event*e){return (!filter||filter(e))&&(!SDL_event_watchers.filter.callback||SDL_event_watchers.filter.callback(SDL_event_watchers.filter.userdata,e));}
#include "admission.inc"
#include "pump.inc"
static Sint64 SDL_events_get_polling_interval(){return 10;}
static void SDL_PumpEventsInternal(bool){WIN_PumpEvents(&video);}
static void SDL_SetAtomicPointer(void**p,void*v){*p=v;}
#include "wait.inc"
static void Reset(){
 mainThread=true;OQ4_WindowsNativeFenceRetire();admissionCallback={};transferCallback={};SDL_StopEventLoop();CHECK(SDL_StartEventLoop());oq4_fence_in_pump=oq4_fence_emitting=false;oq4_fence_admission=nullptr;
 CHECK(!oq4_fence_callback_depth&&!oq4_fence_prepare_attempted&&!oq4_fence_context_active);CHECK(OQ4_WindowsNativeFenceRegisterHooks(nullptr));
 nativeQueue.clear();events.clear();inPeek={};inDispatch={};inHousekeeping={};filter={};queueFails=false;g_WindowsMessageHook=nullptr;
 peeks=dispatches=translations=waits=gameUpdates=tailUpdates=queueAttempts=0;g_WindowsEnableMessageLoop=true;SDL_processing_messages=false;
 videoData.gameinput_context=nullptr;video.windows=nullptr;focus=nullptr;SDL_last_warp_time=0;std::memset(keys,0,sizeof(keys));error.clear();
}
static void Posted(UINT id=WM_KEYDOWN){nativeQueue.push_back({nullptr,id,0,0,5});}
static OQ4_NativeFence Current(){OQ4_NativeFence f{};CHECK(OQ4_WindowsNativeFencePending(&f));return f;}
static SDL_Event ConsumeFence(){SDL_Event ev{};OQ4_NativeQueueRecord record{};for(int i=0;i<65536;i++){CHECK(OQ4_WindowsNativeFencePoll(&ev,&record)==1);if(record.kind==OQ4_QUEUE_FENCE)return ev;}CHECK(false);return ev;}
static void Ack(){const auto f=Current();auto marker=ConsumeFence();OQ4_NativeFence copy{};CHECK(OQ4_WindowsNativeFenceCopy(&marker,&copy));CHECK(copy.dispatch==f.dispatch&&copy.sequence==f.sequence);CHECK(OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));}
static void DisabledAndCollection(){
 Reset();for(int i=0;i<4;i++)Posted();WIN_PumpEvents(&video);CHECK(dispatches==4&&peeks==5&&tailUpdates==1);CHECK(events.empty());
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));for(int i=0;i<3;i++)Posted();
 inDispatch=[](){Emit();Emit(601);const int old=peeks;WIN_PumpEvents(&video);CHECK(peeks==old);CHECK(OQ4_WIN_CurrentNativeDispatch()!=0);};
 inPeek=[](){CHECK(WIN_WindowProc(nullptr,WM_KEYDOWN,0,0)==37);};
 inHousekeeping=[](){Emit(604);};
 WIN_PumpEvents(&video);CHECK(peeks==1&&dispatches==2&&translations==1&&tailUpdates==1);CHECK(nativeQueue.size()==2);CHECK(events.size()==6);
 const auto f=Current();CHECK(f.version==1&&f.event_count==5&&f.sequence>0);CHECK(OQ4_WIN_CurrentNativeDispatch()==0);
 const int old=peeks;WIN_PumpEvents(&video);CHECK(peeks==old&&tailUpdates==1);CHECK(WIN_WaitEventTimeout(&video,1000)==0&&waits==0);
 CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence)); // Peeking status is not queue consumption.
 OQ4_NativeFence copy{};CHECK(!OQ4_WindowsNativeFenceCopy(&events.back(),&copy));auto consumed=ConsumeFence();CHECK(OQ4_WindowsNativeFenceCopy(&consumed,&copy));CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch+1,f.sequence));CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence+1));
 CHECK(OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));
 inDispatch={};inHousekeeping={};WIN_PumpEvents(&video);const auto next=Current();CHECK(next.dispatch>f.dispatch&&next.sequence>f.sequence);CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));Ack();
 WIN_PumpEvents(&video);Ack();WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFencePending(&copy));CHECK(!OQ4_WIN_NativeFenceBlocked());
}
static void EmptySentAndHousekeeping(){
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));WIN_PumpEvents(&video);CHECK(events.empty()&&peeks==1);CHECK(!OQ4_WIN_NativeFenceBlocked());
 inPeek=[](){WIN_WindowProc(nullptr,WM_KEYDOWN,0,0);};WIN_PumpEvents(&video);CHECK(Current().event_count==0);Ack();
 keys[0]=true;WIN_PumpEvents(&video);CHECK(Current().event_count==1);CHECK(events[events.size()-2].type==601);Ack();keys[0]=false;
 videoData.gameinput_context=&video;inHousekeeping=[](){Emit();};WIN_PumpEvents(&video);CHECK(gameUpdates==1&&Current().event_count==1);Ack();
}
static void EarlyExits(){
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();Posted();g_WindowsMessageHook=[](void*,MSG*){return false;};WIN_PumpEvents(&video);CHECK(peeks==1&&dispatches==0&&nativeQueue.size()==1);Ack();
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted(WM_MOUSEMOVE);Posted();SDL_last_warp_time=9;WIN_PumpEvents(&video);CHECK(peeks==1&&dispatches==0&&nativeQueue.size()==1);Ack();
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));g_WindowsEnableMessageLoop=false;WIN_PumpEvents(&video);CHECK(peeks==0&&tailUpdates==1&&events.empty());
}
static void FailuresAndAtomicity(){
 for(int mutation=0;mutation<8;++mutation){
  Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();
  filter=[mutation](SDL_Event*ev){if(ev->type==OQ4_WindowsNativeFenceEventType()){
   switch(mutation){case 0:return false;case 1:ev->type=600;break;case 2:ev->user.windowID=1;break;case 3:++ev->user.code;break;
   case 4:ev->user.data1=&video;break;case 5:ev->user.data2=&video;break;case 6:ev->user.reserved=1;break;case 7:queueFails=true;break;}
  }return true;};
  WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy());CHECK(OQ4_WIN_NativeFenceBlocked());CHECK(events.empty());
  OQ4_NativeFence f{99,98,97,96},before=f;CHECK(!OQ4_WindowsNativeFencePending(&f));CHECK(std::memcmp(&f,&before,sizeof(f))==0);
  const int old=peeks;WIN_PumpEvents(&video);CHECK(peeks==old);CHECK(!OQ4_WindowsNativeFenceEnable(true));
 }
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();filter=[](SDL_Event*ev){return ev->type==OQ4_WindowsNativeFenceEventType();};inDispatch=[](){Emit();};WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy()&&events.empty()&&!error.empty());
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();filter=[](SDL_Event*ev){if(ev->type==OQ4_WindowsNativeFenceEventType())ev->user.timestamp=999;return true;};WIN_PumpEvents(&video);Ack();
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();WIN_PumpEvents(&video);auto f=Current();auto marker=events.back();events.clear();CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));CHECK(OQ4_WIN_NativeFenceBlocked());
 marker.user.code++;OQ4_NativeFence copy{4,5,6,7},before=copy;CHECK(!OQ4_WindowsNativeFenceCopy(&marker,&copy));CHECK(std::memcmp(&copy,&before,sizeof(copy))==0);
 mainThread=false;CHECK(!OQ4_WindowsNativeFencePending(&copy));CHECK(!OQ4_WindowsNativeFenceRetire());CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));CHECK(!OQ4_WindowsNativeFenceCopy(&marker,&copy));CHECK(!OQ4_WindowsNativeFenceEnable(true));CHECK(OQ4_WIN_NativeFenceBlocked());CHECK(std::memcmp(&copy,&before,sizeof(copy))==0);mainThread=true;
}
static void ReentryAndLifecycle(){
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();filter=[](SDL_Event*ev){if(ev->type==OQ4_WindowsNativeFenceEventType()){
  const int old=peeks;WIN_PumpEvents(&video);CHECK(peeks==old);OQ4_NativeFence f{};CHECK(!OQ4_WindowsNativeFencePending(&f));CHECK(!OQ4_WindowsNativeFenceCopy(ev,&f));
 }return true;};WIN_PumpEvents(&video);Ack();
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();bool nested=false;filter=[&nested](SDL_Event*ev){if(ev->type==OQ4_WindowsNativeFenceEventType()&&!nested){nested=true;CHECK(!SDL_PushEvent(ev));}return true;};WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy()&&events.empty());
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();filter=[](SDL_Event*ev){if(ev->type==OQ4_WindowsNativeFenceEventType()){
  CHECK(OQ4_WindowsNativeFenceRetire());CHECK(!OQ4_WindowsNativeFenceEnable(true));
 }return true;};WIN_PumpEvents(&video);CHECK(events.empty()&&!OQ4_WIN_NativeFenceBlocked());CHECK(OQ4_WindowsNativeFenceEnable(true));
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();WIN_PumpEvents(&video);auto old=Current();CHECK(WIN_WindowProc(nullptr,WM_KEYDOWN,0,0)==37);CHECK(!OQ4_WindowsNativeFenceHealthy());CHECK(!OQ4_WindowsNativeFenceAck(old.dispatch,old.sequence));
 Posted();Posted();WIN_PumpEventsForHWND(&video,nullptr);CHECK(!OQ4_WindowsNativeFenceHealthy());CHECK(nativeQueue.empty()&&dispatches==4);CHECK(!OQ4_WIN_NativeFenceBlocked());CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();WIN_PumpEvents(&video);CHECK(Current().dispatch>old.dispatch);CHECK(!OQ4_WindowsNativeFenceAck(old.dispatch,old.sequence));Ack();
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=[](){CHECK(OQ4_WindowsNativeFenceRetire());CHECK(!OQ4_WindowsNativeFenceEnable(true));const int old=peeks;WIN_PumpEvents(&video);CHECK(peeks==old);};WIN_PumpEvents(&video);CHECK(events.empty()&&!OQ4_WIN_NativeFenceBlocked());CHECK(OQ4_WindowsNativeFenceEnable(true));
 inDispatch={};Posted(WM_NCDESTROY);WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy()&&!OQ4_WIN_NativeFenceBlocked());CHECK(events.empty());CHECK(OQ4_WindowsNativeFenceEnable(true));
 Posted();WIN_PumpEvents(&video);auto prior=Current();auto stale=events.back();OQ4_WIN_NativeFenceShutdown();CHECK(!OQ4_WindowsNativeFenceEventType());CHECK(OQ4_WindowsNativeFenceEnable(true));CHECK(!OQ4_WindowsNativeFenceAck(prior.dispatch,prior.sequence));Posted();WIN_PumpEvents(&video);CHECK(Current().dispatch>prior.dispatch);OQ4_NativeFence f{};CHECK(!OQ4_WindowsNativeFenceCopy(&stale,&f));Ack();
}
static void HeldWait(){
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();WIN_PumpEvents(&video);
 const auto f=Current();SDL_Event event=ConsumeFence();video.WaitEventTimeout=[](SDL_VideoDevice*,Sint64){++waits;if(waits>1)return -1;return 0;};
 CHECK(SDL_WaitEventTimeout_Device(&video,nullptr,&event,0,-1)==0);CHECK(waits==0&&peeks==1);
 OQ4_NativeFence copy{};CHECK(OQ4_WindowsNativeFenceCopy(&event,&copy));CHECK(OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();queueFails=true;WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy());
 CHECK(SDL_WaitEventTimeout_Device(&video,nullptr,&event,0,-1)==0);CHECK(waits==0&&peeks==1);
}
static void QueueStream(){
 Reset();Emit(701);Emit(SDL_EVENT_POLL_SENTINEL);CHECK(OQ4_WindowsNativeFenceEnable(true));
 Posted();inDispatch=[](){
  SDL_Event direct{};direct.type=702;CHECK(SDL_PeepEvents(&direct,1,SDL_ADDEVENT,0,0)==1);
  std::thread worker([](){SDL_Event outside{};outside.type=703;CHECK(SDL_PeepEvents(&outside,1,SDL_ADDEVENT,0,0)==1);});worker.join();
  Emit(SDL_EVENT_POLL_SENTINEL);Emit(704);
 };
 WIN_PumpEvents(&video);const auto f=Current();CHECK(f.event_count==2);auto marker=events.back();OQ4_NativeFence copied{};CHECK(!OQ4_WindowsNativeFenceCopy(&marker,&copied));
 const Uint32 types[]={701,SDL_EVENT_POLL_SENTINEL,702,703,SDL_EVENT_POLL_SENTINEL,704,OQ4_WindowsNativeFenceEventType()};
 const Uint32 kinds[]={OQ4_QUEUE_OUTSIDE,OQ4_QUEUE_SENTINEL,OQ4_QUEUE_COLLECTION,OQ4_QUEUE_OUTSIDE,OQ4_QUEUE_SENTINEL,OQ4_QUEUE_COLLECTION,OQ4_QUEUE_FENCE};
 Uint64 sequence=0,generation=0;Uint32 ordinal=0;
 for(size_t i=0;i<7;i++){SDL_Event e{};OQ4_NativeQueueRecord r{};CHECK(OQ4_WindowsNativeFencePoll(&e,&r)==1);CHECK(e.type==types[i]&&r.kind==kinds[i]&&r.version==1);CHECK(r.queue_sequence>sequence);sequence=r.queue_sequence;if(!generation)generation=r.generation;CHECK(r.generation==generation);
  if(r.kind==OQ4_QUEUE_COLLECTION){CHECK(r.dispatch==f.dispatch&&r.ordinal==++ordinal&&!r.fence_sequence);}
  else if(r.kind==OQ4_QUEUE_FENCE){CHECK(r.dispatch==f.dispatch&&r.ordinal==2&&r.fence_sequence==f.sequence);marker=e;}
  else CHECK(!r.dispatch&&!r.ordinal&&!r.fence_sequence);
 }
 CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));CHECK(OQ4_WindowsNativeFenceCopy(&marker,&copied));CHECK(OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));
 SDL_Event e{};e.type=987;auto before=e;OQ4_NativeQueueRecord r{9,8,7,6,5,4,3,2},prior=r;CHECK(OQ4_WindowsNativeFencePoll(&e,&r)==0);CHECK(std::memcmp(&e,&before,sizeof(e))==0&&std::memcmp(&r,&prior,sizeof(r))==0);
 mainThread=false;CHECK(OQ4_WindowsNativeFencePoll(&e,&r)==-1);mainThread=true;CHECK(std::memcmp(&e,&before,sizeof(e))==0&&std::memcmp(&r,&prior,sizeof(r))==0);
}
static void QueueLoss(){
 for(int loss=0;loss<6;loss++){
  Reset();Emit(710);CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=[](){Emit(711);Emit(712);};WIN_PumpEvents(&video);auto f=Current();auto marker=events.back();
  if(loss==0)SDL_FlushEvents(711,711);
  if(loss==1)SDL_FilterEvents([](void*,SDL_Event*e){return e->type!=711;},nullptr);
  if(loss==2)SDL_SetEventFilter([](void*,SDL_Event*e){return e->type!=711;},nullptr);
  if(loss==3){SDL_Event e{};CHECK(SDL_PeepEvents(&e,1,SDL_GETEVENT,710,710)==1);}
  if(loss==4){std::thread t([](){SDL_Event e{};CHECK(SDL_PeepEvents(&e,1,SDL_GETEVENT,711,711)==1);});t.join();}
  if(loss==5){SDL_FlushEvents(711,711);SDL_Event e{};e.type=711;CHECK(SDL_PeepEvents(&e,1,SDL_ADDEVENT,0,0)==1);}
  CHECK(!OQ4_WindowsNativeFenceHealthy());OQ4_NativeFence copy{};CHECK(!OQ4_WindowsNativeFenceCopy(&marker,&copy));CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));
  SDL_Event e{};e.type=456;auto before=e;OQ4_NativeQueueRecord record{1,2,3,4,5,6,7,8},saved=record;CHECK(OQ4_WindowsNativeFencePoll(&e,&record)==-1);CHECK(std::memcmp(&e,&before,sizeof(e))==0&&std::memcmp(&record,&saved,sizeof(saved))==0);
  const int priorPeeks=peeks;WIN_PumpEvents(&video);CHECK(peeks==priorPeeks);
 }
 for(int mutation=0;mutation<4;mutation++){
  Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=[](){Emit(720);};WIN_PumpEvents(&video);
  SDL_FilterEvents([](void*ctx,SDL_Event*e){if(e->type==720){switch(*static_cast<int*>(ctx)){case 0:e->type=721;break;case 1:++e->common.timestamp;break;case 2:e->user.data1=&video;break;case 3:++e->user.reserved;break;}}return true;},&mutation);
  CHECK(!OQ4_WindowsNativeFenceHealthy());
 }
 // Public peeking and sentinel maintenance do not discard native data.
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=[](){Emit(730);Emit(SDL_EVENT_POLL_SENTINEL);};WIN_PumpEvents(&video);
 SDL_Event peek{};CHECK(SDL_PeepEvents(&peek,1,SDL_PEEKEVENT,0,65535)==1);CHECK(OQ4_WindowsNativeFenceHealthy());CHECK(SDL_PeepEvents(nullptr,1,SDL_GETEVENT,0,65535)==2);CHECK(OQ4_WindowsNativeFenceHealthy());
 SDL_FlushEvents(SDL_EVENT_POLL_SENTINEL,SDL_EVENT_POLL_SENTINEL);CHECK(OQ4_WindowsNativeFenceHealthy());CHECK(Current().event_count==1);Ack();
 // Disabled ordinary queue semantics survive filtering and arbitrary GET.
 Reset();Emit(740);Emit(741);SDL_FilterEvents([](void*,SDL_Event*e){return e->type!=740;},nullptr);CHECK(events.size()==1);CHECK(SDL_PeepEvents(&peek,1,SDL_GETEVENT,0,65535)==1&&peek.type==741);
 // Disabled guard must preserve SDL's existing error, including allocator failure.
 Reset();queueFails=true;error="allocator failure";peek.type=742;CHECK(SDL_PeepEvents(&peek,1,SDL_ADDEVENT,0,0)==0);CHECK(error=="allocator failure");
 queueFails=false;SDL_SetAtomicInt(&SDL_EventQ.count,SDL_MAX_QUEUED_EVENTS);CHECK(SDL_PeepEvents(&peek,1,SDL_ADDEVENT,0,0)==0);CHECK(error=="Event queue is full (%d events)");SDL_SetAtomicInt(&SDL_EventQ.count,0);
}
static void QueueForgeryAndLifecycle(){
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();filter=[](SDL_Event*e){if(e->type==OQ4_WindowsNativeFenceEventType())CHECK(SDL_PeepEvents(e,1,SDL_ADDEVENT,0,0)==0);return true;};WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy()&&events.empty());
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=[](){Emit(750);};WIN_PumpEvents(&video);auto f=Current();auto stale=events.back();const auto oldGeneration=SDL_EventQ.head->oq4_record.generation;
 SDL_StopEventLoop();CHECK(!OQ4_WindowsNativeFenceHealthy());CHECK(SDL_StartEventLoop());CHECK(!OQ4_WindowsNativeFenceEnable(true));CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));CHECK(OQ4_WindowsNativeFenceRetire());CHECK(OQ4_WindowsNativeFenceEnable(true));
 inDispatch={};Posted();WIN_PumpEvents(&video);auto next=Current();CHECK(next.dispatch>f.dispatch);CHECK(SDL_EventQ.head->oq4_record.generation>oldGeneration);OQ4_NativeFence copy{};CHECK(!OQ4_WindowsNativeFenceCopy(&stale,&copy));Ack();
 // An admission allocator may add another ordinary record first; ordinals follow actual link order.
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=[](){admissionCallback=[](){Emit(761);};Emit(760);};WIN_PumpEvents(&video);CHECK(events[0].type==761&&events[1].type==760&&Current().event_count==2);Ack();
 // Reentrant checked Poll cannot reorder an outside prefix while its Cut is in flight.
 Reset();Emit(770);Emit(771);CHECK(OQ4_WindowsNativeFenceEnable(true));transferCallback=[](){SDL_Event e{};OQ4_NativeQueueRecord r{};CHECK(OQ4_WindowsNativeFencePoll(&e,&r)==-1);};SDL_Event e{};OQ4_NativeQueueRecord r{};CHECK(OQ4_WindowsNativeFencePoll(&e,&r)==1&&e.type==770);CHECK(OQ4_WindowsNativeFencePoll(&e,&r)==1&&e.type==771);
 // A nested foreign discard poisons the outer checked removal before output publication.
 Reset();Emit(780);Emit(781);CHECK(OQ4_WindowsNativeFenceEnable(true));e.type=999;r={1,2,3,4,5,6,7,8};auto saved=e;auto prior=r;transferCallback=[](){SDL_FlushEvents(781,781);};CHECK(OQ4_WindowsNativeFencePoll(&e,&r)==-1);CHECK(!OQ4_WindowsNativeFenceHealthy());CHECK(std::memcmp(&e,&saved,sizeof(e))==0&&std::memcmp(&r,&prior,sizeof(r))==0);
}
static void QueueBounds(){
 Reset();auto seq=oq4_queue_sequence;Emit(790);oq4_queue_sequence=UINT64_MAX;CHECK(!OQ4_WindowsNativeFenceEnable(true));CHECK(events.size()==1);oq4_queue_sequence=seq;
 Reset();seq=oq4_queue_sequence;oq4_queue_sequence=UINT64_MAX;CHECK(!OQ4_WindowsNativeFenceEnable(true));oq4_queue_sequence=seq;
 Reset();auto gen=oq4_queue_generation;oq4_queue_generation=UINT64_MAX;CHECK(!OQ4_WindowsNativeFenceEnable(true));oq4_queue_generation=gen;
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));seq=oq4_queue_sequence;oq4_queue_sequence=UINT64_MAX;Posted();inDispatch=[](){Emit(791);};WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy()&&events.empty());oq4_queue_sequence=seq;
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=[](){Emit(792);Emit(793);};WIN_PumpEvents(&video);SDL_EventQ.head->oq4_record.ordinal=2;SDL_Event e{};OQ4_NativeQueueRecord r{};CHECK(OQ4_WindowsNativeFencePoll(&e,&r)==-1&&!OQ4_WindowsNativeFenceHealthy());
}
static void QueueForgedMetadata(){
 for(int wrong=0;wrong<5;wrong++){
  Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=[](){Emit(795);};WIN_PumpEvents(&video);
  SDL_Event e{};OQ4_NativeQueueRecord r{};CHECK(OQ4_WindowsNativeFencePoll(&e,&r)==1);
  auto&tag=SDL_EventQ.head->oq4_record;
  if(wrong==0)++tag.fence_sequence;if(wrong==1)++tag.generation;if(wrong==2)++tag.dispatch;if(wrong==3)++tag.ordinal;if(wrong==4)oq4_queue_consumed=0;
  e.type=998;r={1,2,3,4,5,6,7,8};auto before=e;auto prior=r;CHECK(OQ4_WindowsNativeFencePoll(&e,&r)==-1);CHECK(!OQ4_WindowsNativeFenceHealthy());CHECK(std::memcmp(&e,&before,sizeof(e))==0&&std::memcmp(&r,&prior,sizeof(r))==0);
 }
}
static void Bounds(){
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=[](){for(int i=0;i<=OQ4_FENCE_MAX_EVENTS;i++)Emit();};WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy());CHECK(events.size()==OQ4_FENCE_MAX_EVENTS); // offending admission is rejected before link.
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));auto d=oq4_fence_dispatch;oq4_fence_dispatch=UINT64_MAX;WIN_PumpEvents(&video);CHECK(peeks==0&&!OQ4_WindowsNativeFenceHealthy());oq4_fence_dispatch=d;
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));auto s=oq4_fence_sequence;oq4_fence_sequence=UINT64_MAX;Posted();WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy()&&events.empty());oq4_fence_sequence=s;
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));auto m=oq4_fence_marker;oq4_fence_marker=0x7fffffffu;Posted();WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy()&&events.empty());oq4_fence_marker=m;
}
struct HookState {
 int prepared=0,finished=0,worked=0;bool aborted=false;
 OQ4_NativeCollectionContext context{};
 std::function<bool(const OQ4_NativeCollectionContext*)> prepare,work;
 std::function<bool(const OQ4_NativeCollectionContext*,bool)> finish;
};
static bool PrepareHook(void* userdata,const OQ4_NativeCollectionContext* context){
 auto& h=*static_cast<HookState*>(userdata);++h.prepared;h.context=*context;
 CHECK(context->version==1&&context->generation&&context->dispatch);CHECK(OQ4_WIN_CurrentNativeDispatch()==context->dispatch);
 return h.prepare?h.prepare(context):true;
}
static bool FinishHook(void* userdata,const OQ4_NativeCollectionContext* context,bool aborted){
 auto& h=*static_cast<HookState*>(userdata);++h.finished;h.aborted=aborted;
 CHECK(std::memcmp(context,&h.context,sizeof(*context))==0);
 return h.finish?h.finish(context,aborted):true;
}
static bool LifecycleWork(void* userdata,const OQ4_NativeCollectionContext* context){
 auto& h=*static_cast<HookState*>(userdata);++h.worked;CHECK(context->kind==OQ4_COLLECTION_LIFECYCLE);
 return h.work?h.work(context):true;
}
static void Register(HookState& h){OQ4_NativeCollectionHooks hooks{1,&h,PrepareHook,FinishHook};CHECK(OQ4_WindowsNativeFenceRegisterHooks(&hooks));}
static void HooksAndZeroEvent(){
 Reset();HookState h;OQ4_NativeCollectionHooks hooks{1,&h,PrepareHook,FinishHook};
 auto bad=hooks;bad.version=2;CHECK(!OQ4_WindowsNativeFenceRegisterHooks(&bad));bad=hooks;bad.Prepare=nullptr;CHECK(!OQ4_WindowsNativeFenceRegisterHooks(&bad));bad=hooks;bad.Finish=nullptr;CHECK(!OQ4_WindowsNativeFenceRegisterHooks(&bad));
 bool worker=true;std::thread other([&]{worker=OQ4_WindowsNativeFenceRegisterHooks(&hooks);});other.join();CHECK(!worker);
 CHECK(OQ4_WindowsNativeFenceRegisterHooks(&hooks));hooks.Prepare=nullptr;hooks.Finish=nullptr;hooks.userdata=nullptr; // copied table, not borrowed
 CHECK(!OQ4_WindowsNativeFenceRunLifecycle(LifecycleWork,&h)&&h.worked==0);
 CHECK(OQ4_WindowsNativeFenceEnable(true));CHECK(!OQ4_WindowsNativeFenceRegisterHooks(nullptr));
 h.prepare=[&](const auto* context){CHECK(peeks==0&&tailUpdates==0);CHECK(context->kind==OQ4_COLLECTION_PUMP);return true;};
 bool observed=false;inPeek=[&]{observed=true;CHECK(h.prepared==1&&h.finished==0);};
 h.finish=[&](const auto* context,bool aborted){CHECK(!aborted&&observed&&tailUpdates==1&&h.finished==1);OQ4_NativeFence pending{};CHECK(!OQ4_WindowsNativeFencePending(&pending));CHECK(events.empty());return OQ4_WindowsNativeFenceMarkActivity(context);};
 WIN_PumpEvents(&video);CHECK(h.prepared==1&&h.finished==1&&!h.aborted&&Current().event_count==0);const auto stale=h.context;CHECK(!OQ4_WindowsNativeFenceMarkActivity(&stale));Ack();
 h.prepare={};h.finish={};WIN_PumpEvents(&video);CHECK(h.prepared==2&&h.finished==2&&events.empty()&&!OQ4_WIN_NativeFenceBlocked());
 CHECK(h.context.dispatch>stale.dispatch);
 CHECK(OQ4_WindowsNativeFenceRetire());CHECK(OQ4_WindowsNativeFenceRegisterHooks(nullptr));
 CHECK(OQ4_WindowsNativeFenceEnable(true));WIN_PumpEvents(&video);CHECK(h.prepared==2&&h.finished==2);
}
static void ExactHookContextAndReentry(){
 Reset();HookState h;Register(h);CHECK(OQ4_WindowsNativeFenceEnable(true));
 h.prepare=[&](const auto* context){
  for(unsigned field=0;field<4;++field){auto wrong=*context;if(field==0)++wrong.version;if(field==1)wrong.kind=OQ4_COLLECTION_LIFECYCLE;if(field==2)++wrong.generation;if(field==3)++wrong.dispatch;CHECK(!OQ4_WindowsNativeFenceMarkActivity(&wrong));}
  bool worker=true;std::thread t([&]{worker=OQ4_WindowsNativeFenceMarkActivity(context);});t.join();CHECK(!worker);
  CHECK(!OQ4_WindowsNativeFenceMarkActivity(nullptr));CHECK(OQ4_WindowsNativeFenceMarkActivity(context));
  CHECK(!OQ4_WindowsNativeFenceRegisterHooks(nullptr)&&!OQ4_WindowsNativeFenceEnable(true));
  const int before=peeks;WIN_PumpEvents(&video);CHECK(peeks==before);CHECK(!OQ4_WindowsNativeFenceRunLifecycle(LifecycleWork,&h));return true;
 };
 h.finish=[&](const auto* context,bool aborted){CHECK(!aborted);const int before=peeks;WIN_PumpEvents(&video);CHECK(peeks==before);CHECK(!OQ4_WindowsNativeFenceRunLifecycle(LifecycleWork,&h));CHECK(!OQ4_WindowsNativeFenceRegisterHooks(nullptr)&&!OQ4_WindowsNativeFenceEnable(true));return OQ4_WindowsNativeFenceMarkActivity(context);};
 WIN_PumpEvents(&video);CHECK(h.worked==0&&h.prepared==1&&h.finished==1);Ack();
 const auto stale=h.context;CHECK(OQ4_WindowsNativeFenceRetire());CHECK(OQ4_WindowsNativeFenceEnable(true));
 h.prepare=[&](const auto* context){CHECK(context->generation>stale.generation);CHECK(!OQ4_WindowsNativeFenceMarkActivity(&stale));return true;};
 WIN_PumpEvents(&video);Ack();
}
static void HookFailuresAndRetirement(){
 for(int failure=0;failure<7;++failure){Reset();HookState h;Register(h);CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();
  h.prepare=[&](const auto* context){if(failure==0)return false;if(failure==1){CHECK(OQ4_WindowsNativeFenceRetire());CHECK(!OQ4_WindowsNativeFenceEnable(true)&&!OQ4_WindowsNativeFenceRegisterHooks(nullptr));}
   if(failure==2)++const_cast<OQ4_NativeCollectionContext*>(context)->dispatch;return true;};
  h.finish=[&](const auto* context,bool aborted){CHECK(h.finished==1);CHECK(!OQ4_WindowsNativeFenceRegisterHooks(nullptr));
   if(failure<3||failure==6)CHECK(aborted);else CHECK(!aborted);
   if(failure==3)return false;if(failure==4){CHECK(OQ4_WindowsNativeFenceRetire());CHECK(!OQ4_WindowsNativeFenceEnable(true));}
   if(failure==5)++const_cast<OQ4_NativeCollectionContext*>(context)->generation;return true;};
  if(failure==6)inDispatch=[] {CHECK(OQ4_WindowsNativeFenceRetire());};
  WIN_PumpEvents(&video);CHECK(h.prepared==1&&h.finished==1&&!OQ4_WindowsNativeFenceHealthy()&&events.empty());
  CHECK(peeks==(failure<3?0:1));CHECK(!oq4_fence_context_active&&!oq4_fence_prepare_attempted&&!oq4_fence_callback_depth&&!oq4_fence_in_pump);
  CHECK(OQ4_WindowsNativeFenceRetire());CHECK(OQ4_WindowsNativeFenceRegisterHooks(nullptr));CHECK(OQ4_WindowsNativeFenceEnable(true));
 }
}
static void LifecycleCollections(){
 Reset();HookState h;CHECK(!OQ4_WindowsNativeFenceRunLifecycle(LifecycleWork,&h));CHECK(OQ4_WindowsNativeFenceEnable(true));
 CHECK(!OQ4_WindowsNativeFenceRunLifecycle(nullptr,&h));CHECK(OQ4_WindowsNativeFenceRunLifecycle(LifecycleWork,&h));CHECK(h.worked==1&&peeks==0&&Current().event_count==0);Ack();
 CHECK(OQ4_WindowsNativeFenceRetire());Register(h);CHECK(OQ4_WindowsNativeFenceEnable(true));
 h.prepare=[&](const auto* context){CHECK(context->kind==OQ4_COLLECTION_LIFECYCLE&&peeks==0);return true;};
 h.work=[&](const auto* context){CHECK(h.prepared==1&&h.finished==0);CHECK(OQ4_WIN_CurrentNativeDispatch()==context->dispatch);CHECK(!OQ4_WindowsNativeFenceRunLifecycle(LifecycleWork,&h));WIN_PumpEvents(&video);CHECK(peeks==0);Emit(881);return true;};
 // Pending is deliberately unavailable until Finish returns; don't use Current helper here.
 h.finish=[&](const auto*,bool aborted){CHECK(!aborted&&h.worked==2);OQ4_NativeFence pending{};CHECK(!OQ4_WindowsNativeFencePending(&pending));return true;};
 CHECK(OQ4_WindowsNativeFenceRunLifecycle(LifecycleWork,&h));CHECK(h.finished==1&&Current().event_count==1&&peeks==0);Ack();
 for(int mode=0;mode<3;++mode){Reset();HookState fail;Register(fail);CHECK(OQ4_WindowsNativeFenceEnable(true));
  fail.work=[&](const auto* context){if(mode==0)return false;if(mode==1){CHECK(OQ4_WindowsNativeFenceRetire());CHECK(!OQ4_WindowsNativeFenceRegisterHooks(nullptr)&&!OQ4_WindowsNativeFenceEnable(true));}else ++const_cast<OQ4_NativeCollectionContext*>(context)->kind;return true;};
  CHECK(!OQ4_WindowsNativeFenceRunLifecycle(LifecycleWork,&fail));CHECK(fail.prepared==1&&fail.worked==1&&fail.finished==1&&fail.aborted&&peeks==0&&!OQ4_WindowsNativeFenceHealthy());
 }
}
#ifdef _MSC_VER
extern "C" __declspec(dllimport) void __stdcall RaiseException(DWORD,DWORD,DWORD,const uintptr_t*);
static void Raise(){RaiseException(0xe1234567u,0,0,nullptr);}
static bool CatchPump(){__try {WIN_PumpEvents(&video);}__except(1){return true;}return false;}
static bool CatchLifecycle(HookState* hooks){__try {(void)OQ4_WindowsNativeFenceRunLifecycle(LifecycleWork,hooks);}__except(1){return true;}return false;}
static void Abnormal(){
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=Raise;CHECK(CatchPump());CHECK(!OQ4_WindowsNativeFenceHealthy()&&OQ4_WIN_NativeFenceBlocked());CHECK(!SDL_processing_messages&&!oq4_fence_collecting&&!oq4_fence_in_pump&&scopeDepth==0);CHECK(events.empty());
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();filter=[](SDL_Event*ev){if(ev->type==OQ4_WindowsNativeFenceEventType())Raise();return true;};CHECK(CatchPump());CHECK(!OQ4_WindowsNativeFenceHealthy()&&OQ4_WIN_NativeFenceBlocked());CHECK(!oq4_fence_emitting&&!oq4_fence_admission);
}
static void AbnormalHooks(){
 for(int mode=0;mode<8;++mode){Reset();HookState h;Register(h);CHECK(OQ4_WindowsNativeFenceEnable(true));
  h.prepare=[&](const auto*){if(mode==0||mode==4){Raise();}if(mode==3||mode==7){CHECK(OQ4_WindowsNativeFenceRetire());Raise();}return true;};
  h.finish=[&](const auto*,bool aborted){CHECK(h.finished==1);if(mode==1||mode==5){CHECK(!aborted);Raise();}else CHECK(aborted);return true;};
  if(mode<4){Posted();if(mode==2)inDispatch=Raise;CHECK(CatchPump());}
  else {if(mode==6)h.work=[](const auto*)->bool {Raise();return true;};CHECK(CatchLifecycle(&h));}
  CHECK(h.prepared==1&&h.finished==1&&!OQ4_WindowsNativeFenceHealthy());
  CHECK(!oq4_fence_prepare_attempted&&!oq4_fence_callback_depth&&!oq4_fence_context_active&&!oq4_fence_in_pump&&!oq4_fence_collecting);
  CHECK(OQ4_WindowsNativeFenceRetire());CHECK(OQ4_WindowsNativeFenceRegisterHooks(nullptr));CHECK(OQ4_WindowsNativeFenceEnable(true));
 }
}
#endif
int main(){DisabledAndCollection();EmptySentAndHousekeeping();EarlyExits();FailuresAndAtomicity();ReentryAndLifecycle();HeldWait();QueueStream();QueueLoss();QueueForgeryAndLifecycle();QueueBounds();QueueForgedMetadata();Bounds();HooksAndZeroEvent();ExactHookContextAndReentry();HookFailuresAndRetirement();LifecycleCollections();
#ifdef _MSC_VER
Abnormal();
AbnormalHooks();
#endif
#ifdef _MSC_VER
std::printf("SEH cleanup enabled\n");
#else
std::printf("SEH cleanup unavailable in this C toolchain\n");
#endif
OQ4_WindowsNativeFenceRetire();SDL_StopEventLoop();std::printf("PASS %d checks\n",checks);}
