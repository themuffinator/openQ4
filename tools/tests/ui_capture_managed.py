#!/usr/bin/env python3
"""Validate managed capture scripts and failure detection without launching a game."""

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ui'))
import capture_legacy_baseline as capture

FIXTURES = ROOT / 'tools/ui/fixtures'


def options(**changes):
    values = dict(retained_managed=True, retained_peer=False, retained_open=False,
                  retained_script=FIXTURES / 'managed-settings-smoke.cfg',
                  retained_resume_script=FIXTURES / 'managed-settings-smoke-resume.cfg',
                  language_reload=True, video_restart=True, profile_frames=0, timeline=None,
                  retained_document=FIXTURES / 'managed-settings-smoke.q4ui',
                  presentation_alias_probe=False, event_program_probe=False, gamma=1, brightness=1)
    values.update(changes)
    return SimpleNamespace(**values)


READBACK = '''GUI_VALUE brightness-reading::text=1.25
GUI_VALUE shadows-reading::text=#str_200157
GUI_VALUE brightness-target-reading::text=1.50
GUI_VALUE shadows-target-reading::text=0
'''
FINAL = 'RETAINED_GUI path=retained-smoke.q4ui focus=managed-shadows revision=4 active=1 brightness=1.250000 shadows=1 contexts=1\n'
OWNERSHIP = 'Retained UI ownership: open=0 suspended=1 session_gui=1 game_time=1000 requests=0\n'
BEFORE = OWNERSHIP.replace('session_gui=1 game_time=1000', 'session_gui=0 game_time=900')
AFTER = OWNERSHIP.replace('session_gui=1 game_time=1000', 'session_gui=0 game_time=1100')
LOG = '''RETAINED_GUI_LOADED retained-smoke.q4ui
RETAINED_GUI path=retained-smoke.q4ui focus= revision=1 active=1 brightness=1.000000 shadows=1 contexts=1
RETAINED_GUI_OPERATION focus passed
RETAINED_GUI_OPERATION menu passed
RETAINED_GUI_OPERATION menu passed
RETAINED_GUI path=retained-smoke.q4ui focus=managed-brightness revision=2 active=1 brightness=1.500000 shadows=1 contexts=1
RETAINED_GUI_OPERATION focus passed
RETAINED_GUI_OPERATION menu passed
RETAINED_GUI_OPERATION menu passed
RETAINED_GUI path=retained-smoke.q4ui focus=managed-shadows revision=3 active=1 brightness=1.500000 shadows=0 contexts=1
RETAINED_GUI_OPERATION save passed
RETAINED_GUI_OPERATION state passed
RETAINED_GUI_OPERATION state passed
RETAINED_GUI_OPERATION focus passed
RETAINED_GUI_OPERATION menu passed
RETAINED_GUI_OPERATION menu passed
RETAINED_GUI_OPERATION focus passed
RETAINED_GUI_OPERATION menu passed
RETAINED_GUI_OPERATION menu passed
RETAINED_GUI path=retained-smoke.q4ui focus=managed-shadows revision=7 active=1 brightness=1.250000 shadows=1 contexts=1
GUI_VALUE brightness-target-reading::text=1.25
GUI_VALUE shadows-target-reading::text=1
RETAINED_GUI_OPERATION restore passed
''' + FINAL + READBACK + FINAL


def evidence_log(peer=False, resets=2):
    result = BEFORE + LOG.replace('RETAINED_GUI_LOADED retained-smoke.q4ui\n',
                                  'RETAINED_GUI_LOADED retained-smoke.q4ui\n' + OWNERSHIP) + OWNERSHIP
    for _ in range(resets):
        result += 'RETAINED_GUI_RESOURCE path=retained-smoke.q4ui event=restored\n' + FINAL + READBACK + OWNERSHIP
    if peer:
        result = 'Retained UI preview loaded: retained-smoke.q4ui\n' + result.replace('contexts=1', 'contexts=2') + FINAL + OWNERSHIP
    return result + AFTER


def change_last_report(log, old, new):
    lines = log.splitlines(keepends=True)
    index = max(i for i, line in enumerate(lines) if line.startswith('RETAINED_GUI path='))
    lines[index] = lines[index].replace(old, new)
    return ''.join(lines)


def change_report(log, index, old, new):
    lines = log.splitlines(keepends=True)
    row = [i for i, line in enumerate(lines) if line.startswith('RETAINED_GUI path=')][index]
    lines[row] = lines[row].replace(old, new)
    return ''.join(lines)


def alias_options(**changes):
    values = dict(presentation_alias_probe=True,
                  retained_document=FIXTURES / 'presentation-alias-smoke.q4ui',
                  retained_script=FIXTURES / 'presentation-alias-smoke.cfg',
                  retained_resume_script=FIXTURES / 'presentation-alias-smoke-resume.cfg')
    values.update(changes)
    return options(**values)


def alias_log(args):
    """A counted log stand-in; mutations below test the independent oracle."""
    commands, close, _ = capture.retained_commands(args, 'retained-smoke.q4ui')
    values = iter(capture.presentation_alias_expected_values(int(args.language_reload) + int(args.video_restart), args.brightness))
    rows = []
    active = peer = opened = False
    report = 0
    for command in (commands + close).splitlines():
        if command.startswith('ui_retainedPreview '):
            peer = True
            rows.append('Retained UI preview loaded: retained-smoke.q4ui')
        elif command.startswith('testGUI '):
            active = opened = True
            rows.append('RETAINED_GUI_LOADED retained-smoke.q4ui')
        elif command == 'testGUI':
            active = False
        elif command == 'ui_retainedClose':
            peer = False
        elif command == 'ui_retainedOwnership':
            tick = 1000 if active else 1100 if opened else 900
            rows.append(f'Retained UI ownership: open=0 suspended=1 session_gui={int(active)} game_time={tick} requests=0')
        elif command in ('reloadLanguage', 'vid_restart windowed'):
            rows.append('RETAINED_GUI_RESOURCE path=retained-smoke.q4ui event=restored')
        elif command == 'openq4_retainedGui report':
            focus = 'managed-shadows' if report >= 2 else ''
            shadows = int(report < 2)
            rows.append(f'RETAINED_GUI path=retained-smoke.q4ui focus={focus} revision=1 active=1 brightness={args.brightness:.6f} shadows={shadows} contexts={1+int(peer)}')
            report += 1
        elif command.startswith('openq4_retainedGui '):
            rows.append(f'RETAINED_GUI_OPERATION {command.split()[1]} passed')
        elif command.startswith('openq4_guiGet '):
            name, value = next(values)
            assert command == f'openq4_guiGet "{name}"'
            text = value if isinstance(value, str) else ' '.join(map(str, value))
            rows.append(f'GUI_VALUE {name}={text}')
    assert next(values, None) is None
    return '\n'.join(rows) + '\n', commands + close


def event_options(**changes):
    values = dict(event_program_probe=True,
                  retained_document=FIXTURES / 'event-program-smoke.q4ui',
                  retained_script=FIXTURES / 'event-program-smoke.cfg',
                  retained_resume_script=FIXTURES / 'event-program-smoke-resume.cfg')
    values.update(changes)
    return options(**values)


def event_log(args):
    """Model trace timing, including asynchronous application dispatch."""
    commands, close, _ = capture.retained_commands(args, 'retained-smoke.q4ui')
    values = iter(capture.event_program_expected_values(int(args.language_reload) + int(args.video_restart)))
    events = iter(capture.EVENT_PROGRAM_TRACE)
    dispatches = iter(capture.EVENT_PROGRAM_DISPATCH)
    hosts = [(1.25, 1), (1.75, 0), (1.75, 0), (1.75, 1), (1.25, 1), (1.5, 0), (1.5, 0), (1.25, 0)]
    rows = []
    queued = 0
    active = peer = opened = False
    focus = ''
    report = 0

    def event(name):
        nonlocal queued
        expected, actions, writes = next(events)
        assert name == expected, (name, expected)
        rows.append(f'RETAINED_GUI_EVENT name={name} actions={actions} writes={writes}')
        queued += actions

    def pump():
        nonlocal queued
        for _ in range(queued):
            operation, value, brightness, shadows, close_requested = next(dispatches)
            rows.append(f'RETAINED_GUI_DISPATCH path=retained-smoke.q4ui operation={operation} value={value} '
                        f'brightness={brightness:.6f} shadows={shadows} close={close_requested}')
        queued = 0

    for command in (commands + 'echo UI_BASELINE_CAPTURE_COMPLETE\n' + close).splitlines():
        if command.startswith('ui_retainedPreview '):
            peer = True
            rows.append('Retained UI preview loaded: retained-smoke.q4ui')
        elif command.startswith('testGUI '):
            active = opened = True
            rows.append('RETAINED_GUI_LOADED retained-smoke.q4ui')
            event('onActivate')
            pump()
            event('onInit')
        elif command == 'testGUI':
            active = False
            event('onDeactivate')
            pump()
        elif command == 'ui_retainedClose':
            peer = False
        elif command == 'echo UI_BASELINE_CAPTURE_COMPLETE':
            rows.append('UI_BASELINE_CAPTURE_COMPLETE')
        elif command == 'ui_retainedOwnership':
            tick = 1000 if active else 1100 if opened else 900
            rows.append(f'Retained UI ownership: open=0 suspended=1 session_gui={int(active)} game_time={tick} requests=0')
        elif command in ('reloadLanguage', 'vid_restart windowed'):
            rows.append('RETAINED_GUI_RESOURCE path=retained-smoke.q4ui event=restored')
        elif command == 'openq4_retainedGui report':
            brightness, shadows = hosts[report] if report < len(hosts) else (1.25, 0)
            rows.append(f'RETAINED_GUI path=retained-smoke.q4ui focus={focus} revision=1 active=1 '
                        f'brightness={brightness:.6f} shadows={shadows} contexts={1+int(peer)}')
            report += 1
        elif command.startswith('openq4_retainedGui '):
            verb = command.split()[1]
            if verb == 'focus':
                focus = command.split('"')[1]
            elif verb == 'event':
                event(command.split('"')[1])
            elif verb == 'trigger':
                event('onTrigger')
            elif command == 'openq4_retainedGui menu accept 0':
                event('control::' + focus.removeprefix('managed-'))
            rows.append(f'RETAINED_GUI_OPERATION {verb} passed')
        elif command.startswith('openq4_guiGet '):
            name, value = next(values)
            assert command == f'openq4_guiGet "{name}"'
            text = value if isinstance(value, str) else ' '.join(map(str, value))
            rows.append(f'GUI_VALUE {name}={text}')
        elif command.startswith('wait '):
            pump()
    assert next(events, None) is None and next(dispatches, None) is None and next(values, None) is None
    return '\n'.join(rows) + '\n', commands + close


class ManagedCapture(unittest.TestCase):
    def check_script(self, source, **kwargs):
        with tempfile.TemporaryDirectory(dir=ROOT / '.tmp', prefix='managed-capture-') as directory:
            path = Path(directory) / 'script.cfg'
            path.write_text(source, encoding='utf-8')
            return capture.interaction_script(path, **kwargs)

    def test_managed_state_is_bounded_and_no_command_eval(self):
        valid = 'openq4_retainedGui state "brightnessTarget" "1.25"\nopenq4_retainedGui state "shadowsTarget" "false"\n'
        self.assertEqual(self.check_script(valid, managed=True), valid)
        for source in ('openq4_retainedGui state "x" "NaN"',
                       'openq4_retainedGui state "x" "1e999"',
                       'openq4_retainedGui state "x" "1000000000001"',
                       'openq4_retainedGui state "x" "1"; quit',
                       'openq4_retainedGui state "x" "exec config.cfg"',
                       'openq4_retainedGui menu accept 2',
                       'openq4_retainedGui focus "x\ny"',
                       'ui_retainedData "retained-data/test.json"',
                       'wait 999\n' * 4):
            with self.subTest(source=source), self.assertRaises(ValueError):
                self.check_script(source, managed=True)

    def test_resource_resume_cannot_replay_actions_or_state(self):
        resume = capture.interaction_script(FIXTURES / 'managed-settings-smoke-resume.cfg', managed=True, observe_only=True)
        self.assertIn('openq4_guiGet "brightness-reading::text"', resume)
        for command in ('focus "x"', 'menu accept 1', 'state "x" "1"', 'save', 'restore',
                        'presentation "x" "1" 0', 'update', 'pending "x" "1"', 'event "onInit"', 'trigger'):
            with self.subTest(command=command), self.assertRaises(ValueError):
                self.check_script('openq4_retainedGui ' + command, managed=True, observe_only=True)

    def test_inspect_is_bounded_and_read_only(self):
        command = 'openq4_retainedGui inspect "settings-panel"\n'
        for observe_only in (False, True):
            self.assertEqual(self.check_script(command, managed=True, observe_only=observe_only), command)
        for value in ('""', '"' + 'x'*129 + '"', '"x;y"', '"x::y"', '"x\\y"',
                      '"x"; quit', '"x" "y"', '"x\ny"', 'x', '"x" 1'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.check_script('openq4_retainedGui inspect ' + value, managed=True)

    def test_event_commands_are_semantic_bounded_and_resume_is_read_only(self):
        valid = ('openq4_retainedGui pending "draft" "13"\n'
                 'openq4_retainedGui pending "gate" "false"\n'
                 'openq4_retainedGui event "Probe::Main"\nopenq4_retainedGui trigger\n')
        self.assertEqual(self.check_script(valid, managed=True), valid)
        for command in ('pending "draft" "1e999"', 'pending "draft" "1e13"',
                        'pending "draft" "exec config.cfg"', 'pending "draft" "1"; quit',
                        'event "x::y::z"', 'event "GUI::state"', 'event ""', 'event "x y"', 'event "onInit"; quit',
                        'event "onInit\nonDeactivate"', 'trigger 1', 'trigger; quit'):
            with self.subTest(command=command), self.assertRaises(ValueError):
                self.check_script('openq4_retainedGui ' + command, managed=True)
        self.assertIn('event "Probe::Main"', capture.interaction_script(FIXTURES / 'event-program-smoke.cfg', managed=True))
        resume = capture.interaction_script(FIXTURES / 'event-program-smoke-resume.cfg', managed=True, observe_only=True)
        self.assertIn('openq4_guiGet "initialized"', resume)
        self.assertNotIn(' event ', resume)

    def test_event_fixture_declares_event_controls_localized_art_and_typed_programs(self):
        source = (FIXTURES / 'event-program-smoke.q4ui').read_text(encoding='utf-8')
        document = json.loads(source[source.index('{'):])
        self.assertEqual(document['id'], 'event-program-smoke')
        self.assertEqual(document['aliases']['curr'], document['aliases']['desktop::curr'])
        self.assertEqual(document['state']['brightness']['cvar'], 'r_brightness')
        self.assertEqual(document['state']['shadows']['cvar'], 'r_shadows')
        controls = []
        def visit(node):
            text = node.get('properties', {}).get('text')
            if text:
                self.assertRegex(text['value'], r'^#str_[A-Za-z0-9_.-]+$')
            if 'control' in node:
                controls.append(node['control'])
                self.assertNotIn('action', node['control'])
                self.assertIn(node['control']['event'], document['events'])
            for child in node.get('children', []):
                visit(child)
        visit(document['root'])
        self.assertEqual(len(controls), 2)
        for lifecycle in ('onActivate', 'onInit', 'onTrigger', 'onDeactivate'):
            self.assertIn(lifecycle, document['events'])
        self.assertEqual(document['events']['probe::main'][0]['values']['seenPending'], {'state': 'draft'})
        self.assertEqual(document['events']['probe::main'][0]['values']['seenWidth'], {'presentation': 'panel::rect', 'component': 2})
        self.assertEqual(document['actions']['settings.brightness']['arguments']['value'], {'presentation': 'brightness-request'})
        self.assertEqual(document['events']['onDeactivate'][-1], {'op': 'action', 'action': 'dismiss'})
        self.assertNotIn('"presentation"', json.dumps(document['bindings']))
        self.assertNotIn('"presentation"', json.dumps(document['presentationVariables']))

    def test_event_contract_requires_exact_sources_and_cli_defaults(self):
        args = event_options()
        self.assertEqual(capture.event_program_sources(args)['id'], 'event-program-smoke-v1')
        with tempfile.TemporaryDirectory(dir=ROOT / '.tmp', prefix='event-identity-') as directory:
            for attribute in ('retained_document', 'retained_script', 'retained_resume_script'):
                original = getattr(args, attribute)
                copied = Path(directory) / original.name
                copied.write_bytes(original.read_bytes())
                altered = event_options(**{attribute: copied})
                self.assertEqual(capture.event_program_sources(altered)['sources'][attribute]['sha256'], capture.digest(original))
                copied.write_bytes(copied.read_bytes() + b'\n// Unqualified edit\n')
                with self.subTest(attribute=attribute), self.assertRaises(ValueError):
                    capture.event_program_sources(altered)
        base = ['capture', '--mode', 'sp', '--renderer', 'gl', '--assets', '.', '--output', '.tmp/unlaunched']
        good = ['--retained-managed', '--event-program-probe', '--language-reload', '--video-restart']
        with patch.object(sys, 'argv', base + good), patch.object(capture, 'capture', return_value=0) as launch:
            self.assertEqual(capture.main(), 0)
        parsed = launch.call_args.args[0]
        self.assertEqual(parsed.retained_document, FIXTURES / 'event-program-smoke.q4ui')
        self.assertEqual(parsed.retained_script, FIXTURES / 'event-program-smoke.cfg')
        self.assertEqual(parsed.retained_resume_script, FIXTURES / 'event-program-smoke-resume.cfg')
        for arguments in (['--event-program-probe'], good + ['--presentation-probe'], good + ['--presentation-alias-probe']):
            with self.subTest(arguments=arguments), patch.object(sys, 'argv', base + arguments), \
                    patch.object(capture, 'capture') as launch, contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                capture.main()
            launch.assert_not_called()

    def test_event_oracle_checks_ordered_values_lifecycle_and_actual_dispatch(self):
        args = event_options(retained_peer=True)
        log, commands = event_log(args)
        def validate(text, *, qualify=True):
            return capture.managed_evidence(text, commands, peer=True, resource_resets=2, settings_fixture=False,
                                            event_fixture=qualify, mode='sp')
        result = validate(log)
        self.assertTrue(result['passed'], result['errors'])
        self.assertTrue(result['event_program_contract']['outgoing_lifecycle_passed'])
        self.assertIsNone(validate(log, qualify=False)['event_program_contract'])
        failures = {
            'pending applied before event': log.replace('GUI_VALUE draftReading=2\nRETAINED_GUI_EVENT name=Probe::Main',
                                                       'GUI_VALUE draftReading=13\nRETAINED_GUI_EVENT name=Probe::Main', 1),
            'batch lost pre-write value': log.replace('GUI_VALUE seenPending=13', 'GUI_VALUE seenPending=14', 1),
            'wrong vector component': log.replace('GUI_VALUE seenWidth=432', 'GUI_VALUE seenWidth=32', 1),
            'nested program skipped': log.replace('GUI_VALUE nested=1', 'GUI_VALUE nested=0', 1),
            'shared aliases diverged': log.replace('GUI_VALUE desktop::curr=14', 'GUI_VALUE desktop::curr=2', 1),
            'false override reenabled expression': log.replace('GUI_VALUE curr=15', 'GUI_VALUE curr=14', 1),
            'captured action changed later': log.replace('operation=settings.brightness.set value=1.5 ',
                                                        'operation=settings.brightness.set value=0.75 ', 1),
            'state action captured final draft': log.replace('operation=settings.brightness.set value=1.25 ',
                                                            'operation=settings.brightness.set value=1.75 ', 1),
            'conditional false branch dispatched': change_report(log, 2, 'shadows=0', 'shadows=1'),
            'host-source branch stale': change_report(log, 3, 'shadows=1', 'shadows=0'),
            'snapshot restored host effects': change_report(log, 6, 'brightness=1.500000', 'brightness=1.250000'),
            'fresh restored control failed': change_report(log, 7, 'brightness=1.250000', 'brightness=1.500000'),
            'lifecycle replayed': log.replace('RETAINED_GUI_OPERATION restore passed',
                                              'RETAINED_GUI_OPERATION restore passed\nRETAINED_GUI_EVENT name=onInit actions=0 writes=1', 1),
            'lifecycle counter not restored': 'GUI_VALUE initialized=6'.join(log.rsplit('GUI_VALUE initialized=1', 1)),
            'partial tuple restored': 'GUI_VALUE panel::rect=32 32 432'.join(log.rsplit('GUI_VALUE panel::rect=32 32 432 200', 1)),
            'wrong commit writes': log.replace('name=Probe::Main actions=3 writes=7', 'name=Probe::Main actions=3 writes=6'),
            'wrong event source': log.replace('name=onTrigger actions=0 writes=1', 'name=onInit actions=0 writes=1'),
            'missing external event': log.replace('RETAINED_GUI_EVENT name=Probe::Main actions=3 writes=7\n', ''),
            'commit after result': log.replace('RETAINED_GUI_EVENT name=Probe::Main actions=3 writes=7\nRETAINED_GUI_OPERATION event passed',
                                               'RETAINED_GUI_OPERATION event passed\nRETAINED_GUI_EVENT name=Probe::Main actions=3 writes=7'),
            'deactivation missing': log.replace('RETAINED_GUI_EVENT name=onDeactivate actions=2 writes=2\n', ''),
            'close setting incorrect': log.replace('operation=settings.brightness.set value=1 brightness=1.000000',
                                                    'operation=settings.brightness.set value=1 brightness=1.250000'),
            'dismiss not dispatched': log.replace('operation=ui.dismiss value=- brightness=1.000000 shadows=0 close=1',
                                                   'operation=ui.dismiss value=- brightness=1.000000 shadows=0 close=0'),
            'deactivation before capture': log.replace('UI_BASELINE_CAPTURE_COMPLETE\n', '') + 'UI_BASELINE_CAPTURE_COMPLETE\n',
            'nonfinite dispatch': log.replace('operation=settings.brightness.set value=1.5 ',
                                               'operation=settings.brightness.set value=NaN ', 1),
            'unrelated instance dispatched': log.replace('RETAINED_GUI_DISPATCH path=retained-smoke.q4ui',
                                                          'RETAINED_GUI_DISPATCH path=peer.q4ui', 1),
        }
        for name, text in failures.items():
            with self.subTest(name=name):
                self.assertNotEqual(text, log, 'negative mutation did not alter the evidence')
                self.assertFalse(validate(text)['passed'])
        for changes in ({'retained_peer': False}, {'language_reload': False, 'video_restart': False},
                        {'language_reload': True, 'video_restart': False}):
            varied = event_options(**changes)
            text, script = event_log(varied)
            measured = capture.managed_evidence(text, script, peer=varied.retained_peer,
                resource_resets=int(varied.language_reload) + int(varied.video_restart), settings_fixture=False,
                event_fixture=True, mode='sp')
            self.assertTrue(measured['passed'], measured['errors'])

    def test_event_capture_marker_allows_only_surrounding_whitespace(self):
        args = event_options(retained_peer=True)
        log, commands = event_log(args)
        marker = 'UI_BASELINE_CAPTURE_COMPLETE\n'
        def validate(text):
            return capture.managed_evidence(text, commands, peer=True, resource_resets=2,
                                            settings_fixture=False, event_fixture=True, mode='sp')
        for replacement in ('UI_BASELINE_CAPTURE_COMPLETE \n', ' \tUI_BASELINE_CAPTURE_COMPLETE \t\n'):
            with self.subTest(replacement=replacement):
                result = validate(log.replace(marker, replacement))
                self.assertTrue(result['passed'], result['errors'])
                self.assertTrue(result['event_program_contract']['outgoing_lifecycle_passed'])
        for replacement in ('echo UI_BASELINE_CAPTURE_COMPLETE\n', 'prefixUI_BASELINE_CAPTURE_COMPLETE\n',
                            'UI_BASELINE_CAPTURE_COMPLETEsuffix\n', 'UI_BASELINE_CAPTURE_COMPLETE suffix\n',
                            marker * 2, marker + ' UI_BASELINE_CAPTURE_COMPLETE \t\n'):
            with self.subTest(replacement=replacement):
                result = validate(log.replace(marker, replacement))
                self.assertFalse(result['passed'])
                self.assertFalse(result['event_program_contract']['outgoing_lifecycle_passed'])

    def test_alias_semantic_literals_are_bounded_data(self):
        valid = ('openq4_guiGet "curr"\nopenq4_guiGet "desktop::curr"\n'
                 'openq4_retainedGui presentation "CuRR" "-1e2" 0\n'
                 'openq4_retainedGui presentation "panel::rect" "32 32 432 200" 1\n'
                 'openq4_retainedGui presentation "title::color" "1,0.875,0.5,1" 1\n'
                 'openq4_retainedGui presentation "title::text" "#str_200009" 1\n'
                 'openq4_retainedGui presentation "panel::visible" "true" 0\n'
                 'openq4_retainedGui update\n')
        self.assertEqual(self.check_script(valid, managed=True), valid)
        for literal in ('NaN', 'inf', '1e999', '0 0 1e13 1', '0,0,1e999,1',
                        '1, 2 3', '1 2 3 4 5', '#str_200009; quit', 'exec autoexec.cfg',
                        '<span>text</span>', '1\t2', '1\x002', '1\x1b2', '1\\n2', '"; quit; "'):
            with self.subTest(literal=literal), self.assertRaises(ValueError):
                self.check_script(f'openq4_retainedGui presentation "curr" "{literal}" 1', managed=True)
        for command in ('openq4_retainedGui presentation "x::y::z" "1" 0',
                        'openq4_retainedGui presentation "curr" "1" 2',
                        'openq4_guiGet "curr"; quit', 'openq4_retainedGui update; quit'):
            with self.subTest(command=command), self.assertRaises(ValueError):
                self.check_script(command, managed=True)

    def test_alias_fixture_has_explicit_targets_localization_and_separate_state(self):
        source = (FIXTURES / 'presentation-alias-smoke.q4ui').read_text(encoding='utf-8')
        document = json.loads(source[source.index('{'):])
        self.assertEqual(document['id'], 'presentation-alias-smoke')
        self.assertEqual(document['aliases']['curr'], document['aliases']['desktop::curr'])
        self.assertEqual(document['presentationVariables']['page']['value'], {'state': 'aliasSource'})
        self.assertNotIn('curr', document['state'])
        panel = document['root']['children'][0]
        self.assertTrue(panel['paths'])
        for property in ('left', 'top', 'width', 'height'):
            self.assertEqual(panel['properties'][property]['unit'], 'dp')
        self.assertEqual(panel['properties']['display']['value'], 'block')
        self.assertEqual(panel['properties']['pointer-events']['value'], 'auto')
        self.assertIn({'id': 'alias-opacity', 'node': panel['id'], 'property': 'opacity',
                       'value': {'state': 'aliasOpacity'}}, document['bindings'])
        def visit(node):
            text = node.get('properties', {}).get('text')
            if text:
                self.assertRegex(text['value'], r'^#str_[A-Za-z0-9_.-]+$')
            if 'control' in node:
                self.assertIn(node['control']['action'], document['actions'])
            for child in node.get('children', []):
                visit(child)
        visit(document['root'])
        resume = capture.interaction_script(FIXTURES / 'presentation-alias-smoke-resume.cfg', managed=True, observe_only=True)
        self.assertIn('openq4_guiGet "curr"', resume)
        self.assertNotIn('presentation ', resume)

    def test_alias_contract_requires_exact_document_and_script_content(self):
        args = alias_options()
        provenance = capture.presentation_alias_sources(args)
        self.assertEqual(provenance['id'], 'presentation-alias-smoke-v1')
        self.assertEqual(len(provenance['sources']), 3)
        with tempfile.TemporaryDirectory(dir=ROOT / '.tmp', prefix='alias-identity-') as directory:
            for attribute in ('retained_document', 'retained_script', 'retained_resume_script'):
                original = getattr(args, attribute)
                copied = Path(directory) / original.name
                copied.write_bytes(original.read_bytes())
                changed = alias_options(**{attribute: copied})
                self.assertEqual(capture.presentation_alias_sources(changed)['sources'][attribute]['sha256'], capture.digest(original))
                copied.write_bytes(copied.read_bytes() + b'\n// Unqualified edit\n')
                with self.subTest(attribute=attribute), self.assertRaises(ValueError):
                    capture.presentation_alias_sources(changed)

    def test_alias_oracle_checks_ownership_recreation_tuples_and_parent_input(self):
        args = alias_options(retained_peer=True, brightness=1.25, gamma=1.3)
        log, commands = alias_log(args)
        def validate(text, *, qualify=True):
            return capture.managed_evidence(text, commands, peer=True, resource_resets=2, settings_fixture=False,
                                            alias_fixture=qualify, initial_brightness=1.25, mode='sp')
        result = validate(log)
        self.assertTrue(result['passed'], result['errors'])
        self.assertTrue(result['presentation_alias_contract']['passed'])
        self.assertIsNone(validate(log, qualify=False)['presentation_alias_contract'])
        failures = {
            'shared alias diverged': log.replace('GUI_VALUE desktop::curr=7', 'GUI_VALUE desktop::curr=3', 1),
            'transient did not appear': log.replace('GUI_VALUE curr=7', 'GUI_VALUE curr=3', 1),
            'update did not evaluate': log.replace('GUI_VALUE curr=3\nRETAINED_GUI_OPERATION presentation passed\nRETAINED_GUI_OPERATION state passed',
                                                  'GUI_VALUE curr=7\nRETAINED_GUI_OPERATION presentation passed\nRETAINED_GUI_OPERATION state passed', 1),
            'explicit reset by source': log.replace('GUI_VALUE curr=9', 'GUI_VALUE curr=4', 1),
            'false reenabled metadata': log.replace('GUI_VALUE curr=11', 'GUI_VALUE curr=4', 1),
            'bound transient ignored': log.replace('GUI_VALUE panel::opacity=0.25', 'GUI_VALUE panel::opacity=1', 1),
            'false reenabled binding': log.replace('GUI_VALUE panel::opacity=0.875', 'GUI_VALUE panel::opacity=0.5', 1),
            'partial rect': log.replace('GUI_VALUE panel::rect=32 32 432 200', 'GUI_VALUE panel::rect=32 32 432', 1),
            'partial colour': log.replace('GUI_VALUE title::color=1 0.875 0.5 1', 'GUI_VALUE title::color=1 0.875 0.5', 1),
            'text lost localization': log.replace('GUI_VALUE title::text=#str_200009', 'GUI_VALUE title::text=Options', 1),
            'nonfinite value': log.replace('GUI_VALUE curr=17', 'GUI_VALUE curr=NaN', 1),
            'restore lost override': log.replace('GUI_VALUE curr=11', 'GUI_VALUE curr=17'),
            'hidden parent failed': log.replace('GUI_VALUE panel::visible=0', 'GUI_VALUE panel::visible=1', 1),
            'noevents action leaked': change_report(log, 1, 'shadows=1', 'shadows=0'),
            'new activation suppressed': log.replace('brightness=1.250000 shadows=0 contexts=2', 'brightness=1.250000 shadows=1 contexts=2', 1),
            'host brightness changed': log.replace('brightness=1.250000', 'brightness=1.500000', 1),
            'failed write': log.replace('RETAINED_GUI_OPERATION presentation passed', 'RETAINED_GUI_OPERATION presentation failed', 1),
            'failed update': log.replace('RETAINED_GUI_OPERATION update passed', 'RETAINED_GUI_OPERATION update failed', 1),
            'recreated focus lost': change_last_report(log, 'focus=managed-shadows', 'focus='),
            'recreated values lost': 'GUI_VALUE curr=4'.join(log.rsplit('GUI_VALUE curr=11', 1)),
            'read reordered with write': log.replace('RETAINED_GUI_OPERATION update passed\nGUI_VALUE curr=3',
                                                    'GUI_VALUE curr=3\nRETAINED_GUI_OPERATION update passed', 1),
        }
        for name, text in failures.items():
            with self.subTest(name=name):
                self.assertNotEqual(text, log, 'negative mutation did not alter the evidence')
                self.assertFalse(validate(text)['passed'])

    def test_alias_probe_cli_defaults_and_bounded_renderer_color(self):
        base = ['capture', '--mode', 'sp', '--renderer', 'gl', '--assets', '.', '--output', '.tmp/unlaunched']
        good = ['--retained-managed', '--presentation-alias-probe', '--language-reload', '--video-restart',
                '--brightness', '1.25', '--gamma', '1.3']
        with patch.object(sys, 'argv', base + good), patch.object(capture, 'capture', return_value=0) as launch:
            self.assertEqual(capture.main(), 0)
        args = launch.call_args.args[0]
        self.assertEqual(args.retained_document, FIXTURES / 'presentation-alias-smoke.q4ui')
        self.assertEqual(args.retained_script, FIXTURES / 'presentation-alias-smoke.cfg')
        self.assertEqual(args.retained_resume_script, FIXTURES / 'presentation-alias-smoke-resume.cfg')
        self.assertEqual((args.brightness, args.gamma), (1.25, 1.3))
        for arguments, expected in (([], (1, 1)), (['--brightness', '0', '--gamma', '0.1'], (0, .1)),
                                    (['--brightness', '2', '--gamma', '3'], (2, 3))):
            with patch.object(sys, 'argv', base + arguments), patch.object(capture, 'capture', return_value=0) as launch:
                self.assertEqual(capture.main(), 0)
            args = launch.call_args.args[0]
            self.assertEqual((args.brightness, args.gamma), expected)
        for arguments in (['--presentation-alias-probe'], good + ['--presentation-probe'],
                          ['--gamma', 'NaN'], ['--gamma', 'inf'], ['--gamma', '0.09'], ['--gamma', '3.01'],
                          ['--brightness', 'NaN'], ['--brightness', 'inf'], ['--brightness', '-0.01'], ['--brightness', '2.01']):
            with self.subTest(arguments=arguments), patch.object(sys, 'argv', base + arguments), \
                    patch.object(capture, 'capture') as launch, contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                capture.main()
            launch.assert_not_called()

    def test_settings_contract_uses_requested_initial_brightness_only(self):
        commands, close, _ = capture.retained_commands(options(), 'retained-smoke.q4ui')
        log = evidence_log().replace('brightness=1.000000', 'brightness=1.400000', 1)
        result = capture.managed_evidence(log, commands + close, peer=False, resource_resets=2,
                                          settings_fixture=True, initial_brightness=1.4, mode='sp')
        self.assertTrue(result['passed'], result['errors'])
        self.assertFalse(capture.managed_evidence(log, commands + close, peer=False, resource_resets=2,
                                                settings_fixture=True, mode='sp')['passed'])

    def test_settings_fixture_has_localized_controls_and_host_bindings(self):
        source = (FIXTURES / 'managed-settings-smoke.q4ui').read_text(encoding='utf-8')
        document = json.loads(source[source.index('{'):])
        nodes = {}

        def visit(node):
            self.assertNotIn(node['id'], nodes)
            nodes[node['id']] = node
            text = node.get('properties', {}).get('text')
            if text:
                self.assertRegex(text['value'], r'^#str_[A-Za-z0-9_.-]+$')
            for child in node.get('children', []):
                visit(child)

        visit(document['root'])
        controls = [node for node in nodes.values() if 'control' in node]
        self.assertEqual([node['id'] for node in controls], ['managed-brightness', 'managed-shadows'])
        for node in controls:
            self.assertIn(node['control']['action'], document['actions'])
        for timeline in document['timelines']:
            for track in timeline['tracks']:
                self.assertIn(track['node'], nodes)
        self.assertEqual(document['state']['brightness']['cvar'], 'r_brightness')
        self.assertEqual(document['state']['shadows']['cvar'], 'r_shadows')

    def test_managed_script_loads_peer_first_and_never_replays_setup(self):
        commands, close, settle = capture.retained_commands(options(retained_peer=True), 'retained-smoke.q4ui')
        self.assertTrue(commands.startswith('ui_retainedOwnership\nui_retainedPreview "retained-smoke.q4ui"\ntestGUI "retained-smoke.q4ui"\n'))
        self.assertEqual(commands.count('openq4_retainedGui save\n'), 1)
        self.assertEqual(commands.count('openq4_retainedGui menu accept 1\n'), 4)
        for tail in commands.split('reloadLanguage\n')[1:]:
            self.assertNotIn('openq4_retainedGui state', tail)
            self.assertNotIn('openq4_retainedGui menu', tail)
        self.assertTrue(commands.endswith('ui_retainedClose\nwait 2\nopenq4_retainedGui report\nui_retainedOwnership\n'))
        self.assertEqual(close, 'testGUI\nwait 3\nui_retainedOwnership\n')
        self.assertEqual(commands.count('ui_retainedOwnership\n'), 6)
        self.assertEqual(settle, 30)

    def test_existing_preview_load_and_resume_are_preserved(self):
        args = options(retained_managed=False, retained_script=FIXTURES / 'snapshot-smoke.cfg',
                       retained_resume_script=FIXTURES / 'snapshot-resume.cfg', retained_open=True,
                       profile_frames=60, timeline='enter')
        commands, close, settle = capture.retained_commands(args, 'retained-smoke.q4ui')
        self.assertTrue(commands.startswith('ui_retainedOpen "retained-smoke.q4ui"\nwait 2\nui_retainedOwnership\n'))
        self.assertEqual(commands.count('ui_retainedProfile 60\n'), 2)
        self.assertEqual(commands.count('ui_retainedPlay "enter"\n'), 1)
        self.assertEqual(close, 'ui_retainedClose\nwait 3\nui_retainedOwnership\n')
        self.assertEqual(settle, 62)

    def test_managed_evidence_requires_actions_host_values_and_live_peer_owner(self):
        commands, close, _ = capture.retained_commands(options(retained_peer=True), 'retained-smoke.q4ui')
        log = evidence_log(peer=True)

        def validate(text):
            return capture.managed_evidence(text, commands + close, peer=True, resource_resets=2, settings_fixture=True, mode='sp')

        self.assertTrue(validate(log)['passed'], validate(log)['errors'])
        failures = {
            'missing normal load': log.replace('RETAINED_GUI_LOADED retained-smoke.q4ui\n', ''),
            'failed action': log.replace('RETAINED_GUI_OPERATION menu passed', 'RETAINED_GUI_OPERATION menu failed', 1),
            'failed sentinel restore': log.replace('RETAINED_GUI_OPERATION restore passed', 'RETAINED_GUI_OPERATION restore failed'),
            'missing restore': log.replace('RETAINED_GUI_OPERATION restore passed\n', ''),
            'replayed action': log + 'RETAINED_GUI_OPERATION menu passed\n',
            'resource failure': log.replace('event=restored', 'event=failed', 1),
            'skipped resource barrier': log.replace('RETAINED_GUI_RESOURCE path=retained-smoke.q4ui event=restored\n', '', 1),
            'peer missing': log.replace('contexts=2', 'contexts=1'),
            'peer not closed': change_last_report(log, 'contexts=1', 'contexts=2'),
            'host action not applied': log.replace('brightness=1.500000', 'brightness=1.000000', 1),
            'snapshot rolled host back': log.replace('brightness=1.250000', 'brightness=1.500000'),
            'draft not restored': log.replace('brightness-target-reading::text=1.50', 'brightness-target-reading::text=1.25'),
            'binding not live': log.replace('brightness-reading::text=1.25', 'brightness-reading::text=1.50'),
            'focus lost': change_last_report(log, 'focus=managed-shadows', 'focus='),
            'revision changed': change_last_report(log, 'revision=4', 'revision=5'),
            'unreadable property': log.replace('GUI_VALUE brightness-reading::text=1.25', 'openq4_guiGet: unknown GUI variable', 1),
            'nonfinite host': log.replace('brightness=1.250000', 'brightness=nan', 1),
        }
        for name, text in failures.items():
            with self.subTest(name=name):
                self.assertFalse(validate(text)['passed'])

    def test_revision_is_compared_only_within_resource_epoch_segments(self):
        commands, close, _ = capture.retained_commands(options(retained_peer=True), 'retained-smoke.q4ui')
        resource = 'RETAINED_GUI_RESOURCE path=retained-smoke.q4ui event=restored\n'
        epochs = evidence_log(peer=True).split(resource)
        epochs[1] = epochs[1].replace('revision=4', 'revision=2')
        epochs[2] = epochs[2].replace('revision=4', 'revision=3')
        log = resource.join(epochs)

        def validate(text):
            return capture.managed_evidence(text, commands + close, peer=True, resource_resets=2, settings_fixture=True, mode='sp')

        result = validate(log)
        self.assertTrue(result['passed'], result['errors'])
        self.assertEqual([(row['resource_epoch'], row['revision']) for row in result['reports'][4:]],
                         [(0, 4), (0, 4), (1, 2), (2, 3), (2, 3)])
        self.assertFalse(validate(change_last_report(log, 'revision=3', 'revision=4'))['passed'])
        changed = epochs.copy()
        changed[0] = change_last_report(changed[0], 'revision=4', 'revision=5')
        self.assertFalse(validate(resource.join(changed))['passed'])

    def test_normal_session_ownership_pause_resume_and_mp_continuation(self):
        commands, close, _ = capture.retained_commands(options(retained_peer=True), 'retained-smoke.q4ui')

        def validate(log, mode='sp'):
            return capture.managed_evidence(log, commands + close, peer=True, resource_resets=2, settings_fixture=True, mode=mode)

        log = evidence_log(peer=True)
        self.assertTrue(validate(log)['ownership_passed'])
        self.assertTrue(validate(log.replace('suspended=1', 'suspended=0'))['passed'])
        for bad in (log.replace('open=0', 'open=1', 1), log.replace('requests=0', 'requests=1', 1),
                    log.replace(OWNERSHIP, OWNERSHIP.replace('session_gui=1', 'session_gui=0'), 1),
                    log.replace(AFTER, OWNERSHIP), log.replace(AFTER, AFTER.replace('game_time=1100', 'game_time=1000')),
                    log.replace(OWNERSHIP, OWNERSHIP.replace('game_time=1000', 'game_time=999'), 1),
                    log.replace(OWNERSHIP, '', 1)):
            self.assertFalse(validate(bad)['ownership_passed'])
        self.assertFalse(validate(log, 'mp')['passed'])
        rows = log.splitlines(keepends=True)
        tick = 1000
        for i, row in enumerate(rows):
            if row == OWNERSHIP:
                rows[i] = row.replace('game_time=1000', f'game_time={tick}')
                tick += 100
        mp_log = ''.join(rows).replace(AFTER, AFTER.replace('game_time=1100', f'game_time={tick}'))
        self.assertTrue(validate(mp_log, 'mp')['passed'], validate(mp_log, 'mp')['errors'])
        self.assertFalse(validate(mp_log, 'sp')['passed'])

    def test_capture_cli_rejects_incompatible_ownership_without_launch(self):
        base = ['capture', '--mode', 'sp', '--renderer', 'gl', '--assets', '.', '--output', '.tmp/unlaunched',
                '--retained-document', str(FIXTURES / 'managed-settings-smoke.q4ui')]
        for arguments in (['--retained-managed', '--retained-open'], ['--retained-peer'],
                          ['--retained-managed', '--profile-frames', '1'], ['--retained-managed', '--timeline', 'enter']):
            with self.subTest(arguments=arguments), patch.object(sys, 'argv', base + arguments), \
                    patch.object(capture, 'capture') as launch, contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                capture.main()
            launch.assert_not_called()


if __name__ == '__main__':
    (ROOT / '.tmp').mkdir(exist_ok=True)
    unittest.main()
