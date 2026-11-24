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
struct vertCache_t {int tag=1; bool indexBuffer=false; int offset=0,size=0,vbo=0; void* virtMem=nullptr;};
struct srfTriangles_t {vertCache_t* ambientCache=nullptr; int numVerts=0,numIndexes=0; glIndex_t* indexes=nullptr; vertCache_t* indexCache=nullptr;};
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
int staticGeoQueries=0;
int VK_StaticGeo_VertexOffset(const srfTriangles_t*,size_t){++staticGeoQueries;return -1;}
int VK_Ring_Alloc(Ring& ring,const void* data,size_t bytes,int align){
    const int offset=(ring.used+align-1)&~(align-1);
    if(bytes>size_t(ring.capacity)||size_t(offset)+bytes>size_t(ring.capacity))return -1;
    std::memcpy(ring.bytes.data()+offset,data,bytes);ring.used=offset+int(bytes);++ring.uploads;return offset;
}
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
    std::puts("Vulkan GUI uploads: recycled vertex/index/GPU memos, world reuse and invalid/exhausted caches passed");
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
