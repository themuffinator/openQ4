#!/usr/bin/env python3
"""Execute the production multi-view demo start-state checks.

The game writes each start-state client record with a POWERUP_MAX-bit powerup
mask. POWERUP_MAX grew from 16 to 18 when The Awakening's powerups were added,
and the engine's playback validator, which assumed 16, then rejected every
recording with two or more players ("invalid MVD multiplayer start-state client
4"). tools/tests/native/MvdStartStateFixture.cpp builds network-state records
at both widths for one to six players and splices in, verbatim:

  * MVD_StartStateMatchesPlayers and idMultiViewDemo::ValidateNetworkStateRecord
    from src/framework/async/MultiViewDemo.cpp, and
  * MP_StartStatePowerupBits from src/mpgame/MultiplayerGame.cpp.

Both layouts must validate and be read at their own width; malformed start
states must be refused at every width. Single-reversal controls re-run the
fixture against mutated production source and require each to fail.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

MUTATIONS = (
    (
        "engine",
        "the validator assumes a fixed 16-bit powerup mask again",
        "!parsed && powerupBits <= MVD_START_STATE_MAX_POWERUP_BITS; powerupBits++ ) {",
        "!parsed && powerupBits <= MVD_START_STATE_MIN_POWERUP_BITS; powerupBits++ ) {",
    ),
    (
        "engine",
        "start-state clients are no longer held to the player list",
        "\t\t\t !seenClient[clientNum] || clientNum <= previousStartClient ||\n",
        "\t\t\t clientNum <= previousStartClient ||\n",
    ),
    (
        "game",
        "the game reads legacy 16-bit recordings at POWERUP_MAX",
        "static const int widths[ 2 ] = { POWERUP_MAX, 16 };",
        "static const int widths[ 2 ] = { POWERUP_MAX, POWERUP_MAX };",
    ),
)


def block(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def build_fixture(mvd: str, game: str) -> str:
    constants = "\n".join(re.findall(
        r"^static const int (?:MVD_MAX_GAME_MESSAGE_BYTES|MVD_MAX_INSTANCES|MVD_START_STATE_\w+)\s*=[^;]*;",
        mvd, re.M))
    helper = block(mvd, "static bool MVD_StartStateMatchesPlayers(")
    bodies = block(mvd, "bool idMultiViewDemo::ValidateNetworkStateRecord(") + "\n" + \
        block(mvd, "bool idMultiViewDemo::ValidateNetworkStatePayload(") + "\n" + \
        block(game, "static int MP_StartStatePowerupBits(")
    harness = (ROOT / "tools/tests/native/MvdStartStateFixture.cpp").read_text(encoding="utf-8")
    return (harness.replace("// PRODUCTION_CONSTANTS", constants)
            .replace("// PRODUCTION_ENGINE_HELPER", helper)
            .replace("// PRODUCTION_FUNCTIONS", bodies))


def compile_and_run(source: str, directory: Path, name: str, compiler: str) -> subprocess.CompletedProcess:
    cpp = directory / f"{name}.cpp"
    exe = directory / (f"{name}.exe" if os.name == "nt" else name)
    cpp.write_text(source, encoding="utf-8")
    command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-const-variable",
               "-Wno-unused-function", "-Wno-sign-compare", str(cpp), "-o", str(exe)]
    if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=directory, check=True)
    return subprocess.run([str(exe)], cwd=directory, capture_output=True, text=True)


def main() -> None:
    mvd = (ROOT / "src/framework/async/MultiViewDemo.cpp").read_text(encoding="utf-8")
    game = (ROOT / "src/mpgame/MultiplayerGame.cpp").read_text(encoding="utf-8")
    reader = block(game, "void idMultiplayerGame::ClientReadStartState(")
    for token in ("const int powerupBits = MP_StartStatePowerupBits( msg );",
                  "msg.GetRemainingReadBits() < powerupBits + ASYNC_PLAYER_INSTANCE_BITS + 1",
                  "powerup = msg.ReadBits( powerupBits );"):
        if token not in reader:
            raise SystemExit(f"mvd_start_state_contract: ClientReadStartState is missing {token!r}")
    writer = block(game, "void idMultiplayerGame::WriteStartState(")
    if "msg.WriteBits( static_cast< idPlayer * >( ent )->inventory.powerups, POWERUP_MAX );" not in writer:
        raise SystemExit("mvd_start_state_contract: the start-state writer's powerup width changed")

    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the MVD start-state fixture")
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mvd-start-state-", dir=output) as directory:
        path = Path(directory)
        result = compile_and_run(build_fixture(mvd, game), path, "startstate", compiler)
        if result.returncode != 0:
            raise SystemExit(f"mvd_start_state_contract: FAILED\n{result.stdout}{result.stderr}")
        for index, (where, label, original, mutated) in enumerate(MUTATIONS):
            source = mvd if where == "engine" else game
            if source.count(original) != 1:
                raise SystemExit(f"mvd_start_state_contract: mutation anchor not unique: {label}")
            mutated_mvd = mvd.replace(original, mutated, 1) if where == "engine" else mvd
            mutated_game = game.replace(original, mutated, 1) if where == "game" else game
            control = compile_and_run(build_fixture(mutated_mvd, mutated_game), path, f"control{index}", compiler)
            if control.returncode == 0:
                raise SystemExit(f"mvd_start_state_contract: control did not fail: {label}")
    print(f"mvd_start_state_contract: ok ({len(MUTATIONS)} single-reversal controls fail)")


if __name__ == "__main__":
    main()
