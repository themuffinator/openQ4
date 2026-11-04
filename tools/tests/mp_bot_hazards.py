#!/usr/bin/env python3
"""Execute production hazard gathering and binary-mover swept bounds.

Geometry, entity lists and damage lookup are supplied by the native fixture.
The actual mover envelope, hazard policy and floor admission come from game_mp.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_bot_navmesh_search import function

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    nav = (ROOT / "src/mpgame/bots/NavMesh.cpp").read_text(encoding="utf-8")
    mover = (ROOT / "src/mpgame/Mover.cpp").read_text(encoding="utf-8")
    bodies = function(mover, "bool idMover_Binary::GetTranslationBounds(") + "\n" + "\n".join(
        function(nav, signature) for signature in (
            "void rvNavMesh::GatherHazards(", "int rvNavMesh::HazardAtFloor(")
    )
    source = (ROOT / "tools/tests/native/MpBotHazardsFixture.cpp").read_text(encoding="utf-8")
    source = source.replace("// PRODUCTION_FUNCTIONS", bodies)
    constants = "\n".join(re.findall(r"^static const float\s+NAV_(?:LETHAL_HURT_DAMAGE|LETHAL_HURT_RATE|TRAPDOOR_DEPTH)\s*=.*$", nav, re.M))
    source = source.replace("// PRODUCTION_CONSTANTS", constants)
    mutations = (
        ("initial position only", "swept.AddBounds( last );", "// omitted destination"),
        ("moving hazard remains priced", "deadly && ( swept ||", "deadly && ( false ||"),
        ("disabled crusher ignored", "if ( deadly && master &&", 'if ( deadly && ent->spawnArgs.GetBool( "on", "1" ) && master &&'),
        ("bound parent treated as world", "if ( IsBound() || !GetPhysics()", "if ( false || !GetPhysics()"),
    )
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for bot hazard qualification")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-bot-hazards-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        for index, (name, anchor, replacement) in enumerate((("production", "", ""), *mutations)):
            changed = source
            if anchor:
                assert source.count(anchor) == 1, name
                changed = source.replace(anchor, replacement)
            cpp = path / f"hazards{index}.cpp"
            exe = path / (f"hazards{index}.exe" if os.name == "nt" else f"hazards{index}")
            cpp.write_text(changed, encoding="utf-8")
            command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", str(cpp), "-o", str(exe)]
            if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, cwd=path, check=True)
            result = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True)
            assert (result.returncode == 0) == (index == 0), f"{name}: {result.stdout}{result.stderr}"
            print(f"{name}: {'passed' if index == 0 else 'reversal rejected'}")


if __name__ == "__main__":
    main()
