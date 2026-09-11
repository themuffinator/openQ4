// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "DurableFile.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace openq4 {
namespace durable_detail {

constexpr std::size_t MaxPathBytes = 32700;
constexpr std::size_t ChunkBytes = 64 * 1024;
constexpr unsigned TempAttempts = 32;

// Compile-time-only fault injection. The standalone test includes this source;
// engine builds contain no mutable hook or alternate operating-system backend.
enum class Point { OpenRead, Read, OpenTemp, Write, FileSync, CloseRead,
    CloseWrite, Rename, MetadataSync, Remove, Cleanup, CreatePublication };
#ifdef OPENQ4_DURABLE_FILE_TESTING
struct Faults {
    Point point = Point::OpenRead;
    int failOn = 0, calls = 0;
    std::size_t readChunk = ChunkBytes, writeChunk = ChunkBytes;
    bool zeroRead = false, zeroWrite = false;
    void (*beforeRead)() = nullptr;
    void (*beforeCreatePublication)() = nullptr;
};
thread_local Faults faults;
bool Fail(Point point) {
    return faults.failOn > 0 && point == faults.point && ++faults.calls == faults.failOn;
}
#else
bool Fail(Point) { return false; }
#endif

bool Error(std::string& error, const char* operation, unsigned long code = 0) {
    error = std::string(operation) + (code ? " (OS error " + std::to_string(code) + ")" : "");
    return false;
}

bool Utf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first == 0) return false;
        if (first < 0x80) continue;
        unsigned left;
        std::uint32_t value, minimum;
        if (first >= 0xc2 && first <= 0xdf) { left = 1; value = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { left = 2; value = first & 0xf; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { left = 3; value = first & 7; minimum = 0x10000; }
        else return false;
        if (text.size() - i < left) return false;
        while (left--) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

struct Path {
#ifdef _WIN32
    std::wstring full, parent, leaf;
#else
    std::string full, parent, leaf;
#endif
};

bool ParsePath(const std::string& input, Path& path, std::string& error) {
    if (input.empty() || input.size() > MaxPathBytes || !Utf8(input))
        return Error(error,"Invalid or overlong UTF-8 path");
    std::string normalized = input;
    std::size_t root = 1;
#ifdef _WIN32
    std::replace(normalized.begin(),normalized.end(),'\\','/');
    const bool drive = normalized.size() > 3 &&
        ((normalized[0] >= 'A' && normalized[0] <= 'Z') ||
         (normalized[0] >= 'a' && normalized[0] <= 'z')) &&
        normalized[1] == ':' && normalized[2] == '/';
    const bool unc = normalized.size() > 4 && normalized[0] == '/' && normalized[1] == '/';
    if (!drive && !unc) return Error(error,"Path must be absolute");
    root = drive ? 3 : 2;
#else
    if (normalized[0] != '/') return Error(error,"Path must be absolute");
#endif
#ifdef _WIN32
    unsigned components = 0;
#endif
    for (std::size_t begin = root; begin <= normalized.size();) {
        const auto end = normalized.find('/',begin);
        const auto count = (end == std::string::npos ? normalized.size() : end) - begin;
        const auto part = normalized.substr(begin,count);
        if (part.empty() || part == "." || part == "..")
            return Error(error,"Path must contain a file and no empty or dot components");
#ifdef _WIN32
        if (part.back() == '.' || part.back() == ' ' ||
            std::any_of(part.begin(),part.end(),[](unsigned char c) {
                return c < 32 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
            })) return Error(error,"Windows path contains a device, stream or ambiguous component");
#endif
#ifdef _WIN32
        ++components;
#endif
        if (end == std::string::npos) break;
        begin = end + 1;
    }
#ifdef _WIN32
    if (unc && components < 3) return Error(error,"UNC path must contain server, share and file");
    std::replace(normalized.begin(),normalized.end(),'/','\\');
    const int count = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,normalized.data(),
        static_cast<int>(normalized.size()),nullptr,0);
    if (count <= 0) return Error(error,"UTF-8 path conversion failed",GetLastError());
    std::wstring wide(static_cast<std::size_t>(count),L'\0');
    if (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,normalized.data(),
        static_cast<int>(normalized.size()),wide.data(),count) != count)
        return Error(error,"UTF-8 path conversion failed",GetLastError());
    path.full = drive ? L"\\\\?\\" + wide : L"\\\\?\\UNC\\" + wide.substr(2);
    const auto split = path.full.rfind(L'\\');
    path.parent = path.full.substr(0,split + 1);
    path.leaf = path.full.substr(split + 1);
#else
    path.full = normalized;
    const auto split = normalized.rfind('/');
    path.parent = split == 0 ? "/" : normalized.substr(0,split);
    path.leaf = normalized.substr(split + 1);
#endif
    return true;
}

#ifdef _WIN32
using Native = HANDLE;
const Native Invalid = INVALID_HANDLE_VALUE;
unsigned long LastError() { return GetLastError(); }
bool MissingError(unsigned long code) { return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND; }
void CloseRaw(Native handle) { CloseHandle(handle); }
#else
using Native = int;
constexpr Native Invalid = -1;
unsigned long LastError() { return static_cast<unsigned long>(errno); }
bool MissingError(unsigned long code) { return code == ENOENT; }
void CloseRaw(Native handle) { close(handle); }
#endif

struct Handle {
    Native value = Invalid;
    Handle() = default;
    explicit Handle(Native v) : value(v) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value != Invalid) CloseRaw(value); }
    bool Close(std::string& error, Point point) {
        if (value == Invalid) return true;
        const auto v = value; value = Invalid;
#ifdef _WIN32
        const bool okay = CloseHandle(v) != FALSE;
#else
        // Do not retry close(EINTR): on Linux the descriptor has been released
        // and could already belong to another thread. Treat its state as failed.
        const bool okay = close(v) == 0;
#endif
        const auto code = okay ? 0 : LastError();
        if (!okay || Fail(point)) return Error(error,"File close failed",code);
        return true;
    }
};

bool Regular(Native file, std::uint64_t& size, std::string& error) {
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file,&info))
        return Error(error,"Cannot inspect regular file",GetLastError());
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        return Error(error,"Target is not a regular file");
    size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
#else
    struct stat info{};
    if (fstat(file,&info) != 0) return Error(error,"Cannot inspect regular file",errno);
    if (!S_ISREG(info.st_mode) || info.st_size < 0) return Error(error,"Target is not a regular file");
    size = static_cast<std::uint64_t>(info.st_size);
#endif
    return true;
}

bool ReadChunk(Native file, char* bytes, std::size_t request, std::size_t& count, std::string& error) {
    if (Fail(Point::Read)) return Error(error,"File read failed (injected)");
#ifdef OPENQ4_DURABLE_FILE_TESTING
    if (faults.zeroRead) { count = 0; return true; }
    request = (std::min)(request,faults.readChunk);
    if (faults.beforeRead) { auto callback = faults.beforeRead; faults.beforeRead = nullptr; callback(); }
#endif
#ifdef _WIN32
    DWORD got = 0;
    if (!ReadFile(file,bytes,static_cast<DWORD>(request),&got,nullptr))
        return Error(error,"File read failed",GetLastError());
    count = got;
#else
    ssize_t got;
    do { got = read(file,bytes,request); } while (got < 0 && errno == EINTR);
    if (got < 0) return Error(error,"File read failed",errno);
    count = static_cast<std::size_t>(got);
#endif
    return true;
}

bool WriteAll(Native file, const std::string& bytes, std::string& error) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        auto request = (std::min)(ChunkBytes,bytes.size()-offset);
        if (Fail(Point::Write)) return Error(error,"File write failed (injected)");
#ifdef OPENQ4_DURABLE_FILE_TESTING
        if (faults.zeroWrite) return Error(error,"File write made no progress");
        request = (std::min)(request,faults.writeChunk);
#endif
#ifdef _WIN32
        DWORD wrote = 0;
        if (!WriteFile(file,bytes.data()+offset,static_cast<DWORD>(request),&wrote,nullptr))
            return Error(error,"File write failed",GetLastError());
#else
        ssize_t wrote;
        do { wrote = write(file,bytes.data()+offset,request); } while (wrote < 0 && errno == EINTR);
        if (wrote < 0) return Error(error,"File write failed",errno);
#endif
        if (wrote == 0) return Error(error,"File write made no progress");
        offset += static_cast<std::size_t>(wrote);
    }
    return true;
}

bool Sync(Native file, Point point, std::string& error) {
    if (Fail(point)) return Error(error,point == Point::MetadataSync ?
        "Namespace durability could not be confirmed (injected)" : "File sync failed (injected)");
#ifdef _WIN32
    if (!FlushFileBuffers(file)) return Error(error,"File flush failed",GetLastError());
#else
    int result;
    do { result = fsync(file); } while (result != 0 && errno == EINTR);
    if (result != 0) return Error(error,point == Point::MetadataSync ?
        "Namespace durability could not be confirmed" : "File sync failed",errno);
#ifdef __APPLE__
    // Never silently downgrade a requested physical barrier to fsync alone.
    do { result = fcntl(file,F_FULLFSYNC); } while (result != 0 && errno == EINTR);
    if (result != 0) return Error(error,"Full storage sync failed; durability unconfirmed",errno);
#endif
#endif
    return true;
}

std::atomic<std::uint64_t> tempSequence{0};
std::string TempLeaf() {
#ifdef _WIN32
    const auto pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    const auto pid = static_cast<unsigned long>(getpid());
#endif
    // CREATE_NEW/O_EXCL is the ownership proof, not predictability of the name.
    // Bounded collision retries never delete a file left by a different call.
    return ".openq4-durable-" + std::to_string(pid) + "-" +
        std::to_string(tempSequence.fetch_add(1,std::memory_order_relaxed)) + ".tmp";
}

#ifndef _WIN32
bool OpenParent(const Path& path, Handle& parent, std::string& error, bool* missing = nullptr) {
    parent.value = open(path.parent.c_str(),O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent.value == Invalid) {
        const auto code = errno;
        if (missing) *missing = code == ENOENT;
        return Error(error,"Cannot open parent directory",code);
    }
    return true;
}
#endif

struct Temp {
    Handle file;
#ifdef _WIN32
    std::wstring name;
#else
    std::string name;
    Native parent = Invalid;
#endif
    bool owned = false;
    ~Temp() {
        if (!owned) return;
        if (file.value != Invalid) { CloseRaw(file.value); file.value = Invalid; }
#ifdef _WIN32
        DeleteFileW(name.c_str());
#else
        unlinkat(parent,name.c_str(),0);
#endif
        // Cleanup is best effort and never changes the authoritative target.
        // A crash/failure can leave this one inert, uniquely owned temp behind.
    }
    bool Create(const Path& path, Native parentHandle, std::string& error) {
        if (Fail(Point::OpenTemp)) return Error(error,"Temporary file creation failed (injected)");
        for (unsigned attempt = 0; attempt < TempAttempts; ++attempt) {
            const auto leaf = TempLeaf();
#ifdef _WIN32
            (void)parentHandle;
            name = path.parent + std::wstring(leaf.begin(),leaf.end());
            file.value = CreateFileW(name.c_str(),GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_DELETE,nullptr,CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH,nullptr);
            const auto code = GetLastError();
            const bool collision = code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS;
#else
            (void)path;
            parent = parentHandle;
            name = leaf;
            file.value = openat(parent,name.c_str(),O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,0600);
            const auto code = errno;
            const bool collision = code == EEXIST;
#endif
            if (file.value != Invalid) { owned = true; return true; }
            if (!collision) return Error(error,"Temporary file creation failed",code);
        }
        return Error(error,"Temporary file collision budget exhausted");
    }
};

bool InspectTarget(const Path& path, Native parent, bool& missing, std::string& error) {
    missing = false;
#ifdef _WIN32
    (void)parent;
    Handle file(CreateFileW(path.full.c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,nullptr));
    if (file.value == Invalid) {
        const auto code = GetLastError();
        if (MissingError(code)) { missing = true; return true; }
        return Error(error,"Cannot inspect target",code);
    }
    std::uint64_t size;
    return Regular(file.value,size,error);
#else
    struct stat info{};
    if (fstatat(parent,path.leaf.c_str(),&info,AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) { missing = true; return true; }
        return Error(error,"Cannot inspect target",errno);
    }
    if (!S_ISREG(info.st_mode)) return Error(error,"Target is not a regular file");
    return true;
#endif
}

DurableReadResult Read(const Path& path, std::size_t budget, std::string& out, std::string& error) {
    if (Fail(Point::OpenRead)) { Error(error,"File open failed (injected)"); return DurableReadResult::Failed; }
#ifdef _WIN32
    Handle file(CreateFileW(path.full.c_str(),GENERIC_READ,FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,nullptr));
#else
    // O_NONBLOCK avoids hanging before the regular-file check on a FIFO.
    Handle file(open(path.full.c_str(),O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
#endif
    if (file.value == Invalid) {
        const auto code = LastError();
        if (MissingError(code)) { error.clear(); return DurableReadResult::Missing; }
        Error(error,"File open failed",code); return DurableReadResult::Failed;
    }
    std::uint64_t size;
    if (!Regular(file.value,size,error)) return DurableReadResult::Failed;
    if (size > budget) { Error(error,"File exceeds read budget"); return DurableReadResult::Failed; }
    std::string candidate(static_cast<std::size_t>(size),'\0');
    std::size_t offset = 0;
    while (offset < candidate.size()) {
        std::size_t got = 0;
        if (!ReadChunk(file.value,candidate.data()+offset,(std::min)(ChunkBytes,candidate.size()-offset),got,error))
            return DurableReadResult::Failed;
        if (got == 0) { Error(error,"File shortened during read"); return DurableReadResult::Failed; }
        offset += got;
    }
    char extra;
    std::size_t got = 0;
    if (!ReadChunk(file.value,&extra,1,got,error)) return DurableReadResult::Failed;
    std::uint64_t finalSize;
    if (!Regular(file.value,finalSize,error)) return DurableReadResult::Failed;
    if (got != 0 || finalSize != size) { Error(error,"File changed size during read"); return DurableReadResult::Failed; }
    if (!file.Close(error,Point::CloseRead)) return DurableReadResult::Failed;
    out.swap(candidate); error.clear(); return DurableReadResult::Present;
}

bool Replace(const Path& path, const std::string& bytes, std::string& error) {
    Handle parent;
#ifndef _WIN32
    if (!OpenParent(path,parent,error)) return false;
#endif
    bool missing;
    if (!InspectTarget(path,parent.value,missing,error)) return false;
    Temp temp;
    if (!temp.Create(path,parent.value,error) || !WriteAll(temp.file.value,bytes,error) ||
        !Sync(temp.file.value,Point::FileSync,error) || !temp.file.Close(error,Point::CloseWrite)) return false;
    if (Fail(Point::Rename)) return Error(error,"Replacement rename failed (injected)");
#ifdef _WIN32
    if (!MoveFileExW(temp.name.c_str(),path.full.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return Error(error,"Replacement rename failed; publication/durability may be uncertain",GetLastError());
    temp.owned = false;
    if (Fail(Point::MetadataSync)) return Error(error,"Replacement published; namespace durability unconfirmed (injected)");
#else
    if (renameat(parent.value,temp.name.c_str(),parent.value,path.leaf.c_str()) != 0)
        return Error(error,"Replacement rename failed",errno);
    temp.owned = false;
    if (!Sync(parent.value,Point::MetadataSync,error)) return false;
#endif
    error.clear(); return true;
}

DurableCreateResult Create(const Path& path, const std::string& bytes, std::string& error) {
    Handle parent;
#ifndef _WIN32
    if (!OpenParent(path,parent,error)) return DurableCreateResult::Failed;
#endif
    bool missing;
    if (!InspectTarget(path,parent.value,missing,error)) return DurableCreateResult::Failed;
    if (!missing) { error.clear(); return DurableCreateResult::Exists; }
    Temp temp;
    if (!temp.Create(path,parent.value,error) || !WriteAll(temp.file.value,bytes,error) ||
        !Sync(temp.file.value,Point::FileSync,error) || !temp.file.Close(error,Point::CloseWrite))
        return DurableCreateResult::Failed;
    if (Fail(Point::CreatePublication)) {
        Error(error,"New-file publication failed (injected)"); return DurableCreateResult::Failed;
    }
#ifdef OPENQ4_DURABLE_FILE_TESTING
    if (faults.beforeCreatePublication) {
        auto callback=faults.beforeCreatePublication;faults.beforeCreatePublication=nullptr;callback();
    }
#endif
    // The earlier existence check is only an optimization. The native operation
    // itself refuses an occupied name, including one created after that check.
#ifdef _WIN32
    if (!MoveFileExW(temp.name.c_str(),path.full.c_str(),MOVEFILE_WRITE_THROUGH)) {
        const auto code=GetLastError();
        if (code==ERROR_FILE_EXISTS || code==ERROR_ALREADY_EXISTS) {
            error.clear(); return DurableCreateResult::Exists;
        }
        Error(error,"New-file publication failed; publication/durability may be uncertain",code);
        return DurableCreateResult::Failed;
    }
    temp.owned=false;
    if (Fail(Point::MetadataSync)) {
        Error(error,"New file published; namespace durability unconfirmed (injected)");
        return DurableCreateResult::Failed;
    }
#else
    if (linkat(parent.value,temp.name.c_str(),parent.value,path.leaf.c_str(),0)!=0) {
        const auto code=errno;
        if (code==EEXIST) { error.clear(); return DurableCreateResult::Exists; }
        Error(error,"New-file publication failed; publication/durability may be uncertain",code);
        return DurableCreateResult::Failed;
    }
    // Only our newly created temp has a second name. Never remove or replace
    // the target on failure; it may already be visible to another process.
    if (Fail(Point::Cleanup) || unlinkat(parent.value,temp.name.c_str(),0)!=0) {
        Error(error,"New file published; temporary cleanup/durability unconfirmed");
        return DurableCreateResult::Failed;
    }
    temp.owned=false;
    if (!Sync(parent.value,Point::MetadataSync,error)) return DurableCreateResult::Failed;
#endif
    error.clear(); return DurableCreateResult::Created;
}

bool Remove(const Path& path, std::string& error) {
    Handle parent;
#ifndef _WIN32
    bool missingParent = false;
    if (!OpenParent(path,parent,error,&missingParent)) {
        if (missingParent) { error.clear(); return true; }
        return false;
    }
#endif
    bool missing;
    if (!InspectTarget(path,parent.value,missing,error)) return false;
    if (missing) {
#ifndef _WIN32
        // Also makes a retried unlink's previously uncertain absence durable.
        if (!Sync(parent.value,Point::MetadataSync,error)) return false;
#endif
        error.clear(); return true;
    }
    if (Fail(Point::Remove)) return Error(error,"Removal failed (injected)");
#ifdef _WIN32
    // Reserve only a name we own. MoveFileEx atomically replaces the empty
    // placeholder; WRITE_THROUGH persists removal of the authoritative name.
    Temp tombstone;
    if (!tombstone.Create(path,Invalid,error) || !tombstone.file.Close(error,Point::CloseWrite)) return false;
    if (Fail(Point::Rename)) return Error(error,"Removal rename failed (injected)");
    // A failed write-through move can have an uncertain outcome. Never remove
    // the destination from a destructor once it might hold the original file.
    tombstone.owned = false;
    if (!MoveFileExW(path.full.c_str(),tombstone.name.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return Error(error,"Removal rename failed; namespace durability may be uncertain",GetLastError());
    // From here the tombstone contains the original; keep it on uncertain
    // durability for diagnosis instead of the Temp destructor discarding it.
    if (Fail(Point::MetadataSync)) return Error(error,"Target removed; namespace durability unconfirmed (injected)");
    if (Fail(Point::Cleanup)) return Error(error,"Target removed durably; tombstone cleanup failed (injected)");
    if (!DeleteFileW(tombstone.name.c_str()))
        return Error(error,"Target removed durably; tombstone cleanup failed",GetLastError());
#else
    if (unlinkat(parent.value,path.leaf.c_str(),0) != 0) return Error(error,"Removal failed",errno);
    if (!Sync(parent.value,Point::MetadataSync,error)) return false;
#endif
    error.clear(); return true;
}

} // namespace durable_detail

DurableReadResult DurableReadExact(const std::string& path, std::size_t maxBytes,
    std::string& outBytes, std::string& error) {
    using namespace durable_detail;
    try {
        Path parsed;
        if (maxBytes > DurableFileMaxBytes || !ParsePath(path,parsed,error)) {
            if (maxBytes > DurableFileMaxBytes) Error(error,"Read budget exceeds durable-file limit");
            return DurableReadResult::Failed;
        }
        return Read(parsed,maxBytes,outBytes,error);
    } catch (const std::bad_alloc&) {
        Error(error,"Durable read allocation failed"); return DurableReadResult::Failed;
    }
}

bool DurableReplaceExact(const std::string& path, const std::string& bytes, std::string& error) {
    using namespace durable_detail;
    try {
        Path parsed;
        if (bytes.size() > DurableFileMaxBytes) return Error(error,"Replacement exceeds durable-file limit");
        return ParsePath(path,parsed,error) && Replace(parsed,bytes,error);
    } catch (const std::bad_alloc&) { return Error(error,"Durable replacement allocation failed"); }
}

DurableCreateResult DurableCreateExact(const std::string& path, const std::string& bytes, std::string& error) {
    using namespace durable_detail;
    try {
        Path parsed;
        if (bytes.size()>DurableFileMaxBytes) {
            Error(error,"New file exceeds durable-file limit"); return DurableCreateResult::Failed;
        }
        if (!ParsePath(path,parsed,error)) return DurableCreateResult::Failed;
        return Create(parsed,bytes,error);
    } catch (const std::bad_alloc&) {
        Error(error,"Durable new-file allocation failed"); return DurableCreateResult::Failed;
    }
}

bool DurableRemoveExact(const std::string& path, std::string& error) {
    using namespace durable_detail;
    try {
        Path parsed;
        return ParsePath(path,parsed,error) && Remove(parsed,error);
    } catch (const std::bad_alloc&) { return Error(error,"Durable removal allocation failed"); }
}

struct DurableFileLease::Impl {
    durable_detail::Handle file;
};

DurableFileLease::DurableFileLease() = default;
DurableFileLease::~DurableFileLease() = default;

bool DurableFileLease::TryAcquire(const std::string& path, std::string& error) {
    using namespace durable_detail;
    if (IsHeld()) return Error(error,"Durable file lease is already held by this instance");
    try {
        Path parsed;
        if (!ParsePath(path,parsed,error)) return false;
        auto candidate = std::make_unique<Impl>();
#ifdef _WIN32
        candidate->file.value = CreateFileW(parsed.full.c_str(),GENERIC_READ | GENERIC_WRITE,0,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
#else
        candidate->file.value = open(parsed.full.c_str(),O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK,0600);
#endif
        if (candidate->file.value == Invalid) return Error(error,"Cannot acquire durable file lease",LastError());
        std::uint64_t size;
        if (!Regular(candidate->file.value,size,error)) return false;
#ifndef _WIN32
        int result;
        do { result = flock(candidate->file.value,LOCK_EX | LOCK_NB); } while (result != 0 && errno == EINTR);
        if (result != 0) return Error(error,"Durable file lease is busy or unsupported",errno);
#endif
        impl = std::move(candidate);
        error.clear(); return true;
    } catch (const std::bad_alloc&) { return Error(error,"Durable file lease allocation failed"); }
}

void DurableFileLease::Release() { impl.reset(); }
bool DurableFileLease::IsHeld() const { return impl && impl->file.value != durable_detail::Invalid; }

} // namespace openq4
