#include "../../Game_local.h"
#include "../ScriptEvents.h"

/*
===============================================================================

	riMonsterPainLord

	monster_q4x_pain_lord, the m05_bio boss: a Strogg overseer with two
	half-processed marines (the "dudes", damage zones "dl" and "dr") strapped
	to his back, who works a processing machine between fights. The map
	script drives him through six "...Func" keys.

	Stun: when his health first drops below two thirds, and again below one
	third, he is stunned ("StunnedFunc"): he staggers ("stunned_intro"), then
	loops "stunned_loop" and takes no damage. Shooting the dudes breaks it
	("stunned_end", "UnstunnedFunc").

	The dudes share one pool of "damageRegionHealth", shown on the player's
	HUD as the boss shield bar. When it falls past half, the dude that was hit
	is torn off ("fx_gib"); at nothing the other goes too and he is enraged
	("EnragedFunc"): no more stuns, and he animates 1.75 times as fast.

	Processing: the script's requestProcessing() makes him run
	"MoveToProcessingStationFunc" (which walks him to the machine) at his next
	chance, and its atProcessingStation() puts him to work ("console_idle").
	While he works only the dudes can be hurt, until they are gone; damage
	that takes their pool past three quarters or one quarter pulls him off
	the machine ("ProcessingBrokenFunc"), otherwise he finishes the animation
	("ProcessingDoneFunc").

	Headshots: the def scales the head 2 and the legs 0.75, but lists the
	legs zone ("*root origin", every joint) after the head ("*neck1"), and
	the later zone wins, so in the expansion a headshot counted as a leg hit.
	A zone that a later one swallows whole gets its joints back here.

===============================================================================
*/

class riMonsterPainLord : public idAI {
public:
	CLASS_PROTOTYPE( riMonsterPainLord );

							riMonsterPainLord( void );

	void					Spawn( void );
	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

	virtual void			Think( void );
	virtual void			AdjustHealthByDamage( int damage );
	virtual int				GetDamageForLocation( int damage, int location );

protected:
	virtual bool			CanTurn( void ) const;
	virtual bool			CheckActions( void );
	virtual void			OnTacticalChange( aiTactical_t oldTactical );

private:
	void					Enrage( void );
	void					DamageDude( int damage, const char *side );
	bool					DudeAttached( char side ) const;
	void					DetachDude( char side );
	void					UpdateShieldBar( void );
	void					RestoreShadowedDamageZones( void );

	bool					stunned;
	bool					enraged;
	bool					atProcessingStation;
	bool					processingRequested;
	bool					interrupted;			// the dudes were hurt enough to end a stun or the processing
	bool					shieldBarShown;
	int						maxHealth;
	int						dudeHealth;
	int						dudeMaxHealth;

	rvScriptFuncUtility		moveToProcessingStationFunc;
	rvScriptFuncUtility		stunnedFunc;
	rvScriptFuncUtility		enragedFunc;
	rvScriptFuncUtility		unstunnedFunc;
	rvScriptFuncUtility		processingBrokenFunc;
	rvScriptFuncUtility		processingDoneFunc;

	void					Event_RequestProcessing( void );
	void					Event_AtProcessingStation( void );

	stateResult_t			State_Killed( const stateParms_t &parms );
	stateResult_t			State_Dead( const stateParms_t &parms );
	stateResult_t			State_Torso_Stun( const stateParms_t &parms );
	stateResult_t			State_Torso_ProcessingStation( const stateParms_t &parms );

	CLASS_STATES_PROTOTYPE( riMonsterPainLord );
};

CLASS_DECLARATION( idAI, riMonsterPainLord )
	EVENT( EV_PainLord_RequestProcessing,	riMonsterPainLord::Event_RequestProcessing )
	EVENT( EV_PainLord_AtProcessingStation,	riMonsterPainLord::Event_AtProcessingStation )
END_CLASS

CLASS_STATES_DECLARATION( riMonsterPainLord )
	STATE( "State_Killed",				riMonsterPainLord::State_Killed )
	STATE( "State_Dead",				riMonsterPainLord::State_Dead )
	STATE( "Torso_Stun",				riMonsterPainLord::State_Torso_Stun )
	STATE( "Torso_ProcessingStation",	riMonsterPainLord::State_Torso_ProcessingStation )
END_CLASS_STATES

static const float PAINLORD_ENRAGED_ANIM_RATE = 1.75f;

/*
================
riMonsterPainLord::riMonsterPainLord
================
*/
riMonsterPainLord::riMonsterPainLord( void ) {
	stunned = false;
	enraged = false;
	atProcessingStation = false;
	processingRequested = false;
	interrupted = false;
	shieldBarShown = false;
	maxHealth = 0;
	dudeHealth = 0;
	dudeMaxHealth = 0;
}

/*
================
riMonsterPainLord::Spawn
================
*/
void riMonsterPainLord::Spawn( void ) {
	maxHealth = spawnArgs.GetInt( "health" );
	dudeMaxHealth = spawnArgs.GetInt( "damageRegionHealth" );
	dudeHealth = dudeMaxHealth;
	shieldBarShown = false;

	moveToProcessingStationFunc.Init( spawnArgs.GetString( "MoveToProcessingStationFunc" ) );
	stunnedFunc.Init( spawnArgs.GetString( "StunnedFunc" ) );
	enragedFunc.Init( spawnArgs.GetString( "EnragedFunc" ) );
	unstunnedFunc.Init( spawnArgs.GetString( "UnstunnedFunc" ) );
	processingBrokenFunc.Init( spawnArgs.GetString( "ProcessingBrokenFunc" ) );
	processingDoneFunc.Init( spawnArgs.GetString( "ProcessingDoneFunc" ) );

	RestoreShadowedDamageZones();
}

/*
================
riMonsterPainLord::RestoreShadowedDamageZones

Gives a damage zone that later zones took over completely its joints, and its
"damage_scale", back. The savegame carries the result.
================
*/
void riMonsterPainLord::RestoreShadowedDamageZones( void ) {
	idList<jointHandle_t> jointList;
	for ( const idKeyValue *kv = spawnArgs.MatchPrefix( "damage_zone " ); kv; kv = spawnArgs.MatchPrefix( "damage_zone ", kv ) ) {
		idStr zone = kv->GetKey();
		zone.Strip( "damage_zone " );
		if ( damageGroups.FindIndex( zone ) >= 0 ) {
			continue;
		}
		const float scale = spawnArgs.GetFloat( va( "damage_scale %s", zone.c_str() ), "1" );
		animator.GetJointList( kv->GetValue(), jointList );
		for ( int i = 0; i < jointList.Num(); i++ ) {
			damageGroups[ jointList[ i ] ] = zone;
			damageScale[ jointList[ i ] ] = scale;
		}
		jointList.Clear();
	}
}

/*
================
riMonsterPainLord::Save
================
*/
void riMonsterPainLord::Save( idSaveGame *savefile ) const {
	savefile->WriteBool( stunned );
	savefile->WriteBool( enraged );
	savefile->WriteBool( atProcessingStation );
	savefile->WriteBool( processingRequested );
	savefile->WriteBool( interrupted );
	savefile->WriteBool( shieldBarShown );
	savefile->WriteInt( maxHealth );
	savefile->WriteInt( dudeHealth );
	savefile->WriteInt( dudeMaxHealth );
	moveToProcessingStationFunc.Save( savefile );
	stunnedFunc.Save( savefile );
	enragedFunc.Save( savefile );
	unstunnedFunc.Save( savefile );
	processingBrokenFunc.Save( savefile );
	processingDoneFunc.Save( savefile );
}

/*
================
riMonsterPainLord::Restore
================
*/
void riMonsterPainLord::Restore( idRestoreGame *savefile ) {
	savefile->ReadBool( stunned );
	savefile->ReadBool( enraged );
	savefile->ReadBool( atProcessingStation );
	savefile->ReadBool( processingRequested );
	savefile->ReadBool( interrupted );
	savefile->ReadBool( shieldBarShown );
	savefile->ReadInt( maxHealth );
	savefile->ReadInt( dudeHealth );
	savefile->ReadInt( dudeMaxHealth );
	moveToProcessingStationFunc.Restore( savefile );
	stunnedFunc.Restore( savefile );
	enragedFunc.Restore( savefile );
	unstunnedFunc.Restore( savefile );
	processingBrokenFunc.Restore( savefile );
	processingDoneFunc.Restore( savefile );

	if ( enraged ) {
		animator.SetPlaybackRate( PAINLORD_ENRAGED_ANIM_RATE );
	}
	// the HUD is rebuilt on load
	shieldBarShown = false;
}

/*
================
riMonsterPainLord::UpdateShieldBar
================
*/
void riMonsterPainLord::UpdateShieldBar( void ) {
	if ( fl.hidden || aifl.dead || !fl.takedamage ) {
		return;
	}
	idPlayer *player = gameLocal.GetLocalPlayer();
	idUserInterface *hud = player ? player->GetHud() : NULL;
	if ( hud == NULL ) {
		return;
	}
	if ( !shieldBarShown ) {
		hud->HandleNamedEvent( "showBossShieldBar" );
		shieldBarShown = true;
	}
	hud->SetStateFloat( "boss_shield_percent", dudeMaxHealth > 0 ? (float)dudeHealth / dudeMaxHealth : 0.0f );
	hud->HandleNamedEvent( "updateBossShield" );
}

/*
================
riMonsterPainLord::Think
================
*/
void riMonsterPainLord::Think( void ) {
	UpdateShieldBar();
	idAI::Think();
}

/*
================
riMonsterPainLord::Enrage
================
*/
void riMonsterPainLord::Enrage( void ) {
	animator.SetPlaybackRate( PAINLORD_ENRAGED_ANIM_RATE );
	stunned = false;
	enraged = true;
	enragedFunc.CallFunc( NULL );
}

/*
================
riMonsterPainLord::AdjustHealthByDamage

Stunned, or at work with his dudes still on, he takes no damage.
================
*/
void riMonsterPainLord::AdjustHealthByDamage( int damage ) {
	if ( damage <= 0 || aifl.undying || stunned || ( atProcessingStation && dudeHealth > 0 ) ) {
		return;
	}

	const int before = health;
	bool stun = false;
	if ( !enraged ) {
		const float thresholds[ 2 ] = { maxHealth * 2.0f / 3.0f, maxHealth / 3.0f };
		for ( int i = 0; i < 2; i++ ) {
			if ( health >= thresholds[ i ] && health - damage < thresholds[ i ] ) {
				// A heavy hit must not skip both shield phases or kill him while
				// starting a stun. Leave him just below this threshold, once.
				damage = health - Max( 1, (int)ceil( thresholds[ i ] ) - 1 );
				stun = true;
				break;
			}
		}
	}

	idAI::AdjustHealthByDamage( damage );
	if ( stun && health < before ) {
		interrupted = false;
		stunned = true;
		stunnedFunc.CallFunc( NULL );
	}
}

// The render mask already persists in saves. Use it instead of introducing a
// second, potentially inconsistent saved record of which captive remains.
bool riMonsterPainLord::DudeAttached( char side ) const {
	if ( !renderEntity.hModel ) {
		return true;
	}
	const unsigned int mask = renderEntity.hModel->GetSurfaceMask( va( "models/monsters/pain_lord/pain_lord_dude_%c", side ) );
	return mask == 0 || ( renderEntity.suppressSurfaceMask & mask ) != mask;
}

void riMonsterPainLord::DetachDude( char side ) {
	if ( !DudeAttached( side ) ) {
		return;
	}
	HideSurface( va( "models/monsters/pain_lord/pain_lord_dude_%c", side ) );
	HideSurface( va( "models/monsters/pain_lord/pain_lord_dude_tubes_%c", side ) );
	PlayEffect( "fx_gib", animator.GetJointHandle( va( "d%c_spine", side ) ) );
}

/*
================
riMonsterPainLord::DamageDude
================
*/
void riMonsterPainLord::DamageDude( int damage, const char *side ) {
	if ( damage <= 0 || dudeHealth <= 0 || !DudeAttached( side[0] ) ) {
		return;
	}
	const int before = dudeHealth;
	const float half = dudeMaxHealth * 0.5f;
	const float quarter = dudeMaxHealth * 0.25f;

	dudeHealth -= damage;

	if ( stunned && dudeHealth > 0 && damage > pain.threshold ) {
		PlayAnim( ANIMCHANNEL_TORSO, va( "stunned_pain_%s", side ), 0 );
	}

	// taking the pool past three quarters or one quarter pulls him off the machine
	if ( atProcessingStation ) {
		if ( ( dudeHealth < quarter * 3.0f && before >= quarter * 3.0f ) || ( dudeHealth < quarter && before >= quarter ) ) {
			interrupted = true;
		}
	}

	// past half, and at nothing, a dude is torn off
	if ( dudeHealth <= 0 || ( dudeHealth < half && before >= half ) ) {
		DetachDude( side[0] );
		interrupted = stunned || atProcessingStation;
		if ( dudeHealth <= 0 ) {
			// One explosive hit can exhaust the entire shared pool. Both
			// captives must disappear, even if only one joint was hit.
			DetachDude( side[0] == 'l' ? 'r' : 'l' );
			dudeHealth = 0;
			Enrage();
		}
	}
}

/*
================
riMonsterPainLord::GetDamageForLocation
================
*/
int riMonsterPainLord::GetDamageForLocation( int damage, int location ) {
	const char *group = GetDamageGroup( location );
	if ( !idStr::Icmp( group, "dl" ) ) {
		if ( !DudeAttached( 'l' ) ) {
			return 0;
		}
		if ( dudeHealth > 0 ) {
			DamageDude( damage, "left" );
		}
	} else if ( !idStr::Icmp( group, "dr" ) ) {
		if ( !DudeAttached( 'r' ) ) {
			return 0;
		}
		if ( dudeHealth > 0 ) {
			DamageDude( damage, "right" );
		}
	}
	return idAI::GetDamageForLocation( damage, location );
}

/*
================
riMonsterPainLord::CanTurn
================
*/
bool riMonsterPainLord::CanTurn( void ) const {
	return !atProcessingStation && idAI::CanTurn();
}

/*
================
riMonsterPainLord::OnTacticalChange

He does not announce where he is going.
================
*/
void riMonsterPainLord::OnTacticalChange( aiTactical_t oldTactical ) {
}

/*
================
riMonsterPainLord::CheckActions
================
*/
bool riMonsterPainLord::CheckActions( void ) {
	if ( atProcessingStation ) {
		PerformAction( "Torso_ProcessingStation", 2, true );
		return true;
	}
	if ( stunned ) {
		PerformAction( "Torso_Stun", 2, true );
		return true;
	}
	if ( processingRequested ) {
		processingRequested = false;
		moveToProcessingStationFunc.CallFunc( NULL );
		return true;
	}
	return idAI::CheckActions();
}

/*
================
riMonsterPainLord::Event_RequestProcessing
================
*/
void riMonsterPainLord::Event_RequestProcessing( void ) {
	processingRequested = true;
}

/*
================
riMonsterPainLord::Event_AtProcessingStation
================
*/
void riMonsterPainLord::Event_AtProcessingStation( void ) {
	if ( health <= 0 || aifl.dead ) {
		return;
	}
	interrupted = false;
	atProcessingStation = true;
}

/*
================
riMonsterPainLord::State_Killed
================
*/
stateResult_t riMonsterPainLord::State_Killed( const stateParms_t &parms ) {
	DisableAnimState( ANIMCHANNEL_TORSO );
	DisableAnimState( ANIMCHANNEL_LEGS );
	OverrideAnim( ANIMCHANNEL_LEGS );
	PlayAnim( ANIMCHANNEL_TORSO, "death", parms.blendFrames );
	PostState( "State_Dead" );
	return SRESULT_DONE;
}

/*
================
riMonsterPainLord::State_Dead
================
*/
stateResult_t riMonsterPainLord::State_Dead( const stateParms_t &parms ) {
	return AnimDone( ANIMCHANNEL_TORSO, 0 ) ? SRESULT_DONE : SRESULT_WAIT;
}

/*
================
riMonsterPainLord::State_Torso_Stun
================
*/
stateResult_t riMonsterPainLord::State_Torso_Stun( const stateParms_t &parms ) {
	enum {
		STAGE_INTRO,
		STAGE_INTRO_WAIT,
		STAGE_LOOP,
		STAGE_LOOP_WAIT,
		STAGE_END_WAIT
	};
	switch ( parms.stage ) {
		case STAGE_INTRO:
			OverrideAnim( ANIMCHANNEL_LEGS );
			PlayAnim( ANIMCHANNEL_TORSO, "stunned_intro", parms.blendFrames );
			return SRESULT_STAGE( STAGE_INTRO_WAIT );

		case STAGE_INTRO_WAIT:
			return AnimDone( ANIMCHANNEL_TORSO, 0 ) ? SRESULT_STAGE( STAGE_LOOP ) : SRESULT_WAIT;

		case STAGE_LOOP:
		case STAGE_LOOP_WAIT:
			if ( parms.stage == STAGE_LOOP_WAIT && !AnimDone( ANIMCHANNEL_TORSO, 0 ) ) {
				return SRESULT_WAIT;
			}
			if ( !interrupted ) {
				PlayAnim( ANIMCHANNEL_TORSO, "stunned_loop", 0 );
				return SRESULT_STAGE( STAGE_LOOP_WAIT );
			}
			interrupted = false;
			stunned = false;
			unstunnedFunc.CallFunc( NULL );
			PlayAnim( ANIMCHANNEL_TORSO, enraged ? "stunned_end_rage" : "stunned_end", 0 );
			return SRESULT_STAGE( STAGE_END_WAIT );

		case STAGE_END_WAIT:
			return AnimDone( ANIMCHANNEL_TORSO, 4 ) ? SRESULT_DONE : SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
riMonsterPainLord::State_Torso_ProcessingStation
================
*/
stateResult_t riMonsterPainLord::State_Torso_ProcessingStation( const stateParms_t &parms ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT
	};
	switch ( parms.stage ) {
		case STAGE_INIT:
			OverrideAnim( ANIMCHANNEL_LEGS );
			PlayAnim( ANIMCHANNEL_TORSO, "console_idle", parms.blendFrames );
			return SRESULT_STAGE( STAGE_WAIT );

		case STAGE_WAIT:
			if ( interrupted ) {
				interrupted = false;
				atProcessingStation = false;
				processingBrokenFunc.CallFunc( NULL );
				return SRESULT_DONE;
			}
			if ( AnimDone( ANIMCHANNEL_TORSO, parms.blendFrames ) ) {
				atProcessingStation = false;
				processingDoneFunc.CallFunc( NULL );
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}
