#!/usr/bin/env python3
"""Emit Meson source paths for openQ4 targets."""

from __future__ import annotations

import argparse
import pathlib
import sys


GAME_SOURCES = [
    "game/Actor.cpp",
    "game/AF.cpp",
    "game/AFEntity.cpp",
    "game/BrittleFracture.cpp",
    "game/Camera.cpp",
    "game/Effect.cpp",
    "game/Entity.cpp",
    "game/FreeView.cpp",
    "game/GameEdit.cpp",
    "game/Game_Debug.cpp",
    "game/Game_local.cpp",
    "game/Game_Log.cpp",
    "game/Game_network.cpp",
    "game/Game_render.cpp",
    "game/Healing_Station.cpp",
    "game/Icon.cpp",
    "game/IconManager.cpp",
    "game/IK.cpp",
    "game/Instance.cpp",
    "game/Item.cpp",
    "game/Light.cpp",
    "game/LipSync.cpp",
    "game/Misc.cpp",
    "game/Moveable.cpp",
    "game/Mover.cpp",
    "game/MultiplayerGame.cpp",
    "game/Playback.cpp",
    "game/Player.cpp",
    "game/PlayerView.cpp",
    "game/Player_Cheats.cpp",
    "game/Player_States.cpp",
    "game/Projectile.cpp",
    "game/Pvs.cpp",
    "game/SecurityCamera.cpp",
    "game/Sound.cpp",
    "game/spawner.cpp",
    "game/SplineMover.cpp",
    "game/Target.cpp",
    "game/TramGate.cpp",
    "game/Trigger.cpp",
    "game/Weapon.cpp",
    "game/WorldSpawn.cpp",
]

ENGINE_SOURCE_GLOBS = [
    "aas/*.cpp",
    "cm/*.cpp",
    "framework/*.cpp",
    "framework/async/*.cpp",
    "idlib/*.cpp",
    "idlib/bv/*.cpp",
    "idlib/containers/*.cpp",
    "idlib/geometry/*.cpp",
    "idlib/hashing/*.cpp",
    "idlib/math/*.cpp",
    "renderer/*.cpp",
    "renderer/OpenGL/*.cpp",
    "tools/compilers/dmap/*.cpp",
    "sound/*.cpp",
    "sound/OpenAL/*.cpp",
    "sys/*.cpp",
    "sys/sdl3/TextClipboard.cpp",
    "ui/*.cpp",
]

# CPU image utilities shared between the engine and (post module-split) the
# renderer modules; built as the openq4_imagetools static library
IMAGETOOLS_SOURCE_GLOBS = [
    "imagetools/*.cpp",
    "imagetools/Color/*.cpp",
    "imagetools/DXT/*.cpp",
    "imagetools/ETC/*.cpp",
    "imagetools/jpeg-6/*.c",
]

# CPU render-geometry shared between the engine (dmap) and the renderer;
# built as the openq4_render_geo static library
RENDER_GEO_SOURCE_GLOBS = [
    "render_geo/*.cpp",
]

# renderer sources for the renderer-gl dynamic module; the loader stays
# engine-side and the module glue TU (RendererGLModule.cpp) activates via
# OPENQ4_RENDERER_GL_MODULE
RENDERER_GL_SOURCE_GLOBS = [
    "renderer/*.cpp",
    "renderer/OpenGL/*.cpp",
]

RENDERER_GL_EXCLUDED_SOURCES = (
    "src/renderer/RendererModule.cpp",
)

# renderer sources for the renderer-vk dynamic module (Phase C): the shared
# front-end plus renderer/Vulkan/*, minus the loader and the GL-backend TUs
# (replaced by Vulkan equivalents; mixed front-end TUs keep their GL call
# sites satisfied by the module's GL stub TU + hook-resolving GLEW flavor)
RENDERER_VK_SOURCE_GLOBS = [
    "renderer/*.cpp",
    "renderer/Vulkan/*.cpp",
]

# Query/timestamp plumbing is a required Vulkan module input, not an optional
# source that may silently disappear while the surrounding directory still
# satisfies the broad discovery glob.
RENDERER_VK_REQUIRED_SOURCES = (
    "renderer/Vulkan/VulkanGpuFrameTiming.cpp",
)

RENDERER_VK_EXCLUDED_SOURCES = (
    "src/renderer/RendererModule.cpp",
    # GL backend, replaced wholesale
    "src/renderer/draw_arb2.cpp",
    "src/renderer/draw_common.cpp",
    "src/renderer/tr_backend.cpp",
    "src/renderer/tr_render.cpp",
    "src/renderer/tr_rendertools.cpp",
    "src/renderer/ModernGLExecutor.cpp",
    "src/renderer/ModernGLShaderLibrary.cpp",
    "src/renderer/ModernLightImageAtlas.cpp",
    "src/renderer/ModernSpecularProbeAtlas.cpp",
    "src/renderer/RenderGraphResources.cpp",
    "src/renderer/GLStateCache.cpp",
    "src/renderer/GLDebugScope.cpp",
    "src/renderer/RendererUpload.cpp",
)

# renderer sources for the renderer-gles dynamic module: the shared front-end
# plus renderer/GLES/*, minus the loader and the fixed-function draw paths.
#
# Differs from renderer_vk in two ways that are the whole point of the module:
#   - the ModernGL translation units are KEPT, not replaced. GLES is still GL,
#     so ModernGLExecutor, the shader library, the state cache, the upload
#     manager and the render-graph resources are reused as-is.
#   - tr_backend.cpp is KEPT. The symbols it still references from the dropped
#     TUs are supplied by renderer/GLES/gles_Backend.cpp, so the module reuses
#     the real frame loop instead of writing its own.
#   - renderer/GLES_D3/* is the second backend in this module: a Doom
#     3-shaped ES 3.0 path (BE_GLES_D3, `r_renderer glesd3`) that renders the
#     view itself instead of deferring to the ModernGL executor. Opt-in, so
#     BE_MODERN remains the default ES backend.
RENDERER_GLES_SOURCE_GLOBS = [
    "renderer/*.cpp",
    "renderer/OpenGL/*.cpp",
    "renderer/GLES/*.cpp",
    "renderer/GLES_D3/*.cpp",
    # one translation unit per shader stage, d3es/neo/renderer/glsl style
    "renderer/GLES_D3/glsl/*.cpp",
]

RENDERER_GLES_EXCLUDED_SOURCES = (
    "src/renderer/RendererModule.cpp",
    # fixed-function and ARB-assembly draw paths; no ES equivalent at any version
    "src/renderer/draw_arb2.cpp",
    "src/renderer/draw_common.cpp",
    "src/renderer/tr_render.cpp",
    "src/renderer/tr_rendertools.cpp",
    # the standalone clear probe is its own executable, not part of the module
    "src/renderer/GLES/gles_clear_probe.cpp",
)

GAME_SOURCE_GLOBS = [
    "game/ai/*.cpp",
    "game/anim/*.cpp",
    "game/bots/*.cpp",
    "game/client/*.cpp",
    "game/gamesys/*.cpp",
    "game/mp/*.cpp",
    "game/mp/stats/*.cpp",
    "game/physics/*.cpp",
    "game/script/*.cpp",
    "game/vehicle/*.cpp",
    "game/weapon/*.cpp",
]

LEGACY_WIN32_SOURCES = {
    "src/sys/win32/win_glimp.cpp",
    "src/sys/win32/win_input.cpp",
    "src/sys/win32/win_wndproc.cpp",
}

SDL3_WIN32_SOURCES = {
    "src/sys/win32/win_sdl3.cpp",
}

SDL3_LINUX_SOURCES = (
    "sys/posix/posix_main.cpp",
    "sys/posix/posix_net.cpp",
    "sys/posix/posix_signal.cpp",
    "sys/posix/posix_syscon.cpp",
    "sys/posix/posix_threads.cpp",
    "sys/linux/linux_sdl3.cpp",
    "sys/linux/main.cpp",
    "sys/linux/stack.cpp",
)

LINUX_DEDICATED_SOURCES = (
    "sys/posix/posix_main.cpp",
    "sys/posix/posix_net.cpp",
    "sys/posix/posix_signal.cpp",
    "sys/posix/posix_syscon.cpp",
    "sys/posix/posix_threads.cpp",
    "sys/linux/dedicated.cpp",
    "sys/linux/main.cpp",
    "sys/linux/stack.cpp",
    "sys/stub/stub_gl.cpp",
    "sys/stub/stub_openal.cpp",
)

LINUX_X11_HELPER_SOURCES = (
    "sys/linux/libXNVCtrl/NVCtrl.c",
)

SDL3_DARWIN_SOURCES = (
    "sys/posix/posix_main.cpp",
    "sys/posix/posix_net.cpp",
    "sys/posix/posix_signal.cpp",
    "sys/posix/posix_syscon.cpp",
    "sys/posix/posix_threads.cpp",
    "sys/osx/macosx_compat.mm",
    "sys/osx/macosx_misc.mm",
    "sys/osx/macosx_sdl3.cpp",
    "sys/osx/macosx_sdl3_main.cpp",
)

LINUX_PLATFORM_SOURCES = (
    "sys/posix/posix_input.cpp",
    "sys/posix/posix_main.cpp",
    "sys/posix/posix_net.cpp",
    "sys/posix/posix_signal.cpp",
    "sys/posix/posix_syscon.cpp",
    "sys/posix/posix_threads.cpp",
    "sys/linux/glimp.cpp",
    "sys/linux/input.cpp",
    "sys/linux/main.cpp",
    "sys/linux/stack.cpp",
    "sys/linux/libXNVCtrl/NVCtrl.c",
)

# Android shares POSIX networking/threads but owns its app lifecycle and logging.
SDL3_ANDROID_SOURCES = (
    "sys/posix/posix_main.cpp",
    "sys/posix/posix_net.cpp",
    "sys/posix/posix_signal.cpp",
    "sys/posix/posix_syscon.cpp",
    "sys/posix/posix_threads.cpp",
    "sys/android/android_log.cpp",
    "sys/android/android_main.cpp",
    "sys/android/android_sdl3.cpp",
)

DARWIN_PLATFORM_SOURCES = (
    "sys/posix/posix_input.cpp",
    "sys/posix/posix_main.cpp",
    "sys/posix/posix_net.cpp",
    "sys/posix/posix_signal.cpp",
    "sys/posix/posix_syscon.cpp",
    "sys/posix/posix_threads.cpp",
    "sys/osx/macosx_compat.mm",
    "sys/osx/macosx_event.mm",
    "sys/osx/macosx_glimp.mm",
    "sys/osx/macosx_misc.mm",
    "sys/osx/macosx_sys.mm",
)


class SourceListError(RuntimeError):
    pass


def add_source(
    source_set: set[str], ordered_sources: list[str], rel_path: pathlib.Path
) -> None:
    normalized = rel_path.as_posix()
    if normalized not in source_set:
        source_set.add(normalized)
        ordered_sources.append(normalized)


def remove_source(
    source_set: set[str], ordered_sources: list[str], normalized: str
) -> None:
    if normalized not in source_set:
        return
    source_set.remove(normalized)
    ordered_sources[:] = [path for path in ordered_sources if path != normalized]


def require_source_file(full_path: pathlib.Path) -> None:
    if full_path.is_symlink():
        raise SourceListError(f"Refusing symlinked source file: {full_path}")
    if not full_path.is_file():
        raise SourceListError(f"Missing expected source file: {full_path}")


def add_required_source(
    source_set: set[str],
    ordered_sources: list[str],
    source_root: pathlib.Path,
    rel_path: str,
) -> None:
    full_path = source_root / rel_path
    require_source_file(full_path)
    add_source(source_set, ordered_sources, pathlib.Path("src") / rel_path)


def add_globbed_sources(
    source_set: set[str],
    ordered_sources: list[str],
    source_root: pathlib.Path,
    pattern: str,
) -> None:
    for match in sorted(source_root.glob(pattern)):
        if match.is_symlink():
            raise SourceListError(f"Refusing symlinked source file: {match}")
        if match.is_file():
            add_source(
                source_set,
                ordered_sources,
                pathlib.Path("src") / match.relative_to(source_root),
            )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--host-system",
        choices=("windows", "linux", "darwin", "android"),
        default="windows",
        help="Meson host system for source selection.",
    )
    parser.add_argument(
        "--platform-backend",
        choices=("sdl3", "legacy_win32", "native"),
        default="sdl3",
        help="Platform backend source selection (Windows: sdl3/legacy_win32, Linux/macOS: sdl3/native).",
    )
    parser.add_argument(
        "--include-game",
        choices=("true", "false"),
        default="true",
        help="Include src/game sources in output.",
    )
    parser.add_argument(
        "--linux-x11-helpers",
        choices=("true", "false"),
        default="false",
        help="Include optional X11 helper sources for Linux SDL3 builds.",
    )
    parser.add_argument(
        "--target-kind",
        choices=("client", "dedicated"),
        default="client",
        help="Select client or dedicated-server platform sources.",
    )
    parser.add_argument(
        "--emit",
        choices=("engine", "imagetools", "render_geo", "renderer_gl", "renderer_vk", "renderer_gles"),
        default="engine",
        help="Emit engine target sources or one of the split library/module source lists.",
    )
    parser.add_argument(
        "--renderer",
        choices=("static", "module"),
        default="static",
        help=(
            "Renderer linkage for the engine emit. 'module' sheds the renderer "
            "sources (the client loads renderer-gl_<arch> instead), keeping only "
            "the engine-side loader TU."
        ),
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    if args.host_system == "android" and (
        args.platform_backend != "sdl3" or args.target_kind != "client"
    ):
        print("Android requires the SDL3 client target.", file=sys.stderr)
        return 1

    repo_root = pathlib.Path(__file__).resolve().parents[2]
    source_root = repo_root / "src"

    if not source_root.is_dir():
        print(f"Missing source root: {source_root}", file=sys.stderr)
        return 1

    source_set: set[str] = set()
    ordered_sources: list[str] = []

    include_game = args.include_game == "true"

    if args.emit in ("imagetools", "render_geo", "renderer_gl", "renderer_vk", "renderer_gles"):
        globs = {
            "imagetools": IMAGETOOLS_SOURCE_GLOBS,
            "render_geo": RENDER_GEO_SOURCE_GLOBS,
            "renderer_gl": RENDERER_GL_SOURCE_GLOBS,
            "renderer_vk": RENDERER_VK_SOURCE_GLOBS,
            "renderer_gles": RENDERER_GLES_SOURCE_GLOBS,
        }[args.emit]
        try:
            for pattern in globs:
                add_globbed_sources(source_set, ordered_sources, source_root, pattern)
        except SourceListError as exc:
            print(exc, file=sys.stderr)
            return 1
        if args.emit == "renderer_gl":
            for path in RENDERER_GL_EXCLUDED_SOURCES:
                remove_source(source_set, ordered_sources, path)
        if args.emit == "renderer_gles":
            for path in RENDERER_GLES_EXCLUDED_SOURCES:
                remove_source(source_set, ordered_sources, path)
        if args.emit == "renderer_vk":
            try:
                for rel_path in RENDERER_VK_REQUIRED_SOURCES:
                    add_required_source(source_set, ordered_sources, source_root, rel_path)
            except SourceListError as exc:
                print(exc, file=sys.stderr)
                return 1
            for path in RENDERER_VK_EXCLUDED_SOURCES:
                remove_source(source_set, ordered_sources, path)
        if not ordered_sources:
            print(f"No {args.emit} source files discovered.", file=sys.stderr)
            return 1
        print("\n".join(ordered_sources))
        return 0

    try:
        if include_game:
            for rel in GAME_SOURCES:
                add_required_source(source_set, ordered_sources, source_root, rel)

        for pattern in ENGINE_SOURCE_GLOBS:
            add_globbed_sources(source_set, ordered_sources, source_root, pattern)

        # The SDL client links the owned input driver explicitly in Meson.
        # Dedicated and other platform targets use the callback-free facade.
        if args.target_kind == "client" and args.platform_backend == "sdl3":
            remove_source(source_set, ordered_sources, "src/framework/NativeInputDispatchDisabled.cpp")

        if include_game:
            for pattern in GAME_SOURCE_GLOBS:
                add_globbed_sources(source_set, ordered_sources, source_root, pattern)

        if args.host_system == "windows":
            for match in sorted(source_root.glob("sys/win32/*.cpp")):
                if match.is_symlink():
                    raise SourceListError(f"Refusing symlinked source file: {match}")
                if match.is_file():
                    add_source(
                        source_set,
                        ordered_sources,
                        pathlib.Path("src") / match.relative_to(source_root),
                    )

            if args.platform_backend == "sdl3":
                for path in LEGACY_WIN32_SOURCES:
                    remove_source(source_set, ordered_sources, path)
                for path in SDL3_WIN32_SOURCES:
                    add_required_source(
                        source_set,
                        ordered_sources,
                        repo_root / "src",
                        pathlib.Path(path).relative_to("src").as_posix(),
                    )
                if args.target_kind == "dedicated":
                    # Phase B7: the Windows ded links no GL; win_qgl's loader
                    # carries hard opengl32 imports and is replaced by the
                    # GL 1.1 stub TU
                    remove_source(source_set, ordered_sources, "src/sys/win32/win_qgl.cpp")
                    add_required_source(
                        source_set, ordered_sources, source_root, "sys/stub/stub_gl_win.cpp"
                    )
            else:
                for path in SDL3_WIN32_SOURCES:
                    remove_source(source_set, ordered_sources, path)
        elif args.host_system == "linux":
            if args.target_kind == "dedicated":
                platform_sources = LINUX_DEDICATED_SOURCES
            else:
                platform_sources = (
                    SDL3_LINUX_SOURCES if args.platform_backend == "sdl3" else LINUX_PLATFORM_SOURCES
                )
            for rel_path in platform_sources:
                add_required_source(source_set, ordered_sources, source_root, rel_path)
            if (
                args.target_kind == "client"
                and args.platform_backend == "sdl3"
                and args.linux_x11_helpers == "true"
            ):
                for rel_path in LINUX_X11_HELPER_SOURCES:
                    add_required_source(source_set, ordered_sources, source_root, rel_path)
        elif args.host_system == "android":
            for rel_path in SDL3_ANDROID_SOURCES:
                add_required_source(source_set, ordered_sources, source_root, rel_path)
        elif args.host_system == "darwin":
            platform_sources = (
                SDL3_DARWIN_SOURCES if args.platform_backend == "sdl3" else DARWIN_PLATFORM_SOURCES
            )
            for rel_path in platform_sources:
                add_required_source(source_set, ordered_sources, source_root, rel_path)
        else:
            print(f"Unsupported host system: {args.host_system}", file=sys.stderr)
            return 1
    except SourceListError as exc:
        print(exc, file=sys.stderr)
        return 1

    if args.renderer == "module":
        # Phase B8 F3: the module-only client sheds the renderer sources; the
        # renderer-gl module carries them. Only the engine-side loader TU
        # stays (RendererGLModule.cpp compiles empty without the module
        # define, so dropping it here is equivalent and keeps lists minimal).
        kept_renderer_sources = {"src/renderer/RendererModule.cpp"}
        filtered_sources = []
        for rel in ordered_sources:
            normalized = rel.replace("\\", "/")
            if normalized.startswith("src/renderer/") and normalized not in kept_renderer_sources:
                source_set.discard(rel)
                continue
            filtered_sources.append(rel)
        ordered_sources = filtered_sources
        # the QGL loader only serves the statically linked renderer; its
        # opengl32 imports have no place in a module-only client
        remove_source(source_set, ordered_sources, "src/sys/win32/win_qgl.cpp")

    if not ordered_sources:
        print("No source files discovered.", file=sys.stderr)
        return 1

    print("\n".join(ordered_sources))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
