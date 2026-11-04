#!/usr/bin/env python3
"""Exercise production frame arenas and cache retirement with a held consumer."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <cstdint>
#undef INT_MAX
struct idMath { static constexpr int INT_MAX=2147483647; };
using byte=unsigned char;
template<class T> T Max(T a,T b){return a>b?a:b;}
struct Common {template<class... T> void FatalError(const char*,T...){throw std::runtime_error("invalid allocation");}} commonStorage,*common=&commonStorage;
struct SIMD {void Memset(void* p,int n,int bytes){std::memset(p,n,bytes);}} simdStorage,*SIMDProcessor=&simdStorage;
static int liveBlocks=0;
void* Mem_Alloc16(size_t bytes){++liveBlocks;return ::operator new(bytes,std::align_val_t(16));}
void Mem_Free16(void* p){--liveBlocks;::operator delete(p,std::align_val_t(16));}
void* Mem_ClearedAlloc(size_t bytes){return std::calloc(1,bytes);}
void Mem_Free(void* p){std::free(p);}
struct srfTriangles_t {srfTriangles_t* nextDeferredFree=nullptr;int marker=0;};
struct frameMemoryBlock_t {frameMemoryBlock_t* next;int size,used;alignas(16) byte base[4];};
struct frameData_t {
 frameMemoryBlock_t *memory,*alloc;
 srfTriangles_t *firstDeferredFreeTriSurf,*lastDeferredFreeTriSurf;
 int memoryHighwater;
 void *cmdHead,*cmdTail;
 int slot;
};
static frameData_t *frameData=nullptr,*smpFrames[2];
static thread_local frameData_t *backEndFrameData=nullptr;
static std::atomic<bool> inFlight{false};
bool R_RenderThread_InFlight(){return inFlight;}
void R_RenderThread_Sync(const char*){assert(!inFlight);}
void R_RenderThread_SyncFrontEnd(const char*){}
struct Cvar {bool GetBool(){return false;}} r_lockSurfaces;
static int retiredSurfaces=0;
static bool requireCombinedRetirement=false;
void R_FreeDeferredTriSurfs(frameData_t* frame){
 int count=0;
 for(auto* p=frame->firstDeferredFreeTriSurf;p;p=p->nextDeferredFree)++count;
 if(requireCombinedRetirement){assert(count==2);requireCombinedRetirement=false;}
 retiredSurfaces+=count;
 frame->firstDeferredFreeTriSurf=frame->lastDeferredFreeTriSurf=nullptr;
}
struct idCmdArgs {};
void R_FrameOwnershipSelfTest_f(const idCmdArgs&){}
void R_FrameSnapshotStats_f(const idCmdArgs&){}
void R_ClearRetainedSnapshotIndexes(){}
enum{CMD_FL_RENDERER=1};
struct Cmd {void AddCommand(const char*,void(*)(const idCmdArgs&),int,const char*){}} cmdStorage,*cmdSystem=&cmdStorage;
int R_CountFrameData(){return 0;}
void* R_FrameAlloc(int);
void R_ClearCommandChain(){frameData->cmdHead=frameData->cmdTail=R_FrameAlloc(16);}
void R_AdvanceFrameData();
#define MEMORY_BLOCK_SIZE 0x100000
struct vertCache_t {vertCache_t *next,*prev;int tag=1;};
class idVertexCache {
public:
 vertCache_t dynamicHeaders[2],deferredFreeList[2],freeDynamicHeaders;
 int freed=0;
 void RetireFrame(int);
 void ActuallyFree(vertCache_t* p){p->next->prev=p->prev;p->prev->next=p->next;p->tag=0;++freed;}
};
'''
CASES = r'''
static bool Contains(frameData_t* frame,const void* p){
 const auto address=reinterpret_cast<uintptr_t>(p);
 for(auto* block=frame->memory;block;block=block->next){
  const auto begin=reinterpret_cast<uintptr_t>(block->base);
  if(address>=begin && address<begin+block->size)return true;
 }
 return false;
}
static void Head(vertCache_t& h){h.next=h.prev=&h;}
static void Link(vertCache_t& h,vertCache_t& p){p.next=h.next;p.prev=&h;h.next->prev=&p;h.next=&p;}
int main(){
 R_InitFrameData();assert(frameData==smpFrames[0]);
 auto* old=frameData;
 auto* first=static_cast<byte*>(R_FrameAlloc(1000));std::memset(first,0x5a,1000);
 assert(reinterpret_cast<uintptr_t>(first)%16==0);
 srfTriangles_t parent,interaction;
 old->firstDeferredFreeTriSurf=old->lastDeferredFreeTriSurf=&parent;
 smpFrames[1]->firstDeferredFreeTriSurf=smpFrames[1]->lastDeferredFreeTriSurf=&interaction;
 requireCombinedRetirement=true;
 std::mutex mutex;std::condition_variable wake;bool release=false;
 byte* consumerBytes=nullptr;
 inFlight=true;
 std::thread worker([&]{
  R_SetBackEndFrameData(old);
  std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return release;});
  consumerBytes=static_cast<byte*>(R_FrameAlloc(64));std::memset(consumerBytes,0x42,64);
  assert(Contains(old,consumerBytes));
  R_SetBackEndFrameData(nullptr);
 });
 R_AdvanceFrameData();assert(frameData==smpFrames[1] && retiredSurfaces==0);
 auto* large=static_cast<byte*>(R_FrameAlloc(3*MEMORY_BLOCK_SIZE));
 std::memset(large,0x69,3*MEMORY_BLOCK_SIZE);
 assert(Contains(frameData,large) && !Contains(old,large));
 {std::lock_guard<std::mutex> lock(mutex);release=true;wake.notify_all();}
 worker.join();inFlight=false;
 for(int i=0;i<1000;++i)assert(first[i]==0x5a);
 for(int i=0;i<64;++i)assert(consumerBytes[i]==0x42);
 assert(large[0]==0x69 && large[3*MEMORY_BLOCK_SIZE-1]==0x69);
 int used=frameData->alloc->used;
 for(int invalid : {-1,idMath::INT_MAX}){
  bool rejected=false;try{R_FrameAlloc(invalid);}catch(const std::runtime_error&){rejected=true;}
  assert(rejected && frameData->alloc->used==used);
 }
 R_AdvanceFrameData();assert(frameData==old && retiredSurfaces==2);
 assert(R_FrameAlloc(1000)==first); // reuse begins only after the consumer retires
 R_ShutdownFrameData();assert(frameData==nullptr && smpFrames[0]==nullptr && smpFrames[1]==nullptr && liveBlocks==0);
 R_InitFrameData();R_ShutdownFrameData();assert(liveBlocks==0);
 idVertexCache cache;Head(cache.freeDynamicHeaders);
 vertCache_t dynamic[2],deferred[2];
 for(int i=0;i<2;++i){Head(cache.dynamicHeaders[i]);Head(cache.deferredFreeList[i]);Link(cache.dynamicHeaders[i],dynamic[i]);Link(cache.deferredFreeList[i],deferred[i]);}
 cache.RetireFrame(0);
 assert(cache.freed==1 && deferred[0].tag==0 && deferred[1].tag==1);
 assert(cache.dynamicHeaders[1].next==&dynamic[1] && cache.deferredFreeList[1].next==&deferred[1]);
 assert(cache.freeDynamicHeaders.next==&dynamic[0]);
 cache.RetireFrame(1);cache.RetireFrame(0);cache.RetireFrame(1);
 assert(cache.freed==2 && cache.freeDynamicHeaders.next==&dynamic[1] && dynamic[1].next==&dynamic[0]);
}
'''


def main():
    main_source = (ROOT / "src/renderer/tr_main.cpp").read_text(encoding="utf-8")
    cache_source = (ROOT / "src/renderer/VertexCache.cpp").read_text(encoding="utf-8")
    bodies = "\n".join(function_body(main_source, signature) for signature in (
        "frameData_t *R_BackEndFrameData(", "void R_SetBackEndFrameData(",
        "void R_FreeDeferredFrameSurfaces(", "void R_ToggleSmpFrame(",
        "void R_AdvanceFrameData(", "void R_ShutdownFrameData(", "void R_InitFrameData(",
        "void *R_FrameAlloc("))
    bodies += function_body(cache_source, "void idVertexCache::RetireFrame(")
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="frame-ownership-", dir=ROOT / ".tmp") as directory:
        temp = Path(directory)
        for name, source, expected in (
            ("production", bodies, True),
            ("shared-arena", bodies.replace("frame = R_BackEndFrameData();", "frame = frameData;"), False),
            ("split-tri-retirement", bodies.replace("deferred.lastDeferredFreeTriSurf->nextDeferredFree = frame->firstDeferredFreeTriSurf;", "R_FreeDeferredTriSurfs( &deferred );\n            deferred.firstDeferredFreeTriSurf = frame->firstDeferredFreeTriSurf;"), False),
        ):
            cpp = temp / f"{name}.cpp"
            exe = temp / (f"{name}.exe" if os.name == "nt" else name)
            cpp.write_text(SUPPORT + source + CASES, encoding="utf-8")
            subprocess.run([compiler, "-std=c++20", "-pthread", str(cpp), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, timeout=20)
            if (result.returncode == 0) != expected:
                raise AssertionError(f"{name}: unexpected exit {result.returncode}: {result.stderr.decode(errors='replace')}")
    print("Frame ownership: concurrent arenas, large/aligned allocations, shared-triangle retirement, cache slots, teardown/reinit and 2 negative controls passed")


if __name__ == "__main__":
    main()
