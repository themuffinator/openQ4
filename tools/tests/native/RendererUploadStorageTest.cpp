// Counted GL boundary for actual upload-manager allocation/lifetime methods.
// No native driver, window, input, or engine launch. GPL-3.0-or-later.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>
using byte = unsigned char;
using GLenum = unsigned; using GLbitfield = unsigned; using GLint = int;
using GLuint = unsigned; using GLuint64 = std::uint64_t;
using GLsizeiptr = std::ptrdiff_t; using GLsizeiptrARB = std::ptrdiff_t;
using GLsync = void*;
constexpr unsigned GL_NO_ERROR=0, GL_ARRAY_BUFFER=1, GL_ARRAY_BUFFER_ARB=1,
    GL_BUFFER_SIZE=2, GL_STREAM_DRAW_ARB=3, GL_MAP_WRITE_BIT=4,
    GL_MAP_PERSISTENT_BIT=8, GL_MAP_COHERENT_BIT=16, GL_DYNAMIC_STORAGE_BIT=32;
template<class T> struct idList {};
struct renderBackendCaps_t;
#define private public
#include "src/renderer/RendererUpload.h"
#undef private
static unsigned checks=0;
#define CHECK(expr) do { ++checks; if (!(expr)) throw std::runtime_error(std::string("FAIL: ")+ #expr); } while(0)
struct CVar { int value; int GetInteger() const {return value;} bool GetBool() const {return value!=0;} };
static CVar r_rendererUploadMegs{2},r_rendererUploadFrameBuffers{3},r_rendererUploadPersistent{1};
struct renderBackendCaps_t {bool hasSync=true,hasBufferStorage=true,hasMapBufferRange=true,hasVBO=true;};
static struct {struct {bool persistentMappedUploads=true;} renderFeatures;} glConfig;
static struct DriverReport {unsigned flags=0;} quirks;
constexpr unsigned RENDERER_DRIVER_QUIRK_DISABLE_PERSISTENT_UPLOADS=1;
static const DriverReport& RendererDriverQuirks_LastReport(){return quirks;}
struct idMath {static int ClampInt(int a,int b,int c){return std::clamp(c,a,b);}};
struct Common {template<class... A> void Printf(const char*,A...){} template<class... A> void Warning(const char*,A...){} } commonObject;
static Common* common=&commonObject;
static bool rendererThread=true;
[[maybe_unused]] static bool R_ImagePolicyRendererThread(){return rendererThread;}
static constexpr int RENDERER_UPLOAD_MIN_FRAME_BUFFERS=3, RENDERER_UPLOAD_MIN_MEGS=1, RENDERER_UPLOAD_MAX_MEGS=128;
static std::atomic<uint64_t> rg_uploadStorageGeneration{1};
static idUploadManager rg_uploadManager;
struct Backend {
    std::map<unsigned,int> buffers;
    unsigned next=1,bound=0,error=0,calls=0,allocated=0,deleted=0,mapped=0,unmapped=0;
    std::string failure;
    int failAt=0,seen=0;
    bool persistentOnly=false;
    bool Hit(const char* stage,bool persistent=false){
        ++calls;
        return failure==stage && (!persistentOnly||persistent) && (++seen==failAt || failAt<0);
    }
} gl;
static unsigned GetError(){++gl.calls;auto e=gl.error;gl.error=0;return e;}
static void Gen(int count,unsigned* name){CHECK(count==1&&gl.error==0);if(gl.Hit("name")){*name=0;return;}*name=gl.next++;gl.buffers[*name]=0;++gl.allocated;if(gl.Hit("name-error"))gl.error=9;}
static void Delete(int count,const unsigned* name){CHECK(count==1);++gl.calls;CHECK(gl.buffers.erase(*name)==1);++gl.deleted;if(gl.bound==*name)gl.bound=0;}
static void Allocate(unsigned target,std::ptrdiff_t bytes,const void*,unsigned,bool persistent){
    CHECK(target==GL_ARRAY_BUFFER);CHECK(gl.bound&&gl.buffers.contains(gl.bound));
    if(gl.Hit("allocate",persistent)){gl.error=9;return;}
    gl.buffers[gl.bound]=int(bytes)-(gl.Hit("size",persistent)?1:0);
}
static void Data(unsigned t,std::ptrdiff_t n,const void* p,unsigned f){Allocate(t,n,p,f,false);}
static void Storage(unsigned t,std::ptrdiff_t n,const void* p,unsigned f){Allocate(t,n,p,f,true);}
static void GetSize(unsigned t,unsigned field,int* out){CHECK(t==GL_ARRAY_BUFFER&&field==GL_BUFFER_SIZE);CHECK(gl.bound&&gl.buffers.contains(gl.bound));*out=gl.buffers[gl.bound];if(gl.Hit("query"))gl.error=9;}
static void* Map(unsigned,std::ptrdiff_t,std::ptrdiff_t,unsigned){
    if(gl.Hit("map-null",true))return nullptr;
    ++gl.mapped;if(gl.Hit("map-error",true))gl.error=9;
    return reinterpret_cast<void*>(std::uintptr_t(gl.bound)*4096);
}
static unsigned char Unmap(unsigned){++gl.calls;++gl.unmapped;return 1;}
static GLsync Fence(unsigned,unsigned){return nullptr;}
static unsigned Wait(GLsync,unsigned,GLuint64){return 0;}
static void DeleteSync(GLsync){++gl.calls;}
static auto glGetError=&GetError;
static auto glGenBuffersARB=&Gen;
static auto glDeleteBuffersARB=&Delete;
static auto glBufferDataARB=&Data;
static auto glBufferStorage=&Storage;
static auto glGetBufferParameterivARB=&GetSize;
static auto glMapBufferRange=&Map;
static auto glUnmapBuffer=&Unmap;
static auto glFenceSync=&Fence;
static auto glClientWaitSync=&Wait;
static auto glDeleteSync=&DeleteSync;
struct idVertexCache {
    static void InvalidateBufferBindings(){}
    static void BindArrayBuffer(unsigned name){++gl.calls;if(name)CHECK(gl.error==0);if(gl.Hit("bind")){gl.error=9;return;}gl.bound=name;}
};
static void R_GLStateCache_InvalidateBufferBinding(unsigned,const char*){}

// Irrelevant allocator/stat bookkeeping is counted independently from native
// storage, so requested capacity cannot itself make the actual query succeed.
idBufferAllocator::idBufferAllocator() {}
void idBufferAllocator::Init(int n,bool p){capacityBytes=n;persistentMapped=p;}
void idBufferAllocator::Shutdown(){capacityBytes=0;persistentMapped=false;}
void idBufferAllocator::BeginFrame(){}
idRingBuffer::idRingBuffer() {}
void idRingBuffer::Init(int n,bool p){capacityBytes=n;persistentMapped=p;}
void idRingBuffer::Shutdown(){capacityBytes=0;persistentMapped=false;}
void idRingBuffer::BeginFrame(){}
int idRingBuffer::Capacity() const{return capacityBytes;}
idLegacyStreamBuffer::idLegacyStreamBuffer() {}
void idLegacyStreamBuffer::Init(bool){}
void idUploadManager::UpdateAllocatorStats(){}
bool idUploadManager::SelectFrameBufferForFrame(int){return true;}
// @PRODUCTION@

static rendererUploadStorage_t Sentinel(){return {999,99,99,99,99,99,true};}
static void Refused(idUploadManager& manager){
    auto out=Sentinel();const auto before=out;const auto calls=gl.calls;
    CHECK(!manager.QueryStorage(out));CHECK(!std::memcmp(&out,&before,sizeof(out)));CHECK(gl.calls==calls);
}
static renderBackendCaps_t Caps(unsigned path){
    renderBackendCaps_t caps;
    caps.hasMapBufferRange=path>=2;caps.hasBufferStorage=path==3;
    return caps;
}
static void Reset(){gl={};rendererThread=true;quirks.flags=0;r_rendererUploadMegs.value=2;r_rendererUploadFrameBuffers.value=3;r_rendererUploadPersistent.value=1;}
static void Basic(){
    for(unsigned path:{1u,2u,3u})for(int buffers:{3,4,8})for(int megs:{1,16,128}){
        Reset();r_rendererUploadMegs.value=megs;r_rendererUploadFrameBuffers.value=buffers;
        idUploadManager manager;Refused(manager);manager.Init(Caps(path));
        auto out=Sentinel();auto calls=gl.calls;CHECK(manager.QueryStorage(out));CHECK(gl.calls==calls);
        CHECK(out.generation&&out.path==path&&out.requestedMegs==megs&&out.requestedBuffers==buffers);
        CHECK(out.bytesPerBuffer==megs*1024*1024&&out.bufferCount==buffers&&!out.persistentFallback);
        CHECK(gl.buffers.size()==unsigned(buffers)&&gl.bound==0);
        for(auto& [name,size]:gl.buffers)CHECK(name&&size==out.bytesPerBuffer);
        rendererThread=false;Refused(manager);rendererThread=true;
        const auto generation=out.generation;manager.Shutdown();Refused(manager);
        CHECK(gl.buffers.empty()&&gl.allocated==gl.deleted&&gl.mapped==gl.unmapped);
        manager.Init(Caps(path));CHECK(manager.QueryStorage(out));CHECK(out.generation>generation);manager.Shutdown();
    }
}
static void Failures(){
    for(unsigned path:{1u,2u})for(const char* failure:{"name","name-error","allocate","size","query","bind"})for(int at:{1,2,3}){
        Reset();gl.failure=failure;gl.failAt=at;idUploadManager manager;manager.Init(Caps(path));
        Refused(manager);CHECK(gl.buffers.empty());CHECK(gl.allocated==gl.deleted);
        CHECK(!manager.stats.dynamicFrameBridge&&!manager.stats.staticBufferAllocator&&manager.FrameCapacity()==0);
        manager.Shutdown();
    }
    for(const char* failure:{"map-null","map-error","allocate","size"})for(int at:{1,2,3}){
        Reset();gl.failure=failure;gl.failAt=at;gl.persistentOnly=true;
        idUploadManager manager;manager.Init(Caps(3));rendererUploadStorage_t out{};CHECK(manager.QueryStorage(out));
        CHECK(out.path==2&&out.persistentFallback&&!manager.hasSync&&!manager.stats.fenceSyncAvailable);
        CHECK(!manager.stats.persistentMapped&&!manager.ring.persistentMapped&&!manager.allocator.persistentMapped);
        CHECK(gl.buffers.size()==3);manager.Shutdown();CHECK(gl.allocated==gl.deleted&&gl.mapped==gl.unmapped);
    }
    Reset();gl.failure="allocate";gl.failAt=-1;idUploadManager manager;manager.Init(Caps(3));Refused(manager);
    CHECK(gl.buffers.empty()&&gl.allocated==gl.deleted);manager.Shutdown();
    Reset();auto caps=Caps(1);caps.hasVBO=false;manager.Init(caps);Refused(manager);CHECK(gl.buffers.empty());manager.Shutdown();
}
static void Orphan(){
    for(unsigned path:{1u,2u})for(const char* failure:{"allocate","size","query","bind"}){
        Reset();idUploadManager manager;manager.Init(Caps(path));rendererUploadStorage_t out{};CHECK(manager.QueryStorage(out));
        manager.BeginFrame(1);CHECK(manager.QueryStorage(out));
        gl.failure=failure;gl.failAt=1;gl.seen=0;manager.BeginFrame(2);
        Refused(manager);CHECK(gl.buffers.empty()&&!manager.stats.dynamicFrameBridge&&manager.FrameCapacity()==0);
        manager.Shutdown();CHECK(gl.allocated==gl.deleted);
    }
    Reset();idUploadManager manager;manager.Init(Caps(3));rendererUploadStorage_t out{};CHECK(manager.QueryStorage(out));
    manager.frameBuffers[0].mapped=nullptr;Refused(manager);
    manager.frameBuffers[0].mapped=reinterpret_cast<byte*>(std::uintptr_t(manager.frameBuffers[0].vbo)*4096);
    manager.storage.bytesPerBuffer++;Refused(manager);manager.storage.bytesPerBuffer--;
    manager.storage.generation=0;Refused(manager);manager.Shutdown();
}
static void Unavailable(){
    Reset();glGetBufferParameterivARB=nullptr;idUploadManager manager;manager.Init(Caps(1));Refused(manager);
    CHECK(gl.buffers.empty());manager.Shutdown();glGetBufferParameterivARB=&GetSize;
    Reset();gl.error=9;manager.Init(Caps(1));Refused(manager);CHECK(gl.buffers.empty());manager.Shutdown();
    Reset();const auto before=rg_uploadStorageGeneration.load();rg_uploadStorageGeneration=(std::numeric_limits<uint64_t>::max)();
    manager.Init(Caps(3));Refused(manager);CHECK(gl.buffers.empty());manager.Shutdown();rg_uploadStorageGeneration=before;
}
int main(){try{Basic();Failures();Orphan();Unavailable();std::printf("%u checks passed\n",checks);return 0;}catch(const std::exception& e){std::printf("%s\n",e.what());return 1;}}
