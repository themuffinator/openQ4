#!/usr/bin/env python3
"""Guards the plumbing behind inline ^i icon escapes in GUI text.

Quake 4 text can embed an icon with a three character escape - "^iw07" in an
obituary line means "the railgun icon here".  The codes are content driven: the
game walks "icon <code>" spawn args while caching an entity def's media and
hands each one to uiManager->RegisterIcon.  def/player.def alone registers the
eleven weapon icons (w00-w10) and the two generic means-of-death icons (dm0 for
a plain frag, dm1 for a telefrag) that idMultiplayerGame::ReceiveDeathMessage
builds its HUD death lines from.

idUserInterfaceManager declares RegisterIcon with an empty inline body rather
than as a pure virtual, so an engine that never overrides it still links, still
runs, and silently drops every registration - idStr::IsEscape consumes "^iw07"
whatever happens, so the obituary keeps its text and just loses its icon.  That
is exactly what happened: the device context's hardcoded builtin list covered
the scoreboard and menu codes, nothing covered the means-of-death icons, and
the escapes rendered as nothing at all.  Pin the override.

Sizing has the same silent-failure shape.  An icon resolves its UVs from the
material's image dimensions, which are zero until the image is resident, and a
zero-height icon is skipped by every draw and measure path.  So the authored
rect has to survive registration, and something has to re-resolve icons that
were registered too early.
"""

from __future__ import annotations

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def body_of(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing {signature!r} in {context}")
    end = source.find("\n}", start)
    if end == -1:
        raise AssertionError(f"Unterminated {signature!r} in {context}")
    return source[start:end]


def validate_manager_forwards() -> None:
    header = read(ROOT / "src" / "ui" / "UserInterfaceLocal.h")
    source = read(ROOT / "src" / "ui" / "UserInterface.cpp")

    if not re.search(r"virtual\s+void\s+RegisterIcon\s*\(", header):
        raise AssertionError(
            "idUserInterfaceManagerLocal does not override RegisterIcon; the base class body is "
            "empty, so every game-side icon registration is discarded and inline ^i escapes - "
            "the obituary means-of-death icons among them - draw nothing"
        )

    forward = body_of(
        source,
        "void idUserInterfaceManagerLocal::RegisterIcon(",
        "UserInterface.cpp",
    )
    require(forward, "dc.RegisterIcon(", "idUserInterfaceManagerLocal::RegisterIcon")


def validate_registration_and_sizing() -> None:
    header = read(ROOT / "src" / "ui" / "DeviceContext.h")
    source = read(ROOT / "src" / "ui" / "DeviceContext.cpp")

    # The authored rect has to be kept, or a re-size reads back its own output.
    for field in ("registeredX", "registeredY", "registeredWidth", "registeredHeight", "sized"):
        require(header, field, "DeviceContext.h embeddedIcon_t")

    register = body_of(source, "void idDeviceContext::RegisterIcon(", "DeviceContext.cpp")
    for field in ("registeredX", "registeredY", "registeredWidth", "registeredHeight"):
        require(register, field, "idDeviceContext::RegisterIcon")

    size_one = body_of(source, "void idDeviceContext::SizeIcon(", "DeviceContext.cpp")
    if re.search(r"static_cast<int>\(\s*icon\.(s1|t1|width|height)\s*\)", size_one):
        raise AssertionError(
            "idDeviceContext::SizeIcon reads its own resolved UVs back as the authored rect; "
            "sizing an icon a second time then walks it away from the sprite it was registered for"
        )
    require(size_one, "icon.sized = false;", "idDeviceContext::SizeIcon")
    require(size_one, "icon.sized = true;", "idDeviceContext::SizeIcon")

    # Something has to retry icons whose image was not resident at registration.
    require(source, "void idDeviceContext::SizeIcons()", "DeviceContext.cpp")
    find = body_of(source, "bool idDeviceContext::FindIcon(", "DeviceContext.cpp")
    require(find, "SizeIcon(", "idDeviceContext::FindIcon")

    end_level_load = body_of(
        read(ROOT / "src" / "ui" / "UserInterface.cpp"),
        "void idUserInterfaceManagerLocal::EndLevelLoad()",
        "UserInterface.cpp",
    )
    require(end_level_load, "dc.SizeIcons();", "idUserInterfaceManagerLocal::EndLevelLoad")


def validate_game_registration() -> None:
    for tree in ("game",):
        local = GAME_LIBS_ROOT / "src" / tree / "Game_local.cpp"
        if not local.is_file():
            continue

        source = read(local)
        # The spawn arg is "icon <code>", with a space - the key is advanced past
        # five characters to reach the three character code.
        require(source, 'MATCH( "icon " )', f"{tree} Game_local.cpp")
        require(source, "uiManager->RegisterIcon", f"{tree} Game_local.cpp")

    death = GAME_LIBS_ROOT / "src" / "game" / "MultiplayerGame.cpp"
    if not death.is_file():
        return

    receive = body_of(
        read(death),
        "void idMultiplayerGame::ReceiveDeathMessage(",
        "game MultiplayerGame.cpp",
    )
    # Weapon deaths take a w%02d icon, everything at or above MAX_WEAPONS takes a
    # dm%d one; both halves are registered from def/player.def.
    require(receive, 'va( "w%02d", methodOfDeath )', "idMultiplayerGame::ReceiveDeathMessage")
    require(receive, 'va( "dm%d", methodOfDeath - MAX_WEAPONS )', "idMultiplayerGame::ReceiveDeathMessage")
    require(receive, "^i", "idMultiplayerGame::ReceiveDeathMessage")


def main() -> int:
    try:
        validate_manager_forwards()
        validate_registration_and_sizing()
        validate_game_registration()
    except AssertionError as error:
        print(f"ui_embedded_icons: FAILED - {error}")
        return 1

    print("ui_embedded_icons: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
