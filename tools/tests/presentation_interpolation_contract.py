#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static contract checks for high-refresh player/viewmodel presentation."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOTS = ("src/game",)
PRESENTATION_STATE = (
    "presentationViewTime",
    "presentationCanInterpolate",
    "presentationPrevViewOrigin",
    "presentationPrevViewAxis",
    "presentationPrevFov",
    "presentationCurViewOrigin",
    "presentationCurViewAxis",
    "presentationCurFov",
    "presentationPrevLookAngles",
    "presentationCurLookAngles",
    "presentationPrevCmdAngles",
    "presentationCurCmdAngles",
    "presentationLookCoupled",
    "presentationLookActive",
    "presentationLookCorrection",
    "presentationAltViewTime",
    "presentationAltViewCanInterpolate",
    "presentationPrevAltOrigin",
    "presentationCurAltOrigin",
)
PRESENTATION_WEAPON_STATE = (
    "presentationViewModelTime",
    "presentationViewModelCanInterpolate",
    "presentationPrevPlayerViewOrigin",
    "presentationPrevPlayerViewAxis",
    "presentationCurPlayerViewOrigin",
    "presentationCurPlayerViewAxis",
    "presentationPrevViewModelOrigin",
    "presentationPrevViewModelAxis",
    "presentationCurViewModelOrigin",
    "presentationCurViewModelAxis",
    "presentationRestoreViewModelOrigin",
    "presentationRestoreViewModelAxis",
    "presentationFlashlightBackoff",
)
PRESENTATION_ENTITY_STATE = (
    "presentationPoseTime",
    "presentationPoseCanInterpolate",
    "presentationPoseMoved",
    "presentationPosePushed",
    "presentationPoseHeld",
    "presentationPrevOrigin",
    "presentationPrevAxis",
    "presentationCurOrigin",
    "presentationCurAxis",
)


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index < 0:
        raise AssertionError(f"Missing {first!r} in {context}")
    if second_index < 0:
        raise AssertionError(f"Missing {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


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


def normalized(body: str) -> str:
    return " ".join(body.split())


def check_source_root(source_root: str) -> dict[str, str]:
    context = source_root
    game_local_h = read(f"{source_root}/Game_local.h")
    game_local_cpp = read(f"{source_root}/Game_local.cpp")
    multiplayer_cpp = read(f"{source_root}/MultiplayerGame.cpp")
    player_h = read(f"{source_root}/Player.h")
    player_cpp = read(f"{source_root}/Player.cpp")
    weapon_h = read(f"{source_root}/Weapon.h")
    weapon_cpp = read(f"{source_root}/Weapon.cpp")
    anim_h = read(f"{source_root}/anim/Anim.h")
    anim_cpp = read(f"{source_root}/anim/Anim_Blend.cpp")
    client_entity_h = read(f"{source_root}/client/ClientEntity.h")
    client_entity_cpp = read(f"{source_root}/client/ClientEntity.cpp")
    client_effect_h = read(f"{source_root}/client/ClientEffect.h")
    client_effect_cpp = read(f"{source_root}/client/ClientEffect.cpp")
    client_model_h = read(f"{source_root}/client/ClientModel.h")
    client_model_cpp = read(f"{source_root}/client/ClientModel.cpp")
    lightning_gun_cpp = read(f"{source_root}/weapon/WeaponLightningGun.cpp")
    entity_h = read(f"{source_root}/Entity.h")
    entity_cpp = read(f"{source_root}/Entity.cpp")
    actor_cpp = read(f"{source_root}/Actor.cpp")
    light_cpp = read(f"{source_root}/Light.cpp")
    projectile_cpp = read(f"{source_root}/Projectile.cpp")

    require(game_local_h, "mutable openq4::PresentationClock presentationClock", context)
    require(game_local_h, "presentationClockSample", context)
    clear = function(game_local_cpp, "void idGameLocal::Clear( void )", context)
    require(clear, "presentationClock.Reset();", context)
    tic_fraction = function(game_local_cpp, "float idGameLocal::GetPresentationTicFraction( void ) const", context)
    require(tic_fraction, "presentationClock.Phase", context)
    require(tic_fraction, "idLib::sys->GetClockTicks()", context)
    require(tic_fraction, "return presentationClockSample;", context)
    reject(tic_fraction, "Sys_Milliseconds", context)
    clock = function(game_local_cpp, "int idGameLocal::GetPresentationTimeMsec( void ) const", context)
    require(clock, "GetPresentationTicFraction()", context)
    fraction = function(game_local_cpp, "float idGameLocal::GetPresentationInterpolationFraction( void ) const", context)
    require_before(fraction, "return presentationSceneFraction;", "return GetPresentationTicFraction();", context)

    axis = function(
        game_local_cpp,
        "idMat3 idGameLocal::InterpolatePresentationAxis",
        context,
    )
    require(axis, "blended.Slerp", f"{context} rotational interpolation")

    prepare = function(
        game_local_cpp,
        "void idGameLocal::PreparePlayerSceneForRender( idPlayer *player",
        context,
    )
    require(prepare, "player->CalculateRenderView();", f"{context} draw-time camera refresh")
    require(prepare, "UpdatePresentationWeapon", f"{context} draw-time viewmodel refresh")
    require(prepare, "GetDemoState() == DEMO_PLAYING || IsTimeDemo()", f"{context} demo presentation bypass")
    require(prepare, "player->IsPresentationViewInterpolated()", f"{context} one-clock scene gate")
    require(prepare, "UpdatePresentationEntityPoses();", f"{context} movers follow the camera clock")
    require(prepare, "ClearPresentationEntityPoses();", f"{context} authoritative-camera fallback")
    require(prepare, "presentationSceneFraction = GetPresentationInterpolationFraction();", f"{context} freezes one draw fraction")
    require(prepare, "const int authoritativeInterval = Max( 0, time - previousTime );", f"{context} previous-to-current interval")
    require(prepare, "idMath::Ceil( common->GetUserCmdMsecFloat() )", f"{context} sequential animation interval")
    require(prepare, "GetMHz() == common->GetUserCmdHz()", f"{context} exact animation cadence")
    require(prepare, "idMath::Ftoi( authoritativeInterval * presentationSceneFraction + 0.5f )", f"{context} rounded animation sample")
    require(prepare, "idMath::ClampInt( previousTime, time, previousTime + presentationOffset )", f"{context} bounded previous-to-current animation sample")
    require(prepare, "presentationAnimationTime = time;", f"{context} discontinuity fallback")
    require_before(prepare, "presentationSceneFraction = -1.0f;", "GetDemoState() == DEMO_PLAYING || IsTimeDemo()", f"{context} demo disables frozen sampling")
    require_before(prepare, "presentationAnimationTime = -1;", "GetDemoState() == DEMO_PLAYING || IsTimeDemo()", f"{context} demo disables skeletal sampling")
    require_before(prepare, "presentationSceneFraction = GetPresentationInterpolationFraction();", "player->CalculateRenderView();", f"{context} camera and skeleton share one fraction")
    require_before(prepare, "presentationAnimationTime = idMath::ClampInt", "player->CalculateRenderView();", f"{context} skeletal clock freezes before scene submission")
    reject(prepare, "Think(", f"{context} presentation-only draw pass")
    reject(prepare, "RunFrame(", f"{context} presentation-only draw pass")

    end_scene = function(
        game_local_cpp,
        "void idGameLocal::EndPresentationSceneForRender( void )",
        context,
    )
    require(end_scene, "presentationSceneFraction = -1.0f;", f"{context} frozen fraction draw-scope reset")
    require(end_scene, "presentationAnimationTime = -1;", f"{context} skeletal clock draw-scope reset")

    # Skeletal presentation owns an aligned, draw-only pose.  It is deliberately
    # not a second animator timeline: every submission is freshly evaluated (or
    # copied from the authoritative pose for non-rewindable AF/angular state),
    # then all mutable animator state is put back before gameplay can observe it.
    require(anim_h, "CreatePresentationFrame", f"{context} animator presentation API")
    require(anim_h, "ClearPresentationFrame", f"{context} animator presentation lifetime API")
    require(anim_h, "GetPresentationJointTransform", f"{context} presentation joint API")
    require(anim_h, "idJointMat *\t\t\t\tpresentationJoints", f"{context} animator-owned scratch joints")
    require(anim_h, "presentationJointsValid", f"{context} explicit scratch lifetime")
    require(
        anim_h,
        "GetPresentationJointTransform( jointHandle_t jointHandle, idVec3 &offset, idMat3 &axis ) const",
        f"{context} timeless read-only presentation accessor",
    )
    reject(anim_h, "lastPresentationTransformTime", f"{context} no time-only presentation cache")
    reject(anim_h, "presentationTransformTime", f"{context} no time-only presentation cache")

    animator_constructor = function(anim_cpp, "idAnimator::idAnimator()", context)
    require(animator_constructor, "presentationJoints\t\t= NULL;", f"{context} scratch pointer initialization")
    require(animator_constructor, "presentationJointsValid\t= false;", f"{context} scratch validity initialization")

    animator_allocated = function(anim_cpp, "size_t idAnimator::Allocated( void ) const", context)
    require(animator_allocated, "presentationJoints ? numJoints * sizeof( presentationJoints[0] ) : 0", f"{context} scratch memory accounting")

    animator_free = function(anim_cpp, "void idAnimator::FreeData( void )", context)
    require(animator_free, "Mem_Free16( presentationJoints );", f"{context} scratch free")
    require(animator_free, "presentationJoints = NULL;", f"{context} scratch pointer clear")
    require(animator_free, "presentationJointsValid = false;", f"{context} scratch validity clear")

    animator_save = function(anim_cpp, "void idAnimator::Save( idSaveGame *savefile ) const", context)
    reject(animator_save, "presentationJoints", f"{context} animator save-format isolation")
    reject(animator_save, "presentationJointsValid", f"{context} animator save-format isolation")

    # A pose drawn between tics takes its bounds from the tic pose GetBounds
    # skinned, widened by the joints' travel. The remembered pose is draw-only
    # state like the scratch joints: never saved, and dropped whenever
    # frameBounds stops being the mesh's own exact bounds.
    require(anim_h, "bool\t\t\t\t\t\tGetPresentationBounds( const idRenderModel *model, const idJointMat *poseJoints, idBounds &bounds ) const;", f"{context} presentation bounds API")
    require(animator_constructor, "frameBoundsJointsValid\t= false;", f"{context} reference pose starts invalid")
    require(animator_free, "Mem_Free16( frameBoundsJoints );", f"{context} reference pose free")
    require(animator_free, "frameBoundsJointsValid = false;", f"{context} reference pose cleared with the model")
    reject(animator_save, "frameBoundsJoints", f"{context} reference pose stays out of the save format")
    animator_restore = function(anim_cpp, "void idAnimator::Restore( idRestoreGame *savefile )", context)
    require_before(animator_restore, "savefile->ReadBounds( frameBounds );", "frameBoundsJointsValid = false;", f"{context} restored bounds have no reference pose")
    get_bounds = function(anim_cpp, "bool idAnimator::GetBounds( int currentTime, idBounds &bounds )", context)
    require_before(get_bounds, "frameBounds = bounds;", "SIMDProcessor->Memcpy( frameBoundsJoints, joints, numJoints * sizeof( frameBoundsJoints[0] ) );", f"{context} exact bounds keep their pose")
    require_before(get_bounds, "frameBoundsJointsValid = true;", "frameBoundsJointsValid = false;", f"{context} envelope bounds drop the reference pose")
    presentation_bounds = function(anim_cpp, "bool idAnimator::GetPresentationBounds( const idRenderModel *model, const idJointMat *poseJoints, idBounds &bounds ) const", context)
    require(presentation_bounds, "frameBoundsJointsValid && frameBoundsNumJoints == numJoints", f"{context} reference pose must match the skeleton")
    require(presentation_bounds, "modelDef->ModelHandle() == model", f"{context} reference bounds belong to this model")
    require(presentation_bounds, "model->BoundsFromReferencePose( frameBoundsJoints, frameBounds, poseJoints, bounds )", f"{context} widened reference bounds")
    require(presentation_bounds, "return model->BoundsFromJoints( poseJoints, bounds );", f"{context} exact bounds when the model declines")

    create_frame = function(anim_cpp, "bool idAnimator::CreateFrame( int currentTime, bool force )", context)
    require(create_frame, "presentationJointsValid = false;", f"{context} authoritative evaluation invalidates scratch")

    create_presentation = function(
        anim_cpp,
        "bool idAnimator::CreatePresentationFrame( int currentTime, idJointMat **jointsPtr )",
        context,
    )
    require(create_presentation, "Mem_Alloc16( numJoints * sizeof( presentationJoints[0] ), MA_ANIM )", f"{context} aligned animator scratch allocation")
    require(create_presentation, "presentationJointsValid = false;", f"{context} fresh presentation evaluation")
    reject(create_presentation, "if ( presentationJointsValid", f"{context} no validity-only pose reuse")
    # Guard the actual anti-pattern - a cache keyed on the requested presentation
    # time - rather than the "lastPresentation" prefix, which also matches the
    # lastPresentationJointMod* diagnostics this function legitimately writes.
    reject(create_presentation, "lastPresentationTime", f"{context} no time-only pose reuse")
    require(create_presentation, "AFPoseJoints.Num() == 0", f"{context} AF rewind exclusion")
    require(create_presentation, "angularVelocity.GetStartTime() == 0", f"{context} angular joint-mod rewind exclusion")
    require(create_presentation, "if ( !canEvaluateAtPresentationTime )", f"{context} authoritative fallback gate")
    require(create_presentation, "SIMDProcessor->Memcpy( presentationJoints, joints", f"{context} authoritative fallback/copy")
    require(create_presentation, "idJointMat *authoritativeJoints = joints;", f"{context} authoritative joint pointer stash")
    require(create_presentation, "const int authoritativeTransformTime = lastTransformTime;", f"{context} transform cache stash")
    require(create_presentation, "const bool authoritativeStoppedAnimatingUpdate = stoppedAnimatingUpdate;", f"{context} stopped-animation cache stash")
    require(create_presentation, "authoritativeJointModMats[i] = jointMods[i]->mat;", f"{context} joint-mod matrix stash")
    require(create_presentation, "authoritativeJointModTimes[i] = jointMods[i]->lastTime;", f"{context} joint-mod time stash")
    require(create_presentation, "joints = presentationJoints;", f"{context} isolated evaluation destination")
    require(create_presentation, "CreateFrame( currentTime, true );", f"{context} forced draw-only evaluation")
    require(create_presentation, "joints = authoritativeJoints;", f"{context} authoritative joint pointer restore")
    require(create_presentation, "lastTransformTime = authoritativeTransformTime;", f"{context} transform cache restore")
    require(create_presentation, "stoppedAnimatingUpdate = authoritativeStoppedAnimatingUpdate;", f"{context} stopped-animation cache restore")
    require(create_presentation, "jointMods[i]->mat = authoritativeJointModMats[i];", f"{context} joint-mod matrix restore")
    require(create_presentation, "jointMods[i]->lastTime = authoritativeJointModTimes[i];", f"{context} joint-mod time restore")
    require(create_presentation, "*jointsPtr = presentationJoints;", f"{context} scratch pose export")

    clear_presentation = function(anim_cpp, "void idAnimator::ClearPresentationFrame( void )", context)
    require(clear_presentation, "presentationJointsValid = false;", f"{context} scratch lifetime end")

    get_presentation_joint = function(
        anim_cpp,
        "bool idAnimator::GetPresentationJointTransform( jointHandle_t jointHandle, idVec3 &offset, idMat3 &axis ) const",
        context,
    )
    require(get_presentation_joint, "!presentationJointsValid", f"{context} scoped presentation joint read")
    require(get_presentation_joint, "presentationJoints[ jointHandle ].ToVec3()", f"{context} presentation translation read")
    require(get_presentation_joint, "presentationJoints[ jointHandle ].ToMat3()", f"{context} presentation rotation read")
    reject(get_presentation_joint, "CreateFrame", f"{context} accessor never evaluates animation")
    reject(get_presentation_joint, "currentTime", f"{context} accessor has no hidden time input")

    # A mover drawn on the authoritative pose under an interpolated eye separates
    # by one tic of its own travel and snaps back every tic.  Everything visible
    # has to share the camera's presentation time or it beats against it.
    for field in PRESENTATION_ENTITY_STATE:
        require(entity_h, field, f"{context} entity presentation state")
    require(entity_h, "idLinkList<idEntity>	presentationNode;", f"{context} presentation list membership")

    model_transform = function(entity_cpp, "void idEntity::UpdateModelTransform( void )", context)
    require(model_transform, "presentationPoseHeld", f"{context} held pose survives bind resolution")

    entity_save = function(entity_cpp, "void idEntity::Save( idSaveGame *savefile ) const", context)
    for field in PRESENTATION_ENTITY_STATE:
        reject(entity_save, field, f"{context} entity save-format isolation")

    sample_pose = function(entity_cpp, "bool idEntity::SamplePresentationPose( void )", context)
    require(sample_pose, "presentationPoseTime == gameLocal.time", f"{context} one sample per authoritative tic")
    require(sample_pose, "const idVec3 simOrigin = renderEntity.origin;", f"{context} samples the composed visual transform")
    require(sample_pose, "const idMat3 simAxis = renderEntity.axis;", f"{context} samples the composed visual transform")
    reject(sample_pose, "GetPhysics()->GetOrigin()", f"{context} physics sampling drops viewAxis and modelOffset")
    reject(sample_pose, "GetPhysics()->GetAxis()", f"{context} physics sampling drops viewAxis and modelOffset")
    require(sample_pose, "common->GetUserCmdMsecFloat()", f"{context} sequential-tic guard")
    require(sample_pose, "gameLocal.GetMHz() == common->GetUserCmdHz()", f"{context} exact cadence guard")
    require(sample_pose, "PRESENTATION_POSE_STEP_TOLERANCE", f"{context} teleport guard scales with motion in flight")
    require(sample_pose, "RestoreAuthoritativePresentationPose();", f"{context} nothing interpolated left behind at rest")
    require(sample_pose, "presentationAnimator->IsAnimating( gameLocal.GetPreviousTime(), true )", f"{context} active root animation eligibility")
    require(sample_pose, "presentationAnimator->IsAnimating( gameLocal.time, true )", f"{context} current root animation eligibility")
    require(sample_pose, "cent->HasPresentationAnimation( gameLocal.GetPreviousTime(), gameLocal.time )", f"{context} bound client-model animation eligibility")
    stationary_pose = function(
        "void synthetic() {" + sample_pose + "}",
        "if ( simOrigin == presentationCurOrigin && simAxis == presentationCurAxis )",
        context,
    )
    require(stationary_pose, "sequentialFrame", f"{context} stationary animation retains continuous history")
    require(stationary_pose, "presentationPoseCanInterpolate && presentationAnimationActive", f"{context} stationary animation joins presentation list")
    reject(sample_pose, "Think(", f"{context} sampling runs no gameplay")
    reject(sample_pose, "RunPhysics", f"{context} sampling runs no gameplay")

    get_pose = function(entity_cpp, "bool idEntity::GetPresentationPose( idVec3 &origin, idMat3 &axis ) const", context)
    require(get_pose, "origin.Lerp", f"{context} entity position interpolation")
    require(get_pose, "InterpolatePresentationAxis", f"{context} entity axis interpolation")
    require(get_pose, "GetPresentationInterpolationFraction", f"{context} shared camera fraction")

    update_pose = function(entity_cpp, "void idEntity::UpdatePresentationPose( void )", context)
    require(update_pose, "GetPresentationPose(", f"{context} interpolated entity pose")
    require(update_pose, "gameLocal.GetPresentationAnimationTimeMsec()", f"{context} shared skeletal sample clock")
    require(update_pose, "animator->CreateFrame( gameLocal.time, false );", f"{context} current authoritative joints first")
    require(update_pose, "animator->CreatePresentationFrame( presentationTime, &presentationJoints )", f"{context} isolated skeletal sample")
    require(update_pose, "if ( !hasInterpolatedRoot && !hasPresentationJoints && clientEntities.IsListEmpty() )", f"{context} stationary animated/bound entity submission")
    require(update_pose, "renderEntity_t presentationRenderEntity = renderEntity;", f"{context} transient skeletal render copy")
    require(update_pose, "presentationRenderEntity.callback = NULL;", f"{context} scratch joints cannot be overwritten by callback")
    require(update_pose, "presentationRenderEntity.joints = presentationJoints;", f"{context} renderer receives scratch joints")
    require(update_pose, "GetPresentationBounds( presentationRenderEntity.hModel, presentationJoints, presentationRenderEntity.bounds )", f"{context} scratch joint bounds")
    require(update_pose, "UpdateEntityDef", f"{context} entity render resubmission")
    require(update_pose, "UpdatePresentationClientEntities( presentationTime );", f"{context} attached client entities follow the skeletal clock")
    require(update_pose, "presentationPoseHeld = true;", f"{context} attached visuals resolve against the drawn pose")
    require(update_pose, "presentationPoseHeld = false;", f"{context} drawn pose is released before the restore")
    require(update_pose, "animator->ClearPresentationFrame();", f"{context} scratch pose expires after attachments")
    require_before(update_pose, "presentationPoseHeld = true;", "UpdatePresentationClientEntities( presentationTime );", f"{context} attachment joint lookup while root is held")
    require_before(update_pose, "UpdatePresentationClientEntities( presentationTime );", "presentationPoseHeld = false;", f"{context} held attachment scope")
    require_before(update_pose, "UpdatePresentationClientEntities( presentationTime );", "animator->ClearPresentationFrame();", f"{context} attachment lookup before scratch expiry")
    require(update_pose, "renderEntity.origin = authoritativeOrigin;", f"{context} authoritative render pose restore")
    require(update_pose, "renderEntity.axis = authoritativeAxis;", f"{context} authoritative render pose restore")
    reject(update_pose, "GetPhysicsToVisualTransform", f"{context} visual transform must not be applied twice")
    reject(update_pose, "AddEntityDef", f"{context} never creates a render def on a draw frame")
    reject(update_pose, "Think(", f"{context} presentation-only entity pass")
    reject(update_pose, "RunPhysics", f"{context} presentation-only entity pass")
    reject(update_pose, "UpdateSound", f"{context} presentation-only entity pass")
    reject(update_pose, "SetOrigin", f"{context} physics and clip models stay authoritative")

    restore_pose = function(
        entity_cpp,
        "void idEntity::RestoreAuthoritativePresentationPose( void )",
        context,
    )
    require(restore_pose, "UpdatePresentationClientEntities( gameLocal.time );", f"{context} attached visuals restore to authoritative joints")

    entity_callback = function(
        entity_cpp,
        "bool idEntity::UpdateRenderEntity( renderEntity_s *renderEntity, const renderView_t *renderView )",
        context,
    )
    require(entity_callback, "animator->CreateFrame( gameLocal.time, false )", f"{context} normal callback remains authoritative")
    reject(entity_callback, "CreatePresentationFrame", f"{context} renderer callback cannot outlive scratch pose")
    reject(entity_callback, "GetPresentationJointTransform", f"{context} renderer callback cannot reuse expired scratch pose")

    presentation_joint_world = function(
        entity_cpp,
        "bool idAnimatedEntity::GetPresentationJointWorldTransform( jointHandle_t jointHandle, idVec3 &offset, idMat3 &axis )",
        context,
    )
    require(presentation_joint_world, "presentationPoseHeld", f"{context} presentation joint lookup root scope")
    require(presentation_joint_world, "gameLocal.GetPresentationAnimationTimeMsec() < 0", f"{context} presentation joint lookup scene scope")
    require(presentation_joint_world, "animator.GetPresentationJointTransform", f"{context} read-only scratch joint lookup")
    require(presentation_joint_world, "ConvertLocalToWorldTransform", f"{context} held root applied to scratch joint")
    reject(presentation_joint_world, "CreateFrame", f"{context} joint lookup never advances animation")

    light_pose = function(light_cpp, "void idLight::UpdatePresentationPose( void )", context)
    require(light_pose, "UpdateLightDef", f"{context} lights follow the drawn mover pose")
    require(light_pose, "localLightAxis * interpolatedAxis", f"{context} light axis from the drawn pose")
    require(light_pose, "renderLight.origin = authoritativeOrigin;", f"{context} authoritative light pose restore")

    view_weapon_optout = function(
        weapon_cpp,
        "bool rvViewWeapon::AllowsPresentationInterpolation( void ) const",
        context,
    )
    require(view_weapon_optout, "return false;", f"{context} viewmodel keeps its own presentation path")

    actor_policy = function(
        actor_cpp,
        "bool idActor::AllowsPresentationInterpolation( void ) const",
        context,
    )
    require(actor_policy, "gameLocal.isMultiplayer", f"{context} competitive actors stay on the simulation clock")
    # The exclusion is about remote actors. The client's own body is suppressed in
    # its owner's view and reaches that owner only as its own shadow, so holding it
    # on the 60 Hz tic staggered that shadow under an interpolated eye.
    require(actor_policy, "gameLocal.GetLocalPlayer()", f"{context} the local player is exempt from the multiplayer exclusion")
    require(actor_policy, "return false;", f"{context} multiplayer actor root/skeleton exclusion")
    # A spectator fires no shots, so MP-07 never rewinds against what it draws;
    # remote bodies may share its camera's clock only then.
    require(actor_policy, "!localPlayer->spectating", f"{context} remote bodies interpolate only for spectators")
    require(actor_policy, "idAFEntity_Gibbable::AllowsPresentationInterpolation()", f"{context} single-player actor eligibility retained")

    sample_sweep = function(
        game_local_cpp,
        "void idGameLocal::SamplePresentationEntityPoses( void )",
        context,
    )
    require(sample_sweep, "skipCinematic", f"{context} hidden fast-forward bypass")
    require(sample_sweep, "ClearPresentationEntityPoses();", f"{context} disabled/skip handback")
    require(sample_sweep, "presentationEntities.Next()", f"{context} previous-member cleanup sweep")
    require(sample_sweep, "activeEntities.Next()", f"{context} changed-candidate sweep")
    require(sample_sweep, "GetTeamMaster()", f"{context} active physics-team ownership")
    require(sample_sweep, "GetNextTeamEntity()", f"{context} bound team-member coverage")
    require(sample_sweep, "presentationNode.InList()", f"{context} duplicate sample guard")
    reject(sample_sweep, "spawnedEntities.Next()", f"{context} no static-map full sweep")
    require(sample_sweep, "g_presentationInterpolation", f"{context} user control")
    require(sample_sweep, "presentationNode.AddToEnd( presentationEntities );", f"{context} moving entities are listed")
    reject(sample_sweep, "Think(", f"{context} sampling runs no gameplay")

    update_sweep = function(
        game_local_cpp,
        "void idGameLocal::UpdatePresentationEntityPoses( void )",
        context,
    )
    require(update_sweep, "presentationNode.Next()", f"{context} removal-safe presentation walk")
    require(update_sweep, "UpdatePresentationPose();", f"{context} entity re-anchor")

    clear_sweep = function(
        game_local_cpp,
        "void idGameLocal::ClearPresentationEntityPoses( void )",
        context,
    )
    require(clear_sweep, "RestoreAuthoritativePresentationPose();", f"{context} authoritative handback")
    require(clear_sweep, "presentationNode.Remove();", f"{context} list teardown")

    carrier = function(player_cpp, "const idEntity *idPlayer::GetPresentationCarrier( void ) const", context)
    require(carrier, "vehicleController.IsDriving()", f"{context} driven vehicle carrier")
    require(carrier, "GetBindMaster()", f"{context} bound carrier")
    require(carrier, "physicsObj.GetGroundEntity()", f"{context} ridden carrier")

    can_interpolate_view = function(
        player_cpp,
        "bool idPlayer::CanInterpolatePresentationView( void ) const",
        context,
    )
    require(can_interpolate_view, "GetPresentationCarrier()", f"{context} carrier consistency")
    require(can_interpolate_view, "carrier->CanInterpolatePresentationPose()", f"{context} eye rides the carrier's clock")

    view_interpolated = function(
        player_cpp,
        "bool idPlayer::IsPresentationViewInterpolated( void ) const",
        context,
    )
    require(view_interpolated, "gameLocal.GetCamera()", f"{context} cinematic camera is authoritative")
    require(view_interpolated, "privateCameraView", f"{context} private camera is authoritative")
    require(view_interpolated, "pm_thirdPerson", f"{context} third person is authoritative")

    draw = function(game_local_cpp, "bool idGameLocal::Draw( int clientNum", context)
    require(draw, "PreparePlayerSceneForRender( player", f"{context} single-player draw path")
    require(draw, "EndPresentationSceneForRender();", f"{context} single-player scratch scope end")
    require_before(draw, "player->playerView.RenderPlayerView", "EndPresentationSceneForRender();", f"{context} single-player scratch survives rendering only")
    multiplayer_draw = function(
        multiplayer_cpp,
        "bool idMultiplayerGame::Draw( int clientNum )",
        context,
    )
    require(
        multiplayer_draw,
        "gameLocal.PreparePlayerSceneForRender( viewPlayer );",
        f"{context} multiplayer/spectator draw path",
    )
    if multiplayer_draw.count("gameLocal.EndPresentationSceneForRender();") < 2:
        raise AssertionError(f"Missing multiplayer early-return/final scene resets in {context}")
    render_index = multiplayer_draw.find("viewPlayer->playerView.RenderPlayerView")
    final_reset_index = multiplayer_draw.rfind("gameLocal.EndPresentationSceneForRender();")
    if render_index < 0 or final_reset_index <= render_index:
        raise AssertionError(f"Expected multiplayer final scene reset after rendering in {context}")

    for field in PRESENTATION_STATE:
        require(player_h, field, f"{context} player presentation state")
    require(player_h, "GetPresentationViewPos", f"{context} player presentation API")

    reset = function(player_cpp, "void idPlayer::ResetPresentationViewState( void )", context)
    require(reset, "presentationViewTime = -1;", f"{context} reset")
    restore = function(player_cpp, "void idPlayer::Restore( idRestoreGame *savefile )", context)
    require(restore, "ResetPresentationViewState();", f"{context} restore reseed")
    save = function(player_cpp, "void idPlayer::Save( idSaveGame *savefile ) const", context)
    for field in PRESENTATION_STATE:
        reject(save, field, f"{context} save-format isolation")

    update = function(player_cpp, "void idPlayer::UpdatePresentationViewState( void )", context)
    require(update, "presentationViewTime == gameLocal.time", f"{context} zero-tic endpoint retention")
    require(update, "common->GetUserCmdMsecFloat()", f"{context} sequential-tic guard")
    require(update, "originDelta.LengthSqr() <= Square( 32.0f )", f"{context} teleport guard")
    require(update, "angleDelta.Length() <= 90.0f", f"{context} view discontinuity guard")
    same_time_update = function(
        "void synthetic() {" + update + "}",
        "if ( presentationViewTime == gameLocal.time )",
        context,
    )
    require(same_time_update, "originDelta.LengthSqr() <= Square( 32.0f )", f"{context} same-tic teleport guard")
    require(same_time_update, "angleDelta.Length() <= 90.0f", f"{context} same-tic angle guard")
    require(same_time_update, "presentationCanInterpolate = false;", f"{context} same-tic history collapse")
    require(same_time_update, "presentationPrevViewOrigin = presentationCurViewOrigin;", f"{context} same-tic endpoint collapse")
    require(update, "gameLocal.GetMHz() == common->GetUserCmdHz()", f"{context} exact cadence guard")
    if source_root == "src/game":
        require(update, "!activePredictionViewSmoothing", f"{context} prediction smoothing guard")

    get_view = function(player_cpp, "void idPlayer::GetPresentationViewPos", context)
    require(get_view, "origin.Lerp", f"{context} position interpolation")
    require(get_view, "InterpolatePresentationAxis", f"{context} axis interpolation")

    render_view = function(player_cpp, "void idPlayer::CalculateRenderView( void )", context)
    require(render_view, "UpdatePresentationViewState();", f"{context} render-view endpoint capture")
    require(render_view, "GetPresentationViewPos", f"{context} first-person presentation pose")
    require(render_view, "GetPresentationFov()", f"{context} FOV interpolation")
    require(render_view, "renderView->time =", f"{context} presentation shader clock")
    # Late look (HF-03) samples the engine generator once per prepared draw and
    # only there; presentation_hf_contract.py pins the rest of that boundary.
    reject(player_cpp, "GetPresentationViewDelta", f"{context} retired pending-delta sampler")
    if player_cpp.count("usercmdGen->GetPresentationViewAngles") != 1:
        raise AssertionError(f"Expected one late-look input sample site in {context}")
    require(
        function(player_cpp, "void idPlayer::UpdatePresentationLook( void )", context),
        "usercmdGen->GetPresentationViewAngles",
        f"{context} input sampled only while freezing the draw's look",
    )

    require(weapon_h, "UpdatePresentationWeapon", f"{context} viewmodel presentation API")
    for field in PRESENTATION_WEAPON_STATE:
        require(weapon_h, field, f"{context} transient viewmodel state")

    weapon_save = function(weapon_cpp, "void rvWeapon::Save ( idSaveGame *savefile ) const", context)
    for field in PRESENTATION_WEAPON_STATE:
        reject(weapon_save, field, f"{context} weapon save-format isolation")
    weapon_restore = function(weapon_cpp, "void rvWeapon::Restore ( idRestoreGame *savefile )", context)
    require(weapon_restore, "ResetPresentationViewModelState();", f"{context} weapon restore reseed")

    update_viewmodel = function(
        weapon_cpp,
        "void rvWeapon::UpdatePresentationViewModelState",
        context,
    )
    require(update_viewmodel, "presentationViewModelTime == gameLocal.time", f"{context} viewmodel endpoint retention")
    require(update_viewmodel, "owner->CanInterpolatePresentationView()", f"{context} camera/viewmodel cadence pairing")
    require(update_viewmodel, "originDelta.LengthSqr() <= Square( 24.0f )", f"{context} viewmodel discontinuity guard")
    require(update_viewmodel, "angleDelta.Length() <= 70.0f", f"{context} viewmodel angle guard")
    same_time_viewmodel = function(
        "void synthetic() {" + update_viewmodel + "}",
        "if ( presentationViewModelTime == gameLocal.time )",
        context,
    )
    require(same_time_viewmodel, "originDelta.LengthSqr() <= Square( 24.0f )", f"{context} same-tic viewmodel origin guard")
    require(same_time_viewmodel, "angleDelta.Length() <= 70.0f", f"{context} same-tic viewmodel angle guard")
    require(same_time_viewmodel, "!continuousPose", f"{context} same-tic viewmodel collapse guard")
    require(same_time_viewmodel, "presentationViewModelCanInterpolate = false;", f"{context} same-tic viewmodel history collapse")

    get_viewmodel = function(
        weapon_cpp,
        "void rvWeapon::GetPresentationViewModelTransform",
        context,
    )
    require(get_viewmodel, "owner->GetPresentationViewPos", f"{context} camera/viewmodel alignment")
    require(get_viewmodel, "prevLocalOrigin", f"{context} camera-local viewmodel interpolation")
    require(get_viewmodel, "localOrigin.Lerp", f"{context} complete viewmodel pose interpolation")
    require(get_viewmodel, "InterpolatePresentationAxis", f"{context} viewmodel axis interpolation")

    update_weapon = function(
        weapon_cpp,
        "void rvViewWeapon::UpdatePresentationWeapon( bool showViewModel )",
        context,
    )
    require(update_weapon, "const idVec3 authoritativeOrigin", f"{context} authoritative pose preservation")
    require(update_weapon, "ApplyPresentationViewModelTransform();", f"{context} interpolated viewmodel pose")
    require(update_weapon, "SetOrigin( authoritativeOrigin );", f"{context} authoritative pose restore")
    require(update_weapon, "SetAxis( authoritativeAxis );", f"{context} authoritative pose restore")
    require(update_weapon, "RestoreAuthoritativeViewModelTransform();", f"{context} cached viewmodel pose restore")
    reject(update_weapon, "weapon->Think", f"{context} no duplicate weapon logic")
    reject(update_weapon, "UpdateSound", f"{context} no duplicate sound update")

    update_model = function(
        weapon_cpp,
        "void rvViewWeapon::UpdatePresentationModel( void )",
        context,
    )
    require(update_model, "renderEntity_t presentationRenderEntity = renderEntity;", f"{context} transient render copy")
    require(update_model, "gameLocal.GetPresentationAnimationTimeMsec()", f"{context} viewmodel shares skeletal clock")
    require(update_model, "presentationTime != gameLocal.time", f"{context} authoritative viewmodel fallback")
    require(update_model, "presentationAnimator->CreateFrame( gameLocal.time, false );", f"{context} current authoritative viewmodel joints first")
    require(update_model, "CreatePresentationFrame( presentationTime, &presentationJoints )", f"{context} isolated viewmodel skeletal sample")
    require(update_model, "presentationRenderEntity.callback = NULL;", f"{context} viewmodel scratch callback suppression")
    require(update_model, "presentationRenderEntity.joints = presentationJoints;", f"{context} viewmodel scratch joints")
    require(update_model, "GetPresentationBounds( presentationRenderEntity.hModel, presentationJoints, presentationRenderEntity.bounds )", f"{context} viewmodel scratch bounds")
    require(update_model, "UpdateEntityDef", f"{context} viewmodel render resubmission")
    require(update_model, "UpdatePresentationClientEntities( presentationTime );", f"{context} attached client entities follow the drawn pose")
    require(update_model, "weapon->UpdatePresentationEffects();", f"{context} weapon-driven view effects follow the drawn pose")
    require(update_model, "presentationPoseHeld = true;", f"{context} viewmodel attachment root held")
    require(update_model, "presentationPoseHeld = false;", f"{context} viewmodel attachment root released")
    require(update_model, "presentationAnimator->ClearPresentationFrame();", f"{context} viewmodel scratch expiry")
    require_before(update_model, "presentationPoseHeld = true;", "UpdatePresentationClientEntities( presentationTime );", f"{context} viewmodel attachment held scope")
    require_before(update_model, "UpdatePresentationClientEntities( presentationTime );", "weapon->UpdatePresentationEffects();", f"{context} held models precede held weapon effects")
    require_before(update_model, "weapon->UpdatePresentationEffects();", "presentationPoseHeld = false;", f"{context} viewmodel effects use held joints")
    require_before(update_model, "weapon->UpdatePresentationEffects();", "presentationAnimator->ClearPresentationFrame();", f"{context} effects query joints before scratch expiry")

    update_client_entities = function(
        weapon_cpp,
        "void rvViewWeapon::UpdatePresentationClientEntities( int presentationTime )",
        context,
    )
    require(update_client_entities, "clientEntities.Next()", f"{context} bound client entity walk")
    require(update_client_entities, "next = cent->bindNode.Next();", f"{context} removal-safe client entity walk")
    require(update_client_entities, "cent->UpdatePresentationTransform( presentationTime );", f"{context} client entity re-anchor")

    require(client_entity_h, "UpdatePresentationTransform\t( int presentationTime )", f"{context} presentation time bind API")
    require(client_entity_h, "HasPresentationAnimation", f"{context} bound animation eligibility API")
    require(client_entity_h, "UpdateBind\t\t\t( int presentationTime = -1 )", f"{context} authoritative default bind path")

    cent_transform = function(
        client_entity_cpp,
        "void rvClientEntity::UpdatePresentationTransform ( int presentationTime )",
        context,
    )
    require(cent_transform, "if ( !bindMaster )", f"{context} bound-only re-anchor")
    require(cent_transform, "UpdateBind( presentationTime );", f"{context} bind transform refresh")
    require(cent_transform, "Present();", f"{context} client entity render resubmission")
    reject(cent_transform, "UpdateSound", f"{context} no duplicate sound update")
    reject(cent_transform, "RunPhysics", f"{context} presentation-only client entity pass")

    cent_bind = function(
        client_entity_cpp,
        "void rvClientEntity::UpdateBind ( int presentationTime )",
        context,
    )
    require(cent_bind, "GetPresentationJointWorldTransform( bindJoint, worldOrigin, worldAxis )", f"{context} held scratch joint bind")
    require(cent_bind, "presentationTime < 0", f"{context} authoritative bind gate")
    require(cent_bind, "GetJointWorldTransform( bindJoint, gameLocal.time", f"{context} authoritative joint bind fallback")

    require(client_effect_h, "UpdatePresentationTransform\t( int presentationTime )", f"{context} effect presentation time API")
    require(client_effect_h, "UpdateBind\t\t( int presentationTime = -1 )", f"{context} authoritative effect bind default")

    effect_bind = function(
        client_effect_cpp,
        "void rvClientEffect::UpdateBind ( int presentationTime )",
        context,
    )
    require(effect_bind, "rvClientEntity::UpdateBind ( presentationTime );", f"{context} effect origin uses held joint")
    require(effect_bind, "GetPresentationJointWorldTransform( endOriginJoint, endOrigin, axis )", f"{context} effect endpoint uses held joint")
    require(effect_bind, "GetJointWorldTransform( endOriginJoint, gameLocal.time", f"{context} authoritative effect endpoint fallback")

    effect_transform = function(
        client_effect_cpp,
        "void rvClientEffect::UpdatePresentationTransform ( int presentationTime )",
        context,
    )
    require(effect_transform, "effectDefHandle < 0", f"{context} live effect def only")
    require(effect_transform, "UpdateBind( presentationTime );", f"{context} effect bind transform refresh")
    require(effect_transform, "UpdateEffectDef( effectDefHandle, &renderEffect, gameLocal.time )", f"{context} authoritative effect clock")
    reject(effect_transform, "FreeEffectDef", f"{context} effect lifetime stays in Think")
    reject(effect_transform, "PostEventMS", f"{context} effect lifetime stays in Think")

    require(client_model_h, "UpdatePresentationTransform( int presentationTime )", f"{context} animated client-model presentation API")
    require(client_model_h, "HasPresentationAnimation( int fromTime, int toTime ) const", f"{context} animated client-model eligibility API")
    require(client_model_h, "PresentPresentation( int presentationTime )", f"{context} animated client-model submission API")

    client_model_present = function(client_model_cpp, "void rvClientModel::Present(void)", context)
    require(client_model_present, "PresentPresentation( -1 );", f"{context} normal client-model submission is authoritative")

    client_model_presentation = function(
        client_model_cpp,
        "void rvClientModel::PresentPresentation( int presentationTime )",
        context,
    )
    require(client_model_presentation, "renderEntity_t presentationRenderEntity = renderEntity;", f"{context} transient client-model render copy")
    require(client_model_presentation, "submittedRenderEntity = &renderEntity", f"{context} authoritative client-model default")
    require(client_model_presentation, "presentationTime != gameLocal.time", f"{context} authoritative client-model restore path")
    require(client_model_presentation, "animator->CreateFrame( gameLocal.time, false );", f"{context} current authoritative client-model joints first")
    require(client_model_presentation, "CreatePresentationFrame( presentationTime, &presentationJoints )", f"{context} isolated client-model skeletal sample")
    require(client_model_presentation, "presentationRenderEntity.callback = NULL;", f"{context} client-model scratch callback suppression")
    require(client_model_presentation, "presentationRenderEntity.joints = presentationJoints;", f"{context} client-model scratch joints")
    require(client_model_presentation, "GetPresentationBounds( presentationRenderEntity.hModel, presentationJoints, presentationRenderEntity.bounds )", f"{context} client-model scratch bounds")
    require(client_model_presentation, "submittedRenderEntity = &presentationRenderEntity;", f"{context} client-model scratch submission")
    require(client_model_presentation, "animator->ClearPresentationFrame();", f"{context} client-model scratch expiry")

    client_model_transform = function(
        client_model_cpp,
        "void rvClientModel::UpdatePresentationTransform( int presentationTime )",
        context,
    )
    require(client_model_transform, "UpdateBind( presentationTime );", f"{context} client model follows held bind joint")
    require(client_model_transform, "PresentPresentation( presentationTime );", f"{context} client model samples the shared skeletal clock")

    client_model_active = function(
        client_model_cpp,
        "bool rvClientModel::HasPresentationAnimation( int fromTime, int toTime ) const",
        context,
    )
    require(client_model_active, "animator->IsAnimating( fromTime, true )", f"{context} previous client-model animation activity")
    require(client_model_active, "animator->IsAnimating( toTime, true )", f"{context} current client-model animation activity")

    client_model_callback = function(
        client_model_cpp,
        "bool rvClientModel::UpdateRenderEntity( renderEntity_s *renderEntity, const renderView_t *renderView )",
        context,
    )
    require(client_model_callback, "animator->CreateFrame( gameLocal.time, false )", f"{context} normal client-model callback remains authoritative")
    reject(client_model_callback, "CreatePresentationFrame", f"{context} callback cannot reuse expired client-model scratch")

    apply_transform = function(
        weapon_cpp,
        "void rvWeapon::ApplyPresentationViewModelTransform( void )",
        context,
    )
    require(apply_transform, "GetPresentationViewModelTransform", f"{context} cached viewmodel sample")
    require(apply_transform, "presentationRestoreViewModelOrigin = viewModelOrigin;", f"{context} authoritative cached pose stashed")
    require(apply_transform, "viewModelOrigin = presentationWeaponOrigin;", f"{context} joint queries see the drawn pose")
    require(apply_transform, "viewModelAxis = presentationWeaponAxis;", f"{context} joint queries see the drawn pose")

    restore_transform = function(
        weapon_cpp,
        "void rvWeapon::RestoreAuthoritativeViewModelTransform( void )",
        context,
    )
    require(restore_transform, "viewModelOrigin = presentationRestoreViewModelOrigin;", f"{context} cached pose restore")
    require(restore_transform, "viewModelAxis = presentationRestoreViewModelAxis;", f"{context} cached pose restore")

    global_joint = function(
        weapon_cpp,
        "bool rvWeapon::GetGlobalJointTransform ( bool view, const jointHandle_t jointHandle",
        context,
    )
    require(global_joint, "viewAnimator->GetPresentationJointTransform( jointHandle, origin, axis )", f"{context} viewweapon effects read scoped scratch joints")
    require(global_joint, "viewAnimator->GetJointTransform( jointHandle, gameLocal.time, origin, axis )", f"{context} viewweapon joint query authoritative fallback")
    require_before(global_joint, "GetPresentationJointTransform", "GetJointTransform", f"{context} presentation joint preferred only while valid")

    lightning_effects = function(
        lightning_gun_cpp,
        "void rvWeaponLightningGun::UpdatePresentationEffects( void )",
        context,
    )
    require(lightning_effects, "if ( !trailEffectView )", f"{context} never creates an effect on a draw frame")
    require(lightning_effects, "GetGlobalJointTransform( true, barrelJointView", f"{context} beam origin from the drawn barrel joint")
    require(lightning_effects, "SetEndOrigin( currentPath.origin );", f"{context} authoritative beam endpoint retained")
    require(lightning_effects, "UpdatePresentationTransform( gameLocal.GetPresentationAnimationTimeMsec() );", f"{context} beam re-push on skeletal clock")
    reject(lightning_effects, "TracePoint", f"{context} no draw-frame tracing")
    reject(lightning_effects, "UseAmmo", f"{context} no draw-frame gameplay")
    reject(lightning_effects, "PlayEffect", f"{context} no draw-frame effect creation")

    think = function(weapon_cpp, "void rvWeapon::Think ( void )", context)
    require(think, "CalculateViewModelTransform", f"{context} authoritative viewmodel calculation")
    require(think, "UpdatePresentationViewModelState", f"{context} authoritative viewmodel endpoint capture")
    viewmodel_capture = function(
        "void synthetic() {" + think + "}",
        "if ( gameLocal.isNewFrame || presentationViewModelTime < 0 )",
        context,
    )
    require(viewmodel_capture, "CalculateViewModelTransform", f"{context} viewmodel new-frame calculation gate")
    require(viewmodel_capture, "UpdatePresentationViewModelState", f"{context} viewmodel new-frame sample gate")

    projectile_prediction = function(
        projectile_cpp,
        "void idProjectile::ClientPredictionThink( void )",
        context,
    )
    require(projectile_prediction, "if ( !gameLocal.isNewFrame )", f"{context} projectile replay guard")
    require(projectile_prediction, "Think();", f"{context} projectile authoritative-frame update")
    if source_root == "src/game":
        projectile_snapshot = function(
            projectile_cpp,
            "void idProjectile::ReadFromSnapshot( const idBitMsgDelta &msg )",
            context,
        )
        require(projectile_snapshot, "case IMPACTED:", f"{context} projectile terminal snapshot handling")
        require(projectile_snapshot, "Create( ownerEnt, snapshotOrigin, snapshotDir );", f"{context} late projectile visual reconstruction")
        require(projectile_snapshot, "Explode( NULL, true );", f"{context} late projectile impact effect")
        require(projectile_snapshot, 'StartSound( "snd_fizzle"', f"{context} late projectile fizzle effect")

    return {
        "fraction": normalized(fraction),
        "axis": normalized(axis),
        "create_presentation": normalized(create_presentation),
        "clear_presentation": normalized(clear_presentation),
        "get_presentation_joint": normalized(get_presentation_joint),
        "reset": normalized(reset),
        "get_view": normalized(get_view),
        "update_viewmodel": normalized(update_viewmodel),
        "get_viewmodel": normalized(get_viewmodel),
        "update_weapon": normalized(update_weapon),
        "update_model": normalized(update_model),
        "update_client_entities": normalized(update_client_entities),
        "cent_transform": normalized(cent_transform),
        "cent_bind": normalized(cent_bind),
        "effect_bind": normalized(effect_bind),
        "effect_transform": normalized(effect_transform),
        "client_model_present": normalized(client_model_present),
        "client_model_presentation": normalized(client_model_presentation),
        "client_model_transform": normalized(client_model_transform),
        "client_model_active": normalized(client_model_active),
        "client_model_callback": normalized(client_model_callback),
        "apply_transform": normalized(apply_transform),
        "restore_transform": normalized(restore_transform),
        "global_joint": normalized(global_joint),
        "lightning_effects": normalized(lightning_effects),
        "sample_pose": normalized(sample_pose),
        "get_pose": normalized(get_pose),
        "update_pose": normalized(update_pose),
        "restore_pose": normalized(restore_pose),
        "entity_callback": normalized(entity_callback),
        "presentation_joint_world": normalized(presentation_joint_world),
        "model_transform": normalized(model_transform),
        "light_pose": normalized(light_pose),
        "sample_sweep": normalized(sample_sweep),
        "update_sweep": normalized(update_sweep),
        "clear_sweep": normalized(clear_sweep),
        "carrier": normalized(carrier),
        "actor_policy": normalized(actor_policy),
        "view_interpolated": normalized(view_interpolated),
    }


def main() -> None:
    # one game tree since CORE-07: there is no second copy to drift from
    for source_root in SOURCE_ROOTS:
        check_source_root(source_root)
    print("presentation_interpolation_contract: ok")


if __name__ == "__main__":
    main()
