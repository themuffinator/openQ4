#!/usr/bin/env python3
"""Actual private module services/unload with counted reentrant video callbacks.

Boot/reselection/probe tests execute their exact source entry guards, with an
effect marker replacing the unchanged loader tail. No DLL, native video, OS
input or game is used. Full translation units are separately compiled.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile
from renderer_consumed_policy import method
ROOT=Path(__file__).resolve().parents[2]
SUPPORT=r'''
#include "RendererModule.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
static int checks=0;
#define TEST(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(false)
struct idStr {static void Copynz(char* d,const char* s,int n){if(n>0)std::snprintf(d,n,"%s",s);}};
class idRenderSystem {public:bool IsOpenGLRunning(){return true;}} renderer;
static idRenderSystem* renderSystem=&renderer;
struct Module {bool interfacesPublished=true,moduleExportValid=true;renderExport_t moduleExport{};intptr_t moduleHandle=1;rendererModuleStatus_t status{};} rm_state;
static int restores=0,unloads=0,shutdowns=0,retains=0,releases=0,restarts=0,prepares=0,cancels=0,captures=0,completed=0,loaderEffects=0;
static std::function<void()> onRetain,onRelease;static bool retained=true;static uint64_t nextLease=0;
static renderImageRecoveryLease_t backendLease{};
static renderWindowServices_t services{};const renderWindowServices_t* Sys_GetRenderWindowServices(){return &services;}
using memModuleStats_t=void(*)();void Mem_UnregisterModuleStats(memModuleStats_t){}void* Sys_DLL_GetProcAddress(intptr_t,const char*){return nullptr;}
#define MEM_MODULE_STATS_ENTRY_POINT "test"
void Sys_DLL_Unload(intptr_t){++unloads;}void RM_RestorePublishedInterfaces(){++restores;}
bool Retain(){++retains;if(onRetain)onRetain();return retained;}void Release(){++releases;if(onRelease)onRelease();}
bool Prepare(uint64_t owner,uint64_t request,const char*,const renderImagePolicy_t*,renderImageRecoveryLease_t* out,char*,int){++prepares;backendLease={owner,request,++nextLease};*out=backendLease;return true;}
bool Cold(uint64_t owner,uint64_t request,const char*,uint32_t,const char*,uint32_t,renderImageRecoveryLease_t* out,char*,int){renderImagePolicy_t p{};return Prepare(owner,request,"",&p,out,nullptr,0);}
bool Capture(const renderImageRecoveryLease_t*,uint32_t,char* out,uint32_t capacity,uint32_t* n,char*,int){++captures;if(capacity<4)return false;std::memcpy(out,"data",4);*n=4;return true;}
bool Cancel(const renderImageRecoveryLease_t*,char*,int){++cancels;backendLease={};return true;}
bool Complete(const renderImageRecoveryLease_t*,uint32_t,const renderImagePolicyResult_t*,char*,int){++completed;backendLease={};return true;}
bool Restart(const renderImagePolicyRequest_t*,renderImagePolicyResult_t* out,char*,int){++restarts;out->attempt=10;out->deviceGeneration=20;return true;}
'''
MAIN=r'''
static void Reset(){
 onRetain={};onRelease={};retained=true;rm_state={};rm_state.interfacesPublished=rm_state.moduleExportValid=true;rm_state.moduleHandle=1;
 rm_state.moduleExport.PrepareImagePolicyRecovery=Prepare;rm_state.moduleExport.CaptureImagePolicyRecovery=Capture;rm_state.moduleExport.PrepareColdImagePolicyRecovery=Cold;
 rm_state.moduleExport.CancelImagePolicyRecovery=Cancel;rm_state.moduleExport.ReleaseImagePolicyRecovery=Complete;rm_state.moduleExport.TryImagePolicyRestart=Restart;
 rm_state.moduleExport.Shutdown=[](){++shutdowns;};
 services={};services.RetainVideoSystem=Retain;services.ReleaseVideoSystem=Release;
 services.ApplyScreenParmsStrict=[](const renderWindowRequest_t*,renderWindowState_t*,char*,int){return true;};services.QueryWindowState=[](renderWindowState_t*){return true;};
 rm_imageServiceBusy=rm_imageServiceFailed=false;rm_imageRecoveryLease={};rm_displayVideoPin=nullptr;rm_displayModuleEpoch=5;
 restores=unloads=shutdowns=retains=releases=restarts=prepares=cancels=captures=completed=loaderEffects=0;
}
int main(){try{
 char error[256]{};constexpr char id[]="0123456789abcdef0123456789abcdef";renderImagePolicy_t policy{};rendererImageRecoveryLease_t lease{},sentinel{88,{77,66,55}};
 Reset();TEST(R_RendererModule_PrepareImageRecovery(7,9,id,&policy,&lease,error,sizeof(error)));TEST(lease.moduleEpoch==5&&retains==1&&releases==1);
 char data[20]="untouched";uint32_t bytes=99;TEST(R_RendererModule_CaptureImageRecovery(&lease,1,data,20,&bytes,error,sizeof(error)));TEST(bytes==4&&!std::memcmp(data,"data",4));
 auto stale=lease;++stale.moduleEpoch;TEST(!R_RendererModule_CancelImageRecovery(&stale,error,sizeof(error)));TEST(cancels==0);
 TEST(R_RendererModule_CancelImageRecovery(&lease,error,sizeof(error)));TEST(cancels==1);
 for(int op=0;op<4;++op){Reset();onRetain=[&]{if(op==0)R_RendererModule_Shutdown();if(op==1)R_RendererModule_Boot();if(op==2)R_RendererModule_BootEarly();if(op==3)R_RendererModule_RunVulkanProbe(false);};
  lease=sentinel;TEST(!R_RendererModule_PrepareImageRecovery(7,9,id,&policy,&lease,error,sizeof(error)));TEST(lease.moduleEpoch==88&&lease.resources.preparation==55);
  TEST(!prepares&&!restores&&!unloads&&!shutdowns&&!loaderEffects&&rm_state.moduleHandle==1);
 }
 Reset();renderImagePolicyRequest_t request{};rendererImagePolicyResult_t result{},old{};old.moduleEpoch=123;result=old;
 onRetain=[](){R_RendererModule_Shutdown();};TEST(!R_RendererModule_TryImagePolicyRestart(&request,&result,error,sizeof(error)));TEST(!restarts&&!unloads&&result.moduleEpoch==123);
 Reset();onRelease=[](){R_RendererModule_Shutdown();};result=old;TEST(!R_RendererModule_TryImagePolicyRestart(&request,&result,error,sizeof(error)));TEST(restarts==1&&!unloads&&result.moduleEpoch==123);
 Reset();onRelease=[](){R_RendererModule_Shutdown();};lease=sentinel;TEST(!R_RendererModule_PrepareImageRecovery(7,9,id,&policy,&lease,error,sizeof(error)));TEST(prepares==1&&cancels==1&&!unloads&&lease.moduleEpoch==88);
 Reset();TEST(R_RendererModule_PrepareImageRecovery(7,9,id,&policy,&lease,error,sizeof(error)));onRelease=[](){R_RendererModule_Shutdown();};
 TEST(!R_RendererModule_CancelImageRecovery(&lease,error,sizeof(error)));TEST(!cancels&&backendLease.preparation);
 onRelease={};TEST(R_RendererModule_CancelImageRecovery(&lease,error,sizeof(error)));
 Reset();TEST(R_RendererModule_PrepareImageRecovery(7,9,id,&policy,&lease,error,sizeof(error)));rendererImagePolicyResult_t receipt{5,{}};
 onRelease=[](){R_RendererModule_Shutdown();};TEST(!R_RendererModule_ReleaseImageRecovery(&lease,2,&receipt,error,sizeof(error)));TEST(!completed&&backendLease.preparation);
 onRelease={};TEST(R_RendererModule_ReleaseImageRecovery(&lease,2,&receipt,error,sizeof(error)));TEST(completed==1);
 Reset();TEST(RM_UnloadModule());TEST(unloads==1&&restores==1&&shutdowns==1&&rm_state.moduleHandle==0);
 Reset();rm_imageServiceBusy=true;TEST(!RM_UnloadModule());TEST(rm_imageServiceFailed&&!unloads&&!restores&&rm_state.moduleHandle==1);
 std::printf("PASS %d module recovery checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %d %s\n",checks,e.what());return 1;}}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',default=shutil.which('clang++') or 'g++');p.add_argument('--sanitize',action='store_true');p.add_argument('--mutations',action='store_true');a=p.parse_args()
 out=Path(tempfile.mkdtemp(prefix='image-recovery-module-',dir=ROOT/'.tmp')).resolve();env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
 paths=[Path(__file__),ROOT/'src/renderer/RendererModule.cpp']+[ROOT/'src/renderer'/n for n in ['RendererModule.h','RenderModuleAPI.h','RendererResourceSettings.h','DisplayPresentation.h']]
 sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();record={'passed':False,'sources':{str(f.relative_to(ROOT)):sha(f) for f in paths},'runs':[],'scope':__doc__}
 try:
  src=paths[1].read_text();globals=src[src.index('static uint64_t rm_displayModuleEpoch'):src.index('// module binary short tags')]
  body=SUPPORT+globals+method(src,'static bool RM_UnloadModule(')+method(src,'void R_RendererModule_Shutdown(')
  for name,cut,tail in [('void R_RendererModule_Boot(','\trendererModuleStatus_t','++loaderEffects;\n}'),('void R_RendererModule_BootEarly(','\tRM_RegisterCommands','++loaderEffects;\n}'),('bool R_RendererModule_RunVulkanProbe(','\tchar modulePath','++loaderEffects;return true;\n}')]:
   part=method(src,name);body+=part[:part.index(cut)]+tail+'\n'
  body+=method(src,'bool R_RendererModule_TryImagePolicyRestart(')
  body+=src[src.index('namespace {\nbool RM_ImageError'):src.index('bool R_RendererModule_TryInitializeDisplay(')]
  cases=[('positive',body)]
  if a.mutations:
   for label,before,after in [
    ('unload-unpinned','if (!RM_AllowImageModuleChange()) return false;','if (false) return false;'),
    ('restart-retain-latch','if (rm_imageServiceFailed || epoch != rm_displayModuleEpoch','if (epoch != rm_displayModuleEpoch'),
    ('restart-release-latch','if (!succeeded || rm_imageServiceFailed) return false;','if (!succeeded) return false;'),
    ('service-retain-latch','if(!afterRelease&&!rm_imageServiceFailed&&epoch','if(!afterRelease&&epoch'),
    ('cleanup-before-release',',error,size,true))',',error,size))'),
   ]:
    count=body.count(before);assert count==(2 if label=='cleanup-before-release' else 1),(label,count)
    cases.append((label,body.replace(before,after)))
  for label,source in cases:
   cpp=out/(label+'.cpp');cpp.write_text(source+MAIN);exe=out/(label+'.exe')
   cmd=[a.compiler,'-std=c++20','-DOPENQ4_RENDERER_MODULE_ONLY','-I',ROOT/'src/renderer',cpp,'-o',exe]
   if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0','-no-pie']
   if Path(a.compiler).stem in ('cl','clang-cl'):cmd=[a.compiler,'/nologo','/std:c++20','/EHsc','/MTd','/D_DEBUG','/DOPENQ4_RENDERER_MODULE_ONLY','/I'+str(ROOT/'src/renderer'),cpp,'/Fe:'+str(exe),'/Fo:'+str(out)+os.sep]
   for name,cmd in [('compile',cmd),('run',[exe])]:
    r=subprocess.run([str(v) for v in cmd],cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120);log=out/(label+'-'+name+'.log');log.write_text(r.stdout)
    record['runs'].append({'case':label,'name':name,'exit':r.returncode,'log':str(log),'command':[str(v) for v in cmd]});print(label,name,r.returncode,r.stdout[-1000:],flush=True)
    if (name=='compile' and r.returncode) or (name=='run' and ((label=='positive')!=(r.returncode==0))):raise RuntimeError(str(log))
  record['passed']=True
 finally:
  record['source_unchanged']=all(sha(ROOT/n)==v for n,v in record['sources'].items());record['artifacts']={str(f):sha(f) for f in out.iterdir() if f.is_file()}
  (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(out/'result.json')
if __name__=='__main__':main()
