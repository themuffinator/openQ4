#!/usr/bin/env python3
"""Windowed SP gameplay checks for presets, device recreation and auto-detection.

Uses the stable staged Windows runtime and engine commands/captures only. No
native input is injected. Output defaults to .tmp/preset-audit/<backend>.
This is a functional smoke, not a frame-rate qualification or GPU benchmark.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

import renderer_gameplay_benchmark as benchmark
from mp_round_remote_smoke import verify_runtime_is_current, write_script

ROOT = Path(__file__).resolve().parents[2]
PROFILES = ('minimum', 'lowpower', 'low', 'performance', 'balanced', 'enhanced', 'quality', 'ultra', 'maximum')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--render-api', choices=('gl', 'vk'), required=True)
    parser.add_argument('--vk-device', type=int, default=-1)
    parser.add_argument('--auto-only', action='store_true')
    parser.add_argument('--profiles', help='Optional comma-separated subset/order for focused restart reproduction.')
    parser.add_argument('--expected-auto', choices=PROFILES)
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    parser.add_argument('--expected-scale', type=int, help='Require this automatic scene scale on startup and explicit detection.')
    parser.add_argument('--expected-post-aa', type=int, help='Require this automatic post-AA mode.')
    parser.add_argument('--legacy-commands', action='store_true', help='Exercise all four stock execMachineSpec mappings in gameplay.')
    parser.add_argument('--expected-startup', choices=PROFILES,
                        help='Require fresh first-launch canonical detection before the map script.')
    parser.add_argument('--startup-preset', choices=PROFILES,
                        help='Write a fresh autoexec profile with equal-to-bootstrap AA/scale overrides.')
    parser.add_argument('--saved-preset', choices=PROFILES,
                        help='Require preservation of an existing saved config whose spec marker is missing.')
    parser.add_argument('--unset-machine-spec', action='store_true',
                        help='Also leave the saved legacy machine classification unset; requires --saved-preset.')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not (320 <= args.width <= 16384 and 240 <= args.height <= 16384):
        parser.error('Window dimensions must be within the supported display catalog')
    if args.unset_machine_spec and not args.saved_preset:
        parser.error('--unset-machine-spec requires --saved-preset')
    output = (args.output or ROOT / '.tmp/preset-audit' / args.render_api).resolve()
    output.mkdir(parents=True, exist_ok=True)
    runtime = ROOT / '.install'
    verify_runtime_is_current(runtime)
    save = output / 'save'
    game = save / 'baseoq4'
    game.mkdir(parents=True, exist_ok=True)
    configured = args.startup_preset or args.saved_preset
    if args.startup_preset and args.saved_preset:
        raise ValueError('Choose either a fresh autoexec or an existing saved config')
    if configured:
        if (game/'config.spec').exists() or (game/'openQ4Config.cfg').exists():
            raise ValueError('Configuration tests require a fresh test profile')
        write_script(game/('autoexec.cfg' if args.startup_preset else 'openQ4Config.cfg'),
                     [f'seta com_performancePreset {configured}', 'seta com_machineSpec '+('-1' if args.unset_machine_spec else '3'),
                      'seta r_multiSamples 0', 'seta r_screenFraction '+('50' if args.startup_preset else '67')])
    spec = benchmark.RunSpec('preset-audit', 'SP', 'game/airdefense1', 'game/airdefense1',
        'functional preset coverage', 'spawn', 'auto', '60', '0', 'windowed', 'default', 'best', args.render_api)
    launch = benchmark.common_args(ROOT, runtime, save, 'openq4.log', '', spec, args.width, args.height,
        'baseline', False, False, (('in_mouse', '0'), ('in_joystick', '0'), ('ui_retained', '1')), 'presets.cfg')
    for key, value in [('si_gameType', 'singleplayer'), ('com_gameMode', 'SP'),
                       ('r_vkDevice', str(args.vk_device)), ('r_vkValidation', '1')]:
        benchmark.append_set(launch, key, value)
    benchmark.append_command(launch, 'map', 'game/airdefense1')
    assert sum(s.startswith('+') for s in launch) <= 64
    # Maximum first, then down through the ladder: catches inherited expensive
    # settings/resources. Each profile recreates the device in actual gameplay.
    sequence = tuple(args.profiles.split(',')) if args.profiles else (() if args.auto_only else ('maximum',) + PROFILES[:-1])
    if any(name not in PROFILES for name in sequence):
        raise ValueError('Unknown preset in --profiles')
    lines = ['echo PRESET_AUDIT_STARTUP', 'com_performancePreset', 'r_multiSamples',
             'r_screenFraction', 'r_postAA', 'echo PRESET_AUDIT_STARTUP_END',
             'god', 'notarget', 'g_showHud 1', 'ui_hudLayout 1', 'waitMsec 3000',
             'rendererVkProbe', 'performancePresetSelfTest']
    for name in sequence:
        lines += [f'echo PRESET_AUDIT_BEGIN_{name}', f'applyPerformancePreset {name}',
                  'vid_restart', 'waitMsec 3000', 'viewpos', 'gfxInfo', 'r_fullscreen',
                  'com_performancePreset', 'r_shadows', 'r_useShadowMap', 'r_ssao', 'r_hdrSceneTarget',
                  'r_ssaoSamples', 'r_shadowMapFilterTaps', 'r_shadowMapSize',
                  f'screenshot "screenshots/{name}.tga"', 'wait 3', f'echo PRESET_AUDIT_END_{name}']
    legacy = (('low',75,0),('performance',85,0),('balanced',100,2),('ultra',100,4))
    if args.legacy_commands:
        for level,(name,scale,msaa) in enumerate(legacy):
            lines += [f'echo PRESET_LEGACY_BEGIN_{level}', f'com_machineSpec {level}', 'execMachineSpec',
                      'com_performancePreset','r_screenFraction','r_multiSamples','r_mode',
                      'r_windowWidth','r_windowHeight','r_swapInterval',f'echo PRESET_LEGACY_END_{level}']
    lines += ['echo PRESET_AUDIT_AUTO', 'autoDetectPerformancePreset', 'com_performancePreset',
              'vid_restart', 'waitMsec 3000', 'gfxInfo', 'r_fullscreen',
              'r_screenFraction', 'r_postAA',
              'screenshot "screenshots/auto.tga"', 'wait 3', 'echo PRESET_AUDIT_COMPLETE', 'quit']
    write_script(game / 'presets.cfg', lines)
    (output / 'launch.json').write_text(json.dumps(launch, indent=2), encoding='utf-8')
    (output / 'runtime.json').write_text(json.dumps({str(p.relative_to(runtime)):
        hashlib.sha256(p.read_bytes()).hexdigest() for p in [runtime/'openQ4-client_x64.exe',
        runtime/f'renderer-{args.render_api}_x64.dll', runtime/'baseoq4/game-sp_x64.dll']}, indent=2), encoding='utf-8')
    code, timeout, elapsed = benchmark.launch_and_wait(runtime/'openQ4-client_x64.exe', launch,
        runtime, output/'stdout.txt', output/'stderr.txt', 600)
    text = (game/'logs/openq4.log').read_text(encoding='utf-8', errors='replace')
    failures = []
    if code or timeout:
        failures.append(f'process exit={code}, timeout={timeout}')
    identities = json.loads((output/'runtime.json').read_text(encoding='utf-8'))
    if any(hashlib.sha256((runtime/name).read_bytes()).hexdigest() != digest for name, digest in identities.items()):
        failures.append('staged runtime changed during test')
    for marker in ['PerformancePreset self-test passed (presets=9', 'PRESET_AUDIT_COMPLETE']:
        if marker not in text:
            failures.append('missing ' + marker)
    if args.expected_startup:
        startup = re.findall(r"First-launch performance preset '([^']+)'", text)
        if startup != [args.expected_startup]:
            failures.append('unexpected or missing first-launch preset ' + repr(startup))
    if args.saved_preset and re.search(r"First-launch performance preset '",text):
        failures.append('existing saved configuration was auto-detected again')
    startup_match = re.search(r'^PRESET_AUDIT_STARTUP\s*$(.*?)^PRESET_AUDIT_STARTUP_END\s*$',text,re.M|re.S)
    if args.expected_startup and not configured:
        for key,value in [('r_screenFraction',args.expected_scale),('r_postAA',args.expected_post_aa)]:
            if value is not None and (not startup_match or re.findall(rf'^"{key}" is:"([^"\n]+)"',startup_match[1],re.M) != [str(value)]):
                failures.append('incorrect first-launch '+key)
    if configured:
        match = re.search(r'^PRESET_AUDIT_STARTUP\s*$(.*?)^PRESET_AUDIT_STARTUP_END\s*$',text,re.M|re.S)
        expected = {'com_performancePreset':configured,'r_multiSamples':'0',
                    'r_screenFraction':'50' if args.startup_preset else '67'}
        for key,value in expected.items():
            if not match or re.findall(rf'^"{key}" is:"([^"\n]+)"',match[1],re.M) != [value]:
                failures.append('startup override not preserved: ' + key)
    backend = 'vulkan' if args.render_api == 'vk' else 'gl'
    if f'Renderer API: requested={backend} active={backend} disposition=' not in text:
        failures.append('active renderer differs from request')
    if re.search(r'VUID-|Validation Error|PerformancePreset self-test failed|FATAL ERROR|shadow map pass failed|required shadow resource unavailable', text):
        failures.append('runtime/validation/preset error; inspect log')
    for name in sequence:
        match = re.search(rf'^PRESET_AUDIT_BEGIN_{name}\s*$(.*?)^PRESET_AUDIT_END_{name}\s*$', text, re.M|re.S)
        if not match:
            failures.append('incomplete profile ' + name)
            continue
        section = match[1]
        modern = name in ('enhanced', 'quality', 'ultra', 'maximum')
        # Only the two tiers below the documented minimum hardware draw no shadows.
        expected = {'com_performancePreset': name, 'r_fullscreen': '0',
            'r_shadows': str(int(name not in ('minimum', 'lowpower'))),
            'r_useShadowMap': str(int(modern)), 'r_ssao': str(int(modern)), 'r_hdrSceneTarget': str(int(modern))}
        for key, value in expected.items():
            if re.findall(rf'^"{key}" is:"([^"\n]+)"', section, re.M) != [value]:
                failures.append(f'{name}: incorrect {key} readback')
        if not re.search(r'^origin: ', section, re.M) or 'Renderer AA:' not in section:
            failures.append(name + ': missing gameplay/device observation')
    auto = re.findall(r"Auto-detected performance preset '([^']+)' \(([^\n]+)\)", text)
    if not auto or auto[-1][0] not in PROFILES[:-1]:  # never Maximum
        failures.append('missing or unsafe automatic recommendation')
    elif args.expected_auto and auto[-1][0] != args.expected_auto:
        failures.append('unexpected automatic recommendation ' + auto[-1][0])
    auto_match = re.search(r'^PRESET_AUDIT_AUTO\s*$(.*?)^PRESET_AUDIT_COMPLETE\s*$',text,re.M|re.S)
    for key,value in [('r_screenFraction',args.expected_scale),('r_postAA',args.expected_post_aa),('r_fullscreen',0)]:
        if value is not None and (not auto_match or re.findall(rf'^"{key}" is:"([^"\n]+)"',auto_match[1],re.M) != [str(value)]):
            failures.append('incorrect automatic '+key)
    if args.legacy_commands:
        for level,(name,scale,msaa) in enumerate(legacy):
            section = re.search(rf'^PRESET_LEGACY_BEGIN_{level}\s*$(.*?)^PRESET_LEGACY_END_{level}\s*$',text,re.M|re.S)
            expected = {'com_performancePreset':name,'r_screenFraction':str(scale),'r_multiSamples':str(msaa),
                        'r_mode':'-1','r_windowWidth':str(args.width),'r_windowHeight':str(args.height),'r_swapInterval':'0'}
            for key,value in expected.items():
                if not section or re.findall(rf'^"{key}" is:"([^"\n]+)"',section[1],re.M) != [value]:
                    failures.append(f'legacy spec {level}: incorrect {key}')
    captures = {}
    from PIL import Image, ImageStat
    for name in (*sequence, 'auto'):
        path = game/f'screenshots/{name}.tga'
        if not path.is_file():
            failures.append('missing engine capture ' + name)
            continue
        with Image.open(path) as source:
            pixels = source.convert('RGB')
            if pixels.size != (args.width,args.height):
                failures.append('unexpected drawable capture size '+name+': '+repr(pixels.size))
            if max(ImageStat.Stat(pixels).stddev) < 5:
                failures.append('blank capture ' + name)
            png = output/f'{name}.png'
            pixels.save(png)
            with Image.open(png) as check:
                assert check.convert("RGB").tobytes() == pixels.tobytes()
            captures[name] = {'size': pixels.size, 'sha256': hashlib.sha256(png.read_bytes()).hexdigest()}
        # PNG is lossless and verified above; retain one copy of each capture.
        path.unlink()
    report = {'passed': not failures, 'failures': failures, 'exit': code, 'timeout': timeout,
        'elapsed': elapsed, 'auto': auto[-1] if auto else None, 'captures': captures,
        'limitations': 'Windowed functional SP smoke on the selected local device; no other hardware or FPS qualification.'}
    (output/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)
    return int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())
