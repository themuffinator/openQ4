#!/usr/bin/env python3
"""Guards the agreements that keep a multiplayer weapon change consistent.

Reported from a live match: walking over weapon pickups with the trigger held
made the shooter appear to be firing two different weapons at once, most
reliably out of the lightning gun.  Two independent agreements were broken.

  * A multiplayer client strips BUTTON_ATTACK for the whole of a weapon change
    (idPlayer::LocalClientPredictionThink and idPlayer::NonLocalClientPrediction-
    Think both do it), so no client ever animates a shot while a weapon is being
    swapped out.  idPlayer::Think does not, so the authoritative simulation kept
    firing the outgoing weapon.  idPlayer::Weapon_Combat calls PutAway() and then
    FireWeapon() in the same frame, so the outgoing weapon could be told to begin
    a fresh attack after it had been told to lower - and rvWeaponShotgun's fire
    loop re-enters "Fire" without re-testing wsfl.lowerWeapon, spending another
    shell that nobody rendered.  The pickup auto-switch opens that window with no
    input from the player at all.  rvWeapon::NoFireWhileSwitching has to answer
    for every weapon, not just the lightning gun, and idPlayer::FireWeapon has to
    answer it before the CancelReload() branch, which raises wsfl.attack.

  * The world model - the model every *other* player sees - outlives the
    rvWeapon.  idPlayer::SetWeapon frees the weapon object and builds a new one
    against the same idAnimatedEntity, so whatever the previous weapon left bound
    to it plays on over the new one.  rvWeapon::InitViewModel starts with
    rvViewWeapon::Clear(), which stops every effect and sound on the view model;
    rvWeapon::InitWorldModel needs the same, because a weapon freed part way
    through its fire cycle (SetWeapon(-1) out of ReadFromSnapshot, the
    weaponCatchup branch of Weapon_Combat, OwnerDied) never runs its own
    teardown.  rvWeapon::OwnerDied only hid the world model, which leaves bound
    effects running for the respawn to show again.

The single-player tree is a separate fork of the same code and has to be checked
independently.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()

# Both game trees are forks of the same source and both are shipped.
GAME_TREES = ("game", "game")


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def body_of(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing {signature!r} in {context}")
    end = source.find("\n}", start)
    if end == -1:
        raise AssertionError(f"Unterminated {signature!r} in {context}")
    return source[start:end]


def tree_dirs() -> list[tuple[str, Path]]:
    return [(name, GAME_LIBS_ROOT / "src" / name) for name in GAME_TREES]


def validate_no_fire_while_switching() -> None:
    for name, tree in tree_dirs():
        if not tree.is_dir():
            continue

        header = read(tree / "Weapon.h")
        if "NoFireWhileSwitching		( void ) const { return false; }" in header:
            raise AssertionError(
                f"{name} Weapon.h: rvWeapon::NoFireWhileSwitching defaults to false again. "
                "A client drops BUTTON_ATTACK for the whole weapon change, so any weapon that "
                "still fires server-side during it spends ammo and spawns shots nobody rendered"
            )
        require(
            header,
            "NoFireWhileSwitching		( void ) const { return true; }",
            f"{name} Weapon.h",
        )

        # The lightning gun's override was the only thing turning this on; it must not
        # come back as the sole opt-in.
        lightning = read(tree / "weapon" / "WeaponLightningGun.cpp")
        if "NoFireWhileSwitching" in lightning:
            raise AssertionError(
                f"{name} WeaponLightningGun.cpp re-declares NoFireWhileSwitching; the base class "
                "answers for every weapon now"
            )

        player = read(tree / "Player.cpp")
        fire = body_of(player, "void idPlayer::FireWeapon( void ) {", f"{name} Player.cpp")

        require(fire, "noFireWhileSwitching", f"{name} idPlayer::FireWeapon")
        require(
            fire,
            "gameLocal.isMultiplayer && idealWeapon != currentWeapon && weapon->NoFireWhileSwitching()",
            f"{name} idPlayer::FireWeapon",
        )

        # CancelReload() raises wsfl.attack, so the switching test has to come first or a
        # weapon that is already lowering restarts its fire cycle.
        try:
            guard = fire.index("if ( noFireWhileSwitching ) {")
        except ValueError:
            raise AssertionError(
                f"{name} idPlayer::FireWeapon no longer answers noFireWhileSwitching up front; "
                "the weapon->IsReloading() branch below it calls CancelReload(), which raises "
                "wsfl.attack on a weapon that is being put away"
            )
        reload_branch = fire.index("weapon->CancelReload();")
        ready_branch = fire.index("weapon->BeginAttack();")
        if guard > reload_branch or guard > ready_branch:
            raise AssertionError(
                f"{name} idPlayer::FireWeapon tests noFireWhileSwitching after it has already "
                "started or resumed an attack"
            )


def validate_client_switch_masks_attack() -> None:
    """The client half of the same agreement, which the server half is matched to."""
    tree = GAME_LIBS_ROOT / "src" / "game"
    if not tree.is_dir():
        return

    player = read(tree / "Player.cpp")
    for signature in (
        "void idPlayer::LocalClientPredictionThink( void ) {",
        "void idPlayer::NonLocalClientPredictionThink( void ) {",
    ):
        body = body_of(player, signature, "game Player.cpp")
        index = body.find("idealWeapon != currentWeapon")
        if index == -1 or "usercmd.buttons &= ~BUTTON_ATTACK;" not in body[index : index + 200]:
            raise AssertionError(
                f"{signature.strip()} no longer clears BUTTON_ATTACK across a weapon change; "
                "idPlayer::FireWeapon's noFireWhileSwitching rule exists to match it"
            )


def validate_world_model_is_reset() -> None:
    for name, tree in tree_dirs():
        if not tree.is_dir():
            continue

        weapon = read(tree / "Weapon.cpp")

        # The view model is cleared wholesale; the world model has to be cleared too, or
        # the outgoing weapon's looping effects play on over the incoming one.
        init_view = body_of(weapon, "void rvWeapon::InitViewModel( void ) {", f"{name} Weapon.cpp")
        require(init_view, "viewModel->Clear ( );", f"{name} rvWeapon::InitViewModel")

        init_world = body_of(weapon, "void rvWeapon::InitWorldModel( void ) {", f"{name} Weapon.cpp")
        for call in ("ent->StopAllEffects();", "ent->StopSound( SND_CHANNEL_ANY, false );"):
            if call not in init_world:
                raise AssertionError(
                    f"{name} rvWeapon::InitWorldModel does not {call!r}; the world model entity "
                    "outlives the rvWeapon, so the previous weapon's looping effects and sounds "
                    "are inherited by the next one and the player visibly fires two weapons"
                )
        # And before the model is swapped, so nothing is left bound to a joint that the
        # new model does not have.
        if init_world.index("ent->StopAllEffects();") > init_world.index('spawnArgs.GetString( "model_world" )'):
            raise AssertionError(
                f"{name} rvWeapon::InitWorldModel clears the world model after resolving the new "
                "model; clear it before anything else touches the entity"
            )

        owner_died = body_of(weapon, "void rvWeapon::OwnerDied( void ) {", f"{name} Weapon.cpp")
        if "worldModel->StopAllEffects( );" not in owner_died:
            raise AssertionError(
                f"{name} rvWeapon::OwnerDied only hides the world model; Hide() drops the model "
                "def but leaves bound client effects running, and the respawn shows them again"
            )


def validate_lightning_gun_teardown() -> None:
    for name, tree in tree_dirs():
        if not tree.is_dir():
            continue

        source = read(tree / "weapon" / "WeaponLightningGun.cpp")

        require(source, "void rvWeaponLightningGun::StopFireEffects( void ) {", f"{name} WeaponLightningGun.cpp")

        stop = body_of(
            source, "void rvWeaponLightningGun::StopFireEffects( void ) {", f"{name} WeaponLightningGun.cpp"
        )
        for needle in (
            'viewModel->StopEffect( "fx_spire" );',
            'viewModel->StopEffect( "fx_flash" );',
            'worldModel->StopEffect( gameLocal.GetEffect( weaponDef->dict, "fx_flash_world" ) );',
        ):
            require(stop, needle, f"{name} rvWeaponLightningGun::StopFireEffects")

        # State_Fire's STAGE_DONE is only one of the ways the fire state ends.  The
        # destructor is the one that matters for a weapon change with the trigger held,
        # and ClientStale is the one that matters for leaving the PVS mid-burst.
        for signature in (
            "rvWeaponLightningGun::~rvWeaponLightningGun( void ) {",
            "void rvWeaponLightningGun::ClientStale( void ) {",
        ):
            body = body_of(source, signature, f"{name} WeaponLightningGun.cpp")
            if "StopFireEffects( );" not in body:
                raise AssertionError(
                    f"{name} {signature.strip()} does not tear the fire state down; the weapon can "
                    "be freed part way through State_Fire and its looping world muzzle effect then "
                    "outlives it on the shared world model"
                )

        client_stale = body_of(
            source, "void rvWeaponLightningGun::ClientStale( void ) {", f"{name} WeaponLightningGun.cpp"
        )
        if "StopChainLightning( );" not in client_stale:
            raise AssertionError(
                f"{name} rvWeaponLightningGun::ClientStale leaves the chain lightning arcs playing; "
                "Think() is what normally retires them and it stops being called while stale"
            )

        fire = body_of(source, "stateResult_t rvWeaponLightningGun::State_Fire( const stateParms_t& parms ) {", f"{name} WeaponLightningGun.cpp")
        require(fire, "StopFireEffects( );", f"{name} rvWeaponLightningGun::State_Fire")


def validate_current_weapon_ammo_guard() -> None:
    """player->weapon is NULL for the frames in the middle of idPlayer::SetWeapon."""
    for name, tree in tree_dirs():
        if not tree.is_dir():
            continue

        item = read(tree / "Item.cpp")
        give = body_of(
            item, "bool idItem::GiveToPlayer( idPlayer *player, bool updateHud ) {", f"{name} Item.cpp"
        )
        index = give.find('spawnArgs.GetBool( "item_currentWeaponAmmo" )')
        if index == -1:
            raise AssertionError(f"Missing item_currentWeaponAmmo handling in {name} idItem::GiveToPlayer")
        if "player->weapon == NULL" not in give[index : index + 400]:
            raise AssertionError(
                f"{name} idItem::GiveToPlayer dereferences player->weapon for item_currentWeaponAmmo "
                "without a NULL test; there is no weapon object between SetWeapon freeing the old "
                "one and building the new one, and running over an item is how that window opens"
            )


def main() -> int:
    try:
        validate_no_fire_while_switching()
        validate_client_switch_masks_attack()
        validate_world_model_is_reset()
        validate_lightning_gun_teardown()
        validate_current_weapon_ammo_guard()
    except AssertionError as error:
        print(f"mp_weapon_switch_contract: FAILED - {error}")
        return 1

    print("mp_weapon_switch_contract: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
