#!/usr/bin/env python3
"""Run the production retained target allocator against a counted renderer.

Checks mixed viewport sizes, the shared byte budget and non-destructive failed
allocation. No graphics device, game window or host input is accessed.
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
#include <memory>
#include <string>
#include <vector>
struct idImageOpts {int width=0,height=0,format=0,numLevels=0;bool isPersistant=false;};
enum {FMT_RGBA8,TF_NEAREST,DS_DEFAULTED};
struct idImage {};
struct idMaterial {int GetState() const {return 1;}};
struct idRenderTexture {bool destroyed=false;};
struct Renderer {
    int creates=0,destroys=0,clears=0;bool failImage=false;
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
    void BindRenderTexture(idRenderTexture* target,void*){assert(target && !target->destroyed);bound=target;}
    void ClearRenderTarget(bool color,bool depth,int,float r,float g,float b,float a){assert(color && !depth && r==0 && g==0 && b==0 && a==0);++clears;}
} renderer;
Renderer* renderSystem=&renderer;
struct Declarations {idMaterial material;const idMaterial* FindMaterial(const char*){return &material;}} declarations;
Declarations* declManager=&declarations;
const char* va(const char* format,unsigned value){static char text[128];std::snprintf(text,sizeof text,format,value);return text;}
struct Host {
    struct Layer {idRenderTexture* target=nullptr;const idMaterial* material=nullptr;const idMaterial* maskMaterial=nullptr;int width=0,height=0;};
    std::vector<Layer> layers;
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
    std::puts("retained layer pool: mixed viewport preservation, exact shared byte budget and failure isolation passed");
}
'''


def main():
    source=(ROOT/'src/ui/RetainedUI.cpp').read_text()
    code=SUPPORT+function_body(source,'bool BeginLayer(').replace(' override','')+MAIN
    compiler=next((found for name in ('clang++','g++','c++') if (found:=shutil.which(name))),None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-layer-pool-',dir=ROOT/'.tmp') as temp:
        source=Path(temp)/'pool.cpp';binary=Path(temp)/'pool.exe'
        source.write_text(code,encoding='utf-8')
        subprocess.run([compiler,'-std=c++17',str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)


if __name__=='__main__':
    main()
