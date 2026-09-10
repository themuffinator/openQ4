// Production Windows pump/fence/queue methods; all native calls are counted doubles.
// No OS input, window, IME, clipboard, or game operation.
#include <cstdint>
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
struct SDL_AtomicInt { int value=0; };
static int SDL_GetAtomicInt(SDL_AtomicInt*p){return p->value;}
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
static bool SDL_IsMainThread(){return mainThread;}
static bool SDL_SetError(const char*s){error=s;return false;}
static void SDL_ClearError(){error.clear();}
static Uint64 SDL_GetTicksNS(){return 11;}
static Uint32 nextType=0x9000;
static Uint32 SDL_RegisterEvents(int){return nextType++;}
static bool SDL_PushEvent(SDL_Event*);
#include "fence-api.inc"
#include "fence-internal.inc"
#include "fence.inc"
static std::deque<MSG> nativeQueue;
static std::vector<SDL_Event> events;
static std::function<void()> inPeek,inDispatch,inHousekeeping;
static std::function<bool(SDL_Event*)> filter;
static int peeks=0,dispatches=0,translations=0,waits=0,gameUpdates=0,tailUpdates=0,queueAttempts=0;
static bool queueFails=false;
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
static bool SDL_CallEventWatchers(SDL_Event*e){return !filter||filter(e);}
static int SDL_PeepEvents(SDL_Event*e,int,int action,Uint32,Uint32){
 if(action==SDL_GETEVENT){if(events.empty())return 0;if(e)*e=events.front();events.erase(events.begin());return 1;}
 ++queueAttempts;if(queueFails)return -1;events.push_back(*e);return 1;
}
#include "admission.inc"
#include "pump.inc"
static Sint64 SDL_events_get_polling_interval(){return 10;}
static void SDL_PumpEventsInternal(bool){WIN_PumpEvents(&video);}
static void SDL_SetAtomicPointer(void**p,void*v){*p=v;}
#include "wait.inc"
static void Reset(){
 mainThread=true;OQ4_WindowsNativeFenceRetire();oq4_fence_in_pump=oq4_fence_emitting=false;oq4_fence_admission=nullptr;
 nativeQueue.clear();events.clear();inPeek={};inDispatch={};inHousekeeping={};filter={};queueFails=false;g_WindowsMessageHook=nullptr;
 peeks=dispatches=translations=waits=gameUpdates=tailUpdates=queueAttempts=0;g_WindowsEnableMessageLoop=true;SDL_processing_messages=false;
 videoData.gameinput_context=nullptr;video.windows=nullptr;focus=nullptr;SDL_last_warp_time=0;std::memset(keys,0,sizeof(keys));error.clear();
}
static void Posted(UINT id=WM_KEYDOWN){nativeQueue.push_back({nullptr,id,0,0,5});}
static OQ4_NativeFence Current(){OQ4_NativeFence f{};CHECK(OQ4_WindowsNativeFencePending(&f));return f;}
static void Ack(){const auto f=Current();CHECK(!events.empty());OQ4_NativeFence copy{};CHECK(OQ4_WindowsNativeFenceCopy(&events.back(),&copy));CHECK(copy.dispatch==f.dispatch&&copy.sequence==f.sequence);CHECK(OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));}
static void DisabledAndCollection(){
 Reset();for(int i=0;i<4;i++)Posted();WIN_PumpEvents(&video);CHECK(dispatches==4&&peeks==5&&tailUpdates==1);CHECK(events.empty());
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));for(int i=0;i<3;i++)Posted();
 inDispatch=[](){Emit();Emit(601);const int old=peeks;WIN_PumpEvents(&video);CHECK(peeks==old);CHECK(OQ4_WIN_CurrentNativeDispatch()!=0);};
 inPeek=[](){CHECK(WIN_WindowProc(nullptr,WM_KEYDOWN,0,0)==37);};
 inHousekeeping=[](){Emit(604);};
 WIN_PumpEvents(&video);CHECK(peeks==1&&dispatches==2&&translations==1&&tailUpdates==1);CHECK(nativeQueue.size()==2);CHECK(events.size()==6);
 const auto f=Current();CHECK(f.version==1&&f.event_count==5&&f.sequence>f.dispatch);CHECK(OQ4_WIN_CurrentNativeDispatch()==0);
 const int old=peeks;WIN_PumpEvents(&video);CHECK(peeks==old&&tailUpdates==1);CHECK(WIN_WaitEventTimeout(&video,1000)==0&&waits==0);
 CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence)); // Peeking status is not queue consumption.
 OQ4_NativeFence copy{};CHECK(OQ4_WindowsNativeFenceCopy(&events.back(),&copy));CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch+1,f.sequence));CHECK(!OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence+1));
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
 const auto f=Current();SDL_Event event{};video.WaitEventTimeout=[](SDL_VideoDevice*,Sint64){++waits;if(waits>1)return -1;return 0;};
 CHECK(SDL_WaitEventTimeout_Device(&video,nullptr,&event,0,-1)==1);CHECK(event.type==OQ4_WindowsNativeFenceEventType());
 CHECK(SDL_WaitEventTimeout_Device(&video,nullptr,&event,0,-1)==0);CHECK(waits==0&&peeks==1);
 OQ4_NativeFence copy{};CHECK(OQ4_WindowsNativeFenceCopy(&event,&copy));CHECK(OQ4_WindowsNativeFenceAck(f.dispatch,f.sequence));
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();queueFails=true;WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy());
 CHECK(SDL_WaitEventTimeout_Device(&video,nullptr,&event,0,-1)==0);CHECK(waits==0&&peeks==1);
}
static void Bounds(){
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=[](){for(int i=0;i<=OQ4_FENCE_MAX_EVENTS;i++)Emit();};WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy());CHECK(events.size()==OQ4_FENCE_MAX_EVENTS+1); // final ordinary event is queued but no native mutation may be accepted after fault.
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));auto d=oq4_fence_dispatch;oq4_fence_dispatch=UINT64_MAX;WIN_PumpEvents(&video);CHECK(peeks==0&&!OQ4_WindowsNativeFenceHealthy());oq4_fence_dispatch=d;
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));auto s=oq4_fence_sequence;oq4_fence_sequence=UINT64_MAX;Posted();WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy()&&events.empty());oq4_fence_sequence=s;
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));auto m=oq4_fence_marker;oq4_fence_marker=0x7fffffffu;Posted();WIN_PumpEvents(&video);CHECK(!OQ4_WindowsNativeFenceHealthy()&&events.empty());oq4_fence_marker=m;
}
#ifdef _MSC_VER
extern "C" __declspec(dllimport) void __stdcall RaiseException(DWORD,DWORD,DWORD,const uintptr_t*);
static void Raise(){RaiseException(0xe1234567u,0,0,nullptr);}
static bool CatchPump(){__try {WIN_PumpEvents(&video);}__except(1){return true;}return false;}
static void Abnormal(){
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();inDispatch=Raise;CHECK(CatchPump());CHECK(!OQ4_WindowsNativeFenceHealthy()&&OQ4_WIN_NativeFenceBlocked());CHECK(!SDL_processing_messages&&!oq4_fence_collecting&&!oq4_fence_in_pump&&scopeDepth==0);CHECK(events.empty());
 Reset();CHECK(OQ4_WindowsNativeFenceEnable(true));Posted();filter=[](SDL_Event*ev){if(ev->type==OQ4_WindowsNativeFenceEventType())Raise();return true;};CHECK(CatchPump());CHECK(!OQ4_WindowsNativeFenceHealthy()&&OQ4_WIN_NativeFenceBlocked());CHECK(!oq4_fence_emitting&&!oq4_fence_admission);
}
#endif
int main(){DisabledAndCollection();EmptySentAndHousekeeping();EarlyExits();FailuresAndAtomicity();ReentryAndLifecycle();HeldWait();Bounds();
#ifdef _MSC_VER
Abnormal();
#endif
#ifdef _MSC_VER
std::printf("SEH cleanup enabled\n");
#else
std::printf("SEH cleanup unavailable in this C toolchain\n");
#endif
std::printf("PASS %d checks\n",checks);}

