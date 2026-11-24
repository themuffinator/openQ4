#!/usr/bin/env python3
"""Execute production scheduler methods and SP/MP/network timestamp boundaries."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def definition(text, name):
    match = re.search(r'\b(?:int|float)\s+' + re.escape(name) + r'\([^;{}]*\)\s*const\s*\{', text)
    if not match:
        raise RuntimeError('Missing production clock definition: ' + name)
    start = text.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]


def main():
    compiler = next((shutil.which(c) for c in ('clang++', 'g++', 'c++') if shutil.which(c)), None)
    if not compiler:
        raise RuntimeError('Executable clock coverage requires a C++ compiler')
    header = (ROOT / 'src/framework/Common.h').read_text(encoding='utf-8')
    common = (ROOT / 'src/framework/Common.cpp').read_text(encoding='utf-8')
    usercmd = (ROOT / 'src/framework/UsercmdGen.h').read_text(encoding='utf-8')
    constants = '\n'.join(re.findall(r'const int USERCMD_(?:HZ|MSEC)\s*=.*?;', usercmd))
    assert len(constants.splitlines()) == 2
    methods = '\n'.join(definition(header, name) for name in (
        'GetUserCmdTime', 'GetUserCmdDeltaMsec', 'GetUserCmdMsecNumerator', 'GetUserCmdMsecDenominator'))
    methods += '\n' + '\n'.join(definition(common, 'idCommonLocal::' + name).replace('idCommonLocal::', '')
                                 for name in ('GetUserCmdHz', 'GetUserCmdMSec'))
    # One game module serves game_sp and game_mp, so both modes run the same
    # frame step in idGameLocal::RunFrame. Execute it, with the multiplayer
    # frame-counter resync gated on isMultiplayer, on a campaign map (the
    # session passes serverGameFrame 0) and on a network map (the async server
    # passes its own frame, then stamps gameTime for the next one).
    game = (ROOT / 'src/game/Game_local.cpp').read_text(encoding='utf-8')
    server = (ROOT / 'src/framework/async/AsyncServer.cpp').read_text(encoding='utf-8')
    run_frame = game[game.index('gameReturn_t idGameLocal::RunFrame('):]
    run_frame = run_frame[run_frame.index('} else do {'):]
    frame_step = re.match(r'\} else do \{\s*(previousTime = time;.*?msec = time - previousTime;)', run_frame, re.S).group(1)
    assert re.search(r'const int frameMsec = GetMSec\(\);\s*time \+= frameMsec;', frame_step), 'the frame step moved'
    assert 'if ( isMultiplayer && serverGameFrame != framenum ) {' in frame_step, 'the frame resync lost its mode gate'
    network_step = re.search(r'gameTime = gameFrame \* common->GetUserCmdMSec\(\);', server).group()
    source = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
CHECK_CONSTANTS
struct Common { CHECK_METHODS
  int warnings=0; template<class... A> void Warning(const char*, A...) { ++warnings; } };
static Common owner; static Common *common=&owner;
struct gameReturn_t { int sessionCommand; };
struct Game { int time=0, previousTime=0, msec=0, framenum=0, step=16; bool isMultiplayer=false;
  int GetMSec() const { return step; }
  gameReturn_t Frame(int serverGameFrame) { gameReturn_t ret{1}; FRAME_STEP return ret; } };
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while(0)
int main() {
  Game sp,mp; mp.isMultiplayer=true;
  CHECK(owner.GetUserCmdTime(-1)==0 && owner.GetUserCmdTime(0)==0);
  CHECK(owner.GetUserCmdMSec()==16 && owner.GetUserCmdHz()==60);
  for(int gameFrame=1;gameFrame<=1000000;++gameFrame) {
    CHECK(sp.Frame(0).sessionCommand==1 && mp.Frame(gameFrame-1).sessionCommand==1);
    int gameTime; NETWORK_STEP
    CHECK(gameTime==sp.time && gameTime==mp.time && gameTime==gameFrame*16);
    CHECK(sp.msec==16 && mp.msec==16 && sp.framenum==gameFrame && mp.framenum==gameFrame);
    CHECK(owner.GetUserCmdTime(gameFrame)==static_cast<long long>(gameFrame)*1000/60);
    const int delta=owner.GetUserCmdDeltaMsec(gameFrame); CHECK(delta==16 || delta==17);
  }
  CHECK(owner.warnings==0);
  // A campaign tic follows its (slow-motion) step and never resyncs to the
  // serverGameFrame the session leaves at 0.
  sp.step=8; const int before=sp.time; sp.Frame(0); CHECK(sp.time==before+8 && sp.msec==8 && owner.warnings==0);
  // Network play keeps game_mp's resync: a lagging game catches up to the
  // server's frame, and a game ahead of it waits without advancing.
  Game behind; behind.isMultiplayer=true;
  CHECK(behind.Frame(3).sessionCommand==1 && behind.framenum==4 && behind.time==64 && behind.msec==64 && owner.warnings==1);
  Game ahead; ahead.isMultiplayer=true; ahead.framenum=5; ahead.time=80;
  CHECK(ahead.Frame(2).sessionCommand==0 && ahead.framenum==5 && ahead.time==80 && owner.warnings==2);
  std::puts("production scheduler/SP/MP/network clocks: one million ticks agree");
}
'''
    for marker, value in [('CHECK_CONSTANTS', constants), ('CHECK_METHODS', methods),
                          ('FRAME_STEP', frame_step), ('NETWORK_STEP', network_step)]:
        source = source.replace(marker, value)
    temporary = ROOT / '.tmp/clock-ownership'
    temporary.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=temporary) as directory:
        path = Path(directory)
        (path / 'clock.cpp').write_text(source, encoding='utf-8')
        executable = path / 'clock.exe'
        subprocess.run([compiler, '-std=c++17', '-O2', str(path / 'clock.cpp'), '-o', str(executable)],
                       cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
