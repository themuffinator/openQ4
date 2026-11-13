#!/usr/bin/env python3
"""Opt-in, input-free campaign discovery, switching and engine capture checks.

Installed retail assets and expansion content are read through the engine.
Writable profiles and synthetic discovery fixtures stay under project .tmp.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import time
import zipfile
from pathlib import Path

from campaign_runtime import AWAKENING_MAPS, ROOT, task_path

REQUIRED = ('def/q4xdamage.def','def/weapons/spikegun.def','scripts/main.script','scripts/events.script') + tuple(
    f'maps/game/{name}.{extension}' for name in AWAKENING_MAPS for extension in ('map','proc','cm'))
CASES = ('absent','metadata-only','partial','ready-loose','ready-pk4','empty-loose-shadow','empty-patch-shadow',
         'absent-retained','partial-retained',
         'real-ready-legacy','real-ready-retained','real-singleplayer-retained',
         'switch-sp','switch-mp','switch-arena','direct-mp','dedicated',
         'mod-explicit-base','mod-switch-awakening','mod-manifest-awakening','mod-manifest-quake4','mod-mp')
DONE = 'CAMPAIGN_SELECTION_COMPLETE'
# A player's mod over The Awakening (fs_game awkmod, fs_game_base q4xbase).
MOD = 'awkmod'
MOD_AWAKENING_CASES = ('mod-explicit-base','mod-switch-awakening','mod-manifest-awakening')
MOD_CASES = MOD_AWAKENING_CASES + ('mod-manifest-quake4','mod-mp')


def mod_manifest(save: Path, campaign: str | None) -> None:
    """The test mod's mod.json, optionally tying it to one campaign."""
    manifest = dict(name='Awakening mod test', version='1.0', releaseDate='2026-11-13',
                    website='https://example.invalid', author='openQ4 tests', requiredopenQ4Version='0.14.0')
    if campaign:
        manifest['campaign'] = campaign
    (save / MOD).mkdir(parents=True, exist_ok=True)
    (save / MOD / 'mod.json').write_text(json.dumps(manifest, indent=2) + '\n')


def search_order(paths: str, *dirs: str) -> bool:
    """Whether `path` lists each game directory, in the given order (top first).

    A directory entry is a line ending in /<name>; a pack is a line under it."""
    lowered = paths.replace('\\', '/').lower()
    found = [match.start() if (match := re.search(rf'/{name}[ \t]*$', lowered, re.M)) else -1 for name in dirs]
    return all(index >= 0 for index in found) and found == sorted(found)


def fixture(root: Path, case: str) -> tuple[Path,int,int]:
    case = case.removesuffix('-retained')
    game = root / 'q4xbase'
    game.mkdir(parents=True)
    if case == 'metadata-only':
        (game / 'mod.json').write_text('{"displayName":"obsolete layer"}')
        (game / 'game-sp_x64.dll').write_bytes(b'OBSOLETE MODULE MUST NEVER LOAD')
    elif case == 'partial':
        path = game / REQUIRED[0]
        path.parent.mkdir(parents=True)
        path.write_text('partial content')
    elif case == 'ready-loose':
        for name in REQUIRED:
            path = game / name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('discovery fixture; never mounted')
    elif case in ('ready-pk4','empty-loose-shadow','empty-patch-shadow'):
        with zipfile.ZipFile(game / 'a.pk4','w') as archive:
            for name in REQUIRED:
                archive.writestr(name,'discovery fixture; never mounted')
        if case == 'empty-loose-shadow':
            path = game / REQUIRED[0]
            path.parent.mkdir(parents=True)
            path.touch()
        if case == 'empty-patch-shadow':
            with zipfile.ZipFile(game / 'z.pk4','w') as archive:
                archive.writestr(REQUIRED[0],b'')
    present = case not in ('absent','metadata-only')
    ready = case in ('ready-loose','ready-pk4')
    return root,int(present),int(ready)


def run(args, case: str) -> dict:
    output = task_path(args.output / case)
    output.mkdir(parents=True)
    save = output / 'save'
    for name in ('baseoq4','q4xbase'):
        (save / name).mkdir(parents=True)
    real = case.startswith('real-') or case in ('switch-sp','switch-mp','switch-arena','direct-mp','dedicated') + MOD_CASES
    if real:
        content,present,ready = args.awakening.resolve(strict=True),1,1
    else:
        content,present,ready = fixture(output / 'content',case)
    width,height = args.size.split('x')
    settings = {
        'fs_basepath': str(args.retail.resolve(strict=True)), 'fs_savepath': str(save), 'fs_devpath': str(save),
        'fs_cachepath': str(output / 'cache'), 'fs_awakeningpath': str(content), 'fs_game': 'baseoq4',
        'si_gameType': 'singleplayer', 'ui_autoJoin': '1', 's_noSound': '1',
        'win_allowMultipleInstances' if os.name == 'nt' else 'sys_allowMultipleInstances': '1',
        'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
        'r_hiddenWindow': '1', 'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0',
        'r_borderlessDefaultMigrated': '1', 'r_mode': '-1', 'r_customWidth': width, 'r_customHeight': height,
        'r_windowWidth': width, 'r_windowHeight': height, 'r_multiSamples': '0', 'r_renderApi': args.renderer,
        'com_skipLogoVideos': '1', 'com_skipLoadingContinue': '1', 'g_autoSkipCinematics': '1',
        'logFile': '2', 'logFileName': 'logs/selection.log', 'developer': '1',
        'sys_lang': args.language,
        'ui_retained': '1' if case.endswith('retained') else '0', 'ui_retainedTrace': '1',
        'net_LANServer': '1', 'net_port': '29052' if case == 'dedicated' else '29051',
    }
    paths = ['echo CAMPAIGN_PATH_BEGIN','path','echo CAMPAIGN_PATH_END']
    finish = ['waitMsec 3000','fs_game','si_gameType','com_activeGameModule',*paths,f'echo {DONE}','quit']
    launch = ['+exec','selection.cfg']
    if case in ('switch-sp','switch-mp','switch-arena'):
        settings.update(g_autoExecAfterMapLoad='selection.cfg',g_autoExecAfterMapLoadDelayMs='500')
        (save / 'baseoq4/selection.cfg').write_text('\n'.join(
            ['openq4_assertMapState game/airdefense1'] + finish if case == 'switch-sp' else
            ['waitMsec 3000','screenshot screenshots/arena.tga'] + finish if case == 'switch-arena' else
            ['waitMsec 3000','openq4_assertMPClientActive','openq4_reportMPState'] + finish) + '\n')
        next_action = (['set fs_game baseoq4','campaignSelect quake4 start'] if case == 'switch-sp' else
                       ['set fs_game baseoq4','campaignSelect arena'] if case == 'switch-arena' else
                       ['set fs_game baseoq4','set si_gameType CTF','spawnServer mp/q4ctf1'])
        if case == 'switch-mp':
            (save / 'baseoq4/Quake4Config.cfg').write_text('set si_gameType DM\n')
        if case == 'switch-arena':
            settings['fs_game'] = 'q4xbase'
            (save / 'q4xbase/autoexec.cfg').write_text('// Defer the base validation until the campaign mount changes.\n')
            (save / 'baseoq4/autoexec.cfg').write_text('waitMsec 1000\nexec selection.cfg\n')
        (save / 'q4xbase/selection.cfg').write_text('\n'.join(
            ['waitMsec 3000','openq4_assertMapState game/m01_stranarus_trench1',
             'echo AWAKENING_GAMEPLAY_BEFORE_SWITCH','set g_autoExecAfterMapLoad selection.cfg',*next_action]) + '\n')
        launch = ['+campaignSelect','awakening','start']
    elif case in MOD_AWAKENING_CASES:
        # The campaign starts with the mod kept on top of the expansion: an
        # explicit base, a switch from Quake 4, or the mod's own mod.json.
        mod_manifest(save, 'awakening' if case == 'mod-manifest-awakening' else None)
        settings.update(fs_game=MOD, fs_game_base='q4xbase' if case == 'mod-explicit-base' else '',
                        g_autoExecAfterMapLoad='selection.cfg', g_autoExecAfterMapLoadDelayMs='500')
        (save / MOD / 'selection.cfg').write_text('\n'.join(
            ['waitMsec 3000','openq4_assertMapState game/m01_stranarus_trench1','fs_game_base','fs_awakeningActive',
             'echo AWAKENING_MOD_GAMEPLAY'] + finish) + '\n')
        launch = ['+campaignSelect','awakening','start']
    elif case == 'mod-manifest-quake4':
        # A mod tied to Quake 4 refuses the expansion even when asked for it.
        mod_manifest(save, 'quake4')
        settings.update(fs_game=MOD, fs_game_base='q4xbase')
        (save / MOD / 'selection.cfg').write_text('\n'.join(
            ['waitMsec 500','fs_game_base','fs_awakeningActive'] + finish) + '\n')
    elif case == 'mod-mp':
        # Multiplayer drops the expansion from beneath the mod and keeps the mod.
        mod_manifest(save, None)
        settings.update(fs_game=MOD, fs_game_base='q4xbase', si_gameType='CTF', com_gameMode='MP',
                        g_autoExecAfterMapLoad='selection.cfg', g_autoExecAfterMapLoadDelayMs='500')
        (save / MOD / 'selection.cfg').write_text('\n'.join(
            ['waitMsec 3000','openq4_reportMPState','openq4_assertMPClientActive','fs_game_base','fs_awakeningActive'] +
            finish) + '\n')
        launch = ['+spawnServer','mp/q4ctf1']
    elif case in ('direct-mp','dedicated'):
        settings.update(fs_game='q4xbase',si_gameType='CTF',com_gameMode='MP',
                        g_autoExecAfterMapLoad='selection.cfg',g_autoExecAfterMapLoadDelayMs='500')
        stages = ['waitMsec 3000','openq4_reportMPState']
        if case == 'direct-mp': stages.append('openq4_assertMPClientActive')
        (save / 'baseoq4/selection.cfg').write_text('\n'.join(stages + finish) + '\n')
        launch = ['+spawnServer','mp/q4ctf1']
        if case == 'dedicated':
            settings.update(net_serverDedicated='1', com_skipRenderer='1')
            launch += ['+exec','selection.cfg']
    else:
        stages = ['waitMsec 500','campaignList',*paths,
                  'campaignMenu' if case == 'real-singleplayer-retained' else 'campaignMenu campaigns',
                  'waitMsec 1200','ui_retainedStatus','screenshot screenshots/campaign.tga',f'echo {DONE}','quit']
        (save / 'baseoq4/selection.cfg').write_text('\n'.join(stages) + '\n')
    stem = 'openQ4-ded_x64' if case == 'dedicated' else 'openQ4-client_x64'
    exe = args.runtime.resolve() / (stem + ('.exe' if os.name == 'nt' else ''))
    command = [str(exe)]
    for key,value in settings.items(): command += ['+set',key,value]
    command += launch
    options = {}
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options.update(startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
    started = time.monotonic()
    with (output / 'process.log').open('w') as stream:
        process = subprocess.Popen(command,cwd=args.runtime.resolve(),stdin=subprocess.DEVNULL,
                                   stdout=stream,stderr=subprocess.STDOUT,**options)
        try: code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            code = 'timeout'
    logs = {name:(save / name / 'logs/selection.log').read_text(encoding='latin1')
            for name in ('baseoq4','q4xbase',MOD) if (save / name / 'logs/selection.log').exists()}
    log = '\n'.join(logs.values())
    errors = re.findall(r'(?mi)^(?:\^\d)?(?:ERROR:|FATAL ERROR:|ASSERTION FAILED).*',log)
    readiness = re.findall(r'CAMPAIGN awakening present=(\d) available=(\d)',log)
    mounted = re.findall(r'CAMPAIGN_PATH_BEGIN\s*(.*?)CAMPAIGN_PATH_END',log,re.S)
    isolated = bool(mounted) and all('q4xbase' not in value.lower() for value in mounted)
    ready_ok = case in ('switch-sp','switch-mp','switch-arena','direct-mp','dedicated') + MOD_CASES or readiness == [(str(present),str(ready))]
    if case in MOD_AWAKENING_CASES:
        # The mod above the expansion, the expansion above openQ4 and retail.
        isolated = bool(mounted) and all(search_order(value, MOD, 'q4xbase', 'baseoq4', 'q4base') for value in mounted)
    passed = code == 0 and DONE in log and not errors and isolated and ready_ok
    retained_warnings = [line for line in log.splitlines() if 'WARNING' in line and 'retained' in line.lower()]
    if case.endswith('retained'):
        expected_gui = 'singleplayer' if case == 'real-singleplayer-retained' else 'campaigns'
        passed = passed and not retained_warnings and f'RETAINED_GUI_LOADED guis/menu/{expected_gui}.rui' in log
    if case in ('switch-sp','switch-mp','switch-arena'):
        passed = passed and 'AWAKENING_GAMEPLAY_BEFORE_SWITCH' in logs.get('q4xbase','')
    if case in ('switch-mp','direct-mp','dedicated'):
        passed = passed and '"si_gameType" is:"CTF"' in log and '"com_activeGameModule" is:"game_mp"' in log
    if case == 'switch-arena':
        passed = passed and 'CAMPAIGN_SELECTED id=arena gameDir=baseoq4 module=game_sp' in log
    if case in MOD_AWAKENING_CASES:
        modlog = logs.get(MOD,'')
        passed = (passed and 'AWAKENING_MOD_GAMEPLAY' in modlog and f'"fs_game" is:"{MOD}"' in modlog and
                  '"fs_game_base" is:"q4xbase"' in modlog and '"fs_awakeningActive" is:"1"' in modlog and
                  f"The Awakening mounted: fs_game '{MOD}', fs_game_base 'q4xbase'" in modlog and
                  f'CAMPAIGN_SELECTED id=awakening gameDir={MOD} module=game_sp' in modlog and
                  "spawning 'rvMonsterTurret' as 'riMonsterTurret'" in modlog)
        if case == 'mod-manifest-awakening':
            passed = passed and f"Mod '{MOD}' plays over The Awakening (mod.json campaign)" in modlog
    if case in ('mod-manifest-quake4','mod-mp'):
        modlog = logs.get(MOD,'')
        passed = (passed and f'"fs_game" is:"{MOD}"' in modlog and '"fs_game_base" is:""' in modlog and
                  '"fs_awakeningActive" is:"0"' in modlog and 'The Awakening mounted' not in modlog)
        if case == 'mod-manifest-quake4':
            passed = passed and f"Mod '{MOD}' plays over Quake 4 only (mod.json campaign)" in modlog
        else:
            passed = passed and '"com_activeGameModule" is:"game_mp"' in modlog
    shots = []
    for tga in save.rglob('screenshots/*.tga'):
        from PIL import Image
        png = tga.with_suffix('.png')
        Image.open(tga).convert('RGB').save(png)
        shots.append(str(png))
    result = dict(case=case,passed=bool(passed),exit=code,seconds=round(time.monotonic()-started,2),
                  errors=errors,readiness=readiness,base_content_isolated=isolated,
                  retained_warnings=retained_warnings,
                  selected_modules=re.findall(r'Selected game module: .*',log),screenshots=shots,
                  log_files=[str(save / name / 'logs/selection.log') for name in logs])
    print(f"{case}: {'PASS' if passed else 'FAIL'} ({result['seconds']}s) {errors[:1]}",flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail',type=Path,required=True)
    parser.add_argument('--awakening',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,default=ROOT / '.install')
    parser.add_argument('--output',type=Path,default=ROOT / '.tmp/game-consolidation/selection')
    parser.add_argument('--renderer',choices=('gl','vulkan'),default='gl')
    parser.add_argument('--size',default='1280x720')
    parser.add_argument('--language',choices=('english','german','russian','brazilian','czech','hungarian','turkish','ukrainian'),default='english')
    parser.add_argument('--cases',nargs='+',choices=CASES,default=CASES)
    parser.add_argument('--timeout',type=int,default=300)
    args = parser.parse_args()
    args.output = task_path(args.output)
    if args.output.exists(): parser.error('use a new project-local output directory')
    if not re.fullmatch(r'\d{3,4}x\d{3,4}',args.size): parser.error('size must be WIDTHxHEIGHT')
    args.output.mkdir(parents=True)
    report = dict(complete=False,renderer=args.renderer,results=[],
                  executable_sha256={path.name:hashlib.sha256(path.read_bytes()).hexdigest()
                      for path in args.runtime.glob('openQ4-*') if path.suffix.lower() in ('','.exe')})
    report_path = args.output / 'selection.json'
    for case in args.cases:
        report['results'].append(run(args,case))
        report_path.write_text(json.dumps(report,indent=2)+'\n')
    report['complete'] = True
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    return 0 if all(case['passed'] for case in report['results']) else 1


if __name__ == '__main__':
    raise SystemExit(main())
