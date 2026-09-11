#!/usr/bin/env python3
"""Execute the exact ActuallyLoadImage prepared entry against counted native calls.

The unchanged ordinary file-selection tail is represented by a marker. This
checks the new branch's actual publication/order, borrowed CPU lifetime, upload
arguments and absence of fallback; it is not a driver or ordinary-loader test.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile
from renderer_consumed_policy import method
ROOT=Path(__file__).resolve().parents[2]
SUPPORT=r'''
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include "RendererConsumedPolicy.h"
static int checks=0,ordinary=0,borrowed=0,allocations=0,uploads=0,errors=0,observed=0,loaded=0,destructors=0,generators=0;
static bool allowed=true,mutationAllowed=true,uses=true,acceptObservation=true,failBorrow=false,throwBorrow=false,context=true;
static int failUpload=-1;
#define TEST(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(false)
using textureType_t=int;using textureFormat_t=int;using textureColor_t=int;
struct bimageFile_t{int textureType=1,format=7,colorFormat=0,width=8,height=8,numLevels=4;};
struct bimageImage_t{int level=0,destZ=0,width=0,height=0;};
class idBinaryImage {
public:bimageFile_t header{};std::array<bimageImage_t,4> parts{};std::array<std::array<unsigned char,32>,4> data{};
 ~idBinaryImage(){++destructors;}
 const bimageFile_t& GetFileHeader()const{return header;}int NumImages()const{return 4;}
 const bimageImage_t& GetImageHeader(int n)const{return parts.at(n);}const unsigned char* GetImageData(int n)const{return data.at(n).data();}
};
static idBinaryImage binary;
bool R_ImagePolicyOperationAllowed(){return allowed;}bool R_ImagePolicyContentMutation(){return mutationAllowed;}
bool R_ImagePolicyUsesPreparedContent(){return uses;}void R_ImagePolicyObserveError(const char*){++errors;allowed=false;}
struct Renderer{bool IsOpenGLRunning(){return context;}}tr;
class idImage {
public:bimageFile_t opts{9,9,9,9,9,9};bool defaulted=true;std::string loadedSourceName="original";void(*generatorFunction)(idImage*)=nullptr;
 const char* GetName()const{return "textures/input";}void ActuallyLoadImage(bool);
 void AllocImage(){++allocations;if(!allowed)return;TEST(opts.width==8&&opts.height==8&&opts.numLevels==4&&opts.format==7&&opts.textureType==1&&opts.colorFormat==0);}
 void SubImageUpload(int level,int x,int y,int side,int width,int height,const unsigned char* data){
  if(!allowed)return;TEST(x==0&&y==0&&side==0&&level==uploads&&width==(8>>level)&&height==(8>>level)&&data==binary.data[level].data());
  if(uploads==failUpload){R_ImagePolicyObserveError("injected native refusal");return;}++uploads;
 }
};
imageConsumedLoad_t::imageConsumedLoad_t(idImage& i):image(i){}imageConsumedLoad_t::~imageConsumedLoad_t(){}
bool imageConsumedLoad_t::Prepared(const imagePortableContent_t& d){TEST(d.version==1&&d.scope==IPC_DIRECT_SOURCE);++observed;return acceptObservation;}
void imageConsumedLoad_t::Content(const idBinaryImage& b,imageConsumedSource_t source){TEST(&b==&binary&&source==ICS_DIRECT_DDS);}
void imageConsumedLoad_t::Loaded(imageConsumedSource_t source){TEST(source==ICS_DIRECT_DDS);++::loaded;}
int R_ImagePolicyBorrowPreparedContent(const idImage*,const idBinaryImage*& out,imagePortableContent_t& d){
 ++borrowed;if(throwBorrow)throw std::bad_alloc();if(failBorrow)return -1;
 d.version=1;d.scope=IPC_DIRECT_SOURCE;std::memcpy(d.file.qpath,"textures/owned.dds",19);out=&binary;return 1;
}
'''
MAIN=r'''
static void Reset(){ordinary=borrowed=allocations=uploads=errors=observed=loaded=destructors=generators=0;
 allowed=mutationAllowed=uses=acceptObservation=context=true;failBorrow=throwBorrow=false;failUpload=-1;
 for(int i=0;i<4;++i){binary.parts[i]={i,0,8>>i,8>>i};binary.data[i].fill(static_cast<unsigned char>(21+i));}}
int main(){try{
 Reset();const auto pristine=binary.data;idImage image;image.ActuallyLoadImage(false);
 TEST(borrowed==1&&allocations==1&&uploads==4&&!ordinary&&!errors&&loaded==1&&!image.defaulted&&image.loadedSourceName=="textures/owned.dds");TEST(binary.data==pristine&&!destructors);
 for(int mode=0;mode<6;++mode){Reset();idImage i;
  if(mode==0)allowed=false;if(mode==1)mutationAllowed=false;if(mode==2)context=false;if(mode==3)failBorrow=true;if(mode==4)throwBorrow=true;if(mode==5)acceptObservation=false;
  i.ActuallyLoadImage(false);TEST(!allocations&&!uploads&&!ordinary&&i.opts.width==9&&i.defaulted&&i.loadedSourceName=="original"&&binary.data==pristine&&!destructors);
 }
 for(int n=0;n<4;++n){Reset();failUpload=n;idImage i;i.ActuallyLoadImage(false);TEST(uploads==n&&errors==1&&!ordinary&&binary.data==pristine&&!destructors);}
 Reset();uses=false;idImage other;other.ActuallyLoadImage(false);TEST(ordinary==1&&!borrowed&&!allocations);
 Reset();other.generatorFunction=[](idImage*){++generators;};other.ActuallyLoadImage(false);TEST(generators==1&&!borrowed&&!ordinary&&!allocations);
 std::printf("PASS %d actual loader-entry checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %d %s\n",checks,e.what());return 1;}}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',default=shutil.which('clang++') or 'g++');p.add_argument('--sanitize',action='store_true');p.add_argument('--mutations',action='store_true');a=p.parse_args()
 out=Path(tempfile.mkdtemp(prefix='image-recovery-loader-',dir=ROOT/'.tmp')).resolve();env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
 paths=[Path(__file__),ROOT/'src/renderer/Image_load.cpp',ROOT/'src/renderer/RendererConsumedPolicy.h',ROOT/'src/imagetools/ImageContentIdentity.h']
 sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();record={'passed':False,'sources':{str(f.relative_to(ROOT)):sha(f) for f in paths},'runs':[],'scope':__doc__}
 try:
  part=method(paths[1].read_text(),'void idImage::ActuallyLoadImage(');part=part[:part.index('\tconst imageDownsizePolicy_t& consumedDownsize')]+ '\n++ordinary;\n}\n'
  cases=[('positive',part)]
  if a.mutations:
   for label,before,after in [('fallback','if (selected<0) return;','if (selected<0) {++ordinary;return;}'),('width','opts.width=h.width;','opts.width=9;'),('data','prepared->GetImageData(i)','prepared->GetImageData(0)'),('source','loadedSourceName=descriptor.file.qpath;','loadedSourceName="wrong";'),('borrow-skip','if (R_ImagePolicyUsesPreparedContent()) try {','if (false) try {')]:
    assert part.count(before)==1,label;cases.append((label,part.replace(before,after)))
  for label,body in cases:
   cpp=out/(label+'.cpp');cpp.write_text(SUPPORT+body+MAIN);exe=out/(label+'.exe')
   cmd=[a.compiler,'-std=c++20','-I',ROOT/'src/renderer',cpp,'-o',exe]
   if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0','-no-pie']
   if Path(a.compiler).stem in ('cl','clang-cl'):cmd=[a.compiler,'/nologo','/std:c++20','/EHsc','/MTd','/D_DEBUG','/I'+str(ROOT/'src/renderer'),cpp,'/Fe:'+str(exe),'/Fo:'+str(out)+os.sep]
   for name,cmd in [('compile',cmd),('run',[exe])]:
    r=subprocess.run([str(v) for v in cmd],cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120);log=out/(label+'-'+name+'.log');log.write_text(r.stdout)
    record['runs'].append({'case':label,'name':name,'exit':r.returncode,'log':str(log),'command':[str(v) for v in cmd]});print(label,name,r.returncode,r.stdout[-500:],flush=True)
    if (name=='compile' and r.returncode) or (name=='run' and ((label=='positive')!=(r.returncode==0))):raise RuntimeError(str(log))
  record['passed']=True
 finally:
  record['source_unchanged']=all(sha(ROOT/n)==v for n,v in record['sources'].items());record['artifacts']={str(f):sha(f) for f in out.iterdir() if f.is_file()}
  (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(out/'result.json')
if __name__=='__main__':main()
