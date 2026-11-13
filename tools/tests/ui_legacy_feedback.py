#!/usr/bin/env python3
"""Exercise production GUI batch boundaries and stock message expansion."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cstring>
#include <vector>
enum { SS_GUI=1, SS_POST_PROCESS=2, MF_NEED_CURRENT_RENDER=1 };
struct idMaterial {
    int sort=SS_GUI, flags=0;
    int GetSort() const { return sort; }
    bool TestMaterialFlag(int f) const { return (flags&f)!=0; }
};
struct guiModelSurface_t { int numIndexes; const idMaterial* material; };
template<class T> struct List:std::vector<T> { using std::vector<T>::operator=; int Num() const { return int(this->size()); } };
struct idGuiModel { List<guiModelSurface_t> surfaces; bool HasSceneFeedback() const; };
struct renderCrop_t { int x=0,y=0,width=1280,height=720; };
struct { int uiViewportX=0,uiViewportY=0,uiViewportWidth=1280,uiViewportHeight=720,vidHeight=720; } glConfig;
struct View { bool isEditor=false; };
struct idRenderSystemLocal {
    bool useUIViewportFor2D=false; int currentRenderCrop=0,flushes=0;
    renderCrop_t renderCrops[2]; void* activeRenderTexture=nullptr; View* viewDef=nullptr;
    idGuiModel model; idGuiModel* guiModel=&model;
    void FlushGui() { ++flushes; model.surfaces.clear(); }
    void SetUseUIViewportFor2D(bool enable);
};
struct idStr { static int Icmp(const char*a,const char*b) {
    while (*a && *b) { int x=(*a>='A'&&*a<='Z')?*a+32:*a; int y=(*b>='A'&&*b<='Z')?*b+32:*b;
        if(x!=y)return x-y; ++a;++b; } return *a-*b;
} };
struct idRectangle { float x,y,w,h; };
'''
CHECKS = r'''
int main() {
    // Both backends end the batch at the final scene composite: GL for its
    // eager capture, and both so the HUD that follows is a pure GUI view the
    // shared GUI domain can own (REN-01). The VK build must agree with GL.
    constexpr int captureFlush = 1;
    idRenderSystemLocal r;
    idMaterial ui, composite{SS_POST_PROCESS,0}, feedback{SS_GUI,MF_NEED_CURRENT_RENDER};
    // Ordinary retained/legacy quads with an identical viewport still batch.
    r.model.surfaces={{6,&ui}}; r.SetUseUIViewportFor2D(true);
    assert(r.flushes==0 && r.model.surfaces.size()==1);
    // The composite needs a boundary even if ordinary UI is last in the batch.
    r.model.surfaces={{6,&composite},{6,&ui}}; r.SetUseUIViewportFor2D(false);
    assert(r.flushes==captureFlush && (r.model.surfaces.empty() == bool(captureFlush)));
    r.model.surfaces={{6,&feedback}}; r.SetUseUIViewportFor2D(true);
    assert(r.flushes==2*captureFlush);
    // Empty material changes must not turn harmless UI into an extra flush.
    r.model.surfaces={{0,&composite},{6,&ui},{0,nullptr}}; r.SetUseUIViewportFor2D(false);
    assert(r.flushes==2*captureFlush);
    glConfig.uiViewportX=100; glConfig.uiViewportWidth=1080;
    r.SetUseUIViewportFor2D(true); assert(r.flushes==2*captureFlush+1);
    r.SetUseUIViewportFor2D(true); assert(r.flushes==2*captureFlush+1);
    for (auto expansion : {idRectangle{106,0,0,0},idRectangle{0,329,0,0},idRectangle{0,0,0,0}}) {
        idRectangle rect{0,0,640,480};
        OpenQ4_ExpandLegacyMessageBackdrop("guis/msg.gui","msg_black",expansion.x,expansion.y,rect);
        assert(rect.x==-expansion.x && rect.y==-expansion.y);
        assert(rect.w==640+2*expansion.x && rect.h==480+2*expansion.y);
    }
    // Furniture, unrelated documents and custom-sized mod backings are untouched.
    for (const char* name : {"msg_bg","msg_top","msg_text"}) {
        idRectangle rect{0,0,640,480};
        OpenQ4_ExpandLegacyMessageBackdrop("guis/msg.gui",name,100,100,rect);
        assert(rect.x==0 && rect.w==640);
    }
    idRectangle custom{10,0,620,480};
    OpenQ4_ExpandLegacyMessageBackdrop("guis/msg.gui","msg_black",100,100,custom);
    assert(custom.x==10 && custom.w==620);
    idRectangle other{0,0,640,480};
    OpenQ4_ExpandLegacyMessageBackdrop("guis/mainmenu.gui","msg_black",100,100,other);
    assert(other.x==0 && other.w==640);
}
'''

MASK_SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <string>
struct idStr {
    std::string value;
    idStr(const char* s=""):value(s){}
    operator const char*() const { return value.c_str(); }
    int Length() const { return int(value.size()); }
    void BackSlashesToSlashes() { std::replace(value.begin(),value.end(),'\\','/'); }
    void ExtractFileExtension(idStr& out) const {
        auto dot=value.find_last_of('.'); out.value=dot==std::string::npos?"":value.substr(dot+1);
    }
    void StripFileExtension() { auto dot=value.find_last_of('.'); if(dot!=std::string::npos)value.resize(dot); }
    void DefaultFileExtension(const char* ext) { if(value.find_last_of('.')==std::string::npos)value+=ext; }
    int Icmp(const char* other) const {
        auto a=value,b=std::string(other);
        for(auto& c:a)c=std::tolower(c); for(auto& c:b)c=std::tolower(c); return a.compare(b);
    }
    static int Icmpn(const char* a,const char* b,int n) {
        return idStr(std::string(a).substr(0,n).c_str()).Icmp(std::string(b).substr(0,n).c_str());
    }
};
struct Files {
    bool present=true; int probes=0;
    int ReadFile(const char* name,void*) { ++probes; assert(std::string(name).find(".tga")!=std::string::npos); return present?128:-1; }
} files;
auto* fileSystem=&files;
'''
MASK_CHECKS = r'''
int main() {
    const std::string base="gfx/guis/hud/reticles/mgun_scope_distort";
    for(const char* suffix:{"","_left","_right","_top","_bottom"}) {
        auto name=base+suffix;
        assert(R_HasUncompressedLegacyScopeMask(name.c_str()));
        assert(R_HasUncompressedLegacyScopeMask((name+".tga").c_str()));
        files.present=false;
        assert(!R_HasUncompressedLegacyScopeMask(name.c_str())); // DDS-only packs retain fallback
        files.present=true;
    }
    assert(R_IsLegacyScopeDistortionMask("GFX\\GUIS\\HUD\\RETICLES\\MGUN_SCOPE_DISTORT.TGA"));
    const int probes=files.probes;
    for(const char* name:{static_cast<const char*>(nullptr),"gfx/guis/hud/reticles/mgun_scope_distort.dds",
            "gfx/guis/hud/reticles/mgun_scope_distort_mod.tga",
            "gfx/guis/hud/reticles/mgun_scope_grey.tga","textures/stone.tga"}) {
        assert(!R_HasUncompressedLegacyScopeMask(name));
    }
    assert(files.probes==probes);
}
'''


def main():
    (ROOT / '.tmp').mkdir(exist_ok=True)
    compiler = next((shutil.which(n) for n in ('clang++', 'g++', 'c++') if shutil.which(n)), None)
    assert compiler, 'C++ compiler required'
    bodies = '\n'.join(function_body((ROOT / path).read_text(), signature) for path, signature in [
        ('src/renderer/GuiModel.cpp', 'bool idGuiModel::HasSceneFeedback() const'),
        ('src/renderer/RenderSystem.cpp', 'void idRenderSystemLocal::SetUseUIViewportFor2D( bool enable )'),
    ])
    for path in ('src/ui/Window.cpp', 'src/ui/SimpleWindow.cpp'):
        assert 'OpenQ4_ExpandLegacyMessageBackdrop(' in (ROOT / path).read_text(), path
    with tempfile.TemporaryDirectory(prefix='legacy-feedback-', dir=ROOT / '.tmp') as temp:
        source = Path(temp) / 'check.cpp'
        binary = Path(temp) / 'check.exe'
        source.write_text(SUPPORT + bodies + '\n#include "' +
                          (ROOT / 'src/ui/LegacyGuiLayout.h').as_posix() + '"\n' + CHECKS)
        subprocess.run([compiler, '-std=c++17', str(source), '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)
        subprocess.run([compiler, '-std=c++17', '-DOPENQ4_RENDERER_VK_MODULE', str(source), '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)
        mask = '#include "' + (ROOT / 'src/imagetools/LegacyScopeMask.h').as_posix() + '"\n'
        mask += function_body((ROOT / 'src/imagetools/Image_files.cpp').read_text(),
                              'static bool R_HasUncompressedLegacyScopeMask( const char *name )')
        source.write_text(MASK_SUPPORT + mask + MASK_CHECKS)
        subprocess.run([compiler, '-std=c++17', str(source), '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)
    print('legacy UI: feedback ordering, retained batching, wide/tall message geometry and mask source policy passed')


if __name__ == '__main__':
    main()
