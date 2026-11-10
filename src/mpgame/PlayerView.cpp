// RAVEN BEGIN
// bdube: note that this file is no longer merged with Doom3 updates
//
// MERGE_DATE 07/07/2004

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Game_local.h"
#include "../game/gamesys/RetainedViewEffects.inl"
#include "../game/gamesys/Subtitles.inl"

#if defined(_XENON) && (defined(_PROFILE) || defined(_DEBUG))
#include "../sys/xenon/ProfilingSupport.h"
#endif

const int IMPULSE_DELAY = 150;

/*
======================
GetAspectCorrectScreenRect
======================
*/
static void GetAspectCorrectScreenRect( float &x, float &y, float &w, float &h ) {
	const float baseAspect = ( float )SCREEN_WIDTH / ( float )SCREEN_HEIGHT;
	const float currentAspect = idMath::ClampFloat( 0.1f, 10.0f, gameLocal.GetScreenAspectRatio() );

	if ( currentAspect >= baseAspect ) {
		w = SCREEN_HEIGHT * currentAspect;
		h = SCREEN_HEIGHT;
		x = ( SCREEN_WIDTH - w ) * 0.5f;
		y = 0.0f;
	} else {
		w = SCREEN_WIDTH;
		h = SCREEN_WIDTH / currentAspect;
		x = 0.0f;
		y = ( SCREEN_HEIGHT - h ) * 0.5f;
	}
}

/*
======================
DrawAspectCorrectFullscreenMaterial
======================
*/
static void DrawAspectCorrectFullscreenMaterial( const idMaterial *material ) {
	float x;
	float y;
	float w;
	float h;

	if ( material == NULL ) {
		return;
	}

	GetAspectCorrectScreenRect( x, y, w, h );
	renderSystem->DrawStretchPic( x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, material );
}
/*
==============
idPlayerView::idPlayerView
==============
*/
idPlayerView::idPlayerView() {
	memset( screenBlobs, 0, sizeof( screenBlobs ) );
	memset( &view, 0, sizeof( view ) );
	player = NULL;
	dvMaterial = NULL;
	dvMaterialBlend = NULL;
	tunnelMaterial = NULL;
	armorMaterial = NULL;
	bloodSprayMaterial = NULL;
	bfgVision = false;
	dvFinishTime = 0;
	tvFinishTime = 0;
	tvStartTime = 0;
	kickFinishTime = 0;
	kickAngles.Zero();
	lastDamageTime = 0.0f;
	fadeTime = 0;
	fadeRate = 0.0;
	fadeFromColor.Zero();
	fadeToColor.Zero();
	fadeColor.Zero();

	ClearEffects();

	dvScale = 1.0f;
	shakeScale = 1.0f;
	tvScale = 1.0f;

// openQ4 BEGIN
	liquidViewAmount = 0.0f;
	liquidViewContents = 0;
	vrComfort = 0.0f;
	vrComfortYaw = 0.0f;
	vrComfortTime = 0;
// openQ4 END
}

/*
==============
idPlayerView::Save
==============
*/
void idPlayerView::Save( idSaveGame *savefile ) const {
	int i;
	const screenBlob_t *blob;

	blob = &screenBlobs[ 0 ];
	for( i = 0; i < MAX_SCREEN_BLOBS; i++, blob++ ) {
		savefile->WriteMaterial( blob->material );
		savefile->WriteFloat( blob->x );
		savefile->WriteFloat( blob->y );
		savefile->WriteFloat( blob->w );
		savefile->WriteFloat( blob->h );
		savefile->WriteFloat( blob->s1 );
		savefile->WriteFloat( blob->t1 );
		savefile->WriteFloat( blob->s2 );
		savefile->WriteFloat( blob->t2 );
		savefile->WriteInt( blob->finishTime );
		savefile->WriteInt( blob->startFadeTime );
		savefile->WriteFloat( blob->driftAmount );
	}

	savefile->WriteInt( dvFinishTime );
	savefile->WriteMaterial( dvMaterial );

	savefile->WriteMaterial( dvMaterialBlend );	// cnicholson: Added unsaved var

	savefile->WriteFloat ( dvScale );

	savefile->WriteInt( kickFinishTime );
	savefile->WriteAngles( kickAngles );

	savefile->WriteBool( bfgVision );

	savefile->WriteMaterial( tunnelMaterial );
	savefile->WriteMaterial( armorMaterial );

	savefile->WriteMaterial( bloodSprayMaterial );

	savefile->WriteFloat( lastDamageTime );

	savefile->WriteFloat( shakeFinishTime );
	savefile->WriteFloat( shakeScale );
	savefile->WriteFloat( tvScale );
	savefile->WriteInt( tvFinishTime );
	savefile->WriteInt( tvStartTime );

	savefile->WriteVec4( fadeColor );
	savefile->WriteVec4( fadeToColor );
	savefile->WriteVec4( fadeFromColor );
	savefile->WriteFloat( fadeRate );
	savefile->WriteInt( fadeTime );

	savefile->WriteObject( player );
	savefile->WriteRenderView( view );
}

/*
==============
idPlayerView::Restore
==============
*/
void idPlayerView::Restore( idRestoreGame *savefile ) {
	ResetRetainedViewEffects();
	int i;
	screenBlob_t *blob;

	blob = &screenBlobs[ 0 ];
	for( i = 0; i < MAX_SCREEN_BLOBS; i++, blob++ ) {
		savefile->ReadMaterial( blob->material );
		savefile->ReadFloat( blob->x );
		savefile->ReadFloat( blob->y );
		savefile->ReadFloat( blob->w );
		savefile->ReadFloat( blob->h );
		savefile->ReadFloat( blob->s1 );
		savefile->ReadFloat( blob->t1 );
		savefile->ReadFloat( blob->s2 );
		savefile->ReadFloat( blob->t2 );
		savefile->ReadInt( blob->finishTime );
		savefile->ReadInt( blob->startFadeTime );
		savefile->ReadFloat( blob->driftAmount );
	}

	savefile->ReadInt( dvFinishTime );
	savefile->ReadMaterial( dvMaterial );

	savefile->ReadMaterial( dvMaterialBlend );	// cnicholson: Added unrestored var

	savefile->ReadFloat( dvScale );

	savefile->ReadInt( kickFinishTime );
	savefile->ReadAngles( kickAngles );			

	savefile->ReadBool( bfgVision );

	savefile->ReadMaterial( tunnelMaterial );
	savefile->ReadMaterial( armorMaterial );

	savefile->ReadMaterial( bloodSprayMaterial );

	savefile->ReadFloat( lastDamageTime );

	savefile->ReadFloat( shakeFinishTime );
	savefile->ReadFloat( shakeScale );
	savefile->ReadFloat( tvScale );
	savefile->ReadInt( tvFinishTime );
	savefile->ReadInt ( tvStartTime );

	savefile->ReadVec4( fadeColor );
	savefile->ReadVec4( fadeToColor );
	savefile->ReadVec4( fadeFromColor );
	savefile->ReadFloat( fadeRate );
	savefile->ReadInt( fadeTime );

	savefile->ReadObject( player );
	savefile->ReadRenderView( view );
}

/*
==============
idPlayerView::SetPlayerEntity
==============
*/
void idPlayerView::SetPlayerEntity( idPlayer *playerEnt ) {
	player = playerEnt;

	const idDict* dict = NULL;
	if( !playerEnt ) {
		return;
	}

	const char *playerViewDef = playerEnt->spawnArgs.GetString( "def_playerView" );
	dict = gameLocal.FindEntityDefDict( playerViewDef, false );
	if ( !dict ) {
		dict = gameLocal.FindEntityDefDict( "playerView_marine", false );
	}

	if( dict ) {
		dvMaterial = declManager->FindMaterial( dict->GetString("mtr_doubleVision") );
		dvMaterialBlend = declManager->FindMaterial( dict->GetString("mtr_doubleVisionBlend") );
		tunnelMaterial = declManager->FindMaterial( dict->GetString("mtr_tunnel") );
		armorMaterial = declManager->FindMaterial( dict->GetString("mtr_armourEffect") );
		bloodSprayMaterial = declManager->FindMaterial( dict->GetString("mtr_bloodspray") );
	}
}

/*
==============
idPlayerView::ClearEffects
==============
*/
void idPlayerView::ClearEffects() {
	ResetRetainedViewEffects();
	lastDamageTime = MS2SEC( gameLocal.time - 99999 );

	dvFinishTime = ( gameLocal.time - 99999 );
	tvFinishTime = ( gameLocal.time - 99999 );
	tvStartTime = ( gameLocal.time - 99999 );
	kickFinishTime = ( gameLocal.time - 99999 );
	shakeFinishTime = gameLocal.time; 

	for ( int i = 0 ; i < MAX_SCREEN_BLOBS ; i++ ) {
		screenBlobs[i].finishTime = gameLocal.time;
	}

	fadeTime = 0;
	bfgVision = false;
}

/*
==============
idPlayerView::GetScreenBlob
==============
*/
screenBlob_t *idPlayerView::GetScreenBlob() {
	screenBlob_t	*oldest = &screenBlobs[0];

	for ( int i = 1 ; i < MAX_SCREEN_BLOBS ; i++ ) {
		if ( screenBlobs[i].finishTime < oldest->finishTime ) {
			oldest = &screenBlobs[i];
		}
	}
	return oldest;
}

/*
==============
idPlayerView::DamageImpulse

LocalKickDir is the direction of force in the player's coordinate system,
which will determine the head kick direction
==============
*/
// RAVEN BEGIN
// jnewquist: Controller rumble
void idPlayerView::DamageImpulse( idVec3 localKickDir, const idDict *damageDef, int damage, const char *damageDefName ) {
	//
	// double vision effect
	//
	float tvTime = damageDef->GetFloat( "tv_time" );
	if ( tvTime ) {
		tvStartTime = gameLocal.time;
		tvFinishTime = gameLocal.time + tvTime;		
		damageDef->GetFloat ( "tv_scale", "1", tvScale );
	}

	if ( lastDamageTime > 0.0f && SEC2MS( lastDamageTime ) + IMPULSE_DELAY > gameLocal.time ) {
		// keep shotgun from obliterating the view
		return;
	}

	//
	// double vision effect
	//
	float dvTime = damageDef->GetFloat( "dv_time" );
	if ( dvTime ) {
		dvFinishTime = gameLocal.time + (g_dvTime.GetFloat() * dvTime);
		damageDef->GetFloat ( "dv_scale", "1", dvScale );
	}

	//
	// head angle kick
	//
	const float	modifierScale = 0.25f;
	const float inverseModifier = ( 1.0f - modifierScale );

	float		modifier = idMath::ClampFloat( 0.0f, inverseModifier, damage / 100.0f * inverseModifier ) + modifierScale;
	float	kickTime = damageDef->GetFloat( "kick_time" );

	if ( kickTime ) {
		kickFinishTime = gameLocal.time + g_kickTime.GetFloat() * kickTime;

		// forward / back kick will pitch view
		kickAngles[0] = localKickDir[0];

		// side kick will yaw view
		kickAngles[1] = localKickDir[1]*0.5f;

		// up / down kick will pitch view
		kickAngles[0] += localKickDir[2];

		// roll will come from  side
		kickAngles[2] = localKickDir[1];

		float kickAmplitude = damageDef->GetFloat( "kick_amplitude" );
		if ( kickAmplitude ) {
			kickAngles *= kickAmplitude;
		}

		if ( modifier < kickAmplitude ) {
			modifier = kickAmplitude;
		}
	}
	else {
		kickTime = 500;
	}

	//
	// screen blob
	//
	bool meleeDamageDef = ( damageDef->FindKey( "snd_hit" ) != NULL ) || ( damageDef->FindKey( "snd_miss" ) != NULL );
	if ( !meleeDamageDef && damageDefName != NULL ) {
		meleeDamageDef = idStr::Icmpn( damageDefName, "melee_", 6 ) == 0;
	}

	const char *configuredBlobMaterial = damageDef->GetString( "mtr_blob" );
	if ( !configuredBlobMaterial[0] ) {
		configuredBlobMaterial = damageDef->GetString( "blob_material" );
	}
	const float configuredBlobX = damageDef->GetFloat( "blob_x" );
	const float configuredBlobY = damageDef->GetFloat( "blob_y" );
	const float configuredBlobOffsetX = damageDef->GetFloat( "blob_offset_x" );
	const float configuredBlobOffsetY = damageDef->GetFloat( "blob_offset_y" );
	const float configuredBlobWidth = damageDef->GetFloat( "blob_width" );
	const float configuredBlobHeight = damageDef->GetFloat( "blob_height" );
	const float configuredBlobSize = damageDef->GetFloat( "blob_size" );
	const bool hasBlobConfig = configuredBlobMaterial[0] ||
		( configuredBlobX != 0.0f ) || ( configuredBlobY != 0.0f ) ||
		( configuredBlobOffsetX != 0.0f ) || ( configuredBlobOffsetY != 0.0f ) ||
		( configuredBlobWidth > 0.0f ) || ( configuredBlobHeight > 0.0f ) ||
		( configuredBlobSize > 0.0f );

	float blobTime = damageDef->GetFloat( "blob_time" );
	bool synthesizedMeleeBlob = false;
	if ( blobTime <= 0.0f && meleeDamageDef && !hasBlobConfig ) {
		// Compatibility fallback for melee defs that never configured blob visuals.
		blobTime = 500.0f;
		synthesizedMeleeBlob = true;
	}

	if ( blobTime > 0.0f ) {
		screenBlob_t	*blob = GetScreenBlob();
		blob->startFadeTime = gameLocal.time;
		blob->finishTime = gameLocal.time + blobTime * g_blobTime.GetFloat();

		const char *materialName = configuredBlobMaterial;

		const idMaterial *blobMaterial = NULL;
		if ( materialName[0] ) {
			blobMaterial = declManager->FindMaterial( materialName, false );
			if ( blobMaterial == NULL && strchr( materialName, '/' ) == NULL && strchr( materialName, '\\' ) == NULL ) {
				const idStr materialPath = va( "textures/decals/%s", materialName );
				blobMaterial = declManager->FindMaterial( materialPath, false );
			}
		}
		if ( blobMaterial == NULL ) {
			if ( synthesizedMeleeBlob && bloodSprayMaterial != NULL ) {
				blobMaterial = bloodSprayMaterial;
			} else {
				blobMaterial = declManager->FindMaterial( "textures/decals/genericdamage" );
			}
		}
		blob->material = blobMaterial;

		float blobX = configuredBlobX;
		if ( blobX == 0.0f ) {
			blobX = configuredBlobOffsetX;
		}
		blob->x = blobX;
		blob->x += ( gameLocal.random.RandomInt()&63 ) - 32;
		float blobY = configuredBlobY;
		if ( blobY == 0.0f ) {
			blobY = configuredBlobOffsetY;
		}
		blob->y = blobY;
		blob->y += ( gameLocal.random.RandomInt()&63 ) - 32;

		float blobWidth = configuredBlobWidth;
		float blobHeight = configuredBlobHeight;
		if ( blobWidth <= 0.0f || blobHeight <= 0.0f ) {
			if ( configuredBlobSize > 0.0f ) {
				if ( blobWidth <= 0.0f ) {
					blobWidth = configuredBlobSize;
				}
				if ( blobHeight <= 0.0f ) {
					blobHeight = configuredBlobSize;
				}
			}
		}
		if ( blobWidth <= 0.0f || blobHeight <= 0.0f ) {
			if ( synthesizedMeleeBlob && bloodSprayMaterial != NULL ) {
				blobWidth = 600.0f;
				blobHeight = 480.0f;
			} else {
				blobWidth = 400.0f;
				blobHeight = 400.0f;
			}
		}
		
		float scale = ( 256 + ( ( gameLocal.random.RandomInt()&63 ) - 32 ) ) / 256.0f;
		blob->w = blobWidth * g_blobSize.GetFloat() * scale;
		blob->h = blobHeight * g_blobSize.GetFloat() * scale;
		blob->s1 = 0;
		blob->t1 = 0;
		blob->s2 = 1;
		blob->t2 = 1;
	}

	//
	// save lastDamageTime for tunnel vision accentuation
	//
	lastDamageTime = MS2SEC( gameLocal.time );
}
// RAVEN END

/*
==================
idPlayerView::AddBloodSpray

If we need a more generic way to add blobs then we can do that
but having it localized here lets the material be pre-looked up etc.
==================
*/
void idPlayerView::AddBloodSpray( float duration ) {
	if ( duration <= 0 || bloodSprayMaterial == NULL || g_skipViewEffects.GetBool() ) {
		return;
	}
	// visit this for chainsaw
	screenBlob_t *blob = GetScreenBlob();
	blob->startFadeTime = gameLocal.time;
	blob->finishTime = gameLocal.time + ( duration * 1000 );
	blob->material = bloodSprayMaterial;
	blob->x = ( gameLocal.random.RandomInt() & 63 ) - 32;
	blob->y = ( gameLocal.random.RandomInt() & 63 ) - 32;
	blob->driftAmount = 0.5f + gameLocal.random.CRandomFloat() * 0.5;
	float scale = ( 256 + ( ( gameLocal.random.RandomInt()&63 ) - 32 ) ) / 256.0f;
	blob->w = 600 * g_blobSize.GetFloat() * scale;
	blob->h = 480 * g_blobSize.GetFloat() * scale;
	float s1 = 0.0f;
	float t1 = 0.0f;
	float s2 = 1.0f;
	float t2 = 1.0f;
	if ( blob->driftAmount < 0.6 ) {
		s1 = 1.0f;
		s2 = 0.0f;
	} else if ( blob->driftAmount < 0.75 ) {
		t1 = 1.0f;
		t2 = 0.0f;
	} else if ( blob->driftAmount < 0.85 ) {
		s1 = 1.0f;
		s2 = 0.0f;
		t1 = 1.0f;
		t2 = 0.0f;
	}
	blob->s1 = s1;
	blob->t1 = t1;
	blob->s2 = s2;
	blob->t2 = t2;
}

/*
==================
idPlayerView::WeaponFireFeedback

Called when a weapon fires, generates head twitches, etc
==================
*/
void idPlayerView::WeaponFireFeedback( const idDict *weaponDef ) {
	int		recoilTime;

	recoilTime = weaponDef->GetInt( "recoilTime" );
	// don't shorten a damage kick in progress
	if ( recoilTime && recoilTime > (kickFinishTime - gameLocal.time) ) {
		idAngles angles;
		weaponDef->GetAngles( "recoilAngles", "5 0 0", angles );
		kickAngles = angles;
		int	finish = gameLocal.time + g_kickTime.GetFloat() * recoilTime;
		kickFinishTime = finish;
	}	

}

/*
===================
idPlayerView::CalculateShake
===================
*/
// RAVEN BEGIN
// jnewquist: Controller rumble
float idPlayerView::CalculateShake( idAngles &shakeAngleOffset ) const {
	idVec3	origin, matrix;

	float shakeVolume = soundSystem->CurrentShakeAmplitudeForPosition( SOUNDWORLD_GAME, gameLocal.time, player->firstPersonViewOrigin );
	//
	// shakeVolume should somehow be molded into an angle here
	// it should be thought of as being in the range 0.0 -> 1.0, although
	// since CurrentShakeAmplitudeForPosition() returns all the shake sounds
	// the player can hear, it can go over 1.0 too.
	//
	shakeAngleOffset[0] = gameLocal.random.CRandomFloat() * shakeVolume;
	shakeAngleOffset[1] = gameLocal.random.CRandomFloat() * shakeVolume;
	shakeAngleOffset[2] = gameLocal.random.CRandomFloat() * shakeVolume;

	return shakeVolume;
}
// RAVEN END

/*
===================
idPlayerView::AngleOffset

  kickVector, a world space direction that the attack should 
===================
*/
idAngles idPlayerView::AngleOffset() const {
	idAngles	ang;

	ang.Zero();

	if ( gameLocal.time < kickFinishTime ) {
		float offset = kickFinishTime - gameLocal.time;

		ang = kickAngles * offset * offset * g_kickAmplitude.GetFloat();

		for ( int i = 0 ; i < 3 ; i++ ) {
			if ( ang[i] > 70.0f ) {
				ang[i] = 70.0f;
			} else if ( ang[i] < -70.0f ) {
				ang[i] = -70.0f;
			}
		}
	}
	return ang;
}

/*
===================
idPlayerView::ShakeOffsets
===================
*/
// RAVEN BEGIN
// jnewquist: Controller rumble
void idPlayerView::ShakeOffsets( idVec3 &shakeOffset, idAngles &shakeAngleOffset, const idBounds bounds ) const {
	float shakeVolume = 0.0f;
	shakeOffset.Zero();
	shakeAngleOffset.Zero();

	if( gameLocal.isMultiplayer ) {
		return;
	}

	shakeVolume = CalculateShake( shakeAngleOffset );

	if( gameLocal.time < shakeFinishTime ) {
		float offset = ( shakeFinishTime - gameLocal.time ) * shakeScale * 0.001f;

		shakeOffset[0] = idMath::ClampFloat( bounds[0][0] - 1.0f, bounds[1][0] + 1.0f, rvRandom::flrand( -offset, offset ) );
		shakeOffset[1] = idMath::ClampFloat( bounds[0][1] - 1.0f, bounds[1][1] + 1.0f, rvRandom::flrand( -offset, offset ) );
		shakeOffset[2] = idMath::ClampFloat( bounds[0][2] - 1.0f, bounds[1][2] + 1.0f, rvRandom::flrand( -offset, offset ) );

		shakeAngleOffset[0] = idMath::ClampFloat( -70.0f, 70.0f, rvRandom::flrand( -offset, offset ) );
		shakeAngleOffset[1] = idMath::ClampFloat( -70.0f, 70.0f, rvRandom::flrand( -offset, offset ) );
		shakeAngleOffset[2] = idMath::ClampFloat( -70.0f, 70.0f, rvRandom::flrand( -offset, offset ) );
	}
}
// RAVEN END

/*
==================
idPlayerView::SingleView
==================
*/
void idPlayerView::SingleView( idUserInterface *hud, const renderView_t *view, int renderFlags ) {
	// normal rendering
	if ( !view ) {
		return;
	}

	if ( !( RF_GUI_ONLY & renderFlags ) ) {
		idPresentationRendererScope rendererScope;
		// jscott: portal sky rendering with KRABS
		idCamera *portalSky = gameLocal.GetPortalSky();
		if ( portalSky ) {
			renderView_t portalSkyView = *view;
			portalSky->GetViewParms( &portalSkyView );
			gameRenderWorld->RenderScene( &portalSkyView, ( renderFlags & ( ~RF_PRIMARY_VIEW ) ) | RF_DEFER_COMMAND_SUBMIT | RF_PORTAL_SKY );
		}
		gameRenderWorld->RenderScene( view, renderFlags | RF_PENUMBRA_MAP );
	}

	if ( RF_NO_GUI & renderFlags ) {
		return;
	}

	// draw screen blobs
	if ( !pm_thirdPerson.GetBool() && !g_skipViewEffects.GetBool() ) {
		// Soften the scene before damage indicators, HUD and wheel artwork.
		DrawRetainedViewEffects( true );
		for ( int i = 0 ; i < MAX_SCREEN_BLOBS ; i++ ) {
			screenBlob_t	*blob = &screenBlobs[i];
			if ( blob->finishTime <= gameLocal.time ) {
				continue;
			}
			
			blob->y += blob->driftAmount;

			float	fade = (float)( blob->finishTime - gameLocal.time ) / ( blob->finishTime - blob->startFadeTime );
			if ( fade > 1.0f ) {
				fade = 1.0f;
			}
			if ( fade ) {
				renderSystem->SetColor4( 1,1,1,fade );
				renderSystem->DrawStretchPic( blob->x, blob->y, blob->w, blob->h,blob->s1, blob->t1, blob->s2, blob->t2, blob->material );
			}
		}

		// Render tunnel vision
		if ( gameLocal.time < tvFinishTime ) {
			renderSystem->SetColor4( 1.0f, 1.0f, 1.0f, tvScale * ((float)(tvFinishTime - gameLocal.time) / (float)(tvFinishTime - tvStartTime)) );
			DrawAspectCorrectFullscreenMaterial( tunnelMaterial );
		}

		// openQ4: damage numbers project against the view that was just
		// submitted, so they have to be drawn after the scene and before the
		// HUD proper takes the foreground
		if ( gameLocal.isMultiplayer ) {
			gameLocal.mpGame.damageNumbers.Draw( view );
		}

		player->DrawHUD( hud );


/*
		// tunnel vision
		float	health = 0.0f;
		if ( g_testHealthVision.GetFloat() != 0.0f ) {
			health = g_testHealthVision.GetFloat();
		} else {
			health = player->health;
		}
		float alpha = health / 100.0f;
		if ( alpha < 0.0f ) {
			alpha = 0.0f;
		}
		if ( alpha > 1.0f ) {
			alpha = 1.0f;
		}

		if ( alpha < 1.0f  ) {
			renderSystem->SetColor4( ( player->health <= 0.0f ) ? MS2SEC( gameLocal.time ) : lastDamageTime, 1.0f, 1.0f, ( player->health <= 0.0f ) ? 0.0f : alpha );
			renderSystem->DrawStretchPic( 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 0.0f, 1.0f, 1.0f, tunnelMaterial );
		}
*/		

		

		// Render the object system
		// RAVEN BEGIN
		// twhitaker: always draw objective system
		if ( player->objectiveSystem ) {
			player->objectiveSystem->Redraw( gameLocal.time );
		}		
		// RAVEN END
	}

	// Dialogue subtitles sit above the HUD and below debug post-process tests.
	DrawSubtitles();

	// test a single material drawn over everything
	if ( g_testPostProcess.GetString()[0] ) {
		const idMaterial *mtr = declManager->FindMaterial( g_testPostProcess.GetString(), false );
		if ( !mtr ) {
			common->Printf( "Material not found.\n" );
			g_testPostProcess.SetString( "" );
		} else {
			renderSystem->SetColor4( 1.0f, 1.0f, 1.0f, 1.0f );
			DrawAspectCorrectFullscreenMaterial( mtr );
		}
	}
}

/*
===================
idPlayerView::DoubleVision
===================
*/
void idPlayerView::DoubleVision( idUserInterface *hud, const renderView_t *view, int offset ) {

	if ( !g_doubleVision.GetBool() ) {
		SingleView( hud, view, RF_NO_GUI );
		return;
	}

	float	scale = offset * g_dvAmplitude.GetFloat() * dvScale;
	if( scale < 0.0f ) {
		return;
	}

	if ( scale > 0.5f ) {
		scale = 0.5f;
	}
	float shift = scale * idMath::Sin( idMath::Sqrt ( offset ) * g_dvFrequency.GetFloat() );
	shift = fabs( shift );

	// Capture from the active framebuffer extents.
	// A forced 640x480 crop only updates a sub-rect in the RT postprocess path.
	SingleView( hud, view, RF_NO_GUI );
	renderSystem->CaptureRenderToImage( "_scratch" );

	// carry red tint if in berserk mode
	idVec4 color(1, 1, 1, 1);

	renderSystem->SetColor4( color.x, color.y, color.z, 1.0f );
// RAVEN BEGIN
// jnewquist: Call DrawStretchCopy, which will flip the texcoords for D3D
// jmarshall - DrawStretchCopy was removed from the engine interface; DrawStretchPic matches the GL path.
	renderSystem->DrawStretchPic( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, shift, 1, 1, 0, dvMaterial );
	renderSystem->DrawStretchPic( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 1, 1-shift, 0, dvMaterialBlend );
// jmarshall end
// RAVEN END
}

/*
=================
idPlayerView::Flash

flashes the player view with the given color
=================
*/
void idPlayerView::Flash(idVec4 color, int time ) {
	Fade(idVec4(0, 0, 0, 0), time);
	fadeFromColor = colorWhite;
// openQ4 BEGIN
	// Reduced flashing (ui_reducedFlashing) starts the white-out at 0.40.
	if ( cvarSystem->GetCVarBool( "ui_reducedFlashing" ) ) {
		fadeFromColor[ 3 ] = 0.4f;
	}
// openQ4 END
}

/*
=================
idPlayerView::Fade

used for level transition fades
assumes: color.w is 0 or 1
=================
*/
void idPlayerView::Fade( idVec4 color, int time ) {
	RetainedRecoveryFade( color, time );

	if ( !fadeTime ) {
		fadeFromColor.Set( 0.0f, 0.0f, 0.0f, 1.0f - color[ 3 ] );
	} else {
		fadeFromColor = fadeColor;
	}
	fadeToColor = color;

	if ( time <= 0 ) {
		fadeRate = 0;
		time = 0;
		fadeColor = fadeToColor;
	} else {
		fadeRate = 1.0f / ( float )time;
	}

	if ( gameLocal.realClientTime == 0 && time == 0 ) {
		fadeTime = 1;
	} else {
		fadeTime = gameLocal.realClientTime + time;
	}
}

/*
=================
idPlayerView::ScreenFade
=================
*/
// openQ4 BEGIN
// openQ4 BEGIN
/*
===================
OpenQ4_SoundPointInLiquid

Handed to the sound world so it can tell which side of a liquid surface an emitter is on. Kept as a
plain function rather than a member because the sound system holds it as a function pointer, and it
is deliberately a bare contents query: it runs once per audible emitter per frame.
===================
*/
static bool OpenQ4_SoundPointInLiquid( const idVec3 &point ) {
	return gameLocal.LiquidContentsAtPoint( point, NULL ) != 0;
}
// openQ4 END

/*
===================
idPlayerView::LiquidAtEye

Which liquid the camera itself is inside, and the point where the underwater audio muffle is
decided. Deliberately not idPhysics_Player::GetWaterLevel(): WATERLEVEL_HEAD needs the whole
bounding box submerged, and the eye is 8 units below the top of that box standing and 16 crouched,
so keying off it would leave the screen clear and the audio dry while the view was already under.
===================
*/
int idPlayerView::LiquidAtEye( const renderView_t *view ) const {
	int liquidContents = 0;

	if ( view ) {
		liquidContents = gameLocal.LiquidContentsAtPoint( view->vieworg, player );
	}

	idSoundWorld *gameSoundWorld = soundSystem->GetSoundWorldFromId( SOUNDWORLD_GAME );
	if ( gameSoundWorld ) {
		gameSoundWorld->SetUnderwater( liquidContents != 0 );
		// Lets the sound world work out which emitters have to shout through the surface to be
		// heard. Liquid lives in the collision world, which only the game can reach.
		gameSoundWorld->SetLiquidTest( OpenQ4_SoundPointInLiquid );
	}

	return liquidContents;
}

/*
===================
idPlayerView::LiquidOverlay

A full screen tint in the colour of whatever the player is swimming in, the way the older Quake
games shade the view underwater. Drawn over the world but under the HUD.
===================
*/
void idPlayerView::LiquidOverlay( int liquidContents ) {
	// Ease in and out rather than snapping. Breaking the surface is fast in life but a hard cut
	// between two very different images reads as a glitch, so give it about a sixth of a second.
	const float target = liquidContents ? 1.0f : 0.0f;
	const float step = MS2SEC( gameLocal.msec ) * 6.0f;
	if ( liquidViewAmount < target ) {
		liquidViewAmount = Min( target, liquidViewAmount + step );
	} else if ( liquidViewAmount > target ) {
		liquidViewAmount = Max( target, liquidViewAmount - step );
	}

	// which liquid to colour it as: while fading out, keep the one we were in
	if ( liquidContents ) {
		liquidViewContents = liquidContents;
	} else if ( liquidViewAmount <= 0.0f ) {
		liquidViewContents = 0;
	}

	const float scale = idMath::ClampFloat( 0.0f, 1.0f, g_liquidScreenTint.GetFloat() );
	const float amount = liquidViewAmount * scale;

	// The post-process filter multiplies the scene, so these are absorption colours - what the
	// liquid lets through - not a wash laid over the top. Water takes red out first, which is most
	// of why underwater looks the way it does.
	// The tint is what the liquid still lets through after fogDistance units of it, so the two
	// numbers together describe the water rather than just colouring it: clear water carries a
	// long way and loses red, lava carries almost nothing.
	idVec3 filterTint( 0.72f, 0.90f, 1.0f );
	float fogDistance = 2200.0f;
	if ( liquidViewContents & CONTENTS_LAVA ) {
		filterTint.Set( 1.0f, 0.42f, 0.18f );
		fogDistance = 110.0f;
	} else if ( liquidViewContents & CONTENTS_SLIME ) {
		filterTint.Set( 0.42f, 0.76f, 0.32f );
		fogDistance = 420.0f;
	}

	// Hand the state to the renderer. It answers false when it cannot draw the effect - the Vulkan
	// module has no arbitrary GLSL, and the pass can be switched off with r_underwater - in which
	// case fall back to a flat wash so the player still knows they are under something.
	if ( renderSystem->SetUnderwaterView( amount, filterTint, fogDistance ) ) {
		return;
	}

	if ( amount <= 0.0f ) {
		return;
	}

	idVec4 wash;
	if ( liquidViewContents & CONTENTS_LAVA ) {
		wash.Set( 0.65f, 0.14f, 0.02f, 0.65f );
	} else if ( liquidViewContents & CONTENTS_SLIME ) {
		wash.Set( 0.16f, 0.36f, 0.06f, 0.55f );
	} else {
		wash.Set( 0.08f, 0.34f, 0.52f, 0.22f );
	}
	wash[3] *= amount;

	const bool previousUIViewportMode = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D( false );
	renderSystem->SetColor4( wash[0], wash[1], wash[2], wash[3] );
	renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 0, 1, 1, declManager->FindMaterial( "_white" ) );
	renderSystem->SetUseUIViewportFor2D( previousUIViewportMode );
}
// openQ4 END

void idPlayerView::ScreenFade() {
	int		msec;
	float	t;

	if ( !fadeTime ) {
		return;
	}

	msec = fadeTime - gameLocal.realClientTime;

	if ( msec <= 0 ) {
		fadeColor = fadeToColor;
		if ( fadeColor[ 3 ] == 0.0f ) {
			fadeTime = 0;
		}
	} else {
		t = ( float )msec * fadeRate;
		fadeColor = fadeFromColor * t + fadeToColor * ( 1.0f - t );
	}

	if ( fadeColor[ 3 ] != 0.0f ) {
		renderSystem->SetColor4( fadeColor[ 0 ], fadeColor[ 1 ], fadeColor[ 2 ], fadeColor[ 3 ] );
		DrawAspectCorrectFullscreenMaterial( declManager->FindMaterial( "_white" ) );
	}
}

/*
===================
idPlayerView::InfluenceVision
===================
*/
void idPlayerView::InfluenceVision( idUserInterface *hud, const renderView_t *view ) {

	float distance = 0.0f;
	float pct = 1.0f;
	if ( player->GetInfluenceEntity() ) {
		distance = ( player->GetInfluenceEntity()->GetPhysics()->GetOrigin() - player->GetPhysics()->GetOrigin() ).Length();
		if ( player->GetInfluenceRadius() != 0.0f && distance < player->GetInfluenceRadius() ) {
			pct = distance / player->GetInfluenceRadius();
			pct = 1.0f - idMath::ClampFloat( 0.0f, 1.0f, pct );
		}
	}
	if ( player->GetInfluenceMaterial() ) {
		SingleView( hud, view );
		renderSystem->SetColor4( 1.0f, 1.0f, 1.0f, pct );
		DrawAspectCorrectFullscreenMaterial( player->GetInfluenceMaterial() );
	} else if ( player->GetInfluenceEntity() == NULL ) {
		SingleView( hud, view, RF_NO_GUI );
		return;
	} else {
		int offset =  25 + idMath::Sin ( gameLocal.time );
		DoubleVision( hud, view, pct * offset );
	}
}

/*
===================
idPlayerView::VRView

The world once per headset eye, each from its tracked pose with its own
off-axis frustum; fades go into the eyes, while the HUD, screen blobs and
damage numbers go to the virtual screen, which the headset composites as a
layer in front of them. Double vision and influence effects stand down.
===================
*/
void idPlayerView::VRView( idUserInterface *hud, const renderView_t *view, const vrFrameState_t &vrFrame, float trackingYaw ) {
	// multiplayer has no vehicle in stereo, whose sights would hang the HUD on
	// the line of sight; a single player seat must not leave it there
	cvarSystem->SetCVarBool( "vr_hudOnSight", false );

	idVec3 eyeOrigin;
	idMat3 eyeAxis;
	player->GetPresentationViewPos( eyeOrigin, eyeAxis );
	eyeOrigin.z += player->VRCrouchLift( vrFrame );

	// the listener follows the tracked head
	if ( vrFrame.head.valid ) {
		idVec3 headOrigin;
		idMat3 headAxis;
		vrPose_t head = vrFrame.head;
		head.origin += VR_HeadOffsetCorrection( vrFrame );
		VR_PoseToWorld( head, eyeOrigin, trackingYaw, headOrigin, headAxis );
		soundSystem->PlaceListener( headOrigin, headAxis, player->entityNumber + 1, gameLocal.time, "Undefined" );
	}

	// where the shot lands, marked in each eye over its 3D pass
	vrAimMarker_t aimMarker;
	const bool drawAimMarker = vrFrame.aimLaser != VR_AIM_LASER_OFF && player->GetVRAimMarker( aimMarker );
	const idMaterial *aimMaterial = drawAimMarker ? declManager->FindMaterial( "_white" ) : NULL;
	// a weapon's zoom magnifies each eye by its own ratio, eased like the flat
	// screen's field of view; the headset's own field of view cannot narrow
	const float zoomScale = player->VRZoomScale();
	// zoomed, the dot alone marks the shot: a beam from the hidden gun would
	// cut across the magnified view
	const int aimLaser = ( zoomScale < 1.0f && vrFrame.aimLaser == VR_AIM_LASER_BEAM ) ? VR_AIM_LASER_DOT : vrFrame.aimLaser;
	// the edges of each eye darken while the stick moves or turns the player
	const float vignette = VRComfortVignette( vrFrame );
	const idMaterial *vignetteMaterial = vignette > 0.01f ? declManager->FindMaterial( "_white" ) : NULL;

	for ( int eye = 0; eye < VR_NUM_EYES; eye++ ) {
		if ( !renderSystem->SetVRRenderTarget( eye ) ) {
			// Tiled screenshots deliberately render to the desktop. The XR
			// pose can still be valid, but no eye target belongs to this draw.
			if ( eye == 0 ) {
				SingleView( hud, view, RF_NO_GUI | RF_PRIMARY_VIEW );
			}
			break;
		}
		renderView_t eyeView = *view;
		VR_BuildEyeView( vrFrame, eye, eyeOrigin, trackingYaw, eyeView );
		if ( zoomScale < 1.0f ) {
			eyeView.fovTanLeft *= zoomScale;
			eyeView.fovTanRight *= zoomScale;
			eyeView.fovTanUp *= zoomScale;
			eyeView.fovTanDown *= zoomScale;
			eyeView.fov_x = RAD2DEG( 2.0f * idMath::ATan( idMath::Tan( DEG2RAD( eyeView.fov_x * 0.5f ) ) * zoomScale ) );
			eyeView.fov_y = RAD2DEG( 2.0f * idMath::ATan( idMath::Tan( DEG2RAD( eyeView.fov_y * 0.5f ) ) * zoomScale ) );
		}
		SingleView( hud, &eyeView, RF_NO_GUI | RF_PRIMARY_VIEW );
		VR_DrawComfortVignette( eyeView, vignette, vignetteMaterial );
		if ( drawAimMarker ) {
			VR_DrawAimMarker( eyeView, aimMarker, aimLaser, aimMaterial );
		}
		ScreenFade();
	}
	renderSystem->SetVRRenderTarget( -1 );

	SingleView( hud, view, RF_GUI_ONLY );
}

/*
===================
idPlayerView::VRComfortVignette

openQ4 VR: how strongly to vignette this frame (vr_comfortVignette). It
follows artificial motion only: the stick's walking and running (the
player's velocity, which a room-scale step leaves alone) and smooth turning
(the rate of the tracking yaw). A change of more than 15 degrees in one frame
is a snap turn or a recentre, a cut rather than motion, and is ignored.
===================
*/
float idPlayerView::VRComfortVignette( const vrFrameState_t &vrFrame ) {
	const float setting = idMath::ClampFloat( 0.0f, 1.0f, cvarSystem->GetCVarFloat( "vr_comfortVignette" ) );
	const int now = Sys_Milliseconds();
	const float seconds = vrComfortTime > 0 ? idMath::ClampFloat( 0.0f, 0.25f, ( now - vrComfortTime ) * 0.001f ) : 0.0f;
	float turnRate = 0.0f;
	if ( seconds > 0.0f ) {
		const float delta = idMath::AngleNormalize180( vrFrame.bodyYaw - vrComfortYaw );
		if ( idMath::Fabs( delta ) < 15.0f ) {
			turnRate = delta / seconds;
		}
	}
	vrComfortYaw = vrFrame.bodyYaw;
	vrComfortTime = now;
	const float target = setting > 0.0f ? VR_ComfortMotion( player->GetPhysics()->GetLinearVelocity().Length(), turnRate ) : 0.0f;
	vrComfort = VR_ComfortEase( vrComfort, target, seconds );
	return vrComfort * setting;
}

/*
===================
idPlayerView::RenderPlayerView
===================
*/
void idPlayerView::RenderPlayerView( idUserInterface *hud ) {
	if ( !player ) {
		return;
	}

	const renderView_t *view = player->GetRenderView();
	if ( !view ) {
		return;
	}
	
	bool guiRendered = false;

	// place the sound origin for the player
	soundSystem->PlaceListener( view->vieworg, view->viewaxis, player->entityNumber + 1, gameLocal.time, "Undefined" );

// openQ4 BEGIN
	const int eyeLiquidContents = LiquidAtEye( view );

	// a headset presents first-person gameplay in stereo
	vrFrameState_t vrFrame;
	float vrTrackingYaw = 0.0f;
	if ( player->GetVRView( vrFrame, vrTrackingYaw ) && vrFrame.stereo ) {
		LiquidOverlay( eyeLiquidContents );
		VRView( hud, view, vrFrame, vrTrackingYaw );
		if ( net_clientLagOMeter.GetBool() && gameLocal.isClient && !( gameLocal.GetDemoState() == DEMO_PLAYING && ( gameLocal.IsServerDemoPlaying() || gameLocal.IsTimeDemo() ) ) ) {
			gameLocal.lagometer.Draw();
		}
		return;
	}
// openQ4 END

	if ( g_skipViewEffects.GetBool() ) {
		SingleView( hud, view );
	} else {
		if ( player->GetInfluenceMaterial() || player->GetInfluenceEntity() ) {
			InfluenceVision( hud, view );
			guiRendered = true;
		} else if ( g_doubleVision.GetBool() && gameLocal.time < dvFinishTime ) {
			DoubleVision( hud, view, dvFinishTime - gameLocal.time );
			guiRendered = false;
		} else {
			SingleView( hud, view, RF_NO_GUI | RF_PRIMARY_VIEW );
		}

// openQ4 BEGIN
		LiquidOverlay( eyeLiquidContents );
// openQ4 END

		DrawRetainedViewEffects();

		// Now draw GUI's.
		if ( !guiRendered ) {
			SingleView( hud, view, RF_GUI_ONLY );
		}

		ScreenFade();
	}

	if ( net_clientLagOMeter.GetBool() && gameLocal.isClient && !( gameLocal.GetDemoState() == DEMO_PLAYING && ( gameLocal.IsServerDemoPlaying() || gameLocal.IsTimeDemo() ) ) ) {
		gameLocal.lagometer.Draw();
	}
}

// RAVEN END
