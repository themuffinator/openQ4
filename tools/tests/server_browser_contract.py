#!/usr/bin/env python3
"""Execute the production server scanner against bounded network/UI/file doubles."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from async_drop_client_contract import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
class idStr {
public:
    std::string value;
    idStr() = default;
    idStr(const char *s): value(s ? s : "") {}
    idStr(const std::string &s): value(s) {}
    const char *c_str() const { return value.c_str(); }
    int Length() const { return static_cast<int>(value.size()); }
    void Clear() { value.clear(); }
    void ToLower() { for(char &c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    bool IsEmpty() const { return value.empty(); }
    idStr &operator+=(char s) { value+=s; return *this; }
    idStr &operator+=(const char *s) { value+=s; return *this; }
    idStr &operator+=(const idStr &s) { value+=s.value; return *this; }
    bool operator==(const idStr &s) const { return value==s.value; }
    bool operator<(const idStr &s) const { return value<s.value; }
    static int Icmp(const char *a,const char *b) {
        std::string x=a,y=b;
        for(char &c:x)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for(char &c:y)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return x.compare(y);
    }
    int Icmp(const char *s) const { return Icmp(c_str(),s); }
    int Icmp(const idStr &s) const { return Icmp(c_str(),s.c_str()); }
    operator const char *() const { return c_str(); }
    char operator[](int n) const { return value[n]; }
};
const char *va(const char *format,...) {
    static thread_local char buffers[16][8192]; static thread_local unsigned which=0;
    char *out=buffers[which++%16]; va_list ap; va_start(ap,format);
    std::vsnprintf(out,8192,format,ap); va_end(ap); return out;
}
template<class T> T Min(T a,T b){return std::min(a,b);}
template<class T> T Max(T a,T b){return std::max(a,b);}
struct idMath {static int ClampInt(int lo,int hi,int v){return std::clamp(v,lo,hi);}};
template<class T> class idList {
public:
    std::vector<T> data;
    int Num() const {return static_cast<int>(data.size());}
    void Clear(){data.clear();}
    int Append(const T &t){data.push_back(t);return Num()-1;}
    int FindIndex(const T &t) const {auto i=std::find(data.begin(),data.end(),t);return i==data.end()?-1:static_cast<int>(i-data.begin());}
    int AddUnique(const T &t){int i=FindIndex(t);return i<0?Append(t):i;}
    void RemoveIndex(int i){data.erase(data.begin()+i);}
    T &operator[](int i){assert(i>=0 && i<Num());return data[i];}
    const T &operator[](int i) const {assert(i>=0 && i<Num());return data[i];}
    void Sort(int (*cmp)(const T*,const T*)){std::sort(data.begin(),data.end(),[&](const T &a,const T &b){return cmp(&a,&b)<0;});}
    void Sort(){std::sort(data.begin(),data.end());}
};
using idStrList=idList<idStr>;
struct idKeyValue {
    idStr key,value;
    const idStr &GetValue() const {return value;}
    const idStr &GetKey() const {return key;}
};
struct idDict {
    std::vector<idKeyValue> data;
    const idKeyValue *FindKey(const char *key) const {for(const auto &p:data)if(!p.key.Icmp(key))return &p;return nullptr;}
    const char *GetString(const char *key,const char *fallback="") const {auto p=FindKey(key);return p?p->value.c_str():fallback;}
    void GetString(const char *key,const char *fallback,idStr &out) const {out=GetString(key,fallback);}
    int GetInt(const char *key,const char *fallback="0") const {return std::atoi(GetString(key,fallback));}
    bool GetBool(const char *key) const {return GetInt(key)!=0;}
    void Set(const char *key,const char *value){for(auto &p:data)if(!p.key.Icmp(key)){p.value=value;return;}data.push_back({key,value});}
    void SetInt(const char *key,int v){Set(key,std::to_string(v).c_str());}
    void Delete(const char *key){data.erase(std::remove_if(data.begin(),data.end(),[&](const idKeyValue &p){return !p.key.Icmp(key);}),data.end());}
    void Clear(){data.clear();}
    void Print()const{}
    int GetNumKeyVals() const {return static_cast<int>(data.size());}
    const idKeyValue *GetKeyVal(int i) const {return &data[i];}
};
enum {CVAR_GUI=1,CVAR_INTEGER=2,CVAR_ARCHIVE=4};
struct idCVar {
    std::string value;
    idCVar(const char*,const char *initial,int,const char*):value(initial){}
    int GetInteger() const {return std::atoi(value.c_str());}
    const char *GetString() const {return value.c_str();}
    void SetString(const char *v){value=v;}
};
enum {NA_IP,NA_IP6,NA_LOOPBACK,NA_BROADCAST};
constexpr int MAX_NICKLEN=32,MAX_ASYNC_CLIENTS=32,MAX_STRING_CHARS=1024,PORT_SERVER=28004,DECL_MAPDEF=1;
struct netadr_t {std::string host;int port=0,type=NA_IP;};
bool Sys_StringToNetAdr(const char *s,netadr_t *out,bool) {
    std::string value=s;
    if(value.empty() || value.find_first_of(" \t\r\n\";\\")!=std::string::npos)return false;
    auto colon=value.rfind(':');
    if(colon==std::string::npos)return false;
    std::string port=value.substr(colon+1);
    if(port.empty() || port.find_first_not_of("0123456789")!=std::string::npos)return false;
    out->port=std::atoi(port.c_str());out->host=value.substr(0,colon);out->type=value[0]=='['?NA_IP6:NA_IP;
    return out->port>0 && out->port<65536 && out->host.find_first_not_of("0123456789abcdefABCDEF.:[]%") == std::string::npos;
}
const char *Sys_NetAdrToString(const netadr_t &adr){return va("%s:%d",adr.host.c_str(),adr.port);}
bool Sys_CompareNetAdrBase(const netadr_t &a,const netadr_t &b){return a.host==b.host;}
int clockValue=100;
int Sys_Milliseconds(){return clockValue;}
struct idUserInterface {
    idDict state;
    const idDict &State()const{return state;}
    void SetStateString(const char *s,const char *v){state.Set(s,v);}
    void SetStateInt(const char *s,int v){state.SetInt(s,v);}
    void SetStateBool(const char *s,bool v){state.SetInt(s,v);}
    void DeleteStateVar(const char *s){state.Delete(s);}
    void StateChanged(int){}
};
// PRODUCTION_LIST_GUI_INTERFACE
struct idListGUILocal:public idListGUI {
    idUserInterface *gui=nullptr; std::string name; std::vector<std::pair<int,idStr>> rows;
    void Config(idUserInterface *g,const char *n){gui=g;name=n?n:"";}
    bool IsConfigured()const{return gui!=nullptr;}
    int Num(){return static_cast<int>(rows.size());}
    bool Del(int id){auto i=std::find_if(rows.begin(),rows.end(),[&](const auto &r){return r.first==id;});if(i==rows.end())return false;rows.erase(i);return true;}
    void Clear(){rows.clear();}
    void SetStateChanges(bool){}
    void Add(int id,const idStr &s){rows.push_back({id,s});}
    int GetNumSelections(){return gui->State().GetInt((name+"_numsel").c_str());}
    void Shutdown(){gui=nullptr;rows.clear();}
    void SetSelection(int row){gui->SetStateInt((name+"_sel_0").c_str(),row);}
    int GetSelection(char*,int,int=0)const {
        assert(gui);int row=gui->State().GetInt((name+"_sel_0").c_str(),"-1");
        assert(row>=-1); // Mirrors the inherited list's dangerous negative-index boundary.
        if(row<0 || row>=static_cast<int>(rows.size()))return -1;
        gui->SetStateInt((name+"_selid_0").c_str(),rows[row].first);return rows[row].first;
    }
};
struct UIManager {idListGUILocal *last=nullptr;idListGUI *AllocListGUI(){return last=new idListGUILocal;}void FreeListGUI(idListGUI *p){delete static_cast<idListGUILocal*>(p);}} uiValue,*uiManager=&uiValue;
struct Language {
    const char *GetString(const char *token){
        if(!std::strcmp(token,"#str_42861"))return "Showing %d out of %d servers";
        if(!std::strcmp(token,"#str_107298"))return "Scanning - Found: %s";
        return token;
    }
} language;
struct Common {
    Language *GetLanguageDict(){return &language;}
    int GetPresentationTime(){return clockValue;}
    void Printf(const char*,...){}void DPrintf(const char*,...){}void Warning(const char*,...){}
} commonValue,*common=&commonValue;
struct idDecl{};
struct idDeclEntityDef:idDecl{idDict dict;};
struct Decls {const idDecl *FindType(int,const char*,bool){return nullptr;}} declValue,*declManager=&declValue;
struct Files {
    std::map<std::string,std::string> files;
    bool failWrite=false,failPromote=false;std::string screenshot;
    int ReadFile(const char *path,void **buffer){
        auto f=files.find(path);if(f==files.end()){if(buffer)*buffer=nullptr;return -1;}
        if(buffer){*buffer=std::malloc(f->second.size()+1);std::memcpy(*buffer,f->second.c_str(),f->second.size()+1);}
        return static_cast<int>(f->second.size());
    }
    void FreeFile(void *p){std::free(p);}
    int WriteFile(const char *path,const void *data,int length){if(failWrite)return -1;files[path]=std::string(static_cast<const char*>(data),length);return length;}
    bool PromoteFile(const char *pending,const char *final){if(failPromote)return false;files[final]=files[pending];files.erase(pending);return true;}
    void RemoveFile(const char *path){files.erase(path);}
    void FindMapScreenshot(const char *map,char *out,int length){screenshot=map;std::snprintf(out,length,"shot/%s",map);}
} fileValue,*fileSystem=&fileValue;
struct NetworkClient {
    std::vector<std::string> queries;
    bool IsPortInitialized(){return true;}void InitPort(){}
    void GetServerInfo(netadr_t &adr){queries.push_back(Sys_NetAdrToString(adr));}
};
struct idAsyncNetwork{static NetworkClient client;};
NetworkClient idAsyncNetwork::client;
'''

PACKET_SUPPORT = r'''
constexpr int ASYNC_PROTOCOL_VERSION=104;
constexpr int ASYNC_PROTOCOL_MAJOR=0,ASYNC_PROTOCOL_MINOR=104;
struct CvarSystem {bool GetCVarBool(const char*){return false;}} cvars,*cvarSystem=&cvars;
struct idBitMsg {
    int challenge=0,limit=5,protocol=ASYNC_PROTOCOL_VERSION;
    mutable int reads=0;
    int ReadLong()const{++reads;return reads==1?challenge:reads==2?protocol:0;}
    int ReadShort()const{++reads;return 0;}
    int ReadByte()const{++reads;return MAX_ASYNC_CLIENTS;}
    void ReadString(char *out,int)const{++reads;out[0]=0;}
    void ReadDeltaDict(idDict &dict,void*)const{
        ++reads;dict.Set("si_name","Packet Arena");dict.Set("si_map","mp/q4dm1");dict.Set("si_maxPlayers","4");
    }
    bool IsReadOverflowed()const{return reads>limit;}
};
struct idAsyncClient {
    idServerScan serverList;
    void ProcessInfoResponseMessage(const netadr_t,const idBitMsg&);
};
'''

MAIN = r'''
static networkServer_t server(const char *host,int ping,const char *name,int clients=0){
    networkServer_t s{};s.adr={host,PORT_SERVER,NA_IP};s.ping=ping;s.clients=clients;s.OSMask=0;
    s.serverInfo.Set("si_name",name);s.serverInfo.Set("si_map","mp/q4dm1");
    s.serverInfo.Set("si_gameType","DM");s.serverInfo.Set("si_maxPlayers","4");
    s.serverInfo.Set("fs_game","baseoq4");return s;
}
int main(){
    assert(ServerBrowserText("^2Real\tname\n^imaterial") == idStr("Real name imaterial"));
    assert(ServerBrowserText(std::string(200,'x').c_str()).Length()==96);
    assert(ServerBrowserText((std::string(95,'x')+"\xc3\xa9").c_str()).Length()==95);
    assert(ServerBrowserText((std::string(63,'x')+"\xf0\x9f\x8e\xaf").c_str(),64).Length()==63);
    assert(ServerBrowserText("\xed\xa0\x80") == idStr("???"));
    assert(ServerBrowserMod("MiXeD") == idStr("mixed"));
    assert(ServerBrowserMod("mod\twith\ncontrols").IsEmpty());
    assert(ServerBrowserMod(std::string(65,'m').c_str()).IsEmpty());
    assert(ServerBrowserMapPath("mp/q4dm1"));
    for(const char *path:{"../secret","mp/../secret","/absolute","mp/a;quit","C:/file","mp/q4dm1.tga","mp/a\\b"})assert(!ServerBrowserMapPath(path));
    idStr address;
    {idServerScan headless;headless.SetupLANScan();auto s=server("10.0.0.1",5,"Headless");s.challenge=headless.GetChallenge();assert(headless.InfoResponse(s)==0);assert(!headless.GetSelectedAddress(address));headless.Clear();headless.Shutdown();}
    idServerScan scan;idUserInterface gui;scan.GUIConfig(&gui,"serverList");scan.GUIInit();
    assert(!scan.GetSelectedAddress(address));
    scan.SetupLANScan();
    auto a=server("10.0.0.1",5,"^2Zulu\tArena\n^imaterial");a.challenge=scan.GetChallenge();
    a.serverInfo.Set("net_serverDedicated","1");
    auto b=server("10.0.0.2",20,"Alpha");b.challenge=scan.GetChallenge();
    auto malformed=a;malformed.clients=-1;assert(scan.InfoResponse(malformed)==-1);
    malformed.clients=MAX_ASYNC_CLIENTS+1;assert(scan.InfoResponse(malformed)==-1);
    assert(scan.InfoResponse(a)==0);
    assert(uiManager->last->rows[0].second.value.find("mtr_dedicated")!=std::string::npos);
    gui.SetStateInt("serverList_sel_0",0);scan.GUIUpdateSelected();assert(scan.GetSelectedAddress(address));
    const idStr original=address;assert(original==idStr("10.0.0.1:28004"));
    assert(scan.InfoResponse(b)==1);assert(scan.GetSelectedAddress(address)&&address==original);
    assert(scan.InfoResponse(a)>=0 && scan.Num()==2); // Duplicate LAN response.
    for(int row:{-2,INT_MIN,5000,INT_MAX}){gui.SetStateInt("serverList_sel_0",row);scan.GUIUpdateSelected();assert(!scan.GetSelectedAddress(address));assert(!gui.State().GetBool("browser_selected"));}
    gui.SetStateInt("serverList_sel_0",0);scan.GUIUpdateSelected();assert(scan.GetSelectedAddress(address));
    scan.SetSorting(SORT_SERVERNAME);assert(scan.GetSelectedAddress(address)&&address==original);
    assert(gui.State().GetInt("serverList_sel_0")==1);
    scan.SetSorting(SORT_SERVERNAME);assert(scan.GetSelectedAddress(address)&&address==original);
    const auto &row=uiManager->last->rows[0].second.value;
    assert(std::count(row.begin(),row.end(),'\t')==9);assert(row.find('\n')==std::string::npos);assert(row.find('^')==std::string::npos);
    assert(gui.State().GetInt("browser_count")==2); // OS mask does not reject our own modules.
    gui_filter_players.SetString("2");scan.ApplyFilter();assert(!scan.GetSelectedAddress(address));assert(gui.State().GetInt("browser_count")==0);
    assert(std::string(gui.State().GetString("server_IP")).empty());
    gui_filter_players.SetString("0");scan.ApplyFilter();assert(!scan.GetSelectedAddress(address));
    gui.SetStateInt("serverList_sel_0",0);scan.GUIUpdateSelected();assert(scan.GetSelectedAddress(address));
    scan.ToggleFavorite();assert(fileValue.files["server_favorites.list"].find(address.c_str())!=std::string::npos);
    assert(std::string(gui.State().GetString("favoriteStatus"))=="#str_200293");
    scan.SetSorting(SORT_FAVORITE);assert(scan.GetSelectedAddress(address));
    fileValue.failWrite=true;scan.ToggleFavorite();assert(std::string(gui.State().GetString("favoriteStatus"))=="#str_200293");fileValue.failWrite=false;
    const std::string savedFavorites=fileValue.files["server_favorites.list"];
    fileValue.failPromote=true;scan.ToggleFavorite();assert(fileValue.files["server_favorites.list"]==savedFavorites);
    assert(!fileValue.files.count("server_favorites.pending"));fileValue.failPromote=false;
    scan.ToggleFavorite();assert(fileValue.files["server_favorites.list"].empty());
    scan[0].serverInfo.Set("fs_game","custom");scan.UpdateFilterByMod(1);assert(gui.State().GetInt("browser_count")==1);
    scan.UpdateFilterByMod(1);assert(gui.State().GetInt("browser_count")==1);
    scan.UpdateFilterByMod(1);assert(gui.State().GetInt("browser_count")==2);
    gui_filter_gameType.SetString("7");scan.ApplyFilter();assert(gui.State().GetInt("browser_count")==0);
    scan[0].serverInfo.Set("si_gameType","Duel");scan.ApplyFilter();assert(gui.State().GetInt("browser_count")==1);
    scan[1].serverInfo.Set("si_gameType","unrecognized");scan.ApplyFilter();assert(gui.State().GetInt("browser_count")==1);
    gui_filter_gameType.SetString("0");scan.ApplyFilter();scan.ResetSorting();
    gui.SetStateInt("serverList_sel_0",0);scan.GUIUpdateSelected();assert(scan.GetSelectedAddress(address)&&address==original);
    fileValue.screenshot.clear();scan[0].serverInfo.Set("si_map","../secret");scan.GUIUpdateSelected();assert(fileValue.screenshot.empty());
    clockValue+=1200;scan.RunFrame();assert(scan.GetState()==idServerScan::IDLE);assert(!gui.State().GetBool("browser_scanning"));
    scan.Shutdown();
    fileValue.files["server_favorites.list"]="10.0.0.9:28004\n[2001:db8::1]:28004\n10.0.0.9:28004\n10.0.0.3:28004;quit\n";
    idServerScan restored;restored.GUIConfig(&gui,"serverList");restored.GUIInit();restored.AddFavoriteServers();restored.NetScan();
    assert(idAsyncNetwork::client.queries.size()==2);
    assert(idAsyncNetwork::client.queries[1]=="[2001:db8::1]:28004");
    restored.Shutdown();
    // Execute the real packet callback: losing any required field must not
    // create a browser row, even when the challenge was otherwise accepted.
    for(int completeFields=0;completeFields<=5;++completeFields){
        idAsyncClient packetClient;packetClient.serverList.SetupLANScan();
        idBitMsg msg;msg.challenge=packetClient.serverList.GetChallenge();msg.limit=completeFields;
        packetClient.ProcessInfoResponseMessage(netadr_t{"10.1.0.1",PORT_SERVER,NA_IP},msg);
        assert(packetClient.serverList.Num()==(completeFields==5?1:0));
        packetClient.serverList.Shutdown();
    }
}
'''


def main() -> None:
    scan = (ROOT / "src/framework/async/ServerScan.cpp").read_text(encoding="utf-8")
    scan = scan.replace('#include "../../ui/ListGUILocal.h"', "")
    header = (ROOT / "src/framework/async/ServerScan.h").read_text(encoding="utf-8")
    list_interface = (ROOT / "src/ui/ListGUI.h").read_text(encoding="utf-8")
    support = SUPPORT.replace("// PRODUCTION_LIST_GUI_INTERFACE", list_interface)
    client = (ROOT / "src/framework/async/AsyncClient.cpp").read_text(encoding="utf-8")
    response = function_body(client, "void idAsyncClient::ProcessInfoResponseMessage(")
    if not response.index("if ( msg.IsReadOverflowed() )") < response.index("serverList.InfoResponse( serverInfo )"):
        raise AssertionError("Truncated infoResponse reaches the visible browser")
    # Every new display lookup must exist in each shipped language table.
    labels = set(re.findall(r'"(#str_\d+)"', scan))
    for language in ("english", "french", "italian", "spanish", "polish", "russian"):
        tables = "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in (ROOT / "content/baseoq4/pak0/strings").glob(language + "_*.lang"))
        missing = labels - set(re.findall(r'"(#str_\d+)"', tables))
        if missing:
            raise AssertionError(f"Browser labels missing from {language}: {sorted(missing)}")
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="server-browser-", dir=temporary) as directory:
        source, binary = Path(directory) / "browser.cpp", Path(directory) / "browser.exe"
        source.write_text(support + header + scan + PACKET_SUPPORT + response + MAIN, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("server_browser_contract: PASS (production scan, rows, selection, filters, favorites, malformed input)")


if __name__ == "__main__":
    main()
