#!/usr/bin/env python3
"""Exercise production class substitutions and campaign isolation contracts."""
from __future__ import annotations

import shutil
import re
import subprocess
import tempfile
from pathlib import Path

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <cassert>
#include <cctype>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
struct idStr {
    static int Cmp(const char* a, const char* b) { return std::strcmp(a, b); }
    static int Icmp(const char* a, const char* b) {
        while (*a && *b && std::tolower(*a) == std::tolower(*b)) { ++a; ++b; }
        return std::tolower(*a) - std::tolower(*b);
    }
};
struct idTypeInfo {
    const char* classname;
    const idTypeInfo* parent;
    bool IsType(const idTypeInfo& base) const {
        for (const idTypeInfo* p=this; p; p=p->parent) if (p==&base) return true;
        return false;
    }
};
struct idClass {
    static std::map<std::string,idTypeInfo*> classes;
    static idTypeInfo* GetClass(const char* name) {
        auto found=classes.find(name);
        return found==classes.end() ? nullptr : found->second;
    }
};
std::map<std::string,idTypeInfo*> idClass::classes;
struct FakeFileSystem {
    const char* mounted="baseoq4";
    const char* base="";
    const char* GetActiveGameDir() const { return mounted; }
} fs, *fileSystem=&fs;
// fs_awakeningActive as FileSystem Startup sets it: the expansion mounted as
// the game or as the base beneath a mod.
bool FS_AwakeningActive() { return !idStr::Icmp(fs.mounted,"q4xbase") || !idStr::Icmp(fs.base,"q4xbase"); }
struct FakeGame {
    int printed=0;
    template<typename... T> [[noreturn]] void Error(const char*,T...) { throw std::runtime_error("invalid substitution"); }
    template<typename... T> void Printf(const char*,T...) { ++printed; }
} gameLocal;
struct idClassSubstitution {
    const char* original;
    const char* replacement;
    const char* requiredGameDir;
    idClassSubstitution* next;
    static idClassSubstitution* list;
    idClassSubstitution(const char*,const char*,const char* = nullptr);
    static idTypeInfo* Resolve(idTypeInfo*);
    static void Validate();
    static bool InScope(const char*);
};
idClassSubstitution* idClassSubstitution::list=nullptr;
'''

MAIN = r'''
int main() {
    idTypeInfo stock{"rvMonsterTurret",nullptr};
    idTypeInfo expansion{"riMonsterTurret",&stock};
    idTypeInfo alternative{"AlternativeTurret",&stock};
    idTypeInfo child{"ChildTurret",&expansion};
    idTypeInfo unrelated{"Unrelated",nullptr};
    idClass::classes={{stock.classname,&stock},{expansion.classname,&expansion},
        {alternative.classname,&alternative},{child.classname,&child},{unrelated.classname,&unrelated}};
    assert(idClassSubstitution::Resolve(nullptr)==nullptr);
    assert(idClassSubstitution::Resolve(&stock)==&stock);
    {
        idClassSubstitution sub(stock.classname,expansion.classname,"q4xbase");
        for (const char* mounted : {"baseoq4","q4base","another_mod"}) {
            fs.mounted=mounted;
            assert(idClassSubstitution::Resolve(&stock)==&stock);
            gameLocal.printed=0;
            idClassSubstitution::Validate();
            assert(gameLocal.printed==0);
        }
        // A queued fs_game change cannot affect classes in the live old map:
        // production code must consult the mounted filesystem, not the CVar.
        fs.mounted="baseoq4";
        assert(idClassSubstitution::Resolve(&stock)==&stock);
        fs.mounted="Q4XBASE";
        assert(idClassSubstitution::Resolve(&stock)==&expansion);
        gameLocal.printed=0;
        idClassSubstitution::Validate();
        assert(gameLocal.printed==1);
        fs.mounted="baseoq4";
        assert(idClassSubstitution::Resolve(&stock)==&stock);
        // A mod over the Awakening (fs_game <mod>, fs_game_base q4xbase) gets
        // the expansion's classes; the same mod over Quake 4 does not.
        fs.mounted="hidef2k";
        fs.base="q4xbase";
        assert(idClassSubstitution::Resolve(&stock)==&expansion);
        gameLocal.printed=0;
        idClassSubstitution::Validate();
        assert(gameLocal.printed==1);
        fs.base="";
        assert(idClassSubstitution::Resolve(&stock)==&stock);
        fs.mounted="baseoq4";
    }
    idClassSubstitution::list=nullptr;
    {
        idClassSubstitution awake(stock.classname,expansion.classname,"q4xbase");
        idClassSubstitution other(stock.classname,alternative.classname,"another_mod");
        idClassSubstitution::Validate();
        fs.mounted="another_mod";
        assert(idClassSubstitution::Resolve(&stock)==&alternative);
        fs.mounted="q4xbase";
        assert(idClassSubstitution::Resolve(&stock)==&expansion);
        idClassSubstitution chain(expansion.classname,child.classname,"q4xbase");
        idClassSubstitution::Validate();
        assert(idClassSubstitution::Resolve(&stock)==&child);
    }
    auto invalid=[&](const char* from,const char* to,const char* scope,bool duplicate=false) {
        idClassSubstitution::list=nullptr;
        idClassSubstitution first(from,to,scope);
        idClassSubstitution second(stock.classname,expansion.classname,duplicate ? "Q4XBASE" : "other");
        if (!duplicate) idClassSubstitution::list=&first;
        try { idClassSubstitution::Validate(); } catch (const std::runtime_error&) { return; }
        assert(false && "invalid substitution was accepted");
    };
    invalid(stock.classname,stock.classname,"q4xbase");
    invalid("missing",expansion.classname,"q4xbase");
    invalid(stock.classname,"missing","q4xbase");
    invalid(stock.classname,unrelated.classname,"q4xbase");
    invalid(stock.classname,expansion.classname,"q4xbase",true);
    invalid(stock.classname,expansion.classname,nullptr,true);
    idClassSubstitution::list=nullptr;
    {
        idClassSubstitution global(stock.classname,expansion.classname);
        fs.mounted="baseoq4";
        idClassSubstitution::Validate();
        assert(idClassSubstitution::Resolve(&stock)==&expansion);
    }
}
'''


def validate_isolation() -> None:
    fs = (ROOT / 'src/framework/FileSystem.cpp').read_text()
    probe = function_body(fs, 'idCampaignContentInfo idFileSystemLocal::GetAwakeningContentInfo(')
    probe = re.sub(r'//[^\n]*|/\*.*?\*/', '', probe, flags=re.S)
    for forbidden in ('AddGameDirectory(', 'SetupGameDirectories(', 'searchPaths ='):
        assert forbidden not in probe, 'discovery must not mount campaign content'
    startup = function_body(fs, 'void idFileSystemLocal::Startup(')
    assert startup.index('!awakeningAllowed') < startup.index('SetupGameDirectories( BASE_GAMEDIR )')
    assert 'const bool awakeningAllowed = false;' in startup
    dll = function_body(fs, 'void idFileSystemLocal::FindDLL(')
    assert 'q4xbase' in dll and 'OPENQ4_GAMEDIR' in dll
    # OpenFileReadFlags delegates to the search shared with length-only queries
    flags_entry = function_body(fs, 'idFile *idFileSystemLocal::OpenFileReadFlags(')
    assert 'return OpenFileReadSearch(' in flags_entry
    open_file = function_body(fs, 'idFile *idFileSystemLocal::OpenFileReadSearch(')
    for name in ('guis/menu/', 'guis/mainmenu.gui', 'guis/arena_menu.gui', 'guis/campaign_menu.gui', 'default.cfg'):
        assert name in open_file, f'campaign content can override engine navigation: {name}'
    assert 'protectCampaignNavigation = fs_awakeningActive.GetBool()' in open_file
    # the expansion mounts beneath a single-player mod as well as on its own,
    # and every module reads the mounted state from the one read-only CVar
    assert "fs_awakeningActive.SetBool(" in startup
    assert startup.index('!awakeningAllowed') < startup.index('modInfo.campaign')
    header = (ROOT / 'src/framework/FileSystem.h').read_text(encoding='latin-1')
    assert 'GetCVarBool( "fs_awakeningActive" )' in function_body(header, 'ID_INLINE bool FS_AwakeningActive(')
    for tree in ('game','mpgame'):
        source = (ROOT / f'src/{tree}/gamesys/Class.cpp').read_text()
        resolve = function_body(source, 'idTypeInfo *idClassSubstitution::Resolve(')
        scope = function_body(source, 'bool idClassSubstitution::InScope(')
        assert 'InScope( sub->requiredGameDir )' in resolve and 'GetCVarString' not in resolve
        assert 'GetActiveGameDir()' in scope and 'FS_AwakeningActive()' in scope and 'GetCVarString' not in scope
    turret = (ROOT / 'src/game/awakening/ai/Monster_Turret.cpp').read_text()
    assert 'SPAWNCLASS_SUBSTITUTION_FOR_GAME( "q4xbase", rvMonsterTurret, riMonsterTurret )' in turret
    assert not (ROOT / 'src/mpgame/awakening').exists()


def main() -> None:
    validate_isolation()
    compiler = next((path for name in ('clang++','g++','c++') if (path:=shutil.which(name))),None)
    if not compiler:
        raise RuntimeError('C++ compiler required for class-scope regression checks')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='campaign-scope-',dir=ROOT / '.tmp') as directory:
        for tree in ('game','mpgame'):
            source = (ROOT / f'src/{tree}/gamesys/Class.cpp').read_text()
            bodies = '\n'.join(function_body(source,signature) for signature in (
                'idClassSubstitution::idClassSubstitution(',
                'bool idClassSubstitution::InScope(',
                'idTypeInfo *idClassSubstitution::Resolve(',
                'void idClassSubstitution::Validate('))
            cpp = Path(directory) / f'{tree}.cpp'
            binary = cpp.with_suffix('.exe')
            cpp.write_text(SUPPORT + bodies + MAIN,encoding='utf-8')
            subprocess.run([compiler,'-std=c++17','-Wall','-Wextra',str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=30)
    print('campaign_scope: production class substitutions and content isolation passed')


if __name__ == '__main__':
    main()
