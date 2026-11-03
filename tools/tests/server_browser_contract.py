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
    static int Icmpn(const char *a,const char *b,int n) {
        for(int i=0;i<n;++i){
            const int x=std::tolower(static_cast<unsigned char>(a[i])),y=std::tolower(static_cast<unsigned char>(b[i]));
            if(x!=y||!x)return x-y;
        }
        return 0;
    }
    friend idStr operator+(const idStr &a,const char *b) { return idStr(a.value+b); }
    // idStr's own: a caret and a digit (or c and three digits) is a color.
    void RemoveColors() {
        std::string out;
        for(size_t i=0;i<value.size();++i){
            if(value[i]=='^'&&i+4<value.size()&&(value[i+1]=='c'||value[i+1]=='C')){i+=4;continue;}
            if(value[i]=='^'&&i+1<value.size()&&std::isdigit(static_cast<unsigned char>(value[i+1]))){++i;continue;}
            out+=value[i];
        }
        value=out;
    }
    void StripLeading(char c) { while(!value.empty()&&value.front()==c)value.erase(value.begin()); }
    void StripTrailing(char c) { while(!value.empty()&&value.back()==c)value.pop_back(); }
    int Cmp(const char *s) const { return value.compare(s); }
    int Find(const char *text,bool casesensitive=true) const {
        std::string a=value,b=text;
        if(!casesensitive){for(char &c:a)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                           for(char &c:b)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));}
        const auto at=a.find(b);return at==std::string::npos?-1:static_cast<int>(at);
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
    void SetInteger(int v){value=std::to_string(v);}
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
    // As the platform's: 127.0.0.1 is the loopback endpoint.
    out->port=std::atoi(port.c_str());out->host=value.substr(0,colon);
    out->type=value[0]=='['?NA_IP6:out->host=="127.0.0.1"?NA_LOOPBACK:NA_IP;
    return out->port>0 && out->port<65536 && out->host.find_first_not_of("0123456789abcdefABCDEF.:[]%") == std::string::npos;
}
// As the platform's: a loopback endpoint reads as localhost.
const char *Sys_NetAdrToString(const netadr_t &adr){return adr.type==NA_LOOPBACK?va("localhost:%d",adr.port):va("%s:%d",adr.host.c_str(),adr.port);}
bool Sys_CompareNetAdrBase(const netadr_t &a,const netadr_t &b){return a.host==b.host;}
int clockValue=100;
int Sys_Milliseconds(){return clockValue;}
struct idUserInterface {
    idDict state;
    const idDict &State()const{return state;}
    void SetStateString(const char *s,const char *v){state.Set(s,v);}
    void SetStateInt(const char *s,int v){state.SetInt(s,v);}
    void SetStateBool(const char *s,bool v){state.SetInt(s,v);}
    void SetStateFloat(const char *s,float v){state.Set(s,std::to_string(v).c_str());}
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
        if(!std::strcmp(token,"#str_107293"))return "Scanning for LAN servers...";
        if(!std::strcmp(token,"#str_107294"))return "Scanned %d openQ4 Servers";
        if(!std::strcmp(token,"#str_107297"))return "Scanning %d/%d: %s";
        if(!std::strcmp(token,"#str_107736"))return "Getting list of servers...";
        static const std::map<std::string,std::string> details={{"#str_107702","Map"},{"#str_107659","Time Limit"},
            {"#str_107660","Frag Limit"},{"#str_107661","Capture Limit"},{"#str_41404","Round Limit"},{"#str_122008","Control Time"},
            {"#str_107664","Team Damage"},{"#str_107666","Auto Balance Teams"},{"#str_107662","Pure"},{"#str_104254","Allow Spectators"},
            {"#str_107703","Yes"},{"#str_107704","No"}};
        const auto detail=details.find(token);if(detail!=details.end())return detail->second.c_str();
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
    // Not connecting, so no reply is the connection's protocol check.
    bool CheckConnectServerProtocol(const netadr_t,int,int){return false;}
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
    // The read view a menu drawing its own list uses: the servers the filter
    // passes in the sort's walking order, the sort, whether a server is a
    // favorite, a revision each change moves, and the game type's name.
    {bool up=true;assert(scan.GetSorting(up)==SORT_SERVERNAME&&!up);
     idList<int> order;scan.VisibleServers(order);assert(order.Num()==2);
     assert(!std::strcmp(scan[order[0]].serverInfo.GetString("si_name"),"^2Zulu\tArena\n^imaterial"));
     assert(!std::strcmp(scan[order[1]].serverInfo.GetString("si_name"),"Alpha"));
     const int before=scan.GetRevision();scan.ApplyFilter();assert(scan.GetRevision()>before);
     assert(!scan.IsFavoriteServer(order[0])&&!scan.IsFavoriteServer(-1)&&!scan.IsFavoriteServer(2));
     assert(!std::strcmp(idServerScan::GameTypeName("Team DM"),"#str_107677")&&!std::strcmp(idServerScan::GameTypeName("custom"),"custom"));}
    const auto &row=uiManager->last->rows[0].second.value;
    assert(std::count(row.begin(),row.end(),'\t')==9);assert(row.find('\n')==std::string::npos);assert(row.find('^')==std::string::npos);
    assert(gui.State().GetInt("browser_count")==2); // OS mask does not reject our own modules.
    gui_filter_players.SetString("2");scan.ApplyFilter();assert(!scan.GetSelectedAddress(address));assert(gui.State().GetInt("browser_count")==0);
    {idList<int> order;scan.VisibleServers(order);assert(order.Num()==0);}
    assert(std::string(gui.State().GetString("server_IP")).empty());
    gui_filter_players.SetString("0");scan.ApplyFilter();assert(!scan.GetSelectedAddress(address));
    // The filter row's tags and spinner (a menu drawing its own list): the
    // players filter's bits hide full and empty servers in the stock choices'
    // order, NO PASSWORD shows only servers without one, and the game type
    // steps round every type, naming each from the registry.
    {const auto count=[&]{return gui.State().GetInt("browser_count");};
     assert(!scan.IsFilterSet(idServerScan::BROWSER_FILTER_FULL)&&!scan.IsFilterSet(idServerScan::BROWSER_FILTER_EMPTY)&&count()==2);
     scan[0].clients=4;
     scan.ToggleFilter(idServerScan::BROWSER_FILTER_FULL);assert(gui_filter_players.GetInteger()==1&&count()==1);
     scan.ToggleFilter(idServerScan::BROWSER_FILTER_EMPTY);assert(gui_filter_players.GetInteger()==3&&count()==0);
     scan.ToggleFilter(idServerScan::BROWSER_FILTER_FULL);assert(gui_filter_players.GetInteger()==2&&count()==1);
     assert(scan.IsFilterSet(idServerScan::BROWSER_FILTER_EMPTY)&&!scan.IsFilterSet(idServerScan::BROWSER_FILTER_FULL));
     scan.ToggleFilter(idServerScan::BROWSER_FILTER_EMPTY);assert(gui_filter_players.GetInteger()==0&&count()==2);
     scan[0].clients=0;scan[1].serverInfo.Set("si_usePass","1");
     scan.ToggleFilter(idServerScan::BROWSER_FILTER_PASSWORD);
     assert(gui_filter_password.GetInteger()==2&&scan.IsFilterSet(idServerScan::BROWSER_FILTER_PASSWORD)&&count()==1);
     scan.ToggleFilter(idServerScan::BROWSER_FILTER_PASSWORD);assert(gui_filter_password.GetInteger()==0&&count()==2);
     scan[1].serverInfo.Set("si_usePass","0");
     const int before=scan.GetRevision();
     scan.StepGameTypeFilter(-1);assert(scan.GetGameTypeFilter()==12&&count()==0&&scan.GetRevision()>before);
     assert(!std::strcmp(idServerScan::GameTypeFilterName(12),"#str_107682"));
     scan.StepGameTypeFilter(1);assert(scan.GetGameTypeFilter()==0&&!*idServerScan::GameTypeFilterName(0)&&count()==2);
     scan.StepGameTypeFilter(1);assert(scan.GetGameTypeFilter()==1&&!std::strcmp(idServerScan::GameTypeFilterName(1),"#str_107679")&&count()==2);
     gui_filter_gameType.SetString("99");assert(!*idServerScan::GameTypeFilterName(99));
     scan.StepGameTypeFilter(1);assert(scan.GetGameTypeFilter()==1);
     gui_filter_gameType.SetString("0");scan.ApplyFilter();assert(count()==2);}
    gui.SetStateInt("serverList_sel_0",0);scan.GUIUpdateSelected();assert(scan.GetSelectedAddress(address));
    scan.ToggleFavorite();assert(fileValue.files["server_favorites.list"].find(address.c_str())!=std::string::npos);
    {idList<int> order;scan.VisibleServers(order);int favorites=0;for(int i=0;i<order.Num();++i)favorites+=scan.IsFavoriteServer(order[i]);assert(favorites==1);}
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
    // Loopback favorites (a server on this machine) list too, numeric or as
    // a "localhost" line saved before the numeric form.
    fileValue.files["server_favorites.list"]="10.0.0.9:28004\n[2001:db8::1]:28004\n10.0.0.9:28004\n10.0.0.3:28004;quit\n127.0.0.1:28005\nlocalhost:28006\n";
    idServerScan restored;restored.GUIConfig(&gui,"serverList");restored.GUIInit();restored.AddFavoriteServers();restored.NetScan();
    assert(idAsyncNetwork::client.queries.size()==4);
    assert(idAsyncNetwork::client.queries[1]=="[2001:db8::1]:28004");
    assert(idAsyncNetwork::client.queries[2]=="localhost:28005"&&idAsyncNetwork::client.queries[3]=="localhost:28006");
    restored.Shutdown();
    // A loopback server toggled into the favorites is saved in the numeric form.
    {idServerScan local;local.GUIConfig(&gui,"serverList");local.GUIInit();local.SetupLANScan();
     auto self=server("127.0.0.1",3,"Local");self.adr.type=NA_LOOPBACK;self.challenge=local.GetChallenge();assert(local.InfoResponse(self)==0);
     gui.SetStateInt("serverList_sel_0",0);local.GUIUpdateSelected();assert(!local.IsFavoriteServer(0));local.ToggleFavorite();
     assert(fileValue.files["server_favorites.list"].find("127.0.0.1:28004\n")!=std::string::npos);
     assert(fileValue.files["server_favorites.list"].find("localhost:28004")==std::string::npos&&local.IsFavoriteServer(0));
     // A menu drawing its own list toggles a server by index, as the stock
     // list toggles its selection; a server that does not exist changes nothing.
     assert(local.ToggleFavoriteServer(0)&&!local.IsFavoriteServer(0));
     assert(fileValue.files["server_favorites.list"].find("127.0.0.1:28004")==std::string::npos);
     const std::string kept=fileValue.files["server_favorites.list"];
     assert(!local.ToggleFavoriteServer(1)&&!local.ToggleFavoriteServer(-1)&&fileValue.files["server_favorites.list"]==kept);
     assert(local.ToggleFavoriteServer(0)&&local.IsFavoriteServer(0));
     local.Shutdown();}
    // FIND PLAYER/CLAN: only servers with a player whose name holds the text
    // (colors removed, any case) pass; empty text, or a new search, clears it.
    {idServerScan search;search.GUIConfig(&gui,"serverList");search.GUIInit();search.SetupLANScan();
     auto x=server("10.2.0.1",5,"With Grunt",2);x.challenge=search.GetChallenge();
     std::snprintf(x.nickname[0],MAX_NICKLEN,"%s","^1[Q4]^7Grunt");std::snprintf(x.nickname[1],MAX_NICKLEN,"%s","Kane");
     auto y=server("10.2.0.2",9,"Without",1);y.challenge=search.GetChallenge();std::snprintf(y.nickname[0],MAX_NICKLEN,"%s","Strauss");
     auto z=server("10.2.0.3",9,"Empty",0);z.challenge=search.GetChallenge();
     assert(search.InfoResponse(x)==0&&search.InfoResponse(y)==1&&search.InfoResponse(z)==2);
     idList<int> order;search.VisibleServers(order);assert(order.Num()==3);
     const int before=search.GetRevision();
     search.SetPlayerSearch("  grunt ");assert(!std::strcmp(search.GetPlayerSearch(),"grunt")&&search.GetRevision()>before);
     search.VisibleServers(order);assert(order.Num()==1&&order[0]==0);
     search.SetPlayerSearch("[q4]");search.VisibleServers(order);assert(order.Num()==1&&order[0]==0);   // a clan tag in a name
     search.SetPlayerSearch("^2STRAUSS");search.VisibleServers(order);assert(order.Num()==1&&order[0]==1);
     search.SetPlayerSearch("nobody");search.VisibleServers(order);assert(order.Num()==0);
     const int same=search.GetRevision();search.SetPlayerSearch("nobody");assert(search.GetRevision()==same);
     search.SetPlayerSearch("");search.VisibleServers(order);assert(order.Num()==3);
     search.Shutdown();}
    // The scan's progress, as the stock browser's load bar shows it (and the
    // retained page reads it): a LAN sweep names its stage, then the server
    // that answered, across its reply window; a completed scan its count on a
    // full bar; a clear empties it. The stock GUI gets the same.
    {idUserInterface bar;idServerScan lan;lan.GUIConfig(&bar,"serverList");lan.GUIInit();
     assert(!*lan.GetLoadNotice()&&lan.GetLoadProgress()==0.0f);
     clockValue=1000;lan.SetupLANScan();
     assert(!std::strcmp(lan.GetLoadNotice(),"Scanning for LAN servers...")&&lan.GetLoadProgress()==0.0f);
     assert(!std::strcmp(bar.State().GetString("loadNotice"),"Scanning for LAN servers...")&&!std::strcmp(bar.State().GetString("loadPct"),"0.000000"));
     const int before=lan.GetRevision();
     clockValue=1500;lan.RunFrame();
     assert(lan.GetLoadProgress()==0.5f&&lan.GetRevision()>before&&!std::strcmp(bar.State().GetString("loadPct"),"0.500000"));
     const int steady=lan.GetRevision();lan.RunFrame();assert(lan.GetRevision()==steady);   // nothing new, no news
     auto a=server("10.3.0.1",5,"^2LAN Arena",1);a.challenge=lan.GetChallenge();assert(lan.InfoResponse(a)==0);
     assert(!std::strcmp(lan.GetLoadNotice(),"Scanning - Found: LAN Arena"));
     clockValue=2100;lan.RunFrame();
     assert(lan.GetState()==idServerScan::IDLE&&!std::strcmp(lan.GetLoadNotice(),"Scanned 1 openQ4 Servers")&&lan.GetLoadProgress()==1.0f);
     assert(!std::strcmp(bar.State().GetString("loadNotice"),"Scanned 1 openQ4 Servers")&&!std::strcmp(bar.State().GetString("loadPct"),"1.000000"));
     lan.Clear();assert(!*lan.GetLoadNotice()&&lan.GetLoadProgress()==0.0f&&!*bar.State().GetString("loadNotice"));
     lan.Shutdown();}
    // A new Internet list: the master's request, then the server that answered
    // with the count heard from or given up on out of those listed, then the
    // count the scan covered. A refresh says nothing until a server answers.
    {idUserInterface bar;idServerScan net;net.GUIConfig(&bar,"serverList");net.GUIInit();
     clockValue=5000;net.Clear();net.NetScan();net.StartServers(true);net.RunFrame();
     assert(net.GetState()==idServerScan::NET_SCAN&&!std::strcmp(net.GetLoadNotice(),"Getting list of servers...")&&net.GetLoadProgress()==0.0f);
     net.AddServer(0,"10.4.0.1:28004");net.AddServer(1,"10.4.0.2:28004");net.RunFrame();
     assert(!std::strcmp(net.GetLoadNotice(),"Getting list of servers..."));
     auto b=server("10.4.0.1",5,"^1Answering",0);b.challenge=net.GetChallenge();assert(net.InfoResponse(b)==0);
     assert(!std::strcmp(net.GetLoadNotice(),"Scanning 1/2: Answering")&&net.GetLoadProgress()==0.5f);
     clockValue=20000;net.RunFrame();
     assert(net.GetState()==idServerScan::IDLE&&!std::strcmp(net.GetLoadNotice(),"Scanned 2 openQ4 Servers")&&net.GetLoadProgress()==1.0f);
     net.NetScan();assert(net.GetState()==idServerScan::NET_SCAN&&!*net.GetLoadNotice()&&net.GetLoadProgress()==0.0f);
     auto c=server("10.4.0.2",9,"Second",0);c.challenge=net.GetChallenge();assert(net.InfoResponse(c)>=0);
     assert(!std::strcmp(net.GetLoadNotice(),"Scanning 1/2: Second"));
     // A server without a name is named by its address.
     auto d=server("10.4.0.1",5,"",0);d.challenge=net.GetChallenge();assert(net.InfoResponse(d)>=0);
     assert(!std::strcmp(net.GetLoadNotice(),"Scanning 2/2: 10.4.0.1:28004")&&net.GetLoadProgress()==1.0f);
     net.Shutdown();}
    // A server's details, as the stock browser's tooltip shows them: its
    // address, its players' names as the browser reads them, and its settings
    // with the limit its game type ends on. The tooltip follows the hovered
    // row (or the row openq4_browser names) and hides with none.
    {idUserInterface tip;idServerScan d;d.GUIConfig(&tip,"serverList");d.GUIInit();d.SetupLANScan();
     auto dm=server("10.5.0.1",5,"Duel Hall",2);dm.challenge=d.GetChallenge();
     std::snprintf(dm.nickname[0],MAX_NICKLEN,"%s","^1Grunt");std::snprintf(dm.nickname[1],MAX_NICKLEN,"%s","Kane\tX");
     dm.serverInfo.Set("si_timeLimit","15");dm.serverInfo.Set("si_fragLimit","25");dm.serverInfo.Set("si_teamDamage","1");
     dm.serverInfo.Set("si_autobalance","0");dm.serverInfo.Set("si_pure","1");dm.serverInfo.Set("si_spectators","0");
     assert(d.InfoResponse(dm)==0);
     auto flags=server("10.5.0.2",9,"Flags",0);flags.challenge=d.GetChallenge();
     flags.serverInfo.Set("si_gameType","One Flag CTF");flags.serverInfo.Set("si_captureLimit","7");assert(d.InfoResponse(flags)==1);
     auto rounds=server("10.5.0.3",9,"Rounds",0);rounds.challenge=d.GetChallenge();
     rounds.serverInfo.Set("si_gameType","Clan Arena");rounds.serverInfo.Set("si_roundLimit","9");assert(d.InfoResponse(rounds)==2);
     auto zone=server("10.5.0.4",9,"Zone",0);zone.challenge=d.GetChallenge();
     zone.serverInfo.Set("si_gameType","DeadZone");zone.serverInfo.Set("si_controlTime","120");assert(d.InfoResponse(zone)==3);
     idStr address;idStrList players,settings;
     assert(d.GetServerDetails(0,address,players,settings)&&address==idStr("10.5.0.1:28004"));
     assert(players.Num()==2&&players[0]==idStr("Grunt")&&players[1]==idStr("Kane X"));
     assert(settings.Num()==7&&settings[0]==idStr("Map: mp/q4dm1")&&settings[1]==idStr("Time Limit: 15")&&settings[2]==idStr("Frag Limit: 25")&&
            settings[3]==idStr("Team Damage: Yes")&&settings[4]==idStr("Auto Balance Teams: No")&&settings[5]==idStr("Pure: Yes")&&
            settings[6]==idStr("Allow Spectators: No"));
     assert(d.GetServerDetails(1,address,players,settings)&&settings[2]==idStr("Capture Limit: 7")&&players.Num()==0);
     assert(d.GetServerDetails(2,address,players,settings)&&settings[2]==idStr("Round Limit: 9"));
     assert(d.GetServerDetails(3,address,players,settings)&&settings[2]==idStr("Control Time: 120"));
     assert(!d.GetServerDetails(4,address,players,settings)&&!d.GetServerDetails(-1,address,players,settings));
     idList<int> order;d.VisibleServers(order);assert(order.Num()==4);
     int dmRow=-1,flagsRow=-1;for(int i=0;i<order.Num();++i){if(order[i]==0)dmRow=i;if(order[i]==1)flagsRow=i;}
     d.GUIUpdateHover();assert(!tip.State().GetBool("serverinfo_visible"));
     tip.SetStateInt("serverList_hover",flagsRow);d.GUIUpdateHover();
     assert(tip.State().GetBool("serverinfo_visible")&&!std::strcmp(tip.State().GetString("server_tooltip_ip"),"10.5.0.2:28004"));
     assert(!std::strcmp(tip.State().GetString("server_tooltip_settingslist_item_2"),"Capture Limit: 7")&&
            tip.State().FindKey("server_tooltip_settingslist_item_7")==nullptr&&tip.State().FindKey("server_tooltip_playerlist_item_0")==nullptr);
     tip.SetStateInt("serverList_hover",dmRow);d.GUIUpdateHover();
     assert(!std::strcmp(tip.State().GetString("server_tooltip_playerlist_item_0"),"Grunt")&&
            !std::strcmp(tip.State().GetString("server_tooltip_playerlist_item_1"),"Kane X")&&
            tip.State().FindKey("server_tooltip_playerlist_item_2")==nullptr&&
            !std::strcmp(tip.State().GetString("server_tooltip_settingslist_item_6"),"Allow Spectators: No"));
     tip.SetStateInt("serverList_hover",-1);d.GUIUpdateHover();assert(!tip.State().GetBool("serverinfo_visible"));
     tip.SetStateInt("serverList_hover",9);d.GUIUpdateHover();assert(!tip.State().GetBool("serverinfo_visible"));   // past the list
     d.SetHoverOverride(flagsRow);d.GUIUpdateHover();   // the pointer on no row, the override names one
     assert(tip.State().GetBool("serverinfo_visible")&&!std::strcmp(tip.State().GetString("server_tooltip_ip"),"10.5.0.2:28004"));
     d.SetHoverOverride(-1);tip.SetStateInt("serverList_hover",-1);d.GUIUpdateHover();assert(!tip.State().GetBool("serverinfo_visible"));
     // The selection no longer drives the tooltip.
     tip.SetStateInt("serverList_sel_0",dmRow);d.GUIUpdateSelected();assert(!tip.State().GetBool("serverinfo_visible"));
     d.Shutdown();}
    // A server's map path is looked up only when it is a plain map path.
    assert(idServerScan::ValidMapPath("mp/q4dm1")&&!idServerScan::ValidMapPath("../secret")&&!idServerScan::ValidMapPath("/absolute"));
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
    for language in ("english", "german", "french", "italian", "spanish", "polish", "russian",
                     "brazilian", "czech", "hungarian", "turkish", "ukrainian"):
        tables = "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in (ROOT / "content/baseoq4/pak0/strings").glob(language + "_*.lang"))
        missing = labels - set(re.findall(r'"(#str_\d+)"', tables))
        if missing:
            raise AssertionError(f"Browser labels missing from {language}: {sorted(missing)}")
    # The limit the browser's details name for each public game type is the
    # one the game's descriptor table ends the match on.
    types = (ROOT / "src/mpgame/mp/GameTypes.cpp").read_text(encoding="utf-8")
    table = types[types.index("mpGameTypeInfoTable[] = {"):types.index("};", types.index("mpGameTypeInfoTable[] = {"))]
    expected = {}
    for name, flags, public in re.findall(r'\{\s*GAME_\w+,\s*"([^"]+)",[^{}]*?\n\s*([A-Z_| ]+?),\s*(?:\n\s*)?MP_GAMESTATE_\w+,\s*(true|false)\s*\}', table):
        if public != "true":
            continue
        flags = {flag.strip() for flag in flags.split("|")}
        expected[name] = ("si_captureLimit" if "GTF_CAPTURELIMIT" in flags else "si_roundLimit" if "GTF_ROUNDLIMIT" in flags
                          else "si_controlTime" if "GTF_DEADZONE" in flags else "si_fragLimit" if "GTF_FRAGLIMIT" in flags else "?")
    limits = scan[scan.index("static void ServerBrowserMatchLimit("):scan.index("static const char *ServerBrowserGameType(")]
    listed = lambda array: re.findall(r'"([^"]+)"', re.search(array + r'\[\] = \{([^}]*)\}', limits).group(1))
    actual = {name: "si_fragLimit" for name in expected}
    actual.update({name: "si_captureLimit" for name in listed("capture")})
    actual.update({name: "si_roundLimit" for name in listed("rounds")})
    actual["DeadZone"] = "si_controlTime"
    if len(expected) != 12 or "?" in expected.values() or actual != expected:
        raise AssertionError(f"the browser's match limits drifted from the game types: {expected} != {actual}")
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
    print("server_browser_contract: PASS (production scan, rows, selection, filters, favorites, loopback favorites, player search, read view, "
          "scan progress, server details and tooltip, malformed input)")


if __name__ == "__main__":
    main()
