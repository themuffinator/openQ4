#!/usr/bin/env python3
"""Exercise the production stock-GUI source/media cache without a game window.

The VFS and declarations are stand-ins; the cache and scoped observers are
production code. Runtime map chains qualify the window walk and hand-offs.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <unordered_set>
#include <stdexcept>
#include <memory>
#include "src/idlib/ParserSourceObserver.h"
#include "src/framework/DeclReferenceObserver.h"
enum declType_t { DECL_MATERIAL, DECL_SOUND, DECL_TABLE };
constexpr float SS_GUI = 100;
constexpr float SS_POST_PROCESS = 200;
constexpr int MF_DEFAULTED = 1;
struct idStr : std::string {
    using std::string::string;
    void Clear() { clear(); }
    operator const char*() const { return c_str(); }
};
template<class T> struct idList : std::vector<T> {
    int Num() const { return static_cast<int>(this->size()); }
};
struct idDecl {
    declType_t type; std::string name; bool referenced = false;
    idDecl(declType_t type, const char* name):type(type),name(name) {}
    declType_t GetType() const { return type; }
    const char* GetName() const { return name.c_str(); }
};
struct idMaterial : idDecl {
    mutable float sort = SS_GUI;
    idMaterial(const char* name):idDecl(DECL_MATERIAL,name) {}
    float GetSort() const { return sort; }
    void SetSort(float value) const { sort = value; }
    void SetImageClassifications(int) const {}
    bool TestMaterialFlag(int) const { return false; }
    int GetImageWidth() const { return 128; }
    int GetImageHeight() const { return 16; }
};
struct VFS {
    std::map<std::string,std::string> files;
    int allocations = 0;
    int ReadFile(const char* path, void** out) {
        auto it=files.find(path);
        if(it==files.end()) { *out=nullptr; return -1; }
        char* data=new char[it->second.size()+1]; ++allocations;
        std::memcpy(data,it->second.c_str(),it->second.size()+1);
        *out=data; return static_cast<int>(it->second.size());
    }
    void FreeFile(void* data) { delete[] static_cast<char*>(data); --allocations; }
} vfs, *fileSystem=&vfs;
struct Decls {
    std::map<std::pair<declType_t,std::string>,idDecl*> values;
    int calls=0;
    const idDecl* FindType(declType_t type,const char* name) {
        auto* decl=values.at({type,name}); decl->referenced=true; ++calls; return decl;
    }
    const idMaterial* FindMaterial(const char* name) {
        return static_cast<const idMaterial*>(FindType(DECL_MATERIAL,name));
    }
    const idDecl* DeclByIndex(declType_t type,int,bool) { return values.at({type,"pulse"}); }
} decls, *declManager=&decls;
constexpr int WOP_TYPE_TABLE = 1;
struct idFile_Memory {
    std::string bytes;
    idFile_Memory(const char*,const char* data,int size):bytes(data,size) {}
    idFile_Memory(const char*,char*,int)=delete; // Mutable storage selects writing in production.
};
struct idSimpleWindow { const idMaterial* background = nullptr; };
struct idWindow {
    struct Op { int opType, a; };
    struct Draw { idSimpleWindow* simp; };
    const idMaterial* background = nullptr;
    const idMaterial* levelLoadDirectBackground = nullptr;
    idStr backGroundName;
    int page = 0;
    bool CanKeepForLevelLoad() const { return true; }
    bool Interactive() const { return true; }
    void ReadFromSaveGame(idFile_Memory* saved) { page=std::stoi(saved->bytes); }
    void CaptureForLevelLoad();
    int numOps = 0;
    Op ops[1]{};
    idList<Draw> drawWindows;
    idList<idWindow*> children;
    virtual void RefreshForLevelLoad();
};
struct idSliderWindow : idWindow {
    const idMaterial* thumbMat = nullptr;
    int thumbWidth = 0, thumbHeight = 0;
    void RefreshForLevelLoad() override;
};
'''
GUI = r'''
struct Common {
    int GetPresentationTime() { return 100; }
    void Printf(const char*,const char*) {}
} commonInstance, *common=&commonInstance;
struct idUserInterfaceLocal {
    idWindow* desktop=nullptr;
    std::unique_ptr<LegacyLevelLoadCache> levelLoadCache;
    bool initialized=true, interactive=false, active=false;
    void* lightColorVar=reinterpret_cast<void*>(1);
    void* bindHandler=reinterpret_cast<void*>(1);
    struct Dict : std::map<std::string,std::string> {
        void Set(const char* key,const char* value) { (*this)[key]=value; }
    } state;
    idStr pendingCmd, returnCmd, source;
    // The HUD presentations joined to the previous map's windows: a kept GUI
    // resets what it derived from them and has its peers join again.
    struct Peer { int rejoined=0; void Rejoin() { ++rejoined; } } peer, air;
    Peer *remastered=&peer, *essentialHud=&air;
    int hudResets=0;
    void ResetHudPresentation(bool sourceChanged) { assert(!sourceChanged); ++hudResets; }
    struct Evaluated { bool cleared=false; void Clear() { cleared=true; } } evaluatedHudState;
    void Activate(bool,int) {}
    bool KeepForLevelLoad();
};
'''
MAIN = r'''
int main() {
    LegacyLevelLoadCache cache, nested;
    int parser=0, anotherParser=0;
    idMaterial background("menu/background"), scripted("menu/hover");
    idDecl sound(DECL_SOUND,"menu/click"), table(DECL_TABLE,"pulse");
    for(auto* decl : std::vector<idDecl*>{&background,&scripted,&sound,&table})
        decls.values[{decl->GetType(),decl->GetName()}]=decl;
    {
        idParserSourceObserver sources(&parser,&cache,LegacyLevelLoadCache::SourceRead);
        idDeclReferenceObserver media(&cache,LegacyLevelLoadCache::Referenced);
        idParserSourceObserver::Notify(&anotherParser,"unrelated", "x",1);
        idParserSourceObserver::Notify(&parser,"guis/root.gui","root",4);
        idParserSourceObserver::Notify(&parser,"guis/preferred.inc",nullptr,-1);
        idParserSourceObserver::Notify(&parser,"shared.inc","include",7);
        idParserSourceObserver::Notify(&parser,"shared.inc","include",7);
        idDeclReferenceObserver::Notify(&background);
        try {
            idDeclReferenceObserver inner(&nested,LegacyLevelLoadCache::Referenced);
            idDeclReferenceObserver::Notify(&scripted);
            idDeclReferenceObserver::Notify(&sound);
            throw std::runtime_error("unwind");
        } catch(const std::runtime_error&) {}
        idDeclReferenceObserver::Notify(&table);
        idDeclReferenceObserver::Notify(&background);
    }
    idParserSourceObserver::Notify(&parser,"outside","x",1);
    idDeclReferenceObserver::Notify(&sound);
    assert(cache.sources.size()==3 && cache.resources.size()==4);
    assert(nested.resources.size()==2);
    cache.Finish(true);
    vfs.files={{"guis/root.gui","root"},{"shared.inc","include"}};
    assert(cache.Unchanged() && vfs.allocations==0);
    vfs.files["shared.inc"]="INCLUDE"; // same size, no timestamp involved
    assert(!cache.Unchanged() && vfs.allocations==0);
    vfs.files["shared.inc"]="include";
    vfs.files["guis/preferred.inc"]="new"; // include resolution changes
    assert(!cache.Unchanged() && vfs.allocations==0);
    vfs.files.erase("guis/preferred.inc");
    vfs.files.erase("guis/root.gui");
    assert(!cache.Unchanged() && vfs.allocations==0);
    vfs.files["guis/root.gui"]="root";
    assert(cache.Unchanged());
    // A purge changes contents, not declaration identity. Re-reference both
    // currently displayed and script-only assets before the next frame.
    background.sort=scripted.sort=0;
    cache.Reference();
    assert(decls.calls==4 && background.referenced && scripted.referenced);
    assert(sound.referenced && table.referenced);
    assert(background.sort==SS_GUI && scripted.sort==SS_GUI);
    // Real window traversal: direct scroller backgrounds have no authored
    // background name, simple children have separate storage, and expression
    // tables plus thumb dimensions must be restored inside the load.
    idWindow root;
    idSliderWindow scroller;
    idSimpleWindow simple;
    root.numOps=1; root.ops[0]={WOP_TYPE_TABLE,0};
    root.children.push_back(&scroller);
    root.drawWindows.push_back({&simple});
    scroller.background=&background; scroller.thumbMat=&scripted;
    simple.background=&scripted;
    background.referenced=scripted.referenced=table.referenced=false;
    background.sort=scripted.sort=0;
    root.CaptureForLevelLoad();
    scroller.background=nullptr; // Restoring an empty authored background name clears it.
    root.RefreshForLevelLoad();
    assert(scroller.background==&background && simple.background==&scripted);
    assert(background.referenced && scripted.referenced && table.referenced);
    assert(background.sort==SS_GUI && scripted.sort==SS_GUI);
    assert(scroller.thumbWidth==128 && scroller.thumbHeight==16);
    // Exercise the production keep path after a menu was left on Load Game.
    idUserInterfaceLocal gui;
    gui.desktop=&root;
    gui.levelLoadCache=std::make_unique<LegacyLevelLoadCache>();
    vfs.files["menu.gui"]="original";
    LegacyLevelLoadCache::SourceRead(gui.levelLoadCache.get(),"menu.gui","original",8);
    gui.levelLoadCache->Finish(true);
    gui.levelLoadCache->windowDefaults="0";
    root.page=2;
    gui.source="menu.gui";
    gui.state["text"]="stale";
    gui.state["gamePublished"]="keep me";
    gui.pendingCmd="loadGame stale";
    gui.returnCmd="old command";
    assert(gui.KeepForLevelLoad());
    assert(gui.state["text"]=="Test Text!" && gui.state["name"]=="menu.gui" && gui.state["gamePublished"]=="keep me");
    assert(root.page==0 && !gui.initialized && !gui.lightColorVar && !gui.bindHandler);
    assert(gui.pendingCmd.empty() && gui.returnCmd.empty() && gui.interactive);
    assert(gui.hudResets==1 && gui.peer.rejoined==1 && gui.air.rejoined==1 && gui.evaluatedHudState.cleared);
    vfs.files["menu.gui"]="changed!";
    root.page=2;
    assert(!gui.KeepForLevelLoad() && root.page==2);
    cache.Finish(false);
    assert(!cache.Unchanged());
    LegacyLevelLoadCache empty;
    empty.Finish(true);
    assert(!empty.Unchanged());
}
'''


def main():
    source = (ROOT / "src/ui/UserInterface.cpp").read_text(encoding="utf-8")
    cache = function_body(source, "struct LegacyLevelLoadCache {") + ";\n"
    window = (ROOT / "src/ui/Window.cpp").read_text(encoding="utf-8")
    slider = (ROOT / "src/ui/SliderWindow.cpp").read_text(encoding="utf-8")
    refresh = function_body(window, "void idWindow::CaptureForLevelLoad()")
    refresh += function_body(window, "void idWindow::RefreshForLevelLoad()")
    refresh += function_body(slider, "void idSliderWindow::RefreshForLevelLoad()")
    compiler = next((path for name in ("clang++", "g++", "c++")
                     if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("C++ compiler required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="stock-level-load-", dir=ROOT / ".tmp") as temp:
        cpp, binary = Path(temp) / "cache.cpp", Path(temp) / "cache.exe"
        keep = function_body(source, "bool idUserInterfaceLocal::KeepForLevelLoad()")
        cpp.write_text(SUPPORT + cache + refresh + GUI + keep + MAIN, encoding="utf-8")
        subprocess.run([compiler, "-std=c++20", "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True, cwd=ROOT)
        subprocess.run([str(binary)], check=True, cwd=ROOT)
    print("stock GUI source invalidation, scoped capture, authored-state reset, window/scroller traversal and purge references: PASS")


if __name__ == "__main__":
    main()
