#!/usr/bin/env python3
"""Compile actual Windows/POSIX event queues against a counted allocator.

Checks ownership transfer, pending-only clear, overflow/FIFO, stale/reused slots
and console erasure. Both production bodies compile on the invoking host; this
does not claim a full platform engine build or concurrent-producer qualification.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
static unsigned checks=0, frees=0, warnings=0;
static void Check(bool value,const char* message) {
    ++checks;
    if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
struct Common {
    void Printf(const char* format,...) {
        Check(std::string(format).find("overflow")!=std::string::npos,"only expected queue warning");
        ++warnings;
    }
} commonObject,*common=&commonObject;
struct Block {
    std::unique_ptr<unsigned char[]> memory;
    std::vector<unsigned char> original;
    bool freed=false;
};
static std::vector<Block> blocks;
static std::unordered_map<void*,std::size_t> indices;
static void* Allocate(std::size_t bytes) {
    Block block;
    block.memory=std::make_unique<unsigned char[]>(bytes+8);
    for(std::size_t i=0;i<bytes;++i) block.memory[i]=static_cast<unsigned char>(0x20+(i%90));
    // Include embedded zero bytes in arbitrary binary payloads.
    if(bytes>4) block.memory[3]=0;
    std::fill_n(block.memory.get()+bytes,8,0xa5);
    block.original.assign(block.memory.get(),block.memory.get()+bytes);
    void* pointer=block.memory.get();
    Check(indices.emplace(pointer,blocks.size()).second,"distinct live allocation");
    blocks.push_back(std::move(block));
    return pointer;
}
static Block& Lookup(void* pointer) {
    const auto found=indices.find(pointer);
    Check(found!=indices.end(),"payload is an owned allocator block");
    return blocks[found->second];
}
static void Mem_Free(void* pointer) {
    auto& block=Lookup(pointer);
    Check(!block.freed,"payload freed exactly once");
    for(std::size_t i=0;i<8;++i)
        Check(block.memory[block.original.size()+i]==0xa5,"payload guard bytes preserved");
    block.freed=true;
    ++frees;
    // Retain storage until test end to make dangling-slot reads/double frees
    // deterministic rather than dependent on a system allocator's reuse.
}
static bool Freed(void* pointer) { return Lookup(pointer).freed; }
static void ExpectBytes(void* pointer,bool erased) {
    auto& block=Lookup(pointer);
    for(std::size_t i=0;i<block.original.size();++i)
        Check(block.memory[i]==(erased?0:block.original[i]),"discard erasure matches payload kind and bound");
}
'''

MAIN = r'''
static void Empty() {
    const auto ev=Sys_GetEvent();
    Check(ev.evType==SE_NONE && ev.evValue==0 && ev.evValue2==0 &&
          ev.evPtrLength==0 && ev.evPtr==nullptr,"empty event has no owned payload");
}
static void CheckEvent(const sysEvent_t& event,sysEventType_t type,int value,int length,void* pointer) {
    Check(event.evType==type && event.evValue==value && event.evValue2==-value &&
          event.evPtrLength==length && event.evPtr==pointer,"FIFO event metadata and payload unchanged");
}
static void CheckCleared() {
    Check(eventHead==0 && eventTail==0,"clear resets both cursors");
    Empty();
    const unsigned before=frees;
    Sys_ClearEvents();
    Check(frees==before,"repeated clear is harmless");
}
static void PendingOnly() {
    Sys_ClearEvents(); CheckCleared();
    void* transferred=Allocate(12);
    Queue(SE_RETAINED_UI,11,12,transferred);
    const auto held=Sys_GetEvent();
    CheckEvent(held,SE_RETAINED_UI,11,12,transferred);
    void* privateConsole=Allocate(35);
    const char secret[]="set password private-value";
    std::memcpy(privateConsole,secret,sizeof(secret));
    Lookup(privateConsole).original.assign(static_cast<unsigned char*>(privateConsole),
        static_cast<unsigned char*>(privateConsole)+35);
    void* plainConsole=Allocate(18);
    void* malformedConsole=Allocate(14);
    std::fill_n(static_cast<unsigned char*>(malformedConsole),14,'x');
    Lookup(malformedConsole).original.assign(14,'x');
    void* binary=Allocate(21);
    Queue(SE_CONSOLE,12,35,privateConsole);
    Queue(SE_CONSOLE,13,18,plainConsole);
    Queue(SE_CONSOLE,14,14,malformedConsole);
    Queue(SE_RETAINED_UI,15,21,binary);
    Queue(SE_KEY,16,0,nullptr);
    Queue(SE_CONSOLE,17,99,nullptr);
    const auto before=frees;
    Sys_ClearEvents(); CheckCleared();
    Check(frees==before+4 && !Freed(transferred),"clear frees pending pointers but not dequeued caller ownership");
    for(void* pointer:{privateConsole,plainConsole,malformedConsole}) {
        Check(Freed(pointer),"pending console released"); ExpectBytes(pointer,true);
    }
    Check(Freed(binary),"pending binary released"); ExpectBytes(binary,false);
    ExpectBytes(transferred,false); CheckEvent(held,SE_RETAINED_UI,11,12,transferred);
    Mem_Free(transferred);
    Sys_ClearEvents(); CheckCleared(); // Slot still contains an already-freed dequeued pointer.
}
static void InvalidLengths() {
    for(int length:{0,-1}) {
        void* pointer=Allocate(16);
        Queue(SE_CONSOLE,40+length,length,pointer);
        Sys_ClearEvents();
        Check(Freed(pointer),"nonpositive length still releases owned pointer");
        ExpectBytes(pointer,false); // Never turn a negative signed length into a huge wipe.
    }
    CheckCleared();
}
static void DrainedSlotsAndReuse() {
    std::vector<sysEvent_t> held;
    for(int i=0;i<MAX_QUED_EVENTS;++i) {
        void* pointer=Allocate(8);
        Queue(SE_RETAINED_UI,i,8,pointer);
        held.push_back(Sys_GetEvent());
        CheckEvent(held.back(),SE_RETAINED_UI,i,8,pointer);
    }
    const auto before=frees;
    Sys_ClearEvents(); CheckCleared();
    Check(frees==before,"fully drained stale ring owns none of the returned pointers");
    // Reuse every physical ring slot while callers still own old payloads.
    std::vector<void*> pending;
    for(int i=0;i<MAX_QUED_EVENTS;++i) {
        pending.push_back(Allocate(9));
        Queue(SE_RETAINED_UI,1000+i,9,pending.back());
    }
    Sys_ClearEvents(); CheckCleared();
    for(void* pointer:pending) Check(Freed(pointer),"reused slot frees its new owned payload");
    for(const auto& event:held) {
        Check(!Freed(event.evPtr),"reused slot does not release prior caller pointer");
        ExpectBytes(event.evPtr,false); Mem_Free(event.evPtr);
    }
    Sys_ClearEvents(); CheckCleared();
}
static void WrappedPendingRange() {
    std::vector<sysEvent_t> held;
    std::vector<void*> pending;
    for(int i=0;i<200;++i) {
        void* pointer=Allocate(7);
        Queue(SE_RETAINED_UI,i,7,pointer);
        if(i<150) held.push_back(Sys_GetEvent()); else pending.push_back(pointer);
    }
    // Head passes the physical array boundary without overflowing live capacity.
    const auto previousWarnings=warnings;
    for(int i=0;i<206;++i) {
        pending.push_back(Allocate(7)); Queue(SE_RETAINED_UI,200+i,7,pending.back());
    }
    Check(eventHead>MAX_QUED_EVENTS && eventHead-eventTail==MAX_QUED_EVENTS &&
          warnings==previousWarnings,"wrapped live range fills capacity without overflow");
    Sys_ClearEvents(); CheckCleared();
    for(void* pointer:pending) Check(Freed(pointer),"both segments of wrapped pending range freed");
    for(const auto& event:held) {
        Check(!Freed(event.evPtr),"wrapped clear preserves prior dequeued ownership"); Mem_Free(event.evPtr);
    }
}
static void OverflowOrder() {
    const int extra=37;
    std::vector<void*> pointers;
    const auto previousWarnings=warnings;
    for(int i=0;i<MAX_QUED_EVENTS+extra;++i) {
        pointers.push_back(Allocate(10));
        Queue(i%2?SE_RETAINED_UI:SE_CONSOLE,i,10,pointers.back());
    }
    Check(warnings==previousWarnings+extra,"each overflow discards exactly one oldest entry");
    for(int i=0;i<extra;++i) {
        Check(Freed(pointers[i]),"overflow releases evicted oldest payload");
        ExpectBytes(pointers[i],i%2==0);
    }
    for(int i=extra;i<MAX_QUED_EVENTS+extra;++i) {
        const auto event=Sys_GetEvent();
        CheckEvent(event,i%2?SE_RETAINED_UI:SE_CONSOLE,i,10,pointers[i]);
        Check(!Freed(event.evPtr),"overflow retains each remaining payload for consumer");
        ExpectBytes(event.evPtr,false); Mem_Free(event.evPtr);
    }
    Empty(); Sys_ClearEvents(); CheckCleared();
}
int main() {
    PendingOnly(); InvalidLengths(); DrainedSlotsAndReuse(); WrappedPendingRange(); OverflowOrder();
    Check(frees==blocks.size(),"all allocations have exactly one owner release");
    for(const auto& block:blocks) Check(block.freed,"no outstanding allocation remains");
    std::printf("PASS %u ownership checks, %zu allocations, %u frees\n",checks,blocks.size(),frees);
}
'''


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def event_types():
    source = (ROOT / "src/sys/sys_public.h").read_text(encoding="utf-8")
    enum = re.search(r"typedef enum\s*\{\s*SE_NONE,.*?\}\s*sysEventType_t;", source, re.S)
    structure = re.search(r"typedef struct sysEvent_s\s*\{.*?\}\s*sysEvent_t;", source, re.S)
    if not enum or not structure:
        raise AssertionError("production event type declarations not found")
    return enum.group() + "\n" + structure.group() + "\n"


def production(platform):
    relative = "src/sys/win32/win_main.cpp" if platform == "windows" else "src/sys/posix/posix_main.cpp"
    source = (ROOT / relative).read_text(encoding="utf-8")
    helper = function_body(source, "static void Sys_DiscardQueuedEvent(")
    queue = function_body(source, "void Sys_QueEvent(" if platform == "windows" else "void Posix_QueEvent(")
    get = function_body(source, "sysEvent_t Sys_GetEvent(")
    clear = function_body(source, "void Sys_ClearEvents(")
    globals_ = source[source.index("#define\tMAX_QUED_EVENTS"):source.index("// Only pending queue entries")]
    if "256" not in globals_ or "eventQue[MAX_QUED_EVENTS]" not in globals_:
        raise AssertionError("production queue declaration changed")
    call = "Sys_QueEvent(0,type,value,-value,length,pointer);" if platform == "windows" else "Posix_QueEvent(type,value,-value,length,pointer);"
    wrapper = "\nstatic void Queue(sysEventType_t type,int value,int length,void* pointer) { " + call + " }\n"
    return relative, SUPPORT + event_types() + globals_ + helper + "\n" + queue + "\n" + get + "\n" + clear + wrapper + MAIN, clear


def main():
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    scratch = ROOT / ".tmp"
    scratch.mkdir(exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="sys-event-ownership-", dir=scratch))
    env = dict(os.environ, TEMP=str(directory), TMP=str(directory), TMPDIR=str(directory))
    results = {"scope": "compiled actual Windows and POSIX queue bodies on this host; no engine/platform/input launch",
               "files": {"src/sys/sys_public.h": digest(ROOT / "src/sys/sys_public.h"),
                         "tools/tests/sys_event_queue_ownership.py": digest(Path(__file__))}, "cases": {}}
    for platform in ("windows", "posix"):
        relative, original, clear = production(platform)
        results["files"][relative] = digest(ROOT / relative)
        mutations = {
            "production": original,
            "legacy-clear-leak": original.replace(clear, "void Sys_ClearEvents() { eventHead=eventTail=0; }"),
            "clear-dequeued-slots": original.replace(clear, clear.replace("{", "{\n eventTail=0;", 1)),
            "omit-console-wipe": original.replace("event.evType == SE_CONSOLE && event.evPtrLength > 0", "false"),
        }
        for name, source in mutations.items():
            label = platform + "-" + name
            cpp, exe = directory / (label + ".cpp"), directory / (label + ".exe")
            cpp.write_text(source, encoding="utf-8")
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", str(cpp), "-o", str(exe)]
            compiled = subprocess.run(command, text=True, capture_output=True, env=env)
            (directory / (label + "-compile.log")).write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
            if compiled.returncode:
                raise AssertionError(f"{label} compilation failed: {compiled.stderr}")
            ran = subprocess.run([str(exe)], text=True, capture_output=True, env=env)
            (directory / (label + "-run.log")).write_text(ran.stdout + ran.stderr, encoding="utf-8")
            if (ran.returncode == 0) != (name == "production"):
                raise AssertionError(f"{label} unexpected exit {ran.returncode}: {ran.stdout}{ran.stderr}")
            results["cases"][label] = {"compile_exit": compiled.returncode, "run_exit": ran.returncode,
                                       "command": command, "output": ran.stdout + ran.stderr,
                                       "source_sha256": digest(cpp)}
    for path in directory.iterdir():
        if path.is_file():
            results["files"][path.relative_to(ROOT).as_posix()] = digest(path)
    (directory / "result.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    for platform in ("windows", "posix"):
        print(platform + ": " + results["cases"][platform + "-production"]["output"].strip())
    print("PASS: six compiled leak/stale-ownership/erasure mutants rejected")
    print("Evidence: " + str(directory / "result.json"))


if __name__ == "__main__":
    main()
