#!/usr/bin/env python3
"""Execute production stream-copy ownership across aliasing and later views."""
from pathlib import Path
import os
import shutil
import subprocess
from filesystem_case_segments import function_body
from game_regression_suite import RegressionTemporaryDirectory

ROOT = Path(__file__).resolve().parents[2]
PREPARATION = r'''
#include "FrameSnapshotCopy.h"
#include "FrameSnapshotRetain.h"
#include <cassert>
#include <memory>
#include <vector>
static std::vector<std::unique_ptr<unsigned char[]>> preparedBlocks;
static void* R_FrameAlloc(std::size_t bytes){preparedBlocks.emplace_back(new unsigned char[bytes]);return preparedBlocks.back().get();}
struct srfTriangles_t {};
struct viewEntity_t {viewEntity_t* next=nullptr;};
struct drawSurf_t {drawSurf_t* nextOnLight=nullptr;unsigned char* source=nullptr;unsigned char* submitted=nullptr;bool mutate=false;};
struct idRenderLightLocal {srfTriangles_t* frustumTris=nullptr;};
struct viewLight_t {
 viewLight_t* next=nullptr;idRenderLightLocal* lightDef=nullptr;srfTriangles_t* frustumTris=nullptr;
 drawSurf_t *globalShadows=nullptr,*localShadows=nullptr,*localInteractions=nullptr,*globalInteractions=nullptr,*translucentInteractions=nullptr;
 drawSurf_t *globalShadowMapStencilSupplements=nullptr,*localShadowMapStencilSupplements=nullptr;
 drawSurf_t *localShadowMapCasters=nullptr,*globalShadowMapCasters=nullptr,*localShadowMapDynamicCasters=nullptr,*globalShadowMapDynamicCasters=nullptr;
 drawSurf_t *localTranslucentShadowMapCasters=nullptr,*globalTranslucentShadowMapCasters=nullptr;
};
struct World {bool AnyLightGridAvailable(){return false;}};
struct viewDef_t {
 bool frameSnapshot=false,lightGridAvailable=false;World* renderWorld=nullptr;
 viewEntity_t* viewEntitys=nullptr;viewLight_t* viewLights=nullptr;
 int numDrawSurfs=0,numOutlineDrawSurfs=0;drawSurf_t **drawSurfs=nullptr,**outlineDrawSurfs=nullptr;
};
struct frameLightReceipt_s {idRenderLightLocal *source,*snapshot;frameLightReceipt_s* next;};
struct Frame {frameLightReceipt_s* lightReceipts=nullptr;} frame,*frameData=&frame;
class viewSnapshot_t {
 idFrameSnapshotCopies copies;
public:
 void PrepareGeometry(const srfTriangles_t*){}
 srfTriangles_t* Geometry(srfTriangles_t* p){return p;}
 void Entity(viewEntity_t*){}
 void Surface(drawSurf_t* surf,bool=false,bool prepare=false){
  if(!surf)return;
  if(prepare){if(surf->mutate)surf->source[0]=0x42;return;}
  surf->submitted=static_cast<unsigned char*>(copies.Copy(surf->source,16,R_FrameAlloc).data);
 }
 void Chain(drawSurf_t* surf,bool prepare=false){for(;surf;surf=surf->nextOnLight)Surface(surf,false,prepare);}
};
'''
PREPARATION_CASE = r'''
static void CheckPreparation(){
 unsigned char source[16]={};
 drawSurf_t ambient,lateCaster;ambient.source=lateCaster.source=source;lateCaster.mutate=true;
 drawSurf_t* draws[]={&ambient};viewLight_t light;light.globalShadowMapCasters=&lateCaster;
 viewDef_t view;view.drawSurfs=draws;view.numDrawSurfs=1;view.viewLights=&light;
 R_SnapshotRenderViewInternal(&view);
 assert(ambient.submitted[0]==0x42 && lateCaster.submitted==ambient.submitted);
 source[0]=0xff;R_SnapshotRenderViewInternal(&view);assert(ambient.submitted[0]==0x42);
}
'''
CASES = r'''
#include "FrameSnapshotCopy.h"
#include "FrameSnapshotRetain.h"
#include <cassert>
#include <array>
#include <memory>
#include <vector>
int main() {
 CheckPreparation();
 // Immutable versions survive producer rewrites, eviction and frame retirement.
 idFrameSnapshotRetainedIndexes indexStore(64);
 std::uint32_t indices[4]={0,1,2,3};
 auto original=indexStore.Acquire(indices,sizeof(indices));
 auto hit=indexStore.Acquire(indices,sizeof(indices));
 assert(hit.reused && hit.lease==original.lease);
 indices[1]=7;
 auto changed=indexStore.Acquire(indices,sizeof(indices));
 assert(!changed.reused && changed.lease!=original.lease);
 assert((*original.lease)[1]==1 && (*changed.lease)[1]==7);
 auto shorter=indexStore.Acquire(indices,8);
 assert(!shorter.reused && shorter.lease->size()==2);
 std::weak_ptr<const idFrameSnapshotRetainedIndexes::block_t> weak=changed.lease;
 indexStore.Clear(); assert(!weak.expired());
 changed.lease.reset(); assert(weak.expired());
 assert((*hit.lease)[1]==1);
 std::uint32_t pressure[16]={};
 auto evicted=indexStore.Acquire(pressure,sizeof(pressure));
 indexStore.Acquire(indices,sizeof(indices));
 assert((*evicted.lease)[0]==0 && !indexStore.Acquire(pressure,68).lease);
 // Pressure evicts the least recently acquired streams only, never the whole cache.
 idFrameSnapshotRetainedIndexes lru(64);
 std::uint32_t a[4]={1,1,1,1},b[4]={2,2,2,2},c[4]={3,3,3,3},d[8]={4,4,4,4,4,4,4,4};
 lru.Acquire(a,sizeof(a));lru.Acquire(b,sizeof(b));lru.Acquire(c,sizeof(c));
 assert(lru.Acquire(a,sizeof(a)).reused);
 lru.Acquire(d,sizeof(d));
 assert(lru.Size()==3 && lru.ResidentBytes()==64);
 assert(lru.Acquire(a,sizeof(a)).reused && lru.Acquire(c,sizeof(c)).reused && lru.Acquire(d,sizeof(d)).reused);
 assert(!lru.Acquire(b,sizeof(b)).reused);
 // One-shot ranges beyond the entry cap displace the oldest one at a time.
 idFrameSnapshotRetainedIndexes capped;
 std::vector<std::array<std::uint32_t,4>> oneShot(idFrameSnapshotRetainedIndexes::maximumEntries+7232);
 for(std::size_t i=0;i<oneShot.size();++i){oneShot[i].fill(static_cast<std::uint32_t>(i));capped.Acquire(oneShot[i].data(),16);}
 assert(capped.Size()==idFrameSnapshotRetainedIndexes::maximumEntries);
 assert(capped.Acquire(oneShot.back().data(),16).reused && !capped.Acquire(oneShot.front().data(),16).reused);

 std::vector<std::unique_ptr<unsigned char[]>> blocks;
 auto allocate=[&](std::size_t size)->void* {blocks.emplace_back(new unsigned char[size]);return blocks.back().get();};
 std::array<unsigned char,4096> source;source.fill(0x5a);
 idFrameSnapshotCopies first;
 auto full=first.Copy(source.data(),source.size(),allocate);
 auto initialBlocks=blocks.size();
 auto alias=first.Copy(source.data(),source.size(),allocate);
 assert(!full.reused && alias.reused && full.data==alias.data && blocks.size()==initialBlocks);
 auto prefix=first.Copy(source.data(),source.size()/2,allocate);
 auto offset=first.Copy(source.data()+1,source.size()/2,allocate);
 assert(!prefix.reused && !offset.reused && prefix.data!=full.data && offset.data!=prefix.data);
 assert(first.Copy(source.data(),source.size()/2,allocate).data==prefix.data);
 assert(first.Copy(nullptr,4096,allocate).data==nullptr);
 assert(first.Copy(source.data(),0,allocate).data==nullptr && blocks.size()==initialBlocks+2);
 // A later view observes rebuilt contents, while the submitted reader remains
 // unchanged even when its producer uses exactly the same source address.
 source.fill(0xa5);
 idFrameSnapshotCopies second;
 auto later=second.Copy(source.data(),source.size(),allocate);
 assert(later.data!=full.data && !later.reused);
 assert(static_cast<unsigned char*>(full.data)[2048]==0x5a);
 assert(static_cast<unsigned char*>(later.data)[2048]==0xa5);
 // Submitted writes cannot reach game-owned storage or another view.
 static_cast<unsigned char*>(full.data)[0]=0x42;
 assert(source[0]==0xa5 && static_cast<unsigned char*>(later.data)[0]==0xa5);
 assert(static_cast<unsigned char*>(alias.data)[0]==0x42);
 // Force table growth, collisions and the bounded ordinary-copy fallback.
 std::vector<std::array<unsigned char,64>> many(40000);
 idFrameSnapshotCopies growing;
 std::vector<void*> retained;
 for(std::size_t i=0;i<many.size();++i){
  many[i].fill(static_cast<unsigned char>(i));
  retained.push_back(growing.Copy(many[i].data(),many[i].size(),allocate).data);
 }
 for(std::size_t i=0;i<many.size();++i){
  auto again=growing.Copy(many[i].data(),many[i].size(),allocate);
  assert(static_cast<unsigned char*>(again.data)[17]==static_cast<unsigned char>(i));
  if(i<32768)assert(again.reused && again.data==retained[i]);
  else assert(!again.reused && again.data!=retained[i]);
 }
}
'''


def main():
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    header = (ROOT / "src/renderer/FrameSnapshotCopy.h").read_text(encoding="utf-8")
    snapshot = (ROOT / "src/renderer/FrameSnapshot.cpp").read_text(encoding="utf-8")
    preparation = function_body(snapshot, "static void R_SnapshotRenderViewInternal(")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with RegressionTemporaryDirectory(prefix="snapshot-copy-", dir=ROOT / ".tmp") as directory:
        temp = Path(directory)
        (temp / "FrameSnapshotRetain.h").write_text((ROOT / "src/renderer/FrameSnapshotRetain.h").read_text(), encoding="utf-8")
        for name, code, passed in (("production", header, True),
                                   ("borrowed-source", header.replace("return { copy, false };", "return { const_cast<void *>(source), false };"), False),
                                   ("size-blind", header.replace(" && entries[slot].bytes == bytes", "").replace(" ^ bytes;", ";"), False),
                                   ("late-preparation", header, False), ("stale-retention", header, False),
                                   ("whole-clear", header, False)):
            retention = (ROOT / "src/renderer/FrameSnapshotRetain.h").read_text()
            if name == "stale-retention":
                retention = retention.replace("std::memcmp( old->data(), source, bytes ) == 0", "true")
            if name == "whole-clear":
                retention = retention.replace("Erase( entries.find( recency.back() ) );", "Clear();")
                assert "Clear();" in retention
            (temp / "FrameSnapshotRetain.h").write_text(retention, encoding="utf-8")
            body = preparation.replace("snapshot.Surface( view->drawSurfs[i], true, true )", "snapshot.Surface( view->drawSurfs[i], true, false )") if name == "late-preparation" else preparation
            (temp / "copy.cpp").write_text(PREPARATION + body + PREPARATION_CASE + CASES, encoding="utf-8")
            (temp / "FrameSnapshotCopy.h").write_text(code, encoding="utf-8")
            exe = temp / (name + (".exe" if os.name == "nt" else ""))
            subprocess.run([compiler, "-std=c++20", str(temp / "copy.cpp"), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, timeout=20)
            if (result.returncode == 0) != passed:
                raise AssertionError(f"{name}: unexpected exit {result.returncode}")
    print("Snapshot copies: complete-view preparation, alias reuse, distinct ranges, later-view isolation, "
          "least-recently-used retention and five negative controls passed")


if __name__ == "__main__":
    main()
