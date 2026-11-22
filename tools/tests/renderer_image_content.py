#!/usr/bin/env python3
"""Actual byte identity, DDS/bimage loading and cold CPU reconstruction with counted VFS/allocator boundaries.
No native upload, driver, durable journal, or multi-source decoded reconstruction qualification.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess, tempfile
from renderer_consumed_policy import method
from renderer_image_reduction import SUPPORT, MAIN
ROOT=Path(__file__).resolve().parents[2]

def source():
 s=SUPPORT.replace('#include "RendererConsumedPolicy.h"','#include "RendererConsumedPolicy.h"\n#include <functional>')
 # Use actual engine enum values and format sizing, rather than the prior
 # reduction fixture's simplified format constants.
 a=s.index('enum textureFormat_t');b=s.index('const int MAX_BINARY_IMAGE_DIMENSION',a)
 opts=(ROOT/'src/renderer/ImageOpts.h').read_text()
 enums='\n'.join(re.search(r'enum '+name+r'\s*\{[\s\S]*?\};',opts)[0] for name in ['textureType_t','textureFormat_t','textureColor_t'])
 image=(ROOT/'src/renderer/Image.h').read_text()
 usage=re.search(r'typedef enum \{\s*TD_SPECULAR[\s\S]*?} textureUsage_t;',image)[0]
 s=s[:a]+enums+'\n'+re.search(r'#define FMT_MAX_VALID[^\n]*',opts)[0]+'\n'+usage+'\n'+s[b:]
 start=s.index('struct bimageImage_t');end=s.index('template<class T>struct idList',start)
 s=s[:start]+'#include "BinaryImageData.h"\n'+s[end:]
 s=s.replace('void Clear(){this->clear();}','void Clear(){this->clear();}void Swap(idList& o){this->swap(o);}')
 a=s.index('struct idBinaryImage{');b=s.index('struct idStr{',a)
 s=s[:a]+r"""
struct idBinaryImage{bimageFile_t fileData{};idList<idBinaryImageData>images;byte*loadedFileData=nullptr;int fileDepth=1;int loadedFileBytes=0;imageFileContent_t fileContent{};
 std::string name;idBinaryImage(const char* n="test"):name(n){}const char*GetName()const{return name.c_str();}
 ~idBinaryImage(){Clear();}void Clear();
 void Load2DFromOwnedCompressedData(int,int,int,textureFormat_t,textureColor_t,byte*,const int*,const int*);
 bool GetContentIdentity(imageBinaryContent_t&)const;const imageFileContent_t&GetFileContent()const{return fileContent;}
 void ObserveFileContent(const imageFileContent_t& f){fileContent=f;}void SwapContent(idBinaryImage&)noexcept;
 bool LoadExactContentFile(const imageFileContent_t&);
 ID_TIME_T LoadFromGeneratedFileUnchecked();ID_TIME_T LoadFromCompactGeneratedFileUnchecked();
 void MakeGeneratedFileName(struct idStr&);
 bool LoadFromFile(struct idFile*,int=-1);
 bool LoadFromGeneratedFile(struct idFile*,ID_TIME_T,bool,int=-1,const imageFileContent_t* =nullptr,bool=false);
};
template<class T>void idSwap(T&a,T&b){std::swap(a,b);}
static int swapCalls=0;
template<class T>struct idSwapClass{template<class U>void Big(U&v){++swapCalls;auto*p=reinterpret_cast<byte*>(&v);std::reverse(p,p+sizeof(v));}};
"""+s[b:]
 s=s.replace('const char*c_str()const{return s.c_str();}', 'const char*c_str()const{return s.c_str();}operator const char*()const{return s.c_str();}')
 s+='\nvoid idBinaryImage::MakeGeneratedFileName(idStr& out){out=idStr("generated/ordinary.bimage");}\nvoid R_MakeCompactBinaryImageFileName(idStr& out,const char*){out=idStr("generated/compact.bimage");}\n'
 a=s.index('struct idFile{');b=s.index('struct Caps{',a)
 s=s[:a]+r"""
static std::function<void()> onRead,onClose;static bool failFileList=false;
struct idFile{std::vector<byte>bytes;bool shortRead=false;int cursor=0;int Length()const{return int(bytes.size());}int Tell()const{return cursor;}int64_t Timestamp()const{return 73;}int64_t ContentStamp()const{return Timestamp();}int Read(void*p,int n){if(shortRead)--n;if(n<0||cursor+n>Length())return 0;memcpy(p,bytes.data()+cursor,n);cursor+=n;if(onRead)onRead();return n;}};
struct FileSystem{idFile file;bool missing=false;int open=0,opens=0;std::string lastPath;idFile*OpenFileRead(const char*path){++opens;lastPath=path;if(missing)return nullptr;++open;file.cursor=0;return &file;}void CloseFile(idFile*p){TEST(p==&file&&open==1);--open;if(onClose)onClose();}bool InProductionMode()const{return true;}}fs;
FileSystem*fileSystem=&fs;
struct idFileLocal{idFile*file;idFileLocal(idFile*f):file(f){}~idFileLocal(){if(file)fileSystem->CloseFile(file);}operator idFile*()const{return file;}idFile*operator->()const{return file;}};
"""+s[b:]
 fmt=(ROOT/'src/imagetools/ImageToolsState.cpp').read_text();s+='\n'+method(fmt,'int BitsForFormat(')+'\n'+method(fmt,'int BytesPerBlockForFormat(')
 files=(ROOT/'src/imagetools/Image_files.cpp').read_text();binary=(ROOT/'src/imagetools/BinaryImage.cpp').read_text();process=(ROOT/'src/imagetools/Image_process.cpp').read_text()
 s+='\n'+'\n'.join(method(process,x) for x in ['void R_ApplyImageDownsizePolicy(', 'int R_ImageDownsizePolicyMipSkip(', 'bool R_ResolveImageReduction(', 'bool R_ImageReductionIsExact('])
 s+='\n'+'\n'.join(method(binary,x) for x in ['static bool R_BinaryImageFormatIsBlockCompressed(', 'static int R_BinaryImageMinimumDataSize(', 'void idBinaryImage::Clear(', 'void idBinaryImage::Load2DFromOwnedCompressedData(', 'bool idBinaryImage::GetContentIdentity(', 'void idBinaryImage::SwapContent(', 'bool idBinaryImage::LoadFromGeneratedFile( idFile *', 'bool idBinaryImage::LoadFromFile(', 'bool idBinaryImage::LoadExactContentFile(', 'ID_TIME_T idBinaryImage::LoadFromGeneratedFileUnchecked(', 'ID_TIME_T idBinaryImage::LoadFromCompactGeneratedFileUnchecked('])
 s+='\n'+files[files.index('static ID_INLINE uint32 R_ReadLittleUInt32('):files.index('static bool R_ReadDDSFileInfoUncached(')]
 s+='\n'+re.search(r'typedef bool \(\*imageDDSPrefetchTake_t\)\([^;]*;',image)[0]
 s+='\n'+files[files.index('static imageDDSPrefetchTake_t ddsPrefetchTake'):files.index('/*\n=============\nR_LoadPrecompressedDDS')]
 s+='\n'+method(files,'bool R_LoadPrecompressedDDS(')
 # Actual CPU reconstruction TU; remove only engine includes for the counted boundary.
 cold=(ROOT/'src/imagetools/ImageContentRecovery.cpp').read_text()
 s+='\n'+'\n'.join(line for line in cold.splitlines() if not line.startswith('#include'))
 s+='\n'+method(MAIN,'static std::vector<byte> DDS(')
 s+='\n'+(ROOT/'tools/tests/native/RendererImageContentTest.cpp').read_text()
 return s

def prefetch_contract():
 """Level-load image prefetch: workers only read their own open file into their own buffer."""
 manager=(ROOT/'src/renderer/ImageManager.cpp').read_text()
 read=method(manager,'static void Read( levelImagePrefetchEntry_t &entry )')
 assert 'entry.file->Read( entry.buffer, entry.bytes )' in read and 'R_MakeImageFileContent( IFC_DIRECT_DDS, entry.qpath.c_str(),' in read
 for forbidden in ('idStr ','fileSystem->','Mem_Alloc','Mem_Free','common->','.done'):
  assert forbidden not in read,'prefetch worker read must not '+forbidden.strip()
 work=method(manager,'void Work()')
 assert work.index('queue.pop_front();')<work.index('Read( *entry );')<work.index('entry->done = true;'),'worker publishes done after its read'
 assert 'std::lock_guard<std::mutex> lock( mutex );\n\t\t\t\tentry->done = true;' in work,'done is published under the lock'
 pump=method(manager,'void Pump( int consumeIndex )')
 assert 'fileSystem->OpenFileRead( entry.qpath.c_str() )' in pump and 'Mem_Alloc( bytes )' in pump,'the main thread opens and allocates'
 assert 'nextToOpen - consumeIndex < LEVEL_IMAGE_PREFETCH_MAX_OPEN' in pump and 'outstandingBytes < LEVEL_IMAGE_PREFETCH_MAX_BYTES' in pump,'bounded window'
 settle=method(manager,'void Settle( levelImagePrefetchEntry_t &entry, bool wantData )')
 assert settle.index('workDone.wait_for( lock, std::chrono::milliseconds( 2 ), [&entry] { return entry.done; } )')<settle.index('fileSystem->CloseFile( entry.file );'),'files close on the main thread after their read'
 # the loading screen presents while a worker reads, outside the workers' lock
 assert settle.index('lock.unlock();')<settle.index('common->LoadingCheckpoint();')<settle.index('lock.lock();'),'the wait presents outside the lock'
 end=method(manager,'void End()')
 assert end.index('R_SetDDSPrefetchSource( NULL );')<end.index('worker.join();'),'the hook is withdrawn before workers stop'
 load=method(manager,'int idImageManager::LoadLevelImages(')
 assert load.index('prefetch.Pump( i );')<load.index('ActuallyLoadImage( false );')<load.index('prefetch.Retire( i );')<load.index('prefetch.End();')
 image=(ROOT/'src/renderer/Image_load.cpp').read_text()
 predict=method(image,'bool idImage::PredictDirectDDSPayload(')
 assert 'usage == TD_PBR_COLOR' in predict and 'R_ImagePolicyUsesPreparedContent()' in predict and '!precompressed' in predict
 heap=(ROOT/'src/idlib/Heap.cpp').read_text()
 assert 'std::atomic<int>\tc_heapAllocRunningCount;' in heap,'inflate allocates from worker threads'

def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler');p.add_argument('--sanitize',action='store_true');p.add_argument('--mutations',action='store_true');a=p.parse_args()
 prefetch_contract()
 names=['src/imagetools/'+x for x in ['ImageContentIdentity.h','ImageContentIdentity.cpp','ImageContentRecovery.cpp','BinaryImage.h','BinaryImage.cpp','BinaryImageData.h','Image_files.cpp','Image_process.cpp']]
 names+=['src/renderer/'+x for x in ['RendererConsumedPolicy.h','RendererConsumedPolicy.cpp','Image.h','Image_load.cpp','ImageManager.cpp']]
 names+=['src/renderer/ImageOpts.h','src/imagetools/ImageToolsState.cpp','src/idlib/CryptoHash.cpp','src/idlib/CryptoHash.h','tools/tests/renderer_image_content.py','tools/tests/native/RendererImageContentTest.cpp','tools/tests/renderer_image_reduction.py','tools/tests/renderer_consumed_policy.py']
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();before={n:sha(ROOT/n) for n in names}
 code=source();out=Path(tempfile.mkdtemp(prefix='image-content-',dir=ROOT/'.tmp'));env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
 if a.sanitize:env['ASAN_OPTIONS']='symbolize=0'
 for n in ['RendererConsumedPolicy.h']:
  text=(ROOT/'src/renderer'/n).read_text().replace('../imagetools/ImageContentIdentity.h','ImageContentIdentity.h');(out/n).write_text(text)
 for n in ['ImageContentIdentity.h','BinaryImageData.h']:shutil.copyfile(ROOT/'src/imagetools'/n,out/n)
 compiler=a.compiler or shutil.which('clang++') or shutil.which('g++');assert compiler
 cases=[('actual',code,False)]
 if a.mutations:
  mutations=[
   ('packed-reject-compatible-version','fileData.headerMagic == legacyPackedMagic','false'),
   ('cache-admit-obsolete-version','allowLegacyPacked && dataBytes >= 0 &&','true &&'),
   ('packed-ignore-version','fileData.headerMagic == legacyPackedMagic','true'),
   ('packed-admit-cube','fileData.textureType == TT_2D && fileData.numLevels == 1','fileData.numLevels == 1'),
   ('packed-admit-old-mips','fileData.textureType == TT_2D && fileData.numLevels == 1','fileData.textureType == TT_2D'),
   ('dds-prefetch-feeds-recovery','const bool prefetched = expected == NULL && ddsPrefetchTake != NULL','const bool prefetched = ddsPrefetchTake != NULL'),
   ('dds-prefetch-ignored','&& ddsPrefetchTake( name.c_str(), &buffer, &fileSize, timestamp, &actualContent, &contentObserved );','&& false;'),
   ('dds-skip-byte-proof','if (expected && (!contentObserved || !R_ImageFileContentEqual(requested,actualContent))) return false;','if (false) return false;'),
   ('cache-skip-byte-proof','!R_ImageFileContentEqual(*expected,actual)','false'),
   ('cold-skip-output-proof','!R_ImageBinaryContentEqual(expected.binary,actual)','false'),
   ('cold-cache-source-authority','expected.resolved.maxDimension || expected.resolved.mipShift || expected.resolved.minDimension != 1 ||','false ||'),
   ('cold-clear-output-on-entry','const imagePortableContent_t expected=requested;','output.Clear(); const imagePortableContent_t expected=requested;'),
   # The BC6H half-float branch (d229362cf) publishes first in source order but is
   # unreachable here (BPTC caps stay on); mutate the compressed-load publication.
   ('dds-forget-file-identity','buffer = NULL;\t// the binary image now owns the file buffer\n        if (contentObserved) image.ObserveFileContent(actualContent);','buffer = NULL;\n        (void)contentObserved;'),
   ('ordinary-cache-unobserved','if (R_MakeImageFileContent(IFC_OBSERVED_BIMAGE,binaryFileName.c_str(),loadedFileData,loadedFileBytes,content)) fileContent=content;','(void)content;'),
   ('compact-cache-unobserved','if (R_MakeImageFileContent(IFC_OBSERVED_BIMAGE,compactFileName.c_str(),loadedFileData,loadedFileBytes,content)) fileContent=content;','(void)content;'),
  ]
  for name,old,new in mutations:
   assert old in code,name;cases.append((name,code.replace(old,new,1),True))
 records=[];status='failed'
 try:
  for tag,text,rejected in cases:
   cpp=out/(tag+'.cpp');cpp.write_text(text);exe=out/(tag+'.exe')
   cmd=[compiler,'-std=c++20','-I',str(out),str(cpp),str(ROOT/'src/imagetools/ImageContentIdentity.cpp'),str(ROOT/'src/idlib/CryptoHash.cpp'),'-o',str(exe)]
   if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
   if Path(compiler).stem.lower() in ['cl','clang-cl']:cmd=[compiler,'/nologo','/std:c++20','/EHsc','/MTd','/D_DEBUG','/I'+str(out),str(cpp),str(ROOT/'src/imagetools/ImageContentIdentity.cpp'),str(ROOT/'src/idlib/CryptoHash.cpp'),'/Fe:'+str(exe),'/Fo:'+str(out)+os.sep]
   for stage,command in [('compile',cmd),('run',[str(exe)])]:
    result=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,timeout=120);log=out/f'{tag}-{stage}.log';log.write_text(result.stdout+result.stderr)
    records.append({'case':tag,'stage':stage,'command':command,'exit_code':result.returncode,'expected_rejection':rejected and stage=='run','log':str(log),'sha256':sha(log)})
    if (stage=='compile' and result.returncode) or (stage=='run' and bool(result.returncode)!=rejected):raise RuntimeError(str(log))
   print(tag,'rejected compiled mutation' if rejected else result.stdout.strip())
  status='passed'
 finally:
  after={n:sha(ROOT/n) for n in names}
  if before!=after:status='source_changed'
  (out/'result.json').write_text(json.dumps({'status':status,'files_before':before,'files_after':after,'records':records,'generated':{str(x):sha(x) for x in out.iterdir() if x.suffix in ['.cpp','.h','.exe']},'limitations':__doc__},indent=2)+'\n');print(out/'result.json')
 if status!='passed':raise SystemExit(1)
if __name__=='__main__':main()
