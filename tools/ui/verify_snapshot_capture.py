#!/usr/bin/env python3
"""Check retained checkpoint/renderer-restart state from the engine's trace.

Requires the snapshot scripts and interaction-smoke document. Screenshot
review is separate; this does not establish completed menus or savegame parity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'tools/ui/fixtures'


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def commands(path: Path) -> list[str]:
    return [line.strip() for line in path.read_text(encoding='utf-8').splitlines()
            if line.strip() and not line.strip().startswith('//')]


def expected_commands(capture: dict) -> list[str]:
    """Reconstruct this qualification's complete CFG, including its restart.

    Setup appears once. Only the exact observation/resume fixture may follow
    vid_restart; replaying setup would hide a failure to preserve the instance.
    """
    preview = capture['retained_preview']
    frames = preview['profile_frames']
    settle = preview['settle_frames']
    if type(frames) is not int or not 0 <= frames <= 3600 or settle != max(30, frames + 2):
        raise ValueError('invalid profile or settling frame contract')
    if type(preview['application_open']) is not bool:
        raise ValueError('invalid application ownership contract')
    if type(preview.get('language_reload', False)) is not bool:
        raise ValueError('invalid language reload contract')
    opened = preview['application_open']
    begin = ['wait 2', 'ui_retainedOwnership'] if opened else []
    end = ['ui_retainedOwnership'] if opened else []
    profile = [f'ui_retainedProfile {frames}'] if frames else []
    load = 'ui_retainedOpen' if opened else 'ui_retainedPreview'
    result = []
    if capture.get('presentation_probe'):
        probe = capture['presentation_probe']
        sources = {f'presentation-smoke.{suffix}': digest(FIXTURES / f'presentation-smoke.{suffix}')
                   for suffix in ('gui', 'cfg')}
        if not probe['passed'] or probe['sources'] != sources:
            raise ValueError('presentation probe provenance changed')
        result += commands(FIXTURES / 'presentation-smoke.cfg')
    result += [f'{load} "retained-smoke.q4ui"'] + begin + profile + ['wait 2']
    result += commands(FIXTURES / 'snapshot-smoke.cfg') + [f'wait {settle}'] + end
    if preview.get('language_reload', False):
        result += ['reloadLanguage', 'wait 2']
    result += ['vid_restart windowed', 'wait 2'] + begin + profile
    result += commands(FIXTURES / 'snapshot-resume.cfg') + [f'wait {settle}'] + end
    result += ['gfxInfo', 'screenshot "screenshots/ui-baseline.tga"', 'echo UI_BASELINE_CAPTURE_COMPLETE']
    if opened:
        result += ['ui_retainedClose', 'wait 3', 'ui_retainedOwnership']
    return result + ['quit']


def verify(folder: Path) -> dict:
    try:
        return verify_capture(folder)
    except (OSError, ValueError, TypeError, KeyError, IndexError, struct.error) as error:
        return {'passed': False, 'reason': f'invalid or missing capture evidence: {error}'}


def verify_capture(folder: Path) -> dict:
    capture = json.loads((folder / 'capture.json').read_text(encoding='utf-8'))
    preview = capture['retained_preview']
    if (capture['status'] != 'captured_pending_visual_review' or capture['returncode'] != 0 or
            preview['diagnostics'] or preview['video_restart'] is not True or
            not preview.get('resume_script') or preview.get('timeline') or preview.get('state_data') or
            capture.get('legacy_import')):
        return {'passed': False, 'reason': 'capture failed or uses a different snapshot qualification flow'}
    game = folder / 'save/baseoq4'
    cfg_path = game / 'ui-baseline.cfg'
    log_path = game / 'logs/openq4.log'
    image_path = game / 'screenshots/ui-baseline.tga'
    image = capture['screenshot']
    # Read the fixed engine target path, never a path selected by metadata.
    # Both native Windows and portable slash spellings describe that artifact.
    image_name_ok = isinstance(image['path'], str) and image['path'].replace('\\', '/') == image_path.relative_to(folder).as_posix()
    provenance = (preview['sha256'] == digest(FIXTURES / 'interaction-smoke.q4ui') == digest(game / 'retained-smoke.q4ui') and
                  preview['interaction_script']['sha256'] == digest(FIXTURES / 'snapshot-smoke.cfg') and
                  preview['resume_script']['sha256'] == digest(FIXTURES / 'snapshot-resume.cfg') and
                  capture['cfg_sha256'] == digest(cfg_path) and capture['log_sha256'] == digest(log_path) and
                  image_name_ok and image['sha256'] == digest(image_path))
    if not provenance:
        return {'passed': False, 'reason': 'fixture, scripts, staged document, CFG, log or screenshot provenance changed'}
    if commands(cfg_path) != expected_commands(capture):
        return {'passed': False, 'reason': 'generated CFG does not contain exactly one setup and the expected restart observations'}
    data = image_path.read_bytes()
    if len(data) < 18 or struct.unpack_from('<HH', data, 12) != (image['width'], image['height']):
        return {'passed': False, 'reason': 'screenshot dimensions differ from capture metadata'}
    log = re.sub(r'\^[0-9]', '', log_path.read_text(encoding='utf-8', errors='replace'))
    lines = log.splitlines()
    renderer = re.findall(r'Renderer API: requested=\S+ active=(\S+) disposition=(\S+)', log)
    diagnostics = [line for line in lines if
                   (('WARNING:' in line or 'ERROR:' in line) and ('retained UI:' in line or '_retained' in line)) or
                   line.startswith('usage: ui_retained')]
    if (not renderer or renderer[-1][0] != capture['renderer'] or capture['active_renderer'] != capture['renderer'] or
            not any(line.strip() == 'UI_BASELINE_CAPTURE_COMPLETE' for line in lines) or diagnostics):
        return {'passed': False, 'reason': 'engine log does not establish a completed capture on the requested renderer'}
    trace = [line for line in lines if line.startswith(('Retained UI control:', 'Retained UI action:', 'Retained UI actions:'))]
    snapshots = [line for line in lines if line.startswith(('Retained UI checkpoint:', 'Retained UI instance restored after renderer/language change:'))]
    if trace != preview['interaction_trace'] or snapshots != preview['snapshot_trace']:
        return {'passed': False, 'reason': 'capture traces differ from the engine log'}
    counts = [int(line.split(': ')[1]) for line in trace if line.startswith('Retained UI actions:')]
    actions = [line for line in trace if line.startswith('Retained UI action:')]
    states = []
    for line in trace:
        match = re.fullmatch(r'Retained UI control: (\S+) state=(\S+) focus=(\S*) bounds=[0-9.,-]+', line)
        if match:
            states.append(list(match.groups()))
    expected = [['modal-game-options', 'focus', 'modal-game-options'],
                ['modal-controls', 'disabled', 'modal-game-options'],
                ['reference-game-options', 'default', 'modal-game-options']]
    saved = [int(match.group(1)) for line in snapshots
             if (match := re.fullmatch(r'Retained UI checkpoint: save bytes=(\d+)', line))]
    restored = [int(match.group(1)) for line in snapshots
                if (match := re.fullmatch(r'Retained UI checkpoint: restore bytes=(\d+)', line))]
    restarts = [line for line in snapshots if line.startswith('Retained UI instance restored after renderer/language change:')]
    result = {'action_batches': counts, 'actions': actions, 'states': states,
              'checkpoint_bytes': saved, 'restored_bytes': restored, 'resource_restores': len(restarts)}
    expected_restart = 'Retained UI instance restored after renderer/language change: retained-smoke.q4ui'
    expected_restores = [expected_restart] * (1 + int(preview.get('language_reload', False)))
    ordered = [line for line in lines if line in trace or line in snapshots]
    expected_checkpoints = ([f'Retained UI checkpoint: save bytes={saved[0]}',
                             f'Retained UI checkpoint: restore bytes={saved[0]}'] if len(saved) == 1 else [])
    expected_order = expected_checkpoints + trace[:4] + expected_restores + trace[4:]
    result['passed'] = bool(counts == [0, 0] and not actions and len(trace) == 9
        and states == expected * 2 + [['reference-system', 'focus', 'reference-system']]
        and len(saved) == 1 and 0 < saved[0] <= 128 * 1024 * 1024
        and restored == saved and restarts == expected_restores and ordered == expected_order)
    result['provenance'] = True
    if preview['application_open']:
        ownership_trace = [line for line in lines if line.startswith('Retained UI ownership:')]
        ownership = []
        for line in ownership_trace:
            match = re.fullmatch(r'Retained UI ownership: open=(\d) suspended=(\d) session_gui=(\d) game_time=(-?\d+) requests=(\d+)', line)
            if match:
                ownership.append([int(value) for value in match.groups()])
        ownership_ok = ownership_trace == preview['ownership_trace'] and len(ownership) == 5
        if ownership_ok:
            ownership_ok = all(row[0] == 1 and row[2] == 1 and row[3] >= 0 and row[4] == 0 for row in ownership[:-1])
            ownership_ok = ownership_ok and ownership[-1][0] == 0 and ownership[-1][2] == 0 and ownership[-1][4] == 0
            for begin, end in zip(ownership[:-1:2], ownership[1:-1:2]):
                ownership_ok = ownership_ok and (end[3] == begin[3] if capture['mode'] == 'sp' else end[3] > begin[3])
            ownership_ok = ownership_ok and ownership[-1][3] > ownership[-2][3]
        result['ownership'] = ownership
        result['ownership_passed'] = ownership_ok
        result['passed'] = result['passed'] and ownership_ok
    result['replacement_acceptance'] = False
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    result = verify(parser.parse_args().capture)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
