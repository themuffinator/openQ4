#!/usr/bin/env python3
"""Keep Vulkan shadow ownership and query retirement on the submitted frame."""
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
#include <cstdint>
enum renderCommand_t { RC_NOP, RC_SET_BUFFER, RC_DRAW_VIEW };
struct emptyCommand_t { renderCommand_t commandId,*next; };
struct setBufferCommand_t { renderCommand_t commandId,*next; unsigned buffer; int frameCount; };
struct { int frameCount=9999; } tr;
struct { int frameCount=4; } backEnd;
struct classicDeformGeometryState_t { int generation; };
struct classicDeformRecord_t { std::uint64_t frameToken; void* resultMaterial; classicDeformGeometryState_t resultGeometry; };
struct drawSurf_t { void* material; const classicDeformGeometryState_t* geo; };
static bool R_ClassicDeformDomain_ValidateRecordForFrame(const classicDeformRecord_t& r,std::uint64_t token){return r.frameToken==token;}
static void CaptureGeometryState(const classicDeformGeometryState_t* geo,classicDeformGeometryState_t& result){result=*geo;}
static bool GeometryStateMatches(const classicDeformGeometryState_t& a,const classicDeformGeometryState_t& b){return a.generation==b.generation;}
struct viewDef_t {};
struct viewLight_t {};
struct vkShadowLightState_t { const viewLight_t* vLight; bool valid; };
struct {
    const viewDef_t* preparedView=nullptr;
    int preparedFrame=-1,numLights=0;
    vkShadowLightState_t lights[2]{};
} vkShadow;
enum { VK_NULL_HANDLE=0, VK_FRAMES_IN_FLIGHT=2, VK_SHADOW_TIMING_RING_SPANS=8,
       VK_SHADOW_TIMING_MAX_SPAN_AGE_FRAMES=16 };
using vkShadowTimingPhase_t=int;
struct vkShadowTimingSpan_t {
    bool claimed=false,open=false,pending=false;
    int frameNumber=0,phase=0;
};
static vkShadowTimingSpan_t vkShadowTimingSpans[VK_SHADOW_TIMING_RING_SPANS];
static int vkShadowTimingFrameSlot=0,vkShadowTimingQueryPool=1,vkShadowTimingNextSpan=0;
static struct { bool available,enabled,synchronized; int pending,dropped; } vkShadowTimingReport;
struct { bool value=true; bool GetBool(){return value;} } r_shadowMapGpuSyncTimings;
static bool enabled=true;
static bool VK_ShadowGpuTiming_Available(){return true;}
static bool VK_ShadowGpuTiming_Enabled(){return enabled;}
static bool resolved[VK_SHADOW_TIMING_RING_SPANS]{},waited[VK_SHADOW_TIMING_RING_SPANS]{};
static void VK_ShadowGpuTiming_ReleaseSpan(vkShadowTimingSpan_t& span){span={};}
static void VK_ShadowGpuTiming_ResolveSpan(vkShadowTimingSpan_t&,int i,bool wait){resolved[i]=true;waited[i]=wait;}
'''

CASES = r'''
int main(){
    setBufferCommand_t begin{RC_SET_BUFFER,nullptr,0,5};
    emptyCommand_t nop{RC_NOP,&begin.commandId};
    RB_BeginCommandFrame(&nop);
    assert(backEnd.frameCount==5 && tr.frameCount==9999);
    assert(R_ClassicDeformDomain_CurrentFrameToken()==10000);
    assert(R_ClassicDeformDomain_BackEndFrameToken()==6);
    classicDeformGeometryState_t geo{42};
    drawSurf_t draw{nullptr,&geo};
    classicDeformRecord_t record{6,nullptr,geo};
    assert(R_ClassicDeformDomain_RecordMatchesDrawSurf(record,&draw,R_ClassicDeformDomain_BackEndFrameToken()));
    assert(!R_ClassicDeformDomain_RecordMatchesDrawSurf(record,&draw,R_ClassicDeformDomain_CurrentFrameToken()));
    geo.generation=43;
    assert(!R_ClassicDeformDomain_RecordMatchesDrawSurf(record,&draw,R_ClassicDeformDomain_BackEndFrameToken()));
    // Captures and mid-frame command flushes need no new SET_BUFFER.
    emptyCommand_t partial{RC_DRAW_VIEW,nullptr};
    RB_BeginCommandFrame(&partial);RB_BeginCommandFrame(nullptr);
    assert(backEnd.frameCount==5);
    viewDef_t view,other;
    viewLight_t light;
    vkShadow.preparedView=&view;vkShadow.preparedFrame=5;
    vkShadow.numLights=1;vkShadow.lights[0]={&light,true};
    assert(VK_ShadowMap_ViewPrepared(&view) && !VK_ShadowMap_ViewPrepared(&other));
    assert(VK_ShadowMap_LightState(&light)==&vkShadow.lights[0]);
    ++tr.frameCount;
    assert(VK_ShadowMap_ViewPrepared(&view) && VK_ShadowMap_LightState(&light));
    vkShadow.lights[0].valid=false;assert(!VK_ShadowMap_LightState(&light));
    vkShadow.lights[0].valid=true;
    // Queries of this frame remain pending even if the front end is far ahead.
    const int claimed=VK_ShadowGpuTiming_ClaimSpan(1);
    assert(claimed==0 && vkShadowTimingSpans[0].frameNumber==5);
    vkShadowTimingSpans[0].pending=true;
    vkShadowTimingSpans[1]={true,false,true,4,1};
    vkShadowTimingSpans[2]={true,false,true,2,1};
    vkShadowTimingSpans[3]={true,true,false,5,1};
    vkShadowTimingSpans[4]={true,true,false,2,1};
    VK_ShadowGpuTiming_BeginView(1);
    assert(vkShadowTimingReport.pending==1 && vkShadowTimingReport.dropped==1);
    assert(!resolved[0] && resolved[1] && !waited[1] && resolved[2] && waited[2]);
    assert(vkShadowTimingSpans[3].claimed && !vkShadowTimingSpans[4].claimed);
    enabled=false;VK_ShadowGpuTiming_BeginView(1);
    assert(vkShadowTimingSpans[0].claimed && vkShadowTimingSpans[1].claimed);
    assert(!vkShadowTimingSpans[2].claimed && vkShadowTimingSpans[3].claimed);
    begin.frameCount=6;RB_BeginCommandFrame(&nop);
    // Recycled arena addresses must not revive last frame's shadow records.
    assert(!VK_ShadowMap_ViewPrepared(&view) && !VK_ShadowMap_LightState(&light));
}
'''


def main():
    def read(path):
        return (ROOT / path).read_text(encoding="utf-8")

    source = SUPPORT + function_body(read("src/renderer/RenderSystem.cpp"), "void RB_BeginCommandFrame(")
    deform = read("src/renderer/ClassicDeformDomain.cpp")
    for signature in ("std::uint64_t R_ClassicDeformDomain_CurrentFrameToken(",
                      "std::uint64_t R_ClassicDeformDomain_BackEndFrameToken(",
                      "bool R_ClassicDeformDomain_RecordMatchesDrawSurf("):
        source += function_body(deform, signature)
    for name in ("ClassicGuiDomain", "ClassicInteractionDomain", "ClassicFogBlendDomain", "ClassicWorldAmbientDomain"):
        domain = read("src/renderer/" + name + ".cpp")
        assert "R_ClassicDeformDomain_CurrentFrameToken()" not in domain
        assert "R_ClassicDeformDomain_RecordMatchesDrawSurf( record, drawSurf, frameToken )" in domain
    shadow = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    timing = read("src/renderer/Vulkan/VulkanShadowTiming.cpp")
    for signature in ("static bool VK_ShadowMap_ViewPrepared(", "const vkShadowLightState_t *VK_ShadowMap_LightState("):
        source += function_body(shadow, signature)
    for signature in ("void VK_ShadowGpuTiming_BeginView(", "static int VK_ShadowGpuTiming_ClaimSpan("):
        source += function_body(timing, signature)
    # Preflight must see the command identity before creating any GPU resources.
    for path in ("src/renderer/tr_backend.cpp", "src/renderer/Vulkan/vk_Backend.cpp"):
        body = function_body(read(path), "void RB_ExecuteBackEndCommands(")
        assert body.index("RB_BeginCommandFrame( cmds );") < body.index("R_ClassicGuiDomain_ResetFrame();")
    for backend in (shadow, timing, read("src/renderer/Vulkan/vk_GuiExecutor.cpp")):
        assert "tr.frameCount" not in backend
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="frame-identity-", dir=ROOT / ".tmp") as directory:
        temp = Path(directory)
        cpp = temp / "identity.cpp"
        cpp.write_text(source + CASES, encoding="utf-8")
        exe = temp / ("identity.exe" if os.name == "nt" else "identity")
        subprocess.run([compiler, "-std=c++20", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=30)
    print("Frame identity: submitted-frame deformation/shadow records, query ages and partial command streams passed")


if __name__ == "__main__":
    main()
