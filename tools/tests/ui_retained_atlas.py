#!/usr/bin/env python3
"""Exercise production coverage atlas tiling, uploads and bounded fallback.

Uses the indexed-draw fixture's renderer double. Production rasterization,
packing, submission, worker queueing, rectangle uploads and retirement
methods are compiled without rewriting; a job-system double runs the
rasters when the test says the workers have.
GPU colour/coverage is separately checked with engine captures on both APIs.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body
from ui_retained_layer_pool import DRAW_SUPPORT

ROOT = Path(__file__).resolve().parents[2]


def member_line(source: str, text: str) -> str:
    """The host's own declaration line holding text, so the fixture keeps it verbatim."""
    lines = [line.strip() for line in source.splitlines() if text in line]
    if len(lines) != 1:
        raise AssertionError(f'expected one host line holding {text!r}, found {len(lines)}')
    return lines[0] + '\n'


def main():
    source = (ROOT / 'src/ui/RetainedUI.cpp').read_text(encoding='utf-8')
    raster = (ROOT / 'src/ui/retained/CoverageRaster.h').read_text(encoding='utf-8')
    render_system = (ROOT / 'src/renderer/RenderSystem.cpp').read_text(encoding='utf-8')
    region = function_body(render_system, 'bool idRenderSystemLocal::UploadImageRegion(')
    # A rectangle reaches only an image a whole upload shaped, inside it.
    for required in ('!image->IsScratchImage() && image->GetUsage() != TD_LOOKUP_TABLE_RGBA', 'opts.format != FMT_RGBA8', 'opts.numLevels != 1',
                     'width > opts.width - x', 'height > opts.height - y', 'rowPitch < width',
                     'memcpy( packed.Ptr() + row * width * 4, data + row * rowPitch * 4, width * 4 );',
                     'image->SubImageUpload( 0, x, y, 0, width, height, packed.Ptr() );',
                     'R_RenderThread_Sync( "image" );'):
        assert required in region, required
    api = (ROOT / 'src/renderer/RenderSystem.h').read_text(encoding='utf-8')
    assert api.rindex('virtual bool UploadImageRegion(') > api.rindex('virtual void BeginGuiRoot()'), 'appended slot'
    assert '#define RENDER_API_VERSION\t\t\t36' in (ROOT / 'src/renderer/RenderModuleAPI.h').read_text(encoding='utf-8')
    # Vulkan stages a pitched rectangle's own rows, packed: no read past its last pixel.
    vulkan = function_body((ROOT / 'src/renderer/Vulkan/vk_Image.cpp').read_text(encoding='utf-8'), 'void idImage::SubImageUpload(')
    assert 'dataBytes = (size_t)width * height * info.bytesPerBlock;' in vulkan
    assert 'const uint8_t *src = (const uint8_t *)pic + row * sourcePitch;' in vulkan
    assert 'const uint32_t rowLengthTexels = 0;' in vulkan

    support = '#include <cmath>\n' + DRAW_SUPPORT.split('// This fixture qualifies indexed mesh submission')[0]
    support = support.replace('#include <vector>', '#include <vector>\n#include <functional>\n#include <memory>\n#include <set>')
    support = support.replace('const char* GetName() const', 'int GetState()const{return 0;} const char* GetName() const')
    support = support.replace('idMaterial solid{"_retainedSolid"}; const idMaterial* FindMaterial(const char*){return &solid;}',
                              'std::unordered_map<std::string,idMaterial> materials; const idMaterial* FindMaterial(const char* name){if(reentry && !std::strncmp(name,"_retainedAtlas/",15))Reenter();return &materials.try_emplace(name,idMaterial{name}).first->second;}')
    support = support.replace('struct Renderer {', r'''
enum {FMT_RGBA8, TF_NEAREST, DS_DEFAULTED=9};
struct idImageOpts {int width=0,height=0,format=0,numLevels=0;bool isPersistant=false;};
struct idImage {};
const char* va(const char* format,unsigned index){static char text[128];std::snprintf(text,sizeof(text),format,index);return text;}
// A print (a decl load, a warning) inside a map change redraws the loading
// screen, a retained root of its own (PacifierUpdate): the test's hook.
// The hook may clear itself; it runs as a copy, so its closure outlives that.
std::function<void()> reentry;
void Reenter(){if(auto hook=reentry)hook();}
struct Common {int warnings=0;void Warning(const char*,...){++warnings;Reenter();}} commonObject;
Common* common=&commonObject;
// The job system's double: lists run when the test says the workers have.
enum class idJobSubmitResult {ACCEPTED,EXECUTED_SYNCHRONOUSLY,QUEUE_FULL,INVALID_STATE};
struct idJobContext {void* data=nullptr;bool IsCancellationRequested()const{return false;}};
typedef void (*idJobFunction)(const idJobContext&);
struct idJobList;
struct JobSystem {
    bool workers=true,refuse=false;std::set<idJobList*> lists;int submitted=0;
    bool IsInitialized()const{return workers;} bool IsSynchronous()const{return false;}
    std::unique_ptr<idJobList> CreateJobList(const char*,size_t);
    void RunAll();
} jobSystem;
struct idJobList {
    std::vector<std::pair<idJobFunction,void*>> jobs;bool submitted=false,done=false;
    ~idJobList(){jobSystem.lists.erase(this);}
    bool AddJob(idJobFunction function,void* data){assert(!submitted);jobs.push_back({function,data});return true;}
    idJobSubmitResult Submit(){if(jobSystem.refuse)return idJobSubmitResult::QUEUE_FULL;submitted=true;++jobSystem.submitted;return idJobSubmitResult::ACCEPTED;}
    bool TryWait()const{return done;}
    bool Wait(){assert(done);return true;}
    void Run(){for(auto& job:jobs){idJobContext context;context.data=job.second;job.first(context);}done=true;}
};
std::unique_ptr<idJobList> JobSystem::CreateJobList(const char*,size_t){auto list=std::make_unique<idJobList>();lists.insert(list.get());return list;}
void JobSystem::RunAll(){for(auto* list:lists)if(list->submitted&&!list->done)list->Run();}
struct Renderer {
    idImage image;bool uploadOk=true,regionOk=true;int uploads=0,creates=0,regions=0;std::string failImage;
    std::unordered_map<std::string,std::vector<byte>> uploaded;
    std::set<std::string> shaped;
    std::vector<std::array<int,4>> regionRects;
    idImage* CreateImage(const char* name,const idImageOpts* options,int){
        assert(options->width==TEST_ATLAS_SIZE*TEST_ATLAS_GRID&&options->height==options->width);++creates;
        // Fresh storage takes rectangles at once; its contents are undefined on
        // a GPU (zero here) and never sampled outside an uploaded tile.
        shaped.insert(name);uploaded[name].assign(size_t(options->width)*options->height*4,byte(0));return &image;
    }
    bool UploadImage(const char* name,const byte* pixels,int w,int h){
        assert(w==TEST_ATLAS_SIZE*TEST_ATLAS_GRID&&h==w);++uploads;
        if(!uploadOk||failImage==name)return false;uploaded[name].assign(pixels,pixels+w*h*4);shaped.insert(name);return true;
    }
    // As the renderer: only storage CreateImage allocated or a whole upload shaped takes a rectangle.
    bool UploadImageRegion(const char* name,const byte* data,int x,int y,int w,int h,int pitch){
        constexpr int size=TEST_ATLAS_SIZE*TEST_ATLAS_GRID;
        if(!uploadOk||!regionOk||failImage==name||!shaped.count(name))return false;
        assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=size&&y+h<=size&&pitch>=w);
        auto& image=uploaded.at(name);
        for(int row=0;row<h;++row)std::memcpy(image.data()+((size_t(y)+row)*size+x)*4,data+size_t(row)*pitch*4,size_t(w)*4);
        ++regions;regionRects.push_back({x,y,w,h});return true;
    }
''')
    support += 'namespace openq4::ui {\n' + function_body(raster, 'struct CoverageBitmap {') + ';\n'
    support += function_body(raster, 'inline bool RasterizeCoverage(') + '\n}\n'
    support += r'''
struct {bool enabled=true;bool GetBool()const{return enabled;}} ui_retainedAtlas;
// A recording render demo turns the atlas off; these cases do not record.
static bool RetainedAtlasActive(){return ui_retainedAtlas.GetBool();}
struct Host {
    int viewportWidth=2*(TEST_ATLAS_SIZE-2)+260,viewportHeight=720;
    bool rasterNewMeshes=true;
    std::uint64_t frame=1;bool warming=false;
    std::uint64_t RenderFrame()const{return frame;}
    unsigned long long rasterDraws=0,rasterSavedVertices=0;
    static constexpr int AtlasSize=TEST_ATLAS_SIZE,AtlasPages=TEST_ATLAS_PAGES;
    static constexpr int AtlasGrid=TEST_ATLAS_GRID,AtlasGroup=AtlasGrid*AtlasGrid,AtlasImageSize=AtlasGrid*AtlasSize;
'''
    support += member_line(source, 'static constexpr std::uint64_t RetireFrames=')
    support += function_body(source, 'struct HostMesh {') + ';\n'
    support += 'std::vector<std::vector<byte>> atlasImages;\n' + function_body(source, 'byte* PagePixel(') + '\n'
    support += function_body(source, 'struct AtlasPage {') + ';\n'
    support += 'std::vector<AtlasPage> atlasPages; HostMesh scratchMesh; std::unordered_map<std::uint64_t,HostMesh> meshes;\n'
    for declaration in ('struct RasterRect {', 'struct RasterJob {', 'struct RasterBatch {', 'enum class Placement {'):
        support += function_body(source, declaration) + ';\n'
    for member in ('std::vector<std::unique_ptr<RasterJob>> queuedRasters, finishedRasters;',
                   'std::vector<std::unique_ptr<RasterBatch>> rasterBatches;',
                   'std::uint64_t rasterTickets=0,lastAtlasUpload=0;', 'unsigned long long retiredMeshes=0;', 'bool atlasBusy=false;'):
        support += member_line(source, member)
    support += 'bool PageHas(size_t page,byte value){for(int y=0;y<AtlasSize;++y)for(int x=0;x<AtlasSize;++x)if(PagePixel(page,x,y)[0]==value)return true;return false;}\n'
    for signature in ('void Draw(', 'void DrawMesh(', 'bool CanDrawMeshOpacity(', 'bool DrawMeshOpacity(', 'void DrawRaster(', 'void ReleaseMesh(', 'void DropRaster(',
                      'static bool Recyclable(', 'static bool RasterTiles(', 'static void RasterJobMain(', 'void QueueRaster(', 'void SubmitRasters(',
                      'void PlaceRasters(', 'bool RetireIdle(', 'Placement PlaceRaster(', 'void UploadAtlas(', 'static void Uploaded(', 'void EndRoot(',
                      'const idMaterial* Material(', 'void Convert(', 'void Submit(const HostMesh&'):
        support += function_body(source, signature).replace(' override', '') + '\n'
    support += r'''
    // One root's frame, as DrawRoot runs it.
    void Frame(std::uint64_t id,std::uint64_t revision,const std::vector<openq4::ui::Vertex>& vertices,const std::vector<int>& indices,std::uintptr_t handle=0){
        ++frame;DrawMesh(id,revision,vertices,indices,handle);EndRoot();
    }
    void Idle(){++frame;EndRoot();}
};
// A solid 510-pixel square of one-pixel columns: one full page's tile.
void Square(std::vector<openq4::ui::Vertex>& vertices,std::vector<int>& indices,float value){
    vertices.clear();indices.clear();
    for(int i=0;i<TEST_ATLAS_SIZE-2;++i){
        const int base=int(vertices.size());const float x=float(i);
        for(auto p:std::array<std::array<float,2>,4>{{{x,0},{x+1,0},{x+1,float(TEST_ATLAS_SIZE-2)},{x,float(TEST_ATLAS_SIZE-2)}}})
            vertices.push_back({p[0],p[1],0,0,value,0,0,1});
        indices.insert(indices.end(),{base,base+1,base+2,base,base+2,base+3});
    }
}
int main(){
    constexpr int extent=Host::AtlasSize-2, width=2*extent+260;
    static_assert(Host::AtlasSize*Host::AtlasSize*Host::AtlasPages*4==32*1024*1024,"unchanged pixel storage budget");
    std::vector<openq4::ui::Vertex> vertices;std::vector<int> indices;
    for(int i=0;i<width;++i){
        const int base=int(vertices.size());const float x=float(i);
        for(auto p:std::array<std::array<float,2>,4>{{{x,0},{x+1,0},{x+1,2},{x,2}}})
            vertices.push_back({p[0],p[1],0,0,.5f,0,0,.5f});
        indices.insert(indices.end(),{base,base+1,base+2,base,base+2,base+3});
    }
    const auto wideVertices=vertices;const auto wideIndices=indices;
    // Without workers a mesh rasterizes and places in its third frame, as before them.
    jobSystem.workers=false;
    Host host;
    // Startup's short-lived screen keeps its meshes out of the atlas.
    host.rasterNewMeshes=false;
    for(int i=0;i<4;++i){++host.frame;host.DrawMesh(9,1,vertices,indices,0);host.EndRoot();}
    assert(host.meshes.at(9).rasterTiles.empty() && !host.meshes.at(9).rasterTried && host.atlasPages.empty());
    host.ReleaseMesh(9);host.rasterNewMeshes=true;
    renderer.uploadOk=false;
    for(int i=0;i<4;++i){++host.frame;renderer.calls=0;host.DrawMesh(1,1,vertices,indices,0);host.EndRoot();}
    assert(host.meshes.at(1).rasterTiles.size()==3 && host.atlasPages.size()==1);
    assert(renderer.calls==1 && renderer.drawn.size()==vertices.size()); // Entire mesh falls back while any upload fails.
    assert(common->warnings==1 && host.atlasPages[0].references==3);
    renderer.calls=0;assert(!host.DrawMeshOpacity(1,1,.5f) && renderer.calls==0);
    renderer.uploadOk=true;host.EndRoot();++host.frame;renderer.calls=0;
    host.DrawMesh(1,1,vertices,indices,0);
    assert(renderer.calls==3 && renderer.drawn.size()==4 && host.rasterSavedVertices==vertices.size()-12);
    const auto& tiles=host.meshes.at(1).rasterTiles;
    assert(tiles[0].quad[1].xyz.x==tiles[1].quad[0].xyz.x && tiles[1].quad[1].xyz.x==tiles[2].quad[0].xyz.x);
    assert(tiles[0].quad[0].xyz.x==0 && tiles[2].quad[1].xyz.x==640);
    for(const auto& tile:tiles){
        const int x=int(tile.quad[0].st.x*Host::AtlasImageSize)%Host::AtlasSize,y=int(tile.quad[0].st.y*Host::AtlasImageSize)%Host::AtlasSize;
        assert(host.PagePixel(tile.page,x,y)[0]==128 && host.PagePixel(tile.page,x,y)[3]==128);
        assert(host.PagePixel(tile.page,x-1,y)[0]==0); // Untouched tile padding.
    }
    renderer.calls=0;assert(host.DrawMeshOpacity(1,1,.5f) && renderer.calls==3);
    for(const auto& v:renderer.drawn)for(int c=0;c<4;++c)assert(v.color[c]==128 && v.color2[c]==128);
    for(const auto& tile:host.meshes.at(1).rasterTiles)assert(tile.quad[0].color[0]==255); // Live alpha never accumulates in the cached quad.
    renderer.calls=0;assert(!host.DrawMeshOpacity(1,2,.5f) && renderer.calls==0);
    ++host.viewportWidth;assert(!host.DrawMeshOpacity(1,1,.5f));--host.viewportWidth;
    ui_retainedAtlas.enabled=false;assert(!host.DrawMeshOpacity(1,1,.5f));ui_retainedAtlas.enabled=true;
    ++host.frame;
    renderer.calls=0;host.DrawMesh(1,2,vertices,indices,0);
    assert(host.meshes.at(1).rasterTiles.empty() && host.atlasPages[0].references==0 && renderer.calls==1);
    for(int i=0;i<3;++i){++host.frame;host.DrawMesh(1,2,vertices,indices,0);host.EndRoot();}
    assert(!host.meshes.at(1).rasterTiles.empty());host.ReleaseMesh(1);
    assert(host.meshes.empty() && host.atlasPages[0].references==0);

    Host sparse;vertices.clear();indices.clear();
    for(int i=0;i<64;++i){
        const int base=int(vertices.size());const float x=float(i<32?i*4:2*extent+20+(i-32)*4);
        for(auto p:std::array<std::array<float,2>,4>{{{x,0},{x+4,0},{x+4,2},{x,2}}})
            vertices.push_back({p[0],p[1],0,0,.5f,0,0,0}); // Additive RGB remains visible even with zero coverage alpha.
        indices.insert(indices.end(),{base,base+1,base+2,base,base+2,base+3});
    }
    const auto sparseVertices=vertices;const auto sparseIndices=indices;
    for(int i=0;i<3;++i){++sparse.frame;sparse.DrawMesh(3,1,vertices,indices,0);sparse.EndRoot();}
    assert(sparse.meshes.at(3).rasterTiles.size()==2 && sparse.atlasPages[0].references==2); // Empty middle tile is omitted.
    const auto& sparseTiles=sparse.meshes.at(3).rasterTiles;
    assert(sparseTiles[0].quad[0].xyz.x==0 && std::abs(sparseTiles[0].quad[1].xyz.x-128*640.f/width)<.001f);
    assert(std::abs(sparseTiles[1].quad[0].xyz.x-(2*extent+20)*640.f/width)<.001f &&
        std::abs(sparseTiles[1].quad[1].xyz.x-(2*extent+148)*640.f/width)<.001f);
    for(const auto& tile:sparse.meshes.at(3).rasterTiles)assert(sparse.PageHas(tile.page,128));
    sparse.ReleaseMesh(3);assert(sparse.atlasPages[0].references==0);

    Host multiply;idMaterial factor{"_retainedMultiply"};
    vertices=wideVertices;indices=wideIndices;
    // Two separated bands leave a hole inside a tile. Multiplication must
    // preserve the destination throughout that hole and the tile padding.
    for(size_t i=0;i<vertices.size();++i){
        auto& v=vertices[i];v.r=v.g=v.b=.5f;v.a=1;
        if(i>=vertices.size()/2)v.y+=4;
    }
    for(int i=0;i<4;++i){++multiply.frame;multiply.DrawMesh(4,1,vertices,indices,reinterpret_cast<std::uintptr_t>(&factor));multiply.EndRoot();}
    const auto& factors=multiply.meshes.at(4);
    assert(factors.rasterTiles.size()==3 && renderer.material==multiply.atlasPages[factors.rasterTiles.back().page].multiplyMaterial);
    assert(renderer.material->name.find("_retainedMultiplyAtlas/")==0);
    const auto& factorTile=factors.rasterTiles[1];
    const int fx=int(factorTile.quad[0].st.x*Host::AtlasImageSize)%Host::AtlasSize,fy=int(factorTile.quad[0].st.y*Host::AtlasImageSize)%Host::AtlasSize;
    for(int ch=0;ch<4;++ch){
        assert(multiply.PagePixel(factorTile.page,fx,fy+2)[ch]==255); // Gap between the bands.
        assert(multiply.PagePixel(factorTile.page,fx-1,fy)[ch]==255); // Padding.
    }
    // Written once, the padding ring is whole: every side and corner.
    const int fw=int(factorTile.quad[1].st.x*Host::AtlasImageSize)-int(factorTile.quad[0].st.x*Host::AtlasImageSize);
    const int fh=int(factorTile.quad[2].st.y*Host::AtlasImageSize)-int(factorTile.quad[0].st.y*Host::AtlasImageSize);
    for(const auto& p:std::vector<std::array<int,2>>{{fx-1,fy-1},{fx+fw,fy-1},{fx-1,fy+fh},{fx+fw,fy+fh},{fx+fw,fy},{fx,fy+fh},{fx,fy-1}})
        for(int ch=0;ch<4;++ch)assert(multiply.PagePixel(factorTile.page,p[0],p[1])[ch]==255);
    assert(!multiply.DrawMeshOpacity(4,1,.5f)); // Group opacity is not a multiply factor.
    for(auto& v:vertices)v.r=v.g=v.b=v.a=1;
    for(int i=0;i<3;++i){++multiply.frame;multiply.DrawMesh(4,2,vertices,indices,reinterpret_cast<std::uintptr_t>(&factor));multiply.EndRoot();}
    assert(multiply.meshes.at(4).rasterTiles.empty()); // White RGBA is empty.
    for(auto& v:vertices)v.a=.5f;
    for(int i=0;i<3;++i){++multiply.frame;multiply.DrawMesh(4,3,vertices,indices,reinterpret_cast<std::uintptr_t>(&factor));multiply.EndRoot();}
    assert(multiply.meshes.at(4).rasterTiles.size()==3); // White RGB alone can still multiply destination alpha.
    multiply.ReleaseMesh(4);for(const auto& page:multiply.atlasPages)assert(page.references==0);

    vertices=wideVertices;indices=wideIndices;
    Host full;full.atlasPages.resize(Host::AtlasPages);
    full.atlasImages.assign(Host::AtlasPages/Host::AtlasGroup,std::vector<byte>(size_t(Host::AtlasImageSize)*Host::AtlasImageSize*4));
    for(auto& page:full.atlasPages){page.x=Host::AtlasSize;page.y=Host::AtlasSize;page.references=1;}
    // Only the first tile fits. Planned before any pixel or lease, a mesh
    // that does not fit takes nothing and leaves the allocations as they were.
    full.atlasPages[0].x=0;full.atlasPages[0].y=Host::AtlasSize-4;
    for(int i=0;i<3;++i){++full.frame;renderer.calls=0;full.DrawMesh(2,1,vertices,indices,0);}
    assert(full.meshes.at(2).rasterTiles.empty() && renderer.calls==1 && renderer.drawn.size()==vertices.size());
    assert(full.atlasPages.size()==Host::AtlasPages && full.finishedRasters.size()==1); // It waits for room.
    for(const auto& page:full.atlasPages)assert(page.references==1 && !page.dirty);
    assert(full.atlasPages[0].x==0 && full.atlasPages[0].y==Host::AtlasSize-4);

    // Independent pages share physical images without sharing allocation or
    // retirement. Fill each page so this reaches every row and the next image.
    Host grouped;openq4::ui::CoverageBitmap bitmap;bitmap.width=bitmap.height=extent;
    bitmap.pixels.resize(extent*extent*4);
    const int createsBefore=renderer.creates;
    const std::vector<Host::RasterRect> page{{0,0,extent,extent}};
    for(int index=0;index<=Host::AtlasGroup;++index){
        for(size_t i=0;i<bitmap.pixels.size();i+=4){bitmap.pixels[i]=byte(10+index);bitmap.pixels[i+3]=255;}
        auto& mesh=grouped.meshes[100+index];mesh.width=grouped.viewportWidth;mesh.height=grouped.viewportHeight;
        assert(grouped.PlaceRaster(mesh,bitmap,page)==Host::Placement::Placed);
        const auto& tile=grouped.meshes[100+index].rasterTiles.at(0);
        assert(tile.page==index);
        assert(int(tile.quad[0].st.x*Host::AtlasImageSize)==(index%Host::AtlasGrid)*Host::AtlasSize+1);
        assert(int(tile.quad[0].st.y*Host::AtlasImageSize)==((index%Host::AtlasGroup)/Host::AtlasGrid)*Host::AtlasSize+1);
        if(index%Host::AtlasGroup)assert(grouped.atlasPages[index].material==grouped.atlasPages[index-1].material);
    }
    assert(renderer.creates-createsBefore==2);
    // Each page goes as one rectangle, straight into the storage CreateImage
    // allocated: no whole upload. A refused upload leaves its pages dirty.
    const int regionsBefore=renderer.regions,groupWholeBefore=renderer.uploads;
    renderer.failImage="_retainedAtlasImage0";grouped.EndRoot();
    for(int i=0;i<Host::AtlasGroup;++i)assert(grouped.atlasPages[i].dirty);
    assert(!grouped.atlasPages[Host::AtlasGroup].dirty && renderer.regions==regionsBefore+1);
    const auto secondImage=renderer.uploaded.at("_retainedAtlasImage1");
    renderer.failImage.clear();grouped.EndRoot();
    for(const auto& page:grouped.atlasPages)assert(!page.dirty && page.dirtyRight==0);
    assert(secondImage==renderer.uploaded.at("_retainedAtlasImage1") && renderer.regions==regionsBefore+1+Host::AtlasGroup);
    assert(renderer.uploads==groupWholeBefore+1); // Only image 0's refused attempt was whole.
    auto uploadedPixel=[&](int page,int x,int y)->byte{
        const auto& pixels=renderer.uploaded.at("_retainedAtlasImage"+std::to_string(page/Host::AtlasGroup));
        const int px=(page%Host::AtlasGrid)*Host::AtlasSize+x,py=((page%Host::AtlasGroup)/Host::AtlasGrid)*Host::AtlasSize+y;
        return pixels[(py*Host::AtlasImageSize+px)*4];
    };
    for(int page=0;page<=Host::AtlasGroup;++page){
        assert(uploadedPixel(page,1,1)==10+page && uploadedPixel(page,extent,extent)==10+page);
        assert(uploadedPixel(page,0,0)==0 && uploadedPixel(page,Host::AtlasSize-1,Host::AtlasSize-1)==0);
    }
    grouped.ReleaseMesh(100);grouped.frame+=5;
    for(size_t i=0;i<bitmap.pixels.size();i+=4)bitmap.pixels[i]=99;
    {auto& mesh=grouped.meshes[200];mesh.width=grouped.viewportWidth;mesh.height=grouped.viewportHeight;}
    assert(grouped.PlaceRaster(grouped.meshes[200],bitmap,page)==Host::Placement::Placed);
    assert(grouped.meshes[200].rasterTiles[0].page==0);
    const int uploadsBefore=renderer.uploads;grouped.EndRoot();
    // The recycled page alone goes, as one rectangle; its neighbours stay.
    assert(renderer.uploads==uploadsBefore && renderer.regions==regionsBefore+2+Host::AtlasGroup);
    assert((renderer.regionRects.back()==std::array<int,4>{0,0,Host::AtlasSize,Host::AtlasSize}));
    assert(uploadedPixel(0,1,1)==99);
    for(int page=1;page<=Host::AtlasGroup;++page)assert(uploadedPixel(page,1,1)==10+page);
    assert(renderer.uploaded.at("_retainedAtlasImage0")==grouped.atlasImages[0]);
    // A small tile sends only its own padded rectangle.
    openq4::ui::CoverageBitmap small;small.width=small.height=8;small.pixels.assign(8*8*4,0);
    for(size_t i=0;i<small.pixels.size();i+=4){small.pixels[i]=77;small.pixels[i+3]=255;}
    {auto& mesh=grouped.meshes[201];mesh.width=grouped.viewportWidth;mesh.height=grouped.viewportHeight;}
    assert(grouped.PlaceRaster(grouped.meshes[201],small,{{0,0,8,8}})==Host::Placement::Placed);
    const int smallPage=grouped.meshes[201].rasterTiles[0].page;
    assert(smallPage==Host::AtlasGroup+1 && grouped.atlasPages[smallPage].dirtyRight==10 && grouped.atlasPages[smallPage].dirtyBottom==10);
    grouped.EndRoot();
    const int sx=(smallPage%Host::AtlasGrid)*Host::AtlasSize,sy=((smallPage%Host::AtlasGroup)/Host::AtlasGrid)*Host::AtlasSize;
    assert((renderer.regionRects.back()==std::array<int,4>{sx,sy,10,10}) && uploadedPixel(smallPage,1,1)==77);
    assert(renderer.uploaded.at("_retainedAtlasImage1")==grouped.atlasImages[1]);

    // A frame drawn out of sight is not a resting frame: a mesh first drawn
    // there rasterizes on its third presented frame, and never in one.
    Host warm;vertices=wideVertices;indices=wideIndices;
    warm.warming=true;
    for(int i=0;i<3;++i){++warm.frame;warm.DrawMesh(5,1,vertices,indices,0);warm.EndRoot();assert(warm.meshes.at(5).rasterTiles.empty());}
    warm.warming=false;
    for(int i=0;i<2;++i){++warm.frame;warm.DrawMesh(5,1,vertices,indices,0);warm.EndRoot();assert(warm.meshes.at(5).rasterTiles.empty());}
    ++warm.frame;warm.DrawMesh(5,1,vertices,indices,0);warm.EndRoot();assert(warm.meshes.at(5).rasterTiles.size()==3);

    // With workers the third frame queues a copy and EndRoot submits the
    // frame's queue as one list; the mesh draws as geometry until a later
    // frame places and uploads its tiles.
    jobSystem.workers=true;
    Host async;vertices=wideVertices;indices=wideIndices;
    for(int i=0;i<2;++i){renderer.calls=0;async.Frame(6,1,vertices,indices);assert(renderer.calls==1 && async.queuedRasters.empty());}
    ++async.frame;renderer.calls=0;async.DrawMesh(6,1,vertices,indices,0);
    assert(async.queuedRasters.size()==1 && async.meshes.at(6).rasterTicket && async.meshes.at(6).rasterTiles.empty() && renderer.calls==1);
    assert(async.queuedRasters[0]->vertices.size()==vertices.size()); // A copy: the runtime may change its own.
    const int submittedBefore=jobSystem.submitted;async.EndRoot();
    assert(async.queuedRasters.empty() && async.rasterBatches.size()==1 && jobSystem.submitted==submittedBefore+1);
    for(int i=0;i<2;++i){renderer.calls=0;async.Frame(6,1,vertices,indices);assert(renderer.calls==1 && async.meshes.at(6).rasterTiles.empty());}
    jobSystem.RunAll();
    renderer.calls=0;async.Frame(6,1,vertices,indices); // Drawn as geometry, then placed and uploaded.
    assert(renderer.calls==1 && async.meshes.at(6).rasterTiles.size()==3 && !async.atlasPages[0].dirty && async.rasterBatches.empty());
    assert(async.lastAtlasUpload==async.frame && async.meshes.at(6).rasterTicket==0);
    renderer.calls=0;async.Frame(6,1,vertices,indices);assert(renderer.calls==3 && renderer.drawn.size()==4);
    // A result for a mesh that changed or went meanwhile takes nothing.
    vertices=sparseVertices;indices=sparseIndices;
    for(int i=0;i<3;++i)async.Frame(7,1,vertices,indices);
    for(int i=0;i<3;++i)async.Frame(8,1,vertices,indices);
    assert(async.rasterBatches.size()==2);
    async.Frame(7,2,vertices,indices);async.ReleaseMesh(8);
    jobSystem.RunAll();async.Idle();async.Idle();
    assert(async.finishedRasters.empty() && async.meshes.at(7).rasterTiles.empty() && async.meshes.at(7).rasterTicket==0);
    for(int i=0;i<2;++i)async.Frame(7,2,vertices,indices);
    jobSystem.RunAll();async.Frame(7,2,vertices,indices);assert(async.meshes.at(7).rasterTiles.size()==2);
    // Results that finish in the frame after an upload wait one frame more:
    // uploads in successive frames stall Vulkan.
    for(int i=0;i<2;++i)async.Frame(9,1,vertices,indices);
    ++async.frame;async.DrawMesh(9,1,vertices,indices,0);async.DrawMesh(10,1,wideVertices,wideIndices,0);async.EndRoot();
    jobSystem.RunAll();
    async.Frame(10,1,wideVertices,wideIndices);async.Frame(10,1,wideVertices,wideIndices); // Mesh 10 queues on its third frame.
    const auto uploadFrame=async.frame-1;
    assert(async.lastAtlasUpload==uploadFrame && async.meshes.at(9).rasterTiles.size()==2);
    jobSystem.RunAll();
    async.Frame(10,1,wideVertices,wideIndices);
    assert(async.frame==uploadFrame+2 && async.meshes.at(10).rasterTiles.size()==3 && async.lastAtlasUpload==async.frame);
    // Rectangles only.
    assert(renderer.regionRects.size()>=3);
    renderer.regionOk=false;async.ReleaseMesh(10);async.frame+=5;
    for(int i=0;i<3;++i)async.Frame(11,1,wideVertices,wideIndices);
    jobSystem.RunAll();const int wholeBefore=renderer.uploads;async.Frame(11,1,wideVertices,wideIndices);
    assert(renderer.uploads==wholeBefore+1 && !async.atlasPages[0].dirty); // A refused rectangle uploads the image whole.
    assert(renderer.uploaded.at("_retainedAtlasImage0")==async.atlasImages[0]);
    renderer.regionOk=true;
    // A refused submission rasterizes here; the next frame places it.
    jobSystem.refuse=true;
    for(int i=0;i<3;++i)async.Frame(12,1,sparseVertices,sparseIndices);
    assert(async.rasterBatches.empty() && async.finishedRasters.size()==1);
    async.Idle();async.Idle();assert(async.meshes.at(12).rasterTiles.size()==2);
    jobSystem.refuse=false;

    // A full atlas takes back the tiles of meshes not drawn for a while, so
    // the page in front can use it; meshes still drawn keep theirs.
    Host retire;Square(vertices,indices,.5f);
    for(int mesh=0;mesh<Host::AtlasPages;++mesh){for(int i=0;i<3;++i)retire.Frame(1000+mesh,1,vertices,indices);jobSystem.RunAll();retire.Idle();}
    for(int mesh=0;mesh<Host::AtlasPages;++mesh)assert(retire.meshes.at(1000+mesh).rasterTiles.size()==1);
    for(const auto& page:retire.atlasPages)assert(page.references==1);
    ++retire.frame;for(int mesh=0;mesh<Host::AtlasPages;++mesh)retire.DrawMesh(1000+mesh,1,vertices,indices,0);retire.EndRoot();
    const auto shown=retire.frame;
    for(int i=0;i<3;++i){retire.Frame(2000,1,vertices,indices);retire.DrawMesh(1031,1,vertices,indices,0);}
    jobSystem.RunAll();retire.Frame(2000,1,vertices,indices);retire.DrawMesh(1031,1,vertices,indices,0);
    assert(retire.meshes.at(2000).rasterTiles.empty() && retire.finishedRasters.size()==1 && retire.retiredMeshes==0);
    while(retire.frame<shown+Host::RetireFrames){retire.Frame(2000,1,vertices,indices);retire.DrawMesh(1031,1,vertices,indices,0);}
    assert(retire.meshes.at(2000).rasterTiles.empty() && retire.retiredMeshes==0);
    retire.Frame(2000,1,vertices,indices);retire.DrawMesh(1031,1,vertices,indices,0);
    assert(retire.retiredMeshes==Host::AtlasPages-1 && retire.meshes.at(1031).rasterTiles.size()==1);
    assert(retire.meshes.at(1000).rasterTiles.empty() && !retire.meshes.at(1000).rasterTried && retire.atlasPages[0].references==0);
    // The pages recycle four frames after their last tile went.
    for(int i=0;i<4;++i){retire.Frame(2000,1,vertices,indices);assert(retire.meshes.at(2000).rasterTiles.empty());}
    retire.Frame(2000,1,vertices,indices);
    assert(retire.meshes.at(2000).rasterTiles.size()==1 && retire.meshes.at(2000).rasterTiles[0].page==0 && retire.finishedRasters.empty());
    // A retired mesh drawn again rasterizes again once it rests.
    for(int i=0;i<3;++i)retire.Frame(1000,1,vertices,indices);
    jobSystem.RunAll();retire.Idle();retire.Idle();
    assert(retire.meshes.at(1000).rasterTiles.size()==1 && retire.meshes.at(1000).rasterTiles[0].page==1);
    // A result waiting for room whose mesh stops being drawn takes nothing,
    // and rasterizes again when it is drawn and rests.
    Host leave;
    for(int mesh=0;mesh<Host::AtlasPages;++mesh){for(int i=0;i<3;++i)leave.Frame(3000+mesh,1,vertices,indices);jobSystem.RunAll();leave.Idle();}
    for(int i=0;i<3;++i){leave.Frame(4000,1,vertices,indices);for(int mesh=0;mesh<Host::AtlasPages;++mesh)leave.DrawMesh(3000+mesh,1,vertices,indices,0);}
    jobSystem.RunAll();
    for(std::uint64_t i=0;i<=Host::RetireFrames+1;++i){++leave.frame;for(int mesh=0;mesh<Host::AtlasPages;++mesh)leave.DrawMesh(3000+mesh,1,vertices,indices,0);leave.EndRoot();}
    assert(leave.finishedRasters.empty() && leave.meshes.at(4000).rasterTicket==0 && !leave.meshes.at(4000).rasterTried && leave.retiredMeshes==0);
    // A root that a print inside EndRoot redraws draws and ends as usual, but
    // its EndRoot leaves the atlas to the busy one: no list, plan or page
    // changes underneath it.
    Host nested;int nestedRoots=0;
    for(int i=0;i<2;++i)nested.Frame(20,1,wideVertices,wideIndices);
    nested.Frame(20,1,wideVertices,wideIndices);jobSystem.RunAll();
    reentry=[&]{
        reentry=nullptr;++nestedRoots;
        for(int i=0;i<3;++i)nested.DrawMesh(21,1,sparseVertices,sparseIndices,0);
        const auto batches=nested.rasterBatches.size();nested.EndRoot();
        assert(nested.rasterBatches.size()==batches && nested.atlasBusy);
    };
    nested.Frame(20,1,wideVertices,wideIndices); // Placing mesh 20 creates its page: FindMaterial prints.
    assert(nestedRoots==1 && !reentry && !nested.atlasBusy);
    assert(nested.meshes.at(20).rasterTiles.size()==3 && !nested.atlasPages[0].dirty);
    // Without workers a mesh resting inside a busy EndRoot rasterizes and waits.
    jobSystem.workers=false;
    for(int i=0;i<2;++i)nested.Frame(22,1,sparseVertices,sparseIndices);
    ++nested.frame;nested.atlasBusy=true;nested.DrawMesh(22,1,sparseVertices,sparseIndices,0);nested.EndRoot();
    assert(nested.meshes.at(22).rasterTiles.empty() && nested.finishedRasters.size()==1 && nested.atlasBusy);
    nested.atlasBusy=false;nested.Idle();nested.Idle();
    assert(nested.meshes.at(22).rasterTiles.size()==2 && nested.finishedRasters.empty());
    jobSystem.workers=true;
    std::puts("retained atlas: exact tiled coordinates, padding, upload retry, whole-mesh fallback, revision retirement, all-or-nothing placement, in-image pages, rectangle uploads, out-of-sight frames, worker rasters, stale results, upload spacing, idle retirement and roots redrawn by a print passed");
}
'''
    atlas_size, atlas_pages = re.search(r'static constexpr int AtlasSize=(\d+), AtlasPages=(\d+);', source).groups()
    atlas_grid = re.search(r'static constexpr int AtlasGrid=(\d+),', source).group(1)
    support = support.replace('TEST_ATLAS_SIZE', atlas_size).replace('TEST_ATLAS_PAGES', atlas_pages).replace('TEST_ATLAS_GRID', atlas_grid)
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-atlas-', dir=ROOT / '.tmp') as folder:
        cpp, binary = Path(folder) / 'atlas.cpp', Path(folder) / 'atlas.exe'
        cpp.write_text(support, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
