#!/usr/bin/env python3
"""Static checks for macOS package robustness without Finder/runtime testing."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def validate_runtime_startup_error() -> None:
    compat = read("src/sys/macos/macos_compat.mm")

    for token in (
        "Sys_ErrorIfMacOSAppBundleRuntimeIncomplete",
        "Sys_ErrorIfMacOSAppBundlePackageRootIncomplete",
        "Sys_SelectMacOSAppBundleRuntimeRoots",
        "Sys_GetSiblingSelfContainedAppRuntimeRoots",
        "OpenQ4BundleRuntimeMissingTitle",
        "OpenQ4BundleRuntimeMissingBody",
        "Expected self-contained app contract: data in Contents/Resources/baseoq4 and signed game modules in Contents/Frameworks",
        'resourceDirectory.AppendPath( "Resources" )',
        'frameworkDirectory.AppendPath( "Frameworks" )',
        "Sys_GetAppBundlePackageRootFromExecutableDirectory",
        "Sys_IsMacOSAppBundleDirectoryName",
        'static const char appBundleSuffix[] = ".app";',
        "idStr::Icmp( suffixStart, appBundleSuffix )",
        "Sys_LocalizedMacOSPackageRootString",
        "localizedStringForKey",
        'table:@"OpenQ4PackageRoot"',
        "OpenQ4PackageRootMissingTitle",
        "OpenQ4PackageRootMissingBody",
        "openQ4.app adjacent package root is incomplete",
        "This legacy package layout needs openQ4.app, baseoq4/, openQ4-client_<arch>, and openQ4-ded_<arch> together",
        "Current self-contained packages support moving only openQ4.app to /Applications",
        "Expected adjacent package-root contract: openQ4.app, loose binaries, and baseoq4/ together",
        "Package root: %s",
        "App path: %s",
        "Missing or unusable entries: %s",
        "Expected runtime architecture: %s",
        "Existing mismatched runtime entries: %s",
        "none detected",
        "openQ4.app",
        "BASE_GAMEDIR",
        "openQ4-client_%s",
        "openQ4-ded_%s",
        "%s/game_%s.dylib",
        "%s/game_%s.dll",
        "%s/game_%s.so",
        "Sys_AppendMacOSPackageRootIssue",
        "Sys_AppendAlternateMacOSPackageRootEntries",
        "Sys_ExecutableFileExists",
        "Sys_RequireMacOSPackageRootExecutable",
        "Sys_RequireMacOSPackageRootDirectory",
        '"missing"',
        '"not executable"',
        '"not a regular file"',
    ):
        require(compat, token, "macOS adjacent package-root startup diagnostic")
    reject(
        compat,
        'appName.Icmp( "openQ4.app" )',
        "macOS app-bundle runtime detection should tolerate renamed .app bundles",
    )


def validate_game_module_package_root_probe() -> None:
    filesystem = read("src/framework/FileSystem.cpp")
    compat = read("src/sys/macos/macos_compat.mm")

    find_dll = function_body(
        filesystem,
        "void idFileSystemLocal::FindDLL( const char *name, char _dllPath[ MAX_OSPATH ], bool updateChecksum ) {",
    )
    for token in (
        "Sys_GetPackageRootDirectory",
        "Sys_GetGameModuleRootDirectory",
        "moduleSearchRoots",
    ):
        require(find_dll, token, "FindDLL trusted macOS game-module probe")

    package_root = function_body(compat, "bool Sys_GetPackageRootDirectory( char *packageRoot, int packageRootSize ) {")
    require(
        package_root,
        "Sys_SelectMacOSAppBundleRuntimeRoots",
        "macOS Sys_GetPackageRootDirectory app-bundle runtime-root selection",
    )
    module_root = function_body(compat, "bool Sys_GetGameModuleRootDirectory( char *moduleRootPath, int moduleRootSize ) {")
    require(module_root, "Sys_SelectMacOSAppBundleRuntimeRoots", "macOS trusted module-root selection")


def validate_app_bundle_cd_path() -> None:
    compat = read("src/sys/macos/macos_compat.mm")
    posix = read("src/sys/posix/posix_main.cpp")

    cd_path = function_body(compat, "const char *Sys_DefaultCDPath( void ) {")
    for token in (
        "Sys_GetPackageRootDirectory",
        "cdpath = packageRoot",
        "return cdpath.c_str()",
        "return Posix_Cwd()",
    ):
        require(cd_path, token, "macOS app-bundle CD-path selection")

    require(
        posix,
        "#if !defined( MACOS_X )\n// Only relevant when specified on the command line.",
        "non-macOS POSIX CD-path implementation guard",
    )
    posix_cd_path = function_body(posix, "const char *Sys_DefaultCDPath( void ) {")
    require(posix_cd_path, "return Posix_Cwd()", "non-macOS POSIX CD-path fallback")


def validate_package_metadata_and_archive_guards() -> None:
    package = read("tools/build/package_nightly.py")

    for token in (
        "MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME",
        "OpenQ4PackageRoot.strings",
        "MACOS_PACKAGE_ROOT_ERROR_STRINGS",
        "English",
        "French",
        "write_macos_package_root_error_strings",
        "validate_macos_package_root_error_bytes",
        "macOS archive missing {locale} localized package-root error strings",
        'name.endswith(f".lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}")',
        "Contents/Resources/English.lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}",
        "Contents/Resources/French.lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}",
    ):
        require(package, token, "macOS app localized startup-error package metadata")

    for token in (
        '".DS_Store"',
        '"__MACOSX"',
        '"._"',
        '".dSYM"',
        "validate_no_package_symlinks",
        "validate_no_macos_metadata_artifacts",
        "validate_no_macos_casefold_path_collisions",
        "validate_macos_archive_metadata_member_size",
        "macos_package_suffix_from_name",
        "contains duplicate key",
        "contains unexpected header key",
        "validate_macos_manifest_archive_filename",
        "contains unsafe archive filename",
        "package_suffix",
        "runtime_archive",
        "runtime_archive_name=archive_path.name",
        "symbol_archive",
        "contains duplicate binary entry",
        "contains unexpected binary entries",
        "is missing binary entries",
        "has invalid sha256",
        "has invalid size",
        "has invalid macho_uuid",
        "dsym is",
        "macOS archive path must not be a symlink",
        "Unsupported macOS archive format for validation",
        "macOS archive contains symlink entry",
        "ord(character) < 32",
        "macOS archive contains case-insensitive duplicate entries",
        "MAX_MACOS_RUNTIME_ARCHIVE_MEMBERS",
        "macOS archive contains too many members",
        "MAX_MACOS_RUNTIME_ARCHIVE_TOTAL_BYTES",
        "macOS archive total expanded size is too large",
        "MACOS_TAR_ARCHIVE_READ_MODES",
        '"tar.gz": "r:gz"',
        '"tar.xz": "r:xz"',
        "read_macos_zip_member",
        "macOS archive is not a valid {archive_format} archive",
        "macOS archive is not a valid zip archive",
        "MACOS_SUPPORT_INFO_REQUIRED_TOKENS",
        "MACOS_SUPPORT_INFO_FORBIDDEN_TOKENS",
        "MAX_MACOS_SUPPORT_INFO_SCRIPT_BYTES",
        "validate_macos_support_info_script_bytes",
        "validate_macos_package_support_collector",
        "macOS archive support collector",
        "macOS package support collector",
        "forbidden privacy/no-launch pattern",
        "contains_control_chars()",
        "sanitize_text()",
        "limit_stream_tail()",
        "Support report output is limited to the final",
        "Support package root must not contain control characters",
        "Support output directory must not contain control characters",
        ".XXXXXX.tar.gz.tmp",
        "except (FileNotFoundError, RuntimeError) as exc",
        "does not launch openQ4",
        "does not copy retail q4base PK4 assets",
        "truncated copy failed; source was not copied",
        "openQ4-client_x64 >",
        "openQ4-ded_x64 >",
        "|| cat",
        'tail -c "${max_bytes}" < "${source_path}" 2>/dev/null || cat',
        "validate_macos_package_root_engine_binaries",
        "macOS package contains stale or mismatched root engine binaries",
        "macOS archive contains stale or mismatched root engine binaries",
        "MAX_MACOS_SYMBOL_ARCHIVE_MEMBERS",
        "macOS symbol archive contains too many members",
        "MAX_MACOS_SYMBOL_ARCHIVE_TOTAL_BYTES",
        "macOS symbol archive total expanded size is too large",
        "macOS symbol archive is not a valid xz-compressed tar archive",
    ):
        require(package, token, "macOS archive hygiene guards")


def validate_release_path_policy() -> None:
    manual_release = read(".github/workflows/manual-release.yml")
    package_policy = read("docs/dev/macos-package-layout-and-release-policy.md")
    building = read("BUILDING.md")
    platform_support = read("docs/dev/platform-support.md")

    for token in (
        "First-class macOS releases require signed/notarized DMGs",
        "macos_support_tier",
        "first-class",
        "Preview macOS unsigned/unnotarized tar.gz release artifacts enabled as fallback output",
    ):
        require(manual_release, token, "manual release first-class macOS DMG gate")

    for source, context in (
        (package_policy, "macOS package layout and release policy"),
        (building, "build documentation"),
        (platform_support, "platform support documentation"),
    ):
        require(source, "signed/notarized DMGs", context)
        require(source, "`-unsigned.tar.gz`", context)
        require(source, "preview", context)


def validate_support_info_path_resolution() -> None:
    collector = read("tools/macos/collect_macos_support_info.sh")
    support_doc = read("docs/user/macos-support-data.md")

    for token in (
        'case "$0" in',
        'SCRIPT_DIR=$(CDPATH= cd "${script_dir}" && pwd -P)',
        "runtime_arch_token()",
        "Detected runtime architecture token: %s",
        "prepare_package_root()",
        "Support package root must not be a symlink",
        "Support package root must be an existing directory",
        "prepare_output_target()",
        "HOME_DIR=${HOME:-}",
        "umask 077",
        "ARCHIVE_TMP",
        "Support archive target must not be a symlink or directory",
        "Support archive target already exists",
        "Support archive target appeared while collecting data",
        "MAX_SUPPORT_TEXT_BYTES",
        "MAX_CRASH_REPORT_BYTES",
        "MAX_SUPPORT_ARCHIVE_BYTES",
        "sanitize_text()",
        "limit_stream_tail()",
        "Support report output is limited to the final",
        "write_bounded_report()",
        "Source file was larger than",
        "write_openq4_log_candidate_paths()",
        "HOME was not set; home-scoped openq4.log paths were skipped.",
        "HOME was not set; home-scoped openq4.log files were skipped.",
        "HOME was not set; the macOS DiagnosticReports directory could not be located.",
        "path_exists_for_inspection()",
        "Skipped symlinked source:",
        "copy_crash_report_if_safe()",
        "archive-safe names",
        "support collector does not follow symlinks",
        'COPYFILE_DISABLE=1 tar -czf "${ARCHIVE_TMP}"',
        'COPYFILE_DISABLE=1 tar -tzf "${ARCHIVE_TMP}"',
        "Support archive is empty or unreadable before publish",
        "Support archive validation failed before publish",
        "Support archive is too large before publish",
        'ln "${ARCHIVE_TMP}" "${ARCHIVE_PATH}"',
        'chmod 600 "${ARCHIVE_TMP}"',
        "package/path-resolution.txt",
        "Package root: %s",
        "App path: %s",
        "App executable path: %s",
        "Expected loose client path: %s",
        "Expected loose dedicated-server path: %s",
        "Expected embedded game-data path: %s",
        "Expected embedded game-module path: %s",
        "Expected log keys: fs_basepath, fs_cdpath, fs_savepath",
        "grep -E 'fs_(basepath|cdpath|savepath)",
        "No openq4.log files were found. fs_basepath, fs_cdpath, and fs_savepath values could not be copied without launching openQ4.",
        "does not launch openQ4",
    ):
        require(collector, token, "macOS support collector path-resolution report")
    reject(collector, "dirname --", "macOS support collector portable script directory resolution")
    reject(collector, 'mv "${ARCHIVE_TMP}" "${ARCHIVE_PATH}"', "macOS support collector no-clobber archive publish")

    for token in (
        "`package/path-resolution.txt`",
        "package root, app path, expected loose runtime paths",
        "`fs_basepath`, `fs_cdpath`, and `fs_savepath`",
        "without launching openQ4",
    ):
        require(support_doc, token, "macOS support data path-resolution documentation")


def validate_docs_plan_and_release_notes() -> None:
    plan = read("docs/dev/archive/2026-10-18/plans/2026-06-30-apple-support-no-macos-access.md")
    package_policy = read("docs/dev/macos-package-layout-and-release-policy.md")
    release_completion = read("docs/dev/release-completion.md")
    release_notes = read("docs/dev/releases/v0.6.5.md")

    for token in (
        "- [x] Add a localized, clear startup error when adjacent runtime files are",
        "- [x] Make the error name the expected adjacent package-root contract:",
        "- [x] Add static tests for package layout docs, app metadata, and error text",
        "- [x] Keep symlink, AppleDouble, `.DS_Store`, `__MACOSX`, debug bundle, and",
        "- [x] Keep signed/notarized DMG as the only first-class release path.",
        "- [x] Keep unsigned tarballs clearly named and documented as experimental",
        "- [x] Add support-info output showing package root, app path, `fs_basepath`,",
        "Phase 5 implementation status",
        "tools/tests/macos_package_robustness.py",
        "No macOS platform testing is required or claimed for Phase 5.",
    ):
        require(plan, token, "Phase 5 macOS no-platform-test implementation plan")

    for token in (
        "openQ4.app adjacent package root is incomplete",
        "Expected adjacent package-root contract: `openQ4.app`, loose binaries, and `baseoq4/` together",
        "Legacy adjacent packages need the app, loose binaries, and data together",
        "Current self-contained packages support moving only `openQ4.app` to `/Applications`",
        "`package/path-resolution.txt`",
    ):
        require(package_policy, token, "macOS package layout policy startup error and support path report")

    for source, context in (
        (release_completion, "release completion notes"),
        (release_notes, "curated release notes"),
    ):
        require(source, "localized macOS startup error", context)
        require(source, "renamed `.app`", context)
        require(source, "package/path-resolution.txt", context)


def validate_ci_wiring() -> None:
    local_runner = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    macos_debug = read(".github/workflows/macos-debug.yml")

    for source, context in (
        (local_runner, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "macos_package_robustness.py", context)

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "python tools/tests/macos_package_robustness.py", context)


def main() -> None:
    validate_runtime_startup_error()
    validate_game_module_package_root_probe()
    validate_app_bundle_cd_path()
    validate_package_metadata_and_archive_guards()
    validate_release_path_policy()
    validate_support_info_path_resolution()
    validate_docs_plan_and_release_notes()
    validate_ci_wiring()
    print("macos_package_robustness: ok")


if __name__ == "__main__":
    main()
