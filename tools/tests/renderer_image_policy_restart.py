#!/usr/bin/env python3
"""Compile the actual checked-resource coordinator against counted engine/GPU boundaries.

No game, window, GPU or input. Both backend branches execute; a driver/device
receipt and rendered owner frame remain integration qualifications.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include "RendererResourceSettings.h"
#include "DisplayPresentation.h"
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <new>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
static int allocationBudget=-1,allocationCalls=0;
void* operator new(std::size_t n){++allocationCalls;if(allocationBudget==0)throw std::bad_alloc();if(allocationBudget>0)--allocationBudget;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static int checks=0;
#define TEST(x) do { ++checks; if (!(x)) throw std::runtime_error(#x); } while (0)
template<class T> T Max(T a,T b){return std::max(a,b);}
struct idStr {
 static void Copynz(char* o,const char* s,int n){if(n>0){std::snprintf(o,n,"%s",s);}}
 static void snPrintf(char* o,int n,const char* f,...){va_list a;va_start(a,f);std::vsnprintf(o,n,f,a);va_end(a);}
 static int Icmpn(const char* a,const char* b,int n){for(int i=0;i<n;++i){const int x=std::tolower(static_cast<unsigned char>(a[i]));const int y=std::tolower(static_cast<unsigned char>(b[i]));if(x!=y||!x||!y)return x-y;}return 0;}
};
struct idCVar { int value=0; bool modified=true;std::string text="0";const char* GetString()const{return text.c_str();} int GetInteger() const{return value;} void ClearModified(){modified=false;} };
idCVar image_downSize,image_downSizeLimit,image_downSizeSpecular,image_downSizeSpecularLimit;
idCVar image_downSizeBump,image_downSizeBumpLimit,image_usePrecompressedTextures,image_ignoreHighQuality;
static std::function<void(const char*)> callback;
static bool packed=false, allocationFailure=false, uploadFailure=false, missingMip=false;
static bool newDefault=false, newMaterialDefault=false, context=true, oldPending=false, finalPending=false;
static int starts=0, teardowns=0, finishes=0, reloads=0, parses=0, publications=0;
static void Boundary(const char* name){if(callback)callback(name);}
static idCVar dependency;
struct Cvars {const idCVar* Find(const char*){Boundary("dependency");return &dependency;} bool GetCVarBool(const char*){Boundary("packed");return packed;}} cvars;
static Cvars* cvarSystem=&cvars;
enum {TT_2D,TT_CUBIC};
struct idImageOpts {int width=8,height=8,numLevels=4,textureType=TT_2D;};
struct idImage {
 uint64_t identity=R_ImagePolicyNewResourceIdentity();uint64_t GetImagePolicyIdentity()const{return identity;}
 idImageOpts opts;bool loaded=true,defaulted=false,file=true;uint64_t storage=1;
 bool IsLoaded()const{return loaded;}bool IsDefaulted()const{return defaulted;}bool IsFileBacked()const{return file;}
 uint64_t GetStorageGeneration()const{return storage;}const idImageOpts& GetOpts()const{return opts;}
 void Reload(bool){
  ++reloads;
  {renderImageOperation_t op(this);if(!op.Allowed())return;loaded=false;++storage;Boundary("allocate");if(allocationFailure)return;loaded=true;op.Succeeded();}
  defaulted=defaulted||newDefault;
  for(int side=0;side<(opts.textureType==TT_CUBIC?6:1);++side)for(int level=0;level<opts.numLevels;++level){
   if(missingMip && level==opts.numLevels-1)continue;
   renderImageOperation_t op(this,true,level,side,Max(1,opts.width>>level),Max(1,opts.height>>level));
   if(!op.Allowed())return;Boundary("upload");if(uploadFailure)return;op.Succeeded();
  }
 }
};
template<class T>struct List:std::vector<T>{int Num()const{return static_cast<int>(this->size());}};
struct Images {
 List<idImage*> images;bool insideLevelLoad=false,preloadingMapImages=false;
 void ClearCheckedImagePolicyChanges(){++publications;image_downSize.ClearModified();}
} imageManager;
static Images* globalImages=&imageManager;
enum {DECL_MATERIAL,DS_UNPARSED,DS_DEFAULTED,DS_PARSED};
struct idDecl {virtual ~idDecl()=default;virtual int Index()const=0;virtual int GetLineNum()const=0;virtual const char* GetName()const=0;virtual const char* GetFileName()const=0;virtual int GetTextLength()const=0;virtual void GetText(char*)const=0;};
struct newShaderStage_t{int numFragmentProgramImages=0,numShaderTextures=0;idImage* fragmentProgramImages[4]{};idImage* shaderTextureImages[4]{};};
struct shaderStage_t {struct {idImage* image=nullptr;}texture;newShaderStage_t* newStage=nullptr;};
struct pbrMaterialTexture_t {bool present=false;idImage* image=nullptr;};
struct pbrMaterialInfo_t {pbrMaterialTexture_t albedo,normal,orm,metallic,roughness,ao,emissive,legacyBump,legacyDiffuse,legacySpecular,legacyEmissive;};
struct specularProbeMaterialInfo_t {idImage* cubeImage=nullptr;};
struct idMaterial: idDecl {
 uint64_t identity=R_ImagePolicyNewResourceIdentity();uint64_t GetImagePolicyIdentity()const{return identity;}
 int index=0,line=1,state=DS_PARSED;bool implicit=false;std::string name="material",file="test.mtr",text="{ highquality }";
 shaderStage_t stage; pbrMaterialInfo_t pbr;specularProbeMaterialInfo_t probe;idImage* portal=nullptr;idImage* falloff=nullptr;
 int Index()const override{return index;}int GetLineNum()const override{return line;}const char* GetName()const override{return name.c_str();}
 const char* GetFileName()const override{return file.c_str();}int GetTextLength()const override{return static_cast<int>(text.size());}
 void GetText(char* out)const override{std::memcpy(out,text.c_str(),text.size()+1);Boundary("source");}
 bool IsImplicit()const{return implicit;}int GetState()const{return state;}
 void Invalidate(){state=DS_UNPARSED;}void EnsureNotPurged(){++parses;state=newMaterialDefault?DS_DEFAULTED:DS_PARSED;Boundary("parse");}
 int GetNumStages()const{return 1;}const shaderStage_t* GetStage(int)const{return &stage;}
 const pbrMaterialInfo_t& GetPBRInfo()const{return pbr;}const specularProbeMaterialInfo_t& GetSpecularProbeInfo()const{return probe;}
 const idImage* GetPortalImage()const{return portal;}idImage* LightFalloffImage()const{return falloff;}
};
struct Decls {List<idMaterial*> materials;int GetNumDecls(int){Boundary("inventory");return materials.Num();}const idDecl* DeclByIndex(int,int n,bool){return materials.at(n);}} declarations;
static Decls* declManager=&declarations;
static renderDisplayPresentation_t display{};
void R_GetDisplayPresentation(renderDisplayPresentation_t* p){*p=display;}
bool GLimp_EnsureActiveContext(const char*){Boundary("context");return context;}
static int pendingGL=0;
void GL_CheckErrors(){if(pendingGL){R_ImagePolicyObserveError("mock GL error before drain",pendingGL);pendingGL=0;}}
void glFinish(){++finishes;Boundary("finish");}
struct VkContext {bool presentationBlocked=false,uploadBatchOpen=false,uploadBatchInFlight=false;int numUploadBatchPending=0,numUploadBatchInFlight=0;} vkCtx;
void VK_Device_FlushUploadBatch(){Boundary("flush");if((!starts&&oldPending)||(starts&&finalPending))vkCtx.presentationBlocked=true;vkCtx.uploadBatchOpen=false;}
void VK_Device_WaitUploadBatch(){++finishes;Boundary("wait");if(!vkCtx.presentationBlocked){vkCtx.uploadBatchInFlight=false;vkCtx.numUploadBatchPending=vkCtx.numUploadBatchInFlight=0;}}
bool R_TryFullVidRestartForImagePolicy(const renderWindowRequest_t*,char*,int);
'''

MAIN = r'''
bool R_TryFullVidRestartForImagePolicy(const renderWindowRequest_t* window,char* error,int size){
 ++starts;TEST(window->parms.width==800);Boundary("restart");
 if(!R_ImagePolicyBeforeTeardown(error,size))return false;++teardowns;display.available=0;
 for(auto* i:globalImages->images)i->loaded=false;
 R_ImagePolicyBeginDeviceReload();++display.generation;display.available=1;
 for(auto* i:globalImages->images)if(R_ImagePolicyShouldReload(i))i->Reload(true);
 if(!R_ImagePolicyAfterDeviceReload(error,size)){display.available=0;for(auto* i:globalImages->images)i->loaded=false;return false;}
 Boundary("fonts");Boundary("world");
 if(!R_ImagePolicyFinish(error,size)){display.available=0;for(auto* i:globalImages->images)i->loaded=false;return false;}
 return true;
}
static idImage image,extra;static idMaterial material,introduced;static renderImagePolicyRequest_t request;
static void Reset(){
 TEST(!active);recovery.reset();recoveryInvalidated=false;callback={};packed=allocationFailure=uploadFailure=missingMip=newDefault=newMaterialDefault=oldPending=finalPending=false;
 dependency={};context=true;starts=teardowns=finishes=reloads=parses=publications=pendingGL=0;vkCtx={};
 image={};extra={};material={};introduced={};introduced.index=1;introduced.name="introduced";introduced.stage.texture.image=&extra;material.stage.texture.image=&image;
 imageManager={};imageManager.images.push_back(&image);declarations={};declarations.materials.push_back(&material);
 image_downSize={};image_downSizeLimit={};image_downSizeSpecular={};image_downSizeSpecularLimit={};
 image_downSizeBump={};image_downSizeBumpLimit={};image_usePrecompressedTextures={};image_ignoreHighQuality={};
 request={};request.window.parms.width=800;request.expectedCurrent=ReadPolicy();
 display={};display.available=1;display.generation=20;R_ImagePolicyBindRendererThread();
}
static renderImagePolicyResult_t sentinel;
static bool Run(bool expected){
 renderImagePolicyResult_t output=sentinel;char error[256]="untouched";
 static int scenario=0;++scenario;
 const bool ok=R_TryImagePolicyRestart(&request,&output,error,sizeof(error));if(ok!=expected)std::fprintf(stderr,"scenario %d: %s (expected %d, actual %d)\n",scenario,error,expected,ok);TEST(ok==expected);TEST(!active);
 if(!ok){TEST(std::memcmp(&output,&sentinel,sizeof(output))==0);TEST(error[0]);TEST(publications==0);}
 else {TEST(output.attempt>0);TEST(output.deviceGeneration==display.generation);TEST(output.imagesVerified>=1);TEST(output.materialSourcesReparsed==1);TEST(publications==1);TEST(!error[0]);}
 return ok;
}
int main(){
#if defined(_MSC_VER) && defined(_DEBUG)
 _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
 _CrtSetReportMode(_CRT_ERROR,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ERROR,_CRTDBG_FILE_STDERR);
#endif
 try{
 std::memset(&sentinel,0x6b,sizeof(sentinel));
 TEST(ClassifySource("{ highQuality }").quality);TEST(ClassifySource("{ uncompressed }").quality);
 TEST(!ClassifySource("{ forceHighQuality }").quality);
 TEST(!ClassifySource("// highquality\n{ /* uncompressed */ }").quality);
 TEST(ClassifySource("{ \"// path\" highquality }").quality);
 TEST(ClassifySource("{ highquality fragmentProgram x }").program);
 TEST(ClassifySource("{ \"highquality\" }").quality);

 Reset();Run(true);TEST(reloads==1&&parses==1&&finishes>=2);
 Reset();image.opts.textureType=TT_CUBIC;Run(true);TEST(reloads==1);
 Reset();extra.loaded=false;globalImages->images.push_back(&extra);Run(true);TEST(reloads==1);TEST(!extra.loaded);
 Reset();image.defaulted=true;Run(true);
 Reset();material.state=DS_DEFAULTED;newMaterialDefault=true;Run(true);
 Reset();request.expectedCurrent.downSizeLimit=-1;Run(false);TEST(!starts);
 Reset();request.expectedCurrent.downSize=1;Run(false);TEST(!starts);
 Reset();packed=true;Run(false);TEST(!starts);
 Reset();material.text="{ { highquality program test.vfp } }";Run(false);TEST(!starts);
 Reset();material.text.clear();Run(false);TEST(!starts);
 Reset();material.text="{ STUB: 0 5 }";Run(false);TEST(!starts);
 Reset();material.text=std::string(300000,'x');Run(false);TEST(!starts);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"parse"))dependency.text="1.25";};Run(false);
 Reset();globalImages->insideLevelLoad=true;Run(false);TEST(!starts);
 Reset();globalImages->preloadingMapImages=true;Run(false);TEST(!starts);
 Reset();globalImages->images.push_back(&image);Run(false);TEST(!starts);
 Reset();globalImages->images.resize(32769,&image);Run(false);TEST(!starts);
 Reset();declarations.materials.resize(32769,&material);Run(false);TEST(!starts);
 Reset();allocationFailure=true;Run(false);TEST(starts==1&&recovery);
 Reset();uploadFailure=true;Run(false);TEST(recovery);
 Reset();missingMip=true;Run(false);
 Reset();newDefault=true;Run(false);TEST(recovery);newDefault=false;image.defaulted=false;Run(true);TEST(!recovery);
 Reset();newDefault=true;Run(false);newDefault=false;Run(false);TEST(recovery); // New default never becomes baseline on retry.
 Reset();newMaterialDefault=true;Run(false);newMaterialDefault=false;Run(true);TEST(!recovery);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"parse"))material.text="changed";};Run(false);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"parse"))material.file="other.mtr";};Run(false);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"parse"))material.text[2]='x';};Run(false);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"fonts"))image_downSize.value=1;};Run(false);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"world"))++display.generation;};Run(false);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"parse"))++display.failureSequence;};Run(false);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"allocate"))R_ImagePolicyObserveError("actual API refusal",-7);};Run(false);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"packed")){
  renderImagePolicyResult_t out=sentinel;char e[128];TEST(!R_TryImagePolicyRestart(&request,&out,e,sizeof(e)));TEST(!std::memcmp(&out,&sentinel,sizeof(out)));}};Run(false);TEST(!starts);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"parse")){bool permitted=true;std::thread t([&]{permitted=R_ImagePolicyOperationAllowed();});t.join();TEST(!permitted);}};Run(false);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"restart"))request.window.parms.width=123;};Run(true); // Immutable nested request.
 Reset();callback=[](const char* where){if(!std::strcmp(where,"parse")){extra.loaded=false;globalImages->images.push_back(&extra);extra.Reload(true);material.stage.texture.image=&extra;}};Run(true);TEST(reloads==2);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"parse")){extra.loaded=true;globalImages->images.push_back(&extra);material.stage.texture.image=&extra;}};Run(false); // IsLoaded alone proves nothing.
 Reset();callback=[](const char* where){if(!std::strcmp(where,"world"))++image.storage;};Run(false);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"world")){globalImages->images.push_back(&extra);extra.Reload(true);declarations.materials.push_back(&introduced);R_ImagePolicyObserveError("fail after introducing resources");}};Run(false);callback={};
 {renderImagePolicyResult_t out{};char error[256];TEST(R_TryImagePolicyRestart(&request,&out,error,sizeof(error)));TEST(out.materialSourcesReparsed==2);TEST(extra.loaded);TEST(!recovery);}
 Reset();callback=[](const char* where){if(!std::strcmp(where,"world"))globalImages->images.push_back(&image);};Run(false);
 Reset();extra.file=false;globalImages->images.push_back(&extra);callback=[](const char* where){if(!std::strcmp(where,"world"))globalImages->images.pop_back();};Run(false);

 Reset();image.opts.numLevels=32;Run(false);
 // Direct file uploads, even without a Reload entry, invalidate a prior
 // failed recovery. Generated/persistent content uploads remain outside census.
 Reset();allocationFailure=true;Run(false);allocationFailure=false;
 {const auto before=resourceMutationEpoch.load();renderImageOperation_t upload(&image,true,0,0,8,8);TEST(upload.Allowed());upload.Succeeded();TEST(resourceMutationEpoch.load()!=before);}
 Run(false);TEST(starts==1&&recoveryInvalidated);
 Reset();{image.file=false;const auto before=resourceMutationEpoch.load();renderImageOperation_t upload(&image,true,0,0,8,8);TEST(upload.Allowed());upload.Succeeded();TEST(resourceMutationEpoch.load()==before);}
 Reset();image.identity=0;Run(false);TEST(!starts);
 Reset();material.identity=0;Run(false);TEST(!starts);
 Reset();allocationFailure=true;Run(false);allocationFailure=false;const int priorStarts=starts;
 R_ImagePolicyLifecycleChanged();Run(false);TEST(starts==priorStarts&&recoveryInvalidated);
 Run(false);TEST(starts==priorStarts); // No automatic recensus after lifecycle changes.
 Reset();allocationFailure=true;Run(false);allocationFailure=false;TEST(R_ImagePolicyContentMutation());
 Run(false);TEST(starts==1&&recoveryInvalidated); // Same text/defaults, different consumed content lifetime.
 Reset();image.defaulted=true;allocationFailure=true;Run(false);allocationFailure=false;image.identity=R_ImagePolicyNewResourceIdentity();
 Run(false);TEST(starts==1&&recoveryInvalidated);
 Reset();allocationFailure=true;Run(false);allocationFailure=false;material.identity=R_ImagePolicyNewResourceIdentity();
 Run(false);TEST(starts==1&&recoveryInvalidated); // Parsed source token, independent of default allowances.
 Reset();material.state=DS_DEFAULTED;allocationFailure=true;Run(false);allocationFailure=false;material.identity=R_ImagePolicyNewResourceIdentity();
 Run(false);TEST(starts==1&&recoveryInvalidated);
 Reset();material.state=DS_DEFAULTED;material.implicit=true;allocationFailure=true;Run(false);allocationFailure=false;material.name="renamed";
 Run(false);TEST(starts==1&&recoveryInvalidated);
 Reset();allocationFailure=true;Run(false);allocationFailure=false;declarations.materials.clear();
 Run(false);TEST(starts==1&&recoveryInvalidated); // No stale DeclByIndex / engine error path.
 Reset();callback=[](const char* at){if(!std::strcmp(at,"world"))R_ImagePolicyResourceDestroyed();};Run(false);TEST(recoveryInvalidated);
 Reset();callback=[](const char* at){if(!std::strcmp(at,"parse")){bool permitted=true;std::thread worker([&]{permitted=R_ImagePolicyContentMutation();});worker.join();TEST(!permitted);}};Run(false);
 Reset();extra.loaded=false;material.pbr.albedo.present=true;material.pbr.albedo.image=&extra;Run(false);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"source"))image_downSize.value=1;};Run(false);TEST(!starts);
#ifdef OPENQ4_RENDERER_VK_MODULE
 Reset();oldPending=true;Run(false);TEST(!starts);
 Reset();finalPending=true;Run(false);TEST(starts==1);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"wait")&&starts)vkCtx.uploadBatchOpen=true;};Run(false);
#else
 Reset();pendingGL=1285;Run(false);TEST(!starts);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"upload"))pendingGL=1282;};Run(false);
 Reset();callback=[](const char* where){if(!std::strcmp(where,"finish")&&starts)context=false;};Run(false);
#endif
 bool reachedSuccess=false;
 for(int point=0;point<2000;++point){
  Reset();renderImagePolicyResult_t out=sentinel;char error[256];allocationCalls=0;allocationBudget=point;
  bool ok=false;try{ok=R_TryImagePolicyRestart(&request,&out,error,sizeof(error));}catch(...){allocationBudget=-1;throw;}
  const int calls=allocationCalls;allocationBudget=-1;
  if(ok){TEST(publications==1);TEST(calls<=point);reachedSuccess=true;std::printf("allocation denial sweep reached full success at %d allocations\n",point);break;}
  TEST(!std::memcmp(&out,&sentinel,sizeof(out)));TEST(error[0]);TEST(publications==0);TEST(!active);
 }
 TEST(reachedSuccess);
 std::printf("checked image policy: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''


def mutated(source, old, new):
    if source.count(old) != 1:
        raise AssertionError(f'mutation anchor is not unique: {old}')
    return source.replace(old, new)


def mutations(source):
    changes = [
        ('new-default', 'image->IsDefaulted() && !it->second.baselineDefault', 'false'),
        ('mip-proof', 'it->second.mips[side] != expected', 'false'),
        ('storage-proof', 'it->second.storage != image->GetStorageGeneration()', 'false'),
        ('source-identity', 'source.name != current->GetName() || source.file != current->GetFileName() ||', 'source.name != current->GetName() || false ||'),
        ('source-text', "return text.back() == '\\0' && text.compare(0, source.text.size(), source.text) == 0;", 'return true;'),
        ('loader-drift', 'a.dependencyText[i].value != a.dependencies[i]->GetString()', 'false'),
        ('device-generation', 'current.generation != a.result.deviceGeneration', 'false'),
        ('device-failure', 'current.failureSequence != a.initialFailures', 'false'),
        ('retained-census', 'original.second.resident && !seen.count(original.first)', 'false'),
        ('final-duplicates', '!image || !seen.insert(image).second', '!image'),
        ('new-dependency', 'if (!image->IsLoaded()) const_cast<idImage*>(image)->Reload(true);', '(void)image;'),
        ('shader-refusal', 'if (customProgram) return Error', 'if (false) return Error'),
        ('foreign-thread', 'if (std::this_thread::get_id() != rendererThread) {\n        FailLocked("Image policy work crossed the renderer thread"); return false;', 'if (false) {\n        FailLocked("Image policy work crossed the renderer thread"); return false;'),
        ('direct-upload-epoch', 'if (allowed && image && image->IsFileBacked()) allowed = R_ImagePolicyContentMutation();', '(void)0;'),
        ('recovery-epoch', 'if (recovery && recovery->mutationEpoch != candidate.mutationEpoch) recoveryInvalidated = true;', '(void)0;'),
        ('image-instance', 'if (old->second.identity != image->GetImagePolicyIdentity()) {', 'if (false) {'),
        ('material-instance', '!source.identity || source.material->GetImagePolicyIdentity() != source.identity ||', 'false ||'),
        ('default-name', 'proof.name != current->GetName()', 'false'),
        ('source-index-range', 'if (source.index < 0 || source.index >= declManager->GetNumDecls(DECL_MATERIAL)) return false;', '(void)0;'),
        ('sticky-recovery', 'if (recoveryInvalidated || !candidate.mutationEpoch)', 'if (!candidate.mutationEpoch)'),
        ('early-publication', 'const renderImagePolicyRequest_t immutable = *request;', 'const renderImagePolicyRequest_t immutable = *request; *output = {};'),
    ]
    return [(name, mutated(source, old, new)) for name, old, new in changes]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repository', type=Path, default=ROOT)
    parser.add_argument('--headers', type=Path, default=ROOT)
    parser.add_argument('--mutations', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--compiler')
    args = parser.parse_args()
    repository, headers = args.repository.resolve(), args.headers.resolve()
    inputs = [repository / name for name in (
        'src/renderer/RendererResourceSettings.cpp', 'src/renderer/RendererResourceSettings.h',
        'src/renderer/RenderModuleAPI.h', 'tools/tests/renderer_image_policy_restart.py')]
    inputs += [headers / 'src/renderer/DisplayPresentation.h']
    def hashes():
        return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    before = hashes()
    source = inputs[0].read_text(encoding='utf-8')
    source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include "') and not line.startswith('#pragma hdrstop'))
    compiler = args.compiler or next((p for n in ('clang++', 'g++', 'c++') if (p := shutil.which(n))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    scratch = repository / '.tmp'
    scratch.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='image-policy-', dir=scratch))
    env = dict(os.environ, TEMP=str(out), TMP=str(out), TMPDIR=str(out))
    records = []
    cases = [('gl', 'baseline', source), ('vk', 'baseline', source)]
    if args.mutations:
        cases += [('gl', name, changed) for name, changed in mutations(source)]
    status = 'running'
    try:
        for backend, name, generated in cases:
            tag = backend + '-' + name
            unit = out / (tag + '.cpp')
            unit.write_text(SUPPORT + generated + MAIN, encoding='utf-8')
            binary = out / (tag + '.exe')
            command = [compiler, '-std=c++20', '-pthread', '-I', str(repository / 'src/renderer'), '-I', str(headers / 'src/renderer')]
            if backend == 'vk':
                command += ['-DOPENQ4_RENDERER_VK_MODULE']
            if args.sanitize:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-no-pie']
            command += [str(unit), '-o', str(binary)]
            if Path(compiler).stem.lower() in ('cl', 'clang-cl'):
                if args.sanitize:
                    raise RuntimeError('sanitizer option requires GCC/Clang Unix-style driver')
                command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/MTd', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/I' + str(repository / 'src/renderer'), '/I' + str(headers / 'src/renderer'), *(['/DOPENQ4_RENDERER_VK_MODULE'] if backend == 'vk' else []), str(unit), '/Fe:' + str(binary), '/Fo:' + str(out / (tag + '.obj'))]
            for stage, call in (('compile', command), ('run', [str(binary)])):
                result = subprocess.run(call, cwd=out, env=env, timeout=90, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                log = out / (tag + '-' + stage + '.log')
                log.write_text(result.stdout, encoding='utf-8')
                record = {'backend': backend, 'case': name, 'stage': stage, 'command': call, 'exit_code': result.returncode, 'log': str(log), 'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest()}
                records.append(record)
                if stage == 'compile' and result.returncode:
                    raise RuntimeError(f'{tag}: compilation failed')
                if stage == 'run':
                    if name == 'baseline' and result.returncode:
                        raise RuntimeError(f'{tag}: baseline failed')
                    if name != 'baseline' and not result.returncode:
                        raise RuntimeError(f'{tag}: compiled mutation survived')
                    record['expected_rejection'] = name != 'baseline'
                    print(f'{tag}: ' + ('rejected compiled mutation' if name != 'baseline' else result.stdout.strip()))
        status = 'passed'
    finally:
        after = hashes()
        if before != after:
            status = 'source_changed'
        elif status != 'passed':
            status = 'failed'
        (out / 'result.json').write_text(json.dumps({'status': status, 'files_before': before, 'files_after': after, 'records': records, 'limitations': 'Complete production coordinator with counted engine/GPU boundaries; no driver, UI-present or actual device qualification.'}, indent=2) + '\n')
        print(out / 'result.json')
    if status != 'passed':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
