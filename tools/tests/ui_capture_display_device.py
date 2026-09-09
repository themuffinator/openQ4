#!/usr/bin/env python3
"""Mutation-test display capture evidence without launching SDL, a renderer or a game.

Synthetic observations follow the production DISPLAY_PROBE emitter and deliberately
start with nonzero historical failures/restarts. These tests qualify the oracle and
bounded launch script, not physical presentation or settings confirmation recovery.
"""

import contextlib
import copy
import io
import json
from pathlib import Path
import runpy
import shlex
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ui'))
import capture_display_device as capture


FIELDS = ('live', 'draft', 'baseline', 'dirty', 'open', 'phase', 'liveWidth', 'draftWidth')
VALUES = ('1', '1.25', '1', '1', '1', '1', '1280', '1280')
READS = [f'GUI_VALUE {key}={value}' for key, value in zip(FIELDS, VALUES)]
ACTIONS = [
    'UI_SETTINGS operation=settings.system.begin result=0 phase=1 owner=37 dirty=0',
    'UI_SETTINGS operation=settings.system.edit result=0 phase=1 owner=37 dirty=1',
]
MARKER = 'DISPLAY_DEVICE_CAPTURE_COMPLETE'


def observations():
    base = dict(operation='save', result='1', observed='1', epoch='7', generation='10',
                ready='1', window='1', available='1', outcome='2', submitted='100', presented='100',
                failures='2', native='0', restart='3', logical='1280x720', pixel='1280x720',
                display='19', position='30,40', hidden='1', fullscreen='0', maximized='0',
                samples='0', interval='1', valid='3')
    result = [base]
    for operation, generation, restart, sequence, size, outcome, failures in (
        ('apply', 12, 4, 100, '960x540', 1, 2),
        ('report', 12, 4, 107, '960x540', 2, 2),
        ('restore', 14, 5, 107, '1280x720', 1, 2),
        ('report', 14, 5, 114, '1280x720', 2, 2),
        ('missing-display', 15, 5, 114, '0x0', 3, 3),
        ('restore', 17, 6, 114, '1280x720', 1, 3),
        ('report', 17, 6, 121, '1280x720', 2, 3),
    ):
        row = dict(base, operation=operation, generation=str(generation), restart=str(restart),
                   submitted=str(sequence), presented=str(sequence), logical=size, pixel=size,
                   outcome=str(outcome), failures=str(failures))
        if size == '960x540': row['interval'] = '0'
        if operation == 'missing-display':
            row.update(result='0', ready='0', window='0', available='0', display='0', position='0,0',
                       hidden='0', samples='0', interval='0', valid='0')
        result.append(row)
    return result


def trace(rows=None):
    lines = ['RendererModule self-test passed', 'Renderer API: requested=gl active=gl']
    lines.extend(ACTIONS + READS)
    for index, row in enumerate(observations() if rows is None else rows):
        lines.append('DISPLAY_PROBE ' + ' '.join(f'{key}={value}' for key, value in row.items()))
        if index == 5:
            lines.append('gfxInfo: graphics device is unavailable')
        if index in (2, 4, 7):
            lines.extend(READS)
    return '\n'.join(lines + [MARKER, 'Shutting down renderer']) + '\n'


def change_row(index, **fields):
    rows = observations()
    rows[index].update({key: str(value) for key, value in fields.items()})
    return trace(rows)


class DisplayOracleTests(unittest.TestCase):
    negative_cases = 0

    def rejects(self, log):
        result = capture.qualify(log)
        self.assertFalse(result['passed'], f'Oracle accepted corrupted evidence:\n{log}')
        self.assertTrue(result['errors'])
        self.assertFalse(result['settings_confirmation_qualified'])
        self.assertFalse(result['replacement_acceptance'])
        type(self).negative_cases += 1

    def test_complete_trace_qualifies_only_private_seam(self):
        result = capture.qualify(trace())
        self.assertTrue(result['passed'], result['errors'])
        self.assertEqual(len(result['rows']), 8)
        self.assertEqual(len(result['menu_reads']), 32)
        self.assertEqual(len(result['settings_actions']), 2)
        self.assertFalse(result['settings_confirmation_qualified'])
        self.assertFalse(result['replacement_acceptance'])

    def test_unavailable_device_diagnostic_must_return_between_failure_and_restore(self):
        marker = 'gfxInfo: graphics device is unavailable\n'
        self.rejects(trace().replace(marker,''))
        self.rejects(marker + trace().replace(marker,''))
        self.rejects(trace().replace(marker,marker+marker))

    def test_high_density_observed_restore_and_vulkan_parameter_bit(self):
        rows = observations()
        for index, row in enumerate(rows):
            if index != 5:
                row['pixel'] = '1920x1080' if index in (1, 2) else '2560x1440'
                row['valid'] = '7'
        result = capture.qualify(trace(rows))
        self.assertTrue(result['passed'], result['errors'])

    def test_missing_duplicate_and_reordered_operations(self):
        original = observations()
        candidates = []
        for index in range(8):
            rows = copy.deepcopy(original)
            del rows[index]
            candidates.append(rows)
        for first, second in ((0, 1), (1, 2), (2, 4), (5, 6), (6, 7)):
            rows = copy.deepcopy(original)
            rows[first], rows[second] = rows[second], rows[first]
            candidates.append(rows)
        candidates.extend([original + [original[-1]], original[:2] + [original[1]] + original[2:]])
        for index, rows in enumerate(candidates):
            with self.subTest(case=index):
                self.rejects(trace(rows))

    def test_false_results_and_unavailable_observations(self):
        for index in range(8):
            for fields in ({'result': '1' if index == 5 else '0'}, {'observed': '0'}):
                with self.subTest(index=index, fields=fields):
                    self.rejects(change_row(index, **fields))
        for index in (0, 1, 2, 3, 4, 6, 7):
            for field, value in (('ready', 0), ('window', 0), ('available', 0), ('hidden', 0), ('fullscreen', 1)):
                with self.subTest(index=index, field=field):
                    self.rejects(change_row(index, **{field: value}))

    def test_fresh_submit_and_present_required(self):
        rows = observations()
        for before, after in ((1, 2), (3, 4), (6, 7)):
            for fields in ({'outcome': '1'}, {'outcome': '10'},
                           {'presented': rows[before]['presented']},
                           {'submitted': rows[before]['submitted']},
                           {'presented': str(int(rows[before]['presented']) - 1)},
                           {'submitted': str(int(rows[before]['submitted']) - 1)}):
                with self.subTest(after=after, fields=fields):
                    self.rejects(change_row(after, **fields))

    def test_generation_and_epoch_continuity(self):
        rows = observations()
        for index in range(8):
            with self.subTest(epoch_index=index):
                self.rejects(change_row(index, epoch='8'))
        for before, after in ((1, 2), (3, 4), (6, 7)):
            with self.subTest(generation_report=after):
                self.rejects(change_row(after, generation=int(rows[before]['generation']) + 1))
        for before, after in ((0, 1), (2, 3), (5, 6)):
            with self.subTest(generation_apply=after):
                self.rejects(change_row(after, generation=rows[before]['generation']))
        rows[5]['generation'] = '1'
        self.rejects(trace(rows))

    def test_restart_count_does_not_publish_false_readiness(self):
        for index in range(1, 8):
            with self.subTest(index=index):
                self.rejects(change_row(index, restart=int(observations()[index]['restart']) + 1))

    def test_only_deliberate_missing_display_may_add_failures(self):
        for start in (1, 2, 3, 4, 6, 7):
            rows = observations()
            for row in rows[start:]:
                row['failures'] = str(int(row['failures']) + 1)
            with self.subTest(extra_failure_at=start):
                self.rejects(trace(rows))
        for fields in ({'failures': '2'}, {'ready': '1'}, {'result': '1'}, {'outcome': '2'}):
            with self.subTest(missing_display=fields):
                self.rejects(change_row(5, **fields))
        for index in (0, 1, 2, 3, 4, 6, 7):
            with self.subTest(native=index):
                self.rejects(change_row(index, native='-4'))

    def test_counters_cannot_reset_during_same_module_epoch(self):
        rows = observations()
        for row in rows[3:]:
            row['submitted'] = str(int(row['submitted']) - 90)
            row['presented'] = str(int(row['presented']) - 90)
        self.rejects(trace(rows))
        rows = observations()
        for row in rows[3:]:
            row['failures'] = str(int(row['failures']) - 1)
        self.rejects(trace(rows))

    def test_requested_and_restored_observed_tuple(self):
        for index in (1, 2):
            with self.subTest(applied=index):
                self.rejects(change_row(index, logical='1280x720'))
        replacements = dict(logical='1279x720', pixel='2560x1440', display='21', position='31,40',
                            maximized='1', samples='4', interval='0', valid='1')
        for index in (3, 4, 6, 7):
            for field, value in replacements.items():
                with self.subTest(index=index, field=field):
                    self.rejects(change_row(index, **{field: value}))

    def test_applied_swap_interval_must_survive_first_present_against_old_cvar(self):
        for index in (1,2):
            self.rejects(change_row(index, interval='1'))
        rows = observations()
        rows[1]['interval'] = rows[2]['interval'] = '1'
        self.rejects(trace(rows))

    def test_requested_msaa_is_verified_independently_of_stored_preference(self):
        rows = observations()
        rows[1]['samples'] = rows[2]['samples'] = '4'
        self.assertTrue(capture.qualify(trace(rows),4)['passed'])
        self.rejects(trace(rows))
        self.assertFalse(capture.qualify(trace(),4)['passed'])
        rows[2]['samples'] = '0'
        self.assertFalse(capture.qualify(trace(rows),4)['passed'])

    def test_successful_window_and_per_epoch_tuple_are_authoritative(self):
        for field, value in (('pixel', '0x0'), ('display', '0')):
            rows = observations()
            for index, row in enumerate(rows):
                if index != 5:
                    row[field] = value
            with self.subTest(all_successful=field):
                self.rejects(trace(rows))
        for index in (1, 2):
            for field, value in (('pixel', '1920x1080'), ('display', '21'), ('position', '31,40'),
                                 ('maximized', '1'), ('samples', '4'), ('interval', '1'), ('valid', '7')):
                with self.subTest(index=index, field=field):
                    self.rejects(change_row(index, **{field: value}))

    def test_menu_values_and_action_ownership(self):
        for read in READS:
            with self.subTest(read=read):
                self.rejects(trace().replace(read, read.split('=')[0] + '=99', 1))
        for text in (trace().replace(READS[0] + '\n', '', 1), trace() + READS[0] + '\n',
                     trace().replace('GUI_VALUE draft=1.25', 'GUI_VALUE draft=NaN', 1),
                     trace().replace('GUI_VALUE draft=1.25', 'GUI_VALUE draft=broken', 1),
                     trace().replace(ACTIONS[1], ACTIONS[1] + '\n' + ACTIONS[1]),
                     trace().replace(ACTIONS[0], ''),
                     trace().replace('owner=37', 'owner=0'),
                     trace().replace(ACTIONS[1], ACTIONS[1].replace('owner=37', 'owner=38')),
                     trace().replace(ACTIONS[1], ACTIONS[1].replace('result=0', 'result=1')),
                     trace().replace(ACTIONS[1], ACTIONS[1].replace('phase=1', 'phase=2'))):
            with self.subTest(tail=text[-80:]):
                self.rejects(text)

    def test_action_operation_names_are_literal_and_extra_fields_fail_closed(self):
        for old, new in (('settings.system.', 'settingsXsystemY'),
                         ('settings.system.', 'settings/system/'),
                         ('owner=37 dirty=1', 'owner=37 dirty=1 extra=0'),
                         ('owner=37 dirty=1', 'owner=37 dirty=1 dirty=1')):
            with self.subTest(new=new):
                self.rejects(trace().replace(old, new))

    def test_reads_actions_and_completion_must_be_interleaved_with_operations(self):
        original = trace().splitlines()
        reads = [line for line in original if line.startswith('GUI_VALUE ')]
        no_reads = [line for line in original if not line.startswith('GUI_VALUE ')]
        for text in ('\n'.join(reads + no_reads), '\n'.join(no_reads[:-2] + reads + no_reads[-2:])):
            with self.subTest(grouped_reads=text[:80]):
                self.rejects(text)
        no_actions = [line for line in original if line not in ACTIONS]
        self.rejects('\n'.join(no_actions[:-2] + ACTIONS + no_actions[-2:]))
        self.rejects(trace().replace('\n' + MARKER, '').replace('DISPLAY_PROBE operation=save', MARKER + '\nDISPLAY_PROBE operation=save'))

    def test_completion_requires_one_standalone_marker(self):
        for marker in (MARKER + ' ', '\t' + MARKER + ' \t'):
            result = capture.qualify(trace().replace(MARKER, marker))
            self.assertTrue(result['passed'], result['errors'])
        for marker in ('', 'echo ' + MARKER, 'prefix_' + MARKER, MARKER + '_suffix',
                       MARKER + ' junk', MARKER + '\n' + MARKER):
            with self.subTest(marker=marker):
                self.rejects(trace().replace(MARKER, marker))

    def test_malformed_display_fields_fail_closed(self):
        for key in observations()[0]:
            rows = observations()
            del rows[0][key]
            with self.subTest(missing=key):
                self.rejects(trace(rows))
        invalid = {'epoch': ('0', '-1', 'x', '18446744073709551616', '9' * 5000),
                   'generation': ('-1', 'x', '18446744073709551616'),
                   'submitted': ('-1', 'NaN'), 'presented': ('-1', 'NaN'), 'failures': ('-1', 'NaN'),
                   'restart': ('-1', 'x'), 'outcome': ('99', 'x'), 'native': ('x', '2147483648'),
                   'logical': ('x', '0x720', '1280x-1'), 'pixel': ('x', '0x720'),
                   'display': ('0', 'x', '4294967296'), 'position': ('x', '1,2,3'),
                   'maximized': ('2', 'true'), 'samples': ('x', '-1'), 'interval': ('x',), 'valid': ('x', '0', '1')}
        for key, values in invalid.items():
            for value in values:
                with self.subTest(key=key, value=value if len(value) < 80 else f'{len(value)} digits'):
                    self.rejects(change_row(0, **{key: value}))
        for suffix in (' generation=10', ' unknown=1', ' trailing', ' ready=', ' malformed==1'):
            lines = trace().splitlines()
            index = next(i for i, line in enumerate(lines) if line.startswith('DISPLAY_PROBE '))
            lines[index] += suffix
            with self.subTest(suffix=suffix):
                self.rejects('\n'.join(lines))


class DisplayCaptureRestrictionsTests(unittest.TestCase):
    def test_script_has_only_fixed_semantic_operations_engine_screenshots_and_bounded_waits(self):
        read_commands = [('openq4_guiGet', field) for field in FIELDS]
        expected = [('rendererModuleSelfTest',), ('testGUI', 'display-smoke.q4ui'), ('wait', '3'),
                    ('openq4_retainedGui', 'event', 'edit'), ('wait', '3')] + read_commands
        expected += [('rendererDisplayProbe', 'save'), ('rendererDisplayProbe', 'apply', '960', '540', '0', '0'),
                     ('wait', '6'), ('rendererDisplayProbe', 'report'), ('screenshot', 'screenshots/display-applied.tga')]
        expected += read_commands + [('rendererDisplayProbe', 'restore'), ('wait', '6'),
                                    ('rendererDisplayProbe', 'report'), ('screenshot', 'screenshots/display-restored.tga')]
        expected += read_commands + [('rendererDisplayProbe', 'missing-display'), ('gfxInfo',), ('rendererDisplayProbe', 'restore'),
                                    ('wait', '6'), ('rendererDisplayProbe', 'report'),
                                    ('screenshot', 'screenshots/display-recovered.tga')]
        expected += read_commands + [('testGUI',), ('wait','6'), ('screenshot','screenshots/display-gameplay.tga'), ('gfxInfo',), ('echo', MARKER), ('quit',)]
        self.assertEqual([tuple(shlex.split(line)) for line in capture.SCRIPT.splitlines()], expected)
        self.assertEqual(capture.script_for_renderer('vulkan'),capture.SCRIPT)
        self.assertEqual(capture.script_for_renderer('gl'),capture.SCRIPT.replace('apply 960 540 0 0','apply 960 540 0 4'))
        # The engine lexer splits unquoted hyphenated arguments; shlex does not.
        self.assertIn('rendererDisplayProbe "missing-display"\n', capture.SCRIPT)
        self.assertEqual(capture.FIXTURE, ROOT / 'tools/ui/fixtures/display-device-smoke.q4ui')
        self.assertFalse(any(char in capture.SCRIPT for char in (';', '\r', '\x00')))

    def test_cli_rejects_arbitrary_script_commands_and_backend_values(self):
        base = ['capture_display_device.py', '--mode', 'sp', '--renderer', 'gl', '--assets', '.', '--output', '.tmp/unlaunched']
        for arguments in (['--script', 'exec.cfg'], ['--command', 'quit'], ['--fullscreen'],
                          ['--renderer', 'other'], ['--mode', 'other']):
            with self.subTest(arguments=arguments), patch.object(sys, 'argv', base + arguments), \
                    patch.object(capture.subprocess, 'Popen') as launch, contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(ROOT / 'tools/ui/capture_display_device.py'), run_name='__main__')
            self.assertEqual(caught.exception.code, 2)
            launch.assert_not_called()

    def test_capture_preserves_evidence_and_requires_staged_assets_before_launch(self):
        with tempfile.TemporaryDirectory(prefix='display-oracle-', dir=ROOT / '.tmp') as directory:
            root = Path(directory)
            args = SimpleNamespace(output=root, assets=root / 'absent', mode='sp', renderer='gl')
            with patch.object(capture.subprocess, 'Popen') as launch:
                with self.assertRaisesRegex(ValueError, 'new output'):
                    capture.capture(args)
                args.output = root / 'new'
                with self.assertRaisesRegex(ValueError, 'Staged client'):
                    capture.capture(args)
                launch.assert_not_called()

    def test_mocked_launch_forces_hidden_window_input_off_and_retains_gameplay_profile(self):
        for mode, renderer in (('sp', 'gl'), ('mp', 'vulkan')):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix='display-oracle-', dir=ROOT / '.tmp') as directory:
                root = Path(directory)
                runtime = root / '.install'
                runtime.mkdir()
                executable = runtime / ('openQ4-client_x64.exe' if capture.os.name == 'nt' else 'openQ4-client_x64')
                executable.write_bytes(b'fake binary; never executed')
                assets = root / 'assets'
                (assets / 'q4base').mkdir(parents=True)
                (root / '.vscode').mkdir()
                gameplay = ['+spawnServer', 'mp/q4dm1'] if mode == 'mp' else ['+map', 'game/airdefense1']
                profile = {'name': '(MP) q4dm1 test GL' if mode == 'mp' else '(SP) airdefense1 test GL',
                           'args': ['+set', 'R_FULLSCREEN', '1', '+seta', 'r_hiddenWindow', '0',
                                    '+set', 'in_mouse', '1', '+set', 'ui_autoJoin', '99', '+set', 'r_renderApi', 'other'] + gameplay}
                (root / '.vscode/launch.json').write_text(json.dumps({'configurations': [profile]}), encoding='utf-8')
                output = root / 'evidence'
                args = SimpleNamespace(output=output, assets=assets, mode=mode, renderer=renderer)
                calls = []

                def fake_launch(command, **kwargs):
                    calls.append((command, kwargs))
                    game = output / 'save/baseoq4'
                    (game / 'logs').mkdir()
                    rows = observations()
                    if renderer == 'gl': rows[1]['samples'] = rows[2]['samples'] = '4'
                    (game / 'logs/openq4.log').write_text(trace(rows).replace('active=gl', 'active=' + renderer), encoding='utf-8')
                    (game / 'screenshots').mkdir()
                    for name, width, height in (('applied', 960, 540), ('restored', 1280, 720), ('recovered', 1280, 720), ('gameplay', 1280, 720)):
                        header = bytearray(18)
                        header[2] = 2
                        header[16] = 24
                        struct.pack_into('<HH', header, 12, width, height)
                        (game / f'screenshots/display-{name}.tga').write_bytes(header + b'\x20\x30\x40' * width * height)
                    return SimpleNamespace(pid=1234, wait=lambda timeout: 0)

                with patch.object(capture, 'ROOT', root), patch.object(capture.subprocess, 'Popen', side_effect=fake_launch), \
                        contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(capture.capture(args), 0)
                self.assertEqual(len(calls), 1)
                command, kwargs = calls[0]
                values = {command[i + 1].lower(): command[i + 2] for i in range(len(command) - 2)
                          if command[i].lower() in ('+set', '+seta')}
                for key, value in {'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0',
                                   'r_hiddenWindow': '1', 'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
                                   'r_renderApi': renderer, 'ui_autoJoin': '1' if mode == 'mp' else '0',
                                   'g_autoExecAfterMapLoad': 'display-device.cfg', 'r_windowWidth': '1280',
                                   'r_windowHeight': '720'}.items():
                    self.assertEqual(values[key.lower()], value)
                self.assertEqual(command[-2:], gameplay)
                self.assertEqual(kwargs['cwd'], runtime)
                self.assertEqual((output / 'save/baseoq4/display-device.cfg').read_text(encoding='utf-8'), capture.script_for_renderer(renderer))
                metadata = json.loads((output / 'capture.json').read_text(encoding='utf-8'))
                self.assertEqual(metadata['status'], 'captured_pending_visual_review')
                self.assertFalse(metadata['host_input_injection'])
                self.assertFalse(metadata['qualification']['settings_confirmation_qualified'])


if __name__ == '__main__':
    (ROOT / '.tmp').mkdir(exist_ok=True)
    result = unittest.main(exit=False).result
    if result.wasSuccessful():
        print(f'display capture oracle: {DisplayOracleTests.negative_cases} negative mutations rejected; no renderer/game launch')
    raise SystemExit(0 if result.wasSuccessful() else 1)
