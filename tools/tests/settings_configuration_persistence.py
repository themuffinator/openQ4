#!/usr/bin/env python3
"""Checked production config serialization, native durable commit and lock tests.

Extracts Common's actual persistence methods and the actual binding/archive
serializers. The engine string/file/CVar/container edges are small doubles; the
durable filesystem implementation, lease and compile-time fault hooks are real.
No game, renderer, input or active user configuration is accessed.
"""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#define OPENQ4_DURABLE_FILE_TESTING
#include "src/framework/DurableFile.cpp"
#include "src/framework/SettingsPersistence.h"
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

static int checks=0;
static void Check(bool value,const char* message) {
    ++checks;
    if(!value) throw std::runtime_error(message);
}
class idStr {
    std::string value;
public:
    idStr()=default;
    idStr(const char* text):value(text){}
    idStr(const std::string& text):value(text){}
    const char* c_str()const{return value.c_str();}
    int Length()const{return static_cast<int>(value.size());}
    idStr& operator+=(const char* text){value+=text;return *this;}
    void Replace(const char* from,const char* to){
        size_t at=0;
        while((at=value.find(from,at))!=std::string::npos){value.replace(at,strlen(from),to);at+=strlen(to);}
    }
    static int vsnPrintf(char* out,int size,const char* format,va_list args){
        const int length=std::vsnprintf(out,static_cast<size_t>(size),format,args);
        return length<0 || length>=size?-1:length;
    }
    static int Icmpn(const char* a,const char* b,int length){
        for(int i=0;i<length;++i){
            const int av=std::tolower(static_cast<unsigned char>(a[i]));
            const int bv=std::tolower(static_cast<unsigned char>(b[i]));
            if(av!=bv || !av)return av-bv;
        }
        return 0;
    }
    static int Icmp(const char* a,const char* b){return Icmpn(a,b,static_cast<int>(std::max(strlen(a),strlen(b)))+1);}
};
static bool partialMemoryWrite=false;
class idFile {
public:
    virtual ~idFile()=default;
    virtual int Write(const void*,int)=0;
    virtual int Printf(const char*,...)=0;
    virtual int VPrintf(const char*,va_list)=0;
};
class idFile_Memory:public idFile {
    std::string bytes;
public:
    explicit idFile_Memory(const char*){}
    int Write(const void* input,int length)override{
        if(partialMemoryWrite && length>0){bytes.append(static_cast<const char*>(input),length-1);return length-1;}
        bytes.append(static_cast<const char*>(input),length);return length;
    }
    int Length(){return static_cast<int>(bytes.size());}
    const char* GetDataPtr()const{return bytes.c_str();}
    int Printf(const char*,...)override{assert(false);return 0;}
    int VPrintf(const char*,va_list)override{assert(false);return 0;}
};
using byte=unsigned char;
using ID_TIME_T=std::time_t;
class idBase64 {
public:
    void Encode(const byte*,int){}
    const char* c_str()const{return "checked-version";}
};
class idCompressor {
    idFile* output=nullptr;
public:
    bool refuse=false;
    void Init(idFile* file,bool,int){output=file;}
    int Write(const void* bytes,int length){return refuse?0:output->Write(bytes,length);}
    void FinishCompress(){}
} compressor;
static bool blocked=false;
static bool UI_SettingsBlocksConfigWrite(){return blocked;}
static constexpr int CVAR_ARCHIVE=1,CVAR_PRIVATE=2;
static constexpr int MAX_KEYS=3;
struct Key {idStr binding;} keys[MAX_KEYS];
class idKeyInput {
public:
    static const char* KeyNumToString(int index,bool){return index==0?"a":index==1?"\\":"enter";}
    static void WriteBindings(idFile* f);
};
static std::function<void()> serializeHook;
struct idInternalCVar {
    std::string name,value;int flags=CVAR_ARCHIVE;
    int GetFlags()const{return flags;}
    const char* GetName()const{return name.c_str();}
    const char* GetString()const{
        if(serializeHook){auto hook=std::move(serializeHook);serializeHook={};hook();}
        return value.c_str();
    }
};
struct CVarList:std::vector<idInternalCVar*> {using std::vector<idInternalCVar*>::operator=;int Num()const{return static_cast<int>(size());}};
class idCVarSystemLocal {
public:
    CVarList cvars;
    std::string saveRoot;
    int modified=CVAR_ARCHIVE,clears=0;
    const char* GetCVarString(const char* name){return !strcmp(name,"fs_savepath")?saveRoot.c_str():"version";}
    int GetModifiedFlags(){return modified;}
    void ClearModifiedFlags(int flags){modified&=~flags;++clears;}
    void WriteFlaggedVariables(int flags,const char* setCmd,idFile* f)const;
} variables,*cvarSystem=&variables;
struct FileSystem {
    bool initialized=true,refuseParents=false;
    int creates=0,cacheClears=0;
    std::vector<std::string> created;
    bool IsInitialized()const{return initialized;}
    void CreateOSPath(const char* path){
        ++creates;created.emplace_back(path);
        if(refuseParents)return;
#ifdef _WIN32
        Check(std::strchr(path,'/')==nullptr,"parent creation uses native separators");
#endif
        std::filesystem::create_directories(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path))).parent_path());
    }
    void ClearDirCache(){++cacheClears;}
} files,*fileSystem=&files;
struct Session {int cdWrites=0;void WriteCDKey(){++cdWrites;}} sessionObject,*session=&sessionObject;
struct Arena {bool pending=false;bool NeedsCleanup()const{return pending;}} arenaCampaign;
static bool com_fullyInitialized=true;
#define CONFIG_FILE "openQ4Config.cfg"
class idCommonLocal {
public:
    int messages=0;
    idCompressor* config_compressor=&compressor;
    void Printf(const char*,...){++messages;}
    bool WriteConfigToFileChecked(const char*,bool,std::string&);
    void WriteConfigToFile(const char*);
    void WriteConfiguration(void);
} commonLocal;
'''

TESTS = r'''
static std::string testRoot;
static std::string Error;
static std::string Path(const char* filename){return variables.saveRoot+"/baseoq4/"+filename;}
static std::string Read(const std::string& path){
    std::string result;
    Check(openq4::DurableReadExact(path,openq4::DurableFileMaxBytes,result,Error)==openq4::DurableReadResult::Present,"read test config");
    return result;
}
static void Seed(const std::string& path,const std::string& bytes){
    Check(openq4::DurableReplaceExact(path,bytes,Error),"seed native file");
}
static void Reset(const char* name){
    openq4::durable_detail::faults={};
    variables={};files={};sessionObject={};commonLocal={};compressor={};
    variables.saveRoot=testRoot+"/"+name;
    blocked=false;com_fullyInitialized=true;arenaCampaign.pending=false;
    partialMemoryWrite=false;serializeHook={};
    static idInternalCVar archive{"r_brightness","1.125",CVAR_ARCHIVE};
    static idInternalCVar hidden{"private_key","never-write",CVAR_ARCHIVE|CVAR_PRIVATE};
    static idInternalCVar temporary{"temporary","never-write",0};
    archive.value="1.125";
    variables.cvars={&archive,&hidden,&temporary};
    keys[0].binding="_moveForward";keys[1].binding="say hello";keys[2].binding="";
    std::string journal,lock;
    Check(Common_SettingsPersistencePaths(journal,lock,Error),"resolve test paths");
    Check(journal==Path("ui-settings-recovery.dat") && lock==Path(".settings-recovery.lock"),"unified exact recovery paths");
}
static void Success(){
    Reset("SuccessCase");
    commonLocal.WriteConfiguration();
    Check(variables.modified==0 && variables.clears==1 && sessionObject.cdWrites==1,"auto commit clears only after success");
    auto bytes=Read(Path(CONFIG_FILE));
    Check(bytes.find("unbindall\r\n")!=std::string::npos,"binding serializer preserved");
    Check(bytes.find("bind \"a\" \"_moveForward\"\r\n")!=std::string::npos,"binding bytes preserved");
    Check(bytes.find("seta r_brightness \"1.125\"\r\n")!=std::string::npos,"archive bytes preserved");
    Check(bytes.find("never-write")==std::string::npos,"private and nonarchive values excluded");
#ifdef ID_WRITE_VERSION
    Check(bytes.rfind("// checked-version\r\n",0)==0,"optional compressed version prefix retained");
#endif
    const int creates=files.creates;
    commonLocal.WriteConfiguration();
    Check(files.creates==creates && sessionObject.cdWrites==1,"clean flags avoid redundant writes");
    variables.modified=CVAR_ARCHIVE;
    commonLocal.WriteConfigToFile("Nested/ExactCase.cfg");
    Check(Read(Path("Nested/ExactCase.cfg"))==bytes && variables.modified==CVAR_ARCHIVE,"explicit backup preserves exact relative case and archive dirty");
    Check(Common_WriteSettingsConfiguration(false,Error) && variables.modified==0,"private baseline commit reports checked success");
}
static void Guards(){
    Reset("Guards");
    blocked=true;
    int creates=files.creates;
    commonLocal.WriteConfiguration();commonLocal.WriteConfigToFile("explicit.cfg");
    Check(!Common_WriteSettingsConfiguration(false,Error),"private ordinary commit honors UI guard");
    Check(files.creates==creates && variables.modified==CVAR_ARCHIVE && sessionObject.cdWrites==0,"blocked writes do not mutate or clear flags");
    blocked=false;com_fullyInitialized=false;commonLocal.WriteConfiguration();
    Check(files.creates==creates,"incomplete initialization blocks automatic write");
    com_fullyInitialized=true;
#ifndef ID_DEDICATED
    arenaCampaign.pending=true;commonLocal.WriteConfiguration();
    Check(files.creates==creates,"arena transaction remains protected");
    arenaCampaign.pending=false;
#endif
    const char* bad[]={"","/outside.cfg","../outside.cfg","a/../b.cfg","a//b.cfg","a\\b.cfg","C:outside.cfg","CON.cfg","ui-settings-recovery.dat",".SETTINGS-RECOVERY.LOCK","ui-settings-recovery.dat/nested.cfg",".settings-recovery.lock/nested.cfg"};
    for(const char* path:bad){
        Check(!commonLocal.WriteConfigToFileChecked(path,false,Error),"unsafe or reserved target rejected");
        Check(files.creates==creates && variables.modified==CVAR_ARCHIVE,"invalid path refuses before filesystem mutation");
    }
    Check(!commonLocal.WriteConfigToFileChecked(nullptr,false,Error),"null filename rejected");
    std::string journal="journal-sentinel",lock="lock-sentinel";
    const std::string original=variables.saveRoot;
    for(const char* path:{"relative","","C:relative","//server/","/root/../outside","C:/root/../outside"}){
        variables.saveRoot=path;
        Check(!Common_SettingsPersistencePaths(journal,lock,Error),"unsafe root rejected");
        Check(journal=="journal-sentinel" && lock=="lock-sentinel" && files.creates==creates,"failed path resolution is atomic");
    }
    variables.saveRoot=original;files.initialized=false;
    Check(!Common_SettingsPersistencePaths(journal,lock,Error),"uninitialized filesystem refused");
}
static void JournalAndLease(){
    Reset("Journal");
    for(const std::string& bytes:{std::string(),std::string("malformed journal"),std::string("{\"status\":\"pending\"}")}){
        Seed(Path("ui-settings-recovery.dat"),bytes);
        commonLocal.WriteConfiguration();
        Check(variables.modified==CVAR_ARCHIVE && !std::filesystem::exists(Path(CONFIG_FILE)),"any journal blocks ordinary config publication");
    }
    Check(openq4::DurableRemoveExact(Path("ui-settings-recovery.dat"),Error),"remove test journal");
    std::filesystem::create_directory(Path("ui-settings-recovery.dat"));
    Check(!Common_WriteSettingsConfiguration(false,Error),"nonregular journal also blocks");
    Check(std::filesystem::remove(Path("ui-settings-recovery.dat")),"remove owned empty test directory");
    openq4::DurableFileLease peer;
    Check(peer.TryAcquire(Path(".settings-recovery.lock"),Error),"peer obtains lease");
    Check(!Common_WriteSettingsConfiguration(false,Error) && variables.modified==CVAR_ARCHIVE,"ordinary writer refuses held lease");
    Seed(Path("ui-settings-recovery.dat"),"pending");blocked=true;
    Check(Common_WriteSettingsConfiguration(true,Error),"coordinator-held lease permits qualified pending commit");
    Check(peer.IsHeld() && Read(Path("ui-settings-recovery.dat"))=="pending","coordinator commit does not remove or release recovery ownership");
    Check(variables.modified==0,"coordinator success clears dirty archive");
    peer.Release();blocked=false;
    Check(openq4::DurableRemoveExact(Path("ui-settings-recovery.dat"),Error),"remove test journal after release");
    Check(std::filesystem::exists(Path(".settings-recovery.lock")),"lock identity remains permanently present");
    using openq4::durable_detail::Point;
    using openq4::durable_detail::faults;
    faults={};faults.point=Point::OpenRead;faults.failOn=1;
    variables.modified=CVAR_ARCHIVE;
    Check(!Common_WriteSettingsConfiguration(false,Error) && variables.modified==CVAR_ARCHIVE,"journal read refusal blocks publication");
    faults={};
}
static void DurabilityFailures(){
    using openq4::durable_detail::Point;
    using openq4::durable_detail::faults;
    const Point points[]={Point::OpenTemp,Point::Write,Point::FileSync,Point::CloseWrite,Point::Rename,Point::MetadataSync};
    int index=0;
    for(Point point:points){
        Reset(("Fault"+std::to_string(++index)).c_str());
        Seed(Path(CONFIG_FILE),"old-complete-config");
        faults={};faults.point=point;faults.failOn=1;
        commonLocal.WriteConfiguration();
        Check(variables.modified==CVAR_ARCHIVE && variables.clears==0 && sessionObject.cdWrites==0,"durability failure retains dirty state and skips ancillary writes");
        Check(faults.calls==1,"requested production fault was exercised");
        faults={};
        const auto bytes=Read(Path(CONFIG_FILE));
        if(point!=Point::MetadataSync)Check(bytes=="old-complete-config","prepublication failure preserves complete old file");
        else Check(bytes.find("unbindall")!=std::string::npos,"postpublication failure is still reported without pretending old bytes remain");
        commonLocal.WriteConfiguration();
        Check(variables.modified==0 && sessionObject.cdWrites==1,"next successful durable attempt clears dirty state");
    }
    Reset("ParentRefusal");
    variables.saveRoot+="/not-created";files.refuseParents=true;
    commonLocal.WriteConfiguration();
    Check(variables.modified==CVAR_ARCHIVE && sessionObject.cdWrites==0,"parent creation failure remains dirty");
}
static void SerializationFailures(){
    Reset("Serialization");
    Seed(Path(CONFIG_FILE),"old-complete-config");
    partialMemoryWrite=true;
    Check(!Common_WriteSettingsConfiguration(false,Error),"ignored serializer short write is detected");
    partialMemoryWrite=false;
    Check(Read(Path(CONFIG_FILE))=="old-complete-config" && variables.modified==CVAR_ARCHIVE,"short serialization never publishes");
    keys[0].binding=std::string(5000,'x');
    Check(!Common_WriteSettingsConfiguration(false,Error),"binding formatting truncation refuses commit");
    keys[0].binding="okay";
    variables.cvars[0]->value=std::string(5000,'x');
    Check(!Common_WriteSettingsConfiguration(false,Error),"archive formatting truncation refuses commit");
    variables.cvars[0]->value="1.125";
    idCheckedConfigMemory bound("bounded");
    const std::string chunk(1024*1024,'x');
    for(int i=0;i<16;++i)Check(bound.Write(chunk.data(),static_cast<int>(chunk.size()))==static_cast<int>(chunk.size()),"bounded sink accepts complete valid writes");
    Check(bound.Write("x",1)==0 && bound.Failed(),"aggregate size refuses overflow before allocation");
    Check(bound.Printf("small")==0 && bound.Failed(),"serialization failure remains latched");
    idCheckedConfigMemory malformed("malformed");
    Check(malformed.Write(nullptr,1)==0 && malformed.Failed(),"invalid raw write fails");
    bool nestedAccepted=true;
    serializeHook=[&]{std::string error;nestedAccepted=Common_WriteSettingsConfiguration(true,error);};
    Check(Common_WriteSettingsConfiguration(false,Error) && !nestedAccepted,"serialization callback cannot reenter even coordinator bypass");
    variables.modified=CVAR_ARCHIVE;
    serializeHook=[] {throw std::runtime_error("serializer exception");};
    bool caught=false;
    try{Common_WriteSettingsConfiguration(false,Error);}catch(const std::runtime_error&){caught=true;}
    Check(caught && variables.modified==CVAR_ARCHIVE,"exception never clears dirty state");
    Check(Common_WriteSettingsConfiguration(false,Error),"exception unwinds in-process guard and native lease");
#ifdef ID_WRITE_VERSION
    variables.modified=CVAR_ARCHIVE;commonLocal.config_compressor=nullptr;
    Check(!Common_WriteSettingsConfiguration(false,Error) && variables.modified==CVAR_ARCHIVE,"missing optional compressor refuses safely");
    commonLocal.config_compressor=&compressor;compressor.refuse=true;
    Check(!Common_WriteSettingsConfiguration(false,Error),"version compressor short consumption refuses commit");
#endif
}
int main(int argc,char** argv){
    try{
        Check(argc==2,"test root argument");testRoot=argv[1];
        Success();Guards();JournalAndLease();DurabilityFailures();SerializationFailures();
        std::cout<<checks<<" production configuration persistence checks passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
'''


def source_code():
    common = (ROOT / 'src/framework/Common.cpp').read_text(encoding='utf-8')
    filesystem = (ROOT / 'src/framework/FileSystem.cpp').read_text(encoding='utf-8')
    keys = (ROOT / 'src/framework/KeyInput.cpp').read_text(encoding='utf-8')
    cvars = (ROOT / 'src/framework/CVarSystem.cpp').read_text(encoding='utf-8')
    return '\n'.join((SUPPORT,
        function_body(filesystem, 'static bool FS_IsWindowsDeviceQPathSegment('),
        function_body(filesystem, 'bool FS_ValidateRelativeWritePath('),
        function_body(keys, 'void idKeyInput::WriteBindings('),
        function_body(cvars, 'void idCVarSystemLocal::WriteFlaggedVariables('),
        function_body(common, 'class idCheckedConfigMemory') + ';',
        *(function_body(common, signature) for signature in (
            'static bool Common_SettingsSaveDirectory(',
            'static void Common_CreateSettingsParent(',
            'bool Common_SettingsPersistencePaths(',
            'bool idCommonLocal::WriteConfigToFileChecked(',
            'bool Common_WriteSettingsConfiguration(',
            'void idCommonLocal::WriteConfigToFile(',
            'void idCommonLocal::WriteConfiguration(')),
        TESTS))


def main():
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    output = ROOT / '.tmp/ui'
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='settings-config-', dir=output) as directory:
        temporary = Path(directory)
        source = temporary / 'configuration.cpp'
        source.write_text(source_code(), encoding='utf-8')
        environment = dict(os.environ, TEMP=directory, TMP=directory, TMPDIR=directory)
        for variant, defines in (('client', []), ('dedicated-version', ['-DID_DEDICATED', '-DID_WRITE_VERSION'])):
            executable = temporary / f'{variant}.exe'
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-I', str(ROOT), *defines,
                            str(source), '-o', str(executable)], env=environment, check=True)
            subprocess.run([str(executable), str(temporary / variant).replace('\\', '/')], env=environment, check=True)
    print('Production config serialization and native durable/lock failure scenarios passed (client, dedicated/version).')


if __name__ == '__main__':
    main()
