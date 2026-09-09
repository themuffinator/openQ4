#!/usr/bin/env python3
"""Exercise the production declaration precache warning for generated font pages.

The actual guard and warning branch are extracted. Atlas/material creation is
not reimplemented, and no renderer, window, input or installed asset is loaded.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cctype>
#include <cstdio>
#include <string>
struct idStr : std::string {
    using std::string::string;
    int Icmpn(const char* other,int length) const {
        for(int i=0;i<length;++i){
            const int a=i<static_cast<int>(size())?std::tolower(static_cast<unsigned char>((*this)[i])):0;
            const int b=std::tolower(static_cast<unsigned char>(other[i]));
            if(a!=b)return a-b;if(!a)return 0;
        }return 0;
    }
    int Icmp(const char* other) const{return Icmpn(other,static_cast<int>(size())+1);}
};
enum { DECL_MATERIAL,DECL_ENTITYDEF,DECL_SKIN };
static bool initialized=true,insideLoad=false,tool=false;
static int warnings=0;
static struct {
    bool IsInitialized(){return initialized;}
    void Warning(const char*,...){++warnings;}
} commonObject,*common=&commonObject;
static struct {
    bool GetInsideLoad(){return insideLoad;}
    const char* GetDeclNameFromType(int){return "test";}
} declManagerLocal;
static bool openQ4_IsAnyToolActive(){return tool;}
struct Decl {
    int type=DECL_MATERIAL;
    bool generatedDefaultText=true,implicit=true;
    idStr name;
    bool IsImplicit(){return implicit;}
    void ParseWarning(){
        // PRODUCTION_GUARD
    }
};
int main(){
    for(const char* name:{"_retained/_ttfatlasx_fonts_english_marine_48","_RETAINED/_TTFATLASX_FONTS_FRENCH_MARINE_96",
        "_retained/_ttfatlas_fonts_english_marine_24","_retainedSolid","_retainedLayer/17","_retainedMask/5"}){
        for(int type:{DECL_MATERIAL,DECL_ENTITYDEF,DECL_SKIN})for(bool generated:{false,true})for(bool implicit:{false,true}){
            Decl decl;decl.name=name;decl.type=type;decl.generatedDefaultText=generated;decl.implicit=implicit;
            warnings=0;decl.ParseWarning();
            assert(warnings==((type==DECL_MATERIAL && generated && implicit)?0:1));
        }
    }
    for(const char* name:{"_retained/fonts/marine.tga","guis/assets/fontpage","textures/missing",
        "_retained/_ttfatlasxyz_fonts_marine_48","_retained/_ttfatlasxfonts_marine_48",
        "prefix_retained/_ttfatlasx_fonts_marine_48","_ttfatlasx_fonts_marine_48","_retainedSolidOther"}){
        Decl decl;decl.name=name;warnings=0;decl.ParseWarning();assert(warnings==1);
    }
    Decl ordinary;ordinary.name="textures/ordinary";
    initialized=false;warnings=0;ordinary.ParseWarning();assert(!warnings);initialized=true;
    insideLoad=true;ordinary.ParseWarning();assert(!warnings);insideLoad=false;
    tool=true;ordinary.ParseWarning();assert(!warnings);tool=false;
    ordinary.ParseWarning();assert(warnings==1);
    std::puts("Generated retained font pages: real declaration warning guard recognizes fixed/scalable atlas namespaces; explicit/ordinary/other-type assets still warn passed");
}
'''


def main():
    source = (ROOT/'src/framework/DeclManager.cpp').read_text(encoding='utf-8')
    guard = re.search(r'\tconst bool generatedRetainedResource = [\s\S]*?;',source)
    assert guard
    branch = function_body(source,'if ( common->IsInitialized() && !declManagerLocal.GetInsideLoad() && !openQ4_IsAnyToolActive() && !generatedRetainedResource )')
    fonts = (ROOT/'src/renderer/tr_fontTTF.cpp').read_text(encoding='utf-8')
    for prefix in ('_ttfatlas_','_ttfatlasx_'):
        assert f'va( "{prefix}%s_%i"' in fonts
        assert '"_retained/'+prefix+'"' in guard.group()
    compiler = next((found for name in ('clang++','g++','c++') if (found := shutil.which(name))),None)
    if not compiler: raise RuntimeError('C++ compiler required')
    with tempfile.TemporaryDirectory(prefix='font-precache-',dir=ROOT/'.tmp') as temp:
        source = Path(temp)/'guard.cpp'
        source.write_text(SUPPORT.replace('// PRODUCTION_GUARD',guard.group()+'\n'+branch),encoding='utf-8')
        binary = Path(temp)/'guard.exe';env=dict(os.environ,TEMP=temp,TMP=temp)
        subprocess.run([compiler,'-std=c++17',str(source),'-o',str(binary)],check=True,env=env)
        subprocess.run([str(binary)],check=True,env=env)


if __name__ == '__main__':
    main()
