#!/usr/bin/env python3
"""Regression checks for game-library layers (tools/build/game_layer.py and
stage_gamelibs.py --layer).

One game module serves both modes, so every layer source tree stages into
src/game/<id>/ and the retired multiplayer-only "mpgame" tree is refused."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tests"))
from gamelibs_staging import MANIFEST_NAME, STAGE_SCRIPT, make_minimal_workspace, staged_mtimes, write_file

LAYER_SCRIPT = ROOT / "tools" / "build" / "game_layer.py"
LAYER_ID = "testlayer"


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def layer_manifest(**overrides: object) -> dict:
    manifest = {
        "format": 1,
        "id": LAYER_ID,
        "gameDir": "q4xtest",
        "sources": {"shared": "src/shared", "game": "src/game"},
        "mod": {
            "name": "Test Layer",
            "version": "0.1.0",
            "releaseDate": "2026-09-26",
            "website": "https://example.invalid/",
            "author": "openQ4 tests",
        },
    }
    manifest.update(overrides)
    return manifest


def make_layer(root: Path, manifest: dict | None = None) -> Path:
    write_file(root / "src" / "shared" / "Shared.cpp", "// shared\n")
    write_file(root / "src" / "shared" / "weapon" / "Nested.cpp", "// nested\n")
    write_file(root / "src" / "game" / "Campaign.cpp", "// campaign\n")
    # multiplayer-only code lives in "game" behind a runtime mode check, and
    # shares weapon/ with the shared tree
    write_file(root / "src" / "game" / "weapon" / "Match.cpp", "// if ( gameLocal.isMultiplayer )\n")
    write_file(root / "layer.json", json.dumps(manifest if manifest is not None else layer_manifest()))
    return root


LAYER_FILES = (
    f"src/game/{LAYER_ID}/Shared.cpp",
    f"src/game/{LAYER_ID}/weapon/Nested.cpp",
    f"src/game/{LAYER_ID}/Campaign.cpp",
    f"src/game/{LAYER_ID}/weapon/Match.cpp",
)


def stage(project_root: Path, gamelibs_root: Path, stage_root: Path, layer_root: Path) -> subprocess.CompletedProcess[str]:
    return run(str(STAGE_SCRIPT), str(project_root), str(gamelibs_root), str(stage_root), "--layer", str(layer_root))


def check_fresh(stage_root: Path) -> int:
    return run(str(STAGE_SCRIPT), "--check-fresh", str(stage_root)).returncode


def validate_manifest_reader(work: Path) -> None:
    layer = make_layer(work / "layer")
    result = run(str(LAYER_SCRIPT), "field", str(layer), "gameDir")
    if result.returncode != 0 or result.stdout.strip() != "q4xtest":
        raise AssertionError(f"game_layer.py field gameDir: {result.returncode} {result.stdout!r} {result.stderr!r}")

    out = work / "out" / "mod.json"
    result = run(str(LAYER_SCRIPT), "mod-json", str(layer), "--openq4-version", "1.2.3", "--out", str(out))
    if result.returncode != 0:
        raise AssertionError(f"game_layer.py mod-json failed: {result.stderr}")
    mod = json.loads(out.read_text(encoding="utf-8"))
    if mod.get("name") != "Test Layer" or mod.get("requiredopenQ4Version") != "1.2.3" or mod.get("layer") != LAYER_ID:
        raise AssertionError(f"unexpected mod.json: {mod}")

    rejected = {
        "format": layer_manifest(format=2),
        "id": layer_manifest(id="Bad-Id"),
        "reserved gameDir": layer_manifest(gameDir="baseoq4"),
        "escaping source": layer_manifest(sources={"shared": "../outside"}),
        "unknown tree": layer_manifest(sources={"engine": "src/shared"}),
        "missing mod field": layer_manifest(mod={"name": "x", "version": "1.0.0"}),
        "bad version": layer_manifest(mod=dict(layer_manifest()["mod"], version="1.0")),
    }
    for case, manifest in rejected.items():
        bad = make_layer(work / "bad" / case.replace(" ", "_"), manifest)
        (work / "bad" / "outside").mkdir(parents=True, exist_ok=True)
        result = run(str(LAYER_SCRIPT), "field", str(bad), "id")
        if result.returncode == 0:
            raise AssertionError(f"game_layer.py accepted a layer with a bad {case}")


def validate_layer_stage(work: Path) -> None:
    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)
    layer = make_layer(work / "layer")

    result = stage(project_root, gamelibs_root, stage_root, layer)
    if result.returncode != 0:
        raise AssertionError(f"layer stage failed: {result.stderr}")

    # both layer trees land in the one game module's tree, beside the base sources
    present = (*LAYER_FILES, "src/game/Game_local.cpp")
    for rel in present:
        if not (stage_root / rel).is_file():
            raise AssertionError(f"layer stage is missing {rel}")
    if (stage_root / "src" / "mpgame").exists():
        raise AssertionError("layer stage created a retired src/mpgame tree")

    manifest = json.loads((stage_root / MANIFEST_NAME).read_text(encoding="utf-8"))
    recorded = {entry["path"] for entry in manifest["files"]}
    for rel in present:
        if rel not in recorded:
            raise AssertionError(f"stage manifest does not record {rel}")
    layer_recorded = sorted(rel for rel in recorded if rel.startswith(f"src/game/{LAYER_ID}/"))
    if layer_recorded != sorted(LAYER_FILES):
        raise AssertionError(f"stage manifest records other layer files than the layer provides: {layer_recorded}")
    staged_roots = sorted(root["staged"] for root in manifest.get("sourceRoots", ()))
    if staged_roots.count(f"src/game/{LAYER_ID}") != 2 or "src/game" not in staged_roots or any(
        root.startswith("src/mpgame") for root in staged_roots
    ):
        raise AssertionError(f"stage manifest source roots do not describe one game tree: {staged_roots}")
    layer_info = manifest.get("layer")
    if not isinstance(layer_info, dict) or layer_info.get("id") != LAYER_ID or layer_info.get("gameDir") != "q4xtest":
        raise AssertionError(f"stage manifest has no layer identity: {layer_info}")

    # freshness follows both the base tree and the layer
    if check_fresh(stage_root) != 0:
        raise AssertionError("a new layer stage reads as stale")
    write_file(layer / "src" / "shared" / "Shared.cpp", "// shared, edited\n")
    if check_fresh(stage_root) != 3:
        raise AssertionError("an edited layer source did not make the stage stale")
    if stage(project_root, gamelibs_root, stage_root, layer).returncode != 0 or check_fresh(stage_root) != 0:
        raise AssertionError("re-staging an edited layer did not freshen the stage")
    write_file(layer / "src" / "game" / "Added.cpp", "// added\n")
    if check_fresh(stage_root) != 3:
        raise AssertionError("a new layer source did not make the stage stale")
    write_file(gamelibs_root / "src" / "game" / "Game_local.cpp", "// game, edited\n")
    if stage(project_root, gamelibs_root, stage_root, layer).returncode != 0 or check_fresh(stage_root) != 0:
        raise AssertionError("re-staging after a base edit did not freshen the stage")
    write_file(gamelibs_root / "src" / "game" / "gamesys" / "SysCvar.cpp", "// cvar, edited\n")
    if check_fresh(stage_root) != 3:
        raise AssertionError("an edited base source did not make a layer stage stale")

    # a stage manifest without source roots (an older stager's) is never fresh
    manifest = json.loads((stage_root / MANIFEST_NAME).read_text(encoding="utf-8"))
    manifest.pop("sourceRoots", None)
    (stage_root / MANIFEST_NAME).write_text(json.dumps(manifest), encoding="utf-8")
    if check_fresh(stage_root) != 3:
        raise AssertionError("a stage without source roots read as fresh")


def validate_layer_restage(work: Path) -> None:
    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)
    layer = make_layer(work / "layer")
    if stage(project_root, gamelibs_root, stage_root, layer).returncode != 0:
        raise AssertionError("initial layer stage failed")
    old = 1_700_000_000_000_000_000
    for path in stage_root.rglob("*"):
        if path.is_file():
            os.utime(path, ns=(old, old))

    # the layer sits inside the base tree: restaging both must touch only the
    # edited layer file and drop the deleted one, while the other tree's file
    # in the same directory stays
    write_file(layer / "src" / "shared" / "Shared.cpp", "// shared, edited\n")
    (layer / "src" / "game" / "weapon" / "Match.cpp").unlink()
    if stage(project_root, gamelibs_root, stage_root, layer).returncode != 0:
        raise AssertionError("restaging an edited layer failed")
    mtimes = staged_mtimes(stage_root)
    fresh = sorted(rel for rel, mtime in mtimes.items() if mtime != old)
    if fresh != [f"src/game/{LAYER_ID}/Shared.cpp"]:
        raise AssertionError(f"restaging one edited layer source rewrote: {fresh}")
    manifest = json.loads((stage_root / MANIFEST_NAME).read_text(encoding="utf-8"))
    deleted = f"src/game/{LAYER_ID}/weapon/Match.cpp"
    if (stage_root / deleted).exists() or deleted in {entry["path"] for entry in manifest["files"]}:
        raise AssertionError("a deleted layer source stayed in the stage or its manifest")
    if f"src/game/{LAYER_ID}/weapon/Nested.cpp" not in mtimes:
        raise AssertionError("deleting one tree's file dropped the other tree's file from their shared directory")
    if check_fresh(stage_root) != 0:
        raise AssertionError("a restaged layer reads as stale")

    # a layer whose trees hold no files yet still has its directory in the game
    # tree, and keeps it when restaged
    empty = work / "empty"
    (empty / "src" / "game").mkdir(parents=True)
    write_file(empty / "layer.json", json.dumps(layer_manifest(sources={"game": "src/game"})))
    for _ in range(2):
        if stage(project_root, gamelibs_root, stage_root, empty).returncode != 0:
            raise AssertionError("staging a layer without files failed")
        layer_dir = stage_root / "src" / "game" / LAYER_ID
        if not layer_dir.is_dir() or any(layer_dir.iterdir()):
            raise AssertionError("a layer without files lost its directory in the game tree")


def validate_layer_collisions(work: Path) -> None:
    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)

    clash = make_layer(work / "clash")
    if stage(project_root, gamelibs_root, stage_root, clash).returncode != 0:
        raise AssertionError("staging the layer before its clash failed")
    # shared and game both stage into src/game/<id>/, so one path from each clashes
    write_file(clash / "src" / "game" / "weapon" / "Nested.cpp", "// clashes with shared/weapon/Nested.cpp\n")
    # the clash makes the stage stale, so the next build restages and reports it
    if check_fresh(stage_root) != 3:
        raise AssertionError("a layer source clashing with a staged one left the stage fresh")
    result = stage(project_root, gamelibs_root, stage_root, clash)
    expected = f"provides src/game/{LAYER_ID}/weapon/Nested.cpp from both 'shared' and 'game'"
    if result.returncode == 0 or expected not in result.stderr:
        raise AssertionError(f"a layer providing one staged path twice was accepted: {result.stderr}")

    # a layer never replaces base game files
    write_file(gamelibs_root / "src" / "game" / LAYER_ID / "Existing.cpp", "// base file\n")
    result = stage(project_root, gamelibs_root, stage_root, make_layer(work / "layer"))
    if result.returncode == 0 or "would replace" not in result.stderr:
        raise AssertionError(f"a layer over an existing base directory was accepted: {result.stderr}")


def validate_retired_mpgame_tree(work: Path) -> None:
    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)
    layer = make_layer(work / "layer")
    if stage(project_root, gamelibs_root, stage_root, layer).returncode != 0:
        raise AssertionError("staging the layer before naming its retired tree failed")
    stage_manifest = (stage_root / MANIFEST_NAME).read_bytes()

    # "mpgame" points at an existing directory, so the tree name is the only reason to refuse
    explanation = (
        "source tree 'mpgame' is retired",
        "one game module serves both single-player and multiplayer",
        "into 'game'",
        "gameLocal.isMultiplayer",
        "gameLocal.IsMultiplayerMode()",
    )
    for case, sources in {
        "beside shared and game": {"shared": "src/shared", "game": "src/game", "mpgame": "src/game"},
        "alone": {"mpgame": "src/game"},
    }.items():
        write_file(layer / "layer.json", json.dumps(layer_manifest(sources=sources)))
        for tool, result in (
            ("game_layer.py", run(str(LAYER_SCRIPT), "field", str(layer), "id")),
            ("stage_gamelibs.py", stage(project_root, gamelibs_root, stage_root, layer)),
        ):
            if result.returncode == 0:
                raise AssertionError(f"{tool} accepted a layer naming 'mpgame' {case}")
            # a LayerError the tool reports, not a crash
            if not result.stderr.startswith("error: ") or "Traceback" in result.stderr:
                raise AssertionError(f"{tool} did not report the retired 'mpgame' tree {case} as an error: {result.stderr}")
            missing = [needle for needle in explanation if needle not in result.stderr]
            if missing:
                raise AssertionError(f"{tool} refused 'mpgame' {case} without explaining {missing}: {result.stderr}")

    # the manifest is read before the stage is touched, so the previous stage stands
    if (stage_root / MANIFEST_NAME).read_bytes() != stage_manifest or check_fresh(stage_root) != 0:
        raise AssertionError("refusing a layer that names 'mpgame' disturbed the previous stage")


def main() -> None:
    work = ROOT / ".tmp" / "gamelibs-layer-staging-test"
    shutil.rmtree(work, ignore_errors=True)
    try:
        validate_manifest_reader(work / "manifest")
        validate_layer_stage(work / "stage")
        validate_layer_restage(work / "restage")
        validate_layer_collisions(work / "collisions")
        validate_retired_mpgame_tree(work / "retired-mpgame")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("gamelibs_layer_staging: ok")


if __name__ == "__main__":
    main()
