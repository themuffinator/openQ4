// Counted Windows edges; actual Sys_Error and Sys_PumpEvents are injected.
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <functional>
using DWORD=unsigned long;
struct MSG {unsigned time=17;};
static constexpr int STD_ERROR_HANDLE=-12,FAST_FAIL_FATAL_APP_EXIT=7,PM_NOREMOVE=0;
static std::vector<std::string> calls;
static bool owned=false,retired=true,terminateSucceeds=true,writeSucceeds=true;
static unsigned tests=0;
static std::function<void()> onRetire;
static std::string diagnostic;
#define CHECK(...) do{++tests;if(!(__VA_ARGS__)){std::fprintf(stderr,"FAIL pump line %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(1);}}while(false)
static bool NativeInput_OwnsPump()noexcept{return owned;}
static bool NativeInput_FatalRetire()noexcept{calls.emplace_back("retire");auto callback=std::move(onRetire);if(callback)callback();return retired;}
static int GetStdHandle(int){calls.emplace_back("stderr");return 3;}
static int GetCurrentProcess(){calls.emplace_back("current-process");return 4;}
static bool WriteFile(int handle,const char* message,DWORD length,DWORD*,void*){CHECK(handle==3&&length<=2048);calls.emplace_back("write");diagnostic.assign(message,length);return writeSucceeds;}
static bool TerminateProcess(int handle,unsigned code){CHECK(handle==4&&code==1);calls.emplace_back("terminate");if(terminateSucceeds)throw 1;return false;}
[[noreturn]] static void TestFastFail(int code){CHECK(code==FAST_FAIL_FATAL_APP_EXIT);calls.emplace_back("failfast");throw 2;}
struct idStr{static void vsnPrintf(char* out,std::size_t size,const char* fmt,va_list args){std::vsnprintf(out,size,fmt,args);}};
static void Conbuf_AppendText(const char* text){calls.emplace_back("conbuf");diagnostic+=text;}
static void Win_SetErrorText(const char*){calls.emplace_back("window-error");}
static void Sys_ShowConsole(int,bool){calls.emplace_back("show");}
static void timeEndPeriod(int){calls.emplace_back("timer");}
static void Sys_ShutdownInput(){calls.emplace_back("shutdown");}
static void GLimp_Shutdown(){calls.emplace_back("GLshutdown");}
static void Sys_DestroyConsole(){calls.emplace_back("destroy");}
static bool GetMessage(MSG*,void*,int,int){calls.emplace_back("get-message");return false;}
static bool PeekMessage(MSG*,void*,int,int,int){calls.emplace_back("peek-message");return false;}
static void TranslateMessage(MSG*){calls.emplace_back("translate");}
static void DispatchMessage(MSG*){calls.emplace_back("dispatch");}
static bool Sys_SDL_PumpEvents(){calls.emplace_back("SDL-pump");return true;}
struct Common {[[noreturn]] void Quit(){calls.emplace_back("quit");throw 3;}} instance,*common=&instance;
struct Render {bool IsOpenGLRunning(){calls.emplace_back("renderer");return false;}} render,*renderSystem=&render;
struct {int sysMsgTime=0;} win32;
#define USE_SDL3 1
// PRODUCTION_METHODS
int main(){
 for(bool succeeds:{false,true})for(bool write:{false,true}){calls.clear();retired=false;terminateSucceeds=succeeds;writeSucceeds=write;
  std::string large(5000,'x');int ended=0;try{Sys_Error(large.c_str());}catch(int value){ended=value;}
  CHECK(ended==(succeeds?1:2));CHECK(calls.size()==(succeeds?5u:6u));CHECK(calls[0]=="retire"&&calls[1]=="stderr"&&calls[2]=="write"&&calls[3]=="current-process"&&calls[4]=="terminate");if(!succeeds)CHECK(calls.back()=="failfast");}
 calls.clear();retired=true;int ended=0;try{Sys_Error("original %s","failure");}catch(int value){ended=value;}
 CHECK(ended==3);CHECK(calls.front()=="retire"&&calls[1]=="conbuf"&&calls.back()=="quit");
 for(const auto& c:calls)CHECK(c!="write"&&c!="terminate"&&c!="failfast");
 calls.clear();owned=true;Sys_PumpEvents();CHECK(calls.empty());
 owned=false;Sys_PumpEvents();CHECK(calls.size()==2&&calls[0]=="SDL-pump"&&calls[1]=="peek-message");
 for(bool released:{false,true}){calls.clear();diagnostic.clear();retired=released;terminateSucceeds=true;
  char format[32]="before %s",argument[32]="original";
  onRetire=[&]{std::memcpy(format,"after %s",sizeof("after %s"));std::memcpy(argument,"replacement",sizeof("replacement"));};
  int diagnosticEnd=0;try{Sys_Error(format,argument);}catch(int value){diagnosticEnd=value;}
  CHECK(diagnosticEnd==(released?3:1));CHECK(diagnostic==(released?"before original\n":"before original"));
 }
 std::printf("Native pump/fatal edges: %u checks\n",tests);
}
