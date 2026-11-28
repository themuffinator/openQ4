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
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LINES = ROOT / "docs/dev/voice-generation-lines.json"
DOC = ROOT / "docs/dev/voice-generation-breakdown.md"
TOOL = ROOT / "tools/assets/elevenlabs_voice.py"
VALK = "Awakening Valkaryne re-voiced"


def fail(message: str) -> None:
    raise AssertionError(message)


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
        if not line["target"].startswith("sound/vo_english/openq4_valkaryne/"):
            fail(f"{line['id']}: target outside openQ4's own folder")
        if not line["id"].startswith("vo_valk_") or not line.get("original"):
            fail(f"{line['id']}: not one of her recorded lines")
    for line in effects:
        if not 0.5 <= float(line.get("duration_seconds", 0)) <= 30 or line.get("preset") != "creature":
            fail(f"{line['id']}: a sound effect needs 0.5-30 s and the creature preset")
        if not line["target"].startswith("sound/strogg/openq4_valkaryne/"):
            fail(f"{line['id']}: target outside openQ4's own folder")

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
