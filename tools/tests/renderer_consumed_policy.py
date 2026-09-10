#!/usr/bin/env python3
"""Compile real resolver/observer and Vulkan batch bodies with counted native doubles.

No GPU, source decoding, content identity, portable recovery or engine-runtime
qualification. Material tests compile actual parse publication boundaries around
a counted parser continuation; separate source checks bind every parser consumer.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    masked = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/', lambda match: ' '*len(match[0]), source)
    brace = masked.index('{', start)
    depth = 1
    i = brace + 1
    while depth:
        depth += (masked[i] == '{') - (masked[i] == '}')
        i += 1
    return source[start:i]


SUPPORT = r'''
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <thread>
#include "RendererConsumedPolicy.h"
template<class T>T Max(T a,T b){return a>b?a:b;}
enum {TD_SPECULAR,TD_DIFFUSE,TD_DEFAULT,TD_BUMP,TD_HIGH_QUALITY=15};
using textureUsage_t=int;
enum {PICMIP_FILTER_ALL=0,PICMIP_FILTER_TEXTURES=1,PICMIP_FILTER_MODELS=2,PICMIP_FILTER_OTHER=4,PICMIP_FILTER_MASK=7,IMAGEFLAG_NOMIPS=1};
enum {TT_2D=1,TT_CUBIC=2,DS_PARSED=1,MF_DEFAULTED=1};
struct idStr {static int Icmpn(const char* a,const char* b,int n){for(int i=0;i<n;++i){unsigned char x=a[i],y=b[i];if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y||!x)return x-y;}return 0;}};
struct idImageOpts {int width=8,height=8,numLevels=4,textureType=TT_2D;bool isPersistant=false;};
struct renderDisplayPresentation_t {uint64_t generation=1,failureSequence=0;bool available=true;};
extern renderDisplayPresentation_t nativeDevice;
extern std::thread::id owner;
extern uint64_t nextIdentity;
extern imageDownsizeInputs_t liveInputs;
extern int nativeFinishes,checks;
extern bool failNative;
extern std::function<void()> nativeCallback;
void R_GetDisplayPresentation(renderDisplayPresentation_t* out);
bool R_ImagePolicyRendererThread();
uint64_t R_ImagePolicyNewResourceIdentity() noexcept;
void GL_CheckErrors();void glFinish();
void R_ApplyImageDownsizePolicy(const imageDownsizePolicy_t&,int&,int&);
bool R_ResolveImageReduction(const imageDownsizePolicy_t&,int,int,int,imageReductionResult_t&);
bool R_ImageReductionIsExact(const imageDownsizePolicy_t&,const imageReductionResult_t&);
void R_ResolveImageDownsizePolicy(const imageDownsizeInputs_t&,const char*,int,bool,imageDownsizePolicy_t&);
textureUsage_t R_ResolveMaterialHighQualityUsage(const materialQualityInputs_t&,textureUsage_t,bool);
unsigned int R_ResolveMaterialNoMipFlags(const materialQualityInputs_t&,unsigned int);
class idImage {public:
 uint64_t imagePolicyIdentity=R_ImagePolicyNewResourceIdentity(),storageGeneration=0;
 const char* name="textures/image";int usage=TD_DIFFUSE,filter=0,repeat=0,cubeFiles=0;unsigned flags=0;
 bool allowDownSize=true,defaulted=false,scratchImage=false,loaded=true;
 idImageOpts opts;imageConsumedPolicy_t consumedPolicy{};uint64_t consumedLoadRevision=0;
 const char* GetName()const{return name;}bool IsFileBacked()const{return !scratchImage&&!opts.isPersistant;}
 bool IsLoaded()const{return loaded;}
 bool GetConsumedPolicy(imageConsumedPolicy_t&)const;void InvalidateConsumedPolicy();
};
class idMaterial {public:
 uint64_t imagePolicyIdentity=R_ImagePolicyNewResourceIdentity(),consumedParseRevision=0;uint32_t consumedParseDepth=0;materialConsumedPolicy_t consumedPolicy{};
 bool defaulted=false;void* pd=nullptr;int state=DS_PARSED;
 struct Stage{void* newStage=nullptr;}stage;
 int GetNumStages()const{return 1;}const Stage* GetStage(int)const{return &stage;}
 bool TestMaterialFlag(int)const{return defaulted;}int GetState()const{return state;}
 void MakeDefault(){consumedPolicy={99,99,99,{},true};}
 bool GetConsumedPolicy(materialConsumedPolicy_t&)const;
 bool Parse(const char*,int);void FreeData();
};
struct FakeCvar {bool value=false;bool GetBool()const{return value;}};
extern FakeCvar image_ignoreHighQuality,com_makingBuild;
extern bool contentAllowed;
extern std::function<void(idMaterial&,materialQualityInputs_t)> parserContinuation;
bool R_ImagePolicyContentMutation();
#define TEST(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(0)
'''

MAIN = r'''
#include "tr_local.h"
renderDisplayPresentation_t nativeDevice;
std::thread::id owner=std::this_thread::get_id();uint64_t nextIdentity=1;
imageDownsizeInputs_t liveInputs{};
int nativeFinishes=0,checks=0;bool failNative=false,contentAllowed=true;
std::function<void()> nativeCallback;
FakeCvar image_ignoreHighQuality,com_makingBuild;
std::function<void(idMaterial&,materialQualityInputs_t)> parserContinuation;
bool R_ImagePolicyContentMutation(){return contentAllowed;}
void R_GetDisplayPresentation(renderDisplayPresentation_t* out){*out=nativeDevice;}
bool R_ImagePolicyRendererThread(){return owner==std::this_thread::get_id();}
uint64_t R_ImagePolicyNewResourceIdentity()noexcept{return nextIdentity++;}
imageDownsizeInputs_t R_ReadImageDownsizeInputs(){return liveInputs;}
void GL_CheckErrors(){if(failNative)R_ConsumedPolicyObserveError();}
void glFinish(){++nativeFinishes;if(nativeCallback)nativeCallback();}
static void Prove(imageConsumedLoad_t& scope,idImage& image){imageReductionResult_t r;TEST(R_ResolveImageReduction(scope.Policy(),image.opts.width,image.opts.height,0,r));scope.Reduction(r);}
static void Upload(idImage& image,bool omit=false,bool refuse=false){
 ++image.storageGeneration;
 imageConsumedLoad_t::Operation(&image,false,!refuse,0,0,0,0,0);
 for(int i=0;i<image.opts.numLevels-(omit?1:0);++i)
  imageConsumedLoad_t::Operation(&image,true,true,i,0,Max(1,image.opts.width>>i),Max(1,image.opts.height>>i),1);
}
static void Load(idImage& image,bool omit=false,bool refuse=false,imageConsumedSource_t source=ICS_DECODED_2D){
 imageConsumedLoad_t scope(image);Upload(image,omit,refuse);Prove(scope,image);scope.Loaded(source);
}
static void Unavailable(idImage& image){imageConsumedPolicy_t out;out.revision=998;TEST(!image.GetConsumedPolicy(out));TEST(out.revision==998);}
int main(){try{
 for(int generic: {0,1,-2})for(int spec:{0,1})for(int bump:{0,1})for(int usage:{TD_DIFFUSE,TD_SPECULAR,TD_BUMP,TD_DEFAULT}){
  imageDownsizeInputs_t in{generic,512,spec,128,bump,256,2,1,32};imageDownsizePolicy_t p;
  R_ResolveImageDownsizePolicy(in,"textures/a",usage,true,p);
  TEST(p.maxDimension==(usage==TD_SPECULAR&&spec?128:usage==TD_BUMP&&bump?256:generic?512:0));
  TEST(p.mipShift==(usage==TD_DIFFUSE?2:0));TEST(p.minDimension==32);
  R_ResolveImageDownsizePolicy(in,"textures/a",usage,false,p);TEST(!p.IsActive()&&p.minDimension==1);
 }
 for(int mask=0;mask<16;++mask)for(const char* name:{"TEXTURES/a","models\\b","other/c","addnormals( textures/a, heightmap( models/b, 4 ) )","textures",""}){
  imageDownsizeInputs_t in{0,0,0,0,0,0,3,mask,0};imageDownsizePolicy_t p;R_ResolveImageDownsizePolicy(in,name,TD_DIFFUSE,true,p);
  const int category=(name[0]=='T'||name[0]=='a')?1:name[0]=='m'?2:4;
  const bool allowed=(mask&7)==0||(*name&&(mask&category));TEST(p.mipShift==(allowed?3:0));TEST(p.minDimension==1);
 }
 for(bool ignore:{false,true})for(bool build:{false,true})for(bool force:{false,true}){
  materialQualityInputs_t in{ignore,build};TEST(R_ResolveMaterialHighQualityUsage(in,TD_DIFFUSE,force)==(force||!ignore?TD_HIGH_QUALITY:TD_DIFFUSE));
  TEST(R_ResolveMaterialNoMipFlags(in,8)==unsigned(build?9:8));
 }
 idImage a,b;imageConsumedPolicy_t out;Unavailable(a);
 liveInputs={1,256,0,0,0,0,1,1,32};Load(a);Unavailable(a);const auto first=a.consumedPolicy;
 liveInputs={1,64,0,0,0,0,3,1,8};Load(b);Unavailable(b);
 TEST(R_CompleteConsumedImageUploads());TEST(a.GetConsumedPolicy(out));TEST(out.resolved.maxDimension==256&&out.resolved.mipShift==1&&out.completion==ICC_COMPLETE);
 TEST(b.GetConsumedPolicy(out));TEST(out.resolved.maxDimension==64&&out.resolved.mipShift==3);
 TEST(a.consumedPolicy.inputs.downSizeLimit==256); // Heterogeneous history is not flattened.
 a.allowDownSize=false;Unavailable(a);a.allowDownSize=true;TEST(a.GetConsumedPolicy(out));
 a.opts.width=16;Unavailable(a);a.opts.width=8;TEST(a.GetConsumedPolicy(out));
 ++a.storageGeneration;Unavailable(a);--a.storageGeneration;TEST(a.GetConsumedPolicy(out));
 ++nativeDevice.failureSequence;Unavailable(a);--nativeDevice.failureSequence;TEST(a.GetConsumedPolicy(out));
 ++nativeDevice.generation;Unavailable(a);--nativeDevice.generation;TEST(a.GetConsumedPolicy(out));
 a.loaded=false;Unavailable(a);a.loaded=true;TEST(a.GetConsumedPolicy(out));
 a.InvalidateConsumedPolicy();Unavailable(a);Load(a);TEST(a.consumedPolicy.revision!=first.revision);Unavailable(a);
 TEST(R_CompleteConsumedImageUploads());TEST(a.GetConsumedPolicy(out));
 imageConsumedLoad_t::BeforeOperation(&a);Unavailable(a); // Invalidate before a native callback can query.
 for(int mode=0;mode<7;++mode){idImage i;{imageConsumedLoad_t load(i);Upload(i,mode==1,mode==2);Prove(load,i);
  if(mode==3)imageConsumedLoad_t::Error();if(mode==4)++i.storageGeneration;if(mode==5)++nativeDevice.generation;
  if(mode!=0)load.Loaded(mode==6?ICS_DIRECT_DDS:ICS_DECODED_2D);
 }TEST(R_CompleteConsumedImageUploads());Unavailable(i);}
 {idImage i;Load(i);failNative=true;TEST(!R_CompleteConsumedImageUploads());failNative=false;
  TEST(R_CompleteConsumedImageUploads());Unavailable(i);}
 {idImage i;Load(i);nativeCallback=[](){++nativeDevice.generation;};TEST(!R_CompleteConsumedImageUploads());nativeCallback={};Unavailable(i);}
 {idImage i;Load(i);nativeCallback=[](){throw std::runtime_error("native completion");};TEST(!R_CompleteConsumedImageUploads());nativeCallback={};TEST(R_CompleteConsumedImageUploads());Unavailable(i);}
 {idImage i;Load(i);TEST(R_CompleteConsumedImageUploads());TEST(i.GetConsumedPolicy(out));
  std::thread other([&](){imageConsumedLoad_t load(i);});other.join();Unavailable(i);}
 {idImage i;imageConsumedLoad_t load(i);TEST(!R_CompleteConsumedImageUploads());Upload(i);Prove(load,i);load.Loaded(ICS_DECODED_2D);}
 {idImage outer,inner;{imageConsumedLoad_t l(outer);Upload(outer);Load(inner);Prove(l,outer);l.Loaded(ICS_DECODED_2D);}TEST(R_CompleteConsumedImageUploads());TEST(outer.GetConsumedPolicy(out));TEST(inner.GetConsumedPolicy(out));}
 {idImage i;{imageConsumedLoad_t l(i);Upload(i);Load(i);l.Loaded(ICS_DECODED_2D);}TEST(R_CompleteConsumedImageUploads());Unavailable(i);}
 {idImage i;try{imageConsumedLoad_t l(i);Upload(i);l.Loaded(ICS_DECODED_2D);throw std::runtime_error("load");}catch(...){}TEST(R_CompleteConsumedImageUploads());Unavailable(i);}
 {idImage old,late;Load(old);nativeCallback=[&](){Load(late);};TEST(R_CompleteConsumedImageUploads());nativeCallback={};TEST(old.GetConsumedPolicy(out));Unavailable(late);TEST(R_CompleteConsumedImageUploads());TEST(late.GetConsumedPolicy(out));}

 // Exact source evidence and actual full-layer upload coverage are independent.
 for(auto source:{ICS_DIRECT_DDS,ICS_DECODED_CUBE})for(int mode=0;mode<7;++mode){
  liveInputs={0,0,0,0,0,0,2,1,1};idImage i;i.opts.width=4;i.opts.height=4;i.opts.numLevels=3;i.opts.textureType=source==ICS_DIRECT_DDS?TT_2D:TT_CUBIC;
  {imageConsumedLoad_t scope(i);imageReductionResult_t r;TEST(R_ResolveImageReduction(scope.Policy(),16,16,source==ICS_DIRECT_DDS?5:0,r));
   if(mode==2)r.status=IR_INSUFFICIENT_MIPS;if(mode==3)++r.requestedWidth;if(mode==4)i.opts.width=8;
   if(mode==5)r.authoredLevels=4;if(mode==6)i.opts.textureType=source==ICS_DIRECT_DDS?TT_CUBIC:TT_2D;
   if(mode!=1)scope.Reduction(r);++i.storageGeneration;imageConsumedLoad_t::Operation(&i,false,true,0,0,0,0,0);
   for(int face=0;face<(i.opts.textureType==TT_CUBIC?6:1);++face)for(int mip=0;mip<i.opts.numLevels;++mip)imageConsumedLoad_t::Operation(&i,true,true,mip,face,Max(1,i.opts.width>>mip),Max(1,i.opts.height>>mip),1);
   scope.Loaded(source);
  }TEST(R_CompleteConsumedImageUploads());if(mode==0){TEST(i.GetConsumedPolicy(out));TEST(out.reduction.status==IR_EXACT&&out.reduction.sourceWidth==16&&out.reduction.requestedWidth==4);}else Unavailable(i);
 }
 {idImage i;{imageConsumedLoad_t scope(i);imageReductionResult_t forged;forged.status=IR_EXACT;forged.sourceWidth=998;scope.Reduction(forged);Upload(i);scope.Loaded(ICS_GENERATED);}TEST(R_CompleteConsumedImageUploads());TEST(i.GetConsumedPolicy(out));TEST(out.reduction.status==IR_UNOBSERVED&&out.reduction.sourceWidth==0);}
 idMaterial material;materialConsumedPolicy_t materialOut;materialOut.revision=99;TEST(!material.GetConsumedPolicy(materialOut)&&materialOut.revision==99);
 image_ignoreHighQuality.value=false;com_makingBuild.value=true;
 parserContinuation=[](idMaterial&,materialQualityInputs_t in){TEST(!in.ignoreHighQuality&&in.makingBuild);image_ignoreHighQuality.value=true;com_makingBuild.value=false;};
 TEST(material.Parse("valid",5));TEST(material.GetConsumedPolicy(materialOut));TEST(!materialOut.inputs.ignoreHighQuality&&materialOut.inputs.makingBuild);
 material.defaulted=true;TEST(!material.GetConsumedPolicy(materialOut));material.defaulted=false;TEST(material.GetConsumedPolicy(materialOut));
 material.stage.newStage=&material;TEST(!material.GetConsumedPolicy(materialOut));material.stage.newStage=nullptr;
 parserContinuation={};const auto oldRevision=materialOut.revision;TEST(material.Parse("other",5));TEST(material.GetConsumedPolicy(materialOut));TEST(materialOut.revision!=oldRevision&&materialOut.inputs.ignoreHighQuality);
 parserContinuation=[](idMaterial& m,materialQualityInputs_t){m.defaulted=true;};TEST(!material.Parse("invalid",7));TEST(!material.GetConsumedPolicy(materialOut));TEST(!material.consumedPolicy.parsed);
 material.defaulted=false;parserContinuation={};TEST(material.Parse("valid",5));material.FreeData();TEST(!material.GetConsumedPolicy(materialOut));
 TEST(material.Parse("valid",5));contentAllowed=false;const auto unchanged=material.consumedPolicy.revision;TEST(!material.Parse("denied",6));TEST(material.consumedPolicy.revision==unchanged);contentAllowed=true;
 parserContinuation=[](idMaterial&,materialQualityInputs_t){R_ConsumedPolicyInvalidateThread();};TEST(material.Parse("epoch",5));TEST(!material.GetConsumedPolicy(materialOut));
 parserContinuation=[](idMaterial& m,materialQualityInputs_t){m.FreeData();};TEST(material.Parse("mutated",7));TEST(!material.GetConsumedPolicy(materialOut));
 parserContinuation=[](idMaterial&,materialQualityInputs_t){throw std::runtime_error("parse");};try{material.Parse("throw",5);TEST(false);}catch(const std::runtime_error&){}TEST(!material.GetConsumedPolicy(materialOut));
 bool nested=false;parserContinuation=[&](idMaterial& m,materialQualityInputs_t){if(!nested){nested=true;TEST(m.Parse("inner",5));TEST(!m.GetConsumedPolicy(materialOut));}};
 TEST(material.Parse("outer",5));TEST(!material.GetConsumedPolicy(materialOut));TEST(material.consumedParseDepth==0);
 parserContinuation={};TEST(material.Parse("valid",5));TEST(material.GetConsumedPolicy(materialOut));
 std::printf("consumed policy actual methods: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''

VK_HEADER = r'''
#pragma once
#include <cstdint>
struct VkContext {bool presentationBlocked=false,uploadBatchOpen=false,uploadBatchInFlight=false;uint64_t uploadBatchSerial=0,uploadBatchCompletedSerial=0;};
extern VkContext vkCtx;
void VK_Device_FlushUploadBatch();void VK_Device_WaitUploadBatch();
'''
VK_MAIN = r'''
#include "Vulkan/VulkanDevice.h"
VkContext vkCtx;
void VK_Device_FlushUploadBatch(){if(vkCtx.uploadBatchOpen&&!vkCtx.presentationBlocked){vkCtx.uploadBatchOpen=false;vkCtx.uploadBatchInFlight=true;}}
void VK_Device_WaitUploadBatch(){if(!vkCtx.uploadBatchInFlight||vkCtx.presentationBlocked)return;++nativeFinishes;
 const auto batch=vkCtx.uploadBatchSerial;if(nativeCallback)nativeCallback();
 if(failNative){vkCtx.presentationBlocked=true;++nativeDevice.failureSequence;return;}
 vkCtx.uploadBatchCompletedSerial=batch;vkCtx.uploadBatchInFlight=false;
}
static void Load(idImage& image,bool missingBatch=false){imageConsumedLoad_t scope(image);++image.storageGeneration;
 imageConsumedLoad_t::Operation(&image,false,true,0,0,0,0,0);
 for(int i=0;i<image.opts.numLevels;++i){
  VK_Device_FlushUploadBatch();VK_Device_WaitUploadBatch();
  vkCtx.uploadBatchSerial=R_ImagePolicyNewResourceIdentity();vkCtx.uploadBatchOpen=true;
  imageConsumedLoad_t::Operation(&image,true,true,i,0,Max(1,image.opts.width>>i),Max(1,image.opts.height>>i),missingBatch&&i==1?0:vkCtx.uploadBatchSerial);
 }imageReductionResult_t reduction;TEST(R_ResolveImageReduction(scope.Policy(),image.opts.width,image.opts.height,0,reduction));scope.Reduction(reduction);scope.Loaded(ICS_DECODED_2D);
}
static void Unavailable(idImage& image){imageConsumedPolicy_t out;out.revision=998;TEST(!image.GetConsumedPolicy(out));TEST(out.revision==998);}
int main(){try{
 idImage a,b;imageConsumedPolicy_t out;
 liveInputs={1,256,0,0,0,0,1,1,32};Load(a);Unavailable(a);
 TEST(a.consumedPolicy.batch==vkCtx.uploadBatchSerial&&a.consumedPolicy.batch>vkCtx.uploadBatchCompletedSerial);
 // Only the final batch is pending; completed earlier mip batches need no per-image wait.
 const int before=nativeFinishes;VK_Device_FlushUploadBatch();Unavailable(a);VK_Device_WaitUploadBatch();
 TEST(nativeFinishes==before+1&&a.GetConsumedPolicy(out)&&out.completion==ICC_COMPLETE);
 liveInputs.downSizeLimit=64;Load(b);TEST(a.GetConsumedPolicy(out)&&out.inputs.downSizeLimit==256);Unavailable(b);
 TEST(R_CompleteConsumedImageUploads());TEST(b.GetConsumedPolicy(out)&&out.inputs.downSizeLimit==64);
 vkCtx.presentationBlocked=true;Unavailable(a);vkCtx.presentationBlocked=false;TEST(a.GetConsumedPolicy(out));
 idImage bad;Load(bad,true);TEST(R_CompleteConsumedImageUploads());Unavailable(bad);
 idImage failed;Load(failed);failNative=true;TEST(!R_CompleteConsumedImageUploads());Unavailable(failed);
 failNative=false;vkCtx.presentationBlocked=false;TEST(R_CompleteConsumedImageUploads());Unavailable(failed);
 idImage stale;Load(stale);++nativeDevice.generation;TEST(R_CompleteConsumedImageUploads());Unavailable(stale);
 std::printf("consumed Vulkan policy actual observer: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repository', type=Path, default=ROOT)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--mutations', action='store_true')
    parser.add_argument('--vulkan', action='store_true')
    parser.add_argument('--compiler')
    args = parser.parse_args()
    root = args.repository.resolve()
    names = ['src/renderer/'+name for name in ['RendererConsumedPolicy.h', 'RendererConsumedPolicy.cpp', 'ImageManager.cpp', 'Image_load.cpp', 'Material.cpp', 'Material.h', 'Image.h', 'RendererResourceSettings.cpp', 'RendererResourceSettings.h', 'OpenGL/gl_Image.cpp', 'Vulkan/vk_Image.cpp', 'Vulkan/VulkanDevice.cpp', 'Vulkan/VulkanDevice.h', 'RenderSystem_init.cpp']]
    names.append('tools/tests/renderer_consumed_policy.py')
    names.append("src/imagetools/Image_process.cpp")
    def hashes():
        return {name: hashlib.sha256((root/name).read_bytes()).hexdigest() for name in names}
    before = hashes()
    manager = (root/names[2]).read_text()
    material = (root/'src/renderer/Material.cpp').read_text()
    load = (root/'src/renderer/Image_load.cpp').read_text()
    observer = (root/names[1]).read_text()
    startup_source = (root/'src/renderer/RenderSystem_init.cpp').read_text()
    startup = method(startup_source, 'void idRenderSystemLocal::Init( void )')
    assert startup[startup.index('{')+1:].lstrip().startswith('R_ImagePolicyBindRendererThread();')
    assert startup.index('R_ImagePolicyBindRendererThread();') < startup.index('globalImages->Init();') < startup.index('R_InitMaterials();')
    assert startup_source.count('R_ImagePolicyBindRendererThread();') == 2
    assert 'image_ignoreHighQuality.GetBool()' not in method(material, 'textureUsage_t R_ResolveMaterialHighQualityUsage(')
    assert 'R_ApplyMaterialHighQualityUsage(' not in material and 'R_ApplyMaterialNoMipFlags(' not in material
    assert material.count('R_ResolveMaterialHighQualityUsage( pd->qualityInputs,') == 6
    assert material.count('R_ResolveMaterialNoMipFlags( pd->qualityInputs,') == 4
    actual_load = method(load, 'void idImage::ActuallyLoadImage(')
    assert 'R_GetImageDownsizePolicy(' not in actual_load
    assert actual_load.count('flags, &consumedDownsize );') == 2
    assert 'consumedLoad.Loaded(consumedSource);' in actual_load
    assert 'exactDecodedReduction = width == expectedWidth && height == expectedHeight;' in actual_load
    assert 'consumedLoad.Reduction(consumedReduction);' in actual_load
    extracted = '\n'.join(method(manager, sig) for sig in ['static bool R_ImagePathStartsWith(', 'static bool R_IsImageProgramNameChar(', 'static bool R_ImagePicmipFilterAllows(', 'void R_ResolveImageDownsizePolicy('])
    process = (root/'src/imagetools/Image_process.cpp').read_text()
    extracted += '\n'+'\n'.join(method(process, sig) for sig in ['void R_ApplyImageDownsizePolicy(', 'int R_ImageDownsizePolicyMipSkip(', 'bool R_ResolveImageReduction(', 'bool R_ImageReductionIsExact('])
    extracted += '\n'+'\n'.join(method(material, sig) for sig in ['textureUsage_t R_ResolveMaterialHighQualityUsage(', 'unsigned int R_ResolveMaterialNoMipFlags('])
    parse = method(material, 'bool idMaterial::Parse( const char *text, const int textLength )')
    prefix = parse[:parse.index('\tidLexer')]
    suffix = parse[parse.index('\tpd = NULL;'):]
    extracted += '\n'+prefix+'\n    (void)text; (void)textLength; if(parserContinuation)parserContinuation(*this,qualityInputs);\n'+suffix
    free = method(material, 'void idMaterial::FreeData()')
    extracted += '\n'+free[:free.index('\n', free.index('consumedPolicy = {};'))]+'\n}\n'
    compiler = args.compiler or next((path for name in ['clang++', 'g++', 'c++'] if (path := shutil.which(name))), None)
    assert compiler
    (root/'.tmp').mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='consumed-policy-', dir=root/'.tmp'))
    env = dict(os.environ, TEMP=str(out), TMP=str(out), TMPDIR=str(out))
    (out/'tr_local.h').write_text(SUPPORT)
    (out/'RendererResourceSettings.h').write_text('#pragma once\n')
    (out/'DisplayPresentation.h').write_text('#pragma once\n')
    shutil.copyfile(root/names[0], out/'RendererConsumedPolicy.h')
    if args.vulkan:
        (out/'Vulkan').mkdir()
        (out/'Vulkan/VulkanDevice.h').write_text(VK_HEADER)
    (out/'main.cpp').write_text(MAIN[:MAIN.index('static void Upload(')]+VK_MAIN if args.vulkan else MAIN)
    (out/'resolvers.cpp').write_text('#include "tr_local.h"\n'+extracted)
    cases = [('actual', observer, False)]
    if args.mutations:
        edits = [
            ('early-complete', 'value.batch > completedGL', 'false'),
            ('current-policy', 'candidate.inputs = R_ReadImageDownsizeInputs();', 'candidate.inputs = {};'),
            ('missing-coverage', 'mips[layer] != expected', 'false'),
            ('accept-refusal', 'if (!succeeded ||', 'if (false ||'),
            ('stale-storage', 'value.storage != storageGeneration', 'false'),
            ('ignore-epoch', 'value.observationEpoch != R_ConsumedPolicyObservationEpoch()', 'false'),
            ('ignore-device', 'value.device != device.generation', 'false'),
            ('default-success', 'GetState() != DS_PARSED || TestMaterialFlag(MF_DEFAULTED)', 'false'),
            ('bulk-no-finish', 'glFinish();', '(void)0;'),
            ('late-work-complete', 'completedGL = through;', 'completedGL = issuedGL;'),
            ('active-parse', ' || consumedParseDepth ||', ' || false ||'),
            ('unproved-reduction', 'if ((decoded || direct) &&', 'if (false &&'),
            ('generated-source-proof', 'if (candidate.source == ICS_GENERATED) candidate.reduction = {};', '(void)0;'),
        ]
        if args.vulkan:
            edits = [
                ('vk-early-complete', 'value.batch > vkCtx.uploadBatchCompletedSerial', 'false'),
                ('vk-missing-batch', 'if (!batch)', 'if (false)'),
                ('vk-blocked', 'vkCtx.presentationBlocked || !value.batch', 'false || !value.batch'),
                ('vk-ignore-device', 'value.device != device.generation', 'false'),
                ('vk-current-policy', 'candidate.inputs = R_ReadImageDownsizeInputs();', 'candidate.inputs = {};'),
            ]
        for tag, old, new in edits:
            assert old in observer
            cases.append((tag, observer.replace(old, new, 1), True))
    records = []
    status = 'failed'
    try:
        for tag, code, rejected in cases:
            source = out/(tag+'.cpp'); source.write_text(code)
            binary = out/(tag+'.exe')
            command = [compiler, '-std=c++20', '-pthread', '-I', str(out)]
            if args.vulkan:
                command += ['-DOPENQ4_RENDERER_VK_MODULE']
            if args.sanitize:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-no-pie']
            command += [str(source), str(out/'resolvers.cpp'), str(out/'main.cpp'), '-o', str(binary)]
            if Path(compiler).stem.lower() in ['cl', 'clang-cl']:
                if args.sanitize:
                    raise RuntimeError('Use GCC/Clang Unix-style driver for sanitizers')
                command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/MTd', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/I'+str(out)]
                if args.vulkan:
                    command += ['/DOPENQ4_RENDERER_VK_MODULE']
                command += [str(source), str(out/'resolvers.cpp'), str(out/'main.cpp'), '/Fe:'+str(binary), '/Fo:'+str(out)+os.sep]
            for stage, call in [('compile', command), ('run', [str(binary)])]:
                result = subprocess.run(call, cwd=out, env=env, timeout=120, capture_output=True, text=True)
                log = out/(tag+'-'+stage+'.log'); log.write_text(result.stdout+result.stderr)
                records.append({'case': tag, 'stage': stage, 'command': call, 'exit_code': result.returncode, 'expected_rejection': stage == 'run' and rejected, 'log': str(log), 'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest()})
                if stage == 'compile' and result.returncode or stage == 'run' and (result.returncode != 0) != rejected:
                    raise RuntimeError(str(log))
                if stage == 'run':
                    print(tag+': '+('rejected compiled mutation' if rejected else result.stdout.strip()))
        status = 'passed'
    finally:
        after = hashes()
        if before != after:
            status = 'source_changed'
        generated = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*') if p.suffix in ['.h', '.cpp', '.exe']}
        (out/'result.json').write_text(json.dumps({'status': status, 'files_before': before, 'files_after': after, 'generated': generated, 'records': records, 'limitations': __doc__}, indent=2)+'\n')
        print(out/'result.json')
    if status != 'passed':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
