#!/usr/bin/env python3
"""Execute production choice read/write behavior for GUI and CVar bindings."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from async_drop_client_contract import function_body

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
const char *va(const char *format,int value){static char buffer[32];std::snprintf(buffer,sizeof(buffer),format,value);return buffer;}
struct idStr {static int Icmp(const char *a,const char *b){while(*a&&*b){int diff=std::tolower(*a++)-std::tolower(*b++);if(diff)return diff;}return *a-*b;}};
struct Variable {
    std::string value;
    std::string *binding=nullptr;
    bool NeedsUpdate()const{return binding!=nullptr;}
    const char *c_str()const{return value.c_str();}
    void Set(const char *next){value=next;if(binding)*binding=value;}
    void Update(){if(binding)value=*binding;}
};
struct CVar {std::string value;const char *GetString(){return value.c_str();}void SetString(const char *next){value=next;}};
struct List {
    std::vector<std::string> data;
    int Num()const{return int(data.size());}
    const char *operator[](int index)const{return data.at(index).c_str();}
    void Append(const char *value){data.emplace_back(value);}
};
struct Updates {
    std::vector<Variable*> data;
    int Num()const{return int(data.size());}
    Variable *operator[](int index){return data.at(index);}
    void Update(){for(auto *v:data)v->Update();}
};
struct idChoiceWindow {
    int currentChoice=0,choiceType=1;
    bool liveUpdate=true;
    CVar *cvar=nullptr;
    Variable cvarStr,guiStr;
    Updates updateStr;
    List choices,values;
    void UpdateVars(bool read,bool force=false);
    void UpdateChoice();
    void ValidateChoice();
    void Select(int index){currentChoice=index;@SELECT@}
};
@METHODS@
int main() {
    for(int binding=0;binding<3;++binding)for(int kind=0;kind<3;++kind) {
        idChoiceWindow w;CVar c;std::string guiValue,cvarValue;
        w.choices.data={"First","Second","Third"};
        if(kind!=2)w.values.data={"best_of_one","best_of_three","best_of_five"};
        w.choiceType=kind==0?0:1;
        if(binding!=0){w.cvar=&c;w.cvarStr.binding=&cvarValue;w.updateStr.data.push_back(&w.cvarStr);}
        if(binding!=1){w.guiStr.binding=&guiValue;w.updateStr.data.push_back(&w.guiStr);}
        const std::string desired=kind==0?"2":kind==1?"best_of_five":"Third";
        if(w.cvar)c.value=desired;else guiValue=desired;
        w.UpdateChoice();assert(w.currentChoice==2);
        for(int index=0;index<3;++index) {
            w.Select(index);
            const std::string expected=kind==0?std::to_string(index):kind==1?w.values[index]:w.choices[index];
            if(w.cvar)assert(c.value==expected);
            if(w.guiStr.binding)assert(guiValue==(binding==2?std::to_string(index):expected));
            w.currentChoice=0;w.UpdateChoice();assert(w.currentChoice==index);
        }
        if(w.cvar)c.value="invalid";else guiValue="invalid";
        w.UpdateChoice();assert(w.currentChoice==0);
        // Deferred choices publish only when explicitly requested.
        w.liveUpdate=false;w.currentChoice=2;w.cvarStr.Set(desired.c_str());
        const std::string priorGUI=guiValue,priorCVar=c.value;
        w.UpdateVars(false);assert(guiValue==priorGUI && c.value==priorCVar);
        w.UpdateVars(false,true);
        if(w.cvar)assert(c.value==desired);
        if(w.guiStr.binding)assert(guiValue==(binding==2?"2":desired));
    }
    puts("choice GUI values, integer/CVar/dual bindings, fallback and deferred publication: PASS");
}
'''

# The production choice-list splitter: the stock tables quote their choices
# ('Off';'2x') and the engine quotes the names it lists ("Default Device").
LABELS = r"""
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
struct idStr {
    std::string s;
    idStr() {}
    idStr(const char *text) : s(text) {}
    int Length() const { return int(s.size()); }
    char operator[](int index) const { return s[size_t(index)]; }
    void Append(char c) { s.push_back(c); }
    void Clear() { s.clear(); }
    const char *c_str() const { return s.c_str(); }
    void Strip(char c) { while (!s.empty() && s.front() == c) s.erase(s.begin()); while (!s.empty() && s.back() == c) s.pop_back(); }
    void StripTrailingWhitespace() { while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back(); }
    idStr &operator=(const char *text) { s = text; return *this; }
};
struct idStrList { std::vector<std::string> data; void Append(const idStr &v) { data.push_back(v.s); } };
struct Dict { const char *GetString(const char *key) const { return std::string(key) == "#str_1" ? "Translated" : key; } };
struct Common { Dict dict; const Dict *GetLanguageDict() const { return &dict; } } commonLocal, *common = &commonLocal;
@FUNCTIONS@
static std::vector<std::string> Split(const char *text) { idStrList list; SplitChoiceList(idStr(text), list); return list.data; }
int main() {
    typedef std::vector<std::string> V;
    assert(Split("'Off';'2x';'4x';'8x';'16x'") == V({"Off", "2x", "4x", "8x", "16x"}));
    assert(Split("No;Yes") == V({"No", "Yes"}));
    assert(Split(" 'Standard (4:3)' ; 'Widescreen (16:9)' ") == V({"Standard (4:3)", "Widescreen (16:9)"}));
    // The engine's own lists: double quotes, a separator and an escaped quote inside.
    assert(Split("\"Default Device (OpenAL Soft)\";\"Speakers; front \\\"A\\\"\"") ==
           V({"Default Device (OpenAL Soft)", "Speakers; front \"A\""}));
    // An apostrophe that only begins or ends a plain word opens nothing.
    assert(Split("'tis fine;don't;rock 'n' roll") == V({"'tis fine", "don't", "rock 'n' roll"}));
    assert(Split("#str_1;'#str_1';plain") == V({"Translated", "Translated", "plain"}));
    assert(Split("''; ;last") == V({"last"}));
    puts("choice labels: quoted stock and engine lists show without their quotes: PASS");
}
"""


def main():
    production = (ROOT / "src/ui/ChoiceWindow.cpp").read_text(encoding="utf-8")
    methods = "\n".join(function_body(production, "void idChoiceWindow::" + name)
                        for name in ("UpdateVars", "UpdateChoice", "ValidateChoice"))
    event = function_body(production, "const char *idChoiceWindow::HandleEvent")
    select = event[event.index("if ( choiceType == 0 )"):event.index("if ( runAction2 )")]
    source = HARNESS.replace("@METHODS@", methods).replace("@SELECT@", select)
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="gui-choice-", dir=ROOT / ".tmp") as temp:
        cpp, exe = Path(temp) / "test.cpp", Path(temp) / "test.exe"
        cpp.write_text(source, encoding="utf-8")
        command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)
        functions = "\n".join(function_body(production, "static " + signature) for signature in (
            "void AppendChoiceListSegment", "int ChoiceListQuoteEnd", "void SplitChoiceList"))
        cpp.write_text(LABELS.replace("@FUNCTIONS@", functions), encoding="utf-8")
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
