// openQ4: the picture while a skipped cinematic catches up over several frames
// (idGameLocal::RunFrame, g_cinematicSkipFrameMsec). Included by PlayerView.cpp.
//
// The fast-forward never poses its intermediate states, so the frames between
// show the cinematic's last picture instead: captured once when the skip
// starts, softened, dimmed, with a small spinner turning in the corner so the
// wait reads as loading rather than a hang.
//
// The softening blurs the capture twice and keeps the result (a capture, a
// blurred copy of it, then each frame a wider blur of that copy). Each image is
// written once and read after that write, as the double-vision capture does, so
// neither renderer samples an image it then overwrites in the same frame. The
// blur adds weighted copies over black because a captured frame's alpha is not
// reliable.

namespace {

const char *const CINEMATIC_SKIP_SCENE_IMAGE = "_cinematicSkipSceneImage";
const char *const CINEMATIC_SKIP_BLUR_IMAGE = "_cinematicSkipBlurImage";
const char *const CINEMATIC_SKIP_SCENE = "_cinematicSkipScene";
const char *const CINEMATIC_SKIP_BLUR = "_cinematicSkipBlur";

// how long the picture takes to soften and the spinner to appear
const int CINEMATIC_SKIP_FADE_MSEC = 180;
// the picture's brightness once softened
const float CINEMATIC_SKIP_DIM = 0.65f;
// blur radii as fractions of the screen height: the capture's, then the copy's
const float CINEMATIC_SKIP_BLUR_FIRST = 0.005f;
const float CINEMATIC_SKIP_BLUR_SECOND = 0.011f;
// spinner: radius and inset from the bottom-right corner, fractions of the height
const float CINEMATIC_SKIP_SPINNER_RADIUS = 0.018f;
const float CINEMATIC_SKIP_SPINNER_INSET = 0.06f;
const int CINEMATIC_SKIP_SPINNER_DASHES = 12;
const int CINEMATIC_SKIP_SPINNER_TURN_MSEC = 960;

// A 13-tap disc: the centre, an inner ring of six and an outer ring of six
// turned half a step, weighted towards the centre (weights sum to one).
void CinematicSkip_DrawBlur( const idMaterial *material, float radiusPixels, float width, float height, float scale ) {
	static const float INNER_WEIGHT = 0.08f, OUTER_WEIGHT = 0.06f;
	const float centreWeight = 1.0f - 6.0f * ( INNER_WEIGHT + OUTER_WEIGHT );
	for ( int tap = 0; tap < 13; ++tap ) {
		float s = 0.0f, t = 0.0f, weight = centreWeight;
		if ( tap > 0 ) {
			const bool outer = tap > 6;
			const float angle = DEG2RAD( 60.0f * ( ( tap - 1 ) % 6 ) + ( outer ? 30.0f : 0.0f ) );
			const float radius = outer ? radiusPixels : radiusPixels * 0.5f;
			s = idMath::Cos( angle ) * radius / width;
			t = idMath::Sin( angle ) * radius / height;
			weight = outer ? OUTER_WEIGHT : INNER_WEIGHT;
		}
		const float c = weight * scale;
		renderSystem->SetColor4( c, c, c, 1.0f );
		// captures are stored bottom-up, as the pause-scene capture draws them
		renderSystem->DrawStretchPic( 0, 0, 640, 480, s, 1.0f + t, 1.0f + s, t, material );
	}
}

// A triangle in canvas units, wound the way the 2D pass keeps (see VRWristDisplay).
void CinematicSkip_DrawTri( const idVec2 &a, const idVec2 &b, const idVec2 &c, const idMaterial *material ) {
	const idVec2 st( 0.5f, 0.5f );
	const float cross = ( b.x - a.x ) * ( c.y - a.y ) - ( b.y - a.y ) * ( c.x - a.x );
	if ( cross > 0.0f ) {
		renderSystem->DrawStretchTri( a, c, b, st, st, st, material );
	} else {
		renderSystem->DrawStretchTri( a, b, c, st, st, st, material );
	}
}

// Twelve short dashes on a ring, the brightest leading and the rest trailing
// off behind it. Sized and placed in pixels so it stays round at any aspect.
void CinematicSkip_DrawSpinner( float width, float height, float opacity, int elapsedMsec ) {
	const idMaterial *white = declManager->FindMaterial( "_white" );
	const float toCanvasX = 640.0f / width, toCanvasY = 480.0f / height;
	const float radius = CINEMATIC_SKIP_SPINNER_RADIUS * height;
	const float inset = CINEMATIC_SKIP_SPINNER_INSET * height;
	const idVec2 centre( width - inset - radius, height - inset - radius );
	const float inner = radius * 0.55f, halfWidth = Max( 1.0f, radius * 0.11f );
	const bool still = cvarSystem->GetCVarBool( "ui_retainedReducedMotion" );
	const int lead = still ? 0 : ( elapsedMsec * CINEMATIC_SKIP_SPINNER_DASHES / CINEMATIC_SKIP_SPINNER_TURN_MSEC ) % CINEMATIC_SKIP_SPINNER_DASHES;
	// the interface's value colour: the marine family's, or The Awakening's ice
	const bool awakening = cvarSystem->GetCVarBool( "fs_awakeningActive" );
	const idVec3 colour = awakening ? idVec3( 0.471f, 0.871f, 1.0f ) : idVec3( 1.0f, 0.745f, 0.137f );
	for ( int i = 0; i < CINEMATIC_SKIP_SPINNER_DASHES; ++i ) {
		// how far this dash trails the lead, 0 (the lead) to 11; reduced motion holds an even ring
		const int behind = ( lead - i + CINEMATIC_SKIP_SPINNER_DASHES ) % CINEMATIC_SKIP_SPINNER_DASHES;
		const float level = still ? 0.6f : 1.0f - 0.85f * behind / static_cast<float>( CINEMATIC_SKIP_SPINNER_DASHES - 1 );
		const float angle = DEG2RAD( 360.0f * i / CINEMATIC_SKIP_SPINNER_DASHES - 90.0f );
		const idVec2 dir( idMath::Cos( angle ), idMath::Sin( angle ) );
		const idVec2 side( -dir.y * halfWidth, dir.x * halfWidth );
		const idVec2 p0 = centre + dir * inner, p1 = centre + dir * radius;
		idVec2 q[4] = { p0 - side, p1 - side, p1 + side, p0 + side };
		for ( int k = 0; k < 4; ++k ) {
			q[k].x *= toCanvasX;
			q[k].y *= toCanvasY;
		}
		renderSystem->SetColor4( colour.x, colour.y, colour.z, 0.8f * opacity * level );
		CinematicSkip_DrawTri( q[0], q[1], q[2], white );
		CinematicSkip_DrawTri( q[0], q[2], q[3], white );
	}
}

} // namespace

/*
===================
idPlayerView::DrawCinematicSkip

capture is set on the skip's first presented frame: the scene is rendered once
more (without the HUD or subtitles), captured and blurred into a kept copy.
===================
*/
void idPlayerView::DrawCinematicSkip( bool capture ) {
	const int width = renderSystem->GetScreenWidth(), height = renderSystem->GetScreenHeight();
	const int now = Sys_Milliseconds();
	if ( capture ) {
		cinematicSkipSceneValid = false;
		cinematicSkipStartMsec = now;
		renderView_t *view = player != NULL ? player->GetRenderView() : NULL;
		if ( view != NULL && width > 0 && height > 0 ) {
			idCamera *camera = player->GetPrivateCameraView();
			if ( camera == NULL ) {
				camera = gameLocal.GetCamera();
			}
			if ( camera != NULL ) {
				camera->GetViewParms( view );
			}
			idImageOpts options;
			options.width = width; options.height = height; options.format = FMT_RGBA8;
			options.numLevels = 1; options.isPersistant = true;
			// resolve both images before the scene is submitted (allocation can sync)
			if ( renderSystem->CreateImage( CINEMATIC_SKIP_SCENE_IMAGE, &options, TF_LINEAR ) != NULL &&
				renderSystem->CreateImage( CINEMATIC_SKIP_BLUR_IMAGE, &options, TF_LINEAR ) != NULL ) {
				const idMaterial *scene = declManager->FindMaterial( CINEMATIC_SKIP_SCENE );
				declManager->FindMaterial( CINEMATIC_SKIP_BLUR );
				SingleView( player->hud, view, RF_NO_GUI | RF_PRIMARY_VIEW );
				renderSystem->CaptureRenderToImage( CINEMATIC_SKIP_SCENE_IMAGE );
				const bool uiViewport = renderSystem->GetUseUIViewportFor2D();
				renderSystem->SetUseUIViewportFor2D( false );
				renderSystem->SetColor4( 0, 0, 0, 1 );
				renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 0, 1, 1, declManager->FindMaterial( "_white" ) );
				CinematicSkip_DrawBlur( scene, CINEMATIC_SKIP_BLUR_FIRST * height, width, height, 1.0f );
				renderSystem->CaptureRenderToImage( CINEMATIC_SKIP_BLUR_IMAGE );
				renderSystem->SetUseUIViewportFor2D( uiViewport );
				cinematicSkipSceneValid = true;
			}
		}
	}

	const bool calm = cvarSystem->GetCVarBool( "ui_retainedReducedMotion" );
	const int elapsed = Max( 0, now - cinematicSkipStartMsec );
	const float softened = calm ? 1.0f : idMath::ClampFloat( 0.0f, 1.0f, elapsed / static_cast<float>( CINEMATIC_SKIP_FADE_MSEC ) );
	const float ease = softened * softened * ( 3.0f - 2.0f * softened );

	const bool uiViewport = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D( false );
	const idMaterial *white = declManager->FindMaterial( "_white" );
	renderSystem->SetColor4( 0, 0, 0, 1 );
	renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 0, 1, 1, white );
	if ( cinematicSkipSceneValid && width > 0 && height > 0 ) {
		// the sharp frame softens into the blurred copy, dimming as it goes
		const float brightness = 1.0f - ( 1.0f - CINEMATIC_SKIP_DIM ) * ease;
		CinematicSkip_DrawBlur( declManager->FindMaterial( CINEMATIC_SKIP_BLUR ), CINEMATIC_SKIP_BLUR_SECOND * height,
			width, height, ease * brightness );
		if ( ease < 1.0f ) {
			const float c = ( 1.0f - ease ) * brightness;
			renderSystem->SetColor4( c, c, c, 1.0f );
			renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 1, 1, 0, declManager->FindMaterial( CINEMATIC_SKIP_SCENE ) );
		}
	}
	if ( width > 0 && height > 0 ) {
		CinematicSkip_DrawSpinner( static_cast<float>( width ), static_cast<float>( height ), ease, elapsed );
	}
	renderSystem->SetColor4( 1, 1, 1, 1 );
	renderSystem->SetUseUIViewportFor2D( uiViewport );
}
