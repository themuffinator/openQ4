/*
 * openQ4 corrections to names the Awakening's content mistypes.
 * See LICENSES/QUAKE-4-SDK-EULA.rtf and ContentRepairs.h. Nothing recovered is
 * bundled: every replacement is a name the player's own installed content defines.
 * Evidence for each pair: docs/dev/awakening-missions.md, "Silent names".
 */

#include "ContentRepairs.h"

static idCVar g_awakeningContentRepairs( "g_awakeningContentRepairs", "1", CVAR_GAME | CVAR_BOOL,
	"Resolve the dialogue, sound and joint names the Awakening's content mistypes. 0 leaves them as the alpha has them: silent lines, missing effects and eyes at the feet" );

static bool Awakening_ContentRepairsActive( void ) {
	return !gameLocal.isMultiplayer && g_awakeningContentRepairs.GetBool() && FS_AwakeningActive();
}

/*
===============================================================================

	Dialogue

	idAI::Speak looks its key up in the speaker's spawn arguments and returns
	quietly when the key is absent, so a mistyped key drops the line without a
	warning. The generated <mission>_vo files hold the writers' text for every
	recorded line, which identifies the take each stale name meant.

===============================================================================
*/

typedef struct awakeningDialogueKey_s {
	const char *map;		// maps/game/<map>, without extension
	const char *entity;		// the map entity that speaks
	const char *key;		// the key the mission script speaks
	const char *source;		// the speaker's key that holds the line, or the line's decl
} awakeningDialogueKey_t;

static const awakeningDialogueKey_t awakeningDialogueKeys[] = {
	// Navajas calls "Strogg!" as Tak sets the bunker charge. The script names
	// Tak's take of the word, which Navajas cannot speak; his own is recorded.
	{ "maps/game/m01_stranarus_trench1",	"navajas",			"lipsync_tak_strg1",		"lipsync_nava_strgg1" },
	{ "maps/game/m01_stranarus_trench2",	"navajas",			"lipsync_tak_strg1",		"lipsync_nava_strgg1" },
	// Tak at the second cellblock door, and Darnell's "Above us!" as the gunner
	// drops in: both keys lost their lipsync_ prefix.
	{ "maps/game/m04_prison",				"tak",				"tak_pris_door2",			"lipsync_tak_pris_door2" },
	{ "maps/game/m04_prison",				"darnell",			"vo_darn_aboveus",			"lipsync_darn_aboveus" },
	// Private Hoff is placed as a generic unarmed marine, without the dialogue
	// definition that carries his mumbling.
	{ "maps/game/m04_prison",				"insane_marine",	"lipsync_insane1_mumble",	"vo_insane1_mumble" },
	// Three misspelt keys: Navajas's reply to Tak's sightseeing, his alarm as
	// the cryocene floods in, and Tak's death at the Valkaryne's hands.
	{ "maps/game/m08_cryofac",				"navajas",			"lipsync_nava_smrttak",		"lipsync_nava_smarttak" },
	{ "maps/game/m08_cryofac",				"navajas",			"lipsync_nava_whathll",		"lipsync_nava_whathellgo" },
	{ "maps/game/m08_cryofac",				"tak",				"lipsync_tak_deathscrm",	"lipsync_tak_die" },
};

void Awakening_RestoreDialogueKeys( idDict &args ) {
	if ( !Awakening_ContentRepairsActive() ) {
		return;
	}
	const char *name = args.GetString( "name" );
	if ( !name[ 0 ] ) {
		return;
	}
	idStr map;
	for ( int i = 0; i < (int)( sizeof( awakeningDialogueKeys ) / sizeof( awakeningDialogueKeys[ 0 ] ) ); i++ ) {
		const awakeningDialogueKey_t &line = awakeningDialogueKeys[ i ];
		if ( idStr::Icmp( name, line.entity ) != 0 || args.FindKey( line.key ) != NULL ) {
			continue;
		}
		if ( !map.Length() ) {
			map = gameLocal.GetMapName();
			map.BackSlashesToSlashes();
			map.StripFileExtension();
		}
		if ( map.Icmp( line.map ) != 0 ) {
			continue;
		}
		// a key of the speaker's own definition, or the line's declaration
		const char *recorded = idStr::Icmpn( line.source, "lipsync_", 8 ) == 0 ? args.GetString( line.source ) : line.source;
		if ( !recorded[ 0 ] || declManager->FindType( DECL_SOUND, recorded, false ) == NULL ) {
			continue;
		}
		args.Set( line.key, recorded );
		gameLocal.DPrintf( "Awakening: '%s' speaks '%s' as '%s'\n", name, line.key, recorded );
	}
}

typedef struct awakeningRadioLine_s {
	const char *stale;		// the sound the mission script names
	const char *recorded;	// the same line under the name it was recorded as
} awakeningRadioLine_t;

static const awakeningRadioLine_t awakeningRadioLines[] = {
	// The MCC cannon defence (m06 invasion). The script's own comments quote
	// each call; the lines were re-recorded under speaker names and the script
	// kept the scene numbers, so the whole battle's radio traffic was silent.
	{ "vo_7_13_11",		"vo_marine6_theydroppte" },		// "they're dropping teleporters!"
	{ "vo_7_13_18",		"vo_marine1_gotcnt3" },			// "got contact, 3 incoming squadrons bearing..."
	{ "vo_7_13_2",		"vo_marine1_thereact" },		// "going for the reactor"
	{ "vo_7_13_6",		"vo_marine3_panthersq" },		// "panther squad requesting support on deck 12"
	{ "vo_7_13_12",		"vo_marine6_thatnegpan" },		// "that's a negative panther, they've locked down..."
	{ "vo_7_13_5",		"vo_marine3_nexassalms" },		// "nexus's assault's almost prepped"
	{ "vo_7_13_1",		"vo_marine1_contaceng" },		// "got contact, watch the engine room"
	{ "vo_7_13_9",		"vo_marine5_igotthreemen" },	// "3 men down on deck 5, requesting medical"
	{ "vo_7_13_3",		"vo_marine2_thecomport" },		// "coming from port, hold them off"
	// Darnell's death cry over the radio as the Valkaryne takes him (m08).
	{ "vo_darn_dir",	"vo_darn_die" },
};

const char *Awakening_RecordedLine( const char *sound ) {
	if ( !sound || !sound[ 0 ] || !Awakening_ContentRepairsActive() ) {
		return sound;
	}
	for ( int i = 0; i < (int)( sizeof( awakeningRadioLines ) / sizeof( awakeningRadioLines[ 0 ] ) ); i++ ) {
		const awakeningRadioLine_t &line = awakeningRadioLines[ i ];
		if ( idStr::Icmp( sound, line.stale ) != 0 ) {
			continue;
		}
		// content that supplies the written name keeps it
		const idDecl *written = declManager->FindType( DECL_SOUND, line.stale, false );
		if ( written != NULL && written->GetState() != DS_DEFAULTED ) {
			return sound;
		}
		if ( declManager->FindType( DECL_SOUND, line.recorded, false ) == NULL ) {
			return sound;
		}
		gameLocal.DPrintf( "Awakening: radio line '%s' plays '%s'\n", line.stale, line.recorded );
		return line.recorded;
	}
	return sound;
}

/*
===============================================================================

	Joints

	A missing joint fails as quietly as a missing dialogue key: an actor
	without its eye joint and without an eye_height sees from its origin, at
	its feet, and a frame command's effect on a missing joint is not played.

===============================================================================
*/

typedef struct awakeningJoint_s {
	const char *written;
	const char *intended;
} awakeningJoint_t;

static const awakeningJoint_t awakeningJoints[] = {
	// monster_q4x_tank "joint_eyeOffset": the mesh's eyelids are l_/r_uplid.
	{ "rt_up_lid",		"r_uplid" },
	// monster_q4x_retch "joint_eyeOffset": the mesh's skull is one joint, head.
	{ "jaw",			"head" },
	// Tank rocket_attack: "ai_attack rocket tube1" and "fx fx_rocket_flash
	// gunBarrelReal" share each launch frame; the flash belongs to the launch.
	{ "gunBarrelReal",	"tube1" },
};

jointHandle_t Awakening_IntendedJoint( const idAnimator *animator, const char *name ) {
	if ( !animator || !name || !name[ 0 ] || !Awakening_ContentRepairsActive() ) {
		return INVALID_JOINT;
	}
	for ( int i = 0; i < (int)( sizeof( awakeningJoints ) / sizeof( awakeningJoints[ 0 ] ) ); i++ ) {
		if ( idStr::Icmp( name, awakeningJoints[ i ].written ) == 0 ) {
			const jointHandle_t joint = animator->GetJointHandle( awakeningJoints[ i ].intended );
			static int noted = 0;
			if ( joint != INVALID_JOINT && !( noted & ( 1 << i ) ) ) {
				noted |= 1 << i;
				gameLocal.DPrintf( "Awakening: joint '%s' is '%s'\n", name, awakeningJoints[ i ].intended );
			}
			return joint;
		}
	}
	return INVALID_JOINT;
}

typedef struct awakeningEffect_s {
	const char *model;		// the model declaration whose frame commands name the key
	const char *key;		// an fx_ key no definition using that model supplies
	const char *effect;		// the effect those frame commands were written for
} awakeningEffect_t;

static const awakeningEffect_t awakeningEffects[] = {
	// The Tank's rocket_attack frame commands are the Iron Maiden's: retail's
	// "ai_attack rocket gunBarrelReal" + "fx fx_rocket_flash gunBarrelReal",
	// with the attack moved to tube1. The Maiden's "fx_rocket_flash" key was not
	// carried over, and no Tank rocket flash was ever authored.
	{ "model_monster_tank",	"fx_rocket_flash",	"effects/monsters/iron_maiden/muzzleflash.fx" },
};

const idDecl *Awakening_IntendedEffect( idEntity *ent, const char *key ) {
	if ( !ent || !key || !key[ 0 ] || !Awakening_ContentRepairsActive() || ent->spawnArgs.FindKey( key ) != NULL ) {
		return NULL;
	}
	for ( int i = 0; i < (int)( sizeof( awakeningEffects ) / sizeof( awakeningEffects[ 0 ] ) ); i++ ) {
		const awakeningEffect_t &effect = awakeningEffects[ i ];
		if ( idStr::Icmp( key, effect.key ) != 0 || idStr::Icmp( ent->spawnArgs.GetString( "model" ), effect.model ) != 0 ) {
			continue;
		}
		const idDecl *decl = declManager->FindType( DECL_EFFECT, effect.effect, false );
		static int noted = 0;
		if ( decl != NULL && !( noted & ( 1 << i ) ) ) {
			noted |= 1 << i;
			gameLocal.DPrintf( "Awakening: '%s' on %s plays '%s'\n", key, effect.model, effect.effect );
		}
		return decl;
	}
	return NULL;
}

// The definitions that would precache these effects do not name them.
void Awakening_PrecacheContentRepairs( void ) {
	if ( !Awakening_ContentRepairsActive() ) {
		return;
	}
	// a few small effects: cheaper to load with every campaign map than to
	// parse a model declaration to learn whether the map will use it
	for ( int i = 0; i < (int)( sizeof( awakeningEffects ) / sizeof( awakeningEffects[ 0 ] ) ); i++ ) {
		declManager->FindType( DECL_EFFECT, awakeningEffects[ i ].effect, false );
	}
}

typedef struct awakeningMirroredJoint_s {
	const char *written;		// the joint the frame command names
	const char *counterpart;	// its mirror image, which the mesh has
	const char *pairLeft;		// a left/right pair that fixes the mirror plane
	const char *pairRight;
} awakeningMirroredJoint_t;

static const awakeningMirroredJoint_t awakeningMirroredJoints[] = {
	// The opening's three dropships light fx_thrusters on "engine_righ" and then
	// engine_left. Retail's dropship mesh has engine_left and engine_center but
	// never had a right engine joint, so only the left plume showed.
	{ "engine_righ",	"engine_left",	"gun_left_01",	"gun_right_01" },
};

jointHandle_t Awakening_IntendedEffectJoint( idEntity *ent, const char *name, idVec3 &jointOffset ) {
	jointOffset.Zero();
	if ( !ent || !name || !name[ 0 ] || !Awakening_ContentRepairsActive() ) {
		return INVALID_JOINT;
	}
	idAnimator *animator = ent->GetAnimator();
	if ( !animator ) {
		return INVALID_JOINT;
	}
	const jointHandle_t intended = Awakening_IntendedJoint( animator, name );
	if ( intended != INVALID_JOINT ) {
		return intended;
	}
	for ( int i = 0; i < (int)( sizeof( awakeningMirroredJoints ) / sizeof( awakeningMirroredJoints[ 0 ] ) ); i++ ) {
		const awakeningMirroredJoint_t &mirror = awakeningMirroredJoints[ i ];
		if ( idStr::Icmp( name, mirror.written ) != 0 ) {
			continue;
		}
		const jointHandle_t counterpart = animator->GetJointHandle( mirror.counterpart );
		const jointHandle_t pairLeft = animator->GetJointHandle( mirror.pairLeft );
		const jointHandle_t pairRight = animator->GetJointHandle( mirror.pairRight );
		idVec3 origin, left, right;
		idMat3 axis, unused;
		if ( counterpart == INVALID_JOINT || pairLeft == INVALID_JOINT || pairRight == INVALID_JOINT ||
			!animator->GetJointTransform( counterpart, gameLocal.time, origin, axis ) ||
			!animator->GetJointTransform( pairLeft, gameLocal.time, left, unused ) ||
			!animator->GetJointTransform( pairRight, gameLocal.time, right, unused ) ) {
			return INVALID_JOINT;
		}
		// reflect the counterpart through the plane midway between the pair
		idVec3 normal = right - left;
		if ( normal.Normalize() < 1.0f ) {
			return INVALID_JOINT;
		}
		const idVec3 mirrored = origin - normal * ( 2.0f * ( ( origin - ( left + right ) * 0.5f ) * normal ) );
		jointOffset = ( mirrored - origin ) * axis.Transpose();
		static int noted = 0;
		if ( !( noted & ( 1 << i ) ) ) {
			noted |= 1 << i;
			gameLocal.DPrintf( "Awakening: effect joint '%s' mirrors '%s' %.1f units across '%s'/'%s'\n", name,
				mirror.counterpart, jointOffset.Length(), mirror.pairLeft, mirror.pairRight );
		}
		return counterpart;
	}
	return INVALID_JOINT;
}
