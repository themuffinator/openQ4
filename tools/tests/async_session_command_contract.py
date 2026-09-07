#!/usr/bin/env python3
"""Exercise the production network session-command dispatcher with native callbacks."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from async_drop_client_contract import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>
constexpr int CMD_EXEC_INSERT=1;
struct idStr {
    static int Icmp(const char *a,const char *b) {
        for(;*a && *b;++a,++b) {
            int difference=std::tolower(static_cast<unsigned char>(*a))-std::tolower(static_cast<unsigned char>(*b));
            if(difference) return difference;
        }
        return *a-*b;
    }
};
struct idCmdArgs {
    std::vector<std::string> words;
    void TokenizeString(const char *text,bool) { std::istringstream stream(text); std::string word; while(stream>>word) words.push_back(word); }
    int Argc()const{return static_cast<int>(words.size());}
    const char *Argv(int i)const{return words.at(i).c_str();}
};
struct Server {bool active=false; bool IsActive(){return active;}};
struct idAsyncNetwork {static Server server; static void ExecuteSessionCommand(const char*);};
Server idAsyncNetwork::server;
struct Commands {int mode=0,count=0;std::string text;void BufferCommandText(int m,const char *t){mode=m;text=t;++count;}} commands,*cmdSystem=&commands;
struct Game {void *StartMenu(){return this;}} gameValue,*game=&gameValue;
struct Session {int menus=0;void SetGUI(void*,void*){++menus;}} sessionValue,*session=&sessionValue;
struct Arena {int completions=0;void QueueCompletion(const idCmdArgs&){++completions;}} arenaCampaign;
'''
MAIN = r'''
int main() {
    for(int active=0;active<2;++active) {
        idAsyncNetwork::server.active=active;
        for(const char *command:{"nextMap","NEXTMAP","nextMap extra","nextMap;quit","arbitrary",""}) {
            commands={};
            idAsyncNetwork::ExecuteSessionCommand(command);
            const bool accepted=active && (std::string(command)=="nextMap" || std::string(command)=="NEXTMAP");
            assert(commands.count==(accepted?1:0));
            if(accepted) assert(commands.mode==CMD_EXEC_INSERT && commands.text=="nextMap\n");
        }
    }
    commands={};
    idAsyncNetwork::ExecuteSessionCommand("game_startmenu");
    assert(sessionValue.menus==1 && commands.count==0);
    idAsyncNetwork::ExecuteSessionCommand("arenaComplete win");
    assert(arenaCampaign.completions==1 && commands.count==0);
}
'''


def main():
    body = function_body((ROOT / "src/framework/async/AsyncNetwork.cpp").read_text(encoding="utf-8"),
                         "void idAsyncNetwork::ExecuteSessionCommand")
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="session-command-", dir=ROOT / ".tmp") as directory:
        source, executable = Path(directory) / "contract.cpp", Path(directory) / "contract.exe"
        source.write_text(SUPPORT + body + MAIN, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    print("async session commands: PASS")


if __name__ == "__main__":
    main()
