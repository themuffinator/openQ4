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
 // A view records bytes owned elsewhere (a retained lease) without copying
 // them, and never lets them displace a copy the view already took.
 idFrameSnapshotCopies remembered;
 unsigned char range[32]={},owned[32]={1},other[16]={2};
 assert(remembered.Find(range,32)==nullptr);
 const auto beforeRemember=blocks.size();
 remembered.Remember(range,32,owned,allocate);
 assert(remembered.Find(range,32)==owned && remembered.Find(range,16)==nullptr);
 const auto known=remembered.Copy(range,32,allocate);
 assert(known.reused && known.data==owned && blocks.size()==beforeRemember+1);
 const auto ownCopy=remembered.Copy(range,16,allocate);
 remembered.Remember(range,16,other,allocate);
 assert(!ownCopy.reused && remembered.Find(range,16)==ownCopy.data);

 // Retained static vertex streams. Unchanged ranges share one owned version.
 {
 using streams_t=idFrameSnapshotRetainedStreams;
 auto byteAt=[](const streams_t::lease_t& lease,std::size_t i){return static_cast<const unsigned char*>(lease->data())[i];};
 streams_t streams(256);
 unsigned char verts[60];for(std::size_t i=0;i<sizeof(verts);++i)verts[i]=static_cast<unsigned char>(i);
 auto first=streams.Acquire(verts,sizeof(verts));
 assert(first.lease && !first.reused && first.lease->size()==sizeof(verts) && first.lease->data()!=verts);
 assert(reinterpret_cast<std::uintptr_t>(first.lease->data())%16==0);
 auto shared=streams.Acquire(verts,sizeof(verts));
 assert(shared.reused && shared.lease==first.lease);
 // Lengths are part of the identity, and need not be word sized.
 auto prefix=streams.Acquire(verts,33);
 assert(prefix.lease && !prefix.reused && prefix.lease!=first.lease && prefix.lease->size()==33);
 assert(streams.Acquire(verts,33).reused && streams.Acquire(verts,sizeof(verts)).reused);
 assert(streams.Size()==2 && streams.ResidentBytes()==93);
 // Replacement: a rewrite in place is never served from the stale version.
 // The range is refused twice (ordinary per-view copies), then versioned anew.
 verts[40]=0xee;
 auto rewritten=streams.Acquire(verts,sizeof(verts));
 assert(!rewritten.lease && !rewritten.reused && streams.ResidentBytes()==33);
 assert(!streams.Acquire(verts,sizeof(verts)).lease);
 auto replacement=streams.Acquire(verts,sizeof(verts));
 assert(replacement.lease && !replacement.reused && replacement.lease!=first.lease && byteAt(replacement.lease,40)==0xee);
 assert(streams.Acquire(verts,sizeof(verts)).reused && streams.Acquire(verts,33).reused);
 // Lifetime: the submitted version outlives its replacement until released.
 std::weak_ptr<const streams_t::block_t> oldVersion=first.lease;
 assert(byteAt(first.lease,40)==40 && byteAt(shared.lease,59)==59);
 first.lease.reset();assert(!oldVersion.expired());
 shared.lease.reset();assert(oldVersion.expired());
 // Data rewritten again and again is posed or deformed: the refusal run
 // doubles to its cap, so such a range stays on per-view copies.
 unsigned lastRun=0;
 for(unsigned expected=4;;expected=expected*2>streams_t::maximumRefusals?streams_t::maximumRefusals:expected*2){
  ++verts[41];
  unsigned run=0;
  while(!streams.Acquire(verts,sizeof(verts)).lease)++run;
  assert(run==expected);
  if(run==streams_t::maximumRefusals&&lastRun==run)break;
  lastRun=run;
 }
 // Clear and shutdown drop cached versions, never a submitted lease.
 std::weak_ptr<const streams_t::block_t> held=replacement.lease;
 streams.Clear();
 assert(streams.Size()==0 && streams.ResidentBytes()==0 && !held.expired() && byteAt(replacement.lease,40)==0xee);
 replacement.lease.reset();assert(held.expired());
 // Eviction: pressure drops the least recently acquired versions one at a
 // time; an evicted version stays readable through its lease.
 streams_t pressureStreams(64);
 unsigned char sa[16],sb[16],sc[16],sd[32],huge[65]={};
 std::memset(sa,1,sizeof(sa));std::memset(sb,2,sizeof(sb));std::memset(sc,3,sizeof(sc));std::memset(sd,4,sizeof(sd));
 auto leasedA=pressureStreams.Acquire(sa,sizeof(sa));
 pressureStreams.Acquire(sb,sizeof(sb));pressureStreams.Acquire(sc,sizeof(sc));
 assert(pressureStreams.Acquire(sa,sizeof(sa)).reused);
 pressureStreams.Acquire(sd,sizeof(sd));
 assert(pressureStreams.Size()==3 && pressureStreams.ResidentBytes()==64);
 assert(pressureStreams.Acquire(sa,sizeof(sa)).reused && pressureStreams.Acquire(sc,sizeof(sc)).reused && pressureStreams.Acquire(sd,sizeof(sd)).reused);
 assert(!pressureStreams.Acquire(sb,sizeof(sb)).reused && pressureStreams.ResidentBytes()==64);
 assert(!pressureStreams.Acquire(sa,sizeof(sa)).reused && byteAt(leasedA.lease,15)==1);
 // Oversized ranges are never retained; the caller keeps its ordinary copy.
 assert(!pressureStreams.Acquire(huge,sizeof(huge)).lease && !pressureStreams.Acquire(nullptr,16).lease && !pressureStreams.Acquire(sa,0).lease);
 // A refused range holds no bytes, and returning from refusal respects the budget.
 sd[0]=9;
 assert(!pressureStreams.Acquire(sd,sizeof(sd)).lease && !pressureStreams.Acquire(sd,sizeof(sd)).lease);
 const auto residentWhileRefused=pressureStreams.ResidentBytes();
 assert(pressureStreams.Acquire(sd,sizeof(sd)).lease && residentWhileRefused<=32 && pressureStreams.ResidentBytes()<=64);
 // One-shot ranges beyond the entry cap displace the oldest one at a time.
 streams_t cappedStreams;
 std::vector<std::array<unsigned char,16>> oneShotStreams(streams_t::maximumEntries+513);
 for(std::size_t i=0;i<oneShotStreams.size();++i){oneShotStreams[i].fill(static_cast<unsigned char>(i));cappedStreams.Acquire(oneShotStreams[i].data(),16);}
 assert(cappedStreams.Size()==streams_t::maximumEntries && cappedStreams.ResidentBytes()==streams_t::maximumEntries*16);
 assert(cappedStreams.Acquire(oneShotStreams.back().data(),16).reused && !cappedStreams.Acquire(oneShotStreams.front().data(),16).reused);
 }

 // Leases: one owner per version per holder epoch, and the holder alone keeps
 // a replaced or evicted version readable.
 {
 using holder_t=std::vector<std::shared_ptr<const void>>;
 idFrameSnapshotRetainedIndexes leasedIndexes(64);
 std::uint32_t li[4]={5,6,7,8};
 holder_t frameA,frameB;bool reused=false;
 const std::uint32_t* v1=leasedIndexes.Lease(li,sizeof(li),3,frameA,reused);
 assert(v1 && !reused && frameA.size()==1 && v1!=li && v1[2]==7);
 assert(leasedIndexes.Lease(li,sizeof(li),3,frameA,reused)==v1 && reused && frameA.size()==1);
 // Another holder (the other frame slot, or this one after it emptied) owns it too.
 assert(leasedIndexes.Lease(li,sizeof(li),4,frameB,reused)==v1 && reused && frameB.size()==1);
 li[2]=70;
 const std::uint32_t* v2=leasedIndexes.Lease(li,sizeof(li),3,frameA,reused);
 assert(v2 && v2!=v1 && !reused && v2[2]==70 && frameA.size()==2);
 leasedIndexes.Clear();frameB.clear();
 assert(v1[2]==7 && v2[2]==70);
 std::weak_ptr<const void> held=frameA.front();frameA.clear();assert(held.expired());
 // Acquire keeps returning owners of its own.
 auto owned=leasedIndexes.Acquire(li,sizeof(li));
 assert(owned.lease && !owned.reused && leasedIndexes.Lease(li,sizeof(li),5,frameA,reused)==owned.lease->data() && reused);

 idFrameSnapshotRetainedStreams leasedStreams(256);
 unsigned char lv[48];for(std::size_t i=0;i<sizeof(lv);++i)lv[i]=static_cast<unsigned char>(i+1);
 holder_t slot0,slot1;
 const void* s1=leasedStreams.Lease(lv,sizeof(lv),7,slot0,reused);
 assert(s1 && !reused && slot0.size()==1 && reinterpret_cast<std::uintptr_t>(s1)%16==0);
 assert(leasedStreams.Lease(lv,sizeof(lv),7,slot0,reused)==s1 && reused && slot0.size()==1);
 assert(leasedStreams.Lease(lv,sizeof(lv),8,slot1,reused)==s1 && slot1.size()==1);
 // A rewrite drops the version from the cache; the refused range holds nothing.
 lv[3]=0xee;
 assert(!leasedStreams.Lease(lv,sizeof(lv),7,slot0,reused) && !leasedStreams.Lease(lv,sizeof(lv),7,slot0,reused));
 const void* s2=leasedStreams.Lease(lv,sizeof(lv),7,slot0,reused);
 assert(s2 && s2!=s1 && slot0.size()==2 && static_cast<const unsigned char*>(s2)[3]==0xee);
 leasedStreams.Clear();slot1.clear();
 assert(static_cast<const unsigned char*>(s1)[3]==4);
 }

 // Per-view identities: insertion marks, values, growth, and a fresh view
 // that remembers nothing of the previous one.
 {
 idFrameSnapshotIdentities ids;
 ids.Begin();
 int keys[3000];int values[3000];
 assert(ids.Find(&keys[0])==nullptr && !ids.Contains(&keys[0]));
 bool inserted=false;
 ids.Insert(&keys[0],&inserted);assert(inserted && ids.Contains(&keys[0]) && ids.Find(&keys[0])==nullptr);
 ids.Insert(&keys[0],&inserted);assert(!inserted);
 for(int i=0;i<3000;++i)ids.Insert(&keys[i])=&values[i];
 assert(ids.Size()==3000 && ids.Capacity()>=6000);
 for(int i=0;i<3000;++i)assert(ids.Find(&keys[i])==&values[i]);
 ids.Begin();
 assert(ids.Size()==0 && ids.Find(&keys[0])==nullptr && !ids.Contains(&keys[1234]));
 ids.Insert(&keys[1234],&inserted);assert(inserted && ids.Find(&keys[1234])==nullptr);
 ids.Insert(&keys[1234])=&values[1];assert(ids.Find(&keys[1234])==&values[1] && ids.Find(&keys[1235])==nullptr);
 ids.Release();assert(ids.Capacity()==0 && ids.Find(&keys[1234])==nullptr);
 ids.Begin();ids.Insert(&keys[9])=&values[9];assert(ids.Find(&keys[9])==&values[9]);
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
        retained = (ROOT / "src/renderer/FrameSnapshotRetain.h").read_text()
        indexes, marker, streams = retained.partition("class idFrameSnapshotRetainedStreams {")
        assert marker

        def mutate(text, old, new):
            assert old in text, old
            return text.replace(old, new)

        for name, code, passed in (("production", header, True),
                                   ("borrowed-source", header.replace("return { copy, false };", "return { const_cast<void *>(source), false };"), False),
                                   ("size-blind", header.replace(" && entries[slot].bytes == bytes", "").replace(" ^ bytes;", ";"), False),
                                   ("remember-overwrites", mutate(header, " || Find( source, bytes ) != nullptr", ""), False),
                                   ("late-preparation", header, False), ("stale-retention", header, False),
                                   ("whole-clear", header, False), ("stale-stream", header, False),
                                   ("stream-whole-clear", header, False), ("stream-eager-retry", header, False),
                                   ("lease-unheld", header, False), ("lease-epoch-blind", header, False),
                                   ("identity-stale-view", mutate(header, "if ( ++generation == 0 ) {", "if ( generation == 0 ) {"), False)):
            index_code, stream_code = indexes, streams
            if name == "stale-retention":
                index_code = mutate(indexes, "std::memcmp( old->data(), source, bytes ) == 0", "true")
            if name == "whole-clear":
                index_code = mutate(indexes, "Erase( entries.find( recency.back() ) );", "Clear();")
            if name == "stale-stream":
                stream_code = mutate(streams, "std::memcmp( entry.lease->data(), source, bytes ) == 0", "true")
            if name == "stream-whole-clear":
                stream_code = mutate(streams, "Erase( entries.find( recency.back() ) );", "Clear();")
            if name == "stream-eager-retry":
                stream_code = mutate(streams, "entry.refusals = entry.nextRefusals - 1;", "entry.refusals = 0;")
            if name == "lease-unheld":
                index_code = mutate(indexes, "holder.push_back( entry->lease );", "")
            if name == "lease-epoch-blind":
                stream_code = mutate(streams, "if ( entry->leaseEpoch != epoch ) {", "if ( entry->leaseEpoch == 0 ) {")
            (temp / "FrameSnapshotRetain.h").write_text(index_code + marker + stream_code, encoding="utf-8")
            body = preparation.replace("snapshot.Surface( view->drawSurfs[i], true, true )", "snapshot.Surface( view->drawSurfs[i], true, false )") if name == "late-preparation" else preparation
            (temp / "copy.cpp").write_text(PREPARATION + body + PREPARATION_CASE + CASES, encoding="utf-8")
            (temp / "FrameSnapshotCopy.h").write_text(code, encoding="utf-8")
            exe = temp / (name + (".exe" if os.name == "nt" else ""))
            subprocess.run([compiler, "-std=c++20", str(temp / "copy.cpp"), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, timeout=20)
            if (result.returncode == 0) != passed:
                raise AssertionError(f"{name}: unexpected exit {result.returncode}")
    print("Snapshot copies: complete-view preparation, alias reuse, distinct ranges, later-view isolation, "
          "least-recently-used index retention, retained static vertex streams (replacement, lifetime, "
          "eviction, refusal of rewritten ranges), one lease per version per frame, per-view identity "
          "tables and twelve negative controls passed")


if __name__ == "__main__":
    main()
