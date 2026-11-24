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
  effect given in its place is the one retail's source of those commands plays;
- a map entity's written sound is missing (or is the retail leftover the row
  says) and the intended one is on disk;
- a speech row's sound declaration asks for a sample that is not on disk, its
  recording and its lip-sync declaration are, and a speaker really holds it;
- each reply the Cryofac script revision adds has one anchor in the installed
  script, directly follows that anchor's line in the writers' own order, and is
  a key its speaker has, with its recording;
- the m01 battle radio (M01Radio.inl, scripts/openq4/awakening/m01_radio.script)
  hooks only scenes the maps call, wraps each scene in the function it hooks,
  and says only draft lines: names the unloaded draft VO script holds, whose
  draft scene calls them over the radio or tags them "Over Radio".
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
# The replies maps/game/m08_cryofac.openq4-v4.script adds: (the line the scene
# already speaks, the speaker of the reply, the reply). LateMissions.inl holds
# the edits; the writers' order comes from scripts/maps/m08_cryo_vo.script.
CRYOFAC_REPLIES = (
    ("nava_whyisplc", "tak", "tak_goodquest"),
    ("nava_sorryserg", "tak", "tak_darnman"),
    ("tak_die", "navajas", "nava_takthatbitch"),
)
M01_RADIO = "src/game/awakening/M01Radio.inl"
M01_RADIO_SCRIPT = "content/baseoq4/pak0/scripts/openq4/awakening/m01_radio.script"
M01_DRAFT_SCRIPT = "scripts/maps/m04_stranarus_trench_intro.script"
M01_DRAFT_VO = "scripts/maps/m04_stranarus_trench_vo.script"

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
        "sounds": table(source, "awakeningEntitySounds", 5),
        "speech": table(source, "awakeningSpeech", 5),
    }
    for kind, rows in tables.items():
        seen = set()
        for row in rows:
            written = row[:3] if kind in ("dialogue", "sounds") else row[1:2] if kind == "speech" else row[:1]
            if written in seen:
                raise AssertionError(f"{kind}: {written} is listed twice")
            seen.add(written)
            # a speech row's lip-sync declaration is usually the speech's own name
            names = row[:1] + row[2:] if kind == "speech" and row[1] == row[2] else row
            if len(set(value.lower() for value in names)) != len(names):
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
                     "Awakening_IntendedEffectJoint", "Awakening_IntendedEffect", "Awakening_PrecacheContentRepairs",
                     "Awakening_CorrectEntitySounds", "Awakening_SpeechNames", "Awakening_UnwrittenLine"):
        body = source[source.find(f"{function}("):]     # its definition comes before any use
        body = body[:body.find("\n}\n")]
        require(body, "Awakening_ContentRepairsActive()", function)
    # A key or sound the content does supply is never replaced.
    require(source, "args.FindKey( line.key ) != NULL", SOURCE)
    # A declaration the engine made up for a missing name does not count as supplied.
    require(source, "return decl != NULL && !decl->IsImplicit() && decl->GetState() != DS_DEFAULTED;", SOURCE)
    require(source, "if ( Awakening_SoundDeclared( line.stale ) || !Awakening_SoundDeclared( line.recorded ) ) {", SOURCE)
    require(source, "( !sound.leftover && Awakening_SoundPlayable( sound.written ) )", SOURCE)
    require(source, "ent->spawnArgs.FindKey( key ) != NULL", SOURCE)

    game_local = read(ROOT / "src/game/Game_local.cpp")
    require(game_local, '#include "awakening/ContentRepairs.inl"', "Game_local.cpp")
    spawn = game_local[game_local.find("void idGameLocal::SpawnMapEntities("):]
    defaults = spawn.find("args.SetDefaults( &(entityDef->dict) );")
    restore = spawn.find("Awakening_RestoreDialogueKeys( args );")
    inhibit = spawn.find("InhibitEntitySpawn( args )")
    if not 0 <= defaults < restore < inhibit:
        raise AssertionError("SpawnMapEntities must restore dialogue keys after the definition's defaults")
    # entity sounds are corrected wherever an entity's media is gathered
    if game_local.count("Awakening_CorrectEntitySounds(") != game_local.count("Awakening_CorrectLateMissionMedia(") or \
            game_local.count("Awakening_CorrectEntitySounds(") != 3:
        raise AssertionError("Game_local.cpp must correct entity sounds at the spawn and at both precache passes")
    # speech is redirected where a line starts, with and without a head
    require(read(ROOT / "src/game/LipSync.cpp"),
            "StartSoundShader( declManager->FindSound( speechSound ? speechSound : lipSync->GetName() )", "StartLipSyncing")
    require(read(ROOT / "src/game/LipSync.cpp"), "lipSync = declManager->FindLipSync( lipSyncName );", "StartLipSyncing")
    require(read(ROOT / "src/game/ai/AI.cpp"),
            "StartSoundShader (declManager->FindSound ( speechSound ? speechSound : lipsync )", "idAI::Speak")
    late = read(ROOT / "src/game/awakening/LateMissions.inl")
    require(late, 'awakeningM08Version4 = "maps/game/m08_cryofac.openq4-v4.script"', "LateMissions.inl")
    require(late, "return awakeningM08Version4;", "LateMissions.inl")
    require(late, "cryoProgram.Icmp( awakeningM08Version4 ) == 0", "LateMissions.inl")
    for line, speaker, reply in CRYOFAC_REPLIES:
        require(late, f'aiSpeakWait( ${speaker}, \\"lipsync_{reply}\\" );', "LateMissions.inl v4 edits")
    # Cryofac's opening: the squad and Navajas's first mark move out of the
    # parked speeders, only from the recovered map's own placements.
    for name, before, after in (("tak", "-2288 -6432 768", "-2224 -6480 768"),
                                ("navajas", "-2048 -6456 768", "-2096 -6470 768"),
                                ("navajas_intropos_1", "-2120 -6352 768", "-2120 -6408 768")):
        if not re.search(r'\{\s*"%s",\s*"%s",\s*"%s"\s*\}' % (name, before, after), late):
            raise AssertionError(f"LateMissions.inl must move Cryofac's {name} from {before} to {after}")
    # only Tak opens the cryodoors: their seal panels take no input
    require(late, '!idStr::Icmp( args.GetString( "gui" ), "guis/common/strogg/generic_seal.gui" )', "Cryofac seals")
    require(late, 'args.SetBool( "gui_noninteractive", true );', "Cryofac seals")
    require(late, "Awakening_CorrectCryofacEntity( args );", "Awakening_DirectLateMissionEntity")
    # the second ice wall's meshes pass napalm on to its buried damage volume
    details = read(ROOT / "src/game/awakening/LateMissionDetails.inl")
    require(details, 'gameLocal.FindEntity( "door_ice_damage_2" )', "Awakening_StartCryofacDetails")
    require(details, "mesh->SetDamageEntity( volume );", "Awakening_StartCryofacDetails")
    require(details, "Awakening_StartCryofacDetails();", "Awakening_StartLateMissionDetails")
    require(read(ROOT / "src/game/Misc.cpp"),
            'sound = Awakening_RecordedLine( spawnArgs.GetString( "snd_radiochatter", "" ) );', "idFuncRadioChatter")
    # a radio line nothing declares stays silent instead of sounding the missing-sample tone
    require(read(ROOT / "src/game/Misc.cpp"), "if ( Awakening_UnwrittenLine( sound ) ) {", "idFuncRadioChatter")
    require(source, "if ( Awakening_SoundDeclared( sound ) ) {", "Awakening_UnwrittenLine")
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

    # The m01 battle radio: every hook wraps the scene it names, and the script
    # is compiled only for the two trench maps, beside the map's own program.
    radio = read(ROOT / M01_RADIO)
    hooks = re.findall(r'\{\s*"(maps/game/[^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*"oq4_m01_radio::(\w+)"\s*\}', radio)
    if len(hooks) != 11:
        raise AssertionError(f"{M01_RADIO}: expected 11 scene hooks, found {len(hooks)}")
    placement = read(ROOT / M01_RADIO_SCRIPT)
    for map_name, entity, call, placed in hooks:
        body = re.search(r"void\s+" + placed + r"\s*\(\s*\)\s*\{(.*?)\n\}", placement, re.S)
        if not body:
            raise AssertionError(f"{M01_RADIO_SCRIPT}: no function '{placed}'")
        if not re.search(r"m04_stranarus_trench::" + re.escape(call) + r"\s*\(\s*\)", body.group(1)):
            raise AssertionError(f"{M01_RADIO_SCRIPT}: '{placed}' does not run the scene '{call}'")
    require(radio, 'Awakening_SoundDeclared( "vo_4_2_2_1" )', M01_RADIO)
    require(radio, "anchors != expected", M01_RADIO)
    require(radio, "gameLocal.program.CompileFile( awakeningM01RadioScript );", M01_RADIO)
    require(game_local, '#include "awakening/M01Radio.inl"', "Game_local.cpp")
    require(game_local, "Awakening_DirectM01Radio( args );", "Game_local.cpp")
    tables["m01radio"] = [tuple(hook) for hook in hooks]
    tables["m01lines"] = [(line,) for line in re.findall(r'oq4Radio\(\s*"(vo_4_[0-9_]+)"', placement)]
    if len(tables["m01lines"]) != len(set(tables["m01lines"])):
        raise AssertionError(f"{M01_RADIO_SCRIPT}: a draft line is placed twice")

    # The Awakening is single player only. One game module serves both modes
    # (CORE-07), so every repair runs behind a guard that refuses network play.
    if (ROOT / "src/mpgame").exists():
        raise AssertionError("src/mpgame is retired; the game tree is src/game")
    repairs = read(ROOT / "src/game/awakening/ContentRepairs.inl")
    require(repairs, "static bool Awakening_ContentRepairsActive( void ) {\n"
            "\treturn !gameLocal.isMultiplayer && g_awakeningContentRepairs.GetBool() && FS_AwakeningActive();",
            "the content repairs never run in network play")
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

    def playable(self, sound: str) -> bool:
        """A declared sound with its sample, or a sample named by its path."""
        if "/" not in sound:
            return self.has_sample(sound)
        relative = sound.replace("\\", "/")
        localized = re.sub(r"^sound/vo/", "sound/vo_english/", relative, flags=re.I)
        return any((root / candidate).is_file() for root in self.roots for candidate in (relative, localized))

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
        if key.lower().startswith("lipsync_talk_"):
            # a key idAI::TalkTo builds itself: retail spells it this way
            if not re.fullmatch(r"lipsync_talk_(primary|secondary|loop\d+)", key):
                raise AssertionError(f"{map_name}: '{key}' is not a key the game reads")
        elif not re.search(r"aiSpeak\w*\s*\(\s*\$" + re.escape(entity) + r"\s*,\s*\"" + re.escape(key) + r"\"", script, re.I):
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

    for map_name, entity, key, written, intended in tables["sounds"]:
        args = content.map_entity(map_name, entity)
        if args.get(key.lower(), "").lower() != written.lower():
            raise AssertionError(f"{map_name}: {entity} \"{key}\" is no longer '{written}'")
        if not content.playable(intended):
            raise AssertionError(f"{map_name}: '{intended}' for {entity} is not in the content")
        if content.playable(written):
            # a leftover: it must be a retail line, and the scene's own line must be the expansion's
            if not re.fullmatch(r"vo_\d+(_\d+)+", written) or intended.lower() not in content.sounds:
                raise AssertionError(f"{map_name}: '{written}' on {entity} plays; the row is obsolete")

    lipsyncs = set()
    for root in content.roots:
        for path in (root / "lipsync").rglob("*.lipsync"):
            lipsyncs.update(name.lower() for name in re.findall(r"^\s*lipsync\s+(\S+)", read(path), re.M | re.I))
    for map_name, speech, lip_sync, declared, sample in tables["speech"]:
        if content.playable(declared):
            raise AssertionError(f"speech: '{declared}' is on disk; the row for '{speech}' is obsolete")
        if content.has_sample(speech):
            raise AssertionError(f"speech: '{speech}' is declared with a sample; the row is obsolete")
        if not content.playable(sample):
            raise AssertionError(f"speech: the recording '{sample}' is not in the content")
        if lip_sync.lower() not in lipsyncs:
            raise AssertionError(f"speech: no lip-sync declaration '{lip_sync}'")
        # some speaker of that map holds the line: a definition key or a map key
        holders = [name for name, keys in content.defs.items() if speech.lower() in (v.lower() for v in keys.values())]
        in_map = f'"{speech.lower()}"' in read(awakening / f"{map_name}.map").lower()
        if not holders and not in_map:
            raise AssertionError(f"speech: nothing in {map_name} speaks '{speech}'")

    vo = read(awakening / "scripts/maps/m08_cryo_vo.script")
    order = [name.lower() for name in re.findall(r"void\s+VO_(\w+)\s*\(", vo)]
    cryofac = strip_comments(read(awakening / "maps/game/m08_cryofac.script"))
    for line, speaker, reply in CRYOFAC_REPLIES:
        if order.index(reply) != order.index(line) + 1:
            raise AssertionError(f"m08: the writers no longer place '{reply}' directly after '{line}'")
        if f'"lipsync_{reply}"' in cryofac:
            raise AssertionError(f"m08: the installed script already speaks '{reply}'; the edit is obsolete")
        args = content.map_entity("maps/game/m08_cryofac", speaker)
        keys = content.keys(args.get("classname", ""))
        keys.update(args)
        recorded = keys.get(f"lipsync_{reply}")
        if not recorded or not content.has_sample(recorded):
            raise AssertionError(f"m08: ${speaker} cannot speak 'lipsync_{reply}'")

    # Each m01 hook names a trigger the map has, still calling its scene, and each
    # placed line is a draft radio line nothing declares or a declared one with
    # its sample (once the audio ships, every placed line must reach a sample).
    for map_name, entity, call, _ in tables["m01radio"]:
        if content.map_entity(map_name, entity).get("call") != call:
            raise AssertionError(f"{map_name}: '{entity}' no longer calls '{call}'")
    draft = read(awakening / M01_DRAFT_SCRIPT)
    vo = read(awakening / M01_DRAFT_VO)
    texts = {name.lower(): text for name, text in re.findall(r"void\s+VO_(\w+)\s*\([^)]*\)\s*\{\s*//([^\n]*)", vo)}
    for (line,) in tables["m01lines"]:
        stem = line.lower()[3:]
        if stem not in texts:
            raise AssertionError(f"m01 radio: '{line}' is not a line of the draft VO script")
        radio_call = re.search(r"^\s*radioChatterPlay\w*\s*\([^,]+,\s*\"" + re.escape(line) + r"\"", draft, re.M | re.I)
        if not radio_call and "over radio" not in texts[stem].lower():
            raise AssertionError(f"m01 radio: '{line}' is neither a draft radio call nor an over-the-radio line")
        if line.lower() in content.sounds and not content.has_sample(line):
            raise AssertionError(f"m01 radio: '{line}' is declared without its sample; it would play the missing-sample tone")

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
