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

Blocks already converted to explicit fields leave the table. Stage 2 converted
those whose bytes stay the same. Stage 3 converted the rest under payload version
5, so their raw reads remain only as legacy readers for versions 2 to 4. The flag
bitfields keep their old bytes only while each saved flag list follows its
struct's declaration order, so FLAG_LISTS checks that order as well.
"""
from __future__ import annotations

import collections
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

RAW = re.compile(r"\b(?:savefile|saveFile)\s*->\s*(?:Write|Read)\s*\(")
HELPER = re.compile(r"\bOpenQ4_(?:Write|Read)SaveGame(?:RawField|Bytes)\s*\(")
# Script globals are copied one byte at a time from the program's variable block.
SCRIPT_BYTES = re.compile(r"\bsavefile\s*->\s*(?:Write|Read)Byte\s*\(\s*variables\s*\[")

CLASSES = {"bitfield", "aggregate", "le-scalars", "script-storage", "bytes", "legacy-reader", "helper"}


def W(text: str) -> str:
    return "savefile->Write(" + text + ")"


def R(text: str) -> str:
    return "savefile->Read(" + text + ")"


# Relative path -> class -> calls. A call listed twice must occur twice.
GAME: dict[str, dict[str, list[str]]] = {
    # The flag bitfields and LE-scalar blocks were converted to explicit fields
    # with unchanged bytes (Q4-06 stage 2); FLAG_LISTS below pins their order.
    # Version 5 writes trace models, joint modifiers and path targets through
    # SaveCodec; these reads decode the raw structures of versions 2 to 4.
    "SecurityCamera.cpp": {"legacy-reader": [R("&trm,sizeof(trm)")]},
    "anim/Anim_Blend.cpp": {"legacy-reader": [R("&jointMod,sizeof(jointMod)"), R("&saved,sizeof(saved)")]},
    "physics/Clip.cpp": {"legacy-reader": [R("&entry->trm,sizeof(entry->trm)")]},
    "vehicle/VehicleDriver.cpp": {"legacy-reader": [R("&pathTargetInfo,sizeof(pathTargetInfo)"),
                                                    R("&lastPathTargetInfo,sizeof(lastPathTargetInfo)")]},
    # Script values are little-endian ints and floats, and its pointer-sized
    # slots are 8 bytes on every portable build (validate_script_slot_widths).
    # Written from a copy whose address temporaries are zeroed (ZeroPointerLocals).
    "script/Script_Interpreter.cpp": {"script-storage": [W("savedStack,localstackUsed"), R("localstack,localstackUsed")]},
    "script/Script_Program.cpp": {
        "script-storage": [W("data,savedSize"), R("data,savedSize"),
                           "savefile->WriteByte(variables[", "savefile->WriteByte(variables[",
                           "savefile->ReadByte(variables[", "savefile->ReadByte(variables["],
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
    "gamesys/Event.cpp": {"legacy-reader": [R("legacyData.get(),argsize")]},
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

# Direct raw writes per game tree: only script storage, whose bytes are the
# same on every 64-bit little-endian build. A new aggregate write fails here.
GAME_DIRECT_WRITES = 2


def field(op: str, name: str, context: str, label: str) -> str:
    return f'OpenQ4_{op}SaveGameRawField(savefile,{name},"{context}","{label}")'


# Flag bitfields written one bit per flag (SaveCodec_WriteFlagBytes). Each list
# in the save and restore code must follow its struct's declaration order, or
# the bytes would move: (source, save function, restore function, struct
# source, declaration marker, member).
FLAG_LISTS = [
    ("Entity.cpp", "idEntity::WriteFlagBytes(", "idEntity::ReadFlagBytes(", "Entity.h", "struct entityFlags_s", "fl"),
    ("physics/Physics_AF.cpp", "idAFConstraint::Save(", "idAFConstraint::Restore(", "physics/Physics_AF.h",
     "struct constraintFlags_s", "fl"),
    ("physics/Physics_AF.cpp", "idAFBody::Save(", "idAFBody::Restore(", "physics/Physics_AF.h", "struct bodyFlags_s", "fl"),
    ("vehicle/Vehicle.cpp", "rvVehicle::Save (", "rvVehicle::Restore (", "vehicle/Vehicle.h", "struct vehicleFlags_s", "vfl"),
    ("vehicle/VehiclePosition.cpp", "rvVehiclePosition::Save (", "rvVehiclePosition::Restore (", "vehicle/Vehicle.h",
     "struct positionFlags_s", "fl"),
    ("vehicle/VehicleParts.cpp", "rvVehiclePart::Save (", "rvVehiclePart::Restore\t(", "vehicle/VehicleParts.h",
     "typedef struct partFlags_s", "fl"),
    ("ai/Monster_ConvoyHover.cpp", "rvMonsterConvoyHover::Save (", "rvMonsterConvoyHover::Restore (",
     "ai/Monster_ConvoyHover.cpp", "struct {", "myfl"),
]


UI: dict[str, dict[str, list[str]]] = {
    # Window layout fields go through the typed OpenQ4_*SaveGameField overloads
    # (same bytes as the raw copy). Version 5 writes the transition interpolator
    # through SaveCodec; these reads decode the raw records of versions 2 to 4.
    "Window.cpp": {
        "legacy-reader": [field("Read", "interp", "idWindow::ReadSaveGameTransition", "interpolatestate"),
                          field("Read", "saved", "idWindow::ReadSaveGameTransition", "interpolatestate")],
        "bytes": ['OpenQ4_WriteSaveGameBytes(savefile,string,len,"idWindow::WriteSaveGameString","string")',
                  'OpenQ4_ReadSaveGameBytes(savefile,&string[0],len,"idWindow::ReadSaveGameString","string")'],
    },
    "SimpleWindow.cpp": {
        "bytes":['OpenQ4_WriteSaveGameBytes(savefile,background->GetName(),stringLen,"idSimpleWindow::WriteToSaveGame","backgroundname")',
                  'OpenQ4_ReadSaveGameBytes(savefile,&(backName)[0],stringLen,"idSimpleWindow::ReadFromSaveGame","backgroundname")'],
    },
    "Winvar.h": {
        "helper": ["OpenQ4_WriteSaveGameBytes(idFile*savefile,constvoid*buffer,intlen,constchar*context,constchar*fieldName)",
                   "OpenQ4_ReadSaveGameBytes(idFile*savefile,void*buffer,intlen,constchar*context,constchar*fieldName)",
                   "OpenQ4_ReadSaveGameRawField(idFile*savefile,type&value,constchar*context,constchar*fieldName)",
                   "OpenQ4_ReadSaveGameBytes(savefile,&value,sizeof(value),context,fieldName)",
                   W("buffer,len"), R("buffer,len")],
        "bytes": ['OpenQ4_WriteSaveGameBytes(savefile,data.c_str(),len,"idWinStr::WriteToSaveGame","string")',
                  'OpenQ4_ReadSaveGameBytes(savefile,&data[0],len,"idWinStr::ReadFromSaveGame","string")',
                  'OpenQ4_WriteSaveGameBytes(savefile,data.c_str(),len,"idWinBackground::WriteToSaveGame","materialname")',
                  'OpenQ4_ReadSaveGameBytes(savefile,&data[0],len,"idWinBackground::ReadFromSaveGame","materialname")'],
    },
    "UserInterface.cpp": {"bytes": [W("string.c_str(),len"), R("buffer,len")]},
    "RegExp.cpp": {"bytes": [W("buffer,len"), R("buffer,len")]},
    # Doom 3 PDA minigames write typed fields (same bytes); these reads decode
    # their unversioned legacy format only.
    "GameBearShootWindow.cpp": {"legacy-reader": [R("&value,sizeof(value)")]},
    "GameBustOutWindow.cpp": {"legacy-reader": [R("&value,sizeof(value)")]},
    "GameSSDWindow.cpp": {"legacy-reader": [R("&value,sizeof(value)")] * 3 + [R("data,size")]},
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


def body_after(text: str, marker: str) -> str:
    start = text.index(marker)
    return text[start:text.index("\n}", start)]


def validate_flag_lists(tree: str) -> list[str]:
    problems = []
    for source, save, restore, header, marker, member in FLAG_LISTS:
        text = (ROOT / "src" / tree / source).read_text(encoding="latin-1")
        declared_text = (ROOT / "src" / tree / header).read_text(encoding="latin-1")
        if marker == "struct {":
            block = re.search(r"struct\s*\{([^}]*)\}\s*" + member + r"\s*;", declared_text).group(1)
        else:
            start = declared_text.index(marker)
            block = declared_text[start:declared_text.index("}", start)]
        declared = re.findall(r"\bbool\s+(\w+)\s*:\s*1\s*;", block)
        saved_list = re.search(member + r"Bits\[\]\s*=\s*\{(.*?)\};", body_after(text, save), re.S)
        saved = re.findall(re.escape(member) + r"\.(\w+)", saved_list.group(1)) if saved_list else []
        restored = sorted((int(index), name) for name, index in re.findall(
            re.escape(member) + r"\.(\w+)\s*=\s*" + re.escape(member) + r"Bits\[\s*(\d+)\s*\]", body_after(text, restore)))
        if not declared or saved != declared or [name for _, name in restored] != declared or \
                [index for index, _ in restored] != list(range(len(declared))):
            problems.append(f"src/{tree}/{source}: {member} flag list does not follow {marker} in {header} "
                            f"(declared {declared}, saved {saved}, restored {[n for _, n in restored]})")
        if f"static_assert( sizeof( {member} ) == ( {len(declared)} + 7 ) / 8" not in body_after(text, save):
            problems.append(f"src/{tree}/{source}: {save} lacks the {member} layout assertion")
    return problems


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
        problems += validate_flag_lists(tree)
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
