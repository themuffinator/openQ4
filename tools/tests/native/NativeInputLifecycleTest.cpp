// Generated includes contain complete production methods. Counted foreign
// dependencies observe ordering; no engine/native/GUI/window is activated.
#include "src/framework/NativeInputPublications.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>
static unsigned checks=0,revokes=0;static std::uint64_t publicationTransition=1;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(1);}}while(false)
static std::function<void()> onRevoke;
static const auto originalThread=std::this_thread::get_id();
bool Sys_EventDispositionBoundThread() noexcept{return originalThread==std::this_thread::get_id();}
std::uint64_t Sys_EventDispositionEpoch() noexcept{return Sys_EventDispositionBoundThread()?1:0;}
namespace openq4 {
void NativeInputBeforeSessionChange()noexcept{++revokes;++publicationTransition;if(onRevoke)onRevoke();}
void NativeInputBeforeInputBlockerChange()noexcept{NativeInputBeforeSessionChange();}
void NativeInputBeforeUiChange(std::uint64_t,std::uint64_t)noexcept{NativeInputBeforeSessionChange();}
void NativeInputBeforeWindowChange()noexcept{NativeInputBeforeSessionChange();}
std::uint64_t NativeInputSessionTransition()noexcept{return publicationTransition;}
bool NativeInputPublicationSlotEmpty()noexcept{return true;}
}
#define BIT(x) (1<<(x))
#include "cvar_flags.inc"
struct idStr {
    std::string text;
    idStr(const char* v=""):text(v){}idStr(bool v):text(v?"1":"0"){}idStr(int v):text(std::to_string(v)){}
    idStr(float v):text(std::to_string(v)){}
    int Icmp(const char* v)const{auto a=text,b=std::string(v);for(auto&c:a)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));for(auto&c:b)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return a.compare(b);}
    int Cmp(const char* v)const{return text.compare(v);}const char* c_str()const{return text.c_str();}
    static int Icmp(const char* a,const char* b){return idStr(a).Icmp(b);}
    operator const char*()const{return text.c_str();}int Length()const{return static_cast<int>(text.size());}
    static bool IsNumeric(const char* v){char* e=nullptr;std::strtod(v,&e);return e!=v && !*e;}
    static int FindChar(const char* v,char c){const auto* p=std::strchr(v,c);return p?static_cast<int>(p-v):-1;}
};
struct Common {template<class...T>void Printf(T...){ }template<class...T>void Warning(T...){ }} commonObject;
static Common* common=&commonObject;
struct SessionStub {bool multiplayer=false;bool IsMultiplayer()const{return multiplayer;}} sessionObject;
static SessionStub* session=&sessionObject;
struct idAsyncNetwork {struct Client{bool active;bool IsActive()const{return active;}};static inline Client client{};static inline bool cheats=true;static bool AreCheatsEnabled(){return cheats;}};
struct CvarSystem{void SetModifiedFlags(int){}} cvarSystemObject;static CvarSystem* cvarSystem=&cvarSystemObject;
static unsigned assignments=0;
static void CVar_AssignString(idStr& target,const char* value,bool){++assignments;target=value;}
struct idCVar {
    int flags=CVAR_STATIC|CVAR_BOOL;const char* text="0";float minimum=0,maximum=0;
    int GetFlags()const{return flags;}const char* GetString()const{return text;}const char* GetDescription()const{return "";}
    float GetMinValue()const{return minimum;}float GetMaxValue()const{return maximum;}
    const char** GetValueStrings()const{return nullptr;}void* GetValueCompletion()const{return nullptr;}
};
static void Mem_Free(const void*){}
struct idInternalCVar {
    idStr nameString{"com_asyncInput"},valueString{"0"},resetString{"0"},descriptionString;
    const char* value=valueString.c_str();const char* description="";const char** valueStrings=nullptr;void* valueCompletion=nullptr;
    int flags=CVAR_BOOL|CVAR_NOCHEAT,integerValue=0;float floatValue=0,valueMin=0,valueMax=0;
    void SetModified(){flags|=CVAR_MODIFIED;}
    static const char** CopyValueStrings(const char** p){return p;}
    void Set(const char*,bool,bool);void Reset();void Update(const idCVar*);void UpdateValue();void UpdateCheat();
    void InternalSetString(const char*);void InternalServerSetString(const char*);void InternalSetBool(bool);void InternalSetInteger(int);void InternalSetFloat(float);
    bool GetBool()const{return integerValue!=0;}
};
#include "cvar_methods.inc"
static idInternalCVar com_asyncInput;
static bool consoleBlocked=false,previewOpen=false;
bool openq4::Console_BlocksNativeInput()noexcept{return consoleBlocked;}
static bool RetainedUI_IsOpen(){return previewOpen;}
class idUserInterface {};
struct idSessionLocal {
    idUserInterface *guiTest=nullptr,*guiActive=nullptr;bool insideExecuteMapChange=false,loadingSaveGame=false;
    bool QueryNativeInputPublication(openq4::NativeSessionPublication&)const noexcept;
};
#include "session_query.inc"
struct idUserInterfaceManaged {
    std::uint64_t allocationId=1;bool nativeInputClosing=false,nativeInputChanging=false;
    void MarkNativeInputClosing()noexcept;
    void SetNativeInputChanging(bool)noexcept;
};
template<class T>struct List:std::vector<T>{int Num()const{return static_cast<int>(this->size());}};
struct idUserInterfaceManagerLocal {
    const std::thread::id nativePresenceThread=std::this_thread::get_id();
    bool nativeBoundaryActive=false,textBoundaryActive=false,clipboardBoundaryActive=false;unsigned applicationPumpDepth=0;
    List<idUserInterfaceManaged*> allocations;
    bool QueryNativeInputAllocation(std::uintptr_t,std::uint64_t&)const noexcept;
};
#include "manager_queries.inc"
// The window producer is exercised with counted SDL_GetWindowID only. Source
// observation must use its stored values and never call this native-like getter.
using SDL_Window=int;
static SDL_Window* s_sdlWindow=reinterpret_cast<SDL_Window*>(1);
static bool s_sdlVideoReferenceHeld=true,s_sdlAppInBackground=false;
struct Window{void* hWnd=reinterpret_cast<void*>(2);bool activeApp=true;}win32;
static unsigned windowGetters=0;static std::uint32_t windowId=3;
static std::uint32_t SDL_GetWindowID(SDL_Window*){++windowGetters;return windowId;}
#if !defined(_WIN32)
#define _WIN32
#define OPENQ4_TEST_UNDEFINE_WIN32
#endif
#define OPENQ4_SDL3_CHECKED_NATIVE_QUEUE 1
#include "window_publications.inc"
#if defined(OPENQ4_TEST_UNDEFINE_WIN32)
#undef _WIN32
#endif
static void Cvars() {
    for(unsigned which=0;which<7;++which) {
        idInternalCVar v;const auto start=revokes;
        onRevoke=[&]{CHECK(v.valueString.text=="0");};
        if(which==0)v.Set("1",false,false);
        if(which==1)v.InternalSetString("1");if(which==2)v.InternalServerSetString("1");
        if(which==3)v.InternalSetBool(true);if(which==4)v.InternalSetInteger(1);if(which==5)v.InternalSetFloat(1);
        if(which==6){v.flags|=CVAR_ROM;v.Set("1",true,false);}
        CHECK(revokes==start+1 && v.GetBool());
        onRevoke={};const auto assigned=assignments;v.Set("1",true,false);CHECK(assignments==assigned && revokes==start+1);
        onRevoke=[&]{CHECK(v.valueString.text=="1");};v.Reset();CHECK(!v.GetBool() && revokes==start+2);onRevoke={};
    }
    idInternalCVar v;const auto start=revokes;v.Set("1",false,false);v.Set("0",false,false);
    CHECK(revokes==start+2 && !v.GetBool());v.Reset();CHECK(revokes==start+2);
    v.nameString="other";v.Set("1",true,false);v.Reset();CHECK(revokes==start+2);
    v.nameString="COM_ASYNCINPUT";v.flags|=CVAR_ROM;v.Set("1",false,false);CHECK(!v.GetBool() && revokes==start+2);
    v.InternalSetString("1");CHECK(v.GetBool() && revokes==start+3);
    onRevoke=[&]{CHECK(v.valueString.text=="1");};v.Set(nullptr,true,false);CHECK(!v.GetBool());onRevoke={};
    idCVar metadata;const auto old=revokes;onRevoke=[&]{CHECK(v.valueMax==0);};metadata.maximum=1;v.Update(&metadata);
    CHECK(revokes==old+1 && v.valueMax==1);onRevoke={};
    v.flags=CVAR_CHEAT;sessionObject.multiplayer=true;idAsyncNetwork::cheats=false;v.Set("1",true,false);
    CHECK(!v.GetBool() && revokes==old+1);v.InternalServerSetString("1");CHECK(v.GetBool() && revokes==old+2);
    sessionObject.multiplayer=false;idAsyncNetwork::cheats=true;
}
static void SessionAndManager() {
    idSessionLocal s;idUserInterface a,b;s.guiActive=&a;openq4::NativeSessionPublication out;
    CHECK(s.QueryNativeInputPublication(out) && out.current==reinterpret_cast<std::uintptr_t>(&a) && out.inputAllowed);
    for(unsigned which=0;which<6;++which) {
        s.insideExecuteMapChange=which==0;s.loadingSaveGame=which==1;consoleBlocked=which==2;previewOpen=which==3;
        com_asyncInput.integerValue=which==4;auto saved=publicationTransition;if(which==5)publicationTransition=0;
        CHECK(s.QueryNativeInputPublication(out) && !out.inputAllowed && out.current==reinterpret_cast<std::uintptr_t>(&a));publicationTransition=saved;
    }
    s.insideExecuteMapChange=s.loadingSaveGame=consoleBlocked=previewOpen=false;com_asyncInput.integerValue=0;s.guiTest=&b;
    CHECK(s.QueryNativeInputPublication(out) && out.current==reinterpret_cast<std::uintptr_t>(&b));
    const auto old=out;std::thread t([&]{CHECK(!s.QueryNativeInputPublication(out));});t.join();CHECK(out.current==old.current && out.transition==old.transition);
    idUserInterfaceManagerLocal manager;idUserInterfaceManaged owner;manager.allocations.push_back(&owner);
    const auto address=reinterpret_cast<std::uintptr_t>(&owner);std::uint64_t id=999;
    CHECK(manager.QueryNativeInputAllocation(address,id) && id==owner.allocationId);
    for(unsigned which=0;which<6;++which) {
        manager.nativeBoundaryActive=which==0;manager.textBoundaryActive=which==1;manager.clipboardBoundaryActive=which==2;
        manager.applicationPumpDepth=which==3;owner.nativeInputClosing=which==4;owner.nativeInputChanging=which==5;id=999;
        CHECK(!manager.QueryNativeInputAllocation(address,id) && id==999);
    }
    manager.applicationPumpDepth=0;owner.nativeInputClosing=owner.nativeInputChanging=false;
    onRevoke=[&]{CHECK(!owner.nativeInputChanging);};owner.SetNativeInputChanging(true);onRevoke={};
    CHECK(!manager.QueryNativeInputAllocation(address,id));
    onRevoke=[&]{CHECK(owner.nativeInputChanging);};owner.SetNativeInputChanging(false);onRevoke={};
    CHECK(manager.QueryNativeInputAllocation(address,id));
    onRevoke=[&]{CHECK(!owner.nativeInputClosing);CHECK(manager.QueryNativeInputAllocation(address,id));};owner.MarkNativeInputClosing();onRevoke={};
    CHECK(!manager.QueryNativeInputAllocation(address,id));owner.nativeInputClosing=false;++owner.allocationId;
    CHECK(manager.QueryNativeInputAllocation(address,id) && id==2);id=999;
    std::thread worker([&]{CHECK(!manager.QueryNativeInputAllocation(address,id));});worker.join();CHECK(id==999);
}
static void Windows() {
    using namespace openq4;
    SDL3_PublishNativeWindowIdentity();CHECK(windowGetters==1);NativeWindowPublication claim,out;
    CHECK(Sys_AcquireNativeInputWindowClaim(claim) && claim.registration && claim.association && claim.lifetime);
    CHECK(!Sys_AcquireNativeInputWindowClaim(out));auto expected=claim;
    for(unsigned i=0;i<5;++i){CHECK(Sys_QueryNativeInputWindow(out));CHECK(out.providerOwned && out.registration==claim.registration);}
    CHECK(windowGetters==1);SDL3_RetireNativeWindowPublication();CHECK(Sys_QueryNativeInputWindow(out)&&!out.windowAllowed&&out.providerOwned);
    SDL3_PublishNativeWindowIdentity();CHECK(Sys_QueryNativeInputWindow(out)&&out.windowAllowed&&out.lifetime!=claim.lifetime);
    // Reused HWND/SDL ID is a new window, while original provider claim remains
    // independently owned and can be explicitly retired/released.
    auto wrong=claim;++wrong.registration;CHECK(!Sys_ReleaseNativeInputWindowClaim(wrong));
    CHECK(Sys_ReleaseNativeInputWindowClaim(claim));CHECK(!Sys_ReleaseNativeInputWindowClaim(claim));
    CHECK(Sys_AcquireNativeInputWindowClaim(claim) && claim.registration!=expected.registration);
    s_nativeWindowHighwater=UINT64_MAX;SDL3_RetireNativeWindowPublication();SDL3_PublishNativeWindowIdentity();
    CHECK(Sys_QueryNativeInputWindow(out)&&!out.windowAllowed&&out.providerOwned&&out.registration==claim.registration);
    const auto saved=out;std::thread t([&]{CHECK(!Sys_QueryNativeInputWindow(out));CHECK(!Sys_ReleaseNativeInputWindowClaim(claim));});t.join();
    CHECK(out.registration==saved.registration);CHECK(Sys_ReleaseNativeInputWindowClaim(claim));CHECK(!Sys_AcquireNativeInputWindowClaim(out));
}
int main(){Cvars();SessionAndManager();Windows();std::printf("NativeInputLifecycleTest PASS %u checks\n",checks);}
