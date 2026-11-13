#!/usr/bin/env python3
"""Subtitle playback and engine screenshots in stock/Awakening, without host input."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from audio_dialogue import tone
from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
PRIMARY = ('The corridor is clear. Move forward when the signal changes. '
           'Stay with your squad and watch the upper walkway. '
           'This original test dialogue checks that long subtitles remain readable while the sound continues.')
SECONDARY = 'The second speaker is checking the radio.'
# Original words for the caption-pack fixtures: a line of the harness's own, and one
# mapped onto an installed line the game ships no transcript for. Neither is a
# transcript of that line.
PACK = 'This caption comes from an authored pack.'
GAP = 'An authored pack can caption a line the game has no text for.'
# An installed single-sample line without a lip-sync transcript, in each campaign.
GAP_SHADERS = {False: 'vo_1_1_1_140_1', True: 'vo_7_6_2'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--language', default='english')
    parser.add_argument('--awakening', type=Path)
    parser.add_argument('--map', default='game/airdefense1')
    parser.add_argument('--size', default='1280x720')
    parser.add_argument('--classic', action='store_true')
    parser.add_argument('--runtime', type=Path, default=ROOT / '.install',
                        help='Staged runtime to run: .install, or a fixed separate stage such as .tmp/runtime/<name>')
    parser.add_argument('--timeout', type=int, default=600, help='Seconds; an Awakening map can take two minutes to load on a busy machine')
    args = parser.parse_args()
    root = args.output.resolve()
    if root.exists() or not root.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('choose a new output directory under the project .tmp')
    runtime = args.runtime.resolve(strict=True)
    verify_runtime_is_current(runtime)
    width, height = (int(n) for n in args.size.split('x'))
    game_dir = 'q4xbase' if args.awakening else 'baseoq4'
    game = root / 'save' / game_dir
    for part in ('sound', 'lipsync', 'strings'):
        (game / part).mkdir(parents=True, exist_ok=True)
    for name, frequency in [('a', 440), ('b', 660), ('missing', 880), ('pack', 550)]:
        tone(game / f'sound/vo/subtitle_test/{name}.wav', frequency)
    # A short line that ends on its own, for the held tail.
    tone(game / 'sound/vo/subtitle_test/short.wav', 330, seconds=0.4)
    fixtures = ('a', 'b', 'missing', 'pack', 'short')
    (game / 'sound/subtitle_test.sndshd').write_text('\n'.join(
        f'oq4_subtitle_{name}\n{{\n sound/vo/subtitle_test/{name}.wav\n}}' for name in fixtures))
    gap = GAP_SHADERS[bool(args.awakening)]
    # The pack fixtures are authored subtitle/ declarations: one by sample path for a
    # shader with no same-name lipSync, one by shader name for the installed line.
    (game / 'lipsync/subtitle_test.lipsync').write_text(
        'lipSync oq4_subtitle_a { text "#str_239990" }\n'
        'lipSync oq4_subtitle_b { text "#str_239991" }\n'
        'lipSync oq4_subtitle_short { text "#str_239991" }\n'
        'lipSync oq4_subtitle_missing { description "A description is not a transcript" text "#str_239999" }\n'
        'lipSync subtitle/sound/vo/subtitle_test/pack { text "#str_239992" }\n'
        f'lipSync subtitle/{gap} {{ text "#str_239993" }}\n')
    (game / 'strings/english_subtitle_test.lang').write_text(
        '{\n"#str_239990" "' + PRIMARY + '"\n"#str_239991" "' + SECONDARY + '"\n"#str_239992" "' + PACK +
        '"\n"#str_239993" "' + GAP + '"\n}\n', encoding='utf-8')
    # The primary intentionally has no Turkish translation: prove the English
    # fallback stays usable while a second line uses the selected text locale.
    (game / 'strings/turkish_subtitle_test.lang').write_text(
        '{\n"#str_239991" "İkinci konuşmacı telsizi kontrol ediyor."\n}\n', encoding='utf-8')
    steps = []
    def probe(name): steps.extend([f'echo SUBTITLE_{name}', 'listSubtitles'])
    def capture(name): steps.append(f'screenshot "screenshots/subtitles-{name}.tga"')
    probe('NATURAL')
    steps += ['notarget', 'set ui_subtitles 1', 'set s_musicVolume 0', 'testRadioChatter oq4_subtitle_a', 'waitMsec 350']
    probe('START'); capture('start')
    steps += ['set s_dialogueVolume 0', 'set s_volume 0', 'waitMsec 100']
    probe('MUTED'); capture('muted')
    steps += ['testRadioChatter pause', 'waitMsec 100']; probe('PAUSE_A')
    steps += ['waitMsec 350']; probe('PAUSE_B')
    steps += ['testRadioChatter resume', 'waitMsec 400']; probe('RESUME')
    steps += ['set timescale 0.5', 'waitMsec 200']; probe('SLOW_A')
    steps += ['waitMsec 600']; probe('SLOW_B')
    steps += ['set timescale 1', 'set s_volume 1', 'set s_dialogueVolume 1', 's_restart', 'waitMsec 250']; probe('RESTART')
    steps += ['testRadioChatter distract oq4_subtitle_b', 'waitMsec 150']; probe('OVERLAP'); capture('overlap')
    steps += ['set ui_subtitleScale 1.6', 'set ui_subtitleBackground 0.8', 'waitMsec 200']; capture('large')
    # The largest text, both scales at 2, on a lone caption: below a portrait cinematic's
    # artwork only one line fits, so it pages a line at a time there. The untranscribed
    # fixture takes over the second speaker's channel, which stop leaves; the overlap returns.
    steps += ['testRadioChatter distract oq4_subtitle_missing', 'testRadioChatter stop', 'set ui_subtitleScale 2', 'set ui_retainedTextScale 2', 'testRadioChatter oq4_subtitle_a',
              'waitMsec 200']; probe('LARGEST'); capture('largest')
    # A second speaker at that size: one line below the artwork and one above it.
    steps += ['testRadioChatter distract oq4_subtitle_b', 'waitMsec 150']; probe('LARGEST_OVERLAP'); capture('largest-overlap')
    steps += ['set ui_subtitleScale 1.6', 'reset ui_retainedTextScale']
    # The map opens in a cinematic, which refuses a save. After the captures that need the
    # cinematic, load the map again with cinematics skipped, then save and load outside one.
    cinematic = steps + ['set g_autoSkipCinematics 1', 'set g_autoExecAfterMapLoad "subtitle-restore.cfg"', f'map {args.map}']
    steps = ['set g_autoExecAfterMapLoad ""', 'notarget', 'testRadioChatter oq4_subtitle_a',
             'testRadioChatter distract oq4_subtitle_b', 'waitMsec 300']
    probe('BEFORE_SAVE'); steps.append('s_soundWorlds')
    steps += ['echo SUBTITLE_SAVE', 'saveGame audio_subtitles', 'waitMsec 300', 'loadGame audio_subtitles', 'waitMsec 500']
    probe('RESTORE'); steps.append('s_soundWorlds'); capture('restore')
    steps += ['testRadioChatter oq4_subtitle_missing', 'waitMsec 100']; probe('INTERRUPT')
    steps += ['testRadioChatter distract oq4_subtitle_missing', 'testRadioChatter stop', 'waitMsec 100']; probe('STOP')
    # The untranscribed line plays on, on the second speaker's channel: marked once asked
    # for, in a slot the map's own transcribed dialogue leaves, and gone once turned off.
    steps += ['set ui_subtitleUntranscribed 1', 'waitMsec 100']; probe('MARKED'); capture('marker')
    steps += ['set ui_subtitleUntranscribed 0', 'waitMsec 100']; probe('UNMARKED')
    authored = 'vo_mcc_allop' if args.awakening else 'vo_1_1_1_110_1'
    steps += [f'testRadioChatter {authored}', 'waitMsec 100']; probe('AUTHORED'); capture('authored')
    # Caption-pack declarations: a new line, then an installed one with no transcript.
    steps += ['testRadioChatter oq4_subtitle_pack', 'waitMsec 100']; probe('PACK')
    steps += [f'testRadioChatter {gap}', 'waitMsec 100']; probe('GAP')
    # A 0.4 s line that ends on its own stays up 1.5 s from its start, then clears.
    steps += ['testRadioChatter oq4_subtitle_short', 'waitMsec 800']; probe('TAIL_HELD')
    steps += ['waitMsec 1200']; probe('TAIL_GONE')
    steps += ['testRadioChatter oq4_subtitle_a', 'set ui_subtitles 0', 'waitMsec 100']; capture('off')
    steps += ['set ui_subtitles 1', 'set ui_subtitleSpeakers 0', 'waitMsec 100']; capture('no-speaker')
    steps += ['writeConfig "subtitle-settings.cfg"', 'testRadioChatter stop',
              'testGUI guis/mainmenu.gui', 'waitMsec 1500', 'openq4_guiAction main_b_settings', 'waitMsec 1500',
              'openq4_guiAction set_b_audio', 'waitMsec 1000',
              # The menu opened with testGUI from a running map is the stock one in both
              # UI modes, so its stock Audio page scrolls to the last subtitle row.
              'ui_hudTest guis/mainmenu.gui state gui_set_audio_scroll 7',
              'openq4_guiAction set_audio_scroll_thumb', 'waitMsec 300',
              'ui_hudTest guis/mainmenu.gui get gui_set_audio_scroll']
    capture('settings')
    steps += ['testGUI', 'echo SUBTITLE_COMPLETE', 'quit']
    (game / 'subtitle-test.cfg').write_text('\n'.join(cinematic) + '\n')
    (game / 'subtitle-restore.cfg').write_text('\n'.join(steps) + '\n')
    config = root / 'alsoft.conf'
    config.write_text('[general]\ndrivers=wave\nchannels=stereo\nhrtf=false\n[wave]\nfile=' + str(root / 'mix.wav') + '\n')
    cvars = {
        'fs_basepath': str(args.assets.resolve(strict=True)), 'fs_savepath': str(root / 'save'), 'fs_devpath': str(root / 'save'),
        'fs_game': game_dir, 'com_gameMode': 'SP', 'si_gameType': 'singleplayer', 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0', 'r_borderlessDefaultMigrated': '1',
        'win_allowMultipleInstances': '1', 'r_hiddenWindow': '1', 'r_windowWidth': str(width), 'r_windowHeight': str(height),
        'r_mode': '-1', 'r_customWidth': str(width), 'r_customHeight': str(height), 'r_renderApi': 'gl', 'r_multiSamples': '0',
        'r_swapInterval': '1', 'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
        's_noSound': '0', 's_muteUnfocused': '0', 'com_skipLogoVideos': '1', 'g_autoSkipCinematics': '0',
        's_decodeMemoryMB': '16', 's_numberOfSpeakers': '2', 's_openALHRTF': '0', 'ui_subtitles': '1',
        # Markers off until the phase that asks for them: the earlier phases count transcript cues.
        'ui_subtitleUntranscribed': '0',
        'com_skipLoadingContinue': '1', 'com_maxfps': '60', 'ui_autoJoin': '0', 'ui_retained': '0' if args.classic else '1',
        'sys_lang': args.language, 'g_autoExecAfterMapLoad': 'subtitle-test.cfg', 'g_autoExecAfterMapLoadDelayMs': '3000'}
    if args.awakening:
        cvars['fs_awakeningpath'] = str(args.awakening.resolve(strict=True))
    exe = runtime / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    command = [str(exe)]
    for key, value in cvars.items(): command += ['+set', key, value]
    command += ['+map', args.map]
    options = {'env': {**os.environ, 'ALSOFT_CONF': str(config), 'ALSOFT_DRIVERS': 'wave'}}
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 0
        options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    with (root / 'process.log').open('w') as output:
        run = subprocess.run(command, cwd=runtime, stdout=output, stderr=subprocess.STDOUT, timeout=args.timeout, **options)
    log = (game / 'logs/openq4.log').read_text(encoding='utf-8', errors='replace')
    phases = {}
    for name, section in re.findall(r'^SUBTITLE_(\w+)\s*\n(.*?)(?=^SUBTITLE_|\Z)', log, re.M | re.S):
        phases[name] = {shader: {'time': int(time), 'duration': int(duration), 'bytes': int(length), 'source': source}
                        for shader,time,duration,length,source in re.findall(
                            r'Subtitle shader=(\S+) emitter=\d+ speaker=\d+ time=(\d+) duration=(\d+) radio=\d textBytes=(\d+) source=(\w+)', section)}
    def cue(name, letter='a'): return phases.get(name, {}).get('oq4_subtitle_' + letter, {})
    # The saved game's load is the last level load the log reports.
    loads = re.findall(r'^\s*(\d+) msec to load \S+', log, re.M)
    load_msec = int(loads[-1]) if loads else 0

    def game_clock(name):
        # The game sound world's clock and the wall clock, read just after the phase's cues.
        section = re.search(rf'^SUBTITLE_{name}\s*\n(.*?)(?=^SUBTITLE_|\Z)', log, re.M | re.S)
        world = re.search(r'^SOUND_WORLD id=1 paused=\d time=(-?\d+) .*? now=(-?\d+)', section[1], re.M) if section else None
        return (int(world[1]), int(world[2])) if world else None
    clocks = [game_clock('BEFORE_SAVE'), game_clock('RESTORE')]
    # A restored line resumes where it was saved only if the game sound world's clock is
    # held from the moment the saved sound state is read, partway through the load, until
    # the map change completes: the wall clock then runs ahead of it by that stretch. With
    # the defect nothing held it and the gap stayed within a frame or two.
    held_msec = (clocks[1][1] - clocks[0][1]) - (clocks[1][0] - clocks[0][0]) if all(clocks) else None
    checks = {'complete': run.returncode == 0 and 'SUBTITLE_COMPLETE' in log,
              'transcript_and_english_fallback': cue('START').get('bytes') == len(PRIMARY.encode()),
              'muted_dialogue_caption': bool(cue('MUTED')),
              'pause_holds': bool(cue('PAUSE_A')) and cue('PAUSE_A').get('time') == cue('PAUSE_B').get('time'),
              'resume_advances': cue('RESUME').get('time', -1) > cue('PAUSE_B').get('time', 10**9),
              'slow_clock': 200 <= cue('SLOW_B').get('time', 0) - cue('SLOW_A').get('time', 0) <= 450,
              'restart_preserves': cue('RESTART').get('time', -1) > cue('SLOW_B').get('time', 10**9),
              'two_speakers': bool(cue('OVERLAP') and cue('OVERLAP', 'b')),
              'largest_text_caption': bool(cue('LARGEST')) and not cue('LARGEST', 'b'),
              'largest_text_two_speakers': bool(cue('LARGEST_OVERLAP') and cue('LARGEST_OVERLAP', 'b')),
              'restore_both': bool(cue('BEFORE_SAVE') and cue('BEFORE_SAVE', 'b') and cue('RESTORE') and cue('RESTORE', 'b')),
              # A real save and load: written outside a cinematic, then the level loads from it.
              'restore_resumes_in_place': held_msec is not None and load_msec > 0 and held_msec >= 100 and
                  0 < cue('RESTORE').get('time', -1) - cue('BEFORE_SAVE').get('time', 10**9) <= clocks[1][0] - clocks[0][0],
              # A failed load also reloads the level, so it is ruled out by its own warnings.
              'saved_and_loaded': "Can't save during a cinematic" not in log and 'could not restore saved state' not in log and
                  "Couldn't open savegame" not in log and
                  bool(re.search(r"^SUBTITLE_SAVE\s*\n(?:.*\n)*?Saved 'audio_subtitles'\s*\n(?:.*\n)*?Map: ", log, re.M)),
              'interruption_and_missing_text': not cue('INTERRUPT') and bool(cue('INTERRUPT', 'b')) and not cue('INTERRUPT', 'missing'),
              # The line without a transcript shows as a marker with no text while
              # ui_subtitleUntranscribed is set, unless two transcribed lines (the map's own)
              # hold both slots, and not at all once cleared.
              'untranscribed_marker': (cue('MARKED', 'missing').get('source') == 'none' and cue('MARKED', 'missing').get('bytes') == 0
                  or sum(c['source'] != 'none' for c in phases.get('MARKED', {}).values()) == 2) and
                  'MARKED' in phases and 'UNMARKED' in phases and not cue('UNMARKED', 'missing'),
              'stopped_clear': not any(n.startswith('oq4_subtitle_') for n in phases.get('STOP', {})),
              'installed_transcript': phases.get('AUTHORED', {}).get(authored, {}).get('source') == 'installed',
              'authored_pack_line': cue('PACK', 'pack').get('source') == 'authored' and cue('PACK', 'pack').get('bytes') == len(PACK.encode()),
              'authored_pack_fills_installed_gap': phases.get('GAP', {}).get(gap, {}).get('source') == 'authored' and
                  phases.get('GAP', {}).get(gap, {}).get('bytes') == len(GAP.encode()),
              # Held on its last words after the line ended, then cleared.
              'tail_held_then_cleared': bool(cue('TAIL_HELD', 'short')) and
                  cue('TAIL_HELD', 'short').get('time') == cue('TAIL_HELD', 'short').get('duration', 0) - 1 and
                  not cue('TAIL_GONE', 'short'),
              'audio_settings_reached': 'GUI_ACTION set_b_audio ' in log and
                  'HUD_STATE source=guis/mainmenu.gui key=gui_set_audio_scroll value=7' in log,
              'no_document_error': not re.search(r'(?:WARNING|ERROR).*subtitles\.rui', log)}
    saved = root / 'save/baseoq4/subtitle-settings.cfg'
    settings = saved.read_text() if saved.exists() else ''
    checks['archived_controls'] = all(f'seta {key} "{value}"' in settings for key,value in
                                     [('ui_subtitles','1'), ('ui_subtitleScale','1.6'), ('ui_subtitleBackground','0.8'), ('ui_subtitleSpeakers','0'),
                                      ('ui_subtitleUntranscribed','0')])
    captures = list((game / 'screenshots').glob('subtitles-*.tga'))
    checks['engine_captures'] = len(captures) == 12
    from PIL import Image
    for path in captures:
        with Image.open(path) as im: im.save(path.with_suffix('.png'))
    report = {'checks': checks, 'phases': phases, 'language': args.language, 'size': args.size, 'classic': args.classic,
              'executable_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'harness_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'runtime_sha256': {str(p.relative_to(runtime)): hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in (runtime / 'baseoq4').iterdir() if p.suffix.lower() in ('.dll','.so','.dylib','.pk4')},
              'command': command, 'captures': [str(p) for p in captures], 'host_input': False, 'microphone': False,
              'restore_load_msec': load_msec, 'restore_clock_held_msec': held_msec,
              # False when the map's own transcribed dialogue held both slots during MARKED.
              'marker_shown': cue('MARKED', 'missing').get('source') == 'none', 'passed': all(checks.values())}
    (root / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'result': str(root / 'result.json'), 'checks': checks}, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
