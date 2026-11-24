//----------------------------------------------------------------
// BotCombat.h
//
// Small, stateless combat queries shared by multiplayer bot behaviours.
// Keeping these outside rvBot makes perception and shot-safety rules usable by
// future tactical states without giving those states access to rvBot internals.
//----------------------------------------------------------------

#ifndef __GAME_MP_BOTCOMBAT_H__
#define __GAME_MP_BOTCOMBAT_H__

class idPlayer;
class idProjectile;
class idVec3;

//----------------------------------------------------------------
// Tests centre chest, head and pelvis points, then inset shoulder/hip points
// that can see a player leaning around narrow cover.  Centre chest remains the
// first choice so this visibility query does not become an unconditional
// head-shot policy.  FOV and target-selection policy belong to the caller;
// this function only answers line of sight in the observer's clip world and
// refuses inactive players or players in another multiplayer instance.
//----------------------------------------------------------------
bool BotCombatFindVisibleAimPoint( idPlayer *observer, idPlayer *target,
								   idVec3 &visiblePoint );

//----------------------------------------------------------------
// Final safety check for a shot already selected by the caller.  It rejects an
// intervening teammate and any actual or planned impact within
// splashSafetyRadius of the shooter or an exposed, live teammate.  Projectile
// shots use the current, MP-resolved projectile speed, gravity and collision
// bounds, so a ballistic arc clipping cover or a teammate crossing the flight
// path is caught rather than approximated by an eye-to-target point ray.  The
// splash test projects player motion to impact time and still honours cover.
// An intended foe is a valid trace hit, but is deliberately not exempt from
// the point-blank splash check.  A clear hitscan trace is safe.
//
// aimPoint is the point the round will actually travel toward, which for a
// player weapon means along the view axis - not the point the bot would like to
// be aiming at.  The two are allowed to differ by the whole fire cone, and a
// safety proof run down the wrong one of them proves nothing.
//
// requireUsefulImpact asks the same trajectory query to prove the shot is worth
// taking: an unobstructed shot and one that stops on another hostile are useful
// by construction, and anything that terminates on the world has to put the
// intended foe inside the resulting splash.  Leave it false for deliberate
// doorway suppression, where a safe world impact is useful by definition.  Pass
// a zero splash radius for weapons that have none - it correctly makes a world
// impact useless without gating the clear shot, which is the common case.
// impactPoint may be NULL; when supplied it receives the trace end position.
//----------------------------------------------------------------
bool BotCombatLineOfFireIsSafe( idPlayer *shooter, idPlayer *intendedFoe,
								const idVec3 &shotOrigin, const idVec3 &aimPoint,
								float splashSafetyRadius, bool requireUsefulImpact = false,
								idVec3 *impactPoint = NULL );

//----------------------------------------------------------------
// Finds the most immediate hostile projectile whose relative trajectory
// approaches within threatClearance of the player's moving bounds during the
// next lookAheadSeconds.  Projectile gravity and collision bounds are sampled
// along the path.  A wall that will intercept ordnance suppresses a false dodge
// unless the resulting splash can still reach the player; armed, nearly-resting
// explosives are also threats.  Guided steering is naturally reconsidered on
// the caller's next short-interval scan using its new live velocity.
// A valid same-instance owner can prove ordnance friendly; damage-capable
// projectiles with a missing or stale owner remain conservative threats.
//
// On success closestPoint is the projectile's predicted position and
// timeToClosest is seconds from now.  Outputs are reset on failure.
//----------------------------------------------------------------
bool BotCombatFindIncomingProjectileThreat( idPlayer *self,
									 float lookAheadSeconds, float threatClearance,
									 idProjectile *&threatProjectile,
									 float &timeToClosest, idVec3 &closestPoint );

#endif /* !__GAME_MP_BOTCOMBAT_H__ */
