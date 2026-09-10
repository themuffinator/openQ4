#!/usr/bin/env python3
"""Actual resource constructors/destructors and token primitives in separate TUs.

Field storage/CommonInit are stand-ins; actual constructor statements and
renderer-side allocation primitives are compiled. No engine, GPU or input.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile
from renderer_image_policy_boundaries import method
ROOT=Path(__file__).resolve().parents[2]
SUPPORT=r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <new>
uint64_t R_ImagePolicyNewResourceIdentity() noexcept;
void R_ImagePolicyResourceDestroyed() noexcept;
void R_ImagePolicyLifecycleChanged() noexcept;
uint64_t ReadEpoch();void ExhaustIdentity();void ExhaustEpoch();
bool R_ImagePolicyContentMutation();void PermitContent(bool);int ContentCalls();int Mutations();
struct idStr {const char* value;idStr(const char* p=""):value(p){}void Clear(){value="";}};
enum {TF_DEFAULT,TR_REPEAT,TD_DEFAULT,CF_2D,FILE_NOT_FOUND_TIMESTAMP=-1};
class idImage {
public:
 idImage(const char* name);
 ~idImage();
 uint64_t GetImagePolicyIdentity()const{return imagePolicyIdentity;}
private:
 idStr imgName;
 const uint64_t imagePolicyIdentity;
 unsigned texnum,internalFormat,dataFormat,dataType;uint64_t storageGeneration;
 void(*generatorFunction)(idImage*);int filter,repeat,usage,cubeFiles,flags;
 bool allowDownSize,referencedOutsideLevelLoad,levelLoadReferenced,defaulted,scratchImage;
 int sourceFileTime,binaryFileTime,refCount,useCount;idStr loadedSourceName;
 static constexpr unsigned TEXTURE_NOT_LOADED=0xffffffff;
};
class idMaterial {
public:
 idMaterial();~idMaterial();
 void FreeData();bool Parse(const char*,int);
 uint64_t GetImagePolicyIdentity()const{return imagePolicyIdentity;}
 void CommonInit(){surfaceArea=99;}
private:const uint64_t imagePolicyIdentity;float surfaceArea;
};
class idImageManager {public:void Init();void Shutdown();void BeginLevelLoad();};
'''
MAIN=r'''
#include "support.h"
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>
#include <stdexcept>
#include <cstdlib>
static bool deny=false;static int allocations=0,checks=0;
void* operator new(size_t n){++allocations;if(deny)throw std::bad_alloc();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,size_t)noexcept{std::free(p);}
#define TEST(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(0)
extern idImage earlyImage;extern idMaterial earlyMaterial;
int main(){try{
 TEST(ContentCalls()==0); // Constructors have no coordinator/engine callbacks.
 TEST(earlyImage.GetImagePolicyIdentity()!=0);TEST(earlyMaterial.GetImagePolicyIdentity()!=0);
 TEST(earlyImage.GetImagePolicyIdentity()!=earlyMaterial.GetImagePolicyIdentity());
 alignas(idImage) unsigned char imageStorage[sizeof(idImage)];
 auto* image=new(imageStorage)idImage("same");auto original=image->GetImagePolicyIdentity();auto epoch=ReadEpoch();image->~idImage();
 TEST(ReadEpoch()!=epoch);image=new(imageStorage)idImage("same");TEST(image->GetImagePolicyIdentity()!=original);image->~idImage();
 alignas(idMaterial) unsigned char materialStorage[sizeof(idMaterial)];
 auto* material=new(materialStorage)idMaterial;original=material->GetImagePolicyIdentity();epoch=ReadEpoch();material->CommonInit();TEST(material->GetImagePolicyIdentity()==original);
 material->~idMaterial();TEST(ReadEpoch()!=epoch);material=new(materialStorage)idMaterial;TEST(material->GetImagePolicyIdentity()!=original);material->~idMaterial();
 {idMaterial m;PermitContent(false);int changes=Mutations();m.FreeData();TEST(!m.Parse("same source",11));TEST(Mutations()==changes);
  PermitContent(true);epoch=ReadEpoch();m.FreeData();TEST(m.Parse("same source",11));TEST(Mutations()==changes+2&&ReadEpoch()!=epoch);}
 {idImageManager manager;epoch=ReadEpoch();manager.Init();TEST(ReadEpoch()!=epoch);epoch=ReadEpoch();manager.Shutdown();TEST(ReadEpoch()!=epoch);epoch=ReadEpoch();manager.BeginLevelLoad();TEST(ReadEpoch()!=epoch);}
 epoch=ReadEpoch();const int before=allocations;deny=true;original=R_ImagePolicyNewResourceIdentity();R_ImagePolicyLifecycleChanged();deny=false;
 TEST(allocations==before&&original!=0&&ReadEpoch()!=epoch);
 uint64_t ids[800]{};std::thread workers[8];
 for(int t=0;t<8;++t)workers[t]=std::thread([&,t]{for(int i=0;i<100;++i)ids[t*100+i]=R_ImagePolicyNewResourceIdentity();});
 for(auto& thread:workers)thread.join();std::sort(std::begin(ids),std::end(ids));
 for(int i=0;i<800;++i){TEST(ids[i]!=0);if(i)TEST(ids[i]!=ids[i-1]);}
 ExhaustIdentity();TEST(R_ImagePolicyNewResourceIdentity()==UINT64_MAX);TEST(R_ImagePolicyNewResourceIdentity()==0);TEST(R_ImagePolicyNewResourceIdentity()==0);
 ExhaustEpoch();R_ImagePolicyLifecycleChanged();TEST(ReadEpoch()==0);R_ImagePolicyResourceDestroyed();TEST(ReadEpoch()==0);
 std::printf("actual resource constructors / separate-TU identity: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){deny=false;std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--repository',type=Path,default=ROOT);parser.add_argument('--sanitize',action='store_true');parser.add_argument('--mutations',action='store_true');args=parser.parse_args()
 root=args.repository.resolve();paths=[root/name for name in ['src/renderer/Image.h','src/renderer/Material.h','src/renderer/Material.cpp','src/renderer/RendererResourceSettings.cpp','tools/tests/renderer_image_policy_lifetimes.py','tools/tests/renderer_image_policy_boundaries.py','src/renderer/ImageManager.cpp']]
 def hashes():return {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
 before=hashes();image=paths[0].read_text();material_h=paths[1].read_text();material=paths[2].read_text();source=paths[3].read_text()
 for typename,text in [('idImage',image),('idMaterial',material_h)]:
  assert f'{typename}(const {typename}&) = delete;' in text and f'{typename}& operator=(const {typename}&) = delete;' in text
  assert 'const uint64_t' in text and 'imagePolicyIdentity' in text
 ctor=method(image,'ID_INLINE idImage::idImage(').replace('ID_INLINE ','')
 destructor=method(image,'~idImage()').replace('~idImage()', 'idImage::~idImage()',1)
 ctors='#include "support.h"\n'+ctor+'\n'+destructor+'\n'+method(material,'idMaterial::idMaterial()')+'\n'+method(material,'idMaterial::~idMaterial()')+'\n idImage earlyImage("before-provider");idMaterial earlyMaterial;\n'
 ctors+='\nstatic bool permit=true;static int contentCalls=0,mutations=0;bool R_ImagePolicyContentMutation(){++contentCalls;R_ImagePolicyLifecycleChanged();return permit;}void PermitContent(bool value){permit=value;}int ContentCalls(){return contentCalls;}int Mutations(){return mutations;}\n'
 for signature,guard,tail in [('void idMaterial::FreeData()', 'if ( !R_ImagePolicyContentMutation() ) return;', '++mutations;'),('bool idMaterial::Parse( const char *text, const int textLength )', 'if ( !R_ImagePolicyContentMutation() ) return false;', '++mutations;return true;')]:
  body=material[material.index(signature):];brace=body.index('{');assert body[brace+1:].lstrip().startswith(guard)
  ctors+=body[:body.index(guard)+len(guard)]+'\n'+tail+'\n}\n'
 manager=paths[-1].read_text()
 for signature in ['void idImageManager::Init()', 'void idImageManager::Shutdown()', 'void idImageManager::BeginLevelLoad()']:
  body=manager[manager.index(signature):];brace=body.index('{');guard='R_ImagePolicyLifecycleChanged();';assert body[brace+1:].lstrip().startswith(guard)
  ctors+=body[:body.index(guard)+len(guard)]+'\n++mutations;\n}\n'
 provider='#include <atomic>\n#include "support.h"\n'
 for name,value in [('resourceIdentityCounter','0'),('resourceMutationEpoch','1')]:
  declaration=f'constinit std::atomic<uint64_t> {name}{{{value}}};';assert declaration in source;provider+=declaration+'\n'
 provider+='\n'.join(method(source,name) for name in ['void AdvanceMutationEpoch()','uint64_t R_ImagePolicyNewResourceIdentity()','void R_ImagePolicyResourceDestroyed()','void R_ImagePolicyLifecycleChanged()'])
 provider+='\nuint64_t ReadEpoch(){return resourceMutationEpoch.load();}void ExhaustIdentity(){resourceIdentityCounter=UINT64_MAX-1;}void ExhaustEpoch(){resourceMutationEpoch=UINT64_MAX;}\n'
 compiler=next((p for n in ['clang++','g++','c++'] if(p:=shutil.which(n))),None);assert compiler
 out=Path(tempfile.mkdtemp(prefix='image-lifetimes-',dir=root/'.tmp'));env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
 for name,text in [('support.h',SUPPORT),('main.cpp',MAIN)]: (out/name).write_text(text)
 records=[];status='running'
 cases=[('constructors',('constructors.cpp','provider.cpp'),ctors,provider,False),('provider',('provider.cpp','constructors.cpp'),ctors,provider,False)]
 if args.mutations:
  cases += [('reused-id',('constructors.cpp','provider.cpp'),ctors,provider.replace('return old + 1;', 'return 1;',1),True),
   ('missing-destroy',('constructors.cpp','provider.cpp'),ctors,provider.replace('void R_ImagePolicyResourceDestroyed() noexcept { AdvanceMutationEpoch(); }','void R_ImagePolicyResourceDestroyed() noexcept {}'),True),
   ('missing-lifecycle',('constructors.cpp','provider.cpp'),ctors,provider.replace('void R_ImagePolicyLifecycleChanged() noexcept { AdvanceMutationEpoch(); }','void R_ImagePolicyLifecycleChanged() noexcept {}'),True),
   ('missing-constructor-id',('constructors.cpp','provider.cpp'),ctors.replace('imagePolicyIdentity(R_ImagePolicyNewResourceIdentity())','imagePolicyIdentity(0)',1),provider,True),
   ('unguarded-material',('constructors.cpp','provider.cpp'),ctors.replace('if ( !R_ImagePolicyContentMutation() ) return;','(void)0;',1),provider,True)]
 try:
  for tag,order,constructor_source,provider_source,rejected in cases:
   case=out/tag;case.mkdir();(case/'constructors.cpp').write_text(constructor_source);(case/'provider.cpp').write_text(provider_source)
   generated={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [case/'constructors.cpp',case/'provider.cpp',out/'main.cpp',out/'support.h']}
   binary=out/(tag+'.exe');command=[compiler,'-std=c++20','-pthread']
   if args.sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
   command += ['-I',str(out)]+[str(case/name) for name in order]+[str(out/'main.cpp'),'-o',str(binary)]
   for stage,call in [('compile',command),('run',[str(binary)])]:
    result=subprocess.run(call,cwd=out,env=env,timeout=90,capture_output=True,text=True);log=out/(tag+'-'+stage+'.log');log.write_text(result.stdout+result.stderr)
    records.append({'generated':generated,'stage':stage,'command':call,'exit_code':result.returncode,'log':str(log),'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
    if stage=='compile' and result.returncode:raise RuntimeError(str(log))
    if stage=='run':
     if (result.returncode!=0)!=rejected:raise RuntimeError(str(log))
     records[-1]['expected_rejection']=rejected
     print(tag+': '+('rejected compiled mutation' if rejected else result.stdout.strip()))
  status='passed'
 finally:
  after=hashes();status='source_changed' if before!=after else status if status=='passed' else 'failed'
  (out/'result.json').write_text(json.dumps({'status':status,'files_before':before,'files_after':after,'records':records,'limitations':__doc__},indent=2)+'\n');print(out/'result.json')
 if status!='passed':raise SystemExit(1)
if __name__=='__main__':main()
