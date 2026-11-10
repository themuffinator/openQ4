#!/usr/bin/env python3
"""Savegame v4 writer, bounded v3 decoder, integrity and transaction contracts."""

from __future__ import annotations

import os
import re
import struct
import subprocess
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()
COMPONENTS = ("txt", "tga", "save")
MAX_SAVEGAME_BYTES = 512 * 1024 * 1024
INTEGRITY_TRAILER_BYTES = 16
MENU_GUI_ASPECT = 640.0 / 480.0
MENU_PREVIEW_BOUNDS = (25.0, 78.0, 183.0, 137.0)


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(source: str, token: str, context: str) -> None:
    if token not in source:
        raise AssertionError(f"Missing {token!r} in {context}")


def constant(source: str, name: str) -> int:
    match = re.search(rf"\b{name}\s*=\s*(\d+)\s*;", source)
    if match is None:
        raise AssertionError(f"Missing integer constant {name}")
    return int(match.group(1))


def snapshot_tuples(source: str, array_name: str) -> list[tuple[int, str, int, str]]:
    match = re.search(rf"\b{array_name}\s*\[\s*\]\s*=\s*\{{(?P<body>.*?)\n\}};", source, re.DOTALL)
    if match is None:
        raise AssertionError(f"Missing snapshot array {array_name}")
    tuples = [
        (int(build), digest, int(count), wire_abi)
        for build, digest, count, wire_abi in re.findall(
            r'\{\s*(\d+)\s*,\s*"([0-9a-f]{64})"\s*,\s*(\d+)\s*,\s*"([a-z0-9-]+)"\s*\}', match.group("body")
        )
    ]
    if not tuples:
        raise AssertionError(f"Snapshot array {array_name} is empty")
    if len(tuples) != len(set(tuples)):
        raise AssertionError(f"Snapshot array {array_name} contains duplicate tuples")
    return tuples


def valid_total_save_length(file_length: int) -> bool:
    return 0 < file_length <= MAX_SAVEGAME_BYTES


def can_append_integrity(protected_length: int) -> bool:
    return 0 < protected_length <= MAX_SAVEGAME_BYTES - INTEGRITY_TRAILER_BYTES


def append_integrity(payload: bytes) -> bytes:
    if not can_append_integrity(len(payload)):
        raise ValueError("payload would exceed total savegame size limit")
    checksum = zlib.crc32(payload) & 0xFFFFFFFF
    return payload + struct.pack("<4I", int.from_bytes(b"OQ4I", "little"), 1, len(payload), checksum)


def valid_integrity(data: bytes) -> bool:
    if not valid_total_save_length(len(data)) or len(data) < INTEGRITY_TRAILER_BYTES:
        return False
    marker, version, protected_length, stored_checksum = struct.unpack_from(
        "<4I", data, len(data) - INTEGRITY_TRAILER_BYTES
    )
    if marker != int.from_bytes(b"OQ4I", "little") or version != 1:
        return False
    if protected_length != len(data) - INTEGRITY_TRAILER_BYTES:
        return False
    return zlib.crc32(data[:protected_length]) & 0xFFFFFFFF == stored_checksum


def validate_size_limit_model() -> None:
    if MAX_SAVEGAME_BYTES >= 2**31:
        raise AssertionError("Savegame cap must fit the engine's signed int file interfaces")
    for invalid_length in (-1, 0, MAX_SAVEGAME_BYTES + 1, 2**31 - 1):
        if valid_total_save_length(invalid_length):
            raise AssertionError(f"Total-size model accepted invalid length {invalid_length}")
    if not valid_total_save_length(MAX_SAVEGAME_BYTES):
        raise AssertionError("Total-size model rejected its inclusive upper bound")

    maximum_protected_length = MAX_SAVEGAME_BYTES - INTEGRITY_TRAILER_BYTES
    if not can_append_integrity(maximum_protected_length):
        raise AssertionError("Writer-size model rejected the largest payload that fits its trailer")
    for invalid_length in (-1, 0, maximum_protected_length + 1, MAX_SAVEGAME_BYTES, 2**31 - 1):
        if can_append_integrity(invalid_length):
            raise AssertionError(f"Writer-size model accepted invalid protected length {invalid_length}")


def validate_integrity_model() -> None:
    payload = bytes((index * 37 + 11) & 0xFF for index in range(131_173))
    protected = append_integrity(payload)
    if not valid_integrity(protected):
        raise AssertionError("A correctly protected v3 payload did not validate")

    mutation_offsets = (0, len(payload) // 2, len(payload) - 1, len(payload), len(payload) + 4, len(payload) + 8, len(payload) + 12)
    for offset in mutation_offsets:
        mutated = bytearray(protected)
        mutated[offset] ^= 0x5A
        if valid_integrity(mutated):
            raise AssertionError(f"Integrity model accepted mutation at byte {offset}")
    for cut in (1, 4, 15, 16, len(protected) - 1):
        if valid_integrity(protected[:cut]):
            raise AssertionError(f"Integrity model accepted truncation to {cut} bytes")
    if valid_integrity(protected + b"trailing"):
        raise AssertionError("Integrity model accepted trailing bytes")


def recover(state: dict[str, dict[str, str | None]]) -> dict[str, dict[str, str | None]]:
    state = {name: values.copy() for name, values in state.items()}
    temp_game_exists = state["save"]["temp"] is not None
    any_backup_exists = any(state[name]["backup"] is not None for name in COMPONENTS)
    any_temp_exists = any(state[name]["temp"] is not None for name in COMPONENTS)

    if not temp_game_exists and not any_backup_exists:
        if any_temp_exists:
            state["txt"]["temp"] = None
            state["tga"]["temp"] = None
        if state["tga"]["final"] == "delete":
            state["tga"]["final"] = None
        return state

    if not temp_game_exists and state["save"]["final"] == "new":
        for name in COMPONENTS:
            state[name]["temp"] = None
            state[name]["backup"] = None
        if state["tga"]["final"] == "delete":
            state["tga"]["final"] = None
        return state

    for name in COMPONENTS:
        values = state[name]
        temp_exists = values["temp"] is not None
        if values["backup"] is not None:
            values["final"] = values["backup"]
            values["backup"] = None
        elif (temp_game_exists and not temp_exists) or (not temp_game_exists and any_backup_exists):
            values["final"] = None
        values["temp"] = None
    return state


def crash_states(overwrite: bool, include_preview: bool) -> list[tuple[str, bool, dict[str, dict[str, str | None]]]]:
    included = COMPONENTS
    state = {
        name: {
            "final": "old" if overwrite else None,
            "temp": "delete" if name == "tga" and not include_preview else "new",
            "backup": None,
        }
        for name in COMPONENTS
    }
    states: list[tuple[str, bool, dict[str, dict[str, str | None]]]] = []

    def record(label: str, committed: bool = False) -> None:
        states.append((label, committed, {name: values.copy() for name, values in state.items()}))

    record("staging complete")
    for name in included:
        if state[name]["final"] is not None:
            state[name]["backup"] = state[name]["final"]
            state[name]["final"] = None
        record(f"backed up {name}")
    for name in included:
        state[name]["final"] = state[name]["temp"]
        state[name]["temp"] = None
        record(f"committed {name}", committed=name == "save")
    for name in included:
        state[name]["backup"] = None
        record(f"cleaned {name}", committed=True)
    return states


def validate_transaction_model() -> None:
    for overwrite in (False, True):
        for include_preview in (False, True):
            payload_committed = False
            for label, commits_payload, state in crash_states(overwrite, include_preview):
                payload_committed = payload_committed or commits_payload
                recovered = recover(state)
                expected = "new" if payload_committed else ("old" if overwrite else None)
                for name in COMPONENTS:
                    expected_component = expected
                    if name == "tga" and payload_committed and not include_preview:
                        expected_component = None
                    if recovered[name]["final"] != expected_component:
                        raise AssertionError(
                            f"Recovery mixed snapshots after {label}: {name}={recovered[name]['final']!r}, "
                            f"expected {expected_component!r}"
                        )
                    if recovered[name]["temp"] is not None or recovered[name]["backup"] is not None:
                        raise AssertionError(f"Recovery left staging files after {label}: {name}={recovered[name]}")


def fit_menu_preview(
    image_width: int,
    image_height: int,
    *,
    viewport_aspect: float,
    aspect_correction: bool,
) -> tuple[float, float, float, float]:
    image_aspect = (
        image_width / image_height
        if image_width > 0 and image_height > 0
        else MENU_GUI_ASPECT
    )
    logical_aspect = image_aspect
    if not aspect_correction and viewport_aspect > 0.0:
        logical_aspect /= viewport_aspect / MENU_GUI_ASPECT

    bounds_x, bounds_y, max_width, max_height = MENU_PREVIEW_BOUNDS
    width = max_width
    height = max_height
    if logical_aspect > max_width / max_height:
        height = width / logical_aspect
    else:
        width = height * logical_aspect
    return (
        bounds_x + (max_width - width) * 0.5,
        bounds_y + (max_height - height) * 0.5,
        width,
        height,
    )


def validate_menu_preview_aspect_model() -> None:
    for image_width, image_height in ((320, 240), (640, 360), (3440, 1440), (1080, 1920), (0, 0)):
        expected_aspect = (
            image_width / image_height
            if image_width > 0 and image_height > 0
            else MENU_GUI_ASPECT
        )
        for aspect_correction in (False, True):
            for viewport_aspect in (4.0 / 3.0, 16.0 / 9.0, 21.0 / 9.0):
                x, y, width, height = fit_menu_preview(
                    image_width,
                    image_height,
                    viewport_aspect=viewport_aspect,
                    aspect_correction=aspect_correction,
                )
                bounds_x, bounds_y, max_width, max_height = MENU_PREVIEW_BOUNDS
                if width <= 0.0 or height <= 0.0:
                    raise AssertionError("Preview fit produced an empty rectangle")
                if x < bounds_x - 1e-5 or y < bounds_y - 1e-5:
                    raise AssertionError("Preview fit escaped the top/left of its menu bounds")
                if x + width > bounds_x + max_width + 1e-5 or y + height > bounds_y + max_height + 1e-5:
                    raise AssertionError("Preview fit escaped the bottom/right of its menu bounds")

                physical_stretch = 1.0 if aspect_correction else viewport_aspect / MENU_GUI_ASPECT
                physical_aspect = width * physical_stretch / height
                if abs(physical_aspect - expected_aspect) > 1e-5:
                    raise AssertionError(
                        f"Preview aspect mismatch: got {physical_aspect}, expected {expected_aspect}"
                    )


def git_tag_commit_count(tag: str) -> int | None:
    """The commit count of a release tag, or None where the checkout lacks it."""
    try:
        result = subprocess.run(["git", "-C", str(ROOT), "rev-list", "--count", tag],
                                capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None
    try:
        return int(result.stdout.strip())
    except ValueError:
        return None


def validate_release_layout_contract(session: str, sp: str, mp: str, minimum_build: int) -> None:
    """Shallow release checkouts stamped build 1; their snapshot selects their tag's build."""
    releases_h = read(ROOT / "src/framework/SaveGameReleases.h")
    rows = re.findall(r'\{\s*"([0-9.]+)",\s*"([0-9a-f]{64})",\s*(\d+),\s*(\d+)\s*\}', releases_h)
    if len(rows) < 4:
        raise AssertionError("SaveGameReleases.h must list the build-1 v3 releases 0.10.0 through 0.13.0")
    builds = [int(row[3]) for row in rows]
    if builds != sorted(builds) or len(set(row[1] for row in rows)) != len(rows):
        raise AssertionError("Release snapshots must be unique and ordered by build")
    if min(builds) < minimum_build:
        raise AssertionError("A listed release falls below the verified build floor it is read against")
    for version, _source_hash, _files, build in rows:
        # A full checkout stamps the commit count, as 0.13.1 (858) and later do.
        tag_build = git_tag_commit_count(f"v{version}")
        if tag_build is not None and tag_build != int(build):
            raise AssertionError(f"Release {version} is listed as build {build}, but its tag carries {tag_build}")
    require(releases_h, "OPENQ4_SAVEGAME_SHALLOW_RELEASE_BUILD = 1", "shallow release build stamp")
    require(session, '#include "SaveGameReleases.h"', "engine release snapshot table")
    require(session, "layoutBuild < minimumBuild", "engine layout build floor")
    require(session, "OPENQ4_SAVEGAME_V2_FIRST_RELEASE_BUILD : SESSION_OPENQ4_SAVEGAME_MINIMUM_SUPPORTED_BUILD;", "engine per-version floor")
    require(session, "saveGameRestoreScope( savegameFile, payload )", "engine restore layout scope")
    require(session, "openQ4_saveGameRestore.version = payload.version;", "engine restore format scope")
    for source, context in ((sp, "SP GameLib"), (mp, "MP GameLib")):
        for token in (
            '#include "framework/SaveGameReleases.h"',
            "openQ4SaveGameLayoutBuild = OpenQ4_SaveGameLayoutBuild( openQ4SaveGameCompatibilityVersion, buildNumber,",
            "openQ4SaveGameCompatibilityVersion < OPENQ4_SAVEGAME_V2_VERSION",
            "idStr savedWireABI = OPENQ4_SAVEGAME_V2_WIRE_ABI;",
            "return openQ4SaveGameLayoutBuild >= firstBuildWithField;",
            "return OpenQ4_SaveGameHasInterpolatorCacheFlag( openQ4SaveGameLayoutBuild );",
            "OpenQ4_FindSaveGameGuiRevision( name, openQ4SaveGameLayoutBuild )",
            "!GuiRecordNamesWindow( revision->currentWindow )",
        ):
            require(source, token, f"{context} release layout decoding")

    # Retail 1.0 (build 1262) omitted idShaking's target history; an openQ4 dev
    # build can carry the same number, so only an unstamped payload is retail.
    retail_check = "savefile->GetBuildNumber() == INITIAL_RELEASE_BUILD_NUMBER"
    guarded_check = "!savefile->HasOpenQ4SaveGameCompatibilityStamp() && " + retail_check
    for tree in ("game", "mpgame"):
        misc = read(GAME_LIBS_ROOT / f"src/{tree}/Misc.cpp")
        if misc.count(retail_check) != misc.count(guarded_check):
            raise AssertionError(f"src/{tree} reads a stamped build-1262 payload as retail 1.0")

    # Version 2 (0.6.95-0.9.0) is read from build 546, where its sound channels
    # gained four fields; every v2 payload stamped build 1 is a release from 0.6.95
    # on, and it carries no ABI stamp, so it is read only as the Windows x64 layout.
    if constant(releases_h, "OPENQ4_SAVEGAME_V2_VERSION") != 2 or constant(releases_h, "OPENQ4_SAVEGAME_V2_FIRST_RELEASE_BUILD") != 546:
        raise AssertionError("Version 2 is read from build 546, the 0.6.95 tag")
    require(releases_h, 'OPENQ4_SAVEGAME_V2_WIRE_ABI = "windows-msvcabi-x64-le-raw1"', "v2 implied wire ABI")
    # Liquid support arrived with v3 itself, so a v2 payload has none of its fields.
    liquid_gate = "if ( savefile->GetOpenQ4SaveGameCompatibilityVersion() >= OPENQ4_SAVEGAME_TYPED_COMPATIBILITY_VERSION ) {"
    for tree in ("game", "mpgame"):
        for relative, first_read in (("ai/AI.cpp", "savefile->ReadInt( liquidLevel );"),
                                     ("Player.cpp", "savefile->ReadInt( (int &)previousWaterLevel );")):
            source = read(GAME_LIBS_ROOT / f"src/{tree}/{relative}")
            read_at = source.find(first_read)
            gate = source.rfind(liquid_gate, 0, read_at) if read_at >= 0 else -1
            if gate < 0 or not 0 < read_at - gate < 200:
                raise AssertionError(f"src/{tree}/{relative} reads v3 liquid state from a v2 payload")

    # Raw interpolator records grew a word with idExtrapolate's cache flag (build 1582).
    if constant(releases_h, "OPENQ4_SAVEGAME_BUILD_WITH_INTERPOLATOR_CACHE_FLAG") != 1582:
        raise AssertionError("The interpolator cache flag entered the save at build 1582 (f15a41f03)")
    extrapolate_h = read(ROOT / "src/idlib/math/Extrapolate.h")
    if re.search(r"mutable\s+bool\s+cacheValid\s*;", extrapolate_h) is None:
        raise AssertionError("idExtrapolate lost the cache flag the legacy interpolator layout omits")
    for anim, context in ((GAME_LIBS_ROOT / "src/game/anim/Anim_Blend.cpp", "SP"), (GAME_LIBS_ROOT / "src/mpgame/anim/Anim_Blend.cpp", "MP")):
        source = read(anim)
        require(source, "openQ4SaveGameLegacyInterpolate_t<idAngles> angularVelocity;", f"{context} legacy joint modifier record")
        require(source, "Anim_ReadSavedJointMod( savefile, *jointMods[ i ] );", f"{context} joint modifier decoder")
    require(read(ROOT / "src/ui/Window.cpp"), "openQ4SaveGameLegacyInterpolate_t<idVec4> saved;", "GUI transition legacy layout")

    # Each GUI whose edit changed its saved structure keeps the revision older payloads saved.
    revisions = re.findall(r'\{\s*"(guis/[^"]+)",\s*(\d+),\s*"(guis/savecompat/[^"]+)",\s*"([A-Za-z0-9_]+)"\s*\}', releases_h)
    if not revisions:
        raise AssertionError("SaveGameReleases.h lists no GUI content revisions")
    pak0 = ROOT / "content/baseoq4/pak0"
    # A GUI another game ships is read from openQ4's definition of it (idLegacyHudLayout::DefinitionFor).
    definitions = dict(re.findall(r'\{"(guis/[^"]+\.gui)","(guis/openq4/[^"]+\.gui)"\}', read(ROOT / "src/ui/RetainedHud.cpp")))
    for name, _build, saved_definition, current_window in revisions:
        current = read(pak0 / definitions.get(name, name))
        saved = read(pak0 / saved_definition)
        if f"windowDef {current_window}" not in current:
            raise AssertionError(f"{name} no longer defines {current_window}, its current-revision signature")
        if f"windowDef {current_window}" in saved:
            raise AssertionError(f"{saved_definition} defines {current_window}, so it cannot tell the revisions apart")


def validate_source_contracts() -> None:
    session = read(ROOT / "src/framework/Session.cpp")
    session_menu = read(ROOT / "src/framework/Session_menu.cpp")
    main_menu_gui = read(ROOT / "content/baseoq4/pak0/guis/mainmenu.gui")
    engine_file_h = read(ROOT / "src/framework/File.h")
    engine_file_cpp = read(ROOT / "src/framework/File.cpp")
    sp = read(GAME_LIBS_ROOT / "src/game/gamesys/SaveGame.cpp")
    sp_h = read(GAME_LIBS_ROOT / "src/game/gamesys/SaveGame.h")
    sp_class_h = read(GAME_LIBS_ROOT / "src/game/gamesys/Class.h")
    sp_player = read(GAME_LIBS_ROOT / "src/game/Player.cpp")
    sp_physics_player = read(GAME_LIBS_ROOT / "src/game/physics/Physics_Player.cpp")
    mp = read(GAME_LIBS_ROOT / "src/mpgame/gamesys/SaveGame.cpp")
    mp_h = read(GAME_LIBS_ROOT / "src/mpgame/gamesys/SaveGame.h")
    mp_class_h = read(GAME_LIBS_ROOT / "src/mpgame/gamesys/Class.h")
    mp_player = read(GAME_LIBS_ROOT / "src/mpgame/Player.cpp")
    mp_physics_player = read(GAME_LIBS_ROOT / "src/mpgame/physics/Physics_Player.cpp")
    game_file_h = read(GAME_LIBS_ROOT / "src/framework/File.h")
    releases_h = read(ROOT / "src/framework/SaveGameReleases.h")

    # v5 (portable encodings, roadmap Q4-06) is written; v2 to v4 stay readable
    # on the wire ABI that wrote them, and v4 still carries Awakening status.
    if constant(session, "SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_VERSION") != 5:
        raise AssertionError("Engine writer must use the v5 portable payload")
    if constant(session, "SESSION_OPENQ4_SAVEGAME_TYPED_COMPATIBILITY_VERSION") != 3:
        raise AssertionError("Engine must retain its bounded v3 reader")
    if constant(releases_h, "OPENQ4_SAVEGAME_PORTABLE_VERSION") != 5:
        raise AssertionError("The portable payload must be version 5")
    require(releases_h, 'OPENQ4_SAVEGAME_PORTABLE_WIRE_ABI = "le64-portable1"', "portable wire ABI stamp")
    for source, context in ((sp_h, "SP GameLib"), (mp_h, "MP GameLib")):
        if constant(source, "OPENQ4_SAVEGAME_COMPATIBILITY_VERSION") != 5:
            raise AssertionError(f"{context} current save format differs from engine v5")
        if constant(source, "OPENQ4_SAVEGAME_PORTABLE_COMPATIBILITY_VERSION") != 5:
            raise AssertionError(f"{context} portable save format differs from engine v5")
        if constant(source, "OPENQ4_SAVEGAME_TYPED_COMPATIBILITY_VERSION") != 3:
            raise AssertionError(f"{context} typed save reader differs from engine v3")

    # A save is only claimed when a verified decoder exists for its layout. The
    # per-snapshot allowlists made that claim for tuples that were never
    # exercised against a real save, and every one that was later tested
    # desynced part way through the restore - after the running map had already
    # been torn down. They are replaced by a build floor plus per-field build
    # thresholds, so nothing may reintroduce a tuple allowlist.
    for source, context in (
        (session, "engine"),
        (sp, "SP GameLib"),
        (mp, "MP GameLib"),
    ):
        for banned in (
            "SAVEGAME_V2_SNAPSHOTS",
            "V3_PRE_PLAYER_LIQUID_FIELDS_SNAPSHOTS",
            "IsSupportedV2Snapshot",
            "IsV3PrePlayerLiquidFieldsSnapshot",
        ):
            if banned in source:
                raise AssertionError(
                    f"{context} still carries the {banned} compatibility allowlist; "
                    "save support must come from a verified decoder, not a tuple list"
                )

    minimum_build = constant(session, "SESSION_OPENQ4_SAVEGAME_MINIMUM_SUPPORTED_BUILD")
    if minimum_build <= 0:
        raise AssertionError("Engine must declare the oldest v3 build it can restore")

    # The two player liquid fields landed in different builds, so they need
    # independent thresholds; one boolean for both is what made saves written
    # between them read a field their file does not contain.
    sp_swim = constant(sp, "OPENQ4_SAVEGAME_BUILD_WITH_PLAYER_SWIM_SPEED")
    mp_swim = constant(mp, "OPENQ4_SAVEGAME_BUILD_WITH_PLAYER_SWIM_SPEED")
    sp_sound = constant(sp, "OPENQ4_SAVEGAME_BUILD_WITH_PLAYER_LIQUID_SOUND")
    mp_sound = constant(mp, "OPENQ4_SAVEGAME_BUILD_WITH_PLAYER_LIQUID_SOUND")
    if sp_swim != mp_swim or sp_sound != mp_sound:
        raise AssertionError("SP/MP player liquid field build thresholds differ")
    if sp_swim >= sp_sound:
        raise AssertionError("swimSpeed was added before nextLiquidSurfaceSoundTime; thresholds must reflect that")
    if minimum_build > sp_swim:
        raise AssertionError(
            "The verified build floor must not exclude saves the liquid thresholds still describe"
        )

    validate_release_layout_contract(session, sp, mp, minimum_build)

    for source, header, class_header, player, physics_player, context in (
        (sp, sp_h, sp_class_h, sp_player, sp_physics_player, "SP GameLib"),
        (mp, mp_h, mp_class_h, mp_player, mp_physics_player, "MP GameLib"),
    ):
        for token in (
            "bool idRestoreGame::HasOpenQ4PlayerSwimSpeedSaveField",
            "bool idRestoreGame::HasOpenQ4PlayerLiquidSoundSaveField",
            "bool idRestoreGame::HasOpenQ4PlayerLiquidSaveFieldForBuild",
            "HasNextSerializedEmptyClassFrame",
            'idStr::Icmp( cls->classname, "idPhysics" ) == 0',
            "!cls->saveDeclaredHere",
            "!cls->restoreDeclaredHere",
        ):
            require(source, token, f"{context} v0.10 compatibility decoder")
        for accessor in (
            "HasOpenQ4PlayerSwimSpeedSaveField( void ) const",
            "HasOpenQ4PlayerLiquidSoundSaveField( void ) const",
        ):
            require(header, accessor, f"{context} compatibility accessor")
        for token in (
            "struct idMemberFunctionOwner",
            "struct idMemberFunctionDeclaredHere",
            "decltype( &nameofclass::Save )",
            "decltype( &nameofclass::Restore )",
            "saveDeclaredHere",
            "restoreDeclaredHere",
        ):
            require(class_header, token, f"{context} source-declared class-frame ownership")
        for obsolete_comparison in (
            "cls->super->Save == cls->Save",
            "cls->super->Restore == cls->Restore",
        ):
            if obsolete_comparison in source:
                raise AssertionError(f"{context} still derives save frames from linker-foldable function addresses")
        if re.search(
            r"ReadInt\s*\(\s*previousWaterType\s*\)\s*;\s*"
            r"nextLiquidSurfaceSoundTime\s*=\s*0\s*;\s*"
            r"if\s*\(\s*savefile->HasOpenQ4PlayerLiquidSoundSaveField\s*\(\s*\)\s*\)\s*\{\s*"
            r"savefile->ReadInt\s*\(\s*nextLiquidSurfaceSoundTime\s*\)\s*;\s*\}\s*"
            r"savefile->ReadInt\s*\(\s*nextLiquidDamageTime\s*\)",
            player,
            re.DOTALL,
        ) is None:
            raise AssertionError(f"{context} does not gate the liquid-sound timer on the build that added it")
        if re.search(
            r"ReadFloat\s*\(\s*playerSpeed\s*\)\s*;\s*"
            r"swimSpeed\s*=\s*0\.0f\s*;\s*"
            r"if\s*\(\s*savefile->HasOpenQ4PlayerSwimSpeedSaveField\s*\(\s*\)\s*\)\s*\{\s*"
            r"savefile->ReadFloat\s*\(\s*swimSpeed\s*\)\s*;\s*\}\s*"
            r"savefile->ReadVec3\s*\(\s*viewForward\s*\)",
            physics_player,
            re.DOTALL,
        ) is None:
            raise AssertionError(f"{context} does not gate swim speed on the build that added it")

    require(session, 'SESSION_LEGACY_SAVEGAME_WIRE_ABI = "windows-msvcabi-x64-le-raw1"',
            "engine unstamped legacy ABI restriction")
    require(session, "Session_GetSaveGameWireABI(), SESSION_LEGACY_SAVEGAME_WIRE_ABI",
            "engine unstamped legacy ABI comparison")
    for source, context in ((sp, "SP GameLib"), (mp, "MP GameLib")):
        require(source, 'OpenQ4SaveGameWireABI(), "windows-msvcabi-x64-le-raw1"',
                f"{context} unstamped legacy ABI restriction")

    for source, context in ((session, "engine"), (sp, "SP GameLib"), (mp, "MP GameLib")):
        for token in (
            '"windows"',
            '"linux"',
            '"macos"',
            '"msvcabi"',
            '"itaniumabi"',
            '"x64"',
            '"arm64"',
            '"le"',
            '"be"',
            '"-raw1"',
        ):
            require(source, token, f"{context} wire ABI stamp")

    for source, context in ((engine_file_h, "engine idFile"), (game_file_h, "GameLib idFile")):
        require(source, "virtual bool", context)
        require(source, "Sync( void )", context)
    for token in ("bool idFile_Permanent::Sync( void )", "_commit( _fileno( o ) )", "fsync( fileno( o ) )"):
        require(engine_file_cpp, token, "cross-platform durable file sync")

    for token in (
        "SESSION_MAX_SAVEGAME_BYTES = 512 * 1024 * 1024",
        "Session_GetBoundedSaveGameLength",
        "CRC32_InitChecksum",
        "CRC32_UpdateChecksum",
        "CRC32_FinishChecksum",
        "Session_AppendSaveGameIntegrityTrailer",
        "calculatedChecksum != savedChecksum",
        "Session_RecoverInterruptedSaveFiles",
        "Session_CreateSavePreviewDeletionMarker",
        "Session_IsSavePreviewDeletionMarker",
        "The payload is committed last.",
        "!tempGameExists && Session_RelativeSaveFileExists( gameFile )",
    ):
        require(session, token, "engine v3 integrity/transaction contract")
    if re.search(
        r"Session_AppendSaveGameIntegrityTrailer.*?"
        r"protectedLength\s*>\s*SESSION_MAX_SAVEGAME_BYTES\s*-\s*SESSION_OPENQ4_SAVEGAME_INTEGRITY_BYTES.*?"
        r"Session_CalculateSaveGameChecksum",
        session,
        re.DOTALL,
    ) is None:
        raise AssertionError("Staged save size is not capped before its CRC scan and integrity append")
    if re.search(
        r"game->SaveGame\s*\(\s*fileOut\s*,\s*saveType\s*\).*?"
        r"stagedProtectedLength\s*<=\s*SESSION_MAX_SAVEGAME_BYTES\s*-\s*SESSION_OPENQ4_SAVEGAME_INTEGRITY_BYTES.*?"
        r"payloadFile->Write\s*\(.*?stagedProtectedLength.*?"
        r"payloadFile->Sync\s*\(\s*\).*?"
        r"Session_AppendSaveGameIntegrityTrailer",
        session,
        re.DOTALL,
    ) is None:
        raise AssertionError("Writer does not reject oversized output before syncing or checksumming it")
    if re.search(
        r"OpenFileRead\s*\(\s*in.*?"
        r"Session_GetBoundedSaveGameLength\s*\(\s*loadGameFile\s*,\s*in\s*,\s*\"load preflight\".*?"
        r"Session_ReadSaveGameString\s*\(\s*loadGameFile\s*,\s*gamename",
        session,
        re.DOTALL,
    ) is None:
        raise AssertionError("Load path does not enforce the total save size before header/payload preflight")
    if re.search(
        r"Session_ValidateSaveGamePayload.*?"
        r"Session_GetBoundedSaveGameLength\s*\(\s*file\s*,\s*savePath\s*,\s*\"payload preflight\".*?"
        r"Session_CalculateSaveGameChecksum",
        session,
        re.DOTALL,
    ) is None:
        raise AssertionError("Payload validation does not enforce the total save size before its CRC scan")
    if re.search(
        r"Session_InitStagedSaveFile\s*\([^;]+tempDescriptionFile.*?"
        r"Session_InitStagedSaveFile\s*\([^;]+tempGameFile.*?Session_CommitStagedSaveFiles",
        session,
        re.DOTALL,
    ) is None:
        raise AssertionError("The payload is not demonstrably the final save commit point")

    for token in (
        "Session_MenuFitSaveGamePreviewRect",
        'cvarSystem->GetCVarBool( "ui_aspectCorrection" )',
        "viewportStretch = ( viewportWidth / viewportHeight ) / SESSION_MENU_GUI_ASPECT",
        'SetStateFloat( "loadgame_preview_x"',
        'SetStateFloat( "loadgame_preview_y"',
        'SetStateFloat( "loadgame_preview_w"',
        'SetStateFloat( "loadgame_preview_h"',
        "Session_MenuSetSaveGamePreviewRect( guiActive, defaultPreview )",
        "Session_MenuSetSaveGamePreviewRect( guiActive, material )",
    ):
        require(session_menu, token, "load-menu save preview aspect contract")
    require(
        main_menu_gui,
        'rect\t"gui::loadgame_preview_x","gui::loadgame_preview_y","gui::loadgame_preview_w","gui::loadgame_preview_h"',
        "load-menu dynamic preview rectangle",
    )


def main() -> None:
    validate_size_limit_model()
    validate_integrity_model()
    validate_transaction_model()
    validate_menu_preview_aspect_model()
    validate_source_contracts()
    print("savegame_v3_contract: ok")


if __name__ == "__main__":
    main()
