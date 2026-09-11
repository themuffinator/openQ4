#!/usr/bin/env python3
"""Real durable image codec/envelope, bounded synthetic cohorts and allocation faults.

The CPU codec proves structure, not source availability, VFS authority, current
renderer ownership or GPU completion. Those have separate actual-method tests.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
FILES=['tools/tests/native/RendererImageRecoveryTest.cpp']+['src/'+p for p in (
 'renderer/RendererImageRecovery.h','renderer/RendererImageRecovery.cpp','renderer/RendererConsumedPolicy.h',
 'renderer/RendererResourceSettings.h','renderer/RenderModuleAPI.h','renderer/DisplayPresentation.h','renderer/ImageOpts.h',
 'imagetools/ImageRecoveryEnvelope.h','imagetools/ImageContentIdentity.h','imagetools/ImageContentIdentity.cpp','idlib/CryptoHash.h','idlib/CryptoHash.cpp')]
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',default=shutil.which('clang++') or 'g++');p.add_argument('--sanitize',action='store_true');p.add_argument('--mutations',action='store_true');a=p.parse_args()
 out=Path(tempfile.mkdtemp(prefix='image-recovery-codec-',dir=ROOT/'.tmp')).resolve();env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
 sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();paths=[Path(__file__)]+[ROOT/n for n in FILES]
 record={'passed':False,'scope':__doc__,'sources':{str(f.relative_to(ROOT)):sha(f) for f in paths},'runs':[]}
 try:
  cases=[('positive',None,None,None)]
  if a.mutations:
   cases += [(label,'src/'+file,before,after) for label,file,before,after in [
    ('direction','imagetools/ImageRecoveryEnvelope.h','U32(s,8)==direction','true'),
    ('crc','imagetools/ImageRecoveryEnvelope.h','CRC(s.substr(0,s.size()-4))==U32(s,s.size()-4)','true'),
    ('chunk-association','imagetools/ImageRecoveryEnvelope.h',"value->compare(0,4,key,6,4)!=0||(*value)[4]!=':'",'false'),
    ('base64-padbits','imagetools/ImageRecoveryEnvelope.h','(pad2&&(b&15))||(!pad2&&pad1&&(c&3))','false'),
    ('bounds','imagetools/ImageRecoveryEnvelope.h','s.size()<=RawMaxBytes','s.size()<=RawMaxBytes+1'),
    ('image-order','renderer/RendererImageRecovery.cpp','(previous&&!(Key(*previous)<Key(i)))','false'),
    ('source-scope','renderer/RendererImageRecovery.cpp','c.file.kind==IFC_DIRECT_DDS','true'),
    ('exact-size','renderer/RendererImageRecovery.cpp','r.selectedWidth==b.width','true'),
    ('unloaded-authority','renderer/RendererImageRecovery.cpp','else if(!EmptyContent(i.content))','else if(false)'),
    ('early-decode','renderer/RendererImageRecovery.cpp','if(!Frame(raw,direction,attempt))','output.value.reset(); if(!Frame(raw,direction,attempt))'),
   ]]
  for label,file,before,after in cases:
   case=out/label;case.mkdir()
   for name in FILES:
    target=case/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes((ROOT/name).read_bytes())
   if file:
    target=case/file;s=target.read_text();assert s.count(before)==1,(label,s.count(before));target.write_text(s.replace(before,after))
   exe=case/'codec.exe';units=[case/'tools/tests/native/RendererImageRecoveryTest.cpp']+[case/'src'/n for n in ('renderer/RendererImageRecovery.cpp','imagetools/ImageContentIdentity.cpp','idlib/CryptoHash.cpp')]
   cmd=[a.compiler,'-std=c++20','-O1','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-but-set-variable','-I',case,*units,'-o',exe]
   if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0','-no-pie']
   if Path(a.compiler).stem in ('cl','clang-cl'):cmd=[a.compiler,'/nologo','/std:c++20','/EHsc','/MTd','/D_DEBUG','/I'+str(case),*units,'/Fe:'+str(exe),'/Fo:'+str(case)+os.sep]
   for stage,command in [('compile',cmd),('run',[exe])]:
    r=subprocess.run([str(x) for x in command],cwd=case,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=180);log=case/(stage+'.log');log.write_text(r.stdout)
    record['runs'].append({'case':label,'stage':stage,'exit':r.returncode,'command':[str(x) for x in command],'log':str(log)});print(label,stage,r.returncode,r.stdout[-800:],flush=True)
    if (stage=='compile' and r.returncode) or (stage=='run' and ((label=='positive')!=(r.returncode==0))):raise RuntimeError(str(log))
  record['passed']=True
 finally:
  record['source_unchanged']=all(sha(ROOT/n)==h for n,h in record['sources'].items())
  record['artifacts']={str(f):sha(f) for f in out.rglob('*') if f.is_file()}
  (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(out/'result.json',flush=True)
if __name__=='__main__':main()
