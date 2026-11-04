#!/usr/bin/env python3
"""Check smooth scope contours and the production scope router."""
from pathlib import Path
import math
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def check_contours():
    """Sample authored curves independently of the renderer's tessellator.

    Exact antialiasing cannot repair a staircase baked into a circular path.
    Bound the radial error while retaining the deliberate inner-ring notches.
    """
    sys.path.insert(0, str(ROOT / 'tools/ui'))
    import build_retained_scopes as scopes

    def samples(name):
        contours = []
        for command in scopes.measured(name, [0, 0, 0, 1])['commands']:
            points = [tuple(v / scopes.U for v in point) for point in command.get('points', [])]
            op = command['op']
            if op == 'move':
                contours.append([points[0]])
            elif op == 'line':
                contours[-1].append(points[0])
            elif op == 'cubic':
                start = contours[-1][-1]
                a, b, end = points
                for step in range(1, 33):
                    t = step / 32
                    contours[-1].append(tuple((1-t)**3*start[i] + 3*(1-t)**2*t*a[i] +
                                             3*(1-t)*t*t*b[i] + t**3*end[i] for i in (0, 1)))
            elif op != 'close':
                raise AssertionError(f'unexpected contour operation: {op}')
        return contours

    outer, inner = samples('nail_ring2')
    radius = lambda p: math.hypot(p[0]-320, p[1]-241)
    assert max(abs(radius(p)-252) for p in outer) < .1, 'nail ring contains geometric teeth'
    assert min(map(radius, inner)) >= 224.99 and max(map(radius, inner)) <= 242.1
    for degrees, expected in ((0, 225), (45, 242), (90, 225), (135, 242),
                              (180, 225), (225, 242), (270, 225), (315, 242)):
        angle = math.radians(degrees)
        point = min(inner, key=lambda p: abs(math.remainder(math.atan2(p[1]-241, p[0]-320)-angle, math.tau)))
        assert abs(radius(point)-expected) < .1, 'lost an intentional ring notch'
    hooks = samples('nail_hook')
    assert len(hooks) == 4
    for contour, (sx, sy) in zip(hooks, ((1, 1), (-1, 1), (1, -1), (-1, -1))):
        assert len(contour) == len(hooks[0])
        for (x, y), (a, b) in zip(hooks[0], contour):
            assert abs(a-(320+sx*(x-320))) < 1e-6 and abs(b-(240+sy*(y-240))) < 1e-6
    print('scope contours: smooth circular boundary, retained notches and symmetric hooks passed')


SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>
struct idStr {
    std::string value;
    idStr(const char* v=""):value(v){}
    operator const char*()const{return value.c_str();}
    const char* c_str()const{return value.c_str();}
    static int Icmp(const char*a,const char*b){std::string x(a),y(b);for(auto&c:x)c=std::tolower(c);for(auto&c:y)c=std::tolower(c);return x.compare(y);}
    int Icmp(const char*b)const{return Icmp(value.c_str(),b);}
    void SetFileExtension(const char*ext){value=value.substr(0,value.find_last_of('.'))+ext;}
    void Clear(){value.clear();}
};
struct idKeyValue {idStr key,value;const idStr& GetKey()const{return key;}const idStr& GetValue()const{return value;}};
struct idDict {
    std::vector<idKeyValue> values;
    void Set(const char*k,const char*v){for(auto&x:values)if(!x.key.Icmp(k)){x.value=v;return;}values.push_back({k,v});}
    const char* GetString(const char*k)const{for(auto&x:values)if(!x.key.Icmp(k))return x.value;return "";}
    int GetNumKeyVals()const{return (int)values.size();}
    const idKeyValue*GetKeyVal(int i)const{return &values[i];}
};
struct idUserInterface {
    idStr name;idDict state;bool unique=false;int changed=0;std::vector<std::string> events;
    idUserInterface(const char*n):name(n){state.Set("name",n);}
    const char*Name()const{return name;} const idDict&State()const{return state;}
    void SetStateString(const char*k,const char*v){state.Set(k,v);}
    void StateChanged(int time){changed=time;} float GetStateFloat(const char*k)const{return std::atof(state.GetString(k));}
    void HandleNamedEvent(const char*e){events.push_back(e);} bool IsUniqued()const{return unique;}
};
struct Cvars {bool retained=true;const char*mod="baseoq4";bool GetCVarBool(const char*){return retained;}const char*GetCVarString(const char*k){return std::string(k)=="fs_game"?mod:"";}} cv;
auto*cvarSystem=&cv;
struct idFile{};
struct Files {bool missing=false,modOverride=false;int reads=0;idFile file;int ReadFile(const char*,void*){++reads;return missing?-1:1;}idFile*OpenFileRead(const char*,bool,const char*){return modOverride?&file:nullptr;}void CloseFile(idFile*){}} fs;
auto*fileSystem=&fs;
struct Manager {
    bool invalid=false;int loads=0,frees=0;
    idUserInterface*FindGui(const char*n,bool,bool unique){++loads;if(invalid)return nullptr;auto*g=new idUserInterface(n);g->unique=unique;return g;}
    void DeAlloc(idUserInterface*g){++frees;delete g;}
} manager;
auto*uiManager=&manager;
struct Game {int time=101;} gameLocal;
'''
MAIN = r'''
int main(){
 for(const char*kind:{"machinegun","railgun","nailgun"}){
    const std::string stock=std::string("guis/weapons/")+kind+"_scope.gui",retained=stock.substr(0,stock.size()-4)+".q4ui";
    idDict args;args.Set("gui_zoom",stock.c_str());idStr requested;
    idUserInterface old(stock.c_str());old.SetStateString("lockStatus","1");old.SetStateString("lockRange","12.4");
    idUserInterface*gui=&old;cv.retained=false;
    OpenQ4_UpdateScopeGui(gui,requested,args);assert(gui==&old);
    cv.retained=true;OpenQ4_UpdateScopeGui(gui,requested,args);
    assert(gui!=&old && std::string(gui->Name())==retained && gui->changed==101 && gui->unique);
    assert(std::string(gui->State().GetString("name"))==retained && std::string(gui->State().GetString("lockRange"))=="12.4");
    assert(gui->events.size()==1 && gui->events[0]=="lockAquired");
    int loads=manager.loads,reads=fs.reads;OpenQ4_UpdateScopeGui(gui,requested,args);assert(manager.loads==loads && fs.reads==reads);
    // Restore has consumed the serialized GUI, then clears only the transient preference cache.
    requested.Clear();cv.retained=false;int frees=manager.frees;
    OpenQ4_UpdateScopeGui(gui,requested,args);assert(std::string(gui->Name())==stock && manager.frees==frees+1);
    manager.DeAlloc(gui);
    gui=&old;requested.Clear();cv.retained=true;fs.missing=true;
    OpenQ4_UpdateScopeGui(gui,requested,args);assert(gui==&old);fs.missing=false;
    requested.Clear();manager.invalid=true;OpenQ4_UpdateScopeGui(gui,requested,args);assert(gui==&old);manager.invalid=false;
    requested.Clear();cv.mod="custom";fs.modOverride=true;OpenQ4_UpdateScopeGui(gui,requested,args);assert(gui==&old);
    cv.mod="baseoq4";fs.modOverride=false;
 }
 idDict args;args.Set("gui_zoom","guis/modscope.gui");idStr requested;idUserInterface old("guis/modscope.gui");auto*gui=&old;
 int reads=fs.reads;OpenQ4_UpdateScopeGui(gui,requested,args);assert(gui==&old && reads==fs.reads);
}
'''

def main():
    check_contours()
    subprocess.run([sys.executable, str(ROOT/'tools/ui/build_retained_scopes.py'), '--check'], cwd=ROOT, check=True)
    compiler=next((shutil.which(n) for n in ('clang++','g++','c++') if shutil.which(n)),None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    with tempfile.TemporaryDirectory(prefix='scope-routing-',dir=ROOT/'.tmp') as directory:
        source=Path(directory)/'routing.cpp'; binary=Path(directory)/'routing.exe'
        source.write_text(SUPPORT+'\n#include "'+(ROOT/'src/game/gamesys/RetainedScope.h').as_posix()+'"\n'+MAIN,encoding='utf-8')
        subprocess.run([compiler,'-std=c++17',str(source),'-o',str(binary)],check=True,cwd=ROOT)
        subprocess.run([str(binary)],check=True,cwd=ROOT)
    print('scope routing: stock/retained, gate changes, state, restore, ownership, missing/invalid documents and mod precedence passed')

if __name__=='__main__':
    main()
