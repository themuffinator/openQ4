#!/usr/bin/env python3
"""Execute production weapon initialization across snapshot catch-up boundaries."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    source = (ROOT / "src/mpgame/Player.cpp").read_text(encoding="utf-8")
    # Keep the production admission/initialization block, stopping before
    # unrelated GUI, combat and drawing adapters. The latter are qualified
    # by mp_mvd_seek_smoke.py against recorded gameplay.
    initialization = source.split("void idPlayer::UpdateWeapon( void )", 1)[1].split("{", 1)[1].split(
        "\tif ( hiddenWeapon && tipUp", 1)[0]
    harness = r'''
#include <cassert>
#include <cstdio>
struct { bool isClient = true; } gameLocal;
struct Weapon {
    int catches = 0, raiseStart = -1;
    void NetCatchup() { ++catches; }
    void SetNetRaiseStartTime(int time) { raiseStart = time; }
};
struct idPlayer {
    int health = 100, idealWeapon = 1, creations = 0, netWeaponRaiseTime = -1;
    bool spectating = false, weaponViewModel = true, weaponWorldModel = true;
    bool weaponCatchup = false;
    Weapon storage, *weapon = nullptr;
    void SetWeapon(int index) { assert(index == idealWeapon); weapon = &storage; ++creations; }
    void UpdateWeapon();
};
void idPlayer::UpdateWeapon() { @PRODUCTION@ }
int main() {
    for (int catchup = 0; catchup != 2; ++catchup) {
        idPlayer player;
        player.weaponCatchup = catchup != 0;
        player.UpdateWeapon();
        assert(player.creations == 1 && player.storage.catches == catchup);
        assert(!player.weaponCatchup);
        player.UpdateWeapon();
        assert(player.creations == 1 && player.storage.catches == catchup);
    }
    // A seek's catch-up wins over a respawn's raise time; either way the time is spent.
    for (int catchup = 0; catchup != 2; ++catchup) {
        idPlayer player;
        player.weaponCatchup = catchup != 0;
        player.netWeaponRaiseTime = 500;
        player.UpdateWeapon();
        assert(player.storage.catches == catchup && player.storage.raiseStart == (catchup ? -1 : 500));
        assert(player.netWeaponRaiseTime == -1 && !player.weaponCatchup);
    }
    for (int admission = 0; admission != 4; ++admission) {
        idPlayer player;
        player.weaponCatchup = true;
        if (admission == 0) player.health = 0;
        if (admission == 1) player.weaponViewModel = false;
        if (admission == 2) player.weaponWorldModel = false;
        if (admission == 3) player.idealWeapon = -1;
        player.UpdateWeapon();
        assert(player.creations == 0 && player.storage.catches == 0);
        assert(player.weaponCatchup);
    }
    puts("MVD weapon initialization: PASS (catch-up exactly once; normal spawn and missing snapshots preserved)");
}
'''
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    task_tmp = ROOT / ".tmp"
    task_tmp.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mvd-seek-weapon-", dir=task_tmp) as name:
        directory = Path(name)
        cpp = directory / "fixture.cpp"
        exe = directory / ("fixture.exe" if os.name == "nt" else "fixture")
        command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)]
        cpp.write_text(harness.replace("@PRODUCTION@", initialization), encoding="utf-8")
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)
        control = initialization.replace("weapon->NetCatchup();", "/* lost snapshot catch-up */")
        if control == initialization:
            raise AssertionError("missing catch-up mutation anchor")
        cpp.write_text(harness.replace("@PRODUCTION@", control), encoding="utf-8")
        subprocess.run(command, cwd=directory, check=True)
        result = subprocess.run([str(exe)], cwd=directory, capture_output=True)
        if result.returncode == 0:
            raise AssertionError("discarded snapshot catch-up control passed")
    print("mvd_seek_weapon_state: PASS (pre-fix control fails)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
