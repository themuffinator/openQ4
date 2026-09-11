#!/usr/bin/env python3
"""Actual recovery coordinator + codec with counted engine/VFS/GPU boundaries.

The existing image content suite separately executes the actual DDS/cache CPU
read/hash/reconstruction. This suite proves owner/order/publication behavior;
it uses no module binary, OS device, game, input or real file-system VFS.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile
import renderer_image_policy_restart as old
ROOT=Path(__file__).resolve().parents[2]


MAIN=r'''
static constexpr char attempt[]="0123456789abcdef0123456789abcdef";
static renderImageRecoveryLease_t lease{};
static char error[256];
static void Seed(){cpuPayload=56;Reset();preparedRecovery.reset();sourceRefused=false;cpuReads=cpuTakes=0;lease={};error[0]=0;}
static bool Prepare(){const auto target=ReadPolicy();return R_PrepareImagePolicyRecovery(7,9,attempt,&target,&lease,error,sizeof(error));}
int main(){try{
 std::memset(&sentinel,0x6b,sizeof(sentinel));
 Seed();TEST(Prepare());TEST(lease.owner==7&&lease.request==9&&lease.preparation);TEST(!starts&&!teardowns&&cpuReads>=3&&preparedRecovery);
 std::vector<char> raw(1152*1024);uint32_t size=77;TEST(R_CaptureImagePolicyRecovery(&lease,1,raw.data(),uint32_t(raw.size()),&size,error,sizeof(error)));TEST(size>88);
 const std::string saved(raw.data(),size);uint32_t keep=size;TEST(!R_CaptureImagePolicyRecovery(&lease,1,raw.data(),size-1,&keep,error,sizeof(error)));TEST(keep==size);
 auto stale=lease;++stale.owner;TEST(!R_CancelPreparedImagePolicyRecovery(&stale,error,sizeof(error)));TEST(preparedRecovery);
 request.recovery=lease;request.recoveryDirection=2;renderImagePolicyResult_t result=sentinel;TEST(R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));
 TEST(cpuTakes==1&&starts==1&&preparedRecovery);TEST(!R_CancelPreparedImagePolicyRecovery(&lease,error,sizeof(error)));TEST(R_ReleaseCompletedImagePolicyRecovery(&lease,2,&result,error,sizeof(error)));TEST(!preparedRecovery);
 Seed();TEST(R_PrepareColdImagePolicyRecovery(7,10,attempt,1,saved.data(),uint32_t(saved.size()),&lease,error,sizeof(error)));
 request.recovery=lease;request.recoveryDirection=1;TEST(R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));TEST(cpuTakes==1);
 TEST(R_ReleaseCompletedImagePolicyRecovery(&lease,1,&result,error,sizeof(error)));
 Seed();TEST(Prepare());request.recovery=lease;request.recoveryDirection=2;sourceRefused=true;result=sentinel;const int retainedReads=cpuReads;
 TEST(R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));TEST(starts==1&&cpuReads==retainedReads);
 TEST(R_ReleaseCompletedImagePolicyRecovery(&lease,2,&result,error,sizeof(error)));
 Seed();TEST(Prepare());TEST(!R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));TEST(!starts);TEST(R_CancelPreparedImagePolicyRecovery(&lease,error,sizeof(error)));
 for(const char* where:{"read","source","complete"}){Seed();callback=[&](const char* at){if(!std::strcmp(at,where))R_ImagePolicyLifecycleChanged();};
  const renderImageRecoveryLease_t pristine{11,12,13};lease=pristine;TEST(!Prepare());TEST(!std::memcmp(&lease,&pristine,sizeof(lease)));TEST(!preparedRecovery&&!starts&&!teardowns);}
 Seed();callback=[](const char* at){if(!std::strcmp(at,"read"))TEST(!R_ImagePolicyContentMutation());};TEST(!Prepare());TEST(!preparedRecovery&&!starts);
 Seed();image.observable=false;TEST(!Prepare());TEST(!cpuReads&&!starts);
 Seed();image.file=false;TEST(!Prepare());TEST(!cpuReads&&!starts);
 Seed();image.defaulted=true;TEST(!Prepare());TEST(!cpuReads&&!starts);
 Seed();material.state=DS_DEFAULTED;TEST(!Prepare());TEST(!cpuReads&&!starts);
 Seed();sourceRefused=true;TEST(!R_PrepareColdImagePolicyRecovery(7,9,attempt,1,saved.data(),uint32_t(saved.size()),&lease,error,sizeof(error)));TEST(!starts&&!preparedRecovery);
 Seed();material.text="{ changed source }";TEST(!R_PrepareColdImagePolicyRecovery(7,9,attempt,1,saved.data(),uint32_t(saved.size()),&lease,error,sizeof(error)));TEST(!cpuReads&&!starts);
 Seed();TEST(Prepare());R_ImagePolicyLifecycleChanged();request.recovery=lease;request.recoveryDirection=2;TEST(!R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));TEST(!starts);
 TEST(!R_CancelPreparedImagePolicyRecovery(&lease,error,sizeof(error)));
 Seed();auto target=ReadPolicy();target.downSize=1;target.downSizeLimit=4;TEST(R_PrepareImagePolicyRecovery(7,9,attempt,&target,&lease,error,sizeof(error)));
 const int beforeReads=cpuReads;image_downSize.value=1;image_downSizeLimit.value=4;request.expectedCurrent=target;request.recovery=lease;request.recoveryDirection=2;
 uploadFailure=true;result=sentinel;TEST(!R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));TEST(!std::memcmp(&result,&sentinel,sizeof(result)));
 TEST(!R_CancelPreparedImagePolicyRecovery(&lease,error,sizeof(error)));TEST(preparedRecovery&&preparedRecovery->cpu[0].size()==1&&preparedRecovery->cpu[1].size()==1);
 uploadFailure=false;sourceRefused=true;image_downSize.value=image_downSizeLimit.value=0;request.expectedCurrent=ReadPolicy();request.recoveryDirection=1;
 TEST(R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));TEST(cpuReads==beforeReads&&image.opts.width==8&&image.opts.height==8&&image.opts.numLevels==4);
 auto forged=result;++forged.uploads;TEST(!R_ReleaseCompletedImagePolicyRecovery(&lease,1,&forged,error,sizeof(error)));TEST(preparedRecovery);
 TEST(R_ReleaseCompletedImagePolicyRecovery(&lease,1,&result,error,sizeof(error)));

 for(int kind=0;kind<3;++kind){Seed();TEST(Prepare());auto mismatch=lease;if(kind==0)++mismatch.owner;if(kind==1)++mismatch.request;if(kind==2)++mismatch.preparation;
  request.recovery=mismatch;request.recoveryDirection=2;result=sentinel;TEST(!R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));TEST(!starts&&!std::memcmp(&result,&sentinel,sizeof(result)));}
 Seed();TEST(Prepare());image_downSize.value=1;TEST(!R_CancelPreparedImagePolicyRecovery(&lease,error,sizeof(error)));TEST(preparedRecovery);image_downSize.value=0;TEST(R_CancelPreparedImagePolicyRecovery(&lease,error,sizeof(error)));
 Seed();TEST(Prepare());material.text="{ replacement }";request.recovery=lease;request.recoveryDirection=2;TEST(!R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));TEST(!starts);
 Seed();TEST(Prepare());request.recovery=lease;request.recoveryDirection=2;uploadFailure=true;TEST(!R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));
 request.recoveryDirection=1;TEST(!R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));const int repeatedReads=cpuReads;
 uploadFailure=false;sourceRefused=true;TEST(R_TryImagePolicyRestart(&request,&result,error,sizeof(error)));TEST(cpuReads==repeatedReads);
 TEST(R_ReleaseCompletedImagePolicyRecovery(&lease,1,&result,error,sizeof(error)));

 Seed();cpuPayload=300ull*1024*1024;image.portable=Content();TEST(!Prepare());TEST(!preparedRecovery&&!starts&&!teardowns&&cpuReads==2);
 Seed();image.portable.binary.payloadBytes=513ull*1024*1024;TEST(!Prepare());TEST(!cpuReads&&!starts&&!preparedRecovery);
 Seed();image.portable.file.bytes=65ull*1024*1024;TEST(!Prepare());TEST(!cpuReads&&!starts&&!preparedRecovery);
 bool success=false;
 for(int point=0;point<3000;++point){Seed();const renderImageRecoveryLease_t pristine{11,12,13};lease=pristine;allocationCalls=0;allocationBudget=point;
  bool ok=false;try{ok=Prepare();}catch(...){allocationBudget=-1;throw;}const int calls=allocationCalls;allocationBudget=-1;
  if(ok){TEST(calls<=point&&preparedRecovery);success=true;std::printf("full preparation allocation sweep: %d\n",point);break;}
  TEST(!std::memcmp(&lease,&pristine,sizeof(lease)));TEST(!preparedRecovery&&!active&&!starts&&!teardowns);
 }
 TEST(success);std::printf("PASS %d owned recovery checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %d: %s (%s)\n",checks,e.what(),error);return 1;}}
'''

def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',default=shutil.which('clang++') or 'g++');p.add_argument('--sanitize',action='store_true');p.add_argument('--mutations',action='store_true');a=p.parse_args()
 out=Path(tempfile.mkdtemp(prefix='image-recovery-owner-',dir=ROOT/'.tmp')).resolve();env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
 paths=[Path(__file__),Path(old.__file__),ROOT/'tools/tests/renderer_image_recovery_fixture.py']+[ROOT/'src'/n for n in ('renderer/RendererResourceSettings.cpp','renderer/RendererResourceSettings.h','renderer/RendererImageRecovery.cpp','renderer/RendererImageRecovery.h','renderer/RendererConsumedPolicy.h','renderer/RenderModuleAPI.h','imagetools/ImageRecoveryEnvelope.h','imagetools/ImageContentIdentity.h','imagetools/ImageContentIdentity.cpp','idlib/CryptoHash.cpp','idlib/CryptoHash.h')]
 sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();record={'passed':False,'sources':{str(f.relative_to(ROOT)):sha(f) for f in paths},'runs':[],'scope':__doc__}
 try:
  src=(ROOT/'src/renderer/RendererResourceSettings.cpp').read_text();src='\n'.join(l for l in src.splitlines() if not l.startswith('#include "') and not l.startswith('#pragma hdrstop'))
  mainpart=old.MAIN[:old.MAIN.index('int main()')]
  cases=[('positive',src)]
  if a.mutations:
   changes=[
    ('combined-cpu-budget','i.content.binary.payloadBytes>512ull*1024*1024-total','false'),
    ('source-read-budget','entry.content.file.bytes>MaxSourceFileBytes','false'),
    ('owner','a.owner==b.owner','true'),
    ('request','a.request==b.request','true'),
    ('recapture-after-write','candidate.selectedContent=candidate.prepared->selected[immutable.recoveryDirection-1];','if(!StageImages(candidate,selected,error,size))return false; candidate.selectedContent=candidate.prepared->selected[immutable.recoveryDirection-1];'),
    ('forget-cpu','prepared->cpu[0].swap(a.cpu);','a.cpu.clear();'),
    ('cancel-touched','||preparedRecovery->touched||\n            preparedRecovery->epoch','||false||\n            preparedRecovery->epoch'),
    ('cancel-epoch','preparedRecovery->epoch!=resourceMutationEpoch.load(std::memory_order_relaxed)||!SamePolicy(preparedRecovery->initialPolicy','false||!SamePolicy(preparedRecovery->initialPolicy'),
    ('material-identity','if(!ir::SameMaterial(current.materials[n],saved.materials[n]))','if(false)'),
    ('release-receipt','receipt.uploads!=exact.uploads','false'),
    ('copy-result-early','const renderImagePolicyRequest_t immutable = *request;','const renderImagePolicyRequest_t immutable = *request; *output={};'),
   ]
   for label,before,after in changes:
    assert src.count(before)==1,(label,src.count(before));cases.append((label,src.replace(before,after)))
  for label,body in cases:
   support=old.SUPPORT.replace('imagePortableContent_t Content(){','static uint64_t cpuPayload=56;\nimagePortableContent_t Content(){').replace('c.binary.payloadBytes=56;','c.binary.payloadBytes=cpuPayload;')
   source=support+body+mainpart+MAIN;cpp=out/(label+'.cpp');cpp.write_text(source)
   exe=out/(label+'.exe');common=[cpp,ROOT/'src/renderer/RendererImageRecovery.cpp',ROOT/'src/imagetools/ImageContentIdentity.cpp',ROOT/'src/idlib/CryptoHash.cpp']
   include=[ROOT/'src/renderer',ROOT.parent.parent/'src/renderer'];msvc=Path(a.compiler).stem in ('cl','clang-cl')
   flags=[a.compiler,'-std=c++20','-pthread']+sum((['-I',str(v)] for v in include),[])
   if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0','-no-pie']
   cmd=flags+common+['-o',exe]
   if msvc:cmd=[a.compiler,'/nologo','/std:c++20','/EHsc','/MTd','/D_DEBUG']+['/I'+str(v) for v in include]+common+['/Fe:'+str(exe),'/Fo:'+str(out)+os.sep]
   for name,cmd in [('compile',cmd),('run',[exe])]:
    r=subprocess.run([str(v) for v in cmd],cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120);log=out/(label+'-'+name+'.log');log.write_text(r.stdout)
    record['runs'].append({'case':label,'name':name,'exit':r.returncode,'log':str(log),'command':[str(v) for v in cmd]});print(label,name,r.returncode,r.stdout[-1000:],flush=True)
    if (name=='compile' and r.returncode) or (name=='run' and ((label=='positive')!=(r.returncode==0))):raise RuntimeError(str(log))
  record['passed']=True
 finally:
  record['source_unchanged']=all(sha(ROOT/n)==v for n,v in record['sources'].items());record['artifacts']={str(f):sha(f) for f in out.iterdir() if f.is_file()}
  (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(out/'result.json')
if __name__=='__main__':main()
