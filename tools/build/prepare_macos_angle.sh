#!/usr/bin/env bash
# Stage an ANGLE (OpenGL ES on Metal/GL) runtime for local macOS GLES work.
#
# macOS ships no OpenGL ES. ANGLE provides libEGL/libGLESv2 so the GLES render
# path can be built and run on a development Mac instead of only on an Android
# device. This is a LOCAL DEVELOPMENT AID: the binaries are sourced from an
# installed Google Chrome, which is not a redistributable source. Shipping a
# GLES build would need an ANGLE built from source and pinned/verified the way
# tools/build/prepare_macos_moltenvk.sh pins MoltenVK.
#
# The staged copies get an @rpath install name so a target can LINK against
# them. That matters: openQ4's GL 1.x entry points bind at link time, so a
# GLES build must resolve them from libGLESv2 rather than OpenGL.framework.
#
# Usage:
#   tools/build/prepare_macos_angle.sh [--output-dir DIR]

set -euo pipefail

LOG_PREFIX="prepare_macos_angle"
CHROME_LIBS="/Applications/Google Chrome.app/Contents/Frameworks/Google Chrome Framework.framework/Libraries"
ANGLE_LIBS=("libEGL.dylib" "libGLESv2.dylib")

log()  { printf '%s: %s\n' "${LOG_PREFIX}" "$*"; }
die()  { printf '%s: error: %s\n' "${LOG_PREFIX}" "$*" >&2; exit 1; }

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output_dir=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --output-dir) [ "$#" -ge 2 ] || die "--output-dir requires an argument."
                      output_dir="$2"; shift 2 ;;
        --output-dir=*) output_dir="${1#*=}"; shift ;;
        -h|--help) sed -n '2,18p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

[ -n "${output_dir}" ] || output_dir="${repo_root}/.tmp/angle-macos"

[ -d "${CHROME_LIBS}" ] || die "Google Chrome not found; no ANGLE source available at:
       ${CHROME_LIBS}"

mkdir -p "${output_dir}"

for lib in "${ANGLE_LIBS[@]}"; do
    src="${CHROME_LIBS}/${lib}"
    dst="${output_dir}/${lib}"
    [ -f "${src}" ] || die "missing ${lib} in Chrome's Libraries directory."

    cp -f "${src}" "${dst}"
    chmod u+w "${dst}"

    # Chrome ships these with a "./<name>" install name, which resolves against
    # the process working directory rather than the loading binary. Rewrite to
    # @rpath so linked consumers find them via their own runpath.
    install_name_tool -id "@rpath/${lib}" "${dst}" 2>/dev/null || \
        die "install_name_tool failed to set the install name on ${lib}."

    # libEGL references libGLESv2 by the same relative name; retarget it too.
    for dep in "${ANGLE_LIBS[@]}"; do
        if otool -L "${dst}" | grep -q "\./${dep}"; then
            install_name_tool -change "./${dep}" "@rpath/${dep}" "${dst}" 2>/dev/null || true
        fi
    done

    # install_name_tool invalidates the existing signature; re-sign ad-hoc so
    # dyld will load the library on Apple Silicon.
    codesign --force --sign - "${dst}" 2>/dev/null || \
        die "ad-hoc codesign failed for ${lib}."

    log "staged ${lib} ($(otool -D "${dst}" | tail -1))"
done

log "ANGLE staged to ${output_dir}"
log "ANGLE is a translation layer for local development, not a shippable openQ4 dependency."
