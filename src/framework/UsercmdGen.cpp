/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include "NativeInputDispatch.h"
#include "PresentationLook.h"
#include "Session_local.h"
#include "VRSystemLocal.h"
#include "../idlib/NumericString.h"
#include "../ui/RetainedUI.h"
#ifdef __ANDROID__
#include "../sys/android/android_public.h"
#include "TouchInput.h"
#endif

static const float MOUSE_CPI_INCHES_PER_CM = 2.5399999618530273f;
static const float MOUSE_CPI_VIEW_SCALE = 45.45454545454546f;
static const int MOUSE_FILTER_SAMPLES = 32;
static const float JOYSTICK_AXIS_LOOK_SCALE = 1.0f / 127.0f;
static idCVar ui_weaponWheelOffset( "ui_weaponWheelOffset", "27", CVAR_GUI | CVAR_ARCHIVE | CVAR_INTEGER,
	"weapon wheel horizontal offset in percent of viewport width; 0 centers, 27 preserves the original position", -40, 40 );
static idCVar in_weaponWheelSensitivity( "in_weaponWheelSensitivity", "4.0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "Classic controller weapon wheel cursor sensitivity (pixels per look degree)", 0.25f, 16.0f );
// Local presentation bridge: radial selectors need stick deflection, not
// integrated view angles. These never enter a usercmd or the network protocol.
static idCVar ui_radialStickActive( "ui_radialStickActive", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_ROM | CVAR_NOCHEAT, "the radial selector's current input is a controller" );
static idCVar ui_radialStickX( "ui_radialStickX", "0", CVAR_SYSTEM | CVAR_FLOAT | CVAR_ROM | CVAR_NOCHEAT, "local radial selector horizontal stick deflection" );
static idCVar ui_radialStickY( "ui_radialStickY", "0", CVAR_SYSTEM | CVAR_FLOAT | CVAR_ROM | CVAR_NOCHEAT, "local radial selector vertical stick deflection, independent of look inversion" );
static idCVar ui_radialOwner( "ui_radialOwner", "0", CVAR_SYSTEM | CVAR_INTEGER | CVAR_ROM | CVAR_NOCHEAT, "local open radial selector: 0 none, 1 weapons, 2 quick chat" );
static idCVar ui_radialCancel( "ui_radialCancel", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_ROM | CVAR_NOCHEAT, "local radial Back request, consumed by its owner" );
// openQ4: the game sets this while the held Remastered scoreboard lists more
// rows than it shows (ui-visual-design.md 14.14). The weapon-cycle controls
// and the right stick then scroll it through the game's scoreboardScroll
// command, on this client alone, and never reach the server as weapon changes.
static idCVar ui_scoreboardScrolls( "ui_scoreboardScrolls", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_ROM | CVAR_NOCHEAT,
	"set by the game while the held scoreboard scrolls: the weapon-cycle controls and the right stick scroll it" );

/*
================
usercmd_t::ByteSwap
================
*/
void usercmd_t::ByteSwap( void ) {
	angles[0] = LittleShort( angles[0] );
	angles[1] = LittleShort( angles[1] );
	angles[2] = LittleShort( angles[2] );
	sequence = LittleLong( sequence );
}

/*
================
usercmd_t::operator==
================
*/
bool usercmd_t::operator==( const usercmd_t &rhs ) const { 
	return ( buttons == rhs.buttons &&
			forwardmove == rhs.forwardmove &&
			rightmove == rhs.rightmove &&
			upmove == rhs.upmove &&
			angles[0] == rhs.angles[0] &&
			angles[1] == rhs.angles[1] &&
			angles[2] == rhs.angles[2] &&
			impulse == rhs.impulse &&
			flags == rhs.flags &&
			mx == rhs.mx &&
			my == rhs.my );
}


const int KEY_MOVESPEED	= 127;

typedef enum {
	UB_NONE,

	UB_UP,
	UB_DOWN,
	UB_LEFT,
	UB_RIGHT,
	UB_FORWARD,
	UB_BACK,
	UB_LOOKUP,
	UB_LOOKDOWN,
	UB_STRAFE,
	UB_MOVELEFT,
	UB_MOVERIGHT,

	UB_BUTTON0,
	UB_BUTTON1,
	UB_BUTTON2,
	UB_BUTTON3,
	UB_BUTTON4,
	UB_BUTTON5,
	UB_BUTTON6,
	UB_BUTTON7,

	UB_ATTACK,
	UB_SPEED,
	UB_ZOOM,
	UB_SHOWSCORES,
	UB_MLOOK,
	UB_WEAPONWHEEL,
	UB_QUICKCHAT,

	UB_IMPULSE0,
	UB_IMPULSE1,
	UB_IMPULSE2,
	UB_IMPULSE3,
	UB_IMPULSE4,
	UB_IMPULSE5,
	UB_IMPULSE6,
	UB_IMPULSE7,
	UB_IMPULSE8,
	UB_IMPULSE9,
	UB_IMPULSE10,
	UB_IMPULSE11,
	UB_IMPULSE12,
	UB_IMPULSE13,
	UB_IMPULSE14,
	UB_IMPULSE15,
	UB_IMPULSE16,
	UB_IMPULSE17,
	UB_IMPULSE18,
	UB_IMPULSE19,
	UB_IMPULSE20,
	UB_IMPULSE21,
	UB_IMPULSE22,
	UB_IMPULSE23,
	UB_IMPULSE24,
	UB_IMPULSE25,
	UB_IMPULSE26,
	UB_IMPULSE27,
	UB_IMPULSE28,
	UB_IMPULSE29,
	UB_IMPULSE30,
	UB_IMPULSE31,
	UB_IMPULSE32,
	UB_IMPULSE33,
	UB_IMPULSE34,
	UB_IMPULSE35,
	UB_IMPULSE36,
	UB_IMPULSE37,
	UB_IMPULSE38,
	UB_IMPULSE39,
	UB_IMPULSE40,
	UB_IMPULSE41,
	UB_IMPULSE42,
	UB_IMPULSE43,
	UB_IMPULSE44,
	UB_IMPULSE45,
	UB_IMPULSE46,
	UB_IMPULSE47,
	UB_IMPULSE48,
	UB_IMPULSE49,
	UB_IMPULSE50,
	UB_IMPULSE51,
	UB_IMPULSE52,
	UB_IMPULSE53,
	UB_IMPULSE54,
	UB_IMPULSE55,
	UB_IMPULSE56,
	UB_IMPULSE57,
	UB_IMPULSE58,
	UB_IMPULSE59,
	UB_IMPULSE60,
	UB_IMPULSE61,
	UB_IMPULSE62,
	UB_IMPULSE63,
	UB_IMPULSE127 = UB_IMPULSE0 + IMPULSE_127,

	UB_MAX_BUTTONS = UB_IMPULSE127 + 1
} usercmdButton_t;

typedef struct {
	const char *string;
	usercmdButton_t	button;
} userCmdString_t;

userCmdString_t	userCmdStrings[] = {
	{ "_moveUp",		UB_UP },
	{ "_moveDown",		UB_DOWN },
	{ "_left",			UB_LEFT },
	{ "_right",			UB_RIGHT },
	{ "_forward",		UB_FORWARD },
	{ "_back",			UB_BACK },
	{ "_lookUp",		UB_LOOKUP },
	{ "_lookDown",		UB_LOOKDOWN },
	{ "_strafe",		UB_STRAFE },
	{ "_moveLeft",		UB_MOVELEFT },
	{ "_moveRight",		UB_MOVERIGHT },

	{ "_attack",		UB_ATTACK },
	{ "_speed",			UB_SPEED },
	{ "_zoom",			UB_ZOOM },
	{ "_showScores",	UB_SHOWSCORES },
	{ "_mlook",			UB_MLOOK },
	{ "_weaponWheel",	UB_WEAPONWHEEL },
	{ "_quickChat",		UB_QUICKCHAT },
	{ "_altattack",		UB_ZOOM },		// the Awakening's name for zoom / alternate fire
	{ "_ingameStats",	UB_BUTTON5 },
	{ "_voiceChat",		UB_BUTTON6 },
	{ "_tourney",		UB_BUTTON7 },

	{ "_button0",		UB_BUTTON0 },
	{ "_button1",		UB_BUTTON1 },
	{ "_button2",		UB_BUTTON2 },
	{ "_button3",		UB_BUTTON3 },
	{ "_button4",		UB_BUTTON4 },
	{ "_button5",		UB_BUTTON5 },
	{ "_button6",		UB_BUTTON6 },
	{ "_button7",		UB_BUTTON7 },

	{ "_impulse0",		UB_IMPULSE0 },
	{ "_impulse1",		UB_IMPULSE1 },
	{ "_impulse2",		UB_IMPULSE2 },
	{ "_impulse3",		UB_IMPULSE3 },
	{ "_impulse4",		UB_IMPULSE4 },
	{ "_impulse5",		UB_IMPULSE5 },
	{ "_impulse6",		UB_IMPULSE6 },
	{ "_impulse7",		UB_IMPULSE7 },
	{ "_impulse8",		UB_IMPULSE8 },
	{ "_impulse9",		UB_IMPULSE9 },
	{ "_impulse10",		UB_IMPULSE10 },
	{ "_impulse11",		UB_IMPULSE11 },
	{ "_impulse12",		UB_IMPULSE12 },
	{ "_impulse13",		UB_IMPULSE13 },
	{ "_impulse14",		UB_IMPULSE14 },
	{ "_impulse15",		UB_IMPULSE15 },
	{ "_impulse16",		UB_IMPULSE16 },
	{ "_impulse17",		UB_IMPULSE17 },
	{ "_impulse18",		UB_IMPULSE18 },
	{ "_impulse19",		UB_IMPULSE19 },
	{ "_impulse20",		UB_IMPULSE20 },
	{ "_impulse21",		UB_IMPULSE21 },
	{ "_impulse22",		UB_IMPULSE22 },
	{ "_impulse23",		UB_IMPULSE23 },
	{ "_impulse24",		UB_IMPULSE24 },
	{ "_impulse25",		UB_IMPULSE25 },
	{ "_impulse26",		UB_IMPULSE26 },
	{ "_impulse27",		UB_IMPULSE27 },
	{ "_impulse28",		UB_IMPULSE28 },
	{ "_impulse29",		UB_IMPULSE29 },
	{ "_impulse30",		UB_IMPULSE30 },
	{ "_impulse31",		UB_IMPULSE31 },
	{ "_impulse32",		UB_IMPULSE32 },
	{ "_impulse33",		UB_IMPULSE33 },
	{ "_impulse34",		UB_IMPULSE34 },
	{ "_impulse35",		UB_IMPULSE35 },
	{ "_impulse36",		UB_IMPULSE36 },
	{ "_impulse37",		UB_IMPULSE37 },
	{ "_impulse38",		UB_IMPULSE38 },
	{ "_impulse39",		UB_IMPULSE39 },
	{ "_impulse40",		UB_IMPULSE40 },
	{ "_impulse41",		UB_IMPULSE41 },
	{ "_impulse42",		UB_IMPULSE42 },
	{ "_impulse43",		UB_IMPULSE43 },
	{ "_impulse44",		UB_IMPULSE44 },
	{ "_impulse45",		UB_IMPULSE45 },
	{ "_impulse46",		UB_IMPULSE46 },
	{ "_impulse47",		UB_IMPULSE47 },
	{ "_impulse48",		UB_IMPULSE48 },
	{ "_impulse49",		UB_IMPULSE49 },
	{ "_impulse50",		UB_IMPULSE50 },
	{ "_impulse51",		UB_IMPULSE51 },
	{ "_impulse52",		UB_IMPULSE52 },
	{ "_impulse53",		UB_IMPULSE53 },
	{ "_impulse54",		UB_IMPULSE54 },
	{ "_impulse55",		UB_IMPULSE55 },
	{ "_impulse56",		UB_IMPULSE56 },
	{ "_impulse57",		UB_IMPULSE57 },
	{ "_impulse58",		UB_IMPULSE58 },
	{ "_impulse59",		UB_IMPULSE59 },
	{ "_impulse60",		UB_IMPULSE60 },
	{ "_impulse61",		UB_IMPULSE61 },
	{ "_impulse62",		UB_IMPULSE62 },
	{ "_impulse63",		UB_IMPULSE63 },

	{ NULL,				UB_NONE },
};

static bool ParseImpulseCommand( const char *cmdString, const char *prefix, int &impulseNum ) {
	if ( idStr::Icmpn( cmdString, prefix, static_cast<int>( strlen( prefix ) ) ) != 0 ) {
		return false;
	}

	const char *impulseSuffix = cmdString + strlen( prefix );
	return idNumericString::ParseUnsignedBounded( impulseSuffix, IMPULSE_127, impulseNum );
}

static int ResolveImpulseAction( const char *cmdString ) {
	int impulseNum = 0;
	if ( ParseImpulseCommand( cmdString, "_impulse", impulseNum ) ||
		 ParseImpulseCommand( cmdString, "impulse_", impulseNum ) ||
		 ParseImpulseCommand( cmdString, "impulse", impulseNum ) ) {
		return UB_IMPULSE0 + impulseNum;
	}

	return UB_NONE;
}

 class buttonState_t {
 public:
	int		on;
	bool	held;

			buttonState_t() { Clear(); };
	void	Clear( void );
	void	SetKeyState( int keystate, bool toggle );
};

/*
================
buttonState_t::Clear
================
*/
void buttonState_t::Clear( void ) {
	held = false;
	on = 0;
}

/*
================
buttonState_t::SetKeyState
================
*/
void buttonState_t::SetKeyState( int keystate, bool toggle ) {
	if ( !toggle ) {
		held = false;
		on = keystate;
	} else if ( !keystate ) {
		held = false;
	} else if ( !held ) {
		held = true;
		on ^= 1;
	}
}


const int NUM_USER_COMMANDS = sizeof(userCmdStrings) / sizeof(userCmdString_t);

const int MAX_CHAT_BUFFER = 127;

class idUsercmdGenLocal : public idUsercmdGen {
public:
					idUsercmdGenLocal( void );
	
	void			Init( void );

	void			InitForNewMap( void );

	void			Shutdown( void );

	void			Clear( void );

	void			ClearAngles( void );

	usercmd_t		TicCmd( int ticNumber );

	void			InhibitUsercmd( inhibit_t subsystem, bool inhibit );
	void			RetainedInputChanged();
    void NativeInputChanged() noexcept;
    void NativeInputSource(std::uint64_t,std::uint64_t,std::uint64_t,unsigned,int,bool) noexcept;
    bool NativeKeyBlocked(int) const noexcept;

	void			UsercmdInterrupt( void );

	int				CommandStringUsercmdData( const char *cmdString );
	int				ResolveImpulseCommand( const char *cmdString ) const;

	int				GetNumUserCommands( void );

	const char *	GetUserCommandName( int index );

	void			MouseState( int *x, int *y, int *button, bool *down );

	int				ButtonState( int key );
	int				KeyState( int key );

	usercmd_t		GetDirectUsercmd( void );
	void			TriggerImpulse( int impulseNum );
	void			SetUsercmdButton( int action, bool down );	// not part of idUsercmdGen, see Sys_SetUsercmdButton
#ifdef __ANDROID__
	void			SetTouchMotion( const openq4::touch::Motion &motion ) { touchMotion.Submit( motion ); }
	void			ResetTouchMotion();
	void			SetTouchButtons( unsigned buttons );
#endif
	bool			GetPresentationViewAngles( float &yaw, float &pitch );

private:
	void			MakeCurrent( void );
	void			InitCurrent( void );

	bool			Inhibited( void );
	void			MigrateLegacyRunDefaults( void );
	bool			IsRunButtonActive( void ) const;
	bool			ShouldToggleRun( void ) const;
	void			AdjustAngles( void );
	void			KeyMove( void );
	void			JoystickMove( void );
	void			MouseMove( void );
	float			NormalizeMouseDeltasForCpi( float &mx, float &my, float &strafeMx, float &strafeMy ) const;
	float			CalculateMouseSensitivity( float mx, float my, float *debugRate, float *debugPower ) const;
	float			GetZoomLookSensitivityScale( void ) const;
	int				GetMouseFilterSamples( void );
	void			ResetMouseFilter( void );
	void			BeginMouseFilter( void );
	void			EndMouseFilter( void );
	void			UpdateMouseAccelDebugLog( void );
	void			CloseMouseAccelDebugLog( void );
	void			WriteMouseAccelDebugLog( float mx, float my, float rate, float power );
	void			CmdButtons( void );

	void			Mouse( void );
	void			MouseProbe( void );
	bool			PresentationLookRate( float &yawRate, float &pitchRate );
    void MouseEvent(int action,int value);
	void			Keyboard( void );
	void			Joystick( void );

	void			Key( int keyNum, bool down );
	void			SetButtonAction( int action, bool down );
	bool			ScoreboardScrolls( void );
	void			ScrollScoreboard( int rows );

	idVec3			viewangles;
	int				flags;
	int				impulse;

	buttonState_t	toggled_crouch;
	buttonState_t	toggled_run;
	buttonState_t	toggled_zoom;

	int				buttonState[UB_MAX_BUTTONS];
	bool			keyState[K_LAST_KEY];
	bool			directButtonState[UB_MAX_BUTTONS];	// SetUsercmdButton's own held-state, see Key()'s keyState
#ifdef __ANDROID__
	openq4::touch::MotionState touchMotion;
#endif
    bool nativeUnknownKeyBlocked[K_LAST_KEY] = {};
    struct nativeHeldSource_t {
        std::uint64_t route=0,window=0,device=0;
        unsigned source=0;
        int key=0;
        bool down=false;
    } nativeHeldSources[512];
    bool nativeSourceExhausted=false;
	bool			retainedKeyBlocked[K_LAST_KEY];
	bool			retainedDirectBlocked[UB_MAX_BUTTONS];
	bool			retainedAxisBlocked[MAX_JOYSTICK_AXIS];

	int				inhibitCommands;	// true when in console or menu locally
	int				lastCommandTime;

	bool			initialized;

	usercmd_t		cmd;		// the current cmd being built
	usercmd_t		buffered[MAX_BUFFERED_USERCMD];

	int				continuousMouseX, continuousMouseY;	// for gui event generatioin, never zerod
	int				mouseButton;						// for gui event generatioin
	bool			mouseDown;

	int				mouseDx, mouseDy;	// added to by mouse events
	double			mouseProbeLastMsec;	// in_presentationViewProbe's last delivery, host ms
	double			mouseProbeRemainder;
	double			lastBuildMsec;		// when MakeCurrent last built a usercmd, host ms
	idFile *		mouseAccelDebugLog;
	float			mouseFilterYaw[MOUSE_FILTER_SAMPLES];
	float			mouseFilterPitch[MOUSE_FILTER_SAMPLES];
	int				mouseFilterCount;
	int				mouseFilterIndex;
	float			mouseFilterBaseYaw;
	float			mouseFilterBasePitch;

	int				joystickAxis[MAX_JOYSTICK_AXIS];	// set by joystick events
	int				scoreboardStickDirection;			// the right stick's scroll of the held scoreboard
	int				scoreboardStickRepeat;				// when the held stick scrolls again

	static idCVar	in_yawSpeed;
	static idCVar	in_pitchSpeed;
	static idCVar	in_angleSpeedKey;
	static idCVar	in_freeLook;
	static idCVar	in_alwaysRun;
	static idCVar	in_toggleRun;
	static idCVar	in_toggleCrouch;
	static idCVar	in_toggleZoom;
	static idCVar	sensitivity;
	static idCVar	m_pitch;
	static idCVar	m_yaw;
	static idCVar	m_strafeScale;
	static idCVar	m_smooth;
	static idCVar	m_strafeSmooth;
	static idCVar	m_cpi;
	static idCVar	m_filter;
	static idCVar	m_maxMouseDelta;
	static idCVar	m_showMouseRate;
	static idCVar	cl_mouseAccel;
	static idCVar	cl_mouseAccelDebug;
	static idCVar	cl_mouseAccelOffset;
	static idCVar	cl_mouseAccelPower;
	static idCVar	cl_mouseSensCap;
	static idCVar	in_presentationView;
	static idCVar	in_presentationViewProbe;
	static idCVar	in_presentationViewProbeZoom;
	static idCVar	in_presentationViewProbeAttack;
};

idCVar idUsercmdGenLocal::in_yawSpeed( "in_yawspeed", "140", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "yaw change speed when holding down _left or _right button" );
idCVar idUsercmdGenLocal::in_pitchSpeed( "in_pitchspeed", "140", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "pitch change speed when holding down look _lookUp or _lookDown button" );
idCVar idUsercmdGenLocal::in_angleSpeedKey( "in_anglespeedkey", "1.5", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "angle change scale when holding down _speed button" );
idCVar idUsercmdGenLocal::in_freeLook( "in_freeLook", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "look around with mouse (reverse _mlook button)" );
idCVar idUsercmdGenLocal::in_alwaysRun( "in_alwaysRun", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "always run (reverse _speed button)" );
static idCVar in_runDefaultMigrated( "in_runDefaultMigrated", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "one-time migration flag for legacy openQ4 run defaults" );
idCVar idUsercmdGenLocal::in_toggleRun( "in_toggleRun", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "pressing _speed button toggles run on/off - only in MP" );
idCVar idUsercmdGenLocal::in_toggleCrouch( "in_toggleCrouch", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "pressing _movedown button toggles player crouching/standing" );
idCVar idUsercmdGenLocal::in_toggleZoom( "in_toggleZoom", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "pressing _zoom button toggles zoom on/off" );
idCVar idUsercmdGenLocal::sensitivity( "sensitivity", "5", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "mouse view sensitivity" );
idCVar idUsercmdGenLocal::m_pitch( "m_pitch", "0.022", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "mouse pitch scale" );
idCVar idUsercmdGenLocal::m_yaw( "m_yaw", "0.022", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "mouse yaw scale" );
idCVar idUsercmdGenLocal::m_strafeScale( "m_strafeScale", "6.25", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "mouse strafe movement scale" );
idCVar idUsercmdGenLocal::m_smooth( "m_smooth", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "number of samples blended for mouse viewing", 1, 8, idCmdSystem::ArgCompletion_Integer<1,8> );
idCVar idUsercmdGenLocal::m_strafeSmooth( "m_strafeSmooth", "4", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "number of samples blended for mouse moving", 1, 8, idCmdSystem::ArgCompletion_Integer<1,8> );
idCVar idUsercmdGenLocal::m_cpi( "m_cpi", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "mouse counts per inch for physical sensitivity scaling; 0 uses raw legacy sensitivity", 0, 100000 );
idCVar idUsercmdGenLocal::m_filter( "m_filter", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "number of view-angle samples to blend after mouse movement; 0 disables filtering", 0, MOUSE_FILTER_SAMPLES - 1, idCmdSystem::ArgCompletion_Integer<0,MOUSE_FILTER_SAMPLES - 1> );
idCVar idUsercmdGenLocal::m_maxMouseDelta( "m_maxMouseDelta", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER, "legacy maximum mouse delta after smoothing; 0 disables clamping for high-DPI mice", 0, 65535 );
idCVar idUsercmdGenLocal::m_showMouseRate( "m_showMouseRate", "0", CVAR_SYSTEM | CVAR_BOOL, "shows mouse movement" );
idCVar idUsercmdGenLocal::cl_mouseAccel( "cl_mouseAccel", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "QuakeLive-style mouse acceleration; negative values reduce sensitivity as movement rate increases" );
idCVar idUsercmdGenLocal::cl_mouseAccelDebug( "cl_mouseAccelDebug", "0", CVAR_SYSTEM | CVAR_BOOL, "writes mouse acceleration samples to logs/mouse.log" );
idCVar idUsercmdGenLocal::cl_mouseAccelOffset( "cl_mouseAccelOffset", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "movement rate subtracted before mouse acceleration is applied" );
idCVar idUsercmdGenLocal::cl_mouseAccelPower( "cl_mouseAccelPower", "2", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "mouse acceleration exponent; 2 matches QuakeLive", 1, 8 );
idCVar idUsercmdGenLocal::cl_mouseSensCap( "cl_mouseSensCap", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "maximum accelerated mouse sensitivity; 0 disables the cap", 0, 100000 );
idCVar idUsercmdGenLocal::in_presentationView( "in_presentationView", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "late look: turn the drawn view with mouse, gyro, look-key and stick input on every presented frame instead of up to a 60 Hz tick later; aiming, firing and the network still use the authoritative usercmd" );
idCVar idUsercmdGenLocal::in_presentationViewProbe( "in_presentationViewProbe", "0", CVAR_SYSTEM | CVAR_CHEAT | CVAR_FLOAT, "development probe: synthetic horizontal mouse counts per second, delivered through the ordinary mouse path for no-input latency measurements; requires cheats" );
idCVar idUsercmdGenLocal::in_presentationViewProbeZoom( "in_presentationViewProbeZoom", "0", CVAR_SYSTEM | CVAR_CHEAT | CVAR_BOOL, "development probe: hold the zoom button, for no-input zoomed late-look measurements; requires cheats" );
idCVar idUsercmdGenLocal::in_presentationViewProbeAttack( "in_presentationViewProbeAttack", "0", CVAR_SYSTEM | CVAR_CHEAT | CVAR_BOOL, "development probe: hold the attack button, so muzzle flashes, view lights and tracers can be reviewed under late look without input; requires cheats" );

static idUsercmdGenLocal localUsercmdGen;
idUsercmdGen	*usercmdGen = &localUsercmdGen;

/*
================
idUsercmdGenLocal::idUsercmdGenLocal
================
*/
idUsercmdGenLocal::idUsercmdGenLocal( void ) {
	lastCommandTime = 0;
	initialized = false;

	flags = 0;
	impulse = 0;

	toggled_crouch.Clear();
	toggled_run.Clear();
	toggled_zoom.Clear();
	toggled_run.on = in_alwaysRun.GetBool();

	mouseAccelDebugLog = NULL;
	mouseProbeLastMsec = -1.0;
	mouseProbeRemainder = 0.0;
	lastBuildMsec = -1.0;
	memset( mouseFilterYaw, 0, sizeof( mouseFilterYaw ) );
	memset( mouseFilterPitch, 0, sizeof( mouseFilterPitch ) );
	mouseFilterCount = 0;
	mouseFilterIndex = 0;
	mouseFilterBaseYaw = 0.0f;
	mouseFilterBasePitch = 0.0f;

	ClearAngles();
	Clear();
}

/*
================
idUsercmdGenLocal::InhibitUsercmd
================
*/
void idUsercmdGenLocal::InhibitUsercmd( inhibit_t subsystem, bool inhibit ) {
	if ( inhibit ) {
		inhibitCommands |= 1 << subsystem;
	} else {
		inhibitCommands &= ( 0xffffffff ^ ( 1 << subsystem ) );
	}
}

/*
===============
idUsercmdGenLocal::ButtonState

Returns (the fraction of the frame) that the key was down
===============
*/
int	idUsercmdGenLocal::ButtonState( int key ) {
	if ( key<0 || key>=UB_MAX_BUTTONS ) {
		return 0;
	}
	return ( buttonState[key] > 0 ) ? 1 : 0;
}

/*
===============
idUsercmdGenLocal::KeyState

Returns (the fraction of the frame) that the key was down
bk20060111
===============
*/
int	idUsercmdGenLocal::KeyState( int key ) {
	if ( key<0 || key>=K_LAST_KEY ) {
		return 0;
	}
	return ( keyState[key] ) ? 1 : 0;
}


//=====================================================================


/*
================
idUsercmdGenLocal::GetNumUserCommands
================
*/
int idUsercmdGenLocal::GetNumUserCommands( void ) {
	return NUM_USER_COMMANDS;
}

/*
================
idUsercmdGenLocal::GetNumUserCommands
================
*/
const char *idUsercmdGenLocal::GetUserCommandName( int index ) {
	if (index >= 0 && index < NUM_USER_COMMANDS) {
		return userCmdStrings[index].string;
	}
	return "";
}

/*
================
idUsercmdGenLocal::Inhibited

is user cmd generation inhibited
================
*/
bool idUsercmdGenLocal::Inhibited( void ) {
	return inhibitCommands != 0 || RetainedUI_IsOpen() || NativeInput_Inhibited();
}

/*
================
idUsercmdGenLocal::MigrateLegacyRunDefaults

Older openQ4 builds archived in_alwaysRun as 0, which leaves retail Quake 4's
footstep-authored run animations unused until the player manually corrects the
setting. Migrate those profiles once so existing installs regain retail input
and movement behavior automatically.
================
*/
void idUsercmdGenLocal::MigrateLegacyRunDefaults( void ) {
	if ( in_runDefaultMigrated.GetBool() ) {
		return;
	}

	if ( !in_alwaysRun.GetBool() && !in_toggleRun.GetBool() ) {
		common->Printf( "Migrating legacy input config: restoring retail in_alwaysRun 1\n" );
		in_alwaysRun.SetBool( true );
	}

	in_runDefaultMigrated.SetBool( true );
}

/*
================
idUsercmdGenLocal::IsRunButtonActive

Retail Quake 4 treats _speed as an inversion of in_alwaysRun in both SP and MP.
The player footstep events are authored on the run anims, so honoring that logic
is required to reach the retail movement/sound path.
================
*/
bool idUsercmdGenLocal::IsRunButtonActive( void ) const {
	return ( toggled_run.on != 0 ) != in_alwaysRun.GetBool();
}

/*
================
idUsercmdGenLocal::ShouldToggleRun
================
*/
bool idUsercmdGenLocal::ShouldToggleRun( void ) const {
	return in_toggleRun.GetBool() && idAsyncNetwork::IsActive();
}

/*
================
idUsercmdGenLocal::AdjustAngles

Moves the local angle positions
================
*/
void idUsercmdGenLocal::AdjustAngles( void ) {
	float	speed;
	const float usercmdSeconds = common->GetUserCmdSec();
	
	if ( IsRunButtonActive() ) {
		speed = usercmdSeconds * in_angleSpeedKey.GetFloat();
	} else {
		speed = usercmdSeconds;
	}

	if ( !ButtonState( UB_STRAFE ) ) {
		viewangles[YAW] -= speed * in_yawSpeed.GetFloat() * ButtonState( UB_RIGHT );
		viewangles[YAW] += speed * in_yawSpeed.GetFloat() * ButtonState( UB_LEFT );
	}

	viewangles[PITCH] -= speed * in_pitchSpeed.GetFloat() * ButtonState( UB_LOOKUP );
	viewangles[PITCH] += speed * in_pitchSpeed.GetFloat() * ButtonState( UB_LOOKDOWN );
}

/*
================
idUsercmdGenLocal::KeyMove

Sets the usercmd_t based on key states
================
*/
void idUsercmdGenLocal::KeyMove( void ) {
	int		forward, side, up;

	forward = 0;
	side = 0;
	up = 0;
	if ( ButtonState( UB_STRAFE ) ) {
		side += KEY_MOVESPEED * ButtonState( UB_RIGHT );
		side -= KEY_MOVESPEED * ButtonState( UB_LEFT );
	}

	side += KEY_MOVESPEED * ButtonState( UB_MOVERIGHT );
	side -= KEY_MOVESPEED * ButtonState( UB_MOVELEFT );

	up -= KEY_MOVESPEED * toggled_crouch.on;
	up += KEY_MOVESPEED * ButtonState( UB_UP );

	forward += KEY_MOVESPEED * ButtonState( UB_FORWARD );
	forward -= KEY_MOVESPEED * ButtonState( UB_BACK );

	cmd.forwardmove = idMath::ClampChar( forward );
	cmd.rightmove = idMath::ClampChar( side );
	cmd.upmove = idMath::ClampChar( up );
}

/*
=================
idUsercmdGenLocal::NormalizeMouseDeltasForCpi
=================
*/
float idUsercmdGenLocal::NormalizeMouseDeltasForCpi( float &mx, float &my, float &strafeMx, float &strafeMy ) const {
	const float cpi = m_cpi.GetFloat();
	if ( cpi <= 0.0f ) {
		return 1.0f;
	}

	const float cpiScale = cpi / MOUSE_CPI_INCHES_PER_CM;
	if ( cpiScale <= 0.0f ) {
		return 1.0f;
	}

	mx /= cpiScale;
	my /= cpiScale;
	strafeMx /= cpiScale;
	strafeMy /= cpiScale;
	return MOUSE_CPI_VIEW_SCALE;
}

/*
=================
idUsercmdGenLocal::CalculateMouseSensitivity
=================
*/
float idUsercmdGenLocal::CalculateMouseSensitivity( float mx, float my, float *debugRate, float *debugPower ) const {
	float rate = 0.0f;
	float power = 0.0f;
	float mouseSensitivity = sensitivity.GetFloat();
	const float accel = cl_mouseAccel.GetFloat();

	if ( accel != 0.0f ) {
		rate = idMath::Sqrt( mx * mx + my * my ) / Max( 1.0f, static_cast<float>( common->GetUserCmdMSec() ) );
		if ( m_cpi.GetFloat() > 0.0f ) {
			rate *= 1000.0f;
		}
		rate -= cl_mouseAccelOffset.GetFloat();

		power = cl_mouseAccelPower.GetFloat() - 1.0f;
		if ( power < 0.0f ) {
			power = 0.0f;
		}

		if ( rate > 0.0f ) {
			const float accelRate = idMath::Fabs( accel ) * rate;
			const float accelSensitivity = idMath::Pow( accelRate, power );
			if ( accel <= 0.0f ) {
				mouseSensitivity -= accelSensitivity;
			} else {
				mouseSensitivity += accelSensitivity;
			}
			rate = accelRate;
		}

		if ( cl_mouseSensCap.GetFloat() > 0.0f && cl_mouseSensCap.GetFloat() < mouseSensitivity ) {
			mouseSensitivity = cl_mouseSensCap.GetFloat();
		}
	}

	if ( debugRate != NULL ) {
		*debugRate = rate;
	}
	if ( debugPower != NULL ) {
		*debugPower = power;
	}

	return mouseSensitivity;
}

/*
=================
idUsercmdGenLocal::GetZoomLookSensitivityScale
=================
*/
float idUsercmdGenLocal::GetZoomLookSensitivityScale( void ) const {
	// openQ4: the game module registers this cvar; find it once it exists
	// rather than by name for every usercmd and every late-look sample
	static idCVar *zoomedCVar = NULL;
	if ( zoomedCVar == NULL ) {
		zoomedCVar = cvarSystem->Find( "pm_isZoomed" );
	}
	const int zoomedSlowPercent = zoomedCVar != NULL ? zoomedCVar->GetInteger() : 0;
	if ( zoomedSlowPercent <= 0 ) {
		return 1.0f;
	}

	return idMath::ClampFloat( 0.01f, 1.0f, static_cast<float>( zoomedSlowPercent ) * 0.01f );
}

/*
=================
idUsercmdGenLocal::GetMouseFilterSamples
=================
*/
int idUsercmdGenLocal::GetMouseFilterSamples( void ) {
	const int samples = idMath::ClampInt( 0, MOUSE_FILTER_SAMPLES - 1, m_filter.GetInteger() );
	if ( samples != m_filter.GetInteger() ) {
		m_filter.SetInteger( samples );
	}
	return samples;
}

/*
=================
idUsercmdGenLocal::ResetMouseFilter
=================
*/
void idUsercmdGenLocal::ResetMouseFilter( void ) {
	memset( mouseFilterYaw, 0, sizeof( mouseFilterYaw ) );
	memset( mouseFilterPitch, 0, sizeof( mouseFilterPitch ) );
	mouseFilterCount = 0;
	mouseFilterIndex = 0;
	mouseFilterBaseYaw = viewangles[YAW];
	mouseFilterBasePitch = viewangles[PITCH];
}

/*
=================
idUsercmdGenLocal::BeginMouseFilter
=================
*/
void idUsercmdGenLocal::BeginMouseFilter( void ) {
	if ( GetMouseFilterSamples() <= 0 ) {
		if ( m_filter.IsModified() ) {
			ResetMouseFilter();
			m_filter.ClearModified();
		}
		return;
	}

	if ( m_filter.IsModified() ) {
		ResetMouseFilter();
		m_filter.ClearModified();
	}

	viewangles[YAW] = mouseFilterBaseYaw;
	viewangles[PITCH] = mouseFilterBasePitch;
}

/*
=================
idUsercmdGenLocal::EndMouseFilter
=================
*/
void idUsercmdGenLocal::EndMouseFilter( void ) {
	const int samples = GetMouseFilterSamples();
	if ( samples <= 0 ) {
		return;
	}

	mouseFilterYaw[mouseFilterIndex] = viewangles[YAW];
	mouseFilterPitch[mouseFilterIndex] = viewangles[PITCH];

	mouseFilterCount++;
	if ( mouseFilterCount > samples ) {
		mouseFilterCount = samples;
	}

	float yaw = 0.0f;
	float pitch = 0.0f;
	int index = mouseFilterIndex;
	for ( int i = 0; i < mouseFilterCount; i++ ) {
		yaw += mouseFilterYaw[index];
		pitch += mouseFilterPitch[index];
		index = ( index - 1 ) & ( MOUSE_FILTER_SAMPLES - 1 );
	}

	mouseFilterIndex = ( mouseFilterIndex + 1 ) & ( MOUSE_FILTER_SAMPLES - 1 );
	mouseFilterBaseYaw = viewangles[YAW];
	mouseFilterBasePitch = viewangles[PITCH];
	viewangles[YAW] = yaw / static_cast<float>( mouseFilterCount );
	viewangles[PITCH] = pitch / static_cast<float>( mouseFilterCount );
}

/*
=================
idUsercmdGenLocal::CloseMouseAccelDebugLog
=================
*/
void idUsercmdGenLocal::CloseMouseAccelDebugLog( void ) {
	if ( mouseAccelDebugLog == NULL ) {
		return;
	}

	if ( fileSystem != NULL ) {
		fileSystem->CloseFile( mouseAccelDebugLog );
	}
	mouseAccelDebugLog = NULL;
}

/*
=================
idUsercmdGenLocal::UpdateMouseAccelDebugLog
=================
*/
void idUsercmdGenLocal::UpdateMouseAccelDebugLog( void ) {
	if ( !cl_mouseAccelDebug.GetBool() ) {
		CloseMouseAccelDebugLog();
		return;
	}

	if ( mouseAccelDebugLog != NULL ) {
		return;
	}

	if ( fileSystem == NULL || !fileSystem->IsInitialized() ) {
		return;
	}

	mouseAccelDebugLog = fileSystem->OpenFileWrite( "logs/mouse.log", "fs_savepath" );
	if ( mouseAccelDebugLog != NULL ) {
		mouseAccelDebugLog->WriteFloatString( "mx my frame_msec rate power\n" );
	}
}

/*
=================
idUsercmdGenLocal::WriteMouseAccelDebugLog
=================
*/
void idUsercmdGenLocal::WriteMouseAccelDebugLog( float mx, float my, float rate, float power ) {
	if ( mouseAccelDebugLog == NULL ) {
		return;
	}

	mouseAccelDebugLog->WriteFloatString( "%g %g %d ", mx, my, common->GetUserCmdMSec() );
	if ( cl_mouseAccel.GetFloat() != 0.0f ) {
		mouseAccelDebugLog->WriteFloatString( "%g %g ", rate, power );
	}
	mouseAccelDebugLog->WriteFloatString( "\n" );
}

/*
=================
idUsercmdGenLocal::MouseMove
=================
*/
void idUsercmdGenLocal::MouseMove( void ) {
	float		mx, my, strafeMx, strafeMy;
	static int	history[8][2];
	static int	historyCounter;
	int			i;
	float		accelRate, accelPower;

	UpdateMouseAccelDebugLog();

	history[historyCounter&7][0] = mouseDx;
	history[historyCounter&7][1] = mouseDy;
	
	// allow mouse movement to be smoothed together
	int smooth = m_smooth.GetInteger();
	if ( smooth < 1 ) {
		smooth = 1;
	}
	if ( smooth > 8 ) {
		smooth = 8;
	}
	mx = 0;
	my = 0;
	for ( i = 0 ; i < smooth ; i++ ) {
		mx += history[ ( historyCounter - i + 8 ) & 7 ][0];
		my += history[ ( historyCounter - i + 8 ) & 7 ][1];
	}
	mx /= smooth;
	my /= smooth;

	// use a larger smoothing for strafing
	smooth = m_strafeSmooth.GetInteger();
	if ( smooth < 1 ) {
		smooth = 1;
	}
	if ( smooth > 8 ) {
		smooth = 8;
	}
	strafeMx = 0;
	strafeMy = 0;
	for ( i = 0 ; i < smooth ; i++ ) {
		strafeMx += history[ ( historyCounter - i + 8 ) & 7 ][0];
		strafeMy += history[ ( historyCounter - i + 8 ) & 7 ][1];
	}
	strafeMx /= smooth;
	strafeMy /= smooth;

	historyCounter++;

	const bool hasHighDelta = idMath::Fabs( mx ) > 1000.0f || idMath::Fabs( my ) > 1000.0f;
	if ( hasHighDelta ) {
		// Modern high-DPI mice can legitimately exceed the old retail cutoff.
		static bool highDeltaWarningShown = false;
		if ( !highDeltaWarningShown ) {
			highDeltaWarningShown = true;
			Sys_DebugPrintf( "idUsercmdGenLocal::MouseMove: Detected high mouse delta (expected with high-DPI mice). Set m_maxMouseDelta to restore legacy clamping for spurious spikes.\n" );
		}
	}

	const int maxMouseDelta = m_maxMouseDelta.GetInteger();
	if ( maxMouseDelta > 0 && ( idMath::Fabs( mx ) > maxMouseDelta || idMath::Fabs( my ) > maxMouseDelta ) ) {
		static bool clampedDeltaWarningShown = false;
		if ( !clampedDeltaWarningShown ) {
			clampedDeltaWarningShown = true;
			Sys_DebugPrintf( "idUsercmdGenLocal::MouseMove: Clamping mouse delta above m_maxMouseDelta.\n" );
		}
		mx = my = 0;
	}

	const float mouseAxisScale = NormalizeMouseDeltasForCpi( mx, my, strafeMx, strafeMy );
	const float mouseSensitivity = CalculateMouseSensitivity( mx, my, &accelRate, &accelPower ) * GetZoomLookSensitivityScale();
	WriteMouseAccelDebugLog( mx, my, accelRate, accelPower );

	mx *= mouseSensitivity;
	my *= mouseSensitivity;

	if ( m_showMouseRate.GetBool() ) {
		Sys_DebugPrintf( "[%3i %3i  = %5.1f %5.1f = %5.1f %5.1f] ", mouseDx, mouseDy, mx, my, strafeMx, strafeMy );
	}

	mouseDx = 0;
	mouseDy = 0;

	if ( !strafeMx && !strafeMy && GetMouseFilterSamples() <= 0 ) {
		return;
	}

	BeginMouseFilter();

	if ( ButtonState( UB_STRAFE ) || !( cmd.buttons & BUTTON_MLOOK ) ) {
		// add mouse X/Y movement to cmd
		strafeMx *= m_strafeScale.GetFloat();
		strafeMy *= m_strafeScale.GetFloat();
		// clamp as a vector, instead of separate floats
		float len = sqrt( strafeMx * strafeMx + strafeMy * strafeMy );
		if ( len > 127 ) {
			strafeMx = strafeMx * 127 / len;
			strafeMy = strafeMy * 127 / len;
		}
	}

	if ( !ButtonState( UB_STRAFE ) ) {
		viewangles[YAW] -= m_yaw.GetFloat() * mouseAxisScale * mx;
	} else {
		cmd.rightmove = idMath::ClampChar( (int)(cmd.rightmove + strafeMx) );
	}

	if ( !ButtonState( UB_STRAFE ) && ( cmd.buttons & BUTTON_MLOOK ) ) {
		viewangles[PITCH] += m_pitch.GetFloat() * mouseAxisScale * my;
	} else {
		cmd.forwardmove = idMath::ClampChar( (int)(cmd.forwardmove - strafeMy) );
	}

	EndMouseFilter();
}

/*
=================
idUsercmdGenLocal::JoystickMove
=================
*/
void idUsercmdGenLocal::JoystickMove( void ) {
	float	anglespeed;
	const float usercmdSeconds = common->GetUserCmdSec();
	// AXIS_ROLL is used as a backend capability flag: non-zero means dedicated look axes are available.
	const bool hasDedicatedLookAxis = joystickAxis[AXIS_ROLL] != 0;
	const float lookAxisX = ( hasDedicatedLookAxis ?
		joystickAxis[AXIS_SIDE] :
		joystickAxis[AXIS_YAW] ) * JOYSTICK_AXIS_LOOK_SCALE;
	float lookAxisY = ( hasDedicatedLookAxis ?
		joystickAxis[AXIS_FORWARD] :
		-joystickAxis[AXIS_PITCH] ) * JOYSTICK_AXIS_LOOK_SCALE;
	if ( !hasDedicatedLookAxis && cvarSystem->GetCVarBool( "in_joystickInvertLook" ) ) {
		// backends only apply invert to dedicated look axes; the shared move/look axis is inverted here
		lookAxisY = -lookAxisY;
	}
	const int moveAxisX = joystickAxis[AXIS_YAW];
	const int moveAxisY = joystickAxis[AXIS_PITCH];
	cvarSystem->SetCVarBool( "ui_radialStickActive", idKeyInput::LastInputWasController() );

	// openQ4: while the held scoreboard scrolls (ui-visual-design.md 14.14),
	// the right stick scrolls it a row on each push past half its travel, then
	// every 120 ms while held, the way the stick points whatever the look
	// inversion, and leaves the view's pitch alone.
	if ( hasDedicatedLookAxis && ScoreboardScrolls() ) {
		const float stickY = cvarSystem->GetCVarBool( "in_joystickInvertLook" ) ? -lookAxisY : lookAxisY;
		const int direction = stickY > 0.5f ? 1 : ( stickY < -0.5f ? -1 : 0 );
		const int now = Sys_Milliseconds();
		if ( direction == 0 ) {
			scoreboardStickDirection = 0;
		} else if ( direction != scoreboardStickDirection || now - scoreboardStickRepeat >= 0 ) {
			ScrollScoreboard( direction );
			scoreboardStickRepeat = now + ( direction != scoreboardStickDirection ? 400 : 120 );
			scoreboardStickDirection = direction;
		}
		lookAxisY = 0.0f;
	} else {
		scoreboardStickDirection = 0;
	}

	if ( IsRunButtonActive() ) {
		anglespeed = usercmdSeconds * in_angleSpeedKey.GetFloat();
	} else {
		anglespeed = usercmdSeconds;
	}

	float joystickLookSensitivity = cvarSystem->GetCVarFloat( "in_joystickLookSensitivity" );
	if ( joystickLookSensitivity <= 0.0f ) {
		joystickLookSensitivity = 1.0f;
	}
	joystickLookSensitivity = idMath::ClampFloat( 0.1f, 4.0f, joystickLookSensitivity ) * GetZoomLookSensitivityScale();
	const bool weaponWheel = ( cmd.buttons & ( BUTTON_WEAPONWHEEL | BUTTON_QUICKCHAT ) ) != 0;
	if ( weaponWheel ) {
		// Wheel navigation has its own sensitivity and must not inherit slow
		// aiming, zoom or run modifiers. These deltas use the unscaled input tic.
		anglespeed = usercmdSeconds;
		joystickLookSensitivity = 1.0f;
	}

	if ( hasDedicatedLookAxis || !ButtonState( UB_STRAFE ) ) {
		// positive axis values mean stick right/down; turning right decreases yaw and
		// looking down increases pitch, matching the mouse and keyboard handling above
		viewangles[YAW] -= anglespeed * ( weaponWheel ? 140.0f : in_yawSpeed.GetFloat() ) * joystickLookSensitivity * lookAxisX;
		viewangles[PITCH] += anglespeed * ( weaponWheel ? 140.0f : in_pitchSpeed.GetFloat() ) * joystickLookSensitivity * lookAxisY;
	}

	if ( hasDedicatedLookAxis ) {
		cmd.rightmove = idMath::ClampChar( cmd.rightmove + moveAxisX );
		cmd.forwardmove = idMath::ClampChar( cmd.forwardmove + moveAxisY );
	} else {
		if ( ButtonState( UB_STRAFE ) ) {
			cmd.rightmove = idMath::ClampChar( cmd.rightmove + moveAxisX );
			cmd.forwardmove = idMath::ClampChar( cmd.forwardmove + moveAxisY );
		}
	}

	cmd.upmove = idMath::ClampChar( cmd.upmove + joystickAxis[AXIS_UP] );
}

/*
==============
idUsercmdGenLocal::CmdButtons
==============
*/
void idUsercmdGenLocal::CmdButtons( void ) {
	int		i;

	cmd.buttons = 0;

	// figure button bits
	for (i = 0 ; i <= 7 ; i++) {
		if ( ButtonState( (usercmdButton_t)( UB_BUTTON0 + i ) ) ) {
			cmd.buttons |= 1 << i;
		}
	}

	// check the attack button
	if ( ButtonState( UB_ATTACK ) ) {
		cmd.buttons |= BUTTON_ATTACK;
	}
	// in_presentationViewProbeAttack holds fire the way a held button would
	if ( in_presentationViewProbeAttack.GetBool() ) {
		cmd.buttons |= BUTTON_ATTACK;
	}

	// check the run button
	if ( IsRunButtonActive() ) {
		cmd.buttons |= BUTTON_RUN;
	}

	// check the zoom button; while the quick-chat wheel is held, zoom switches
	// its channel, so the press passes through as it is
	if ( ButtonState( UB_QUICKCHAT ) ? ButtonState( UB_ZOOM ) != 0 : toggled_zoom.on ) {
		cmd.buttons |= BUTTON_ZOOM;
	}
	// in_presentationViewProbeZoom holds zoom the way a held button would
	if ( in_presentationViewProbeZoom.GetBool() ) {
		cmd.buttons |= BUTTON_ZOOM;
	}

	// check the scoreboard button
	if ( ButtonState( UB_SHOWSCORES ) || ButtonState( UB_IMPULSE19 ) ) {
		// the button is toggled in SP mode as well but without effect
		cmd.buttons |= BUTTON_SCORES;
	}

	// explicitly expose strafe as a button bit for vehicle/gameplay consumers.
	if ( ButtonState( UB_STRAFE ) ) {
		cmd.buttons |= BUTTON_STRAFE;
	}

	// check the mouse look button
	if ( ButtonState( UB_MLOOK ) ^ in_freeLook.GetInteger() ) {
		cmd.buttons |= BUTTON_MLOOK;
	}

	if ( ButtonState( UB_WEAPONWHEEL ) ) {
		cmd.buttons |= BUTTON_WEAPONWHEEL;
	}

	if ( ButtonState( UB_QUICKCHAT ) ) {
		cmd.buttons |= BUTTON_QUICKCHAT;
	}
}

static bool IsWeaponSelectionImpulse( int action ) {
	if ( action >= UB_IMPULSE0 && action <= UB_IMPULSE12 ) {
		return true;
	}

	return action == UB_IMPULSE14 || action == UB_IMPULSE15 || action == UB_IMPULSE51;
}

/*
================
idUsercmdGenLocal::InitCurrent

inits the current command for this frame
================
*/
void idUsercmdGenLocal::InitCurrent( void ) {
	memset( &cmd, 0, sizeof( cmd ) );
	cmd.flags = flags;
	cmd.impulse = impulse;
	cmd.buttons |= in_alwaysRun.GetBool() ? BUTTON_RUN : 0;
	cmd.buttons |= in_freeLook.GetBool() ? BUTTON_MLOOK : 0;
}

/*
================
idUsercmdGenLocal::MakeCurrent

creates the current command for this frame
================
*/
void idUsercmdGenLocal::MakeCurrent( void ) {
	idVec3		oldAngles;
	int		i;

	oldAngles = viewangles;
#ifdef __ANDROID__
	const openq4::touch::Motion touch = touchMotion.Take();
#ifndef OPENQ4_SIGMATOUCH
	SetTouchButtons( touch.buttons );
#endif
#endif
	
	if ( !Inhibited() ) {
#ifdef __ANDROID__
		if ( touch.impulse >= 0 ) TriggerImpulse( touch.impulse );
#endif
		// update toggled key states
		toggled_crouch.SetKeyState( ButtonState( UB_DOWN ), in_toggleCrouch.GetBool() );
		toggled_run.SetKeyState( ButtonState( UB_SPEED ), ShouldToggleRun() );
		if ( !ButtonState( UB_QUICKCHAT ) ) {
			toggled_zoom.SetKeyState( ButtonState( UB_ZOOM ), in_toggleZoom.GetBool() );
		}

		// keyboard angle adjustment
		AdjustAngles();

		// set button bits
		CmdButtons();

		// get basic movement from keyboard
		KeyMove();

		// get basic movement from mouse
		MouseMove();

		// get basic movement from joystick
		JoystickMove();
#ifdef __ANDROID__
		cmd.rightmove = idMath::ClampChar( cmd.rightmove + idMath::FtoiFast( touch.side * 127.0f ) );
		cmd.forwardmove = idMath::ClampChar( cmd.forwardmove + idMath::FtoiFast( touch.forward * 127.0f ) );
		const float touchZoomScale = GetZoomLookSensitivityScale();
		viewangles[YAW] += touch.yaw * touchZoomScale;
		viewangles[PITCH] += touch.pitch * touchZoomScale;
#endif

		// check to make sure the angles haven't wrapped
		if ( viewangles[PITCH] - oldAngles[PITCH] > 90 ) {
			viewangles[PITCH] = oldAngles[PITCH] + 90;
		} else if ( oldAngles[PITCH] - viewangles[PITCH] > 90 ) {
			viewangles[PITCH] = oldAngles[PITCH] - 90;
		} 

		// openQ4 VR: the tracked aim pose owns the angles while a headset
		// session has input focus
		vrUsercmdInput_t vrInput;
		if ( vrSystem != NULL && vrSystem->GetUsercmdInput( viewangles[YAW] - oldAngles[YAW], oldAngles[YAW], vrInput ) ) {
			viewangles[YAW] = vrInput.aimYaw;
			viewangles[PITCH] = vrInput.aimPitch;
			viewangles[ROLL] = 0.0f;
			cmd.forwardmove = idMath::ClampChar( cmd.forwardmove + idMath::FtoiFast( vrInput.forward * 127.0f ) );
			cmd.rightmove = idMath::ClampChar( cmd.rightmove + idMath::FtoiFast( vrInput.right * 127.0f ) );
			if ( vrInput.crouch ) {
				cmd.buttons |= BUTTON_VRCROUCH;
			}
			cmd.buttons |= BUTTON_VRAIM;
		}
	} else {
		mouseDx = 0;
		mouseDy = 0;
#ifdef __ANDROID__
		touchMotion.Block();
#endif
	}

	for ( i = 0; i < 3; i++ ) {
		cmd.angles[i] = ANGLE2SHORT( viewangles[i] );
	}

	cmd.mx = continuousMouseX;
	cmd.my = continuousMouseY;

	flags = cmd.flags;
	impulse = cmd.impulse;

	// late look measures held rates from here to the next usercmd
	lastBuildMsec = Sys_GetClockTicks() * 1000.0 / Sys_ClockTicksPerSecond();
}

//=====================================================================


/*
================
idUsercmdGenLocal::CommandStringUsercmdData

Returns the button if the command string is used by the async usercmd generator.
================
*/
int	idUsercmdGenLocal::CommandStringUsercmdData( const char *cmdString ) {
	for ( userCmdString_t *ucs = userCmdStrings ; ucs->string ; ucs++ ) {
		if ( idStr::Icmp( cmdString, ucs->string ) == 0 ) {
			return ucs->button;
		}
	}

	return ResolveImpulseAction( cmdString );
}

/*
================
idUsercmdGenLocal::ResolveImpulseCommand
================
*/
int idUsercmdGenLocal::ResolveImpulseCommand( const char *cmdString ) const {
	const int action = ResolveImpulseAction( cmdString );
	if ( action < UB_IMPULSE0 || action > UB_IMPULSE127 ) {
		return -1;
	}

	return action - UB_IMPULSE0;
}

/*
================
idUsercmdGenLocal::Init
================
*/
void idUsercmdGenLocal::Init( void ) {
	MigrateLegacyRunDefaults();
	toggled_run.on = in_alwaysRun.GetBool();
	initialized = true;
}

/*
================
idUsercmdGenLocal::InitForNewMap
================
*/
void idUsercmdGenLocal::InitForNewMap( void ) {
	flags = 0;
	impulse = 0;

	toggled_crouch.Clear();
	toggled_run.Clear();
	toggled_zoom.Clear();
	toggled_run.on = in_alwaysRun.GetBool();

	Clear();
	ClearAngles();
	// openQ4 VR: the tracked aim starts again from the zeroed angles
	VR_UsercmdAnglesCleared();
}

/*
================
idUsercmdGenLocal::Shutdown
================
*/
void idUsercmdGenLocal::Shutdown( void ) {
	CloseMouseAccelDebugLog();
	initialized = false;
}

/*
================
idUsercmdGenLocal::Clear
================
*/
void idUsercmdGenLocal::Clear( void ) {
#ifdef __ANDROID__
	touchMotion.Block();
#endif
	// clears all key states 
	memset( buttonState, 0, sizeof( buttonState ) );
	memset( keyState, false, sizeof( keyState ) );
	memset( directButtonState, false, sizeof( directButtonState ) );
	memset( retainedKeyBlocked, false, sizeof( retainedKeyBlocked ) );
	memset( retainedDirectBlocked, false, sizeof( retainedDirectBlocked ) );
	memset( retainedAxisBlocked, false, sizeof( retainedAxisBlocked ) );
	toggled_zoom.Clear();

	inhibitCommands = false;

	mouseDx = mouseDy = 0;
	mouseButton = 0;
	mouseDown = false;

	scoreboardStickDirection = 0;
	scoreboardStickRepeat = 0;
}

/*
================
idUsercmdGenLocal::ClearAngles
================
*/
void idUsercmdGenLocal::ClearAngles( void ) {
	viewangles.Zero();
	ResetMouseFilter();
}

/*
================
idUsercmdGenLocal::TicCmd

Returns a buffered usercmd
================
*/
usercmd_t idUsercmdGenLocal::TicCmd( int ticNumber ) {

	// the packetClient code can legally ask for com_ticNumber+1, because
	// it is in the async code and com_ticNumber hasn't been updated yet,
	// but all other code should never ask for anything > com_ticNumber
	if ( ticNumber > com_ticNumber+1 ) {
		common->Error( "idUsercmdGenLocal::TicCmd ticNumber > com_ticNumber" );
	}

	if ( ticNumber <= com_ticNumber - MAX_BUFFERED_USERCMD ) {
		// this can happen when something in the game code hitches badly, allowing the
		// async code to overflow the buffers
		//common->Printf( "warning: idUsercmdGenLocal::TicCmd ticNumber <= com_ticNumber - MAX_BUFFERED_USERCMD\n" );
	}

	return buffered[ ticNumber & (MAX_BUFFERED_USERCMD-1) ];
}

//======================================================================


/*
===================
idUsercmdGenLocal::Key

Handles async mouse/keyboard button actions
===================
*/
void idUsercmdGenLocal::Key( int keyNum, bool down ) {
	if ( keyNum <= 0 || keyNum >= K_LAST_KEY ) {
		return;
	}
    if (NativeInput_Inhibited()) {
        if (!NativeInput_TypedPollDelivery() && down) nativeUnknownKeyBlocked[keyNum]=true;
        return;
    }
    if (NativeKeyBlocked(keyNum)) return;
	if ( RetainedUI_IsOpen() || retainedKeyBlocked[keyNum] ) {
		retainedKeyBlocked[keyNum] = down;
		return;
	}
	// Sanity check, sometimes we get double message :(
	if ( keyState[ keyNum ] == down ) {
		return;
	}
	// A HUD radial selector owns Back while it is open. Quarantine the
	// matching release too, so an east-button crouch binding cannot leak out
	// when the selector closes on this press. An already-held binding still
	// receives its release; a duplicate press must not steal that release.
	const int radialOwner = cvarSystem->GetCVarInteger( "ui_radialOwner" );
	if ( down && keyNum == K_JOY4 && radialOwner != 0 ) {
		retainedKeyBlocked[keyNum] = true;
		cvarSystem->SetCVarBool( "ui_radialCancel", true );
		return;
	}
	keyState[ keyNum ] = down;

	int action = idKeyInput::GetUsercmdAction( keyNum );
	if ( action < 0 || action >= UB_MAX_BUTTONS ) {
		return;
	}
	if ( down && ( ( radialOwner == 1 && action == UB_WEAPONWHEEL ) || ( radialOwner == 2 && action == UB_QUICKCHAT ) ) ) {
		keyState[keyNum] = false;
		retainedKeyBlocked[keyNum] = true;
		cvarSystem->SetCVarBool( "ui_radialCancel", true );
		return;
	}

	SetButtonAction( action, down );
}

void Usercmd_ArmLatencyFlash( void );

/*
===================
idUsercmdGenLocal::SetButtonAction

Applies a press or release to one usercmd action. Callers are responsible for
never sending two presses or two releases in a row, since buttonState counts
the inputs currently holding the action down.
===================
*/
void idUsercmdGenLocal::SetButtonAction( int action, bool down ) {
	if ( down ) {
		// the press itself, as the event arrives, not the next usercmd build
		if ( action == UB_ATTACK && buttonState[ action ] == 0 ) {
			Usercmd_ArmLatencyFlash();
		}
		if ( action == UB_WEAPONWHEEL || action == UB_QUICKCHAT || IsWeaponSelectionImpulse( action ) ) {
			toggled_zoom.Clear();
		}

		buttonState[ action ]++;

		if ( !Inhibited()  ) {
			if ( ( action == UB_IMPULSE14 || action == UB_IMPULSE15 ) && ScoreboardScrolls() ) {
				ScrollScoreboard( action == UB_IMPULSE14 ? 1 : -1 );
			} else if ( action >= UB_IMPULSE0 && action <= UB_IMPULSE127 ) {
				cmd.impulse = action - UB_IMPULSE0;
				cmd.flags ^= UCF_IMPULSE_SEQUENCE;
			}
		}
	} else {
		buttonState[ action ]--;
		// we might have one held down across an app active transition
		if ( buttonState[ action ] < 0 ) {
			buttonState[ action ] = 0;
		}
	}
}

/*
===================
idUsercmdGenLocal::ScoreboardScrolls

True while the scores control is held and the game reports that the board
it shows lists more rows than fit (ui_scoreboardScrolls).
===================
*/
bool idUsercmdGenLocal::ScoreboardScrolls( void ) {
	return ( ButtonState( UB_SHOWSCORES ) || ButtonState( UB_IMPULSE19 ) ) && ui_scoreboardScrolls.GetBool();
}

/*
===================
idUsercmdGenLocal::ScrollScoreboard
===================
*/
void idUsercmdGenLocal::ScrollScoreboard( int rows ) {
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "scoreboardScroll %d\n", rows ) );
}

/*
===================
idUsercmdGenLocal::SetUsercmdButton

Presses or releases an action directly, for input sources that name the action
rather than a key - the touch controls, which have no key to be rebound.
===================
*/
void idUsercmdGenLocal::SetUsercmdButton( int action, bool down ) {
	if ( action <= UB_NONE || action >= UB_MAX_BUTTONS ) {
		return;
	}
	if ( NativeInput_Inhibited() || RetainedUI_IsOpen() || retainedDirectBlocked[action] ) {
		retainedDirectBlocked[action] = down;
		return;
	}

	// Own held-state, the way Key() has keyState: a repeated press would
	// otherwise leave the action permanently held.
	if ( directButtonState[ action ] == down ) {
		return;
	}
	directButtonState[ action ] = down;

	SetButtonAction( action, down );
}

/*
===================
idUsercmdGenLocal::Mouse
===================
*/
void idUsercmdGenLocal::MouseEvent(int action,int value) {
    if (NativeInput_Inhibited()) {
        if (!NativeInput_TypedPollDelivery() && action>=M_ACTION1 && action<=M_ACTION8 && value)
            nativeUnknownKeyBlocked[K_MOUSE1+(action-M_ACTION1)]=true;
        return;
    }
				if ( action >= M_ACTION1 && action <= M_ACTION8 ) {
					mouseButton = K_MOUSE1 + ( action - M_ACTION1 );
					mouseDown = ( value != 0 );
					Key( mouseButton, mouseDown );
				} else {
					switch ( action ) {
						case M_DELTAX:
							mouseDx += value;
							continuousMouseX += value;
							break;
						case M_DELTAY:
							mouseDy += value;
							continuousMouseY += value;
							break;
						case M_DELTAZ:
							int key = value < 0 ? K_MWHEELDOWN : K_MWHEELUP;
							value = abs( value );
							while( value-- > 0 ) {
								Key( key, true );
								Key( key, false );
								mouseButton = key;
								mouseDown = true;
							}
							break;
					}
				}
}

/*
===================
idUsercmdGenLocal::MouseProbe

in_presentationViewProbe: a development stand-in for a hand on the mouse, so
late look can be measured without synthesising device input. Counts accrue
with host time and enter mouseDx exactly where device motion does, so the
late-look sampler and the next usercmd both see them through the ordinary
sensitivity path. They never move a GUI cursor.
===================
*/
void idUsercmdGenLocal::MouseProbe( void ) {
	const float rate = in_presentationViewProbe.GetFloat();
	if ( rate == 0.0f ) {
		mouseProbeLastMsec = -1.0;
		mouseProbeRemainder = 0.0;
		return;
	}
	const double now = Sys_GetClockTicks() * 1000.0 / Sys_ClockTicksPerSecond();
	if ( mouseProbeLastMsec >= 0.0 && now > mouseProbeLastMsec ) {
		mouseProbeRemainder += rate * ( now - mouseProbeLastMsec ) * 0.001;
		const int counts = static_cast<int>( mouseProbeRemainder );
		mouseProbeRemainder -= counts;
		mouseDx += counts;
	}
	mouseProbeLastMsec = now;
}

void idUsercmdGenLocal::Mouse( void ) {
	MouseProbe();
    if (NativeInput_BeginMouse()) {
        int action=0,value=0;
        while (NativeInput_NextMouse(action,value)) {
            try { MouseEvent(action,value); }
            catch (...) { NativeInput_AbortDelivery(); (void)NativeInput_CompleteMouse(); throw; }
            if (!NativeInput_CompleteMouse()) break;
        }
        NativeInput_EndMouse();
        return;
    }
	int i, numEvents;

	numEvents = Sys_PollMouseInputEvents();

	if ( numEvents ) {
		//
	    // Study each of the buffer elements and process them.
		//
		for( i = 0; i < numEvents; i++ ) {
			int action, value;
			if ( Sys_ReturnMouseInputEvent( i, action, value ) ) {
                MouseEvent(action,value);
			}
		}
	}

	Sys_EndMouseInputEvents();
}

/*
===============
idUsercmdGenLocal::Keyboard
===============
*/
void idUsercmdGenLocal::Keyboard( void ) {
    if (NativeInput_BeginKeyboard()) {
        int key=0;bool down=false;
        while (NativeInput_NextKeyboard(key,down)) {
            try { Key(key,down); }
            catch (...) { NativeInput_AbortDelivery(); (void)NativeInput_CompleteKeyboard(); throw; }
            if (!NativeInput_CompleteKeyboard()) break;
        }
        NativeInput_EndKeyboard();
        return;
    }

	int numEvents = Sys_PollKeyboardInputEvents();

	if ( numEvents ) {
		//
	    // Study each of the buffer elements and process them.
		//
		int key;
		bool state;
		for( int i = 0; i < numEvents; i++ ) {
			if (Sys_ReturnKeyboardInputEvent( i, key, state )) {
				Key ( key, state );
			}
		}
	}

	Sys_EndKeyboardInputEvents();
}

/*
===============
idUsercmdGenLocal::Joystick
===============
*/
void idUsercmdGenLocal::Joystick( void ) {
	int numEvents;

	memset( joystickAxis, 0, sizeof( joystickAxis ) );

	numEvents = Sys_PollJoystickInputEvents();
	for ( int i = 0; i < numEvents; i++ ) {
		int axis;
		int value;
		if ( Sys_ReturnJoystickInputEvent( i, axis, value ) ) {
			if ( axis >= 0 && axis < MAX_JOYSTICK_AXIS ) {
				if ( axis != AXIS_ROLL && ( RetainedUI_IsOpen() || retainedAxisBlocked[axis] ) ) {
					retainedAxisBlocked[axis] = idMath::Abs(value) >= 12;
					continue;
				}
				joystickAxis[ axis ] = idMath::ClampChar( value );
			}
		}
	}

	Sys_EndJoystickInputEvents();
}

void idUsercmdGenLocal::RetainedInputChanged() {
#ifdef __ANDROID__
	touchMotion.Block();
#endif
	for ( int key = 0; key < K_LAST_KEY; ++key )
		// The ordered event path may already have observed a release whose
		// poll sample will be discarded at this handoff. Use that current state
		// instead of keeping an obsolete blocked bit until another release.
		retainedKeyBlocked[key] = keyState[key] || idKeyInput::IsDown(key);
	for ( int action = 0; action < UB_MAX_BUTTONS; ++action )
		retainedDirectBlocked[action] = retainedDirectBlocked[action] || directButtonState[action];
	for ( int axis = 0; axis < MAX_JOYSTICK_AXIS; ++axis ) if ( axis != AXIS_ROLL ) {
		int value = 0; Sys_GetJoystickAxisState(axis,value);
		retainedAxisBlocked[axis] = idMath::Abs(value) >= 12;
	}
	memset(buttonState,0,sizeof(buttonState));
	memset(keyState,0,sizeof(keyState));
	memset(directButtonState,0,sizeof(directButtonState));
	memset(joystickAxis,0,sizeof(joystickAxis));
	mouseDx = mouseDy = 0;
	ResetMouseFilter();
}
void Usercmd_RetainedInputChanged() { localUsercmdGen.RetainedInputChanged(); }

/*
===================
Usercmd_ConsumeLatencyFlash

com_latencyFlash, for click-to-photon sensors such as NVIDIA LDAT and OSLTT:
the console's overlay pass takes the pending press and draws a white bar on
the first frame drawn after the attack button went down, the frame whose
late-look view already reflects input sampled that late.
===================
*/
static bool usercmdLatencyFlashPending = false;
static double usercmdLatencyFlashArmedClock = 0.0;

// The input the current frame's draw took, on the high-resolution clock, so the
// frame can be timed to its swap (Common_RecordFrameInputToSwap, HF-08): the
// first late-look sample and the press behind a drawn latency flash; 0 = none.
static double usercmdFrameLookSampleClock = 0.0;
static double usercmdFrameFlashPressClock = 0.0;

bool Usercmd_ConsumeLatencyFlash( void ) {
	const bool pending = usercmdLatencyFlashPending;
	usercmdLatencyFlashPending = false;
	return pending;
}

void Usercmd_ArmLatencyFlash( void ) {
	usercmdLatencyFlashPending = true;
	usercmdLatencyFlashArmedClock = Sys_GetClockTicks();
}

// the console drew the flash for the press it just consumed
void Usercmd_NoteLatencyFlashDrawn( void ) {
	usercmdFrameFlashPressClock = usercmdLatencyFlashArmedClock;
}

void Usercmd_TakeFrameInputClocks( double &lookSampleClock, double &flashPressClock ) {
	lookSampleClock = usercmdFrameLookSampleClock;
	flashPressClock = usercmdFrameFlashPressClock;
	usercmdFrameLookSampleClock = 0.0;
	usercmdFrameFlashPressClock = 0.0;
}
bool idUsercmdGenLocal::NativeKeyBlocked(int key) const noexcept {
    if(nativeSourceExhausted || nativeUnknownKeyBlocked[key])return true;
    for(const auto& source:nativeHeldSources)if(source.down && source.key==key)return true;
    return false;
}
void idUsercmdGenLocal::NativeInputSource(std::uint64_t route,std::uint64_t window,std::uint64_t device,
    unsigned source,int key,bool down) noexcept {
    if(!route || !window || !source || key<=0 || key>=K_LAST_KEY){nativeSourceExhausted=true;return;}
    nativeHeldSource_t* vacant=nullptr;
    for(auto& held:nativeHeldSources) {
        if(held.route==route && held.window==window && held.device==device && held.source==source) {
            if(held.key!=key){nativeSourceExhausted=true;return;}
            const bool released=held.down && !down;held.down=down;
            // The up-only PreliminaryKeyEvent branch changes one scalar and
            // never dispatches commands, GUI, or input-family callbacks.
            if(released && !NativeKeyBlocked(key) && NativeInput_HeldSourceCurrent(route,window))
                idKeyInput::PreliminaryKeyEvent(key,false);
            return;
        }
        if(!held.down && !vacant)vacant=&held;
    }
    // A replacement route's release cannot clear the original source hold.
    if(down){if(vacant)*vacant={route,window,device,source,key,true};else nativeSourceExhausted=true;}
}
void idUsercmdGenLocal::NativeInputChanged() noexcept {
#ifdef __ANDROID__
    touchMotion.Block();
#endif
    if(NativeInput_Inhibited()) {
        for(int key=1;key<K_LAST_KEY;++key)
            nativeUnknownKeyBlocked[key]=nativeUnknownKeyBlocked[key] || keyState[key] || idKeyInput::IsDown(key);
        // Direct/axis input has no native physical-source join in this slice.
        for(int action=0;action<UB_MAX_BUTTONS;++action)
            retainedDirectBlocked[action]=retainedDirectBlocked[action] || directButtonState[action];
    }
    memset(buttonState,0,sizeof(buttonState));memset(keyState,0,sizeof(keyState));
    memset(directButtonState,0,sizeof(directButtonState));memset(joystickAxis,0,sizeof(joystickAxis));
    mouseDx=mouseDy=0;ResetMouseFilter();
    // Deliberately preserve inhibitCommands, including INHIBIT_SESSION.
}
void Usercmd_NativeInputChanged() noexcept {localUsercmdGen.NativeInputChanged();}
void Usercmd_NativeInputSource(std::uint64_t route,std::uint64_t window,std::uint64_t device,
    unsigned source,int key,bool down) noexcept {localUsercmdGen.NativeInputSource(route,window,device,source,key,down);}


/*
================
idUsercmdGenLocal::UsercmdInterrupt

Called asyncronously
================
*/
void idUsercmdGenLocal::UsercmdInterrupt( void ) {
	// dedicated servers won't create usercmds
	if ( !initialized ) {
		return;
	}

	// init the usercmd for com_ticNumber+1
	InitCurrent();

	// process the system mouse events
	Mouse();

	// process the system keyboard events
	Keyboard();

	// process the system joystick events
	Joystick();

	// create the usercmd for com_ticNumber+1
	MakeCurrent();

	// save a number for debugging cmdDemos and networking
	cmd.sequence = com_ticNumber+1;

	buffered[(com_ticNumber+1) & (MAX_BUFFERED_USERCMD-1)] = cmd;
}

/*
================
idUsercmdGenLocal::GetPresentationViewAngles

Late look. Drains pending mouse input into the same accumulators the next
usercmd consumes and reports the angles that usercmd would carry if it were
built now. Nothing is consumed and no usercmd field changes, so gameplay
input timing is unchanged. The game compares the result with the usercmd it
last applied and turns only the drawn view by the difference.
================
*/
bool idUsercmdGenLocal::GetPresentationViewAngles( float &yaw, float &pitch ) {
	yaw = 0.0f;
	pitch = 0.0f;

	if ( !in_presentationView.GetBool() || !initialized || Inhibited() ) {
		return false;
	}
#ifdef __ANDROID__
	// touch look arrives as a per-usercmd delta with no continuous sample
	return false;
#else
	// Smoothed or filtered views blend several usercmds of motion; showing the
	// raw motion ahead of them would turn the view one way and then settle it
	// another. Those settings keep the authoritative view.
	if ( m_smooth.GetInteger() > 1 || GetMouseFilterSamples() > 0 ) {
		return false;
	}
	// recorded commands drive the game, not this generator
	if ( sessLocal.cmdDemoFile != NULL || sessLocal.readDemo != NULL ) {
		return false;
	}
	// a tracked headset owns the view angles
	if ( vrSystem != NULL && vrSystem->IsActive() ) {
		return false;
	}

	float yawRate = 0.0f;
	float pitchRate = 0.0f;

	// the async tic thread runs the same event drain under this lock
	Sys_EnterCriticalSection();
	Mouse();
	float mx = static_cast<float>( mouseDx );
	float my = static_cast<float>( mouseDy );
	const idVec3 builtAngles = viewangles;
	const double builtMsec = lastBuildMsec;
	// a radial selector turns look input into a cursor, not a view
	const bool radial = ( cmd.buttons & ( BUTTON_WEAPONWHEEL | BUTTON_QUICKCHAT ) ) != 0 ||
		ButtonState( UB_WEAPONWHEEL ) || ButtonState( UB_QUICKCHAT );
	const bool strafe = ButtonState( UB_STRAFE ) != 0;
	const bool mouseLook = ( ButtonState( UB_MLOOK ) ^ in_freeLook.GetInteger() ) != 0;
	const bool haveRate = !radial && PresentationLookRate( yawRate, pitchRate );
	Sys_LeaveCriticalSection();

	if ( radial || builtMsec < 0.0 ) {
		return false;
	}

	yaw = builtAngles[YAW];
	pitch = builtAngles[PITCH];

	// Mouse and gyro motion not yet in a usercmd, scaled exactly as MouseMove
	// will scale it. m_smooth 1 makes its history average the raw delta.
	const int maxMouseDelta = m_maxMouseDelta.GetInteger();
	if ( maxMouseDelta > 0 && ( idMath::Fabs( mx ) > maxMouseDelta || idMath::Fabs( my ) > maxMouseDelta ) ) {
		mx = my = 0.0f;
	}
	if ( mx != 0.0f || my != 0.0f ) {
		float strafeMx = 0.0f;
		float strafeMy = 0.0f;
		const float mouseAxisScale = NormalizeMouseDeltasForCpi( mx, my, strafeMx, strafeMy );
		const float mouseSensitivity = CalculateMouseSensitivity( mx, my, NULL, NULL ) * GetZoomLookSensitivityScale();
		if ( !strafe ) {
			yaw -= m_yaw.GetFloat() * mouseAxisScale * mx * mouseSensitivity;
			if ( mouseLook ) {
				pitch += m_pitch.GetFloat() * mouseAxisScale * my * mouseSensitivity;
			}
		}
	}

	// Look keys and stick deflection are rates the next usercmd integrates over
	// a whole tick; show the share of that tick that has already elapsed.
	if ( haveRate ) {
		const double nowMsec = Sys_GetClockTicks() * 1000.0 / Sys_ClockTicksPerSecond();
		const double elapsedSeconds = ( nowMsec - builtMsec ) * 0.001;
		const double tickSeconds = common->GetUserCmdSec();
		yaw += openq4::PresentationRateShare( yawRate, elapsedSeconds, tickSeconds );
		pitch += openq4::PresentationRateShare( pitchRate, elapsedSeconds, tickSeconds );
	}
	// the draw freezes its first sample; time that one to the swap
	if ( usercmdFrameLookSampleClock <= 0.0 ) {
		usercmdFrameLookSampleClock = Sys_GetClockTicks();
	}
	return true;
#endif
}

/*
================
idUsercmdGenLocal::PresentationLookRate

Degrees per second the next usercmd would turn by from held look keys
(AdjustAngles) and stick deflection (JoystickMove's look branch outside the
radial selectors). It reads current key and axis state without consuming it.
Called with the async lock held.
================
*/
bool idUsercmdGenLocal::PresentationLookRate( float &yawRate, float &pitchRate ) {
	yawRate = 0.0f;
	pitchRate = 0.0f;
	const float runScale = IsRunButtonActive() ? in_angleSpeedKey.GetFloat() : 1.0f;

	if ( !ButtonState( UB_STRAFE ) ) {
		yawRate -= runScale * in_yawSpeed.GetFloat() * ButtonState( UB_RIGHT );
		yawRate += runScale * in_yawSpeed.GetFloat() * ButtonState( UB_LEFT );
	}
	pitchRate -= runScale * in_pitchSpeed.GetFloat() * ButtonState( UB_LOOKUP );
	pitchRate += runScale * in_pitchSpeed.GetFloat() * ButtonState( UB_LOOKDOWN );

	int axes[MAX_JOYSTICK_AXIS];
	for ( int axis = 0; axis < MAX_JOYSTICK_AXIS; ++axis ) {
		int value = 0;
		if ( !Sys_GetJoystickAxisState( axis, value ) || ( axis != AXIS_ROLL && retainedAxisBlocked[axis] ) ) {
			value = 0;
		}
		axes[axis] = idMath::ClampChar( value );
	}
	const bool hasDedicatedLookAxis = axes[AXIS_ROLL] != 0;
	const float lookAxisX = ( hasDedicatedLookAxis ? axes[AXIS_SIDE] : axes[AXIS_YAW] ) * JOYSTICK_AXIS_LOOK_SCALE;
	float lookAxisY = ( hasDedicatedLookAxis ? axes[AXIS_FORWARD] : -axes[AXIS_PITCH] ) * JOYSTICK_AXIS_LOOK_SCALE;
	if ( !hasDedicatedLookAxis && cvarSystem->GetCVarBool( "in_joystickInvertLook" ) ) {
		lookAxisY = -lookAxisY;
	}
	if ( hasDedicatedLookAxis && ScoreboardScrolls() ) {
		// the right stick scrolls the held scoreboard instead of pitching
		lookAxisY = 0.0f;
	}
	float joystickLookSensitivity = cvarSystem->GetCVarFloat( "in_joystickLookSensitivity" );
	if ( joystickLookSensitivity <= 0.0f ) {
		joystickLookSensitivity = 1.0f;
	}
	joystickLookSensitivity = idMath::ClampFloat( 0.1f, 4.0f, joystickLookSensitivity ) * GetZoomLookSensitivityScale();
	if ( hasDedicatedLookAxis || !ButtonState( UB_STRAFE ) ) {
		yawRate -= runScale * in_yawSpeed.GetFloat() * joystickLookSensitivity * lookAxisX;
		pitchRate += runScale * in_pitchSpeed.GetFloat() * joystickLookSensitivity * lookAxisY;
	}
	return yawRate != 0.0f || pitchRate != 0.0f;
}

/*
================
idUsercmdGenLocal::MouseState
================
*/
void idUsercmdGenLocal::MouseState( int *x, int *y, int *button, bool *down ) {
	if ( x != NULL ) {
		*x = continuousMouseX;
	}
	if ( y != NULL ) {
		*y = continuousMouseY;
	}
	if ( button != NULL ) {
		*button = mouseButton;
	}
	if ( down != NULL ) {
		*down = mouseDown;
	}
}

/*
================
idUsercmdGenLocal::GetDirectUsercmd
================
*/
usercmd_t idUsercmdGenLocal::GetDirectUsercmd( void ) {

	// initialize current usercmd
	InitCurrent();

	// process the system mouse events
	Mouse();

	// process the system keyboard events
	Keyboard();
    NativeInput_ContinueDeferred(*eventLoop);

	// process the system joystick events
	Joystick();

	// create the usercmd
	MakeCurrent();

	cmd.duplicateCount = 0;

	return cmd;
}

/*
================
idUsercmdGenLocal::TriggerImpulse
================
*/
void idUsercmdGenLocal::TriggerImpulse( int impulseNum ) {
	if ( impulseNum < IMPULSE_0 || impulseNum > IMPULSE_127 ) {
		return;
	}

	if ( !initialized || session == NULL ) {
		return;
	}

	const char *currentMapName = session->GetCurrentMapName();
	if ( currentMapName == NULL || currentMapName[0] == '\0' ) {
		return;
	}

	impulse = impulseNum;
	flags ^= UCF_IMPULSE_SEQUENCE;
	cmd.impulse = impulse;
	cmd.flags = flags;
}

#ifdef __ANDROID__
/*
================
Sys_SetUsercmdButton

The touch controls have no key that could be rebound, so they name the action
itself; this is what mobile/quake4_bridge.h's Quake4_PostButton reaches. Here
rather than in sys/android because idUsercmdGenLocal never leaves this file.
================
*/
void Sys_SetUsercmdButton( int action, bool down ) {
	Sys_EnterCriticalSection();
	localUsercmdGen.SetUsercmdButton( action, down );
	Sys_LeaveCriticalSection();
}

void idUsercmdGenLocal::SetTouchButtons( unsigned buttons ) {
	static const int actions[QUAKE4_BTN_COUNT] = {
		UB_ATTACK, UB_ZOOM, UB_UP, UB_DOWN, UB_SPEED, UB_STRAFE, UB_WEAPONWHEEL, UB_SHOWSCORES
	};
	for ( int i = 0; i < QUAKE4_BTN_COUNT; ++i ) SetUsercmdButton( actions[i], ( buttons & ( 1u << i ) ) != 0 );
}

void idUsercmdGenLocal::ResetTouchMotion() {
	touchMotion.Clear();
	SetTouchButtons( 0 );
}

void Sys_SetTouchMotion( float side, float forward, float yaw, float pitch, bool active, unsigned buttons, unsigned canceledButtons,
		int nextWeapon, int reload ) {
	Sys_EnterCriticalSection();
	localUsercmdGen.SetTouchMotion( { side, forward, yaw, pitch, active, buttons, canceledButtons, nextWeapon, reload } );
	Sys_LeaveCriticalSection();
}

void Sys_ResetTouchMotion() {
	Sys_EnterCriticalSection();
	localUsercmdGen.ResetTouchMotion();
	Sys_LeaveCriticalSection();
}
#endif
