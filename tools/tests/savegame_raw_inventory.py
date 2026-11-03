#!/usr/bin/env python3
"""Lock the savegame's native-layout blocks (roadmap Q4-06 stage 1).

A save is portable only if every saved byte has an explicit width and byte order.
Typed writers (WriteInt, WriteFloat, WriteVec3, ...) already swap to little-endian;
what remains are calls that copy memory as the compiler laid it out. This test finds
every such call in both game trees and the GUI save code and requires each one to be
listed below with a class:

  bitfield        flag struct whose bit allocation is up to the compiler
  aggregate       struct or template with padding, bool/enum members or a layout
                  tied to the code (it has changed before)
  le-scalars      fixed-width ints and floats (arrays, vectors, rectangles) whose
                  bytes match on every 64-bit little-endian target
  script-storage  script values in host byte order; pointer-sized slots included
  bytes           strings and byte payloads with an explicit length
  legacy-reader   reads that serve only the v2/v3 historical decoders
  helper          the GUI byte/field helper definitions themselves

A new, removed or recounted raw call fails until this table is updated, so a change
to the save format is always reviewed. The SP and MP trees must stay identical.
"""
from __future__ import annotations

import collections
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

RAW = re.compile(r"\b(?:savefile|saveFile)\s*->\s*(?:Write|Read)\s*\(")
HELPER = re.compile(r"\bOpenQ4_(?:Write|Read)SaveGame(?:Field|Bytes)\s*\(")
# Script globals are copied one byte at a time from the program's variable block.
SCRIPT_BYTES = re.compile(r"\bsavefile\s*->\s*(?:Write|Read)Byte\s*\(\s*variables\s*\[")

CLASSES = {"bitfield", "aggregate", "le-scalars", "script-storage", "bytes", "legacy-reader", "helper"}


def W(text: str) -> str:
    return "savefile->Write(" + text + ")"


def R(text: str) -> str:
    return "savefile->Read(" + text + ")"


def flag(name: str, receiver: str = "savefile") -> list[str]:
    return [f"{receiver}->Write(&{name},sizeof({name}))", f"{receiver}->Read(&{name},sizeof({name}))"]


# Relative path -> class -> calls. A call listed twice must occur twice.
GAME: dict[str, dict[str, list[str]]] = {
    "BrittleFracture.cpp": {"bitfield": flag("fl")},
    "SecurityCamera.cpp": {"aggregate": [W("&trm,sizeof(trm)"), R("&trm,sizeof(trm)")]},
    "ai/Monster_ConvoyHover.cpp": {
        "bitfield": flag("myfl"),
        "le-scalars": [W("hoverDampening,sizeof(idAngles)*DAMPEN_ANGLE_SAMPLES"),
                       R("hoverDampening,sizeof(idAngles)*DAMPEN_ANGLE_SAMPLES")],
    },
    "anim/Anim_Blend.cpp": {
        "aggregate": [W("jointMods[i],sizeof(*jointMods[i])"), R("&jointMod,sizeof(jointMod)")],
        "le-scalars": [
            W("joints,numJoints*sizeof(joints[0])"), R("joints,numJoints*sizeof(joints[0])"),
            W('AFPoseJoints.Ptr(),idLib::SizeToInt(AFPoseJoints.MemoryUsed(),"idAnimator::Save")'),
            R('AFPoseJoints.Ptr(),idLib::SizeToInt(AFPoseJoints.MemoryUsed(),"idAnimator::Restore")'),
            W('AFPoseJointMods.Ptr(),idLib::SizeToInt(AFPoseJointMods.MemoryUsed(),"idAnimator::Save")'),
            R('AFPoseJointMods.Ptr(),idLib::SizeToInt(AFPoseJointMods.MemoryUsed(),"idAnimator::Restore")'),
            W("AFPoseJointFrame,AFPoseJointFrameSize*sizeof(AFPoseJointFrame[0])"),
            R("AFPoseJointFrame,AFPoseJointFrameSize*sizeof(AFPoseJointFrame[0])"),
        ],
        "legacy-reader": [R("&saved,sizeof(saved)")],
    },
    "physics/Clip.cpp": {"aggregate": [W("&entry->trm,sizeof(entry->trm)"), R("&entry->trm,sizeof(entry->trm)")]},
    # The constraint and body flag structs share the expression `fl`.
    "physics/Physics_AF.cpp": {"bitfield": flag("fl", "saveFile") * 2},
    "script/Script_Interpreter.cpp": {"script-storage": [W("&localstack,localstackUsed"), R("localstack,localstackUsed")]},
    "script/Script_Program.cpp": {
        "script-storage": [W("data,savedSize"), R("data,savedSize"),
                           "savefile->WriteByte(variables[", "savefile->WriteByte(variables[",
                           "savefile->ReadByte(variables[", "savefile->ReadByte(variables["],
    },
    "vehicle/Vehicle.cpp": {"bitfield": flag("vfl")},
    "vehicle/VehicleDriver.cpp": {"aggregate": flag("pathTargetInfo") + flag("lastPathTargetInfo")},
    "vehicle/VehicleParts.cpp": {"bitfield": flag("fl")},
    "vehicle/VehiclePosition.cpp": {
        "bitfield": [W("&fl,sizeof(fl)"), R("&fl,sizeof(fl)")],
        "le-scalars": [f"savefile->{op}({name},sizeof{name})"
                       for name in ("mEyeJointAxisMap", "mEyeJointDirMap", "mDriverJointAxisMap", "mDriverJointDirMap")
                       for op in ("Write", "Read")],
    },
    # Historical decoders only; the current writer saves these through typed fields.
    "Entity.cpp": {"legacy-reader": [R("&fl,sizeof(fl)")]},
    "Mover.cpp": {"legacy-reader": [R("&move,sizeof(move)"), R("&rot,sizeof(rot)")]},
    "Player.cpp": {"legacy-reader": [R("&pfl,sizeof(pfl)")]},
    "Projectile.cpp": {"legacy-reader": [R("&projectileFlags,sizeof(projectileFlags)")]},
    "Weapon.cpp": {"legacy-reader": [R("&wfl,sizeof(wfl)"), R("&wsfl,sizeof(wsfl)")]},
    "ai/AI.cpp": {"legacy-reader": [R("&aifl,sizeof(aifl)"), R("&combat.fl,sizeof(combat.fl)"),
                                    R("&enemy.fl,sizeof(enemy.fl)"), R("&passive.fl,sizeof(passive.fl)")]},
    "ai/AI_Actions.cpp": {"legacy-reader": [R("&fl,sizeof(fl)")]},
    "ai/AI_Move.cpp": {"legacy-reader": [R("&fl,sizeof(fl)")]},
    "gamesys/Event.cpp": {"legacy-reader": [R("legacyData,argsize")]},
    "gamesys/State.cpp": {"legacy-reader": ["saveFile->Read(&fl,sizeof(fl))"]},
    "physics/Physics_RigidBody.cpp": {"legacy-reader": [R("&state.pushVelocity,sizeof(state.pushVelocity)")]},
    "IK.cpp": {"legacy-reader": [R(f"{name},sizeof({name})") for name in (
        "&waistJoint", "ankleJoints", "dirJoints", "dirJoints", "elbowForward", "elbowJoints", "footJoints",
        "handJoints", "hipForward", "hipJoints", "kneeForward", "kneeJoints", "lowerArmLength",
        "lowerArmToElbowJoint", "lowerLegLength", "lowerLegToKneeJoint", "oldAnkleHeights", "shoulderForward",
        "shoulderJoints", "upperArmLength", "upperArmToShoulderJoint", "upperLegLength", "upperLegToHipJoint")]},
}
# `sizeof(&waistJoint)` above is written `sizeof(waistJoint)` in the source.
GAME["IK.cpp"]["legacy-reader"][0] = R("&waistJoint,sizeof(waistJoint)")

# Direct raw writes per game tree. Q4-06 stage 2 drives this to zero.
GAME_DIRECT_WRITES = 23


def field(op: str, name: str, context: str, label: str) -> str:
    return f'OpenQ4_{op}SaveGameField(savefile,{name},"{context}","{label}")'


def window_fields(context_write: str, context_read: str, names: list[tuple[str, str, str]]) -> list[str]:
    calls = []
    for written, read, label in names:
        calls.append(field("Write", written, context_write, label))
        calls.append(field("Read", read, context_read, label))
    return calls


WINDOW_SCALARS = [
    ("actualX", "savedActualX", "actualX"), ("actualY", "savedActualY", "actualY"),
    ("borderSize", "savedBorderSize", "bordersize"), ("childID", "savedChildID", "childID"),
    ("clientRect", "savedClientRect", "clientrect"), ("cursor", "savedCursor", "cursor"),
    ("drawRect", "savedDrawRect", "drawrect"), ("flags", "savedFlags", "flags"),
    ("fontNum", "savedFontNum", "fontnumber"), ("forceAspectHeight", "savedForceAspectHeight", "forceaspectheight"),
    ("forceAspectWidth", "savedForceAspectWidth", "forceaspectwidth"), ("lastTimeRun", "savedLastTimeRun", "lasttimerun"),
    ("matScalex", "savedMatScaleX", "materialscalex"), ("matScaley", "savedMatScaleY", "materialscaley"),
    ("origin", "savedOrigin", "origin"), ("savedTextSpacing", "savedTextSpacing", "textspacing"),
    ("savedTextStyle", "savedTextStyle", "textstyle"), ("shear", "savedShear", "shear"),
    ("textAlign", "savedTextAlign", "textalign"), ("textAlignx", "savedTextAlignX", "textalignx"),
    ("textAligny", "savedTextAlignY", "textaligny"), ("textRect", "savedTextRect", "textrect"),
    ("textShadow", "savedTextShadow", "textshadow"), ("timeLine", "savedTimeLine", "timeline"),
    ("xOffset", "savedXOffset", "xoffset"), ("yOffset", "savedYOffset", "yoffset"),
]
SIMPLE_SCALARS = [
    ("borderSize", "savedBorderSize", "bordersize"), ("clientRect", "savedClientRect", "clientrect"),
    ("drawRect", "savedDrawRect", "drawrect"), ("flags", "savedFlags", "flags"),
    ("fontNum", "savedFontNum", "fontnumber"), ("matScalex", "savedMatScaleX", "materialscalex"),
    ("matScaley", "savedMatScaleY", "materialscaley"), ("origin", "savedOrigin", "origin"),
    ("textAlign", "savedTextAlign", "textalign"), ("textAlignx", "savedTextAlignX", "textalignx"),
    ("textAligny", "savedTextAlignY", "textaligny"), ("textRect", "savedTextRect", "textrect"),
    ("textShadow", "savedTextShadow", "textshadow"), ("textSpacing", "savedTextSpacing", "textspacing"),
    ("textStyle", "savedTextStyle", "textstyle"),
]


def minigame(names: list[str], extra: list[str] | None = None) -> list[str]:
    return [W(f"&{name},sizeof({name})") for name in names] + (extra or [])


UI: dict[str, dict[str, list[str]]] = {
    "Window.cpp": {
        "le-scalars": window_fields("idWindow::WriteToSaveGame", "idWindow::ReadFromSaveGame", WINDOW_SCALARS),
        "aggregate": [field("Write", "trans.interp", "idWindow::WriteSaveGameTransition", "interpolatestate"),
                      field("Read", "interp", "idWindow::ReadSaveGameTransition", "interpolatestate"),
                      field("Read", "saved", "idWindow::ReadSaveGameTransition", "interpolatestate")],
        "bytes": ['OpenQ4_WriteSaveGameBytes(savefile,string,len,"idWindow::WriteSaveGameString","string")',
                  'OpenQ4_ReadSaveGameBytes(savefile,&string[0],len,"idWindow::ReadSaveGameString","string")'],
    },
    "SimpleWindow.cpp": {
        "le-scalars": window_fields("idSimpleWindow::WriteToSaveGame", "idSimpleWindow::ReadFromSaveGame", SIMPLE_SCALARS),
        "bytes": ['OpenQ4_WriteSaveGameBytes(savefile,background->GetName(),stringLen,"idSimpleWindow::WriteToSaveGame","backgroundname")',
                  'OpenQ4_ReadSaveGameBytes(savefile,&(backName)[0],stringLen,"idSimpleWindow::ReadFromSaveGame","backgroundname")'],
    },
    "Winvar.h": {
        "helper": ["OpenQ4_WriteSaveGameBytes(idFile*savefile,constvoid*buffer,intlen,constchar*context,constchar*fieldName)",
                   "OpenQ4_ReadSaveGameBytes(idFile*savefile,void*buffer,intlen,constchar*context,constchar*fieldName)",
                   "OpenQ4_WriteSaveGameField(idFile*savefile,consttype&value,constchar*context,constchar*fieldName)",
                   "OpenQ4_ReadSaveGameField(idFile*savefile,type&value,constchar*context,constchar*fieldName)",
                   "OpenQ4_WriteSaveGameBytes(savefile,&value,sizeof(value),context,fieldName)",
                   "OpenQ4_ReadSaveGameBytes(savefile,&value,sizeof(value),context,fieldName)",
                   W("buffer,len"), R("buffer,len")],
        "bytes": ['OpenQ4_WriteSaveGameBytes(savefile,data.c_str(),len,"idWinStr::WriteToSaveGame","string")',
                  'OpenQ4_ReadSaveGameBytes(savefile,&data[0],len,"idWinStr::ReadFromSaveGame","string")',
                  'OpenQ4_WriteSaveGameBytes(savefile,data.c_str(),len,"idWinBackground::WriteToSaveGame","materialname")',
                  'OpenQ4_ReadSaveGameBytes(savefile,&data[0],len,"idWinBackground::ReadFromSaveGame","materialname")'],
    },
    "UserInterface.cpp": {"bytes": [W("string.c_str(),len"), R("buffer,len")]},
    "RegExp.cpp": {"bytes": [W("buffer,len"), R("buffer,len")]},
    # Doom 3 PDA minigames: every field is a float, int, bool or float vector.
    "GameBearShootWindow.cpp": {"le-scalars": minigame(
        ["width", "height", "visible", "entColor", "position", "rotation", "rotationSpeed", "velocity", "fadeIn",
         "fadeOut", "timeSlice", "timeRemaining", "gameOver", "updateScore", "bearHitTarget", "bearScale",
         "bearIsShrinking", "turretAngle", "turretForce", "windForce"],
        [R("&value,sizeof(value)"),
         'OpenQ4_ReadSaveGameBytes(savefile,data,size,"idGameBearShootWindow::ReadFromSaveGame",fieldName)'])},
    "GameBustOutWindow.cpp": {"le-scalars": minigame(
        ["visible", "width", "height", "color", "position", "velocity", "removed", "fadeOut", "x", "y", "width",
         "height", "isBroken", "timeSlice", "gameOver", "updateScore", "paddleVelocity", "ballSpeed",
         "ballHitCeiling"],
        [R("&value,sizeof(value)"),
         'OpenQ4_ReadSaveGameBytes(savefile,data,size,"idGameBustOutWindow::ReadFromSaveGame",fieldName)'])},
    "GameSSDWindow.cpp": {"le-scalars": minigame(
        ["beginColor", "beginPosition", "crosshairHeight", "crosshairWidth", "destroyed", "dir", "endColor",
         "endPosition", "endPosition", "finalSize", "followBuddy", "foreColor", "hitRadius", "inUse", "killBuddy",
         "matColor", "noHit", "noPlayerDamage", "position", "radius", "rotation", "rotationSpeed", "screenBounds",
         "size", "speed", "speed", "textScale"],
        [R("&value,sizeof(value)")] * 3 + [R("data,size")] * 2)},
}


def calls_in(text: str) -> collections.Counter:
    found: collections.Counter = collections.Counter()
    for pattern in (RAW, HELPER):
        for match in pattern.finditer(text):
            depth, index = 0, text.index("(", match.start())
            while True:
                depth += text[index] == "("
                depth -= text[index] == ")"
                index += 1
                if depth == 0:
                    break
            found[re.sub(r"\s+", "", text[match.start():index])] += 1
    for match in SCRIPT_BYTES.finditer(text):
        found[re.sub(r"\s+", "", match.group(0))] += 1
    return found


def scan(tree: Path) -> dict[str, collections.Counter]:
    result = {}
    for path in sorted(tree.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        relative = path.relative_to(tree).as_posix()
        found = calls_in(path.read_text(encoding="latin-1"))
        if found:
            result[relative] = found
    return result


def expected(manifest: dict[str, dict[str, list[str]]]) -> dict[str, collections.Counter]:
    result = {}
    for relative, classes in manifest.items():
        unknown = set(classes) - CLASSES
        assert not unknown, f"{relative}: unknown class {sorted(unknown)}"
        result[relative] = collections.Counter(call for calls in classes.values() for call in calls)
    return result


def compare(label: str, found: dict[str, collections.Counter], manifest: dict[str, dict[str, list[str]]]) -> list[str]:
    problems = []
    want = expected(manifest)
    for relative in sorted(set(found) | set(want)):
        have, need = found.get(relative, collections.Counter()), want.get(relative, collections.Counter())
        for call in sorted(set(have) | set(need)):
            if have[call] != need[call]:
                kind = "unclassified" if need[call] == 0 else "stale" if have[call] == 0 else "count changed"
                problems.append(f"{label}/{relative}: {kind} raw save call ({have[call]} found, {need[call]} listed): {call}")
    return problems


def summary(manifest: dict[str, dict[str, list[str]]]) -> dict[str, int]:
    totals: collections.Counter = collections.Counter()
    for classes in manifest.values():
        for name, calls in classes.items():
            totals[name] += len(calls)
    return dict(sorted(totals.items()))


def validate_script_slot_widths() -> None:
    # Script storage is saved raw. Its pointer-sized types are the only slots whose
    # width follows the target; the Q4-06 contract relies on them staying intptr_t
    # (8 bytes on every supported target) and on no other type changing width.
    source = (ROOT / "src/game/script/Script_Program.cpp").read_text(encoding="latin-1")
    for name, size in (("scriptevent", "sizeof(intptr_t)"), ("namespace", "sizeof(intptr_t)"),
                       ("string", "MAX_STRING_LEN"), ("float", "sizeof( float )"), ("vector", "sizeof( idVec3 )"),
                       ("entity", "sizeof( int )"), ("field", "sizeof( int )"), ("function", "sizeof(intptr_t)"),
                       ("virtual function", "sizeof( int )"), ("pointer", "sizeof(intptr_t)"),
                       ("object", "sizeof( int )"), ("boolean", "sizeof( int )")):
        if not re.search(rf'"{re.escape(name)}",\s*{re.escape(size)},', source):
            raise AssertionError(f"script type '{name}' no longer has size {size}; reclassify script storage")


def self_test() -> None:
    sample = "void f(idSaveGame *savefile){ savefile->Write( &newBlock, sizeof( newBlock ) ); savefile->WriteInt( x ); }"
    found = calls_in(sample)
    assert found == collections.Counter({W("&newBlock,sizeof(newBlock)"): 1}), found
    problems = compare("sample", {"New.cpp": found}, {})
    assert problems and "unclassified" in problems[0], problems


def main() -> None:
    self_test()
    validate_script_slot_widths()
    problems = []
    for tree in ("game", "mpgame"):
        problems += compare(f"src/{tree}", scan(ROOT / "src" / tree), GAME)
    problems += compare("src/ui", scan(ROOT / "src" / "ui"), UI)
    writes = sum(1 for classes in GAME.values() for name, calls in classes.items() if name != "legacy-reader"
                 for call in calls if "->Write(" in call)
    if writes != GAME_DIRECT_WRITES:
        problems.append(f"game trees: {writes} direct raw writes listed, expected {GAME_DIRECT_WRITES}")
    if problems:
        raise AssertionError("savegame raw inventory changed:\n  " + "\n  ".join(problems))
    print(f"savegame_raw_inventory: ok (game per tree {summary(GAME)}, {GAME_DIRECT_WRITES} direct writes; "
          f"ui {summary(UI)})")


if __name__ == "__main__":
    main()
