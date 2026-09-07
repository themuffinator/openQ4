#!/usr/bin/env python3
"""Execute the production list event handler with headless GUI/input doubles."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from async_drop_client_contract import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
template<class T> T Max(T a,T b){return std::max(a,b);}
struct idMath { static int Ftoi(float v){return static_cast<int>(v);} };
class idStr {
public:
    std::string value;
    idStr()=default;
    idStr(const char *s):value(s){}
    const char *c_str()const{return value.c_str();}
    operator const char *()const{return c_str();}
    int Length()const{return static_cast<int>(value.size());}
    char operator[](int i)const{return value[i];}
    void CapLength(int length){value.resize(length);}
    idStr &operator+=(const char *suffix){value+=suffix;return *this;}
    void Append(int c){value+=static_cast<char>(c);}
    static bool CharIsPrintable(int c){return c>=32&&c<127;}
    static int Icmpn(const char *a,const char *b,int n){
        for(int i=0;i<n;++i){int x=std::tolower(static_cast<unsigned char>(a[i]));int y=std::tolower(static_cast<unsigned char>(b[i]));if(x!=y||!x)return x-y;}return 0;
    }
};
template<class T> struct idList {
    std::vector<T> values;
    int Num()const{return static_cast<int>(values.size());}
    void Clear(){values.clear();}
    void Append(const T &v){values.push_back(v);}
    int FindIndex(const T &v)const{auto it=std::find(values.begin(),values.end(),v);return it==values.end()?-1:static_cast<int>(it-values.begin());}
    void RemoveIndex(int i){values.erase(values.begin()+i);}
    T &operator[](int i){assert(i>=0&&i<Num());return values[i];}
};
char *va(const char *format,...){
    static char buffers[8][128];static unsigned next=0;char *out=buffers[next++%8];
    va_list ap;va_start(ap,format);std::vsnprintf(out,128,format,ap);va_end(ap);return out;
}
enum {SE_KEY,SE_CHAR,SE_MOUSE};
enum {K_MOUSE1=1000,K_MOUSE2,K_ENTER,K_KP_ENTER,K_MWHEELUP,K_MWHEELDOWN,K_UPARROW,K_PGUP,K_DOWNARROW,K_PGDN,K_CTRL};
enum {ON_ENTER};
struct sysEvent_t {int evType,evValue,evValue2;};
struct idKeyInput {static bool ctrl;static bool IsDown(int key){return key==K_CTRL&&ctrl;}};
bool idKeyInput::ctrl=false;
struct SysDouble {sysEvent_t GenerateMouseButtonEvent(int,bool down){return {SE_KEY,K_MOUSE1,down};}} sysValue;
SysDouble *sys=&sysValue; // A pure value; this never calls an operating-system input API.
struct GuiDouble {
    int time=1000;float x=10,y=6;
    std::map<std::string,int> state;
    std::vector<int> observedRows;
    std::vector<int> observedCounts;
    float CursorX()const{return x;}float CursorY()const{return y;}int GetTime()const{return time;}
    void SetStateInt(const char *key,int value){state[key]=value;}
};
struct SliderDouble {
    float high=0,value=0;
    bool Contains(float x,float)const{return x>=85;}
    float GetHigh()const{return high;}float GetValue()const{return value;}void SetValue(float v){value=v;}
};
struct idWindow {
    GuiDouble *gui;bool noEvents=false;int entered=0;idStr cmd;
    bool Contains(float x,float y)const{return x>=0&&x<100&&y>=0&&y<90;}
    const char *HandleEvent(const sysEvent_t *event,bool*){
        if(!noEvents&&event->evType==SE_KEY&&event->evValue2&&event->evValue==K_MOUSE1&&Contains(gui->x,gui->y)){
            gui->observedRows.push_back(gui->state["serverList_sel_0"]);
            gui->observedCounts.push_back(gui->state["serverList_numsel"]);
            cmd="selected";
        }
        return cmd;
    }
    void RunScript(int script){assert(script==ON_ENTER);++entered;cmd="connect";}
};
struct idDeviceContext {
    int TextWidth(const char *text,float,int,int){
        int width=0;for(const unsigned char *p=reinterpret_cast<const unsigned char *>(text);*p;++p)if((*p&0xc0)!=0x80)width+=6;return width;
    }
};
class idListWindow:public idWindow {
public:
    int itemheight=18,top=0,clickTime=0,clickRow=-1,typedTime=0;
    float actualY=0;
    struct {float h=90;} textRect;
    SliderDouble slider;SliderDouble *scroller=&slider;
    bool multipleSel=false;
    idList<idStr> listItems;
    idList<int> currentSel;
    idStr listName="serverList",typed;
    explicit idListWindow(GuiDouble &g){gui=&g;for(auto s:{"Alpha","Beta","Charlie","Delta"})listItems.Append(s);}
    float GetMaxCharHeight()const{return 12;}
    void SetCurrentSel(int);
    void ClearSelection(int);
    void AddCurrentSel(int);
    int GetCurrentSel();
    bool IsSelected(int);
    const char *HandleEvent(const sysEvent_t*,bool*);
};
static const int pixelOffset=3,doubleClickSpeed=300;
static const int Q4_LIST_WINDOW_TEXT_SPACING=0;
'''

MAIN = r'''
static void click(idListWindow &list,int row,int time,bool ctrl=false){
    list.gui->x=10;list.gui->y=row*18+6;list.gui->time=time;idKeyInput::ctrl=ctrl;
    const sysEvent_t down={SE_KEY,K_MOUSE1,1},up={SE_KEY,K_MOUSE1,0};
    list.HandleEvent(&down,nullptr);list.HandleEvent(&up,nullptr);
}
static void key(idListWindow &list,int value,int type=SE_KEY){
    const sysEvent_t event={type,value,1};list.HandleEvent(&event,nullptr);
}
int main(){
    idDeviceContext dc;
    assert(std::string(openQ4_FitBrowserCell(&dc,"DM",.17f,12).c_str())=="DM");
    assert(std::string(openQ4_FitBrowserCell(&dc,"Deathmatch",.17f,36).c_str())=="Dea...");
    assert(std::string(openQ4_FitBrowserCell(&dc,"Deathmatch",.17f,17).c_str()).empty());
    assert(std::string(openQ4_FitBrowserCell(&dc,"A\xc3\xa9\xf0\x9f\x8e\xaf" "BCDEF",.17f,36).c_str())=="A\xc3\xa9\xf0\x9f\x8e\xaf...");
    assert(std::string(openQ4_FitBrowserCell(&dc,"A\xc3\xa9\xf0\x9f\x8e\xaf" "BCDEF",.17f,24).c_str())=="A...");
    GuiDouble g;idListWindow list(g);
    click(list,0,1000);assert(list.entered==0&&list.GetCurrentSel()==0&&g.observedRows.back()==0);
    click(list,1,1100);assert(list.entered==0&&list.GetCurrentSel()==1&&g.observedRows.back()==1);
    click(list,1,1200);assert(list.entered==1&&std::string(list.cmd.c_str())=="connect");
    // A completed double-click cannot turn the next single click into another join.
    click(list,1,1250);assert(list.entered==1);
    click(list,1,1800);assert(list.entered==1);
    // A preselected first row at GUI time zero is still a single click.
    GuiDouble fresh;idListWindow initial(fresh);initial.SetCurrentSel(0);
    click(initial,0,0);assert(initial.entered==0);click(initial,0,100);assert(initial.entered==1);
    click(initial,0,500);key(initial,K_ENTER);click(initial,0,600);assert(initial.entered==2);
    GuiDouble m;idListWindow multi(m);multi.multipleSel=true;
    click(multi,0,1000,true);assert(multi.currentSel.Num()==1&&multi.IsSelected(0));
    click(multi,1,1100,true);assert(multi.currentSel.Num()==2&&multi.IsSelected(0)&&multi.IsSelected(1));
    click(multi,0,1200,true);assert(multi.currentSel.Num()==1&&!multi.IsSelected(0)&&multi.IsSelected(1));
    click(multi,1,1300,true);assert(multi.currentSel.Num()==0&&m.state["serverList_sel_0"]==-1&&m.state["serverList_numsel"]==0);
    assert(m.observedRows.back()==-1&&m.observedCounts.back()==0&&multi.entered==0);
    // Clicking below the final row clears the selection; it never picks the last server.
    click(list,4,1900);assert(list.currentSel.Num()==0&&g.state["serverList_sel_0"]==-1&&list.entered==1);
    GuiDouble e;idListWindow empty(e);empty.listItems.Clear();click(empty,0,1000);
    assert(empty.currentSel.Num()==0&&e.state["serverList_sel_0"]==-1&&e.state["serverList_numsel"]==0&&empty.entered==0);
    // Scroller clicks and disabled lists must not select or activate a server.
    click(list,0,2000);list.gui->x=90;list.gui->y=24;list.gui->time=2100;
    const sysEvent_t down={SE_KEY,K_MOUSE1,1};list.HandleEvent(&down,nullptr);
    assert(list.GetCurrentSel()==0&&list.entered==1);
    list.noEvents=true;click(list,1,2200);assert(list.GetCurrentSel()==0&&list.entered==1);list.noEvents=false;
    // Action callbacks see the newly selected row from keyboard and type-search.
    idKeyInput::ctrl=false;g.x=10;g.y=6;key(list,K_DOWNARROW);
    assert(list.GetCurrentSel()==1&&g.observedRows.back()==1&&g.observedCounts.back()==1);
    key(list,'D',SE_CHAR);assert(list.GetCurrentSel()==3&&g.observedRows.back()==3);
    list.textRect.h=36;list.slider.high=2;key(list,K_UPARROW);
    assert(list.GetCurrentSel()==2&&g.observedRows.back()==2&&list.top==1&&g.state["serverList_top"]==1);
    key(list,K_PGDN);assert(list.GetCurrentSel()==3&&g.observedRows.back()==3&&list.top==2);
    key(list,K_PGUP);assert(list.GetCurrentSel()==2&&g.observedRows.back()==2);
    key(list,K_ENTER);assert(list.entered==2);
}
'''


def main() -> None:
    source = (ROOT / "src/ui/ListWindow.cpp").read_text(encoding="utf-8")
    header = (ROOT / "src/ui/ListWindow.h").read_text(encoding="utf-8")
    assert "clickRow" in header and "clickRow = -1;" in function_body(source, "void idListWindow::CommonInit()")
    functions = "\n".join(function_body(source, signature) for signature in (
        "static idStr openQ4_FitBrowserCell(",
        "void idListWindow::SetCurrentSel(", "void idListWindow::ClearSelection(",
        "void idListWindow::AddCurrentSel(", "int idListWindow::GetCurrentSel(",
        "bool idListWindow::IsSelected(", "const char *idListWindow::HandleEvent("))
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="list-selection-", dir=ROOT / ".tmp") as directory:
        source_path, binary = Path(directory) / "selection.cpp", Path(directory) / "selection.exe"
        source_path.write_text(SUPPORT + functions + MAIN, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", str(source_path), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        # Compile the same production functions against the real string API as
        # well. Runtime doubles must not accidentally invent engine methods.
        real_string = r'''
#include <cstring>
#include <cstdlib>
#define ID_INLINE inline
#define id_attribute(x)
#define BIT(x) (1<<(x))
using byte=unsigned char;
class idCmdArgs;
template<class T> struct idList;
struct idLib {static int SizeToInt(size_t,const char*);static void Error(const char*,...);};
#include "src/idlib/Str.h"
#undef vsnprintf
'''
        checked_support = re.sub(r"class idStr \{[\s\S]*?\n\};", lambda _: real_string, SUPPORT, count=1)
        source_path.write_text(checked_support + functions + MAIN, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-D_CRT_SECURE_NO_WARNINGS", "-fsyntax-only", "-I", str(ROOT), str(source_path)], check=True)
    print("list_window_selection_contract: PASS (production click identity, Ctrl selection, callbacks, keyboard, scrolling)")


if __name__ == "__main__":
    main()
