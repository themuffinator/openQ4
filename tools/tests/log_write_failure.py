#!/usr/bin/env python3
"""A failed log write stops logging instead of recursing (#219).

idFile_Permanent::Write reports a failed write with common->Printf. Before the
fix, idCommonLocal::VPrintf wrote that report to the same failing log, which
failed and reported again until the stack overflowed (seen in a 0.14.1 crash
dump: 3,400 VPrintf -> Write -> Printf frames). This compiles VPrintf's
production log block against counted doubles whose Write fails the way
idFile_Permanent::Write does, and checks the print chain stays shallow, the log
closes once, and later prints reach only the console.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#define MAX_OSPATH 256
typedef time_t ID_TIME_T;
struct idStr {
    std::string s;
    idStr(const char* c = "") : s(c) {}
    idStr& operator=(const char* c) { s = c; return *this; }
    const char* c_str() const { return s.c_str(); }
    int Find(char c) const { const auto at = s.find(c); return at == std::string::npos ? -1 : static_cast<int>(at); }
    int Icmp(const char* other) const {
        const char* a = s.c_str();
        while (*a && *other && std::tolower(*a) == std::tolower(*other)) { ++a; ++other; }
        return std::tolower(*a) - std::tolower(*other);
    }
};
struct idFile {
    int writes = 0, flushes = 0, failFrom = 0;
    int Write(const void*, int length);
    void Flush() { ++flushes; }
    void ForceFlush() {}
};
struct CVar {
    int value; const char* text;
    int GetInteger() const { return value; }
    const char* GetString() const { return text; }
};
CVar com_logFile{2, ""}, com_logFileName{0, "qconsole.log"};
bool com_preserveStartupRecoveryLog = false;
struct FileSystem {
    int closes = 0;
    bool IsInitialized() const { return true; }
    idFile* OpenFileAppend(const char*, bool, const char*) { return nullptr; }
    idFile* OpenFileWrite(const char*) { return nullptr; }
    void CloseFile(idFile*) { ++closes; }
} fileSystemObject, *fileSystem = &fileSystemObject;
struct CVarSystem { bool GetCVarBool(const char*) const { return false; } } cvarSystemObject, *cvarSystem = &cvarSystemObject;
namespace idLib { int SizeToInt(size_t value, const char*) { return static_cast<int>(value); } }
idFile theLog;
idFile* logFile = &theLog;
bool logFileFailed = false;
int depth = 0, deepest = 0;
std::vector<std::string> console;
void FatalError(const char*, ...) { assert(false && "the log is already open"); }
void Common_WriteLogIdentityBanner(idFile*) {}
void Printf(const char* fmt, ...);
// VPrintf's log block, as production writes it, for one formatted print.
void LogPrint(const char* msg) {
'''

MAIN = r'''
}
void Printf(const char* fmt, ...) {
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    deepest = std::max(deepest, ++depth);
    assert(depth < 8 && "a failed log write recursed through Printf");
    console.push_back(msg);
    LogPrint(msg);
    --depth;
}
// idFile_Permanent::Write: a write that makes no progress is reported with
// Printf and returns 0.
int idFile::Write(const void*, int length) {
    ++writes;
    if (failFrom > 0 && writes >= failFrom) {
        Printf("idFile_Permanent::Write: 0 bytes written to %s\n", "qconsole.log");
        return 0;
    }
    return length;
}
int main() {
    Printf("first\n");
    assert(theLog.writes == 1 && theLog.flushes == 1 && logFile == &theLog && !logFileFailed);
    theLog.failFrom = 2;  // the disk fills up
    Printf("second\n");
    assert(deepest <= 3);
    assert(logFile == nullptr && logFileFailed && fileSystemObject.closes == 1);
    assert(theLog.writes == 2 && theLog.flushes == 1);
    const auto said = [](const char* text) {
        return std::any_of(console.begin(), console.end(), [&](const std::string& line) { return line.find(text) != std::string::npos; });
    };
    assert(said("0 bytes written") && said("Log file write failed; logging stopped for this session"));
    Printf("third\n");  // reaches the console only
    assert(theLog.writes == 2 && said("third") && fileSystemObject.closes == 1);
    std::puts("ok");
}
'''


def log_block(source: str) -> str:
    """From VPrintf's "// logFile" comment through the end of its log block."""
    vprintf = source.index("void idCommonLocal::VPrintf(")
    start = source.index("\t// logFile", vprintf)
    block = source.index("if ( com_logFile.GetInteger()", start)
    depth = 0
    for index in range(source.index("{", block), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError("VPrintf log block has no end")


def main() -> int:
    source_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "src/framework/Common.cpp"
    block = log_block(source_path.read_text(encoding="latin-1"))
    file_cpp = (ROOT / "src/framework/File.cpp").read_text(encoding="latin-1")
    # the premise: a failed permanent-file write reports itself through Printf
    assert 'common->Printf( "idFile_Permanent::Write: 0 bytes written to %s\\n"' in file_cpp
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="log-write-failure-", dir=ROOT / ".tmp") as directory:
        cpp = Path(directory) / "log.cpp"
        binary = cpp.with_suffix(".exe")
        cpp.write_text(SUPPORT + block + MAIN, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wno-unused-variable", "-D_CRT_SECURE_NO_WARNINGS", str(cpp), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        if result.returncode != 0 or result.stdout.strip() != "ok":
            print(result.stdout, result.stderr)
            raise AssertionError(f"log write failure check failed (exit {result.returncode})")
    print("log_write_failure: a failed log write closes the log once and later prints reach the console")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
