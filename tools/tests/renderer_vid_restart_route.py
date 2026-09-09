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


if __name__ == '__main__':
    main()
