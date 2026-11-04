#!/usr/bin/env python3
"""Focused source-contract checks for multiplayer bot combat safety helpers."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index < 0 or second_index < 0 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing function {signature!r}")
    open_at = source.find("{", start)
    if open_at < 0:
        raise AssertionError(f"Missing body for {signature!r}")
    depth = 0
    for index in range(open_at, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_at + 1:index]
    raise AssertionError(f"Unterminated body for {signature!r}")


def trajectory_point(origin: tuple[float, float, float],
                     velocity: tuple[float, float, float],
                     gravity: tuple[float, float, float],
                     when: float) -> tuple[float, float, float]:
    """Reference the constant-gravity trajectory used by the C++ sweeps."""
    return tuple(
        position + speed * when + 0.5 * acceleration * when * when
        for position, speed, acceleration in zip(origin, velocity, gravity)
    )


def point_distance(point: tuple[float, float, float],
                   target: tuple[float, float, float]) -> float:
    return sum((a - b) ** 2 for a, b in zip(point, target)) ** 0.5


def main() -> None:
    header = read("src/mpgame/bots/BotCombat.h")
    source = read("src/mpgame/bots/BotCombat.cpp")
    source_lister = read("tools/build/list_sources.py")

    # Public helper boundary: no rvBot internals or persistent state.
    for signature in (
        "bool BotCombatFindVisibleAimPoint(",
        "bool BotCombatLineOfFireIsSafe(",
        "bool BotCombatFindIncomingProjectileThreat(",
    ):
        require(header, signature, "BotCombat public API")
        require(source, signature, "BotCombat implementation")
    if "#include \"Bot.h\"" in source or "rvBot::" in source:
        raise AssertionError("BotCombat helpers must remain independent of rvBot internals")

    # Visibility samples a stable centre-mass point before partial-cover
    # fallbacks and validates the observer's multiplayer instance.
    for sample in (
        "BOTCOMBAT_CHEST_FRACTION",
        "BOTCOMBAT_HEAD_FRACTION",
        "BOTCOMBAT_PELVIS_FRACTION",
    ):
        require(source, sample, "multi-sample visibility")
    visibility = function_body(source, "bool BotCombatFindVisibleAimPoint")
    require_before(visibility, "chest,", "head,", "centre-mass visibility priority")
    require(source, "observer->GetInstance() != target->GetInstance()", "visibility instance filter")
    require(source, "gameLocal.mpGame.CanPlay( observer )", "observer playable filter")
    require(source, "gameLocal.mpGame.CanPlay( target )", "visibility playable filter")
    require(source, "MASK_SHOT_BOUNDINGBOX", "player/world visibility trace")
    require(source, "BotCombatTraceEntity( trace ) == target", "target trace acceptance")
    require(source, "BOTCOMBAT_LATERAL_INSET", "partial-cover lateral inset")
    require(source, "chest + lateral", "right shoulder visibility fallback")
    require(source, "chest - lateral", "left shoulder visibility fallback")
    require_before(source, "chest,", "chest + lateral", "centre chest visibility priority")

    # Shot safety revalidates a target that may have changed state since it was
    # selected, rejects friendly blockers, and checks both the shooter and live
    # nearby teammates for splash exposure.  The splash checks deliberately run
    # even when the trace reaches its planned endpoint without a current blocker:
    # projectile lead can put that endpoint ahead of the target's present hull.
    line_safety = function_body(source, "bool BotCombatLineOfFireIsSafe")
    require(line_safety, "BotCombatValidFoe( shooter, intendedFoe )", "target revalidation")
    require(line_safety, "BotCombatSameTeam( shooter, hitPlayer )", "friendly-fire rejection")
    require(line_safety, "BotCombatCurrentShotModel( shooter, shotModel )", "resolved projectile shot model")
    require(header, "bool requireUsefulImpact = false", "optional useful-impact contract")
    require(line_safety, "BotCombatTrajectoryPoint( shotOrigin, launchVelocity", "gravity-aware shot arc")
    require(line_safety, "gameLocal.TraceBounds( shooter, trace", "projectile hull sweep")
    require(line_safety, "BotCombatShotCrossesMovingTeamMate", "moving teammate path rejection")
    require(line_safety, "BotCombatSplashThreatensPlayer( shooter", "projected self splash clearance")
    require(
        line_safety,
        "BotCombatSplashThreatensTeamMate( shooter, splashIgnore, actualImpact",
        "nearby teammate splash rejection",
    )
    require_before(
        line_safety,
        "BotCombatSameTeam( shooter, hitPlayer )",
        "hitPlayer == intendedFoe",
        "friendly safety ordering",
    )
    require(line_safety, "if ( requireUsefulImpact )", "visible-shot usefulness gate")
    # An unobstructed shot, and one that stops on another hostile, are useful by
    # construction and must be answered before the splash test.  Weapons with no
    # splash carry a zero radius, and BotCombatSplashThreatensPlayer rejects a
    # zero radius unconditionally (pinned just below as the "hitscan splash
    # bypass"), so putting them through it makes every hitscan weapon and every
    # splash-free projectile hold fire unless the trace happens to land on the
    # target hull - which the deliberate aim-belief error usually prevents.
    require(line_safety, "if ( !hit )", "unobstructed shot is useful")
    require(line_safety, "if ( hitPlayer )", "hostile intercept is useful")
    require(
        line_safety,
        "BotCombatSplashThreatensPlayer( intendedFoe, BotCombatSplashIgnore( hit, NULL ),",
        "trajectory-aware direct-or-splash target reach",
    )
    splash_player = function_body(source, "static bool BotCombatSplashThreatensPlayer")
    require(splash_player, "radius <= 0.0f", "hitscan splash bypass")

    team_splash = function_body(source, "static bool BotCombatSplashThreatensTeamMate")
    for contract in (
        "gameLocal.IsTeamGame()",
        "teamMate->GetInstance() != shooter->GetInstance()",
        "!gameLocal.mpGame.CanPlay( teamMate )",
        "teamMate->spectating",
        "teamMate->health <= 0",
        "BotCombatSameTeam( shooter, teamMate )",
        "BotCombatSplashThreatensPlayer( teamMate, splashIgnore, impact",
    ):
        require(team_splash, contract, "splash teammate filtering")

    predicted_splash = function_body(source, "static bool BotCombatPredictedBoundsExposed")
    require(predicted_splash, "GetLinearVelocity() * impactTime", "future teammate position")
    require(predicted_splash, "MASK_SOLID", "future splash cover traces")
    require(predicted_splash, "BotCombatTraceEntity( trace ) == player", "predicted hull trace acceptance")

    moving_corridor = function_body(source, "static bool BotCombatShotCrossesMovingTeamMate")
    require(moving_corridor, "segmentStart - velocity * Max( 0.0f, startTime )", "relative shot start")
    require(moving_corridor, "segmentEnd - velocity * Max( 0.0f, endTime )", "relative shot end")
    require(moving_corridor, ".Expand(", "projectile hull corridor")
    require(moving_corridor, ".LineIntersection( relativeStart, relativeEnd )", "moving crossing test")

    shot_model = function_body(source, "static bool BotCombatCurrentShotModel")
    for contract in (
        'spawnArgs.GetString( "def_projectile", "" )',
        "gameLocal.FindEntityDefDict( projectileName, false )",
        "idProjectile::GetVelocity( model.projectileDef ).x",
        "idProjectile::GetGravity( model.projectileDef )",
        "BotCombatProjectileBoundsFromDef( model.projectileDef )",
        "BotCombatProjectileClipMask( model.projectileDef )",
    ):
        require(shot_model, contract, "weapon-aware projectile model")

    # CanPlay uses entityNumber as an array index.  Helpers may receive stale
    # entity pointers, so the range guard is part of their safety contract.
    valid_player = function_body(source, "static bool BotCombatValidClientPlayer")
    require(valid_player, "player->entityNumber >= 0", "CanPlay lower-bound guard")
    require(valid_player, "player->entityNumber < MAX_CLIENTS", "CanPlay upper-bound guard")
    valid_foe = function_body(source, "static bool BotCombatValidFoe")
    require(valid_foe, "gameLocal.mpGame.CanPlay( shooter )", "shooter playable revalidation")
    require(valid_foe, "shooter->spectating", "shooter spectator revalidation")
    require(valid_foe, "shooter->health <= 0", "shooter health revalidation")

    # Projectile prediction is restricted to active hostile owners in this
    # instance, then samples live gravity, hull collision, splash and motion.
    projectile_query = function_body(source, "bool BotCombatFindIncomingProjectileThreat")
    require(
        projectile_query,
        "BotCombatValidClientPlayer( self )",
        "projectile query caller guard",
    )
    require(source, "ent->IsType( idProjectile::GetClassType() )", "projectile enumeration")
    require(source, "BotCombatProjectileCanDamage( projectile )", "damage-capable projectile filter")
    require(source, "projectile->GetOwner()", "projectile owner filter")
    require(source, "BotCombatValidClientPlayer( owner )", "projectile owner client guard")
    require(source, "gameLocal.mpGame.CanPlay( owner )", "owner playable filter")
    require(source, "projectile->GetInstance() != self->GetInstance()", "projectile instance filter")
    require(source, "BotCombatSameTeam( self, owner )", "owner hostility filter")
    require(projectile_query, "projectile->GetPhysics()->GetGravity()", "live projectile gravity")
    require(projectile_query, "extraClearance + projectileRadius", "projectile bounds allowance")
    require(projectile_query, "BotCombatFindProjectileImpact(", "world interception prediction")
    require(projectile_query, "BotCombatProjectileDistanceAtTime(", "moving-bounds trajectory distance")
    require(projectile_query, "BotCombatProjectileSplashRadius( projectile )", "live splash radius")
    require(projectile_query, 'GetBool( "detonate_on_fuse", "0" )', "resting explosive fuse threat")
    require(projectile_query, "projectileVelocity.LengthSqr() <= Square( 8.0f )", "resting projectile gate")

    impact_sweep = function_body(source, "static botCombatProjectileImpact_t BotCombatFindProjectileImpact")
    require(impact_sweep, "projectile->GetPhysics()->GetGravity()", "impact gravity")
    require(impact_sweep, "BotCombatSymmetricProjectileBounds(", "impact hull")
    require(impact_sweep, "projectile->GetPhysics()->GetClipMask()", "live impact mask")
    require(impact_sweep, "gameLocal.TraceBounds( projectile, trace", "parabolic world sweep")
    require(impact_sweep, "BotCombatProjectileImpactTerminates( projectile, hit )", "bounce/impact policy")

    impact_policy = function_body(source, "static bool BotCombatProjectileImpactTerminates")
    require(impact_policy, 'GetBool( "detonate_on_actor", "0" )', "actor detonation policy")
    require(impact_policy, 'GetBool( "detonate_on_world", "0" )', "world detonation policy")

    damage_capable = function_body(source, "static bool BotCombatProjectileCanDamage")
    for damage_key in (
        '"def_damage"',
        '"def_splash_damage"',
        '"def_residual_damage"',
        '"def_emit_damage"',
    ):
        require(damage_capable, damage_key, "damage-capable projectile definition")

    owner_filter = function_body(source, "static bool BotCombatHostileProjectileOwner")
    require(
        owner_filter,
        "!BotCombatValidClientPlayer( owner )",
        "stale projectile owner detection",
    )
    require_before(
        owner_filter,
        "!BotCombatValidClientPlayer( owner )",
        "BotCombatSameTeam( self, owner )",
        "stale owner before friendly proof",
    )
    # The bot's own blast always hurts it, and a team mate's does whenever
    # team damage is on: both are threats to dodge, never targets.
    require_before(
        owner_filter,
        "if ( owner == self ) {",
        "BotCombatSameTeam( self, owner )",
        "own ordnance is a threat",
    )
    require(owner_filter, 'GetBool( "si_teamDamage" )', "team mate ordnance under team damage")
    threat_scan = function_body(source, "bool BotCombatFindIncomingProjectileThreat")
    require(threat_scan, "const bool ownRound = ( owner == self );", "own rounds threaten only by blast")
    require(threat_scan, "!ownRound && closestDistance <= unsafeClearance", "own rounds never strike directly")
    require(threat_scan, "bounceNearby", "a bouncing grenade's first landing is a blast threat")

    # The recursive Meson source collector discovers the new translation unit
    # on setup without a hand-maintained source list.
    require(source_lister, 'target_root.rglob("*.cpp")', "recursive source discovery")

    # Sanity-check the trajectory math independently.  Gravity bends a path
    # below its linear extrapolation, while zero gravity remains exactly linear.
    ballistic = trajectory_point((0.0, 0.0, 100.0), (200.0, 0.0, 100.0),
                                 (0.0, 0.0, -800.0), 0.5)
    if any(abs(a - b) > 1e-6 for a, b in zip(ballistic, (100.0, 0.0, 50.0))):
        raise AssertionError("Ballistic projectile reference is incorrect")
    linear = trajectory_point((100.0, 20.0, 0.0), (-200.0, 0.0, 0.0),
                              (0.0, 0.0, 0.0), 0.5)
    if point_distance(linear, (0.0, 0.0, 0.0)) > 20.0 + 1e-6:
        raise AssertionError("Linear crossing projectile reference is incorrect")

    # Moving-frame corridor reference: subtracting teammate motion puts a
    # crossing player on the projectile line at the same future instant.
    projectile_at_crossing = trajectory_point((0.0, 0.0, 40.0), (400.0, 0.0, 0.0),
                                              (0.0, 0.0, 0.0), 0.25)
    teammate_at_crossing = trajectory_point((100.0, -50.0, 40.0), (0.0, 200.0, 0.0),
                                            (0.0, 0.0, 0.0), 0.25)
    if point_distance(projectile_at_crossing, teammate_at_crossing) > 1e-6:
        raise AssertionError("Moving teammate corridor reference is incorrect")

    validate_team_knockback_throw()
    validate_penetrating_hitscan()
    print("mp_bot_combat: ok")


def validate_penetrating_hitscan() -> None:
    """A penetrating hitscan must not pass through a foe into a team-mate.

    idGameLocal::HitScan keeps tracing through every actor while the hitscan
    def's penetrate is positive (the multiplayer railgun's is 1.0), so the bot
    traces the whole range the same way instead of stopping at the first hit.
    """
    combat = read("src/mpgame/bots/BotCombat.cpp")
    context = "penetrating hitscan team safety"
    check = function_body(combat, "static bool BotCombatPenetratingShotHitsTeamMate(")
    for token in (
        'shooter->weapon->spawnArgs.GetString( "def_hitscan", "" )',
        "gameLocal.FindEntityDefDict( hitscanName, false )",
        'hitscan->GetFloat( "penetrate" ) <= 0.0f',
        'hitscan->GetFloat( "range", "40000" )',
        "!hit->IsType( idActor::GetClassType() )",
        "BotCombatSameTeam( shooter, static_cast<idPlayer *>( hit ) )",
        "ignore = hit;",
    ):
        require(check, token, context)
    line_of_fire = function_body(combat, "bool BotCombatLineOfFireIsSafe(")
    require(line_of_fire, "BotCombatPenetratingShotHitsTeamMate( shooter, shotOrigin, shotDirection )", context)
    # Only the hitscan branch: a projectile stops at its first impact.
    require_before(line_of_fire, "MASK_SHOT_BOUNDINGBOX, shooter );",
                   "BotCombatPenetratingShotHitsTeamMate( shooter, shotOrigin, shotDirection )", context)

    game = read("src/mpgame/Game_local.cpp")
    for token in (
        'penetrate = hitscanDict.GetFloat( "penetrate" );',
        "if ( ent->IsType ( idActor::GetClassType() ) && penetrate > 0.0f ) {",
        "additionalIgnore = ent;",
    ):
        require(game, token, "idGameLocal::HitScan pass-through the bot mirrors")


def validate_team_knockback_throw() -> None:
    """A blast must not throw a team-mate into the void, team damage on or off.

    The bot's prediction must be the push the game applies: RadiusDamage's
    falloff and fudged multiplayer direction, idPlayer::Damage's knockback_team
    and g_knockback scale.  Both sides are pinned so they cannot drift apart.
    """
    combat = read("src/mpgame/bots/BotCombat.cpp")
    context = "team knockback throw"
    throw = function_body(combat, "static bool BotCombatSplashThrowsTeamMate(")
    for token in (
        "model.teamKnockback * model.power * ( 1.0f - distance / model.splashRadius )",
        "( origin + idVec3( 0.0f, 0.0f, 31.875f ) ) - impact",
        "direction.z += 24.0f;",
        "g_knockback.GetFloat() * (float)knockback / 200.0f",
        "BOTCOMBAT_TEAM_KNOCK_MIN_SPEED",
        "BotCombatPredictedBoundsExposed( teamMate, splashIgnore, impact, leadTime )",
        "navMesh.AirInputLandingSafe( origin, velocity + kick, vec3_origin, idMath::INFINITY )",
    ):
        require(throw, token, context)
    model = function_body(combat, "static bool BotCombatCurrentShotModel(")
    require(model, 'splash->GetInt( "knockback_team", va( "%d", knockback ) )', context)
    require(model, "model.power = power;", context)
    require(model, 'model.selfKnockback\t= gameLocal.IsTeamGame() ? model.teamKnockback : (float)Max( 0, knockback );', "self knockback mode")
    require(model, 'splash->GetFloat( "attackerPushScale", "2" )', "self push scale")

    line_of_fire = function_body(combat, "bool BotCombatLineOfFireIsSafe(")
    require(line_of_fire, "BotCombatSplashThrowsTeamMate( shooter, splashIgnore, actualImpact, shotModel, impactTime )", context)
    # It runs after both team-damage branches, so it covers team damage on and off.
    require_before(line_of_fire, "BOTCOMBAT_TEAM_PUSH_RADIUS", "BotCombatSplashThrowsTeamMate(", context)
    require_before(line_of_fire, "BotCombatSplashThrowsShooter( shooter, splashIgnore, actualImpact, shotModel, impactTime )",
                   "const float selfDamage", "self trajectory before health-only splash trade")

    game = read("src/mpgame/Game_local.cpp")
    radius = function_body(game, "void idGameLocal::RadiusDamage(")
    require(radius, "idVec3( 0.0f, 0.0f, 31.875f )", "RadiusDamage push direction the bot predicts")
    require(radius, "dir[2] += 24;", "RadiusDamage push direction the bot predicts")
    require(radius, "damageScale = dmgPower * ( 1.0f - dist / radius );", "RadiusDamage falloff the bot predicts")
    require(radius, "damageScale *= attackerDamageScale;", "RadiusDamage self falloff")
    player = read("src/mpgame/Player.cpp")
    require(player, 'damageDef->dict.GetInt( "knockback_team", va( "%d", knockback ), knockback );', "player team knockback")
    require(player, "kick *= g_knockback.GetFloat() * knockback * attackerPushScale / 200.0f;", "player knockback scale")


if __name__ == "__main__":
    main()
