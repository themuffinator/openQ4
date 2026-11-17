
#include "Game_local.h"

#include "ClassicLights.h"

/*
===============================================================================

	Classic dynamic lights

	See ClassicLights.h for what this is and where the colors come from.

===============================================================================
*/

// A classic dlight is cheap but not free: each one is a real interaction pass
// in this renderer, not a vertex tint like it was in Quake II.  Cap the number
// that can be alive at once and recycle the oldest, exactly the way
// CL_AllocDlight did.
const int	MAX_CLASSIC_FLASHES			= 24;

// Beyond this the flash cannot plausibly reach anything the viewer can see, so
// it would only burn a pool slot that a nearby event wants.
const float	CLASSIC_FLASH_CULL_DIST		= 3000.0f;

// Classic dlights collapse as they die instead of just dimming; that shrink is
// most of what makes the pop read as a flash rather than a fade.
const float	CLASSIC_FLASH_END_SCALE		= 0.55f;

// Quake II held a muzzle flash for 0.1s.  Widening the authored 200-unit Quake 4
// flash to 300 lands on the same radius Quake III used for its weapon dlights.
const int	CLASSIC_MUZZLEFLASH_TIME	= 100;
const float	CLASSIC_MUZZLEFLASH_WIDEN	= 1.5f;
const float	CLASSIC_MUZZLEFLASH_RADIUS	= 200.0f;
const float	CLASSIC_MUZZLEFLASH_JITTER	= 32.0f;

// Fallback for anything that explodes without a recognised projectile class.
const float	CLASSIC_EXPLOSION_RADIUS	= 260.0f;
const int	CLASSIC_EXPLOSION_TIME		= 220;
const float	CLASSIC_EXPLOSION_MAX		= 512.0f;

// A shadowed explosion flash keeps the entities it starts inside out of its
// shadows: the burnt shell of a barrel around the light would otherwise black
// out the room the flash is meant to light.
const int	MAX_CLASSIC_FLASH_SUPPRESS	= 4;

// Light ids for those flashes, one per pool slot.  High enough never to meet
// the weapon ids (hundreds) or the actor flashlight ids (entityNumber + 1).
const int	CLASSIC_FLASH_LIGHT_ID_BASE	= 0x7e000000;

// A second explosion flash this close in space and time to a live one is the
// same event: a projectile's detonation flash and its detonation effect, or a
// barrel's flash and its fx_explode.
const float	CLASSIC_FLASH_MERGE_DIST	= 160.0f;
const int	CLASSIC_FLASH_MERGE_TIME	= 50;

typedef struct classicFlash_s {
	renderLight_t		light;
	qhandle_t			handle;
	int					startTime;
	int					endTime;
	float				startRadius;
	idVec3				color;
	bool				explosion;			// an explosion flash, not a muzzle flash
	bool				generic;			// raised for an effect, not for a known projectile or barrel
	int					numSuppressed;
	idEntityPtr<idEntity>	suppressed[ MAX_CLASSIC_FLASH_SUPPRESS ];
} classicFlash_t;

static classicFlash_t	classicFlashes[ MAX_CLASSIC_FLASHES ];
static bool				classicFlashesInitialized = false;

// Deliberately not gameLocal.random: that sequence is part of the networked
// simulation, and drawing from it on a client only would desync the game.
static idRandom			classicFlashRandom( 0x51ede4d );

/*
===============================================================================

	Projectile classes

	Matched as a case-insensitive substring of the projectile classname, first
	match wins, so the unlit families have to come before anything they could
	fall through into.  "flyLightAuthoredSP" marks the two classes whose shipped
	single-player fly effect already carries a light segment of its own -- adding
	ours on top of those would double the projectile's contribution.

		effects/weapons/rocketlauncher/fly.fx		lights/fire2, 200, orange
		effects/weapons/grenadelauncher/trail.fx	lights/grenade_flicker, 60, red

	Their multiplayer counterparts (fly_mp.fx, trail_mp.fx) carry no light, which
	is why multiplayer takes the classic light for those classes as well.

===============================================================================
*/

typedef struct classicLightClass_s {
	const char *	keyword;
	const char *	flyShader;
	float			flyColor[3];
	float			flyRadius;
	bool			flyLightAuthoredSP;
	float			burstColor[3];
	float			burstRadius;
	int				burstTime;
} classicLightClass_t;

static const classicLightClass_t classicLightClasses[] = {
	// Nothing here is a "bright projectile": bullets and nails are metal, and
	// the rest are debris.  Quake II and Quake III did not light them either,
	// and there are far too many of them in flight to afford it.
	{ "bullet",			NULL,						{ 0.0f,			0.0f,		0.0f		},	0.0f,	false,	{ 0.0f,			0.0f,		0.0f		},	0.0f,	0	},
	{ "nail",			NULL,						{ 0.0f,			0.0f,		0.0f		},	0.0f,	false,	{ 0.0f,			0.0f,		0.0f		},	0.0f,	0	},
	{ "debris",			NULL,						{ 0.0f,			0.0f,		0.0f		},	0.0f,	false,	{ 0.0f,			0.0f,		0.0f		},	0.0f,	0	},
	{ "glass",			NULL,						{ 0.0f,			0.0f,		0.0f		},	0.0f,	false,	{ 0.0f,			0.0f,		0.0f		},	0.0f,	0	},
	{ "spawner",		NULL,						{ 0.0f,			0.0f,		0.0f		},	0.0f,	false,	{ 0.0f,			0.0f,		0.0f		},	0.0f,	0	},
	{ "whip",			NULL,						{ 0.0f,			0.0f,		0.0f		},	0.0f,	false,	{ 0.0f,			0.0f,		0.0f		},	0.0f,	0	},

	// effects/weapons/rocketlauncher/fly.fx
	{ "rocket",			"lights/fire2",				{ 0.905882f,	0.517647f,	0.160784f	},	200.0f,	true,	{ 1.0f,			0.823529f,	0.290196f	},	320.0f,	250	},
	// effects/weapons/grenadelauncher/trail.fx
	{ "grenade",		"lights/grenade_flicker",	{ 0.972549f,	0.286275f,	0.086274f	},	60.0f,	true,	{ 1.0f,			0.823529f,	0.290196f	},	300.0f,	250	},
	// effects/weapons/napalmgun/globburn.fx
	{ "napalm",			NULL,						{ 0.921569f,	0.545098f,	0.321569f	},	110.0f,	false,	{ 1.0f,			0.588235f,	0.278431f	},	300.0f,	350	},
	// effects/weapons/dmg/core.fx -- the dark matter core light is 1.5 units
	// across, so the gun's signature violet never reaches the room without this.
	{ "dmg",			"lights/round",				{ 0.501961f,	0.0f,		1.0f		},	150.0f,	false,	{ 0.627451f,	0.219608f,	1.0f		},	360.0f,	400	},
	{ "darkmatter",		"lights/round",				{ 0.501961f,	0.0f,		1.0f		},	150.0f,	false,	{ 0.627451f,	0.219608f,	1.0f		},	360.0f,	400	},

	// The energy weapons all share the "0.7 0.8 1" flashColor their weapon defs
	// author, so their bolts and impacts stay in the same blue-white family.
	{ "hyperblaster",	NULL,						{ 0.7f,			0.8f,		1.0f		},	120.0f,	false,	{ 0.7f,			0.8f,		1.0f		},	150.0f,	130	},
	{ "blaster",		NULL,						{ 0.7f,			0.8f,		1.0f		},	100.0f,	false,	{ 0.7f,			0.8f,		1.0f		},	130.0f,	120	},
	{ "plasma",			NULL,						{ 0.7f,			0.8f,		1.0f		},	120.0f,	false,	{ 0.7f,			0.8f,		1.0f		},	150.0f,	130	},
	{ "energy",			NULL,						{ 0.7f,			0.8f,		1.0f		},	120.0f,	false,	{ 0.7f,			0.8f,		1.0f		},	150.0f,	130	},

	// effects/monsters/scientist/gas_grenade_mflash.fx
	{ "acid",			NULL,						{ 0.403922f,	0.886275f,	0.427451f	},	90.0f,	false,	{ 0.403922f,	0.886275f,	0.427451f	},	160.0f,	160	},
	{ "vomit",			NULL,						{ 0.403922f,	0.886275f,	0.427451f	},	90.0f,	false,	{ 0.403922f,	0.886275f,	0.427451f	},	160.0f,	160	},

	// effects/monsters/scientist/concussion_fly.fx
	{ "fireball",		NULL,						{ 0.839216f,	0.266667f,	0.019608f	},	120.0f,	false,	{ 1.0f,			0.823529f,	0.290196f	},	280.0f,	220	},
	// effects/monsters/strogg_flyer/bomb_burst.fx
	{ "bomb",			NULL,						{ 0.0f,			0.0f,		0.0f		},	0.0f,	false,	{ 1.0f,			0.823529f,	0.290196f	},	300.0f,	200	},
	{ "cannon",			NULL,						{ 0.905882f,	0.517647f,	0.160784f	},	140.0f,	false,	{ 1.0f,			0.823529f,	0.290196f	},	280.0f,	220	},
};

static const int classicLightClassCount = sizeof( classicLightClasses ) / sizeof( classicLightClasses[ 0 ] );

// effects/monsters/strogg_flyer/bomb_burst.fx -- the one explosion flash Raven
// did author, and the closest thing the shipped assets have to a house color
// for "something just blew up".
static const float classicExplosionColor[3] = { 1.0f, 0.823529f, 0.290196f };

// The flashColor every ballistic weapon in the game authors.
static const float classicMuzzleColor[3] = { 1.0f, 0.8f, 0.4f };

/*
================
G_ClassicLightScale
================
*/
static float G_ClassicLightScale( void ) {
	return idMath::ClampFloat( 0.25f, 4.0f, g_classicDynamicLightScale.GetFloat() );
}

/*
================
G_ClassicLightMaterial

Quake 4's own effect lights use lights/round for a plain radial falloff; fall
back to the engine default if a mod has stripped it.
================
*/
static const idMaterial *G_ClassicLightMaterial( const char *shaderName ) {
	const idMaterial *shader = NULL;

	if ( shaderName && *shaderName ) {
		shader = declManager->FindMaterial( shaderName, false );
	}
	if ( !shader ) {
		shader = declManager->FindMaterial( "lights/round", false );
	}
	if ( !shader ) {
		shader = declManager->FindMaterial( "lights/defaultPointLight", false );
	}
	return shader;
}

/*
================
G_ClassicLightsEnabled
================
*/
bool G_ClassicLightsEnabled( void ) {
	if ( !g_classicDynamicLights.GetBool() ) {
		return false;
	}
	if ( !gameRenderWorld ) {
		return false;
	}
	// A dedicated server never draws, so it must never take a light handle.
	if ( gameLocal.isServer && gameLocal.localClientNum < 0 ) {
		return false;
	}
	return true;
}

/*
================
G_ClassicFlashAllowed

Mirrors the spawn guards idGameLocal::PlayEffect uses, so a predicted frame that
is re-run on a multiplayer client does not stack a fresh flash every pass.
================
*/
static bool G_ClassicFlashAllowed( void ) {
	if ( !G_ClassicLightsEnabled() ) {
		return false;
	}
	if ( !gameLocal.isNewFrame ) {
		return false;
	}
	if ( !gameLocal.GetLocalPlayer() ) {
		return false;
	}
	return true;
}

/*
================
G_FindClassicLightClass
================
*/
static const classicLightClass_t *G_FindClassicLightClass( const idDict &spawnArgs ) {
	const char *classname = spawnArgs.GetString( "classname", "" );

	if ( !classname || !*classname ) {
		return NULL;
	}

	for ( int i = 0; i < classicLightClassCount; i++ ) {
		if ( idStr::FindText( classname, classicLightClasses[ i ].keyword, false ) >= 0 ) {
			return &classicLightClasses[ i ];
		}
	}
	return NULL;
}

/*
================
G_ClassicSplashRadius

Big explosions should flash bigger than small ones, so let the damage def the
projectile already carries drive the size.
================
*/
static float G_ClassicSplashRadius( const idDict &spawnArgs ) {
	const char *splashName = spawnArgs.GetString( "def_splash_damage", "" );

	if ( !splashName || !*splashName ) {
		return 0.0f;
	}

	const idDict *splashDict = gameLocal.FindEntityDefDict( splashName, false );
	if ( !splashDict ) {
		return 0.0f;
	}
	return splashDict->GetFloat( "radius", "0" );
}

/*
================
G_ClassicLightProfile
================
*/
bool G_ClassicLightProfile( const idDict &spawnArgs, classicLightProfile_t &profile ) {
	const classicLightClass_t *cls = G_FindClassicLightClass( spawnArgs );

	profile.flyShader	= NULL;
	profile.flyColor.Zero();
	profile.flyRadius	= 0.0f;
	profile.burstColor.Set( classicExplosionColor[0], classicExplosionColor[1], classicExplosionColor[2] );
	profile.burstRadius	= 0.0f;
	profile.burstTime	= CLASSIC_EXPLOSION_TIME;

	if ( cls ) {
		profile.flyShader = cls->flyShader;
		profile.flyColor.Set( cls->flyColor[0], cls->flyColor[1], cls->flyColor[2] );
		profile.flyRadius = cls->flyRadius;
		profile.burstColor.Set( cls->burstColor[0], cls->burstColor[1], cls->burstColor[2] );
		profile.burstRadius = cls->burstRadius;
		profile.burstTime = cls->burstTime;
	} else {
		// An unrecognised projectile only earns a flash if it actually carries
		// splash damage; otherwise every stray impact in the game would pop.
		if ( G_ClassicSplashRadius( spawnArgs ) > 0.0f ) {
			profile.burstRadius = CLASSIC_EXPLOSION_RADIUS;
		}
	}

	// Content can always say what it wants instead of relying on the table.
	idVec3 overrideColor;
	if ( spawnArgs.GetVector( "classic_light_color", "", overrideColor ) ) {
		profile.flyColor = overrideColor;
		profile.burstColor = overrideColor;
	}
	if ( spawnArgs.GetFloat( "classic_light_radius", "0" ) > 0.0f ) {
		profile.flyRadius = spawnArgs.GetFloat( "classic_light_radius", "0" );
	}
	if ( !spawnArgs.GetBool( "classic_light", "1" ) ) {
		profile.flyRadius = 0.0f;
		profile.burstRadius = 0.0f;
	}

	// Scale the detonation flash with the blast it belongs to.
	if ( profile.burstRadius > 0.0f ) {
		const float splashRadius = G_ClassicSplashRadius( spawnArgs );
		if ( splashRadius > 0.0f ) {
			profile.burstRadius = Max( profile.burstRadius, splashRadius * 1.5f );
		}
		profile.burstRadius = Min( profile.burstRadius, CLASSIC_EXPLOSION_MAX );
	}

	return ( profile.flyRadius > 0.0f || profile.burstRadius > 0.0f );
}

/*
================
G_ClassicProjectileLight
================
*/
bool G_ClassicProjectileLight( const idDict &spawnArgs, renderLight_t &light ) {
	classicLightProfile_t profile;

	if ( !G_ClassicLightsEnabled() ) {
		return false;
	}
	if ( !G_ClassicLightProfile( spawnArgs, profile ) || profile.flyRadius <= 0.0f ) {
		return false;
	}

	const classicLightClass_t *cls = G_FindClassicLightClass( spawnArgs );
	if ( cls && cls->flyLightAuthoredSP && !gameLocal.isMultiplayer &&
		 spawnArgs.GetFloat( "classic_light_radius", "0" ) <= 0.0f ) {
		// The shipped single-player fly effect already lights this one.
		return false;
	}

	const idMaterial *shader = G_ClassicLightMaterial( profile.flyShader );
	if ( !shader ) {
		return false;
	}

	const float radius = profile.flyRadius * G_ClassicLightScale();

	memset( &light, 0, sizeof( light ) );
	light.shader						= shader;
	light.pointLight					= true;
	light.lightRadius[0]				= radius;
	light.lightRadius[1]				= radius;
	light.lightRadius[2]				= radius;
	light.shaderParms[SHADERPARM_RED]	= profile.flyColor.x;
	light.shaderParms[SHADERPARM_GREEN]	= profile.flyColor.y;
	light.shaderParms[SHADERPARM_BLUE]	= profile.flyColor.z;
	light.shaderParms[SHADERPARM_ALPHA]	= 1.0f;
	light.shaderParms[SHADERPARM_TIMEOFFSET] = -MS2SEC( gameLocal.time );
	light.detailLevel					= DEFAULT_LIGHT_DETAIL_LEVEL;
	// idProjectile keeps its own model out of these shadows
	light.noShadows						= !G_DynamicLightShadowsEnabled();

	return true;
}

/*
================
G_AllocClassicFlash

Free slot if there is one, otherwise steal whichever flash is closest to dying.
A slot is live while endTime is set; the render handle is only taken on the
first update, so it cannot stand in for occupancy.
================
*/
/*
================
G_ReleaseClassicFlashSuppress

Hands the entities a flash kept out of its shadows back their shadows.
touchEntities is false while the world is being torn down underneath us.
================
*/
static void G_ReleaseClassicFlashSuppress( classicFlash_t &flash, bool touchEntities ) {
	for ( int i = 0; i < flash.numSuppressed; i++ ) {
		idEntity *ent = touchEntities ? flash.suppressed[ i ].GetEntity() : NULL;
		if ( ent && ent->GetRenderEntity()->suppressShadowInLightID == flash.light.lightId ) {
			ent->GetRenderEntity()->suppressShadowInLightID = 0;
			ent->UpdateVisuals();
		}
		flash.suppressed[ i ] = NULL;
	}
	flash.numSuppressed = 0;
}

static classicFlash_t *G_AllocClassicFlash( void ) {
	classicFlash_t *oldest = NULL;

	if ( !classicFlashesInitialized ) {
		memset( classicFlashes, 0, sizeof( classicFlashes ) );
		for ( int i = 0; i < MAX_CLASSIC_FLASHES; i++ ) {
			classicFlashes[ i ].handle = -1;
		}
		classicFlashesInitialized = true;
	}

	for ( int i = 0; i < MAX_CLASSIC_FLASHES; i++ ) {
		if ( classicFlashes[ i ].endTime == 0 ) {
			return &classicFlashes[ i ];
		}
		if ( !oldest || classicFlashes[ i ].endTime < oldest->endTime ) {
			oldest = &classicFlashes[ i ];
		}
	}

	if ( oldest->handle != -1 ) {
		gameRenderWorld->FreeLightDef( oldest->handle );
		oldest->handle = -1;
	}
	G_ReleaseClassicFlashSuppress( *oldest, true );
	return oldest;
}

/*
================
G_FindClassicExplosionFlash

A live explosion flash raised for the same event: started this frame or the
one before, near the same spot.
================
*/
static classicFlash_t *G_FindClassicExplosionFlash( const idVec3 &origin ) {
	if ( !classicFlashesInitialized ) {
		return NULL;
	}
	for ( int i = 0; i < MAX_CLASSIC_FLASHES; i++ ) {
		classicFlash_t &flash = classicFlashes[ i ];
		if ( flash.endTime == 0 || !flash.explosion ) {
			continue;
		}
		if ( gameLocal.time - flash.startTime > CLASSIC_FLASH_MERGE_TIME ) {
			continue;
		}
		if ( ( flash.light.origin - origin ).LengthSqr() > Square( CLASSIC_FLASH_MERGE_DIST ) ) {
			continue;
		}
		return &flash;
	}
	return NULL;
}

/*
================
G_SuppressClassicFlashEnclosers

An explosion flash starts inside whatever blew up: a barrel's burnt shell, a
turret or a flyer coming apart.  Those keep their shadows in every other light
but stay out of this one's, which is the same arrangement the firer's body and
gun have with their muzzle flash.  Level geometry (func_static) is left alone,
and so is an entity whose suppression slot already belongs to someone else.
================
*/
static void G_SuppressClassicFlashEnclosers( classicFlash_t &flash ) {
	idEntity *touching[ 16 ];
	const idVec3 &origin = flash.light.origin;
	const idBounds probe( origin - idVec3( 1.0f, 1.0f, 1.0f ), origin + idVec3( 1.0f, 1.0f, 1.0f ) );
	const int count = gameLocal.EntitiesTouchingBounds( gameLocal.currentThinkingEntity, probe, -1, touching, 16 );

	for ( int i = 0; i < count && flash.numSuppressed < MAX_CLASSIC_FLASH_SUPPRESS; i++ ) {
		idEntity *ent = touching[ i ];
		if ( !ent || ent == gameLocal.world || ent->IsType( idStaticEntity::GetClassType() ) ) {
			continue;
		}
		renderEntity_t *renderEnt = ent->GetRenderEntity();
		if ( !renderEnt || !renderEnt->hModel || renderEnt->noShadow ) {
			continue;
		}
		const int owned = renderEnt->suppressShadowInLightID;
		if ( owned != 0 && ( owned < CLASSIC_FLASH_LIGHT_ID_BASE ||
							 owned >= CLASSIC_FLASH_LIGHT_ID_BASE + MAX_CLASSIC_FLASHES ) ) {
			continue;
		}
		renderEnt->suppressShadowInLightID = flash.light.lightId;
		ent->UpdateVisuals();
		flash.suppressed[ flash.numSuppressed++ ] = ent;
	}
}

/*
================
G_RaiseClassicFlash

shadowLightId names a light id whose shadows the caster already suppresses
(the firing weapon's muzzle flash).  An explosion flash passes 0 and gets an id
of its own, with the entities it starts inside kept out of its shadows.
================
*/
static void G_RaiseClassicFlash( const idVec3 &origin, const idVec3 &color, float radius, int durationMS,
								 bool explosion, bool generic, int shadowLightId, bool castShadows ) {
	if ( !G_ClassicFlashAllowed() ) {
		return;
	}
	if ( radius <= 0.0f || durationMS <= 0 ) {
		return;
	}

	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( ( player->GetPhysics()->GetOrigin() - origin ).LengthSqr() >
		 Square( CLASSIC_FLASH_CULL_DIST + radius ) ) {
		return;
	}

	// A listen server runs every instance; only the one being watched draws.
	if ( gameLocal.isListenServer && gameLocal.currentThinkingEntity &&
		 gameLocal.currentThinkingEntity->GetInstance() != player->GetInstance() ) {
		return;
	}

	const idMaterial *shader = G_ClassicLightMaterial( NULL );
	if ( !shader ) {
		return;
	}

	classicFlash_t *flash = NULL;
	if ( explosion ) {
		classicFlash_t *same = G_FindClassicExplosionFlash( origin );
		if ( same ) {
			if ( generic || !same->generic ) {
				// already lit; the first specific flash for an event stands
				return;
			}
			// a projectile or barrel flash knows its colour and size; it
			// replaces the generic flash an effect raised for the same blast
			if ( same->handle != -1 ) {
				gameRenderWorld->FreeLightDef( same->handle );
				same->handle = -1;
			}
			G_ReleaseClassicFlashSuppress( *same, true );
			flash = same;
		}
	}
	if ( !flash ) {
		flash = G_AllocClassicFlash();
	}

	memset( &flash->light, 0, sizeof( flash->light ) );
	flash->light.shader			= shader;
	flash->light.pointLight		= true;
	flash->light.origin			= origin;
	flash->light.axis			= mat3_identity;
	flash->light.detailLevel	= DEFAULT_LIGHT_DETAIL_LEVEL;
	flash->light.noShadows		= !castShadows;
	flash->light.shaderParms[SHADERPARM_ALPHA]		= 1.0f;
	flash->light.shaderParms[SHADERPARM_TIMEOFFSET]	= -MS2SEC( gameLocal.time );

	flash->color		= color;
	flash->startRadius	= radius;
	flash->startTime	= gameLocal.time;
	flash->endTime		= gameLocal.time + durationMS;
	flash->handle		= -1;
	flash->explosion	= explosion;
	flash->generic		= generic;
	flash->numSuppressed = 0;

	if ( castShadows ) {
		if ( shadowLightId != 0 ) {
			flash->light.lightId = shadowLightId;
		} else {
			flash->light.lightId = CLASSIC_FLASH_LIGHT_ID_BASE + (int)( flash - classicFlashes );
			G_SuppressClassicFlashEnclosers( *flash );
		}
	}
}

/*
================
G_ClassicFlash
================
*/
void G_ClassicFlash( const idVec3 &origin, const idVec3 &color, float radius, int durationMS ) {
	G_RaiseClassicFlash( origin, color, radius, durationMS, true, false, 0, G_DynamicLightShadowsEnabled() );
}

/*
================
G_ClassicMuzzleFlash
================
*/
void G_ClassicMuzzleFlash( const idDict &weaponDef, const idVec3 &origin, int shadowLightId, bool castShadows ) {
	idVec4 flashColor;
	idVec3 color;

	if ( !G_ClassicFlashAllowed() ) {
		return;
	}

	weaponDef.GetVec4( "flashColor", "0 0 0 0", flashColor );
	color.Set( flashColor.x, flashColor.y, flashColor.z );
	if ( color.LengthSqr() <= 0.0f ) {
		color.Set( classicMuzzleColor[0], classicMuzzleColor[1], classicMuzzleColor[2] );
	}

	float radius = weaponDef.GetFloat( "flashRadius", "0" );
	if ( radius <= 0.0f ) {
		radius = CLASSIC_MUZZLEFLASH_RADIUS;
	}
	radius *= CLASSIC_MUZZLEFLASH_WIDEN;

	// Quake II jittered the muzzle flash radius every shot so repeated fire
	// never strobes at a fixed size.
	radius += classicFlashRandom.RandomFloat() * CLASSIC_MUZZLEFLASH_JITTER;

	// Without a light id the firer suppresses, the gun would shadow its own
	// flash, so a muzzle flash only casts when the weapon hands one over.
	G_RaiseClassicFlash( origin, color, radius * G_ClassicLightScale(), CLASSIC_MUZZLEFLASH_TIME,
		false, false, shadowLightId, castShadows && shadowLightId != 0 );
}

/*
================
G_ClassicExplosionFlash
================
*/
void G_ClassicExplosionFlash( const idDict &spawnArgs, const idVec3 &origin, float defaultRadius ) {
	classicLightProfile_t profile;

	if ( !G_ClassicFlashAllowed() ) {
		return;
	}

	// A class that matched the table has already had its say, including saying
	// "never".  Only a def the table does not recognise falls back to the caller's
	// default, which is how barrels and damagables get a flash at all.
	const bool matched = ( G_FindClassicLightClass( spawnArgs ) != NULL );

	G_ClassicLightProfile( spawnArgs, profile );

	float radius = profile.burstRadius;
	if ( radius <= 0.0f ) {
		if ( matched || !spawnArgs.GetBool( "classic_light", "1" ) ) {
			return;
		}
		radius = defaultRadius;
	}
	if ( radius <= 0.0f ) {
		return;
	}

	G_RaiseClassicFlash( origin, profile.burstColor, radius * G_ClassicLightScale(), profile.burstTime,
		true, false, 0, G_DynamicLightShadowsEnabled() );
}

/*
===============================================================================

	Effect explosions

	Projectiles and barrels raise their own flashes, but most of what blows up
	in Quake 4 only plays an effect: dying turrets, flyers and hover tanks,
	harvester parts, vehicles and every scripted level explosion.  An effect is
	taken for an explosion when its name says so (explosion, explode,
	detonate, death, destroy, or anything under effects/explosions/) and it
	burns: it
	draws a gfx/effects/fire/ material.  The second test keeps gore bursts,
	electrical deaths and smoke trails dark.  An effect that already carries a
	light segment lights itself.

===============================================================================
*/

typedef enum {
	CLASSIC_EFFECT_UNKNOWN = 0,
	CLASSIC_EFFECT_PLAIN,
	CLASSIC_EFFECT_EXPLOSION
} classicEffectKind_t;

// Indexed by effect decl index; decls outlive maps, so the verdicts do too.
static idList<byte>		classicEffectKinds;

/*
================
G_ClassicEffectIsExplosion
================
*/
static bool G_ClassicEffectIsExplosion( const idDecl *effect ) {
	const int index = effect->Index();
	if ( index < 0 ) {
		return false;
	}
	if ( index < classicEffectKinds.Num() && classicEffectKinds[ index ] != CLASSIC_EFFECT_UNKNOWN ) {
		return classicEffectKinds[ index ] == CLASSIC_EFFECT_EXPLOSION;
	}
	if ( index >= classicEffectKinds.Num() ) {
		const int oldNum = classicEffectKinds.Num();
		classicEffectKinds.SetNum( index + 1 );
		for ( int i = oldNum; i < classicEffectKinds.Num(); i++ ) {
			classicEffectKinds[ i ] = CLASSIC_EFFECT_UNKNOWN;
		}
	}

	bool explosion = false;
	idStr name = effect->GetName();
	name.BackSlashesToSlashes();
	name.ToLower();
	if ( name.Find( "explo" ) >= 0 || name.Find( "detonat" ) >= 0 || name.Find( "death" ) >= 0 ||
		 name.Find( "destroy" ) >= 0 ||
		 name.Find( "effects/explosions/" ) >= 0 ) {
		const int length = effect->GetTextLength();
		if ( length > 0 ) {
			idTempArray<char> text( length + 1 );
			effect->GetText( text.Ptr() );
			text[ length ] = '\0';

			idLexer lexer( LEXFL_NOERRORS | LEXFL_NOWARNINGS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWPATHNAMES );
			lexer.LoadMemory( text.Ptr(), length, effect->GetName() );

			bool burns = false;
			bool lit = false;
			idToken token;
			idToken prev;
			idToken prev2;
			while ( lexer.ReadToken( &token ) ) {
				// a light segment opens as: light "name" {
				if ( token == "{" && prev.type == TT_STRING && !prev2.Icmp( "light" ) ) {
					lit = true;
					break;
				}
				if ( token.type == TT_STRING && !idStr::Icmpn( token.c_str(), "gfx/effects/fire/", 17 ) ) {
					burns = true;
				}
				prev2 = prev;
				prev = token;
			}
			explosion = burns && !lit;
		}
	}

	classicEffectKinds[ index ] = explosion ? CLASSIC_EFFECT_EXPLOSION : CLASSIC_EFFECT_PLAIN;
	if ( explosion ) {
		common->DPrintf( "classic dynamic lights: '%s' flashes as an explosion\n", effect->GetName() );
	}
	return explosion;
}

/*
================
G_ClassicEffectFlash
================
*/
void G_ClassicEffectFlash( const idDecl *effect, const idVec3 &origin, bool loop ) {
	if ( loop || !effect || !G_ClassicFlashAllowed() ) {
		return;
	}
	if ( !G_ClassicEffectIsExplosion( effect ) ) {
		return;
	}
	const float radius = CLASSIC_EXPLOSION_RADIUS * G_ClassicLightScale();
	const idVec3 color( classicExplosionColor[0], classicExplosionColor[1], classicExplosionColor[2] );
	G_RaiseClassicFlash( origin, color, radius, CLASSIC_EXPLOSION_TIME,
		true, true, 0, G_DynamicLightShadowsEnabled() );
}

/*
================
G_ClassicWorldExplosionFlash

For call sites that already know something exploded -- barrels, damagables --
even when the def carries no recognised projectile class or splash damage.
================
*/
void G_ClassicWorldExplosionFlash( const idDict &spawnArgs, const idVec3 &origin ) {
	G_ClassicExplosionFlash( spawnArgs, origin, CLASSIC_EXPLOSION_RADIUS );
}

/*
================
G_UpdateClassicLights
================
*/
void G_UpdateClassicLights( void ) {
	if ( !classicFlashesInitialized ) {
		return;
	}
	if ( !gameRenderWorld ) {
		return;
	}

	// Everything visible rides the presentation clock, so the decay is smooth
	// between authoritative tics instead of stepping at the game frame rate.
	const int now = gameLocal.GetPresentationTimeMsec();
	const bool enabled = g_classicDynamicLights.GetBool();

	for ( int i = 0; i < MAX_CLASSIC_FLASHES; i++ ) {
		classicFlash_t &flash = classicFlashes[ i ];

		if ( flash.endTime == 0 ) {
			continue;
		}

		if ( !enabled || now >= flash.endTime || flash.endTime <= flash.startTime ) {
			if ( flash.handle != -1 ) {
				gameRenderWorld->FreeLightDef( flash.handle );
				flash.handle = -1;
			}
			G_ReleaseClassicFlashSuppress( flash, true );
			flash.endTime = 0;
			continue;
		}

		const float frac = idMath::ClampFloat( 0.0f, 1.0f,
			( float )( now - flash.startTime ) / ( float )( flash.endTime - flash.startTime ) );
		const float intensity = 1.0f - frac;
		const float radius = flash.startRadius * ( 1.0f - frac * ( 1.0f - CLASSIC_FLASH_END_SCALE ) );

		flash.light.lightRadius[0] = radius;
		flash.light.lightRadius[1] = radius;
		flash.light.lightRadius[2] = radius;
		flash.light.shaderParms[SHADERPARM_RED]		= flash.color.x * intensity;
		flash.light.shaderParms[SHADERPARM_GREEN]	= flash.color.y * intensity;
		flash.light.shaderParms[SHADERPARM_BLUE]	= flash.color.z * intensity;

		if ( flash.handle == -1 ) {
			flash.handle = gameRenderWorld->AddLightDef( &flash.light );
		} else {
			gameRenderWorld->UpdateLightDef( flash.handle, &flash.light );
		}
	}
}

/*
================
G_FreeClassicLights
================
*/
void G_FreeClassicLights( void ) {
	if ( !classicFlashesInitialized ) {
		return;
	}

	for ( int i = 0; i < MAX_CLASSIC_FLASHES; i++ ) {
		if ( classicFlashes[ i ].handle != -1 ) {
			if ( gameRenderWorld ) {
				gameRenderWorld->FreeLightDef( classicFlashes[ i ].handle );
			}
			classicFlashes[ i ].handle = -1;
		}
		G_ReleaseClassicFlashSuppress( classicFlashes[ i ], false );
		classicFlashes[ i ].endTime = 0;
	}
}
