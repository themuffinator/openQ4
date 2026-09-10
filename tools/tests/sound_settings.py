#!/usr/bin/env python3
"""Actual checked audio methods against counted OpenAL APIs; no native device calls."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def body(text, signature):
    at=text.index(signature); start=text.index('{',at); level=0
    tokens=re.finditer(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',text[start:],re.S)
    for m in tokens:
        if m.group()=='{': level+=1
        elif m.group()=='}':
            level-=1
            if not level: return text[at:start+m.end()]
    raise ValueError(signature)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler',default='clang++');ap.add_argument('--repository',type=Path);ap.add_argument('--no-mutations',action='store_true');ap.add_argument('--sanitize',action='store_true');a=ap.parse_args()
    repo=(a.repository or (ROOT if (ROOT/'subprojects').exists() else ROOT.parents[1])).resolve();scratch=Path(tempfile.mkdtemp(prefix='sound-settings-',dir=ROOT/'.tmp'))
    paths=[ROOT/'src/sound/SoundSettings.cpp',ROOT/'src/sound/SoundSettings.h',ROOT/'src/sound/OpenAL/AL_SoundVoice.cpp',ROOT/'src/sound/OpenAL/AL_SoundVoice.h',ROOT/'src/sound/OpenAL/AL_SoundHardware.cpp',ROOT/'src/sound/OpenAL/AL_SoundHardware.h',ROOT/'src/sound/snd_system.cpp',Path(__file__),ROOT/'tools/tests/native/SoundSettingsTest.cpp']
    text=paths[0].read_text(); voice=paths[2].read_text(); hardware=paths[4].read_text(); system=paths[6].read_text()
    assert '#include "SoundSettings.h"' in system
    for signature, token in [('void idSoundHardware_OpenAL::Init()', 'if (SoundSettings_BlockAutomaticRestart()) return;'),('void idSoundHardware_OpenAL::Shutdown()', 'SoundSettings_DeviceDestroyed();'),('bool idSoundHardware_OpenAL::UpdateDeviceMonitoring()', 'if (SoundSettings_BlockAutomaticRestart()) return false;')]:
        assert token in body(hardware,signature)
    assert 'SoundSettings_DeviceEvent();' in body(hardware,'static void ALC_APIENTRY openQ4_OpenALDeviceEventCallback')
    assert 'if (SoundSettings_BlockAutomaticRestart()) return;' in body(system,'void idSoundSystemLocal::Restart()')
    assert 'soundSettingsSourceGeneration = SoundSettings_SourceCreated();' in body(voice,'void idSoundVoice_OpenAL::Create(')
    assert 'soundSettingsSourceGeneration = 0;' in body(voice,'void idSoundVoice_OpenAL::DestroyInternal()')
    source=text.replace('#include "snd_local.h"','').replace('#include <SDL3/SDL_init.h>','')
    constants=voice[voice.index('static const float OPENQ4_OPENAL_PORTAL_DIRECT_'):voice.index('/*\n========================\nidSoundVoice_OpenAL::idSoundVoice_OpenAL')]
    production=source+'\n'+constants+'\n'+body(voice,'bool idSoundVoice_OpenAL::ApplyWetDryRoutingChecked(')+'\n'+body(voice,'void idSoundVoice_OpenAL::DestroyWetDryFilters()')+'\n'+body(hardware,'void idSoundHardware_OpenAL::Update()')+'\n'+body(system,'void idSoundSystemLocal::Render()')
    # Retain the engine's actual 6 dB conversion and nonfinite-bit predicate.
    # Only the elementary idMath ClampFloat/Pow calls use standard math doubles.
    math=(repo/'src/idlib/math/Math.h').read_text()
    db=body((repo/'src/sound/snd_local.h').read_text(),'ID_INLINE_EXTERN float DBtoLinear(').replace('ID_INLINE_EXTERN','static',1)
    bits=body(math,'ID_INLINE unsigned int idMath_FloatBits(').replace('ID_INLINE','static',1)
    nan=next(line for line in math.splitlines() if line.startswith('#define') and 'FLOAT_IS_NAN(x)' in line)
    helpers='struct idMath { static float ClampFloat(float a,float b,float c){return std::clamp(c,a,b);} static float Pow(float x,float y){return std::pow(x,y);} };\n'+db+'\n'+bits+'\n'+nan+'\n'
    generated=paths[-1].read_text().replace('// @PRODUCTION@',helpers+production)
    # The portable capture entry point is linked in this same production TU.
    # Its new provider-string query is deliberately unused by the original
    # suite; sound_recovery.py qualifies the actual capture/enumeration path.
    generated=generated.replace('extern "C" {', 'extern "C" {\nconst ALchar* AL_APIENTRY alGetString(ALenum) noexcept { Fake::Tick(); return "unused-counted-provider"; }',1)
    recovery_source=ROOT/'src/sound/SoundRecovery.cpp'
    working=scratch/'test.cpp';working.write_text(generated)
    # Mutations that restore a borrowed public POD parameter must also change
    # its actual declaration, so they execute instead of merely failing linkage.
    header=paths[1].read_text();header_copy=scratch/'SoundSettings.h';header_copy.write_text(header)
    cmd=[a.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-but-set-variable','-DUSE_OPENAL','-DUSE_SDL3','-DOPENQ4_OPENAL_EFX_SUPPORTED=1','-I',str(ROOT/'src/sound'),'-I',str(repo/'subprojects/openal-soft-prebuilt/include'),str(working),str(recovery_source),'-o',str(scratch/'test.exe')]
    if Path(a.compiler).name.lower() in ('cl','cl.exe'):
        cmd=[a.compiler,'/nologo','/std:c++20','/EHsc','/W4','/WX','/permissive-', '/MTd',
             '/DUSE_OPENAL','/DUSE_SDL3','/DOPENQ4_OPENAL_EFX_SUPPORTED=1',
             '/I'+str(ROOT/'src/sound'),'/I'+str(repo/'subprojects/openal-soft-prebuilt/include'),str(working),str(recovery_source),
             '/Fe:'+str(scratch/'test.exe'),'/Fo:'+str(scratch)+os.sep]
    elif a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
    env=os.environ.copy();env.update(TEMP=str(scratch),TMP=str(scratch),TMPDIR=str(scratch))
    def run(command,log):
        p=subprocess.run(command,env=env,cwd=scratch,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace');(scratch/log).write_text(p.stdout,encoding='utf-8');return p
    commands={}
    def variant(name):
        # Preserve each executable instead of reopening one Windows image name
        # immediately after exit (linker/antivirus sharing can outlive the child).
        exe=scratch/(name+'.exe')
        command=[arg.replace(str(scratch/'test.exe'),str(exe)) for arg in cmd]
        commands[name]=command
        return command,exe
    result={'sources':{str(p.relative_to(ROOT)):sha(p) for p in paths},'command':cmd,'limitations':['Counted API doubles; no actual device, mixer, audibility, playback continuity or hardware error timing qualification.','Real OpenAL public declarations; bounded engine/CVar/container doubles. Production API, complete checked voice route and normal Render/hardware Update bodies execute.','Legacy Init/Shutdown/Restart/monitoring integration guarded by source checks, not whole-engine compilation.']}
    result['sources'].update({str(p.relative_to(ROOT)):sha(p) for p in [recovery_source,ROOT/'src/sound/SoundRecovery.h']})
    dependencies=['src/idlib/math/Math.h','src/sound/snd_local.h','src/sound/snd_world.cpp','src/sound/SoundVoice.h']+['subprojects/openal-soft-prebuilt/include/AL/'+f for f in ('al.h','alc.h','alext.h','efx.h')]
    result['read_only_dependencies']={f:sha(repo/f) for f in dependencies}
    c=run(cmd,'compile.log');result['compile']=c.returncode
    if c.returncode:print(c.stdout);raise RuntimeError(scratch)
    p=run([str(scratch/'test.exe')],'run.log');result['run']=p.returncode;result['stdout']=p.stdout
    if p.returncode:print(p.stdout);raise RuntimeError(scratch)
    stub_main='''
int main(){SoundSettingsLease l{4,5,6};SoundSettingsObservation o;o.generation=99;char e[3];
if(SoundSettings_Begin(1,2,{2,false,48},l,o,e,3)||l.token!=6||o.generation!=99)return 1;
if(SoundSettings_CancelCaptured(l,o,e,3)||SoundSettings_TryApply(l,o,e,3)||SoundSettings_TryRestore(l,o,e,3)||SoundSettings_RevalidateRestore(l,o,e,3)||SoundSettings_Query(l,o,e,3)||SoundSettings_CheckCompletion(l,o,e,3)||SoundSettings_Finish(l,o,e,3))return 2;
if(o.generation!=99||SoundSettings_BlockAutomaticRestart()||SoundSettings_NativeOperationCurrent()||SoundSettings_NormalUpdateBegin()||SoundSettings_SourceCreated())return 3;
SoundSettings_Abandon(l);SoundSettings_DeviceEvent();SoundSettings_DeviceDestroyed();SoundSettings_DeviceInitialized();SoundSettings_NormalUpdateEnd(false);
return 0;}
'''
    result['unsupported']={}
    for name,prefix in {'nonSDL':'#undef USE_SDL3\n','dedicated':'#define ID_DEDICATED 1\n',
                        'oldProvider':'#define AL_LIBTYPE_STATIC\n#include <AL/al.h>\n#include <AL/alc.h>\n#include <AL/alext.h>\n#include <AL/efx.h>\n#undef ALC_HRTF_SOFT\n'}.items():
        working.write_text(prefix+source+stub_main)
        command,exe=variant(name); c=run(command,name+'-compile.log')
        if c.returncode:print(c.stdout);raise RuntimeError(name+' stub compilation '+str(scratch))
        p=run([str(exe)],name+'-run.log');result['unsupported'][name]=p.returncode
        if p.returncode:raise RuntimeError(name+' stub behavior '+str(scratch))
    mutants={
      'borrow-caller-policy':('SoundSettingsPolicy target,','const SoundSettingsPolicy& target,'),
      'borrow-caller-lease':('SoundSettingsLease lease','const SoundSettingsLease& lease'),
      'nested-abandon-allowed':('!state.busy && !state.updating && Same(lease,state.lease)','!state.busy && Same(lease,state.lease)'),
      'recovery-apply-allowed':('if (!restore && state.restoreOnly)','if (false && !restore && state.restoreOnly)'),
      'recovery-nonbaseline-accepted':('!Same(Policy(),state.baseline.requested)','false'),
      'recovery-old-event-accepted':('events<=state.events','false'),
      'recovery-event-not-renewed':('state.events=events; state.restoreOnly=true;','state.restoreOnly=true;'),
      'recovery-old-proof-kept':('state.expected={}; state.routing={}; state.validUpdate=false; state.operationToken=0;','state.expected={};'),
      'recovery-output-marked-proved':('c.effectsVerified=effectProof;','c.effectsVerified=true;'),
      'owner-bypass':('a.owner==b.owner','(void(a.owner),void(b.owner),true)'),
      'accept-reset-fallback':('!(restore?result.outputMode==desired.outputMode:OutputMatches(result.outputMode,desired.outputMode))','false'),
      'reject-stereo-submodes':('return OutputMatches(mode,ALC_STEREO_SOFT)', 'return mode==ALC_STEREO_SOFT'),
      'accept-reset-false':('return accepted && noError;','return (void(accepted),true) && noError;'),
      'skip-normal-update':('|| !state.validUpdate ||\n\t\tstate.routing.routingGeneration!=generation || state.routing.routingUpdate<=state.attemptUpdate','|| false'),
      'skip-baseline-source':('int i=0;i<h.voices.Num();++i','int i=1;i<h.voices.Num();++i'),
      'skip-first-source':('c.sourceCount=0;\n\t\tfor (int i=0;','c.sourceCount=0;\n\t\tfor (int i=1;'),
      'mute-overwritten':('c.muted=soundSystemLocal.IsMuted();','soundSystemLocal.muted=false; c.muted=soundSystemLocal.IsMuted();'),
      'wet-disable-stale':('route?slot:AL_EFFECTSLOT_NULL','slot'),
      'stale-source-generation':('source==openalSource && lifetime==soundSettingsSourceGeneration','source==openalSource && (void(lifetime),true)'),
      'external-device-ignored':('strcmp(s_deviceName.GetString(),state.baseline.requestedDevice)==0','true'),
      'cancel-touched-audio':('state.phase!=SoundSettingsPhase::Captured','false'),
      'cancel-actual-drift':('!Actual(current,state.baseline) || !Dependencies()','!Dependencies()'),
      'completion-releases-lease':('return Completion(lease,out,error,size);','const bool result=Completion(lease,out,error,size); if(result)state.lease={}; return result;'),
      'completion-adopts-policy':('return Completion(lease,out,error,size);','const bool result=Completion(lease,out,error,size); if(result)SoundSettingsAccess::AdoptRequestedPolicy(); return result;'),
      'finish-ignores-current-proof':('!entry.entered || !Completion(lease,current,error,size)','!entry.entered || (false && !Completion(lease,current,error,size))'),
      'hotplug-ignored':('eventSerial.load(std::memory_order_acquire)==events','true'),
      'surround-hrtf-conflict':('target.speakers==6 && captured.requestedHrtf==2','false'),
      'slot-bind-omitted':('sloti(h.auxEffectSlot,AL_EFFECTSLOT_EFFECT,enabled?h.auxReverbEffect:AL_EFFECT_NULL);','(void)enabled;'),
      'reentrant-normal-validation':('struct UpdateExit { ~UpdateExit() { state.updating=false; } } exit;', 'state.updating=false;'),
      'lost-native-ownership':('if (!SoundSettings_NativeOperationCurrent()) return false;\n\tconst bool result=operation();\n\treturn SoundSettings_NativeOperationCurrent() && result;', 'return operation();'),
      'stale-final-source':('live.source!=routed.source || live.lifetime!=routed.lifetime || live.directFilter!=routed.directFilter','live.source!=routed.source || live.directFilter!=routed.directFilter'),
      'degraded-baseline-accepted':('if (h.efxFiltersAvailable && (!item.directFilter','if (false && (!item.directFilter'),
      'strict-filter-cleanup-lost':('else if( soundSettingsDeleteFilters != NULL )','else if( false )'),
      'external-output-overwritten':('state.phase!=SoundSettingsPhase::Failed && !Actual(current,state.expected)','false'),
    }
    result['mutants']={}
    if not a.no_mutations:
        for name,(old,new) in mutants.items():
            assert old in generated,name
            altered=generated.replace(old,new,-1 if name in ('recovery-nonbaseline-accepted','cancel-touched-audio') else 1)
            if name=='skip-baseline-source':
                # New portable resource preflight has another voice walk; this
                # existing mutation must still target BaselineSources itself.
                exact=body(generated,'static bool BaselineSources(')
                assert old in exact
                altered=generated.replace(exact,exact.replace(old,new,1),1)
            altered_header=header
            if name=='borrow-caller-policy':
                altered=generated.replace('SoundSettingsPolicy target,','const SoundSettingsPolicy& target,')
                altered_header=header.replace('SoundSettingsPolicy target,','const SoundSettingsPolicy& target,')
            elif name=='borrow-caller-lease':
                pattern=r'((?:SoundSettings_\w+|Attempt|Completion)\()SoundSettingsLease(?=[ ,)])'
                altered=re.sub(pattern,r'\1const SoundSettingsLease&',generated)
                altered_header=re.sub(pattern,r'\1const SoundSettingsLease&',header)
            working.write_text(altered);header_copy.write_text(altered_header);command,exe=variant(name);c=run(command,name+'-compile.log')
            if c.returncode:raise RuntimeError('Uncompiled mutant '+name+' '+str(scratch))
            p=run([str(exe)],name+'-run.log');result['mutants'][name]=p.returncode
            if not p.returncode:raise RuntimeError('Surviving mutant '+name+' '+str(scratch))
    working.write_text(generated);header_copy.write_text(header);result['passed']=True
    result['variant_commands']=commands
    (scratch/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'path':str(scratch/'result.json'),'stdout':result['stdout'],'mutants':len(result['mutants'])}));return 0
if __name__=='__main__':raise SystemExit(main())
