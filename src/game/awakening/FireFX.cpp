#include "../Game_local.h"

/*
===============================================================================

	riFireFX

	func_fire_volume: a burning gas vent that only the Freeze Gun can put out.

	While it burns, actors touching the volume are hurt by def_damage every
	damageDelay milliseconds. Damage from a def carrying "filter_freeze" wears
	the fire down: below half health the large fire gives way to a small one,
	and at zero health it goes out and leaves smoke. Target lights shrink with
	the fire (their "fireCount" says how many fires share them), and the fire's
	other targets are activated once it is out.

	The volume's brushes must use a shot-clip material so the freeze beam can
	hit it; the entity keeps projectile-only collision while it burns.

===============================================================================
*/

class riFireFX : public idTrigger {
public:
	CLASS_PROTOTYPE( riFireFX );

							riFireFX( void );

	void					Spawn( void );
	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

	virtual void			Damage( idEntity *inflictor, idEntity *attacker, const idVec3 &dir, const char *damageDefName, const float damageScale, const int location );

private:
	enum fireStage_t {
		FIRE_LARGE,
		FIRE_SMALL,
		FIRE_OUT
	};

	void					StartStageEffect( void );
	void					StopStageEffects( void );
	void					ShrinkTargetLights( int damageTaken );
	void					ActivateTargetsWhenOut( void );
	void					UpdateContents( void );
	void					Event_RestoreStageEffect( void );

	void					Event_Touch( idEntity *other, trace_t *trace );
	void					Event_Activate( idEntity *activator );
	void					Event_LookAtTarget( void );

	int						nextDamageTime;
	int						stage;
	bool					burning;
	rvClientEntityPtr<rvClientEffect>	effect;
};

// shares the event number rvEffect registers under the same name
const idEventDef EV_FireFX_LookAtTarget( "lookAtTarget", NULL );
const idEventDef EV_FireFX_RestoreStageEffect( "<fireFXRestoreStage>", NULL );

CLASS_DECLARATION( idTrigger, riFireFX )
	EVENT( EV_Touch,				riFireFX::Event_Touch )
	EVENT( EV_Activate,				riFireFX::Event_Activate )
	EVENT( EV_FireFX_LookAtTarget,	riFireFX::Event_LookAtTarget )
	EVENT( EV_FireFX_RestoreStageEffect, riFireFX::Event_RestoreStageEffect )
END_CLASS

/*
================
riFireFX::riFireFX
================
*/
riFireFX::riFireFX( void ) {
	nextDamageTime = 0;
	stage = FIRE_LARGE;
	burning = false;
}

/*
================
riFireFX::Spawn
================
*/
void riFireFX::Spawn( void ) {
	health = spawnArgs.GetInt( "health", "100" );
	fl.takedamage = true;

	nextDamageTime = gameLocal.time;
	stage = FIRE_LARGE;
	burning = spawnArgs.GetBool( "on", "1" );
	UpdateContents();
	if ( burning ) {
		BecomeActive( TH_THINK );
		StartStageEffect();
	}
}

/*
================
riFireFX::Save
================
*/
void riFireFX::Save( idSaveGame *savefile ) const {
	savefile->WriteInt( nextDamageTime );
	savefile->WriteInt( stage );
	savefile->WriteBool( burning );
}

/*
================
riFireFX::Restore
================
*/
void riFireFX::Restore( idRestoreGame *savefile ) {
	savefile->ReadInt( nextDamageTime );
	savefile->ReadInt( stage );
	savefile->ReadBool( burning );
	// Also repair the solid clip contents stored by older saves.
	UpdateContents();

	// Client effects restore after their owner. Reconcile on the first frame
	// instead of allocating another loop while the saved graph is incomplete.
	PostEventMS( &EV_FireFX_RestoreStageEffect, 0 );
}

void riFireFX::Event_RestoreStageEffect( void ) {
	static const char * const stageEffects[] = { "fx_fireLarge", "fx_fireSmall", "fx_smoke" };
	effect = NULL;
	if ( !burning || stage < FIRE_LARGE || stage > FIRE_OUT ) {
		StopStageEffects();
		return;
	}
	const idDecl *stageDecls[3];
	for ( int i = FIRE_LARGE; i <= FIRE_OUT; ++i ) {
		stageDecls[i] = gameLocal.GetEffect( spawnArgs, stageEffects[i] );
	}
	for ( rvClientEntity *cent = clientEntities.Next(); cent != NULL; cent = cent->bindNode.Next() ) {
		if ( !cent->IsType( rvClientEffect::Type ) ) {
			continue;
		}
		rvClientEffect *fx = static_cast<rvClientEffect *>( cent );
		bool isStageEffect = false;
		for ( int i = FIRE_LARGE; i <= FIRE_OUT; ++i ) {
			isStageEffect |= stageDecls[i] != NULL && fx->GetRenderEffect()->declEffect == stageDecls[i];
		}
		if ( !isStageEffect ) {
			continue;
		}
		if ( fx->GetRenderEffect()->declEffect == stageDecls[stage] && !effect.IsValid() ) {
			effect = fx;
		} else {
			fx->Stop( true );
		}
	}
	if ( effect.IsValid() ) {
		Event_LookAtTarget();
	} else {
		StartStageEffect();
	}
}

// Match common/shot_clip: freeze projectiles hit the flame, but players and
// squadmates can cross it and receive touch damage. A quenched/disabled vent
// must not remain an invisible wall or absorb shots meant for enemies beyond.
void riFireFX::UpdateContents( void ) {
	GetPhysics()->SetContents( burning && stage < FIRE_OUT ? CONTENTS_PROJECTILE | CONTENTS_TRIGGER : 0 );
}

/*
================
riFireFX::StartStageEffect
================
*/
void riFireFX::StartStageEffect( void ) {
	static const char * const stageEffects[] = { "fx_fireLarge", "fx_fireSmall", "fx_smoke" };

	if ( stage < FIRE_LARGE || stage > FIRE_OUT ) {
		return;
	}
	const idMat3 upright = ( -GetPhysics()->GetGravityNormal() ).ToMat3();
	effect = PlayEffect( stageEffects[ stage ], GetPhysics()->GetOrigin(), upright, true );
	Event_LookAtTarget();
	PostEventMS( &EV_FireFX_LookAtTarget, 0 );
}

/*
================
riFireFX::StopStageEffects
================
*/
void riFireFX::StopStageEffects( void ) {
	StopEffect( "fx_fireLarge" );
	StopEffect( "fx_fireSmall" );
	StopEffect( "fx_smoke" );
}

/*
================
riFireFX::ShrinkTargetLights

Each light starts at its own light_radius. A fire that loses a fraction of its
health takes that fraction, split across the light's fireCount fires, off the
light's current radius, so several fires feeding one light each dim a share.
================
*/
void riFireFX::ShrinkTargetLights( int damageTaken ) {
	const float maxHealth = spawnArgs.GetFloat( "health", "100" );
	if ( maxHealth <= 0.0f ) {
		return;
	}

	for ( int i = 0; i < targets.Num(); i++ ) {
		idEntity *ent = targets[ i ].GetEntity();
		if ( ent == NULL || !ent->IsType( idLight::GetClassType() ) ) {
			continue;
		}
		idLight *light = static_cast<idLight *>( ent );

		const idVec3 baseRadius = light->spawnArgs.GetVector( "light_radius", "320 320 320" );
		const float fireCount = light->spawnArgs.GetFloat( "fireCount", "1" );
		if ( baseRadius.x <= 0.0f || fireCount <= 0.0f ) {
			continue;
		}

		const float current = light->GetRadius().x / baseRadius.x;
		const float scale = idMath::ClampFloat( 0.0f, 1.0f, current - ( damageTaken / maxHealth ) / fireCount );
		light->SetRadiusXYZ( baseRadius.x * scale, baseRadius.y * scale, baseRadius.z * scale );
	}
}

/*
================
riFireFX::ActivateTargetsWhenOut

Lights were dimmed along the way; everything else is triggered now.
================
*/
void riFireFX::ActivateTargetsWhenOut( void ) {
	for ( int i = 0; i < targets.Num(); i++ ) {
		idEntity *ent = targets[ i ].GetEntity();
		if ( ent == NULL || ent->IsType( idLight::GetClassType() ) ) {
			continue;
		}
		if ( ent->RespondsTo( EV_Activate ) || ent->HasSignal( SIG_TRIGGER ) ) {
			ent->Signal( SIG_TRIGGER );
			ent->ProcessEvent( &EV_Activate, this );
		}
		for ( int j = 0; j < MAX_RENDERENTITY_GUI; j++ ) {
			if ( ent->GetRenderEntity()->gui[ j ] ) {
				ent->GetRenderEntity()->gui[ j ]->Trigger( gameLocal.time );
			}
		}
	}
}

/*
================
riFireFX::Damage

Only damage from a def with "filter_freeze" affects the fire.
================
*/
void riFireFX::Damage( idEntity *inflictor, idEntity *attacker, const idVec3 &dir, const char *damageDefName, const float damageScale, const int location ) {
	const idDict *damageDef = gameLocal.FindEntityDefDict( damageDefName, false );
	if ( damageDef == NULL ) {
		gameLocal.Warning( "Unknown damageDef '%s'", damageDefName );
		return;
	}
	if ( health <= 0 || !burning || !damageDef->GetBool( "filter_freeze" ) ) {
		return;
	}

	const int oldHealth = health;
	idTrigger::Damage( inflictor, attacker, dir, damageDefName, damageScale, location );
	ShrinkTargetLights( oldHealth - Max( health, 0 ) );

	if ( stage == FIRE_LARGE && health > 0 && health < spawnArgs.GetInt( "health", "100" ) / 2 ) {
		StopEffect( "fx_fireLarge" );
		stage = FIRE_SMALL;
		StartStageEffect();
	}
	if ( stage < FIRE_OUT && health <= 0 ) {
		StopStageEffects();
		stage = FIRE_OUT;
		UpdateContents();
		StartStageEffect();
		ActivateTargetsWhenOut();
	}
}

/*
================
riFireFX::Event_Touch
================
*/
void riFireFX::Event_Touch( idEntity *other, trace_t *trace ) {
	if ( !burning || stage >= FIRE_OUT || gameLocal.time < nextDamageTime ) {
		return;
	}
	if ( other == NULL || !other->IsType( idActor::GetClassType() ) ) {
		return;
	}

	other->Damage( this, NULL, vec3_origin, spawnArgs.GetString( "def_damage", "damage_painTrigger" ), 1.0f, INVALID_JOINT );
	nextDamageTime = gameLocal.time + spawnArgs.GetInt( "damageDelay", "1000" );
}

/*
================
riFireFX::Event_Activate

Toggles the fire on and off.
================
*/
void riFireFX::Event_Activate( idEntity *activator ) {
	burning = !burning;
	UpdateContents();
	if ( !burning ) {
		BecomeInactive( TH_THINK );
		StopStageEffects();
		return;
	}
	BecomeActive( TH_THINK );
	StartStageEffect();
}

/*
================
riFireFX::Event_LookAtTarget

Aims the running effect at the first fxtarget.
================
*/
void riFireFX::Event_LookAtTarget( void ) {
	rvClientEffect *fx = effect.GetEntity();
	if ( fx == NULL ) {
		return;
	}
	for ( const idKeyValue *kv = spawnArgs.MatchPrefix( "fxtarget" ); kv != NULL; kv = spawnArgs.MatchPrefix( "fxtarget", kv ) ) {
		idEntity *target = gameLocal.FindEntity( kv->GetValue() );
		if ( target == NULL ) {
			continue;
		}
		const idVec3 dir = target->GetPhysics()->GetOrigin() - GetPhysics()->GetOrigin();
		fx->SetEndOrigin( target->GetPhysics()->GetOrigin() );
		fx->SetAxis( dir.ToMat3() );
		return;
	}
}
