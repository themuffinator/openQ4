// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace openq4 {

enum class DurableReadResult { Missing, Present, Failed };
enum class DurableCreateResult { Created, Exists, Failed };
inline constexpr std::size_t DurableFileMaxBytes = 16 * 1024 * 1024;

// Exact native paths only: no VFS, package lookup, search path, or directory
// creation. The caller validates containment in its save root, supplies an
// absolute UTF-8 path with an existing, trusted parent directory, and serializes
// access to this target. Leaf symlinks/reparse points and nonregular files are
// rejected. This is not a sandbox against another process replacing ancestors.
// Dot path components, embedded NUL, and Windows device/stream paths are invalid.
// The replacement inherits parent permissions (0600 on POSIX); old ACLs, streams,
// extended attributes and hard-link identity are not copied to the new file.

// Both maxBytes and file size must be <= DurableFileMaxBytes. Present replaces
// outBytes only after a complete checked read/close. Missing and Failed leave it
// unchanged. Missing means an absent path, not a permission or sharing failure.
DurableReadResult DurableReadExact(const std::string& absoluteUtf8Path,
    std::size_t maxBytes, std::string& outBytes, std::string& error);

// Publishes complete bytes via a bounded, exclusively created adjacent temp.
// Before rename, failure preserves the final file. After attempting publication,
// failure may mean the new file is already visible but its durability is unknown;
// callers must retain recovery state, not assume the old bytes are still present.
bool DurableReplaceExact(const std::string& absoluteUtf8Path,
    const std::string& bytes, std::string& error);

// Publishes fully written new bytes only when the exact target is absent at
// the native publication boundary. A racing creator is never overwritten.
// Exists is an ordinary collision, clears error and makes no target mutation;
// nonregular/inaccessible targets may instead return Failed. Failed after a
// publication attempt can leave complete new bytes visible with durability
// unknown, so retain recovery evidence and never retry with ReplaceExact.
// No cross-process lease is needed to prevent creation collisions. This does
// not protect later changes or supply editor overwrite/conflict resolution.
DurableCreateResult DurableCreateExact(const std::string& absoluteUtf8Path,
    const std::string& bytes, std::string& error);

// Idempotent exact-name removal. Windows first moves the target to an owned,
// inert .openq4-durable-*.tmp tombstone with MOVEFILE_WRITE_THROUGH. Its subsequent
// deletion is checked; a crash can leave an inert tombstone, never a journal at
// the authoritative name. No enumeration or deletion of other temporary files.
// Failure after the namespace operation may already have removed the target.
bool DurableRemoveExact(const std::string& absoluteUtf8Path, std::string& error);

// Cross-process, nonblocking exclusion for one trusted exact lock path. Keep the
// fixed inert lock file permanently: unlinking it would let peers lock different
// inodes. Windows uses an exclusive sharing handle; POSIX uses flock(LOCK_NB).
// The OS releases ownership on process exit/crash. This is advisory on POSIX;
// every participant must use the same path and respect the lease. No journal
// check/write may occur outside the lease. Acquiring an already-held instance
// fails without releasing its current ownership. No lock-file contents are used.
class DurableFileLease {
public:
    DurableFileLease();
    ~DurableFileLease();
    DurableFileLease(const DurableFileLease&) = delete;
    DurableFileLease& operator=(const DurableFileLease&) = delete;
    bool TryAcquire(const std::string& absoluteUtf8Path, std::string& error);
    void Release();
    bool IsHeld() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Success clears error. No function reports unsupported sync as success.
// Durability is the OS/filesystem/device acknowledgement, not a guarantee against
// lying hardware, damaged media, or arbitrary remote filesystem semantics.
// Linux: file fsync, atomic renameat/unlinkat, parent-directory fsync.
// macOS: those barriers plus F_FULLFSYNC; unsupported barriers fail closed.
// Windows: checked WriteFile + FlushFileBuffers, then same-directory MoveFileExW
// with REPLACE_EXISTING|WRITE_THROUGH (never COPY_ALLOWED). Microsoft documents
// WRITE_THROUGH as waiting for the move on disk. ReplaceFile's similarly named
// flag is unsupported and is deliberately not used. Temporary cleanup itself
// need not survive a crash; only the exact target is authoritative.
// Primary contracts:
// https://man7.org/linux/man-pages/man2/fsync.2.html
// https://man7.org/linux/man-pages/man2/rename.2.html
// https://man7.org/linux/man-pages/man2/link.2.html
// https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fsync.2.html
// https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers
// https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw
// https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew
// SDL_FlushIO only promises a stream flush and SDL_RenamePath exposes no durable
// rename flag; native handles/descriptors are necessary for this contract.
// CreateExact uses MoveFileExW without REPLACE_EXISTING on Windows. POSIX
// linkat publishes only this call's exclusively created, fully synced adjacent
// temp, then removes that temp and syncs the directory. It never links an
// existing source document or installed asset. Unsupported filesystems refuse.

} // namespace openq4
