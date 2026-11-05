#!/usr/bin/env bash
# Build the standalone SDL Android host; signing is a separate CI step.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
project_root="$(pwd)"
task_root="${project_root}/.tmp/android-release"
version="${OPENQ4_ANDROID_VERSION:?Set OPENQ4_ANDROID_VERSION}"
if [[ ! "${version}" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
    echo 'Android release version must have three numeric components.' >&2
    exit 1
fi
mkdir -p "${task_root}/sources" "${task_root}/output"

# Use the same patched, checksum-pinned SDL source as the desktop build.
python -m mesonbuild.mesonmain subprojects download sdl3
sdl_dir="$(python -c 'import configparser; c=configparser.ConfigParser(); c.read("subprojects/sdl3.wrap"); print(c["wrap-file"]["directory"])')"
sdl_source="${project_root}/subprojects/${sdl_dir}"

# The desktop OpenAL Soft release; it needs NDK r30's libc++ (r27d lacked the
# ranges support it uses). Ship the exact source alongside the APK (LGPL).
openal_version=1.25.2
openal_sha256=fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894
openal_archive="${task_root}/sources/openal-soft-${openal_version}.tar.gz"
curl --fail --location --retry 3 \
    "https://github.com/kcat/openal-soft/archive/refs/tags/${openal_version}.tar.gz" -o "${openal_archive}"
printf '%s  %s\n' "${openal_sha256}" "${openal_archive}" | sha256sum --check --strict
tar -xzf "${openal_archive}" -C "${task_root}/sources"
python tools/build/prepare_android_deps.py --ndk "${ANDROID_NDK_HOME}" \
    --sdl-source "${sdl_source}" --openal-source "${task_root}/sources/openal-soft-${openal_version}" \
    --prefix "${task_root}/deps" --build-root "${task_root}/dependency-builds" --api 24 --jobs 4
python tools/build/android_cross.py --ndk "${ANDROID_NDK_HOME}" --api 24 --out "${task_root}/arm64.ini"
# A release build ships the same content packs as every other platform.
prebuilt_args=()
if [ -n "${OPENQ4_PREBUILT_CONTENT_PACKS:-}" ]; then
    prebuilt_args=("-Dprebuilt_content_packs=${OPENQ4_PREBUILT_CONTENT_PACKS}")
fi
bash tools/build/meson_setup.sh setup builddir/android-arm64 \
    --cross-file "${task_root}/arm64.ini" --buildtype=release -Db_ndebug=true \
    --wrap-mode=forcefallback "-Dandroid_deps_root=${task_root}/deps" \
    -Dversion_track=stable "-Dversion_base_override=${version}" "${prebuilt_args[@]}"
bash tools/build/meson_setup.sh compile -C builddir/android-arm64 -j 4
bash tools/build/meson_setup.sh install -C builddir/android-arm64 --no-rebuild --skip-subprojects
python tools/build/verify_release_source_provenance.py --project-root "${project_root}" \
    --expected-project-commit "${GITHUB_SHA:?Set GITHUB_SHA}" --build-dir builddir/android-arm64
"${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" \
    --strip-unneeded .install/android/arm64-v8a/lib/*.so
python tools/build/android_elf.py --api 24 .install/android/arm64-v8a/lib/*.so
gradle --no-daemon -p mobile/android --project-cache-dir="${task_root}/gradle-project" \
    "-Psdl3SourceRoot=${sdl_source}" "-Popenq4Stage=${project_root}/.install/android/arm64-v8a" assembleRelease

# Include the actual patched SDL tree, untouched OpenAL source and the build
# recipes. Rebuilders can replace libraries and sign their own APK.
source_package="${task_root}/output/openq4-${version}-android-dependency-sources.tar.xz"
tar -cJf "${source_package}" \
    -C "${task_root}/sources" "openal-soft-${openal_version}.tar.gz" \
    -C "${project_root}/subprojects" "${sdl_dir}" \
    -C "${project_root}" tools/build/prepare_android_deps.py tools/build/android_cross.py \
    tools/build/android_elf.py tools/build/build_android_release.sh docs/dev/android-build.md mobile/android/README.md
