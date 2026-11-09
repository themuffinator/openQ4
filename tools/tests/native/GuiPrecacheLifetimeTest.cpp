// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
static int checks = 0, live = 0;
static void check(bool ok, const char* why) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL %s\n",why); std::exit(1); } }
struct idStr {
    static int Icmp(const char* a, const char* b) { return std::strcmp(a,b); }
    static int Icmpn(const char* a, const char* b, int n) { return std::strncmp(a,b,n); }
};
template<class T> struct List : std::vector<T> { int Num() const { return int(this->size()); } };
struct idUserInterface;
struct idUserInterfaceManagerLocal {
    List<idUserInterface*> guis;
    idUserInterface* FindGui(const char*, bool, bool=false, bool=false);
};
static idUserInterfaceManagerLocal manager;
static auto* uiManager = &manager;
struct idUserInterface {
    std::string path;
    bool interactive=true, unique=false;
    int refs=1;
    idUserInterface() { ++live; }
    ~idUserInterface() { --live; }
    const char* GetSourceFile() const { return path.c_str(); }
    bool IsInteractive() const { return interactive; }
    bool IsUniqued() const { return unique; }
    void SetUniqued(bool value) { unique=value; }
    void AddRef() { ++refs; }
    bool InitFromFile(const char* name) {
        if (!std::strcmp(name,"missing.gui")) return false;
        path=name; manager.guis.push_back(this); return true;
    }
};
static idUserInterface* UI_CreateForPath(const char*) { return new idUserInterface; }
static void openQ4_UpdateInitialization() {}
// @LOOKUP@
struct Game { bool isMultiplayer=true; void FindEntityDef(const char*,bool) {} };
static Game gameLocal;
struct Decls { void FindSound(const char*,bool) {} void MediaPrint(const char*,...) {} };
static Decls decls;
static auto* declManager = &decls;
struct idPlayer { static void PrecacheMultiplayerSpawn() {} };
struct idMultiplayerGame {
    static constexpr int AS_NUM_SOUNDS=2;
    static constexpr const char* announcerSoundDefs[2] = {"one","two"};
    static constexpr const char* MPGuis[4] = {"main.gui","net.gui","hud.gui",nullptr};
    void Precache();
};
// @PRECACHE@
struct KeyValue {
    std::string key,value;
    const char* GetKey() const { return key.c_str(); }
    const std::string& GetValue() const { return value; }
};
#define TIME_THIS_SCOPE(...)
#define MEM_SCOPED_TAG(...)
static bool RememberMapMediaPrecache(const char*,const std::string&) { return false; }
static double OpenQ4_BeginMapMediaPrecacheCall() { return 0; }
static constexpr int MAP_MEDIA_GUI=0;
static void OpenQ4_EndMapMediaPrecacheCall(int,double) {}
static void PrecacheDictionary(const KeyValue* kv) {
    for (int once=0;once<1;++once)
    // @DICTIONARY@
}
int main() {
    idMultiplayerGame mp;
    gameLocal.isMultiplayer=false; mp.Precache(); check(live==0,"SP skips MP-only precache");
    gameLocal.isMultiplayer=true;
    for (int map=0;map<32;++map) {
        mp.Precache();
        check(live==3,"MP precache must reuse interactive templates across maps");
        check(manager.guis[0]->refs==map+1,"precache touches the surviving menu");
    }
    const KeyValue gui{"gui_surface","terminal.gui"};
    for (int map=0;map<32;++map) {
        PrecacheDictionary(&gui);
        check(live==4,"dictionary precache must reuse interactive templates across maps");
    }
    for (const char* flag : {"gui_noninteractive","gui_parm1","gui_inventory"}) {
        const KeyValue kv{flag,"ignored.gui"}; PrecacheDictionary(&kv);
        check(live==4,"GUI flags are not media paths");
    }
    auto* first=manager.FindGui("terminal.gui",true,true);
    auto* second=manager.FindGui("terminal.gui",true,true);
    check(first!=second && first!=manager.guis[3],"gameplay unique instances remain isolated");
    check(first->IsUniqued() && second->IsUniqued(),"unique identity survives precaching");
    first->interactive=false;
    check(manager.FindGui("terminal.gui",true)!=first,"noninteractive unique instance stays private");
    auto* privateGui=manager.FindGui("private.gui",true,true);
    privateGui->interactive=false;
    check(manager.FindGui("private.gui",true)!=privateGui,"uniqued flag alone prevents ordinary reuse");
    check(manager.FindGui("private.gui",true,false,true)==privateGui && privateGui->IsUniqued(),
          "precache may touch an existing private GUI without changing its identity");
    check(manager.FindGui("private.gui",true,true)!=privateGui,"subsequent entity request remains isolated");
    const int before=live;
    check(!manager.FindGui("absent.gui",false) && live==before,"lookup-only does not allocate");
    check(!manager.FindGui("missing.gui",true) && live==before,"failed load releases candidate");
    check(!manager.FindGui(nullptr,true) && !manager.FindGui("",true),"empty paths allocate nothing");
    for (auto* guiOwner : manager.guis) delete guiOwner;
    manager.guis.clear();
    check(live==0,"all GUI owners retire at teardown");
    std::printf("PASS %d GUI precache ownership checks\n",checks);
}
