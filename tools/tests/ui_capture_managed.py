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
                  language_reload=True, video_restart=True, profile_frames=0, timeline=None)
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
        for command in ('focus "x"', 'menu accept 1', 'state "x" "1"', 'save', 'restore'):
            with self.subTest(command=command), self.assertRaises(ValueError):
                self.check_script('openq4_retainedGui ' + command, managed=True, observe_only=True)

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
