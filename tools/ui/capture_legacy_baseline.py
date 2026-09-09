#!/usr/bin/env python3
"""Capture initial SP/MP GUI references through the engine screenshot command.

Uses the matching VS Code map launch profile with an isolated savepath, hidden
window and disabled mouse/controller input. Requires the guarded current UI
branch build; this is a reference capture, not a replacement acceptance test.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import legacy_import
import system_settings_probe
import display_settings_probe

ROOT = Path(__file__).resolve().parents[2]
ALIAS_FIXTURE = ROOT / 'tools/ui/fixtures/presentation-alias-smoke'
EVENT_FIXTURE = ROOT / 'tools/ui/fixtures/event-program-smoke'


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def interaction_script(path: Path, *, managed: bool = False, observe_only: bool = False) -> str:
    """Allow semantic runtime operations and bounded waits; no device commands."""
    source = path.read_text(encoding='utf-8')
    if len(source) > 32768:
        raise ValueError('retained interaction script exceeds 32 KiB')
    if any((ord(char) < 32 and char not in '\r\n') or ord(char) == 127 for char in source):
        raise ValueError('retained interaction script contains control characters')
    # The engine lexer splits punctuation in unquoted IDs (notably '-').
    identifier = r'"[A-Za-z0-9_.-]{1,128}"'
    alias = r'"[A-Za-z0-9_.-]{1,128}(?:::[A-Za-z0-9_.-]{1,128})?"'
    number = r'-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?'
    patterns = [rf'ui_retained(?:Focus|State) {identifier}', rf'ui_retainedEnabled {identifier} [01]',
                rf'ui_retainedValue {identifier} {identifier}', r'ui_retainedData "retained-data/[A-Za-z0-9_-]{1,64}\.json"',
                rf'ui_retainedModal push {identifier}', r'ui_retainedModal pop', r'ui_retainedEvents',
                r'ui_retainedCheckpoint (?:save|restore)',
                r'ui_retainedMenu (?:next|previous|up|down|left|right|accept|back) [01]', r'wait [1-9][0-9]{0,2}']
    if managed:
        patterns = [r'openq4_retainedGui report',
                    rf'openq4_retainedGui inspect {identifier}',
                    rf'openq4_guiGet {alias}',
                    r'wait [1-9][0-9]{0,2}']
        if not observe_only:
            literal = rf'"(?:true|false|{number})"'
            presentation = rf'"(?:true|false|#str_[A-Za-z0-9_.-]+|{number}(?: +{number}){{0,3}}|{number}(?:,{number}){{1,3}})"'
            patterns += [rf'openq4_retainedGui focus {identifier}',
                         r'openq4_retainedGui menu (?:next|previous|up|down|left|right|accept|back) [01]',
                         rf'openq4_retainedGui (?:state|pending) {identifier} {literal}',
                         rf'openq4_retainedGui event {alias}',
                         rf'openq4_retainedGui presentation {alias} {presentation} [01]',
                         r'openq4_retainedGui (?:save|restore|update|trigger)']
    lines = [line.strip() for line in source.splitlines() if line.strip() and not line.strip().startswith('//')]
    if len(lines) > 256 or any(not any(re.fullmatch(pattern, line) for pattern in patterns) for line in lines):
        raise ValueError('retained script must contain only semantic control commands and bounded waits')
    if sum(int(line.split()[1]) for line in lines if line.startswith('wait ')) > 3600:
        raise ValueError('retained script waits exceed 3600 frames')
    for line in lines:
        if line.startswith('openq4_retainedGui event ') and line.split('"')[1].lower().startswith('gui::'):
            raise ValueError('managed event names cannot use the reserved gui:: namespace')
        if line.startswith(('openq4_retainedGui state ', 'openq4_retainedGui pending ', 'openq4_retainedGui presentation ')):
            value = re.findall(r'"([^"]*)"', line)[1]
            if value not in ('true', 'false') and not value.startswith('#str_'):
                if any(not math.isfinite(float(part)) or abs(float(part)) > 1e12 for part in re.split(r'[, ]+', value)):
                    raise ValueError('managed numeric value exceeds the canonical finite +/-1e12 limit')
    return '\n'.join(lines)+'\n'


def retained_commands(args: argparse.Namespace, staged_name: str) -> tuple[str, str, int]:
    """Build the captured semantic script without starting a process."""
    settle = max(30, args.profile_frames + 2)
    script = interaction_script(args.retained_script, managed=args.retained_managed) if args.retained_script else ''
    if getattr(args, 'display_settings_probe', False):
        display_settings_probe.sources(args)
        script = display_settings_probe.inject_screenshots(script)
    if args.retained_managed:
        peer = f'ui_retainedPreview "{staged_name}"\n' if args.retained_peer else ''
        commands = 'ui_retainedOwnership\n' + peer + f'testGUI "{staged_name}"\nwait 2\nui_retainedOwnership\n'
        commands += script + f'wait {settle}\nopenq4_retainedGui report\nui_retainedOwnership\n'
        # Managed instances must survive without action/setup replay. The same
        # read-only resume checks run after each separate resource barrier.
        resume = interaction_script(args.retained_resume_script, managed=True, observe_only=True) if args.retained_resume_script else 'openq4_retainedGui report\n'
        for enabled, command in ((args.language_reload, 'reloadLanguage'), (args.video_restart, 'vid_restart windowed')):
            if enabled:
                commands += command + '\nwait 2\n' + resume + f'wait {settle}\nui_retainedOwnership\n'
        # Observe both contexts through the resets, then remove the overlay so
        # the engine screenshot shows this managed instance's actual focus.
        if args.retained_peer:
            commands += 'ui_retainedClose\nwait 2\nopenq4_retainedGui report\nui_retainedOwnership\n'
        return commands, 'testGUI\nwait 3\nui_retainedOwnership\n', settle
    play = f'ui_retainedPlay "{args.timeline}"\n' if args.timeline else ''
    profile = f'ui_retainedProfile {args.profile_frames}\n' if args.profile_frames else ''
    script = 'wait 2\n' + script if script else ''
    begin = 'wait 2\nui_retainedOwnership\n' if args.retained_open else ''
    end = 'ui_retainedOwnership\n' if args.retained_open else ''
    loader = 'ui_retainedOpen' if args.retained_open else 'ui_retainedPreview'
    commands = f'{loader} "{staged_name}"\n' + begin + profile + play + script + f'wait {settle}\n' + end
    if args.language_reload:
        commands += 'reloadLanguage\nwait 2\n'
    if args.video_restart:
        resume = interaction_script(args.retained_resume_script) if args.retained_resume_script else play + script
        commands += 'vid_restart windowed\nwait 2\n' + begin + profile + resume + f'wait {settle}\n' + end
    close = 'ui_retainedClose\nwait 3\nui_retainedOwnership\n' if args.retained_open else ''
    return commands, close, settle


def managed_evidence(log: str, commands: str, *, peer: bool, resource_resets: int, settings_fixture: bool,
                     mode: str | None = None, alias_fixture: bool = False, event_fixture: bool = False,
                     initial_brightness: float = 1) -> dict:
    """Check actual adapter results; an image alone does not qualify actions."""
    lines = log.splitlines()
    errors = []
    trace = [line for line in lines if line.startswith(('RETAINED_GUI', 'GUI_VALUE ', 'Retained UI ownership:'))]
    reports = []
    resource_epoch = observation_segment = 0
    observed_revision = None
    pattern = r'RETAINED_GUI path=(\S+) focus=(\S*) revision=([0-9]+) active=([01]) brightness=(\S+) shadows=([01]) contexts=([0-9]+)'
    for line in lines:
        if line == 'RETAINED_GUI_RESOURCE path=retained-smoke.q4ui event=restored':
            resource_epoch += 1
            observation_segment += 1
            observed_revision = None
        elif re.fullmatch(r'RETAINED_GUI_OPERATION (?:menu|state|restore|presentation|update|event|trigger) passed', line):
            observation_segment += 1
            observed_revision = None
        if not line.startswith('RETAINED_GUI '):
            continue
        match = re.fullmatch(pattern, line)
        if not match:
            errors.append('malformed managed report')
            continue
        path, focus, revision, active, brightness, shadows, contexts = match.groups()
        try:
            number = float(brightness)
        except ValueError:
            number = math.nan
        if not math.isfinite(number):
            errors.append('non-finite managed brightness readback')
            number = None
        revision = int(revision)
        # StateRevision belongs to the current runtime epoch; snapshots do not
        # serialize it. Only observations without intervening state/action
        # work or a successful resource recreation share a revision contract.
        if (settings_fixture or alias_fixture or event_fixture) and observed_revision is not None and revision != observed_revision:
            errors.append('managed state revision changed within a read-only resource-epoch segment')
        observed_revision = revision
        reports.append(dict(path=path, focus=focus, revision=revision, active=int(active), brightness=number,
                            shadows=int(shadows), contexts=int(contexts), resource_epoch=resource_epoch,
                            observation_segment=observation_segment))
    operations = [line for line in lines if line.startswith('RETAINED_GUI_OPERATION ')]
    expected_operations = ['RETAINED_GUI_OPERATION ' + match.group(1) + ' passed'
        for match in re.finditer(r'^openq4_retainedGui (focus|menu|state|pending|save|restore|presentation|update|event|trigger)(?: |$)', commands, re.MULTILINE)]
    if operations != expected_operations:
        errors.append('managed operation results do not match the submitted script or an operation failed')
    expected_reports = len(re.findall(r'^openq4_retainedGui report$', commands, re.MULTILINE))
    if not reports or len(reports) != expected_reports:
        errors.append('managed report count differs from the submitted script')
    loaded = [line for line in lines if line.startswith('RETAINED_GUI_LOADED ')]
    if loaded != ['RETAINED_GUI_LOADED retained-smoke.q4ui']:
        errors.append('normal manager did not load exactly one retained fixture')
    resources = [line for line in lines if line.startswith('RETAINED_GUI_RESOURCE ')]
    if resources != ['RETAINED_GUI_RESOURCE path=retained-smoke.q4ui event=restored'] * resource_resets:
        errors.append('managed resource restoration did not complete once per requested reset')
    for index, report in enumerate(reports):
        expected_contexts = 1 if not peer or index == len(reports)-1 else 2
        if report['path'] != 'retained-smoke.q4ui' or report['active'] != 1 or report['contexts'] != expected_contexts:
            errors.append('managed owner, activity or peer context count changed unexpectedly')
            break
    if peer and 'Retained UI preview loaded:' not in log:
        errors.append('peer preview was not loaded')
    reads = re.findall(r'^GUI_VALUE ([^=]+)=(.*)$', log, re.MULTILINE)
    expected_reads = re.findall(r'^openq4_guiGet "([^"]+)"$', commands, re.MULTILINE)
    managed_reads = [(name, value) for name, value in reads if name in expected_reads]
    if [name for name, _ in managed_reads] != expected_reads:
        errors.append('managed presentation readback sequence differs from the submitted script')
    if any(line.startswith(('openq4_retainedGui:', 'usage: openq4_retainedGui', 'usage: openq4_guiGet')) for line in lines):
        errors.append('managed diagnostic command was rejected')
    ownership = []
    for line in lines:
        if not line.startswith('Retained UI ownership:'):
            continue
        match = re.fullmatch(r'Retained UI ownership: open=([01]) suspended=([01]) session_gui=([01]) game_time=(-?[0-9]+) requests=([0-9]+)', line)
        if not match:
            errors.append('malformed managed session ownership observation')
            continue
        ownership.append(dict(zip(('open', 'suspended', 'session_gui', 'game_time', 'requests'), map(int, match.groups()))))
    expected_ownership = len(re.findall(r'^ui_retainedOwnership$', commands, re.MULTILINE))
    ownership_passed = None
    if expected_ownership or mode is not None:
        ownership_passed = len(ownership) == expected_ownership and len(ownership) >= 4
        if ownership_passed:
            ownership_passed = all(row['open'] == 0 and row['requests'] == 0 and row['game_time'] >= 0 for row in ownership)
            ownership_passed = ownership_passed and ownership[0]['session_gui'] == 0 and ownership[-1]['session_gui'] == 0
            ownership_passed = ownership_passed and all(row['session_gui'] == 1 for row in ownership[1:-1])
            times = [row['game_time'] for row in ownership[1:-1]]
            if mode == 'sp':
                ownership_passed = ownership_passed and len(set(times)) == 1
            elif mode == 'mp':
                ownership_passed = ownership_passed and times[1] > times[0] and all(a <= b for a, b in zip(times, times[1:]))
            ownership_passed = ownership_passed and ownership[-1]['game_time'] > times[-1]
        if not ownership_passed:
            errors.append('managed session ownership or SP pause/resume / MP continuation contract failed')
    if settings_fixture:
        # Five scripted reports establish both settings mutations and restore;
        # every subsequent report must observe the same final live host values.
        expected_host = [(initial_brightness, 1), (1.5, 1), (1.5, 0), (1.25, 1), (1.25, 1)]
        expected_host += [(1.25, 1)] * max(0, len(reports)-5)
        if len(reports) < 6 or any(row['brightness'] is None or
                not math.isclose(row['brightness'], value, abs_tol=1e-6) or row['shadows'] != shadows
                for row, (value, shadows) in zip(reports, expected_host)):
            errors.append('managed settings host readback or non-replaying restore contract failed')
        if len(reports) >= 5 and any(r['focus'] != 'managed-shadows' for r in reports[2:]):
            errors.append('managed focus did not survive settings save/restore and resource reload')
        expected_values = [('brightness-target-reading::text', '1.25'), ('shadows-target-reading::text', '1')]
        restored_values = [('brightness-reading::text', '1.25'), ('shadows-reading::text', '#str_200157'),
                           ('brightness-target-reading::text', '1.50'), ('shadows-target-reading::text', '0')]
        expected_values += restored_values * (1 + resource_resets)
        if managed_reads != expected_values:
            errors.append('managed host bindings or restored application presentation values differ')
    alias_contract = None
    if alias_fixture:
        alias_contract = presentation_alias_evidence(log, commands, reports, managed_reads,
                                                     resource_resets=resource_resets, brightness=initial_brightness)
        errors.extend(alias_contract['errors'])
    event_contract = None
    if event_fixture:
        event_contract = event_program_evidence(log, commands, reports, managed_reads, resource_resets=resource_resets)
        errors.extend(event_contract['errors'])
    return {'passed': not errors, 'errors': errors, 'managed_trace': trace, 'reports': reports,
            'operations': operations, 'resource_events': resources, 'presentation_values': managed_reads,
            'ownership': ownership, 'ownership_passed': ownership_passed, 'session_mode': mode,
            'settings_fixture_contract': settings_fixture, 'presentation_alias_contract': alias_contract,
            'event_program_contract': event_contract,
            'replacement_acceptance': False}


def presentation_alias_sources(args: argparse.Namespace) -> dict:
    """A named probe qualifies only its exact canonical source and scripts."""
    sources = {}
    for attribute, suffix in (('retained_document', '.q4ui'), ('retained_script', '.cfg'),
                              ('retained_resume_script', '-resume.cfg')):
        canonical = Path(str(ALIAS_FIXTURE) + suffix)
        supplied = getattr(args, attribute)
        if attribute == 'retained_resume_script' and not (args.language_reload or args.video_restart):
            continue
        if supplied is None or digest(supplied) != digest(canonical):
            raise ValueError('--presentation-alias-probe requires the exact presentation-alias-smoke document and scripts')
        sources[attribute] = {'source': str(supplied), 'sha256': digest(supplied)}
    return {'id': 'presentation-alias-smoke-v1', 'sources': sources}


def event_program_sources(args: argparse.Namespace) -> dict:
    """Event acceptance is scoped to the exact document/setup/resume bytes."""
    sources = {}
    for attribute, suffix in (('retained_document', '.q4ui'), ('retained_script', '.cfg'),
                              ('retained_resume_script', '-resume.cfg')):
        canonical = Path(str(EVENT_FIXTURE) + suffix)
        supplied = getattr(args, attribute)
        if attribute == 'retained_resume_script' and not (args.language_reload or args.video_restart):
            continue
        if supplied is None or digest(supplied) != digest(canonical):
            raise ValueError('--event-program-probe requires the exact event-program-smoke document and scripts')
        sources[attribute] = {'source': str(supplied), 'sha256': digest(supplied)}
    return {'id': 'event-program-smoke-v1', 'sources': sources}


def event_program_expected_values(resource_resets: int) -> list[tuple[str, str | list[float]]]:
    """Independent values for the fixed event sequence, including restore."""
    def final(clicks: int, brightness: str) -> list[tuple[str, str | list[float]]]:
        return [('activated', [1]), ('initialized', [1]), ('triggered', [1]), ('eventsSeen', [1]),
                ('nested', [1]), ('blocked', [1]), ('controls', [1]), ('brightnessClicks', [clicks]),
                ('draftReading', [14]), ('seenPending', [13]), ('seenWidth', [432]),
                ('curr', [15]), ('desktop::curr', [15]), ('brightness-request', [.75]),
                ('brightness-target-reading::text', '1.75'), ('panel::rect', [32, 32, 432, 200]),
                ('title::color', [1, .875, .5, 1]), ('title::text', '#str_200009'),
                ('panel::visible', [1]), ('panel::noevents', [0]),
                ('brightness-reading::text', brightness), ('shadows-reading::text', '#str_200158')]
    initial = [('activated', [1]), ('initialized', [1]), ('triggered', [0]), ('draftReading', [2]), ('curr', [2]),
               ('draftReading', [2]), ('seenPending', [13]), ('draftReading', [14]), ('seenWidth', [432]),
               ('curr', [14]), ('desktop::curr', [14]), ('eventsSeen', [1]), ('nested', [1]),
               ('brightness-request', [.75]), ('shadows-target-reading::text', '1'),
               ('panel::rect', [32, 32, 432, 200]), ('title::color', [1, .875, .5, 1]),
               ('blocked', [1]), ('controls', [0]), ('blocked', [1]), ('controls', [1]),
               ('brightnessClicks', [1]), ('brightness-target-reading::text', '1.75'),
               ('triggered', [1]), ('curr', [15])]
    disturbed = [(name, [6]) for name in ('activated', 'initialized', 'triggered', 'eventsSeen',
                                         'nested', 'blocked', 'controls', 'brightnessClicks')]
    disturbed += [('draftReading', [42]), ('curr', [99]), ('panel::rect', [48, 32, 432, 200]), ('brightness-request', [1.5])]
    return initial + disturbed + final(1, '1.50') + final(2, '1.25') * (1 + resource_resets)


EVENT_PROGRAM_TRACE = [('onActivate', 1, 2), ('onInit', 0, 1), ('Probe::Main', 3, 7),
                       ('control::shadows', 0, 1), ('control::shadows', 1, 2),
                       ('control::brightness', 1, 2), ('onTrigger', 0, 1),
                       ('probe::disturb', 2, 13), ('control::brightness', 1, 2), ('onDeactivate', 2, 2)]
EVENT_PROGRAM_DISPATCH = [('settings.brightness.set', 1.25, 1.25, 1, 0),
                          ('settings.brightness.set', 1.5, 1.5, 1, 0),
                          ('settings.brightness.set', 1.75, 1.75, 1, 0),
                          ('settings.shadows.set', 0, 1.75, 0, 0),
                          ('settings.shadows.set', 1, 1.75, 1, 0),
                          ('settings.brightness.set', 1.25, 1.25, 1, 0),
                          ('settings.brightness.set', 1.5, 1.5, 1, 0),
                          ('settings.shadows.set', 0, 1.5, 0, 0),
                          ('settings.brightness.set', 1.25, 1.25, 0, 0),
                          ('settings.brightness.set', 1, 1, 0, 0), ('ui.dismiss', '-', 1, 0, 1)]


def event_program_evidence(log: str, commands: str, reports: list[dict], reads: list[tuple[str, str]],
                           *, resource_resets: int) -> dict:
    errors = []
    expected_values = event_program_expected_values(resource_resets)
    matches = len(reads) == len(expected_values)
    for (name, value), (target, wanted) in zip(reads, expected_values):
        if name != target:
            matches = False
        if isinstance(wanted, str):
            matches = matches and value == wanted
        else:
            try:
                numbers = [float(part) for part in re.split(r'[,\s]+', value.strip())]
                matches = matches and len(numbers) == len(wanted) and all(
                    math.isfinite(a) and math.isclose(a, b, rel_tol=1e-9, abs_tol=1e-9) for a, b in zip(numbers, wanted))
            except ValueError:
                matches = False
    if not matches:
        errors.append('event values violate pending/batch order, shared presentation, conditional execution or non-replaying save/reload contract')
    expected_host = [(1.25, 1), (1.75, 0), (1.75, 0), (1.75, 1), (1.25, 1), (1.5, 0), (1.5, 0), (1.25, 0)]
    expected_host += [(1.25, 0)] * max(0, len(reports)-8)
    if len(reports) < 9 or any(row['brightness'] is None or
            not math.isclose(row['brightness'], brightness, abs_tol=1e-6) or row['shadows'] != shadows
            for row, (brightness, shadows) in zip(reports, expected_host)):
        errors.append('event host readbacks violate ordered action capture, conditional control or restore-with-live-host contract')
    if any(row['focus'] != 'managed-shadows' for row in reports[2:4]) or any(
            row['focus'] != 'managed-brightness' for row in reports[4:]):
        errors.append('event control focus did not survive activation, save/restore and resource recreation')
    lines = log.splitlines()
    event_lines = [line for line in lines if line.startswith('RETAINED_GUI_EVENT ')]
    expected_events = [f'RETAINED_GUI_EVENT name={name} actions={actions} writes={writes}'
                       for name, actions, writes in EVENT_PROGRAM_TRACE]
    if event_lines != expected_events:
        errors.append('event commit trace differs: lifecycle replay, missing program, unexpected branch or incorrect action/write count')
    dispatch = []
    dispatch_lines = [line for line in lines if line.startswith('RETAINED_GUI_DISPATCH ')]
    for line in dispatch_lines:
        match = re.fullmatch(r'RETAINED_GUI_DISPATCH path=(\S+) operation=(\S+) value=(\S+) brightness=(\S+) shadows=([01]) close=([01])', line)
        if match:
            path, operation, value, brightness, shadows, close = match.groups()
            try:
                item = (operation, value if value == '-' else float(value), float(brightness), int(shadows), int(close))
                if path == 'retained-smoke.q4ui' and math.isfinite(item[2]) and (value == '-' or math.isfinite(item[1])):
                    dispatch.append(item)
                    continue
            except ValueError:
                pass
        errors.append('malformed or non-finite event application dispatch trace')
    if dispatch != EVENT_PROGRAM_DISPATCH:
        errors.append('event application dispatch values/order or final deactivation brightness/dismissal differ')
    # Program traces belong to their initiating semantic command, never to
    # restore or resource recreation. Dispatch itself may wait for the pump.
    expected_order = []
    focus = ''
    for line in commands.splitlines():
        if line.startswith('openq4_guiGet '):
            expected_order.append(('read', line.split('"')[1]))
        elif line.startswith('openq4_retainedGui '):
            verb = line.split()[1]
            if verb == 'focus':
                focus = line.split('"')[1]
            if verb == 'event':
                expected_order.append(('event', line.split('"')[1]))
            elif verb == 'trigger':
                expected_order.append(('event', 'onTrigger'))
            elif line == 'openq4_retainedGui menu accept 0':
                expected_order.append(('event', 'control::' + focus.removeprefix('managed-')))
            expected_order.append(('report', '') if verb == 'report' else ('operation', verb))
    actual_order = []
    for line in lines:
        if line.startswith('GUI_VALUE '):
            actual_order.append(('read', line[len('GUI_VALUE '):].split('=', 1)[0]))
        elif line.startswith('RETAINED_GUI_OPERATION '):
            actual_order.append(('operation', line.split()[1]))
        elif line.startswith('RETAINED_GUI path='):
            actual_order.append(('report', ''))
        elif line.startswith('RETAINED_GUI_EVENT '):
            name = line.split()[1].removeprefix('name=')
            if name not in ('onActivate', 'onInit', 'onDeactivate'):
                actual_order.append(('event', name))
    if actual_order != expected_order:
        errors.append('event commits, semantic results and readbacks occurred out of the submitted order')
    # Require the outgoing lifecycle and its host effects after the rendered
    # capture, then prove the session resumed via the shared ownership oracle.
    # Engine echo appends a space. Accept surrounding horizontal whitespace,
    # while requiring one standalone marker rather than a substring match.
    markers = [i for i, line in enumerate(lines) if line.strip(' \t') == 'UI_BASELINE_CAPTURE_COMPLETE']
    first_report = next((i for i, line in enumerate(lines) if line.startswith('RETAINED_GUI path=')), -1)
    lifecycle_valid = len(markers) == 1 and event_lines == expected_events and len(dispatch_lines) == 11
    if lifecycle_valid:
        lifecycle_valid = (lines.index(expected_events[0]) < lines.index(expected_events[1]) < first_report < markers[0]
            < lines.index(expected_events[-1]) < lines.index(dispatch_lines[-2]) < lines.index(dispatch_lines[-1]))
    if not lifecycle_valid:
        errors.append('event lifecycle initialization or completed outgoing deactivation was not observed at the capture boundary')
    return {'id': 'event-program-smoke-v1', 'passed': not errors, 'errors': errors,
            'expected_values': expected_values, 'event_trace': event_lines, 'dispatch_trace': dispatch_lines,
            'resource_recreations': resource_resets, 'outgoing_lifecycle_passed': lifecycle_valid,
            'replacement_acceptance': False}


def presentation_alias_expected_values(resource_resets: int, brightness: float) -> list[tuple[str, str | list[float]]]:
    """Independent acceptance values for the fixed authored probe sequence."""
    # The engine CVar exposes a float before the canonical number-text binding.
    host_brightness = struct.unpack('<f', struct.pack('<f', brightness))[0]
    final = [('curr', [11]), ('desktop::curr', [11]), ('panel::opacity', [.875]),
             ('panel::rect', [32, 32, 432, 200]), ('title::color', [1, .875, .5, 1]),
             ('title::text', '#str_200009'), ('panel::visible', [1]), ('panel::noevents', [0]),
             ('shadows-reading::text', '#str_200158'), ('brightness-reading::text', f'{host_brightness:.2f}')]
    setup = [('curr', [2]), ('desktop::curr', [2]), ('curr', [3]), ('curr', [7]), ('desktop::curr', [7]),
             ('curr', [3]), ('curr', [9]), ('curr', [11]), ('desktop::curr', [11]),
             ('panel::opacity', [1]), ('panel::opacity', [.25]), ('panel::opacity', [1]),
             ('panel::opacity', [.75]), ('panel::opacity', [.875]),
             ('panel::rect', [32, 32, 432, 200]), ('title::color', [1, .875, .5, 1]),
             ('title::text', '#str_200148'), ('title::text', '#str_200009'),
             ('panel::visible', [0]), ('panel::visible', [1]), ('panel::noevents', [1]),
             ('panel::noevents', [0]), ('shadows-reading::text', '#str_200158'),
             ('curr', [17]), ('panel::opacity', [.5]), ('panel::rect', [24, 32, 432, 200]),
             ('title::color', [.5, .5, .5, 1]), ('title::text', '#str_200148')]
    return setup + final * (1 + resource_resets)


def presentation_alias_evidence(log: str, commands: str, reports: list[dict], reads: list[tuple[str, str]],
                                *, resource_resets: int, brightness: float) -> dict:
    errors = []
    expected = presentation_alias_expected_values(resource_resets, brightness)
    matches = len(reads) == len(expected)
    for (name, value), (target, wanted) in zip(reads, expected):
        if name != target:
            matches = False
        if isinstance(wanted, str):
            matches = matches and value == wanted
        else:
            try:
                numbers = [float(part) for part in re.split(r'[,\s]+', value.strip())]
                matches = matches and len(numbers) == len(wanted) and all(
                    math.isfinite(a) and math.isclose(a, b, rel_tol=1e-9, abs_tol=1e-9) for a, b in zip(numbers, wanted))
            except ValueError:
                matches = False
    if not matches:
        errors.append('presentation alias values violate shared/transient/explicit ownership, tuple or save/reload contract')
    # First the armed action is cancelled by its parent, then a fresh allowed
    # activation changes shadows. This probe never changes host brightness.
    if len(reports) < 5 or [row['shadows'] for row in reports] != [1, 1] + [0] * (len(reports)-2):
        errors.append('presentation parent noevents did not suppress the armed action before a fresh allowed activation')
    if any(row['brightness'] is None or not math.isclose(row['brightness'], brightness, abs_tol=1e-6) for row in reports):
        errors.append('presentation probe changed the requested host brightness')
    if any(row['focus'] != 'managed-shadows' for row in reports[2:]):
        errors.append('presentation probe focus did not survive save and same-source resource recreation')
    expected_order = []
    for line in commands.splitlines():
        if line.startswith('openq4_guiGet '):
            expected_order.append(('read', line.split('"')[1]))
        elif line.startswith('openq4_retainedGui '):
            verb = line.split()[1]
            expected_order.append(('report', '') if verb == 'report' else ('operation', verb))
    actual_order = []
    for line in log.splitlines():
        if line.startswith('GUI_VALUE '):
            actual_order.append(('read', line[len('GUI_VALUE '):].split('=', 1)[0]))
        elif line.startswith('RETAINED_GUI_OPERATION '):
            actual_order.append(('operation', line.split()[1]))
        elif line.startswith('RETAINED_GUI path='):
            actual_order.append(('report', ''))
    if actual_order != expected_order:
        errors.append('presentation alias reads, writes and reports occurred out of the submitted order')
    return {'id': 'presentation-alias-smoke-v1', 'passed': not errors, 'errors': errors,
            'expected_brightness': brightness, 'resource_recreations': resource_resets,
            'expected_values': expected, 'replacement_acceptance': False}


def capture(args: argparse.Namespace) -> int:
    alias_sources = presentation_alias_sources(args) if args.presentation_alias_probe else None
    event_sources = event_program_sources(args) if args.event_program_probe else None
    system_sources = system_settings_probe.sources(args) if args.system_settings_probe else None
    display_sources = display_settings_probe.sources(args) if getattr(args, 'display_settings_probe', False) else None
    output = args.output.resolve()
    if output.exists():
        raise ValueError('use a new output directory to preserve previous capture evidence')
    runtime = args.runtime.resolve()
    executable = runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    if not executable.is_file():
        raise ValueError(f'missing staged client: {executable}')
    configs = json.loads((ROOT / '.vscode/launch.json').read_text(encoding='utf-8-sig'))['configurations']
    prefix = '(SP) airdefense1 ' if args.mode == 'sp' else '(MP) q4dm1 '
    profile = next(c for c in configs if c['name'].startswith(prefix) and c['name'].endswith('GL'))
    savepath = output / 'save'
    game = savepath / 'baseoq4'
    game.mkdir(parents=True)
    data_files = []
    for path in args.retained_data:
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}\.json',path.name) or path.name.lower() in {row['name'].lower() for row in data_files}:
            raise ValueError('retained data files require unique simple .json names')
        contents = path.read_bytes()
        if len(contents) > 16*1024*1024:
            raise ValueError('retained data exceeds the canonical 16 MiB limit')
        (game/'retained-data').mkdir(exist_ok=True)
        (game/'retained-data'/path.name).write_bytes(contents)
        data_files.append({'name':path.name,'source':str(path),'sha256':hashlib.sha256(contents).hexdigest()})
    cfg_path = game / 'ui-baseline.cfg'
    presentation = ''
    presentation_sources = {}
    if args.presentation_probe:
        for extension in ('gui', 'cfg'):
            fixture = ROOT / f'tools/ui/fixtures/presentation-smoke.{extension}'
            presentation_sources[fixture.name] = digest(fixture)
            if extension == 'gui':
                (game / fixture.name).write_bytes(fixture.read_bytes())
            else:
                presentation = fixture.read_text(encoding='utf-8')
    import_requests = legacy_import.requests(args.legacy_export_list) if args.legacy_export_list else []
    preview = close = ''
    if args.retained_document:
        fixture = args.retained_document.resolve()
        staged_name = 'retained-smoke' + fixture.suffix.lower()
        (game / staged_name).write_bytes(fixture.read_bytes())
        preview, close, settle = retained_commands(args, staged_name)
    final_inspection = 'openq4_retainedGui inspect "settings-panel"\n' if display_sources else ''
    cfg_path.write_text(presentation + preview + legacy_import.commands(import_requests) + 'gfxInfo\nscreenshot "screenshots/ui-baseline.tga"\n' + final_inspection + 'echo UI_BASELINE_CAPTURE_COMPLETE\n' + close + 'quit\n', encoding='utf-8')
    overrides = {
        'fs_basepath': str(args.assets.resolve()), 'fs_savepath': str(savepath), 'fs_devpath': str(savepath),
        'fs_game': 'baseoq4', 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0',
        'r_borderlessDefaultMigrated': '1', 'r_hiddenWindow': '1',
        'r_windowWidth': str(args.width), 'r_windowHeight': str(args.height),
        'r_mode': '-1', 'r_customWidth': str(args.width), 'r_customHeight': str(args.height),
        'r_renderApi': args.renderer, 'r_rendererSharedGui': '1' if args.shared_gui else '0', 'r_rendererSharedInWorldGui': '0',
        'r_gamma': str(args.gamma), 'r_brightness': str(args.brightness),
        'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
        'g_autoScreenshot': '0', 'g_autoSkipCinematics': '1',
        'g_autoExecAfterMapLoad': 'ui-baseline.cfg', 'g_autoExecAfterMapLoadDelayMs': '3000',
        'com_skipLoadingContinue': '1', 'com_loadingContinueAutoAdvance': '1',
        'com_maxfps': '60', 'ui_autoJoin': '1' if args.mode == 'mp' else '0',
        'ui_retainedScale': str(args.ui_scale), 'ui_retainedDensity': str(args.density),
        'ui_retainedReducedMotion': '1' if args.reduced_motion else '0',
        'ui_retainedTrace': '1' if args.event_program_probe or args.system_settings_probe or display_sources or args.retained_trace else '0',
    }
    if args.retained_managed:
        # Isolated, explicit host defaults make the action/readback sequence
        # independent of archived settings in any interactive installation.
        overrides.update({'r_shadows': '1'})
    override_keys = {key.lower() for key in overrides}
    retained = []
    original = profile['args']
    i = 0
    while i < len(original):
        if original[i].lower() in ('+set', '+seta') and i + 2 < len(original):
            if original[i + 1].lower() not in override_keys:
                retained.extend(original[i:i + 3])
            i += 3
        else:
            retained.append(original[i])
            i += 1
    if display_sources:
        # This dimensions-only fixture must begin from the same supported
        # framebuffer on GL and Vulkan, independent of archived menu defaults.
        overrides['r_multiSamples'] = '0'
    command = [str(executable)]
    for key, value in overrides.items():
        command.extend(['+set', key, value])
    command.extend(value.replace('${workspaceFolder}', str(ROOT)) for value in retained)
    binaries = [executable] + sorted((runtime / 'baseoq4').glob('game-*'))
    binaries += sorted(runtime.glob('renderer-*'))
    metadata = {
        'status': 'running', 'profile': profile['name'], 'mode': args.mode, 'renderer': args.renderer,
        'requested_renderer_color': {'r_gamma': args.gamma, 'r_brightness': args.brightness},
        'capture_method': 'engine screenshot command after 3 seconds of active map drawing',
        'windowed': True, 'hidden_window': True, 'host_input_injection': False, 'shared_gui': args.shared_gui,
        'command': command, 'cwd': str(runtime), 'cfg_sha256': digest(cfg_path),
        'binaries': {str(p.relative_to(runtime)): digest(p) for p in binaries if p.is_file()},
    }
    report_path = output / 'capture.json'
    if args.retained_document:
        metadata['retained_preview'] = {'source': str(args.retained_document),
                                        'application_open': args.retained_open,
                                        'managed_application': args.retained_managed,
                                        'peer_preview': args.retained_peer,
                                        'loader': 'testGUI' if args.retained_managed else 'ui_retainedOpen' if args.retained_open else 'ui_retainedPreview',
                                        'sha256': digest(args.retained_document),
                                        'density_override': args.density, 'ui_scale': args.ui_scale,
                                        'settle_frames': settle, 'video_restart': args.video_restart,
                                        'language_reload': args.language_reload,
                                        'profile_frames': args.profile_frames,
                                        'timeline': args.timeline, 'reduced_motion': args.reduced_motion,
                                        'replacement_acceptance': False}
        if args.retained_script:
            metadata['retained_preview']['interaction_script'] = {'source': str(args.retained_script), 'sha256': digest(args.retained_script)}
        if args.retained_resume_script:
            metadata['retained_preview']['resume_script'] = {'source': str(args.retained_resume_script), 'sha256': digest(args.retained_resume_script)}
        metadata['retained_preview']['state_data'] = data_files
        if args.retained_managed:
            metadata['retained_preview']['managed_initial_host_state'] = {'r_brightness': args.brightness, 'r_shadows': True}
            metadata['retained_preview']['resource_resume_replays_setup'] = False
            if alias_sources:
                metadata['retained_preview']['presentation_alias_probe'] = alias_sources
            if event_sources:
                metadata['retained_preview']['event_program_probe'] = event_sources
            if display_sources:
                metadata['retained_preview']['display_settings_probe'] = display_sources
            if args.retained_peer:
                metadata['retained_preview']['peer_source'] = {'source': str(args.retained_document),
                    'sha256': digest(args.retained_document), 'loader': 'ui_retainedPreview', 'closed_after_screenshot': False}

    def save_report():
        report_path.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')

    save_report()
    started = time.monotonic()
    with (output / 'process.log').open('w', encoding='utf-8') as process_log:
        kwargs = {}
        if os.name == 'nt':
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            kwargs = {'startupinfo': startup, 'creationflags': subprocess.CREATE_NO_WINDOW}
        process = subprocess.Popen(command, cwd=runtime, stdout=process_log, stderr=subprocess.STDOUT, **kwargs)
        print(f'Baseline {args.mode}/{args.renderer}: PID {process.pid}; hidden, windowed, mouse disabled.', flush=True)
        try:
            metadata['returncode'] = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            metadata['status'] = 'timeout'
            metadata['seconds'] = round(time.monotonic() - started, 3)
            save_report()
            return 1
    metadata['seconds'] = round(time.monotonic() - started, 3)
    screenshot = game / 'screenshots/ui-baseline.tga'
    log_path = game / 'logs/openq4.log'
    log = log_path.read_text(encoding='utf-8', errors='replace') if log_path.exists() else ''
    plain_log = re.sub(r'\^[0-9]', '', log)
    if import_requests:
        metadata['legacy_import'] = legacy_import.collect(game,plain_log,import_requests)
    diagnostics = [line for line in plain_log.splitlines() if 'WARNING:' in line or 'ERROR:' in line or 'FATAL:' in line]
    metadata['diagnostics'] = {'warnings': sum('WARNING:' in line for line in diagnostics),
                               'errors': sum('ERROR:' in line or 'FATAL:' in line for line in diagnostics)}
    valid = metadata['returncode'] == 0 and screenshot.is_file() and 'UI_BASELINE_CAPTURE_COMPLETE' in log
    active_apis = re.findall(r'Renderer API: requested=\S+ active=(\S+) disposition=(\S+)', plain_log)
    metadata['active_renderer'] = active_apis[-1][0] if active_apis else None
    valid = valid and metadata['active_renderer'] == args.renderer
    if args.presentation_probe:
        values = re.findall(r'^GUI_VALUE ([^=]+)=(.*)$', plain_log, re.MULTILINE)
        expected = [('probe', [1]), ('probe', [2]), ('desktop::probe', [2]), ('probe', [7]),
                    ('panel::visible', [1]), ('panel::visible', [0]), ('panel::rect', [80,80,480,320]),
                    ('panel::visible', [1])]
        try:
            measured = [(name, [float(v) for v in re.split(r'[,\s]+', value.strip())]) for name,value in values]
        except ValueError:
            measured = []
        missing_reads = plain_log.count('openq4_guiGet: unknown GUI variable')
        missing_writes = plain_log.count('openq4_guiSet: unknown GUI variable')
        passed = measured == expected and missing_reads == 1 and missing_writes == 1
        metadata['presentation_probe'] = {'sources':presentation_sources, 'values':values,
            'unknown_reads':missing_reads, 'unknown_writes':missing_writes, 'passed':passed}
        valid = valid and passed
    if import_requests:
        valid = valid and metadata['legacy_import']['passed']
    if args.retained_document:
        retained_diagnostics = [line for line in diagnostics if 'retained UI:' in line or 'retained GUI ' in line or '_retained' in line]
        retained_diagnostics += [line for line in plain_log.splitlines() if line.startswith('usage: ui_retained')]
        metadata['retained_preview']['diagnostics'] = retained_diagnostics
        profiles = [json.loads(line.split('Retained UI profile: ', 1)[1]) for line in plain_log.splitlines() if 'Retained UI profile: ' in line]
        metadata['retained_preview']['profiles'] = profiles
        metadata['retained_preview']['interaction_trace'] = [line for line in plain_log.splitlines()
            if line.startswith(('Retained UI control:', 'Retained UI action:', 'Retained UI actions:'))]
        metadata['retained_preview']['ownership_trace'] = [line for line in plain_log.splitlines() if line.startswith('Retained UI ownership:')]
        metadata['retained_preview']['binding_trace'] = [line for line in plain_log.splitlines() if line.startswith(('Retained UI value:', 'Retained UI data:', 'Retained UI bounds:'))]
        metadata['retained_preview']['snapshot_trace'] = [line for line in plain_log.splitlines()
            if line.startswith(('Retained UI checkpoint:', 'Retained UI instance restored after renderer/language change:'))]
        if args.profile_frames and (len(profiles) != (2 if args.video_restart else 1)
                                   or any(p.get('frames') != args.profile_frames for p in profiles)):
            retained_diagnostics.append('retained CPU profile did not complete for the requested frame count')
        if args.retained_managed:
            evidence = managed_evidence(plain_log, preview + close, peer=args.retained_peer,
                resource_resets=int(args.language_reload) + int(args.video_restart) + (3 if display_sources else 0),
                settings_fixture=args.retained_document.resolve() == ROOT / 'tools/ui/fixtures/managed-settings-smoke.q4ui',
                mode=args.mode, alias_fixture=args.presentation_alias_probe, event_fixture=args.event_program_probe,
                initial_brightness=args.brightness)
            if args.system_settings_probe:
                system = system_settings_probe.evidence(plain_log, width=args.width,
                    resets=int(args.language_reload) + int(args.video_restart))
                system['sources'] = system_sources
                evidence['system_settings_contract'] = system
                evidence['passed'] = evidence['passed'] and system['passed']
                # Only the exact, source-bound negative cases are expected;
                # preserve every diagnostic in the report and reject extras.
                if system['passed']:
                    retained_diagnostics = [line for line in retained_diagnostics
                        if line.strip() not in system_settings_probe.EXPECTED_DIAGNOSTICS]
            if display_sources:
                display = display_settings_probe.evidence(plain_log, game=game,
                    resets=int(args.language_reload) + int(args.video_restart))
                display['sources'] = display_sources
                evidence['display_settings_contract'] = display
                evidence['passed'] = evidence['passed'] and display['passed']
            metadata['retained_preview']['managed_trace'] = evidence.pop('managed_trace')
            metadata['retained_preview']['managed_validation'] = evidence
            valid = valid and evidence['passed'] and not retained_diagnostics
        else:
            valid = valid and 'Retained UI preview loaded:' in plain_log and not retained_diagnostics
        if args.timeline:
            played = plain_log.count(f'Retained UI timeline played: {args.timeline}')
            metadata['retained_preview']['timeline_play_count'] = played
            valid = valid and played == (2 if args.video_restart and not args.retained_resume_script else 1)
    if screenshot.is_file():
        image = screenshot.read_bytes()
        if len(image) >= 18:
            width, height = struct.unpack_from('<HH', image, 12)
            metadata['screenshot'] = {'path': str(screenshot.relative_to(output)), 'width': width,
                                      'height': height, 'sha256': digest(screenshot)}
            expected_size = display_settings_probe.FINAL_SIZE if display_sources else (args.width, args.height)
            valid = valid and (width, height) == expected_size
        else:
            valid = False
    metadata['status'] = 'captured_pending_visual_review' if valid else 'failed'
    metadata['log_sha256'] = digest(log_path) if log_path.exists() else None
    save_report()
    print(f'{metadata["status"]}: {report_path}', flush=True)
    return 0 if valid else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('sp', 'mp'), required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), required=True)
    parser.add_argument('--assets', type=Path, required=True, help='Installation root containing q4base.')
    parser.add_argument('--runtime', type=Path, default=ROOT / '.install')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    parser.add_argument('--shared-gui', action='store_true', help='exercise the shared GUI renderer domain')
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--retained-document', type=Path, help='Optional Q4UI or RML integration fixture, copied into the isolated savepath.')
    parser.add_argument('--retained-resume-script', type=Path, help='Checks after video restart; managed mode reads surviving state after each language/video reset without setup replay.')
    parser.add_argument('--retained-script', type=Path, help='Optional semantic control script; does not send device input.')
    parser.add_argument('--retained-trace', action='store_true', help='Record retained application and display transaction diagnostics.')
    parser.add_argument('--retained-data', type=Path, action='append', default=[], help='State JSON copied to retained-data/<name>; repeat for scripted state batches.')
    ownership = parser.add_mutually_exclusive_group()
    ownership.add_argument('--retained-open', action='store_true', help='Acquire preview application ownership with host input still disabled; inspect pause/resume and close.')
    ownership.add_argument('--retained-managed', action='store_true', help='Load a normal manager-owned .q4ui with testGUI and qualify typed application actions.')
    parser.add_argument('--retained-peer', action='store_true', help='Managed mode only: keep an independent preview of the same document alive across resets, then close the peer and report the managed owner.')
    parser.add_argument('--legacy-export-list', type=Path, help='JSON source/hash records to preprocess through the engine without executing GUI scripts.')
    parser.add_argument('--presentation-probe', action='store_true', help='Exercise production presentation reads/writes with an authored legacy fixture after map gameplay; no host input.')
    parser.add_argument('--presentation-alias-probe', action='store_true', help='Managed mode: qualify exact authored presentation-alias-smoke sources; defaults document/setup/resume paths and checks alias ownership and reload readbacks.')
    parser.add_argument('--event-program-probe', action='store_true', help='Managed mode: qualify exact event-program-smoke sources, lifecycle/ordered-program state and actual application dispatch; enables bounded retained tracing.')
    parser.add_argument('--system-settings-probe', action='store_true', help='Managed mode: qualify owned SYSTEM drafts, immediate application, conflicts and resource persistence; device changes are rejected before writes.')
    parser.add_argument('--display-settings-probe', action='store_true', help='Managed mode: qualify exact display fixture Apply/Keep/Apply/Revert, owning-view presentation witnesses, durable journal traces and four engine screenshots; final size is 960x540.')
    parser.add_argument('--gamma', type=float, default=1, help='Explicit renderer gamma, finite 0.1..3 (default 1).')
    parser.add_argument('--brightness', type=float, default=1, help='Explicit initial renderer brightness, finite 0..2 (default 1); settings scripts may subsequently change it.')
    parser.add_argument('--timeline', help='Canonical timeline to play before capture, and again after an optional video restart.')
    parser.add_argument('--reduced-motion', action='store_true')
    parser.add_argument('--density', type=float, default=0, help='Test density override; zero uses SDL display scale.')
    parser.add_argument('--ui-scale', type=float, default=1)
    parser.add_argument('--video-restart', action='store_true', help='Restart the windowed renderer with the preview loaded before capturing.')
    parser.add_argument('--language-reload', action='store_true', help='Reload the same language dictionary with the preview loaded before optional video restart.')
    parser.add_argument('--profile-frames', type=int, default=0, help='Measure 1..3600 rendered UI frames before capture; zero disables profiling.')
    args = parser.parse_args()
    if args.display_settings_probe:
        if not args.retained_managed or args.retained_peer or args.presentation_probe or args.presentation_alias_probe or args.event_program_probe or args.system_settings_probe:
            parser.error('--display-settings-probe requires one --retained-managed owner and cannot combine with another probe')
        args.retained_document = args.retained_document or Path(str(display_settings_probe.FIXTURE) + '.q4ui')
        args.retained_script = args.retained_script or Path(str(display_settings_probe.FIXTURE) + '.cfg')
        if args.language_reload or args.video_restart:
            args.retained_resume_script = args.retained_resume_script or Path(str(display_settings_probe.FIXTURE) + '-resume.cfg')
    if args.system_settings_probe:
        if not args.retained_managed or args.presentation_probe or args.presentation_alias_probe or args.event_program_probe:
            parser.error('--system-settings-probe requires --retained-managed and cannot combine with another presentation probe')
        args.retained_document = args.retained_document or Path(str(system_settings_probe.FIXTURE) + '.q4ui')
        args.retained_script = args.retained_script or Path(str(system_settings_probe.FIXTURE) + '.cfg')
        if args.language_reload or args.video_restart:
            args.retained_resume_script = args.retained_resume_script or Path(str(system_settings_probe.FIXTURE) + '-resume.cfg')
    if args.event_program_probe:
        if not args.retained_managed or args.presentation_probe or args.presentation_alias_probe:
            parser.error('--event-program-probe requires --retained-managed and cannot combine with other presentation probes')
        args.retained_document = args.retained_document or Path(str(EVENT_FIXTURE) + '.q4ui')
        args.retained_script = args.retained_script or Path(str(EVENT_FIXTURE) + '.cfg')
        if args.language_reload or args.video_restart:
            args.retained_resume_script = args.retained_resume_script or Path(str(EVENT_FIXTURE) + '-resume.cfg')
    if args.presentation_alias_probe:
        if not args.retained_managed or args.presentation_probe:
            parser.error('--presentation-alias-probe requires --retained-managed and cannot combine with --presentation-probe')
        args.retained_document = args.retained_document or Path(str(ALIAS_FIXTURE) + '.q4ui')
        args.retained_script = args.retained_script or Path(str(ALIAS_FIXTURE) + '.cfg')
        if args.language_reload or args.video_restart:
            args.retained_resume_script = args.retained_resume_script or Path(str(ALIAS_FIXTURE) + '-resume.cfg')
    if not math.isfinite(args.gamma) or not .1 <= args.gamma <= 3:
        parser.error('--gamma must be finite and between 0.1 and 3')
    if not math.isfinite(args.brightness) or not 0 <= args.brightness <= 2:
        parser.error('--brightness must be finite and between 0 and 2')
    if args.width < 1 or args.height < 1 or args.timeout < 1:
        parser.error('dimensions and timeout must be positive')
    if not math.isfinite(args.density) or not 0 <= args.density <= 8:
        parser.error('density must be zero (automatic) or a positive value up to 8')
    if not math.isfinite(args.ui_scale) or not .75 <= args.ui_scale <= 2:
        parser.error('UI scale must be between 0.75 and 2')
    if args.video_restart and not args.retained_document:
        parser.error('--video-restart requires --retained-document')
    if args.language_reload and not args.retained_document:
        parser.error('--language-reload requires --retained-document')
    if args.retained_script and (not args.retained_document or args.retained_document.suffix.lower() != '.q4ui'):
        parser.error('--retained-script requires a .q4ui document')
    if args.retained_resume_script and (not (args.video_restart or (args.retained_managed and args.language_reload))
                                       or not args.retained_document or args.retained_document.suffix.lower() != '.q4ui'):
        parser.error('--retained-resume-script requires a .q4ui document and video restart (or managed language reload)')
    if args.retained_data and (len(args.retained_data) > 16 or not args.retained_document or args.retained_document.suffix.lower() != '.q4ui'):
        parser.error('--retained-data requires a .q4ui document and at most 16 files')
    if args.retained_open and (not args.retained_document or args.retained_document.suffix.lower() != '.q4ui'):
        parser.error('--retained-open requires a .q4ui document')
    if args.retained_managed and (not args.retained_document or args.retained_document.suffix.lower() != '.q4ui'):
        parser.error('--retained-managed requires a .q4ui document')
    if args.retained_peer and not args.retained_managed:
        parser.error('--retained-peer requires --retained-managed')
    if args.retained_managed and (args.timeline or args.profile_frames or args.retained_data):
        parser.error('managed mode uses its semantic script; preview timeline/profile/state-data commands are unsupported')
    if not 0 <= args.profile_frames <= 3600 or (args.profile_frames and not args.retained_document):
        parser.error('--profile-frames requires a retained document and a count from 1 to 3600')
    if args.retained_document and args.retained_document.suffix.lower() not in ('.rml', '.q4ui'):
        parser.error('retained document must be .rml or .q4ui')
    if args.timeline and (not args.retained_document or args.retained_document.suffix.lower() != '.q4ui'
                          or not re.fullmatch(r'[A-Za-z0-9_.-]{1,128}', args.timeline)):
        parser.error('--timeline requires a .q4ui document and a valid stable timeline ID')
    try:
        return capture(args)
    except (OSError, ValueError, StopIteration) as error:
        print(f'Baseline capture failed: {error}', flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
