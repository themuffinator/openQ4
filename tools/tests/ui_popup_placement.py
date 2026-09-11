#!/usr/bin/env python3
"""Compile the actual bounded popup placement geometry; no window or native input."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default=shutil.which('clang++') or 'g++')
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--mutations',action='store_true')
    args=parser.parse_args()
    out=Path(tempfile.mkdtemp(prefix='popup-placement-',dir=ROOT/'.tmp')).resolve()
    env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
    files=[Path(__file__),ROOT/'src/ui/retained/PopupPlacement.h',ROOT/'src/ui/retained/PopupPlacement.cpp',ROOT/'tools/tests/native/UiPopupPlacementTest.cpp']
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    record={'passed':False,'sources':{str(p.relative_to(ROOT)):sha(p) for p in files},'runs':[],'mutations':[]}
    exe=out/'test.exe';units=[files[2],files[3]]
    cmd=[args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-I',ROOT/'src/ui/retained',*units,'-o',exe]
    if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    if Path(args.compiler).stem.lower() in ('cl','clang-cl'):
        cmd=[args.compiler,'/nologo','/std:c++20','/EHsc','/MTd','/W4','/WX','/I'+str(ROOT/'src/ui/retained'),*units,'/Fe:'+str(exe),'/Fo:'+str(out)+os.sep]
    try:
        for stage,command in [('compile',cmd),('run',[exe])]:
            result=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True)
            log=out/(stage+'.log');log.write_text(result.stdout+result.stderr)
            record['runs'].append({'stage':stage,'command':list(map(str,command)),'exit':result.returncode,'log':str(log),'sha256':sha(log)})
            print(stage,result.returncode,result.stdout+result.stderr,flush=True)
            if result.returncode:raise RuntimeError(log)
        if args.mutations:
            original=files[2].read_text()
            rules=[
                ('ancestor_inset','x*a.x+y*a.y-inset','x*a.x+y*a.y'),
                ('window_right','viewport.x+viewport.width-inset','viewport.x+viewport.width+1000-inset'),
                ('mirrored_winding','winding>0?1:-1','1'),
                ('direction','below<desiredHeight && above>below','false && above>below'),
                ('preserve_output','    if (!Valid(region) || !Valid(anchor)','    out={};\n    if (!Valid(region) || !Valid(anchor)'),
                ('minimum_width','if (!Origins(region,minWidth,minHeight,anchor,0,feasible)) return false;','minWidth=1; if (!Origins(region,minWidth,minHeight,anchor,0,feasible)) return false;'),
            ]
            for name,before,after in rules:
                if original.count(before)!=1:raise RuntimeError('Nonunique mutation '+name)
                changed=out/(name+'.cpp');changed.write_text(original.replace(before,after),newline='\n')
                command=[str(changed) if str(part)==str(files[2]) else part for part in cmd]
                compiled=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True)
                log=out/(name+'-compile.log');log.write_text(compiled.stdout+compiled.stderr)
                if compiled.returncode:raise RuntimeError('Uncompiled mutation '+str(log))
                observed=subprocess.run([exe],cwd=out,env=env,capture_output=True,text=True)
                log=out/(name+'-run.log');log.write_text(observed.stdout+observed.stderr)
                rejected=observed.returncode!=0 and 'FAIL ' in observed.stderr
                record['mutations'].append({'name':name,'rejected':rejected,'exit':observed.returncode,'source_sha256':sha(changed),'log':str(log),'sha256':sha(log)})
                if not rejected:raise RuntimeError('Mutation survived '+name)
                print('Rejected compiled mutation',name,flush=True)
        record['passed']=True
    finally:
        record['source_unchanged']=all(sha(ROOT/n)==h for n,h in record['sources'].items())
        record['artifacts']={str(p):sha(p) for p in out.iterdir() if p.is_file()}
        (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
        print(out/'result.json',flush=True)

if __name__=='__main__':main()
