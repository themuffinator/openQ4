#!/usr/bin/env python3
"""Validation profiles for OpenQ4 local pushes and pull requests."""

from __future__ import annotations

import argparse
import fnmatch
import os
import platform
import shlex
import subprocess
import sys
import time
from pathlib import Path


PROFILE_DEFAULTS = {
    "push": {
        "build_dir": "builddir",
        "buildtype": "debug",
        "clean": False,
        "install": False,
    },
    "pr": {
        "build_dir": ".tmp/validation/pr-builddir",
        "buildtype": "release",
        "clean": True,
        "install": True,
    },
}

STAGED_REQUIRED_GAME_FILES = (
    "default.cfg",
    "mod.json",
    "openq4_defaults.cfg",
    "openq4_profile_steamdeck.cfg",
)

NON_RUNTIME_PATTERNS = (
    "*.exp",
    "*.ilk",
    "*.lib",
    "*.map",
    "*.pdb",
    "*.zip",
)


class ValidationError(RuntimeError):
    pass


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def host_is_windows() -> bool:
    return platform.system().lower() == "windows"


def format_command(command: list[str]) -> str:
    if host_is_windows():
        return subprocess.list2cmdline(command)
    return " ".join(shlex.quote(part) for part in command)


def rel(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path)


def section(title: str) -> None:
    print(f"\n==> {title}", flush=True)


def run_command(
    command: list[str],
    *,
    cwd: Path,
    env: dict[str, str],
    title: str,
    dry_run: bool,
) -> None:
    section(title)
    print(format_command(command), flush=True)
    if dry_run:
        return

    result = subprocess.run(command, cwd=str(cwd), env=env)
    if result.returncode != 0:
        raise ValidationError(f"{title} failed with exit code {result.returncode}.")


def capture_command(command: list[str], *, cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def resolve_meson_wrapper(root: Path) -> list[str]:
    if host_is_windows():
        wrapper = root / "tools" / "build" / "meson_setup.ps1"
        if not wrapper.is_file():
            raise ValidationError(f"OpenQ4 Meson wrapper not found: {wrapper}")
        return ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(wrapper)]

    wrapper = root / "tools" / "build" / "meson_setup.sh"
    if not wrapper.is_file():
        raise ValidationError(f"OpenQ4 Meson wrapper not found: {wrapper}")
    return ["bash", str(wrapper)]


def is_meson_build_dir(path: Path) -> bool:
    return (path / "meson-private" / "coredata.dat").is_file() and (path / "build.ninja").is_file()


def setup_args(args: argparse.Namespace, root: Path, build_dir: Path) -> list[str]:
    base = ["setup"]
    if args.clean or not is_meson_build_dir(build_dir):
        base += ["--wipe", str(build_dir), str(root)]
    else:
        base += ["--reconfigure", str(build_dir), str(root)]

    base += [
        "--backend",
        "ninja",
        f"--buildtype={args.buildtype}",
        "--wrap-mode=forcefallback",
    ]

    if args.platform_backend:
        base.append(f"-Dplatform_backend={args.platform_backend}")

    base.extend(args.extra_setup_arg or [])
    return base


def compile_args(args: argparse.Namespace, build_dir: Path) -> list[str]:
    command = ["compile", "-C", str(build_dir)]
    if args.jobs is not None:
        command += ["-j", str(args.jobs)]
    command.extend(args.extra_compile_arg or [])
    return command


def install_args(build_dir: Path) -> list[str]:
    return ["install", "-C", str(build_dir), "--no-rebuild", "--skip-subprojects"]


def validation_env(args: argparse.Namespace, root: Path) -> dict[str, str]:
    env = os.environ.copy()
    if args.game_libs_repo:
        env["OPENQ4_GAMELIBS_REPO"] = str(Path(args.game_libs_repo).resolve())
    elif "OPENQ4_GAMELIBS_REPO" not in env:
        default_game_libs = (root / ".." / "OpenQ4-GameLibs").resolve()
        env["OPENQ4_GAMELIBS_REPO"] = str(default_game_libs)

    if args.build_gamelibs:
        env["OPENQ4_BUILD_GAMELIBS"] = "1"

    if args.skip_icon_sync:
        env["OPENQ4_SKIP_ICON_SYNC"] = "1"

    return env


def run_python_tests(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    tests = [
        root / "tools" / "tests" / "hdr_postprocess_math.py",
    ]
    for test_script in tests:
        if not test_script.is_file():
            raise ValidationError(f"Python validation test not found: {test_script}")
        run_command(
            [sys.executable, str(test_script)],
            cwd=root,
            env=env,
            title=f"Python check: {rel(test_script, root)}",
            dry_run=args.dry_run,
        )


def ensure_game_libs_repo(env: dict[str, str]) -> None:
    game_libs_repo = Path(env["OPENQ4_GAMELIBS_REPO"])
    expected = game_libs_repo / "src" / "game"
    if not expected.is_dir():
        raise ValidationError(
            "OpenQ4-GameLibs source directory was not found. "
            f"Expected: {expected}"
        )


def find_any(root: Path, patterns: tuple[str, ...]) -> list[Path]:
    matches: list[Path] = []
    for pattern in patterns:
        matches.extend(sorted(root.glob(pattern)))
    return matches


def validate_staged_payload(root: Path, *, dry_run: bool) -> None:
    section("Validate staged .install payload")
    if dry_run:
        print("Staged payload checks skipped during dry run.", flush=True)
        return

    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    if not install_root.is_dir():
        raise ValidationError(f"Install root is missing: {install_root}")
    if not game_dir.is_dir():
        raise ValidationError(f"Staged game directory is missing: {game_dir}")

    client_candidates = sorted(install_root.glob("openQ4-client_*"))
    dedicated_candidates = sorted(install_root.glob("openQ4-ded_*"))
    if not client_candidates:
        raise ValidationError("Staged client executable was not found under .install/.")
    if not dedicated_candidates:
        raise ValidationError("Staged dedicated-server executable was not found under .install/.")

    sp_modules = find_any(game_dir, ("game-sp_*.dll", "game-sp_*.so", "game-sp_*.dylib"))
    mp_modules = find_any(game_dir, ("game-mp_*.dll", "game-mp_*.so", "game-mp_*.dylib"))
    if not sp_modules:
        raise ValidationError("Staged single-player game module was not found under .install/baseoq4/.")
    if not mp_modules:
        raise ValidationError("Staged multiplayer game module was not found under .install/baseoq4/.")

    for relative_name in STAGED_REQUIRED_GAME_FILES:
        required_file = game_dir / relative_name
        if not required_file.is_file():
            raise ValidationError(f"Required staged game file is missing: {rel(required_file, root)}")

    if host_is_windows() and not (install_root / "OpenAL32.dll").is_file():
        raise ValidationError("Windows staged payload is missing OpenAL32.dll.")

    bad_artifacts: list[Path] = []
    for directory in (install_root, game_dir):
        for path in directory.iterdir():
            if not path.is_file():
                continue
            if any(fnmatch.fnmatch(path.name.lower(), pattern.lower()) for pattern in NON_RUNTIME_PATTERNS):
                bad_artifacts.append(path)

    if bad_artifacts:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in bad_artifacts)
        raise ValidationError(f"Non-runtime artifacts remain staged:\n{formatted}")

    print(f"Client: {rel(client_candidates[0], root)}", flush=True)
    print(f"Dedicated server: {rel(dedicated_candidates[0], root)}", flush=True)
    print(f"SP module: {rel(sp_modules[0], root)}", flush=True)
    print(f"MP module: {rel(mp_modules[0], root)}", flush=True)


def run_runtime_matrix(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    matrix_script = root / "tools" / "tests" / "renderer_validation_matrix.py"
    if not matrix_script.is_file():
        raise ValidationError(f"Renderer validation matrix not found: {matrix_script}")

    command = [
        sys.executable,
        str(matrix_script),
        "--tiers",
        args.runtime_tiers,
        "--timeout",
        str(args.runtime_timeout),
    ]
    if args.runtime_cases:
        command += ["--cases", args.runtime_cases]
    if args.runtime_basepath is not None:
        command += ["--basepath", args.runtime_basepath]

    run_command(
        command,
        cwd=root,
        env=env,
        title="Optional renderer startup validation matrix",
        dry_run=args.dry_run,
    )


def check_dirty_worktree(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    if not args.fail_on_dirty:
        return

    result = capture_command(["git", "status", "--short"], cwd=root, env=env)
    if result.returncode != 0:
        raise ValidationError(result.stderr.strip() or "git status failed.")
    if result.stdout.strip():
        raise ValidationError("Working tree is dirty; commit, stash, or rerun without --fail-on-dirty.")


def git_value(root: Path, env: dict[str, str], *git_args: str) -> str:
    result = capture_command(["git", *git_args], cwd=root, env=env)
    if result.returncode != 0:
        return "unavailable"
    return result.stdout.strip() or "unavailable"


def describe_git_revision(root: Path, env: dict[str, str]) -> str:
    short_sha = git_value(root, env, "rev-parse", "--short", "HEAD")
    branch = git_value(root, env, "rev-parse", "--abbrev-ref", "HEAD")
    status = capture_command(["git", "status", "--short"], cwd=root, env=env)
    dirty_state = "dirty" if status.returncode == 0 and status.stdout.strip() else "clean"

    github_sha = env.get("GITHUB_SHA", "").strip()
    if github_sha:
        return f"{short_sha} ({branch}, {dirty_state}, GitHub SHA {github_sha[:12]})"

    return f"{short_sha} ({branch}, {dirty_state})"


def apply_profile_defaults(args: argparse.Namespace, root: Path) -> None:
    defaults = PROFILE_DEFAULTS[args.profile]
    if args.build_dir is None:
        args.build_dir = str(root / defaults["build_dir"])
    if args.buildtype is None:
        args.buildtype = defaults["buildtype"]
    if args.clean is None:
        args.clean = defaults["clean"]
    if args.install is None:
        args.install = defaults["install"]


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=sorted(PROFILE_DEFAULTS), help="Validation profile to run.")
    parser.add_argument("--source-root", default="", help="OpenQ4 source root. Defaults to this script's repository.")
    parser.add_argument("--build-dir", default=None, help="Meson build directory for this validation run.")
    parser.add_argument("--buildtype", default=None, help="Meson buildtype. Profile default: push=debug, pr=release.")
    parser.add_argument("--platform-backend", default="", help="Optional Meson platform_backend override.")
    parser.add_argument("--clean", dest="clean", action="store_true", default=None, help="Use Meson --wipe for setup.")
    parser.add_argument("--no-clean", dest="clean", action="store_false", help="Reconfigure/reuse an existing build directory.")
    parser.add_argument("--install", dest="install", action="store_true", default=None, help="Run Meson install and staged payload checks.")
    parser.add_argument("--no-install", dest="install", action="store_false", help="Skip Meson install and staged payload checks.")
    parser.add_argument("--skip-python-tests", action="store_true", help="Skip lightweight Python validation tests.")
    parser.add_argument("--skip-build", action="store_true", help="Skip Meson setup/compile/install steps.")
    parser.add_argument("--build-gamelibs", action="store_true", help="Ask the Windows Meson wrapper to build OpenQ4-GameLibs during compile.")
    parser.add_argument("--game-libs-repo", default="", help="Override the OpenQ4-GameLibs companion repository path.")
    parser.add_argument("--skip-icon-sync", action="store_true", help="Set OPENQ4_SKIP_ICON_SYNC=1 for this run.")
    parser.add_argument("--jobs", "-j", type=int, default=None, help="Parallel compile job count passed to Meson.")
    parser.add_argument("--extra-setup-arg", action="append", default=[], help="Additional argument appended to Meson setup.")
    parser.add_argument("--extra-compile-arg", action="append", default=[], help="Additional argument appended to Meson compile.")
    parser.add_argument("--runtime", action="store_true", help="Also run the safe renderer startup validation matrix after install.")
    parser.add_argument("--runtime-cases", default="", help="Comma-separated renderer validation case ids.")
    parser.add_argument("--runtime-tiers", default="auto,legacy", help="Renderer tiers for --runtime. Defaults to auto,legacy.")
    parser.add_argument("--runtime-timeout", type=int, default=60, help="Per-case renderer validation timeout.")
    parser.add_argument("--runtime-basepath", default=None, help="Quake 4 base path override for renderer validation.")
    parser.add_argument("--fail-on-dirty", action="store_true", help="Fail when the OpenQ4 working tree has uncommitted changes.")
    parser.add_argument("--dry-run", action="store_true", help="Print the selected commands without executing them.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    root = Path(args.source_root).resolve() if args.source_root else repo_root_from_script()
    apply_profile_defaults(args, root)
    build_dir = Path(args.build_dir).resolve()
    build_dir.parent.mkdir(parents=True, exist_ok=True)
    env = validation_env(args, root)
    wrapper = resolve_meson_wrapper(root)

    started = time.monotonic()
    print(f"OpenQ4 {args.profile} validation", flush=True)
    print(f"Source root: {root}", flush=True)
    print(f"Git revision: {describe_git_revision(root, env)}", flush=True)
    print(f"Build dir: {build_dir}", flush=True)
    print(f"Build type: {args.buildtype}", flush=True)
    print(f"GameLibs repo: {env['OPENQ4_GAMELIBS_REPO']}", flush=True)

    try:
        check_dirty_worktree(args, root, env)
        ensure_game_libs_repo(env)

        if not args.skip_python_tests:
            run_python_tests(args, root, env)

        if not args.skip_build:
            run_command(
                wrapper + setup_args(args, root, build_dir),
                cwd=root,
                env=env,
                title="Meson setup",
                dry_run=args.dry_run,
            )
            run_command(
                wrapper + compile_args(args, build_dir),
                cwd=root,
                env=env,
                title="Meson compile",
                dry_run=args.dry_run,
            )
            if args.install:
                run_command(
                    wrapper + install_args(build_dir),
                    cwd=root,
                    env=env,
                    title="Meson install",
                    dry_run=args.dry_run,
                )
                validate_staged_payload(root, dry_run=args.dry_run)

        if args.runtime:
            if not args.install and not args.dry_run:
                print("warning: --runtime uses the current .install tree because --install is disabled.", file=sys.stderr)
            run_runtime_matrix(args, root, env)

    except ValidationError as exc:
        print(f"\nvalidation failed: {exc}", file=sys.stderr, flush=True)
        return 1

    elapsed = time.monotonic() - started
    section("Validation complete")
    print(f"{args.profile} validation passed in {elapsed:.1f}s.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
