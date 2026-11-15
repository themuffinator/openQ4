//----------------------------------------------------------------
// HitSounds.cpp
//
// openQ4 hit sounds by damage (MP-15). The decisions are the engine-free core
// in HitSounds.h; this file holds the one record and plays the sound.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "HitSounds.h"
#include "HitFeedback.h"

static mpHitSoundState_t	hitSoundState = { 0, 0, false };
// the player the pending pulse belongs to: the local one, or the one followed
static idEntityPtr<idPlayer>	hitSoundPlayer;
// what has played since the last Clear (openq4_reportClientOptions)
static int					hitSoundTones = 0;
static int					hitSoundStock = 0;
static float				hitSoundLastPitch = 0.0f;

// A channel of the listener's own emitter that nothing else plays on, so a
// tone never cuts a voice line or a weapon and a new hit replaces the last.
static const s_channelType HITSOUND_CHANNEL = SND_CHANNEL_CUSTOM + 31;

/*
================
HitSoundPlay

Quake 4's cue, from the player definition as the stock game reads it, at the
given pitch. The tone is the listener's: it plays at full volume wherever the
hit landed, on the local player's emitter whether they fired or follow the
player who did.
================
*/
static void HitSoundPlay( idPlayer *player, bool armor, float pitch ) {
	const char *sound = NULL;
	idPlayer *listener = gameLocal.GetLocalPlayer();

	if ( player == NULL ) {
		player = listener;
	}
	if ( player == NULL ) {
		return;
	}
	if ( armor ) {
		player->spawnArgs.GetString( "snd_armorHit", "", &sound );
	}
	if ( sound == NULL || sound[ 0 ] == '\0' ) {
		player->spawnArgs.GetString( "snd_weaponHit", "", &sound );
	}
	if ( sound == NULL || sound[ 0 ] == '\0' ) {
		return;
	}

	if ( pitch > 0.0f && pitch != 1.0f ) {
		hitSoundTones++;
	} else {
		hitSoundStock++;
	}
	hitSoundLastPitch = pitch;

	if ( pitch > 0.0f && pitch != 1.0f && listener != NULL ) {
		const idSoundShader *shader = declManager->FindSound( sound );
		idSoundEmitter *emitter = soundSystem->EmitterForIndex( SOUNDWORLD_GAME, listener->GetSoundEmitter() );
		if ( shader != NULL && emitter != NULL ) {
			soundShaderParms_t parms;
			memset( &parms, 0, sizeof( parms ) );
			parms.frequencyShift = pitch;
			parms.soundShaderFlags = SSF_GLOBAL;
			emitter->StartSound( shader, HITSOUND_CHANNEL, 0.0f, SSF_GLOBAL );
			emitter->ModifySound( HITSOUND_CHANNEL, &parms );
			return;
		}
	}
	// the sound as shipped; also what a listener with no emitter yet hears
	soundSystem->PlayShaderDirectly( SOUNDWORLD_GAME, sound );
}

/*
================
rvHitSounds::Clear
================
*/
void rvHitSounds::Clear( void ) {
	MPHitSoundClear( hitSoundState );
	hitSoundPlayer = NULL;
	hitSoundTones = 0;
	hitSoundStock = 0;
	hitSoundLastPitch = 0.0f;
}

/*
================
rvHitSounds::GetCounts
================
*/
void rvHitSounds::GetCounts( int &tones, int &stock, float &lastPitch ) {
	tones = hitSoundTones;
	stock = hitSoundStock;
	lastPitch = hitSoundLastPitch;
}

/*
================
rvHitSounds::OnHitInfo

The hit-info message reached the attacker, or whoever follows them. Team and
self hits stay silent, as they are in Quake 4.
================
*/
void rvHitSounds::OnHitInfo( idPlayer *player, int damage, int hitFlags ) {
	if ( hitFlags & ( HITFLAG_TEAM | HITFLAG_SELF ) ) {
		return;
	}
	const int set = hud_hitSound.GetInteger();
	if ( MPHitSoundOnAmount( hitSoundState, set, damage, gameLocal.realClientTime ) != MP_HITSOUND_PLAY_TONE ) {
		return;
	}
	HitSoundPlay( player, ( hitFlags & HITFLAG_ARMOR ) != 0, MPHitSoundPitch( set, damage ) );
}

/*
================
rvHitSounds::OnPulse
================
*/
void rvHitSounds::OnPulse( idPlayer *player, bool armor ) {
	const bool amountsExpected = gameLocal.serverInfo.GetInt( "g_hitFeedback", "2" ) >= 2;
	const mpHitSoundPlay_t play = MPHitSoundOnPulse( hitSoundState, hud_hitSound.GetInteger(), amountsExpected,
		armor, gameLocal.realClientTime );
	if ( play == MP_HITSOUND_PLAY_STOCK ) {
		HitSoundPlay( player, armor, 1.0f );
	} else if ( hitSoundState.pulseTime != 0 ) {
		hitSoundPlayer = player;
	}
}

/*
================
rvHitSounds::Frame
================
*/
void rvHitSounds::Frame( void ) {
	bool armor = false;
	if ( MPHitSoundFrame( hitSoundState, gameLocal.realClientTime, armor ) == MP_HITSOUND_PLAY_STOCK ) {
		HitSoundPlay( hitSoundPlayer.GetEntity(), armor, 1.0f );
		hitSoundPlayer = NULL;
	}
}
