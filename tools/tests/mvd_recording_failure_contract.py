#!/usr/bin/env python3
"""Execute production MVD write/finalization paths with deterministic I/O faults."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
using byte = unsigned char;
@ABI@
@CONSTANTS@
enum { MVD_IDLE, MVD_RECORDING, DEMO_NONE, MEASURE_SIZE, MVD_RECORD_END = 250 };
struct idStr : std::string {
    using std::string::string;
    using std::string::operator=;
    void BestUnit(const char *, float, int) { assign("fixture bytes"); }
    static void Copynz(char *dest, const char *source, int size) {
        assert(size > 0); std::snprintf(dest, size, "%s", source);
    }
};
static const char *va(const char *format, ...) {
    static char buffer[256];
    va_list args; va_start(args, format); std::vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    return buffer;
}
// The checksum implementation is outside this fault-injection contract.
static unsigned CRC32_BlockChecksum(const void *, int) { return 0; }
struct idFile_Memory {
    std::vector<byte> data;
    explicit idFile_Memory(const char *) {}
    template<class T> void Append(T value) {
        const byte *bytes = reinterpret_cast<const byte *>(&value);
        data.insert(data.end(), bytes, bytes + sizeof(value));
    }
    void WriteUnsignedInt(unsigned value) { Append(value); }
    void WriteUnsignedShort(unsigned short value) { Append(value); }
    void WriteInt(int value) { Append(value); }
    const byte *GetDataPtr() const { return data.data(); }
    int Length() const { return static_cast<int>(data.size()); }
};
struct idFile {
    int bytes = 0, writes = 0, syncs = 0, closes = 0, failWrite = 0;
    bool syncOK = true;
    std::function<void()> duringSync;
    int Write(const void *, int length) {
        ++writes;
        const int accepted = writes == failWrite ? std::max(0, length - 1) : length;
        bytes += accepted;
        return accepted;
    }
    int Length() const { return bytes; }
    bool Sync() { ++syncs; if (duringSync) duringSync(); return syncOK; }
};
struct FileSystem {
    bool promoteOK = true;
    int promotions = 0;
    std::vector<std::string> order;
    void CloseFile(idFile *file) { ++file->closes; order.push_back("close"); }
    bool PromoteFile(const char *partial, const char *final, const char *base) {
        assert(std::string(partial) == "demos/fixture.mvd.part");
        assert(std::string(final) == "demos/fixture.mvd");
        assert(std::string(base) == "fs_savepath");
        ++promotions; order.push_back("promote"); return promoteOK;
    }
} fs, *fileSystem = &fs;
struct Common {
    int warnings = 0, notices = 0;
    void Warning(const char *, ...) { ++warnings; }
    void Printf(const char *, ...) { ++notices; }
} messages, *common = &messages;
struct Game {
    int stopped = 0;
    void SetDemoState(int state, bool server, bool repeater) {
        assert(state == DEMO_NONE && !server && !repeater); ++stopped;
    }
} gameState, *game = &gameState;
struct Server {
    int GetGameFrame() const { return 100; }
    int GetGameTime() const { return 1600; }
};
struct idAsyncNetwork { static Server server; };
Server idAsyncNetwork::server;
struct CVar { int value = 1; int GetInteger() const { return value; } } mvd_maxSizeMB;
struct idMultiViewDemo {
    idFile *file = nullptr;
    int state = MVD_RECORDING, recordCount = 0, snapshotCount = 2, reliableCount = 3;
    int indexWrites = 0, clears = 0;
    bool finalizing = false, indexOK = true, recordingResultValid = false;
    idStr fileName = "demos/fixture.mvd", tempFileName = "demos/fixture.mvd.part", lastError;
    serverMVDRecordingResult_t recordingResult{};
    bool WouldExceedLimits(int additionalBytes) const;
    bool WriteRecord(unsigned short type, unsigned short version, unsigned short flags,
                     const void *payload, int payloadLength, bool enforceLimits = true);
    bool CommitRecording();
    void SetRecordingResult(serverMVDResultState_t state, serverMVDResultReason_t reason,
                            const char *finalQPath, const char *partialQPath);
    bool StopRecording(const char *reason, bool finalize,
                       serverMVDResultReason_t failureReason = SERVER_MVD_REASON_STREAM_WRITE_FAILED);
    bool WriteIndexRecord() { ++indexWrites; return indexOK; }
    void Clear() {
        ++clears; state = MVD_IDLE; finalizing = false;
        fileName.clear(); tempFileName.clear(); lastError.clear();
    }
};
'''
MAIN = r'''
static void ResetDependencies() {
    fs = FileSystem{}; messages = Common{}; gameState = Game{}; mvd_maxSizeMB.value = 1;
}
int main() {
    // A short header or payload is never counted as a complete record.
    for (int failedWrite = 0; failedWrite <= 2; ++failedWrite) {
        ResetDependencies(); idFile file; idMultiViewDemo demo; demo.file = &file;
        file.failWrite = failedWrite;
        const char payload[] = "payload";
        assert(demo.WriteRecord(11, 2, 1, payload, sizeof(payload)) == (failedWrite == 0));
        assert(demo.recordCount == (failedWrite == 0 ? 1 : 0));
        assert(file.writes == (failedWrite == 1 ? 1 : 2));
        assert(!failedWrite || demo.lastError.find("short write") != std::string::npos);
        assert(file.syncs == 0 && file.closes == 0 && fs.promotions == 0);
    }
    // Rejected lengths/pointers do not touch the stream or consume its budget.
    for (int invalid = 0; invalid < 4; ++invalid) {
        idFile file; idMultiViewDemo demo; demo.file = invalid == 0 ? nullptr : &file;
        const char payload = 1;
        const int length = invalid == 1 ? -1 : invalid == 2 ? MVD_MAX_RECORD_BYTES + 1 : 1;
        assert(!demo.WriteRecord(11, 2, 1, invalid == 3 ? nullptr : &payload, length));
        assert(file.writes == 0 && demo.recordCount == 0);
    }
    // Exact cap boundary succeeds; an over-budget record cannot write a header.
    for (int overflow = 0; overflow < 2; ++overflow) {
        ResetDependencies(); idFile file; idMultiViewDemo demo; demo.file = &file;
        file.bytes = 1024 * 1024 - MVD_RECORD_HEADER_BYTES - 1 + overflow;
        const char payload = 1;
        assert(demo.WriteRecord(11, 2, 1, &payload, 1) == !overflow);
        assert(file.writes == (overflow ? 0 : 2));
        assert(!overflow || demo.lastError == "MVD recording size limit reached");
    }
    // Disabling the configured cap does not overflow at the maximum file length.
    {
        idFile file; idMultiViewDemo demo; demo.file = &file; file.bytes = INT32_MAX;
        mvd_maxSizeMB.value = 2047;
        assert(demo.WouldExceedLimits(24));
        mvd_maxSizeMB.value = 0;
        assert(!demo.WouldExceedLimits(24));
    }
    // Finalization failure matrix: index, end-header, end-payload, sync,
    // promotion and an explicit stream abort, plus the clean control case.
    for (int fault = 0; fault < 7; ++fault) {
        ResetDependencies(); idFile file; idMultiViewDemo demo; demo.file = &file;
        demo.indexOK = fault != 1;
        file.failWrite = fault == 2 ? 1 : fault == 3 ? 2 : 0;
        file.syncOK = fault != 4; fs.promoteOK = fault != 5;
        const bool finalize = fault != 6;
        assert(demo.StopRecording("fixture", finalize) == (fault == 0));
        assert(file.closes == 1 && demo.file == nullptr && demo.state == MVD_IDLE);
        assert(demo.clears == 1 && gameState.stopped == 1 && demo.recordingResultValid);
        assert(file.syncs == (fault == 0 || fault == 4 || fault == 5 ? 1 : 0));
        assert(demo.indexWrites == (finalize ? 1 : 0));
        assert(fs.promotions == (fault == 0 || fault == 5 ? 1 : 0));
        assert(fs.order.front() == "close");
        if (fs.promotions) assert(fs.order.back() == "promote");
        const auto &result = demo.recordingResult;
        assert(result.state == (fault == 0 ? SERVER_MVD_RESULT_COMMITTED : SERVER_MVD_RESULT_FAILED));
        const auto reason = fault == 0 ? SERVER_MVD_REASON_NONE : fault <= 3 ? SERVER_MVD_REASON_FINALIZE_WRITE_FAILED :
            fault == 4 ? SERVER_MVD_REASON_SYNC_FAILED : fault == 5 ? SERVER_MVD_REASON_PROMOTE_FAILED : SERVER_MVD_REASON_STREAM_WRITE_FAILED;
        assert(result.reason == reason);
        assert(std::string(result.finalQPath) == (fault == 0 ? "demos/fixture.mvd" : ""));
        assert(std::string(result.partialQPath) == (fault == 0 ? "" : "demos/fixture.mvd.part"));
        // A second stop preserves the terminal result and cannot close/promote twice.
        const auto terminal = result;
        assert(!demo.StopRecording("duplicate", true));
        assert(file.closes == 1 && demo.clears == 1 && gameState.stopped == 1);
        assert(std::memcmp(&terminal, &demo.recordingResult, sizeof(terminal)) == 0);
    }
    // A reentrant stop during Sync also cannot publish or close the stream twice.
    {
        ResetDependencies(); idFile file; idMultiViewDemo demo; demo.file = &file;
        file.duringSync = [&]() {
            assert(demo.finalizing && !demo.StopRecording("reentrant", true));
            assert(file.closes == 0 && fs.promotions == 0);
        };
        assert(demo.StopRecording("outer", true));
        assert(file.closes == 1 && fs.promotions == 1 && demo.clears == 1);
    }
    puts("mvd_recording_failure_contract: PASS (short writes, cap, finalization, sync, promotion and duplicate/reentrant stop)");
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "src/framework/async/MultiViewDemo.cpp")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    network = (ROOT / "src/framework/async/NetworkSystem.h").read_text(encoding="utf-8")
    abi = network[network.index("static const int SERVER_MVD_RESULT_QPATH_BYTES"):network.index("} serverMVDRecordingResult_t;") + len("} serverMVDRecordingResult_t;")]
    constants = "\n".join(re.search(r"^static const [^\n]* " + name + r" = [^;]+;", source, re.M)[0]
                          for name in ("MVD_RECORD_HEADER_BYTES", "MVD_MAX_RECORD_BYTES", "MVD_RECORD_SYNC"))
    functions = "\n".join(function_body(source, signature) for signature in (
        "static bool MVD_WriteExact(", "bool idMultiViewDemo::WouldExceedLimits(",
        "bool idMultiViewDemo::WriteRecord(", "void idMultiViewDemo::SetRecordingResult(",
        "bool idMultiViewDemo::CommitRecording(", "bool idMultiViewDemo::StopRecording("))
    compiler = next((p for n in ("clang++", "g++", "c++") if (p := shutil.which(n))), None)
    if compiler is None:
        raise RuntimeError("a native C++ compiler is required")
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mvd-write-", dir=ROOT / ".tmp") as temp:
        cpp, exe = Path(temp) / "test.cpp", Path(temp) / "test.exe"
        cpp.write_text(SUPPORT.replace("@ABI@", abi).replace("@CONSTANTS@", constants) + functions + MAIN, encoding="utf-8")
        subprocess.run([compiler, *flags, str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
