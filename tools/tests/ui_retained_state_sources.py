#!/usr/bin/env python3
"""Run the production CVar-to-retained-state adapter with typed registry fixtures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <variant>
namespace openq4::ui { using StateValue=std::variant<double,bool,std::string>; }
enum { CVAR_BOOL=1, CVAR_INTEGER=2, CVAR_FLOAT=4, CVAR_STRING=8 };
struct idCVar {
    int flags, integer; float floating; std::string text;
    int GetFlags() const { return flags; }
    int GetInteger() const { return integer; }
    bool GetBool() const { return integer!=0; }
    float GetFloat() const { return floating; }
    const char* GetString() const { return text.c_str(); }
};
struct Registry {
    std::map<std::string,idCVar> values;
    const idCVar* Find(const char* name) const { auto it=values.find(name); return it==values.end()?nullptr:&it->second; }
} registry;
Registry* cvarSystem=&registry;
'''
MAIN = r'''
int main() {
    registry.values={{"integer",{CVAR_INTEGER,2147483647,2147483648.f,"2147483647"}},
                     {"float",{CVAR_FLOAT,1,1.25f,"1.25"}},
                     {"boolean",{CVAR_BOOL,1,1.f,"1"}},
                     {"text",{CVAR_STRING,0,0.f,"Player"}},
                     {"bad",{CVAR_FLOAT,0,std::numeric_limits<float>::infinity(),"inf"}}};
    openq4::ui::StateValue value;
    assert(ReadCVar("integer",0,value) && std::get<double>(value)==2147483647.0);
    assert(ReadCVar("float",0,value) && std::get<double>(value)==1.25);
    assert(ReadCVar("boolean",1,value) && std::get<bool>(value));
    assert(ReadCVar("boolean",0,value) && std::get<double>(value)==1.0);
    assert(ReadCVar("text",2,value) && std::get<std::string>(value)=="Player");
    assert(!ReadCVar("text",0,value) && !ReadCVar("integer",1,value));
    assert(!ReadCVar("bad",0,value) && !ReadCVar("missing",0,value));
    std::puts("retained state sources: production typed CVar reads, integer precision and invalid sources passed");
}
'''


def main():
    code = (ROOT/'src/ui/RetainedUI.cpp').read_text()
    adapter = function_body(code,'bool ReadCVar(').replace(' override','')
    compiler = next((found for name in ('clang++','g++','c++') if (found:=shutil.which(name))),None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-state-source-',dir=ROOT/'.tmp') as temp:
        source=Path(temp)/'sources.cpp'; binary=Path(temp)/'sources.exe'
        source.write_text(SUPPORT+adapter+MAIN,encoding='utf-8')
        subprocess.run([compiler,'-std=c++20',str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)


if __name__ == '__main__':
    main()
