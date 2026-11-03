#!/usr/bin/env python3
"""Compile production generated-image host and upload methods with a renderer double."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "src/ui/retained/GeneratedImagePool.h"
using byte=unsigned char;
using textureFilter_t=int; using textureRepeat_t=int;
enum {FMT_RGBA8=1,TT_2D=2,TF_LINEAR=3,DS_DEFAULTED=4};
struct idImageOpts {int width=0,height=0,textureType=TT_2D,format=0,numLevels=0;bool isPersistant=false;};
struct idStr:std::string {using std::string::string;};
const char* va(const char* format,unsigned value){static char text[128];std::snprintf(text,sizeof text,format,value);return text;}
static void R_RenderThread_Sync(const char*){} // no render thread here
struct idImage {
    idImageOpts options; bool scratch=true;
    int filter=TF_LINEAR,repeat=7,direct=0,legacy=0;
    std::vector<byte> pixels;
    const idImageOpts& GetOpts() const{return options;}
    bool IsScratchImage() const{return scratch;}
    int GetFilter() const{return filter;} int GetRepeat() const{return repeat;}
    void SetSamplerState(int f,int r){filter=f;repeat=r;}
    void UploadScratch(const byte*,int,int){++legacy;filter=99;repeat=99;}
    void SubImageUpload(int level,int x,int y,int z,int w,int h,const byte* data){
        assert(level==0 && x==0 && y==0 && z==0 && w==options.width && h==options.height);
        ++direct;pixels.assign(data,data+w*h*4);
    }
};
struct Images {
    bool missing=false;
    std::unordered_map<std::string,idImage> values;
    idImage* GetImage(const char* name){auto i=values.find(name);return missing||i==values.end()?nullptr:&i->second;}
} images;
Images* globalImages=&images;
struct Renderer {
    struct Config {int maxTextureSize=4096;} config;
    bool failCreate=false;
    unsigned creates=0;
    const Config& GetGLConfig() const{return config;}
    idImage* CreateImage(const char* name,idImageOpts* options,int filter){
        ++creates;if(failCreate)return nullptr;
        assert(options->format==FMT_RGBA8 && options->numLevels==1 && options->isPersistant && filter==TF_LINEAR);
        auto& image=images.values[name];image.options=*options;image.filter=filter;image.scratch=true;
        image.pixels.clear();return &image;
    }
    bool UploadImage(const char*,const byte*,int,int);
} renderer;
Renderer* renderSystem=&renderer;
struct idMaterial {std::string name;int state=0;int GetState() const{return state;}};
struct Declarations {
    bool fail=false;
    std::unordered_map<std::string,idMaterial> values;
    const idMaterial* FindMaterial(const char* name){auto& m=values[name];m.name=name;m.state=fail?DS_DEFAULTED:0;return &m;}
} declarations;
Declarations* declManager=&declarations;
'''
HOST = r'''
struct Host {
    openq4::ui::GeneratedImagePool generatedImages;
    std::array<const idMaterial*,openq4::ui::GeneratedImagePool::Capacity> generatedMaterials{};
    std::uint64_t frame=1;
    std::uint64_t RenderFrame() const{return frame;}
'''
MAIN = r'''
};
int main(){
    Host host;
    const std::vector<byte> pixels={128,0,0,128,0,64,0,64,0,0,0,0,255,255,255,255,0,0,128,128,64,0,64,128};
    const auto first=host.GenerateTexture(pixels,1,6);
    assert(first && images.values.size()==1 && declarations.values.size()==1);
    const auto name="_retainedGeneratedImage1";
    assert(images.values.at(name).pixels==pixels && images.values.at(name).direct==1 && images.values.at(name).legacy==0);
    assert(images.values.at(name).options.textureType==TT_2D && images.values.at(name).options.height==6);
    assert(images.values.at(name).filter==TF_LINEAR && images.values.at(name).repeat==7);
    const auto second=host.GenerateTexture(pixels,2,3);
    assert(second && first!=second && host.generatedImages.ResidentBytes()==48);
    host.ReleaseGeneratedTexture(first);host.ReleaseGeneratedTexture(first);host.ReleaseGeneratedTexture(999);
    host.CollectGeneratedImages();
    assert(images.values.at(name).pixels==pixels && host.generatedImages.ResidentBytes()==48);
    ++host.frame;host.CollectGeneratedImages();
    assert(images.values.at(name).options.width==1 && images.values.at(name).options.height==1);
    assert(host.generatedImages.ResidentBytes()==24 && host.generatedImages.IsLive(2));
    const auto replacement=host.GenerateTexture(pixels,3,2);
    assert(replacement==first && images.values.size()==2 && declarations.values.size()==2);
    host.ReleaseGeneratedTexture(second);host.ReleaseGeneratedTexture(replacement);
    ++host.frame;host.CollectGeneratedImages();assert(!host.generatedImages.ResidentBytes());
    const auto creates=renderer.creates;
    renderer.config.maxTextureSize=2;
    assert(!host.GenerateTexture(pixels,1,6) && renderer.creates==creates);
    renderer.config.maxTextureSize=4096;
    assert(!host.GenerateTexture(pixels,2,2) && !host.GenerateTexture(pixels,0,6));
    for(int fail=0;fail<3;++fail){
        renderer.failCreate=fail==0;images.missing=fail==1;declarations.fail=fail==2;
        assert(!host.GenerateTexture(pixels,1,6));
        assert(host.generatedImages.ResidentBytes()==24 && !host.generatedImages.IsLive(1));
        renderer.failCreate=false;images.missing=false;declarations.fail=false;
        ++host.frame;host.CollectGeneratedImages();assert(!host.generatedImages.ResidentBytes());
    }
    for(int cycle=0;cycle<1000;++cycle){
        auto handle=host.GenerateTexture(pixels,1,6);assert(handle);
        host.ReleaseGeneratedTexture(handle);++host.frame;host.CollectGeneratedImages();
        assert(!host.generatedImages.ResidentBytes() && images.values.size()==2 && declarations.values.size()==2);
    }
    assert(!renderer.UploadImage(name,nullptr,1,1) && !renderer.UploadImage(name,pixels.data(),0,1));
    assert(!renderer.UploadImage("missing",pixels.data(),1,1));
    auto& legacy=images.values.at(name);legacy.scratch=false;legacy.filter=8;legacy.repeat=9;
    assert(renderer.UploadImage(name,pixels.data(),1,6) && legacy.legacy==1 && legacy.filter==8 && legacy.repeat==9);
    renderSystem=nullptr;assert(!host.GenerateTexture(pixels,1,6));assert(host.ShrinkGeneratedImage(1));
    std::puts("Generated image engine adapter: bounded allocations, exact 2D upload including 1:6, failures, frame retirement, 1000 cycles and legacy sampler preservation passed");
}
'''


def source() -> str:
    engine = (ROOT / "src/ui/RetainedUI.cpp").read_text(encoding="utf-8")
    renderer = (ROOT / "src/renderer/RenderSystem.cpp").read_text(encoding="utf-8")
    result = SUPPORT + function_body(renderer, "bool idRenderSystemLocal::UploadImage(").replace(
        "idRenderSystemLocal::UploadImage", "Renderer::UploadImage") + HOST
    for signature in ("std::uintptr_t GenerateTexture(", "void ReleaseGeneratedTexture(",
                      "bool ShrinkGeneratedImage(", "void CollectGeneratedImages("):
        result += function_body(engine, signature).replace(" override", "") + "\n"
    return result + MAIN


def main() -> None:
    if len(sys.argv) == 3 and sys.argv[1] == "--generate-native":
        Path(sys.argv[2]).write_text(source(), encoding="utf-8")
        return
    if len(sys.argv) != 1:
        raise SystemExit("usage: ui_generated_images.py [--generate-native OUTPUT]")
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("C++20 compiler required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ui-generated-", dir=ROOT / ".tmp") as directory:
        cpp, executable = Path(directory) / "generated.cpp", Path(directory) / "generated.exe"
        cpp.write_text(source(), encoding="utf-8")
        subprocess.run([compiler,"-std=c++20","-I",str(ROOT),str(cpp),"-o",str(executable)],check=True)
        subprocess.run([str(executable)],check=True)


if __name__ == "__main__":
    main()
