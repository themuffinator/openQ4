#!/usr/bin/env python3
"""Adversarial SYSTEM exit capture oracle tests; every process is mocked."""
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
from zipfile import ZipFile, ZIP_DEFLATED

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/ui'))
import capture_system_exit as capture
from ui_capture_system_page import number_contract_mutations

VK_INIT='Vulkan renderer initialized: Fixture GPU (Vulkan 1.4.325)'
CACHE_WARNING='WARNING: vertex array range in virtual memory (SLOW)'
STOCK_WARNING='WARNING: reviewed stock warning'


def warning_baseline():
    return '\n'.join(('GPU frame timing reset: generation=1 reason=renderer init',VK_INIT,CACHE_WARNING,
        STOCK_WARNING,'GPU frame timing reset: generation=2 reason=vid_restart',VK_INIT,CACHE_WARNING))+'\n'


def route(stage,mode):
    child=stage['child']
    return (f'OPENQ4_SYSTEM enabled=1 active={capture.PAGE if child else capture.PARENT} '
            f'parent={capture.PARENT if child else "-"} child={int(child)} guiTest=0 menu=1 map=1 '
            f'multiplayer={int(mode=="mp")} menuSound=1 canReturn={int(child and not stage["values"][1])}')


def trace(mode='sp',renderer='gl'):
    lines=[f'Renderer API: requested={renderer} active={renderer}',
           'OPENQ4_MENU_ACTIVATION PASS elapsed=15ms limit=10000ms']
    if renderer=='vulkan': lines += ['GPU frame timing reset: generation=1 reason=renderer init',VK_INIT,CACHE_WARNING]
    owner,request=0,100
    for index,stage in enumerate(capture.stages()):
        lines += [capture.STAGE_MARKER+' '+stage['name']]
        if stage['opening']:
            owner+=17
            lines += ['RETAINED_GUI_LOADED '+capture.PAGE,'OPENQ4_SYSTEM operation=open result=1',route(stage,mode)]
        generation=10+stage['restarts']; sequence=100+index*100
        if stage['display']=='apply': request+=1
        active_request=request
        if stage['display']=='restore': active_request=request+1
        lines += ['RETAINED_GUI_OPERATION '+command.split()[1]+' passed' for command in stage['commands'] if command.startswith('openq4_retainedGui ')]
        # Pin the production lifecycle spelling independently of the oracle.
        events = [('onActivate',1,1)] if stage['opening'] else \
                 [('continueediting',1,1)] if stage['name']=='continued' else stage['events']
        lines += [f'RETAINED_GUI_EVENT name={name} actions={actions} writes={writes}' for name,actions,writes in events]
        def exit_event(event,token=request): lines.append(f'UI_SETTINGS_EXIT owner={owner} request={token} event={event}')
        if stage['name']=='immediate_exit': exit_event('ready',0)
        if stage['display']=='apply': exit_event('armed')
        if stage['display']=='restore': exit_event('canceled')
        lines += [f'UI_SETTINGS operation=settings.system.{op} result=0 phase={phase} owner={owner} dirty={dirty}' for op,phase,dirty in stage['actions']]
        def display_stage(value,token): lines.append(f'UI_SETTINGS_DISPLAY stage={value} owner={owner} request={token} result=0 blocked={int(value!=0)} detail=')
        if stage['display']=='apply': lines.append('UI_SETTINGS_JOURNAL state=pending durable=1')
        if stage['display'] in ('apply','restore'):
            if renderer=='vulkan': lines += [f'GPU frame timing reset: generation={2+stage["restarts"]} reason=recoverable vid_restart',VK_INIT,CACHE_WARNING]
            lines.append(f'UI_SETTINGS_DEVICE restore={int(stage["display"]=="restore")} epoch=2 generation={generation} submitted={sequence} presented={sequence} failures=0 width=1280 height=720')
            display_stage(2 if stage['display']=='apply' else 6,active_request)
            lines.append(f'RETAINED_GUI_RESOURCE path={capture.PAGE} event=restored')
            if stage['display']=='apply':
                lines.append(f'UI_SETTINGS_VIEW owner={owner} request={request} epoch=2 generation={generation} submitted={sequence+1} presented={sequence+1} failures=0')
                display_stage(3,request)
                lines.append(f'UI_SETTINGS_PRESENT owner={owner} request={request} epoch=2 generation={generation} submitted={sequence+2} presented={sequence+2} failures=0')
            else:
                display_stage(0,0); request=active_request
        if stage['display']=='keep':
            lines.append('UI_SETTINGS_JOURNAL state=confirmed durable=1')
            exit_event('ready'); display_stage(0,0)
        if stage['exit_event']:
            exit_event('consumed',0 if stage['name']=='immediate_exit' else request)
            lines.append(f'RETAINED_GUI_EXIT path={capture.PAGE} owner={owner} source=applyExit')
        if stage['dismiss']:
            lines.append(f'RETAINED_GUI_DISPATCH path={capture.PAGE} operation=ui.dismiss value=- brightness={stage["live"]} shadows=1 close=1')
        lines.append(route(stage,mode))
        if stage['child']:
            for field,value in zip(capture.FIELDS,stage['values']):
                if value is None: value=request if field=='request' else 14.5
                lines.append(f'GUI_VALUE {field}={value}')
            for i,control in enumerate(capture.CONTROLS):
                lines.append(f'RETAINED_GUI_WIDGET id={control} role={(2,3)[i]} type=0 accepted={stage["values"][6+i*2]} pending=0 proposed=0 rejected=0 token=0 popup=0 firstVisible=0')
            focus=('discard_keep_editing' if stage['values'][5] else 'settings_revert' if stage['values'][11]
                   else 'settings_back' if stage['name']=='continued' else 'settings_brightness')
            lines.append(f'RETAINED_GUI path={capture.PAGE} focus={focus} revision=7 active=1 brightness={stage["live"]} shadows=1 contexts=1')
        lines.append(f'DISPLAY_PROBE operation=report result=1 observed=1 epoch=2 generation={generation} ready=1 window=1 available=1 outcome=2 submitted={sequence+12} presented={sequence+12} failures=0 native=0 restart={4+stage["restarts"]} logical=1280x720 pixel=1280x720 display=1 position=24,24 hidden=1 fullscreen=0 maximized=0 samples=0 interval={stage["interval"]} valid={7 if renderer=="vulkan" else 3}')
    return '\n'.join(lines+[capture.COMPLETE])+'\n'


def image():
    header=bytearray(18); header[2]=2; header[16]=24
    struct.pack_into('<HH',header,12,1280,720)
    return bytes(header)+bytes(range(256))*10800


def package(path,raw,extra=False):
    path.parent.mkdir(parents=True,exist_ok=True)
    with ZipFile(path,'w',compression=ZIP_DEFLATED) as archive:
        archive.writestr(capture.PAGE,raw)
        if extra: archive.writestr('unrelated.txt',b'changed package identity')


def configuration():
    return '\n'.join(f'seta {key} "{value}"' for key,value in dict(r_brightness=1.1,r_swapInterval=0,r_windowWidth=1280,r_windowHeight=720,r_fullscreen=0,r_multiSamples=0).items())+'\n'


class ExitOracleTests(unittest.TestCase):
    mutations=0
    def reject(self,log,mode='sp',renderer='gl'):
        result=capture.qualify(log,mode,renderer)
        self.assertFalse(result['passed'],result)
        self.assertTrue(result['errors'])
        type(self).mutations+=1

    def test_reference(self):
        for mode,renderer in [('sp','gl'),('mp','vulkan')]:
            result=capture.qualify(trace(mode,renderer),mode,renderer)
            self.assertTrue(result['passed'],result['errors'])
            self.assertEqual(result['owners'],[17,34,51,68])
            self.assertEqual(result['requests'],[101,103])
            self.assertEqual(len(result['devices']),3)
            self.assertFalse(result['replacement_acceptance'])
            self.assertFalse(result['full_system_acceptance'])

    def test_every_required_observation_missing_or_replayed(self):
        original=trace().splitlines()
        for index,line in enumerate(original):
            if line.startswith(('SYSTEM_EXIT_','OPENQ4_','GUI_VALUE ','RETAINED_GUI','UI_SETTINGS','DISPLAY_PROBE')):
                with self.subTest(index=index,line=line[:80]):
                    self.reject('\n'.join(original[:index]+original[index+1:]))
                    self.reject('\n'.join(original[:index]+[line]+original[index:]))

    def test_exact_production_lifecycle_spelling(self):
        log=trace()
        lifecycle='RETAINED_GUI_EVENT name=onActivate actions=1 writes=1'
        self.assertEqual(log.count(lifecycle),4)
        self.assertTrue(capture.qualify(log,'sp','gl')['passed'])
        for name in ('onactivate','OnActivate','onDeactivate','onInit','unknown'):
            with self.subTest(name=name):
                self.reject(log.replace(lifecycle,lifecycle.replace('onActivate',name),1))

    def test_continue_keeps_one_focus_action_and_one_local_write(self):
        original=trace();continued='RETAINED_GUI_EVENT name=continueediting actions=1 writes=1'
        self.assertEqual(original.count(continued),1)
        for changed in (continued.replace('actions=1','actions=0'),continued.replace('actions=1','actions=2'),
                        continued.replace('writes=1','writes=0'),continued+'\n'+continued,
                        continued.replace('continueediting','continueEditing')):
            self.reject(original.replace(continued,changed,1))

    def test_matched_source_cannot_weaken_numeric_exit_guards(self):
        raw=(ROOT/'content/baseoq4/pak0'/capture.PAGE).read_text(encoding='utf-8')
        original=json.loads('\n'.join(line for line in raw.splitlines() if not line.lstrip().startswith('//')))
        for name,model in number_contract_mutations(original):
            with self.subTest(name=name),tempfile.TemporaryDirectory(prefix='system-number-exit-contract-',dir=ROOT/'.tmp') as directory:
                root=Path(directory);runtime=root/'.install';data=json.dumps(model).encode('utf-8')
                source=root/'content/baseoq4/pak0'/capture.PAGE;source.parent.mkdir(parents=True);source.write_bytes(data)
                package(runtime/'baseoq4/pak0.pk4',data)
                with patch.object(capture,'ROOT',root):
                    with self.assertRaises(ValueError):capture.source_contract(runtime)
                type(self).mutations += 1

    def test_false_markers_malformed_and_outside(self):
        for marker in (capture.COMPLETE,capture.STAGE_MARKER+' open'):
            self.assertTrue(capture.qualify(trace().replace(marker+'\n',' \t'+marker+' \t\n'),'sp','gl')['passed'])
            for replacement in ('prefix '+marker,marker+' suffix'): self.reject(trace().replace(marker,replacement,1))
        for line in ('UI_SETTINGS operation=settings.system.begin result=0 phase=1 owner=17 dirty=0',
                     'GUI_VALUE dirty=0','RETAINED_GUI_EXIT path='+capture.PAGE+' owner=17 source=applyExit'):
            self.reject(line+'\n'+trace()); self.reject(trace()+line+'\n')

    def test_owner_action_and_window_negatives(self):
        mutations=[('owner=17','owner=0'),('owner=34','owner=17'),('owner=51','owner=52'),
            ('owner=17','owner='+('9'*5000)),('request=101','request=100'),('event=armed','event=ready'),
            ('event=canceled','event=ready'),('source=applyExit','source=direct'),('phase=4','phase=1'),
            ('result=0 phase=0','result=3 phase=0'),('actions=2 writes=1','actions=1 writes=1'),
            ('guiTest=0','guiTest=1'),('map=1','map=0'),('parent='+capture.PARENT,'parent=-'),
            ('menuSound=1','menuSound=0'),('multiplayer=0','multiplayer=1'),('canReturn=0','canReturn=1'),
            ('fullscreen=0','fullscreen=1'),('hidden=1','hidden=0'),('samples=0','samples=8'),
            ('interval=0','interval=1'),('failures=0','failures=1'),('outcome=2','outcome=1'),
            ('logical=1280x720','logical=960x540'),('pixel=1280x720','pixel=960x540'),
            ('valid=3','valid=0'),('restart=5','restart=4'),('generation=11','generation=10'),
            ('epoch=2','epoch=3'),('ready=1','ready=0'),('native=0','native=-4'),
            ('operation=report','operation=apply9605400'),('observed=1','observed=0'),
            ('elapsed=15ms','elapsed=10001ms'),('active=gl','active=vulkan')]
        for before,after in mutations:
            with self.subTest(before=before): self.reject(trace().replace(before,after,1))

    def test_readback_and_widget_negatives(self):
        for before,after in [('draftBrightness=1.1','draftBrightness=1.9'),('baselineBrightness=1.1','baselineBrightness=1'),
            ('draftVSync=0','draftVSync=1'),('baselineVSync=1','baselineVSync=0'),('discardVisible=1','discardVisible=0'),
            ('confirmationVisible=1','confirmationVisible=0'),('canConfirm=1','canConfirm=0'),('canRevert=1','canRevert=0'),
            ('remaining=14.5','remaining=nan'),('remaining=14.5','remaining=0'),('remaining=14.5','remaining=16'),
            ('GUI_VALUE request=101','GUI_VALUE request=99'),('GUI_VALUE dirty=1','GUI_VALUE dirty=true'),
            ('pending=0','pending=1'),('rejected=0','rejected=1'),('token=0','token=12'),('popup=0','popup=1'),
            ('role=3','role=2'),('type=0','type=1'),('accepted=1.1','accepted=1'),('contexts=1','contexts=2'),
            ('brightness=1 shadows=1 contexts','brightness=1.1 shadows=1 contexts')]:
            with self.subTest(before=before): self.reject(trace().replace(before,after,1))
        self.reject(trace().replace('GUI_VALUE open=1','GUI_VALUE open=1 extra=0',1))
        self.reject(trace().replace('samples=0 interval=1','samples=0 samples=0 interval=1',1))
        self.reject(trace().replace('focus=discard_keep_editing','focus=-',1))
        self.reject(trace().replace('focus=settings_back','focus=discard_keep_editing',1))
        self.reject(trace().replace('focus=settings_revert','focus=settings_keep',1))

    def test_oversized_numeric_records_fail_without_exception(self):
        import re
        for prefix,keys in [('DISPLAY_PROBE ',('epoch','generation','submitted','presented','restart')),
                            ('UI_SETTINGS_DEVICE ',('epoch','generation','submitted','presented')),
                            ('UI_SETTINGS_VIEW ',('owner','request','epoch','generation','submitted','presented')),
                            ('UI_SETTINGS_PRESENT ',('owner','request','epoch','generation','submitted','presented'))]:
            row=next(line for line in trace().splitlines() if line.startswith(prefix))
            for key in keys:
                with self.subTest(prefix=prefix,key=key):
                    changed=re.sub(r'\b'+key+r'=\d+',key+'='+('9'*5000),row)
                    self.reject(trace().replace(row,changed,1))

    def test_publication_and_presentation_ordering(self):
        original=trace().splitlines()
        def move(before,target,after=False):
            rows=original.copy(); index=next(i for i,line in enumerate(rows) if line.startswith(before))
            row=rows.pop(index); index=next(i for i,line in enumerate(rows) if line.startswith(target))
            rows.insert(index+int(after),row); self.reject('\n'.join(rows))
        move('UI_SETTINGS_JOURNAL state=pending','UI_SETTINGS_DEVICE ',True)
        move('UI_SETTINGS_JOURNAL state=confirmed','UI_SETTINGS_EXIT owner=51 request=103 event=ready',True)
        move('UI_SETTINGS_PRESENT ','UI_SETTINGS_VIEW ')
        move('UI_SETTINGS_EXIT owner=34 request=0 event=ready','UI_SETTINGS_EXIT owner=34 request=0 event=consumed',True)
        move('RETAINED_GUI_EXIT ','UI_SETTINGS_EXIT owner=34 request=0 event=consumed')
        view=next(line for line in original if line.startswith('UI_SETTINGS_VIEW '))
        for before,after in [('submitted=1401','submitted=1400'),('presented=1401','presented=1400')]:
            self.reject(trace().replace(view,view.replace(before,after),1))
        for prefix in ('UI_SETTINGS_VIEW ','UI_SETTINGS_PRESENT '):
            row=next(line for line in original if line.startswith(prefix))
            for key in ('owner','request','epoch','generation','submitted','presented','failures'):
                import re
                changed=re.sub(r'\b'+key+r'=\d+',key+('=1' if key=='failures' else '=0'),row)
                self.reject(trace().replace(row,changed,1))

    def test_witnesses_bounded_by_settled_reports(self):
        original=trace().splitlines()
        start=original.index(capture.STAGE_MARKER+' display_confirm')
        end=original.index(capture.STAGE_MARKER+' display_reverted')
        for keys in (('submitted',),('presented',),('submitted','presented')):
            for offset in (100000,-1000):
                rows=original.copy()
                for i in range(start,end):
                    if rows[i].startswith(('UI_SETTINGS_DEVICE ','UI_SETTINGS_VIEW ','UI_SETTINGS_PRESENT ')):
                        for key in keys:
                            rows[i]=re.sub(r'\b'+key+r'=(\d+)',lambda m:key+'='+str(int(m[1])+offset),rows[i])
                self.reject('\n'.join(rows))
        # A later PRESENT may be no later than the same-stage settled report.
        row=next(line for line in original if line.startswith('UI_SETTINGS_PRESENT '))
        self.reject(trace().replace(row,re.sub(r'\b(submitted|presented)=\d+',r'\g<1>=1413',row),1))

    def test_exact_cache_warning_per_verified_vulkan_initialization(self):
        actual=trace('mp','vulkan')+STOCK_WARNING+'\n'; baseline=warning_baseline()
        report=capture.qualify_warnings(actual,baseline,'mp','vulkan')
        self.assertTrue(report['passed'],report)
        self.assertFalse(report['raw_equal'])
        self.assertEqual(report['added'],{CACHE_WARNING:2}); self.assertEqual(report['removed'],{})
        self.assertEqual(report['expected'],{CACHE_WARNING:4,STOCK_WARNING:1})
        self.assertEqual(report['unexplained_added'],{}); self.assertEqual(report['unexplained_removed'],{})
        self.assertEqual(report['cache_initialization_rule']['baseline']['initializations'],2)
        self.assertEqual(report['cache_initialization_rule']['actual']['initializations'],4)
        changes=[actual+CACHE_WARNING+'\n',actual.replace(CACHE_WARNING+'\n','',1),
            actual.replace(VK_INIT+'\n'+CACHE_WARNING,CACHE_WARNING+'\n'+VK_INIT,1),
            actual.replace(VK_INIT,VK_INIT+'\n'+VK_INIT,1),
            actual.replace(VK_INIT+'\n'+CACHE_WARNING,VK_INIT+'\n'+CACHE_WARNING+'\n'+VK_INIT+'\n'+CACHE_WARNING,1),
            actual.replace('reason=recoverable vid_restart','reason=other',1),
            actual.replace(STOCK_WARNING,STOCK_WARNING+' changed'),actual+STOCK_WARNING+'\n',
            actual.replace(STOCK_WARNING+'\n',''),actual+'WARNING: unknown\n',actual+'ERROR: injected\n',
            actual.replace('restart=5','restart=4',1),actual.replace('UI_SETTINGS_DEVICE restore=0','UI_SETTINGS_DEVICE restore=1',1)]
        # Moving a valid adjacent pair out of its restart must also fail.
        pair=VK_INIT+'\n'+CACHE_WARNING+'\n'
        moved=actual.replace(pair,'',1); changes.append(moved+pair)
        restart='GPU frame timing reset: generation=3 reason=recoverable vid_restart\n'+pair
        moved=actual.replace(restart,'',1)
        changes.append(moved.replace(capture.STAGE_MARKER+' display_confirm',restart+capture.STAGE_MARKER+' display_confirm',1))
        for changed in changes:
            self.assertFalse(capture.qualify_warnings(changed,baseline,'mp','vulkan')['passed'])
        for changed in (baseline+CACHE_WARNING+'\n',baseline.replace(CACHE_WARNING+'\n','',1),
            baseline.replace(pair,CACHE_WARNING+'\n'+VK_INIT+'\n',1),baseline.replace('reason=vid_restart','reason=other'),
            baseline.replace(VK_INIT,VK_INIT+' extra',1),baseline+'ERROR: baseline failure\n',
            baseline.replace(STOCK_WARNING,STOCK_WARNING+' changed')):
            self.assertFalse(capture.qualify_warnings(actual,changed,'mp','vulkan')['passed'])
        self.assertTrue(capture.qualify_warnings(trace(),'','sp','gl')['passed'])
        self.assertFalse(capture.qualify_warnings(trace()+STOCK_WARNING,'','sp','gl')['passed'])
        self.assertFalse(capture.qualify_warnings(trace()+STOCK_WARNING,STOCK_WARNING,'sp','gl')['passed'])

    def test_fixed_semantic_script(self):
        commands=[shlex.split(line) for line in capture.script().splitlines()]
        allowed={'openq4_assertMenuActivation','wait','echo','openq4_system','openq4_retainedGui','openq4_guiGet','rendererDisplayProbe','screenshot','gfxInfo','quit'}
        self.assertEqual({row[0] for row in commands},allowed)
        self.assertEqual(commands[0],['openq4_assertMenuActivation','10000'])
        self.assertEqual(commands[-2:],[['echo',capture.COMPLETE],['quit']])
        self.assertEqual([row for row in commands if row[0]=='rendererDisplayProbe'],[['rendererDisplayProbe','report']]*21)
        for row in commands:
            self.assertNotIn(';',' '.join(row))
            if row[0]=='openq4_retainedGui': self.assertIn(row[1],('focus','menu','widget','report'))
        self.assertNotIn('testGUI',capture.script()); self.assertNotIn('vid_restart',capture.script())
        self.assertEqual(len(capture.SCREENSHOTS),8)
        self.assertEqual([row[1] for row in commands if row[0]=='screenshot'],['screenshots/system-exit-'+name+'.tga' for name in capture.SCREENSHOTS])
        vsync=next(stage for stage in capture.stages() if stage['name']=='display_draft')
        self.assertEqual(vsync['shot'],'vsync-draft')
        continued=next(stage for stage in capture.stages() if stage['name']=='continued')
        self.assertEqual(continued['commands'],capture.key('accept'))
        reverted=next(stage for stage in capture.stages() if stage['name']=='display_reverted')
        self.assertEqual(reverted['commands'],capture.key('accept'))

    def test_real_source_and_mocked_capture_bindings(self):
        production=(ROOT/'content/baseoq4/pak0'/capture.PAGE).read_bytes()
        for mode,renderer,defect in [('sp','gl',None),('mp','vulkan',None)]+[('sp','gl',x) for x in ('source','binary','warning','image','journal','config','exit','script','baseline')]:
            with self.subTest(mode=mode,defect=defect),tempfile.TemporaryDirectory(prefix='system-exit-oracle-',dir=ROOT/'.tmp') as directory:
                root=Path(directory); runtime=root/'.install'; runtime.mkdir(); (root/'.tmp').mkdir()
                executable=runtime/('openQ4-client_x64.exe' if capture.os.name=='nt' else 'openQ4-client_x64'); executable.write_bytes(b'never executed')
                source=root/'content/baseoq4/pak0'/capture.PAGE; source.parent.mkdir(parents=True); source.write_bytes(production)
                package(runtime/'baseoq4/pak0.pk4',production)
                (root/'.vscode').mkdir()
                gameplay=['+map','game/airdefense1'] if mode=='sp' else ['+spawnServer','mp/q4dm1']
                profile=dict(name=('(SP) airdefense1 ' if mode=='sp' else '(MP) q4dm1 ')+'test GL',
                    args=['+set','R_FULLSCREEN','1','+seta','in_mouse','1','+set','r_multiSamples','8','+set','ui_autoJoin','0']+gameplay)
                (root/'.vscode/launch.json').write_text(json.dumps({'configurations':[profile]}),encoding='utf-8')
                assets=root/'assets'; (assets/'q4base').mkdir(parents=True)
                baseline=root/'reviewed.log'; baseline.write_text(warning_baseline() if mode=='mp' else '',encoding='utf-8')
                output=root/'.tmp/evidence'; args=SimpleNamespace(output=output,runtime=runtime,assets=assets,mode=mode,renderer=renderer,density=1.25,warning_baseline=baseline)
                calls=[]
                def launch(command,**kwargs):
                    calls.append((command,kwargs)); game=output/'save/baseoq4'; (game/'logs').mkdir(); (game/'screenshots').mkdir()
                    log=trace(mode,renderer)+(STOCK_WARNING+'\n' if mode=='mp' else '')
                    if defect=='warning': log+='WARNING: new failure\n'
                    (game/'logs/openq4.log').write_text(log,encoding='utf-8')
                    for name in capture.SCREENSHOTS: (game/f'screenshots/system-exit-{name}.tga').write_bytes(image() if defect!='image' else b'bad')
                    (game/'.settings-recovery.lock').write_bytes(b'')
                    (game/'openQ4Config.cfg').write_text(configuration().replace('"1.1"','"1"') if defect=='config' else configuration(),encoding='utf-8')
                    if defect=='journal': (game/'ui-settings-recovery.dat').write_bytes(b'unfinished')
                    if defect=='source': package(runtime/'baseoq4/pak0.pk4',production,True)
                    if defect=='binary': executable.write_bytes(b'replaced during capture')
                    if defect=='script': (game/'system-exit.cfg').write_text('changed\n',encoding='utf-8')
                    if defect=='baseline': baseline.write_text('WARNING: changed approval\n',encoding='utf-8')
                    return SimpleNamespace(pid=77,wait=lambda timeout:1 if defect=='exit' else 0)
                with patch.object(capture,'ROOT',root),patch.object(capture.subprocess,'Popen',side_effect=launch),contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(capture.capture(args),int(defect is not None))
                    with self.assertRaisesRegex(ValueError,'new output'): capture.capture(args)
                self.assertEqual(len(calls),1); command,kwargs=calls[0]
                values={command[i+1].lower():command[i+2] for i in range(len(command)-2) if command[i].lower() in ('+set','+seta')}
                for name,value in dict(r_fullscreen='0',r_fullscreenDesktop='0',r_borderless='0',r_hiddenWindow='1',r_multiSamples='0',r_swapInterval='1',
                    in_mouse='0',in_joystick='0',in_joystickRumble='0',r_renderApi=renderer,ui_retainedSystem='1',
                    ui_autoJoin='1' if mode=='mp' else '0',g_autoExecAfterMapLoad='system-exit.cfg',g_autoExecAfterMapLoadDelayMs='3000').items(): self.assertEqual(values[name.lower()],value)
                self.assertEqual(command[-2:],gameplay); self.assertEqual(kwargs['cwd'],runtime)
                self.assertEqual(kwargs['env']['TEMP'],str(root/'.tmp'))
                metadata=json.loads((output/'capture.json').read_text(encoding='utf-8'))
                self.assertEqual(metadata['status'],'failed' if defect else 'captured_pending_visual_review')
                self.assertEqual(metadata['source_unchanged'],defect!='source')
                self.assertEqual(metadata['binaries_unchanged'],defect!='binary')
                self.assertFalse((output/'save/baseoq4'/capture.PAGE).exists())
                self.assertEqual((output/'save/baseoq4/system-exit.cfg').read_text(encoding='utf-8'),'changed\n' if defect=='script' else capture.script())

    def test_matching_packaged_bytes_do_not_excuse_changed_control_contract(self):
        raw=(ROOT/'content/baseoq4/pak0'/capture.PAGE).read_text(encoding='utf-8')
        original=json.loads('\n'.join(line for line in raw.splitlines() if not line.lstrip().startswith('//')))
        def node(model,name):
            pending=[model['root']]
            while pending:
                value=pending.pop()
                if value['id']==name: return value
                pending.extend(value.get('children',[]))
            raise AssertionError('missing canonical node')
        mutations=[
            lambda model: node(model,'settings_brightness')['control'].update(step=.2),
            lambda model: node(model,'settings_vsync')['control']['options'][0].update(value=2),
            lambda model: node(model,'discard_apply_changes')['control'].update(event='discard'),
            lambda model: model['actions']['cancel'].update(operation='settings.system.apply'),
            lambda model: model['aliases']['draftVSync'].update(variable=model['aliases']['baselineVSync']['variable']),
            lambda model: model['events']['applyExit'][0]['then'].append({'op':'action','action':'dismiss'}),
            lambda model: model['events']['discard'][0]['then'].pop(0),
            lambda model: model['aliases'].pop('confirmationVisible'),
        ]
        for index,mutate in enumerate(mutations):
            with self.subTest(index=index),tempfile.TemporaryDirectory(prefix='system-exit-contract-',dir=ROOT/'.tmp') as directory:
                root=Path(directory); runtime=root/'.install'; model=copy.deepcopy(original); mutate(model)
                data=json.dumps(model).encode('utf-8')
                source=root/'content/baseoq4/pak0'/capture.PAGE; source.parent.mkdir(parents=True); source.write_bytes(data)
                package(runtime/'baseoq4/pak0.pk4',data)
                with patch.object(capture,'ROOT',root):
                    with self.assertRaises(ValueError): capture.source_contract(runtime)


if __name__=='__main__':
    (ROOT/'.tmp').mkdir(exist_ok=True)
    result=unittest.main(exit=False).result
    if result.wasSuccessful(): print(f'SYSTEM exit oracle: {ExitOracleTests.mutations} negative mutations rejected; no game or renderer launched')
    raise SystemExit(0 if result.wasSuccessful() else 1)
