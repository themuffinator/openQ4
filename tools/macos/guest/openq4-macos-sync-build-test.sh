#!/usr/bin/env bash
set -euo pipefail

contains_control_chars() {
    LC_ALL=C printf '%s' "$1" | LC_ALL=C grep -q '[[:cntrl:]]'
}

require_guest_home() {
    local home_value="${OPENQ4_GUEST_HOME:-${HOME:-}}"
    if [[ -z "${home_value}" ]]; then
        echo "HOME must be set or OPENQ4_GUEST_HOME must point to the macOS guest home directory." >&2
        exit 2
    fi
    if contains_control_chars "${home_value}"; then
        echo "OPENQ4_GUEST_HOME/HOME must not contain control characters." >&2
        exit 2
    fi
    case "${home_value}" in
        /*)
            ;;
        *)
            echo "OPENQ4_GUEST_HOME/HOME must be an absolute POSIX path: ${home_value}" >&2
            exit 2
            ;;
    esac
    case "${home_value}" in
        "/"|"."|".."|*\\*|*//*|*/./*|*/../*|*/.|*/..)
            echo "OPENQ4_GUEST_HOME/HOME must be a clean absolute POSIX path: ${home_value}" >&2
            exit 2
            ;;
    esac
    if [[ ! -d "${home_value}" ]]; then
        echo "macOS guest home directory does not exist: ${home_value}" >&2
        exit 2
    fi
    printf '%s\n' "${home_value%/}"
}

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This build/test script must run inside macOS." >&2
    exit 1
fi

GUEST_HOME="$(require_guest_home)"
export HOME="${GUEST_HOME}"

action="${1:-build}"
graphics_bridge="${OPENQ4_MACOS_GRAPHICS_BRIDGE:-opengl}"
openal_provider="${OPENQ4_MACOS_OPENAL_PROVIDER:-system}"
os_matrix_role="${OPENQ4_MACOS_OS_MATRIX_ROLE:-current-manual-signoff}"
stamp="${OPENQ4_MACOS_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"
RESULT_TOKEN_MAX_LENGTH=80

expand_guest_path() {
    case "$1" in
        "~")
            printf '%s\n' "${GUEST_HOME}"
            ;;
        "~/"*)
            printf '%s/%s\n' "${GUEST_HOME}" "${1#~/}"
            ;;
        *)
            printf '%s\n' "$1"
            ;;
    esac
}

workspace="$(expand_guest_path "${OPENQ4_GUEST_WORKSPACE:-${GUEST_HOME}/openq4-work}")"
repo="${workspace}/openQ4"
gamelibs="${repo}"
basepath="$(expand_guest_path "${OPENQ4_BASEPATH:-${workspace}/Quake4}")"

require_result_token() {
    local label="$1"
    local value="$2"

    case "${value}" in
        ""|[!A-Za-z0-9]*|*[!A-Za-z0-9._-]*)
            echo "Invalid ${label} '${value}'. Use letters, digits, dots, underscores, or dashes, starting with a letter or digit." >&2
            exit 2
            ;;
    esac
    if (( ${#value} > RESULT_TOKEN_MAX_LENGTH )); then
        echo "Invalid ${label} '${value}'. Use at most ${RESULT_TOKEN_MAX_LENGTH} characters." >&2
        exit 2
    fi
}

require_choice() {
    local label="$1"
    local value="$2"
    shift 2

    local choice
    for choice in "$@"; do
        if [[ "${value}" == "${choice}" ]]; then
            return
        fi
    done

    echo "Invalid ${label} '${value}'. Expected one of: $*" >&2
    exit 2
}

require_positive_integer() {
    local label="$1"
    local value="$2"
    local max="$3"

    case "${value}" in
        ""|*[!0-9]*)
            echo "Invalid ${label} '${value}'. Expected a positive integer no greater than ${max}." >&2
            exit 2
            ;;
    esac
    if (( ${#value} > ${#max} )); then
        echo "Invalid ${label} '${value}'. Expected a positive integer no greater than ${max}." >&2
        exit 2
    fi

    local number=$((10#${value}))
    if (( number < 1 || number > max )); then
        echo "Invalid ${label} '${value}'. Expected a positive integer no greater than ${max}." >&2
        exit 2
    fi
}

require_safe_guest_roots() {
    python3 - "${workspace}" "${basepath}" "${GUEST_HOME}" <<'PY'
import pathlib
import sys
import unicodedata

workspace_raw = sys.argv[1]
basepath_raw = sys.argv[2]
guest_home_raw = sys.argv[3]
workspace = pathlib.Path(workspace_raw).expanduser()
basepath = pathlib.Path(basepath_raw).expanduser()
home = pathlib.Path(guest_home_raw).resolve()
root = pathlib.Path("/")


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(2)


def require_absolute_after_expansion(label: str, raw_value: str, path: pathlib.Path) -> None:
    path_text = raw_value
    if any(ord(character) < 32 or ord(character) == 127 for character in path_text):
        fail(f"{label} must not contain control characters: {path_text!r}")
    if "\\" in path_text:
        fail(f"{label} must be a POSIX path without backslashes: {path_text}")
    if not path.is_absolute():
        fail(f"{label} must be an absolute path after ~ expansion: {path_text}")
    segments = path_text.strip("/").split("/") if path_text.strip("/") else []
    if any(segment in {"", ".", ".."} for segment in segments):
        fail(f"{label} must not contain dot or empty path segments: {path_text}")


def resolve(label: str, raw_value: str, path: pathlib.Path) -> pathlib.Path:
    require_absolute_after_expansion(label, raw_value, path)
    if str(path) in {"", ".", ".."}:
        fail(f"{label} must be an explicit directory path: {path}")
    if path.is_symlink():
        fail(f"{label} must not be a symlink: {path}")
    try:
        resolved = path.resolve(strict=False)
    except OSError as exc:
        fail(f"{label} could not be resolved safely: {path}: {exc}")
    if resolved in {root, home}:
        fail(f"{label} must not be the filesystem root or home directory: {resolved}")
    return resolved


def path_key(path: pathlib.Path) -> str:
    return unicodedata.normalize("NFC", path.as_posix().rstrip("/")).casefold()


def path_is_same_or_under(candidate: pathlib.Path, parent: pathlib.Path) -> bool:
    candidate_key = path_key(candidate)
    parent_key = path_key(parent)
    return candidate_key == parent_key or candidate_key.startswith(f"{parent_key}/")


def require_basepath_outside_reserved_workspace_children() -> None:
    reserved_children = ("openQ4", "openQ4-game", "incoming-quake4", "results")
    for child in reserved_children:
        reserved = workspace_resolved / child
        if path_key(basepath_resolved) == path_key(reserved):
            fail(f"OPENQ4_BASEPATH must not target reserved workflow directory {child}.")
        if path_is_same_or_under(basepath_resolved, reserved):
            fail(f"OPENQ4_BASEPATH must not live under reserved workflow directory {child}.")


workspace_resolved = resolve("OPENQ4_GUEST_WORKSPACE", workspace_raw, workspace)
basepath_resolved = resolve("OPENQ4_BASEPATH", basepath_raw, basepath)
if basepath_resolved == workspace_resolved:
    fail("OPENQ4_BASEPATH must not be the same as OPENQ4_GUEST_WORKSPACE.")
try:
    workspace_resolved.relative_to(basepath_resolved)
except ValueError:
    pass
else:
    fail("OPENQ4_BASEPATH must not contain OPENQ4_GUEST_WORKSPACE.")
require_basepath_outside_reserved_workspace_children()
PY
}

require_result_token "OPENQ4_MACOS_RUN_ID" "${stamp}"
require_choice "macOS workflow action" "${action}" build smoke renderer launcher signoff all
require_choice "OPENQ4_MACOS_GRAPHICS_BRIDGE" "${graphics_bridge}" opengl metal
require_choice "OPENQ4_MACOS_OPENAL_PROVIDER" "${openal_provider}" apple_framework system
require_choice "OPENQ4_MACOS_OS_MATRIX_ROLE" "${os_matrix_role}" current-manual-signoff current-hosted-ci-runner floor-candidate latest-public-macos
require_safe_guest_roots

results_root="${workspace}/results"
run_dir="${results_root}/${stamp}-${action}-${graphics_bridge}"

shell_quote() {
    python3 - "$1" <<'PY'
import shlex
import sys

print(shlex.quote(sys.argv[1]))
PY
}

resolve_under_repo() {
    local candidate="$1"
    python3 - "${repo}" "${candidate}" <<'PY'
import pathlib
import sys

repo = pathlib.Path(sys.argv[1]).resolve()
candidate = pathlib.Path(sys.argv[2])
if not candidate.is_absolute():
    candidate = repo / candidate
candidate = candidate.resolve()

try:
    candidate.relative_to(repo)
except ValueError:
    raise SystemExit(f"Path escapes the openQ4 repository: {candidate}")

print(candidate)
PY
}

require_safe_builddir() {
    local raw_builddir="$1"
    local resolved_builddir="$2"

    if [[ -e "${raw_builddir}" && -L "${raw_builddir}" ]]; then
        echo "OPENQ4_BUILDDIR must not be a symlink: ${raw_builddir}" >&2
        exit 2
    fi
    if [[ -e "${resolved_builddir}" && ! -d "${resolved_builddir}" ]]; then
        echo "OPENQ4_BUILDDIR must resolve to a directory: ${resolved_builddir}" >&2
        exit 2
    fi
    if [[ "${resolved_builddir}" == "${repo}" ]]; then
        echo "OPENQ4_BUILDDIR must not resolve to the openQ4 repository root." >&2
        exit 2
    fi

    local relative_builddir="${resolved_builddir#${repo}/}"
    local first_segment="${relative_builddir%%/*}"
    if [[ "${first_segment}" != ".tmp" && "${first_segment}" != builddir* ]]; then
        echo "OPENQ4_BUILDDIR must live under .tmp/ or use a builddir* name: ${resolved_builddir}" >&2
        exit 2
    fi

    case "${resolved_builddir}" in
        "${repo}/.git"|\
        "${repo}/.git/"*|\
        "${repo}/.install"|\
        "${repo}/.install/"*|\
        "${repo}/baseoq4"|\
        "${repo}/baseoq4/"*|\
        "${repo}/content"|\
        "${repo}/content/"*|\
        "${repo}/src"|\
        "${repo}/src/"*|\
        "${repo}/tools"|\
        "${repo}/tools/"*)
            echo "OPENQ4_BUILDDIR must not target source, content, tool, git, or staged runtime directories: ${resolved_builddir}" >&2
            exit 2
            ;;
    esac
}

require_empty_result_directory() {
    local candidate="$1"
    local existing_entry

    existing_entry="$(find "${candidate}" -mindepth 1 -print -quit)"
    if [[ -n "${existing_entry}" ]]; then
        echo "macOS result directory already exists and is not empty: ${candidate}" >&2
        echo "Use a fresh OPENQ4_MACOS_RUN_ID or remove the stale result directory before rerunning." >&2
        echo "First existing entry: ${existing_entry}" >&2
        exit 2
    fi
}

prepare_run_dir() {
    local log_path="${run_dir}/openq4-macos-workflow.log"

    if [[ -L "${results_root}" ]]; then
        echo "macOS results root must not be a symlink: ${results_root}" >&2
        exit 2
    fi
    if [[ -e "${results_root}" && ! -d "${results_root}" ]]; then
        echo "macOS results root must be a directory: ${results_root}" >&2
        exit 2
    fi
    mkdir -p "${results_root}"
    if [[ -L "${results_root}" || ! -d "${results_root}" ]]; then
        echo "macOS results root must be a real directory: ${results_root}" >&2
        exit 2
    fi

    if [[ -L "${run_dir}" ]]; then
        echo "macOS result directory must not be a symlink: ${run_dir}" >&2
        exit 2
    fi
    if [[ -e "${run_dir}" && ! -d "${run_dir}" ]]; then
        echo "macOS result directory must be a directory: ${run_dir}" >&2
        exit 2
    fi
    if [[ -d "${run_dir}" ]]; then
        require_empty_result_directory "${run_dir}"
    fi
    mkdir -p "${run_dir}"
    if [[ -L "${run_dir}" || ! -d "${run_dir}" ]]; then
        echo "macOS result directory must be a real directory: ${run_dir}" >&2
        exit 2
    fi
    require_empty_result_directory "${run_dir}"
    if [[ -L "${log_path}" || -d "${log_path}" ]]; then
        echo "macOS workflow log path must not be a symlink or directory: ${log_path}" >&2
        exit 2
    fi
    if [[ -e "${log_path}" && ! -f "${log_path}" ]]; then
        echo "macOS workflow log path must be a regular file: ${log_path}" >&2
        exit 2
    fi
}

find_case_insensitive_tree_collision() {
    local tree="$1"
    python3 - "${tree}" <<'PY'
import pathlib
import sys
import unicodedata

root = pathlib.Path(sys.argv[1])
seen = {}
for path in sorted(root.rglob("*")):
    rel = path.relative_to(root).as_posix()
    key = unicodedata.normalize("NFC", rel).casefold()
    previous = seen.get(key)
    if previous is not None and previous != rel:
        print(previous)
        print(rel)
        raise SystemExit(0)
    seen[key] = rel
PY
}

if [[ -x "${GUEST_HOME}/.local/bin/meson" ]]; then
    export PATH="${GUEST_HOME}/.local/bin:${PATH}"
    export OPENQ4_MESON="${OPENQ4_MESON:-${GUEST_HOME}/.local/bin/meson}"
fi

prepare_run_dir
exec > >(tee -a "${run_dir}/openq4-macos-workflow.log") 2>&1

host_arch() {
    case "$(uname -m)" in
        arm64|aarch64) printf 'arm64\n' ;;
        x86_64|amd64) printf 'x64\n' ;;
        *) uname -m ;;
    esac
}

host_lipo_arch() {
    case "$(uname -m)" in
        arm64|aarch64) printf 'arm64\n' ;;
        x86_64|amd64) printf 'x86_64\n' ;;
        *) uname -m ;;
    esac
}

git_commit_or_unavailable() {
    local path="$1"
    if git -C "${path}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git -C "${path}" rev-parse --verify HEAD 2>/dev/null || printf 'unavailable\n'
    else
        printf 'unavailable\n'
    fi
}

git_dirty_or_unavailable() {
    local path="$1"
    if ! git -C "${path}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        printf 'unavailable\n'
        return
    fi
    if [[ -n "$(git -C "${path}" status --porcelain 2>/dev/null)" ]]; then
        printf 'true\n'
    else
        printf 'false\n'
    fi
}

client_binary() {
    local arch
    arch="$(host_arch)"
    local preferred="${repo}/.install/openQ4-client_${arch}"
    if [[ -e "${preferred}" && -L "${preferred}" ]]; then
        echo "Staged macOS client must not be a symlink: ${preferred}" >&2
        return 1
    fi
    if [[ -f "${preferred}" && -x "${preferred}" ]]; then
        printf '%s\n' "${preferred}"
        return
    fi

    if [[ "${OPENQ4_ALLOW_CROSS_ARCH_CLIENT:-0}" != "1" ]]; then
        echo "Missing staged macOS client for host architecture ${arch}: ${repo}/.install/openQ4-client_${arch}" >&2
        echo "Set OPENQ4_ALLOW_CROSS_ARCH_CLIENT=1 only when intentionally validating Rosetta/cross-arch behavior." >&2
        return 1
    fi

    local candidate
    for candidate in "${repo}"/.install/openQ4-client_*; do
        if [[ -e "${candidate}" && -L "${candidate}" ]]; then
            echo "Staged macOS client must not be a symlink: ${candidate}" >&2
            return 1
        fi
        if [[ -f "${candidate}" && -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return
        fi
    done
}

dedicated_binary() {
    local arch
    arch="$(host_arch)"
    local preferred="${repo}/.install/openQ4-ded_${arch}"
    if [[ -e "${preferred}" && -L "${preferred}" ]]; then
        echo "Staged macOS dedicated server must not be a symlink: ${preferred}" >&2
        return 1
    fi
    if [[ -f "${preferred}" && -x "${preferred}" ]]; then
        printf '%s\n' "${preferred}"
        return
    fi
    echo "Missing staged macOS dedicated server for host architecture ${arch}: ${repo}/.install/openQ4-ded_${arch}" >&2
    return 1
}

count_q4base_pk4s() {
    local root="$1"
    find "${root}/q4base" -maxdepth 1 -type f \( -iname '*.pk4' \) | wc -l | tr -d '[:space:]'
}

validate_asset_basepath() {
    if [[ ! -d "${basepath}/q4base" ]]; then
        echo "Missing Quake 4 asset basepath: ${basepath}" >&2
        echo "Expected ${basepath}/q4base. Run host action Assets or set OPENQ4_BASEPATH." >&2
        exit 1
    fi
    if [[ -L "${basepath}" || -L "${basepath}/q4base" ]]; then
        echo "Quake 4 asset basepath must not be a symlink: ${basepath}" >&2
        exit 1
    fi

    local bad_entry
    bad_entry="$(find "${basepath}" \( -type l -o \( ! -type f -a ! -type d \) \) -print -quit)"
    if [[ -n "${bad_entry}" ]]; then
        echo "Quake 4 asset basepath contains a symlink or special file: ${bad_entry}" >&2
        exit 1
    fi
    bad_entry="$(find "${basepath}" \( -iname '.DS_Store' -o -name '._*' -o -iname '__MACOSX' -o -iname '.fseventsd' -o -iname '.Spotlight-V100' -o -iname '.Trashes' -o -name $'Icon\r' -o -iname '*.dSYM' \) -print -quit)"
    if [[ -n "${bad_entry}" ]]; then
        echo "Quake 4 asset basepath contains non-runtime macOS metadata/debug entry: ${bad_entry}" >&2
        exit 1
    fi

    local case_collision
    case_collision="$(find_case_insensitive_tree_collision "${basepath}")"
    if [[ -n "${case_collision}" ]]; then
        echo "Quake 4 asset basepath contains a case-insensitive path collision:" >&2
        printf '%s\n' "${case_collision}" >&2
        exit 1
    fi

    local pk4_count
    pk4_count="$(count_q4base_pk4s "${basepath}")"
    if [[ "${pk4_count}" == "0" ]]; then
        echo "Quake 4 asset basepath has no q4base PK4 files: ${basepath}/q4base" >&2
        exit 1
    fi

    echo "Validated Quake 4 asset basepath: ${basepath} (${pk4_count} q4base PK4 files)."
}

validate_staged_macos_payload() {
    require_repo

    local arch
    arch="$(host_arch)"
    local lipo_arch
    lipo_arch="$(host_lipo_arch)"
    local install_root="${repo}/.install"
    local game_dir="${install_root}/baseoq4"

    if [[ ! -d "${install_root}" ]]; then
        echo "Missing staged macOS install root: ${install_root}" >&2
        exit 1
    fi
    if [[ ! -d "${game_dir}" ]]; then
        echo "Missing staged macOS game directory: ${game_dir}" >&2
        exit 1
    fi

    local required_executables=(
        "${install_root}/openQ4-client_${arch}"
        "${install_root}/openQ4-ded_${arch}"
        "${game_dir}/game_${arch}.dylib"
    )
    local required_scripts=(
        "${install_root}/collect_macos_support_info.sh"
    )
    local required_files=(
        "${install_root}/openQ4.icns"
        "${install_root}/assets/splash/quake4_rt_bitmap_4001.bmp"
    )

    local path
    for path in "${required_executables[@]}"; do
        if [[ ! -f "${path}" || ! -x "${path}" ]]; then
            echo "Missing or non-executable staged macOS payload: ${path}" >&2
            exit 1
        fi
    done
    for path in "${required_scripts[@]}"; do
        if [[ ! -f "${path}" || ! -x "${path}" ]]; then
            echo "Missing or non-executable staged macOS support script: ${path}" >&2
            exit 1
        fi
    done
    for path in "${required_files[@]}"; do
        if [[ ! -f "${path}" ]]; then
            echo "Missing staged macOS support file: ${path}" >&2
            exit 1
        fi
    done
    if LC_ALL=C grep -q $'\r' "${install_root}/collect_macos_support_info.sh"; then
        echo "macOS staged support collector contains CRLF or carriage returns." >&2
        exit 1
    fi
    local support_token
    for support_token in \
        "OPENQ4_PACKAGE_ROOT" \
        "redact_text()" \
        "contains_control_chars()" \
        "Support package root must not contain control characters" \
        "Support output directory must not contain control characters" \
        ".XXXXXX.tar.gz.tmp" \
        "MAX_SUPPORT_ARCHIVE_BYTES" \
        "sanitize_text()" \
        "limit_stream_tail()" \
        "Support report output is limited to the final" \
        "write_bounded_report()" \
        "does not dump the environment" \
        "does not launch openQ4" \
        "does not copy retail q4base PK4 assets" \
        "truncated copy failed; source was not copied" \
        "COPYFILE_DISABLE=1 tar -czf" \
        "COPYFILE_DISABLE=1 tar -tzf" \
        "Support archive is empty or unreadable before publish" \
        "Support archive validation failed before publish" \
        "Support archive is too large before publish" \
        'ln "${ARCHIVE_TMP}" "${ARCHIVE_PATH}"'
    do
        if ! grep -Fq "${support_token}" "${install_root}/collect_macos_support_info.sh"; then
            echo "macOS staged support collector is missing marker: ${support_token}" >&2
            exit 1
        fi
    done
    local forbidden_support_token
    for forbidden_support_token in \
        "printenv" \
        "env >" \
        "set >" \
        "openQ4-client_arm64 >" \
        "openQ4-client_arm64 2>" \
        "openQ4-client_x64 >" \
        "openQ4-client_x64 2>" \
        "openQ4-client_x86 >" \
        "openQ4-client_x86 2>" \
        "openQ4-ded_arm64 >" \
        "openQ4-ded_arm64 2>" \
        "openQ4-ded_x64 >" \
        "openQ4-ded_x64 2>" \
        "openQ4-ded_x86 >" \
        "openQ4-ded_x86 2>" \
        "xattr -l" \
        "xattr -p" \
        "xattr -w" \
        "|| cat" \
        'tail -c "${max_bytes}" < "${source_path}" 2>/dev/null || cat'
    do
        if grep -Fq "${forbidden_support_token}" "${install_root}/collect_macos_support_info.sh"; then
            echo "macOS staged support collector contains forbidden privacy/no-launch pattern: ${forbidden_support_token}" >&2
            exit 1
        fi
    done

    local bad_entry
    bad_entry="$(find "${install_root}" \( -iname '.DS_Store' -o -name '._*' -o -iname '__MACOSX' -o -iname '.fseventsd' -o -iname '.Spotlight-V100' -o -iname '.Trashes' -o -name $'Icon\r' -o -name '*.dSYM' \) -print -quit)"
    if [[ -n "${bad_entry}" ]]; then
        echo "macOS staged install contains non-runtime metadata/debug entry: ${bad_entry}" >&2
        exit 1
    fi
    bad_entry="$(find "${install_root}" \( -type l -o \( ! -type f -a ! -type d \) \) -print -quit)"
    if [[ -n "${bad_entry}" ]]; then
        echo "macOS staged install contains a symlink or special file: ${bad_entry}" >&2
        exit 1
    fi

    local case_collision
    case_collision="$(find_case_insensitive_tree_collision "${install_root}")"
    if [[ -n "${case_collision}" ]]; then
        echo "macOS staged install contains a case-insensitive path collision:" >&2
        printf '%s\n' "${case_collision}" >&2
        exit 1
    fi

    local expected_root_binaries=(
        "${install_root}/openQ4-client_${arch}"
        "${install_root}/openQ4-ded_${arch}"
    )
    while IFS= read -r path; do
        local matched=0
        local expected_binary
        for expected_binary in "${expected_root_binaries[@]}"; do
            if [[ "${path}" == "${expected_binary}" ]]; then
                matched=1
                break
            fi
        done
        if [[ "${matched}" == "0" ]]; then
            echo "macOS staged install contains stale or mismatched root engine binary: ${path}" >&2
            exit 1
        fi
    done < <(find "${install_root}" -maxdepth 1 -type f \( -name 'openQ4-client_*' -o -name 'openQ4-ded_*' \) -print)

    if compgen -G "${game_dir}/game_*.dll" >/dev/null || compgen -G "${game_dir}/game_*.so" >/dev/null; then
        echo "macOS staged install contains non-dylib game modules." >&2
        exit 1
    fi

    # one game module serves game_sp and game_mp; the game-* sweep below also
    # rejects the retired split game-sp/game-mp modules of an older build
    local expected_modules=(
        "${game_dir}/game_${arch}.dylib"
    )
    while IFS= read -r path; do
        local matched=0
        local expected_module
        for expected_module in "${expected_modules[@]}"; do
            if [[ "${path}" == "${expected_module}" ]]; then
                matched=1
                break
            fi
        done
        if [[ "${matched}" == "0" ]]; then
            echo "macOS staged install contains stale or mismatched game module: ${path}" >&2
            exit 1
        fi
    done < <(find "${game_dir}" -maxdepth 1 -type f \( -name 'game_*.dylib' -o -name 'game-*.dylib' \) -print)

    if command -v lipo >/dev/null 2>&1; then
        for path in "${required_executables[@]}"; do
            if ! lipo -archs "${path}" | tr ' ' '\n' | grep -qx "${lipo_arch}"; then
                echo "Staged macOS binary architecture mismatch: ${path}; expected ${lipo_arch}" >&2
                lipo -archs "${path}" >&2 || true
                exit 1
            fi
        done
    fi

    echo "Validated staged macOS payload for ${arch}."
}

require_repo() {
    if [[ ! -d "${repo}" ]]; then
        echo "Missing guest source workspace: ${repo}" >&2
        echo "Run host action Sync first." >&2
        exit 1
    fi
}

build_openq4() {
    require_repo
    cd "${repo}"
    export OPENQ4_GAMELIBS_REPO="${gamelibs}"
    # Keep Meson subprojects (SDL3, GLEW, stb_vorbis) on the same macOS 11.0
    # floor as the main project's -mmacosx-version-min=11.0 flags.
    export MACOSX_DEPLOYMENT_TARGET=11.0

    local buildtype="${OPENQ4_BUILDTYPE:-debug}"
    local raw_builddir="${OPENQ4_BUILDDIR:-builddir}"
    local builddir="${raw_builddir}"
    local platform_backend="${OPENQ4_PLATFORM_BACKEND:-sdl3}"
    require_choice "OPENQ4_BUILDTYPE" "${buildtype}" plain debug debugoptimized release minsize custom
    require_choice "OPENQ4_PLATFORM_BACKEND" "${platform_backend}" sdl3 native
    builddir="$(resolve_under_repo "${builddir}")"
    require_safe_builddir "${raw_builddir}" "${builddir}"

    local openal_arch=""
    local openal_root=""
    if [[ "${openal_provider}" == "system" ]]; then
        case "$(uname -m)" in
            arm64) openal_arch="arm64" ;;
            x86_64) openal_arch="x64" ;;
            *)
                echo "Unsupported macOS architecture for OpenAL Soft: $(uname -m)" >&2
                exit 1
                ;;
        esac
        openal_root="${repo}/.tmp/openal-soft-macos-${openal_arch}"
        if [[ -d "${openal_root}" ]]; then
            bash tools/build/prepare_macos_openal_soft.sh \
                --verify-only \
                --architecture "${openal_arch}" \
                --deployment-target "${MACOSX_DEPLOYMENT_TARGET}" \
                --output-root "${openal_root}"
        else
            bash tools/build/prepare_macos_openal_soft.sh \
                --architecture "${openal_arch}" \
                --deployment-target "${MACOSX_DEPLOYMENT_TARGET}" \
                --output-root "${openal_root}"
        fi
        export PKG_CONFIG_PATH="${openal_root}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
    fi

    echo "Configuring openQ4 (${buildtype}, backend=${platform_backend}, macos_graphics_bridge=${graphics_bridge}, macos_openal_provider=${openal_provider})"
    bash tools/build/meson_setup.sh setup --wipe "${builddir}" . \
        --backend ninja \
        --buildtype="${buildtype}" \
        --wrap-mode=forcefallback \
        "-Dplatform_backend=${platform_backend}" \
        "-Dmacos_graphics_bridge=${graphics_bridge}" \
        "-Dmacos_openal_provider=${openal_provider}"

    echo "Compiling openQ4"
    bash tools/build/meson_setup.sh compile -C "${builddir}"

    echo "Staging openQ4 into .install"
    bash tools/build/meson_setup.sh install -C "${builddir}" --no-rebuild --skip-subprojects
    if [[ "${openal_provider}" == "system" ]]; then
        bash tools/build/prepare_macos_openal_soft.sh \
            --verify-only \
            --architecture "${openal_arch}" \
            --deployment-target "${MACOSX_DEPLOYMENT_TARGET}" \
            --output-root "${openal_root}" \
            --stage-install-dir .install
    fi
    validate_staged_macos_payload
}

run_smoke() {
    require_repo
    cd "${repo}"

    validate_asset_basepath

    local client
    if ! client="$(client_binary)"; then
        exit 1
    fi
    if [[ -z "${client}" || ! -x "${client}" ]]; then
        echo "Missing staged macOS client under ${repo}/.install. Run Build first." >&2
        exit 1
    fi

    local smoke_limit="${OPENQ4_SMOKE_LIMIT:-1}"
    local smoke_settle_frames="${OPENQ4_SMOKE_SETTLE_FRAMES:-10}"
    local smoke_sample_frames="${OPENQ4_SMOKE_SAMPLE_FRAMES:-10}"
    local smoke_timeout="${OPENQ4_SMOKE_TIMEOUT:-300}"
    require_positive_integer "OPENQ4_SMOKE_LIMIT" "${smoke_limit}" 100000
    require_positive_integer "OPENQ4_SMOKE_SETTLE_FRAMES" "${smoke_settle_frames}" 100000
    require_positive_integer "OPENQ4_SMOKE_SAMPLE_FRAMES" "${smoke_sample_frames}" 100000
    require_positive_integer "OPENQ4_SMOKE_TIMEOUT" "${smoke_timeout}" 86400

    echo "Running openQ4 macOS renderer smoke with ${client}"
    python3 tools/tests/renderer_gameplay_benchmark.py \
        --profile smoke \
        --limit "${smoke_limit}" \
        --settle-frames "${smoke_settle_frames}" \
        --sample-frames "${smoke_sample_frames}" \
        --timeout "${smoke_timeout}" \
        --output-dir "${run_dir}/renderer-smoke" \
        --basepath "${basepath}"
}

run_mp_smoke() {
    require_repo
    cd "${repo}"

    validate_asset_basepath

    local client
    if ! client="$(client_binary)"; then
        exit 1
    fi
    if [[ -z "${client}" || ! -x "${client}" ]]; then
        echo "Missing staged macOS client under ${repo}/.install. Run Build first." >&2
        exit 1
    fi

    local mp_smoke_settle_frames="${OPENQ4_MP_SMOKE_SETTLE_FRAMES:-10}"
    local mp_smoke_sample_frames="${OPENQ4_MP_SMOKE_SAMPLE_FRAMES:-10}"
    local mp_smoke_timeout="${OPENQ4_MP_SMOKE_TIMEOUT:-360}"
    local mp_smoke_port="${OPENQ4_MP_SMOKE_PORT:-28110}"
    local mp_smoke_client_delay="${OPENQ4_MP_SMOKE_CLIENT_DELAY:-12}"
    local mp_smoke_client_delay_frames="${OPENQ4_MP_SMOKE_CLIENT_DELAY_FRAMES:-480}"
    require_positive_integer "OPENQ4_MP_SMOKE_SETTLE_FRAMES" "${mp_smoke_settle_frames}" 100000
    require_positive_integer "OPENQ4_MP_SMOKE_SAMPLE_FRAMES" "${mp_smoke_sample_frames}" 100000
    require_positive_integer "OPENQ4_MP_SMOKE_TIMEOUT" "${mp_smoke_timeout}" 86400
    require_positive_integer "OPENQ4_MP_SMOKE_PORT" "${mp_smoke_port}" 65535
    require_positive_integer "OPENQ4_MP_SMOKE_CLIENT_DELAY" "${mp_smoke_client_delay}" 3600
    require_positive_integer "OPENQ4_MP_SMOKE_CLIENT_DELAY_FRAMES" "${mp_smoke_client_delay_frames}" 100000

    echo "Running openQ4 macOS multiplayer smoke with ${client}"
    python3 tools/tests/renderer_gameplay_benchmark.py \
        --profile smoke \
        --cases mp-q4dm1-listen \
        --tiers auto \
        --maxfps 240 \
        --swap-intervals 0 \
        --display-modes windowed \
        --shadow-presets default \
        --settle-frames "${mp_smoke_settle_frames}" \
        --sample-frames "${mp_smoke_sample_frames}" \
        --timeout "${mp_smoke_timeout}" \
        --mp-port "${mp_smoke_port}" \
        --mp-client-delay "${mp_smoke_client_delay}" \
        --mp-client-delay-frames "${mp_smoke_client_delay_frames}" \
        --output-dir "${run_dir}/renderer-mp-smoke" \
        --basepath "${basepath}"
}

run_renderer_matrix() {
    require_repo
    cd "${repo}"

    local renderer_timeout="${OPENQ4_RENDERER_TIMEOUT:-180}"
    require_positive_integer "OPENQ4_RENDERER_TIMEOUT" "${renderer_timeout}" 86400

    echo "Running macOS-facing renderer validation matrix"
    python3 tools/tests/renderer_validation_matrix.py \
        --tiers auto,gl41 \
        --cases renderer-gbuffer-selftest,renderer-cluster-grid-selftest,renderer-shadow-planner-selftest,renderer-shadow-projected-diagnostic,renderer-modern-visible-selftest,shader-library-gl41,tier-auto,tier-gl41 \
        --timeout "${renderer_timeout}" \
        --output-dir "${run_dir}/renderer-matrix"
}

append_command_report() {
    local report="$1"
    local title="$2"
    shift 2

    {
        echo
        echo "## ${title}"
        echo '```text'
        "$@" 2>&1 || true
        echo '```'
    } >> "${report}"
}

append_staged_binary_arch_report() {
    local report="$1"
    local arch
    arch="$(host_arch)"
    local install_root="${repo}/.install"
    local game_dir="${install_root}/baseoq4"
    local required=(
        "${install_root}/openQ4-client_${arch}"
        "${install_root}/openQ4-ded_${arch}"
        "${game_dir}/game_${arch}.dylib"
    )
    local path

    {
        echo
        echo "## Staged Binary Architectures"
        echo '```text'
        if command -v lipo >/dev/null 2>&1; then
            for path in "${required[@]}"; do
                printf '%s: ' "${path}"
                lipo -archs "${path}" 2>&1 || true
            done
        else
            echo "lipo was not found."
        fi
        echo '```'
    } >> "${report}"
}

install_launcher() {
    require_repo
    mkdir -p "${GUEST_HOME}/Desktop"
    local launcher="${GUEST_HOME}/Desktop/openQ4.command"
    if [[ -L "${launcher}" || -d "${launcher}" || ( -e "${launcher}" && ! -f "${launcher}" ) ]]; then
        echo "Refusing to replace unsafe macOS launcher path: ${launcher}" >&2
        exit 1
    fi
    local client
    if ! client="$(client_binary)"; then
        exit 1
    fi
    if [[ -z "${client}" || ! -x "${client}" ]]; then
        echo "Missing staged macOS client under ${repo}/.install. Run Build first." >&2
        exit 1
    fi
    local install_dir_q client_q basepath_q
    install_dir_q="$(shell_quote "${repo}/.install")"
    client_q="$(shell_quote "${client}")"
    basepath_q="$(shell_quote "${basepath}")"

    local launcher_tmp
    launcher_tmp="$(mktemp "${GUEST_HOME}/Desktop/openQ4.command.XXXXXX")"
    cat > "${launcher_tmp}" <<EOF
#!/usr/bin/env bash
cd ${install_dir_q}
exec ${client_q} +set fs_basepath ${basepath_q} "\$@"
EOF
    chmod +x "${launcher_tmp}"
    mv -f "${launcher_tmp}" "${launcher}"
    echo "Installed macOS launcher: ${launcher}"
}

write_signoff_report() {
    require_repo
    local report="${run_dir}/macos-runtime-signoff.md"
    if [[ -L "${report}" || -d "${report}" || ( -e "${report}" && ! -f "${report}" ) ]]; then
        echo "Refusing to replace unsafe macOS signoff report path: ${report}" >&2
        exit 1
    fi
    local report_tmp
    report_tmp="$(mktemp "${run_dir}/macos-runtime-signoff.md.XXXXXX")"
    local client
    client="$(client_binary 2>/dev/null || true)"
    local dedicated
    dedicated="$(dedicated_binary 2>/dev/null || true)"
    local openq4_commit
    openq4_commit="$(git_commit_or_unavailable "${repo}")"
    local openq4_dirty
    openq4_dirty="$(git_dirty_or_unavailable "${repo}")"
    local gamelibs_commit
    gamelibs_commit="$(git_commit_or_unavailable "${gamelibs}")"
    local gamelibs_dirty
    gamelibs_dirty="$(git_dirty_or_unavailable "${gamelibs}")"

    {
        echo "# macOS Runtime Signoff"
        echo
        echo "- Date (UTC): $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        echo "- Host: $(hostname)"
        echo "- Architecture: $(uname -m)"
        echo "- Architecture policy: arm64-only preview release matrix"
        echo "- OS matrix role: ${os_matrix_role}"
        echo "- Graphics bridge: ${graphics_bridge}"
        echo "- OpenAL provider: ${openal_provider}"
        echo "- Build directory: ${OPENQ4_BUILDDIR:-builddir}"
        echo "- openQ4 commit: ${openq4_commit}"
        echo "- openQ4 dirty: ${openq4_dirty}"
        echo "- \`openQ4-game\` commit: ${gamelibs_commit}"
        echo "- \`openQ4-game\` dirty: ${gamelibs_dirty}"
        echo "- Asset basepath: ${basepath}"
        echo "- Client: ${client:-not found}"
        echo "- Dedicated server: ${dedicated:-not found}"
        echo "- Results: ${run_dir}"
        echo
        echo "## Automated Evidence"
        echo "- [x] Bridge-specific build and staged install completed."
        echo "- [x] Staged macOS payload integrity checks completed."
        echo "- [x] Quake 4 asset basepath validation completed."
        echo "- [x] Renderer smoke profile completed with retail Quake 4 assets."
        echo "- [x] Multiplayer listen-server smoke completed with retail Quake 4 assets."
        echo "- [x] MP game module path is present in the staged payload."
        echo "- [x] macOS-facing renderer validation matrix completed."
        echo "- [x] Desktop launcher was written for Finder/Terminal launch checks."
        echo "- [x] Package layout contract is self-contained app: data in Contents/Resources/baseoq4 and signed game modules in Contents/Frameworks."
        echo "- Renderer smoke output: ${run_dir}/renderer-smoke"
        echo "- Renderer MP smoke output: ${run_dir}/renderer-mp-smoke"
        echo "- Renderer matrix output: ${run_dir}/renderer-matrix"
        echo "- Workflow log: ${run_dir}/openq4-macos-workflow.log"
        echo
        echo "## Manual Hardware Checklist"
        echo "- [ ] Launch openQ4 from Finder or the Desktop launcher and enter a single-player map."
        echo "- [ ] Launch openQ4.app from the mounted signed/notarized DMG or final release image and enter a single-player map."
        echo "- [ ] Drag only openQ4.app to /Applications or another user-writable location, launch it there, and enter a single-player map."
        echo "- [ ] Copy the whole package payload to a user-writable location and verify the loose client, dedicated server, and support collector still use the sibling app runtime."
        echo "- [ ] Confirm the copied app contains baseoq4 data under Contents/Resources and the signed game module under Contents/Frameworks, with no adjacent baseoq4 duplicate."
        echo "- [ ] Launch the app executable from Terminal with an unrelated working directory."
        echo "- [ ] Confirm fs_basepath, fs_cdpath, and fs_savepath in logs for Finder/copied package and Terminal launches."
        echo "- [ ] Confirm Gatekeeper assessment for signed/notarized DMGs, or record unsigned/unnotarized approval friction for development archives."
        echo "- [ ] Verify keyboard text entry, console toggle, mouse-look, clicks, and wheel input."
        echo "- [ ] Verify at least one SDL game controller, including hotplug and rumble when hardware supports it."
        echo "- [ ] Verify audio output, volume changes, and at least one device switch or reconnect."
        echo "- [ ] Verify windowed, fullscreen, selected-display, and HiDPI/Retina behavior on attached displays."
        echo "- [ ] Verify the matching OpenGL or Metal bridge package path in actual gameplay, not only at the main menu."
        echo "- [ ] Launch multiplayer, load the mp/q4dm1 listen-server path, confirm the game module loads as game_mp, connect a local client, and exit cleanly."
        echo "- [ ] Launch the dedicated server binary, load an MP server configuration far enough to initialize the game module as game_mp, then shut it down cleanly."
        echo "- [ ] Record any input, audio, display, renderer, package, Gatekeeper, or crash issues with logs from this results directory."
    } > "${report_tmp}"

    append_command_report "${report_tmp}" "Xcode And SDK" bash -lc 'xcode="$(xcodebuild -version 2>/dev/null | tr "\n" " " | sed "s/[[:space:]]*$//")"; [ -n "${xcode}" ] || xcode="not found"; printf "Xcode: %s\n" "${xcode}"; sdk="$(xcrun --sdk macosx --show-sdk-version 2>/dev/null || true)"; [ -n "${sdk}" ] || sdk="not found"; printf "macOS SDK: %s\n" "${sdk}"; sdk_path="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || true)"; [ -n "${sdk_path}" ] || sdk_path="not found"; printf "macOS SDK path: %s\n" "${sdk_path}"'
    append_command_report "${report_tmp}" "macOS Version" sw_vers
    append_command_report "${report_tmp}" "Kernel" uname -a
    append_command_report "${report_tmp}" "Hardware" system_profiler SPHardwareDataType
    append_command_report "${report_tmp}" "Displays" system_profiler SPDisplaysDataType
    append_command_report "${report_tmp}" "Audio Devices" system_profiler SPAudioDataType
    append_command_report "${report_tmp}" "USB Devices" system_profiler SPUSBDataType
    append_command_report "${report_tmp}" "Bluetooth Devices" system_profiler SPBluetoothDataType
    append_command_report "${report_tmp}" "Staged Payload" find "${repo}/.install" -maxdepth 2 -print
    append_staged_binary_arch_report "${report_tmp}"
    mv -f "${report_tmp}" "${report}"

    echo "macOS runtime signoff report: ${report}"
}

run_signoff() {
    build_openq4
    run_smoke
    run_mp_smoke
    run_renderer_matrix
    install_launcher
    write_signoff_report
}

case "${action}" in
    build)
        build_openq4
        ;;
    smoke)
        run_smoke
        ;;
    renderer)
        run_renderer_matrix
        ;;
    launcher)
        install_launcher
        ;;
    signoff)
        run_signoff
        ;;
    all)
        run_signoff
        ;;
    *)
        echo "Unknown action '${action}'. Expected build, smoke, renderer, launcher, signoff, or all." >&2
        exit 2
        ;;
esac

echo "openQ4 macOS workflow '${action}' complete. Guest results: ${run_dir}"
