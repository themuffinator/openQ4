#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Execute stock-map neutral flag preparation and the live entity filter."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_vote_electorate_contract import function

ROOT = Path(__file__).resolve().parents[2]
STUBS = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#undef INFINITY
#define BIT(x) (1 << (x))
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
template<class T> T Max(T a,T b) { return std::max(a,b); }
struct idVec3 {
    float x=0,y=0,z=0;
    idVec3 operator-(const idVec3& b) const { return {x-b.x,y-b.y,z-b.z}; }
    float LengthSqr() const { return x*x+y*y+z*z; }
};
struct idMath { static constexpr float INFINITY=FLT_MAX; };
struct idStr : std::string {
    using std::string::string; using std::string::operator=;
    operator const char*() const { return c_str(); }
    static int Icmp(const char* a,const char* b) {
        while (*a && *b && std::tolower((unsigned char)*a)==std::tolower((unsigned char)*b)) { ++a; ++b; }
        return std::tolower((unsigned char)*a)-std::tolower((unsigned char)*b);
    }
    int Icmp(const char* b) const { return Icmp(c_str(),b); }
    static int FindText(const char* a,const char* b) { auto p=std::string(a).find(b); return p==std::string::npos?-1:int(p); }
};
template<class... A> const char* va(const char* format,A... args) {
    static char text[1024]; std::snprintf(text,sizeof(text),format,args...); return text;
}
struct idKeyValue { std::string key; const char* GetKey() const { return key.c_str(); } };
struct idDict {
    std::map<std::string,std::string> values;
    mutable idKeyValue found;
    void Set(const char* k,const char* v) { values[k]=v; }
    void SetBool(const char* k,bool v) { Set(k,v?"1":"0"); }
    const char* GetString(const char* k,const char* fallback="") const {
        auto i=values.find(k); return i==values.end()?fallback:i->second.c_str();
    }
    bool GetString(const char* k,const char* fallback,const char** out) const {
        *out=GetString(k,fallback); return values.count(k)!=0;
    }
    int GetInt(const char* k,const char* fallback="0") const { return std::atoi(GetString(k,fallback)); }
    bool GetBool(const char* k,const char* fallback="0") const { return GetInt(k,fallback)!=0; }
    bool GetBool(const char* k,const char* fallback,bool& out) const { out=GetBool(k,fallback); return values.count(k)!=0; }
    bool GetVector(const char* k,const char* fallback,idVec3& out) const {
        std::sscanf(GetString(k,fallback),"%f %f %f",&out.x,&out.y,&out.z); return values.count(k)!=0;
    }
    void SetDefaults(const idDict* d) { for (auto& p:d->values) values.emplace(p); }
    const idKeyValue* FindKey(const char* k) const {
        if (!values.count(k)) return nullptr; found.key=k; return &found;
    }
    bool MatchPrefix(const char* prefix) const { for (auto& p:values) if (p.first.find(prefix)==0) return true; return false; }
};
struct idMapEntity { idDict epairs; };
struct idMapFile {
    std::vector<idMapEntity*> entities;
    idMapFile() { entities.push_back(new idMapEntity); }
    ~idMapFile() { for (auto* e:entities) delete e; }
    int GetNumEntities() const { return int(entities.size()); }
    idMapEntity* GetEntity(int i) { return entities.at(i); }
    void AddEntity(idMapEntity* e) { entities.push_back(e); }
};
struct idDeclEntityDef { idDict dict; };
#include "game/mp/GameTypes.h"
enum { MAX_CTF_FLAGS=TEAM_MAX+1 };
struct CVar { int GetInteger() const { return 1; } } g_skill;
struct idGameLocal {
    bool isMultiplayer=true;
    int gameType=GAME_CTF;
    idDict serverInfo;
    std::map<std::string,idDeclEntityDef> defs;
    struct MP { bool IsBuyingAllowedInTheCurrentGameMode() const { return false; }
        bool PickupInhibited(const char*) const { return false; } } mpGame;
    const idDeclEntityDef* FindEntityDef(const char* n,bool) const {
        auto i=defs.find(n); return i==defs.end()?nullptr:&i->second;
    }
    bool InhibitEntitySpawn(idDict& args);
    template<class... A> void Warning(const char*,A...) {}
} gameLocal;
// convoy2b's campaign-only actor suppression (a developer cvar, off by default)
static bool OpenQ4_ShouldSuppressConvoy2bActorSpawn(const idDict&) { return false; }
struct idEntity {};
struct idMultiplayerGame {
    idEntity* flagEntities[MAX_CTF_FLAGS] = {};
    void SetFlagEntity(idEntity*,int);
    idEntity* GetFlagEntity(int);
};
'''

TESTS = r'''
idMapEntity* Add(idMapFile& map,const char* classname,const char* origin,const char* name="") {
    auto* e=new idMapEntity; e->epairs.Set("classname",classname); e->epairs.Set("origin",origin);
    e->epairs.Set("name",name); map.AddEntity(e); return e;
}
int main() {
    for (int team=0;team<=TEAM_MAX;++team) {
        auto& d=gameLocal.defs[team==0?"base_marine":team==1?"base_strogg":"mp_ctf_one_flag"].dict;
        d.Set("spawnclass","rvItemCTFFlag"); d.Set("team",va("%d",team));
    }
    for (bool authored : {false,true}) {
        idMapFile map;
        Add(map,"base_marine","-1000 0 0"); Add(map,"base_strogg","1000 0 0");
        Add(map,"item_health_mega","0 100 16");
        Add(map,"weapon_rocketlauncher","-800 0 16");
        Add(map,"powerup_regeneration","0 0 16")->epairs.Set("team","marine");
        Add(map,"item_armor_large","0 0 16")->epairs.SetBool("not_multiplayer",true);
        Add(map,"func_static","0 0 0","openq4_neutral_flag");
        if (authored) Add(map,"mp_ctf_one_flag","0 500 16","mapper_flag");
        const int count=map.GetNumEntities();
        AddMissingNeutralFlag(&map);
        CHECK(map.GetNumEntities()==count+int(!authored));
        if (!authored) {
            const auto& d=map.GetEntity(count)->epairs;
            CHECK(std::string(d.GetString("origin"))=="0 100 16");
            CHECK(std::string(d.GetString("name"))=="openq4_neutral_flag_1");
            CHECK(std::string(d.GetString("classname"))=="mp_ctf_one_flag" && d.GetBool("nodrop"));
        }
        AddMissingNeutralFlag(&map); // Same-map reload cannot duplicate the flag.
        CHECK(map.GetNumEntities()==count+int(!authored));
    }
    for (int bases=0;bases<=2;++bases) {
        idMapFile map;
        if (bases>=1) Add(map,"base_marine","-1000 0 0");
        if (bases>=2) Add(map,"base_strogg","1000 0 0");
        AddMissingNeutralFlag(&map); // No suitable pickup must not invent a position.
        CHECK(map.GetNumEntities()==bases+1);
    }
    for (int mode=GAME_DM;mode<NUM_GAME_TYPES;++mode) {
        if (!MPGameTypeIsSelectable(mode)) continue;
        gameLocal.gameType=mode;
        gameLocal.serverInfo.Set("si_gameType",MPGameType(mode)->name);
        gameLocal.serverInfo.Set("si_entityFilter",MPGameType(mode)->entityFilter);
        idDict neutral;
        neutral.SetBool("filter_One Flag CTF",true); neutral.SetBool("filter_Arena One Flag CTF",true);
        CHECK(gameLocal.InhibitEntitySpawn(neutral)==!MPGameTypeHasAny(mode,GTF_ONEFLAG));
        idDict shared;
        shared.SetBool("filter_CTF",true); shared.SetBool("filter_Arena CTF",true);
        if (MPGameTypeHasAny(mode,GTF_ONEFLAG)) {
            CHECK(!gameLocal.InhibitEntitySpawn(shared));
            shared.SetBool(va("filter_%s",MPGameType(mode)->name),false);
            CHECK(gameLocal.InhibitEntitySpawn(shared)); // Explicit exclusion beats fallback.
            gameLocal.serverInfo.Set("si_entityFilter","custom");
            CHECK(gameLocal.InhibitEntitySpawn(neutral)); // Keep explicit operator filters.
        }
    }
    idMultiplayerGame mp; idEntity entities[MAX_CTF_FLAGS];
    for (int i=-2;i<MAX_CTF_FLAGS+2;++i) {
        const bool valid=i>=0 && i<MAX_CTF_FLAGS;
        mp.SetFlagEntity(valid?&entities[i]:&entities[0],i);
        CHECK(mp.GetFlagEntity(i)==(valid?&entities[i]:nullptr));
    }
    for (int i=0;i<MAX_CTF_FLAGS;++i) CHECK(mp.GetFlagEntity(i)==&entities[i]);
    std::puts("neutral placement, authored precedence, filtering, repeat loads and flag cache bounds: PASS");
}
'''


def main() -> None:
    local = (ROOT / "src/game/Game_local.cpp").read_text(encoding="utf-8")
    mp = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    types = (ROOT / "src/game/mp/GameTypes.cpp").read_text(encoding="utf-8")
    begin = types.index("static const mpGameTypeInfo_t mpGameTypeInfoTable[]")
    end = types.index("\n/*", types.index("const int mpNumGameTypeInfo", begin))
    definitions = types[begin:end] + "\n".join(function(types, signature) for signature in (
        "const mpGameTypeInfo_t *MPGameType(", "bool MPGameTypeIsSelectable(", "bool MPGameTypeHasAny(",
    ))
    bodies = function(local, "static void AddMissingNeutralFlag(") + function(local, "bool idGameLocal::InhibitEntitySpawn(")
    bodies += function(mp, "void idMultiplayerGame::SetFlagEntity(") + function(mp, "idEntity* idMultiplayerGame::GetFlagEntity(")
    load = function(local, "void idGameLocal::LoadMap(")
    assert load.index("ApplyEntityStringFiles()") < load.index("AddMissingNeutralFlag( mapFile )") < load.index("numClients = 0")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for flag-map regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="flag-map-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        cpp, exe = path / "flags.cpp", path / ("flags.exe" if os.name == "nt" else "flags")
        cpp.write_text(STUBS + definitions + bodies + TESTS, encoding="utf-8")
        command = [compiler, "-std=c++17", f"-I{ROOT / 'src'}", str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print("mp_match_flag_map_contract: PASS")


if __name__ == "__main__":
    main()
