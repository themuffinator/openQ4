#!/usr/bin/env python3
"""Execute production GUI presentation/IME queries and session consumers in memory.

Window doubles model register evaluation and failed edit-field geometry. The
production functions are extracted, not rewritten. No host input is accessed.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
struct idStr {
    std::string text;
    idStr()=default;
    idStr(const char* value):text(value){}
    idStr(std::string value):text(value){}
    const char* c_str() const {return text.c_str();}
    int Length() const {return static_cast<int>(text.size());}
    int Find(const char* needle) const {auto pos=text.find(needle);return pos==text.npos?-1:static_cast<int>(pos);}
    idStr Left(int count) const {return text.substr(0,count);}
    idStr Right(int count) const {return text.substr(text.size()-count);}
};
struct idRectangle {float x=0,y=0,w=0,h=0;};
struct idWinVar {
    std::string value="0", expression="2"; bool evaluate=true;
    const char* c_str() const {return value.c_str();}
    void Set(const char* text){value=text;}
    void SetEval(bool enabled){evaluate=enabled;}
    void Frame(){if(evaluate)value=expression;}
};
struct idSimpleWindow {
    std::map<std::string,idWinVar> values;
    idWinVar* GetWinVarByName(const char* name){auto it=values.find(name);return it==values.end()?nullptr:&it->second;}
};
struct idWindow;
struct drawWin_t {idWindow* win=nullptr;idSimpleWindow* simp=nullptr;};
struct idWindow : idSimpleWindow {
    virtual ~idWindow()=default;
    std::map<std::string,drawWin_t> children;
    idWindow* focus=nullptr;
    idWinVar* GetWinVarByName(const char* name,bool fixup){
        // Match the legacy resolver's dangerous fixup and gui:: allocation.
        auto result=idSimpleWindow::GetWinVarByName(name);
        if(result && fixup) result->evaluate=false;
        if(!result && std::strstr(name,"gui::")) result=&values[name];
        return result;
    }
    drawWin_t* FindChildByName(const char* name){auto it=children.find(name);return it==children.end()?nullptr:&it->second;}
    idWindow* GetFocusedChild(){return focus;}
};
struct idEditWindow : idWindow {
    bool valid=false;
    bool GetTextInputState(idRectangle& area,float& offset){area={10,20,300,40};offset=17;return valid;}
};
struct idDict {
    std::map<std::string,int> values;
    bool GetInt(const char* key,const char* fallback,int& result) const {
        auto it=values.find(key);result=it==values.end()?std::atoi(fallback):it->second;return it!=values.end();
    }
};
const char* va(const char* format,int value){static char result[64];std::snprintf(result,sizeof result,format,value);return result;}
struct idUserInterfaceLocal {
    idWindow* desktop=nullptr;idDict state;
    const idDict& State() const {return state;}
    bool GetPresentationValue(const char*,idStr&) const;
    bool SetPresentationValue(const char*,const char*,bool overrideExpression=true);
    bool GetTextInputState(idRectangle&,float&) const;
};
using idUserInterface=idUserInterfaceLocal;
'''
MAIN = r'''
int main(){
    idUserInterfaceLocal gui;
    idStr out="unchanged";
    assert(!gui.GetPresentationValue("curr",out) && out.text=="unchanged");
    assert(!gui.SetPresentationValue("curr","1"));
    idWindow root,child;
    idSimpleWindow simple;
    root.values["curr"]={};root.values["skill"]={};
    child.values["visible"]={};simple.values["rect"]={"0,0,640,100"};
    root.children={{"desktop",{&root,nullptr}},{"panel",{&child,nullptr}},{"simple",{nullptr,&simple}},{"empty",{}}};
    gui.desktop=&root;
    assert(gui.GetPresentationValue("curr",out) && out.text=="0");
    root.values["curr"].Frame();
    assert(gui.GetPresentationValue("desktop::curr",out) && out.text=="2");
    assert(root.values["curr"].evaluate);
    assert(gui.GetPresentationValue("panel::visible",out) && out.text=="0");
    assert(gui.GetPresentationValue("simple::rect",out) && out.text=="0,0,640,100");
    for(auto name:{"","missing","missing::visible","::curr","desktop::","desktop::gui::allocated","gui::allocated","empty::x"}) {
        out="unchanged";
        assert(!gui.GetPresentationValue(name,out) && out.text=="unchanged");
        assert(!gui.SetPresentationValue(name,"1"));
    }
    assert(!gui.GetPresentationValue(nullptr,out) && !gui.SetPresentationValue(nullptr,"1"));
    assert(!gui.SetPresentationValue("curr",nullptr));
    assert(root.values.size()==2 && child.values.size()==1 && simple.values.size()==1);
    assert(gui.SetPresentationValue("curr","4",false));
    assert(gui.GetPresentationValue("curr",out) && out.text=="4");
    root.values["curr"].Frame();
    assert(gui.GetPresentationValue("curr",out) && out.text=="2");
    assert(gui.SetPresentationValue("desktop::curr","7"));
    root.values["curr"].Frame();
    assert(gui.GetPresentationValue("curr",out) && out.text=="7");
    assert(gui.SetPresentationValue("curr","8",false));
    root.values["curr"].Frame();
    assert(gui.GetPresentationValue("curr",out) && out.text=="8");
    assert(gui.SetPresentationValue("simple::rect","10,20,300,400"));
    assert(gui.GetPresentationValue("simple::rect",out) && out.text=="10,20,300,400");
    assert(!simple.values["rect"].evaluate);
    assert(!MainMenuWindowStateEqualsInt(&gui,"missing",0));
    assert(MainMenuWindowStateEqualsInt(&gui,"desktop::curr",8));
    assert(MainMenuWindowStateIsNonZero(&gui,"curr"));
    assert(!MainMenuWindowStateIsNonZero(nullptr,"curr"));
    assert(MainMenuSetWindowVar(&gui,"panel::visible","1"));
    assert(!child.values["visible"].evaluate);
    assert(MainMenuGetNewGameOption(nullptr,"desktop::skill","skill",5)==5);
    gui.state.values["skill"]=3;
    assert(MainMenuGetNewGameOption(&gui,"desktop::skill","skill",5)==0);
    assert(root.values["skill"].evaluate);
    root.values.erase("skill");
    assert(MainMenuGetNewGameOption(&gui,"desktop::skill","skill",5)==3);
    gui.state.values["desktop::skill"]=4;
    assert(MainMenuGetNewGameOption(&gui,"desktop::skill","skill",5)==4);
    assert(MainMenuGetNewGameOption(&gui,nullptr,nullptr,5)==5);
    idRectangle area={1,2,3,4};float offset=5;
    idEditWindow edit;
    for(auto focus:{static_cast<idWindow*>(nullptr),&child,static_cast<idWindow*>(&edit)}) {
        root.focus=focus;
        assert(!gui.GetTextInputState(area,offset));
        assert(area.x==1 && area.y==2 && area.w==3 && area.h==4 && offset==5);
    }
    edit.valid=true;
    assert(gui.GetTextInputState(area,offset));
    assert(area.x==10 && area.y==20 && area.w==300 && area.h==40 && offset==17);
    std::puts("GUI presentation bridge: side-effect-free reads, explicit overrides, session fallback and atomic IME queries passed");
}
'''


def main():
    ui = (ROOT/'src/ui/UserInterface.cpp').read_text()
    session = (ROOT/'src/framework/Session_menu.cpp').read_text()
    code = SUPPORT + '\n'.join(function_body(ui, signature) for signature in (
        'static idWinVar *FindPresentationVariable(',
        'bool idUserInterfaceLocal::GetPresentationValue(',
        'bool idUserInterfaceLocal::SetPresentationValue(',
        'bool idUserInterfaceLocal::GetTextInputState('))
    code += '\n'.join(function_body(session, signature) for signature in (
        'static int MainMenuGetNewGameOption(',
        'static bool MainMenuWindowStateIsNonZero(',
        'static bool MainMenuWindowStateEqualsInt(',
        'static bool MainMenuSetWindowVar(')) + MAIN
    public = (ROOT/'src/ui/UserInterface.h').read_text()
    assert 'GetDesktop(' not in public and 'idWindow' not in public
    for path in ('src/framework/Session.cpp','src/framework/Session_menu.cpp','src/sys/sdl3/sdl3_backend.cpp'):
        assert 'GetDesktop(' not in (ROOT/path).read_text(),path
    compiler = next((found for name in ('clang++','g++','c++') if (found:=shutil.which(name))),None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-bridge-',dir=ROOT/'.tmp') as temp:
        source=Path(temp)/'bridge.cpp';binary=Path(temp)/'bridge.exe'
        source.write_text(code,encoding='utf-8')
        subprocess.run([compiler,'-std=c++17',str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)


if __name__=='__main__':
    main()
