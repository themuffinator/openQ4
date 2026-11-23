# Linux ARM64 cross-compilation

openQ4 has a canonical supplementary cross-build from an Ubuntu 24.04 x64 build machine to a GNU/Linux ARM64 target. It compiles the client, dedicated server, and both in-tree game modules with the SDL3 native-Wayland path. Native ARM64 CI remains authoritative for build/package evidence and assetless Weston/Xvfb window, compositor, and input-startup behavior. The current hosted runs do not prove audio or real SP/MP gameplay, so Linux ARM64 release packages remain preview. First-class release signoff requires the physical-machine tests and review recorded in [the Linux ARM64 evidence record](linux-arm64-signoff-evidence.md).

The checked-in Meson machine file is `tools/cross/linux-arm64.ini`. It is intentionally specific to Debian/Ubuntu multiarch directory conventions so that an x64 package can never satisfy an ARM64 target dependency by accident. The corresponding package set is `tools/cross/ubuntu-linux-arm64-packages.txt`, and `.github/workflows/linux-arm64-cross.yml` is the executable reference setup.

## Build-machine setup

On an Ubuntu 24.04 x64 machine, enable the `arm64` dpkg architecture. Keep the existing Ubuntu archive entries restricted to `amd64`, then add `http://ports.ubuntu.com/ubuntu-ports` entries restricted to `arm64`. The workflow demonstrates the required deb822 source entries. After `apt-get update`, install the non-comment lines from the package manifest:

```bash
mapfile -t packages < <(sed -e '/^[[:space:]]*#/d' -e '/^[[:space:]]*$/d' tools/cross/ubuntu-linux-arm64-packages.txt)
sudo apt-get install -y --no-install-recommends "${packages[@]}"
python3 -m pip install meson ninja
```

The engine checkout contains the canonical SP/MP sources in `src/game/` and `src/mpgame/`. Meson compiles them directly and writes their source inventory to the build directory. No companion checkout or source staging is required; `-Dbuild_engine=false` supports a standalone game-module build using the same sources and headers.

## Configure and compile

From the openQ4 repository root:

```bash
export PKG_CONFIG_ALLOW_CROSS=1

bash tools/build/meson_setup.sh setup --wipe builddir-arm64-cross . \
  --backend ninja \
  --buildtype=debugoptimized \
  --wrap-mode=forcefallback \
  --cross-file tools/cross/linux-arm64.ini \
  -Dplatform_backend=sdl3 \
  -Dlinux_x11=disabled \
  -Duse_pch=false

bash tools/build/meson_setup.sh compile -C builddir-arm64-cross
```

This configuration is Wayland/EGL-only on purpose. It proves that ARM64 does not acquire an undeclared dependency on x64 X11/GLX development files. Native ARM64 CI separately builds and exercises assetless X11/Xvfb fallback startup.

Expected outputs are:

- `builddir-arm64-cross/openQ4-client_arm64`
- `builddir-arm64-cross/openQ4-ded_arm64`
- `builddir-arm64-cross/baseoq4/game-sp_arm64.so`
- `builddir-arm64-cross/baseoq4/game-mp_arm64.so`

The reference workflow performs a real build of all four outputs rather than a configure-only check. It requires AArch64 ELF64 identity for every artifact, PIE for the client and dedicated server, RELRO plus BIND_NOW and a non-executable stack for all four, no X11 dependency in the Wayland-only client, and no direct OpenAL, OpenGL/GLX, SDL, PipeWire, Wayland, or X11/Xext dependency in the dedicated server. It also requires different SP/MP module bytes and exactly one defined `GLOBAL`/`DEFAULT` `GetGameAPI` export from each module. Use `readelf -h`, `readelf -lW`, `readelf -dW`, and `readelf --wide --dyn-syms` for the same local checks.

Native ARM64 jobs in Push Verification and Commit Validation build every change. The cross build adds only the x64-hosted toolchain path, which depends on the build description far more than on the sources, so it runs when a `meson.build`, `meson_options.txt`, a subproject, `tools/cross/`, the Meson wrapper, the validator or the workflow itself changes, every Monday, and on manual dispatch.

## Build tools versus target tools

Python source discovery, package generation, and `wayland-scanner` run on the x64 build machine. C, C++, archive, strip, compile checks, and dependency resolution target ARM64. Any new generator added to Meson must therefore be found with `native: true`; target utilities must come from the cross file.

The canonical file does not set `sys_root`. Ubuntu multiarch target packages use absolute `/usr/lib/aarch64-linux-gnu` and `/usr/include` paths, while `pkg_config_libdir` prevents host-architecture `.pc` files from entering resolution. For a dedicated SDK sysroot, copy the machine file and change the compiler paths, `sys_root`, and `pkg_config_libdir` together. Do not combine a partial sysroot with Ubuntu multiarch metadata.

No executable wrapper is configured. Cross compilation is a build-time safety net, not a substitute for a native ARM64 run under Wayland. A local QEMU wrapper can help investigate CPU-only startup, but it does not validate GPU, compositor, input, audio, or gameplay compatibility and must not be used as release sign-off evidence. Likewise, hosted assetless Weston/Xvfb startup is useful regression coverage, but only real-hardware SP/MP gameplay with stock assets plus working audio and input devices can close the Linux ARM64 release gate.

The cross-build workflow does not execute the cross-built server because no target wrapper is configured; native ARM64 commit and push jobs run the assetless dedicated-server smoke instead. Neither substitutes for the stock-map dedicated-server test required by [the first-class signoff evidence record](linux-arm64-signoff-evidence.md).
