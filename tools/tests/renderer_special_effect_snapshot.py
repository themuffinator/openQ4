#!/usr/bin/env python3
"""Run production controller enqueue/consume paths across later game-tic edits."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <cassert>
#include <cstring>
#include <memory>
#include <vector>
enum { SPECIAL_EFFECT_NONE=0, SPECIAL_EFFECT_BLUR=1, SPECIAL_EFFECT_AL=2,
       SPECIAL_EFFECT_MAX=3, MAX_ENTITY_SHADER_PARMS=16, RF_DEFER_COMMAND_SUBMIT=1,
       TF_LINEAR=0, TR_CLAMP=0, TD_DEFAULT=0 };
using ESpecialEffectType=int;
enum renderCommand_t { RC_DRAW_VIEW, RC_DRAW_SPECIAL_EFFECTS };
struct viewDef_t {
    struct { int viewID=0; } renderView;
    int renderFlags=0,numDrawSurfs=1;
    void* renderWorld=this;
};
struct Cvar { int value=0; int GetInteger()const{return value;} } r_forceSpecialEffects;
struct idImage {} image;
struct Images {
    int loads=0;
    idImage* ImageFromFile(const char*,int,int,int){++loads;return &image;}
} images,*globalImages=&images;
struct Target { int GetWidth(){return 256;} int GetHeight(){return 256;} } target;
struct idRenderSystemLocal {
    int specialEffectsEnabled=0;
    float specialEffectParms[SPECIAL_EFFECT_MAX][MAX_ENTITY_SHADER_PARMS]{};
    idImage* specialALLightImage=nullptr;
    Target* specialBlurDepthRenderTexture=&target;
    Target* specialALDepthRenderTexture=&target;
    void SetSpecialEffect(ESpecialEffectType,bool);
    void SetSpecialEffectParm(ESpecialEffectType,int,float);
    void ResetSpecialEffects();
} tr;
static int syncs=0,flushes=0,packetMask=0;
static bool deferred=false;
static void R_RenderThread_Sync(const char*){++syncs;}
static void R_SubmitDeferredCommands(){++flushes;}
static std::vector<std::unique_ptr<char[]>> buffers;
static void* Allocate(int size,bool isDeferred){
    deferred=isDeferred;buffers.emplace_back(new char[size]{});return buffers.back().get();
}
static void* R_GetCommandBuffer(int size){return Allocate(size,false);}
static void* R_GetCommandBufferDeferred(int size){return Allocate(size,true);}
static bool R_ScenePackets_FrontEndCaptureRequired(){return true;}
static void R_ScenePackets_AddSpecialEffects(const viewDef_t*,int mask){packetMask=mask;}
template<class T> static T Max(T a,T b){return a>b?a:b;}
'''

BACKENDS = r'''
static struct { const viewDef_t* viewDef=nullptr; int frameCount=1; } backEnd;
static struct { bool GLSLProgramAvailable=true; } glConfig;
static const viewDef_t* rbRVSpecialCommandView=nullptr;
static int rbRVSpecialCommandFrame=0,rbRVSpecialActiveMask=0;
static bool rbRVSpecialBlurPrepared=false,rbRVSpecialALPrepared=false;
static specialEffectsState_t rbRVSpecialEffects;
static float capturedDistance=0;
static bool RB_EnsureRVSpecialBlurResources(){return true;}
static bool RB_EnsureRVSpecialALResources(){return true;}
static bool RB_CaptureRVSpecialDepth(Target*,int,int,bool,float distance){capturedDistance=distance;return true;}
static struct {
    const viewDef_t* pendingSpecialEffectsView=nullptr;
    int pendingSpecialEffectsMask=0;
    specialEffectsState_t pendingSpecialEffects{};
    Target* pendingSpecialEffectsSource=nullptr;
    bool pendingSpecialEffectsNeedsResolve=false;
} vkExec;
'''

CASES = r'''
static drawSpecialEffectsCommand_t* Enqueue(viewDef_t& view){
    R_AddSpecialEffects(&view);
    return reinterpret_cast<drawSpecialEffectsCommand_t*>(buffers.back().get());
}
int main(){
    tr.ResetSpecialEffects();
    viewDef_t first,second;
    tr.SetSpecialEffect(SPECIAL_EFFECT_BLUR,true);
    tr.SetSpecialEffectParm(SPECIAL_EFFECT_BLUR,7,640.0f);
    auto* a=Enqueue(first);
    assert(!deferred && flushes==1 && packetMask==SPECIAL_EFFECT_BLUR);
    tr.SetSpecialEffectParm(SPECIAL_EFFECT_BLUR,7,1280.0f);
    second.renderFlags=RF_DEFER_COMMAND_SUBMIT;
    auto* b=Enqueue(second);
    assert(deferred && flushes==1 && syncs==0);
    // A later tic disables and resets the controller before either backend runs.
    tr.SetSpecialEffect(SPECIAL_EFFECT_BLUR,false);
    tr.ResetSpecialEffects();
    r_forceSpecialEffects.value=SPECIAL_EFFECT_AL;
    RB_DrawSpecialEffects(a);
    assert(rbRVSpecialCommandView==&first && rbRVSpecialActiveMask==SPECIAL_EFFECT_BLUR);
    assert(rbRVSpecialEffects.parms[SPECIAL_EFFECT_BLUR][7]==640 && capturedDistance==640);
    VK_GuiExecutor_PrepareSpecialEffects(a);
    assert(vkExec.pendingSpecialEffectsView==&first && vkExec.pendingSpecialEffectsMask==SPECIAL_EFFECT_BLUR);
    assert(vkExec.pendingSpecialEffects.parms[SPECIAL_EFFECT_BLUR][7]==640);
    RB_DrawSpecialEffects(b);VK_GuiExecutor_PrepareSpecialEffects(b);
    assert(rbRVSpecialCommandView==&second && capturedDistance==1280);
    assert(vkExec.pendingSpecialEffectsView==&second && vkExec.pendingSpecialEffects.parms[SPECIAL_EFFECT_BLUR][7]==1280);
    assert(syncs==0);
    // Forced admission is also captured at enqueue, not re-read during recording.
    r_forceSpecialEffects.value=SPECIAL_EFFECT_BLUR;
    auto* forced=Enqueue(first);
    r_forceSpecialEffects.value=0;
    VK_GuiExecutor_PrepareSpecialEffects(forced);
    assert(vkExec.pendingSpecialEffectsMask==SPECIAL_EFFECT_BLUR);
    const auto count=buffers.size();
    R_AddSpecialEffects(&first);R_AddSpecialEffects(nullptr);
    tr.SetSpecialEffect(SPECIAL_EFFECT_BLUR,true);
    first.renderView.viewID=-1;R_AddSpecialEffects(&first);
    assert(buffers.size()==count);
    // Actual resource creation still synchronizes, and only on first use.
    tr.SetSpecialEffect(SPECIAL_EFFECT_AL,true);
    tr.SetSpecialEffect(SPECIAL_EFFECT_AL,false);
    tr.SetSpecialEffect(SPECIAL_EFFECT_AL,true);
    assert(syncs==1 && images.loads==1);
    tr.SetSpecialEffect(-1,true);tr.SetSpecialEffect(SPECIAL_EFFECT_MAX,true);
    tr.SetSpecialEffectParm(SPECIAL_EFFECT_BLUR,-1,99);
    tr.SetSpecialEffectParm(SPECIAL_EFFECT_BLUR,MAX_ENTITY_SHADER_PARMS,99);
    assert(syncs==1);
}
'''


def main():
    def read(path):
        return (ROOT / path).read_text(encoding="utf-8")

    header = read("src/renderer/tr_local.h")
    end = header.index("} drawSpecialEffectsCommand_t;") + len("} drawSpecialEffectsCommand_t;")
    start = header.rindex("typedef struct {", 0, header.index("} specialEffectsState_t;"))
    source = SUPPORT + header[start:end] + BACKENDS
    front = read("src/renderer/RenderSystem.cpp")
    for signature in ("void R_AddSpecialEffects(", "void idRenderSystemLocal::SetSpecialEffect(",
                      "void idRenderSystemLocal::SetSpecialEffectParm(", "void idRenderSystemLocal::ResetSpecialEffects("):
        source += function_body(front, signature)
    gl = read("src/renderer/draw_common.cpp")
    vk = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    source += function_body(gl, "void RB_DrawSpecialEffects(")
    source += function_body(vk, "void VK_GuiExecutor_PrepareSpecialEffects(")
    # The executable exercises copies, while these guards cover later shader reads.
    for backend in (gl, vk, read("src/renderer/ScenePackets.cpp")):
        assert "tr.specialEffectParms" not in backend
        assert "tr.specialEffectsEnabled" not in backend
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="special-effects-", dir=ROOT / ".tmp") as directory:
        temp = Path(directory)
        cpp = temp / "snapshot.cpp"
        cpp.write_text(source + CASES, encoding="utf-8")
        exe = temp / ("snapshot.exe" if os.name == "nt" else "snapshot")
        subprocess.run([compiler, "-std=c++20", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=30)
    print("Special effects: GL/Vulkan queued views survive later edits/reset, forced masks and resource-only waits passed")


if __name__ == "__main__":
    main()
