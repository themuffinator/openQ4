#!/usr/bin/env python3
"""Regression checks for POSIX memory reporting and page locking."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def validate_posix_page_locking() -> None:
    source = read("src/sys/posix/posix_main.cpp")
    lock_body = function_body(source, "bool Sys_LockMemory( void *ptr, int bytes ) {")
    unlock_body = function_body(source, "bool Sys_UnlockMemory( void *ptr, int bytes ) {")
    memory_status_body = function_body(source, "void Sys_GetCurrentMemoryStatus( sysMemoryStats_t &stats ) {")

    for body, context, syscall in (
        (lock_body, "POSIX LockMemory", "mlock"),
        (unlock_body, "POSIX UnlockMemory", "munlock"),
    ):
        require(body, "ptr == NULL || bytes <= 0", context)
        require(body, "static_cast<size_t>( bytes )", context)
        require(body, f"{syscall}( ptr, static_cast<size_t>( bytes ) ) == 0", context)
        reject(body, "return true;", context)

    require(memory_status_body, "static_cast<unsigned long long>( info.freeram ) + static_cast<unsigned long long>( info.bufferram )", "Linux memory-status field widening")


def validate_ram_total_helpers() -> None:
    linux_main = read("src/sys/linux/main.cpp")
    macos_compat = read("src/sys/macos/macos_compat.mm")

    for source, context in (
        (linux_main, "Linux system RAM total"),
        (macos_compat, "macOS system RAM total"),
    ):
        require(source, "Sys_RoundSystemRamMegabytes", context)
        require(source, "megabytes = ( megabytes + 8ULL ) & ~15ULL;", context)
        require(source, "idMath::INT_MAX", context)

    linux_body = function_body(linux_main, "int Sys_GetSystemRam( void ) {")
    require(linux_body, "sysconf( _SC_PHYS_PAGES )", "Linux system RAM total")
    require(linux_body, "sysconf( _SC_PAGE_SIZE )", "Linux system RAM total")
    require(linux_body, "static_cast<unsigned long long>( count ) * static_cast<unsigned long long>( pageSize )", "Linux system RAM total")
    reject(linux_body, "(double)count", "Linux system RAM total")
    reject(linux_body, "(int)(", "Linux system RAM total")

    macos_body = function_body(macos_compat, "int Sys_GetSystemRam( void ) {")
    require(macos_body, 'sysctlbyname( "hw.memsize"', "macOS system RAM total")
    require(macos_body, "Sys_RoundSystemRamMegabytes( memSizeBytes, 1024 )", "macOS system RAM total")
    reject(macos_body, "return (int)(", "macOS system RAM total")


def validate_release_note() -> None:
    source = read("docs/dev/release-completion.md")

    require(source, "Linux and macOS memory handling now matches the Windows contract", "release completion notes")
    require(source, "real page-lock and unlock calls", "release completion notes")


def validate_level_load_heap_release() -> None:
    posix = read("src/sys/posix/posix_main.cpp")
    body = function_body(posix, "void Sys_ReleaseFreeHeapMemory( void ) {")
    require(body, "#if defined( __GLIBC__ )", "POSIX heap release")
    require(body, "malloc_trim( 0 );", "POSIX heap release")
    require(posix, "#include <malloc.h>", "POSIX heap release include")
    windows = read("src/sys/win32/win_shared.cpp")
    require(windows, "void Sys_ReleaseFreeHeapMemory(void) {", "Windows heap release")
    require(read("src/sys/sys_public.h"), "void			Sys_ReleaseFreeHeapMemory( void );", "heap release declaration")
    session = read("src/framework/Session.cpp")
    settle = session.find("settleMsec = Sys_Milliseconds() - phaseStart;")
    release = session.find("Sys_ReleaseFreeHeapMemory();")
    if settle == -1 or release == -1 or release < settle:
        raise AssertionError("level load must release free heap memory after the settle frames")


def main() -> None:
    validate_posix_page_locking()
    validate_ram_total_helpers()
    validate_level_load_heap_release()
    validate_release_note()
    print("posix_memory_management: ok")


if __name__ == "__main__":
    main()
