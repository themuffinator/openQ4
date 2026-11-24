#!/usr/bin/env python3
"""Qualify native wheels in gameplay using semantic commands, never host input."""
import argparse
import json
import re
from pathlib import Path
import capture_retained_screens as capture

ROOT=Path(__file__).resolve().parents[2]

def shot(name): return 'screenshot "screenshots/'+name+'.tga"'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,default=ROOT/'.install')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--renderer',choices=('gl','vulkan'),default='gl')
    parser.add_argument('--size',default='1280x720')
    parser.add_argument('--scenario',action='append',choices=('marine','strogg','mp'))
    args=parser.parse_args();args.output=args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT/'.tmp').resolve()): parser.error('use a fresh output below .tmp')
    args.language='english';args.gate='1';args.timeout=180
    results=[]
    for name,level in [('marine','game/airdefense1'),('strogg','game/recomp'),('mp','mp:mp/q4dm1')]:
        if args.scenario and name not in args.scenario: continue
        capture.SCENARIOS[name]=(level,[
            'notarget','give all','waitMsec 1500','testWeaponWheel open','waitMsec 600',
            'testWeaponWheel probe',shot('default'),
            'set ui_retainedOpaqueBacking 1','waitMsec 300',shot('focus-disabled'),
            'set ui_retainedOpaqueBacking 0','waitMsec 300',shot('focus-restored'),
            # The mouse's selector dot, held on the rim past it, and a keyed
            # step (next weapon) that turns it to the new choice.
            'testWeaponWheel cursor 60 60','waitMsec 300',shot('mouse-dot'),'testWeaponWheel probe',
            'testWeaponWheel cursor 400 0','waitMsec 300',shot('mouse-rim'),'testWeaponWheel probe',
            'testWeaponWheel step 14','waitMsec 300',shot('keyed-next'),'testWeaponWheel probe',
            'testWeaponWheel cursor 0 0',
            'testWeaponWheel slot 2','waitMsec 350',shot('selected-shotgun'),
            'testWeaponWheel release','waitMsec 1500','testWeaponWheel probe',shot('equipped'),
            'testWeaponWheel open','testWeaponWheel slot 1','waitMsec 200','testWeaponWheel cancel',
            'waitMsec 500','testWeaponWheel probe',
            'testWeaponWheel empty 1','testWeaponWheel open','testWeaponWheel slot 1','waitMsec 250',shot('empty-refusal'),
            'testWeaponWheel release','waitMsec 600','testWeaponWheel probe',
            'set ui_weaponWheelOffset 0','testWeaponWheel open','waitMsec 500',shot('centered'),
            'set ui_weaponWheelOffset -27','waitMsec 300',shot('left'),
            'set ui_retainedReducedMotion 1','testWeaponWheel cancel','testWeaponWheel open','waitMsec 100',shot('reduced'),
            'set ui_retainedReducedMotion 0','set ui_weaponWheelOffset 27','set ui_retained 0','waitMsec 350',shot('classic'),
            'set ui_retained 1','waitMsec 500',shot('restored'),'testWeaponWheel cancel',
            'waitMsec 600','testWeaponWheel probe','set ui_weaponWheelOffset -17','writeConfig "wheel-settings.cfg"','set ui_weaponWheelOffset 0',
            'exec "wheel-settings.cfg"','waitMsec 100','testWeaponWheel probe',
        ])
        result=capture.run(args,name);results.append(result)
        (args.output/'report.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
        log=(ROOT/result['log']).read_text(encoding='utf-8',errors='replace')
        traces=re.findall(r'WEAPON_WHEEL active=(\d+) slot=(-?\d+) slots=(\d+) current=(-?\d+) ideal=(-?\d+) retained=(\d+) offset=(-?\d+) timescale=([.\d]+) gui=(\S+)',log)
        result['wheel_traces']=traces
        assert result['complete'] and result['returncode']==0, result
        assert not result['errors'] and not result['retained_warnings'], (result['errors'],result['retained_warnings'])
        assert any(t[0]=='1' and t[2]=='11' and 'weapon_wheel_' in t[8] for t in traces), 'native wheel must draw in real gameplay'
        assert any(t[0]=='0' and t[3]=='2' and t[4]=='2' for t in traces), 'release equips the selected shotgun'
        # The rim point (118, 0) is at 90 degrees, the hyperblaster's slot;
        # next weapon then steps clockwise to the grenade launcher.
        rim=[i for i,t in enumerate(traces) if t[0]=='1' and t[1]=='3']
        assert rim and any(t[0]=='1' and t[1]=='4' for t in traces[rim[0]+1:]), 'the bounded selector and a keyed step choose on the wheel'
        assert traces[-1][0]=='0' and traces[-1][3]=='2' and traces[-1][7]=='1.000', 'cancel clears wheel and transient time scale'
        if name=='mp': assert all(t[7]=='1.000' for t in traces), 'MP simulation never slows'
        config=(args.output/name/'save/baseoq4/wheel-settings.cfg').read_text(encoding='utf-8',errors='replace')
        assert re.search(r'ui_weaponWheelOffset\s+"-17"',config), 'placement must be archived'
        assert traces[-1][6]=='-17', 'config reload restores the saved nondefault offset'
        assert 'geometry ring overflow' not in log
        print(name, 'passed',len(result['screenshots']),'engine screenshots',flush=True)
        (args.output/'report.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
