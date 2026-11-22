#!/usr/bin/env python3
"""Pin openQ4's volume settings to retail Quake 4 1.4.2 (docs/dev/audio-volume-parity.md).

Retail's values were read from the static idCVar objects in Quake4.exe 1.4.2 and its
stock mainmenu.gui. This check keeps the master volume unbounded with the stock 0 to 2
slider in both menus, the music volume on retail's music, and the dead BFG s_volume_dB
CVar out of the engine.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace")


def cvar(text: str, name: str) -> str:
    match = re.search(r'idCVar\s+\w+\(\s*"' + re.escape(name) + r'"[^;]*;', text)
    return match.group(0) if match else ""


def gui_block(text: str, name: str) -> str:
    at = text.find(name)
    if at < 0:
        return ""
    start = text.index("{", at)
    return text[start:text.index("}", start)]


def main() -> int:
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    system = read("src/sound/snd_system.cpp")
    world = read("src/sound/snd_world.cpp")
    shader = read("src/sound/snd_shader.cpp")
    emitter = read("src/sound/snd_emitter.cpp")
    session = read("src/framework/Session_menu.cpp")
    retained = read("tools/ui/retained_audio.py")
    stock = read("content/baseoq4/pak0/guis/menu/settings/audio.gui")

    volume = cvar(system, "s_volume")
    require('"0.5"' in volume and "CVAR_ARCHIVE" in volume and "CVAR_FLOAT" in volume,
            "s_volume keeps retail's archived float default 0.5")
    require(not re.search(r'"\s*,\s*[-\d.]+f?\s*,\s*[-\d.]+f?\s*\)\s*;$', volume),
            "s_volume stays unbounded, as retail's is")
    music = cvar(system, "s_musicVolume")
    require('"0.5"' in music and "0.0f, 1.0f" in music, "s_musicVolume keeps retail's 0.5 in 0 to 1")
    require("CVAR_ARCHIVE" in cvar(world, "s_doorDistanceAdd") and '"150"' in cvar(world, "s_doorDistanceAdd"),
            "s_doorDistanceAdd keeps retail's archived 150")
    require('"s_volume_dB"' not in system + world + emitter,
            "the BFG s_volume_dB CVar, which nothing read, stays removed")

    slider = gui_block(stock, "sliderDef set_audio_vol_slider")
    require("low\t0" in slider and "high\t2" in slider and "step\t0.1" in slider,
            "the stock-style Sound Volume slider runs 0 to 2 in 0.1 steps, as mainmenu.gui does")
    require('"audioVolume", "audio.volume", (0, 200, 10)' in retained,
            "the retained Volume row runs 0 to 200%")
    require('SetCVarFloat( "s_volume", idMath::ClampInt( 0, 200, value ) / 100.0f )' in session,
            "the retained Volume request accepts up to 200%")
    for document in ("audio.rui", "audio_awakening.rui"):
        text = read(f"content/baseoq4/pak0/guis/menu/settings/{document}")
        at = text.find('"action": "audioVolume"')
        window = text[at:at + 600] if at >= 0 else ""
        require('"maximum": 200' in window, f"{document} is regenerated with the 0 to 200% Volume row")

    rule = shader[shader.find("static bool SND_IsMusicSamplePath"):]
    rule = rule[:rule.find("}") + 1]
    require('"sound/musical/"' in rule and '"sound/music/"' in rule,
            "score samples (and the Awakening's) follow the music volume")
    require("sound/ambience/musical/" not in rule.split("return", 1)[-1],
            "retail's ambient drones under sound/ambience/musical/ stay ordinary speakers")
    require("parms.soundClass == SOUND_CLASS_MUSICAL" in emitter,
            "sound class 3 follows the music volume, as retail's class fade does")
    require("s_volume.GetFloat()" in emitter, "the master volume scales every channel")

    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print("sound_volume_retail_contract: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
