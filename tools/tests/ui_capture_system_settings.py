#!/usr/bin/env python3
"""Test SYSTEM capture qualification using synthetic logs; never launch a game.

Expected observations are authored independently below. This tests the oracle,
committed semantic script restrictions and CLI source binding, not engine UI
output, the actual settings service, persistence or device application.
"""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ui'))
import capture_legacy_baseline as capture
import system_settings_probe as probe

FIXTURE = ROOT / 'tools/ui/fixtures/system-settings-smoke'
FIELDS = ('live', 'draft', 'baseline', 'dirty', 'open', 'canApply', 'phase',
          'liveShadows', 'draftShadows', 'liveWidth', 'draftWidth', 'spare')
OPERATIONS = (
    ('begin', 0, 1, 0), ('edit', 0, 1, 1), ('apply', 0, 1, 0),
    ('edit', 0, 1, 1), ('defaults', 0, 1, 1), ('revert', 0, 1, 0),
    ('cancel', 0, 0, 0), ('begin', 0, 1, 0), ('edit', 3, 1, 0),
    ('edit', 0, 1, 1), ('apply', 3, 1, 1), ('revert', 0, 1, 0),
    ('edit', 0, 1, 1), ('apply', 4, 1, 1), ('cancel', 0, 0, 0),
    ('begin', 0, 1, 0), ('edit', 0, 1, 1), ('apply', 0, 1, 0), ('edit', 0, 1, 1),
)
DIAGNOSTICS = (
    'WARNING: retained GUI retained-smoke.q4ui: r_brightness: outside the registered CVar range',
    'WARNING: retained GUI retained-smoke.q4ui: System settings batch requires unsupported effects or an owning confirmation view',
    'WARNING: retained GUI retained-smoke.q4ui: Settings changed outside this session; reopen before applying',
)


def options(**changes):
    values = dict(brightness=1, width=1280, language_reload=True, video_restart=True,
                  retained_managed=True, retained_open=False, retained_peer=False,
                  profile_frames=0, timeline=None,
                  retained_document=Path(str(FIXTURE) + '.q4ui'),
                  retained_script=Path(str(FIXTURE) + '.cfg'),
                  retained_resume_script=Path(str(FIXTURE) + '-resume.cfg'))
    values.update(changes)
    return SimpleNamespace(**values)


def log_lines(width=1280, resets=2):
    # These rows describe observation checkpoints, independently of the oracle's
    # expected_rows implementation. Traces are parsed separately by the oracle;
    # this synthetic log does not purport to model real engine interleaving.
    rows = [
        [1, 1, 1, 0, 1, 0, 1, 1, 1, width, width, 7],
        [1, 1.25, 1, 1, 1, 1, 1, 1, 0, width, width, 7],
        [1.25, 1.25, 1.25, 0, 1, 0, 1, 0, 0, width, width, 9],
        [1.25, 1, 1.25, 1, 1, 0, 1, 0, 1, width, 1280, 9],
        [1.25, 1, 1.25, 1, 1, 0, 1, 0, 1, width, 1280, 9],
        [1.25, 1.25, 1.25, 0, 1, 0, 1, 0, 0, width, width, 9],
        [1.25, 0, 0, 0, 0, 0, 0, 0, 0, width, 0, 9],
        [1.25, 1.25, 1.25, 0, 1, 0, 1, 0, 0, width, width, 9],
        [1.25, 1.25, 1.25, 1, 1, 0, 1, 0, 0, width, 960, 9],
        [1.1, 1.5, 1.25, 1, 1, 1, 1, 0, 0, width, width, 9],
        [1.25, 1.25, 1.25, 0, 1, 0, 1, 0, 0, width, width, 9],
        [1.25, 1.5, 1.25, 1, 1, 1, 1, 0, 0, width, width, 9],
    ]
    rows += [rows[-1][:] for _ in range(resets)]
    result = [f'GUI_VALUE {field}={number:.17g}' for row in rows for field, number in zip(FIELDS, row)]
    result += [f'UI_SETTINGS operation=settings.system.{op} result={code} phase={phase} owner=27 dirty={dirty}'
               for op, code, phase, dirty in OPERATIONS]
    return result + list(DIAGNOSTICS)


def evidence(lines, width=1280, resets=2):
    return probe.evidence('\n'.join(lines) + '\n', width=width, resets=resets)


class SystemSettingsCapture(unittest.TestCase):
    def rejects(self, lines, **kwargs):
        result = evidence(lines, **kwargs)
        self.assertFalse(result['passed'])
        self.assertTrue(result['errors'])
        self.assertFalse(result['replacement_acceptance'])
        self.assertFalse(result['device_application_qualified'])

    def test_complete_observations_and_all_nineteen_operations_pass(self):
        for width in (1280, 1920):
            for resets in (0, 1, 2):
                with self.subTest(width=width, resets=resets):
                    result = evidence(log_lines(width, resets), width, resets)
                    self.assertTrue(result['passed'], result['errors'])
                    self.assertEqual(len(result['readbacks']), (12 + resets) * 12)
                    self.assertEqual(len(result['operation_trace']), 19)
                    self.assertEqual(result['id'], 'system-settings-smoke-v1')
                    self.assertEqual(result['expected_diagnostics'], list(DIAGNOSTICS))
                    self.assertFalse(result['replacement_acceptance'])
                    self.assertFalse(result['device_application_qualified'])

    def test_every_missing_changed_or_reordered_readback_fails(self):
        original = log_lines()
        for index in range(168):
            with self.subTest(index=index, mutation='missing'):
                self.rejects(original[:index] + original[index + 1:])
            changed = original.copy()
            key, number = changed[index].rsplit('=', 1)
            changed[index] = f'{key}={float(number) + .01:.17g}'
            with self.subTest(index=index, mutation='value'):
                self.rejects(changed)
        self.rejects([original[0]] + original)
        changed = original.copy(); changed[0], changed[1] = changed[1], changed[0]
        self.rejects(changed)
        changed = original.copy(); changed[0] = 'GUI_VALUE wrong=1'
        self.rejects(changed)
        self.rejects(log_lines(resets=1))
        self.rejects(log_lines(width=1920))

    def test_nonfinite_malformed_and_outside_tolerance_values_fail(self):
        for value in ('NaN', 'nan', 'inf', '-inf', '1e309', '', 'number', '1 2'):
            with self.subTest(value=value):
                changed = log_lines(); changed[0] = f'GUI_VALUE live={value}'
                self.rejects(changed)
        near = log_lines(); near[0] = 'GUI_VALUE live=1.0000005'
        self.assertTrue(evidence(near)['passed'])
        near[0] = 'GUI_VALUE live=1.000002'
        self.rejects(near)

    def test_every_operation_result_and_phase_is_checked_without_replay(self):
        original = log_lines()
        for offset, (_, code, phase, _) in enumerate(OPERATIONS):
            index = 168 + offset
            mutations = {
                'missing': original[:index] + original[index + 1:],
                'replayed': original + [original[index]],
            }
            for name, old, new in (('result', f'result={code}', f'result={code + 1}'),
                                   ('phase', f'phase={phase}', f'phase={phase + 1}'),
                                   ('operation', 'operation=settings.system.', 'operation=settings.system.wrong_')):
                changed = original.copy(); changed[index] = changed[index].replace(old, new)
                mutations[name] = changed
            for mutation, changed in mutations.items():
                with self.subTest(operation=offset, mutation=mutation):
                    self.rejects(changed)
        # Device work is currently unqualified: neither a falsely successful
        # operation nor a changed live display value may satisfy this probe.
        changed = original.copy(); changed[168 + 10] = changed[168 + 10].replace('result=3', 'result=0')
        self.rejects(changed)
        changed = original.copy(); changed[8 * 12 + FIELDS.index('liveWidth')] = 'GUI_VALUE liveWidth=960'
        self.rejects(changed)

    def test_owner_and_exact_negative_diagnostics_are_required(self):
        original = log_lines()
        self.rejects([line.replace('owner=27', 'owner=0') for line in original])
        for index in range(168, 168 + 19):
            changed = original.copy(); changed[index] = changed[index].replace('owner=27', 'owner=28')
            with self.subTest(owner_change=index):
                self.rejects(changed)
        for warning in DIAGNOSTICS:
            with self.subTest(missing=warning):
                self.rejects([line for line in original if line != warning])
            self.rejects(original + [warning])
        self.rejects(original + ['WARNING: retained GUI retained-smoke.q4ui: unexpected error'])
        self.rejects([line.replace('outside the registered CVar range', 'outside the editor range') for line in original])
        changed = original.copy(); changed[-1], changed[-2] = changed[-2], changed[-1]
        self.rejects(changed)

    def test_committed_setup_is_semantic_and_resume_is_observation_only(self):
        args = options()
        setup = capture.interaction_script(args.retained_script, managed=True)
        resume = capture.interaction_script(args.retained_resume_script, managed=True, observe_only=True)
        self.assertEqual(re.findall(r'^openq4_guiGet "([^"]+)"$', setup, re.MULTILINE), list(FIELDS) * 12)
        self.assertEqual(re.findall(r'^openq4_guiGet "([^"]+)"$', resume, re.MULTILINE), list(FIELDS))
        self.assertEqual(re.findall(r'^openq4_retainedGui event "([^"]+)"$', setup, re.MULTILINE),
                         ['edit', 'apply', 'later', 'defaults', 'revert', 'cancel', 'begin', 'invalid',
                          'device', 'apply', 'revert', 'later', 'external', 'apply', 'cancel', 'begin', 'edit', 'apply', 'later'])
        self.assertIn('openq4_retainedGui pending "settings.draft.r_brightness" "1.9"', setup)
        self.assertIn('openq4_retainedGui pending "spare" "9"', setup)
        self.assertLess(setup.index('openq4_retainedGui save'), setup.index('openq4_retainedGui restore'))
        commands, close, _ = capture.retained_commands(args, 'retained-smoke.q4ui')
        self.assertEqual(commands.count(setup), 1)
        self.assertTrue(commands.startswith('ui_retainedOwnership\ntestGUI "retained-smoke.q4ui"'))
        for part in commands.split('reloadLanguage\n')[1:] + commands.split('vid_restart windowed\n')[1:]:
            self.assertIn(resume, part)
            self.assertNotIn('openq4_retainedGui event ', part)
            self.assertNotIn('openq4_retainedGui pending ', part)
            self.assertNotIn('openq4_retainedGui restore', part)
        self.assertEqual(close, 'testGUI\nwait 3\nui_retainedOwnership\n')
        source = args.retained_document.read_text(encoding='utf-8')
        model = json.loads(source[source.index('{'):])
        self.assertEqual(model['events']['onActivate'], [{'op': 'action', 'action': 'begin'}])
        self.assertEqual(model['actions']['device']['arguments'], {'r_windowWidth': 960})
        self.assertEqual(model['actions']['invalid']['arguments'], {'r_brightness': 2.5})
        self.assertFalse(model['extensions']['openq4.fixture']['replacementAcceptance'])

    def test_resume_cannot_hide_setup_replay_or_device_commands(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '.tmp', prefix='ui-system-probe-') as temp:
            path = Path(temp) / 'resume.cfg'
            for command in ('openq4_retainedGui event "begin"', 'openq4_retainedGui pending "spare" "9"',
                            'openq4_retainedGui state "settings.draft.r_brightness" "1.9"',
                            'openq4_retainedGui restore', 'openq4_retainedGui menu accept 1',
                            'vid_restart windowed', 'r_brightness 1.5', 'exec "setup.cfg"',
                            'openq4_guiGet "live"; quit'):
                path.write_text(command + '\n', encoding='utf-8')
                with self.subTest(command=command), self.assertRaises(ValueError):
                    capture.interaction_script(path, managed=True, observe_only=True)

    def test_source_bytes_are_bound_with_explicit_initial_conditions(self):
        expected = {Path(str(FIXTURE) + suffix).name: hashlib.sha256(Path(str(FIXTURE) + suffix).read_bytes()).hexdigest()
                    for suffix in ('.q4ui', '.cfg', '-resume.cfg')}
        self.assertEqual(probe.sources(options()), expected)
        no_reset = probe.sources(options(language_reload=False, video_restart=False, retained_resume_script=None))
        self.assertEqual(set(no_reset), {FIXTURE.name + '.q4ui', FIXTURE.name + '.cfg'})
        for changes in ({'brightness': 1.25}, {'brightness': float('nan')}, {'width': 960},
                        {'retained_document': None}, {'retained_script': None}, {'retained_resume_script': None}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                probe.sources(options(**changes))
        with tempfile.TemporaryDirectory(dir=ROOT / '.tmp', prefix='ui-system-probe-') as temp:
            for attribute in ('retained_document', 'retained_script', 'retained_resume_script'):
                original = getattr(options(), attribute)
                copied = Path(temp) / original.name; copied.write_bytes(original.read_bytes())
                self.assertEqual(probe.sources(options(**{attribute: copied})), expected)
                copied.write_bytes(copied.read_bytes() + b'\n// Different qualification source\n')
                with self.subTest(attribute=attribute), self.assertRaises(ValueError):
                    probe.sources(options(**{attribute: copied}))

    def test_cli_conflicts_are_rejected_before_capture(self):
        base = ['capture', '--mode', 'sp', '--renderer', 'gl', '--assets', '.', '--output', '.tmp/unlaunched',
                '--system-settings-probe']
        for arguments in ([], ['--retained-managed', '--presentation-probe'],
                          ['--retained-managed', '--presentation-alias-probe'], ['--retained-managed', '--event-program-probe'],
                          ['--retained-managed', '--retained-open'], ['--retained-managed', '--timeline', 'enter'],
                          ['--retained-managed', '--profile-frames', '1'], ['--retained-managed', '--retained-data', 'data.json'],
                          ['--retained-managed', '--brightness', 'NaN']):
            with self.subTest(arguments=arguments), patch.object(sys, 'argv', base + arguments), \
                    patch.object(capture, 'capture') as launch, contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                capture.main()
            launch.assert_not_called()
        with patch.object(sys, 'argv', base + ['--retained-managed', '--language-reload', '--video-restart']), \
                patch.object(capture, 'capture', return_value=0) as launch:
            self.assertEqual(capture.main(), 0)
        args = launch.call_args.args[0]
        self.assertEqual(args.retained_document, options().retained_document)
        self.assertEqual(args.retained_script, options().retained_script)
        self.assertEqual(args.retained_resume_script, options().retained_resume_script)
        self.assertEqual(len(probe.sources(args)), 3)

    def test_cli_source_mismatch_stops_before_process_launch(self):
        base = ['capture', '--mode', 'sp', '--renderer', 'gl', '--assets', '.', '--output', '.tmp/unlaunched',
                '--system-settings-probe', '--retained-managed', '--language-reload', '--video-restart']
        with tempfile.TemporaryDirectory(dir=ROOT / '.tmp', prefix='ui-system-probe-') as temp:
            for attribute in ('retained_document', 'retained_script', 'retained_resume_script'):
                original = getattr(options(), attribute)
                path = Path(temp) / original.name; path.write_bytes(original.read_bytes() + b'\n')
                arguments = base + ['--' + attribute.replace('_', '-'), str(path)]
                output = io.StringIO()
                with self.subTest(attribute=attribute), patch.object(sys, 'argv', arguments), \
                        patch.object(capture.subprocess, 'Popen') as launch, contextlib.redirect_stdout(output):
                    self.assertEqual(capture.main(), 1)
                launch.assert_not_called()
                self.assertIn('exact committed fixture', output.getvalue())
            for arguments in (['--brightness', '1.25'], ['--width', '960']):
                with self.subTest(arguments=arguments), patch.object(sys, 'argv', base + arguments), \
                        patch.object(capture.subprocess, 'Popen') as launch, contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(capture.main(), 1)
                launch.assert_not_called()


if __name__ == '__main__':
    (ROOT / '.tmp').mkdir(exist_ok=True)
    unittest.main()
