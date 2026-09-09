#!/usr/bin/env python3
"""Exercise production GUI-manager ownership without renderer, input or a game.

The managed declaration and manager method bodies are compiled unchanged against
counted GUI/resource stand-ins. This covers lifecycle and cache behavior only;
it does not establish retained routing, document rendering or demo replay.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <functional>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
using ID_TIME_T = long long;
struct idStr : std::string {
    using std::string::string;
    using std::string::operator=;
    operator const char*() const { return c_str(); }
    static int Icmp(const char* a, const char* b) {
        while (*a && *b) {
            const int delta = std::tolower(static_cast<unsigned char>(*a++)) -
                              std::tolower(static_cast<unsigned char>(*b++));
            if (delta) return delta;
        }
        return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
    }
};
template<class T> struct idList {
    std::vector<T> values;
    int Num() const { return static_cast<int>(values.size()); }
    T& operator[](int i) { return values.at(i); }
    const T& operator[](int i) const { return values.at(i); }
    void Append(const T& value) { values.push_back(value); }
    T* Find(const T& value) {
        auto it=std::find(values.begin(),values.end(),value);
        return it==values.end() ? nullptr : &*it;
    }
    void AddUnique(const T& value) { if (!Find(value)) Append(value); }
    void Remove(const T& value) {
        auto it=std::find(values.begin(),values.end(),value);
        if(it!=values.end()) values.erase(it);
    }
};
struct idUserInterface {
    virtual ~idUserInterface() = default;
    virtual const char* Name() const = 0;
    virtual bool InitFromFile(const char*,bool=true,bool=true) = 0;
    virtual bool IsInteractive() const = 0;
    virtual bool IsUniqued() const = 0;
    virtual void SetUniqued(bool) = 0;
};
'''

MANAGER = r'''
class idUserInterfaceManagerLocal {
public:
    void Shutdown();
    void Touch(const char*);
    void BeginLevelLoad();
    void EndLevelLoad();
    void Reload(bool);
    void ListGuis() const;
    idUserInterface* Alloc() const;
    void DeAlloc(idUserInterface*);
    idUserInterface* FindGui(const char*,bool=false,bool=false,bool=false);
    idUserInterface* FindDemoGui(const char*);
    void RegisterAllocation(idUserInterfaceManaged*);
    void RegisterGui(idUserInterfaceManaged*);
    void RegisterDemoGui(idUserInterfaceManaged*);
    void UnregisterGui(idUserInterfaceManaged*);
    void UpdateAlwaysThinkGui(idUserInterfaceManaged*);
    void RemoveAlwaysThinkGui(idUserInterfaceManaged*);
    void RunAlwaysThinkGUIs(int);
    idList<idUserInterfaceManaged*> allocations,guis,alwaysThinkGUIs,demoGuis;
    unsigned long long nextAllocationId=0;
    struct Context { void SizeIcons() {} void Shutdown() {} } dc;
} uiManagerLocal;
static int destroyed=0,retainedShutdowns=0;
static bool recycleAllocation=false;
static void* recycledAllocation=nullptr;
class idUserInterfaceLocal : public idUserInterfaceManaged {
public:
    idStr source;
    ID_TIME_T timestamp=1;
    bool menu=false,think=false,interactive=false,unique=false;
    int ticks=0,loads=0,lastTime=-1;
    std::function<void()> onThink,onLoad;
    ~idUserInterfaceLocal() override { ++destroyed; }
    static void* operator new(size_t bytes) {
        if(recycledAllocation) {
            void* result=recycledAllocation; recycledAllocation=nullptr; return result;
        }
        return ::operator new(bytes);
    }
    static void operator delete(void* memory) {
        if(recycleAllocation && !recycledAllocation) recycledAllocation=memory;
        else ::operator delete(memory);
    }
    const char* Name() const override { return source.c_str(); }
    const char* GetSourceFile() const override { return source.c_str(); }
    ID_TIME_T GetTimeStamp() const override { return timestamp; }
    bool IsMenuGui() const override { return menu; }
    bool AlwaysThink() const override { return think; }
    bool IsInteractive() const override { return interactive; }
    bool IsUniqued() const override { return unique; }
    void SetUniqued(bool value) override { unique=value; }
    size_t Size() override { return sizeof(*this); }
    int NumTransitions() override { return 3; }
    void RunTimeEvents(int time) override {
        ++ticks; lastTime=time; auto callback=onThink; if(callback) callback();
    }
    bool InitFromFile(const char* path,bool=true,bool=true) override {
        if(!path || !*path || !idStr::Icmp(path,"missing.gui")) return false;
        // A production reload must not pass a pointer owned by this instance.
        assert(path!=source.c_str());
        source=path; ++loads; RegisterLoaded();
        auto callback=onLoad; if(callback) callback();
        return true;
    }
    void PublishDemo() { RegisterDemo(); }
    void ChangeThinking(bool value) { think=value; RefreshThinking(); }
};
struct Commands { void RemoveCommand(const char*) {} } commands;
Commands* cmdSystem=&commands;
struct idChatWindow { static void Reset() {} };
void RetainedUI_Shutdown() {
    assert(uiManagerLocal.allocations.Num()==0);
    ++retainedShutdowns;
}
struct Common {
    void Printf(const char*,...) {}
    void Error(const char*,...) { throw std::runtime_error("missing save GUI"); }
} commonObject;
Common* common=&commonObject;
struct Session { bool saving=false; bool IsLoadingSaveGame() const {return saving;} } sessionObject;
Session* session=&sessionObject;
struct FileSystem {
    ID_TIME_T timestamp=1;
    int ReadFile(const char*,void*,ID_TIME_T* stamp) { *stamp=timestamp; return 0; }
} files;
FileSystem* fileSystem=&files;
struct idMaterial {
    idUserInterface* gui=nullptr;
    idUserInterface* GlobalGui() const { return gui; }
};
constexpr int DECL_MATERIAL=0;
struct DeclManager {
    std::vector<idMaterial> materials;
    int GetNumDecls(int) const { return static_cast<int>(materials.size()); }
    const idMaterial* DeclByIndex(int,int index,bool) const { return &materials.at(index); }
} declarations;
DeclManager* declManager=&declarations;
'''

MAIN = r'''
static void Empty() {
    assert(uiManagerLocal.allocations.Num()==0 && uiManagerLocal.guis.Num()==0 &&
           uiManagerLocal.alwaysThinkGUIs.Num()==0 && uiManagerLocal.demoGuis.Num()==0);
}
static idUserInterfaceLocal* Load(const char* name) {
    return static_cast<idUserInterfaceLocal*>(uiManagerLocal.FindGui(name,true));
}
int main() {
    auto& manager=uiManagerLocal;
    // Allocations are owned before loading and remain safe through level scans.
    auto blank=manager.Alloc();
    assert(manager.allocations.Num()==1 && manager.guis.Num()==0);
    manager.BeginLevelLoad(); manager.EndLevelLoad(); manager.ListGuis();
    assert(manager.allocations.Num()==1);
    manager.DeAlloc(blank); Empty();
    const int afterBlank=destroyed;
    manager.DeAlloc(blank); Empty();
    assert(destroyed==afterBlank);
    manager.DeAlloc(nullptr);
    assert(manager.FindGui(nullptr,true)==nullptr && manager.FindGui("",true)==nullptr);
    assert(manager.FindDemoGui(nullptr)==nullptr && manager.FindDemoGui("")==nullptr);
    const int beforeFailure=destroyed;
    manager.Touch("missing.gui");
    assert(manager.FindGui("missing.gui",true)==nullptr);
    sessionObject.saving=true;
    bool rejected=false;
    try { manager.FindGui("missing.gui",true); } catch(const std::runtime_error&) { rejected=true; }
    sessionObject.saving=false;
    assert(rejected && destroyed==beforeFailure+3); Empty();

    // Direct legacy/editor deletion removes all non-owning registries once.
    auto editor=new idUserInterfaceLocal;
    editor->think=true;
    assert(editor->InitFromFile("guis/temp.guied"));
    editor->PublishDemo(); editor->PublishDemo();
    assert(manager.guis.Num()==1 && manager.demoGuis.Num()==1 && manager.alwaysThinkGUIs.Num()==1);
    assert(manager.FindDemoGui("GUIS/TEMP.GUIED")==editor);
    delete editor; Empty();
    auto reopened=manager.FindGui("guis/temp.guied",true,true);
    assert(dynamic_cast<idUserInterfaceLocal*>(reopened)!=nullptr);
    delete reopened; Empty();

    // Shared, interactive and explicitly unique caches preserve retail rules.
    auto shared=Load("shared.gui");
    assert(shared->GetRefs()==1 && manager.FindGui("SHARED.GUI")==shared && shared->GetRefs()==2);
    shared->interactive=true;
    auto interactiveCopy=Load("shared.gui");
    assert(interactiveCopy!=shared);
    assert(manager.FindGui("shared.gui",true,false,true)==shared);
    auto unique=static_cast<idUserInterfaceLocal*>(manager.FindGui("unique.gui",true,true));
    assert(unique->IsUniqued() && !unique->IsInteractive());
    assert(manager.FindGui("unique.gui",true)!=unique);
    manager.Shutdown(); Empty();

    // Menu refs persist; touched world views and material-held views survive.
    auto menu=Load("menu.gui"); menu->menu=true;
    auto orphan=Load("orphan.gui"); orphan->ChangeThinking(true);
    auto referenced=Load("referenced.gui");
    auto material=Load("material.gui");
    declarations.materials.push_back({material});
    manager.BeginLevelLoad();
    assert(menu->GetRefs()==1 && material->GetRefs()==0 && referenced->GetRefs()==0);
    assert(manager.FindGui("referenced.gui")==referenced);
    const int beforePurge=destroyed;
    manager.EndLevelLoad();
    assert(destroyed==beforePurge+1 && manager.FindGui("orphan.gui")==nullptr);
    assert(manager.alwaysThinkGUIs.Num()==0 && manager.guis.Num()==3);
    declarations.materials.clear(); manager.Shutdown(); Empty();

    // A thinker can delete a later view, including reusing its exact address.
    auto first=Load("first.gui"); first->ChangeThinking(true);
    auto victim=Load("victim.gui"); victim->ChangeThinking(true);
    idUserInterfaceLocal* replacement=nullptr;
    first->onThink=[&] {
        if(replacement) return;
        recycleAllocation=true;
        delete victim;
        replacement=new idUserInterfaceLocal;
        assert(replacement==victim); // Deliberately exercise address reuse.
        recycleAllocation=false;
        replacement->think=true;
        replacement->InitFromFile("replacement.gui");
    };
    manager.RunAlwaysThinkGUIs(10);
    assert(first->ticks==1 && replacement && replacement->ticks==0);
    manager.RunAlwaysThinkGUIs(20);
    assert(first->ticks==2 && replacement->ticks==1 && replacement->lastTime==20);
    first->ChangeThinking(false);
    manager.RunAlwaysThinkGUIs(30);
    assert(first->ticks==2 && replacement->ticks==2);
    auto self=Load("self-delete.gui"); self->ChangeThinking(true);
    bool selfCalled=false;
    self->onThink=[&] { selfCalled=true; delete self; };
    const int beforeSelf=destroyed;
    manager.RunAlwaysThinkGUIs(40);
    assert(selfCalled && destroyed==beforeSelf+1 && replacement->ticks==3);
    assert(manager.FindGui("self-delete.gui")==nullptr);
    manager.Shutdown(); Empty();

    // Reload retains path ownership, skips unchanged files and tolerates a
    // callback removing another entry without reloading its replacement.
    first=Load("reload-first.gui"); victim=Load("reload-victim.gui"); replacement=nullptr;
    files.timestamp=1; manager.Reload(false);
    assert(first->loads==1 && victim->loads==1);
    first->onLoad=[&] {
        if(replacement) return;
        recycleAllocation=true; delete victim;
        replacement=new idUserInterfaceLocal;
        assert(replacement==victim); recycleAllocation=false;
        replacement->InitFromFile("reload-replacement.gui");
    };
    files.timestamp=2; manager.Reload(false);
    assert(first->loads==2 && replacement && replacement->loads==1);
    manager.Reload(true);
    assert(first->loads==3 && replacement->loads==2);
    manager.Shutdown(); Empty();

    // One object can occur in loaded/demo subsets; a blank demo allocation is
    // still owned. Shutdown destroys both exactly once and is repeatable.
    auto dual=Load("dual.gui"); dual->PublishDemo(); dual->ChangeThinking(true);
    manager.Alloc();
    const int beforeShutdown=destroyed;
    manager.Shutdown();
    assert(destroyed==beforeShutdown+2); Empty();
    manager.Shutdown(); Empty();
    assert(recycledAllocation==nullptr && retainedShutdowns==6);
    std::puts("GUI manager lifecycle: ownership, direct deletion, refs, uniqueness, material retention, reload and callback address reuse passed");
}
'''


def main():
    source = (ROOT / 'src/ui/UserInterface.cpp').read_text(encoding='utf-8')
    header = (ROOT / 'src/ui/UserInterfaceManaged.h').read_text(encoding='utf-8')
    managed = function_body(header, 'class idUserInterfaceManaged :') + ';\n'
    signatures = (
        'idUserInterfaceManaged::idUserInterfaceManaged()',
        'idUserInterfaceManaged::~idUserInterfaceManaged()',
        'void idUserInterfaceManaged::RegisterLoaded()',
        'void idUserInterfaceManaged::RegisterDemo()',
        'void idUserInterfaceManaged::RefreshThinking()',
        'void idUserInterfaceManagerLocal::Shutdown()',
        'void idUserInterfaceManagerLocal::Touch(',
        'void idUserInterfaceManagerLocal::BeginLevelLoad()',
        'void idUserInterfaceManagerLocal::EndLevelLoad()',
        'void idUserInterfaceManagerLocal::Reload(',
        'void idUserInterfaceManagerLocal::ListGuis()',
        'idUserInterface *idUserInterfaceManagerLocal::Alloc(',
        'void idUserInterfaceManagerLocal::DeAlloc(',
        'idUserInterface *idUserInterfaceManagerLocal::FindGui(',
        'idUserInterface *idUserInterfaceManagerLocal::FindDemoGui(',
        'void idUserInterfaceManagerLocal::RegisterAllocation(',
        'void idUserInterfaceManagerLocal::RegisterGui(',
        'void idUserInterfaceManagerLocal::RegisterDemoGui(',
        'void idUserInterfaceManagerLocal::UnregisterGui(',
        'void idUserInterfaceManagerLocal::UpdateAlwaysThinkGui(',
        'void idUserInterfaceManagerLocal::RemoveAlwaysThinkGui(',
        'void idUserInterfaceManagerLocal::RunAlwaysThinkGUIs(',
    )
    code = SUPPORT + managed + MANAGER + '\n'.join(function_body(source, signature) for signature in signatures) + MAIN
    compiler = next((found for name in ('clang++', 'g++', 'c++') if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-manager-', dir=ROOT / '.tmp') as temp:
        test_source = Path(temp) / 'manager.cpp'
        binary = Path(temp) / 'manager.exe'
        test_source.write_text(code, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', str(test_source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
