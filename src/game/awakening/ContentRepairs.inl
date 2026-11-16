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

// A sound declaration the content really has: not one the engine made up for a
// name it was asked for, which a missing sound also gets.
static bool Awakening_SoundDeclared( const char *name ) {
	const idDecl *decl = declManager->FindType( DECL_SOUND, name, false );
	return decl != NULL && !decl->IsImplicit() && decl->GetState() != DS_DEFAULTED;
}

// A declaration, or a sample named by its path. Voice samples are stored under
// the language's folder, which only the sample loader maps.
static bool Awakening_SoundPlayable( const char *name ) {
	idStr path = name;
	if ( path.Find( '/' ) < 0 ) {
		return Awakening_SoundDeclared( name );
	}
	// file lookups ignore case; a lower-case request is the portable one
	path.ToLower();
	if ( fileSystem->ReadFile( path, NULL, NULL ) > 0 ) {
		return true;
	}
	return path.Replace( "/vo/", va( "/vo_%s/", cvarSystem->GetCVarString( "sys_lang" ) ) ) &&
		fileSystem->ReadFile( path, NULL, NULL ) > 0;
}

typedef struct awakeningDialogueKey_s {
	const char *map;		// maps/game/<map>, without extension
	const char *entity;		// the map entity that speaks
	const char *key;		// the key the mission script speaks, or the game reads when the speaker is talked to
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
	// The two Fox marines' third reply when talked to. The game reads
	// lipsync_talk_loop1, as every retail map spells it; this map alone has
	// lipsync_talk_loop_1, so Meakim and Pappas fell silent after two replies.
	{ "maps/game/m04_prison",				"sq2_marine_2",		"lipsync_talk_loop1",		"lipsync_talk_loop_1" },
	{ "maps/game/m04_prison",				"sq2_marine_3",		"lipsync_talk_loop1",		"lipsync_talk_loop_1" },
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
		if ( !recorded[ 0 ] || !Awakening_SoundDeclared( recorded ) ) {
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
		if ( Awakening_SoundDeclared( line.stale ) || !Awakening_SoundDeclared( line.recorded ) ) {
			return sound;
		}
		gameLocal.DPrintf( "Awakening: radio line '%s' plays '%s'\n", line.stale, line.recorded );
		return line.recorded;
	}
	return sound;
}

// A radio line a script names but nothing declares, such as Cryofac's
// vo_darn_dntwrryct (m08), recorded without a declaration. The sound system
// fills a missing sound with a 186 ms tone, so the alpha beeps over the radio
// there. A sample named by its path is left to the sound system, which
// reports it.
bool Awakening_UnwrittenLine( const char *sound ) {
	if ( !sound || !sound[ 0 ] || !Awakening_ContentRepairsActive() || idStr::FindChar( sound, '/' ) >= 0 ) {
		return false;
	}
	if ( Awakening_SoundDeclared( sound ) ) {
		return false;
	}
	gameLocal.DPrintf( "Awakening: radio line '%s' is not declared; the radio stays silent\n", sound );
	return true;
}

/*
===============================================================================

	Map entities

	The same slip in a map's own keys: a speaker or a character names a sound
	that does not exist, or one left over from the retail map it was built from.

===============================================================================
*/

typedef struct awakeningEntitySound_s {
	const char *map;		// maps/game/<map>, without extension
	const char *entity;
	const char *key;
	const char *written;	// the value the map has
	const char *intended;	// a declaration, or a sample by its path
	bool leftover;			// the written sound exists, but belongs to another scene
} awakeningEntitySound_t;

static const awakeningEntitySound_t awakeningEntitySounds[] = {
	// Cryofac was mission 9 when its music was delivered: the tracks are
	// m9_cryo_*, the speakers ask for m8_cryo_*, and the level had no music.
	{ "maps/game/m08_cryofac",	"music_intro",		"s_shader",	"sound/music/m8_cryo_intro.ogg",	"sound/music/m9_cryo_intro.ogg",	false },
	{ "maps/game/m08_cryofac",	"music_cryo_cue",	"s_shader",	"sound/music/m8_cryo_cue.ogg",		"sound/music/m9_cryo_cue.ogg",		false },
	{ "maps/game/m08_cryofac",	"music_cryo_exit",	"s_shader",	"sound/music/m8_cryo_exit.ogg",		"sound/music/m9_cryo_exit.ogg",		false },
	// Renner's radio call: "What is your position?" The first reply's speaker
	// kept retail MCC's line ("But a Strogg unit is headed directly for Cougar
	// squad."). Its twin talkRichards_2 has the recorded second reply, and the
	// script's commented-out call beside the trigger names the first.
	{ "maps/game/m06_mcc",		"talkRichards_1",	"s_shader",	"vo_1_1_11_360_2",					"vo_marine_noideawe",				true },
};

void Awakening_CorrectEntitySounds( idDict &args ) {
	if ( !Awakening_ContentRepairsActive() ) {
		return;
	}
	const char *name = args.GetString( "name" );
	if ( !name[ 0 ] ) {
		return;
	}
	idStr map;
	for ( int i = 0; i < (int)( sizeof( awakeningEntitySounds ) / sizeof( awakeningEntitySounds[ 0 ] ) ); i++ ) {
		const awakeningEntitySound_t &sound = awakeningEntitySounds[ i ];
		if ( idStr::Icmp( name, sound.entity ) != 0 || idStr::Icmp( args.GetString( sound.key ), sound.written ) != 0 ) {
			continue;
		}
		if ( !map.Length() ) {
			map = gameLocal.GetMapName();
			map.BackSlashesToSlashes();
			map.StripFileExtension();
		}
		if ( map.Icmp( sound.map ) != 0 || !Awakening_SoundPlayable( sound.intended ) ||
			( !sound.leftover && Awakening_SoundPlayable( sound.written ) ) ) {
			continue;
		}
		args.Set( sound.key, sound.intended );
	}
}

/*
===============================================================================

	Speech

	A line can have its key, its lip-sync declaration and its recording and
	still be silent: its sound declaration names a folder or a file that is
	spelt differently on disk. The speech starts under the lip-sync
	declaration that carries its mouth shapes and plays the recording by path.

===============================================================================
*/

typedef struct awakeningSpeech_s {
	const char *map;		// maps/game/<map>, without extension: where the line is preloaded
	const char *speech;		// the name the speaker's dialogue key holds
	const char *lipSync;	// the lip-sync declaration of the line
	const char *declared;	// the sample its sound declaration asks for, which does not exist
	const char *sample;		// the recording
} awakeningSpeech_t;

static const awakeningSpeech_t awakeningSpeech[] = {
	// Navajas sets and blows the charges (m08). These six declarations alone
	// spell the mission's folder with a hyphen, which also ends the path for
	// the declaration parser; every other m08 line has M08_Cryo.
	{ "maps/game/m08_cryofac",	"vo_nava_herewegset2",		"vo_nava_herewegset2",		"sound/vo/M08-Cryo/nava_herewegset2.wav",		"sound/vo/m08_cryo/nava_herewegset2.wav" },
	{ "maps/game/m08_cryofac",	"vo_nava_seconchrg2",		"vo_nava_seconchrg2",		"sound/vo/M08-Cryo/nava_seconchrg2.wav",		"sound/vo/m08_cryo/nava_seconchrg2.wav" },
	{ "maps/game/m08_cryofac",	"vo_nava_thatsit2",			"vo_nava_thatsit2",			"sound/vo/M08-Cryo/nava_thatsit2.wav",			"sound/vo/m08_cryo/nava_thatsit2.wav" },
	{ "maps/game/m08_cryofac",	"vo_nava_demvawaychrg2",	"vo_nava_demvawaychrg2",	"sound/vo/M08-Cryo/nava_demvawaychrg2.wav",		"sound/vo/m08_cryo/nava_demvawaychrg2.wav" },
	{ "maps/game/m08_cryofac",	"vo_nava_thosechrglive2",	"vo_nava_thosechrglive2",	"sound/vo/M08-Cryo/nava_thosechrglive2.wav",	"sound/vo/m08_cryo/nava_thosechrglive2.wav" },
	{ "maps/game/m08_cryofac",	"vo_nava_detonat2",			"vo_nava_detonat2",			"sound/vo/M08-Cryo/nava_detonat2.wav",			"sound/vo/m08_cryo/nava_detonat2.wav" },
	// Private Pappas's second reply when talked to (m04). The map and the
	// definitions name vo_fox3_prismed; the line was generated as vo_fox_prismed
	// over fox_prismed.wav, and recorded as fox3_prismed.wav.
	{ "maps/game/m04_prison",	"vo_fox3_prismed",			"vo_fox_prismed",			"sound/vo/M04_Prison/fox_prismed.wav",			"sound/vo/m04_prison/fox3_prismed.wav" },
	// The Pain Lord's three tube marines scream as the machine processes each
	// one (m05). Their lines are declared, keyed and captioned "(Screams of
	// agony)", but no take was ever recorded. These are stand-in screams with
	// no words: the expansion's unused take of a screaming marine for the
	// first, and two of retail Quake 4's Stroggification screams (medlabs) for
	// the others, so the three men do not share one voice.
	{ "maps/game/m05_bio",		"vo_marine1_agony1",		"vo_marine1_agony1",		"sound/vo/M05_Bio/marine1_agony1.wav",			"sound/vo/m05_bio/scrmar4_pain.wav" },
	{ "maps/game/m05_bio",		"vo_marine2_agony1",		"vo_marine2_agony1",		"sound/vo/M05_Bio/marine2_agony1.wav",			"sound/vo/medlabs_1/2_1_1_01_5.ogg" },
	{ "maps/game/m05_bio",		"vo_marine3_agony1",		"vo_marine3_agony1",		"sound/vo/M05_Bio/marine3_agony1.wav",			"sound/vo/medlabs_1/2_1_1_01_7.ogg" },
};

// Whether each row applies to the installed content: 0 not yet asked, 1 yes,
// -1 no. Asked once a map (the precache clears it), not on every spoken line.
static signed char awakeningSpeechApplies[ sizeof( awakeningSpeech ) / sizeof( awakeningSpeech[ 0 ] ) ];

bool Awakening_SpeechNames( const char *speech, const char *&lipSync, const char *&sound ) {
	if ( !speech || !speech[ 0 ] || !Awakening_ContentRepairsActive() ) {
		return false;
	}
	for ( int i = 0; i < (int)( sizeof( awakeningSpeech ) / sizeof( awakeningSpeech[ 0 ] ) ); i++ ) {
		const awakeningSpeech_t &line = awakeningSpeech[ i ];
		if ( idStr::Icmp( speech, line.speech ) != 0 ) {
			continue;
		}
		if ( awakeningSpeechApplies[ i ] == 0 ) {
			// content whose declaration reaches a sample keeps it
			const bool applies = !Awakening_SoundPlayable( line.declared ) && Awakening_SoundPlayable( line.sample );
			awakeningSpeechApplies[ i ] = applies ? 1 : -1;
			if ( applies ) {
				gameLocal.DPrintf( "Awakening: speech '%s' plays '%s'\n", line.speech, line.sample );
			}
		}
		if ( awakeningSpeechApplies[ i ] < 0 ) {
			return false;
		}
		lipSync = line.lipSync;
		sound = line.sample;
		return true;
	}
	return false;
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
	// the recordings a map's speech plays by path
	memset( awakeningSpeechApplies, 0, sizeof( awakeningSpeechApplies ) );
	idStr map = gameLocal.GetMapName();
	map.BackSlashesToSlashes();
	map.StripFileExtension();
	for ( int i = 0; i < (int)( sizeof( awakeningSpeech ) / sizeof( awakeningSpeech[ 0 ] ) ); i++ ) {
		const char *lipSync = NULL, *sound = NULL;
		if ( map.Icmp( awakeningSpeech[ i ].map ) == 0 && Awakening_SpeechNames( awakeningSpeech[ i ].speech, lipSync, sound ) ) {
			declManager->FindSound( sound );
		}
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
