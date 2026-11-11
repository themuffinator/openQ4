#!/usr/bin/env python3
"""Windowed SP hardware selection and SYSTEM-menu smoke, without device input.

Uses the stable staged executable, isolated writable profiles, engine semantic
menu actions and the engine screenshot command. This is functional validation,
not performance qualification. A menu GPU choice exercises Apply, engine reload
and subsequent real map gameplay, including superseding a launch backend pin.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

import renderer_gameplay_benchmark as benchmark
from mp_round_remote_smoke import verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]


def menu(action):
    return [f'openq4_retainedGui menu {action} 1', f'openq4_retainedGui menu {action} 0', 'wait 3']


def observe_menu():
    return ['testGUI guis/menu/settings/system.rui', 'waitMsec 2000', 'openq4_retainedGui report',
            *[f'openq4_guiGet "{node}::text"' for node in
              ('settings_backend-value', 'settings_gpu-value', 'rendering-active', 'rendering-notice')],
            'openq4_retainedGui widget settings_gpu']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--assets', type=Path, help='Optional installed Quake 4 asset root; otherwise use engine install discovery.')
    parser.add_argument('--backend', choices=('gl', 'vulkan', 'best'))
    parser.add_argument('--device', help='Saved stable GPU UUID, or auto; never an enumeration index.')
    parser.add_argument('--saved', action='store_true', help='Store choices in config instead of launch arguments.')
    choice = parser.add_mutually_exclusive_group()
    choice.add_argument('--menu-gpu', type=int, help='Select this enabled GPU row through the real menu (0 = first), Apply and restart. -1 chooses Auto.')
    choice.add_argument('--menu-backend', choices=('best','gl','vulkan'), help='Choose a backend through the real menu, Apply and restart.')
    parser.add_argument('--expected-backend', choices=('gl', 'vulkan'), required=True)
    parser.add_argument('--expected-device')
    parser.add_argument('--expected-auto')
    args = parser.parse_args()
    changing = args.menu_gpu is not None or args.menu_backend is not None
    if args.device and not re.fullmatch(r'auto|[0-9a-f]{32}', args.device): parser.error('Invalid device ID')
    if args.menu_gpu is not None and not -1 <= args.menu_gpu < 32: parser.error('GPU slot out of range')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    save = output / 'save'
    game = save / 'baseoq4'
    game.mkdir(parents=True, exist_ok=True)
    if (game / 'openQ4Config.cfg').exists(): raise RuntimeError('Use a fresh isolated profile')
    runtime = ROOT / '.install'
    verify_runtime_is_current(runtime)
    paths = [runtime / n for n in ('openQ4-client_x64.exe', 'renderer-gl_x64.dll', 'renderer-vk_x64.dll', 'baseoq4/game-sp_x64.dll')]
    hashes = {p.relative_to(runtime).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    launch = []
    settings = {'fs_savepath': str(save), 'fs_devpath': str(save), 'fs_game': 'baseoq4',
                'com_gameMode': 'SP', 'si_gameType': 'singleplayer', 'ui_autoJoin': '0',
                'win_allowMultipleInstances': '1', 'logFile': '2', 'logFileName': 'logs/openq4.log',
                'developer': '1', 'r_hiddenWindow': '1', 'r_fullscreen': '0', 'r_fullscreenDesktop': '0',
                'r_borderless': '0', 'r_borderlessDefaultMigrated': '1', 'r_mode': '-1',
                'r_windowWidth': '1280', 'r_windowHeight': '720', 'r_swapInterval': '0',
                'in_mouse': '0', 'in_joystick': '0', 'ui_retained': '1', 'r_vkValidation': '1',
                'com_skipLoadingContinue': '1', 'com_loadingContinueAutoAdvance': '1',
                'g_autoSkipCinematics': '1', 'g_autoExecAfterMapLoad': 'aftermap.cfg',
                'g_autoExecAfterMapLoadDelayMs': '1000'}
    if args.assets: settings['fs_basepath'] = str(args.assets.resolve())
    preferences = {key: value for key, value in [('r_renderApi', args.backend), ('r_renderDevice', args.device)] if value}
    if args.saved:
        write_script(game / 'openQ4Config.cfg', [f'seta {key} "{value}"' for key, value in preferences.items()])
    else: settings.update(preferences)
    for key, value in settings.items(): benchmark.append_set(launch, key, value)
    benchmark.append_command(launch, 'exec', 'boot.cfg')
    assert sum(word.startswith('+') for word in launch) <= 64
    after = ['echo RENDER_SELECTION_GAMEPLAY', 'god', 'notarget', 'waitMsec 3000', 'viewpos', 'gfxInfo',
             'rendererDevices', 'r_renderApi', 'r_renderDevice', 'r_bootRenderDevice', 'r_actualRenderApi', 'r_fullscreen',
             'rendererModuleSelfTest', 'performancePresetSelfTest', 'autoDetectPerformancePreset',
             'vid_restart', 'waitMsec 2000', 'rendererDevices', 'screenshot screenshots/gameplay.tga',
             *observe_menu(), 'screenshot screenshots/system.tga', 'wait 3',
             'openq4_retainedGui focus settings_gpu', *menu('accept'),
             'screenshot "screenshots/device-list.tga"', *menu('back'), 'testGUI',
             'echo RENDER_SELECTION_COMPLETE', 'quit']
    write_script(game / 'aftermap.cfg', after)
    boot = ['waitMsec 2000', 'rendererDevices']
    if not changing:
        boot += ['map game/airdefense1']
    else:
        # The phase is archived by the checked restart; config execution after
        # reload runs it once. No filesystem race or native input is involved.
        write_script(game / 'autoexec.cfg', ['vstr openq4_renderSelectionReload'])
        write_script(game / 'reload.cfg', ['seta openq4_renderSelectionReload ""', 'waitMsec 3000', 'map game/airdefense1'])
        control = 'settings_backend' if args.menu_backend else 'settings_gpu'
        steps = ('best','gl','vulkan').index(args.menu_backend) if args.menu_backend else args.menu_gpu+1
        boot += observe_menu() + [f'openq4_retainedGui focus {control}'] + menu('accept') + menu('home')
        for _ in range(steps): boot += menu('down')
        boot += menu('accept') + ['waitMsec 500', 'openq4_guiGet "rendering-notice::text"',
                                 'openq4_retainedGui event apply', 'waitMsec 1500',
                                 'r_renderApi', 'r_renderDevice', 'r_actualRenderApi',
                                 'openq4_guiGet "rendering-notice::text"', 'screenshot screenshots/pending.tga',
                                 'seta openq4_renderSelectionReload "exec reload.cfg"',
                                 'echo RENDER_SELECTION_RESTART', 'openq4_retainedGui event restartRenderer']
    write_script(game / 'boot.cfg', boot)
    (output / 'launch.json').write_text(json.dumps(launch, indent=2), encoding='utf-8')
    (output / 'runtime.json').write_text(json.dumps(hashes, indent=2), encoding='utf-8')
    code, timeout, elapsed = benchmark.launch_and_wait(paths[0], launch, runtime,
        output / 'stdout.txt', output / 'stderr.txt', 240)
    log = (game / 'logs/openq4.log').read_text(encoding='utf-8', errors='replace')
    failures = []
    if code or timeout: failures.append(f'exit={code}, timeout={timeout}')
    if 'RENDER_SELECTION_COMPLETE' not in log: failures.append('missing completion')
    if not re.search(r'^origin: ', log, re.M): failures.append('map gameplay not observed')
    section = log.rsplit('RENDER_SELECTION_GAMEPLAY', 1)[-1]
    for key, expected in [('r_actualRenderApi', args.expected_backend), ('r_fullscreen', '0')]:
        if re.findall(rf'^"{key}" is:"([^"\n]+)"', section, re.M) != [expected]: failures.append('incorrect '+key)
    if args.expected_device and f'active={args.expected_device}' not in section: failures.append('wrong active GPU UUID')
    if args.expected_auto and f"Auto-detected performance preset '{args.expected_auto}'" not in section: failures.append('wrong detected preset')
    if changing and 'ReloadEngine end' not in log: failures.append('menu restart did not finish')
    if re.search(r'VUID-|Validation Error|FATAL ERROR|self-test failed|usage: openq4|openq4_retainedGui: requires', log): failures.append('runtime, diagnostic or Vulkan validation error')
    for name, digest in hashes.items():
        if hashlib.sha256((runtime / name).read_bytes()).hexdigest() != digest: failures.append('runtime changed during test')
    from PIL import Image, ImageStat
    captures = {}
    for name in ['gameplay', 'system', 'device-list'] + (['pending'] if changing else []):
        path = game / f'screenshots/{name}.tga'
        if not path.exists(): failures.append('missing capture '+name);continue
        with Image.open(path) as source:
            pixels = source.convert('RGB')
            if pixels.size != (1280,720) or max(ImageStat.Stat(pixels).stddev) < 5: failures.append('invalid capture '+name)
            png = output / f'{name}.png'
            pixels.save(png)
            with Image.open(png) as check: assert check.convert('RGB').tobytes() == pixels.tobytes()
        path.unlink() # Retain the verified lossless PNG only.
        captures[name] = hashlib.sha256(png.read_bytes()).hexdigest()
    inventory = re.findall(r'^Rendering device (\d+): id=(\S+) usable=(\d) preferred=(\d) name=(.*)$',log,re.M)
    report = {'passed': not failures, 'failures': failures, 'elapsed': elapsed, 'inventory': inventory,
              'captures': captures, 'scope': 'Local Windows windowed SP gameplay and engine-only menu operations; not an FPS or other-platform qualification.'}
    (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)
    return bool(failures)


if __name__ == '__main__': sys.exit(main())
