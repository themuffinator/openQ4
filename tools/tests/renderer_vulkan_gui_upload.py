#!/usr/bin/env python3
"""Exercise production Vulkan uploads with recycled GUI cache addresses."""

from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body


ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / 'src/renderer/Vulkan/vk_GuiExecutor.cpp').read_text(encoding='utf-8')
    source = source[source.index('// Memoized ring upload:'):]
    fixture = r'''
#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
using glIndex_t = unsigned int;
constexpr int VK_FRAMES_IN_FLIGHT=2, VK_TRI_MEMO_SIZE=16, TAG_FREE=0;
struct idDrawVert {float x,y; unsigned char color[4];};
struct vertCache_t {int tag=1; bool indexBuffer=false; int offset=0,size=0,vbo=0; void* virtMem=nullptr; vertCache_t** user=nullptr;};
struct srfTriangles_t {vertCache_t* ambientCache=nullptr; int numVerts=0,numIndexes=0; glIndex_t* indexes=nullptr; vertCache_t* indexCache=nullptr;
    srfTriangles_t* ambientSurface=nullptr; bool deformedSurface=false;};
struct viewDef_t {void* viewEntitys=nullptr;};
struct {viewDef_t* viewDef=nullptr;} backEnd;
struct {void* Position(const vertCache_t* c){return static_cast<unsigned char*>(c->virtMem)+c->offset;}} vertexCache;
struct vkVertUpload_t {const void* vertKey=nullptr; int vertexOffset=-1;};
struct vkIdxUpload_t {const void* idxKey=nullptr; int indexOffset=-1;};
struct GpuMemo {const void* ambientKey=nullptr; int vertexOffset=-1;};
struct Ring {std::array<unsigned char,4096> bytes{}; int capacity=4096,used=0,uploads=0;};
struct State {
    std::array<vkVertUpload_t,VK_TRI_MEMO_SIZE> vertMemo{};
    std::array<vkIdxUpload_t,VK_TRI_MEMO_SIZE> idxMemo{};
    std::array<GpuMemo,1> gpuSkinningMemo{};
    std::array<Ring,VK_FRAMES_IN_FLIGHT> vertexRings{},indexRings{};
} vkExec;
int VK_GpuSkinning_FindMemo(const void* key,bool,bool& found){
    found=vkExec.gpuSkinningMemo[0].ambientKey==key;return found?0:-1;
}
// Resident static geometry is asked only for world streams: a 2D surface's
// header is recycled within the presentation and must reach the ring.
int staticGeoQueries=0,staticGeoVertexAnswer=-1;
int VK_StaticGeo_VertexOffset(const srfTriangles_t*,size_t){++staticGeoQueries;return staticGeoVertexAnswer;}
// A surface's own index list is offered to it only beside a resident vertex stream.
int surfaceIndexQueries=0,surfaceIndexAnswer=-1;
int VK_StaticGeo_SurfaceIndexOffset(const srfTriangles_t*,size_t){++surfaceIndexQueries;return surfaceIndexAnswer;}
// A list that is not the surface's own may be a lasting interaction's, with a block of its own.
int interactionIndexQueries=0,interactionIndexAnswer=-1;
int VK_StaticGeo_InteractionIndexOffset(const srfTriangles_t*,size_t){++interactionIndexQueries;return interactionIndexAnswer;}
int VK_Ring_Alloc(Ring& ring,const void* data,size_t bytes,int align){
    const int offset=(ring.used+align-1)&~(align-1);
    if(bytes>size_t(ring.capacity)||size_t(offset)+bytes>size_t(ring.capacity))return -1;
    std::memcpy(ring.bytes.data()+offset,data,bytes);ring.used=offset+int(bytes);++ring.uploads;return offset;
}
// r_vkResourceProfile: what the rings were filled with (rendererVulkanMemoryInfo)
struct {bool GetBool()const{return true;}} r_vkResourceProfile;
struct {unsigned long long vertexStreamBytes=0,indexBytes=0,residentSurfaceIndexBytes=0,ownedSurfaceIndexBytes=0,deformedVertexBytes=0;} vkResourceProfile;
constexpr int VK_STATIC_GEO_FLAG=0x40000000;
'''
    for signature in ('static bool VK_Exec_CPUCacheValid(', 'static bool VK_Exec_UploadTriGeometry('):
        fixture += function_body(source, signature) + '\n'
    fixture += r'''
int main(){
    std::array<idDrawVert,4> vertices{{{0,0,{255,255,255,255}},{1,0,{255,255,255,255}},
        {1,1,{255,255,255,255}},{0,1,{255,255,255,255}}}};
    std::array<glIndex_t,6> indices{{0,1,2,0,2,3}};
    vertCache_t vertexHeader;vertexHeader.size=sizeof(vertices);vertexHeader.virtMem=vertices.data();
    vertCache_t indexHeader;indexHeader.size=sizeof(indices);indexHeader.virtMem=indices.data();indexHeader.indexBuffer=true;
    srfTriangles_t tri{&vertexHeader,4,6,indices.data(),nullptr};
    viewDef_t view;backEnd.viewDef=&view;
    int v0,i0,v1,i1;
    assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0));
    // The ring profile counts what was copied: a streamed vertex stream and its index list.
    assert(vkResourceProfile.vertexStreamBytes==sizeof(vertices) && vkResourceProfile.indexBytes==sizeof(indices)
        && vkResourceProfile.residentSurfaceIndexBytes==0 && vkResourceProfile.ownedSurfaceIndexBytes==0
        && vkResourceProfile.deformedVertexBytes==0);
    // The screen-update allocator reuses headers and payload addresses.
    // The new rectangle must not inherit the old white full-screen quad.
    for(auto& v:vertices){v.x+=370;v.y+=67;v.color[1]=v.color[2]=0;}
    indices={{0,2,1,0,3,2}};
    assert(VK_Exec_UploadTriGeometry(0,&tri,v1,i1));
    assert(v1!=v0 && i1!=i0);
    assert(!std::memcmp(vkExec.vertexRings[0].bytes.data()+v1,vertices.data(),sizeof(vertices)));
    assert(!std::memcmp(vkExec.indexRings[0].bytes.data()+i1,indices.data(),sizeof(indices)));
    // A recycled header can also collide with a GPU-skinned world surface.
    vkExec.gpuSkinningMemo[0]={&vertexHeader,v0};
    assert(VK_Exec_UploadTriGeometry(0,&tri,v1,i1) && v1!=v0);
    assert(!std::memcmp(vkExec.vertexRings[0].bytes.data()+v1,vertices.data(),sizeof(vertices)));
    // The alternative CPU index-cache path needs the same lifetime rule.
    tri.indexes=nullptr;tri.indexCache=&indexHeader;
    assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0));indices[0]=3;
    assert(VK_Exec_UploadTriGeometry(0,&tri,v1,i1) && i1!=i0);
    assert(!std::memcmp(vkExec.indexRings[0].bytes.data()+i1,indices.data(),sizeof(indices)));

    // No 2D surface was offered to the resident cache.
    assert(staticGeoQueries==0);

    // World depth/ambient/light passes retain their existing upload reuse;
    // the resident cache is asked once per stream and frame, before the ring.
    vkExec=State{};view.viewEntitys=&view;
    assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0));
    assert(VK_Exec_UploadTriGeometry(0,&tri,v1,i1) && v0==v1 && i0==i1);
    assert(vkExec.vertexRings[0].uploads==1 && vkExec.indexRings[0].uploads==1);
    assert(staticGeoQueries==1);
    vkExec.gpuSkinningMemo[0]={&vertexHeader,1024};
    assert(VK_Exec_UploadTriGeometry(0,&tri,v1,i1) && v1==1024 && i1==i0);
    assert(vkExec.vertexRings[0].uploads==1);

    // Nothing above offered an index list to the resident cache: every vertex
    // stream was streamed, and a list held in an index cache block never is.
    assert(surfaceIndexQueries==0);
    staticGeoVertexAnswer=VK_STATIC_GEO_FLAG|4096;
    vkExec=State{};
    assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0) && v0==(VK_STATIC_GEO_FLAG|4096) && surfaceIndexQueries==0);
    // A surface whose vertex stream is resident offers the list it draws itself
    // with: after the per-frame memo, once a frame. While the list is not
    // resident yet the ring takes it, and the profile says whose it was.
    tri.indexes=indices.data();tri.indexCache=nullptr;vertexHeader.user=&tri.ambientCache;
    vkExec=State{};vkResourceProfile={};
    assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0) && v0==(VK_STATIC_GEO_FLAG|4096) && surfaceIndexQueries==1);
    assert(i0>=0 && (i0&VK_STATIC_GEO_FLAG)==0 && vkExec.indexRings[0].uploads==1 && vkExec.vertexRings[0].uploads==0);
    assert(vkResourceProfile.indexBytes==sizeof(indices) && vkResourceProfile.residentSurfaceIndexBytes==sizeof(indices)
        && vkResourceProfile.ownedSurfaceIndexBytes==sizeof(indices));
    assert(VK_Exec_UploadTriGeometry(0,&tri,v1,i1) && i1==i0 && surfaceIndexQueries==1 && vkExec.indexRings[0].uploads==1);
    // A resident list costs the ring nothing and is not counted as copied.
    surfaceIndexAnswer=VK_STATIC_GEO_FLAG|8192;
    vkExec=State{};vkResourceProfile={};
    assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0) && i0==(VK_STATIC_GEO_FLAG|8192) && surfaceIndexQueries==2);
    assert(vkExec.indexRings[0].uploads==0 && vkExec.vertexRings[0].uploads==0);
    assert(vkResourceProfile.indexBytes==0 && vkResourceProfile.residentSurfaceIndexBytes==0
        && vkResourceProfile.ownedSurfaceIndexBytes==0);
    assert(VK_Exec_UploadTriGeometry(0,&tri,v1,i1) && v1==v0 && i1==i0 && surfaceIndexQueries==2);
    // A streamed vertex stream keeps its list in the ring without asking,
    // and a 2D surface never asks, whatever the caches would answer.
    staticGeoVertexAnswer=-1;
    vkExec=State{};
    assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0) && (i0&VK_STATIC_GEO_FLAG)==0 && surfaceIndexQueries==2);
    staticGeoVertexAnswer=VK_STATIC_GEO_FLAG|4096;
    vkExec=State{};view.viewEntitys=nullptr;
    assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0) && (v0&VK_STATIC_GEO_FLAG)==0 && (i0&VK_STATIC_GEO_FLAG)==0
        && surfaceIndexQueries==2);
    view.viewEntitys=&view;
    staticGeoVertexAnswer=-1;surfaceIndexAnswer=-1;
    // An interaction's list. It is asked for when the surface's own lookup has
    // no answer, whether or not the vertex stream is resident, once a frame
    // after the memo; a resident one costs the ring nothing and is not counted
    // as copied. A surface whose own list was found is not asked again, and a
    // 2D surface never is.
    {
        const int asked=interactionIndexQueries;
        assert(asked>0);
        vkExec=State{};vkResourceProfile={};interactionIndexAnswer=VK_STATIC_GEO_FLAG|12288;
        assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0) && (v0&VK_STATIC_GEO_FLAG)==0 && i0==(VK_STATIC_GEO_FLAG|12288));
        assert(interactionIndexQueries==asked+1 && vkExec.indexRings[0].uploads==0 && vkExec.vertexRings[0].uploads==1);
        assert(vkResourceProfile.indexBytes==0 && vkResourceProfile.vertexStreamBytes==sizeof(vertices));
        assert(VK_Exec_UploadTriGeometry(0,&tri,v1,i1) && i1==i0 && interactionIndexQueries==asked+1);
        staticGeoVertexAnswer=VK_STATIC_GEO_FLAG|4096;vkExec=State{};vkResourceProfile={};
        assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0) && i0==(VK_STATIC_GEO_FLAG|12288) && interactionIndexQueries==asked+2);
        assert(vkResourceProfile.indexBytes==0 && vkResourceProfile.residentSurfaceIndexBytes==0);
        surfaceIndexAnswer=VK_STATIC_GEO_FLAG|8192;vkExec=State{};
        assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0) && i0==(VK_STATIC_GEO_FLAG|8192) && interactionIndexQueries==asked+2);
        surfaceIndexAnswer=-1;staticGeoVertexAnswer=-1;
        vkExec=State{};view.viewEntitys=nullptr;
        assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0) && (i0&VK_STATIC_GEO_FLAG)==0 && interactionIndexQueries==asked+2);
        view.viewEntitys=&view;interactionIndexAnswer=-1;
    }
    // The profile says how much of the streamed vertices belonged to a model
    // generated anew each frame: a skinned mesh, or the subset a light draws
    // over one. A resident stream is not counted at all.
    vkExec=State{};vkResourceProfile={};tri.deformedSurface=true;
    assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0));
    assert(vkResourceProfile.vertexStreamBytes==sizeof(vertices) && vkResourceProfile.deformedVertexBytes==sizeof(vertices));
    assert(VK_Exec_UploadTriGeometry(0,&tri,v1,i1) && vkResourceProfile.deformedVertexBytes==sizeof(vertices));
    srfTriangles_t lightTri{&vertexHeader,4,6,indices.data(),nullptr};lightTri.ambientSurface=&tri;
    vkExec=State{};vkResourceProfile={};
    assert(VK_Exec_UploadTriGeometry(0,&lightTri,v0,i0) && vkResourceProfile.deformedVertexBytes==sizeof(vertices));
    tri.deformedSurface=false;vkExec=State{};vkResourceProfile={};
    assert(VK_Exec_UploadTriGeometry(0,&lightTri,v0,i0));
    assert(vkResourceProfile.vertexStreamBytes==sizeof(vertices) && vkResourceProfile.deformedVertexBytes==0);
    tri.deformedSurface=true;staticGeoVertexAnswer=VK_STATIC_GEO_FLAG|4096;vkExec=State{};vkResourceProfile={};
    assert(VK_Exec_UploadTriGeometry(0,&tri,v0,i0));
    assert(vkResourceProfile.vertexStreamBytes==0 && vkResourceProfile.deformedVertexBytes==0);
    tri.deformedSurface=false;staticGeoVertexAnswer=-1;
    tri.indexes=nullptr;tri.indexCache=&indexHeader;

    view.viewEntitys=nullptr;vkExec=State{};
    vertexHeader.tag=TAG_FREE;
    assert(!VK_Exec_UploadTriGeometry(0,&tri,v1,i1) && v1==-1 && i1==-1);
    vertexHeader.tag=1;indexHeader.size=1;
    assert(!VK_Exec_UploadTriGeometry(0,&tri,v1,i1));indexHeader.size=sizeof(indices);
    assert(!VK_Exec_UploadTriGeometry(-1,&tri,v1,i1));
    assert(!VK_Exec_UploadTriGeometry(VK_FRAMES_IN_FLIGHT,&tri,v1,i1));
    assert(!VK_Exec_UploadTriGeometry(0,nullptr,v1,i1));
    vkExec.vertexRings[0].capacity=1;
    assert(!VK_Exec_UploadTriGeometry(0,&tri,v1,i1));
    vkExec=State{};vkExec.indexRings[0].capacity=1;
    assert(!VK_Exec_UploadTriGeometry(0,&tri,v1,i1));
    std::puts("Vulkan GUI uploads: recycled vertex/index/GPU memos, world reuse, resident index lists and invalid/exhausted caches passed");
}
'''
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vulkan-gui-upload-', dir=ROOT / '.tmp') as folder:
        cpp, binary = Path(folder) / 'upload.cpp', Path(folder) / 'upload.exe'
        cpp.write_text(fixture, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
