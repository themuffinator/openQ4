#!/usr/bin/env python3
"""Actual existing-image metadata entry paths with the real recovery coordinator.

Name normalization/hash storage and engine/GPU calls are counted stand-ins.
The actual ImageFromFile/ImageHandleDeferred/ScratchImage, sampler, options,
scratch upload, default generation and GL/VK Resize bodies execute. Frame/depth
copy metadata prefixes execute up to a counted native-copy continuation. No device/game/input runs.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, tempfile
import renderer_image_policy_restart as base
from renderer_image_policy_boundaries import method

ROOT = Path(__file__).resolve().parents[2]

STR = r'''
 std::string value{""};idStr(const char* text=""):value(text){}
 operator const char*()const{return value.c_str();}const char* c_str()const{return value.c_str();}
 char operator[](size_t n)const{return value[n];}
 static int Icmp(const char* a,const char* b){return Icmpn(a,b,4096);}
 int Icmp(const char* text)const{return Icmp(value.c_str(),text);}
 void Replace(const char* a,const char* b){size_t p;while((p=value.find(a))!=std::string::npos)value.replace(p,std::strlen(a),b);}
 void BackSlashesToSlashes(){for(char& c:value)if(c=='\\')c='/';}int FileNameHash()const{return 0;}
'''
IMAGE = r'''
 bool scratchImage=false,allowDownSize=true,levelLoadReferenced=false,referencedOutsideLevelLoad=false;
 int usage=0,filter=0,repeat=0,cubeFiles=0;unsigned flags=0;unsigned texnum=1;
 int GetFilter()const{return filter;}int GetRepeat()const{return repeat;}
 int GetUploadWidth()const{return opts.width;}int GetUploadHeight()const{return opts.height;}
 void ActuallyLoadImage(bool){++metadataNative;loaded=true;}
 void AllocImage(){++metadataNative;loaded=true;}void DeriveOpts(){++metadataNative;}
 void AllocImage(const idImageOpts&,textureFilter_t,textureRepeat_t);
 void SetTexParameters(){++metadataNative;}void SetSamplerState(textureFilter_t,textureRepeat_t);
 void MakeDefault();void ResizeGL(int,int);void ResizeVK(int,int);void UploadScratch(const byte*,int,int);
 int internalFormat=10,dataFormat=0,dataType=0;bool CopyFramebuffer(int,int,int,int,int);bool CopyDepthbuffer(int,int,int,int,int);
 void GenerateImage(const byte*,int,int,textureFilter_t,textureRepeat_t,textureUsage_t){if(!R_ImagePolicyOperationAllowed())return;++metadataNative;}
 void GenerateCubeImage(const byte*[6],int,textureFilter_t,textureUsage_t){++metadataNative;}
 void SubImageUpload(int,int,int,int,int,int,const void*)const{++metadataNative;}
'''
MANAGER = r'''
 struct Hash{int First(int)const{return 0;}int Next(int)const{return -1;}} imageHash;
 idImage* defaultImage=nullptr;
 idImage* AllocImage(const char*){++metadataNative;return nullptr;}
 idImage* ImageFromFile(const char*,textureFilter_t,textureRepeat_t,textureUsage_t,cubeFiles_t,bool,unsigned);
 idImage* ImageHandleDeferred(const char*,textureFilter_t,textureRepeat_t,textureUsage_t,cubeFiles_t,bool,unsigned);
 idImage* ScratchImage(const char*,idImageOpts*,textureFilter_t,textureRepeat_t,textureUsage_t);
'''
HELPERS = r'''
static constexpr int DEFAULT_SIZE=16;struct{bool GetBool()const{return false;}}com_developer;
struct Common{void Error(const char*,...){throw std::runtime_error("unexpected engine Error");}void Printf(const char*,...){}} commonObject;
static Common* common=&commonObject;
struct idLib{static void FatalError(const char*,...){throw std::runtime_error("unexpected FatalError");}};
struct FileSystem{void RecordLevelLoadResource(int,const char*,const char*,int,int){}} files;
static FileSystem* fileSystem=&files;
const char* va(const char*,...){return "observed declaration";}
void R_NormalizeInternalImageName(idStr&){}
textureUsage_t R_ImageUsageForName(const char*,textureUsage_t usage){return usage;}
bool R_AllowImageDownSizeForName(const char*,bool allow){return allow;}
void R_BindTextureForDirectAccess(int,unsigned){++metadataNative;}
using GLenum=int;enum{RENDERER_CONTEXT_PROFILE_ES=1,GL_COLOR_ATTACHMENT0=2,GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT=3,GL_RGBA8=10,GL_RGB8,GL_RGBA,GL_RGB,GL_SRGB8_ALPHA8,GL_SRGB8,GL_RGB565,GL_RGB5_A1,GL_RGBA4,GL_UNSIGNED_BYTE,FMT_RGBA8,GL_DEPTH_COMPONENT,GL_DEPTH_COMPONENT16,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT32,GL_DEPTH_COMPONENT32F,GL_DEPTH24_STENCIL8,GL_DEPTH32F_STENCIL8,GL_FLOAT,FMT_DEPTH};
struct RenderTexture{int GetNumColorImages()const{return 0;}};struct{RenderTexture* renderTexture=nullptr;}backEnd;struct{struct{int profile=0;}backendCaps;}glConfig;
'''
TESTS = r'''
static void Failed(){Reset();allocationFailure=true;Run(false);allocationFailure=false;metadataNative=0;}
static void Retired(uint64_t epoch){TEST(resourceMutationEpoch.load()!=epoch);Run(false);TEST(starts==1&&recoveryInvalidated);}
static void Exercise(int which,bool unchanged=false){
 switch(which){
 case 0:imageManager.ImageHandleDeferred("image",0,0,0,0,unchanged,0);break;
 case 1:imageManager.insideLevelLoad=true;imageManager.ImageFromFile("image",0,0,0,0,unchanged,0);break;
 case 2:{idImageOpts opts=image.opts;imageManager.ScratchImage("image",&opts,0,0,unchanged?image.usage:3);break;}
 case 3:image.SetSamplerState(unchanged?image.filter:TF_LINEAR,image.repeat);break;
 case 4:image.ResizeGL(unchanged?image.opts.width:16,image.opts.height);break;
 case 5:image.ResizeVK(unchanged?image.opts.width:16,image.opts.height);break;
 case 6:{idImageOpts opts=image.opts;opts.isPersistant=true;opts.width=16;image.AllocImage(opts,TF_LINEAR,TR_CLAMP);break;}
 case 7:{static byte pixels[4096]{};image.UploadScratch(pixels,16,16);break;}
 case 8:{static byte pixels[4096]{};image.UploadScratch(pixels,16,96);break;}
 case 15:image.MakeDefault();break;
 case 9:case 10:{idImageOpts opts=image.opts;imageManager.ScratchImage("image",&opts,0,0,which==9?3:image.usage);break;}
 case 11:case 13:image.CopyFramebuffer(0,0,which==11?16:8,8,0);break;
 case 12:case 14:image.CopyDepthbuffer(0,0,which==12?16:8,8,0);break;
 }
}
int main(){try{
 std::memset(&sentinel,0x6b,sizeof(sentinel));
 for(int path=0;path<16;++path){
  Failed();image.loaded=true; // A surviving resident after another resource's refusal.
  if(path==7||path==8){image.usage=TD_LOOKUP_TABLE_RGBA;image.opts.textureType=path==8?TT_CUBIC:TT_2D;}
  if(path==9)image.scratchImage=true;
  if(path>=11){image.file=false;glConfig.backendCaps.profile=path==13?RENDERER_CONTEXT_PROFILE_ES:0;if(path==13)image.internalFormat=-1;}
  const auto epoch=resourceMutationEpoch.load();Exercise(path);Retired(epoch);
  if(path<3||path==9||path==10)TEST(metadataNative==0); // Exact no-load/no-allocation reproductions.
 }
 // Same-value lookup/sampler/resize calls do not retire a valid census.
 for(int path=0;path<6;++path){
  Failed();image.loaded=true;if(path==2)image.scratchImage=true;
  const auto epoch=resourceMutationEpoch.load();Exercise(path,true);
  TEST(resourceMutationEpoch.load()==epoch&&metadataNative==0);
  imageManager.insideLevelLoad=false;Run(true);TEST(!recovery);
 }
 // Ordinary dynamic-target pixel copies with unchanged metadata keep the
 // file/default recovery census; no claims about generated content are added.
 for(bool depth:{false,true}){Failed();image.loaded=true;image.file=false;glConfig.backendCaps.profile=0;
  if(depth)image.internalFormat=GL_DEPTH_COMPONENT24;
  const auto epoch=resourceMutationEpoch.load();
  TEST(depth?image.CopyDepthbuffer(0,0,8,8,0):image.CopyFramebuffer(0,0,8,8,0));
  TEST(resourceMutationEpoch.load()==epoch&&metadataNative==2);Run(true);
 }
 // Changed metadata is refused before any field/native continuation on a
 // foreign thread during an admitted attempt, even for a persistent target.
 for(int path=0;path<16;++path){
  Reset();if(path==7||path==8){image.usage=TD_LOOKUP_TABLE_RGBA;image.opts.textureType=path==8?TT_CUBIC:TT_2D;}
  if(path==9)image.scratchImage=true;
  if(path>=11){image.file=false;glConfig.backendCaps.profile=path==13?RENDERER_CONTEXT_PROFILE_ES:0;if(path==13)image.internalFormat=-1;}
  bool invoked=false;callback=[&](const char* at){if(invoked||std::strcmp(at,"restart"))return;invoked=true;
   const auto old=image;metadataNative=0;std::thread foreign([&]{Exercise(path);});foreign.join();
   TEST(image.allowDownSize==old.allowDownSize&&image.usage==old.usage&&image.scratchImage==old.scratchImage);
   TEST(image.filter==old.filter&&image.repeat==old.repeat&&image.opts==old.opts&&image.defaulted==old.defaulted&&image.internalFormat==old.internalFormat&&image.dataFormat==old.dataFormat&&image.dataType==old.dataType&&metadataNative==0);
  };Run(false);TEST(invoked&&teardowns==0);
 }
 std::printf("actual metadata paths / recovery ownership: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''


def unit(repository, changes=None):
    renderer = repository/'src/renderer'
    sources={p:(renderer/p).read_text() for p in ['RendererResourceSettings.cpp','ImageManager.cpp','Image_load.cpp','OpenGL/gl_Image.cpp','Vulkan/vk_Image.cpp','Image_intrinsic.cpp']}
    if changes:
        file,old,new=changes
        assert sources[file].count(old)==1,old
        sources[file]=sources[file].replace(old,new)
    support=base.SUPPORT.replace('struct idStr {','struct idStr {'+STR)
    anchor='enum {TT_2D=1,TT_CUBIC=2};using textureUsage_t=int;'
    assert support.count(anchor)==1, 'Review the shared recovery fixture enum declarations'
    support=support.replace(anchor,anchor+'\nusing textureFilter_t=int;using textureRepeat_t=int;using cubeFiles_t=int;using byte=unsigned char;\nenum{TF_LINEAR=1,TR_CLAMP=2,TR_REPEAT=0,TD_LOOKUP_TABLE_RGBA=4,CF_2D=0,LEVEL_LOAD_RESOURCE_IMAGE=0,GL_TEXTURE_CUBE_MAP_EXT=0,GL_TEXTURE_2D=1,TF_DEFAULT=0,TD_DEFAULT=0};static int metadataNative=0;')
    support=support.replace('struct idImageOpts {int width=8,height=8,numLevels=4,textureType=TT_2D;};','struct idImageOpts {int width=8,height=8,numLevels=4,textureType=TT_2D,format=0;bool isPersistant=false;bool operator==(const idImageOpts&)const=default;};')
    support=support.replace('struct idImage {','struct idImage {'+IMAGE)
    support=support.replace('bool IsFileBacked()const{return file;}','bool IsFileBacked()const{return file&&!scratchImage&&!opts.isPersistant;}')
    support=support.replace('struct Images {','struct idImageManager {'+MANAGER).replace('static Images* globalImages','static idImageManager* globalImages')
    support=support.replace('struct Decls {','struct Decls {void MediaPrint(const char*,...){}')
    methods=[]
    for file,signature in [('ImageManager.cpp','idImage\t*idImageManager::ImageFromFile('),('ImageManager.cpp','idImage *idImageManager::ImageHandleDeferred('),('ImageManager.cpp','idImage * idImageManager::ScratchImage('),('Image_load.cpp','void idImage::AllocImage( const idImageOpts'),('Image_load.cpp','void idImage::SetSamplerState('),('Image_load.cpp','void idImage::UploadScratch('),('Image_intrinsic.cpp','void idImage::MakeDefault(')]:
        methods.append(method(sources[file],signature))
    for file,name in [('OpenGL/gl_Image.cpp','ResizeGL'),('Vulkan/vk_Image.cpp','ResizeVK')]:
        methods.append(method(sources[file],'void idImage::Resize(').replace('idImage::Resize','idImage::'+name,1))
    for signature in ['bool idImage::CopyFramebuffer(', 'bool idImage::CopyDepthbuffer(']:
        body=method(sources['Image_load.cpp'],signature)
        end=body.index('opts.height = imageHeight;')+len('opts.height = imageHeight;')
        methods.append(body[:end]+'\n ++metadataNative;return true;\n}\n')
    coordinator='\n'.join(line for line in sources['RendererResourceSettings.cpp'].splitlines() if not line.startswith('#include "') and not line.startswith('#pragma hdrstop'))
    return support+HELPERS+'\n'.join(methods)+coordinator+base.MAIN[:base.MAIN.index('int main()')]+TESTS


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--repository',type=Path,default=ROOT);parser.add_argument('--headers',type=Path,default=ROOT);parser.add_argument('--sanitize',action='store_true');parser.add_argument('--mutations',action='store_true');parser.add_argument('--compiler');args=parser.parse_args()
    root=args.repository.resolve();headers=args.headers.resolve()
    names=['src/renderer/RendererResourceSettings.cpp','src/renderer/RendererResourceSettings.h','src/renderer/RendererConsumedPolicy.h','src/renderer/RenderModuleAPI.h','src/renderer/DisplayPresentation.h','src/renderer/ImageManager.cpp','src/renderer/Image_intrinsic.cpp','src/renderer/Image_load.cpp','src/renderer/OpenGL/gl_Image.cpp','src/renderer/Vulkan/vk_Image.cpp','tools/tests/renderer_image_policy_metadata.py','tools/tests/renderer_image_policy_restart.py','tools/tests/renderer_image_policy_boundaries.py']
    names+=['tools/tests/renderer_image_recovery_fixture.py','src/renderer/RendererImageRecovery.h','src/renderer/RendererImageRecovery.cpp','src/imagetools/ImageRecoveryEnvelope.h','src/imagetools/ImageContentIdentity.h','src/imagetools/ImageContentIdentity.cpp','src/idlib/CryptoHash.h','src/idlib/CryptoHash.cpp']
    extra_sources=[str(root/name) for name in ('src/renderer/RendererImageRecovery.cpp','src/imagetools/ImageContentIdentity.cpp','src/idlib/CryptoHash.cpp')]
    def hashes():return {str(root/p):hashlib.sha256((root/p).read_bytes()).hexdigest() for p in names}
    before=hashes();cases=[('baseline',None)]
    if args.mutations:
        cases += [(name,(file,old,new)) for name,file,old,new in [
            ('lazy-permission','ImageManager.cpp','if ( image->allowDownSize != mergedAllowDownSize && !R_ImagePolicyContentMutation() ) return image;','(void)0;'),
            ('eager-permission','ImageManager.cpp','if ( allowDownSizeChanged && !R_ImagePolicyContentMutation() ) return image;','(void)0;'),
            ('scratch-classification','ImageManager.cpp','if ( ( !image->scratchImage || image->usage != usage ) && !R_ImagePolicyContentMutation() ) return image;','(void)0;'),
            ('scratch-usage','ImageManager.cpp','!image->scratchImage || image->usage != usage','!image->scratchImage'),
            ('default-classification','Image_intrinsic.cpp','if ( !defaulted && !R_ImagePolicyContentMutation() ) return;','(void)0;'),
            ('scratch-cube-resize','Image_load.cpp','if ( !R_ImagePolicyContentMutation() ) return;\n\t\t\topts.width = cols;\n\t\t\topts.height = rows;\n\t\t\tAllocImage();\n\t\t}\n\t\tSetSamplerState( TF_LINEAR, TR_CLAMP );','opts.width = cols;\n\t\t\topts.height = rows;\n\t\t\tAllocImage();\n\t\t}\n\t\tSetSamplerState( TF_LINEAR, TR_CLAMP );'),
            ('scratch-2d-resize','Image_load.cpp','if ( !R_ImagePolicyContentMutation() ) return;\n\t\t\topts.width = cols;\n\t\t\topts.height = rows;\n\t\t\tAllocImage();\n\t\t}\n\t\tSetSamplerState( TF_LINEAR, TR_REPEAT );','opts.width = cols;\n\t\t\topts.height = rows;\n\t\t\tAllocImage();\n\t\t}\n\t\tSetSamplerState( TF_LINEAR, TR_REPEAT );'),
            ('frame-copy-size','Image_load.cpp','if ( ( opts.width != imageWidth || opts.height != imageHeight || IsFileBacked() ) && !R_ImagePolicyContentMutation() ) return false;','(void)0;'),
            ('frame-copy-format','Image_load.cpp','if ( !R_ImagePolicyContentMutation() ) return false;\n\t\t\topts.format = FMT_RGBA8;','opts.format = FMT_RGBA8;'),
            ('depth-copy-metadata','Image_load.cpp','if ( ( !hasDepthStorage || opts.width != imageWidth || opts.height != imageHeight || IsFileBacked() ) && !R_ImagePolicyContentMutation() ) return false;','(void)0;'),
            ('sampler','Image_load.cpp','if ( !R_ImagePolicyContentMutation() ) return;\n\tfilter = tf;','filter = tf;'),
            ('resize-gl','OpenGL/gl_Image.cpp','if ( !R_ImagePolicyContentMutation() ) return;\n\topts.width = width;','opts.width = width;'),
            ('resize-vk','Vulkan/vk_Image.cpp','if ( !R_ImagePolicyContentMutation() ) return;\n\topts.width = width;','opts.width = width;'),
            ('persistent-options','Image_load.cpp','if ( ( filter != tf || repeat != tr || !( opts == imgOpts ) || defaulted || !imgOpts.isPersistant ) && !R_ImagePolicyContentMutation() ) return;','if ( !imgOpts.isPersistant && !R_ImagePolicyContentMutation() ) return;'),
        ]]
    out=Path(tempfile.mkdtemp(prefix='image-metadata-',dir=root/'.tmp'));env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out));compiler=args.compiler or next((p for n in ['clang++','g++','c++'] if(p:=shutil.which(n))),None);assert compiler
    records=[];status='running'
    try:
        for tag,change in cases:
            source=out/(tag+'.cpp');source.write_text(unit(root,change));binary=out/(tag+'.exe')
            command=[compiler,'-std=c++20','-pthread','-I',str(root/'src/renderer'),'-I',str(headers/'src/renderer')]
            if args.sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
            command += [str(source)]+extra_sources+['-o',str(binary)]
            if Path(compiler).stem.lower() in ['cl','clang-cl']:
                command=[compiler,'/nologo','/std:c++20','/EHsc','/MTd','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/I'+str(root/'src/renderer'),'/I'+str(headers/'src/renderer'),str(source)]+extra_sources+['/Fe:'+str(binary),'/Fo:'+str(out)+os.sep]
            for stage,call in [('compile',command),('run',[str(binary)])]:
                result=subprocess.run(call,env=env,cwd=out,capture_output=True,text=True,timeout=90);log=out/(tag+'-'+stage+'.log');log.write_text(result.stdout+result.stderr)
                records.append({'case':tag,'stage':stage,'command':call,'exit_code':result.returncode,'log':str(log),'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
                if stage=='compile' and result.returncode:raise RuntimeError(str(log))
                if stage=='run':
                    if bool(result.returncode)!=(change is not None):raise RuntimeError(str(log))
                    records[-1]['expected_rejection']=change is not None
                    print(tag+': '+('rejected compiled mutation' if change else result.stdout.strip()))
        status='passed'
    finally:
        after=hashes();status='source_changed' if before!=after else status if status=='passed' else 'failed'
        (out/'result.json').write_text(json.dumps({'status':status,'files_before':before,'files_after':after,'records':records,'limitations':__doc__},indent=2)+'\n');print(out/'result.json')
    if status!='passed':raise SystemExit(1)


if __name__=='__main__':main()
