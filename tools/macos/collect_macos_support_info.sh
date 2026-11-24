#!/bin/sh
# Collect redacted macOS support data for openQ4 crash and hardware reports.

set -eu
umask 077

case "$0" in
    */*) script_dir=${0%/*} ;;
    *) script_dir=. ;;
esac
SCRIPT_DIR=$(CDPATH= cd "${script_dir}" && pwd -P)
PACKAGE_ROOT=${OPENQ4_PACKAGE_ROOT:-$SCRIPT_DIR}
OUTPUT_DIR=${1:-$(pwd)}
HOME_DIR=${HOME:-}
STAMP=$(date -u +"%Y%m%d-%H%M%SZ")
WORK_PARENT=$(mktemp -d "${TMPDIR:-/tmp}/openq4-support.XXXXXX")
BUNDLE_NAME="openq4-macos-support-${STAMP}"
BUNDLE_DIR="${WORK_PARENT}/${BUNDLE_NAME}"
ARCHIVE_PATH="${OUTPUT_DIR%/}/${BUNDLE_NAME}.tar.gz"
ARCHIVE_TMP=
MAX_SUPPORT_TEXT_BYTES=2097152
MAX_CRASH_REPORT_BYTES=8388608
MAX_SUPPORT_ARCHIVE_BYTES=134217728

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

cleanup() {
    if [ -n "${ARCHIVE_TMP:-}" ]; then
        rm -f "${ARCHIVE_TMP}"
    fi
    rm -rf "${WORK_PARENT}"
}
trap cleanup EXIT HUP INT TERM

runtime_arch_token() {
    case "$(uname -m 2>/dev/null || printf unknown)" in
        arm64|aarch64) printf 'arm64\n' ;;
        x86_64|amd64) printf 'x64\n' ;;
        i386|i686) printf 'x86\n' ;;
        *) uname -m 2>/dev/null || printf 'unknown\n' ;;
    esac
}

RUNTIME_ARCH=$(runtime_arch_token)
APP_ROOT="${PACKAGE_ROOT}/openQ4.app"
APP_RESOURCE_ROOT="${APP_ROOT}/Contents/Resources"
APP_FRAMEWORK_ROOT="${APP_ROOT}/Contents/Frameworks"
SKIPPED_CRASH_REPORT_INDEX=0

contains_control_chars() {
    LC_ALL=C printf '%s' "$1" | grep -q '[[:cntrl:]]'
}

prepare_package_root() {
    if contains_control_chars "${PACKAGE_ROOT}"; then
        fail "Support package root must not contain control characters"
    fi
    if [ -L "${PACKAGE_ROOT}" ]; then
        fail "Support package root must not be a symlink: ${PACKAGE_ROOT}"
    fi
    if [ ! -d "${PACKAGE_ROOT}" ]; then
        fail "Support package root must be an existing directory: ${PACKAGE_ROOT}"
    fi
}

prepare_output_target() {
    if [ -z "${OUTPUT_DIR}" ]; then
        fail "Support output directory must not be empty"
    fi
    if contains_control_chars "${OUTPUT_DIR}"; then
        fail "Support output directory must not contain control characters"
    fi
    if [ -L "${OUTPUT_DIR}" ]; then
        fail "Support output directory must not be a symlink: ${OUTPUT_DIR}"
    fi
    if [ -e "${OUTPUT_DIR}" ] && [ ! -d "${OUTPUT_DIR}" ]; then
        fail "Support output path exists but is not a directory: ${OUTPUT_DIR}"
    fi
    mkdir -p "${OUTPUT_DIR}"
    if [ -L "${OUTPUT_DIR}" ] || [ ! -d "${OUTPUT_DIR}" ]; then
        fail "Support output directory must be a real directory: ${OUTPUT_DIR}"
    fi
    if [ -L "${ARCHIVE_PATH}" ] || [ -d "${ARCHIVE_PATH}" ]; then
        fail "Support archive target must not be a symlink or directory: ${ARCHIVE_PATH}"
    fi
    if [ -e "${ARCHIVE_PATH}" ]; then
        fail "Support archive target already exists: ${ARCHIVE_PATH}"
    fi
    ARCHIVE_TMP=$(mktemp "${OUTPUT_DIR%/}/.${BUNDLE_NAME}.XXXXXX.tar.gz.tmp") || fail "Unable to create support archive temporary target in: ${OUTPUT_DIR}"
    if [ -L "${ARCHIVE_TMP}" ] || [ ! -f "${ARCHIVE_TMP}" ]; then
        fail "Support archive temporary target must be a regular file: ${ARCHIVE_TMP}"
    fi
}

prepare_package_root
prepare_output_target
mkdir -p "${BUNDLE_DIR}/system" "${BUNDLE_DIR}/package" "${BUNDLE_DIR}/logs" "${BUNDLE_DIR}/crash-reports"

redact_text() {
    sanitize_text | \
    sed -E \
        -e 's|/Users/[^/[:space:]]+|~|g' \
        -e 's|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}|<email>|g'
}

sanitize_text() {
    LC_ALL=C tr '\000-\010\013\014\015\016-\037\177' '?'
}

limit_stream_tail() {
    max_bytes=$1
    printf 'Support report output is limited to the final %s bytes before redaction.\n\n' "${max_bytes}"
    tail -c "${max_bytes}"
}

write_text() {
    target=$1
    shift
    {
        for line in "$@"; do
            printf '%s\n' "$line"
        done
    } | redact_text > "${BUNDLE_DIR}/${target}"
}

write_command() {
    target=$1
    shift
    {
        printf '$'
        for part in "$@"; do
            printf ' %s' "$part"
        done
        printf '\n\n'
        {
            "$@" 2>&1 || printf '\n(command failed; continuing support collection)\n'
        } | limit_stream_tail "${MAX_SUPPORT_TEXT_BYTES}"
    } | redact_text > "${BUNDLE_DIR}/${target}"
}

path_exists_for_inspection() {
    inspect_path=$1
    if [ -L "${inspect_path}" ]; then
        printf '\n-- %s --\n' "${inspect_path}"
        printf '(skipped symlink; support collector does not follow symlinks)\n'
        return 1
    fi
    [ -e "${inspect_path}" ]
}

write_openq4_log_candidate_paths() {
    target=$1
    : > "${target}"
    if [ -n "${HOME_DIR}" ]; then
        printf '%s\n' "${HOME_DIR}/Library/Application Support/openQ4/baseoq4/logs/openq4.log" >> "${target}"
        printf '%s\n' "${HOME_DIR}/baseoq4/logs/openq4.log" >> "${target}"
    fi
    printf '%s\n' "${PACKAGE_ROOT}/baseoq4/logs/openq4.log" >> "${target}"
}

write_openq4_renderer_config_candidate_paths() {
    target=$1
    : > "${target}"
    if [ -n "${HOME_DIR}" ]; then
        printf '%s\n' "${HOME_DIR}/Library/Application Support/openQ4/baseoq4/openQ4Config.cfg" >> "${target}"
        printf '%s\n' "${HOME_DIR}/baseoq4/openQ4Config.cfg" >> "${target}"
    fi
}

copy_text_if_present() {
    source_path=$1
    target_path=$2
    max_bytes=${3:-$MAX_SUPPORT_TEXT_BYTES}
    if [ -L "${source_path}" ]; then
        write_text "${target_path}" \
            "Skipped symlinked source: ${source_path}" \
            "The support collector does not follow symlinks when copying package, log, or crash-report text."
    elif [ -f "${source_path}" ]; then
        source_bytes=$(wc -c < "${source_path}" 2>/dev/null | tr -d '[:space:]' || printf '0')
        case "${source_bytes}" in
            ''|*[!0123456789]*) source_bytes=0 ;;
        esac
        {
            if [ "${source_bytes}" -gt "${max_bytes}" ]; then
                printf 'Source file was larger than %s bytes and was truncated to its final %s bytes for this support archive: %s\n\n' "${max_bytes}" "${max_bytes}" "${source_path}"
                if ! tail -c "${max_bytes}" < "${source_path}" 2>/dev/null; then
                    printf '(truncated copy failed; source was not copied)\n'
                fi
            else
                cat "${source_path}"
            fi
        } | redact_text > "${BUNDLE_DIR}/${target_path}"
    fi
}

write_bounded_report() {
    target=$1
    limit_stream_tail "${MAX_SUPPORT_TEXT_BYTES}" | redact_text > "${BUNDLE_DIR}/${target}"
}

copy_crash_report_if_safe() {
    crash_path=$1
    crash_name=$(basename "${crash_path}")
    case "${crash_name}" in
        *[!ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-]*)
            SKIPPED_CRASH_REPORT_INDEX=$((SKIPPED_CRASH_REPORT_INDEX + 1))
            write_text "crash-reports/skipped-${SKIPPED_CRASH_REPORT_INDEX}.txt" \
                "Skipped crash report with unsupported filename characters: ${crash_name}" \
                "The support collector only copies DiagnosticReports files with archive-safe names."
            return
            ;;
    esac
    copy_text_if_present "${crash_path}" "crash-reports/${crash_name}" "${MAX_CRASH_REPORT_BYTES}"
}

write_command "system/sw_vers.txt" sw_vers
write_command "system/uname.txt" uname -a
write_command "system/hardware.txt" system_profiler SPHardwareDataType
write_command "system/displays.txt" system_profiler SPDisplaysDataType

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Rosetta is not a supported openQ4 release target; this report only identifies translated support sessions.\n'

    if command -v arch >/dev/null 2>&1; then
        printf '\n$ arch\n'
        arch 2>&1 || printf '\n(command failed; continuing support collection)\n'
    else
        printf '\narch not found.\n'
    fi

    printf '\n$ uname -m\n'
    uname -m 2>&1 || printf '\n(command failed; continuing support collection)\n'

    if command -v sysctl >/dev/null 2>&1; then
        printf '\n$ sysctl -n sysctl.proc_translated\n'
        sysctl -n sysctl.proc_translated 2>&1 || printf '\n(sysctl.proc_translated unavailable; this is expected on Intel Macs and some native arm64 sessions)\n'
    else
        printf '\nsysctl not found.\n'
    fi
} | write_bounded_report "system/rosetta.txt"

if command -v xcodebuild >/dev/null 2>&1; then
    write_command "system/xcode.txt" xcodebuild -version
else
    write_text "system/xcode.txt" "xcodebuild not found"
fi

if command -v xcrun >/dev/null 2>&1; then
    write_command "system/macos-sdk.txt" xcrun --sdk macosx --show-sdk-version
else
    write_text "system/macos-sdk.txt" "xcrun not found"
fi

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Detected runtime architecture token: %s\n' "${RUNTIME_ARCH}"
    printf 'Current directory: %s\n' "$(pwd)"
    printf '\nExpected self-contained app and diagnostic package entries:\n'
    for entry in \
        "openQ4.app" \
        "openQ4-client_${RUNTIME_ARCH}" \
        "openQ4-ded_${RUNTIME_ARCH}" \
        "collect_macos_support_info.sh" \
        "VERSION.txt" \
        "SYMBOLS.txt"
    do
        if [ -e "${PACKAGE_ROOT}/${entry}" ]; then
            printf 'present: %s\n' "${entry}"
        else
            printf 'missing: %s\n' "${entry}"
        fi
    done
    printf '\nPackage root listing:\n'
    ls -la "${PACKAGE_ROOT}" 2>&1 || true
    printf '\nEmbedded baseoq4 data listing:\n'
    ls -la "${APP_RESOURCE_ROOT}/baseoq4" 2>&1 || true
    printf '\nEmbedded game-module listing:\n'
    ls -la "${APP_FRAMEWORK_ROOT}" 2>&1 || true
} | write_bounded_report "package/layout.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Detected runtime architecture token: %s\n' "${RUNTIME_ARCH}"
    printf 'App path: %s\n' "${APP_ROOT}"
    printf 'App executable path: %s\n' "${APP_ROOT}/Contents/MacOS/openQ4"
    printf 'Expected loose client path: %s\n' "${PACKAGE_ROOT}/openQ4-client_${RUNTIME_ARCH}"
    printf 'Expected loose dedicated-server path: %s\n' "${PACKAGE_ROOT}/openQ4-ded_${RUNTIME_ARCH}"
    printf 'Expected embedded game-data path: %s\n' "${APP_RESOURCE_ROOT}/baseoq4"
    printf 'Expected embedded game-module path: %s\n' "${APP_FRAMEWORK_ROOT}"
    printf 'Expected log keys: fs_basepath, fs_cdpath, fs_savepath\n'
    printf '\nCaptured filesystem path lines from available logs:\n'
    if [ -z "${HOME_DIR}" ]; then
        printf 'HOME was not set; home-scoped openq4.log paths were skipped.\n'
    fi

    found_log=0
    path_lines="${WORK_PARENT}/path-lines.txt"
    log_candidates="${WORK_PARENT}/path-log-candidates.txt"
    write_openq4_log_candidate_paths "${log_candidates}"
    while IFS= read -r log_path; do
        if [ -L "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            printf '(skipped symlink; support collector does not follow symlinks)\n'
        elif [ -f "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            if grep -E 'fs_(basepath|cdpath|savepath)|Auto-detected fs_basepath|base path|cd path|save path' "${log_path}" > "${path_lines}" 2>/dev/null; then
                tail -n 80 "${path_lines}"
            else
                printf '(no fs_basepath, fs_cdpath, or fs_savepath lines found in this log)\n'
            fi
        fi
    done < "${log_candidates}"

    if [ "${found_log}" -eq 0 ]; then
        printf 'No openq4.log files were found. fs_basepath, fs_cdpath, and fs_savepath values could not be copied without launching openQ4.\n'
    fi
} | write_bounded_report "package/path-resolution.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Expected package metadata keys: version, version_tag, platform, arch, openq4_commit, openq4_dirty, openq4_game_commit, openq4_game_dirty\n'

    for manifest_path in \
        "${PACKAGE_ROOT}/VERSION.txt" \
        "${PACKAGE_ROOT}/openQ4.app/Contents/Resources/VERSION.txt"
    do
        printf '\n-- %s --\n' "${manifest_path}"
        if [ -L "${manifest_path}" ]; then
            printf '(skipped symlink; support collector does not follow symlinks)\n'
        elif [ -f "${manifest_path}" ]; then
            if ! grep -E '^(version|version_tag|platform|arch|openq4_commit|openq4_dirty|openq4_game_commit|openq4_game_dirty)=' "${manifest_path}" 2>/dev/null; then
                printf '(no recognized package build metadata keys found)\n'
            fi
        else
            printf '(metadata manifest not found)\n'
        fi
    done

    printf '\nGame module files in the app Frameworks directory (plus legacy adjacent locations and retired split game-sp/game-mp modules):\n'
    found_module=0
    for module_path in \
        "${APP_FRAMEWORK_ROOT}"/game_*.dylib \
        "${PACKAGE_ROOT}/baseoq4"/game_*.dylib \
        "${PACKAGE_ROOT}/baseoq4"/game_*.dll \
        "${PACKAGE_ROOT}/baseoq4"/game_*.so \
        "${APP_FRAMEWORK_ROOT}"/game-sp_* \
        "${APP_FRAMEWORK_ROOT}"/game-mp_* \
        "${PACKAGE_ROOT}/baseoq4"/game-sp_* \
        "${PACKAGE_ROOT}/baseoq4"/game-mp_*
    do
        if [ -L "${module_path}" ]; then
            found_module=1
            printf '%s (skipped symlink; support collector does not follow symlinks)\n' "${module_path}"
        elif [ -e "${module_path}" ]; then
            found_module=1
            printf '%s\n' "${module_path}"
        fi
    done
    if [ "${found_module}" -eq 0 ]; then
        printf '(no game module files found)\n'
    fi
} | write_bounded_report "package/build-metadata.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Architecture checks do not launch openQ4.\n'

    for binary_path in \
        "${APP_ROOT}/Contents/MacOS/openQ4" \
        "${APP_FRAMEWORK_ROOT}"/game_*.dylib \
        "${PACKAGE_ROOT}/openQ4-client_arm64" \
        "${PACKAGE_ROOT}/openQ4-client_x64" \
        "${PACKAGE_ROOT}/openQ4-client_x86" \
        "${PACKAGE_ROOT}/openQ4-ded_arm64" \
        "${PACKAGE_ROOT}/openQ4-ded_x64" \
        "${PACKAGE_ROOT}/openQ4-ded_x86" \
        "${PACKAGE_ROOT}/baseoq4"/game_*.dylib
    do
        if path_exists_for_inspection "${binary_path}"; then
            printf '\n-- %s --\n' "${binary_path}"
            if command -v file >/dev/null 2>&1; then
                file "${binary_path}" 2>&1 || printf '(file inspection failed; continuing support collection)\n'
            else
                printf 'file not found.\n'
            fi
            if command -v lipo >/dev/null 2>&1; then
                printf 'lipo -archs: '
                lipo -archs "${binary_path}" 2>&1 || printf '(lipo architecture inspection failed; continuing support collection)\n'
            else
                printf 'lipo not found.\n'
            fi
        fi
    done
} | write_bounded_report "package/binary-architecture.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Dependency and install-name checks do not launch openQ4.\n'

    if command -v otool >/dev/null 2>&1; then
        for binary_path in \
            "${APP_ROOT}/Contents/MacOS/openQ4" \
            "${APP_FRAMEWORK_ROOT}"/game_*.dylib \
            "${PACKAGE_ROOT}/openQ4-client_arm64" \
            "${PACKAGE_ROOT}/openQ4-client_x64" \
            "${PACKAGE_ROOT}/openQ4-client_x86" \
            "${PACKAGE_ROOT}/openQ4-ded_arm64" \
            "${PACKAGE_ROOT}/openQ4-ded_x64" \
            "${PACKAGE_ROOT}/openQ4-ded_x86" \
            "${PACKAGE_ROOT}/baseoq4"/game_*.dylib
        do
            if path_exists_for_inspection "${binary_path}"; then
                printf '\n-- otool -L: %s --\n' "${binary_path}"
                otool -L "${binary_path}" 2>&1 || printf '\n(otool dependency inspection failed; continuing support collection)\n'
                case "${binary_path}" in
                    *.dylib)
                        printf '\n-- otool -D: %s --\n' "${binary_path}"
                        otool -D "${binary_path}" 2>&1 || printf '\n(otool install-name inspection failed; continuing support collection)\n'
                        ;;
                esac
            fi
        done
    else
        printf 'otool not found.\n'
    fi
} | write_bounded_report "package/dylib-dependencies.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Expected log keys: OpenAL vendor, OpenAL renderer, OpenAL version, OpenAL requested device, OpenAL default device, OpenAL active device, OpenAL EFX\n'
    printf '\nCaptured OpenAL and EFX lines from available logs:\n'
    if [ -z "${HOME_DIR}" ]; then
        printf 'HOME was not set; home-scoped openq4.log paths were skipped.\n'
    fi

    found_log=0
    found_audio_line=0
    openal_lines="${WORK_PARENT}/openal-lines.txt"
    log_candidates="${WORK_PARENT}/openal-log-candidates.txt"
    write_openq4_log_candidate_paths "${log_candidates}"
    while IFS= read -r log_path; do
        if [ -L "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            printf '(skipped symlink; support collector does not follow symlinks)\n'
        elif [ -f "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            if grep -E 'OpenAL (vendor|renderer|version|requested device|default device|active device|ALC version|EFX|HRTF|output mode)|idSoundHardware_OpenAL::Init|s_deviceName|s_useEAXReverb' "${log_path}" > "${openal_lines}" 2>/dev/null; then
                found_audio_line=1
                tail -n 120 "${openal_lines}"
            else
                printf '(no OpenAL vendor, renderer, device, or EFX lines found in this log)\n'
            fi
        fi
    done < "${log_candidates}"

    if [ "${found_log}" -eq 0 ]; then
        printf 'No openq4.log files were found. OpenAL vendor, renderer, device name, and EFX warning lines could not be copied without launching openQ4.\n'
    elif [ "${found_audio_line}" -eq 0 ]; then
        printf '\nNo OpenAL vendor, renderer, device name, or EFX warning lines were found in the copied logs.\n'
    fi
} | write_bounded_report "logs/openal-summary.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Expected renderer keys: R_InitOpenGL, renderer startup phase, Renderer driver quirks, interaction fallback, render-target/MSAA diagnostics, selected filesystem/module paths, fatal signal\n'
    printf '\nCaptured renderer startup and crash lines from available logs:\n'
    if [ -z "${HOME_DIR}" ]; then
        printf 'HOME was not set; home-scoped openq4.log paths were skipped.\n'
    fi

    found_log=0
    found_renderer_line=0
    renderer_lines="${WORK_PARENT}/renderer-lines.txt"
    log_candidates="${WORK_PARENT}/renderer-log-candidates.txt"
    write_openq4_log_candidate_paths "${log_candidates}"
    while IFS= read -r log_path; do
        if [ -L "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            printf '(skipped symlink; support collector does not follow symlinks)\n'
        elif [ -f "${log_path}" ]; then
            found_log=1
            printf '\n-- %s --\n' "${log_path}"
            if grep -E 'R_InitOpenGL|R_ReloadARBPrograms|renderer startup phase|last renderer startup phase|first ARB2 interaction handoff|ARB2 interaction driver bypass|interaction color mode|renderer startup ARB interaction selection|Renderer driver quirks|Renderer bootstrap|Renderer upload manager|SDL3: graphics bridge|SDL3: reported OpenGL context|SDL3: OpenGL context|GL_ARB_vertex_buffer_object disabled|SimpleInteraction[.]vfp|material_interaction|GLSL material interaction|Apple OpenGL 2[.]1 interaction|Unsupported Apple OpenGL 2[.]1 compatibility path|bypassing ARB2 light interactions|using ARB2 renderSystem|idRenderTexture|GL_FRAMEBUFFER_|framebuffer .*incomplete|Forward render target MSAA:|MSAA requested|offscreen MSAA|_forwardRender|Filesystem paths:|Selected game module:|Game module search failed:|Game module load failed:|dlopen .* failed:|fatal signal SIGSEGV|fatal signal SIG|last game module phase:|game module phase:|Quality profile:|SDL3: reported OpenGL framebuffer attributes:|no stencil buffer|Apple GL 2[.]1 interaction routes:|Apple GL 2[.]1 corridor was not applied|ReloadGameModule failed|game module swap to' "${log_path}" > "${renderer_lines}" 2>/dev/null; then
                found_renderer_line=1
                tail -n 260 "${renderer_lines}"
            else
                printf '(no renderer startup, interaction-fallback, render-target, filesystem/module, or fatal-signal lines found in this log)\n'
            fi
        fi
    done < "${log_candidates}"

    if [ "${found_log}" -eq 0 ]; then
        printf 'No openq4.log files were found. Renderer startup and crash lines could not be copied without launching openQ4.\n'
    elif [ "${found_renderer_line}" -eq 0 ]; then
        printf '\nNo renderer startup, interaction-fallback, render-target, filesystem/module, or fatal-signal lines were found in the copied logs.\n'
    fi
} | write_bounded_report "logs/renderer-summary.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Only renderer and performance settings are copied: r_* plus com_machineSpec and com_performancePreset. Bindings, player/account, network, audio-device, and arbitrary config settings are excluded.\n'
    printf '\nCaptured renderer/performance settings from safe saved configs:\n'
    if [ -z "${HOME_DIR}" ]; then
        printf 'HOME was not set; saved openQ4Config.cfg paths were skipped.\n'
    fi

    found_config=0
    found_renderer_config_line=0
    renderer_config_lines="${WORK_PARENT}/renderer-config-lines.txt"
    renderer_config_candidates="${WORK_PARENT}/renderer-config-candidates.txt"
    write_openq4_renderer_config_candidate_paths "${renderer_config_candidates}"
    while IFS= read -r config_path; do
        if [ -L "${config_path}" ]; then
            found_config=1
            printf '\n-- %s --\n' "${config_path}"
            printf '(skipped symlink; support collector does not follow symlinks)\n'
        elif [ -f "${config_path}" ]; then
            found_config=1
            printf '\n-- %s --\n' "${config_path}"
            if grep -E '^[[:space:]]*seta?[[:space:]]+(r_[[:alnum:]_]+|com_(machineSpec|performancePreset))[[:space:]]+' "${config_path}" > "${renderer_config_lines}" 2>/dev/null; then
                found_renderer_config_line=1
                tail -n 240 "${renderer_config_lines}"
            else
                printf '(no renderer or performance settings were found in this config)\n'
            fi
        fi
    done < "${renderer_config_candidates}"

    if [ "${found_config}" -eq 0 ]; then
        printf 'No saved openQ4Config.cfg files were found. Renderer settings could not be copied without launching openQ4.\n'
    elif [ "${found_renderer_config_line}" -eq 0 ]; then
        printf '\nNo renderer or performance settings were found in the inspected saved configs.\n'
    fi
} | write_bounded_report "logs/renderer-config.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Signing and Gatekeeper checks do not launch openQ4.\n'

    if command -v codesign >/dev/null 2>&1; then
        for signed_path in \
            "${APP_ROOT}" \
            "${APP_ROOT}/Contents/MacOS/openQ4" \
            "${APP_FRAMEWORK_ROOT}"/game_*.dylib \
            "${PACKAGE_ROOT}/openQ4-client_arm64" \
            "${PACKAGE_ROOT}/openQ4-client_x64" \
            "${PACKAGE_ROOT}/openQ4-client_x86" \
            "${PACKAGE_ROOT}/openQ4-ded_arm64" \
            "${PACKAGE_ROOT}/openQ4-ded_x64" \
            "${PACKAGE_ROOT}/openQ4-ded_x86" \
            "${PACKAGE_ROOT}/baseoq4"/game_*.dylib
        do
            if path_exists_for_inspection "${signed_path}"; then
                printf '\n-- codesign verify: %s --\n' "${signed_path}"
                codesign --verify --deep --strict --verbose=4 "${signed_path}" 2>&1 || printf '\n(codesign verify failed; continuing support collection)\n'
                printf '\n-- codesign display: %s --\n' "${signed_path}"
                codesign -dv --verbose=4 "${signed_path}" 2>&1 || printf '\n(codesign display failed; continuing support collection)\n'
            fi
        done
    else
        printf 'codesign not found.\n'
    fi

    if command -v spctl >/dev/null 2>&1; then
        for assessed_path in \
            "${APP_ROOT}" \
            "${PACKAGE_ROOT}/openQ4-client_arm64" \
            "${PACKAGE_ROOT}/openQ4-client_x64" \
            "${PACKAGE_ROOT}/openQ4-client_x86" \
            "${PACKAGE_ROOT}/openQ4-ded_arm64" \
            "${PACKAGE_ROOT}/openQ4-ded_x64" \
            "${PACKAGE_ROOT}/openQ4-ded_x86"
        do
            if path_exists_for_inspection "${assessed_path}"; then
                printf '\n-- spctl assess: %s --\n' "${assessed_path}"
                spctl --assess --type execute --verbose=4 "${assessed_path}" 2>&1 || printf '\n(spctl assessment failed; continuing support collection)\n'
            fi
        done
    else
        printf 'spctl not found.\n'
    fi

    if command -v xcrun >/dev/null 2>&1; then
        for stapled_path in \
            "${APP_ROOT}"
        do
            if path_exists_for_inspection "${stapled_path}"; then
                printf '\n-- xcrun stapler validate: %s --\n' "${stapled_path}"
                xcrun stapler validate "${stapled_path}" 2>&1 || printf '\n(stapler validation failed; continuing support collection)\n'
            fi
        done
    else
        printf 'xcrun not found; stapler validation skipped.\n'
    fi
} | write_bounded_report "package/signing.txt"

{
    printf 'Collector timestamp UTC: %s\n' "${STAMP}"
    printf 'Package root: %s\n' "${PACKAGE_ROOT}"
    printf 'Only extended-attribute names are listed; values are not copied.\n'
    printf 'Quarantine state is reported by checking whether com.apple.quarantine is present.\n'

    if command -v xattr >/dev/null 2>&1; then
        xattr_names="${WORK_PARENT}/xattr-names.txt"
        for xattr_path in \
            "${PACKAGE_ROOT}" \
            "${APP_ROOT}" \
            "${APP_ROOT}/Contents/MacOS/openQ4" \
            "${APP_RESOURCE_ROOT}/baseoq4" \
            "${APP_FRAMEWORK_ROOT}"/game_*.dylib \
            "${PACKAGE_ROOT}/openQ4-client_arm64" \
            "${PACKAGE_ROOT}/openQ4-client_x64" \
            "${PACKAGE_ROOT}/openQ4-client_x86" \
            "${PACKAGE_ROOT}/openQ4-ded_arm64" \
            "${PACKAGE_ROOT}/openQ4-ded_x64" \
            "${PACKAGE_ROOT}/openQ4-ded_x86" \
            "${PACKAGE_ROOT}/baseoq4" \
            "${PACKAGE_ROOT}/baseoq4"/game_*.dylib \
            "${PACKAGE_ROOT}/collect_macos_support_info.sh"
        do
            if path_exists_for_inspection "${xattr_path}"; then
                printf '\n-- %s --\n' "${xattr_path}"
                if xattr "${xattr_path}" > "${xattr_names}" 2>&1; then
                    if [ -s "${xattr_names}" ]; then
                        cat "${xattr_names}"
                        if grep -qx 'com.apple.quarantine' "${xattr_names}"; then
                            printf 'quarantine: present\n'
                        else
                            printf 'quarantine: absent\n'
                        fi
                    else
                        printf '(no extended attributes)\n'
                        printf 'quarantine: absent\n'
                    fi
                else
                    cat "${xattr_names}"
                    printf '\n(xattr name listing failed; continuing support collection)\n'
                fi
            fi
        done
    else
        printf 'xattr not found.\n'
    fi
} | write_bounded_report "package/quarantine.txt"

copy_text_if_present "${PACKAGE_ROOT}/VERSION.txt" "package/VERSION.txt"
copy_text_if_present "${PACKAGE_ROOT}/openQ4.app/Contents/Resources/VERSION.txt" "package/app-VERSION.txt"
copy_text_if_present "${PACKAGE_ROOT}/SYMBOLS.txt" "package/SYMBOLS.txt"
copy_text_if_present "${PACKAGE_ROOT}/openQ4.app/Contents/Info.plist" "package/Info.plist"
if [ -n "${HOME_DIR}" ]; then
    copy_text_if_present "${HOME_DIR}/Library/Application Support/openQ4/baseoq4/logs/openq4.log" "logs/home-openq4.log"
    copy_text_if_present "${HOME_DIR}/baseoq4/logs/openq4.log" "logs/home-baseoq4-openq4.log"
    copy_text_if_present "${HOME_DIR}/Library/Application Support/openQ4/baseoq4/logs/fatal.txt" "logs/home-fatal.txt"
    copy_text_if_present "${HOME_DIR}/baseoq4/logs/fatal.txt" "logs/home-baseoq4-fatal.txt"
else
    write_text "logs/home-paths-unavailable.txt" \
        "HOME was not set; home-scoped openq4.log files were skipped." \
        "Package-local logs were still inspected when present."
fi
copy_text_if_present "${PACKAGE_ROOT}/baseoq4/logs/openq4.log" "logs/package-baseoq4-openq4.log"
copy_text_if_present "${PACKAGE_ROOT}/baseoq4/logs/fatal.txt" "logs/package-baseoq4-fatal.txt"
copy_text_if_present "${APP_RESOURCE_ROOT}/baseoq4/logs/openq4.log" "logs/app-resource-baseoq4-openq4.log"
copy_text_if_present "${APP_RESOURCE_ROOT}/baseoq4/logs/fatal.txt" "logs/app-resource-baseoq4-fatal.txt"

CRASH_LIST="${WORK_PARENT}/crash-list.txt"
if [ -z "${HOME_DIR}" ]; then
    write_text "crash-reports/README.txt" \
        "HOME was not set; the macOS DiagnosticReports directory could not be located." \
        "The support collector continued without home-scoped crash reports."
else
    CRASH_DIR="${HOME_DIR}/Library/Logs/DiagnosticReports"
    if [ -L "${CRASH_DIR}" ]; then
        write_text "crash-reports/README.txt" \
            "The macOS DiagnosticReports directory is a symlink and was skipped." \
            "The support collector does not follow symlinks when copying crash-report text."
    elif [ -d "${CRASH_DIR}" ]; then
        find "${CRASH_DIR}" -type f \( \
            -name 'openQ4*.ips' -o \
            -name 'openQ4*.crash' -o \
            -name 'openQ4-client*.ips' -o \
            -name 'openQ4-client*.crash' -o \
            -name 'openQ4-ded*.ips' -o \
            -name 'openQ4-ded*.crash' \
        \) -mtime -30 -print | sort | tail -n 10 > "${CRASH_LIST}"
        if [ -s "${CRASH_LIST}" ]; then
            while IFS= read -r crash_path; do
                copy_crash_report_if_safe "${crash_path}"
            done < "${CRASH_LIST}"
        else
            write_text "crash-reports/README.txt" "No matching openQ4 crash reports were found in ~/Library/Logs/DiagnosticReports from the last 30 days."
        fi
    else
        write_text "crash-reports/README.txt" "The macOS DiagnosticReports directory was not found."
    fi
fi

write_text "README.txt" \
    "Review this archive before attaching it to a public issue." \
    "The collector redacts /Users/<name> paths and email-like strings, does not dump the environment, does not launch openQ4, and does not copy retail q4base PK4 assets." \
    "Copied text is sanitized for embedded control characters, and command/report output is stream-limited before redaction so noisy tools cannot inflate the support archive." \
    "The collector does not follow symlinked package, log, or crash-report inputs; skipped symlinks are recorded in the relevant report files." \
    "If HOME is not set, home-scoped logs and DiagnosticReports are skipped with an archive note instead of aborting collection." \
    "system/rosetta.txt records the collector process architecture and sysctl.proc_translated value so unsupported Rosetta/translated reports are easy to spot." \
    "package/build-metadata.txt records package VERSION.txt metadata, app VERSION.txt metadata, openQ4/openQ4-game commit fields when present, and the game module filenames in the app Frameworks directory or a legacy adjacent baseoq4 directory." \
    "package/binary-architecture.txt records file/lipo architecture output for package executables and game modules without launching openQ4." \
    "package/dylib-dependencies.txt records otool dependency and game-module install-name output without launching openQ4." \
    "package/path-resolution.txt records the package root, app path, expected loose runtime paths, and any fs_basepath, fs_cdpath, or fs_savepath lines found in copied logs." \
    "package/signing.txt records codesign, spctl execute assessment, and stapler validation output for package executables and app bundles without launching openQ4." \
    "package/quarantine.txt lists extended-attribute names and com.apple.quarantine presence without copying extended-attribute values." \
    "logs/openal-summary.txt records OpenAL vendor, renderer, version, device, and EFX warning/status lines found in copied logs." \
    "logs/renderer-summary.txt records renderer startup, driver-quirk, ARB2 interaction, and fatal-signal breadcrumbs found in copied logs." \
    "logs/renderer-config.txt records only renderer/performance settings (r_* plus named com settings) from safe saved configs; bindings, player/account, network, audio-device, and arbitrary config settings are excluded." \
    "crash-reports/ includes up to 10 recent matching openQ4, openQ4-client, and openQ4-ded DiagnosticReports files when macOS wrote them." \
    "If package/SYMBOLS.txt is present, include it with any .ips report so maintainers can pick the matching macOS dSYM symbol archive." \
    "For issue #73 style crashes, include full terminal output as text in the issue body too; this archive cannot recover terminal output that was not logged."

COPYFILE_DISABLE=1 tar -czf "${ARCHIVE_TMP}" -C "${WORK_PARENT}" "${BUNDLE_NAME}"
archive_bytes=$(wc -c < "${ARCHIVE_TMP}" 2>/dev/null | tr -d '[:space:]' || printf '0')
case "${archive_bytes}" in
    ''|*[!0123456789]*) archive_bytes=0 ;;
esac
if [ "${archive_bytes}" -le 0 ]; then
    fail "Support archive is empty or unreadable before publish: ${ARCHIVE_TMP}"
fi
if [ "${archive_bytes}" -gt "${MAX_SUPPORT_ARCHIVE_BYTES}" ]; then
    fail "Support archive is too large before publish: ${archive_bytes} bytes (max ${MAX_SUPPORT_ARCHIVE_BYTES})"
fi
if ! COPYFILE_DISABLE=1 tar -tzf "${ARCHIVE_TMP}" >/dev/null 2>&1; then
    fail "Support archive validation failed before publish: ${ARCHIVE_TMP}"
fi
chmod 600 "${ARCHIVE_TMP}"
if [ -L "${ARCHIVE_PATH}" ] || [ -e "${ARCHIVE_PATH}" ]; then
    fail "Support archive target appeared while collecting data: ${ARCHIVE_PATH}"
fi
if ! ln "${ARCHIVE_TMP}" "${ARCHIVE_PATH}"; then
    fail "Support archive target appeared while collecting data: ${ARCHIVE_PATH}"
fi
rm -f "${ARCHIVE_TMP}"
ARCHIVE_TMP=""

printf 'Created %s\n' "${ARCHIVE_PATH}"
printf 'Review the archive before attaching it to a public GitHub issue.\n'
