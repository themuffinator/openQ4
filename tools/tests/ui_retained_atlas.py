#!/usr/bin/env python3
"""Exercise production coverage atlas tiling, uploads and bounded fallback.

Uses the indexed-draw fixture's renderer double. Production rasterization,
packing, submission and retirement methods are compiled without rewriting.
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


def main():
    source = (ROOT / 'src/ui/RetainedUI.cpp').read_text(encoding='utf-8')
    raster = (ROOT / 'src/ui/retained/CoverageRaster.h').read_text(encoding='utf-8')
    support = '#include <cmath>\n' + DRAW_SUPPORT.split('// This fixture qualifies indexed mesh submission')[0]
    support = support.replace('const char* GetName() const', 'int GetState()const{return 0;} const char* GetName() const')
    support = support.replace('idMaterial solid{"_retainedSolid"}; const idMaterial* FindMaterial(const char*){return &solid;}',
                              'std::unordered_map<std::string,idMaterial> materials; const idMaterial* FindMaterial(const char* name){return &materials.try_emplace(name,idMaterial{name}).first->second;}')
    support = support.replace('struct Renderer {', r'''
enum {FMT_RGBA8, TF_NEAREST, DS_DEFAULTED=9};
struct idImageOpts {int width=0,height=0,format=0,numLevels=0;bool isPersistant=false;};
struct idImage {};
const char* va(const char* format,unsigned index){static char text[128];std::snprintf(text,sizeof(text),format,index);return text;}
struct Common {int warnings=0;void Warning(const char*,...){++warnings;}} commonObject;
Common* common=&commonObject;
struct Renderer {
    idImage image;bool uploadOk=true;int uploads=0,creates=0;std::string failImage;
    std::unordered_map<std::string,std::vector<byte>> uploaded;
    idImage* CreateImage(const char*,const idImageOpts* options,int){assert(options->width==TEST_ATLAS_SIZE*TEST_ATLAS_GRID&&options->height==options->width);++creates;return &image;}
    bool UploadImage(const char* name,const byte* pixels,int w,int h){
        assert(w==TEST_ATLAS_SIZE*TEST_ATLAS_GRID&&h==w);++uploads;
        if(!uploadOk||failImage==name)return false;uploaded[name].assign(pixels,pixels+w*h*4);return true;
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
    std::uint64_t frame=1;
    std::uint64_t RenderFrame()const{return frame;}
    unsigned long long rasterDraws=0,rasterSavedVertices=0;
    static constexpr int AtlasSize=TEST_ATLAS_SIZE,AtlasPages=TEST_ATLAS_PAGES;
    static constexpr int AtlasGrid=TEST_ATLAS_GRID,AtlasGroup=AtlasGrid*AtlasGrid,AtlasImageSize=AtlasGrid*AtlasSize;
'''
    support += function_body(source, 'struct HostMesh {') + ';\n'
    support += function_body(source, 'struct AtlasPage {') + ';\n'
    support += 'std::vector<AtlasPage> atlasPages; HostMesh scratchMesh; std::unordered_map<std::uint64_t,HostMesh> meshes;\n'
    for signature in ('void Draw(', 'void DrawMesh(', 'bool CanDrawMeshOpacity(', 'bool DrawMeshOpacity(', 'void DrawRaster(', 'void ReleaseMesh(', 'void DropRaster(',
                      'void CacheRaster(', 'bool CacheRasterTile(', 'void EndRoot(',
                      'const idMaterial* Material(', 'void Convert(', 'void Submit(const HostMesh&'):
        support += function_body(source, signature).replace(' override', '') + '\n'
    support += r'''
};
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
        const auto& page=host.atlasPages[tile.page];
        const int x=int(tile.quad[0].st.x*Host::AtlasImageSize)%Host::AtlasSize,y=int(tile.quad[0].st.y*Host::AtlasImageSize)%Host::AtlasSize;
        assert(page.pixels[(y*Host::AtlasSize+x)*4]==128 && page.pixels[(y*Host::AtlasSize+x)*4+3]==128);
        assert(page.pixels[(y*Host::AtlasSize+x-1)*4]==0); // Untouched tile padding.
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
    for(int i=0;i<3;++i){++sparse.frame;sparse.DrawMesh(3,1,vertices,indices,0);sparse.EndRoot();}
    assert(sparse.meshes.at(3).rasterTiles.size()==2 && sparse.atlasPages[0].references==2); // Empty middle tile is omitted.
    const auto& sparseTiles=sparse.meshes.at(3).rasterTiles;
    assert(sparseTiles[0].quad[0].xyz.x==0 && std::abs(sparseTiles[0].quad[1].xyz.x-128*640.f/width)<.001f);
    assert(std::abs(sparseTiles[1].quad[0].xyz.x-(2*extent+20)*640.f/width)<.001f &&
        std::abs(sparseTiles[1].quad[1].xyz.x-(2*extent+148)*640.f/width)<.001f);
    for(const auto& tile:sparse.meshes.at(3).rasterTiles){
        const auto& page=sparse.atlasPages[tile.page];
        assert(std::any_of(page.pixels.begin(),page.pixels.end(),[](byte value){return value==128;}));
    }
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
    const auto& factorTile=factors.rasterTiles[1];const auto& factorPage=multiply.atlasPages[factorTile.page];
    const int fx=int(factorTile.quad[0].st.x*Host::AtlasImageSize)%Host::AtlasSize,fy=int(factorTile.quad[0].st.y*Host::AtlasImageSize)%Host::AtlasSize;
    for(int ch=0;ch<4;++ch){
        assert(factorPage.pixels[((fy+2)*Host::AtlasSize+fx)*4+ch]==255); // Gap between the bands.
        assert(factorPage.pixels[(fy*Host::AtlasSize+fx-1)*4+ch]==255); // Padding.
    }
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
    for(auto& page:full.atlasPages){page.pixels.resize(Host::AtlasSize*Host::AtlasSize*4);page.x=Host::AtlasSize;page.y=Host::AtlasSize;page.references=1;}
    // Only the first tile fits. No partial result may escape and its lease
    // must be released without disturbing the pre-existing allocations.
    full.atlasPages[0].x=0;full.atlasPages[0].y=Host::AtlasSize-4;
    for(int i=0;i<3;++i){++full.frame;renderer.calls=0;full.DrawMesh(2,1,vertices,indices,0);}
    assert(full.meshes.at(2).rasterTiles.empty() && renderer.calls==1 && renderer.drawn.size()==vertices.size());
    assert(full.atlasPages.size()==Host::AtlasPages);
    for(const auto& page:full.atlasPages)assert(page.references==1);

    // Independent pages share physical images without sharing allocation or
    // retirement. Fill each page so this reaches every row and the next image.
    Host grouped;openq4::ui::CoverageBitmap bitmap;bitmap.width=bitmap.height=extent;
    bitmap.pixels.resize(extent*extent*4);
    const int createsBefore=renderer.creates;
    for(int page=0;page<=Host::AtlasGroup;++page){
        for(size_t i=0;i<bitmap.pixels.size();i+=4){bitmap.pixels[i]=byte(10+page);bitmap.pixels[i+3]=255;}
        assert(grouped.CacheRasterTile(grouped.meshes[100+page],bitmap,0,0,extent,extent));
        const auto& tile=grouped.meshes[100+page].rasterTiles.at(0);
        assert(tile.page==page);
        assert(int(tile.quad[0].st.x*Host::AtlasImageSize)==(page%Host::AtlasGrid)*Host::AtlasSize+1);
        assert(int(tile.quad[0].st.y*Host::AtlasImageSize)==((page%Host::AtlasGroup)/Host::AtlasGrid)*Host::AtlasSize+1);
        if(page%Host::AtlasGroup)assert(grouped.atlasPages[page].material==grouped.atlasPages[page-1].material);
    }
    assert(renderer.creates-createsBefore==2);
    renderer.failImage="_retainedAtlasImage0";grouped.EndRoot();
    for(int i=0;i<Host::AtlasGroup;++i)assert(grouped.atlasPages[i].dirty);
    assert(!grouped.atlasPages[Host::AtlasGroup].dirty);
    const auto secondImage=renderer.uploaded.at("_retainedAtlasImage1");
    renderer.failImage.clear();grouped.EndRoot();
    for(const auto& page:grouped.atlasPages)assert(!page.dirty);
    assert(secondImage==renderer.uploaded.at("_retainedAtlasImage1"));
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
    assert(grouped.CacheRasterTile(grouped.meshes[200],bitmap,0,0,extent,extent));
    assert(grouped.meshes[200].rasterTiles[0].page==0);grouped.EndRoot();
    assert(uploadedPixel(0,1,1)==99);
    for(int page=1;page<=Host::AtlasGroup;++page)assert(uploadedPixel(page,1,1)==10+page);
    std::puts("retained atlas: exact tiled coordinates, padding, upload retry, whole-mesh fallback, revision retirement and bounded partial allocation passed");
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
