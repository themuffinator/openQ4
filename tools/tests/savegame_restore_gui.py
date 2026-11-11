#!/usr/bin/env python3
"""Exercise SP/MP serialized GUI admission at the actual restore boundary."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>
struct idStr:std::string {
    using std::string::string;using std::string::operator=;
    int Length() const {return static_cast<int>(size());}
    operator const char*() const {return c_str();}
    static int Icmp(const char* a,const char* b){for(;;++a,++b){const int x=std::tolower(static_cast<unsigned char>(*a)),y=std::tolower(static_cast<unsigned char>(*b));if(x!=y)return x-y;if(!x)return 0;}}
    void SetFileExtension(const char* ext){const size_t dot=find_last_of('.'),slash=find_last_of("/\\");if(dot!=npos&&(slash==npos||dot>slash))erase(dot);append(ext);}
};
struct idDict {};
struct File {int reads=0;};
struct idUserInterface {
    bool valid=true;
    bool ReadFromSaveGame(File* file){++file->reads;return valid;}
} available;
struct Manager {
    idUserInterface* result=&available;
    int calls=0;bool unique=false;std::string path;
    idUserInterface* FindGui(const char* name,bool load,bool value){if(!load)throw std::runtime_error("autoload disabled");++calls;unique=value;path=name;return result;}
} manager,*uiManager=&manager;
struct openQ4SaveGameGuiRevision_t {const char* currentWindow="new";} revision;
static bool historical=false;static int openQ4SaveGameLayoutBuild=0;
static const openQ4SaveGameGuiRevision_t* OpenQ4_FindSaveGameGuiRevision(const char*,int){return historical?&revision:nullptr;}
struct idRestoreGame {
    File storage;File* file=&storage;
    idStr name="guis/in-world-terminal.gui";
    bool unique=false,current=true;
    int syncs=0,flags=0,parms=0,legacy=0;
    void ReadSyncId(const char*){++syncs;}
    void ReadString(idStr& out){out=name;}
    void ReadBool(bool& out){out=unique;++flags;}
    bool GuiRecordNamesWindow(const char*){return current;}
    void ReadUserInterfaceRevision(idUserInterface*,const openQ4SaveGameGuiRevision_t&){++legacy;}
    void UpdateGuiParms(idUserInterface*,const idDict*){++parms;}
    void ReadUserInterface(idUserInterface*&,const idDict*);
    [[noreturn]] void Error(const char* format,...){
        char message[512];va_list args;va_start(args,format);vsnprintf(message,sizeof(message),format,args);va_end(args);
        throw std::runtime_error(message);
    }
};
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
'''
MAIN = r'''
int main(){try {
    idDict args;
    for(bool unique:{false,true}) {
        idRestoreGame restore;restore.unique=unique;manager.result=nullptr;historical=false;
        idUserInterface* ui=&available;std::string error;
        try {restore.ReadUserInterface(ui,&args);}catch(const std::runtime_error& e){error=e.what();}
        require(!error.empty(),"missing serialized GUI skipped its unframed payload without an error");
        require(error.find(restore.name)!=std::string::npos,"missing-GUI error lost the exact definition name");
        require(!ui && !restore.storage.reads && !restore.parms && !restore.legacy,"failed GUI published state or read payload");
        require(restore.syncs==1 && restore.flags==1 && manager.unique==unique,"failed GUI header/uniqueness handling changed");
    }
    for(bool historic:{false,true})for(bool current:{false,true})for(bool unique:{false,true}) {
        idRestoreGame restore;restore.current=current;restore.unique=unique;
        manager.result=&available;available.valid=true;historical=historic;
        idUserInterface* ui=nullptr;restore.ReadUserInterface(ui,&args);
        const bool old=historic && !current;
        require(ui==&available && restore.parms==1 && manager.unique==unique,"healthy GUI ownership or parameters changed");
        require(restore.legacy==(old?1:0) && restore.storage.reads==(old?0:1),"historical/current GUI reader choice changed");
    }
    {
        idRestoreGame restore;manager.result=&available;available.valid=false;historical=false;
        idUserInterface* ui=nullptr;bool rejected=false;
        try {restore.ReadUserInterface(ui,&args);}catch(const std::runtime_error&){rejected=true;}
        require(rejected && restore.storage.reads==1 && !restore.parms,"failed GUI payload accepted or parameters published");
    }
    {
        idRestoreGame restore;restore.name="";manager.calls=0;
        idUserInterface* ui=&available;restore.ReadUserInterface(ui,&args);
        require(!ui && !manager.calls && !restore.flags && !restore.storage.reads && !restore.parms,"null GUI record consumed payload or created a GUI");
    }
    for(const char* saved:{"guis/weapons/railgun_scope.q4ui","guis/hud/REMASTERED_MARINE.Q4UI"}) {
        idRestoreGame restore;restore.name=saved;manager.result=&available;available.valid=true;historical=false;
        idUserInterface* ui=nullptr;restore.ReadUserInterface(ui,&args);
        idStr expected=saved;expected.SetFileExtension(".rui");
        require(ui==&available && manager.path==expected,"a GUI record saved before the .rui rename did not open the renamed document");
    }
    {
        idRestoreGame restore;restore.name="guis/menu/title.rui";manager.result=&available;available.valid=true;historical=false;
        idUserInterface* ui=nullptr;restore.ReadUserInterface(ui,&args);
        require(manager.path=="guis/menu/title.rui","a current .rui GUI record was renamed");
    }
    std::puts("GUI restore: missing definition rejected before payload; null, current, unique, historical and pre-.rui readers preserved");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
'''


def source(mode):
    return SUPPORT + function_body((ROOT / 'src' / mode / 'gamesys/SaveGame.cpp').read_text(),
                                   'void idRestoreGame::ReadUserInterface(') + MAIN


def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]), encoding='utf-8')
        return 0
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    failures = 0
    with tempfile.TemporaryDirectory(prefix='save-gui-', dir=ROOT / '.tmp') as directory:
        for mode in ('game', 'mpgame'):
            cpp = Path(directory) / (mode + '.cpp'); exe = cpp.with_suffix('.exe')
            cpp.write_text(source(mode), encoding='utf-8')
            subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], check=True)
            print(mode, flush=True)
            failures += subprocess.run([str(exe)]).returncode != 0
    return int(failures != 0)


if __name__ == '__main__':
    raise SystemExit(main())
