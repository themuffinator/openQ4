#!/usr/bin/env python3
"""The names The Awakening's content mistypes resolve to the ones it meant.

The engine fails a missing dialogue key, radio sound or joint without a word:
the line is not spoken, the effect not shown, an actor's eyes stay at its feet.
src/game/awakening/ContentRepairs.inl lists each such name beside the surviving
one (docs/dev/awakening-missions.md, "Silent names").

Always checked, from the sources alone:
- every table row is well formed and names two different things;
- the four call sites, the g_awakeningContentRepairs switch, the aiDumpState eye
  height and the openq4_testSpeak tester are in place, and the single-player
  module is the only one that carries them.

Checked as well when the content is available (--awakening DIR --retail DIR, or
the local defaults): every written name is really absent and every replacement
really present, so a table row cannot rot into a guess:
- the mission script speaks the written key, its speaker lacks it and has the
  replacement, and the replacement's recording is on disk;
- the written radio sound has no declaration, the recorded one has, with its
  sample;
- the written joint is missing from the mesh that names it and the intended
  joint is there;
- no definition supplies the effect key a model's frame commands name, and the
  effect given in its place is the one retail's source of those commands plays.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = "src/game/awakening/ContentRepairs.inl"

# where each written name comes from in the content
DIALOGUE_SCRIPTS = {
    "maps/game/m01_stranarus_trench1": "scripts/maps/m04_stranarus_trench.script",
    "maps/game/m01_stranarus_trench2": "scripts/maps/m04_stranarus_trench.script",
    "maps/game/m04_prison": "maps/game/m04_prison.script",
    "maps/game/m08_cryofac": "maps/game/m08_cryofac.script",
}
RADIO_SCRIPTS = ("scripts/maps/m06_mcc_invasion.script", "maps/game/m08_cryofac.script")
JOINT_MODELS = {
    # written joint -> (entityDef or model that names it, the key or command that does)
    "rt_up_lid": ("monster_q4x_tank", "joint_eyeOffset"),
    "jaw": ("monster_q4x_retch", "joint_eyeOffset"),
    "gunBarrelReal": ("model_monster_tank", "fx_rocket_flash"),
    "engine_righ": ("model_q4x_dropship", "fx_thrusters"),
}


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="latin-1", errors="replace")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"Missing {token!r} in {context}")


def table(source: str, name: str, width: int) -> list[tuple[str, ...]]:
    start = source.find(f"{name}[] = {{")
    if start < 0:
        raise AssertionError(f"Missing table {name} in {SOURCE}")
    end = source.find("\n};", start)
    body = re.sub(r"//[^\n]*", "", source[start:end])
    rows = [tuple(re.findall(r'"([^"]*)"', row)) for row in re.findall(r"\{([^{}]*)\}", body)]
    if not rows or any(len(row) != width for row in rows):
        raise AssertionError(f"{name}: expected rows of {width} strings, got {rows}")
    return rows


def check_sources() -> dict[str, list[tuple[str, ...]]]:
    source = read(ROOT / SOURCE)
    tables = {
        "dialogue": table(source, "awakeningDialogueKeys", 4),
        "radio": table(source, "awakeningRadioLines", 2),
        "joints": table(source, "awakeningJoints", 2),
        "mirrored": table(source, "awakeningMirroredJoints", 4),
        "effects": table(source, "awakeningEffects", 3),
    }
    for kind, rows in tables.items():
        seen = set()
        for row in rows:
            written = row[:3] if kind == "dialogue" else row[:1]
            if written in seen:
                raise AssertionError(f"{kind}: {written} is listed twice")
            seen.add(written)
            if len(set(value.lower() for value in row)) != len(row):
                raise AssertionError(f"{kind}: {row} repeats a name")
    for row in tables["dialogue"]:
        if row[0] not in DIALOGUE_SCRIPTS:
            raise AssertionError(f"dialogue: no script is recorded for {row[0]}")
    for written, _ in tables["joints"]:
        if written not in JOINT_MODELS:
            raise AssertionError(f"joints: no model is recorded for {written}")
    for row in tables["mirrored"]:
        if row[0] not in JOINT_MODELS:
            raise AssertionError(f"mirrored: no model is recorded for {row[0]}")

    # Every correction is switched off outside the campaign and by one setting.
    require(source, "return !gameLocal.isMultiplayer && g_awakeningContentRepairs.GetBool() && FS_AwakeningActive();", SOURCE)
    for function in ("Awakening_RestoreDialogueKeys", "Awakening_RecordedLine", "Awakening_IntendedJoint",
                     "Awakening_IntendedEffectJoint", "Awakening_IntendedEffect", "Awakening_PrecacheContentRepairs"):
        body = source[source.find(f"{function}("):]     # its definition comes before any use
        body = body[:body.find("\n}\n")]
        require(body, "Awakening_ContentRepairsActive()", function)
    # A key or sound the content does supply is never replaced.
    require(source, "args.FindKey( line.key ) != NULL", SOURCE)
    require(source, "written->GetState() != DS_DEFAULTED", SOURCE)
    require(source, "ent->spawnArgs.FindKey( key ) != NULL", SOURCE)

    game_local = read(ROOT / "src/game/Game_local.cpp")
    require(game_local, '#include "awakening/ContentRepairs.inl"', "Game_local.cpp")
    spawn = game_local[game_local.find("void idGameLocal::SpawnMapEntities("):]
    defaults = spawn.find("args.SetDefaults( &(entityDef->dict) );")
    restore = spawn.find("Awakening_RestoreDialogueKeys( args );")
    inhibit = spawn.find("InhibitEntitySpawn( args )")
    if not 0 <= defaults < restore < inhibit:
        raise AssertionError("SpawnMapEntities must restore dialogue keys after the definition's defaults")
    require(read(ROOT / "src/game/Misc.cpp"),
            'sound = Awakening_RecordedLine( spawnArgs.GetString( "snd_radiochatter", "" ) );', "idFuncRadioChatter")
    actor = read(ROOT / "src/game/Actor.cpp")
    require(actor, "eyeOffsetJoint = Awakening_IntendedJoint( headAnimator, jointname );", "idActor eye joint")
    require(actor, 'eyeOffsetJoint = Awakening_IntendedJoint( &animator, spawnArgs.GetString( "joint_eyeOffset" ) );',
            "idActor::Restore")
    blend = read(ROOT / "src/game/anim/Anim_Blend.cpp")
    require(blend, "effectJoint = Awakening_IntendedEffectJoint( ent, command.joint->c_str(), effectJointOffset );",
            "FC_FX")
    require(blend, "const idDecl *intendedEffect = Awakening_IntendedEffect( ent, command.string->c_str() );", "FC_FX")
    # the replacement effects load with the map, on a new game and on a restore
    if game_local.count("Awakening_PrecacheContentRepairs();") != 2:
        raise AssertionError("Game_local.cpp must precache the content repairs on map start and on restore")
    # the failure warning must survive for every joint nobody listed
    require(blend, 'idStr error = "Failed to play effect";', "FC_FX")
    commands = read(ROOT / "src/game/gamesys/SysCmds.cpp")
    require(commands, 'cmdSystem->AddCommand( "openq4_testSpeak"', "SysCmds.cpp")
    require(commands, "eyeHeight=%.1f", "aiDumpState")

    # The Awakening is single player only.
    for path in (ROOT / "src/mpgame").rglob("*"):
        if path.suffix in (".cpp", ".h", ".inl") and "ContentRepairs" in read(path):
            raise AssertionError(f"{path} must not carry the Awakening's content repairs")
    return tables


# ---- the player's content ----------------------------------------------------

def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def block(text: str, start: int) -> str:
    depth, index = 1, start
    while depth and index < len(text):
        depth += text[index] == "{"
        depth -= text[index] == "}"
        index += 1
    return text[start:index - 1]


class Content:
    def __init__(self, awakening: Path, retail: Path):
        self.roots = (awakening, retail)
        self.awakening = awakening
        self.defs: dict[str, dict[str, str]] = {}
        self.models: dict[str, dict] = {}
        for root in (retail, awakening):        # the expansion's definitions win
            for path in sorted((root / "def").rglob("*.def")):
                text = strip_comments(read(path))
                for match in re.finditer(r"^\s*entityDef\s+(\S+)\s*\{", text, re.M):
                    body = block(text, match.end())
                    self.defs[match.group(1).lower()] = {
                        key.lower(): value for key, value in re.findall(r'"([^"]*)"\s+"([^"]*)"', body)}
                for match in re.finditer(r"^\s*model\s+(\S+)\s*\{", text, re.M):
                    body = block(text, match.end())
                    inherit = re.search(r"^\s*inherit\s+(\S+)", body, re.M)
                    mesh = re.search(r"^\s*mesh\s+(\S+)", body, re.M)
                    self.models[match.group(1).lower()] = {
                        "inherit": inherit.group(1).lower() if inherit else None,
                        "mesh": mesh.group(1).strip('"').replace("\\", "/") if mesh else None,
                        "body": body,
                    }
        self.sounds: dict[str, str] = {}
        for root in self.roots:
            for path in (root / "sound").rglob("*.sndshd"):
                text = strip_comments(read(path))
                for match in re.finditer(r"^\s*sound\s+(\S+)\s*\{", text, re.M):
                    self.sounds.setdefault(match.group(1).lower(), block(text, match.end()))

    def keys(self, name: str, seen: set[str] | None = None) -> dict[str, str]:
        seen = seen if seen is not None else set()
        name = name.lower()
        if name in seen or name not in self.defs:
            return {}
        seen.add(name)
        own = self.defs[name]
        merged: dict[str, str] = {}
        for key in sorted(own, reverse=True):   # every "inherit*" key adds a parent
            if key.startswith("inherit"):
                merged.update(self.keys(own[key], seen))
        merged.update(own)
        return merged

    def map_entity(self, map_name: str, entity: str) -> dict[str, str]:
        text = read(self.awakening / f"{map_name}.map")
        index = text.lower().find(f'"name" "{entity.lower()}"')
        if index < 0:
            raise AssertionError(f"{map_name}: no entity '{entity}'")
        start = text.rfind("\n{", 0, index)
        end = text.find("\n}", index)
        return {key.lower(): value for key, value in re.findall(r'"([^"]*)"\s+"([^"]*)"', text[start:end])}

    def has_sample(self, sound: str) -> bool:
        body = self.sounds.get(sound.lower(), "")
        for sample in re.findall(r"(sound/\S+\.(?:wav|ogg))", body, re.I):
            relative = sample.replace("\\", "/")
            # voice-over lives under the language's folder: sound/vo -> sound/vo_english
            localized = re.sub(r"^sound/vo/", "sound/vo_english/", relative, flags=re.I)
            for root in self.roots:
                for candidate in (relative, localized):
                    path = root / candidate
                    if path.is_file() or path.with_suffix(".ogg").is_file() or path.with_suffix(".wav").is_file():
                        return True
        return False

    def joints(self, model: str) -> list[str]:
        seen = set()
        while model and model not in seen:
            seen.add(model)
            info = self.models.get(model.lower())
            if not info:
                break
            if info["mesh"]:
                for root in (self.awakening, self.roots[1]):
                    path = root / info["mesh"]
                    if path.is_file():
                        body = re.search(r"joints\s*\{(.*?)\n\}", read(path), re.S)
                        return re.findall(r'^\s*"([^"]+)"', body.group(1), re.M) if body else []
                raise AssertionError(f"{model}: mesh {info['mesh']} not found")
            model = info["inherit"]
        raise AssertionError(f"No mesh for model {model}")


def check_content(tables: dict[str, list[tuple[str, ...]]], awakening: Path, retail: Path) -> None:
    content = Content(awakening, retail)

    for map_name, entity, key, source in tables["dialogue"]:
        script = strip_comments(read(awakening / DIALOGUE_SCRIPTS[map_name]))
        if not re.search(r"aiSpeak\w*\s*\(\s*\$" + re.escape(entity) + r"\s*,\s*\"" + re.escape(key) + r"\"", script, re.I):
            raise AssertionError(f"{map_name}: the script never has ${entity} speak '{key}'")
        args = content.map_entity(map_name, entity)
        keys = content.keys(args.get("classname", ""))
        keys.update(args)
        if key.lower() in keys:
            raise AssertionError(f"{map_name}: ${entity} already has '{key}'; the row is obsolete")
        recorded = keys.get(source.lower()) if source.lower().startswith("lipsync_") else source
        if not recorded:
            raise AssertionError(f"{map_name}: ${entity} has no '{source}' to speak as '{key}'")
        if not content.has_sample(recorded):
            raise AssertionError(f"{map_name}: '{recorded}' ({key}) has no recording")

    radio_scripts = "\n".join(strip_comments(read(awakening / path)) for path in RADIO_SCRIPTS)
    for stale, recorded in tables["radio"]:
        if not re.search(r"radioChatterPlay\w*\s*\([^,]+,\s*\"" + re.escape(stale) + r"\"", radio_scripts, re.I):
            raise AssertionError(f"radio: no script plays '{stale}'")
        if stale.lower() in content.sounds:
            raise AssertionError(f"radio: '{stale}' is declared; the row is obsolete")
        if not content.has_sample(recorded):
            raise AssertionError(f"radio: '{recorded}' has no recording")

    for written, intended in tables["joints"]:
        owner, user = JOINT_MODELS[written]
        if owner in content.defs:
            keys = content.keys(owner)
            if keys.get(user.lower(), "").lower() != written.lower():
                raise AssertionError(f"{owner}: \"{user}\" is no longer '{written}'")
            model = keys["model"]
        else:
            model = owner
            if not re.search(re.escape(user) + r"\s+" + re.escape(written) + r"\b", content.models[owner]["body"]):
                raise AssertionError(f"{owner}: no '{user}' frame command on '{written}'")
        joints = {joint.lower() for joint in content.joints(model)}
        if written.lower() in joints:
            raise AssertionError(f"{owner}: the mesh has '{written}'; the row is obsolete")
        if intended.lower() not in joints:
            raise AssertionError(f"{owner}: the mesh has no '{intended}'")

    for model, key, effect in tables["effects"]:
        if not re.search(r"\bfx\s+" + re.escape(key) + r"\b", content.models[model.lower()]["body"]):
            raise AssertionError(f"{model}: no frame command plays '{key}'")
        for name in content.defs:
            keys = content.keys(name)
            if keys.get("model", "").lower() == model.lower() and key.lower() in keys:
                raise AssertionError(f"{name} supplies '{key}'; the row is obsolete")
        if not any((root / effect).is_file() for root in content.roots):
            raise AssertionError(f"{model}: '{effect}' is not in the content")
        # the evidence: retail's Iron Maiden plays this effect from the same frame command
        maiden = content.keys("monster_iron_maiden")
        if maiden.get(key.lower(), "").lower() != effect.lower():
            raise AssertionError(f"retail's Iron Maiden no longer plays '{effect}' as '{key}'")
        if not re.search(r"\bfx\s+" + re.escape(key) + r"\s+gunBarrelReal\b",
                         content.models[maiden["model"].lower()]["body"]):
            raise AssertionError("retail's Iron Maiden no longer plays the flash on gunBarrelReal")

    for written, counterpart, pair_left, pair_right in tables["mirrored"]:
        owner, user = JOINT_MODELS[written]
        if not re.search(re.escape(user) + r"\s+" + re.escape(written) + r"\b", content.models[owner]["body"]):
            raise AssertionError(f"{owner}: no '{user}' frame command on '{written}'")
        joints = {joint.lower() for joint in content.joints(owner)}
        if written.lower() in joints:
            raise AssertionError(f"{owner}: the mesh has '{written}'; the row is obsolete")
        for joint in (counterpart, pair_left, pair_right):
            if joint.lower() not in joints:
                raise AssertionError(f"{owner}: the mesh has no '{joint}'")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--awakening", type=Path, help="the expansion's q4xbase folder")
    parser.add_argument("--retail", type=Path, help="unpacked Quake 4 q4base content (def, sound, models)")
    parser.add_argument("--require-content", action="store_true", help="fail when the content is not available")
    args = parser.parse_args()

    tables = check_sources()
    awakening = args.awakening or ROOT / ".home" / "q4xbase"
    retail = args.retail or Path(r"E:\_SOURCE\_ASSETS\Q4")
    have = (awakening / "def").is_dir() and (retail / "def").is_dir()
    if have:
        check_content(tables, awakening, retail)
    elif args.require_content:
        raise AssertionError(f"Awakening content not found at {awakening} / {retail}")
    rows = sum(len(rows) for rows in tables.values())
    print(f"awakening_content_repairs: ok ({rows} names, content {'checked' if have else 'not available'})")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f"awakening_content_repairs: FAILED: {error}", file=sys.stderr)
        sys.exit(1)
