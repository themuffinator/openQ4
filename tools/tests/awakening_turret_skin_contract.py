#!/usr/bin/env python3
"""The Awakening's mannable Strogg chaingun turret has a finished skin and
the effects its alpha content left out (docs/user/awakening.md).

- openQ4's materials for model common_turret_strogg load ahead of the
  expansion's q4x_mapobjects.mtr, use openQ4's own colour, specular and glow
  maps (all present in pak0), and keep the expansion's normal maps, which
  openQ4 never ships.
- Under q4xbase idGameLocal::FindEntityDef finds "<name>_q4x_openq4" first;
  the turret's weapon definition inherits the expansion's, keeps its rate of
  fire and 40 damage a round, and adds presentation (muzzle flash and light,
  tracers, casings, barrel smoke), convergence on the crosshair, a zoom and
  barrel heat (idVehicleTurretGun, a player gunner only, saved).
- The expansion's turret file redefined damage_machinegun, which the player's
  machinegun shares; openQ4 gives the turret its own damage definition and the
  machinegun retail's back (damage_bullet, 15 and 20 zoomed).
- The turret parts' "snd_move" servo loop plays where a def sets
  "playMoveSound"; it is transient, never saved.
- The turret's HUD is openQ4's guis/vehicles/turret_hud.gui with the
  Remastered peer remastered_chaingun_turret.q4ui: the gunner's health and
  armor and the barrel heat.
- A vehicle weapon ejects casings at all (Raven's timing test returned on
  every shot) in its joint's frame, and trails "fx_burstSmoke" after a burst
  of "burstSmokeShots" or more; neither is saved.
- materials/*_openq4.mtr files re-declare a game's materials without a
  "previously defined" warning, as def/*_openq4.def files already did.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAK0 = ROOT / "content" / "baseoq4" / "pak0"
MATERIAL_FILE = "materials/00_awakening_turret_openq4.mtr"
DEF_FILE = "def/vehicles/awakening_turret_openq4.def"
TURRET_MATERIALS = [f"models/mapobjects/q4x/common_turret_strogg/common_turret_strogg0{i}" for i in range(1, 5)]


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="replace")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    brace = source.find("{", start + len(signature))
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
    raise AssertionError(f"Unterminated {signature!r} in {context}")


def decl_bodies(text: str) -> dict[str, str]:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    bodies = {}
    for match in re.finditer(r"(?:entityDef\s+)?([A-Za-z0-9_/]+)\s*\{", text):
        if match.start() and text[:match.start()].count("{") != text[:match.start()].count("}"):
            continue
        depth = 0
        for index in range(match.end() - 1, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    bodies[match.group(1)] = text[match.end():index]
                    break
    return bodies


def check_find_entity_def() -> None:
    source = read(ROOT / "src/game/Game_local.cpp")
    body = function_body(source, "const idDeclEntityDef *idGameLocal::FindEntityDef(", "Game_local.cpp")
    mp = body.find('"%s_mp"')
    q4x = body.find('"%s_q4x_openq4"')
    assert mp >= 0 and q4x > mp, "multiplayer keeps its _mp redirect; the Awakening one is single player only"
    assert re.search(r'else if \(.*GetActiveGameDir\(\), "q4xbase" \)', body), "the _q4x_openq4 redirect is scoped to q4xbase"
    assert body.count("FindType( DECL_ENTITYDEF, name, makeDefault )") == 1, "the plain name stays the fallback"


def check_vehicle_weapon() -> None:
    source = read(ROOT / "src/game/vehicle/VehicleParts.cpp")
    header = read(ROOT / "src/game/vehicle/VehicleParts.h")
    eject = function_body(source, "void rvVehicleWeapon::EjectBrass ( void )", "VehicleParts.cpp")
    assert "gameLocal.time > brassEjectNext" not in eject, "Raven's inverted timing test is gone"
    assert "gameLocal.time < brassEjectNext" in eject and "brassEjectNext = gameLocal.time + brassEjectDelay" in eject
    assert "GetPlayerView" not in eject and "weaponDepthHackInViewID" not in eject, "casings use the joint frame, not the view"
    assert "cent->SetAxis ( axis );" in eject
    assert "g_brassTime.GetFloat() / 2.0f" in eject, "casings live as long as a hand-held weapon's"

    assert "void\t\t\t\t\tEndBurst\t\t\t( void );" in header
    assert re.search(r"int\s+burstShots;", header) and re.search(r"int\s+burstSmokeShots;", header)
    end = function_body(source, "void rvVehicleWeapon::EndBurst( void )", "VehicleParts.cpp")
    assert "burstSmokeShots <= 0" in end and '"fx_burstSmoke"' in end
    post = function_body(source, "void rvVehicleWeapon::RunPostPhysics ( void )", "VehicleParts.cpp")
    assert re.search(r"BUTTON_ATTACK \) \) \{\s*EndBurst\(\);\s*return;", post), "letting go of the trigger ends the burst"
    fire = function_body(source, "bool rvVehicleWeapon::Fire()", "VehicleParts.cpp")
    assert "burstShots++;" in fire
    spawn = function_body(source, "void rvVehicleWeapon::Spawn ( void )", "VehicleParts.cpp")
    restore = function_body(source, "void rvVehicleWeapon::Restore ( idRestoreGame* savefile )", "VehicleParts.cpp")
    save = function_body(source, "void rvVehicleWeapon::Save ( idSaveGame* savefile ) const", "VehicleParts.cpp")
    for name, body in (("Spawn", spawn), ("Restore", restore)):
        assert 'burstSmokeShots = spawnArgs.GetInt( "burstSmokeShots", "0" );' in body, f"{name} reads burstSmokeShots"
    assert "burst" not in save.lower(), "burst state is presentation only and never saved"


def check_decl_override_files() -> None:
    source = read(ROOT / "src/framework/DeclManager.cpp")
    body = function_body(source, "static bool DeclManager_IsopenQ4OverrideDeclFile(", "DeclManager.cpp")
    assert '"_openq4.def"' in body and '"materials/"' in body and '"_openq4.mtr"' in body


def check_content() -> None:
    material_path = PAK0 / MATERIAL_FILE
    assert Path(MATERIAL_FILE).name < "q4x_mapobjects.mtr", "the override must sort ahead of the expansion's file"
    materials = decl_bodies(read(material_path))
    for name in TURRET_MATERIALS:
        assert name in materials, f"{name} is re-declared"
        body = materials[name]
        bump = re.search(r"bumpmap\s+(\S+)", body)
        assert bump and bump.group(1) == name + "_local.tga", f"{name} keeps the expansion's normal map"
        for image in re.findall(r"(models/openq4/awakening/\S+\.(?:jpg|tga))", body):
            assert (PAK0 / image).is_file(), f"{name} names a missing image {image}"
        assert re.search(r"diffusemap|blend\s+diffusemap", body) and "specularmap" in body
    assert "alphatest" in materials[TURRET_MATERIALS[1]], "the drum's windows stay cut out"
    assert "twoSided" in materials[TURRET_MATERIALS[3]], "the rounds show through the front windows too"
    for index in (0, 2):
        assert "blend\t\tadd" in materials[TURRET_MATERIALS[index]], "sensor lens and vent glow stages"

    defs = decl_bodies(read(PAK0 / DEF_FILE))
    weapon = defs.get("vehicle_chaingun_weapon_q4x_openq4")
    assert weapon is not None, "the turret weapon's Awakening definition"
    assert re.search(r'"inherit"\s+"vehicle_chaingun_weapon"', weapon)
    for balance_key in ("def_damage", "firedelay", "spread", "count", "force"):
        assert f'"{balance_key}"' not in weapon, f"{balance_key} stays the expansion's"
    assert re.search(r'"spawnclass"\s+"idVehicleTurretGun"', weapon), "the turret gun heats"
    for key, value in (("convergence", "1"), ("zoomFov", "50"), ("spreadCold", "2"), ("heatTolerance", "100"),
                       ("safeHeatLevel", "40")):
        assert re.search(rf'"{key}"\s+"{value}"', weapon), f"{key} {value}"
    hitscan = re.search(r'"def_hitscan"\s+"([^"]+)"', weapon).group(1)
    assert hitscan in defs and '"inherit"\t\t\t\t"hitscan_bullet"' in defs[hitscan]
    damage = re.search(r'"def_damage"\s+"([^"]+)"', defs[hitscan]).group(1)
    assert re.search(r'"inherit"\s+"damage_bullet"', defs[damage]) and re.search(r'"damage"\s+"40"', defs[damage]), \
        "the turret keeps the expansion's 40 a round, with damage_bullet's bleeding, sounds and knockback"
    # The player's machinegun gets retail's damage back (the expansion's turret
    # file replaced the shared damage_machinegun with an uninherited 40).
    for name, value in (("damage_machinegun", "15"), ("damage_machinegun_flesh", "18"), ("damage_machinegun_zoom", "20"),
                        ("damage_machinegun_zoom_flesh", "24")):
        body = defs.get(name + "_q4x_openq4")
        assert body is not None and re.search(rf'"damage"\s+"{value}"', body), f"{name} is retail's {value}"
        parent = "damage_bullet" if name == "damage_machinegun" else "damage_machinegun_q4x_openq4"
        assert re.search(rf'"inherit"\s+"{parent}"', body), f"{name} inherits {parent}"
    for vehicle in ("vehicle_q4x_turret_chaingun", "vehicle_q4x_turret_chaingun_trench"):
        body = defs.get(vehicle + "_q4x_openq4")
        assert body is not None and re.search(rf'"inherit"\s+"{vehicle}"', body)
        assert re.search(r'"gui_hud"\s+"guis/vehicles/turret_hud.gui"', body), f"{vehicle} shows the turret HUD"
    for part in ("vehicle_q4x_turret_chaingun_yaw", "vehicle_q4x_turret_chaingun_pitch", "vehicle_acidgun_turret_yaw",
                 "vehicle_acidgun_turret_pitch"):
        body = defs.get(part + "_q4x_openq4")
        assert body is not None and re.search(r'"playMoveSound"\s+"1"', body), f"{part} plays its servo loop"
    brass = re.search(r'"def_ejectBrass"\s+"([^"]+)"', weapon).group(1)
    assert brass in defs and re.search(r'"spawnclass"\s+"rvClientMoveable"', defs[brass]), "casings are client moveables"
    # pak0 has no directory entries, so the decl manager never lists a folder
    # only openQ4 adds: its effect files sit directly in effects/ and carry the
    # longer decl name inside, like the liquids effects
    declared = {}
    for fx_file in (PAK0 / "effects").glob("*.fx"):
        match = re.search(r"^effect\s+(\S+)", read(fx_file), re.M)
        assert match, f"{fx_file.name} declares an effect"
        declared[match.group(1)] = fx_file
    referenced = re.findall(r'"(effects/openq4/awakening/[^"]+)\.fx"', read(PAK0 / DEF_FILE))
    assert len(referenced) == 4, "muzzle flash, tracer, burst smoke and overheat venting"
    for effect in referenced:
        assert effect in declared, f"{effect} is declared by a file directly in pak0/effects"
    nested = [p for p in (PAK0 / "effects").rglob("*.fx") if p.parent != PAK0 / "effects"]
    assert not nested, f"effect files below effects/ are never loaded from pak0: {nested}"

    shipped = [p for p in PAK0.rglob("*") if p.is_file() and "mapobjects/q4x" in p.as_posix()]
    assert not shipped, f"openQ4 never ships expansion paths: {shipped}"


def check_turret_gun() -> None:
    source = read(ROOT / "src/game/awakening/vehicle/TurretGun.cpp")
    assert "CLASS_DECLARATION( rvVehicleWeapon, idVehicleTurretGun )" in source
    fire = function_body(source, "bool idVehicleTurretGun::Fire( void )", "TurretGun.cpp")
    assert "if ( overheated ) {" in fire and "GetPlayerDriver() && heatTolerance > 0.0f" in fire, \
        "an overheated gun never fires and only a player gunner heats it"
    post = function_body(source, "void idVehicleTurretGun::RunPostPhysics( void )", "TurretGun.cpp")
    assert "spread = spreadHot;" in post and "zoomSpreadScale" in post, "an AI gunner keeps the def's spread"
    save = function_body(source, "void idVehicleTurretGun::Save( idSaveGame *savefile ) const", "TurretGun.cpp")
    restore = function_body(source, "void idVehicleTurretGun::Restore( idRestoreGame *savefile )", "TurretGun.cpp")
    fields = ["heat", "overheated", "lastHeatTime", "lastShotTime"]
    assert re.findall(r"Write\w+\( (\w+) \)", save) == fields, "heat state is saved in order"
    assert re.findall(r"Read\w+\( (\w+) \)", restore) == fields, "and restored in the same order"
    hud = function_body(source, "void idVehicleTurretGun::UpdateHUD( idUserInterface *gui )", "TurretGun.cpp")
    for key in ("vehicle_heat", "vehicle_overheat", "vehicle_heatwarn", "vehicle_zoom"):
        assert f'"{key}"' in hud, f"the HUD reads {key}"

    position = read(ROOT / "src/game/vehicle/VehiclePosition.cpp")
    assert "weapon->UpdateHUD ( gui );" in function_body(position, "void rvVehiclePosition::UpdateHUD ( idUserInterface* gui )",
                                                         "VehiclePosition.cpp")
    controller = read(ROOT / "src/game/vehicle/VehicleController.cpp")
    assert "weapon->CanZoom() && weapon->GetZoomGui() &&" in controller, "a zoom without a scope GUI draws none"

    parts = read(ROOT / "src/game/vehicle/VehicleParts.cpp")
    sound = function_body(parts, "void rvVehicleTurret::ReadMoveSound ( void )", "VehicleParts.cpp")
    assert 'spawnArgs.GetBool( "playMoveSound" )' in sound, "only defs that ask play the servo loop"
    for name in ("Spawn ( void )", "Restore ( idRestoreGame* savefile )"):
        assert "ReadMoveSound ( );" in function_body(parts, f"void rvVehicleTurret::{name}", "VehicleParts.cpp")
    turret_save = function_body(parts, "void rvVehicleTurret::Save ( idSaveGame* savefile ) const", "VehicleParts.cpp")
    assert "moveSound" not in turret_save, "the servo loop is transient"

    retained = read(ROOT / "src/ui/RetainedHud.cpp")
    assert '{"guis/vehicles/turret_hud.gui","guis/hud/remastered_chaingun_turret.q4ui"}' in retained
    assert (PAK0 / "guis/hud/remastered_chaingun_turret.q4ui").is_file()
    gui = read(PAK0 / "guis/vehicles/turret_hud.gui")
    for key in ("gui::vehicle_heat", "gui::vehicle_overheat", "gui::player_healthpct", "gui::player_armorpct", "#str_231708",
                "#str_231709", "#str_231710"):
        assert key in gui, f"the Classic turret HUD reads {key}"
    for language in sorted((PAK0 / "strings").glob("*_openq4.lang")):
        table = read(language)
        for key in ("#str_231708", "#str_231709", "#str_231710"):
            assert f'"{key}"' in table, f"{language.name} translates {key}"


def main() -> None:
    check_find_entity_def()
    check_vehicle_weapon()
    check_decl_override_files()
    check_content()
    check_turret_gun()
    print("awakening_turret_skin_contract: ok")


if __name__ == "__main__":
    main()
