#!/usr/bin/env python3
"""Static contract for native Linux dedicated-server smoke coverage."""

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


def require_count(haystack: str, needle: str, expected: int, context: str) -> None:
    actual = haystack.count(needle)
    if actual != expected:
        raise AssertionError(f"Expected {expected} occurrence(s) of {needle!r} in {context}, found {actual}")


def main() -> None:
    runner = read("tools/tests/linux_dedicated_server_smoke.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    cross = read(".github/workflows/linux-arm64-cross.yml")
    platform_support = read("docs/dev/platform-support.md")
    cross_docs = read("docs/dev/linux-arm64-cross-compilation.md")
    release_completion = read("docs/dev/release-completion.md")
    session = read("src/framework/Session.cpp")
    renderer = read("src/renderer/RenderSystem_init.cpp")
    meson = read("meson.build")
    meson_sources = read("tools/build/meson_sources.py")
    linux_dedicated = read("src/sys/linux/dedicated.cpp")
    gl_stub = read("src/sys/stub/stub_gl.cpp")
    openal_stub = read("src/sys/stub/stub_openal.cpp")
    glew_meson = read("subprojects/glew/meson.build")
    validator = read("tools/validation/openq4_validate.py")
    release_verifier = read("tools/build/verify_linux_release_artifacts.py")

    for token in (
        'q4base / "pak001.pk4"',
        "zipfile.ZipFile",
        '"fs_validateOfficialPaks", "0"',
        '"g_allowAssetlessStartup", "1"',
        '"si_gameType", "dm"',
        '"s_noSound", "1"',
        '"net_serverDedicated", "1"',
        'IPV4_SELF_TEST_MARKER = "IPv4 network self-test: passed"',
        'CANONICAL_GAMETYPE_MARKER = \'"si_gameType" is:"DM"\'',
        "IPV4_SELF_TEST_MARKER,",
        "CANONICAL_GAMETYPE_MARKER,",
        '"+netIPv4SelfTest"',
        '"+si_gameType"',
        '"IPv4 network self-test: FAILED"',
        '"+wait", "1"',
        '"Selected game module: logical=\'game_mp\'"',
        '"game initialized."',
        '"--- Common Initialization Complete ---"',
        '"Type \'help\' for dedicated server info."',
        '"--------------- Game Shutdown ---------------"',
        '"ERROR:"',
        "subprocess.TimeoutExpired",
    ):
        require(runner, token, "Linux dedicated-server smoke runner")
    command = runner[runner.index("    command = [") : runner.index("\n    ]", runner.index("    command = ["))]
    if not (
        command.index('"si_gameType", "dm"')
        < command.index('"+netIPv4SelfTest"')
        < command.index('"+si_gameType"')
        < command.index('"+echo", READY_MARKER')
    ):
        raise AssertionError("Linux dedicated smoke must test IPv4 and canonical DM before its ready marker")
    reject(runner, "Program Files", "asset-free Linux dedicated-server smoke runner")
    reject(runner, "steamapps", "asset-free Linux dedicated-server smoke runner")
    require(
        session,
        '#ifdef ID_DEDICATED\n\tcommon->Printf( "Dedicated server: skipping client GUI preload.\\n" );\n#else',
        "dedicated session client-GUI exclusion",
    )
    # A dedicated server never has a device. Session init's no-device guard is
    # the client's only and follows the command registrations, so a dedicated
    # server registers rescanSI (which copies its settings into each map it
    # spawns) and allocates the worlds a map needs; without them it could not
    # spawn a map at all.
    init = session[session.index("void idSessionLocal::Init() {") :]
    init = init[: init.index("\n}\n")]
    guard = "#ifndef ID_DEDICATED\n\t// A rejected recoverable restart can leave no device"
    require(init, guard, "client-only session no-device guard")
    require_count(init, "IsOpenGLRunning() ) return;", 1, "session init")
    if not (
        init.index('AddCommand( "rescanSI"')
        < init.index(guard)
        < init.index("IsOpenGLRunning() ) return;")
        < init.index("rw = renderSystem->AllocRenderWorld();")
    ):
        raise AssertionError("session init must register its commands before the client-only no-device guard")
    # The dedicated server never creates a context, so idRenderSystem::Shutdown()
    # tears down a vertex cache that Init() never touched. That used to be
    # guarded at the call site with glConfig.isInitialized, which was wrong in
    # both directions: the flag is set before vertexCache.Init() runs, and
    # ShutdownOpenGL() clears it without freeing the cache. The guard now lives
    # in idVertexCache itself, keyed off the list sentinels.
    vertex_cache = read("src/renderer/VertexCache.cpp")
    require(
        renderer,
        "vertexCache.Shutdown();",
        "dedicated vertex-cache shutdown",
    )
    reject(
        renderer,
        "if ( glConfig.isInitialized ) {\n\t\tvertexCache.Shutdown();\n\t}",
        "call-site vertex-cache shutdown guard (must live inside idVertexCache)",
    )
    require(
        vertex_cache,
        "void idVertexCache::PurgeAll() {\n\tif ( staticHeaders.next == NULL ) {",
        "uninitialized vertex-cache purge guard",
    )
    require(
        vertex_cache,
        # deferredFreeList became one list per vertex frame (8ed86d2d9); Init links
        # every slot, so the current slot's sentinel still marks "never initialized".
        "void idVertexCache::Shutdown() {\n\tif ( staticHeaders.next == NULL || deferredFreeList[listNum].next == NULL ) {",
        "uninitialized vertex-cache shutdown guard",
    )

    dedicated_source_block = meson_sources[
        meson_sources.index("LINUX_DEDICATED_SOURCES") : meson_sources.index("LINUX_X11_HELPER_SOURCES")
    ]
    for source in (
        '"sys/linux/dedicated.cpp"',
        '"sys/stub/stub_gl.cpp"',
        '"sys/stub/stub_openal.cpp"',
    ):
        require(dedicated_source_block, source, "Linux dedicated source split")
    reject(dedicated_source_block, '"sys/linux/linux_sdl3.cpp"', "Linux dedicated source split")
    require(meson_sources, '"--target-kind"', "dedicated-aware Meson source discovery")
    require(meson_sources, 'args.target_kind == "dedicated"', "dedicated-aware Meson source discovery")

    for token in (
        "dedicated_engine_source_paths",
        "openq4_dedicated_sources",
        "dedicated_deps = [",
        "glew_dedicated_dep",
        "openal_dep.partial_dependency(",
        "'-DUSE_SDL3=1', '-DOPENQ4_HAVE_X11_HELPERS=1'",
    ):
        require(meson, token, "Linux dedicated Meson split")
    require_count(meson, "dependencies: dedicated_deps", 3, "dedicated target dependency split")
    # Pin the defines the dedicated GLEW variant depends on, not the exact
    # argument list: it also carries -DGLAPI=extern so the GL-free dedicated
    # targets can satisfy GL 1.1 references from plain stub definitions.
    dedicated_glew_block = glew_meson[glew_meson.index("glew_dedicated_lib = static_library(") :]
    for define in ("'-DGLEW_NO_GLU'", "'-DOPENQ4_GLEW_SDL3_LOADER'"):
        require(dedicated_glew_block, define, "headless dedicated GLEW resolver")

    require(linux_dedicated, "Sys_GetDesktopResolution", "Linux dedicated platform stubs")
    reject(linux_dedicated, '#include "local.h"', "Linux dedicated X11-free platform stubs")
    require(gl_stub, "OpenQ4_GlewGetProcAddress", "Linux dedicated GLEW stub")
    require(gl_stub, "GLimp_SetScreenParms", "Linux dedicated GLimp stubs")
    require(openal_stub, "alcOpenDevice", "Linux dedicated OpenAL stubs")
    require(openal_stub, "alBufferSamplesSOFT", "Linux dedicated OpenAL extension stubs")

    require(
        validator,
        "LINUX_DEDICATED_COMMON_ALLOWED_NEEDED",
        "staged Linux dedicated fail-closed dependency validation",
    )
    require(
        validator,
        "LINUX_DEDICATED_ARCH_ALLOWED_NEEDED",
        "staged Linux dedicated architecture-specific dependency validation",
    )
    require(
        validator,
        "validate_linux_dedicated_runtime_dependencies(root, dedicated_runtime_specs)",
        "staged Linux dedicated dependency validation",
    )
    require(validator, 'staged_binary_arch(binary_path, "game-mp")', "staged MP module dependency validation")
    for dependency in (
        "libc.so.6",
        "libstdc++.so.6",
        "libgcc_s.so.1",
        "libatomic.so.1",
        "libunwind.so.1",
        "ld-linux-x86-64.so.2",
        "ld-linux-aarch64.so.1",
    ):
        require(validator, dependency, "staged Linux dedicated core-runtime allowlist")
    reject(
        validator,
        "LINUX_DEDICATED_FORBIDDEN_NEEDED_PREFIXES",
        "legacy fail-open Linux dedicated dependency blacklist",
    )
    require(
        release_verifier,
        "staged_validator.validate_linux_dedicated_runtime_dependencies(",
        "extracted Linux release dedicated dependency validation",
    )
    require(
        release_verifier,
        "[(dedicated[0], arch), (mp_modules[0], arch)]",
        "extracted Linux release dedicated and MP module dependency validation",
    )

    require_count(commit, "python tools/tests/linux_dedicated_server_smoke.py", 2, "commit validation native x64/ARM64 smoke wiring")
    require(commit, "commit-linux-arm64-dedicated-smoke", "commit validation ARM64 smoke artifact")
    require(commit, "commit-linux-x64-dedicated-smoke", "commit validation x64 smoke artifact")
    require(commit, "python tools/tests/linux_dedicated_server_smoke_contract.py", "commit validation static contract")
    require(commit, "tools/tests/linux_dedicated_server_smoke.py", "commit validation smoke syntax check")

    require_count(push, "python tools/tests/linux_dedicated_server_smoke.py", 1, "push validation Linux matrix smoke wiring")
    require(push, "if: startsWith(matrix.os, 'ubuntu-')", "push validation Linux-only smoke gate")
    require(push, "push-${{ matrix.artifact_name }}-dedicated-smoke", "push validation smoke artifact")
    require(push, "python tools/tests/linux_dedicated_server_smoke_contract.py", "push validation static contract")
    require(push, "tools/tests/linux_dedicated_server_smoke.py", "push validation smoke syntax check")

    require(cross, "builddir-arm64-cross/openQ4-ded_arm64", "ARM64 cross-build dedicated artifact")
    reject(cross, "qemu-aarch64", "ARM64 cross-build runtime substitution")
    require(platform_support, "asset-free dedicated-server startup and clean shutdown", "Linux platform support policy")
    require(cross_docs, "does not execute the cross-built server", "Linux ARM64 cross-build runtime scope")
    require(release_completion, "Linux dedicated-server packages now get a real startup gate", "release completion notes")

    print("linux_dedicated_server_smoke_contract: ok")


if __name__ == "__main__":
    main()
