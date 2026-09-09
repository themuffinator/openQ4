#!/usr/bin/env python3
"""Probe normal SYSTEM dirty-exit ownership and durable display confirmation.

Uses only fixed engine semantic commands after map gameplay, hidden/windowed
with mouse and joystick disabled. API-present receipts prove ordering, not
scanout or visual quality. This does not qualify full SYSTEM/editor replacement.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time

import capture_system_page as page

ROOT = page.ROOT
PAGE, PARENT = page.PAGE, page.PARENT
STAGE_MARKER = 'SYSTEM_EXIT_STAGE'
COMPLETE = 'SYSTEM_EXIT_CAPTURE_COMPLETE'
FIELDS = ('open', 'dirty', 'busy', 'canApply', 'phase', 'discardVisible',
          'draftBrightness', 'baselineBrightness', 'draftVSync', 'baselineVSync',
          'request', 'confirmationVisible', 'canConfirm', 'canRevert', 'canRetry', 'remaining')
CONTROLS = ('settings_brightness', 'settings_vsync')
SCREENSHOTS = ('discard-dialog', 'continued', 'immediate-return', 'vsync-draft', 'confirmation',
               'reverted', 'keep-return', 'reopened')
digest, key, activate = page.digest, page.key, page.activate


def stages():
    rows = []
    def add(name, commands, *, brightness=1.1, baseline=1.1, vsync=1,
            dirty=0, discard=0, live=1.1, child=True, opening=False,
            confirm=False, interval=1, restarts=0, actions=(), events=(),
            shot=None, display=None, exit_event=False, dismiss=False):
        values = (1, dirty, int(confirm), int(dirty and not confirm), 2 if confirm else 1,
                  discard, brightness, baseline, vsync, 1 if vsync == 0 and dirty else vsync,
                  None if confirm else '', int(confirm), int(confirm), int(confirm), 0,
                  None if confirm else 0)
        rows.append(dict(name=name, commands=commands, values=values, child=child,
                         opening=opening, live=live, interval=interval, restarts=restarts,
                         actions=list(actions), events=list(events), shot=shot,
                         display=display, exit_event=exit_event, dismiss=dismiss))
    begin = [('begin', 1, 0)]
    # Activate() supplies this spelling; control event IDs below are folded.
    onactivate = [('onActivate', 1, 1)]
    back = [('onback', 0, 1)]
    edit = [('edit', 1, 1)]
    brightness_edit = ['openq4_retainedGui focus "settings_brightness"'] + key('right')
    # Home then Accept selects the authored first option (0); no physical input.
    vsync_edit = activate('settings_vsync') + key('home') + key('accept')
    add('open', ['openq4_system open'], brightness=1, baseline=1, live=1,
        opening=True, actions=begin, events=onactivate)
    add('discard_draft', brightness_edit, baseline=1, dirty=1, live=1, actions=edit)
    add('discard_dialog', activate('settings_back'), baseline=1, dirty=1, discard=1,
        live=1, events=back, shot='discard-dialog')
    add('discard_exit', activate('discard_changes'), child=False, live=1,
        actions=[('cancel', 0, 0)], events=[('discard', 2, 1)], dismiss=True)
    add('reopen_discard', ['openq4_system open'], brightness=1, baseline=1, live=1,
        opening=True, actions=begin, events=onactivate)
    add('immediate_draft', brightness_edit, baseline=1, dirty=1, live=1, actions=edit)
    add('continue_dialog', activate('settings_back'), baseline=1, dirty=1, discard=1,
        live=1, events=back)
    # Accept the modal's safe initial focus without a diagnostic Focus command.
    add('continued', key('accept'), baseline=1, dirty=1, live=1,
        events=[('continueediting', 0, 1)], shot='continued')
    add('immediate_dialog', activate('settings_back'), baseline=1, dirty=1, discard=1,
        live=1, events=back)
    add('immediate_exit', activate('discard_apply_changes'), child=False,
        actions=[('applyExit', 0, 0)], events=[('applyexit', 1, 1)],
        exit_event=True, shot='immediate-return')
    add('reopen_immediate', ['openq4_system open'], opening=True, actions=begin, events=onactivate)
    add('display_draft', vsync_edit, vsync=0, dirty=1, actions=edit, shot='vsync-draft')
    add('display_dialog', activate('settings_back'), vsync=0, dirty=1, discard=1, events=back)
    add('display_confirm', activate('discard_apply_changes'), vsync=0, dirty=1, confirm=True,
        interval=0, restarts=1, actions=[('applyExit', 4, 1)], events=[('applyexit', 1, 1)],
        display='apply', shot='confirmation')
    add('display_reverted', key('accept'), restarts=2,
        actions=[('revert', 2, 1)], display='restore', shot='reverted')
    add('keep_draft', vsync_edit, vsync=0, dirty=1, restarts=2, actions=edit)
    add('keep_dialog', activate('settings_back'), vsync=0, dirty=1, discard=1,
        restarts=2, events=back)
    add('keep_confirm', activate('discard_apply_changes'), vsync=0, dirty=1, confirm=True,
        interval=0, restarts=3, actions=[('applyExit', 4, 1)], events=[('applyexit', 1, 1)], display='apply')
    add('keep_exit', activate('settings_keep'), child=False, interval=0, restarts=3,
        actions=[('confirm', 2, 1)], display='keep', exit_event=True, shot='keep-return')
    add('reopen_kept', ['openq4_system open'], vsync=0, interval=0, restarts=3,
        opening=True, actions=begin, events=onactivate, shot='reopened')
    add('finished', activate('settings_back'), child=False, interval=0, restarts=3,
        events=[('onback', 1, 0)], dismiss=True)
    return rows


def script():
    lines = ['openq4_assertMenuActivation 10000', 'wait 3']
    for stage in stages():
        lines += [f'echo {STAGE_MARKER} {stage["name"]}'] + stage['commands']
        lines += ['wait 12', 'openq4_system report']
        if stage['child']:
            lines += [f'openq4_guiGet "{field}"' for field in FIELDS]
            lines += [f'openq4_retainedGui widget "{control}"' for control in CONTROLS]
            lines += ['openq4_retainedGui report']
        lines += ['rendererDisplayProbe report']
        if stage['shot']:
            lines += [f'screenshot "screenshots/system-exit-{stage["shot"]}.tga"']
    return '\n'.join(lines + ['gfxInfo', f'echo {COMPLETE}', 'quit']) + '\n'


def source_contract(runtime):
    source = ROOT / 'content/baseoq4/pak0' / PAGE
    raw, binding = page.staged_source(runtime)
    if not source.is_file() or source.read_bytes() != raw:
        raise ValueError('Production SYSTEM source must exactly match its effective staged source')
    model = json.loads('\n'.join(line for line in raw.decode('utf-8').splitlines()
                                 if not line.lstrip().startswith('//')))
    if model.get('id') != 'openq4.system': raise ValueError('Unexpected SYSTEM document identity')
    nodes, pending = {}, [model['root']]
    while pending:
        node = pending.pop()
        if node['id'] in nodes: raise ValueError('Duplicate SYSTEM node')
        nodes[node['id']] = node
        pending.extend(node.get('children', []))
    expected = {'settings_back':('event','onBack'), 'discard_changes':('event','discard'),
                'discard_keep_editing':('event','continueEditing'),
                'discard_apply_changes':('event','applyExit'),
                'settings_keep':('action','keep'), 'settings_revert':('action','revert')}
    for name,(kind,target) in expected.items():
        if nodes.get(name,{}).get('control',{}).get(kind) != target:
            raise ValueError(f'Production exit control contract changed: {name}')
    slider = nodes.get('settings_brightness',{}).get('control',{})
    if any(slider.get(key) != value for key,value in dict(role='slider',minimum=.5,maximum=2,step=.1).items()):
        raise ValueError('Production brightness tick contract changed')
    choice = nodes.get('settings_vsync',{}).get('control',{})
    if choice.get('role') != 'choice' or [option.get('value') for option in choice.get('options',[])] != [0,1]:
        raise ValueError('Production VSync choice contract changed')
    aliases = model.get('aliases',{})
    for field in FIELDS:
        if field not in aliases: raise ValueError(f'Missing readback alias: {field}')
    for alias,key in {'draftBrightness':'draft.r_brightness','baselineBrightness':'baseline.r_brightness',
                      'draftVSync':'draft.r_swapInterval','baselineVSync':'baseline.r_swapInterval'}.items():
        variable = aliases[alias].get('variable')
        if model.get('presentationVariables',{}).get(variable,{}).get('value') != {'state':'settings.'+key}:
            raise ValueError(f'Production service alias changed: {alias}')
    actions = model.get('actions',{})
    for name,operation in [('applyExit','applyExit'),('cancel','cancel'),('keep','confirm'),('revert','revert')]:
        if actions.get(name,{}).get('operation') != 'settings.system.'+operation:
            raise ValueError(f'Production service operation changed: {name}')
    # Bind the script to the actual production event programs, including the
    # deliberate absence of a direct dismissal in Apply and Exit.
    events=model.get('events',{})
    for name,wanted in [('discard',[{'op':'action','action':'cancel'},
                                   {'op':'setState','values':{'page.discardVisible':False}},
                                   {'op':'action','action':'dismiss'}]),
                        ('applyExit',[{'op':'setState','values':{'page.discardVisible':False}},
                                      {'op':'action','action':'applyExit'}])]:
        program=events.get(name,[])
        if len(program)!=1 or program[0].get('op')!='if' or program[0].get('then')!=wanted:
            raise ValueError(f'Production exit event contract changed: {name}')
    return {'document':model['id'], 'path':str(source), 'sha256':digest(source), 'staged':binding,
            'normal_filesystem_path':PAGE, 'replacement_acceptance':False}


PATTERNS = {
    'action': r'UI_SETTINGS operation=settings.system\.(\w+) result=(\d+) phase=(\d+) owner=(\d+) dirty=([01])',
    'journal': r'UI_SETTINGS_JOURNAL state=(pending|confirmed) durable=([01])',
    'device': r'UI_SETTINGS_DEVICE restore=([01]) epoch=(\d+) generation=(\d+) submitted=(\d+) presented=(\d+) failures=(\d+) width=(\d+) height=(\d+)',
    'stage': r'UI_SETTINGS_DISPLAY stage=(\d+) owner=(\d+) request=(\d+) result=(\d+) blocked=([01]) detail=(.*)',
    'view': r'UI_SETTINGS_VIEW owner=(\d+) request=(\d+) epoch=(\d+) generation=(\d+) submitted=(\d+) presented=(\d+) failures=(\d+)',
    'present': r'UI_SETTINGS_PRESENT owner=(\d+) request=(\d+) epoch=(\d+) generation=(\d+) submitted=(\d+) presented=(\d+) failures=(\d+)',
    'exit': r'UI_SETTINGS_EXIT owner=(\d+) request=(\d+) event=(armed|ready|consumed|canceled)',
    'adapter_exit': r'RETAINED_GUI_EXIT path=(\S+) owner=(\d+) source=applyExit',
    'event': r'RETAINED_GUI_EVENT name=(\w+) actions=(\d+) writes=(\d+)',
    'operation': r'RETAINED_GUI_OPERATION (focus|menu) passed',
}
ROUTE_FIELDS = set('enabled active parent child guiTest menu map multiplayer menuSound canReturn'.split())
WIDGET_FIELDS = set('id role type accepted pending proposed rejected token popup firstVisible'.split())
REPORT_FIELDS = set('path focus revision active brightness shadows contexts'.split())
DISPLAY_FIELDS = set(('operation result observed epoch generation ready window available outcome submitted presented '
                      'failures native restart logical pixel display position hidden fullscreen maximized samples interval valid').split())


def uint(value, *, positive=False):
    if not isinstance(value,str) or not re.fullmatch(r'\d{1,20}',value): return None
    result = int(value)
    return result if (1 if positive else 0) <= result < 2**64 else None


def qualify(log, mode, renderer):
    errors, records, activation, completion = [], [], [], []
    current = None
    for line in log.splitlines():
        stripped = line.strip()
        if STAGE_MARKER in line:
            match = re.fullmatch(STAGE_MARKER+r' ([a-z_]+)', stripped)
            if not match: errors.append('Malformed stage marker'); continue
            current = {'name':match[1], 'lines':[]}; records.append(current)
        elif COMPLETE in line:
            completion.append(stripped)
        elif line.startswith('OPENQ4_MENU_ACTIVATION '):
            activation.append(line)
            if records: errors.append('Late normal menu activation')
        elif line.startswith(('OPENQ4_SYSTEM ', 'GUI_VALUE ', 'RETAINED_GUI', 'UI_SETTINGS', 'DISPLAY_PROBE')):
            if current is None or completion: errors.append('Observation outside the fixed stage interval')
            else: current['lines'].append(line)
        if any(word in line for word in ('ERROR:', 'FATAL:', 'openq4_guiGet: unknown', 'usage: openq4_',
                                         'openq4_retainedGui: requires')):
            errors.append('Engine error or unsupported semantic command')
    match = re.fullmatch(r'OPENQ4_MENU_ACTIVATION PASS elapsed=(\d{1,5})ms limit=10000ms',activation[0]) if len(activation)==1 else None
    if not match or int(match[1]) > 10000: errors.append('Missing bounded menu activation after gameplay')
    if completion != [COMPLETE]: errors.append('Missing, malformed or replayed completion')
    expected = stages()
    if [row['name'] for row in records] != [row['name'] for row in expected]: errors.append('Stage order/count differs')
    owners, owner, display_reports, requests, devices = [], None, [], [], []
    for record, stage in zip(records,expected):
        at, lines = stage['name'], record['lines']
        def fail(detail): errors.append(at+': '+detail)
        traces = {key:[] for key in PATTERNS}
        routes, reads, widgets, reports, displays, sequence, loads, resources, route_ops, dismisses = [],[],[],[],[],[],[],[],[],[]
        for i,line in enumerate(lines):
            matched = False
            for kind,pattern in PATTERNS.items():
                match = re.fullmatch(pattern,line)
                if match:
                    traces[kind].append((i,*match.groups())); matched=True; break
            if matched: continue
            if line.startswith('OPENQ4_SYSTEM operation='): route_ops.append(line)
            elif line.startswith('OPENQ4_SYSTEM '):
                routes.append((i,page._fields(line,'OPENQ4_SYSTEM ',ROUTE_FIELDS))); sequence.append('route')
            elif line.startswith('GUI_VALUE '):
                reads.append(line[10:].split('=',1)); sequence.append('read')
            elif line.startswith('RETAINED_GUI_WIDGET '):
                widgets.append(page._fields(line,'RETAINED_GUI_WIDGET ',WIDGET_FIELDS)); sequence.append('widget')
            elif line.startswith('RETAINED_GUI path='):
                reports.append(page._fields(line,'RETAINED_GUI ',REPORT_FIELDS)); sequence.append('report')
            elif line.startswith('DISPLAY_PROBE '):
                displays.append(page._fields(line,'DISPLAY_PROBE ',DISPLAY_FIELDS)); sequence.append('display')
            elif line.startswith('RETAINED_GUI_LOADED '): loads.append(line)
            elif line.startswith('RETAINED_GUI_RESOURCE '): resources.append((i,line))
            elif line.startswith('RETAINED_GUI_DISPATCH '):
                dismisses.append(page._fields(line,'RETAINED_GUI_DISPATCH ',set('path operation value brightness shadows close'.split())))
            else: fail('Unexpected or malformed observation: '+line[:150])
        wanted_sequence = ['route']*(2 if stage['opening'] else 1)
        if stage['child']: wanted_sequence += ['read']*len(FIELDS)+['widget']*2+['report']
        if sequence != wanted_sequence+['display']: fail('Settled route/readback/renderer observation order differs')
        if route_ops != (['OPENQ4_SYSTEM operation=open result=1'] if stage['opening'] else []): fail('Session open failed or replayed')
        if loads != (['RETAINED_GUI_LOADED '+PAGE] if stage['opening'] else []): fail('Unexpected page lifetime/load')
        for _,route in routes:
            wanted = dict(enabled='1', active=PAGE if stage['child'] else PARENT,
                          parent=PARENT if stage['child'] else '-', child=str(int(stage['child'])),
                          guiTest='0',menu='1',map='1',multiplayer=str(int(mode=='mp')),menuSound='1',
                          canReturn=str(int(stage['child'] and not stage['values'][1])))
            if route != wanted: fail('Session parent/child/gameplay ownership mismatch')
        if routes:
            settle = routes[-1][0]
            if any(row[0] >= settle for group in traces.values() for row in group): fail('Action or receipt occurred after settled route')
        actions = traces['action']
        if [(op,code,phase,dirty) for _,op,code,phase,_,dirty in actions] != [(op,'0',str(phase),str(dirty)) for op,phase,dirty in stage['actions']]:
            fail('Settings operation result/order/phase differs')
        if stage['opening'] and actions:
            token = uint(actions[0][4],positive=True)
            if token is None or token in owners: fail('Reopened page did not receive a fresh owner')
            owners.append(token); owner=token
        if any(uint(row[4],positive=True) != owner for row in actions): fail('Settings owner changed or is foreign')
        if [row[1:] for row in traces['event']] != [(name,str(count),str(writes)) for name,count,writes in stage['events']]:
            fail('Authored lifecycle/event replay or omission')
        if [row[1] for row in traces['operation']] != [line.split()[1] for line in stage['commands'] if line.startswith('openq4_retainedGui ')]:
            fail('Semantic focus/menu operation failed or replayed')
        if stage['child']:
            if len(reads) != len(FIELDS): fail('Missing service readbacks')
            for row,field,value in zip(reads,FIELDS,stage['values']):
                if len(row)!=2 or row[0]!=field: fail('Readback field/order differs'); continue
                if field=='request':
                    if value is None:
                        token=uint(row[1],positive=True)
                        if token is None or (requests and token <= requests[-1]): fail('Invalid or reused confirmation request')
                        requests.append(token or 0)
                    elif row[1] != '': fail('Stale request in editing page')
                elif value is None:
                    if not re.fullmatch(r'\d{1,2}(?:\.\d{1,16})?',row[1]) or not 0<float(row[1])<=15: fail('Invalid confirmation countdown')
                elif not page._number(row[1],value): fail('Service-backed draft/baseline/readiness mismatch: '+field)
            for index,widget in enumerate(widgets):
                if not widget or index>=2: fail('Malformed widget'); continue
                fixed=dict(id=CONTROLS[index],role=str((2,3)[index]),type='0',pending='0',proposed='0',rejected='0',token='0',popup='0',firstVisible='0')
                if any(widget[key]!=value for key,value in fixed.items()) or not page._number(widget['accepted'],stage['values'][6+index*2]): fail('Widget accepted/pending state differs')
            for report in reports:
                if not report or any(report[key]!=value for key,value in dict(path=PAGE,active='1',contexts='1',shadows='1').items()) or uint(report['revision']) is None or not page._number(report['brightness'],stage['live']): fail('Rendered owner/live brightness differs')
                if report and stage['values'][5] and report['focus']!='discard_keep_editing': fail('Dirty modal lacks its safe initial focus')
                if report and stage['values'][11] and report['focus']!='settings_revert': fail('Confirmation modal lacks its safe initial focus')
                if report and at=='continued' and report['focus']!='settings_back': fail('Continue failed to restore the previous page focus')
        if len(dismisses) != int(stage['dismiss']): fail('Unexpected or duplicate direct dismissal')
        for row in dismisses:
            if not row or any(row[key]!=value for key,value in dict(path=PAGE,operation='ui.dismiss',value='-',shadows='1',close='1').items()) or not page._number(row['brightness'],stage['live']): fail('Invalid dismissal/live brightness')
        if len(displays)!=1 or displays[0] is None: fail('Missing actual renderer report')
        else:
            row=displays[0]
            fixed=dict(operation='report',result='1',observed='1',ready='1',window='1',available='1',outcome='2',
                       failures='0',native='0',logical='1280x720',pixel='1280x720',hidden='1',fullscreen='0',
                       maximized='0',samples='0',interval=str(stage['interval']),valid='7' if renderer=='vulkan' else '3')
            if any(row[key]!=value for key,value in fixed.items()): fail('Actual renderer/window/interval success tuple differs')
            numeric={key:uint(row[key],positive=key in ('epoch','generation','display')) for key in ('epoch','generation','display','submitted','presented','restart')}
            if None in numeric.values() or (numeric['submitted'] is not None and numeric['presented'] is not None and numeric['submitted']<numeric['presented']): fail('Invalid renderer counter/identity')
            elif display_reports:
                first,prior=display_reports[0],display_reports[-1]
                if numeric['epoch']!=first['epoch'] or numeric['display']!=first['display'] or numeric['restart']!=first['restart']+stage['restarts'] or numeric['submitted']<=prior['submitted'] or numeric['presented']<=prior['presented'] or numeric['generation']<prior['generation']:
                    fail('Renderer drift, replayed present, or restart count differs')
                if stage['display'] in ('apply','restore') and numeric['generation']<=prior['generation']: fail('Device generation did not advance for restart')
                if stage['display'] not in ('apply','restore') and numeric['generation']!=prior['generation']: fail('Unexpected device restart')
            if not re.fullmatch(r'-?\d{1,10},-?\d{1,10}',row['position']): fail('Invalid observed window placement')
            if None not in numeric.values(): display_reports.append(numeric)
        display=stage['display']
        journal = [(row[1],row[2]) for row in traces['journal']]
        if journal != ([('pending','1')] if display=='apply' else [('confirmed','1')] if display=='keep' else []): fail('Durable journal ordering/count differs')
        if [row[1] for row in traces['stage']] != (['2','3'] if display=='apply' else ['6','0'] if display=='restore' else ['0'] if display=='keep' else []): fail('Display lifecycle stages differ')
        for _,code,token,request,result,blocked,detail in traces['stage']:
            if uint(token,positive=True)!=owner or result!='0' or blocked!=str(int(code!='0')) or detail or (request!='0' if code=='0' else uint(request,positive=True) is None): fail('Display lifecycle owner/result/block/request differs')
        if len(traces['device']) != int(display in ('apply','restore')): fail('Device restart count differs')
        if resources != ([(resources[0][0],f'RETAINED_GUI_RESOURCE path={PAGE} event=restored')] if resources and display in ('apply','restore') else []): fail('Resource restore failed, omitted or replayed')
        if display in ('apply','restore') and len(resources)!=1: fail('Missing owning view resource restore')
        if traces['device']:
            row=traces['device'][0]
            values=[uint(value) for value in row[1:]]
            if None in values or values[0]!=int(display=='restore') or values[5:]!=[0,1280,720] or not values[1] or not values[2] or values[3]<values[4]: fail('Actual display device result differs')
            else:
                devices.append(values)
                if display_reports and any(display_reports[-1][key]!=value for key,value in zip(('epoch','generation'),values[1:3])): fail('Device/report identity mismatch')
                if display_reports and any(value>display_reports[-1][key] for key,value in zip(('submitted','presented'),values[3:5])): fail('Device counters exceed the settled report')
                if len(display_reports)>1 and any(value<display_reports[-2][key] for key,value in zip(('submitted','presented'),values[3:5])): fail('Device counters precede the prior settled report')
                if display=='apply' and traces['stage'] and traces['journal'] and resources and not traces['journal'][0][0]<row[0]<traces['stage'][0][0]<resources[0][0]: fail('Device write happened before durable journal or outside lifecycle')
                if display=='restore' and traces['stage'] and resources and not row[0]<traces['stage'][0][0]<resources[0][0]: fail('Restore/resource order differs')
        if (display=='apply' and traces['stage'] and requests and traces['device'] and
            all(uint(value) is not None for value in traces['device'][0][1:])):
            request=requests[-1]
            if any(uint(row[3])!=request for row in traces['stage']): fail('Displayed request differs from display lifecycle')
            views,presents=traces['view'],traces['present']
            if len(views)!=1 or len(presents)!=1: fail('Missing or replayed owning-view/fresh-present witnesses')
            device=[uint(value) for value in traces['device'][0][1:]]
            for view in views:
                values=[uint(value) for value in view[1:]]
                if None in values or values[:4]!=[owner,request,*device[1:3]] or values[-1]!=0 or not traces['stage'][0][0]<view[0]<traces['stage'][-1][0] or values[4]<=device[3] or values[5]<=device[4]: fail('Foreign/stale owning-view receipt')
                if None not in values and display_reports and any(value>display_reports[-1][key] for key,value in zip(('submitted','presented'),values[4:6])): fail('Owning-view counters exceed the settled report')
            if presents and views:
                present=presents[0]; values=[uint(value) for value in present[1:]]; view=[uint(value) for value in views[-1][1:]]
                if None in values or None in view or values[:4]!=view[:4] or values[-1]!=0 or values[4]<=view[4] or values[5]<=view[5] or not views[-1][0]<traces['stage'][-1][0]<present[0]: fail('Confirmation lacks fresh presentation of its exact owning view')
                if None not in values and display_reports and any(value>display_reports[-1][key] for key,value in zip(('submitted','presented'),values[4:6])): fail('Fresh-present counters exceed the settled report')
        elif traces['view'] or traces['present']: fail('Unexpected confirmation view/present witness')
        exits=traces['exit']; adapter=traces['adapter_exit']
        wanted_exit=['ready','consumed'] if stage['exit_event'] else ['canceled'] if display=='restore' else ['armed'] if display=='apply' else []
        if [row[3] for row in exits]!=wanted_exit: fail('Exit intent/receipt canceled, missing or replayed')
        for _,token,request,event in exits:
            wanted_request=0 if at=='immediate_exit' else requests[-1] if requests else None
            if uint(token,positive=True)!=owner or uint(request)!=wanted_request: fail('Exit receipt belongs to stale owner/request')
        if len(adapter)!=int(stage['exit_event']): fail('Adapter exit consumption missing or replayed')
        if stage['exit_event'] and len(adapter)==1 and len(exits)==2:
            if adapter[0][1:]!=(PAGE,str(owner)) or not exits[0][0]<exits[1][0]<adapter[0][0]: fail('Adapter closed without ordered service receipt consumption')
            if display=='keep' and traces['journal'] and traces['stage'] and not traces['journal'][0][0]<exits[0][0]<traces['stage'][0][0]<exits[1][0]: fail('Keep closed before durable confirmation and completed display work')
        if actions and exits:
            if display in ('apply','restore') and exits[0][0]>=actions[0][0]: fail('Exit intent was not armed/canceled before its operation')
            if at=='immediate_exit' and not exits[0][0]<actions[0][0]<exits[-1][0]: fail('Immediate exit was not acknowledged before consuming its receipt')
        record['traces']=traces
    active=re.findall(r'Renderer API: requested=\S+ active=(\S+)',log)
    if not active or any(value!=renderer for value in active): errors.append('Actual renderer differs from requested profile')
    return {'passed':not errors,'errors':errors,'stages':records,'owners':owners,'requests':requests,
            'devices':devices,'renderer_reports':display_reports,'host_input_injection':False,
            'normal_session_route':not errors,'replacement_acceptance':False,
            'full_system_acceptance':False,'visual_review_required':True}


def diagnostics(log):
    return Counter(line.strip() for line in log.splitlines() if 'WARNING:' in line)


def qualify_warnings(log, baseline, mode, renderer):
    """Qualify the one source-reviewed cache warning against actual init events.

    Every other warning retains exact signature and multiplicity comparison.
    Raw differences remain evidence even when this narrow rule explains them.
    """
    log=re.sub(r'\^[0-9]','',log); baseline=re.sub(r'\^[0-9]','',baseline)
    actual, prior=diagnostics(log),diagnostics(baseline)
    expected=prior.copy(); errors=[]
    result={'actual':dict(actual),'baseline':dict(prior),'added':dict(actual-prior),
            'removed':dict(prior-actual),'raw_equal':actual==prior}
    if any(re.search(r'\b(?:ERROR|FATAL):',text) for text in (log,baseline)):
        errors.append('Engine error in actual or reviewed baseline log')
    if (mode,renderer)==('mp','vulkan'):
        signature='WARNING: vertex array range in virtual memory (SLOW)'
        def audit(text, name, count, reason):
            lines=[line.strip() for line in text.splitlines()]
            initial=[i for i,line in enumerate(lines) if line.startswith('Vulkan renderer initialized:')]
            warnings=[i for i,line in enumerate(lines) if line==signature]
            resets=[]
            for i,line in enumerate(lines):
                match=re.fullmatch(r'GPU frame timing reset: generation=(\d+) reason=(.*)',line)
                if match and match[2] in ('renderer init','vid_restart','recoverable vid_restart'):
                    resets.append((i,match[2]))
            if len(initial)!=count or len(warnings)!=count or any(not re.fullmatch(r'Vulkan renderer initialized: .+ \(Vulkan \d+\.\d+\.\d+\)',lines[i]) for i in initial):
                errors.append(name+': successful Vulkan initialization count/format differs')
            following=[next((n for n in range(i+1,len(lines)) if lines[n]),len(lines)) for i in initial]
            if warnings != following:
                errors.append(name+': cache warning must occur exactly once immediately after each initialization')
            if [value for _,value in resets] != ['renderer init']+[reason]*(count-1):
                errors.append(name+': renderer initialization/restart markers differ')
            if len(initial)==count and len(resets)==count:
                if any(not (resets[n][0]<initial[n] and (n==0 or initial[n-1]<resets[n][0])) for n in range(count)):
                    errors.append(name+': restart does not precede its corresponding initialization')
            return lines,initial,resets,{'initializations':len(initial),'warnings':len(warnings),
                'initialization_lines':[i+1 for i in initial],'warning_lines':[i+1 for i in warnings]}
        old,old_inits,_,old_record=audit(baseline,'baseline',2,'vid_restart')
        new,new_inits,resets,new_record=audit(log,'actual',4,'recoverable vid_restart')
        if {old[i] for i in old_inits}!={new[i] for i in new_inits} or len({new[i] for i in new_inits})!=1:
            errors.append('Actual and baseline Vulkan device/version identities differ')
        # The complete semantic oracle independently proves the three successful
        # typed restarts, module generations, actual settings, and presentation.
        if not qualify(log,mode,renderer)['passed']:
            errors.append('Actual exit lifecycle/restart witnesses failed')
        if len(new_inits)==4 and len(resets)==4:
            stage_names=('display_confirm','display_reverted','keep_confirm')
            stage_positions=[i for i,line in enumerate(new) if line.startswith(STAGE_MARKER+' ')]
            opens=[i for i,line in enumerate(new) if line==STAGE_MARKER+' open']
            if len(opens)!=1 or new_inits[0]>=opens[0]: errors.append('Initial device was not created before the SYSTEM flow')
            for n,name in enumerate(stage_names,1):
                positions=[i for i,line in enumerate(new) if line==STAGE_MARKER+' '+name]
                if len(positions)!=1: errors.append('Missing unique restart stage: '+name); continue
                start=positions[0]; end=next((i for i in stage_positions if i>start),len(new))
                devices=[i for i in range(start,end) if new[i].startswith('UI_SETTINGS_DEVICE ')]
                if len(devices)!=1 or not start<resets[n][0]<new_inits[n]<devices[0]<end:
                    errors.append(name+': cache initialization is outside its successful device restart')
        expected[signature]=4
        result['cache_initialization_rule']={'signature':signature,'source':'src/renderer/VertexCache.cpp: idVertexCache::Init; src/renderer/RenderSystem_init.cpp: R_InitRendererDevice',
            'baseline':old_record,'actual':new_record,
            'explanation':'Reviewed baseline has initial device + one vid_restart; this fixed exit flow has initial device + three recoverable restarts. Require one adjacent cache warning per successful initialization.'}
    elif (mode,renderer)==('sp','gl'):
        expected=Counter()
        if prior: errors.append('SP/GL requires an empty warning baseline')
    else: errors.append('Unsupported warning qualification profile')
    if actual!=expected: errors.append('Warning signatures or exact expected multiplicities differ')
    result.update(expected=dict(expected),unexplained_added=dict(actual-expected),
                  unexplained_removed=dict(expected-actual),errors=errors,passed=not errors)
    return result


def binary_hashes(runtime):
    executable=runtime/('openQ4-client_x64.exe' if os.name=='nt' else 'openQ4-client_x64')
    paths={executable,*runtime.glob('renderer-*'),*(runtime/'baseoq4').glob('game-*')}
    paths.update(path for path in runtime.iterdir() if path.suffix.lower() in ('.dll','.so','.dylib') or '.so.' in path.name)
    return {str(path.relative_to(runtime)):digest(path) for path in sorted(paths) if path.is_file()}


def launch_command(args, output):
    profiles=json.loads((ROOT/'.vscode/launch.json').read_text(encoding='utf-8-sig'))['configurations']
    prefix='(SP) airdefense1 ' if args.mode=='sp' else '(MP) q4dm1 '
    profile=next(row for row in profiles if row['name'].startswith(prefix) and row['name'].endswith('GL'))
    values=dict(fs_basepath=str(args.assets.resolve()),fs_savepath=str(output/'save'),fs_devpath=str(output/'save'),
        fs_game='baseoq4',logFile='2',logFileName='logs/openq4.log',r_fullscreen='0',r_fullscreenDesktop='0',
        r_borderless='0',r_borderlessDefaultMigrated='1',r_hiddenWindow='1',r_windowWidth='1280',r_windowHeight='720',
        r_mode='-1',r_customWidth='1280',r_customHeight='720',r_renderApi=args.renderer,r_rendererSharedGui='1',
        r_rendererSharedInWorldGui='0',r_multiSamples='0',r_swapInterval='1',r_gamma='1',r_brightness='1',r_shadows='1',
        in_mouse='0',in_joystick='0',in_joystickRumble='0',g_autoScreenshot='0',g_autoSkipCinematics='1',
        g_autoExecAfterMapLoad='system-exit.cfg',g_autoExecAfterMapLoadDelayMs='3000',com_skipLoadingContinue='1',
        com_loadingContinueAutoAdvance='1',com_maxfps='60',ui_autoJoin='1' if args.mode=='mp' else '0',
        ui_retainedScale='1',ui_retainedDensity=str(args.density),ui_retainedTrace='1',ui_retainedSystem='1',ui_retainedReducedMotion='0')
    executable=args.runtime.resolve()/('openQ4-client_x64.exe' if os.name=='nt' else 'openQ4-client_x64')
    command=[str(executable)]
    for key,value in values.items(): command+=['+set',key,value]
    original,index=profile['args'],0
    while index<len(original):
        count=3 if original[index].lower() in ('+set','+seta') and index+2<len(original) else 1
        if count==1 or original[index+1].lower() not in {key.lower() for key in values}:
            command += [value.replace('${workspaceFolder}',str(ROOT)) for value in original[index:index+count]]
        index+=count
    return command,profile['name']


def persistence(game):
    errors=[]; record={}
    journal,lock,config=game/'ui-settings-recovery.dat',game/'.settings-recovery.lock',game/'openQ4Config.cfg'
    record['journal_absent']=not(journal.exists() or journal.is_symlink())
    record['lease_file_present']=lock.is_file() and not lock.is_symlink()
    if not record['journal_absent'] or not record['lease_file_present']: errors.append('Journal cleanup or permanent lease identity differs')
    try:
        if not config.is_file() or config.is_symlink() or config.stat().st_size>16*1024*1024: raise ValueError('Missing/nonregular/oversized final configuration')
        text=config.read_text(encoding='utf-8'); record['configuration_sha256']=digest(config)
        values={}
        for key,expected in dict(r_brightness=1.1,r_swapInterval=0,r_windowWidth=1280,r_windowHeight=720,r_fullscreen=0,r_multiSamples=0).items():
            matches=re.findall(r'^seta '+key+r' "([^"\r\n]*)"\r?$',text,re.MULTILINE)
            if len(matches)!=1 or not page._number(matches[0],expected): raise ValueError('Confirmed archive mismatch: '+key)
            values[key]=matches[0]
        record['values']=values
    except (OSError,ValueError) as error: errors.append(str(error))
    record.update(passed=not errors,errors=errors)
    return record


def capture(args):
    output,runtime=args.output.resolve(),args.runtime.resolve()
    if output.exists(): raise ValueError('Use a new output directory to preserve evidence')
    if not output.is_relative_to((ROOT/'.tmp').resolve()): raise ValueError('Evidence must use a private repository .tmp directory')
    if (args.mode,args.renderer) not in (('sp','gl'),('mp','vulkan')): raise ValueError('Qualified profiles are SP/GL and MP/Vulkan')
    if args.density not in (1.25,2.0): raise ValueError('Density must be 1.25 or 2')
    source=source_contract(runtime); binaries=binary_hashes(runtime)
    command,profile=launch_command(args,output)
    if not Path(command[0]).is_file() or not (args.assets/'q4base').is_dir(): raise ValueError('Staged client and installed q4base assets required')
    baseline=args.warning_baseline
    baseline_text=re.sub(r'\^[0-9]','',baseline.read_text(encoding='utf-8',errors='replace')) if baseline else ''
    if args.mode=='mp' and not baseline: raise ValueError('MP requires an explicitly reviewed warning baseline log')
    game=output/'save/baseoq4'; game.mkdir(parents=True)
    cfg=game/'system-exit.cfg'; cfg.write_text(script(),encoding='utf-8')
    metadata=dict(status='running',mode=args.mode,renderer=args.renderer,profile=profile,command=command,cwd=str(runtime),
        source=source,binaries=binaries,script_sha256=digest(cfg),density=args.density,windowed=True,hidden_window=True,
        host_input_injection=False,capture_method='engine screenshot after map gameplay',
        warning_baseline={'path':str(baseline),'sha256':digest(baseline)} if baseline else None)
    tool_paths=(Path(__file__).resolve(),Path(page.__file__).resolve())
    metadata['tool_sources']={str(path):digest(path) for path in tool_paths}
    report=output/'capture.json'
    def save(): report.write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    save(); started=time.monotonic()
    with (output/'process.log').open('w',encoding='utf-8') as process_log:
        options={'env':{**os.environ,'TEMP':str(ROOT/'.tmp'),'TMP':str(ROOT/'.tmp')}}
        if os.name=='nt':
            startup=subprocess.STARTUPINFO(); startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow=0
            options.update(startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
        process=subprocess.Popen(command,cwd=runtime,stdout=process_log,stderr=subprocess.STDOUT,**options)
        print(f'SYSTEM exit {args.mode}/{args.renderer}: PID {process.pid}, hidden/windowed, no host input.',flush=True)
        try: metadata['returncode']=process.wait(timeout=240)
        except subprocess.TimeoutExpired:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
            metadata['status']='timeout'; save(); return 1
    metadata['seconds']=round(time.monotonic()-started,3)
    log_path=game/'logs/openq4.log'
    log=re.sub(r'\^[0-9]','',log_path.read_text(encoding='utf-8',errors='replace')) if log_path.is_file() else ''
    metadata['log_sha256']=digest(log_path) if log_path.is_file() else None
    metadata['process_log_sha256']=digest(output/'process.log')
    metadata['qualification']=qualify(log,args.mode,args.renderer)
    metadata['warnings']=qualify_warnings(log,baseline_text,args.mode,args.renderer)
    metadata['persistence']=persistence(game)
    metadata['screenshots']=[]
    for name in SCREENSHOTS:
        path=game/f'screenshots/system-exit-{name}.tga'; row={'path':str(path.relative_to(output)),'passed':False}
        try:
            if path.is_symlink(): raise ValueError('Screenshot is a link')
            raw=path.read_bytes(); size=page.tga_dimensions(raw)
            if size!=(1280,720): raise ValueError('Incomplete or unexpected engine screenshot')
            # Engine screenshot emits uncompressed pixels. Reject flat output;
            # image composition and text legibility still require visual review.
            channels=raw[16]//8; start=18+raw[0]
            if raw[2]!=2 or len(raw)!=start+1280*720*channels: raise ValueError('Unexpected engine TGA encoding/payload')
            colours={raw[start+i*channels:start+i*channels+3] for i in range(0,1280*720,113)}
            if len(colours)<16: raise ValueError('Flat engine screenshot')
            row.update(passed=True,size=size,sha256=digest(path),sampled_colours=len(colours))
        except (OSError,ValueError) as error: row['error']=str(error)
        metadata['screenshots'].append(row)
    try:
        metadata['source_after']=source_contract(runtime)
        metadata['source_unchanged']=metadata['source_after']==source
        metadata['binaries_after']=binary_hashes(runtime)
        metadata['binaries_unchanged']=metadata['binaries_after']==binaries
        metadata['script_unchanged']=digest(cfg)==metadata['script_sha256']
        metadata['tool_sources_unchanged']={str(path):digest(path) for path in tool_paths}==metadata['tool_sources']
        metadata['warning_baseline_unchanged']=not baseline or digest(baseline)==metadata['warning_baseline']['sha256']
    except (OSError,ValueError) as error:
        metadata.update(source_unchanged=False,binaries_unchanged=False,binding_error=str(error))
    valid=(metadata['returncode']==0 and metadata['qualification']['passed'] and metadata['warnings']['passed'] and
           metadata['persistence']['passed'] and metadata['source_unchanged'] and metadata['binaries_unchanged'] and
           metadata.get('script_unchanged',False) and metadata.get('tool_sources_unchanged',False) and
           metadata.get('warning_baseline_unchanged',False) and
           all(row['passed'] for row in metadata['screenshots']))
    metadata['status']='captured_pending_visual_review' if valid else 'failed'
    save(); print(f'{metadata["status"]}: {report}',flush=True)
    return 0 if valid else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('sp','mp'),required=True)
    parser.add_argument('--renderer',choices=('gl','vulkan'),required=True)
    parser.add_argument('--assets',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,default=ROOT/'.install')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--density',type=float,choices=(1.25,2.0),default=1.25)
    parser.add_argument('--warning-baseline',type=Path)
    args=parser.parse_args()
    try: return capture(args)
    except (OSError,ValueError,StopIteration) as error:
        print(f'SYSTEM exit capture failed: {error}',flush=True); return 1


if __name__=='__main__': raise SystemExit(main())
