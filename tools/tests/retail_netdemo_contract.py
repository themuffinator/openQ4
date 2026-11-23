#!/usr/bin/env python3
"""Contract for the retail Quake 4 1.4.2 network demo reader (MP-22).

Three parts, none of which starts the engine:

* an independent reference reader of the 1.4.2 `.ndmo85` container and its
  message channel (header, pak table, recording client, user info, the game's
  network info, packets, the recorder's command tail; channel fragments, the
  zero-run coding and the reliable queue). When the two demos Quake 4 1.4.2
  installs in `q4mp/demos` are present, it reads both and pins what they hold,
  the same counts the engine prints when it reads them
  (`mp_retail_netdemo_smoke.py` compares the two);
* damaged copies of those demos, each damaging one outer length or field, which
  the reference reader must refuse; the smoke plays the same copies in the
  engine, which must refuse each without a crash or fatal error;
* source checks: the engine's bounds and refusals, the MVD player's retail
  path, the game module's 1.4.2 read gates, the decl renumbering, the library
  and localization.

The demos are Quake 4's own content and are never copied into the repository;
without a Quake 4 1.4.2 installation the fixture part is skipped.
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BASEPATH = Path(os.environ.get("OPENQ4_QUAKE4_BASEPATH", r"E:\SteamLibrary\steamapps\common\Quake 4"))

IDENT = 0x4F4D444E
VERSION = 12
PROTOCOL = (2, 85)
MAX_CLIENTS = 32
USERCMD_BACKUP = 256
MAX_ENTITIES = 1 << 12
PVS_WORDS = (MAX_ENTITIES + 31) >> 5
MAX_STATE_BYTES = 512
MAX_SNAPSHOTS = 64
MAX_GAME_MESSAGE = 8192
MAX_MESSAGE = 16384
MAX_QUEUE = 16384
FRAGMENT_SIZE = 1300
MAX_PURE_PAKS = 128
GAME_PAK_SLOTS = 60
MAX_DICT_KEYS = 4096
MAX_STRING = 1023

# What the two shipped demos hold, as the engine reports them
# ("Read Quake 4 1.4.2 <kind> demo '<name>': P packets, S snapshots, R reliable messages").
FIXTURES = {
    "id_perftest.ndmo85": {"server": False, "client": 2, "map": "mp/q4dm7", "packets": 2177,
                           "snapshots": 1892, "reliables": 2322, "duration_ms": 151287},
    "id_serverdemo.ndmo85": {"server": True, "client": 32, "map": "mp/q4dm7", "packets": 922,
                             "snapshots": 922, "reliables": 3050, "duration_ms": 58944},
}


class Refused(Exception):
    pass


class Reader:
    def __init__(self, data: bytes, offset: int = 0):
        self.data, self.pos = data, offset

    def need(self, count: int) -> None:
        if count < 0 or self.pos + count > len(self.data):
            raise Refused(f"truncated at {self.pos} reading {count} bytes")

    def int(self) -> int:
        self.need(4)
        value = struct.unpack_from("<i", self.data, self.pos)[0]
        self.pos += 4
        return value

    def short(self) -> int:
        self.need(2)
        value = struct.unpack_from("<h", self.data, self.pos)[0]
        self.pos += 2
        return value

    def byte(self) -> int:
        self.need(1)
        self.pos += 1
        return self.data[self.pos - 1]

    def skip(self, count: int) -> None:
        self.need(count)
        self.pos += count

    def string(self, limit: int) -> str:
        count = self.int()
        if count < 0 or count > limit:
            raise Refused(f"string length {count}")
        self.need(count)
        text = self.data[self.pos:self.pos + count]
        if b"\0" in text:
            raise Refused("string holds a terminator")
        self.pos += count
        return text.decode("latin-1")

    def dict(self) -> dict[str, str]:
        count = self.int()
        if count < 0 or count > MAX_DICT_KEYS:
            raise Refused(f"dictionary count {count}")
        return {self.string(MAX_STRING): self.string(MAX_STRING) for _ in range(count)}

    def usercmd(self) -> tuple[int, ...] | None:
        frame = self.int()
        if frame == -1:
            return None
        rest = struct.unpack("<iiih3bhhh2B", self.data[self.pos:self.pos + 25]) if self.pos + 25 <= len(self.data) else None
        self.skip(25)
        return (frame,) + rest


@dataclass
class Demo:
    version: int = 0
    protocol: tuple[int, int] = (0, 0)
    usercmd_start: int = 0
    server: bool = False
    client: int = -1
    paks: int = 0
    server_id: int = 0
    game_init: int = 0
    client_time: int = 0
    incoming_sequence: int = 0
    fragment_sequence: int = 0
    fragment: bytes = b""
    reliable_last: int = 0
    snapshot_sequence: int = 0
    server_info: dict[str, str] = field(default_factory=dict)
    users: list[dict[str, str]] = field(default_factory=list)
    players: list[int] = field(default_factory=list)
    offsets: dict[str, int] = field(default_factory=dict)
    packets: list[tuple[int, int, int]] = field(default_factory=list)
    usercmds: int = 0
    snapshots: int = 0
    reliables: int = 0


def parse(data: bytes) -> Demo:
    """The container, as RetailNetDemo.cpp's ParseContainer reads it."""
    r, demo = Reader(data), Demo()
    if len(data) < 16 or r.int() != IDENT:
        raise Refused("not a Quake 4 network demo")
    demo.version = r.int()
    protocol = r.int()
    demo.protocol = (protocol >> 16, protocol & 0xFFFF)
    demo.usercmd_start = r.int()
    if demo.version != VERSION or demo.protocol != PROTOCOL:
        raise Refused("another Quake 4 release")
    if demo.usercmd_start <= 16 or demo.usercmd_start > len(data) - 4:
        raise Refused("command stream offset outside the file")
    demo.offsets["fs"] = r.pos
    r.string(260), r.string(260)
    demo.paks = r.int()
    if demo.paks < 0 or demo.paks > MAX_PURE_PAKS:
        raise Refused(f"pak count {demo.paks}")
    if demo.paks:
        for _ in range(demo.paks):
            r.string(260), r.int()
        os_id = r.int()
        for _ in range(GAME_PAK_SLOTS):
            r.string(260), r.int()
        if os_id < 0 or os_id >= 6:
            raise Refused("recording OS")
    kind = r.byte()
    if kind > 1:
        raise Refused("recording kind")
    demo.server = kind == 1
    demo.offsets["client"] = r.pos
    fields = [r.int() for _ in range(7)]
    r.skip(4), r.short()
    rest = [r.int() for _ in range(10)]
    demo.client_time, demo.client = fields[1], fields[3]
    demo.server_id, demo.snapshot_sequence, demo.game_init = rest[0], rest[3], rest[6]
    game_frame, game_time, snapshot_frame, snapshot_time = rest[7], rest[8], rest[4], rest[5]
    if (demo.client != MAX_CLIENTS) if demo.server else not 0 <= demo.client < MAX_CLIENTS:
        raise Refused("recording client")
    if min(game_frame, game_time, snapshot_frame, snapshot_time) < 0 or snapshot_frame > game_frame or not 0 <= demo.server_id <= 0xFFFF:
        raise Refused("client clock")
    demo.offsets["usercmd_table"] = r.pos
    if not demo.server:
        for _ in range(MAX_CLIENTS * USERCMD_BACKUP):
            r.usercmd()
    demo.offsets["channel"] = r.pos
    demo.incoming_sequence, demo.fragment_sequence = r.int(), r.int()
    length = r.int()
    if length < 0 or length > MAX_MESSAGE:
        raise Refused("channel fragment")
    r.need(length)
    demo.fragment = data[r.pos:r.pos + length]
    r.skip(length)
    r.int()
    demo.reliable_last = r.int()
    start, end = r.int(), r.int()
    if start < 0 or end < start or end > MAX_QUEUE or end != start:
        raise Refused("reliable queue")
    demo.offsets["users"] = r.pos
    demo.users = [r.dict() for _ in range(MAX_CLIENTS)]
    demo.server_info = r.dict()
    r.dict()
    demo.offsets["network"] = r.pos
    if not demo.server:
        for number in range(MAX_ENTITIES):
            valid = r.byte()
            if valid > 1:
                raise Refused("entity flag")
            if valid:
                if "first_state" not in demo.offsets:
                    demo.offsets["first_state"] = r.pos
                if r.int() != number:
                    raise Refused("entity number")
                size = r.int()
                if size < 1 or size > MAX_STATE_BYTES:
                    raise Refused(f"entity state of {size} bytes")
                r.skip(size)
        r.skip(MAX_CLIENTS * PVS_WORDS * 4)
    count = r.int()
    if count < 0 or count > MAX_CLIENTS:
        raise Refused("player count")
    previous = -1
    for _ in range(count):
        slot = r.int()
        r.int()
        if slot <= previous or slot >= MAX_CLIENTS:
            raise Refused("player list")
        demo.players.append(slot)
        previous = slot
    if not demo.server and demo.client not in demo.players:
        raise Refused("recording player missing")
    if not demo.server:
        demo.offsets["snapshots"] = r.pos
        snapshots = r.int()
        if snapshots < 0 or snapshots > MAX_SNAPSHOTS:
            raise Refused("pending snapshots")
        for _ in range(snapshots):
            r.int()
            states = r.int()
            if states < 0 or states > MAX_ENTITIES:
                raise Refused("pending snapshot states")
            for _ in range(states):
                number, size = r.int(), r.int()
                if not 0 <= number < MAX_ENTITIES or size < 1 or size > MAX_STATE_BYTES:
                    raise Refused("pending snapshot state")
                r.skip(size)
            r.skip(PVS_WORDS * 4)
    for _ in range(2):
        size = r.int()
        if size < 1 or size > MAX_GAME_MESSAGE:
            raise Refused("game state message")
        r.skip(size)
    demo.offsets["packets"] = r.pos
    previous_time = demo.client_time
    while True:
        if r.pos > demo.usercmd_start - 4:
            raise Refused("packets run into the command stream")
        time = r.int()
        if time == 0:
            if r.pos == demo.usercmd_start:
                break
            time = previous_time
        size = r.int()
        if size < 0 or size > MAX_MESSAGE or r.pos > demo.usercmd_start - size:
            raise Refused(f"packet size {size}")
        if time < previous_time:
            raise Refused("packet back in time")
        demo.packets.append((time, r.pos, size))
        r.skip(size)
        previous_time = time
    if r.pos != demo.usercmd_start or not demo.packets:
        raise Refused("packet stream end")
    previous = None
    while True:
        cmd = r.usercmd()
        if cmd is None:
            break
        if demo.server or (previous is not None and cmd[0] <= previous):
            raise Refused("command stream")
        previous = cmd[0]
        demo.usercmds += 1
    if r.pos != len(data):
        raise Refused("bytes after the command stream")
    return demo


def zero_run(data: bytes, size: int) -> bytes:
    """idCompressor_RunLength_ZeroBased with 3-bit words, as DecodeZeroRun."""
    in_bits, read, out, write = len(data) * 8, 0, bytearray(size), 0

    def word() -> int:
        nonlocal read
        if read + 3 > in_bits:
            raise Refused("zero-run input ends early")
        value = 0
        for i in range(3):
            value |= ((data[read >> 3] >> (read & 7)) & 1) << i
            read += 1
        return value

    def put(value: int) -> None:
        nonlocal write
        for i in range(3):
            if write < size * 8:
                out[write >> 3] |= ((value >> i) & 1) << (write & 7)
            write += 1

    while write < size * 8:
        value = word()
        if value == 0:
            for _ in range(word() + 1):
                put(0)
        else:
            put(value)
    return bytes(out)


def decode(data: bytes, demo: Demo) -> None:
    """The message channel and the engine message ids, as RetailNetDemo.cpp's Decode."""
    incoming, fragment_sequence, fragments = demo.incoming_sequence, demo.fragment_sequence, bytearray(demo.fragment)
    reliable_last = demo.reliable_last
    last_snapshot = 0 if demo.server else demo.snapshot_sequence
    for index, (_, offset, size) in enumerate(demo.packets):
        packet = data[offset:offset + size]
        if size < 6:
            continue
        packet_id = struct.unpack_from("<h", packet, 0)[0]
        if packet_id == -1 or (packet_id & 0xFFFF) != demo.server_id:
            continue
        bits = struct.unpack_from("<I", packet, 2)[0]
        sequence, cursor = bits & 0x7FFFFFFF, 6
        if bits & 0x80000000:
            start, length = struct.unpack_from("<hh", packet, 6)
            cursor = 10
        if sequence <= incoming:
            continue
        if bits & 0x80000000:
            if sequence != fragment_sequence:
                fragment_sequence, fragments = sequence, bytearray()
            if start != len(fragments) or not 0 <= length <= FRAGMENT_SIZE or length > size - cursor:
                continue
            fragments += packet[cursor:cursor + length]
            if length == FRAGMENT_SIZE:
                continue
            payload = bytes(fragments)
        else:
            payload = packet[cursor:]
        incoming = sequence
        if len(payload) < 2:
            continue
        message_size = struct.unpack_from("<h", payload, 0)[0]
        if message_size < 6 or message_size > MAX_MESSAGE or message_size > 4 * (len(payload) - 2) + 8:
            raise Refused(f"packet {index} message size")
        message, at = zero_run(payload[2:], message_size), 4
        while True:
            if at + 2 > len(message):
                raise Refused(f"packet {index} reliables")
            reliable_size = struct.unpack_from("<h", message, at)[0]
            at += 2
            if reliable_size == 0:
                break
            if reliable_size < 0 or at + 4 + reliable_size > len(message):
                raise Refused(f"packet {index} reliable size")
            reliable_sequence = struct.unpack_from("<i", message, at)[0]
            body = message[at + 4:at + 4 + reliable_size]
            at += 4 + reliable_size
            if reliable_sequence != reliable_last + 1:
                continue
            reliable_last = reliable_sequence
            if body[0] == 6 or (body[0] == 5 and not demo.server):   # GAME, APPLYSNAPSHOT
                demo.reliables += 1
        if len(message) - at >= 5:
            game_init, unreliable = struct.unpack_from("<iB", message, at)
            if unreliable == 3 and game_init == demo.game_init and len(message) - at >= 20:
                snapshot_sequence = struct.unpack_from("<i", message, at + 5)[0]
                if snapshot_sequence > last_snapshot:
                    last_snapshot = snapshot_sequence
                    demo.snapshots += 1


def damaged_variants(name: str, data: bytes) -> list[tuple[str, bytes, str]]:
    """Damaged copies of a fixture: (file name, bytes, what is damaged)."""
    demo = parse(data)
    stem = Path(name).stem
    put = lambda buffer, offset, value: buffer[:offset] + struct.pack("<i", value) + buffer[offset + 4:]
    first_time, first_offset, first_size = demo.packets[0]
    middle = demo.packets[len(demo.packets) // 2]
    out = [
        ("header", data[:10], "truncated inside the header"),
        ("paks", data[:demo.offsets["fs"] + 40], "truncated inside the pak list"),
        ("users", data[:demo.offsets["users"] + 30], "truncated inside the player info"),
        ("network", data[:demo.offsets["network"] + 9], "truncated inside the game's network info"),
        ("packets", data[:middle[1] + middle[2] // 2], "truncated inside a packet"),
        ("tail", data[:demo.usercmd_start + 2], "truncated inside the command stream"),
        ("version", put(data, 4, 11), "another release's demo version"),
        ("protocol", put(data, 8, (2 << 16) | 71), "another release's protocol"),
        ("ucmdstart", put(data, 12, len(data) + 100), "command stream offset past the end"),
        ("fsgame", put(data, demo.offsets["fs"], -5), "negative string length"),
        ("pakcount", put(data, demo.offsets["fs"] + 4 + 4 + len(b"q4mp"), 0x7FFFFFFF), "pak count"),
        ("usercount", put(data, demo.offsets["users"], 0x10000000), "dictionary count"),
        ("packetsize", put(data, first_offset - 4, 0x7FFF0000), "packet size"),
        ("packetgrow", put(data, first_offset - 4, first_size + 40000), "packet size past the stream"),
        ("messagesize", data[:first_offset + 6] + struct.pack("<h", 30000) + data[first_offset + 8:],
         "channel message size"),
    ]
    if not demo.server and "first_state" in demo.offsets:
        out.append(("statesize", put(data, demo.offsets["first_state"] + 4, 600), "entity state size"))
        out.append(("snapcount", put(data, demo.offsets["snapshots"], 1000), "pending snapshot count"))
    if demo.server:
        out.append(("recorder", put(data, demo.offsets["client"] + 12, 5), "server demo with a player as recorder"))
    # damage inside a packet's coded bytes: the container stays valid, so only
    # the channel decode or the game can catch it; it must never crash
    flipped = bytearray(data)
    for k in range(8, min(middle[2], 64), 7):
        flipped[middle[1] + k] ^= 0x5A
    out.append(("payload", bytes(flipped), "flipped bits inside a packet"))
    return [(f"bad_{stem}_{tag}.ndmo85", blob, what) for tag, blob, what in out]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="replace")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def source_checks(failures: list[str]) -> None:
    header = read("src/framework/async/RetailNetDemo.h")
    reader = read("src/framework/async/RetailNetDemo.cpp")
    for token in ("NETDEMO_IDENT				= 0x4F4D444E", "NETDEMO_VERSION			= 12", "NETDEMO_PROTOCOL_MAJOR	= 2",
                  "NETDEMO_PROTOCOL_MINOR	= 85", "NETDEMO_MAX_STATE_BYTES	= 512", "NETDEMO_MAX_GAME_MESSAGE	= 8192",
                  "NETDEMO_MAX_MESSAGE		= 16384", "NETDEMO_FRAGMENT_SIZE		= 1400 - 100", "NETDEMO_MAX_RELAYED_CMDS	= 10"):
        require(token in header, f"RetailNetDemo.h pins {token!r}", failures)
    for token in (
        "count < 0 || count > maxLength", "count < 0 || count > 4096", "pakCount < 0 || pakCount > NETDEMO_MAX_PURE_PAKS",
        "fragmentLength < 0 || fragmentLength > NETDEMO_MAX_MESSAGE", "queueEnd != queueStart",
        "size < 1 || size > NETDEMO_MAX_STATE_BYTES", "snapshots < 0 || snapshots > NETDEMO_MAX_SNAPSHOTS",
        "size < 1 || size > NETDEMO_MAX_GAME_MESSAGE || !r.Skip( size )",
        "size < 0 || size > NETDEMO_MAX_MESSAGE || r.cursor > demo.usercmdStart - size",
        "if ( r.cursor == demo.usercmdStart ) {", "r.cursor != length",
        "messageSize > 4 * ( payloadLength - 2 ) + 8", "gameBytes < 1 || gameBytes > NETDEMO_MAX_GAME_MESSAGE",
        "reliableSequence != reliableLast + 1", "int lastSnapshotSequence = demo.serverDemo ? 0 : demo.snapshotSequence;",
        "FS_FilePack( fileName, checksum, gameDir )", "gameDir.Icmp( BASE_GAMEDIR ) == 0",
        "DeclManager_ShadowedSource( declType, i, shadowFile, shadowLine )",
    ):
        require(token in reader, f"RetailNetDemo.cpp checks {token!r}", failures)
    mvd = read("src/framework/async/MultiViewDemo.cpp")
    for token in ("openq4_retail::IsRetailNetDemoName( fileName.c_str() )", "OpenRetailNetDemo()",
                  "openq4_retail::ParseContainer( bytes.Ptr(), length, demo, lastError )",
                  "openq4_retail::MissingPaks( demo, missing )", "fps != common->GetUserCmdHz()",
                  "openq4_retail::BuildDeclRemap( demo );", "retailSource ? retailEntityDefs : declManager->GetNumDecls( DECL_ENTITYDEF )",
                  "openq4_retail::ReadRelayedUsercmd( msg, cmd, last )", "MVD_ValidateReliablePayload( record.payload, record.version, lastError, retailSource )",
                  "return QueryRetailFileInfo( name, info );", "openq4_retail::ClearDeclRemap();",
                  "cmdSystem->AddCommand( \"playNetTimeDemo\", TimeDemo_f"):
        require(token in mvd, f"MultiViewDemo.cpp has {token!r}", failures)
    game_h = read("src/mpgame/Game_local.h")
    require("GAME_SNAPSHOT_PROTOCOL_RETAIL		= 85" in game_h, "the game names protocol 2.85", failures)
    require("networkSystem->RetailDemoDeclIndex( type, index )" in game_h, "the game asks for the recording's decl numbering", failures)
    require(game_h.count("gameLocal.NetworkDeclIndex( type, msg.ReadLong() )") == 2, "both ReadDecl overloads renumber", failures)
    network = read("src/mpgame/Game_network.cpp")
    require("( minor_ref == GAME_SNAPSHOT_PROTOCOL && minor == GAME_SNAPSHOT_PROTOCOL_RETAIL )" in network,
            "the game accepts a 2.85 recording", failures)
    require("entityDefBits = idMath::BitsForInteger( NetworkDeclCount( DECL_ENTITYDEF ) ) + 1;" in network,
            "entity definitions are read in the recording's width", failures)
    require(network.count("deltaMsg.SetRetailBaseReads( RetailSnapshotFormat() );") == 2,
            "snapshot deltas read the base as 1.4.2 did", failures)
    gates = {
        "src/mpgame/Player.cpp": ("RetailSnapshotFormat() ? 16 : POWERUP_MAX", "gameLocal.IsClassicRuleset() && !gameLocal.RetailSnapshotFormat()",
                                  "memcpy( decodedWeaponMods, inventory.weaponMods, sizeof( decodedWeaponMods ) );"),
        "src/mpgame/physics/Physics_Player.cpp": ("gameLocal.IsClassicRuleset() && !gameLocal.RetailSnapshotFormat()",),
        "src/mpgame/mp/GameState.cpp": ("Quake 4 1.4.2 had no overtime state", "gameLocal.RetailSnapshotFormat() ? TEAM_MAX : MAX_CTF_FLAGS"),
        "src/mpgame/MultiplayerGame.cpp": ("gameLocal.RetailSnapshotFormat() ? 16 : MP_StartStatePowerupBits( msg )",),
        "src/mpgame/Entity.cpp": ("gameLocal.RetailSnapshotFormat() ? NULL :",),
    }
    for path, tokens in gates.items():
        text = read(path)
        for token in tokens:
            require(token in text, f"{path} gates {token!r}", failures)
    bitmsg = read("src/idlib/BitMsg.cpp")
    require("if ( retailBaseReads && ( numBits < 0 ? -numBits : numBits ) > base->GetRemainingReadBits() ) {" in bitmsg,
            "a retail base read past its end gives -1 without overflowing", failures)
    session = read("src/framework/Session_demo.cpp")
    require('DemoCollectExtension( paths, ".ndmo85" );' in session, "the library lists .ndmo85 files", failures)
    require("entry.type == DEMO_LIBRARY_MVD || entry.type == DEMO_LIBRARY_LEGACY_NET" in session,
            "the library plays a retail network demo through the MVD player", failures)
    for language in ("brazilian", "czech", "english", "french", "german", "hungarian", "italian", "polish",
                     "russian", "spanish", "turkish", "ukrainian"):
        strings = read(f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        for number in (232880, 232881, 232882):
            require(f'"#str_{number}"' in strings, f"{language} has #str_{number}", failures)
    docs = read("docs/user/multiview-demos.md")
    require(".ndmo85" in docs, "the player guide names .ndmo85", failures)


def fixture_checks(basepath: Path, failures: list[str]) -> int:
    folder = basepath / "q4mp" / "demos"
    checked = 0
    for name, expected in FIXTURES.items():
        path = folder / name
        if not path.is_file():
            continue
        data = path.read_bytes()
        try:
            demo = parse(data)
            decode(data, demo)
        except Refused as error:
            failures.append(f"{name}: the reference reader refused it: {error}")
            continue
        got = {"server": demo.server, "client": demo.client, "map": demo.server_info.get("si_map"),
               "packets": len(demo.packets), "snapshots": demo.snapshots, "reliables": demo.reliables,
               "duration_ms": demo.packets[-1][0] - demo.packets[0][0]}
        require(got == expected, f"{name}: read {got}, expected {expected}", failures)
        require(demo.server_info.get("si_fps", "60") == "60", f"{name}: recorded at 60 frames a second", failures)
        for variant, blob, what in damaged_variants(name, data):
            try:
                damaged = parse(blob)
                decode(blob, damaged)
                if "payload" not in variant:
                    failures.append(f"{variant}: the reference reader accepted a demo with a damaged {what}")
            except Refused:
                pass
        checked += 1
    return checked


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--basepath", type=Path, default=DEFAULT_BASEPATH, help="Quake 4 1.4.2 installation")
    args = parser.parse_args()
    failures: list[str] = []
    source_checks(failures)
    checked = fixture_checks(args.basepath, failures)
    for failure in failures:
        print(f"retail_netdemo_contract: FAIL: {failure}")
    if failures:
        return 1
    print(f"retail_netdemo_contract: ok ({checked} shipped demo(s) read"
          f"{'' if checked else '; no Quake 4 1.4.2 installation, fixture part skipped'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
