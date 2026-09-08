#!/usr/bin/env python3
"""Regression checks for openQ4-game source staging."""

from __future__ import annotations

import ast
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "build"))
from gamelibs_stage_path import build_stage_root, stage_root as target_stage_root
STAGE_SCRIPT = ROOT / "tools" / "build" / "stage_gamelibs.py"
POWERSHELL_WRAPPER = ROOT / "tools" / "build" / "meson_setup.ps1"
SHELL_WRAPPER = ROOT / "tools" / "build" / "meson_setup.sh"
MANIFEST_NAME = "openq4_gamelibs_stage_manifest.json"
SUPPORT_FIXTURES = {
    "idlib/idlib_public.h": "// idlib\n",
    "renderer/RenderWorld.h": "// renderer\n",
    "ui/UserInterface.h": "// ui\n",
    "sys/sys_public.h": "// sys\n",
    "bse/BSE.h": "// bse\n",
    "MayaImport/MayaImport.h": "// maya import\n",
}
TRACKED_PYTHON_FIXTURE = "sys/linux/pk4/id_utils.py"
BYTECODE_FIXTURES = (
    "src/sys/linux/pk4/__pycache__/id_utils.cpython-314.pyc",
    "src/sys/linux/pk4/id_utils.pyc",
    "src/game/__pycache__/Game_local.cpython-314.pyc",
    "src/mpgame/generated.pyo",
)


def staged_support_dir_names() -> tuple[str, ...]:
    tree = ast.parse(STAGE_SCRIPT.read_text(encoding="utf-8"), filename=str(STAGE_SCRIPT))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if not any(isinstance(target, ast.Name) and target.id == "OPENQ4_SUPPORT_DIRS" for target in node.targets):
            continue
        value = ast.literal_eval(node.value)
        if isinstance(value, tuple) and all(isinstance(entry, str) for entry in value):
            return value
    raise AssertionError("could not read OPENQ4_SUPPORT_DIRS from stage_gamelibs.py")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def run_stage(project_root: Path, gamelibs_root: Path, stage_root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(STAGE_SCRIPT), str(project_root), str(gamelibs_root), str(stage_root)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def write_file(path: Path, data: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8")


def make_minimal_workspace(work: Path) -> tuple[Path, Path, Path]:
    project_root = work / "openQ4"
    gamelibs_root = work / "openQ4-game"
    stage_root = project_root / ".tmp" / "gamelibs_stage"

    for relative_path, contents in SUPPORT_FIXTURES.items():
        write_file(project_root / "src" / relative_path, contents)
    write_file(project_root / "src" / TRACKED_PYTHON_FIXTURE, "# tracked Python source\n")
    write_file(gamelibs_root / "src" / "game" / "Game_local.cpp", "// game\n")
    write_file(gamelibs_root / "src" / "game" / "gamesys" / "SysCvar.cpp", "// cvar\n")
    write_file(gamelibs_root / "src" / "mpgame" / "Game_local.cpp", "// mpgame\n")
    write_file(gamelibs_root / "src" / "mpgame" / "gamesys" / "SysCvar.cpp", "// mp cvar\n")
    for relative_path in BYTECODE_FIXTURES:
        destination_root = project_root if relative_path.startswith("src/sys/") else gamelibs_root
        bytecode_path = destination_root / relative_path
        bytecode_path.parent.mkdir(parents=True, exist_ok=True)
        bytecode_path.write_bytes(b"transient python bytecode\x00")
    return project_root, gamelibs_root, stage_root


def validate_manifest(stage_root: Path) -> None:
    manifest_path = stage_root / MANIFEST_NAME
    if not manifest_path.is_file():
        raise AssertionError("stage manifest was not written")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    files = manifest.get("files")
    if manifest.get("format") != 1:
        raise AssertionError("unexpected stage manifest format")
    if not isinstance(files, list) or manifest.get("fileCount") != len(files):
        raise AssertionError("stage manifest file count mismatch")

    paths = {entry["path"]: entry["sha256"] for entry in files}
    expected_paths = (
        "src/game/Game_local.cpp",
        "src/game/gamesys/SysCvar.cpp",
        "src/mpgame/Game_local.cpp",
        "src/mpgame/gamesys/SysCvar.cpp",
        *(f"src/{relative_path}" for relative_path in SUPPORT_FIXTURES),
        f"src/{TRACKED_PYTHON_FIXTURE}",
    )
    for rel in expected_paths:
        staged = stage_root / rel
        if not staged.is_file():
            raise AssertionError(f"missing staged file: {rel}")
        if paths.get(rel) != sha256(staged):
            raise AssertionError(f"manifest hash mismatch for {rel}")
    for rel in BYTECODE_FIXTURES:
        if (stage_root / rel).exists() or rel in paths:
            raise AssertionError(f"generated Python bytecode was staged: {rel}")


def validate_successful_stage(work: Path) -> None:
    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)
    result = run_stage(project_root, gamelibs_root, stage_root)
    if result.returncode != 0:
        raise AssertionError(f"stage_gamelibs.py failed unexpectedly: {result.stderr}")
    if result.stdout.strip() != stage_root.resolve().as_posix():
        raise AssertionError("stage_gamelibs.py stdout should be the resolved stage root only")
    validate_manifest(stage_root)


def validate_symlink_rejection(work: Path) -> None:
    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)
    target = gamelibs_root / "src" / "game" / "Game_local.cpp"
    link = gamelibs_root / "src" / "game" / "linked.cpp"
    try:
        os.symlink(target, link)
    except (OSError, NotImplementedError):
        return

    result = run_stage(project_root, gamelibs_root, stage_root)
    if result.returncode == 0:
        raise AssertionError("stage_gamelibs.py accepted a symlink source")
    if "refusing to stage symlink" not in result.stderr:
        raise AssertionError(f"unexpected symlink rejection message: {result.stderr}")


def validate_stage_root_guard(work: Path) -> None:
    project_root, gamelibs_root, _stage_root = make_minimal_workspace(work)
    result = run_stage(project_root, gamelibs_root, work / "outside-stage")
    if result.returncode == 0:
        raise AssertionError("stage_gamelibs.py accepted a stage root outside openQ4/.tmp")
    if "stage root must be under openQ4 .tmp" not in result.stderr:
        raise AssertionError(f"unexpected stage-root guard message: {result.stderr}")


def extract_posix_refresh_probe() -> str:
    wrapper = SHELL_WRAPPER.read_text(encoding="utf-8")
    begin_marker = "# OPENQ4_GAMELIBS_REFRESH_PROBE_BEGIN\n"
    end_marker = "# OPENQ4_GAMELIBS_REFRESH_PROBE_END"
    if wrapper.count(begin_marker) != 1 or wrapper.count(end_marker) != 1:
        raise AssertionError("could not uniquely locate the POSIX GameLibs refresh probe")
    return wrapper.split(begin_marker, 1)[1].split(end_marker, 1)[0]


def run_posix_refresh_probe(project_root: Path, gamelibs_root: Path, stage_root: Path) -> bool:
    completed = subprocess.run(
        [
            sys.executable,
            "-c",
            extract_posix_refresh_probe(),
            str(gamelibs_root),
            str(project_root),
            str(stage_root),
        ],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode == 0:
        return True
    if completed.returncode == 10:
        return False
    raise AssertionError(
        "POSIX GameLibs refresh probe failed unexpectedly:\n"
        f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
    )


def validate_posix_support_refresh_probe(work: Path) -> None:
    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)

    def restage() -> None:
        completed = run_stage(project_root, gamelibs_root, stage_root)
        if completed.returncode != 0:
            raise AssertionError(f"GameLibs restage failed: {completed.stderr}")

    restage()
    if run_posix_refresh_probe(project_root, gamelibs_root, stage_root):
        raise AssertionError("fresh POSIX support stage requested an unnecessary refresh")

    for relative_path in BYTECODE_FIXTURES:
        source_root = project_root if relative_path.startswith("src/sys/") else gamelibs_root
        bytecode_path = source_root / relative_path
        bytecode_path.write_bytes(bytecode_path.read_bytes() + b"changed")
    if run_posix_refresh_probe(project_root, gamelibs_root, stage_root):
        raise AssertionError("generated Python bytecode change requested a POSIX refresh")

    added_source = project_root / "src" / "ui" / "Added.h"
    write_file(added_source, "// added\n")
    if not run_posix_refresh_probe(project_root, gamelibs_root, stage_root):
        raise AssertionError("POSIX support-file addition did not request a refresh")
    added_source.unlink()
    restage()

    deleted_source = project_root / "src" / "sys" / "sys_public.h"
    deleted_source.unlink()
    if not run_posix_refresh_probe(project_root, gamelibs_root, stage_root):
        raise AssertionError("POSIX support-file deletion did not request a refresh")
    write_file(deleted_source, SUPPORT_FIXTURES["sys/sys_public.h"])
    restage()

    source = project_root / "src" / "bse" / "BSE.h"
    rename_intermediate = source.with_name("BSE.rename-test")
    renamed_source = source.with_name("bse.h")
    source.rename(rename_intermediate)
    rename_intermediate.rename(renamed_source)
    if not run_posix_refresh_probe(project_root, gamelibs_root, stage_root):
        raise AssertionError("POSIX support-file rename did not request a refresh")
    renamed_source.rename(rename_intermediate)
    rename_intermediate.rename(source)
    restage()

    source = project_root / "src" / "MayaImport" / "MayaImport.h"
    source_mtime_ns = source.stat().st_mtime_ns
    write_file(source, "// MAYA import\n")
    os.utime(source, ns=(source_mtime_ns, source_mtime_ns))
    if not run_posix_refresh_probe(project_root, gamelibs_root, stage_root):
        raise AssertionError("same-timestamp POSIX support edit did not request a refresh")
    restage()

    staged = stage_root / "src" / "renderer" / "RenderWorld.h"
    staged_mtime_ns = staged.stat().st_mtime_ns
    write_file(staged, "// RENDERER\n")
    os.utime(staged, ns=(staged_mtime_ns, staged_mtime_ns))
    if not run_posix_refresh_probe(project_root, gamelibs_root, stage_root):
        raise AssertionError("same-timestamp staged POSIX support edit did not request a refresh")
    restage()

    manifest_path = stage_root / MANIFEST_NAME
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for entry in manifest["files"]:
        if entry["path"] == "src/idlib/idlib_public.h":
            entry["sha256"] = "0" * 64
            break
    else:
        raise AssertionError("GameLibs manifest fixture is missing the idlib support file")
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    if not run_posix_refresh_probe(project_root, gamelibs_root, stage_root):
        raise AssertionError("POSIX support manifest drift did not request a refresh")


def validate_posix_wrapper_refresh(work: Path) -> None:
    bash = shutil.which("bash")
    if os.name != "posix" or bash is None:
        return

    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)
    stage_root = target_stage_root(project_root, "linux", "aarch64")
    wrapper = project_root / "tools" / "build" / "meson_setup.sh"
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "tools" / "build" / "meson_setup.sh", wrapper)
    shutil.copy2(ROOT / "tools" / "build" / "gamelibs_stage_path.py", wrapper.parent)

    baseline_mtime_ns = 1_700_000_000_000_000_000
    for module_name in ("game", "mpgame"):
        for source in (gamelibs_root / "src" / module_name).rglob("*"):
            if source.is_file():
                os.utime(source, ns=(baseline_mtime_ns, baseline_mtime_ns))

    result = run_stage(project_root, gamelibs_root, stage_root)
    if result.returncode != 0:
        raise AssertionError(f"initial GameLibs stage failed: {result.stderr}")

    # Simulate a mounted filesystem that loses copied source timestamps. The
    # staged manifest still proves the files are identical, so the wrapper
    # must not needlessly reconfigure Meson.
    stale_stage_mtime_ns = baseline_mtime_ns - 2_000_000_000
    for module_name in ("game", "mpgame"):
        for staged_file in (stage_root / "src" / module_name).rglob("*"):
            if staged_file.is_file():
                os.utime(staged_file, ns=(stale_stage_mtime_ns, stale_stage_mtime_ns))

    build_dir = project_root / "builddir"
    write_file(
        build_dir / "meson-info" / "intro-machines.json",
        json.dumps({"host": {"system": "linux", "cpu_family": "aarch64"}}),
    )
    write_file(build_dir / "meson-private" / "coredata.dat", "test\n")
    write_file(build_dir / "build.ninja", "# test\n")
    write_file(
        build_dir / "meson-info" / "intro-buildoptions.json",
        json.dumps(
            [
                {"name": "build_engine", "value": True},
                {"name": "build_games", "value": True},
            ]
        ),
    )

    meson_log = work / "meson.log"
    fake_meson = work / "fake-meson"
    write_file(
        fake_meson,
        "#!/usr/bin/env bash\n"
        "printf '%s\\n' \"$*\" >> \"${OPENQ4_FAKE_MESON_LOG}\"\n",
    )
    fake_meson.chmod(0o755)

    env = os.environ.copy()
    env.update(
        {
            "OPENQ4_MESON": str(fake_meson),
            "OPENQ4_SKIP_ICON_SYNC": "1",
            "OPENQ4_FAKE_MESON_LOG": str(meson_log),
            # The wrapper resolves the GameLibs repo from this variable and
            # falls back to <repo_root>/../openQ4-game only when it is unset.
            # CI exports it for the real checkout, which would otherwise make
            # the wrapper diff this fixture's stage against the real game
            # sources and always demand a reconfigure. Pin it to the fixture
            # so the test means the same thing everywhere.
            "OPENQ4_GAMELIBS_REPO": str(gamelibs_root),
        }
    )

    def run_wrapper() -> list[str]:
        meson_log.unlink(missing_ok=True)
        completed = subprocess.run(
            [bash, str(wrapper), "compile", "-C", str(build_dir)],
            cwd=project_root,
            env=env,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if completed.returncode != 0:
            raise AssertionError(
                f"meson_setup.sh failed during GameLibs refresh test:\n"
                f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
            )
        return meson_log.read_text(encoding="utf-8").splitlines()

    if any(line.startswith("setup --reconfigure ") for line in run_wrapper()):
        raise AssertionError("fresh GameLibs stage caused an unnecessary Meson reconfigure")

    for module_name in ("game", "mpgame"):
        staged_files = [
            path
            for staged_module in ("game", "mpgame")
            for path in (stage_root / "src" / staged_module).rglob("*")
            if path.is_file()
        ]
        rounded_now_ns = (time.time_ns() // 1_000_000_000) * 1_000_000_000
        for staged_file in staged_files:
            os.utime(staged_file, ns=(rounded_now_ns, rounded_now_ns))

        source = gamelibs_root / "src" / module_name / "Game_local.cpp"
        original = source.read_text(encoding="utf-8")
        write_file(source, original.replace(module_name, module_name.upper(), 1))

        invocations = run_wrapper()
        if not any(line.startswith("setup --reconfigure ") for line in invocations):
            raise AssertionError(f"{module_name} source edit did not trigger a Meson reconfigure")

        result = run_stage(project_root, gamelibs_root, stage_root)
        if result.returncode != 0:
            raise AssertionError(f"GameLibs restage failed after {module_name} edit: {result.stderr}")

    (gamelibs_root / "src" / "mpgame" / "gamesys" / "SysCvar.cpp").unlink()
    invocations = run_wrapper()
    if not any(line.startswith("setup --reconfigure ") for line in invocations):
        raise AssertionError("mpgame source deletion did not trigger a Meson reconfigure")


def validate_windows_wrapper_refresh(work: Path) -> None:
    if os.name != "nt":
        return

    powershell = shutil.which("powershell") or shutil.which("pwsh")
    if powershell is None:
        raise AssertionError("PowerShell was not found for the Windows GameLibs refresh regression test")

    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)
    result = run_stage(project_root, gamelibs_root, stage_root)
    if result.returncode != 0:
        raise AssertionError(f"initial GameLibs stage failed: {result.stderr}")

    build_dir = project_root / "builddir"
    write_file(build_dir / "meson-private" / "coredata.dat", "test\n")
    write_file(build_dir / "build.ninja", "# test\n")
    write_file(
        build_dir / "meson-info" / "intro-buildoptions.json",
        json.dumps(
            [
                {"name": "build_engine", "value": True},
                {"name": "build_games", "value": True},
            ]
        ),
    )

    probe = work / "probe-gamelibs-refresh.ps1"
    write_file(
        probe,
        r'''param(
    [Parameter(Mandatory = $true)][string]$WrapperPath,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$GameLibsRepo
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$parseTokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $WrapperPath,
    [ref]$parseTokens,
    [ref]$parseErrors
)
if ($parseErrors.Count -ne 0) {
    throw "Could not parse meson_setup.ps1: $($parseErrors[0].Message)"
}

$functionNames = @(
    "Test-MesonBuildDirectory",
    "Get-MesonBuildOptionValue",
    "Get-openQ4GameLibsRepoPath",
    "Get-GamelibsFileMap",
    "Get-GamelibsFileHashMap",
    "Test-GamelibsStageRefreshNeeded"
)
foreach ($functionName in $functionNames) {
    $functionAst = $ast.Find({
        param($node)
        return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -ceq $functionName
    }, $true)
    if ($null -eq $functionAst) {
        throw "Could not find function '$functionName' in meson_setup.ps1."
    }
    Invoke-Expression $functionAst.Extent.Text
}

$refreshNeeded = Test-GamelibsStageRefreshNeeded `
    -BuildDir $BuildDir `
    -RepoRoot $RepoRoot `
    -GameLibsRepo $GameLibsRepo
Write-Output $refreshNeeded.ToString().ToLowerInvariant()
''',
    )

    def refresh_needed() -> bool:
        completed = subprocess.run(
            [
                powershell,
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(probe),
                "-WrapperPath",
                str(POWERSHELL_WRAPPER),
                "-BuildDir",
                str(build_dir),
                "-RepoRoot",
                str(project_root),
                "-GameLibsRepo",
                str(gamelibs_root),
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if completed.returncode != 0:
            raise AssertionError(
                "PowerShell GameLibs refresh probe failed:\n"
                f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
            )
        output = [line.strip().lower() for line in completed.stdout.splitlines() if line.strip()]
        if not output or output[-1] not in {"true", "false"}:
            raise AssertionError(f"unexpected PowerShell GameLibs refresh result: {completed.stdout!r}")
        return output[-1] == "true"

    def restage() -> None:
        completed = run_stage(project_root, gamelibs_root, stage_root)
        if completed.returncode != 0:
            raise AssertionError(f"GameLibs restage failed: {completed.stderr}")

    if refresh_needed():
        raise AssertionError("fresh Windows GameLibs stage caused an unnecessary Meson reconfigure")

    for relative_path in BYTECODE_FIXTURES:
        source_root = project_root if relative_path.startswith("src/sys/") else gamelibs_root
        bytecode_path = source_root / relative_path
        bytecode_path.write_bytes(bytecode_path.read_bytes() + b"changed")
    if refresh_needed():
        raise AssertionError("generated Python bytecode change requested a Windows refresh")

    deleted_source = gamelibs_root / "src" / "mpgame" / "gamesys" / "SysCvar.cpp"
    deleted_source.unlink()
    if not refresh_needed():
        raise AssertionError("Windows GameLibs source deletion did not trigger a Meson reconfigure")

    write_file(deleted_source, "// mp cvar\n")
    restage()
    source = gamelibs_root / "src" / "game" / "Game_local.cpp"
    rename_intermediate = source.with_name("Game_local.rename-test")
    renamed_source = source.with_name("game_local.cpp")
    source.rename(rename_intermediate)
    rename_intermediate.rename(renamed_source)
    if not refresh_needed():
        raise AssertionError("Windows GameLibs source rename did not trigger a Meson reconfigure")
    renamed_source.rename(rename_intermediate)
    rename_intermediate.rename(source)
    restage()

    source_mtime_ns = source.stat().st_mtime_ns
    write_file(source, "// GAME\n")
    os.utime(source, ns=(source_mtime_ns, source_mtime_ns))
    if not refresh_needed():
        raise AssertionError("same-timestamp Windows GameLibs source edit did not trigger a Meson reconfigure")

    restage()
    staged = stage_root / "src" / "game" / "Game_local.cpp"
    staged_mtime_ns = staged.stat().st_mtime_ns
    write_file(staged, "// EVIL\n")
    os.utime(staged, ns=(staged_mtime_ns, staged_mtime_ns))
    if not refresh_needed():
        raise AssertionError("same-timestamp staged GameLibs edit did not trigger a Meson reconfigure")

    restage()
    added_support = project_root / "src" / "ui" / "Added.h"
    write_file(added_support, "// added\n")
    if not refresh_needed():
        raise AssertionError("Windows support-file addition did not trigger a Meson reconfigure")
    added_support.unlink()
    restage()

    deleted_support = project_root / "src" / "sys" / "sys_public.h"
    deleted_support.unlink()
    if not refresh_needed():
        raise AssertionError("Windows support-file deletion did not trigger a Meson reconfigure")
    write_file(deleted_support, SUPPORT_FIXTURES["sys/sys_public.h"])
    restage()

    support_source = project_root / "src" / "bse" / "BSE.h"
    support_rename_intermediate = support_source.with_name("BSE.rename-test")
    renamed_support = support_source.with_name("bse.h")
    support_source.rename(support_rename_intermediate)
    support_rename_intermediate.rename(renamed_support)
    if not refresh_needed():
        raise AssertionError("Windows support-file rename did not trigger a Meson reconfigure")
    renamed_support.rename(support_rename_intermediate)
    support_rename_intermediate.rename(support_source)
    restage()

    support_source = project_root / "src" / "MayaImport" / "MayaImport.h"
    support_mtime_ns = support_source.stat().st_mtime_ns
    write_file(support_source, "// MAYA import\n")
    os.utime(support_source, ns=(support_mtime_ns, support_mtime_ns))
    if not refresh_needed():
        raise AssertionError("same-timestamp Windows support edit did not trigger a Meson reconfigure")
    restage()

    staged_support = stage_root / "src" / "renderer" / "RenderWorld.h"
    staged_support_mtime_ns = staged_support.stat().st_mtime_ns
    write_file(staged_support, "// RENDERER\n")
    os.utime(staged_support, ns=(staged_support_mtime_ns, staged_support_mtime_ns))
    if not refresh_needed():
        raise AssertionError("same-timestamp staged Windows support edit did not trigger a Meson reconfigure")
    restage()

    manifest_path = stage_root / MANIFEST_NAME
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for entry in manifest["files"]:
        if entry["path"] == "src/idlib/idlib_public.h":
            entry["sha256"] = "0" * 64
            break
    else:
        raise AssertionError("GameLibs manifest fixture is missing the idlib support file")
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    if not refresh_needed():
        raise AssertionError("Windows support manifest drift did not trigger a Meson reconfigure")


def validate_source_contracts() -> None:
    script = STAGE_SCRIPT.read_text(encoding="utf-8")
    shell_wrapper = (ROOT / "tools" / "build" / "meson_setup.sh").read_text(encoding="utf-8")
    powershell_wrapper = POWERSHELL_WRAPPER.read_text(encoding="utf-8")
    meson = (ROOT / "meson.build").read_text(encoding="utf-8")
    game_targets = (ROOT / "content" / "baseoq4" / "meson.build").read_text(encoding="utf-8")
    aas_file = (ROOT / "src" / "aas" / "AASFile.h").read_text(encoding="utf-8")
    precompiled = (ROOT / "src" / "idlib" / "precompiled.h").read_text(encoding="utf-8")
    validator = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")
    building = (ROOT / "BUILDING.md").read_text(encoding="utf-8")

    fixture_support_dirs = tuple(relative_path.split("/", 1)[0] for relative_path in SUPPORT_FIXTURES)
    if fixture_support_dirs != staged_support_dir_names():
        raise AssertionError("GameLibs wrapper fixtures do not cover every staged openQ4 support directory")

    required_script_tokens = (
        "MANIFEST_NAME",
        "openq4_gamelibs_stage_manifest.json",
        "refusing to stage symlink",
        "refusing to stage non-regular file",
        "stage root must be under openQ4 .tmp",
        "sha256",
        "gameLibsGitCommit",
        "gameLibsGitDirty",
        "validate_stage_manifest",
        "PYTHON_BYTECODE_SUFFIXES",
        '"__pycache__" in relative.parts',
        '"mpgame": gamelibs_root / "src" / "mpgame"',
    )
    for token in required_script_tokens:
        if token not in script:
            raise AssertionError(f"missing staging script token: {token}")

    for token in (
        "test_gamelibs_stage_refresh_needed",
        "OPENQ4_GAMELIBS_REFRESH_PROBE_BEGIN",
        'support_dir_names = ("idlib", "renderer", "ui", "sys", "bse", "MayaImport")',
        "source_specs",
        "staged_specs",
        "regular_file_map",
        "python_bytecode_suffixes",
        '"__pycache__" in relative_parts',
        "onerror=raise_walk_error",
        "manifest_hashes",
        "file_sha256(source_path)",
        "file_sha256(staged_files[relative_path])",
        "raise SystemExit(0 if needs_refresh else 10)",
        "run_meson setup --reconfigure",
    ):
        if token not in shell_wrapper:
            raise AssertionError(f"missing POSIX GameLibs refresh token: {token}")

    for token in (
        "Get-GamelibsFileMap",
        "Get-GamelibsFileHashMap",
        "openq4_gamelibs_stage_manifest.json",
        "System.Security.Cryptography.SHA256",
        '@("idlib", "renderer", "ui", "sys", "bse", "MayaImport")',
        "$sourceRoots += $RepoRoot",
        "$stagedRoots += $stageRoot",
        "Test-GamelibsStageRefreshNeeded",
        '$relativeParts -ccontains "__pycache__"',
    ):
        if token not in powershell_wrapper:
            raise AssertionError(f"missing Windows GameLibs refresh token: {token}")

    if "& $buildGameLibsScript -GameLibsRepo $gameLibsRepo" not in powershell_wrapper:
        raise AssertionError("Windows wrapper does not bind the companion repository as a named parameter")
    if '$buildArgs += @("-GameLibsRepo", $gameLibsRepo)' in powershell_wrapper:
        raise AssertionError("Windows wrapper still uses positional array splatting for a named parameter")

    for token in (
        "openq4_gamelibs_stage_manifest.json",
        "Staged openQ4-game source manifest not found",
        "game_sp_sources = files(game_sp_absolute_paths)",
        "game_mp_sources = files(game_mp_absolute_paths)",
        "game_sources = game_sp_sources + game_mp_sources",
        "game_sp_module_defs_file",
        "game_mp_module_defs_file",
        "game_target_override_options = ['cpp_std=c++17']",
    ):
        if token not in meson:
            raise AssertionError(f"missing Meson staging contract token: {token}")

    sp_marker = "if build_games and build_game_sp"
    mp_marker = "if build_games and build_game_mp"
    if sp_marker not in game_targets or mp_marker not in game_targets:
        raise AssertionError("missing SP/MP game target blocks")
    sp_target_block, mp_target_block = game_targets.split(mp_marker, 1)
    sp_target_block = sp_target_block.split(sp_marker, 1)[1]
    for token in ("game_sp_sources", "game_sp_module_defs_file", "game_target_override_options"):
        if token not in sp_target_block:
            raise AssertionError(f"missing SP target binding: {token}")
    for token in ("game_mp_sources", "game_mp_module_defs_file"):
        if token in sp_target_block:
            raise AssertionError(f"SP target incorrectly references MP binding: {token}")
    for token in ("game_mp_sources", "game_mp_module_defs_file", "game_target_override_options", "-DGAME_MPAPI"):
        if token not in mp_target_block:
            raise AssertionError(f"missing MP target binding: {token}")
    for token in ("game_sp_sources", "game_sp_module_defs_file"):
        if token in mp_target_block:
            raise AssertionError(f"MP target incorrectly references SP binding: {token}")

    for token in (
        "#ifdef GAME_MPAPI",
        '#include "../mpgame/Game_local.h"',
        '#include "../game/Game_local.h"',
    ):
        if token not in precompiled:
            raise AssertionError(f"missing SP/MP precompiled-header routing token: {token}")

    for token in (
        "aasArea_t& GetArea(int index) { return areas[index]; }",
        "const aasArea_t& GetArea(int index) const { return areas[index]; }",
    ):
        if token not in aas_file:
            raise AssertionError(f"missing SDK-compatible AAS area accessor: {token}")

    if "gamelibs_staging.py" not in validator:
        raise AssertionError("validation runner does not include gamelibs_staging.py")
    if "source-input repository" not in building:
        raise AssertionError("BUILDING.md does not document the GameLibs source-input role")


def validate_target_stage_paths(work: Path) -> None:
    project_root = work / "project"
    build_dir = project_root / "builddir"
    for system, cpu, expected in (
        ("windows", "x86_64", "gamelibs_stage"),
        ("linux", "x86_64", "gamelibs_stage-linux-x64"),
        ("linux", "aarch64", "gamelibs_stage-linux-arm64"),
        ("darwin", "aarch64", "gamelibs_stage-darwin-arm64"),
        ("darwin", "x86_64", "gamelibs_stage-darwin-x64"),
        ("android", "aarch64", "gamelibs_stage-android-arm64"),
    ):
        write_file(
            build_dir / "meson-info" / "intro-machines.json",
            json.dumps({"host": {"system": system, "cpu_family": cpu}}),
        )
        actual = build_stage_root(project_root, build_dir)
        if actual != project_root / ".tmp" / expected:
            raise AssertionError(f"wrong target stage for {system}/{cpu}: {actual}")
    if target_stage_root(project_root, "macos", "arm64") != target_stage_root(project_root, "darwin", "aarch64"):
        raise AssertionError("package platform and Meson machine must identify the same source stage")


def main() -> None:
    work = ROOT / ".tmp" / "gamelibs-staging-test"
    shutil.rmtree(work, ignore_errors=True)
    try:
        validate_target_stage_paths(work / "target-paths")
        validate_successful_stage(work / "success")
        validate_symlink_rejection(work / "symlink")
        validate_stage_root_guard(work / "stage-root")
        validate_posix_support_refresh_probe(work / "posix-support-refresh")
        validate_posix_wrapper_refresh(work / "posix-refresh")
        validate_windows_wrapper_refresh(work / "windows-refresh")
        validate_source_contracts()
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("gamelibs_staging: ok")


if __name__ == "__main__":
    main()
