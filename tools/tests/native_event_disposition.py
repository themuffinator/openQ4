#!/usr/bin/env python3
"""Actual portable ordinary-disposition ledger, ingress and continuity model.

Uses pinned SDL public types; source Poll/Observe/Copy and engine delivery are
counted doubles. No SDL pump, queue-sidecar integration, native/GUI activation,
OS input, full engine build, journal change or game is performed.
"""
from pathlib import Path
import argparse
import configparser
import hashlib
import importlib.util
import json
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDL_HEADERS = ("SDL_atomic.h SDL_audio.h SDL_begin_code.h SDL_blendmode.h SDL_camera.h "
    "SDL_close_code.h SDL_endian.h SDL_error.h SDL_events.h SDL_gamepad.h SDL_guid.h "
    "SDL_init.h SDL_iostream.h SDL_joystick.h SDL_keyboard.h SDL_keycode.h SDL_mouse.h "
    "SDL_mutex.h SDL_pen.h SDL_pixels.h SDL_platform_defines.h SDL_power.h SDL_properties.h "
    "SDL_rect.h SDL_scancode.h SDL_sensor.h SDL_stdinc.h SDL_surface.h SDL_thread.h "
    "SDL_touch.h SDL_video.h").split()

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdl-source', type=Path)
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--msvc-debug', action='store_true')
    parser.add_argument('--sanitizers', action='store_true')
    parser.add_argument('--no-mutations', action='store_true')
    parser.add_argument('--test-source', default='tools/tests/native/NativeRouteAccountingTest.cpp')
    args = parser.parse_args()
    (ROOT/'.tmp').mkdir(exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix='native-event-disposition-', dir=ROOT/'.tmp'))
    helper_path = ROOT/'tools/tests/sdl3_clipboard_status.py'
    spec = importlib.util.spec_from_file_location('sdl_source_helper', helper_path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    helper.ROOT = ROOT
    helper.FILES = ['include/SDL3/'+name for name in SDL_HEADERS]
    config = configparser.ConfigParser()
    config.read(ROOT/'subprojects/sdl3.wrap', encoding='utf-8')
    specification = dict(config['wrap-file'])
    assert specification['directory'] == 'SDL3-3.4.10', 'Review SDL public header closure on dependency update'
    sdl, provision, _ = helper.provision_source(args.sdl_source, specification, folder)
    names = ['src/sys/sdl3/NativeEventDisposition.h', 'src/sys/sdl3/NativeEventDisposition.cpp',
             'src/sys/sdl3/NativeQueueBatch.h', 'src/sys/sdl3/NativeQueueBatch.cpp',
             'src/ui/retained/TextInput.h', 'src/ui/retained/TextInput.cpp',
             'src/sys/EventQueueContinuity.h', 'tools/tests/native/NativeEventDispositionTest.cpp',
             'subprojects/packagefiles/sdl3/include/SDL3/SDL_openq4_native_fence.h',
             'tools/tests/native_event_disposition.py', 'tools/tests/sdl3_clipboard_status.py', 'subprojects/sdl3.wrap',
             'tools/tests/native/NativeRouteAccountingTest.cpp', args.test_source]
    before = {name: sha(ROOT/name) for name in names}
    flags = [args.compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-DSDL_STATIC_LIB']
    if os.name != 'nt': flags += ['-pthread']
    include = [ROOT, ROOT/'subprojects/packagefiles/sdl3/include', sdl/'include']
    flags += ['-I'+str(path) for path in include]
    if args.sanitizers:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g0']
    if args.msvc_debug:
        assert not args.sanitizers
        flags = [args.compiler, '/nologo', '/std:c++20', '/EHsc', '/W3', '/WX', '/MTd',
                 '/D_ITERATOR_DEBUG_LEVEL=2', '/DSDL_STATIC_LIB'] + ['/I'+str(path) for path in include]
    env = {**os.environ, 'TEMP': str(folder), 'TMP': str(folder), 'TMPDIR': str(folder)}
    report = {'passed': False, 'scope': __doc__, 'sources': before, 'cases': [], 'source_provision': provision,
              'sdl_headers': {name: sha(sdl/'include/SDL3'/name) for name in SDL_HEADERS}}
    source = ROOT/'src/sys/sdl3/NativeEventDisposition.cpp'
    text = source.read_text(encoding='utf-8-sig')
    projection = folder/'NativeEventDisposition.cpp'
    batch_source = ROOT/'src/sys/sdl3/NativeQueueBatch.cpp'
    batch_text = batch_source.read_text(encoding='utf-8-sig')
    batch_projection = folder/'NativeQueueBatch.cpp'
    batch_projection.write_text(batch_text, encoding='utf-8', newline='\n')
    # The quoted include in this copied TU resolves to its unchanged real header.
    flags += (['/I'+str(source.parent)] if args.msvc_debug else ['-I'+str(source.parent)])
    projection.write_text(text, encoding='utf-8', newline='\n')
    def run(command, name):
        result = subprocess.run(command, cwd=folder, env=env, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=120)
        log = folder/(name+'.log')
        log.write_text(result.stdout+result.stderr, encoding='utf-8')
        return result, log
    def compile(path, name):
        obj = folder/(name+'.obj')
        command = flags + (['/c', str(path), '/Fo:'+str(obj)] if args.msvc_debug else ['-c', str(path), '-o', str(obj)])
        result, log = run(command, name+'-compile')
        if result.returncode:
            raise RuntimeError(str(log)+'\n'+result.stdout+result.stderr)
        return obj
    try:
        common = [compile(ROOT/name, 'common-'+str(n)) for n, name in enumerate([
            'src/ui/retained/TextInput.cpp', args.test_source])]
        batch_base = compile(batch_projection, 'batch-base')
        ledger_base = None
        def case(name, mutation=False, batch_mutation=False):
            nonlocal ledger_base
            obj = compile(batch_projection if batch_mutation else projection, name)
            if not mutation: ledger_base = obj
            binary = folder/(name+'.exe')
            command = flags + [str(path) for path in [*common, obj, ledger_base if batch_mutation else batch_base]]
            command += ['/Fe:'+str(binary)] if args.msvc_debug else ['-o', str(binary)]
            result, log = run(command, name+'-link')
            if result.returncode: raise RuntimeError(str(log)+'\n'+result.stdout+result.stderr)
            result, log = run([str(binary)], name+'-run')
            record = {'name': name, 'exit': result.returncode, 'source_sha256': sha(projection),
                      'batch_source_sha256': sha(batch_projection),
                      'binary_sha256': sha(binary), 'log_sha256': sha(log)}
            report['cases'].append(record)
            if mutation:
                if not result.returncode or 'FAIL' not in result.stdout+result.stderr:
                    raise RuntimeError('Mutation not assertion-rejected: '+name+'\n'+result.stdout+result.stderr)
                record['rejected'] = True
            else:
                if result.returncode: raise RuntimeError(str(log)+'\n'+result.stdout+result.stderr)
                record['checks'] = int(re.findall(r'PASS (\d+) checks', result.stdout)[-1])
                print(result.stdout.strip(), flush=True)
        case('baseline')
        mutations = {
            'skip-record-order': ('index != next ||', '(void(index),false) ||'),
            'skip-record-identity': ('record.receipt == receipt &&', 'true &&'),
            'skip-record-sequence': ('record.queueSequence == records[next].tag.queue_sequence', 'true'),
            'skip-ticket-order': ('ticket.emission == completed + 1', 'true'),
            'skip-delivery-entry': ('phase != Phase::Delivering || !inFlight || !TicketMatches(ticket)', 'phase != Phase::Delivering || !TicketMatches(ticket)'),
            'allow-double-delivery-entry': ('phase != Phase::Delivering || inFlight || !TicketMatches(ticket)', 'phase != Phase::Delivering || !TicketMatches(ticket)'),
            'advance-before-delivered': ('if (completed == issued && !inFlight)', 'if (!inFlight)'),
            'skip-probe-after-source': ('!probe.Current(context)) return Fail', 'false) return Fail'),
            'skip-ingress-validation': ('!ingress->Validate(*source, receipt.batch, error) ||', 'false ||'),
            'skip-current-continuity': ('expected == receipt && probe.Current(context)', 'expected == receipt'),
            'ignore-reentry-latch': ('if (calling) return Fail(error, "Reentrant native disposition requires retirement");', 'if (calling) return false;'),
            'skip-postcallback-retirement': ('receipt.batch, error) || phase == Phase::Retired ||', 'receipt.batch, error) ||'),
            'permit-reused-ledger': ('phase != Phase::Empty || !identity', '!identity'),
            'skip-emission-cap': ('issued == MaxEmissions', 'false'),
            'allow-fence-emission': ('records[next].tag.kind == OQ4_QUEUE_FENCE || issued == MaxEmissions', 'issued == MaxEmissions'),
            'skip-batch-completion': ('phase != Phase::Between || next != records.size() || inFlight || completed != issued', 'phase == Phase::Retired'),
        }
        planned_mutations = {
            'retired-inspection-before-retire': ('calling || phase != Phase::Retired || !PlannedTicketMatches(ticket)', 'calling || !PlannedTicketMatches(ticket)'),
            'retired-inspection-reentry': ('calling || phase != Phase::Retired || !PlannedTicketMatches(ticket)', 'phase != Phase::Retired || !PlannedTicketMatches(ticket)'),
            'retired-inspection-foreign-thread': ('if (thread != std::this_thread::get_id() || calling || phase != Phase::Retired || !PlannedTicketMatches(ticket))', 'if (calling || phase != Phase::Retired || !PlannedTicketMatches(ticket))'),
            'retired-inspection-requires-admission': ('const auto& emission = emissions[static_cast<std::size_t>(ticket.emission - 1)];\n    NativeIssuedEmission result;', 'const auto& emission = emissions[static_cast<std::size_t>(ticket.emission - 1)];\n    if (!emission.admission) return false;\n    NativeIssuedEmission result;'),
            'retired-inspection-fabricates-terminal': ('result.terminal = emission.done;', 'result.terminal = true;'),
            'retired-inspection-loses-inflight': ('result.inFlight = inFlight && plannedInFlight == ticket.emission;', 'result.inFlight = false;'),
            'retired-inspection-loses-trigger': ('if (emission.trigger) result.trigger = {ticket.record, emission.trigger};', 'if (false) result.trigger = {ticket.record, emission.trigger};'),
            'schedule-default-changed': ('return BeginPlanned(input, provider, batch, NativeDispositionSchedule::BeforeSessionDrain, error);', 'return BeginPlanned(input, provider, batch, NativeDispositionSchedule::AtMousePollEntry, error);'),
            'schedule-unknown-accepted': ('if (schedule != NativeDispositionSchedule::BeforeSessionDrain && schedule != NativeDispositionSchedule::AtMousePollEntry)', 'if (false)'),
            'schedule-selection-swapped': ('? PollFirstOrder : SessionFirstOrder', '? SessionFirstOrder : PollFirstOrder'),
            'schedule-enum-increment': ('else currentPass = passOrder[checkpoint];', 'else currentPass = static_cast<NativeDispositionPass>(static_cast<unsigned>(currentPass) + 1);'),
            'schedule-empty-pass-reordered': ('inFlight || pass != currentPass ||', 'inFlight || (void(pass),false) ||'),
            'schedule-skip-final-pass': ('++checkpoint == passOrder.size()', '++checkpoint == passOrder.size()-1'),
            'plan-skip-admission-order': ('ticket.emission - 1 == NextEmission(admissionCursor[pass], emission.pass)', '(void(pass),true)'),
            'plan-skip-delivery-order': ('ticket.emission - 1 != NextEmission(deliveryCursor[static_cast<std::size_t>(currentPass)], currentPass)', 'false'),
            'plan-skip-pass-order': ('!translationClosed || emission.pass != currentPass || !emission.admission || emission.done ||\n            ticket.emission - 1 != NextEmission(deliveryCursor[static_cast<std::size_t>(currentPass)], currentPass)', '!translationClosed || !emission.admission || emission.done'),
            'plan-skip-child-parent': ('inFlight && plannedInFlight == emission.trigger', 'true'),
            'plan-skip-parent-child-admit': ('if (emission.child && !emissions[static_cast<std::size_t>(emission.child - 1)].admission)', 'if (false)'),
            'plan-skip-parent-kind': ('parent.pass != NativeDispositionPass::KeyboardPoll || parent.child', 'parent.child'),
            'plan-skip-parent-reuse': ('parent.pass != NativeDispositionPass::KeyboardPoll || parent.child', 'parent.pass != NativeDispositionPass::KeyboardPoll'),
            'plan-skip-cap': ('emissions.size() == MaxEmissions', 'false'),
            'plan-skip-ticket-ledger': ('ticket.record.receipt != receipt ||', ''),
            'plan-skip-ticket-sequence': ('ticket.record.queueSequence == records[emission.record].tag.queue_sequence', 'true'),
            'plan-skip-sibling-admission': ('if (emission.pass != NativeDispositionPass::SessionDeferred && !emission.admission)\n                return Fail(error, "Source record has an unadmitted sibling emission");', 'if (false && emission.admission) return false;'),
            'plan-skip-pass-drain': ('NextEmission(deliveryCursor[static_cast<std::size_t>(currentPass)], currentPass) != emissions.size()', 'false'),
            'plan-skip-all-passes': (' || (planned && !passesComplete)', ''),
        }
        quarantine_mutations = {
            'quarantine-lose-prefix': ('owner.quarantine=std::move(candidate);', 'candidate.reset();'),
            'quarantine-lose-current-copy': ('provisional=false;\n            failure.copied=true;', 'provisional=true;\n            failure.copied=true;'),
            'quarantine-keep-rejected-slot': ('if (candidate && provisional) candidate->prefix->events.pop_back();', 'if (false && candidate && provisional) candidate->prefix->events.pop_back();'),
            'quarantine-reuse-oom-generation': ('lastEpoch=status.providerEpoch; lastGeneration=status.generation; lastSequence=sequence;', 'lastSequence=sequence;'),
            'quarantine-reset-with-owned-prefix': ('if (quarantine) return Fail', 'if (false) return Fail'),
            'quarantine-wrong-thread-take': ('if (std::this_thread::get_id()!=thread) return false;\n    if (calling)', 'if (calling)'),
            'quarantine-hide-removal-boundary': ('failure.returnedOne=true;', 'failure.returnedOne=false;'),
            'quarantine-forget-copied-boundary': ('failure.copied=true; failure.reason=', 'failure.copied=false; failure.reason='),
        }
        if Path(args.test_source).name == 'NativeRouteAccountingTest.cpp':
            mutations.update(planned_mutations)
        if not args.no_mutations:
            for name, (old, new) in mutations.items():
                assert text.count(old) == 1, (name, 'mutation anchor not unique')
                projection.write_text(text.replace(old, new), encoding='utf-8', newline='\n')
                case(name, True)
            projection.write_text(text, encoding='utf-8', newline='\n')
            if Path(args.test_source).name == 'NativeRouteAccountingTest.cpp':
                for name, (old, new) in quarantine_mutations.items():
                    assert batch_text.count(old) == 1, (name, 'mutation anchor not unique')
                    batch_projection.write_text(batch_text.replace(old, new), encoding='utf-8', newline='\n')
                    case(name, True, True)
        report['sources_unchanged'] = before == {name: sha(ROOT/name) for name in names}
        report['passed'] = report['sources_unchanged']
    finally:
        projection.write_text(text, encoding='utf-8', newline='\n')
        batch_projection.write_text(batch_text, encoding='utf-8', newline='\n')
        report['logs'] = {p.name: sha(p) for p in folder.glob('*.log')}
        report['objects'] = {p.name: sha(p) for p in folder.glob('*.obj')}
        (folder/'result.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(folder/'result.json', flush=True)
    if not report['passed']: raise SystemExit('Source binding failed')

if __name__ == '__main__':
    main()
