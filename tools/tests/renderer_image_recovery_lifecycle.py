#!/usr/bin/env python3
"""Actual full renderer Shutdown and recovery owner with counted resource cleanup.

Both CPU directions, original census, admission, interruption and reentry are
real. GPU/font/image-manager cleanup endpoints are counted; no device or input.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile
import renderer_image_policy_restart as restart
from filesystem_case_segments import function_body
ROOT=Path(__file__).resolve().parents[2]
SUPPORT=r'''
static int shutdownCalls=0,cpuDisposals=0,initializations=0,deviceInitializations=0;
static bool traceDisposal=false,disposalOutsideScope=false;
static std::function<void(const char*)> shutdownHook;
static void ShutdownBoundary(const char* name){++shutdownCalls;if(shutdownHook)shutdownHook(name);}
static bool r_initialRendererDevicePending=false;
static bool R_RendererRestartError(char* error,int size,const char* reason){idStr::Copynz(error,reason,size);return false;}
struct {bool isInitialized=true;}glConfig;
struct Common{void Printf(const char*){ShutdownBoundary("log");}} commonInstance;
static Common* common=&commonInstance;
static void R_RendererMetrics_ResetGpuFrameTiming(const char*){ShutdownBoundary("metrics");}
static void R_DoneFreeType(){ShutdownBoundary("font");}
static void R_ShutdownRenderTargetsBeforeImagePurge(){ShutdownBoundary("targets");}
struct Models{void Shutdown(){ShutdownBoundary("models");}} models;
static Models* renderModelManager=&models;
struct idCinematic{static void ShutdownCinematic(){ShutdownBoundary("cinematic");}};
static void R_ShutdownFrameData(){ShutdownBoundary("frame");}
struct Cache{void Shutdown(){ShutdownBoundary("cache");}} vertexCache;
static void R_ShutdownTriSurfData(){ShutdownBoundary("tris");}
static void RB_ShutdownDebugTools(){ShutdownBoundary("debug");}
static void ProcessPendingRenderTextureDeletes(){ShutdownBoundary("pending");}
struct GuiModel{};
class idRenderSystemLocal{
public:
 FILE* logFile=nullptr;GuiModel* guiModel=nullptr;GuiModel* demoGuiModel=nullptr;
 void Shutdown();void Init();
 void ShutdownSpecialEffects(){ShutdownBoundary("effects");}
 void Clear(){ShutdownBoundary("clear");guiModel=demoGuiModel=nullptr;}
 void ShutdownOpenGL(){ShutdownBoundary("device");glConfig.isInitialized=false;}
} engine;
static void OnCpuDestroy(){if(traceDisposal){++cpuDisposals;if(!fullOwnerShutdown)disposalOutsideScope=true;}}
'''
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default=shutil.which('clang++') or 'g++');p.add_argument('--sanitize',action='store_true');p.add_argument('--mutations',action='store_true');a=p.parse_args()
    out=Path(tempfile.mkdtemp(prefix='image-recovery-lifecycle-',dir=ROOT/'.tmp'));env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
    names=['tools/tests/renderer_image_recovery_lifecycle.py','tools/tests/renderer_image_policy_restart.py','tools/tests/renderer_image_recovery_fixture.py','tools/tests/filesystem_case_segments.py','tools/tests/native/RendererImageRecoveryLifecycleTest.cpp','src/renderer/RenderSystem_init.cpp','src/renderer/RendererResourceSettings.cpp','src/renderer/RendererResourceSettings.h','src/renderer/RendererConsumedPolicy.h','src/renderer/RenderModuleAPI.h','src/renderer/DisplayPresentation.h','src/renderer/RendererImageRecovery.cpp','src/renderer/RendererImageRecovery.h','src/imagetools/ImageRecoveryEnvelope.h','src/imagetools/ImageContentIdentity.cpp','src/imagetools/ImageContentIdentity.h','src/idlib/CryptoHash.cpp','src/idlib/CryptoHash.h']
    sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    record={'passed':False,'sources':{n:sha(ROOT/n) for n in names},'runs':[],'scope':__doc__}
    try:
        body=(ROOT/'src/renderer/RendererResourceSettings.cpp').read_text()
        body='\n'.join(l for l in body.splitlines() if not l.startswith('#include "') and not l.startswith('#pragma hdrstop'))
        routes=(ROOT/'src/renderer/RenderSystem_init.cpp').read_text()
        shutdown=function_body(routes,'void idRenderSystemLocal::Shutdown(')
        # First-effect prefixes qualify admission, not full initialization.
        initialize=function_body(routes,'void idRenderSystemLocal::Init(')
        initialize=initialize[:initialize.index('\tr_initialRendererDevicePending = false;')]+'\t++initializations;\n}\n'
        device=function_body(routes,'static bool R_InitRendererDevice( bool legacyPolicy, bool forceWindow, char *error, int errorSize ) {')
        device=device[:device.index('\t// if the device')]+ '\t++deviceInitializations;return true;\n}\n'
        support=restart.SUPPORT.replace('struct idBinaryImage{','static void OnCpuDestroy();\nstruct idBinaryImage{~idBinaryImage(){OnCpuDestroy();}')
        support=support.replace('struct Images {','static void ShutdownBoundary(const char*);\nstruct Images {void PurgeAllImages(){ShutdownBoundary("purge");}\nvoid Shutdown(){ShutdownBoundary("images");R_ImagePolicyLifecycleChanged();images.clear();}')
        tests=(ROOT/'tools/tests/native/RendererImageRecoveryLifecycleTest.cpp').read_text()
        prefix=restart.MAIN[:restart.MAIN.index('int main()')]
        cases=[('positive',body,shutdown)]
        if a.mutations:
            changes=[
                ('missing-completion','shutdown','\t(void)imageOwnerShutdown.Complete();',''),
                ('dispose-before-device-cleanup','shutdown','\tglobalImages->Shutdown();','\tglobalImages->Shutdown();\n\t(void)imageOwnerShutdown.Complete();'),
                ('inactive-admission','body','if (active) { FailLocked("Full renderer shutdown interrupted checked image work"); return; }','(void)0;'),
                ('wrong-thread-admission','body','(rendererThread != std::thread::id() && rendererThread != std::this_thread::get_id())','false'),
                ('unwind-forget','body','if (!completed) recoveryInvalidated = true;','if (!completed) {preparedRecovery.reset();recovery.reset();}'),
                ('forget-one-direction','body','retired.swap(preparedRecovery); retiredBaseline.swap(recovery);','retiredBaseline.swap(recovery);'),
                ('keep-invalidated','body','recoveryInvalidated = false;\n        AdvanceMutationEpoch();','AdvanceMutationEpoch();'),
                ('reuse-preparation','body','completed = true;','completed = true;nextPreparation=0;'),
                ('initialize-during-shutdown','body','if (fullOwnerShutdown) return false;\n    if (rendererThread','if (rendererThread'),
                ('operation-during-shutdown','body','if (fullOwnerShutdown) return false;\n    if (!active)','if (!active)'),
                ('replacement-manager','body','globalImages != imageOwner ||','false ||'),
                ('repopulated-manager','body','globalImages->images.Num() != 0','false'),
            ]
            for label,where,old,new in changes:
                source=body if where=='body' else shutdown;assert source.count(old)==1,(label,source.count(old))
                cases.append((label,source.replace(old,new) if where=='body' else body,source.replace(old,new) if where=='shutdown' else shutdown))
        for label,core,owner in cases:
            cpp=out/(label+'.cpp');cpp.write_text(support+core+prefix+SUPPORT+owner+initialize+device+tests)
            exe=out/(label+'.exe');sources=[cpp]+[ROOT/n for n in ('src/renderer/RendererImageRecovery.cpp','src/imagetools/ImageContentIdentity.cpp','src/idlib/CryptoHash.cpp')]
            cmd=[a.compiler,'-std=c++20','-pthread','-I',str(ROOT/'src/renderer')]
            if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0','-no-pie']
            cmd+=sources+['-o',exe]
            if Path(a.compiler).stem.lower() in ('cl','clang-cl'):
                assert not a.sanitize
                cmd=[a.compiler,'/nologo','/std:c++20','/EHsc','/MTd','/D_DEBUG','/I'+str(ROOT/'src/renderer')]+sources+['/Fe:'+str(exe),'/Fo:'+str(out)+os.sep]
            for step,command in [('compile',cmd),('run',[exe])]:
                r=subprocess.run([str(v) for v in command],cwd=out,env=env,capture_output=True,text=True,timeout=120)
                log=out/(label+'-'+step+'.log');log.write_text(r.stdout+r.stderr)
                record['runs'].append(dict(case=label,step=step,exit=r.returncode,log=str(log),sha256=sha(log)))
                print(label,step,r.returncode,(r.stdout+r.stderr)[-1000:],flush=True)
                if (step=='compile' and r.returncode) or (step=='run' and ((label=='positive')!=(r.returncode==0))):raise RuntimeError(str(log))
        record['passed']=True
    finally:
        record['sources_unchanged']=all(sha(ROOT/n)==h for n,h in record['sources'].items())
        record['artifacts']={str(f):sha(f) for f in out.iterdir() if f.is_file()}
        (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(out/'result.json',flush=True)
if __name__=='__main__':main()
