#!/usr/bin/env python3
"""Run the game module's SP/MP precaching and the production GUI lookup against counted owners."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from sound_settings import body

ROOT = Path(__file__).resolve().parents[2]


# (part, production text, mutated text). The last two undo the mode gates the
# one game module now carries for game_sp and game_mp.
MUTATIONS = {
    'mp_duplicate': ('precache', 'true, false, true', 'true'),
    'dictionary_duplicate': ('dictionary', 'true, false, true', 'true'),
    'shared_gameplay': ('lookup', '!forceNOTUnique &&', 'false &&'),
    'mp_spawn_skipped': ('precache', 'idPlayer::PrecacheMultiplayerSpawn();', ';'),
    'sp_precaches_mp': ('precache', 'if ( !gameLocal.isMultiplayer ) {', 'if ( false ) {'),
}


def source(mutation=None):
    # One game module (src/game) serves game_sp and game_mp; the fixture runs
    # its precache in both modes.
    multiplayer = (ROOT / 'src/game/MultiplayerGame.cpp').read_text()
    game = (ROOT / 'src/game/Game_local.cpp').read_text()
    ui = (ROOT / 'src/ui/UserInterface.cpp').read_text()
    dictionary = body(game, 'else if ( MATCH( "gui_" ) )')
    parts = {
        'precache': body(multiplayer, 'void idMultiplayerGame::Precache('),
        'dictionary': dictionary[dictionary.index('{'):],
        'lookup': body(ui, 'idUserInterface *idUserInterfaceManagerLocal::FindGui('),
    }
    if mutation:
        part, before, after = MUTATIONS[mutation]
        assert before in parts[part], (mutation, before)
        parts[part] = parts[part].replace(before, after)
    fixture = (ROOT / 'tools/tests/native/GuiPrecacheLifetimeTest.cpp').read_text()
    return fixture.replace('// @LOOKUP@', parts['lookup']).replace('// @PRECACHE@', parts['precache']) \
        .replace('// @DICTIONARY@', parts['dictionary'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutations', action='store_true')
    args = parser.parse_args()
    scratch = ROOT / '.tmp/gui-precache-lifetime'
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as directory:
        for mutation in [None] + (list(MUTATIONS) if args.mutations else []):
            cpp = Path(directory) / ('game-' + (mutation or 'production') + '.cpp')
            exe = cpp.with_suffix('.exe' if os.name == 'nt' else '')
            cpp.write_text(source(mutation))
            subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(cpp), '-o', str(exe)], cwd=directory, check=True)
            result = subprocess.run([str(exe)], cwd=directory, text=True, capture_output=True)
            if mutation:
                assert result.returncode != 0 and 'FAIL ' in result.stderr, (mutation, result)
                print(f'game {mutation}: rejected ({result.stderr.strip()})')
            else:
                assert result.returncode == 0, result.stdout + result.stderr
                print('game: ' + result.stdout.strip())


if __name__ == '__main__':
    main()
