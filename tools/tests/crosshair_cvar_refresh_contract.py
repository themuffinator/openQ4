#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static contract checks for applying crosshair cvar changes to the cursor GUI at once,
and for the animated crosshair that draws the combat picture in the cursor GUI's place."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOTS = ("src/game", "src/mpgame")
TEST_PATH = "tools/tests/crosshair_cvar_refresh_contract.py"
REFRESH = "static void Player_RefreshCrosshairGUI( idUserInterface *cursor, const rvWeapon *weapon, bool driving )"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8").replace("\r\n", "\n")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def function(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing function {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    if opening < 0:
        raise AssertionError(f"Missing body for {signature!r} in {context}")

    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"Unterminated body for {signature!r} in {context}")


def require_order(body: str, needles: tuple[str, ...], context: str) -> None:
    cursor = -1
    for needle in needles:
        index = body.find(needle, cursor + 1)
        if index < 0:
            raise AssertionError(f"Missing ordered token {needle!r} in {context}")
        cursor = index


def normalized(body: str) -> str:
    return " ".join(body.split())


def check_source_root(source_root: str) -> str:
    player_path = f"{source_root}/Player.cpp"
    player = read(player_path)

    # Only the modified flags are tested per drawn frame; the cursor is touched
    # when one of the three crosshair cvars changed, and the flags are cleared
    # only once the change reached the cursor.
    refresh = function(player, REFRESH, player_path)
    require_order(
        refresh,
        (
            "if ( !g_crosshairColor.IsModified() && !g_crosshairCustom.IsModified() && !g_crosshairCustomFile.IsModified() ) {",
            "return;",
            "if ( !driving ) {",
            "if ( weapon == NULL ) {",
            "return;",
            "weapon->UpdateCrosshairGUI( cursor );",
            'cursor->HandleNamedEvent( "weaponChange" );',
            "g_crosshairColor.ClearModified();",
            "g_crosshairCustom.ClearModified();",
            "g_crosshairCustomFile.ClearModified();",
        ),
        f"{player_path} Player_RefreshCrosshairGUI",
    )

    # After each cursor redraw: the first redraw runs the cursor GUI's onInit,
    # which would reset the colours the refresh applies.
    draw = function(player, "void idPlayer::DrawHUD( idUserInterface *_hud )", player_path)
    require_order(
        draw,
        (
            "vehicleController.UpdateCursorGUI( cursor );",
            "cursor->Redraw( gameLocal.time );",
            "Player_RefreshCrosshairGUI( cursor, weapon, true );",
            "cursor->Redraw( gameLocal.time );",
            "Player_RefreshCrosshairGUI( cursor, weapon, false );",
            "rvHitMarker::Draw();",
        ),
        f"{player_path} DrawHUD cursor refresh",
    )

    # The animated crosshair (hud_crosshairAnimated) draws the combat picture
    # itself.  Switching it reaches the cursor through the same refresh as a
    # color change, and it draws after that refresh and before the hit marker,
    # whose pulse it docks into the design: on foot from the weapon in hand,
    # in a vehicle from the gun of the seat.
    toggle = "if ( rvAnimatedCrosshair::ConsumeToggle() ) { g_crosshairColor.SetModified(); }"
    require_order(
        normalized(draw),
        (
            toggle,
            "Player_RefreshCrosshairGUI( cursor, weapon, true );",
            "rvAnimatedCrosshair::Draw( cursor, NULL, crosshairSeat != NULL ? crosshairSeat->GetActiveWeapon() : NULL );",
            toggle,
            "Player_RefreshCrosshairGUI( cursor, weapon, false );",
            "rvAnimatedCrosshair::Draw( cursor, weapon, NULL );",
            "rvHitMarker::Draw();",
        ),
        f"{player_path} DrawHUD animated crosshair",
    )

    # While it draws a design the cursor GUI's own copy goes transparent, for
    # the weapon in hand and for a vehicle's gun alike.
    weapon_path = f"{source_root}/Weapon.cpp"
    require(
        function(read(weapon_path), "void rvWeapon::UpdateCrosshairGUI( idUserInterface* gui ) const", weapon_path),
        'gui->SetStateString( "crossColor", rvAnimatedCrosshair::CursorColor( ',
        f"{weapon_path} UpdateCrosshairGUI",
    )
    vehicle_path = f"{source_root}/vehicle/VehicleParts.cpp"
    require(
        function(read(vehicle_path), "void rvVehicleWeapon::UpdateCursorGUI ( idUserInterface* gui ) const", vehicle_path),
        'gui->SetStateString ( "crossColor", rvAnimatedCrosshair::CursorColor( ',
        f"{vehicle_path} UpdateCursorGUI",
    )

    # Off by default, so a fresh profile keeps the stock picture; and the hit
    # marker stands aside only while the animated crosshair owns its pulse.
    require(
        read(f"{source_root}/gamesys/SysCvar.cpp"),
        'idCVar hud_crosshairAnimated(		"hud_crosshairAnimated",	"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL,',
        f"{source_root}/gamesys/SysCvar.cpp",
    )
    marker_path = f"{source_root}/HitMarker.cpp"
    marker = read(marker_path)
    require_order(
        function(marker, "void rvHitMarker::Draw( void )", marker_path),
        ("if ( AnimatedCrosshair_OwnsMarker() ) {", "return;", "if ( !hud_hitMarker.GetBool() ) {"),
        f"{marker_path} rvHitMarker::Draw",
    )
    require(
        function(marker, "void rvHitMarker::Trigger( int damage, int flags, hitMarkerSource_t source )", marker_path),
        "if ( !hud_hitMarker.GetBool() && !rvAnimatedCrosshair::Enabled() ) {",
        f"{marker_path} rvHitMarker::Trigger",
    )
    shared = "gamesys/AnimatedCrosshair.inl" if source_root == "src/game" else "../game/gamesys/AnimatedCrosshair.inl"
    if not marker.rstrip().endswith(f'#include "{shared}"'):
        raise AssertionError(f"{marker_path} must end by including {shared}")

    # The stock per-think refresh was Xbox 360 only and never ran on MP clients.
    think = function(player, "void idPlayer::Think( void )", player_path)
    if "g_crosshairColor.IsModified()" in think:
        raise AssertionError(f"{player_path} idPlayer::Think still polls g_crosshairColor; DrawHUD owns the refresh")

    return normalized(refresh)


def main() -> None:
    bodies = {source_root: check_source_root(source_root) for source_root in SOURCE_ROOTS}
    if bodies[SOURCE_ROOTS[0]] != bodies[SOURCE_ROOTS[1]]:
        raise AssertionError("SP/MP drift in Player_RefreshCrosshairGUI")

    # One implementation for both trees, and only under the retained interface:
    # its pieces are windows onto vector art the stock interface does not have.
    shared = read("src/game/gamesys/AnimatedCrosshair.inl")
    require(
        normalized(function(shared, "bool rvAnimatedCrosshair::Enabled( void )", "AnimatedCrosshair.inl")),
        'retained = cvarSystem->Find( "ui_retained" ); } return hud_crosshairAnimated.GetBool() && retained != NULL && retained->GetBool();',
        "AnimatedCrosshair.inl rvAnimatedCrosshair::Enabled",
    )
    if (ROOT / "src/mpgame/gamesys/AnimatedCrosshair.inl").exists():
        raise AssertionError("The animated crosshair has one implementation, under src/game/gamesys")

    # Suite membership and CI execution are checked by game_regression_suite.py.
    print("crosshair_cvar_refresh_contract: ok")


if __name__ == "__main__":
    main()
