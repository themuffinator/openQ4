#!/usr/bin/env python3
"""Mutation-test the display capture oracle and source-bound command builder.

Synthetic logs and pixel payloads validate the checker, never the engine/device.
No process, input, window or real renderer is started by these tests.
"""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ui'))
import capture_legacy_baseline as capture
import display_settings_probe as probe

FIELDS = ('phase', 'open', 'dirty', 'canApply', 'request', 'confirmationVisible',
          'canConfirm', 'canRevert', 'canRetry', 'remaining', 'draftWidth',
          'draftHeight', 'baselineWidth', 'baselineHeight', 'liveWidth', 'liveHeight')
FIXTURE = ROOT / 'tools/ui/fixtures/display-settings-smoke'


def options(**changes):
    values = dict(width=1280, height=720, brightness=1, gamma=1, language_reload=False, video_restart=False,
                  retained_managed=True, retained_open=False, retained_peer=False, retained_trace=False,
                  profile_frames=0, timeline=None, retained_data=[], display_settings_probe=True,
                  presentation_probe=False, presentation_alias_probe=False, event_program_probe=False,
                  system_settings_probe=False, legacy_export_list=None, shared_gui=False,
                  mode='sp', renderer='gl', ui_scale=1, density=0, reduced_motion=False, timeout=1,
                  retained_document=Path(str(FIXTURE)+'.q4ui'), retained_script=Path(str(FIXTURE)+'.cfg'),
                  retained_resume_script=None)
    values.update(changes)
    return SimpleNamespace(**values)


def synthetic_log(resets=0):
    # Independent observations: no expected_rows/oracle helpers construct data.
    rows = [
        (1,1,0,0,'',0,0,0,0,0,1280,720,1280,720,1280,720),
        (1,1,1,1,'',0,0,0,0,0,960,540,1280,720,1280,720),
        (2,1,1,0,'11',1,1,1,0,14.8,960,540,1280,720,960,540),
        (1,1,0,0,'',0,0,0,0,0,960,540,960,540,960,540),
        (2,1,1,0,'13',1,1,1,0,14.7,1024,576,960,540,1024,576),
        (1,1,0,0,'',0,0,0,0,0,960,540,960,540,960,540),
    ]
    def group(index):
        return [f'GUI_VALUE {field}={value}' for field,value in zip(FIELDS, rows[index])]
    def action(operation,phase,dirty):
        return f'UI_SETTINGS operation=settings.system.{operation} result=0 phase={phase} owner=77 dirty={dirty}'
    def stage(stage,request,blocked):
        return f'UI_SETTINGS_DISPLAY stage={stage} owner=77 request={request} result=0 blocked={blocked} detail='
    def device(restore,generation,count,width,height):
        return f'UI_SETTINGS_DEVICE restore={restore} epoch=7 generation={generation} submitted={count} presented={count} failures=0 width={width} height={height}'
    def witness(kind,request,generation,count):
        return f'UI_SETTINGS_{kind} owner=77 request={request} epoch=7 generation={generation} submitted={count} presented={count} failures=0'
    resource='RETAINED_GUI_RESOURCE path=retained-smoke.q4ui event=restored'
    lines=[action('begin',1,0)]+group(0)+[action('edit',1,1)]+group(1)
    lines += [action('apply',4,1),'UI_SETTINGS_JOURNAL state=pending durable=1',device(0,10,100,960,540),
              stage(2,11,1),resource,witness('VIEW',11,10,101),stage(3,11,1),witness('PRESENT',11,10,102)]+group(2)
    lines += ['Wrote screenshots/ui-display-confirm-a.tga',action('confirm',2,1),
              'UI_SETTINGS_JOURNAL state=confirmed durable=1',stage(0,0,0)]+group(3)
    lines += ['Wrote screenshots/ui-display-keep.tga',action('edit',1,1),action('apply',4,1),
              'UI_SETTINGS_JOURNAL state=pending durable=1',device(0,11,200,1024,576),stage(2,13,1),resource,
              witness('VIEW',13,11,201),stage(3,13,1),witness('PRESENT',13,11,202)]+group(4)
    lines += ['Wrote screenshots/ui-display-confirm-b.tga',action('revert',2,1),device(1,12,300,960,540),stage(6,14,1),resource,stage(0,0,0)]+group(5)
    lines += ['Wrote screenshots/ui-display-revert.tga']
    for _ in range(resets):
        lines += [resource]+group(5)
    return lines


def write_images(game):
    screenshots=game/'screenshots'
    screenshots.mkdir(parents=True,exist_ok=True)
    (game/'.settings-recovery.lock').write_bytes(b'')
    (game/'openQ4Config.cfg').write_bytes(b'seta r_windowWidth "960"\r\nseta r_windowHeight "540"\r\n')
    for name,width,height in (('confirm-a',960,540),('keep',960,540),('confirm-b',1024,576),('revert',960,540)):
        header=bytearray(18);header[2]=2;header[16]=24;struct.pack_into('<HH',header,12,width,height)
        pixels=bytes(component for index in range(257) for component in (index%256,(index*3)%256,(index*7)%256))
        size=width*height*3
        (screenshots/f'ui-display-{name}.tga').write_bytes(header+(pixels*((size+len(pixels)-1)//len(pixels)))[:size])


class DisplayCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary=tempfile.TemporaryDirectory(prefix='display-oracle-',dir=ROOT/'.tmp')
        cls.game=Path(cls.temporary.name)/'game';write_images(cls.game)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def result(self,lines=None,resets=0):
        return probe.evidence('\n'.join(synthetic_log(resets) if lines is None else lines)+'\n',game=self.game,resets=resets)

    def rejects(self,lines,resets=0):
        result=self.result(lines,resets)
        self.assertFalse(result['passed'],result)
        self.assertTrue(result['errors'])
        self.assertFalse(result['device_application_qualified'])
        self.assertFalse(result['replacement_acceptance'])

    def test_valid_independent_sequence(self):
        for resets in (0,1,2):
            result=self.result(resets=resets)
            self.assertTrue(result['passed'],result['errors'])
            self.assertEqual(len(result['readbacks']),96+16*resets)
            self.assertEqual(len(result['operation_trace']),7)
            self.assertEqual(result['expected_final_size'],[960,540])
            self.assertTrue(result['visual_review_required'])
            self.assertFalse(result['replacement_acceptance'])
            self.assertEqual(len(result['owner_present_trace']),2)

    def test_every_ordered_observation_required_once(self):
        original=synthetic_log()
        for index in range(len(original)):
            with self.subTest(index=index,mutation='missing'):
                self.rejects(original[:index]+original[index+1:])
            # Multiple draws before a presentation are legitimate; the oracle
            # must use the final accepted view observation, not a fixed count.
            if not original[index].startswith('UI_SETTINGS_VIEW '):
                with self.subTest(index=index,mutation='duplicate'):
                    self.rejects(original[:index]+[original[index]]+original[index:])
        for index,line in enumerate(original):
            if not line.startswith('GUI_VALUE '):continue
            mutated=original.copy();key,value=line.rsplit('=',1)
            if key.endswith('request'):new='11' if not value else '0'
            elif key.endswith('remaining') and float(value)>0:new='0'
            else:new=str(float(value)+1)
            mutated[index]=key+'='+new
            with self.subTest(index=index,mutation='value'):
                self.rejects(mutated)

    def test_device_success_cannot_be_inferred_from_cvars(self):
        original=synthetic_log()
        self.rejects([line for line in original if not line.startswith('UI_SETTINGS_DEVICE ')])
        for old,new in (('epoch=7','epoch=0'),('generation=11','generation=10'),('failures=0','failures=1'),
                        ('restore=1','restore=0'),('width=1024','width=960'),('height=576','height=540'),
                        ('submitted=200','submitted=99'),('presented=200','presented=99')):
            changed=[line.replace(old,new) if line.startswith('UI_SETTINGS_DEVICE ') else line for line in original]
            with self.subTest(old=old):self.rejects(changed)
        self.rejects(original+['UI_SETTINGS_DEVICE incomplete'])

    def test_witness_requires_owner_and_later_presentation(self):
        original=synthetic_log()
        for prefix in ('UI_SETTINGS_VIEW ','UI_SETTINGS_PRESENT '):
            for old,new in (('owner=77','owner=0'),('request=11','request=12'),('epoch=7','epoch=8'),
                            ('generation=10','generation=9'),('failures=0','failures=1')):
                changed=[line.replace(old,new) if line.startswith(prefix) else line for line in original]
                with self.subTest(prefix=prefix,old=old):self.rejects(changed)
        for old,new in (('submitted=102','submitted=101'),('presented=102','presented=101')):
            self.rejects([line.replace(old,new) if line.startswith('UI_SETTINGS_PRESENT ') else line for line in original])
        changed=original.copy();view=next(i for i,line in enumerate(changed) if line.startswith('UI_SETTINGS_VIEW'))
        present=next(i for i,line in enumerate(changed) if line.startswith('UI_SETTINGS_PRESENT'))
        changed[view],changed[present]=changed[present],changed[view];self.rejects(changed)
        changed=original.copy();changed.insert(present,original[view].replace('submitted=101','submitted=102'))
        self.rejects(changed)
        changed=original.copy();changed.insert(view,original[view])
        self.assertTrue(self.result(changed)['passed'])

    def test_journal_identity_stage_and_warnings(self):
        original=synthetic_log()
        for old,new in (('durable=1','durable=0'),('state=confirmed','state=pending'),('result=0','result=1'),
                        ('blocked=1','blocked=0'),('stage=6','stage=0'),('owner=77','owner=0'),('request=14','request=13')):
            with self.subTest(old=old):self.rejects([line.replace(old,new) for line in original])
        for warning in ('WARNING: retained GUI retained-smoke.q4ui: unexpected',
                        'WARNING: Loading non pre-cached material decl _retained/_ttfatlasx_fonts_english_marine_48',
                        'ERROR: renderer failed','FATAL: cannot initialize'):
            self.rejects(original+[warning])
        self.assertTrue(self.result(original+['WARNING: stock fixture baseline warning'])['passed'])
        self.rejects(synthetic_log(1),resets=0)

    def test_tga_missing_dimensions_truncation_and_flat_output(self):
        path=self.game/'screenshots/ui-display-confirm-b.tga';original=path.read_bytes()
        try:
            for data in (b'',original[:17],original[:-1],original+bytes(1)):
                path.write_bytes(data);self.rejects(synthetic_log())
            changed=bytearray(original);struct.pack_into('<HH',changed,12,960,540)
            path.write_bytes(changed);self.rejects(synthetic_log())
            changed=bytearray(original);changed[18:]=bytes(len(changed)-18)
            path.write_bytes(changed);self.rejects(synthetic_log())
            path.unlink();self.rejects(synthetic_log())
        finally:path.write_bytes(original)

    def test_final_journal_lock_and_archived_dimensions(self):
        journal=self.game/'ui-settings-recovery.dat';lock=self.game/'.settings-recovery.lock';config=self.game/'openQ4Config.cfg'
        original=config.read_bytes()
        try:
            journal.write_bytes(b'corrupt pending record');self.rejects(synthetic_log());journal.unlink()
            lock.unlink();self.rejects(synthetic_log());lock.write_bytes(b'')
            for data in (b'',original.replace(b'"960"',b'"1024"'),original+b'seta r_windowWidth "960"\n',
                         original.replace(b'"540"',b'"NaN"')):
                config.write_bytes(data);self.rejects(synthetic_log())
        finally:
            config.write_bytes(original)
            if journal.exists():journal.unlink()
            lock.write_bytes(b'')

    def test_exact_sources_and_owned_screenshot_injection(self):
        args=options();expected={Path(str(FIXTURE)+suffix).name:hashlib.sha256(Path(str(FIXTURE)+suffix).read_bytes()).hexdigest() for suffix in ('.q4ui','.cfg','-resume.cfg')}
        self.assertEqual(probe.sources(args),expected)
        commands,close,_=capture.retained_commands(args,'retained-smoke.q4ui')
        shots=re.findall(r'^screenshot "([^"]+)"$',commands,re.MULTILINE)
        self.assertEqual(shots,[f'screenshots/ui-display-{name}.tga' for name in ('confirm-a','keep','confirm-b','revert')])
        self.assertEqual(commands.count('openq4_guiGet "liveHeight"'),6)
        self.assertEqual(close,'testGUI\nwait 3\nui_retainedOwnership\n')
        pieces=commands.split('openq4_guiGet "liveHeight"\n')[1:]
        for index,piece in enumerate(pieces):self.assertEqual(piece.startswith('screenshot '),index>=2)
        for name in ('confirm-a','keep','confirm-b','revert'):
            self.assertIn(f'screenshot "screenshots/ui-display-{name}.tga"\nopenq4_retainedGui inspect "settings-panel"\n',commands)
        self.assertEqual(commands.count('openq4_retainedGui inspect "settings-panel"'),4)
        for changes in ({'width':960},{'height':540},{'brightness':1.25},{'retained_peer':True},{'retained_document':None},{'retained_script':None},
                        {'language_reload':True,'retained_resume_script':None}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):probe.sources(options(**changes))
        with tempfile.TemporaryDirectory(prefix='display-source-',dir=ROOT/'.tmp') as temp:
            for attribute,suffix in (('retained_document','.q4ui'),('retained_script','.cfg'),('retained_resume_script','-resume.cfg')):
                source=Path(str(FIXTURE)+suffix);copied=Path(temp)/source.name;copied.write_bytes(source.read_bytes())
                self.assertEqual(probe.sources(options(**{attribute:copied})),expected)
                copied.write_bytes(copied.read_bytes()+b'\n// changed source\n')
                with self.assertRaises(ValueError):capture.retained_commands(options(**{attribute:copied}),'retained-smoke.q4ui')

    def test_resume_never_replays_actions_or_screenshots(self):
        args=options(language_reload=True,video_restart=True,retained_resume_script=Path(str(FIXTURE)+'-resume.cfg'))
        commands,_,_=capture.retained_commands(args,'retained-smoke.q4ui')
        for tail in (commands.split('reloadLanguage\n',1)[1],commands.split('vid_restart windowed\n',1)[1]):
            self.assertNotIn('openq4_retainedGui event ',tail)
            self.assertNotIn('openq4_retainedGui menu ',tail)
            self.assertNotIn('screenshot ',tail)
        self.assertEqual(commands.count('openq4_guiGet "liveHeight"'),8)

    def test_cli_conflicts_and_defaults_without_process(self):
        base=['capture','--mode','sp','--renderer','gl','--assets','.', '--output','.tmp/unlaunched','--display-settings-probe']
        for arguments in ([],['--retained-managed','--retained-peer'],['--retained-managed','--system-settings-probe'],
                          ['--retained-managed','--event-program-probe'],['--retained-managed','--presentation-alias-probe'],
                          ['--retained-managed','--presentation-probe'],['--retained-managed','--retained-open']):
            with self.subTest(arguments=arguments),patch.object(sys,'argv',base+arguments),patch.object(capture,'capture') as launch,contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                capture.main()
            launch.assert_not_called()
        with patch.object(sys,'argv',base+['--retained-managed']),patch.object(capture,'capture',return_value=0) as launch:
            self.assertEqual(capture.main(),0)
            self.assertEqual(probe.sources(launch.call_args.args[0]),probe.sources(options()))

    def test_capture_glue_requires_device_evidence_and_uses_final_size(self):
        with tempfile.TemporaryDirectory(prefix='display-capture-glue-',dir=ROOT/'.tmp') as temporary:
            temp=Path(temporary);runtime=temp/'runtime';runtime.mkdir()
            (runtime/('openQ4-client_x64.exe' if capture.os.name=='nt' else 'openQ4-client_x64')).write_bytes(b'fake-for-orchestration-test')
            for omit_devices in (False,True):
                output=temp/('invalid' if omit_devices else 'valid')
                args=options(output=output,runtime=runtime,assets=temp)
                captured_command=[]
                class ProcessDouble:
                    pid=123
                    def __init__(self,command,**kwargs):
                        captured_command.extend(command)
                        game=output/'save/baseoq4';(game/'logs').mkdir()
                        lines=synthetic_log()
                        if omit_devices:lines=[line for line in lines if not line.startswith('UI_SETTINGS_DEVICE ')]
                        lines += ['Renderer API: requested=gl active=gl disposition=builtin','UI_BASELINE_CAPTURE_COMPLETE']
                        (game/'logs/openq4.log').write_text('\n'.join(lines)+'\n',encoding='utf-8')
                        write_images(game)
                        (game/'screenshots/ui-baseline.tga').write_bytes((game/'screenshots/ui-display-revert.tga').read_bytes())
                    def wait(self,timeout):return 0
                # The generic managed ownership checker is tested separately;
                # this isolates orchestration of the real display oracle.
                with patch.object(capture.subprocess,'Popen',ProcessDouble), \
                     patch.object(capture,'managed_evidence',return_value={'passed':True,'managed_trace':[]}) as managed, \
                     contextlib.redirect_stdout(io.StringIO()):
                    result=capture.capture(args)
                self.assertEqual(result,1 if omit_devices else 0)
                self.assertEqual(managed.call_args.kwargs['resource_resets'],3)
                report=json.loads((output/'capture.json').read_text(encoding='utf-8'))
                self.assertEqual(report['screenshot']['width'],960)
                self.assertEqual(report['screenshot']['height'],540)
                self.assertEqual(report['retained_preview']['managed_validation']['display_settings_contract']['passed'],not omit_devices)
                self.assertEqual(report['retained_preview']['display_settings_probe'],probe.sources(args))
                self.assertEqual(captured_command[captured_command.index('ui_retainedTrace')+1],'1')
                self.assertEqual(captured_command[captured_command.index('r_hiddenWindow')+1],'1')
                self.assertEqual(captured_command[captured_command.index('r_fullscreen')+1],'0')
                self.assertEqual(captured_command[captured_command.index('r_multiSamples')+1],'0')
                self.assertEqual(captured_command.count('r_multiSamples'),1)
                self.assertEqual(captured_command[captured_command.index('in_mouse')+1],'0')
                config=(output/'save/baseoq4/ui-baseline.cfg').read_text(encoding='utf-8')
                self.assertEqual(config.count('screenshot "screenshots/ui-display-'),4)
                self.assertEqual(config.count('openq4_retainedGui inspect "settings-panel"'),5)
                self.assertIn('screenshot "screenshots/ui-baseline.tga"\nopenq4_retainedGui inspect "settings-panel"\n',config)


if __name__=='__main__':
    unittest.main()
