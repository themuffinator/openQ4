#!/usr/bin/env python3
"""Exercise Android save-root selection and early fatal breadcrumb placement."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
static void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct idStr: std::string {
    using std::string::string;
    using std::string::operator=;
    int Length() const {return static_cast<int>(size());}
    void CapLength(int count){resize(count);}
    void StripTrailing(char value){while(!empty() && back()==value)pop_back();}
    void AppendPath(const char* value){if(!empty() && back()!='/')push_back('/');append(value);}
    static void Copynz(char* out,const char* value,int size){std::snprintf(out,size,"%s",value);}
};
static idStr savepath;
static std::map<std::string,std::string> environment;
static const char* TestGetenv(const char* name){auto i=environment.find(name);return i==environment.end()?nullptr:i->second.c_str();}
#define getenv TestGetenv
static const char* internalStorage="/data/user/0/com.darkmatter.openq4/files";
static const char* SDL_GetAndroidInternalStoragePath(){return internalStorage;}
static const char* Posix_Cwd(){return "/legacy-cwd";}
static constexpr int MAX_OSPATH=1024;
static const char* OPENQ4_GAMEDIR="baseoq4";
static char posix_fatalBreadcrumbPath[MAX_OSPATH];
static std::vector<std::string> created;
static void Sys_Mkdir(const char* path){created.emplace_back(path);}
'''
MAIN = r'''
static void reset(){
    savepath.clear();environment.clear();created.clear();posix_fatalBreadcrumbPath[0]='\0';
    internalStorage="/data/user/0/com.darkmatter.openq4/files";
}
struct SaveRootCase {const char* root;std::vector<std::string> levels;};
int main(){try {
    // The fatal directory is created like mkdir -p: every level from the top,
    // because a first run's save root may not exist yet.
    const std::string app="/data/user/0/com.darkmatter.openq4";
    const SaveRootCase cases[]={
        {"/data/user/0/com.darkmatter.openq4/files/saves",
         {"/data","/data/user","/data/user/0",app,app+"/files",app+"/files/saves",
          app+"/files/saves/baseoq4",app+"/files/saves/baseoq4/logs"}},
        {"/private/player saves///",
         {"/private","/private/player saves","/private/player saves/baseoq4","/private/player saves/baseoq4/logs"}},
        {"/",{"/baseoq4","/baseoq4/logs"}},
    };
    for(const SaveRootCase& test:cases){
        reset();environment[OPENQ4_ANDROID_SAVE_ROOT_ENV]=test.root;environment["USER_FILES"]="/legacy";
        std::string expected=test.root;while(expected.size()>1 && expected.back()=='/')expected.pop_back();
        require(Sys_DefaultSavePath()==expected,"explicit save root did not win over legacy/default paths");
        Posix_InitFatalBreadcrumbPath();
        std::string directory=expected+(expected.back()=='/'?"":"/")+"baseoq4";
        require(posix_fatalBreadcrumbPath==directory+"/logs/fatal.txt","fatal report is outside the selected save root");
        require(created==test.levels,"fatal directory creation changed");
        environment[OPENQ4_ANDROID_SAVE_ROOT_ENV]="/different";
        require(Sys_DefaultSavePath()==expected,"cached save path moved during the same engine instance");
        Posix_InitFatalBreadcrumbPath();require(created.size()==test.levels.size(),"cached fatal path repeated initialization");
    }
    for(const char* invalid:{"", "relative/saves", "content://external/tree/saves"}){
        reset();environment[OPENQ4_ANDROID_SAVE_ROOT_ENV]=invalid;environment["USER_FILES"]="/legacy///";
        require(std::string(Sys_DefaultSavePath())=="/legacy/quake4","invalid explicit path bypassed the legacy host");
    }
    reset();environment["USER_FILES"]="/legacy";
    require(std::string(Sys_DefaultSavePath())=="/legacy/quake4","unset override changed legacy host behavior");
    reset();environment["USER_FILES"]="relative";
    require(std::string(Sys_DefaultSavePath())==internalStorage,"relative legacy root bypassed private storage");
    reset();require(std::string(Sys_DefaultSavePath())==internalStorage,"private-storage fallback changed");
    reset();internalStorage=nullptr;
    require(std::string(Sys_DefaultSavePath())=="/legacy-cwd","last-resort cwd fallback changed");
    std::puts("Android save paths: explicit host, legacy/private fallback, cache and fatal breadcrumb placement passed");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
'''


def source():
    header = (ROOT/'src/sys/android/android_public.h').read_text()
    constant = re.search(r'^#define OPENQ4_ANDROID_SAVE_ROOT_ENV\s+"[^"]+"', header, re.M)
    if not constant:
        raise RuntimeError('Android save-root environment contract is missing')
    posix = (ROOT/'src/sys/posix/posix_main.cpp').read_text()
    return (SUPPORT + '\n' + constant.group() + '\n'
            + function_body((ROOT/'src/sys/android/android_main.cpp').read_text(), 'const char *Sys_DefaultSavePath(')
            + function_body(posix, 'static void Posix_MkdirParents(')
            + function_body(posix, 'void Posix_InitFatalBreadcrumbPath(')
            + MAIN)


def main():
    if len(sys.argv)==3 and sys.argv[1]=='--generate-native':
        Path(sys.argv[2]).write_text(source(), encoding='utf-8')
        return 0
    compiler=next((p for name in ('clang++','g++','c++') if (p:=shutil.which(name))),None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    with tempfile.TemporaryDirectory(prefix='android-savepath-',dir=ROOT/'.tmp') as directory:
        cpp=Path(directory)/'savepath.cpp';exe=cpp.with_suffix('.exe')
        cpp.write_text(source(),encoding='utf-8')
        subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    return 0


if __name__=='__main__':
    raise SystemExit(main())
