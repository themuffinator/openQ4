// Counted stand-ins around production private SDL observer and admission bodies.
// No native windows, keyboard input, IME, clipboard, or SDL initialization.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using Uint32=uint32_t; using Uint64=uint64_t; using Sint32=int32_t;
using DWORD=uint32_t; using LONG=int32_t; using WCHAR=char16_t; using HIMC=void*;
using HWND=void*; using UINT=unsigned; using WPARAM=uintptr_t; using LPARAM=intptr_t; using LRESULT=intptr_t;
using SDL_PropertiesID=uint32_t;
#define CALLBACK
#define SDL_DECLSPEC
#define SDLCALL
#define SDL_MAX_UINT64 UINT64_MAX
#define SDL_VIDEO_DRIVER_WINDOWS 1
#define SDL_DISABLE_WINDOWS_IME 1
#define SDL_zero(v) std::memset(&(v),0,sizeof(v))
#define SDL_zeroa(v) std::memset((v),0,sizeof(v))
#define SDL_memcpy std::memcpy
constexpr Uint32 SDL_EVENT_TEXT_INPUT=0x303, SDL_ADDEVENT=0;
constexpr unsigned WM_IME_STARTCOMPOSITION=0x10d,WM_IME_ENDCOMPOSITION=0x10e,WM_IME_COMPOSITION=0x10f;
constexpr unsigned WM_IME_CHAR=0x286,WM_CHAR=0x102,WM_UNICHAR=0x109,WM_KILLFOCUS=8,WM_SHOWWINDOW=0x18,WM_NCDESTROY=0x82,WM_INPUTLANGCHANGE=0x51;
constexpr DWORD GCS_COMPSTR=8,GCS_COMPATTR=0x10,GCS_CURSORPOS=0x80,GCS_RESULTSTR=0x800;
constexpr unsigned CP_UTF8=65001,WC_ERR_INVALID_CHARS=0x80;
struct WindowData { HWND hwnd; };
struct SDL_Window { Uint32 id; WindowData* internal; bool text_input_active=true; };
struct SDL_VideoDevice {};
struct SDL_CommonEvent { Uint32 type,reserved;Uint64 timestamp; };
struct SDL_TextInputEvent { Uint32 type,reserved;Uint64 timestamp;Uint32 windowID;const char* text; };
struct SDL_UserEvent { Uint32 type,reserved;Uint64 timestamp;Uint32 windowID;Sint32 code;void* data1;void* data2; };
union SDL_Event { Uint32 type; SDL_CommonEvent common;SDL_TextInputEvent text;SDL_UserEvent user; };
struct SDL_Keyboard { SDL_Window* focus=nullptr; } SDL_keyboard;
static std::vector<SDL_Event> queued;
static std::deque<std::string> temporaries;
static std::vector<SDL_Window*> windows;
static bool mainThread=true,rejectMarker=false,rejectText=false,failAdmission=false,allocationFail=false;
static bool mutateText=false,reenterCopy=false;
static int mutateMarker=0,reenterText=0;
static int allocations=0,frees=0,contexts=0,releases=0,checks=0,deadKeys=0,legacyCalls=0;
static HIMC context=reinterpret_cast<HIMC>(uintptr_t(1));
static std::u16string preedit,resultText;
static LONG cursor=0; static bool readFail=false,readChanged=false,conversionFail=false;
static std::string error;
static void Check(bool value,const char* message) { ++checks;if(!value)throw std::runtime_error(message); }
static bool SDL_IsMainThread(){return mainThread;}
static bool SDL_SetError(const char* text){error=text;return false;}
static void SDL_ClearError(){error.clear();}
static void* SDL_malloc(size_t size){if(allocationFail)return nullptr;auto*p=std::malloc(size);if(p)++allocations;return p;}
static void SDL_free(void* p){if(p)++frees;std::free(p);}
static size_t SDL_strnlen(const char* text,size_t n){if(!text)return 0;size_t i=0;while(i<n&&text[i])++i;return i;}
static size_t SDL_wcslen(const WCHAR*text){size_t n=0;while(text[n])++n;return n;}
static Uint32 SDL_RegisterEvents(int){static Uint32 next=0x8001;return next++;}
static SDL_Window* SDL_GetWindowFromID(Uint32 id){for(auto*w:windows)if(w->id==id)return w;return nullptr;}
static bool SDL_TextInputActive(SDL_Window*w){return w&&w->text_input_active;}
static bool SDL_EventEnabled(Uint32){return true;}
static int SDL_iscntrl(unsigned char c){return std::iscntrl(c);}
static const char* SDL_CreateTemporaryString(const char*s){temporaries.emplace_back(s);return temporaries.back().c_str();}
static Uint64 SDL_GetTicksNS(){static Uint64 ticks=0;return ++ticks;}
static HIMC ImmGetContext(HWND){++contexts;return context;}
static bool ImmReleaseContext(HWND,HIMC){++releases;return true;}
static LONG ImmGetCompositionStringW(HIMC,DWORD query,void*out,DWORD bytes){
    if(readFail)return -1;if(query==GCS_CURSORPOS)return cursor;
    const auto&s=query==GCS_RESULTSTR?resultText:preedit;
    if(!out)return LONG(s.size()*2);
    if(bytes<s.size()*2)return -1;std::memcpy(out,s.data(),s.size()*2);
    return LONG(s.size()*2)-(readChanged?2:0);
}
static int WIN_WideCharToMultiByte(unsigned,unsigned,const WCHAR*in,int length,char*out,int capacity,void*,void*){
    if(conversionFail)return 0;std::string encoded;
    for(int i=0;i<length;++i){uint32_t c=in[i];if(c>=0xd800&&c<=0xdbff){if(++i==length||in[i]<0xdc00||in[i]>0xdfff)return 0;c=0x10000+((c-0xd800)<<10)+(in[i]-0xdc00);}else if(c>=0xdc00&&c<=0xdfff)return 0;
        if(c<0x80)encoded.push_back(char(c));else if(c<0x800){encoded.push_back(char(0xc0|(c>>6)));encoded.push_back(char(0x80|(c&63)));}
        else if(c<0x10000){encoded.push_back(char(0xe0|(c>>12)));encoded.push_back(char(0x80|((c>>6)&63)));encoded.push_back(char(0x80|(c&63)));}
        else{encoded.push_back(char(0xf0|(c>>18)));encoded.push_back(char(0x80|((c>>12)&63)));encoded.push_back(char(0x80|((c>>6)&63)));encoded.push_back(char(0x80|(c&63)));}}
    if(!out)return int(encoded.size());if(capacity<int(encoded.size()))return 0;std::memcpy(out,encoded.data(),encoded.size());return int(encoded.size());
}
bool SDL_PushEvent(SDL_Event*);
void SDL_SendKeyboardText(const char*);
static bool SDL_CallEventWatchers(SDL_Event*);
static int SDL_PeepEvents(SDL_Event*,int,int,int,int);
static void WIN_ResetDeadKeys(){++deadKeys;}
static bool WIN_UpdateTextInputArea(SDL_VideoDevice*,SDL_Window*){++legacyCalls;return true;}
#include "provenance-api.inc"
#include "provenance-internal.inc"
#include "provenance.inc"
static bool SDL_CallEventWatchers(SDL_Event*e){
    if(e->type==SDL_EVENT_TEXT_INPUT){
        if(reenterText){const int mode=reenterText;reenterText=0;
            if(mode==3)SDL_SendKeyboardText("injected");
            else if(mode==2){const char*original=e->text.text;e->text.text="injected";Check(SDL_PushEvent(e),"reentrant same-pointer push supported");e->text.text=original;}
            else{SDL_Event injected{};injected.type=SDL_EVENT_TEXT_INPUT;injected.text.windowID=e->text.windowID;injected.text.text="injected";Check(SDL_PushEvent(&injected),"reentrant public push supported");}
        }
        if(mutateText)e->text.text="filtered";return !rejectText;
    }
    if(reenterCopy){OQ4_TextRecord r{};char s[16]{};Check(!OQ4_WindowsTextCopy(e,&r,s,sizeof(s)),"watcher cannot consume half-admitted marker");Check(!OQ4_WindowsTextReset(),"watcher cannot reset half-admitted marker");}
    if(mutateMarker==1)++e->user.code;
    if(mutateMarker==2)++e->user.windowID;
    if(mutateMarker==3)e->type=SDL_EVENT_TEXT_INPUT;
    if(mutateMarker==4)e->user.data1=reinterpret_cast<void*>(uintptr_t(1));
    if(mutateMarker==5)e->user.data2=reinterpret_cast<void*>(uintptr_t(1));
    if(mutateMarker==6)++e->common.reserved;
    if(mutateMarker==7)++e->common.timestamp;
    return !rejectMarker;
}
static int SDL_PeepEvents(SDL_Event*e,int,int,int,int){if(failAdmission&&e->type==SDL_EVENT_TEXT_INPUT)return 0;queued.push_back(*e);return 1;}
#include "admission.inc"
static SDL_Window* currentWindow=nullptr;
static bool nestedResult=false;
static LRESULT WIN_WindowProcObserved(HWND,UINT msg,WPARAM wp,LPARAM lp);
LRESULT CALLBACK WIN_WindowProc(HWND,UINT,WPARAM,LPARAM);
static LRESULT WIN_WindowProcObserved(HWND,UINT msg,WPARAM wp,LPARAM lp){
    OQ4_WIN_ObserveTextMessage(currentWindow,msg,wp,lp);
    ++legacyCalls;
    if(msg==WM_CHAR)SDL_SendKeyboardText("x");
    if(msg==WM_IME_COMPOSITION&&nestedResult)WIN_WindowProc(nullptr,WM_CHAR,0,0);
    return 123;
}
#include "window-scope.inc"
#include "session.inc"
struct Captured { OQ4_TextRecord record;std::string text;Uint32 token; };
static std::vector<Captured> Drain(){
    std::vector<Captured>out;for(const auto&e:queued)if(e.type==oq4_event_type){OQ4_TextRecord r{};std::vector<char>text(65537);Check(OQ4_WindowsTextCopy(&e,&r,text.data(),text.size()),"copy marker");out.push_back({r,text.data(),Uint32(e.user.code)});Check(OQ4_WindowsTextRelease(&e),"release marker");Check(!OQ4_WindowsTextRelease(&e),"release once");}queued.clear();return out;
}
static void Fresh(SDL_Window&w){
    OQ4_WIN_TextShutdown();Check(allocations==frees,"observer storage balanced at shutdown");queued.clear();temporaries.clear();windows={&w};currentWindow=&w;SDL_keyboard.focus=&w;
    mainThread=true;rejectMarker=rejectText=failAdmission=allocationFail=mutateText=reenterCopy=false;mutateMarker=reenterText=0;readFail=readChanged=conversionFail=nestedResult=false;context=reinterpret_cast<HIMC>(uintptr_t(1));preedit.clear();resultText.clear();cursor=0;
    Check(OQ4_WindowsTextEnable(true),"enable observer");SDL_VideoDevice device;Check(WIN_StartTextInput(&device,&w,0),"legacy start succeeds");Drain();
}
static Captured OnlyCommit(const std::vector<Captured>&records){std::vector<Captured>v;for(auto&r:records)if(r.record.kind==OQ4_TEXT_COMMIT)v.push_back(r);Check(v.size()==1,"exactly one canonical commit");return v.at(0);}
int main(){try{
    WindowData native{reinterpret_cast<HWND>(uintptr_t(10))};SDL_Window window{1,&native};SDL_VideoDevice device;
    Fresh(window);const auto initialLifetime=oq4_windows[0].lifetime;
    Check(WIN_WindowProc(nullptr,WM_CHAR,0,0)==123,"wrapper preserves native return");Check(queued.size()==2,"marker precedes public commit");const auto publicText=queued[1];Check(OQ4_WindowsTextAssociation(&publicText)==Uint32(queued[0].user.code),"explicit association");
    auto direct=OnlyCommit(Drain());Check(direct.text=="x"&&direct.record.origin==OQ4_TEXT_UNMARKED_CHARACTER,"unmarked character path");Check(direct.record.window_lifetime==initialLifetime&&direct.record.session!=0,"native lifetime and session");
    WIN_WindowProc(nullptr,WM_IME_STARTCOMPOSITION,0,0);preedit=u"\U0001F642a";cursor=2;WIN_WindowProc(nullptr,WM_IME_COMPOSITION,0,GCS_COMPSTR);resultText=u"\u03bb";nestedResult=true;WIN_WindowProc(nullptr,WM_IME_COMPOSITION,0,GCS_RESULTSTR);nestedResult=false;WIN_WindowProc(nullptr,WM_IME_ENDCOMPOSITION,0,0);
    auto composition=Drain();std::vector<Uint32>kinds;for(auto&r:composition)kinds.push_back(r.record.kind);
    Check(kinds==std::vector<Uint32>{OQ4_TEXT_BEGIN,OQ4_TEXT_PREEDIT,OQ4_TEXT_PREEDIT,OQ4_TEXT_RESULT,OQ4_TEXT_COMMIT,OQ4_TEXT_END},"ordered begin preedit clear result commit end");
    Check(composition[1].text=="\xf0\x9f\x99\x82" "a"&&composition[1].record.selection_start==2,"UTF16 cursor units and full UTF8");
    const auto origin=composition[0].record.composition;Check(origin!=0,"composition nonzero");for(auto&r:composition)Check(r.record.composition==origin,"clear/result/commit retain exact origin");
    Check(OnlyCommit(composition).record.origin==OQ4_TEXT_OBSERVED_COMPOSITION,"synchronous default result retains origin");
    WIN_WindowProc(nullptr,WM_CHAR,0,0);Check(OnlyCommit(Drain()).record.origin==OQ4_TEXT_UNKNOWN,"asynchronous later character is unknown, never content matched");
    WIN_WindowProc(nullptr,WM_IME_STARTCOMPOSITION,0,0);nestedResult=true;WIN_WindowProc(nullptr,WM_IME_COMPOSITION,0,GCS_RESULTSTR);nestedResult=false;Check(OnlyCommit(Drain()).record.origin==OQ4_TEXT_UNKNOWN,"ended old composition cannot be adopted by new begin");
    Fresh(window);WIN_WindowProc(nullptr,WM_IME_STARTCOMPOSITION,0,0);WIN_WindowProc(nullptr,WM_CHAR,0,0);Check(OnlyCommit(Drain()).record.origin==OQ4_TEXT_UNKNOWN,"unscoped character during composition is unknown");
    Fresh(window);WIN_WindowProc(nullptr,WM_IME_STARTCOMPOSITION,0,0);const auto old=oq4_windows[0].composition;WIN_ClearComposition(&device,&window);WIN_WindowProc(nullptr,WM_IME_STARTCOMPOSITION,0,0);resultText=u"x";nestedResult=true;WIN_WindowProc(nullptr,WM_IME_COMPOSITION,0,GCS_RESULTSTR);auto canceled=Drain();Check(oq4_windows[0].composition!=old,"new begin never reuses identity");Check(OnlyCommit(canceled).record.origin==OQ4_TEXT_UNKNOWN,"cancel then new begin cannot adopt old result");
    Fresh(window);WIN_WindowProc(nullptr,WM_IME_STARTCOMPOSITION,0,0);WIN_WindowProc(nullptr,WM_IME_STARTCOMPOSITION,0,0);resultText=u"x";nestedResult=true;WIN_WindowProc(nullptr,WM_IME_COMPOSITION,0,GCS_RESULTSTR);Check(OnlyCommit(Drain()).record.origin==OQ4_TEXT_UNKNOWN,"overlapping begin unknown");
    Fresh(window);resultText=u"x";nestedResult=true;WIN_WindowProc(nullptr,WM_IME_COMPOSITION,0,GCS_RESULTSTR);Check(OnlyCommit(Drain()).record.origin==OQ4_TEXT_UNKNOWN,"result without begin unknown");
    Fresh(window);WIN_WindowProc(nullptr,WM_IME_STARTCOMPOSITION,0,0);context=reinterpret_cast<HIMC>(uintptr_t(2));preedit=u"a";WIN_WindowProc(nullptr,WM_IME_COMPOSITION,0,GCS_COMPSTR);auto changed=Drain();Check(changed.back().record.origin==OQ4_TEXT_UNKNOWN,"context replacement unknown");
    Fresh(window);mutateText=true;WIN_WindowProc(nullptr,WM_CHAR,0,0);Check(OnlyCommit(Drain()).text=="filtered","public filter transformation precedes private copy");
    for(int mode=1;mode<=3;++mode){
        Fresh(window);WIN_WindowProc(nullptr,WM_IME_STARTCOMPOSITION,0,0);const auto nativeOrigin=oq4_windows[0].composition;
        resultText=u"x";nestedResult=true;reenterText=mode;WIN_WindowProc(nullptr,WM_IME_COMPOSITION,0,GCS_RESULTSTR);
        auto records=Drain();int commits=0;for(const auto&item:records)if(item.record.kind==OQ4_TEXT_COMMIT){++commits;
            if(item.text=="injected")Check(item.record.origin==OQ4_TEXT_UNKNOWN&&item.record.composition==0&&item.record.native_message==0,"reentrant synthetic text cannot inherit native scope");
            else Check(item.text=="x"&&item.record.origin==OQ4_TEXT_OBSERVED_COMPOSITION&&item.record.composition==nativeOrigin,"original admission retains immutable native origin");}
        Check(commits==2&&OQ4_WindowsTextHealthy(),"synthetic and native commits remain distinct and ordered");
    }
    for(int mode=1;mode<=6;++mode){
        Fresh(window);mutateMarker=mode;WIN_WindowProc(nullptr,WM_CHAR,0,0);
        Check(!OQ4_WindowsTextHealthy()&&oq4_bytes==0,"changed marker faults and frees allocation");
        Check(queued.size()==1&&queued[0].type==SDL_EVENT_TEXT_INPUT&&queued[0].common.reserved==OQ4_TEXT_FAILED,"changed marker rejected before queue, explicit public fallback");
    }
    Fresh(window);mutateMarker=7;WIN_WindowProc(nullptr,WM_CHAR,0,0);Check(OQ4_WindowsTextHealthy()&&OnlyCommit(Drain()).text=="x","SDL-owned marker timestamp may change");
    Fresh(window);rejectText=true;WIN_WindowProc(nullptr,WM_CHAR,0,0);Check(queued.empty(),"rejected public commit has no private bypass");
    Fresh(window);rejectMarker=true;WIN_WindowProc(nullptr,WM_CHAR,0,0);Check(!OQ4_WindowsTextHealthy()&&queued.size()==1&&OQ4_WindowsTextAssociation(&queued[0])==OQ4_TEXT_FAILED,"marker refusal keeps explicit legacy fallback");Check(oq4_bytes==0,"failed marker allocation freed");
    Fresh(window);failAdmission=true;WIN_WindowProc(nullptr,WM_CHAR,0,0);Check(!OQ4_WindowsTextHealthy()&&oq4_bytes==0,"public admission refusal removes private commit");OQ4_TextRecord unchanged{};unchanged.version=99;char unchangedText[8]="before";Check(!OQ4_WindowsTextCopy(&queued[0],&unchanged,unchangedText,8)&&unchanged.version==99&&std::string(unchangedText)=="before","removed marker copy is atomic");
    Fresh(window);reenterCopy=true;WIN_WindowProc(nullptr,WM_CHAR,0,0);Drain();
    Fresh(window);mainThread=false;SDL_Event background{};background.type=SDL_EVENT_TEXT_INPUT;background.text.windowID=1;background.text.text="background";Check(SDL_PushEvent(&background),"SDL background push remains supported");mainThread=true;Check(queued.size()==1&&background.common.reserved==OQ4_TEXT_FAILED,"background commit has no native affinity");
    Fresh(window);mainThread=false;error="preserved";auto offThreadScope=OQ4_WIN_SaveTextScope();OQ4_WIN_ObserveTextMessage(&window,WM_IME_STARTCOMPOSITION,0,0);OQ4_WIN_RestoreTextScope(offThreadScope);mainThread=true;Check(error=="preserved"&&oq4_windows[0].composition==0&&queued.empty(),"native off-thread observation has no shared mutation or legacy error");
    Fresh(window);WIN_WindowProc(nullptr,WM_CHAR,0,0);const auto marker=queued[0];OQ4_TextRecord r{};r.version=99;char tiny[8]={'Q'};Check(!OQ4_WindowsTextCopy(&marker,&r,tiny,1)&&r.version==99&&tiny[0]=='Q',"small copy does not mutate outputs");mainThread=false;Check(!OQ4_WindowsTextRelease(&marker),"off-thread release rejected");mainThread=true;Drain();
    for(int mode=0;mode<6;++mode){Fresh(window);WIN_WindowProc(nullptr,WM_IME_STARTCOMPOSITION,0,0);preedit=u"x";cursor=0;if(mode==0)preedit={char16_t(0xd800)};if(mode==1)preedit={u'a',0,u'b'};if(mode==2){preedit=u"\U0001F642";cursor=1;}if(mode==3)readFail=true;if(mode==4)readChanged=true;if(mode==5)conversionFail=true;WIN_WindowProc(nullptr,WM_IME_COMPOSITION,0,GCS_COMPSTR);Check(!OQ4_WindowsTextHealthy(),"invalid native composition poisons stream");Check(contexts==releases,"all successful contexts released");}
    Fresh(window);allocationFail=true;WIN_WindowProc(nullptr,WM_CHAR,0,0);Check(!OQ4_WindowsTextHealthy()&&queued.size()==1&&queued[0].common.reserved==OQ4_TEXT_FAILED,"OOM keeps legacy fallback");
    Fresh(window);SDL_Event nullText{};nullText.type=SDL_EVENT_TEXT_INPUT;nullText.text.windowID=1;Check(SDL_PushEvent(&nullText)&&!OQ4_WindowsTextHealthy()&&nullText.common.reserved==OQ4_TEXT_FAILED,"null synthetic text does not dereference");
    Fresh(window);for(int i=0;i<OQ4_TEXT_SLOTS;++i)SDL_SendKeyboardText("a");Check(OQ4_WindowsTextHealthy(),"exact record bound accepted");SDL_SendKeyboardText("b");Check(!OQ4_WindowsTextHealthy()&&oq4_bytes==OQ4_TEXT_SLOTS*2,"record overflow bounded");queued.clear();Check(OQ4_WindowsTextReset()&&oq4_bytes==0,"external queue flush reconciled by explicit reset");
    Fresh(window);std::string big(OQ4_TEXT_MAX,'a');for(int i=0;i<15;++i)SDL_SendKeyboardText(big.c_str());Check(OQ4_WindowsTextHealthy(),"byte budget usable");SDL_SendKeyboardText(big.c_str());Check(!OQ4_WindowsTextHealthy()&&oq4_bytes<=OQ4_TEXT_BYTES,"aggregate byte budget bounded");
    Fresh(window);for(auto bad:std::vector<std::string>{"\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xe2\x82"}){Check(OQ4_WindowsTextReset(),"reset before malformed commit");SDL_SendKeyboardText(bad.c_str());Check(!OQ4_WindowsTextHealthy(),"invalid UTF8 private commit rejected");}
    Fresh(window);const auto lifetime=oq4_windows[0].lifetime;WIN_WindowProc(nullptr,WM_NCDESTROY,0,0);window.id=2;Fresh(window);Check(oq4_windows[0].lifetime>lifetime,"window pointer reuse gets new lifetime");
    Fresh(window);WIN_StopTextInput(&device,&window);auto stop=Drain();Check(stop.size()==2&&stop[0].record.kind==OQ4_TEXT_CANCEL&&stop[1].record.kind==OQ4_TEXT_SESSION_END,"cancel precedes session end");
    Fresh(window);for(int i=0;i<OQ4_TEXT_WINDOWS+1;++i){
        const auto oldLifetime=oq4_windows[0].lifetime;
        Check(OQ4_WindowsTextEnable(false),"disable before native destruction");
        WIN_WindowProc(nullptr,WM_NCDESTROY,0,0);
        Check(queued.empty(),"disabled destruction emits no private lifecycle");
        Check(std::none_of(std::begin(oq4_windows),std::end(oq4_windows),[](const auto&item){return item.window!=nullptr;}),"disabled destruction retires its registry slot");
        ++window.id;Check(OQ4_WindowsTextEnable(true),"observer reenabled after native destruction");WIN_StartTextInput(&device,&window,0);
        Check(oq4_windows[0].lifetime>oldLifetime&&OQ4_WindowsTextHealthy(),"repeated recreated windows get fresh live identities");Drain();
    }
    Fresh(window);oq4_token=0x7ffffffe;SDL_SendKeyboardText("last");Check(OQ4_WindowsTextHealthy(),"last positive marker accepted");SDL_SendKeyboardText("overflow");Check(!OQ4_WindowsTextHealthy()&&!OQ4_WindowsTextReset(),"marker exhaustion never wraps");
    OQ4_WIN_TextShutdown();Check(allocations==frees,"all private allocations released");Check(deadKeys>0&&legacyCalls>0,"legacy native path exercised");
    std::cout<<"PASS "<<checks<<" checks\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
