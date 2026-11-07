#!/usr/bin/env python3
"""Run the production retained target allocator against a counted renderer.

Checks mixed viewport sizes, the shared byte budget and non-destructive failed
allocation, and that layer composites sample GL row order on every backend,
which the Vulkan executor's render-texture contract (pinned below) provides.
No graphics device, game window or host input is accessed.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace openq4::ui {
struct Vertex {float x=0,y=0,u=0,v=0,r=1,g=1,b=1,a=1;};
struct Bounds {float x=0,y=0,width=0,height=0;};
}
struct idImageOpts {int width=0,height=0,format=0,numLevels=0;bool isPersistant=false;};
enum {FMT_RGBA8,TF_NEAREST,DS_DEFAULTED};
struct idImage {};
struct idMaterial {int GetState() const {return 1;}};
struct idRenderTexture {bool destroyed=false;};
struct Renderer {
    int creates=0,destroys=0,clears=0,defaultBinds=0;bool failImage=false;
    std::vector<std::unique_ptr<idImage>> images;
    std::vector<std::unique_ptr<idRenderTexture>> targets;
    idRenderTexture* bound=nullptr;
    idImage* CreateImage(const char*,idImageOpts*,int){
        ++creates;if(failImage)return nullptr;
        images.push_back(std::make_unique<idImage>());return images.back().get();
    }
    idRenderTexture* CreateRenderTexture(idImage*,void*){
        targets.push_back(std::make_unique<idRenderTexture>());return targets.back().get();
    }
    void DestroyRenderTexture(idRenderTexture* target){assert(target && !target->destroyed);target->destroyed=true;++destroys;}
    // Only a composite may bind the default target (null); allocation never does.
    void BindRenderTexture(idRenderTexture* target,void*){assert(!target || !target->destroyed);defaultBinds+=!target;bound=target;}
    void ClearRenderTarget(bool color,bool depth,int,float r,float g,float b,float a){assert(color && !depth && r==0 && g==0 && b==0 && a==0);++clears;}
} renderer;
Renderer* renderSystem=&renderer;
struct Declarations {idMaterial material;const idMaterial* FindMaterial(const char*){return &material;}} declarations;
Declarations* declManager=&declarations;
const char* va(const char* format,unsigned value){static char text[128];std::snprintf(text,sizeof text,format,value);return text;}
struct Host {
    struct Layer {idRenderTexture* target=nullptr;const idMaterial* material=nullptr;const idMaterial* maskMaterial=nullptr;int width=0,height=0;};
    std::vector<Layer> layers;
    idRenderTexture* boundTarget=nullptr;
    int viewportWidth=1280,viewportHeight=720;
    std::vector<openq4::ui::Vertex> drawn;std::uintptr_t drawnMaterial=0;
    void Draw(const std::vector<openq4::ui::Vertex>& vertices,const std::vector<int>& indices,std::uintptr_t material){
        assert(indices.size()==6);drawn=vertices;drawnMaterial=material;
    }
'''
MAIN = r'''
};
int main(){
    Host host;
    assert(host.BeginLayer(1,1280,720));
    auto first=host.layers[0].target;
    assert(host.BeginLayer(2,800,600));
    auto second=host.layers[1].target;
    assert(host.layers[0].target==first && !first->destroyed && renderer.destroys==0);
    assert(host.BeginLayer(1,1280,720) && renderer.creates==2 && renderer.bound==first);
    assert(host.BeginLayer(1,640,480));
    assert(first->destroyed && renderer.destroys==1 && host.layers[1].target==second && !second->destroyed);
    const int created=renderer.creates,cleared=renderer.clears;
    auto resized=host.layers[0].target;
    assert(!host.BeginLayer(0,100,100) && !host.BeginLayer(49,100,100));
    assert(!host.BeginLayer(3,0,100) && !host.BeginLayer(3,100,-1));
    assert(!host.BeginLayer(3,8192,8192)); // Existing targets count toward the 256 MiB cap.
    assert(!host.BeginLayer(1,8192,8192)); // Budget failure must not destroy this old target.
    assert(renderer.creates==created && renderer.clears==cleared);
    assert(host.layers[0].target==resized && !resized->destroyed && !second->destroyed);
    assert(host.BeginLayer(3,4096,4096));
    renderer.failImage=true;
    assert(!host.BeginLayer(4,320,200));
    assert(host.layers[0].target==resized && !resized->destroyed && !second->destroyed);
    renderer.failImage=false;
    Host empty;
    assert(empty.BeginLayer(48,8192,8192)); // Sparse slot ID is not a memory multiplier.
    assert(!empty.BeginLayer(1,1,1));
    // Composites sample every backend's layers in GL row order: the view's
    // top row is v=1. Sampling Vulkan layers top-origin mirrored them onto
    // empty rows, so every faded or masked element vanished there.
    assert(renderer.defaultBinds==0);
    const auto near=[](float a,float b){return std::fabs(a-b)<1e-5f;};
    host.CompositeLayer(2,0,.5f,{100,200,300,50});
    assert(renderer.defaultBinds==1 && renderer.bound==nullptr && host.drawn.size()==4);
    assert(host.drawnMaterial==reinterpret_cast<std::uintptr_t>(host.layers[1].material));
    assert(near(host.drawn[0].y,200) && near(host.drawn[0].u,100.f/1280) && near(host.drawn[0].v,1-200.f/720));
    assert(near(host.drawn[2].y,250) && near(host.drawn[2].u,400.f/1280) && near(host.drawn[2].v,1-250.f/720));
    for (const auto& vertex : host.drawn) assert(near(vertex.r,.5f) && near(vertex.a,.5f)); // Premultiplied opacity.
    host.MaskLayer(1,2,{0,0,1280,720});
    assert(renderer.bound==host.layers[1].target && near(host.drawn[0].v,1) && near(host.drawn[2].v,0) && near(host.drawn[0].a,1));
    const std::vector<openq4::ui::Vertex> mask={{100,200,0,0,1,0,0,.25f},{400,200,0,0,0,1,0,.5f},
        {400,250,0,0,0,0,1,.75f},{100,250,0,0,1,1,1,1}};
    host.CompositeMaskedLayer(2,mask,{0,1,2,0,2,3},.5f);
    assert(renderer.bound==nullptr && host.drawnMaterial==reinterpret_cast<std::uintptr_t>(host.layers[1].material));
    for(size_t i=0;i<mask.size();++i){
        const auto& v=host.drawn[i];
        assert(near(v.x,mask[i].x) && near(v.y,mask[i].y) && near(v.u,mask[i].x/1280) && near(v.v,1-mask[i].y/720));
        assert(near(v.r,mask[i].a*.5f) && near(v.g,v.r) && near(v.b,v.r) && near(v.a,v.r));
    }
    std::puts("retained layer pool: mixed viewport preservation, exact shared byte budget, failure isolation and GL-order composites passed");
}
'''


DRAW_SUPPORT = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
typedef unsigned char byte;
typedef int glIndex_t;
namespace openq4::ui { struct Vertex {float x=0,y=0,u=0,v=0,r=1,g=1,b=1,a=1;}; }
struct idVec3 {float x=0,y=0,z=0; void Set(float a,float b,float c){x=a;y=b;z=c;}};
struct idVec2 {float x=0,y=0; void Set(float a,float b){x=a;y=b;}};
struct idDrawVert {idVec3 xyz; idVec2 st; idVec3 normal; idVec3 tangents[2]; byte color[4]={}; byte color2[4]={}; void Clear(){*this=idDrawVert{};}};
template<class T> struct idList {std::vector<T> items; void SetNum(int n){items.resize(n);} T& operator[](int i){return items[i];} T* Ptr(){return items.data();}};
template<class T> T Min(T a,T b){return a<b?a:b;}
template<class T> T Max(T a,T b){return a>b?a:b;}
struct idStr {static int Icmpn(const char* a,const char* b,int n){return std::strncmp(a,b,n);} static int Icmp(const char* a,const char* b){return std::strcmp(a,b);}};
struct idMath {static float ClampFloat(float low,float high,float value){return value<low?low:value>high?high:value;}};
struct idMaterial {std::string name; const char* GetName() const {return name.c_str();}};
struct Renderer {
    std::vector<idDrawVert> drawn; std::vector<glIndex_t> indices; const idMaterial* material=nullptr;
    int calls=0,largestVertices=0; size_t largestIndices=0,totalIndices=0;
    void SetColor4(float,float,float,float){}
    void DrawStretchPic(const idDrawVert* vertices,const glIndex_t* used,int count,int indexCount,const idMaterial* surface,bool clip){
        assert(!clip && count>0 && indexCount%3==0);
        for (int i=0;i<indexCount;++i) assert(used[i]>=0 && used[i]<count);
        drawn.assign(vertices,vertices+count); indices.assign(used,used+indexCount); material=surface;
        ++calls; largestVertices=std::max(largestVertices,count); largestIndices=std::max(largestIndices,size_t(indexCount)); totalIndices+=indexCount;
    }
} renderer;
Renderer* renderSystem=&renderer;
struct Declarations {idMaterial solid{"_retainedSolid"}; const idMaterial* FindMaterial(const char*){return &solid;}} declarations;
Declarations* declManager=&declarations;
// These allocation/conversion fixtures submit immediately. The batching
// fixture compiles and exercises the production queue and its ordering rules.
// This fixture qualifies indexed mesh submission with atlas caching disabled.
// Rasterization itself is covered by the retained runtime and capture suites.
struct {bool GetBool()const{return false;}} ui_retainedAtlas;
static bool RetainedAtlasActive(){return ui_retainedAtlas.GetBool();}
struct Host {
    int viewportWidth=1280,viewportHeight=720;
    bool rasterNewMeshes=true;
    struct AtlasPage {bool dirty=false;const idMaterial* material=nullptr;const idMaterial* multiplyMaterial=nullptr;std::uint64_t lastFrame=0;int references=0;};
    std::vector<AtlasPage> atlasPages;
    unsigned rasterDraws=0,rasterSavedVertices=0;
    std::uint64_t RenderFrame()const{return 1;}
    template<class... T>void CacheRaster(T&&...){assert(false && "atlas disabled in indexed mesh fixture");}
'''
DRAW_MAIN = r'''
};
int main(){
    // Half-tinted premultiplied quad: rgb and a at 0.5.
    std::vector<openq4::ui::Vertex> quad(4);
    for (auto& vertex : quad) {vertex.r=vertex.g=vertex.b=vertex.a=.5f;}
    const std::vector<int> indices{0,1,2,0,2,3};
    Host host;
    idMaterial additive{"_retainedAdd/gfx/guis/mainmenu/q4text"},picture{"_retained/gfx/guis/mainmenu/level_mcc"};
    // Additive light keeps its premultiplied tint and carries no coverage, so
    // it never writes a composition layer's alpha (an opaque box on composite).
    host.Draw(quad,indices,reinterpret_cast<std::uintptr_t>(&additive));
    // Each vertex converts once: the quad's four corners, still indexed.
    assert(renderer.material==&additive && renderer.drawn.size()==4 && renderer.indices==std::vector<glIndex_t>({0,1,2,0,2,3}));
    for (const auto& vertex : renderer.drawn) assert(vertex.color[0]==128 && vertex.color[3]==0);
    // A straight image is unpremultiplied and keeps its coverage.
    host.Draw(quad,indices,reinterpret_cast<std::uintptr_t>(&picture));
    for (const auto& vertex : renderer.drawn) assert(vertex.color[0]==255 && vertex.color[3]==128);
    // Generated pixels already carry premultiplied alpha. The engine keeps
    // their tint premultiplied too, unlike ordinary file-backed pictures.
    idMaterial generated{"_retainedGenerated/1"};
    host.Draw(quad,indices,reinterpret_cast<std::uintptr_t>(&generated));
    for (const auto& vertex : renderer.drawn) assert(vertex.color[0]==128 && vertex.color[3]==128);
    // A retained mesh converts once per revision, and its release frees it.
    const auto pictureHandle=reinterpret_cast<std::uintptr_t>(&picture);
    host.DrawMesh(7,1,quad,indices,pictureHandle);
    for (auto& vertex : quad) vertex.r=.25f;
    quad[1].x=100;
    host.DrawMesh(7,1,quad,indices,pictureHandle);
    for (const auto& vertex : renderer.drawn) assert(vertex.color[0]==255);
    host.DrawMesh(7,2,quad,indices,pictureHandle);
    for (const auto& vertex : renderer.drawn) assert(vertex.color[0]==128);
    assert(renderer.drawn[1].xyz.x==50);
    host.viewportWidth=640;
    host.DrawMesh(7,2,quad,indices,pictureHandle);
    assert(renderer.drawn[1].xyz.x==100); // A new view size converts again.
    host.viewportWidth=1280;
    host.ReleaseMesh(7);
    assert(host.meshes.empty());
    // Large meshes split into whole-triangle chunks under the engine's surface
    // limits, each passing the contiguous vertex range it uses.
    std::vector<openq4::ui::Vertex> strip(4*7000);
    std::vector<int> cells;
    for (int cell=0;cell<7000;++cell) { const int base=4*cell; cells.insert(cells.end(),{base,base+1,base+2,base,base+2,base+3}); }
    renderer.calls=renderer.largestVertices=0; renderer.largestIndices=renderer.totalIndices=0;
    host.Draw(strip,cells,0);
    assert(renderer.calls==3 && renderer.largestVertices==12000 && renderer.largestIndices<=36000 && renderer.totalIndices==cells.size());
    assert(renderer.material==&declarations.solid);
    // A triangle spanning more than a chunk draws from its own copy.
    std::vector<openq4::ui::Vertex> far(13001);
    far[13000].x=7;
    renderer.calls=0;
    host.Draw(far,{0,13000,1},0);
    assert(renderer.calls==1 && renderer.drawn.size()==3 && renderer.drawn[1].xyz.x==7*.5f);
    std::puts("retained host draw: additive pictures write no coverage, straight images keep theirs, meshes convert once per revision and chunk within surface limits");
}
'''


SOFT_SUPPORT = r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
typedef unsigned char byte;
typedef int glIndex_t;
namespace openq4::ui { struct Bounds {float x=0,y=0,width=0,height=0;}; }
struct idVec3 {float x=0,y=0,z=0; void Set(float a,float b,float c){x=a;y=b;z=c;}};
struct idVec2 {float x=0,y=0; void Set(float a,float b){x=a;y=b;}};
struct idDrawVert {idVec3 xyz; idVec2 st; idVec3 normal; idVec3 tangents[2]; byte color[4]={}; byte color2[4]={}; void Clear(){*this=idDrawVert{};}};
template<class T> T Min(T a,T b){return a<b?a:b;}
template<class T> T Max(T a,T b){return a>b?a:b;}
struct idImageOpts {int width=0,height=0,format=0,numLevels=0;bool isPersistant=false;};
enum {FMT_RGBA8,TF_NEAREST,TF_LINEAR=3,DS_DEFAULTED=9};
struct idImage {std::string name;int width=0,height=0,filter=0;};
struct idMaterial {std::string name;int state=1;int GetState() const {return state;}};
struct idRenderTexture {idImage* image=nullptr;bool destroyed=false;};
struct Call {std::string op;const void* target=nullptr;std::string name;float parms[4]={};std::vector<idDrawVert> vertices;const idMaterial* material=nullptr;};
struct Renderer {
    std::vector<Call> calls;
    std::vector<std::unique_ptr<idImage>> images;
    std::vector<std::unique_ptr<idRenderTexture>> targets;
    idRenderTexture* bound=reinterpret_cast<idRenderTexture*>(1);
    int fontResets=0;
    void ResetRetainedFontCache(){++fontResets;}
    idImage* CreateImage(const char* name,idImageOpts* options,int filter){
        assert(options->format==FMT_RGBA8 && options->numLevels==1 && options->isPersistant);
        images.push_back(std::make_unique<idImage>(idImage{name,options->width,options->height,filter}));
        calls.push_back({"image",images.back().get(),name});return images.back().get();
    }
    idRenderTexture* CreateRenderTexture(idImage* image,void*){
        targets.push_back(std::make_unique<idRenderTexture>(idRenderTexture{image}));return targets.back().get();
    }
    void DestroyRenderTexture(idRenderTexture* target){if(!target)return;assert(!target->destroyed);target->destroyed=true;calls.push_back({"destroy",target});}
    void BindRenderTexture(idRenderTexture* target,void*){assert(!target || !target->destroyed);bound=target;calls.push_back({"bind",target});}
    void CaptureRenderToImage(const char* name){assert(!bound);calls.push_back({"capture",nullptr,name});}
    void ClearRenderTarget(bool color,bool depth,int,float r,float g,float b,float a){
        assert(color && !depth && r==0 && g==0 && b==0 && a==0);calls.push_back({"clear",bound});
    }
    void SetColor4(float r,float g,float b,float a){Call call{"color"};call.parms[0]=r;call.parms[1]=g;call.parms[2]=b;call.parms[3]=a;calls.push_back(call);}
    void DrawStretchPic(const idDrawVert* vertices,const glIndex_t* indices,int count,int indexCount,const idMaterial* material,bool clip){
        assert(count==4 && indexCount==6 && !clip && indices[0]==0 && indices[5]==3);
        Call call{"draw",bound};call.vertices.assign(vertices,vertices+count);call.material=material;calls.push_back(call);
    }
} renderer;
Renderer* renderSystem=&renderer;
struct Declarations {
    idMaterial backdrop{"_retainedBlur/backdrop"},scratch{"_retainedBlur/scratch"},streak{"_retainedBlur/streak"};
    idMaterial reducedScratch{"_retainedBlur/reducedScratch"},reducedResult{"_retainedBlur/reducedResult"};
    const idMaterial* FindMaterial(const char* name){
        for(idMaterial* material:{&backdrop,&scratch,&streak,&reducedScratch,&reducedResult}) if(std::string(name)==material->name) return material;
        return nullptr;
    }
} declarations;
Declarations* declManager=&declarations;
const char* va(const char* format,unsigned value){static char text[128];std::snprintf(text,sizeof text,format,value);return text;}
struct {bool enabled=true;bool GetBool()const{return enabled;}} ui_retainedDirectBackdrop;
struct {bool enabled=false;bool GetBool()const{return enabled;}} ui_retainedHalfBlur;
struct Host {
    struct Layer {idRenderTexture* target=nullptr;int width=0,height=0;};
    std::vector<Layer> layers;
    idRenderTexture* boundTarget=nullptr;
    std::vector<int> atlasPages;
    int viewportWidth=1600,viewportHeight=900;
    float viewportX=160,viewportY=90;
    int captureWidth=1920,captureHeight=1080;
    bool softFocus=true;
    idRenderTexture* blurScratch=nullptr;
    const idMaterial* blurBackdropMaterial=nullptr;
    const idMaterial* blurScratchMaterial=nullptr;
    const idMaterial* blurStreakMaterial=nullptr;
    const idMaterial* linearMaskMaterial=nullptr;
    bool backdropCreated=false;
    int blurWidth=0,blurHeight=0;
    idRenderTexture *reducedBlurScratch=nullptr,*reducedBlurResult=nullptr;
    const idMaterial *reducedBlurMaterial=nullptr,*reducedBlurResolve=nullptr;
    int reducedBlurWidth=0,reducedBlurHeight=0;
    std::vector<int> meshes;
    std::vector<std::string> scalableFaces,fonts;
    bool fontFallbackReported=false;
    // Generated image ownership has its own production adapter/runtime fixture.
    struct GeneratedImages {template<class F> void Reset(F release){}} generatedImages;
    // Model targets have their own fixture; this host owns none.
    static constexpr unsigned ModelCapacity=0;
    void FreeModel(unsigned){std::abort();}
    std::array<const idMaterial*,64> generatedMaterials{};
    std::uintptr_t generatedProbe=0;
    bool ShrinkGeneratedImage(unsigned){return true;}
'''
SOFT_MAIN = r'''
};
static bool Near(float a,float b){return std::fabs(a-b)<1e-5f;}
static int Count(const char* op){int n=0;for(const auto& call:renderer.calls)n+=call.op==op;return n;}
int main(){
    Host host;
    idRenderTexture destination;host.layers.resize(3);host.layers[2].target=&destination;
    const openq4::ui::Bounds region{100,200,300,100};
    // A host without soft focus, or an inactive destination, leaves the layer.
    host.softFocus=false;
    assert(!host.SoftenBackdrop(3,10,.8f,region) && renderer.calls.empty());
    host.softFocus=true;
    assert(!host.SoftenBackdrop(0,10,.8f,region) && !host.SoftenBackdrop(2,10,.8f,region) && !host.SoftenBackdrop(4,10,.8f,region));
    assert(!host.SoftenBackdrop(3,NAN,.8f,region) && !host.SoftenBackdrop(3,10,1.5f,region) && renderer.calls.empty());
    assert(!host.SoftenBackdrop(3,10,.8f,{2000,2000,10,10}) && renderer.calls.empty()); // Off the view.
    assert(host.SoftenBackdrop(3,10,.8f,region));
    // One linear capture image the window's size and a view-sized scratch target.
    assert(renderer.images.size()==2 && renderer.images[0]->name=="_retainedBackdrop" && renderer.images[0]->width==1920 &&
        renderer.images[0]->height==1080 && renderer.images[0]->filter==TF_LINEAR);
    assert(renderer.images[1]->name=="_retainedBlurScratch" && renderer.images[1]->width==1600 && renderer.images[1]->height==900 &&
        renderer.images[1]->filter==TF_LINEAR && renderer.targets.size()==1 && host.blurScratch==renderer.targets[0].get());
    std::vector<Call> passes;
    for(const auto& call:renderer.calls) if(call.op!="image") passes.push_back(call);
    // The base is bound before the window is copied; the horizontal pass draws
    // into the cleared scratch, the vertical one into the destination layer,
    // which stays bound for the runtime's composite.
    const std::vector<std::string> order{"bind","capture","bind","clear","color","draw","color","bind","color","draw","color"};
    assert(passes.size()==order.size());
    for(size_t i=0;i<order.size();++i) assert(passes[i].op==order[i]);
    assert(passes[0].target==nullptr && passes[1].name=="_retainedBackdrop" && passes[2].target==host.blurScratch && passes[3].target==host.blurScratch);
    assert(passes[7].target==&destination && renderer.bound==&destination);
    // parm0..3: the step along the pass's axis in its source's UV, the sigma
    // in pixels and the saturation, then white again for later geometry.
    assert(Near(passes[4].parms[0],1.f/1920) && passes[4].parms[1]==0 && passes[4].parms[2]==10 && passes[4].parms[3]==1);
    assert(passes[8].parms[0]==0 && Near(passes[8].parms[1],1.f/900) && passes[8].parms[2]==10 && Near(passes[8].parms[3],.8f));
    assert(passes[6].parms[0]==1 && passes[10].parms[3]==1);
    assert(passes[5].material==&declarations.backdrop && passes[9].material==&declarations.scratch);
    // The horizontal pass covers the rows the vertical one reads (3 sigma),
    // and samples the window copy where the view sits in it; both sample GL
    // row order, the view's top row at the top of v.
    const auto& wide=passes[5].vertices;
    const float sx=640.f/1600,sy=480.f/900;
    assert(Near(wide[0].xyz.x,100*sx) && Near(wide[0].xyz.y,170*sy) && Near(wide[2].xyz.x,400*sx) && Near(wide[2].xyz.y,330*sy));
    assert(Near(wide[0].st.x,(160+100)/1920.f) && Near(wide[0].st.y,1-(90+170)/1080.f) &&
        Near(wide[2].st.x,(160+400)/1920.f) && Near(wide[2].st.y,1-(90+330)/1080.f));
    const auto& tall=passes[9].vertices;
    assert(Near(tall[0].xyz.y,200*sy) && Near(tall[2].xyz.y,300*sy));
    assert(Near(tall[0].st.x,100/1600.f) && Near(tall[0].st.y,1-200/900.f) && Near(tall[2].st.x,400/1600.f) && Near(tall[2].st.y,1-300/900.f));
    for(const auto& vertex:tall) assert(vertex.color[0]==255 && vertex.color[3]==255);
    // The speed streak copies the whole view out of the window capture into
    // the scratch target unblurred, then streaks it into the destination layer
    // in the view's own coordinates, with the strength in parm0.
    renderer.calls.clear();
    assert(!host.StreakBackdrop(3,0,region) && !host.StreakBackdrop(3,1.5f,region) && !host.StreakBackdrop(3,NAN,region) &&
        !host.StreakBackdrop(2,.5f,region) && renderer.calls.empty());
    assert(host.StreakBackdrop(3,.5f,region));
    std::vector<Call> streak;
    for(const auto& call:renderer.calls) if(call.op!="image") streak.push_back(call);
    const std::vector<std::string> streakOrder{"bind","capture","bind","color","draw","color","bind","color","draw","color"};
    assert(streak.size()==streakOrder.size());
    for(size_t i=0;i<streakOrder.size();++i) assert(streak[i].op==streakOrder[i]);
    assert(streak[0].target==nullptr && streak[1].name=="_retainedBackdrop" && streak[2].target==host.blurScratch &&
        streak[6].target==&destination && renderer.bound==&destination);
    assert(streak[4].material==&declarations.backdrop && streak[3].parms[2]==0 && streak[3].parms[3]==1);
    const auto& copy=streak[4].vertices;
    assert(Near(copy[0].xyz.x,0) && Near(copy[0].xyz.y,0) && Near(copy[2].xyz.x,1600*sx) && Near(copy[2].xyz.y,900*sy));
    assert(Near(copy[0].st.x,160/1920.f) && Near(copy[0].st.y,1-90/1080.f) && Near(copy[2].st.x,(160+1600)/1920.f) &&
        Near(copy[2].st.y,1-(90+900)/1080.f));
    assert(streak[8].material==&declarations.streak && Near(streak[7].parms[0],.5f) && streak[9].parms[0]==1);
    const auto& smear=streak[8].vertices;
    assert(Near(smear[0].xyz.x,100*sx) && Near(smear[0].xyz.y,200*sy) && Near(smear[2].xyz.x,400*sx) && Near(smear[2].xyz.y,300*sy));
    assert(Near(smear[0].st.x,100/1600.f) && Near(smear[0].st.y,1-200/900.f) && Near(smear[2].st.x,400/1600.f) &&
        Near(smear[2].st.y,1-300/900.f));
    // A defaulted streak material fails openly and leaves soft focus working.
    host.blurStreakMaterial=nullptr;declarations.streak.state=DS_DEFAULTED;renderer.calls.clear();
    assert(!host.StreakBackdrop(3,.5f,region) && Count("draw")==0 && !host.blurStreakMaterial && host.SoftenBackdrop(3,4,.9f,region));
    declarations.streak.state=1;
    // A region past the view is clamped to it, reach included.
    renderer.calls.clear();
    assert(host.SoftenBackdrop(3,4,.9f,{-50,-50,1700,1000}) && renderer.images.size()==2 && renderer.targets.size()==1);
    assert(Count("clear")==0); // Both passes cover the view; scratch is wholly overwritten.
    for(const auto& call:renderer.calls) if(call.op=="draw") for(const auto& vertex:call.vertices)
        assert(vertex.xyz.x>=-1e-4f && vertex.xyz.x<=640.0001f && vertex.xyz.y>=-1e-4f && vertex.xyz.y<=480.0001f);
    const openq4::ui::Bounds fullView{0,0,1600,900};
    renderer.calls.clear();
    assert(host.CanSoftenBackdropDirect(4,.9f,fullView) && renderer.calls.empty());
    assert(!host.CanSoftenBackdropDirect(4,.9f,region) && !host.CanSoftenBackdropDirect(NAN,.9f,fullView));
    ui_retainedDirectBackdrop.enabled=false;
    assert(!host.SoftenBackdropDirect(4,.9f,fullView) && renderer.calls.empty());ui_retainedDirectBackdrop.enabled=true;
    assert(host.SoftenBackdropDirect(4,.9f,fullView) && Count("capture")==1 && Count("draw")==2 && Count("clear")==0);
    assert(renderer.calls.back().op=="color" && renderer.bound==nullptr);
    const Call* finalDraw=nullptr;for(const auto& call:renderer.calls)if(call.op=="draw")finalDraw=&call;
    assert(finalDraw && finalDraw->target==nullptr && finalDraw->material==host.blurScratchMaterial);
    // A new view size replaces only the scratch target.
    host.viewportWidth=1280;host.viewportHeight=720;
    renderer.calls.clear();assert(!host.SoftenBackdropDirect(4,.9f,fullView) && renderer.calls.empty());
    assert(host.SoftenBackdrop(3,4,.9f,region) && renderer.images.size()==3 && renderer.images[2]->width==1280 &&
        renderer.targets.size()==2 && renderer.targets[0]->destroyed && host.blurScratch==renderer.targets[1].get());
    // Clearing the layers releases the blur targets; the next use rebuilds both.
    host.ClearLayers();
    assert(renderer.targets[1]->destroyed && !host.blurScratch && !host.backdropCreated && host.layers.empty());
    destination.destroyed=false;host.layers.resize(3);host.layers[2].target=&destination;
    assert(host.SoftenBackdrop(3,4,.9f,region) && renderer.images.size()==5 && renderer.images[3]->name=="_retainedBackdrop");
    // A defaulted pass material fails openly and keeps no target.
    host.ClearLayers();destination.destroyed=false;host.layers.resize(3);host.layers[2].target=&destination;
    declarations.scratch.state=DS_DEFAULTED;
    assert(!host.SoftenBackdrop(3,4,.9f,region) && !host.blurScratch && renderer.targets.back()->destroyed);
    // A fatal error in startup tears the host down before any renderer loads:
    // the reset drops every handle and calls nothing.
    destination.destroyed=false;host.layers.resize(3);host.layers[2].target=&destination;host.blurScratch=&destination;
    host.meshes.push_back(1);host.scalableFaces.push_back("face");host.fonts.push_back("font");host.fontFallbackReported=true;
    const size_t callsBefore=renderer.calls.size();const int resetsBefore=renderer.fontResets;
    renderSystem=nullptr;host.Reset();renderSystem=&renderer;
    assert(renderer.calls.size()==callsBefore && renderer.fontResets==resetsBefore && !destination.destroyed);
    assert(host.layers.empty() && !host.blurScratch && host.meshes.empty() && host.scalableFaces.empty() && host.fonts.empty() &&
        !host.fontFallbackReported && !host.backdropCreated);
    // With a renderer the reset clears its font cache and destroys the targets.
    host.layers.resize(1);host.layers[0].target=&destination;
    host.Reset();
    assert(renderer.fontResets==resetsBefore+1 && destination.destroyed && host.layers.empty());
    // Filtering precedes decimation on each axis; captures remain live, and
    // the final linear resolve covers the full base surface.
    declarations.scratch.state=1; ui_retainedHalfBlur.enabled=true;
    Host reduced; reduced.viewportWidth=reduced.captureWidth=1280;
    reduced.viewportHeight=reduced.captureHeight=720; reduced.viewportX=reduced.viewportY=0;
    const openq4::ui::Bounds reducedView{0,0,1280,720};
    renderer.calls.clear();
    assert(reduced.DrawSoftenedBackdrop(nullptr,7.5f,.8f,reducedView));
    assert(Count("capture")==1 && Count("draw")==3 && Count("clear")==0);
    assert(reduced.reducedBlurScratch->image->width==640 && reduced.reducedBlurScratch->image->height==720);
    assert(reduced.reducedBlurResult->image->width==640 && reduced.reducedBlurResult->image->height==360);
    assert(renderer.bound==nullptr);
    const size_t imagesBefore=renderer.images.size();
    renderer.calls.clear();
    assert(reduced.DrawSoftenedBackdrop(nullptr,7.5f,.8f,reducedView));
    assert(Count("capture")==1 && Count("draw")==3 && renderer.images.size()==imagesBefore);
    for(float sigma:{0.f,5.99f,8.01f,256.f}) {
        renderer.calls.clear();
        assert(reduced.DrawSoftenedBackdrop(nullptr,sigma,.8f,reducedView));
        assert(Count("draw")==2 && Count("capture")==1);
    }
    reduced.viewportX=1; renderer.calls.clear();
    assert(reduced.DrawSoftenedBackdrop(nullptr,7.5f,.8f,reducedView) && Count("draw")==2);
    reduced.viewportX=0;
    // Odd dimensions do not land exactly between source texels, so keep the
    // ordinary kernel instead of applying half-centred pairs at a wrong phase.
    reduced.viewportWidth=reduced.captureWidth=1279; reduced.viewportHeight=reduced.captureHeight=719;
    renderer.calls.clear();
    assert(reduced.DrawSoftenedBackdrop(nullptr,7.5f,.8f,reducedView));
    assert(Count("draw")==2);
    reduced.ClearLayers();
    assert(!reduced.reducedBlurScratch && !reduced.reducedBlurResult && !reduced.reducedBlurMaterial && !reduced.reducedBlurResolve);
    std::puts("retained soft focus and speed streak: capture, separable passes, the view copy, regions, GL row order, target lifetime and a reset with no renderer passed");
}
'''


def main():
    source=(ROOT/'src/ui/RetainedUI.cpp').read_text()
    source_text=source
    bodies=[function_body(source,signature).replace(' override','') for signature in (
        'void BindTarget(','bool BeginLayer(','void CompositeLayer(','void CompositeMaskedLayer(','void MaskLayer(','void DrawLayer(')]
    # The Vulkan executor's side of the row-order contract the composites rely on.
    executor=(ROOT/'src/renderer/Vulkan/vk_GuiExecutor.cpp').read_text(encoding='utf-8')
    assert 'vkExec.activePipelineTarget.lowerOrigin = true;' in function_body(executor,'bool VK_Exec_SetRenderTarget('),\
        'Vulkan render textures must keep GL row order, which retained layer composites sample'
    assert 'VK_Exec_MarkCanonicalWrites();' in function_body(executor,'void VK_GuiExecutor_Draw2DView('),\
        '2D writes into a layer must be marked as canonical rows'
    assert 'passPlan.textureFlipY = imageEntry->materialSampleFlipY;' in executor,\
        'GUI stages must flip an image written in the other row order'
    code=SUPPORT+'\n'.join(bodies)+MAIN
    compiler=next((found for name in ('clang++','g++','c++') if (found:=shutil.which(name))),None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-layer-pool-',dir=ROOT/'.tmp') as temp:
        source=Path(temp)/'pool.cpp';binary=Path(temp)/'pool.exe'
        source.write_text(code,encoding='utf-8')
        subprocess.run([compiler,'-std=c++17',str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
        draw=Path(temp)/'draw.cpp';drawBinary=Path(temp)/'draw.exe'
        draw_bodies=[function_body(source_text,'struct HostMesh {')+';','HostMesh scratchMesh;','std::unordered_map<std::uint64_t,HostMesh> meshes;']+[
            function_body(source_text,signature).replace(' override','') for signature in (
                'void Draw(','void DrawMesh(','void DrawRaster(','void ReleaseMesh(','void DropRaster(','const idMaterial* Material(','void Convert(','void Submit(const HostMesh&')]
        draw.write_text(DRAW_SUPPORT+'\n'.join(draw_bodies)+DRAW_MAIN,encoding='utf-8')
        subprocess.run([compiler,'-std=c++17',str(draw),'-o',str(drawBinary)],check=True)
        subprocess.run([str(drawBinary)],check=True)
        soft=Path(temp)/'soft.cpp';softBinary=Path(temp)/'soft.exe'
        soft_bodies=[function_body(source_text,signature).replace(' override','') for signature in (
            'void BindTarget(','bool SoftenBackdrop(','bool CanSoftenBackdropDirect(','bool SoftenBackdropDirect(','bool DrawSoftenedBackdrop(',
            'bool StreakBackdrop(','bool EnsureBlurTargets(','bool EnsureReducedBlurTargets(','void DrawBlurPass(','void ClearLayers(','void Reset(')]
        atlas_constants='\n'.join(line for line in source_text.splitlines() if 'static constexpr int Atlas' in line)
        soft.write_text(SOFT_SUPPORT+atlas_constants+'\n'+'\n'.join(soft_bodies)+SOFT_MAIN,encoding='utf-8')
        subprocess.run([compiler,'-std=c++17',str(soft),'-o',str(softBinary)],check=True)
        subprocess.run([str(softBinary)],check=True)


if __name__=='__main__':
    main()
