#!/usr/bin/env python3
"""The Awakening's m01 dropship scene, revisions 4-6 (docs/dev/m01-interior-feedback-audit.md).

- Fresh m01 starts compile scripts/openq4/awakening/m01_dropship-v6.script and
  redirect the hangar, cabin and evacuation relays to it, only when all three
  of the map's anchors match. Revisions 1-5 stay in pak0 unchanged in name,
  because suspended save threads keep their bytecode.
- The seven marines who board along the rounded hangar lane get a short walk
  range and keep full AI thinks (a simple think steers at a stale goal);
  rvAIManager::IsSimpleThink honours "noSimpleThink".
- The seat nameplates are openQ4's drawings: every skin entry maps one of
  Quake 4's eight plate materials to a declared openQ4 material whose image is
  in pak0, and the generator reproduces the committed images.
- The scene places the expansion's harness model in the six surviving seats of
  both damaged cabin sets, and every rappel goes through the rope approach.
- The landing zone's marines show as they reach the ground, not at touchdown,
  and the Atlas fire team animates during the cinematic.
- Markswell's entrance, briefing and sit hold their last frames instead of
  returning to his armed idle, and the sit is unclipped so it reaches his seat.
"""

from __future__ import annotations

import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAK0 = ROOT / "content" / "baseoq4" / "pak0"
SCRIPT_DIR = PAK0 / "scripts" / "openq4" / "awakening"
V6 = SCRIPT_DIR / "m01_dropship-v6.script"
CINEMATIC = ROOT / "src" / "game" / "awakening" / "M01Cinematic.inl"
MANAGER = ROOT / "src" / "game" / "ai" / "AI_Manager.cpp"
SKINS = PAK0 / "skins" / "openq4_awakening_m01.skin"
MATERIALS = PAK0 / "materials" / "awakening_nameplates_openq4.mtr"
PLATES = PAK0 / "models" / "openq4" / "awakening" / "dropship"
GENERATOR = ROOT / "tools" / "assets" / "awakening_nameplates.py"
SQUAD = ["tak", "darnell", "navajas", "dekard", "townsend", "atkins", "mitchell", "markswell"]
RETAIL = ["bidwell", "rhodes", "cortez", "kane", "anderson", "morris", "sledge", "strauss"]
LANE = ["ds_dekard", "ds_navajas", "ds_tak", "ds_darnell", "ds_atkins", "ds_mitchell", "ds_townsend"]

failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    script = V6.read_text(encoding="ascii")
    cinematic = CINEMATIC.read_text(encoding="utf-8")

    # Revision selection and immutability of the older programs.
    check('"scripts/openq4/awakening/m01_dropship-v6.script"' in cinematic, "M01Cinematic does not select revision 6")
    for older in ["m01_dropship.script", "m01_dropship-v2.script", "m01_dropship-v3.script", "m01_dropship-v4.script",
                  "m01_dropship-v5.script"]:
        check((SCRIPT_DIR / older).is_file(), f"older revision {older} must stay in pak0 for saves")
    for relay, call in [("intro_unit1_seq_2_trigger_seq_3", "oq4_m01::hangar"),
                        ("intro_unit1_seq_5_trigger_seq_6", "oq4_m01::cabin"),
                        ("m04_trench_unit1_main", "oq4_m01::evacuate")]:
        check(f'"{relay}"' in cinematic and f'"{call}"' in cinematic, f"relay {relay} is not directed to {call}")
        check(f"void {call.split('::')[1]}()" in script, f"{call} is missing from revision 6")
    check("!hangarAnchor || !cabinAnchor || !evacuationAnchor" in cinematic, "a missing anchor must keep the original callbacks")

    # Lane marines: walk range and full thinks, scoped to them.
    for name in LANE:
        check(f'"{name}"' in cinematic, f"{name} is not a lane marine")
        check(f"boardLane(${name}," in script, f"{name} does not board along the lane")
    check('args.Set( "walkRange", "24" )' in cinematic and 'args.Set( "noSimpleThink", "1" )' in cinematic,
          "lane marines need a short walk range and full thinks")
    check('spawnArgs.GetBool( "noSimpleThink" )' in MANAGER.read_text(encoding="utf-8"),
          "rvAIManager::IsSimpleThink must honour noSimpleThink")
    check("thread m04_stranarus_trench::moveMarkswell();" in script, "Markswell keeps his authored route")

    # Calm flight before the first hit; turbulence from it.
    cabin = script[script.index("void cabin()"):]
    check('setScenePhase(0, "briefing", 0);' in cabin, "no camera sway before the first hit")
    check(cabin.index("calmSquad();") < cabin.index("sceneImpact(1,") < cabin.index("shakeSquad();"),
          "the squad must sit calmly until the first hit")

    # Harnesses in both damaged sets, lifted with each marine.
    check('seatHarnesses($intro_unit1_seq_6_drop_ship_damaged, "oq4_m01_harness_b");' in script,
          "breach set has no harnesses")
    check('seatHarnesses($intro_unit1_seq_6_drop_ship_damaged_1, "oq4_m01_harness_e");' in script,
          "evacuation set has no harnesses")
    check(script.count("seatHarness(ship, prefix +") == 6, "six surviving seats need harnesses")
    for seat, who in [("e1", "Tak"), ("e2", "darnell"), ("e3", "navajas"), ("e4", "Dekard"), ("e7", "mitchell"), ("e8", "markswell")]:
        check(f'"oq4_m01_harness_{seat}"' in script, f"{who}'s harness never lifts")

    # Every rappel goes through the rope approach.
    check("markRope();" in script, "the rope mark is never computed")
    check(len(re.findall(r'aiScriptedAnimWait\([^,]+, "st_repelling"', script)) == 1, "a rappel bypasses descend()")
    check(not re.search(r"descend\([^)]*,", script), "descend() no longer takes a stride count")

    # The landing zone is peopled during the descent.
    check("thread descendThen($m04_trench_unit1_seq_1_darnell);" in script, "Darnell's arrival does not show the LZ")
    check("descend($m04_trench_unit1_seq_1_tak);\n    takLands();" in script, "ground Tak must show after his rappel")
    landing = script[script.index("void landing() {"):script.index("void evacuate()")]
    check("sys.trigger($tak);" not in landing and "sys.trigger($darnell);" not in landing,
          "landing() must not trigger the LZ marines a second time")
    check('"m04_trench_unit1_seq_2_atlas_guy_", 33' in cinematic and 'args.Set( "cinematic", "1" )' in cinematic,
          "the Atlas fire team must animate during the cinematic")

    # Markswell's performance: one take split into three clips.
    for clip in ["st_markswell_enters", "st_markswell_talks", "st_markswell_sits"]:
        check(re.search(r'aiScriptedAnim(?:Wait)?\(\$intro_unit1_seq_6_markswell, "' + clip + r'", 8, 0\);', script) is not None,
              f"{clip} must hold its last frame, not end in the armed idle")
    sit = script[script.index("void cabinSit() {"):script.index("void cabin() {")]
    check(sit.index("$intro_unit1_seq_6_markswell.disableClip();") < sit.index('"st_markswell_sits"'),
          "the seat and hidden stand-in stop an unclipped sit short of the seat")
    cabin = script[script.index("void cabin() {"):script.index("void prepareMarine(")]
    check("float acknowledged = thread sceneRadio(\"vo_vansen_rogerthtp\");" in cabin
          and cabin.index("sys.waitForThread(acknowledged);") < cabin.index('"st_markswell_talks"'),
          "the briefing must start as Vansen's acknowledgement ends")

    # Skins, materials and plate images.
    materials = MATERIALS.read_text(encoding="ascii")
    skins = SKINS.read_text(encoding="ascii")
    for skin in ["m01_dropship_cabin", "m01_dropship_damaged"]:
        check(f'setSkin("skins/openq4/awakening/{skin}")' in script, f"{skin} is never applied")
        body = re.search(rf"skin skins/openq4/awakening/{skin}\s*\{{(.*?)\}}", skins, re.S)
        check(body is not None, f"skin {skin} is not declared")
        if body is None:
            continue
        pairs = dict(line.split() for line in body.group(1).strip().splitlines())
        check(sorted(k.rsplit("_", 1)[1] for k in pairs) == sorted(RETAIL), f"{skin} must remap all eight plates")
        check(sorted(v.rsplit("_", 1)[1] for v in pairs.values()) == sorted(SQUAD), f"{skin} must name the whole squad once")
        for material in pairs.values():
            check(re.search(rf"^{re.escape(material)}\s*$", materials, re.M) is not None, f"{material} is not declared")
    for name in SQUAD:
        image = PLATES / f"nameplate_{name}.tga"
        check(f"map\t\tmodels/openq4/awakening/dropship/nameplate_{name}.tga" in materials, f"{name}'s material has no image")
        data = image.read_bytes() if image.is_file() else b""
        # Uncompressed true-colour, 256x64, 32 bits; Pillow appends a TGA 2.0 footer.
        check(len(data) in (18 + 256 * 64 * 4, 18 + 256 * 64 * 4 + 26) and data[2] == 2 and
              data[12:16] == bytes([0, 1, 64, 0]) and data[16] == 32, f"{image.name} is not a 256x64 RGBA TGA")

    try:
        import PIL  # noqa: F401
    except ImportError:
        print("awakening_m01_dropship_contract: Pillow unavailable, generator reproduction skipped")
    else:
        with tempfile.TemporaryDirectory() as out:
            subprocess.run([sys.executable, str(GENERATOR), "--out", out], check=True)
            for name in SQUAD:
                fresh = hashlib.sha256((Path(out) / f"nameplate_{name}.tga").read_bytes()).hexdigest()
                committed = hashlib.sha256((PLATES / f"nameplate_{name}.tga").read_bytes()).hexdigest()
                check(fresh == committed, f"generator does not reproduce nameplate_{name}.tga")

    if failures:
        for failure in failures:
            print("FAIL:", failure)
        return 1
    print("awakening_m01_dropship_contract: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
