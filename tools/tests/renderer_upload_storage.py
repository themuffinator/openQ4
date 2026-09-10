#!/usr/bin/env python3
"""Actual upload-manager allocation/cleanup at a counted GL boundary; no driver."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler');parser.add_argument('--sanitizers',action='store_true')
    parser.add_argument('--mutations',action='store_true')
    args=parser.parse_args()
    compiler=args.compiler or os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++') or shutil.which('cl')
    if not compiler: raise SystemExit('A C++ compiler is required')
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    files=['src/renderer/RendererUpload.h','src/renderer/RendererUpload.cpp','tools/tests/native/RendererUploadStorageTest.cpp','tools/tests/renderer_upload_storage.py']
    hashes={p:sha(ROOT/p) for p in files}
    text=(ROOT/files[1]).read_text(encoding='utf-8')
    markers=['static uint64_t R_RendererUpload_NextStorageGeneration()',
        'static bool R_RendererUpload_StorageErrorsClear()',
        'static bool R_RendererUpload_BoundStorageMatches(',
        'static void R_RendererUpload_DeleteBufferName(',
        'idUploadManager::idUploadManager()', 'void idUploadManager::Init(',
        'void idUploadManager::Shutdown(', 'void idUploadManager::BeginFrame(',
        'int idUploadManager::FrameCapacity(', 'bool idUploadManager::CreateFrameBuffers(',
        'void idUploadManager::ShutdownFrameBuffers(', 'bool idUploadManager::QueryStorage(',
        'const char *idUploadManager::PathName(']
    bodies=[]
    for marker in markers:
        start=text.index(marker);end=text.index('\n}',start)+2;bodies.append(text[start:end])
    work=Path(tempfile.mkdtemp(prefix='renderer-upload-storage-',dir=ROOT/'.tmp'))
    msvc=Path(compiler).name.lower() in ('cl','cl.exe')
    mutations=[('baseline','','')]
    if args.mutations:
        mutations += [
            ('size-proof','actualBytes == expectedBytes','actualBytes >= 0'),
            ('name-error','!R_RendererUpload_StorageErrorsClear() || frameBuffers[i].vbo == 0','frameBuffers[i].vbo == 0'),
            ('map-error','!R_RendererUpload_StorageErrorsClear() || frameBuffers[i].mapped == NULL','frameBuffers[i].mapped == NULL'),
            ('allocation-failure','if (!created) {','if (!created && false) {'),
            ('fallback-cleanup','ShutdownFrameBuffers();\n\t\t\trequestedPath = useMapRange','/* missing cleanup */\n\t\t\trequestedPath = useMapRange'),
            ('orphan-proof','storageReady = R_RendererUpload_BoundStorageMatches(stats.ringSizeBytes);','storageReady = R_RendererUpload_BoundStorageMatches(stats.ringSizeBytes) || clean;'),
            ('query-thread','!R_ImagePolicyRendererThread() || !initialized','!initialized'),
            ('query-generation','!storageReady || !storage.generation ||','!storageReady ||'),
            ('fallback-fence','hasSync = path == UPLOAD_PATH_PERSISTENT && syncAvailable;','hasSync = requestedPath != UPLOAD_PATH_DISABLED && syncAvailable;'),
        ]
    results=[]
    for name,old,new in mutations:
        case=work/name;case.mkdir();production='\n\n'.join(bodies)
        if old:
            assert production.count(old)==1,(name,production.count(old))
            production=production.replace(old,new)
        unit=(ROOT/files[2]).read_text(encoding='utf-8').replace('// @PRODUCTION@',production)
        source=case/'unit.cpp';source.write_text(unit,encoding='utf-8',newline='\n')
        exe=case/('test.exe' if os.name=='nt' else 'test')
        command=[compiler,'/nologo','/std:c++20','/EHsc','/MTd','/W4','/WX','/wd4100','/I'+str(ROOT),str(source),'/Fe'+str(exe)] if msvc else [compiler,'-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I'+str(ROOT),str(source),'-o',str(exe)]
        if args.sanitizers and not msvc: command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
        build=subprocess.run(command,cwd=case,capture_output=True,text=True,timeout=120)
        (case/'compile.log').write_text(build.stdout+build.stderr,encoding='utf-8')
        run=subprocess.run([str(exe)],cwd=case,capture_output=True,text=True,timeout=60) if build.returncode==0 else None
        (case/'run.log').write_text(run.stdout+run.stderr if run else '',encoding='utf-8')
        passed=build.returncode==0 and ((run.returncode==0) == (name=='baseline')) and (name=='baseline' or 'FAIL:' in run.stdout)
        results.append(dict(name=name,passed=passed,command=command,compile_exit=build.returncode,run_exit=run.returncode if run else None,
            unit_sha256=sha(source),binary_sha256=sha(exe) if exe.exists() else None,
            compile_log_sha256=sha(case/'compile.log'),run_log_sha256=sha(case/'run.log'),output=run.stdout if run else ''))
        print(name+': '+('passed' if passed else 'FAILED'))
        if not passed:
            print((case/('compile.log' if build.returncode else 'run.log')).read_text());break
    result=dict(passed=len(results)==len(mutations) and all(r['passed'] for r in results),sources=hashes,cases=results,sanitizers=args.sanitizers)
    assert hashes=={p:sha(ROOT/p) for p in files},'Sources changed during qualification'
    (work/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(work/'result.json')
    if not result['passed']:raise SystemExit(1)
if __name__=='__main__':main()
