#!/usr/bin/env python3
"""Run openQ4-updater against a throwaway install and check what it did.

    python tools/tests/updater_helper_apply.py --helper <openQ4-updater_x64[.exe]> [--scratch <dir>]

Cases:
  * a complete plan moves every staged file in, keeps the replaced files in
    .update/backup/, retires a listed pack, writes an "ok" result, removes the
    plan and starts the relaunch program;
  * a plan that fails halfway (a folder sits where a file must go) undoes the
    files it already moved, so the install is byte-for-byte what it was;
  * a plan without its end line, or one naming another install, changes
    nothing;
  * the helper refuses a folder that is not called .update.

The relaunch program is a copy of the helper itself: started without
arguments it prints its usage and exits, which proves the relaunch ran
without starting a game.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


def write_plan(update_dir: Path, install: Path, relaunch: Path, files: list[str], removals: list[str],
               marker: Path, complete: bool = True, install_override: str | None = None) -> None:
    lines = [
        "openq4-update-plan 1",
        "version\t0.15.0",
        "from\t0.14.1",
        "install\t" + escape(install_override or str(install)),
        "relaunch\t" + escape(str(relaunch)),
        "cwd\t" + escape(str(install)),
        # The relaunched helper writes nothing, but a marker argument shows on
        # POSIX that the arguments survive; Windows replays the tail.
        "commandtail\t--marker " + escape(str(marker)),
        "arg\t--marker",
        "arg\t" + escape(str(marker)),
        "restartfailed\tStart openQ4 again from its folder.",
    ]
    lines += ["file\t" + escape(f) for f in files]
    lines += ["remove\t" + escape(r) for r in removals]
    if complete:
        lines.append("end")
    (update_dir / "plan.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def tree_digest(root: Path) -> dict[str, str]:
    result = {}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        if relative.startswith(".update"):
            continue
        if path.is_file():
            result[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def make_install(root: Path, helper: Path, case: str) -> tuple[Path, Path]:
    # One folder per case: a relaunched copy from the case before may still be exiting.
    install = root / case / "openQ4 \u00e9t\u00e9"
    shutil.rmtree(install, ignore_errors=True)
    (install / "baseoq4").mkdir(parents=True)
    (install / "openQ4-client_x64.exe").write_bytes(b"old client")
    (install / "baseoq4" / "game_x64.dll").write_bytes(b"old game")
    (install / "baseoq4" / "pak0.pk4").write_bytes(b"old pak0")
    (install / "baseoq4" / "pak7.pk4").write_bytes(b"retired overlay")
    (install / "keep.txt").write_bytes(b"untouched")
    relaunch = install / ("relaunch" + helper.suffix)
    shutil.copy2(helper, relaunch)
    update = install / ".update"
    (update / "stage" / "baseoq4").mkdir(parents=True)
    return install, relaunch


def stage(install: Path, files: dict[str, bytes]) -> list[str]:
    for relative, data in files.items():
        target = install / ".update" / "stage" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return sorted(files)


def run_helper(helper: Path, update_dir: Path) -> int:
    copy = update_dir / helper.name
    shutil.copy2(helper, copy)
    completed = subprocess.run([str(copy), "--apply", str(update_dir), "--wait-pid", "0"],
                               capture_output=True, timeout=120)
    return completed.returncode


def read_result(update_dir: Path) -> dict[str, str]:
    result = {}
    text = (update_dir / "result.txt").read_text(encoding="utf-8")
    for line in text.splitlines()[1:]:
        if line == "end":
            break
        key, _, value = line.partition("\t")
        result[key] = value
    return result


def check(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)
        print("FAIL:", message)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--helper", required=True)
    parser.add_argument("--scratch", default="")
    args = parser.parse_args()
    helper = Path(args.helper).resolve()
    scratch = Path(args.scratch) if args.scratch else Path(tempfile.mkdtemp(prefix="openq4-updater-"))
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True, exist_ok=True)
    failures: list[str] = []

    # 1. A complete plan.
    install, relaunch = make_install(scratch, helper, "complete")
    update = install / ".update"
    files = stage(install, {
        "openQ4-client_x64.exe": b"new client",
        "baseoq4/game_x64.dll": b"new game",
        "baseoq4/pak0.pk4": b"new pak0",
        "docs/new.html": b"new doc",
        relaunch.name: relaunch.read_bytes(),
    })
    marker = scratch / "relaunched"
    write_plan(update, install, relaunch, files, ["baseoq4/pak7.pk4"], marker)
    code = run_helper(helper, update)
    check(code == 0, f"complete plan exits 0 (got {code})", failures)
    check((install / "openQ4-client_x64.exe").read_bytes() == b"new client", "client replaced", failures)
    check((install / "baseoq4" / "pak0.pk4").read_bytes() == b"new pak0", "pak0 replaced", failures)
    check((install / "docs" / "new.html").read_bytes() == b"new doc", "new file added", failures)
    check(not (install / "baseoq4" / "pak7.pk4").exists(), "retired pack removed", failures)
    check((update / "backup" / "baseoq4" / "pak7.pk4").read_bytes() == b"retired overlay", "retired pack kept in backup", failures)
    check((update / "backup" / "openQ4-client_x64.exe").read_bytes() == b"old client", "old client kept in backup", failures)
    check((install / "keep.txt").read_bytes() == b"untouched", "unlisted file untouched", failures)
    check(not (update / "plan.txt").exists(), "plan removed after use", failures)
    result = read_result(update)
    check(result.get("status") == "ok" and result.get("version") == "0.15.0", f"ok result ({result})", failures)
    log = (update / "updater.log").read_text(encoding="utf-8", errors="replace")
    check("installed 0.15.0" in log, "log records the install", failures)
    check("started " in log and relaunch.name in log, "the game is started again", failures)

    # 2. A failure halfway is undone.
    install, relaunch = make_install(scratch, helper, "rollback")
    update = install / ".update"
    before = tree_digest(install)
    files = stage(install, {
        "baseoq4/game_x64.dll": b"new game",
        "openQ4-client_x64.exe": b"new client",
        "zz/blocked": b"cannot land",
    })
    (install / "zz" / "blocked").mkdir(parents=True)	# a folder in the way of the last file
    before = tree_digest(install)
    write_plan(update, install, relaunch, files, ["baseoq4/pak7.pk4"], marker)
    code = run_helper(helper, update)
    check(code != 0, "failed plan exits non-zero", failures)
    check(tree_digest(install) == before, "failed plan leaves the install exactly as it was", failures)
    check((install / ".update" / "stage" / "openQ4-client_x64.exe").exists(), "undone files return to the stage", failures)
    result = read_result(update)
    check(result.get("status") == "failed" and "folder is in the way" in result.get("detail", ""), f"failed result ({result})", failures)

    # 3. Incomplete plans and plans for another install change nothing.
    for label, complete, override in (("unfinished plan", False, None), ("plan for another install", True, str(scratch / "elsewhere"))):
        install, relaunch = make_install(scratch, helper, label.replace(" ", "-"))
        update = install / ".update"
        files = stage(install, {"openQ4-client_x64.exe": b"new client"})
        before = tree_digest(install)
        write_plan(update, install, relaunch, files, [], marker, complete=complete, install_override=override)
        run_helper(helper, update)
        check(tree_digest(install) == before, f"{label} changes nothing", failures)

    # 4. Only a folder named .update is accepted.
    other = scratch / "not-an-update"
    other.mkdir(exist_ok=True)
    completed = subprocess.run([str(helper), "--apply", str(other)], capture_output=True, timeout=60)
    check(completed.returncode == 2, "refuses a folder that is not .update", failures)

    if not args.scratch:
        # The relaunched copies may still be exiting on Windows.
        for _ in range(20):
            try:
                shutil.rmtree(scratch)
                break
            except OSError:
                time.sleep(0.25)

    if failures:
        print(f"{len(failures)} updater helper checks failed")
        return 1
    print("updater helper: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
