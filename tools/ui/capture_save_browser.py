#!/usr/bin/env python3
"""Exercise retained save browsing, then save and restore real SP gameplay.

Uses the stable staged client, a hidden window and a private .tmp savepath.
All operations are semantic engine commands and engine render-target captures.
No OS input or screen capture is used. Physical devices remain unqualified.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import zipfile

import capture_retained_screens as capture
from capture_system_refresh import active_renderer

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
from mp_round_remote_smoke import verify_runtime_is_current


def activate(control):
    return [f'openq4_retainedGui focus "{control}"', 'openq4_retainedGui menu accept 1',
            'openq4_retainedGui menu accept 0', 'waitMsec 400']


def observe(stage):
    return [f'echo "SAVE_BROWSER_STAGE {stage}"', 'openq4_retainedGui report',
            *[f'openq4_retainedGui inspect "row-{row}-selected"' for row in range(4)],
            f'screenshot "screenshots/{stage}.tga"']


def steps():
    return ['waitMsec 1500', 'saveGame Zeta', 'waitMsec 1100', 'saveGame Alpha', 'waitMsec 1100',
            'saveGame Bravo', 'waitMsec 1100', 'screenshot "screenshots/gameplay.tga"',
            'openq4_assertMenuActivation 10000', 'waitMsec 1500', *activate('nav_loadgame'), 'waitMsec 1000',
            *activate('browser-manual'), *activate('row-1'), 'openq4_retainedGui event onSecondary', 'waitMsec 400',
            'openq4_retainedGui event onTertiary', 'waitMsec 400', 'openq4_retainedGui event onTertiary', 'waitMsec 400',
            *observe('name-sorted'),
            # Refresh under a held row. Alpha must stay selected and the Load
            # page must stay open. Delayed queue ordering is covered natively;
            # the console dispatcher services each diagnostic event immediately.
            'openq4_retainedGui focus "row-0"', 'openq4_retainedGui menu accept 1',
            'openq4_retainedGui event onSecondary', 'waitMsec 400', 'openq4_retainedGui menu accept 0', 'waitMsec 400',
            *observe('held-cancelled'),
            *activate('browser-automatic'), *observe('autosaves'),
            *activate('browser-manual'), *activate('row-1'), *activate('load_delete'),
            'waitMsec 600', 'screenshot "screenshots/delete-confirm.tga"', *activate('deleteModal_no'), 'waitMsec 600',
            *observe('delete-cancelled'),
            *activate('load_back'), 'waitMsec 1000', *activate('nav_savegame'), 'waitMsec 1000',
            *activate('browser-manual'), *activate('browser-sort'), *activate('browser-sort'),
            'openq4_retainedGui focus "save-name"', 'openq4_retainedGui number begin "save-name"',
            'openq4_retainedGui number input "save-name" "Middle name"',
            'openq4_retainedGui event onSecondary', 'waitMsec 400',
            'openq4_retainedGui number commit "save-name"', 'waitMsec 1500', *observe('saved-sorted'),
            *activate('save_back'), 'waitMsec 1000', *activate('nav_loadgame'), 'waitMsec 1000',
            # Loading clears the command buffer. The map-load hook resumes
            # verification from a separate script after the restore completes.
            'set g_autoExecAfterMapLoad "retained-save-browser-after.cfg"',
            *activate('load_go'), capture.AFTER_MAP,
            'waitMsec 1500', 'screenshot "screenshots/restored-gameplay.tga"', 'ui_retainedStatus']


def qualify(log):
    sections = dict(re.findall(r'^SAVE_BROWSER_STAGE (\S+)\s*\n(.*?)(?=^SAVE_BROWSER_STAGE |\Z)', log, re.M | re.S))
    checks = {}
    for stage, selected in (('name-sorted', 0), ('held-cancelled', 0), ('autosaves', -1),
                            ('delete-cancelled', 1), ('saved-sorted', 2)):
        section = sections.get(stage, '')
        rows = dict(re.findall(r'^RETAINED_GUI_NODE id=row-(\d+)-selected bounds=\S+ opacity=\S+ display=(\S+)', section, re.M))
        checks[stage] = len(rows) == 4 and all(rows[str(row)] == ('block' if row == selected else 'none') for row in range(4))
    checks['name-sort-keeps-selection'] = bool(re.search(r'RETAINED_SAVE_CATALOG page=load .*sort=2 filter=1 count=3 first=0 selected=0 slot=Alpha', log))
    checks['focus-follows-filename'] = 'focus=row-0 ' in sections.get('name-sorted', '')
    checks['save-revealed-in-order'] = bool(re.search(r'RETAINED_SAVE_CATALOG page=save .*sort=2 filter=1 count=4 first=0 selected=2 slot=Middle_name', log))
    checks['semantic-operations'] = not re.search(r'RETAINED_GUI_OPERATION .* failed|openq4_retainedGui: requires', log)
    checks['restored-gameplay'] = bool(re.search(r'^RETAINED_STAGE save\s*-\s*browser after map\s*$', log, re.M))
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), required=True)
    parser.add_argument('--family', choices=('marine', 'strogg'), default='marine')
    parser.add_argument('--language', default='english')
    parser.add_argument('--size', default='1280x720')
    args = parser.parse_args()
    args.runtime, args.gate, args.timeout, args.set = ROOT / '.install', '1', 240, []
    args.output = args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('choose a new output directory under the project .tmp')
    verify_runtime_is_current(args.runtime)
    args.output.mkdir(parents=True)
    binaries = [args.runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64'),
                *sorted(args.runtime.glob('renderer-*')), *sorted((args.runtime / 'baseoq4').glob('game-*'))]
    binaries = [p for p in binaries if p.is_file()]
    bindings = {str(p.relative_to(args.runtime)): capture.digest(p) for p in binaries}
    sources = ['src/framework/Session_menu.cpp', 'src/framework/Session_local.h', 'src/ui/UserInterfaceRetained.cpp',
               'tools/ui/retained_saves.py', 'tools/ui/capture_save_browser.py']
    documents = [f'guis/menu/{page}{suffix}.rui' for page in ('loadgame', 'savegame') for suffix in ('', '_strogg')]
    sources += ['content/baseoq4/pak0/' + path for path in documents]
    source_bindings = {p: capture.digest(ROOT / p) for p in sources}
    with zipfile.ZipFile(args.runtime / 'baseoq4/pak0.pk4') as pack:
        for document in documents:
            if pack.read(document).replace(b'\r\n', b'\n') != (ROOT / 'content/baseoq4/pak0' / document).read_bytes().replace(b'\r\n', b'\n'):
                raise RuntimeError('staged save document differs from source: ' + document)
    capture.SCENARIOS['save-browser'] = ('game/recomp' if args.family == 'strogg' else 'game/airdefense1', steps())
    result = capture.run(args, 'save-browser')
    log = re.sub(r'\^[0-9]', '', (ROOT / result['log']).read_text(encoding='utf-8', errors='replace'))
    checks = qualify(log)
    saves = (ROOT / result['log']).parents[1] / 'savegames'
    checks['saved-files'] = all((saves / (name + '.save')).is_file() for name in ('Alpha', 'Bravo', 'Zeta', 'Middle_name'))
    # Existing dynamic save-preview material precache notices are recorded
    # separately; actual retained GUI/runtime warnings still fail the run.
    result['preview_precache_warnings'] = [line for line in result['retained_warnings'] if re.search(
        r'Loading non pre-cached material decl _retained/(?:savegames/|gfx/guis/loadscreens/generic$)', line)]
    checks.update(completed=result['complete'] and result['returncode'] == 0 and not result['errors'],
                  renderer=active_renderer(log) == args.renderer, captures=len(result['screenshots']) == 8,
                  retained_clean=all(line in result['preview_precache_warnings'] for line in result['retained_warnings']),
                  unchanged_binaries=bindings == {str(p.relative_to(args.runtime)): capture.digest(p) for p in binaries},
                  unchanged_sources=source_bindings == {p: capture.digest(ROOT / p) for p in sources})
    result.update(checks=checks, binaries=bindings, sources=source_bindings, log_sha256=capture.digest(ROOT / result['log']),
                  active_renderer=active_renderer(log), family=args.family, language=args.language, scope=__doc__.strip(),
                  status='captured_pending_visual_review' if all(checks.values()) else 'failed')
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': result['status'], 'failed': [k for k, value in checks.items() if not value]}, indent=2))
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
