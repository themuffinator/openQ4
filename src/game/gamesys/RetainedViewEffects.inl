// openQ4 additions to the SDK game sources; see src/game/LICENSE.
// Shared SP/MP implementation, included after the local Game_local.h.

static float RetainedViewEase( float amount ) {
	amount = idMath::ClampFloat( 0, 1, amount );
	return amount * amount * ( 3 - 2 * amount );
}

static bool RetainedCrashCamera() {
	if ( gameLocal.isMultiplayer ) return false;
	idCamera *camera = gameLocal.GetCamera();
	if ( !camera || idStr::Icmp( camera->GetName(), "cin_opening" ) ) return false;
	idStr map = gameLocal.GetMapName(); map.StripFileExtension(); map.StripPath();
	return !map.Icmp( "airdefense1" );
}

idPlayerView::~idPlayerView() {
	ResetSubtitles();
	if ( presentationFx ) uiManager->DeAlloc( presentationFx );
}

void idPlayerView::ResetRetainedViewEffects() {
	ResetSubtitles();
	deathFxStart = -1;
	recoveryFxUntil = 0;
	recoverySequence = false;
	crashFxStart = -1;
	crashFxDuration = 0;
	crashFxClosing = false;
	if ( presentationFx ) { uiManager->DeAlloc( presentationFx ); presentationFx = NULL; }
}

void idPlayerView::RetainedRecoveryFade( const idVec4 &color, int duration ) {
	if ( !player || gameLocal.isMultiplayer || !cvarSystem->GetCVarBool( "ui_retained" ) ) return;
	// The crash's authored black fades open and close Kane's eyes three
	// times. Scope this to the wake-up camera, not the preceding dropship
	// cinematic or later gameplay fades. Keep all stock fade deadlines.
	if ( RetainedCrashCamera() && color[0] < .01f && color[1] < .01f && color[2] < .01f && duration > 0 ) {
		const bool closing = color[3] > .99f;
		if ( color[3] < .01f || ( closing && crashFxStart >= 0 ) ) {
			crashFxStart = gameLocal.time;
			crashFxDuration = duration;
			crashFxClosing = closing;
			if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) gameLocal.Printf( "RETAINED_CRASH %s time=%d duration=%d\n", closing ? "close" : "wake", gameLocal.time, duration );
		}
		return;
	}
	idStr map = gameLocal.GetMapName(); map.StripFileExtension(); map.StripPath();
	if ( map.Icmp( "mcc_2" ) ) return;
	const bool inBed = player->IsInVehicle() && player->GetVehicleController().GetVehicle() &&
		!idStr::Icmp( player->GetVehicleController().GetVehicle()->GetName(), "mcc2_playerBed" );
	if ( inBed ) recoverySequence = true;
	// Only the scripted treatment sequence qualifies. Its white fade-ins
	// remain intact; acuity takes a little longer to recover after each blink.
	if ( recoverySequence && color[0] > .95f && color[1] > .95f && color[2] > .95f && color[3] < .01f ) {
		recoveryFxUntil = gameLocal.time + Max( duration, 0 ) + 4000;
		// The last treatment fade follows the bed exit. Consume that sequence
		// here so subsequent white fades elsewhere in the map remain unchanged.
		if ( !inBed ) recoverySequence = false;
		if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) gameLocal.Printf( "RETAINED_RECOVERY until=%d inBed=%d\n", recoveryFxUntil, inBed );
	}
}

void idPlayerView::DrawRetainedViewEffects( bool wheelOnly ) {
	if ( !player || player->spectating || !cvarSystem->GetCVarBool( "ui_retained" ) ) {
		ResetRetainedViewEffects(); return;
	}
	const bool calm = cvarSystem->GetCVarBool( "ui_retainedReducedMotion" ) || cvarSystem->GetCVarBool( "ui_reducedFlashing" );
	float death = 0, recovery = 0, shock = 0, wheel = 0;
	float deathFocus = 0, deathTunnel = 0, crashFocus = 0, crashLids = 0;
	if ( wheelOnly ) {
		wheel = idMath::ClampFloat( 0, 1, player->GetWeaponWheelBlend() );
		wheel = wheel * wheel * ( 3 - 2 * wheel );
	} else {
		const int now = gameLocal.time;
		const bool dead = player->health <= 0;
		if ( !dead ) deathFxStart = -1;
		else if ( deathFxStart < 0 || now < deathFxStart ) deathFxStart = now;
		death = dead ? idMath::ClampFloat( 0, 1, ( now - deathFxStart ) / 1800.0f ) : 0;
		death = death * death * ( 3 - 2 * death );
		const float elapsed = dead ? ( now - deathFxStart ) / 1000.0f : 0;
		if ( dead ) {
			// One loss of acuity settles into the quieter death grade. The
			// inner peripheral shade arrives later and leaves the center clear.
			deathTunnel = RetainedViewEase( ( elapsed - .3f ) / 2.1f );
			if ( !calm ) {
				shock = .34f * RetainedViewEase( elapsed / .12f ) * ( 1 - RetainedViewEase( ( elapsed - .12f ) / 1.28f ) );
				deathFocus = RetainedViewEase( elapsed / .32f ) * ( 1 - RetainedViewEase( ( elapsed - .32f ) / 1.25f ) );
			}
		}
		if ( dead || !RetainedCrashCamera() || now < crashFxStart ) {
			crashFxStart = -1;
		} else if ( crashFxStart >= 0 ) {
			const float sinceFade = ( now - crashFxStart ) / 1000.0f;
			const float opening = 1 - RetainedViewEase( ( now - crashFxStart ) / ( crashFxDuration + 2300.0f ) );
			// A single small setback as vision returns, never a looping pulse.
			const float falter = calm ? 0 : .16f * RetainedViewEase( ( sinceFade - 1.8f ) / .7f ) *
				( 1 - RetainedViewEase( ( sinceFade - 2.5f ) / 1.6f ) );
			crashFocus = crashFxClosing ? RetainedViewEase( ( now - crashFxStart ) / float( Max( 1, crashFxDuration ) ) ) :
				idMath::ClampFloat( 0, 1, opening + falter );
			crashLids = crashFxClosing ? crashFocus : opening;
		}
		recovery = dead ? 0 : idMath::ClampFloat( 0, 1, ( recoveryFxUntil - now ) / 6000.0f );
		recovery = recovery * recovery * ( 3 - 2 * recovery );
	}
	if ( death <= 0 && recovery <= 0 && shock <= 0 && wheel <= 0 && crashFocus <= 0 && crashLids <= 0 ) return;
	if ( !presentationFx ) presentationFx = uiManager->FindGui( "guis/hud/view_effects.rui", true, true );
	if ( !presentationFx ) return;
	presentationFx->SetStateFloat( "death_amount", death );
	presentationFx->SetStateFloat( "death_shock", shock );
	presentationFx->SetStateFloat( "death_focus", deathFocus );
	presentationFx->SetStateFloat( "death_tunnel", deathTunnel );
	presentationFx->SetStateFloat( "crash_focus", crashFocus );
	presentationFx->SetStateFloat( "crash_lids", crashLids );
	presentationFx->SetStateFloat( "recovery_amount", recovery );
	// Separate submissions share a document but never blur the HUD or apply
	// recovery/death twice when InfluenceVision has already drawn its GUI.
	presentationFx->SetStateFloat( "wheel_amount", wheel );
	presentationFx->StateChanged( gameLocal.time );
	presentationFx->Redraw( gameLocal.time );
}
