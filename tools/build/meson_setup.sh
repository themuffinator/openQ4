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

test_game_sources_refresh_needed() {
    local build_dir="$1"
    [[ -d "${build_dir}" ]] || return 1
    local games
    games="$(get_meson_build_option_value "${build_dir}" build_games || true)"
    [[ "${games}" != "false" ]] || return 1
    # Content and git-state drift only refresh the snapshot; a membership change reconfigures.
    if "${PYTHON_CMD}" "${script_dir}/game_source_inventory.py" "${repo_root}" \
        --refresh "${build_dir}/openq4_game_sources.json" >/dev/null; then
        return 1
    fi
    return 0
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

    if test_game_sources_refresh_needed "${BUILD_DIR}"; then
        echo "In-tree game source membership changed. Reconfiguring '${BUILD_DIR}'..."
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
