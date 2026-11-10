// Shared SP/MP gameplay presentation. Quake 4 SDK EULA; see src/game/LICENSE.
//
// openQ4 animated crosshair: the weapon's own crosshair design drawn live in
// place of the cursor GUI's still picture.  Each design comes apart into the
// pieces Raven drew it from - arcs, ticks, bars, darts, brackets - and those
// pieces answer the weapon: they assemble when it is raised, kick when it
// fires, close as it charges, wait while it cycles and stand down while it
// reloads.  Hit feedback docks to the design instead of being laid over it.
//
// Retained interface only (ui_retained 1): the pieces are windows onto the
// vector reconstructions of the stock crosshairs, which only that renderer
// has.  The stock interface keeps the cursor GUI's picture.

#ifndef __GAME_ANIMATEDCROSSHAIR_H__
#define __GAME_ANIMATEDCROSSHAIR_H__

class rvWeapon;
class rvVehicleWeapon;
class idUserInterface;

/*
===============================================================================

	What the weapon in hand, or the gun of a vehicle, reports on a drawn
	frame.  Presentation only: none of it is saved or sent, and a weapon that
	reports nothing still gets the transitions every design shares.

===============================================================================
*/
struct crosshairSignals_t {
	const char *	design;				// the crosshair material the weapon names
	int				weaponKey;			// identity of the weapon: a change replays the assembly
	bool			raised;				// in hand and able to act, as opposed to switching or hidden
	bool			firing;				// trigger held and the weapon answering it
	bool			reloading;
	bool			empty;				// nothing left to fire
	bool			zoomed;
	int				nextAttackTime;		// game time the next shot is allowed; a step forward is a shot
	int				clip;				// rounds loaded, or -1 without a clip
	int				clipSize;
	float			reloadProgress;		// 0..1 where the weapon knows it, or -1
	float			charge;				// 0..1 held charge, or -1 for a weapon that does not charge
	float			spin;				// 0..1 barrel, drum or blade speed
	float			lock;				// 0 nothing, below 1 acquiring, 1 locked

					crosshairSignals_t( void ) { Clear(); }
	void			Clear( void ) {
		design = "";
		weaponKey = 0;
		raised = false;
		firing = false;
		reloading = false;
		empty = false;
		zoomed = false;
		nextAttackTime = 0;
		clip = -1;
		clipSize = 0;
		reloadProgress = -1.0f;
		charge = -1.0f;
		spin = 0.0f;
		lock = 0.0f;
	}
};

class rvAnimatedCrosshair {
public:
	// hud_crosshairAnimated, under the retained interface
	static bool				Enabled( void );

	// map load, respawn: nothing carries over
	static void				Clear( void );

	// The color the cursor GUI's combat picture takes.  The animated crosshair
	// draws that picture itself, so the GUI's copy goes transparent while its
	// windows keep deciding when a combat crosshair is the one showing.  A
	// design it does not animate keeps g_crosshairColor.
	static const char *		CursorColor( const char *design );

	// true once after the option, or the interface it needs, changes
	static bool				ConsumeToggle( void );

	// 2D pass, straight after the cursor GUI's redraw.  One of the two weapons
	// is NULL: on foot the weapon in hand, in a vehicle the gun of the seat.
	static void				Draw( idUserInterface *cursor, const rvWeapon *weapon, const rvVehicleWeapon *vehicleWeapon );

	// forces a state for captures and tuning: testCrosshair
	static void				Test( const idCmdArgs &args );
};

#endif	/* !__GAME_ANIMATEDCROSSHAIR_H__ */
