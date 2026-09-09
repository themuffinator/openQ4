#!/usr/bin/env python3
"""Exercise production display-service methods with ABI PODs and host doubles.

No window, graphics device, process, input or console command is opened. The
actual loader/probe methods are extracted; only their platform/backend edges
are stand-ins. Covers builtin, module-only and dedicated compilation branches.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include "src/renderer/RendererModule.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
class idRenderSystem {
public:
    bool ready=true;int restart=7;
    bool IsOpenGLRunning(){return ready;}
    int GetVideoRestartCount(){return restart;}
} builtinRenderer,moduleRenderer,*renderSystem=&builtinRenderer;
class idRenderModelManager {} builtinModels,moduleModels,*renderModelManager=&builtinModels;
struct idStr {
    static int Icmp(const char* a,const char* b){
        while(*a && std::tolower(static_cast<unsigned char>(*a))==std::tolower(static_cast<unsigned char>(*b))){++a;++b;}
        return std::tolower(static_cast<unsigned char>(*a))-std::tolower(static_cast<unsigned char>(*b));
    }
    static void Copynz(char* output,const char* text,int size){
        if(size<=0)return;int i=0;for(;i<size-1 && text[i];++i)output[i]=text[i];output[i]=0;
    }
};
class idCmdArgs {
    std::vector<std::string> values;
public:
    idCmdArgs(std::initializer_list<const char*> args){for(const char* value:args)values.emplace_back(value);}
    int Argc()const{return static_cast<int>(values.size());}
    const char* Argv(int index)const{return index>=0 && index<Argc()?values[index].c_str():"";}
};
static std::vector<std::string> logs;
class idCommon {
public:
    void Printf(const char* format,...){
        char line[4096];va_list args;va_start(args,format);std::vsnprintf(line,sizeof(line),format,args);va_end(args);logs.emplace_back(line);
    }
} commonObject,*common=&commonObject;
struct State {
    rendererModuleStatus_t status={};renderExport_t moduleExport={};
    bool moduleExportValid=false,interfacesPublished=false;
    idRenderSystem* savedRenderSystem=nullptr;idRenderModelManager* savedRenderModelManager=nullptr;
} rm_state;
static uint64_t rm_displayModuleEpoch=1;
static bool rm_displayProbeSaved=false;
static renderWindowRequest_t rm_displayProbeRestore={};
static uint64_t rm_displayProbeEpoch=0;
static renderDisplayPresentation_t presentation={};
static renderWindowState_t window={};
static renderWindowRequest_t submitted={};
static renderWindowServices_t windowServices={};
static const renderWindowServices_t* rm_displayVideoPin=nullptr;
static bool servicesAvailable=true,windowAvailable=true,restartOkay=true,queryRace=false,epochRace=false,throwBackend=false;
static bool retainOkay=true,destroyDeviceOnFailure=false;
static int moduleQueries=0,builtinQueries=0,moduleRestarts=0,builtinRestarts=0;
static int videoRetains=0,videoReleases=0,videoReferences=0;
static std::vector<std::string> lifetime;
static void RM_RestorePublishedInterfaces();
static void RM_UnloadModule(){lifetime.push_back("unload");RM_RestorePublishedInterfaces();rm_state.moduleExportValid=false;rm_state.moduleExport={};}
const renderWindowServices_t* Sys_GetRenderWindowServices(){return servicesAvailable?&windowServices:nullptr;}
static bool RetainVideo(){++videoRetains;lifetime.push_back("retain");if(!retainOkay)return false;++videoReferences;return true;}
static void ReleaseVideo(){assert(videoReferences==1);--videoReferences;++videoReleases;lifetime.push_back("release");}
static bool QueryWindow(renderWindowState_t* output){
    if(queryRace)++presentation.generation;
    if(epochRace)++rm_displayModuleEpoch;
    if(!windowAvailable)return false;*output=window;return true;
}
static bool ApplyWindow(const renderWindowRequest_t*,renderWindowState_t*,char*,int){assert(false);return false;}
static void ModuleQuery(renderDisplayPresentation_t* output){
    if(throwBackend)throw std::runtime_error("backend failure");++moduleQueries;*output=presentation;
}
void R_GetDisplayPresentation(renderDisplayPresentation_t* output){++builtinQueries;*output=presentation;}
static bool ModuleRestart(const renderWindowRequest_t* request,char* error,int size){
    assert(rm_displayVideoPin==&windowServices && videoReferences==1);lifetime.push_back("restart");
    if(throwBackend)throw std::runtime_error("backend failure");++moduleRestarts;submitted=*request;
    if(restartOkay)renderSystem->ready=true;else if(destroyDeviceOnFailure)renderSystem->ready=false;
    if(!restartOkay)idStr::Copynz(error,"module refused",size);return restartOkay;
}
bool R_TryFullVidRestart(const renderWindowRequest_t* request,char* error,int size){
    assert(rm_displayVideoPin==&windowServices && videoReferences==1);lifetime.push_back("restart");
    ++builtinRestarts;submitted=*request;if(!restartOkay)idStr::Copynz(error,"builtin refused",size);return restartOkay;
}
static renderExport_t Export(){
    renderExport_t result={};result.version=RENDER_API_VERSION;result.backendName="test";
    result.renderSystem=&moduleRenderer;result.renderModelManager=&moduleModels;
    result.TryDeviceRestart=ModuleRestart;result.GetDisplayPresentation=ModuleQuery;return result;
}
static void Reset(){
    assert(!rm_displayVideoPin && !videoReferences);
    rm_state={};rm_displayModuleEpoch=1;rm_displayProbeSaved=false;rm_displayProbeRestore={};rm_displayProbeEpoch=0;
    renderSystem=&builtinRenderer;renderModelManager=&builtinModels;builtinRenderer={};moduleRenderer={};
    servicesAvailable=windowAvailable=restartOkay=true;queryRace=epochRace=throwBackend=false;
    retainOkay=true;destroyDeviceOnFailure=false;videoRetains=videoReleases=0;lifetime.clear();
    moduleQueries=builtinQueries=moduleRestarts=builtinRestarts=0;logs.clear();submitted={};
    windowServices={};windowServices.QueryWindowState=QueryWindow;windowServices.ApplyScreenParmsStrict=ApplyWindow;
    windowServices.RetainVideoSystem=RetainVideo;windowServices.ReleaseVideoSystem=ReleaseVideo;
    window={};window.hidden=true;window.logicalWidth=1024;window.logicalHeight=768;window.pixelWidth=2048;window.pixelHeight=1536;
    window.displayId=42;window.displayIndex=2;window.positionValid=true;window.windowX=13;window.windowY=29;window.maximized=true;
    presentation={};presentation.generation=11;presentation.available=1;presentation.outcome=RDP_PRESENTED;
    presentation.samples=4;presentation.swapInterval=1;presentation.parametersValid=RDP_PARAMETER_SAMPLES|RDP_PARAMETER_SWAP_INTERVAL;
    presentation.submittedSequence=10;presentation.presentedSequence=9;
}
static void Activate();
'''

MAIN = r'''
static void Activate(){rm_state.moduleExport=Export();rm_state.moduleExportValid=true;rm_state.status.disposition=RENDER_MODULE_DISPOSITION_MODULE;RM_PublishActiveModuleInterfaces(rm_state.moduleExport);}
static bool Probe(std::initializer_list<const char*> args){
    logs.clear();R_RendererDisplayProbe_f(idCmdArgs(args));
    const auto line=std::find_if(logs.begin(),logs.end(),[](const auto& text){return text.rfind("DISPLAY_PROBE operation=",0)==0;});
    assert(line!=logs.end());return line->find(" result=1 ")!=std::string::npos;
}
static void Activation(){
    Reset();const char* reason="";auto table=Export();
    assert(RM_ExportCanRender(&table,&reason));assert(!RM_ExportCanRender(nullptr,&reason));
    --table.version;assert(!RM_ExportCanRender(&table,&reason));table=Export();table.backendName="";assert(!RM_ExportCanRender(&table,&reason));
    table=Export();table.renderSystem=nullptr;assert(!RM_ExportCanRender(&table,&reason));
    table=Export();table.TryDeviceRestart=nullptr;assert(!RM_ExportCanRender(&table,&reason));
    table=Export();table.GetDisplayPresentation=nullptr;assert(!RM_ExportCanRender(&table,&reason));
    Activate();assert(rm_displayModuleEpoch==2 && renderSystem==&moduleRenderer && renderModelManager==&moduleModels);
    RM_RestorePublishedInterfaces();assert(rm_displayModuleEpoch==3 && renderSystem==&builtinRenderer && renderModelManager==&builtinModels);
    RM_RestorePublishedInterfaces();assert(rm_displayModuleEpoch==3);
    R_RendererModule_Shutdown();assert(rm_displayModuleEpoch==4 && rm_state.status.disposition==RENDER_MODULE_DISPOSITION_NONE);
    Activate();assert(rm_displayModuleEpoch==5);R_RendererModule_Shutdown();assert(rm_displayModuleEpoch==7);
    rm_displayModuleEpoch=UINT64_MAX;RM_AdvanceDisplayEpoch();assert(rm_displayModuleEpoch==0);RM_AdvanceDisplayEpoch();assert(rm_displayModuleEpoch==0);
    Activate();rendererDisplayState_t result={};result.moduleEpoch=777;
    assert(!R_RendererModule_QueryDisplay(&result) && result.moduleEpoch==777);
}
static void Query(){
    Reset();rendererDisplayState_t output;std::memset(&output,0x5a,sizeof(output));const auto sentinel=output;
    assert(!R_RendererModule_QueryDisplay(nullptr));assert(!R_RendererModule_QueryDisplay(&output));assert(!std::memcmp(&output,&sentinel,sizeof(output)));
    Activate();assert(R_RendererModule_QueryDisplay(&output));assert(output.moduleEpoch==2 && output.rendererReady && output.windowValid);
    assert(output.window.logicalWidth==1024 && output.window.pixelWidth==2048 && output.presentation.presentedSequence==9 && output.videoRestartCount==7);
    assert(moduleQueries==2 && !builtinQueries);
    output=sentinel;queryRace=true;assert(!R_RendererModule_QueryDisplay(&output));assert(!std::memcmp(&output,&sentinel,sizeof(output)));queryRace=false;
    epochRace=true;assert(!R_RendererModule_QueryDisplay(&output));assert(!std::memcmp(&output,&sentinel,sizeof(output)));epochRace=false;
    moduleRenderer.ready=false;windowAvailable=false;presentation.available=0;presentation.outcome=RDP_INIT_FAILED;
    assert(R_RendererModule_QueryDisplay(&output) && !output.rendererReady && !output.windowValid && output.presentation.outcome==RDP_INIT_FAILED);
    throwBackend=true;output=sentinel;bool threw=false;try{R_RendererModule_QueryDisplay(&output);}catch(const std::runtime_error&){threw=true;}
    assert(threw && !std::memcmp(&output,&sentinel,sizeof(output)));throwBackend=false;
    Reset();rm_state.status.disposition=RENDER_MODULE_DISPOSITION_BUILTIN;
#if !defined(OPENQ4_RENDERER_MODULE_ONLY) && !defined(ID_DEDICATED)
    assert(R_RendererModule_QueryDisplay(&output) && builtinQueries==2 && !moduleQueries);
#else
    assert(!R_RendererModule_QueryDisplay(&output) && !builtinQueries && !moduleQueries);
#endif
}
static void Dispatch(){
    Reset();Activate();renderWindowRequest_t request={};request.parms.width=800;request.parms.height=600;request.displayId=42;
    char error[64]="sentinel";assert(R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && !*error);
    assert(moduleRestarts==1 && !builtinRestarts && submitted.parms.width==800 && submitted.displayId==42);
    restartOkay=false;assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && std::string(error)=="module refused");
    assert(moduleRestarts==2 && !builtinRestarts);
    servicesAvailable=false;assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && moduleRestarts==2);servicesAvailable=true;
    windowServices.QueryWindowState=nullptr;assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && moduleRestarts==2);windowServices.QueryWindowState=QueryWindow;
    windowServices.ApplyScreenParmsStrict=nullptr;assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && moduleRestarts==2);windowServices.ApplyScreenParmsStrict=ApplyWindow;
    windowServices.RetainVideoSystem=nullptr;assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && moduleRestarts==2);windowServices.RetainVideoSystem=RetainVideo;
    windowServices.ReleaseVideoSystem=nullptr;assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && moduleRestarts==2);windowServices.ReleaseVideoSystem=ReleaseVideo;
    assert(!R_RendererModule_TryDeviceRestart(nullptr,error,sizeof(error)) && moduleRestarts==2);
    throwBackend=true;bool threw=false;try{R_RendererModule_TryDeviceRestart(&request,error,sizeof(error));}catch(const std::runtime_error&){threw=true;}
    assert(threw && !builtinRestarts);throwBackend=false;
    // Unrecoverable backend exceptions still propagate. Engine shutdown owns
    // the remaining lease rather than hiding the failure as a usable device.
    assert(videoReferences==1);R_RendererModule_Shutdown();assert(!videoReferences && !rm_displayVideoPin);
    Reset();rm_state.status.disposition=RENDER_MODULE_DISPOSITION_BUILTIN;
#if !defined(OPENQ4_RENDERER_MODULE_ONLY) && !defined(ID_DEDICATED)
    assert(R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && builtinRestarts==1);
#else
    assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && !builtinRestarts);
    assert(!videoRetains && !videoReleases && !rm_displayVideoPin);
#endif
}
static void VideoLifetime(){
    Reset();Activate();renderWindowRequest_t request={};request.parms.width=800;request.parms.height=600;char error[80];
    retainOkay=false;
    assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && !moduleRestarts);
    assert(videoRetains==1 && !videoReferences && !videoReleases && !rm_displayVideoPin);
    retainOkay=true;lifetime.clear();
    assert(R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)));
    assert(videoRetains==2 && videoReleases==1 && !videoReferences && !rm_displayVideoPin);
    assert((lifetime==std::vector<std::string>{"retain","restart","release"}));
    // A preflight refusal with an existing live device needs no retained lease.
    restartOkay=false;
    assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)));
    assert(moduleRenderer.ready && videoRetains==3 && videoReleases==2 && !rm_displayVideoPin);
    // After context destruction, retries share one pin. Unavailable services
    // or a malformed request must not discard the display identity to restore.
    destroyDeviceOnFailure=true;lifetime.clear();
    assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)));
    assert(!moduleRenderer.ready && videoRetains==4 && videoReleases==2 && videoReferences==1);
    assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && videoRetains==4);
    assert(!R_RendererModule_TryDeviceRestart(nullptr,error,sizeof(error)) && videoReferences==1);
    servicesAvailable=false;assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && videoReferences==1);servicesAvailable=true;
    restartOkay=true;
    assert(R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && moduleRenderer.ready);
    assert(videoRetains==4 && videoReleases==3 && !videoReferences && !rm_displayVideoPin);
    assert((lifetime==std::vector<std::string>{"retain","restart","restart","restart","release"}));
    restartOkay=false;
    assert(!R_RendererModule_TryDeviceRestart(&request,error,sizeof(error)) && videoReferences==1);
    lifetime.clear();R_RendererModule_Shutdown();
    assert(!videoReferences && !rm_displayVideoPin && videoRetains==5 && videoReleases==4);
    assert((lifetime==std::vector<std::string>{"unload","release"}));
    R_RendererModule_Shutdown();assert(videoReleases==4);
}
static void BoundedProbe(){
    Reset();Activate();assert(Probe({"rendererDisplayProbe"}));assert(Probe({"rendererDisplayProbe","REPORT"}));
    assert(!Probe({"rendererDisplayProbe","report","extra"}));assert(!Probe({"rendererDisplayProbe","unknown"}));
    assert(!Probe({"rendererDisplayProbe","apply","800","600"}) && !moduleRestarts);
    window.hidden=false;assert(!Probe({"rendererDisplayProbe","save"}));window.hidden=true;
    window.fullscreen=true;assert(!Probe({"rendererDisplayProbe","save"}));window.fullscreen=false;
    window.minimized=true;assert(!Probe({"rendererDisplayProbe","save"}));window.minimized=false;
    presentation.parametersValid=0;assert(!Probe({"rendererDisplayProbe","save"}));presentation.parametersValid=3;
    moduleRenderer.ready=false;assert(!Probe({"rendererDisplayProbe","save"}));moduleRenderer.ready=true;
    assert(!Probe({"rendererDisplayProbe","save","extra"}));assert(Probe({"rendererDisplayProbe","save"}));
    assert(rm_displayProbeRestore.parms.width==1024 && rm_displayProbeRestore.parms.height==768);
    assert(rm_displayProbeRestore.parms.multiSamples==4 && rm_displayProbeRestore.swapInterval==1);
    assert(rm_displayProbeRestore.displayId==42 && rm_displayProbeRestore.displayIndex==2 && rm_displayProbeRestore.maximized);
    assert(rm_displayProbeRestore.restorePlacement && rm_displayProbeRestore.windowX==13 && rm_displayProbeRestore.windowY==29);
    for(const char* malformed:{"","800junk","800 ","+800","-800","999999999999999999999999","0","319","16385"}){
        int dimension=42;assert(!RM_ParseProbeDimension(malformed,320,16384,dimension) && dimension==42);
        assert(!Probe({"rendererDisplayProbe","apply",malformed,"600"}) && !moduleRestarts);
    }
    assert(!Probe({"rendererDisplayProbe","apply","800","239"}) && !moduleRestarts);
    assert(!Probe({"rendererDisplayProbe","apply","800"}) && !moduleRestarts);
    for(const char* interval:{"","-1","2","1junk","0 ","9999999999999999999999"}){
        assert(!Probe({"rendererDisplayProbe","apply","800","600",interval}) && !moduleRestarts);
    }
    assert(!Probe({"rendererDisplayProbe","apply","800","600","0","extra"}) && !moduleRestarts);
    for(const char* samples:{"-1","1","3","17","4junk","9999999999999999999999"}){
        assert(!Probe({"rendererDisplayProbe","apply","800","600","0",samples}) && !moduleRestarts);
    }
    assert(!Probe({"rendererDisplayProbe","apply","800","600","0","4","extra"}) && !moduleRestarts);
    assert(Probe({"rendererDisplayProbe","apply","800","600"}) && moduleRestarts==1);
    assert(submitted.parms.width==800 && submitted.parms.height==600 && submitted.parms.hiddenWindow && !submitted.parms.fullScreen && !submitted.maximized);
    window.logicalWidth=800;window.logicalHeight=600;window.maximized=false;
    assert(Probe({"rendererDisplayProbe","restore"}) && moduleRestarts==2);
    assert(submitted.parms.width==1024 && submitted.parms.height==768 && submitted.maximized);
    assert(Probe({"rendererDisplayProbe","missing-display"}) && submitted.displayId==UINT32_MAX);
    assert(Probe({"rendererDisplayProbe","apply","800","600","0"}) && submitted.swapInterval==0);
    assert(Probe({"rendererDisplayProbe","apply","800","600","1"}) && submitted.swapInterval==1);
    for(const char* samples:{"0","2","4","8","16"}){
        assert(Probe({"rendererDisplayProbe","apply","800","600","0",samples}) && submitted.parms.multiSamples==std::atoi(samples));
    }
    assert(Probe({"rendererDisplayProbe","restore"}) && submitted.swapInterval==1);
    const int calls=moduleRestarts;
    for(const char* operation:{"apply","restore","missing-display"}){
        const auto args=std::string(operation)=="apply"?idCmdArgs({"rendererDisplayProbe",operation,"800","600"}):idCmdArgs({"rendererDisplayProbe",operation});
        window.hidden=false;R_RendererDisplayProbe_f(args);window.hidden=true;
        window.fullscreen=true;R_RendererDisplayProbe_f(args);window.fullscreen=false;
        window.minimized=true;R_RendererDisplayProbe_f(args);window.minimized=false;
        assert(moduleRestarts==calls);
    }
    queryRace=true;assert(!Probe({"rendererDisplayProbe","restore"}) && moduleRestarts==calls);queryRace=false;
    windowAvailable=false;moduleRenderer.ready=true;assert(!Probe({"rendererDisplayProbe","restore"}));
    moduleRenderer.ready=false;assert(!Probe({"rendererDisplayProbe","apply","800","600"}));assert(!Probe({"rendererDisplayProbe","missing-display"}));
    assert(Probe({"rendererDisplayProbe","restore"}) && moduleRestarts==calls+1);
    RM_AdvanceDisplayEpoch();assert(!Probe({"rendererDisplayProbe","restore"}) && moduleRestarts==calls+1);
}
int main(){Activation();Query();Dispatch();VideoLifetime();BoundedProbe();std::puts("Renderer display services: ABI activation, coherent observation, identity exhaustion, exact backend dispatch, balanced video lease and bounded actual-state probe passed");}
'''


def main():
    source = (ROOT / 'src/renderer/RendererModule.cpp').read_text(encoding='utf-8')
    signatures = (
        'static void RM_ReleaseDisplayVideoPin( void )',
        'static void RM_AdvanceDisplayEpoch( void )',
        'static void RM_PublishActiveModuleInterfaces( const renderExport_t &moduleExport )',
        'static void RM_RestorePublishedInterfaces( void )',
        'static bool RM_ValidateExport( const renderExport_t *moduleExport, bool &diagnosticsOnly, const char **reason )',
        'static bool RM_ExportCanRender( const renderExport_t *moduleExport, const char **reason )',
        'void R_RendererModule_Shutdown( void )',
        'bool R_RendererModule_QueryDisplay( rendererDisplayState_t *outState )',
        'bool R_RendererModule_TryDeviceRestart( const renderWindowRequest_t *request, char *error, int errorSize )',
        'static bool RM_ParseProbeDimension( const char *text, int minimum, int maximum, int &value )',
        'static void R_RendererDisplayProbe_f( const idCmdArgs &args )',
    )
    code = SUPPORT + '\n'.join(function_body(source, signature) for signature in signatures) + MAIN
    compiler = next((found for name in ('clang++', 'g++', 'c++') if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='display-services-', dir=ROOT / '.tmp') as temp:
        test_source = Path(temp) / 'services.cpp'
        test_source.write_text(code, encoding='utf-8')
        for name, defines in (('builtin', []), ('module-only', ['-DOPENQ4_RENDERER_MODULE_ONLY']), ('dedicated', ['-DID_DEDICATED'])):
            binary = Path(temp) / f'{name}.exe'
            subprocess.run([compiler, '-std=c++17', *defines, '-I', str(ROOT), str(test_source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
