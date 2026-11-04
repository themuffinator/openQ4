#!/usr/bin/env python3
"""Execute the production recovery sweep's live-player collision contract."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from mp_bot_navmesh_search import function

ROOT = Path(__file__).resolve().parents[2]


def main():
    nav = (ROOT / "src/mpgame/bots/NavMesh.cpp").read_text(encoding="utf-8")
    body = function(nav, "static bool NavRecoveryTranslation(")
    source = (ROOT / "tools/tests/native/MpBotRecoveryTrafficFixture.cpp").read_text(encoding="utf-8")
    source = source.replace("// PRODUCTION_FUNCTION", body)
    mutations = (
        ("omit players", "i < gameLocal.numClients", "i < 0"),
        ("include own hull", "entity == self ||", "false ||"),
        ("cross-instance bodies", "entity->GetInstance() != self->GetInstance()", "false"),
        ("spectator hull", "other->spectating ||", "false ||"),
        ("dead player hull", "other->health <= 0 ||", "false ||"),
        ("non-solid hull", "!( clip->GetContents() & MASK_PLAYERSOLID ) ||", "false ||"),
        ("wrong hit ordering", "hit.fraction < trace.fraction", "hit.fraction > trace.fraction"),
    )
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for recovery traffic qualification")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-bot-traffic-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        cpp, exe = path / "traffic.cpp", path / ("traffic.exe" if os.name == "nt" else "traffic")
        command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        for index, (name, anchor, replacement) in enumerate((("production", "", ""), *mutations)):
            if anchor:
                assert source.count(anchor) == 1, name
            cpp.write_text(source.replace(anchor, replacement) if anchor else source, encoding="utf-8")
            subprocess.run(command, cwd=path, check=True)
            result = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True)
            assert (result.returncode == 0) == (index == 0), f"{name}: {result.stdout}{result.stderr}"
            print(f"{name}: {'passed' if index == 0 else 'reversal rejected'}")


if __name__ == "__main__":
    main()
