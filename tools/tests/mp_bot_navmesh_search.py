#!/usr/bin/env python3
"""Prove the bot navmesh search core on generated graphs.

The production methods - strongly connected components and their reachability
closure, ALT landmark selection, the landmark heuristic, and both forms of the
single-source cost field, and the exact pair pricing behind goal choice - are
spliced verbatim from src/game/bots/NavMesh.cpp into
tools/tests/native/MpBotNavSearchFixture.cpp and compiled.  The fixture
generates two-floor grids with walls, one-way drops, cheap long-range jump pads
and sealed pits, and checks the methods against brute force: the closure must
equal breadth-first reachability, pair pricing must equal the exact route cost,
the heuristic must never overestimate a real route and must be consistent on
every edge, and ranked goal pricing must return exactly the exhaustive field's
best few while expanding fewer nodes.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def function(source: str, signature: str) -> str:
    """Return one function, from its signature to the matching closing brace."""
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main() -> None:
    nav = (ROOT / "src/game/bots/NavMesh.cpp").read_text(encoding="utf-8")
    constants = "\n".join(re.findall(r"^static const (?:int|float)\s+NAV_\w+\s*=\s*[0-9][^\n]+", nav, re.M))
    bodies = "\n".join(function(nav, signature) for signature in (
        "void rvNavMesh::BeginSearch(",
        "void rvNavMesh::HeapPush(",
        "int rvNavMesh::HeapPop(",
        "void rvNavMesh::Dijkstra(",
        "void rvNavMesh::BuildComponents(",
        "bool rvNavMesh::NodeCanReach(",
        "void rvNavMesh::BuildLandmarks(",
        "float rvNavMesh::Heuristic(",
        "static void NavInsertBest(",
        "int rvNavMesh::BuildCostField( const idVec3 &origin, float maxCost, const idList<int> *targets )",
        "int rvNavMesh::BuildCostField( const idVec3 &origin, float maxCost, const navFieldTarget_t *targets,",
        "float rvNavMesh::CostFieldDistance(",
        "float rvNavMesh::PathCost(",
        "void rvNavMesh::RankedSettle(",
    ))
    harness = (ROOT / "tools/tests/native/MpBotNavSearchFixture.cpp").read_text(encoding="utf-8")
    harness = harness.replace("// PRODUCTION_CONSTANTS", constants).replace("// PRODUCTION_FUNCTIONS", bodies)
    # A second build forces goal pricing past its work budget almost at once,
    # so the field fallback is proven exact too.
    budget = re.search(r"NAV_PRICING_MIN_BUDGET\s*=\s*\d+", harness)
    if not budget:
        raise AssertionError("NAV_PRICING_MIN_BUDGET is no longer a spliced constant")
    fallback = "#define PRICING_FALLBACK_TEST 1\n" + harness.replace(budget.group(0), "NAV_PRICING_MIN_BUDGET = 16")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the navmesh search proofs")
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-bot-navsearch-", dir=output) as directory:
        path = Path(directory)
        for name, source in (("navsearch", harness), ("navsearch_fallback", fallback)):
            cpp, exe = path / f"{name}.cpp", path / (f"{name}.exe" if os.name == "nt" else name)
            cpp.write_text(source, encoding="utf-8")
            command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-const-variable",
                       "-Wno-unused-parameter", "-Wno-unused-variable", str(cpp), "-o", str(exe)]
            if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, cwd=path, check=True)
            subprocess.run([str(exe)], cwd=path, check=True)
    print("mp_bot_navmesh_search: ok")


if __name__ == "__main__":
    main()
