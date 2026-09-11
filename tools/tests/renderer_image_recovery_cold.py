#!/usr/bin/env python3
"""Actual DDS/bimage cold reconstruction after complete codec/envelope roundtrip.

Reuses the original source-bound content harness without changing its tests:
every observed descriptor now crosses the new Encode/Pack/Unpack/Decode path
before its successful and adversarial cold reads. No driver or real VFS assets.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile
import renderer_image_content as content
from renderer_image_recovery_codec import FILES
ROOT=Path(__file__).resolve().parents[2]
ROUNDTRIP=r'''
static imagePortableContent_t Durable(const imagePortableContent_t& input){
 namespace ir=openq4::imageRecovery;
 for(unsigned direction:{1u,2u}){
  ir::Data data;data.attempt="0123456789abcdef0123456789abcdef";data.direction=direction;
  ir::Image image;image.name=input.file.qpath;image.usage=input.usage;image.resident=true;image.content=input;data.images.push_back(image);
  std::string raw,back,error;TEST(ir::Encode(data,raw,error));ir::Values fields;TEST(ir::Pack(raw,direction,data.attempt,fields));
  TEST(ir::Unpack(fields,direction,data.attempt,back)&&back==raw);ir::Record record;TEST(ir::Decode(back,direction,data.attempt,record,error));
  TEST(record.Get()->images.size()==1);const auto& actual=record.Get()->images[0].content;
  TEST(R_ImageFileContentEqual(input.file,actual.file)&&R_ImageBinaryContentEqual(input.binary,actual.binary));
  if(direction==2)return actual;
 }
 throw std::runtime_error("missing direction");
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',default=shutil.which('clang++') or 'g++');p.add_argument('--sanitize',action='store_true');p.add_argument('--mutations',action='store_true');a=p.parse_args()
 out=Path(tempfile.mkdtemp(prefix='image-recovery-cold-',dir=ROOT/'.tmp')).resolve();env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
 extra=['tools/tests/renderer_image_content.py','tools/tests/renderer_image_reduction.py','tools/tests/renderer_consumed_policy.py','tools/tests/renderer_image_recovery_codec.py','tools/tests/native/RendererImageContentTest.cpp']+['src/imagetools/'+n for n in ('ImageContentRecovery.cpp','BinaryImage.h','BinaryImage.cpp','BinaryImageData.h','Image_files.cpp','Image_process.cpp','ImageToolsState.cpp')]+['src/renderer/Image.h']
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();paths=[Path(__file__)]+[ROOT/n for n in set(FILES+extra)]
 record={'passed':False,'scope':__doc__,'sources':{str(p.relative_to(ROOT)):sha(p) for p in paths},'runs':[]}
 try:
  code=content.source();anchor='static imagePortableContent_t Descriptor('
  code=code.replace(anchor,ROUNDTRIP+'\n'+anchor)
  original='    return value;\n}\nstatic void PristineFailure';assert code.count(original)==1
  code=code.replace(original,'    return Durable(value);\n}\nstatic void PristineFailure')
  code='#include "RendererImageRecovery.h"\n'+code;cases=[('positive',code)]
  if a.mutations:
   for label,before,after in [
    ('dds-byte-proof','if (expected && (!contentObserved || !R_ImageFileContentEqual(requested,actualContent))) return false;','if (false) return false;'),
    ('cache-byte-proof','!R_ImageFileContentEqual(*expected,actual)','false'),
    ('output-digest','!R_ImageBinaryContentEqual(expected.binary,actual)','false'),
   ]:
    assert code.count(before)==1,(label,code.count(before));cases.append((label,code.replace(before,after)))
  for label,source in cases:
   cpp=out/(label+'.cpp');cpp.write_text(source);exe=out/(label+'.exe')
   units=[cpp]+[ROOT/'src'/n for n in ('renderer/RendererImageRecovery.cpp','imagetools/ImageContentIdentity.cpp','idlib/CryptoHash.cpp')]
   include=[ROOT/'src/renderer',ROOT/'src/imagetools'];cmd=[a.compiler,'-std=c++20',*sum((['-I',str(p)] for p in include),[]),*units,'-o',exe]
   if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0','-no-pie']
   if Path(a.compiler).stem in ('cl','clang-cl'):cmd=[a.compiler,'/nologo','/std:c++20','/EHsc','/MTd','/D_DEBUG',*['/I'+str(p) for p in include],*units,'/Fe:'+str(exe),'/Fo:'+str(out)+os.sep]
   for stage,command in [('compile',cmd),('run',[exe])]:
    r=subprocess.run([str(x) for x in command],cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=180);log=out/(label+'-'+stage+'.log');log.write_text(r.stdout)
    record['runs'].append({'case':label,'stage':stage,'exit':r.returncode,'command':[str(x) for x in command],'log':str(log)});print(label,stage,r.returncode,r.stdout[-1000:],flush=True)
    if (stage=='compile' and r.returncode) or (stage=='run' and ((label=='positive')!=(r.returncode==0))):raise RuntimeError(str(log))
  record['passed']=True
 finally:
  record['source_unchanged']=all(sha(ROOT/n)==h for n,h in record['sources'].items());record['artifacts']={str(p):sha(p) for p in out.iterdir() if p.is_file()}
  (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(out/'result.json',flush=True)
if __name__=='__main__':main()
