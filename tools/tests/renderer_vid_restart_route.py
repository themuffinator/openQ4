#!/usr/bin/env python3
"""Run the production full-restart and startup routes for both renderer modules.

Only device/resource boundaries are stand-ins. The extracted functions retain
their real calls and preprocessor branches; no window, GPU or input is opened.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
static std::vector<std::string> events;
static bool deviceAlive=false, fontsAlive=false, configuredFullscreen=true;
static bool createdFullscreen=false;
static int imageReloads=0, imagePurges=0, deviceStarts=0;
static void Record(const char* event) { events.emplace_back(event); }
struct renderFeatureSet_t { bool scenePackets=false; };
struct Config {
    bool isInitialized=false;
    int backendCaps=0;
    renderFeatureSet_t renderFeatures;
} glConfig;
struct CVars {
    bool GetCVarBool(const char*) { return configuredFullscreen; }
    void SetCVarBool(const char*,bool value) { configuredFullscreen=value; }
} cvars, *cvarSystem=&cvars;
struct RendererCvar { void SetModified() { Record("renderer-selection"); } } r_renderer;
struct Common {
    void FatalError(const char* message) { throw std::runtime_error(message); }
    void Printf(const char*,...) {}
} commonObject, *common=&commonObject;
struct Images {
    void PurgeAllImages() {
        assert(deviceAlive && !fontsAlive);
        ++imagePurges; Record("purge");
    }
    void ReloadImages(bool force) {
        assert(force && deviceAlive && glConfig.isInitialized && !fontsAlive);
        ++imageReloads; Record("reload");
    }
} images, *globalImages=&images;
struct VertexCache { void Init() { assert(deviceAlive); Record("vertex-cache"); } } vertexCache;
struct idRenderSystemLocal {
    void InitOpenGL();
    void SetBackEndRenderer() { Record("backend-selection"); }
} tr;
static void StartDevice(const char* backend) {
    assert(!deviceAlive && !glConfig.isInitialized && !fontsAlive);
    createdFullscreen=configuredFullscreen;
    deviceAlive=true; glConfig.isInitialized=true;
    ++deviceStarts; Record(backend);
}
static void R_InitOpenGL() {
#ifdef OPENQ4_RENDERER_VK_MODULE
    throw std::runtime_error("Vulkan restart entered the OpenGL initializer");
#else
    StartDevice("opengl");
#endif
}
bool VK_InitRenderDevice() { StartDevice("vulkan"); return true; }
static void GLimp_Shutdown() {
    assert(deviceAlive && !fontsAlive);
    deviceAlive=false; Record("device-shutdown");
}
static void R_DoneFreeType() {
    assert(deviceAlive && fontsAlive); fontsAlive=false; Record("fonts-shutdown");
}
static void R_InitFreeType() {
    assert(deviceAlive && !fontsAlive && imageReloads==imagePurges+1);
    fontsAlive=true; Record("fonts-init");
}
static void R_RefreshConsoleFontAtlas() {
    assert(deviceAlive && fontsAlive); Record("font-atlas");
}
static void R_ShutdownRenderTargetsBeforeImagePurge() {
    assert(deviceAlive); Record("targets-shutdown");
}
static void R_ClearActiveRenderTextures() { Record("targets-clear"); }
static void R_MaterialResourceTable_Init(int,renderFeatureSet_t features) {
    assert(deviceAlive && features.scenePackets); Record("materials-init");
}
static void R_GpuSkinning_ContractInit(int) { Record("skinning-init"); }
static int glGetError() { assert(deviceAlive); return 0; }
static const int GL_NO_ERROR=0;
#define NOOP(name) static void name() { Record(#name); }
NOOP(R_MigrateLegacyShadowMapContactQuality)
NOOP(Sys_ShutdownInput)
NOOP(R_ShutdownFrameData)
NOOP(R_RendererMetrics_ShutdownGpuTimers)
NOOP(R_ClassicGuiDomain_ResetFrame)
NOOP(R_ClassicCinematicPostDomain_ResetFrame)
NOOP(R_ClassicSpecialFrameDomain_ResetFrame)
NOOP(R_ClassicWorldAmbientDomain_ResetFrame)
NOOP(R_ClassicInteractionDomain_ResetFrame)
NOOP(R_ClassicFogBlendDomain_ResetFrame)
NOOP(R_ClassicSubviewDomain_ResetFrame)
NOOP(R_MaterialResourceTable_Shutdown)
NOOP(R_RenderGraphResources_Shutdown)
NOOP(R_ModernGLExecutor_Shutdown)
NOOP(R_GpuSkinning_ContractShutdown)
NOOP(R_RendererUpload_Shutdown)
NOOP(RendererBootstrap_Shutdown)
NOOP(R_PurgeFramebufferCopyFBOs)
NOOP(R_GLDebugOutput_Shutdown)
NOOP(R_InitFrameData)
NOOP(R_SetColorMappings)
'''

MAIN = r'''
static size_t Position(const char* event) {
    auto found=std::find(events.begin(),events.end(),event);
    assert(found!=events.end());
    return static_cast<size_t>(found-events.begin());
}
int main() {
#ifdef OPENQ4_RENDERER_VK_MODULE
    const char* backend="vulkan";
#else
    const char* backend="opengl";
#endif
    tr.InitOpenGL();
    assert(deviceStarts==1 && imageReloads==1 && createdFullscreen);
    assert(Position(backend)<Position("reload"));
    events.clear();
    tr.InitOpenGL();
    assert(deviceStarts==1 && imageReloads==1);
    assert(events.size()==1 && events.front()=="R_MigrateLegacyShadowMapContactQuality");
    fontsAlive=true;

    // Repeat from each fullscreen preference, with and without a forced
    // window. The temporary restart mode must not replace the saved setting.
    for(bool fullscreen : {true,false}) {
        for(bool forceWindow : {false,true}) {
            configuredFullscreen=fullscreen;
            events.clear();
            const int startsBefore=deviceStarts, reloadsBefore=imageReloads;
            R_PerformFullVidRestart(forceWindow);
            assert(configuredFullscreen==fullscreen);
            assert(createdFullscreen==(fullscreen && !forceWindow));
            assert(deviceStarts==startsBefore+1 && imageReloads==reloadsBefore+1);
            assert(imagePurges==imageReloads-1 && fontsAlive && deviceAlive);
            assert(Position("targets-shutdown")<Position("fonts-shutdown"));
            assert(Position("fonts-shutdown")<Position("purge"));
            assert(Position("purge")<Position("device-shutdown"));
            assert(Position("device-shutdown")<Position(backend));
            assert(Position(backend)<Position("reload"));
            assert(Position("reload")<Position("fonts-init"));
            assert(Position("fonts-init")<Position("font-atlas"));
#ifdef OPENQ4_RENDERER_VK_MODULE
            assert(Position("vulkan")<Position("materials-init"));
            assert(Position("materials-init")<Position("vertex-cache"));
            assert(Position("vertex-cache")<Position("R_InitFrameData"));
            assert(Position("R_InitFrameData")<Position("reload"));
#endif
        }
    }
    std::printf("%s full restart: native route, resource order, repeated restart, single reload and window preference passed\n",backend);
}
'''

SCREEN = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
struct glimpParms_t { int width=0,height=0,displayHz=0; bool fullScreen=false,borderless=false,hiddenWindow=false; };
using renderWindowParms_t = glimpParms_t;
struct { int vidWidth=640,vidHeight=480; bool isFullscreen=false; } glConfig;
struct { struct { unsigned width=1600,height=900; } swapchainExtent; } vkCtx;
static bool windowOkay=true,swapchainOkay=true;
static int windows=0,swaps=0;
struct Services {
    bool ApplyScreenParms(const renderWindowParms_t* p) {
        ++windows; assert(p->width==1920 && p->height==1080 && p->fullScreen);
        return windowOkay;
    }
} services,*vkBackendServices=nullptr;
static bool VK_Device_RecreateSwapchain() { ++swaps; return swapchainOkay; }
'''

SCREEN_MAIN = r'''
int main() {
    glimpParms_t p; p.width=1920; p.height=1080; p.fullScreen=true;
    assert(!GLimp_SetScreenParms(p) && !windows && !swaps);
    vkBackendServices=&services; windowOkay=false;
    assert(!GLimp_SetScreenParms(p) && windows==1 && !swaps);
    windowOkay=true; swapchainOkay=false;
    assert(!GLimp_SetScreenParms(p) && windows==2 && swaps==1);
    assert(glConfig.vidWidth==640 && glConfig.vidHeight==480 && !glConfig.isFullscreen);
    swapchainOkay=true;
    assert(GLimp_SetScreenParms(p) && windows==3 && swaps==2);
    assert(glConfig.vidWidth==1600 && glConfig.vidHeight==900 && glConfig.isFullscreen);
    std::puts("Vulkan screen apply: window/swapchain failure propagates without publishing stale dimensions");
}
'''

DEVICE_INIT = r'''
#include <cassert>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
static std::vector<std::string> events;
static bool servicesAvailable=true,prepareOkay=true,createOkay=true,deviceOkay=true;
static bool applyOkay=true,recreateOkay=true,preserved=false,deviceAlive=false,windowAlive=false;
static int pixelWidth=1280,pixelHeight=720;
struct glimpParms_t {
    int width,height,displayHz,multiSamples;
    bool fullScreen,borderless,hiddenWindow,stereo;
};
using renderWindowParms_t=glimpParms_t;
struct renderFramebufferDesc_t {
    int surfaceKind,redBits,greenBits,blueBits,alphaBits,depthBits,stencilBits;
    bool doubleBuffer;
};
static const int RENDER_SURFACE_VULKAN=1;
struct renderModuleWindowInfo_t {
    int pixelWidth,pixelHeight,uiViewportX,uiViewportY,uiViewportWidth,uiViewportHeight;
};
struct {
    bool isInitialized=false;
    int uiViewportX=0,uiViewportY=0,uiViewportWidth=0,uiViewportHeight=0;
    const char* renderer_string="test";
    const char* version_string="test";
} glConfig;
struct { struct { uint32_t width=640,height=480; } swapchainExtent; } vkCtx;
struct CVar { bool GetBool() const { return false; } int GetInteger() const { return 0; } };
static CVar r_fullscreen,r_borderless,r_displayRefresh,r_hiddenWindow;
struct Common {
    void Printf(const char*,...) {}
    void Warning(const char*,...) { events.emplace_back("warning"); }
} commonObject,*common=&commonObject;
struct Services {
    bool PrepareWindowSystem() { events.emplace_back("prepare"); return prepareOkay; }
    bool CreateWindowForFramebuffer(const renderFramebufferDesc_t* desc,const renderWindowParms_t* parms,
                                   renderModuleWindowInfo_t*,bool* reused) {
        assert(desc->surfaceKind==RENDER_SURFACE_VULKAN && parms->width==640 && parms->height==480);
        events.emplace_back("window-create"); *reused=preserved;
        if(createOkay)windowAlive=true;
        return createOkay;
    }
    bool ApplyScreenParms(const renderWindowParms_t*) {
        assert(deviceAlive && windowAlive && !glConfig.isInitialized);
        events.emplace_back("window-apply"); return applyOkay;
    }
    void RefreshNativeWindowHandles(renderModuleWindowInfo_t* info) {
        assert(deviceAlive && windowAlive && applyOkay);
        events.emplace_back("refresh"); info->pixelWidth=pixelWidth;info->pixelHeight=pixelHeight;
        info->uiViewportX=11;info->uiViewportY=17;
        info->uiViewportWidth=1000;info->uiViewportHeight=600;
    }
    void DestroyAttemptWindow() {
        assert(!deviceAlive && !glConfig.isInitialized);
        events.emplace_back("window-destroy"); if(!preserved)windowAlive=false;
    }
    void NotifyWindowReady() {
        assert(deviceAlive && windowAlive && glConfig.isInitialized && events.back()=="config");
        events.emplace_back("ready");
    }
} services,*vkBackendServices=nullptr;
static Services* Sys_GetRenderWindowServices() { return servicesAvailable?&services:nullptr; }
static void R_GetInitialWindowSize(bool,int* width,int* height) { *width=640;*height=480; }
static bool VK_Device_Init(Services* value) {
    assert(value==&services && windowAlive && !deviceAlive && !glConfig.isInitialized);
    events.emplace_back("device-init"); deviceAlive=deviceOkay;
    vkCtx.swapchainExtent={640,480}; return deviceOkay;
}
static void VK_Device_Shutdown() {
    assert(deviceAlive && windowAlive && !glConfig.isInitialized);
    events.emplace_back("device-shutdown");deviceAlive=false;
}
static bool VK_Device_RecreateSwapchain() {
    assert(deviceAlive && windowAlive && !glConfig.isInitialized);
    events.emplace_back("recreate");
    // A failed replacement can retire the old chain: keeping the device
    // alive is not evidence that it can submit another frame.
    vkCtx.swapchainExtent={};
    if(recreateOkay)vkCtx.swapchainExtent={static_cast<uint32_t>(pixelWidth),static_cast<uint32_t>(pixelHeight)};
    return recreateOkay;
}
static void VK_FillGLConfigFromDevice() {
    assert(deviceAlive && windowAlive && applyOkay && !glConfig.isInitialized);
    assert(vkCtx.swapchainExtent.width && vkCtx.swapchainExtent.height);
    events.emplace_back("config");
}
static void Sys_InitInput() {
    assert(glConfig.isInitialized && events.back()=="ready");events.emplace_back("input");
}
static void Reset() {
    events.clear(); servicesAvailable=prepareOkay=createOkay=deviceOkay=applyOkay=recreateOkay=true;
    preserved=deviceAlive=windowAlive=false;glConfig={};pixelWidth=1280;pixelHeight=720;
}
static void Expect(std::initializer_list<const char*> expected) {
    std::vector<std::string> names;for(const char* value:expected)names.emplace_back(value);
    assert(events==names);
}
'''

DEVICE_INIT_MAIN = r'''
int main() {
    Reset();servicesAvailable=false;
    assert(!VK_InitRenderDevice());Expect({"warning"});
    Reset();prepareOkay=false;
    assert(!VK_InitRenderDevice());Expect({"prepare","warning"});
    Reset();createOkay=false;
    assert(!VK_InitRenderDevice());Expect({"prepare","window-create","warning"});
    Reset();deviceOkay=false;
    assert(!VK_InitRenderDevice());Expect({"prepare","window-create","device-init","window-destroy"});
    for(bool keepWindow:{false,true}) {
        for(bool failWindow:{false,true}) {
            Reset();preserved=keepWindow;
            applyOkay=!failWindow;recreateOkay=failWindow;
            assert(!VK_InitRenderDevice());
            if(failWindow)Expect({"prepare","window-create","device-init","window-apply","warning","device-shutdown","window-destroy"});
            else Expect({"prepare","window-create","device-init","window-apply","refresh","recreate","warning","device-shutdown","window-destroy"});
            assert(!deviceAlive && !glConfig.isInitialized && windowAlive==keepWindow);
            assert(!glConfig.uiViewportWidth && !glConfig.uiViewportHeight);
            // A clean attempt can follow either refusal without a stale live
            // device, even when the engine preserved its native window.
            events.clear();applyOkay=recreateOkay=true;
            assert(VK_InitRenderDevice());
            Expect({"prepare","window-create","device-init","window-apply","refresh","recreate","config","ready","input"});
            assert(deviceAlive && windowAlive && glConfig.isInitialized);
            assert(glConfig.uiViewportX==11 && glConfig.uiViewportY==17 && glConfig.uiViewportWidth==1000 && glConfig.uiViewportHeight==600);
        }
    }
    Reset();pixelWidth=640;pixelHeight=480;
    assert(VK_InitRenderDevice());
    Expect({"prepare","window-create","device-init","window-apply","refresh","config","ready","input"});
    std::puts("Vulkan startup: screen/swapchain refusal cleans device before attempt window, skips publication/input and permits retry");
}
'''


def main():
    source = (ROOT / 'src/renderer/RenderSystem_init.cpp').read_text(encoding='utf-8')
    code = SUPPORT + '\n'.join(function_body(source, signature) for signature in (
        'static void R_PerformFullVidRestart( bool forceWindow )',
        'void idRenderSystemLocal::InitOpenGL( void )',
    )) + MAIN
    compiler = next((found for name in ('clang++', 'g++', 'c++') if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vid-restart-', dir=ROOT / '.tmp') as temp:
        test_source = Path(temp) / 'restart.cpp'
        test_source.write_text(code, encoding='utf-8')
        for backend in ('opengl', 'vulkan'):
            binary = Path(temp) / f'{backend}.exe'
            define = ['-DOPENQ4_RENDERER_VK_MODULE'] if backend == 'vulkan' else []
            subprocess.run([compiler, '-std=c++17', *define, str(test_source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        backend_source = (ROOT / 'src/renderer/Vulkan/vk_Backend.cpp').read_text(encoding='utf-8')
        screen_source = Path(temp) / 'screen.cpp'
        screen_source.write_text(SCREEN + function_body(backend_source,
            'bool GLimp_SetScreenParms( glimpParms_t parms )') + SCREEN_MAIN, encoding='utf-8')
        binary = Path(temp) / 'screen.exe'
        subprocess.run([compiler, '-std=c++17', str(screen_source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        init_source = Path(temp) / 'device-init.cpp'
        init_source.write_text(DEVICE_INIT + function_body(backend_source,
            'bool VK_InitRenderDevice( void )') + DEVICE_INIT_MAIN, encoding='utf-8')
        binary = Path(temp) / 'device-init.exe'
        subprocess.run([compiler, '-std=c++17', str(init_source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
