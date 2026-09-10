#!/usr/bin/env python3
"""Compile actual preference declarations, protection and config selection.

Uses counted registry/file storage with production UpdateCheat, SYSTEM Writable
and WriteFlaggedVariables method bodies. No devices or live config are changed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
PREFERENCES = {
    'r_displayRefresh': ('src/renderer/RenderSystem_init.cpp', '75'),
    'r_skipSky': ('src/renderer/RenderSystem_init.cpp', '1'),
    'image_writeGeneratedImages': ('src/imagetools/BinaryImage.cpp', '0'),
    's_maxEmitterChannels': ('src/sound/snd_world.cpp', '24'),
}
SUPPORT = r'''
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#define BIT(n) (1 << (n))
@FLAGS@
static unsigned checks=0;
static void Check(bool value,const char* reason) {
 ++checks; if(!value){std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);}
}
struct idInternalCVar {
 std::string name,value;int flags;
 idInternalCVar(const char* n,const char* v,int f):name(n),value(v),flags(f){UpdateCheat();}
 void UpdateCheat();
 int GetFlags()const{return flags;}
 const char* GetName()const{return name.c_str();}
 const char* GetString()const{return value.c_str();}
};
using idCVar=idInternalCVar;
struct SystemSettingDescriptor {std::string key;};
static bool Fail(std::string& error,const std::string& key,const char* reason){error=key+": "+reason;return false;}
struct idFile {
 std::string text;
 void Printf(const char* format,...){char buffer[512];va_list args;va_start(args,format);int n=std::vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);Check(n>=0&&n<int(sizeof(buffer)),"bounded config output");text+=buffer;}
};
struct Vars:std::vector<idInternalCVar*> {int Num()const{return int(size());}};
struct idCVarSystemLocal {Vars cvars;void WriteFlaggedVariables(int,const char*,idFile*)const;};
@METHODS@
int main(){
 std::vector<idInternalCVar> variables={@DECLARATIONS@};
 const char* changes[]={"75","1","0","24"};
 idCVarSystemLocal registry;std::string error;idFile file;
 for(size_t i=0;i<variables.size();++i){
  auto& v=variables[i];
  Check((v.GetFlags()&CVAR_ARCHIVE)!=0,v.GetName());
  Check(Writable({v.name},v,error),error.c_str());
  Check(!(v.GetFlags()&(CVAR_USERINFO|CVAR_SERVERINFO|CVAR_NETWORKSYNC)),"local preference cannot become network authority");
  v.value=changes[i];registry.cvars.push_back(&v);
 }
 idInternalCVar diagnostic("r_skipRender","0",@DIAGNOSTIC_FLAGS@);
 Check(!Writable({diagnostic.name},diagnostic,error),"unrelated render diagnostic remains protected");
 registry.cvars.push_back(&diagnostic);
 for(int protectedFlag:{CVAR_ROM,CVAR_INIT,CVAR_NETWORKSYNC,CVAR_PRIVATE}){
  idInternalCVar protectedValue("protected","1",CVAR_ARCHIVE|protectedFlag);
  Check(!Writable({protectedValue.name},protectedValue,error),"archive never bypasses explicit protection");
 }
 idInternalCVar privateValue("private","secret",CVAR_ARCHIVE|CVAR_PRIVATE);
 registry.cvars.push_back(&privateValue);
 registry.WriteFlaggedVariables(CVAR_ARCHIVE,"seta",&file);
 std::string expected;
 for(const auto& v:variables)expected+="seta "+v.name+" \""+v.value+"\"\n";
 Check(file.text==expected,"exact changed preference values selected for config; private/diagnostic excluded");
 std::printf("PASS %u checks; actual declarations, protection and archive selection.\n",checks);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'clang++'))
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise SystemExit('Required C++ compiler unavailable: ' + args.compiler)
    inputs = {}

    def read(name):
        path = ROOT / name
        inputs[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        return path.read_text(encoding='utf-8')

    def declaration(name, path):
        found = re.findall(r'\bidCVar\s+' + re.escape(name) + r'\s*\(\s*"' +
            re.escape(name) + r'"\s*,\s*("[^"\r\n]*")\s*,\s*([^,]+),', read(path))
        if len(found) != 1:
            raise ValueError('Expected one actual declaration: ' + name)
        return found[0]

    flags_header = read('src/framework/CVarSystem.h')
    flags = re.search(r'typedef enum\s*\{[^}]*\}\s*cvarFlags_t;', flags_header, re.S)
    if not flags:
        raise ValueError('CVar flags declaration unavailable')
    registry = read('src/framework/CVarSystem.cpp')
    host = read('src/ui/application/SystemSettingsHost.cpp')
    methods = '\n'.join([
        function_body(registry, 'void idInternalCVar::UpdateCheat('),
        function_body(registry, 'void idCVarSystemLocal::WriteFlaggedVariables('),
        function_body(host, 'bool Writable('),
    ])
    declarations = []
    for name, (path, _) in PREFERENCES.items():
        default, expression = declaration(name, path)
        declarations.append('{' + json.dumps(name) + ',' + default + ',' + expression + '}')
    _, diagnostic = declaration('r_skipRender', 'src/renderer/RenderSystem_init.cpp')
    source = SUPPORT.replace('@FLAGS@', flags[0]).replace('@METHODS@', methods)
    source = source.replace('@DECLARATIONS@', ','.join(declarations)).replace('@DIAGNOSTIC_FLAGS@', diagnostic)
    scratch = ROOT / '.tmp'
    scratch.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='settings-archive-policy-', dir=scratch))
    cpp = out / 'test.cpp'
    cpp.write_text(source, encoding='utf-8')
    exe = out / ('test.exe' if os.name == 'nt' else 'test')
    if Path(compiler).stem.lower() == 'cl':
        command = [compiler, '/nologo', '/MTd', '/std:c++20', '/EHsc', '/utf-8', '/W4', '/WX', str(cpp), '/Fe:' + str(exe)]
    else:
        command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)]
    compiled = subprocess.run(command, cwd=out, capture_output=True, text=True, encoding='utf-8', errors='replace')
    (out / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
    run = subprocess.run([str(exe)], cwd=out, capture_output=True, text=True) if not compiled.returncode else None
    if run:
        (out / 'run.log').write_text(run.stdout + run.stderr, encoding='utf-8')
    stable = all(hashlib.sha256((ROOT / p).read_bytes()).hexdigest() == digest for p, digest in inputs.items())
    report = dict(status='passed' if stable and run and run.returncode == 0 else 'failed',
        command=command, compile_exit=compiled.returncode, run_exit=run.returncode if run else None,
        source_bindings=inputs, inputs_unchanged=stable,
        artifacts={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()},
        scope='Actual declaration flags, cheat classification, typed SYSTEM write eligibility and exact archive serialization selection. Counted storage; no device execution or disk durability proof.')
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print((run.stdout + run.stderr) if run else compiled.stdout + compiled.stderr)
    print(out / 'result.json')
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
