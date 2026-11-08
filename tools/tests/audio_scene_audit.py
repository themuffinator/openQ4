#!/usr/bin/env python3
"""Audit authored speech in real campaign scenes: dialogue routing, Dialogue Volume and subtitles.

One hidden single-player client plays the opening of each map in turn with its
cinematics, AI and scripts running and s_logSpeech on: every start that may be
speech (classified dialogue, flagged voice-over, the radio channel or declared
by a lipSync) is logged with the routing the mixer gives it and the transcript
status the subtitle overlay has for it. Periodic probes list the audible voices
and the active subtitle cues, first with Dialogue Volume at 1 and then muted.

With --walk, each map continues past its opening through its scripted scenes:
the entities whose script call (directly or through the functions it calls)
makes a character speak, and the radio chatter entities, are read from the
installed .map and script files and triggered in authored order, with probes
between them, as the console `trigger` command does. Anything that reaches the
level's end is left out. Scenes start out of their gameplay order, so a line
whose speaker has not spawned yet stays silent; the report counts what plays.

With --progress the player is moved instead: teleported into each trigger volume the
player can touch, nearest first from the player start, so the map's own trigger chains
run as the player reaches them (doors, spawns, scripted scenes and the conversations
that wait on them). Volumes that hurt or reach the level's end are left out. With
--fight, the hostiles fighting the player halfway through each step are killed through
ordinary damage from the player (openq4_killHostiles), so the deaths, death targets and
the conversations scripts hold until a fight is won run too. This is still not a
playthrough: no route is walked and nothing is aimed or dodged.

Checks: declared or flagged speech always follows Dialogue Volume; muting it
silences every dialogue voice while other sounds continue; every cue shown has
a transcript. The report keeps shader and sample identifiers and counts, never
transcript text. No physical device, microphone or host input is used.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import zipfile

from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
STOCK_MAPS = ['game/airdefense1', 'game/airdefense2', 'game/hangar1', 'game/hangar2', 'game/mcc_landing',
              'game/medlabs', 'game/walker', 'game/dispersal', 'game/recomp', 'game/mcc_1', 'game/convoy1',
              'game/building_b', 'game/convoy2', 'game/convoy2b', 'game/hub1', 'game/hub2', 'game/mcc_2',
              'game/storage1', 'game/storage2', 'game/tram1', 'game/tram1b', 'game/process1', 'game/waste',
              'game/process2', 'game/network1', 'game/network2', 'game/putra', 'game/core1', 'game/core2']
AWAKENING_MAPS = ['game/m01_stranarus_trench1', 'game/m01_stranarus_trench2', 'game/m02_trianfac',
                  'game/m03_airassault', 'game/m04_prison', 'game/m05_bio', 'game/m06_mcc', 'game/m06_mcc_invasion',
                  'game/m07_race', 'game/m07_race1', 'game/m07_race2', 'game/m08_cryofac', 'game/m09_valkaryne']
SPEECH = re.compile(r'^SOUND_SPEECH game=(\d) time=(-?\d+) emitter=(\d+) speaker=(-?\d+) channel=(-?\d+) shader=(\S+) '
                    r'sample=(\S+) dialogue=(\d) authored=(\d) vo=(\d) radio=(\d) music=(\d) global=(\d) private=(\d) '
                    r'declared=(\d) transcript=(\w+)', re.M)
FIELDS = ('game', 'time', 'emitter', 'speaker', 'channel', 'shader', 'sample', 'dialogue', 'authored', 'vo', 'radio',
          'music', 'global', 'private', 'declared', 'transcript')
SOURCE_ROW = re.compile(r'^\s*\d+\s+(\d+)\s+[\d.]+\s+[\d.]+\s+(\S+)', re.M)
CHANNEL = re.compile(r'^SOUND_DIALOGUE emitter=(\d+) channel=(-?\d+) shader=(\S+) sample=(\S+) '
                     r'dialogue=(\d) transcript=(\w+)\r?\nSOUND_DISTANCE_BACKEND ([^\n]+)', re.M)
CHANNEL_END = re.compile(r'^SOUND_DIALOGUE_END channels=(\d+)\s*$', re.M)
# The harness's own markers are matched anywhere on a line: a map script that prints
# without a newline (process2's "colour swap left") leaves the next echo mid-line.
SCENE_MARK = re.compile(r'SCENE_(?:PROBE|STEP|MEMORY|END|BEGIN)\b', re.M)
CUE = re.compile(r'^Subtitle shader=(\S+) emitter=\d+ speaker=\d+ time=\d+ duration=\d+ radio=\d textBytes=\d+', re.M)
PROBE = re.compile(r'SCENE_PROBE (\S+) (\d+) (full|mute)\s*$', re.M)
ADMISSION = re.compile(r'^Audio admission: reserved=(\d+) KiB peak=(\d+) KiB budget=(\d+) MiB refused=(\d+)', re.M)
MEMORY = re.compile(r'^Audio memory: CPU=(\d+) KiB OpenAL=(\d+) KiB pending-input<=(\d+) KiB aggregate<=(\d+) KiB', re.M)
STREAMING = re.compile(r'^Vorbis streaming: reserved=(\d+) KiB', re.M)
VOICE = re.compile(r'^SOUND_VOICE source=\d+ state=(\w+) routed=\d queued=(\d) serial=\d+ offsetMs=[\d.-]+ sample=(\S+)', re.M)
PROCESS = re.compile(r'^Process memory: private=(\d+) KiB resident=(\d+) KiB', re.M)
STEP = re.compile(r'SCENE_STEP (\S+) (\d+) (\S+)\s*$', re.M)
KILLS = re.compile(r'KILL_HOSTILES killed=(\d+) survived=(\d+) protected=(\d+)\s*$', re.M)
SURVIVOR = re.compile(r'KILL_HOSTILE name=(\S+) class=(\S+) result=(survived|protected)\s*$', re.M)
# teleport/trigger refuse a dead player; god mode does not cover every hazard
# (a teleported vehicle driver, noGod damage), and the rest of the chain is lost.
DEAD = re.compile(r'You must be alive to use this command\.')
MAP_LOAD = re.compile(r'^Map: (\S+)', re.M)
SHUTDOWN = re.compile(r'^-+ Game Map Shutdown -+\s*$', re.M)
LOAD_TIME = re.compile(r'^\s*(\d+) msec to load (\S+)\s*$', re.M)
LOAD_PHASES = re.compile(r'^Map load phases: ([^\n]+) msec\s*$', re.M)
MEDIA_PHASES = re.compile(r'^Map media phases: ([^\n]+) msec\s*$', re.M)
ENTITY = re.compile(r'// entity \d+\n\{\n((?:"[^"\n]*" "[^"\n]*"\n)+)')
FUNCTION = re.compile(r'\b(?:void|float|entity|vector|string|boolean)\s+(\w+)\s*\([^;{)]*\)\s*\{')
# What makes a character speak in the stock and Awakening scripts: the actor speak
# event (aiSpeak and its wrappers call it) and the lipSync or voice-over shader names.
SPEAKS = re.compile(r'\.speak\s*\(|"lipsync_|"vo_', re.I)


def content_files(roots, want):
    """Installed files a game layer sees, later roots and later paks overriding earlier."""
    files = {}
    for root in roots:
        for pak in sorted(root.glob('*.pk4'), key=lambda p: p.name.lower()):
            with zipfile.ZipFile(pak) as archive:
                for name in archive.namelist():
                    if want(name.lower()):
                        files[name.lower()] = archive.read(name).decode('latin-1')
        for path in root.rglob('*'):
            name = path.relative_to(root).as_posix().lower()
            if path.is_file() and want(name):
                files[name] = path.read_text(encoding='latin-1')
    return files


def script_functions(scripts):
    """Every script function by qualified name: (namespace, body without comments)."""
    bodies = {}
    for text in scripts.values():
        text = re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', ' ', text, flags=re.S))
        spans, open_spaces, depth = [], [], 0
        for m in re.finditer(r'namespace\s+(\w+)\s*\{|\{|\}', text):
            if m.group(1):
                depth += 1
                open_spaces.append((m.group(1), depth, m.end()))
            elif m.group(0) == '{':
                depth += 1
            else:
                if open_spaces and open_spaces[-1][1] == depth:
                    space, _, start = open_spaces.pop()
                    spans.append((start, m.start(), space))
                depth -= 1
        for m in FUNCTION.finditer(text):
            space = '::'.join(n for s, e, n in sorted(spans) if s <= m.start() < e)
            depth, end = 1, m.end()
            while depth and end < len(text):
                depth += {'{': 1, '}': -1}.get(text[end], 0)
                end += 1
            bodies[(space + '::' if space else '') + m.group(1)] = (space, text[m.end():end])
    return bodies


# Trigger classes that can react to player contact (not activation-only counters).
TOUCHED = ('trigger_once', 'trigger_multiple', 'trigger_once_entityname', 'trigger_facing')


def origin_of(entity):
    try:
        origin = tuple(float(v) for v in entity.get('origin', '').split())
        return origin if len(origin) == 3 and all(math.isfinite(v) for v in origin) else None
    except ValueError:
        return None


def player_touchable(entity):
    """Conservative candidates; activation still depends on the running scene."""
    if entity.get('classname') not in TOUCHED:
        return False
    def enabled(key):
        return entity.get(key, '0').strip() not in ('', '0')
    if enabled('onlyvehicle') or enabled('flashlight_trigger'):
        return False
    if not enabled('anytouch') and (enabled('notouch') or enabled('noclient')):
        return False
    if entity.get('classname') == 'trigger_once_entityname':
        matches = entity.get('entityname') == 'player1'
        if matches == enabled('excludeentityname'):
            return False
    return True


def nearest_route(start, entities):
    """Entities ordered by a nearest-neighbour tour from start, a rough route through the level."""
    left, route, here = list(entities), [], start
    while left:
        best = min(left, key=lambda e: sum((a - b) ** 2 for a, b in zip(origin_of(e), here)))
        left.remove(best)
        route.append(best)
        here = origin_of(best)
    return route


def scene_steps(roots, maps, progress=False, blocked_exits=None):
    """Per map, the (classname, entity) pairs to visit: the triggers whose script starts
    speech in authored order, or with progress every trigger volume the player touches."""
    scripts = content_files(roots, lambda n: n.endswith('.script') and n.startswith(('scripts/', 'maps/')))
    wanted = {f'maps/{m.lower()}.map' for m in maps}
    sources = content_files(roots, lambda n: n in wanted)
    bodies = script_functions(scripts)

    def resolve(space, name):
        if '::' in name:
            return name if name in bodies else None
        if space and f'{space}::{name}' in bodies:
            return f'{space}::{name}'
        return name if name in bodies else None

    calls, refs = {}, {}
    for name, (space, body) in bodies.items():
        calls[name] = {r for r in (resolve(space, c) for c in re.findall(r'\b((?:\w+::)*\w+)\s*\(', body)) if r and r != name}
        refs[name] = {r.lower() for r in re.findall(r'\$(\w+)', body)}
    speech = {name for name, (_, body) in bodies.items() if SPEAKS.search(body)}
    grown = True
    while grown:
        grown = False
        for name in bodies:
            if name not in speech and calls[name] & speech:
                speech.add(name)
                grown = True
    steps = {}
    for name in maps:
        text = sources.get(f'maps/{name.lower()}.map', '')
        entities = [{k.lower(): v for k, v in re.findall(r'"([^"]*)" "([^"]*)"', block)} for block in ENTITY.findall(text)]
        by_name = {e.get('name', '').lower(): e for e in entities}
        endings = {n for n, e in by_name.items() if e.get('classname') == 'target_endlevel'}

        def ends_level(node, seen):
            # Entity targets, script calls and the entities a function names, followed to a level end.
            if node in seen:
                return False
            seen.add(node)
            if node.startswith('$'):
                entity = by_name.get(node[1:])
                if entity is None:
                    return False
                if node[1:] in endings:
                    return True
                following = ['$' + v.lower() for k, v in entity.items() if k.startswith('target')]
                following += [resolve('', v) for k, v in entity.items() if k.startswith('call')]
                return any(n and ends_level(n, seen) for n in following)
            return node in bodies and (any(ends_level(c, seen) for c in calls[node]) or
                                       any(ends_level('$' + r, seen) for r in refs[node]))

        chosen = []
        if progress:
            # Moving near an unrelated trigger can touch an overlapping exit,
            # and scripts can reach an exit indirectly. Remove the end targets
            # in this disposable session as well as excluding their chains.
            # A timed objective the route cannot keep pace with (Awakening's race
            # lockdown gates) fails the mission, which ends the level too.
            failures = {n for n, e in by_name.items() if e.get('classname') == 'item_objectivefailed'}
            if blocked_exits is not None:
                blocked_exits[name] = sorted(by_name[n]['name'] for n in endings | failures)
            starts = [origin_of(e) for e in entities if e.get('classname') == 'info_player_start' and origin_of(e)]
            volumes = [e for e in entities if player_touchable(e) and e.get('name') and origin_of(e)
                       and not ends_level('$' + e['name'].lower(), set())]
            steps[name] = [(e['classname'], e['name']) for e in nearest_route(starts[0] if starts else (0, 0, 0), volumes)]
            continue
        for entity in entities:
            classname, entity_name = entity.get('classname', ''), entity.get('name', '')
            if classname == 'worldspawn' or not entity_name:
                continue
            functions = [resolve('', v) for k, v in entity.items() if k.startswith('call')]
            if classname != 'func_radiochatter' and not any(f in speech for f in functions if f):
                continue
            if not ends_level('$' + entity_name.lower(), set()):
                chosen.append((classname, entity_name))
        steps[name] = chosen
    return steps


def sample_key(name):
    """Canonical sample identity: lower case, forward slashes, no extension."""
    name = name.replace('\\', '/').lower()
    stem, dot, extension = name.rpartition('.')
    return stem if dot and '/' not in extension else name


def scene_entries(maps, repeat):
    """(label, map) per opening; a repeated sweep labels each pass's openings passN/map.
    The console's echo splits punctuation such as '#' into separate words, so labels
    keep to letters, digits and slashes, as map names do."""
    return [(f'pass{run}/{name}' if repeat > 1 else name, name) for run in range(1, repeat + 1) for name in maps]


def scene_cfgs(entries, seconds, interval, walk=None, step_msec=4000, tail=0, move='trigger', blocked_exits=None,
               fight=False):
    """One cfg per opening; with a walk, each map's scene triggers follow its opening probes.
    With fight, the hostiles engaged halfway through each step are killed before its probe,
    and once more before the tail."""
    probes = max(1, (seconds * 1000) // (interval + 150))
    files = []
    for index, (name, level) in enumerate(entries):
        # Disarmed until this cfg re-arms it for the next map, so a level change no
        # step should make cannot run this cfg again on top of itself.
        steps = ['set g_autoExecAfterMapLoad ""', 'god', f'echo SCENE_BEGIN {name}']
        for entity in (blocked_exits or {}).get(level, []):
            steps += [f'echo SCENE_SUPPRESS_EXIT {name} "{entity}"', f'remove "{entity}"']
        count = 0

        def probe(wait):
            nonlocal count
            count += 1
            return [f'waitMsec {wait}', f'echo SCENE_PROBE {name} {count - 1} full', 'listPlayingSounds', 's_dialogueVoices', 'listSubtitles', 'listSoundVoices',
                    'set s_dialogueVolume 0', 'waitMsec 150', f'echo SCENE_PROBE {name} {count - 1} mute', 'listPlayingSounds',
                    's_dialogueVoices', 'set s_dialogueVolume 1']
        for _ in range(probes):
            steps += probe(interval)
        if walk is not None:
            for number, (_, entity) in enumerate(walk.get(level, [])):
                # Quoted names survive idLexer tokenization even when they
                # start with a digit (e.g. recomp's 2wavfinal_berserker_trig).
                steps += [f'echo SCENE_STEP {name} {number} "{entity}"', f'{move} "{entity}"']
                if fight:
                    steps += [f'waitMsec {step_msec // 2}', 'openq4_killHostiles']
                    steps += probe(step_msec - step_msec // 2)
                else:
                    steps += probe(step_msec)
            if fight:
                steps.append('openq4_killHostiles')
            # Let the conversations the last steps started finish.
            for _ in range((tail * 1000) // (interval + 150)):
                steps += probe(interval)
        # Residency after the opening: a filter no sample matches prints only the totals.
        # The voice report shows which streamed tracks are playing on bounded queues.
        steps += [f'echo SCENE_MEMORY {name}', 'listSounds openq4_residency_totals_only', 'listSoundVoices',
                  f'echo SCENE_END {name}']
        if index + 1 < len(entries):
            # Re-armed in the same command pass as the load, so it fires on the next map only.
            steps += [f'set g_autoExecAfterMapLoad "scene-{index + 1}.cfg"', f'map {entries[index + 1][1]}']
        else:
            steps += ['set g_autoExecAfterMapLoad ""', 'echo SCENE_COMPLETE', 'quit']
        files.append('\n'.join(steps) + '\n')
    return files


def launch(args, root, entries, walk=None, blocked_exits=None):
    game_dir = 'q4xbase' if args.awakening else 'baseoq4'
    game = root / 'save' / game_dir
    game.mkdir(parents=True, exist_ok=True)
    move = 'teleport' if args.progress else 'trigger'
    for index, text in enumerate(scene_cfgs(entries, args.seconds, args.interval, walk, args.step_msec, args.tail, move,
                                            blocked_exits, getattr(args, 'fight', False))):
        (game / f'scene-{index}.cfg').write_text(text, encoding='utf-8')
    config = root / 'alsoft.conf'
    config.write_text('[general]\ndrivers=wave\nchannels=stereo\nhrtf=false\n[wave]\nfile=' + str(root / 'mix.wav') + '\n',
                      encoding='utf-8')
    cvars = {
        'fs_basepath': str(args.assets.resolve(strict=True)), 'fs_savepath': str(root / 'save'), 'fs_devpath': str(root / 'save'),
        'fs_game': game_dir, 'com_gameMode': 'SP', 'si_gameType': 'singleplayer', 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0', 'r_borderlessDefaultMigrated': '1',
        'win_allowMultipleInstances': '1', 'r_hiddenWindow': '1', 'r_windowWidth': '1280', 'r_windowHeight': '720',
        'r_mode': '-1', 'r_customWidth': '1280', 'r_customHeight': '720', 'r_renderApi': 'gl', 'r_multiSamples': '0',
        'r_swapInterval': '1', 'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
        's_noSound': '1' if getattr(args, 'silent', False) else '0', 's_muteUnfocused': '0', 'com_skipLogoVideos': '1', 'g_autoSkipCinematics': '0',
        's_numberOfSpeakers': '2', 's_openALHRTF': '0', 's_dialogueVolume': '1', 's_logSpeech': '1', 'ui_subtitles': '1',
        's_asyncSampleDecode': '1' if args.decode_mode == 'worker' else '0',
        's_asyncStreamDecode': '1' if getattr(args, 'stream_decode', 'worker') == 'worker' else '0',
        's_decodeMemoryMB': str(args.decode_memory_mb), 's_audioMemoryMB': str(args.audio_memory_mb),
        'com_showLevelLoadTimes': '1',
        'com_skipLoadingContinue': '1', 'com_maxfps': '60', 'ui_autoJoin': '0', 'ui_retained': '1', 'sys_lang': 'english',
        'g_autoExecAfterMapLoad': 'scene-0.cfg', 'g_autoExecAfterMapLoadDelayMs': '2000',
    }
    if args.awakening:
        cvars['fs_awakeningpath'] = str(args.awakening.resolve(strict=True))
    exe = ROOT / '.install' / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    command = [str(exe)]
    for key, value in cvars.items():
        command += ['+set', key, value]
    command += ['+map', entries[0][1]]
    options = {'env': {**os.environ, 'ALSOFT_CONF': str(config), 'ALSOFT_DRIVERS': 'wave'}}
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    walked = sum(len(walk.get(level, [])) for _, level in entries) if walk else 0
    timeout = args.timeout or 90 + len(entries) * (args.seconds + 60 + (args.tail if walk else 0)) + walked * (args.step_msec + 1500) // 1000
    started = time.monotonic()
    with (root / 'process.log').open('w') as out:
        try:
            code = subprocess.run(command, cwd=ROOT / '.install', stdout=out, stderr=subprocess.STDOUT,
                                  timeout=timeout, **options).returncode
        except subprocess.TimeoutExpired:
            code = None  # a map that failed to load stalls the chain; report what ran
    path = game / 'logs/openq4.log'
    log = path.read_text(encoding='utf-8', errors='replace') if path.exists() else ''
    return code, log, command, round(time.monotonic() - started, 1)


def probe_state(body):
    """Join audible sources to current channels, never to earlier uses of a sample."""
    channels = list(CHANNEL.finditer(body))
    end = CHANNEL_END.search(body)
    sources = {}
    for channel in channels:
        backend = dict(re.findall(r'(\w+)=([^\s]+)', channel[7]))
        if backend.get('available') == '1' and 'source' in backend:
            sources[backend['source']] = {'sample': sample_key(channel[4]), 'shader': channel[3].lower(),
                                          'dialogue': channel[5] == '1', 'transcript': channel[6]}
    dialogue, effects, mapped, unattributed = [], [], [], []
    for source, sample in SOURCE_ROW.findall(body):
        sample = sample_key(sample)
        channel = sources.get(source)
        if channel is None or channel['sample'] != sample:
            unattributed.append(f'{source}:{sample}')
        elif channel['dialogue']:
            dialogue.append(sample)
            if channel['transcript'] == 'text':
                mapped.append((sample, channel['shader']))
        else:
            effects.append(sample)
    return {'dialogue': dialogue, 'effects': effects, 'mapped': mapped,
            'cues': [c.lower() for c in CUE.findall(body)],
            'routing_complete': bool(end) and int(end[1]) == len(channels) and not unattributed,
            'unattributed': unattributed}


def load_timing(segment):
    """Only a completed load with both phase reports is timing evidence.

    Final sound-media time includes joining workers and uploads, not all audio
    work (which overlaps gameInit); the sound-off comparison measures wall cost.
    """
    loads = list(LOAD_TIME.finditer(segment))
    if len(loads) != 1:
        return {}
    phases = LOAD_PHASES.search(segment, loads[0].end())
    media = MEDIA_PHASES.search(segment, loads[0].end())
    if not phases or not media:
        return {}
    parse = lambda match: {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', match[1])}
    load, finish = parse(phases), parse(media)
    total = int(loads[0][1])
    load_keys = {'renderWorld', 'gameInit', 'playerSpawn', 'cacheJoin', 'mediaFinish', 'settle', 'total'}
    media_keys = {'render', 'sound', 'decl', 'bse', 'loadSize', 'ui', 'total'}
    if not load_keys <= load.keys() or not media_keys <= finish.keys() or \
            load['total'] != total or finish['total'] != load['mediaFinish']:
        return {}
    return {'map': loads[0][2], 'total_ms': total, 'sound_finalize_ms': finish['sound'],
            'load_phases_ms': load, 'media_phases_ms': finish}


def load_cost(loud, quiet):
    """Signed differences: cache/scheduling noise is not clamped into a speedup."""
    if not loud or not quiet or loud['map'] != quiet['map']:
        return {}
    return {'audio_load_ms': loud['total_ms'] - quiet['total_ms'],
            'sound_finalize_ms': loud['sound_finalize_ms'], 'silent_load_ms': quiet['total_ms']}


def audit(log, labels):
    """Per opening: its speech starts and probes, from the log segment ending at its SCENE_END."""
    scenes = {}
    begin = 0
    for name in labels:
        marker = re.search(rf'SCENE_END {re.escape(name)}\s*$', log[begin:], re.M)
        segment = log[begin:begin + marker.end()] if marker else log[begin:]
        if marker:
            begin += marker.end()
        speech = [dict(zip(FIELDS, m)) for m in SPEECH.findall(segment)]
        speech = [s for s in speech if s['game'] == '1']
        # Scripted scenes: what each step's trigger found, and the speech after the opening.
        begin_at = re.search(rf'SCENE_BEGIN {re.escape(name)}\s*$', segment, re.M)
        step_marks = list(STEP.finditer(segment))
        first_dead = DEAD.search(segment)
        dead_at = sum(m.start() < first_dead.start() for m in step_marks) - 1 if first_dead else None
        missing_steps = sum('entity not found' in segment[m.end():(step_marks[i + 1].start() if i + 1 < len(step_marks) else len(segment))]
                                                   .split('SCENE_PROBE', 1)[0] for i, m in enumerate(step_marks))
        split = step_marks[0].start() if step_marks else len(segment)
        walked, opening = ([dict(zip(FIELDS, m)) for m in SPEECH.findall(part)] for part in (segment[split:], segment[:split]))
        walked = [s for s in walked if s['game'] == '1' and s['dialogue'] == '1']
        opening = [s for s in opening if s['game'] == '1' and s['dialogue'] == '1']
        level_changes = MAP_LOAD.findall(segment[begin_at.end():]) if begin_at else []
        # The map ending inside its own scene (an error drop, a failed mission, a
        # dead player's game over) loses the rest of the chain as surely as a change.
        shutdown = SHUTDOWN.search(segment, begin_at.end()) if begin_at else None
        if shutdown:
            level_changes.append(f'shutdown after step {sum(m.start() < shutdown.start() for m in step_marks) - 1}')
        text_shaders = {s['shader'].lower() for s in speech if s['transcript'] == 'text'}
        probes = {}
        marks = list(PROBE.finditer(segment))
        for i, m in enumerate(marks):
            next_mark = SCENE_MARK.search(segment, m.end())
            body = segment[m.end():next_mark.start() if next_mark else len(segment)]
            probes[(int(m[2]), m[3])] = probe_state(body)
        misrouted = sorted({f"{s['shader']} {s['sample']}" for s in speech
                            if (s['declared'] == '1' or s['authored'] == '1') and s['music'] == '0' and s['dialogue'] == '0'})
        mute_leaks, effects_while_muted, heard, cue_without_text = [], 0, 0, []
        reach, missed = 0, []
        for (index, kind), probe in sorted(probes.items()):
            if kind == 'mute':
                mute_leaks += probe['dialogue']
                effects_while_muted += bool(probe['effects'])
                continue
            heard += bool(probe['dialogue'])
            cue_without_text += [c for c in probe['cues'] if c not in text_shaders]
            for sample, shader in probe['mapped']:
                if shader in probe['cues']:
                    reach += 1
                elif len(probe['cues']) < 2:  # the overlay shows the two newest utterances
                    missed.append(sample)
        statuses = Counter(s['transcript'] for s in speech if s['dialogue'] == '1')
        kills = [tuple(int(v) for v in k) for k in KILLS.findall(segment)]
        survivors = Counter(f'{c} {r}' for _, c, r in SURVIVOR.findall(segment))
        memory = {}
        at = re.search(rf'SCENE_MEMORY {re.escape(name)}\s*$', segment, re.M)
        if at:
            tail = segment[at.end():]
            # The totals cover every sample; the filter only empties the per-sample listing.
            admission, totals, streaming = ADMISSION.search(tail), MEMORY.search(tail), STREAMING.search(tail)
            process = PROCESS.search(tail)
            if process:
                # Present with s_noSound too, where the audio figures are absent.
                memory = {'process_private_kib': int(process[1]), 'process_resident_kib': int(process[2])}
            if admission and totals:
                queued = VOICE.findall(tail)
                memory |= {'reserved_kib': int(admission[1]), 'payload_peak_kib': int(admission[2]),
                          'budget_mib': int(admission[3]), 'refused_total': int(admission[4]),
                          'cpu_kib': int(totals[1]), 'openal_kib': int(totals[2]), 'aggregate_kib': int(totals[4]),
                          'stream_reserved_kib': int(streaming[1]) if streaming else None,
                          'voices_in_use': len(queued),
                          'streams_playing': sorted(sample_key(v[2]) for v in queued if v[1] == '1' and v[0] == 'playing'),
                          'streams_not_playing': sorted(sample_key(v[2]) for v in queued if v[1] == '1' and v[0] != 'playing')}
        scenes[name] = {
            'loaded': bool(marker),
            'loading': load_timing(segment[:begin_at.start()]) if begin_at else {},
            'memory': memory,
            'speech_starts': len(speech),
            'dialogue_starts': sum(s['dialogue'] == '1' for s in speech),
            'unique_dialogue_shaders': len({s['shader'].lower() for s in speech if s['dialogue'] == '1'}),
            'radio_starts': sum(s['radio'] == '1' for s in speech),
            'radio_not_dialogue': sorted({s['shader'] for s in speech if s['radio'] == '1' and s['dialogue'] == '0'}),
            'vo_flag_only': sorted({s['shader'] for s in speech if s['vo'] == '1' and s['dialogue'] == '0' and s['declared'] == '0'}),
            'transcripts': dict(statuses),
            'untranscribed_shaders': sorted({s['shader'] for s in speech if s['dialogue'] == '1' and s['transcript'] != 'text'}),
            'misrouted_speech': misrouted,
            'probes': len(probes) // 2,
            'voice_routing_recorded': bool(probes) and all(p['routing_complete'] for p in probes.values()),
            'unattributed_audible_sources': sorted({s for p in probes.values() for s in p['unattributed']}),
            'dialogue_heard_at_full': heard,
            'mute_leaks': sorted(set(mute_leaks)),
            'effects_while_muted': effects_while_muted,
            'cues_without_transcript': sorted(set(cue_without_text)),
            'mapped_lines_shown': reach,
            'mapped_lines_missed': sorted(set(missed)),
            'scene_steps': len(step_marks),
            'scene_steps_not_spawned': missing_steps,
            'player_dead_at_step': dead_at,
            'player_dead_commands': len(DEAD.findall(segment)),
            'fight_passes': len(kills),
            'hostiles_killed': sum(k[0] for k in kills),
            'hostiles_survived': sum(k[1] for k in kills),
            'hostiles_protected': sum(k[2] for k in kills),
            'hostile_survivor_classes': dict(sorted(survivors.items())),
            'scene_dialogue_starts': len(walked),
            'streams_observed_playing': sorted({sample_key(v[2]) for v in VOICE.findall(segment)
                                                if v[1] == '1' and v[0] == 'playing'}),
            'scene_dialogue_shaders': sorted({s['shader'].lower() for s in walked}),
            'opening_dialogue_shaders': sorted({s['shader'].lower() for s in opening}),
            'unexpected_level_changes': level_changes,
            'dialogue_shaders': sorted({s['shader'].lower() for s in speech if s['dialogue'] == '1'}),
            'transcribed_shaders': sorted(text_shaders),
        }
    return scenes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True, help='Read-only Quake 4 install root (the folder holding q4base)')
    parser.add_argument('--awakening', type=Path, help='Read-only Awakening content root (the folder holding q4xbase)')
    parser.add_argument('--output', type=Path, required=True, help='A new directory under the project .tmp')
    parser.add_argument('--maps', help='Comma-separated maps (default: every campaign map of the game)')
    parser.add_argument('--seconds', type=int, default=30, help='Seconds of each opening to audit')
    parser.add_argument('--interval', type=int, default=3000, help='Milliseconds between probes')
    parser.add_argument('--repeat', type=int, default=1, help='Play the maps this many times in one client (a long session)')
    parser.add_argument('--walk', action='store_true', help="Trigger each map's speech scenes after its opening")
    parser.add_argument('--progress', action='store_true',
                        help='With --walk: teleport the player through every trigger volume it can touch instead')
    parser.add_argument('--fight', action='store_true',
                        help='With --progress: kill the hostiles fighting the player halfway through each step')
    parser.add_argument('--step-msec', type=int, default=4000, help='Milliseconds between scene triggers')
    parser.add_argument('--tail', type=int, default=15, help='Seconds of probes after the last scene of a map')
    parser.add_argument('--silent-baseline', action='store_true',
                        help='Replay the openings with s_noSound 1 and report process-memory and load-time differences')
    parser.add_argument('--decode-mode', choices=['worker', 'inline'], default='worker',
                        help='Pin asynchronous or main-thread sample decoding for loading comparisons')
    parser.add_argument('--stream-decode', choices=['worker', 'inline'], default='worker',
                        help='Decode streamed blocks on job workers (default) or on the main thread')
    parser.add_argument('--decode-memory-mb', type=int, default=64, help='Pending decode budget (16-1024 MiB)')
    parser.add_argument('--audio-memory-mb', type=int, default=256, help='Aggregate payload budget (32-2048 MiB)')
    parser.add_argument('--timeout', type=int, default=0)
    args = parser.parse_args()
    args.repeat = max(1, args.repeat)
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('choose a new output directory under the project .tmp')
    if args.walk and args.silent_baseline:
        parser.error('the silent baseline compares openings; run it without --walk')
    if args.progress and not args.walk:
        parser.error('--progress requires --walk')
    if args.fight and not args.progress:
        parser.error('--fight requires --progress')
    if args.seconds < 1 or args.interval < 1 or args.step_msec < 1 or args.tail < 0:
        parser.error('seconds, interval and step-msec must be positive; tail must be nonnegative')
    if not 16 <= args.decode_memory_mb <= 1024 or not 32 <= args.audio_memory_mb <= 2048:
        parser.error('decode-memory-mb must be 16-1024 and audio-memory-mb must be 32-2048')
    verify_runtime_is_current(ROOT / '.install')
    maps = [m.strip() for m in args.maps.split(',')] if args.maps else (AWAKENING_MAPS if args.awakening else STOCK_MAPS)
    entries = scene_entries(maps, args.repeat)
    labels = [label for label, _ in entries]
    walk = None
    blocked_exits = {}
    if args.walk:
        roots = [args.assets.resolve(strict=True) / 'q4base']
        if args.awakening:
            roots.append(args.awakening.resolve(strict=True) / 'q4xbase')
        walk = scene_steps(roots, maps, args.progress, blocked_exits)
    output.mkdir(parents=True)
    code, log, command, seconds = launch(args, output, entries, walk, blocked_exits)
    scenes = audit(log, labels)
    loaded = [name for name, scene in scenes.items() if scene['loaded']]
    total = Counter()
    for scene in scenes.values():
        total.update({'speech_starts': scene['speech_starts'], 'dialogue_starts': scene['dialogue_starts'],
                      'radio_starts': scene['radio_starts'], 'probes': scene['probes'],
                      'mapped_lines_shown': scene['mapped_lines_shown'], 'scene_steps': scene['scene_steps'],
                      'scene_steps_not_spawned': scene['scene_steps_not_spawned'],
                      'scene_dialogue_starts': scene['scene_dialogue_starts'],
                      'hostiles_killed': scene['hostiles_killed'], 'hostiles_survived': scene['hostiles_survived'],
                      'hostiles_protected': scene['hostiles_protected']})
        total.update({'transcript_' + key: value for key, value in scene['transcripts'].items()})
    # The corpus the run reached: distinct dialogue shaders, and how many had a transcript.
    heard = set().union(*(s['dialogue_shaders'] for s in scenes.values())) if scenes else set()
    total['distinct_dialogue_shaders'] = len(heard)
    total['distinct_transcribed_shaders'] = len(heard & set().union(*(s['transcribed_shaders'] for s in scenes.values())))
    total['distinct_scene_only_shaders'] = len(set().union(*(s['scene_dialogue_shaders'] for s in scenes.values())) -
                                               set().union(*(s['opening_dialogue_shaders'] for s in scenes.values())))
    checks = {
        'complete': code == 0 and 'SCENE_COMPLETE' in log and len(loaded) == len(entries),
        'speech_heard': total['dialogue_starts'] > 0,
        'voice_routing_recorded': all(s['voice_routing_recorded'] for s in scenes.values()),
        'declared_speech_is_dialogue': all(not s['misrouted_speech'] for s in scenes.values()),
        'muted_dialogue_silent': all(not s['mute_leaks'] for s in scenes.values()),
        'effects_continue_while_muted': all(s['effects_while_muted'] > 0 for s in scenes.values() if s['loaded']),
        'dialogue_audible_at_full': sum(s['dialogue_heard_at_full'] for s in scenes.values()) > 0,
        'cues_have_transcripts': all(not s['cues_without_transcript'] for s in scenes.values()),
        # Every opening, and the level changes between them, fit the pinned budget.
        'residency_recorded': all(s['memory'] for s in scenes.values() if s['loaded']),
        'loading_recorded': all(s['loading'] for s in scenes.values()),
        'no_audio_refusals': all(s['memory'].get('refused_total', 1) == 0 for s in scenes.values() if s['loaded']),
        # Observe throughout the scene, not only its final residency snapshot:
        # a progression step can legitimately stop the last music/ambience track.
        # Later maps still have to play a stream on a recycled voice.
        'streams_play_on_recycled_voices': len(entries) < 2 or
            any(s['streams_observed_playing'] for s in list(scenes.values())[1:]),
    }
    if walk is not None:
        # Every scene trigger ran on its own map: none changed the level, and the
        # scenes started speech the openings did not.
        checks['scenes_walked'] = total['scene_steps'] == sum(len(walk.get(level, [])) for _, level in entries)
        checks['scenes_stay_on_their_map'] = all(not s['unexpected_level_changes'] for s in scenes.values())
        checks['scenes_add_speech'] = total['distinct_scene_only_shaders'] > 0
        checks['player_survives'] = all(s['player_dead_at_step'] is None for s in scenes.values())
    if args.fight:
        # Every step and the tail ran their kill pass, and the passes found fights to win.
        checks['fights_resolved'] = all(s['fight_passes'] == s['scene_steps'] + 1 for s in scenes.values() if s['loaded']) \
            and total['hostiles_killed'] > 0
    if args.silent_baseline:
        # The same openings with sound off: what audio adds to the whole process,
        # and how much of that the managed payloads above do not account for.
        quiet_root = output / 'silent'
        quiet_code, quiet_log, _, quiet_seconds = launch(argparse.Namespace(**vars(args), silent=True), quiet_root, entries)
        quiet = audit(quiet_log, labels)
        cost = {}
        loading_cost = {}
        for name in labels:
            loud, still = scenes[name]['memory'], quiet[name]['memory']
            if 'reserved_kib' in loud and 'process_private_kib' in loud and 'process_private_kib' in still:
                added = loud['process_private_kib'] - still['process_private_kib']
                cost[name] = {'audio_private_kib': added, 'reserved_kib': loud['reserved_kib'],
                              'beyond_payloads_kib': added - loud['reserved_kib'],
                              'audio_resident_kib': loud['process_resident_kib'] - still['process_resident_kib'],
                              'silent_private_kib': still['process_private_kib']}
            timing = load_cost(scenes[name]['loading'], quiet[name]['loading'])
            if timing:
                loading_cost[name] = timing
        checks['silent_baseline_complete'] = quiet_code == 0 and 'SCENE_COMPLETE' in quiet_log and len(cost) == len(labels)
        checks['silent_loading_recorded'] = len(loading_cost) == len(labels)
        total['process_cost'] = cost
        total['loading_cost'] = loading_cost
        total['silent_run_seconds'] = quiet_seconds
        if cost:
            beyond = sorted(c['beyond_payloads_kib'] for c in cost.values())
            total.update({'beyond_payloads_median_kib': beyond[len(beyond) // 2], 'beyond_payloads_max_kib': beyond[-1],
                          'audio_private_max_kib': max(c['audio_private_kib'] for c in cost.values())})
    if args.repeat > 1:
        # A long session: an opening played again holds no more audio, voices or
        # stream reservations than its first pass, beyond what one opening varies by.
        growth = {}
        for name in maps:
            passes = [scenes[f'pass{run}/{name}']['memory'] for run in range(1, args.repeat + 1)]
            if not all(passes):
                continue
            first = passes[0]
            growth[name] = {
                'reserved_kib': [m['reserved_kib'] for m in passes],
                'voices_in_use': [m['voices_in_use'] for m in passes],
                'stream_reserved_kib': [m['stream_reserved_kib'] for m in passes],
                'stable': all(m['reserved_kib'] <= first['reserved_kib'] + max(8192, first['reserved_kib'] // 10) and
                              m['voices_in_use'] <= first['voices_in_use'] + 8 and
                              (m['stream_reserved_kib'] or 0) <= (first['stream_reserved_kib'] or 0) + 8192 for m in passes[1:]),
            }
        checks['repeat_holds_steady'] = len(growth) == len(maps) and all(g['stable'] for g in growth.values())
        total['repeat_growth'] = growth
    residency = [s['memory'] for s in scenes.values() if s['memory']]
    if residency:
        largest = max(scenes, key=lambda name: scenes[name]['memory'].get('reserved_kib', -1))
        total.update({'largest_reserved_kib': scenes[largest]['memory']['reserved_kib'],
                      'payload_peak_kib': max(m['payload_peak_kib'] for m in residency),
                      'budget_mib': residency[-1]['budget_mib']})
        total['largest_map'] = largest
    exe = ROOT / '.install' / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    report = {
        'passed': all(checks.values()), 'checks': checks, 'totals': dict(total), 'scenes': scenes,
        'maps': maps, 'repeat': args.repeat, 'seconds_per_map': args.seconds, 'probe_interval_ms': args.interval, 'run_seconds': seconds,
        'decode_mode': args.decode_mode, 'stream_decode': args.stream_decode, 'decode_memory_mb': args.decode_memory_mb, 'audio_memory_mb': args.audio_memory_mb,
        'walk': {level: [entity for _, entity in steps] for level, steps in walk.items()} if walk else None,
        'step_msec': args.step_msec if walk else None,
        'progress': args.progress, 'fight': args.fight, 'suppressed_level_exits': blocked_exits,
        'game': 'q4xbase' if args.awakening else 'baseoq4', 'returncode': code, 'command': command,
        'microphone': False, 'host_input': False, 'output': 'OpenAL wave writer', 'transcripts_recorded': False,
        'executable_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
        'runtime_sha256': {str(p.relative_to(ROOT / '.install')): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in (ROOT / '.install/baseoq4').iterdir()
                           if p.is_file() and p.suffix.lower() in ('.dll', '.so', '.dylib', '.pk4')},
        'harness_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'checks': checks, 'totals': dict(total), 'loaded': len(loaded), 'openings': len(entries)}, indent=2))
    print(json.dumps({'result': str(output / 'result.json'), 'passed': report['passed']}))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
