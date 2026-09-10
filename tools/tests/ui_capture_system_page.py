#!/usr/bin/env python3
"""Mutation-test the normal SYSTEM capture oracle; never launch a game or SDL."""
import contextlib
import copy
import io
import json
from pathlib import Path
import re
import shlex
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import warnings
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/ui'))
import capture_system_page as capture


def route(stage, mode):
    page = stage['page']
    return ('OPENQ4_SYSTEM enabled=1 active='+(capture.PAGE if page else capture.PARENT)+
            ' parent='+(capture.PARENT if page else '-')+f' child={int(page)} guiTest=0 menu=1 map=1 multiplayer={int(mode=="mp")} menuSound=1 canReturn={int(page and not stage["values"][1])}')


def trace(mode='sp', renderer='gl'):
    lines = ['Renderer API: requested='+renderer+' active='+renderer, 'OPENQ4_MENU_ACTIVATION PASS elapsed=13ms limit=10000ms']
    owner = 1
    for stage in capture.stages():
        lines.append(capture.STAGE_MARKER+' '+stage['name'])
        if stage['opening']:
            owner += 36
            lines += ['RETAINED_GUI_LOADED '+capture.PAGE, 'OPENQ4_SYSTEM operation=open result=1', route(stage,mode)]
        lines += ['RETAINED_GUI_OPERATION '+command.split()[1]+' passed' for command in stage['commands'] if command.startswith('openq4_retainedGui ')]
        # Pin the changed production event shapes independently of stages().
        events = [('apply',1,0)] if stage['name'] in ('applied','extras_applied','extras_restored') else \
                 [('continueediting',1,1)] if stage['name']=='keep_editing' else stage['events']
        lines += [f'RETAINED_GUI_EVENT name={event} actions={actions} writes={writes}' for event,actions,writes in events]
        lines += [f'UI_SETTINGS operation=settings.system.{operation} result=0 phase={0 if operation=="cancel" else 1} owner={owner} dirty={dirty}' for operation,dirty in stage['actions']]
        if not stage['page']:
            lines.append(f'RETAINED_GUI_DISPATCH path={capture.PAGE} operation=ui.dismiss value=- brightness=1.100000 shadows=1 close=1')
        lines += ['RETAINED_GUI_RESOURCE path='+capture.PAGE+' event=restored'] * stage['resets']
        lines.append(route(stage,mode))
        if stage['page']:
            lines += [f'GUI_VALUE {field}={value}' for field,value in zip(capture.FIELDS,stage['values'])]
            values = tuple(stage['values'][index] for index in capture.VALUE_INDICES)
            for i,control in enumerate(capture.CONTROLS):
                lines.append(f'RETAINED_GUI_WIDGET id={control} role={capture.ROLES[i]} type={capture.TYPES[i]} accepted={values[i]} pending=0 proposed=0 rejected=0 token=0 popup={stage["popup"] if i==2 else 0} firstVisible={stage["resolution_first"] if control=="settings_resolution_scale" else 0}')
            lines.append(f'RETAINED_GUI path={capture.PAGE} focus=settings_brightness revision=7 active=1 brightness={stage["live"]} shadows=1 contexts=1')
    lines += [capture.COMPLETE]
    return '\n'.join(lines)+'\n'


def image():
    header = bytearray(18); header[2]=2; header[16]=24
    struct.pack_into('<HH',header,12,1280,720)
    return bytes(header)+b'\x30\x40\x50'*(1280*720)


def package(path, members):
    path.parent.mkdir(parents=True,exist_ok=True)
    with warnings.catch_warnings():
        warnings.simplefilter('ignore',UserWarning)  # Intentional duplicate-member rejection tests.
        with ZipFile(path,'w',compression=ZIP_DEFLATED) as archive:
            for name,raw in members: archive.writestr(name,raw)


def number_contract_mutations(original):
    """Independent canonical-source changes shared by both runner preflights."""
    def node(model,name):
        pending=[model['root']]
        while pending:
            item=pending.pop()
            if item['id']==name:return item
            pending.extend(item.get('children',[]))
        raise AssertionError('missing canonical node '+name)
    def binding(model,name):return next(item for item in model['bindings'] if item['id']==name)
    mutations=[
        ('pending-source-removed',lambda m:m['state'].pop('ui.numberDraftsPending')),
        ('pending-host-owned',lambda m:m['state']['ui.numberDraftsPending'].update(cvar='r_brightness')),
        ('pending-wrong-type',lambda m:m['state']['ui.numberDraftsPending'].update(type='number',initial=0)),
        ('pending-numeric-false',lambda m:m['state']['ui.numberDraftsPending'].update(initial=0)),
        ('message-caller-write',lambda m:m['events']['apply'][0]['then'].append({'op':'setState','values':{'ui.numberDraftMessage':'forged'}})),
        ('apply-guard-removed',lambda m:m['events']['apply'][0].update(condition=True)),
        ('apply-direct-bypass',lambda m:node(m,'settings_apply')['control'].update(action='apply')),
        ('apply-local-write',lambda m:m['events']['apply'][0]['then'].append({'op':'setState','values':{'page.discardVisible':False}})),
        ('apply-enabled-bypass',lambda m:binding(m,'settings_apply.enabled').update(value={'state':'settings.canApply'})),
        ('apply-opaque-while-disabled',lambda m:binding(m,'settings_apply.opacity').update(value=1)),
        ('apply-exit-guard-removed',lambda m:m['events']['applyExit'][0].update(condition=True)),
        ('apply-exit-availability-bypass',lambda m:binding(m,'discard_apply_changes.enabled').update(value=True)),
        ('back-ignores-local-only',lambda m:m['events']['onBack'][0]['else'][0]['then'][0].update(condition={'state':'settings.dirty'})),
        ('back-modal-no-refocus',lambda m:m['events']['onBack'][0].update(then=[{'op':'setState','values':{'page.discardVisible':False}}])),
        ('continue-drops-refocus',lambda m:m['events']['continueEditing'].pop()),
        ('continue-focus-before-hide',lambda m:m['events']['continueEditing'].reverse()),
        ('focus-action-wrong-target',lambda m:m['actions']['focusNumberDraft'].update(operation='ui.dismiss')),
        ('focus-action-arguments',lambda m:m['actions']['focusNumberDraft'].update(arguments={'value':1})),
        ('discard-guard-removed',lambda m:m['events']['discard'][0].update(condition=True)),
        ('discard-closed-local-unavailable',lambda m:m['events']['discard'][0].update(condition={'state':'settings.open'})),
        ('discard-close-before-cancel',lambda m:m['events']['discard'][0]['then'].reverse()),
        ('discard-disabled-closed',lambda m:binding(m,'discard_changes.enabled').update(value={'state':'settings.open'})),
        ('discard-panel-hides-local-only',lambda m:binding(m,'discard-panel.display').update(value='none')),
        ('local-guidance-hides-recovery',lambda m:binding(m,'settings-message.text').update(value={'op':'select','args':[{'state':'ui.numberDraftsPending'},{'state':'ui.numberDraftMessage'},{'state':'settings.message'}]})),
        ('number-baseline-source',lambda m:node(m,'settings_brightness_number')['control'].update(value={'state':'settings.baseline.r_brightness'})),
        ('number-wrong-typed-action',lambda m:node(m,'settings_brightness_number')['control'].update(action='edit.r_forceAmbient')),
        ('number-range-drift',lambda m:node(m,'settings_ambient_number')['control'].update(maximum=2)),
        ('number-quantization',lambda m:node(m,'settings_brightness_number')['control'].update(step=.1)),
        ('number-unbounded',lambda m:node(m,'settings_brightness_number')['control'].update(maxBytes=65536)),
        ('number-no-exponent',lambda m:node(m,'settings_brightness_number')['control'].update(exponent=False)),
        ('number-numeric-boolean',lambda m:node(m,'settings_brightness_number')['control'].update(exponent=1)),
        ('number-unclipped',lambda m:node(m,'settings_brightness_number-viewport')['properties'].update(overflow={'type':'keyword','value':'visible'})),
        ('number-reordered-focus',lambda m:node(m,'settings_brightness_row-controls')['children'].reverse()),
        ('number-no-wrap',lambda m:node(m,'settings_brightness_row-controls')['properties'].update({'flex-wrap':{'type':'keyword','value':'nowrap'}})),
        ('number-nested-role',lambda m:node(m,'settings_brightness_row-controls').update(control={'role':'button'})),
        ('number-availability-bypass',lambda m:binding(m,'settings_brightness_number.enabled').update(value=True)),
    ]
    for name,change in mutations:
        model=copy.deepcopy(original);change(model);yield name,model


class SystemPageOracleTests(unittest.TestCase):
    negative_cases = 0

    def reject(self, log, mode='sp'):
        result = capture.qualify(log,mode)
        self.assertFalse(result['passed'],result)
        self.assertTrue(result['errors'])
        type(self).negative_cases += 1

    def test_reference_and_float_round_trip(self):
        for mode,renderer in (('sp','gl'),('mp','vulkan')):
            result = capture.qualify(trace(mode,renderer),mode)
            self.assertTrue(result['passed'],result['errors'])
            self.assertEqual(len(result['stages']),25)
            self.assertEqual(len(result['service_owners']),29)
            self.assertFalse(result['replacement_acceptance'])
            self.assertFalse(result['full_system_acceptance'])
        self.assertTrue(capture.qualify(trace().replace('=1.1\n','=1.1000000000000001\n'),'sp')['passed'])

    def test_exact_apply_continue_program_counts_and_order(self):
        original=trace();apply='RETAINED_GUI_EVENT name=apply actions=1 writes=0'
        continued='RETAINED_GUI_EVENT name=continueediting actions=1 writes=1'
        self.assertEqual(original.count(apply),3);self.assertEqual(original.count(continued),1)
        for old in (apply,continued):
            for changed in (old.replace('actions=1','actions=0'),old.replace('actions=1','actions=2'),
                            old.replace('writes=0','writes=1') if old==apply else old.replace('writes=1','writes=0'),
                            old+'\n'+old,old.replace('name=','name=wrong_')):
                self.reject(original.replace(old,changed,1))
        lines=original.splitlines();at=lines.index(apply);service=next(i for i in range(at+1,len(lines)) if lines[i].startswith('UI_SETTINGS operation=settings.system.apply '))
        lines[at],lines[service]=lines[service],lines[at];self.reject('\n'.join(lines))

    def test_matched_staged_source_cannot_weaken_local_draft_guards(self):
        raw=(ROOT/'content/baseoq4/pak0'/capture.PAGE).read_text(encoding='utf-8')
        original=json.loads('\n'.join(line for line in raw.splitlines() if not line.lstrip().startswith('//')))
        for name,model in number_contract_mutations(original):
            with self.subTest(name=name),tempfile.TemporaryDirectory(prefix='system-number-contract-',dir=ROOT/'.tmp') as directory:
                root=Path(directory);runtime=root/'.install';data=json.dumps(model).encode('utf-8')
                source=root/'content/baseoq4/pak0'/capture.PAGE;source.parent.mkdir(parents=True);source.write_bytes(data)
                package(runtime/'baseoq4/pak0.pk4',[(capture.PAGE,data)])
                with patch.object(capture,'ROOT',root):
                    with self.assertRaises(ValueError):capture.source_contract(runtime)
                type(self).negative_cases += 1

    def test_every_required_observation_is_required_and_ordered(self):
        original = trace().splitlines()
        for index,line in enumerate(original):
            if line.startswith(('SYSTEM_PAGE_', 'OPENQ4_', 'GUI_VALUE ', 'RETAINED_GUI_', 'RETAINED_GUI ', 'UI_SETTINGS ')):
                with self.subTest(index=index,line=line[:70]): self.reject('\n'.join(original[:index]+original[index+1:]))
        first = original.index('GUI_VALUE open=1')
        changed = original.copy(); changed[first],changed[first+1]=changed[first+1],changed[first]
        self.reject('\n'.join(changed))
        changed = original.copy(); changed.insert(first,changed[first]); self.reject('\n'.join(changed))

    def test_marker_whitespace_and_false_markers(self):
        for marker in (capture.COMPLETE,capture.STAGE_MARKER+' open'):
            self.assertTrue(capture.qualify(trace().replace(marker+'\n',' \t'+marker+' \t\n'),'sp')['passed'])
            for mutation in ('prefix '+marker,marker+' suffix',marker+'\n'+marker):
                self.reject(trace().replace(marker+'\n',mutation+'\n',1))

    def test_route_ownership_and_mode_mutations(self):
        for before,after in [('guiTest=0','guiTest=1'),('map=1','map=0'),('menu=1','menu=0'),('enabled=1','enabled=0'),
                             ('menuSound=1','menuSound=0'),('child=1','child=0'),('multiplayer=0','multiplayer=1'),
                             ('canReturn=0','canReturn=1'),('parent='+capture.PARENT,'parent=-'),
                             ('operation=open result=1','operation=open result=0'),
                             ('elapsed=13ms','elapsed=10001ms'),('elapsed=13ms','elapsed='+('9'*5000)+'ms')]:
            with self.subTest(before=before): self.reject(trace().replace(before,after,1))
        self.reject(trace('mp'),'sp')

    def test_typed_readbacks_and_atomic_widget_fields(self):
        for before,after in [('draftBrightness=1.1','draftBrightness=1.9'),('baselineBrightness=1.1','baselineBrightness=1'),
                             ('draftShadows=0','draftShadows=1'),('discardVisible=1','discardVisible=0'),
                             ('draftPostAA=2','draftPostAA=0'),('GUI_VALUE dirty=1','GUI_VALUE dirty=nan'),
                             ('GUI_VALUE open=1','GUI_VALUE open=true'),('pending=0','pending=1'),('proposed=0','proposed=1.25'),
                             ('rejected=0','rejected=1'),('token=0','token=101'),('popup=1','popup=0'),
                             ('firstVisible=0','firstVisible=1'),('role=2','role=1'),('type=0','type=1'),
                             ('contexts=1','contexts=2'),('active=1 brightness','active=0 brightness'),('shadows=1 contexts','shadows=0 contexts')]:
            with self.subTest(before=before): self.reject(trace().replace(before,after,1))
        self.reject(trace().replace('RETAINED_GUI_WIDGET id=settings_brightness ','RETAINED_GUI_WIDGET id=settings_brightness token=0 ',1))
        self.reject(trace().replace('contexts=1\n','contexts=1 extra=1\n',1))

    def test_no_action_replay_and_stable_owner_across_reload(self):
        for before,after in [('result=0 phase=1','result=3 phase=1'),('owner=37','owner=0'),('owner=37','owner='+('9'*5000)),
                             ('owner=73','owner=37'),('actions=1 writes=1','actions=2 writes=1'),('event=restored','event=failed'),
                             ('RETAINED_GUI_OPERATION focus passed','RETAINED_GUI_OPERATION focus failed'),
                             ('operation=ui.dismiss value=-','operation=settings.brightness.set value=1.1')]:
            with self.subTest(before=before): self.reject(trace().replace(before,after,1))
        mark = capture.STAGE_MARKER+' language\n'
        for extra in ['UI_SETTINGS operation=settings.system.begin result=0 phase=1 owner=37 dirty=0',
                      'RETAINED_GUI_EVENT name=onactivate actions=1 writes=1',
                      'RETAINED_GUI_LOADED '+capture.PAGE,
                      'RETAINED_GUI_DISPATCH path='+capture.PAGE+' operation=ui.dismiss value=- brightness=1.1 shadows=1 close=1']:
            self.reject(trace().replace(mark,mark+extra+'\n'))
        self.reject(trace().replace('operation=settings.system.edit result=0 phase=1 owner=37','operation=settings.system.edit result=0 phase=1 owner=38',1))
        lines=trace().splitlines(); action=next(i for i,line in enumerate(lines) if line.startswith('UI_SETTINGS '))
        moved=lines.pop(action); target=next(i for i,line in enumerate(lines) if line.startswith('GUI_VALUE open=')); lines.insert(target+1,moved)
        self.reject('\n'.join(lines))

    def test_all_seven_immediate_draft_apply_and_seed_readbacks(self):
        original=trace()
        rows={row['name']:row for row in capture.stages()}
        for name in ('extras_dirty','extras_applied','extras_restore_draft','extras_restored'):
            stage=rows[name]
            start=original.index(capture.STAGE_MARKER+' '+name+'\n')
            end=original.find(capture.STAGE_MARKER+' ',start+len(capture.STAGE_MARKER))
            body=original[start:end]
            for index,(control,alias,_,role,_) in enumerate(capture.EXTRA_ROWS):
                for prefix,offset in [('draft',12+index*2),('baseline',13+index*2)]:
                    value=stage['values'][offset]
                    old=f'GUI_VALUE {prefix}{alias}={value}\n'
                    self.assertIn(old,body)
                    changed=body.replace(old,f'GUI_VALUE {prefix}{alias}={value+1}\n',1)
                    self.reject(original[:start]+changed+original[end:])
                old=f'RETAINED_GUI_WIDGET id={control} role={role} type={1 if role==1 else 0}'
                changed=body.replace(old,f'RETAINED_GUI_WIDGET id={control} role={role} type={0 if role==1 else 1}',1)
                self.reject(original[:start]+changed+original[end:])
        self.assertEqual(rows['extras_dirty']['actions'],[('edit',1)]*7)
        self.assertEqual(rows['extras_restore_draft']['actions'],[('edit',1)]*7)

    def test_errors_and_unknown_commands_fail(self):
        for error in ('ERROR: retained failure','FATAL: bad device','openq4_guiGet: unknown GUI variable',
                      'openq4_retainedGui: requires an active GUI','usage: openq4_system invalid'):
            self.reject(trace()+error+'\n')

    def test_script_is_fixed_and_semantic_only(self):
        commands = [shlex.split(line) for line in capture.script().splitlines()]
        allowed = {'openq4_assertMenuActivation','wait','echo','openq4_system','openq4_guiGet','openq4_retainedGui','reloadLanguage','vid_restart','screenshot','gfxInfo','quit'}
        self.assertEqual(set(row[0] for row in commands),allowed)
        self.assertEqual(commands[0],['openq4_assertMenuActivation','10000'])
        self.assertEqual(commands[-2:], [['echo',capture.COMPLETE],['quit']])
        self.assertNotIn('testGUI',capture.script()); self.assertNotIn('ui_retainedPreview',capture.script())
        self.assertEqual([row for row in commands if row[0]=='vid_restart'],[['vid_restart','windowed']])
        self.assertEqual([row for row in commands if row[0]=='openq4_system' and row[1]=='open'],[['openq4_system','open']]*5)
        for row in commands:
            self.assertNotIn(';',' '.join(row))
            if row[0]=='wait': self.assertTrue(1<=int(row[1])<=60)
            if row[0]=='openq4_retainedGui': self.assertIn(row[1],('focus','menu','widget','report'))
            if row[0]=='screenshot': self.assertRegex(row[1],r'^screenshots/system-[a-z]+\.tga$')

    def test_tga_payload_validation(self):
        good=image(); self.assertEqual(capture.tga_dimensions(good),(1280,720))
        for raw in (good[:18],good[:-1],good[:2]+b'\x01'+good[3:],good[:16]+b'\x08'+good[17:]):
            self.assertIsNone(capture.tga_dimensions(raw))
        header=bytearray(good[:18]); header[2]=10
        packets=(b'\xff\x11\x22\x33')*((1280*720)//128)
        self.assertEqual(capture.tga_dimensions(bytes(header)+packets),(1280,720))
        self.assertIsNone(capture.tga_dimensions(bytes(header)+packets[:-1]))

    def test_packaged_source_precedence_and_exact_member_binding(self):
        with tempfile.TemporaryDirectory(prefix='system-package-order-',dir=ROOT/'.tmp') as directory:
            runtime=Path(directory); game=runtime/'baseoq4'
            for name,raw in [('pak0.pk4',b'zero'),('pak1.pk4',b'one'),('pak001.pk4',b'padded'),('pak01.pk4',b'padded-two')]:
                package(game/name,[(capture.PAGE,raw)])
            raw,binding=capture.staged_source(runtime)
            self.assertEqual(raw,b'one')
            self.assertEqual(binding['packages_low_to_high'],['pak001.pk4','pak01.pk4','pak0.pk4','pak1.pk4'])
            self.assertEqual(binding['effective']['member'],capture.PAGE)
            self.assertEqual(binding['effective']['package_sha256'],capture.digest(game/'pak1.pk4'))
            self.assertEqual(binding['effective']['member_sha256'],capture.hashlib.sha256(b'one').hexdigest())
            self.assertEqual(binding['effective']['member_size'],3)
            package(game/'zzz.pk4',[(capture.PAGE.upper().replace('/','\\'),b'last')])
            self.assertEqual(capture.staged_source(runtime)[0],b'last')
            loose=game/capture.PAGE; loose.parent.mkdir(parents=True); loose.write_bytes(b'loose')
            raw,binding=capture.staged_source(runtime)
            self.assertEqual(raw,b'loose'); self.assertEqual(binding['effective']['kind'],'loose')
            self.assertEqual(len(binding['candidates_low_to_high']),6)

    def test_archive_sort_matches_padded_and_large_engine_number_rules(self):
        names=['zzz.pk4','pak1.pk4','aaa.pk4','pak10.pk4','pak02.pk4','pak.pk4','pak2.pk4','pak001.pk4']
        result=sorted(map(Path,names),key=capture.cmp_to_key(capture._compare_packages))
        self.assertEqual([path.name for path in result],['aaa.pk4','pak001.pk4','pak02.pk4','pak10.pk4','pak1.pk4','pak2.pk4','pak.pk4','zzz.pk4'])
        # The engine stops accumulation once the prefix reaches 1,000,000.
        self.assertLess(capture._compare_packages(Path('pak10000009.pk4'),Path('pak10000010.pk4')),0)

    def test_ambiguous_or_unsupported_package_candidates_fail_closed(self):
        cases=[([(capture.PAGE,b'a'),(capture.PAGE,b'b')],'Duplicate'),
               ([(capture.PAGE,b'a'),(capture.PAGE.upper(),b'b')],'Duplicate'),
               ([(capture.PAGE,b'a'),(capture.PAGE.replace('/','\\'),b'b')],'Duplicate'),
               ([(capture.PAGE.replace('/',':'),b'a')],'Ambiguous'),
               ([(capture.PAGE,b'a'),('addon.conf',b'addonDef {}')],'Addon-dependent'),
               ([(capture.PAGE,b'x'*(8*1024*1024+1))],'size')]
        for members,error in cases:
            with self.subTest(error=error,members=[name for name,_ in members]), tempfile.TemporaryDirectory(prefix='system-package-invalid-',dir=ROOT/'.tmp') as directory:
                runtime=Path(directory); package(runtime/'baseoq4/pak0.pk4',members)
                with self.assertRaisesRegex(ValueError,error): capture.staged_source(runtime)
        with tempfile.TemporaryDirectory(prefix='system-package-corrupt-',dir=ROOT/'.tmp') as directory:
            runtime=Path(directory); game=runtime/'baseoq4'; game.mkdir()
            (game/'pak0.pk4').write_bytes(b'not a zip')
            with self.assertRaisesRegex(ValueError,'Cannot verify'): capture.staged_source(runtime)
            archive=game/'pak0.pk4'
            package(archive,[(capture.PAGE+'\x01hidden',b'a')])
            archive.write_bytes(archive.read_bytes().replace(b'\x01hidden',b'\0hidden'))
            with self.assertRaisesRegex(ValueError,'Ambiguous'): capture.staged_source(runtime)

    def test_case_collisions_and_missing_effective_source_fail_closed(self):
        with tempfile.TemporaryDirectory(prefix='system-package-case-',dir=ROOT/'.tmp') as directory:
            runtime=Path(directory); game=runtime/'baseoq4'; game.mkdir()
            with self.assertRaisesRegex(ValueError,'absent'): capture.staged_source(runtime)
            package(game/'pak0.pk4',[('unrelated/file.txt',b'not the page')])
            with self.assertRaisesRegex(ValueError,'absent'): capture.staged_source(runtime)
            original=Path.iterdir
            def duplicate_packages(path):
                return iter([game/'pak0.pk4',game/'PAK0.PK4']) if path==game else original(path)
            with patch.object(Path,'iterdir',duplicate_packages):
                with self.assertRaisesRegex(ValueError,'Ambiguous package'): capture.staged_source(runtime)
            (game/'guis').mkdir()
            def duplicate_loose(path):
                return iter([game/'pak0.pk4',game/'guis',game/'GUIS']) if path==game else original(path)
            with patch.object(Path,'iterdir',duplicate_loose):
                with self.assertRaisesRegex(ValueError,'Ambiguous loose'): capture.staged_source(runtime)

    def test_source_contract_uses_effective_bytes_not_first_matching_package(self):
        production=(ROOT/'content/baseoq4/pak0'/capture.PAGE).read_bytes()
        with tempfile.TemporaryDirectory(prefix='system-effective-source-',dir=ROOT/'.tmp') as directory:
            root=Path(directory); runtime=root/'.install'; game=runtime/'baseoq4'
            source=root/'content/baseoq4/pak0'/capture.PAGE; source.parent.mkdir(parents=True); source.write_bytes(production)
            package(game/'pak0.pk4',[(capture.PAGE,production)])
            package(game/'pak1.pk4',[(capture.PAGE,production+b'\n')])
            with patch.object(capture,'ROOT',root):
                with self.assertRaisesRegex(ValueError,'exactly match'): capture.source_contract(runtime)
                loose=game/capture.PAGE; loose.parent.mkdir(parents=True); loose.write_bytes(production)
                self.assertEqual(capture.source_contract(runtime)['staged']['effective']['kind'],'loose')
                before=capture.source_contract(runtime)
                package(game/'pak1.pk4',[(capture.PAGE,production+b'\n'),('extra.txt',b'repack')])
                after=capture.source_contract(runtime)
                self.assertEqual(before['sha256'],after['sha256']); self.assertNotEqual(before,after)
                loose.write_bytes(production+b'\n')
                with self.assertRaisesRegex(ValueError,'exactly match'): capture.source_contract(runtime)

    def test_mocked_normal_launch_and_source_binding(self):
        production=(ROOT/'content/baseoq4/pak0'/capture.PAGE).read_bytes()
        for mode,renderer,changed in (('sp','gl',False),('mp','vulkan',False),('sp','gl',True)):
            with self.subTest(mode=mode,changed=changed), tempfile.TemporaryDirectory(prefix='system-page-oracle-',dir=ROOT/'.tmp') as directory:
                root=Path(directory); runtime=root/'.install'; runtime.mkdir(); (root/'.tmp').mkdir()
                executable=runtime/('openQ4-client_x64.exe' if capture.os.name=='nt' else 'openQ4-client_x64'); executable.write_bytes(b'never executed')
                path=root/'content/baseoq4/pak0'/capture.PAGE
                path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(production)
                package(runtime/'baseoq4/pak0.pk4',[(capture.PAGE,production)])
                package(runtime/'baseoq4/pak1.pk4',[('unrelated.txt',b'packaged content')])
                (root/'.vscode').mkdir(); gameplay=['+map','game/airdefense1'] if mode=='sp' else ['+spawnServer','mp/q4dm1']
                profile={'name':('(SP) airdefense1 ' if mode=='sp' else '(MP) q4dm1 ')+'test GL',
                         'args':['+set','R_FULLSCREEN','1','+seta','ui_retainedSystem','0','+set','in_mouse','1','+set','ui_autoJoin','0']+gameplay}
                (root/'.vscode/launch.json').write_text(json.dumps({'configurations':[profile]}),encoding='utf-8')
                assets=root/'assets'; (assets/'q4base').mkdir(parents=True)
                output=root/'.tmp/evidence'; args=SimpleNamespace(output=output,runtime=runtime,assets=assets,mode=mode,renderer=renderer,density=None)
                calls=[]
                def launch(command,**kwargs):
                    calls.append((command,kwargs)); game=output/'save/baseoq4'; (game/'logs').mkdir(); (game/'screenshots').mkdir()
                    (game/'logs/openq4.log').write_text(trace(mode,renderer),encoding='utf-8')
                    for name in capture.SCREENSHOTS: (game/f'screenshots/system-{name}.tga').write_bytes(image())
                    if changed: package(runtime/'baseoq4/pak0.pk4',[(capture.PAGE,production),('extra.txt',b'changed during capture')])
                    return SimpleNamespace(pid=1234,wait=lambda timeout:0)
                with patch.object(capture,'ROOT',root),patch.object(capture.subprocess,'Popen',side_effect=launch),contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(capture.capture(args),int(changed))
                    with self.assertRaisesRegex(ValueError,'new output'): capture.capture(args)
                self.assertEqual(len(calls),1); command,kwargs=calls[0]
                values={command[i+1].lower():command[i+2] for i in range(len(command)-2) if command[i].lower() in ('+set','+seta')}
                for key,value in {'r_fullscreen':'0','r_fullscreenDesktop':'0','r_borderless':'0','r_hiddenWindow':'1',
                                  'in_mouse':'0','in_joystick':'0','in_joystickRumble':'0','ui_retainedSystem':'1',
                                  'r_renderApi':renderer,'ui_autoJoin':'1' if mode=='mp' else '0',
                                  'g_autoExecAfterMapLoadDelayMs':'3000','g_autoExecAfterMapLoad':'system-page.cfg'}.items():
                    self.assertEqual(values[key.lower()],value)
                for _,_,key,_,seed in capture.EXTRA_ROWS: self.assertEqual(values[key.lower()],str(seed))
                self.assertEqual(command[-2:],gameplay); self.assertEqual(kwargs['cwd'],runtime)
                self.assertEqual(kwargs['env']['TEMP'],str(root/'.tmp'))
                self.assertFalse((output/'save/baseoq4'/capture.PAGE).exists())
                self.assertEqual((output/'save/baseoq4/system-page.cfg').read_text(encoding='utf-8'),capture.script())
                metadata=json.loads((output/'capture.json').read_text(encoding='utf-8'))
                self.assertEqual(metadata['status'],'failed' if changed else 'captured_pending_visual_and_warning_review')
                self.assertEqual(len(metadata['screenshots']),9); self.assertEqual(metadata['source_unchanged'],not changed)
                self.assertEqual(metadata['source']['staged']['effective']['member'],capture.PAGE)
                self.assertEqual(Path(metadata['source']['staged']['effective']['package']).name,'pak0.pk4')
                self.assertEqual(metadata['source']['sha256'],metadata['source_after']['sha256'])
                self.assertFalse(metadata['qualification']['replacement_acceptance'])
                loose=runtime/'baseoq4'/capture.PAGE; loose.parent.mkdir(parents=True); loose.write_bytes(production+b'\n')
                with patch.object(capture,'ROOT',root):
                    with self.assertRaisesRegex(ValueError,'exactly match'): capture.source_contract(runtime)


if __name__=='__main__':
    (ROOT/'.tmp').mkdir(exist_ok=True)
    result=unittest.main(exit=False).result
    if result.wasSuccessful(): print(f'SYSTEM page capture oracle: {SystemPageOracleTests.negative_cases} negative mutations rejected; no game or renderer launched')
    raise SystemExit(0 if result.wasSuccessful() else 1)
