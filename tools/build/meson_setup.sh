#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
default_builddir="${repo_root}/builddir"
declare -a MESON_CMD=()

resolve_python() {
    if command -v python3 >/dev/null 2>&1; then
        command -v python3
        return 0
    fi

    if command -v python >/dev/null 2>&1; then
        command -v python
        return 0
    fi

    return 1
}

resolve_meson_cmd() {
    local python_cmd=""
    python_cmd="$(resolve_python || true)"

    if [[ -n "${python_cmd}" ]] && "${python_cmd}" -c 'import mesonbuild.mesonmain' >/dev/null 2>&1; then
        MESON_CMD=("${python_cmd}" -m mesonbuild.mesonmain)
        return
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

    python - "$build_dir" "$has_explicit" <<'PY'
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

    if [[ ! -f "${intro_options_path}" ]]; then
        return 1
    fi

    python - "$intro_options_path" "$option_name" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    options = json.load(handle)

for option in options:
    if option.get("name") == sys.argv[2]:
        value = option.get("value")
        if isinstance(value, bool):
            print("true" if value else "false")
        elif value is None:
            print("")
        else:
            print(str(value))
        raise SystemExit(0)

raise SystemExit(1)
PY
}

desired_build_libbse() {
    if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
        printf '%s\n' "false"
    else
        printf '%s\n' "true"
    fi
}

set_build_libbse_arg() {
    local desired_arg="-Dbuild_libbse=$(desired_build_libbse)"
    local result=()
    local arg

    for arg in "$@"; do
        if [[ "${arg}" == -Dbuild_libbse=* ]]; then
            continue
        fi
        result+=("${arg}")
    done

    result+=("${desired_arg}")
    printf '%s\0' "${result[@]}"
}

load_build_dir_info() {
    local info
    mapfile -t info < <(get_compile_build_dir "$@")
    BUILD_DIR="${info[0]}"
    BUILD_DIR_HAS_EXPLICIT="${info[1]}"
}

command_name="${1:-}"
if [[ -z "${command_name}" ]]; then
    echo "No Meson arguments were provided to meson_setup.sh." >&2
    exit 1
fi

declare -a effective_args=("$@")

if [[ "${command_name}" == "setup" ]]; then
    mapfile -d '' -t effective_args < <(set_build_libbse_arg "${effective_args[@]}")
fi

if [[ "${command_name}" == "compile" || "${command_name}" == "install" ]]; then
    load_build_dir_info "${effective_args[@]}"

    if [[ "${command_name}" == "compile" ]] && ! test_meson_build_directory "${BUILD_DIR}"; then
        echo "Meson build directory '${BUILD_DIR}' is missing or invalid. Running meson setup..."
        declare -a setup_args=(
            setup
            --wipe
            "${BUILD_DIR}"
            "${repo_root}"
            --backend
            ninja
            --buildtype=debug
            --wrap-mode=forcefallback
        )
        mapfile -d '' -t setup_args < <(set_build_libbse_arg "${setup_args[@]}")
        run_meson "${setup_args[@]}"
    fi

    if test_meson_build_directory "${BUILD_DIR}"; then
        current_bse="$(get_meson_build_option_value "${BUILD_DIR}" build_libbse || true)"
        desired_bse="$(desired_build_libbse)"
        if [[ -n "${current_bse}" && "${current_bse}" != "${desired_bse}" ]]; then
            echo "Applying local/CI BSE policy to '${BUILD_DIR}' (build_libbse=${desired_bse})..."
            declare -a reconfigure_args=(
                setup
                --reconfigure
                "${BUILD_DIR}"
                "${repo_root}"
            )
            mapfile -d '' -t reconfigure_args < <(set_build_libbse_arg "${reconfigure_args[@]}")
            run_meson "${reconfigure_args[@]}"
        fi
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

exec "${MESON_CMD[@]}" "${effective_args[@]}"
