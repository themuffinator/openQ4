// RAVEN BEGIN
// bdube: note that this file is no longer merged with Doom3 updates
//
// MERGE_DATE 07/07/2004

#ifndef __GAME_PLAYERVIEW_H__
#define __GAME_PLAYERVIEW_H__
#include "../game/gamesys/SubtitlePaging.h"

/*
===============================================================================

  Player view.

===============================================================================
*/

// screenBlob_t are for the on-screen damage claw marks, etc
typedef struct {
	const idMaterial *	material;
	float				x, y, w, h;
	float				s1, t1, s2, t2;
	int					finishTime;
	int					startFadeTime;
	float				driftAmount;
} screenBlob_t;

#define	MAX_SCREEN_BLOBS	8

class idPlayerView {
public:
						idPlayerView();
						~idPlayerView();

	void				Save( idSaveGame *savefile ) const;
	void				Restore( idRestoreGame *savefile );

	void				SetPlayerEntity( class idPlayer *playerEnt );

	void				ClearEffects( void );

// RAVEN BEGIN
// jnewquist: Controller rumble
	void				DamageImpulse( idVec3 localKickDir, const idDict *damageDef, int damage, const char *damageDefName = NULL );
// RAVEN END

	void				WeaponFireFeedback( const idDict *weaponDef );

	idAngles			AngleOffset( void ) const;			// returns the current kick angle

// RAVEN BEGIN
// jnewquist: Controller rumble
	float				CalculateShake( idAngles &shakeAngleOffset ) const;
// RAVEN END

// RAVEN BEGIN
// jscott: for screen shake
	void				ShakeOffsets( idVec3 &shakeOffset, idAngles &shakeAngleOffset, const idBounds bounds ) const;
// RAVEN END

	// adds little entities to the renderer for local blood blobs, etc

	// this may involve rendering to a texture and displaying
	// that with a warp model or in double vision mode
	void				RenderPlayerView( idUserInterface *hud );

	void				Fade( idVec4 color, int time );

	void				Flash( idVec4 color, int time );

	void				AddBloodSpray( float duration );

	// temp for view testing
	void				EnableBFGVision( bool b ) { bfgVision = b; };

// RAVEN BEGIN
// jscott: accessors required for the fx system
	void				SetDoubleVisionParms( float time, float scale ) { dvFinishTime = SEC2MS( time ); dvScale = scale; }
	// openQ4 (#234): world shakes follow g_screenShake; firing shakes pass
	// fromFiring and follow g_viewKick instead
	void				SetShakeParms( float time, float scale, bool fromFiring = false );
	void				SetTunnelParms( float time, float scale ) { tvStartTime = gameLocal.time; tvFinishTime = tvStartTime + time; tvScale = 1.0f / scale; }
// RAVEN END

private:
#include "../game/gamesys/SubtitleState.h"
	void ResetRetainedViewEffects();
	void RetainedRecoveryFade( const idVec4 &color, int duration );
	void DrawRetainedViewEffects( bool wheelOnly = false );
	void RequestViewDepthOfField( bool allowed, const renderView_t &view, const renderViewDepthOfField_t &scene );
	// Depth of field on a focused in-world GUI: how far it has faded in (0..1),
	// the eased focus distance and the game time it was last advanced.
	float guiFocusBlend = 0, guiFocusDistance = 0;
	int guiFocusTime = -1;
	idUserInterface *presentationFx = NULL;
	// The renderer draws the wheel's scene focus as depth of field this frame.
	bool wheelSceneFocus = false;
	int deathFxStart = -1, recoveryFxUntil = 0;
	bool recoverySequence = false;
	int crashFxStart = -1, crashFxDuration = 0;
	bool crashFxClosing = false;
// RAVEN BEGIN
// AReis: Modified SingleView() signature to include renderFlags variable.
	void				SingleView( idUserInterface *hud, const renderView_t *view, int renderFlags = RF_NORMAL );
	// openQ4 VR: the 3D pass once per headset eye, the HUD on the virtual screen
	void				VRView( idUserInterface *hud, const renderView_t *view, const vrFrameState_t &vrFrame, float trackingYaw );
	// openQ4: desktop stereoscopic 3D (r_stereo3D); false renders the flat view
	bool				StereoView( idUserInterface *hud, const renderView_t *view, int eyeLiquidContents );
// RAVEN END
	void				DoubleVision( idUserInterface *hud, const renderView_t *view, int offset );
	void				BerserkVision( idUserInterface *hud, const renderView_t *view );
	void				InfluenceVision( idUserInterface *hud, const renderView_t *view );
	void				ScreenFade();

// openQ4 BEGIN
	// Liquid view treatment. Keyed off the eye position rather than the player's water level,
	// because WATERLEVEL_HEAD only trips once the whole bounding box is under - the camera sits
	// eight units lower than that standing, sixteen crouched, so the screen would stay clear
	// while the view was visibly submerged.
	int					LiquidAtEye( const renderView_t *view ) const;
	void				LiquidOverlay( int liquidContents, bool drawWash = true );
	void				LiquidWash( void );
	float				liquidWashAmount;	// the flat wash owed this frame when the renderer has no underwater pass
	float				liquidViewAmount;	// eased 0-1 so breaking the surface does not cut
	int					liquidViewContents;	// remembered while fading back out

	// openQ4 VR: the comfort vignette, eased, and the tracking yaw it last saw
	float				VRComfortVignette( const vrFrameState_t &vrFrame );
	float				vrComfort;
	float				vrComfortYaw;
	int					vrComfortTime;
// openQ4 END

	screenBlob_t *		GetScreenBlob();

	screenBlob_t		screenBlobs[MAX_SCREEN_BLOBS];

	int					dvFinishTime;		// double vision will be stopped at this time
	const idMaterial *	dvMaterial;			// material to take the double vision screen shot
// RAVEN BEGIN
// jscott: to make double vision work with alpha components
	const idMaterial *	dvMaterialBlend;
// jscott: for effects
	float				dvScale;
// RAVEN END

	int					kickFinishTime;		// view kick will be stopped at this time
	idAngles			kickAngles;			

	bool				bfgVision;			// 

	const idMaterial *	tunnelMaterial;		// health tunnel vision
	const idMaterial *	armorMaterial;		// armor damage view effect
// RAVEN BEGIN
// bdube: not using these
//	const idMaterial *	berserkMaterial;	// berserk effect
//	const idMaterial *	irGogglesMaterial;	// ir effect
	const idMaterial *	bloodSprayMaterial; // blood spray
//	const idMaterial *	bfgMaterial;		// when targeted with BFG
// RAVEN END

	float				lastDamageTime;		// accentuate the tunnel effect for a while

// RAVEN BEGIN
// jscott: for effects
	float				shakeFinishTime;
	float				shakeScale;
	float				tvScale;
	int					tvFinishTime;
	int					tvStartTime;
// RAVEN END

	idVec4				fadeColor;			// fade color
	idVec4				fadeToColor;		// color to fade to
	idVec4				fadeFromColor;		// color to fade from
	float				fadeRate;			// fade rate
	int					fadeTime;			// fade time

	idPlayer *			player;
	renderView_t		view;
};

#endif /* !__GAME_PLAYERVIEW_H__ */

// RAVEN END
