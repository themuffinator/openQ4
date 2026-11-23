#!/usr/bin/env python3
"""Contract for the retail Quake 4 render demo reader (MP-22).

Needs no game files. It builds a retail-layout stream (every record kind the
reader accepts, including an in-world GUI's window tree and an effect's live
state), checks that the independent decoder `retail_render_demo.py` reads it to
its last byte, that every cut and a few thousand corrupted copies end in a
`DemoError` rather than an exception from a read past the end, and pins the
engine wiring that the staged smoke (`retail_render_demo_smoke.py`) exercises:
stream detection, byte tokens, the bounds order, the effect layout query, the
library entry and its string in every language.
"""
from __future__ import annotations

import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

import retail_render_demo as decoder  # noqa: E402


class Stream:
    def __init__(self):
        self.out = bytearray()
        self.strings: list[str] = []

    def byte(self, value: int):
        self.out += bytes([value & 0xff])

    def int(self, value: int):
        self.out += struct.pack("<i", value)

    def float(self, *values: float):
        for value in values:
            self.out += struct.pack("<f", value)

    def string(self, text: str):
        self.int(len(text))
        self.out += text.encode("latin-1")

    def hash(self, text: str):
        if text in self.strings:
            self.int(self.strings.index(text))
        else:
            self.strings.append(text)
            self.int(-1)
            self.string(text)

    def render(self, command: int):
        self.byte(1)
        self.byte(command)

    def sound(self, command: int):
        self.byte(2)
        self.byte(command)


def sound_parms(s: Stream):
    s.float(40.0, 400.0, 1.0, 1.0, 0.0)
    s.int(0)
    s.int(0)
    s.float(1.0, 0.0, 0.0)


def win_vars(s: Stream, kinds: str):
    for index, kind in enumerate(kinds):
        s.hash(f"var{index}")
        s.byte(1)
        if kind == "b":
            s.byte(1)
        elif kind == "f":
            s.float(1.0)
        elif kind == "2":
            s.float(0.0, 0.0)
        elif kind == "4":
            s.float(0.0, 0.0, 640.0, 480.0)
        else:
            s.hash("text")


def window(s: Stream, children: int):
    s.float(0.0, 0.0)
    s.int(0)
    s.int(0x10000000)
    s.int(0)
    s.float(*[0.0] * 8)
    s.float(0.0, 0.0)
    s.int(0)
    s.float(*[0.0] * 9)
    s.float(0.0, 0.0)
    s.int(0), s.int(0), s.int(0)
    s.byte(0), s.byte(0)
    s.float(0.0)
    win_vars(s, "bbbb444444ffss")
    s.float(0.0, 0.0, 640.0, 480.0)
    s.hash("Desktop")
    s.int(1)  # one register
    s.byte(1)
    s.out += struct.pack("<h", 7)
    s.int(4)
    s.out += struct.pack("<4H", 1, 2, 3, 4)
    s.hash("rect")
    s.int(children + 1)
    for _ in range(children):
        s.byte(1)
        window(s, 0)
    s.byte(0)  # a simple window
    s.hash("simple")
    s.int(0)
    s.float(*[0.0] * 12)
    s.float(0.0, 0.0)
    s.int(0)
    s.float(1.0, 1.0, 0.0)
    s.int(0)
    s.float(0.0, 0.0)
    s.int(0), s.int(0)
    win_vars(s, "sb44444ff2sb")


def build_stream() -> bytes:
    s = Stream()
    s.out += b"Quake4 RDEMO\x00"
    s.int(0)  # uncompressed
    s.byte(3)
    s.int(0)  # retail savegame version
    # the sound world's state: one live emitter playing one shader
    s.sound(0)
    s.float(0.0, 0.0, 0.0)
    s.float(1, 0, 0, 0, 1, 0, 0, 0, 1)
    s.int(1), s.int(0), s.int(0), s.int(0)
    s.int(3)  # emitters 1 and 2
    s.int(1)
    s.float(16.0, 0.0, 0.0)
    s.int(0)
    sound_parms(s)
    s.float(0.0)
    s.int(0)
    for slot in range(8):
        s.byte(1 if slot == 0 else 0)
        s.int(0)
        sound_parms(s)
        s.int(0), s.int(0), s.int(1 if slot == 0 else 0), s.int(0), s.int(0)
        s.float(*[0.0] * 15)
    s.float(0.0)
    s.byte(0)
    s.int(0)
    s.float(400.0)
    s.byte(1)
    s.float(0.0)
    s.int(0)
    s.float(0.0, 0.0, 0.0)
    s.int(0)
    s.string("ambient_hum")
    s.string("sound/ambient/hum.wav")
    s.int(-1)
    s.int(-1)  # emitter 2 is free
    # the map
    s.render(13)
    s.int(0), s.int(224), s.int(216), s.int(0)
    s.out += b"maps/game/test".ljust(256, b"\x00")
    s.int(1000)
    # an entity with joints and an in-world GUI
    s.render(5)
    s.int(7)
    s.int(1)  # model
    s.int(42), s.int(0)
    s.float(8, 8, 8, -8, -8, -8)  # bounds, maximum first
    s.int(0), s.int(0)
    s.int(0), s.int(0), s.int(0), s.int(0), s.int(0)
    s.float(0.0, 0.0, 0.0)
    s.float(1, 0, 0, 0, 1, 0, 0, 0, 1)
    s.int(0), s.int(0), s.int(0), s.int(0)
    s.int(-1)
    s.float(*[1.0] * 12)
    s.int(1), s.int(0), s.int(0)  # one GUI
    s.int(0)
    s.int(2)  # joints
    s.int(1)
    s.float(0.0)
    s.byte(0), s.byte(0), s.byte(0), s.byte(0)
    s.int(0)
    s.float(0.0)
    s.int(0)
    s.hash("models/test/body.md5mesh")
    s.float(*[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0] * 2)
    s.int(34)  # the GUI: id, state, window tree, cursor
    s.int(2)
    s.hash("name"), s.hash("guis/test/screen.gui")
    s.hash("text"), s.hash("hello")
    window(s, 1)
    s.float(320.0, 240.0)
    # a light
    s.render(8)
    s.int(3)
    s.float(1, 0, 0, 0, 1, 0, 0, 0, 1)
    s.float(0.0, 0.0, 64.0)
    s.int(0), s.int(0)
    s.float(0.0)
    s.byte(0), s.byte(0), s.byte(1), s.byte(0)
    s.float(*[0.0] * 21)
    s.int(0), s.int(0), s.int(1)
    s.float(*[1.0] * 12)
    s.int(0)
    s.hash("lights/test")
    # an effect and its live state: one segment holding no particles
    s.render(12)
    s.int(5)
    s.int(1)
    s.int(0), s.int(0), s.int(0)
    s.float(0.0, 0.0, 0.0)
    s.float(1, 0, 0, 0, 1, 0, 0, 0, 1)
    s.float(0.0, 0.0, -1.0)
    s.float(0.0, 0.0, 0.0)
    s.float(1.0)
    s.byte(0), s.byte(1), s.byte(0), s.byte(1)
    s.int(0)
    s.float(0.0)
    s.int(0)
    s.float(*[1.0] * 12)
    s.hash("effects/test/smoke")
    s.int(0)  # effect flags: its once-written block follows
    s.out += bytes(4 * 3 + 16 + 4 * 2 + 12 + 36 + 12 * 3 + 4 + 12 * 3 + 36 * 2 + 24 * 2)
    s.out += bytes(4 + 12 * 2 + 36 + 4 * 3)
    s.int(0)  # the segment
    s.float(0.0)
    s.int(0)
    s.out += bytes(4 + 4 * 2 + 8 * 2 + 4 * 2 + 4 * 2)
    # the view, the HUD, the end of the frame
    s.render(1)
    s.int(1), s.int(0), s.int(0), s.int(640), s.int(480)
    s.float(90.0, 73.74)
    s.float(0.0, 0.0, 0.0)
    s.float(1, 0, 0, 0, 1, 0, 0, 0, 1)
    s.byte(0), s.byte(0)
    s.int(1032)
    s.float(*[0.0] * 12)
    s.int(0)
    s.render(20)
    s.int(4)
    for _ in range(4):
        s.float(0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0)
        s.out += b"\xff\xff\xff\xff"
    s.int(6)
    for index in (0, 1, 2, 0, 2, 3):
        s.int(index)
    s.int(1)
    s.int(1)
    s.float(1.0, 1.0, 1.0, 1.0)
    s.int(0), s.int(4), s.int(0), s.int(6)
    s.hash("gfx/guis/hud/test")
    s.sound(1)
    s.float(0.0, 0.0, 0.0)
    s.float(1, 0, 0, 0, 1, 0, 0, 0, 1)
    s.int(1), s.int(1032)
    s.render(17)
    return bytes(s.out)


def require(text: str, needle: str, context: str):
    if needle not in text:
        raise AssertionError(f"{context}: missing {needle!r}")


def check_decoder() -> None:
    stream = build_stream()
    stats = decoder.decode(stream)
    assert stats["frames"] == 1, stats
    assert stats["trailing_bytes"] == 0, stats
    assert stats["maps"][0]["map"] == "maps/game/test", stats["maps"]
    assert stats["guis"] == ["guis/test/screen.gui"], stats["guis"]
    assert stats["effects"] == ["effects/test/smoke"], stats["effects"]
    assert stats["sound_state"] == {"emitters": 3, "alive": 1, "channels": 1}, stats["sound_state"]
    assert stats["max_joints"] == 2, stats
    # every cut ends in a DemoError or in fewer frames, never another exception
    for cut in range(len(stream)):
        try:
            result = decoder.decode(stream[:cut])
        except decoder.DemoError:
            continue
        assert result["frames"] == 0, f"cut at {cut} decoded a whole frame"
    # corrupted copies: any outcome but an exception escaping the decoder
    rng = random.Random(1834)
    header = len(b"Quake4 RDEMO\x00") + 4
    for _ in range(3000):
        bad = bytearray(stream)
        for _ in range(rng.randint(1, 4)):
            bad[rng.randrange(header, len(bad))] = rng.randrange(256)
        try:
            decoder.decode(bytes(bad))
        except decoder.DemoError:
            pass


def check_engine_wiring() -> None:
    read = lambda relative: (ROOT / relative).read_text(encoding="utf-8", errors="replace")  # noqa: E731
    demo_file = read("src/framework/DemoFile.cpp")
    for needle in ("head[0] == DS_VERSION && head[5] == DS_SOUND && head[6] == 0",
                   "streamVersion >= 1 && streamVersion <= OPENQ4_RENDERDEMO_CURRENT_VERSION",
                   "retailRenderStream = retailStream;",
                   "is not a render demo this build recognizes"):
        require(demo_file, needle, "retail stream detection")
    session = read("src/framework/Session.cpp")
    for needle in ("readDemo->IsRetailRenderStream()",
                   "rw->ProcessRetailDemoCommand( readDemo, &currentDemoRenderView, &retailPortalSkyPending, &demoTimeOffset )",
                   "sw->ProcessRetailDemoCommand( readDemo )",
                   "renderDemoPendingFrameDeltaUS = deltaMS * 1000;",
                   "RF_DEFER_COMMAND_SUBMIT | RF_PORTAL_SKY"):
        require(session, needle, "retail playback dispatch")
    reader = read("src/renderer/RenderWorld_demo_retail.cpp")
    for needle in ("r.Vec3( ent.bounds[1], \"entity bounds\" );\n\tr.Vec3( ent.bounds[0], \"entity bounds\" );",
                   "bse->DemoParticleLayout(", "RetailSkipWindow( r, depth + 1, windows )",
                   "RETAIL_RENDER_ENTITY_BYTES", "a model definition, which retail never writes",
                   "RetailSkipEffectState( r, layout, recordedStart )", "uiManager->FindGui( guiName, true, true )"):
        require(reader.replace("\r\n", "\n"), needle, "retail render reader")
    require(read("src/bse/BSEInterface.h"), "virtual int							DemoParticleLayout(", "BSE layout query")
    require(read("src/framework/Common.cpp"), "virtual int					DemoParticleLayout(", "disabled BSE")
    sound = read("src/sound/snd_world.cpp")
    for needle in ("bool idSoundWorldLocal::ProcessRetailDemoCommand( idDemoFile* readDemo )",
                   "RetailDemoEmitter( i, true )", "slotChannel[slot] = fields[2];"):
        require(sound, needle, "retail sound reader")
    library = read("src/framework/Session_demo.cpp")
    for needle in ("DemoReadRetailRenderMap( probe, retailMap )", 'DemoLocalized( "#str_232870" )',
                   "probe.IsRetailRenderStream()"):
        require(library, needle, "retail library entry")
    for lang in ("brazilian", "czech", "english", "french", "german", "hungarian", "italian", "polish",
                 "russian", "spanish", "turkish", "ukrainian"):
        require(read(f"content/baseoq4/pak0/strings/{lang}_openq4.lang"), '"#str_232870"', f"{lang} strings")


def main() -> int:
    check_decoder()
    check_engine_wiring()
    print("retail render demo contract: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
