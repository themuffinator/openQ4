//----------------------------------------------------------------
// HitFeedback.cpp
//
// openQ4 attacker side hit feedback: floating damage numbers, ported from
// Quake Live's damage plums.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "HitFeedback.h"
#include "ObjectiveMarkerLayout.h"
#include "../../game/gamesys/HudPrimitives.h"

/*
===============================================================================

	Presentation constants.

	Taken from Quake Live's damage plum producer, which stages a marker with a
	1000ms life, a 600ms fade delay and a screen space arc.  Quake Live's
	virtual screen is 640x480, the same as idTech 4's, so the velocities carry
	over as written rather than needing a scale factor.

===============================================================================
*/
static const int	NUMBER_DURATION			= 1000;		// ms on screen
static const int	NUMBER_FADE_DELAY		= 600;		// ms before it starts fading
static const float	NUMBER_ORIGIN_JITTER	= 10.0f;	// +/- world units, so stacked hits separate
static const float	NUMBER_DRIFT_X			= 50.0f;	// +/- virtual pixels per second
static const float	NUMBER_RISE_Y			= 120.0f;	// virtual pixels per second, upwards
static const float	NUMBER_RISE_VARIANCE	= 20.0f;
static const float	NUMBER_GRAVITY_Y		= 150.0f;	// virtual pixels per second squared

static const float	NUMBER_CHAR_WIDTH		= 10.0f;
static const float	NUMBER_CHAR_HEIGHT		= 14.0f;
static const float	NUMBER_SHADOW_OFFSET	= 1.0f;

// A point behind the viewer projects with a negative w, and
// GlobalToNormalizedDeviceCoordinates divides through without checking the
// sign, so it has to be rejected before it is handed over.
static const float	NUMBER_NEAR_CLIP		= 4.0f;

static const idVec4	colorDamageLow( 0.25f, 0.5f, 1.0f, 1.0f );
static const idVec4	colorDamageMedium( 1.0f, 1.0f, 0.0f, 1.0f );
static const idVec4	colorDamageHigh( 1.0f, 0.5f, 0.0f, 1.0f );
static const idVec4	colorDamageSevere( 1.0f, 0.0f, 0.0f, 1.0f );

/*
================
Weapon palette for DAMAGENUMBERSTYLE_WEAPON.

Quake Live reads these off the weapon descriptor.  Quake 4's weapon defs carry
no such field, so the palette is indexed by weapon slot - the order of
def_weapon0..def_weapon15 on the player def - and a weapon def may override its
entry with a "damageNumberColor" spawn arg.  What matters is that the colours
stay apart from each other, not that they match any particular weapon's skin.
================
*/
static const idVec4 weaponNumberColors[ MAX_WEAPONS ] = {
	idVec4( 0.60f, 0.85f, 1.00f, 1.0f ),	// 0  blaster
	idVec4( 1.00f, 0.85f, 0.40f, 1.0f ),	// 1  machinegun
	idVec4( 1.00f, 0.55f, 0.20f, 1.0f ),	// 2  shotgun
	idVec4( 0.45f, 1.00f, 0.55f, 1.0f ),	// 3  hyperblaster
	idVec4( 0.55f, 0.80f, 0.35f, 1.0f ),	// 4  grenade launcher
	idVec4( 0.90f, 0.90f, 0.90f, 1.0f ),	// 5  nailgun
	idVec4( 1.00f, 0.35f, 0.25f, 1.0f ),	// 6  rocket launcher
	idVec4( 0.35f, 0.75f, 1.00f, 1.0f ),	// 7  railgun
	idVec4( 0.75f, 0.55f, 1.00f, 1.0f ),	// 8  lightning gun
	idVec4( 1.00f, 0.40f, 0.85f, 1.0f ),	// 9  dark matter gun
	idVec4( 0.70f, 1.00f, 0.90f, 1.0f ),	// 10
	idVec4( 1.00f, 0.75f, 0.60f, 1.0f ),	// 11
	idVec4( 0.60f, 0.70f, 1.00f, 1.0f ),	// 12
	idVec4( 0.85f, 1.00f, 0.45f, 1.0f ),	// 13
	idVec4( 1.00f, 0.60f, 0.75f, 1.0f ),	// 14
	idVec4( 0.80f, 0.80f, 0.60f, 1.0f )		// 15
};

/*
================
DrawCenteredString

The game module has no device context, so numbers are drawn straight off the
console's character atlas - a 16x16 grid of glyphs - which lets them take a
float position and an arbitrary size instead of the fixed cells
renderSystem->DrawSmallStringExt is locked to.
================
*/
static void DrawCenteredString( float x, float y, float charWidth, float charHeight,
								const char *text, const idVec4 &color, const idMaterial *material, rvHudPrimitives& presentation ) {
	const int	length = idStr::Length( text );
	float		cursor;
	int			i;

	if ( length <= 0 || material == NULL ) {
		return;
	}

	cursor = x - ( length * charWidth ) * 0.5f;
	y -= charHeight * 0.5f;


	for ( i = 0; i < length; i++ ) {
		const int	ch = text[ i ] & 255;
		const float	frow = ( ch >> 4 ) * 0.0625f;
		const float	fcol = ( ch & 15 ) * 0.0625f;

		presentation.Rect(cursor,y,charWidth,charHeight,color,material,idVec4(fcol,frow,fcol+.0625f,frow+.0625f),true);
		cursor += charWidth;
	}
}

/*
================
HitMarkerFlagsForHit

The wire flags and the marker's flags are deliberately separate types - one is a
network format, the other is a presentation choice - so they are translated
rather than shared.
================
*/
static int HitMarkerFlagsForHit( int hitFlags ) {
	int markerFlags = 0;

	if ( hitFlags & HITFLAG_TEAM ) {
		markerFlags |= HITMARKER_TEAM;
	}
	if ( hitFlags & HITFLAG_SELF ) {
		markerFlags |= HITMARKER_SELF;
	}
	if ( hitFlags & HITFLAG_ARMOR ) {
		markerFlags |= HITMARKER_ARMOR;
	}
	if ( hitFlags & HITFLAG_KILL ) {
		markerFlags |= HITMARKER_KILL;
	}

	return markerFlags;
}

/*
================
rvDamageNumbers::rvDamageNumbers
================
*/
rvDamageNumbers::rvDamageNumbers( void ) {
	charSetMaterial = NULL;
	Clear();
}

/*
================
rvDamageNumbers::Clear
================
*/
void rvDamageNumbers::Clear( void ) {
	memset( numbers, 0, sizeof( numbers ) );
	nextNumber = 0;
}

/*
================
rvDamageNumbers::ColorForHit
================
*/
void rvDamageNumbers::ColorForHit( int damage, int weapon, idVec4 &color ) {
	switch ( hud_damageNumberStyle.GetInteger() ) {
		case DAMAGENUMBERSTYLE_AMOUNT:
			if ( damage > 75 ) {
				color = colorDamageSevere;
			} else if ( damage > 50 ) {
				color = colorDamageHigh;
			} else if ( damage <= 25 ) {
				color = colorDamageLow;
			} else {
				color = colorDamageMedium;
			}
			return;

		case DAMAGENUMBERSTYLE_WEAPON: {
			idPlayer *		player;
			const char *	weaponDefName;

			color = colorWhite;
			if ( weapon < 0 || weapon >= MAX_WEAPONS ) {
				return;
			}
			color = weaponNumberColors[ weapon ];

			// let a weapon def speak for itself when it wants to
			player = gameLocal.GetLocalPlayer();
			if ( player == NULL ) {
				return;
			}
			weaponDefName = player->spawnArgs.GetString( va( "def_weapon%d", weapon ), "" );
			if ( weaponDefName[ 0 ] == '\0' ) {
				return;
			}

			const idDeclEntityDef *weaponDef = gameLocal.FindEntityDef( weaponDefName, false );
			if ( weaponDef != NULL ) {
				idVec4 override;

				if ( weaponDef->dict.GetVec4( "damageNumberColor", "0 0 0 0", override ) ) {
					color = override;
				}
			}
			return;
		}

		default:
			// Quake Live's default lane: white at a scratch, red at 100
			color.Lerp( colorWhite, colorDamageSevere, idMath::ClampFloat( 0.0f, 1.0f, damage / 100.0f ) );
			color.w = 1.0f;
			return;
	}
}

/*
================
rvDamageNumbers::Add

Stages one number.  The caller has already decided the hit is worth showing.
================
*/
void rvDamageNumbers::Add( const idVec3 &origin, int damage, int weapon, int flags ) {
	damageNumber_t *number;

	// Multiplayer only, and not merely by convention: single player runs game-sp,
	// which links none of this and registers no hud_damageNumber* cvar.  The test
	// is here so the same is true of any future caller inside game-mp - the Arena
	// campaign aside, nothing in this module should stage a plum outside a match.
	if ( !gameLocal.isMultiplayer || damage <= 0 ) {
		return;
	}

	switch ( hud_damageNumbers.GetInteger() ) {
		case DAMAGENUMBERS_OPPONENTS:
			if ( flags & ( HITFLAG_TEAM | HITFLAG_SELF ) ) {
				return;
			}
			break;
		case DAMAGENUMBERS_ALL:
			break;
		default:
			return;
	}

	number = &numbers[ nextNumber ];
	nextNumber = ( nextNumber + 1 ) % MAX_NUMBERS;

	// a zero start time is the free marker for a slot, so never store one
	number->startTime = ( gameLocal.time > 0 ) ? gameLocal.time : 1;

	number->origin = origin;
	number->origin.x += random.CRandomFloat() * NUMBER_ORIGIN_JITTER;
	number->origin.y += random.CRandomFloat() * NUMBER_ORIGIN_JITTER;
	number->origin.z += random.CRandomFloat() * NUMBER_ORIGIN_JITTER;

	number->screenVelocity.x = random.CRandomFloat() * NUMBER_DRIFT_X;
	number->screenVelocity.y = -NUMBER_RISE_Y - random.RandomFloat() * NUMBER_RISE_VARIANCE;

	ColorForHit( damage, weapon, number->color );
	idStr::snPrintf( number->text, sizeof( number->text ), "%d", damage );
}

/*
================
rvDamageNumbers::ClientReceive
================
*/
void rvDamageNumbers::ClientReceive( const idBitMsg &msg ) {
	idVec3	origin;
	int		damage;
	int		weapon;
	int		flags;

	origin.x = msg.ReadFloat();
	origin.y = msg.ReadFloat();
	origin.z = msg.ReadFloat();
	damage = msg.ReadShort();
	weapon = msg.ReadByte();
	flags = msg.ReadByte();

	// The marker wants every hit this message reports, amount or no amount: it
	// is the cue that the shot landed, and it is the same message either way.
	rvHitMarker::Trigger( ( damage > 0 ) ? damage : HITMARKER_DAMAGE_UNKNOWN,
						  HitMarkerFlagsForHit( flags ), HITMARKER_PRECISE );

	// g_hitFeedback 1 permits the cue but withholds the amount, and the amount
	// is the whole of this feature
	if ( damage <= 0 ) {
		return;
	}

	Add( origin, damage, ( weapon == 0xff ) ? -1 : weapon, flags );
}

/*
================
DamageNumberHud

openQ4 VR: in the headset the numbers are drawn on the HUD, a layer the
runtime hangs in front of the tracked head, not over either eye's image. They
project through that plane from the head, as the objective markers do; the
renderer's last view, which GlobalToNormalizedDeviceCoordinates reads, is the
right eye, so the numbers sat off their targets by its offset and frustum.
Keep these dimensions aligned with OpenXRSystem::EndFrame's HUD quad.
================
*/
struct damageNumberHud_t {
	idVec3	origin;
	idMat3	axis;
	float	distance;
	float	width;
	float	offset;
	float	canvasWidth;
};

// DAMAGENUMBER_PROJECT_*: what a frame's numbers project through
enum {
	DAMAGENUMBER_PROJECT_NONE = -1,		// stereo without a tracked head: nothing to project from
	DAMAGENUMBER_PROJECT_VIEW = 0,		// the flat view the scene was just rendered from
	DAMAGENUMBER_PROJECT_HUD = 1		// the headset's HUD layer
};

static int DamageNumberHud( damageNumberHud_t &hud ) {
	idPlayer *viewer = gameLocal.GetLocalPlayer();
	vrFrameState_t vrFrame;
	float trackingYaw;
	if ( viewer == NULL || !viewer->GetVRView( vrFrame, trackingYaw ) || !vrFrame.stereo ) {
		return DAMAGENUMBER_PROJECT_VIEW;
	}
	if ( !vrFrame.head.valid ) {
		return DAMAGENUMBER_PROJECT_NONE;
	}
	idVec3 eyeOrigin;
	idMat3 eyeAxis;
	viewer->GetPresentationViewPos( eyeOrigin, eyeAxis );
	eyeOrigin.z += viewer->VRCrouchLift( vrFrame );
	vrPose_t head = vrFrame.head;
	head.origin += VR_HeadOffsetCorrection( vrFrame );
	VR_PoseToWorld( head, eyeOrigin, trackingYaw, hud.origin, hud.axis );
	hud.distance = idMath::ClampFloat( 0.5f, 5.0f, cvarSystem->GetCVarFloat( "vr_hudDistance" ) );
	hud.width = idMath::ClampFloat( 0.3f, 4.0f, cvarSystem->GetCVarFloat( "vr_hudWidth" ) );
	hud.offset = ( vrFrame.aimMode == VR_AIM_HAND && !cvarSystem->GetCVarBool( "vr_hudOnSight" ) )
		? cvarSystem->GetCVarFloat( "vr_hudHeightOffset" ) : 0.0f;
	hud.canvasWidth = SCREEN_HEIGHT * static_cast<float>( renderSystem->GetScreenWidth() ) / static_cast<float>( Max( 1, renderSystem->GetScreenHeight() ) );
	return DAMAGENUMBER_PROJECT_HUD;
}

/*
================
rvDamageNumbers::Draw

A number is projected fresh every frame and then carried along a screen space
arc, so it tracks the point it was staged at while still drifting clear of it.
================
*/
void rvDamageNumbers::Draw( const renderView_t *view ) {
	int		i;
	float	scale;

	if ( !gameLocal.isMultiplayer || view == NULL ||
		 hud_damageNumbers.GetInteger() <= DAMAGENUMBERS_OFF ) {
		return;
	}

	if ( charSetMaterial == NULL ) {
		charSetMaterial = declManager->FindMaterial( "fonts/english/bigchars" );
	}

	damageNumberHud_t hud;
	const int projection = DamageNumberHud( hud );
	if ( projection == DAMAGENUMBER_PROJECT_NONE ) {
		return;
	}

	scale = idMath::ClampFloat( 0.25f, 4.0f, hud_damageNumberScale.GetFloat() );
	rvHudPrimitives presentation;

	for ( i = 0; i < MAX_NUMBERS; i++ ) {
		damageNumber_t *	number = &numbers[ i ];
		idVec3				ndc;
		idVec4				color;
		float				elapsed;
		float				alpha;
		float				x, y;

		if ( number->startTime == 0 ) {
			continue;
		}

		elapsed = gameLocal.time - number->startTime;
		if ( elapsed < 0.0f || elapsed >= NUMBER_DURATION ) {
			number->startTime = 0;
			continue;
		}

		if ( projection == DAMAGENUMBER_PROJECT_HUD ) {
			// where the line from the head to the number meets the HUD layer;
			// a number off the layer's edge has nowhere to be drawn
			const idVec3 delta = number->origin - hud.origin;
			const float forward = delta * hud.axis[ 0 ];
			if ( forward < NUMBER_NEAR_CLIP ) {
				continue;
			}
			objectiveMarker::Placement placement;
			if ( !objectiveMarker::ProjectOnHud( -( delta * hud.axis[ 1 ] ), delta * hud.axis[ 2 ], forward,
					hud.distance, hud.width, hud.offset, hud.canvasWidth, SCREEN_HEIGHT, 0.0f, 0.0f, placement ) || placement.edge ) {
				continue;
			}
			x = placement.x * SCREEN_WIDTH / hud.canvasWidth;
			y = placement.y;
		} else {
			// behind the near plane, so there is nothing meaningful to project
			if ( ( number->origin - view->vieworg ) * view->viewaxis[ 0 ] < NUMBER_NEAR_CLIP ) {
				continue;
			}

			renderSystem->GlobalToNormalizedDeviceCoordinates( number->origin, ndc );
			x = ( ndc.x * 0.5f + 0.5f ) * SCREEN_WIDTH;
			y = ( 0.5f - ndc.y * 0.5f ) * SCREEN_HEIGHT;
		}

		elapsed = MS2SEC( elapsed );
		x += number->screenVelocity.x * elapsed;
		y += number->screenVelocity.y * elapsed + 0.5f * NUMBER_GRAVITY_Y * elapsed * elapsed;

		alpha = 1.0f;
		if ( elapsed > MS2SEC( NUMBER_FADE_DELAY ) ) {
			alpha = 1.0f - ( elapsed - MS2SEC( NUMBER_FADE_DELAY ) ) /
					MS2SEC( NUMBER_DURATION - NUMBER_FADE_DELAY );
			alpha = idMath::ClampFloat( 0.0f, 1.0f, alpha );
		}

		const float charWidth = NUMBER_CHAR_WIDTH * scale;
		const float charHeight = NUMBER_CHAR_HEIGHT * scale;

		// shadow first, so a number stays readable over a bright wall
		color.Set( 0.0f, 0.0f, 0.0f, alpha * 0.5f );
		DrawCenteredString( x + NUMBER_SHADOW_OFFSET * scale, y + NUMBER_SHADOW_OFFSET * scale,
							charWidth, charHeight, number->text, color, charSetMaterial, presentation );

		color = number->color;
		color.w *= alpha;
		DrawCenteredString( x, y, charWidth, charHeight, number->text, color, charSetMaterial, presentation );
	}

	presentation.Draw("native/damage-numbers");
}

/*
================
rvDamageNumbers::ServerSend

Publishes one hit to the attacker.  The message rides the unreliable channel:
it is cosmetic, it is bursty in exactly the way a reliable queue is worst at,
and idGameLocal::SendUnreliableMessage already forwards to whoever is
spectating the attacker, which is what a caster wants.
================
*/
void rvDamageNumbers::ServerSend( idPlayer *victim, idEntity *attacker, int weapon, int damage, int hitFlags ) {
	idBitMsg	msg;
	byte		msgBuf[ MAX_GAME_MESSAGE_SIZE ];
	idPlayer *	attackingPlayer;
	idVec3		origin;
	int			flags;

	if ( gameLocal.isClient || !gameLocal.isMultiplayer || g_hitFeedback.GetInteger() <= 0 ) {
		return;
	}
	if ( victim == NULL || attacker == NULL || damage <= 0 ) {
		return;
	}
	if ( !attacker->IsType( idPlayer::GetClassType() ) ) {
		// world damage, a turret, a trigger: nobody to credit
		return;
	}
	attackingPlayer = static_cast< idPlayer * >( attacker );

	flags = hitFlags & ( HITFLAG_ARMOR | HITFLAG_KILL );
	if ( attackingPlayer == victim ) {
		flags |= HITFLAG_SELF;
	} else if ( gameLocal.IsTeamGame() && attackingPlayer->team == victim->team ) {
		flags |= HITFLAG_TEAM;
	}

	// the victim's centre reads better than their feet, and it is the same
	// fallback Quake Live uses when a hit carries no impact point
	origin = victim->GetPhysics()->GetAbsBounds().GetCenter();

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.BeginWriting();
	msg.WriteByte( GAME_UNRELIABLE_MESSAGE_HITINFO );
	msg.WriteFloat( origin.x );
	msg.WriteFloat( origin.y );
	msg.WriteFloat( origin.z );
	// g_hitFeedback 1 permits the hit to be announced without disclosing how
	// much it was worth
	msg.WriteShort( ( g_hitFeedback.GetInteger() >= 2 ) ? idMath::ClampInt( 0, 32767, damage ) : 0 );
	msg.WriteByte( ( weapon >= 0 && weapon < MAX_WEAPONS ) ? weapon : 0xff );
	msg.WriteByte( flags );

	gameLocal.SendUnreliableMessage( msg, attackingPlayer->entityNumber );

	// SendUnreliableMessage deliberately skips the local client, so a listen
	// server host has to be staged directly.  Remote spectators following the
	// host are still served by the send above.
	if ( attackingPlayer->entityNumber == gameLocal.localClientNum ) {
		const int reported = ( g_hitFeedback.GetInteger() >= 2 ) ? damage : 0;

		rvHitMarker::Trigger( ( reported > 0 ) ? reported : HITMARKER_DAMAGE_UNKNOWN,
							  HitMarkerFlagsForHit( flags ), HITMARKER_PRECISE );
		gameLocal.mpGame.damageNumbers.Add( origin, reported, weapon, flags );
	}
}
