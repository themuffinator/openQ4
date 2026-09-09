#!/usr/bin/env python3
"""Exercise native UI preservation through actual GL swap and scene methods.

Extracted production methods run against counted graphics boundaries: no GPU,
window, game, or input is used. This proves routing, extents, and ordering at
those boundaries, not shader pixels or driver behavior. Artifacts are retained
under this checkout's .tmp, including deliberately rejected source mutations.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]


def method(source: str, signature: str) -> str:
    position = 0
    while True:
        position = source.index(signature, position)
        brace = source.index("{", position)
        if source.find(";", position, brace) < 0:
            return function_body(source[position:], signature) + "\n"
        position += len(signature)


SUPPORT = r'''
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
using int64 = long long;
using GLfloat = float;
using GLhandleARB = unsigned;
template<class T> T Min(T a,T b){return std::min(a,b);}
template<class T> T Max(T a,T b){return std::max(a,b);}
struct idMath {
 static int ClampInt(int a,int b,int v){return std::clamp(v,a,b);}
 static float ClampFloat(float a,float b,float v){return std::clamp(v,a,b);}
};
struct Cvar {double value=0;bool GetBool()const{return value!=0;}int GetInteger()const{return int(value);}float GetFloat()const{return float(value);}};
static Cvar r_resolutionScaleMode{1},r_resolutionScaleSharpness{.7},r_showImages,r_finish,r_frontBuffer;
static int requested=85,checks=0,frameCopies=0,quads=0,shaderBinds=0;
static bool validShader=true,preserveDepth=false;
static std::vector<std::string> calls;
static std::array<int,16> backbuffer{};
static std::array<int,4> viewport{},quadExtent{};
static std::array<float,2> inverseSource{};
static float sharpen=-1;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#x<<"\n";std::exit(1);}}while(false)
struct idScreenRect {int x1=0,y1=0,x2=1279,y2=719;};
struct Vec2 {float x=0,y=0;};
struct Name {bool empty=false;int Length()const{return empty?0:1;}};
struct World {Name mapName;};
struct viewDef_t {
 void* viewEntitys=(void*)1;int renderFlags=0;bool isSubview=false,isXraySubview=false;
 viewDef_t* superView=nullptr;void* subviewSurface=nullptr;World* renderWorld=nullptr;
 struct {int viewID=0;} renderView;
 idScreenRect viewport,scissor;Vec2 temporalJitterPixels;bool temporalJitterEnabled=false;
};
constexpr int RF_PORTAL_SKY=1;
static struct {int vidWidth=1280,vidHeight=720,maxTextureSize=8192;bool GLSLProgramAvailable=true;} glConfig;
static struct Common {template<class... T>void Warning(const char*,T...){}} commonObject,*common=&commonObject;
static constexpr int RB_SCREEN_FRACTION_MIN=10,RB_SCREEN_FRACTION_NATIVE=100,RB_SCREEN_FRACTION_MAX=200;
static int rbLastReportedScreenFractionRequest=0,rbLastReportedScreenFractionEffective=0;
struct temporalPresentationFrameState_t {
 bool dynamicResolutionRequested=false,temporalAARequested=false,screenSpace=false;
 int nativeWidth=1280,nativeHeight=720,sceneWidth=1088,sceneHeight=612;
};
static temporalPresentationFrameState_t presentation;
static int R_TemporalPresentation_EffectiveScreenFraction(){return requested;}
static bool R_TemporalPresentation_DynamicResolutionRequested(){return presentation.dynamicResolutionRequested;}
static bool R_TemporalPresentation_TemporalAARequested(){return presentation.temporalAARequested;}
static bool R_TemporalPresentation_ScreenSpaceEffectsRequested(){return presentation.screenSpace;}
static const temporalPresentationFrameState_t& R_TemporalPresentation_GetFrameState(){return presentation;}
struct idImage {
 struct Opts {int width=1088,height=612,numMSAASamples=1;} opts;
 const Opts& GetOpts()const{return opts;}
 void Bind(){}
 void CopyFramebuffer(int,int,int,int){++frameCopies;calls.push_back("copy");}
};
struct idRenderTexture {
 idImage* color=nullptr;int GetNumColorImages()const{return color?1:0;}
 idImage* GetColorImage(int){return color;}
 static void BindNull(){calls.push_back("bind-default");}
};
static idImage sceneImage,copyImage,depthImage;
static idRenderTexture sceneTarget{&sceneImage};
static struct Images {idImage* currentRenderImage=&copyImage;idImage* currentDepthImage=&depthImage;void BindNull(){}} images,*globalImages=&images;
static struct {idRenderTexture* renderTexture=nullptr;viewDef_t* viewDef=nullptr;int frameCount=1;idScreenRect currentScissor;} backEnd;
static struct {int viewportOffset[2]{};bool takingScreenshot=false;} tr;
struct rbSceneScaleState_t {
 bool active=false;int effectivePercent=100,scaledWidth=0,scaledHeight=0;
 idScreenRect nativeViewport,nativeScissor;
};
static bool RB_IsSceneRenderTexture(idRenderTexture* target){return target==&sceneTarget;}
static bool RB_ShouldPreserveSceneRenderTargetFarDepth(const viewDef_t*){return preserveDepth;}
static bool RB_EnsureSceneDepthAwarePresentProgram(){return false;}
static int rbSceneRenderTargetPreserveDepthFrame=-1,rbSceneRenderTargetPreserveDepthWidth=0,rbSceneRenderTargetPreserveDepthHeight=0;
static idImage* rbSceneRenderTargetPreserveDepthImage=nullptr;
static unsigned rbSceneDepthAwarePresentProgram=2;
static int rbSceneDepthAwarePresentSceneLocation=4,rbSceneDepthAwarePresentDepthLocation=5,rbSceneDepthAwarePresentUVOffsetLocation=6;
static void RB_CaptureCurrentRenderImage(int w,int h){copyImage.opts.width=w;copyImage.opts.height=h;copyImage.CopyFramebuffer(0,0,w,h);}
static void RB_CaptureCurrentDepthImage(int,int){}
constexpr int GL_BACK=0,GL_MODULATE=1,GL_TEXTURE_2D=2,GL_TEXTURE_COMPARE_MODE=3,GL_NONE=4,GL_DEPTH_TEXTURE_MODE=5,GL_LUMINANCE=6;
static void glDrawBuffer(int){} static void glReadBuffer(int){} static void glScissor(int,int,int,int){}
static void glViewport(int x,int y,int w,int h){viewport={x,y,w,h};}
static void GL_SelectTexture(int){} static void GL_TexEnv(int){} static void glTexParameteri(int,int,int){}
static void RB_SetFramebufferSRGBEnabled(bool){}
static void RB_BeginFullscreenPostProcessPass(int,int,int,int){}
static void RB_EndFullscreenPostProcessPass(){}
static void RB_DrawFullscreenPostProcessQuad(int w,int h,int tw,int th){++quads;quadExtent={w,h,tw,th};backbuffer.fill(2);calls.push_back("scene-quad");}
static void RB_DrawFullscreenPostProcessQuadOffsetScaled(int w,int h,int tw,int th,float,float){RB_DrawFullscreenPostProcessQuad(w,h,tw,th);}
static void RB_DrawFullscreenPostProcessQuadUnitUV(){++quads;backbuffer.fill(0);calls.push_back("full-frame-filter");}
enum {RB_RES_SCALE_UNIFORM_INV_TEX_SIZE,RB_RES_SCALE_UNIFORM_INV_LOW_RES_SIZE,RB_RES_SCALE_UNIFORM_SHARPEN_AMOUNT};
static struct Shader {unsigned glslProgramObject=1;int shaderTextureLocations[1]{0};int shaderParmLocations[3]{0,1,2};} rbResolutionScaleStage;
static void RB_InitResolutionScaleStage(){}
static bool R_ValidateGLSLProgram(const Shader*){return validShader;}
static void glUseProgramObjectARB(unsigned program){if(program==1)++shaderBinds;}
static void glUniform1iARB(int,int){}
static void glUniform2fvARB(int location,int,const GLfloat* v){if(location==1)inverseSource={v[0],v[1]};}
static void glUniform1fARB(int location,GLfloat v){if(location==2)sharpen=v;}
static void RB_ShowImages(){calls.push_back("debug-images");}
static void glFinish(){calls.push_back("finish");}
static void RB_LogComment(const char*){}
static void RB_ApplyCRTToBackBuffer(){calls.push_back("crt");}
static void RB_ApplyColorMappingsToBackBuffer(){calls.push_back("color");}
static void RB_ForceOpaquePresentAlpha(){calls.push_back("alpha");}
static void GLimp_SwapBuffers(){calls.push_back("present");}
'''

MAIN = r'''
static void Reset(){
 calls.clear();backbuffer.fill(1);viewport={};quadExtent={};inverseSource={};
 frameCopies=quads=shaderBinds=0;sharpen=-1;validShader=true;preserveDepth=false;
 requested=85;presentation={};r_resolutionScaleMode.value=1;r_resolutionScaleSharpness.value=.7;
 r_frontBuffer.value=r_finish.value=r_showImages.value=0;tr.takingScreenshot=false;
 glConfig={};sceneImage={};copyImage={};images={};sceneTarget.color=&sceneImage;
 backEnd.renderTexture=nullptr;backEnd.viewDef=nullptr;
}
static void DrawUi(){for(unsigned i=0;i<backbuffer.size();++i)backbuffer[i]=int(100+i%3);calls.push_back("ui");}
static void UiOnly(){
 for(int mode=0;mode<=3;++mode)for(int fraction:{10,25,85,100,125,200})
 for(int feature=0;feature<4;++feature)for(bool screenshot:{false,true}){
  Reset();requested=fraction;r_resolutionScaleMode.value=mode;tr.takingScreenshot=screenshot;
  presentation.dynamicResolutionRequested=feature==1;presentation.temporalAARequested=feature==2;presentation.screenSpace=feature==3;
  viewDef_t ui;ui.viewEntitys=nullptr;backEnd.viewDef=&ui;
  CHECK(!RB_ScaledSceneTargetRequested(&ui));
  DrawUi();const auto saved=backbuffer;RB_SwapBuffers(nullptr);
  CHECK(backbuffer==saved);CHECK(frameCopies==0);CHECK(quads==0);
  const std::vector<std::string> expected=screenshot?std::vector<std::string>{"ui","crt","color","alpha"}:std::vector<std::string>{"ui","crt","color","alpha","present"};
  CHECK(calls==expected);
 }
 Reset();r_frontBuffer.value=1;DrawUi();RB_SwapBuffers(nullptr);
 CHECK((calls==std::vector<std::string>{"ui","alpha"}));
}
static void SceneScaling(){
 for(int mode=0;mode<=3;++mode)for(int fraction:{25,85,100,125,200}){
  Reset();requested=fraction;r_resolutionScaleMode.value=mode;
  viewDef_t world;backEnd.viewDef=&world;
  int width=-1,height=-2,effective=-3;
  const bool scaled=fraction!=100 && !(mode==0 && fraction<100);
  CHECK(RB_ComputeScaledSceneSize(&world,width,height,&effective)==scaled);
  if(!scaled){CHECK(width==-1&&height==-2&&effective==-3);continue;}
  CHECK(width==1280*fraction/100);CHECK(height==720*fraction/100);CHECK(effective==fraction);
  sceneImage.opts.width=width;sceneImage.opts.height=height;
  backEnd.renderTexture=&sceneTarget;
  rbSceneScaleState_t state;state.active=true;state.effectivePercent=effective;state.scaledWidth=width;state.scaledHeight=height;
  RB_PresentSceneRenderTargetToBackBuffer(state);
  CHECK(quads==1);CHECK(frameCopies==0);CHECK(backEnd.renderTexture==nullptr);
  CHECK((viewport==std::array<int,4>{0,0,1280,720}));
  CHECK((quadExtent==std::array<int,4>{width,height,width,height}));
  if(mode==2 && fraction<100){CHECK(shaderBinds==1);CHECK(std::abs(inverseSource[0]-1.f/width)<1e-7f);CHECK(std::abs(inverseSource[1]-1.f/height)<1e-7f);CHECK(std::abs(sharpen-.7f)<1e-6f);}
  if(mode==1 || fraction>100)CHECK(shaderBinds==0);
  DrawUi();const auto saved=backbuffer;const int savedQuads=quads;RB_SwapBuffers(nullptr);
  CHECK(backbuffer==saved);CHECK(quads==savedQuads);CHECK(frameCopies==0);
  CHECK(std::find(calls.begin(),calls.end(),"scene-quad")<std::find(calls.begin(),calls.end(),"ui"));
 }
}
static void FailureAndFallback(){
 for(int failure=0;failure<5;++failure){
  Reset();viewDef_t world;backEnd.viewDef=&world;backEnd.renderTexture=&sceneTarget;
  rbSceneScaleState_t state;state.active=true;state.effectivePercent=85;state.scaledWidth=1088;state.scaledHeight=612;
  if(failure==0)backEnd.renderTexture=nullptr;
  if(failure==1)backEnd.viewDef=nullptr;
  if(failure==2){sceneTarget.color=nullptr;images.currentRenderImage=nullptr;}
  if(failure==3)sceneImage.opts.width=0;
  if(failure==4)state.scaledHeight=0;
  RB_PresentSceneRenderTargetToBackBuffer(state);CHECK(quads==0);
  DrawUi();const auto saved=backbuffer;RB_SwapBuffers(nullptr);
  CHECK(backbuffer==saved);CHECK(quads==0);CHECK(frameCopies==0);
 }
 // A failed sharpen program still leaves a world-only fixed-function resolve.
 Reset();viewDef_t world;backEnd.viewDef=&world;backEnd.renderTexture=&sceneTarget;
 rbSceneScaleState_t state;state.active=true;state.effectivePercent=85;state.scaledWidth=1088;state.scaledHeight=612;
 validShader=false;r_resolutionScaleMode.value=2;
 RB_PresentSceneRenderTargetToBackBuffer(state);CHECK(quads==1);CHECK(shaderBinds==0);
 DrawUi();const auto saved=backbuffer;RB_SwapBuffers(nullptr);CHECK(backbuffer==saved);CHECK(quads==1);
 // The MSAA scene path copies the scene extent before UI, never the final frame.
 Reset();backEnd.viewDef=&world;backEnd.renderTexture=&sceneTarget;sceneImage.opts.numMSAASamples=4;
 RB_PresentSceneRenderTargetToBackBuffer(state);CHECK(quads==1);CHECK(frameCopies==1);
 CHECK((quadExtent==std::array<int,4>{1088,612,1088,612}));
 DrawUi();const auto msaaSaved=backbuffer;RB_SwapBuffers(nullptr);CHECK(backbuffer==msaaSaved);CHECK(frameCopies==1);
}
static void Eligibility(){
 Reset();viewDef_t view;CHECK(RB_ScaledSceneTargetRequested(&view));
 CHECK(!RB_ScaledSceneTargetRequested(nullptr));
 for(int excluded=0;excluded<9;++excluded){
  view={};World empty;empty.mapName.empty=true;
  switch(excluded){case 0:view.viewEntitys=nullptr;break;case 1:view.renderFlags=RF_PORTAL_SKY;break;
   case 2:view.isSubview=true;break;case 3:view.superView=&view;break;case 4:view.subviewSurface=&view;break;
   case 5:view.renderView.viewID=-1;break;case 6:view.renderWorld=&empty;break;case 7:view.isXraySubview=true;break;
   case 8:view.viewport.x2=639;break;}
  CHECK(!RB_ScaledSceneTargetRequested(&view));
 }
 // Dynamic scene extents are authoritative; the output viewport stays native.
 Reset();view={};presentation.dynamicResolutionRequested=true;presentation.sceneWidth=960;presentation.sceneHeight=544;
 int w=0,h=0;CHECK(RB_ComputeScaledSceneSize(&view,w,h));CHECK(w==960&&h==544);
 // Mode0's exception remains limited to static below-native crop testing.
 for(int feature=1;feature<4;++feature){Reset();r_resolutionScaleMode.value=0;
  presentation.dynamicResolutionRequested=feature==1;presentation.temporalAARequested=feature==2;presentation.screenSpace=feature==3;
  CHECK(RB_ScaledSceneTargetRequested(&view));}
 // Supersampling cannot exceed the texture limit.
 Reset();requested=200;glConfig.maxTextureSize=1920;
 int effective=0;CHECK(RB_ComputeScaledSceneSize(&view,w,h,&effective));CHECK(effective==150&&w==1920&&h==1080);
}
int main(){UiOnly();SceneScaling();FailureAndFallback();Eligibility();std::cout<<"PASS "<<checks<<" production-method checks\n";}
'''


def assemble(draw: str, backend: str) -> str:
    signatures = (
        "static bool RB_IsMainScenePostProcessView( const viewDef_t *viewDef )",
        "static int RB_RequestedScreenFraction( void )",
        "static bool RB_ViewCoversBackBuffer( const viewDef_t *viewDef )",
        "static int RB_MaxSceneScaleDimension( void )",
        "static int RB_EffectiveScreenFractionForView( const viewDef_t *viewDef )",
        "static int RB_ScaledDimension( int nativeDimension, int scalePercent )",
        "static bool RB_ScaledSceneTargetRequested( const viewDef_t *viewDef )",
        "static bool RB_ComputeScaledSceneSize( const viewDef_t *viewDef, int &targetWidth,",
        "static bool RB_BindSceneScaleSharpenProgram( int sourceWidth, int sourceHeight,",
        "static void RB_PresentSceneRenderTargetToBackBuffer( const rbSceneScaleState_t &scaleState )",
        "void RB_ApplyResolutionScaleToBackBuffer( void )",
    )
    return SUPPORT + "\n".join(method(draw, s) for s in signatures) + method(
        backend, "const void\tRB_SwapBuffers( const void *data )"
    ) + MAIN


def replace_once(source: str, before: str, after: str) -> str:
    if source.count(before) != 1:
        raise AssertionError(f"mutation anchor not unique: {before}")
    return source.replace(before, after, 1)


def main() -> None:
    source_paths = [ROOT / "src/renderer" / name for name in (
        "draw_common.cpp", "tr_backend.cpp", "RenderSystem.cpp", "GLES/gles_Backend.cpp"
    )]
    snapshots = {str(p.relative_to(ROOT)): p.read_bytes() for p in source_paths}
    # Hash the exact checkout bytes, but normalize C++ source newlines before
    # extraction. Windows Git checkouts use CRLF; mutation anchors and support
    # strings use LF. Their concatenation otherwise produces mixed newlines.
    draw, backend, render, gles = (
        snapshots[str(p.relative_to(ROOT))].decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")
        for p in source_paths
    )
    # Verify production DrawView still owns the scene resolve after restoring
    # native coordinates. Runtime tests below invoke those complete presenters.
    view = method(draw, "void\tRB_STD_DrawView( void )")
    if not (view.index("RB_BeginDrawingView();") < view.index("RB_RestoreSceneScaling( sceneScaleState );") < view.index("RB_PresentSceneRenderTargetToBackBuffer( sceneScaleState );")):
        raise AssertionError("world resolve must remain after drawing and native-coordinate restoration")
    crop = method(render, "void idRenderSystemLocal::BeginFrame(")
    for required in ("screenFraction < 100 && r_resolutionScaleMode.GetInteger() == 0", "CropRenderSize( w, h );"):
        if required not in crop:
            raise AssertionError("explicit legacy mode0 crop was changed")
    method(gles, "void RB_ApplyResolutionScaleToBackBuffer( void )")
    code = assemble(draw, backend)
    variants = {"production": code}
    unsafe = "void RB_ApplyResolutionScaleToBackBuffer( void ) {\n if (RB_RequestedScreenFraction()<100 && r_resolutionScaleMode.GetInteger()!=0) { globalImages->currentRenderImage->CopyFramebuffer(0,0,1280,720); RB_DrawFullscreenPostProcessQuadUnitUV(); }\n}"
    variants["mutant-final-frame-filter"] = assemble(replace_once(draw, method(draw, "void RB_ApplyResolutionScaleToBackBuffer( void )").rstrip(), unsafe), backend)
    variants["mutant-disable-world-scaling"] = code.replace("static bool RB_ScaledSceneTargetRequested( const viewDef_t *viewDef ) {", "static bool RB_ScaledSceneTargetRequested( const viewDef_t *viewDef ) { return false;", 1)
    variants["mutant-lose-mode0-policy"] = replace_once(code, "&& r_resolutionScaleMode.GetInteger() == 0", "&& false")
    variants["mutant-wrong-scene-size"] = replace_once(code, "targetWidth = RB_ScaledDimension( nativeWidth, scalePercent );", "targetWidth = nativeWidth;")
    variants["mutant-disable-world-present"] = code.replace("static void RB_PresentSceneRenderTargetToBackBuffer( const rbSceneScaleState_t &scaleState ) {", "static void RB_PresentSceneRenderTargetToBackBuffer( const rbSceneScaleState_t &scaleState ) { return;", 1)
    variants["mutant-scaled-output-viewport"] = replace_once(code, "targetViewportWidth,\n\t\ttargetViewportHeight );", "sourceViewportWidth,\n\t\tsourceViewportHeight );")
    variants["mutant-disable-sharpen"] = code.replace("static bool RB_BindSceneScaleSharpenProgram( int sourceWidth, int sourceHeight,\n\t\tint textureWidth, int textureHeight ) {", "static bool RB_BindSceneScaleSharpenProgram( int sourceWidth, int sourceHeight,\n\t\tint textureWidth, int textureHeight ) { return false;", 1)
    if len(set(variants.values())) != len(variants):
        raise AssertionError("ineffective production mutation")
    compiler = next((found for name in ("clang++", "g++", "c++") if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("C++ compiler required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="native-ui-output-", dir=ROOT / ".tmp"))
    env = dict(os.environ, TEMP=str(output), TMP=str(output), TMPDIR=str(output))
    results = {}
    for name, source in variants.items():
        cpp, exe = output / f"{name}.cpp", output / f"{name}.exe"
        cpp.write_text(source, encoding="utf-8")
        build = subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(exe)], env=env, capture_output=True, text=True)
        (output / f"{name}-compile.log").write_text(build.stdout + build.stderr, encoding="utf-8")
        if build.returncode:
            raise AssertionError(f"{name} failed to compile; see {output}")
        run = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
        (output / f"{name}-run.log").write_text(run.stdout + run.stderr, encoding="utf-8")
        if (run.returncode == 0) != (name == "production"):
            raise AssertionError(f"{name} returned unexpected result: {run.returncode}; see {output}")
        artifacts = [cpp, exe, output / f"{name}-compile.log", output / f"{name}-run.log"]
        results[name] = {"compile_exit": build.returncode, "run_exit": run.returncode, "output": run.stdout + run.stderr,
                         "artifacts": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in artifacts}}
    for path in source_paths:
        if path.read_bytes() != snapshots[str(path.relative_to(ROOT))]:
            raise AssertionError(f"source changed during test: {path}")
    report = {"passed": True, "compiler": compiler, "test_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "sources": {name: hashlib.sha256(data).hexdigest() for name, data in snapshots.items()}, "results": results,
              "limits": "Production method routing and graphics call contracts with doubles; no engine, driver or raster qualification."}
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(results["production"]["output"].strip())
    print(f"Rejected {len(results)-1} compiled source mutations; evidence: {output / 'result.json'}")


if __name__ == "__main__":
    main()
