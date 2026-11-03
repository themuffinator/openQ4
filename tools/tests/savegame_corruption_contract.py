#!/usr/bin/env python3
"""Savegame corruption and fuzz guardrails for engine-side load code."""

from __future__ import annotations

import importlib.util
import os
import re
import struct
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def read_game_libs(relative_path: str) -> str:
    path = GAME_LIBS_ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required openQ4-game source file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_regex(haystack: str, pattern: str, context: str) -> None:
    if re.search(pattern, haystack, re.MULTILINE | re.DOTALL) is None:
        raise AssertionError(f"Missing pattern {pattern!r} in {context}")


def parse_int_constant(relative_path: str, pattern: str, context: str) -> int:
    source = read(relative_path)
    match = re.search(pattern, source, re.MULTILINE)
    if match is None:
        raise AssertionError(f"Missing integer constant for {context}")
    return int(match.group(1))


def parse_ssd_entity_types() -> dict[str, int]:
    source = read("src/ui/GameSSDWindow.h")
    for match in re.finditer(r"enum\s*\{(?P<body>.*?)\};", source, re.MULTILINE | re.DOTALL):
        body = match.group("body")
        if "SSD_ENTITY_BASE" not in body:
            continue

        values: dict[str, int] = {}
        current = -1
        for raw_entry in body.split(","):
            entry = raw_entry.strip()
            if not entry:
                continue
            if "=" in entry:
                name, value = entry.split("=", 1)
                current = int(value.strip())
            else:
                name = entry
                current += 1
            values[name.strip()] = current
        return values

    raise AssertionError("Missing SSD entity type enum")


MAX_STRING_CHARS = parse_int_constant(
    "src/idlib/Lib.h",
    r"#define\s+MAX_STRING_CHARS\s+(\d+)",
    "MAX_STRING_CHARS",
)
MAX_ASYNC_CLIENTS = parse_int_constant(
    "src/framework/async/AsyncNetwork.h",
    r"MAX_ASYNC_CLIENTS\s*=\s*(\d+)",
    "MAX_ASYNC_CLIENTS",
)
SESSION_MAX_SAVEGAME_DICT_KV = 16384
MAX_EXPRESSION_REGISTERS = parse_int_constant(
    "src/idlib/precompiled.h",
    r"MAX_EXPRESSION_REGISTERS\s*=\s*(\d+)",
    "MAX_EXPRESSION_REGISTERS",
)
REGISTER_REGCOUNTS = (4, 1, 1, 1, 0, 2, 3, 4)
SSD_MAX_ASTEROIDS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_ASTEROIDS\s+(\d+)", "MAX_ASTEROIDS")
SSD_MAX_ASTRONAUT = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_ASTRONAUT\s+(\d+)", "MAX_ASTRONAUT")
SSD_MAX_EXPLOSIONS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_EXPLOSIONS\s+(\d+)", "MAX_EXPLOSIONS")
SSD_MAX_POINTS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_POINTS\s+(\d+)", "MAX_POINTS")
SSD_MAX_PROJECTILES = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_PROJECTILES\s+(\d+)", "MAX_PROJECTILES")
SSD_MAX_POWERUPS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_POWERUPS\s+(\d+)", "MAX_POWERUPS")
SSD_MAX_SAVE_LEVELS = parse_int_constant(
    "src/ui/GameSSDWindow.cpp",
    r"GAME_SSD_MAX_SAVE_LEVELS\s*=\s*(\d+)",
    "GAME_SSD_MAX_SAVE_LEVELS",
)
SSD_MAX_SAVE_WEAPONS = parse_int_constant(
    "src/ui/GameSSDWindow.cpp",
    r"GAME_SSD_MAX_SAVE_WEAPONS\s*=\s*(\d+)",
    "GAME_SSD_MAX_SAVE_WEAPONS",
)
SSD_ENTITY_TYPES = parse_ssd_entity_types()
SSD_POOL_ORDER = (
    ("asteroid", SSD_ENTITY_TYPES["SSD_ENTITY_ASTEROID"], SSD_MAX_ASTEROIDS),
    ("astronaut", SSD_ENTITY_TYPES["SSD_ENTITY_ASTRONAUT"], SSD_MAX_ASTRONAUT),
    ("explosion", SSD_ENTITY_TYPES["SSD_ENTITY_EXPLOSION"], SSD_MAX_EXPLOSIONS),
    ("points", SSD_ENTITY_TYPES["SSD_ENTITY_POINTS"], SSD_MAX_POINTS),
    ("projectile", SSD_ENTITY_TYPES["SSD_ENTITY_PROJECTILE"], SSD_MAX_PROJECTILES),
    ("powerup", SSD_ENTITY_TYPES["SSD_ENTITY_POWERUP"], SSD_MAX_POWERUPS),
)
SSD_ENTITY_POOL_LIMITS = {entity_type: max_count for _, entity_type, max_count in SSD_POOL_ORDER}
SSD_MAX_SAVE_ENTITY_REFS = sum(SSD_ENTITY_POOL_LIMITS.values())


class CorruptSave(ValueError):
    pass


def validate_register_schema_model(
    saved_enabled: int,
    saved_type: int,
    saved_count: int,
    saved_name: str,
    saved_regs: tuple[int, int, int, int],
    live_type: int,
    live_count: int,
    live_name: str,
    live_regs: tuple[int, int, int, int],
) -> None:
    if saved_enabled not in (0, 1):
        raise CorruptSave("invalid register enabled flag")
    if not 0 <= live_type < len(REGISTER_REGCOUNTS):
        raise CorruptSave("invalid parsed register type")
    if not 0 <= live_count <= 4 or live_count != REGISTER_REGCOUNTS[live_type]:
        raise CorruptSave("invalid parsed register count")
    if not 0 <= saved_type < len(REGISTER_REGCOUNTS):
        raise CorruptSave("invalid saved register type")
    if not 0 <= saved_count <= 4 or saved_count != REGISTER_REGCOUNTS[saved_type]:
        raise CorruptSave("invalid saved register count")
    if (saved_type, saved_count, saved_name.casefold()) != (live_type, live_count, live_name.casefold()):
        raise CorruptSave("register schema mismatch")
    if saved_type == 4 and saved_enabled != 0:
        raise CorruptSave("string register cannot be enabled")
    for i in range(saved_count):
        if saved_regs[i] >= MAX_EXPRESSION_REGISTERS or live_regs[i] >= MAX_EXPRESSION_REGISTERS:
            raise CorruptSave("register expression index out of range")
        if saved_regs[i] != live_regs[i]:
            raise CorruptSave("register expression index mismatch")


class SaveHeaderReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def read_int(self) -> int:
        if self.offset + 4 > len(self.data):
            raise CorruptSave("truncated int")
        value = struct.unpack_from("<i", self.data, self.offset)[0]
        self.offset += 4
        return value

    def read_save_string(self, max_length: int) -> bytes:
        length = self.read_int()
        remaining = len(self.data) - self.offset
        if length < 0 or length > max_length or length > remaining:
            raise CorruptSave(f"invalid save string length {length}")
        value = self.data[self.offset : self.offset + length]
        self.offset += length
        return value

    def read_cstring(self, max_length: int) -> bytes:
        start = self.offset
        for _ in range(max_length):
            if self.offset >= len(self.data):
                raise CorruptSave("truncated cstring")
            value = self.data[self.offset]
            self.offset += 1
            if value == 0:
                return self.data[start : self.offset - 1]
        raise CorruptSave("unterminated cstring")

    def read_dict(self) -> dict[bytes, bytes]:
        count = self.read_int()
        if count < 0 or count > SESSION_MAX_SAVEGAME_DICT_KV:
            raise CorruptSave(f"invalid dict count {count}")
        values: dict[bytes, bytes] = {}
        for _ in range(count):
            key = self.read_cstring(MAX_STRING_CHARS)
            value = self.read_cstring(MAX_STRING_CHARS)
            values[key] = value
        return values


class SSDRestoreGuardReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0
        self.restored_ids: dict[int, set[int]] = {entity_type: set() for entity_type in SSD_ENTITY_POOL_LIMITS}

    def read_int(self) -> int:
        if self.offset + 4 > len(self.data):
            raise CorruptSave("truncated SSD int")
        value = struct.unpack_from("<i", self.data, self.offset)[0]
        self.offset += 4
        return value

    def read_count(self, label: str, max_count: int) -> int:
        count = self.read_int()
        if count < 0 or count > max_count:
            raise CorruptSave(f"invalid SSD {label} {count}")
        return count

    @staticmethod
    def validate_index(label: str, value: int, count: int) -> None:
        if value < 0 or value >= count:
            raise CorruptSave(f"invalid SSD {label} {value} for count {count}")

    @staticmethod
    def validate_index_or_end(label: str, value: int, count: int) -> None:
        if value < 0 or value > count:
            raise CorruptSave(f"invalid SSD {label} {value} for count {count}")

    @staticmethod
    def validate_entity_reference(label: str, entity_type: int, entity_id: int) -> None:
        max_count = SSD_ENTITY_POOL_LIMITS.get(entity_type)
        if max_count is None:
            raise CorruptSave(f"invalid SSD {label} type {entity_type}")
        if entity_id < 0 or entity_id >= max_count:
            raise CorruptSave(f"invalid SSD {label} id {entity_id}")

    def parse(self) -> None:
        level_count = self.read_count("level count", SSD_MAX_SAVE_LEVELS)
        weapon_count = self.read_count("weapon count", SSD_MAX_SAVE_WEAPONS)

        current_weapon = self.read_int()
        current_level = self.read_int()
        next_level = self.read_int()
        self.validate_index("current weapon", current_weapon, weapon_count)
        self.validate_index("current level", current_level, level_count)
        self.validate_index_or_end("next level", next_level, level_count)

        for label, entity_type, max_count in SSD_POOL_ORDER:
            count = self.read_count(f"{label} count", max_count)
            for _ in range(count):
                entity_id = self.read_int()
                self.validate_entity_reference(f"{label} id", entity_type, entity_id)
                self.restored_ids[entity_type].add(entity_id)

        entity_ref_count = self.read_count("entity reference count", SSD_MAX_SAVE_ENTITY_REFS)
        for _ in range(entity_ref_count):
            entity_type = self.read_int()
            entity_id = self.read_int()
            self.validate_entity_reference("entity reference", entity_type, entity_id)
            if entity_id not in self.restored_ids[entity_type]:
                raise CorruptSave(f"SSD entity reference points at inactive pool id {entity_id}")

        if self.offset != len(self.data):
            raise CorruptSave("SSD guard payload has trailing bytes")


def int32(value: int) -> bytes:
    return struct.pack("<i", value)


def save_string(value: bytes) -> bytes:
    return int32(len(value)) + value


def save_dict(pairs: tuple[tuple[bytes, bytes], ...] = ()) -> bytes:
    data = int32(len(pairs))
    for key, value in pairs:
        data += key + b"\0" + value + b"\0"
    return data


def valid_retail_header() -> bytes:
    return (
        save_string(b"Quake4")
        + int32(12)
        + save_string(b"maps/game/airdefense1.map")
        + save_string(b"")
        + (save_dict() * MAX_ASYNC_CLIENTS)
    )


def parse_retail_header(data: bytes) -> int:
    reader = SaveHeaderReader(data)
    reader.read_save_string(MAX_STRING_CHARS)
    reader.read_int()
    reader.read_save_string(MAX_STRING_CHARS)
    reader.read_save_string(MAX_STRING_CHARS)
    for _ in range(MAX_ASYNC_CLIENTS):
        reader.read_dict()
    return reader.offset


def expect_corrupt(data: bytes, label: str) -> None:
    try:
        parse_retail_header(data)
    except CorruptSave:
        return
    raise AssertionError(f"{label} unexpectedly parsed as a valid save header")


def ssd_guard_payload(
    *,
    level_count: int = 1,
    weapon_count: int = 1,
    current_weapon: int = 0,
    current_level: int = 0,
    next_level: int = 0,
    pool_counts: dict[str, int] | None = None,
    pool_ids: dict[str, tuple[int, ...]] | None = None,
    entity_ref_count: int | None = None,
    entity_refs: tuple[tuple[int, int], ...] = (),
) -> bytes:
    pool_counts = pool_counts or {}
    pool_ids = pool_ids or {}

    data = (
        int32(level_count)
        + int32(weapon_count)
        + int32(current_weapon)
        + int32(current_level)
        + int32(next_level)
    )

    for label, _, _ in SSD_POOL_ORDER:
        ids = pool_ids.get(label, ())
        data += int32(pool_counts.get(label, len(ids)))
        for entity_id in ids:
            data += int32(entity_id)

    data += int32(len(entity_refs) if entity_ref_count is None else entity_ref_count)
    for entity_type, entity_id in entity_refs:
        data += int32(entity_type) + int32(entity_id)
    return data


def parse_ssd_guard_payload(data: bytes) -> None:
    SSDRestoreGuardReader(data).parse()


def expect_ssd_corrupt(data: bytes, label: str) -> None:
    try:
        parse_ssd_guard_payload(data)
    except CorruptSave:
        return
    raise AssertionError(f"{label} unexpectedly parsed as a valid SSD restore guard payload")


def validate_header_fuzz_model() -> None:
    valid = valid_retail_header()
    if parse_retail_header(valid) != len(valid):
        raise AssertionError("valid save header fixture was not fully consumed")

    expect_corrupt(b"\x00\x01", "truncated game-name length")
    expect_corrupt(int32(-1), "negative game-name length")
    expect_corrupt(int32(MAX_STRING_CHARS + 1) + (b"x" * (MAX_STRING_CHARS + 1)), "oversized game-name length")
    expect_corrupt(save_string(b"Quake4") + int32(12) + int32(100), "map-name length beyond remaining bytes")

    header_before_dicts = (
        save_string(b"Quake4")
        + int32(12)
        + save_string(b"maps/game/airdefense1.map")
        + save_string(b"")
    )
    expect_corrupt(header_before_dicts + int32(-1), "negative persistent-player dictionary count")
    expect_corrupt(
        header_before_dicts + int32(SESSION_MAX_SAVEGAME_DICT_KV + 1),
        "oversized persistent-player dictionary count",
    )
    expect_corrupt(header_before_dicts + int32(1) + b"missing-nul", "unterminated persistent-player key")
    expect_corrupt(
        header_before_dicts + int32(1) + b"key\0" + (b"value" * MAX_STRING_CHARS),
        "unterminated persistent-player value",
    )


def validate_ssd_restore_fuzz_model() -> None:
    parse_ssd_guard_payload(ssd_guard_payload())

    asteroid_type = SSD_ENTITY_TYPES["SSD_ENTITY_ASTEROID"]
    parse_ssd_guard_payload(
        ssd_guard_payload(
            pool_ids={"asteroid": (0,)},
            entity_refs=((asteroid_type, 0),),
        )
    )

    for bad_count in (-1, SSD_MAX_SAVE_LEVELS + 1):
        expect_ssd_corrupt(ssd_guard_payload(level_count=bad_count), f"bad SSD level count {bad_count}")

    for bad_count in (-1, SSD_MAX_SAVE_WEAPONS + 1):
        expect_ssd_corrupt(ssd_guard_payload(weapon_count=bad_count), f"bad SSD weapon count {bad_count}")

    expect_ssd_corrupt(ssd_guard_payload(current_level=-1), "negative SSD current level")
    expect_ssd_corrupt(ssd_guard_payload(current_level=1), "SSD current level beyond restored level count")
    expect_ssd_corrupt(ssd_guard_payload(next_level=-1), "negative SSD next level")
    expect_ssd_corrupt(ssd_guard_payload(next_level=2), "SSD next level beyond restored level count")
    expect_ssd_corrupt(ssd_guard_payload(current_weapon=-1), "negative SSD current weapon")
    expect_ssd_corrupt(ssd_guard_payload(current_weapon=1), "SSD current weapon beyond restored weapon count")

    for label, _, max_count in SSD_POOL_ORDER:
        expect_ssd_corrupt(
            ssd_guard_payload(pool_counts={label: -1}),
            f"negative SSD {label} pool count",
        )
        expect_ssd_corrupt(
            ssd_guard_payload(pool_counts={label: max_count + 1}),
            f"oversized SSD {label} pool count",
        )
        expect_ssd_corrupt(
            ssd_guard_payload(pool_ids={label: (-1,)}),
            f"negative SSD {label} pool id",
        )
        expect_ssd_corrupt(
            ssd_guard_payload(pool_ids={label: (max_count,)}),
            f"oversized SSD {label} pool id",
        )

    unknown_type = max(SSD_ENTITY_POOL_LIMITS) + 1
    expect_ssd_corrupt(
        ssd_guard_payload(entity_refs=((unknown_type, 0),)),
        "unknown SSD entity reference type",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(pool_ids={"asteroid": (0,)}, entity_refs=((asteroid_type, -1),)),
        "negative SSD entity reference id",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(pool_ids={"asteroid": (0,)}, entity_refs=((asteroid_type, SSD_MAX_ASTEROIDS),)),
        "oversized SSD entity reference id",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(entity_refs=((asteroid_type, 0),)),
        "SSD entity reference to inactive pool id",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(entity_ref_count=-1),
        "negative SSD entity reference count",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(entity_ref_count=SSD_MAX_SAVE_ENTITY_REFS + 1),
        "oversized SSD entity reference count",
    )


def validate_session_source_contract() -> None:
    source = read("src/framework/Session.cpp")
    menu_source = read("src/framework/Session_menu.cpp")
    dict_source = read("src/idlib/Dict.cpp")
    sound_world_source = read("src/sound/snd_world.cpp")

    for token in (
        '#include "BuildVersion.h"',
        "static bool Session_WriteSaveGameBytes( idFile *file, const void *buffer, int len, const char *fieldName, const char *savePath )",
        "static bool Session_WriteSaveGameInt( idFile *file, int value, const char *fieldName, const char *savePath )",
        "static bool Session_WriteSaveGameUnsignedInt( idFile *file, unsigned int value, const char *fieldName, const char *savePath )",
        "static bool Session_WriteSaveGameString( idFile *file, const char *string, int maxLength, const char *fieldName, const char *savePath )",
        "static bool Session_WriteSaveGameCString( idFile *file, const char *string, int maxLength, const char *fieldName, const char *savePath )",
        "static bool Session_WriteSaveGameDict( idFile *file, const idDict &dict, const char *fieldName, const char *savePath )",
        "static bool Session_WriteSaveTextLine( idFile *file, const char *line, bool quoted, const char *fieldName, const char *savePath )",
        "SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC",
        "SESSION_OPENQ4_SAVEGAME_FOOTER_MAGIC",
        "SESSION_OPENQ4_SAVEGAME_FOOTER_BYTES",
        "SESSION_OPENQ4_SAVEGAME_INTEGRITY_MAGIC",
        "SESSION_OPENQ4_SAVEGAME_INTEGRITY_BYTES",
        "SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_VERSION = 5",
        "SESSION_OPENQ4_SAVEGAME_TYPED_COMPATIBILITY_VERSION = 3",
        "SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_VERSION == OPENQ4_SAVEGAME_PORTABLE_VERSION",
        "SESSION_OPENQ4_SAVEGAME_MINIMUM_SUPPORTED_BUILD",
        "static const char *Session_GetSaveGameWireABI( void )",
        "SESSION_MAX_SAVE_DESCRIPTION_BYTES = 8192",
        "SESSION_MAX_SAVE_PREVIEW_BYTES = 64 * 1024 * 1024",
        "static bool Session_IsSafeSaveMaterialPath( const idStr &path )",
        "static bool Session_SaveDescriptionMatchesSlot( const idStr &expectedSlotName, const idStr &descriptionSaveName )",
        "static bool Session_ValidateStagedSaveDescription( const idStr &descriptionPath, const idStr &expectedSlotName, saveType_t saveType )",
        "static bool Session_ValidateStagedSavePreview( const idStr &previewPath )",
        "static bool Session_ValidateSaveGamePayload( idFile *file, const idStr &savePath, bool allowLegacyPayload, sessionSaveGamePayload_t *payloadOut = NULL,",
        "payloadVersion < OPENQ4_SAVEGAME_V2_VERSION ||",
        "payloadVersion > SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_VERSION",
        "const sessionVerifiedSaveChecksum_t *verifiedChecksum = NULL",
        "verifiedChecksum->file == file",
        "verifiedChecksum->protectedLength == savedProtectedLength",
        "calculatedChecksum = verifiedChecksum->checksum;",
        "storage.success = verified == storage.payload;",
        "static void Session_PersistCheckpointPreview( const idJobContext &context )",
        "verified == bytes;",
        "sessionPendingCheckpointPreviewStorage->jobs->TryWait()",
        "if ( !sessionPendingCheckpointPreviewReady || !storageReady || !previewStorageReady || !commitReady )",
        "previewStorage && previewStorage->jobs && previewStorage->success",
        "( previewPersisted || Session_SyncRelativeSaveFile( tempPreviewFile ) )",
        "OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH",
        "OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT",
        "if ( marker != SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC )",
        "OpenQ4_FindSaveGameRelease( payloadBuild, payloadSourceStamp.c_str(), payloadSourceFileCount )",
        "OPENQ4_SAVEGAME_V2_FIRST_RELEASE_BUILD : SESSION_OPENQ4_SAVEGAME_MINIMUM_SUPPORTED_BUILD;",
        "layoutBuild < minimumBuild",
        "payloadWireABI = OPENQ4_SAVEGAME_V2_WIRE_ABI;",
        "OpenQ4_SaveGamePortableWireABI( Session_GetSaveGameWireABI() ) : Session_GetSaveGameWireABI();",
        "payloadWireABI.Icmp( wireABI ) != 0",
        "payloadOut->otherPlatform = true;",
        "loadGameFailure = LOADGAME_FAILURE_PLATFORM;",
        "static bool Session_CalculateSaveGameChecksum( idFile *file, int protectedLength, unsigned int &checksum, const idStr &savePath )",
        "static bool Session_AppendSaveGameIntegrityTrailer( const idStr &relativePath )",
        "calculatedChecksum != savedChecksum",
        "const int footerOffset = protectedLength - SESSION_OPENQ4_SAVEGAME_FOOTER_BYTES;",
        "if ( valid && footerMarker != SESSION_OPENQ4_SAVEGAME_FOOTER_MAGIC )",
        "if ( valid && savedFooterOffset != footerOffset )",
        "if ( valid && !Session_SaveDescriptionMatchesSlot( expectedSlotName, sidecarSaveName ) )",
        "if ( valid && !Session_IsSafeSaveMaterialPath( sidecarScreenshot ) )",
        "if ( imageType != 2 && imageType != 10 )",
        "if ( bitsPerPixel != 24 && bitsPerPixel != 32 )",
        "Session_ValidateStagedSaveDescription( tempDescriptionFile, saveSlotFileName, saveType )",
        "Session_ValidateStagedSavePreview( tempPreviewFile )",
        "Session_RemoveRelativeSaveFile( tempPreviewFile );",
        "if ( bytesWritten != len )",
        "if ( bytesWritten != sizeof( value ) )",
        "if ( count < 0 || count > SESSION_MAX_SAVEGAME_DICT_KV )",
        "static bool Session_ReadSaveGameInt( idFile *file, int &value, const char *fieldName, const char *savePath )",
        "static bool Session_ReadSaveGameUnsignedInt( idFile *file, unsigned int &value, const char *fieldName, const char *savePath )",
        "if ( bytesRead != sizeof( value ) )",
        "static bool Session_ReadSaveGameString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath )",
        "if ( len < 0 || len > maxLength || len > remainingBytes )",
        "if ( bytesRead != len )",
        "static bool Session_ReadSaveGameCString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath )",
        "for ( int len = 0; len < maxLength; len++ )",
        "has unterminated %s",
        "static bool Session_ReadSaveGameDict( idFile *file, idDict &dict, const char *fieldName, const char *savePath )",
        "SESSION_MAX_SAVEGAME_DICT_KV = 16384",
        "if ( count < 0 || count > SESSION_MAX_SAVEGAME_DICT_KV )",
        "Session_ReadSaveGameCString( file, key, MAX_STRING_CHARS",
        "Session_ReadSaveGameCString( file, value, MAX_STRING_CHARS",
        "idDict loadedPersistentPlayerInfo[MAX_ASYNC_CLIENTS];",
        "idFile *loadGameFile = fileSystem->OpenFileRead",
        "loadingSaveGame = false;",
        "savegameFile = NULL;",
        "if ( headerValid && saveMap.IsEmpty() )",
        "static bool Session_IsSafeNormalizedMapPath( const idStr &path )",
        "static bool Session_SaveGameMapExists( const idStr &normalizedPath, const idStr &normalizedEntityFilter )",
        "Session_RecoverInterruptedSaveFiles( in, descriptionPath, previewPath )",
        "Session_IsCompatibleSaveGameVersion( loadedSavegameVersion )",
        "Session_SaveGameMapExists( normalizedSaveMap, normalizedEntityFilter )",
        "static bool Session_RecoverInterruptedSaveFiles( const idStr &gameFile, const idStr &descriptionFile, const idStr &previewFile )",
        "The payload is committed last.",
        "mapSpawnData.persistentPlayerInfo[i] = loadedPersistentPlayerInfo[i];",
        "savegameFile = loadGameFile;",
        "inFileName[i] <= ' '",
    ):
        require(source, token, "Session savegame corruption reader contract")

    for field_name in ("game name", "map name", "entity filter"):
        require_regex(
            source,
            rf"Session_ReadSaveGameString\s*\(\s*loadGameFile,\s*[^,]+,\s*MAX_STRING_CHARS,\s*\"{field_name}\"",
            f"load savegame bounded {field_name} header read",
        )

    require(source, "if ( headerValid && !Session_IsSupportedSaveGameName( gamename ) )", "savegame header name allowlist")
    require(source, "for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ )", "fixed persistent-player dictionary slots")
    require_regex(
        source,
        r"Session_ReadSaveGameDict\s*\(\s*loadGameFile,\s*loadedPersistentPlayerInfo\s*\[\s*i\s*\],"
        r"\s*va\s*\(\s*\"persistent player info %d\"",
        "bounded persistent-player dictionary reads into temporary load state",
    )
    require_regex(
        source,
        r"for\s*\(\s*i\s*=\s*0;\s*i\s*<\s*MAX_ASYNC_CLIENTS;\s*i\+\+\s*\).*?"
        r"mapSpawnData\.persistentPlayerInfo\s*\[\s*i\s*\]\s*=\s*loadedPersistentPlayerInfo\s*\[\s*i\s*\];",
        "persistent-player dictionaries are committed after full header validation",
    )
    require_regex(
        source,
        r"Session_ReadSaveGameDict\s*\(\s*loadGameFile,\s*loadedPersistentPlayerInfo.*?"
        r"Session_ValidateSaveGamePayload\s*\(\s*loadGameFile,\s*in,\s*true,\s*&payload\s*\).*?"
        r"Session_SaveGameMapExists\s*\(\s*normalizedSaveMap,\s*normalizedEntityFilter\s*\).*?"
        r"mapSpawnData\.persistentPlayerInfo.*?ExecuteMapChange",
        "payload integrity and target map are preflighted before load state is committed or the map changes",
    )
    require_regex(
        source,
        r"Session_ReadSaveGameString\s*\(\s*file,\s*gamename,\s*MAX_STRING_CHARS,\s*\"game name\".*?"
        r"Session_ReadSaveGameString\s*\(\s*file,\s*saveMap,\s*MAX_STRING_CHARS,\s*\"map name\".*?"
        r"Session_ReadSaveGameDict\s*\(\s*file,\s*persistentPlayerInfo.*?"
        r"Session_ValidateSaveGamePayload\s*\(\s*file,\s*savePath,\s*false,\s*NULL,\s*verifiedChecksum\s*\)",
        "staged save validation uses bounded header/dict reads and payload footer check",
    )
    require_regex(
        source,
        r"Session_WriteSaveTextLine\s*\(\s*fileDesc,\s*escapedSlotName.*?"
        r"if\s*\(\s*!descriptionWritten\s*\).*?"
        r"Session_ValidateStagedSaveDescription\s*\(\s*tempDescriptionFile,\s*saveSlotFileName,\s*saveType\s*\)",
        "staged save description write and validation cleanup",
    )
    require_regex(
        source,
        r"Session_WriteSaveGameString\s*\(\s*fileOut,\s*SAVEGAME_GAME_NAME_RETAIL.*?"
        r"Session_WriteSaveGameDict\s*\(\s*fileOut,\s*mapSpawnData\.persistentPlayerInfo\s*\[\s*i\s*\].*?"
        r"if\s*\(\s*!headerWritten\s*\)",
        "staged save header write failure cleanup",
    )
    require_regex(
        source,
        r"game->SaveGame\s*\(\s*fileOut,\s*saveType\s*\).*?"
        r"Session_AppendSaveGameIntegrityTrailer\s*\(\s*tempGameFile\s*\).*?"
        r"Session_ValidateStagedSaveGameHeader\s*\(\s*tempGameFile\s*\)",
        "integrity trailer is appended before staged payload validation",
    )
    require_regex(
        source,
        r"Session_InitStagedSaveFile\s*\([^;]+tempDescriptionFile[^;]+;.*?"
        r"Session_InitStagedSaveFile\s*\([^;]+tempGameFile[^;]+;.*?"
        r"Session_CommitStagedSaveFiles",
        "sidecars are committed before the payload commit point",
    )

    for token in (
        "SESSION_MENU_MAX_SAVE_DESCRIPTION_BYTES = 8192",
        "SESSION_MENU_MAX_SAVEGAME_DICT_KV = 16384",
        "SESSION_MENU_MAX_SAVEGAME_BASENAME = 96",
        "static bool Session_MenuIsSafeSaveSlotName( const idStr &slotName )",
        "static bool Session_MenuSaveDescriptionMatchesSlot( const idStr &slotName, const idStr &descriptionSaveName )",
        "static bool Session_MenuIsSafeSaveMaterialPath( const idStr &path )",
        "sessLocal.ScrubSaveGameFileName( scrubbedName );",
        "idStr::FindText( path.c_str(), \"..\" )",
        "static bool Session_MenuReadSaveGameString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath )",
        "static bool Session_MenuReadSaveGameCString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath )",
        "static bool Session_MenuSkipSaveGameDict( idFile *file, const char *fieldName, const char *savePath )",
        "static bool Session_MenuIsLoadableSaveGameSlot( const idStr &slotName )",
        "static bool Session_MenuReadSaveDescription( const idStr &slotName, sessionMenuSaveDescription_t &description )",
        "len > SESSION_MENU_MAX_SAVE_DESCRIPTION_BYTES",
        "idLexer src( buffer, len, descriptionPath.c_str(), LEXFL_NOERRORS | LEXFL_NOSTRINGCONCAT );",
        "!Session_MenuSaveDescriptionMatchesSlot( slotName, description.saveName )",
        "!Session_MenuIsSafeSaveMaterialPath( description.screenshot )",
        "!Session_MenuIsLoadableSaveGameSlot( slotName )",
        "Session_MenuReadSaveDescription( loadGameList[i], description )",
        "description.screenshot.Length() > 0 || description.noOverwrite",
    ):
        require(menu_source, token, "Session savegame menu/list contract")

    for token in (
        "static void DictWriteChecked( idFile *f, int bytesWritten, int expected, const char *detail, int offset )",
        "idDict::WriteToFileHandle: failed to write %s at offset %d",
        "if ( args.Num() < 0 || args.Num() > MAX_DICT_FILE_KV )",
        "DictWriteChecked( f, f->Write( s, len + 1 ), len + 1, \"string\", offset );",
        "DictWriteChecked( f, f->Write( &c, sizeof( c ) ), static_cast<int>( sizeof( c ) ), \"key/value count\", offset );",
    ):
        require(dict_source, token, "idDict savegame write contract")

    for token in (
        "SOUND_SAVEGAME_MAX_EMITTERS = 8192",
        "SOUND_SAVEGAME_MAX_TOTAL_CHANNELS = 8192",
        "static void WriteChecked( idFile* savefile, int bytesWritten, int expected, const char* fieldName, int offset )",
        "idSoundWorldLocal::WriteToSaveGame: failed to write %s at offset %d",
        "while( num > 1 )",
        "const int restoredIndex = emitters.Append( emitter );",
        "Do not use AllocSoundEmitter here",
        "class idSoundStateReader",
        "bool ReadInt( int& value, const char* fieldName )",
        "common->Error( \"idSoundWorldLocal::ReadFromSaveGame: %s\", detail );",
        "has a truncated %s",
        "bool ReadString( idStr& value, const char* fieldName )",
        "len < 0 || len > MAX_STRING_CHARS",
        "parms.soundClass < 0 || parms.soundClass >= SOUND_MAX_CLASSES",
        "numEmitters < 1 || numEmitters > SOUND_SAVEGAME_MAX_EMITTERS",
        "numChannels < 0 || numChannels > MAX_CHANNELS_PER_EMITTER",
        "totalChannels > SOUND_SAVEGAME_MAX_TOTAL_CHANNELS - numChannels",
        "idSoundStateReader reader( savefile, demoFile );",
        "reader.ReadString( shaderName, \"sound shader name\" )",
    ):
        require(sound_world_source, token, "sound-world savegame restore corruption contract")


def validate_gamelibs_save_payload_contract() -> None:
    for module in ("game", "mpgame"):
        save_header = read_game_libs(f"src/{module}/gamesys/SaveGame.h")
        save_source = read_game_libs(f"src/{module}/gamesys/SaveGame.cpp")
        game_header = read_game_libs(f"src/{module}/Game_local.h")
        game_local = read_game_libs(f"src/{module}/Game_local.cpp")
        actor_source = read_game_libs(f"src/{module}/Actor.cpp")
        ai_move = read_game_libs(f"src/{module}/ai/AI_Move.cpp")
        client_effect = read_game_libs(f"src/{module}/client/ClientEffect.cpp")
        client_entity = read_game_libs(f"src/{module}/client/ClientEntity.cpp")
        misc_source = read_game_libs(f"src/{module}/Misc.cpp")
        mover_source = read_game_libs(f"src/{module}/Mover.cpp")
        physics_af = read_game_libs(f"src/{module}/physics/Physics_AF.cpp")
        script_interpreter = read_game_libs(f"src/{module}/script/Script_Interpreter.cpp")
        script_program = read_game_libs(f"src/{module}/script/Script_Program.h")
        script_program_source = read_game_libs(f"src/{module}/script/Script_Program.cpp")

        for token in (
            "OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'S' << 24 )",
            "OPENQ4_SAVEGAME_COMPATIBILITY_VERSION = 5",
            "OPENQ4_SAVEGAME_PORTABLE_COMPATIBILITY_VERSION = 5",
            "OPENQ4_SAVEGAME_SYNC_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'Y' << 24 )",
            "OPENQ4_SAVEGAME_FOOTER_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'F' << 24 )",
            "OPENQ4_SAVEGAME_INTEGRITY_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'I' << 24 )",
            "void\t\t\t\t\tWriteChecked( int bytesWritten, int expected, const char *detail, int offset );",
            "void\t\t\t\t\tWriteSaveGameFooter( int numObjects );",
            "void\t\t\t\t\tReadSyncId( const char *detail = \"unspecified\", const char *classname = NULL );",
            "void\t\t\t\t\tReadSaveGameFooter( void );",
            "bool\t\t\t\t\tIsOpenQ4SaveGameCompatible( void ) const;",
            "const char *\t\t\tGetOpenQ4SaveGameCompatibilityError( void ) const;",
            "int\t\t\t\t\t\topenQ4SaveGameCompatibilitySourceFileCount;",
            "int\t\t\t\t\t\topenQ4SaveGameNextSyncId;",
            "bool\t\t\t\t\topenQ4SaveGameSyncMarkersEnabled;",
            "idStr\t\t\t\t\topenQ4SaveGameCompatibilityStamp;",
            "idStr\t\t\t\t\topenQ4SaveGameCompatibilityError;",
            "idHashIndex\t\t\t\tobjectHash;",
        ):
            require(save_header, token, f"{module} savegame compatibility header")

        # The generated stamp hashes every save-relevant game source, so it changes
        # whenever one of them does. Game_local.h brings the game headers into every
        # game PCH, so a header that includes the stamp makes each such edit rebuild
        # every game object. Only SaveGame.cpp reads it.
        for header_path in sorted((GAME_LIBS_ROOT / "src" / module).rglob("*.h")):
            if b"openq4_savegame_compat_generated.h" in header_path.read_bytes():
                raise AssertionError(
                    f"{header_path.relative_to(GAME_LIBS_ROOT).as_posix()} includes the generated "
                    "savegame stamp; include it from gamesys/SaveGame.cpp so the game PCH does not depend on it"
                )

        for token in (
            '#include "framework/BuildVersion.h"',
            '__has_include( "openq4_savegame_compat_generated.h" )',
            '#include "openq4_savegame_compat_generated.h"',
            '#define OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH "standalone-openq4-game"',
            "#define OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT 0",
            "static bool SaveGame_IsValidRenderBounds( const idBounds &bounds )",
            "static int SaveGame_ObjectHashKey( const idClass *obj )",
            "static int SaveGame_FindObjectIndex( const idList<const idClass *> &objects, const idHashIndex &objectHash, const idClass *obj )",
            "objectHash.Add( SaveGame_ObjectHashKey( obj ), index );",
            "if ( objects.Num() == 0 )",
            "const float boundsMin = bounds[0][i];",
            "const float boundsMax = bounds[1][i];",
            "FLOAT_IS_NAN( boundsMin ) || FLOAT_IS_NAN( boundsMax )",
            "bounds[1][i] - bounds[0][i] >= MAX_BOUND_SIZE",
            "void idSaveGame::WriteChecked( int bytesWritten, int expected, const char *detail, int offset )",
            "idSaveGame: failed to write %s at offset %d",
            "WriteChecked( file->Write( writeBuffer, writeBufferUsed ), writeBufferUsed, \"buffered data\", offset );",
            "WriteChecked( file->Write( buffer, len ), len, \"raw data\", offset );",
            "const int encoded = LittleLong( value );",
            "Write( string, len );",
            "const int footerOffset = file->Tell() + writeBufferUsed;",
            "WriteBool( unique );\n\tFlushWriteBuffer();",
            "if ( !ui->WriteToSaveGame( file ) )",
            "FlushWriteBuffer();\n\tsoundSystem->WriteToSaveGame( SOUNDWORLD_GAME, file );",
            "WriteSaveGameFooter( numObjects );\n\tFlushWriteBuffer();",
            "idSaveGame::WriteWinding: invalid point count %d",
            "idSaveGame::WriteDict: invalid key/value count %d (max %d)",
            "WriteInt( OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC );",
            "WriteInt( OPENQ4_SAVEGAME_COMPATIBILITY_VERSION );",
            "WriteString( OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH );",
            "WriteInt( OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT );",
            "WriteString( OpenQ4_SaveGamePortableWireABI( OpenQ4SaveGameWireABI() ) );",
            "WriteInt( OPENQ4_SAVEGAME_SYNC_MAGIC );",
            "void idSaveGame::WriteSaveGameFooter( int numObjects )",
            "WriteInt( OPENQ4_SAVEGAME_FOOTER_MAGIC );",
            "ReadInt( marker );",
            "if ( marker != OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC )",
            "if ( buildNumber == BUILD_NUMBER &&",
            "idStr::Icmp( OpenQ4SaveGameWireABI(), \"windows-msvcabi-x64-le-raw1\" ) == 0",
            "legacy save payload build/ABI %d/%s is not supported by current build/ABI %d/%s",
            "ReadInt( openQ4SaveGameCompatibilityVersion );",
            "ReadString( openQ4SaveGameCompatibilityStamp );",
            "ReadInt( openQ4SaveGameCompatibilitySourceFileCount );",
            "openQ4SaveGameCompatibilityVersion > OPENQ4_SAVEGAME_COMPATIBILITY_VERSION",
            "OPENQ4_SAVEGAME_BUILD_WITH_PLAYER_SWIM_SPEED",
            "OPENQ4_SAVEGAME_BUILD_WITH_PLAYER_LIQUID_SOUND",
            "payload format version %d is not supported (supported %d to %d)",
            "openQ4SaveGameCompatibilityVersion < OPENQ4_SAVEGAME_V2_VERSION",
            "ReadString( savedWireABI );",
            "OpenQ4_SaveGamePortableWireABI( OpenQ4SaveGameWireABI() ) : OpenQ4SaveGameWireABI();",
            "savedWireABI.Icmp( wireABI ) != 0",
            "Schema-compatible save source differs",
            "void idRestoreGame::ReadSyncId( const char *detail, const char *classname )",
            "marker mismatch while reading %s%s%s",
            "sequence mismatch while reading %s%s%s",
            "void idRestoreGame::ReadSaveGameFooter( void )",
            "OPENQ4_SAVEGAME_FOOTER_MAGIC",
            "OPENQ4_SAVEGAME_INTEGRITY_MAGIC",
            "protectedLength != integrityOffset",
            "unexpected trailing bytes after savegame payload",
            "bool idRestoreGame::IsOpenQ4SaveGameCompatible( void ) const",
            "const char *idRestoreGame::GetOpenQ4SaveGameCompatibilityError( void ) const",
            "!SaveGame_IsValidRenderBounds( renderEntity.bounds )",
            "idRestoreGame::ReadRenderEntity: invalid render bounds",
        ):
            require(save_source, token, f"{module} savegame compatibility source")

        destructor_start = save_source.index("idSaveGame::~idSaveGame( void )")
        destructor_end = save_source.index("/*", destructor_start)
        destructor_block = save_source[destructor_start:destructor_end]
        if "Close();" in destructor_block:
            raise AssertionError(
                f"{module} idSaveGame destructor must not serialize while unwinding a failed save"
            )

        write_object_start = save_source.index(
            "void idSaveGame::WriteObject( const idClass *obj )"
        )
        write_object_end = save_source.index("/*", write_object_start)
        write_object_block = save_source[write_object_start:write_object_end]
        for token in (
            "SaveGame_FindObjectIndex( objects, objectHash, obj )",
            "WriteObject FindIndex failed; writing NULL reference",
            "index = 0;",
            "WriteInt( index );",
        ):
            require(write_object_block, token, f"{module} transient save reference fallback")
        if "gameLocal.Error" in write_object_block or "obj->GetClassname()" in write_object_block:
            raise AssertionError(
                f"{module} transient save reference fallback must not abort or dereference an unregistered object"
            )

        require(game_local, "savegame.Close();", f"{module} primary save explicitly closes its writer")
        require(save_source, "sg.Close();", f"{module} checkSave explicitly closes its writer")

        require_regex(
            game_local,
            r"savegame\.ReadBuildNumber\(\);.*?"
            r"if\s*\(\s*!savegame\.IsOpenQ4SaveGameCompatible\(\)\s*\).*?"
            r"GetOpenQ4SaveGameCompatibilityError\(\).*?"
            r"return false;.*?"
            r"savegame\.CreateObjects\(\);",
            f"{module} savegame compatibility gate before object restore",
        )

        for token in (
            "memset( &fl, 0, sizeof( fl ) );",
            "path[i].reach = NULL;",
            "path[i].seekPos.Zero();",
            "pathLen\t\t\t\t= 0;",
            "pathArea\t\t\t= 0;",
            "pathTime\t\t\t= 0;",
            "const int savedPathLen = ( pathLen >= 0 && pathLen <= MAX_PATH_LEN ) ? pathLen : 0;",
            "savefile->WriteInt( savedPathLen );\t// cnicholson: Added unsaved vars",
            "for (i=0; i< savedPathLen; ++i)",
            "savefile->WriteInt( savedPathLen > 0 ? pathArea : 0 );",
        ):
            require(ai_move, token, f"{module} AI move savegame path initialization contract")

        require(
            script_program,
            "NumFunctions( void ) { return functions.Num(); }",
            f"{module} script program function-count accessor",
        )

        for token in (
            "if ( callStackDepth < 0 || callStackDepth > MAX_STACK_DEPTH )",
            "callStack[i].s < -1 || callStack[i].s >= gameLocal.program.NumStatements()",
            "func_index >= gameLocal.program.NumFunctions()",
            "func_index == -1",
            "callStack[i].stackbase < 0 || callStack[i].stackbase > LOCALSTACK_SIZE",
            "maxStackDepth < callStackDepth || maxStackDepth > MAX_STACK_DEPTH",
            "localstackUsed < 0 || localstackUsed > LOCALSTACK_SIZE",
            "memset( localstack, 0, sizeof( localstack ) );",
            "savefile->Read( localstack, localstackUsed );",
            "localstackBase < 0 || localstackBase > localstackUsed",
            "maxLocalstackUsed < localstackUsed || maxLocalstackUsed > LOCALSTACK_SIZE",
            "instructionPointer < -1 || instructionPointer >= gameLocal.program.NumStatements()",
            "popParms < 0 || popParms > localstackUsed",
            "unknown multi-frame event",
            "multiFrameEvent = NULL;",
        ):
            require(script_interpreter, token, f"{module} script interpreter restore bounds contract")

        for token in (
            "MAX_SAVEGAME_ANIMATED_ANIMS",
            "MAX_SAVEGAME_CLIENT_CRAWL_JOINTS",
            "MAX_SAVEGAME_ACTOR_ATTACHMENTS",
            "MAX_SAVEGAME_ELEVATOR_FLOORS",
        ):
            require(game_header, token, f"{module} savegame scalar/list limits")

        for token in (
            "channel < ANIMCHANNEL_ALL || channel >= ANIM_NumAnimChannels",
            "idAnimState::Restore: invalid animation channel %d",
            "idActor::Restore: invalid attachment animation channel %d",
        ):
            require(actor_source, token, f"{module} actor animation channel restore bounds")

        for token in (
            "restoredEntityNumber < -1 || restoredEntityNumber >= MAX_CENTITIES",
            "rvClientEntity::Restore: invalid client entity number %d",
        ):
            require(client_entity, token, f"{module} client entity restore bounds")

        for token in (
            "jointStart < 0 || jointStart >= numJoints",
            "jointEnd < 0 || jointEnd >= numJoints",
            "crawlDir != -1 && crawlDir != 1",
        ):
            require(client_effect, token, f"{module} client crawl effect restore bounds")

        for token in (
            "num_anims < 0 || num_anims > MAX_SAVEGAME_ANIMATED_ANIMS",
            "current_anim_index < 0 || current_anim_index > num_anims",
            "anim < 0 || anim > animator.NumAnims()",
        ):
            require(misc_source, token, f"{module} idAnimated restore bounds")

        for token in (
            "savedState < INIT || savedState > WAITING_ON_DOORS",
            "idElevator::Restore: invalid elevator state %d",
            "num < 0 || num > MAX_SAVEGAME_ELEVATOR_FLOORS",
            "idElevator::Restore: invalid floor count %d",
        ):
            require(mover_source, token, f"{module} elevator restore bounds")

        for token in (
            "idPhysics_AF::Restore: articulated body count mismatch %d (expected %d)",
            "idPhysics_AF::Restore: articulated constraint count mismatch %d (expected %d)",
        ):
            require(physics_af, token, f"{module} articulated physics restore count contract")

        for token in (
            "top_functions < 0 || top_functions > functions.Num()",
            "top_statements < 0 || top_statements > statements.Num()",
            "top_types < 0 || top_types > types.Num()",
            "top_defs < 0 || top_defs > varDefs.Num()",
            "top_files < 0 || top_files > fileList.Num()",
        ):
            require(script_program_source, token, f"{module} script program restore watermark bounds")

    meson = read("meson.build")
    for token in (
        "generate_savegame_compat_header.py",
        "openq4_savegame_compat_generated.h",
        "savegame_compat_header = custom_target",
        "game_sp_sources += [savegame_compat_header, generated_version_header]",
        "game_mp_sources += [savegame_compat_header, generated_version_header]",
        "openq4_engine_sources += [savegame_compat_header]",
        "depend_files: files(engine_source_paths) + game_sources",
    ):
        require(meson, token, "Meson savegame compatibility header wiring")

    generator = read("tools/build/generate_savegame_compat_header.py")
    for token in (
        "RELEVANCE_TOKENS",
        "normalize_source_bytes",
        'data.replace(b"\\r\\n", b"\\n").replace(b"\\r", b"\\n")',
        "OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH",
        "OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT",
        "write_if_changed",
        "PROJECT_SCAN_DIRS",
        "GAME_SCAN_DIRS",
        '"src/game"',
        '"src/mpgame"',
    ):
        require(generator, token, "savegame compatibility header generator")

    require(
        read("docs/dev/release-completion.md"),
        "new saves carry a generated engine/GameLibs source snapshot stamp plus payload sync markers, an end footer, an exact wire-ABI stamp, and whole-file CRC protection",
        "release completion notes",
    )


def validate_savegame_compat_stamp_model() -> None:
    generator_path = ROOT / "tools" / "build" / "generate_savegame_compat_header.py"
    spec = importlib.util.spec_from_file_location("openq4_savegame_compat_generator", generator_path)
    if spec is None or spec.loader is None:
        raise AssertionError("Could not load savegame compatibility generator")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="savegame-compat-stamp-", dir=temp_root) as raw_temp:
        work = Path(raw_temp)
        project_root = work / "openQ4"
        game_root = work / "stage"
        project_file = project_root / "src" / "framework" / "Session.cpp"
        game_file = game_root / "src" / "game" / "Game_local.cpp"
        project_file.parent.mkdir(parents=True)
        game_file.parent.mkdir(parents=True)

        project_file.write_bytes(b"void SaveGame();\n")
        game_file.write_bytes(b"void Restore();\n")
        lf_hash, lf_count = generator.build_digest(project_root, game_root)

        project_file.write_bytes(b"void SaveGame();\r\n")
        game_file.write_bytes(b"void Restore();\r\n")
        crlf_hash, crlf_count = generator.build_digest(project_root, game_root)
        if (crlf_hash, crlf_count) != (lf_hash, lf_count):
            raise AssertionError("Savegame compatibility stamp varies with checkout line endings")

        game_file.write_bytes(b"void Restore();\nvoid Save();\n")
        changed_hash, changed_count = generator.build_digest(project_root, game_root)
        if changed_hash == lf_hash or changed_count != lf_count:
            raise AssertionError("Savegame compatibility stamp did not track a relevant semantic source change")


# A GUI variable that leaves its payload uninitialized reaches the savegame writer
# holding whatever the heap last left there. game/airdefense1 lost a save that way:
# a scalar transition operand such as "-180" fills only x through sscanf, so an
# uninitialized idWinVec4 carried three indeterminate floats into the write and the
# non-finite guard unloaded the running map. Types that zero themselves are exempt,
# and idRectangle's own constructor is pinned so that exemption stays true.
WINVAR_SELF_INITIALIZING_DATA_TYPES = ("idStr", "idRectangle")


def parse_winvar_classes() -> dict[str, dict[str, str | None]]:
    source = read("src/ui/Winvar.h")
    boundaries = [
        (match.start(), match.group(1))
        for match in re.finditer(r"^class\s+(\w+)\s*:\s*public\s+(?:idWinVar|idWinStr)\s*\{", source, re.MULTILINE)
    ]
    if not boundaries:
        raise AssertionError("No idWinVar subclasses found in src/ui/Winvar.h")

    offsets = [offset for offset, _ in boundaries] + [len(source)]
    classes: dict[str, dict[str, str | None]] = {}
    for index, (_, class_name) in enumerate(boundaries):
        block = source[offsets[index] : offsets[index + 1]]
        members = [
            match.group(1).replace(" ", "")
            for match in re.finditer(r"^\s*([A-Za-z_][\w]*\s*\*?)\s+data\s*;", block, re.MULTILINE)
            if "return" not in match.group(0)
        ]
        if len(members) != 1:
            raise AssertionError(f"{class_name} must declare exactly one data member, found {members!r}")
        constructor = re.search(r"\b%s\s*\(\s*\)\s*:\s*([^{]*)\{" % re.escape(class_name), block)
        classes[class_name] = {
            "data_type": members[0],
            "initializers": constructor.group(1) if constructor else None,
        }
    return classes


def validate_gui_winvar_initialization_contract() -> None:
    classes = parse_winvar_classes()
    for class_name, details in classes.items():
        data_type = details["data_type"]
        initializers = details["initializers"]
        if data_type in WINVAR_SELF_INITIALIZING_DATA_TYPES:
            continue
        if initializers is None:
            raise AssertionError(
                f"{class_name} has no default constructor initializer list, so its {data_type} data is indeterminate"
            )
        if re.search(r"\bdata\s*\(", initializers) is None:
            raise AssertionError(
                f"{class_name} must initialize its {data_type} data in the default constructor; "
                f"an indeterminate value reaches the savegame writer. Got: {initializers.strip()!r}"
            )

    for required in ("idWinVec2", "idWinVec3", "idWinVec4", "idWinFloat"):
        if required not in classes:
            raise AssertionError(f"src/ui/Winvar.h no longer declares {required}")

    # The exemption above is only sound while idRectangle zeroes itself.
    require_regex(
        read("src/ui/Rectangle.h"),
        r"idRectangle\s*\(\s*\)\s*\{\s*x\s*=\s*y\s*=\s*w\s*=\s*h\s*=\s*0",
        "idRectangle default constructor",
    )

    # idVec2/3/4 deliberately leave their members alone, which is why the winvars
    # above have to initialize theirs. Pin that so the exemption is never widened.
    vector_source = read("src/idlib/math/Vector.h")
    for vector_type in ("idVec2", "idVec3", "idVec4"):
        constructor = re.search(
            r"ID_INLINE %s::%s\( void \) \{(?P<body>.*?)\}" % (vector_type, vector_type),
            vector_source,
            re.DOTALL,
        )
        if constructor is None:
            raise AssertionError(f"{vector_type} default constructor not found in src/idlib/math/Vector.h")
        if re.search(r"\b[xyzw]\s*=", constructor.group("body")):
            raise AssertionError(
                f"{vector_type} now initializes its members; revisit the idWinVar initialization contract"
            )

    # A transition operand that names a var which does not resolve keeps its literal
    # text, which parses as nothing, so the transition silently runs from zero.
    require(
        read("src/ui/GuiScript.cpp"),
        "which is not a valid var; the transition uses zero for it",
        "unresolved transition source diagnostic",
    )


def validate_gui_register_restore_contract() -> None:
    # Unused slots in legacy saves may contain indeterminate bytes. They are not
    # consumed, while new writes zero-fill them to avoid disclosing process data.
    for register_type, register_count in enumerate(REGISTER_REGCOUNTS):
        active = tuple(range(100, 104))
        saved = tuple(active[i] if i < register_count else 0xFFFF for i in range(4))
        validate_register_schema_model(
            0 if register_type == 4 else 1,
            register_type,
            register_count,
            "windowColor",
            saved,
            register_type,
            register_count,
            "WindowColor",
            active,
        )

    valid = (10, 11, 12, 13)
    corrupt_cases = (
        (2, 0, 4, "rect", valid, 0, 4, "rect", valid),
        (1, -1, 4, "rect", valid, 0, 4, "rect", valid),
        (1, len(REGISTER_REGCOUNTS), 4, "rect", valid, 0, 4, "rect", valid),
        (1, 0, -1, "rect", valid, 0, 4, "rect", valid),
        (1, 0, 5, "rect", valid, 0, 4, "rect", valid),
        (1, 0, 3, "rect", valid, 0, 4, "rect", valid),
        (1, 0, 4, "other", valid, 0, 4, "rect", valid),
        (1, 4, 0, "text", valid, 4, 0, "text", valid),
        (1, 0, 4, "rect", (MAX_EXPRESSION_REGISTERS, 11, 12, 13), 0, 4, "rect", valid),
        (1, 0, 4, "rect", valid, 0, 4, "rect", (MAX_EXPRESSION_REGISTERS, 11, 12, 13)),
        (1, 0, 4, "rect", (9, 11, 12, 13), 0, 4, "rect", valid),
    )
    for case in corrupt_cases:
        try:
            validate_register_schema_model(*case)
        except CorruptSave:
            pass
        else:
            raise AssertionError(f"Corrupt GUI register schema was accepted: {case!r}")

    source = read("src/ui/RegExp.cpp")
    contract_tokens = (
        "savefile->WriteUnsignedChar( value )",
        "savefile->WriteShort( value )",
        "savefile->WriteInt( value )",
        "savefile->WriteUnsignedShort( value )",
        "savefile->ReadUnsignedChar( value )",
        "savefile->ReadShort( value )",
        "savefile->ReadInt( value )",
        "savefile->ReadUnsignedShort( value )",
        "const unsigned short savedReg = ( i < regCount ) ? regs[i] : 0;",
        "type == STRING && enabled",
        "unsigned short savedRegs[4] = { 0, 0, 0, 0 };",
        "savedEnabled > 1",
        "savedType < 0 || savedType >= NUMTYPES",
        "savedRegCount < 0 || savedRegCount > 4 || savedRegCount != expectedSavedRegCount",
        "savedType != type || savedRegCount != regCount || savedName.Icmp( name ) != 0",
        "savedType == STRING && savedEnabled != 0",
        "savedRegs[i] >= MAX_EXPRESSION_REGISTERS || regs[i] >= MAX_EXPRESSION_REGISTERS",
        "savedRegs[i] != regs[i]",
        "enabled = savedEnabled != 0;",
        "RegExp_WriteSaveGameInt( savefile, num, \"register list count\" );",
        "const int num = RegExp_ReadSaveGameInt( savefile, \"register list count\" );",
    )
    for token in contract_tokens:
        require(source, token, "GUI register savegame schema validation")

    read_start = source.index("void idRegister::ReadFromSaveGame( idFile *savefile )")
    read_end = source.index("idRegisterList::AddReg", read_start)
    read_block = source[read_start:read_end]
    apply_offset = read_block.index("enabled = savedEnabled != 0;")
    for token in (
        "savedType < 0 || savedType >= NUMTYPES",
        "savedRegCount < 0 || savedRegCount > 4",
        "savedName.Icmp( name ) != 0",
        "savedRegs[i] >= MAX_EXPRESSION_REGISTERS",
        "savedRegs[i] != regs[i]",
    ):
        if read_block.index(token) >= apply_offset:
            raise AssertionError(f"GUI register validation occurs after mutable state is applied: {token!r}")
    if read_block.index("var->ReadFromSaveGame( savefile );") <= apply_offset:
        raise AssertionError("GUI register variable payload is restored before schema validation")

    raw_primitive_regressions = (
        "savefile->Write( &enabled",
        "savefile->Write( &type",
        "savefile->Write( &regCount",
        "savefile->Write( &regs[0]",
        "RegExp_ReadSaveGameBytes( savefile, &enabled",
        "RegExp_ReadSaveGameBytes( savefile, &type",
        "RegExp_ReadSaveGameBytes( savefile, &regCount",
        "RegExp_ReadSaveGameBytes( savefile, &regs[0]",
    )
    for needle in raw_primitive_regressions:
        if needle in source:
            raise AssertionError(f"GUI register primitive bypasses checked endian-safe I/O: {needle!r}")

    header = read("src/ui/RegExp.h")
    for token in ("regs[0] = 0;", "regs[1] = 0;", "regs[2] = 0;", "regs[3] = 0;"):
        if header.count(token) < 2:
            raise AssertionError(f"Both GUI register constructors must initialize unused slots: {token!r}")


def validate_gui_stream_alignment_contract() -> None:
    descendant_base = -2
    max_descendants = 1024 * 1024

    def decode_window_reference(
        saved_reference: int,
        direct_children: int,
        descendant_children: int,
        allow_descendant: bool,
    ) -> tuple[str, int] | None:
        if saved_reference == -1:
            return None
        if saved_reference >= 0:
            if saved_reference >= direct_children:
                raise CorruptSave("direct GUI child reference is out of range")
            return ("direct", saved_reference)
        if not allow_descendant or saved_reference > descendant_base:
            raise CorruptSave("tagged GUI descendant reference is not allowed")
        ordinal = descendant_base - saved_reference
        if ordinal < 0 or ordinal >= descendant_children or ordinal >= max_descendants:
            raise CorruptSave("GUI descendant ordinal is out of range")
        return ("descendant", ordinal)

    if decode_window_reference(-1, 3, 9, False) is not None:
        raise AssertionError("Null GUI window reference did not remain null")
    if decode_window_reference(1, 3, 9, False) != ("direct", 1):
        raise AssertionError("Legacy direct GUI child reference changed meaning")
    if decode_window_reference(-2, 3, 9, True) != ("descendant", 0):
        raise AssertionError("First tagged GUI descendant reference decoded incorrectly")
    if decode_window_reference(-10, 3, 9, True) != ("descendant", 8):
        raise AssertionError("Tagged GUI descendant reference decoded incorrectly")

    corrupt_window_references = (
        (3, 3, 9, False),
        (-2, 3, 9, False),
        (-11, 3, 9, True),
        (-(2**31), 3, 9, True),
    )
    for case in corrupt_window_references:
        try:
            decode_window_reference(*case)
        except CorruptSave:
            pass
        else:
            raise AssertionError(f"Corrupt GUI window reference was accepted: {case!r}")

    ui_source = read("src/ui/UserInterface.cpp")
    for token in (
        "UI_MAX_SAVEGAME_STATE_ENTRIES = 16384",
        "UI_MAX_SAVEGAME_STATE_BYTES = 16 * 1024 * 1024",
        "UI_ReadSaveGameInt( savefile, num, \"state count\" )",
        "UI_ReadSaveGameBool( savefile, restoredActive, \"active flag\" )",
        "savedValue > 1",
        "idUserInterfaceLocal::ReadFromSaveGame: non-finite %s at offset %d",
        "restoredState.FindKey( key ) != NULL",
        "savefile == NULL || desktop == NULL",
        "SetCursor( restoredCursorX, restoredCursorY );",
        "desktop->ReadFromSaveGame( savefile );",
    ):
        require(ui_source, token, "top-level GUI positional restore validation")

    for game in ("game", "mpgame"):
        require(read(f"src/{game}/gamesys/SaveGame.cpp"),
                "aborting before its positional state payload can desynchronize the stream",
                "serialized GUI restore boundary")

    window_source = read("src/ui/Window.cpp")
    for token in (
        "savedChildID != childID",
        "const unsigned int structuralFlagMask = WIN_CHILD | WIN_DESKTOP;",
        "savedName.Icmp( name ) != 0",
        "SAVEGAME_WINDOW_REFERENCE_DESCENDANT_BASE = -2",
        "if ( !BuildSaveGameChildOrder( orderedChildren, \"idWindow::ReadFromSaveGame\" ) )",
        "static_cast<int64>( SAVEGAME_WINDOW_REFERENCE_DESCENDANT_BASE ) - static_cast<int64>( savedChildId )",
        "&hadSavedFocusReference, &focusReadSucceeded",
        "&hadSavedCaptureReference, &captureReadSucceeded",
        "NULL, &overReadSucceeded",
        "if ( !ValidateRestoredTrackedWindowPointers( hadSavedFocusReference, hadSavedCaptureReference ) )",
        "if ( hadSavedFocusReference || focusedChild != NULL )",
        "if ( hadSavedCaptureReference || captureChild != NULL )",
        "if ( focusMatches > 1 )",
        "if ( captureMatches > 1 )",
        "have duplicate id",
        "OpenQ4_WriteSaveGameInt( savefile, len, \"idWindow::WriteSaveGameString\", \"length\" )",
        "could not resolve transition target for window",
        "transition target '%s' for window '%s' in gui '%s' is missing or ambiguous",
        "OpenQ4_WriteSaveGameInt( savefile, savedTarget, \"idWindow::WriteSaveGameTransition\", \"target field\" )",
        "TransitionFieldOffset( dw->simp != NULL, transitions[i].offset )",
        "if ( offset < 0 )",
        "restore cannot continue without desynchronizing the stream",
        "namedEvents[i] == NULL || namedEvents[i]->mEvent == NULL",
        "timeLineEvents[i] == NULL || timeLineEvents[i]->event == NULL",
        "OpenQ4_WriteSaveGameBool( savefile, timeLineEvents[i]->pending",
        "OpenQ4_ReadSaveGameBool( savefile, timeLineEvents[i]->pending",
        "( window.simp == NULL ) == ( window.win == NULL )",
        "could not resolve saved transition target",
    ):
        require(window_source, token, "window savegame structural alignment validation")

    require_regex(
        window_source,
        r"if\s*\(\s*matchedIndex\s*==\s*-1\s*\)\s*\{\s*common->Error\(",
        "missing named events fail instead of returning mid-stream",
    )
    missing_event_block = re.search(
        r"if\s*\(\s*matchedIndex\s*==\s*-1\s*\)\s*\{(?P<body>.*?)\n\s*\}",
        window_source,
        re.MULTILINE | re.DOTALL,
    )
    if missing_event_block is None or "return;" in missing_event_block.group("body"):
        raise AssertionError("Missing named-event restore may return with unread positional payload")

    script_source = read("src/ui/GuiScript.cpp")
    for token in (
        "savefile->WriteInt( conditionReg )",
        "savefile->ReadInt( savedConditionReg )",
        "savedConditionReg < -1 || savedConditionReg >= MAX_EXPRESSION_REGISTERS",
        "savedConditionReg != conditionReg",
        "GUI script structure changed",
        "parms[i].var == NULL",
        "list[i] == NULL",
    ):
        require(script_source, token, "GUI script positional restore validation")
    if "OpenQ4_ReadSaveGameField( savefile, conditionReg" in script_source:
        raise AssertionError("GUI restore still overwrites parsed script structure from the save")


def validate_minigame_restore_contract() -> None:
    ui_restore_contracts = {
        "src/ui/Winvar.h": (
            "static ID_INLINE bool OpenQ4_WriteSaveGameBytes( idFile *savefile, const void *buffer, int len, const char *context, const char *fieldName )",
            "static ID_INLINE bool OpenQ4_ReadSaveGameBytes( idFile *savefile, void *buffer, int len, const char *context, const char *fieldName )",
            "static ID_INLINE bool OpenQ4_WriteSaveGameBool( idFile *savefile, bool value, const char *context, const char *fieldName )",
            "static ID_INLINE bool OpenQ4_ReadSaveGameBool( idFile *savefile, bool &value, const char *context, const char *fieldName )",
            "savedValue > 1",
            "static ID_INLINE bool OpenQ4_WriteSaveGameInt( idFile *savefile, int value, const char *context, const char *fieldName )",
            "static ID_INLINE bool OpenQ4_ReadSaveGameInt( idFile *savefile, int &value, const char *context, const char *fieldName )",
            "static ID_INLINE bool OpenQ4_WriteSaveGameFloat( idFile *savefile, float value, const char *context, const char *fieldName )",
            "static ID_INLINE bool OpenQ4_ReadSaveGameFloat( idFile *savefile, float &value, const char *context, const char *fieldName )",
            "common->Error( \"%s: refusing non-finite %s\"",
            "common->DPrintf( \"%s: zeroing non-finite %s at offset %d\\n\"",
            "value = 0.0f;",
            # Layout fields are typed (Q4-06); an untyped field does not compile.
            "static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, float value, const char *context, const char *fieldName )",
            "static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, idRectangle &value, const char *context, const char *fieldName )",
            "bool OpenQ4_WriteSaveGameField( idFile *savefile, const type &value, const char *context, const char *fieldName ) = delete;",
            "bool OpenQ4_ReadSaveGameField( idFile *savefile, type &value, const char *context, const char *fieldName ) = delete;",
            "static ID_INLINE bool OpenQ4_ReadSaveGameRawField( idFile *savefile, type &value, const char *context, const char *fieldName )",
            "common->Error( \"%s: truncated %s at offset %d (read %d of %d)\"",
            "OpenQ4_ReadSaveGameBool( savefile, eval, \"idWinBool::ReadFromSaveGame\", \"eval flag\" )",
            "OpenQ4_ReadSaveGameBool( savefile, data, \"idWinBool::ReadFromSaveGame\", \"value\" )",
            "OpenQ4_ReadSaveGameInt( savefile, data, \"idWinInt::ReadFromSaveGame\", \"value\" )",
            "OpenQ4_ReadSaveGameFloat( savefile, data, \"idWinFloat::ReadFromSaveGame\", \"value\" )",
            "OpenQ4_ReadSaveGameBytes( savefile, &data[0], len, \"idWinStr::ReadFromSaveGame\", \"string\" )",
            "OpenQ4_ReadSaveGameBytes( savefile, &data[0], len, \"idWinBackground::ReadFromSaveGame\", \"material name\" )",
        ),
        "src/ui/Window.cpp": (
            "OpenQ4_ReadSaveGameInt( savefile, offset, \"idWindow::ReadSaveGameTransition\", \"offset\" )",
            "Window_ReadSaveGameTransitionInterpolate( savefile, trans.interp )",
            "OpenQ4_ReadSaveGameRawField( savefile, interp, \"idWindow::ReadSaveGameTransition\", \"interpolate state\" )",
            "openQ4SaveGameLegacyInterpolate_t<idVec4> saved;",
            "SaveCodec_WriteInterpolate( out, trans.interp );",
            "if ( OpenQ4_SaveGameRestoreIsPortable( savefile ) ) {",
            "SaveCodec_ReadInterpolate( saved, interp );",
            "OpenQ4_SaveGameHasInterpolatorCacheFlag( OpenQ4_SaveGameRestoreLayoutBuild( savefile ) )",
            "OpenQ4_ReadSaveGameBytes( savefile, &string[0], len, \"idWindow::ReadSaveGameString\", \"string\" )",
            "OpenQ4_ReadSaveGameField( savefile, savedActualX, \"idWindow::ReadFromSaveGame\", \"actualX\" )",
            "&hadSavedFocusReference, &focusReadSucceeded",
            "&hadSavedCaptureReference, &captureReadSucceeded",
            "NULL, &overReadSucceeded",
            "OpenQ4_ReadSaveGameBool( savefile, timeLineEvents[i]->pending, \"idWindow::ReadFromSaveGame\", \"timeline pending flag\" )",
            "OpenQ4_ReadSaveGameInt( savefile, timeLineEvents[i]->time, \"idWindow::ReadFromSaveGame\", \"timeline event time\" )",
            "OpenQ4_ReadSaveGameInt( savefile, num, \"idWindow::ReadFromSaveGame\", \"transition count\" )",
        ),
        "src/ui/SimpleWindow.cpp": (
            "OpenQ4_ReadSaveGameField( savefile, savedFlags, \"idSimpleWindow::ReadFromSaveGame\", \"flags\" )",
            "OpenQ4_ReadSaveGameInt( savefile, stringLen, \"idSimpleWindow::ReadFromSaveGame\", \"background length\" )",
            "OpenQ4_ReadSaveGameBytes( savefile, &(backName)[0], stringLen,",
            "\"idSimpleWindow::ReadFromSaveGame\", \"background name\" )",
        ),
        "src/ui/GuiScript.cpp": (
            "const int conditionBytes = savefile->ReadInt( savedConditionReg );",
            "savedConditionReg != conditionReg",
        ),
    }
    for relative_path, tokens in ui_restore_contracts.items():
        source = read(relative_path)
        for token in tokens:
            require(source, token, f"UI savegame restore short-read guard in {relative_path}")

    ui_raw_read_regressions = {
        "src/ui/Winvar.h": (
            "savefile->Read( &eval",
            "savefile->Read( &data",
        ),
        "src/ui/Window.cpp": (
            "savefile->Read( &actualX",
            "savefile->Read( &drawRect",
            "savefile->Read( &winID",
            "savefile->Read( &timeLineEvents",
            "savefile->Read( &num, sizeof( num )",
        ),
        "src/ui/SimpleWindow.cpp": (
            "savefile->Read( &flags",
            "savefile->Read( &drawRect",
            "savefile->Read( &stringLen",
        ),
        "src/ui/GuiScript.cpp": (
            "savefile->Read( &conditionReg",
        ),
    }
    for relative_path, needles in ui_raw_read_regressions.items():
        source = read(relative_path)
        for needle in needles:
            if needle in source:
                raise AssertionError(f"UI savegame restore field bypasses checked read helper: {needle!r} in {relative_path}")

    winvar_source = read("src/ui/Winvar.h")
    for needle in (
        "savefile->Write( &eval",
        "savefile->Write(&eval",
        "savefile->Write( &data",
        "savefile->Write(&data",
        "OpenQ4_ReadSaveGameBytes( savefile, &eval",
    ):
        if needle in winvar_source:
            raise AssertionError(f"WinVar primitive bypasses checked typed savegame I/O: {needle!r}")

    minigame_contracts = {
        "src/ui/GameBustOutWindow.cpp": (
            "GAME_BUSTOUT_MAX_SAVE_ENTITIES = 512",
            "GAME_BUSTOUT_SAVE_MAGIC = 'O' | ( 'Q' << 8 ) | ( 'B' << 16 ) | ( 'O' << 24 )",
            "GAME_BUSTOUT_SAVE_VERSION = 1",
            "GameBustOut_WriteSaveInt",
            "savefile->WriteInt( value );",
            "GameBustOut_ReadLegacySaveInt",
            "GameBustOut_ReadEndianSaveInt",
            "savefile->ReadInt( value )",
            "GameBustOut_ReadSaveInt",
            "GameBustOut_ReadSaveVersion",
            "savefile->Seek( markerOffset, FS_SEEK_SET );",
            "unsupported format version",
            "GameBustOut_WriteSaveInt( savefile, GAME_BUSTOUT_SAVE_MAGIC );",
            "GameBustOut_WriteSaveInt( savefile, GAME_BUSTOUT_SAVE_VERSION );",
            "if ( bytesRead != sizeof( value ) )",
            "GameBustOut_ValidateSaveCount",
            "count < 0 || count > GAME_BUSTOUT_MAX_SAVE_ENTITIES",
            "GameBustOut_ValidateEntityIndex",
            "index < 0 || index >= entityCount",
            "GameBustOut_WriteSaveField",
            "OpenQ4_ReadSaveGameField( savefile, value,",
            "GameBustOut_ReadSaveField",
            "GameBustOut_ReadSavePowerup",
            "value < POWERUP_NONE || value > POWERUP_MULTIBALL",
            "powerup = GameBustOut_ReadSavePowerup( savefile, \"entity powerup\", saveVersion );",
            "GameBustOut_ReadSaveField( savefile, ballSpeed, \"ball speed\" );",
            "ball entity index",
            "powerup entity index",
            "brick entity index",
        ),
        "src/ui/GameBearShootWindow.cpp": (
            "GAME_BEARSHOOT_MAX_SAVE_ENTITIES = 64",
            "GAME_BEARSHOOT_SAVE_MAGIC = 'O' | ( 'Q' << 8 ) | ( 'B' << 16 ) | ( 'S' << 24 )",
            "GAME_BEARSHOOT_SAVE_VERSION = 1",
            "GameBearShoot_WriteSaveInt",
            "savefile->WriteInt( value );",
            "GameBearShoot_ReadLegacySaveInt",
            "GameBearShoot_ReadEndianSaveInt",
            "savefile->ReadInt( value )",
            "GameBearShoot_ReadSaveInt",
            "GameBearShoot_ReadSaveVersion",
            "savefile->Seek( markerOffset, FS_SEEK_SET );",
            "unsupported format version",
            "GameBearShoot_WriteSaveInt( savefile, GAME_BEARSHOOT_SAVE_MAGIC );",
            "GameBearShoot_WriteSaveInt( savefile, GAME_BEARSHOOT_SAVE_VERSION );",
            "if ( bytesRead != sizeof( value ) )",
            "GameBearShoot_ValidateSaveCount",
            "count < 0 || count > GAME_BEARSHOOT_MAX_SAVE_ENTITIES",
            "GameBearShoot_ValidateEntityIndex",
            "index < 0 || index >= entityCount",
            "GameBearShoot_WriteSaveField",
            "OpenQ4_ReadSaveGameField( savefile, value,",
            "GameBearShoot_ReadSaveField",
            "GameBearShoot_ReadSaveField( savefile, timeRemaining, \"time remaining\" );",
            "GameBearShoot_ReadSaveField( savefile, windForce, \"wind force\" );",
            "turret entity index",
            "bear entity index",
            "gunblast entity index",
        ),
        "src/ui/GameSSDWindow.cpp": (
            "GAME_SSD_SAVE_MAGIC = 'O' | ( 'Q' << 8 ) | ( 'S' << 16 ) | ( 'D' << 24 )",
            "GAME_SSD_SAVE_VERSION = 1",
            "GAME_SSD_MAX_SAVE_LEVELS = 64",
            "GAME_SSD_MAX_SAVE_WEAPONS = 16",
            "GAME_SSD_MAX_SAVE_ENTITY_REFS = MAX_ASTEROIDS + MAX_ASTRONAUT + MAX_EXPLOSIONS + MAX_POINTS + MAX_PROJECTILES + MAX_POWERUPS",
            "GameSSD_WriteSaveInt",
            "savefile->WriteInt( value );",
            "GameSSD_ReadLegacySaveInt",
            "GameSSD_ReadLegacySaveBlock",
            "truncated legacy %s",
            "GameSSD_ReadEndianSaveInt",
            "savefile->ReadInt( value )",
            "GameSSD_ReadSaveInt",
            "GameSSD_ReadSaveCount",
            "GameSSD_ReadSavePoolId",
            "GameSSD_ValidateSaveCount",
            "GameSSD_ValidatePoolId",
            "GameSSD_ValidateEntityType",
            "GameSSD_ValidateExpectedEntityType",
            "GameSSD_ValidateEntityReference",
            "GameSSD_ReadSaveVersion",
            "savefile->Seek( markerOffset, FS_SEEK_SET );",
            "unsupported format version",
            "GameSSD_WriteSaveInt( savefile, GAME_SSD_SAVE_MAGIC );",
            "GameSSD_WriteSaveInt( savefile, GAME_SSD_SAVE_VERSION );",
            "GameSSD_WriteLevelData",
            "GameSSD_ReadLevelData",
            "GameSSD_WriteAsteroidData",
            "GameSSD_ReadAsteroidData",
            "GameSSD_WriteAstronautData",
            "GameSSD_ReadAstronautData",
            "GameSSD_WritePowerupData",
            "GameSSD_ReadPowerupData",
            "GameSSD_WriteWeaponData",
            "GameSSD_ReadWeaponData",
            "GameSSD_WriteGameStats",
            "GameSSD_ReadGameStats",
            "GameSSD_WriteSaveField",
            "OpenQ4_ReadSaveGameField( savefile, value,",
            "GameSSD_ReadSaveField",
            "GameSSD_WriteSaveInt( savefile, entCount );",
            "explosionPool[i].id = i;",
            "pointsPool[i].id = i;",
            "projectilePool[i].id = i;",
            "powerupPool[i].id = i;",
            "GameSSD_ReadSaveCount( savefile, \"asteroid count\", MAX_ASTEROIDS );",
            "GameSSD_ReadSavePoolId( savefile, \"asteroid id\", MAX_ASTEROIDS );",
            "GameSSD_ReadSaveCount( savefile, \"astronaut count\", MAX_ASTRONAUT );",
            "GameSSD_ReadSavePoolId( savefile, \"astronaut id\", MAX_ASTRONAUT );",
            "GameSSD_ReadSaveCount( savefile, \"explosion count\", MAX_EXPLOSIONS );",
            "GameSSD_ReadSavePoolId( savefile, \"explosion id\", MAX_EXPLOSIONS );",
            "GameSSD_ReadSaveCount( savefile, \"points count\", MAX_POINTS );",
            "GameSSD_ReadSavePoolId( savefile, \"points id\", MAX_POINTS );",
            "GameSSD_ReadSaveCount( savefile, \"projectile count\", MAX_PROJECTILES );",
            "GameSSD_ReadSavePoolId( savefile, \"projectile id\", MAX_PROJECTILES );",
            "GameSSD_ReadSaveCount( savefile, \"powerup count\", MAX_POWERUPS );",
            "GameSSD_ReadSavePoolId( savefile, \"powerup id\", MAX_POWERUPS );",
            "GameSSD_ReadSaveCount( savefile, \"level count\", GAME_SSD_MAX_SAVE_LEVELS );",
            "GameSSD_ReadSaveCount( savefile, \"weapon count\", GAME_SSD_MAX_SAVE_WEAPONS );",
            "GameSSD_ReadSaveCount( savefile, \"entity reference count\", GAME_SSD_MAX_SAVE_ENTITY_REFS );",
            "GameSSD_ValidateSaveIndex( \"crosshair index\", currentCrosshair, SSDCrossHair::CROSSHAIR_COUNT );",
            "GameSSD_ValidateExpectedEntityType( \"asteroid entity\", type, SSD_ENTITY_ASTEROID );",
            "GameSSD_ValidateExpectedEntityType( \"astronaut entity\", type, SSD_ENTITY_ASTRONAUT );",
            "GameSSD_ValidateExpectedEntityType( \"explosion entity\", type, SSD_ENTITY_EXPLOSION );",
            "GameSSD_ValidateExpectedEntityType( \"points entity\", type, SSD_ENTITY_POINTS );",
            "GameSSD_ValidateExpectedEntityType( \"projectile entity\", type, SSD_ENTITY_PROJECTILE );",
            "GameSSD_ValidateExpectedEntityType( \"powerup entity\", type, SSD_ENTITY_POWERUP );",
            "GameSSD_ValidateSaveIndex( \"explosion type\", explosionType, EXPLOSION_MATERIAL_COUNT );",
            "GameSSD_ValidateSaveIndex( \"powerup state\", powerupState, POWERUP_STATE_OPEN + 1 );",
            "GameSSD_ValidateSaveIndex( \"powerup type\", powerupType, POWERUP_TYPE_MAX );",
            "GameSSD_ValidateSaveIndex( \"current level\", gameStats.currentLevel, levelCount );",
            "GameSSD_ValidateSaveIndexOrEnd( \"next level\", gameStats.nextLevel, levelCount );",
            "GameSSD_ValidateSaveIndex( \"current weapon\", gameStats.currentWeapon, weaponCount );",
            "GameSSD_ValidateEntityReference( \"explosion buddy\", type, id );",
            "type = GameSSD_ReadSaveInt( savefile, \"entity reference type\" );",
            "id = GameSSD_ReadSaveInt( savefile, \"entity reference id\" );",
            "GameSSD_ValidateEntityReference( \"entity reference\", type, id );",
            "if( ent == NULL || !ent->inUse )",
            "GameSSD_ReadSaveField( savefile, position, \"entity position\" );",
            "GameSSD_ReadSaveField( savefile, finalSize, \"explosion final size\" );",
            "GameSSD_ReadSaveField( savefile, dir, \"projectile direction\" );",
            "GameSSD_ReadSaveField( savefile, screenBounds, \"screen bounds\" );",
        ),
    }
    for relative_path, tokens in minigame_contracts.items():
        source = read(relative_path)
        for token in tokens:
            require(source, token, f"minigame save restore corruption guard in {relative_path}")

    raw_integer_regressions = {
        "src/ui/GameBustOutWindow.cpp": (
            "savefile->Write( &numLevels",
            "savefile->Write( &numBricks",
            "savefile->Write( &currentLevel",
            "savefile->Write( &gameScore",
            "savefile->Write( &nextBallScore",
            "savefile->Write( &bigPaddleTime",
            "savefile->Write( &ballsRemaining",
            "savefile->Write( &ballsInPlay",
            "savefile->Write( &numberOfEnts",
            "savefile->Write( &ballIndex",
            "savefile->Write( &powerIndex",
            "savefile->Write( &index",
            "savefile->Write( &powerup",
            "savefile->Read( &numLevels",
            "savefile->Read( &numBricks",
            "savefile->Read( &currentLevel",
            "savefile->Read( &gameScore",
            "savefile->Read( &nextBallScore",
            "savefile->Read( &bigPaddleTime",
            "savefile->Read( &ballsRemaining",
            "savefile->Read( &ballsInPlay",
            "savefile->Read( &powerup",
        ),
        "src/ui/GameBearShootWindow.cpp": (
            "savefile->Write( &currentLevel",
            "savefile->Write( &goalsHit",
            "savefile->Write( &bearShrinkStartTime",
            "savefile->Write( &windUpdateTime",
            "savefile->Write( &numberOfEnts",
            "savefile->Write( &index",
            "savefile->Read( &currentLevel",
            "savefile->Read( &goalsHit",
            "savefile->Read( &bearShrinkStartTime",
            "savefile->Read( &windUpdateTime",
        ),
        "src/ui/GameSSDWindow.cpp": (
            "savefile->Write(&currentCrosshair",
            "savefile->Read(&currentCrosshair",
            "savefile->Write(&type",
            "savefile->Read(&type",
            "savefile->Write(&currentTime",
            "savefile->Read(&currentTime",
            "savefile->Write(&lastUpdate",
            "savefile->Read(&lastUpdate",
            "savefile->Write(&elapsed",
            "savefile->Read(&elapsed",
            "savefile->Write(&health",
            "savefile->Read(&health",
            "savefile->Write(&count",
            "savefile->Read(&count",
            "savefile->Write(&id",
            "savefile->Read(&id",
            "savefile->Write(&length",
            "savefile->Read(&length",
            "savefile->Write(&beginTime",
            "savefile->Read(&beginTime",
            "savefile->Write(&endTime",
            "savefile->Read(&endTime",
            "savefile->Write(&explosionType",
            "savefile->Read(&explosionType",
            "savefile->Write(&distance",
            "savefile->Read(&distance",
            "savefile->Write(&powerupState",
            "savefile->Read(&powerupState",
            "savefile->Write(&powerupType",
            "savefile->Read(&powerupType",
            "savefile->Write(&ssdTime",
            "savefile->Read(&ssdTime",
            "savefile->Write(&levelCount",
            "savefile->Read(&levelCount",
            "savefile->Write(&weaponCount",
            "savefile->Read(&weaponCount",
            "savefile->Write(&superBlasterTimeout",
            "savefile->Read(&superBlasterTimeout",
            "savefile->Write(&gameStats",
            "savefile->Read(&gameStats",
            "savefile->Write(&entCount",
            "savefile->Read(&entCount",
            "savefile->Write(&(levelData[i])",
            "savefile->Read(&newLevel",
            "savefile->Write(&(asteroidData[i])",
            "savefile->Read(&newAsteroid",
            "savefile->Write(&(astronautData[i])",
            "savefile->Read(&newAstronaut",
            "savefile->Write(&(powerupData[i])",
            "savefile->Read(&newPowerup",
            "savefile->Write(&(weaponData[i])",
            "savefile->Read(&newWeapon",
        ),
    }
    for relative_path, needles in raw_integer_regressions.items():
        source = read(relative_path)
        for needle in needles:
            if needle in source:
                raise AssertionError(f"Minigame integer field still uses raw host-order IO: {needle!r} in {relative_path}")

    raw_block_regressions = {
        "src/ui/GameBustOutWindow.cpp": (
            "savefile->Read( &visible",
            "savefile->Read( &width",
            "savefile->Read( &color",
            "savefile->Read( &paddleVelocity",
            "savefile->Read( &ballSpeed",
        ),
        "src/ui/GameBearShootWindow.cpp": (
            "savefile->Read( &width",
            "savefile->Read( &entColor",
            "savefile->Read( &timeRemaining",
            "savefile->Read( &bearScale",
            "savefile->Read( &windForce",
        ),
        "src/ui/GameSSDWindow.cpp": (
            "savefile->Read(&position",
            "savefile->Read(&matColor",
            "savefile->Read(&finalSize",
            "savefile->Read(&beginPosition",
            "savefile->Read(&dir",
            "savefile->Read(&screenBounds",
        ),
    }
    for relative_path, needles in raw_block_regressions.items():
        source = read(relative_path)
        for needle in needles:
            if needle in source:
                raise AssertionError(f"Minigame save field bypasses checked block reader: {needle!r} in {relative_path}")

    ssd_source = read("src/ui/GameSSDWindow.cpp")
    unchecked_ssd_restore_fields = (
        'count = GameSSD_ReadSaveInt( savefile, "asteroid count" );',
        'id = GameSSD_ReadSaveInt( savefile, "asteroid id" );',
        'count = GameSSD_ReadSaveInt( savefile, "astronaut count" );',
        'id = GameSSD_ReadSaveInt( savefile, "astronaut id" );',
        'count = GameSSD_ReadSaveInt( savefile, "explosion count" );',
        'id = GameSSD_ReadSaveInt( savefile, "explosion id" );',
        'count = GameSSD_ReadSaveInt( savefile, "points count" );',
        'id = GameSSD_ReadSaveInt( savefile, "points id" );',
        'count = GameSSD_ReadSaveInt( savefile, "projectile count" );',
        'id = GameSSD_ReadSaveInt( savefile, "projectile id" );',
        'count = GameSSD_ReadSaveInt( savefile, "powerup count" );',
        'id = GameSSD_ReadSaveInt( savefile, "powerup id" );',
        'levelCount = GameSSD_ReadSaveInt( savefile, "level count" );',
        'weaponCount = GameSSD_ReadSaveInt( savefile, "weapon count" );',
        'entCount = GameSSD_ReadSaveInt( savefile, "entity reference count" );',
    )
    for needle in unchecked_ssd_restore_fields:
        if needle in ssd_source:
            raise AssertionError(f"SSD restore field bypasses bounds helper: {needle!r}")
    if ssd_source.count("ent->id = id;") < 6:
        raise AssertionError("SSD pool restores must stamp each restored pool object's saved id")


def validate_restore_teardown_lists() -> None:
    """Restored owner lists must stay safe for failed-load teardown.

    Destructors delete every listed pointer and raise SIG_REMOVED, so a read that
    fails part way must leave only complete entries, never a list sized over
    unset pointers or unresolved signal functions. Restored physics objects are
    attached to the entity restoring them instead of a saved owner pointer.
    """
    for module in ("game", "mpgame"):
        entity = read_game_libs(f"src/{module}/Entity.cpp")
        signals = entity[entity.index("bool readsignals;"):entity.index("savefile->ReadInt( mpGUIState );")]
        require(signals, "num > MAX_SIGNAL_THREADS", f"{module} signal count bound")
        require(signals, "signals->signal[ i ].Append( sig );", f"{module} complete signal entries")
        if "SetNum( num )" in signals:
            raise AssertionError(f"{module} signal restore sizes the list before reading its entries")
        restore_physics = entity[entity.index("void idEntity::RestorePhysics( idPhysics *phys ) {"):]
        restore_physics = restore_physics[:restore_physics.index("\n}")]
        require(restore_physics, "phys->SetSelf( this );", f"{module} RestorePhysics owner")

        for relative, needles, forbidden in (
            ("vehicle/VehiclePosition.cpp", ("mParts.Append ( part );", "mWeapons.Append ( part );"),
             ("mParts.SetNum", "mWeapons.SetNum")),
            ("anim/Anim_Blend.cpp", ("jointMods.Append( new jointMod_t );",), ("jointMods.SetNum",)),
            ("BrittleFracture.cpp", ("shards[i]->decals.Append( new idFixedWinding );",), ("decals.SetNum",)),
        ):
            source = read_game_libs(f"src/{module}/{relative}")
            for needle in needles:
                require(source, needle, f"{module} {relative} append-only restore")
            for token in forbidden:
                if token in source:
                    raise AssertionError(f"{module} {relative} sizes an owning list with {token}")

        client_entity = read_game_libs(f"src/{module}/client/ClientEntity.cpp")
        constructor = client_entity[client_entity.index("rvClientEntity::rvClientEntity( void ) {"):]
        constructor = constructor[:constructor.index("\n}")]
        for node in ("spawnNode", "bindNode", "presentationNode"):
            require(constructor, f"{node}.SetOwner( this );", f"{module} restored client entity {node} owner")

        # lastRadioChatter is static: it must not carry a spawn id into the next
        # level, and saves that did so must restore without a typed-reference error.
        misc = read_game_libs(f"src/{module}/Misc.cpp")
        destructor = misc[misc.index("idFuncRadioChatter::~idFuncRadioChatter() {"):]
        require(destructor[:destructor.index("\n}")], "lastRadioChatter = NULL;", f"{module} radio chatter reset")
        restore = misc[misc.index("void idFuncRadioChatter::Restore( idRestoreGame *savefile ) {"):]
        restore = restore[:restore.index("\n}")]
        require(restore, "idEntityPtr<idEntity> saved;", f"{module} radio chatter tolerant restore")
        if "lastRadioChatter.Restore" in restore:
            raise AssertionError(f"{module} radio chatter restores its static through a typed reference")

        validate_failed_restore_teardown(module)


def function_body(source: str, signature: str) -> str:
    body = source[source.index(signature):]
    return body[:body.index("\n}")]


def validate_failed_restore_teardown(module: str) -> None:
    """A refused load deletes a partly restored map without a second error.

    Teardown destructors post events, raise SIG_REMOVED and eject vehicle
    drivers, so none of them may run script on half-read state or fail on a
    full event queue. Saved owners, indexes and bindings that later code
    dereferences without checking are validated or replaced on restore.
    """
    event = read_game_libs(f"src/{module}/gamesys/Event.cpp")
    alloc = function_body(event, "idEvent *idEvent::Alloc(")
    full_queue = alloc[alloc.index("if ( FreeEvents.IsListEmpty() ) {"):alloc.index("WriteDebugInfo( );")]
    require(full_queue, "gameLocal.GameState() == GAMESTATE_SHUTDOWN", f"{module} shutdown drops events on a full queue")
    require(full_queue, "return NULL;", f"{module} shutdown event drop")
    post = function_body(read_game_libs(f"src/{module}/gamesys/Class.cpp"), "bool idClass::PostEventArgs(")
    require(post, "if ( event == NULL ) {", f"{module} PostEventArgs handles a dropped event")

    entity = read_game_libs(f"src/{module}/Entity.cpp")
    prepare = function_body(entity, "void idEntity::PrepareFailedRestoreShutdown( void ) {")
    require(prepare, "signals->signal[ i ].Clear();", f"{module} failed restore drops signal threads")
    restore = function_body(entity, "void idEntity::Restore( idRestoreGame *savefile ) {")
    require(restore, "entityNumber == ENTITYNUM_NONE && restoredEntityNumber != ENTITYNUM_NONE",
            f"{module} unregistered entity number check")

    vehicle = read_game_libs(f"src/{module}/vehicle/Vehicle.cpp")
    require(function_body(vehicle, "rvVehicle::~rvVehicle ( void ) {"), "!failedRestoreShutdown",
            f"{module} failed restore skips driver ejection")
    require(function_body(vehicle, "void rvVehicle::PrepareFailedRestoreShutdown( void ) {"),
            "failedRestoreShutdown = true;", f"{module} vehicle failed-restore flag")
    actor = read_game_libs(f"src/{module}/Actor.cpp")
    occupant = function_body(actor, "void idActor::ValidateRestoredBindings( idRestoreGame *savefile ) const {")
    require(occupant, "position >= vehicle->GetNumPositions()", f"{module} vehicle position bound")

    af = function_body(read_game_libs(f"src/{module}/AF.cpp"), "void idAF::Restore( idRestoreGame *savefile, idEntity *owner ) {")
    require(af, "self = ( savedSelf != NULL ) ? owner : NULL;", f"{module} articulated figure owner")
    require(af, "physicsObj.SetSelf( owner );", f"{module} articulated figure physics owner")
    require(read_game_libs(f"src/{module}/AFEntity.cpp"), "af.Restore( savefile, this );", f"{module} AF owner argument")
    animator = function_body(read_game_libs(f"src/{module}/anim/Anim_Blend.cpp"), "void idAnimator::Restore( idRestoreGame *savefile ) {")
    require(animator, "savefile->ReadObject( savedEntity );", f"{module} animator keeps its owner")
    if "savefile->ReadObject( entity );" in animator:
        raise AssertionError(f"{module} animator restores its owner from the save")
    constraint = function_body(read_game_libs(f"src/{module}/physics/Physics_AF.cpp"),
                               "void idAFConstraint::Restore( idRestoreGame *saveFile ) {")
    require(constraint, "if ( boxIndex[i] != -1 ) {", f"{module} AF constraint box index check")

    client = read_game_libs(f"src/{module}/client/ClientEntity.cpp")
    binding = function_body(client, "void rvClientEntity::ValidateRestoredBinding( idRestoreGame *savefile ) const {")
    require(binding, "bindNode.ListHead() != &master->clientEntities", f"{module} client entity master list")
    require(binding, "idAnimatedEntity::GetClassType()", f"{module} client entity joint master")
    require(function_body(client, "void rvClientEntity::Restore( idRestoreGame *savefile ) {"),
            "entityNumber == -1 && restoredEntityNumber != -1", f"{module} unregistered client entity number check")
    require(read_game_libs(f"src/{module}/gamesys/SaveGame.cpp"), "->ValidateRestoredBinding( this );",
            f"{module} client entity binding validation pass")

    target = read_game_libs(f"src/{module}/Target.cpp")
    influence = function_body(target, "void idTarget_SetInfluence::Restore( idRestoreGame *savefile ) {")
    if "Append( itemNum )" in influence:
        raise AssertionError(f"{module} influence target restores unchecked entity numbers")
    require(function_body(target, "static int Target_ReadInfluenceEntityNumber( idRestoreGame *savefile ) {"),
            "itemNum >= MAX_GENTITIES", f"{module} influence entity number bound")

    if module == "game":
        fire = function_body(read_game_libs("src/game/awakening/DOTEntity.cpp"),
                             "void FireDOTEntity::Restore( idRestoreGame *savefile ) {")
        require(fire, "num < 0 || num > 4096", "game fire burning joint bound")


def validate_validation_wiring() -> None:
    for relative_path in (
        "tools/validation/openq4_validate.py",
        ".github/workflows/push-verification.yml",
        ".github/workflows/commit-validation.yml",
    ):
        require(read(relative_path), "savegame_corruption_contract.py", f"{relative_path} wiring")


def main() -> None:
    validate_header_fuzz_model()
    validate_ssd_restore_fuzz_model()
    validate_session_source_contract()
    validate_gamelibs_save_payload_contract()
    validate_savegame_compat_stamp_model()
    validate_gui_register_restore_contract()
    validate_gui_winvar_initialization_contract()
    validate_gui_stream_alignment_contract()
    validate_minigame_restore_contract()
    validate_restore_teardown_lists()
    validate_validation_wiring()
    print("savegame_corruption_contract: ok")


if __name__ == "__main__":
    main()
