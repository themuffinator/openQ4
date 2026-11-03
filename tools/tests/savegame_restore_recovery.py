#!/usr/bin/env python3
"""Execute production error, save-load transaction and recovery UI methods.

The full methods are compiled against counted services. This tests exception and
UI routing, not renderer behavior, file decoding or game object destruction.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
enum { ERP_DROP=1, ERP_FATAL, ERP_DISCONNECT, EDITOR_GUI=8, EDITOR_DEBUGGER=16,
       CMD_EXEC_NOW=0, MSG_OK=1, RETAINED_LOAD_ROWS=6 };
struct idException {
    char error[4096];
    explicit idException(const char* text) { std::snprintf(error,sizeof(error),"%s",text); }
};
struct Fatal : std::runtime_error { using std::runtime_error::runtime_error; };
struct idStr {
    std::string value;
    idStr(const char* text="") : value(text) {}
    void StripFileExtension() { auto n=value.rfind('.'); if(n!=std::string::npos)value.resize(n); }
    operator const char*() const { return value.c_str(); }
    static void vsnPrintf(char* out,size_t size,const char* fmt,va_list args) { std::vsnprintf(out,size,fmt,args); }
    static void snPrintf(char* out,size_t size,const char* fmt,...) { va_list a; va_start(a,fmt); std::vsnprintf(out,size,fmt,a); va_end(a); }
};
struct StringList {
    std::vector<std::string> values;
    int FindIndex(const idStr& value) const {
        auto it=std::find(values.begin(),values.end(),value.value);
        return it==values.end() ? -1 : int(it-values.begin());
    }
    void AddUnique(const char* text) { values.emplace_back(text); }
};
struct RestoreScope { const void* file=nullptr; int layoutBuild=0; int version=0; } openQ4_saveGameRestore;
struct CVars { int copy=0; bool fullscreen=false;
    int GetCVarInteger(const char*) { return copy; } bool GetCVarBool(const char*) { return fullscreen; }
} cvars, *cvarSystem=&cvars;
struct Renderer { bool running=true; bool IsOpenGLRunning() { return running; } } renderer,*renderSystem=&renderer;
struct Commands { int restarts=0; void BufferCommandText(int,const char*) { ++restarts; } } commands,*cmdSystem=&commands;
struct Arena { bool cleanup=false; int aborts=0; int recorded=0; bool NeedsCleanup() { return cleanup; } void RecordDecidedResult() { ++recorded; } void AbortMatch() { assert(recorded==aborts+1); ++aborts; } } arenaCampaign;
bool com_refreshOnPrint=true;
// Error() queues and unwinds only off the main thread; these runs are on it
#define MAX_PRINT_MSG_SIZE 4096
static bool Common_OnMainThread() { return true; }
static bool Common_QueueOffMainThread(bool,const char*) { return false; }
int com_errorEntered=0,com_editors=0,ticks=1000,clipboardWrites=0,flushes=0,shutdowns=0;
bool liveRestore=false,restoredObjectsUnwound=false;
std::vector<std::string> events;
int Sys_Milliseconds() { return ticks; }
void Sys_SetClipboardData(const char*) { ++clipboardWrites; }
void Sys_Printf(const char*,...) {}
void Com_FlushBufferedOutput() { ++flushes; }
[[noreturn]] void Sys_Error(const char*,...) { throw Fatal("fatal service"); }
struct GUI {};
class idCommonLocal {
public:
    char errorMessage[4096]="previous error";
    StringList errorList;
    int warnings=0;
    std::string warning;
    void Error(const char*,...);
    void Printf(const char*,...) {}
    void Shutdown() { ++shutdowns; }
    const char* GetLocalizedString(const char* id) { return id; }
    void Warning(const char* fmt,...) {
        char text[8192]; va_list args; va_start(args,fmt);
        std::vsnprintf(text,sizeof(text),fmt,args); va_end(args);
        warning=text; ++warnings; events.push_back("warning");
    }
} commonLocal,*common=&commonLocal;
class idSessionLocal {
public:
    enum loadGameFailure_t { LOADGAME_FAILURE_NONE, LOADGAME_FAILURE_UNREADABLE,
        LOADGAME_FAILURE_DAMAGED, LOADGAME_FAILURE_MAP, LOADGAME_FAILURE_PLATFORM, LOADGAME_FAILURE_RESTORE };
    loadGameFailure_t loadGameFailure=LOADGAME_FAILURE_NONE;
    GUI home,page;
    GUI* guiRetainedHome=nullptr;
    GUI* retainedPagePending=nullptr;
    StringList retainedLoadSlots;
    int retainedLoadSelected=-1,retainedLoadFirst=0;
    int stops=0,menus=0,rows=0,notices=0,dialogs=0;
    bool shouldFail=false,beginSucceeds=true,pendingExists=true,retainedEnabled=true;
    std::string notice,title;
    bool ExecuteSaveGameMapChange(const char*);
    void ShowSaveRestoreFailure(const char*);
    void ExecuteMapChange() {
        struct Owner {
            Owner() { liveRestore=true; restoredObjectsUnwound=false; }
            ~Owner() { liveRestore=false; restoredObjectsUnwound=true; events.push_back("unwind"); }
        } owner;
        if(shouldFail)common->Error("bad saved object %d",17);
    }
    void Stop() {
        if(liveRestore)throw std::logic_error("session stopped before restore unwound");
        if(restoredObjectsUnwound && shouldFail && openQ4_saveGameRestore.file)
            throw std::logic_error("restore error policy stayed armed during session teardown");
        ++stops; events.push_back("stop");
    }
    void StartMenu() {
        assert(!liveRestore); ++menus; events.push_back("menu");
    }
    void UpdateRetainedHome() { guiRetainedHome=retainedEnabled ? &home : nullptr; events.push_back("home"); }
    bool BeginRetainedPage(const char* name) {
        assert(std::string(name)=="loadGame" && guiRetainedHome);
        retainedPagePending=pendingExists ? &page : nullptr;
        return beginSucceeds;
    }
    void ScrubSaveGameFileName(idStr&) {} // filename handling has its own contracts
    void PublishRetainedLoadRows(GUI* gui) { assert(gui==&page); ++rows; }
    void PublishRetainedLoadSave(GUI* gui,const char* text) { assert(gui==&page); ++notices; notice=text; }
    void MessageBox(int kind,const char* text,const char* heading,bool wait) {
        assert(kind==MSG_OK && !wait); ++dialogs; notice=text; title=heading;
    }
} sessionLocal,*session=&sessionLocal;
'''

MAIN = r'''
void reset() {
    commonLocal=idCommonLocal(); sessionLocal=idSessionLocal();
    openQ4_saveGameRestore={}; cvars=CVars(); renderer=Renderer(); renderSystem=&renderer;
    arenaCampaign=Arena(); commands=Commands();
    com_errorEntered=0; com_editors=0; com_refreshOnPrint=true;
    clipboardWrites=flushes=shutdowns=0; ticks+=1000;
    liveRestore=restoredObjectsUnwound=false; events.clear();
}
void arm() { openQ4_saveGameRestore.file=&sessionLocal; openQ4_saveGameRestore.layoutBuild=1687; openQ4_saveGameRestore.version=5; }
void transactions() {
    reset(); arm();
    assert(session->ExecuteSaveGameMapChange("Good"));
    assert(session->stops==0 && session->menus==0 && common->warnings==0);
    assert(openQ4_saveGameRestore.file && clipboardWrites==0);
    reset(); arm(); session->shouldFail=true;
    assert(!session->ExecuteSaveGameMapChange("Damaged"));
    assert(restoredObjectsUnwound && !openQ4_saveGameRestore.file && openQ4_saveGameRestore.layoutBuild==0 &&
        openQ4_saveGameRestore.version==0);
    assert((events==std::vector<std::string>{"unwind","warning","stop","menu","home"}));
    assert(session->stops==1 && session->loadGameFailure==idSessionLocal::LOADGAME_FAILURE_RESTORE);
    assert(common->warnings==1 && common->warning.find("bad saved object 17")!=std::string::npos);
    assert(session->notices==1 && session->notice=="#str_231190" && clipboardWrites==0);
    session->shouldFail=false; arm(); assert(session->ExecuteSaveGameMapChange("Good"));
    assert(session->stops==1 && session->menus==1);
    // Handled restore failures must not build up Common's fatal error storm.
    reset(); session->shouldFail=true;
    for(int n=0;n<1000;++n) { arm(); assert(!session->ExecuteSaveGameMapChange("Damaged")); }
    assert(session->stops==1000 && clipboardWrites==0 && com_errorEntered==0 && shutdowns==0);
}
void errorPolicy() {
    reset(); arm();
    std::string longMessage(12000,'x');
    try { common->Error("%s",longMessage.c_str()); assert(false); }
    catch(const idException& error) { assert(std::strlen(error.error)==sizeof(error.error)-1); }
    assert(clipboardWrites==0 && session->stops==0 && !com_refreshOnPrint);
    assert(common->errorList.values.empty() && std::string(common->errorMessage)=="previous error");
    // Ordinary errors retain the existing drop, shutdown and clipboard policy.
    reset(); arenaCampaign.cleanup=true;
    try { common->Error("ordinary %d",23); assert(false); }
    catch(const idException& error) { assert(std::string(error.error)=="ordinary 23"); }
    assert(clipboardWrites==1 && session->stops==1 && arenaCampaign.recorded==1 && arenaCampaign.aborts==1 && com_errorEntered==0);
    // A live save context cannot downgrade startup, automated-copy or recursive errors.
    for(int cause=0;cause<5;++cause) {
        reset(); arm();
        if(cause==0)renderer.running=false;
        if(cause==1)renderSystem=nullptr;
        if(cause==2)cvars.copy=1;
        if(cause==3)com_errorEntered=ERP_DROP;
        if(cause==4)com_errorEntered=ERP_FATAL;
        bool fatal=false;
        try { common->Error("fatal test"); } catch(const Fatal&) { fatal=true; }
        assert(fatal && flushes>0);
    }
}
void uiRouting() {
    for(int path=0;path<4;++path) {
        reset();
        if(path==1)session->retainedEnabled=false;
        if(path==2)session->beginSucceeds=false;
        if(path==3)session->pendingExists=false;
        for(int i=0;i<24;++i)session->retainedLoadSlots.values.push_back("Slot"+std::to_string(i));
        session->ShowSaveRestoreFailure("Slot19.save");
        assert(session->menus==1 && session->notice=="#str_231190");
        if(path==0) {
            assert(session->notices==1 && session->dialogs==0 && session->rows==1);
            assert(session->retainedLoadSelected==19 && session->retainedLoadFirst==18);
        } else {
            assert(session->dialogs==1 && session->notices==0 && session->title=="#str_231191");
        }
    }
    reset(); session->ShowSaveRestoreFailure("Absent");
    assert(session->retainedLoadSelected==-1 && session->rows==0 && session->notices==1);
}
int main() {
    try { transactions(); errorPolicy(); uiRouting(); }
    catch(const std::exception& error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
    std::puts("save recovery: owned unwind before stop, repeated failure/reload, bounded errors, fatal policy, retained/classic routing passed");
}
'''


def source(mutate=False):
    common = (ROOT/'src/framework/Common.cpp').read_text(encoding='utf-8')
    session = (ROOT/'src/framework/Session.cpp').read_text(encoding='utf-8')
    menu = (ROOT/'src/framework/Session_menu.cpp').read_text(encoding='utf-8')
    error = function_body(common, 'void idCommonLocal::Error(')
    if mutate:
        before = 'code == ERP_DROP && openQ4_saveGameRestore.file != NULL'
        assert before in error
        error = error.replace(before, 'false', 1)
    return '\n'.join([SUPPORT, error,
        function_body(session, 'bool idSessionLocal::ExecuteSaveGameMapChange('),
        function_body(menu, 'static const char *Session_LoadFailureText('),
        function_body(menu, 'void idSessionLocal::ShowSaveRestoreFailure('), MAIN])


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--generate-native':
        Path(sys.argv[2]).write_text(source(), encoding='utf-8')
        return 0
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    mutate = '--mutation-check' in sys.argv
    with tempfile.TemporaryDirectory(prefix='save-recovery-', dir=ROOT/'.tmp') as directory:
        cpp = Path(directory)/'recovery.cpp'
        executable = cpp.with_suffix('.exe')
        cpp.write_text(source(mutate), encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        print(result.stdout + result.stderr, end='')
        if mutate:
            assert result.returncode != 0 and 'session stopped before restore unwound' in result.stderr
            print('Removing the scoped unwind policy reproduced premature session teardown.')
            return 0
        return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
