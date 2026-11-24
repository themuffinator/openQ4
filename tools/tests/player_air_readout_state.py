#!/usr/bin/env python3
"""Execute authoritative air state, rate policy and production event codec."""
from __future__ import annotations
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from mp_lag_compensation_state import block

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    player = (ROOT / "src/game/Player.cpp").read_text()
    header = (ROOT / "src/game/PlayerAirReadout.h").read_text()
    network = (ROOT / "src/game/Game_network.cpp").read_text()
    codec = block(player, "void idPlayer::WriteAirReadoutMessage(")
    getter = block(player, "int idPlayer::GetAirReadoutPercent(")
    event = player.split("bool idPlayer::ClientReceiveEvent(", 1)[1]
    receiver = block(event, "case EVENT_AIR_READOUT:").replace("case EVENT_AIR_READOUT:", "")
    # One Player.cpp serves both modes. Each (re)spawned player starts with a fresh readout and
    # publish clock. game_mp also reset both in its Restore; the merged Restore keeps the campaign's
    # save text, and a restored (campaign) player never reads that state: the readout is a network
    # client's and the publication a network server's, and isMultiplayer is isServer || isClient.
    init = block(player, "void idPlayer::Init( void )")
    assert player.count("airReadout.Reset( gameLocal.time );") == 1 and player.count("airPublishClock.Reset();") == 1
    assert "airReadout.Reset( gameLocal.time );\n\tairPublishClock.Reset();" in init.replace("\r\n", "\n")
    assert "gameLocal.isClient && airReadout.percent >= 0 ? airReadout.percent :" in getter
    assert 'SetStateInt( "player_air", GetAirReadoutPercent() )' in block(player, "void idPlayer::UpdateHudStats(")
    assert "if ( gameLocal.isServer && gameLocal.isNewFrame &&\n\t\tairPublishClock.Admit" in block(player, "void idPlayer::UpdateAir(").replace("\r\n", "\n")
    assert "WriteAirReadoutMessage( outMsg, true )" in block(network, "void idGameLocal::ServerWriteInitialReliableMessages(")
    bootstrap = block(network, "void idGameLocal::ServerWriteServerDemoSnapshot(")
    assert "sequence == 1" in bootstrap and "GAME_UNRELIABLE_RECORD_CLIENTNUM" in bootstrap and "route.WriteChar( -1 )" in bootstrap
    fixture = r'''
#include <cassert>
#include <vector>
#include <cstdio>
#include <limits>
@HEADER@
struct idBitMsg {
    std::vector<int> bits; mutable unsigned pos=0; mutable bool overflow=false;
    void BeginWriting() {bits.clear();pos=0;overflow=false;}
    void WriteBits(int value,int count) {for(int i=0;i<count;++i) bits.push_back((unsigned(value)>>i)&1);}
    int ReadBits(int count) const {unsigned v=0; for(int i=0;i<count;++i) {
        if(pos>=bits.size()) {overflow=true;return -1;} v|=unsigned(bits[pos++])<<i;} return int(v);}
    void WriteByte(int v){WriteBits(v,8);} void WriteLong(int v){WriteBits(v,32);}
    int ReadByte() const{return ReadBits(8);} int ReadLong() const{return ReadBits(32);}
    bool IsReadOverflowed()const{return overflow;}
};
struct { bool isClient=false; int time=12345; int GetSpawnId(const void*){return 0x34567;} } gameLocal;
struct {int GetInteger()const{return 1000;}} pm_airTics;
struct idMath {static int BitsForInteger(int){return 8;}};
constexpr int MAX_EVENT_PARAM_SIZE=128, GAME_RELIABLE_MESSAGE_EVENT=9, GAME_UNRELIABLE_MESSAGE_EVENT=0;
struct idPlayer {
    enum {EVENT_AIR_READOUT=19}; int airTics=420; mpAirReadoutState airReadout;
    int GetAirReadoutPercent()const;
    void WriteAirReadoutMessage(idBitMsg&,bool)const;
    bool Receive(const idBitMsg&msg) @RECEIVER@
};
@GETTER@
@CODEC@
int main() {
    assert(MPAirPercent(-1,100)==0 && MPAirPercent(50,100)==50 && MPAirPercent(101,100)==100);
    assert(MPAirPercent(50,0)==100 && MPAirPercent(2147483646,2147483647)==99);
    mpAirReadoutState state;
    assert(state.Apply(200,42) && state.percent==42);
    for(auto bad : std::vector<std::pair<int,int>>{{199,80},{-1,50},{201,-1},{201,101},{200,43}}) {
        assert(!state.Apply(bad.first,bad.second) && state.percent==42 && state.serverTime==200);
    }
    assert(state.Apply(200,42) && state.Apply(201,0));
    state.Reset(); assert(state.percent==-1 && state.Apply(1,100));
    state.Reset(1000); assert(!state.Apply(999,0) && state.percent==-1 && state.Apply(1000,100));
    state.Reset(0); assert(state.Apply(0,50)); // a new map/seek has a new clock
    mpAirPublishClock clock;
    assert(clock.Admit(0,100) && !clock.Admit(99,99) && clock.Admit(100,99));
    assert(!clock.Admit(110,98) && clock.Admit(200,98));
    assert(!clock.Admit(1199,98) && clock.Admit(1200,98));
    assert(!clock.Admit(1300,101) && !clock.Admit(-1,50));
    clock.Reset(); assert(clock.Admit(10,100));
    clock.Reset(); assert(clock.Admit(2147483647,10) && !clock.Admit(2147483647,9));
    for(bool reliable : {false,true}) {
        idPlayer source, target; idBitMsg wire;
        source.WriteAirReadoutMessage(wire,reliable);
        assert(wire.ReadByte()==(reliable?GAME_RELIABLE_MESSAGE_EVENT:GAME_UNRELIABLE_MESSAGE_EVENT));
        assert(wire.ReadBits(32)==0x34567 && wire.ReadByte()==idPlayer::EVENT_AIR_READOUT);
        if(reliable) {assert(wire.ReadLong()==12345 && wire.ReadBits(8)==5);}
        assert(target.Receive(wire) && target.airReadout.percent==42 && target.airReadout.serverTime==12345);
        assert(wire.pos==wire.bits.size());
    }
    idPlayer target; idBitMsg truncated; truncated.WriteLong(1);
    assert(!target.Receive(truncated) && target.airReadout.percent==-1);
    idBitMsg invalid;invalid.WriteLong(2);invalid.WriteByte(255);
    assert(!target.Receive(invalid) && target.airReadout.percent==-1);
    assert(target.GetAirReadoutPercent()==42);
    assert(target.airReadout.Apply(100,21));gameLocal.isClient=true;
    assert(target.GetAirReadoutPercent()==21);gameLocal.isClient=false;
    assert(target.GetAirReadoutPercent()==42);
    std::puts("Air readout: authority/order/reset, bounded publication and reliable/unreliable codec: PASS");
}
'''.replace("@HEADER@", header).replace("@CODEC@", codec).replace("@GETTER@", getter).replace("@RECEIVER@", receiver)
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a C++17 compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="player-air-readout-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        cpp, exe = path / "fixture.cpp", path / ("fixture.exe" if os.name == "nt" else "fixture")
        command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)]
        cpp.write_text(fixture)
        subprocess.run(command, cwd=path, check=True)
        subprocess.run([str(exe)], cwd=path, check=True)
        for before, after in (("stamp < serverTime", "false"), ("now < nextSend", "false")):
            assert before in fixture
            cpp.write_text(fixture.replace(before, after))
            subprocess.run(command, cwd=path, check=True)
            failed = subprocess.run([str(exe)], cwd=path, capture_output=True)
            assert failed.returncode != 0, f"disabled {before} control escaped"
    print("player_air_readout_state: PASS (ordering and publication-limit reversals fail)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
