#!/usr/bin/env python3
"""Portable recovery grammar and full checked capture against counted OpenAL."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--repository',type=Path);ap.add_argument('--compiler',default='clang++');ap.add_argument('--sanitize',action='store_true');ap.add_argument('--no-mutations',action='store_true');a=ap.parse_args()
    repo=(a.repository or ROOT).resolve();scratch=Path(tempfile.mkdtemp(prefix='sound-recovery-',dir=ROOT/'.tmp'))
    spec=importlib.util.spec_from_file_location('baseline',ROOT/'tools/tests/sound_settings.py');baseline=importlib.util.module_from_spec(spec);spec.loader.exec_module(baseline);body=baseline.body
    owned=['src/sound/SoundSettings.h','src/sound/SoundSettings.cpp','src/sound/SoundRecovery.h','src/sound/SoundRecovery.cpp','tools/tests/native/SoundRecoveryTest.cpp','tools/tests/sound_recovery.py']
    deps=['src/sound/OpenAL/AL_SoundVoice.cpp','src/sound/OpenAL/AL_SoundVoice.h','src/sound/OpenAL/AL_SoundHardware.cpp','src/sound/OpenAL/AL_SoundHardware.h','src/sound/snd_system.cpp','tools/tests/sound_settings.py','tools/tests/native/SoundSettingsTest.cpp']
    source=(ROOT/owned[1]).read_text().replace('#include "snd_local.h"','').replace('#include <SDL3/SDL_init.h>','')
    voice=(ROOT/deps[0]).read_text();hardware=(ROOT/deps[2]).read_text();system=(ROOT/deps[4]).read_text()
    constants=voice[voice.index('static const float OPENQ4_OPENAL_PORTAL_DIRECT_'):voice.index('/*\n========================\nidSoundVoice_OpenAL::idSoundVoice_OpenAL')]
    production=source+'\n'+constants+'\n'+body(voice,'bool idSoundVoice_OpenAL::ApplyWetDryRoutingChecked(')+'\n'+body(voice,'void idSoundVoice_OpenAL::DestroyWetDryFilters()')+'\n'+body(hardware,'void idSoundHardware_OpenAL::Update()')+'\n'+body(system,'void idSoundSystemLocal::Render()')
    math=(repo/'src/idlib/math/Math.h').read_text();db=body((repo/'src/sound/snd_local.h').read_text(),'ID_INLINE_EXTERN float DBtoLinear(').replace('ID_INLINE_EXTERN','static',1)
    bits=body(math,'ID_INLINE unsigned int idMath_FloatBits(').replace('ID_INLINE','static',1)
    nan=next(line for line in math.splitlines() if line.startswith('#define') and 'FLOAT_IS_NAN(x)' in line)
    helpers='struct idMath { static float ClampFloat(float a,float b,float c){return std::clamp(c,a,b);} static float Pow(float x,float y){return std::pow(x,y);} };\n'+db+'\n'+bits+'\n'+nan+'\n'
    extension=(ROOT/owned[4]).read_text();query,tests=extension.split('// @QUERY_DOUBLES@',1)[1].split('// @TESTS@',1)
    fixture=(ROOT/deps[-1]).read_text().replace('int main(){','static int LegacyMain(){').replace('printf("%d checks passed\\n",checks);}', 'printf("%d checks passed\\n",checks);return 0;}')
    fixture=fixture.replace('static bool denyAllocation=false;','static bool denyAllocation=false;\nstatic long allocCountdown=-1;')
    fixture=fixture.replace('void* operator new(std::size_t n) {if(denyAllocation)', '#if defined(_MSC_VER)\n__declspec(noinline)\n#else\n__attribute__((noinline))\n#endif\nvoid* operator new(std::size_t n) {if(allocCountdown>0)--allocCountdown;else if(allocCountdown==0)throw std::bad_alloc();if(denyAllocation)')
    fixture=fixture.replace('extern "C" {',query+'\nextern "C" {',1)
    old=body(fixture,'const ALCchar* ALC_APIENTRY alcGetString(')
    fixture=fixture.replace(old,'''const ALCchar* ALC_APIENTRY alcGetString(ALCdevice* d,ALCenum p) noexcept {RecoveryFake::Query();
if(p==ALC_HRTF_SPECIFIER_SOFT)return RecoveryFake::specifier.c_str();
if(!d&&(p==ALC_ALL_DEVICES_SPECIFIER||p==ALC_DEVICE_SPECIFIER))return RecoveryFake::names.c_str();
return p==ALC_DEFAULT_ALL_DEVICES_SPECIFIER||p==ALC_DEFAULT_DEVICE_SPECIFIER?Fake::defaultName.c_str():Fake::actual.c_str();}''')
    fixture=fixture.replace('switch(p){case ALC_CONNECTED:', 'switch(p){case ALC_NUM_HRTF_SPECIFIERS_SOFT:*v=RecoveryFake::countOverride==-1?static_cast<int>(RecoveryFake::hrtfs.size()):RecoveryFake::countOverride;break;case ALC_CONNECTED:')
    fixture=fixture.replace('return Fake::missingProc==p?nullptr:reinterpret_cast<void*>(&Fake::Reset);','return Fake::missingProc==p?nullptr:std::strcmp(p,"alcGetStringiSOFT")==0?reinterpret_cast<void*>(&RecoveryFake::Stringi):reinterpret_cast<void*>(&Fake::Reset);')
    fixture=fixture.replace('return std::strcmp(p,"ALC_EXT_EFX")==0?Fake::efxSupport:Fake::support;', 'return std::strcmp(p,"ALC_EXT_EFX")==0?Fake::efxSupport:(std::strcmp(p,"ALC_ENUMERATE_ALL_EXT")==0||std::strcmp(p,"ALC_ENUMERATION_EXT")==0)?RecoveryFake::enumeration:Fake::support;')
    old=body(fixture,'ALCboolean ALC_APIENTRY alcIsExtensionPresent(')
    fixture=fixture.replace(old,'ALCboolean ALC_APIENTRY alcIsExtensionPresent(ALCdevice*,const ALCchar* p) noexcept {Fake::Tick();return std::strcmp(p,"ALC_EXT_EFX")==0?Fake::efxSupport:(std::strcmp(p,"ALC_ENUMERATE_ALL_EXT")==0||std::strcmp(p,"ALC_ENUMERATION_EXT")==0)?RecoveryFake::enumeration:Fake::support;}')
    generated=fixture.replace('// @PRODUCTION@',helpers+production)+'\n'+tests
    working=scratch/'test.cpp';working.write_text(generated);codec=scratch/'SoundRecovery.cpp';original_codec=(ROOT/owned[3]).read_text();codec.write_text(original_codec)
    exe=scratch/'test.exe';msvc=Path(a.compiler).name.lower() in ('cl','cl.exe')
    cmd=[a.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-but-set-variable','-DUSE_OPENAL','-DUSE_SDL3','-DOPENQ4_OPENAL_EFX_SUPPORTED=1','-I',str(ROOT/'src/sound'),'-I',str(repo/'subprojects/openal-soft-prebuilt/include'),str(working),str(codec),'-o',str(exe)]
    if msvc:cmd=[a.compiler,'/nologo','/std:c++20','/EHsc','/W4','/WX','/permissive-','/MTd','/DUSE_OPENAL','/DUSE_SDL3','/DOPENQ4_OPENAL_EFX_SUPPORTED=1','/I'+str(ROOT/'src/sound'),'/I'+str(repo/'subprojects/openal-soft-prebuilt/include'),str(working),str(codec),'/Fe:'+str(exe),'/Fo:'+str(scratch)+os.sep]
    elif a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
    env=os.environ.copy();env.update(TEMP=str(scratch),TMP=str(scratch),TMPDIR=str(scratch))
    result={'sources':{p:sha(ROOT/p) for p in owned},'fixed_dependencies':{p:sha(ROOT/p) for p in deps},'read_only_dependencies':{p:sha(repo/p) for p in ['src/idlib/math/Math.h','src/sound/snd_local.h']+['subprojects/openal-soft-prebuilt/include/AL/'+f for f in ('al.h','alc.h','alext.h','efx.h')]},'commands':{},'mutants':{},'limitations':['Actual recovery codec and capture plus prior full checked audio/voice/update methods, using counted native APIs and bounded engine doubles. No live device, playback, audibility, native driver timing or cold recreation qualification.','Portable v1 deliberately refuses actual effect/slot/filter resources; full EFX reconstruction remains required. Logical specifiers are not physical endpoint IDs. No host journal/startup integration.']}
    def run(command,name):
        result['commands'][name]=command;p=subprocess.run(command,cwd=scratch,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace');(scratch/(name+'.log')).write_text(p.stdout,encoding='utf-8');return p
    def qualify(name):
        output=scratch/(name+'.exe');command=[s.replace(str(exe),str(output)) for s in cmd];c=run(command,name+'-compile')
        if c.returncode:print(c.stdout);raise RuntimeError('compile '+str(scratch))
        return run([str(output)],name+'-run')
    p=qualify('baseline');result['stdout']=p.stdout
    if p.returncode:print(p.stdout);raise RuntimeError('behavior '+str(scratch))
    result['unsupported']={}
    stub='''
int main(){SoundRecoveryData d;d.requested={2,false,48};d.provider={"vendor","renderer","version"};d.actualDevice="device";
SoundRecoveryRecord r;std::string e(0,'\\0');if(!SoundRecovery_BuildObserved(d,r,e))return 1;auto* before=r.Value();char error[4]{};
if(SoundSettings_CaptureRecovery({1,2,3},r,error,sizeof(error))||r.Value()!=before||error[0]||error[3])return 2;
if(SoundSettings_BlockAutomaticRestart())return 3;
return 0;}
'''
    for name,prefix in {'nonSDL':'#undef USE_SDL3\n','dedicated':'#define ID_DEDICATED 1\n','oldProvider':'#define AL_LIBTYPE_STATIC\n#include <AL/al.h>\n#include <AL/alc.h>\n#include <AL/alext.h>\n#include <AL/efx.h>\n#undef ALC_HRTF_SOFT\n'}.items():
        working.write_text(prefix+source+stub);p=qualify(name);result['unsupported'][name]=p.returncode
        if p.returncode:raise RuntimeError('stub '+name+' '+str(scratch))
    working.write_text(generated)
    mutations={
      'unknown-field':('codec','input.size()!=(candidate->form==SoundRecoveryForm::Exact?19u:18u)','false'),
      'pending-used-as-confirmed':('codec','return d.form==SoundRecoveryForm::Exact;','return (void(d),true);'),
      'integer-roundoff':('codec','if (normalized!=std::bit_cast<std::uint64_t>(static_cast<double>(integer))) return false;','(void)normalized;'),
      'duplicate-device':('codec','devices!=1','devices==0'),
      'duplicate-hrtf':('codec','hrtfs!=1','hrtfs==0'),
      'wrong-hrtf-name':('codec','r->hrtfSpecifier!=a->hrtfSpecifier','false'),
      'default-diagnostic-as-authority':('codec','a.hrtfPolicy==b.hrtfPolicy && a.efxDebug==b.efxDebug;','a.defaultDevice==b.defaultDevice && a.hrtfPolicy==b.hrtfPolicy && a.efxDebug==b.efxDebug;'),
      'requested-actual-collapse':('codec','if (target.speakers!=before->requested.speakers)','if (true)'),
      'allow-effect-enable':('codec','if (target.efx && target.efx!=before->requested.efx)','if (false)'),
      'allow-idle-filter-resources':('capture','if (h.voices[i].openalDirectFilter || h.voices[i].openalAuxFilter)','if (h.voices[i].openalSource && (h.voices[i].openalDirectFilter || h.voices[i].openalAuxFilter))'),
      'allow-uncompleted-capture':('capture','} else if (!Completion(lease,observed,error,size) || !current()) return false;','} else if (!SoundSettingsAccess::Read(observed) || !current()) return false;'),
    }
    if not a.no_mutations:
        for name,(where,old,new) in mutations.items():
            text=original_codec if where=='codec' else generated
            if old not in text:raise RuntimeError('missing mutant '+name)
            working.write_text(generated if where=='codec' else text.replace(old,new,1));codec.write_text(text.replace(old,new,1) if where=='codec' else original_codec)
            p=qualify(name);result['mutants'][name]=p.returncode
            if not p.returncode:raise RuntimeError('surviving '+name+' '+str(scratch))
    working.write_text(generated);codec.write_text(original_codec);result['passed']=True
    (scratch/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'path':str(scratch/'result.json'),'stdout':result['stdout'],'mutants':len(result['mutants'])}))
if __name__=='__main__':main()
