#!/usr/bin/env python3
"""Regression checks for Linux ARM64 CI coverage."""

import json
import os
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def read_game_libs(relative_path: str) -> str:
    return (GAME_LIBS_ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_count(haystack: str, needle: str, expected: int, context: str) -> None:
    actual = haystack.count(needle)
    if actual != expected:
        raise AssertionError(f"Expected {expected} occurrence(s) of {needle!r} in {context}, found {actual}")


def matrix_entry_for_artifact(source: str, artifact_suffix: str) -> str:
    marker = f"artifact_suffix: {artifact_suffix}"
    require_count(source, marker, 1, "commit validation Wayland matrix")
    marker_offset = source.index(marker)
    entry_start = source.rfind("\n          - ", 0, marker_offset)
    entry_end = source.find("\n          - ", marker_offset)
    if entry_start == -1:
        raise AssertionError(f"Unable to find matrix-entry start for {artifact_suffix!r}")
    if entry_end == -1:
        entry_end = source.find("\n\n    steps:", marker_offset)
    if entry_end == -1:
        raise AssertionError(f"Unable to find matrix-entry end for {artifact_suffix!r}")
    return source[entry_start + 1 : entry_end]


def validate_push_workflow() -> None:
    source = read(".github/workflows/push-verification.yml")

    require(source, "Linux ARM64 Push Verification", "push verification workflow")
    require(source, "os: ubuntu-24.04-arm", "push verification workflow")
    require(source, "artifact_name: linux-arm64", "push verification workflow")
    require(source, "runtime_smoke: true", "push verification workflow")
    require(source, "startsWith(matrix.os, 'ubuntu-')", "push verification Linux dependency gate")
    require(source, "binutils", "push verification readelf dependency")
    require(source, "xvfb", "push verification runtime display dependency")
    require(source, "libgl1-mesa-dri", "push verification software GL runtime dependency")
    require(source, "libglx-mesa0", "push verification GLX runtime dependency")
    require(source, "xvfb-run -a bash tools/validation/validate_push.sh", "push verification runtime smoke")
    require(
        source,
        'runtime_cases="renderer-foundation-selftests,renderer-pbr-visible-selftest,renderer-cluster-grid-selftest,renderer-default-safety-selftest"',
        "push verification runtime smoke",
    )
    require(source, '--runtime-cases "${runtime_cases}"', "push verification runtime smoke")
    require(source, "sdl3-x11-display-diagnostics", "push verification X11 display diagnostics case")
    require(source, "OPENQ4_FORCE_X11=1 xvfb-run -a python tools/tests/renderer_validation_matrix.py", "push verification OPENQ4_FORCE_X11 runtime smoke")
    require(source, "--cases sdl3-force-x11-display-diagnostics", "push verification OPENQ4_FORCE_X11 runtime smoke")
    require(source, "--runtime-skip-official-pak-validation", "push verification assetless runtime smoke")
    require(source, "LIBGL_ALWAYS_SOFTWARE=1", "push verification software GL runtime smoke")
    require(source, "SDL_VIDEO_DRIVER=x11 SDL_VIDEODRIVER=x11", "push verification runtime display override")
    require(source, "push-${{ matrix.artifact_name }}-renderer-validation", "push verification renderer report artifact")
    require(source, "path: .tmp/renderer-validation", "push verification renderer report artifact")
    require(source, "include-hidden-files: true", "push verification hidden staging/report artifacts")
    require(source, "python tools/tests/linux_arm64_ci_coverage.py", "push script-smoke regression check")


def validate_commit_workflow() -> None:
    source = read(".github/workflows/commit-validation.yml")

    require(source, "Linux ARM64 Commit Validation", "commit validation workflow")
    require(source, "runs-on: ubuntu-24.04-arm", "commit validation workflow")
    require(source, "binutils", "commit validation readelf dependency")
    require(source, "xvfb", "commit validation runtime display dependency")
    require(source, "libgl1-mesa-dri", "commit validation software GL runtime dependency")
    require(source, "libglx-mesa0", "commit validation GLX runtime dependency")
    require(source, "xvfb-run -a bash tools/validation/validate_pr.sh", "commit validation runtime smoke")
    require(
        source,
        "--runtime-cases renderer-foundation-selftests,renderer-pbr-visible-selftest,renderer-cluster-grid-selftest,renderer-default-safety-selftest",
        "commit validation runtime smoke",
    )
    require(source, "sdl3-x11-display-diagnostics", "commit validation X11 display diagnostics case")
    require(source, "OPENQ4_FORCE_X11=1 xvfb-run -a python tools/tests/renderer_validation_matrix.py", "commit validation OPENQ4_FORCE_X11 runtime smoke")
    require(source, "--cases sdl3-force-x11-display-diagnostics", "commit validation OPENQ4_FORCE_X11 runtime smoke")
    require(source, "--runtime-skip-official-pak-validation", "commit validation assetless runtime smoke")
    require(source, "LIBGL_ALWAYS_SOFTWARE=1", "commit validation software GL runtime smoke")
    require(source, "SDL_VIDEO_DRIVER=x11 SDL_VIDEODRIVER=x11", "commit validation runtime display override")
    require(source, "commit-linux-arm64-renderer-validation", "commit validation renderer report artifact")
    require(source, "path: .tmp/renderer-validation", "commit validation renderer report artifact")
    require(source, "include-hidden-files: true", "commit validation hidden report artifact")
    require(source, "python tools/tests/linux_arm64_ci_coverage.py", "commit script-smoke regression check")
    require(source, "linux-wayland:", "commit validation native Wayland job")
    require(source, "Linux ${{ matrix.arch_label }} ${{ matrix.compiler_label }} Wayland ${{ matrix.libdecor_label }} Commit Validation", "commit validation native Wayland job")
    require(source, "CC: ${{ matrix.cc }}", "commit validation compiler selection")
    require(source, "CXX: ${{ matrix.cxx }}", "commit validation C++ compiler selection")
    arm64_clang = matrix_entry_for_artifact(source, "arm64-clang-no-libdecor-wayland-only")
    for token in (
        "arch_label: ARM64",
        "runner: ubuntu-24.04-arm",
        "expected_uname: aarch64",
        "compiler_label: Clang",
        "cc: clang",
        "cxx: clang++",
        "libdecor_label: no-libdecor",
        'disable_libdecor: "1"',
        "linux_x11: disabled",
    ):
        require(arm64_clang, token, "commit validation ARM64 Clang Wayland-only matrix entry")

    arm64_gcc = matrix_entry_for_artifact(source, "arm64-libdecor")
    for token in (
        "arch_label: ARM64",
        "runner: ubuntu-24.04-arm",
        "expected_uname: aarch64",
        "compiler_label: GCC",
        "cc: gcc",
        "cxx: g++",
        "libdecor_label: libdecor",
    ):
        require(arm64_gcc, token, "commit validation ARM64 GCC libdecor matrix entry")

    require(
        source,
        "  linux-arm64:\n"
        "    name: Linux ARM64 Commit Validation\n"
        "    runs-on: ubuntu-24.04-arm\n"
        "    timeout-minutes: 150\n"
        "    env:\n"
        "      OPENQ4_GAMELIBS_REPO: ${{ github.workspace }}\n"
        "      CC: gcc\n"
        "      CXX: g++",
        "commit validation native ARM64 GCC dedicated job",
    )
    require(source, "weston --backend=headless-backend.so", "commit validation native Wayland compositor")
    require(source, "SDL_VIDEO_DRIVER=wayland", "commit validation native Wayland video driver")
    require(source, "SDL_VIDEODRIVER=wayland", "commit validation native Wayland legacy video driver")
    require(source, "OPENQ4_WAYLAND_DISABLE_LIBDECOR=1", "commit validation libdecor opt-out matrix")
    require(source, "OPENQ4_WAYLAND_PREFER_LIBDECOR=1", "commit validation preferred libdecor matrix")
    require(source, "OPENQ4_WAYLAND_SYNC_WINDOW_OPS=1", "commit validation sync-window matrix")
    require(source, "sdl3-wayland-window-lifecycle", "commit validation Wayland window lifecycle case")
    require(source, "sdl3-wayland-window-stress", "commit validation Wayland window stress case")
    require(source, "sdl3-wayland-mouse-capture", "commit validation Wayland mouse capture case")
    require(source, "sdl3-wayland-mouse-capture-stress", "commit validation Wayland mouse capture stress case")
    require(source, "sdl3-wayland-display-diagnostics", "commit validation Wayland display diagnostics case")
    require(source, "commit-linux-wayland-${{ matrix.artifact_suffix }}-renderer-validation", "commit validation Wayland renderer report artifact")


def validate_linux_hardening_contract() -> None:
    meson = read("meson.build")
    validator = read("tools/validation/openq4_validate.py")

    require(meson, "linux_compile_hardening_args", "Linux compile hardening")
    require(meson, "-fstack-protector-strong", "Linux compile hardening")
    require(meson, "-D_FORTIFY_SOURCE=2", "Linux compile hardening")
    require(meson, "linux_link_hardening_args", "Linux link hardening")
    require(meson, "-Wl,-z,relro", "Linux link hardening")
    require(meson, "-Wl,-z,now", "Linux link hardening")
    require(meson, "-Wl,-z,noexecstack", "Linux link hardening")
    require(meson, "linux_executable_pie", "Linux executable PIE")
    require(meson, "pie: linux_executable_pie", "Linux executable PIE")
    require(meson, "'Linux executable PIE': linux_executable_pie", "Meson hardening summary")

    require(validator, "validate_linux_binary_hardening", "Linux staged hardening validation")
    require(validator, "readelf", "Linux staged hardening validation")
    require(validator, "GNU_RELRO", "Linux staged hardening validation")
    require(validator, "GNU_STACK", "Linux staged hardening validation")
    require(validator, "BIND_NOW", "Linux staged hardening validation")
    require(validator, "Linux binary is not PIE/ET_DYN", "Linux staged hardening validation")
    require(validator, '"arm64": ("ELF64", "AArch64")', "Linux staged ARM64 ELF validation")
    require(validator, '["--wide", "--dyn-syms"]', "Linux dynamic module export validation")
    require(validator, 'fields[4] in {"GLOBAL", "WEAK", "UNIQUE"}', "Linux public symbol binding validation")
    require(validator, 'fields[5] in {"DEFAULT", "PROTECTED"}', "Linux public symbol visibility validation")
    require(validator, 'expected_game_exports = {"GetGameAPI", "openQ4_Mem_GetModuleStats"}', "Linux exact game module export set")
    require(validator, "len(public_symbols) == len(expected_game_exports)", "no extra public Linux module export")
    require(validator, 'symbol[:3] == ("FUNC", "GLOBAL", "DEFAULT")', "Linux game module export binding validation")
    require(validator, "Linux game module must expose exactly the GLOBAL/DEFAULT/FUNC GetGameAPI", "Linux game module export validation")


def validate_runtime_flags() -> None:
    renderer = read("tools/tests/renderer_validation_matrix.py")
    runner = read("tools/validation/openq4_validate.py")

    require(renderer, '"id": "sdl3-wayland-window-lifecycle"', "renderer validation Wayland window lifecycle case")
    require(renderer, "native Wayland SDL3 window lifecycle smoke", "renderer validation Wayland window lifecycle case")
    require(renderer, '"SDL3: current video driver: wayland"', "renderer validation Wayland driver assertion")
    require(renderer, '"SDL3: native Wayland window state after fullscreen change"', "renderer validation Wayland fullscreen assertion")
    require(renderer, '"SDL3: native Wayland window state after windowed change"', "renderer validation Wayland windowed assertion")
    require(renderer, '"+vid_restart"', "renderer validation window lifecycle restart command")
    require(renderer, '"id": "sdl3-wayland-window-stress"', "renderer validation Wayland window stress case")
    require(renderer, "native Wayland SDL3 repeated window/fullscreen transition stress", "renderer validation Wayland window stress case")
    require(renderer, '"r_windowWidth",\n                "1280"', "renderer validation Wayland window stress width change")
    require(renderer, '"id": "sdl3-wayland-mouse-capture"', "renderer validation Wayland mouse capture case")
    require(renderer, "native Wayland SDL3 relative mouse capture smoke", "renderer validation Wayland mouse capture case")
    require(renderer, '"+sdl3MouseCaptureDiagnostics"', "renderer validation mouse capture command")
    require(renderer, '"SDL3 mouse capture diagnostics after activate:"', "renderer validation mouse capture activation assertion")
    require(renderer, '"relative=on"', "renderer validation relative mouse assertion")
    require(renderer, '"captured=yes"', "renderer validation relative mouse assertion")
    require(renderer, '"id": "sdl3-wayland-mouse-capture-stress"', "renderer validation Wayland mouse capture stress case")
    require(renderer, "native Wayland SDL3 repeated relative mouse capture stress", "renderer validation Wayland mouse capture stress case")
    require(renderer, '"SDL3 mouse capture diagnostics: begin repeat=4"', "renderer validation mouse capture repeat assertion")
    require(renderer, '"SDL3 mouse capture diagnostics: iteration 4/4"', "renderer validation mouse capture repeat assertion")
    require(renderer, '"id": "sdl3-wayland-display-diagnostics"', "renderer validation Wayland display diagnostics case")
    require(renderer, "native Wayland SDL3 display diagnostics smoke", "renderer validation Wayland display diagnostics case")
    require(renderer, '"+listDisplays"', "renderer validation display diagnostics command")
    require(renderer, '"SDL3: detected"', "renderer validation display enumeration assertion")
    require(renderer, '"contentScale"', "renderer validation display scale assertion")
    require(renderer, '"orientation"', "renderer validation display orientation assertion")
    require(renderer, '"selected display"', "renderer validation selected-display assertion")
    require(renderer, '"id": "sdl3-x11-display-diagnostics"', "renderer validation X11 display diagnostics case")
    require(renderer, "SDL3 X11/Xvfb fallback display diagnostics smoke", "renderer validation X11 display diagnostics case")
    require(renderer, '"SDL3: current video driver: x11"', "renderer validation X11 driver assertion")
    require(renderer, '"id": "sdl3-force-x11-display-diagnostics"', "renderer validation OPENQ4_FORCE_X11 display diagnostics case")
    require(renderer, "openQ4 XWayland fallback diagnostics smoke", "renderer validation OPENQ4_FORCE_X11 display diagnostics case")
    require(renderer, '"OPENQ4_FORCE_X11=1"', "renderer validation OPENQ4_FORCE_X11 environment assertion")
    require(renderer, '"videoDriver": "x11"', "renderer validation X11 driver-specific metadata")
    require(renderer, '"videoDriver": "wayland"', "renderer validation Wayland driver-specific metadata")
    require(renderer, "filter_driver_specific_cases", "renderer validation default driver-specific filter")
    require(renderer, "--skip-official-pak-validation", "renderer validation matrix assetless option")
    require(renderer, '"fs_validateOfficialPaks"', "renderer validation matrix startup cvar")
    require(renderer, '"g_allowAssetlessStartup"', "renderer validation matrix assetless game guard")
    require(renderer, "skipOfficialPakValidation", "renderer validation matrix report metadata")
    require(runner, "--runtime-skip-official-pak-validation", "validation profile assetless option")
    require(runner, '"--skip-official-pak-validation"', "validation profile renderer handoff")


def validate_renderer_selftest_object_lifetime() -> None:
    planner = read("src/renderer/ModernShadowPlanner.cpp")
    executor = read("src/renderer/ModernGLExecutor.cpp")

    for source, context in (
        (planner, "modern shadow planner self-tests"),
        (executor, "modern GL executor self-tests"),
    ):
        reject(source, "memset( lightDefs, 0, sizeof( lightDefs ) );", context)
    reject(planner, "memset( &lightDef, 0, sizeof( lightDef ) );", "projected shadow diagnostic self-test")


def validate_assetless_renderer_bootstrap() -> None:
    source = read("src/renderer/RenderSystem_init.cpp")

    require(source, 'FindMaterial( "_default", false )', "renderer default material stock lookup")
    require(source, "using generated internal fallback", "renderer default material assetless fallback")
    require(source, 'FindMaterial( "_default" )', "renderer default material generated fallback lookup")
    require(source, "_default material fallback not available", "renderer default material fallback fatal guard")


def validate_assetless_game_bootstrap() -> None:
    for module in ("game",):
        game_local = read_game_libs(f"src/{module}/Game_local.cpp")
        sys_cvar = read_game_libs(f"src/{module}/gamesys/SysCvar.cpp")
        sys_cvar_header = read_game_libs(f"src/{module}/gamesys/SysCvar.h")

        require(game_local, 'FindEntityDefDict( "aas_types", false )', f"{module} stock AAS lookup")
        require(game_local, "g_allowAssetlessStartup.GetBool()", f"{module} assetless AAS guard")
        require(game_local, "continuing without AAS because g_allowAssetlessStartup is enabled", f"{module} assetless AAS log")
        require(sys_cvar, '"g_allowAssetlessStartup"', f"{module} assetless startup cvar")
        require(sys_cvar_header, "g_allowAssetlessStartup", f"{module} assetless startup cvar declaration")


def validate_release_note() -> None:
    source = read("docs/dev/release-completion.md")

    require(source, "Linux builds and Wayland validation are harder to regress", "release completion notes")
    require(source, "OPENQ4_WAYLAND_DISABLE_LIBDECOR=1", "release completion notes")
    require(source, "Linux ARM64 is now covered by normal CI", "release completion notes")
    require(source, "assetless renderer startup smoke", "release completion notes")
    require(source, "AAS declarations", "release completion notes")


def validate_linux_arm64_release_claim_policy() -> None:
    workflow = read(".github/workflows/manual-release.yml")
    evidence = read("docs/dev/linux-arm64-signoff-evidence.md")
    evidence_template = read("docs/dev/linux-arm64-first-class-evidence.toml")
    evidence_helper = read("tools/build/verify_linux_arm64_release_evidence.py")

    input_start = workflow.index("      linux_arm64_support_tier:")
    input_end = workflow.index("      macos_support_tier:", input_start)
    support_input = workflow[input_start:input_end]
    require(support_input, "default: preview", "manual release Linux ARM64 support input")
    require(support_input, "- preview", "manual release Linux ARM64 support input")
    require(support_input, "- first-class", "manual release Linux ARM64 support input")
    require(support_input, "linux_arm64_evidence_ref:", "manual release Linux ARM64 evidence input")
    require(
        support_input,
        "generate_linux_arm64_evidence_candidate:",
        "manual release Linux ARM64 candidate input",
    )

    for token in (
        "OPENQ4_LINUX_ARM64_SUPPORT_TIER",
        "OPENQ4_LINUX_ARM64_EVIDENCE_SHA",
        "OPENQ4_ARM64_EVIDENCE_CANDIDATE",
        "OPENQ4_RELEASE_VERSION_TAG",
        'linux_arm64_support_tier == "first-class"',
        "Resolve accepted Linux ARM64 evidence",
        "docs/dev/linux-arm64-first-class-evidence.toml",
        "docs/dev/linux-arm64-evidence/stock-sp-report.json",
        "docs/dev/linux-arm64-evidence/dedicated-report.json",
        "verify_linux_arm64_release_evidence.py validate",
        "verify_linux_arm64_release_evidence.py verify",
        "verify_linux_arm64_release_evidence.py write-candidate",
        "verify_release_asset_set.py",
        "--print-archive-sha256",
        "--stock-sp-report",
        "--dedicated-report",
        "--expected-openq4-commit",
        "--expected-openq4-game-commit",
        "--expected-release-version",
        "--expected-version-tag",
        "--expected-release-tag",
        "--expected-package-filename",
        "Verify first-class Linux ARM64 evidence against staged and packaged bytes",
        "Write Linux ARM64 first-class evidence candidate",
        "Upload Linux ARM64 first-class evidence candidate",
        "Revalidate accepted Linux ARM64 archive after artifact download",
        "linux_arm64_archive_sha256",
        "generate_linux_arm64_evidence_candidate",
        "linux_arm64_evidence_ref",
        "source_date_epoch",
        "SOURCE_DATE_EPOCH",
        "stale_user_preview_claims",
        "selected_curated_release_notes",
        '" (preview)" if linux_arm64_support_tier == "preview" else ""',
        'f"Linux ARM64{linux_arm64_label_suffix}"',
        '"-preview" if linux_arm64_support_tier == "preview" else ""',
        "linux-arm64-preview.tar.xz",
        "linux-arm64-preview-debugsymbols.tar.xz",
        "## Linux ARM64 Support",
        "linux_arm64_preview_claim_pattern",
        "linux[[:space:]_-]+arm64",
        "stale_linux_arm64_assets",
        "gh release delete-asset",
        "linux_arm64_support_tier=preview",
        "linux_arm64_release_reason",
        "Linux ARM64 first-class evidence candidate",
        "Publishing manual releases must be dispatched from refs/heads/main",
        "Linux ARM64 evidence candidates must be dispatched from a pushed branch",
        "must descend from the current origin/main",
        "inputs.generate_linux_arm64_evidence_candidate == false",
    ):
        require(workflow, token, "manual release Linux ARM64 support policy")
    reject(workflow, "required_record_fields", "legacy free-form Linux ARM64 evidence parsing")
    reject(workflow, "linux-arm64-signoff-status:start", "legacy Markdown Linux ARM64 status parsing")

    heredoc_start_marker = "          python - <<'PY' >> \"$GITHUB_OUTPUT\"\n"
    heredoc_end_marker = "\n          PY\n"
    heredoc_start = workflow.index(heredoc_start_marker) + len(heredoc_start_marker)
    heredoc_end = workflow.index(heredoc_end_marker, heredoc_start)
    embedded_python = textwrap.dedent(workflow[heredoc_start:heredoc_end])
    compile(embedded_python, ".github/workflows/manual-release.yml:release-matrix", "exec")

    with tempfile.TemporaryDirectory(prefix="openq4-linux-arm64-release-policy-") as temp_dir:
        temp_root = Path(temp_dir)
        preview_docs = {
            temp_root / "README.md": "preview Linux ARM64\n",
            temp_root / "docs" / "user" / "getting-started.md": (
                "Linux ARM64 packages are preview builds\n"
            ),
            temp_root / "assets" / "release" / "README.html": (
                "Linux ARM64 Preview Notes\n"
            ),
        }
        for path, contents in preview_docs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")

        base_environment = os.environ.copy()
        base_environment["OPENQ4_MACOS_SUPPORT_TIER"] = "preview"
        base_environment["OPENQ4_RELEASE_VERSION_TAG"] = "9.9.9-test"
        base_environment["OPENQ4_ARM64_EVIDENCE_CANDIDATE"] = "false"

        preview_environment = base_environment.copy()
        preview_environment["OPENQ4_LINUX_ARM64_SUPPORT_TIER"] = "preview"
        preview_result = subprocess.run(
            [sys.executable, "-c", embedded_python],
            cwd=temp_root,
            env=preview_environment,
            check=False,
            capture_output=True,
            text=True,
        )
        if preview_result.returncode != 0:
            raise AssertionError(
                "preview Linux ARM64 release matrix failed:\n"
                + preview_result.stdout
                + preview_result.stderr
            )
        preview_matrix_line = next(
            (
                line
                for line in preview_result.stdout.splitlines()
                if line.startswith("release_matrix=")
            ),
            "",
        )
        require(preview_matrix_line, "release_matrix=", "preview Linux ARM64 release matrix")
        preview_matrix = json.loads(preview_matrix_line.split("=", 1)[1])["include"]
        preview_linux = {
            entry["binary_arch"]: entry
            for entry in preview_matrix
            if entry["platform"] == "linux"
        }
        if preview_linux["x64"]["package_suffix"] != "":
            raise AssertionError("Linux x64 release must not inherit the ARM64 preview suffix")
        if preview_linux["arm64"]["package_suffix"] != "-preview":
            raise AssertionError("preview Linux ARM64 archive must use the -preview suffix")
        if preview_linux["arm64"]["label"] != "Linux ARM64 (preview)":
            raise AssertionError("preview Linux ARM64 matrix label is not explicit")

        candidate_environment = base_environment.copy()
        candidate_environment["OPENQ4_LINUX_ARM64_SUPPORT_TIER"] = "preview"
        candidate_environment["OPENQ4_ARM64_EVIDENCE_CANDIDATE"] = "true"
        candidate_result = subprocess.run(
            [sys.executable, "-c", embedded_python],
            cwd=temp_root,
            env=candidate_environment,
            check=False,
            capture_output=True,
            text=True,
        )
        if candidate_result.returncode != 0:
            raise AssertionError(
                "Linux ARM64 evidence-candidate matrix failed:\n"
                + candidate_result.stdout
                + candidate_result.stderr
            )
        candidate_matrix_line = next(
            (
                line
                for line in candidate_result.stdout.splitlines()
                if line.startswith("release_matrix=")
            ),
            "",
        )
        require(
            candidate_matrix_line,
            "release_matrix=",
            "Linux ARM64 evidence-candidate matrix",
        )
        candidate_matrix = json.loads(candidate_matrix_line.split("=", 1)[1])["include"]
        if len(candidate_matrix) != 1:
            raise AssertionError("evidence-candidate mode must schedule exactly one build")
        candidate_entry = candidate_matrix[0]
        if candidate_entry["platform"] != "linux" or candidate_entry["binary_arch"] != "arm64":
            raise AssertionError("evidence-candidate mode scheduled a non-ARM64 Linux build")
        if candidate_entry["label"] != "Linux ARM64 first-class evidence candidate":
            raise AssertionError("evidence-candidate matrix label is not explicit")
        if candidate_entry["package_suffix"] != "":
            raise AssertionError("evidence-candidate archive must use the first-class filename")

        invalid_candidate_environment = candidate_environment.copy()
        invalid_candidate_environment["OPENQ4_LINUX_ARM64_SUPPORT_TIER"] = "first-class"
        invalid_candidate_result = subprocess.run(
            [sys.executable, "-c", embedded_python],
            cwd=temp_root,
            env=invalid_candidate_environment,
            check=False,
            capture_output=True,
            text=True,
        )
        if invalid_candidate_result.returncode == 0:
            raise AssertionError("evidence-candidate mode accepted the first-class tier")
        require(
            invalid_candidate_result.stdout + invalid_candidate_result.stderr,
            "non-publishing preview-tier builds",
            "first-class evidence-candidate tier rejection",
        )

        first_class_environment = base_environment.copy()
        first_class_environment["OPENQ4_LINUX_ARM64_SUPPORT_TIER"] = "first-class"
        missing_evidence_result = subprocess.run(
            [sys.executable, "-c", embedded_python],
            cwd=temp_root,
            env=first_class_environment,
            check=False,
            capture_output=True,
            text=True,
        )
        if missing_evidence_result.returncode == 0:
            raise AssertionError("first-class Linux ARM64 release accepted no evidence commit")
        require(
            missing_evidence_result.stdout + missing_evidence_result.stderr,
            "require a resolved immutable accepted-evidence commit",
            "missing Linux ARM64 accepted-evidence commit rejection",
        )

        first_class_environment["OPENQ4_LINUX_ARM64_EVIDENCE_SHA"] = "e" * 40

        selected_release_notes = (
            temp_root / "docs" / "dev" / "releases" / "v9.9.9-test.md"
        )
        selected_release_notes.parent.mkdir(parents=True, exist_ok=True)
        selected_release_notes.write_text(
            "# Synthetic release\n\nThe linux-arm64-preview archive remains available.\n",
            encoding="utf-8",
        )

        stale_notes_result = subprocess.run(
            [sys.executable, "-c", embedded_python],
            cwd=temp_root,
            env=first_class_environment,
            check=False,
            capture_output=True,
            text=True,
        )
        if stale_notes_result.returncode == 0:
            raise AssertionError(
                "first-class Linux ARM64 release accepted stale curated preview notes"
            )
        require(
            stale_notes_result.stdout + stale_notes_result.stderr,
            "user-facing preview claims still remain",
            "stale curated Linux ARM64 preview-note rejection",
        )
        # macOS is a preview too, so the scan skips "preview" when macOS or
        # Apple follows it, but a preview claim that still reaches Linux ARM64
        # must keep failing.
        for path in preview_docs:
            path.write_text("first-class Linux ARM64 support\n", encoding="utf-8")
        selected_release_notes.write_text(
            "# Synthetic release\n\nLinux ARM64 and macOS packages are preview builds.\n",
            encoding="utf-8",
        )
        shared_preview_result = subprocess.run(
            [sys.executable, "-c", embedded_python],
            cwd=temp_root,
            env=first_class_environment,
            check=False,
            capture_output=True,
            text=True,
        )
        if shared_preview_result.returncode == 0:
            raise AssertionError(
                "first-class Linux ARM64 release accepted a preview claim shared with macOS"
            )
        require(
            shared_preview_result.stdout + shared_preview_result.stderr,
            "user-facing preview claims still remain",
            "shared Linux ARM64/macOS preview-claim rejection",
        )

        for path in preview_docs:
            path.write_text(
                "first-class Linux ARM64 support beside preview Apple Silicon/arm64 macOS\n",
                encoding="utf-8",
            )
        selected_release_notes.write_text(
            "# Synthetic release\n\nLinux ARM64 first-class support. The preview macOS "
            "packages stay unsigned.\n",
            encoding="utf-8",
        )

        accepted_result = subprocess.run(
            [sys.executable, "-c", embedded_python],
            cwd=temp_root,
            env=first_class_environment,
            check=False,
            capture_output=True,
            text=True,
        )
        if accepted_result.returncode != 0:
            raise AssertionError(
                "first-class Linux ARM64 release rejected resolved evidence commit:\n"
                + accepted_result.stdout
                + accepted_result.stderr
            )
        require(
            accepted_result.stdout,
            "linux_arm64_support_tier=first-class",
            "accepted Linux ARM64 first-class release matrix",
        )
        accepted_matrix_line = next(
            (
                line
                for line in accepted_result.stdout.splitlines()
                if line.startswith("release_matrix=")
            ),
            "",
        )
        require(accepted_matrix_line, "release_matrix=", "accepted Linux ARM64 release matrix")
        accepted_matrix = json.loads(accepted_matrix_line.split("=", 1)[1])["include"]
        accepted_linux_arm64 = next(
            entry
            for entry in accepted_matrix
            if entry["platform"] == "linux" and entry["binary_arch"] == "arm64"
        )
        if accepted_linux_arm64["package_suffix"] != "":
            raise AssertionError("first-class Linux ARM64 archive retained the preview suffix")
        if accepted_linux_arm64["label"] != "Linux ARM64":
            raise AssertionError("first-class Linux ARM64 matrix label retained preview wording")

    require(evidence, "Required Two-Pass Workflow", "Linux ARM64 signoff evidence")
    require(evidence, "linux_arm64_evidence_ref", "Linux ARM64 signoff evidence")
    require(evidence, "generate_linux_arm64_evidence_candidate", "Linux ARM64 signoff evidence")
    require(evidence, "same triggering openQ4 commit", "Linux ARM64 signoff evidence")
    require(evidence, "Fast-forward the exact tested candidate commit", "Linux ARM64 signoff evidence")
    require(evidence, "creates no tag or GitHub release", "Linux ARM64 signoff evidence")
    require(evidence, "SOURCE_DATE_EPOCH", "Linux ARM64 signoff evidence")
    require(evidence, "hard failure", "Linux ARM64 signoff evidence")
    require(evidence, "physical 64-bit little-endian AArch64", "Linux ARM64 signoff evidence")
    require(evidence, "desktop OpenGL compatibility driver", "Linux ARM64 signoff evidence")
    require(evidence, "representative single-player gameplay", "Linux ARM64 signoff evidence")
    require(evidence, "stock multiplayer map", "Linux ARM64 signoff evidence")
    require(evidence, "stock map with the packaged dedicated server", "Linux ARM64 signoff evidence")
    require(evidence, "Verify keyboard, relative mouse, controller", "Linux ARM64 signoff evidence")
    require(evidence, "SHA-256", "Linux ARM64 signoff evidence")
    require(evidence, "schema version 3", "Linux ARM64 signoff evidence")
    require(
        evidence,
        "human audible playback",
        "Linux ARM64 manual audio evidence boundary",
    )
    for token in (
        "schema_version = 3",
        'status = "pending"',
        "[review]",
        "[runtime_reports]",
        "[candidate]",
        "[sha256]",
        'openq4_commit = "pending"',
        'openq4_game_commit = "pending"',
        'package_filename = "pending"',
        'archive = "pending"',
        'client = "pending"',
        'dedicated = "pending"',
        'game = "pending"',
        'stock_sp = "pending"',
    ):
        require(evidence_template, token, "Linux ARM64 evidence TOML template")
    for token in (
        "GIT_SHA_RE",
        "SHA256_RE",
        "ROOT_KEYS",
        "REVIEW_KEYS",
        "CANDIDATE_KEYS",
        "SHA256_KEYS",
        "RUNTIME_REPORT_KEYS",
        "RuntimeReportPaths",
        "validate_evidence_manifest",
        "validate_runtime_reports",
        "read_runtime_report",
        "verify_release_evidence",
        "write_candidate",
        "files_equal",
        "require_regular_file",
        "MAX_MANIFEST_BYTES",
        "MAX_RUNTIME_REPORT_BYTES",
        "require_review_value",
        "REVIEW_PLACEHOLDER_VALUES",
        "REVIEW_MINIMUM_LENGTHS",
        "--print-archive-sha256",
    ):
        require(evidence_helper, token, "Linux ARM64 candidate evidence verifier")
    require(
        read("tools/validation/openq4_validate.py"),
        '"linux_arm64_release_evidence.py"',
        "validation runner ARM64 evidence fixture wiring",
    )
    for validation_workflow in (
        ".github/workflows/commit-validation.yml",
        ".github/workflows/push-verification.yml",
    ):
        require(
            read(validation_workflow),
            "tools/tests/linux_arm64_release_evidence.py",
            f"ARM64 evidence fixture wiring in {validation_workflow}",
        )

    for path, token in (
        ("README.md", "preview Linux ARM64"),
        ("docs/user/getting-started.md", "Linux ARM64 packages are preview builds"),
        ("assets/release/README.html", "Linux ARM64 Preview Notes"),
        ("docs/dev/releases/v0.8.1.md", "Linux ARM64 packages remain preview"),
        (".github/scripts/announce-release-discord.mjs", "Linux ARM64 preview"),
    ):
        require(read(path), token, f"Linux ARM64 preview claim in {path}")

    for path in (
        "BUILDING.md",
        "docs/dev/platform-support.md",
        "docs/dev/linux-arm64-cross-compilation.md",
    ):
        require(
            read(path),
            "linux-arm64-signoff-evidence.md",
            f"Linux ARM64 evidence link in {path}",
        )


def validate_no_duplicate_jobs() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")

    require_count(push, "windows-build:", 1, "push verification workflow")
    require_count(push, "Linux ARM64 Push Verification", 1, "push verification workflow")
    require_count(commit, "Linux ARM64 Commit Validation", 1, "commit validation workflow")
    require_count(commit, "linux-wayland:", 1, "commit validation workflow")


def main() -> None:
    validate_push_workflow()
    validate_commit_workflow()
    validate_linux_hardening_contract()
    validate_runtime_flags()
    validate_renderer_selftest_object_lifetime()
    validate_assetless_renderer_bootstrap()
    validate_assetless_game_bootstrap()
    validate_release_note()
    validate_linux_arm64_release_claim_policy()
    validate_no_duplicate_jobs()
    print("linux_arm64_ci_coverage: ok")


if __name__ == "__main__":
    main()
