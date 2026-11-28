#!/usr/bin/env python3
"""The voice-generation line list stays usable by tools/assets/elevenlabs_voice.py.

Always checked, from the repository alone:
- docs/dev/voice-generation-lines.json is schema 2: every line has its group,
  id, voice, text, tier and notes, and ids are unique within a group;
- each voice profile fits Voice Design's limits (description at most 500
  characters, preview text 100 to 1000) and names its models;
- the Valkaryne group has her 37 lines and 6 optional sound effects, each line
  with a preset the tool processes, a hive flag and a target under openQ4's own
  folders, and the effects have durations the sound-effect API accepts;
- docs/dev/voice-generation-breakdown.md lists the same Valkaryne entries;
- the tool reads its key from ELEVENLABS_API_KEY, never prints it, and knows
  every preset the list uses.

Checked as well when the Awakening content is available (--awakening DIR or
.home/q4xbase): each Valkaryne line's original recording is on disk.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LINES = ROOT / "docs/dev/voice-generation-lines.json"
DOC = ROOT / "docs/dev/voice-generation-breakdown.md"
TOOL = ROOT / "tools/assets/elevenlabs_voice.py"
PACK = ROOT / "content/baseoq4/pak0"
SHADERS = PACK / "sound/openq4_valkaryne.sndshd"
CAPTIONS = PACK / "lipsync/openq4_valkaryne.lipsync"
REPAIRS = ROOT / "src/game/awakening/ContentRepairs.inl"
VALK = "Awakening Valkaryne re-voiced"


def fail(message: str) -> None:
    raise AssertionError(message)


def check_installed_voice(valk: list[dict]) -> None:
    """The installed voice in pak0 and the game's swap (ContentRepairs.inl)."""
    installed = [l for l in valk if l.get("install")]
    shaders: dict[str, list[str]] = {}
    for line in installed:
        shaders.setdefault(line["shader"], []).append(line["target"])
        wav = PACK / line["target"]
        if not wav.is_file():
            fail(f"{wav.relative_to(ROOT)}: not installed (run elevenlabs_voice.py install)")
        with wave.open(str(wav)) as audio:
            if (audio.getframerate(), audio.getnchannels(), audio.getsampwidth()) != (44100, 1, 2):
                fail(f"{wav.name}: the game's voice format is 44.1 kHz, mono, 16-bit")
    declared = SHADERS.read_text(encoding="ascii")
    for shader, targets in shaders.items():
        body = re.search(r"^sound oq4_" + re.escape(shader) + r" \{(.*?)^\}", declared, re.M | re.S)
        if not body or sorted(re.findall(r"sound/\S+\.wav", body.group(1))) != sorted(targets):
            fail(f"{SHADERS.name}: oq4_{shader} does not declare exactly its installed takes")
    captions = CAPTIONS.read_text(encoding="ascii")
    for line in installed:
        if line.get("kind") != "sfx" and f"lipSync subtitle/oq4_{line['shader']}\n" not in captions:
            fail(f"{CAPTIONS.name}: no caption for oq4_{line['shader']}")
    repairs = REPAIRS.read_text(encoding="utf-8")
    if 'g_awakeningValkaryneVoice( "g_awakeningValkaryneVoice", "1", CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL' not in repairs:
        fail(f"{REPAIRS.name}: the voice setting must default to openQ4's voice and be saved")
    entity = repairs[repairs.find("void Awakening_CorrectEntitySounds("):]
    radio = repairs[repairs.find("const char *Awakening_RecordedLine("):]
    # the swap must not depend on the content-repair switch
    if entity.find("Awakening_SwapValkaryneSounds( args );") > entity.find("Awakening_ContentRepairsActive()"):
        fail(f"{REPAIRS.name}: speakers and her keys must swap before the content-repair gate")
    if radio.find("Awakening_ValkaryneSound( sound );") > radio.find("Awakening_ContentRepairsActive()"):
        fail(f"{REPAIRS.name}: radio lines must swap before the content-repair gate")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--awakening", type=Path, help="the expansion's q4xbase folder")
    args = parser.parse_args()

    data = json.loads(LINES.read_text(encoding="utf-8"))
    if data.get("schema") != 2:
        fail(f"{LINES.name}: expected schema 2")
    seen: set[tuple[str, str]] = set()
    for line in data["lines"]:
        for field in ("group", "id", "voice", "text", "tier", "notes"):
            if not isinstance(line.get(field), str):
                fail(f"{LINES.name}: {line.get('id')} has no {field}")
        key = (line["group"], line["id"])
        if key in seen:
            fail(f"{LINES.name}: {line['id']} appears twice in {line['group']}")
        seen.add(key)

    for name, profile in data.get("voices", {}).items():
        if not 1 <= len(profile.get("description", "")) <= 500:
            fail(f"voice {name}: the description must be 1-500 characters (Voice Design)")
        if not 100 <= len(profile.get("preview_text", "")) <= 1000:
            fail(f"voice {name}: the preview text must be 100-1000 characters")
        for field in ("design_model", "tts_model", "sfx_model"):
            if not profile.get(field):
                fail(f"voice {name}: no {field}")

    valk = [l for l in data["lines"] if l["group"] == VALK]
    speech = [l for l in valk if l.get("kind") != "sfx"]
    effects = [l for l in valk if l.get("kind") == "sfx"]
    if len(speech) != 37 or len(effects) != 6:
        fail(f"{VALK}: expected 37 lines and 6 sound effects, found {len(speech)} and {len(effects)}")
    if "valkaryne" not in data.get("voices", {}):
        fail("no valkaryne voice profile")
    for line in speech:
        if line["preset"] not in ("nexus", "monitor") or not isinstance(line.get("hive"), bool):
            fail(f"{line['id']}: preset must be nexus or monitor, with a hive flag")
        # sound/vo_<name>/ is the engine's dialogue path (captions, Dialogue
        # Volume) that its sound/vo/ language remap leaves alone
        if not line["target"].startswith("sound/vo_openq4/valkaryne/"):
            fail(f"{line['id']}: speech must live under sound/vo_openq4/valkaryne/")
        if line.get("shader") != line["id"] or line.get("install") is not True:
            fail(f"{line['id']}: speech replaces its own shader")
        if not line["id"].startswith("vo_valk_") or not line.get("original"):
            fail(f"{line['id']}: not one of her recorded lines")
    for line in effects:
        if not 0.5 <= float(line.get("duration_seconds", 0)) <= 30 or line.get("preset") != "creature":
            fail(f"{line['id']}: a sound effect needs 0.5-30 s and the creature preset")
        if not line["target"].startswith("sound/openq4/valkaryne/"):
            fail(f"{line['id']}: target outside openQ4's own folder")
        if line.get("shader") not in ("valkaryne_idle", "valkaryne_pain", "valkaryne_death"):
            fail(f"{line['id']}: not one of her creature shaders")

    doc = DOC.read_text(encoding="utf-8")
    table = doc[doc.find("#### " + VALK):]
    listed = re.findall(r"^\| `([a-z0-9_]+)` \|", table, re.M)
    if listed != [l["id"] for l in valk]:
        fail(f"{DOC.name}: the {VALK} table does not list the JSON's entries in order")

    tool = TOOL.read_text(encoding="utf-8")
    if 'os.environ.get("ELEVENLABS_API_KEY")' not in tool or re.search(r"print\([^)]*\bkey\b", tool):
        fail(f"{TOOL.name}: the key must come from ELEVENLABS_API_KEY and never be printed")
    for preset in {l["preset"] for l in valk}:
        if preset != "nexus" and f'preset == "{preset}"' not in tool:
            fail(f"{TOOL.name}: no processing for preset {preset}")

    check_installed_voice(valk)

    awakening = args.awakening or ROOT / ".home" / "q4xbase"
    have = (awakening / "sound").is_dir()
    if have:
        for line in speech:
            if not (awakening / line["original"]).is_file():
                fail(f"{line['id']}: original {line['original']} not found")
    print(f"voice_generation_lines: ok ({len(data['lines'])} lines, {len(data.get('voices', {}))} voice profile, "
          f"content {'checked' if have else 'not available'})")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f"voice_generation_lines: FAILED: {error}")
        sys.exit(1)
