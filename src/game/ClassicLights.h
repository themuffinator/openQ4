
#ifndef __GAME_CLASSICLIGHTS_H__
#define __GAME_CLASSICLIGHTS_H__

/*
===============================================================================

	Classic dynamic lights

	Quake II and Quake III lit the world from the events themselves: every
	muzzle flash, every glowing projectile and every explosion allocated a
	short-lived point light that decayed away over a tenth of a second or so.
	Quake 4 instead relies on authored BSE light segments, and most of the
	shipped effects carry none -- player projectiles other than the
	single-player rocket and grenade are unlit, and no explosion in the game
	flashes the room at all.

	This is that classic layer, rebuilt on top of the render lights the game
	already owns.  It is purely a client presentation feature: nothing here is
	simulated, saved or networked, and every light is derived from an event
	that already reaches the client on its own.

	Colors are taken from the shipped Quake 4 assets (weapon def "flashColor"
	values, and the tints of the light segments Raven did author in
	effects/weapons and effects/monsters) so the classic lights read as part of
	the same effect rather than a wash of arbitrary color over it.

===============================================================================
*/

// Per-projectile-class light description, resolved from the projectile
// classname (see ClassicLights.cpp for the table).
typedef struct classicLightProfile_s {
	const char *		flyShader;			// light material for the tracking light, NULL for the default round point light
	idVec3				flyColor;
	float				flyRadius;			// 0 when the class gets no tracking light
	idVec3				burstColor;
	float				burstRadius;		// 0 when the class gets no detonation flash
	int					burstTime;			// detonation flash lifetime in milliseconds
} classicLightProfile_t;

// True when classic lights are enabled and this instance of the game is
// actually drawing a view for someone.
bool					G_ClassicLightsEnabled		( void );

// Fills in the classic profile for a projectile def.  Returns false when the
// class is deliberately unlit (bullets, nails, debris).
bool					G_ClassicLightProfile		( const idDict &spawnArgs, classicLightProfile_t &profile );

// Installs the tracking light for a bright projectile into an otherwise unused
// renderLight_t.  Its shadows follow g_dynamicLightShadows; the projectile
// keeps its own model out of them.  Returns false when the projectile gets no classic light,
// either because the class is unlit or because the shipped effect for it
// already carries an authored light segment.
bool					G_ClassicProjectileLight	( const idDict &spawnArgs, renderLight_t &light );

// One-shot world flashes.  G_ClassicExplosionFlash's defaultRadius is used for
// call sites that know something exploded even though the def carries no
// recognised projectile class or splash damage -- barrels and damagables.
//
// Flashes follow g_dynamicLightShadows (G_DynamicLightShadowsEnabled).  A muzzle
// flash passes the firing weapon's muzzle-flash light id, in which the firer's
// body and gun already suppress their shadows; an explosion flash instead asks
// the entities it starts inside, such as a barrel's burnt shell, to stay out
// of its shadows.
void					G_ClassicFlash				( const idVec3 &origin, const idVec3 &color, float radius, int durationMS );
void					G_ClassicMuzzleFlash		( const idDict &weaponDef, const idVec3 &origin, int shadowLightId = 0, bool castShadows = false );

// Explosion flash for an effect nothing else lights: an explosion, detonation
// or death effect that burns (it draws fire materials) and carries no light
// segment of its own, such as a dying turret or a scripted level explosion.
// A flash raised at the same spot in the same frame, by a detonating
// projectile or an exploding barrel, already covers it.
void					G_ClassicEffectFlash		( const idDecl *effect, const idVec3 &origin, bool loop );
void					G_ClassicExplosionFlash		( const idDict &spawnArgs, const idVec3 &origin, float defaultRadius = 0.0f );
void					G_ClassicWorldExplosionFlash	( const idDict &spawnArgs, const idVec3 &origin );

// Fades and retires the one-shot flashes.  Called once per rendered frame,
// before the view is built.
void					G_UpdateClassicLights		( void );

// Releases every live flash handle.  Called whenever the render world goes
// away underneath us.
void					G_FreeClassicLights			( void );

#endif	/* !__GAME_CLASSICLIGHTS_H__ */
