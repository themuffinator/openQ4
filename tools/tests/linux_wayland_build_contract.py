#!/usr/bin/env python3
"""Static regression checks for native Wayland and Linux ELF build contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(source: str, token: str, context: str) -> None:
    if token not in source:
        raise AssertionError(f"Missing {token!r} in {context}")


def reject(source: str, token: str, context: str) -> None:
    if token in source:
        raise AssertionError(f"Unexpected {token!r} in {context}")


def require_order(source: str, first: str, second: str, context: str) -> None:
    first_index = source.find(first)
    second_index = source.find(second)
    if first_index == -1 or second_index == -1:
        raise AssertionError(f"Missing ordered tokens {first!r} and/or {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def matrix_entry_for_artifact(source: str, artifact_suffix: str) -> str:
    marker = f"artifact_suffix: {artifact_suffix}"
    if source.count(marker) != 1:
        raise AssertionError(
            f"Expected exactly one {marker!r} in native Wayland workflow"
        )
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


def validate_build_machine_tools() -> None:
    meson = read("meson.build")
    sdl_meson = read("subprojects/packagefiles/sdl3/meson.build")

    require(meson, "meson_version: '>=1.6.0'", "root Meson feature floor")
    require(meson, "py = find_program('python', 'python3', native: true", "Windows build-machine Python")
    require(meson, "py = find_program('python3', 'python', native: true", "Unix build-machine Python")
    reject(meson, "find_installation('python3'", "target-machine Python lookup")
    require(sdl_meson, "find_program('wayland-scanner', native: true)", "build-machine Wayland scanner")


def validate_cross_platform_sdl_config_defaults() -> None:
    source = read("subprojects/packagefiles/sdl3/meson.build")
    wrap = read("subprojects/sdl3.wrap")
    first_host_branch = source.find("if host_machine.system() == 'darwin'")
    if first_host_branch == -1:
        raise AssertionError("SDL Meson overlay is missing its first host-system branch")
    unconditional_config = source[:first_host_branch]

    require(wrap, "patch_directory = sdl3", "SDL Meson packagefile overlay")
    for variable in (
        "SDL_LIBDECOR_VERSION_MAJOR",
        "SDL_LIBDECOR_VERSION_MINOR",
        "SDL_LIBDECOR_VERSION_PATCH",
    ):
        require(
            unconditional_config,
            f"cdata.set('{variable}', 0)",
            "cross-platform SDL config defaults",
        )


def validate_wayland_dynamic_loading() -> None:
    source = read("subprojects/packagefiles/sdl3/meson.build")

    for token in (
        "pipewire_dep = dependency('libpipewire-0.3')",
        "sdl_deps += pipewire_dep.partial_dependency(",
        "cdata.set_quoted('SDL_AUDIO_DRIVER_PIPEWIRE_DYNAMIC', 'libpipewire-0.3.so.0')",
        "dependency('wayland-client', version: '>=1.18')",
        "dependency('xkbcommon', version: '>=0.5.0')",
        "dependency('egl')",
        "partial_dependency(",
        "cdata.set('HAVE_LIBDECOR_H', 1)",
        "libdecor_dep.version().split('.')",
        "cdata.set_quoted('SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC', 'libwayland-client.so.0')",
        "cdata.set_quoted('SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_EGL', 'libwayland-egl.so.1')",
        "cdata.set_quoted('SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_CURSOR', 'libwayland-cursor.so.0')",
        "cdata.set_quoted('SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_XKBCOMMON', 'libxkbcommon.so.0')",
        "cdata.set_quoted('SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_LIBDECOR', 'libdecor-0.so.0')",
    ):
        require(source, token, "SDL native Wayland/libdecor configuration")

    reject(source, "SDL_LIBDECOR_VERSION_MAJOR', 99", "fake libdecor version")
    reject(source, "sdl_deps += dependency('libpipewire-0.3')", "hard-linked PipeWire client")
    reject(source, "sdl_deps += dependency('wayland-client')", "hard-linked Wayland client")


def validate_linux_ime_support() -> None:
    source = read("subprojects/packagefiles/sdl3/meson.build")
    backend = read("src/sys/sdl3/sdl3_backend.cpp")
    syscon = read("src/sys/posix/posix_syscon.cpp")
    edit_window = read("src/ui/EditWindow.cpp")
    legacy_gui = read("src/ui/UserInterface.cpp")
    for token in (
        "cdata.set('HAVE_FCITX', 1)",
        "cdata.set('SDL_USE_IME', 1)",
        "dependency('ibus-1.0', required: false)",
        "cdata.set('HAVE_IBUS_IBUS_H', 1)",
    ):
        require(source, token, "SDL Linux IBus/Fcitx support")

    for token in (
        'SDL_HINT_IME_IMPLEMENTED_UI, "none"',
        "consoleAcceptsText || guiAcceptsText",
        "SDL_StartTextInput(s_sdlWindow)",
        "SDL_ClearComposition(s_sdlWindow)",
        "SDL_StopTextInput(s_sdlWindow)",
        "SDL_SetTextInputArea(s_sdlWindow",
        "SDL_EVENT_TEXT_EDITING_CANDIDATES",
        "SDL_StepUTF8(&text, &remaining)",
        "codepoint == SDL_INVALID_UNICODE_CODEPOINT || codepoint > 0xff",
        "idStr::CharIsPrintable(static_cast<byte>(codepoint))",
    ):
        require(backend, token, "SDL native IME lifecycle")

    require(backend, "activeGui->GetTextInputState(area, cursorOffset)", "active GUI text-input interface")
    require(legacy_gui, "dynamic_cast<idEditWindow *>( desktop->GetFocusedChild() )", "legacy focused GUI edit-field IME gate")
    require(edit_window, "idEditWindow::GetTextInputState", "GUI edit-field IME geometry")
    require(edit_window, "cursorPixels - paintOffset", "GUI edit-field caret placement")

    for token in (
        "Posix_ConsoleAppendUTF8",
        "SDL_StepUTF8( &text, &remaining )",
        "codepoint == SDL_INVALID_UNICODE_CODEPOINT || codepoint > 0xff",
        "idStr::CharIsPrintable( static_cast<byte>( codepoint ) )",
        "Posix_ConsoleAppendUTF8( clipboardText, true )",
        "Posix_ConsoleAppendUTF8( text, false )",
    ):
        require(syscon, token, "SDL POSIX system-console UTF-8 input")


def validate_openal_device_event_threading() -> None:
    source = read("src/sound/OpenAL/AL_SoundHardware.cpp")
    precompiled = read("src/idlib/precompiled.h")

    require(precompiled, "#include <atomic>", "precompiled standard-library atomics")
    for token in (
        "static std::atomic<int> openQ4_PendingOpenALDeviceEvents( 0 );",
        "openQ4_PendingOpenALDeviceEvents.fetch_or( flag, std::memory_order_relaxed )",
        "openQ4_PendingOpenALDeviceEvents.exchange( 0, std::memory_order_relaxed )",
    ):
        require(source, token, "OpenAL asynchronous device-event handoff")
    reject(source, "static volatile int openQ4_PendingOpenALDeviceEvents", "OpenAL racy device-event handoff")


def validate_wayland_only_mode() -> None:
    options = read("meson_options.txt")
    meson = read("meson.build")
    sdl_options = read("subprojects/packagefiles/sdl3/meson_options.txt")
    sdl_video = read("subprojects/packagefiles/sdl3/src/video/meson.build")
    glew = read("subprojects/glew/src/glew.c")
    shell_wrapper = read("tools/build/meson_setup.sh")
    powershell_wrapper = read("tools/build/meson_setup.ps1")
    building = read("BUILDING.md")
    release_notes = read("docs/dev/releases/v0.8.1.md")

    require(options, "'linux_x11'", "root native Wayland-only option")
    require(meson, "linux_x11_option.disabled()", "root X11 dependency gating")
    require(meson, "cc.find_library('OpenGL', required: true)", "Wayland EGL OpenGL dispatch library")
    require(meson, "sdl3_default_options += ['x11=disabled']", "SDL X11 option propagation")
    require(meson, "not linux_x11_option.auto()", "explicit Linux SDL feature ownership")
    require(meson, "sdl3_subproject = subproject('sdl3'", "explicit Linux bundled SDL selection")
    require(sdl_options, "'x11'", "SDL X11 feature option")
    require(sdl_video, "if not x11_option.disabled()", "SDL X11 source gating")
    require(shell_wrapper, "platform_backend linux_x11 macos_graphics_bridge", "shell builddir migration preserves Linux X11 mode")
    require(powershell_wrapper, '"linux_x11"', "PowerShell builddir migration preserves Linux X11 mode")
    require(building, "subprojects purge --confirm sdl3", "stale SDL patch-overlay recovery guidance")
    require(building, "stale SDL definitions can silently retain X11 libraries", "stale Wayland-only dependency warning")
    require(release_notes, "Meson does not reapply a changed", "SDL patch-overlay upgrade note")
    if glew.count("!defined(OPENQ4_GLEW_SDL3_LOADER)") < 2:
        raise AssertionError("GLEW SDL loader must exclude both GLX header and implementation branches")


def validate_arm64_wayland_ci() -> None:
    workflow = read(".github/workflows/commit-validation.yml")

    for token in (
        "runs-on: ${{ matrix.runner }}",
        "CC: ${{ matrix.cc }}",
        "CXX: ${{ matrix.cxx }}",
        "prefer_libdecor: \"1\"",
        "OPENQ4_WAYLAND_PREFER_LIBDECOR=1",
        "bash tools/validation/validate_pr.sh \\\n            --build-dir builddir \\",
        "--extra-setup-arg=-Dlinux_x11=${{ matrix.linux_x11 }}",
        "#define HAVE_LIBDECOR_H 1",
        "LD_DEBUG_OUTPUT",
        "libdecor loaded despite OPENQ4_WAYLAND_DISABLE_LIBDECOR=1",
        "Native Wayland-only binary unexpectedly depends on X11/GLX",
    ):
        require(workflow, token, "ARM64 native Wayland workflow")

    arm64_clang = matrix_entry_for_artifact(workflow, "arm64-clang-no-libdecor-wayland-only")
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
        require(arm64_clang, token, "ARM64 Clang native Wayland-only matrix entry")

    arm64_gcc = matrix_entry_for_artifact(workflow, "arm64-libdecor")
    for token in (
        "arch_label: ARM64",
        "runner: ubuntu-24.04-arm",
        "expected_uname: aarch64",
        "compiler_label: GCC",
        "cc: gcc",
        "cxx: g++",
        "libdecor_label: libdecor",
    ):
        require(arm64_gcc, token, "ARM64 GCC native Wayland matrix entry")


def validate_elf_contract() -> None:
    meson = read("meson.build")
    module_meson = read("content/baseoq4/meson.build")
    export_map = read("tools/build/linux_game_module.map")
    validator = read("tools/validation/openq4_validate.py")
    require(meson, "'-Wl,-z,defs'", "Linux game-module defined-import policy")
    require(meson, "'-Wl,--version-script=' + linux_game_module_export_map[0].full_path()", "Linux game-module export map")
    require(module_meson, "link_depends: game_module_link_depends", "Linux game-module export-map relink dependency")
    require(export_map, "GetGameAPI;", "Linux game-module public API")
    require(export_map, "local:", "Linux game-module local symbol policy")
    require(export_map, "*;", "Linux game-module default-local symbol policy")
    for token in (
        '"arm64": ("ELF64", "AArch64")',
        '"x64": ("ELF64", "Advanced Micro Devices X86-64")',
        "Linux ELF architecture does not match its staged name",
        '["--wide", "--dyn-syms"]',
        'fields[4] in {"GLOBAL", "WEAK", "UNIQUE"}',
        'fields[5] in {"DEFAULT", "PROTECTED"}',
        "len(public_symbols) == 1",
        'public_symbols[0][4] == "GetGameAPI"',
        'readelf_env["LC_ALL"] = "C"',
        "Linux game module must expose exactly one GLOBAL/DEFAULT/FUNC",
        "linux_binary_specs",
    ):
        require(validator, token, "Linux staged ELF validation")


def validate_alignment_contract() -> None:
    sys_public = read("src/sys/sys_public.h")
    require(sys_public, "__attribute__((aligned(16))) x", "Linux ALIGN16 declaration")


def validate_multiple_instance_benchmark() -> None:
    benchmark = read("tools/tests/renderer_gameplay_benchmark.py")
    common = read("src/framework/Common.cpp")
    posix = read("src/sys/posix/posix_main.cpp")

    require(
        benchmark,
        'multiple_instance_cvar = "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances"',
        "platform-specific gameplay benchmark multi-instance cvar",
    )
    require(benchmark, 'append_set(args, multiple_instance_cvar, "1")', "gameplay benchmark multi-instance launch override")
    require(posix, 'idCVar sys_allowMultipleInstances( "sys_allowMultipleInstances", "0"', "POSIX multi-instance cvar")
    require(posix, "if ( sys_allowMultipleInstances.GetBool() || posix_instanceLockFd != -1 )", "POSIX instance-lock bypass")
    require_order(common, "StartupVariable( NULL, false );", "Sys_AlreadyRunning()", "command-line cvars before instance lock")


def main() -> None:
    validate_build_machine_tools()
    validate_cross_platform_sdl_config_defaults()
    validate_wayland_dynamic_loading()
    validate_linux_ime_support()
    validate_openal_device_event_threading()
    validate_wayland_only_mode()
    validate_arm64_wayland_ci()
    validate_elf_contract()
    validate_alignment_contract()
    validate_multiple_instance_benchmark()
    print("linux_wayland_build_contract: ok")


if __name__ == "__main__":
    main()
