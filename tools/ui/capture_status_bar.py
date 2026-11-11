#!/usr/bin/env python3
"""Exercise the live status bar in windowed gameplay without host input."""
import argparse
import json
import re
from pathlib import Path
import capture_retained_screens as capture

ROOT=Path(__file__).resolve().parents[2]
def shot(name): return 'screenshot "screenshots/'+name+'.tga"'
def equip(slot): return ['testWeaponWheel open', 'testWeaponWheel slot '+str(slot), 'testWeaponWheel release', 'waitMsec 1400']

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,default=ROOT/'.install')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--renderer',choices=('gl','vulkan'),default='gl')
    parser.add_argument('--size',default='1280x720')
    parser.add_argument('--scenario',action='append',choices=('marine','strogg','mp','missing','invalid','custom'))
    args=parser.parse_args();args.output=args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT/'.tmp').resolve()): parser.error('use a fresh output below .tmp')
    args.language='english';args.gate='1';args.timeout=180
    results=[]
    for scenario in args.scenario or ['marine','strogg','mp']:
        level='game/recomp' if scenario=='strogg' else 'mp:mp/q4dm1' if scenario=='mp' else 'game/airdefense1'
        stages=['set g_autoExecAfterMapLoad ""','notarget','god','give all','waitMsec 1200']+equip(1)
        stages += ['testStatusBar stats 100 75','waitMsec 400','testStatusBar probe',shot('healthy'),
            'testStatusBar consume 7','waitMsec 250','testStatusBar probe',shot('ammo-used'),
            'testStatusBar stats 20 0','waitMsec 400','testStatusBar probe',shot('low-no-armor'),
            'testStatusBar stats 125 100','waitMsec 80',shot('gain'),'waitMsec 400',shot('recovered'),
            'set ui_hudScale 1.5','set ui_hudSafeInset 5','waitMsec 300',shot('scaled-inset'),
            'set ui_hudScale 1','set ui_hudSafeInset 0','set ui_retainedReducedMotion 1','waitMsec 300',shot('reduced'),
            'set ui_retained 0','waitMsec 400','testStatusBar probe',shot('stock'),
            'set ui_retained 1','waitMsec 600','testStatusBar probe',shot('restored')]
        if scenario in ('marine','strogg','mp'):
            stages += equip(0)+['testStatusBar probe',shot('unlimited')]+equip(6)+['testStatusBar probe',shot('clipless'),
                'set g_showHud 0','waitMsec 200',shot('hidden'),'set g_showHud 1','waitMsec 200',
                'vid_restart','waitMsec 1800','testStatusBar probe',shot('renderer-restarted')]
        if scenario=='marine': stages += ['saveGame "status-bar"','waitMsec 500',
            'testStatusBar stats 45 0','loadGame "status-bar"','waitMsec 3000','testStatusBar probe',shot('save-restored')]
        capture.SCENARIOS[scenario]=(level,stages)
        path='guis/hud/status_bar_marine.rui'
        if scenario=='missing': capture.OVERRIDES[scenario]={path:lambda runtime:''}
        if scenario=='invalid': capture.OVERRIDES[scenario]={path:lambda runtime:'{"format":"invalid"}'}
        if scenario=='custom':
            capture.OVERRIDES[scenario]={'guis/hud.gui':lambda runtime: (ROOT/'content/baseoq4/pak0/guis/hud.gui').read_text()}
        result=capture.run(args,scenario);results.append(result)
        log=(ROOT/result['log']).read_text(encoding='utf-8',errors='replace')
        traces=re.findall(r'STATUS_BAR gui=(\S+) health=(\d+) armor=(\d+) ammo=(-?\d+) clip=(-?\d+) maximum=(-?\d+) drawn=(\d+)',log)
        result['status_traces']=traces
        (args.output/'report.json').write_text(json.dumps(results,indent=2)+'\n')
        assert result['complete'] and result['returncode']==0,result
        assert not result['errors'],result['errors']
        if scenario!='invalid': assert not result['retained_warnings'],result['retained_warnings']
        assert traces and any(t[0]=='stock' for t in traces),'Stock UI must stay available'
        if scenario in ('missing','invalid','custom'):
            assert all(t[0]=='stock' and t[-1]=='0' for t in traces),'unavailable or mod-owned HUD must use stock gauges'
        else:
            assert any(t[0].endswith('status_bar_'+('strogg' if scenario=='strogg' else 'marine')+'.rui') and t[-1]=='1' for t in traces),'native status bar must draw during gameplay'
            assert any(t[1:3]==('20','0') and t[-1]=='1' for t in traces),'low health and zero armor bind during gameplay'
            assert traces[-1][-1]=='1','renderer restart must restore a drawn status bar'
            assert any(t[3]=='293' for t in traces),'consuming seven rounds must update live ammunition'
            if scenario=='marine': assert traces[-1][1:3]==('125','100'),'save/load must restore both status values'
        print(scenario,'passed',len(result['screenshots']),'engine screenshots',flush=True)

if __name__=='__main__': main()
