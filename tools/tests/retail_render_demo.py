#!/usr/bin/env python3
"""Decode a retail Quake 4 1.4.2 render demo stream (MP-22).

An independent decoder of the layout openQ4's retail reader assumes
(`src/renderer/RenderWorld_demo_retail.cpp`, `src/sound/snd_world.cpp`): it
walks an uncompressed `.demo` (scheme 0, as retail's `compressDemo <name> 0`
writes it) command by command, checks every count and index against the bytes
that remain, and reports what the stream holds. It never trusts a length it has
not bounded, so a damaged file ends in a `DemoError`, not an exception from a
read past the end.

The stream is little-endian. Retail writes the top-level token and every
subcommand as one byte, unlike openQ4's own 32-bit tokens:

    DS_FINISHED 0, DS_RENDER 1, DS_SOUND 2, DS_VERSION 3 (+ int savegame version)

Use: `retail_render_demo.py <file.demo> [--json]`.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

MAGIC = b"Quake4 RDEMO\x00"
RENDERDEMO_VERSION = 1834
RENDER_ENTITY_SIZE = 224
RENDER_LIGHT_SIZE = 216
MAP_NAME_BYTES = 256
SHADER_PARMS = 12
GLOBAL_SHADER_PARMS = 12
ENTITY_GUIS = 3
SOUND_CHANNELS = 8
# bounds no real recording comes near; they keep a damaged count from
# allocating or looping without end
MAX_JOINTS = 65536
MAX_GUI_VERTS = 1 << 20
MAX_GUI_INDEXES = 1 << 22
MAX_GUI_SURFACES = 1 << 16
MAX_EMITTERS = 1 << 16
MAX_STRING = 1 << 16
MAX_HANDLE = 1 << 20
MAX_WINDOW_DEPTH = 64
MAX_GUI_REGISTERS = 1 << 16
MAX_GUI_CHILDREN = 1 << 12
MAX_DICT = 1 << 12

DC_NAMES = {
    1: "RENDERVIEW", 2: "PORTALSKY_RENDERVIEW", 3: "UPDATE_ENTITYDEF", 4: "DELETE_ENTITYDEF",
    5: "ENTITYDEF", 6: "UPDATE_LIGHTDEF", 7: "DELETE_LIGHTDEF", 8: "LIGHTDEF", 9: "UPDATE_EFFECTDEF",
    10: "STOP_EFFECTDEF", 11: "DELETE_EFFECTDEF", 12: "EFFECTDEF", 13: "LOADMAP", 14: "CROP_RENDER",
    15: "UNCROP_RENDER", 16: "CAPTURE_RENDER", 17: "END_FRAME", 18: "DEFINE_MODEL",
    19: "SET_PORTAL_STATE", 20: "GUI_MODEL",
}
SCMD_NAMES = {0: "STATE", 1: "PLACE_LISTENER", 2: "ALLOC_EMITTER", 3: "FREE", 4: "UPDATE", 5: "START",
              6: "MODIFY", 7: "STOP", 8: "FADE"}


class DemoError(ValueError):
    pass


class ProbeStop(Exception):
    """A probe reached an effect whose state it cannot follow: it decoded so far."""


class Reader:
    def __init__(self, data: bytes, offset: int = 0):
        self.data, self.pos, self.strings = data, offset, []

    def remaining(self) -> int:
        return len(self.data) - self.pos

    def take(self, count: int) -> bytes:
        if count < 0 or count > self.remaining():
            raise DemoError(f"truncated at byte {self.pos}: wants {count}, has {self.remaining()}")
        chunk = self.data[self.pos:self.pos + count]
        self.pos += count
        return chunk

    def byte(self) -> int:
        return self.take(1)[0]

    def boolean(self) -> bool:
        return self.byte() != 0

    def int(self) -> int:
        return struct.unpack("<i", self.take(4))[0]

    def float(self) -> float:
        return struct.unpack("<f", self.take(4))[0]

    def floats(self, count: int) -> tuple:
        return struct.unpack(f"<{count}f", self.take(4 * count))

    def bounded(self, name: str, low: int, high: int) -> int:
        value = self.int()
        if value < low or value > high:
            raise DemoError(f"{name} {value} outside {low}..{high} at byte {self.pos - 4}")
        return value

    def string(self) -> str:
        length = self.bounded("string length", 0, MAX_STRING)
        return self.take(length).decode("latin-1")

    def hash_string(self) -> str:
        index = self.int()
        if index == -1:
            value = self.string()
            self.strings.append(value)
            return value
        if index < 0 or index >= len(self.strings):
            raise DemoError(f"hash string {index} of {len(self.strings)} at byte {self.pos - 4}")
        return self.strings[index]


def read_render_view(r: Reader) -> dict:
    view = {"viewID": r.int(), "rect": [r.int() for _ in range(4)], "fov": r.floats(2),
            "origin": r.floats(3), "axis": r.floats(9), "cramZNear": r.boolean(), "forceUpdate": r.boolean(),
            "time": r.int(), "shaderParms": r.floats(GLOBAL_SHADER_PARMS)}
    view["globalMaterial"] = r.hash_string() if r.int() else None
    return view


def read_win_vars(r: Reader, kinds: str) -> None:
    """Window variables: a name, an evaluation flag, then the value."""
    sizes = {"b": 1, "f": 4, "2": 8, "4": 16}
    for kind in kinds:
        r.hash_string()
        r.boolean()
        if kind == "s":
            r.hash_string()
        else:
            r.take(sizes[kind])


def read_simple_window(r: Reader) -> None:
    r.hash_string()
    r.take(4 + 4 * 12 + 8 + 4 + 4 * 3 + 4 + 4 * 2 + 4 + 4)
    read_win_vars(r, "sb44444ff2sb")


def read_window(r: Reader, depth: int = 0) -> int:
    """One window and its children; returns how many windows it held."""
    if depth > MAX_WINDOW_DEPTH:
        raise DemoError("gui windows nest too deep")
    r.take(4 * 2 + 4 + 4 + 4 + 4 * 8 + 8 + 4 + 4 * 9 + 8 + 4 * 3 + 1 + 1 + 4)
    read_win_vars(r, "bbbb444444ffss")
    r.take(4 * 4)
    r.hash_string()
    for _ in range(r.bounded("gui register count", 0, MAX_GUI_REGISTERS)):
        r.take(1 + 2 + 4 + 2 * 4)
        r.hash_string()
    windows = 1
    for _ in range(r.bounded("gui child count", 0, MAX_GUI_CHILDREN)):
        if r.boolean():
            windows += read_window(r, depth + 1)
        else:
            read_simple_window(r)
            windows += 1
    return windows


def read_gui(r: Reader) -> dict:
    """An in-world GUI: retail serializes its whole window tree."""
    gui_id = r.int()
    state = {}
    for _ in range(r.bounded("gui state count", 0, MAX_DICT)):
        key = r.hash_string()
        state[key] = r.hash_string()
    windows = read_window(r)
    r.floats(2)
    return {"id": gui_id, "name": state.get("name", ""), "windows": windows}


def read_entity(r: Reader) -> dict:
    handle = r.bounded("entity handle", 0, MAX_HANDLE)
    model = r.int()
    r.take(4 * 2 + 24 + 4 * 2 + 4 * 5 + 12 + 36)  # entityNum, bodyId, bounds, callback(+data), view ids, origin, axis
    custom, reference, overlay, skin = (r.int() for _ in range(4))
    r.take(4 + 4 * SHADER_PARMS)  # sound handle, parms
    guis = [r.int() for _ in range(ENTITY_GUIS)]
    r.take(4)  # remote view
    joints = r.bounded("joint count", 0, MAX_JOINTS)
    r.take(4 + 4 + 4 + 4 + 4)  # joints pointer, depth hack, four bools, depth hack view, shadow LOD
    r.take(4)  # suppressLOD
    names = {}
    for key, present in (("model", model), ("customShader", custom), ("referenceShader", reference),
                         ("overlayShader", overlay), ("customSkin", skin)):
        if present:
            names[key] = r.hash_string()
    r.take(joints * 48)
    names["guis"] = [read_gui(r) for present in guis if present]
    return {"handle": handle, "joints": joints, **names}


def read_light(r: Reader) -> dict:
    handle = r.bounded("light handle", 0, MAX_HANDLE)
    return {"handle": handle, **read_render_light_payload(r)}


# BSE particle records (retail rvParticle::WriteToDemo and its overrides)
PARTICLE_TYPES = ("sprite", "line", "oriented", "model", "light", "electricity", "linked", "debris")
MAX_PARTICLE_INDEX = 1 << 14


def read_envelope(r: Reader, kind: str) -> None:
    """rvEnvParms: 1D writes four floats, 2D/3D a flag and four vectors; then the table."""
    if kind == "1":
        r.take(16)
    else:
        r.boolean()
        r.take(4 * (8 if kind == "2" else 12))
    if r.int():
        r.string()


def read_render_light_payload(r: Reader) -> dict:
    r.take(36 + 12 + 4 + 4 + 4 + 4 + 12 * 7)
    prelight = r.int()
    r.take(4)
    shader = r.int()
    r.take(4 * SHADER_PARMS + 4)
    names = {}
    if prelight:
        names["prelightModel"] = r.hash_string()
    if shader:
        names["shader"] = r.hash_string()
    return names


def read_particle(r: Reader, kind: str) -> None:
    r.bounded("particle link", -1, MAX_PARTICLE_INDEX)
    r.take(4)
    flags = r.bounded("particle flags", 0, 1 << 24)
    r.take(4 + 12 + 12)
    full = not flags & 8
    if full:
        r.take(4 * 3 + 4 + 4 * 2 + 12 + 36 + 12 * 3)
        read_envelope(r, "3"), read_envelope(r, "1"), read_envelope(r, "3"), read_envelope(r, "3")
    if kind == "light":
        r.int()
        read_render_light_payload(r)
    extra = {"sprite": "21", "line": "13", "oriented": "23", "model": "33", "light": "3",
             "electricity": "13", "linked": "1", "debris": "3"}[kind]
    if full:
        for envelope in extra:
            read_envelope(r, envelope)
        if kind == "model":
            r.string()
    if kind == "electricity":
        r.int()
        if full:
            r.take(4 * 2 + 12 * 3 + 4 * 2)
            r.string()


def read_segment(r: Reader, kind: str | None) -> None:
    flags = r.bounded("segment flags", 0, 1 << 24)
    r.take(8)
    if not flags & 8:
        r.take(4 + 4 * 2 + 8 * 2 + 4 * 2 + 4 * 2)
    if kind is None:
        return
    r.bounded("free particle", -1, MAX_PARTICLE_INDEX)
    r.bounded("used particle", -1, MAX_PARTICLE_INDEX)
    for _ in range(MAX_PARTICLE_INDEX):
        if r.bounded("particle", -1, MAX_PARTICLE_INDEX) < 0:
            return
        read_particle(r, kind)
    raise DemoError("particle chain without its end")


def next_command_parses(r: Reader, lookahead: int = 3) -> bool:
    """True when the next few commands decode: the end of an effect's state."""
    probe = Reader(r.data, r.pos)
    probe.strings = list(r.strings)
    scratch = {"render": {}, "sound": {}, "frames": 0, "maps": [], "models": set(), "effects": set(),
               "sounds": set(), "guis": set(), "max_joints": 0, "gui_surfaces": 0}
    try:
        for _ in range(lookahead):
            if probe.remaining() == 0:
                return True
            token = probe.byte()
            if token == 0:
                return probe.remaining() == 0
            if token == 1:
                read_render(probe, scratch, probe_effects=False)
            elif token == 2:
                if probe.remaining() and probe.data[probe.pos] == 0:
                    return False  # a sound state starts a recording, never follows an effect
                read_sound(probe, scratch)
            else:
                return False
        return True
    except ProbeStop:
        return True
    except DemoError:
        return False


def read_segments(r: Reader, depth: int = 0) -> bool:
    """Retail writes no segment count and no pool flag: search for the layout
    that leaves the stream at a whole command. The engine reads both from the
    effect's declaration instead."""
    if next_command_parses(r):
        return True
    if depth >= 64:
        return False
    for kind in (None, *PARTICLE_TYPES):
        saved, strings = r.pos, list(r.strings)
        try:
            read_segment(r, kind)
            if read_segments(r, depth + 1):
                return True
        except DemoError:
            pass
        r.pos, r.strings = saved, strings
    return False


def read_bse_state(r: Reader) -> None:
    flags = r.int()
    r.take(4 * 3 + 16 + 4 * 2 + 12 + 36 + 12 * 3 + 4 + 12 * 3 + 36 * 2 + 24 * 2)
    if not flags & 0x40:
        r.take(4 + 12 * 2 + 36 + 4 * 3)
    if not read_segments(r):
        raise DemoError("effect state matches no segment layout")


def read_effect(r: Reader, probe_effects: bool = True) -> dict:
    handle = r.bounded("effect handle", 0, MAX_HANDLE)
    decl = r.int()
    r.take(4 * 3 + 12 + 36 + 12 + 12 + 4 + 4 + 4 + 4 + 4 + 4 * SHADER_PARMS)
    name = r.hash_string() if decl else None
    if not probe_effects:
        raise ProbeStop()
    read_bse_state(r)
    return {"handle": handle, "effect": name}


def read_gui_model(r: Reader) -> dict:
    verts = r.bounded("gui vertex count", 0, MAX_GUI_VERTS)
    r.take(verts * 60)
    indexes = r.bounded("gui index count", 0, MAX_GUI_INDEXES)
    for value in struct.unpack(f"<{indexes}i", r.take(4 * indexes)):
        if value < 0 or value >= max(verts, 1):
            raise DemoError(f"gui index {value} of {verts} vertices")
    surfaces = r.bounded("gui surface count", 0, MAX_GUI_SURFACES)
    materials = []
    for _ in range(surfaces):
        r.take(4 + 16)
        first_vert, num_verts, first_index, num_indexes = (r.int() for _ in range(4))
        if not (0 <= first_vert <= verts and 0 <= num_verts <= verts - first_vert
                and 0 <= first_index <= indexes and 0 <= num_indexes <= indexes - first_index):
            raise DemoError("gui surface range outside its model")
        materials.append(r.hash_string())
    return {"verts": verts, "indexes": indexes, "surfaces": surfaces, "materials": materials}


def read_render(r: Reader, stats: dict, probe_effects: bool = True) -> bool:
    command = r.byte()
    name = DC_NAMES.get(command)
    if name is None:
        raise DemoError(f"render command {command} at byte {r.pos - 1}")
    stats["render"][name] = stats["render"].get(name, 0) + 1
    if name in ("RENDERVIEW", "PORTALSKY_RENDERVIEW"):
        view = read_render_view(r)
        stats.setdefault("views", []).append({"kind": name, "time": view["time"], "origin": view["origin"],
                                               "fov": view["fov"], "rect": view["rect"]})
    elif name in ("UPDATE_ENTITYDEF", "DELETE_ENTITYDEF", "UPDATE_LIGHTDEF", "DELETE_LIGHTDEF",
                  "UPDATE_EFFECTDEF", "STOP_EFFECTDEF", "DELETE_EFFECTDEF"):
        r.bounded("handle", 0, MAX_HANDLE)
    elif name == "ENTITYDEF":
        entity = read_entity(r)
        stats["models"].add(entity.get("model", ""))
        stats["max_joints"] = max(stats["max_joints"], entity["joints"])
        for gui in entity["guis"]:
            stats["guis"].add(gui["name"])
    elif name == "LIGHTDEF":
        read_light(r)
    elif name == "EFFECTDEF":
        stats["effects"].add(read_effect(r, probe_effects)["effect"] or "")
    elif name == "LOADMAP":
        version, entity_size, light_size, word = r.int(), r.int(), r.int(), r.int()
        map_name = r.take(MAP_NAME_BYTES).split(b"\0", 1)[0].decode("latin-1")
        start = r.int()
        if (entity_size, light_size) != (RENDER_ENTITY_SIZE, RENDER_LIGHT_SIZE):
            raise DemoError(f"load map header {version}/{entity_size}/{light_size}")
        stats["maps"].append({"map": map_name, "version": version, "word": word, "startTimeMsec": start})
    elif name == "CROP_RENDER":
        width, height, _ = r.int(), r.int(), r.int()
        if not (0 < width <= 32768 and 0 < height <= 32768):
            raise DemoError(f"crop {width}x{height}")
    elif name == "CAPTURE_RENDER":
        r.hash_string()
    elif name == "SET_PORTAL_STATE":
        r.int(), r.int()
    elif name == "GUI_MODEL":
        gui = read_gui_model(r)
        stats["gui_surfaces"] = max(stats["gui_surfaces"], gui["surfaces"])
    elif name == "DEFINE_MODEL":
        raise DemoError("DC_DEFINE_MODEL: retail never writes it")
    return name == "END_FRAME"


def read_sound_parms(r: Reader) -> tuple:
    return r.floats(10)


def read_sound_state(r: Reader) -> dict:
    r.take(12 + 36 + 4 * 4)
    emitters = r.bounded("emitter count", 0, MAX_EMITTERS)
    alive, channels = 0, 0
    for index in range(1, emitters):
        handle = r.int()
        if handle < 0:
            continue
        if handle != index:
            raise DemoError(f"sound state emitter {handle} in slot {index}")
        alive += 1
        r.take(12 + 4)
        read_sound_parms(r)
        r.take(8)
        for _ in range(SOUND_CHANNELS):
            r.take(1 + 4)
            read_sound_parms(r)
            r.take(4 * 5 + 4 * 2 + 4 * 6 + 4 * 4 + 4 * 3)
        r.take(4 + 1 + 4 + 4 + 1 + 4 + 4 + 12)
        while True:
            channel = r.int()
            if channel < 0:
                break
            if channel >= SOUND_CHANNELS:
                raise DemoError(f"sound state channel {channel}")
            r.string(), r.string()
            channels += 1
    return {"emitters": emitters, "alive": alive, "channels": channels}


def read_sound(r: Reader, stats: dict) -> None:
    command = r.byte()
    name = SCMD_NAMES.get(command)
    if name is None:
        raise DemoError(f"sound command {command} at byte {r.pos - 1}")
    stats["sound"][name] = stats["sound"].get(name, 0) + 1
    if name == "STATE":
        stats["sound_state"] = read_sound_state(r)
    elif name == "PLACE_LISTENER":
        r.take(12 + 36 + 8)
    elif name == "ALLOC_EMITTER":
        r.bounded("emitter", 1, MAX_EMITTERS)
    elif name == "FREE":
        r.bounded("emitter", 1, MAX_EMITTERS), r.boolean()
    elif name == "UPDATE":
        r.bounded("emitter", 1, MAX_EMITTERS)
        r.take(12 + 12 + 4)
        read_sound_parms(r)
    elif name == "START":
        r.bounded("emitter", 1, MAX_EMITTERS)
        stats["sounds"].add(r.hash_string())
        r.int(), r.float(), r.int()
    elif name == "MODIFY":
        r.bounded("emitter", 1, MAX_EMITTERS), r.int()
        read_sound_parms(r)
    elif name == "STOP":
        r.bounded("emitter", 1, MAX_EMITTERS), r.int()
    elif name == "FADE":
        r.bounded("emitter", 1, MAX_EMITTERS), r.int(), r.float(), r.float()


def decode(data: bytes) -> dict:
    if not data.startswith(MAGIC) or len(data) < len(MAGIC) + 4:
        raise DemoError("not a Quake4 RDEMO file")
    scheme = struct.unpack_from("<i", data, len(MAGIC))[0]
    if scheme != 0:
        raise DemoError(f"compression scheme {scheme}: decode an uncompressed copy (compressDemo <name> 0)")
    r = Reader(data, len(MAGIC) + 4)
    stats = {"render": {}, "sound": {}, "frames": 0, "maps": [], "models": set(), "effects": set(),
             "sounds": set(), "guis": set(), "max_joints": 0, "gui_surfaces": 0, "version": None}
    while r.remaining():
        token = r.byte()
        if token == 0:
            break
        if token == 1:
            if read_render(r, stats):
                stats["frames"] += 1
        elif token == 2:
            read_sound(r, stats)
        elif token == 3:
            stats["version"] = r.int()
        else:
            raise DemoError(f"top-level token {token} at byte {r.pos - 1}")
    stats["trailing_bytes"] = r.remaining()
    for key in ("models", "effects", "sounds", "guis"):
        stats[key] = sorted(stats[key])
    views = stats.pop("views", [])
    stats["first_views"] = views[:4]
    stats["view_count"] = len(views)
    stats["view_times"] = [v["time"] for v in views if v["kind"] == "RENDERVIEW"]
    return stats


def command_offsets(data: bytes) -> dict:
    """Where a few kinds of record first start in an uncompressed stream, for
    tests that cut or corrupt one: the offset of its DS_ byte, and for some the
    offset of a count or index inside it."""
    marks = {}
    read_render, read_sound, read_entity = globals()["read_render"], globals()["read_sound"], globals()["read_entity"]
    read_gui, hash_string = globals()["read_gui"], Reader.hash_string

    def on_render(r, stats, **kw):
        if kw.get("probe_effects", True):
            name = DC_NAMES.get(r.data[r.pos])
            if name and name not in marks:
                marks[name] = r.pos - 1
        return read_render(r, stats, **kw)

    def on_sound(r, stats):
        if r.data[r.pos] == 0 and "SCMD_STATE" not in marks:
            marks["SCMD_STATE"] = r.pos - 1
        return read_sound(r, stats)

    def on_entity(r):
        start = r.pos
        entity = read_entity(r)
        if entity["joints"] and "JOINTS" not in marks:
            marks["JOINTS"] = start + 4 * 4 + 24 + 4 * 7 + 12 + 36 + 4 * 5 + 4 * SHADER_PARMS + 4 * ENTITY_GUIS + 4
        return entity

    def on_gui(r):
        if "ENTITYDEF_GUI" not in marks:
            marks["ENTITYDEF_GUI"] = marks.get("ENTITYDEF", r.pos)
            marks["ENTITYDEF_GUI_STATE"] = r.pos + 4
        return read_gui(r)

    def on_hash(self):
        if "HASH" not in marks and self.remaining() >= 4 and struct.unpack_from("<i", self.data, self.pos)[0] >= 0:
            marks["HASH"] = self.pos
        return hash_string(self)

    globals().update(read_render=on_render, read_sound=on_sound, read_entity=on_entity, read_gui=on_gui)
    Reader.hash_string = on_hash
    try:
        decode(data)
    finally:
        globals().update(read_render=read_render, read_sound=read_sound, read_entity=read_entity, read_gui=read_gui)
        Reader.hash_string = hash_string
    return marks


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("demo", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        stats = decode(args.demo.read_bytes())
    except DemoError as error:
        print(f"{args.demo}: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(stats, indent=2))
    else:
        summary = {k: v for k, v in stats.items() if k not in ("models", "effects", "sounds", "view_times")}
        print(json.dumps(summary, indent=2))
        print(f"{len(stats['models'])} models, {len(stats['effects'])} effects, {len(stats['sounds'])} sounds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
