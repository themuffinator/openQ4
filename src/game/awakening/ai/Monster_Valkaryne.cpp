#include "../../Game_local.h"
#include "../ScriptEvents.h"

/*
===============================================================================

	riMonsterValkaryne

	monster_q4x_valkaryne, the final boss (m09_valkaryne). The map script
	moves her between two modes with docked() and undock():

	Docked: plugged into her platform. She idles on "plugin", cannot move and
	takes no damage, and her only attack is the long "plugin_range_attack_big"
	death-ray volley (action_pluginAttackBig).

	Undocked: undock() plays "plugin_to_idle" and hands her her ranged and
	melee attacks back. She now turns only in 90 degree steps
	("turn_right_90" / "turn_left_90"), whenever the way she wants to face is
	more than three quarters of her head's look range away, or her enemy is
	out of sight on that side.

	She spawns with her ranged and melee attacks off, as the expansion's did:
	she fights only after the script has docked and undocked her. Her attacks
	themselves are frame commands in her model def.

	The finale's Valkaryne ("valk" on m09) names the map's docking script in
	"requestDocking" (valkaryne_dock: she vanishes, is docked() and reappears
	on her platform). Nothing in the shipped scripts ever runs it, and the
	expansion's code parsed the key without calling it, so she never docked:
	she walked about harmless and could be killed before the Makron sphere,
	skipping the fight the script builds around the sphere (it undocks her when
	the sphere dies). On original-script saves she asks for it when she first has an enemy,
	which the intro gives her with becomeAggressive() just as it starts the
	sphere's shields. The key names the function without its map_m09
	namespace, so a name that does not resolve is looked up as the one script
	function of that name in any namespace. Whether she asked is kept in her
	spawn args, which the savegame carries. Corrected fresh M09 maps start docking
	during the intro and supply the versioned callback explicitly. Before phase
	two she is immune even during the handoff to that script.

===============================================================================
*/

class riMonsterValkaryne : public idAI {
public:
	CLASS_PROTOTYPE( riMonsterValkaryne );

							riMonsterValkaryne( void );

	void					Spawn( void );
	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

	virtual void			AdjustHealthByDamage( int damage );
	virtual bool			Pain( idEntity *inflictor, idEntity *attacker, int damage, const idVec3 &dir, int location );

protected:
	virtual bool			CanMove( void ) const;
	virtual const char *	GetIdleAnimName( void );
	virtual bool			CheckActions( void );

private:
	void					SetDocked( bool dock );
	bool					RequestDocking( void );
	bool					DamageProtected( void ) const;

	rvAIAction				actionPluginAttackBig;
	bool					docked;
	bool					undockRequested;
	int						nextTurnTime;

	void					Event_Docked( void );
	void					Event_Undock( void );

	stateResult_t			State_Killed( const stateParms_t &parms );
	stateResult_t			State_Undock( const stateParms_t &parms );
	stateResult_t			State_Torso_PluginAttackBig( const stateParms_t &parms );
	stateResult_t			State_Torso_TurnRight90( const stateParms_t &parms );
	stateResult_t			State_Torso_TurnLeft90( const stateParms_t &parms );
	stateResult_t			TurnNinety( const stateParms_t &parms, const char *anim );

	CLASS_STATES_PROTOTYPE( riMonsterValkaryne );
};

CLASS_DECLARATION( idAI, riMonsterValkaryne )
	EVENT( EV_Valkaryne_Docked,	riMonsterValkaryne::Event_Docked )
	EVENT( EV_Valkaryne_Undock,	riMonsterValkaryne::Event_Undock )
END_CLASS

CLASS_STATES_DECLARATION( riMonsterValkaryne )
	STATE( "State_Killed",			riMonsterValkaryne::State_Killed )
	STATE( "Undock",				riMonsterValkaryne::State_Undock )
	STATE( "Torso_PluginAttackBig",	riMonsterValkaryne::State_Torso_PluginAttackBig )
	STATE( "Torso_TurnRight90",		riMonsterValkaryne::State_Torso_TurnRight90 )
	STATE( "Torso_TurnLeft90",		riMonsterValkaryne::State_Torso_TurnLeft90 )
END_CLASS_STATES

static const int	VALKARYNE_TURN_DELAY	= 250;
static const float	VALKARYNE_TURN_FRACTION	= 0.75f;

/*
================
riMonsterValkaryne::riMonsterValkaryne
================
*/
riMonsterValkaryne::riMonsterValkaryne( void ) {
	docked = false;
	undockRequested = false;
	nextTurnTime = 0;
}

/*
================
riMonsterValkaryne::Spawn
================
*/
void riMonsterValkaryne::Spawn( void ) {
	actionPluginAttackBig.Init( spawnArgs, "action_pluginAttackBig", "Torso_PluginAttackBig", AIACTIONF_ATTACK );

	// the plug-in attack is hers until the script undocks her
	actionPluginAttackBig.fl.disabled = false;
	actionRangedAttack.fl.disabled = true;
	actionMeleeAttack.fl.disabled = true;
}

/*
================
riMonsterValkaryne::Save
================
*/
void riMonsterValkaryne::Save( idSaveGame *savefile ) const {
	actionPluginAttackBig.Save( savefile );
	savefile->WriteBool( docked );
	savefile->WriteBool( undockRequested );
	savefile->WriteInt( nextTurnTime );
}

/*
================
riMonsterValkaryne::Restore
================
*/
void riMonsterValkaryne::Restore( idRestoreGame *savefile ) {
	actionPluginAttackBig.Restore( savefile );
	savefile->ReadBool( docked );
	savefile->ReadBool( undockRequested );
	savefile->ReadInt( nextTurnTime );
	if ( docked ) {
		// Older saves may have been made before docking suspended gravity.
		if ( !spawnArgs.FindKey( "openq4_dockedPreviousNoGravity" ) ) {
			spawnArgs.SetBool( "openq4_dockedPreviousNoGravity", move.fl.noGravity );
		}
		move.fl.noGravity = true;
		physicsObj.SetLinearVelocity( vec3_origin );
	}
}

/*
================
riMonsterValkaryne::SetDocked
================
*/
void riMonsterValkaryne::SetDocked( bool dock ) {
	if ( DebugActions() ) {
		gameLocal.Printf( "%d %s: %s\n", gameLocal.time, name.c_str(), dock ? "docked" : "undocked" );
	}
	// CanMove suppresses navigation, but the monster physics still applies
	// gravity. The descent's scripted perches are attachment points, including
	// one above open space: a plugged-in Valkaryne must remain at that point.
	// Keep the previous policy in saved spawn args without changing the save
	// layout, and restore it when the mobile phase starts.
	if ( dock && !docked ) {
		spawnArgs.SetBool( "openq4_dockedPreviousNoGravity", move.fl.noGravity );
		move.fl.noGravity = true;
		physicsObj.SetLinearVelocity( vec3_origin );
	} else if ( !dock && docked ) {
		move.fl.noGravity = spawnArgs.GetBool( "openq4_dockedPreviousNoGravity", "0" );
		spawnArgs.Delete( "openq4_dockedPreviousNoGravity" );
	}
	docked = dock;
	actionPluginAttackBig.fl.disabled = !dock;
	actionRangedAttack.fl.disabled = dock;
	actionMeleeAttack.fl.disabled = dock;
}

/*
================
FindScriptFunctionAnyNamespace

The script function called name, or, when no function of that name is in
the global namespace, the only one in any other namespace.
================
*/
static const function_t *FindScriptFunctionAnyNamespace( const char *name ) {
	const function_t *func = gameLocal.program.FindFunction( name );
	if ( func != NULL || strstr( name, "::" ) != NULL ) {
		return func;
	}
	for ( int i = 0; i < gameLocal.program.NumFunctions(); i++ ) {
		const function_t *candidate = gameLocal.program.GetFunction( i );
		if ( candidate->eventdef != NULL || candidate->def == NULL || candidate->def->scope == NULL ||
			 candidate->def->scope->Type() != ev_namespace ) {
			continue;
		}
		// functions are named by their global name ("map_m09::valkaryne_dock")
		const char *unscoped = candidate->Name();
		for ( const char *sep = strstr( unscoped, "::" ); sep != NULL; sep = strstr( unscoped, "::" ) ) {
			unscoped = sep + 2;
		}
		if ( idStr::Icmp( unscoped, name ) ) {
			continue;
		}
		if ( func != NULL ) {
			// more than one: ambiguous
			return NULL;
		}
		func = candidate;
	}
	return func;
}

/*
================
riMonsterValkaryne::RequestDocking

Starts the "requestDocking" script, once; true when it did.
================
*/
bool riMonsterValkaryne::RequestDocking( void ) {
	const char *funcName = spawnArgs.GetString( "requestDocking" );
	if ( !funcName[0] || spawnArgs.GetBool( "openq4_dockingRequested" ) ||
		spawnArgs.GetBool( "openq4_dockingComplete" ) ) {
		return false;
	}
	spawnArgs.SetBool( "openq4_dockingRequested", true );

	const function_t *func = FindScriptFunctionAnyNamespace( funcName );
	if ( func == NULL || func->eventdef != NULL || func->type->NumParameters() != 0 ) {
		gameLocal.Warning( "%s: no docking script '%s'", name.c_str(), funcName );
		return false;
	}
	idThread *thread = new idThread();
	thread->CallFunction( func, false );
	thread->DelayedStart( 0 );
	return true;
}

/*
================
riMonsterValkaryne::AdjustHealthByDamage
================
*/
bool riMonsterValkaryne::DamageProtected( void ) const {
	// The finale must not be killed during its intro or the frame before its
	// docking thread runs. Cinematic copies without a docking request retain
	// their authored damage rules. Store the phase in already-serialized args.
	const bool awaitingSphere = spawnArgs.GetString( "requestDocking" )[0] &&
		!spawnArgs.GetBool( "openq4_dockingComplete" ) && actionRangedAttack.fl.disabled;
	return docked || undockRequested || awaitingSphere;
}

void riMonsterValkaryne::AdjustHealthByDamage( int damage ) {
	if ( !DamageProtected() ) {
		health -= damage;
	}
}

bool riMonsterValkaryne::Pain( idEntity *inflictor, idEntity *attacker, int damage, const idVec3 &dir, int location ) {
	// Health-only immunity still let shots interrupt every docked volley.
	// The sphere's scripted pain animation remains under the map's control.
	return !DamageProtected() && idAI::Pain( inflictor, attacker, damage, dir, location );
}

/*
================
riMonsterValkaryne::CanMove
================
*/
bool riMonsterValkaryne::CanMove( void ) const {
	return !docked && idAI::CanMove();
}

/*
================
riMonsterValkaryne::GetIdleAnimName
================
*/
const char *riMonsterValkaryne::GetIdleAnimName( void ) {
	return docked ? "plugin" : idAI::GetIdleAnimName();
}

/*
================
riMonsterValkaryne::CheckActions
================
*/
bool riMonsterValkaryne::CheckActions( void ) {
	if ( undockRequested ) {
		PerformAction( "Undock", 4, true );
		return true;
	}

	if ( !docked && enemy.ent && RequestDocking() ) {
		return true;
	}

	if ( docked ) {
		if ( PerformAction( &actionPluginAttackBig, (checkAction_t)&idAI::CheckAction_RangedAttack, &actionTimerSpecialAttack ) ) {
			return true;
		}
	} else if ( !move.fl.moving && gameLocal.time > nextTurnTime ) {
		// she turns in 90 degree steps, never smoothly
		const float delta = idMath::AngleNormalize180( move.ideal_yaw - move.current_yaw );
		const float limit = lookMax.yaw * VALKARYNE_TURN_FRACTION;
		if ( delta > limit || ( delta > 0.0f && !enemy.fl.inFov ) ) {
			PerformAction( "Torso_TurnRight90", 4, true );
			return true;
		}
		if ( delta < -limit || ( delta < 0.0f && !enemy.fl.inFov ) ) {
			PerformAction( "Torso_TurnLeft90", 4, true );
			return true;
		}
	}

	return idAI::CheckActions();
}

/*
================
riMonsterValkaryne::Event_Docked
================
*/
void riMonsterValkaryne::Event_Docked( void ) {
	if ( undockRequested || spawnArgs.GetBool( "openq4_dockingComplete" ) ) {
		return;
	}
	SetDocked( true );
}

/*
================
riMonsterValkaryne::Event_Undock
================
*/
void riMonsterValkaryne::Event_Undock( void ) {
	if ( health <= 0 || spawnArgs.GetBool( "openq4_dockingComplete" ) ) {
		return;
	}
	// An early sphere kill still consumes the one-time docking request.
	spawnArgs.SetBool( "openq4_dockingRequested", true );
	undockRequested = true;
}

/*
================
riMonsterValkaryne::State_Killed
================
*/
stateResult_t riMonsterValkaryne::State_Killed( const stateParms_t &parms ) {
	OverrideAnim( ANIMCHANNEL_LEGS );
	PlayAnim( ANIMCHANNEL_TORSO, "death", 0 );
	PostState( "State_Dead" );
	return SRESULT_DONE;
}

/*
================
riMonsterValkaryne::State_Undock
================
*/
stateResult_t riMonsterValkaryne::State_Undock( const stateParms_t &parms ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT
	};
	switch ( parms.stage ) {
		case STAGE_INIT:
			PlayAnim( ANIMCHANNEL_TORSO, "plugin_to_idle", parms.blendFrames );
			return SRESULT_STAGE( STAGE_WAIT );

		case STAGE_WAIT:
			if ( AnimDone( ANIMCHANNEL_TORSO, parms.blendFrames ) ) {
				SetDocked( false );
				undockRequested = false;
				spawnArgs.SetBool( "openq4_dockingComplete", true );
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
riMonsterValkaryne::State_Torso_PluginAttackBig
================
*/
stateResult_t riMonsterValkaryne::State_Torso_PluginAttackBig( const stateParms_t &parms ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT
	};
	switch ( parms.stage ) {
		case STAGE_INIT:
			OverrideAnim( ANIMCHANNEL_LEGS );
			PlayAnim( ANIMCHANNEL_TORSO, "plugin_range_attack_big", parms.blendFrames );
			// Commit the beam to the position telegraphed by its windup. The
			// recovered clip locks at frame 70 and fires at 96 at double rate:
			// only about half a second to escape its wide spread. At the
			// campaign's run speed that makes the exposed descent pipes lethal
			// even while running. Keep the damage and burst, but give the whole
			// windup to move out of the aimed area. This flag and the recorded
			// enemy positions already survive saves and reset when actions end.
			enemy.fl.lockOrigin = true;
			return SRESULT_STAGE( STAGE_WAIT );

		case STAGE_WAIT:
			return AnimDone( ANIMCHANNEL_TORSO, parms.blendFrames ) ? SRESULT_DONE : SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
riMonsterValkaryne::TurnNinety
================
*/
stateResult_t riMonsterValkaryne::TurnNinety( const stateParms_t &parms, const char *anim ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT
	};
	switch ( parms.stage ) {
		case STAGE_INIT:
			OverrideAnim( ANIMCHANNEL_LEGS );
			PlayAnim( ANIMCHANNEL_TORSO, anim, parms.blendFrames );
			AnimTurn( 90.0f, true );
			return SRESULT_STAGE( STAGE_WAIT );

		case STAGE_WAIT:
			if ( !move.fl.moving && !AnimDone( ANIMCHANNEL_TORSO, 0 ) ) {
				return SRESULT_WAIT;
			}
			nextTurnTime = gameLocal.time + VALKARYNE_TURN_DELAY;
			AnimTurn( 0.0f, true );
			return SRESULT_DONE;
	}
	return SRESULT_ERROR;
}

/*
================
riMonsterValkaryne::State_Torso_TurnRight90
================
*/
stateResult_t riMonsterValkaryne::State_Torso_TurnRight90( const stateParms_t &parms ) {
	return TurnNinety( parms, "turn_right_90" );
}

/*
================
riMonsterValkaryne::State_Torso_TurnLeft90
================
*/
stateResult_t riMonsterValkaryne::State_Torso_TurnLeft90( const stateParms_t &parms ) {
	return TurnNinety( parms, "turn_left_90" );
}
