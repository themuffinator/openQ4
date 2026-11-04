/*
 * Scene-local camera response; see LICENSES/QUAKE-4-SDK-EULA.rtf.
 * The script owns saved cue times in its helper's spawnArgs. No new save bytes,
 * random integration, camera-entity mutation or player-view shake state.
 */
static idCVar g_debugM01Feedback( "g_debugM01Feedback", "0", CVAR_GAME | CVAR_BOOL,
	"Log Awakening M01 camera response and scene cue state" );

static void Awakening_M01CameraFeedback( renderView_t *view ) {
	if ( !view || gameLocal.isMultiplayer || !gameLocal.GetCamera() ||
		idStr::Icmp( cvarSystem->GetCVarString( "fs_game" ), "q4xbase" ) != 0 ) {
		return;
	}
	const idEntity *cue = gameLocal.FindEntity( "oq4_m01_feedback" );
	if ( !cue || !cue->spawnArgs.GetBool( "feedback_active" ) ) {
		return;
	}
	// Even a VR configuration without an active headset retains its comfort choice.
	const float amount = cvarSystem->GetCVarBool( "vr_enable" ) ? 0.0f :
		idMath::ClampFloat( 0.0f, 1.0f, cvarSystem->GetCVarFloat( "ui_cinematicShake" ) );
	const float now = MS2SEC( gameLocal.GetPresentationTimeMsec() );
	const float age = Max( 0.0f, now - cue->spawnArgs.GetFloat( "feedback_started" ) );
	const float flight = idMath::ClampFloat( 0.0f, 0.3f, cue->spawnArgs.GetFloat( "feedback_flight" ) );
	idAngles offset( flight * 0.4f * idMath::Sin( age * 2.1f ),
		flight * 0.3f * idMath::Sin( age * 1.7f ), flight * idMath::Sin( age * 1.3f ) );
	const float duration = idMath::ClampFloat( 0.1f, 2.0f, cue->spawnArgs.GetFloat( "feedback_duration", "1" ) );
	const float elapsed = now - cue->spawnArgs.GetFloat( "feedback_impact", "-100" );
	if ( elapsed >= 0.0f && elapsed < duration ) {
		const float u = elapsed / duration;
		const float strength = idMath::ClampFloat( 0.0f, 3.0f, cue->spawnArgs.GetFloat( "feedback_strength" ) );
		const float envelope = strength * ( 1.0f - u ) * ( 1.0f - u );
		const float direction = cue->spawnArgs.GetBool( "feedback_left" ) ? -1.0f : 1.0f;
		offset.pitch += envelope * idMath::Sin( u * idMath::TWO_PI * 2.0f );
		offset.yaw += envelope * direction * 0.35f * idMath::Sin( u * idMath::TWO_PI * 1.5f );
		offset.roll += envelope * direction * 0.7f * idMath::Sin( u * idMath::TWO_PI * 2.5f );
	}
	offset *= amount;
	// Called once, after the final authored-camera refresh. Angular movement keeps
	// the camera at its authored safe position; no near-wall translation/clipping.
	view->viewaxis = offset.ToMat3() * view->viewaxis;
	if ( g_debugM01Feedback.GetBool() ) {
		static int lastTrace = -1;
		if ( gameLocal.time < lastTrace || gameLocal.time - lastTrace >= 64 ) {
			lastTrace = gameLocal.time;
			gameLocal.Printf( "M01_FEEDBACK time=%d phase=%s amount=%.2f angles=%.4f,%.4f,%.4f impact=%.3f\n",
				gameLocal.time, cue->spawnArgs.GetString( "feedback_phase" ), amount,
				offset.pitch, offset.yaw, offset.roll, elapsed );
		}
	}
}
