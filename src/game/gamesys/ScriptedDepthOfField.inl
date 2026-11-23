// openQ4 additions to the SDK game sources; see src/game/LICENSE.
// Shared SP/MP implementation of ScriptedDepthOfField.h, included once by
// each module's script/Script_Thread.cpp after Game_local.h.

namespace {

// The worldspawn spawn args that carry the state through savegames. A
// transition runs from the "0" values at t0 to the "1" values at t1 (game
// milliseconds); a focus entity, when named, stands in for the distance.
const char *const SCRIPTED_DOF_ACTIVE = "openq4_dof_active";
const char *const SCRIPTED_DOF_T0 = "openq4_dof_t0";
const char *const SCRIPTED_DOF_T1 = "openq4_dof_t1";
const char *const SCRIPTED_DOF_AMOUNT0 = "openq4_dof_amount0";
const char *const SCRIPTED_DOF_AMOUNT1 = "openq4_dof_amount1";
const char *const SCRIPTED_DOF_FOCUS0 = "openq4_dof_focus0";
const char *const SCRIPTED_DOF_FOCUS1 = "openq4_dof_focus1";
const char *const SCRIPTED_DOF_ENTITY1 = "openq4_dof_entity1";
const char *const SCRIPTED_DOF_FSTOP0 = "openq4_dof_fstop0";
const char *const SCRIPTED_DOF_FSTOP1 = "openq4_dof_fstop1";
const char *const SCRIPTED_DOF_FOCAL = "openq4_dof_focal";
const char *const SCRIPTED_DOF_RANGE = "openq4_dof_range";
const char *const SCRIPTED_DOF_MAXBLUR = "openq4_dof_maxblur";

// Where the effect stood when last evaluated, so the next change eases from
// it. Not saved: after a load a change starts from the saved target instead.
struct scriptedDofLast_t {
	bool	valid;
	float	amount;
	float	focus;
	float	fStop;
	idVec3	viewOrigin;
	idMat3	viewAxis;
};
scriptedDofLast_t scriptedDofLast;

idDict *ScriptedDof_State( void ) {
	return gameLocal.world != NULL ? &gameLocal.world->spawnArgs : NULL;
}

float ScriptedDof_Ease( float f ) {
	f = idMath::ClampFloat( 0.0f, 1.0f, f );
	return f * f * ( 3.0f - 2.0f * f );
}

// Distance along the view axis to an entity: an actor's eyes, otherwise the
// middle of its bounds. 0 when it is gone, which autofocuses.
float ScriptedDof_EntityDistance( const char *name, const idVec3 &viewOrigin, const idMat3 &viewAxis ) {
	if ( name == NULL || name[0] == '\0' ) {
		return 0.0f;
	}
	idEntity *ent = gameLocal.FindEntity( name );
	if ( ent == NULL ) {
		return 0.0f;
	}
	const idVec3 point = ent->IsType( idActor::GetClassType() )
		? static_cast<idActor *>( ent )->GetEyePosition()
		: ent->GetPhysics()->GetAbsBounds().GetCenter();
	return Max( 1.0f, ( point - viewOrigin ) * viewAxis[0] );
}

// The view a change measures entity distances from before any view has been
// evaluated: the local player's eyes.
bool ScriptedDof_FallbackView( idVec3 &origin, idMat3 &axis ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player == NULL ) {
		return false;
	}
	origin = player->GetEyePosition();
	axis = player->viewAngles.ToMat3();
	return true;
}

}	// namespace

renderViewDepthOfField_t ScriptedDepthOfField::Evaluate( const idVec3 &viewOrigin, const idMat3 &viewAxis ) {
	renderViewDepthOfField_t out;
	out.amount = 0.0f;
	const idDict *state = ScriptedDof_State();
	if ( state == NULL || !state->GetBool( SCRIPTED_DOF_ACTIVE ) ) {
		scriptedDofLast.valid = false;
		return out;
	}
	const int t0 = state->GetInt( SCRIPTED_DOF_T0 );
	const int t1 = state->GetInt( SCRIPTED_DOF_T1 );
	const float f = t1 > t0 ? ScriptedDof_Ease( static_cast<float>( gameLocal.time - t0 ) / static_cast<float>( t1 - t0 ) ) : 1.0f;

	const float amount = idMath::ClampFloat( 0.0f, 1.0f,
		idMath::Lerp( state->GetFloat( SCRIPTED_DOF_AMOUNT0 ), state->GetFloat( SCRIPTED_DOF_AMOUNT1 ), f ) );
	const char *entity = state->GetString( SCRIPTED_DOF_ENTITY1 );
	const float focus1 = entity[0] != '\0' ? ScriptedDof_EntityDistance( entity, viewOrigin, viewAxis )
		: state->GetFloat( SCRIPTED_DOF_FOCUS1 );
	const float focus0 = state->GetFloat( SCRIPTED_DOF_FOCUS0 );
	float focus = 0.0f;
	if ( focus1 > 0.0f ) {
		// focus pulls ease in log space, as a lens's focus ring turns
		focus = focus0 > 0.0f ? idMath::Exp( idMath::Lerp( idMath::Log( focus0 ), idMath::Log( focus1 ), f ) ) : focus1;
	}
	const float fStop = idMath::Lerp( state->GetFloat( SCRIPTED_DOF_FSTOP0, "2.8" ), state->GetFloat( SCRIPTED_DOF_FSTOP1, "2.8" ), f );

	out.amount = amount;
	out.focusDistance = focus;
	out.fStop = Max( 0.5f, fStop );
	out.focalLength = state->GetFloat( SCRIPTED_DOF_FOCAL );
	out.focusRange = state->GetFloat( SCRIPTED_DOF_RANGE );
	out.maxBlur = state->GetFloat( SCRIPTED_DOF_MAXBLUR );

	scriptedDofLast.valid = true;
	scriptedDofLast.amount = amount;
	scriptedDofLast.focus = focus;
	scriptedDofLast.fStop = out.fStop;
	scriptedDofLast.viewOrigin = viewOrigin;
	scriptedDofLast.viewAxis = viewAxis;
	return out;
}

void ScriptedDepthOfField::Set( float focusDistance, const char *focusEntity, float fStop, float blendSeconds ) {
	idDict *state = ScriptedDof_State();
	if ( state == NULL ) {
		return;
	}
	// Start from where the effect stands now; a first change fades in at the
	// target focus.
	float amount0 = 0.0f;
	float focus0 = 0.0f;
	float fStop0 = fStop;
	if ( state->GetBool( SCRIPTED_DOF_ACTIVE ) ) {
		idVec3 origin;
		idMat3 axis;
		bool haveView = scriptedDofLast.valid;
		if ( haveView ) {
			origin = scriptedDofLast.viewOrigin;
			axis = scriptedDofLast.viewAxis;
		} else {
			haveView = ScriptedDof_FallbackView( origin, axis );
		}
		if ( haveView ) {
			const renderViewDepthOfField_t now = Evaluate( origin, axis );
			amount0 = now.amount;
			focus0 = now.focusDistance;
			fStop0 = now.fStop;
		}
	}
	const char *entity = focusEntity != NULL ? focusEntity : "";
	if ( focus0 <= 0.0f && entity[0] == '\0' ) {
		focus0 = focusDistance;
	}
	const int blendMsec = static_cast<int>( Max( 0.0f, blendSeconds ) * 1000.0f );
	state->SetBool( SCRIPTED_DOF_ACTIVE, true );
	state->SetInt( SCRIPTED_DOF_T0, gameLocal.time );
	state->SetInt( SCRIPTED_DOF_T1, gameLocal.time + blendMsec );
	state->SetFloat( SCRIPTED_DOF_AMOUNT0, amount0 );
	state->SetFloat( SCRIPTED_DOF_AMOUNT1, 1.0f );
	state->SetFloat( SCRIPTED_DOF_FOCUS0, Max( 0.0f, focus0 ) );
	state->SetFloat( SCRIPTED_DOF_FOCUS1, Max( 0.0f, focusDistance ) );
	state->Set( SCRIPTED_DOF_ENTITY1, entity );
	state->SetFloat( SCRIPTED_DOF_FSTOP0, Max( 0.5f, fStop0 ) );
	state->SetFloat( SCRIPTED_DOF_FSTOP1, Max( 0.5f, fStop ) );
}

void ScriptedDepthOfField::SetLens( float focalLength, float focusRange, float maxBlur ) {
	idDict *state = ScriptedDof_State();
	if ( state == NULL ) {
		return;
	}
	state->SetFloat( SCRIPTED_DOF_FOCAL, Max( 0.0f, focalLength ) );
	state->SetFloat( SCRIPTED_DOF_RANGE, Max( 0.0f, focusRange ) );
	state->SetFloat( SCRIPTED_DOF_MAXBLUR, Max( 0.0f, maxBlur ) );
}

void ScriptedDepthOfField::Clear( float blendSeconds ) {
	idDict *state = ScriptedDof_State();
	if ( state == NULL || !state->GetBool( SCRIPTED_DOF_ACTIVE ) ) {
		return;
	}
	// Fade out holding the current focus.
	const char *entity = state->GetString( SCRIPTED_DOF_ENTITY1 );
	const idStr held = entity;
	const float focus = state->GetFloat( SCRIPTED_DOF_FOCUS1 );
	const float fStop = state->GetFloat( SCRIPTED_DOF_FSTOP1, "2.8" );
	Set( focus, held.c_str(), fStop, blendSeconds );
	state->SetFloat( SCRIPTED_DOF_AMOUNT1, 0.0f );
}
