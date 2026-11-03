#!/usr/bin/env python3
"""Engine screenshots of startup, menu depth, recovery and death; no host input."""
import argparse
import json
from pathlib import Path
import capture_retained_screens as capture

ROOT = Path(__file__).resolve().parents[2]

def accept(ident):
    return [f'openq4_retainedGui focus {ident.removeprefix("nav_")}', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0']

def shot(name):
    return f'screenshot "screenshots/{name}.tga"'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,default=ROOT/'.install')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--renderer',choices=('gl','vulkan'),default='gl')
    parser.add_argument('--size',default='1280x720')
    parser.add_argument('--scenario',action='append',choices=('menus','recovery','death','death-reduced','death-mp','crash','crash-reduced','progression'))
    parser.add_argument('--language',default='english')
    args=parser.parse_args(); args.output=args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT/'.tmp').resolve()):
        parser.error('choose a fresh directory under the project .tmp')
    args.gate='1';args.timeout=240
    capture.SCENARIOS['menus']=(None,[
        'waitMsec 2000','testGUI guis/loading/initialization.q4ui','waitMsec 500',shot('initialization'),
        'set ui_retainedReducedMotion 1','openq4_retainedGui event onInit','waitMsec 300',shot('initialization-still'),
        'testGUI','set ui_retainedReducedMotion 0','campaignMenu','waitMsec 700',*accept('nav_campaign'),
        'waitMsec 700',shot('campaigns'),*accept('nav_newgame'),'waitMsec 130',shot('detail-enter'),
        'waitMsec 700',shot('skill'),*accept('nav_skill4'),*accept('nav_turbo'),'waitMsec 400',shot('skill-warlord'),
        *accept('nav_back'),'waitMsec 150',shot('detail-leave'),'waitMsec 500',shot('campaigns-returned'),
        *accept('nav_newgame'),'waitMsec 500',shot('skill-reopened'),
        'set ui_retainedReducedMotion 1',*accept('nav_back'),'waitMsec 300',*accept('nav_newgame'),'waitMsec 300',shot('skill-reduced'),
    ])
    # Leave the original map scripts, cinematic camera and treatment timing in
    # charge. Captures span all three blinks and the bed exit.
    capture.SCENARIO_CVARS['recovery']={'g_autoSkipCinematics':'0'}
    capture.SCENARIO_CVARS['death']={'ui_retainedCaptureLoadFade':'1'}
    recovery=[]
    for i in range(15):
        recovery += [shot(f'recovery-{i:02}'),'waitMsec 3000']
    capture.SCENARIOS['recovery']=('game/mcc_2',recovery)
    capture.SCENARIOS['death']=('game/airdefense1',[
        'notarget',shot('alive'),'kill','waitMsec 150',shot('death-shock'),
        'waitMsec 350',shot('death-focus'),'waitMsec 700',shot('death-tunnel'),'waitMsec 1400',shot('death-settled'),
        'set ui_retained 0','waitMsec 150',shot('death-classic'),'set ui_retained 1','waitMsec 600',shot('death-restored')])
    capture.SCENARIO_CVARS['death-reduced']={'ui_retainedCaptureLoadFade':'1','ui_retainedReducedMotion':'1','ui_reducedFlashing':'1'}
    capture.SCENARIOS['death-reduced']=capture.SCENARIOS['death']
    capture.SCENARIOS['death-mp']=('mp:mp/q4dm1',[
        shot('alive'),'kill 0','waitMsec 350',shot('death-focus'),'waitMsec 2200',shot('death-settled')])
    # Run the real opening, including the dropship cinematic. The wake-up
    # camera begins after it; installed model frame commands own every fade.
    crash=['waitMsec 138000']
    for i in range(39):
        crash += [shot(f'crash-{i:02}'),'waitMsec 1500']
    capture.SCENARIOS['crash']=('game/airdefense1',crash)
    capture.SCENARIOS['crash-reduced']=capture.SCENARIOS['crash']
    capture.SCENARIO_CVARS['crash']={'g_autoSkipCinematics':'0','com_gameMode':'SP','si_gameType':'singleplayer'}
    capture.SCENARIO_CVARS['crash-reduced']={**capture.SCENARIO_CVARS['crash'],'ui_retainedReducedMotion':'1','ui_reducedFlashing':'1'}
    capture.SCENARIO_CVARS['progression']={'ui_retainedCaptureLoadFade':'1'}
    capture.SCENARIOS['progression']=(None,[
        'waitMsec 2000','campaignMenu','waitMsec 500',*accept('campaign'),'waitMsec 600',*accept('newgame'),
        'waitMsec 600',*accept('skill2'),*accept('turbo'),shot('before-start'),*accept('start'),
        'waitMsec 2000','g_skill','g_turboMode',shot('campaign-started')])
    results=[]
    for name in args.scenario or ('menus','recovery','death'):
        # Full dropship playback plus the crash and its gameplay handoff can
        # exceed four minutes while creating render-target captures.
        args.timeout=300 if name.startswith('crash') else 240
        result=capture.run(args,name);results.append(result)
        (args.output/'report.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
        assert result['log'], f'{name}: client did not produce a log; check process.log and whether another client is running'
        log=(ROOT/result['log']).read_text(encoding='utf-8',errors='replace')
        assert 'frame geometry ring overflow' not in log, 'Vulkan geometry budget exceeded'
        if name=='menus':
            assert 'RETAINED_INITIALIZATION' in log and 'RETAINED_DETAIL handover level=4 gui=guis/menu/campaign_skill.q4ui' in log
            assert 'command=campaignSkill4 accepted=1' in log and 'command=campaignSkillTurbo accepted=1' in log
        if name=='recovery':
            assert log.count('RETAINED_RECOVERY until=')==3 and 'inBed=0' in log
        if name.startswith('death'):
            assert 'RETAINED_GUI_LOADED guis/hud/view_effects.q4ui' in log, 'death must actually enter its scene effect'
        if name.startswith('crash'):
            assert log.count('RETAINED_CRASH wake ')==3 and log.count('RETAINED_CRASH close ')==2, 'three authored wakes and two blackouts must drive the effect'
        if name in ('death','death-reduced','progression'):
            from PIL import Image,ImageStat
            black=args.output/name/'save/baseoq4/screenshots/loading-fade-black.png'
            assert black.is_file() and max(high for low,high in ImageStat.Stat(Image.open(black)).extrema)==0, 'loading handover must be fully black'
            if name.startswith('death'): assert 'RETAINED_GUI_LOADED guis/hud/view_effects.q4ui' in log
            else: assert 'command=campaignSkillStart accepted=1' in log and 'Entering doom_main()' in log
        print(f"{name}: complete={result['complete']} shots={len(result['screenshots'])} errors={result['errors']} warnings={result['retained_warnings']}",flush=True)
    (args.output/'report.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
    assert all(r['complete'] and r['returncode']==0 and not r['errors'] and not r['retained_warnings'] for r in results)

if __name__=='__main__':main()
