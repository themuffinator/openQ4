#!/usr/bin/env python3
"""Guard the integrated multiplayer bot-intelligence contract.

The multiplayer bot lives in the companion GameLibs checkout, but its behaviour
crosses several source boundaries: idPlayer reports accepted damage, rvBot turns
that information and projectile predictions into movement, the objective module
supplies rule urgency, and the combat helpers provide exposed aim points and a
last-moment shot-safety decision.  These checks pin those connections without
pinning formatting or most tuning values.
"""

from __future__ import annotations

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)
).resolve()


def read_game(relative_path: str) -> str:
    path = GAME_LIBS_ROOT / relative_path
    if not path.is_file():
        raise AssertionError(
            f"required GameLibs source is missing: {path} "
            "(set OPENQ4_GAMELIBS_REPO to the companion checkout)"
        )
    return path.read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_all(haystack: str, needles: tuple[str, ...], context: str) -> None:
    for needle in needles:
        require(haystack, needle, context)


def require_regex(haystack: str, pattern: str, context: str) -> re.Match[str]:
    match = re.search(pattern, haystack, re.DOTALL)
    if match is None:
        raise AssertionError(f"Missing /{pattern}/ in {context}")
    return match


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_at = haystack.find(first)
    second_at = haystack.find(second)
    if first_at == -1:
        raise AssertionError(f"Missing {first!r} in {context}")
    if second_at == -1:
        raise AssertionError(f"Missing {second!r} in {context}")
    if first_at >= second_at:
        raise AssertionError(f"{first!r} must precede {second!r} in {context}")


def braced_section(source: str, start: int, context: str) -> str:
    """Return one C++ braced section while ignoring braces in comments/strings."""

    open_at = source.find("{", start)
    if open_at == -1:
        raise AssertionError(f"No opening brace for {context}")

    state = "code"
    depth = 0
    index = open_at
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""

        if state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and next_char == "/":
                state = "code"
                index += 1
        elif state in ("string", "character"):
            if char == "\\":
                index += 1
            elif (state == "string" and char == '"') or (
                state == "character" and char == "'"
            ):
                state = "code"
        else:
            if char == "/" and next_char == "/":
                state = "line_comment"
                index += 1
            elif char == "/" and next_char == "*":
                state = "block_comment"
                index += 1
            elif char == '"':
                state = "string"
            elif char == "'":
                state = "character"
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return source[open_at + 1 : index]
        index += 1

    raise AssertionError(f"Unterminated braced section for {context}")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function {signature!r} in {context}")
    return braced_section(source, start, context)


def validate_perception_and_damage(
    bot: str, header: str, player: str, combat: str
) -> None:
    is_enemy = function_body(bot, "bool rvBot::IsEnemy", "rvBot::IsEnemy")
    require_all(
        is_enemy,
        (
            "other->GetInstance() != self->GetInstance()",
            "!gameLocal.mpGame.CanPlay( other )",
            "other->spectating",
            "other->health <= 0",
            "other->team == self->team",
        ),
        "rvBot::IsEnemy active-instance filtering",
    )

    update_enemy = function_body(bot, "void rvBot::UpdateEnemy", "rvBot::UpdateEnemy")
    require_all(
        update_enemy,
        (
            "POWERUP_INVISIBILITY",
            "BOT_INVIS_REVEAL_MSEC",
            "other == lastAttacker.GetEntity()",
            "BUTTON_ATTACK",
            "invisRevealed",
            "CanSee( self, other, &visiblePoint )",
        ),
        "rvBot::UpdateEnemy invisibility reveal",
    )
    require_regex(
        update_enemy,
        r"PowerUpActive\(\s*POWERUP_INVISIBILITY\s*\).*?"
        r"!invisRevealed.*?firing",
        "rvBot::UpdateEnemy invisibility gate",
    )

    can_see = function_body(bot, "bool rvBot::CanSee", "rvBot::CanSee")
    require_all(
        can_see,
        (
            "BotCombatFindVisibleAimPoint(",
            "static_cast<idPlayer *>( other )",
            "*visiblePoint = point",
        ),
        "rvBot::CanSee multi-point delegation",
    )

    visibility = function_body(
        combat,
        "bool BotCombatFindVisibleAimPoint",
        "BotCombatFindVisibleAimPoint",
    )
    require_all(
        visibility,
        (
            "observer->GetInstance() != target->GetInstance()",
            "gameLocal.mpGame.CanPlay( target )",
            "BotCombatTraceEntity( trace ) == target",
            "idVec3 samples[]",
            "chest + lateral",
            "chest - lateral",
            "pelvis + lateral",
            "pelvis - lateral",
        ),
        "BotCombatFindVisibleAimPoint target filtering",
    )
    require_before(visibility, "chest,", "head,", "centre-chest visibility priority")
    require_before(visibility, "head,", "pelvis,", "head/pelvis fallback order")
    require_before(visibility, "pelvis,", "chest + lateral", "lateral-cover fallback order")

    update_aim = function_body(bot, "void rvBot::UpdateAim", "rvBot::UpdateAim")
    require_all(
        update_aim,
        ("const bool foeVisible", "truePos = enemyVisiblePoint"),
        "rvBot::UpdateAim exposed-point consumption",
    )

    # Reaction is competence; a style's eagerness bends it by a quarter at most.
    acquire = function_body(bot, "void rvBot::AcquireEnemy", "rvBot::AcquireEnemy")
    require(
        acquire,
        "1.25f - 0.5f * idMath::ClampFloat( 0.0f, 1.0f, traits.initiative )",
        "rvBot::AcquireEnemy bounded initiative reaction scale",
    )

    damage = function_body(player, "void idPlayer::Damage", "idPlayer::Damage")
    require(
        damage,
        "gameLocal.isMultiplayer && !gameLocal.isClient && damage > 0",
        "idPlayer::Damage server bot hook guard",
    )
    require(
        damage,
        "botManager.OnPlayerDamaged( this, attacker, damage, dir );",
        "idPlayer::Damage bot attacker hook",
    )
    require_before(
        damage,
        "health -= damage;",
        "botManager.OnPlayerDamaged( this, attacker, damage, dir );",
        "idPlayer::Damage accepted-damage notification",
    )

    manager_damage = function_body(
        bot,
        "void rvBotManager::OnPlayerDamaged",
        "rvBotManager::OnPlayerDamaged",
    )
    require_all(
        manager_damage,
        (
            "gameLocal.isClient",
            "IsBot( victim->entityNumber )",
            "idPlayer::GetClassType()",
            "idProjectile::GetClassType()",
            "GetOwner()",
            "OnDamaged( source, damage, dir )",
        ),
        "rvBotManager::OnPlayerDamaged source resolution",
    )

    on_damaged = function_body(bot, "void rvBot::OnDamaged", "rvBot::OnDamaged")
    require_all(
        on_damaged,
        (
            "ScheduleDodge( dir )",
            "IsEnemy( self, attacker )",
            "lastAttacker = attacker",
            "lastAttackerTime = gameLocal.time",
            "CanSee( self, attacker, &visiblePoint )",
        ),
        "rvBot::OnDamaged threat memory",
    )
    require_all(
        header,
        ("OnDamaged(", "lastAttacker", "lastAttackerTime"),
        "Bot.h damage-perception state",
    )


def validate_dodging_and_projectiles(bot: str, header: str, combat: str) -> None:
    require_all(
        header,
        ("dodgeStartTime", "dodgeEndTime", "nextThreatScanTime"),
        "Bot.h explicit dodge state",
    )

    schedule = function_body(bot, "void rvBot::ScheduleDodge", "rvBot::ScheduleDodge")
    require_regex(
        schedule,
        r"if\s*\(\s*gameLocal\.time\s*<\s*dodgeEndTime\s*\)\s*\{\s*return;",
        "rvBot::ScheduleDodge non-postponing window",
    )
    require_all(
        schedule,
        (
            "dodgeStartTime = gameLocal.time + Max( 0, delay )",
            "dodgeEndTime = dodgeStartTime + BOT_DODGE_MSEC",
        ),
        "rvBot::ScheduleDodge bounded window",
    )

    is_dodging = function_body(bot, "bool rvBot::IsDodging", "rvBot::IsDodging")
    require_regex(
        is_dodging,
        r"gameLocal\.time\s*>=\s*dodgeStartTime\s*&&\s*"
        r"gameLocal\.time\s*<\s*dodgeEndTime",
        "rvBot::IsDodging explicit window",
    )

    movement = function_body(bot, "void rvBot::UpdateMovement", "rvBot::UpdateMovement")
    require_all(
        movement,
        (
            "gameLocal.time >= nextThreatScanTime",
            "nextThreatScanTime = gameLocal.time +",
            "BotCombatFindIncomingProjectileThreat(",
            "timeToClosest",
            "ScheduleDodge( threat->GetPhysics()->GetLinearVelocity(), &closestPoint )",
            "predictiveDelay",
            "dodgeEndTime = dodgeStartTime + BOT_DODGE_MSEC",
        ),
        "rvBot::UpdateMovement projectile anticipation",
    )

    projectile_query = function_body(
        combat,
        "bool BotCombatFindIncomingProjectileThreat",
        "BotCombatFindIncomingProjectileThreat",
    )
    require_all(
        projectile_query,
        (
            "BotCombatValidClientPlayer( self )",
            "idProjectile::GetClassType()",
            "projectile->GetInstance() != self->GetInstance()",
            "BotCombatProjectileCanDamage( projectile )",
            "BotCombatHostileProjectileOwner( self, owner )",
            "projectileVelocity - selfVelocity",
            "projectile->GetPhysics()->GetGravity()",
            "BotCombatFindProjectileImpact(",
            "BotCombatProjectileDistanceAtTime(",
            "BotCombatProjectileSplashRadius( projectile )",
            'GetBool( "detonate_on_fuse", "0" )',
            "lookAheadSeconds",
        ),
        "BotCombatFindIncomingProjectileThreat ballistic/world-aware filter",
    )
    hostile_owner = function_body(
        combat,
        "static bool BotCombatHostileProjectileOwner",
        "BotCombatHostileProjectileOwner",
    )
    require_all(
        hostile_owner,
        (
            "!BotCombatValidClientPlayer( owner )",
            "owner->GetInstance() != self->GetInstance()",
            "!gameLocal.mpGame.CanPlay( owner )",
            "owner == self",
            "BotCombatSameTeam( self, owner )",
        ),
        "BotCombatHostileProjectileOwner conservative friendliness proof",
    )


def objective_priorities(source: str) -> dict[str, float]:
    names = (
        "CAPTURE",
        "CARRIER",
        "INTERCEPT",
        "RESCUE",
        "RETURN",
        "ESCORT",
        "CONTROL",
        "FETCH",
        "DEFEND",
    )
    result: dict[str, float] = {}
    for name in names:
        match = require_regex(
            source,
            rf"BOTOBJ_PRIORITY_{name}\s*=\s*([0-9]+(?:\.[0-9]+)?)f?\s*;",
            f"BotObjective {name.lower()} priority",
        )
        result[name] = float(match.group(1))
    return result


def validate_objectives_items_and_goals(
    bot: str, header: str, objective_header: str, objective: str
) -> None:
    require(bot, '#include "BotObjective.h"', "Bot.cpp objective boundary")
    require_all(
        objective_header,
        ("botObjectiveKind_t", "BOTOBJ_DEFEND", "botObjective_t", "BotFindObjective("),
        "BotObjective public contract",
    )
    for implementation_detail in (
        "BotFindCTFObjective",
        "BotFindFreezeTagObjective",
        "BotFindDeadZoneObjective",
    ):
        if implementation_detail in bot:
            raise AssertionError(
                f"{implementation_detail} leaked into Bot.cpp; mode discovery must remain "
                "separate from route/combat policy"
            )
        require(objective, implementation_detail, "BotObjective mode discovery")

    priorities = objective_priorities(objective)
    if priorities["CAPTURE"] != priorities["CARRIER"]:
        raise AssertionError("carrier completion and capture must share top rule urgency")
    priority_ladder = (
        priorities["CAPTURE"],
        priorities["INTERCEPT"],
        priorities["RESCUE"],
        priorities["RETURN"],
        priorities["ESCORT"],
        priorities["CONTROL"],
        priorities["FETCH"],
        priorities["DEFEND"],
    )
    if any(left <= right for left, right in zip(priority_ladder, priority_ladder[1:])):
        raise AssertionError(f"objective urgency ladder is not strictly descending: {priority_ladder}")

    item_utility = function_body(bot, "float rvBot::ItemUtility", "rvBot::ItemUtility")
    require_all(
        item_utility,
        (
            "rvItemCTFFlag::GetClassType()",
            "riDeadZonePowerup::GetClassType()",
            'GetInt( "inv_health"',
            "self->inventory.maxHealth",
            'GetInt( "inv_armor"',
            "self->inventory.maxarmor",
            'GetString( "inv_weapon"',
            "self->inventory.weapons",
            'MatchPrefix( "inv_ammo_"',
            "MaxAmmoForAmmoClass",
            "idItemPowerup::GetClassType()",
        ),
        "rvBot::ItemUtility inventory awareness",
    )

    # One bounded cost field prices every pickup; only the winner pays for a
    # validated route, and a blocked winner falls through to the next one.
    pick_item = function_body(bot, "idEntity *rvBot::PickItemGoal", "rvBot::PickItemGoal")
    require_all(
        pick_item,
        (
            "item->GetInstance() != self->GetInstance()",
            "ItemUtility( self, item )",
            "MAX_ROUTED_CANDIDATES",
            "navMesh.BuildCostField( origin, searchRange * 1.5f + 256.0f,",
            "targets.Ptr(), targets.Num(), MAX_ROUTED_CANDIDATES )",
            "navMesh.NodeForEntity( item )",
            "target.minWeight = pendingMsec > 0 ? option.utility * 0.5f : option.utility",
            "navMesh.CostFieldDistance( option.node )",
            "botManager.GoalClaimCount( this, item )",
            "claimScale",
            "ItemPendingMsec( item )",
            "navMesh.FindPath( origin, candidates[i].item->GetPhysics()->GetOrigin(), goalPath, false, BotMaxSafeFallDelta( self ) )",
        ),
        "rvBot::PickItemGoal routed utility and claims",
    )

    claims = function_body(
        bot,
        "int rvBotManager::GoalClaimCount",
        "rvBotManager::GoalClaimCount",
    )
    require_all(
        claims,
        (
            "bots[i].GetGoalEntity() != goal",
            "player->GetInstance() != requesterPlayer->GetInstance()",
            "player->team != requesterPlayer->team",
        ),
        "rvBotManager::GoalClaimCount team/instance reservation",
    )

    update_goal = function_body(bot, "void rvBot::UpdateGoal", "rvBot::UpdateGoal")
    require_all(
        update_goal,
        (
            "BotFindObjective( self, objective )",
            "objective.priority",
            "BOTGOAL_OBJECTIVE",
            "BotObjectiveRouteOrigin( self, objective )",
            "PathDistanceRemaining( origin )",
            "remaining + BOT_GOAL_PROGRESS_EPSILON < goalBestDistance",
            "goalProgressTime = gameLocal.time",
            "gameLocal.time - goalProgressTime > BOT_RECOVER_IDLE_MSEC",
            "gameLocal.time < goalCommitUntil",
            "desiredPriority < goalUtility * BOT_GOAL_SWITCH_MARGIN",
            "expectedTravel",
        ),
        "rvBot::UpdateGoal priorities, progress and hysteresis",
    )
    if "GoalClaimCount( this, objective.entity.GetEntity() )" in update_goal:
        raise AssertionError(
            "objective roles are already allocated globally; applying a second claim "
            "discount can peel the selected escort or rescuer off its assignment"
        )
    require_regex(
        update_goal,
        r"enemyPriority\s*>\s*desiredPriority\s*&&\s*objectivePriority\s*<=\s*0\.0f",
        "rvBot::UpdateGoal objective/combat separation",
    )
    require_all(
        header,
        (
            "BOTGOAL_OBJECTIVE",
            "goalUtility",
            "goalCommitUntil",
            "goalBestDistance",
            "goalProgressTime",
            "objectiveKind",
            "objectiveHoldPosition",
        ),
        "Bot.h goal decision state",
    )


def validate_weapons_aim_and_fire(bot: str, combat: str) -> None:
    table_at = bot.find("static const botWeaponRange_t botWeaponRanges[]")
    if table_at == -1:
        raise AssertionError("Missing botWeaponRanges range-scoring table")
    weapon_table = braced_section(bot, table_at, "botWeaponRanges")
    require_all(
        weapon_table,
        ('"weapon_dmg"', '"weapon_napalmgun"'),
        "botWeaponRanges expansion weapon coverage",
    )

    update_weapon = function_body(bot, "void rvBot::UpdateWeapon", "rvBot::UpdateWeapon")
    require_all(
        update_weapon,
        (
            "rangeBlend",
            "closeScore",
            "farScore",
            "rvBotCharacterManager::WeaponBias",
            "currentScore",
            "lastWeaponSwitchTime",
            "self->SelectWeapon( want, false )",
        ),
        "rvBot::UpdateWeapon range scoring",
    )
    require_regex(
        update_weapon,
        r"gameLocal\.time\s*-\s*lastWeaponSwitchTime\s*<\s*[0-9]+",
        "rvBot::UpdateWeapon minimum dwell",
    )
    require_regex(
        update_weapon,
        r"bestScore\s*<\s*currentScore\s*\*\s*[0-9]+(?:\.[0-9]+)?f",
        "rvBot::UpdateWeapon score-margin hysteresis",
    )

    projectile_gravity = function_body(
        bot, "idVec3 rvBot::ProjectileGravity", "rvBot::ProjectileGravity"
    )
    require_all(
        projectile_gravity,
        (
            'GetString( "def_projectile"',
            "gameLocal.FindEntityDefDict(",
            "idProjectile::GetGravity( dict )",
        ),
        "rvBot::ProjectileGravity live projectile definition",
    )

    update_aim = function_body(bot, "void rvBot::UpdateAim", "rvBot::UpdateAim")
    require_all(
        update_aim,
        (
            "const float speed = ProjectileSpeed( self )",
            "const idVec3 gravity = ProjectileGravity( self )",
            "foeVel - ownVel",
            "gravity * ( -0.5f * flight * flight )",
            "aimBelief + lead + gravityCompensation",
            "aimPoint = aimBelief + lead + gravityCompensation",
        ),
        "rvBot::UpdateAim ballistic lead/gravity compensation",
    )
    # Drop is compensated in full at every skill; skill shows in the lead.
    # Scaling gravity by aimLead landed novice grenades so short that the
    # usefulness proof refused nearly every one.
    if "flight * flight * traits.aimLead" in update_aim:
        raise AssertionError("rvBot::UpdateAim scales projectile drop by lead skill")
    require_regex(
        update_aim,
        r"for\s*\(\s*int\s+i\s*=\s*0\s*;\s*i\s*<\s*[23]\s*;.*?"
        r"flight\s*=.*?gravityCompensation",
        "rvBot::UpdateAim iterative ballistic flight solution",
    )
    # A pure lag trails a steady runner by speed x time constant forever, and
    # a slew damped against standing still trails anything crossing the view:
    # together they kept skill 5 off the hull of every moving target.  The
    # belief carries the registered motion forward, and the slew is damped
    # relative to the aim point's own angular rate.
    require_all(
        update_aim,
        (
            "aimPerceivedVelocity += ( foe->GetPhysics()->GetLinearVelocity() - aimPerceivedVelocity )",
            "aimBelief += aimPerceivedVelocity * ( traits.aimAnticipation * dt )",
            "const idVec3 foeVel = foeVisible ? aimPerceivedVelocity : enemyLastSeenVelocity",
            "mag / dt > BOT_AIM_RATE_JUMP",
            "const idAngles pursuitRate = foe ? aimTargetRate : ang_zero",
            "( aimRate[i] - pursuitRate[i] )",
        ),
        "rvBot::UpdateAim motion anticipation and smooth pursuit",
    )
    require_before(
        update_aim,
        "aimBelief += aimPerceivedVelocity * ( traits.aimAnticipation * dt )",
        "aimBelief += ( truePos - aimBelief ) * alpha",
        "rvBot::UpdateAim anticipation before the lag correction",
    )
    # A fresh belief starts with no registered motion: velocity takes a
    # moment to read, which is part of every reaction.
    fresh_belief = update_aim[update_aim.index("if ( !aimBeliefValid )") :]
    fresh_belief = fresh_belief[: fresh_belief.index("aimBeliefValid	= true;")]
    require(fresh_belief, "aimPerceivedVelocity.Zero()", "rvBot::UpdateAim fresh belief")

    fire = function_body(bot, "void rvBot::UpdateFire", "rvBot::UpdateFire")
    require_all(
        fire,
        (
            "!IsEnemy( self, foe )",
            "onTargetTime += gameLocal.GetMSec()",
            "onTargetTime >= settleMsec",
            # Initiative is taste: it may shorten the settle, never remove it.
            "Max( 0.25f, 2.0f - traits.initiative * 2.0f )",
            "BOTMISTAKE_MISTIMEDSHOT",
            "const bool singleShot",
            "gameLocal.time < nextSingleShotTime",
            "self->weapon->IsReady()",
            "BotWeaponSplashRadius( self )",
            # The safety proof runs down the vector the weapon actually fires
            # along - the view axis, which is what rvWeapon takes its muzzle
            # axis from - and not toward aimPoint.  The cone gate above lets
            # those two differ by the whole effective fire cone, so a proof run
            # against the aim ray says nothing about where the round goes.
            "const idVec3 firePoint = eye + aimAngles.ToForward() * shotRange;",
            "BotCombatLineOfFireIsSafe( self, foe, eye, firePoint, splashRadius,",
            "!suppressing",
            # The hold-fire slew is the view's speed relative to the aim
            # point's: tracking a close strafer turns faster than the hold
            # rate while sitting on target.
            "const float slewPitch		= aimRate.pitch - aimTargetRate.pitch;",
            "const float slewYaw			= aimRate.yaw - aimTargetRate.yaw;",
        ),
        "rvBot::UpdateFire settle, cadence and safety gates",
    )
    require_before(
        fire,
        "BotCombatLineOfFireIsSafe( self, foe, eye, firePoint, splashRadius,",
        "cmd.buttons |= BUTTON_ATTACK;",
        "rvBot::UpdateFire final safety",
    )
    post_attack = fire[fire.index("cmd.buttons |= BUTTON_ATTACK;") :]
    require_all(
        post_attack,
        (
            "if ( singleShot )",
            "nextSingleShotTime = gameLocal.time + cadence",
            "onTargetTime = 0",
            "aimLeadRoll = 1.0f + gameLocal.random.CRandomFloat() * traits.aimLeadError",
        ),
        "rvBot::UpdateFire slow-shot settle reset and lead reroll",
    )

    if "finalTrace" in fire or "gameLocal.TracePoint(" in fire:
        raise AssertionError(
            "rvBot::UpdateFire must not veto ballistic shots with a straight point trace"
        )

    line_safety = function_body(
        combat,
        "bool BotCombatLineOfFireIsSafe",
        "BotCombatLineOfFireIsSafe",
    )
    require_all(
        line_safety,
        (
            "BotCombatValidFoe( shooter, intendedFoe )",
            "BotCombatSameTeam( shooter, hitPlayer )",
            "BotCombatCurrentShotModel( shooter, shotModel )",
            "BotCombatTrajectoryPoint( shotOrigin, launchVelocity",
            "gameLocal.TraceBounds( shooter, trace",
            "BotCombatShotCrossesMovingTeamMate",
            "BotCombatSplashThreatensPlayer( shooter, splashIgnore, actualImpact",
            "BotCombatSplashThreatensTeamMate( shooter, splashIgnore, actualImpact",
            "hitPlayer == intendedFoe",
            "if ( requireUsefulImpact )",
            # The usefulness proof must let an unobstructed shot through before
            # it reaches the splash test.  A weapon with no splash carries a
            # zero radius, and BotCombatSplashThreatensPlayer answers false for
            # a zero radius unconditionally, so routing every hitscan weapon
            # and every splash-free projectile through it makes the bot hold
            # fire unless the trace happens to terminate on the target hull -
            # which the deliberate aim error usually prevents.
            "if ( !hit )",
            "if ( hitPlayer )",
            "BotCombatSplashThreatensPlayer( intendedFoe, BotCombatSplashIgnore( hit, NULL ),",
        ),
        "BotCombatLineOfFireIsSafe trajectory, safety and usefulness contract",
    )

    team_splash = function_body(
        combat,
        "static bool BotCombatSplashThreatensTeamMate",
        "BotCombatSplashThreatensTeamMate",
    )
    require_all(
        team_splash,
        (
            "gameLocal.IsTeamGame()",
            "teamMate->GetInstance() != shooter->GetInstance()",
            "!gameLocal.mpGame.CanPlay( teamMate )",
            "BotCombatSameTeam( shooter, teamMate )",
            "BotCombatSplashThreatensPlayer( teamMate, splashIgnore, impact",
        ),
        "BotCombat splash teammate exposure",
    )


def validate_safe_route_progress(bot: str, header: str) -> None:
    safe_move = function_body(
        bot, "static bool BotMoveDirectionSafe", "BotMoveDirectionSafe"
    )
    require_all(
        safe_move,
        (
            "gameLocal.TraceBounds(",
            "self->GetPhysics()->GetBounds()",
            "MASK_PLAYERSOLID",
            "sweep.fraction",
            "gameLocal.TracePoint(",
            "floor.fraction >= 1.0f",
            "floor.c.normal.z",
            # Solid ground can still be lava or a kill volume.
            "navMesh.IsHazardous( floor.endpos, true )",
        ),
        "BotMoveDirectionSafe hull/floor validation",
    )

    movement = function_body(bot, "void rvBot::UpdateMovement", "rvBot::UpdateMovement")
    require_all(
        movement,
        (
            "const idVec3 routeDir = moveDir",
            "( moveDir - routeDir ).LengthSqr()",
            "!BotMoveDirectionSafe( self, moveDir )",
            "moveDir = routeDir",
            "const bool routeProgress",
            "pathCorner > stuckPathCorner",
            "cornerDistance + BOT_GOAL_PROGRESS_EPSILON < stuckCornerDistance",
            "stuckChecks >= 2",
            "transportInProgress",
        ),
        "rvBot::UpdateMovement safe steering and route progress",
    )
    if movement.count("BotMoveDirectionSafe( self,") < 2:
        raise AssertionError(
            "rvBot::UpdateMovement must validate both route-free combat steering and "
            "route-layer steering before applying them"
        )
    require_all(
        header,
        ("stuckPathCorner", "stuckCornerDistance", "repathFailures", "noRouteSince"),
        "Bot.h route-progress recovery state",
    )


def validate_tactical_coherence(bot: str, header: str) -> None:
    preferred_range = function_body(
        bot,
        "static float BotPreferredCombatRange",
        "BotPreferredCombatRange",
    )
    require_all(
        preferred_range,
        (
            "self->GetCurrentWeapon()",
            'GetString( va( "def_weapon%d", currentSlot )',
            "botWeaponRanges[i].preferredRange",
            "traits.rangeDiscipline",
            "traits.combatRange",
        ),
        "weapon-aware movement range",
    )

    formation = function_body(
        bot,
        "static idVec3 BotObjectiveRouteOrigin",
        "BotObjectiveRouteOrigin",
    )
    require_all(
        formation,
        (
            "objective.kind != BOTOBJ_ESCORT",
            "carrier->GetPhysics()->GetLinearVelocity()",
            "sideOffsets[4]",
            "objective.origin - forward * trailingDistance",
            "navMesh.FindNearestNode( candidate, 192.0f, true )",
            "navMesh.GetNode( node ).origin",
        ),
        "projected stable carrier escort formation",
    )

    objective_hold = function_body(
        bot, "bool rvBot::AtObjectiveHoldPosition", "rvBot::AtObjectiveHoldPosition"
    )
    require_all(
        objective_hold,
        (
            "objectiveKind == BOTOBJ_DEFEND",
            "BOT_DEFEND_RADIUS * BOT_DEFEND_RADIUS",
            "height <= 128.0f",
        ),
        "dedicated base-defense perimeter",
    )

    pursuit = function_body(
        bot, "idVec3 rvBot::EnemyPursuitOrigin", "rvBot::EnemyPursuitOrigin"
    )
    require_all(
        pursuit,
        (
            "enemyLastSeenOrigin",
            "enemyLastSeenVelocity",
            "gameLocal.time - enemyLastSeenTime",
            "traits.pursuit",
            "lead.z = 0.0f",
            "leadDistance > 256.0f",
        ),
        "bounded last-observation pursuit prediction",
    )
    if "enemy.GetEntity()->GetPhysics()->GetLinearVelocity()" in pursuit:
        raise AssertionError(
            "EnemyPursuitOrigin must not read live enemy velocity through cover"
        )

    update_goal = function_body(bot, "void rvBot::UpdateGoal", "rvBot::UpdateGoal")
    if update_goal.count("EnemyPursuitOrigin()") < 4:
        raise AssertionError(
            "all new, fallback, and refreshed enemy routes must use pursuit prediction"
        )
    require(header, "EnemyPursuitOrigin( void ) const", "Bot.h pursuit helper")

    target_score = function_body(bot, "float rvBot::TargetScore", "rvBot::TargetScore")
    require_all(
        target_score,
        (
            "BotCarriesScoringObjective( other )",
            "BotCarriesScoringObjective( teamMate )",
            "teamMate->GetInstance() != self->GetInstance()",
            "teamMate->team != self->team",
            "carrierThreatBias",
        ),
        "carrier protection target priority",
    )

    movement = function_body(bot, "void rvBot::UpdateMovement", "rvBot::UpdateMovement")
    if movement.count("BotPreferredCombatRange( self, traits )") < 2:
        raise AssertionError(
            "weapon-aware spacing must cover routed and route-free combat"
        )
    require_all(
        movement,
        (
            "alternateDodgeValid",
            "alternateDodgeDir",
            "BotMoveDirectionSafe( self, alternateDodgeDir )",
            "strafeSide = -strafeSide",
            "wantDodgeJump = false",
        ),
        "opposite-side safe dodge fallback",
    )


def main() -> int:
    try:
        bot = read_game("src/mpgame/bots/Bot.cpp")
        header = read_game("src/mpgame/bots/Bot.h")
        player = read_game("src/mpgame/Player.cpp")
        combat = read_game("src/mpgame/bots/BotCombat.cpp")
        objective_header = read_game("src/mpgame/bots/BotObjective.h")
        objective = read_game("src/mpgame/bots/BotObjective.cpp")

        validate_perception_and_damage(bot, header, player, combat)
        validate_dodging_and_projectiles(bot, header, combat)
        validate_objectives_items_and_goals(bot, header, objective_header, objective)
        validate_weapons_aim_and_fire(bot, combat)
        validate_safe_route_progress(bot, header)
        validate_tactical_coherence(bot, header)
    except AssertionError as error:
        print(f"mp_bot_intelligence: FAILED - {error}")
        return 1

    print("mp_bot_intelligence: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
