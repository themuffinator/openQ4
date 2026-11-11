#!/usr/bin/env python3
"""Capture retained optics over a real SP map without host input.

Use --assets, --output under .tmp, --renderer gl|vulkan and --size WxH.
The standard stable .install runtime is windowed and hidden by the shared
capture harness; all images come from the engine screenshot command.
"""
import argparse
import json
from pathlib import Path
import capture_retained_screens as capture

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,default=ROOT/'.install')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--renderer',choices=('gl','vulkan'),default='gl')
    parser.add_argument('--size',default='1280x720')
    args=parser.parse_args()
    args.output=args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT/'.tmp').resolve()):
        parser.error('choose a new output directory under the project .tmp')
    args.gate='1';args.language='english';args.timeout=240
    stages=['god','notarget','set ui_testGuiInGame 1','give weapon_machinegun','waitMsec 1200',
            'testWeaponScope weapon_machinegun','waitMsec 1500','testWeaponScope',
            'set ui_retained 0','waitMsec 300','testWeaponScope','set ui_retained 1','waitMsec 300','testWeaponScope']
    for kind in ('machinegun','railgun','nailgun'):
        stages += [f'testGUI guis/weapons/{kind}_scope.rui','waitMsec 1200',
                   'openq4_retainedGui state playerYaw 37',f'screenshot "screenshots/{kind}.tga"']
        if kind=='railgun':
            stages += ['openq4_retainedGui state player_ammo 1','waitMsec 100','screenshot "screenshots/railgun-one.tga"']
        if kind=='nailgun':
            stages += ['openq4_retainedGui pending lockStatus 1','openq4_retainedGui pending lockYaw 70',
                       'openq4_retainedGui pending lockRange 12.4','openq4_retainedGui state lockTime 2.0',
                       'openq4_retainedGui event lockAquired','waitMsec 500','screenshot "screenshots/nailgun-locked.tga"']
    capture.SCENARIOS['scopes']=('game/airdefense1',stages)
    result=capture.run(args,'scopes')
    log=(ROOT/result['log']).read_text(encoding='utf-8',errors='replace')
    assert result['complete'] and result['returncode']==0 and not result['errors'],result
    assert 'frame geometry ring overflow' not in log, 'Vulkan geometry budget exceeded'
    assert not any('retained UI:' in message for message in result['retained_warnings']),result['retained_warnings']
    for kind in ('machinegun','railgun','nailgun'):
        assert f'guis/weapons/{kind}_scope.rui' in result['retained_loaded'],kind
    assert 'WEAPON_SCOPE gui=guis/weapons/machinegun_scope.gui' in log and 'WEAPON_SCOPE gui=guis/weapons/machinegun_scope.rui' in log
    (args.output/'report.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(f"scopes: {args.renderer} {args.size}, five engine captures, live gate fallback, no retained errors")

if __name__=='__main__':
    main()
