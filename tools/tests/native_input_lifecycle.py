#!/usr/bin/env python3
"""Actual CVar setters and copied Session/manager/window producer methods.

No GUI/native/OS input. Windows window getter is counted; Linux also executes
that explicitly projected POD producer branch, not a claimed Linux HWND path.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,tempfile
from filesystem_case_segments import function_body
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def body(path):return (ROOT/path).read_text(encoding='utf-8-sig')
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--compiler',default='clang++')
    parser.add_argument('--msvc-debug',action='store_true');parser.add_argument('--sanitizers',action='store_true');parser.add_argument('--no-mutations',action='store_true')
    a=parser.parse_args();msvc=Path(a.compiler).name.lower() in ('cl','cl.exe')
    folder=Path(tempfile.mkdtemp(prefix='native-input-lifecycle-',dir=ROOT/'.tmp'))
    cvar=body('src/framework/CVarSystem.cpp');session=body('src/framework/Session.cpp');manager=body('src/ui/UserInterface.cpp');sdl=body('src/sys/sdl3/sdl3_backend.cpp')
    cvar_flags=re.search(r'typedef enum \{\s*CVAR_ALL.*?\}\s*cvarFlags_t;',body('src/framework/CVarSystem.h'),re.S)[0]
    includes={'cvar_flags.inc':cvar_flags,'cvar_methods.inc':''.join(function_body(cvar,'void idInternalCVar::'+method+'(') for method in
        ['Set','Reset','Update','UpdateValue','UpdateCheat','InternalSetString','InternalServerSetString','InternalSetBool','InternalSetInteger','InternalSetFloat']),
        'session_query.inc':function_body(session,'bool idSessionLocal::QueryNativeInputPublication('),
        'manager_queries.inc':function_body(manager,'void idUserInterfaceManaged::MarkNativeInputClosing(')+function_body(manager,'void idUserInterfaceManaged::SetNativeInputChanging(')+function_body(manager,'bool idUserInterfaceManagerLocal::QueryNativeInputAllocation('),
        'window_publications.inc':sdl[sdl.index('static openq4::NativeWindowPublication s_nativeInputWindow'):sdl.index('static bool s_haveAbsoluteMousePosition')]}
    # Every simple pinned mutation is preceded by invalidation. Constructor
    # initializer lists are lifecycle-before-existence; chained Session globals
    # have one explicit pre-chain invalidation. These are coverage guards, while
    # the projected methods below provide the behavioral ordering assertions.
    coverage=[]
    for filename,pattern,hook in [
        ('src/framework/Session.cpp',r'(?m)^\s*(guiActive|guiTest|insideExecuteMapChange|loadingSaveGame)\s*=(?!=)','NativeInputBeforeSessionChange'),
        ('src/framework/Session_menu.cpp',r'(?m)^\s*guiActive\s*=(?!=)','NativeInputBeforeSessionChange'),
        ('src/framework/Console.cpp',r'(?m)^\s*keyCatching\s*=(?!=)','NativeInputBeforeInputBlockerChange'),
        ('src/sys/sdl3/sdl3_backend.cpp',r'(?m)^\s*(win32.activeApp|s_sdlAppInBackground|s_sdlVideoReferenceHeld)\s*=(?!=)','NativeInputBeforeWindowChange')]:
        value=body(filename)
        for match in re.finditer(pattern,value):
            prefix=value[:match.start()].rstrip().splitlines()[-1]
            if hook not in prefix:raise RuntimeError(filename+' mutation missing preceding hook at '+str(value[:match.start()].count('\n')+1))
        coverage.append({'path':filename,'mutations':len(list(re.finditer(pattern,value)))})
    files=['src/framework/CVarSystem.cpp','src/framework/CVarSystem.h','src/framework/Session.cpp','src/framework/Session_menu.cpp',
        'src/framework/Console.cpp','src/ui/UserInterface.cpp','src/sys/sdl3/sdl3_backend.cpp','src/framework/NativeInputPublications.h',
        'tools/tests/native/NativeInputLifecycleTest.cpp','tools/tests/native_input_lifecycle.py','tools/tests/filesystem_case_segments.py']
    original={n:sha(ROOT/n) for n in files};report={'passed':False,'sources':original,'coverage':coverage,'cases':[]}
    flags=['/nologo','/std:c++20','/EHsc','/W4','/WX','/wd4244','/MTd' if a.msvc_debug else '/MT'] if msvc else ['-std=c++20','-Wall','-Wextra','-Werror','-Wno-misleading-indentation']
    if a.msvc_debug:flags+=['/D_DEBUG']
    if os.name!='nt' and not msvc:flags+=['-pthread']
    if a.sanitizers:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    flags += [('/I' if msvc else '-I')+str(n) for n in [ROOT,folder]]
    env={**os.environ,'TEMP':str(folder),'TMP':str(folder),'TMPDIR':str(folder)}
    if a.sanitizers:env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
    def case(name,changed=None,mutant=False):
        for path,text in includes.items():(folder/path).write_text(changed.get(path,text) if changed else text,encoding='utf-8',newline='\n')
        exe=folder/(name+('.exe' if os.name=='nt' else ''))
        args=[a.compiler]+flags+[str(ROOT/'tools/tests/native/NativeInputLifecycleTest.cpp')]+(['/Fe:'+str(exe),'/Fo:'+str(folder/'test.obj')] if msvc else ['-o',str(exe)])
        r=subprocess.run(args,cwd=folder,env=env,capture_output=True,text=True,timeout=90);(folder/(name+'-compile.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        r=subprocess.run([str(exe)],cwd=folder,env=env,capture_output=True,text=True,timeout=45);(folder/(name+'-run.log')).write_text(r.stdout+r.stderr)
        report['cases'].append({'name':name,'exit':r.returncode,'sha256':sha(exe),'output':r.stdout+r.stderr})
        if (r.returncode!=0)!=mutant:raise RuntimeError(name+' unexpected result\n'+r.stdout+r.stderr)
        if mutant and 'FAIL' not in r.stderr:raise RuntimeError(name+' failed without checked assertion')
        print(name+': '+('rejected' if mutant else r.stdout.strip()),flush=True)
    try:
        case('baseline')
        mutations=[
            ('setter-missing','cvar_methods.inc','if (nameString.Icmp("com_asyncInput") == 0) openq4::NativeInputBeforeInputBlockerChange();',''),
            ('reset-missing','cvar_methods.inc','if (nameString.Icmp("com_asyncInput") == 0 && valueString.Cmp(resetString.c_str()) != 0)\n\t\topenq4::NativeInputBeforeInputBlockerChange();',''),
            ('setter-too-late','cvar_methods.inc','value = valueString.c_str();\n\tUpdateValue();','value = valueString.c_str();\n\tUpdateValue();'),
            ('closing-owner-visible','manager_queries.inc','!(allocation->nativeInputClosing || allocation->nativeInputChanging)','!allocation->nativeInputChanging'),
            ('changing-owner-visible','manager_queries.inc','!(allocation->nativeInputClosing || allocation->nativeInputChanging)','!allocation->nativeInputClosing'),
            ('closing-after-publication','manager_queries.inc','openq4::NativeInputBeforeUiChange(allocationId);\n    nativeInputClosing = true;','nativeInputClosing = true;\n    openq4::NativeInputBeforeUiChange(allocationId);'),
            ('manager-busy-visible','manager_queries.inc','clipboardBoundaryActive || applicationPumpDepth || !current','!current'),
            ('loading-route-allowed','session_query.inc',' && !loadingSaveGame',''),
            ('async-route-allowed','session_query.inc','!com_asyncInput.GetBool() && ',' '),
            ('failed-transition-rejects-cleanup','session_query.inc','value.transition = transition;','if(!transition)return false;value.transition = transition;'),
            ('window-fault-rejects-provider','window_publications.inc','if(!Sys_EventDispositionBoundThread())return false;','if(!Sys_EventDispositionBoundThread() || s_nativeWindowExhausted)return false;'),
            ('window-lifetime-reused','window_publications.inc','s_nativeInputWindow.lifetime=++s_nativeWindowHighwater;','s_nativeInputWindow.lifetime=1;'),
            ('claim-registration-unchecked','window_publications.inc','a.association==b.association && a.registration==b.registration','a.association==b.association'),
        ]
        if not a.no_mutations:
            for name,file,old,new in mutations:
                text=includes[file]
                if name=='setter-too-late':
                    old='if (nameString.Icmp("com_asyncInput") == 0) openq4::NativeInputBeforeInputBlockerChange();\n\tCVar_AssignString( valueString, newValue, ( flags & CVAR_PRIVATE ) != 0 );'
                    new='CVar_AssignString( valueString, newValue, ( flags & CVAR_PRIVATE ) != 0 );\n\tif (nameString.Icmp("com_asyncInput") == 0) openq4::NativeInputBeforeInputBlockerChange();'
                expected=2 if name=='setter-missing' else 1
                if text.count(old)!=expected:raise RuntimeError('Mutation anchor '+name+' count='+str(text.count(old)))
                case(name,{file:text.replace(old,new)},True)
        report['passed']=original=={n:sha(ROOT/n) for n in files}
    finally:
        report['logs']={p.name:sha(p) for p in folder.glob('*.log')};(folder/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(folder/'result.json')
    if not report['passed']:raise RuntimeError('Sources changed')
if __name__=='__main__':main()
