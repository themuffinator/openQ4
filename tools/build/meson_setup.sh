#!/usr/bin/env bash
set -euo pipefail

case "${BASH_SOURCE[0]}" in
    */*) script_dir="${BASH_SOURCE[0]%/*}" ;;
    *) script_dir=. ;;
esac
script_dir="$(CDPATH= cd "${script_dir}" && pwd)"
repo_root="$(CDPATH= cd "${script_dir}/../.." && pwd)"
default_builddir="${repo_root}/builddir"
sync_icons_script="${script_dir}/sync_icons.py"
check_staged_content_script="${script_dir}/check_staged_content_edits.py"
declare -a MESON_CMD=()
PYTHON_CMD=""
declare -a READ_ARRAY_RESULT=()

configure_macos_deployment_target() {
    local host_name=""
    host_name="$(uname -s 2>/dev/null || true)"
    [[ "${host_name}" == "Darwin" ]] || return 0

    if [[ -z "${MACOSX_DEPLOYMENT_TARGET:-}" ]]; then
        # Keep Meson subprojects and companion GameLibs on the same floor as
        # the main project's -mmacosx-version-min setting.
        export MACOSX_DEPLOYMENT_TARGET=11.0
    elif [[ ! "${MACOSX_DEPLOYMENT_TARGET}" =~ ^[0-9]+([.][0-9]+){1,2}$ ]]; then
        echo "MACOSX_DEPLOYMENT_TARGET must be a dotted macOS version, got '${MACOSX_DEPLOYMENT_TARGET}'." >&2
        exit 1
    fi

    echo "macOS deployment target: ${MACOSX_DEPLOYMENT_TARGET}"
}

resolve_meson_cmd() {
    local candidate=""
    local python_cmd=""

    if [[ -n "${OPENQ4_MESON:-}" ]]; then
        if [[ ! -x "${OPENQ4_MESON}" ]]; then
            echo "OPENQ4_MESON points to a missing or non-executable Meson: '${OPENQ4_MESON}'." >&2
            exit 1
        fi

        for candidate in python python3; do
            if python_cmd="$(command -v "${candidate}" 2>/dev/null)"; then
                PYTHON_CMD="${python_cmd}"
                MESON_CMD=("${OPENQ4_MESON}")
                return
            fi
        done

        echo "Python was not found. Install Python or ensure it is available on PATH." >&2
        exit 1
    fi

    for candidate in python python3; do
        if ! python_cmd="$(command -v "${candidate}" 2>/dev/null)"; then
            continue
        fi

        if [[ -z "${PYTHON_CMD}" ]]; then
            PYTHON_CMD="${python_cmd}"
        fi

        if "${python_cmd}" -c 'import mesonbuild.mesonmain' >/dev/null 2>&1; then
            PYTHON_CMD="${python_cmd}"
            MESON_CMD=("${python_cmd}" -m mesonbuild.mesonmain)
            return
        fi
    done

    if [[ -z "${PYTHON_CMD}" ]]; then
        echo "Python was not found. Install Python or ensure it is available on PATH." >&2
        exit 1
    fi

    if command -v meson >/dev/null 2>&1; then
        MESON_CMD=("$(command -v meson)")
        return
    fi

    echo "Meson was not found. Install it into the active Python environment or make 'meson' available on PATH." >&2
    exit 1
}

run_meson() {
    "${MESON_CMD[@]}" "$@"
}

read_line_array() {
    READ_ARRAY_RESULT=()
    local item=""

    while IFS= read -r item; do
        READ_ARRAY_RESULT+=("${item}")
    done
}

read_nul_array() {
    READ_ARRAY_RESULT=()
    local item=""

    while IFS= read -r -d '' item; do
        READ_ARRAY_RESULT+=("${item}")
    done
}

configure_macos_deployment_target
resolve_meson_cmd

test_meson_build_directory() {
    local build_dir="$1"
    [[ -f "${build_dir}/meson-private/coredata.dat" && -f "${build_dir}/build.ninja" ]]
}

get_compile_build_dir() {
    local build_dir="$default_builddir"
    local has_explicit=0
    local args=("$@")
    local i=0

    while (( i < ${#args[@]} )); do
        local arg="${args[$i]}"
        if [[ "${arg}" == "-C" && $((i + 1)) -lt ${#args[@]} ]]; then
            build_dir="${args[$((i + 1))]}"
            has_explicit=1
            break
        fi

        if [[ "${arg}" == -C* && "${arg}" != "-C" ]]; then
            build_dir="${arg:2}"
            has_explicit=1
            break
        fi

        ((i += 1))
    done

    "${PYTHON_CMD}" - "$build_dir" "$has_explicit" <<'PY'
import os
import sys

print(os.path.abspath(sys.argv[1]))
print(sys.argv[2])
PY
}

get_meson_build_option_value() {
    local build_dir="$1"
    local option_name="$2"
    local intro_options_path="${build_dir}/meson-info/intro-buildoptions.json"
    local cmd_line_path="${build_dir}/meson-private/cmd_line.txt"

    "${PYTHON_CMD}" - "$intro_options_path" "$cmd_line_path" "$option_name" <<'PY'
import configparser
import json
import os
import sys

intro_path, cmd_line_path, option_name = sys.argv[1:4]

if os.path.isfile(intro_path):
    with open(intro_path, "r", encoding="utf-8") as handle:
        options = json.load(handle)
    for option in options:
        if option.get("name") == option_name:
            value = option.get("value")
            if isinstance(value, bool):
                print("true" if value else "false")
            elif value is None:
                print("")
            else:
                print(str(value))
            raise SystemExit(0)

if os.path.isfile(cmd_line_path):
    parser = configparser.RawConfigParser()
    parser.read(cmd_line_path, encoding="utf-8")
    if parser.has_option("options", option_name):
        print(parser.get("options", option_name))
        raise SystemExit(0)

raise SystemExit(1)
PY
}

resolve_gamelibs_repo_path() {
    "${PYTHON_CMD}" - "${repo_root}" "${OPENQ4_GAMELIBS_REPO:-}" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
raw = sys.argv[2].strip()
repo = pathlib.Path(raw) if raw else root.parent / "openQ4-game"
print(repo.resolve().as_posix())
PY
}

test_gamelibs_stage_refresh_needed() {
    local build_dir="$1"
    test_meson_build_directory "${build_dir}" || return 1

    local build_engine=""
    local build_games=""
    build_engine="$(get_meson_build_option_value "${build_dir}" build_engine || true)"
    build_games="$(get_meson_build_option_value "${build_dir}" build_games || true)"
    if [[ "${build_engine}" != "true" && "${build_games}" != "true" ]]; then
        return 1
    fi

    local gamelibs_repo=""
    gamelibs_repo="$(resolve_gamelibs_repo_path)"
    local stage_root=""
    stage_root="$("${PYTHON_CMD}" "${script_dir}/gamelibs_stage_path.py" \
        --source-root "${repo_root}" --build-dir "${build_dir}")" || return 0
    local source_game_dirs=("${gamelibs_repo}/src/game" "${gamelibs_repo}/src/mpgame")
    local staged_game_dirs=("${stage_root}/src/game" "${stage_root}/src/mpgame")

    local directory_path=""
    for directory_path in "${source_game_dirs[@]}"; do
        [[ -d "${directory_path}" ]] || return 1
    done
    for directory_path in "${staged_game_dirs[@]}"; do
        [[ -d "${directory_path}" ]] || return 0
    done

    local probe_status=0
    if "${PYTHON_CMD}" - "${gamelibs_repo}" "${repo_root}" "${stage_root}" <<'PY'
# OPENQ4_GAMELIBS_REFRESH_PROBE_BEGIN
import hashlib
import json
import os
import pathlib
import re
import sys

gamelibs_root = pathlib.Path(sys.argv[1])
project_root = pathlib.Path(sys.argv[2])
stage_root = pathlib.Path(sys.argv[3])
support_dir_names = ("idlib", "renderer", "ui", "sys", "bse", "MayaImport")
python_bytecode_suffixes = (".pyc", ".pyo")


def raise_walk_error(error):
    raise error


def regular_file_map(specs):
    files = {}
    for relative_root, directory in specs:
        if not directory.exists():
            continue
        if directory.is_symlink() or not directory.is_dir():
            raise ValueError(f"GameLibs refresh input is not a regular directory: {directory}")

        resolved_root = relative_root.resolve()
        for current_root, directory_names, file_names in os.walk(
            directory,
            followlinks=False,
            onerror=raise_walk_error,
        ):
            current_path = pathlib.Path(current_root)
            for directory_name in directory_names:
                child_directory = current_path / directory_name
                if child_directory.is_symlink():
                    raise ValueError(f"GameLibs refresh input must not be a symlink: {child_directory}")

            for file_name in file_names:
                path = current_path / file_name
                if path.is_symlink() or not path.is_file():
                    raise ValueError(f"GameLibs refresh input must be a regular file: {path}")
                resolved_path = path.resolve()
                relative_path = resolved_path.relative_to(resolved_root).as_posix()
                relative_parts = pathlib.PurePosixPath(relative_path).parts
                if "__pycache__" in relative_parts or path.suffix.lower() in python_bytecode_suffixes:
                    continue
                if relative_path in files:
                    raise ValueError(f"duplicate GameLibs refresh input: {relative_path}")
                files[relative_path] = resolved_path
    return files


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def refresh_needed():
    source_specs = [
        (gamelibs_root, gamelibs_root / "src" / "game"),
        (gamelibs_root, gamelibs_root / "src" / "mpgame"),
    ]
    source_specs.extend(
        (project_root, project_root / "src" / directory_name)
        for directory_name in support_dir_names
    )
    staged_specs = [
        (stage_root, stage_root / "src" / "game"),
        (stage_root, stage_root / "src" / "mpgame"),
    ]
    staged_specs.extend(
        (stage_root, stage_root / "src" / directory_name)
        for directory_name in support_dir_names
    )

    source_files = regular_file_map(source_specs)
    staged_files = regular_file_map(staged_specs)
    if source_files.keys() != staged_files.keys():
        return True

    manifest = json.loads(
        (stage_root / "openq4_gamelibs_stage_manifest.json").read_text(encoding="utf-8")
    )
    if not isinstance(manifest, dict) or manifest.get("format") != 1:
        return True
    entries = manifest.get("files")
    file_count = manifest.get("fileCount")
    if (
        not isinstance(entries, list)
        or not isinstance(file_count, int)
        or isinstance(file_count, bool)
        or file_count != len(entries)
    ):
        return True

    manifest_hashes = {}
    for entry in entries:
        if not isinstance(entry, dict):
            return True
        relative_path = entry.get("path")
        expected_hash = entry.get("sha256")
        if (
            not isinstance(relative_path, str)
            or relative_path not in source_files
            or relative_path in manifest_hashes
            or not isinstance(expected_hash, str)
            or re.fullmatch(r"[0-9a-fA-F]{64}", expected_hash) is None
        ):
            return True
        manifest_hashes[relative_path] = expected_hash.lower()

    if manifest_hashes.keys() != source_files.keys():
        return True
    for relative_path, source_path in source_files.items():
        expected_hash = manifest_hashes[relative_path]
        if file_sha256(source_path) != expected_hash:
            return True
        if file_sha256(staged_files[relative_path]) != expected_hash:
            return True
    return False


try:
    needs_refresh = refresh_needed()
except Exception as exc:
    print(f"warning: could not verify staged GameLibs snapshot: {exc}", file=sys.stderr)
    needs_refresh = True
raise SystemExit(0 if needs_refresh else 10)
# OPENQ4_GAMELIBS_REFRESH_PROBE_END
PY
    then
        return 0
    else
        probe_status=$?
        if [[ ${probe_status} -eq 10 ]]; then
            return 1
        fi
        echo "GameLibs staging verification failed unexpectedly; forcing a Meson reconfigure." >&2
        return 0
    fi
}

load_build_dir_info() {
    read_line_array < <(get_compile_build_dir "$@")
    BUILD_DIR="${READ_ARRAY_RESULT[0]}"
    BUILD_DIR_HAS_EXPLICIT="${READ_ARRAY_RESULT[1]}"
}

test_obsolete_bse_build_option_present() {
    local build_dir="$1"
    [[ -n "${build_dir}" && -d "${build_dir}" ]] || return 1
    get_meson_build_option_value "${build_dir}" build_libbse >/dev/null 2>&1
}

declare -a SETUP_ARGS_RESULT=()

build_setup_args_for_existing_build_dir() {
    local build_dir="$1"
    SETUP_ARGS_RESULT=(
        setup
        "${build_dir}"
        "${repo_root}"
        --backend
        ninja
    )

    local buildtype=""
    buildtype="$(get_meson_build_option_value "${build_dir}" buildtype || true)"
    if [[ -n "${buildtype}" ]]; then
        SETUP_ARGS_RESULT+=("--buildtype=${buildtype}")
    fi

    local wrap_mode=""
    wrap_mode="$(get_meson_build_option_value "${build_dir}" wrap_mode || true)"
    if [[ -n "${wrap_mode}" ]]; then
        SETUP_ARGS_RESULT+=("--wrap-mode=${wrap_mode}")
    fi

    local option_name=""
    local option_value=""
    for option_name in platform_backend linux_x11 macos_graphics_bridge macos_openal_provider version_track version_iteration version_base_override openal_root_override use_pch build_engine build_games build_game_sp build_game_mp build_renderer_gl build_renderer_vk enforce_msvc_2026; do
        option_value="$(get_meson_build_option_value "${build_dir}" "${option_name}" || true)"
        if [[ -n "${option_value}" ]]; then
            SETUP_ARGS_RESULT+=("-D${option_name}=${option_value}")
        fi
    done
}

remove_build_directory() {
    local build_dir="$1"
    [[ -n "${build_dir}" && -d "${build_dir}" ]] || return 0

    local resolved_build_dir=""
    local resolved_repo_root=""
    resolved_build_dir="$(cd -- "${build_dir}" && pwd -P)"
    resolved_repo_root="$(cd -- "${repo_root}" && pwd -P)"

    if [[ "${resolved_build_dir}" == "/" || "${resolved_build_dir}" == "${resolved_repo_root}" ]]; then
        echo "Refusing to remove unsafe Meson build directory '${resolved_build_dir}'." >&2
        exit 1
    fi

    if [[ ! -f "${resolved_build_dir}/meson-private/coredata.dat" && ! -f "${resolved_build_dir}/build.ninja" ]]; then
        echo "Refusing to remove '${resolved_build_dir}' because it does not look like a Meson build directory." >&2
        exit 1
    fi

    rm -rf -- "${resolved_build_dir}"
}

remove_stale_bse_artifacts() {
    local directory_path="$1"
    [[ -n "${directory_path}" && -d "${directory_path}" ]] || return 0

    find "${directory_path}" -maxdepth 1 -type f \
        \( -name 'openQ4-BSE_*.dll' -o -name 'openQ4-BSE_*.dylib' -o -name 'openQ4-BSE_*.so' -o -name 'openQ4-BSE_*.lib' -o -name 'openQ4-BSE_*.pdb' -o \
           -name 'openQ4-BSE_*.dll' -o -name 'openQ4-BSE_*.dylib' -o -name 'openQ4-BSE_*.so' -o -name 'openQ4-BSE_*.lib' -o -name 'openQ4-BSE_*.pdb' \) \
        -print | while IFS= read -r match; do
            [[ -n "${match}" ]] || continue
            echo "Removing stale BSE artifact '${match}'"
            rm -f -- "${match}"
        done
}

remove_non_runtime_install_artifacts() {
    local install_root="$1"
    [[ -n "${install_root}" && -d "${install_root}" ]] || return 0

    find "${install_root}" -maxdepth 1 -type f \
        \( -name '*.lib' -o -name '*.exp' -o -name '*.ilk' -o -name '*.map' -o -name '*.zip' -o -name 'mgscope_sendinput.cfg' -o -name 'scope_autotest*.cfg' \) \
        -print | while IFS= read -r match; do
            [[ -n "${match}" ]] || continue
            echo "Removing non-runtime staged artifact '${match}'"
            rm -f -- "${match}"
        done

    local install_game_dir="${install_root}/baseoq4"
    [[ -d "${install_game_dir}" ]] || return 0

    find "${install_game_dir}" -maxdepth 1 -type f \
        \( -name '*.lib' -o -name '*.exp' -o -name '*.ilk' -o -name '*.map' \) \
        -print | while IFS= read -r match; do
            [[ -n "${match}" ]] || continue
            echo "Removing non-runtime staged artifact '${match}'"
            rm -f -- "${match}"
        done
}

declare -a effective_args=()
for arg in "$@"; do
    effective_args+=("${arg%$'\r'}")
done

command_name="${effective_args[0]:-}"
if [[ -z "${command_name}" ]]; then
    echo "No Meson arguments were provided to meson_setup.sh." >&2
    exit 1
fi

if [[ ( "${command_name}" == "setup" || "${command_name}" == "compile" || "${command_name}" == "install" ) && "${OPENQ4_SKIP_ICON_SYNC:-0}" != "1" ]]; then
    if [[ ! -f "${sync_icons_script}" ]]; then
        echo "Icon sync script not found: '${sync_icons_script}'." >&2
        exit 1
    fi

    "${PYTHON_CMD}" "${sync_icons_script}" --source-root "${repo_root}"
fi

if [[ "${command_name}" == "install" ]]; then
    if [[ ! -f "${check_staged_content_script}" ]]; then
        echo "Staged content edit check script not found: '${check_staged_content_script}'." >&2
        exit 1
    fi

    "${PYTHON_CMD}" "${check_staged_content_script}" --source-root "${repo_root}"
fi

if [[ "${command_name}" == "setup" ]]; then
    for (( i = 0; i < ${#effective_args[@]}; ++i )); do
        if [[ "${effective_args[$i]}" == "--reconfigure" && $((i + 1)) -lt ${#effective_args[@]} ]]; then
            candidate_builddir="$(cd -- "${effective_args[$((i + 1))]}" 2>/dev/null && pwd || true)"
            if [[ -n "${candidate_builddir}" ]] && test_obsolete_bse_build_option_present "${candidate_builddir}"; then
                echo "Meson build directory '${candidate_builddir}' still uses the removed build_libbse option. Recreating it..."
                remove_build_directory "${candidate_builddir}"
                declare -a rewritten_args=()
                for arg in "${effective_args[@]}"; do
                    if [[ "${arg}" == "--reconfigure" ]]; then
                        continue
                    fi
                    rewritten_args+=("${arg}")
                done
                effective_args=("${rewritten_args[@]}")
            fi
            break
        fi
    done
fi

if [[ "${command_name}" == "compile" || "${command_name}" == "install" ]]; then
    load_build_dir_info "${effective_args[@]}"

    if [[ "${command_name}" == "compile" ]] && ! test_meson_build_directory "${BUILD_DIR}"; then
        echo "Meson build directory '${BUILD_DIR}' is missing or invalid. Running meson setup..."
        declare -a setup_args=()
        if test_obsolete_bse_build_option_present "${BUILD_DIR}"; then
            echo "Meson build directory '${BUILD_DIR}' still uses the removed build_libbse option. Recreating it..."
            build_setup_args_for_existing_build_dir "${BUILD_DIR}"
            setup_args=("${SETUP_ARGS_RESULT[@]}")
            remove_build_directory "${BUILD_DIR}"
        else
            setup_args=(
                setup
                "${BUILD_DIR}"
                "${repo_root}"
                --backend
                ninja
                --buildtype=debug
                --wrap-mode=forcefallback
            )
        fi
        run_meson "${setup_args[@]}"
    elif test_obsolete_bse_build_option_present "${BUILD_DIR}"; then
        echo "Meson build directory '${BUILD_DIR}' still uses the removed build_libbse option. Recreating it..."
        build_setup_args_for_existing_build_dir "${BUILD_DIR}"
        remove_build_directory "${BUILD_DIR}"
        run_meson "${SETUP_ARGS_RESULT[@]}"
    fi

    if test_gamelibs_stage_refresh_needed "${BUILD_DIR}"; then
        echo "GameLibs staging inputs changed since the last snapshot. Reconfiguring '${BUILD_DIR}'..."
        run_meson setup --reconfigure "${BUILD_DIR}" "${repo_root}"
    fi

    if [[ "${BUILD_DIR_HAS_EXPLICIT}" == "0" ]]; then
        declare -a remaining_args=()
        if (( ${#effective_args[@]} > 1 )); then
            remaining_args=("${effective_args[@]:1}")
        fi
        effective_args=("${effective_args[0]}" -C "${BUILD_DIR}" "${remaining_args[@]}")
    fi

    if [[ "${command_name}" == "install" ]]; then
        found_skip_subprojects=0
        for arg in "${effective_args[@]}"; do
            if [[ "${arg}" == "--skip-subprojects" ]]; then
                found_skip_subprojects=1
                break
            fi
        done
        if [[ "${found_skip_subprojects}" == "0" ]]; then
            effective_args+=(--skip-subprojects)
        fi
    fi
fi

if run_meson "${effective_args[@]}"; then
    exit_code=0
else
    exit_code=$?
fi

if [[ "${exit_code}" == "0" && ( "${command_name}" == "compile" || "${command_name}" == "install" ) ]]; then
    remove_stale_bse_artifacts "${BUILD_DIR}"
    remove_non_runtime_install_artifacts "${repo_root}/.install"
    if [[ "${command_name}" == "install" ]]; then
        remove_stale_bse_artifacts "${repo_root}/.install"
    fi
fi

exit "${exit_code}"
