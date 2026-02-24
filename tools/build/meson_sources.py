#!/usr/bin/env python3
"""Emit Meson source paths for OpenQ4 targets."""

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
    "renderer/Color/*.cpp",
    "renderer/DXT/*.cpp",
    "renderer/jpeg-6/*.c",
    "renderer/OpenGL/*.cpp",
    "tools/compilers/dmap/*.cpp",
    "sound/*.cpp",
    "sound/OpenAL/*.cpp",
    "sys/*.cpp",
    "ui/*.cpp",
]

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

LINUX_PLATFORM_SOURCES = (
    "sys/posix/posix_input.cpp",
    "sys/posix/posix_main.cpp",
    "sys/posix/posix_net.cpp",
    "sys/posix/posix_signal.cpp",
    "sys/posix/posix_threads.cpp",
    "sys/linux/glimp.cpp",
    "sys/linux/input.cpp",
    "sys/linux/main.cpp",
    "sys/linux/stack.cpp",
    "sys/linux/libXNVCtrl/NVCtrl.c",
)

DARWIN_PLATFORM_SOURCES = (
    "sys/posix/posix_input.cpp",
    "sys/posix/posix_main.cpp",
    "sys/posix/posix_net.cpp",
    "sys/posix/posix_signal.cpp",
    "sys/posix/posix_threads.cpp",
    "sys/osx/macosx_compat.mm",
    "sys/osx/macosx_event.mm",
    "sys/osx/macosx_glimp.mm",
    "sys/osx/macosx_misc.mm",
    "sys/osx/macosx_sys.mm",
)


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


def add_required_source(
    source_set: set[str],
    ordered_sources: list[str],
    source_root: pathlib.Path,
    rel_path: str,
) -> None:
    full_path = source_root / rel_path
    if not full_path.is_file():
        print(f"Missing expected source file: {full_path}", file=sys.stderr)
        raise FileNotFoundError(rel_path)
    add_source(source_set, ordered_sources, pathlib.Path("src") / rel_path)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--host-system",
        choices=("windows", "linux", "darwin"),
        default="windows",
        help="Meson host system for source selection.",
    )
    parser.add_argument(
        "--platform-backend",
        choices=("sdl3", "legacy_win32", "native"),
        default="sdl3",
        help="Platform backend source selection (Windows: sdl3/legacy_win32, non-Windows: native).",
    )
    parser.add_argument(
        "--include-game",
        choices=("true", "false"),
        default="true",
        help="Include src/game sources in output.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    repo_root = pathlib.Path(__file__).resolve().parents[2]
    source_root = repo_root / "src"

    if not source_root.is_dir():
        print(f"Missing source root: {source_root}", file=sys.stderr)
        return 1

    source_set: set[str] = set()
    ordered_sources: list[str] = []

    include_game = args.include_game == "true"

    if include_game:
        for rel in GAME_SOURCES:
            path = source_root / rel
            if not path.is_file():
                print(f"Missing expected source file: {path}", file=sys.stderr)
                return 1
            add_source(source_set, ordered_sources, pathlib.Path("src") / rel)

    for pattern in ENGINE_SOURCE_GLOBS:
        matches = sorted(source_root.glob(pattern))
        for match in matches:
            if match.is_file():
                add_source(
                    source_set,
                    ordered_sources,
                    pathlib.Path("src") / match.relative_to(source_root),
                )

    if include_game:
        for pattern in GAME_SOURCE_GLOBS:
            matches = sorted(source_root.glob(pattern))
            for match in matches:
                if match.is_file():
                    add_source(
                        source_set,
                        ordered_sources,
                        pathlib.Path("src") / match.relative_to(source_root),
                    )

    try:
        if args.host_system == "windows":
            for match in sorted(source_root.glob("sys/win32/*.cpp")):
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
            else:
                for path in SDL3_WIN32_SOURCES:
                    remove_source(source_set, ordered_sources, path)
        elif args.host_system == "linux":
            for rel_path in LINUX_PLATFORM_SOURCES:
                add_required_source(source_set, ordered_sources, source_root, rel_path)
        elif args.host_system == "darwin":
            for rel_path in DARWIN_PLATFORM_SOURCES:
                add_required_source(source_set, ordered_sources, source_root, rel_path)
        else:
            print(f"Unsupported host system: {args.host_system}", file=sys.stderr)
            return 1
    except FileNotFoundError:
        return 1

    if not ordered_sources:
        print("No source files discovered.", file=sys.stderr)
        return 1

    print("\n".join(ordered_sources))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
