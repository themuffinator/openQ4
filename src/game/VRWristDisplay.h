// Shared SP/MP VR presentation. Quake 4 SDK EULA; see src/game/LICENSE.
#ifndef __GAME_VRWRISTDISPLAY_H__
#define __GAME_VRWRISTDISPLAY_H__

/*
===============================================================================

	Wrist displays (VR-09, vr_hudMode 1, the default in a headset).

	With the status on the wrists no HUD hangs in front of the eyes. Each
	eye draws two small displays over its 3D pass, each standing a few
	centimetres off a wrist and turned to the head, so either reads at a
	glance however the wrist is held:

	- the weapon hand's: the weapon's own icon, the clip or magazine count
	  in large figures with the reserve under it (one count for a weapon
	  without a clip, the infinity sign for one that needs no ammunition),
	  a fill, RELOAD or NO AMMO, and the weapon's name for a moment after a
	  change;
	- the off hand's: health and armor, each an icon, a figure and a fill,
	  red and pulsing while health is low, then a row for each of air under
	  water, a boss's health, powerups or a carried flag, and the newest
	  pickups and objective notices.

	Only what a player must see without looking down stays in front of the
	eyes: captions, damage-direction arcs (drawn here, round the line of
	sight), the weapon wheel while it is open, a vehicle's or turret's own
	HUD, and in multiplayer the score, timer and chat overlay. The stock
	HUD still receives its state; it is only not drawn.

	vr_hudMode 0 hangs the whole stock HUD in front of the eyes instead.

===============================================================================
*/

class idMaterial;
struct renderView_s;
typedef struct vrFrameState_s vrFrameState_t;

const int VR_WRIST_MAX_POWERUPS = 4;

// What the local player shows on its wrists this frame (idPlayer::GetVRWristStatus).
struct vrWristStatus_t {
	bool				valid;			// alive, playing, on foot: the displays show
	bool				strogg;			// the Strogg HUD's colours (after the Stroggification)

	int					health;
	int					maxHealth;
	int					armor;
	int					maxArmor;
	int					air;			// percent of a full breath; 100 above water
	float				boss;			// a boss's health, 0..1, or -1 without a boss

	bool				hasWeapon;
	int					weaponIndex;
	const idMaterial *	weaponIcon;
	idStr				weaponName;		// localized
	bool				infinite;		// the blaster and gauntlet
	bool				usesClip;		// a magazine, reloaded; multiplayer shows one count
	int					clip;			// in the magazine
	int					clipSize;
	int					reserve;		// carried besides the magazine
	int					total;			// all of it
	int					maxAmmo;

	int					powerups;
	const idMaterial *	powerupIcon[VR_WRIST_MAX_POWERUPS];
	int					powerupSeconds[VR_WRIST_MAX_POWERUPS];	// -1 holds until death
	bool				flag;			// carrying a flag (CTF)

	int					hitTime;		// game time of the last hit, 0 for none
	idVec3				hitDirection;	// world direction the last hit travelled
};

// True while a VR frame's HUD pass puts the status on the wrists: the stock
// HUD and the objective notices stand down (idPlayer::DrawHUD, SingleView).
bool	VRWrist_HudOnWrists( void );
// The HUD pass of a VR frame, for the wrists or not.
void	VRWrist_SetGuiPass( bool wrists );
// The setting: vr_hudMode 1.
bool	VRWrist_Selected( void );

// The newest pickup and objective notices, for the off hand's display.
void	VRWrist_NotePickup( const char *text, const char *icon );
void	VRWrist_NoteObjective( const char *title, bool complete );

// Once per VR frame, before the eyes: what the displays show and where
// they stand. eyeOrigin and trackingYaw place the tracking space as
// VR_BuildEyeView does.
void	VRWrist_Prepare( const vrWristStatus_t &status, const vrFrameState_t &frame, const idVec3 &eyeOrigin, float trackingYaw,
			bool show, bool damageDirection );
// Per eye, between its 3D pass and its fade: lens is the eye as the lens
// shows it, unmagnified by a weapon's zoom.
void	VRWrist_DrawEye( const renderView_s &lens );

#endif /* !__GAME_VRWRISTDISPLAY_H__ */
