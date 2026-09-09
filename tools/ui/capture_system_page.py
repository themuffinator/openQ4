#!/usr/bin/env python3
"""Capture the opt-in production SYSTEM child through the normal Session route.

No testGUI, preview document, physical input, or OS screenshot is used. The fixed
script runs after three seconds of map gameplay. This is a bounded integration
probe; full SYSTEM, widget, editor and replacement acceptance remain open.
"""
from __future__ import annotations

import argparse
from functools import cmp_to_key
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import time
from zipfile import BadZipFile, ZipFile

ROOT = Path(__file__).resolve().parents[2]
PAGE = 'guis/menu/settings/system.q4ui'
PARENT = 'guis/mainmenu.gui'
STAGE_MARKER = 'SYSTEM_PAGE_STAGE'
COMPLETE = 'SYSTEM_PAGE_CAPTURE_COMPLETE'
FIELDS = ('open', 'dirty', 'busy', 'canApply', 'phase', 'discardVisible',
          'draftBrightness', 'baselineBrightness', 'draftShadows', 'baselineShadows',
          'draftPostAA', 'baselinePostAA')
CONTROLS = ('settings_brightness', 'settings_shadows', 'settings_postaa')
EXTRA_ROWS = (('settings_bloom','Bloom','r_bloom',1,0),
              ('settings_ssao','SSAO','r_ssao',1,0),
              ('settings_tonemap','Tonemap','r_hdrToneMap',1,0),
              ('settings_crt','CRT','r_crt',1,0),
              ('settings_irradiance','Irradiance','r_useLightGrid',1,0),
              ('settings_resolution_scale','ResolutionScale','r_screenFraction',3,100),
              ('settings_ui_aspect','UIAspect','ui_aspectCorrection',1,1))
EXTRA_SEED = tuple(row[4] for row in EXTRA_ROWS)
EXTRA_CHANGED = (1,1,1,1,1,85,0)
FIELDS += tuple(prefix+row[1] for row in EXTRA_ROWS for prefix in ('draft','baseline'))
CONTROLS += tuple(row[0] for row in EXTRA_ROWS)
ROLES = (2,1,3)+tuple(row[3] for row in EXTRA_ROWS)
TYPES = tuple(1 if role==1 else 0 for role in ROLES)
VALUE_INDICES = tuple(range(6,len(FIELDS),2))
SCREENSHOTS = ('draft', 'applied', 'discard', 'choice', 'reloaded', 'extrasapplied', 'extrasrestored', 'returned', 'reopened')


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def _compare_packages(left: Path, right: Path) -> int:
    # FileSystem.cpp FS_ParseNumberedPakName / FS_ComparePk4LoadOrder.
    # Later entries win. Preserve its capped numeric accumulator as well as
    # padded-number priority; ordinary lexical sorting is insufficient.
    a, b = left.name.lower(), right.name.lower()
    ma, mb = re.fullmatch(r'pak([0-9]+)\.pk4', a), re.fullmatch(r'pak([0-9]+)\.pk4', b)
    def number(digits):
        value = 0
        for digit in digits:
            if value < 1000000: value = value * 10 + int(digit)
        return value
    if ma and mb:
        ka, kb = (-len(ma[1]), number(ma[1])), (-len(mb[1]), number(mb[1]))
        if ka != kb: return (ka > kb) - (ka < kb)
    if bool(ma) != bool(mb):
        ka, kb = ('pak' if ma else a), ('pak' if mb else b)
        if ka != kb: return (ka > kb) - (ka < kb)
        return -1 if ma else 1
    return (a > b) - (a < b)


def staged_source(runtime: Path) -> tuple[bytes, dict]:
    """Resolve this page in the highest-priority cdpath/baseoq4 mount.

    No extraction or source copy. SetupGameDirectories mounts cdpath last;
    AddGameDirectory puts loose files before packages and later-loaded packages
    before earlier ones. Addon activation and duplicate archive members require
    runtime information this bounded probe does not have, so fail closed.
    """
    game = runtime / 'baseoq4'
    if not game.is_dir(): raise ValueError('Staged baseoq4 directory is missing')
    packages = [path for path in game.iterdir() if path.suffix.lower() == '.pk4']
    names = [path.name.lower() for path in packages]
    if len(set(names)) != len(names) or any(not name.isascii() for name in names):
        raise ValueError('Ambiguous package names in staged baseoq4')
    packages.sort(key=cmp_to_key(_compare_packages))
    candidates, selected = [], None
    for package in packages:
        try:
            with ZipFile(package) as archive:
                members = []
                addon = False
                for item in archive.infolist():
                    name = item.filename.replace('\\', '/').lower()
                    addon |= name == 'addon.conf'
                    if name.replace(':', '/') == PAGE:
                        if name != PAGE or item.orig_filename != item.filename or item.is_dir():
                            raise ValueError(f'Ambiguous SYSTEM member in {package.name}')
                        members.append(item)
                if len(members) > 1:
                    raise ValueError(f'Duplicate SYSTEM members in {package.name}')
                if not members: continue
                if addon: raise ValueError(f'Addon-dependent SYSTEM source in {package.name}')
                member = members[0]
                if member.file_size > 8 * 1024 * 1024 or member.flag_bits & 1 or member.compress_type not in (0, 8):
                    raise ValueError(f'Unsupported SYSTEM member encoding or size in {package.name}')
                raw = archive.read(member)  # Also verifies the member CRC.
                binding = {'kind':'package', 'package':str(package), 'package_sha256':digest(package),
                           'member':member.filename, 'member_sha256':hashlib.sha256(raw).hexdigest(),
                           'member_size':len(raw), 'member_crc32':f'{member.CRC:08x}'}
                candidates.append(binding)
                selected = raw, binding
        except (BadZipFile, RuntimeError, NotImplementedError) as error:
            raise ValueError(f'Cannot verify staged package {package.name}: {error}') from error
    loose = game
    for part in PAGE.split('/'):
        if not loose.is_dir(): loose = None; break
        matches = [path for path in loose.iterdir() if path.name.lower() == part]
        if len(matches) > 1: raise ValueError('Ambiguous loose SYSTEM path')
        if not matches: loose = None; break
        loose = matches[0]
    if loose is not None:
        if not loose.is_file(): raise ValueError('Loose SYSTEM override is not a regular file')
        raw = loose.read_bytes()
        binding = {'kind':'loose', 'path':str(loose), 'sha256':hashlib.sha256(raw).hexdigest(), 'size':len(raw)}
        candidates.append(binding)
        selected = raw, binding
    if selected is None: raise ValueError('Production SYSTEM source is absent from staged packages and loose files')
    return selected[0], {'effective':selected[1], 'candidates_low_to_high':candidates,
                         'packages_low_to_high':[path.name for path in packages],
                         'mount':str(game), 'precedence':'cdpath/baseoq4: loose, then reverse engine PK4 load order'}


def key(action: str) -> list[str]:
    return [f'openq4_retainedGui menu "{action}" 1', f'openq4_retainedGui menu "{action}" 0']


def activate(control: str) -> list[str]:
    return [f'openq4_retainedGui focus "{control}"'] + key('accept')


def stages() -> list[dict]:
    rows = []

    def add(name, commands, *, brightness=1.1, baseline=1.1, shadows=1, postaa=0,
            dirty=0, discard=0, popup=0, live=1.1, page=True, actions=(), events=(),
            screenshot=None, resets=0, opening=False, extras=EXTRA_SEED,
            extra_baseline=EXTRA_SEED, resolution_first=0):
        rows.append(dict(name=name, commands=commands, page=page,
                         values=(1, dirty, 0, dirty, 1, discard, brightness, baseline, shadows, 1, postaa, 0)
                                +tuple(value for pair in zip(extras,extra_baseline) for value in pair),
                         popup=popup, live=live, actions=list(actions), events=list(events),
                         screenshot=screenshot, resets=resets, opening=opening, resolution_first=resolution_first))

    add('open', ['openq4_system open'], brightness=1, baseline=1, live=1,
        actions=[('begin', 0)], events=[('onactivate', 1, 1)], opening=True)
    add('brightness_dirty', ['openq4_retainedGui focus "settings_brightness"'] + key('right'),
        baseline=1, dirty=1, live=1, actions=[('edit', 1)], screenshot='draft')
    add('applied', activate('settings_apply'), actions=[('apply', 0)], screenshot='applied')
    add('shadow_dirty', activate('settings_shadows'), shadows=0, dirty=1, actions=[('edit', 1)])
    add('discard_dialog', activate('settings_back'), shadows=0, dirty=1, discard=1,
        events=[('onback', 0, 1)], screenshot='discard')
    add('keep_editing', activate('discard_keep_editing'), shadows=0, dirty=1,
        events=[('continueediting', 0, 1)])
    add('discarded', activate('settings_back') + ['wait 3'] + activate('discard_changes'),
        page=False, actions=[('cancel', 0)], events=[('onback', 0, 1), ('discard', 2, 1)])
    add('reopen_discarded', ['openq4_system open'], actions=[('begin', 0)],
        events=[('onactivate', 1, 1)], opening=True)
    add('choice_tentative', activate('settings_postaa') + key('down'), popup=1, screenshot='choice')
    add('choice_cancelled', key('back'))
    add('choice_dirty', activate('settings_postaa') + key('down') + key('down') + key('accept'),
        postaa=2, dirty=1, actions=[('edit', 1)])
    add('choice_restored', activate('settings_back') + ['wait 3'] + activate('discard_changes'),
        page=False, actions=[('cancel', 0)], events=[('onback', 0, 1), ('discard', 2, 1)])
    add('reopen_choice', ['openq4_system open'], actions=[('begin', 0)],
        events=[('onactivate', 1, 1)], opening=True)
    add('reload_draft', ['openq4_retainedGui focus "settings_brightness"'] + key('right'),
        brightness=1.2, dirty=1, actions=[('edit', 1)])
    add('language', ['reloadLanguage', 'wait 30'], brightness=1.2, dirty=1, resets=1)
    add('video', ['vid_restart windowed', 'wait 60'], brightness=1.2, dirty=1, resets=1, screenshot='reloaded')
    add('final_clean', activate('settings_back') + ['wait 3'] + activate('discard_changes'),
        page=False, actions=[('cancel', 0)], events=[('onback', 0, 1), ('discard', 2, 1)])
    add('reopen_clean', ['openq4_system open'], actions=[('begin', 0)],
        events=[('onactivate', 1, 1)], opening=True)
    extra_changed, extra_seed = [], []
    for control,_,_,role,_ in EXTRA_ROWS:
        if role==1:
            extra_changed += activate(control); extra_seed += activate(control)
        else:
            extra_changed += activate(control)+key('home')+key('down')*4+key('accept')
            extra_seed += activate(control)+key('home')+key('down')*5+key('accept')
    add('extras_dirty', extra_changed, extras=EXTRA_CHANGED, dirty=1, actions=[('edit',1)]*7)
    add('extras_applied', activate('settings_apply'), extras=EXTRA_CHANGED,
        extra_baseline=EXTRA_CHANGED, actions=[('apply',0)], screenshot='extrasapplied')
    add('extras_restore_draft', extra_seed, extra_baseline=EXTRA_CHANGED, dirty=1,
        actions=[('edit',1)]*7, resolution_first=1)
    add('extras_restored', activate('settings_apply'), actions=[('apply',0)], resolution_first=1, screenshot='extrasrestored')
    add('returned', activate('settings_back'), page=False, events=[('onback', 1, 0)], screenshot='returned')
    add('reopened', ['openq4_system open'], actions=[('begin', 0)], events=[('onactivate', 1, 1)],
        opening=True, screenshot='reopened')
    add('finished', activate('settings_back'), page=False, events=[('onback', 1, 0)])
    return rows


def script() -> str:
    lines = ['openq4_assertMenuActivation 10000', 'wait 3']
    for stage in stages():
        lines += [f'echo {STAGE_MARKER} {stage["name"]}'] + stage['commands'] + ['wait 6', 'openq4_system report']
        if stage['page']:
            lines += [f'openq4_guiGet "{field}"' for field in FIELDS]
            lines += [f'openq4_retainedGui widget "{control}"' for control in CONTROLS]
            lines += ['openq4_retainedGui report']
        if stage['screenshot']:
            lines += [f'screenshot "screenshots/system-{stage["screenshot"]}.tga"']
    lines += ['gfxInfo', f'echo {COMPLETE}', 'quit']
    return '\n'.join(lines) + '\n'


def source_contract(runtime: Path) -> dict:
    source = ROOT / 'content/baseoq4/pak0' / PAGE
    staged, binding = staged_source(runtime)
    if not source.is_file() or source.read_bytes() != staged:
        raise ValueError('Production SYSTEM source must exactly match its effective staged member or loose override')
    # The first-party page uses JSON plus standalone copyright comments. Do not
    # silently rewrite or copy the source into a test-specific loader location.
    model = json.loads('\n'.join(line for line in source.read_text(encoding='utf-8').splitlines()
                                 if not line.lstrip().startswith('//')))
    if model.get('id') != 'openq4.system':
        raise ValueError('Unexpected production SYSTEM document identity')
    nodes, pending = {}, [model['root']]
    while pending:
        node = pending.pop()
        nodes[node['id']] = node
        pending.extend(node.get('children', []))
    required = CONTROLS + ('settings_apply', 'settings_back', 'discard_changes', 'discard_keep_editing')
    if any(node not in nodes for node in required) or any(field not in model.get('aliases', {}) for field in FIELDS):
        raise ValueError('Production SYSTEM probe controls or readback aliases changed')
    slider = nodes['settings_brightness']['control']
    if (slider.get('role') != 'slider' or slider.get('minimum') != .5 or slider.get('maximum') != 2 or slider.get('step') != .1):
        raise ValueError('Production brightness tick contract changed')
    choice = nodes['settings_postaa']['control']
    if [option.get('value') for option in choice.get('options', [])] != list(range(5)):
        raise ValueError('Production PostAA option contract changed')
    for field, state in {'draftBrightness':'settings.draft.r_brightness', 'baselineBrightness':'settings.baseline.r_brightness',
                         'draftShadows':'settings.draft.r_shadows', 'baselineShadows':'settings.baseline.r_shadows',
                         'draftPostAA':'settings.draft.r_postAA', 'baselinePostAA':'settings.baseline.r_postAA'}.items():
        target = model['aliases'][field].get('variable')
        if model.get('presentationVariables', {}).get(target, {}).get('value') != {'state':state}:
            raise ValueError(f'Production service readback alias changed: {field}')
    for control,alias,key,role,_ in EXTRA_ROWS:
        spec=nodes[control].get('control',{})
        if spec.get('role')!=('toggle' if role==1 else 'choice') or spec.get('value')!={'state':'settings.draft.'+key}:
            raise ValueError(f'Production immediate control readback changed: {control}')
        action=model.get('actions',{}).get(spec.get('action'))
        wanted={'input':'boolean' if role==1 else 'number','operation':'settings.system.edit','arguments':{key:{'input':'value'}}}
        if action!=wanted: raise ValueError(f'Production immediate control proposal changed: {control}')
        if role==3 and [option.get('value') for option in spec.get('options',[])]!=[10,25,50,75,85,100,125,150,200]:
            raise ValueError('Production resolution scale option contract changed')
        for prefix in ('draft','baseline'):
            field=prefix+alias; target=model['aliases'][field].get('variable')
            if model.get('presentationVariables',{}).get(target,{}).get('value')!={'state':'settings.'+prefix+'.'+key}:
                raise ValueError(f'Production service readback alias changed: {field}')
    return {'document':model['id'], 'source':str(source), 'staged':binding, 'sha256':digest(source),
            'normal_filesystem_path':PAGE, 'replacement_acceptance':False}


def _fields(line: str, prefix: str, expected: set[str]) -> dict | None:
    parts = [token.split('=', 1) for token in line[len(prefix):].split()]
    if any(len(part) != 2 for part in parts):
        return None
    result = dict(parts)
    return result if len(parts) == len(expected) and set(result) == expected else None


def _number(value: str, expected: float) -> bool:
    if len(value) > 64 or re.fullmatch(r'-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?', value) is None:
        return False
    number = float(value)
    return math.isfinite(number) and math.isclose(number, expected, rel_tol=1e-8, abs_tol=1e-8)


def tga_dimensions(raw: bytes) -> tuple[int, int] | None:
    if len(raw) < 18 or raw[1] != 0 or raw[2] not in (2, 10) or raw[16] not in (24, 32): return None
    size = struct.unpack_from('<HH',raw,12)
    if size != (1280,720): return None
    channels, offset, pixels = raw[16]//8, 18+raw[0], size[0]*size[1]
    if raw[2] == 2: return size if len(raw) >= offset+pixels*channels else None
    decoded = 0
    while decoded < pixels:
        if offset >= len(raw): return None
        packet = raw[offset]; offset += 1
        count = (packet & 127)+1; decoded += count
        offset += channels if packet & 128 else count*channels
        if decoded > pixels or offset > len(raw): return None
    return size


def qualify(log: str, mode: str) -> dict:
    errors, records, activation, completion = [], [], [], []
    current = None
    for line in log.splitlines():
        stripped = line.strip()
        if STAGE_MARKER in line:
            match = re.fullmatch(STAGE_MARKER + r' ([a-z_]+)', stripped)
            if not match:
                errors.append('Malformed standalone stage marker'); continue
            current = {'name':match.group(1), 'lines':[]}; records.append(current)
        elif COMPLETE in line:
            completion.append(stripped)
            if stripped != COMPLETE: errors.append('Malformed standalone completion marker')
        elif line.startswith('OPENQ4_MENU_ACTIVATION '):
            activation.append(line)
            if records: errors.append('Menu activation occurred after SYSTEM observations')
        elif line.startswith(('OPENQ4_SYSTEM ', 'GUI_VALUE ', 'RETAINED_GUI_', 'RETAINED_GUI ', 'UI_SETTINGS ')):
            if current is None or completion: errors.append('Observation outside the ordered stage interval')
            else: current['lines'].append(line)
    if len(activation) != 1 or not re.fullmatch(r'OPENQ4_MENU_ACTIVATION PASS elapsed=\d+ms limit=10000ms', activation[0]):
        errors.append('Missing bounded normal menu activation from map gameplay')
    elif len(re.search(r'elapsed=(\d+)', activation[0]).group(1)) > 5 or int(re.search(r'elapsed=(\d+)', activation[0]).group(1)) > 10000:
        errors.append('Menu activation exceeded its limit')
    if completion != [COMPLETE]: errors.append('Missing or duplicate standalone completion')
    expected_stages = stages()
    if [record['name'] for record in records] != [stage['name'] for stage in expected_stages]:
        errors.append('Missing, reordered, duplicate or extra SYSTEM stages')
    owners = []
    route_fields = set('enabled active parent child guiTest menu map multiplayer menuSound canReturn'.split())
    widget_fields = set('id role type accepted pending proposed rejected token popup firstVisible'.split())
    report_fields = set('path focus revision active brightness shadows contexts'.split())
    for record, stage in zip(records, expected_stages):
        at = stage['name']; lines = record['lines']
        routes, reads, widgets, reports, operations, actions, events, resources, loads, observed, dispatches = [], [], [], [], [], [], [], [], [], [], []
        for line in lines:
            if line.startswith('OPENQ4_SYSTEM operation='):
                operations.append(line)
            elif line.startswith('OPENQ4_SYSTEM '):
                routes.append(_fields(line, 'OPENQ4_SYSTEM ', route_fields)); observed.append('route')
            elif line.startswith('GUI_VALUE '):
                parts = line[len('GUI_VALUE '):].split('=', 1); reads.append(parts); observed.append('read:' + parts[0])
            elif line.startswith('RETAINED_GUI_WIDGET '):
                widgets.append(_fields(line, 'RETAINED_GUI_WIDGET ', widget_fields)); observed.append('widget')
            elif line.startswith('RETAINED_GUI path='):
                reports.append(_fields(line, 'RETAINED_GUI ', report_fields)); observed.append('report')
            elif line.startswith('RETAINED_GUI_OPERATION '):
                match = re.fullmatch(r'RETAINED_GUI_OPERATION (focus|menu) passed', line)
                if not match: errors.append(f'{at}: failed or unexpected semantic operation')
                else: events.append(('operation', match.group(1)))
            elif line.startswith('UI_SETTINGS '):
                match = re.fullmatch(r'UI_SETTINGS operation=settings\.system\.(\w+) result=(\d+) phase=(\d+) owner=(\d+) dirty=(\d+)', line)
                if not match: errors.append(f'{at}: malformed settings result')
                else: actions.append(match.groups())
            elif line.startswith('RETAINED_GUI_EVENT '):
                match = re.fullmatch(r'RETAINED_GUI_EVENT name=(\w+) actions=(\d+) writes=(\d+)', line)
                if not match: errors.append(f'{at}: malformed program trace')
                else: events.append(('event', match[1].lower(), int(match[2]), int(match[3])))
            elif line.startswith('RETAINED_GUI_RESOURCE '): resources.append(line)
            elif line.startswith('RETAINED_GUI_LOADED '): loads.append(line)
            elif line.startswith('RETAINED_GUI_DISPATCH '):
                dispatches.append(_fields(line,'RETAINED_GUI_DISPATCH ',set('path operation value brightness shadows close'.split())))
            else: errors.append(f'{at}: unexpected traced observation')
        if operations != (['OPENQ4_SYSTEM operation=open result=1'] if stage['opening'] else []):
            errors.append(f'{at}: normal SYSTEM route failed or repeated')
        settled = [index for index,line in enumerate(lines) if line.startswith('OPENQ4_SYSTEM ') and not line.startswith('OPENQ4_SYSTEM operation=')]
        if settled and any(index > settled[-1] for index,line in enumerate(lines)
                           if line.startswith(('UI_SETTINGS ', 'RETAINED_GUI_EVENT ', 'RETAINED_GUI_RESOURCE ',
                                               'RETAINED_GUI_OPERATION ', 'RETAINED_GUI_LOADED ', 'RETAINED_GUI_DISPATCH '))):
            errors.append(f'{at}: work occurred after the settled observations')
        if loads != (['RETAINED_GUI_LOADED '+PAGE] if stage['opening'] else []):
            errors.append(f'{at}: page was not loaded through the expected normal route or was replayed')
        if resources != ['RETAINED_GUI_RESOURCE path='+PAGE+' event=restored'] * stage['resets']:
            errors.append(f'{at}: resource restoration missing, replayed or failed')
        if len(dispatches) != int(not stage['page']): errors.append(f'{at}: unexpected direct host action or repeated dismissal')
        for row in dispatches:
            if not row or any(row[key] != value for key,value in {'path':PAGE,'operation':'ui.dismiss','value':'-','shadows':'1','close':'1'}.items()) or not _number(row['brightness'],stage['live']):
                errors.append(f'{at}: invalid direct host dismissal')
        expected_observed = ['route'] * (2 if stage['opening'] else 1)
        if stage['page']: expected_observed += ['read:'+field for field in FIELDS] + ['widget']*len(CONTROLS) + ['report']
        if observed != expected_observed: errors.append(f'{at}: readback order/count differs from fixed script')
        for route in routes:
            expected_route = {'enabled':'1', 'active':PAGE if stage['page'] else PARENT, 'parent':PARENT if stage['page'] else '-',
                              'child':str(int(stage['page'])), 'guiTest':'0', 'menu':'1', 'map':'1', 'multiplayer':str(int(mode=='mp')),
                              'menuSound':'1', 'canReturn':str(int(stage['page'] and not stage['values'][1]))}
            if route != expected_route: errors.append(f'{at}: actual Session parent/child/gameplay ownership mismatch')
        if stage['page']:
            if len(reads) != len(FIELDS) or any(len(row)!=2 or row[0]!=field or not _number(row[1],value)
                                              for row,field,value in zip(reads,FIELDS,stage['values'])):
                errors.append(f'{at}: service-backed aliases differ from expected draft/baseline')
            values = tuple(stage['values'][index] for index in VALUE_INDICES)
            for index, widget in enumerate(widgets):
                if not widget or index >= len(CONTROLS):
                    errors.append(f'{at}: malformed widget readback'); continue
                fixed = {'id':CONTROLS[index], 'role':str(ROLES[index]), 'type':str(TYPES[index]),
                         'pending':'0', 'rejected':'0', 'token':'0', 'popup':str(stage['popup'] if index==2 else 0),
                         'firstVisible':str(stage['resolution_first'] if CONTROLS[index]=='settings_resolution_scale' else 0)}
                if any(widget[key] != value for key,value in fixed.items()) or not _number(widget['accepted'],values[index]) or not _number(widget['proposed'],0):
                    errors.append(f'{at}: widget accepted/pending/popup state mismatch')
            for report in reports:
                if (not report or report['path'] != PAGE or report['active'] != '1' or report['contexts'] != '1' or
                    not re.fullmatch(r'\d+',report['revision']) or not _number(report['brightness'],stage['live']) or report['shadows'] != '1'):
                    errors.append(f'{at}: actual host/rendered owner readback mismatch')
        expected_actions = [(operation,'0','0' if operation=='cancel' else '1',str(dirty)) for operation,dirty in stage['actions']]
        if [(row[0],row[1],row[2],row[4]) for row in actions] != expected_actions:
            errors.append(f'{at}: settings operation failed, repeated or mutated the wrong draft')
        for row in actions:
            if not re.fullmatch(r'[1-9]\d{0,19}',row[3]) or int(row[3]) >= 2**64: errors.append(f'{at}: invalid service owner token')
            owners.append((at,row[0],row[3]))
        expected_operations = [('operation', line.split()[1]) for line in stage['commands'] if line.startswith('openq4_retainedGui ')]
        if [event for event in events if event[0]=='operation'] != expected_operations:
            errors.append(f'{at}: semantic input operation sequence mismatch')
        if [event[1:] for event in events if event[0]=='event'] != stage['events']:
            errors.append(f'{at}: authored lifecycle/Back program replay or omission')
    if owners:
        current_owner, prior_owners = None, set()
        for at,operation,owner in owners:
            if operation == 'begin':
                if owner in prior_owners: errors.append(f'{at}: reopened SYSTEM page reused an old owner')
                prior_owners.add(owner); current_owner = owner
            elif owner != current_owner:
                errors.append(f'{at}: SYSTEM editing owner changed before close or across resource restoration')
    else: errors.append('No real settings service operations were observed')
    for line in log.splitlines():
        if ('ERROR:' in line or 'FATAL:' in line or 'openq4_guiGet: unknown' in line or
            line.startswith(('usage: openq4_', 'openq4_retainedGui: requires'))): errors.append('Engine error or unsupported semantic command')
    return {'passed':not errors, 'errors':errors, 'stages':records, 'service_owners':owners,
            'normal_session_route':not errors, 'replacement_acceptance':False, 'full_system_acceptance':False,
            'widget_editor_acceptance':False, 'host_input_injection':False}


def capture(args) -> int:
    output, runtime = args.output.resolve(), args.runtime.resolve()
    if output.exists(): raise ValueError('Use a new output directory to preserve evidence')
    if not output.is_relative_to((ROOT/'.tmp').resolve()): raise ValueError('Capture evidence must use a private repository .tmp directory')
    executable = runtime / ('openQ4-client_x64.exe' if os.name=='nt' else 'openQ4-client_x64')
    if not executable.is_file() or not (args.assets/'q4base').is_dir(): raise ValueError('Staged client and installed q4base assets are required')
    source = source_contract(runtime)
    profiles = json.loads((ROOT/'.vscode/launch.json').read_text(encoding='utf-8-sig'))['configurations']
    prefix = '(SP) airdefense1 ' if args.mode=='sp' else '(MP) q4dm1 '
    profile = next(row for row in profiles if row['name'].startswith(prefix) and row['name'].endswith('GL'))
    game = output/'save/baseoq4'; game.mkdir(parents=True)
    cfg = game/'system-page.cfg'; cfg.write_text(script(),encoding='utf-8')
    density = args.density if args.density is not None else (1.25 if args.mode=='sp' else 2)
    overrides = {'fs_basepath':str(args.assets.resolve()), 'fs_savepath':str(output/'save'), 'fs_devpath':str(output/'save'),
        'fs_game':'baseoq4', 'logFile':'2', 'logFileName':'logs/openq4.log',
        'r_fullscreen':'0', 'r_fullscreenDesktop':'0', 'r_borderless':'0', 'r_borderlessDefaultMigrated':'1', 'r_hiddenWindow':'1',
        'r_windowWidth':'1280', 'r_windowHeight':'720', 'r_mode':'-1', 'r_customWidth':'1280', 'r_customHeight':'720',
        'r_renderApi':args.renderer, 'r_rendererSharedGui':'1', 'r_rendererSharedInWorldGui':'0', 'r_multiSamples':'0', 'r_swapInterval':'1',
        'r_gamma':'1', 'r_brightness':'1', 'r_shadows':'1', 'r_postAA':'0',
        **{row[2]:str(row[4]) for row in EXTRA_ROWS},
        'in_mouse':'0', 'in_joystick':'0', 'in_joystickRumble':'0', 'g_autoScreenshot':'0', 'g_autoSkipCinematics':'1',
        'g_autoExecAfterMapLoad':'system-page.cfg', 'g_autoExecAfterMapLoadDelayMs':'3000',
        'com_skipLoadingContinue':'1', 'com_loadingContinueAutoAdvance':'1', 'com_maxfps':'60',
        'ui_autoJoin':'1' if args.mode=='mp' else '0', 'ui_retainedScale':'1', 'ui_retainedDensity':str(density),
        'ui_retainedTrace':'1', 'ui_retainedSystem':'1', 'ui_retainedReducedMotion':'0'}
    command = [str(executable)]
    for key,value in overrides.items(): command += ['+set',key,value]
    original, index = profile['args'], 0
    while index < len(original):
        count = 3 if original[index].lower() in ('+set','+seta') and index+2 < len(original) else 1
        if count==1 or original[index+1].lower() not in {key.lower() for key in overrides}:
            command += [value.replace('${workspaceFolder}',str(ROOT)) for value in original[index:index+count]]
        index += count
    binaries = [executable] + sorted(runtime.glob('renderer-*')) + sorted((runtime/'baseoq4').glob('game-*'))
    metadata = {'status':'running', 'profile':profile['name'], 'mode':args.mode, 'renderer':args.renderer,
        'command':command, 'cwd':str(runtime), 'source':source, 'script_sha256':digest(cfg), 'density':density, 'ui_scale':1,
        'windowed':True, 'hidden_window':True, 'host_input_injection':False, 'capture_method':'engine screenshot after active map gameplay',
        'binaries':{str(path.relative_to(runtime)):digest(path) for path in binaries if path.is_file()}}
    report = output/'capture.json'
    def save(): report.write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    save(); started = time.monotonic()
    with (output/'process.log').open('w',encoding='utf-8') as process_log:
        options = {'env':{**os.environ,'TEMP':str(ROOT/'.tmp'),'TMP':str(ROOT/'.tmp')}}
        if os.name=='nt':
            startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow=0
            options.update(startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
        process = subprocess.Popen(command,cwd=runtime,stdout=process_log,stderr=subprocess.STDOUT,**options)
        print(f'SYSTEM page {args.mode}/{args.renderer}: PID {process.pid}, hidden/windowed, no host input.',flush=True)
        try: metadata['returncode'] = process.wait(timeout=240)
        except subprocess.TimeoutExpired:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
            metadata['status']='timeout'; save(); return 1
    metadata['seconds'] = round(time.monotonic()-started,3)
    log_path = game/'logs/openq4.log'
    log = re.sub(r'\^[0-9]','',log_path.read_text(encoding='utf-8',errors='replace')) if log_path.is_file() else ''
    metadata['log_sha256'] = digest(log_path) if log_path.is_file() else None
    metadata['qualification'] = qualify(log,args.mode)
    metadata['diagnostics'] = [line for line in log.splitlines() if any(word in line for word in ('WARNING:','ERROR:','FATAL:'))]
    active = re.findall(r'Renderer API: requested=\S+ active=(\S+)',log)
    metadata['active_renderer'] = active[-1] if active else None
    valid = metadata['returncode']==0 and metadata['qualification']['passed'] and metadata['active_renderer']==args.renderer
    metadata['screenshots'] = []
    for name in SCREENSHOTS:
        path = game/f'screenshots/system-{name}.tga'
        if not path.is_file() or path.stat().st_size < 18: valid=False; continue
        raw = path.read_bytes(); size = tga_dimensions(raw)
        if size is None: valid=False
        metadata['screenshots'].append({'path':str(path.relative_to(output)), 'size':size, 'sha256':digest(path)})
    try:
        metadata['source_after'] = source_contract(runtime)
        metadata['source_unchanged'] = metadata['source_after'] == source
    except (OSError,ValueError) as error:
        metadata['source_unchanged'] = False; metadata['source_change_error'] = str(error)
    valid = valid and metadata['source_unchanged']
    metadata['status'] = 'captured_pending_visual_and_warning_review' if valid else 'failed'
    save(); print(f'{metadata["status"]}: {report}',flush=True); return 0 if valid else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('sp','mp'),required=True)
    parser.add_argument('--renderer',choices=('gl','vulkan'),required=True)
    parser.add_argument('--assets',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,default=ROOT/'.install')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--density',type=float,choices=(1.25,2.0))
    args = parser.parse_args()
    try: return capture(args)
    except (OSError,ValueError,StopIteration) as error:
        print(f'SYSTEM page capture failed: {error}',flush=True); return 1


if __name__ == '__main__':
    raise SystemExit(main())
