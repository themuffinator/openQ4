#!/usr/bin/env python3
"""Exercise typed display restart/restore after gameplay without host input.

This qualifies a private device seam, not settings confirmation/journal recovery.
Only engine screenshot commands capture images. Every run requires a new folder.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tools/ui/fixtures/display-device-smoke.q4ui'
FIELDS = ('live', 'draft', 'baseline', 'dirty', 'open', 'phase', 'liveWidth', 'draftWidth')
READS = ''.join(f'openq4_guiGet "{field}"\n' for field in FIELDS)
SCRIPT = ('rendererModuleSelfTest\ntestGUI "display-smoke.q4ui"\nwait 3\n'
          'openq4_retainedGui event "edit"\nwait 3\n' + READS +
          'rendererDisplayProbe save\nrendererDisplayProbe apply 960 540 0 0\nwait 6\n'
          'rendererDisplayProbe report\nscreenshot "screenshots/display-applied.tga"\n' + READS +
          'rendererDisplayProbe restore\nwait 6\nrendererDisplayProbe report\n'
          'screenshot "screenshots/display-restored.tga"\n' + READS +
          'rendererDisplayProbe "missing-display"\ngfxInfo\nrendererDisplayProbe restore\nwait 6\n'
          'rendererDisplayProbe report\nscreenshot "screenshots/display-recovered.tga"\n' + READS +
          'testGUI\nwait 6\nscreenshot "screenshots/display-gameplay.tga"\n'
          'gfxInfo\necho DISPLAY_DEVICE_CAPTURE_COMPLETE\nquit\n')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def script_for_renderer(renderer):
    return SCRIPT.replace('apply 960 540 0 0', 'apply 960 540 0 4') if renderer == 'gl' else SCRIPT


def qualify(log, expected_samples=0):
    errors, rows, trace = [], [], []
    fields = set('operation result observed epoch generation ready window available outcome submitted presented failures native restart logical pixel display position hidden fullscreen maximized samples interval valid'.split())
    signed = {'native','samples','interval'}
    booleans = {'result','observed','ready','window','available','hidden','fullscreen','maximized'}
    for line in log.splitlines():
        if line.startswith('DISPLAY_PROBE '):
            tokens = line[len('DISPLAY_PROBE '):].split(' ')
            parts = [token.split('=') for token in tokens]
            if any(len(part) != 2 for part in parts):
                errors.append('Malformed display observation'); continue
            row = dict(parts)
            if len(parts) != len(fields) or set(row) != fields:
                errors.append('Missing, duplicate or extra observation fields'); continue
            valid = True
            for key,value in row.items():
                if key == 'operation': continue
                if len(value) > 32:
                    valid = False; continue
                if key in ('logical','pixel','position'):
                    pattern = r'-?\d+,-?\d+' if key == 'position' else r'\d+x\d+'
                    okay = re.fullmatch(pattern,value) is not None
                    numbers = re.split('[x,]',value) if okay else []
                    okay = okay and all(-(2**31) <= int(number) < 2**31 for number in numbers)
                else:
                    okay = re.fullmatch(r'-?\d+' if key in signed else r'\d+',value) is not None
                    if okay:
                        number = int(value)
                        okay = (-(2**31) <= number < 2**31) if key in signed else 0 <= number < 2**64
                        if key in booleans: okay = okay and number in (0,1)
                        if key == 'outcome': okay = okay and 0 <= number <= 11
                        if key == 'valid': okay = okay and 0 <= number <= 7
                        if key == 'display': okay = okay and 0 <= number < 2**32
                        if key == 'restart': okay = okay and 0 <= number < 2**31
                        if key == 'interval': okay = okay and -1 <= number <= 1
                        if key == 'samples': okay = okay and number in (0,2,4,8,16)
                valid = valid and okay
            if not valid: errors.append('Invalid display field type/range'); continue
            rows.append(row); trace.append(('display',row['operation']))
        elif line.startswith('GUI_VALUE '):
            trace.append(('read',line[len('GUI_VALUE '):].split('=',1)[0]))
        elif line.startswith('UI_SETTINGS '):
            action = re.search(r'operation=settings\.system\.(\w+)',line)
            trace.append(('action',action.group(1) if action else 'invalid'))
        elif 'DISPLAY_DEVICE_CAPTURE_COMPLETE' in line:
            trace.append(('complete',line.strip()))
        elif line.startswith('gfxInfo:'):
            trace.append(('diagnostic',line))
    operations = ('save','apply','report','restore','report','missing-display','restore','report')
    expected_trace = [('action','begin'),('action','edit')] + [('read',field) for field in FIELDS]
    for group in (operations[:3],operations[3:5],operations[5:]):
        for op in group:
            expected_trace.append(('display',op))
            if op == 'missing-display': expected_trace.append(('diagnostic','gfxInfo: graphics device is unavailable'))
        expected_trace += [('read',field) for field in FIELDS]
    expected_trace += [('complete','DISPLAY_DEVICE_CAPTURE_COMPLETE')]
    if trace != expected_trace: errors.append('Display operations, menu observations and completion are out of order')
    if [row['operation'] for row in rows] != list(operations):
        errors.append('Missing, reordered or extra display operations')
    else:
        for index,row in enumerate(rows):
            if row['observed'] != '1' or row['result'] != ('0' if index == 5 else '1'):
                errors.append(f'Unexpected operation result at {index}')
            if row['epoch'] != rows[0]['epoch'] or int(row['epoch']) <= 0 or int(row['generation']) <= 0:
                errors.append(f'Module/device identity invalid at {index}')
            if index != 5:
                if int(row['display']) <= 0 or any(int(number) <= 0 for key in ('logical','pixel') for number in row[key].split('x')):
                    errors.append(f'Invalid active display identity or dimensions at {index}')
                if any(row[key] != value for key,value in (('ready','1'),('window','1'),('available','1'),('hidden','1'),('fullscreen','0'))):
                    errors.append(f'Invalid active window/device at {index}')
                if row['native'] != '0' or row['outcome'] not in ('1','2') or int(row['valid']) & 3 != 3:
                    errors.append(f'Invalid successful backend result at {index}')
            if index:
                for key in ('generation','submitted','presented','failures'):
                    if int(row[key]) < int(rows[index-1][key]): errors.append(f'{key} regressed at {index}')
                if index != 5 and row['failures'] != rows[index-1]['failures']:
                    errors.append(f'Unexpected backend failure at {index}')
            if int(row['restart']) != int(rows[0]['restart']) + (0,1,1,2,2,2,3,3)[index]:
                errors.append(f'Restart readiness incorrectly published at {index}')
        for before,after,size in ((1,2,'960x540'),(3,4,'1280x720'),(6,7,'1280x720')):
            first,last = rows[before],rows[after]
            if (first['generation'] != last['generation'] or last['outcome'] != '2' or
                int(last['presented']) <= int(first['presented']) or int(last['submitted']) <= int(first['submitted']) or
                first['failures'] != last['failures'] or first['logical'] != size or last['logical'] != size):
                errors.append(f'No clean presentation of requested dimensions at {after}')
            for field in ('pixel','display','position','maximized','samples','interval','valid'):
                if first[field] != last[field]: errors.append(f'Observed {field} changed within device epoch at {after}')
        for index in (0,1,2,3,4,6,7):
            if rows[index]['interval'] != ('0' if index in (1,2) else '1'):
                errors.append(f'Requested swap interval was not retained at {index}')
            if rows[index]['samples'] != (str(expected_samples) if index in (1,2) else '0'):
                errors.append(f'Requested context samples were not retained at {index}')
        for before,after in ((0,1),(2,3),(5,6)):
            if int(rows[after]['generation']) <= int(rows[before]['generation']):
                errors.append(f'Device generation did not advance at {after}')
        if int(rows[5]['failures']) <= int(rows[4]['failures']) or rows[5]['ready'] != '0' or int(rows[5]['outcome']) < 3:
            errors.append('Missing display did not report failed initialization')
        for index in (3,4,6,7):
            for field in ('logical','pixel','display','position','maximized','samples','interval','valid'):
                if rows[index][field] != rows[0][field]:
                    errors.append(f'Restored {field} differs from actual baseline at {index}')
    reads = re.findall(r'^GUI_VALUE ([^=]+)=(.*)$',log,re.MULTILINE)
    expected = list(zip(FIELDS,(1,1.25,1,1,1,1,1280,1280))) * 4
    try:
        if [(key,float(value)) for key,value in reads] != expected:
            errors.append('Menu draft/live/baseline changed across device recreation')
    except ValueError: errors.append('Malformed menu readback')
    actions = re.findall(r'^UI_SETTINGS operation=settings\.system\.(\w+) result=(\d+) phase=(\d+) owner=(\d+) dirty=(\d+)$',log,re.MULTILINE)
    if (len(actions) != 2 or [(r[0],r[1],r[2],r[4]) for r in actions] != [('begin','0','1','0'),('edit','0','1','1')]
        or actions[0][3] == '0' or actions[0][3] != actions[1][3]):
        errors.append('Settings ownership/actions replayed or failed')
    return {'passed':not errors,'errors':errors,'rows':rows,'menu_reads':reads,'settings_actions':actions,
            'settings_confirmation_qualified':False,'replacement_acceptance':False}


def capture(args):
    output, runtime = args.output.resolve(), (ROOT / '.install').resolve()
    if output.exists():
        raise ValueError('Use a new output directory to preserve evidence')
    executable = runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    if not executable.is_file() or not (args.assets / 'q4base').is_dir():
        raise ValueError('Staged client and installed q4base assets are required')
    configurations = json.loads((ROOT / '.vscode/launch.json').read_text(encoding='utf-8-sig'))['configurations']
    prefix = '(SP) airdefense1 ' if args.mode == 'sp' else '(MP) q4dm1 '
    profile = next(row for row in configurations if row['name'].startswith(prefix) and row['name'].endswith('GL'))
    game = output / 'save/baseoq4'
    game.mkdir(parents=True)
    (game / 'display-smoke.q4ui').write_bytes(FIXTURE.read_bytes())
    cfg = game / 'display-device.cfg'
    cfg.write_text(script_for_renderer(args.renderer), encoding='utf-8')
    overrides = {
        'fs_basepath':str(args.assets.resolve()), 'fs_savepath':str(output/'save'), 'fs_devpath':str(output/'save'),
        'fs_game':'baseoq4', 'logFile':'2', 'logFileName':'logs/openq4.log',
        'r_fullscreen':'0', 'r_fullscreenDesktop':'0', 'r_borderless':'0', 'r_borderlessDefaultMigrated':'1',
        'r_hiddenWindow':'1', 'r_windowWidth':'1280', 'r_windowHeight':'720', 'r_mode':'-1',
        'r_customWidth':'1280', 'r_customHeight':'720', 'r_renderApi':args.renderer, 'r_multiSamples':'0',
        'r_swapInterval':'1', 'r_gamma':'1', 'r_brightness':'1', 'r_shadows':'1',
        'in_mouse':'0', 'in_joystick':'0', 'in_joystickRumble':'0',
        'g_autoScreenshot':'0', 'g_autoSkipCinematics':'1', 'g_autoExecAfterMapLoad':'display-device.cfg',
        'g_autoExecAfterMapLoadDelayMs':'3000', 'com_skipLoadingContinue':'1', 'com_loadingContinueAutoAdvance':'1',
        'com_maxfps':'60', 'ui_autoJoin':'1' if args.mode == 'mp' else '0',
        'ui_retainedScale':'1', 'ui_retainedDensity':'1.25' if args.mode == 'sp' else '2', 'ui_retainedTrace':'1',
    }
    command = [str(executable)]
    for key,value in overrides.items(): command.extend(['+set',key,value])
    original, index = profile['args'], 0
    while index < len(original):
        count = 3 if original[index].lower() in ('+set','+seta') and index + 2 < len(original) else 1
        if count == 1 or original[index+1].lower() not in {key.lower() for key in overrides}:
            command.extend(value.replace('${workspaceFolder}', str(ROOT)) for value in original[index:index+count])
        index += count
    binaries = [executable] + sorted(runtime.glob('renderer-*.dll')) + sorted((runtime/'baseoq4').glob('game-*.dll'))
    metadata = {'status':'running', 'profile':profile['name'], 'mode':args.mode, 'renderer':args.renderer,
        'command':command, 'cwd':str(runtime), 'windowed':True, 'hidden_window':True, 'host_input_injection':False,
        'capture_method':'engine screenshot command after gameplay and typed display operations',
        'cfg_sha256':digest(cfg), 'fixture_sha256':digest(FIXTURE), 'requested_samples':4 if args.renderer=='gl' else 0,
        'binaries':{str(path.relative_to(runtime)):digest(path) for path in binaries}}
    report = output/'capture.json'
    def save(): report.write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    save()
    started = time.monotonic()
    with (output/'process.log').open('w',encoding='utf-8') as process_log:
        options = {}
        if os.name == 'nt':
            startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 0
            options = {'startupinfo':startup,'creationflags':subprocess.CREATE_NO_WINDOW}
        process = subprocess.Popen(command,cwd=runtime,stdout=process_log,stderr=subprocess.STDOUT,**options)
        print(f'Display probe {args.mode}/{args.renderer}: PID {process.pid}, hidden/windowed, host input disabled.',flush=True)
        try: metadata['returncode'] = process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
            metadata['status']='timeout'; save(); return 1
    metadata['seconds'] = round(time.monotonic()-started,3)
    log_path = game/'logs/openq4.log'
    log = re.sub(r'\^[0-9]','',log_path.read_text(encoding='utf-8',errors='replace')) if log_path.is_file() else ''
    metadata['log_sha256'] = digest(log_path) if log_path.is_file() else None
    metadata['qualification'] = qualify(log, metadata['requested_samples'])
    metadata['diagnostics'] = [line for line in log.splitlines() if any(word in line for word in ('WARNING:','ERROR:','FATAL:'))]
    metadata['screenshots'] = []
    valid = metadata['returncode'] == 0 and metadata['qualification']['passed'] and not any('ERROR:' in line or 'FATAL:' in line for line in metadata['diagnostics'])
    actual = re.findall(r'Renderer API: requested=\S+ active=(\S+)',log)
    valid = valid and bool(actual) and actual[-1] == args.renderer
    metadata['active_renderer'] = actual[-1] if actual else None
    for name,size in (('applied',(960,540)),('restored',(1280,720)),('recovered',(1280,720)),('gameplay',(1280,720))):
        path = game/f'screenshots/display-{name}.tga'
        if not path.is_file() or path.stat().st_size < 18: valid=False; continue
        measured = struct.unpack_from('<HH',path.read_bytes(),12)
        valid = valid and measured == size
        metadata['screenshots'].append({'path':str(path.relative_to(output)),'size':measured,'sha256':digest(path)})
    metadata['status'] = 'captured_pending_visual_review' if valid else 'failed'
    save(); print(f'{metadata["status"]}: {report}',flush=True)
    return 0 if valid else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('sp','mp'),required=True)
    parser.add_argument('--renderer',choices=('gl','vulkan'),required=True)
    parser.add_argument('--assets',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    try: raise SystemExit(capture(args))
    except (OSError,ValueError,StopIteration) as error:
        print(f'Display capture failed: {error}',flush=True); raise SystemExit(1)
