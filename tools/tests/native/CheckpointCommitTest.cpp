// Production transaction/recovery plus queue/lifetime/polling, with failures at
// every namespace operation and recovery of every interrupted state.
#include "src/framework/DurableFile.h"
#include "src/framework/SaveGameTransaction.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
namespace fs = std::filesystem;
static int checks;
static void Check(bool yes) { ++checks; if (!yes) { std::fprintf(stderr,"check %d failed\n",checks); std::abort(); } }
using Disk=std::map<std::string,std::string>;
struct Ops {
    Disk disk;
    std::set<int> failures;
    std::set<std::string> blocked;
    int calls=0;
    std::vector<Disk> interrupted;
    bool Fail(std::string& error) { if(failures.count(++calls)) { error="injected";return true; } return false; }
    bool Exists(const std::string& p,bool& exists,std::string& error) {
        if(Fail(error) || blocked.count(p)) {error="inaccessible or nonregular";return false;}
        exists=disk.count(p)>0;return true;
    }
    bool Move(const std::string& from,const std::string& to,std::string& error) {
        if(Fail(error))return false;
        Check(disk.count(from) && !disk.count(to));
        disk[to]=disk.at(from);disk.erase(from);interrupted.push_back(disk);return true;
    }
    bool Remove(const std::string& p,std::string& error) {
        if(Fail(error))return false;
        disk.erase(p);interrupted.push_back(disk);return true;
    }
};
static Ops* recovery;
struct idStr : std::string { using std::string::string; using std::string::operator=; };
struct Common { void Printf(const char*,...) {} void Warning(const char*,...) {} } commonObject;
static Common* common=&commonObject;
static bool Session_RelativeSaveFileExists(const idStr& p) { return recovery->disk.count(p)>0; }
static void Session_RemoveRelativeSaveFile(const idStr& p) { recovery->disk.erase(p); }
static bool Session_RenameRelativeSaveFile(const idStr& a,const idStr& b) {
    if(!recovery->disk.count(a) || recovery->disk.count(b)) return false;
    recovery->disk[b]=recovery->disk.at(a);recovery->disk.erase(a);return true;
}
static bool Session_IsSavePreviewDeletionMarker(const idStr& p) { return recovery->disk.count(p) && recovery->disk.at(p)=="delete-preview"; }
static bool Session_ValidateStagedSaveGameHeader(const idStr& p) { return recovery->disk.count(p) && (recovery->disk.at(p)=="old2" || recovery->disk.at(p)=="new2"); }
#include "CheckpointRecoveryUnderTest.inc"
static auto Files() {
    std::array<openq4::SaveGameTransactionFile,3> f;
    for(int i=0;i<3;++i) {f[i].final="slot"+std::to_string(i);f[i].temporary=f[i].final+".tmp";f[i].backup=f[i].final+".prev";}
    return f;
}
static Disk Fixture(int existing) {
    Disk d;
    for(int i=0;i<3;++i) {d["slot"+std::to_string(i)+".tmp"]="new"+std::to_string(i);if(existing&(1<<i))d["slot"+std::to_string(i)]="old"+std::to_string(i);}
    return d;
}
static void RecoverAndCheck(const Disk& state,int existing) {
    Ops r;r.disk=state;recovery=&r;
    const bool committed=r.disk.count("slot2") && r.disk.at("slot2")=="new2" && !r.disk.count("slot2.tmp");
    Check(Session_RecoverInterruptedSaveFiles("slot2","slot0","slot1"));
    for(int i=0;i<3;++i) {
        const auto key="slot"+std::to_string(i);
        const bool present=committed || (existing&(1<<i));
        Check((r.disk.count(key)>0)==present);
        if(present)Check(r.disk.at(key)==(committed?"new":"old")+std::to_string(i));
        Check(!r.disk.count(key+".prev") && !r.disk.count(key+".tmp"));
    }
}
static void TransactionCases() {
    // Missing optional old sidecars and new slots have the same publication
    // point. Inspect failures and crashes with every original-file combination.
    for(int existing=0;existing<8;++existing) {
        for(int first=0;first<=26;++first) for(int second=0;second<=26;++second) {
            Ops ops;ops.disk=Fixture(existing);if(first)ops.failures.insert(first);if(second)ops.failures.insert(second);
            auto f=Files();auto result=openq4::CommitSaveGame(f,ops,false);
            if(result.committed)Check(ops.disk.at("slot2")=="new2");
            else Check(!ops.disk.count("slot2") || ops.disk.at("slot2")=="old2");
            RecoverAndCheck(ops.disk,existing);
            for(const auto& crash:ops.interrupted)RecoverAndCheck(crash,existing);
        }
    }
    Ops blocked;blocked.disk=Fixture(7);blocked.blocked.insert("slot1");auto f=Files();auto result=openq4::CommitSaveGame(f,blocked,false);
    Check(!result.committed && blocked.disk==Fixture(7));
    Ops backup;backup.disk=Fixture(7);backup.disk["slot0.prev"]="older";f=Files();result=openq4::CommitSaveGame(f,backup,false);
    Check(!result.committed && result.recoveryNeeded && backup.disk.at("slot0.prev")=="older");
    Ops marker;marker.disk=Fixture(7);marker.disk["slot1.tmp"]="delete-preview";f=Files();result=openq4::CommitSaveGame(f,marker,true);
    Check(result.committed && result.cleanupComplete && !marker.disk.count("slot1"));
}
static void StagingCases() {
    // A crash while a save is being staged leaves temporaries but no backups.
    // Recovery must keep the previous slot whole: payload with or without its
    // sidecars, or nothing.
    for(int existing:{0,4,5,6,7}) for(int staged=0;staged<8;++staged) {
        Disk d;
        for(int i=0;i<3;++i) {
            const auto key="slot"+std::to_string(i);
            if(existing&(1<<i))d[key]="old"+std::to_string(i);
            if(staged&(1<<i))d[key+".tmp"]="new"+std::to_string(i);
        }
        RecoverAndCheck(d,existing);
    }
}
struct idJobContext {void* data;};
enum class idJobPriority {LOW};
enum class idJobSubmitResult {ACCEPTED,EXECUTED_SYNCHRONOUSLY,QUEUE_FULL};
enum class Schedule {Deferred,Immediate,NoList,NoJob,Rejected};
static Schedule schedule=Schedule::Deferred;
struct idJobList {
    void (*callback)(const idJobContext&)=nullptr;void* data=nullptr;bool queued=false;int polls=0;
    bool AddJob(void(*fn)(const idJobContext&),void* ptr) {callback=fn;data=ptr;return schedule!=Schedule::NoJob;}
    idJobSubmitResult Submit() {if(schedule==Schedule::Rejected)return idJobSubmitResult::QUEUE_FULL;queued=true;if(schedule==Schedule::Immediate){Wait();return idJobSubmitResult::EXECUTED_SYNCHRONOUSLY;}return idJobSubmitResult::ACCEPTED;}
    void Wait(){if(queued){queued=false;callback({data});}}
    bool TryWait(){if(++polls>=2)Wait();return !queued;}
    ~idJobList(){Wait();}
};
struct JobSystem {
    std::unique_ptr<idJobList> CreateJobList(const char*,int,int,idJobPriority){return schedule==Schedule::NoList?nullptr:std::make_unique<idJobList>();}
} jobSystem;
#include "CheckpointCommitUnderTest.inc"
struct OtherStorage {std::unique_ptr<idJobList> jobs;};
static std::function<bool()> sessionPendingCheckpoint;
static std::shared_ptr<OtherStorage> sessionPendingCheckpointStorage,sessionPendingCheckpointPreviewStorage;
static std::shared_ptr<sessionSaveCommitJob_t> sessionPendingCheckpointCommit;
static std::string sessionPendingCheckpointPreview;
static bool sessionFinishingPendingSave=false,sessionPendingCheckpointPreviewReady=true;
static int sessionPendingCheckpointSince=0,tick=0;
static int Sys_Milliseconds(){return ++tick;}
static void Sys_Sleep(int){++tick;}
struct Renderer {int PollSavePreview(const char*){return 1;}void CancelSavePreview(){}} renderer;
static Renderer* renderSystem=&renderer;
static void Session_QueueCheckpointPreview(OtherStorage&){}
struct idSessionLocal {
    int redraws=0;bool syncNextGameFrame=false,insideExecuteMapChange=false;
    bool FinishPendingSave(bool);
    void UpdateScreen(bool){++redraws;Check(!FinishPendingSave(true));}
};
struct idFile {};
static int fileCloses,stagedCleanups,stateRestores;
struct FileSystem {void CloseFile(idFile* f){++fileCloses;delete f;}} fsObject;
static FileSystem* fileSystem=&fsObject;
struct SoundSystem {void SetMute(bool){}} soundObject;
static SoundSystem* soundSystem=&soundObject;
enum saveType_t {ST_CHECKPOINT};
static void Session_RemoveStagedSaveFiles(const idStr&,const idStr&,const idStr&){++stagedCleanups;}
static void Session_RestoreGeneratedSaveState(idSessionLocal&,bool,saveType_t,const idStr&,int){++stateRestores;}
#include "CheckpointPendingUnderTest.inc"
static void GuardCases(){
    for(bool preserve:{false,true})for(bool success:{false,true}){
        idSessionLocal session;fileCloses=stagedCleanups=stateRestores=0;
        auto state=std::make_shared<idFile*>(new idFile);
        {sessionSaveOperationGuard_t guard(session,state,"game","description","preview",true,ST_CHECKPOINT,"previous",1);
         if(preserve)guard.PreserveRecovery();if(success)guard.Complete();}
        Check(*state==nullptr && fileCloses==1);
        Check(stagedCleanups==(!success && !preserve?1:0));
        Check(stateRestores==(!success?1:0) && session.syncNextGameFrame==!success);
    }
}
static std::string Utf8(const fs::path& p){auto u=p.generic_u8string();return {reinterpret_cast<const char*>(u.data()),u.size()};}
static void Put(const fs::path& p,const std::string& data){std::ofstream o(p,std::ios::binary);o<<data;o.close();Check(!o.fail());}
static std::string Get(const fs::path& p){std::ifstream i(p,std::ios::binary);return {std::istreambuf_iterator<char>(i),std::istreambuf_iterator<char>()};}
static auto NativeFiles(const fs::path& root){auto f=Files();for(auto& entry:f){entry.final=Utf8(root/fs::u8path(entry.final+"-\xc3\xa9"));entry.temporary=entry.final+".tmp";entry.backup=entry.final+".prev";}return f;}
static void Clean(const fs::path& root){
    for(auto& f:NativeFiles(root))for(auto* p:{&f.final,&f.temporary,&f.backup}){std::error_code ec;fs::remove(fs::u8path(*p),ec);Check(!ec);}
}
static void Populate(const fs::path& root){Clean(root);int i=0;for(auto& f:NativeFiles(root)){Put(fs::u8path(f.final),"old"+std::to_string(i));Put(fs::u8path(f.temporary),"new"+std::to_string(i++));}}
static void NativeCases(const fs::path& root){
    for(auto mode:{Schedule::Deferred,Schedule::Immediate,Schedule::NoList,Schedule::NoJob,Schedule::Rejected}){
        Populate(root);schedule=mode;sessionSaveCommitJob_t commit;commit.files=NativeFiles(root);
        const bool accepted=Session_QueueCheckpointCommit(commit);
        Check(accepted==(mode==Schedule::Deferred || mode==Schedule::Immediate));
        if(accepted)commit.jobs->Wait();
        Check(commit.result.committed==accepted);
        int i=0;for(auto& f:commit.files)Check(Get(fs::u8path(f.final))==(accepted?"new":"old")+std::to_string(i++));
    }
    Populate(root);schedule=Schedule::Deferred;
    {sessionSaveCommitJob_t commit;commit.files=NativeFiles(root);Check(Session_QueueCheckpointCommit(commit));}
    for(int i=0;i<3;++i)Check(Get(fs::u8path(NativeFiles(root)[i].final))=="new"+std::to_string(i));
    Populate(root);sessionSaveCommitFileOps_t ops;std::string error;bool exists=false;
    Check(!ops.Exists(Utf8(root),exists,error)); // never treat a directory as a missing/regular file
    const auto f=NativeFiles(root);Check(!ops.Move(f[0].temporary,f[0].final,error));Check(Get(fs::u8path(f[0].final))=="old0");
    // fs_savepath may be relative to the working directory.
    const auto relativeTemp=fs::relative(fs::u8path(f[2].temporary)),relativeBackup=fs::relative(fs::u8path(f[2].backup));
    if(!relativeTemp.empty()){std::string unconfirmed;Check(Session_MoveSaveFileDurably(Utf8(relativeTemp),Utf8(relativeBackup),unconfirmed,error));Check(Get(fs::u8path(f[2].backup))=="new2");}
    for(bool wait:{false,true}){
        Populate(root);schedule=Schedule::Deferred;int completions=0;idSessionLocal session;
        sessionPendingCheckpoint=[&](){
            auto commit=std::make_shared<sessionSaveCommitJob_t>();commit->files=NativeFiles(root);Check(Session_QueueCheckpointCommit(*commit));
            sessionPendingCheckpointCommit=commit;
            sessionPendingCheckpoint=[&,commit](){Check(commit->result.committed);++completions;return true;};return true;
        };
        const bool done=session.FinishPendingSave(wait);Check(done==wait && completions==(wait?1:0));
        while(sessionPendingCheckpoint)session.FinishPendingSave(false);
        Check(completions==1 && !sessionPendingCheckpointCommit && !sessionFinishingPendingSave);
        Check(wait?session.redraws>0:session.redraws==0);
    }
    Clean(root);
}
int main(int argc,char** argv){
    Check(argc==2);const auto root=fs::u8path(argv[1]);Check(root.is_absolute() && root.filename()=="checkpoint-commit-test" && root.parent_path().filename()==".tmp");
    Check(!fs::is_symlink(fs::symlink_status(root)));fs::create_directories(root);
    TransactionCases();StagingCases();GuardCases();NativeCases(root);Check(fs::is_empty(root));fs::remove(root);
    std::printf("Checkpoint commit: %d publication/rollback/recovery, native path, scheduler and two-stage polling checks passed\n",checks);
}
