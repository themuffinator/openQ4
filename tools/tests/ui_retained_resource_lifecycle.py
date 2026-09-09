#!/usr/bin/env python3
"""Exercise the production retained-view coordinator without a renderer/game.

Runtime and host resource boundaries record calls. Registry, refresh, close,
shutdown and root-viewport routing functions are extracted without rewriting.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
struct Event { std::string kind; int id; double time; };
static std::vector<Event> events;
static int nextId=0;
static double PresentationTime();
static void Record(const char* kind,int id=0,double time=-1) { events.push_back({kind,id,time}); }
namespace openq4::ui {
struct Viewport { int width=0,height=0; float displayScale=1,userScale=1,pixelDensityX=1,pixelDensityY=1,originX=0,originY=0; };
struct Diagnostic { std::string pointer,message; int line=0,column=0; };
struct Input {};
struct ControlAction {};
struct RuntimeStatistics {};
class Runtime;
}
static std::vector<openq4::ui::Runtime*> runtimes;
class EngineHost {
public:
    int viewportWidth=640,viewportHeight=480,resets=0;
    std::uint64_t frame=1;
    std::uint64_t RenderFrame() const { return frame; }
    void Reset();
};
namespace openq4::ui {
class Runtime {
public:
    EngineHost& host;
    const int id=++nextId;
    bool loaded=false,failSave=false,failLoad=false,failRestore=false;
    int state=0,releases=0;
    Runtime(EngineHost& owner):host(owner) { runtimes.push_back(this); }
    ~Runtime() { Shutdown(); runtimes.erase(std::find(runtimes.begin(),runtimes.end(),this)); Record("destroy",id); }
    bool IsLoaded() const { return loaded; }
    bool LoadDocument(const std::string&,const std::string&,std::vector<Diagnostic>&) {
        Record("load",id,PresentationTime()); loaded=!failLoad; state=0; return loaded;
    }
    bool LoadMarkup(const std::string& source,const std::string& path) {
        std::vector<Diagnostic> diagnostics; return LoadDocument(source,path,diagnostics);
    }
    bool SaveSnapshot(std::string& snapshot,std::string& error,double now) {
        assert(loaded); Record("save",id,now);
        if(failSave) { error="save failure"; return false; }
        snapshot=std::to_string(state); return true;
    }
    bool RestoreSnapshot(const std::string& snapshot,std::string& error,double now) {
        assert(loaded); Record("restore",id,now);
        if(failRestore) { error="restore failure"; return false; }
        state=std::stoi(snapshot); return true;
    }
    void ReleaseInputSources() { assert(loaded); ++releases; Record("release",id); }
    void Shutdown() { loaded=false; state=-1; Record("shutdown",id); }
    void SetReducedMotion(bool,double) { assert(loaded); }
    void Frame(const Viewport& viewport,double now) {
        assert(loaded && viewport.width==host.viewportWidth && viewport.height==host.viewportHeight);
        Record("frame",id,now);
    }
};
}
void EngineHost::Reset() {
    for(auto* runtime:runtimes) assert(!runtime->IsLoaded());
    ++resets; Record("reset");
}
struct Renderer {
    int generation=0;
    bool running=true,uiViewport=false;
    int GetVideoRestartCount() const { return generation; }
    bool IsOpenGLRunning() const { return running; }
    bool GetUseUIViewportFor2D() const { return uiViewport; }
    void SetUseUIViewportFor2D(bool value) { uiViewport=value; }
    void FlushGui() { Record("flush"); }
    void BindRenderTexture(void*,void*) { Record("root-target"); }
    void SetColor4(float,float,float,float) {}
} renderer, *renderSystem=&renderer;
struct Common {
    void Warning(const char*,...) {}
    void Printf(const char*,...) {}
    void FatalError(const char* error) { throw std::runtime_error(error); }
} commonObject, *common=&commonObject;
struct Commands { void RemoveCommand(const char*) {} } commands, *cmdSystem=&commands;
struct WindowState {
    int uiViewportWidth=1920,uiViewportHeight=1080,uiViewportX=80,uiViewportY=40;
    float displayScale=1.25f,pixelDensityX=2,pixelDensityY=2;
} engineWindowState;
struct CVar {
    float value;
    float GetFloat() const { return value; }
    bool GetBool() const { return value!=0; }
} ui_retainedDensity{0},ui_retainedScale{1.5f},ui_retainedReducedMotion{0};
static int codePageGeneration=1;
static int LangDict_GetCodePageGeneration() { return codePageGeneration; }
'''

HELPERS = r'''
void SetApplicationOpen(bool value) { applicationOpen.store(value); }
void CancelInput(bool=false,bool=true) { Record("cancel-preview"); }
bool WindowFocused() { return true; }
struct Owner {
    openq4::ui::Runtime* runtime=nullptr;
    int before=0,restored=0,failed=0;
};
void OwnerEvent(void* opaque,retainedUIViewEvent_t event) {
    auto& owner=*static_cast<Owner*>(opaque);
    if(event==retainedUIViewEvent_t::BeforeResourceReset) {
        assert(owner.runtime->IsLoaded()); ++owner.before; Record("before",owner.runtime->id);
    } else if(event==retainedUIViewEvent_t::Restored) {
        assert(owner.runtime->IsLoaded()); ++owner.restored; Record("restored",owner.runtime->id);
    } else {
        assert(!owner.runtime->IsLoaded()); ++owner.failed; Record("failed",owner.runtime->id);
    }
}
'''

MAIN = r'''
static size_t First(const char* kind) {
    for(size_t i=0;i<events.size();++i) if(events[i].kind==kind)return i;
    throw std::runtime_error(std::string("missing event ")+kind);
}
static size_t Last(const char* kind) {
    for(size_t i=events.size();i>0;--i) if(events[i-1].kind==kind)return i-1;
    throw std::runtime_error(std::string("missing event ")+kind);
}
static int Count(const char* kind) {
    return static_cast<int>(std::count_if(events.begin(),events.end(),[&](auto& event){return event.kind==kind;}));
}
static void CheckRefreshOrder(int saves,int restores) {
    assert(Count("save")==saves && Count("restore")==restores && Count("reset")==1);
    assert(Last("save")<First("before"));
    assert(Last("before")<First("shutdown"));
    assert(First("shutdown")<First("reset"));
    assert(First("reset")<First("load"));
    assert(Last("load")<First("restore"));
    double savedTime=-1,restoredTime=-1,lastLoadTime=-1;
    for(const auto& event:events) {
        if(event.kind=="save") {
            if(savedTime<0)savedTime=event.time;
            assert(event.time==savedTime);
        }
        if(event.kind=="load")lastLoadTime=(std::max)(lastLoadTime,event.time);
        if(event.kind=="restore") {
            if(restoredTime<0)restoredTime=event.time;
            assert(event.time==restoredTime && event.time>=lastLoadTime && event.time>=savedTime);
        }
    }
}
int main() {
    const auto initialEpoch=epoch;
    Owner left,right;
    auto* a=RetainedUI_CreateView(OwnerEvent,&left);
    auto* b=RetainedUI_CreateView(OwnerEvent,&right);
    previewView=RetainedUI_CreateView(PreviewResourceEvent,nullptr);
    runtime=RetainedUI_ViewRuntime(previewView);
    auto* previewRuntime=runtime;
    left.runtime=RetainedUI_ViewRuntime(a); right.runtime=RetainedUI_ViewRuntime(b);
    std::vector<openq4::ui::Diagnostic> diagnostics;
    assert(RetainedUI_LoadView(a,"document-a","a.q4ui",diagnostics));
    assert(RetainedUI_LoadView(b,"document-b","b.q4ui",diagnostics));
    assert(RetainedUI_LoadView(previewView,"preview","preview.q4ui",diagnostics));
    left.runtime->state=11; right.runtime->state=22; runtime->state=33;
    SetApplicationOpen(true);
    auto* empty=RetainedUI_CreateView(nullptr,nullptr);
    assert(epoch==initialEpoch && !RetainedUI_PrepareView(empty));
    RetainedUI_DestroyView(empty);
    const int resetsBefore=host.resets;
    events.clear(); ++renderer.generation;
    assert(RetainedUI_PrepareView(a));
    CheckRefreshOrder(3,3);
    assert(host.resets==resetsBefore+1 && left.runtime->state==11 && right.runtime->state==22 && runtime->state==33);
    assert(left.runtime==RetainedUI_ViewRuntime(a) && right.runtime==RetainedUI_ViewRuntime(b) && runtime==previewRuntime);
    assert(left.before==1 && right.before==1 && left.restored==1 && right.restored==1);
    assert(left.runtime->releases==1 && right.runtime->releases==1 && runtime->releases==1);
    assert(applicationOpen.load());

    // Resource failures leave only that view unloaded. Restore and compile
    // failures exercise the same peer-independent path as snapshot failure.
    for(int failure=0;failure<3;++failure) {
        left.runtime->failSave=failure==0;
        left.runtime->failLoad=failure==1;
        left.runtime->failRestore=failure==2;
        events.clear(); ++languageRevision;
        assert(!RetainedUI_PrepareView(a));
        CheckRefreshOrder(3,failure==2?3:2);
        assert(a->failed && !left.runtime->IsLoaded() && left.failed==failure+1);
        assert(RetainedUI_PrepareView(b) && RetainedUI_PrepareView(previewView));
        assert(right.runtime->state==22 && runtime->state==33);
        assert(left.runtime==RetainedUI_ViewRuntime(a));
        left.runtime->failSave=left.runtime->failLoad=left.runtime->failRestore=false;
        assert(RetainedUI_LoadView(a,"document-a","a.q4ui",diagnostics));
        left.runtime->state=11;
    }
    openq4::ui::Viewport viewport;
    assert(RetainedUI_DefaultViewport(viewport));
    assert(viewport.width==1920 && viewport.originX==80 && viewport.displayScale==1.25f && viewport.userScale==1.5f);
    assert(viewport.pixelDensityX==2);
    ui_retainedDensity.value=2.5f;
    assert(RetainedUI_DefaultViewport(viewport) && viewport.displayScale==2.5f);
    host.viewportWidth=777; host.viewportHeight=333;
    renderer.uiViewport=false;
    assert(RetainedUI_DrawViewRoot(a,viewport));
    viewport.width=800; viewport.height=600;
    assert(RetainedUI_DrawViewRoot(b,viewport));
    assert(host.viewportWidth==777 && host.viewportHeight==333 && !renderer.uiViewport);

    // A dirty resource generation cannot invalidate geometry already queued
    // for this front-end frame. Unchanged-generation peers still prepare.
    assert(rootSubmissionPending && RetainedUI_PrepareView(a));
    events.clear(); ++languageRevision;
    const int beforeDeferredReset=host.resets;
    assert(!RetainedUI_PrepareView(b));
    assert(events.empty() && host.resets==beforeDeferredReset);
    assert(left.runtime->IsLoaded() && right.runtime->IsLoaded() && runtime->IsLoaded());
    RetainedUI_FrameSubmitted();
    assert(RetainedUI_PrepareView(b)); CheckRefreshOrder(3,3);
    assert(host.resets==beforeDeferredReset+1);
    assert(RetainedUI_DrawViewRoot(a,viewport));
    events.clear(); ++languageRevision; ++host.frame;
    assert(RetainedUI_PrepareView(b)); CheckRefreshOrder(3,3);
    assert(!rootSubmissionPending);

    const int beforeClose=host.resets;
    Close();
    assert(views.size()==2 && runtime==nullptr && previewView==nullptr && !applicationOpen.load());
    assert(host.resets==beforeClose && left.runtime->state==11 && right.runtime->state==22);
    assert(RetainedUI_PrepareView(a) && RetainedUI_PrepareView(b));
    events.clear();
    RetainedUI_Shutdown();
    assert(Last("shutdown")<First("reset") && Last("before")<First("shutdown"));
    assert(Last("reset")<First("failed"));
    assert(!left.runtime->IsLoaded() && !right.runtime->IsLoaded() && a->failed && b->failed);
    assert(RetainedUI_ViewRuntime(a)==left.runtime && RetainedUI_ViewRuntime(b)==right.runtime);
    RetainedUI_DestroyView(a); RetainedUI_DestroyView(b);
    RetainedUI_DestroyView(nullptr);
    assert(views.empty() && runtimes.empty() && epoch==initialEpoch);
    std::puts("Retained view lifecycle: shared resource barriers/clocks, isolated failures, stable owners, preview close and viewport restoration passed");
}
'''


def main():
    source = (ROOT / 'src/ui/RetainedUI.cpp').read_text(encoding='utf-8')
    header = (ROOT / 'src/ui/RetainedUI.h').read_text(encoding='utf-8')
    view = function_body(source, 'struct retainedUIView_t {') + ';\n'
    globals_source = source[source.index('EngineHost host;'):source.index('void RecordProfile(')]
    signatures = (
        'double PresentationTime()',
        'bool RegisteredView(',
        'bool LoadViewDocument(',
        'void ReportViewDiagnostics(',
        'void PreviewResourceEvent(',
        'void Close()',
        'bool RefreshResources()',
        'retainedUIView_t* RetainedUI_CreateView(',
        'void RetainedUI_DestroyView(',
        'openq4::ui::Runtime* RetainedUI_ViewRuntime(',
        'bool RetainedUI_LoadView(',
        'bool RetainedUI_PrepareView(',
        'bool RetainedUI_DefaultViewport(',
        'double RetainedUI_PresentationTime()',
        'void RetainedUI_FrameSubmitted()',
        'bool RetainedUI_DrawViewRoot(',
        'void RetainedUI_Shutdown()',
    )
    code = SUPPORT + header.replace('#pragma once', '') + view + globals_source + HELPERS
    code += '\n'.join(function_body(source, signature) for signature in signatures) + MAIN
    compiler = next((found for name in ('clang++', 'g++', 'c++') if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='retained-views-', dir=ROOT / '.tmp') as temp:
        test_source = Path(temp) / 'views.cpp'
        binary = Path(temp) / 'views.exe'
        test_source.write_text(code, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', str(test_source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
