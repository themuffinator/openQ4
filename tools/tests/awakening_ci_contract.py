#!/usr/bin/env python3
"""Canonical campaign/source CI and package boundaries."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def main():
    workflows = ROOT / ".github/workflows"
    for name in ("commit-validation.yml", "push-verification.yml", "manual-release.yml",
                 "linux-arm64-cross.yml", "macos-debug.yml", "macos-sanitizer.yml", "macos-universal2-candidate.yml"):
        source = (workflows / name).read_text(encoding="utf-8")
        for obsolete in ("openQ4-game.git", "openQ4-game-awakening.git", "OPENQ4_AWAKENING_SHA",
                         "OPENQ4_GAMELIBS_SHA", "inputs.openq4_game_ref", "--require-game-layer q4xbase",
                         "layer_sp_module", "layer_mp_module"):
            assert obsolete not in source, f"{name} depends on retired sources: {obsolete}"
        assert "meson_setup" in source
    release = (workflows / "manual-release.yml").read_text(encoding="utf-8")
    # one game module serves game_sp and game_mp
    assert 'game_module="${module_dir}/game_${{ matrix.binary_arch }}.dylib"' in release
    assert 'check_macos_install_name "${game_module}" "@loader_path/game_${{ matrix.binary_arch }}.dylib"' in release
    for mode in ("sp", "mp"):
        assert f"{mode}_module=" not in release, f"manual-release still names a split {mode} module"
    meson = (ROOT / "meson.build").read_text(encoding="utf-8")
    assert "game_libs_repo_root = meson.project_source_root()" in meson
    assert "game_source_inventory.py" in meson
    assert "subdir('content/q4xbase')" not in meson
    for tree in ("game",):
        assert (ROOT / "src" / tree / "Game_local.h").is_file()
        assert "SDK" in (ROOT / "src" / tree / "LICENSE").read_text()
    assert (ROOT / "LICENSES/QUAKE-4-SDK-EULA.rtf").is_file()
    assert not (ROOT / "src/game/awakening/mpgame").exists()
    turret = (ROOT / "src/game/awakening/ai/Monster_Turret.cpp").read_text()
    assert 'SPAWNCLASS_SUBSTITUTION_FOR_GAME( "q4xbase"' in turret
    from sys import path
    path.insert(0, str(ROOT / "tools/build"))
    from game_layer import PACKAGED_LAYER_GAME_DIRS
    assert PACKAGED_LAYER_GAME_DIRS == ()
    print("awakening_ci_contract: ok")

if __name__ == "__main__":
    main()
