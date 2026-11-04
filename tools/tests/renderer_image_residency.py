#!/usr/bin/env python3
"""Execute image reload selection and bounded Vulkan slot admission without a GPU."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
struct idImage {
    bool loaded=false,file=true,referencedOutsideLevelLoad=false,levelLoadReferenced=false;
    bool IsLoaded()const{return loaded;}bool IsFileBacked()const{return file;}
    bool IsReloadReferenced()const;
};
struct Receipt { bool resident; };
struct Attempt { std::map<const idImage*,Receipt> images; };
Attempt *attempt=nullptr;
Attempt *Current(){return attempt;}
struct vkImageEntry_t { bool inUse=false; };
constexpr int VK_MAX_IMAGES=4096;
vkImageEntry_t vkImages[VK_MAX_IMAGES];
bool vkImageTableWarned=false;
int warnings=0;
struct Common { void Warning(const char*,int){++warnings;} } commonObject;
Common *common=&commonObject;
'''
MAIN = r'''
int main() {
    // Every combination of loaded/file/level/outside reference status.
    for(int bits=0;bits<16;++bits) {
        idImage image{bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8)};
        const bool expected=bits!=2;
        assert(R_ImagePolicyShouldReload(&image)==expected);
        // An explicit checked restart's residency receipt remains authoritative.
        Attempt checked;attempt=&checked;checked.images[&image]={false};
        assert(!R_ImagePolicyShouldReload(&image));checked.images[&image]={true};
        assert(R_ImagePolicyShouldReload(&image));checked.images.clear();
        assert(R_ImagePolicyShouldReload(&image));attempt=nullptr;
    }
    assert(!R_ImagePolicyShouldReload(nullptr));
    // A catalogue accumulated across old maps must not become a resident set.
    std::vector<idImage> catalogue(12000);
    for(int i=0;i<2200;++i)catalogue[i].levelLoadReferenced=true;
    for(int i=2200;i<2400;++i)catalogue[i].referencedOutsideLevelLoad=true;
    catalogue[2400].file=false;
    int reloads=0;
    for(const auto& image:catalogue)if(R_ImagePolicyShouldReload(&image))++reloads;
    assert(reloads==2401 && reloads<VK_MAX_IMAGES);
    // Both lookup and admission execute the production table bounds.
    assert(!VK_Image_GetEntry(0xffffffffu) && !VK_Image_GetEntry(VK_MAX_IMAGES));
    for(int i=0;i<VK_MAX_IMAGES;++i) {
        assert(VK_Image_AllocSlot()==i);vkImages[i].inUse=true;assert(VK_Image_GetEntry(i)==&vkImages[i]);
    }
    assert(VK_Image_GetResidentCount()==VK_MAX_IMAGES);
    for(int i=0;i<1000;++i)assert(VK_Image_AllocSlot()==-1);
    assert(warnings==1);vkImages[123].inUse=false;
    assert(!VK_Image_GetEntry(123) && VK_Image_GetResidentCount()==VK_MAX_IMAGES-1);
    assert(VK_Image_AllocSlot()==123);vkImages[123].inUse=true;
    assert(VK_Image_AllocSlot()==-1 && warnings==2);
    std::puts("Image residency: purged-map laziness, startup references, checked recovery, 4096-slot pressure and warning recovery passed");
}
'''


def main():
    header = (ROOT / 'src/renderer/Image.h').read_text()
    policy = (ROOT / 'src/renderer/RendererResourceSettings.cpp').read_text()
    images = (ROOT / 'src/renderer/Vulkan/vk_Image.cpp').read_text()
    predicate = function_body(header, 'bool IsReloadReferenced() const').replace(
        'bool IsReloadReferenced()', 'bool idImage::IsReloadReferenced()', 1)
    source = SUPPORT + predicate + function_body(policy, 'bool R_ImagePolicyShouldReload(')
    source += '\n'.join(function_body(images, signature) for signature in (
        'vkImageEntry_t *VK_Image_GetEntry(', 'static int VK_Image_AllocSlot(',
        'int VK_Image_GetResidentCount(')) + MAIN
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    scratch = ROOT / '.tmp/renderer-image-residency'
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as directory:
        directory = Path(directory)
        environment = dict(os.environ, TEMP=str(directory), TMP=str(directory))
        test, binary = directory / 'residency.cpp', directory / 'residency.exe'
        test.write_text(source)
        subprocess.run([compiler, '-std=c++17', str(test), '-o', str(binary)], check=True, env=environment)
        subprocess.run([str(binary)], check=True, env=environment)


if __name__ == '__main__':
    main()
