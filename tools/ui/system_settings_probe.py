"""Source-bound SYSTEM transaction gameplay observations, not GUI acceptance."""
import hashlib
import math
from pathlib import Path
import re

FIXTURE = Path(__file__).resolve().parent / 'fixtures/system-settings-smoke'
FIELDS = ['live', 'draft', 'baseline', 'dirty', 'open', 'canApply', 'phase',
          'liveShadows', 'draftShadows', 'liveWidth', 'draftWidth', 'spare']
EXPECTED_DIAGNOSTICS = [
    'WARNING: retained GUI retained-smoke.q4ui: r_brightness: outside the registered CVar range',
    'WARNING: retained GUI retained-smoke.q4ui: System settings batch requires device work without a qualified result route',
    'WARNING: retained GUI retained-smoke.q4ui: Settings changed outside this session; reopen before applying',
]


def sources(args):
    if args.brightness != 1 or args.width == 960:
        raise ValueError('SYSTEM probe requires initial brightness 1 and a window width other than 960')
    result = {}
    for attribute, suffix in [('retained_document', '.q4ui'), ('retained_script', '.cfg'),
                              ('retained_resume_script', '-resume.cfg')]:
        expected = Path(str(FIXTURE) + suffix)
        supplied = getattr(args, attribute)
        if supplied is None and attribute == 'retained_resume_script' and not (args.language_reload or args.video_restart):
            continue
        if supplied is None or supplied.read_bytes() != expected.read_bytes():
            raise ValueError('SYSTEM probe requires the exact committed fixture and semantic scripts')
        result[expected.name] = hashlib.sha256(expected.read_bytes()).hexdigest()
    return result


def expected_rows(width, resets):
    initial = [1, 1, 1, 0, 1, 0, 1, 1, 1, width, width, 7]
    edited = [1, 1.25, 1, 1, 1, 1, 1, 1, 0, width, width, 7]
    applied = [1.25, 1.25, 1.25, 0, 1, 0, 1, 0, 0, width, width, 9]
    defaults = [1.25, 1, 1.25, 1, 1, 0, 1, 0, 1, width, 1280, 9]
    closed = [1.25, 0, 0, 0, 0, 0, 0, 0, 0, width, 0, 9]
    device = [1.25, 1.25, 1.25, 1, 1, 0, 1, 0, 0, width, 960, 9]
    conflict = [1.1, 1.5, 1.25, 1, 1, 1, 1, 0, 0, width, width, 9]
    final = [1.25, 1.5, 1.25, 1, 1, 1, 1, 0, 0, width, width, 9]
    return [initial, edited, applied, defaults, defaults, applied, closed, applied,
            device, conflict, applied, final] + [final] * resets


def evidence(log, *, width, resets):
    errors = []
    reads = re.findall(r'^GUI_VALUE ([^=]+)=(.*)$', log, re.MULTILINE)
    expected = [(field, value) for row in expected_rows(width, resets) for field, value in zip(FIELDS, row)]
    if len(reads) != len(expected):
        errors.append('SYSTEM readback count does not match the complete scenario')
    for index, ((field, text), (key, number)) in enumerate(zip(reads, expected)):
        try:
            value = float(text.strip())
        except ValueError:
            value = math.nan
        if field != key or not math.isfinite(value) or not math.isclose(value, number, abs_tol=1e-6, rel_tol=0):
            errors.append(f'SYSTEM readback {index}: expected {key}={number}, got {field}={text.strip()}')
    trace = re.findall(r'^UI_SETTINGS operation=settings.system\.(\w+) result=(\d+) phase=(\d+) owner=(\d+) dirty=([01])$', log, re.MULTILINE)
    wanted = [('begin', 0), ('edit', 0), ('apply', 0), ('edit', 0), ('defaults', 0),
              ('revert', 0), ('cancel', 0), ('begin', 0), ('edit', 3), ('edit', 0),
              ('apply', 3), ('revert', 0), ('edit', 0), ('apply', 4), ('cancel', 0),
              ('begin', 0), ('edit', 0), ('apply', 0), ('edit', 0)]
    if [(op, int(code)) for op, code, _, _, _ in trace] != wanted:
        errors.append('SYSTEM operation order/results differ or resource restoration replayed an operation')
    if not trace or len({owner for _, _, _, owner, _ in trace}) != 1 or trace[0][3] == '0':
        errors.append('SYSTEM owner changed or was unavailable')
    if any(int(phase) != (0 if op == 'cancel' else 1) for op, _, phase, _, _ in trace):
        errors.append('SYSTEM transaction phase is unexpected')
    diagnostics = [line.strip() for line in log.splitlines() if 'WARNING: retained GUI ' in line]
    if diagnostics != EXPECTED_DIAGNOSTICS:
        errors.append('SYSTEM negative-case diagnostics differ or an unexpected warning occurred')
    return {'id': 'system-settings-smoke-v1', 'passed': not errors, 'errors': errors,
            'readbacks': reads, 'operation_trace': trace, 'expected_diagnostics': EXPECTED_DIAGNOSTICS,
            'replacement_acceptance': False, 'device_application_qualified': False}
