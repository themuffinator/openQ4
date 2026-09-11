// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Standalone: compile this source alone. Includes unchanged production I/O with
// compile-time-only fault points; all noninjected calls use real native files.
#define OPENQ4_DURABLE_FILE_TESTING
#include "src/framework/DurableFile.cpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;
using namespace openq4;
using namespace openq4::durable_detail;
namespace {
std::string error;
int checks = 0;
std::string executable;
void Check(bool okay, const char* message) {
    ++checks;
    if (!okay) { std::cerr << "FAIL: " << message << ": " << error << '\n'; std::exit(1); }
}
std::string Utf8Path(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
void Arm(Point point, int call = 1) { faults = {}; faults.point = point; faults.failOn = call; }
void Reset() { faults = {}; }
void Put(const std::string& path, const std::string& bytes) {
    Reset(); Check(DurableReplaceExact(path,bytes,error),"write fixture");
}
std::string Get(const std::string& path) {
    Reset(); std::string value;
    Check(DurableReadExact(path,DurableFileMaxBytes,value,error) == DurableReadResult::Present,"read fixture");
    return value;
}
fs::path NativePath(const std::string& bytes) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(bytes.data()),bytes.size()));
}
void RawPut(const fs::path& path, const std::string& bytes) {
    std::ofstream file(path,std::ios::binary | std::ios::trunc);
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    file.close(); Check(!file.fail(),"raw fixture write");
}
std::string creationRacePath;
void CreateCompetitor() { RawPut(NativePath(creationRacePath),"external racing creator"); }
#ifndef _WIN32
std::string mutationPath;
void Grow() { RawPut(NativePath(mutationPath),"abcdefghijk"); }
void Shrink() { RawPut(NativePath(mutationPath),"a"); }
#endif

void ReadCases(const fs::path& dir) {
    const auto path = Utf8Path(dir / NativePath("journal-\xc3\xa9-\xe6\xb0\xb4.json"));
    std::string output = "unchanged";
    error = "old";
    Check(DurableReadExact(path,1024,output,error) == DurableReadResult::Missing &&
        output == "unchanged" && error.empty(),"missing is distinct and output preserving");
    const std::string bytes("{\0UTF8:\xc3\xa9\n}",12);
    Put(path,bytes);
    Check(DurableReadExact(path,bytes.size(),output,error) == DurableReadResult::Present &&
        output == bytes && error.empty(),"exact binary and unicode path round trip");
    output = "unchanged";
    Check(DurableReadExact(path,bytes.size()-1,output,error) == DurableReadResult::Failed &&
        output == "unchanged" && !error.empty(),"read size budget rejects without publication");
    Check(DurableReadExact(path,DurableFileMaxBytes+1,output,error) == DurableReadResult::Failed &&
        output == "unchanged","global read budget enforced");
    faults.readChunk = 2;
    Check(DurableReadExact(path,1024,output,error) == DurableReadResult::Present && output == bytes,
        "short read chunks accumulate exact bytes");
    for (const auto point : {Point::OpenRead,Point::Read,Point::CloseRead}) {
        output = "unchanged"; Arm(point);
        Check(DurableReadExact(path,1024,output,error) == DurableReadResult::Failed &&
            output == "unchanged" && !error.empty(),"read/open/close failure leaves output unchanged");
        Reset();
    }
    Arm(Point::Read,2); output = "unchanged";
    Check(DurableReadExact(path,1024,output,error) == DurableReadResult::Failed &&
        output == "unchanged","trailing EOF read failure cannot publish");
    Reset(); faults.zeroRead = true;
    Check(DurableReadExact(path,1024,output,error) == DurableReadResult::Failed &&
        output == "unchanged","early EOF rejected");
    Reset();
#ifndef _WIN32
    // Windows denies write sharing while reading. On POSIX detect size changes
    // to an already-open regular file as failures rather than truncated success.
    for (const auto change : {&Grow,&Shrink}) {
        Put(path,"abcdef"); mutationPath = path; faults.beforeRead = change;
        output = "unchanged";
        Check(DurableReadExact(path,1024,output,error) == DurableReadResult::Failed &&
            output == "unchanged","concurrent size change rejected");
        Reset();
    }
#else
    Handle lock(CreateFileW(NativePath(path).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr));
    Check(lock.value != Invalid,"exclusive external reader fixture");
    Check(DurableReadExact(path,1024,output,error) == DurableReadResult::Failed && !error.empty(),
        "sharing failure is not missing");
    Check(lock.Close(error,Point::CloseRead),"close sharing fixture");
#endif
    Put(path,""); output = "old";
    Check(DurableReadExact(path,0,output,error) == DurableReadResult::Present && output.empty(),"zero budget reads empty file");
    Check(DurableRemoveExact(path,error),"remove unicode file");
}

void ReplaceCases(const fs::path& dir) {
    const auto path = Utf8Path(dir / "replace.json");
    const std::string old = "original authoritative bytes";
    std::string large(ChunkBytes * 3 + 17,'x');
    for (std::size_t i = 0; i < large.size(); ++i) large[i] = static_cast<char>(i % 251);
    Put(path,old);
    faults.writeChunk = 7;
    Check(DurableReplaceExact(path,large,error),"short writes loop through complete replacement");
    Check(Get(path) == large,"multi-chunk exact binary replacement");
    for (const auto point : {Point::OpenTemp,Point::Write,Point::FileSync,Point::CloseWrite,Point::Rename}) {
        Put(path,old); Arm(point);
        Check(!DurableReplaceExact(path,large,error) && !error.empty(),"pre-publication operation failure reported");
        Check(Get(path) == old,"pre-publication failure preserves authoritative bytes");
    }
    Put(path,old); Arm(Point::Write,2);
    Check(!DurableReplaceExact(path,large,error),"failure after a real partial write reported");
    Check(Get(path) == old,"partial temp write never becomes authoritative");
    Put(path,old); faults.zeroWrite = true;
    Check(!DurableReplaceExact(path,large,error),"zero write cannot spin or succeed");
    Check(Get(path) == old,"zero write preserves final");
    const std::string excessive(DurableFileMaxBytes+1,'z');
    Check(!DurableReplaceExact(path,excessive,error) && !error.empty(),"global replacement bound enforced");
    Check(Get(path) == old,"oversize write preserves final");
    Arm(Point::MetadataSync);
    Check(!DurableReplaceExact(path,"published-but-unconfirmed",error) &&
        error.find("durability") != std::string::npos,"post-rename uncertainty reported as failure");
    Check(Get(path) == "published-but-unconfirmed","post-rename failure does not pretend old file survived");
    Put(path,"retry"); Check(Get(path) == "retry","retry after uncertain publication works");

    // Collision names are not ours. The implementation may try only 32 names,
    // must preserve every colliding file, then fail without changing the target.
    std::vector<fs::path> collisions;
    const auto sequence = tempSequence.load();
#ifdef _WIN32
    const auto pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    const auto pid = static_cast<unsigned long>(getpid());
#endif
    for (unsigned i = 0; i < TempAttempts; ++i) {
        const auto file = dir / (".openq4-durable-" + std::to_string(pid) + "-" + std::to_string(sequence+i) + ".tmp");
        RawPut(file,"unowned"); collisions.push_back(file);
    }
    Check(!DurableReplaceExact(path,"unpublished",error) && error.find("collision") != std::string::npos,
        "temporary-name retries are bounded");
    Check(Get(path) == "retry","collision exhaustion preserves final");
    for (const auto& file : collisions) {
        Check(Get(Utf8Path(file)) == "unowned","collision content preserved");
        Check(fs::remove(file),"remove exact collision fixture");
    }
    Check(DurableRemoveExact(path,error),"remove replacement fixture");
}

void RemoveCases(const fs::path& dir) {
    const auto path = Utf8Path(dir / "remove.json");
    Put(path,"original"); Arm(Point::Remove);
    Check(!DurableRemoveExact(path,error) && !error.empty(),"removal failure reported");
    Check(Get(path) == "original","pre-removal failure preserves original");
#ifdef _WIN32
    for (const auto point : {Point::OpenTemp,Point::CloseWrite,Point::Rename}) {
        Arm(point); Check(!DurableRemoveExact(path,error),"pre-removal tombstone failure reported");
        Check(Get(path) == "original","pre-removal tombstone failure preserves original");
    }
#endif
    Arm(Point::MetadataSync);
    Check(!DurableRemoveExact(path,error) && error.find("durability") != std::string::npos,
        "post-removal namespace sync failure reported");
    Reset(); std::string output = "unchanged";
    Check(DurableReadExact(path,1024,output,error) == DurableReadResult::Missing && output == "unchanged",
        "failed namespace barrier can already have removed target");
    Check(DurableRemoveExact(path,error) && error.empty(),"retry removal is idempotent");
#ifdef _WIN32
    Put(path,"original"); Arm(Point::Cleanup);
    Check(!DurableRemoveExact(path,error) && error.find("cleanup failed") != std::string::npos,
        "tombstone cleanup refusal is reported after durable removal");
    Reset();
    Check(DurableReadExact(path,1024,output,error) == DurableReadResult::Missing,"cleanup failure never resurrects target");
#endif
    Put(path,"again"); Check(DurableRemoveExact(path,error),"checked actual removal succeeds");
    Check(DurableRemoveExact(path,error),"already missing removal succeeds");
}

void PathCases(const fs::path& dir) {
    std::string output = "unchanged";
    const auto good = Utf8Path(dir / "path.json");
    for (const auto& bad : std::vector<std::string>{"relative.json",good+"/",good+"/../other",good+"/./other",
        good+std::string("\0suffix",7),good+"\xc0\xaf",std::string(MaxPathBytes+1,'a')}) {
        Check(DurableReadExact(bad,1024,output,error) == DurableReadResult::Failed && output == "unchanged",
            "invalid path read rejected");
        Check(!DurableReplaceExact(bad,"bad",error),"invalid path write rejected");
        Check(!DurableRemoveExact(bad,error),"invalid path remove rejected");
    }
    const auto directory = dir / "not-a-file";
    Check(fs::create_directory(directory),"directory fixture");
    const auto directoryString = Utf8Path(directory);
    Check(DurableReadExact(directoryString,1024,output,error) == DurableReadResult::Failed,"directory read rejected");
    Check(!DurableReplaceExact(directoryString,"bad",error),"directory replacement rejected");
    Check(!DurableRemoveExact(directoryString,error) && fs::is_directory(directory),"directory removal rejected");
    Check(fs::remove(directory),"remove own empty directory fixture");
    const auto missingParent = Utf8Path(dir / "absent-parent" / "journal.json");
    Check(DurableReadExact(missingParent,1024,output,error) == DurableReadResult::Missing,"absent parent means missing read");
    Check(!DurableReplaceExact(missingParent,"bad",error),"replacement does not create parent directories");
    Put(good,"safe");
    const auto linked = dir / "leaf-link";
    std::error_code symlinkError;
    fs::create_symlink(NativePath(good),linked,symlinkError);
    if (!symlinkError) {
        const auto link = Utf8Path(linked);
        Check(DurableReadExact(link,1024,output,error) == DurableReadResult::Failed,"symlink read rejected");
        Check(!DurableReplaceExact(link,"bad",error),"symlink write rejected");
        Check(!DurableRemoveExact(link,error),"symlink remove rejected");
        Check(Get(good) == "safe","symlink target untouched");
        Check(fs::remove(linked),"remove exact symlink fixture");
    } else {
#ifndef _WIN32
        Check(false,"POSIX symlink fixture must be supported");
#else
        std::cout << "SKIP: Windows symlink creation unavailable (" << symlinkError.message() << ")\n";
#endif
    }
#ifndef _WIN32
    const auto fifo = Utf8Path(dir / "fifo");
    if (mkfifo(fifo.c_str(),0600) == 0) {
        Check(DurableReadExact(fifo,1024,output,error) == DurableReadResult::Failed,"FIFO rejected without blocking");
        Check(!DurableReplaceExact(fifo,"bad",error),"FIFO replacement rejected");
        Check(!DurableRemoveExact(fifo,error),"FIFO removal rejected");
        Check(unlink(fifo.c_str()) == 0,"remove exact FIFO fixture");
    } else {
        Check(errno == EOPNOTSUPP || errno == ENOTSUP,"FIFO fixture failed for an unexpected reason");
        std::cout << "SKIP: test filesystem does not support FIFOs\n";
    }
#else
    for (const auto& bad : {good+":stream",good+".",good+" ",std::string("\\\\.\\NUL"),std::string("C:relative")})
        Check(!DurableReplaceExact(bad,"bad",error),"Windows device/stream/ambiguous path rejected");
#endif
    Check(DurableRemoveExact(good,error),"remove path fixture");
}

struct Child {
#ifdef _WIN32
    PROCESS_INFORMATION process{};
#else
    pid_t pid = -1;
    bool completed = false;
    int exitCode = 0;
#endif
    void Start(const std::vector<std::string>& arguments) {
#ifdef _WIN32
        const auto app = NativePath(executable).wstring();
        std::wstring command = L"\"" + app + L"\"";
        for (const auto& arg : arguments) command += L" \"" + NativePath(arg).wstring() + L"\"";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        Check(CreateProcessW(app.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
            nullptr,nullptr,&startup,&process) != FALSE,"launch isolated lease child without a visible window");
#else
        pid = fork(); Check(pid >= 0,"fork isolated lease child");
        if (pid == 0) {
            std::vector<char*> args{const_cast<char*>(executable.c_str())};
            for (const auto& arg : arguments) args.push_back(const_cast<char*>(arg.c_str()));
            args.push_back(nullptr);
            execv(executable.c_str(),args.data()); _exit(127);
        }
#endif
    }
    bool Exited(int& code) {
#ifdef _WIN32
        if (WaitForSingleObject(process.hProcess,0) != WAIT_OBJECT_0) return false;
        DWORD result = 0; Check(GetExitCodeProcess(process.hProcess,&result) != FALSE,"read child exit code");
        code = static_cast<int>(result); return true;
#else
        if (!completed) {
            int status = 0;
            const auto result = waitpid(pid,&status,WNOHANG);
            Check(result >= 0,"wait for lease child");
            if (result == 0) return false;
            completed = true; exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }
        code = exitCode; return true;
#endif
    }
    int Wait() {
        for (int i = 0; i < 500; ++i) {
            int code; if (Exited(code)) return code;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(false,"child completion exceeded five seconds"); return -1;
    }
    void Crash() {
#ifdef _WIN32
        Check(TerminateProcess(process.hProcess,99) != FALSE,"terminate only the spawned lease holder");
#else
        Check(kill(pid,SIGKILL) == 0,"terminate only the spawned lease holder");
#endif
    }
    ~Child() {
#ifdef _WIN32
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
#endif
    }
};

void CreateCases(const fs::path& dir) {
    const auto path=Utf8Path(dir / NativePath("new-\xc3\xa9-\xe6\xb0\xb4.q4ui"));
    std::string bytes(ChunkBytes*3+17,'x');
    for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<char>(i%251);
    Reset();faults.writeChunk=7;
    Check(DurableCreateExact(path,bytes,error)==DurableCreateResult::Created && error.empty(),"new complete Unicode/binary file published");
    Check(Get(path)==bytes,"new multi-chunk bytes match exactly");
    error="old";
    Check(DurableCreateExact(path,"replacement",error)==DurableCreateResult::Exists && error.empty(),"existing file is an ordinary creation collision");
    Check(Get(path)==bytes,"collision never overwrites existing document");
    Check(DurableRemoveExact(path,error),"remove exact creation fixture");
    for(const auto point:{Point::OpenTemp,Point::Write,Point::FileSync,Point::CloseWrite,Point::CreatePublication}) {
        Arm(point);
        Check(DurableCreateExact(path,bytes,error)==DurableCreateResult::Failed && !error.empty(),"new-file prepublication failure reported");
        Check(!fs::exists(NativePath(path)),"no partial new file becomes visible");
        Reset();
    }
    Arm(Point::Write,2);
    Check(DurableCreateExact(path,bytes,error)==DurableCreateResult::Failed && !fs::exists(NativePath(path)),"partial new-file write remains private");
    Reset();faults.zeroWrite=true;
    Check(DurableCreateExact(path,bytes,error)==DurableCreateResult::Failed && !fs::exists(NativePath(path)),"zero-progress new-file write refuses");
    Reset();
    Check(DurableCreateExact(path,std::string(DurableFileMaxBytes+1,'z'),error)==DurableCreateResult::Failed &&
        !fs::exists(NativePath(path)),"new-file total byte bound precedes effects");
    Check(DurableCreateExact("relative.q4ui",bytes,error)==DurableCreateResult::Failed,"new-file path must be absolute");
    Check(DurableCreateExact(Utf8Path(dir / "missing-parent" / "file"),bytes,error)==DurableCreateResult::Failed &&
        !fs::exists(dir / "missing-parent"),"new-file operation never creates parent directories");
    creationRacePath=path;faults.beforeCreatePublication=CreateCompetitor;
    Check(DurableCreateExact(path,bytes,error)==DurableCreateResult::Exists && error.empty(),"creator after preflight wins without overwrite");
    Check(Get(path)=="external racing creator","late external bytes remain intact");
    Check(DurableRemoveExact(path,error),"remove late creator fixture");
    Arm(Point::MetadataSync);
    Check(DurableCreateExact(path,bytes,error)==DurableCreateResult::Failed && !error.empty(),"published new-file durability failure is not success");
    Check(Get(path)==bytes,"uncertain publication retains complete new bytes");
    Check(DurableCreateExact(path,"retry",error)==DurableCreateResult::Exists && Get(path)==bytes,"retry after uncertain publication cannot replace it");
    Check(DurableRemoveExact(path,error),"remove uncertain new-file fixture");
#ifndef _WIN32
    Arm(Point::Cleanup);
    Check(DurableCreateExact(path,bytes,error)==DurableCreateResult::Failed && !error.empty(),"published link cleanup failure is uncertain");
    Check(Get(path)==bytes,"temporary link cleanup never removes new target");
    Check(DurableRemoveExact(path,error),"remove cleanup-refusal fixture");
#endif
    Reset();
    Check(DurableCreateExact(path,"",error)==DurableCreateResult::Created && Get(path).empty(),"empty new file publishes exactly");
    Check(DurableRemoveExact(path,error),"remove empty new-file fixture");
    const auto directory=dir / "create-directory";
    Check(fs::create_directory(directory),"reserve creation directory collision");
    Check(DurableCreateExact(Utf8Path(directory),bytes,error)!=DurableCreateResult::Created && fs::is_directory(directory),"directory collision stays intact");
    Check(fs::remove(directory),"remove empty creation directory collision");
    const auto target=dir / "create-link-target",link=dir / "create-link";
    RawPut(target,"linked external work");
    std::error_code linkError;fs::create_symlink(target,link,linkError);
    if(!linkError) {
        Check(DurableCreateExact(Utf8Path(link),bytes,error)!=DurableCreateResult::Created,"new-file operation refuses leaf symlink");
        Check(Get(Utf8Path(target))=="linked external work","linked source is unchanged");
        Check(fs::remove(link),"remove exact creation link fixture");
    } else std::cout << "SKIP new-file symlink fixture: " << linkError.message() << '\n';
    Check(fs::remove(target),"remove exact creation link target");

    // Real concurrent native publications. No application lease or mutex
    // serializes creators; exactly one complete payload can own the name.
    constexpr unsigned count=8;
    std::atomic<unsigned> waiting{0};std::atomic<bool> start{false};
    std::vector<DurableCreateResult> results(count,DurableCreateResult::Failed);
    std::vector<std::string> errors(count),payloads;
    std::vector<std::thread> threads;
    for(unsigned i=0;i<count;++i)payloads.push_back(std::string(ChunkBytes*4+11,char('A'+i)));
    for(unsigned i=0;i<count;++i)threads.emplace_back([&,i]{
        waiting.fetch_add(1);while(!start.load())std::this_thread::yield();
        results[i]=DurableCreateExact(path,payloads[i],errors[i]);
    });
    while(waiting.load()!=count)std::this_thread::yield();
    start.store(true);
    for(auto& thread:threads)thread.join();
    unsigned created=0,winner=0;
    for(unsigned i=0;i<count;++i) {
        Check(errors[i].empty() && (results[i]==DurableCreateResult::Created || results[i]==DurableCreateResult::Exists),"native contenders return created or collision");
        if(results[i]==DurableCreateResult::Created){++created;winner=i;}
    }
    Check(created==1 && Get(path)==payloads[winner],"one native contender publishes one complete payload");
    Check(DurableRemoveExact(path,error),"remove concurrent thread fixture");
    // The standalone narrow-main child harness has no Windows Unicode argv
    // adapter; Unicode paths are covered above through the actual UTF-8 API.
    const auto processPath=Utf8Path(dir / "process-create.q4ui");
    Child first,second;first.Start({"--create-exact",processPath,"first-process"});second.Start({"--create-exact",processPath,"second-process"});
    const int a=first.Wait(),b=second.Wait();
    std::cout << "Independent create results: " << a << ',' << b << '\n';
    Check((a==0 && b==2)||(a==2 && b==0),"one independent process wins creation");
    Check(Get(processPath)==(a==0?"first-process":"second-process"),"independent winner bytes remain exact");
    Check(DurableRemoveExact(processPath,error),"remove independent process fixture");
}

void LeaseCases(const fs::path& dir) {
    const auto path = Utf8Path(dir / "settings-recovery.lock");
    DurableFileLease first, second;
    Check(!first.IsHeld() && !second.IsHeld(),"new leases are unheld");
    Check(first.TryAcquire(path,error) && first.IsHeld() && error.empty(),"first lease acquires exact path");
    Check(!second.TryAcquire(path,error) && !second.IsHeld() && !error.empty(),"independent lease instance refuses busy path");
    Check(!first.TryAcquire(Utf8Path(dir / "different.lock"),error) && first.IsHeld(),
        "already-held acquisition refuses without releasing owner");
    Check(!fs::exists(dir / "different.lock"),"already-held attempt has no other path effects");
    {
        Child child; child.Start({"--lease-probe",path});
        Check(child.Wait() == 0,"second process refuses lease held by parent");
    }
    first.Release(); Check(!first.IsHeld() && fs::is_regular_file(NativePath(path)),
        "release leaves fixed inert lock file in place");
    Check(second.TryAcquire(path,error),"peer reacquires after explicit release");
    second.Release(); second.Release();
    {
        DurableFileLease scoped;
        Check(scoped.TryAcquire(path,error),"scoped lease acquisition");
    }
    Check(first.TryAcquire(path,error),"destructor releases lease");
    first.Release();
    {
        const auto marker = Utf8Path(dir / "child-ready.txt");
        Child child; child.Start({"--lease-hold",path,marker});
        for (int i = 0; i < 500 && !fs::exists(NativePath(marker)); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Check(fs::exists(NativePath(marker)),"separate process acquired lease and signaled readiness");
        Check(!first.TryAcquire(path,error),"parent cannot acquire child's live lease");
        child.Crash(); Check(child.Wait() != 0,"lease holder terminated without RAII cleanup");
        Check(first.TryAcquire(path,error),"OS releases lock after holder crash");
        first.Release(); Check(fs::remove(NativePath(marker)),"remove exact child marker");
    }
    Check(!first.TryAcquire("relative.lock",error),"lease rejects relative path");
    const auto directory = dir / "lock-directory";
    Check(fs::create_directory(directory),"lease directory fixture");
    Check(!first.TryAcquire(Utf8Path(directory),error) && !first.IsHeld(),"lease rejects nonregular file");
    Check(fs::remove(directory),"remove empty lease directory fixture");
    const auto linked = dir / "lock-link";
    std::error_code symlinkError;
    fs::create_symlink(NativePath(path),linked,symlinkError);
    if (!symlinkError) {
        Check(!first.TryAcquire(Utf8Path(linked),error),"lease rejects leaf symlink");
        Check(fs::remove(linked),"remove exact lease link fixture");
    }
    Check(fs::remove(NativePath(path)),"test cleanup removes exact unheld lock fixture");
}
} // namespace

int main(int argc, char** argv) {
    if (argc==4 && std::string(argv[1])=="--create-exact") {
        const auto result=DurableCreateExact(argv[2],argv[3],error);
        return result==DurableCreateResult::Created?0:result==DurableCreateResult::Exists?2:3;
    }
    if (argc == 3 && std::string(argv[1]) == "--lease-probe") {
        DurableFileLease probe;
        return !probe.TryAcquire(argv[2],error) && !error.empty() ? 0 : 9;
    }
    if (argc == 4 && std::string(argv[1]) == "--lease-hold") {
        DurableFileLease holder;
        if (!holder.TryAcquire(argv[2],error)) return 10;
        RawPut(NativePath(argv[3]),"ready");
        // Parent kills this process to exercise OS cleanup after a crash.
        // A finite timeout prevents a stranded process if the parent test fails.
        std::this_thread::sleep_for(std::chrono::seconds(30)); return 11;
    }
    Check(argc == 2,"pass one existing absolute test scratch directory");
    executable = Utf8Path(fs::absolute(NativePath(argv[0])));
    const fs::path root = NativePath(argv[1]);
    Check(root.is_absolute() && fs::is_directory(root),"scratch root is absolute existing directory");
    fs::path dir;
    bool created = false;
    for (unsigned attempt = 0; attempt < 32 && !created; ++attempt) {
        dir = root / TempLeaf();
        created = fs::create_directory(dir);
    }
    Check(created,"reserve isolated test directory");
    std::cout << "DurableFileTest scratch: " << Utf8Path(dir) << '\n';
    ReadCases(dir); ReplaceCases(dir); CreateCases(dir); RemoveCases(dir); PathCases(dir); LeaseCases(dir);
    // Only exact regular files created inside this exclusive, nonrecursive test
    // directory remain (uncertain Windows tombstones). Preserve unexpected data.
    for (const auto& entry : fs::directory_iterator(dir)) {
        const auto name = entry.path().filename().string();
        Check(entry.is_regular_file() && name.rfind(".openq4-durable-",0) == 0 &&
            entry.path().parent_path() == dir,"only owned inert tombstones remain");
        Check(fs::remove(entry.path()),"remove exact inert test tombstone");
    }
    Check(fs::remove(dir),"remove empty owned test directory");
    std::cout << "DurableFileTest passed: " << checks << " checks (real native I/O plus fault injection)\n";
    return 0;
}
