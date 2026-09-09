"""Source-bound display transaction observations and engine screenshot oracle."""
import hashlib
import math
from pathlib import Path
import re
import struct

FIXTURE = Path(__file__).resolve().parent / 'fixtures/display-settings-smoke'
FIELDS = ('phase', 'open', 'dirty', 'canApply', 'request', 'confirmationVisible',
          'canConfirm', 'canRevert', 'canRetry', 'remaining', 'draftWidth',
          'draftHeight', 'baselineWidth', 'baselineHeight', 'liveWidth', 'liveHeight')
SHOTS = (('confirm-a', 960, 540), ('keep', 960, 540),
         ('confirm-b', 1024, 576), ('revert', 960, 540))
FINAL_SIZE = (960, 540)


def sources(args):
    if (args.width, args.height) != (1280, 720) or args.brightness != 1:
        raise ValueError('display settings probe requires initial 1280x720 and brightness 1')
    if not args.retained_managed or getattr(args, 'retained_peer', False):
        raise ValueError('display settings probe requires one managed confirmation owner')
    result = {}
    for attribute, suffix in (('retained_document', '.q4ui'), ('retained_script', '.cfg'),
                              ('retained_resume_script', '-resume.cfg')):
        expected = Path(str(FIXTURE) + suffix)
        supplied = getattr(args, attribute)
        optional = attribute == 'retained_resume_script' and not (args.language_reload or args.video_restart)
        if not (optional and supplied is None) and (supplied is None or supplied.read_bytes() != expected.read_bytes()):
            raise ValueError('display settings probe requires exact canonical document/setup/resume bytes')
        result[expected.name] = hashlib.sha256(expected.read_bytes()).hexdigest()
    return result


def inject_screenshots(script):
    """Called only after canonical source validation and semantic filtering."""
    marker = 'openq4_guiGet "liveHeight"'
    if script.splitlines().count(marker) != 6:
        raise ValueError('display settings setup must contain six complete readback groups')
    result = []
    group = 0
    for line in script.splitlines():
        result.append(line)
        if line == marker:
            if group >= 2:
                result.append(f'screenshot "screenshots/ui-display-{SHOTS[group-2][0]}.tga"')
                result.append('openq4_retainedGui inspect "settings-panel"')
            group += 1
    return '\n'.join(result) + '\n'


def screenshot_evidence(game):
    errors, images = [], []
    for name, width, height in SHOTS:
        relative = f'screenshots/ui-display-{name}.tga'
        path = game / relative
        row = {'path': relative, 'expected_width': width, 'expected_height': height}
        images.append(row)
        try:
            if path.is_symlink():
                raise ValueError('screenshot is a link')
            data = path.read_bytes()
            row['sha256'] = hashlib.sha256(data).hexdigest()
            if len(data) < 18:
                raise ValueError('truncated TGA header')
            actual_width, actual_height = struct.unpack_from('<HH', data, 12)
            row.update(width=actual_width, height=actual_height, bits=data[16])
            if (actual_width, actual_height) != (width, height) or data[1] != 0 or data[2] != 2 or data[16] not in (24, 32):
                raise ValueError('unexpected TGA type or actual framebuffer dimensions')
            channels = data[16] // 8
            start = 18 + data[0]
            if len(data) != start + width * height * channels:
                raise ValueError('incomplete or extra TGA pixel payload')
            step = max(1, width * height // 8192)
            colours = {data[start+i*channels:start+i*channels+3] for i in range(0, width*height, step)}
            values = [value for colour in colours for value in colour]
            row['sampled_colours'] = len(colours)
            row['sampled_channel_range'] = [min(values), max(values)]
            if len(colours) < 16 or max(values) - min(values) < 32:
                raise ValueError('framebuffer is blank, flat or lacks plausible rendered colour variation')
            row['passed'] = True
        except (OSError, ValueError) as failure:
            row['passed'] = False
            row['error'] = str(failure)
            errors.append(f'{relative}: {failure}')
    return images, errors


def persistence_evidence(game):
    errors = []
    journal = game / 'ui-settings-recovery.dat'
    lock = game / '.settings-recovery.lock'
    config = game / 'openQ4Config.cfg'
    result = {'journal_absent': not (journal.exists() or journal.is_symlink()),
              'permanent_lock_present': lock.is_file() and not lock.is_symlink()}
    if not result['journal_absent'] or not result['permanent_lock_present']:
        errors.append('completed display recovery retained a journal or lost its permanent lock identity')
    try:
        if not config.is_file() or config.is_symlink() or config.stat().st_size > 16*1024*1024:
            raise ValueError('final configuration is absent, linked, nonregular or oversized')
        data = config.read_bytes()
        result['configuration_sha256'] = hashlib.sha256(data).hexdigest()
        text = data.decode('utf-8')
        settings = {}
        for key, expected in (('r_windowWidth', 960), ('r_windowHeight', 540)):
            values = re.findall(r'^seta ' + key + r' "([^"\r\n]*)"\r?$', text, re.MULTILINE)
            if len(values) != 1 or not re.fullmatch(r'[0-9]+', values[0]) or int(values[0]) != expected:
                raise ValueError('final configuration does not retain the confirmed dimensions')
            settings[key] = int(values[0])
        result['archived_dimensions'] = settings
    except (OSError, ValueError) as failure:
        errors.append(str(failure))
    result['passed'] = not errors
    return result, errors


def evidence(log, *, game, resets=0):
    errors = []
    lines = log.splitlines()
    reads = [(index, *match.groups()) for index, line in enumerate(lines)
             if (match := re.fullmatch(r'GUI_VALUE ([^=]+)=(.*)', line))]
    # None denotes a separately validated dynamic request or countdown.
    rows = [
        [1, 1, 0, 0, '', 0, 0, 0, 0, 0, 1280, 720, 1280, 720, 1280, 720],
        [1, 1, 1, 1, '', 0, 0, 0, 0, 0, 960, 540, 1280, 720, 1280, 720],
        [2, 1, 1, 0, None, 1, 1, 1, 0, None, 960, 540, 1280, 720, 960, 540],
        [1, 1, 0, 0, '', 0, 0, 0, 0, 0, 960, 540, 960, 540, 960, 540],
        [2, 1, 1, 0, None, 1, 1, 1, 0, None, 1024, 576, 960, 540, 1024, 576],
        [1, 1, 0, 0, '', 0, 0, 0, 0, 0, 960, 540, 960, 540, 960, 540],
    ]
    rows += [rows[-1]] * resets
    expected = [(field, value) for row in rows for field, value in zip(FIELDS, row)]
    if len(reads) != len(expected):
        errors.append('display readback count differs from six setup groups and requested resumes')
    requests = []
    for index, ((_, field, text), (key, value)) in enumerate(zip(reads, expected)):
        okay = field == key
        if key == 'request':
            okay = okay and (text == '' if value == '' else bool(re.fullmatch(r'[1-9][0-9]{0,19}', text)) and int(text) < 2**64)
            if value is None and okay:
                requests.append(int(text))
        else:
            try:
                number = float(text)
            except ValueError:
                number = math.nan
            okay = okay and math.isfinite(number) and (0 < number <= 15 if value is None else math.isclose(number, value, abs_tol=1e-6, rel_tol=0))
        if not okay:
            errors.append(f'display readback {index} has an unexpected field or value: {field}={text}')
    if len(requests) != 2 or not requests[0] < requests[1]:
        errors.append('the two confirmation requests lack distinct increasing identities')

    patterns = {
        'action': r'UI_SETTINGS operation=settings.system\.(\w+) result=(\d+) phase=(\d+) owner=(\d+) dirty=([01])',
        'journal': r'UI_SETTINGS_JOURNAL state=(pending|confirmed) durable=([01])',
        'device': r'UI_SETTINGS_DEVICE restore=([01]) epoch=(\d+) generation=(\d+) submitted=(\d+) presented=(\d+) failures=(\d+) width=(\d+) height=(\d+)',
        'stage': r'UI_SETTINGS_DISPLAY stage=(\d+) owner=(\d+) request=(\d+) result=(\d+) blocked=([01]) detail=(.*)',
        'view': r'UI_SETTINGS_VIEW owner=(\d+) request=(\d+) epoch=(\d+) generation=(\d+) submitted=(\d+) presented=(\d+) failures=(\d+)',
        'present': r'UI_SETTINGS_PRESENT owner=(\d+) request=(\d+) epoch=(\d+) generation=(\d+) submitted=(\d+) presented=(\d+) failures=(\d+)',
    }
    traces = {name: [] for name in patterns}
    flow = []
    for index, line in enumerate(lines):
        for name, pattern in patterns.items():
            match = re.fullmatch(pattern, line)
            if match:
                traces[name].append((index, *match.groups()))
                if name not in ('view', 'present'):
                    flow.append((name, match.group(1)))
                break
        else:
            if line.startswith('UI_SETTINGS'):
                errors.append('unknown or malformed display transaction trace')
        if line.startswith('GUI_VALUE '):
            flow.append(('read', line.split('=', 1)[0][10:]))
        if line.startswith('RETAINED_GUI_RESOURCE '):
            if line != 'RETAINED_GUI_RESOURCE path=retained-smoke.q4ui event=restored':
                errors.append('retained resource recovery failed or belongs to another document')
            flow.append(('resource', 'restored'))
        if line.startswith('Wrote screenshots/ui-display-'):
            flow.append(('screenshot', line.removeprefix('Wrote ')))

    wanted_actions = [('begin', 0, 1, 0), ('edit', 0, 1, 1), ('apply', 0, 4, 1),
                      ('confirm', 0, 2, 1), ('edit', 0, 1, 1), ('apply', 0, 4, 1), ('revert', 0, 2, 1)]
    actions = traces['action']
    if [(op, int(code), int(phase), int(dirty)) for _, op, code, phase, _, dirty in actions] != wanted_actions:
        errors.append('typed action order, result, phase or dirty state differs from the display scenario')
    owners = {int(owner) for _, _, _, _, owner, _ in actions}
    if len(owners) != 1 or 0 in owners:
        errors.append('display transaction does not retain one nonzero owner')
    owner = next(iter(owners), 0)
    if [(state, int(durable)) for _, state, durable in traces['journal']] != [('pending', 1), ('confirmed', 1), ('pending', 1)]:
        errors.append('journal transitions lack acknowledged durable Pending/Confirmed/Pending order')

    devices = [tuple(map(int, row[1:])) for row in traces['device']]
    if len(devices) != 3 or [(d[0], d[6], d[7]) for d in devices] != [(0, 960, 540), (0, 1024, 576), (1, 960, 540)]:
        errors.append('three actual typed device results do not match Apply/Apply/Restore dimensions')
    elif (not devices[0][1] or any(d[1] != devices[0][1] or not d[2] or d[5] != 0 or d[3] < d[4] for d in devices)
          or not devices[0][2] < devices[1][2] < devices[2][2]
          or any(a[3] >= b[3] or a[4] >= b[4] for a, b in zip(devices, devices[1:]))):
        errors.append('device epoch/generation, presentation counters or failure sequence is invalid')
    stages = traces['stage']
    if [int(row[1]) for row in stages] != [2, 3, 0, 2, 3, 6, 0]:
        errors.append('display stages did not await presentation, confirm and finish each direction')
    if any(int(o) != owner or int(code) != 0 or int(blocked) != (int(stage) != 0) or detail
           for _, stage, o, _, code, blocked, detail in stages):
        errors.append('display stage result, ownership or persistence block is inconsistent')
    if len(stages) == 7 and len(requests) == 2:
        tokens = [int(row[3]) for row in stages]
        if tokens[:5] != [requests[0], requests[0], 0, requests[1], requests[1]] or tokens[5] <= requests[1] or tokens[6] != 0:
            errors.append('stage identities differ from displayed requests or reuse a restore identity')

    # Actual view observations and the later confirmed presentation must share
    # the same owner/device/request and advance BOTH counters beyond that view.
    views = [(row[0], *map(int, row[1:])) for row in traces['view']]
    presents = [(row[0], *map(int, row[1:])) for row in traces['present']]
    if len(presents) != 2 or not views or any(v[1] != owner or v[2] not in requests for v in views):
        errors.append('owning confirmation view/presentation witnesses are missing or unexpected')
    if len(requests) == 2 and len(devices) == 3 and len(stages) == 7:
        for view in views:
            if view[2] not in requests:
                continue
            index = requests.index(view[2])
            device = devices[index]
            if (view[3:5] != device[1:3] or view[7] != device[5] or view[5] < device[3] or view[6] < device[4]
                    or not stages[index*3][0] < view[0] < stages[index*3+1][0]):
                errors.append('accepted owner-view observation lies outside its ready device/request stage')
    for index, present in enumerate(presents):
        prior = [v for v in views if v[2] == present[2] and v[0] < present[0]]
        if not prior or index >= len(requests) or index >= len(devices):
            errors.append('presentation has no prior owning-view observation')
            continue
        view, device = prior[-1], devices[index]
        if (present[1] != owner or present[2] != requests[index] or present[3:5] != device[1:3]
                or view[1:5] != present[1:5] or view[7] != present[7] or present[7] != device[5]
                or present[5] <= view[5] or present[6] <= view[6]
                or view[5] < device[3] or view[6] < device[4]
                or (len(stages) == 7 and present[0] <= stages[index*3+1][0])
                or (len(reads) > (2+2*index)*16 and present[0] >= reads[(2+2*index)*16][0])):
            errors.append('confirmation did not follow a fresh presentation of its owning view')

    group = [('read', name) for name in FIELDS]
    wanted_flow = [('action', 'begin')] + group + [('action', 'edit')] + group
    wanted_flow += [('action', 'apply'), ('journal', 'pending'), ('device', '0'), ('stage', '2'), ('resource', 'restored'), ('stage', '3')] + group
    wanted_flow += [('screenshot', 'screenshots/ui-display-confirm-a.tga'), ('action', 'confirm'), ('journal', 'confirmed'), ('stage', '0')] + group
    wanted_flow += [('screenshot', 'screenshots/ui-display-keep.tga'), ('action', 'edit'), ('action', 'apply'), ('journal', 'pending'), ('device', '0'), ('stage', '2'), ('resource', 'restored'), ('stage', '3')] + group
    wanted_flow += [('screenshot', 'screenshots/ui-display-confirm-b.tga'), ('action', 'revert'), ('device', '1'), ('stage', '6'), ('resource', 'restored'), ('stage', '0')] + group
    wanted_flow += [('screenshot', 'screenshots/ui-display-revert.tga')] + ([('resource', 'restored')] + group) * resets
    if flow != wanted_flow:
        errors.append('readbacks, journal/device boundaries, recovery events or engine screenshots occurred out of order')
    diagnostics = [line for line in lines if 'ERROR:' in line or 'FATAL:' in line or
                   ('WARNING:' in line and any(word in line.lower() for word in ('retained', 'ui settings', 'ui_settings')))]
    if diagnostics:
        errors.append('display flow emitted UI/native retained warnings or errors')
    images, image_errors = screenshot_evidence(game)
    errors.extend(image_errors)
    persistence, persistence_errors = persistence_evidence(game)
    errors.extend(persistence_errors)
    return {'id': 'display-settings-smoke-v1', 'passed': not errors, 'errors': errors,
            'readbacks': [(key, value) for _, key, value in reads], 'operation_trace': actions,
            'journal_trace': traces['journal'], 'device_trace': devices, 'stage_trace': stages,
            'owner_view_trace': views, 'owner_present_trace': presents, 'screenshots': images,
            'final_persistence': persistence,
            'diagnostics': diagnostics, 'expected_final_size': list(FINAL_SIZE),
            'device_application_qualified': not errors, 'replacement_acceptance': False,
            'visual_review_required': True}
